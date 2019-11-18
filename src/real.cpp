// Copyright 2016-2019 Francesco Biscani (bluescarni@gmail.com)
//
// This file is part of the mp++ library.
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <mp++/config.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(MPPP_HAVE_STRING_VIEW)
#include <string_view>
#endif

#include <mp++/detail/gmp.hpp>
#include <mp++/detail/mpfr.hpp>
#include <mp++/detail/utils.hpp>
#include <mp++/integer.hpp>
#include <mp++/real.hpp>

#if defined(MPPP_WITH_QUADMATH)
#include <mp++/real128.hpp>
#endif

namespace mppp
{

namespace detail
{

namespace
{

// Some misc tests to check that the mpfr struct conforms to our expectations.
struct expected_mpfr_struct_t {
    ::mpfr_prec_t _mpfr_prec;
    ::mpfr_sign_t _mpfr_sign;
    ::mpfr_exp_t _mpfr_exp;
    ::mp_limb_t *_mpfr_d;
};

static_assert(sizeof(expected_mpfr_struct_t) == sizeof(mpfr_struct_t) && offsetof(mpfr_struct_t, _mpfr_prec) == 0u
                  && offsetof(mpfr_struct_t, _mpfr_sign) == offsetof(expected_mpfr_struct_t, _mpfr_sign)
                  && offsetof(mpfr_struct_t, _mpfr_exp) == offsetof(expected_mpfr_struct_t, _mpfr_exp)
                  && offsetof(mpfr_struct_t, _mpfr_d) == offsetof(expected_mpfr_struct_t, _mpfr_d)
                  && std::is_same<::mp_limb_t *, decltype(std::declval<mpfr_struct_t>()._mpfr_d)>::value,
              "Invalid mpfr_t struct layout and/or MPFR types.");

#if MPPP_CPLUSPLUS >= 201703L

// If we have C++17, we can use structured bindings to test the layout of mpfr_struct_t
// and its members' types.
constexpr bool test_mpfr_struct_t()
{
    auto [prec, sign, exp, ptr] = mpfr_struct_t{};
    static_assert(std::is_same<decltype(ptr), ::mp_limb_t *>::value);
    ignore(prec, sign, exp, ptr);

    return true;
}

static_assert(test_mpfr_struct_t());

#endif

} // namespace

// Wrapper for calling mpfr_lgamma().
void real_lgamma_wrapper(::mpfr_t rop, const ::mpfr_t op, ::mpfr_rnd_t)
{
    // NOTE: we ignore the sign for consistency with lgamma.
    int signp;
    ::mpfr_lgamma(rop, &signp, op, MPFR_RNDN);
}

// A small helper to check the input of the trunc() overloads.
void real_check_trunc_arg(const real &r)
{
    if (mppp_unlikely(r.nan_p())) {
        throw std::domain_error("Cannot truncate a NaN value");
    }
}

void mpfr_to_stream(const ::mpfr_t r, std::ostream &os, int base)
{
    // All chars potentially used by MPFR for representing the digits up to base 62, sorted.
    constexpr char all_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    // Check the base.
    if (mppp_unlikely(base < 2 || base > 62)) {
        throw std::invalid_argument("Cannot convert a real to a string in base " + to_string(base)
                                    + ": the base must be in the [2,62] range");
    }
    // Special values first.
    if (mpfr_nan_p(r)) {
        // NOTE: up to base 16 we can use nan, inf, etc., but with larger
        // bases we have to use the syntax with @.
        os << (base <= 16 ? "nan" : "@nan@");
        return;
    }
    if (mpfr_inf_p(r)) {
        if (mpfr_sgn(r) < 0) {
            os << '-';
        }
        os << (base <= 16 ? "inf" : "@inf@");
        return;
    }

    // Get the string fractional representation via the MPFR function,
    // and wrap it into a smart pointer.
    ::mpfr_exp_t exp(0);
    std::unique_ptr<char, void (*)(char *)> str(::mpfr_get_str(nullptr, &exp, base, 0, r, MPFR_RNDN), ::mpfr_free_str);
    // LCOV_EXCL_START
    if (mppp_unlikely(!str)) {
        throw std::runtime_error("Error in the conversion of a real to string: the call to mpfr_get_str() failed");
    }
    // LCOV_EXCL_STOP

    // Print the string, inserting a decimal point after the first digit.
    bool dot_added = false;
    for (auto cptr = str.get(); *cptr != '\0'; ++cptr) {
        os << (*cptr);
        if (!dot_added) {
            if (base <= 10) {
                // For bases up to 10, we can use the followig guarantee
                // from the standard:
                // http://stackoverflow.com/questions/13827180/char-ascii-relation
                // """
                // The mapping of integer values for characters does have one guarantee given
                // by the Standard: the values of the decimal digits are contiguous.
                // (i.e., '1' - '0' == 1, ... '9' - '0' == 9)
                // """
                // http://eel.is/c++draft/lex.charset#3
                if (*cptr >= '0' && *cptr <= '9') {
                    os << '.';
                    dot_added = true;
                }
            } else {
                // For bases larger than 10, we do a binary search among all the allowed characters.
                // NOTE: we need to search into the whole all_chars array (instead of just up to all_chars
                // + base) because apparently mpfr_get_str() seems to ignore lower/upper case when the base
                // is small enough (e.g., it uses 'a' instead of 'A' when printing in base 11).
                // NOTE: the range needs to be sizeof() - 1 because sizeof() also includes the terminator.
                if (std::binary_search(all_chars, all_chars + (sizeof(all_chars) - 1u), *cptr)) {
                    os << '.';
                    dot_added = true;
                }
            }
        }
    }
    assert(dot_added);

    // Adjust the exponent. Do it in multiprec in order to avoid potential overflow.
    integer<1> z_exp{exp};
    --z_exp;
    const auto exp_sgn = z_exp.sgn();
    if (exp_sgn && !mpfr_zero_p(r)) {
        // Add the exponent at the end of the string, if both the value and the exponent
        // are nonzero.
        // NOTE: for bases greater than 10 we need '@' for the exponent, rather than 'e' or 'E'.
        // https://www.mpfr.org/mpfr-current/mpfr.html#Assignment-Functions
        os << (base <= 10 ? 'e' : '@');
        if (exp_sgn == 1) {
            // Add extra '+' if the exponent is positive, for consistency with
            // real128's string format (and possibly other formats too?).
            os << '+';
        }
        os << z_exp;
    }
}

// NOTE: the use of ATOMIC_VAR_INIT ensures that the initialisation of default_prec
// is constant initialisation:
//
// http://en.cppreference.com/w/cpp/atomic/ATOMIC_VAR_INIT
//
// This essentially means that this initialisation happens before other types of
// static initialisation:
//
// http://en.cppreference.com/w/cpp/language/initialization
//
// This ensures that static reals, which are subject to dynamic initialization, are initialised
// when this variable has already been constructed, and thus access to it will be safe.
std::atomic<::mpfr_prec_t> real_default_prec = ATOMIC_VAR_INIT(::mpfr_prec_t(0));

} // namespace detail

/// Default constructor.
/**
 * \rststar
 * The value will be initialised to positive zero. The precision of ``this`` will be
 * either the default precision, if set, or the value returned by :cpp:func:`~mppp::real_prec_min()`
 * otherwise.
 * \endrststar
 */
real::real()
{
    // Init with minimum or default precision.
    const auto dp = real_get_default_prec();
    ::mpfr_init2(&m_mpfr, dp ? dp : real_prec_min());
    ::mpfr_set_zero(&m_mpfr, 1);
}

// Init a real with precision p, setting its value to n. No precision
// checking is performed.
real::real(const ptag &, ::mpfr_prec_t p, bool ignore_prec)
{
    assert(ignore_prec);
    assert(detail::real_prec_check(p));
    detail::ignore(ignore_prec);
    ::mpfr_init2(&m_mpfr, p);
}

/// Copy constructor.
/**
 * The copy constructor performs an exact deep copy of the input object.
 *
 * @param other the \link mppp::real real\endlink that will be copied.
 */
real::real(const real &other) : real(&other.m_mpfr) {}

/// Copy constructor with custom precision.
/**
 * This constructor will set \p this to a copy of \p other with precision \p p. If \p p
 * is smaller than the precision of \p other, a rounding operation will be performed,
 * otherwise the value will be copied exactly.
 *
 * @param other the \link mppp::real real\endlink that will be copied.
 * @param p the desired precision.
 *
 * @throws std::invalid_argument if \p p is outside the range established by
 * \link mppp::real_prec_min() real_prec_min()\endlink and \link mppp::real_prec_max() real_prec_max()\endlink.
 */
real::real(const real &other, ::mpfr_prec_t p)
{
    // Init with custom precision, and then set.
    ::mpfr_init2(&m_mpfr, check_init_prec(p));
    ::mpfr_set(&m_mpfr, &other.m_mpfr, MPFR_RNDN);
}

// Construction from FPs.
template <typename Func, typename T>
void real::dispatch_fp_construction(const Func &func, const T &x, ::mpfr_prec_t p)
{
    ::mpfr_init2(&m_mpfr, compute_init_precision(p, x));
    func(&m_mpfr, x, MPFR_RNDN);
}

void real::dispatch_construction(const float &x, ::mpfr_prec_t p)
{
    dispatch_fp_construction(::mpfr_set_flt, x, p);
}

void real::dispatch_construction(const double &x, ::mpfr_prec_t p)
{
    dispatch_fp_construction(::mpfr_set_d, x, p);
}

void real::dispatch_construction(const long double &x, ::mpfr_prec_t p)
{
    dispatch_fp_construction(::mpfr_set_ld, x, p);
}

// Special casing for bool, otherwise MSVC warns if we fold this into the
// constructor from unsigned.
void real::dispatch_construction(const bool &b, ::mpfr_prec_t p)
{
    dispatch_integral_init(p, b);
    ::mpfr_set_ui(&m_mpfr, static_cast<unsigned long>(b), MPFR_RNDN);
}

#if defined(MPPP_WITH_QUADMATH)
void real::dispatch_construction(const real128 &x, ::mpfr_prec_t p)
{
    // Init the value.
    ::mpfr_init2(&m_mpfr, compute_init_precision(p, x));
    assign_real128(x);
}
#endif

void real::dispatch_mpz_construction(const ::mpz_t n, ::mpfr_prec_t p)
{
    ::mpfr_init2(&m_mpfr, p);
    ::mpfr_set_z(&m_mpfr, n, MPFR_RNDN);
}

void real::dispatch_mpq_construction(const ::mpq_t q, ::mpfr_prec_t p)
{
    ::mpfr_init2(&m_mpfr, p);
    ::mpfr_set_q(&m_mpfr, q, MPFR_RNDN);
}

// Various helpers and constructors from string-like entities.
void real::construct_from_c_string(const char *s, int base, ::mpfr_prec_t p)
{
    if (mppp_unlikely(base && (base < 2 || base > 62))) {
        throw std::invalid_argument("Cannot construct a real from a string in base " + detail::to_string(base)
                                    + ": the base must either be zero or in the [2,62] range");
    }
    const ::mpfr_prec_t prec = p ? check_init_prec(p) : real_get_default_prec();
    if (mppp_unlikely(!prec)) {
        throw std::invalid_argument("Cannot construct a real from a string if the precision is not explicitly "
                                    "specified and no default precision has been set");
    }
    ::mpfr_init2(&m_mpfr, prec);
    const auto ret = ::mpfr_set_str(&m_mpfr, s, base, MPFR_RNDN);
    if (mppp_unlikely(ret == -1)) {
        ::mpfr_clear(&m_mpfr);
        throw std::invalid_argument(std::string{"The string '"} + s + "' does not represent a valid real in base "
                                    + detail::to_string(base));
    }
}

real::real(const ptag &, const char *s, int base, ::mpfr_prec_t p)
{
    construct_from_c_string(s, base, p);
}

real::real(const ptag &, const std::string &s, int base, ::mpfr_prec_t p) : real(s.c_str(), base, p) {}

#if defined(MPPP_HAVE_STRING_VIEW)
real::real(const ptag &, const std::string_view &s, int base, ::mpfr_prec_t p)
    : real(s.data(), s.data() + s.size(), base, p)
{
}
#endif

/// Constructor from range of characters, base and precision.
/**
 * This constructor will initialise \p this from the content of the input half-open range,
 * which is interpreted as the string representation of a floating-point value in base \p base.
 *
 * Internally, the constructor will copy the content of the range to a local buffer, add a
 * string terminator, and invoke the constructor from string, base and precision.
 *
 * @param begin the start of the input range.
 * @param end the end of the input range.
 * @param base the base used in the string representation.
 * @param p the desired precision.
 *
 * @throws unspecified any exception thrown by the constructor from string, or by memory
 * allocation errors in standard containers.
 */
real::real(const char *begin, const char *end, int base, ::mpfr_prec_t p)
{
    MPPP_MAYBE_TLS std::vector<char> buffer;
    buffer.assign(begin, end);
    buffer.emplace_back('\0');
    construct_from_c_string(buffer.data(), base, p);
}

/// Constructor from range of characters and precision.
/**
 * This constructor is equivalent to the constructor from range of characters with a ``base`` value hard-coded
 * to 10.
 *
 * @param begin the start of the input range.
 * @param end the end of the input range.
 * @param p the desired precision.
 *
 * @throws unspecified any exception thrown by the constructor from range of characters, base and precision.
 */
real::real(const char *begin, const char *end, ::mpfr_prec_t p) : real(begin, end, 10, p) {}

/// Constructor from range of characters.
/**
 * This constructor is equivalent to the constructor from range of characters with a ``base`` value hard-coded
 * to 10 and a precision value hard-coded to zero (that is, the precision will be the default precision, if set).
 *
 * @param begin the start of the input range.
 * @param end the end of the input range.
 *
 * @throws unspecified any exception thrown by the constructor from range of characters, base and precision.
 */
real::real(const char *begin, const char *end) : real(begin, end, 10, 0) {}

/// Constructor from a special value, sign and precision.
/**
 * \rststar
 * This constructor will initialise ``this`` with one of the special values
 * specified by the :cpp:type:`~mppp::real_kind` enum. The precision of ``this``
 * will be ``p``. If ``p`` is zero, the precision will be set to the default
 * precision (as indicated by :cpp:func:`~mppp::real_get_default_prec()`).
 *
 * If ``k`` is not NaN, the sign bit will be set to positive if ``sign``
 * is nonnegative, negative otherwise.
 * \endrststar
 *
 * @param k the desired special value.
 * @param sign the desired sign for \p this.
 * @param p the desired precision for \p this.
 *
 * @throws std::invalid_argument if \p p is not within the bounds established by
 * \link mppp::real_prec_min() real_prec_min()\endlink and \link mppp::real_prec_max() real_prec_max()\endlink,
 * or if \p p is zero but no default precision has been set.
 */
real::real(real_kind k, int sign, ::mpfr_prec_t p)
{
    ::mpfr_prec_t prec;
    if (p) {
        prec = check_init_prec(p);
    } else {
        const auto dp = real_get_default_prec();
        if (mppp_unlikely(!dp)) {
            throw std::invalid_argument("Cannot init a real with an automatically-deduced precision if "
                                        "the global default precision has not been set");
        }
        prec = dp;
    }
    ::mpfr_init2(&m_mpfr, prec);
    // NOTE: handle all cases explicitly, in order to avoid
    // compiler warnings.
    switch (k) {
        case real_kind::nan:
            break;
        case real_kind::inf:
            set_inf(sign);
            break;
        case real_kind::zero:
            set_zero(sign);
            break;
        default:
            // Clean up before throwing.
            ::mpfr_clear(&m_mpfr);
            using kind_cast_t = std::underlying_type<::mpfr_kind_t>::type;
            throw std::invalid_argument(
                "The 'real_kind' value passed to the constructor of a real ("
                + std::to_string(static_cast<kind_cast_t>(k)) + ") is not one of the three allowed values ('nan'="
                + std::to_string(static_cast<kind_cast_t>(real_kind::nan))
                + ", 'inf'=" + std::to_string(static_cast<kind_cast_t>(real_kind::inf))
                + " and 'zero'=" + std::to_string(static_cast<kind_cast_t>(real_kind::zero)) + ")");
    }
}

/// Copy constructor from ``mpfr_t``.
/**
 * This constructor will initialise ``this`` with an exact deep copy of ``x``.
 *
 * \rststar
 * .. warning::
 *    It is the user's responsibility to ensure that ``x`` has been correctly initialised
 *    with a precision within the bounds established by :cpp:func:`~mppp::real_prec_min()`
 *    and :cpp:func:`~mppp::real_prec_max()`.
 * \endrststar
 *
 * @param x the ``mpfr_t`` that will be deep-copied.
 */
real::real(const ::mpfr_t x)
{
    // Init with the same precision as other, and then set.
    ::mpfr_init2(&m_mpfr, mpfr_get_prec(x));
    ::mpfr_set(&m_mpfr, x, MPFR_RNDN);
}

/// Copy assignment operator.
/**
 * The operator will deep-copy \p other into \p this.
 *
 * @param other the \link mppp::real real\endlink that will be copied into \p this.
 *
 * @return a reference to \p this.
 */
real &real::operator=(const real &other)
{
    if (mppp_likely(this != &other)) {
        if (m_mpfr._mpfr_d) {
            // this has not been moved-from.
            // Copy the precision. This will also reset the internal value.
            // No need for prec checking as we assume other has a valid prec.
            set_prec_impl<false>(other.get_prec());
        } else {
            // this has been moved-from: init before setting.
            ::mpfr_init2(&m_mpfr, other.get_prec());
        }
        // Perform the actual copy from other.
        ::mpfr_set(&m_mpfr, &other.m_mpfr, MPFR_RNDN);
    }
    return *this;
}

// Implementation of the assignment from string.
void real::string_assignment_impl(const char *s, int base)
{
    if (mppp_unlikely(base && (base < 2 || base > 62))) {
        throw std::invalid_argument("Cannot assign a real from a string in base " + detail::to_string(base)
                                    + ": the base must either be zero or in the [2,62] range");
    }
    const auto ret = ::mpfr_set_str(&m_mpfr, s, base, MPFR_RNDN);
    if (mppp_unlikely(ret == -1)) {
        ::mpfr_set_nan(&m_mpfr);
        throw std::invalid_argument(std::string{"The string '"} + s
                                    + "' cannot be interpreted as a floating-point value in base "
                                    + detail::to_string(base));
    }
}

// Dispatching for string assignment.
real &real::string_assignment(const char *s)
{
    const auto dp = real_get_default_prec();
    if (mppp_unlikely(!dp)) {
        throw std::invalid_argument("Cannot assign a string to a real if a default precision is not set");
    }
    set_prec_impl<false>(dp);
    string_assignment_impl(s, 10);
    return *this;
}

real &real::string_assignment(const std::string &s)
{
    return string_assignment(s.c_str());
}

#if defined(MPPP_HAVE_STRING_VIEW)
real &real::string_assignment(const std::string_view &s)
{
    MPPP_MAYBE_TLS std::vector<char> buffer;
    buffer.assign(s.begin(), s.end());
    buffer.emplace_back('\0');
    return string_assignment(buffer.data());
}
#endif

/// Copy assignment from ``mpfr_t``.
/**
 * This operator will set ``this`` to a deep copy of ``x``.
 *
 * \rststar
 * .. warning::
 *    It is the user's responsibility to ensure that ``x`` has been correctly initialised
 *    with a precision within the bounds established by :cpp:func:`~mppp::real_prec_min()`
 *    and :cpp:func:`~mppp::real_prec_max()`.
 * \endrststar
 *
 * @param x the ``mpfr_t`` that will be copied.
 *
 * @return a reference to \p this.
 */
real &real::operator=(const ::mpfr_t x)
{
    // Set the precision, assuming the prec of x is valid.
    set_prec_impl<false>(mpfr_get_prec(x));
    // Set the value.
    ::mpfr_set(&m_mpfr, x, MPFR_RNDN);
    return *this;
}

/// Set to another \link mppp::real real\endlink value.
/**
 * \rststar
 * This method will set ``this`` to the value of ``other``. Contrary to the copy assignment operator,
 * the precision of the assignment is dictated by the precision of ``this``, rather than
 * the precision of ``other``. Consequently, the precision of ``this`` will not be altered by the
 * assignment, and a rounding might occur, depending on the values
 * and the precisions of the operands.
 *
 * This method is a thin wrapper around the ``mpfr_set()`` assignment function from the MPFR API.
 *
 * .. seealso ::
 *    https://www.mpfr.org/mpfr-current/mpfr.html#Assignment-Functions
 * \endrststar
 *
 * @param other the value to which \p this will be set.
 *
 * @return a reference to \p this.
 */
real &real::set(const real &other)
{
    return set(&other.m_mpfr);
}

// Implementation of string setters.
real &real::set_impl(const char *s, int base)
{
    string_assignment_impl(s, base);
    return *this;
}

real &real::set_impl(const std::string &s, int base)
{
    return set(s.c_str(), base);
}

#if defined(MPPP_HAVE_STRING_VIEW)
real &real::set_impl(const std::string_view &s, int base)
{
    return set(s.data(), s.data() + s.size(), base);
}
#endif

// Set to character range.
real &real::set(const char *begin, const char *end, int base)
{
    MPPP_MAYBE_TLS std::vector<char> buffer;
    buffer.assign(begin, end);
    buffer.emplace_back('\0');
    return set(buffer.data(), base);
}

/// Set to an ``mpfr_t``.
/**
 * \rststar
 * This method will set ``this`` to the value of ``x``. Contrary to the corresponding assignment operator,
 * the precision of the assignment is dictated by the precision of ``this``, rather than
 * the precision of ``x``. Consequently, the precision of ``this`` will not be altered by the
 * assignment, and a rounding might occur, depending on the values
 * and the precisions of the operands.
 *
 * This method is a thin wrapper around the ``mpfr_set()`` assignment function from the MPFR API.
 *
 * .. warning::
 *    It is the user's responsibility to ensure that ``x`` has been correctly initialised.
 *
 * .. seealso ::
 *    https://www.mpfr.org/mpfr-current/mpfr.html#Assignment-Functions
 * \endrststar
 *
 * @param x the ``mpfr_t`` to which \p this will be set.
 *
 * @return a reference to \p this.
 */
real &real::set(const ::mpfr_t x)
{
    ::mpfr_set(&m_mpfr, x, MPFR_RNDN);
    return *this;
}

/// Set to NaN.
/**
 * This setter will set \p this to NaN with an unspecified sign bit. The precision of \p this
 * will not be altered.
 *
 * @return a reference to \p this.
 */
real &real::set_nan()
{
    ::mpfr_set_nan(&m_mpfr);
    return *this;
}

// Set to infinity.
real &real::set_inf(int sign)
{
    ::mpfr_set_inf(&m_mpfr, sign);
    return *this;
}

// Set to zero.
real &real::set_zero(int sign)
{
    ::mpfr_set_zero(&m_mpfr, sign);
    return *this;
}

/// Detect one.
/**
 * @return \p true if \p this is exactly equal to 1, \p false otherwise.
 */
bool real::is_one() const
{
    // NOTE: preempt calling the comparison function, if this is NaN
    // (in such case, the range flag will be touched and we do not
    // want to bother with that).
    return !nan_p() && (::mpfr_cmp_ui(&m_mpfr, 1u) == 0);
}

#if defined(MPPP_WITH_QUADMATH)

namespace detail
{

// Some private machinery for the interaction between real and real128.
namespace
{

// Shortcut for the size of the real_2_112 constant. We just need
// 1 bit of precision for this, but make sure we don't go outside
// the allowed precision range.
constexpr ::mpfr_prec_t size_real_2_112 = clamp_mpfr_prec(1);

// A bare real with static memory allocation, represented as
// an mpfr_struct_t paired to storage for the limbs.
template <::mpfr_prec_t Prec>
using static_real = std::pair<mpfr_struct_t,
                              // Use std::array as storage for the limbs.
                              std::array<::mp_limb_t, mpfr_custom_get_size(Prec) / sizeof(::mp_limb_t)
                                                          + mpfr_custom_get_size(Prec) % sizeof(::mp_limb_t)>>;

// Create a static real with value 2**112. This represents the "hidden bit"
// of the significand of a quadruple-precision FP.
// NOTE: this could be instantiated as a global static instead of being re-computed every time.
// However, since this is not constexpr, there's a high risk of static order initialization screwups
// (e.g., if initing a static real from a real128), so for the time being let's keep things basic.
// We can determine in the future if we can make this constexpr somehow and have a 2**112 instance
// inited during constant initialization.
static_real<size_real_2_112> get_real_2_112()
{
    // NOTE: pair's def ctor value-inits the members: everything in retval is zeroed out.
    static_real<size_real_2_112> retval;
    // Init the limbs first, as indicated by the mpfr docs.
    mpfr_custom_init(retval.second.data(), size_real_2_112);
    // Do the custom init with a zero value, exponent 0 (unused), precision matching the previous call,
    // and the limbs storage pointer.
    mpfr_custom_init_set(&retval.first, MPFR_ZERO_KIND, 0, size_real_2_112, retval.second.data());
    // Set the actual value.
    ::mpfr_set_ui_2exp(&retval.first, 1ul, static_cast<::mpfr_exp_t>(112), MPFR_RNDN);
    return retval;
}

} // namespace

} // namespace detail

void real::assign_real128(const real128 &x)
{
    // Get the IEEE repr. of x.
    const auto t = x.get_ieee();
    // A utility function to write the significand of x
    // as a big integer inside m_mpfr.
    auto write_significand = [this, &t]() {
        // The 4 32-bits part of the significand, from most to least
        // significant digits.
        const auto p1 = std::get<2>(t) >> 32;
        const auto p2 = std::get<2>(t) % (1ull << 32);
        const auto p3 = std::get<3>(t) >> 32;
        const auto p4 = std::get<3>(t) % (1ull << 32);
        // Build the significand, from most to least significant.
        // NOTE: unsigned long is guaranteed to be at least 32 bit.
        ::mpfr_set_ui(&this->m_mpfr, static_cast<unsigned long>(p1), MPFR_RNDN);
        ::mpfr_mul_2ui(&this->m_mpfr, &this->m_mpfr, 32ul, MPFR_RNDN);
        ::mpfr_add_ui(&this->m_mpfr, &this->m_mpfr, static_cast<unsigned long>(p2), MPFR_RNDN);
        ::mpfr_mul_2ui(&this->m_mpfr, &this->m_mpfr, 32ul, MPFR_RNDN);
        ::mpfr_add_ui(&this->m_mpfr, &this->m_mpfr, static_cast<unsigned long>(p3), MPFR_RNDN);
        ::mpfr_mul_2ui(&this->m_mpfr, &this->m_mpfr, 32ul, MPFR_RNDN);
        ::mpfr_add_ui(&this->m_mpfr, &this->m_mpfr, static_cast<unsigned long>(p4), MPFR_RNDN);
    };
    // Check if the significand is zero.
    const bool sig_zero = !std::get<2>(t) && !std::get<3>(t);
    if (std::get<1>(t) == 0u) {
        // Zero or subnormal numbers.
        if (sig_zero) {
            // Zero.
            ::mpfr_set_zero(&m_mpfr, 1);
        } else {
            // Subnormal.
            write_significand();
            ::mpfr_div_2ui(&m_mpfr, &m_mpfr, 16382ul + 112ul, MPFR_RNDN);
        }
    } else if (std::get<1>(t) == 32767u) {
        // NaN or inf.
        if (sig_zero) {
            // inf.
            ::mpfr_set_inf(&m_mpfr, 1);
        } else {
            // NaN.
            ::mpfr_set_nan(&m_mpfr);
        }
    } else {
        // Write the significand into this.
        write_significand();
        // Add the hidden bit on top.
        const auto r_2_112 = detail::get_real_2_112();
        ::mpfr_add(&m_mpfr, &m_mpfr, &r_2_112.first, MPFR_RNDN);
        // Multiply by 2 raised to the adjusted exponent.
        ::mpfr_mul_2si(&m_mpfr, &m_mpfr, static_cast<long>(std::get<1>(t)) - (16383l + 112), MPFR_RNDN);
    }
    if (std::get<0>(t)) {
        // Negate if the sign bit is set.
        ::mpfr_neg(&m_mpfr, &m_mpfr, MPFR_RNDN);
    }
}

real128 real::convert_to_real128() const
{
    // Handle the special cases first.
    if (nan_p()) {
        return real128_nan();
    }
    // NOTE: the number 2**18 = 262144 is chosen so that it's amply outside the exponent
    // range of real128 (a 15 bit value with some offset) but well within the
    // range of long (around +-2**31 guaranteed by the standard).
    //
    // NOTE: the reason why we do these checks with large positive and negative exponents
    // is that they ensure we can safely convert _mpfr_exp to long later.
    if (inf_p() || m_mpfr._mpfr_exp > (1l << 18)) {
        return sgn() > 0 ? real128_inf() : -real128_inf();
    }
    if (zero_p() || m_mpfr._mpfr_exp < -(1l << 18)) {
        // Preserve the signedness of zero.
        return signbit() ? -real128{} : real128{};
    }
    // NOTE: this is similar to the code in real128.hpp for the constructor from integer,
    // with some modification due to the different padding in MPFR vs GMP (see below).
    const auto prec = get_prec();
    // Number of limbs in this.
    auto nlimbs = prec / ::mp_bits_per_limb + static_cast<bool>(prec % ::mp_bits_per_limb);
    assert(nlimbs != 0);
    // NOTE: in MPFR the most significant (nonzero) bit of the significand
    // is always at the high end of the most significand limb. In other words,
    // MPFR pads the multiprecision significand on the right, the opposite
    // of GMP integers (which have padding on the left, i.e., in the top limb).
    //
    // NOTE: contrary to real128, the MPFR format does not have a hidden bit on top.
    //
    // Init retval with the highest limb.
    //
    // NOTE: MPFR does not support nail builds in GMP, so we don't have to worry about that.
    real128 retval{m_mpfr._mpfr_d[--nlimbs]};
    // Init the number of read bits.
    // NOTE: we have read a full limb in the line above, so mp_bits_per_limb bits. If mp_bits_per_limb > 113,
    // then the constructor of real128 truncated the input limb value to 113 bits of precision, so effectively
    // we have read 113 bits only in such a case.
    unsigned read_bits = detail::c_min(static_cast<unsigned>(::mp_bits_per_limb), real128_sig_digits());
    while (nlimbs && read_bits < real128_sig_digits()) {
        // Number of bits to be read from the current limb. Either mp_bits_per_limb or less.
        const unsigned rbits
            = detail::c_min(static_cast<unsigned>(::mp_bits_per_limb), real128_sig_digits() - read_bits);
        // Shift up by rbits.
        // NOTE: cast to int is safe, as rbits is no larger than mp_bits_per_limb which is
        // representable by int.
        retval = scalbn(retval, static_cast<int>(rbits));
        // Add the next limb, removing lower bits if they are not to be read.
        retval += m_mpfr._mpfr_d[--nlimbs] >> (static_cast<unsigned>(::mp_bits_per_limb) - rbits);
        // Update the number of read bits.
        // NOTE: due to the definition of rbits, read_bits can never reach past real128_sig_digits().
        // Hence, this addition can never overflow (as sig_digits is unsigned itself).
        read_bits += rbits;
    }
    // NOTE: from earlier we know the exponent is well within the range of long, and read_bits
    // cannot be larger than 113.
    retval = scalbln(retval, static_cast<long>(m_mpfr._mpfr_exp) - static_cast<long>(read_bits));
    return sgn() > 0 ? retval : -retval;
}

#endif

namespace detail
{

namespace
{

// NOTE: the cleanup machinery relies on a correct implementation
// of the thread_local keyword. If that is not available, we'll
// just skip the cleanup step altogether, which may result
// in "memory leaks" being reported by sanitizers and valgrind.
#if defined(MPPP_HAVE_THREAD_LOCAL)

#if MPFR_VERSION_MAJOR < 4

// A cleanup functor that will call mpfr_free_cache()
// on destruction.
struct mpfr_cleanup {
    // NOTE: marking the ctor constexpr ensures that the initialisation
    // of objects of this class with static storage duration is sequenced
    // before the dynamic initialisation of objects with static storage
    // duration:
    // https://en.cppreference.com/w/cpp/language/constant_initialization
    // This essentially means that we are sure that mpfr_free_cache()
    // will be called after the destruction of any static real object
    // (because real doesn't have a constexpr constructor and thus
    // static reals are destroyed before static objects of this class).
    constexpr mpfr_cleanup() {}
    ~mpfr_cleanup()
    {
#if !defined(NDEBUG)
        // NOTE: Access to cout from concurrent threads is safe as long as the
        // cout object is synchronized to the underlying C stream:
        // https://stackoverflow.com/questions/6374264/is-cout-synchronized-thread-safe
        // http://en.cppreference.com/w/cpp/io/ios_base/sync_with_stdio
        // By default, this is the case, but in theory someone might have changed
        // the sync setting on cout by the time we execute the following line.
        // However, we print only in debug mode, so it should not be too much of a problem
        // in practice.
        std::cout << "Cleaning up MPFR caches." << std::endl;
#endif
        ::mpfr_free_cache();
    }
};

// Instantiate a cleanup object for each thread.
thread_local const mpfr_cleanup cleanup_inst;

#else

// NOTE: in MPFR >= 4, there are both local caches and thread-specific caches.
// Thus, we use two cleanup functors, one thread local and one global.

struct mpfr_tl_cleanup {
    constexpr mpfr_tl_cleanup() {}
    ~mpfr_tl_cleanup()
    {
#if !defined(NDEBUG)
        std::cout << "Cleaning up thread local MPFR caches." << std::endl;
#endif
        ::mpfr_free_cache2(MPFR_FREE_LOCAL_CACHE);
    }
};

struct mpfr_global_cleanup {
    constexpr mpfr_global_cleanup() {}
    ~mpfr_global_cleanup()
    {
#if !defined(NDEBUG)
        std::cout << "Cleaning up global MPFR caches." << std::endl;
#endif
        ::mpfr_free_cache2(MPFR_FREE_GLOBAL_CACHE);
    }
};

thread_local const mpfr_tl_cleanup tl_cleanup_inst;
// NOTE: because the destruction of thread-local objects
// always happens before the destruction of objects with
// static storage duration, the global cleanup will always
// be performed after thread-local cleanup.
const mpfr_global_cleanup global_cleanup_inst;

#endif

#endif

} // namespace

} // namespace detail

/// Destructor.
/**
 * The destructor will free any resource held by the internal ``mpfr_t`` instance.
 */
real::~real()
{
#if defined(MPPP_HAVE_THREAD_LOCAL)
#if MPFR_VERSION_MAJOR < 4
    // NOTE: make sure we "use" the cleanup instantiation functor,
    // so that the compiler is forced to invoke its constructor.
    // This ensures that, as long as at least one real is created, the mpfr_free_cache()
    // function is called on shutdown.
    detail::ignore(&detail::cleanup_inst);
#else
    detail::ignore(&detail::tl_cleanup_inst);
    detail::ignore(&detail::global_cleanup_inst);
#endif
#endif
    if (m_mpfr._mpfr_d) {
        // The object is not moved-from, destroy it.
        assert(detail::real_prec_check(get_prec()));
        ::mpfr_clear(&m_mpfr);
    }
}

// Wrapper to apply the input unary MPFR function to this with
// MPFR_RNDN rounding mode. Returns a reference to this.
template <typename T>
real &real::self_mpfr_unary(T &&f)
{
    std::forward<T>(f)(&m_mpfr, &m_mpfr, MPFR_RNDN);
    return *this;
}

/// In-place Gamma function.
/**
 * This method will set ``this`` to its Gamma function.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::gamma()
{
    return self_mpfr_unary(::mpfr_gamma);
}

/// In-place logarithm of the Gamma function.
/**
 * This method will set ``this`` to the logarithm of its Gamma function.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::lngamma()
{
    return self_mpfr_unary(::mpfr_lngamma);
}

/// In-place logarithm of the absolute value of the Gamma function.
/**
 * This method will set ``this`` to the logarithm of the absolute value of its Gamma function.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::lgamma()
{
    detail::real_lgamma_wrapper(&m_mpfr, &m_mpfr, MPFR_RNDN);
    return *this;
}

/// In-place Digamma function.
/**
 * This method will set ``this`` to its Digamma function.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::digamma()
{
    return self_mpfr_unary(::mpfr_digamma);
}

/// In-place Bessel function of the first kind of order 0.
/**
 * This method will set ``this`` to its Bessel function of the first kind of order 0.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::j0()
{
    return self_mpfr_unary(::mpfr_j0);
}

/// In-place Bessel function of the first kind of order 1.
/**
 * This method will set ``this`` to its Bessel function of the first kind of order 1.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::j1()
{
    return self_mpfr_unary(::mpfr_j1);
}

/// In-place Bessel function of the second kind of order 0.
/**
 * This method will set ``this`` to its Bessel function of the second kind of order 0.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::y0()
{
    return self_mpfr_unary(::mpfr_y0);
}

/// In-place Bessel function of the second kind of order 1.
/**
 * This method will set ``this`` to its Bessel function of the second kind of order 1.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::y1()
{
    return self_mpfr_unary(::mpfr_y1);
}

/// In-place exponential integral.
/**
 * This method will set ``this`` to its exponential integral.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::eint()
{
    return self_mpfr_unary(::mpfr_eint);
}

/// In-place dilogarithm.
/**
 * This method will set ``this`` to its dilogarithm.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::li2()
{
    return self_mpfr_unary(::mpfr_li2);
}

/// In-place Riemann Zeta function.
/**
 * This method will set ``this`` to its Riemann Zeta function.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::zeta()
{
    return self_mpfr_unary(::mpfr_zeta);
}

/// In-place error function.
/**
 * This method will set ``this`` to its error function.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::erf()
{
    return self_mpfr_unary(::mpfr_erf);
}

/// In-place complementary error function.
/**
 * This method will set ``this`` to its complementary error function.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::erfc()
{
    return self_mpfr_unary(::mpfr_erfc);
}

/// In-place Airy function.
/**
 * This method will set ``this`` to its Airy function.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::ai()
{
    return self_mpfr_unary(::mpfr_ai);
}

/// Negate in-place.
/**
 * This method will set ``this`` to ``-this``.
 *
 * @return a reference to ``this``.
 */
real &real::neg()
{
    return self_mpfr_unary(::mpfr_neg);
}

/// In-place absolute value.
/**
 * This method will set ``this`` to its absolute value.
 *
 * @return a reference to ``this``.
 */
real &real::abs()
{
    return self_mpfr_unary(::mpfr_abs);
}

/// Destructively set the precision.
/**
 * \rststar
 * This method will set the precision of ``this`` to exactly ``p`` bits. The value
 * of ``this`` will be set to NaN.
 * \endrststar
 *
 * @param p the desired precision.
 *
 * @return a reference to \p this.
 *
 * @throws std::invalid_argument if the value of \p p is not in the range established by
 * \link mppp::real_prec_min() real_prec_min()\endlink and \link mppp::real_prec_max() real_prec_max()\endlink.
 */
real &real::set_prec(::mpfr_prec_t p)
{
    set_prec_impl<true>(p);
    return *this;
}

/// Set the precision maintaining the current value.
/**
 * \rststar
 * This method will set the precision of ``this`` to exactly ``p`` bits. If ``p``
 * is smaller than the current precision of ``this``, a rounding operation will be performed,
 * otherwise the value will be preserved exactly.
 * \endrststar
 *
 * @param p the desired precision.
 *
 * @return a reference to \p this.
 *
 * @throws std::invalid_argument if the value of \p p is not in the range established by
 * \link mppp::real_prec_min() real_prec_min()\endlink and \link mppp::real_prec_max() real_prec_max()\endlink.
 */
real &real::prec_round(::mpfr_prec_t p)
{
    prec_round_impl<true>(p);
    return *this;
}

// Convert to string.
std::string real::to_string(int base) const
{
    std::ostringstream oss;
    detail::mpfr_to_stream(&m_mpfr, oss, base);
    return oss.str();
}

/// In-place square root.
/**
 * This method will set ``this`` to its square root.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::sqrt()
{
    return self_mpfr_unary(::mpfr_sqrt);
}

/// In-place reciprocal square root.
/**
 * This method will set ``this`` to its reciprocal square root.
 * The precision of ``this`` will not be altered.
 *
 * If ``this`` is zero, the result will be a positive infinity (regardless of the sign of ``this``).
 * If ``this`` is a positive infinity, the result will be +0. If ``this`` is negative,
 * the result will be NaN.
 *
 * @return a reference to ``this``.
 */
real &real::rec_sqrt()
{
    return self_mpfr_unary(::mpfr_rec_sqrt);
}

/// In-place cubic root.
/**
 * This method will set ``this`` to its cubic root.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::cbrt()
{
    return self_mpfr_unary(::mpfr_cbrt);
}

/// In-place sine.
/**
 * This method will set ``this`` to its sine.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::sin()
{
    return self_mpfr_unary(::mpfr_sin);
}

/// In-place cosine.
/**
 * This method will set ``this`` to its cosine.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::cos()
{
    return self_mpfr_unary(::mpfr_cos);
}

/// In-place tangent.
/**
 * This method will set ``this`` to its tangent.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::tan()
{
    return self_mpfr_unary(::mpfr_tan);
}

/// In-place secant.
/**
 * This method will set ``this`` to its secant.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::sec()
{
    return self_mpfr_unary(::mpfr_sec);
}

/// In-place cosecant.
/**
 * This method will set ``this`` to its cosecant.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::csc()
{
    return self_mpfr_unary(::mpfr_csc);
}

/// In-place cotangent.
/**
 * This method will set ``this`` to its cotangent.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::cot()
{
    return self_mpfr_unary(::mpfr_cot);
}

/// In-place arccosine.
/**
 * This method will set ``this`` to its arccosine.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::acos()
{
    return self_mpfr_unary(::mpfr_acos);
}

/// In-place arcsine.
/**
 * This method will set ``this`` to its arcsine.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::asin()
{
    return self_mpfr_unary(::mpfr_asin);
}

/// In-place arctangent.
/**
 * This method will set ``this`` to its arctangent.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::atan()
{
    return self_mpfr_unary(::mpfr_atan);
}

/// In-place hyperbolic cosine.
/**
 * This method will set ``this`` to its hyperbolic cosine.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::cosh()
{
    return self_mpfr_unary(::mpfr_cosh);
}

/// In-place hyperbolic sine.
/**
 * This method will set ``this`` to its hyperbolic sine.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::sinh()
{
    return self_mpfr_unary(::mpfr_sinh);
}

/// In-place hyperbolic tangent.
/**
 * This method will set ``this`` to its hyperbolic tangent.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::tanh()
{
    return self_mpfr_unary(::mpfr_tanh);
}

/// In-place hyperbolic secant.
/**
 * This method will set ``this`` to its hyperbolic secant.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::sech()
{
    return self_mpfr_unary(::mpfr_sech);
}

/// In-place hyperbolic cosecant.
/**
 * This method will set ``this`` to its hyperbolic cosecant.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::csch()
{
    return self_mpfr_unary(::mpfr_csch);
}

/// In-place hyperbolic cotangent.
/**
 * This method will set ``this`` to its hyperbolic cotangent.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::coth()
{
    return self_mpfr_unary(::mpfr_coth);
}

/// In-place inverse hyperbolic cosine.
/**
 * This method will set ``this`` to its inverse hyperbolic cosine.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::acosh()
{
    return self_mpfr_unary(::mpfr_acosh);
}

/// In-place inverse hyperbolic sine.
/**
 * This method will set ``this`` to its inverse hyperbolic sine.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::asinh()
{
    return self_mpfr_unary(::mpfr_asinh);
}

/// In-place inverse hyperbolic tangent.
/**
 * This method will set ``this`` to its inverse hyperbolic tangent.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::atanh()
{
    return self_mpfr_unary(::mpfr_atanh);
}

/// In-place exponential.
/**
 * This method will set ``this`` to its exponential.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::exp()
{
    return self_mpfr_unary(::mpfr_exp);
}

/// In-place base-2 exponential.
/**
 * This method will set ``this`` to ``2**this``.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::exp2()
{
    return self_mpfr_unary(::mpfr_exp2);
}

/// In-place base-10 exponential.
/**
 * This method will set ``this`` to ``10**this``.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::exp10()
{
    return self_mpfr_unary(::mpfr_exp10);
}

/// In-place exponential minus 1.
/**
 * This method will set ``this`` to its exponential minus one.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::expm1()
{
    return self_mpfr_unary(::mpfr_expm1);
}

/// In-place logarithm.
/**
 * This method will set ``this`` to its logarithm.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::log()
{
    return self_mpfr_unary(::mpfr_log);
}

/// In-place base-2 logarithm.
/**
 * This method will set ``this`` to its base-2 logarithm.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::log2()
{
    return self_mpfr_unary(::mpfr_log2);
}

/// In-place base-10 logarithm.
/**
 * This method will set ``this`` to its base-10 logarithm.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::log10()
{
    return self_mpfr_unary(::mpfr_log10);
}

/// In-place augmented logarithm.
/**
 * This method will set ``this`` the logarithm of ``this + 1``.
 * The precision of ``this`` will not be altered.
 *
 * @return a reference to ``this``.
 */
real &real::log1p()
{
    return self_mpfr_unary(::mpfr_log1p);
}

/// Check if the value is an integer.
/**
 * @return ``true`` if ``this`` represents an integer value, ``false`` otherwise.
 */
bool real::integer_p() const
{
    return ::mpfr_integer_p(&m_mpfr) != 0;
}

/// In-place truncation.
/**
 * This method will set ``this`` to its truncated counterpart. The precision of ``this``
 * will not be altered.
 *
 * @return a reference to ``this``.
 *
 * @throws std::domain_error if ``this`` represents a NaN value.
 */
real &real::trunc()
{
    detail::real_check_trunc_arg(*this);
    ::mpfr_trunc(&m_mpfr, &m_mpfr);
    return *this;
}

} // namespace mppp
