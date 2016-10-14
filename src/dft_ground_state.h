// Copyright (c) 2013-2014 Anton Kozhevnikov, Thomas Schulthess
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

/** \file dft_ground_state.h
 *
 *  \brief Contains definition and partial implementation of sirius::DFT_ground_state class.
 */

#ifndef __DFT_GROUND_STATE_H__
#define __DFT_GROUND_STATE_H__

#include "potential.h"
#include "density.h"
#include "k_set.h"
#include "force.h"
#include "json.hpp"
#include "Geometry/Forces_PS.h"

using json = nlohmann::json;

namespace sirius
{

class DFT_ground_state
{
    private:

        Simulation_context& ctx_;

        Unit_cell& unit_cell_;

        Potential& potential_;

        Density& density_;

        K_set& kset_;

        Band band_;

        std::unique_ptr<Forces_PS> forces_;

        int use_symmetry_;

        double ewald_energy_;

        double ewald_energy();

    public:

        DFT_ground_state(Simulation_context& ctx__,
                         Potential& potential__,
                         Density& density__,
                         K_set& kset__,
                         int use_symmetry__)
            : ctx_(ctx__),
              unit_cell_(ctx__.unit_cell()),
              potential_(potential__),
              density_(density__),
              kset_(kset__),
              band_(ctx_),
              use_symmetry_(use_symmetry__)
        {
            if (!ctx_.full_potential()) ewald_energy_ = ewald_energy();

            forces_ = std::unique_ptr<Forces_PS>(new Forces_PS(ctx_, density_, potential_));
        }

        void move_atoms(int istep);

        mdarray<double, 2> forces();

        int find(double potential_tol, double energy_tol, int num_dft_iter);

        void relax_atom_positions();

        void print_info();
        
        void initialize_subspace();

        /// Return nucleus energy in the electrostatic field.
        /** Compute energy of nucleus in the electrostatic potential generated by the total (electrons + nuclei) 
         *  charge density. Diverging self-interaction term z*z/|r=0| is excluded. */
        double energy_enuc() const
        {
            double enuc{0};
            if (ctx_.full_potential()) {
                for (int ialoc = 0; ialoc < unit_cell_.spl_num_atoms().local_size(); ialoc++) {
                    int ia = unit_cell_.spl_num_atoms(ialoc);
                    int zn = unit_cell_.atom(ia).zn();
                    enuc -= 0.5 * zn * potential_.vh_el(ia) * y00;
                }
                ctx_.comm().allreduce(&enuc, 1);
            }
            return enuc;
        }
        
        /// Return eigen-value sum of core states.
        double core_eval_sum() const
        {
            double sum{0};
            for (int ic = 0; ic < unit_cell_.num_atom_symmetry_classes(); ic++) {
                sum += unit_cell_.atom_symmetry_class(ic).core_eval_sum() * 
                       unit_cell_.atom_symmetry_class(ic).num_atoms();
            }
            return sum;
        }

        double energy_vha()
        {
            double eh = potential_.energy_vha();

//          if(ctx_.esm_type() == paw_pseudopotential)
//          {
//              eh += potential_->PAW_hartree_total_energy();
//          }

            return eh;
        }
        
        double energy_vxc()
        {
            return density_.rho()->inner(potential_.xc_potential());
        }
        
        double energy_exc()
        {
            double exc = density_.rho()->inner(potential_.xc_energy_density());
            if (!ctx_.full_potential()) {
                exc += density_.rho_pseudo_core()->inner(potential_.xc_energy_density());
            }
//            if(ctx_.esm_type() == paw_pseudopotential)
//            {
//              exc += potential_->PAW_xc_total_energy();
//            }
            return exc;
        }

        double energy_bxc()
        {
            double ebxc{0};
            for (int j = 0; j < ctx_.num_mag_dims(); j++) {
                ebxc += density_.magnetization(j)->inner(potential_.effective_magnetic_field(j));
            }
            return ebxc;
        }

        double energy_veff()
        {
            return energy_vha() + energy_vxc();
        }

        /// Full eigen-value sum (core + valence)
        double eval_sum()
        {
            return (core_eval_sum() + kset_.valence_eval_sum());
        }
        
        /// Kinetic energy
        /** more doc here
        */
        double energy_kin()
        {
            return (eval_sum() - energy_veff() - energy_bxc());
        }

        double energy_ewald() const
        {
            return ewald_energy_;
        }

        /// Total energy of the electronic subsystem.
        /** From the definition of the density functional we have:
         *  
         *  \f[
         *      E[\rho] = T[\rho] + E^{H}[\rho] + E^{XC}[\rho] + E^{ext}[\rho]
         *  \f]
         *  where \f$ T[\rho] \f$ is the kinetic energy, \f$ E^{H}[\rho] \f$ - electrostatic energy of
         *  electron-electron density interaction, \f$ E^{XC}[\rho] \f$ - exchange-correlation energy
         *  and \f$ E^{ext}[\rho] \f$ - energy in the external field of nuclei.
         *  
         *  Electrostatic and external field energies are grouped in the following way:
         *  \f[
         *      \frac{1}{2} \int \int \frac{\rho({\bf r})\rho({\bf r'}) d{\bf r} d{\bf r'}}{|{\bf r} - {\bf r'}|} + 
         *          \int \rho({\bf r}) V^{nuc}({\bf r}) d{\bf r} = \frac{1}{2} \int V^{H}({\bf r})\rho({\bf r})d{\bf r} + 
         *          \frac{1}{2} \int \rho({\bf r}) V^{nuc}({\bf r}) d{\bf r}
         *  \f]
         *  Here \f$ V^{H}({\bf r}) \f$ is the total (electron + nuclei) electrostatic potential returned by the 
         *  poisson solver. Next we transform the remaining term:
         *  \f[
         *      \frac{1}{2} \int \rho({\bf r}) V^{nuc}({\bf r}) d{\bf r} = 
         *      \frac{1}{2} \int \int \frac{\rho({\bf r})\rho^{nuc}({\bf r'}) d{\bf r} d{\bf r'}}{|{\bf r} - {\bf r'}|} = 
         *      \frac{1}{2} \int V^{H,el}({\bf r}) \rho^{nuc}({\bf r}) d{\bf r}
         *  \f]
         */
        double total_energy()
        {
            double tot_en{0};

            switch (ctx_.esm_type())
            {
                case electronic_structure_method_t::full_potential_lapwlo:
                case electronic_structure_method_t::full_potential_pwlo:
                {
                    tot_en = (energy_kin() + energy_exc() + 0.5 * energy_vha() + energy_enuc());
                }
                break;

                case electronic_structure_method_t::ultrasoft_pseudopotential:
                case electronic_structure_method_t::norm_conserving_pseudopotential:
                {
                    tot_en = kset_.valence_eval_sum() - energy_veff() + 0.5 * energy_vha() +
                             energy_exc() + ewald_energy_;
                }
                break;

                case electronic_structure_method_t::paw_pseudopotential:
                {
                    tot_en = (kset_.valence_eval_sum() - energy_veff() - potential_.PAW_one_elec_energy())    // Ekin
                             + 0.5 * energy_vha() + energy_exc() + potential_.PAW_total_energy() +            // Epot
                             ewald_energy_ ;                                                                  // Ewald
                }
                break;

                default:
                {
                    STOP();
                }
            }

            return tot_en;
        }

        void generate_effective_potential()
        {
            switch(ctx_.esm_type())
            {
                case electronic_structure_method_t::full_potential_lapwlo:
                case electronic_structure_method_t::full_potential_pwlo:
                {
                    potential_.generate_effective_potential(density_.rho(), density_.magnetization());
                    break;
                }

                //TODO case paw_pseudopotential think about
                case electronic_structure_method_t::ultrasoft_pseudopotential:
                case electronic_structure_method_t::norm_conserving_pseudopotential:
                {
                    potential_.generate_effective_potential(density_.rho(), density_.rho_pseudo_core(), density_.magnetization());
                    break;
                };

                case electronic_structure_method_t::paw_pseudopotential:
                {
                    potential_.generate_effective_potential(density_.rho(), density_.rho_pseudo_core(), density_.magnetization());
                    potential_.generate_PAW_effective_potential(density_);
                    break;
                }

            }
        }

        void symmetrize(Periodic_function<double>* f__,
                        Periodic_function<double>* gz__,
                        Periodic_function<double>* gx__,
                        Periodic_function<double>* gy__)
        {
            PROFILE();

            auto& comm = ctx_.comm();

            /* symmetrize PW components */
            unit_cell_.symmetry().symmetrize_function(&f__->f_pw(0), ctx_.gvec(), comm);
            switch (ctx_.num_mag_dims()) {
                case 1: {
                    unit_cell_.symmetry().symmetrize_vector(&gz__->f_pw(0), ctx_.gvec(), comm);
                    break;
                }
                case 3: {
                    unit_cell_.symmetry().symmetrize_vector(&gx__->f_pw(0), &gy__->f_pw(0), &gz__->f_pw(0),
                                                            ctx_.gvec(), comm);
                    break;
                }
            }

            if (ctx_.full_potential()) {
                /* symmetrize MT components */
                unit_cell_.symmetry().symmetrize_function(f__->f_mt(), comm);
                switch (ctx_.num_mag_dims()) {
                    case 1: {
                        unit_cell_.symmetry().symmetrize_vector(gz__->f_mt(), comm);
                        break;
                    }
                    case 3: {
                        unit_cell_.symmetry().symmetrize_vector(gx__->f_mt(), gy__->f_mt(), gz__->f_mt(), comm);
                        break;
                    }
                }
            }

            if (ctx_.full_potential()) {
                ctx_.fft().prepare(ctx_.gvec().partition());
                f__->fft_transform(1);
                switch (ctx_.num_mag_dims()) {
                    case 3: {
                        gx__->fft_transform(1);
                        gy__->fft_transform(1);
                    }
                    case 1: {
                        gz__->fft_transform(1);
                    }
                    case 0: {
                        f__->fft_transform(1);
                    }
                }
                ctx_.fft().dismiss();
            }

            #ifdef __PRINT_OBJECT_HASH
            DUMP("hash(rhomt): %16llX", density_->rho()->f_mt().hash());
            DUMP("hash(rhoit): %16llX", density_->rho()->f_it().hash());
            DUMP("hash(rhopw): %16llX", density_->rho()->f_pw().hash());
            #endif
        }

        Band const& band() const
        {
            return band_;
        }

        json serialize()
        {
            json dict;

            dict["mpi_grid"] = ctx_.mpi_grid_dims();

            std::vector<int> fftgrid(3);
            for (int i = 0; i < 3; i++) {
                fftgrid[i] = ctx_.fft().grid().size(i);
            }
            dict["fft_grid"] = fftgrid;
            if (!ctx_.full_potential()) {
                for (int i = 0; i < 3; i++) {
                    fftgrid[i] = ctx_.fft_coarse().grid().size(i);
                }
                dict["fft_coarse_grid"] = fftgrid;
            }
            dict["num_fv_states"] = ctx_.num_fv_states();
            dict["num_bands"] = ctx_.num_bands();
            dict["aw_cutoff"] = ctx_.aw_cutoff();
            dict["pw_cutoff"] = ctx_.pw_cutoff();
            dict["omega"] = ctx_.unit_cell().omega();
            dict["chemical_formula"] = ctx_.unit_cell().chemical_formula();
            dict["num_atoms"] = ctx_.unit_cell().num_atoms();
            dict["energy"] = json::object();
            dict["energy"]["total"] = total_energy();
            dict["energy"]["enuc"] = energy_enuc();
            dict["energy"]["core_eval_sum"] = core_eval_sum();
            dict["energy"]["vha"] = energy_vha();
            dict["energy"]["vxc"] = energy_vxc();
            dict["energy"]["exc"] = energy_exc();
            dict["energy"]["bxc"] = energy_bxc();
            dict["energy"]["veff"] = energy_veff();
            dict["energy"]["eval_sum"] = eval_sum();
            dict["energy"]["kin"] = energy_kin();
            dict["energy"]["ewald"] = energy_ewald();
            dict["efermi"] = kset_.energy_fermi();
            dict["band_gap"] = kset_.band_gap();
            dict["core_leakage"] = density_.core_leakage();

            return std::move(dict);
        }
};

};

#endif // __DFT_GROUND_STATE_H__

/** \page DFT Spin-polarized DFT
 *  \section section1 Preliminary notes
 *
 *  \note Here and below sybol \f$ {\boldsymbol \sigma} \f$ is reserved for the vector of Pauli matrices. Spin components 
 *        are labeled with \f$ \alpha \f$ or \f$ \beta\f$.
 *
 *  Wave-function of spin-1/2 particle is a two-component spinor:
 *  \f[
 *      {\bf \varphi}({\bf r})=\left( \begin{array}{c} \varphi_1({\bf r}) \\ \varphi_2({\bf r}) \end{array} \right)
 *  \f]
 *  Operator of spin:
 *  \f[
 *      {\bf \hat S}=\frac{\hbar}{2}{\bf \sigma},
 *  \f]
 *  Pauli matrices:
 *  \f[
 *      \sigma_x=\left( \begin{array}{cc}
 *         0 & 1 \\
 *         1 & 0 \\ \end{array} \right) \,
 *           \sigma_y=\left( \begin{array}{cc}
 *         0 & -i \\
 *         i & 0 \\ \end{array} \right) \,
 *           \sigma_z=\left( \begin{array}{cc}
 *         1 & 0 \\
 *         0 & -1 \\ \end{array} \right)
 *  \f]
 *
 *  \section section2 Density and magnetization
 *  Density is defined as:
 *  \f[
 *      \rho({\bf r}) = \sum_{j}^{occ} \Psi_{j}^{*}({\bf r}){\bf I} \Psi_{j}({\bf r}) = 
          \sum_{j}^{occ} \psi_{j}^{\uparrow *} \psi_{j}^{\uparrow} + \psi_{j}^{\downarrow *} \psi_{j}^{\downarrow} 

 *  \f]
 *  Magnetization is defined as:
 *  \f[
 *      {\bf m}({\bf r}) = \sum_{j}^{occ} \Psi_{j}^{*}({\bf r}) {\boldsymbol \sigma} \Psi_{j}({\bf r})
 *  \f]
 *  \f[
 *      m_x({\bf r}) = \sum_{j}^{occ} \psi_{j}^{\uparrow *} \psi_{j}^{\downarrow} + \psi_{j}^{\downarrow *} \psi_{j}^{\uparrow} 
 *  \f]
 *  \f[
 *      m_y({\bf r}) = \sum_{j}^{occ} -i \psi_{j}^{\uparrow *} \psi_{j}^{\downarrow} + i \psi_{j}^{\downarrow *} \psi_{j}^{\uparrow} 
 *  \f]
 *  \f[
 *      m_z({\bf r}) = \sum_{j}^{occ} \psi_{j}^{\uparrow *} \psi_{j}^{\uparrow} - \psi_{j}^{\downarrow *} \psi_{j}^{\downarrow} 
 *  \f]
 *  Density matrix is defined as:
 *  \f[
 *      {\boldsymbol \rho}({\bf r}) = \frac{1}{2} \Big( {\bf I}\rho({\bf r}) + {\boldsymbol \sigma} {\bf m}({\bf r})\Big) = 
 *        \frac{1}{2} \sum_{j}^{occ} \left( \begin{array}{cc} \psi_{j}^{\uparrow *} \psi_{j}^{\uparrow} & 
 *                                                            \psi_{j}^{\downarrow *} \psi_{j}^{\uparrow} \\
 *                                                            \psi_{j}^{\uparrow *} \psi_{j}^{\downarrow} &
 *                                                            \psi_{j}^{\downarrow *} \psi_{j}^{\downarrow} \end{array} \right)
 *  \f]
 *  Pay attention to the order of spin indices in the \f$ 2 \times 2 \f$ density matrix:
 *  \f[
 *    \rho_{\alpha \beta}({\bf r}) = \frac{1}{2} \sum_{j}^{occ} \psi_{j}^{\beta *}({\bf r})\psi_{j}^{\alpha}({\bf r})
 *  \f]
 */
