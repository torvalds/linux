// acconfig.h symbols and macros for libstdc++ v3 -*- C++ -*-

// Defines libstdc++ version.
#undef PACKAGE
#undef VERSION

// Needed for gettext.
#undef ENABLE_NLS
#undef HAVE_CATGETS
#undef HAVE_GETTEXT
#undef HAVE_STPCPY

// Define if GCC supports weak symbols.
#undef _GLIBCPP_SUPPORTS_WEAK

// Include I/O support for 'long long' and 'unsigned long long'.
#undef _GLIBCPP_USE_LONG_LONG

// Define if C99 features such as lldiv_t, llabs, lldiv should be exposed.
#undef _GLIBCPP_USE_C99

// Include support for 'long double'.
#undef _GLIBCPP_USE_LONG_DOUBLE

// Include support for shadow headers, ie --enable-cshadow-headers.
#undef _GLIBCPP_USE_SHADOW_HEADERS

// Define if code specialized for wchar_t should be used.
#undef _GLIBCPP_USE_WCHAR_T

// Define if using setrlimit to limit memory usage during 'make check'.
#undef _GLIBCPP_MEM_LIMITS

// Define to use concept checking code from the boost libraries.
#undef _GLIBCPP_CONCEPT_CHECKS

// Define if the atan2f function exists.
#undef _GLIBCPP_HAVE_ATAN2F 

// Define if the atan2l function exists.
#undef _GLIBCPP_HAVE_ATAN2L

// Define if the copysignf function exists.
#undef _GLIBCPP_HAVE_COPYSIGNF

// Define to use symbol versioning in the shared library.
#undef _GLIBCPP_SYMVER

// Define symbol versioning in assember directives. If symbol
// versioning is beigng used, and the assembler supports this kind of
// thing, then use it.
// NB: _GLIBCPP_AT_AT is a hack to work around quoting issues in m4.
#if _GLIBCPP_SYMVER
  #define _GLIBCPP_ASM_SYMVER(cur, old, version) \
   asm (".symver " #cur "," #old _GLIBCPP_AT_AT #version);
#else
  #define _GLIBCPP_ASM_SYMVER(cur, old, version)
#endif

// Define if gthr-default.h exists (meaning that threading support is enabled).
#undef HAVE_GTHR_DEFAULT

// Define if drand48 exists.
#undef HAVE_DRAND48

// Define if getpagesize exists.
#undef HAVE_GETPAGESIZE

// Define if setenv exists.
#undef HAVE_SETENV

// Define if sigsetjmp exists.
#undef HAVE_SIGSETJMP

// Define if mbstate_t exists in wchar.h.
#undef HAVE_MBSTATE_T

// Define if you have the modff function.
#undef HAVE_MODFF

// Define if you have the modfl function.
#undef HAVE_MODFL

// Define if you have the expf function.
#undef HAVE_EXPF

// Define if you have the expl function.
#undef HAVE_EXPL

// Define if you have the hypotf function.
#undef HAVE_HYPOTF

// Define if you have the hypotl function.
#undef HAVE_HYPOTL

// Define if the compiler/host combination has __builtin_abs
#undef HAVE___BUILTIN_ABS

// Define if the compiler/host combination has __builtin_labs
#undef HAVE___BUILTIN_LABS

// Define if the compiler/host combination has __builtin_cos
#undef HAVE___BUILTIN_COS

// Define if the compiler/host combination has __builtin_cosf
#undef HAVE___BUILTIN_COSF

// Define if the compiler/host combination has __builtin_cosl
#undef HAVE___BUILTIN_COSL

// Define if the compiler/host combination has __builtin_fabs
#undef HAVE___BUILTIN_FABS

// Define if the compiler/host combination has __builtin_fabsf
#undef HAVE___BUILTIN_FABSF

// Define if the compiler/host combination has __builtin_fabsl
#undef HAVE___BUILTIN_FABSL

// Define if the compiler/host combination has __builtin_sin
#undef HAVE___BUILTIN_SIN

// Define if the compiler/host combination has __builtin_sinf
#undef HAVE___BUILTIN_SINF

// Define if the compiler/host combination has __builtin_sinl
#undef HAVE___BUILTIN_SINL

// Define if the compiler/host combination has __builtin_sqrt
#undef HAVE___BUILTIN_SQRT

// Define if the compiler/host combination has __builtin_sqrtf
#undef HAVE___BUILTIN_SQRTF

// Define if the compiler/host combination has __builtin_sqrtl
#undef HAVE___BUILTIN_SQRTL

// Define if poll is available in <poll.h>.
#undef HAVE_POLL

// Define if S_ISREG (Posix) is available in <sys/stat.h>.
#undef HAVE_S_ISREG

// Define if S_IFREG is available in <sys/stat.h>.
#undef HAVE_S_IFREG

// Define if LC_MESSAGES is available in <locale.h>.
#undef HAVE_LC_MESSAGES

// Define if <float.h> exists.
#undef HAVE_FLOAT_H

// Define if modf is present in <math.h>
#undef HAVE_MODF

// @BOTTOM@
//
// Systems that have certain non-standard functions prefixed with an
// underscore, we'll handle those here. Must come after config.h.in.
//
#if defined (HAVE__ISNAN) && ! defined (HAVE_ISNAN)
# define HAVE_ISNAN 1
# define isnan _isnan
#endif

#if defined (HAVE__ISNANF) && ! defined (HAVE_ISNANF)
# define HAVE_ISNANF 1
# define isnanf _isnanf
#endif

#if defined (HAVE__ISNANL) && ! defined (HAVE_ISNANL)
# define HAVE_ISNANL 1
# define isnanl _isnanl
#endif

#if defined (HAVE__ISINF) && ! defined (HAVE_ISINF)
# define HAVE_ISINF 1
# define isinf _isinf
#endif

#if defined (HAVE__ISINFF) && ! defined (HAVE_ISINFF)
# define HAVE_ISINFF 1
# define isinff _isinff
#endif

#if defined (HAVE__ISINFL) && ! defined (HAVE_ISINFL)
# define HAVE_ISINFL 1
# define isinfl _isinfl
#endif

#if defined (HAVE__COPYSIGN) && ! defined (HAVE_COPYSIGN)
# define HAVE_COPYSIGN 1
# define copysign _copysign
#endif

#if defined (HAVE__COPYSIGNL) && ! defined (HAVE_COPYSIGNL)
# define HAVE_COPYSIGNL 1
# define copysignl _copysignl
#endif

#if defined (HAVE__COSF) && ! defined (HAVE_COSF)
# define HAVE_COSF 1
# define cosf _cosf
#endif

#if defined (HAVE__ACOSF) && ! defined (HAVE_ACOSF)
# define HAVE_ACOSF 1
# define acosf _acosf
#endif

#if defined (HAVE__ACOSL) && ! defined (HAVE_ACOSL)
# define HAVE_ACOSL 1
# define acosl _acosl
#endif

#if defined (HAVE__ASINF) && ! defined (HAVE_ASINF)
# define HAVE_ASINF 1
# define asinf _asinf
#endif

#if defined (HAVE__ASINL) && ! defined (HAVE_ASINL)
# define HAVE_ASINL 1
# define asinl _asinl
#endif

#if defined (HAVE__ATANF) && ! defined (HAVE_ATANF)
# define HAVE_ATANF 1
# define atanf _atanf
#endif

#if defined (HAVE__ATANL) && ! defined (HAVE_ATANL)
# define HAVE_ATANL 1
# define atanl _atanl
#endif

#if defined (HAVE__CEILF) && ! defined (HAVE_CEILF)
# define HAVE_CEILF 1
# define aceil _ceilf
#endif

#if defined (HAVE__CEILL) && ! defined (HAVE_CEILL)
# define HAVE_CEILL 1
# define aceil _ceill
#endif

#if defined (HAVE__COSHF) && ! defined (HAVE_COSHF)
# define HAVE_COSHF 1
# define coshf _coshf
#endif

#if defined (HAVE__COSL) && ! defined (HAVE_COSL)
# define HAVE_COSL 1
# define cosl _cosl
#endif

#if defined (HAVE__LOGF) && ! defined (HAVE_LOGF)
# define HAVE_LOGF 1
# define logf _logf
#endif

#if defined (HAVE__COSHL) && ! defined (HAVE_COSHL)
# define HAVE_COSHL 1
# define coshl _coshl
#endif

#if defined (HAVE__EXPF) && ! defined (HAVE_EXPF)
# define HAVE_EXPF 1
# define expf _expf
#endif

#if defined (HAVE__EXPL) && ! defined (HAVE_EXPL)
# define HAVE_EXPL 1
# define expl _expl
#endif

#if defined (HAVE__FABSF) && ! defined (HAVE_FABSF)
# define HAVE_FABSF 1
# define fabsf _fabsf
#endif

#if defined (HAVE__FABSL) && ! defined (HAVE_FABSL)
# define HAVE_FABSL 1
# define fabsl _fabsl
#endif

#if defined (HAVE__FLOORF) && ! defined (HAVE_FLOORF)
# define HAVE_FLOORF 1
# define floorf _floorf
#endif

#if defined (HAVE__FLOORL) && ! defined (HAVE_FLOORL)
# define HAVE_FLOORL 1
# define floorl _floorl
#endif

#if defined (HAVE__FMODF) && ! defined (HAVE_FMODF)
# define HAVE_FMODF 1
# define fmodf _fmodf
#endif

#if defined (HAVE__FMODL) && ! defined (HAVE_FMODL)
# define HAVE_FMODL 1
# define fmodl _fmodl
#endif

#if defined (HAVE__FREXPF) && ! defined (HAVE_FREXPF)
# define HAVE_FREXPF 1
# define frexpf _frexpf
#endif

#if defined (HAVE__FREXPL) && ! defined (HAVE_FREXPL)
# define HAVE_FREXPL 1
# define frexpl _frexpl
#endif

#if defined (HAVE__LDEXPF) && ! defined (HAVE_LDEXPF)
# define HAVE_LDEXPF 1
# define ldexpf _ldexpf
#endif

#if defined (HAVE__LDEXPL) && ! defined (HAVE_LDEXPL)
# define HAVE_LDEXPL 1
# define ldexpl _ldexpl
#endif

#if defined (HAVE__LOG10F) && ! defined (HAVE_LOG10F)
# define HAVE_LOG10F 1
# define log10f _log10f
#endif

#if defined (HAVE__LOGL) && ! defined (HAVE_LOGL)
# define HAVE_LOGL 1
# define logl _logl
#endif

#if defined (HAVE__POWF) && ! defined (HAVE_POWF)
# define HAVE_POWF 1
# define powf _powf
#endif

#if defined (HAVE__LOG10L) && ! defined (HAVE_LOG10L)
# define HAVE_LOG10L 1
# define log10l _log10l
#endif

#if defined (HAVE__MODF) && ! defined (HAVE_MODF)
# define HAVE_MODF 1
# define modf _modf
#endif

#if defined (HAVE__MODL) && ! defined (HAVE_MODL)
# define HAVE_MODL 1
# define modl _modl
#endif

#if defined (HAVE__SINF) && ! defined (HAVE_SINF)
# define HAVE_SINF 1
# define sinf _sinf
#endif

#if defined (HAVE__POWL) && ! defined (HAVE_POWL)
# define HAVE_POWL 1
# define powl _powl
#endif

#if defined (HAVE__SINHF) && ! defined (HAVE_SINHF)
# define HAVE_SINHF 1
# define sinhf _sinhf
#endif

#if defined (HAVE__SINL) && ! defined (HAVE_SINL)
# define HAVE_SINL 1
# define sinl _sinl
#endif

#if defined (HAVE__SQRTF) && ! defined (HAVE_SQRTF)
# define HAVE_SQRTF 1
# define sqrtf _sqrtf
#endif

#if defined (HAVE__SINHL) && ! defined (HAVE_SINHL)
# define HAVE_SINHL 1
# define sinhl _sinhl
#endif

#if defined (HAVE__TANF) && ! defined (HAVE_TANF)
# define HAVE_TANF 1
# define tanf _tanf
#endif

#if defined (HAVE__SQRTL) && ! defined (HAVE_SQRTL)
# define HAVE_SQRTL 1
# define sqrtl _sqrtl
#endif

#if defined (HAVE__TANHF) && ! defined (HAVE_TANHF)
# define HAVE_TANHF 1
# define tanhf _tanhf
#endif

#if defined (HAVE__TANL) && ! defined (HAVE_TANL)
# define HAVE_TANF 1
# define tanf _tanf
#endif

#if defined (HAVE__STRTOF) && ! defined (HAVE_STRTOF)
# define HAVE_STRTOF 1
# define strtof _strtof
#endif

#if defined (HAVE__TANHL) && ! defined (HAVE_TANHL)
# define HAVE_TANHL 1
# define tanhl _tanhl
#endif

#if defined (HAVE__STRTOLD) && ! defined (HAVE_STRTOLD)
# define HAVE_STRTOLD 1
# define strtold _strtold
#endif

#if defined (HAVE__SINCOS) && ! defined (HAVE_SINCOS)
# define HAVE_SINCOS 1
# define sincos _sincos
#endif

#if defined (HAVE__SINCOSF) && ! defined (HAVE_SINCOSF)
# define HAVE_SINCOSF 1
# define sincosf _sincosf
#endif

#if defined (HAVE__SINCOSL) && ! defined (HAVE_SINCOSL)
# define HAVE_SINCOSL 1
# define sincosl _sincosl
#endif

#if defined (HAVE__FINITE) && ! defined (HAVE_FINITE)
# define HAVE_FINITE 1
# define finite _finite
#endif

#if defined (HAVE__FINITEF) && ! defined (HAVE_FINITEF)
# define HAVE_FINITEF 1
# define finitef _finitef
#endif

#if defined (HAVE__FINITEL) && ! defined (HAVE_FINITEL)
# define HAVE_FINITEL 1
# define finitel _finitel
#endif

#if defined (HAVE__QFINITE) && ! defined (HAVE_QFINITE)
# define HAVE_QFINITE 1
# define qfinite _qfinite
#endif

#if defined (HAVE__FPCLASS) && ! defined (HAVE_FPCLASS)
# define HAVE_FPCLASS 1
# define fpclass _fpclass
#endif

#if defined (HAVE__QFPCLASS) && ! defined (HAVE_QFPCLASS)
# define HAVE_QFPCLASS 1
# define qfpclass _qfpclass
#endif

