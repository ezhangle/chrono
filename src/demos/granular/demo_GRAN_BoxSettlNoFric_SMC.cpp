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
//
// Chrono::Granular demo program using SMC method for frictional contact.
//
// Basic simulation of a settling scenario;
//  - box is rectangular
//  - there is no friction
//
// The global reference frame has X to the right, Y into the screen, Z up.
// The global reference frame located in the left lower corner, close to the viewer.
// =============================================================================

#include <iostream>
#include <string>
#include "chrono/core/ChFileutils.h"
#include "chrono_granular/physics/ChGranular.h"
#include "ChGranular_json_parser.hpp"
#include "chrono/utils/ChUtilsSamplers.h"

using namespace chrono;
using namespace chrono::granular;
using std::cout;
using std::endl;
using std::string;

enum { SETTLING = 0, WAVETANK = 1, BOUNCING_PLATE = 2 };

// -----------------------------------------------------------------------------
// Show command line usage
// -----------------------------------------------------------------------------
void ShowUsage() {
    cout << "usage: ./demo_GRAN_BoxSettlNoFirc_SMC <json_file>" << endl;
}

// -----------------------------------------------------------------------------
// Demo for settling a monodisperse collection of shperes in a rectangular box.
// There is no friction. The units are always cm/s/g[L/T/M].
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    GRN_TIME_STEPPING step_mode = GRN_TIME_STEPPING::FIXED;
    int run_mode = SETTLING;

    sim_param_holder params;

    // Some of the default values might be overwritten by user via command line
    if (argc != 2 || ParseJSON(argv[1], params) == false) {
        ShowUsage();
        return 1;
    }

    // Setup simulation
    ChSystemGranularMonodisperse_SMC_Frictionless settlingExperiment(params.sphere_radius, params.sphere_density);
    settlingExperiment.setBOXdims(params.box_X, params.box_Y, params.box_Z);
    settlingExperiment.set_K_n_SPH2SPH(params.normalStiffS2S);
    settlingExperiment.set_K_n_SPH2WALL(params.normalStiffS2W);
    settlingExperiment.set_Gamma_n_SPH2SPH(params.normalDampS2S);
    settlingExperiment.set_Gamma_n_SPH2WALL(params.normalDampS2W);

    settlingExperiment.set_Gamma_t_SPH2SPH(params.tangentDampS2S);
    settlingExperiment.set_mu_t_SPH2SPH(params.tangentStiffS2S);

    settlingExperiment.set_Cohesion_ratio(params.cohesion_ratio);
    settlingExperiment.set_gravitational_acceleration(params.grav_X, params.grav_Y, params.grav_Z);
    settlingExperiment.setOutputDirectory(params.output_dir);
    settlingExperiment.setOutputMode(params.write_mode);

    // Fill box with bodies
    std::vector<ChVector<float>> body_points;

    chrono::utils::PDSampler<float> sampler(2.05 * params.sphere_radius);

    float center_pt[3] = {0.f, 0.f, -2.05f * params.sphere_radius - params.box_Z / 4};

    // width we want to fill to
    double fill_width = params.box_Z / 4;
    // height that makes this width above the cone
    double fill_height = fill_width;

    // fill to top
    double fill_top = params.box_Z / 2 - 2.05 * params.sphere_radius;
    double fill_bottom = fill_top + 2.05 * params.sphere_radius - fill_height + center_pt[2];

    printf("width is %f, bot is %f, top is %f, height is %f\n", fill_width, fill_bottom, fill_top, fill_height);
    // fill box, layer by layer
    ChVector<> hdims(fill_width - params.sphere_radius, fill_width - params.sphere_radius, 0);
    ChVector<> center(0, 0, fill_bottom);
    // shift up for bottom of box
    center.z() += 3 * params.sphere_radius;

    while (center.z() < fill_top) {
        std::cout << "Create layer at " << center.z() << std::endl;
        auto points = sampler.SampleCylinderZ(center, fill_width - params.sphere_radius, 0);
        body_points.insert(body_points.end(), points.begin(), points.end());
        center.z() += 2.05 * params.sphere_radius;
    }

    settlingExperiment.setParticlePositions(body_points);

    settlingExperiment.set_timeStepping(GRN_TIME_STEPPING::FIXED);
    settlingExperiment.set_timeIntegrator(GRN_TIME_INTEGRATOR::FORWARD_EULER);
    settlingExperiment.set_fixed_stepSize(params.step_size);

    ChFileutils::MakeDirectory(params.output_dir.c_str());

    // TODO clean up this API
    // Prescribe a custom position function for the X direction. Note that this MUST be continuous or the simulation
    // will not be stable. The value is in multiples of box half-lengths in that direction, so an x-value of 1 means
    // that the box will be centered at x = box_size_X
    std::function<double(double)> posFunWave = [](double t) {
        // Start oscillating at t = .5s
        double t0 = .5;
        double freq = .1 * M_PI;

        if (t < t0) {
            return -.5;
        } else {
            return (-.5 + .5 * std::sin((t - t0) * freq));
        }
    };
    // Stay centered at origin
    std::function<double(double)> posFunStill = [](double t) { return -.5; };

    std::function<double(double)> posFunZBouncing = [](double t) {
        // Start oscillating at t = .5s
        double t0 = .5;
        double freq = 20 * M_PI;

        if (t < t0) {
            return -.5;
        } else {
            return (-.5 + .01 * std::sin((t - t0) * freq));
        }
    };

    switch (params.run_mode) {
        case SETTLING:
            settlingExperiment.setBDPositionFunction(posFunStill, posFunStill, posFunStill);
            settlingExperiment.set_BD_Fixed(true);
            break;
        case WAVETANK:
            settlingExperiment.setBDPositionFunction(posFunStill, posFunWave, posFunStill);
            settlingExperiment.set_BD_Fixed(false);
            break;
        case BOUNCING_PLATE:
            settlingExperiment.setBDPositionFunction(posFunStill, posFunStill, posFunZBouncing);
            settlingExperiment.set_BD_Fixed(false);
            break;
    }

    // float hdims[3] = {2.f, 2.f, 2.f};

    settlingExperiment.setVerbose(params.verbose);
    // Finalize settings and initialize for runtime
    settlingExperiment.initialize();
    // settlingExperiment.Create_BC_AABox(hdims, center_pt, false);
    // settlingExperiment.Create_BC_Sphere(center_pt, 3.f, true);
    // settlingExperiment.Create_BC_Cone(center_pt, 1, params.box_Z, center_pt[2] + 10 * params.sphere_radius, true);

    int fps = 100;
    // assume we run for at least one frame
    float frame_step = 1.0f / fps;
    float curr_time = 0;
    int currframe = 0;

    std::cout << "frame step is " << frame_step << std::endl;

    // Run settling experiments
    while (curr_time < params.time_end) {
        settlingExperiment.advance_simulation(frame_step);
        curr_time += frame_step;
        printf("rendering frame %u\n", currframe);
        char filename[100];
        sprintf(filename, "%s/step%06d", params.output_dir.c_str(), currframe++);
        settlingExperiment.writeFileUU(std::string(filename));
    }

    return 0;
}