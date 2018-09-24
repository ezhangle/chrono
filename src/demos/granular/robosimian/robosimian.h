// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All right reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Author: Radu Serban
// =============================================================================
//
// =============================================================================

#ifndef ROBO_SIMIAN_H
#define ROBO_SIMIAN_H

#include <array>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "chrono/assets/ChColor.h"
#include "chrono/physics/ChLinkMotorRotation.h"
#include "chrono/physics/ChSystem.h"

namespace robosimian {

// -----------------------------------------------------------------------------
// Various definitions
// -----------------------------------------------------------------------------

enum LimbID {
    FR = 0,  ///< front right
    RR = 1,  ///< rear right
    RL = 2,  ///< rear left
    FL = 3   ///< front left
};

enum class VisualizationType {
    NONE,       ///< no visualization
    COLLISION,  ///< render primitive collision shapes
    MESH        ///< render meshes
};

namespace CollisionFamily {
enum Enum {
    LIMB_FR = 1,  ///< front-right limb
    LIMB_RR = 2,  ///< rear-right limb
    LIMB_RL = 3,  ///< rear-left limb
    LIMB_FL = 4,  ///< front-left limb
    CHASSIS = 5,  ///< chassis (torso)
    SLED = 6,     ///< sled
    WHEEL_DD = 7  ///< direct-drive wheels
};
}

namespace CollisionFlags {
enum Enum {
    NONE = 0,          ///< no collision shapes on any body
    CHASSIS = 1 << 0,  ///< chassis (torso)
    SLED = 1 << 1,     ///< sled
    LIMBS = 1 << 2,    ///< all limb bodies (Excluding final wheels)
    WHEELS = 1 << 3,   ///< all wheels
    ALL = 0xFFFF       ///< collision enabled on all bodies
};
}

enum class ActuationMode {
    ANGLE,  ///< prescribe time-series for joint angle
    SPEED   ///< prescribe time-series for joint angular speed
};

enum class LocomotionMode {
    WALK,      ///<
    SCULL,     ///<
    INCHWORM,  ///<
    DRIVE      ///<
};

// -----------------------------------------------------------------------------
// Definition of a part (body + collision shapes + visualization assets)
// -----------------------------------------------------------------------------

struct BoxShape {
    BoxShape(const chrono::ChVector<>& pos, const chrono::ChQuaternion<>& rot, const chrono::ChVector<>& dims)
        : m_pos(pos), m_rot(rot), m_dims(dims) {}
    chrono::ChVector<> m_pos;
    chrono::ChQuaternion<> m_rot;
    chrono::ChVector<> m_dims;
};

struct SphereShape {
    SphereShape(const chrono::ChVector<>& pos, double radius) : m_pos(pos), m_radius(radius) {}
    chrono::ChVector<> m_pos;
    double m_radius;
};

struct CylinderShape {
    CylinderShape(const chrono::ChVector<>& pos, const chrono::ChQuaternion<>& rot, double radius, double length)
        : m_pos(pos), m_rot(rot), m_radius(radius), m_length(length) {}
    chrono::ChVector<> m_pos;
    chrono::ChQuaternion<> m_rot;
    double m_radius;
    double m_length;
};

struct MeshShape {
    enum Type { CONVEX_HULL, TRIANGLE_SOUP, NODE_CLOUD };
    MeshShape(const chrono::ChVector<>& pos, const chrono::ChQuaternion<>& rot, const std::string& name, Type type)
        : m_pos(pos), m_rot(rot), m_name(name), m_type(type) {}
    chrono::ChVector<> m_pos;
    chrono::ChQuaternion<> m_rot;
    std::string m_name;
    Type m_type;
};

class Part {
  public:
    Part(const std::string& name, chrono::ChSystem* system);
    virtual ~Part() {}

    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }
    void SetVisualizationType(VisualizationType vis);

    std::shared_ptr<chrono::ChBodyAuxRef> GetBody() const { return m_body; }
    const chrono::ChVector<>& GetPos() const { return m_body->GetFrame_REF_to_abs().GetPos(); }
    const chrono::ChQuaternion<>& GetRot() const { return m_body->GetFrame_REF_to_abs().GetRot(); }

  protected:
    void AddVisualizationAssets(VisualizationType vis);
    void AddCollisionShapes();

    std::string m_name;                            ///< subsystem name
    std::shared_ptr<chrono::ChBodyAuxRef> m_body;  ///< rigid body
    std::vector<BoxShape> m_boxes;                 ///< set of collision boxes
    std::vector<SphereShape> m_spheres;            ///< set of collision spheres
    std::vector<CylinderShape> m_cylinders;        ///< set of collision cylinders
    std::vector<MeshShape> m_meshes;               ///< set of collision meshes
    std::string m_mesh_name;                       ///< visualization mesh name
    chrono::ChVector<> m_offset;                   ///< offset for visualization mesh
    chrono::ChColor m_color;                       ///< visualization asset color

    friend class RoboSimian;
    friend class Limb;
};

// -----------------------------------------------------------------------------
// Robot chassis (torso)
// -----------------------------------------------------------------------------

class Chassis : public Part {
  public:
    Chassis(const std::string& name, chrono::ChSystem* system, bool fixed);
    ~Chassis() {}

    /// Initialize the chassis at the specified (absolute) position.
    void Initialize(const chrono::ChCoordsys<>& pos);

    /// Enable/disable collision for the sled (Default: false).
    void SetCollide(bool state);

  private:
    /// Translate the chassis by the specified value.
    void Translate(const chrono::ChVector<>& shift);

    bool m_collide;

    friend class RoboSimian;
};

// -----------------------------------------------------------------------------
// Robot sled (fixed to chassis)
// -----------------------------------------------------------------------------

class Sled : public Part {
  public:
    Sled(const std::string& name, chrono::ChSystem* system);
    ~Sled() {}

    /// Initialize the sled at the specified position (relative to the chassis).
    void Initialize(std::shared_ptr<chrono::ChBodyAuxRef> chassis,  ///< chassis body
                    const chrono::ChVector<>& xyz,                  ///< location (relative to chassis)
                    const chrono::ChVector<>& rpy                   ///< roll-pitch-yaw (relative to chassis)
    );

    /// Enable/disable collision for the sled (default: true).
    void SetCollide(bool state);

  private:
    /// Translate the sled by the specified value.
    void Translate(const chrono::ChVector<>& shift);

    bool m_collide;

    friend class RoboSimian;
};

// -----------------------------------------------------------------------------
// Direct-drive robot wheels (not used in current model)
// -----------------------------------------------------------------------------

class WheelDD : public Part {
  public:
    WheelDD(const std::string& name, int id, chrono::ChSystem* system);
    ~WheelDD() {}

    /// Initialize the direct-drive wheel at the specified position (relative to the chassis).
    void Initialize(std::shared_ptr<chrono::ChBodyAuxRef> chassis,  ///< chassis body
                    const chrono::ChVector<>& xyz,                  ///< location (relative to chassis)
                    const chrono::ChVector<>& rpy                   ///< roll-pitch-yaw (relative to chassis)
    );
};

// -----------------------------------------------------------------------------
// Robot limb components
// -----------------------------------------------------------------------------

class Link {
  public:
    Link(const std::string& mesh_name,
         const chrono::ChVector<>& offset,
         const chrono::ChColor& color,
         double mass,
         const chrono::ChVector<>& com,
         const chrono::ChVector<>& inertia_xx,
         const chrono::ChVector<>& inertia_xy,
         const std::vector<CylinderShape>& shapes)
        : m_mesh_name(mesh_name),
          m_offset(offset),
          m_color(color),
          m_mass(mass),
          m_com(com),
          m_inertia_xx(inertia_xx),
          m_inertia_xy(inertia_xy),
          m_shapes(shapes) {}

  private:
    std::string m_mesh_name;
    chrono::ChVector<> m_offset;
    chrono::ChColor m_color;
    double m_mass;
    chrono::ChVector<> m_com;
    chrono::ChVector<> m_inertia_xx;
    chrono::ChVector<> m_inertia_xy;
    std::vector<CylinderShape> m_shapes;

    friend class Limb;
};

struct LinkData {
    std::string name;
    Link link;
    bool include;
};

struct JointData {
    std::string name;
    std::string linkA;
    std::string linkB;
    bool fixed;
    chrono::ChVector<> xyz;
    chrono::ChVector<> rpy;
    chrono::ChVector<> axis;
};

class Limb {
  public:
    Limb(const std::string& name, LimbID id, const LinkData data[], chrono::ChSystem* system);
    ~Limb() {}

    /// Initialize the limb at the specified position (relative to the chassis).
    void Initialize(std::shared_ptr<chrono::ChBodyAuxRef> chassis,  ///< chassis body
                    const chrono::ChVector<>& xyz,                  ///< location (relative to chassis)
                    const chrono::ChVector<>& rpy,                  ///< roll-pitch-yaw (relative to chassis)
                    CollisionFamily::Enum collision_family,         ///< collision family
                    ActuationMode wheel_mode                        ///< motor type for wheel actuation
    );

    /// Set visualization type for all limb links.
    void SetVisualizationType(VisualizationType vis);

    /// Enable/disable collision on all links, except final wheel (default: false).
    void SetCollideLinks(bool state);

    /// Enable/disable collision for final wheel (default: true).
    void SetCollideWheel(bool state);

    /// Get a handle to the wheel body.
    std::shared_ptr<chrono::ChBodyAuxRef> GetWheelBody() const { return m_wheel->GetBody(); }

    /// Get location of the wheel body.
    const chrono::ChVector<>& GetWheelPos() const { return m_wheel->GetPos(); }

    /// Get angular velocity of the wheel body (expressed in local coordinates).
    chrono::ChVector<> GetWheelAngVelocity() const { return m_wheel->GetBody()->GetWvel_loc(); }

    /// Get wheel angle.
    double GetWheelAngle() const { return m_wheel_motor->GetMotorRot(); }

    /// Get wheel angular speed.
    double GetWheelOmega() const { return m_wheel_motor->GetMotorRot_dt(); }

    /// Get angle for specified motor.
    /// Motors are named "joint1", "joint2", ... , "joint8", starting at the chassis.
    double GetMotorAngle(const std::string& motor_name) const;

    /// Get angular speed for specified motor.
    /// Motors are named "joint1", "joint2", ... , "joint8", starting at the chassis.
    double GetMotorOmega(const std::string& motor_name) const;

    /// Get actuator reaction torque [Nm] for specified motor.
    /// Motors are named "joint1", "joint2", ... , "joint8", starting at the chassis.
    double GetMotorTorque(const std::string& motor_name) const;

    /// Get angles for all 8 limb motors.
    std::array<double, 8> GetMotorAngles();

    /// Get angular velocities for all 8 limb motors.
    std::array<double, 8> GetMotorOmegas();

    /// Get actuator torques for all 8 limb motors.
    std::array<double, 8> GetMotorTorques();

    /// Get current motor actuations.
    void GetMotorActuations(std::array<double, 8>& angles, std::array<double, 8>& speeds);

    /// Set activation for given motor at current time.
    void Activate(const std::string& motor_name, double time, double val);

    /// Set activations for all motors at current time.
    void Activate(double time, const std::array<double, 8>& vals);

  private:
    /// Translate the limb bodies by the specified value.
    void Translate(const chrono::ChVector<>& shift);

    LimbID m_id;
    std::string m_name;
    std::unordered_map<std::string, std::shared_ptr<Part>> m_links;
    std::unordered_map<std::string, std::shared_ptr<chrono::ChLink>> m_joints;
    std::unordered_map<std::string, std::shared_ptr<chrono::ChLinkMotorRotation>> m_motors;
    std::shared_ptr<Part> m_wheel;
    std::shared_ptr<chrono::ChLinkMotorRotation> m_wheel_motor;

    bool m_collide_links;  ///< collide flag for all links (except final wheel)
    bool m_collide_wheel;  ///< collide flag for the final wheel

    friend class RoboSimian;
};

// -----------------------------------------------------------------------------
// Definition of the RoboSimian robot
// -----------------------------------------------------------------------------

class ContactManager;
class ContactMaterial;
class Driver;

class RoboSimian {
  public:
    RoboSimian(chrono::ChMaterialSurface::ContactMethod contact_method, bool has_sled = false, bool fixed = false);
    RoboSimian(chrono::ChSystem* system, bool has_sled = false, bool fixed = false);
    ~RoboSimian();

    chrono::ChSystem* GetSystem() { return m_system; }

    /// Set actuation type for wheel motors (default: SPEED)
    void SetMotorActuationMode(ActuationMode mode) { m_wheel_mode = mode; }

    /// Set collision flags for the various subsystems.
    /// By default, collision is enabled for the sled and wheels only.
    /// The 'flags' argument can be any of the CollisionFlag enums, or a combination thereof (using bit-wise operators).
    void SetCollide(int flags);

    /// Set coefficients of friction for sled-terrain and wheel-terrain contacts.
    /// Default values: 0.8.
    void SetFrictionCoefficients(float sled_friction, float wheel_friction);

    /// Attach a driver system.
    void SetDriver(std::shared_ptr<Driver> driver);

    /// Set visualization type for individual robot subsystems.
    void SetVisualizationTypeChassis(VisualizationType vis);
    void SetVisualizationTypeSled(VisualizationType vis);
    void SetVisualizationTypeLimbs(VisualizationType vis);
    void SetVisualizationTypeLimb(LimbID id, VisualizationType vis);
    void SetVisualizationTypeWheels(VisualizationType vis);

    /// Set output directory.
    void SetOutputDirectory(const std::string& outdir) { m_outdir = outdir; }

    /// Get a handle to the robot's chassis subsystem.
    std::shared_ptr<Chassis> GetChassis() const { return m_chassis; }

    /// Get location of the chassis body.
    const chrono::ChVector<>& GetChassisPos() const { return m_chassis->GetPos(); }

    /// Get orientation of the chassis body.
    const chrono::ChQuaternion<>& GetChassisRot() const { return m_chassis->GetRot(); }

    /// Get location of the wheel body for the specified limb.
    const chrono::ChVector<>& GetWheelPos(LimbID id) const { return m_limbs[id]->GetWheelPos(); }

    /// Get angular velocity of the wheel body for the specified limb (expressed in local coordinates).
    chrono::ChVector<> GetWheelAngVelocity(LimbID id) const { return m_limbs[id]->GetWheelAngVelocity(); }

    /// Get wheel angle for the specified limb.
    double GetWheelAngle(LimbID id) const { return m_limbs[id]->GetWheelAngle(); }

    /// Get wheel angular speed for the specified limb.
    double GetWheelOmega(LimbID id) const { return m_limbs[id]->GetWheelOmega(); }

    /// Get angles for all 8 limb motors.
    std::array<double, 8> GetMotorAngles(LimbID id) { return m_limbs[id]->GetMotorAngles(); }

    /// Get angular velocities for all 8 limb motors.
    std::array<double, 8> GetMotorOmegas(LimbID id) { return m_limbs[id]->GetMotorOmegas(); }

    /// Get actuator torques for all 8 limb motors.
    std::array<double, 8> GetMotorTorques(LimbID id) { return m_limbs[id]->GetMotorTorques(); }

    /// Initialize the robot at the specified chassis position and orientation.
    void Initialize(const chrono::ChCoordsys<>& pos);

    /// Directly activate the specified motor on the specified limb.
    void Activate(LimbID id, const std::string& motor_name, double time, double val);

    /// Advance dynamics of underlying system.
    /// If a driver system is specified, apply motor actuations at current time.
    void DoStepDynamics(double step);

    /// Translate all robot bodies by the specified value.
    void Translate(const chrono::ChVector<>& shift);

    /// Output current data.
    void Output();

    void ReportContacts();

    std::vector<std::shared_ptr<Limb>> GetLimbs() const { return m_limbs; }

  private:
    void Create(bool has_sled, bool fixed);

    chrono::ChSystem* m_system;  ///< pointer to the Chrono system
    bool m_owns_system;          ///< true if system created at construction

    std::shared_ptr<Chassis> m_chassis;          ///< robot chassis
    std::shared_ptr<Sled> m_sled;                ///< optional sled attached to chassis
    std::vector<std::shared_ptr<Limb>> m_limbs;  ///< robot limbs
    ////std::shared_ptr<WheelDD> m_wheel_left;       ///< left DD wheel
    ////std::shared_ptr<WheelDD> m_wheel_right;      ///< right DD wheel

    ActuationMode m_wheel_mode;  ///< type of actuation for wheel motor

    float m_wheel_friction;  ///< coefficient of friction wheel-terrain
    float m_sled_friction;   ///< coefficient of frictoin sled-terrain

    std::shared_ptr<Driver> m_driver;
    ContactManager* m_contacts;
    ContactMaterial* m_materials;

    std::string m_outdir;
    std::ofstream m_outf[4];

    friend class ContactMaterial;
};

// -----------------------------------------------------------------------------
// RoboSimian driver classes
// -----------------------------------------------------------------------------

typedef std::array<std::array<double, 8>, 4> Actuation;

class Driver {
  public:
    enum Phase { POSE, HOLD, START, CYCLE, STOP };

    Driver(const std::string& filename_start,
           const std::string& filename_cycle,
           const std::string& filename_stop,
           bool repeat = false);
    ~Driver();

    /// Specify time intervals to assume and then hold the initial pose.
    void SetTimeOffsets(double time_pose, double time_hold);

    /// Return the current limb motor actuations.
    Actuation GetActuation() { return m_actuations; }

    /// Return the current phase
    std::string GetCurrentPhase() const { return m_phase_names[m_phase]; }

    /// Class to be used as callback interface for user-defined actions at phase changes.
    class PhaseChangeCallback {
      public:
        virtual ~PhaseChangeCallback() {}
        virtual void OnPhaseChange(Driver::Phase old_phase, Driver::Phase new_phase) = 0;
    };

    /// Register a phase-change callback object.
    void RegisterPhaseChangeCallback(PhaseChangeCallback* callback) { m_callback = callback; }

  private:
    void Update(double time);
    void LoadDataLine(double& time, Actuation& activations);

    std::ifstream m_ifs_start;        ///< input file stream for start phase
    std::ifstream m_ifs_cycle;        ///< input file stream for cycle phase
    std::ifstream m_ifs_stop;         ///< input file stream for stop phase
    std::ifstream* m_ifs;             ///< active input file stream
    double m_time_pose;               ///< time interval to assume initial pose
    double m_time_hold;               ///< time interval to hold initial pose
    double m_offset;                  ///< current time offset in input files
    bool m_repeat;                    ///< repeat cycle
    Phase m_phase;                    ///< current phase
    double m_time_1;                  ///< time for cached actuations
    double m_time_2;                  ///< time for cached actuations
    Actuation m_actuations_1;         ///< cached actuations (before)
    Actuation m_actuations_2;         ///< cached actuations (after)
    Actuation m_actuations;           ///< current actuations
    PhaseChangeCallback* m_callback;  ///< user callback for phase change

    static const std::string m_phase_names[5];

    friend class RoboSimian;
};

}  // end namespace robosimian

#endif
