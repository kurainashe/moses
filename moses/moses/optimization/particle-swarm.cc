/*
 * moses/moses/optimization/particle-swarm.cc
 *
 * Copyright (C) 2002-2008 Novamente LLC
 * Copyright (C) 2012 Poulin Holdings LLC
 * All Rights Reserved
 *
 * Written by Arley Ristar
 *            Nil Geisweiller
 *            Linas Vepstas
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <math.h>   // for sqrtf, cbrtf

#include <boost/algorithm/minmax_element.hpp>

#include <opencog/util/oc_omp.h>

#include "../moses/neighborhood_sampling.h"

#include "particle-swarm.h"

namespace opencog { namespace moses {

////////////////////
// Particle Swarm //
////////////////////

void particle_swarm::operator()(deme_t& best_parts,
                               const instance& init_inst,
                               const iscorer_base& iscorer,
                               unsigned max_evals,
                               time_t max_time)
{
    logger().debug("PSO...");

    log_stats_legend();

    // Maintain same name of variables from hill climbing
    // for better understanding.
    // Collect statistics about the run, in struct optim_stats
    nsteps = 0;
    demeID = best_parts.getID();
    over_budget = false;
    struct timeval start;
    gettimeofday(&start, NULL);

    const field_set& fields = best_parts.fields();
    // Track RAM usage. Instances can chew up boat-loads of RAM.
    _instance_bytes = sizeof(instance)
        + sizeof(packed_t) * fields.packed_width();

    unsigned swarm_size = calc_swarm_size(fields);
    int dim_size = fields.dim_size();

////// Particle Inicialization //////
    // Reserve uninitialized instances to not have to reallocate.
    // best_parts deme will be the best "personal" (or "local")
    // particles vector to be returned.
    best_parts.reserve(swarm_size);
    // New uninitialized velocity matrix
    // To update the instances
    std::vector<velocity> velocities(swarm_size,
            std::vector<double>(fields.dim_size()));
    // Discrete values of the instance aren't used for update,
    // because of that we need a structure similar to the continuous.
    discrete_particles
        disc_parts(swarm_size, fields.n_disc_fields());
    // Get the 3 uninitialized sets above and initialize then with
    // the instance definition inside fields and ps_params.
    initialize_particles(swarm_size,
            best_parts, velocities, disc_parts, fields);
    // Inicialization of particle best and global best, and their scores.
    auto temp_parts = best_parts;
    unsigned best_global = 0; // Any value
    // Copy the discrete information too
    disc_parts.temp = disc_parts.best_personal;


    // Equal to HC.
    composite_score best_cscore = worst_composite_score;
    score_t best_score = very_worst_score;
    score_t best_raw_score = very_worst_score;
    size_t current_number_of_evals = 0;

    unsigned iteration = 0;
    unsigned not_improving = 0;
    while(true){
        logger().debug("Iteration: %u", ++iteration);

        // score all instances in the deme
        OMP_ALGO::transform(temp_parts.begin(), temp_parts.end(), temp_parts.begin_scores(),
                            // using bind cref so that score is passed by
                            // ref instead of by copy
                            boost::bind(boost::cref(iscorer), _1));
        current_number_of_evals += swarm_size;

        // XXX What score do i use?
        // I'll use best_score for now.
        bool has_improved = false;
        for (unsigned i = 0; i < swarm_size; ++i) {
            const composite_score &inst_cscore = temp_parts[i].second;
            score_t iscore = inst_cscore.get_penalized_score();
            if(iscore > best_parts[i].second.get_penalized_score()){
                best_parts[i] = temp_parts[i];
                disc_parts.best_personal[i] = disc_parts.temp[i]; //For discrete
                has_improved = true;
                if (iscore >  best_parts[best_global].second.get_penalized_score()) {
                    best_cscore = inst_cscore;
                    best_score = iscore;
                    best_global = i;
                }
            }
            // The instance with the best raw score will typically
            // *not* be the same as the the one with the best
            // weighted score.  We need the raw score for the
            // termination condition, as, in the final answer, we
            // want the best raw score, not the best weighted score.
            score_t rscore = inst_cscore.get_score();
            if (rscore >  best_raw_score) {
                best_raw_score = rscore;
            }
        }

        // Collect statistics about the run, in struct optim_stats
        struct timeval stop, elapsed;
        gettimeofday(&stop, NULL);
        timersub(&stop, &start, &elapsed);
        start = stop;
        unsigned usec = 1000000 * elapsed.tv_sec + elapsed.tv_usec;

        /* If we've blown our budget for evaluating the scorer,
         * then we are done. */
        if (max_evals <= current_number_of_evals) {
            over_budget = true;
            logger().debug("Terminate Local Search: Over budget");
            break;
        }

        if (max_time <= elapsed.tv_sec) {
            over_budget = true;
            logger().debug("Terminate Local Search: Out of time");
            break;
        }
        max_time -= elapsed.tv_sec; // count-down to zero.

        /* If we've aleady gotten the best possible score, we are done. */
        if (opt_params.terminate_if_gte <= best_raw_score) {
            logger().debug("Terminate Local Search: Found best score");
            break;
        }

        // TODO: work in a better way to identify convergence.
        not_improving = (has_improved) ? 0 : not_improving + 1;
        if (not_improving > 2) {
            logger().debug("Terminate Local Search: Convergence.");
            break;
        }

        // Update particles
        update_particles(swarm_size, best_parts, temp_parts,
                best_global, velocities, disc_parts, fields);
    }

    best_parts.n_best_evals = swarm_size;
    best_parts.n_evals = current_number_of_evals;

} // ~operator

////// The functions below are ordered by utilization order inside the function above.

void particle_swarm::log_stats_legend()
{
    logger().info() << "PSO: # "   /* Legend for graph stats */
        "demeID\t"
        "iteration\t"
        "total_steps\t"
        "total_evals\t"
        "microseconds\t"
        "new_instances\t"
        "num_instances\t"
        "inst_RAM\t"
        "num_evals\t"
        "has_improved\t"
        "best_weighted_score\t"
        "delta_weighted\t"
        "best_raw\t"
        "delta_raw\t"
        "complexity";
}

// TODO: Explanation
// There's no explanation for this, it's just a temporary solution.
// Maybe use adaptative pso, something like LPSO (Lander).
unsigned particle_swarm::calc_swarm_size(const field_set& fs) {
    // For disc i'll the same of bit, for bit i'll use a proportion of disc_t value.
    const double byte_relation = 3.0 / 4.0, // For each 4 bytes i'll let it similar to a cont.
                cont_relation = 3; // Normally 3x or 4x of the dimension.

    unsigned disc_bit_size = sizeof(instance) -
            (fs.n_contin_fields() * sizeof(contin_t));

    double total = disc_bit_size * byte_relation +
                    fs.n_contin_fields() * cont_relation;
    check_bounds(total, 4, ps_params.max_parts); // 4 For min, less than this is almost useless.
    return std::round(total); // Round it.
}

void particle_swarm::initialize_particles (const unsigned& swarm_size,
        deme_t& best_parts, std::vector<velocity>& velocities,
        discrete_particles& disc_parts, const field_set& fields) {
    auto velit = velocities.begin();
    auto dvit = disc_parts.best_personal.begin();
    dorepeat(swarm_size){
        instance new_inst(fields.packed_width());
        initialize_random_particle(
            new_inst, *velit, *dvit, fields);
        velit++; dvit++;
        best_parts.push_back(new_inst);
    }
}

void particle_swarm::initialize_random_particle (instance& new_inst,
        velocity& vel, std::vector<double>& dist_values, const field_set& fs){
    auto vit = vel.begin();
    // For each bit
    for(auto it = fs.begin_bit(new_inst);
            it != fs.end_bit(new_inst); ++it, ++vit) {
        *it = gen_bit_value(); // New bit value in instance
        *vit = gen_bit_vel(); // New bit velocity
    }
    // For each disc
    auto dit = dist_values.begin();
    for(auto it = fs.begin_disc(new_inst);
            it != fs.end_disc(new_inst); ++it, ++vit, ++dit) {
        *dit = gen_disc_value(); // New cont value for disc
        *it = cont2disc(*dit, it.multy()); // New disc value in instance
        *vit = gen_disc_vel(); // New disc velocity
    }
    // For each contin
    for(auto it = fs.begin_contin(new_inst);
            it != fs.end_contin(new_inst); ++it, ++vit) {
        *it = gen_cont_value(); // New cont value in instance
        *vit = gen_cont_vel(); // New cont velocity
    }
}

void particle_swarm::update_particles(const unsigned& swarm_size,
        deme_t& best_parts, deme_t& temp_parts, const unsigned& best_global,
        std::vector<velocity>& velocities, discrete_particles& disc_parts, const field_set& fields) {

    // TODO: Update Particles
    for(unsigned part = 0; part < swarm_size; ++part){ // Part == particle index
        unsigned dim = 0; // Dim == dimension index

        // Bit velocity update
        // XXX IT ISN'T THE ORIGINAL BPSO it's just a test for now
        for(auto tit = fields.begin_bit(temp_parts[part].first),
                lit = fields.begin_bit(best_parts[part].first),
                git = fields.begin_bit(best_parts[best_global].first);
                tit != fields.end_bit(temp_parts[part].first);
                ++tit, ++lit, ++git, ++dim){ //Next

            double& vel = velocities[part][dim];
            vel = (ps_params.inertia * vel) + // Maintain velocity
                (cogconst * randGen().randdouble() * (*lit - *tit)) + // Go to my best
                (socialconst * randGen().randdouble() * (*git - *tit)); // Go to global best
            check_bit_vel(vel); // check bounds for bit velocity
            //*tit = ((*tit + vel) > 0.5) ? true : false;
            //logger().debug("test: %d", *dit);
        }
         // Discrete velocity update
         // XXX IT ISN'T THE ORIGINAL DPSO it's just a test for now
        for(auto tit = fields.begin_disc(temp_parts[part].first),
                lit = fields.begin_disc(best_parts[part].first),
                git = fields.begin_disc(best_parts[best_global].first);
                tit != fields.end_disc(temp_parts[part].first);
            ++tit, ++lit, ++git, ++dim){ //Next

            double& vel = velocities[part][dim];
            logger().debug("vi: %f", vel);
            vel = (ps_params.inertia * vel) + // Maintain velocity
                (cogconst * randGen().randdouble()
                 * (double)(*lit - *tit)) + // Go to my best
                (socialconst * randGen().randdouble()
                 * (double)(*git - *tit)); // Go to global best
            check_disc_vel(vel); // check bounds for disc velocity
            logger().debug("vf: %f", vel);
            logger().debug("testi: %d", (int) *tit);
            *tit = std::fmin(std::fmax((((double) *tit) + vel), 0), tit.multy()-1);// CONFINAMENT WITHOUT WIND DISPERSION
            logger().debug("testf: %d", (int) *tit);
        }
        // Continuos velocity update
        // This is the original one
        for(auto tit = fields.begin_contin(temp_parts[part].first),
                lit = fields.begin_contin(best_parts[part].first),
                git = fields.begin_contin(best_parts[best_global].first);
                tit != fields.end_contin(temp_parts[part].first);
            ++tit, ++lit, ++git, ++dim){ //Next

            double& vel = velocities[part][dim];
            vel = (ps_params.inertia * vel) + // Maintain velocity
                (cogconst * randGen().randdouble() * (*lit - *tit)) + // Go to my best
                (socialconst * randGen().randdouble() * (*git - *tit)); // Go to global best
            check_cont_vel(vel); // check bounds for contin velocity
            //*dit = *dit + vel;
        }
    }
}

void particle_swarm::fill_disc_instance(
        const std::vector<double>& cvalues, instance& inst) {

}


} // ~namespace moses
} // ~namespace opencog

