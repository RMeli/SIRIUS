// Copyright (c) 2013-2018 Anton Kozhevnikov, Thomas Schulthess
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted provided that
// the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
//    following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions
//    and the following disclaimer in the documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/** \file rotation.hpp
 *
 *  \brief Generate rotation matrices and related entities.
 */

#ifndef __ROTATION_HPP__
#define __ROTATION_HPP__

#include "memory.hpp"
#include "geometry3d.hpp"

using namespace geometry3d;
using namespace sddk;

namespace sirius {

/// Generate SU(2) rotation matrix from the axes and angle.
inline mdarray<double_complex, 2> rotation_matrix_su2(std::array<double, 3> u__, double theta__)
{
    mdarray<double_complex, 2> rotm(2, 2);

    auto cost = std::cos(theta__ / 2);
    auto sint = std::sin(theta__ / 2);

    rotm(0, 0) = double_complex(cost, -u__[2] * sint);
    rotm(1, 1) = double_complex(cost,  u__[2] * sint);
    rotm(0, 1) = double_complex(-u__[1] * sint, -u__[0] * sint);
    rotm(1, 0) = double_complex( u__[1] * sint, -u__[0] * sint);

    return rotm;
}

/// Generate SU2(2) rotation matrix from a 3x3 rotation matrix in Cartesian coordinates.
/** Create quaternion components from the 3x3 matrix. The components are just a w = Cos(\Omega/2)
 *  and {x,y,z} = unit rotation vector multiplied by Sin(\Omega/2)
 *
 *  See https://en.wikipedia.org/wiki/Quaternions_and_spatial_rotation
 *  and https://en.wikipedia.org/wiki/Rotation_group_SO(3)#Quaternions_of_unit_norm */
inline mdarray<double_complex, 2> rotation_matrix_su2(matrix3d<double> R__)
{
    double det = R__.det() > 0 ? 1.0 : -1.0;

    matrix3d<double> mat = R__ * det;

    mdarray<double_complex, 2> su2mat(2, 2);

    su2mat.zero();

    /* make quaternion components*/
    double w = sqrt(std::max(0.0, 1.0 + mat(0, 0) + mat(1, 1) + mat(2, 2))) / 2.0;
    double x = sqrt(std::max(0.0, 1.0 + mat(0, 0) - mat(1, 1) - mat(2, 2))) / 2.0;
    double y = sqrt(std::max(0.0, 1.0 - mat(0, 0) + mat(1, 1) - mat(2, 2))) / 2.0;
    double z = sqrt(std::max(0.0, 1.0 - mat(0, 0) - mat(1, 1) + mat(2, 2))) / 2.0;

    x = std::copysign(x, mat(2, 1) - mat(1, 2));
    y = std::copysign(y, mat(0, 2) - mat(2, 0));
    z = std::copysign(z, mat(1, 0) - mat(0, 1));

    su2mat(0, 0) = double_complex(w, -z);
    su2mat(1, 1) = double_complex(w, z);
    su2mat(0, 1) = double_complex(-y, -x);
    su2mat(1, 0) = double_complex(y, -x);

    return su2mat;
}

/// Get axis and angle from rotation matrix.
inline std::pair<vector3d<double>, double> axis_angle(matrix3d<double> R__)
{
    vector3d<double> u;
    /* make proper rotation */
    R__ = R__ * R__.det();
    u[0] = R__(2, 1) - R__(1, 2);
    u[1] = R__(0, 2) - R__(2, 0);
    u[2] = R__(1, 0) - R__(0, 1);

    double sint = u.length() / 2.0;
    double cost = (R__(0, 0) + R__(1, 1) + R__(2, 2) - 1) / 2.0;

    double theta = utils::phi_by_sin_cos(sint, cost);

    /* rotation angle is zero */
    if (std::abs(theta) < 1e-12) {
        u = {0, 0, 1};
    } else if (std::abs(theta - pi) < 1e-12) { /* rotation angle is Pi */
        /* rotation matrix for Pi angle has this form

        [-1+2ux^2 |  2 ux uy |  2 ux uz]
        [2 ux uy  | -1+2uy^2 |  2 uy uz]
        [2 ux uz  | 2 uy uz  | -1+2uz^2] */

        if (R__(0, 0) >= R__(1, 1) && R__(0, 0) >= R__(2, 2)) { /* x-component is largest */
            u[0] = std::sqrt(std::abs(R__(0, 0) + 1) / 2);
            u[1] = (R__(0, 1) + R__(1, 0)) / 4 / u[0];
            u[2] = (R__(0, 2) + R__(2, 0)) / 4 / u[0];
        } else if (R__(1, 1) >= R__(0, 0) && R__(1, 1) >= R__(2, 2)) { /* y-component is largest */
            u[1] = std::sqrt(std::abs(R__(1, 1) + 1) / 2);
            u[0] = (R__(1, 0) + R__(0, 1)) / 4 / u[1];
            u[2] = (R__(1, 2) + R__(2, 1)) / 4 / u[1];
        } else {
            u[2] = std::sqrt(std::abs(R__(2, 2) + 1) / 2);
            u[0] = (R__(2, 0) + R__(0, 2)) / 4 / u[2];
            u[1] = (R__(2, 1) + R__(1, 2)) / 4 / u[2];
        }
    } else {
        u = u * (1.0 / u.length());
    }

    return std::pair<vector3d<double>, double>(u, theta);
}

/// Generate rotation matrix from three Euler angles
/** Euler angles \f$ \alpha, \beta, \gamma \f$ define the general rotation as three consecutive rotations:
 *      - about \f$ \hat e_z \f$ through the angle \f$ \gamma \f$ (\f$ 0 \le \gamma < 2\pi \f$)
 *      - about \f$ \hat e_y \f$ through the angle \f$ \beta \f$ (\f$ 0 \le \beta \le \pi \f$)
 *      - about \f$ \hat e_z \f$ through the angle \f$ \alpha \f$ (\f$ 0 \le \gamma < 2\pi \f$)
 *
 *  The total rotation matrix is defined as a product of three rotation matrices:
 *  \f[
 *      R(\alpha, \beta, \gamma) =
 *          \left( \begin{array}{ccc} \cos(\alpha) & -\sin(\alpha) & 0 \\
 *                                    \sin(\alpha) & \cos(\alpha) & 0 \\
 *                                    0 & 0 & 1 \end{array} \right)
 *          \left( \begin{array}{ccc} \cos(\beta) & 0 & \sin(\beta) \\
 *                                    0 & 1 & 0 \\
 *                                    -\sin(\beta) & 0 & \cos(\beta) \end{array} \right)
 *          \left( \begin{array}{ccc} \cos(\gamma) & -\sin(\gamma) & 0 \\
 *                                    \sin(\gamma) & \cos(\gamma) & 0 \\
 *                                    0 & 0 & 1 \end{array} \right) =
 *      \left( \begin{array}{ccc} \cos(\alpha) \cos(\beta) \cos(\gamma) - \sin(\alpha) \sin(\gamma) &
 *                                -\sin(\alpha) \cos(\gamma) - \cos(\alpha) \cos(\beta) \sin(\gamma) &
 *                                \cos(\alpha) \sin(\beta) \\
 *                                \sin(\alpha) \cos(\beta) \cos(\gamma) + \cos(\alpha) \sin(\gamma) &
 *                                \cos(\alpha) \cos(\gamma) - \sin(\alpha) \cos(\beta) \sin(\gamma) &
 *                                \sin(\alpha) \sin(\beta) \\
 *                                -\sin(\beta) \cos(\gamma) &
 *                                \sin(\beta) \sin(\gamma) &
 *                                \cos(\beta) \end{array} \right)
 *  \f]
 */
inline matrix3d<double> rot_mtrx_cart(vector3d<double> euler_angles__)
{
    double alpha = euler_angles__[0];
    double beta = euler_angles__[1];
    double gamma = euler_angles__[2];

    matrix3d<double> rm;
    rm(0, 0) = std::cos(alpha) * std::cos(beta) * std::cos(gamma) - std::sin(alpha) * std::sin(gamma);
    rm(0, 1) = -std::cos(gamma) * std::sin(alpha) - std::cos(alpha) * std::cos(beta) * std::sin(gamma);
    rm(0, 2) = std::cos(alpha) * std::sin(beta);
    rm(1, 0) = std::cos(beta) * std::cos(gamma) * std::sin(alpha) + std::cos(alpha) * std::sin(gamma);
    rm(1, 1) = std::cos(alpha) * std::cos(gamma) - std::cos(beta) * std::sin(alpha) * std::sin(gamma);
    rm(1, 2) = std::sin(alpha) * std::sin(beta);
    rm(2, 0) = -std::cos(gamma) * std::sin(beta);
    rm(2, 1) = std::sin(beta) * std::sin(gamma);
    rm(2, 2) = std::cos(beta);

    return rm;
}

/// Compute Euler angles corresponding to the proper rotation martix.
inline vector3d<double> euler_angles(matrix3d<double> const& rot__)
{
    vector3d<double> angles(0, 0, 0);

    if (std::abs(rot__.det() - 1) > 1e-10) {
        std::stringstream s;
        s << "determinant of rotation matrix is " << rot__.det();
        TERMINATE(s);
    }

    if (std::abs(rot__(2, 2) - 1.0) < 1e-10) { // cos(beta) == 1, beta = 0
        angles[0] = utils::phi_by_sin_cos(rot__(1, 0), rot__(0, 0));
    } else if (std::abs(rot__(2, 2) + 1.0) < 1e-10) { // cos(beta) == -1, beta = Pi
        angles[0] = utils::phi_by_sin_cos(-rot__(0, 1), rot__(1, 1));
        angles[1] = pi;
    } else {
        double beta = std::acos(rot__(2, 2));
        angles[0] = utils::phi_by_sin_cos(rot__(1, 2) / std::sin(beta), rot__(0, 2) / std::sin(beta));
        angles[1] = beta;
        angles[2] = utils::phi_by_sin_cos(rot__(2, 1) / std::sin(beta), -rot__(2, 0) / std::sin(beta));
    }

    auto rm1 = rot_mtrx_cart(angles);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (std::abs(rot__(i, j) - rm1(i, j)) > 1e-8) {
                std::stringstream s;
                s << "matrices don't match" << std::endl
                  << "initial symmetry matrix: " << std::endl
                  << rot__(0, 0) << " " << rot__(0, 1) << " " << rot__(0, 2) << std::endl
                  << rot__(1, 0) << " " << rot__(1, 1) << " " << rot__(1, 2) << std::endl
                  << rot__(2, 0) << " " << rot__(2, 1) << " " << rot__(2, 2) << std::endl
                  << "euler angles : " << angles[0] / pi << " " << angles[1] / pi << " " << angles[2] / pi << std::endl
                  << "computed symmetry matrix : " << std::endl
                  << rm1(0, 0) << " " << rm1(0, 1) << " " << rm1(0, 2) << std::endl
                  << rm1(1, 0) << " " << rm1(1, 1) << " " << rm1(1, 2) << std::endl
                  << rm1(2, 0) << " " << rm1(2, 1) << " " << rm1(2, 2) << std::endl;
                TERMINATE(s);
            }
        }
    }

    return angles;
}

}

#endif // __ROTATION_HPP__