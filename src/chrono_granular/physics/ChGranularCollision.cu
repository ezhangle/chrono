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
// Contains some collision helper functions for chrono_granular, lifted from ChNarrowphaseR
//
// =============================================================================
// Authors: Dan Negrut, Conlain Kelly
// =============================================================================
#include "chrono_granular/physics/ChGranularCollision.cuh"
// Fictitious radius of curvature for collision with a corner or an edge.
__constant__ float edge_radius = 0.1;

__device__ float3 Cross(float3 v1, float3 v2) {
    float3 dest;
    dest.x = v1.y * v2.z - v1.z * v2.y;
    dest.y = v1.z * v2.x - v1.x * v2.z;
    dest.z = v1.x * v2.y - v1.y * v2.x;
    return dest;
}

__device__ float Dot(float3 v1, float3 v2) {
    return (v1.x * v2.x + v1.y * v2.y + v1.z * v2.z);
}

// Get vector 2-norm
__device__ float Length(float3 v) {
    return (sqrt(Dot(v, v)));
}

// Multiply a * v
__device__ float3 operator*(const float a, const float3 v) {
    return make_float3(a * v.x, a * v.y, a * v.z);
}

// Multiply a * v
__device__ float3 operator*(const float3 v, const float a) {
    return make_float3(a * v.x, a * v.y, a * v.z);
}

// Divide v / a
__device__ float3 operator/(const float3 v, const float a) {
    return make_float3(v.x / a, v.y / a, v.z / a);
}

// v1 - v2
__device__ float3 operator-(const float3 v1, const float3 v2) {
    return make_float3(v1.x - v2.x, v1.y - v2.y, v1.z - v2.z);
}

// subtract a from each element
__device__ float3 operator-(const float3 v1, const float a) {
    return make_float3(v1.x - a, v1.y - a, v1.z - a);
}

// v1 + v2
__device__ float3 operator+(const float3 v1, const float3 v2) {
    return make_float3(v1.x + v2.x, v1.y + v2.y, v1.z + v2.z);
}

/// This utility function returns the normal to the triangular face defined by
/// the vertices A, B, and C. The face is assumed to be non-degenerate.
/// Note that order of vertices is important!
__device__ float3 face_normal(const float3& A, const float3& B, const float3& C) {
    float3 v1 = B - A;
    float3 v2 = C - A;
    float3 n = Cross(v1, v2);
    float len = Length(n);

    return n / len;
}

/// This utility function takes the location 'P' and snaps it to the closest
/// point on the triangular face with given vertices (A, B, and C). The result
/// is returned in 'res'. Both 'P' and 'res' are assumed to be specified in
/// the same frame as the face vertices. This function returns 'true' if the
/// result is on an edge of this face and 'false' if the result is inside the
/// triangle.
/// Code from Ericson, "float-time collision detection", 2005, pp. 141
__device__ bool snap_to_face(const float3& A, const float3& B, const float3& C, const float3& P, float3& res) {
    float3 AB = B - A;
    float3 AC = C - A;

    // Check if P in vertex region outside A
    float3 AP = P - A;
    float d1 = Dot(AB, AP);
    float d2 = Dot(AC, AP);
    if (d1 <= 0 && d2 <= 0) {
        res = A;  // barycentric coordinates (1,0,0)
        return true;
    }

    // Check if P in vertex region outside B
    float3 BP = P - B;
    float d3 = Dot(AB, BP);
    float d4 = Dot(AC, BP);
    if (d3 >= 0 && d4 <= d3) {
        res = B;  // barycentric coordinates (0,1,0)
        return true;
    }

    // Check if P in edge region of AB
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        // Return projection of P onto AB
        float v = d1 / (d1 - d3);
        res = A + v * AB;  // barycentric coordinates (1-v,v,0)
        return true;
    }

    // Check if P in vertex region outside C
    float3 CP = P - C;
    float d5 = Dot(AB, CP);
    float d6 = Dot(AC, CP);
    if (d6 >= 0 && d5 <= d6) {
        res = C;  // barycentric coordinates (0,0,1)
        return true;
    }

    // Check if P in edge region of AC
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        // Return projection of P onto AC
        float w = d2 / (d2 - d6);
        res = A + w * AC;  // barycentric coordinates (1-w,0,w)
        return true;
    }

    // Check if P in edge region of BC
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        // Return projection of P onto BC
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        res = B + w * (C - B);  // barycentric coordinates (0,1-w,w)
        return true;
    }

    // P inside face region. Return projection of P onto face
    // barycentric coordinates (u,v,w)
    float denom = 1 / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    res = A + v * AB + w * AC;  // = u*A + v*B + w*C  where  (u = 1 - v - w)
    return false;
}

// =============================================================================
//              FACE - SPHERE
// Face-sphere narrow phase collision detection.
// In: triangular face defined by points A1, B1, C1
//     sphere sphere centered at pos2 and with radius2
__device__ bool face_sphere(const float3& A1,
                            const float3& B1,
                            const float3& C1,
                            const float3& pos2,
                            const float& radius2,
                            const float& separation,
                            float3& norm,
                            float& depth,
                            float3& pt1,
                            float3& pt2,
                            float& eff_radius) {
    float radius2_s = radius2 + separation;

    // Calculate face normal.
    float3 nrm1 = face_normal(A1, B1, C1);

    // Calculate signed height of sphere center above face plane. If the
    // height is larger than the sphere radius plus the separation value
    // or if the sphere center is below the plane, there is no contact.
    float h = Dot(pos2 - A1, nrm1);

    if (h >= radius2_s || h <= 0)
        return false;

    // Find the closest point on the face to the sphere center and determine
    // whether or not this location is inside the face or on an edge.
    float3 faceLoc;

    if (snap_to_face(A1, B1, C1, pos2, faceLoc)) {
        // Closest face feature is an edge. If the distance between the sphere
        // center and the closest point is more than the radius plus the
        // separation value, then there is no contact. Also, ignore contact if
        // the sphere center (almost) coincides with the closest point, in
        // which case we couldn't decide on the proper contact direction.
        float3 delta = pos2 - faceLoc;
        float dist2 = Dot(delta, delta);

        if (dist2 >= radius2_s * radius2_s || dist2 <= 1e-12f)
            return false;

        float dist = sqrt(dist2);
        norm = delta / dist;
        depth = dist - radius2;
        eff_radius = radius2 * edge_radius / (radius2 + edge_radius);
    } else {
        // Closest point on face is inside the face.
        norm = nrm1;
        depth = h - radius2;
        eff_radius = radius2;
    }

    pt1 = faceLoc;
    pt2 = pos2 - norm * radius2;

    return true;
}