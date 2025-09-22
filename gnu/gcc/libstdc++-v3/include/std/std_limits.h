// The template and inlines for the numeric_limits classes. -*- C++ -*- 

// Copyright (C) 1999, 2000, 2001, 2002, 2003, 2005 
// Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/** @file limits
 *  This is a Standard C++ Library header.
 */

// Note: this is not a conforming implementation.
// Written by Gabriel Dos Reis <gdr@codesourcery.com>

//
// ISO 14882:1998
// 18.2.1
//

#ifndef _GLIBCXX_NUMERIC_LIMITS
#define _GLIBCXX_NUMERIC_LIMITS 1

#pragma GCC system_header

#include <bits/c++config.h>

//
// The numeric_limits<> traits document implementation-defined aspects
// of fundamental arithmetic data types (integers and floating points).
// From Standard C++ point of view, there are 13 such types:
//   * integers
//         bool						        (1)
//         char, signed char, unsigned char			(3)
//         short, unsigned short				(2)
//         int, unsigned					(2)
//         long, unsigned long					(2)
//
//   * floating points
//         float						(1)
//         double						(1)
//         long double						(1)
//
// GNU C++ undertstands (where supported by the host C-library)
//   * integer
//         long long, unsigned long long			(2)
//
// which brings us to 15 fundamental arithmetic data types in GNU C++.
//
//
// Since a numeric_limits<> is a bit tricky to get right, we rely on
// an interface composed of macros which should be defined in config/os
// or config/cpu when they differ from the generic (read arbitrary)
// definitions given here.
//

// These values can be overridden in the target configuration file.
// The default values are appropriate for many 32-bit targets.

// GCC only intrinsicly supports modulo integral types.  The only remaining
// integral exceptional values is division by zero.  Only targets that do not
// signal division by zero in some "hard to ignore" way should use false.
#ifndef __glibcxx_integral_traps
# define __glibcxx_integral_traps true
#endif

// float
//

// Default values.  Should be overriden in configuration files if necessary.

#ifndef __glibcxx_float_has_denorm_loss
#  define __glibcxx_float_has_denorm_loss false
#endif
#ifndef __glibcxx_float_traps
#  define __glibcxx_float_traps false
#endif
#ifndef __glibcxx_float_tinyness_before
#  define __glibcxx_float_tinyness_before false
#endif

// double

// Default values.  Should be overriden in configuration files if necessary.

#ifndef __glibcxx_double_has_denorm_loss
#  define __glibcxx_double_has_denorm_loss false
#endif
#ifndef __glibcxx_double_traps
#  define __glibcxx_double_traps false
#endif
#ifndef __glibcxx_double_tinyness_before
#  define __glibcxx_double_tinyness_before false
#endif

// long double

// Default values.  Should be overriden in configuration files if necessary.

#ifndef __glibcxx_long_double_has_denorm_loss
#  define __glibcxx_long_double_has_denorm_loss false
#endif
#ifndef __glibcxx_long_double_traps
#  define __glibcxx_long_double_traps false
#endif
#ifndef __glibcxx_long_double_tinyness_before
#  define __glibcxx_long_double_tinyness_before false
#endif

// You should not need to define any macros below this point.

#define __glibcxx_signed(T)	((T)(-1) < 0)

#define __glibcxx_min(T) \
  (__glibcxx_signed (T) ? (T)1 << __glibcxx_digits (T) : (T)0)

#define __glibcxx_max(T) \
  (__glibcxx_signed (T) ? \
      (((((T)1 << (__glibcxx_digits (T) - 1)) - 1) << 1) + 1) : ~(T)0)


#define __glibcxx_digits(T) \
  (sizeof(T) * __CHAR_BIT__ - __glibcxx_signed (T))

// The fraction 643/2136 approximates log10(2) to 7 significant digits.
#define __glibcxx_digits10(T) \
  (__glibcxx_digits (T) * 643 / 2136)


_GLIBCXX_BEGIN_NAMESPACE(std)

  /**
   *  @brief Describes the rounding style for floating-point types.
   *
   *  This is used in the std::numeric_limits class.
  */
  enum float_round_style
  {
    round_indeterminate       = -1,    ///< Self-explanatory.
    round_toward_zero         = 0,     ///< Self-explanatory.
    round_to_nearest          = 1,     ///< To the nearest representable value.
    round_toward_infinity     = 2,     ///< Self-explanatory.
    round_toward_neg_infinity = 3      ///< Self-explanatory.
  };

  /**
   *  @brief Describes the denormalization for floating-point types.
   *
   *  These values represent the presence or absence of a variable number
   *  of exponent bits.  This type is used in the std::numeric_limits class.
  */
  enum float_denorm_style
  {
    /// Indeterminate at compile time whether denormalized values are allowed.
    denorm_indeterminate = -1,
    /// The type does not allow denormalized values.
    denorm_absent        = 0,
    /// The type allows denormalized values.
    denorm_present       = 1
  };

  /**
   *  @brief Part of std::numeric_limits.
   *
   *  The @c static @c const members are usable as integral constant
   *  expressions.
   *
   *  @note This is a seperate class for purposes of efficiency; you
   *        should only access these members as part of an instantiation
   *        of the std::numeric_limits class.
  */
  struct __numeric_limits_base
  {
    /** This will be true for all fundamental types (which have
        specializations), and false for everything else.  */
    static const bool is_specialized = false;

    /** The number of @c radix digits that be represented without change:  for
        integer types, the number of non-sign bits in the mantissa; for
        floating types, the number of @c radix digits in the mantissa.  */
    static const int digits = 0;
    /** The number of base 10 digits that can be represented without change. */
    static const int digits10 = 0;
    /** True if the type is signed.  */
    static const bool is_signed = false;
    /** True if the type is integer.
     *  @if maint
     *  Is this supposed to be "if the type is integral"?
     *  @endif
    */
    static const bool is_integer = false;
    /** True if the type uses an exact representation.  "All integer types are
        exact, but not all exact types are integer.  For example, rational and
        fixed-exponent representations are exact but not integer."
        [18.2.1.2]/15  */
    static const bool is_exact = false;
    /** For integer types, specifies the base of the representation.  For
        floating types, specifies the base of the exponent representation.  */
    static const int radix = 0;

    /** The minimum negative integer such that @c radix raised to the power of
        (one less than that integer) is a normalized floating point number.  */
    static const int min_exponent = 0;
    /** The minimum negative integer such that 10 raised to that power is in
        the range of normalized floating point numbers.  */
    static const int min_exponent10 = 0;
    /** The maximum positive integer such that @c radix raised to the power of
        (one less than that integer) is a representable finite floating point
	number.  */
    static const int max_exponent = 0;
    /** The maximum positive integer such that 10 raised to that power is in
        the range of representable finite floating point numbers.  */
    static const int max_exponent10 = 0;

    /** True if the type has a representation for positive infinity.  */
    static const bool has_infinity = false;
    /** True if the type has a representation for a quiet (non-signaling)
        "Not a Number."  */
    static const bool has_quiet_NaN = false;
    /** True if the type has a representation for a signaling
        "Not a Number."  */
    static const bool has_signaling_NaN = false;
    /** See std::float_denorm_style for more information.  */
    static const float_denorm_style has_denorm = denorm_absent;
    /** "True if loss of accuracy is detected as a denormalization loss,
        rather than as an inexact result." [18.2.1.2]/42  */
    static const bool has_denorm_loss = false;

    /** True if-and-only-if the type adheres to the IEC 559 standard, also
        known as IEEE 754.  (Only makes sense for floating point types.)  */
    static const bool is_iec559 = false;
    /** "True if the set of values representable by the type is finite.   All
        built-in types are bounded, this member would be false for arbitrary
	precision types." [18.2.1.2]/54  */
    static const bool is_bounded = false;
    /** True if the type is @e modulo, that is, if it is possible to add two
        positive numbers and have a result that wraps around to a third number
        that is less.  Typically false for floating types, true for unsigned
        integers, and true for signed integers.  */
    static const bool is_modulo = false;

    /** True if trapping is implemented for this type.  */
    static const bool traps = false;
    /** True if tinyness is detected before rounding.  (see IEC 559)  */
    static const bool tinyness_before = false;
    /** See std::float_round_style for more information.  This is only
        meaningful for floating types; integer types will all be
	round_toward_zero.  */
    static const float_round_style round_style = round_toward_zero;
  };

  /**
   *  @brief Properties of fundamental types.
   *
   *  This class allows a program to obtain information about the
   *  representation of a fundamental type on a given platform.  For
   *  non-fundamental types, the functions will return 0 and the data
   *  members will all be @c false.
   *
   *  @if maint
   *  _GLIBCXX_RESOLVE_LIB_DEFECTS:  DRs 201 and 184 (hi Gaby!) are
   *  noted, but not incorporated in this documented (yet).
   *  @endif
  */
  template<typename _Tp>
    struct numeric_limits : public __numeric_limits_base
    {
      /** The minimum finite value, or for floating types with
          denormalization, the minimum positive normalized value.  */
      static _Tp min() throw() { return static_cast<_Tp>(0); }
      /** The maximum finite value.  */
      static _Tp max() throw() { return static_cast<_Tp>(0); }
      /** The @e machine @e epsilon:  the difference between 1 and the least
          value greater than 1 that is representable.  */
      static _Tp epsilon() throw() { return static_cast<_Tp>(0); }
      /** The maximum rounding error measurement (see LIA-1).  */
      static _Tp round_error() throw() { return static_cast<_Tp>(0); }
      /** The representation of positive infinity, if @c has_infinity.  */
      static _Tp infinity() throw()  { return static_cast<_Tp>(0); }
      /** The representation of a quiet "Not a Number," if @c has_quiet_NaN. */
      static _Tp quiet_NaN() throw() { return static_cast<_Tp>(0); }
      /** The representation of a signaling "Not a Number," if
          @c has_signaling_NaN. */
      static _Tp signaling_NaN() throw() { return static_cast<_Tp>(0); }
      /** The minimum positive denormalized value.  For types where
          @c has_denorm is false, this is the minimum positive normalized
	  value.  */
      static _Tp denorm_min() throw() { return static_cast<_Tp>(0); }
    };

  // Now there follow 15 explicit specializations.  Yes, 15.  Make sure
  // you get the count right.

  /// numeric_limits<bool> specialization.
  template<>
    struct numeric_limits<bool>
    {
      static const bool is_specialized = true;

      static bool min() throw()
      { return false; }
      static bool max() throw()
      { return true; }

      static const int digits = 1;
      static const int digits10 = 0;
      static const bool is_signed = false;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static bool epsilon() throw()
      { return false; }
      static bool round_error() throw()
      { return false; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static bool infinity() throw()
      { return false; }
      static bool quiet_NaN() throw()
      { return false; }
      static bool signaling_NaN() throw()
      { return false; }
      static bool denorm_min() throw()
      { return false; }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = false;

      // It is not clear what it means for a boolean type to trap.
      // This is a DR on the LWG issue list.  Here, I use integer
      // promotion semantics.
      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<signed char> specialization.
  template<>
    struct numeric_limits<signed char>
    {
      static const bool is_specialized = true;

      static signed char min() throw()
      { return -__SCHAR_MAX__ - 1; }
      static signed char max() throw()
      { return __SCHAR_MAX__; }

      static const int digits = __glibcxx_digits (signed char);
      static const int digits10 = __glibcxx_digits10 (signed char);
      static const bool is_signed = true;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static signed char epsilon() throw()
      { return 0; }
      static signed char round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static signed char infinity() throw()
      { return static_cast<signed char>(0); }
      static signed char quiet_NaN() throw()
      { return static_cast<signed char>(0); }
      static signed char signaling_NaN() throw()
      { return static_cast<signed char>(0); }
      static signed char denorm_min() throw()
      { return static_cast<signed char>(0); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<unsigned char> specialization.
  template<>
    struct numeric_limits<unsigned char>
    {
      static const bool is_specialized = true;

      static unsigned char min() throw()
      { return 0; }
      static unsigned char max() throw()
      { return __SCHAR_MAX__ * 2U + 1; }

      static const int digits = __glibcxx_digits (unsigned char);
      static const int digits10 = __glibcxx_digits10 (unsigned char);
      static const bool is_signed = false;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static unsigned char epsilon() throw()
      { return 0; }
      static unsigned char round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static unsigned char infinity() throw()
      { return static_cast<unsigned char>(0); }
      static unsigned char quiet_NaN() throw()
      { return static_cast<unsigned char>(0); }
      static unsigned char signaling_NaN() throw()
      { return static_cast<unsigned char>(0); }
      static unsigned char denorm_min() throw()
      { return static_cast<unsigned char>(0); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<char> specialization.
  template<>
    struct numeric_limits<char>
    {
      static const bool is_specialized = true;

      static char min() throw()
      { return __glibcxx_signed(char) ? 
	    numeric_limits<signed char>::min() :
	    numeric_limits<unsigned char>::min(); }
      static char max() throw()
      { return __glibcxx_signed(char) ? 
	    numeric_limits<signed char>::max() :
	    numeric_limits<unsigned char>::max(); }

      static const int digits = __glibcxx_digits (char);
      static const int digits10 = __glibcxx_digits10 (char);
      static const bool is_signed = __glibcxx_signed (char);
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static char epsilon() throw()
      { return 0; }
      static char round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static char infinity() throw()
      { return char(); }
      static char quiet_NaN() throw()
      { return char(); }
      static char signaling_NaN() throw()
      { return char(); }
      static char denorm_min() throw()
      { return static_cast<char>(0); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<wchar_t> specialization.
  template<>
    struct numeric_limits<wchar_t>
    {
      static const bool is_specialized = true;

      static wchar_t min() throw()
      { return __glibcxx_min (wchar_t); }
      static wchar_t max() throw()
      { return __glibcxx_max (wchar_t); }

      static const int digits = __glibcxx_digits (wchar_t);
      static const int digits10 = __glibcxx_digits10 (wchar_t);
      static const bool is_signed = __glibcxx_signed (wchar_t);
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static wchar_t epsilon() throw()
      { return 0; }
      static wchar_t round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static wchar_t infinity() throw()
      { return wchar_t(); }
      static wchar_t quiet_NaN() throw()
      { return wchar_t(); }
      static wchar_t signaling_NaN() throw()
      { return wchar_t(); }
      static wchar_t denorm_min() throw()
      { return wchar_t(); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<short> specialization.
  template<>
    struct numeric_limits<short>
    {
      static const bool is_specialized = true;

      static short min() throw()
      { return -__SHRT_MAX__ - 1; }
      static short max() throw()
      { return __SHRT_MAX__; }

      static const int digits = __glibcxx_digits (short);
      static const int digits10 = __glibcxx_digits10 (short);
      static const bool is_signed = true;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static short epsilon() throw()
      { return 0; }
      static short round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static short infinity() throw()
      { return short(); }
      static short quiet_NaN() throw()
      { return short(); }
      static short signaling_NaN() throw()
      { return short(); }
      static short denorm_min() throw()
      { return short(); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<unsigned short> specialization.
  template<>
    struct numeric_limits<unsigned short>
    {
      static const bool is_specialized = true;

      static unsigned short min() throw()
      { return 0; }
      static unsigned short max() throw()
      { return __SHRT_MAX__ * 2U + 1; }

      static const int digits = __glibcxx_digits (unsigned short);
      static const int digits10 = __glibcxx_digits10 (unsigned short);
      static const bool is_signed = false;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static unsigned short epsilon() throw()
      { return 0; }
      static unsigned short round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static unsigned short infinity() throw()
      { return static_cast<unsigned short>(0); }
      static unsigned short quiet_NaN() throw()
      { return static_cast<unsigned short>(0); }
      static unsigned short signaling_NaN() throw()
      { return static_cast<unsigned short>(0); }
      static unsigned short denorm_min() throw()
      { return static_cast<unsigned short>(0); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<int> specialization.
  template<>
    struct numeric_limits<int>
    {
      static const bool is_specialized = true;

      static int min() throw()
      { return -__INT_MAX__ - 1; }
      static int max() throw()
      { return __INT_MAX__; }

      static const int digits = __glibcxx_digits (int);
      static const int digits10 = __glibcxx_digits10 (int);
      static const bool is_signed = true;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static int epsilon() throw()
      { return 0; }
      static int round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static int infinity() throw()
      { return static_cast<int>(0); }
      static int quiet_NaN() throw()
      { return static_cast<int>(0); }
      static int signaling_NaN() throw()
      { return static_cast<int>(0); }
      static int denorm_min() throw()
      { return static_cast<int>(0); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<unsigned int> specialization.
  template<>
    struct numeric_limits<unsigned int>
    {
      static const bool is_specialized = true;

      static unsigned int min() throw()
      { return 0; }
      static unsigned int max() throw()
      { return __INT_MAX__ * 2U + 1; }

      static const int digits = __glibcxx_digits (unsigned int);
      static const int digits10 = __glibcxx_digits10 (unsigned int);
      static const bool is_signed = false;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static unsigned int epsilon() throw()
      { return 0; }
      static unsigned int round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static unsigned int infinity() throw()
      { return static_cast<unsigned int>(0); }
      static unsigned int quiet_NaN() throw()
      { return static_cast<unsigned int>(0); }
      static unsigned int signaling_NaN() throw()
      { return static_cast<unsigned int>(0); }
      static unsigned int denorm_min() throw()
      { return static_cast<unsigned int>(0); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<long> specialization.
  template<>
    struct numeric_limits<long>
    {
      static const bool is_specialized = true;

      static long min() throw()
      { return -__LONG_MAX__ - 1; }
      static long max() throw()
      { return __LONG_MAX__; }

      static const int digits = __glibcxx_digits (long);
      static const int digits10 = __glibcxx_digits10 (long);
      static const bool is_signed = true;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static long epsilon() throw()
      { return 0; }
      static long round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static long infinity() throw()
      { return static_cast<long>(0); }
      static long quiet_NaN() throw()
      { return static_cast<long>(0); }
      static long signaling_NaN() throw()
      { return static_cast<long>(0); }
      static long denorm_min() throw()
      { return static_cast<long>(0); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<unsigned long> specialization.
  template<>
    struct numeric_limits<unsigned long>
    {
      static const bool is_specialized = true;

      static unsigned long min() throw()
      { return 0; }
      static unsigned long max() throw()
      { return __LONG_MAX__ * 2UL + 1; }

      static const int digits = __glibcxx_digits (unsigned long);
      static const int digits10 = __glibcxx_digits10 (unsigned long);
      static const bool is_signed = false;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static unsigned long epsilon() throw()
      { return 0; }
      static unsigned long round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static unsigned long infinity() throw()
      { return static_cast<unsigned long>(0); }
      static unsigned long quiet_NaN() throw()
      { return static_cast<unsigned long>(0); }
      static unsigned long signaling_NaN() throw()
      { return static_cast<unsigned long>(0); }
      static unsigned long denorm_min() throw()
      { return static_cast<unsigned long>(0); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<long long> specialization.
  template<>
    struct numeric_limits<long long>
    {
      static const bool is_specialized = true;

      static long long min() throw()
      { return -__LONG_LONG_MAX__ - 1; }
      static long long max() throw()
      { return __LONG_LONG_MAX__; }

      static const int digits = __glibcxx_digits (long long);
      static const int digits10 = __glibcxx_digits10 (long long);
      static const bool is_signed = true;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static long long epsilon() throw()
      { return 0; }
      static long long round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static long long infinity() throw()
      { return static_cast<long long>(0); }
      static long long quiet_NaN() throw()
      { return static_cast<long long>(0); }
      static long long signaling_NaN() throw()
      { return static_cast<long long>(0); }
      static long long denorm_min() throw()
      { return static_cast<long long>(0); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<unsigned long long> specialization.
  template<>
    struct numeric_limits<unsigned long long>
    {
      static const bool is_specialized = true;

      static unsigned long long min() throw()
      { return 0; }
      static unsigned long long max() throw()
      { return __LONG_LONG_MAX__ * 2ULL + 1; }

      static const int digits = __glibcxx_digits (unsigned long long);
      static const int digits10 = __glibcxx_digits10 (unsigned long long);
      static const bool is_signed = false;
      static const bool is_integer = true;
      static const bool is_exact = true;
      static const int radix = 2;
      static unsigned long long epsilon() throw()
      { return 0; }
      static unsigned long long round_error() throw()
      { return 0; }

      static const int min_exponent = 0;
      static const int min_exponent10 = 0;
      static const int max_exponent = 0;
      static const int max_exponent10 = 0;

      static const bool has_infinity = false;
      static const bool has_quiet_NaN = false;
      static const bool has_signaling_NaN = false;
      static const float_denorm_style has_denorm = denorm_absent;
      static const bool has_denorm_loss = false;

      static unsigned long long infinity() throw()
      { return static_cast<unsigned long long>(0); }
      static unsigned long long quiet_NaN() throw()
      { return static_cast<unsigned long long>(0); }
      static unsigned long long signaling_NaN() throw()
      { return static_cast<unsigned long long>(0); }
      static unsigned long long denorm_min() throw()
      { return static_cast<unsigned long long>(0); }

      static const bool is_iec559 = false;
      static const bool is_bounded = true;
      static const bool is_modulo = true;

      static const bool traps = __glibcxx_integral_traps;
      static const bool tinyness_before = false;
      static const float_round_style round_style = round_toward_zero;
    };

  /// numeric_limits<float> specialization.
  template<>
    struct numeric_limits<float>
    {
      static const bool is_specialized = true;

      static float min() throw()
      { return __FLT_MIN__; }
      static float max() throw()
      { return __FLT_MAX__; }

      static const int digits = __FLT_MANT_DIG__;
      static const int digits10 = __FLT_DIG__;
      static const bool is_signed = true;
      static const bool is_integer = false;
      static const bool is_exact = false;
      static const int radix = __FLT_RADIX__;
      static float epsilon() throw()
      { return __FLT_EPSILON__; }
      static float round_error() throw()
      { return 0.5F; }

      static const int min_exponent = __FLT_MIN_EXP__;
      static const int min_exponent10 = __FLT_MIN_10_EXP__;
      static const int max_exponent = __FLT_MAX_EXP__;
      static const int max_exponent10 = __FLT_MAX_10_EXP__;

      static const bool has_infinity = __FLT_HAS_INFINITY__;
      static const bool has_quiet_NaN = __FLT_HAS_QUIET_NAN__;
      static const bool has_signaling_NaN = has_quiet_NaN;
      static const float_denorm_style has_denorm
	= bool(__FLT_HAS_DENORM__) ? denorm_present : denorm_absent;
      static const bool has_denorm_loss = __glibcxx_float_has_denorm_loss;

      static float infinity() throw()
      { return __builtin_huge_valf (); }
      static float quiet_NaN() throw()
      { return __builtin_nanf (""); }
      static float signaling_NaN() throw()
      { return __builtin_nansf (""); }
      static float denorm_min() throw()
      { return __FLT_DENORM_MIN__; }

      static const bool is_iec559
	= has_infinity && has_quiet_NaN && has_denorm == denorm_present;
      static const bool is_bounded = true;
      static const bool is_modulo = false;

      static const bool traps = __glibcxx_float_traps;
      static const bool tinyness_before = __glibcxx_float_tinyness_before;
      static const float_round_style round_style = round_to_nearest;
    };

#undef __glibcxx_float_has_denorm_loss
#undef __glibcxx_float_traps
#undef __glibcxx_float_tinyness_before

  /// numeric_limits<double> specialization.
  template<>
    struct numeric_limits<double>
    {
      static const bool is_specialized = true;

      static double min() throw()
      { return __DBL_MIN__; }
      static double max() throw()
      { return __DBL_MAX__; }

      static const int digits = __DBL_MANT_DIG__;
      static const int digits10 = __DBL_DIG__;
      static const bool is_signed = true;
      static const bool is_integer = false;
      static const bool is_exact = false;
      static const int radix = __FLT_RADIX__;
      static double epsilon() throw()
      { return __DBL_EPSILON__; }
      static double round_error() throw()
      { return 0.5; }

      static const int min_exponent = __DBL_MIN_EXP__;
      static const int min_exponent10 = __DBL_MIN_10_EXP__;
      static const int max_exponent = __DBL_MAX_EXP__;
      static const int max_exponent10 = __DBL_MAX_10_EXP__;

      static const bool has_infinity = __DBL_HAS_INFINITY__;
      static const bool has_quiet_NaN = __DBL_HAS_QUIET_NAN__;
      static const bool has_signaling_NaN = has_quiet_NaN;
      static const float_denorm_style has_denorm
	= bool(__DBL_HAS_DENORM__) ? denorm_present : denorm_absent;
      static const bool has_denorm_loss = __glibcxx_double_has_denorm_loss;

      static double infinity() throw()
      { return __builtin_huge_val(); }
      static double quiet_NaN() throw()
      { return __builtin_nan (""); }
      static double signaling_NaN() throw()
      { return __builtin_nans (""); }
      static double denorm_min() throw()
      { return __DBL_DENORM_MIN__; }

      static const bool is_iec559
	= has_infinity && has_quiet_NaN && has_denorm == denorm_present;
      static const bool is_bounded = true;
      static const bool is_modulo = false;

      static const bool traps = __glibcxx_double_traps;
      static const bool tinyness_before = __glibcxx_double_tinyness_before;
      static const float_round_style round_style = round_to_nearest;
    };

#undef __glibcxx_double_has_denorm_loss
#undef __glibcxx_double_traps
#undef __glibcxx_double_tinyness_before

  /// numeric_limits<long double> specialization.
  template<>
    struct numeric_limits<long double>
    {
      static const bool is_specialized = true;

      static long double min() throw()
      { return __LDBL_MIN__; }
      static long double max() throw()
      { return __LDBL_MAX__; }

      static const int digits = __LDBL_MANT_DIG__;
      static const int digits10 = __LDBL_DIG__;
      static const bool is_signed = true;
      static const bool is_integer = false;
      static const bool is_exact = false;
      static const int radix = __FLT_RADIX__;
      static long double epsilon() throw()
      { return __LDBL_EPSILON__; }
      static long double round_error() throw()
      { return 0.5L; }

      static const int min_exponent = __LDBL_MIN_EXP__;
      static const int min_exponent10 = __LDBL_MIN_10_EXP__;
      static const int max_exponent = __LDBL_MAX_EXP__;
      static const int max_exponent10 = __LDBL_MAX_10_EXP__;

      static const bool has_infinity = __LDBL_HAS_INFINITY__;
      static const bool has_quiet_NaN = __LDBL_HAS_QUIET_NAN__;
      static const bool has_signaling_NaN = has_quiet_NaN;
      static const float_denorm_style has_denorm
	= bool(__LDBL_HAS_DENORM__) ? denorm_present : denorm_absent;
      static const bool has_denorm_loss
	= __glibcxx_long_double_has_denorm_loss;

      static long double infinity() throw()
      { return __builtin_huge_vall (); }
      static long double quiet_NaN() throw()
      { return __builtin_nanl (""); }
      static long double signaling_NaN() throw()
      { return __builtin_nansl (""); }
      static long double denorm_min() throw()
      { return __LDBL_DENORM_MIN__; }

      static const bool is_iec559
	= has_infinity && has_quiet_NaN && has_denorm == denorm_present;
      static const bool is_bounded = true;
      static const bool is_modulo = false;

      static const bool traps = __glibcxx_long_double_traps;
      static const bool tinyness_before = __glibcxx_long_double_tinyness_before;
      static const float_round_style round_style = round_to_nearest;
    };

#undef __glibcxx_long_double_has_denorm_loss
#undef __glibcxx_long_double_traps
#undef __glibcxx_long_double_tinyness_before

_GLIBCXX_END_NAMESPACE

#undef __glibcxx_signed
#undef __glibcxx_min
#undef __glibcxx_max
#undef __glibcxx_digits
#undef __glibcxx_digits10

#endif // _GLIBCXX_NUMERIC_LIMITS
