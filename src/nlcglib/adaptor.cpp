#include "memory.hpp"
#include "wave_functions.hpp"
#ifdef SIRIUS_NLCGLIB
#include <stdexcept>

#include "adaptor.hpp"
#include "apply_hamiltonian.hpp"
#include "hamiltonian/local_operator.hpp"
#include "hamiltonian/hamiltonian.hpp"
#include "dft/energy.hpp"

using namespace nlcglib;

namespace sirius {

template<class wfc_ptr_t>
std::shared_ptr<Matrix>
make_vector(const std::vector<wfc_ptr_t>& wfct, const Simulation_context& ctx, const K_point_set& kset, nlcglib::memory_type memory = nlcglib::memory_type::none)
{
    std::map<sddk::memory_t, nlcglib::memory_type> memtype = {
        {sddk::memory_t::device, nlcglib::memory_type::device},
        {sddk::memory_t::host, nlcglib::memory_type::host},
        {sddk::memory_t::host_pinned, nlcglib::memory_type::host}};
    std::map<nlcglib::memory_type, sddk::memory_t> memtype_lookup = {
        {nlcglib::memory_type::none, sddk::memory_t::none},
        {nlcglib::memory_type::device, sddk::memory_t::device},
        {nlcglib::memory_type::host, sddk::memory_t::host},
        {nlcglib::memory_type::host, sddk::memory_t::host_pinned}};

    sddk::memory_t target_memory = memtype_lookup.at(memory);
    if (target_memory == sddk::memory_t::none) {
        target_memory = ctx.processing_unit_memory_t();
    }

    std::vector<Matrix::buffer_t> data;
    std::vector<std::pair<int, int>> kpoint_indices;
    // sddk::memory_t preferred_memory = ctx.preferred_memory_t();
    int num_spins = ctx.num_spins();
    int nb        = ctx.num_bands();
    for (auto i = 0u; i < wfct.size(); ++i) {
        auto gidk = kset.spl_num_kpoints(i); // global k-point index
        for (int ispn = 0; ispn < num_spins; ++ispn) {
            auto& array   = wfct[i]->pw_coeffs(wf::spin_index(ispn));
            int lda       = array.size(0);
            MPI_Comm comm = wfct[i]->comm().native();
            // check that wfct has been allocated
            if (is_device_memory(target_memory)) {
                // make sure that array is on device
                if (!array.on_device()) {
                    throw std::runtime_error("Error: expected device storage, but got nullptr");
                }
            }
            kpoint_indices.emplace_back(std::make_pair(gidk, ispn));
            data.emplace_back(std::array<int, 2>{1, lda},  /* stride */
                              std::array<int, 2>{lda, nb}, /* size */
                              array.at(target_memory),     /* pointer */
                              memtype.at(target_memory), comm /* mpi communicator */);
        }
    }
    return std::make_shared<Matrix>(std::move(data), std::move(kpoint_indices), kset.comm().native());
}



Matrix::buffer_t Matrix::get(int i)
{
    return data[i];
}

const Matrix::buffer_t
Matrix::get(int i) const
{
    return data[i];
}

Energy::Energy(K_point_set& kset, Density& density, Potential& potential)
    : kset(kset)
    , density(density)
    , potential(potential)
{
    // intialize hphi and sphi and allocate (device) memory
    int nk    = kset.spl_num_kpoints().local_size();
    auto& ctx = kset.ctx();
    // auto& mpd = ctx.mem_pool(ctx.preferred_memory_t());
    hphis.resize(nk);
    sphis.resize(nk);
    cphis.resize(nk);
    for (int i = 0; i < nk; ++i) {
        auto global_kpoint_index          = kset.spl_num_kpoints(i);
        auto& kp                          = *kset.get<double>(global_kpoint_index);
        sddk::memory_t preferred_memory_t = ctx.processing_unit_memory_t();
        auto num_mag_dims = wf::num_mag_dims(ctx.num_mag_dims());
        auto num_bands = wf::num_bands(ctx.num_bands());
        // make a new wf for Hamiltonian apply...
        hphis[i] = std::make_shared<wf::Wave_functions<double>>(kp.gkvec_sptr(), num_mag_dims, num_bands, preferred_memory_t);
        hphis[i]->allocate(preferred_memory_t);
        sphis[i] = std::make_shared<wf::Wave_functions<double>>(kp.gkvec_sptr(), num_mag_dims, num_bands, preferred_memory_t);
        sphis[i]->allocate(preferred_memory_t);
        cphis[i] = &kp.spinor_wave_functions();
        // allocate on device
        if (is_device_memory(preferred_memory_t)) {
            const int num_sc = (ctx.num_mag_dims() == 3) ? 2 : 1;
            auto& mpd        = sddk::get_memory_pool(sddk::memory_t::device);
            for (int ispn = 0; ispn < num_sc; ispn++) {
                hphis[i]->pw_coeffs(wf::spin_index(ispn)).allocate(mpd);
                sphis[i]->pw_coeffs(wf::spin_index(ispn)).allocate(mpd);
            }
        }
    }
    // need to allocate wavefunctions on GPU
}

void
Energy::compute()
{
    auto& ctx     = kset.ctx();
    int num_spins = ctx.num_spins();
    int num_bands = ctx.num_bands();
    int nk        = kset.spl_num_kpoints().local_size();

    density.generate<double>(kset, ctx.use_symmetry(), true /* add core */, true /* transform to rg */);

    potential.generate(density, ctx.use_symmetry(), true);

    /* compute H@X and new band energies */
    auto H0 = Hamiltonian0<double>(potential, true);

    auto proc_mem_t = ctx.processing_unit_memory_t();

    // apply Hamiltonian
    for (int i = 0; i < nk; ++i) {
        auto& kp = *kset.get<double>(kset.spl_num_kpoints(i));
        std::vector<double> band_energies(num_bands);

        auto mem_guard = cphis[i]->memory_guard(proc_mem_t);
        // TODO change
        if (is_device_memory(ctx.processing_unit_memory_t())) {
            auto& mpd = get_memory_pool(sddk::memory_t::device);
            for (int ispn = 0; ispn < num_spins; ispn++) {
                cphis[i]->pw_coeffs(wf::spin_index(ispn)).allocate(mpd);
                // copy to device
                int num_wf = cphis[i]->num_wf();
                cphis[i]->pw_coeffs(wf::spin_index(ispn)).copy_to(sddk::memory_t::device, 0, num_wf);
            }
        }

        assert(cphis[i] == &kp.spinor_wave_functions());
        apply_hamiltonian(H0, kp, *hphis[i], kp.spinor_wave_functions(), sphis[i]);
        // compute band energies aka diag(<psi|H|psi>)
        for (int ispn = 0; ispn < num_spins; ++ispn) {
            for (int jj = 0; jj < num_bands; ++jj) {
                la::dmatrix<std::complex<double>> dmat(1, 1, sddk::memory_t::host);
                dmat.allocate(sddk::memory_t::device);
                wf::band_range bandr{jj, jj + 1};
                wf::inner(ctx.spla_context(), proc_mem_t, wf::spin_range(ispn),
                          /* bra */ kp.spinor_wave_functions(), bandr,
                          /* ket */ *hphis[i], bandr,
                          /*result*/ dmat, 0, 0);
                kp.band_energy(jj, ispn, dmat(0, 0).real());
            }
        }
    }
    kset.sync_band<double, sync_band_t::energy>();

    // evaluate total energy
    double eewald = ewald_energy(ctx, ctx.gvec(), ctx.unit_cell());
    this->etot    = total_energy(ctx, kset, density, potential, eewald);
}

int
Energy::occupancy()
{
    return kset.ctx().max_occupancy();
}

int
Energy::nelectrons()
{
    return kset.unit_cell().num_electrons();
}

std::shared_ptr<nlcglib::MatrixBaseZ>
Energy::get_hphi()
{
    return make_vector(this->hphis, this->kset.ctx(), this->kset);
}

std::shared_ptr<nlcglib::MatrixBaseZ>
Energy::get_sphi()
{
    return make_vector(this->sphis, this->kset.ctx(), this->kset);
}

std::shared_ptr<nlcglib::MatrixBaseZ>
Energy::get_C(nlcglib::memory_type memory = nlcglib::memory_type::none)
{
    return make_vector(this->cphis, this->kset.ctx(), this->kset, memory);
}

std::shared_ptr<nlcglib::VectorBaseZ>
Energy::get_fn()
{
    auto nk      = kset.spl_num_kpoints().local_size();
    const int ns = kset.ctx().num_spins();
    int nbands   = kset.ctx().num_bands();
    std::vector<std::vector<double>> fn;
    std::vector<std::pair<int, int>> kindices;
    for (int ik = 0; ik < nk; ++ik) {
        // global k-point index
        auto gidk = kset.spl_num_kpoints(ik);
        auto& kp  = *kset.get<double>(gidk);
        for (int ispn = 0; ispn < ns; ++ispn) {
            std::vector<double> fn_local(nbands);
            for (int i = 0; i < nbands; ++i) {
                fn_local[i] = kp.band_occupancy(i, ispn);
            }
            fn.push_back(std::move(fn_local));
            kindices.emplace_back(gidk, ispn);
        }
    }
    return std::make_shared<Array1d>(fn, kindices, kset.comm().native());
}

void
Energy::set_fn(const std::vector<std::pair<int, int>>& keys, const std::vector<std::vector<double>>& fn)
{
    const int nbands = kset.ctx().num_bands();
#ifndef NDEBUG
    const int ns         = kset.ctx().num_spins();
    auto nk              = kset.spl_num_kpoints().local_size();
    const double max_occ = ns == 1 ? 2.0 : 1.0;
#endif

    assert(static_cast<int>(fn.size()) == nk * ns);
    for (size_t iloc = 0; iloc < fn.size(); ++iloc) {
        // global k-point index
        int gidk           = keys[iloc].first;
        int ispn           = keys[iloc].second;
        auto& kp           = *kset.get<double>(gidk);
        const auto& fn_loc = fn[iloc];
        assert(static_cast<int>(fn_loc.size()) == nbands);
        for (int i = 0; i < nbands; ++i) {
            assert(fn_loc[i] >= 0 && fn_loc[i] <= max_occ);
            kp.band_occupancy(i, ispn, fn_loc[i]);
        }
    }
    kset.sync_band<double, sync_band_t::occupancy>();
}

std::shared_ptr<nlcglib::VectorBaseZ>
Energy::get_ek()
{
    auto nk      = kset.spl_num_kpoints().local_size();
    const int ns = kset.ctx().num_spins();
    int nbands   = kset.ctx().num_bands();
    std::vector<std::vector<double>> ek;
    std::vector<std::pair<int, int>> kindices;
    for (int ik = 0; ik < nk; ++ik) {
        // global k-point index
        auto gidk = kset.spl_num_kpoints(ik);
        auto& kp  = *kset.get<double>(gidk);
        for (int ispn = 0; ispn < ns; ++ispn) {
            std::vector<double> ek_local(nbands);
            for (int i = 0; i < nbands; ++i) {
                ek_local[i] = kp.band_energy(i, ispn);
            }
            ek.push_back(std::move(ek_local));
            kindices.emplace_back(gidk, ispn);
        }
    }
    return std::make_shared<Array1d>(ek, kindices, kset.comm().native());
}

std::shared_ptr<nlcglib::VectorBaseZ>
Energy::get_gkvec_ekin()
{
    auto nk      = kset.spl_num_kpoints().local_size();
    const int ns = kset.ctx().num_spins();
    std::vector<std::vector<double>> gkvec_cart;
    std::vector<std::pair<int, int>> kindices;
    for (int ik = 0; ik < nk; ++ik) {
        // global k-point index
        auto gidk = kset.spl_num_kpoints(ik);
        auto& kp  = *kset.get<double>(gidk);
        for (int ispn = 0; ispn < ns; ++ispn) {
            int gkvec_count = kp.gkvec().count();
            auto& gkvec     = kp.gkvec();
            std::vector<double> gkvec_local(gkvec_count);
            for (int i = 0; i < gkvec_count; ++i) {
                gkvec_local[i] = gkvec.gkvec_cart<sddk::index_domain_t::global>(i).length();
            }
            gkvec_cart.push_back(std::move(gkvec_local));
            kindices.emplace_back(gidk, ispn);
        }
    }
    return std::make_shared<Array1d>(gkvec_cart, kindices, kset.comm().native());
}

std::shared_ptr<nlcglib::ScalarBaseZ>
Energy::get_kpoint_weights()
{
    auto nk      = kset.spl_num_kpoints().local_size();
    const int ns = kset.ctx().num_spins();
    std::vector<double> weights;
    std::vector<std::pair<int, int>> kindices;
    for (int ik = 0; ik < nk; ++ik) {
        // global k-point index
        auto gidk = kset.spl_num_kpoints(ik);
        auto& kp  = *kset.get<double>(gidk);

        // also return weights for every spin index
        for (int ispn = 0; ispn < ns; ++ispn) {
            weights.push_back(kp.weight());
            kindices.emplace_back(gidk, ispn);
        }
    }
    return std::make_shared<Scalar>(weights, kindices, kset.comm().native());
}

double
Energy::get_total_energy()
{
    return etot;
}

void
Energy::print_info() const
{
    auto& ctx       = kset.ctx();
    auto& unit_cell = kset.unit_cell();

    auto result_mag = density.get_magnetisation();
    auto mt_mag     = std::get<2>(result_mag);

    if (ctx.num_mag_dims()) {
        std::printf("atom              moment                |moment|");
        std::printf("\n");
        for (int i = 0; i < 80; i++) {
            std::printf("-");
        }
        std::printf("\n");

        for (int ia = 0; ia < unit_cell.num_atoms(); ia++) {
            r3::vector<double> v(mt_mag[ia]);
            std::printf("%4i  [%8.4f, %8.4f, %8.4f]  %10.6f", ia, v[0], v[1], v[2], v.length());
            std::printf("\n");
        }

        std::printf("\n");
    }
}

Array1d::buffer_t
Array1d::get(int i)
{
    // call 1d constructor
    return buffer_t(data[i].size(), data[i].data(), nlcglib::memory_type::host);
}

const Array1d::buffer_t
Array1d::get(int i) const
{
    // call 1d constructor
    return buffer_t(data[i].size(), const_cast<double*>(data[i].data()), nlcglib::memory_type::host);
}

} // namespace sirius
#endif
