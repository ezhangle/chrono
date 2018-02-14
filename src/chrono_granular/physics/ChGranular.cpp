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

#include <cuda_runtime.h>
#include "ChGranular.h"
#include "chrono/utils/ChUtilsGenerators.h"
#include "chrono_granular/ChGranularDefines.h"
#include "chrono_granular/physics/ChGranularDefines.cuh"
#include "chrono_granular/utils/ChGranularUtilities_CUDA.cuh"



chrono::ChGRN_DE_Container::~ChGRN_DE_Container() {

    // Clean up, the device side
    if (p_d_CM_X != nullptr) cudaFree(p_d_CM_X);
    if (p_d_CM_Y != nullptr) cudaFree(p_d_CM_Y);
    if (p_d_CM_Z != nullptr) cudaFree(p_d_CM_Z);

    if (p_d_CM_XDOT != nullptr) cudaFree(p_d_CM_XDOT);
    if (p_d_CM_YDOT != nullptr) cudaFree(p_d_CM_YDOT);
    if (p_d_CM_ZDOT != nullptr) cudaFree(p_d_CM_ZDOT);
}

/** The purpose of this method is to figure out how big a SD is, and how many SDs are going to be necessary
    in order to cover the entire BD.
*/
void chrono::ChGRN_DE_MONODISP_SPH_IN_BOX_SMC::partition_BD() {
    double tempDIM = 2.*sphere_radius*AVERAGE_SPHERES_PER_SD_L_DIR;
    unsigned int howMany = (unsigned int)(std::ceil(box_L / tempDIM));
    // work with an even kFac to hit the CM of the box.
    if (howMany & 1) howMany++;
    tempDIM = box_L / howMany;
    SD_L_AD = (unsigned int)std::ceil(tempDIM / SPACE_UNIT);
    nSDs_L_AD = howMany;

    tempDIM = 2.*sphere_radius*AVERAGE_SPHERES_PER_SD_D_DIR;
    howMany = (unsigned int)(std::ceil(box_D / tempDIM));
    // work with an even kFac to hit the CM of the box.
    if (howMany & 1) howMany++;
    tempDIM = box_D / howMany;
    SD_D_AD = (unsigned int)std::ceil(tempDIM / SPACE_UNIT);
    nSDs_D_AD = howMany;

    tempDIM = 2.*sphere_radius*AVERAGE_SPHERES_PER_SD_H_DIR;
    howMany = (unsigned int)(std::ceil(box_H / tempDIM));
    // work with an even kFac to hit the CM of the box.
    if (howMany & 1) howMany++;
    tempDIM = box_H / howMany;
    SD_H_AD = (unsigned int)std::ceil(tempDIM / SPACE_UNIT);
    nSDs_H_AD = howMany;

    nSDs = nSDs_L_AD * nSDs_D_AD * nSDs_H_AD;

    gpuErrchk(cudaMemcpyToSymbol("d_SD_Ldim_AD", &SD_L_AD, sizeof(d_SD_Ldim_AD))); //!< Ad-ed L-dimension of the SD box; make available as a const value onto the GPU
    gpuErrchk(cudaMemcpyToSymbol("d_SD_Ddim_AD", &SD_D_AD, sizeof(d_SD_Ddim_AD))); //!< Ad-ed D-dimension of the SD box; make available as a const value onto the GPU
    gpuErrchk(cudaMemcpyToSymbol("d_SD_Hdim_AD", &SD_H_AD, sizeof(d_SD_Hdim_AD))); //!< Ad-ed H-dimension of the SD box; make available as a const value onto the GPU

}

/** This method sets up the data structures used to perform a simulation.
*
*/
void chrono::ChGRN_MONODISP_SPH_IN_BOX_NOFRIC_SMC::setup_simulation() {

    partition_BD();

    // set aside device memory to store the position of the CM of the spheres
    gpuErrchk(cudaMalloc((void**)& p_d_CM_X, nDEs * sizeof(int)));
    gpuErrchk(cudaMalloc((void**)& p_d_CM_Y, nDEs * sizeof(int)));
    gpuErrchk(cudaMalloc((void**)& p_d_CM_Z, nDEs * sizeof(int)));

    gpuErrchk(cudaMalloc((void**)& p_device_SD_NumOf_DEs_Touching, nDEs * sizeof(unsigned int)));

    gpuErrchk(cudaMalloc((void**)& p_device_DEs_in_SD_composite, MAX_COUNT_OF_DEs_PER_SD * nSDs * sizeof(unsigned int)));

    cudaMemcpyToSymbol("d_monoDisperseSphRadius_AD", &monoDisperseSphRadius_AD, sizeof(d_monoDisperseSphRadius_AD));

    gpuErrchk(cudaMemcpy(p_d_CM_X, h_X_DE.data(), nDEs, cudaMemcpyHostToDevice));
    gpuErrchk(cudaMemcpy(p_d_CM_Y, h_Y_DE.data(), nDEs, cudaMemcpyHostToDevice));
    gpuErrchk(cudaMemcpy(p_d_CM_Z, h_Z_DE.data(), nDEs, cudaMemcpyHostToDevice));

}


void chrono::ChGRN_MONODISP_SPH_IN_BOX_NOFRIC_SMC::generate_DEs()
{

    // Create the falling balls
    float ball_epsilon = monoDisperseSphRadius_AD / 200.f;  // Margin between balls to ensure no overlap / DEM-splosion

    chrono::utils::HCPSampler<float> sampler(2 * monoDisperseSphRadius_AD + ball_epsilon); // Add epsilon

    // We need to pass in half-length box here
    ChVector<float> boxCenter(0, 0, 0);
    ChVector<float> hdims{ float(box_L / (2.* SPACE_UNIT)) , float(box_D / (2.* SPACE_UNIT)) , float(box_H / (2.* SPACE_UNIT)) };
    std::vector<ChVector<float>> points = sampler.SampleBox(boxCenter, hdims);  // Vector of points

    nDEs = points.size();
    std::cout << nDEs << " balls added!" << std::endl;


    h_X_DE.resize(nDEs);
    h_Y_DE.resize(nDEs);
    h_Z_DE.resize(nDEs);
    for (int i = 0; i < nDEs; i++) {
        auto vec = points.at(i);
        h_X_DE.at(i) = (int)vec.x();
        h_Y_DE.at(i) = (int)vec.y();
        h_Z_DE.at(i) = (int)vec.z();
    }

    h_XDOT_DE.resize(nDEs);
    h_YDOT_DE.resize(nDEs);
    h_ZDOT_DE.resize(nDEs);
}