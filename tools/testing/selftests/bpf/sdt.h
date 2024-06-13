/* <sys/sdt.h> - Systemtap static probe definition macros.

   This file is dedicated to the public domain, pursuant to CC0
   (https://creativecommons.org/publicdomain/zero/1.0/)
*/

#ifndef _SYS_SDT_H
#define _SYS_SDT_H    1

/*
  This file defines a family of macros

       STAP_PROBEn(op1, ..., opn)

  that emit a nop into the instruction stream, and some data into an auxiliary
  note section.  The data in the note section describes the operands, in terms
  of size and location.  Each location is encoded as assembler operand string.
  Consumer tools such as gdb or systemtap insert breakpoints on top of
  the nop, and decode the location operand-strings, like an assembler,
  to find the values being passed.

  The operand strings are selected by the compiler for each operand.
  They are constrained by gcc inline-assembler codes.  The default is:

  #define STAP_SDT_ARG_CONSTRAINT nor

  This is a good default if the operands tend to be integral and
  moderate in number (smaller than number of registers).  In other
  cases, the compiler may report "'asm' requires impossible reload" or
  similar.  In this case, consider simplifying the macro call (fewer
  and simpler operands), reduce optimization, or override the default
  constraints string via:

  #define STAP_SDT_ARG_CONSTRAINT g
  #include <sys/sdt.h>

  See also:
  https://sourceware.org/systemtap/wiki/UserSpaceProbeImplementation
  https://gcc.gnu.org/onlinedocs/gcc/Constraints.html
 */



#ifdef __ASSEMBLER__
# define _SDT_PROBE(provider, name, n, arglist)	\
  _SDT_ASM_BODY(provider, name, _SDT_ASM_SUBSTR_1, (_SDT_DEPAREN_##n arglist)) \
  _SDT_ASM_BASE
# define _SDT_ASM_1(x)			x;
# define _SDT_ASM_2(a, b)		a,b;
# define _SDT_ASM_3(a, b, c)		a,b,c;
# define _SDT_ASM_5(a, b, c, d, e)	a,b,c,d,e;
# define _SDT_ASM_STRING_1(x)		.asciz #x;
# define _SDT_ASM_SUBSTR_1(x)		.ascii #x;
# define _SDT_DEPAREN_0()				/* empty */
# define _SDT_DEPAREN_1(a)				a
# define _SDT_DEPAREN_2(a,b)				a b
# define _SDT_DEPAREN_3(a,b,c)				a b c
# define _SDT_DEPAREN_4(a,b,c,d)			a b c d
# define _SDT_DEPAREN_5(a,b,c,d,e)			a b c d e
# define _SDT_DEPAREN_6(a,b,c,d,e,f)			a b c d e f
# define _SDT_DEPAREN_7(a,b,c,d,e,f,g)			a b c d e f g
# define _SDT_DEPAREN_8(a,b,c,d,e,f,g,h)		a b c d e f g h
# define _SDT_DEPAREN_9(a,b,c,d,e,f,g,h,i)		a b c d e f g h i
# define _SDT_DEPAREN_10(a,b,c,d,e,f,g,h,i,j)		a b c d e f g h i j
# define _SDT_DEPAREN_11(a,b,c,d,e,f,g,h,i,j,k)		a b c d e f g h i j k
# define _SDT_DEPAREN_12(a,b,c,d,e,f,g,h,i,j,k,l)	a b c d e f g h i j k l
#else
#if defined _SDT_HAS_SEMAPHORES
#define _SDT_NOTE_SEMAPHORE_USE(provider, name) \
  __asm__ __volatile__ ("" :: "m" (provider##_##name##_semaphore));
#else
#define _SDT_NOTE_SEMAPHORE_USE(provider, name)
#endif

# define _SDT_PROBE(provider, name, n, arglist) \
  do {									    \
    _SDT_NOTE_SEMAPHORE_USE(provider, name); \
    __asm__ __volatile__ (_SDT_ASM_BODY(provider, name, _SDT_ASM_ARGS, (n)) \
			  :: _SDT_ASM_OPERANDS_##n arglist);		    \
    __asm__ __volatile__ (_SDT_ASM_BASE);				    \
  } while (0)
# define _SDT_S(x)			#x
# define _SDT_ASM_1(x)			_SDT_S(x) "\n"
# define _SDT_ASM_2(a, b)		_SDT_S(a) "," _SDT_S(b) "\n"
# define _SDT_ASM_3(a, b, c)		_SDT_S(a) "," _SDT_S(b) "," \
					_SDT_S(c) "\n"
# define _SDT_ASM_5(a, b, c, d, e)	_SDT_S(a) "," _SDT_S(b) "," \
					_SDT_S(c) "," _SDT_S(d) "," \
					_SDT_S(e) "\n"
# define _SDT_ASM_ARGS(n)		_SDT_ASM_TEMPLATE_##n
# define _SDT_ASM_STRING_1(x)		_SDT_ASM_1(.asciz #x)
# define _SDT_ASM_SUBSTR_1(x)		_SDT_ASM_1(.ascii #x)

# define _SDT_ARGFMT(no)                _SDT_ASM_1(_SDT_SIGN %n[_SDT_S##no]) \
                                        _SDT_ASM_1(_SDT_SIZE %n[_SDT_S##no]) \
                                        _SDT_ASM_1(_SDT_TYPE %n[_SDT_S##no]) \
                                        _SDT_ASM_SUBSTR(_SDT_ARGTMPL(_SDT_A##no))


# ifndef STAP_SDT_ARG_CONSTRAINT
# if defined __powerpc__
# define STAP_SDT_ARG_CONSTRAINT        nZr
# elif defined __arm__
# define STAP_SDT_ARG_CONSTRAINT        g
# else
# define STAP_SDT_ARG_CONSTRAINT        nor
# endif
# endif

# define _SDT_STRINGIFY(x)              #x
# define _SDT_ARG_CONSTRAINT_STRING(x)  _SDT_STRINGIFY(x)
/* _SDT_S encodes the size and type as 0xSSTT which is decoded by the assembler
   macros _SDT_SIZE and _SDT_TYPE */
# define _SDT_ARG(n, x)				    \
  [_SDT_S##n] "n" ((_SDT_ARGSIGNED (x) ? (int)-1 : 1) * (-(((int) _SDT_ARGSIZE (x)) << 8) + (-(0x7f & __builtin_classify_type (x))))), \
  [_SDT_A##n] _SDT_ARG_CONSTRAINT_STRING (STAP_SDT_ARG_CONSTRAINT) (_SDT_ARGVAL (x))
#endif
#define _SDT_ASM_STRING(x)		_SDT_ASM_STRING_1(x)
#define _SDT_ASM_SUBSTR(x)		_SDT_ASM_SUBSTR_1(x)

#define _SDT_ARGARRAY(x)	(__builtin_classify_type (x) == 14	\
				 || __builtin_classify_type (x) == 5)

#ifdef __cplusplus
# define _SDT_ARGSIGNED(x)	(!_SDT_ARGARRAY (x) \
				 && __sdt_type<__typeof (x)>::__sdt_signed)
# define _SDT_ARGSIZE(x)	(_SDT_ARGARRAY (x) \
				 ? sizeof (void *) : sizeof (x))
# define _SDT_ARGVAL(x)		(x)

# include <cstddef>

template<typename __sdt_T>
struct __sdt_type
{
  static const bool __sdt_signed = false;
};
  
#define __SDT_ALWAYS_SIGNED(T) \
template<> struct __sdt_type<T> { static const bool __sdt_signed = true; };
#define __SDT_COND_SIGNED(T,CT)						\
template<> struct __sdt_type<T> { static const bool __sdt_signed = ((CT)(-1) < 1); };
__SDT_ALWAYS_SIGNED(signed char)
__SDT_ALWAYS_SIGNED(short)
__SDT_ALWAYS_SIGNED(int)
__SDT_ALWAYS_SIGNED(long)
__SDT_ALWAYS_SIGNED(long long)
__SDT_ALWAYS_SIGNED(volatile signed char)
__SDT_ALWAYS_SIGNED(volatile short)
__SDT_ALWAYS_SIGNED(volatile int)
__SDT_ALWAYS_SIGNED(volatile long)
__SDT_ALWAYS_SIGNED(volatile long long)
__SDT_ALWAYS_SIGNED(const signed char)
__SDT_ALWAYS_SIGNED(const short)
__SDT_ALWAYS_SIGNED(const int)
__SDT_ALWAYS_SIGNED(const long)
__SDT_ALWAYS_SIGNED(const long long)
__SDT_ALWAYS_SIGNED(const volatile signed char)
__SDT_ALWAYS_SIGNED(const volatile short)
__SDT_ALWAYS_SIGNED(const volatile int)
__SDT_ALWAYS_SIGNED(const volatile long)
__SDT_ALWAYS_SIGNED(const volatile long long)
__SDT_COND_SIGNED(char, char)
__SDT_COND_SIGNED(wchar_t, wchar_t)
__SDT_COND_SIGNED(volatile char, char)
__SDT_COND_SIGNED(volatile wchar_t, wchar_t)
__SDT_COND_SIGNED(const char, char)
__SDT_COND_SIGNED(const wchar_t, wchar_t)
__SDT_COND_SIGNED(const volatile char, char)
__SDT_COND_SIGNED(const volatile wchar_t, wchar_t)
#if defined (__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4))
/* __SDT_COND_SIGNED(char16_t) */
/* __SDT_COND_SIGNED(char32_t) */
#endif

template<typename __sdt_E>
struct __sdt_type<__sdt_E[]> : public __sdt_type<__sdt_E *> {};

template<typename __sdt_E, size_t __sdt_N>
struct __sdt_type<__sdt_E[__sdt_N]> : public __sdt_type<__sdt_E *> {};

#elif !defined(__ASSEMBLER__)
__extension__ extern unsigned long long __sdt_unsp;
# define _SDT_ARGINTTYPE(x)						\
  __typeof (__builtin_choose_expr (((__builtin_classify_type (x)	\
				     + 3) & -4) == 4, (x), 0U))
# define _SDT_ARGSIGNED(x)						\
  (!__extension__							\
   (__builtin_constant_p ((((unsigned long long)			\
			    (_SDT_ARGINTTYPE (x)) __sdt_unsp)		\
			   & ((unsigned long long)1 << (sizeof (unsigned long long)	\
				       * __CHAR_BIT__ - 1))) == 0)	\
    || (_SDT_ARGINTTYPE (x)) -1 > (_SDT_ARGINTTYPE (x)) 0))
# define _SDT_ARGSIZE(x)	\
  (_SDT_ARGARRAY (x) ? sizeof (void *) : sizeof (x))
# define _SDT_ARGVAL(x)		(x)
#endif

#if defined __powerpc__ || defined __powerpc64__
# define _SDT_ARGTMPL(id)	%I[id]%[id]
#elif defined __i386__
# define _SDT_ARGTMPL(id)	%k[id]  /* gcc.gnu.org/PR80115 sourceware.org/PR24541 */
#else
# define _SDT_ARGTMPL(id)	%[id]
#endif

/* NB: gdb PR24541 highlighted an unspecified corner of the sdt.h
   operand note format.

   The named register may be a longer or shorter (!) alias for the
   storage where the value in question is found.  For example, on
   i386, 64-bit value may be put in register pairs, and the register
   name stored would identify just one of them.  Previously, gcc was
   asked to emit the %w[id] (16-bit alias of some registers holding
   operands), even when a wider 32-bit value was used.

   Bottom line: the byte-width given before the @ sign governs.  If
   there is a mismatch between that width and that of the named
   register, then a sys/sdt.h note consumer may need to employ
   architecture-specific heuristics to figure out where the compiler
   has actually put the complete value.
*/

#ifdef __LP64__
# define _SDT_ASM_ADDR	.8byte
#else
# define _SDT_ASM_ADDR	.4byte
#endif

/* The ia64 and s390 nop instructions take an argument. */
#if defined(__ia64__) || defined(__s390__) || defined(__s390x__)
#define _SDT_NOP	nop 0
#else
#define _SDT_NOP	nop
#endif

#define _SDT_NOTE_NAME	"stapsdt"
#define _SDT_NOTE_TYPE	3

/* If the assembler supports the necessary feature, then we can play
   nice with code in COMDAT sections, which comes up in C++ code.
   Without that assembler support, some combinations of probe placements
   in certain kinds of C++ code may produce link-time errors.  */
#include "sdt-config.h"
#if _SDT_ASM_SECTION_AUTOGROUP_SUPPORT
# define _SDT_ASM_AUTOGROUP "?"
#else
# define _SDT_ASM_AUTOGROUP ""
#endif

#define _SDT_DEF_MACROS							     \
	_SDT_ASM_1(.altmacro)						     \
	_SDT_ASM_1(.macro _SDT_SIGN x)				     	     \
	_SDT_ASM_3(.pushsection .note.stapsdt,"","note")		     \
	_SDT_ASM_1(.iflt \\x)						     \
	_SDT_ASM_1(.ascii "-")						     \
	_SDT_ASM_1(.endif)						     \
	_SDT_ASM_1(.popsection)						     \
	_SDT_ASM_1(.endm)						     \
	_SDT_ASM_1(.macro _SDT_SIZE_ x)					     \
	_SDT_ASM_3(.pushsection .note.stapsdt,"","note")		     \
	_SDT_ASM_1(.ascii "\x")						     \
	_SDT_ASM_1(.popsection)						     \
	_SDT_ASM_1(.endm)						     \
	_SDT_ASM_1(.macro _SDT_SIZE x)					     \
	_SDT_ASM_1(_SDT_SIZE_ %%((-(-\\x*((-\\x>0)-(-\\x<0))))>>8))	     \
	_SDT_ASM_1(.endm)						     \
	_SDT_ASM_1(.macro _SDT_TYPE_ x)				             \
	_SDT_ASM_3(.pushsection .note.stapsdt,"","note")		     \
	_SDT_ASM_2(.ifc 8,\\x)					     	     \
	_SDT_ASM_1(.ascii "f")						     \
	_SDT_ASM_1(.endif)						     \
	_SDT_ASM_1(.ascii "@")						     \
	_SDT_ASM_1(.popsection)						     \
	_SDT_ASM_1(.endm)						     \
	_SDT_ASM_1(.macro _SDT_TYPE x)				     	     \
	_SDT_ASM_1(_SDT_TYPE_ %%((\\x)&(0xff)))			     \
	_SDT_ASM_1(.endm)

#define _SDT_UNDEF_MACROS						      \
  _SDT_ASM_1(.purgem _SDT_SIGN)						      \
  _SDT_ASM_1(.purgem _SDT_SIZE_)					      \
  _SDT_ASM_1(.purgem _SDT_SIZE)						      \
  _SDT_ASM_1(.purgem _SDT_TYPE_)					      \
  _SDT_ASM_1(.purgem _SDT_TYPE)

#define _SDT_ASM_BODY(provider, name, pack_args, args, ...)		      \
  _SDT_DEF_MACROS							      \
  _SDT_ASM_1(990:	_SDT_NOP)					      \
  _SDT_ASM_3(		.pushsection .note.stapsdt,_SDT_ASM_AUTOGROUP,"note") \
  _SDT_ASM_1(		.balign 4)					      \
  _SDT_ASM_3(		.4byte 992f-991f, 994f-993f, _SDT_NOTE_TYPE)	      \
  _SDT_ASM_1(991:	.asciz _SDT_NOTE_NAME)				      \
  _SDT_ASM_1(992:	.balign 4)					      \
  _SDT_ASM_1(993:	_SDT_ASM_ADDR 990b)				      \
  _SDT_ASM_1(		_SDT_ASM_ADDR _.stapsdt.base)			      \
  _SDT_SEMAPHORE(provider,name)						      \
  _SDT_ASM_STRING(provider)						      \
  _SDT_ASM_STRING(name)							      \
  pack_args args							      \
  _SDT_ASM_SUBSTR(\x00)							      \
  _SDT_UNDEF_MACROS							      \
  _SDT_ASM_1(994:	.balign 4)					      \
  _SDT_ASM_1(		.popsection)

#define _SDT_ASM_BASE							      \
  _SDT_ASM_1(.ifndef _.stapsdt.base)					      \
  _SDT_ASM_5(		.pushsection .stapsdt.base,"aG","progbits",	      \
							.stapsdt.base,comdat) \
  _SDT_ASM_1(		.weak _.stapsdt.base)				      \
  _SDT_ASM_1(		.hidden _.stapsdt.base)				      \
  _SDT_ASM_1(	_.stapsdt.base: .space 1)				      \
  _SDT_ASM_2(		.size _.stapsdt.base, 1)			      \
  _SDT_ASM_1(		.popsection)					      \
  _SDT_ASM_1(.endif)

#if defined _SDT_HAS_SEMAPHORES
#define _SDT_SEMAPHORE(p,n) \
	_SDT_ASM_1(		_SDT_ASM_ADDR p##_##n##_semaphore)
#else
#define _SDT_SEMAPHORE(p,n) _SDT_ASM_1(		_SDT_ASM_ADDR 0)
#endif

#define _SDT_ASM_BLANK _SDT_ASM_SUBSTR(\x20)
#define _SDT_ASM_TEMPLATE_0		/* no arguments */
#define _SDT_ASM_TEMPLATE_1		_SDT_ARGFMT(1)
#define _SDT_ASM_TEMPLATE_2		_SDT_ASM_TEMPLATE_1 _SDT_ASM_BLANK _SDT_ARGFMT(2)
#define _SDT_ASM_TEMPLATE_3		_SDT_ASM_TEMPLATE_2 _SDT_ASM_BLANK _SDT_ARGFMT(3)
#define _SDT_ASM_TEMPLATE_4		_SDT_ASM_TEMPLATE_3 _SDT_ASM_BLANK _SDT_ARGFMT(4)
#define _SDT_ASM_TEMPLATE_5		_SDT_ASM_TEMPLATE_4 _SDT_ASM_BLANK _SDT_ARGFMT(5)
#define _SDT_ASM_TEMPLATE_6		_SDT_ASM_TEMPLATE_5 _SDT_ASM_BLANK _SDT_ARGFMT(6)
#define _SDT_ASM_TEMPLATE_7		_SDT_ASM_TEMPLATE_6 _SDT_ASM_BLANK _SDT_ARGFMT(7)
#define _SDT_ASM_TEMPLATE_8		_SDT_ASM_TEMPLATE_7 _SDT_ASM_BLANK _SDT_ARGFMT(8)
#define _SDT_ASM_TEMPLATE_9		_SDT_ASM_TEMPLATE_8 _SDT_ASM_BLANK _SDT_ARGFMT(9)
#define _SDT_ASM_TEMPLATE_10		_SDT_ASM_TEMPLATE_9 _SDT_ASM_BLANK _SDT_ARGFMT(10)
#define _SDT_ASM_TEMPLATE_11		_SDT_ASM_TEMPLATE_10 _SDT_ASM_BLANK _SDT_ARGFMT(11)
#define _SDT_ASM_TEMPLATE_12		_SDT_ASM_TEMPLATE_11 _SDT_ASM_BLANK _SDT_ARGFMT(12)
#define _SDT_ASM_OPERANDS_0()		[__sdt_dummy] "g" (0)
#define _SDT_ASM_OPERANDS_1(arg1)	_SDT_ARG(1, arg1)
#define _SDT_ASM_OPERANDS_2(arg1, arg2) \
  _SDT_ASM_OPERANDS_1(arg1), _SDT_ARG(2, arg2)
#define _SDT_ASM_OPERANDS_3(arg1, arg2, arg3) \
  _SDT_ASM_OPERANDS_2(arg1, arg2), _SDT_ARG(3, arg3)
#define _SDT_ASM_OPERANDS_4(arg1, arg2, arg3, arg4) \
  _SDT_ASM_OPERANDS_3(arg1, arg2, arg3), _SDT_ARG(4, arg4)
#define _SDT_ASM_OPERANDS_5(arg1, arg2, arg3, arg4, arg5) \
  _SDT_ASM_OPERANDS_4(arg1, arg2, arg3, arg4), _SDT_ARG(5, arg5)
#define _SDT_ASM_OPERANDS_6(arg1, arg2, arg3, arg4, arg5, arg6) \
  _SDT_ASM_OPERANDS_5(arg1, arg2, arg3, arg4, arg5), _SDT_ARG(6, arg6)
#define _SDT_ASM_OPERANDS_7(arg1, arg2, arg3, arg4, arg5, arg6, arg7) \
  _SDT_ASM_OPERANDS_6(arg1, arg2, arg3, arg4, arg5, arg6), _SDT_ARG(7, arg7)
#define _SDT_ASM_OPERANDS_8(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8) \
  _SDT_ASM_OPERANDS_7(arg1, arg2, arg3, arg4, arg5, arg6, arg7), \
    _SDT_ARG(8, arg8)
#define _SDT_ASM_OPERANDS_9(arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9) \
  _SDT_ASM_OPERANDS_8(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8), \
    _SDT_ARG(9, arg9)
#define _SDT_ASM_OPERANDS_10(arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10) \
  _SDT_ASM_OPERANDS_9(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9), \
    _SDT_ARG(10, arg10)
#define _SDT_ASM_OPERANDS_11(arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10,arg11) \
  _SDT_ASM_OPERANDS_10(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10), \
    _SDT_ARG(11, arg11)
#define _SDT_ASM_OPERANDS_12(arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10,arg11,arg12) \
  _SDT_ASM_OPERANDS_11(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11), \
    _SDT_ARG(12, arg12)

/* These macros can be used in C, C++, or assembly code.
   In assembly code the arguments should use normal assembly operand syntax.  */

#define STAP_PROBE(provider, name) \
  _SDT_PROBE(provider, name, 0, ())
#define STAP_PROBE1(provider, name, arg1) \
  _SDT_PROBE(provider, name, 1, (arg1))
#define STAP_PROBE2(provider, name, arg1, arg2) \
  _SDT_PROBE(provider, name, 2, (arg1, arg2))
#define STAP_PROBE3(provider, name, arg1, arg2, arg3) \
  _SDT_PROBE(provider, name, 3, (arg1, arg2, arg3))
#define STAP_PROBE4(provider, name, arg1, arg2, arg3, arg4) \
  _SDT_PROBE(provider, name, 4, (arg1, arg2, arg3, arg4))
#define STAP_PROBE5(provider, name, arg1, arg2, arg3, arg4, arg5) \
  _SDT_PROBE(provider, name, 5, (arg1, arg2, arg3, arg4, arg5))
#define STAP_PROBE6(provider, name, arg1, arg2, arg3, arg4, arg5, arg6)	\
  _SDT_PROBE(provider, name, 6, (arg1, arg2, arg3, arg4, arg5, arg6))
#define STAP_PROBE7(provider, name, arg1, arg2, arg3, arg4, arg5, arg6, arg7) \
  _SDT_PROBE(provider, name, 7, (arg1, arg2, arg3, arg4, arg5, arg6, arg7))
#define STAP_PROBE8(provider,name,arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8) \
  _SDT_PROBE(provider, name, 8, (arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8))
#define STAP_PROBE9(provider,name,arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9)\
  _SDT_PROBE(provider, name, 9, (arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9))
#define STAP_PROBE10(provider,name,arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10) \
  _SDT_PROBE(provider, name, 10, \
	     (arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10))
#define STAP_PROBE11(provider,name,arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10,arg11) \
  _SDT_PROBE(provider, name, 11, \
	     (arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10,arg11))
#define STAP_PROBE12(provider,name,arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10,arg11,arg12) \
  _SDT_PROBE(provider, name, 12, \
	     (arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10,arg11,arg12))

/* This STAP_PROBEV macro can be used in variadic scenarios, where the
   number of probe arguments is not known until compile time.  Since
   variadic macro support may vary with compiler options, you must
   pre-#define SDT_USE_VARIADIC to enable this type of probe.

   The trick to count __VA_ARGS__ was inspired by this post by
   Laurent Deniau <laurent.deniau@cern.ch>:
       http://groups.google.com/group/comp.std.c/msg/346fc464319b1ee5

   Note that our _SDT_NARG is called with an extra 0 arg that's not
   counted, so we don't have to worry about the behavior of macros
   called without any arguments.  */

#define _SDT_NARG(...) __SDT_NARG(__VA_ARGS__, 12,11,10,9,8,7,6,5,4,3,2,1,0)
#define __SDT_NARG(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12, N, ...) N
#ifdef SDT_USE_VARIADIC
#define _SDT_PROBE_N(provider, name, N, ...) \
  _SDT_PROBE(provider, name, N, (__VA_ARGS__))
#define STAP_PROBEV(provider, name, ...) \
  _SDT_PROBE_N(provider, name, _SDT_NARG(0, ##__VA_ARGS__), ##__VA_ARGS__)
#endif

/* These macros are for use in asm statements.  You must compile
   with -std=gnu99 or -std=c99 to use the STAP_PROBE_ASM macro.

   The STAP_PROBE_ASM macro generates a quoted string to be used in the
   template portion of the asm statement, concatenated with strings that
   contain the actual assembly code around the probe site.

   For example:

	asm ("before\n"
	     STAP_PROBE_ASM(provider, fooprobe, %eax 4(%esi))
	     "after");

   emits the assembly code for "before\nafter", with a probe in between.
   The probe arguments are the %eax register, and the value of the memory
   word located 4 bytes past the address in the %esi register.  Note that
   because this is a simple asm, not a GNU C extended asm statement, these
   % characters do not need to be doubled to generate literal %reg names.

   In a GNU C extended asm statement, the probe arguments can be specified
   using the macro STAP_PROBE_ASM_TEMPLATE(n) for n arguments.  The paired
   macro STAP_PROBE_ASM_OPERANDS gives the C values of these probe arguments,
   and appears in the input operand list of the asm statement.  For example:

	asm ("someinsn %0,%1\n" // %0 is output operand, %1 is input operand
	     STAP_PROBE_ASM(provider, fooprobe, STAP_PROBE_ASM_TEMPLATE(3))
	     "otherinsn %[namedarg]"
	     : "r" (outvar)
	     : "g" (some_value), [namedarg] "i" (1234),
	       STAP_PROBE_ASM_OPERANDS(3, some_value, some_ptr->field, 1234));

    This is just like writing:

	STAP_PROBE3(provider, fooprobe, some_value, some_ptr->field, 1234));

    but the probe site is right between "someinsn" and "otherinsn".

    The probe arguments in STAP_PROBE_ASM can be given as assembly
    operands instead, even inside a GNU C extended asm statement.
    Note that these can use operand templates like %0 or %[name],
    and likewise they must write %%reg for a literal operand of %reg.  */

#define _SDT_ASM_BODY_1(p,n,...) _SDT_ASM_BODY(p,n,_SDT_ASM_SUBSTR,(__VA_ARGS__))
#define _SDT_ASM_BODY_2(p,n,...) _SDT_ASM_BODY(p,n,/*_SDT_ASM_STRING */,__VA_ARGS__)
#define _SDT_ASM_BODY_N2(p,n,no,...) _SDT_ASM_BODY_ ## no(p,n,__VA_ARGS__)
#define _SDT_ASM_BODY_N1(p,n,no,...) _SDT_ASM_BODY_N2(p,n,no,__VA_ARGS__)
#define _SDT_ASM_BODY_N(p,n,...) _SDT_ASM_BODY_N1(p,n,_SDT_NARG(0, __VA_ARGS__),__VA_ARGS__)

#if __STDC_VERSION__ >= 199901L
# define STAP_PROBE_ASM(provider, name, ...)		\
  _SDT_ASM_BODY_N(provider, name, __VA_ARGS__)					\
  _SDT_ASM_BASE
# define STAP_PROBE_ASM_OPERANDS(n, ...) _SDT_ASM_OPERANDS_##n(__VA_ARGS__)
#else
# define STAP_PROBE_ASM(provider, name, args)	\
  _SDT_ASM_BODY(provider, name, /* _SDT_ASM_STRING */, (args))	\
  _SDT_ASM_BASE
#endif
#define STAP_PROBE_ASM_TEMPLATE(n) _SDT_ASM_TEMPLATE_##n,"use _SDT_ASM_TEMPLATE_"


/* DTrace compatible macro names.  */
#define DTRACE_PROBE(provider,probe)		\
  STAP_PROBE(provider,probe)
#define DTRACE_PROBE1(provider,probe,parm1)	\
  STAP_PROBE1(provider,probe,parm1)
#define DTRACE_PROBE2(provider,probe,parm1,parm2)	\
  STAP_PROBE2(provider,probe,parm1,parm2)
#define DTRACE_PROBE3(provider,probe,parm1,parm2,parm3) \
  STAP_PROBE3(provider,probe,parm1,parm2,parm3)
#define DTRACE_PROBE4(provider,probe,parm1,parm2,parm3,parm4)	\
  STAP_PROBE4(provider,probe,parm1,parm2,parm3,parm4)
#define DTRACE_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5)	\
  STAP_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5)
#define DTRACE_PROBE6(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6) \
  STAP_PROBE6(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6)
#define DTRACE_PROBE7(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7) \
  STAP_PROBE7(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7)
#define DTRACE_PROBE8(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8) \
  STAP_PROBE8(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8)
#define DTRACE_PROBE9(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9) \
  STAP_PROBE9(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9)
#define DTRACE_PROBE10(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10) \
  STAP_PROBE10(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10)
#define DTRACE_PROBE11(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10,parm11) \
  STAP_PROBE11(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10,parm11)
#define DTRACE_PROBE12(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10,parm11,parm12) \
  STAP_PROBE12(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10,parm11,parm12)


#endif /* sys/sdt.h */
