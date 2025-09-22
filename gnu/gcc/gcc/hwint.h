/* HOST_WIDE_INT definitions for the GNU compiler.
   Copyright (C) 1998, 2002, 2004 Free Software Foundation, Inc.

   This file is part of GCC.

   Provide definitions for macros which depend on HOST_BITS_PER_INT
   and HOST_BITS_PER_LONG.  */

#ifndef GCC_HWINT_H
#define GCC_HWINT_H

/* This describes the machine the compiler is hosted on.  */
#define HOST_BITS_PER_CHAR  CHAR_BIT
#define HOST_BITS_PER_SHORT (CHAR_BIT * SIZEOF_SHORT)
#define HOST_BITS_PER_INT   (CHAR_BIT * SIZEOF_INT)
#define HOST_BITS_PER_LONG  (CHAR_BIT * SIZEOF_LONG)

/* The string that should be inserted into a printf style format to
   indicate a "long long" operand.  */
#ifndef HOST_LONG_LONG_FORMAT 
#define HOST_LONG_LONG_FORMAT "ll"
#endif

/* If HAVE_LONG_LONG and SIZEOF_LONG_LONG aren't defined, but
   GCC_VERSION >= 3000, assume this is the second or later stage of a
   bootstrap, we do have long long, and it's 64 bits.  (This is
   required by C99; we do have some ports that violate that assumption
   but they're all cross-compile-only.)  Just in case, force a
   constraint violation if that assumption is incorrect.  */
#if !defined HAVE_LONG_LONG
# if GCC_VERSION >= 3000
#  define HAVE_LONG_LONG 1
#  define SIZEOF_LONG_LONG 8
extern char sizeof_long_long_must_be_8[sizeof(long long) == 8 ? 1 : -1];
# endif
#endif

#ifdef HAVE_LONG_LONG
# define HOST_BITS_PER_LONGLONG (CHAR_BIT * SIZEOF_LONG_LONG)
#endif
#ifdef HAVE___INT64
# define HOST_BITS_PER___INT64 (CHAR_BIT * SIZEOF___INT64)
#endif

/* Set HOST_WIDE_INT.  This should be the widest efficient host
   integer type.  It can be 32 or 64 bits, except that if we are
   targeting a machine with 64-bit size_t then it has to be 64 bits.

   With a sane ABI, 'long' is the largest efficient host integer type.
   Thus, we use that unless we have to use 'long long' or '__int64'
   because we're targeting a 64-bit machine from a 32-bit host.  */

#if HOST_BITS_PER_LONG >= 64 || !defined NEED_64BIT_HOST_WIDE_INT
#   define HOST_BITS_PER_WIDE_INT HOST_BITS_PER_LONG
#   define HOST_WIDE_INT long
#else
# if HOST_BITS_PER_LONGLONG >= 64
#   define HOST_BITS_PER_WIDE_INT HOST_BITS_PER_LONGLONG
#   define HOST_WIDE_INT long long
# else
#  if HOST_BITS_PER___INT64 >= 64
#   define HOST_BITS_PER_WIDE_INT HOST_BITS_PER___INT64
#   define HOST_WIDE_INT __int64
#  else
    #error "Unable to find a suitable type for HOST_WIDE_INT"
#  endif
# endif
#endif

/* Various printf format strings for HOST_WIDE_INT.  */

#if HOST_BITS_PER_WIDE_INT == HOST_BITS_PER_LONG
# define HOST_WIDE_INT_PRINT "l"
# define HOST_WIDE_INT_PRINT_C "L"
  /* 'long' might be 32 or 64 bits, and the number of leading zeroes
     must be tweaked accordingly.  */
# if HOST_BITS_PER_WIDE_INT == 64
#  define HOST_WIDE_INT_PRINT_DOUBLE_HEX "0x%lx%016lx"
# else
#  define HOST_WIDE_INT_PRINT_DOUBLE_HEX "0x%lx%08lx"
# endif
#else
# define HOST_WIDE_INT_PRINT HOST_LONG_LONG_FORMAT
# define HOST_WIDE_INT_PRINT_C "LL"
  /* We can assume that 'long long' is at least 64 bits.  */
# define HOST_WIDE_INT_PRINT_DOUBLE_HEX \
    "0x%" HOST_LONG_LONG_FORMAT "x%016" HOST_LONG_LONG_FORMAT "x"
#endif /* HOST_BITS_PER_WIDE_INT == HOST_BITS_PER_LONG */

#define HOST_WIDE_INT_PRINT_DEC "%" HOST_WIDE_INT_PRINT "d"
#define HOST_WIDE_INT_PRINT_DEC_C HOST_WIDE_INT_PRINT_DEC HOST_WIDE_INT_PRINT_C
#define HOST_WIDE_INT_PRINT_UNSIGNED "%" HOST_WIDE_INT_PRINT "u"
#define HOST_WIDE_INT_PRINT_HEX "0x%" HOST_WIDE_INT_PRINT "x"

/* Set HOST_WIDEST_INT.  This is a 64-bit type unless the compiler
   in use has no 64-bit type at all; in that case it's 32 bits.  */

#if HOST_BITS_PER_WIDE_INT >= 64 \
    || (HOST_BITS_PER_LONGLONG < 64 && HOST_BITS_PER___INT64 < 64)
# define HOST_WIDEST_INT		      HOST_WIDE_INT
# define HOST_BITS_PER_WIDEST_INT	      HOST_BITS_PER_WIDE_INT
# define HOST_WIDEST_INT_PRINT_DEC	      HOST_WIDE_INT_PRINT_DEC
# define HOST_WIDEST_INT_PRINT_DEC_C	      HOST_WIDE_INT_PRINT_DEC_C
# define HOST_WIDEST_INT_PRINT_UNSIGNED	      HOST_WIDE_INT_PRINT_UNSIGNED
# define HOST_WIDEST_INT_PRINT_HEX	      HOST_WIDE_INT_PRINT_HEX
# define HOST_WIDEST_INT_PRINT_DOUBLE_HEX     HOST_WIDE_INT_PRINT_DOUBLE_HEX
#else
# if HOST_BITS_PER_LONGLONG >= 64
#  define HOST_BITS_PER_WIDEST_INT	      HOST_BITS_PER_LONGLONG
#  define HOST_WIDEST_INT		      long long
# else
#  if HOST_BITS_PER___INT64 >= 64
#   define HOST_BITS_PER_WIDEST_INT	      HOST_BITS_PER___INT64
#   define HOST_WIDEST_INT		      __int64
#  else
    #error "This line should be impossible to reach"
#  endif
# endif
# define HOST_WIDEST_INT_PRINT_DEC	      "%" HOST_LONG_LONG_FORMAT "d"
# define HOST_WIDEST_INT_PRINT_DEC_C	      "%" HOST_LONG_LONG_FORMAT "dLL"
# define HOST_WIDEST_INT_PRINT_UNSIGNED	      "%" HOST_LONG_LONG_FORMAT "u"
# define HOST_WIDEST_INT_PRINT_HEX	      "0x%" HOST_LONG_LONG_FORMAT "x"
# define HOST_WIDEST_INT_PRINT_DOUBLE_HEX     \
    "0x%" HOST_LONG_LONG_FORMAT "x%016" HOST_LONG_LONG_FORMAT "x"
#endif

/* Define HOST_WIDEST_FAST_INT to the widest integer type supported
   efficiently in hardware.  (That is, the widest integer type that fits
   in a hardware register.)  Normally this is "long" but on some hosts it
   should be "long long" or "__int64".  This is no convenient way to
   autodect this, so such systems must set a flag in config.host; see there
   for details.  */

#ifdef USE_LONG_LONG_FOR_WIDEST_FAST_INT
#  ifdef HAVE_LONG_LONG
#    define HOST_WIDEST_FAST_INT long long
#    define HOST_BITS_PER_WIDEST_FAST_INT HOST_BITS_PER_LONGLONG
#  elif defined (HAVE___INT64)
#    define HOST_WIDEST_FAST_INT __int64
#    define HOST_BITS_PER_WIDEST_FAST_INT HOST_BITS_PER___INT64
#  else
#    error "Your host said it wantted to use long long or __int64 but neither"
#    error "exist"
#  endif
#else
#  define HOST_WIDEST_FAST_INT long
#  define HOST_BITS_PER_WIDEST_FAST_INT HOST_BITS_PER_LONG
#endif

#endif /* ! GCC_HWINT_H */
