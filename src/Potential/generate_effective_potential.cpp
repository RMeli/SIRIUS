#include "potential.h"

namespace sirius {

void Potential::generate_effective_potential(Periodic_function<double>* rho, 
                                             Periodic_function<double>* magnetization[3])
{
    PROFILE_WITH_TIMER("sirius::Potential::generate_effective_potential");

    /* zero effective potential and magnetic field */
    zero();

    /* solve Poisson equation */
    poisson(rho, hartree_potential_);

    /* add Hartree potential to the total potential */
    effective_potential_->add(hartree_potential_);

    xc(rho, magnetization, xc_potential_, effective_magnetic_field_, xc_energy_density_);
   
    effective_potential_->add(xc_potential_);
    
    if (parameters_.full_potential())
    {
        effective_potential_->sync_mt();
        for (int j = 0; j < parameters_.num_mag_dims(); j++) effective_magnetic_field_[j]->sync_mt();
    }

    //if (debug_level > 1) check_potential_continuity_at_mt();
}

void Potential::generate_effective_potential(Periodic_function<double>* rho, 
                                             Periodic_function<double>* rho_core, 
                                             Periodic_function<double>* magnetization[3])
{
    PROFILE_WITH_TIMER("sirius::Potential::generate_effective_potential");

    /* zero effective potential and magnetic field */
    zero();

    /* solve Poisson equation with valence density */
    poisson(rho, hartree_potential_);

    /* add Hartree potential to the effective potential */
    effective_potential_->add(hartree_potential_);

    /* create temporary function for rho + rho_core */
    Periodic_function<double> rhovc(ctx_, 0, nullptr);
    rhovc.zero();
    rhovc.add(rho);
    rhovc.add(rho_core);

    /* construct XC potentials from rho + rho_core */
    xc(&rhovc, magnetization, xc_potential_, effective_magnetic_field_, xc_energy_density_);
    
    /* add XC potential to the effective potential */
    effective_potential_->add(xc_potential_);
    
    /* add local ionic potential to the effective potential */
    effective_potential_->add(local_potential_);

    generate_D_operator_matrix();
}

};
