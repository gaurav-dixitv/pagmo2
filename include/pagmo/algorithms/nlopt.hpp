/* Copyright 2017 PaGMO development team

This file is part of the PaGMO library.

The PaGMO library is free software; you can redistribute it and/or modify
it under the terms of either:

  * the GNU Lesser General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your
    option) any later version.

or

  * the GNU General Public License as published by the Free Software
    Foundation; either version 3 of the License, or (at your option) any
    later version.

or both in parallel, as here.

The PaGMO library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received copies of the GNU General Public License and the
GNU Lesser General Public License along with the PaGMO library.  If not,
see https://www.gnu.org/licenses/. */

#ifndef PAGMO_ALGORITHMS_NLOPT_HPP
#define PAGMO_ALGORITHMS_NLOPT_HPP

#include <pagmo/config.hpp>

#if defined(PAGMO_WITH_NLOPT)

#include <algorithm>
#include <boost/any.hpp>
#include <boost/bimap.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <nlopt.h>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <pagmo/exceptions.hpp>
#include <pagmo/io.hpp>
#include <pagmo/population.hpp>
#include <pagmo/problem.hpp>
#include <pagmo/rng.hpp>
#include <pagmo/type_traits.hpp>
#include <pagmo/types.hpp>
#include <pagmo/utils/constrained.hpp>

#if defined(_MSC_VER)

// Disable a warning from MSVC.
#pragma warning(push, 0)
#pragma warning(disable : 4996)

#endif

namespace pagmo
{

namespace detail
{

// Usual trick with global read-only data useful to the NLopt wrapper.
template <typename = void>
struct nlopt_data {
    // The idea here is to establish a bijection between string name (e.g., "cobyla")
    // and the enums used in the NLopt C API to refer to the algos (e.g., NLOPT_LN_COBYLA).
    // We use a bidirectional map so that we can map both string -> enum and enum -> string,
    // depending on what is needed.
    using names_map_t = boost::bimap<std::string, ::nlopt_algorithm>;
    static const names_map_t names;
    // A map to link a human-readable description to NLopt return codes.
    // NOTE: in C++11 hashing of enums might not be available. Provide our own.
    struct res_hasher {
        std::size_t operator()(::nlopt_result res) const
        {
            return std::hash<int>{}(static_cast<int>(res));
        }
    };
    using result_map_t = std::unordered_map<::nlopt_result, std::string, res_hasher>;
    static const result_map_t results;
};

// Initialise the mapping between algo names and enums for the supported algorithms.
inline typename nlopt_data<>::names_map_t nlopt_names_map()
{
    typename nlopt_data<>::names_map_t retval;
    using value_type = typename nlopt_data<>::names_map_t::value_type;
    retval.insert(value_type("cobyla", NLOPT_LN_COBYLA));
    retval.insert(value_type("bobyqa", NLOPT_LN_BOBYQA));
    retval.insert(value_type("newuoa", NLOPT_LN_NEWUOA));
    retval.insert(value_type("newuoa_bound", NLOPT_LN_NEWUOA_BOUND));
    retval.insert(value_type("praxis", NLOPT_LN_PRAXIS));
    retval.insert(value_type("neldermead", NLOPT_LN_NELDERMEAD));
    retval.insert(value_type("sbplx", NLOPT_LN_SBPLX));
    retval.insert(value_type("mma", NLOPT_LD_MMA));
    retval.insert(value_type("ccsaq", NLOPT_LD_CCSAQ));
    retval.insert(value_type("slsqp", NLOPT_LD_SLSQP));
    retval.insert(value_type("lbfgs", NLOPT_LD_LBFGS));
    retval.insert(value_type("tnewton_precond_restart", NLOPT_LD_TNEWTON_PRECOND_RESTART));
    retval.insert(value_type("tnewton_precond", NLOPT_LD_TNEWTON_PRECOND));
    retval.insert(value_type("tnewton_restart", NLOPT_LD_TNEWTON_RESTART));
    retval.insert(value_type("tnewton", NLOPT_LD_TNEWTON));
    retval.insert(value_type("var2", NLOPT_LD_VAR2));
    retval.insert(value_type("var1", NLOPT_LD_VAR1));
    return retval;
}

// Static init using the helper function above.
template <typename T>
const typename nlopt_data<T>::names_map_t nlopt_data<T>::names = nlopt_names_map();

// Other static init.
template <typename T>
const typename nlopt_data<T>::result_map_t nlopt_data<T>::results = {
    {NLOPT_SUCCESS, "NLOPT_SUCCESS (value = " + std::to_string(NLOPT_SUCCESS) + ", Generic success return value)"},
    {NLOPT_STOPVAL_REACHED, "NLOPT_STOPVAL_REACHED (value = " + std::to_string(NLOPT_STOPVAL_REACHED)
                                + ", Optimization stopped because stopval was reached)"},
    {NLOPT_FTOL_REACHED, "NLOPT_FTOL_REACHED (value = " + std::to_string(NLOPT_FTOL_REACHED)
                             + ", Optimization stopped because ftol_rel or ftol_abs was reached)"},
    {NLOPT_XTOL_REACHED, "NLOPT_XTOL_REACHED (value = " + std::to_string(NLOPT_XTOL_REACHED)
                             + ", Optimization stopped because xtol_rel or xtol_abs was reached)"},
    {NLOPT_MAXEVAL_REACHED, "NLOPT_MAXEVAL_REACHED (value = " + std::to_string(NLOPT_MAXEVAL_REACHED)
                                + ", Optimization stopped because maxeval was reached)"},
    {NLOPT_MAXTIME_REACHED, "NLOPT_MAXTIME_REACHED (value = " + std::to_string(NLOPT_MAXTIME_REACHED)
                                + ", Optimization stopped because maxtime was reached)"},
    {NLOPT_FAILURE, "NLOPT_FAILURE (value = " + std::to_string(NLOPT_FAILURE) + ", Generic failure code)"},
    {NLOPT_INVALID_ARGS, "NLOPT_INVALID_ARGS (value = " + std::to_string(NLOPT_INVALID_ARGS) + ", Invalid arguments)"},
    {NLOPT_OUT_OF_MEMORY,
     "NLOPT_OUT_OF_MEMORY (value = " + std::to_string(NLOPT_OUT_OF_MEMORY) + ", Ran out of memory)"},
    {NLOPT_ROUNDOFF_LIMITED, "NLOPT_ROUNDOFF_LIMITED (value = " + std::to_string(NLOPT_ROUNDOFF_LIMITED)
                                 + ", Halted because roundoff errors limited progress)"},
    {NLOPT_FORCED_STOP,
     "NLOPT_FORCED_STOP (value = " + std::to_string(NLOPT_FORCED_STOP) + ", Halted because of a forced termination)"}};

// Convert an NLopt result in a more descriptive string.
inline std::string nlopt_res2string(::nlopt_result err)
{
    return (nlopt_data<>::results.find(err) == nlopt_data<>::results.end() ? "??" : nlopt_data<>::results.at(err));
}

struct nlopt_obj {
    // Single entry of the log (feval, fitness, n of unsatisfied const, constr. violation, feasibility).
    using log_line_type = std::tuple<unsigned long, double, vector_double::size_type, double, bool>;
    // The log.
    using log_type = std::vector<log_line_type>;
    // Shortcut to the static data.
    using data = nlopt_data<>;
    explicit nlopt_obj(::nlopt_algorithm algo, problem &prob, double stopval, double ftol_rel, double ftol_abs,
                       double xtol_rel, double xtol_abs, int maxeval, int maxtime, unsigned verbosity)
        : m_prob(prob), m_value(nullptr, ::nlopt_destroy), m_verbosity(verbosity)
    {
        // If needed, init the sparsity pattern.
        if (prob.has_gradient_sparsity()) {
            m_sp = prob.gradient_sparsity();
        }

        // Extract and set problem dimension.
        const auto n = boost::numeric_cast<unsigned>(prob.get_nx());
        m_value.reset(::nlopt_create(algo, n));
        // Try to init the nlopt_obj.
        if (!m_value) {
            pagmo_throw(std::runtime_error, "the creation of the nlopt_opt object failed");
        }

        // NLopt does not handle MOO.
        if (prob.get_nobj() != 1u) {
            pagmo_throw(std::invalid_argument, "NLopt algorithms cannot handle multi-objective optimization");
        }

        // Variable to hold the result of various operations.
        ::nlopt_result res;

        // Box bounds.
        const auto bounds = prob.get_bounds();
        res = ::nlopt_set_lower_bounds(m_value.get(), bounds.first.data());
        if (res != NLOPT_SUCCESS) {
            pagmo_throw(std::invalid_argument, "could not set the lower bounds for the NLopt algorithm '"
                                                   + data::names.right.at(algo) + "', the error is: "
                                                   + nlopt_res2string(res));
        }
        res = ::nlopt_set_upper_bounds(m_value.get(), bounds.second.data());
        if (res != NLOPT_SUCCESS) {
            pagmo_throw(std::invalid_argument, "could not set the upper bounds for the NLopt algorithm '"
                                                   + data::names.right.at(algo) + "', the error is: "
                                                   + nlopt_res2string(res));
        }

        // This is just a vector_double that is re-used across objfun invocations.
        // It will hold the current decision vector.
        m_dv.resize(prob.get_nx());

        // Set the objfun + gradient.
        res = ::nlopt_set_min_objective(
            m_value.get(),
            [](unsigned dim, const double *x, double *grad, void *f_data) -> double {
                // Get *this back from the function data.
                auto &nlo = *static_cast<nlopt_obj *>(f_data);

                // A few shortcuts.
                auto &p = nlo.m_prob;
                auto &dv = nlo.m_dv;
                const auto verb = nlo.m_verbosity;
                auto &f_count = nlo.m_objfun_counter;
                auto &log = nlo.m_log;

                // A couple of sanity checks.
                assert(dim == p.get_nx());
                assert(dv.size() == dim);

                if (grad && !p.has_gradient()) {
                    // If grad is not null, it means we are in an algorithm
                    // that needs the gradient. If the problem does not support it,
                    // we error out.
                    pagmo_throw(std::invalid_argument,
                                "during an optimization with the NLopt algorithm '"
                                    + data::names.right.at(::nlopt_get_algorithm(nlo.m_value.get()))
                                    + "' a fitness gradient was requested, but the optimisation problem '"
                                    + p.get_name() + "' does not provide it");
                }

                // Copy the decision vector in our temporary dv vector_double,
                // for use in the pagmo API.
                std::copy(x, x + dim, dv.begin());

                // Compute fitness and, if needed, gradient.
                const auto fitness = p.fitness(dv);
                if (grad) {
                    const auto gradient = p.gradient(dv);

                    if (p.has_gradient_sparsity()) {
                        // Sparse gradient case.
                        auto &sp = nlo.m_sp;
                        // NOTE: problem::gradient() has already checked that
                        // the returned vector has size m_gs_dim, i.e., the stored
                        // size of the sparsity pattern. On the other hand,
                        // problem::gradient_sparsity() also checks that the returned
                        // vector has size m_gs_dim, so these two must have the same size.
                        assert(gradient.size() == sp.size());
                        auto g_it = gradient.begin();

                        // First we fill the dense output gradient with zeroes.
                        std::fill(grad, grad + dim, 0.);
                        // Then we iterate over the sparsity pattern, and fill in the
                        // nonzero bits in grad.
                        for (auto it = sp.begin(); it != sp.end() && it->first == 0u; ++it, ++g_it) {
                            // NOTE: we just need the gradient of the objfun,
                            // i.e., those (i,j) pairs in which i == 0. We know that the gradient
                            // of the objfun, if present, starts at the beginning of sp, as sp is
                            // sorted in lexicographic fashion.
                            grad[it->second] = *g_it;
                        }
                    } else {
                        // Dense gradient case.
                        std::copy(gradient.data(), gradient.data() + p.get_nx(), grad);
                    }
                }

                // Update the log if requested.
                if (verb && !(f_count % verb)) {
                    // Constraints bits.
                    const auto ctol = p.get_c_tol();
                    const auto c1eq = detail::test_eq_constraints(fitness.data() + 1, fitness.data() + 1 + p.get_nec(),
                                                                  ctol.data());
                    const auto c1ineq = detail::test_ineq_constraints(
                        fitness.data() + 1 + p.get_nec(), fitness.data() + fitness.size(), ctol.data() + p.get_nec());
                    // This will be the total number of violated constraints.
                    const auto nv = p.get_nc() - c1eq.first - c1ineq.first;
                    // This will be the norm of the violation.
                    const auto l = c1eq.second + c1ineq.second;
                    // Test feasibility.
                    const auto feas = p.feasibility_f(fitness);

                    if (!(f_count / verb % 50u)) {
                        // Every 50 lines print the column names.
                        print("\n", std::setw(10), "fevals:", std::setw(15), "fitness:", std::setw(15), "violated:",
                              std::setw(15), "viol. norm:", '\n');
                    }
                    // Print to screen the log line.
                    print(std::setw(10), f_count, std::setw(15), fitness[0], std::setw(15), nv, std::setw(15), l,
                          feas ? "" : " i", '\n');
                    // Record the log.
                    log.emplace_back(f_count, fitness[0], nv, l, feas);
                }

                // Update the counter.
                ++f_count;

                // Return the objfun value.
                return fitness[0];
            },
            static_cast<void *>(this));
        if (res != NLOPT_SUCCESS) {
            pagmo_throw(std::invalid_argument, "could not set the objective function for the NLopt algorithm '"
                                                   + data::names.right.at(algo) + "', the error is: "
                                                   + nlopt_res2string(res));
        }

        // Vector-valued constraints.
        const auto nic = boost::numeric_cast<unsigned>(prob.get_nic());
        const auto nec = boost::numeric_cast<unsigned>(prob.get_nec());
        const auto c_tol = prob.get_c_tol();

        // Inequality.
        if (nic) {
            res = ::nlopt_add_inequality_mconstraint(
                m_value.get(), nic,
                [](unsigned m, double *result, unsigned dim, const double *x, double *grad, void *f_data) {
                    // Get *this back from the function data.
                    auto &nlo = *static_cast<nlopt_obj *>(f_data);

                    // A few shortcuts.
                    auto &p = nlo.m_prob;
                    auto &dv = nlo.m_dv;

                    // A couple of sanity checks.
                    assert(dim == p.get_nx());
                    assert(dv.size() == dim);
                    assert(m == p.get_nic());

                    if (grad && !p.has_gradient()) {
                        // If grad is not null, it means we are in an algorithm
                        // that needs the gradient. If the problem does not support it,
                        // we error out.
                        pagmo_throw(
                            std::invalid_argument,
                            "during an optimization with the NLopt algorithm '"
                                + data::names.right.at(::nlopt_get_algorithm(nlo.m_value.get()))
                                + "' an inequality constraints gradient was requested, but the optimisation problem '"
                                + p.get_name() + "' does not provide it");
                    }

                    // Copy the decision vector in our temporary dv vector_double,
                    // for use in the pagmo API.
                    std::copy(x, x + dim, dv.begin());

                    // Compute fitness and write IC to the output.
                    // NOTE: fitness is nobj + nec + nic.
                    const auto fitness = p.fitness(dv);
                    std::copy(fitness.data() + 1 + p.get_nec(), fitness.data() + 1 + p.get_nec() + m, result);

                    if (grad) {
                        // Handle gradient, if requested.
                        const auto gradient = p.gradient(dv);

                        if (p.has_gradient_sparsity()) {
                            // Sparse gradient.
                            auto &sp = nlo.m_sp;
                            // NOTE: problem::gradient() has already checked that
                            // the returned vector has size m_gs_dim, i.e., the stored
                            // size of the sparsity pattern. On the other hand,
                            // problem::gradient_sparsity() also checks that the returned
                            // vector has size m_gs_dim, so these two must have the same size.
                            assert(gradient.size() == sp.size());

                            // Let's first fill it with zeroes.
                            std::fill(grad, grad + p.get_nx() * p.get_nic(), 0.);

                            // Now we need to go into the sparsity pattern and find where
                            // the sparsity data for the constraints start.
                            using pair_t = sparsity_pattern::value_type;
                            auto it_sp = std::lower_bound(sp.begin(), sp.end(), pair_t(p.get_nec() + 1u, 0u));
                            if (it_sp == sp.end()) {
                                // This means that the sparsity data for ineq constraints is empty. Just return.
                                return;
                            }

                            // Need to do a bit of horrid overflow checking :/.
                            using diff_type = std::iterator_traits<decltype(it_sp)>::difference_type;
                            using udiff_type = std::make_unsigned<diff_type>::type;
                            if (sp.size() > static_cast<udiff_type>(std::numeric_limits<diff_type>::max())) {
                                pagmo_throw(std::overflow_error,
                                            "Overflow error, the sparsity pattern size is too large.");
                            }
                            // This is the index at which the ineq constraints start.
                            const auto idx = std::distance(sp.begin(), it_sp);
                            // Grab the start of the gradient data for the ineq constraints.
                            auto g_it = gradient.data() + idx;

                            // Then we iterate over the sparsity pattern, and fill in the
                            // nonzero bits in grad. Run until sp.end() as the IC are at the
                            // end of the sparsity/gradient vector.
                            for (; it_sp != sp.end(); ++it_sp, ++g_it) {
                                grad[it_sp->second] = *g_it;
                            }
                        } else {
                            // Dense gradient.
                            std::copy(gradient.data() + p.get_nx() * (1u + p.get_nec()),
                                      gradient.data() + gradient.size(), grad);
                        }
                    }
                },
                static_cast<void *>(this), c_tol.data() + nec);
            if (res != NLOPT_SUCCESS) {
                pagmo_throw(std::invalid_argument,
                            "could not set the inequality constraints for the NLopt algorithm '"
                                + data::names.right.at(algo) + "', the error is: " + nlopt_res2string(res)
                                + "\nThis usually means that the algorithm does not support inequality constraints");
            }
        }

        // Equality.
        if (nec) {
            res = ::nlopt_add_equality_mconstraint(
                m_value.get(), nec,
                [](unsigned m, double *result, unsigned dim, const double *x, double *grad, void *f_data) {
                    // Get *this back from the function data.
                    auto &nlo = *static_cast<nlopt_obj *>(f_data);

                    // A few shortcuts.
                    auto &p = nlo.m_prob;
                    auto &dv = nlo.m_dv;

                    // A couple of sanity checks.
                    assert(dim == p.get_nx());
                    assert(dv.size() == dim);
                    assert(m == p.get_nec());

                    if (grad && !p.has_gradient()) {
                        // If grad is not null, it means we are in an algorithm
                        // that needs the gradient. If the problem does not support it,
                        // we error out.
                        pagmo_throw(
                            std::invalid_argument,
                            "during an optimization with the NLopt algorithm '"
                                + data::names.right.at(::nlopt_get_algorithm(nlo.m_value.get()))
                                + "' an equality constraints gradient was requested, but the optimisation problem '"
                                + p.get_name() + "' does not provide it");
                    }

                    // Copy the decision vector in our temporary dv vector_double,
                    // for use in the pagmo API.
                    std::copy(x, x + dim, dv.begin());

                    // Compute fitness and write EC to the output.
                    // NOTE: fitness is nobj + nec + nic.
                    const auto fitness = p.fitness(dv);
                    std::copy(fitness.data() + 1, fitness.data() + 1 + p.get_nec(), result);

                    if (grad) {
                        // Handle gradient, if requested.
                        const auto gradient = p.gradient(dv);

                        if (p.has_gradient_sparsity()) {
                            // Sparse gradient case.
                            auto &sp = nlo.m_sp;
                            // NOTE: problem::gradient() has already checked that
                            // the returned vector has size m_gs_dim, i.e., the stored
                            // size of the sparsity pattern. On the other hand,
                            // problem::gradient_sparsity() also checks that the returned
                            // vector has size m_gs_dim, so these two must have the same size.
                            assert(gradient.size() == sp.size());

                            // Let's first fill it with zeroes.
                            std::fill(grad, grad + p.get_nx() * p.get_nec(), 0.);

                            // Now we need to go into the sparsity pattern and find where
                            // the sparsity data for the constraints start.
                            using pair_t = sparsity_pattern::value_type;
                            auto it_sp = std::lower_bound(sp.begin(), sp.end(), pair_t(1u, 0u));
                            if (it_sp == sp.end() || it_sp->first >= p.get_nec() + 1u) {
                                // This means that there sparsity data for eq constraints is empty: either we went
                                // at the end of sp, or the first index pair found refers to inequality constraints.
                                // Just
                                // return.
                                return;
                            }

                            // Need to do a bit of horrid overflow checking :/.
                            using diff_type = std::iterator_traits<decltype(it_sp)>::difference_type;
                            using udiff_type = std::make_unsigned<diff_type>::type;
                            if (sp.size() > static_cast<udiff_type>(std::numeric_limits<diff_type>::max())) {
                                pagmo_throw(std::overflow_error,
                                            "Overflow error, the sparsity pattern size is too large.");
                            }
                            // This is the index at which the eq constraints start.
                            const auto idx = std::distance(sp.begin(), it_sp);
                            // Grab the start of the gradient data for the eq constraints.
                            auto g_it = gradient.data() + idx;

                            // Then we iterate over the sparsity pattern, and fill in the
                            // nonzero bits in grad. We terminate either at the end of sp, or when
                            // we encounter the first inequality constraint.
                            for (; it_sp != sp.end() && it_sp->first < p.get_nec() + 1u; ++it_sp, ++g_it) {
                                grad[it_sp->second] = *g_it;
                            }
                        } else {
                            // Dense gradient.
                            std::copy(gradient.data() + p.get_nx(), gradient.data() + p.get_nx() * (1u + p.get_nec()),
                                      grad);
                        }
                    }
                },
                static_cast<void *>(this), c_tol.data());
            if (res != NLOPT_SUCCESS) {
                pagmo_throw(std::invalid_argument,
                            "could not set the equality constraints for the NLopt algorithm '"
                                + data::names.right.at(algo) + "', the error is: " + nlopt_res2string(res)
                                + "\nThis usually means that the algorithm does not support equality constraints");
            }
        }

        // Handle the various stopping criteria.
        res = ::nlopt_set_stopval(m_value.get(), stopval);
        if (res != NLOPT_SUCCESS) {
            pagmo_throw(std::invalid_argument, "could not set the 'stopval' stopping criterion to "
                                                   + std::to_string(stopval) + " for the NLopt algorithm '"
                                                   + data::names.right.at(algo) + "', the error is: "
                                                   + nlopt_res2string(res));
        }
        res = ::nlopt_set_ftol_rel(m_value.get(), ftol_rel);
        if (res != NLOPT_SUCCESS) {
            pagmo_throw(std::invalid_argument, "could not set the 'ftol_rel' stopping criterion to "
                                                   + std::to_string(ftol_rel) + " for the NLopt algorithm '"
                                                   + data::names.right.at(algo) + "', the error is: "
                                                   + nlopt_res2string(res));
        }
        res = ::nlopt_set_ftol_abs(m_value.get(), ftol_abs);
        if (res != NLOPT_SUCCESS) {
            pagmo_throw(std::invalid_argument, "could not set the 'ftol_abs' stopping criterion to "
                                                   + std::to_string(ftol_abs) + " for the NLopt algorithm '"
                                                   + data::names.right.at(algo) + "', the error is: "
                                                   + nlopt_res2string(res));
        }
        res = ::nlopt_set_xtol_rel(m_value.get(), xtol_rel);
        if (res != NLOPT_SUCCESS) {
            pagmo_throw(std::invalid_argument, "could not set the 'xtol_rel' stopping criterion to "
                                                   + std::to_string(xtol_rel) + " for the NLopt algorithm '"
                                                   + data::names.right.at(algo) + "', the error is: "
                                                   + nlopt_res2string(res));
        }
        res = ::nlopt_set_xtol_abs1(m_value.get(), xtol_abs);
        if (res != NLOPT_SUCCESS) {
            pagmo_throw(std::invalid_argument, "could not set the 'xtol_abs' stopping criterion to "
                                                   + std::to_string(xtol_abs) + " for the NLopt algorithm '"
                                                   + data::names.right.at(algo) + "', the error is: "
                                                   + nlopt_res2string(res));
        }
        res = ::nlopt_set_maxeval(m_value.get(), maxeval);
        if (res != NLOPT_SUCCESS) {
            pagmo_throw(std::invalid_argument, "could not set the 'maxeval' stopping criterion to "
                                                   + std::to_string(maxeval) + " for the NLopt algorithm '"
                                                   + data::names.right.at(algo) + "', the error is: "
                                                   + nlopt_res2string(res));
        }
        res = ::nlopt_set_maxtime(m_value.get(), maxtime);
        if (res != NLOPT_SUCCESS) {
            pagmo_throw(std::invalid_argument, "could not set the 'maxtime' stopping criterion to "
                                                   + std::to_string(maxtime) + " for the NLopt algorithm '"
                                                   + data::names.right.at(algo) + "', the error is: "
                                                   + nlopt_res2string(res));
        }
    }

    // Delete all other ctors/assignment ops.
    nlopt_obj(const nlopt_obj &) = delete;
    nlopt_obj(nlopt_obj &&) = delete;
    nlopt_obj &operator=(const nlopt_obj &) = delete;
    nlopt_obj &operator=(nlopt_obj &&) = delete;

    // Data members.
    problem &m_prob;
    sparsity_pattern m_sp;
    std::unique_ptr<std::remove_pointer<::nlopt_opt>::type, void (*)(::nlopt_opt)> m_value;
    vector_double m_dv;
    unsigned m_verbosity;
    unsigned long m_objfun_counter = 0;
    log_type m_log;
};
}

/// NLopt algorithms.
/**
 * \image html nlopt.png "NLopt logo." width=3cm
 *
 * This user-defined algorithm wraps a selection of solvers from the <a
 * href="http://ab-initio.mit.edu/wiki/index.php/NLopt">NLopt</a> library, focusing on
 * local optimisation (both gradient-based and derivative-free). The complete list of supported
 * NLopt algorithms is:
 * - COBYLA,
 * - BOBYQA,
 * - NEWUOA + bound constraints,
 * - PRAXIS,
 * - Nelder-Mead simplex,
 * - sbplx,
 * - MMA (Method of Moving Asymptotes),
 * - CCSA,
 * - SLSQP,
 * - low-storage BFGS,
 * - preconditioned truncated Newton,
 * - shifted limited-memory variable-metric.
 *
 * The desired NLopt solver is selected upon construction of a pagmo::nlopt algorithm. Various properties
 * of the solver (e.g., the stopping criteria) can be configured after construction via methods provided
 * by this class.
 *
 * All NLopt solvers support only single-objective optimisation, and, as usual in pagmo, minimisation
 * is always assumed. The gradient-based algorithms require the optimisation problem to provide a gradient.
 * Some solvers support equality and/or inequality constaints.
 *
 * In order to support pagmo's population-based optimisation model, nlopt::evolve() will select
 * a single individual from the input pagmo::population to be optimised by the NLopt solver.
 * The optimised individual will then be inserted back into the population at the end of the optimisation.
 * The selection and replacement strategies can be configured via set_selection(const std::string &),
 * set_selection(population::size_type), set_replacement(const std::string &) and
 * set_replacement(population::size_type).
 *
 * \verbatim embed:rst:leading-asterisk
 * .. note::
 *
 *    This user-defined algorithm is available only if pagmo was compiled with the ``PAGMO_WITH_NLOPT`` option
 *    enabled (see the :ref:`installation instructions <install>`).
 *
 * .. seealso::
 *
 *    The `NLopt website <http://ab-initio.mit.edu/wiki/index.php/NLopt_Algorithms>`_ contains a detailed description
 *    of each supported solver.
 *
 * \endverbatim
 */
// TODO:
// - investiagate the use of a fitness cache, after we have good perf testing in place.
class nlopt
{
    using nlopt_obj = detail::nlopt_obj;
    using nlopt_data = detail::nlopt_data<>;

public:
    /// Single data line for the algorithm's log.
    /**
     * A log data line is a tuple consisting of:
     * - the number of objective function evaluations made so far,
     * - the objective function value for the current decision vector,
     * - the number of constraints violated by the current decision vector,
     * - the constraints violation norm for the current decision vector,
     * - a boolean flag signalling the feasibility of the current decision vector.
     */
    using log_line_type = std::tuple<unsigned long, double, vector_double::size_type, double, bool>;
    /// Log type.
    /**
     * The algorithm log is a collection of nlopt::log_line_type data lines, stored in chronological order
     * during the optimisation if the verbosity of the algorithm is set to a nonzero value
     * (see nlopt::set_verbosity()).
     */
    using log_type = std::vector<log_line_type>;

private:
    static_assert(std::is_same<log_line_type, detail::nlopt_obj::log_line_type>::value, "Invalid log line type.");

public:
    /// Default constructor.
    /**
     * The default constructor initialises the pagmo::nlopt algorithm with the ``cobyla`` solver,
     * the ``"best"`` individual selection strategy and the ``"worst"`` individual replacement strategy.
     *
     * @throws unspecified any exception thrown by pagmo::nlopt(const std::string &).
     */
    nlopt() : nlopt("cobyla")
    {
    }
    /// Constructor from solver name.
    /**
     * This constructor will initialise a pagmo::nlopt object which will use the NLopt algorithm specified by
     * the input string \p algo, the ``"best"`` individual selection strategy and the ``"worst"`` individual
     * replacement strategy. \p algo is translated to an NLopt algorithm type according to the following
     * translation table:
     * \verbatim embed:rst:leading-asterisk
     *  ================================  ====================================
     *  ``algo`` string                   NLopt algorithm
     *  ================================  ====================================
     *  ``"cobyla"``                      ``NLOPT_LN_COBYLA``
     *  ``"bobyqa"``                      ``NLOPT_LN_BOBYQA``
     *  ``"newuoa"``                      ``NLOPT_LN_NEWUOA``
     *  ``"newuoa_bound"``                ``NLOPT_LN_NEWUOA_BOUND``
     *  ``"praxis"``                      ``NLOPT_LN_PRAXIS``
     *  ``"neldermead"``                  ``NLOPT_LN_NELDERMEAD``
     *  ``"sbplx"``                       ``NLOPT_LN_SBPLX``
     *  ``"mma"``                         ``NLOPT_LD_MMA``
     *  ``"ccsaq"``                       ``NLOPT_LD_CCSAQ``
     *  ``"slsqp"``                       ``NLOPT_LD_SLSQP``
     *  ``"lbfgs"``                       ``NLOPT_LD_LBFGS``
     *  ``"tnewton_precond_restart"``     ``NLOPT_LD_TNEWTON_PRECOND_RESTART``
     *  ``"tnewton_precond"``             ``NLOPT_LD_TNEWTON_PRECOND``
     *  ``"tnewton_restart"``             ``NLOPT_LD_TNEWTON_RESTART``
     *  ``"tnewton"``                     ``NLOPT_LD_TNEWTON``
     *  ``"var2"``                        ``NLOPT_LD_VAR2``
     *  ``"var1"``                        ``NLOPT_LD_VAR1``
     *  ================================  ====================================
     * \endverbatim
     * The parameters of the selected algorithm can be specified via the methods of this class.
     *
     * \verbatim embed:rst:leading-asterisk
     * .. seealso::
     *
     *    The `NLopt website <http://ab-initio.mit.edu/wiki/index.php/NLopt_Algorithms>`_ contains a detailed
     *    description of each supported solver.
     *
     * \endverbatim
     *
     * @param algo the name of the NLopt algorithm that will be used by this pagmo::nlopt object.
     *
     * @throws std::runtime_error if the NLopt version is not at least 2.
     * @throws std::invalid_argument if \p algo is not one of the allowed algorithm names.
     */
    explicit nlopt(const std::string &algo)
        : m_algo(algo), m_select(std::string("best")), m_replace(std::string("worst")),
          m_rselect_seed(random_device::next()), m_e(static_cast<std::mt19937::result_type>(m_rselect_seed))
    {
        // Check version.
        int major, minor, bugfix;
        ::nlopt_version(&major, &minor, &bugfix);
        if (major < 2) {
            pagmo_throw(std::runtime_error, "Only NLopt version >= 2 is supported"); // LCOV_EXCL_LINE
        }

        // Check the algorithm.
        if (nlopt_data::names.left.find(m_algo) == nlopt_data::names.left.end()) {
            // The selected algorithm is unknown or not among the supported ones.
            std::ostringstream oss;
            std::transform(nlopt_data::names.left.begin(), nlopt_data::names.left.end(),
                           std::ostream_iterator<std::string>(oss, "\n"),
                           [](const uncvref_t<decltype(*nlopt_data::names.left.begin())> &v) { return v.first; });
            pagmo_throw(std::invalid_argument, "unknown/unsupported NLopt algorithm '" + algo
                                                   + "'. The supported algorithms are:\n" + oss.str());
        }
    }
    /// Set the seed for the ``"random"`` selection/replacement policies.
    /**
     * @param seed the value that will be used to seed the random number generator used by the ``"random"``
     * selection/replacement policies.
     */
    void set_random_sr_seed(unsigned seed)
    {
        m_rselect_seed = seed;
        m_e.seed(static_cast<std::mt19937::result_type>(m_rselect_seed));
    }
    /// Set the individual selection policy.
    /**
     * This method will set the policy that is used in evolve() to select the individual
     * that will be optimised.
     *
     * The input string must be one of ``"best"``, ``"worst"`` and ``"random"``:
     * - ``"best"`` will select the best individual in the population,
     * - ``"worst"`` will select the worst individual in the population,
     * - ``"random"`` will randomly choose one individual in the population.
     *
     * set_random_sr_seed() can be used to seed the random number generator used by the ``"random"`` policy.
     *
     * Instead of a selection policy, a specific individual in the population can be selected via
     * set_selection(population::size_type).
     *
     * @param select the selection policy.
     *
     * @throws std::invalid_argument if \p select is not one of ``"best"``, ``"worst"`` or ``"random"``.
     */
    void set_selection(const std::string &select)
    {
        if (select != "best" && select != "worst" && select != "random") {
            pagmo_throw(std::invalid_argument,
                        "the individual selection policy must be one of ['best', 'worst', 'random'], but '" + select
                            + "' was provided instead");
        }
        m_select = select;
    }
    /// Set the individual selection index.
    /**
     * This method will set the index of the individual that is selected for optimisation
     * in evolve().
     *
     * @param n the index in the population of the individual to be selected for optimisation.
     */
    void set_selection(population::size_type n)
    {
        m_select = n;
    }
    /// Get the individual selection policy or index.
    /**
     * This method will return a \p boost::any containing either the individual selection policy (as an \p std::string)
     * or the individual selection index (as a population::size_type). The selection policy or index is set via
     * set_selection(const std::string &) and set_selection(population::size_type).
     *
     * @return the individual selection policy or index.
     */
    boost::any get_selection() const
    {
        return m_select;
    }
    /// Set the individual replacement policy.
    /**
     * This method will set the policy that is used in evolve() to select the individual
     * that will be replaced by the optimised individual.
     *
     * The input string must be one of ``"best"``, ``"worst"`` and ``"random"``:
     * - ``"best"`` will select the best individual in the population,
     * - ``"worst"`` will select the worst individual in the population,
     * - ``"random"`` will randomly choose one individual in the population.
     *
     * set_random_sr_seed() can be used to seed the random number generator used by the ``"random"`` policy.
     *
     * Instead of a replacement policy, a specific individual in the population can be selected via
     * set_replacement(population::size_type).
     *
     * @param replace the replacement policy.
     *
     * @throws std::invalid_argument if \p replace is not one of ``"best"``, ``"worst"`` or ``"random"``.
     */
    void set_replacement(const std::string &replace)
    {
        if (replace != "best" && replace != "worst" && replace != "random") {
            pagmo_throw(std::invalid_argument,
                        "the individual replacement policy must be one of ['best', 'worst', 'random'], but '" + replace
                            + "' was provided instead");
        }
        m_replace = replace;
    }
    /// Set the individual replacement index.
    /**
     * This method will set the index of the individual that is replaced after the optimisation
     * in evolve().
     *
     * @param n the index in the population of the individual to be replaced after the optimisation.
     */
    void set_replacement(population::size_type n)
    {
        m_replace = n;
    }
    /// Get the individual replacement policy or index.
    /**
     * This method will return a \p boost::any containing either the individual replacement policy (as an \p
     * std::string) or the individual replacement index (as a population::size_type). The replacement policy or index is
     * set via set_replacement(const std::string &) and set_replacement(population::size_type).
     *
     * @return the individual replacement policy or index.
     */
    boost::any get_replacement() const
    {
        return m_replace;
    }
    /// Evolve population.
    /**
     * This method will select an individual from \p pop, optimise it with the NLopt algorithm specified upon
     * construction, replace an individual in \p pop with the optimised individual, and finally return \p pop.
     * The individual selection and replacement criteria can be set via set_selection(const std::string &),
     * set_selection(population::size_type), set_replacement(const std::string &) and
     * set_replacement(population::size_type).
     *
     * @param pop the population to be optimised.
     *
     * @return the optimised population.
     *
     * @throws std::invalid_argument in the following cases:
     * - the population's problem is multi-objective,
     * - the setup of the NLopt algorithm fails (e.g., if the problem is constrained but the selected
     *   NLopt solver does not support constrained optimisation),
     * - the selected NLopt solver needs gradients but they are not provided by the population's
     *   problem,
     * - the components of the individual selected for optimisation contain NaNs or they are outside
     *   the problem's bounds,
     * - the individual selection/replacement index if not smaller
     *   than the population's size
     * @throws unspecified any exception thrown by the public interface of pagmo::problem.
     */
    population evolve(population pop) const
    {
        if (!pop.size()) {
            // In case of an empty pop, just return it.
            return pop;
        }

        auto &prob = pop.get_problem();
        const auto nc = prob.get_nc();

        // Create the nlopt obj.
        // NOTE: this will check also the problem's properties.
        nlopt_obj no(nlopt_data::names.left.at(m_algo), prob, m_sc_stopval, m_sc_ftol_rel, m_sc_ftol_abs, m_sc_xtol_rel,
                     m_sc_xtol_abs, m_sc_maxeval, m_sc_maxtime, m_verbosity);

        // Setup of the initial guess.
        vector_double initial_guess;
        if (boost::any_cast<std::string>(&m_select)) {
            const auto &s_select = boost::any_cast<const std::string &>(m_select);
            if (s_select == "best") {
                initial_guess = pop.get_x()[pop.best_idx()];
            } else if (s_select == "worst") {
                initial_guess = pop.get_x()[pop.worst_idx()];
            } else {
                assert(s_select == "random");
                std::uniform_int_distribution<population::size_type> dist(0, pop.size() - 1u);
                initial_guess = pop.get_x()[dist(m_e)];
            }
        } else {
            const auto idx = boost::any_cast<population::size_type>(m_select);
            if (idx >= pop.size()) {
                pagmo_throw(std::invalid_argument, "cannot select the individual at index " + std::to_string(idx)
                                                       + " for evolution: the population has a size of only "
                                                       + std::to_string(pop.size()));
            }
            initial_guess = pop.get_x()[idx];
        }
        // Check the initial guess.
        // NOTE: this should be guaranteed by the population's invariants.
        assert(initial_guess.size() == prob.get_nx());
        const auto bounds = prob.get_bounds();
        for (decltype(bounds.first.size()) i = 0; i < bounds.first.size(); ++i) {
            if (std::isnan(initial_guess[i])) {
                pagmo_throw(std::invalid_argument,
                            "the value of the initial guess at index " + std::to_string(i) + " is NaN");
            }
            if (initial_guess[i] < bounds.first[i] || initial_guess[i] > bounds.second[i]) {
                pagmo_throw(std::invalid_argument, "the value of the initial guess at index " + std::to_string(i)
                                                       + " is outside the problem's bounds");
            }
        }

        // Run the optimisation and store the status returned by NLopt.
        double fitness;
        m_last_opt_result = ::nlopt_optimize(no.m_value.get(), initial_guess.data(), &fitness);
        if (m_verbosity) {
            // Print to screen the result of the optimisation, if we are being verbose.
            std::cout << "\nOptimisation return status: " << detail::nlopt_res2string(m_last_opt_result) << '\n';
        }

        // Replace the log.
        m_log = std::move(no.m_log);

        // Store the new individual into the population.
        if (boost::any_cast<std::string>(&m_replace)) {
            const auto &s_replace = boost::any_cast<const std::string &>(m_replace);
            if (s_replace == "best") {
                if (nc) {
                    pop.set_x(pop.best_idx(), initial_guess);
                } else {
                    pop.set_xf(pop.best_idx(), initial_guess, {fitness});
                }
            } else if (s_replace == "worst") {
                if (nc) {
                    pop.set_x(pop.worst_idx(), initial_guess);
                } else {
                    pop.set_xf(pop.worst_idx(), initial_guess, {fitness});
                }
            } else {
                assert(s_replace == "random");
                std::uniform_int_distribution<population::size_type> dist(0, pop.size() - 1u);
                if (nc) {
                    pop.set_x(dist(m_e), initial_guess);
                } else {
                    pop.set_xf(dist(m_e), initial_guess, {fitness});
                }
            }
        } else {
            const auto idx = boost::any_cast<population::size_type>(m_replace);
            if (idx >= pop.size()) {
                pagmo_throw(std::out_of_range, "cannot replace the individual at index " + std::to_string(idx)
                                                   + " after evolution: the population has a size of only "
                                                   + std::to_string(pop.size()));
            }
            if (nc) {
                pop.set_x(idx, initial_guess);
            } else {
                pop.set_xf(idx, initial_guess, {fitness});
            }
        }

        // Return the evolved pop.
        return pop;
    }
    /// Algorithm's name.
    /**
     * @return a human-readable name for the algorithm.
     */
    std::string get_name() const
    {
        return "NLopt - " + m_algo;
    }
    /// Set verbosity.
    /**
     * This method will set the algorithm's verbosity. If \p n is zero, no output is produced during the optimisation
     * and no logging is performed. If \p n is nonzero, then every \p n objective function evaluations the status
     * of the optimisation will be both printed to screen and recorded internally. See nlopt::log_line_type and
     * nlopt::log_type for information on the logging format.
     *
     * Example (verbosity 1):
     * @code{.unparsed}
     * fevals:       fitness:      violated:    viol. norm:
     *       0        68.6966              1       0.252343 i
     *       1        29.3926              1        15.1127 i
     *       2        54.2992              1        2.05694 i
     *       3        54.2992              1        2.05694 i
     *       4        15.4544              2        9.56984 i
     *       5        16.6126              2        1.80223 i
     *       6        16.8454              2       0.414897 i
     *       7        16.9794              2      0.0818469 i
     *       8        17.0132              2     0.00243968 i
     *       9         17.014              2    2.58628e-05 i
     *      10         17.014              0              0
     *      11         17.014              0              0
     *      12         17.014              0              0
     *      13         17.014              0              0
     *      14         17.014              0              0
     *      15         17.014              0              0
     *      16         17.014              0              0
     *      17         17.014              0              0
     *      18         17.014              0              0
     *      19         17.014              0              0
     * @endcode
     * The little ``i`` at the end of some rows indicates that the decision vector
     * is infeasible.
     *
     * By default, the verbosity level is zero.
     *
     * @param n the desired verbosity level.
     */
    void set_verbosity(unsigned n)
    {
        m_verbosity = n;
    }
    /// Get extra information about the algorithm.
    /**
     * @return a human-readable string containing useful information about the algorithm's properties
     * (e.g., the stopping criteria, the selection/replacement policies, etc.).
     */
    std::string get_extra_info() const
    {
        int major, minor, bugfix;
        ::nlopt_version(&major, &minor, &bugfix);
        return "\tNLopt version: " + std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(bugfix)
               + "\n\tLast optimisation return code: " + detail::nlopt_res2string(m_last_opt_result) + "\n\tVerbosity: "
               + std::to_string(m_verbosity) + "\n\tIndividual selection "
               + (boost::any_cast<population::size_type>(&m_select)
                      ? "idx: " + std::to_string(boost::any_cast<population::size_type>(m_select))
                      : "policy: " + boost::any_cast<std::string>(m_select))
               + "\n\tIndividual replacement "
               + (boost::any_cast<population::size_type>(&m_replace)
                      ? "idx: " + std::to_string(boost::any_cast<population::size_type>(m_replace))
                      : "policy: " + boost::any_cast<std::string>(m_replace))
               + "\n\tStopping criteria:\n\t\tstopval:  "
               + (m_sc_stopval == -HUGE_VAL ? "disabled" : detail::to_string(m_sc_stopval)) + "\n\t\tftol_rel: "
               + (m_sc_ftol_rel <= 0. ? "disabled" : detail::to_string(m_sc_ftol_rel)) + "\n\t\tftol_abs: "
               + (m_sc_ftol_abs <= 0. ? "disabled" : detail::to_string(m_sc_ftol_abs)) + "\n\t\txtol_rel: "
               + (m_sc_xtol_rel <= 0. ? "disabled" : detail::to_string(m_sc_xtol_rel)) + "\n\t\txtol_abs: "
               + (m_sc_xtol_abs <= 0. ? "disabled" : detail::to_string(m_sc_xtol_abs)) + "\n\t\tmaxeval:  "
               + (m_sc_maxeval <= 0. ? "disabled" : detail::to_string(m_sc_maxeval)) + "\n\t\tmaxtime:  "
               + (m_sc_maxtime <= 0. ? "disabled" : detail::to_string(m_sc_maxtime)) + "\n";
    }

private:
    std::string m_algo;
    boost::any m_select;
    boost::any m_replace;
    unsigned m_rselect_seed;
    mutable detail::random_engine_type m_e;
    mutable ::nlopt_result m_last_opt_result = NLOPT_SUCCESS;
    // Stopping criteria.
    double m_sc_stopval = -HUGE_VAL;
    double m_sc_ftol_rel = 0.;
    double m_sc_ftol_abs = 0.;
    double m_sc_xtol_rel = 1E-8;
    double m_sc_xtol_abs = 0.;
    int m_sc_maxeval = 0;
    int m_sc_maxtime = 0;
    // Verbosity/log.
    unsigned m_verbosity = 0;
    mutable log_type m_log;
};
}

#if defined(_MSC_VER)

#pragma warning(pop)

#endif

#else // PAGMO_WITH_NLOPT

#error The nlopt.hpp header was included, but pagmo was not compiled with NLopt support

#endif // PAGMO_WITH_NLOPT

#endif
