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
//
// Chrono::Granular demo program using SMC method for frictional contact.
//
// Basic simulation of a settling scenario;
//  - box is rectangular
//  - there is no friction
//
// The global reference frame has X to the right, Y into the screen, Z up.
// The global reference frame located in the left lower corner, close to the viewer.
//
// If available, OpenGL is used for run-time rendering. Otherwise, the
// simulation is carried out for a pre-defined duration and output files are
// generated for post-processing with POV-Ray.
// =============================================================================

#include <iostream>
#include "chrono/core/ChTimer.h"
#include "chrono/utils/ChUtilsGenerators.h"
#include "chrono_granular/physics/ChGranular.h"

#ifdef CHRONO_OPENGL
#include "chrono_opengl/ChOpenGLWindow.h"
#endif
// #define VISUALIZE 1

using namespace chrono;

// Use your own data structure as desired
struct float3 {
    float x, y, z;
};

std::vector<float3> generate_balls(float xdim, float ydim, float zdim) {
    // Create the falling balls
    float ball_epsilon = .005f;  // Margine between balls to ensure no overlap / DEM-splosion
    float ball_radius = 1.;
    // Add epsilon
    utils::HCPSampler<float> sampler((2 + ball_epsilon) * ball_radius);

    // 2-wide box centered at origin
    ChVector<float> parCenter(0, 0, 0);
    ChVector<float> hdims{xdim / 2, ydim / 2, zdim / 2};
    // Super sneaky transform to list of float3's
    std::vector<ChVector<float>> points = sampler.SampleBox(parCenter, hdims);  // Vector of points
    std::vector<float3> pointsf(points.size());
    std::transform(points.begin(), points.end(), pointsf.begin(), [](ChVector<float> vec) {
        float3 ret = {vec.x(), vec.y(), vec.z()};
        return ret;
    });

    return pointsf;
}

// -----------------------------------------------------------------------------
// Demo for settling a monodisperse collection of shperes in a rectangular box.
// There is no friction.
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    auto ball_list = generate_balls(20.f, 20.f, 30.f);
    size_t num_balls = ball_list.size();
    std::cout << num_balls << " balls added!" << std::endl;
    float time_step = 0.00001f;
    double time_end = 6.f;
    std::string output_prefix = "settling_MONODISP_SPHERES_SMC";

    // Material properties
    float Y = 2e5f;
    float wallY = 1e7f;

    ChGRN_MONODISP_SPH_IN_BOX_NOFRIC_SMC experiment(1.f, 80000);
    experiment.setBOXdims(20.f, 20.f, 30.f);
    experiment.YoungModulus_SPH2SPH(200000.f);
    experiment.YoungModulus_SPH2WALL(10000000.f);
    experiment.settle(10.f);
    return 0;
}
