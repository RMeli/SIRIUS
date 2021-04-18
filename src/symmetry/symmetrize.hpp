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

/** \file symmetrize.hpp
 *
 *  \brief Symmetrize scalar and vector functions.
 */

#ifndef __SYMMETRIZE_HPP__
#define __SYMMETRIZE_HPP__

#include "unit_cell/unit_cell_symmetry.hpp"
#include "SDDK/gvec.hpp"
#include "SDDK/omp.hpp"
#include "typedefs.hpp"
#include "sht/sht.hpp"
#include "utils/profiler.hpp"

namespace sirius {

inline void symmetrize(Unit_cell_symmetry const& sym__, Gvec_shells const& gvec_shells__,
                       sddk::mdarray<double_complex, 3> const& sym_phase_factors__, double_complex* f_pw__,
                       double_complex* x_pw__, double_complex* y_pw__, double_complex* z_pw__)
{
    PROFILE("sirius::symmetrize|fpw");

    auto f_pw = f_pw__ ? gvec_shells__.remap_forward(f_pw__) : std::vector<double_complex>();
    auto x_pw = x_pw__ ? gvec_shells__.remap_forward(x_pw__) : std::vector<double_complex>();
    auto y_pw = y_pw__ ? gvec_shells__.remap_forward(y_pw__) : std::vector<double_complex>();
    auto z_pw = z_pw__ ? gvec_shells__.remap_forward(z_pw__) : std::vector<double_complex>();

    /* local number of G-vectors in a distribution with complete G-vector shells */
    int ngv = gvec_shells__.gvec_count_remapped();

    auto sym_f_pw = f_pw__ ? std::vector<double_complex>(ngv, 0) : std::vector<double_complex>();
    auto sym_x_pw = x_pw__ ? std::vector<double_complex>(ngv, 0) : std::vector<double_complex>();
    auto sym_y_pw = y_pw__ ? std::vector<double_complex>(ngv, 0) : std::vector<double_complex>();
    auto sym_z_pw = z_pw__ ? std::vector<double_complex>(ngv, 0) : std::vector<double_complex>();

    bool is_non_collin = ((x_pw__ != nullptr) && (y_pw__ != nullptr) && (z_pw__ != nullptr));

    std::vector<bool> is_done(ngv, false);

    double norm = 1 / double(sym__.num_mag_sym());

    auto phase_factor = [&](int isym, vector3d<int> G)
    {
        return sym_phase_factors__(0, G[0], isym) *
               sym_phase_factors__(1, G[1], isym) *
               sym_phase_factors__(2, G[2], isym);
    };

    PROFILE_START("sirius::symmetrize|fpw|local");

    #pragma omp parallel
    {
        int nt = omp_get_max_threads();
        int tid = omp_get_thread_num();

        for (int igloc = 0; igloc < gvec_shells__.gvec_count_remapped(); igloc++) {
            auto G = gvec_shells__.gvec_remapped(igloc);

            int igsh = gvec_shells__.gvec_shell_remapped(igloc);

#if !defined(NDEBUG)
            if (igsh != gvec_shells__.gvec().shell(G)) {
                throw std::runtime_error("wrong index of G-shell");
            }
#endif
            /* each thread is working on full shell of G-vectors */
            if (igsh % nt == tid && !is_done[igloc]) {

                double_complex symf(0, 0);
                double_complex symx(0, 0);
                double_complex symy(0, 0);
                double_complex symz(0, 0);

                /* find the symmetrized PW coefficient */

                for (int i = 0; i < sym__.num_mag_sym(); i++) {
                    auto G1 = dot(G, sym__.magnetic_group_symmetry(i).spg_op.R);

                    auto S = sym__.magnetic_group_symmetry(i).spin_rotation;

                    auto phase = std::conj(phase_factor(i, G));

                    /* local index of a rotated G-vector */
                    int ig1 = gvec_shells__.index_by_gvec(G1);

                    if (ig1 == -1) {
                        G1 = G1 * (-1);
#if !defined(NDEBUG)
                        if (igsh != gvec_shells__.gvec().shell(G1)) {
                            throw std::runtime_error("wrong index of G-shell");
                        }
#endif
                        ig1 = gvec_shells__.index_by_gvec(G1);
                        assert(ig1 >= 0 && ig1 < ngv);
                        if (f_pw__) {
                            symf += std::conj(f_pw[ig1]) * phase;
                        }
                        if (!is_non_collin && z_pw__) {
                            symz += std::conj(z_pw[ig1]) * phase * S(2, 2);
                        }
                        if (is_non_collin) {
                            auto v = dot(S, vector3d<double_complex>({x_pw[ig1], y_pw[ig1], z_pw[ig1]}));
                            symx += std::conj(v[0]) * phase;
                            symy += std::conj(v[1]) * phase;
                            symz += std::conj(v[2]) * phase;
                        }
                    } else {
#if !defined(NDEBUG)
                        if (igsh != gvec_shells__.gvec().shell(G1)) {
                            throw std::runtime_error("wrong index of G-shell");
                        }
#endif
                        assert(ig1 >= 0 && ig1 < ngv);
                        if (f_pw__) {
                            symf += f_pw[ig1] * phase;
                        }
                        if (!is_non_collin && z_pw__) {
                            symz += z_pw[ig1] * phase * S(2, 2);
                        }
                        if (is_non_collin) {
                            auto v = dot(S, vector3d<double_complex>({x_pw[ig1], y_pw[ig1], z_pw[ig1]}));
                            symx += v[0] * phase;
                            symy += v[1] * phase;
                            symz += v[2] * phase;
                        }
                    }
                } /* loop over symmetries */

                symf *= norm;
                symx *= norm;
                symy *= norm;
                symz *= norm;

                //if (f_pw__) {
                //    sym_f_pw[igloc] = symf;
                //}
                //if (!is_non_collin && z_pw__) {
                //    sym_z_pw[igloc] = symz;
                //}
                //if (is_non_collin) {
                //    sym_x_pw[igloc] = symx;
                //    sym_y_pw[igloc] = symy;
                //    sym_z_pw[igloc] = symz;
                //}

                /* apply symmetry operation and get all other plane-wave coefficients */

                for (int isym = 0; isym < sym__.num_mag_sym(); isym++) {
                    auto S = sym__.magnetic_group_symmetry(isym).spin_rotation;

                    auto G1 = dot(sym__.magnetic_group_symmetry(isym).spg_op.invRT, G);
                    /* index of a rotated G-vector */
                    int ig1 = gvec_shells__.index_by_gvec(G1);

                    if (ig1 != -1) {
                        assert(ig1 >= 0 && ig1 < ngv);
                        auto phase = std::conj(phase_factor(isym, G1));
                        double_complex symf1, symx1, symy1, symz1;
                        if (f_pw__) {
                            symf1 = symf * phase;
                        }
                        if (!is_non_collin && z_pw__) {
                            symz1 = symz * phase * S(2, 2);
                        }
                        if (is_non_collin) {
                            auto v = dot(S, vector3d<double_complex>({symx, symy, symz}));
                            symx1 = v[0] * phase;
                            symy1 = v[1] * phase;
                            symz1 = v[2] * phase;
                        }

                        if (is_done[ig1]) {
                            if (f_pw__) {
                                /* check that another symmetry operation leads to the same coefficient */
                                if (std::abs(sym_f_pw[ig1] -  symf1) > 1e-12) {
                                    std::stringstream s;
                                    s << "inconsistent symmetry operation" << std::endl
                                      << "  existing value : " << sym_f_pw[ig1] << std::endl
                                      << "  computed value : " << symf1 << std::endl
                                      << "  difference: " << std::abs(sym_f_pw[ig1] - symf1) << std::endl;
                                    throw std::runtime_error(s.str());
                                }
                            }
                            if (!is_non_collin && z_pw__) {
                                if (std::abs(sym_z_pw[ig1] -  symz1) > 1e-12) {
                                    std::stringstream s;
                                    s << "inconsistent symmetry operation" << std::endl
                                      << "  existing value : " << sym_z_pw[ig1] << std::endl
                                      << "  computed value : " << symz1 << std::endl
                                      << "  difference: " << std::abs(sym_z_pw[ig1] - symz1) << std::endl;
                                    throw std::runtime_error(s.str());
                                }
                            }
                            if (is_non_collin) {
                                if (std::abs(sym_x_pw[ig1] - symx1) > 12e-12 ||
                                    std::abs(sym_y_pw[ig1] - symy1) > 12e-12 ||
                                    std::abs(sym_z_pw[ig1] - symz1) > 12e-12) {

                                    throw std::runtime_error("inconsistent symmetry operation");
                                }
                            }
                        } else {
                            if (f_pw__) {
                                sym_f_pw[ig1] = symf1;
                            }
                            if (!is_non_collin && z_pw__) {
                                sym_z_pw[ig1] = symz1;
                            }
                            if (is_non_collin) {
                                sym_x_pw[ig1] = symx1;
                                sym_y_pw[ig1] = symy1;
                                sym_z_pw[ig1] = symz1;

                            }
                            is_done[ig1] = true;
                        }
                    }
                } /* loop over symmetries */
            }
        } /* loop over igloc */
    }
    PROFILE_STOP("sirius::symmetrize|fpw|local");

#if !defined(NDEBUG)
    double diff{0};
    for (int igloc = 0; igloc < gvec_shells__.gvec_count_remapped(); igloc++) {
        auto G = gvec_shells__.gvec_remapped(igloc);
        for (int isym = 0; isym < sym__.num_mag_sym(); isym++) {
            auto S = sym__.magnetic_group_symmetry(isym).spin_rotation;
            auto gv_rot = dot(sym__.magnetic_group_symmetry(isym).spg_op.invRT, G);
            /* index of a rotated G-vector */
            int ig_rot = gvec_shells__.index_by_gvec(gv_rot);
            double_complex phase = std::conj(phase_factor(isym, gv_rot));

            if (f_pw__ && ig_rot != -1) {
                diff += std::abs(sym_f_pw[ig_rot] - sym_f_pw[igloc] * phase);
            }
            if (!is_non_collin && z_pw__ && ig_rot != -1) {
                diff += std::abs(sym_z_pw[ig_rot] - sym_z_pw[igloc] * phase * S(2, 2));
            }
        }
    }
    if (diff > 1e-12) {
        throw std::runtime_error("check of symmetrized PW coefficients failed");
    }
#endif

    if (f_pw__) {
        gvec_shells__.remap_backward(sym_f_pw, f_pw__);
    }
    if (x_pw__) {
        gvec_shells__.remap_backward(sym_x_pw, x_pw__);
    }
    if (y_pw__) {
        gvec_shells__.remap_backward(sym_y_pw, y_pw__);
    }
    if (z_pw__) {
        gvec_shells__.remap_backward(sym_z_pw, z_pw__);
    }
}


/// Symmetrize scalar function.
/** The following operation is performed:
    \f[
      f_{\mathrm{sym}}({\bf x}) = \frac{1}{N_{\mathrm{sym}}} \sum_{{\bf \hat P}} f({\bf \hat P x})
    \f]
    For the function expanded in plane-waves we have:
    \f[
      f_{\mathrm{sym}}({\bf x}) = \frac{1}{N_{\mathrm{sym}}} \sum_{{\bf \hat P}} \sum_{\bf G}
      e^{i{\bf G \hat P x}} \hat f({\bf G})
                 = \frac{1}{N_{\mathrm{sym}}} \sum_{{\bf \hat P}} \sum_{\bf G} e^{i{\bf G (Rx +
                     t)}} \hat f({\bf G})
                 = \frac{1}{N_{\mathrm{sym}}} \sum_{{\bf \hat P}} \sum_{\bf G} e^{i{\bf G t}}
                 e^{i{\bf R^T G x}} \hat f({\bf G})
    \f]
    Substitute \f$\bf \tilde G = \bf R^T \bf G\f$
    \f[
      f_{\mathrm{sym}}({\bf x}) =
      \frac{1}{N_{\mathrm{sym}}} \sum_{{\bf \tilde G}} e^{i {\bf \tilde G} }\sum_{{\bf \hat P}}
      e^{i {\bf R}^{-T} {\bf t}} \hat f ({\bf R}^{-T} {\bf \tilde G}) \,,
    \f]
    to find the Fourier coefficients \f$ \hat f_{\mathrm{sym}} \f$ of \f$f_{\mathrm{sym}}\f$ in
    terms of \f$ \hat f \f$:
    \f[
      \hat f_{\mathrm{sym}}({\bf G}) = \frac{1}{N_{\mathrm{sym}}} \sum_{{\bf \hat P}} e^{i {\bf R}^{-T} {\bf t}}
        \hat f ({\bf R}^{-T} {\bf G})\,.
    \f]
    Once \f$\hat f_{\mathrm{sym}} \f$ has been calculated by the above formula for a single \f$\bf G \f$, its values at
    points \f${\bf R}^{-T} {\bf G}\f$, \f$\forall \, {\bf R}\f$ are given by the update formula
    \f[
      \hat f_{\mathrm{sym}} ({\bf R}^{-T} {\bf G}) = e^{-i {\bf R}^{-T} {\bf G} {\bf t}}
      \hat{f}_{\mathrm{sym}} ({\bf G})\,,
    \f]

    which follows by using that \f$f_{\mathrm{sym}}({\bf \hat P} {\bf x}) = f_{\mathrm{sym}}({\bf x})\f$:
    \f{eqnarray*}{
      f_{\mathrm{sym}}(\hat{P}{\bf x}) &=& \sum_G e^{i G ({\bf R}{\bf x} + {\bf t} )} \hat{f}_{\mathrm{sym}}({\bf G}) \ \
                                       &=& \sum_G e^{i {\bf G} {\bf x}} e^{i {\bf R}^{-T} {\bf G} {\bf t}}
                                       \hat{f}_{\mathrm{sym}}({\bf R}^{-T} {\bf G}) \,.
    \f}
 */
//== inline void symmetrize_function(Unit_cell_symmetry const& sym__, Gvec_shells const& gvec_shells__,
//==                                 mdarray<double_complex, 3> const& sym_phase_factors__, double_complex* f_pw__)
//== {
//==     PROFILE("sirius::symmetrize_function|fpw");
//== 
//==     auto v = gvec_shells__.remap_forward(f_pw__);
//== 
//==     auto phase_factor = [&](int isym, vector3d<int> G)
//==     {
//==         return sym_phase_factors__(0, G[0], isym) *
//==                sym_phase_factors__(1, G[1], isym) *
//==                sym_phase_factors__(2, G[2], isym);
//==     };
//== 
//==     std::vector<double_complex> sym_f_pw(v.size(), 0);
//==     std::vector<bool> is_done(v.size(), false);
//== 
//==     double norm = 1 / double(sym__.num_mag_sym());
//== 
//==     PROFILE_START("sirius::symmetrize_function|fpw|local");
//== 
//==     #pragma omp parallel
//==     {
//==         int nt = omp_get_max_threads();
//==         int tid = omp_get_thread_num();
//== 
//==         for (int igloc = 0; igloc < gvec_shells__.gvec_count_remapped(); igloc++) {
//==             auto G = gvec_shells__.gvec_remapped(igloc);
//== 
//==             int igsh = gvec_shells__.gvec_shell_remapped(igloc);
//== 
//== #if !defined(NDEBUG)
//==             if (igsh != gvec_shells__.gvec().shell(G)) {
//==                 throw std::runtime_error("wrong index of G-shell");
//==             }
//== #endif
//==             /* each thread is working on full shell of G-vectors */
//==             if (igsh % nt == tid) { //&& !is_done[igloc]) {
//==                 double_complex zsym(0, 0);
//== 
//==                 /* find the symmetrized PW coefficient */
//== 
//==                 for (int i = 0; i < sym__.num_mag_sym(); i++) {
//==                     auto gvi = dot(G, sym__.magnetic_group_symmetry(i).spg_op.R);
//== 
//==                     double_complex phase = std::conj(phase_factor(i, G));
//== 
//==                     /* local index of a rotated G-vector */
//==                     int igi = gvec_shells__.index_by_gvec(gvi);
//== 
//==                     if (igi == -1) {
//==                         gvi = gvi * (-1);
//== #if !defined(NDEBUG)
//==                         if (igsh != gvec_shells__.gvec().shell(gvi)) {
//==                             throw std::runtime_error("wrong index of G-shell");
//==                         }
//== #endif
//==                         igi = gvec_shells__.index_by_gvec(gvi);
//==                         assert(igi >= 0 && igi < (int)v.size());
//==                         zsym += std::conj(v[igi]) * phase;
//==                     } else {
//== #if !defined(NDEBUG)
//==                         if (igsh != gvec_shells__.gvec().shell(gvi)) {
//==                             throw std::runtime_error("wrong index of G-shell");
//==                         }
//== #endif
//==                         assert(igi >= 0 && igi < (int)v.size());
//==                         zsym += v[igi] * phase;
//==                     }
//==                 } /* loop over symmetries */
//== 
//==                 zsym *= norm;
//== 
//==                 sym_f_pw[igloc] = zsym;
//== 
//==                 ///* apply symmetry operation and get all other plane-wave coefficients */
//== 
//==                 //for (int isym = 0; isym < sym__.num_mag_sym(); isym++) {
//==                 //    auto gv_rot = dot(G, sym__.magnetic_group_symmetry(isym).spg_op.R);
//==                 //    /* index of a rotated G-vector */
//==                 //    int ig_rot = gvec_shells__.index_by_gvec(gv_rot);
//==                 //    double_complex phase = std::conj(phase_factor(isym, gv_rot));
//== 
//==                 //    if (ig_rot == -1) {
//==                 //        /* skip */
//==                 //    } else {
//==                 //        if (is_done[ig_rot]) {
//==                 //            /* check that another symmetry operation leads to the same coefficient */
//==                 //            if (std::abs(sym_f_pw[ig_rot] -  zsym * phase) > 1e-12) {
//==                 //                std::stringstream s;
//==                 //                s << "inconsistent symmetry operation" << std::endl
//==                 //                  << "  existing value : " << sym_f_pw[ig_rot] << std::endl
//==                 //                  << "  computed value : " <<  zsym * phase << std::endl
//==                 //                  << "  difference: " << std::abs(sym_f_pw[ig_rot] -  zsym * phase) << std::endl;
//==                 //                throw std::runtime_error(s.str());
//==                 //            }
//==                 //        } else {
//==                 //            assert(ig_rot >= 0 && ig_rot < int(v.size()));
//==                 //            sym_f_pw[ig_rot] = zsym * phase;
//==                 //            is_done[ig_rot] = true;
//==                 //        }
//==                 //    }
//==                 //} /* loop over symmetries */
//==             }
//==         } /* loop over igloc */
//==     }
//== 
//==     PROFILE_STOP("sirius::symmetrize_function|fpw|local");
//== 
//==     gvec_shells__.remap_backward(sym_f_pw, f_pw__);
//== }
//== 
//== inline void symmetrize_vector_function(Unit_cell_symmetry const& sym__, Gvec_shells const& gvec_shells__,
//==                                        mdarray<double_complex, 3> const& sym_phase_factors__, double_complex* fz_pw__)
//== {
//==     PROFILE("sirius::symmetrize_vector_function|vzpw");
//== 
//==     auto phase_factor = [&](int isym, const vector3d<int>& G) {
//==         return sym_phase_factors__(0, G[0], isym) *
//==                sym_phase_factors__(1, G[1], isym) *
//==                sym_phase_factors__(2, G[2], isym);
//==     };
//== 
//==     auto v = gvec_shells__.remap_forward(fz_pw__);
//== 
//==     std::vector<double_complex> sym_f_pw(v.size(), 0);
//==     std::vector<bool> is_done(v.size(), false);
//==     double norm = 1 / double(sym__.num_mag_sym());
//== 
//==     #pragma omp parallel
//==     {
//==         int nt = omp_get_max_threads();
//==         int tid = omp_get_thread_num();
//== 
//==         for (int igloc = 0; igloc < gvec_shells__.gvec_count_remapped(); igloc++) {
//==             auto G = gvec_shells__.gvec_remapped(igloc);
//== 
//==             int igsh = gvec_shells__.gvec_shell_remapped(igloc);
//== 
//==             //assert(igsh == gvec_shells__.gvec().shell(G));
//== 
//==             if (igsh % nt == tid) { // && !is_done[igloc]) {
//==                 double_complex zsym(0, 0);
//== 
//==                 for (int i = 0; i < sym__.num_mag_sym(); i++) {
//==                     auto gvi = dot(G, sym__.magnetic_group_symmetry(i).spg_op.R);
//== 
//==                     auto& S = sym__.magnetic_group_symmetry(i).spin_rotation;
//== 
//==                     double_complex phase = std::conj(phase_factor(i, G));
//== 
//==                     /* local index of a rotated G-vector */
//==                     int igi = gvec_shells__.index_by_gvec(gvi);
//== 
//==                     /* use property of plane-wave coefficients of the real function: f(-G) = f^{*}(G) */
//==                     if (igi == -1) {
//==                         gvi = gvi * (-1);
//==                         igi = gvec_shells__.index_by_gvec(gvi);
//==                         //assert(igsh == gvec_shells__.gvec().shell(gvi));
//==                         assert(igi >= 0 && igi < (int)v.size());
//== 
//==                         //zsym += std::conj(v[igi]) * phase * S(2, 2);
//==                         sym_f_pw[igloc] += std::conj(v[igi]) * phase * S(2, 2) * norm;
//==                     } else {
//==                         assert(igi  >= 0 && igi < (int)v.size());
//==                         //assert(igsh == gvec_shells__.gvec().shell(gvi));
//==                         sym_f_pw[igloc] += v[igi] * phase * S(2, 2) * norm;
//== 
//==                         //zsym += v[igi] * phase * S(2, 2);
//==                     }
//==                 } /* loop over symmetries */
//== 
//==                 //zsym *= norm;
//== 
//==                 //sym_f_pw[igloc] = zsym;
//== 
//==                 //for (int i = 0; i < sym__.num_mag_sym(); i++) {
//==                 //    auto gv_rot = transpose(sym__.magnetic_group_symmetry(i).spg_op.R) * G;
//==                 //    /* index of a rotated G-vector */
//==                 //    int ig_rot = gvec_shells__.index_by_gvec(gv_rot);
//==                 //    double_complex phase = std::conj(phase_factor(i, gv_rot));
//== 
//==                 //    auto& S = sym__.magnetic_group_symmetry(i).spin_rotation;
//== 
//==                 //    if (ig_rot == -1) {
//==                 //        /* skip */
//==                 //    } else {
//==                 //        if (is_done[ig_rot]) {
//==                 //            /* check that another symmetry operation leads to the same coefficient */
//==                 //            if (std::abs(sym_f_pw[ig_rot] - zsym * phase * S(2, 2)) > 1e-12) {
//==                 //                std::cout << "sym_f_pw[ig_rot] = " << sym_f_pw[ig_rot]
//==                 //                    << " zsym * phase * S = " << zsym * phase << std::endl
//==                 //                    << "isym : " << i << std::endl;
//==                 //                throw std::runtime_error("inconsistent symmetry operation");
//==                 //            }
//==                 //        } else {
//==                 //            assert(ig_rot >= 0 && ig_rot < int(v.size()));
//==                 //            sym_f_pw[ig_rot] = zsym * phase * S(2, 2);
//==                 //            is_done[ig_rot] = true;
//==                 //        }
//==                 //    }
//==                 //} /* loop over symmetries */
//==             }
//==         }
//==     }
//== 
//==     gvec_shells__.remap_backward(sym_f_pw, fz_pw__);
//== }
//== 
//== /// Symmetrize vector valued function.
//== /** The following operations are performed.
//==  *
//==  *   Fourier coefficient of symmetrized function:
//==  *   \f[
//==  *     \hat f_{\mathrm{sym}}({\bf G}) = \frac{1}{N_{\mathrm{sym}}} \sum_{{\bf \hat P}} e^{i {\bf R}^{-T} {\bf t}} {\bf S} \hat f ({\bf R}^{-T} {\bf G})\,.
//==  *   \f]
//==  *
//==  *   Update formula when \f$\hat f({\bf G})\f$ is known:
//==  *   \f[
//==  *     \hat f_{\mathrm{sym}} ({\bf R}^{-T} {\bf G}) = e^{-i {\bf R}^{-T} {\bf G} {\bf t}}
//==  *     {\bf S}^{-1} \hat{f}_{\mathrm{sym}} ({\bf G})\,,
//==  *   \f]
//==  *
//==  *   The derivation works similarly to the one for symmetrize_function().
//==  */
//== inline void symmetrize_vector_function(Unit_cell_symmetry const& sym__, Gvec_shells const& gvec_shells__,
//==                                        mdarray<double_complex, 3> const& sym_phase_factors__,
//==                                        double_complex* fx_pw__, double_complex* fy_pw__, double_complex* fz_pw__)
//== {
//==     PROFILE("sirius::symmetrize_vector_function|vpw");
//== 
//==     auto vx = gvec_shells__.remap_forward(fx_pw__);
//==     auto vy = gvec_shells__.remap_forward(fy_pw__);
//==     auto vz = gvec_shells__.remap_forward(fz_pw__);
//== 
//==     std::vector<double_complex> sym_fx_pw(vx.size(), 0);
//==     std::vector<double_complex> sym_fy_pw(vx.size(), 0);
//==     std::vector<double_complex> sym_fz_pw(vx.size(), 0);
//==     std::vector<bool> is_done(vx.size(), false);
//== 
//==     double norm = 1 / double(sym__.num_mag_sym());
//== 
//==     auto phase_factor = [&](int isym, const vector3d<int>& G)
//==     {
//==         return sym_phase_factors__(0, G[0], isym) *
//==                sym_phase_factors__(0, G[1], isym) *
//==                sym_phase_factors__(0, G[2], isym);
//==     };
//== 
//==     auto vrot = [&](vector3d<double_complex> const& v, matrix3d<double> const& S) -> vector3d<double_complex>
//==     {
//==         return dot(S, v);
//==     };
//== 
//==     #pragma omp parallel
//==     {
//==         int nt = omp_get_max_threads();
//==         int tid = omp_get_thread_num();
//== 
//==         for (int igloc = 0; igloc < gvec_shells__.gvec_count_remapped(); igloc++) {
//==             auto G = gvec_shells__.gvec_remapped(igloc);
//== 
//==             int igsh = gvec_shells__.gvec_shell_remapped(igloc);
//== 
//==             if (igsh % nt == tid && !is_done[igloc]) {
//==                 double_complex xsym(0, 0);
//==                 double_complex ysym(0, 0);
//==                 double_complex zsym(0, 0);
//== 
//==                 for (int i = 0; i < sym__.num_mag_sym(); i++) {
//==                     /* full space-group symmetry operation is {R|t} */
//==                     auto& invRT = sym__.magnetic_group_symmetry(i).spg_op.invRT;
//==                     auto& S = sym__.magnetic_group_symmetry(i).spin_rotation;
//==                     double_complex phase = phase_factor(i, G);
//==                     auto gv_rot = dot(invRT, G);
//==                     /* index of a rotated G-vector */
//==                     int ig_rot = gvec_shells__.index_by_gvec(gv_rot);
//== 
//==                     if (ig_rot == -1) {
//==                         gv_rot = gv_rot * (-1);
//==                         ig_rot = gvec_shells__.index_by_gvec(gv_rot);
//==                         auto v_rot = vrot({vx[ig_rot], vy[ig_rot], vz[ig_rot]}, S);
//==                         assert(ig_rot >=0 && ig_rot < (int)vx.size());
//==                         xsym += std::conj(v_rot[0]) * phase;
//==                         ysym += std::conj(v_rot[1]) * phase;
//==                         zsym += std::conj(v_rot[2]) * phase;
//==                     } else {
//==                         assert(ig_rot >=0 && ig_rot < (int)vx.size());
//==                         auto v_rot = vrot({vx[ig_rot], vy[ig_rot], vz[ig_rot]}, S);
//==                         xsym += v_rot[0] * phase;
//==                         ysym += v_rot[1] * phase;
//==                         zsym += v_rot[2] * phase;
//==                     }
//==                 } /* loop over symmetries */
//== 
//==                 xsym *= norm;
//==                 ysym *= norm;
//==                 zsym *= norm;
//== 
//==                 for (int i = 0; i < sym__.num_mag_sym(); i++) {
//==                     const auto& invRT = sym__.magnetic_group_symmetry(i).spg_op.invRT;
//==                     const auto& invS = sym__.magnetic_group_symmetry(i).spin_rotation_inv;
//==                     auto gv_rot = dot(invRT, G);
//==                     /* index of a rotated G-vector */
//==                     int ig_rot = gvec_shells__.index_by_gvec(gv_rot);
//==                     auto v_rot = vrot({xsym, ysym, zsym}, invS);
//==                     double_complex phase = std::conj(phase_factor(i, gv_rot));
//== 
//==                     if (ig_rot == -1) {
//==                         /* skip */
//==                     } else {
//==                         assert(ig_rot >= 0 && ig_rot < int(vz.size()));
//==                         sym_fx_pw[ig_rot] = v_rot[0] * phase;
//==                         sym_fy_pw[ig_rot] = v_rot[1] * phase;
//==                         sym_fz_pw[ig_rot] = v_rot[2] * phase;
//==                         is_done[ig_rot] = true;
//==                     }
//==                 } /* loop over symmetries */
//==             }
//==         }
//==     }
//== 
//==     gvec_shells__.remap_backward(sym_fx_pw, fx_pw__);
//==     gvec_shells__.remap_backward(sym_fy_pw, fy_pw__);
//==     gvec_shells__.remap_backward(sym_fz_pw, fz_pw__);
//== }

inline void symmetrize_function(Unit_cell_symmetry const& sym__, Communicator const& comm__, mdarray<double, 3>& frlm__)
{
    PROFILE("sirius::symmetrize_function|flm");

    int lmmax = (int)frlm__.size(0);
    int nrmax = (int)frlm__.size(1);
    if (sym__.num_atoms() != (int)frlm__.size(2)) {
        TERMINATE("wrong number of atoms");
    }

    splindex<splindex_t::block> spl_atoms(sym__.num_atoms(), comm__.size(), comm__.rank());

    int lmax = utils::lmax(lmmax);

    mdarray<double, 2> rotm(lmmax, lmmax);

    mdarray<double, 3> fsym(lmmax, nrmax, spl_atoms.local_size());
    fsym.zero();

    double alpha = 1.0 / double(sym__.num_mag_sym());

    for (int i = 0; i < sym__.num_mag_sym(); i++) {
        /* full space-group symmetry operation is {R|t} */
        int pr = sym__.magnetic_group_symmetry(i).spg_op.proper;
        auto eang = sym__.magnetic_group_symmetry(i).spg_op.euler_angles;
        int isym = sym__.magnetic_group_symmetry(i).isym;
        SHT::rotation_matrix(lmax, eang, pr, rotm);

        for (int ia = 0; ia < sym__.num_atoms(); ia++) {
            int ja = sym__.sym_table(ia, isym);
            auto location = spl_atoms.location(ja);
            if (location.rank == comm__.rank()) {
                linalg(linalg_t::blas).gemm('N', 'N', lmmax, nrmax, lmmax, &alpha, rotm.at(memory_t::host), rotm.ld(),
                                            frlm__.at(memory_t::host, 0, 0, ia), frlm__.ld(), &linalg_const<double>::one(),
                                            fsym.at(memory_t::host, 0, 0, location.local_index), fsym.ld());
            }
        }
    }
    double* sbuf = spl_atoms.local_size() ? fsym.at(memory_t::host) : nullptr;
    comm__.allgather(sbuf, frlm__.at(memory_t::host), lmmax * nrmax * spl_atoms.local_size(),
            lmmax * nrmax * spl_atoms.global_offset());

}

inline void symmetrize_vector_function(Unit_cell_symmetry const& sym__, Communicator const& comm__,
                                       mdarray<double, 3>& vz_rlm__)
{
    PROFILE("sirius::symmetrize_function|vzlm");

    int lmmax = (int)vz_rlm__.size(0);
    int nrmax = (int)vz_rlm__.size(1);

    splindex<splindex_t::block> spl_atoms(sym__.num_atoms(), comm__.size(), comm__.rank());

    if (sym__.num_atoms() != (int)vz_rlm__.size(2)) {
        TERMINATE("wrong number of atoms");
    }

    int lmax = utils::lmax(lmmax);

    mdarray<double, 2> rotm(lmmax, lmmax);

    mdarray<double, 3> fsym(lmmax, nrmax, spl_atoms.local_size());
    fsym.zero();

    double alpha = 1.0 / double(sym__.num_mag_sym());

    for (int i = 0; i < sym__.num_mag_sym(); i++) {
        /* full space-group symmetry operation is {R|t} */
        int pr = sym__.magnetic_group_symmetry(i).spg_op.proper;
        auto eang = sym__.magnetic_group_symmetry(i).spg_op.euler_angles;
        int isym = sym__.magnetic_group_symmetry(i).isym;
        auto S = sym__.magnetic_group_symmetry(i).spin_rotation;
        SHT::rotation_matrix(lmax, eang, pr, rotm);

        for (int ia = 0; ia < sym__.num_atoms(); ia++) {
            int ja = sym__.sym_table(ia, isym);
            auto location = spl_atoms.location(ja);
            if (location.rank == comm__.rank()) {
                double a = alpha * S(2, 2);
                linalg(linalg_t::blas).gemm('N', 'N', lmmax, nrmax, lmmax, &a, rotm.at(memory_t::host),
                rotm.ld(), vz_rlm__.at(memory_t::host, 0, 0, ia), vz_rlm__.ld(), &linalg_const<double>::one(),
                fsym.at(memory_t::host, 0, 0, location.local_index), fsym.ld());
            }
        }
    }

    double* sbuf = spl_atoms.local_size() ? fsym.at(memory_t::host) : nullptr;
    comm__.allgather(sbuf, vz_rlm__.at(memory_t::host), lmmax * nrmax * spl_atoms.local_size(),
            lmmax * nrmax * spl_atoms.global_offset());
}

inline void symmetrize_vector_function(Unit_cell_symmetry const& sym__, Communicator const& comm__,
                                       mdarray<double, 3>& vx_rlm__, mdarray<double, 3>& vy_rlm__,
                                       mdarray<double, 3>& vz_rlm__)
{
    PROFILE("sirius::symmetrize_function|vlm");

    int lmmax = (int)vx_rlm__.size(0);
    int nrmax = (int)vx_rlm__.size(1);

    splindex<splindex_t::block> spl_atoms(sym__.num_atoms(), comm__.size(), comm__.rank());

    int lmax = utils::lmax(lmmax);

    mdarray<double, 2> rotm(lmmax, lmmax);

    mdarray<double, 4> v_sym(lmmax, nrmax, spl_atoms.local_size(), 3);
    v_sym.zero();

    mdarray<double, 3> vtmp(lmmax, nrmax, 3);

    double alpha = 1.0 / double(sym__.num_mag_sym());

    std::vector<mdarray<double, 3>*> vrlm({&vx_rlm__, &vy_rlm__, &vz_rlm__});

    for (int i = 0; i < sym__.num_mag_sym(); i++) {
        /* full space-group symmetry operation is {R|t} */
        int pr = sym__.magnetic_group_symmetry(i).spg_op.proper;
        auto eang = sym__.magnetic_group_symmetry(i).spg_op.euler_angles;
        int isym = sym__.magnetic_group_symmetry(i).isym;
        auto S = sym__.magnetic_group_symmetry(i).spin_rotation;
        SHT::rotation_matrix(lmax, eang, pr, rotm);

        for (int ia = 0; ia < sym__.num_atoms(); ia++) {
            int ja = sym__.sym_table(ia, isym);
            auto location = spl_atoms.location(ja);
            if (location.rank == comm__.rank()) {
                for (int k: {0, 1, 2}) {
                    linalg(linalg_t::blas).gemm('N', 'N', lmmax, nrmax, lmmax, &alpha, rotm.at(memory_t::host), rotm.ld(),
                                                vrlm[k]->at(memory_t::host, 0, 0, ia), vrlm[k]->ld(),
                                                &linalg_const<double>::zero(), vtmp.at(memory_t::host, 0, 0, k), vtmp.ld());
                }
                #pragma omp parallel
                for (int k: {0, 1, 2}) {
                    for (int j: {0, 1, 2}) {
                        #pragma omp for
                        for (int ir = 0; ir < nrmax; ir++) {
                            for (int lm = 0; lm < lmmax; lm++) {
                                v_sym(lm, ir, location.local_index, k) += S(k, j) * vtmp(lm, ir, j);
                            }
                        }
                    }
                }
            }
        }
    }

    for (int k: {0, 1, 2}) {
        double* sbuf = spl_atoms.local_size() ? v_sym.at(memory_t::host, 0, 0, 0, k) : nullptr;
        comm__.allgather(sbuf, vrlm[k]->at(memory_t::host), lmmax * nrmax * spl_atoms.local_size(),
                lmmax * nrmax * spl_atoms.global_offset());
    }
}

/// Apply a given rotation in angular momentum subspace l and spin space s =
/// 1/2. note that we only store three components uu, dd, ud

/* we compute \f[ Oc = (R_l\cross R_s) . O . (R_l\cross R_s)^\dagger \f] */

//inline void apply_symmetry(const mdarray<double_complex, 3> &dm_,
//                           const mdarray<double, 2> &rot,
//                           const mdarray<double_complex, 2> &spin_rot_su2,
//                           const int num_mag_dims_,
//                           const int l,
//                           mdarray<double_complex, 3> &res_)
//{
//    res_.zero();
//    for (int lm1 = 0; lm1 <= 2 * l + 1; lm1++) {
//        for (int lm2 = 0; lm2 <= 2 * l + 1; lm2++) {
//            res_.zero();
//            double_complex dm_rot_spatial[3];
//
//            for (int j = 0; j < num_mag_dims_; j++) {
//                // this is a matrix-matrix multiplication P A P ^-1
//                dm_rot_spatial[j] = 0.0;
//                for (int lm3 = 0; lm3 <= 2 * l + 1; lm3++) {
//                    for (int lm4 = 0; lm4 <= 2 * l + 1; lm4++) {
//                        dm_rot_spatial[j] += dm_(lm3, lm4, j) * rot(l * l + lm1, l * l + lm3) * rot(l * l + lm2, l * l + lm4);
//                    }
//                }
//            }
//            if (num_mag_dims_ != 3)
//                for (int j = 0; j < num_mag_dims_; j++) {
//                    res_(lm1, lm2, j) += dm_rot_spatial[j];
//                }
//            else {
//                // full non colinear magnetism
//                double_complex spin_dm[2][2] = {
//                    {dm_rot_spatial[0], dm_rot_spatial[2]},
//                    {std::conj(dm_rot_spatial[2]), dm_rot_spatial[1]}};
//
//                /* spin blocks of density matrix are: uu, dd, ud
//                   the mapping from linear index (0, 1, 2) of density matrix components is:
//                   for the first spin index: k & 1, i.e. (0, 1, 2) -> (0, 1, 0)
//                   for the second spin index: min(k, 1), i.e. (0, 1, 2) -> (0, 1, 1)
//                */
//                for (int k = 0; k < num_mag_dims_; k++) {
//                    for (int is = 0; is < 2; is++) {
//                        for (int js = 0; js < 2; js++) {
//                            res_(lm1, lm2, k) += spin_rot_su2(k & 1, is) * spin_dm[is][js] * std::conj(spin_rot_su2(std::min(k, 1), js));
//                        }
//                    }
//                }
//            }
//        }
//    }
//}


/// Symmetrize density or occupancy matrix according to a given list of basis functions.
/** Density matrix arises in LAPW or PW methods. In PW it is computed in the basis of beta-projectors. Occupancy
 *  matrix is computed for the Hubbard-U correction. In both cases the matrix has the same structure and is
 *  symmetrized in the same way The symmetrization does depend explicitly on the beta or wfc. The last
 *  parameter is on when the atom has spin-orbit coupling and hubbard correction in
 *  that case, we must skip half of the indices because of the averaging of the
 *  radial integrals over the total angular momentum
 */
inline void symmetrize(const mdarray<double_complex, 4> &ns_,
                       const basis_functions_index &indexb,
                       const int ia,
                       const int ja,
                       const int ndm,
                       const mdarray<double, 2> &rotm,
                       const mdarray<double_complex, 2> &spin_rot_su2,
                       mdarray<double_complex, 4> &dm_,
                       const bool hubbard_)
{
    for (int xi1 = 0; xi1 < indexb.size(); xi1++) {
        int l1  = indexb[xi1].l;
        int lm1 = indexb[xi1].lm;
        int o1  = indexb[xi1].order;

        if ((hubbard_)&&(xi1 >= (2 * l1 + 1))) {
            break;
        }

        for (int xi2 = 0; xi2 < indexb.size(); xi2++) {
            int l2  = indexb[xi2].l;
            int lm2 = indexb[xi2].lm;
            int o2  = indexb[xi2].order;
            std::array<double_complex, 3> dm_rot_spatial = {0, 0, 0};

            //} the hubbard treatment when spin orbit coupling is present is
            // foundamentally wrong since we consider the full hubbard
            // correction with a averaged wave function (meaning we neglect the
            // L.S correction within hubbard). A better option (although still
            // wrong from physics pov) would be to consider a multi orbital case.

            if ((hubbard_)&&(xi2 >= (2 * l2 + 1))) {
                break;
            }

            //      if (l1 == l2) {
            // the rotation matrix of the angular momentum is block
            // diagonal and does not couple different l.
            for (int j = 0; j < ndm; j++) {
                for (int m3 = -l1; m3 <= l1; m3++) {
                    int lm3 = utils::lm(l1, m3);
                    int xi3 = indexb.index_by_lm_order(lm3, o1);
                    for (int m4 = -l2; m4 <= l2; m4++) {
                        int lm4 = utils::lm(l2, m4);
                        int xi4 = indexb.index_by_lm_order(lm4, o2);
                        dm_rot_spatial[j] += ns_(xi3, xi4, j, ia) *
                            rotm(lm1, lm3) * rotm(lm2, lm4);
                    }
                }
            }

            /* magnetic symmetrization */
            if (ndm == 1) {
                dm_(xi1, xi2, 0, ja) += dm_rot_spatial[0];
            } else {
                double_complex spin_dm[2][2] = {
                    {dm_rot_spatial[0], dm_rot_spatial[2]},
                    {std::conj(dm_rot_spatial[2]), dm_rot_spatial[1]}};

                /* spin blocks of density matrix are: uu, dd, ud
                   the mapping from linear index (0, 1, 2) of density matrix components is:
                   for the first spin index: k & 1, i.e. (0, 1, 2) -> (0, 1, 0)
                   for the second spin index: min(k, 1), i.e. (0, 1, 2) -> (0, 1, 1)
                */
                for (int k = 0; k < ndm; k++) {
                    for (int is = 0; is < 2; is++) {
                        for (int js = 0; js < 2; js++) {
                            dm_(xi1, xi2, k, ja) += spin_rot_su2(k & 1, is) * spin_dm[is][js] * std::conj(spin_rot_su2(std::min(k, 1), js));
                        }
                    }
                }
            }
        }
    }
}

inline void
symmetrize(sddk::mdarray<double_complex, 4> const& ns__, sirius::experimental::basis_functions_index const& indexb__,
           int const ia__, int const ja__, int const ndm__, sddk::mdarray<double, 2> const& rotm__,
           sddk::mdarray<double_complex, 2> const& spin_rot_su2__, sddk::mdarray<double_complex, 4>& dm__,
           bool const hubbard__) // TODO: revisit the implementation, also thnik about off-site occupation matrix 
{
    for (int xi1 = 0; xi1 < static_cast<int>(indexb__.size()); xi1++) {
        int l1  = indexb__.l(xi1);
        int lm1 = indexb__.lm(xi1);
        int o1  = indexb__.order(xi1);

        if (hubbard__ && (xi1 >= (2 * l1 + 1))) {
            break;
        }

        for (int xi2 = 0; xi2 < static_cast<int>(indexb__.size()); xi2++) {
            int l2  = indexb__.l(xi2);
            int lm2 = indexb__.lm(xi2);
            int o2  = indexb__.order(xi2);
            std::array<double_complex, 3> dm_rot_spatial = {0, 0, 0};

            //} the hubbard treatment when spin orbit coupling is present is
            // foundamentally wrong since we consider the full hubbard
            // correction with a averaged wave function (meaning we neglect the
            // L.S correction within hubbard). A better option (although still
            // wrong from physics pov) would be to consider a multi orbital case.

            if (hubbard__ && (xi2 >= (2 * l2 + 1))) {
                break;
            }

            //      if (l1 == l2) {
            // the rotation matrix of the angular momentum is block
            // diagonal and does not couple different l.
            for (int j = 0; j < ndm__; j++) {
                for (int m3 = -l1; m3 <= l1; m3++) {
                    int lm3 = utils::lm(l1, m3);
                    int xi3 = indexb__.index_by_lm_order(lm3, o1);
                    for (int m4 = -l2; m4 <= l2; m4++) {
                        int lm4 = utils::lm(l2, m4);
                        int xi4 = indexb__.index_by_lm_order(lm4, o2);
                        dm_rot_spatial[j] += ns__(xi3, xi4, j, ia__) *
                            rotm__(lm1, lm3) * rotm__(lm2, lm4);
                    }
                }
            }

            /* magnetic symmetrization */
            if (ndm__ == 1) {
                dm__(xi1, xi2, 0, ja__) += dm_rot_spatial[0];
            } else {
                double_complex spin_dm[2][2] = {
                    {dm_rot_spatial[0], dm_rot_spatial[2]},
                    {std::conj(dm_rot_spatial[2]), dm_rot_spatial[1]}};

                /* spin blocks of density matrix are: uu, dd, ud
                   the mapping from linear index (0, 1, 2) of density matrix components is:
                   for the first spin index: k & 1, i.e. (0, 1, 2) -> (0, 1, 0)
                   for the second spin index: min(k, 1), i.e. (0, 1, 2) -> (0, 1, 1)
                */
                for (int k = 0; k < ndm__; k++) {
                    for (int is = 0; is < 2; is++) {
                        for (int js = 0; js < 2; js++) {
                            dm__(xi1, xi2, k, ja__) += spin_rot_su2__(k & 1, is) * spin_dm[is][js] *
                                std::conj(spin_rot_su2__(std::min(k, 1), js));
                        }
                    }
                }
            }
        }
    }
}
} // namespace

#endif // __SYMMETRIZE_HPP__
