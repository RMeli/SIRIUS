// Copyright (c) 2013-2019 Anton Kozhevnikov, Thomas Schulthess
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

/** \file non_local_operator.cpp
 *
 *  \brief Contains implementation of sirius::Non_local_operator class.
 */

#include "non_local_operator.hpp"
#include "hubbard/hubbard_matrix.hpp"

namespace sirius {

template <typename T>
Non_local_operator<T>::Non_local_operator(Simulation_context const& ctx__)
    : ctx_(ctx__)
{
    PROFILE("sirius::Non_local_operator");

    pu_                 = this->ctx_.processing_unit();
    auto& uc            = this->ctx_.unit_cell();
    packed_mtrx_offset_ = sddk::mdarray<int, 1>(uc.num_atoms());
    packed_mtrx_size_   = 0;
    for (int ia = 0; ia < uc.num_atoms(); ia++) {
        int nbf                 = uc.atom(ia).mt_basis_size();
        packed_mtrx_offset_(ia) = packed_mtrx_size_;
        packed_mtrx_size_ += nbf * nbf;
    }

    switch (pu_) {
        case sddk::device_t::GPU: {
            packed_mtrx_offset_.allocate(sddk::memory_t::device).copy_to(sddk::memory_t::device);
            break;
        }
        case sddk::device_t::CPU: {
            break;
        }
    }
}

template <typename T>
D_operator<T>::D_operator(Simulation_context const& ctx_)
    : Non_local_operator<T>(ctx_)
{
    if (ctx_.gamma_point()) {
        this->op_ = sddk::mdarray<T, 3>(1, this->packed_mtrx_size_, ctx_.num_mag_dims() + 1);
    } else {
        this->op_ = sddk::mdarray<T, 3>(2, this->packed_mtrx_size_, ctx_.num_mag_dims() + 1);
    }
    this->op_.zero();
    initialize();
}

template <typename T>
void
D_operator<T>::initialize()
{
    PROFILE("sirius::D_operator::initialize");

    auto& uc = this->ctx_.unit_cell();

    const int s_idx[2][2] = {{0, 3}, {2, 1}};

    #pragma omp parallel for
    for (int ia = 0; ia < uc.num_atoms(); ia++) {
        auto& atom = uc.atom(ia);
        int nbf    = atom.mt_basis_size();
        auto& dion = atom.type().d_mtrx_ion();

        /* in case of spin orbit coupling */
        if (uc.atom(ia).type().spin_orbit_coupling()) {
            sddk::mdarray<std::complex<T>, 3> d_mtrx_so(nbf, nbf, 4);
            d_mtrx_so.zero();

            /* transform the d_mtrx */
            for (int xi2 = 0; xi2 < nbf; xi2++) {
                for (int xi1 = 0; xi1 < nbf; xi1++) {

                    /* first compute \f[A^_\alpha I^{I,\alpha}_{xi,xi}\f] cf Eq.19 in doi:10.1103/PhysRevB.71.115106  */

                    /* note that the `I` integrals are already calculated and stored in atom.d_mtrx */
                    for (int sigma = 0; sigma < 2; sigma++) {
                        for (int sigmap = 0; sigmap < 2; sigmap++) {
                            std::complex<T> result(0, 0);
                            for (auto xi2p = 0; xi2p < nbf; xi2p++) {
                                if (atom.type().compare_index_beta_functions(xi2, xi2p)) {
                                    /* just sum over m2, all other indices are the same */
                                    for (auto xi1p = 0; xi1p < nbf; xi1p++) {
                                        if (atom.type().compare_index_beta_functions(xi1, xi1p)) {
                                            /* just sum over m1, all other indices are the same */

                                            /* loop over the 0, z,x,y coordinates */
                                            for (int alpha = 0; alpha < 4; alpha++) {
                                                for (int sigma1 = 0; sigma1 < 2; sigma1++) {
                                                    for (int sigma2 = 0; sigma2 < 2; sigma2++) {
                                                        result += atom.d_mtrx(xi1p, xi2p, alpha) *
                                                                  pauli_matrix[alpha][sigma1][sigma2] *
                                                                  atom.type().f_coefficients(xi1, xi1p, sigma, sigma1) *
                                                                  atom.type().f_coefficients(xi2p, xi2, sigma2, sigmap);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            d_mtrx_so(xi1, xi2, s_idx[sigma][sigmap]) = result;
                        }
                    }
                }
            }

            /* add ionic contribution */

            /* spin orbit coupling mixes terms */

            /* keep the order of the indices because it is crucial here;
               permuting the indices makes things wrong */
            for (int xi2 = 0; xi2 < nbf; xi2++) {
                int l2     = atom.type().indexb(xi2).l;
                double j2  = atom.type().indexb(xi2).j;
                int idxrf2 = atom.type().indexb(xi2).idxrf;
                for (int xi1 = 0; xi1 < nbf; xi1++) {
                    int l1     = atom.type().indexb(xi1).l;
                    double j1  = atom.type().indexb(xi1).j;
                    int idxrf1 = atom.type().indexb(xi1).idxrf;
                    if ((l1 == l2) && (std::abs(j1 - j2) < 1e-8)) {
                        /* up-up down-down */
                        d_mtrx_so(xi1, xi2, 0) += dion(idxrf1, idxrf2) * atom.type().f_coefficients(xi1, xi2, 0, 0);
                        d_mtrx_so(xi1, xi2, 1) += dion(idxrf1, idxrf2) * atom.type().f_coefficients(xi1, xi2, 1, 1);

                        /* up-down down-up */
                        d_mtrx_so(xi1, xi2, 2) += dion(idxrf1, idxrf2) * atom.type().f_coefficients(xi1, xi2, 0, 1);
                        d_mtrx_so(xi1, xi2, 3) += dion(idxrf1, idxrf2) * atom.type().f_coefficients(xi1, xi2, 1, 0);
                    }
                }
            }

            /* the pseudo potential contains information about
               spin orbit coupling so we use a different formula
               Eq.19 doi:10.1103/PhysRevB.71.115106 for calculating the D matrix

               Note that the D matrices are stored and
               calculated in the up-down basis already not the (Veff,Bx,By,Bz) one */
            for (int xi2 = 0; xi2 < nbf; xi2++) {
                for (int xi1 = 0; xi1 < nbf; xi1++) {
                    int idx = xi2 * nbf + xi1;
                    for (int s = 0; s < 4; s++) {
                        this->op_(0, this->packed_mtrx_offset_(ia) + idx, s) = d_mtrx_so(xi1, xi2, s).real();
                        this->op_(1, this->packed_mtrx_offset_(ia) + idx, s) = d_mtrx_so(xi1, xi2, s).imag();
                    }
                }
            }
        } else {
            /* No spin orbit coupling for this atom \f[D = D(V_{eff})
               I + D(B_x) \sigma_x + D(B_y) sigma_y + D(B_z)
               sigma_z\f] since the D matrices are calculated that way */
            for (int xi2 = 0; xi2 < nbf; xi2++) {
                int lm2    = atom.type().indexb(xi2).lm;
                int idxrf2 = atom.type().indexb(xi2).idxrf;
                for (int xi1 = 0; xi1 < nbf; xi1++) {
                    int lm1    = atom.type().indexb(xi1).lm;
                    int idxrf1 = atom.type().indexb(xi1).idxrf;

                    int idx = xi2 * nbf + xi1;
                    switch (this->ctx_.num_mag_dims()) {
                        case 3: {
                            T bx = uc.atom(ia).d_mtrx(xi1, xi2, 2);
                            T by = uc.atom(ia).d_mtrx(xi1, xi2, 3);

                            this->op_(0, this->packed_mtrx_offset_(ia) + idx, 2) = bx;
                            this->op_(1, this->packed_mtrx_offset_(ia) + idx, 2) = -by;

                            this->op_(0, this->packed_mtrx_offset_(ia) + idx, 3) = bx;
                            this->op_(1, this->packed_mtrx_offset_(ia) + idx, 3) = by;
                        }
                        case 1: {
                            T v  = uc.atom(ia).d_mtrx(xi1, xi2, 0);
                            T bz = uc.atom(ia).d_mtrx(xi1, xi2, 1);

                            /* add ionic part */
                            if (lm1 == lm2) {
                                v += dion(idxrf1, idxrf2);
                            }

                            this->op_(0, this->packed_mtrx_offset_(ia) + idx, 0) = v + bz;
                            this->op_(0, this->packed_mtrx_offset_(ia) + idx, 1) = v - bz;
                            break;
                        }
                        case 0: {
                            this->op_(0, this->packed_mtrx_offset_(ia) + idx, 0) = uc.atom(ia).d_mtrx(xi1, xi2, 0);
                            /* add ionic part */
                            if (lm1 == lm2) {
                                this->op_(0, this->packed_mtrx_offset_(ia) + idx, 0) += dion(idxrf1, idxrf2);
                            }
                            break;
                        }
                        default: {
                            TERMINATE("wrong number of magnetic dimensions");
                        }
                    }
                }
            }
        }
    }

    if (this->ctx_.print_checksum()) {
        auto cs = this->op_.checksum();
        utils::print_checksum("D_operator", cs, this->ctx_.out());
    }

    if (this->pu_ == sddk::device_t::GPU && uc.mt_lo_basis_size() != 0) {
        this->op_.allocate(sddk::memory_t::device).copy_to(sddk::memory_t::device);
    }

    /* D-operator is not diagonal in spin in case of non-collinear magnetism
       (spin-orbit coupling falls into this case) */
    if (this->ctx_.num_mag_dims() == 3) {
        this->is_diag_ = false;
    }
}

template <typename T>
Q_operator<T>::Q_operator(Simulation_context const& ctx__)
    : Non_local_operator<T>(ctx__)
{
    /* Q-operator is independent of spin if there is no spin-orbit; however, it simplifies the apply()
     * method if the Q-operator has a spin index */
    if (this->ctx_.gamma_point()) {
        this->op_ = sddk::mdarray<T, 3>(1, this->packed_mtrx_size_, this->ctx_.num_mag_dims() + 1);
    } else {
        this->op_ = sddk::mdarray<T, 3>(2, this->packed_mtrx_size_, this->ctx_.num_mag_dims() + 1);
    }
    this->op_.zero();
    initialize();
}

template <typename T>
void
Q_operator<T>::initialize()
{
    PROFILE("sirius::Q_operator::initialize");

    auto& uc = this->ctx_.unit_cell();

    #pragma omp parallel for
    for (int ia = 0; ia < uc.num_atoms(); ia++) {
        int iat = uc.atom(ia).type().id();
        if (!uc.atom_type(iat).augment()) {
            continue;
        }
        int nbf = uc.atom(ia).mt_basis_size();
        for (int xi2 = 0; xi2 < nbf; xi2++) {
            for (int xi1 = 0; xi1 < nbf; xi1++) {
                /* The ultra soft pseudo potential has spin orbit coupling incorporated to it, so we
                   need to rotate the Q matrix */
                if (uc.atom_type(iat).spin_orbit_coupling()) {
                    /* this is nothing else than Eq.18 of doi:10.1103/PhysRevB.71.115106 */
                    for (auto si = 0; si < 2; si++) {
                        for (auto sj = 0; sj < 2; sj++) {

                            std::complex<T> result(0, 0);

                            for (int xi2p = 0; xi2p < nbf; xi2p++) {
                                if (uc.atom(ia).type().compare_index_beta_functions(xi2, xi2p)) {
                                    for (int xi1p = 0; xi1p < nbf; xi1p++) {
                                        /* The F coefficients are already "block diagonal" so we do a full
                                           summation. We actually rotate the q_matrices only */
                                        if (uc.atom(ia).type().compare_index_beta_functions(xi1, xi1p)) {
                                            result += this->ctx_.augmentation_op(iat).q_mtrx(xi1p, xi2p) *
                                                      (uc.atom(ia).type().f_coefficients(xi1, xi1p, sj, 0) *
                                                           uc.atom(ia).type().f_coefficients(xi2p, xi2, 0, si) +
                                                       uc.atom(ia).type().f_coefficients(xi1, xi1p, sj, 1) *
                                                           uc.atom(ia).type().f_coefficients(xi2p, xi2, 1, si));
                                        }
                                    }
                                }
                            }

                            /* the order of the index is important */
                            const int ind = (si == sj) ? si : sj + 2;
                            /* this gives
                               ind = 0 if si = up and sj = up
                               ind = 1 if si = sj = down
                               ind = 2 if si = down and sj = up
                               ind = 3 if si = up and sj = down */
                            this->op_(0, this->packed_mtrx_offset_(ia) + xi2 * nbf + xi1, ind) = result.real();
                            this->op_(1, this->packed_mtrx_offset_(ia) + xi2 * nbf + xi1, ind) = result.imag();
                        }
                    }
                } else {
                    for (int ispn = 0; ispn < this->ctx_.num_spins(); ispn++) {
                        this->op_(0, this->packed_mtrx_offset_(ia) + xi2 * nbf + xi1, ispn) =
                            this->ctx_.augmentation_op(iat).q_mtrx(xi1, xi2);
                    }
                }
            }
        }
    }
    if (this->ctx_.print_checksum()) {
        auto cs = this->op_.checksum();
        utils::print_checksum("Q_operator", cs, this->ctx_.out());
    }

    if (this->pu_ == sddk::device_t::GPU && uc.mt_lo_basis_size() != 0) {
        this->op_.allocate(sddk::memory_t::device).copy_to(sddk::memory_t::device);
    }

    this->is_null_ = true;
    for (int iat = 0; iat < uc.num_atom_types(); iat++) {
        if (uc.atom_type(iat).augment()) {
            this->is_null_ = false;
        }
        /* Q-operator is not diagonal in spin only in the case of spin-orbit coupling */
        if (uc.atom_type(iat).spin_orbit_coupling()) {
            this->is_diag_ = false;
        }
    }
}

template class Non_local_operator<double>;

template class D_operator<double>;

template class Q_operator<double>;

#if defined(USE_FP32)
template class Non_local_operator<float>;

template class D_operator<float>;

template class Q_operator<float>;
#endif
} // namespace sirius
