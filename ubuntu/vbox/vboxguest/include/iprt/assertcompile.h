/** @file
 * IPRT - Compile Time Assertions.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef IPRT_INCLUDED_assertcompile_h
#define IPRT_INCLUDED_assertcompile_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

/** @defgroup  grp_rt_assert_compile    Compile time assertions
 * @ingroup grp_rt
 *
 * These assertions are used to check structure sizes, member/size alignments
 * and similar compile time expressions.
 *
 * @remarks As you might have noticed, the AssertCompile macros don't follow the
 *          coding guidelines wrt to macros supposedly being all uppercase and
 *          underscored.  For various reasons they don't, and nobody has
 *          complained yet.
 *
 * @{
 */

/**
 * RTASSERTTYPE is the type the AssertCompile() macro redefines.
 * It has no other function and shouldn't be used.
 * Visual C++ uses this.
 */
typedef int RTASSERTTYPE[1];

/**
 * RTASSERTVAR is the type the AssertCompile() macro redefines.
 * It has no other function and shouldn't be used.
 * GCC uses this.
 */
#ifdef __GNUC__
RT_C_DECLS_BEGIN
#endif
extern int RTASSERTVAR[1];
#ifdef __GNUC__
RT_C_DECLS_END
#endif

/** @def RTASSERT_HAVE_STATIC_ASSERT
 * Indicates that the compiler implements static_assert(expr, msg).
 */
#ifdef _MSC_VER
# if _MSC_VER >= 1600 && defined(__cplusplus)
#  define RTASSERT_HAVE_STATIC_ASSERT
# endif
#endif
#if defined(__GNUC__) && defined(__GXX_EXPERIMENTAL_CXX0X__)
# define RTASSERT_HAVE_STATIC_ASSERT
#endif
#if RT_CLANG_PREREQ(6, 0)
# if __has_feature(cxx_static_assert) || __has_feature(c_static_assert)
#  define RTASSERT_HAVE_STATIC_ASSERT
# endif
#endif
#ifdef DOXYGEN_RUNNING
# define RTASSERT_HAVE_STATIC_ASSERT
#endif

/** @def AssertCompileNS
 * Asserts that a compile-time expression is true. If it's not break the build.
 *
 * This differs from AssertCompile in that it accepts some more expressions
 * than what C++0x allows - NS = Non-standard.
 *
 * @param   expr    Expression which should be true.
 */
#ifdef __GNUC__
# define AssertCompileNS(expr)  extern int RTASSERTVAR[1] __attribute__((__unused__)), RTASSERTVAR[(expr) ? 1 : 0] __attribute__((__unused__))
#elif defined(__IBMC__) || defined(__IBMCPP__)
# define AssertCompileNS(expr)  extern int RTASSERTVAR[(expr) ? 1 : 0]
#else
# define AssertCompileNS(expr)  typedef int RTASSERTTYPE[(expr) ? 1 : 0]
#endif

/** @def AssertCompile
 * Asserts that a C++0x compile-time expression is true. If it's not break the
 * build.
 * @param   expr    Expression which should be true.
 */
#ifdef RTASSERT_HAVE_STATIC_ASSERT
# ifdef __cplusplus
#  define AssertCompile(expr)    static_assert(!!(expr), #expr)
# else
#  define AssertCompile(expr)    _Static_assert(!!(expr), #expr)
# endif
#else
# define AssertCompile(expr)    AssertCompileNS(expr)
#endif

/** @def RTASSERT_OFFSET_OF()
 * A offsetof() macro suitable for compile time assertions.
 * Both GCC v4 and VisualAge for C++ v3.08 has trouble using RT_OFFSETOF.
 */
#if defined(__GNUC__)
# if __GNUC__ >= 4
#  define RTASSERT_OFFSET_OF(a_Type, a_Member)  __builtin_offsetof(a_Type, a_Member)
# else
#  define RTASSERT_OFFSET_OF(a_Type, a_Member)  RT_OFFSETOF(a_Type, a_Member)
# endif
#elif (defined(__IBMC__) || defined(__IBMCPP__)) && defined(RT_OS_OS2)
# define RTASSERT_OFFSET_OF(a_Type, a_Member)   __offsetof(a_Type, a_Member)
#elif (defined(__WATCOMC__) && defined(__cplusplus))
# define RTASSERT_OFFSET_OF(a_Type, a_Member)   __offsetof(a_Type, a_Member)
#else
# define RTASSERT_OFFSET_OF(a_Type, a_Member)   RT_OFFSETOF(a_Type, a_Member)
#endif


/** @def AssertCompileSize
 * Asserts a size at compile.
 * @param   type    The type.
 * @param   size    The expected type size.
 */
#define AssertCompileSize(type, size) \
    AssertCompile(sizeof(type) == (size))

/** @def AssertCompileSizeAlignment
 * Asserts a size alignment at compile.
 * @param   type    The type.
 * @param   align   The size alignment to assert.
 */
#define AssertCompileSizeAlignment(type, align) \
    AssertCompile(!(sizeof(type) & ((align) - 1)))

/** @def AssertCompileMemberSize
 * Asserts a member offset alignment at compile.
 * @param   type    The type.
 * @param   member  The member.
 * @param   size    The member size to assert.
 */
#define AssertCompileMemberSize(type, member, size) \
    AssertCompile(RT_SIZEOFMEMB(type, member) == (size))

/** @def AssertCompileMemberSizeAlignment
 * Asserts a member size alignment at compile.
 * @param   type    The type.
 * @param   member  The member.
 * @param   align   The member size alignment to assert.
 */
#define AssertCompileMemberSizeAlignment(type, member, align) \
    AssertCompile(!(RT_SIZEOFMEMB(type, member) & ((align) - 1)))

/** @def AssertCompileMemberAlignment
 * Asserts a member offset alignment at compile.
 * @param   type    The type.
 * @param   member  The member.
 * @param   align   The member offset alignment to assert.
 */
#define AssertCompileMemberAlignment(type, member, align) \
    AssertCompile(!(RTASSERT_OFFSET_OF(type, member) & ((align) - 1)))

/** @def AssertCompileMemberOffset
 * Asserts an offset of a structure member at compile.
 * @param   type    The type.
 * @param   member  The member.
 * @param   off     The expected offset.
 */
#define AssertCompileMemberOffset(type, member, off) \
    AssertCompile(RTASSERT_OFFSET_OF(type, member) == (off))

/** @def AssertCompile2MemberOffsets
 * Asserts that two (sub-structure) members in union have the same offset.
 * @param   type    The type.
 * @param   member1 The first member.
 * @param   member2 The second member.
 */
#define AssertCompile2MemberOffsets(type, member1, member2) \
    AssertCompile(RTASSERT_OFFSET_OF(type, member1) == RTASSERT_OFFSET_OF(type, member2))

/** @def AssertCompileAdjacentMembers
 * Asserts that two structure members are adjacent.
 * @param   type    The type.
 * @param   member1 The first member.
 * @param   member2 The second member.
 */
#define AssertCompileAdjacentMembers(type, member1, member2) \
    AssertCompile(RTASSERT_OFFSET_OF(type, member1) + RT_SIZEOFMEMB(type, member1) == RTASSERT_OFFSET_OF(type, member2))

/** @def AssertCompileMembersAtSameOffset
 * Asserts that members of two different structures are at the same offset.
 * @param   type1   The first type.
 * @param   member1 The first member.
 * @param   type2   The second type.
 * @param   member2 The second member.
 */
#define AssertCompileMembersAtSameOffset(type1, member1, type2, member2) \
    AssertCompile(RTASSERT_OFFSET_OF(type1, member1) == RTASSERT_OFFSET_OF(type2, member2))

/** @def AssertCompileMembersSameSize
 * Asserts that members of two different structures have the same size.
 * @param   type1   The first type.
 * @param   member1 The first member.
 * @param   type2   The second type.
 * @param   member2 The second member.
 */
#define AssertCompileMembersSameSize(type1, member1, type2, member2) \
    AssertCompile(RT_SIZEOFMEMB(type1, member1) == RT_SIZEOFMEMB(type2, member2))

/** @def AssertCompileMembersSameSizeAndOffset
 * Asserts that members of two different structures have the same size and are
 * at the same offset.
 * @param   type1   The first type.
 * @param   member1 The first member.
 * @param   type2   The second type.
 * @param   member2 The second member.
 */
#define AssertCompileMembersSameSizeAndOffset(type1, member1, type2, member2) \
    AssertCompile(   RTASSERT_OFFSET_OF(type1, member1) == RTASSERT_OFFSET_OF(type2, member2) \
                  && RT_SIZEOFMEMB(type1, member1) == RT_SIZEOFMEMB(type2, member2))

/** @} */

#endif /* !IPRT_INCLUDED_assertcompile_h */

