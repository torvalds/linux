// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FORMAT_PARSER_STD_FORMAT_SPEC_H
#define _LIBCPP___FORMAT_PARSER_STD_FORMAT_SPEC_H

/// \file Contains the std-format-spec parser.
///
/// Most of the code can be reused in the chrono-format-spec.
/// This header has some support for the chrono-format-spec since it doesn't
/// affect the std-format-spec.

#include <__algorithm/copy_n.h>
#include <__algorithm/min.h>
#include <__assert>
#include <__concepts/arithmetic.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__format/format_arg.h>
#include <__format/format_error.h>
#include <__format/format_parse_context.h>
#include <__format/format_string.h>
#include <__format/unicode.h>
#include <__format/width_estimation_table.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h> // iter_value_t
#include <__memory/addressof.h>
#include <__type_traits/common_type.h>
#include <__type_traits/is_constant_evaluated.h>
#include <__type_traits/is_trivially_copyable.h>
#include <__variant/monostate.h>
#include <cstdint>
#include <string>
#include <string_view>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace __format_spec {

_LIBCPP_NORETURN _LIBCPP_HIDE_FROM_ABI inline void
__throw_invalid_option_format_error(const char* __id, const char* __option) {
  std::__throw_format_error(
      (string("The format specifier for ") + __id + " does not allow the " + __option + " option").c_str());
}

_LIBCPP_NORETURN _LIBCPP_HIDE_FROM_ABI inline void __throw_invalid_type_format_error(const char* __id) {
  std::__throw_format_error(
      (string("The type option contains an invalid value for ") + __id + " formatting argument").c_str());
}

template <contiguous_iterator _Iterator, class _ParseContext>
_LIBCPP_HIDE_FROM_ABI constexpr __format::__parse_number_result<_Iterator>
__parse_arg_id(_Iterator __begin, _Iterator __end, _ParseContext& __ctx) {
  using _CharT = iter_value_t<_Iterator>;
  // This function is a wrapper to call the real parser. But it does the
  // validation for the pre-conditions and post-conditions.
  if (__begin == __end)
    std::__throw_format_error("End of input while parsing an argument index");

  __format::__parse_number_result __r = __format::__parse_arg_id(__begin, __end, __ctx);

  if (__r.__last == __end || *__r.__last != _CharT('}'))
    std::__throw_format_error("The argument index is invalid");

  ++__r.__last;
  return __r;
}

template <class _Context>
_LIBCPP_HIDE_FROM_ABI constexpr uint32_t __substitute_arg_id(basic_format_arg<_Context> __format_arg) {
  // [format.string.std]/8
  //   If the corresponding formatting argument is not of integral type...
  // This wording allows char and bool too. LWG-3720 changes the wording to
  //    If the corresponding formatting argument is not of standard signed or
  //    unsigned integer type,
  // This means the 128-bit will not be valid anymore.
  // TODO FMT Verify this resolution is accepted and add a test to verify
  //          128-bit integrals fail and switch to visit_format_arg.
  return std::__visit_format_arg(
      [](auto __arg) -> uint32_t {
        using _Type = decltype(__arg);
        if constexpr (same_as<_Type, monostate>)
          std::__throw_format_error("The argument index value is too large for the number of arguments supplied");

        // [format.string.std]/8
        // If { arg-idopt } is used in a width or precision, the value of the
        // corresponding formatting argument is used in its place. If the
        // corresponding formatting argument is not of standard signed or unsigned
        // integer type, or its value is negative for precision or non-positive for
        // width, an exception of type format_error is thrown.
        //
        // When an integral is used in a format function, it is stored as one of
        // the types checked below. Other integral types are promoted. For example,
        // a signed char is stored as an int.
        if constexpr (same_as<_Type, int> || same_as<_Type, unsigned int> || //
                      same_as<_Type, long long> || same_as<_Type, unsigned long long>) {
          if constexpr (signed_integral<_Type>) {
            if (__arg < 0)
              std::__throw_format_error("An argument index may not have a negative value");
          }

          using _CT = common_type_t<_Type, decltype(__format::__number_max)>;
          if (static_cast<_CT>(__arg) > static_cast<_CT>(__format::__number_max))
            std::__throw_format_error("The value of the argument index exceeds its maximum value");

          return __arg;
        } else
          std::__throw_format_error("Replacement argument isn't a standard signed or unsigned integer type");
      },
      __format_arg);
}

/// These fields are a filter for which elements to parse.
///
/// They default to false so when a new field is added it needs to be opted in
/// explicitly.
struct _LIBCPP_HIDE_FROM_ABI __fields {
  uint16_t __sign_                 : 1 {false};
  uint16_t __alternate_form_       : 1 {false};
  uint16_t __zero_padding_         : 1 {false};
  uint16_t __precision_            : 1 {false};
  uint16_t __locale_specific_form_ : 1 {false};
  uint16_t __type_                 : 1 {false};
  // Determines the valid values for fill.
  //
  // Originally the fill could be any character except { and }. Range-based
  // formatters use the colon to mark the beginning of the
  // underlying-format-spec. To avoid parsing ambiguities these formatter
  // specializations prohibit the use of the colon as a fill character.
  uint16_t __use_range_fill_ : 1 {false};
  uint16_t __clear_brackets_ : 1 {false};
  uint16_t __consume_all_    : 1 {false};
};

// By not placing this constant in the formatter class it's not duplicated for
// char and wchar_t.
inline constexpr __fields __fields_bool{.__locale_specific_form_ = true, .__type_ = true, .__consume_all_ = true};
inline constexpr __fields __fields_integral{
    .__sign_                 = true,
    .__alternate_form_       = true,
    .__zero_padding_         = true,
    .__locale_specific_form_ = true,
    .__type_                 = true,
    .__consume_all_          = true};
inline constexpr __fields __fields_floating_point{
    .__sign_                 = true,
    .__alternate_form_       = true,
    .__zero_padding_         = true,
    .__precision_            = true,
    .__locale_specific_form_ = true,
    .__type_                 = true,
    .__consume_all_          = true};
inline constexpr __fields __fields_string{.__precision_ = true, .__type_ = true, .__consume_all_ = true};
inline constexpr __fields __fields_pointer{.__zero_padding_ = true, .__type_ = true, .__consume_all_ = true};

#  if _LIBCPP_STD_VER >= 23
inline constexpr __fields __fields_tuple{.__use_range_fill_ = true, .__clear_brackets_ = true};
inline constexpr __fields __fields_range{.__use_range_fill_ = true, .__clear_brackets_ = true};
inline constexpr __fields __fields_fill_align_width{};
#  endif

enum class __alignment : uint8_t {
  /// No alignment is set in the format string.
  __default,
  __left,
  __center,
  __right,
  __zero_padding
};

enum class __sign : uint8_t {
  /// No sign is set in the format string.
  ///
  /// The sign isn't allowed for certain format-types. By using this value
  /// it's possible to detect whether or not the user explicitly set the sign
  /// flag. For formatting purposes it behaves the same as \ref __minus.
  __default,
  __minus,
  __plus,
  __space
};

enum class __type : uint8_t {
  __default = 0,
  __string,
  __binary_lower_case,
  __binary_upper_case,
  __octal,
  __decimal,
  __hexadecimal_lower_case,
  __hexadecimal_upper_case,
  __pointer_lower_case,
  __pointer_upper_case,
  __char,
  __hexfloat_lower_case,
  __hexfloat_upper_case,
  __scientific_lower_case,
  __scientific_upper_case,
  __fixed_lower_case,
  __fixed_upper_case,
  __general_lower_case,
  __general_upper_case,
  __debug
};

_LIBCPP_HIDE_FROM_ABI inline constexpr uint32_t __create_type_mask(__type __t) {
  uint32_t __shift = static_cast<uint32_t>(__t);
  if (__shift == 0)
    return 1;

  if (__shift > 31)
    std::__throw_format_error("The type does not fit in the mask");

  return 1 << __shift;
}

inline constexpr uint32_t __type_mask_integer =
    __create_type_mask(__type::__binary_lower_case) |      //
    __create_type_mask(__type::__binary_upper_case) |      //
    __create_type_mask(__type::__decimal) |                //
    __create_type_mask(__type::__octal) |                  //
    __create_type_mask(__type::__hexadecimal_lower_case) | //
    __create_type_mask(__type::__hexadecimal_upper_case);

struct __std {
  __alignment __alignment_     : 3;
  __sign __sign_               : 2;
  bool __alternate_form_       : 1;
  bool __locale_specific_form_ : 1;
  __type __type_;
};

struct __chrono {
  __alignment __alignment_     : 3;
  bool __locale_specific_form_ : 1;
  bool __hour_                 : 1;
  bool __weekday_name_         : 1;
  bool __weekday_              : 1;
  bool __day_of_year_          : 1;
  bool __week_of_year_         : 1;
  bool __month_name_           : 1;
};

// The fill UCS scalar value.
//
// This is always an array, with 1, 2, or 4 elements.
// The size of the data structure is always 32-bits.
template <class _CharT>
struct __code_point;

template <>
struct __code_point<char> {
  char __data[4] = {' '};
};

#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <>
struct __code_point<wchar_t> {
  wchar_t __data[4 / sizeof(wchar_t)] = {L' '};
};
#  endif

/// Contains the parsed formatting specifications.
///
/// This contains information for both the std-format-spec and the
/// chrono-format-spec. This results in some unused members for both
/// specifications. However these unused members don't increase the size
/// of the structure.
///
/// This struct doesn't cross ABI boundaries so its layout doesn't need to be
/// kept stable.
template <class _CharT>
struct __parsed_specifications {
  union {
    // The field __alignment_ is the first element in __std_ and __chrono_.
    // This allows the code to always inspect this value regards which member
    // of the union is the active member [class.union.general]/2.
    //
    // This is needed since the generic output routines handle the alignment of
    // the output.
    __alignment __alignment_ : 3;
    __std __std_;
    __chrono __chrono_;
  };

  /// The requested width.
  ///
  /// When the format-spec used an arg-id for this field it has already been
  /// replaced with the value of that arg-id.
  int32_t __width_;

  /// The requested precision.
  ///
  /// When the format-spec used an arg-id for this field it has already been
  /// replaced with the value of that arg-id.
  int32_t __precision_;

  __code_point<_CharT> __fill_;

  _LIBCPP_HIDE_FROM_ABI constexpr bool __has_width() const { return __width_ > 0; }

  _LIBCPP_HIDE_FROM_ABI constexpr bool __has_precision() const { return __precision_ >= 0; }
};

// Validate the struct is small and cheap to copy since the struct is passed by
// value in formatting functions.
static_assert(sizeof(__parsed_specifications<char>) == 16);
static_assert(is_trivially_copyable_v<__parsed_specifications<char>>);
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
static_assert(sizeof(__parsed_specifications<wchar_t>) == 16);
static_assert(is_trivially_copyable_v<__parsed_specifications<wchar_t>>);
#  endif

/// The parser for the std-format-spec.
///
/// Note this class is a member of std::formatter specializations. It's
/// expected developers will create their own formatter specializations that
/// inherit from the std::formatter specializations. This means this class
/// must be ABI stable. To aid the stability the unused bits in the class are
/// set to zero. That way they can be repurposed if a future revision of the
/// Standards adds new fields to std-format-spec.
template <class _CharT>
class _LIBCPP_TEMPLATE_VIS __parser {
public:
  // Parses the format specification.
  //
  // Depending on whether the parsing is done compile-time or run-time
  // the method slightly differs.
  // - Only parses a field when it is in the __fields. Accepting all
  //   fields and then validating the valid ones has a performance impact.
  //   This is faster but gives slighly worse error messages.
  // - At compile-time when a field is not accepted the parser will still
  //   parse it and give an error when it's present. This gives a more
  //   accurate error.
  // The idea is that most times the format instead of the vformat
  // functions are used. In that case the error will be detected during
  // compilation and there is no need to pay for the run-time overhead.
  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator __parse(_ParseContext& __ctx, __fields __fields) {
    auto __begin = __ctx.begin();
    auto __end   = __ctx.end();
    if (__begin == __end || *__begin == _CharT('}') || (__fields.__use_range_fill_ && *__begin == _CharT(':')))
      return __begin;

    if (__parse_fill_align(__begin, __end) && __begin == __end)
      return __begin;

    if (__fields.__sign_) {
      if (__parse_sign(__begin) && __begin == __end)
        return __begin;
    } else if (std::is_constant_evaluated() && __parse_sign(__begin)) {
      std::__throw_format_error("The format specification does not allow the sign option");
    }

    if (__fields.__alternate_form_) {
      if (__parse_alternate_form(__begin) && __begin == __end)
        return __begin;
    } else if (std::is_constant_evaluated() && __parse_alternate_form(__begin)) {
      std::__throw_format_error("The format specifier does not allow the alternate form option");
    }

    if (__fields.__zero_padding_) {
      if (__parse_zero_padding(__begin) && __begin == __end)
        return __begin;
    } else if (std::is_constant_evaluated() && __parse_zero_padding(__begin)) {
      std::__throw_format_error("The format specifier does not allow the zero-padding option");
    }

    if (__parse_width(__begin, __end, __ctx) && __begin == __end)
      return __begin;

    if (__fields.__precision_) {
      if (__parse_precision(__begin, __end, __ctx) && __begin == __end)
        return __begin;
    } else if (std::is_constant_evaluated() && __parse_precision(__begin, __end, __ctx)) {
      std::__throw_format_error("The format specifier does not allow the precision option");
    }

    if (__fields.__locale_specific_form_) {
      if (__parse_locale_specific_form(__begin) && __begin == __end)
        return __begin;
    } else if (std::is_constant_evaluated() && __parse_locale_specific_form(__begin)) {
      std::__throw_format_error("The format specifier does not allow the locale-specific form option");
    }

    if (__fields.__clear_brackets_) {
      if (__parse_clear_brackets(__begin) && __begin == __end)
        return __begin;
    } else if (std::is_constant_evaluated() && __parse_clear_brackets(__begin)) {
      std::__throw_format_error("The format specifier does not allow the n option");
    }

    if (__fields.__type_)
      __parse_type(__begin);

    if (!__fields.__consume_all_)
      return __begin;

    if (__begin != __end && *__begin != _CharT('}'))
      std::__throw_format_error("The format specifier should consume the input or end with a '}'");

    return __begin;
  }

  // Validates the selected the parsed data.
  //
  // The valid fields in the parser may depend on the display type
  // selected. But the type is the last optional field, so by the time
  // it's known an option can't be used, it already has been parsed.
  // This does the validation again.
  //
  // For example an integral may have a sign, zero-padding, or alternate
  // form when the type option is not 'c'. So the generic approach is:
  //
  // typename _ParseContext::iterator __result = __parser_.__parse(__ctx, __format_spec::__fields_integral);
  // if (__parser.__type_ == __format_spec::__type::__char) {
  //   __parser.__validate((__format_spec::__fields_bool, "an integer");
  //   ... // more char adjustments
  // } else {
  //   ... // validate an integral type.
  // }
  //
  // For some types all valid options need a second validation run, like
  // boolean types.
  //
  // Depending on whether the validation is done at compile-time or
  // run-time the error differs
  // - run-time the exception is thrown and contains the type of field
  //   being validated.
  // - at compile-time the line with `std::__throw_format_error` is shown
  //   in the output. In that case it's important for the error to be on one
  //   line.
  // Note future versions of C++ may allow better compile-time error
  // reporting.
  _LIBCPP_HIDE_FROM_ABI constexpr void
  __validate(__fields __fields, const char* __id, uint32_t __type_mask = -1) const {
    if (!__fields.__sign_ && __sign_ != __sign::__default) {
      if (std::is_constant_evaluated())
        std::__throw_format_error("The format specifier does not allow the sign option");
      else
        __format_spec::__throw_invalid_option_format_error(__id, "sign");
    }

    if (!__fields.__alternate_form_ && __alternate_form_) {
      if (std::is_constant_evaluated())
        std::__throw_format_error("The format specifier does not allow the alternate form option");
      else
        __format_spec::__throw_invalid_option_format_error(__id, "alternate form");
    }

    if (!__fields.__zero_padding_ && __alignment_ == __alignment::__zero_padding) {
      if (std::is_constant_evaluated())
        std::__throw_format_error("The format specifier does not allow the zero-padding option");
      else
        __format_spec::__throw_invalid_option_format_error(__id, "zero-padding");
    }

    if (!__fields.__precision_ && __precision_ != -1) { // Works both when the precision has a value or an arg-id.
      if (std::is_constant_evaluated())
        std::__throw_format_error("The format specifier does not allow the precision option");
      else
        __format_spec::__throw_invalid_option_format_error(__id, "precision");
    }

    if (!__fields.__locale_specific_form_ && __locale_specific_form_) {
      if (std::is_constant_evaluated())
        std::__throw_format_error("The format specifier does not allow the locale-specific form option");
      else
        __format_spec::__throw_invalid_option_format_error(__id, "locale-specific form");
    }

    if ((__create_type_mask(__type_) & __type_mask) == 0) {
      if (std::is_constant_evaluated())
        std::__throw_format_error("The format specifier uses an invalid value for the type option");
      else
        __format_spec::__throw_invalid_type_format_error(__id);
    }
  }

  /// \returns the `__parsed_specifications` with the resolved dynamic sizes..
  _LIBCPP_HIDE_FROM_ABI __parsed_specifications<_CharT> __get_parsed_std_specifications(auto& __ctx) const {
    return __parsed_specifications<_CharT>{
        .__std_ = __std{.__alignment_            = __alignment_,
                        .__sign_                 = __sign_,
                        .__alternate_form_       = __alternate_form_,
                        .__locale_specific_form_ = __locale_specific_form_,
                        .__type_                 = __type_},
        .__width_{__get_width(__ctx)},
        .__precision_{__get_precision(__ctx)},
        .__fill_{__fill_}};
  }

  _LIBCPP_HIDE_FROM_ABI __parsed_specifications<_CharT> __get_parsed_chrono_specifications(auto& __ctx) const {
    return __parsed_specifications<_CharT>{
        .__chrono_ =
            __chrono{.__alignment_            = __alignment_,
                     .__locale_specific_form_ = __locale_specific_form_,
                     .__hour_                 = __hour_,
                     .__weekday_name_         = __weekday_name_,
                     .__weekday_              = __weekday_,
                     .__day_of_year_          = __day_of_year_,
                     .__week_of_year_         = __week_of_year_,
                     .__month_name_           = __month_name_},
        .__width_{__get_width(__ctx)},
        .__precision_{__get_precision(__ctx)},
        .__fill_{__fill_}};
  }

  __alignment __alignment_     : 3 {__alignment::__default};
  __sign __sign_               : 2 {__sign::__default};
  bool __alternate_form_       : 1 {false};
  bool __locale_specific_form_ : 1 {false};
  bool __clear_brackets_       : 1 {false};
  __type __type_{__type::__default};

  // These flags are only used for formatting chrono. Since the struct has
  // padding space left it's added to this structure.
  bool __hour_ : 1 {false};

  bool __weekday_name_ : 1 {false};
  bool __weekday_      : 1 {false};

  bool __day_of_year_  : 1 {false};
  bool __week_of_year_ : 1 {false};

  bool __month_name_ : 1 {false};

  uint8_t __reserved_0_ : 2 {0};
  uint8_t __reserved_1_ : 6 {0};
  // These two flags are only used internally and not part of the
  // __parsed_specifications. Therefore put them at the end.
  bool __width_as_arg_     : 1 {false};
  bool __precision_as_arg_ : 1 {false};

  /// The requested width, either the value or the arg-id.
  int32_t __width_{0};

  /// The requested precision, either the value or the arg-id.
  int32_t __precision_{-1};

  __code_point<_CharT> __fill_{};

private:
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_alignment(_CharT __c) {
    switch (__c) {
    case _CharT('<'):
      __alignment_ = __alignment::__left;
      return true;

    case _CharT('^'):
      __alignment_ = __alignment::__center;
      return true;

    case _CharT('>'):
      __alignment_ = __alignment::__right;
      return true;
    }
    return false;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr void __validate_fill_character(_CharT __fill) {
    // The forbidden fill characters all code points formed from a single code unit, thus the
    // check can be omitted when more code units are used.
    if (__fill == _CharT('{'))
      std::__throw_format_error("The fill option contains an invalid value");
  }

#  ifndef _LIBCPP_HAS_NO_UNICODE
  // range-fill and tuple-fill are identical
  template <contiguous_iterator _Iterator>
    requires same_as<_CharT, char>
#    ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
          || (same_as<_CharT, wchar_t> && sizeof(wchar_t) == 2)
#    endif
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_fill_align(_Iterator& __begin, _Iterator __end) {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __begin != __end,
        "when called with an empty input the function will cause "
        "undefined behavior by evaluating data not in the input");
    __unicode::__code_point_view<_CharT> __view{__begin, __end};
    __unicode::__consume_result __consumed = __view.__consume();
    if (__consumed.__status != __unicode::__consume_result::__ok)
      std::__throw_format_error("The format specifier contains malformed Unicode characters");

    if (__view.__position() < __end && __parse_alignment(*__view.__position())) {
      ptrdiff_t __code_units = __view.__position() - __begin;
      if (__code_units == 1)
        // The forbidden fill characters all are code points encoded
        // in one code unit, thus the check can be omitted when more
        // code units are used.
        __validate_fill_character(*__begin);

      std::copy_n(__begin, __code_units, std::addressof(__fill_.__data[0]));
      __begin += __code_units + 1;
      return true;
    }

    if (!__parse_alignment(*__begin))
      return false;

    ++__begin;
    return true;
  }

#    ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
  template <contiguous_iterator _Iterator>
    requires(same_as<_CharT, wchar_t> && sizeof(wchar_t) == 4)
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_fill_align(_Iterator& __begin, _Iterator __end) {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __begin != __end,
        "when called with an empty input the function will cause "
        "undefined behavior by evaluating data not in the input");
    if (__begin + 1 != __end && __parse_alignment(*(__begin + 1))) {
      if (!__unicode::__is_scalar_value(*__begin))
        std::__throw_format_error("The fill option contains an invalid value");

      __validate_fill_character(*__begin);

      __fill_.__data[0] = *__begin;
      __begin += 2;
      return true;
    }

    if (!__parse_alignment(*__begin))
      return false;

    ++__begin;
    return true;
  }

#    endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

#  else // _LIBCPP_HAS_NO_UNICODE
  // range-fill and tuple-fill are identical
  template <contiguous_iterator _Iterator>
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_fill_align(_Iterator& __begin, _Iterator __end) {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __begin != __end,
        "when called with an empty input the function will cause "
        "undefined behavior by evaluating data not in the input");
    if (__begin + 1 != __end) {
      if (__parse_alignment(*(__begin + 1))) {
        __validate_fill_character(*__begin);

        __fill_.__data[0] = *__begin;
        __begin += 2;
        return true;
      }
    }

    if (!__parse_alignment(*__begin))
      return false;

    ++__begin;
    return true;
  }

#  endif // _LIBCPP_HAS_NO_UNICODE

  template <contiguous_iterator _Iterator>
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_sign(_Iterator& __begin) {
    switch (*__begin) {
    case _CharT('-'):
      __sign_ = __sign::__minus;
      break;
    case _CharT('+'):
      __sign_ = __sign::__plus;
      break;
    case _CharT(' '):
      __sign_ = __sign::__space;
      break;
    default:
      return false;
    }
    ++__begin;
    return true;
  }

  template <contiguous_iterator _Iterator>
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_alternate_form(_Iterator& __begin) {
    if (*__begin != _CharT('#'))
      return false;

    __alternate_form_ = true;
    ++__begin;
    return true;
  }

  template <contiguous_iterator _Iterator>
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_zero_padding(_Iterator& __begin) {
    if (*__begin != _CharT('0'))
      return false;

    if (__alignment_ == __alignment::__default)
      __alignment_ = __alignment::__zero_padding;
    ++__begin;
    return true;
  }

  template <contiguous_iterator _Iterator>
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_width(_Iterator& __begin, _Iterator __end, auto& __ctx) {
    if (*__begin == _CharT('0'))
      std::__throw_format_error("The width option should not have a leading zero");

    if (*__begin == _CharT('{')) {
      __format::__parse_number_result __r = __format_spec::__parse_arg_id(++__begin, __end, __ctx);
      __width_as_arg_                     = true;
      __width_                            = __r.__value;
      __begin                             = __r.__last;
      return true;
    }

    if (*__begin < _CharT('0') || *__begin > _CharT('9'))
      return false;

    __format::__parse_number_result __r = __format::__parse_number(__begin, __end);
    __width_                            = __r.__value;
    _LIBCPP_ASSERT_INTERNAL(__width_ != 0,
                            "A zero value isn't allowed and should be impossible, "
                            "due to validations in this function");
    __begin = __r.__last;
    return true;
  }

  template <contiguous_iterator _Iterator>
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_precision(_Iterator& __begin, _Iterator __end, auto& __ctx) {
    if (*__begin != _CharT('.'))
      return false;

    ++__begin;
    if (__begin == __end)
      std::__throw_format_error("End of input while parsing format specifier precision");

    if (*__begin == _CharT('{')) {
      __format::__parse_number_result __arg_id = __format_spec::__parse_arg_id(++__begin, __end, __ctx);
      __precision_as_arg_                      = true;
      __precision_                             = __arg_id.__value;
      __begin                                  = __arg_id.__last;
      return true;
    }

    if (*__begin < _CharT('0') || *__begin > _CharT('9'))
      std::__throw_format_error("The precision option does not contain a value or an argument index");

    __format::__parse_number_result __r = __format::__parse_number(__begin, __end);
    __precision_                        = __r.__value;
    __precision_as_arg_                 = false;
    __begin                             = __r.__last;
    return true;
  }

  template <contiguous_iterator _Iterator>
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_locale_specific_form(_Iterator& __begin) {
    if (*__begin != _CharT('L'))
      return false;

    __locale_specific_form_ = true;
    ++__begin;
    return true;
  }

  template <contiguous_iterator _Iterator>
  _LIBCPP_HIDE_FROM_ABI constexpr bool __parse_clear_brackets(_Iterator& __begin) {
    if (*__begin != _CharT('n'))
      return false;

    __clear_brackets_ = true;
    ++__begin;
    return true;
  }

  template <contiguous_iterator _Iterator>
  _LIBCPP_HIDE_FROM_ABI constexpr void __parse_type(_Iterator& __begin) {
    // Determines the type. It does not validate whether the selected type is
    // valid. Most formatters have optional fields that are only allowed for
    // certain types. These parsers need to do validation after the type has
    // been parsed. So its easier to implement the validation for all types in
    // the specific parse function.
    switch (*__begin) {
    case 'A':
      __type_ = __type::__hexfloat_upper_case;
      break;
    case 'B':
      __type_ = __type::__binary_upper_case;
      break;
    case 'E':
      __type_ = __type::__scientific_upper_case;
      break;
    case 'F':
      __type_ = __type::__fixed_upper_case;
      break;
    case 'G':
      __type_ = __type::__general_upper_case;
      break;
    case 'X':
      __type_ = __type::__hexadecimal_upper_case;
      break;
    case 'a':
      __type_ = __type::__hexfloat_lower_case;
      break;
    case 'b':
      __type_ = __type::__binary_lower_case;
      break;
    case 'c':
      __type_ = __type::__char;
      break;
    case 'd':
      __type_ = __type::__decimal;
      break;
    case 'e':
      __type_ = __type::__scientific_lower_case;
      break;
    case 'f':
      __type_ = __type::__fixed_lower_case;
      break;
    case 'g':
      __type_ = __type::__general_lower_case;
      break;
    case 'o':
      __type_ = __type::__octal;
      break;
    case 'p':
      __type_ = __type::__pointer_lower_case;
      break;
    case 'P':
      __type_ = __type::__pointer_upper_case;
      break;
    case 's':
      __type_ = __type::__string;
      break;
    case 'x':
      __type_ = __type::__hexadecimal_lower_case;
      break;
#  if _LIBCPP_STD_VER >= 23
    case '?':
      __type_ = __type::__debug;
      break;
#  endif
    default:
      return;
    }
    ++__begin;
  }

  _LIBCPP_HIDE_FROM_ABI int32_t __get_width(auto& __ctx) const {
    if (!__width_as_arg_)
      return __width_;

    return __format_spec::__substitute_arg_id(__ctx.arg(__width_));
  }

  _LIBCPP_HIDE_FROM_ABI int32_t __get_precision(auto& __ctx) const {
    if (!__precision_as_arg_)
      return __precision_;

    return __format_spec::__substitute_arg_id(__ctx.arg(__precision_));
  }
};

// Validates whether the reserved bitfields don't change the size.
static_assert(sizeof(__parser<char>) == 16);
#  ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
static_assert(sizeof(__parser<wchar_t>) == 16);
#  endif

_LIBCPP_HIDE_FROM_ABI constexpr void __process_display_type_string(__format_spec::__type __type) {
  switch (__type) {
  case __format_spec::__type::__default:
  case __format_spec::__type::__string:
  case __format_spec::__type::__debug:
    break;

  default:
    std::__throw_format_error("The type option contains an invalid value for a string formatting argument");
  }
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI constexpr void __process_display_type_bool_string(__parser<_CharT>& __parser, const char* __id) {
  __parser.__validate(__format_spec::__fields_bool, __id);
  if (__parser.__alignment_ == __alignment::__default)
    __parser.__alignment_ = __alignment::__left;
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI constexpr void __process_display_type_char(__parser<_CharT>& __parser, const char* __id) {
  __format_spec::__process_display_type_bool_string(__parser, __id);
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI constexpr void __process_parsed_bool(__parser<_CharT>& __parser, const char* __id) {
  switch (__parser.__type_) {
  case __format_spec::__type::__default:
  case __format_spec::__type::__string:
    __format_spec::__process_display_type_bool_string(__parser, __id);
    break;

  case __format_spec::__type::__binary_lower_case:
  case __format_spec::__type::__binary_upper_case:
  case __format_spec::__type::__octal:
  case __format_spec::__type::__decimal:
  case __format_spec::__type::__hexadecimal_lower_case:
  case __format_spec::__type::__hexadecimal_upper_case:
    break;

  default:
    __format_spec::__throw_invalid_type_format_error(__id);
  }
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI constexpr void __process_parsed_char(__parser<_CharT>& __parser, const char* __id) {
  switch (__parser.__type_) {
  case __format_spec::__type::__default:
  case __format_spec::__type::__char:
  case __format_spec::__type::__debug:
    __format_spec::__process_display_type_char(__parser, __id);
    break;

  case __format_spec::__type::__binary_lower_case:
  case __format_spec::__type::__binary_upper_case:
  case __format_spec::__type::__octal:
  case __format_spec::__type::__decimal:
  case __format_spec::__type::__hexadecimal_lower_case:
  case __format_spec::__type::__hexadecimal_upper_case:
    break;

  default:
    __format_spec::__throw_invalid_type_format_error(__id);
  }
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI constexpr void __process_parsed_integer(__parser<_CharT>& __parser, const char* __id) {
  switch (__parser.__type_) {
  case __format_spec::__type::__default:
  case __format_spec::__type::__binary_lower_case:
  case __format_spec::__type::__binary_upper_case:
  case __format_spec::__type::__octal:
  case __format_spec::__type::__decimal:
  case __format_spec::__type::__hexadecimal_lower_case:
  case __format_spec::__type::__hexadecimal_upper_case:
    break;

  case __format_spec::__type::__char:
    __format_spec::__process_display_type_char(__parser, __id);
    break;

  default:
    __format_spec::__throw_invalid_type_format_error(__id);
  }
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI constexpr void __process_parsed_floating_point(__parser<_CharT>& __parser, const char* __id) {
  switch (__parser.__type_) {
  case __format_spec::__type::__default:
  case __format_spec::__type::__hexfloat_lower_case:
  case __format_spec::__type::__hexfloat_upper_case:
    // Precision specific behavior will be handled later.
    break;
  case __format_spec::__type::__scientific_lower_case:
  case __format_spec::__type::__scientific_upper_case:
  case __format_spec::__type::__fixed_lower_case:
  case __format_spec::__type::__fixed_upper_case:
  case __format_spec::__type::__general_lower_case:
  case __format_spec::__type::__general_upper_case:
    if (!__parser.__precision_as_arg_ && __parser.__precision_ == -1)
      // Set the default precision for the call to to_chars.
      __parser.__precision_ = 6;
    break;

  default:
    __format_spec::__throw_invalid_type_format_error(__id);
  }
}

_LIBCPP_HIDE_FROM_ABI constexpr void __process_display_type_pointer(__format_spec::__type __type, const char* __id) {
  switch (__type) {
  case __format_spec::__type::__default:
  case __format_spec::__type::__pointer_lower_case:
  case __format_spec::__type::__pointer_upper_case:
    break;

  default:
    __format_spec::__throw_invalid_type_format_error(__id);
  }
}

template <contiguous_iterator _Iterator>
struct __column_width_result {
  /// The number of output columns.
  size_t __width_;
  /// One beyond the last code unit used in the estimation.
  ///
  /// This limits the original output to fit in the wanted number of columns.
  _Iterator __last_;
};

template <contiguous_iterator _Iterator>
__column_width_result(size_t, _Iterator) -> __column_width_result<_Iterator>;

/// Since a column width can be two it's possible that the requested column
/// width can't be achieved. Depending on the intended usage the policy can be
/// selected.
/// - When used as precision the maximum width may not be exceeded and the
///   result should be "rounded down" to the previous boundary.
/// - When used as a width we're done once the minimum is reached, but
///   exceeding is not an issue. Rounding down is an issue since that will
///   result in writing fill characters. Therefore the result needs to be
///   "rounded up".
enum class __column_width_rounding { __down, __up };

#  ifndef _LIBCPP_HAS_NO_UNICODE

namespace __detail {
template <contiguous_iterator _Iterator>
_LIBCPP_HIDE_FROM_ABI constexpr __column_width_result<_Iterator> __estimate_column_width_grapheme_clustering(
    _Iterator __first, _Iterator __last, size_t __maximum, __column_width_rounding __rounding) noexcept {
  using _CharT = iter_value_t<_Iterator>;
  __unicode::__extended_grapheme_cluster_view<_CharT> __view{__first, __last};

  __column_width_result<_Iterator> __result{0, __first};
  while (__result.__last_ != __last && __result.__width_ <= __maximum) {
    typename __unicode::__extended_grapheme_cluster_view<_CharT>::__cluster __cluster = __view.__consume();
    int __width = __width_estimation_table::__estimated_width(__cluster.__code_point_);

    // When the next entry would exceed the maximum width the previous width
    // might be returned. For example when a width of 100 is requested the
    // returned width might be 99, since the next code point has an estimated
    // column width of 2. This depends on the rounding flag.
    // When the maximum is exceeded the loop will abort the next iteration.
    if (__rounding == __column_width_rounding::__down && __result.__width_ + __width > __maximum)
      return __result;

    __result.__width_ += __width;
    __result.__last_ = __cluster.__last_;
  }

  return __result;
}

} // namespace __detail

// Unicode can be stored in several formats: UTF-8, UTF-16, and UTF-32.
// Depending on format the relation between the number of code units stored and
// the number of output columns differs. The first relation is the number of
// code units forming a code point. (The text assumes the code units are
// unsigned.)
// - UTF-8 The number of code units is between one and four. The first 127
//   Unicode code points match the ASCII character set. When the highest bit is
//   set it means the code point has more than one code unit.
// - UTF-16: The number of code units is between 1 and 2. When the first
//   code unit is in the range [0xd800,0xdfff) it means the code point uses two
//   code units.
// - UTF-32: The number of code units is always one.
//
// The code point to the number of columns is specified in
// [format.string.std]/11. This list might change in the future.
//
// Another thing to be taken into account is Grapheme clustering. This means
// that in some cases multiple code points are combined one element in the
// output. For example:
// - an ASCII character with a combined diacritical mark
// - an emoji with a skin tone modifier
// - a group of combined people emoji to create a family
// - a combination of flag emoji
//
// See also:
// - [format.string.general]/11
// - https://en.wikipedia.org/wiki/UTF-8#Encoding
// - https://en.wikipedia.org/wiki/UTF-16#U+D800_to_U+DFFF

_LIBCPP_HIDE_FROM_ABI constexpr bool __is_ascii(char32_t __c) { return __c < 0x80; }

/// Determines the number of output columns needed to render the input.
///
/// \note When the scanner encounters malformed Unicode it acts as-if every
/// code unit is a one column code point. Typically a terminal uses the same
/// strategy and replaces every malformed code unit with a one column
/// replacement character.
///
/// \param __first    Points to the first element of the input range.
/// \param __last     Points beyond the last element of the input range.
/// \param __maximum  The maximum number of output columns. The returned number
///                   of estimated output columns will not exceed this value.
/// \param __rounding Selects the rounding method.
///                   \c __down result.__width_ <= __maximum
///                   \c __up result.__width_ <= __maximum + 1
template <class _CharT, class _Iterator = typename basic_string_view<_CharT>::const_iterator>
_LIBCPP_HIDE_FROM_ABI constexpr __column_width_result<_Iterator> __estimate_column_width(
    basic_string_view<_CharT> __str, size_t __maximum, __column_width_rounding __rounding) noexcept {
  // The width estimation is done in two steps:
  // - Quickly process for the ASCII part. ASCII has the following properties
  //   - One code unit is one code point
  //   - Every code point has an estimated width of one
  // - When needed it will a Unicode Grapheme clustering algorithm to find
  //   the proper place for truncation.

  if (__str.empty() || __maximum == 0)
    return {0, __str.begin()};

  // ASCII has one caveat; when an ASCII character is followed by a non-ASCII
  // character they might be part of an extended grapheme cluster. For example:
  //   an ASCII letter and a COMBINING ACUTE ACCENT
  // The truncate should happen after the COMBINING ACUTE ACCENT. Therefore we
  // need to scan one code unit beyond the requested precision. When this code
  // unit is non-ASCII we omit the current code unit and let the Grapheme
  // clustering algorithm do its work.
  auto __it = __str.begin();
  if (__format_spec::__is_ascii(*__it)) {
    do {
      --__maximum;
      ++__it;
      if (__it == __str.end())
        return {__str.size(), __str.end()};

      if (__maximum == 0) {
        if (__format_spec::__is_ascii(*__it))
          return {static_cast<size_t>(__it - __str.begin()), __it};

        break;
      }
    } while (__format_spec::__is_ascii(*__it));
    --__it;
    ++__maximum;
  }

  ptrdiff_t __ascii_size = __it - __str.begin();
  __column_width_result __result =
      __detail::__estimate_column_width_grapheme_clustering(__it, __str.end(), __maximum, __rounding);

  __result.__width_ += __ascii_size;
  return __result;
}
#  else // !defined(_LIBCPP_HAS_NO_UNICODE)
template <class _CharT>
_LIBCPP_HIDE_FROM_ABI constexpr __column_width_result<typename basic_string_view<_CharT>::const_iterator>
__estimate_column_width(basic_string_view<_CharT> __str, size_t __maximum, __column_width_rounding) noexcept {
  // When Unicode isn't supported assume ASCII and every code unit is one code
  // point. In ASCII the estimated column width is always one. Thus there's no
  // need for rounding.
  size_t __width = std::min(__str.size(), __maximum);
  return {__width, __str.begin() + __width};
}

#  endif // !defined(_LIBCPP_HAS_NO_UNICODE)

} // namespace __format_spec

#endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FORMAT_PARSER_STD_FORMAT_SPEC_H
