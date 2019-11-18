// Copyright 2016-2019 Francesco Biscani (bluescarni@gmail.com)
//
// This file is part of the mp++ library.
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef MPPP_REAL_HPP
#define MPPP_REAL_HPP

#include <mp++/config.hpp>

#if defined(MPPP_WITH_MPFR)

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(MPPP_HAVE_STRING_VIEW)
#include <string_view>
#endif

#include <mp++/concepts.hpp>
#include <mp++/detail/fwd_decl.hpp>
#include <mp++/detail/gmp.hpp>
#include <mp++/detail/mpfr.hpp>
#include <mp++/detail/type_traits.hpp>
#include <mp++/detail/utils.hpp>
#include <mp++/detail/visibility.hpp>
#include <mp++/integer.hpp>
#include <mp++/rational.hpp>
#include <mp++/type_name.hpp>

#if defined(MPPP_WITH_QUADMATH)
#include <mp++/real128.hpp>
#endif

namespace mppp
{

namespace detail
{

// Clamp the precision between the min and max allowed values. This is used in the generic constructor/assignment
// operator.
constexpr ::mpfr_prec_t clamp_mpfr_prec(::mpfr_prec_t p)
{
    return real_prec_check(p) ? p : (p < real_prec_min() ? real_prec_min() : real_prec_max());
}

// Helper function to print an mpfr to stream in a given base.
MPPP_DLL_PUBLIC void mpfr_to_stream(const ::mpfr_t, std::ostream &, int);

#if !defined(MPPP_DOXYGEN_INVOKED)

// Helpers to deduce the precision when constructing/assigning a real via another type.
template <typename T, enable_if_t<is_integral<T>::value, int> = 0>
inline ::mpfr_prec_t real_deduce_precision(const T &)
{
    static_assert(nl_digits<T>() < nl_max<::mpfr_prec_t>(), "Overflow error.");
    // NOTE: for signed integers, include the sign bit as well.
    return static_cast<::mpfr_prec_t>(nl_digits<T>()) + is_signed<T>::value;
}

// Utility function to determine the number of base-2 digits of the significand
// of a floating-point type which is not in base-2.
template <typename T>
inline ::mpfr_prec_t dig2mpfr_prec()
{
    static_assert(std::is_floating_point<T>::value, "Invalid type.");
    // NOTE: just do a raw cast for the time being, it's not like we have ways of testing
    // this in any case. In the future we could consider switching to a compile-time implementation
    // of the integral log2, and do everything as compile-time integral computations.
    return static_cast<::mpfr_prec_t>(std::ceil(nl_digits<T>() * std::log2(std::numeric_limits<T>::radix)));
}

template <typename T, enable_if_t<std::is_floating_point<T>::value, int> = 0>
inline ::mpfr_prec_t real_deduce_precision(const T &)
{
    static_assert(nl_digits<T>() <= nl_max<::mpfr_prec_t>(), "Overflow error.");
    return std::numeric_limits<T>::radix == 2 ? static_cast<::mpfr_prec_t>(nl_digits<T>()) : dig2mpfr_prec<T>();
}

template <std::size_t SSize>
inline ::mpfr_prec_t real_deduce_precision(const integer<SSize> &n)
{
    // Infer the precision from the bit size of n.
    const auto ls = n.size();
    // Check that ls * GMP_NUMB_BITS is representable by mpfr_prec_t.
    // LCOV_EXCL_START
    if (mppp_unlikely(ls > make_unsigned(nl_max<::mpfr_prec_t>()) / unsigned(GMP_NUMB_BITS))) {
        throw std::overflow_error("The deduced precision for a real from an integer is too large");
    }
    // LCOV_EXCL_STOP
    return static_cast<::mpfr_prec_t>(static_cast<::mpfr_prec_t>(ls) * GMP_NUMB_BITS);
}

template <std::size_t SSize>
inline ::mpfr_prec_t real_deduce_precision(const rational<SSize> &q)
{
    // Infer the precision from the bit size of num/den.
    const auto n_size = q.get_num().size();
    const auto d_size = q.get_den().size();
    // Overflow checks.
    // LCOV_EXCL_START
    if (mppp_unlikely(
            // Overflow in total size.
            (n_size > nl_max<decltype(q.get_num().size())>() - d_size)
            // Check that tot_size * GMP_NUMB_BITS is representable by mpfr_prec_t.
            || ((n_size + d_size) > make_unsigned(nl_max<::mpfr_prec_t>()) / unsigned(GMP_NUMB_BITS)))) {
        throw std::overflow_error("The deduced precision for a real from a rational is too large");
    }
    // LCOV_EXCL_STOP
    return static_cast<::mpfr_prec_t>(static_cast<::mpfr_prec_t>(n_size + d_size) * GMP_NUMB_BITS);
}

#if defined(MPPP_WITH_QUADMATH)

inline ::mpfr_prec_t real_deduce_precision(const real128 &)
{
    // The significand precision in bits is 113 for real128. Let's double-check it.
    static_assert(real128_sig_digits() == 113u, "Invalid number of digits.");
    return 113;
}

#endif

#endif

// Default precision value.
MPPP_DLL_PUBLIC extern std::atomic<::mpfr_prec_t> real_default_prec;

// Fwd declare for friendship.
template <bool, typename F, typename Arg0, typename... Args>
real &mpfr_nary_op_impl(::mpfr_prec_t, const F &, real &, Arg0 &&, Args &&...);

template <bool, typename F, typename Arg0, typename... Args>
real mpfr_nary_op_return_impl(::mpfr_prec_t, const F &, Arg0 &&, Args &&...);

template <typename F>
real real_constant(const F &, ::mpfr_prec_t);

// Wrapper for calling mpfr_lgamma().
MPPP_DLL_PUBLIC void real_lgamma_wrapper(::mpfr_t, const ::mpfr_t, ::mpfr_rnd_t);

// A small helper to check the input of the trunc() overloads.
MPPP_DLL_PUBLIC void real_check_trunc_arg(const real &);

} // namespace detail

// Fwd declare swap.
void swap(real &, real &) noexcept;

template <typename T>
using is_real_interoperable = detail::disjunction<is_cpp_interoperable<T>, detail::is_integer<T>, detail::is_rational<T>
#if defined(MPPP_WITH_QUADMATH)
                                                  ,
                                                  std::is_same<T, real128>
#endif
                                                  >;

template <typename T>
#if defined(MPPP_HAVE_CONCEPTS)
MPPP_CONCEPT_DECL RealInteroperable = is_real_interoperable<T>::value;

#else
using real_interoperable_enabler = detail::enable_if_t<is_real_interoperable<T>::value, int>;
#endif

#if defined(MPPP_HAVE_CONCEPTS)
template <typename T>
MPPP_CONCEPT_DECL CvrReal = std::is_same<detail::uncvref_t<T>, real>::value;
#else
template <typename... Args>
using cvr_real_enabler
    = detail::enable_if_t<detail::conjunction<std::is_same<detail::uncvref_t<Args>, real>...>::value, int>;
#endif

/// Get the default precision for \link mppp::real real\endlink objects.
/**
 * \ingroup real_prec
 * \rststar
 * This function returns the value of the precision used during the construction of
 * :cpp:class:`~mppp::real` objects when an explicit precision value
 * is **not** specified . On program startup, the value returned by this function
 * is zero, meaning that the precision of a :cpp:class:`~mppp::real` object will be chosen
 * automatically according to heuristics depending on the specific situation, if possible.
 *
 * The default precision is stored in a global variable, and its value can be changed via
 * :cpp:func:`~mppp::real_set_default_prec()`. It is safe to read and modify concurrently
 * from multiple threads the default precision.
 * \endrststar
 *
 * @return the value of the default precision for \link mppp::real real\endlink objects.
 */
inline mpfr_prec_t real_get_default_prec()
{
    return detail::real_default_prec.load(std::memory_order_relaxed);
}

/// Set the default precision for \link mppp::real real\endlink objects.
/**
 * \ingroup real_prec
 * \rststar
 * See :cpp:func:`~mppp::real_get_default_prec()` for an explanation of how the default precision value
 * is used.
 * \endrststar
 *
 * @param p the desired value for the default precision for \link mppp::real real\endlink objects.
 *
 * @throws std::invalid_argument if \p p is nonzero and not in the range established by
 * \link mppp::real_prec_min() real_prec_min()\endlink and \link mppp::real_prec_max() real_prec_max()\endlink.
 */
inline void real_set_default_prec(::mpfr_prec_t p)
{
    if (mppp_unlikely(p && !detail::real_prec_check(p))) {
        throw std::invalid_argument("Cannot set the default precision to " + detail::to_string(p)
                                    + ": the value must be either zero or between " + detail::to_string(real_prec_min())
                                    + " and " + detail::to_string(real_prec_max()));
    }
    detail::real_default_prec.store(p, std::memory_order_relaxed);
}

/// Reset the default precision for \link mppp::real real\endlink objects.
/**
 * \ingroup real_prec
 * \rststar
 * This function will reset the default precision value to zero (i.e., the same value assigned
 * on program startup). See :cpp:func:`~mppp::real_get_default_prec()` for an explanation of how the default precision
 * value is used.
 * \endrststar
 */
inline void real_reset_default_prec()
{
    detail::real_default_prec.store(0, std::memory_order_relaxed);
}

namespace detail
{

// Get the default precision, if set, otherwise the clamped deduced precision for x.
template <typename T>
inline ::mpfr_prec_t real_dd_prec(const T &x)
{
    // NOTE: this is guaranteed to be a valid precision value.
    const auto dp = real_get_default_prec();
    // Return default precision if nonzero, otherwise return the clamped deduced precision.
    return dp ? dp : clamp_mpfr_prec(real_deduce_precision(x));
}

} // namespace detail

// Doxygen gets confused by this.
#if !defined(MPPP_DOXYGEN_INVOKED)

// Special initialisation tags for real.
enum class real_kind : std::underlying_type<::mpfr_kind_t>::type {
    nan = MPFR_NAN_KIND,
    inf = MPFR_INF_KIND,
    zero = MPFR_ZERO_KIND
};

#endif

// For the future:
// - construction from/conversion to interoperables can probably be improved performance wise, especially
//   if we exploit the mpfr_t internals.
// - probably we should have a build in the CI against the latest MPFR, built with sanitizers on.
// - probably we should have MPFR as well in the 32bit coverage build.
// - investigate the applicability of a cache.
// - see if it's needed to provide alternate interoperable function/operators that do *not* promote
//   the non-real to real (there's a bunch of functions for direct interface with GMP and cpp types
//   in the MPFR API: arithmetic, comparison, etc.).

/// Multiprecision floating-point class.
/**
 * \rststar
 * This class represents arbitrary-precision real values encoded in a binary floating-point format.
 * It acts as a wrapper around the MPFR ``mpfr_t`` type, pairing a multiprecision significand
 * (whose size can be set at runtime) to a fixed-size exponent. In other words, :cpp:class:`~mppp::real`
 * values can have an arbitrary number of binary digits of precision (limited only by the available memory),
 * but the exponent range is limited.
 *
 * :cpp:class:`~mppp::real` aims to behave like a C++ floating-point type whose precision is a runtime property
 * of the class instances rather than a compile-time property of the type. Because of this, the way precision
 * is handled in :cpp:class:`~mppp::real` differs from the way it is managed in MPFR. The most important difference
 * is that in operations involving :cpp:class:`~mppp::real` the precision of the result is usually determined
 * by the precision of the operands, whereas in MPFR the precision of the operation is determined by the precision
 * of the return value (which is always passed as the first function parameter in the MPFR API). For instance,
 * in the following code,
 *
 * .. code-block:: c++
 *
 *    auto x = real{5,200} + real{6,150};
 *
 * the first operand has a value of 5 and precision of 200 bits, while the second operand has a value of 6 and precision
 * 150 bits. The precision of the result ``x`` (and the precision at which the addition is computed) will be
 * the maximum precision among the two operands, that is, :math:`\mathrm{max}\left(200,150\right)=200` bits.
 *
 * The precision of a :cpp:class:`~mppp::real` can be set at construction, or it can be changed later via functions and
 * methods such as :cpp:func:`mppp::real::set_prec()`, :cpp:func:`mppp::real::prec_round()`, etc. By default,
 * the precision of a :cpp:class:`~mppp::real` is automatically deduced upon construction following a set of heuristics
 * aimed at ensuring that the constructed :cpp:class:`~mppp::real` represents exactly the value used for initialisation.
 * For instance, by default, the construction of a :cpp:class:`~mppp::real` from a 32 bit integer will yield a
 * :cpp:class:`~mppp::real` with a precision of 32 bits. This behaviour can be altered either by specifying explicitly
 * the desired precision value, or by setting a global default precision via :cpp:func:`~mppp::real_set_default_prec()`.
 *
 * This class has the look and feel of a C++ builtin type: it can interact with all of C++'s integral and
 * floating-point primitive types, :cpp:class:`~mppp::integer`, :cpp:class:`~mppp::rational` and
 * :cpp:class:`~mppp::real128` (see the :cpp:concept:`~mppp::RealInteroperable` concept), and it provides overloaded
 * :ref:`operators <real_operators>`. Differently from the builtin types, however, this class does not allow any
 * implicit conversion to/from other types (apart from ``bool``): construction from and conversion to primitive types
 * must always be requested explicitly. As a side effect, syntax such as
 *
 * .. code-block:: c++
 *
 *    real r = 5;
 *    int m = r;
 *
 * will not work, and direct initialization should be used instead:
 *
 * .. code-block:: c++
 *
 *    real r{5};
 *    int m{r};
 *
 * Most of the functionality is exposed via plain :ref:`functions <real_functions>`, with the
 * general convention that the functions are named after the corresponding MPFR functions minus the leading ``mpfr_``
 * prefix. For instance, the MPFR call
 *
 * .. code-block:: c++
 *
 *    mpfr_add(rop,a,b,MPFR_RNDN);
 *
 * that writes the result of ``a + b``, rounded to nearest, into ``rop``, becomes simply
 *
 * .. code-block:: c++
 *
 *    add(rop,a,b);
 *
 * where the ``add()`` function is resolved via argument-dependent lookup. Function calls with overlapping arguments
 * are allowed, unless noted otherwise. Unless otherwise specified, the :cpp:class:`~mppp::real` API always
 * rounds to nearest (that is, the ``MPFR_RNDN`` rounding mode is used).
 *
 * Multiple overloads of the same functionality are often available.
 * Binary functions in MPFR are usually implemented via three-operands functions, in which the first
 * operand is a reference to the return value. The exponentiation function ``mpfr_pow()``, for instance,
 * takes three operands: the return value, the base and the exponent. There are two overloads of the corresponding
 * :ref:`exponentiation <real_exponentiation>` function for :cpp:class:`~mppp::real`:
 *
 * * a ternary overload similar to ``mpfr_pow()``,
 * * a binary overload taking as inputs the base and the exponent, and returning the result
 *   of the exponentiation.
 *
 * This allows to avoid having to set up a return value for one-off invocations of ``pow()`` (the binary overload
 * will do it for you). For example:
 *
 * .. code-block:: c++
 *
 *    real r1, r2, r3{3}, r4{2};
 *    pow(r1,r3,r4);   // Ternary pow(): computes 3**2 and stores
 *                     // the result in r1.
 *    r2 = pow(r3,r4); // Binary pow(): returns 3**2, which is then
 *                     // assigned to r2.
 *
 * In case of unary functions, there are often three overloads available:
 *
 * * a binary overload taking as first parameter a reference to the return value (MPFR style),
 * * a unary overload returning the result of the operation,
 * * a nullary member function that modifies the calling object in-place.
 *
 * For instance, here are three possible ways of computing the absolute value:
 *
 * .. code-block:: c++
 *
 *    real r1, r2, r3{-5};
 *    abs(r1,r3);   // Binary abs(): computes and stores the absolute value
 *                  // of r3 into r1.
 *    r2 = abs(r3); // Unary abs(): returns the absolute value of r3, which is
 *                  // then assigned to r2.
 *    r3.abs();     // Member function abs(): replaces the value of r3 with its
 *                  // absolute value.
 *
 * Note that at this time a subset of the MPFR API has been wrapped by :cpp:class:`~mppp::real`.
 *
 * Various :ref:`overloaded operators <real_operators>` are provided. The arithmetic operators always return
 * a :cpp:class:`~mppp::real` result. The relational operators, ``==``, ``!=``, ``<``, ``>``, ``<=`` and ``>=`` will
 * promote non-:cpp:class:`~mppp::real` arguments to :cpp:class:`~mppp::real` before performing the comparison.
 * Alternative comparison functions
 * treating NaNs specially are provided for use in the C++ standard library (and wherever strict weak ordering relations
 * are needed).
 *
 * Member functions are provided to access directly the internal ``mpfr_t`` instance (see
 * :cpp:func:`mppp::real::get_mpfr_t()` and :cpp:func:`mppp::real::_get_mpfr_t()`), so that
 * it is possible to use transparently the MPFR API with :cpp:class:`~mppp::real` objects.
 * \endrststar
 */
class MPPP_DLL_PUBLIC real
{
#if !defined(MPPP_DOXYGEN_INVOKED)
    // Make friends, for accessing the non-checking prec setting funcs.
    template <bool, typename F, typename Arg0, typename... Args>
    friend real &detail::mpfr_nary_op_impl(::mpfr_prec_t, const F &, real &, Arg0 &&, Args &&...);
    template <bool, typename F, typename Arg0, typename... Args>
    friend real detail::mpfr_nary_op_return_impl(::mpfr_prec_t, const F &, Arg0 &&, Args &&...);
    template <typename F>
    friend real detail::real_constant(const F &, ::mpfr_prec_t);
#endif
    // Utility function to check the precision upon init.
    static ::mpfr_prec_t check_init_prec(::mpfr_prec_t p)
    {
        if (mppp_unlikely(!detail::real_prec_check(p))) {
            throw std::invalid_argument("Cannot init a real with a precision of " + detail::to_string(p)
                                        + ": the maximum allowed precision is " + detail::to_string(real_prec_max())
                                        + ", the minimum allowed precision is " + detail::to_string(real_prec_min()));
        }
        return p;
    }

public:
    // Default constructor.
    real();

private:
    // A tag to call private ctors.
    struct ptag {
    };
    // Init a real with precision p, setting its value to n. No precision
    // checking is performed.
    explicit real(const ptag &, ::mpfr_prec_t, bool);

public:
    // Copy constructor.
    real(const real &);
    // Copy constructor with custom precision.
    explicit real(const real &, ::mpfr_prec_t);
    /// Move constructor.
    /**
     * \rststar
     * .. warning::
     *    Unless otherwise noted, the only valid operations on the moved-from ``other`` object are
     *    destruction and copy/move assignment. After re-assignment, ``other`` can be used normally again.
     * \endrststar
     *
     * @param other the \link mppp::real real\endlink that will be moved.
     */
    real(real &&other) noexcept
    {
        // Shallow copy other.
        m_mpfr = other.m_mpfr;
        // Mark the other as moved-from.
        other.m_mpfr._mpfr_d = nullptr;
    }
    // Constructor from a special value, sign and precision.
    explicit real(real_kind, int, ::mpfr_prec_t);
    /// Constructor from a special value and precision.
    /**
     * This constructor is equivalent to the constructor from a special value, sign and precision
     * with a hard-coded sign of 0.
     *
     * @param k the desired special value.
     * @param p the desired precision for \p this.
     *
     * @throws unspecified any exception thrown by the constructor from a special value, sign and precision.
     */
    explicit real(real_kind k, ::mpfr_prec_t p) : real(k, 0, p) {}
    /// Constructor from a special value.
    /**
     * This constructor is equivalent to the constructor from a special value, sign and precision
     * with a hard-coded sign of 0 and hard-coded precision of 0.
     *
     * @param k the desired special value.
     *
     * @throws unspecified any exception thrown by the constructor from a special value, sign and precision.
     */
    explicit real(real_kind k) : real(k, 0, 0) {}

private:
    // A helper to determine the precision to use in the generic constructors. The precision
    // can be manually provided, taken from the default global value, or deduced according
    // to the properties of the type/value.
    template <typename T>
    static ::mpfr_prec_t compute_init_precision(::mpfr_prec_t provided, const T &x)
    {
        if (provided) {
            // Provided precision trumps everything. Check it and return it.
            return check_init_prec(provided);
        }
        // Return the default or deduced precision.
        return detail::real_dd_prec(x);
    }

    // Construction from FPs.
    template <typename Func, typename T>
    MPPP_DLL_LOCAL void dispatch_fp_construction(const Func &, const T &, ::mpfr_prec_t);
    void dispatch_construction(const float &, ::mpfr_prec_t);
    void dispatch_construction(const double &, ::mpfr_prec_t);
    void dispatch_construction(const long double &, ::mpfr_prec_t);

    // Construction from integral types.
    template <typename T>
    void dispatch_integral_init(::mpfr_prec_t p, const T &n)
    {
        ::mpfr_init2(&m_mpfr, compute_init_precision(p, n));
    }
    // Special casing for bool, otherwise MSVC warns if we fold this into the
    // constructor from unsigned.
    void dispatch_construction(const bool &, ::mpfr_prec_t);
    template <typename T,
              detail::enable_if_t<detail::conjunction<detail::is_integral<T>, detail::is_unsigned<T>>::value, int> = 0>
    void dispatch_construction(const T &n, ::mpfr_prec_t p)
    {
        dispatch_integral_init(p, n);
        if (n <= detail::nl_max<unsigned long>()) {
            ::mpfr_set_ui(&m_mpfr, static_cast<unsigned long>(n), MPFR_RNDN);
        } else {
            // NOTE: here and elsewhere let's use a 2-limb integer, in the hope
            // of avoiding dynamic memory allocation.
            ::mpfr_set_z(&m_mpfr, integer<2>(n).get_mpz_view(), MPFR_RNDN);
        }
    }
    template <typename T,
              detail::enable_if_t<detail::conjunction<detail::is_integral<T>, detail::is_signed<T>>::value, int> = 0>
    void dispatch_construction(const T &n, ::mpfr_prec_t p)
    {
        dispatch_integral_init(p, n);
        if (n <= detail::nl_max<long>() && n >= detail::nl_min<long>()) {
            ::mpfr_set_si(&m_mpfr, static_cast<long>(n), MPFR_RNDN);
        } else {
            ::mpfr_set_z(&m_mpfr, integer<2>(n).get_mpz_view(), MPFR_RNDN);
        }
    }

    // Construction from mppp::integer.
    void dispatch_mpz_construction(const ::mpz_t, ::mpfr_prec_t);
    template <std::size_t SSize>
    void dispatch_construction(const integer<SSize> &n, ::mpfr_prec_t p)
    {
        dispatch_mpz_construction(n.get_mpz_view(), compute_init_precision(p, n));
    }

    // Construction from mppp::rational.
    void dispatch_mpq_construction(const ::mpq_t, ::mpfr_prec_t);
    template <std::size_t SSize>
    void dispatch_construction(const rational<SSize> &q, ::mpfr_prec_t p)
    {
        // NOTE: get_mpq_view() returns an mpq_struct, whose
        // address we then need to use.
        const auto v = detail::get_mpq_view(q);
        dispatch_mpq_construction(&v, compute_init_precision(p, q));
    }

#if defined(MPPP_WITH_QUADMATH)
    void dispatch_construction(const real128 &, ::mpfr_prec_t);
    // NOTE: split this off from the dispatch_construction() overload, so we can re-use it in the
    // generic assignment.
    void assign_real128(const real128 &);
#endif

public:
    /// Generic constructor.
    /**
     * \rststar
     * The generic constructor will set ``this`` to the value of ``x`` with a precision of ``p``.
     *
     * If ``p`` is nonzero, then ``this`` will be initialised exactly to a precision of ``p``, and
     * a rounding operation might occurr.
     *
     * If ``p`` is zero, the constructor will first fetch the default precision ``dp`` via
     * :cpp:func:`~mppp::real_get_default_prec()`. If ``dp`` is nonzero, then ``dp`` will be used
     * as precision for ``this`` and a rounding operation might occurr.
     *
     * Otherwise, if ``dp`` is zero, the precision of ``this`` will be set according to the following
     * heuristics:
     *
     * * if ``x`` is a C++ integral type ``I``, then the precision is set to the bit width of ``I``;
     * * if ``x`` is a C++ floating-point type ``F``, then the precision is set to the number of binary digits
     *   in the significand of ``F``;
     * * if ``x`` is :cpp:class:`~mppp::integer`, then the precision is set to the number of bits in use by
     *   ``x`` (rounded up to the next multiple of the limb type's bit width);
     * * if ``x`` is :cpp:class:`~mppp::rational`, then the precision is set to the sum of the number of bits
     *   used by numerator and denominator (as established by the previous heuristic for :cpp:class:`~mppp::integer`);
     * * if ``x`` is :cpp:class:`~mppp::real128`, then the precision is set to 113.
     *
     * These heuristics aim at ensuring that, whatever the type of ``x``, its value is preserved exactly in the
     * constructed :cpp:class:`~mppp::real`.
     *
     * Construction from ``bool`` will initialise ``this`` to 1 for ``true``, and 0 for ``false``.
     * \endrststar
     *
     * @param x the construction argument.
     * @param p the desired precision.
     *
     * @throws std::overflow_error if an overflow occurs in the computation of the automatically-deduced precision.
     * @throws std::invalid_argument if \p p is nonzero and outside the range established by
     * \link mppp::real_prec_min() real_prec_min()\endlink and \link mppp::real_prec_max() real_prec_max()\endlink.
     */
#if defined(MPPP_HAVE_CONCEPTS)
    template <RealInteroperable T>
#else
    template <typename T, real_interoperable_enabler<T> = 0>
#endif
    explicit real(const T &x, ::mpfr_prec_t p = 0)
    {
        dispatch_construction(x, p);
    }

private:
    MPPP_DLL_LOCAL void construct_from_c_string(const char *, int, ::mpfr_prec_t);
    explicit real(const ptag &, const char *, int, ::mpfr_prec_t);
    explicit real(const ptag &, const std::string &, int, ::mpfr_prec_t);
#if defined(MPPP_HAVE_STRING_VIEW)
    explicit real(const ptag &, const std::string_view &, int, ::mpfr_prec_t);
#endif

public:
    /// Constructor from string, base and precision.
    /**
     * \rststar
     * This constructor will set ``this`` to the value represented by the :cpp:concept:`~mppp::StringType` ``s``, which
     * is interpreted as a floating-point number in base ``base``. ``base`` must be either zero (in which case the base
     * will be automatically deduced) or a number in the [2,62] range. The valid string formats are detailed in the
     * documentation of the MPFR function ``mpfr_set_str()``. Note that leading whitespaces are ignored, but trailing
     * whitespaces will raise an error.
     *
     * The precision of ``this`` will be ``p`` if ``p`` is nonzero, the default precision otherwise. If ``p`` is zero
     * and no default precision has been set, an error will be raised.
     *
     * .. seealso::
     *    https://www.mpfr.org/mpfr-current/mpfr.html#Assignment-Functions
     * \endrststar
     *
     * @param s the input string.
     * @param base the base used in the string representation.
     * @param p the desired precision.
     *
     * @throws std::invalid_argument in the following cases:
     * - \p base is not zero and not in the [2,62] range,
     * - \p p is either outside the valid bounds for a precision value, or it is zero and no
     *   default precision value has been set,
     * - \p s cannot be interpreted as a floating-point number.
     * @throws unspecified any exception thrown by memory errors in standard containers.
     */
#if defined(MPPP_HAVE_CONCEPTS)
    template <StringType T>
#else
    template <typename T, string_type_enabler<T> = 0>
#endif
    explicit real(const T &s, int base, ::mpfr_prec_t p) : real(ptag{}, s, base, p)
    {
    }
    /// Constructor from string and precision.
    /**
     * This constructor is equivalent to the constructor from string with a ``base`` value hard-coded to 10.
     *
     * @param s the input string.
     * @param p the desired precision.
     *
     * @throws unspecified any exception thrown by the constructor from string, base and precision.
     */
#if defined(MPPP_HAVE_CONCEPTS)
    template <StringType T>
#else
    template <typename T, string_type_enabler<T> = 0>
#endif
    explicit real(const T &s, ::mpfr_prec_t p) : real(s, 10, p)
    {
    }
    /// Constructor from string.
    /**
     * This constructor is equivalent to the constructor from string with a ``base`` value hard-coded to 10
     * and a precision value hard-coded to zero (that is, the precision will be the default precision, if set).
     *
     * @param s the input string.
     *
     * @throws unspecified any exception thrown by the constructor from string, base and precision.
     */
#if defined(MPPP_HAVE_CONCEPTS)
    template <StringType T>
#else
    template <typename T, string_type_enabler<T> = 0>
#endif
    explicit real(const T &s) : real(s, 10, 0)
    {
    }
    // Constructor from range of characters, base and precision.
    explicit real(const char *, const char *, int, ::mpfr_prec_t);
    // Constructor from range of characters and precision.
    explicit real(const char *, const char *, ::mpfr_prec_t);
    // Constructor from range of characters.
    explicit real(const char *, const char *);
    // Copy constructor from ``mpfr_t``.
    explicit real(const ::mpfr_t);
    /// Move constructor from ``mpfr_t``.
    /**
     * This constructor will initialise ``this`` with a shallow copy of ``x``.
     *
     * \rststar
     * .. warning::
     *    It is the user's responsibility to ensure that ``x`` has been correctly initialised
     *    with a precision within the bounds established by :cpp:func:`~mppp::real_prec_min()`
     *    and :cpp:func:`~mppp::real_prec_max()`.
     *
     *    Additionally, the user must ensure that, after construction, ``mpfr_clear()`` is never
     *    called on ``x``: the resources previously owned by ``x`` are now owned by ``this``, which
     *    will take care of releasing them when the destructor is called.
     * \endrststar
     *
     * @param x the ``mpfr_t`` that will be moved.
     */
    explicit real(::mpfr_t &&x) : m_mpfr(*x) {}

    // Destructor.
    ~real();

    // Copy assignment operator.
    real &operator=(const real &);

    /// Move assignment operator.
    /**
     * @param other the \link mppp::real real\endlink that will be moved into \p this.
     *
     * @return a reference to \p this.
     */
    real &operator=(real &&other) noexcept
    {
        // NOTE: for generic code, std::swap() is not a particularly good way of implementing
        // the move assignment:
        //
        // https://stackoverflow.com/questions/6687388/why-do-some-people-use-swap-for-move-assignments
        //
        // Here however it is fine, as we know there are no side effects we need to maintain.
        //
        // NOTE: we use a raw std::swap() here (instead of mpfr_swap()) because we don't know in principle
        // if mpfr_swap() relies on the operands not to be in a moved-from state (although it's unlikely).
        std::swap(m_mpfr, other.m_mpfr);
        return *this;
    }

private:
    // Assignment from FPs.
    template <bool SetPrec, typename Func, typename T>
    void dispatch_fp_assignment(const Func &func, const T &x)
    {
        if (SetPrec) {
            set_prec_impl<false>(detail::real_dd_prec(x));
        }
        func(&m_mpfr, x, MPFR_RNDN);
    }
    template <bool SetPrec>
    void dispatch_assignment(const float &x)
    {
        dispatch_fp_assignment<SetPrec>(::mpfr_set_flt, x);
    }
    template <bool SetPrec>
    void dispatch_assignment(const double &x)
    {
        dispatch_fp_assignment<SetPrec>(::mpfr_set_d, x);
    }
    template <bool SetPrec>
    void dispatch_assignment(const long double &x)
    {
        dispatch_fp_assignment<SetPrec>(::mpfr_set_ld, x);
    }
    // Assignment from integral types.
    template <bool SetPrec, typename T>
    void dispatch_integral_ass_prec(const T &n)
    {
        if (SetPrec) {
            set_prec_impl<false>(detail::real_dd_prec(n));
        }
    }
    // Special casing for bool.
    template <bool SetPrec>
    void dispatch_assignment(const bool &b)
    {
        dispatch_integral_ass_prec<SetPrec>(b);
        ::mpfr_set_ui(&m_mpfr, static_cast<unsigned long>(b), MPFR_RNDN);
    }
    template <bool SetPrec, typename T,
              detail::enable_if_t<detail::conjunction<detail::is_integral<T>, detail::is_unsigned<T>>::value, int> = 0>
    void dispatch_assignment(const T &n)
    {
        dispatch_integral_ass_prec<SetPrec>(n);
        if (n <= detail::nl_max<unsigned long>()) {
            ::mpfr_set_ui(&m_mpfr, static_cast<unsigned long>(n), MPFR_RNDN);
        } else {
            ::mpfr_set_z(&m_mpfr, integer<2>(n).get_mpz_view(), MPFR_RNDN);
        }
    }
    template <bool SetPrec, typename T,
              detail::enable_if_t<detail::conjunction<detail::is_integral<T>, detail::is_signed<T>>::value, int> = 0>
    void dispatch_assignment(const T &n)
    {
        dispatch_integral_ass_prec<SetPrec>(n);
        if (n <= detail::nl_max<long>() && n >= detail::nl_min<long>()) {
            ::mpfr_set_si(&m_mpfr, static_cast<long>(n), MPFR_RNDN);
        } else {
            ::mpfr_set_z(&m_mpfr, integer<2>(n).get_mpz_view(), MPFR_RNDN);
        }
    }
    template <bool SetPrec, std::size_t SSize>
    void dispatch_assignment(const integer<SSize> &n)
    {
        if (SetPrec) {
            set_prec_impl<false>(detail::real_dd_prec(n));
        }
        ::mpfr_set_z(&m_mpfr, n.get_mpz_view(), MPFR_RNDN);
    }
    template <bool SetPrec, std::size_t SSize>
    void dispatch_assignment(const rational<SSize> &q)
    {
        if (SetPrec) {
            set_prec_impl<false>(detail::real_dd_prec(q));
        }
        const auto v = detail::get_mpq_view(q);
        ::mpfr_set_q(&m_mpfr, &v, MPFR_RNDN);
    }
#if defined(MPPP_WITH_QUADMATH)
    template <bool SetPrec>
    void dispatch_assignment(const real128 &x)
    {
        if (SetPrec) {
            set_prec_impl<false>(detail::real_dd_prec(x));
        }
        assign_real128(x);
    }
#endif

public:
    /// Generic assignment operator.
    /**
     * \rststar
     * The generic assignment operator will set ``this`` to the value of ``x``.
     *
     * The operator will first fetch the default precision ``dp`` via
     * :cpp:func:`~mppp::real_get_default_prec()`. If ``dp`` is nonzero, then ``dp`` will be used
     * as a new precision for ``this`` and a rounding operation might occurr during the assignment.
     *
     * Otherwise, if ``dp`` is zero, the precision of ``this`` will be set according to the same
     * heuristics described in the generic constructor.
     * \endrststar
     *
     * @param x the assignment argument.
     *
     * @return a reference to \p this.
     *
     * @throws std::overflow_error if an overflow occurs in the computation of the automatically-deduced precision.
     */
#if defined(MPPP_HAVE_CONCEPTS)
    template <RealInteroperable T>
#else
    template <typename T, real_interoperable_enabler<T> = 0>
#endif
    real &operator=(const T &x)
    {
        dispatch_assignment<true>(x);
        return *this;
    }

private:
    // Implementation of the assignment from string.
    MPPP_DLL_LOCAL void string_assignment_impl(const char *, int);
    // Dispatching for string assignment.
    real &string_assignment(const char *);
    real &string_assignment(const std::string &);
#if defined(MPPP_HAVE_STRING_VIEW)
    real &string_assignment(const std::string_view &);
#endif

public:
    /// Assignment from string.
    /**
     * \rststar
     * This operator will set ``this`` to the value represented by the :cpp:concept:`~mppp::StringType` ``s``, which is
     * interpreted as a floating-point number in base 10. The precision of ``this`` will be set to the value returned by
     * :cpp:func:`~mppp::real_get_default_prec()`. If no default precision has been set, an error will be raised. If
     * ``s`` is not a valid representation of a floating-point number in base 10, ``this`` will be set to NaN and an
     * error will be raised.
     * \endrststar
     *
     * @param s the string that will be assigned to \p this.
     *
     * @return a reference to \p this.
     *
     * @throws std::invalid_argument if a default precision has not been set, or if \p s cannot be parsed
     * as a floating-point value in base 10.
     * @throws unspecified any exception thrown by memory allocation errors in standard containers.
     */
#if defined(MPPP_HAVE_CONCEPTS)
    template <StringType T>
#else
    template <typename T, string_type_enabler<T> = 0>
#endif
    real &operator=(const T &s)
    {
        return string_assignment(s);
    }
    // Copy assignment from ``mpfr_t``.
    real &operator=(const ::mpfr_t);

    /// Move assignment from ``mpfr_t``.
    /**
     * This operator will set ``this`` to a shallow copy of ``x``.
     *
     * \rststar
     * .. warning::
     *    It is the user's responsibility to ensure that ``x`` has been correctly initialised
     *    with a precision within the bounds established by :cpp:func:`~mppp::real_prec_min()`
     *    and :cpp:func:`~mppp::real_prec_max()`.
     *
     *    Additionally, the user must ensure that, after the assignment, ``mpfr_clear()`` is never
     *    called on ``x``: the resources previously owned by ``x`` are now owned by ``this``, which
     *    will take care of releasing them when the destructor is called.
     * \endrststar
     *
     * @param x the ``mpfr_t`` that will be moved.
     *
     * @return a reference to \p this.
     */
    real &operator=(::mpfr_t &&x)
    {
        // Clear this.
        ::mpfr_clear(&m_mpfr);
        // Shallow copy x.
        m_mpfr = *x;
        return *this;
    }

    // Set to another real.
    real &set(const real &);

    /// Generic setter.
    /**
     * \rststar
     * This method will set ``this`` to the value of ``x``. Contrary to the generic assignment operator,
     * the precision of the assignment is dictated by the precision of ``this``, rather than
     * being deduced from the type and value of ``x``. Consequently, the precision of ``this`` will not be altered
     * by the assignment, and a rounding might occur, depending on the operands.
     *
     * This method is a thin wrapper around various ``mpfr_set_*()``
     * assignment functions from the MPFR API.
     *
     * .. seealso ::
     *    https://www.mpfr.org/mpfr-current/mpfr.html#Assignment-Functions
     * \endrststar
     *
     * @param x the value to which \p this will be set.
     *
     * @return a reference to \p this.
     */
#if defined(MPPP_HAVE_CONCEPTS)
    template <RealInteroperable T>
#else
    template <typename T, real_interoperable_enabler<T> = 0>
#endif
    real &set(const T &x)
    {
        dispatch_assignment<false>(x);
        return *this;
    }

private:
    // Implementation of string setters.
    real &set_impl(const char *, int);
    real &set_impl(const std::string &, int);
#if defined(MPPP_HAVE_STRING_VIEW)
    real &set_impl(const std::string_view &, int);
#endif

public:
    /// Setter to string.
    /**
     * \rststar
     * This method will set ``this`` to the value represented by the :cpp:concept:`~mppp::StringType` ``s``, which will
     * be interpreted as a floating-point number in base ``base``. ``base`` must be either 0 (in which case the base is
     * automatically deduced), or a value in the [2,62] range. Contrary to the assignment operator from string,
     * the global default precision is ignored and the precision of the assignment is dictated by the precision of
     * ``this``. Consequently, the precision of ``this`` will not be altered by the assignment, and a rounding might
     * occur, depending on the operands.
     *
     * If ``s`` is not a valid representation of a floating-point number in base ``base``, ``this``
     * will be set to NaN and an error will be raised.
     *
     * This method is a thin wrapper around the ``mpfr_set_str()`` assignment function from the MPFR API.
     *
     * .. seealso ::
     *    https://www.mpfr.org/mpfr-current/mpfr.html#Assignment-Functions
     * \endrststar
     *
     * @param s the string to which \p this will be set.
     * @param base the base used in the string representation.
     *
     * @return a reference to \p this.
     *
     * @throws std::invalid_argument if \p s cannot be parsed as a floating-point value in base 10, or if the value
     * of \p base is invalid.
     * @throws unspecified any exception thrown by memory
     * allocation errors in standard containers.
     */
#if defined(MPPP_HAVE_CONCEPTS)
    template <StringType T>
#else
    template <typename T, string_type_enabler<T> = 0>
#endif
    real &set(const T &s, int base = 10)
    {
        return set_impl(s, base);
    }

    /// Set to character range.
    /**
     * This setter will set \p this to the content of the input half-open range,
     * which is interpreted as the string representation of a floating-point value in base \p base.
     *
     * Internally, the setter will copy the content of the range to a local buffer, add a
     * string terminator, and invoke the setter to string.
     *
     * @param begin the start of the input range.
     * @param end the end of the input range.
     * @param base the base used in the string representation.
     *
     * @return a reference to \p this.
     *
     * @throws unspecified any exception thrown by the setter to string, or by memory
     * allocation errors in standard containers.
     */
    real &set(const char *begin, const char *end, int base = 10);

    // Set to an mpfr_t.
    real &set(const ::mpfr_t);

    // Set to NaN.
    real &set_nan();

    /// Set to infinity.
    /**
     * This setter will set \p this to infinity. The sign bit will be positive if \p sign
     * is nonnegative, negative otherwise. The precision of \p this will not be altered.
     *
     * @param sign the sign of the infinity to which \p this will be set.
     *
     * @return a reference to \p this.
     */
    real &set_inf(int sign = 0);

    /// Set to zero.
    /**
     * This setter will set \p this to zero. The sign bit will be positive if \p sign
     * is nonnegative, negative otherwise. The precision of \p this will not be altered.
     *
     * @param sign the sign of the zero to which \p this will be set.
     *
     * @return a reference to \p this.
     */
    real &set_zero(int sign = 0);

    /// Const reference to the internal <tt>mpfr_t</tt>.
    /**
     * This method will return a const pointer to the internal MPFR structure used
     * to represent the value of \p this. The returned object can be used as a
     * <tt>const mpfr_t</tt> function parameter in the MPFR API.
     *
     * @return a const reference to the internal MPFR structure.
     */
    const mpfr_struct_t *get_mpfr_t() const
    {
        return &m_mpfr;
    }
    /// Mutable reference to the internal <tt>mpfr_t</tt>.
    /**
     * This method will return a mutable pointer to the internal MPFR structure used
     * to represent the value of \p this. The returned object can be used as an
     * <tt>mpfr_t</tt> function parameter in the MPFR API.
     *
     * \rststar
     * .. warning::
     *    When using this mutable getter, it is the user's responsibility to ensure
     *    that the internal MPFR structure is kept in a state which respects the invariants
     *    of the :cpp:class:`~mppp::real` class. Specifically, the precision value
     *    must be in the bounds established by :cpp:func:`~mppp::real_prec_min()` and
     *    :cpp:func:`~mppp::real_prec_max()`, and upon destruction a :cpp:class:`~mppp::real`
     *    object must contain a valid ``mpfr_t`` object.
     * \endrststar
     *
     * @return a mutable reference to the internal MPFR structure.
     */
    mpfr_struct_t *_get_mpfr_t()
    {
        return &m_mpfr;
    }

    /// Detect NaN.
    /**
     * @return \p true if \p this is NaN, \p false otherwise.
     */
    bool nan_p() const
    {
        return mpfr_nan_p(&m_mpfr) != 0;
    }

    /// Detect infinity.
    /**
     * @return \p true if \p this is an infinity, \p false otherwise.
     */
    bool inf_p() const
    {
        return mpfr_inf_p(&m_mpfr) != 0;
    }

    /// Detect finite number.
    /**
     * @return \p true if \p this is a finite number (i.e., not NaN or infinity), \p false otherwise.
     */
    bool number_p() const
    {
        return mpfr_number_p(&m_mpfr) != 0;
    }

    /// Detect zero.
    /**
     * @return \p true if \p this is zero, \p false otherwise.
     */
    bool zero_p() const
    {
        return mpfr_zero_p(&m_mpfr) != 0;
    }

    /// Detect regular number.
    /**
     * @return \p true if \p this is a regular number (i.e., not NaN, infinity or zero), \p false otherwise.
     */
    bool regular_p() const
    {
        return mpfr_regular_p(&m_mpfr) != 0;
    }

    // Detect one.
    bool is_one() const;

    /// Detect sign.
    /**
     * @return a positive value if \p this is positive, zero if \p this is zero, a negative value if \p this
     * is negative.
     *
     * @throws std::domain_error if \p this is NaN.
     */
    int sgn() const
    {
        if (mppp_unlikely(nan_p())) {
            // NOTE: mpfr_sgn() in this case would set an error flag, and we generally
            // handle error flags as exceptions.
            throw std::domain_error("Cannot determine the sign of a real NaN");
        }
        return mpfr_sgn(&m_mpfr);
    }

    /// Get the sign bit.
    /**
     * The sign bit is set if ``this`` is negative, -0, or a NaN whose representation has its sign bit set.
     *
     * @return the sign bit of \p this.
     */
    bool signbit() const
    {
        return mpfr_signbit(&m_mpfr) != 0;
    }

    /// Get the precision of \p this.
    /**
     * @return the current precision (i.e., the number of binary digits in the significand) of \p this.
     */
    ::mpfr_prec_t get_prec() const
    {
        return mpfr_get_prec(&m_mpfr);
    }

private:
    // Wrapper to apply the input unary MPFR function to this with
    // MPFR_RNDN rounding mode. Returns a reference to this.
    template <typename T>
    MPPP_DLL_LOCAL real &self_mpfr_unary(T &&);

public:
    // Negate in-place.
    real &neg();
    // In-place absolute value.
    real &abs();

private:
    // Utility function to check precision in set_prec().
    static ::mpfr_prec_t check_set_prec(::mpfr_prec_t p)
    {
        if (mppp_unlikely(!detail::real_prec_check(p))) {
            throw std::invalid_argument("Cannot set the precision of a real to the value " + detail::to_string(p)
                                        + ": the maximum allowed precision is " + detail::to_string(real_prec_max())
                                        + ", the minimum allowed precision is " + detail::to_string(real_prec_min()));
        }
        return p;
    }
    // mpfr_set_prec() wrapper, with or without prec checking.
    template <bool Check>
    void set_prec_impl(::mpfr_prec_t p)
    {
        ::mpfr_set_prec(&m_mpfr, Check ? check_set_prec(p) : p);
    }
    // mpfr_prec_round() wrapper, with or without prec checking.
    template <bool Check>
    void prec_round_impl(::mpfr_prec_t p)
    {
        ::mpfr_prec_round(&m_mpfr, Check ? check_set_prec(p) : p, MPFR_RNDN);
    }

public:
    // Destructively set the precision.
    real &set_prec(::mpfr_prec_t);
    // Set the precision maintaining the current value.
    real &prec_round(::mpfr_prec_t);

private:
    // Generic conversion.
    // integer.
    template <typename T, detail::enable_if_t<detail::is_integer<T>::value, int> = 0>
    T dispatch_conversion() const
    {
        if (mppp_unlikely(!number_p())) {
            throw std::domain_error("Cannot convert a non-finite real to an integer");
        }
        MPPP_MAYBE_TLS detail::mpz_raii mpz;
        // Truncate the value when converting to integer.
        ::mpfr_get_z(&mpz.m_mpz, &m_mpfr, MPFR_RNDZ);
        return T{&mpz.m_mpz};
    }
    // rational.
    template <std::size_t SSize>
    bool rational_conversion(rational<SSize> &rop) const
    {
#if MPFR_VERSION_MAJOR >= 4
        MPPP_MAYBE_TLS detail::mpq_raii mpq;
        // NOTE: we already checked outside
        // that rop is a finite number, hence
        // this function cannot fail.
        ::mpfr_get_q(&mpq.m_mpq, &m_mpfr);
        rop = &mpq.m_mpq;
        return true;
#else
        // Clear the range error flag before attempting the conversion.
        ::mpfr_clear_erangeflag();
        // NOTE: this call can fail if the exponent of this is very close to the upper/lower limits of the exponent
        // type. If the call fails (signalled by a range flag being set), we will return error.
        MPPP_MAYBE_TLS detail::mpz_raii mpz;
        const ::mpfr_exp_t exp2 = ::mpfr_get_z_2exp(&mpz.m_mpz, &m_mpfr);
        // NOTE: not sure at the moment how to trigger this, let's leave it for now.
        // LCOV_EXCL_START
        if (mppp_unlikely(::mpfr_erangeflag_p())) {
            // Let's first reset the error flag.
            ::mpfr_clear_erangeflag();
            return false;
        }
        // LCOV_EXCL_STOP
        // The conversion to n * 2**exp succeeded. We will build a rational
        // from n and exp.
        rop._get_num() = &mpz.m_mpz;
        rop._get_den().set_one();
        if (exp2 >= ::mpfr_exp_t(0)) {
            // The output value will be an integer.
            rop._get_num() <<= detail::make_unsigned(exp2);
        } else {
            // The output value will be a rational. Canonicalisation will be needed.
            rop._get_den() <<= detail::nint_abs(exp2);
            canonicalise(rop);
        }
        return true;
#endif
    }
    template <typename T, detail::enable_if_t<detail::is_rational<T>::value, int> = 0>
    T dispatch_conversion() const
    {
        if (mppp_unlikely(!number_p())) {
            throw std::domain_error("Cannot convert a non-finite real to a rational");
        }
        T rop;
        // LCOV_EXCL_START
        if (mppp_unlikely(!rational_conversion(rop))) {
            throw std::overflow_error("The exponent of a real is too large for conversion to rational");
        }
        // LCOV_EXCL_STOP
        return rop;
    }
    // C++ floating-point.
    template <typename T, detail::enable_if_t<std::is_floating_point<T>::value, int> = 0>
    T dispatch_conversion() const
    {
        if (std::is_same<T, float>::value) {
            return static_cast<T>(::mpfr_get_flt(&m_mpfr, MPFR_RNDN));
        }
        if (std::is_same<T, double>::value) {
            return static_cast<T>(::mpfr_get_d(&m_mpfr, MPFR_RNDN));
        }
        assert((std::is_same<T, long double>::value));
        return static_cast<T>(::mpfr_get_ld(&m_mpfr, MPFR_RNDN));
    }
    // Small helper to raise an exception when converting to C++ integrals.
    template <typename T>
    [[noreturn]] void raise_overflow_error() const
    {
        throw std::overflow_error("Conversion of the real " + to_string() + " to the type '" + type_name<T>()
                                  + "' results in overflow");
    }
    // Unsigned integrals, excluding bool.
    template <typename T>
    bool uint_conversion(T &rop) const
    {
        // Clear the range error flag before attempting the conversion.
        ::mpfr_clear_erangeflag();
        // NOTE: this will handle correctly the case in which this is negative but greater than -1.
        const unsigned long candidate = ::mpfr_get_ui(&m_mpfr, MPFR_RNDZ);
        if (::mpfr_erangeflag_p()) {
            // If the range error flag is set, it means the conversion failed because this is outside
            // the range of unsigned long. Let's clear the error flag first.
            ::mpfr_clear_erangeflag();
            // If the value is positive, and the target type has a range greater than unsigned long,
            // we will attempt the conversion again via integer.
            if (detail::nl_max<T>() > detail::nl_max<unsigned long>() && sgn() > 0) {
                return mppp::get(rop, static_cast<integer<2>>(*this));
            }
            return false;
        }
        if (candidate <= detail::nl_max<T>()) {
            rop = static_cast<T>(candidate);
            return true;
        }
        return false;
    }
    template <typename T,
              detail::enable_if_t<detail::conjunction<detail::negation<std::is_same<bool, T>>, detail::is_integral<T>,
                                                      detail::is_unsigned<T>>::value,
                                  int> = 0>
    T dispatch_conversion() const
    {
        if (mppp_unlikely(!number_p())) {
            throw std::domain_error("Cannot convert a non-finite real to a C++ unsigned integral type");
        }
        T rop;
        if (mppp_unlikely(!uint_conversion(rop))) {
            raise_overflow_error<T>();
        }
        return rop;
    }
    // bool.
    template <typename T, detail::enable_if_t<std::is_same<bool, T>::value, int> = 0>
    T dispatch_conversion() const
    {
        // NOTE: in C/C++ the conversion of NaN to bool gives true:
        // https://stackoverflow.com/questions/9158567/nan-to-bool-conversion-true-or-false
        return !zero_p();
    }
    // Signed integrals.
    template <typename T>
    bool sint_conversion(T &rop) const
    {
        ::mpfr_clear_erangeflag();
        const long candidate = ::mpfr_get_si(&m_mpfr, MPFR_RNDZ);
        if (::mpfr_erangeflag_p()) {
            // If the range error flag is set, it means the conversion failed because this is outside
            // the range of long. Let's clear the error flag first.
            ::mpfr_clear_erangeflag();
            // If the target type has a range greater than long,
            // we will attempt the conversion again via integer.
            if (detail::nl_min<T>() < detail::nl_min<long>() && detail::nl_max<T>() > detail::nl_max<long>()) {
                return mppp::get(rop, static_cast<integer<2>>(*this));
            }
            return false;
        }
        if (candidate >= detail::nl_min<T>() && candidate <= detail::nl_max<T>()) {
            rop = static_cast<T>(candidate);
            return true;
        }
        return false;
    }
    template <typename T,
              detail::enable_if_t<detail::conjunction<detail::is_integral<T>, detail::is_signed<T>>::value, int> = 0>
    T dispatch_conversion() const
    {
        if (mppp_unlikely(!number_p())) {
            throw std::domain_error("Cannot convert a non-finite real to a C++ signed integral type");
        }
        T rop;
        if (mppp_unlikely(!sint_conversion(rop))) {
            raise_overflow_error<T>();
        }
        return rop;
    }
#if defined(MPPP_WITH_QUADMATH)
    template <typename T, detail::enable_if_t<std::is_same<real128, T>::value, int> = 0>
    T dispatch_conversion() const
    {
        return convert_to_real128();
    }
    real128 convert_to_real128() const;
#endif

public:
    /// Generic conversion operator.
    /**
     * \rststar
     * This operator will convert ``this`` to a :cpp:concept:`~mppp::RealInteroperable` type. The conversion
     * proceeds as follows:
     *
     * - if ``T`` is ``bool``, then the conversion returns ``false`` if ``this`` is zero, ``true`` otherwise
     *   (including if ``this`` is NaN);
     * - if ``T`` is a C++ integral type other than ``bool``, the conversion will yield the truncated counterpart
     *   of ``this`` (i.e., the conversion rounds to zero). The conversion may fail due to overflow or domain errors
     *   (i.e., when trying to convert non-finite values);
     * - if ``T`` if a C++ floating-point type, the conversion calls directly the low-level MPFR functions (e.g.,
     *   ``mpfr_get_d()``), and might yield infinities for finite input values;
     * - if ``T`` is :cpp:class:`~mppp::integer`, the conversion rounds to zero and might fail due to domain errors,
     *   but it will never overflow;
     * - if ``T`` is :cpp:class:`~mppp::rational`, the conversion may fail if ``this`` is not finite or if the
     *   conversion produces an overflow in the manipulation of the exponent of ``this`` (that is, if
     *   the absolute value of ``this`` is very large or very small). If the conversion succeeds, it will be exact;
     * - if ``T`` is :cpp:class:`~mppp::real128`, the conversion might yield infinities for finite input values.
     *
     * \endrststar
     *
     * @return \p this converted to \p T.
     *
     * @throws std::domain_error if \p this is not finite and the target type cannot represent non-finite numbers.
     * @throws std::overflow_error if the conversion results in overflow.
     */
#if defined(MPPP_HAVE_CONCEPTS)
    template <RealInteroperable T>
#else
    template <typename T, real_interoperable_enabler<T> = 0>
#endif
    explicit operator T() const
    {
        return dispatch_conversion<T>();
    }

private:
    template <std::size_t SSize>
    bool dispatch_get(integer<SSize> &rop) const
    {
        if (!number_p()) {
            return false;
        }
        MPPP_MAYBE_TLS detail::mpz_raii mpz;
        // Truncate the value when converting to integer.
        ::mpfr_get_z(&mpz.m_mpz, &m_mpfr, MPFR_RNDZ);
        rop = &mpz.m_mpz;
        return true;
    }
    template <std::size_t SSize>
    bool dispatch_get(rational<SSize> &rop) const
    {
        if (!number_p()) {
            return false;
        }
        return rational_conversion(rop);
    }
    bool dispatch_get(bool &b) const
    {
        b = !zero_p();
        return true;
    }
    template <typename T,
              detail::enable_if_t<detail::conjunction<detail::is_integral<T>, detail::is_unsigned<T>>::value, int> = 0>
    bool dispatch_get(T &n) const
    {
        if (!number_p()) {
            return false;
        }
        return uint_conversion(n);
    }
    template <typename T,
              detail::enable_if_t<detail::conjunction<detail::is_integral<T>, detail::is_signed<T>>::value, int> = 0>
    bool dispatch_get(T &n) const
    {
        if (!number_p()) {
            return false;
        }
        return sint_conversion(n);
    }
    template <typename T, detail::enable_if_t<std::is_floating_point<T>::value, int> = 0>
    bool dispatch_get(T &x) const
    {
        x = static_cast<T>(*this);
        return true;
    }
#if defined(MPPP_WITH_QUADMATH)
    bool dispatch_get(real128 &x) const
    {
        x = static_cast<real128>(*this);
        return true;
    }
#endif

public:
    /// Generic conversion method.
    /**
     * \rststar
     * This method, similarly to the conversion operator, will convert ``this`` to a
     * :cpp:concept:`~mppp::RealInteroperable` type, storing the result of the conversion into ``rop``. Differently
     * from the conversion operator, this method does not raise any exception: if the conversion is successful, the
     * method will return ``true``, otherwise the method will return ``false``. If the conversion fails,
     * ``rop`` will not be altered.
     * \endrststar
     *
     * @param rop the variable which will store the result of the conversion.
     *
     * @return ``true`` if the conversion succeeded, ``false`` otherwise. The conversion can fail in the ways
     * specified in the documentation of the conversion operator.
     */
#if defined(MPPP_HAVE_CONCEPTS)
    template <RealInteroperable T>
#else
    template <typename T, real_interoperable_enabler<T> = 0>
#endif
    bool get(T &rop) const
    {
        return dispatch_get(rop);
    }
    /// Convert to string.
    /**
     * \rststar
     * This method will convert ``this`` to a string representation in base ``base``. The returned string is guaranteed
     * to produce exactly the original value when used in one of the constructors from string of
     * :cpp:class:`~mppp::real` (provided that the original precision and base are used in the construction).
     * \endrststar
     *
     * @param base the base to be used for the string representation.
     *
     * @return \p this converted to a string.
     *
     * @throws std::invalid_argument if \p base is not in the [2,62] range.
     * @throws std::runtime_error if the call to the ``mpfr_get_str()`` function of the MPFR API fails.
     */
    std::string to_string(int base = 10) const;

    // In-place square root.
    real &sqrt();

    // In-place reciprocal square root.
    real &rec_sqrt();

    // In-place cubic root.
    real &cbrt();

    // In-place sine.
    real &sin();

    // In-place cosine.
    real &cos();

    // In-place tangent.
    real &tan();

    // In-place secant.
    real &sec();

    // In-place cosecant.
    real &csc();

    // In-place cotangent.
    real &cot();

    // In-place arccosine.
    real &acos();

    // In-place arcsine.
    real &asin();

    // In-place arctangent.
    real &atan();

    // In-place hyperbolic cosine.
    real &cosh();

    // In-place hyperbolic sine.
    real &sinh();

    // In-place hyperbolic tangent.
    real &tanh();

    // In-place hyperbolic secant.
    real &sech();

    // In-place hyperbolic cosecant.
    real &csch();

    // In-place hyperbolic cotangent.
    real &coth();

    // In-place inverse hyperbolic cosine.
    real &acosh();

    // In-place inverse hyperbolic sine.
    real &asinh();

    // In-place inverse hyperbolic tangent.
    real &atanh();

    // In-place exponential.
    real &exp();

    // In-place base-2 exponential.
    real &exp2();

    // In-place base-10 exponential.
    real &exp10();

    // In-place exponential minus 1.
    real &expm1();

    // In-place logarithm.
    real &log();

    // In-place base-2 logarithm.
    real &log2();

    // In-place base-10 logarithm.
    real &log10();

    // In-place augmented logarithm.
    real &log1p();

    // In-place Gamma function.
    real &gamma();

    // In-place logarithm of the Gamma function.
    real &lngamma();

    // In-place logarithm of the absolute value of the Gamma function.
    real &lgamma();

    // In-place Digamma function.
    real &digamma();

    // In-place Bessel function of the first kind of order 0.
    real &j0();

    // In-place Bessel function of the first kind of order 1.
    real &j1();

    // In-place Bessel function of the second kind of order 0.
    real &y0();

    // In-place Bessel function of the second kind of order 1.
    real &y1();

    // In-place exponential integral.
    real &eint();

    // In-place dilogarithm.
    real &li2();

    // In-place Riemann Zeta function.
    real &zeta();

    // In-place error function.
    real &erf();

    // In-place complementary error function.
    real &erfc();

    // In-place Airy function.
    real &ai();

    // Check if the value is an integer.
    bool integer_p() const;

    // In-place truncation.
    real &trunc();

private:
    mpfr_struct_t m_mpfr;
};

// Double check that real is a standard layout class.
static_assert(std::is_standard_layout<real>::value, "real is not a standard layout class.");

template <typename T, typename U>
using are_real_op_types = detail::disjunction<
    detail::conjunction<std::is_same<real, detail::uncvref_t<T>>, std::is_same<real, detail::uncvref_t<U>>>,
    detail::conjunction<std::is_same<real, detail::uncvref_t<T>>, is_real_interoperable<detail::uncvref_t<U>>>,
    detail::conjunction<std::is_same<real, detail::uncvref_t<U>>, is_real_interoperable<detail::uncvref_t<T>>>>;

template <typename T, typename U>
#if defined(MPPP_HAVE_CONCEPTS)
MPPP_CONCEPT_DECL RealOpTypes = are_real_op_types<T, U>::value;
#else
using real_op_types_enabler = detail::enable_if_t<are_real_op_types<T, U>::value, int>;
#endif

template <typename T, typename U>
#if defined(MPPP_HAVE_CONCEPTS)
MPPP_CONCEPT_DECL RealCompoundOpTypes = RealOpTypes<T, U> && !std::is_const<detail::unref_t<T>>::value;
#else
using real_compound_op_types_enabler = detail::enable_if_t<
    detail::conjunction<are_real_op_types<T, U>, detail::negation<std::is_const<detail::unref_t<T>>>>::value, int>;
#endif

/// Destructively set the precision of a \link mppp::real real\endlink.
/**
 * \ingroup real_prec
 * \rststar
 * This function will set the precision of ``r`` to exactly ``p`` bits. The value
 * of ``r`` will be set to NaN.
 * \endrststar
 *
 * @param r the \link mppp::real real\endlink whose precision will be set.
 * @param p the desired precision.
 *
 * @throws unspecified any exception thrown by mppp::real::set_prec().
 */
inline void set_prec(real &r, ::mpfr_prec_t p)
{
    r.set_prec(p);
}

/// Set the precision of a \link mppp::real real\endlink maintaining the current value.
/**
 * \ingroup real_prec
 * \rststar
 * This function will set the precision of ``r`` to exactly ``p`` bits. If ``p``
 * is smaller than the current precision of ``r``, a rounding operation will be performed,
 * otherwise the value will be preserved exactly.
 * \endrststar
 *
 * @param r the \link mppp::real real\endlink whose precision will be set.
 * @param p the desired precision.
 *
 * @throws unspecified any exception thrown by mppp::real::prec_round().
 */
inline void prec_round(real &r, ::mpfr_prec_t p)
{
    r.prec_round(p);
}

/// Get the precision of a \link mppp::real real\endlink.
/**
 * \ingroup real_prec
 *
 * @param r the \link mppp::real real\endlink whose precision will be returned.
 *
 * @return the precision of \p r.
 */
inline mpfr_prec_t get_prec(const real &r)
{
    return r.get_prec();
}

namespace detail
{

template <typename... Args>
using real_set_t = decltype(std::declval<real &>().set(std::declval<const Args &>()...));
}

/** @defgroup real_assignment real_assignment
 *  @{
 */

#if !defined(MPPP_DOXYGEN_INVOKED)

template <typename... Args>
#if defined(MPPP_HAVE_CONCEPTS)
MPPP_CONCEPT_DECL RealSetArgs = detail::is_detected<detail::real_set_t, Args...>::value;
#else
using real_set_args_enabler = detail::enable_if_t<detail::is_detected<detail::real_set_t, Args...>::value, int>;
#endif

#endif

/// Generic setter for \link mppp::real real\endlink.
/**
 * \rststar
 * This function will use the arguments ``args`` to set the value of the :cpp:class:`~mppp::real` ``r``,
 * using one of the available :cpp:func:`mppp::real::set()` overloads. That is,
 * the body of this function is equivalent to
 *
 * .. code-block:: c++
 *
 *    return r.set(args...);
 *
 * The input arguments must satisfy the :cpp:concept:`~mppp::RealSetArgs` concept.
 * \endrststar
 *
 * @param r the \link mppp::real real\endlink that will be set.
 * @param args the arguments that will be passed to mppp::real::set().
 *
 * @return a reference to \p r.
 *
 * @throws unspecified any exception thrown by the invoked mppp::real::set() overload.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <RealSetArgs... Args>
#else
template <typename... Args, real_set_args_enabler<Args...> = 0>
#endif
inline real &set(real &r, const Args &... args)
{
    return r.set(args...);
}

/// Set \link mppp::real real\endlink to \f$n\times 2^e\f$.
/**
 * This function will set ``r`` to \f$n\times 2^e\f$. The precision of ``r``
 * will not be altered. If ``n`` is zero, the result will be positive zero.
 *
 * @param r the \link mppp::real real\endlink argument.
 * @param n the \link mppp::integer integer\endlink multiplier.
 * @param e the exponent.
 *
 * @return a reference to ``r``.
 */
template <std::size_t SSize>
inline real &set_z_2exp(real &r, const integer<SSize> &n, ::mpfr_exp_t e)
{
    ::mpfr_set_z_2exp(r._get_mpfr_t(), n.get_mpz_view(), e, MPFR_RNDN);
    return r;
}

/// Set \link mppp::real real\endlink to NaN.
/**
 * This function will set \p r to NaN with an unspecified sign bit. The precision of \p r
 * will not be altered.
 *
 * @param r the \link mppp::real real\endlink argument.
 *
 * @return a reference to \p r.
 */
inline real &set_nan(real &r)
{
    return r.set_nan();
}

/// Set \link mppp::real real\endlink to infinity.
/**
 * This function will set \p r to infinity. The sign bit will be positive if \p sign
 * is nonnegative, negative otherwise. The precision of \p r will not be altered.
 *
 * @param r the \link mppp::real real\endlink argument.
 * @param sign the sign of the infinity to which \p r will be set.
 *
 * @return a reference to \p r.
 */
inline real &set_inf(real &r, int sign = 0)
{
    return r.set_inf(sign);
}

/// Set \link mppp::real real\endlink to zero.
/**
 * This function will set \p r to zero. The sign bit will be positive if \p sign
 * is nonnegative, negative otherwise. The precision of \p r will not be altered.
 *
 * @param r the \link mppp::real real\endlink argument.
 * @param sign the sign of the zero to which \p r will be set.
 *
 * @return a reference to \p r.
 */
inline real &set_zero(real &r, int sign = 0)
{
    return r.set_zero(sign);
}

/// Swap \link mppp::real real\endlink objects.
/**
 * This function will efficiently swap the content of \p a and \p b.
 *
 * @param a the first operand.
 * @param b the second operand.
 */
inline void swap(real &a, real &b) noexcept
{
    ::mpfr_swap(a._get_mpfr_t(), b._get_mpfr_t());
}

/** @} */

/** @defgroup real_conversion real_conversion
 *  @{
 */

/// Generic conversion function for \link mppp::real real\endlink.
/**
 * \rststar
 * This function will convert the input :cpp:class:`~mppp::real` ``x`` to a
 * :cpp:concept:`~mppp::RealInteroperable` type, storing the result of the conversion into ``rop``.
 * If the conversion is successful, the function
 * will return ``true``, otherwise the function will return ``false``. If the conversion fails, ``rop`` will
 * not be altered.
 * \endrststar
 *
 * @param rop the variable which will store the result of the conversion.
 * @param x the input \link mppp::real real\endlink.
 *
 * @return ``true`` if the conversion succeeded, ``false`` otherwise. The conversion can fail in the ways
 * specified in the documentation of the conversion operator for \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <RealInteroperable T>
#else
template <typename T, real_interoperable_enabler<T> = 0>
#endif
inline bool get(T &rop, const real &x)
{
    return x.get(rop);
}

/// Extract the significand and the exponent of a \link mppp::real real\endlink.
/**
 * This function will extract the scaled significand of ``r`` into ``n``, and return the
 * exponent ``e`` such that ``r`` equals \f$n\times 2^e\f$.
 *
 * If ``r`` is not finite, an error will be raised.
 *
 * @param n the \link mppp::integer integer\endlink that will contain the scaled
 * significand of ``r``.
 * @param r the input \link mppp::real real\endlink.
 *
 * @return the exponent ``e`` such that ``r`` equals \f$n\times 2^e\f$.
 *
 * @throws std::domain_error if ``r`` is not finite.
 * @throws std::overflow_error if the output exponent is larger than an implementation-defined
 * value.
 */
template <std::size_t SSize>
inline mpfr_exp_t get_z_2exp(integer<SSize> &n, const real &r)
{
    if (mppp_unlikely(!r.number_p())) {
        throw std::domain_error("Cannot extract the significand and the exponent of a non-finite real");
    }
    MPPP_MAYBE_TLS detail::mpz_raii m;
    ::mpfr_clear_erangeflag();
    auto retval = ::mpfr_get_z_2exp(&m.m_mpz, r.get_mpfr_t());
    // LCOV_EXCL_START
    if (mppp_unlikely(::mpfr_erangeflag_p())) {
        ::mpfr_clear_erangeflag();
        throw std::overflow_error("Cannot extract the exponent of the real value " + r.to_string()
                                  + ": the exponent's magnitude is too large");
    }
    // LCOV_EXCL_STOP
    n = &m.m_mpz;
    return retval;
}

/** @} */

namespace detail
{

#if !defined(MPPP_DOXYGEN_INVOKED)

// A small helper to init the pairs in the functions below. We need this because
// we cannot take the address of a const real as a real *.
template <typename Arg, enable_if_t<!is_ncrvr<Arg &&>::value, int> = 0>
inline std::pair<real *, ::mpfr_prec_t> mpfr_nary_op_init_pair(::mpfr_prec_t min_prec, Arg &&arg)
{
    // arg is not a non-const rvalue ref, we cannot steal from it. Init with nullptr.
    return std::make_pair(static_cast<real *>(nullptr), c_max(arg.get_prec(), min_prec));
}

template <typename Arg, enable_if_t<is_ncrvr<Arg &&>::value, int> = 0>
inline std::pair<real *, ::mpfr_prec_t> mpfr_nary_op_init_pair(::mpfr_prec_t min_prec, Arg &&arg)
{
    // arg is a non-const rvalue ref, and a candidate for stealing resources.
    return std::make_pair(&arg, c_max(arg.get_prec(), min_prec));
}

// A recursive function to determine, in an MPFR function call,
// the largest argument we can steal resources from, and the max precision among
// all the arguments.
inline void mpfr_nary_op_check_steal(std::pair<real *, ::mpfr_prec_t> &) {}

// NOTE: we need 2 overloads for this, as we cannot extract a non-const pointer from
// arg0 if arg0 is a const ref.
template <typename Arg0, typename... Args, enable_if_t<!is_ncrvr<Arg0 &&>::value, int> = 0>
void mpfr_nary_op_check_steal(std::pair<real *, ::mpfr_prec_t> &, Arg0 &&, Args &&...);

template <typename Arg0, typename... Args, enable_if_t<is_ncrvr<Arg0 &&>::value, int> = 0>
void mpfr_nary_op_check_steal(std::pair<real *, ::mpfr_prec_t> &, Arg0 &&, Args &&...);

template <typename Arg0, typename... Args, enable_if_t<!is_ncrvr<Arg0 &&>::value, int>>
inline void mpfr_nary_op_check_steal(std::pair<real *, ::mpfr_prec_t> &p, Arg0 &&arg0, Args &&... args)
{
    // arg0 is not a non-const rvalue ref, we won't be able to steal from it regardless. Just
    // update the max prec.
    p.second = c_max(arg0.get_prec(), p.second);
    mpfr_nary_op_check_steal(p, std::forward<Args>(args)...);
}

template <typename Arg0, typename... Args, enable_if_t<is_ncrvr<Arg0 &&>::value, int>>
inline void mpfr_nary_op_check_steal(std::pair<real *, ::mpfr_prec_t> &p, Arg0 &&arg0, Args &&... args)
{
    const auto prec0 = arg0.get_prec();
    if (!p.first || prec0 > p.first->get_prec()) {
        // The current argument arg0 is a non-const rvalue reference, and either it's
        // the first argument we encounter we can steal from, or it has a precision
        // larger than the current candidate for stealing resources from. This means that
        // arg0 is the new candidate.
        p.first = &arg0;
    }
    // Update the max precision among the arguments, if necessary.
    p.second = c_max(prec0, p.second);
    mpfr_nary_op_check_steal(p, std::forward<Args>(args)...);
}

// A small wrapper to call an MPFR function f with arguments args. If the first param is true_type,
// the rounding mode MPFR_RNDN will be appended at the end of the function arguments list.
template <typename F, typename... Args>
inline void mpfr_nary_func_wrapper(const std::true_type &, const F &f, Args &&... args)
{
    f(std::forward<Args>(args)..., MPFR_RNDN);
}

template <typename F, typename... Args>
inline void mpfr_nary_func_wrapper(const std::false_type &, const F &f, Args &&... args)
{
    f(std::forward<Args>(args)...);
}

// Apply the MPFR n-ary function F with return value rop and real arguments (arg0, args...).
// The precision of rop will be set to the maximum of the precision among the arguments,
// but not less than min_prec.
// Resources may be stolen from one of the arguments, if possible.
// The Rnd flag controls whether to add the rounding mode (MPFR_RNDN) at the end
// of the function arguments list or not.
// NOTE: this function assumes that all arguments are reals. When we want to invoke MPFR
// functions that have argument types other than mpfr_t, then we will have to wrap them
// into a lambda (or similar) that has only mpfr_t arguments.
template <bool Rnd, typename F, typename Arg0, typename... Args>
inline real &mpfr_nary_op_impl(::mpfr_prec_t min_prec, const F &f, real &rop, Arg0 &&arg0, Args &&... args)
{
    // This pair contains:
    // - a pointer to the largest-precision real from which we can steal resources (may be nullptr),
    // - the largest precision among all arguments.
    // It's inited with arg0's precision (but no less than min_prec), and a pointer to arg0, if arg0 is a nonconst
    // rvalue ref (a nullptr otherwise).
    auto p = mpfr_nary_op_init_pair(min_prec, std::forward<Arg0>(arg0));
    // Examine the remaining arguments.
    mpfr_nary_op_check_steal(p, std::forward<Args>(args)...);
    // Cache this for convenience.
    const auto r_prec = rop.get_prec();
    if (p.second == r_prec) {
        // The largest precision among the operands and the precision of the return value
        // match. No need to steal, just execute the function.
        mpfr_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, rop._get_mpfr_t(), arg0.get_mpfr_t(),
                               args.get_mpfr_t()...);
    } else {
        if (r_prec > p.second) {
            // The precision of the return value is larger than the largest precision
            // among the operands. We can reset its precision destructively
            // because we know it does not overlap with any operand.
            rop.set_prec_impl<false>(p.second);
            mpfr_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, rop._get_mpfr_t(), arg0.get_mpfr_t(),
                                   args.get_mpfr_t()...);
        } else if (!p.first || p.first->get_prec() != p.second) {
            // This covers 2 cases:
            // - the precision of the return value is smaller than the largest precision
            //   among the operands and we cannot steal from any argument,
            // - the precision of the return value is smaller than the largest precision
            //   among the operands, we can steal from an argument but the target argument
            //   does not have enough precision.
            // In these cases, we will just set the precision of rop and call the function.
            // NOTE: we need to set the precision without destroying the rop, as it might
            // overlap with one of the arguments. Since this will be an increase in precision,
            // it should not entail a rounding operation.
            // NOTE: we assume all the precs in the operands are valid, so we will not need
            // to check them.
            rop.prec_round_impl<false>(p.second);
            mpfr_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, rop._get_mpfr_t(), arg0.get_mpfr_t(),
                                   args.get_mpfr_t()...);
        } else {
            // The precision of the return value is smaller than the largest precision among the operands,
            // and we have a candidate for stealing with enough precision: we will use it as return
            // value and then swap out the result to rop.
            mpfr_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, p.first->_get_mpfr_t(), arg0.get_mpfr_t(),
                                   args.get_mpfr_t()...);
            swap(*p.first, rop);
        }
    }
    return rop;
}

// The two overloads that will actually be used in the code: one adds the rounding mode
// as final argument, the other does not.
template <typename F, typename Arg0, typename... Args>
inline real &mpfr_nary_op(::mpfr_prec_t min_prec, const F &f, real &rop, Arg0 &&arg0, Args &&... args)
{
    return mpfr_nary_op_impl<true>(min_prec, f, rop, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
}

template <typename F, typename Arg0, typename... Args>
inline real &mpfr_nary_op_nornd(::mpfr_prec_t min_prec, const F &f, real &rop, Arg0 &&arg0, Args &&... args)
{
    return mpfr_nary_op_impl<false>(min_prec, f, rop, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
}

// Invoke an MPFR function with arguments (arg0, args...), and store the result
// in a value to be created by this function. If possible, this function will try
// to re-use the storage provided by the input arguments, if one or more of these
// arguments are rvalue references and their precision is large enough. As usual,
// the precision of the return value will be the max precision among the operands,
// but not less than min_prec.
// The Rnd flag controls whether to add the rounding mode (MPFR_RNDN) at the end
// of the function arguments list or not.
template <bool Rnd, typename F, typename Arg0, typename... Args>
inline real mpfr_nary_op_return_impl(::mpfr_prec_t min_prec, const F &f, Arg0 &&arg0, Args &&... args)
{
    auto p = mpfr_nary_op_init_pair(min_prec, std::forward<Arg0>(arg0));
    mpfr_nary_op_check_steal(p, std::forward<Args>(args)...);
    if (p.first && p.first->get_prec() == p.second) {
        // There's at least one arg we can steal from, and its precision is large enough
        // to contain the result. Use it.
        mpfr_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, p.first->_get_mpfr_t(), arg0.get_mpfr_t(),
                               args.get_mpfr_t()...);
        return std::move(*p.first);
    }
    // Either we cannot steal from any arg, or the candidate(s) do not have enough precision.
    // Init a new value and use it instead.
    real retval{real::ptag{}, p.second, true};
    mpfr_nary_func_wrapper(std::integral_constant<bool, Rnd>{}, f, retval._get_mpfr_t(), arg0.get_mpfr_t(),
                           args.get_mpfr_t()...);
    return retval;
}

// The two overloads that will actually be used in the code: one adds the rounding mode
// as final argument, the other does not.
template <typename F, typename Arg0, typename... Args>
inline real mpfr_nary_op_return(::mpfr_prec_t min_prec, const F &f, Arg0 &&arg0, Args &&... args)
{
    return mpfr_nary_op_return_impl<true>(min_prec, f, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
}

template <typename F, typename Arg0, typename... Args>
inline real mpfr_nary_op_return_nornd(::mpfr_prec_t min_prec, const F &f, Arg0 &&arg0, Args &&... args)
{
    return mpfr_nary_op_return_impl<false>(min_prec, f, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
}

#endif
} // namespace detail

// These are helper macros to reduce typing when dealing with the common case
// of exposing MPFR functions with a single argument (both variants with retval
// and with return).
#define MPPP_REAL_MPFR_UNARY_RETVAL_IMPL(fname)                                                                        \
    inline real &fname(real &rop, T &&op)                                                                              \
    {                                                                                                                  \
        return detail::mpfr_nary_op(0, ::mpfr_##fname, rop, std::forward<T>(op));                                      \
    }

#define MPPP_REAL_MPFR_UNARY_RETURN_IMPL(fname)                                                                        \
    inline real fname(T &&r)                                                                                           \
    {                                                                                                                  \
        return detail::mpfr_nary_op_return(0, ::mpfr_##fname, std::forward<T>(r));                                     \
    }

#if defined(MPPP_HAVE_CONCEPTS)
#define MPPP_REAL_MPFR_UNARY_HEADER template <CvrReal T>
#else
#define MPPP_REAL_MPFR_UNARY_HEADER template <typename T, cvr_real_enabler<T> = 0>
#endif

#define MPPP_REAL_MPFR_UNARY_RETVAL(fname) MPPP_REAL_MPFR_UNARY_HEADER MPPP_REAL_MPFR_UNARY_RETVAL_IMPL(fname)
#define MPPP_REAL_MPFR_UNARY_RETURN(fname) MPPP_REAL_MPFR_UNARY_HEADER MPPP_REAL_MPFR_UNARY_RETURN_IMPL(fname)

/** @defgroup real_arithmetic real_arithmetic
 *  @{
 */

/// Ternary \link mppp::real real\endlink addition.
/**
 * This function will compute \f$a+b\f$, storing the result in \p rop.
 * The precision of the result will be set to the largest precision among the operands.
 *
 * @param rop the return value.
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return a reference to \p rop.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U>
#else
template <typename T, typename U, cvr_real_enabler<T, U> = 0>
#endif
inline real &add(real &rop, T &&a, U &&b)
{
    return detail::mpfr_nary_op(0, ::mpfr_add, rop, std::forward<T>(a), std::forward<U>(b));
}

/// Ternary \link mppp::real real\endlink subtraction.
/**
 * This function will compute \f$a-b\f$, storing the result in \p rop.
 * The precision of the result will be set to the largest precision among the operands.
 *
 * @param rop the return value.
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return a reference to \p rop.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U>
#else
template <typename T, typename U, cvr_real_enabler<T, U> = 0>
#endif
inline real &sub(real &rop, T &&a, U &&b)
{
    return detail::mpfr_nary_op(0, ::mpfr_sub, rop, std::forward<T>(a), std::forward<U>(b));
}

/// Ternary \link mppp::real real\endlink multiplication.
/**
 * This function will compute \f$a \times b\f$, storing the result in \p rop.
 * The precision of the result will be set to the largest precision among the operands.
 *
 * @param rop the return value.
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return a reference to \p rop.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U>
#else
template <typename T, typename U, cvr_real_enabler<T, U> = 0>
#endif
inline real &mul(real &rop, T &&a, U &&b)
{
    return detail::mpfr_nary_op(0, ::mpfr_mul, rop, std::forward<T>(a), std::forward<U>(b));
}

/// Ternary \link mppp::real real\endlink division.
/**
 * This function will compute \f$a / b\f$, storing the result in \p rop.
 * The precision of the result will be set to the largest precision among the operands.
 *
 * @param rop the return value.
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return a reference to \p rop.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U>
#else
template <typename T, typename U, cvr_real_enabler<T, U> = 0>
#endif
inline real &div(real &rop, T &&a, U &&b)
{
    return detail::mpfr_nary_op(0, ::mpfr_div, rop, std::forward<T>(a), std::forward<U>(b));
}

/// Quaternary \link mppp::real real\endlink fused multiply–add.
/**
 * This function will compute \f$a \times b + c\f$, storing the result in \p rop.
 * The precision of the result will be set to the largest precision among the operands.
 *
 * @param rop the return value.
 * @param a the first operand.
 * @param b the second operand.
 * @param c the third operand.
 *
 * @return a reference to \p rop.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U, CvrReal V>
#else
template <typename T, typename U, typename V, cvr_real_enabler<T, U, V> = 0>
#endif
inline real &fma(real &rop, T &&a, U &&b, V &&c)
{
    return detail::mpfr_nary_op(0, ::mpfr_fma, rop, std::forward<T>(a), std::forward<U>(b), std::forward<V>(c));
}

/// Ternary \link mppp::real real\endlink fused multiply–add.
/**
 * \rststar
 * This function will compute and return :math:`a \times b + c`.
 * The precision of the result will be set to the largest precision among the operands.
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 * @param c the third operand.
 *
 * @return \f$ a \times b + c \f$.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U, CvrReal V>
#else
template <typename T, typename U, typename V, cvr_real_enabler<T, U, V> = 0>
#endif
inline real fma(T &&a, U &&b, V &&c)
{
    return detail::mpfr_nary_op_return(0, ::mpfr_fma, std::forward<T>(a), std::forward<U>(b), std::forward<V>(c));
}

/// Quaternary \link mppp::real real\endlink fused multiply–sub.
/**
 * This function will compute \f$a \times b - c\f$, storing the result in \p rop.
 * The precision of the result will be set to the largest precision among the operands.
 *
 * @param rop the return value.
 * @param a the first operand.
 * @param b the second operand.
 * @param c the third operand.
 *
 * @return a reference to \p rop.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U, CvrReal V>
#else
template <typename T, typename U, typename V, cvr_real_enabler<T, U, V> = 0>
#endif
inline real &fms(real &rop, T &&a, U &&b, V &&c)
{
    return detail::mpfr_nary_op(0, ::mpfr_fms, rop, std::forward<T>(a), std::forward<U>(b), std::forward<V>(c));
}

/// Ternary \link mppp::real real\endlink fused multiply–sub.
/**
 * \rststar
 * This function will compute and return :math:`a \times b - c`.
 * The precision of the result will be set to the largest precision among the operands.
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 * @param c the third operand.
 *
 * @return \f$ a \times b - c \f$.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U, CvrReal V>
#else
template <typename T, typename U, typename V, cvr_real_enabler<T, U, V> = 0>
#endif
inline real fms(T &&a, U &&b, V &&c)
{
    return detail::mpfr_nary_op_return(0, ::mpfr_fms, std::forward<T>(a), std::forward<U>(b), std::forward<V>(c));
}

/// Unary negation for \link mppp::real real\endlink.
/**
 * @param x the \link mppp::real real\endlink that will be negated.
 *
 * @return the negative of \p x.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real neg(T &&x)
{
    return detail::mpfr_nary_op_return(0, ::mpfr_neg, std::forward<T>(x));
}

/// Binary negation for \link mppp::real real\endlink.
/**
 * This function will set \p rop to the negation of \p x.
 *
 * @param rop the \link mppp::real real\endlink that will hold the result.
 * @param x the \link mppp::real real\endlink that will be negated.
 *
 * @return a reference to \p rop.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real neg(real &rop, T &&x)
{
    return detail::mpfr_nary_op(0, ::mpfr_neg, rop, std::forward<T>(x));
}

/// Unary absolute value for \link mppp::real real\endlink.
/**
 * @param x the \link mppp::real real\endlink whose absolute value will be computed.
 *
 * @return the absolute value of \p x.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real abs(T &&x)
{
    return detail::mpfr_nary_op_return(0, ::mpfr_abs, std::forward<T>(x));
}

/// Binary absolute value for \link mppp::real real\endlink.
/**
 * This function will set \p rop to the absolute value of \p x.
 *
 * @param rop the \link mppp::real real\endlink that will hold the result.
 * @param x the \link mppp::real real\endlink whose absolute value will be computed.
 *
 * @return a reference to \p rop.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real abs(real &rop, T &&x)
{
    return detail::mpfr_nary_op(0, ::mpfr_abs, rop, std::forward<T>(x));
}

/** @} */

/** @defgroup real_comparison real_comparison
 *  @{
 */

/// Detect if a \link mppp::real real\endlink is NaN.
/**
 * @param r the \link mppp::real real\endlink that will be examined.
 *
 * @return \p true if \p r is NaN, \p false otherwise.
 */
inline bool nan_p(const real &r)
{
    return r.nan_p();
}

/// Detect if a \link mppp::real real\endlink is infinity.
/**
 * @param r the \link mppp::real real\endlink that will be examined.
 *
 * @return \p true if \p r is an infinity, \p false otherwise.
 */
inline bool inf_p(const real &r)
{
    return r.inf_p();
}

/// Detect if \link mppp::real real\endlink is a finite number.
/**
 * @param r the \link mppp::real real\endlink that will be examined.
 *
 * @return \p true if \p r is a finite number (i.e., not NaN or infinity), \p false otherwise.
 */
inline bool number_p(const real &r)
{
    return r.number_p();
}

/// Detect if a \link mppp::real real\endlink is zero.
/**
 * @param r the \link mppp::real real\endlink that will be examined.
 *
 * @return \p true if \p r is zero, \p false otherwise.
 */
inline bool zero_p(const real &r)
{
    return r.zero_p();
}

/// Detect if a \link mppp::real real\endlink is a regular number.
/**
 * @param r the \link mppp::real real\endlink that will be examined.
 *
 * @return \p true if \p r is a regular number (i.e., not NaN, infinity or zero), \p false otherwise.
 */
inline bool regular_p(const real &r)
{
    return r.regular_p();
}

/// Detect if a \link mppp::real real\endlink is one.
/**
 * @param r the \link mppp::real real\endlink to be checked.
 *
 * @return \p true if \p r is exactly 1, \p false otherwise.
 */
inline bool is_one(const real &r)
{
    return r.is_one();
}

/// Detect the sign of a \link mppp::real real\endlink.
/**
 * @param r the \link mppp::real real\endlink that will be examined.
 *
 * @return a positive value if \p r is positive, zero if \p r is zero, a negative value if \p this
 * is negative.
 *
 * @throws unspecified any exception thrown by mppp::real::sgn().
 */
inline int sgn(const real &r)
{
    return r.sgn();
}

/// Get the sign bit of a \link mppp::real real\endlink.
/**
 * The sign bit is set if ``r`` is negative, -0, or a NaN whose representation has its sign bit set.
 *
 * @param r the \link mppp::real real\endlink that will be examined.
 *
 * @return the sign bit of \p r.
 */
inline bool signbit(const real &r)
{
    return r.signbit();
}

/// \link mppp::real Real\endlink comparison.
/**
 * \rststar
 * This function will compare ``a`` and ``b``, returning:
 *
 * - zero if ``a`` equals ``b``,
 * - a negative value if ``a`` is less than ``b``,
 * - a positive value if ``a`` is greater than ``b``.
 *
 * If at least one NaN value is involved in the comparison, an error will be raised.
 *
 * This function is useful to distinguish the three possible cases. The comparison operators
 * are recommended instead if it is needed to distinguish only two cases.
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return an integral value expressing how ``a`` compares to ``b``.
 *
 * @throws std::domain_error if at least one of the operands is NaN.
 */
inline int cmp(const real &a, const real &b)
{
    ::mpfr_clear_erangeflag();
    auto retval = ::mpfr_cmp(a.get_mpfr_t(), b.get_mpfr_t());
    if (mppp_unlikely(::mpfr_erangeflag_p())) {
        ::mpfr_clear_erangeflag();
        throw std::domain_error("Cannot compare two reals if at least one of them is NaN");
    }
    return retval;
}

/// Equality predicate with special NaN handling for \link mppp::real real\endlink.
/**
 * \rststar
 * If both ``a`` and ``b`` are not NaN, this function is identical to the equality operator for
 * :cpp:class:`~mppp::real`. If at least one operand is NaN, this function will return ``true``
 * if both operands are NaN, ``false`` otherwise.
 *
 * In other words, this function behaves like an equality operator which considers all NaN
 * values equal to each other.
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return \p true if \f$ a = b \f$ (including the case in which both operands are NaN),
 * \p false otherwise.
 */
inline bool real_equal_to(const real &a, const real &b)
{
    const bool a_nan = a.nan_p(), b_nan = b.nan_p();
    return (!a_nan && !b_nan) ? (::mpfr_equal_p(a.get_mpfr_t(), b.get_mpfr_t()) != 0) : (a_nan && b_nan);
}

/// Less-than predicate with special NaN and moved-from handling for \link mppp::real real\endlink.
/**
 * \rststar
 * This function behaves like a less-than operator which considers NaN values
 * greater than non-NaN values, and moved-from objects greater than both NaN and non-NaN values.
 * This function can be used as a comparator in various facilities of the
 * standard library (e.g., ``std::sort()``, ``std::set``, etc.).
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return \p true if \f$ a < b \f$ (following the rules above regarding NaN values and moved-from objects),
 * \p false otherwise.
 */
inline bool real_lt(const real &a, const real &b)
{
    if (!a.get_mpfr_t()->_mpfr_d) {
        // a is moved-from, consider it the largest possible value.
        return false;
    }
    if (!b.get_mpfr_t()->_mpfr_d) {
        // a is not moved-from, b is. a is smaller.
        return true;
    }
    const bool a_nan = a.nan_p();
    return (!a_nan && !b.nan_p()) ? (::mpfr_less_p(a.get_mpfr_t(), b.get_mpfr_t()) != 0) : !a_nan;
}

/// Greater-than predicate with special NaN and moved-from handling for \link mppp::real real\endlink.
/**
 * \rststar
 * This function behaves like a greater-than operator which considers NaN values
 * greater than non-NaN values, and moved-from objects greater than both NaN and non-NaN values.
 * This function can be used as a comparator in various facilities of the
 * standard library (e.g., ``std::sort()``, ``std::set``, etc.).
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return \p true if \f$ a > b \f$ (following the rules above regarding NaN values and moved-from objects),
 * \p false otherwise.
 */
inline bool real_gt(const real &a, const real &b)
{
    if (!b.get_mpfr_t()->_mpfr_d) {
        // b is moved-from, nothing can be bigger.
        return false;
    }
    if (!a.get_mpfr_t()->_mpfr_d) {
        // b is not moved-from, a is. a is bigger.
        return true;
    }
    const bool b_nan = b.nan_p();
    return (!a.nan_p() && !b_nan) ? (::mpfr_greater_p(a.get_mpfr_t(), b.get_mpfr_t()) != 0) : !b_nan;
}

/** @} */

// Square root.
MPPP_REAL_MPFR_UNARY_RETVAL(sqrt)
MPPP_REAL_MPFR_UNARY_RETURN(sqrt)

// Reciprocal square root.
MPPP_REAL_MPFR_UNARY_RETVAL(rec_sqrt)
MPPP_REAL_MPFR_UNARY_RETURN(rec_sqrt)

// Cubic root.
MPPP_REAL_MPFR_UNARY_RETVAL(cbrt)
MPPP_REAL_MPFR_UNARY_RETURN(cbrt)

#if MPFR_VERSION_MAJOR >= 4

// K-th root.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real &rootn_ui(real &rop, T &&op, unsigned long k)
{
    auto rootn_ui_wrapper = [k](::mpfr_t r, const ::mpfr_t o, ::mpfr_rnd_t rnd) { ::mpfr_rootn_ui(r, o, k, rnd); };
    return detail::mpfr_nary_op(0, rootn_ui_wrapper, rop, std::forward<T>(op));
}

#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real rootn_ui(T &&r, unsigned long k)
{
    auto rootn_ui_wrapper
        = [k](::mpfr_t rop, const ::mpfr_t op, ::mpfr_rnd_t rnd) { ::mpfr_rootn_ui(rop, op, k, rnd); };
    return detail::mpfr_nary_op_return(0, rootn_ui_wrapper, std::forward<T>(r));
}

#endif

/** @defgroup real_exponentiation real_exponentiation
 *  @{
 */

/// Ternary \link mppp::real real\endlink exponentiation.
/**
 * This function will set \p rop to \p op1 raised to the power of \p op2.
 * The precision of \p rop will be set to the largest precision among the operands.
 *
 * @param rop the return value.
 * @param op1 the base.
 * @param op2 the exponent.
 *
 * @return a reference to \p rop.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U>
#else
template <typename T, typename U, cvr_real_enabler<T, U> = 0>
#endif
inline real &pow(real &rop, T &&op1, U &&op2)
{
    return detail::mpfr_nary_op(0, ::mpfr_pow, rop, std::forward<T>(op1), std::forward<U>(op2));
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_pow(T &&op1, U &&op2)
{
    return detail::mpfr_nary_op_return(0, ::mpfr_pow, std::forward<T>(op1), std::forward<U>(op2));
}

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, is_real_interoperable<U>>::value, int> = 0>
inline real dispatch_pow(T &&a, const U &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_pow(std::forward<T>(a), tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<is_real_interoperable<T>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_pow(const T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_pow(tmp, std::forward<U>(a));
}

} // namespace detail

/// Binary \link mppp::real real\endlink exponentiation.
/**
 * \rststar
 * The precision of the result will be set to the largest precision among the operands.
 *
 * Non-:cpp:class:`~mppp::real` operands will be converted to :cpp:class:`~mppp::real`
 * before performing the operation. The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 * \endrststar
 *
 * @param op1 the base.
 * @param op2 the exponent.
 *
 * @return \p op1 raised to the power of \p op2.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealOpTypes<T, U> inline real pow(T &&op1, U &&op2)
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
inline real pow(T &&op1, U &&op2)
#endif
{
    return detail::dispatch_pow(std::forward<T>(op1), std::forward<U>(op2));
}

/** @} */

// Trigonometric functions.

MPPP_REAL_MPFR_UNARY_RETVAL(sin)
MPPP_REAL_MPFR_UNARY_RETURN(sin)

MPPP_REAL_MPFR_UNARY_RETVAL(cos)
MPPP_REAL_MPFR_UNARY_RETURN(cos)

MPPP_REAL_MPFR_UNARY_RETVAL(tan)
MPPP_REAL_MPFR_UNARY_RETURN(tan)

MPPP_REAL_MPFR_UNARY_RETVAL(sec)
MPPP_REAL_MPFR_UNARY_RETURN(sec)

MPPP_REAL_MPFR_UNARY_RETVAL(csc)
MPPP_REAL_MPFR_UNARY_RETURN(csc)

MPPP_REAL_MPFR_UNARY_RETVAL(cot)
MPPP_REAL_MPFR_UNARY_RETURN(cot)

MPPP_REAL_MPFR_UNARY_RETVAL(asin)
MPPP_REAL_MPFR_UNARY_RETURN(asin)

MPPP_REAL_MPFR_UNARY_RETVAL(acos)
MPPP_REAL_MPFR_UNARY_RETURN(acos)

MPPP_REAL_MPFR_UNARY_RETVAL(atan)
MPPP_REAL_MPFR_UNARY_RETURN(atan)

// sin and cos at the same time.
// NOTE: we don't have the machinery to steal resources
// for multiple retvals, thus we do a manual implementation
// of this function. We keep the signature with CvrReal
// for consistency with the other functions.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline void sin_cos(real &sop, real &cop, T &&op)
{
    if (mppp_unlikely(&sop == &cop)) {
        throw std::invalid_argument(
            "In the real sin_cos() function, the return values 'sop' and 'cop' must be distinct objects");
    }

    // Set the precision of sop and cop to the
    // precision of op.
    const auto op_prec = op.get_prec();
    // NOTE: use prec_round() to avoid issues in case
    // sop/cop overlap with op.
    sop.prec_round(op_prec);
    cop.prec_round(op_prec);

    // Run the mpfr function.
    ::mpfr_sin_cos(sop._get_mpfr_t(), cop._get_mpfr_t(), op.get_mpfr_t(), MPFR_RNDN);
}

// Ternary atan2.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U>
#else
template <typename T, typename U, cvr_real_enabler<T, U> = 0>
#endif
inline real &atan2(real &rop, T &&y, U &&x)
{
    return detail::mpfr_nary_op(0, ::mpfr_atan2, rop, std::forward<T>(y), std::forward<U>(x));
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_atan2(T &&y, U &&x)
{
    return mpfr_nary_op_return(0, ::mpfr_atan2, std::forward<T>(y), std::forward<U>(x));
}

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, is_real_interoperable<U>>::value, int> = 0>
inline real dispatch_atan2(T &&a, const U &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_atan2(std::forward<T>(a), tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<is_real_interoperable<T>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_atan2(const T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_atan2(tmp, std::forward<U>(a));
}

} // namespace detail

// Binary atan2.
#if defined(MPPP_HAVE_CONCEPTS)
// NOTE: written like this, the constraint is equivalent
// to: requires RealOpTypes<U, T>.
template <typename T, RealOpTypes<T> U>
#else
// NOTE: we flip around T and U in the enabler to keep
// it consistent with the concept above.
template <typename T, typename U, real_op_types_enabler<U, T> = 0>
#endif
inline real atan2(T &&y, U &&x)
{
    return detail::dispatch_atan2(std::forward<T>(y), std::forward<U>(x));
}

// Hyperbolic functions.

MPPP_REAL_MPFR_UNARY_RETVAL(sinh)
MPPP_REAL_MPFR_UNARY_RETURN(sinh)

MPPP_REAL_MPFR_UNARY_RETVAL(cosh)
MPPP_REAL_MPFR_UNARY_RETURN(cosh)

MPPP_REAL_MPFR_UNARY_RETVAL(tanh)
MPPP_REAL_MPFR_UNARY_RETURN(tanh)

MPPP_REAL_MPFR_UNARY_RETVAL(sech)
MPPP_REAL_MPFR_UNARY_RETURN(sech)

MPPP_REAL_MPFR_UNARY_RETVAL(csch)
MPPP_REAL_MPFR_UNARY_RETURN(csch)

MPPP_REAL_MPFR_UNARY_RETVAL(coth)
MPPP_REAL_MPFR_UNARY_RETURN(coth)

MPPP_REAL_MPFR_UNARY_RETVAL(asinh)
MPPP_REAL_MPFR_UNARY_RETURN(asinh)

MPPP_REAL_MPFR_UNARY_RETVAL(acosh)
MPPP_REAL_MPFR_UNARY_RETURN(acosh)

MPPP_REAL_MPFR_UNARY_RETVAL(atanh)
MPPP_REAL_MPFR_UNARY_RETURN(atanh)

// sinh and cosh at the same time.
// NOTE: we don't have the machinery to steal resources
// for multiple retvals, thus we do a manual implementation
// of this function. We keep the signature with CvrReal
// for consistency with the other functions.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline void sinh_cosh(real &sop, real &cop, T &&op)
{
    if (mppp_unlikely(&sop == &cop)) {
        throw std::invalid_argument(
            "In the real sinh_cosh() function, the return values 'sop' and 'cop' must be distinct objects");
    }

    // Set the precision of sop and cop to the
    // precision of op.
    const auto op_prec = op.get_prec();
    // NOTE: use prec_round() to avoid issues in case
    // sop/cop overlap with op.
    sop.prec_round(op_prec);
    cop.prec_round(op_prec);

    // Run the mpfr function.
    ::mpfr_sinh_cosh(sop._get_mpfr_t(), cop._get_mpfr_t(), op.get_mpfr_t(), MPFR_RNDN);
}

// Exponentials and logarithms.

MPPP_REAL_MPFR_UNARY_RETVAL(exp)
MPPP_REAL_MPFR_UNARY_RETURN(exp)

MPPP_REAL_MPFR_UNARY_RETVAL(exp2)
MPPP_REAL_MPFR_UNARY_RETURN(exp2)

MPPP_REAL_MPFR_UNARY_RETVAL(exp10)
MPPP_REAL_MPFR_UNARY_RETURN(exp10)

MPPP_REAL_MPFR_UNARY_RETVAL(expm1)
MPPP_REAL_MPFR_UNARY_RETURN(expm1)

MPPP_REAL_MPFR_UNARY_RETVAL(log)
MPPP_REAL_MPFR_UNARY_RETURN(log)

MPPP_REAL_MPFR_UNARY_RETVAL(log2)
MPPP_REAL_MPFR_UNARY_RETURN(log2)

MPPP_REAL_MPFR_UNARY_RETVAL(log10)
MPPP_REAL_MPFR_UNARY_RETURN(log10)

MPPP_REAL_MPFR_UNARY_RETVAL(log1p)
MPPP_REAL_MPFR_UNARY_RETURN(log1p)

// Gamma functions.

MPPP_REAL_MPFR_UNARY_RETVAL(gamma)
MPPP_REAL_MPFR_UNARY_RETURN(gamma)

MPPP_REAL_MPFR_UNARY_RETVAL(lngamma)
MPPP_REAL_MPFR_UNARY_RETURN(lngamma)

// NOTE: for lgamma we have to spell out the full implementations,
// since we are using a wrapper internally.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real &lgamma(real &rop, T &&op)
{
    return detail::mpfr_nary_op(0, detail::real_lgamma_wrapper, rop, std::forward<T>(op));
}

#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real lgamma(T &&r)
{
    return detail::mpfr_nary_op_return(0, detail::real_lgamma_wrapper, std::forward<T>(r));
}

MPPP_REAL_MPFR_UNARY_RETVAL(digamma)
MPPP_REAL_MPFR_UNARY_RETURN(digamma)

#if MPFR_VERSION_MAJOR >= 4

// Ternary gamma_inc.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U>
#else
template <typename T, typename U, cvr_real_enabler<T, U> = 0>
#endif
inline real &gamma_inc(real &rop, T &&x, U &&y)
{
    return detail::mpfr_nary_op(0, ::mpfr_gamma_inc, rop, std::forward<T>(x), std::forward<U>(y));
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_gamma_inc(T &&x, U &&y)
{
    return mpfr_nary_op_return(0, ::mpfr_gamma_inc, std::forward<T>(x), std::forward<U>(y));
}

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, is_real_interoperable<U>>::value, int> = 0>
inline real dispatch_gamma_inc(T &&a, const U &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_gamma_inc(std::forward<T>(a), tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<is_real_interoperable<T>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_gamma_inc(const T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_gamma_inc(tmp, std::forward<U>(a));
}

} // namespace detail

// Binary gamma_inc.
#if defined(MPPP_HAVE_CONCEPTS)
// NOTE: written like this, the constraint is equivalent
// to: requires RealOpTypes<U, T>.
template <typename T, RealOpTypes<T> U>
#else
// NOTE: we flip around T and U in the enabler to keep
// it consistent with the concept above.
template <typename T, typename U, real_op_types_enabler<U, T> = 0>
#endif
inline real gamma_inc(T &&x, U &&y)
{
    return detail::dispatch_gamma_inc(std::forward<T>(x), std::forward<U>(y));
}

#endif

// Bessel functions.

MPPP_REAL_MPFR_UNARY_RETVAL(j0)
MPPP_REAL_MPFR_UNARY_RETURN(j0)

MPPP_REAL_MPFR_UNARY_RETVAL(j1)
MPPP_REAL_MPFR_UNARY_RETURN(j1)

// Bessel function of the first kind of order n.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real &jn(real &rop, long n, T &&op)
{
    auto jn_wrapper = [n](::mpfr_t r, const ::mpfr_t o, ::mpfr_rnd_t rnd) { ::mpfr_jn(r, n, o, rnd); };
    return detail::mpfr_nary_op(0, jn_wrapper, rop, std::forward<T>(op));
}

#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real jn(long n, T &&r)
{
    auto jn_wrapper = [n](::mpfr_t rop, const ::mpfr_t op, ::mpfr_rnd_t rnd) { ::mpfr_jn(rop, n, op, rnd); };
    return detail::mpfr_nary_op_return(0, jn_wrapper, std::forward<T>(r));
}

MPPP_REAL_MPFR_UNARY_RETVAL(y0)
MPPP_REAL_MPFR_UNARY_RETURN(y0)

MPPP_REAL_MPFR_UNARY_RETVAL(y1)
MPPP_REAL_MPFR_UNARY_RETURN(y1)

// Bessel function of the second kind of order n.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real &yn(real &rop, long n, T &&op)
{
    auto yn_wrapper = [n](::mpfr_t r, const ::mpfr_t o, ::mpfr_rnd_t rnd) { ::mpfr_yn(r, n, o, rnd); };
    return detail::mpfr_nary_op(0, yn_wrapper, rop, std::forward<T>(op));
}

#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real yn(long n, T &&r)
{
    auto yn_wrapper = [n](::mpfr_t rop, const ::mpfr_t op, ::mpfr_rnd_t rnd) { ::mpfr_yn(rop, n, op, rnd); };
    return detail::mpfr_nary_op_return(0, yn_wrapper, std::forward<T>(r));
}

// Other special functions.
MPPP_REAL_MPFR_UNARY_RETVAL(eint)
MPPP_REAL_MPFR_UNARY_RETURN(eint)

MPPP_REAL_MPFR_UNARY_RETVAL(li2)
MPPP_REAL_MPFR_UNARY_RETURN(li2)

MPPP_REAL_MPFR_UNARY_RETVAL(zeta)
MPPP_REAL_MPFR_UNARY_RETURN(zeta)

MPPP_REAL_MPFR_UNARY_RETVAL(erf)
MPPP_REAL_MPFR_UNARY_RETURN(erf)

MPPP_REAL_MPFR_UNARY_RETVAL(erfc)
MPPP_REAL_MPFR_UNARY_RETURN(erfc)

MPPP_REAL_MPFR_UNARY_RETVAL(ai)
MPPP_REAL_MPFR_UNARY_RETURN(ai)

#if MPFR_VERSION_MAJOR >= 4

// Ternary beta.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U>
#else
template <typename T, typename U, cvr_real_enabler<T, U> = 0>
#endif
inline real &beta(real &rop, T &&x, U &&y)
{
    return detail::mpfr_nary_op(0, ::mpfr_beta, rop, std::forward<T>(x), std::forward<U>(y));
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_beta(T &&x, U &&y)
{
    return mpfr_nary_op_return(0, ::mpfr_beta, std::forward<T>(x), std::forward<U>(y));
}

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, is_real_interoperable<U>>::value, int> = 0>
inline real dispatch_beta(T &&a, const U &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_beta(std::forward<T>(a), tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<is_real_interoperable<T>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_beta(const T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_beta(tmp, std::forward<U>(a));
}

} // namespace detail

// Binary beta.
#if defined(MPPP_HAVE_CONCEPTS)
// NOTE: written like this, the constraint is equivalent
// to: requires RealOpTypes<U, T>.
template <typename T, RealOpTypes<T> U>
#else
// NOTE: we flip around T and U in the enabler to keep
// it consistent with the concept above.
template <typename T, typename U, real_op_types_enabler<U, T> = 0>
#endif
inline real beta(T &&x, U &&y)
{
    return detail::dispatch_beta(std::forward<T>(x), std::forward<U>(y));
}

#endif

// Ternary hypot.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U>
#else
template <typename T, typename U, cvr_real_enabler<T, U> = 0>
#endif
inline real &hypot(real &rop, T &&x, U &&y)
{
    return detail::mpfr_nary_op(0, ::mpfr_hypot, rop, std::forward<T>(x), std::forward<U>(y));
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_hypot(T &&x, U &&y)
{
    return mpfr_nary_op_return(0, ::mpfr_hypot, std::forward<T>(x), std::forward<U>(y));
}

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, is_real_interoperable<U>>::value, int> = 0>
inline real dispatch_hypot(T &&a, const U &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_hypot(std::forward<T>(a), tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<is_real_interoperable<T>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_hypot(const T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_hypot(tmp, std::forward<U>(a));
}

} // namespace detail

// Binary hypot.
#if defined(MPPP_HAVE_CONCEPTS)
// NOTE: written like this, the constraint is equivalent
// to: requires RealOpTypes<U, T>.
template <typename T, RealOpTypes<T> U>
#else
// NOTE: we flip around T and U in the enabler to keep
// it consistent with the concept above.
template <typename T, typename U, real_op_types_enabler<U, T> = 0>
#endif
inline real hypot(T &&x, U &&y)
{
    return detail::dispatch_hypot(std::forward<T>(x), std::forward<U>(y));
}

// Ternary agm.
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T, CvrReal U>
#else
template <typename T, typename U, cvr_real_enabler<T, U> = 0>
#endif
inline real &agm(real &rop, T &&x, U &&y)
{
    return detail::mpfr_nary_op(0, ::mpfr_agm, rop, std::forward<T>(x), std::forward<U>(y));
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_agm(T &&x, U &&y)
{
    return mpfr_nary_op_return(0, ::mpfr_agm, std::forward<T>(x), std::forward<U>(y));
}

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, is_real_interoperable<U>>::value, int> = 0>
inline real dispatch_agm(T &&a, const U &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_agm(std::forward<T>(a), tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<is_real_interoperable<T>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_agm(const T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_agm(tmp, std::forward<U>(a));
}

} // namespace detail

// Binary agm.
#if defined(MPPP_HAVE_CONCEPTS)
// NOTE: written like this, the constraint is equivalent
// to: requires RealOpTypes<U, T>.
template <typename T, RealOpTypes<T> U>
#else
// NOTE: we flip around T and U in the enabler to keep
// it consistent with the concept above.
template <typename T, typename U, real_op_types_enabler<U, T> = 0>
#endif
inline real agm(T &&x, U &&y)
{
    return detail::dispatch_agm(std::forward<T>(x), std::forward<U>(y));
}

#undef MPPP_REAL_MPFR_UNARY_RETURN
#undef MPPP_REAL_MPFR_UNARY_RETVAL
#undef MPPP_REAL_MPFR_UNARY_HEADER
#undef MPPP_REAL_MPFR_UNARY_RETURN_IMPL
#undef MPPP_REAL_MPFR_UNARY_RETVAL_IMPL

/** @defgroup real_io real_io
 *  @{
 */

/// Output stream operator for \link mppp::real real\endlink objects.
/**
 * \rststar
 * This operator will insert into the stream ``os`` a string representation of ``r``
 * in base 10 (as returned by :cpp:func:`mppp::real::to_string()`).
 *
 * .. warning::
 *    In future versions of mp++, the behaviour of this operator will change to support the output stream's formatting
 *    flags. For the time being, users are encouraged to use the ``mpfr_get_str()`` function from the MPFR
 *    library if precise and forward-compatible control on the printing format is needed.
 *
 * \endrststar
 *
 * @param os the target stream.
 * @param r the \link mppp::real real\endlink that will be directed to \p os.
 *
 * @return a reference to \p os.
 *
 * @throws unspecified any exception thrown by mppp::real::to_string().
 */
inline std::ostream &operator<<(std::ostream &os, const real &r)
{
    detail::mpfr_to_stream(r.get_mpfr_t(), os, 10);
    return os;
}

/** @} */

namespace detail
{

template <typename F>
inline real real_constant(const F &f, ::mpfr_prec_t p)
{
    ::mpfr_prec_t prec;
    if (p) {
        if (mppp_unlikely(!real_prec_check(p))) {
            throw std::invalid_argument("Cannot init a real constant with a precision of " + detail::to_string(p)
                                        + ": the value must be either zero or between "
                                        + detail::to_string(real_prec_min()) + " and "
                                        + detail::to_string(real_prec_max()));
        }
        prec = p;
    } else {
        const auto dp = real_get_default_prec();
        if (mppp_unlikely(!dp)) {
            throw std::invalid_argument("Cannot init a real constant with an automatically-deduced precision if "
                                        "the global default precision has not been set");
        }
        prec = dp;
    }
    real retval{real::ptag{}, prec, true};
    f(retval._get_mpfr_t(), MPFR_RNDN);
    return retval;
}
} // namespace detail

/** @defgroup real_constants real_constants
 *  @{
 */

/// \link mppp::real Real\endlink \f$\pi\f$ constant.
/**
 * \rststar
 * This function will return a :cpp:class:`~mppp::real` :math:`\pi`
 * with a precision of ``p``. If ``p`` is zero, the precision
 * will be set to the value returned by :cpp:func:`~mppp::real_get_default_prec()`.
 * If ``p`` is zero and no default precision has been set, an error will be raised.
 * \endrststar
 *
 * @param p the desired precision.
 *
 * @return a \link mppp::real real\endlink \f$\pi\f$.
 *
 * @throws std::invalid_argument if \p p is not within the bounds established by
 * \link mppp::real_prec_min() real_prec_min()\endlink and \link mppp::real_prec_max() real_prec_max()\endlink,
 * or if \p p is zero but no default precision has been set.
 */
inline real real_pi(::mpfr_prec_t p = 0)
{
    return detail::real_constant(::mpfr_const_pi, p);
}

/// Set \link mppp::real real\endlink to \f$\pi\f$.
/**
 * This function will set \p rop to \f$\pi\f$. The precision
 * of \p rop will not be altered.
 *
 * @param rop the \link mppp::real real\endlink that will be set to \f$\pi\f$.
 *
 * @return a reference to \p rop.
 */
inline real &real_pi(real &rop)
{
    ::mpfr_const_pi(rop._get_mpfr_t(), MPFR_RNDN);
    return rop;
}

/** @} */

/** @defgroup real_intrem real_intrem
 *  @{
 */

/// Detect if a \link mppp::real real\endlink is an integer.
/**
 * @param r the input \link mppp::real real\endlink.
 *
 * @return ``true`` if ``r`` represents an integral value, ``false`` otherwise.
 */
inline bool integer_p(const real &r)
{
    return r.integer_p();
}

/// Binary \link mppp::real real\endlink truncation.
/**
 * This function will truncate ``op`` and store the result
 * into ``rop``. The precision of the result will be equal to the precision
 * of ``op``.
 *
 * @param rop the return value.
 * @param op the operand.
 *
 * @return a reference to \p rop.
 *
 * @throws std::domain_error if ``op`` is NaN.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real &trunc(real &rop, T &&op)
{
    detail::real_check_trunc_arg(op);
    return detail::mpfr_nary_op_nornd(0, ::mpfr_trunc, rop, std::forward<T>(op));
}

/// Unary \link mppp::real real\endlink truncation.
/**
 * This function will return the truncated counterpart of ``r``.
 * The precision of the result will be equal to the precision
 * of ``r``.
 *
 * @param r the operand.
 *
 * @return the truncated counterpart of ``r``.
 *
 * @throws std::domain_error if ``r`` is NaN.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real trunc(T &&r)
{
    detail::real_check_trunc_arg(r);
    return detail::mpfr_nary_op_return_nornd(0, ::mpfr_trunc, std::forward<T>(r));
}

/** @} */

/** @defgroup real_operators real_operators
 *  @{
 */

/// Identity operator for \link mppp::real real\endlink.
/**
 * \rststar
 * This operator will return a copy of the input :cpp:class:`~mppp::real` ``r``.
 * \endrststar
 *
 * @param r the \link mppp::real real\endlink that will be copied.
 *
 * @return a copy of \p r.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real operator+(T &&r)
{
    return std::forward<T>(r);
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_binary_add(T &&a, U &&b)
{
    return mpfr_nary_op_return(0, ::mpfr_add, std::forward<T>(a), std::forward<U>(b));
}

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, is_real_interoperable<U>>::value, int> = 0>
inline real dispatch_binary_add(T &&a, const U &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_binary_add(std::forward<T>(a), tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<is_real_interoperable<T>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_binary_add(const T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_binary_add(tmp, std::forward<U>(a));
}
} // namespace detail

/// Binary addition involving \link mppp::real real\endlink.
/**
 * \rststar
 * The precision of the result will be set to the largest precision among the operands.
 *
 * Non-:cpp:class:`~mppp::real` operands will be converted to :cpp:class:`~mppp::real`
 * before performing the operation. The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return \f$a+b\f$.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealOpTypes<T, U>
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
#endif
    inline real operator+(T &&a, U &&b)
{
    return detail::dispatch_binary_add(std::forward<T>(a), std::forward<U>(b));
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, unref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline void dispatch_in_place_add(T &a, U &&b)
{
    add(a, a, std::forward<U>(b));
}

template <typename T, enable_if_t<is_real_interoperable<T>::value, int> = 0>
inline void dispatch_in_place_add(real &a, const T &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_add(a, tmp);
}

// NOTE: split this in two parts: for C++ types and real128, we use directly the cast
// operator, for integer and rational we use the get() function.
template <typename T, typename U,
          enable_if_t<conjunction<disjunction<is_cpp_interoperable<T>
#if defined(MPPP_WITH_QUADMATH)
                                              ,
                                              std::is_same<T, real128>
#endif
                                              >,
                                  std::is_same<real, uncvref_t<U>>>::value,
                      int> = 0>
inline void dispatch_in_place_add(T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_add(tmp, std::forward<U>(a));
    x = static_cast<T>(tmp);
}

template <typename T>
inline void real_in_place_convert(T &x, const real &tmp, const real &a, const char *op)
{
    if (mppp_unlikely(!get(x, tmp))) {
        if (is_integer<T>::value) {
            // Conversion to integer can fail only if the source value is not finite.
            assert(!tmp.number_p());
            throw std::domain_error(std::string{"The result of the in-place "} + op + " of the real " + a.to_string()
                                    + " with the integer " + x.to_string() + " is the non-finite value "
                                    + tmp.to_string());
        }
        // Conversion to rational can fail if the source value is not finite, or if the conversion
        // results in overflow in the manipulation of the real exponent.
        if (!tmp.number_p()) {
            throw std::domain_error(std::string{"The result of the in-place "} + op + " of the real " + a.to_string()
                                    + " with the rational " + x.to_string() + " is the non-finite value "
                                    + tmp.to_string());
        }
        // LCOV_EXCL_START
        throw std::overflow_error("The conversion of the real " + tmp.to_string() + " to rational during the in-place "
                                  + op + " of the real " + a.to_string() + " with the rational " + x.to_string()
                                  + " triggers an internal overflow condition");
        // LCOV_EXCL_STOP
    }
}

template <typename T, typename U,
          enable_if_t<conjunction<disjunction<is_integer<T>, is_rational<T>>, std::is_same<real, uncvref_t<U>>>::value,
                      int> = 0>
inline void dispatch_in_place_add(T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_add(tmp, std::forward<U>(a));
    real_in_place_convert(x, tmp, a, "addition");
}
} // namespace detail

/// In-place addition involving \link mppp::real real\endlink.
/**
 * \rststar
 * If ``a`` is a :cpp:class:`~mppp::real`, then this operator is equivalent
 * to the expression:
 *
 * .. code-block:: c++
 *
 *    a = a + b;
 *
 * Otherwise, this operator is equivalent to the expression:
 *
 * .. code-block:: c++
 *
 *    a = static_cast<T>(a + b);
 *
 * That is, the operation is always performed via the corresponding binary operator
 * and the result is assigned back to ``a``, after a conversion if necessary.
 * \endrststar
 *
 * @param a the augend.
 * @param b the addend.
 *
 * @return a reference to \p a.
 *
 * @throws unspecified any exception thrown by the corresponding binary operator,
 * or by the generic conversion operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealCompoundOpTypes<T, U>
#else
template <typename T, typename U, real_compound_op_types_enabler<T, U> = 0>
#endif
    inline T &operator+=(T &a, U &&b)
{
    detail::dispatch_in_place_add(a, std::forward<U>(b));
    return a;
}

/// Prefix increment for \link mppp::real real\endlink.
/**
 * This operator will increment \p x by one.
 *
 * @param x the \link mppp::real real\endlink that will be increased.
 *
 * @return a reference to \p x after the increment.
 */
inline real &operator++(real &x)
{
    return x += 1;
}

/// Suffix increment for \link mppp::real real\endlink.
/**
 * This operator will increment \p x by one and return a copy of \p x as it was before the increment.
 *
 * @param x the \link mppp::real real\endlink that will be increased.
 *
 * @return a copy of \p x before the increment.
 */
inline real operator++(real &x, int)
{
    auto retval(x);
    ++x;
    return retval;
}

/// Negated copy for \link mppp::real real\endlink.
/**
 * \rststar
 * This operator will return a negated copy of the input :cpp:class:`~mppp::real` ``r``.
 * \endrststar
 *
 * @param r the \link mppp::real real\endlink that will be negated.
 *
 * @return a negated copy of \p r.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <CvrReal T>
#else
template <typename T, cvr_real_enabler<T> = 0>
#endif
inline real operator-(T &&r)
{
    real retval{std::forward<T>(r)};
    retval.neg();
    return retval;
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_binary_sub(T &&a, U &&b)
{
    return mpfr_nary_op_return(0, ::mpfr_sub, std::forward<T>(a), std::forward<U>(b));
}

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, is_real_interoperable<U>>::value, int> = 0>
inline real dispatch_binary_sub(T &&a, const U &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_binary_sub(std::forward<T>(a), tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<is_real_interoperable<T>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_binary_sub(const T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_binary_sub(tmp, std::forward<U>(a));
}
} // namespace detail

/// Binary subtraction involving \link mppp::real real\endlink.
/**
 * \rststar
 * The precision of the result will be set to the largest precision among the operands.
 *
 * Non-:cpp:class:`~mppp::real` operands will be converted to :cpp:class:`~mppp::real`
 * before performing the operation. The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return \f$a-b\f$.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealOpTypes<T, U>
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
#endif
    inline real operator-(T &&a, U &&b)
{
    return detail::dispatch_binary_sub(std::forward<T>(a), std::forward<U>(b));
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, unref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline void dispatch_in_place_sub(T &a, U &&b)
{
    sub(a, a, std::forward<U>(b));
}

template <typename T, enable_if_t<is_real_interoperable<T>::value, int> = 0>
inline void dispatch_in_place_sub(real &a, const T &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_sub(a, tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<disjunction<is_cpp_interoperable<T>
#if defined(MPPP_WITH_QUADMATH)
                                              ,
                                              std::is_same<T, real128>
#endif
                                              >,
                                  std::is_same<real, uncvref_t<U>>>::value,
                      int> = 0>
inline void dispatch_in_place_sub(T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_sub(tmp, std::forward<U>(a));
    x = static_cast<T>(tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<disjunction<is_integer<T>, is_rational<T>>, std::is_same<real, uncvref_t<U>>>::value,
                      int> = 0>
inline void dispatch_in_place_sub(T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_sub(tmp, std::forward<U>(a));
    real_in_place_convert(x, tmp, a, "subtraction");
}
} // namespace detail

/// In-place subtraction involving \link mppp::real real\endlink.
/**
 * \rststar
 * If ``a`` is a :cpp:class:`~mppp::real`, then this operator is equivalent
 * to the expression:
 *
 * .. code-block:: c++
 *
 *    a = a - b;
 *
 * Otherwise, this operator is equivalent to the expression:
 *
 * .. code-block:: c++
 *
 *    a = static_cast<T>(a - b);
 *
 * That is, the operation is always performed via the corresponding binary operator
 * and the result is assigned back to ``a``, after a conversion if necessary.
 * \endrststar
 *
 * @param a the minuend.
 * @param b the subtrahend.
 *
 * @return a reference to \p a.
 *
 * @throws unspecified any exception thrown by the corresponding binary operator,
 * or by the generic conversion operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealCompoundOpTypes<T, U>
#else
template <typename T, typename U, real_compound_op_types_enabler<T, U> = 0>
#endif
    inline T &operator-=(T &a, U &&b)
{
    detail::dispatch_in_place_sub(a, std::forward<U>(b));
    return a;
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_binary_mul(T &&a, U &&b)
{
    return mpfr_nary_op_return(0, ::mpfr_mul, std::forward<T>(a), std::forward<U>(b));
}

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, is_real_interoperable<U>>::value, int> = 0>
inline real dispatch_binary_mul(T &&a, const U &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_binary_mul(std::forward<T>(a), tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<is_real_interoperable<T>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_binary_mul(const T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_binary_mul(tmp, std::forward<U>(a));
}
} // namespace detail

/// Prefix decrement for \link mppp::real real\endlink.
/**
 * This operator will decrement \p x by one.
 *
 * @param x the \link mppp::real real\endlink that will be decreased.
 *
 * @return a reference to \p x after the decrement.
 */
inline real &operator--(real &x)
{
    return x -= 1;
}

/// Suffix decrement for \link mppp::real real\endlink.
/**
 * This operator will decrement \p x by one and return a copy of \p x as it was before the decrement.
 *
 * @param x the \link mppp::real real\endlink that will be decreased.
 *
 * @return a copy of \p x before the decrement.
 */
inline real operator--(real &x, int)
{
    auto retval(x);
    --x;
    return retval;
}

/// Binary multiplication involving \link mppp::real real\endlink.
/**
 * \rststar
 * The precision of the result will be set to the largest precision among the operands.
 *
 * Non-:cpp:class:`~mppp::real` operands will be converted to :cpp:class:`~mppp::real`
 * before performing the operation. The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return \f$a\times b\f$.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealOpTypes<T, U>
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
#endif
    inline real operator*(T &&a, U &&b)
{
    return detail::dispatch_binary_mul(std::forward<T>(a), std::forward<U>(b));
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, unref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline void dispatch_in_place_mul(T &a, U &&b)
{
    mul(a, a, std::forward<U>(b));
}

template <typename T, enable_if_t<is_real_interoperable<T>::value, int> = 0>
inline void dispatch_in_place_mul(real &a, const T &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_mul(a, tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<disjunction<is_cpp_interoperable<T>
#if defined(MPPP_WITH_QUADMATH)
                                              ,
                                              std::is_same<T, real128>
#endif
                                              >,
                                  std::is_same<real, uncvref_t<U>>>::value,
                      int> = 0>
inline void dispatch_in_place_mul(T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_mul(tmp, std::forward<U>(a));
    x = static_cast<T>(tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<disjunction<is_integer<T>, is_rational<T>>, std::is_same<real, uncvref_t<U>>>::value,
                      int> = 0>
inline void dispatch_in_place_mul(T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_mul(tmp, std::forward<U>(a));
    real_in_place_convert(x, tmp, a, "multiplication");
}
} // namespace detail

/// In-place multiplication involving \link mppp::real real\endlink.
/**
 * \rststar
 * If ``a`` is a :cpp:class:`~mppp::real`, then this operator is equivalent
 * to the expression:
 *
 * .. code-block:: c++
 *
 *    a = a * b;
 *
 * Otherwise, this operator is equivalent to the expression:
 *
 * .. code-block:: c++
 *
 *    a = static_cast<T>(a * b);
 *
 * That is, the operation is always performed via the corresponding binary operator
 * and the result is assigned back to ``a``, after a conversion if necessary.
 * \endrststar
 *
 * @param a the multiplicand.
 * @param b the multiplicator.
 *
 * @return a reference to \p a.
 *
 * @throws unspecified any exception thrown by the corresponding binary operator,
 * or by the generic conversion operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealCompoundOpTypes<T, U>
#else
template <typename T, typename U, real_compound_op_types_enabler<T, U> = 0>
#endif
    inline T &operator*=(T &a, U &&b)
{
    detail::dispatch_in_place_mul(a, std::forward<U>(b));
    return a;
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_binary_div(T &&a, U &&b)
{
    return mpfr_nary_op_return(0, ::mpfr_div, std::forward<T>(a), std::forward<U>(b));
}

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, uncvref_t<T>>, is_real_interoperable<U>>::value, int> = 0>
inline real dispatch_binary_div(T &&a, const U &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_binary_div(std::forward<T>(a), tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<is_real_interoperable<T>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline real dispatch_binary_div(const T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return dispatch_binary_div(tmp, std::forward<U>(a));
}
} // namespace detail

/// Binary division involving \link mppp::real real\endlink.
/**
 * \rststar
 * The precision of the result will be set to the largest precision among the operands.
 *
 * Non-:cpp:class:`~mppp::real` operands will be converted to :cpp:class:`~mppp::real`
 * before performing the operation. The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return \f$a / b\f$.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename U, RealOpTypes<U> T>
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
#endif
inline real operator/(T &&a, U &&b)
{
    return detail::dispatch_binary_div(std::forward<T>(a), std::forward<U>(b));
}

namespace detail
{

template <typename T, typename U,
          enable_if_t<conjunction<std::is_same<real, unref_t<T>>, std::is_same<real, uncvref_t<U>>>::value, int> = 0>
inline void dispatch_in_place_div(T &a, U &&b)
{
    div(a, a, std::forward<U>(b));
}

template <typename T, enable_if_t<is_real_interoperable<T>::value, int> = 0>
inline void dispatch_in_place_div(real &a, const T &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_div(a, tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<disjunction<is_cpp_interoperable<T>
#if defined(MPPP_WITH_QUADMATH)
                                              ,
                                              std::is_same<T, real128>
#endif
                                              >,
                                  std::is_same<real, uncvref_t<U>>>::value,
                      int> = 0>
inline void dispatch_in_place_div(T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_div(tmp, std::forward<U>(a));
    x = static_cast<T>(tmp);
}

template <typename T, typename U,
          enable_if_t<conjunction<disjunction<is_integer<T>, is_rational<T>>, std::is_same<real, uncvref_t<U>>>::value,
                      int> = 0>
inline void dispatch_in_place_div(T &x, U &&a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    dispatch_in_place_div(tmp, std::forward<U>(a));
    real_in_place_convert(x, tmp, a, "division");
}
} // namespace detail

/// In-place division involving \link mppp::real real\endlink.
/**
 * \rststar
 * If ``a`` is a :cpp:class:`~mppp::real`, then this operator is equivalent
 * to the expression:
 *
 * .. code-block:: c++
 *
 *    a = a / b;
 *
 * Otherwise, this operator is equivalent to the expression:
 *
 * .. code-block:: c++
 *
 *    a = static_cast<T>(a / b);
 *
 * That is, the operation is always performed via the corresponding binary operator
 * and the result is assigned back to ``a``, after a conversion if necessary.
 * \endrststar
 *
 * @param a the dividend.
 * @param b the divisor.
 *
 * @return a reference to \p a.
 *
 * @throws unspecified any exception thrown by the corresponding binary operator,
 * or by the generic conversion operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealCompoundOpTypes<T, U>
#else
template <typename T, typename U, real_compound_op_types_enabler<T, U> = 0>
#endif
    inline T &operator/=(T &a, U &&b)
{
    detail::dispatch_in_place_div(a, std::forward<U>(b));
    return a;
}

namespace detail
{

// Common dispatch functions for comparison operators involving real.
template <typename F, typename T, enable_if_t<is_real_interoperable<T>::value, int> = 0>
inline bool dispatch_real_comparison(const F &f, const real &a, const T &x)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return f(a.get_mpfr_t(), tmp.get_mpfr_t()) != 0;
}

template <typename F, typename T, enable_if_t<is_real_interoperable<T>::value, int> = 0>
inline bool dispatch_real_comparison(const F &f, const T &x, const real &a)
{
    MPPP_MAYBE_TLS real tmp;
    tmp = x;
    return f(tmp.get_mpfr_t(), a.get_mpfr_t()) != 0;
}

template <typename F>
inline bool dispatch_real_comparison(const F &f, const real &a, const real &b)
{
    return f(a.get_mpfr_t(), b.get_mpfr_t()) != 0;
}
} // namespace detail

/// Equality operator involving \link mppp::real real\endlink.
/**
 * \rststar
 * This operator will compare ``a`` and ``b``, returning ``true`` if ``a`` is
 * equal to ``b``, ``false`` otherwise. The comparison is performed via the
 * ``mpfr_equal_p()`` function from the MPFR API. Non-:cpp:class:`~mppp::real`
 * operands will be converted to :cpp:class:`~mppp::real` before performing the operation.
 * The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 *
 * .. note::
 *    This operator does not handle NaN in a special way (that is, NaN compares
 *    different from any value, including NaN itself). See :cpp:func:`mppp::real_equal_to()`
 *    for an equality predicate that handles NaN specially.
 *
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return ``true`` if ``a`` is equal to ``b``, ``false`` otherwise.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealOpTypes<T, U>
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
#endif
    inline bool operator==(const T &a, const U &b)
{
    return detail::dispatch_real_comparison(::mpfr_equal_p, a, b);
}

/// Inequality operator involving \link mppp::real real\endlink.
/**
 * \rststar
 * This operator will compare ``a`` and ``b``, returning ``true`` if ``a`` is
 * different from ``b``, ``false`` otherwise. The comparison is performed via the
 * ``mpfr_equal_p()`` function from the MPFR API. Non-:cpp:class:`~mppp::real`
 * operands will be converted to :cpp:class:`~mppp::real` before performing the operation.
 * The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 *
 * .. note::
 *    This operator does not handle NaN in a special way (that is, NaN compares
 *    different from any value, including NaN itself). See :cpp:func:`mppp::real_equal_to()`
 *    for an equality predicate that handles NaN specially.
 *
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return ``true`` if ``a`` is different from ``b``, ``false`` otherwise.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealOpTypes<T, U>
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
#endif
    inline bool operator!=(const T &a, const U &b)
{
    return !(a == b);
}

/// Greater-than operator involving \link mppp::real real\endlink.
/**
 * \rststar
 * This operator will compare ``a`` and ``b``, returning ``true`` if ``a`` is
 * greater than ``b``, ``false`` otherwise. The comparison is performed via the
 * ``mpfr_greater_p()`` function from the MPFR API. Non-:cpp:class:`~mppp::real`
 * operands will be converted to :cpp:class:`~mppp::real` before performing the operation.
 * The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 *
 * .. note::
 *    This operator does not handle NaN in a special way (that is, NaN is never
 *    greater than any value, and no value is greater than NaN).
 *    See :cpp:func:`mppp::real_gt()` for a greater-than predicate that handles
 *    NaN specially.
 *
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return ``true`` if ``a`` is greater than ``b``, ``false`` otherwise.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealOpTypes<T, U>
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
#endif
    inline bool operator>(const T &a, const U &b)
{
    return detail::dispatch_real_comparison(::mpfr_greater_p, a, b);
}

/// Greater-than or equal operator involving \link mppp::real real\endlink.
/**
 * \rststar
 * This operator will compare ``a`` and ``b``, returning ``true`` if ``a`` is
 * greater than or equal to ``b``, ``false`` otherwise. The comparison is performed via the
 * ``mpfr_greaterequal_p()`` function from the MPFR API. Non-:cpp:class:`~mppp::real`
 * operands will be converted to :cpp:class:`~mppp::real` before performing the operation.
 * The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 *
 * .. note::
 *    This operator does not handle NaN in a special way (that is, NaN is never
 *    greater than or equal to any value, and no value is greater than or equal to NaN).
 *
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return ``true`` if ``a`` is greater than or equal to ``b``, ``false`` otherwise.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealOpTypes<T, U>
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
#endif
    inline bool operator>=(const T &a, const U &b)
{
    return detail::dispatch_real_comparison(::mpfr_greaterequal_p, a, b);
}

/// Less-than operator involving \link mppp::real real\endlink.
/**
 * \rststar
 * This operator will compare ``a`` and ``b``, returning ``true`` if ``a`` is
 * less than ``b``, ``false`` otherwise. The comparison is performed via the
 * ``mpfr_less_p()`` function from the MPFR API. Non-:cpp:class:`~mppp::real`
 * operands will be converted to :cpp:class:`~mppp::real` before performing the operation.
 * The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 *
 * .. note::
 *    This operator does not handle NaN in a special way (that is, NaN is never
 *    less than any value, and no value is less than NaN).
 *    See :cpp:func:`mppp::real_lt()` for a less-than predicate that handles
 *    NaN specially.
 *
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return ``true`` if ``a`` is less than ``b``, ``false`` otherwise.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealOpTypes<T, U>
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
#endif
    inline bool operator<(const T &a, const U &b)
{
    return detail::dispatch_real_comparison(::mpfr_less_p, a, b);
}

/// Less-than or equal operator involving \link mppp::real real\endlink.
/**
 * \rststar
 * This operator will compare ``a`` and ``b``, returning ``true`` if ``a`` is
 * less than or equal to ``b``, ``false`` otherwise. The comparison is performed via the
 * ``mpfr_lessequal_p()`` function from the MPFR API. Non-:cpp:class:`~mppp::real`
 * operands will be converted to :cpp:class:`~mppp::real` before performing the operation.
 * The conversion of non-:cpp:class:`~mppp::real` operands
 * to :cpp:class:`~mppp::real` follows the same heuristics described in the generic assignment operator of
 * :cpp:class:`~mppp::real`. Specifically, the precision of the conversion is either the default
 * precision, if set, or it is automatically deduced depending on the type and value of the
 * operand to be converted.
 *
 * .. note::
 *    This operator does not handle NaN in a special way (that is, NaN is never
 *    less than or equal to any value, and no value is less than or equal to NaN).
 *
 * \endrststar
 *
 * @param a the first operand.
 * @param b the second operand.
 *
 * @return ``true`` if ``a`` is less than or equal to ``b``, ``false`` otherwise.
 *
 * @throws unspecified any exception thrown by the generic assignment operator of \link mppp::real real\endlink.
 */
#if defined(MPPP_HAVE_CONCEPTS)
template <typename T, typename U>
requires RealOpTypes<T, U>
#else
template <typename T, typename U, real_op_types_enabler<T, U> = 0>
#endif
    inline bool operator<=(const T &a, const U &b)
{
    return detail::dispatch_real_comparison(::mpfr_lessequal_p, a, b);
}

/** @} */
} // namespace mppp

#else

#error The real.hpp header was included but mp++ was not configured with the MPPP_WITH_MPFR option.

#endif

#endif
