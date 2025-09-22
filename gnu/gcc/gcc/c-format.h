/* Check calls to formatted I/O functions (-Wformat).
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef GCC_C_FORMAT_H
#define GCC_C_FORMAT_H

/* The meaningfully distinct length modifiers for format checking recognized
   by GCC.  */
enum format_lengths
{
  FMT_LEN_none,
  FMT_LEN_hh,
  FMT_LEN_h,
  FMT_LEN_l,
  FMT_LEN_ll,
  FMT_LEN_L,
  FMT_LEN_z,
  FMT_LEN_t,
  FMT_LEN_j,
  FMT_LEN_H,
  FMT_LEN_D,
  FMT_LEN_DD,
  FMT_LEN_MAX
};


/* The standard versions in which various format features appeared.  */
enum format_std_version
{
  STD_C89,
  STD_C94,
  STD_C9L, /* C99, but treat as C89 if -Wno-long-long.  */
  STD_C99,
  STD_EXT
};

/* Flags that may apply to a particular kind of format checked by GCC.  */
enum
{
  /* This format converts arguments of types determined by the
     format string.  */
  FMT_FLAG_ARG_CONVERT = 1,
  /* The scanf allocation 'a' kludge applies to this format kind.  */
  FMT_FLAG_SCANF_A_KLUDGE = 2,
  /* A % during parsing a specifier is allowed to be a modified % rather
     that indicating the format is broken and we are out-of-sync.  */
  FMT_FLAG_FANCY_PERCENT_OK = 4,
  /* With $ operand numbers, it is OK to reference the same argument more
     than once.  */
  FMT_FLAG_DOLLAR_MULTIPLE = 8,
  /* This format type uses $ operand numbers (strfmon doesn't).  */
  FMT_FLAG_USE_DOLLAR = 16,
  /* Zero width is bad in this type of format (scanf).  */
  FMT_FLAG_ZERO_WIDTH_BAD = 32,
  /* Empty precision specification is OK in this type of format (printf).  */
  FMT_FLAG_EMPTY_PREC_OK = 64,
  /* Gaps are allowed in the arguments with $ operand numbers if all
     arguments are pointers (scanf).  */
  FMT_FLAG_DOLLAR_GAP_POINTER_OK = 128
  /* Not included here: details of whether width or precision may occur
     (controlled by width_char and precision_char); details of whether
     '*' can be used for these (width_type and precision_type); details
     of whether length modifiers can occur (length_char_specs).  */
};


/* Structure describing a length modifier supported in format checking, and
   possibly a doubled version such as "hh".  */
typedef struct
{
  /* Name of the single-character length modifier.  */
  const char *name;
  /* Index into a format_char_info.types array.  */
  enum format_lengths index;
  /* Standard version this length appears in.  */
  enum format_std_version std;
  /* Same, if the modifier can be repeated, or NULL if it can't.  */
  const char *double_name;
  enum format_lengths double_index;
  enum format_std_version double_std;
} format_length_info;


/* Structure describing the combination of a conversion specifier
   (or a set of specifiers which act identically) and a length modifier.  */
typedef struct
{
  /* The standard version this combination of length and type appeared in.
     This is only relevant if greater than those for length and type
     individually; otherwise it is ignored.  */
  enum format_std_version std;
  /* The name to use for the type, if different from that generated internally
     (e.g., "signed size_t").  */
  const char *name;
  /* The type itself.  */
  tree *type;
} format_type_detail;


/* Macros to fill out tables of these.  */
#define NOARGUMENTS	{ T89_V, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN }
#define BADLEN	{ 0, NULL, NULL }
#define NOLENGTHS	{ BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN, BADLEN }


/* Structure describing a format conversion specifier (or a set of specifiers
   which act identically), and the length modifiers used with it.  */
typedef struct format_char_info
{
  const char *format_chars;
  int pointer_count;
  enum format_std_version std;
  /* Types accepted for each length modifier.  */
  format_type_detail types[FMT_LEN_MAX];
  /* List of other modifier characters allowed with these specifiers.
     This lists flags, and additionally "w" for width, "p" for precision
     (right precision, for strfmon), "#" for left precision (strfmon),
     "a" for scanf "a" allocation extension (not applicable in C99 mode),
     "*" for scanf suppression, and "E" and "O" for those strftime
     modifiers.  */
  const char *flag_chars;
  /* List of additional flags describing these conversion specifiers.
     "c" for generic character pointers being allowed, "2" for strftime
     two digit year formats, "3" for strftime formats giving two digit
     years in some locales, "4" for "2" which becomes "3" with an "E" modifier,
     "o" if use of strftime "O" is a GNU extension beyond C99,
     "W" if the argument is a pointer which is dereferenced and written into,
     "R" if the argument is a pointer which is dereferenced and read from,
     "i" for printf integer formats where the '0' flag is ignored with
     precision, and "[" for the starting character of a scanf scanset.  */
  const char *flags2;
  /* If this format conversion character consumes more than one argument,
     CHAIN points to information about the next argument.  For later
     arguments, only POINTER_COUNT, TYPES, and the "c", "R", and "W" flags
     in FLAGS2 are used.  */
  const struct format_char_info *chain;
} format_char_info;


/* Structure describing a flag accepted by some kind of format.  */
typedef struct
{
  /* The flag character in question (0 for end of array).  */
  int flag_char;
  /* Zero if this entry describes the flag character in general, or a
     nonzero character that may be found in flags2 if it describes the
     flag when used with certain formats only.  If the latter, only
     the first such entry found that applies to the current conversion
     specifier is used; the values of 'name' and 'long_name' it supplies
     will be used, if non-NULL and the standard version is higher than
     the unpredicated one, for any pedantic warning.  For example, 'o'
     for strftime formats (meaning 'O' is an extension over C99).  */
  int predicate;
  /* Nonzero if the next character after this flag in the format should
     be skipped ('=' in strfmon), zero otherwise.  */
  int skip_next_char;
  /* The name to use for this flag in diagnostic messages.  For example,
     N_("'0' flag"), N_("field width").  */
  const char *name;
  /* Long name for this flag in diagnostic messages; currently only used for
     "ISO C does not support ...".  For example, N_("the 'I' printf flag").  */
  const char *long_name;
  /* The standard version in which it appeared.  */
  enum format_std_version std;
} format_flag_spec;


/* Structure describing a combination of flags that is bad for some kind
   of format.  */
typedef struct
{
  /* The first flag character in question (0 for end of array).  */
  int flag_char1;
  /* The second flag character.  */
  int flag_char2;
  /* Nonzero if the message should say that the first flag is ignored with
     the second, zero if the combination should simply be objected to.  */
  int ignored;
  /* Zero if this entry applies whenever this flag combination occurs,
     a nonzero character from flags2 if it only applies in some
     circumstances (e.g. 'i' for printf formats ignoring 0 with precision).  */
  int predicate;
} format_flag_pair;


/* Structure describing a particular kind of format processed by GCC.  */
typedef struct
{
  /* The name of this kind of format, for use in diagnostics.  Also
     the name of the attribute (without preceding and following __).  */
  const char *name;
  /* Specifications of the length modifiers accepted; possibly NULL.  */
  const format_length_info *length_char_specs;
  /* Details of the conversion specification characters accepted.  */
  const format_char_info *conversion_specs;
  /* String listing the flag characters that are accepted.  */
  const char *flag_chars;
  /* String listing modifier characters (strftime) accepted.  May be NULL.  */
  const char *modifier_chars;
  /* Details of the flag characters, including pseudo-flags.  */
  const format_flag_spec *flag_specs;
  /* Details of bad combinations of flags.  */
  const format_flag_pair *bad_flag_pairs;
  /* Flags applicable to this kind of format.  */
  int flags;
  /* Flag character to treat a width as, or 0 if width not used.  */
  int width_char;
  /* Flag character to treat a left precision (strfmon) as,
     or 0 if left precision not used.  */
  int left_precision_char;
  /* Flag character to treat a precision (for strfmon, right precision) as,
     or 0 if precision not used.  */
  int precision_char;
  /* If a flag character has the effect of suppressing the conversion of
     an argument ('*' in scanf), that flag character, otherwise 0.  */
  int suppression_char;
  /* Flag character to treat a length modifier as (ignored if length
     modifiers not used).  Need not be placed in flag_chars for conversion
     specifiers, but is used to check for bad combinations such as length
     modifier with assignment suppression in scanf.  */
  int length_code_char;
  /* Pointer to type of argument expected if '*' is used for a width,
     or NULL if '*' not used for widths.  */
  tree *width_type;
  /* Pointer to type of argument expected if '*' is used for a precision,
     or NULL if '*' not used for precisions.  */
  tree *precision_type;
} format_kind_info;

#define T_I	&integer_type_node
#define T89_I	{ STD_C89, NULL, T_I }
#define T_L	&long_integer_type_node
#define T89_L	{ STD_C89, NULL, T_L }
#define T_LL	&long_long_integer_type_node
#define T9L_LL	{ STD_C9L, NULL, T_LL }
#define TEX_LL	{ STD_EXT, NULL, T_LL }
#define T_S	&short_integer_type_node
#define T89_S	{ STD_C89, NULL, T_S }
#define T_UI	&unsigned_type_node
#define T89_UI	{ STD_C89, NULL, T_UI }
#define T_UL	&long_unsigned_type_node
#define T89_UL	{ STD_C89, NULL, T_UL }
#define T_ULL	&long_long_unsigned_type_node
#define T9L_ULL	{ STD_C9L, NULL, T_ULL }
#define TEX_ULL	{ STD_EXT, NULL, T_ULL }
#define T_US	&short_unsigned_type_node
#define T89_US	{ STD_C89, NULL, T_US }
#define T_F	&float_type_node
#define T89_F	{ STD_C89, NULL, T_F }
#define T99_F	{ STD_C99, NULL, T_F }
#define T_D	&double_type_node
#define T89_D	{ STD_C89, NULL, T_D }
#define T99_D	{ STD_C99, NULL, T_D }
#define T_LD	&long_double_type_node
#define T89_LD	{ STD_C89, NULL, T_LD }
#define T99_LD	{ STD_C99, NULL, T_LD }
#define T_C	&char_type_node
#define T89_C	{ STD_C89, NULL, T_C }
#define T_SC	&signed_char_type_node
#define T99_SC	{ STD_C99, NULL, T_SC }
#define T_UC	&unsigned_char_type_node
#define T99_UC	{ STD_C99, NULL, T_UC }
#define T_V	&void_type_node
#define T89_V	{ STD_C89, NULL, T_V }
#define T_W	&wchar_type_node
#define T94_W	{ STD_C94, "wchar_t", T_W }
#define TEX_W	{ STD_EXT, "wchar_t", T_W }
#define T_WI	&wint_type_node
#define T94_WI	{ STD_C94, "wint_t", T_WI }
#define TEX_WI	{ STD_EXT, "wint_t", T_WI }
#define T_ST    &size_type_node
#define T99_ST	{ STD_C99, "size_t", T_ST }
#define T_SST   &signed_size_type_node
#define T99_SST	{ STD_C99, "signed size_t", T_SST }
#define T_PD    &ptrdiff_type_node
#define T99_PD	{ STD_C99, "ptrdiff_t", T_PD }
#define T_UPD   &unsigned_ptrdiff_type_node
#define T99_UPD	{ STD_C99, "unsigned ptrdiff_t", T_UPD }
#define T_IM    &intmax_type_node
#define T99_IM	{ STD_C99, "intmax_t", T_IM }
#define T_UIM   &uintmax_type_node
#define T99_UIM	{ STD_C99, "uintmax_t", T_UIM }
#define T_D32   &dfloat32_type_node
#define TEX_D32 { STD_EXT, "_Decimal32", T_D32 }
#define T_D64   &dfloat64_type_node
#define TEX_D64 { STD_EXT, "_Decimal64", T_D64 }
#define T_D128  &dfloat128_type_node
#define TEX_D128 { STD_EXT, "_Decimal128", T_D128 }

#endif /* GCC_C_FORMAT_H */
