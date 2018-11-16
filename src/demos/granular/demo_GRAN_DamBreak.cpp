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
// Chrono::Granular demo program using SMC method for frictional contact for a
// Dam Break Simulation
//
// The global reference frame has X to the right, Y into the screen, Z up.
// The global reference frame located in the left lower corner, close to the viewer.
// =============================================================================

#include <iostream>
#include <string>
#include "chrono/core/ChFileutils.h"
#include "chrono_granular/physics/ChGranular.h"
#include "ChGranular_json_parser.hpp"
#include "ChGranularDemoUtils.hpp"
#include "chrono/utils/ChUtilsSamplers.h"
#include "ChGranularDemoUtils.hpp"

using namespace chrono;
using namespace chrono::granular;
using std::cout;
using std::endl;
using std::string;

enum { SETTLING = 0, WAVETANK = 1, BOUNCING_PLATE = 2 };

// expected number of args for param sweep
constexpr int num_args_full = 6;

// -----------------------------------------------------------------------------
// Show command line usage
// -----------------------------------------------------------------------------
void ShowUsage() {
    cout << "usage: ./demo_GRAN_DamBreak <json_file> [<radius> <dt> <length_Y> <output_dir>]" << endl;
    cout << "must have either 1 or " << num_args_full - 1 << " arguments" << endl;
}

// -----------------------------------------------------------------------------
// Demo for settling a monodisperse collection of shperes in a rectangular box.
// There is no friction. The units are always cm/s/g[L/T/M].
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    GRAN_TIME_STEPPING step_mode = GRAN_TIME_STEPPING::FIXED;
    int run_mode = SETTLING;

    sim_param_holder params;

    // Some of the default values might be overwritten by user via command line
    if (argc < 2 || argc > 2 && argc != num_args_full || ParseJSON(argv[1], params) == false) {
        ShowUsage();
        return 1;
    }

    if (argc == num_args_full) {
        params.sphere_radius = std::atof(argv[2]);
        params.step_size = std::atof(argv[3]);
        params.box_Y = std::atof(argv[4]);
        params.output_dir = std::string(argv[5]);
        printf("new parameters: r is %f, dt is %f, y is %f, %s\n", params.sphere_radius, params.step_size, params.box_Y,
               params.output_dir.c_str());
    }

    // Setup simulation
    ChSystemGranular_MonodisperseSMC gran_system(params.sphere_radius, params.sphere_density);
    gran_system.setBOXdims(params.box_X, params.box_Y, params.box_Z);
    gran_system.set_K_n_SPH2SPH(params.normalStiffS2S);
    gran_system.set_K_n_SPH2WALL(params.normalStiffS2W);
    gran_system.set_Gamma_n_SPH2SPH(params.normalDampS2S);
    gran_system.set_Gamma_n_SPH2WALL(params.normalDampS2W);

    gran_system.set_K_t_SPH2SPH(params.tangentStiffS2S);
    gran_system.set_K_t_SPH2WALL(params.tangentStiffS2W);
    gran_system.set_Gamma_t_SPH2SPH(params.tangentDampS2S);
    gran_system.set_Gamma_t_SPH2WALL(params.tangentDampS2W);

    gran_system.set_Cohesion_ratio(params.cohesion_ratio);
    gran_system.set_Adhesion_ratio_S2W(params.adhesion_ratio_s2w);
    gran_system.set_gravitational_acceleration(params.grav_X, params.grav_Y, params.grav_Z);
    gran_system.setOutputDirectory(params.output_dir);
    gran_system.setOutputMode(params.write_mode);

    gran_system.set_timeStepping(GRAN_TIME_STEPPING::FIXED);
    gran_system.set_timeIntegrator(GRAN_TIME_INTEGRATOR::FORWARD_EULER);
    gran_system.set_friction_mode(GRAN_FRICTION_MODE::FRICTIONLESS);
    gran_system.set_contactModel(GRAN_CONTACT_MODEL::HOOKE);
    gran_system.set_fixed_stepSize(params.step_size);
    gran_system.setVerbose(params.verbose);

    gran_system.set_BD_Fixed(true);

    // offset of radius from walls
    ChVector<float> rad_offset = 1.02f * params.sphere_radius * ChVector<float>(1, 1, 1);
    // (2 x 1 x 1) box (x,y,z)
    float sphere_diam = 2.f * params.sphere_radius;

    float max_z_fill = 2. * 100.;
    ChVector<float> hdims = .5f * ChVector<float>(2. * 100., params.box_Y, max_z_fill) - rad_offset;

    // start at bottom left corner
    ChVector<float> center =
        ChVector<float>(-params.box_X / 2., -params.box_Y / 2., -params.box_Z / 2.) + hdims + rad_offset;

    // Fill box with bodies
    std::vector<ChVector<float>> body_points =
        PDLayerSampler_BOX<float>(center, hdims, 2. * params.sphere_radius, 1.02);

    std::vector<ChVector<float>> first_points;
    // first_points.push_back(body_points.at(10000));
    gran_system.setParticlePositions(body_points);
    // just at end of material
    float plane_center[3] = {center.x() + hdims.x() + sphere_diam, 0, 0};
    // face in -y, hold material in
    float plane_normal[3] = {-1, 0, 0};

    printf("center is %f, %f, %f, plane center is is %f, %f, %f\n", center[0], center[1], center[2], plane_center[0],
           plane_center[1], plane_center[2]);
    size_t plane_bc_id = gran_system.Create_BC_Plane(plane_center, plane_normal);

    ChFileutils::MakeDirectory(params.output_dir.c_str());

    // Finalize settings and initialize for runtime
    gran_system.initialize();
    // gran_system.disable_BC_by_ID(plane_bc_id);

    int fps = 100;
    // assume we run for at least one frame
    float frame_step = 1. / fps;
    float curr_time = 0;
    int currframe = 0;

    std::cout << "frame step is " << frame_step << std::endl;
    bool plane_active = true;

    // Run settling experiments
    while (curr_time < params.time_end) {
        if (plane_active && curr_time > 1) {
            printf("disabling plane!\n");
            plane_active = false;
            gran_system.disable_BC_by_ID(plane_bc_id);
        }
        gran_system.advance_simulation(frame_step);
        curr_time += frame_step;
        printf("rendering frame %u\n", currframe);
        char filename[100];
        sprintf(filename, "%s/step%06d", params.output_dir.c_str(), currframe++);
        gran_system.writeFileUU(std::string(filename));
    }

    return 0;
}