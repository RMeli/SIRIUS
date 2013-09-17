#ifndef __DFT_GROUND_STATE_H__
#define __DFT_GROUND_STATE_H__

/** \file dft_ground_state.h

    \brief Data and methods for DFT ground state calculation.

    \page DFT Spin-polarized DFT
    \section section1 Preliminary notes
    Wave-function of spin-1/2 particle is a two-component spinor:
    \f[
        {\bf \varphi}({\bf r})=\left( \begin{array}{c} \varphi_1({\bf r}) \\ \varphi_2({\bf r}) \end{array} \right)
    \f]
    Operator of spin:
    \f[
        {\bf \hat S}=\frac{\hbar}{2}{\bf \sigma},
    \f]
*/
namespace sirius
{

class DFT_ground_state
{
    private:

        Global& parameters_;

        Potential* potential_;

        Density* density_;

        K_set* kset_;

    public:

        DFT_ground_state(Global& parameters__, Potential* potential__, Density* density__, K_set* kset__) : 
            parameters_(parameters__), potential_(potential__), density_(density__), kset_(kset__)
        {

        }

        void move_atoms(int istep);

        void forces(mdarray<double, 2>& atom_force);

        void scf_loop();

        void relax_atom_positions();

        void update();

        /// Return nucleus energy in the electrostatic field.
        /** Compute energy of nucleus in the electrostatic potential generated by the total (electrons + nuclei) 
            charge density. Diverging self-interaction term z*z/|r=0| is excluded. */
        inline double energy_enuc();
        
        /// Return eigen-value sum of core states.
        inline double core_eval_sum();
        
        inline double energy_vha()
        {
            return inner(parameters_, density_->rho(), potential_->coulomb_potential());
        }
        
        inline double energy_vxc()
        {
            return inner(parameters_, density_->rho(), potential_->xc_potential());
        }
        
        inline double energy_exc()
        {
            return inner(parameters_, density_->rho(), potential_->xc_energy_density());
        }

        inline double energy_bxc()
        {
            double ebxc = 0.0;
            for (int j = 0; j < parameters_.num_mag_dims(); j++) 
                ebxc += inner(parameters_, density_->magnetization(j), potential_->effective_magnetic_field(j));
            return ebxc;
        }

        inline double energy_veff()
        {
            return inner(parameters_, density_->rho(), potential_->effective_potential());
        }

        /// Full eigen-value sum (core + valence)
        inline double eval_sum()
        {
            return (core_eval_sum() + kset_->valence_eval_sum());
        }
        
        /// Kinetic energy
        /** more doc here
        */
        inline double energy_kin()
        {
            return (eval_sum() - energy_veff() - energy_bxc());
        }

        /// Total energy of the electronic subsystem.
        /** From the definition of the density functional we have:
            
            \f[
                E[\rho] = T[\rho] + E^{H}[\rho] + E^{XC}[\rho] + E^{ext}[\rho]
            \f]
            where \f$ T[\rho] \f$ is the kinetic energy, \f$ E^{H}[\rho] \f$ - electrostatic energy of
            electron-electron density interaction, \f$ E^{XC}[\rho] \f$ - exchange-correlation energy
            and \f$ E^{ext}[\rho] \f$ - energy in the external field of nuclei.
            
            Electrostatic and external field energies are grouped in the following way:
            \f[
                \frac{1}{2} \int \int \frac{\rho({\bf r})\rho({\bf r'}) d{\bf r} d{\bf r'}}{|{\bf r} - {\bf r'}|} + 
                    \int \rho({\bf r}) V^{nuc}({\bf r}) d{\bf r} = \frac{1}{2} \int V^{H}({\bf r})\rho({\bf r})d{\bf r} + 
                    \frac{1}{2} \int \rho({\bf r}) V^{nuc}({\bf r}) d{\bf r}
            \f]
            Here \f$ V^{H}({\bf r}) \f$ is the total (electron + nuclei) electrostatic potential returned by the 
            poisson solver. Next we transform the remaining term:
            \f[
                \frac{1}{2} \int \rho({\bf r}) V^{nuc}({\bf r}) d{\bf r} = 
                \frac{1}{2} \int \int \frac{\rho({\bf r})\rho^{nuc}({\bf r'}) d{\bf r} d{\bf r'}}{|{\bf r} - {\bf r'}|} = 
                \frac{1}{2} \int V^{H,el}({\bf r}) \rho^{nuc}({\bf r}) d{\bf r}
            \f]
        */
        inline double total_energy()
        {
            return (energy_kin() + energy_exc() + 0.5 * energy_vha() + energy_enuc());
        }
};

#include "dft_ground_state.hpp"

};

#endif // __DFT_GROUND_STATE_H__
