// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Radu Serban
// =============================================================================
//
// RoboSimian on rigid terrain
//
// =============================================================================

#include <cmath>
#include <cstdio>
#include <vector>
#include <iostream>
#include <string>

#include "chrono/core/ChFileutils.h"
#include "chrono/utils/ChUtilsInputOutput.h"

#include "chrono/physics/ChSystemNSC.h"
#include "chrono/physics/ChSystemSMC.h"

#include "robosimian.h"

#include "chrono/physics/ChForce.h"

#include "chrono/utils/ChUtilsSamplers.h"

#include "chrono/timestepper/ChTimestepper.h"

#include "chrono_granular/physics/ChGranular.h"

#include "chrono_granular/physics/ChGranularTriMesh.h"

#include "chrono/utils/ChUtilsCreators.h"

#include "../ChGranular_json_parser.hpp"

extern std::vector<std::pair<std::string, std::shared_ptr<chrono::ChBodyAuxRef>>> mesh_body_list;

using namespace chrono;
using namespace chrono::collision;
using namespace chrono::granular;

double time_step = 1e-4;

// Drop the robot on rigid terrain
bool drop = true;

// Phase durations
double duration_pose = 1;            // Interval to assume initial pose
double duration_settle_robot = 0.5;  // Interval to allow robot settling on terrain
double duration_sim = 60;            // Duration of actual locomotion simulation

// Output frequencies
double output_fps = 100;
double render_fps = 100;

enum RUN_MODE { DRIVING = 0, WALKING = 1, INCHWORM = 2, SLED = 3 };

// =============================================================================

class RobotDriverCallback : public robosimian::Driver::PhaseChangeCallback {
  public:
    RobotDriverCallback(robosimian::RoboSimian* robot) : m_robot(robot), m_start_x(0), m_start_time(0) {}
    virtual void OnPhaseChange(robosimian::Driver::Phase old_phase, robosimian::Driver::Phase new_phase) override;

    double GetDistance() const;
    double GetDuration() const;
    double GetAvgSpeed() const;

    double m_start_x;
    double m_start_time;

  private:
    robosimian::RoboSimian* m_robot;
};

void RobotDriverCallback::OnPhaseChange(robosimian::Driver::Phase old_phase, robosimian::Driver::Phase new_phase) {
    if (new_phase == robosimian::Driver::CYCLE && old_phase != robosimian::Driver::CYCLE) {
        m_start_x = m_robot->GetChassisPos().x();
        m_start_time = m_robot->GetSystem()->GetChTime();
    }
}

double RobotDriverCallback::GetDistance() const {
    return m_robot->GetChassisPos().x() - m_start_x;
}

double RobotDriverCallback::GetDuration() const {
    return m_robot->GetSystem()->GetChTime() - m_start_time;
}

double RobotDriverCallback::GetAvgSpeed() const {
    return GetDistance() / GetDuration();
}

void ShowUsage() {
    cout << "usage: ./test_RS_robosimian_granular <json_file>" << endl;
}

// Take a ChBody and write its
void writeMeshFrames(std::ostringstream& outstream,
                     ChBodyAuxRef& body,
                     std::string obj_name,
                     float mesh_scaling,
                     ChVector<> terrain_offset) {
    // these don't actually do anything, don't render them
    if (obj_name == "robosimian/obj/robosim_fts.obj") {
        return;
    }

    // Write the mesh name to find
    outstream << obj_name << ",";

    // Get frame position
    ChFrame<> body_frame = body.GetFrame_REF_to_abs();

    ChVector<> offset;

    // this is to compensate for a Radu bug, note that we offset this mesh explicitly for some reason
    if (obj_name == "robosimian/obj/robosim_wheel_mount.obj") {
        offset = ChVector<>(0.12024, 0, 0);
    }
    ChQuaternion<> rot = body_frame.GetRot();
    ChVector<> pos = (body_frame.GetPos() + terrain_offset + rot.Rotate(offset)) * mesh_scaling;
    // Get basis vectors
    ChVector<> vx = rot.GetXaxis();
    ChVector<> vy = rot.GetYaxis();
    ChVector<> vz = rot.GetZaxis();

    // Output in order
    outstream << pos.x() << ",";
    outstream << pos.y() << ",";
    outstream << pos.z() << ",";
    outstream << vx.x() << ",";
    outstream << vx.y() << ",";
    outstream << vx.z() << ",";
    outstream << vy.x() << ",";
    outstream << vy.y() << ",";
    outstream << vy.z() << ",";
    outstream << vz.x() << ",";
    outstream << vz.y() << ",";
    outstream << vz.z();
    outstream << "\n";
}

// =============================================================================

int main(int argc, char* argv[]) {
    sim_param_holder params;
    // Some of the default values might be overwritten by user via command line
    if (argc != 2 || ParseJSON(argv[1], params) == false) {
        ShowUsage();
        return 1;
    }
    // ------------
    // Timed events
    // ------------

    double time_create_terrain = duration_pose;                       // create terrain after robot assumes initial pose
    double time_start = time_create_terrain + duration_settle_robot;  // start actual simulation after robot settling
    double time_end = time_start + duration_sim;                      // end simulation after specified duration

    // -------------
    // Create system
    // -------------

    ////ChSystemSMC my_sys;
    ChSystemNSC my_sys;

    my_sys.SetMaxItersSolverSpeed(200);
    if (my_sys.GetContactMethod() == ChMaterialSurface::NSC)
        my_sys.SetSolverType(ChSolver::Type::BARZILAIBORWEIN);

    my_sys.Set_G_acc(ChVector<double>(0, 0, -9.8));
    ////my_sys.Set_G_acc(ChVector<double>(0, 0, 0));

    // -----------------------
    // Create RoboSimian robot
    // -----------------------

    robosimian::RoboSimian robot(&my_sys, true, true);

    // Initialize Robosimian robot

    robot.Initialize(ChCoordsys<>(ChVector<>(0, 0, 0), Q_from_AngX(CH_C_PI)));
    robot.SetCollide(false);  // no collision outside of granular material

    std::vector<std::pair<std::string, std::shared_ptr<ChBodyAuxRef>>> gran_collision_bodies;
    std::shared_ptr<robosimian::Driver> driver;
    // -----------------------------------
    // Create a driver and attach to robot
    // -----------------------------------
    switch (params.run_mode) {
        case RUN_MODE::WALKING:
            driver = std::make_shared<robosimian::Driver>(
                "",                                                           // start input file
                GetChronoDataFile("robosimian/actuation/walking_cycle.txt"),  // cycle input file
                "",                                                           // stop input file
                true);
            break;
        case RUN_MODE::DRIVING:
            driver = std::make_shared<robosimian::Driver>(
                GetChronoDataFile("robosimian/actuation/driving_start.txt"),  // start input file
                GetChronoDataFile("robosimian/actuation/driving_cycle.txt"),  // cycle input file
                GetChronoDataFile("robosimian/actuation/driving_stop.txt"),   // stop input file
                true);
            break;
        case RUN_MODE::INCHWORM:
            driver = std::make_shared<robosimian::Driver>(
                GetChronoDataFile("robosimian/actuation/inchworming_start.txt"),  // start input file
                GetChronoDataFile("robosimian/actuation/inchworming_cycle.txt"),  // cycle input file
                GetChronoDataFile("robosimian/actuation/inchworming_stop.txt"),   // stop input file
                true);
        case RUN_MODE::SLED:
            driver = std::make_shared<robosimian::Driver>(
                GetChronoDataFile("robosimian/actuation/sculling_start.txt"),   // start input file
                GetChronoDataFile("robosimian/actuation/sculling_cycle2.txt"),  // cycle input file
                GetChronoDataFile("robosimian/actuation/sculling_stop.txt"),    // stop input file
                true);
            gran_collision_bodies.push_back(std::pair<std::string, std::shared_ptr<ChBodyAuxRef>>(
                "robosimian/obj/robosim_sled_coll.obj", robot.GetSledBody()));
            break;
    }

    RobotDriverCallback cbk(&robot);
    driver->RegisterPhaseChangeCallback(&cbk);

    driver->SetTimeOffsets(duration_pose, duration_settle_robot);
    robot.SetDriver(driver);

    // ---------------------------------
    // Run simulation for specified time
    // ---------------------------------

    int output_steps = (int)std::ceil((1.0 / output_fps) / time_step);
    int render_steps = (int)std::ceil((1.0 / render_fps) / time_step);
    int sim_frame = 0;
    int output_frame = 0;
    int render_frame = 0;

    bool terrain_created = false;

    double iteration_step = 1e-4;

    // Mesh values
    string wheel_mesh_filename = string("robosimian/obj/grousery_wheel.obj");

    float3 scaling;
    scaling.x = 100;
    scaling.y = 100;
    scaling.z = 100;

    auto limbs = robot.GetLimbs();
    gran_collision_bodies.push_back(std::pair<std::string, std::shared_ptr<ChBodyAuxRef>>(
        wheel_mesh_filename, limbs[robosimian::FR]->GetWheelBody()));
    gran_collision_bodies.push_back(std::pair<std::string, std::shared_ptr<ChBodyAuxRef>>(
        wheel_mesh_filename, limbs[robosimian::FL]->GetWheelBody()));
    gran_collision_bodies.push_back(std::pair<std::string, std::shared_ptr<ChBodyAuxRef>>(
        wheel_mesh_filename, limbs[robosimian::RR]->GetWheelBody()));
    gran_collision_bodies.push_back(std::pair<std::string, std::shared_ptr<ChBodyAuxRef>>(
        wheel_mesh_filename, limbs[robosimian::RL]->GetWheelBody()));

    unsigned int num_mesh_bodies = gran_collision_bodies.size();

    std::vector<string> mesh_filenames;
    std::vector<float3> mesh_scalings;

    // add mesh to granular system
    for (unsigned int i = 0; i < num_mesh_bodies; i++) {
        mesh_scalings.push_back(scaling);
    }
    for (unsigned int i = 0; i < num_mesh_bodies; i++) {
        mesh_filenames.push_back(gran_collision_bodies[i].first);
    }

    // Setup granular simulation
    ChSystemGranularMonodisperse_SMC_Frictionless_trimesh m_sys_gran(params.sphere_radius, params.sphere_density);
    m_sys_gran.setBOXdims(params.box_X, params.box_Y, params.box_Z);

    // Fill box with bodies
    std::vector<ChVector<float>> body_points;

    double fill_bottom = -params.box_Z / 2;
    double fill_top = 2.05 * params.sphere_radius;
    chrono::utils::PDSampler<float> sampler(2.05 * params.sphere_radius);

    // fill box, layer by layer
    ChVector<> hdims(params.box_X / 2 - params.sphere_radius, params.box_Y / 2 - params.sphere_radius, 0);
    ChVector<> center(0, 0, fill_bottom);
    // shift up for bottom of box
    center.z() += 3 * params.sphere_radius;

    while (center.z() < fill_top) {
        std::cout << "Create layer at " << center.z() << std::endl;
        auto points = sampler.SampleBox(center, hdims);
        body_points.insert(body_points.end(), points.begin(), points.end());
        center.z() += 2.05 * params.sphere_radius;
    }

    m_sys_gran.setParticlePositions(body_points);

    m_sys_gran.set_BD_Fixed(true);

    m_sys_gran.set_K_n_SPH2SPH(params.normalStiffS2S);
    m_sys_gran.set_K_n_SPH2WALL(params.normalStiffS2W);
    m_sys_gran.set_K_n_SPH2MESH(params.normalStiffS2M);
    m_sys_gran.set_Gamma_n_SPH2SPH(params.normalDampS2S);
    m_sys_gran.set_Gamma_n_SPH2WALL(params.normalDampS2S);
    m_sys_gran.setPsiFactors(params.psi_T, params.psi_h, params.psi_L);
    m_sys_gran.set_Gamma_n_SPH2MESH(params.normalDampS2M);
    m_sys_gran.set_Cohesion_ratio(params.cohesion_ratio);
    m_sys_gran.set_gravitational_acceleration(params.grav_X, params.grav_Y, params.grav_Z);
    m_sys_gran.set_timeStepping(GRN_TIME_STEPPING::FIXED);
    m_sys_gran.set_timeIntegrator(GRN_TIME_INTEGRATOR::CHUNG);
    m_sys_gran.set_fixed_stepSize(params.step_size);

    m_sys_gran.load_meshes(mesh_filenames, mesh_scalings);

    m_sys_gran.disableMeshCollision();  // disable meshes for settling

    /// output preferences
    m_sys_gran.setOutputDirectory(params.output_dir);
    m_sys_gran.setOutputMode(params.write_mode);
    m_sys_gran.setVerbose(params.verbose);
    ChFileutils::MakeDirectory(params.output_dir.c_str());

    unsigned int nSoupFamilies = m_sys_gran.nMeshesInSoup();
    cout << nSoupFamilies << " soup families" << endl;
    double* meshSoupLocOri = new double[7 * nSoupFamilies];

    m_sys_gran.initialize();
    int currframe = 0;

    unsigned int out_fps = 100;
    unsigned int out_steps = 1 / (out_fps * iteration_step);

    // robot should be 100 away from -x boundary of box

    double robot_offset_x = 0;  //(-.5 * params.box_X + 100.) / 100.;
    printf("x offset is %f\n", robot_offset_x);

    // account for the frame difference between robot and terrain
    ChVector<> robot_granular_offset(robot_offset_x, 0, 1.5);  // 1.5;

    double curr_time = 0;
    while (curr_time < time_end && curr_time < params.time_end) {
        if (drop && !terrain_created && my_sys.GetChTime() > time_create_terrain) {
            // Set terrain height to be _just_ below wheel
            double wheel_z = robot.GetWheelPos(robosimian::FR).z() - 0.13;

            // double z = wheel_z - 1;  // put solid terrain well below rover
            double max_gran_z = m_sys_gran.get_max_z() / 100;
            // we want the wheels just above terrain height
            robot_granular_offset.z() = -wheel_z + max_gran_z;
            printf("new z offset is %f\n", robot_granular_offset.z());

            // add meshes back in
            m_sys_gran.enableMeshCollision();

            // Release robot
            robot.GetChassis()->GetBody()->SetBodyFixed(false);

            terrain_created = true;
        }

        if (sim_frame % output_steps == 0) {
            robot.Output();
        }

        // empty forces on each wheel
        for (unsigned int i = 0; i < num_mesh_bodies; i++) {
            auto wheel = gran_collision_bodies[i].second;
            wheel->Empty_forces_accumulators();
        }
        // update each mesh in gpu code
        for (unsigned int i = 0; i < num_mesh_bodies; i++) {
            auto mesh = gran_collision_bodies[i].second;
            auto mesh_pos = mesh->GetPos();
            auto mesh_rot = mesh->GetRot();

            unsigned int body_family_offset = i * 7;

            meshSoupLocOri[body_family_offset + 0] = (robot_granular_offset.x() + mesh_pos.x()) * 100;
            meshSoupLocOri[body_family_offset + 1] = (robot_granular_offset.y() + mesh_pos.y()) * 100;
            meshSoupLocOri[body_family_offset + 2] = (robot_granular_offset.z() + mesh_pos.z()) * 100;
            meshSoupLocOri[body_family_offset + 3] = mesh_rot[0];
            meshSoupLocOri[body_family_offset + 4] = mesh_rot[1];
            meshSoupLocOri[body_family_offset + 5] = mesh_rot[2];
            meshSoupLocOri[body_family_offset + 6] = mesh_rot[3];
        }
        m_sys_gran.meshSoup_applyRigidBodyMotion(meshSoupLocOri);  // Apply the mesh orientation data to the mesh

        float mesh_forces[6 * num_mesh_bodies];
        m_sys_gran.collectGeneralizedForcesOnMeshSoup(mesh_forces);
        // Apply forces to the mesh for the duration of the iteration
        for (unsigned int i = 0; i < num_mesh_bodies; i++) {
            auto mesh = gran_collision_bodies[i].second;

            auto mesh_pos = mesh->GetPos();
            auto mesh_rot = mesh->GetRot();

            unsigned int body_family_offset = i * 6;

            double F_cgs_to_SI = 1e-5;
            double r_cgs_to_SI = 1e-2;

            mesh->Accumulate_force(
                F_cgs_to_SI * ChVector<>(mesh_forces[body_family_offset + 0], mesh_forces[body_family_offset + 1],
                                         mesh_forces[body_family_offset + 2]),
                mesh_pos, false);
            mesh->Accumulate_torque(r_cgs_to_SI * F_cgs_to_SI *
                                        ChVector<>(mesh_forces[body_family_offset + 3],
                                                   mesh_forces[body_family_offset + 4], mesh_forces[5]),
                                    false);
            if (sim_frame % render_steps == 0) {
                cout << "wheel " << i << " pos(" << mesh_pos.x() << ", " << mesh_pos.y() << ", " << mesh_pos.z() << ") "
                     << endl;
                cout << "force (" << mesh_forces[0] << ", " << mesh_forces[1] << ", " << mesh_forces[2] << "); torque ("
                     << mesh_forces[3] << ", " << mesh_forces[4] << ", " << mesh_forces[5] << ")" << endl;
            }
        }

        // Output POV-Ray date and/or snapshot images
        if (sim_frame % render_steps == 0) {
            cout << "Rendering frame " << render_frame << endl;
            char filename[100];
            sprintf(filename, "%s/step%06d", params.output_dir.c_str(), render_frame);
            m_sys_gran.writeFileUU(string(filename));
            // // write some VTKs for debug
            m_sys_gran.write_meshes(string(filename));

            // write mesh transforms for ospray renderer
            char mesh_output[100];
            sprintf(mesh_output, "%s/step%06d_meshes.csv", params.output_dir.c_str(), render_frame);
            std::ofstream meshfile{string(mesh_output)};
            std::ostringstream outstream;
            outstream << "mesh_name,dx,dy,dz,x1,x2,x3,y1,y2,y3,z1,z2,z3\n";

            // write each mesh to the output file
            for (auto b : mesh_body_list) {
                writeMeshFrames(outstream, *(b.second), b.first, scaling.z, robot_granular_offset);
            }
            meshfile << outstream.str();

            render_frame++;
        }

        m_sys_gran.advance_simulation(time_step);

        robot.DoStepDynamics(time_step);

        curr_time += time_step;

        sim_frame++;
    }

    std::cout << "avg. speed: " << cbk.GetAvgSpeed() << std::endl;
    delete[] meshSoupLocOri;

    return 0;
}