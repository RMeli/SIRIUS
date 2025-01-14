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

/** \file band.cpp
 *
 *  \brief Contains implementation of sirius::Band class.
 */

#include "band.hpp"
#include "context/simulation_context.hpp"
#include "k_point/k_point_set.hpp"
#include "utils/profiler.hpp"

namespace sirius {

/// Constructor
Band::Band(Simulation_context& ctx__)
    : ctx_(ctx__)
    , unit_cell_(ctx__.unit_cell())
    , blacs_grid_(ctx__.blacs_grid())
{
    if (!ctx_.initialized()) {
        RTE_THROW("Simulation_context is not initialized");
    }
}

template <typename T>
void
Band::initialize_subspace(K_point_set& kset__, Hamiltonian0<T>& H0__) const
{
    PROFILE("sirius::Band::initialize_subspace");

    int N{0};

    if (ctx_.cfg().iterative_solver().init_subspace() == "lcao") {
        /* get the total number of atomic-centered orbitals */
        N = unit_cell_.num_ps_atomic_wf().first;
    }

    for (int ikloc = 0; ikloc < kset__.spl_num_kpoints().local_size(); ikloc++) {
        int ik  = kset__.spl_num_kpoints(ikloc);
        auto kp = kset__.get<T>(ik);
        auto Hk = H0__(*kp);
        if (ctx_.gamma_point() && (ctx_.so_correction() == false)) {
            ::sirius::initialize_subspace<T, T>(Hk, N);
        } else {
            ::sirius::initialize_subspace<T, std::complex<T>>(Hk, N);
        }
    }

    /* reset the energies for the iterative solver to do at least two steps */
    for (int ik = 0; ik < kset__.num_kpoints(); ik++) {
        for (int ispn = 0; ispn < ctx_.num_spinors(); ispn++) {
            for (int i = 0; i < ctx_.num_bands(); i++) {
                kset__.get<T>(ik)->band_energy(i, ispn, 0);
                kset__.get<T>(ik)->band_occupancy(i, ispn, ctx_.max_occupancy());
            }
        }
    }
}


///// Check wave-functions for orthonormalization.
//template <typename T>
//void Band::check_wave_functions(Hamiltonian_k<real_type<T>>& Hk__) const
//{
//    auto& kp = Hk__.kp();
//    kp.message(1, __function_name__, "%s", "checking wave-functions\n");
//
//    if (!ctx_.full_potential()) {
//
//        sddk::dmatrix<T> ovlp(ctx_.num_bands(), ctx_.num_bands(), ctx_.blacs_grid(), ctx_.cyclic_block_size(), ctx_.cyclic_block_size());
//
//        const bool nc_mag = (ctx_.num_mag_dims() == 3);
//        const int num_sc = nc_mag ? 2 : 1;
//
//        auto& psi = kp.spinor_wave_functions();
//        sddk::Wave_functions<real_type<T>> spsi(kp.gkvec_partition(), ctx_.num_bands(), ctx_.preferred_memory_t(), num_sc);
//
//        if (is_device_memory(ctx_.preferred_memory_t())) {
//            auto& mpd = ctx_.mem_pool(sddk::memory_t::device);
//            for (int ispn = 0; ispn < ctx_.num_spins(); ispn++) {
//                psi.pw_coeffs(ispn).allocate(mpd);
//                psi.pw_coeffs(ispn).copy_to(sddk::memory_t::device, 0, ctx_.num_bands());
//            }
//            for (int i = 0; i < num_sc; i++) {
//                spsi.pw_coeffs(i).allocate(mpd);
//            }
//            ovlp.allocate(sddk::memory_t::device);
//        }
//
//        /* compute residuals */
//        for (int ispin_step = 0; ispin_step < ctx_.num_spinors(); ispin_step++) {
//            auto sr = sddk::spin_range(nc_mag ? 2 : ispin_step);
//            /* apply Hamiltonian and S operators to the wave-functions */
//            Hk__.template apply_h_s<T>(sr, 0, ctx_.num_bands(), psi, nullptr, &spsi);
//            inner(ctx_.spla_context(), sr, psi, 0, ctx_.num_bands(), spsi, 0, ctx_.num_bands(), ovlp, 0, 0);
//
//            double diff = check_identity(ovlp, ctx_.num_bands());
//
//            if (diff > 1e-12) {
//                kp.message(1, __function_name__, "overlap matrix is not identity, maximum error : %20.12f\n", diff);
//            } else {
//                kp.message(1, __function_name__, "%s", "OK! Wave functions are orthonormal.\n");
//            }
//        }
//        if (is_device_memory(ctx_.preferred_memory_t())) {
//            for (int ispn = 0; ispn < ctx_.num_spins(); ispn++) {
//                psi.pw_coeffs(ispn).deallocate(sddk::memory_t::device);
//            }
//        }
//    }
//}

template
void
Band::initialize_subspace<double>(K_point_set& kset__, Hamiltonian0<double>& H0__) const;
#if defined(USE_FP32)
template
void
Band::initialize_subspace<float>(K_point_set& kset__, Hamiltonian0<float>& H0__) const;
#endif

}
