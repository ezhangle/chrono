// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2018 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Dan Negrut, Nic Olsen
// =============================================================================

#include <vector>
#include <algorithm>
#include <fstream>
#include <string>
#ifdef _WINDOWS
#define _USE_MATH_DEFINES
#include <math.h>
#endif
#include "chrono/physics/ChGlobal.h"
#include "chrono/core/ChVector.h"
#include "chrono/core/ChQuaternion.h"
#include "chrono/core/ChMatrix33.h"
#include "chrono/geometry/ChTriangle.h"
#include "chrono/geometry/ChTriangleMeshConnected.h"
#include "chrono_granular/utils/ChGranularUtilities.h"
#include "ChGranularTriMesh.h"

namespace chrono {
namespace granular {

ChSystemGranular_MonodisperseSMC_trimesh::ChSystemGranular_MonodisperseSMC_trimesh(float radiusSPH,
                                                                                   float density,
                                                                                   float3 boxDims)
    : ChSystemGranular_MonodisperseSMC(radiusSPH, density, boxDims),
      K_n_s2m_UU(0),
      K_t_s2m_UU(0),
      Gamma_n_s2m_UU(0),
      Gamma_t_s2m_UU(0),
      rolling_coeff_s2m_UU(0),
      adhesion_s2m_over_gravity(0) {
    // Allocate triangle collision parameters
    gpuErrchk(cudaMallocManaged(&tri_params, sizeof(ChGranParams_trimesh), cudaMemAttachGlobal));

    // Allocate the device soup storage
    gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE, sizeof(ChTriangleSoup<float3>), cudaMemAttachGlobal));
    // start with no triangles
    meshSoup_DEVICE->nTrianglesInSoup = 0;
    meshSoup_DEVICE->numTriangleFamilies = 0;

    set_static_friction_coeff_SPH2MESH(0);
}

ChSystemGranular_MonodisperseSMC_trimesh::~ChSystemGranular_MonodisperseSMC_trimesh() {
    // work to do here
    cleanupTriMesh_DEVICE();
}
double ChSystemGranular_MonodisperseSMC_trimesh::get_max_K() const {
    return std::max(std::max(K_n_s2s_UU, K_n_s2w_UU), K_n_s2m_UU);
}

void ChSystemGranular_MonodisperseSMC_trimesh::initializeTriangles() {
    double K_SU2UU = MASS_SU2UU / (TIME_SU2UU * TIME_SU2UU);
    double GAMMA_SU2UU = 1. / TIME_SU2UU;

    tri_params->K_n_s2m_SU = K_n_s2m_UU / K_SU2UU;
    tri_params->K_t_s2m_SU = K_t_s2m_UU / K_SU2UU;

    tri_params->Gamma_n_s2m_SU = Gamma_n_s2m_UU / GAMMA_SU2UU;
    tri_params->Gamma_t_s2m_SU = Gamma_t_s2m_UU / GAMMA_SU2UU;

    double magGravAcc = sqrt(X_accGrav * X_accGrav + Y_accGrav * Y_accGrav + Z_accGrav * Z_accGrav);
    tri_params->adhesionAcc_s2m = adhesion_s2m_over_gravity * magGravAcc;

    for (unsigned int fam = 0; fam < meshSoup_DEVICE->numTriangleFamilies; fam++) {
        meshSoup_DEVICE->familyMass_SU[fam] = meshSoup_DEVICE->familyMass_SU[fam] / MASS_SU2UU;
        meshSoup_DEVICE->inflation_radii[fam] = meshSoup_DEVICE->inflation_radii[fam] / LENGTH_SU2UU;
    }

    double rolling_scalingFactor = 1.;
    if (gran_params->rolling_mode == GRAN_ROLLING_MODE::VISCOUS) {
        rolling_scalingFactor = 1. / TIME_SU2UU;
    }
    tri_params->rolling_coeff_s2m_SU = rolling_scalingFactor * rolling_coeff_s2m_UU;

    TRACK_VECTOR_RESIZE(SD_numTrianglesTouching, nSDs, "SD_numTrianglesTouching", 0);
    TRACK_VECTOR_RESIZE(SD_TriangleCompositeOffsets, nSDs, "SD_TriangleCompositeOffsets", 0);

    // TODO do we have a good heuristic???
    // this gets resized on-the-fly every timestep
    TRACK_VECTOR_RESIZE(triangles_in_SD_composite, 0, "triangles_in_SD_composite", 0);
}
void ChSystemGranular_MonodisperseSMC_trimesh::initialize() {
    initializeSpheres();
    initializeTriangles();
}

void ChSystemGranular_MonodisperseSMC_trimesh::load_meshes(std::vector<std::string> objfilenames,
                                                           std::vector<float3> scalings,
                                                           std::vector<float> masses,
                                                           std::vector<bool> inflated,
                                                           std::vector<float> inflation_radii) {
    unsigned int size = objfilenames.size();
    if (size != scalings.size() || size != masses.size() || size != inflated.size() || size != inflation_radii.size()) {
        GRANULAR_ERROR("Vectors of obj files, scalings, and masses must have same size\n");
    }

    if (size == 0) {
        printf("WARNING: No meshes provided!\n");
    }

    unsigned int nTriangles = 0;
    unsigned int numTriangleFamilies = 0;
    std::vector<geometry::ChTriangleMeshConnected> all_meshes;
    for (unsigned int i = 0; i < objfilenames.size(); i++) {
        printf("importing %s\n", objfilenames[i].c_str());
        all_meshes.push_back(geometry::ChTriangleMeshConnected());
        geometry::ChTriangleMeshConnected& mesh = all_meshes[all_meshes.size() - 1];

        mesh.LoadWavefrontMesh(GetChronoDataFile(objfilenames[i]), true, false);
        mesh.Transform({0, 0, 0}, ChMatrix33<>(ChVector<>(scalings[i].x, scalings[i].y, scalings[i].z)));

        nTriangles += mesh.getNumTriangles();
        numTriangleFamilies++;
    }

    printf("nTriangles is %u\n", nTriangles);
    printf("nTriangleFamiliesInSoup is %u\n", numTriangleFamilies);

    // Allocate memory to store mesh soup in unified memory
    printf("Allocating mesh unified memory\n");
    setupTriMesh_DEVICE(all_meshes, nTriangles, masses, inflated, inflation_radii);
    printf("Done allocating mesh unified memory\n");

    // Allocate triangle collision memory
}

// result = rot_mat * p + pos
template <class T>
ChVector<T> ChSystemGranular_MonodisperseSMC_trimesh::ApplyFrameTransform(ChVector<T>& p, T* pos, T* rot_mat) {
    ChVector<T> result;

    // Apply rotation matrix to point
    result[0] = rot_mat[0] * p[0] + rot_mat[1] * p[1] + rot_mat[2] * p[2];
    result[1] = rot_mat[3] * p[0] + rot_mat[4] * p[1] + rot_mat[5] * p[2];
    result[2] = rot_mat[6] * p[0] + rot_mat[7] * p[1] + rot_mat[8] * p[2];

    // Apply translation
    result[0] += pos[0];
    result[1] += pos[1];
    result[2] += pos[2];

    return result;
}

void ChSystemGranular_MonodisperseSMC_trimesh::write_meshes(std::string filename) {
    if (file_write_mode == GRAN_OUTPUT_MODE::NONE) {
        return;
    }

    printf("Writing meshes\n");
    std::ofstream outfile(filename + "_mesh.vtk", std::ios::out);
    std::ostringstream ostream;
    ostream << "# vtk DataFile Version 1.0\n";
    ostream << "Unstructured Grid Example\n";
    ostream << "ASCII\n";
    ostream << "\n\n";

    ostream << "DATASET UNSTRUCTURED_GRID\n";
    ostream << "POINTS " << meshSoup_DEVICE->nTrianglesInSoup * 3 << " float\n";

    // Write all vertices
    for (unsigned int tri_i = 0; tri_i < meshSoup_DEVICE->nTrianglesInSoup; tri_i++) {
        ChVector<float> p1(meshSoup_DEVICE->node1[tri_i].x, meshSoup_DEVICE->node1[tri_i].y,
                           meshSoup_DEVICE->node1[tri_i].z);
        ChVector<float> p2(meshSoup_DEVICE->node2[tri_i].x, meshSoup_DEVICE->node2[tri_i].y,
                           meshSoup_DEVICE->node2[tri_i].z);
        ChVector<float> p3(meshSoup_DEVICE->node3[tri_i].x, meshSoup_DEVICE->node3[tri_i].y,
                           meshSoup_DEVICE->node3[tri_i].z);

        unsigned int fam = meshSoup_DEVICE->triangleFamily_ID[tri_i];
        p1 = ApplyFrameTransform<float>(p1, tri_params->fam_frame_broad[fam].pos,
                                        tri_params->fam_frame_broad[fam].rot_mat);
        p2 = ApplyFrameTransform<float>(p2, tri_params->fam_frame_broad[fam].pos,
                                        tri_params->fam_frame_broad[fam].rot_mat);
        p3 = ApplyFrameTransform<float>(p3, tri_params->fam_frame_broad[fam].pos,
                                        tri_params->fam_frame_broad[fam].rot_mat);

        ostream << p1.x() << " " << p1.y() << " " << p1.z() << "\n";
        ostream << p2.x() << " " << p2.y() << " " << p2.z() << "\n";
        ostream << p3.x() << " " << p3.y() << " " << p3.z() << "\n";
    }

    ostream << "\n\n";
    ostream << "CELLS " << meshSoup_DEVICE->nTrianglesInSoup << " " << 4 * meshSoup_DEVICE->nTrianglesInSoup << "\n";
    for (unsigned int tri_i = 0; tri_i < meshSoup_DEVICE->nTrianglesInSoup; tri_i++) {
        ostream << "3 " << 3 * tri_i << " " << 3 * tri_i + 1 << " " << 3 * tri_i + 2 << "\n";
    }

    ostream << "\n\n";
    ostream << "CELL_TYPES " << meshSoup_DEVICE->nTrianglesInSoup << "\n";
    for (unsigned int tri_i = 0; tri_i < meshSoup_DEVICE->nTrianglesInSoup; tri_i++) {
        ostream << "9\n";
    }

    outfile << ostream.str();
}

void ChSystemGranular_MonodisperseSMC_trimesh::cleanupTriMesh_DEVICE() {
    cudaFree(meshSoup_DEVICE->triangleFamily_ID);
    cudaFree(meshSoup_DEVICE->familyMass_SU);
    cudaFree(meshSoup_DEVICE->inflated);
    cudaFree(meshSoup_DEVICE->inflation_radii);

    cudaFree(meshSoup_DEVICE->node1);
    cudaFree(meshSoup_DEVICE->node2);
    cudaFree(meshSoup_DEVICE->node3);

    cudaFree(meshSoup_DEVICE->vel);
    cudaFree(meshSoup_DEVICE->omega);

    cudaFree(meshSoup_DEVICE->generalizedForcesPerFamily);
    cudaFree(tri_params->fam_frame_broad);
    cudaFree(tri_params->fam_frame_narrow);
    cudaFree(meshSoup_DEVICE);
    cudaFree(tri_params);
}

void ChSystemGranular_MonodisperseSMC_trimesh::setupTriMesh_DEVICE(
    const std::vector<geometry::ChTriangleMeshConnected>& all_meshes,
    unsigned int nTriangles,
    std::vector<float> masses,
    std::vector<bool> inflated,
    std::vector<float> inflation_radii) {
    meshSoup_DEVICE->nTrianglesInSoup = nTriangles;

    if (nTriangles != 0) {
        // Allocate all of the requisite pointers
        gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE->triangleFamily_ID, nTriangles * sizeof(unsigned int),
                                    cudaMemAttachGlobal));

        gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE->node1, nTriangles * sizeof(float3), cudaMemAttachGlobal));
        gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE->node2, nTriangles * sizeof(float3), cudaMemAttachGlobal));
        gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE->node3, nTriangles * sizeof(float3), cudaMemAttachGlobal));
    }

    printf("Done allocating nodes for %d triangles\n", nTriangles);

    // Setup the clean copy of the mesh soup from the obj file data
    unsigned int family = 0;
    unsigned int tri_i = 0;
    // for each obj file data set
    for (auto mesh : all_meshes) {
        int n_triangles_mesh = mesh.getNumTriangles();
        for (int i = 0; i < n_triangles_mesh; i++) {
            geometry::ChTriangle tri = mesh.getTriangle(i);

            meshSoup_DEVICE->node1[tri_i] = make_float3(tri.p1.x(), tri.p1.y(), tri.p1.z());
            meshSoup_DEVICE->node2[tri_i] = make_float3(tri.p2.x(), tri.p2.y(), tri.p2.z());
            meshSoup_DEVICE->node3[tri_i] = make_float3(tri.p3.x(), tri.p3.y(), tri.p3.z());

            meshSoup_DEVICE->triangleFamily_ID[tri_i] = family;

            // Normal of a single vertex... Should still work
            int normal_i = mesh.m_face_n_indices.at(i).x();  // normals at each vertex of this triangle
            ChVector<double> normal = mesh.m_normals[normal_i];

            // Generate normal using RHR from nodes 1, 2, and 3
            ChVector<double> AB = tri.p2 - tri.p1;
            ChVector<double> AC = tri.p3 - tri.p1;
            ChVector<double> cross;
            cross.Cross(AB, AC);

            // If the normal created by a RHR traversal is not correct, switch two vertices
            if (cross.Dot(normal) < 0) {
                std::swap(meshSoup_DEVICE->node2[tri_i], meshSoup_DEVICE->node3[tri_i]);
            }
            tri_i++;
        }
        family++;
        printf("Done writing family %d\n", family);
    }

    meshSoup_DEVICE->numTriangleFamilies = family;

    if (meshSoup_DEVICE->nTrianglesInSoup != 0) {
        gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE->familyMass_SU, family * sizeof(float), cudaMemAttachGlobal));
        gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE->inflated, family * sizeof(float), cudaMemAttachGlobal));
        gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE->inflation_radii, family * sizeof(float), cudaMemAttachGlobal));

        for (unsigned int i = 0; i < family; i++) {
            // NOTE The SU conversion is done in initialize after the scaling is determined
            meshSoup_DEVICE->familyMass_SU[i] = masses[i];
            meshSoup_DEVICE->inflated[i] = inflated[i];
            meshSoup_DEVICE->inflation_radii[i] = inflation_radii[i];
        }

        gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE->generalizedForcesPerFamily,
                                    6 * meshSoup_DEVICE->numTriangleFamilies * sizeof(float), cudaMemAttachGlobal));
        // Allocate memory for the float and double frames
        gpuErrchk(cudaMallocManaged(&tri_params->fam_frame_broad,
                                    meshSoup_DEVICE->numTriangleFamilies * sizeof(ChFamilyFrame<float>),
                                    cudaMemAttachGlobal));
        gpuErrchk(cudaMallocManaged(&tri_params->fam_frame_narrow,
                                    meshSoup_DEVICE->numTriangleFamilies * sizeof(ChFamilyFrame<double>),
                                    cudaMemAttachGlobal));

        // Allocate memory for linear and angular velocity
        gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE->vel, meshSoup_DEVICE->numTriangleFamilies * sizeof(float3),
                                    cudaMemAttachGlobal));
        gpuErrchk(cudaMallocManaged(&meshSoup_DEVICE->omega, meshSoup_DEVICE->numTriangleFamilies * sizeof(float3),
                                    cudaMemAttachGlobal));

        for (unsigned int i = 0; i < family; i++) {
            meshSoup_DEVICE->vel[i] = make_float3(0, 0, 0);
            meshSoup_DEVICE->omega[i] = make_float3(0, 0, 0);
        }
    }
}

void ChSystemGranular_MonodisperseSMC_trimesh::collectGeneralizedForcesOnMeshSoup(float* genForcesOnSoup) {
    // pull directly from unified memory
    for (unsigned int i = 0; i < 6 * meshSoup_DEVICE->numTriangleFamilies; i += 6) {
        // Divide by C_F to go from SU to UU
        genForcesOnSoup[i + 0] = meshSoup_DEVICE->generalizedForcesPerFamily[i + 0] * FORCE_SU2UU;
        genForcesOnSoup[i + 1] = meshSoup_DEVICE->generalizedForcesPerFamily[i + 1] * FORCE_SU2UU;
        genForcesOnSoup[i + 2] = meshSoup_DEVICE->generalizedForcesPerFamily[i + 2] * FORCE_SU2UU;

        // Divide by C_TAU to go from SU to UU
        genForcesOnSoup[i + 3] = meshSoup_DEVICE->generalizedForcesPerFamily[i + 3] * TORQUE_SU2UU;
        genForcesOnSoup[i + 4] = meshSoup_DEVICE->generalizedForcesPerFamily[i + 4] * TORQUE_SU2UU;
        genForcesOnSoup[i + 5] = meshSoup_DEVICE->generalizedForcesPerFamily[i + 5] * TORQUE_SU2UU;
    }
}

void ChSystemGranular_MonodisperseSMC_trimesh::meshSoup_applyRigidBodyMotion(double* position_orientation_data,
                                                                             float* vel) {
    // Set both broadphase and narrowphase frames for each family
    for (unsigned int fam = 0; fam < meshSoup_DEVICE->numTriangleFamilies; fam++) {
        generate_rot_matrix<float>(position_orientation_data + 7 * fam + 3, tri_params->fam_frame_broad[fam].rot_mat);
        tri_params->fam_frame_broad[fam].pos[0] = (float)position_orientation_data[7 * fam + 0];
        tri_params->fam_frame_broad[fam].pos[1] = (float)position_orientation_data[7 * fam + 1];
        tri_params->fam_frame_broad[fam].pos[2] = (float)position_orientation_data[7 * fam + 2];

        generate_rot_matrix<double>(position_orientation_data + 7 * fam + 3, tri_params->fam_frame_narrow[fam].rot_mat);
        tri_params->fam_frame_narrow[fam].pos[0] = position_orientation_data[7 * fam + 0];
        tri_params->fam_frame_narrow[fam].pos[1] = position_orientation_data[7 * fam + 1];
        tri_params->fam_frame_narrow[fam].pos[2] = position_orientation_data[7 * fam + 2];

        // Set linear and angular velocity
        const float C_V = TIME_SU2UU / LENGTH_SU2UU;
        meshSoup_DEVICE->vel[fam] = make_float3(C_V * vel[6 * fam + 0], C_V * vel[6 * fam + 1], C_V * vel[6 * fam + 2]);
        const float C_O = TIME_SU2UU;
        meshSoup_DEVICE->omega[fam] =
            make_float3(C_O * vel[6 * fam + 3], C_O * vel[6 * fam + 4], C_O * vel[6 * fam + 5]);
    }
}

template <typename T>
void ChSystemGranular_MonodisperseSMC_trimesh::generate_rot_matrix(double* ep, T* rot_mat) {
    rot_mat[0] = (T)(2 * (ep[0] * ep[0] + ep[1] * ep[1] - 0.5));
    rot_mat[1] = (T)(2 * (ep[1] * ep[2] - ep[0] * ep[3]));
    rot_mat[2] = (T)(2 * (ep[1] * ep[3] + ep[0] * ep[2]));
    rot_mat[3] = (T)(2 * (ep[1] * ep[2] + ep[0] * ep[3]));
    rot_mat[4] = (T)(2 * (ep[0] * ep[0] + ep[2] * ep[2] - 0.5));
    rot_mat[5] = (T)(2 * (ep[2] * ep[3] - ep[0] * ep[1]));
    rot_mat[6] = (T)(2 * (ep[1] * ep[3] - ep[0] * ep[2]));
    rot_mat[7] = (T)(2 * (ep[2] * ep[3] + ep[0] * ep[1]));
    rot_mat[8] = (T)(2 * (ep[0] * ep[0] + ep[3] * ep[3] - 0.5));
}

}  // namespace granular
}  // namespace chrono