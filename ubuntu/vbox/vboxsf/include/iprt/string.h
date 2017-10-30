/** @file
 * IPRT - String Manipulation.
 */

/*
 * Copyright (C) 2006-2017 Oracle Corporation
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

#ifndef ___iprt_string_h
#define ___iprt_string_h

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/stdarg.h>
#include <iprt/err.h> /* for VINF_SUCCESS */
#if defined(RT_OS_LINUX) && defined(__KERNEL__)
  /* no C++ hacks ('new' etc) here anymore! */
# include <linux/string.h>

#elif defined(IN_XF86_MODULE) && !defined(NO_ANSIC)
  RT_C_DECLS_BEGIN
# include "xf86_ansic.h"
  RT_C_DECLS_END

#elif defined(RT_OS_FREEBSD) && defined(_KERNEL)
  RT_C_DECLS_BEGIN
# include <sys/libkern.h>
  RT_C_DECLS_END

#elif defined(RT_OS_NETBSD) && defined(_KERNEL)
  RT_C_DECLS_BEGIN
# include <lib/libkern/libkern.h>
  RT_C_DECLS_END

#elif defined(RT_OS_SOLARIS) && defined(_KERNEL)
  /*
   * Same case as with FreeBSD kernel:
   * The string.h stuff clashes with sys/system.h
   * ffs = find first set bit.
   */
# define ffs ffs_string_h
# include <string.h>
# undef ffs
# undef strpbrk

#else
# include <string.h>
#endif

/* For the time being: */
#include <iprt/utf16.h>
#include <iprt/latin1.h>

/*
 * Supply prototypes for standard string functions provided by
 * IPRT instead of the operating environment.
 */
#if defined(RT_OS_DARWIN) && defined(KERNEL)
RT_C_DECLS_BEGIN
void *memchr(const void *pv, int ch, size_t cb);
char *strpbrk(const char *pszStr, const char *pszChars);
RT_C_DECLS_END
#endif

#if defined(RT_OS_FREEBSD) && defined(_KERNEL)
RT_C_DECLS_BEGIN
char *strpbrk(const char *pszStr, const char *pszChars);
RT_C_DECLS_END
#endif

#if defined(RT_OS_NETBSD) && defined(_KERNEL)
RT_C_DECLS_BEGIN
char *strpbrk(const char *pszStr, const char *pszChars);
RT_C_DECLS_END
#endif

#if (!defined(RT_OS_LINUX) || !defined(_GNU_SOURCE)) && !defined(RT_OS_FREEBSD) && !defined(RT_OS_NETBSD)
RT_C_DECLS_BEGIN
void *memrchr(const char *pv, int ch, size_t cb);
RT_C_DECLS_END
#endif


/** @def RT_USE_RTC_3629
 * When defined the UTF-8 range will stop at  0x10ffff.  If not defined, the
 * range stops at 0x7fffffff.
 * @remarks Must be defined both when building and using the IPRT.  */
#ifdef DOXYGEN_RUNNING
# define RT_USE_RTC_3629
#endif


/**
 * Byte zero the specified object.
 *
 * This will use sizeof(Obj) to figure the size and will call memset, bzero
 * or some compiler intrinsic to perform the actual zeroing.
 *
 * @param   Obj     The object to zero. Make sure to dereference pointers.
 *
 * @remarks Because the macro may use memset it has been placed in string.h
 *          instead of cdefs.h to avoid build issues because someone forgot
 *          to include this header.
 *
 * @ingroup grp_rt_cdefs
 */
#define RT_ZERO(Obj)        RT_BZERO(&(Obj), sizeof(Obj))

/**
 * Byte zero the specified memory area.
 *
 * This will call memset, bzero or some compiler intrinsic to clear the
 * specified bytes of memory.
 *
 * @param   pv          Pointer to the memory.
 * @param   cb          The number of bytes to clear. Please, don't pass 0.
 *
 * @remarks Because the macro may use memset it has been placed in string.h
 *          instead of cdefs.h to avoid build issues because someone forgot
 *          to include this header.
 *
 * @ingroup grp_rt_cdefs
 */
#define RT_BZERO(pv, cb)    do { memset((pv), 0, cb); } while (0)



/** @defgroup grp_rt_str    RTStr - String Manipulation
 * Mostly UTF-8 related helpers where the standard string functions won't do.
 * @ingroup grp_rt
 * @{
 */

RT_C_DECLS_BEGIN


/**
 * The maximum string length.
 */
#define RTSTR_MAX       (~(size_t)0)


/** @def RTSTR_TAG
 * The default allocation tag used by the RTStr allocation APIs.
 *
 * When not defined before the inclusion of iprt/string.h, this will default to
 * the pointer to the current file name.  The string API will make of use of
 * this as pointer to a volatile but read-only string.
 */
#if !defined(RTSTR_TAG) || defined(DOXYGEN_RUNNING)
# define RTSTR_TAG      (__FILE__)
#endif


#ifdef IN_RING3

/**
 * Allocates tmp buffer with default tag, translates pszString from UTF8 to
 * current codepage.
 *
 * @returns iprt status code.
 * @param   ppszString      Receives pointer of allocated native CP string.
 *                          The returned pointer must be freed using RTStrFree().
 * @param   pszString       UTF-8 string to convert.
 */
#define RTStrUtf8ToCurrentCP(ppszString, pszString)     RTStrUtf8ToCurrentCPTag((ppszString), (pszString), RTSTR_TAG)

/**
 * Allocates tmp buffer with custom tag, translates pszString from UTF8 to
 * current codepage.
 *
 * @returns iprt status code.
 * @param   ppszString      Receives pointer of allocated native CP string.
 *                          The returned pointer must be freed using
 *                          RTStrFree()., const char *pszTag
 * @param   pszString       UTF-8 string to convert.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR3DECL(int)  RTStrUtf8ToCurrentCPTag(char **ppszString, const char *pszString, const char *pszTag);

/**
 * Allocates tmp buffer, translates pszString from current codepage to UTF-8.
 *
 * @returns iprt status code.
 * @param   ppszString      Receives pointer of allocated UTF-8 string.
 *                          The returned pointer must be freed using RTStrFree().
 * @param   pszString       Native string to convert.
 */
#define RTStrCurrentCPToUtf8(ppszString, pszString)     RTStrCurrentCPToUtf8Tag((ppszString), (pszString), RTSTR_TAG)

/**
 * Allocates tmp buffer, translates pszString from current codepage to UTF-8.
 *
 * @returns iprt status code.
 * @param   ppszString      Receives pointer of allocated UTF-8 string.
 *                          The returned pointer must be freed using RTStrFree().
 * @param   pszString       Native string to convert.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR3DECL(int)  RTStrCurrentCPToUtf8Tag(char **ppszString, const char *pszString, const char *pszTag);

#endif /* IN_RING3 */

/**
 * Free string allocated by any of the non-UCS-2 string functions.
 *
 * @returns iprt status code.
 * @param   pszString      Pointer to buffer with string to free.
 *                         NULL is accepted.
 */
RTDECL(void)  RTStrFree(char *pszString);

/**
 * Allocates a new copy of the given UTF-8 string (default tag).
 *
 * @returns Pointer to the allocated UTF-8 string.
 * @param   pszString       UTF-8 string to duplicate.
 */
#define RTStrDup(pszString)             RTStrDupTag((pszString), RTSTR_TAG)

/**
 * Allocates a new copy of the given UTF-8 string (custom tag).
 *
 * @returns Pointer to the allocated UTF-8 string.
 * @param   pszString       UTF-8 string to duplicate.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(char *) RTStrDupTag(const char *pszString, const char *pszTag);

/**
 * Allocates a new copy of the given UTF-8 string (default tag).
 *
 * @returns iprt status code.
 * @param   ppszString      Receives pointer of the allocated UTF-8 string.
 *                          The returned pointer must be freed using RTStrFree().
 * @param   pszString       UTF-8 string to duplicate.
 */
#define RTStrDupEx(ppszString, pszString)   RTStrDupExTag((ppszString), (pszString), RTSTR_TAG)

/**
 * Allocates a new copy of the given UTF-8 string (custom tag).
 *
 * @returns iprt status code.
 * @param   ppszString      Receives pointer of the allocated UTF-8 string.
 *                          The returned pointer must be freed using RTStrFree().
 * @param   pszString       UTF-8 string to duplicate.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTStrDupExTag(char **ppszString, const char *pszString, const char *pszTag);

/**
 * Allocates a new copy of the given UTF-8 substring (default tag).
 *
 * @returns Pointer to the allocated UTF-8 substring.
 * @param   pszString       UTF-8 string to duplicate.
 * @param   cchMax          The max number of chars to duplicate, not counting
 *                          the terminator.
 */
#define RTStrDupN(pszString, cchMax)        RTStrDupNTag((pszString), (cchMax), RTSTR_TAG)

/**
 * Allocates a new copy of the given UTF-8 substring (custom tag).
 *
 * @returns Pointer to the allocated UTF-8 substring.
 * @param   pszString       UTF-8 string to duplicate.
 * @param   cchMax          The max number of chars to duplicate, not counting
 *                          the terminator.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(char *) RTStrDupNTag(const char *pszString, size_t cchMax, const char *pszTag);

/**
 * Appends a string onto an existing IPRT allocated string (default tag).
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_STR_MEMORY if we failed to reallocate the string, @a *ppsz
 *          remains unchanged.
 *
 * @param   ppsz                Pointer to the string pointer.  The string
 *                              pointer must either be NULL or point to a string
 *                              returned by an IPRT string API.  (In/Out)
 * @param   pszAppend           The string to append.  NULL and empty strings
 *                              are quietly ignored.
 */
#define RTStrAAppend(ppsz, pszAppend)   RTStrAAppendTag((ppsz), (pszAppend), RTSTR_TAG)

/**
 * Appends a string onto an existing IPRT allocated string (custom tag).
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_STR_MEMORY if we failed to reallocate the string, @a *ppsz
 *          remains unchanged.
 *
 * @param   ppsz                Pointer to the string pointer.  The string
 *                              pointer must either be NULL or point to a string
 *                              returned by an IPRT string API.  (In/Out)
 * @param   pszAppend           The string to append.  NULL and empty strings
 *                              are quietly ignored.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(int) RTStrAAppendTag(char **ppsz, const char *pszAppend, const char *pszTag);

/**
 * Appends N bytes from a strings onto an existing IPRT allocated string
 * (default tag).
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_STR_MEMORY if we failed to reallocate the string, @a *ppsz
 *          remains unchanged.
 *
 * @param   ppsz                Pointer to the string pointer.  The string
 *                              pointer must either be NULL or point to a string
 *                              returned by an IPRT string API.  (In/Out)
 * @param   pszAppend           The string to append.  Can be NULL if cchAppend
 *                              is NULL.
 * @param   cchAppend           The number of chars (not code points) to append
 *                              from pszAppend.   Must not be more than
 *                              @a pszAppend contains, except for the special
 *                              value RTSTR_MAX that can be used to indicate all
 *                              of @a pszAppend without having to strlen it.
 */
#define RTStrAAppendN(ppsz, pszAppend, cchAppend)   RTStrAAppendNTag((ppsz), (pszAppend), (cchAppend), RTSTR_TAG)

/**
 * Appends N bytes from a strings onto an existing IPRT allocated string (custom
 * tag).
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_STR_MEMORY if we failed to reallocate the string, @a *ppsz
 *          remains unchanged.
 *
 * @param   ppsz                Pointer to the string pointer.  The string
 *                              pointer must either be NULL or point to a string
 *                              returned by an IPRT string API.  (In/Out)
 * @param   pszAppend           The string to append.  Can be NULL if cchAppend
 *                              is NULL.
 * @param   cchAppend           The number of chars (not code points) to append
 *                              from pszAppend.   Must not be more than
 *                              @a pszAppend contains, except for the special
 *                              value RTSTR_MAX that can be used to indicate all
 *                              of @a pszAppend without having to strlen it.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(int) RTStrAAppendNTag(char **ppsz, const char *pszAppend, size_t cchAppend, const char *pszTag);

/**
 * Appends one or more strings onto an existing IPRT allocated string.
 *
 * This is a very flexible and efficient alternative to using RTStrAPrintf to
 * combine several strings together.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_STR_MEMORY if we failed to reallocate the string, @a *ppsz
 *          remains unchanged.
 *
 * @param   ppsz                Pointer to the string pointer.  The string
 *                              pointer must either be NULL or point to a string
 *                              returned by an IPRT string API.  (In/Out)
 * @param   cPairs              The number of string / length pairs in the
 *                              @a va.
 * @param   va                  List of string (const char *) and length
 *                              (size_t) pairs.  The strings will be appended to
 *                              the string in the first argument.
 */
#define RTStrAAppendExNV(ppsz, cPairs, va)  RTStrAAppendExNVTag((ppsz), (cPairs), (va), RTSTR_TAG)

/**
 * Appends one or more strings onto an existing IPRT allocated string.
 *
 * This is a very flexible and efficient alternative to using RTStrAPrintf to
 * combine several strings together.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_STR_MEMORY if we failed to reallocate the string, @a *ppsz
 *          remains unchanged.
 *
 * @param   ppsz                Pointer to the string pointer.  The string
 *                              pointer must either be NULL or point to a string
 *                              returned by an IPRT string API.  (In/Out)
 * @param   cPairs              The number of string / length pairs in the
 *                              @a va.
 * @param   va                  List of string (const char *) and length
 *                              (size_t) pairs.  The strings will be appended to
 *                              the string in the first argument.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(int) RTStrAAppendExNVTag(char **ppsz, size_t cPairs, va_list va, const char *pszTag);

/**
 * Appends one or more strings onto an existing IPRT allocated string
 * (untagged).
 *
 * This is a very flexible and efficient alternative to using RTStrAPrintf to
 * combine several strings together.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_STR_MEMORY if we failed to reallocate the string, @a *ppsz
 *          remains unchanged.
 *
 * @param   ppsz                Pointer to the string pointer.  The string
 *                              pointer must either be NULL or point to a string
 *                              returned by an IPRT string API.  (In/Out)
 * @param   cPairs              The number of string / length pairs in the
 *                              ellipsis.
 * @param   ...                 List of string (const char *) and length
 *                              (size_t) pairs.  The strings will be appended to
 *                              the string in the first argument.
 */
DECLINLINE(int) RTStrAAppendExN(char **ppsz, size_t cPairs, ...)
{
    int     rc;
    va_list va;
    va_start(va, cPairs);
    rc = RTStrAAppendExNVTag(ppsz, cPairs, va, RTSTR_TAG);
    va_end(va);
    return rc;
}

/**
 * Appends one or more strings onto an existing IPRT allocated string (custom
 * tag).
 *
 * This is a very flexible and efficient alternative to using RTStrAPrintf to
 * combine several strings together.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_STR_MEMORY if we failed to reallocate the string, @a *ppsz
 *          remains unchanged.
 *
 * @param   ppsz                Pointer to the string pointer.  The string
 *                              pointer must either be NULL or point to a string
 *                              returned by an IPRT string API.  (In/Out)
 * @param   pszTag              Allocation tag used for statistics and such.
 * @param   cPairs              The number of string / length pairs in the
 *                              ellipsis.
 * @param   ...                 List of string (const char *) and length
 *                              (size_t) pairs.  The strings will be appended to
 *                              the string in the first argument.
 */
DECLINLINE(int) RTStrAAppendExNTag(char **ppsz, const char *pszTag, size_t cPairs, ...)
{
    int     rc;
    va_list va;
    va_start(va, cPairs);
    rc = RTStrAAppendExNVTag(ppsz, cPairs, va, pszTag);
    va_end(va);
    return rc;
}

/**
 * Truncates an IPRT allocated string (default tag).
 *
 * @retval  VINF_SUCCESS.
 * @retval  VERR_OUT_OF_RANGE if cchNew is too long.  Nothing is done.
 *
 * @param   ppsz                Pointer to the string pointer.  The string
 *                              pointer can be NULL if @a cchNew is 0, no change
 *                              is made then.  If we actually reallocate the
 *                              string, the string pointer might be changed by
 *                              this call.  (In/Out)
 * @param   cchNew              The new string length (excluding the
 *                              terminator).  The string must be at least this
 *                              long or we'll return VERR_OUT_OF_RANGE and
 *                              assert on you.
 */
#define RTStrATruncate(ppsz, cchNew)    RTStrATruncateTag((ppsz), (cchNew), RTSTR_TAG)

/**
 * Truncates an IPRT allocated string.
 *
 * @retval  VINF_SUCCESS.
 * @retval  VERR_OUT_OF_RANGE if cchNew is too long.  Nothing is done.
 *
 * @param   ppsz                Pointer to the string pointer.  The string
 *                              pointer can be NULL if @a cchNew is 0, no change
 *                              is made then.  If we actually reallocate the
 *                              string, the string pointer might be changed by
 *                              this call.  (In/Out)
 * @param   cchNew              The new string length (excluding the
 *                              terminator).  The string must be at least this
 *                              long or we'll return VERR_OUT_OF_RANGE and
 *                              assert on you.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(int) RTStrATruncateTag(char **ppsz, size_t cchNew, const char *pszTag);

/**
 * Allocates memory for string storage (default tag).
 *
 * You should normally not use this function, except if there is some very
 * custom string handling you need doing that isn't covered by any of the other
 * APIs.
 *
 * @returns Pointer to the allocated string.  The first byte is always set
 *          to the string terminator char, the contents of the remainder of the
 *          memory is undefined.  The string must be freed by calling RTStrFree.
 *
 *          NULL is returned if the allocation failed.  Please translate this to
 *          VERR_NO_STR_MEMORY and not VERR_NO_MEMORY.  Also consider
 *          RTStrAllocEx if an IPRT status code is required.
 *
 * @param   cb                  How many bytes to allocate.  If this is zero, we
 *                              will allocate a terminator byte anyway.
 */
#define RTStrAlloc(cb)                  RTStrAllocTag((cb), RTSTR_TAG)

/**
 * Allocates memory for string storage (custom tag).
 *
 * You should normally not use this function, except if there is some very
 * custom string handling you need doing that isn't covered by any of the other
 * APIs.
 *
 * @returns Pointer to the allocated string.  The first byte is always set
 *          to the string terminator char, the contents of the remainder of the
 *          memory is undefined.  The string must be freed by calling RTStrFree.
 *
 *          NULL is returned if the allocation failed.  Please translate this to
 *          VERR_NO_STR_MEMORY and not VERR_NO_MEMORY.  Also consider
 *          RTStrAllocEx if an IPRT status code is required.
 *
 * @param   cb                  How many bytes to allocate.  If this is zero, we
 *                              will allocate a terminator byte anyway.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(char *) RTStrAllocTag(size_t cb, const char *pszTag);

/**
 * Allocates memory for string storage, with status code (default tag).
 *
 * You should normally not use this function, except if there is some very
 * custom string handling you need doing that isn't covered by any of the other
 * APIs.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_STR_MEMORY
 *
 * @param   ppsz                Where to return the allocated string.  This will
 *                              be set to NULL on failure.  On success, the
 *                              returned memory will always start with a
 *                              terminator char so that it is considered a valid
 *                              C string, the contents of rest of the memory is
 *                              undefined.
 * @param   cb                  How many bytes to allocate.  If this is zero, we
 *                              will allocate a terminator byte anyway.
 */
#define RTStrAllocEx(ppsz, cb)      RTStrAllocExTag((ppsz), (cb), RTSTR_TAG)

/**
 * Allocates memory for string storage, with status code (custom tag).
 *
 * You should normally not use this function, except if there is some very
 * custom string handling you need doing that isn't covered by any of the other
 * APIs.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_STR_MEMORY
 *
 * @param   ppsz                Where to return the allocated string.  This will
 *                              be set to NULL on failure.  On success, the
 *                              returned memory will always start with a
 *                              terminator char so that it is considered a valid
 *                              C string, the contents of rest of the memory is
 *                              undefined.
 * @param   cb                  How many bytes to allocate.  If this is zero, we
 *                              will allocate a terminator byte anyway.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(int) RTStrAllocExTag(char **ppsz, size_t cb, const char *pszTag);

/**
 * Reallocates the specified string (default tag).
 *
 * You should normally not have use this function, except perhaps to truncate a
 * really long string you've got from some IPRT string API, but then you should
 * use RTStrATruncate.
 *
 * @returns VINF_SUCCESS.
 * @retval  VERR_NO_STR_MEMORY if we failed to reallocate the string, @a *ppsz
 *          remains unchanged.
 *
 * @param   ppsz                Pointer to the string variable containing the
 *                              input and output string.
 *
 *                              When not freeing the string, the result will
 *                              always have the last byte set to the terminator
 *                              character so that when used for string
 *                              truncation the result will be a valid C string
 *                              (your job to keep it a valid UTF-8 string).
 *
 *                              When the input string is NULL and we're supposed
 *                              to reallocate, the returned string will also
 *                              have the first byte set to the terminator char
 *                              so it will be a valid C string.
 *
 * @param   cbNew               When @a cbNew is zero, we'll behave like
 *                              RTStrFree and @a *ppsz will be set to NULL.
 *
 *                              When not zero, this will be the new size of the
 *                              memory backing the string, i.e. it includes the
 *                              terminator char.
 */
#define RTStrRealloc(ppsz, cbNew)       RTStrReallocTag((ppsz), (cbNew), RTSTR_TAG)

/**
 * Reallocates the specified string (custom tag).
 *
 * You should normally not have use this function, except perhaps to truncate a
 * really long string you've got from some IPRT string API, but then you should
 * use RTStrATruncate.
 *
 * @returns VINF_SUCCESS.
 * @retval  VERR_NO_STR_MEMORY if we failed to reallocate the string, @a *ppsz
 *          remains unchanged.
 *
 * @param   ppsz                Pointer to the string variable containing the
 *                              input and output string.
 *
 *                              When not freeing the string, the result will
 *                              always have the last byte set to the terminator
 *                              character so that when used for string
 *                              truncation the result will be a valid C string
 *                              (your job to keep it a valid UTF-8 string).
 *
 *                              When the input string is NULL and we're supposed
 *                              to reallocate, the returned string will also
 *                              have the first byte set to the terminator char
 *                              so it will be a valid C string.
 *
 * @param   cbNew               When @a cbNew is zero, we'll behave like
 *                              RTStrFree and @a *ppsz will be set to NULL.
 *
 *                              When not zero, this will be the new size of the
 *                              memory backing the string, i.e. it includes the
 *                              terminator char.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(int) RTStrReallocTag(char **ppsz, size_t cbNew, const char *pszTag);

/**
 * Validates the UTF-8 encoding of the string.
 *
 * @returns iprt status code.
 * @param   psz         The string.
 */
RTDECL(int) RTStrValidateEncoding(const char *psz);

/** @name Flags for RTStrValidateEncodingEx and RTUtf16ValidateEncodingEx
 * @{
 */
/** Check that the string is zero terminated within the given size.
 * VERR_BUFFER_OVERFLOW will be returned if the check fails. */
#define RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED     RT_BIT_32(0)
/** Check that the string is exactly the given length.
 * If it terminates early, VERR_BUFFER_UNDERFLOW will be returned.  When used
 * together with RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED, the given length must
 * include the terminator or VERR_BUFFER_OVERFLOW will be returned. */
#define RTSTR_VALIDATE_ENCODING_EXACT_LENGTH        RT_BIT_32(1)
/** @} */

/**
 * Validates the UTF-8 encoding of the string.
 *
 * @returns iprt status code.
 * @param   psz         The string.
 * @param   cch         The max string length (/ size).  Use RTSTR_MAX to
 *                      process the entire string.
 * @param   fFlags      Combination of RTSTR_VALIDATE_ENCODING_XXX flags.
 */
RTDECL(int) RTStrValidateEncodingEx(const char *psz, size_t cch, uint32_t fFlags);

/**
 * Checks if the UTF-8 encoding is valid.
 *
 * @returns true / false.
 * @param   psz         The string.
 */
RTDECL(bool) RTStrIsValidEncoding(const char *psz);

/**
 * Purge all bad UTF-8 encoding in the string, replacing it with '?'.
 *
 * @returns The number of bad characters (0 if nothing was done).
 * @param   psz         The string to purge.
 */
RTDECL(size_t) RTStrPurgeEncoding(char *psz);

/**
 * Sanitizes a (valid) UTF-8 string by replacing all characters outside a white
 * list in-place by an ASCII replacement character.
 *
 * Multi-byte characters will be replaced byte by byte.
 *
 * @returns The number of code points replaced.  In the case of an incorrectly
 *          encoded string -1 will be returned, and the string is not completely
 *          processed.  In the case of puszValidPairs having an odd number of
 *          code points, -1 will be also return but without any modification to
 *          the string.
 * @param   psz            The string to sanitise.
 * @param   puszValidPairs A zero-terminated array of pairs of Unicode points.
 *                         Each pair is the start and end point of a range,
 *                         and the union of these ranges forms the white list.
 * @param   chReplacement  The ASCII replacement character.
 */
RTDECL(ssize_t) RTStrPurgeComplementSet(char *psz, PCRTUNICP puszValidPairs, char chReplacement);

/**
 * Gets the number of code points the string is made up of, excluding
 * the terminator.
 *
 *
 * @returns Number of code points (RTUNICP).
 * @returns 0 if the string was incorrectly encoded.
 * @param   psz         The string.
 */
RTDECL(size_t) RTStrUniLen(const char *psz);

/**
 * Gets the number of code points the string is made up of, excluding
 * the terminator.
 *
 * This function will validate the string, and incorrectly encoded UTF-8
 * strings will be rejected.
 *
 * @returns iprt status code.
 * @param   psz         The string.
 * @param   cch         The max string length. Use RTSTR_MAX to process the entire string.
 * @param   pcuc        Where to store the code point count.
 *                      This is undefined on failure.
 */
RTDECL(int) RTStrUniLenEx(const char *psz, size_t cch, size_t *pcuc);

/**
 * Translate a UTF-8 string into an unicode string (i.e. RTUNICPs), allocating the string buffer.
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   ppUniString     Receives pointer to the allocated unicode string.
 *                          The returned string must be freed using RTUniFree().
 */
RTDECL(int) RTStrToUni(const char *pszString, PRTUNICP *ppUniString);

/**
 * Translates pszString from UTF-8 to an array of code points, allocating the result
 * array if requested.
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   cchString       The maximum size in chars (the type) to convert. The conversion stop
 *                          when it reaches cchString or the string terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppaCps          If cCps is non-zero, this must either be pointing to pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppusz is NULL or cCps is zero a buffer of at least cCps items
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTUtf16Free().
 * @param   cCps            The number of code points in the unicode string. This includes the terminator.
 * @param   pcCps           Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 */
RTDECL(int)  RTStrToUniEx(const char *pszString, size_t cchString, PRTUNICP *ppaCps, size_t cCps, size_t *pcCps);

/**
 * Calculates the length of the string in RTUTF16 items.
 *
 * This function will validate the string, and incorrectly encoded UTF-8
 * strings will be rejected. The primary purpose of this function is to
 * help allocate buffers for RTStrToUtf16Ex of the correct size. For most
 * other purposes RTStrCalcUtf16LenEx() should be used.
 *
 * @returns Number of RTUTF16 items.
 * @returns 0 if the string was incorrectly encoded.
 * @param   psz         The string.
 */
RTDECL(size_t) RTStrCalcUtf16Len(const char *psz);

/**
 * Calculates the length of the string in RTUTF16 items.
 *
 * This function will validate the string, and incorrectly encoded UTF-8
 * strings will be rejected.
 *
 * @returns iprt status code.
 * @param   psz         The string.
 * @param   cch         The max string length. Use RTSTR_MAX to process the entire string.
 * @param   pcwc        Where to store the string length. Optional.
 *                      This is undefined on failure.
 */
RTDECL(int) RTStrCalcUtf16LenEx(const char *psz, size_t cch, size_t *pcwc);

/**
 * Translate a UTF-8 string into a UTF-16 allocating the result buffer (default
 * tag).
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   ppwszString     Receives pointer to the allocated UTF-16 string.
 *                          The returned string must be freed using RTUtf16Free().
 */
#define RTStrToUtf16(pszString, ppwszString)    RTStrToUtf16Tag((pszString), (ppwszString), RTSTR_TAG)

/**
 * Translate a UTF-8 string into a UTF-16 allocating the result buffer (custom
 * tag).
 *
 * This differs from RTStrToUtf16 in that it always produces a
 * big-endian string.
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   ppwszString     Receives pointer to the allocated UTF-16 string.
 *                          The returned string must be freed using RTUtf16Free().
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int) RTStrToUtf16Tag(const char *pszString, PRTUTF16 *ppwszString, const char *pszTag);

/**
 * Translate a UTF-8 string into a UTF-16BE allocating the result buffer
 * (default tag).
 *
 * This differs from RTStrToUtf16Tag in that it always produces a
 * big-endian string.
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   ppwszString     Receives pointer to the allocated UTF-16BE string.
 *                          The returned string must be freed using RTUtf16Free().
 */
#define RTStrToUtf16Big(pszString, ppwszString)  RTStrToUtf16BigTag((pszString), (ppwszString), RTSTR_TAG)

/**
 * Translate a UTF-8 string into a UTF-16BE allocating the result buffer (custom
 * tag).
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   ppwszString     Receives pointer to the allocated UTF-16BE string.
 *                          The returned string must be freed using RTUtf16Free().
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int) RTStrToUtf16BigTag(const char *pszString, PRTUTF16 *ppwszString, const char *pszTag);

/**
 * Translates pszString from UTF-8 to UTF-16, allocating the result buffer if requested.
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   cchString       The maximum size in chars (the type) to convert. The conversion stop
 *                          when it reaches cchString or the string terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppwsz           If cwc is non-zero, this must either be pointing to pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppwsz is NULL or cwc is zero a buffer of at least cwc items
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTUtf16Free().
 * @param   cwc             The buffer size in RTUTF16s. This includes the terminator.
 * @param   pcwc            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 */
#define RTStrToUtf16Ex(pszString, cchString, ppwsz, cwc, pcwc) \
    RTStrToUtf16ExTag((pszString), (cchString), (ppwsz), (cwc), (pcwc), RTSTR_TAG)

/**
 * Translates pszString from UTF-8 to UTF-16, allocating the result buffer if
 * requested (custom tag).
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   cchString       The maximum size in chars (the type) to convert. The conversion stop
 *                          when it reaches cchString or the string terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppwsz           If cwc is non-zero, this must either be pointing to pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppwsz is NULL or cwc is zero a buffer of at least cwc items
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTUtf16Free().
 * @param   cwc             The buffer size in RTUTF16s. This includes the terminator.
 * @param   pcwc            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTStrToUtf16ExTag(const char *pszString, size_t cchString,
                               PRTUTF16 *ppwsz, size_t cwc, size_t *pcwc, const char *pszTag);


/**
 * Translates pszString from UTF-8 to UTF-16BE, allocating the result buffer if requested.
 *
 * This differs from RTStrToUtf16Ex in that it always produces a
 * big-endian string.
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   cchString       The maximum size in chars (the type) to convert. The conversion stop
 *                          when it reaches cchString or the string terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppwsz           If cwc is non-zero, this must either be pointing to pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppwsz is NULL or cwc is zero a buffer of at least cwc items
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTUtf16Free().
 * @param   cwc             The buffer size in RTUTF16s. This includes the terminator.
 * @param   pcwc            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 */
#define RTStrToUtf16BigEx(pszString, cchString, ppwsz, cwc, pcwc) \
    RTStrToUtf16BigExTag((pszString), (cchString), (ppwsz), (cwc), (pcwc), RTSTR_TAG)

/**
 * Translates pszString from UTF-8 to UTF-16BE, allocating the result buffer if
 * requested (custom tag).
 *
 * This differs from RTStrToUtf16ExTag in that it always produces a
 * big-endian string.
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   cchString       The maximum size in chars (the type) to convert. The conversion stop
 *                          when it reaches cchString or the string terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppwsz           If cwc is non-zero, this must either be pointing to pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppwsz is NULL or cwc is zero a buffer of at least cwc items
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTUtf16Free().
 * @param   cwc             The buffer size in RTUTF16s. This includes the terminator.
 * @param   pcwc            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTStrToUtf16BigExTag(const char *pszString, size_t cchString,
                                  PRTUTF16 *ppwsz, size_t cwc, size_t *pcwc, const char *pszTag);


/**
 * Calculates the length of the string in Latin-1 characters.
 *
 * This function will validate the string, and incorrectly encoded UTF-8
 * strings as well as string with codepoints outside the latin-1 range will be
 * rejected.  The primary purpose of this function is to help allocate buffers
 * for RTStrToLatin1Ex of the correct size.  For most other purposes
 * RTStrCalcLatin1LenEx() should be used.
 *
 * @returns Number of Latin-1 characters.
 * @returns 0 if the string was incorrectly encoded.
 * @param   psz         The string.
 */
RTDECL(size_t) RTStrCalcLatin1Len(const char *psz);

/**
 * Calculates the length of the string in Latin-1 characters.
 *
 * This function will validate the string, and incorrectly encoded UTF-8
 * strings as well as string with codepoints outside the latin-1 range will be
 * rejected.
 *
 * @returns iprt status code.
 * @param   psz         The string.
 * @param   cch         The max string length. Use RTSTR_MAX to process the
 *                      entire string.
 * @param   pcch        Where to store the string length. Optional.
 *                      This is undefined on failure.
 */
RTDECL(int) RTStrCalcLatin1LenEx(const char *psz, size_t cch, size_t *pcch);

/**
 * Translate a UTF-8 string into a Latin-1 allocating the result buffer (default
 * tag).
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   ppszString      Receives pointer to the allocated Latin-1 string.
 *                          The returned string must be freed using RTStrFree().
 */
#define RTStrToLatin1(pszString, ppszString)    RTStrToLatin1Tag((pszString), (ppszString), RTSTR_TAG)

/**
 * Translate a UTF-8 string into a Latin-1 allocating the result buffer (custom
 * tag).
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   ppszString      Receives pointer to the allocated Latin-1 string.
 *                          The returned string must be freed using RTStrFree().
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int) RTStrToLatin1Tag(const char *pszString, char **ppszString, const char *pszTag);

/**
 * Translates pszString from UTF-8 to Latin-1, allocating the result buffer if requested.
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   cchString       The maximum size in chars (the type) to convert.
 *                          The conversion stop when it reaches cchString or
 *                          the string terminator ('\\0'). Use RTSTR_MAX to
 *                          translate the entire string.
 * @param   ppsz            If cch is non-zero, this must either be pointing to
 *                          pointer to a buffer of the specified size, or
 *                          pointer to a NULL pointer.  If *ppsz is NULL or cch
 *                          is zero a buffer of at least cch items will be
 *                          allocated to hold the translated string. If a
 *                          buffer was requested it must be freed using
 *                          RTStrFree().
 * @param   cch             The buffer size in bytes. This includes the
 *                          terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 */
#define RTStrToLatin1Ex(pszString, cchString, ppsz, cch, pcch) \
    RTStrToLatin1ExTag((pszString), (cchString), (ppsz), (cch), (pcch), RTSTR_TAG)

/**
 * Translates pszString from UTF-8 to Latin1, allocating the result buffer if
 * requested (custom tag).
 *
 * @returns iprt status code.
 * @param   pszString       UTF-8 string to convert.
 * @param   cchString       The maximum size in chars (the type) to convert.
 *                          The conversion stop when it reaches cchString or
 *                          the string terminator ('\\0'). Use RTSTR_MAX to
 *                          translate the entire string.
 * @param   ppsz            If cch is non-zero, this must either be pointing to
 *                          pointer to a buffer of the specified size, or
 *                          pointer to a NULL pointer.  If *ppsz is NULL or cch
 *                          is zero a buffer of at least cch items will be
 *                          allocated to hold the translated string. If a
 *                          buffer was requested it must be freed using
 *                          RTStrFree().
 * @param   cch             The buffer size in bytes.  This includes the
 *                          terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTStrToLatin1ExTag(const char *pszString, size_t cchString, char **ppsz, size_t cch, size_t *pcch, const char *pszTag);

/**
 * Get the unicode code point at the given string position.
 *
 * @returns unicode code point.
 * @returns RTUNICP_INVALID if the encoding is invalid.
 * @param   psz         The string.
 */
RTDECL(RTUNICP) RTStrGetCpInternal(const char *psz);

/**
 * Get the unicode code point at the given string position.
 *
 * @returns iprt status code
 * @returns VERR_INVALID_UTF8_ENCODING if the encoding is invalid.
 * @param   ppsz        The string cursor.
 *                      This is advanced one character forward on failure.
 * @param   pCp         Where to store the unicode code point.
 *                      Stores RTUNICP_INVALID if the encoding is invalid.
 */
RTDECL(int) RTStrGetCpExInternal(const char **ppsz, PRTUNICP pCp);

/**
 * Get the unicode code point at the given string position for a string of a
 * given length.
 *
 * @returns iprt status code
 * @retval  VERR_INVALID_UTF8_ENCODING if the encoding is invalid.
 * @retval  VERR_END_OF_STRING if *pcch is 0. *pCp is set to RTUNICP_INVALID.
 *
 * @param   ppsz        The string.
 * @param   pcch        Pointer to the length of the string.  This will be
 *                      decremented by the size of the code point.
 * @param   pCp         Where to store the unicode code point.
 *                      Stores RTUNICP_INVALID if the encoding is invalid.
 */
RTDECL(int) RTStrGetCpNExInternal(const char **ppsz, size_t *pcch, PRTUNICP pCp);

/**
 * Put the unicode code point at the given string position
 * and return the pointer to the char following it.
 *
 * This function will not consider anything at or following the
 * buffer area pointed to by psz. It is therefore not suitable for
 * inserting code points into a string, only appending/overwriting.
 *
 * @returns pointer to the char following the written code point.
 * @param   psz         The string.
 * @param   CodePoint   The code point to write.
 *                      This should not be RTUNICP_INVALID or any other
 *                      character out of the UTF-8 range.
 *
 * @remark  This is a worker function for RTStrPutCp().
 *
 */
RTDECL(char *) RTStrPutCpInternal(char *psz, RTUNICP CodePoint);

/**
 * Get the unicode code point at the given string position.
 *
 * @returns unicode code point.
 * @returns RTUNICP_INVALID if the encoding is invalid.
 * @param   psz         The string.
 *
 * @remark  We optimize this operation by using an inline function for
 *          the most frequent and simplest sequence, the rest is
 *          handled by RTStrGetCpInternal().
 */
DECLINLINE(RTUNICP) RTStrGetCp(const char *psz)
{
    const unsigned char uch = *(const unsigned char *)psz;
    if (!(uch & RT_BIT(7)))
        return uch;
    return RTStrGetCpInternal(psz);
}

/**
 * Get the unicode code point at the given string position.
 *
 * @returns iprt status code.
 * @param   ppsz        Pointer to the string pointer. This will be updated to
 *                      point to the char following the current code point.
 *                      This is advanced one character forward on failure.
 * @param   pCp         Where to store the code point.
 *                      RTUNICP_INVALID is stored here on failure.
 *
 * @remark  We optimize this operation by using an inline function for
 *          the most frequent and simplest sequence, the rest is
 *          handled by RTStrGetCpExInternal().
 */
DECLINLINE(int) RTStrGetCpEx(const char **ppsz, PRTUNICP pCp)
{
    const unsigned char uch = **(const unsigned char **)ppsz;
    if (!(uch & RT_BIT(7)))
    {
        (*ppsz)++;
        *pCp = uch;
        return VINF_SUCCESS;
    }
    return RTStrGetCpExInternal(ppsz, pCp);
}

/**
 * Get the unicode code point at the given string position for a string of a
 * given maximum length.
 *
 * @returns iprt status code.
 * @retval  VERR_INVALID_UTF8_ENCODING if the encoding is invalid.
 * @retval  VERR_END_OF_STRING if *pcch is 0. *pCp is set to RTUNICP_INVALID.
 *
 * @param   ppsz        Pointer to the string pointer. This will be updated to
 *                      point to the char following the current code point.
 * @param   pcch        Pointer to the maximum string length.  This will be
 *                      decremented by the size of the code point found.
 * @param   pCp         Where to store the code point.
 *                      RTUNICP_INVALID is stored here on failure.
 *
 * @remark  We optimize this operation by using an inline function for
 *          the most frequent and simplest sequence, the rest is
 *          handled by RTStrGetCpNExInternal().
 */
DECLINLINE(int) RTStrGetCpNEx(const char **ppsz, size_t *pcch, PRTUNICP pCp)
{
    if (RT_LIKELY(*pcch != 0))
    {
        const unsigned char uch = **(const unsigned char **)ppsz;
        if (!(uch & RT_BIT(7)))
        {
            (*ppsz)++;
            (*pcch)--;
            *pCp = uch;
            return VINF_SUCCESS;
        }
    }
    return RTStrGetCpNExInternal(ppsz, pcch, pCp);
}

/**
 * Get the UTF-8 size in characters of a given Unicode code point.
 *
 * The code point is expected to be a valid Unicode one, but not necessarily in
 * the range supported by UTF-8.
 *
 * @returns The number of chars (bytes) required to encode the code point, or
 *          zero if there is no UTF-8 encoding.
 * @param   CodePoint       The unicode code point.
 */
DECLINLINE(size_t) RTStrCpSize(RTUNICP CodePoint)
{
    if (CodePoint < 0x00000080)
        return 1;
    if (CodePoint < 0x00000800)
        return 2;
    if (CodePoint < 0x00010000)
        return 3;
#ifdef RT_USE_RTC_3629
    if (CodePoint < 0x00011000)
        return 4;
#else
    if (CodePoint < 0x00200000)
        return 4;
    if (CodePoint < 0x04000000)
        return 5;
    if (CodePoint < 0x7fffffff)
        return 6;
#endif
    return 0;
}

/**
 * Put the unicode code point at the given string position
 * and return the pointer to the char following it.
 *
 * This function will not consider anything at or following the
 * buffer area pointed to by psz. It is therefore not suitable for
 * inserting code points into a string, only appending/overwriting.
 *
 * @returns pointer to the char following the written code point.
 * @param   psz         The string.
 * @param   CodePoint   The code point to write.
 *                      This should not be RTUNICP_INVALID or any other
 *                      character out of the UTF-8 range.
 *
 * @remark  We optimize this operation by using an inline function for
 *          the most frequent and simplest sequence, the rest is
 *          handled by RTStrPutCpInternal().
 */
DECLINLINE(char *) RTStrPutCp(char *psz, RTUNICP CodePoint)
{
    if (CodePoint < 0x80)
    {
        *psz++ = (unsigned char)CodePoint;
        return psz;
    }
    return RTStrPutCpInternal(psz, CodePoint);
}

/**
 * Skips ahead, past the current code point.
 *
 * @returns Pointer to the char after the current code point.
 * @param   psz     Pointer to the current code point.
 * @remark  This will not move the next valid code point, only past the current one.
 */
DECLINLINE(char *) RTStrNextCp(const char *psz)
{
    RTUNICP Cp;
    RTStrGetCpEx(&psz, &Cp);
    return (char *)psz;
}

/**
 * Skips back to the previous code point.
 *
 * @returns Pointer to the char before the current code point.
 * @returns pszStart on failure.
 * @param   pszStart    Pointer to the start of the string.
 * @param   psz         Pointer to the current code point.
 */
RTDECL(char *) RTStrPrevCp(const char *pszStart, const char *psz);


/** @page pg_rt_str_format  The IPRT Format Strings
 *
 * IPRT implements most of the commonly used format types and flags with the
 * exception of floating point which is completely missing.  In addition IPRT
 * provides a number of IPRT specific format types for the IPRT typedefs and
 * other useful things.  Note that several of these extensions are similar to
 * \%p and doesn't care much if you try add formating flags/width/precision.
 *
 *
 * Group 0a, The commonly used format types:
 *      - \%s   - Takes a pointer to a zero terminated string (UTF-8) and
 *                prints it with the optionally adjustment (width, -) and
 *                length restriction (precision).
 *      - \%ls  - Same as \%s except that the input is UTF-16 (output UTF-8).
 *      - \%Ls  - Same as \%s except that the input is UCS-32 (output UTF-8).
 *      - \%S   - Same as \%s, used to convert to current codeset but this is
 *                now done by the streams code.  Deprecated, use \%s.
 *      - \%lS  - Ditto. Deprecated, use \%ls.
 *      - \%LS  - Ditto. Deprecated, use \%Ls.
 *      - \%c   - Takes a char and prints it.
 *      - \%d   - Takes a signed integer and prints it as decimal. Thousand
 *                separator (\'), zero padding (0), adjustment (-+), width,
 *                precision
 *      - \%i   - Same as \%d.
 *      - \%u   - Takes an unsigned integer and prints it as decimal. Thousand
 *                separator (\'), zero padding (0), adjustment (-+), width,
 *                precision
 *      - \%x   - Takes an unsigned integer and prints it as lowercased
 *                hexadecimal.  The special hash (\#) flag causes a '0x'
 *                prefixed to be printed. Zero padding (0), adjustment (-+),
 *                width, precision.
 *      - \%X   - Same as \%x except that it is uppercased.
 *      - \%o   - Takes an unsigned (?) integer and prints it as octal. Zero
 *                padding (0), adjustment (-+), width, precision.
 *      - \%p   - Takes a pointer (void technically) and prints it. Zero
 *                padding (0), adjustment (-+), width, precision.
 *
 * The \%d, \%i, \%u, \%x, \%X and \%o format types support the following
 * argument type specifiers:
 *      - \%ll  - long long (uint64_t).
 *      - \%L   - long long (uint64_t).
 *      - \%l   - long (uint32_t, uint64_t)
 *      - \%h   - short (int16_t).
 *      - \%hh  - char (int8_t).
 *      - \%H   - char (int8_t).
 *      - \%z   - size_t.
 *      - \%j   - intmax_t (int64_t).
 *      - \%t   - ptrdiff_t.
 * The type in parentheses is typical sizes, however when printing those types
 * you are better off using the special group 2 format types below (\%RX32 and
 * such).
 *
 *
 * Group 0b, IPRT format tricks:
 *      - %M    - Replaces the format string, takes a string pointer.
 *      - %N    - Nested formatting, takes a pointer to a format string
 *                followed by the pointer to a va_list variable.  The va_list
 *                variable will not be modified and the caller must do va_end()
 *                on it.  Make sure the va_list variable is NOT in a parameter
 *                list or some gcc versions/targets may get it all wrong.
 *
 *
 * Group 1, the basic runtime typedefs (excluding those which obviously are
 * pointer):
 *      - \%RTbool          - Takes a bool value and prints 'true', 'false', or '!%d!'.
 *      - \%RTfile          - Takes a #RTFILE value.
 *      - \%RTfmode         - Takes a #RTFMODE value.
 *      - \%RTfoff          - Takes a #RTFOFF value.
 *      - \%RTfp16          - Takes a #RTFAR16 value.
 *      - \%RTfp32          - Takes a #RTFAR32 value.
 *      - \%RTfp64          - Takes a #RTFAR64 value.
 *      - \%RTgid           - Takes a #RTGID value.
 *      - \%RTino           - Takes a #RTINODE value.
 *      - \%RTint           - Takes a #RTINT value.
 *      - \%RTiop           - Takes a #RTIOPORT value.
 *      - \%RTldrm          - Takes a #RTLDRMOD value.
 *      - \%RTmac           - Takes a #PCRTMAC pointer.
 *      - \%RTnaddr         - Takes a #PCRTNETADDR value.
 *      - \%RTnaipv4        - Takes a #RTNETADDRIPV4 value.
 *      - \%RTnaipv6        - Takes a #PCRTNETADDRIPV6 value.
 *      - \%RTnthrd         - Takes a #RTNATIVETHREAD value.
 *      - \%RTnthrd         - Takes a #RTNATIVETHREAD value.
 *      - \%RTproc          - Takes a #RTPROCESS value.
 *      - \%RTptr           - Takes a #RTINTPTR or #RTUINTPTR value (but not void *).
 *      - \%RTreg           - Takes a #RTCCUINTREG value.
 *      - \%RTsel           - Takes a #RTSEL value.
 *      - \%RTsem           - Takes a #RTSEMEVENT, #RTSEMEVENTMULTI, #RTSEMMUTEX, #RTSEMFASTMUTEX, or #RTSEMRW value.
 *      - \%RTsock          - Takes a #RTSOCKET value.
 *      - \%RTthrd          - Takes a #RTTHREAD value.
 *      - \%RTuid           - Takes a #RTUID value.
 *      - \%RTuint          - Takes a #RTUINT value.
 *      - \%RTunicp         - Takes a #RTUNICP value.
 *      - \%RTutf16         - Takes a #RTUTF16 value.
 *      - \%RTuuid          - Takes a #PCRTUUID and will print the UUID as a string.
 *      - \%RTxuint         - Takes a #RTUINT or #RTINT value, formatting it as hex.
 *      - \%RGi             - Takes a #RTGCINT value.
 *      - \%RGp             - Takes a #RTGCPHYS value.
 *      - \%RGr             - Takes a #RTGCUINTREG value.
 *      - \%RGu             - Takes a #RTGCUINT value.
 *      - \%RGv             - Takes a #RTGCPTR, #RTGCINTPTR or #RTGCUINTPTR value.
 *      - \%RGx             - Takes a #RTGCUINT or #RTGCINT value, formatting it as hex.
 *      - \%RHi             - Takes a #RTHCINT value.
 *      - \%RHp             - Takes a #RTHCPHYS value.
 *      - \%RHr             - Takes a #RTHCUINTREG value.
 *      - \%RHu             - Takes a #RTHCUINT value.
 *      - \%RHv             - Takes a #RTHCPTR, #RTHCINTPTR or #RTHCUINTPTR value.
 *      - \%RHx             - Takes a #RTHCUINT or #RTHCINT value, formatting it as hex.
 *      - \%RRv             - Takes a #RTRCPTR, #RTRCINTPTR or #RTRCUINTPTR value.
 *      - \%RCi             - Takes a #RTINT value.
 *      - \%RCp             - Takes a #RTCCPHYS value.
 *      - \%RCr             - Takes a #RTCCUINTREG value.
 *      - \%RCu             - Takes a #RTUINT value.
 *      - \%RCv             - Takes a #uintptr_t, #intptr_t, void * value.
 *      - \%RCx             - Takes a #RTUINT or #RTINT value, formatting it as hex.
 *
 *
 * Group 2, the generic integer types which are prefered over relying on what
 * bit-count a 'long', 'short',  or 'long long' has on a platform. This are
 * highly prefered for the [u]intXX_t kind of types:
 *      - \%RI[8|16|32|64]  - Signed integer value of the specifed bit count.
 *      - \%RU[8|16|32|64]  - Unsigned integer value of the specifed bit count.
 *      - \%RX[8|16|32|64]  - Hexadecimal integer value of the specifed bit count.
 *
 *
 * Group 3, hex dumpers and other complex stuff which requires more than simple
 * formatting:
 *      - \%Rhxd            - Takes a pointer to the memory which is to be dumped in typical
 *                            hex format. Use the precision to specify the length, and the width to
 *                            set the number of bytes per line. Default width and precision is 16.
 *      - \%RhxD            - Same as \%Rhxd, except that it skips duplicate lines.
 *      - \%Rhxs            - Takes a pointer to the memory to be displayed as a hex string,
 *                            i.e. a series of space separated bytes formatted as two digit hex value.
 *                            Use the precision to specify the length. Default length is 16 bytes.
 *                            The width, if specified, is ignored.
 *      - \%Rrc             - Takes an integer iprt status code as argument. Will insert the
 *                            status code define corresponding to the iprt status code.
 *      - \%Rrs             - Takes an integer iprt status code as argument. Will insert the
 *                            short description of the specified status code.
 *      - \%Rrf             - Takes an integer iprt status code as argument. Will insert the
 *                            full description of the specified status code.
 *      - \%Rra             - Takes an integer iprt status code as argument. Will insert the
 *                            status code define + full description.
 *      - \%Rwc             - Takes a long Windows error code as argument. Will insert the status
 *                            code define corresponding to the Windows error code.
 *      - \%Rwf             - Takes a long Windows error code as argument. Will insert the
 *                            full description of the specified status code.
 *      - \%Rwa             - Takes a long Windows error code as argument. Will insert the
 *                            error code define + full description.
 *
 *      - \%Rhrc            - Takes a COM/XPCOM status code as argument. Will insert the status
 *                            code define corresponding to the Windows error code.
 *      - \%Rhrf            - Takes a COM/XPCOM status code as argument. Will insert the
 *                            full description of the specified status code.
 *      - \%Rhra            - Takes a COM/XPCOM error code as argument. Will insert the
 *                            error code define + full description.
 *
 *      - \%Rfn             - Pretty printing of a function or method. It drops the
 *                            return code and parameter list.
 *      - \%Rbn             - Prints the base name.  For dropping the path in
 *                            order to save space when printing a path name.
 *
 *      - \%lRbs            - Same as \%ls except inlut is big endian UTF-16.
 *
 * On other platforms, \%Rw? simply prints the argument in a form of 0xXXXXXXXX.
 *
 *
 * Group 4, structure dumpers:
 *      - \%RDtimespec      - Takes a PCRTTIMESPEC.
 *
 *
 * Group 5, XML / HTML escapers:
 *      - \%RMas            - Takes a string pointer (const char *) and outputs
 *                            it as an attribute value with the proper escaping.
 *                            This typically ends up in double quotes.
 *
 *      - \%RMes            - Takes a string pointer (const char *) and outputs
 *                            it as an element with the necessary escaping.
 *
 * Group 6, CPU Architecture Register dumpers:
 *      - \%RAx86[reg]      - Takes a 64-bit register value if the register is
 *                            64-bit or smaller.  Check the code wrt which
 *                            registers are implemented.
 *
 */

#ifndef DECLARED_FNRTSTROUTPUT          /* duplicated in iprt/log.h */
# define DECLARED_FNRTSTROUTPUT
/**
 * Output callback.
 *
 * @returns number of bytes written.
 * @param   pvArg       User argument.
 * @param   pachChars   Pointer to an array of utf-8 characters.
 * @param   cbChars     Number of bytes in the character array pointed to by pachChars.
 */
typedef DECLCALLBACK(size_t) FNRTSTROUTPUT(void *pvArg, const char *pachChars, size_t cbChars);
/** Pointer to callback function. */
typedef FNRTSTROUTPUT *PFNRTSTROUTPUT;
#endif

/** @name Format flag.
 * These are used by RTStrFormat extensions and RTStrFormatNumber, mind
 * that not all flags makes sense to both of the functions.
 * @{ */
#define RTSTR_F_CAPITAL         0x0001
#define RTSTR_F_LEFT            0x0002
#define RTSTR_F_ZEROPAD         0x0004
#define RTSTR_F_SPECIAL         0x0008
#define RTSTR_F_VALSIGNED       0x0010
#define RTSTR_F_PLUS            0x0020
#define RTSTR_F_BLANK           0x0040
#define RTSTR_F_WIDTH           0x0080
#define RTSTR_F_PRECISION       0x0100
#define RTSTR_F_THOUSAND_SEP    0x0200
#define RTSTR_F_OBFUSCATE_PTR   0x0400

#define RTSTR_F_BIT_MASK        0xf800
#define RTSTR_F_8BIT            0x0800
#define RTSTR_F_16BIT           0x1000
#define RTSTR_F_32BIT           0x2000
#define RTSTR_F_64BIT           0x4000
#define RTSTR_F_128BIT          0x8000
/** @} */

/** @def RTSTR_GET_BIT_FLAG
 * Gets the bit flag for the specified type.
 */
#define RTSTR_GET_BIT_FLAG(type) \
    ( sizeof(type) * 8 == 32  ? RTSTR_F_32BIT \
    : sizeof(type) * 8 == 64  ? RTSTR_F_64BIT \
    : sizeof(type) * 8 == 16  ? RTSTR_F_16BIT \
    : sizeof(type) * 8 == 8   ? RTSTR_F_8BIT \
    : sizeof(type) * 8 == 128 ? RTSTR_F_128BIT \
    : 0)


/**
 * Callback to format non-standard format specifiers.
 *
 * @returns The number of bytes formatted.
 * @param   pvArg           Formatter argument.
 * @param   pfnOutput       Pointer to output function.
 * @param   pvArgOutput     Argument for the output function.
 * @param   ppszFormat      Pointer to the format string pointer. Advance this till the char
 *                          after the format specifier.
 * @param   pArgs           Pointer to the argument list. Use this to fetch the arguments.
 * @param   cchWidth        Format Width. -1 if not specified.
 * @param   cchPrecision    Format Precision. -1 if not specified.
 * @param   fFlags          Flags (RTSTR_NTFS_*).
 * @param   chArgSize       The argument size specifier, 'l' or 'L'.
 */
typedef DECLCALLBACK(size_t) FNSTRFORMAT(void *pvArg, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                         const char **ppszFormat, va_list *pArgs, int cchWidth,
                                         int cchPrecision, unsigned fFlags, char chArgSize);
/** Pointer to a FNSTRFORMAT() function. */
typedef FNSTRFORMAT *PFNSTRFORMAT;


/**
 * Partial implementation of a printf like formatter.
 * It doesn't do everything correct, and there is no floating point support.
 * However, it supports custom formats by the means of a format callback.
 *
 * @returns number of bytes formatted.
 * @param   pfnOutput   Output worker.
 *                      Called in two ways. Normally with a string and its length.
 *                      For termination, it's called with NULL for string, 0 for length.
 * @param   pvArgOutput Argument to the output worker.
 * @param   pfnFormat   Custom format worker.
 * @param   pvArgFormat Argument to the format worker.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   InArgs      Argument list.
 */
RTDECL(size_t) RTStrFormatV(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, PFNSTRFORMAT pfnFormat, void *pvArgFormat,
                            const char *pszFormat, va_list InArgs) RT_IPRT_FORMAT_ATTR(5, 0);

/**
 * Partial implementation of a printf like formatter.
 *
 * It doesn't do everything correct, and there is no floating point support.
 * However, it supports custom formats by the means of a format callback.
 *
 * @returns number of bytes formatted.
 * @param   pfnOutput   Output worker.
 *                      Called in two ways. Normally with a string and its length.
 *                      For termination, it's called with NULL for string, 0 for length.
 * @param   pvArgOutput Argument to the output worker.
 * @param   pfnFormat   Custom format worker.
 * @param   pvArgFormat Argument to the format worker.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   ...         Argument list.
 */
RTDECL(size_t) RTStrFormat(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, PFNSTRFORMAT pfnFormat, void *pvArgFormat,
                           const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(5, 6);

/**
 * Formats an integer number according to the parameters.
 *
 * @returns Length of the formatted number.
 * @param   psz             Pointer to output string buffer of sufficient size.
 * @param   u64Value        Value to format.
 * @param   uiBase          Number representation base.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags, RTSTR_F_XXX.
 */
RTDECL(int) RTStrFormatNumber(char *psz, uint64_t u64Value, unsigned int uiBase, signed int cchWidth, signed int cchPrecision,
                              unsigned int fFlags);

/**
 * Formats an unsigned 8-bit number.
 *
 * @returns The length of the formatted number or VERR_BUFFER_OVERFLOW.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   u8Value         The value to format.
 * @param   uiBase          Number representation base.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags, RTSTR_F_XXX.
 */
RTDECL(ssize_t) RTStrFormatU8(char *pszBuf, size_t cbBuf, uint8_t u8Value, unsigned int uiBase,
                              signed int cchWidth, signed int cchPrecision, uint32_t fFlags);

/**
 * Formats an unsigned 16-bit number.
 *
 * @returns The length of the formatted number or VERR_BUFFER_OVERFLOW.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   u16Value        The value to format.
 * @param   uiBase          Number representation base.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags, RTSTR_F_XXX.
 */
RTDECL(ssize_t) RTStrFormatU16(char *pszBuf, size_t cbBuf, uint16_t u16Value, unsigned int uiBase,
                               signed int cchWidth, signed int cchPrecision, uint32_t fFlags);

/**
 * Formats an unsigned 32-bit number.
 *
 * @returns The length of the formatted number or VERR_BUFFER_OVERFLOW.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   u32Value        The value to format.
 * @param   uiBase          Number representation base.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags, RTSTR_F_XXX.
 */
RTDECL(ssize_t) RTStrFormatU32(char *pszBuf, size_t cbBuf, uint32_t u32Value, unsigned int uiBase,
                               signed int cchWidth, signed int cchPrecision, uint32_t fFlags);

/**
 * Formats an unsigned 64-bit number.
 *
 * @returns The length of the formatted number or VERR_BUFFER_OVERFLOW.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   u64Value        The value to format.
 * @param   uiBase          Number representation base.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags, RTSTR_F_XXX.
 */
RTDECL(ssize_t) RTStrFormatU64(char *pszBuf, size_t cbBuf, uint64_t u64Value, unsigned int uiBase,
                               signed int cchWidth, signed int cchPrecision, uint32_t fFlags);

/**
 * Formats an unsigned 128-bit number.
 *
 * @returns The length of the formatted number or VERR_BUFFER_OVERFLOW.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   pu128Value      The value to format.
 * @param   uiBase          Number representation base.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags, RTSTR_F_XXX.
 * @remarks The current implementation is limited to base 16 and doesn't do
 *          width or precision and probably ignores few flags too.
 */
RTDECL(ssize_t) RTStrFormatU128(char *pszBuf, size_t cbBuf, PCRTUINT128U pu128Value, unsigned int uiBase,
                                signed int cchWidth, signed int cchPrecision, uint32_t fFlags);

/**
 * Formats an unsigned 256-bit number.
 *
 * @returns The length of the formatted number or VERR_BUFFER_OVERFLOW.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   pu256Value      The value to format.
 * @param   uiBase          Number representation base.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags, RTSTR_F_XXX.
 * @remarks The current implementation is limited to base 16 and doesn't do
 *          width or precision and probably ignores few flags too.
 */
RTDECL(ssize_t) RTStrFormatU256(char *pszBuf, size_t cbBuf, PCRTUINT256U pu256Value, unsigned int uiBase,
                                signed int cchWidth, signed int cchPrecision, uint32_t fFlags);

/**
 * Formats an unsigned 512-bit number.
 *
 * @returns The length of the formatted number or VERR_BUFFER_OVERFLOW.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   pu512Value      The value to format.
 * @param   uiBase          Number representation base.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags, RTSTR_F_XXX.
 * @remarks The current implementation is limited to base 16 and doesn't do
 *          width or precision and probably ignores few flags too.
 */
RTDECL(ssize_t) RTStrFormatU512(char *pszBuf, size_t cbBuf, PCRTUINT512U pu512Value, unsigned int uiBase,
                                signed int cchWidth, signed int cchPrecision, uint32_t fFlags);


/**
 * Formats an 80-bit extended floating point number.
 *
 * @returns The length of the formatted number or VERR_BUFFER_OVERFLOW.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   pr80Value       The value to format.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags, RTSTR_F_XXX.
 */
RTDECL(ssize_t) RTStrFormatR80(char *pszBuf, size_t cbBuf, PCRTFLOAT80U pr80Value, signed int cchWidth,
                               signed int cchPrecision, uint32_t fFlags);

/**
 * Formats an 80-bit extended floating point number, version 2.
 *
 * @returns The length of the formatted number or VERR_BUFFER_OVERFLOW.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   pr80Value       The value to format.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags, RTSTR_F_XXX.
 */
RTDECL(ssize_t) RTStrFormatR80u2(char *pszBuf, size_t cbBuf, PCRTFLOAT80U2 pr80Value, signed int cchWidth,
                                 signed int cchPrecision, uint32_t fFlags);



/**
 * Callback for formatting a type.
 *
 * This is registered using the RTStrFormatTypeRegister function and will
 * be called during string formatting to handle the specified %R[type].
 * The argument for this format type is assumed to be a pointer and it's
 * passed in the @a pvValue argument.
 *
 * @returns Length of the formatted output.
 * @param   pfnOutput       Output worker.
 * @param   pvArgOutput     Argument to the output worker.
 * @param   pszType         The type name.
 * @param   pvValue         The argument value.
 * @param   cchWidth        Width.
 * @param   cchPrecision    Precision.
 * @param   fFlags          Flags (NTFS_*).
 * @param   pvUser          The user argument.
 */
typedef DECLCALLBACK(size_t) FNRTSTRFORMATTYPE(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                               const char *pszType, void const *pvValue,
                                               int cchWidth, int cchPrecision, unsigned fFlags,
                                               void *pvUser);
/** Pointer to a FNRTSTRFORMATTYPE. */
typedef FNRTSTRFORMATTYPE *PFNRTSTRFORMATTYPE;


/**
 * Register a format handler for a type.
 *
 * The format handler is used to handle '%R[type]' format types, where the argument
 * in the vector is a pointer value (a bit restrictive, but keeps it simple).
 *
 * The caller must ensure that no other thread will be making use of any of
 * the dynamic formatting type facilities simultaneously with this call.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_ALREADY_EXISTS if the type has already been registered.
 * @retval  VERR_TOO_MANY_OPEN_FILES if all the type slots has been allocated already.
 *
 * @param   pszType         The type name.
 * @param   pfnHandler      The handler address. See FNRTSTRFORMATTYPE for details.
 * @param   pvUser          The user argument to pass to the handler. See RTStrFormatTypeSetUser
 *                          for how to update this later.
 */
RTDECL(int) RTStrFormatTypeRegister(const char *pszType, PFNRTSTRFORMATTYPE pfnHandler, void *pvUser);

/**
 * Deregisters a format type.
 *
 * The caller must ensure that no other thread will be making use of any of
 * the dynamic formatting type facilities simultaneously with this call.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_FILE_NOT_FOUND if not found.
 *
 * @param   pszType     The type to deregister.
 */
RTDECL(int) RTStrFormatTypeDeregister(const char *pszType);

/**
 * Sets the user argument for a type.
 *
 * This can be used if a user argument needs relocating in GC.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_FILE_NOT_FOUND if not found.
 *
 * @param   pszType     The type to update.
 * @param   pvUser      The new user argument value.
 */
RTDECL(int) RTStrFormatTypeSetUser(const char *pszType, void *pvUser);


/**
 * String printf.
 *
 * @returns The length of the returned string (in pszBuffer) excluding the
 *          terminator.
 * @param   pszBuffer   Output buffer.
 * @param   cchBuffer   Size of the output buffer.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   args        The format argument.
 *
 * @deprecated Use RTStrPrintf2V! Problematic return value on overflow.
 */
RTDECL(size_t) RTStrPrintfV(char *pszBuffer, size_t cchBuffer, const char *pszFormat, va_list args) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * String printf.
 *
 * @returns The length of the returned string (in pszBuffer) excluding the
 *          terminator.
 * @param   pszBuffer   Output buffer.
 * @param   cchBuffer   Size of the output buffer.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   ...         The format argument.
 *
 * @deprecated Use RTStrPrintf2! Problematic return value on overflow.
 */
RTDECL(size_t) RTStrPrintf(char *pszBuffer, size_t cchBuffer, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * String printf with custom formatting.
 *
 * @returns The length of the returned string (in pszBuffer) excluding the
 *          terminator.
 * @param   pfnFormat   Pointer to handler function for the custom formats.
 * @param   pvArg       Argument to the pfnFormat function.
 * @param   pszBuffer   Output buffer.
 * @param   cchBuffer   Size of the output buffer.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   args        The format argument.
 *
 * @deprecated Use RTStrPrintf2ExV! Problematic return value on overflow.
 */
RTDECL(size_t) RTStrPrintfExV(PFNSTRFORMAT pfnFormat, void *pvArg, char *pszBuffer, size_t cchBuffer,
                              const char *pszFormat, va_list args)  RT_IPRT_FORMAT_ATTR(5, 0);

/**
 * String printf with custom formatting.
 *
 * @returns The length of the returned string (in pszBuffer) excluding the
 *          terminator.
 * @param   pfnFormat   Pointer to handler function for the custom formats.
 * @param   pvArg       Argument to the pfnFormat function.
 * @param   pszBuffer   Output buffer.
 * @param   cchBuffer   Size of the output buffer.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   ...         The format argument.
 *
 * @deprecated Use RTStrPrintf2Ex! Problematic return value on overflow.
 */
RTDECL(size_t) RTStrPrintfEx(PFNSTRFORMAT pfnFormat, void *pvArg, char *pszBuffer, size_t cchBuffer,
                             const char *pszFormat, ...)  RT_IPRT_FORMAT_ATTR(5, 6);

/**
 * String printf, version 2.
 *
 * @returns On success, positive count of formatted character excluding the
 *          terminator.  On buffer overflow, negative number giving the required
 *          buffer size (including terminator char).
 *
 * @param   pszBuffer   Output buffer.
 * @param   cbBuffer    Size of the output buffer.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   args        The format argument.
 */
RTDECL(ssize_t) RTStrPrintf2V(char *pszBuffer, size_t cbBuffer, const char *pszFormat, va_list args) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * String printf, version 2.
 *
 * @returns On success, positive count of formatted character excluding the
 *          terminator.  On buffer overflow, negative number giving the required
 *          buffer size (including terminator char).
 *
 * @param   pszBuffer   Output buffer.
 * @param   cbBuffer    Size of the output buffer.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   ...         The format argument.
 */
RTDECL(ssize_t) RTStrPrintf2(char *pszBuffer, size_t cbBuffer, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * String printf with custom formatting, version 2.
 *
 * @returns On success, positive count of formatted character excluding the
 *          terminator.  On buffer overflow, negative number giving the required
 *          buffer size (including terminator char).
 *
 * @param   pfnFormat   Pointer to handler function for the custom formats.
 * @param   pvArg       Argument to the pfnFormat function.
 * @param   pszBuffer   Output buffer.
 * @param   cbBuffer    Size of the output buffer.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   args        The format argument.
 */
RTDECL(ssize_t) RTStrPrintf2ExV(PFNSTRFORMAT pfnFormat, void *pvArg, char *pszBuffer, size_t cbBuffer,
                                const char *pszFormat, va_list args)  RT_IPRT_FORMAT_ATTR(5, 0);

/**
 * String printf with custom formatting, version 2.
 *
 * @returns On success, positive count of formatted character excluding the
 *          terminator.  On buffer overflow, negative number giving the required
 *          buffer size (including terminator char).
 *
 * @param   pfnFormat   Pointer to handler function for the custom formats.
 * @param   pvArg       Argument to the pfnFormat function.
 * @param   pszBuffer   Output buffer.
 * @param   cbBuffer   Size of the output buffer.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   ...         The format argument.
 */
RTDECL(ssize_t) RTStrPrintf2Ex(PFNSTRFORMAT pfnFormat, void *pvArg, char *pszBuffer, size_t cbBuffer,
                               const char *pszFormat, ...)  RT_IPRT_FORMAT_ATTR(5, 6);

/**
 * Allocating string printf (default tag).
 *
 * @returns The length of the string in the returned *ppszBuffer excluding the
 *          terminator.
 * @returns -1 on failure.
 * @param   ppszBuffer  Where to store the pointer to the allocated output buffer.
 *                      The buffer should be freed using RTStrFree().
 *                      On failure *ppszBuffer will be set to NULL.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   args        The format argument.
 */
#define RTStrAPrintfV(ppszBuffer, pszFormat, args)      RTStrAPrintfVTag((ppszBuffer), (pszFormat), (args), RTSTR_TAG)

/**
 * Allocating string printf (custom tag).
 *
 * @returns The length of the string in the returned *ppszBuffer excluding the
 *          terminator.
 * @returns -1 on failure.
 * @param   ppszBuffer  Where to store the pointer to the allocated output buffer.
 *                      The buffer should be freed using RTStrFree().
 *                      On failure *ppszBuffer will be set to NULL.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   args        The format argument.
 * @param   pszTag      Allocation tag used for statistics and such.
 */
RTDECL(int) RTStrAPrintfVTag(char **ppszBuffer, const char *pszFormat, va_list args, const char *pszTag) RT_IPRT_FORMAT_ATTR(2, 0);

/**
 * Allocating string printf.
 *
 * @returns The length of the string in the returned *ppszBuffer excluding the
 *          terminator.
 * @returns -1 on failure.
 * @param   ppszBuffer  Where to store the pointer to the allocated output buffer.
 *                      The buffer should be freed using RTStrFree().
 *                      On failure *ppszBuffer will be set to NULL.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   ...         The format argument.
 */
DECLINLINE(int) RT_IPRT_FORMAT_ATTR(2, 3) RTStrAPrintf(char **ppszBuffer, const char *pszFormat, ...)
{
    int     cbRet;
    va_list va;
    va_start(va, pszFormat);
    cbRet = RTStrAPrintfVTag(ppszBuffer, pszFormat, va, RTSTR_TAG);
    va_end(va);
    return cbRet;
}

/**
 * Allocating string printf (custom tag).
 *
 * @returns The length of the string in the returned *ppszBuffer excluding the
 *          terminator.
 * @returns -1 on failure.
 * @param   ppszBuffer  Where to store the pointer to the allocated output buffer.
 *                      The buffer should be freed using RTStrFree().
 *                      On failure *ppszBuffer will be set to NULL.
 * @param   pszTag      Allocation tag used for statistics and such.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   ...         The format argument.
 */
DECLINLINE(int) RT_IPRT_FORMAT_ATTR(3, 4) RTStrAPrintfTag(char **ppszBuffer, const char *pszTag, const char *pszFormat, ...)
{
    int     cbRet;
    va_list va;
    va_start(va, pszFormat);
    cbRet = RTStrAPrintfVTag(ppszBuffer, pszFormat, va, pszTag);
    va_end(va);
    return cbRet;
}

/**
 * Allocating string printf, version 2.
 *
 * @returns Formatted string. Use RTStrFree() to free it. NULL when out of
 *          memory.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   args        The format argument.
 */
#define RTStrAPrintf2V(pszFormat, args)     RTStrAPrintf2VTag((pszFormat), (args), RTSTR_TAG)

/**
 * Allocating string printf, version 2.
 *
 * @returns Formatted string. Use RTStrFree() to free it. NULL when out of
 *          memory.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   args        The format argument.
 * @param   pszTag      Allocation tag used for statistics and such.
 */
RTDECL(char *) RTStrAPrintf2VTag(const char *pszFormat, va_list args, const char *pszTag) RT_IPRT_FORMAT_ATTR(1, 0);

/**
 * Allocating string printf, version 2 (default tag).
 *
 * @returns Formatted string. Use RTStrFree() to free it. NULL when out of
 *          memory.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   ...         The format argument.
 */
DECLINLINE(char *) RT_IPRT_FORMAT_ATTR(1, 2) RTStrAPrintf2(const char *pszFormat, ...)
{
    char   *pszRet;
    va_list va;
    va_start(va, pszFormat);
    pszRet = RTStrAPrintf2VTag(pszFormat, va, RTSTR_TAG);
    va_end(va);
    return pszRet;
}

/**
 * Allocating string printf, version 2 (custom tag).
 *
 * @returns Formatted string. Use RTStrFree() to free it. NULL when out of
 *          memory.
 * @param   pszTag      Allocation tag used for statistics and such.
 * @param   pszFormat   Pointer to the format string, @see pg_rt_str_format.
 * @param   ...         The format argument.
 */
DECLINLINE(char *) RT_IPRT_FORMAT_ATTR(2, 3) RTStrAPrintf2Tag(const char *pszTag, const char *pszFormat, ...)
{
    char   *pszRet;
    va_list va;
    va_start(va, pszFormat);
    pszRet = RTStrAPrintf2VTag(pszFormat, va, pszTag);
    va_end(va);
    return pszRet;
}

/**
 * Strips blankspaces from both ends of the string.
 *
 * @returns Pointer to first non-blank char in the string.
 * @param   psz     The string to strip.
 */
RTDECL(char *) RTStrStrip(char *psz);

/**
 * Strips blankspaces from the start of the string.
 *
 * @returns Pointer to first non-blank char in the string.
 * @param   psz     The string to strip.
 */
RTDECL(char *) RTStrStripL(const char *psz);

/**
 * Strips blankspaces from the end of the string.
 *
 * @returns psz.
 * @param   psz     The string to strip.
 */
RTDECL(char *) RTStrStripR(char *psz);

/**
 * String copy with overflow handling.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   pszDst              The destination buffer.
 * @param   cbDst               The size of the destination buffer (in bytes).
 * @param   pszSrc              The source string.  NULL is not OK.
 */
RTDECL(int) RTStrCopy(char *pszDst, size_t cbDst, const char *pszSrc);

/**
 * String copy with overflow handling.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   pszDst              The destination buffer.
 * @param   cbDst               The size of the destination buffer (in bytes).
 * @param   pszSrc              The source string.  NULL is not OK.
 * @param   cchSrcMax           The maximum number of chars (not code points) to
 *                              copy from the source string, not counting the
 *                              terminator as usual.
 */
RTDECL(int) RTStrCopyEx(char *pszDst, size_t cbDst, const char *pszSrc, size_t cchSrcMax);

/**
 * String copy with overflow handling and buffer advancing.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   ppszDst             Pointer to the destination buffer pointer.
 *                              This will be advanced to the end of the copied
 *                              bytes (points at the terminator).  This is also
 *                              updated on overflow.
 * @param   pcbDst              Pointer to the destination buffer size
 *                              variable.  This will be updated in accord with
 *                              the buffer pointer.
 * @param   pszSrc              The source string.  NULL is not OK.
 */
RTDECL(int) RTStrCopyP(char **ppszDst, size_t *pcbDst, const char *pszSrc);

/**
 * String copy with overflow handling.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   ppszDst             Pointer to the destination buffer pointer.
 *                              This will be advanced to the end of the copied
 *                              bytes (points at the terminator).  This is also
 *                              updated on overflow.
 * @param   pcbDst              Pointer to the destination buffer size
 *                              variable.  This will be updated in accord with
 *                              the buffer pointer.
 * @param   pszSrc              The source string.  NULL is not OK.
 * @param   cchSrcMax           The maximum number of chars (not code points) to
 *                              copy from the source string, not counting the
 *                              terminator as usual.
 */
RTDECL(int) RTStrCopyPEx(char **ppszDst, size_t *pcbDst, const char *pszSrc, size_t cchSrcMax);

/**
 * String concatenation with overflow handling.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   pszDst              The destination buffer.
 * @param   cbDst               The size of the destination buffer (in bytes).
 * @param   pszSrc              The source string.  NULL is not OK.
 */
RTDECL(int) RTStrCat(char *pszDst, size_t cbDst, const char *pszSrc);

/**
 * String concatenation with overflow handling.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   pszDst              The destination buffer.
 * @param   cbDst               The size of the destination buffer (in bytes).
 * @param   pszSrc              The source string.  NULL is not OK.
 * @param   cchSrcMax           The maximum number of chars (not code points) to
 *                              copy from the source string, not counting the
 *                              terminator as usual.
 */
RTDECL(int) RTStrCatEx(char *pszDst, size_t cbDst, const char *pszSrc, size_t cchSrcMax);

/**
 * String concatenation with overflow handling.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   ppszDst             Pointer to the destination buffer pointer.
 *                              This will be advanced to the end of the copied
 *                              bytes (points at the terminator).  This is also
 *                              updated on overflow.
 * @param   pcbDst              Pointer to the destination buffer size
 *                              variable.  This will be updated in accord with
 *                              the buffer pointer.
 * @param   pszSrc              The source string.  NULL is not OK.
 */
RTDECL(int) RTStrCatP(char **ppszDst, size_t *pcbDst, const char *pszSrc);

/**
 * String concatenation with overflow handling and buffer advancing.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   ppszDst             Pointer to the destination buffer pointer.
 *                              This will be advanced to the end of the copied
 *                              bytes (points at the terminator).  This is also
 *                              updated on overflow.
 * @param   pcbDst              Pointer to the destination buffer size
 *                              variable.  This will be updated in accord with
 *                              the buffer pointer.
 * @param   pszSrc              The source string.  NULL is not OK.
 * @param   cchSrcMax           The maximum number of chars (not code points) to
 *                              copy from the source string, not counting the
 *                              terminator as usual.
 */
RTDECL(int) RTStrCatPEx(char **ppszDst, size_t *pcbDst, const char *pszSrc, size_t cchSrcMax);

/**
 * Performs a case sensitive string compare between two UTF-8 strings.
 *
 * Encoding errors are ignored by the current implementation. So, the only
 * difference between this and the CRT strcmp function is the handling of
 * NULL arguments.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   psz1        First UTF-8 string. Null is allowed.
 * @param   psz2        Second UTF-8 string. Null is allowed.
 */
RTDECL(int) RTStrCmp(const char *psz1, const char *psz2);

/**
 * Performs a case sensitive string compare between two UTF-8 strings, given
 * a maximum string length.
 *
 * Encoding errors are ignored by the current implementation. So, the only
 * difference between this and the CRT strncmp function is the handling of
 * NULL arguments.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   psz1        First UTF-8 string. Null is allowed.
 * @param   psz2        Second UTF-8 string. Null is allowed.
 * @param   cchMax      The maximum string length
 */
RTDECL(int) RTStrNCmp(const char *psz1, const char *psz2, size_t cchMax);

/**
 * Performs a case insensitive string compare between two UTF-8 strings.
 *
 * This is a simplified compare, as only the simplified lower/upper case folding
 * specified by the unicode specs are used. It does not consider character pairs
 * as they are used in some languages, just simple upper & lower case compares.
 *
 * The result is the difference between the mismatching codepoints after they
 * both have been lower cased.
 *
 * If the string encoding is invalid the function will assert (strict builds)
 * and use RTStrCmp for the remainder of the string.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   psz1        First UTF-8 string. Null is allowed.
 * @param   psz2        Second UTF-8 string. Null is allowed.
 */
RTDECL(int) RTStrICmp(const char *psz1, const char *psz2);

/**
 * Performs a case insensitive string compare between two UTF-8 strings, given a
 * maximum string length.
 *
 * This is a simplified compare, as only the simplified lower/upper case folding
 * specified by the unicode specs are used. It does not consider character pairs
 * as they are used in some languages, just simple upper & lower case compares.
 *
 * The result is the difference between the mismatching codepoints after they
 * both have been lower cased.
 *
 * If the string encoding is invalid the function will assert (strict builds)
 * and use RTStrCmp for the remainder of the string.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   psz1        First UTF-8 string. Null is allowed.
 * @param   psz2        Second UTF-8 string. Null is allowed.
 * @param   cchMax      Maximum string length
 */
RTDECL(int) RTStrNICmp(const char *psz1, const char *psz2, size_t cchMax);

/**
 * Performs a case insensitive string compare between a UTF-8 string and a 7-bit
 * ASCII string.
 *
 * This is potentially faster than RTStrICmp and drags in less dependencies.  It
 * is really handy for hardcoded inputs.
 *
 * If the string encoding is invalid the function will assert (strict builds)
 * and use RTStrCmp for the remainder of the string.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   psz1        First UTF-8 string. Null is allowed.
 * @param   psz2        Second string, 7-bit ASCII. Null is allowed.
 * @sa      RTUtf16ICmpAscii
 */
RTDECL(int) RTStrICmpAscii(const char *psz1, const char *psz2);

/**
 * Checks whether @a pszString starts with @a pszStart.
 *
 * @returns true / false.
 * @param   pszString   The string to check.
 * @param   pszStart    The start string to check for.
 */
RTDECL(int) RTStrStartsWith(const char *pszString, const char *pszStart);

/**
 * Checks whether @a pszString starts with @a pszStart, case insensitive.
 *
 * @returns true / false.
 * @param   pszString   The string to check.
 * @param   pszStart    The start string to check for.
 */
RTDECL(int) RTStrIStartsWith(const char *pszString, const char *pszStart);

/**
 * Locates a case sensitive substring.
 *
 * If any of the two strings are NULL, then NULL is returned. If the needle is
 * an empty string, then the haystack is returned (i.e. matches anything).
 *
 * @returns Pointer to the first occurrence of the substring if found, NULL if
 *          not.
 *
 * @param   pszHaystack The string to search.
 * @param   pszNeedle   The substring to search for.
 *
 * @remarks The difference between this and strstr is the handling of NULL
 *          pointers.
 */
RTDECL(char *) RTStrStr(const char *pszHaystack, const char *pszNeedle);

/**
 * Locates a case insensitive substring.
 *
 * If any of the two strings are NULL, then NULL is returned. If the needle is
 * an empty string, then the haystack is returned (i.e. matches anything).
 *
 * @returns Pointer to the first occurrence of the substring if found, NULL if
 *          not.
 *
 * @param   pszHaystack The string to search.
 * @param   pszNeedle   The substring to search for.
 *
 */
RTDECL(char *) RTStrIStr(const char *pszHaystack, const char *pszNeedle);

/**
 * Converts the string to lower case.
 *
 * @returns Pointer to the converted string.
 * @param   psz     The string to convert.
 */
RTDECL(char *) RTStrToLower(char *psz);

/**
 * Converts the string to upper case.
 *
 * @returns Pointer to the converted string.
 * @param   psz     The string to convert.
 */
RTDECL(char *) RTStrToUpper(char *psz);

/**
 * Checks if the string is case foldable, i.e. whether it would change if
 * subject to RTStrToLower or RTStrToUpper.
 *
 * @returns true / false
 * @param   psz     The string in question.
 */
RTDECL(bool) RTStrIsCaseFoldable(const char *psz);

/**
 * Checks if the string is upper cased (no lower case chars in it).
 *
 * @returns true / false
 * @param   psz     The string in question.
 */
RTDECL(bool) RTStrIsUpperCased(const char *psz);

/**
 * Checks if the string is lower cased (no upper case chars in it).
 *
 * @returns true / false
 * @param   psz     The string in question.
 */
RTDECL(bool) RTStrIsLowerCased(const char *psz);

/**
 * Find the length of a zero-terminated byte string, given
 * a max string length.
 *
 * See also RTStrNLenEx.
 *
 * @returns The string length or cbMax. The returned length does not include
 *          the zero terminator if it was found.
 *
 * @param   pszString   The string.
 * @param   cchMax      The max string length.
 */
RTDECL(size_t) RTStrNLen(const char *pszString, size_t cchMax);

/**
 * Find the length of a zero-terminated byte string, given
 * a max string length.
 *
 * See also RTStrNLen.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if the string has a length less than cchMax.
 * @retval  VERR_BUFFER_OVERFLOW if the end of the string wasn't found
 *          before cchMax was reached.
 *
 * @param   pszString   The string.
 * @param   cchMax      The max string length.
 * @param   pcch        Where to store the string length excluding the
 *                      terminator. This is set to cchMax if the terminator
 *                      isn't found.
 */
RTDECL(int) RTStrNLenEx(const char *pszString, size_t cchMax, size_t *pcch);

RT_C_DECLS_END

/** The maximum size argument of a memchr call. */
#define RTSTR_MEMCHR_MAX            ((~(size_t)0 >> 1) - 15)

/**
 * Find the zero terminator in a string with a limited length.
 *
 * @returns Pointer to the zero terminator.
 * @returns NULL if the zero terminator was not found.
 *
 * @param   pszString   The string.
 * @param   cchMax      The max string length.  RTSTR_MAX is fine.
 */
#if defined(__cplusplus) && !defined(DOXYGEN_RUNNING)
DECLINLINE(char const *) RTStrEnd(char const *pszString, size_t cchMax)
{
    /* Avoid potential issues with memchr seen in glibc.
     * See sysdeps/x86_64/memchr.S in glibc versions older than 2.11 */
    while (cchMax > RTSTR_MEMCHR_MAX)
    {
        char const *pszRet = (char const *)memchr(pszString, '\0', RTSTR_MEMCHR_MAX);
        if (RT_LIKELY(pszRet))
            return pszRet;
        pszString += RTSTR_MEMCHR_MAX;
        cchMax    -= RTSTR_MEMCHR_MAX;
    }
    return (char const *)memchr(pszString, '\0', cchMax);
}

DECLINLINE(char *) RTStrEnd(char *pszString, size_t cchMax)
#else
DECLINLINE(char *) RTStrEnd(const char *pszString, size_t cchMax)
#endif
{
    /* Avoid potential issues with memchr seen in glibc.
     * See sysdeps/x86_64/memchr.S in glibc versions older than 2.11 */
    while (cchMax > RTSTR_MEMCHR_MAX)
    {
        char *pszRet = (char *)memchr(pszString, '\0', RTSTR_MEMCHR_MAX);
        if (RT_LIKELY(pszRet))
            return pszRet;
        pszString += RTSTR_MEMCHR_MAX;
        cchMax    -= RTSTR_MEMCHR_MAX;
    }
    return (char *)memchr(pszString, '\0', cchMax);
}

RT_C_DECLS_BEGIN

/**
 * Finds the offset at which a simple character first occurs in a string.
 *
 * @returns The offset of the first occurence or the terminator offset.
 * @param   pszHaystack The string to search.
 * @param   chNeedle    The character to search for.
 */
DECLINLINE(size_t) RTStrOffCharOrTerm(const char *pszHaystack, char chNeedle)
{
    const char *psz = pszHaystack;
    char ch;
    while (   (ch = *psz) != chNeedle
           && ch != '\0')
        psz++;
    return psz - pszHaystack;
}


/**
 * Matches a simple string pattern.
 *
 * @returns true if the string matches the pattern, otherwise false.
 *
 * @param  pszPattern   The pattern. Special chars are '*' and '?', where the
 *                      asterisk matches zero or more characters and question
 *                      mark matches exactly one character.
 * @param  pszString    The string to match against the pattern.
 */
RTDECL(bool) RTStrSimplePatternMatch(const char *pszPattern, const char *pszString);

/**
 * Matches a simple string pattern, neither which needs to be zero terminated.
 *
 * This is identical to RTStrSimplePatternMatch except that you can optionally
 * specify the length of both the pattern and the string.  The function will
 * stop when it hits a string terminator or either of the lengths.
 *
 * @returns true if the string matches the pattern, otherwise false.
 *
 * @param  pszPattern   The pattern. Special chars are '*' and '?', where the
 *                      asterisk matches zero or more characters and question
 *                      mark matches exactly one character.
 * @param  cchPattern   The pattern length. Pass RTSTR_MAX if you don't know the
 *                      length and wish to stop at the string terminator.
 * @param  pszString    The string to match against the pattern.
 * @param  cchString    The string length. Pass RTSTR_MAX if you don't know the
 *                      length and wish to match up to the string terminator.
 */
RTDECL(bool) RTStrSimplePatternNMatch(const char *pszPattern, size_t cchPattern,
                                      const char *pszString, size_t cchString);

/**
 * Matches multiple patterns against a string.
 *
 * The patterns are separated by the pipe character (|).
 *
 * @returns true if the string matches the pattern, otherwise false.
 *
 * @param   pszPatterns The patterns.
 * @param   cchPatterns The lengths of the patterns to use. Pass RTSTR_MAX to
 *                      stop at the terminator.
 * @param   pszString   The string to match against the pattern.
 * @param   cchString   The string length. Pass RTSTR_MAX stop stop at the
 *                      terminator.
 * @param   poffPattern Offset into the patterns string of the patttern that
 *                      matched. If no match, this will be set to RTSTR_MAX.
 *                      This is optional, NULL is fine.
 */
RTDECL(bool) RTStrSimplePatternMultiMatch(const char *pszPatterns, size_t cchPatterns,
                                          const char *pszString, size_t cchString,
                                          size_t *poffPattern);

/**
 * Compares two version strings RTStrICmp fashion.
 *
 * The version string is split up into sections at punctuation, spaces,
 * underscores, dashes and plus signs.  The sections are then split up into
 * numeric and string sub-sections.  Finally, the sub-sections are compared
 * in a numeric or case insesntivie fashion depending on what they are.
 *
 * The following strings are considered to be equal: "1.0.0", "1.00.0", "1.0",
 * "1".  These aren't: "1.0.0r993", "1.0", "1.0r993", "1.0_Beta3", "1.1"
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 *
 * @param   pszVer1     First version string to compare.
 * @param   pszVer2     Second version string to compare first version with.
 */
RTDECL(int) RTStrVersionCompare(const char *pszVer1, const char *pszVer2);


/** @defgroup rt_str_conv   String To/From Number Conversions
 * @{ */

/**
 * Converts a string representation of a number to a 64-bit unsigned number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pu64        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt64Ex(const char *pszValue, char **ppszNext, unsigned uBase, uint64_t *pu64);

/**
 * Converts a string representation of a number to a 64-bit unsigned number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_TRAILING_CHARS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pu64        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt64Full(const char *pszValue, unsigned uBase, uint64_t *pu64);

/**
 * Converts a string representation of a number to a 64-bit unsigned number.
 * The base is guessed.
 *
 * @returns 64-bit unsigned number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(uint64_t) RTStrToUInt64(const char *pszValue);

/**
 * Converts a string representation of a number to a 32-bit unsigned number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pu32        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt32Ex(const char *pszValue, char **ppszNext, unsigned uBase, uint32_t *pu32);

/**
 * Converts a string representation of a number to a 32-bit unsigned number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_TRAILING_CHARS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pu32        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt32Full(const char *pszValue, unsigned uBase, uint32_t *pu32);

/**
 * Converts a string representation of a number to a 64-bit unsigned number.
 * The base is guessed.
 *
 * @returns 32-bit unsigned number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(uint32_t) RTStrToUInt32(const char *pszValue);

/**
 * Converts a string representation of a number to a 16-bit unsigned number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pu16        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt16Ex(const char *pszValue, char **ppszNext, unsigned uBase, uint16_t *pu16);

/**
 * Converts a string representation of a number to a 16-bit unsigned number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_TRAILING_CHARS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pu16        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt16Full(const char *pszValue, unsigned uBase, uint16_t *pu16);

/**
 * Converts a string representation of a number to a 16-bit unsigned number.
 * The base is guessed.
 *
 * @returns 16-bit unsigned number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(uint16_t) RTStrToUInt16(const char *pszValue);

/**
 * Converts a string representation of a number to a 8-bit unsigned number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pu8         Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt8Ex(const char *pszValue, char **ppszNext, unsigned uBase, uint8_t *pu8);

/**
 * Converts a string representation of a number to a 8-bit unsigned number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_NEGATIVE_UNSIGNED
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_TRAILING_CHARS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pu8         Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToUInt8Full(const char *pszValue, unsigned uBase, uint8_t *pu8);

/**
 * Converts a string representation of a number to a 8-bit unsigned number.
 * The base is guessed.
 *
 * @returns 8-bit unsigned number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(uint8_t) RTStrToUInt8(const char *pszValue);

/**
 * Converts a string representation of a number to a 64-bit signed number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pi64        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt64Ex(const char *pszValue, char **ppszNext, unsigned uBase, int64_t *pi64);

/**
 * Converts a string representation of a number to a 64-bit signed number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VINF_SUCCESS
 * @retval  VERR_TRAILING_CHARS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pi64        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt64Full(const char *pszValue, unsigned uBase, int64_t *pi64);

/**
 * Converts a string representation of a number to a 64-bit signed number.
 * The base is guessed.
 *
 * @returns 64-bit signed number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(int64_t) RTStrToInt64(const char *pszValue);

/**
 * Converts a string representation of a number to a 32-bit signed number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pi32        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt32Ex(const char *pszValue, char **ppszNext, unsigned uBase, int32_t *pi32);

/**
 * Converts a string representation of a number to a 32-bit signed number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VINF_SUCCESS
 * @retval  VERR_TRAILING_CHARS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pi32        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt32Full(const char *pszValue, unsigned uBase, int32_t *pi32);

/**
 * Converts a string representation of a number to a 32-bit signed number.
 * The base is guessed.
 *
 * @returns 32-bit signed number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(int32_t) RTStrToInt32(const char *pszValue);

/**
 * Converts a string representation of a number to a 16-bit signed number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pi16        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt16Ex(const char *pszValue, char **ppszNext, unsigned uBase, int16_t *pi16);

/**
 * Converts a string representation of a number to a 16-bit signed number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VINF_SUCCESS
 * @retval  VERR_TRAILING_CHARS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pi16        Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt16Full(const char *pszValue, unsigned uBase, int16_t *pi16);

/**
 * Converts a string representation of a number to a 16-bit signed number.
 * The base is guessed.
 *
 * @returns 16-bit signed number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(int16_t) RTStrToInt16(const char *pszValue);

/**
 * Converts a string representation of a number to a 8-bit signed number.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   ppszNext    Where to store the pointer to the first char following the number. (Optional)
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pi8         Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt8Ex(const char *pszValue, char **ppszNext, unsigned uBase, int8_t *pi8);

/**
 * Converts a string representation of a number to a 8-bit signed number,
 * making sure the full string is converted.
 *
 * @returns iprt status code.
 *          Warnings are used to indicate conversion problems.
 * @retval  VWRN_NUMBER_TOO_BIG
 * @retval  VINF_SUCCESS
 * @retval  VERR_TRAILING_CHARS
 * @retval  VERR_TRAILING_SPACES
 * @retval  VERR_NO_DIGITS
 *
 * @param   pszValue    Pointer to the string value.
 * @param   uBase       The base of the representation used.
 *                      If 0 the function will look for known prefixes before defaulting to 10.
 * @param   pi8         Where to store the converted number. (optional)
 */
RTDECL(int) RTStrToInt8Full(const char *pszValue, unsigned uBase, int8_t *pi8);

/**
 * Converts a string representation of a number to a 8-bit signed number.
 * The base is guessed.
 *
 * @returns 8-bit signed number on success.
 * @returns 0 on failure.
 * @param   pszValue    Pointer to the string value.
 */
RTDECL(int8_t) RTStrToInt8(const char *pszValue);

/**
 * Formats a buffer stream as hex bytes.
 *
 * The default is no separating spaces or line breaks or anything.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_POINTER if any of the pointers are wrong.
 * @retval  VERR_BUFFER_OVERFLOW if the buffer is insufficent to hold the bytes.
 *
 * @param   pszBuf      Output string buffer.
 * @param   cbBuf       The size of the output buffer.
 * @param   pv          Pointer to the bytes to stringify.
 * @param   cb          The number of bytes to stringify.
 * @param   fFlags      Combination of RTSTRPRINTHEXBYTES_F_XXX values.
 * @sa      RTUtf16PrintHexBytes.
 */
RTDECL(int) RTStrPrintHexBytes(char *pszBuf, size_t cbBuf, void const *pv, size_t cb, uint32_t fFlags);
/** @name RTSTRPRINTHEXBYTES_F_XXX - flags for RTStrPrintHexBytes and RTUtf16PritnHexBytes.
 * @{ */
/** Upper case hex digits, the default is lower case. */
#define RTSTRPRINTHEXBYTES_F_UPPER      RT_BIT(0)
/** Add a space between each group. */
#define RTSTRPRINTHEXBYTES_F_SEP_SPACE  RT_BIT(1)
/** Add a colon between each group. */
#define RTSTRPRINTHEXBYTES_F_SEP_COLON  RT_BIT(2)
/** @} */

/**
 * Converts a string of hex bytes back into binary data.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_POINTER if any of the pointers are wrong.
 * @retval  VERR_BUFFER_OVERFLOW if the string contains too many hex bytes.
 * @retval  VERR_BUFFER_UNDERFLOW if there aren't enough hex bytes to fill up
 *          the output buffer.
 * @retval  VERR_UNEVEN_INPUT if the input contains a half byte.
 * @retval  VERR_NO_DIGITS
 * @retval  VWRN_TRAILING_CHARS
 * @retval  VWRN_TRAILING_SPACES
 *
 * @param   pszHex      The string containing the hex bytes.
 * @param   pv          Output buffer.
 * @param   cb          The size of the output buffer.
 * @param   fFlags      Must be zero, reserved for future use.
 */
RTDECL(int) RTStrConvertHexBytes(char const *pszHex, void *pv, size_t cb, uint32_t fFlags);

/** @} */


/** @defgroup rt_str_space  Unique String Space
 * @{
 */

/** Pointer to a string name space container node core. */
typedef struct RTSTRSPACECORE *PRTSTRSPACECORE;
/** Pointer to a pointer to a string name space container node core. */
typedef PRTSTRSPACECORE *PPRTSTRSPACECORE;

/**
 * String name space container node core.
 */
typedef struct RTSTRSPACECORE
{
    /** Hash key. Don't touch. */
    uint32_t        Key;
    /** Pointer to the left leaf node. Don't touch. */
    PRTSTRSPACECORE pLeft;
    /** Pointer to the left right node. Don't touch. */
    PRTSTRSPACECORE pRight;
    /** Pointer to the list of string with the same key. Don't touch. */
    PRTSTRSPACECORE pList;
    /** Height of this tree: max(heigth(left), heigth(right)) + 1. Don't touch */
    unsigned char   uchHeight;
    /** The string length. Read only! */
    size_t          cchString;
    /** Pointer to the string. Read only! */
    const char     *pszString;
} RTSTRSPACECORE;

/** String space. (Initialize with NULL.) */
typedef PRTSTRSPACECORE     RTSTRSPACE;
/** Pointer to a string space. */
typedef PPRTSTRSPACECORE    PRTSTRSPACE;


/**
 * Inserts a string into a unique string space.
 *
 * @returns true on success.
 * @returns false if the string collided with an existing string.
 * @param   pStrSpace       The space to insert it into.
 * @param   pStr            The string node.
 */
RTDECL(bool) RTStrSpaceInsert(PRTSTRSPACE pStrSpace, PRTSTRSPACECORE pStr);

/**
 * Removes a string from a unique string space.
 *
 * @returns Pointer to the removed string node.
 * @returns NULL if the string was not found in the string space.
 * @param   pStrSpace       The space to remove it from.
 * @param   pszString       The string to remove.
 */
RTDECL(PRTSTRSPACECORE) RTStrSpaceRemove(PRTSTRSPACE pStrSpace, const char *pszString);

/**
 * Gets a string from a unique string space.
 *
 * @returns Pointer to the string node.
 * @returns NULL if the string was not found in the string space.
 * @param   pStrSpace       The space to get it from.
 * @param   pszString       The string to get.
 */
RTDECL(PRTSTRSPACECORE) RTStrSpaceGet(PRTSTRSPACE pStrSpace, const char *pszString);

/**
 * Gets a string from a unique string space.
 *
 * @returns Pointer to the string node.
 * @returns NULL if the string was not found in the string space.
 * @param   pStrSpace       The space to get it from.
 * @param   pszString       The string to get.
 * @param   cchMax          The max string length to evaluate.  Passing
 *                          RTSTR_MAX is ok and makes it behave just like
 *                          RTStrSpaceGet.
 */
RTDECL(PRTSTRSPACECORE) RTStrSpaceGetN(PRTSTRSPACE pStrSpace, const char *pszString, size_t cchMax);

/**
 * Callback function for RTStrSpaceEnumerate() and RTStrSpaceDestroy().
 *
 * @returns 0 on continue.
 * @returns Non-zero to aborts the operation.
 * @param   pStr        The string node
 * @param   pvUser      The user specified argument.
 */
typedef DECLCALLBACK(int)   FNRTSTRSPACECALLBACK(PRTSTRSPACECORE pStr, void *pvUser);
/** Pointer to callback function for RTStrSpaceEnumerate() and RTStrSpaceDestroy(). */
typedef FNRTSTRSPACECALLBACK *PFNRTSTRSPACECALLBACK;

/**
 * Destroys the string space.
 *
 * The caller supplies a callback which will be called for each of the string
 * nodes in for freeing their memory and other resources.
 *
 * @returns 0 or what ever non-zero return value pfnCallback returned
 *          when aborting the destruction.
 * @param   pStrSpace       The space to destroy.
 * @param   pfnCallback     The callback.
 * @param   pvUser          The user argument.
 */
RTDECL(int) RTStrSpaceDestroy(PRTSTRSPACE pStrSpace, PFNRTSTRSPACECALLBACK pfnCallback, void *pvUser);

/**
 * Enumerates the string space.
 * The caller supplies a callback which will be called for each of
 * the string nodes.
 *
 * @returns 0 or what ever non-zero return value pfnCallback returned
 *          when aborting the destruction.
 * @param   pStrSpace       The space to enumerate.
 * @param   pfnCallback     The callback.
 * @param   pvUser          The user argument.
 */
RTDECL(int) RTStrSpaceEnumerate(PRTSTRSPACE pStrSpace, PFNRTSTRSPACECALLBACK pfnCallback, void *pvUser);

/** @} */


/** @defgroup rt_str_hash       Sting hashing
 * @{ */

/**
 * Hashes the given string using algorithm \#1.
 *
 * @returns String hash.
 * @param   pszString       The string to hash.
 */
RTDECL(uint32_t)    RTStrHash1(const char *pszString);

/**
 * Hashes the given string using algorithm \#1.
 *
 * @returns String hash.
 * @param   pszString       The string to hash.
 * @param   cchString       The max length to hash. Hashing will stop if the
 *                          terminator character is encountered first. Passing
 *                          RTSTR_MAX is fine.
 */
RTDECL(uint32_t)    RTStrHash1N(const char *pszString, size_t cchString);

/**
 * Hashes the given strings as if they were concatenated using algorithm \#1.
 *
 * @returns String hash.
 * @param   cPairs          The number of string / length pairs in the
 *                          ellipsis.
 * @param   ...             List of string (const char *) and length
 *                          (size_t) pairs.  Passing RTSTR_MAX as the size is
 *                          fine.
 */
RTDECL(uint32_t)    RTStrHash1ExN(size_t cPairs, ...);

/**
 * Hashes the given strings as if they were concatenated using algorithm \#1.
 *
 * @returns String hash.
 * @param   cPairs          The number of string / length pairs in the @a va.
 * @param   va              List of string (const char *) and length
 *                          (size_t) pairs.  Passing RTSTR_MAX as the size is
 *                          fine.
 */
RTDECL(uint32_t)    RTStrHash1ExNV(size_t cPairs, va_list va);

/** @}  */

/** @} */

RT_C_DECLS_END

#endif

