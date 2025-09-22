dnl @synopsis GCC_HEADER_STDINT [( HEADER-TO-GENERATE [, HEADERS-TO-CHECK])]
dnl
dnl the "ISO C9X: 7.18 Integer types <stdint.h>" section requires the
dnl existence of an include file <stdint.h> that defines a set of
dnl typedefs, especially uint8_t,int32_t,uintptr_t.
dnl Many older installations will not provide this file, but some will
dnl have the very same definitions in <inttypes.h>. In other enviroments
dnl we can use the inet-types in <sys/types.h> which would define the
dnl typedefs int8_t and u_int8_t respectivly.
dnl
dnl This macros will create a local "_stdint.h" or the headerfile given as
dnl an argument. In many cases that file will pick the definition from a
dnl "#include <stdint.h>" or "#include <inttypes.h>" statement, while
dnl in other environments it will provide the set of basic 'stdint's defined:
dnl int8_t,uint8_t,int16_t,uint16_t,int32_t,uint32_t,intptr_t,uintptr_t
dnl int_least32_t.. int_fast32_t.. intmax_t
dnl which may or may not rely on the definitions of other files.
dnl
dnl Sometimes the stdint.h or inttypes.h headers conflict with sys/types.h,
dnl so we test the headers together with sys/types.h and always include it
dnl into the generated header (to match the tests with the generated file).
dnl Hopefully this is not a big annoyance.
dnl
dnl If your installed header files require the stdint-types you will want to
dnl create an installable file mylib-int.h that all your other installable
dnl header may include. So, for a library package named "mylib", just use
dnl      GCC_HEADER_STDINT(mylib-int.h)
dnl in configure.in and install that header file in Makefile.am along with
dnl the other headers (mylib.h).  The mylib-specific headers can simply
dnl use "#include <mylib-int.h>" to obtain the stdint-types.
dnl
dnl Remember, if the system already had a valid <stdint.h>, the generated
dnl file will include it directly. No need for fuzzy HAVE_STDINT_H things...
dnl
dnl @author  Guido Draheim <guidod@gmx.de>, Paolo Bonzini <bonzini@gnu.org>

AC_DEFUN([GCC_HEADER_STDINT],
[m4_define(_GCC_STDINT_H, m4_ifval($1, $1, _stdint.h))

inttype_headers=`echo inttypes.h sys/inttypes.h $2 | sed -e 's/,/ /g'`

acx_cv_header_stdint=stddef.h
acx_cv_header_stdint_kind="(already complete)"
for i in stdint.h $inttype_headers; do
  unset ac_cv_type_uintptr_t
  unset ac_cv_type_uintmax_t
  unset ac_cv_type_int_least32_t
  unset ac_cv_type_int_fast32_t
  unset ac_cv_type_uint64_t
  _AS_ECHO_N([looking for a compliant stdint.h in $i, ])
  AC_CHECK_TYPE(uintmax_t,[acx_cv_header_stdint=$i],continue,[#include <sys/types.h>
#include <$i>])
  AC_CHECK_TYPE(uintptr_t,,[acx_cv_header_stdint_kind="(mostly complete)"], [#include <sys/types.h>
#include <$i>])
  AC_CHECK_TYPE(int_least32_t,,[acx_cv_header_stdint_kind="(mostly complete)"], [#include <sys/types.h>
#include <$i>])
  AC_CHECK_TYPE(int_fast32_t,,[acx_cv_header_stdint_kind="(mostly complete)"], [#include <sys/types.h>
#include <$i>])
  AC_CHECK_TYPE(uint64_t,,[acx_cv_header_stdint_kind="(lacks uint64_t)"], [#include <sys/types.h>
#include <$i>])
  break
done
if test "$acx_cv_header_stdint" = stddef.h; then
  acx_cv_header_stdint_kind="(lacks uintmax_t)"
  for i in stdint.h $inttype_headers; do
    unset ac_cv_type_uintptr_t
    unset ac_cv_type_uint32_t
    unset ac_cv_type_uint64_t
    _AS_ECHO_N([looking for an incomplete stdint.h in $i, ])
    AC_CHECK_TYPE(uint32_t,[acx_cv_header_stdint=$i],continue,[#include <sys/types.h>
#include <$i>])
    AC_CHECK_TYPE(uint64_t,,,[#include <sys/types.h>
#include <$i>])
    AC_CHECK_TYPE(uintptr_t,,,[#include <sys/types.h>
#include <$i>])
    break
  done
fi
if test "$acx_cv_header_stdint" = stddef.h; then
  acx_cv_header_stdint_kind="(u_intXX_t style)"
  for i in sys/types.h $inttype_headers; do
    unset ac_cv_type_u_int32_t
    unset ac_cv_type_u_int64_t
    _AS_ECHO_N([looking for u_intXX_t types in $i, ])
    AC_CHECK_TYPE(u_int32_t,[acx_cv_header_stdint=$i],continue,[#include <sys/types.h>
#include <$i>])
    AC_CHECK_TYPE(u_int64_t,,,[#include <sys/types.h>
#include <$i>])
    break
  done
fi
if test "$acx_cv_header_stdint" = stddef.h; then
  acx_cv_header_stdint_kind="(using manual detection)"
fi

test -z "$ac_cv_type_uintptr_t" && ac_cv_type_uintptr_t=no
test -z "$ac_cv_type_uint64_t" && ac_cv_type_uint64_t=no
test -z "$ac_cv_type_u_int64_t" && ac_cv_type_u_int64_t=no
test -z "$ac_cv_type_int_least32_t" && ac_cv_type_int_least32_t=no
test -z "$ac_cv_type_int_fast32_t" && ac_cv_type_int_fast32_t=no

# ----------------- Summarize what we found so far

AC_MSG_CHECKING([what to include in _GCC_STDINT_H])

case `AS_BASENAME(_GCC_STDINT_H)` in
  stdint.h) AC_MSG_WARN([are you sure you want it there?]) ;;
  inttypes.h) AC_MSG_WARN([are you sure you want it there?]) ;;
  *) ;;
esac

AC_MSG_RESULT($acx_cv_header_stdint $acx_cv_header_stdint_kind)

# ----------------- done included file, check C basic types --------

# Lacking an uintptr_t?  Test size of void *
case "$acx_cv_header_stdint:$ac_cv_type_uintptr_t" in
  stddef.h:* | *:no) AC_CHECK_SIZEOF(void *) ;;
esac

# Lacking an uint64_t?  Test size of long
case "$acx_cv_header_stdint:$ac_cv_type_uint64_t:$ac_cv_type_u_int64_t" in
  stddef.h:*:* | *:no:no) AC_CHECK_SIZEOF(long) ;;
esac

if test $acx_cv_header_stdint = stddef.h; then
  # Lacking a good header?  Test size of everything and deduce all types.
  AC_CHECK_SIZEOF(int)
  AC_CHECK_SIZEOF(short)
  AC_CHECK_SIZEOF(char)

  AC_MSG_CHECKING(for type equivalent to int8_t)
  case "$ac_cv_sizeof_char" in
    1) acx_cv_type_int8_t=char ;;
    *) AC_MSG_ERROR(no 8-bit type, please report a bug)
  esac
  AC_MSG_RESULT($acx_cv_type_int8_t)

  AC_MSG_CHECKING(for type equivalent to int16_t)
  case "$ac_cv_sizeof_int:$ac_cv_sizeof_short" in
    2:*) acx_cv_type_int16_t=int ;;
    *:2) acx_cv_type_int16_t=short ;;
    *) AC_MSG_ERROR(no 16-bit type, please report a bug)
  esac
  AC_MSG_RESULT($acx_cv_type_int16_t)

  AC_MSG_CHECKING(for type equivalent to int32_t)
  case "$ac_cv_sizeof_int:$ac_cv_sizeof_long" in
    4:*) acx_cv_type_int32_t=int ;;
    *:4) acx_cv_type_int32_t=long ;;
    *) AC_MSG_ERROR(no 32-bit type, please report a bug)
  esac
  AC_MSG_RESULT($acx_cv_type_int32_t)
fi

# These tests are here to make the output prettier

if test "$ac_cv_type_uint64_t" != yes && test "$ac_cv_type_u_int64_t" != yes; then
  case "$ac_cv_sizeof_long" in
    8) acx_cv_type_int64_t=long ;;
  esac
  AC_MSG_CHECKING(for type equivalent to int64_t)
  AC_MSG_RESULT(${acx_cv_type_int64_t-'using preprocessor symbols'})
fi

# Now we can use the above types

if test "$ac_cv_type_uintptr_t" != yes; then
  AC_MSG_CHECKING(for type equivalent to intptr_t)
  case $ac_cv_sizeof_void_p in
    2) acx_cv_type_intptr_t=int16_t ;;
    4) acx_cv_type_intptr_t=int32_t ;;
    8) acx_cv_type_intptr_t=int64_t ;;
    *) AC_MSG_ERROR(no equivalent for intptr_t, please report a bug)
  esac
  AC_MSG_RESULT($acx_cv_type_intptr_t)
fi

# ----------------- done all checks, emit header -------------
AC_CONFIG_COMMANDS(_GCC_STDINT_H, [
if test "$GCC" = yes; then
  echo "/* generated for " `$CC --version | sed 1q` "*/" > tmp-stdint.h
else
  echo "/* generated for $CC */" > tmp-stdint.h
fi

sed 's/^ *//' >> tmp-stdint.h <<EOF

  #ifndef GCC_GENERATED_STDINT_H
  #define GCC_GENERATED_STDINT_H 1

  #include <sys/types.h>
EOF

if test "$acx_cv_header_stdint" != stdint.h; then
  echo "#include <stddef.h>" >> tmp-stdint.h
fi
if test "$acx_cv_header_stdint" != stddef.h; then
  echo "#include <$acx_cv_header_stdint>" >> tmp-stdint.h
fi

sed 's/^ *//' >> tmp-stdint.h <<EOF
  /* glibc uses these symbols as guards to prevent redefinitions.  */
  #ifdef __int8_t_defined
  #define _INT8_T
  #define _INT16_T
  #define _INT32_T
  #endif
  #ifdef __uint32_t_defined
  #define _UINT32_T
  #endif

EOF

# ----------------- done header, emit basic int types -------------
if test "$acx_cv_header_stdint" = stddef.h; then
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    #ifndef _UINT8_T
    #define _UINT8_T
    typedef unsigned $acx_cv_type_int8_t uint8_t;
    #endif

    #ifndef _UINT16_T
    #define _UINT16_T
    typedef unsigned $acx_cv_type_int16_t uint16_t;
    #endif

    #ifndef _UINT32_T
    #define _UINT32_T
    typedef unsigned $acx_cv_type_int32_t uint32_t;
    #endif

    #ifndef _INT8_T
    #define _INT8_T
    typedef $acx_cv_type_int8_t int8_t;
    #endif

    #ifndef _INT16_T
    #define _INT16_T
    typedef $acx_cv_type_int16_t int16_t;
    #endif

    #ifndef _INT32_T
    #define _INT32_T
    typedef $acx_cv_type_int32_t int32_t;
    #endif
EOF
elif test "$ac_cv_type_u_int32_t" = yes; then
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    /* int8_t int16_t int32_t defined by inet code, we do the u_intXX types */
    #ifndef _INT8_T
    #define _INT8_T
    #endif
    #ifndef _INT16_T
    #define _INT16_T
    #endif
    #ifndef _INT32_T
    #define _INT32_T
    #endif

    #ifndef _UINT8_T
    #define _UINT8_T
    typedef u_int8_t uint8_t;
    #endif

    #ifndef _UINT16_T
    #define _UINT16_T
    typedef u_int16_t uint16_t;
    #endif

    #ifndef _UINT32_T
    #define _UINT32_T
    typedef u_int32_t uint32_t;
    #endif
EOF
else
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    /* Some systems have guard macros to prevent redefinitions, define them.  */
    #ifndef _INT8_T
    #define _INT8_T
    #endif
    #ifndef _INT16_T
    #define _INT16_T
    #endif
    #ifndef _INT32_T
    #define _INT32_T
    #endif
    #ifndef _UINT8_T
    #define _UINT8_T
    #endif
    #ifndef _UINT16_T
    #define _UINT16_T
    #endif
    #ifndef _UINT32_T
    #define _UINT32_T
    #endif
EOF
fi

# ------------- done basic int types, emit int64_t types ------------
if test "$ac_cv_type_uint64_t" = yes; then
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    /* system headers have good uint64_t and int64_t */
    #ifndef _INT64_T
    #define _INT64_T
    #endif
    #ifndef _UINT64_T
    #define _UINT64_T
    #endif
EOF
elif test "$ac_cv_type_u_int64_t" = yes; then
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    /* system headers have an u_int64_t (and int64_t) */
    #ifndef _INT64_T
    #define _INT64_T
    #endif
    #ifndef _UINT64_T
    #define _UINT64_T
    typedef u_int64_t uint64_t;
    #endif
EOF
elif test -n "$acx_cv_type_int64_t"; then
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    /* architecture has a 64-bit type, $acx_cv_type_int64_t */
    #ifndef _INT64_T
    #define _INT64_T
    typedef $acx_cv_type_int64_t int64_t;
    #endif
    #ifndef _UINT64_T
    #define _UINT64_T
    typedef unsigned $acx_cv_type_int64_t uint64_t;
    #endif
EOF
else
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    /* some common heuristics for int64_t, using compiler-specific tests */
    #if defined __STDC_VERSION__ && (__STDC_VERSION__-0) >= 199901L
    #ifndef _INT64_T
    #define _INT64_T
    typedef long long int64_t;
    #endif
    #ifndef _UINT64_T
    #define _UINT64_T
    typedef unsigned long long uint64_t;
    #endif

    #elif defined __GNUC__ && defined (__STDC__) && __STDC__-0
    /* NextStep 2.0 cc is really gcc 1.93 but it defines __GNUC__ = 2 and
       does not implement __extension__.  But that compiler doesn't define
       __GNUC_MINOR__.  */
    # if __GNUC__ < 2 || (__NeXT__ && !__GNUC_MINOR__)
    # define __extension__
    # endif

    # ifndef _INT64_T
    # define _INT64_T
    __extension__ typedef long long int64_t;
    # endif
    # ifndef _UINT64_T
    # define _UINT64_T
    __extension__ typedef unsigned long long uint64_t;
    # endif

    #elif !defined __STRICT_ANSI__
    # if defined _MSC_VER || defined __WATCOMC__ || defined __BORLANDC__

    #  ifndef _INT64_T
    #  define _INT64_T
    typedef __int64 int64_t;
    #  endif
    #  ifndef _UINT64_T
    #  define _UINT64_T
    typedef unsigned __int64 uint64_t;
    #  endif
    # endif /* compiler */

    #endif /* ANSI version */
EOF
fi

# ------------- done int64_t types, emit intptr types ------------
if test "$ac_cv_type_uintptr_t" != yes; then
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    /* Define intptr_t based on sizeof(void*) = $ac_cv_sizeof_void_p */
    typedef u$acx_cv_type_intptr_t uintptr_t;
    typedef $acx_cv_type_intptr_t  intptr_t;
EOF
fi

# ------------- done intptr types, emit int_least types ------------
if test "$ac_cv_type_int_least32_t" != yes; then
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    /* Define int_least types */
    typedef int8_t     int_least8_t;
    typedef int16_t    int_least16_t;
    typedef int32_t    int_least32_t;
    #ifdef _INT64_T
    typedef int64_t    int_least64_t;
    #endif

    typedef uint8_t    uint_least8_t;
    typedef uint16_t   uint_least16_t;
    typedef uint32_t   uint_least32_t;
    #ifdef _UINT64_T
    typedef uint64_t   uint_least64_t;
    #endif
EOF
fi

# ------------- done intptr types, emit int_fast types ------------
if test "$ac_cv_type_int_fast32_t" != yes; then
  dnl NOTE: The following code assumes that sizeof (int) > 1.
  dnl Fix when strange machines are reported.
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    /* Define int_fast types.  short is often slow */
    typedef int8_t       int_fast8_t;
    typedef int          int_fast16_t;
    typedef int32_t      int_fast32_t;
    #ifdef _INT64_T
    typedef int64_t      int_fast64_t;
    #endif

    typedef uint8_t      uint_fast8_t;
    typedef unsigned int uint_fast16_t;
    typedef uint32_t     uint_fast32_t;
    #ifdef _UINT64_T
    typedef uint64_t     uint_fast64_t;
    #endif
EOF
fi

if test "$ac_cv_type_uintmax_t" != yes; then
  sed 's/^ *//' >> tmp-stdint.h <<EOF

    /* Define intmax based on what we found */
    #ifdef _INT64_T
    typedef int64_t       intmax_t;
    #else
    typedef long          intmax_t;
    #endif
    #ifdef _UINT64_T
    typedef uint64_t      uintmax_t;
    #else
    typedef unsigned long uintmax_t;
    #endif
EOF
fi

sed 's/^ *//' >> tmp-stdint.h <<EOF

  #endif /* GCC_GENERATED_STDINT_H */
EOF

if test -r ]_GCC_STDINT_H[ && cmp -s tmp-stdint.h ]_GCC_STDINT_H[; then
  rm -f tmp-stdint.h
else
  mv -f tmp-stdint.h ]_GCC_STDINT_H[
fi

], [
GCC="$GCC"
CC="$CC"
acx_cv_header_stdint="$acx_cv_header_stdint"
acx_cv_type_int8_t="$acx_cv_type_int8_t"
acx_cv_type_int16_t="$acx_cv_type_int16_t"
acx_cv_type_int32_t="$acx_cv_type_int32_t"
acx_cv_type_int64_t="$acx_cv_type_int64_t"
acx_cv_type_intptr_t="$acx_cv_type_intptr_t"
ac_cv_type_uintmax_t="$ac_cv_type_uintmax_t"
ac_cv_type_uintptr_t="$ac_cv_type_uintptr_t"
ac_cv_type_uint64_t="$ac_cv_type_uint64_t"
ac_cv_type_u_int64_t="$ac_cv_type_u_int64_t"
ac_cv_type_u_int32_t="$ac_cv_type_u_int32_t"
ac_cv_type_int_least32_t="$ac_cv_type_int_least32_t"
ac_cv_type_int_fast32_t="$ac_cv_type_int_fast32_t"
ac_cv_sizeof_void_p="$ac_cv_sizeof_void_p"
])

])
