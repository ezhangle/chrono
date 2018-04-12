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
// Authors: Dan Negrut
// =============================================================================

#include <cuda.h>
#include <cuda_runtime.h>
#include "ChGranular.h"
#include "chrono/utils/ChUtilsGenerators.h"
#include "chrono_granular/ChGranularDefines.h"
#include "chrono_granular/utils/ChGranularUtilities_CUDA.cuh"

template <int>
__global__ void primingOperationsRectangularBox(int*, int*, int*, unsigned int*, unsigned int*, unsigned int);

void chrono::granular::ChGRN_MONODISP_SPH_IN_BOX_NOFRIC_SMC::cleanup_simulation() {
    // Handle position level information
    if (p_d_CM_X != nullptr) {
        gpuErrchk(cudaFree(p_d_CM_X));
        p_d_CM_X = nullptr;
    }
    if (p_d_CM_Y != nullptr) {
        gpuErrchk(cudaFree(p_d_CM_Y));
        p_d_CM_Y = nullptr;
    }
    if (p_d_CM_Z != nullptr) {
        gpuErrchk(cudaFree(p_d_CM_Z));
        p_d_CM_Z = nullptr;
    }

    // Handle velocity level information
    if (p_d_CM_XDOT != nullptr) {
        gpuErrchk(cudaFree(p_d_CM_XDOT));
        p_d_CM_XDOT = nullptr;
    }
    if (p_d_CM_YDOT != nullptr) {
        gpuErrchk(cudaFree(p_d_CM_YDOT));
        p_d_CM_YDOT = nullptr;
    }
    if (p_d_CM_ZDOT != nullptr) {
        gpuErrchk(cudaFree(p_d_CM_ZDOT));
        p_d_CM_ZDOT = nullptr;
    }

    // Handle other memory allocated
    if (p_device_SD_NumOf_DEs_Touching != nullptr) {
        gpuErrchk(cudaFree(p_device_SD_NumOf_DEs_Touching));
        p_device_SD_NumOf_DEs_Touching = nullptr;
    }
    if (p_device_DEs_in_SD_composite != nullptr) {
        gpuErrchk(cudaFree(p_device_DEs_in_SD_composite));
        p_device_DEs_in_SD_composite = nullptr;
    }
}

/** This method sets up the data structures used to perform a simulation.
 *
 */
void chrono::granular::ChGRN_MONODISP_SPH_IN_BOX_NOFRIC_SMC::setup_simulation() {
    partition_BD();

    // set aside device memory to store the position of the CM of the spheres
    gpuErrchk(cudaMalloc(&p_d_CM_X, nDEs * sizeof(int)));
    gpuErrchk(cudaMalloc(&p_d_CM_Y, nDEs * sizeof(int)));
    gpuErrchk(cudaMalloc(&p_d_CM_Z, nDEs * sizeof(int)));

    // Set aside memory for velocity information
    gpuErrchk(cudaMalloc(&p_d_CM_XDOT, nDEs * sizeof(int)));
    gpuErrchk(cudaMalloc(&p_d_CM_YDOT, nDEs * sizeof(int)));
    gpuErrchk(cudaMalloc(&p_d_CM_ZDOT, nDEs * sizeof(int)));

    // Set aside memory for velocity update information
    gpuErrchk(cudaMalloc(&p_d_CM_XDOT_update, nDEs * sizeof(int)));
    gpuErrchk(cudaMalloc(&p_d_CM_YDOT_update, nDEs * sizeof(int)));
    gpuErrchk(cudaMalloc(&p_d_CM_ZDOT_update, nDEs * sizeof(int)));

    // allocate mem for array saying for each SD how many spheres touch it
    gpuErrchk(cudaMalloc(&p_device_SD_NumOf_DEs_Touching, nSDs * sizeof(unsigned int)));

    // allocate mem for array that for each SD has the list of all spheres touching it; big array
    gpuErrchk(cudaMalloc(&p_device_DEs_in_SD_composite, MAX_COUNT_OF_DEs_PER_SD * nSDs * sizeof(unsigned int)));

    // Copy over initial position information
    gpuErrchk(cudaMemcpy(p_d_CM_X, h_X_DE.data(), nDEs * sizeof(int), cudaMemcpyHostToDevice));
    gpuErrchk(cudaMemcpy(p_d_CM_Y, h_Y_DE.data(), nDEs * sizeof(int), cudaMemcpyHostToDevice));
    gpuErrchk(cudaMemcpy(p_d_CM_Z, h_Z_DE.data(), nDEs * sizeof(int), cudaMemcpyHostToDevice));

    // Set initial velocities to zero
    gpuErrchk(cudaMemset(p_d_CM_XDOT, 0, nDEs * sizeof(int)));
    gpuErrchk(cudaMemset(p_d_CM_YDOT, 0, nDEs * sizeof(int)));
    gpuErrchk(cudaMemset(p_d_CM_ZDOT, 0, nDEs * sizeof(int)));

    // Set initial velocity updates to zero
    gpuErrchk(cudaMemset(p_d_CM_XDOT_update, 0, nDEs * sizeof(int)));
    gpuErrchk(cudaMemset(p_d_CM_YDOT_update, 0, nDEs * sizeof(int)));
    gpuErrchk(cudaMemset(p_d_CM_ZDOT_update, 0, nDEs * sizeof(int)));
}

// Set the bounds to fill in our box
void chrono::granular::ChGRN_MONODISP_SPH_IN_BOX_NOFRIC_SMC::setFillBounds(float xmin,
                                                                           float ymin,
                                                                           float zmin,
                                                                           float xmax,
                                                                           float ymax,
                                                                           float zmax) {
    boxFillXmin = xmin;
    boxFillYmin = ymin;
    boxFillZmin = zmin;
    boxFillXmax = xmax;
    boxFillYmax = ymax;
    boxFillZmax = zmax;
}

void chrono::granular::ChGRN_MONODISP_SPH_IN_BOX_NOFRIC_SMC::generate_DEs() {
    // Create the falling balls
    float ball_epsilon = monoDisperseSphRadius_SU / 200.f;  // Margin between balls to ensure no overlap / DEM-splosion
    printf("eps is %f, rad is %5f\n", ball_epsilon, monoDisperseSphRadius_SU * 1.0f);

    chrono::utils::HCPSampler<float> sampler(2.4 * monoDisperseSphRadius_SU);  // Add epsilon

    // We need to pass in half-length box here

    // generate from bottom to twice the generateDepth
    // average high and low to get midpoint of generation
    float xmid = box_L * (boxFillXmax + boxFillXmin) / (4. * LENGTH_UNIT);
    float ymid = box_D * (boxFillYmax + boxFillYmin) / (4. * LENGTH_UNIT);
    float zmid = box_H * (boxFillZmax + boxFillZmin) / (4. * LENGTH_UNIT);
    // half-spans in each dimension, the difference
    float xlen = abs(box_L * (boxFillXmax - boxFillXmin) / (4. * LENGTH_UNIT));
    float ylen = abs(box_D * (boxFillYmax - boxFillYmin) / (4. * LENGTH_UNIT));
    float zlen = abs(box_H * (boxFillZmax - boxFillZmin) / (4. * LENGTH_UNIT));
    float generateHalfDepth = box_H / (3. * LENGTH_UNIT);

    // float generateX = -box_D / (2. * LENGTH_UNIT) + generateHalfDepth;
    // float generateY = -box_D / (2. * LENGTH_UNIT) + generateHalfDepth;
    // float generateZ = -box_H / (2. * LENGTH_UNIT) + generateHalfHeight;
    ChVector<float> boxCenter(xmid, ymid, zmid);
    // We need to subtract off a sphere radius to ensure we don't get put at the edge
    ChVector<float> hdims{xlen - monoDisperseSphRadius_SU, ylen - monoDisperseSphRadius_SU,
                          zlen - monoDisperseSphRadius_SU};
    std::vector<ChVector<float>> points = sampler.SampleBox(boxCenter, hdims);  // Vector of points

    nDEs = (unsigned int)points.size();
    std::cout << nDEs << " balls added!" << std::endl;
    // Allocate space for new bodies
    h_X_DE.resize(nDEs);
    h_Y_DE.resize(nDEs);
    h_Z_DE.resize(nDEs);
    // Copy from array of structs to 3 arrays
    for (unsigned int i = 0; i < nDEs; i++) {
        auto vec = points.at(i);
        h_X_DE.at(i) = (int)(vec.x());
        h_Y_DE.at(i) = (int)(vec.y());
        h_Z_DE.at(i) = (int)(vec.z());
        // printf("int is %d, float is %f\n", h_X_DE.at(i), vec.x());
    }
    // Demonstrates issue with half-integer rounding causing specious collisions
    // for (int i = 0; i < nDEs; i++) {
    //     for (int j = 0; j < nDEs; j++) {
    //         if (i == j) {
    //             continue;
    //         }
    //         float r2 = monoDisperseSphRadius_SU * monoDisperseSphRadius_SU;
    //         float dXf = points.at(i).x() - points.at(j).x();
    //         float dYf = points.at(i).y() - points.at(j).y();
    //         float dZf = points.at(i).z() - points.at(j).z();
    //         float d2f = dXf * dXf + dYf * dYf + dZf * dZf;
    //
    //         float dXu = h_X_DE.at(i) - h_X_DE.at(j);
    //         float dYu = h_Y_DE.at(i) - h_Y_DE.at(j);
    //         float dZu = h_Z_DE.at(i) - h_Z_DE.at(j);
    //         float d2u = dXu * dXu + dYu * dYu + dZu * dZu;
    //
    //         float diff = dXf - dXu;
    //         if (fabs(diff) > 0) {
    //             printf("i is %u, j is %u, float dist is %f, %f, %f, int dist is %f, %f, %f, diff is %f\n", i, j, dXf,
    //                    dYf, dZf, dXu, dYu, dZu, d2f - d2u);
    //             // printf("host collision between a %u and b %u, float pen is %f, int pen is %f\n", i, j, d2f - (4 *
    //             // r2),
    //             //        d2u - (4 * r2));
    //         }
    //     }
    // }

    h_XDOT_DE.resize(nDEs);
    h_YDOT_DE.resize(nDEs);
    h_ZDOT_DE.resize(nDEs);
}

/**
This method figures out how big a SD is, and how many SDs are going to be necessary
in order to cover the entire BD.
BD: Bid domain.
SD: Sub-domain.
*/
void chrono::granular::ChGRN_DE_MONODISP_SPH_IN_BOX_SMC::partition_BD() {
    double tempDIM = 2. * sphere_radius * AVERAGE_SPHERES_PER_SD_L_DIR;
    unsigned int howMany = (unsigned int)(std::ceil(box_L / tempDIM));
    // work with an even kFac to hit the CM of the box.
    if (howMany & 1)
        howMany++;
    tempDIM = box_L / howMany;
    SD_L_SU = (unsigned int)std::ceil(tempDIM / LENGTH_UNIT);
    nSDs_L_SU = howMany;

    tempDIM = 2. * sphere_radius * AVERAGE_SPHERES_PER_SD_D_DIR;
    howMany = (unsigned int)(std::ceil(box_D / tempDIM));
    // work with an even kFac to hit the CM of the box.
    if (howMany & 1)
        howMany++;
    tempDIM = box_D / howMany;
    SD_D_SU = (unsigned int)std::ceil(tempDIM / LENGTH_UNIT);
    nSDs_D_SU = howMany;

    tempDIM = 2. * sphere_radius * AVERAGE_SPHERES_PER_SD_H_DIR;
    howMany = (unsigned int)(std::ceil(box_H / tempDIM));
    // work with an even kFac to hit the CM of the box.
    if (howMany & 1)
        howMany++;
    tempDIM = box_H / howMany;
    SD_H_SU = (unsigned int)std::ceil(tempDIM / LENGTH_UNIT);
    nSDs_H_SU = howMany;

    nSDs = nSDs_L_SU * nSDs_D_SU * nSDs_H_SU;
    printf("%u Sds as %u, %u, %u\n", nSDs, nSDs_L_SU, nSDs_D_SU, nSDs_H_SU);

    // Place BD frame at bottom-left corner, one half-length in each direction
    // Can change later if desired
    BD_frame_X = -.5 * (nSDs_L_SU * SD_L_SU);
    BD_frame_Y = -.5 * (nSDs_D_SU * SD_D_SU);
    BD_frame_Z = -.5 * (nSDs_H_SU * SD_H_SU);
    // BD starts at rest
    BD_frame_X_dot = 0;
    BD_frame_Y_dot = 0;
    BD_frame_Z_dot = 0;
}

/**
This method define the mass, time, length Simulation Units. It also sets several other constants that enter the scaling
of various physical quantities set by the user.
*/
void chrono::granular::ChGRN_DE_MONODISP_SPH_IN_BOX_SMC::switch_to_SimUnits() {
    double massSphere = 4. / 3. * M_PI * sphere_radius * sphere_radius * sphere_radius * sphere_density;
    MASS_UNIT = massSphere;
    K_stiffness = (modulusYoung_SPH2SPH > modulusYoung_SPH2WALL ? modulusYoung_SPH2SPH : modulusYoung_SPH2WALL);
    TIME_UNIT = sqrt(massSphere / (PSI_h * K_stiffness)) / PSI_T;

    double magGravAcc = sqrt(X_accGrav * X_accGrav + Y_accGrav * Y_accGrav + Z_accGrav * Z_accGrav);
    LENGTH_UNIT = massSphere * magGravAcc / (PSI_L * K_stiffness);

    monoDisperseSphRadius_SU = sphere_radius / LENGTH_UNIT;
    reciprocal_sphDiam_SU = 1. / (2. * monoDisperseSphRadius_SU);

    float scalingFactor = ((float)PSI_L) / (PSI_T * PSI_T * PSI_h);
    gravAcc_X_factor_SU = scalingFactor * X_accGrav / magGravAcc;
    gravAcc_Y_factor_SU = scalingFactor * Y_accGrav / magGravAcc;
    gravAcc_Z_factor_SU = scalingFactor * Z_accGrav / magGravAcc;
    printf("gravity is %f, %f, %f\n", gravAcc_X_factor_SU, gravAcc_Y_factor_SU, gravAcc_Z_factor_SU);
    printf("mass is %f\n", MASS_UNIT);
}
