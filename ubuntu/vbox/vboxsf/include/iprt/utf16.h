/** @file
 * IPRT - String Manipulation, UTF-16 encoding.
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

#ifndef ___iprt_utf16_h
#define ___iprt_utf16_h

#include <iprt/string.h>

RT_C_DECLS_BEGIN


/** @defgroup rt_str_utf16      UTF-16 String Manipulation
 * @ingroup grp_rt_str
 * @{
 */

/**
 * Allocates memory for UTF-16 string storage (default tag).
 *
 * You should normally not use this function, except if there is some very
 * custom string handling you need doing that isn't covered by any of the other
 * APIs.
 *
 * @returns Pointer to the allocated UTF-16 string.  The first wide char is
 *          always set to the string terminator char, the contents of the
 *          remainder of the memory is undefined.  The string must be freed by
 *          calling RTUtf16Free.
 *
 *          NULL is returned if the allocation failed.  Please translate this to
 *          VERR_NO_UTF16_MEMORY and not VERR_NO_MEMORY.  Also consider
 *          RTUtf16AllocEx if an IPRT status code is required.
 *
 * @param   cb                  How many bytes to allocate, will be rounded up
 *                              to a multiple of two. If this is zero, we will
 *                              allocate a terminator wide char anyway.
 */
#define RTUtf16Alloc(cb)                    RTUtf16AllocTag((cb), RTSTR_TAG)

/**
 * Allocates memory for UTF-16 string storage (custom tag).
 *
 * You should normally not use this function, except if there is some very
 * custom string handling you need doing that isn't covered by any of the other
 * APIs.
 *
 * @returns Pointer to the allocated UTF-16 string.  The first wide char is
 *          always set to the string terminator char, the contents of the
 *          remainder of the memory is undefined.  The string must be freed by
 *          calling RTUtf16Free.
 *
 *          NULL is returned if the allocation failed.  Please translate this to
 *          VERR_NO_UTF16_MEMORY and not VERR_NO_MEMORY.  Also consider
 *          RTUtf16AllocExTag if an IPRT status code is required.
 *
 * @param   cb                  How many bytes to allocate, will be rounded up
 *                              to a multiple of two. If this is zero, we will
 *                              allocate a terminator wide char anyway.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(PRTUTF16) RTUtf16AllocTag(size_t cb, const char *pszTag);

/**
 * Reallocates the specified UTF-16 string (default tag).
 *
 * You should normally not use this function, except if there is some very
 * custom string handling you need doing that isn't covered by any of the other
 * APIs.
 *
 * @returns VINF_SUCCESS.
 * @retval  VERR_NO_UTF16_MEMORY if we failed to reallocate the string, @a
 *          *ppwsz remains unchanged.
 *
 * @param   ppwsz               Pointer to the string variable containing the
 *                              input and output string.
 *
 *                              When not freeing the string, the result will
 *                              always have the last RTUTF16 set to the
 *                              terminator character so that when used for
 *                              string truncation the result will be a valid
 *                              C-style string (your job to keep it a valid
 *                              UTF-16 string).
 *
 *                              When the input string is NULL and we're supposed
 *                              to reallocate, the returned string will also
 *                              have the first RTUTF16 set to the terminator
 *                              char so it will be a valid C-style string.
 *
 * @param   cbNew               When @a cbNew is zero, we'll behave like
 *                              RTUtf16Free and @a *ppwsz will be set to NULL.
 *
 *                              When not zero, this will be rounded up to a
 *                              multiple of two, and used as the new size of the
 *                              memory backing the string, i.e. it includes the
 *                              terminator (RTUTF16) char.
 */
#define RTUtf16Realloc(ppwsz, cbNew)    RTUtf16ReallocTag((ppwsz), (cbNew), RTSTR_TAG)

/**
 * Reallocates the specified UTF-16 string (custom tag).
 *
 * You should normally not use this function, except if there is some very
 * custom string handling you need doing that isn't covered by any of the other
 * APIs.
 *
 * @returns VINF_SUCCESS.
 * @retval  VERR_NO_UTF16_MEMORY if we failed to reallocate the string, @a
 *          *ppwsz remains unchanged.
 *
 * @param   ppwsz               Pointer to the string variable containing the
 *                              input and output string.
 *
 *                              When not freeing the string, the result will
 *                              always have the last RTUTF16 set to the
 *                              terminator character so that when used for
 *                              string truncation the result will be a valid
 *                              C-style string (your job to keep it a valid
 *                              UTF-16 string).
 *
 *                              When the input string is NULL and we're supposed
 *                              to reallocate, the returned string will also
 *                              have the first RTUTF16 set to the terminator
 *                              char so it will be a valid C-style string.
 *
 * @param   cbNew               When @a cbNew is zero, we'll behave like
 *                              RTUtf16Free and @a *ppwsz will be set to NULL.
 *
 *                              When not zero, this will be rounded up to a
 *                              multiple of two, and used as the new size of the
 *                              memory backing the string, i.e. it includes the
 *                              terminator (RTUTF16) char.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(int) RTUtf16ReallocTag(PRTUTF16 *ppwsz, size_t cbNew, const char *pszTag);

/**
 * Free a UTF-16 string allocated by RTStrToUtf16(), RTStrToUtf16Ex(),
 * RTLatin1ToUtf16(), RTLatin1ToUtf16Ex(), RTUtf16Dup() or RTUtf16DupEx().
 *
 * @returns iprt status code.
 * @param   pwszString      The UTF-16 string to free. NULL is accepted.
 */
RTDECL(void)  RTUtf16Free(PRTUTF16 pwszString);

/**
 * Allocates a new copy of the specified UTF-16 string (default tag).
 *
 * @returns Pointer to the allocated string copy. Use RTUtf16Free() to free it.
 * @returns NULL when out of memory.
 * @param   pwszString      UTF-16 string to duplicate.
 * @remark  This function will not make any attempt to validate the encoding.
 */
#define RTUtf16Dup(pwszString)          RTUtf16DupTag((pwszString), RTSTR_TAG)

/**
 * Allocates a new copy of the specified UTF-16 string (custom tag).
 *
 * @returns Pointer to the allocated string copy. Use RTUtf16Free() to free it.
 * @returns NULL when out of memory.
 * @param   pwszString      UTF-16 string to duplicate.
 * @param   pszTag          Allocation tag used for statistics and such.
 * @remark  This function will not make any attempt to validate the encoding.
 */
RTDECL(PRTUTF16) RTUtf16DupTag(PCRTUTF16 pwszString, const char *pszTag);

/**
 * Allocates a new copy of the specified UTF-16 string (default tag).
 *
 * @returns iprt status code.
 * @param   ppwszString     Receives pointer of the allocated UTF-16 string.
 *                          The returned pointer must be freed using RTUtf16Free().
 * @param   pwszString      UTF-16 string to duplicate.
 * @param   cwcExtra        Number of extra RTUTF16 items to allocate.
 * @remark  This function will not make any attempt to validate the encoding.
 */
#define RTUtf16DupEx(ppwszString, pwszString, cwcExtra) \
    RTUtf16DupExTag((ppwszString), (pwszString), (cwcExtra), RTSTR_TAG)

/**
 * Allocates a new copy of the specified UTF-16 string (custom tag).
 *
 * @returns iprt status code.
 * @param   ppwszString     Receives pointer of the allocated UTF-16 string.
 *                          The returned pointer must be freed using RTUtf16Free().
 * @param   pwszString      UTF-16 string to duplicate.
 * @param   cwcExtra        Number of extra RTUTF16 items to allocate.
 * @param   pszTag          Allocation tag used for statistics and such.
 * @remark  This function will not make any attempt to validate the encoding.
 */
RTDECL(int) RTUtf16DupExTag(PRTUTF16 *ppwszString, PCRTUTF16 pwszString, size_t cwcExtra, const char *pszTag);

/**
 * Returns the length of a UTF-16 string in UTF-16 characters
 * without trailing '\\0'.
 *
 * Surrogate pairs counts as two UTF-16 characters here. Use RTUtf16CpCnt()
 * to get the exact number of code points in the string.
 *
 * @returns The number of RTUTF16 items in the string.
 * @param   pwszString  Pointer the UTF-16 string.
 * @remark  This function will not make any attempt to validate the encoding.
 */
RTDECL(size_t) RTUtf16Len(PCRTUTF16 pwszString);

/**
 * Find the length of a zero-terminated byte string, given a max string length.
 *
 * @returns The string length or cbMax. The returned length does not include
 *          the zero terminator if it was found.
 *
 * @param   pwszString  The string.
 * @param   cwcMax      The max string length in RTUTF16s.
 * @sa      RTUtf16NLenEx, RTStrNLen.
 */
RTDECL(size_t) RTUtf16NLen(PCRTUTF16 pwszString, size_t cwcMax);

/**
 * Find the length of a zero-terminated byte string, given
 * a max string length.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if the string has a length less than cchMax.
 * @retval  VERR_BUFFER_OVERFLOW if the end of the string wasn't found
 *          before cwcMax was reached.
 *
 * @param   pwszString  The string.
 * @param   cwcMax      The max string length in RTUTF16s.
 * @param   pcwc        Where to store the string length excluding the
 *                      terminator.  This is set to cwcMax if the terminator
 *                      isn't found.
 * @sa      RTUtf16NLen, RTStrNLenEx.
 */
RTDECL(int) RTUtf16NLenEx(PCRTUTF16 pwszString, size_t cwcMax, size_t *pcwc);

/**
 * Find the zero terminator in a string with a limited length.
 *
 * @returns Pointer to the zero terminator.
 * @returns NULL if the zero terminator was not found.
 *
 * @param   pwszString  The string.
 * @param   cwcMax      The max string length.  RTSTR_MAX is fine.
 */
RTDECL(PCRTUTF16) RTUtf16End(PCRTUTF16 pwszString, size_t cwcMax);

/**
 * Strips blankspaces from both ends of the string.
 *
 * @returns Pointer to first non-blank char in the string.
 * @param   pwsz    The string to strip.
 */
RTDECL(PRTUTF16) RTUtf16Strip(PRTUTF16 pwsz);

/**
 * Strips blankspaces from the start of the string.
 *
 * @returns Pointer to first non-blank char in the string.
 * @param   pwsz    The string to strip.
 */
RTDECL(PRTUTF16) RTUtf16StripL(PCRTUTF16 pwsz);

/**
 * Strips blankspaces from the end of the string.
 *
 * @returns pwsz.
 * @param   pwsz    The string to strip.
 */
RTDECL(PRTUTF16) RTUtf16StripR(PRTUTF16 pwsz);

/**
 * String copy with overflow handling.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   pwszDst             The destination buffer.
 * @param   cwcDst              The size of the destination buffer in RTUTF16s.
 * @param   pwszSrc             The source string.  NULL is not OK.
 */
RTDECL(int) RTUtf16Copy(PRTUTF16 pwszDst, size_t cwcDst, PCRTUTF16 pwszSrc);

/**
 * String copy with overflow handling, ASCII source.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   pwszDst             The destination buffer.
 * @param   cwcDst              The size of the destination buffer in RTUTF16s.
 * @param   pszSrc              The source string, pure ASCII.  NULL is not OK.
 */
RTDECL(int) RTUtf16CopyAscii(PRTUTF16 pwszDst, size_t cwcDst, const char *pszSrc);

/**
 * String copy with overflow handling.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   pwszDst             The destination buffer.
 * @param   cwcDst              The size of the destination buffer in RTUTF16s.
 * @param   pwszSrc             The source string.  NULL is not OK.
 * @param   cwcSrcMax           The maximum number of chars (not code points) to
 *                              copy from the source string, not counting the
 *                              terminator as usual.
 */
RTDECL(int) RTUtf16CopyEx(PRTUTF16 pwszDst, size_t cwcDst, PCRTUTF16 pwszSrc, size_t cwcSrcMax);

/**
 * String concatenation with overflow handling.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   pwszDst             The destination buffer.
 * @param   cwcDst              The size of the destination buffer in RTUTF16s.
 * @param   pwszSrc             The source string.  NULL is not OK.
 */
RTDECL(int) RTUtf16Cat(PRTUTF16 pwszDst, size_t cwcDst, PCRTUTF16 pwszSrc);

/**
 * String concatenation with overflow handling, ASCII source.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   pwszDst             The destination buffer.
 * @param   cwcDst              The size of the destination buffer in RTUTF16s.
 * @param   pszSrc              The source string, pure ASCII.  NULL is not OK.
 */
RTDECL(int) RTUtf16CatAscii(PRTUTF16 pwszDst, size_t cwcDst, const char *pszSrc);

/**
 * String concatenation with overflow handling.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the destination buffer is too small.  The
 *          buffer will contain as much of the string as it can hold, fully
 *          terminated.
 *
 * @param   pwszDst             The destination buffer.
 * @param   cwcDst              The size of the destination buffer in RTUTF16s.
 * @param   pwszSrc             The source string.  NULL is not OK.
 * @param   cwcSrcMax           The maximum number of UTF-16 chars (not code
 *                              points) to copy from the source string, not
 *                              counting the terminator as usual.
 */
RTDECL(int) RTUtf16CatEx(PRTUTF16 pwszDst, size_t cwcDst, PCRTUTF16 pwszSrc, size_t cwcSrcMax);

/**
 * Performs a case sensitive string compare between two UTF-16 strings.
 *
 * @returns < 0 if the first string less than the second string.s
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       First UTF-16 string. Null is allowed.
 * @param   pwsz2       Second UTF-16 string. Null is allowed.
 * @remark  This function will not make any attempt to validate the encoding.
 */
RTDECL(int) RTUtf16Cmp(PCRTUTF16 pwsz1, PCRTUTF16 pwsz2);

/**
 * Performs a case sensitive string compare between an UTF-16 string and a pure
 * ASCII string.
 *
 * @returns < 0 if the first string less than the second string.s
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       First UTF-16 string. Null is allowed.
 * @param   psz2        Second string, pure ASCII. Null is allowed.
 * @remark  This function will not make any attempt to validate the encoding.
 */
RTDECL(int) RTUtf16CmpAscii(PCRTUTF16 pwsz1, const char *psz2);

/**
 * Performs a case sensitive string compare between an UTF-16 string and a UTF-8
 * string.
 *
 * @returns < 0 if the first string less than the second string.s
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       First UTF-16 string. Null is allowed.
 * @param   psz2        Second string, UTF-8. Null is allowed.
 * @remarks NULL and empty strings are treated equally.
 */
RTDECL(int) RTUtf16CmpUtf8(PCRTUTF16 pwsz1, const char *psz2);

/**
 * Performs a case insensitive string compare between two UTF-16 strings.
 *
 * This is a simplified compare, as only the simplified lower/upper case folding
 * specified by the unicode specs are used. It does not consider character pairs
 * as they are used in some languages, just simple upper & lower case compares.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       First UTF-16 string. Null is allowed.
 * @param   pwsz2       Second UTF-16 string. Null is allowed.
 */
RTDECL(int) RTUtf16ICmp(PCRTUTF16 pwsz1, PCRTUTF16 pwsz2);

/**
 * Performs a case insensitive string compare between two big endian UTF-16
 * strings.
 *
 * This is a simplified compare, as only the simplified lower/upper case folding
 * specified by the unicode specs are used. It does not consider character pairs
 * as they are used in some languages, just simple upper & lower case compares.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       First big endian UTF-16 string. Null is allowed.
 * @param   pwsz2       Second big endian  UTF-16 string. Null is allowed.
 */
RTDECL(int) RTUtf16BigICmp(PCRTUTF16 pwsz1, PCRTUTF16 pwsz2);

/**
 * Performs a case insensitive string compare between an UTF-16 string and a
 * UTF-8 string.
 *
 * @returns < 0 if the first string less than the second string.s
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       First UTF-16 string. Null is allowed.
 * @param   psz2        Second string, UTF-8. Null is allowed.
 * @remarks NULL and empty strings are treated equally.
 */
RTDECL(int) RTUtf16ICmpUtf8(PCRTUTF16 pwsz1, const char *psz2);

/**
 * Performs a case insensitive string compare between an UTF-16 string and a
 * pure ASCII string.
 *
 * Since this compare only takes cares about the first 128 codepoints in
 * unicode, no tables are needed and there aren't any real complications.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       First UTF-16 string. Null is allowed.
 * @param   psz2        Second string, pure ASCII. Null is allowed.
 */
RTDECL(int) RTUtf16ICmpAscii(PCRTUTF16 pwsz1, const char *psz2);

/**
 * Performs a case insensitive string compare between two UTF-16 strings
 * using the current locale of the process (if applicable).
 *
 * This differs from RTUtf16ICmp() in that it will try, if a locale with the
 * required data is available, to do a correct case-insensitive compare. It
 * follows that it is more complex and thereby likely to be more expensive.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       First UTF-16 string. Null is allowed.
 * @param   pwsz2       Second UTF-16 string. Null is allowed.
 */
RTDECL(int) RTUtf16LocaleICmp(PCRTUTF16 pwsz1, PCRTUTF16 pwsz2);

/**
 * Performs a case insensitive string compare between two UTF-16 strings,
 * stopping after N characters.
 *
 * This is a simplified compare, as only the simplified lower/upper case folding
 * specified by the unicode specs are used. It does not consider character pairs
 * as they are used in some languages, just simple upper & lower case compares.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       First UTF-16 string. Null is allowed.
 * @param   pwsz2       Second UTF-16 string. Null is allowed.
 * @param   cwcMax      Maximum number of characters to compare.
 */
RTDECL(int) RTUtf16NICmp(PCRTUTF16 pwsz1, PCRTUTF16 pwsz2, size_t cwcMax);

/**
 * Performs a case insensitive string compare between two big endian UTF-16
 * strings, stopping after N characters.
 *
 * This is a simplified compare, as only the simplified lower/upper case folding
 * specified by the unicode specs are used. It does not consider character pairs
 * as they are used in some languages, just simple upper & lower case compares.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       First big endian UTF-16 string. Null is allowed.
 * @param   pwsz2       Second big endian UTF-16 string. Null is allowed.
 * @param   cwcMax      Maximum number of characters to compare.
 */
RTDECL(int) RTUtf16BigNICmp(PCRTUTF16 pwsz1, PCRTUTF16 pwsz2, size_t cwcMax);

/**
 * Performs a case insensitive string compare between a UTF-16 string and a pure
 * ASCII string, stopping after N characters.
 *
 * Since this compare only takes cares about the first 128 codepoints in
 * unicode, no tables are needed and there aren't any real complications.
 *
 * @returns < 0 if the first string less than the second string.
 * @returns 0 if the first string identical to the second string.
 * @returns > 0 if the first string greater than the second string.
 * @param   pwsz1       The UTF-16 first string. Null is allowed.
 * @param   psz2        The pure ASCII second string. Null is allowed.
 * @param   cwcMax      Maximum number of UTF-16 characters to compare.
 */
RTDECL(int) RTUtf16NICmpAscii(PCRTUTF16 pwsz1, const char *psz2, size_t cwcMax);


/**
 * Folds a UTF-16 string to lowercase.
 *
 * This is a very simple folding; is uses the simple lowercase
 * code point, it is not related to any locale just the most common
 * lowercase codepoint setup by the unicode specs, and it will not
 * create new surrogate pairs or remove existing ones.
 *
 * @returns Pointer to the passed in string.
 * @param   pwsz        The string to fold.
 */
RTDECL(PRTUTF16) RTUtf16ToLower(PRTUTF16 pwsz);

/**
 * Folds a UTF-16 string to uppercase.
 *
 * This is a very simple folding; is uses the simple uppercase
 * code point, it is not related to any locale just the most common
 * uppercase codepoint setup by the unicode specs, and it will not
 * create new surrogate pairs or remove existing ones.
 *
 * @returns Pointer to the passed in string.
 * @param   pwsz        The string to fold.
 */
RTDECL(PRTUTF16) RTUtf16ToUpper(PRTUTF16 pwsz);

/**
 * Validates the UTF-16 encoding of the string.
 *
 * @returns iprt status code.
 * @param   pwsz        The string.
 */
RTDECL(int) RTUtf16ValidateEncoding(PCRTUTF16 pwsz);

/**
 * Validates the UTF-16 encoding of the string.
 *
 * @returns iprt status code.
 * @param   pwsz        The string.
 * @param   cwc         The max string length (/ size) in UTF-16 units. Use
 *                      RTSTR_MAX to process the entire string.
 * @param   fFlags      Combination of RTSTR_VALIDATE_ENCODING_XXX flags.
 */
RTDECL(int) RTUtf16ValidateEncodingEx(PCRTUTF16 pwsz, size_t cwc, uint32_t fFlags);

/**
 * Checks if the UTF-16 encoding is valid.
 *
 * @returns true / false.
 * @param   pwsz        The string.
 */
RTDECL(bool) RTUtf16IsValidEncoding(PCRTUTF16 pwsz);

/**
 * Sanitise a (valid) UTF-16 string by replacing all characters outside a white
 * list in-place by an ASCII replacement character.
 *
 * Surrogate paris will be replaced by two chars.
 *
 * @returns The number of code points replaced.  In the case of an incorrectly
 *          encoded string -1 will be returned, and the string is not completely
 *          processed.  In the case of puszValidPairs having an odd number of
 *          code points, -1 will be also return but without any modification to
 *          the string.
 * @param   pwsz           The string to sanitise.
 * @param   puszValidPairs A zero-terminated array of pairs of Unicode points.
 *                         Each pair is the start and end point of a range,
 *                         and the union of these ranges forms the white list.
 * @param   chReplacement  The ASCII replacement character.
 * @sa      RTStrPurgeComplementSet
 */
RTDECL(ssize_t) RTUtf16PurgeComplementSet(PRTUTF16 pwsz, PCRTUNICP puszValidPairs, char chReplacement);


/**
 * Translate a UTF-16 string into a UTF-8 allocating the result buffer (default
 * tag).
 *
 * @returns iprt status code.
 * @param   pwszString      UTF-16 string to convert.
 * @param   ppszString      Receives pointer of allocated UTF-8 string on
 *                          success, and is always set to NULL on failure.
 *                          The returned pointer must be freed using RTStrFree().
 */
#define RTUtf16ToUtf8(pwszString, ppszString)       RTUtf16ToUtf8Tag((pwszString), (ppszString), RTSTR_TAG)

/**
 * Translate a UTF-16 string into a UTF-8 allocating the result buffer.
 *
 * @returns iprt status code.
 * @param   pwszString      UTF-16 string to convert.
 * @param   ppszString      Receives pointer of allocated UTF-8 string on
 *                          success, and is always set to NULL on failure.
 *                          The returned pointer must be freed using RTStrFree().
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTUtf16ToUtf8Tag(PCRTUTF16 pwszString, char **ppszString, const char *pszTag);

/**
 * Translate a UTF-16BE string into a UTF-8 allocating the result buffer
 * (default tag).
 *
 * This differs from RTUtf16ToUtf8 in that the input is always a
 * big-endian string.
 *
 * @returns iprt status code.
 * @param   pwszString      UTF-16BE string to convert.
 * @param   ppszString      Receives pointer of allocated UTF-8 string on
 *                          success, and is always set to NULL on failure.
 *                          The returned pointer must be freed using RTStrFree().
 */
#define RTUtf16BigToUtf8(pwszString, ppszString)       RTUtf16BigToUtf8Tag((pwszString), (ppszString), RTSTR_TAG)

/**
 * Translate a UTF-16BE string into a UTF-8 allocating the result buffer.
 *
 * This differs from RTUtf16ToUtf8Tag in that the input is always a
 * big-endian string.
 *
 * @returns iprt status code.
 * @param   pwszString      UTF-16BE string to convert.
 * @param   ppszString      Receives pointer of allocated UTF-8 string on
 *                          success, and is always set to NULL on failure.
 *                          The returned pointer must be freed using RTStrFree().
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTUtf16BigToUtf8Tag(PCRTUTF16 pwszString, char **ppszString, const char *pszTag);

/**
 * Translate a UTF-16LE string into a UTF-8 allocating the result buffer
 * (default tag).
 *
 * This differs from RTUtf16ToUtf8 in that the input is always a
 * little-endian string.
 *
 * @returns iprt status code.
 * @param   pwszString      UTF-16LE string to convert.
 * @param   ppszString      Receives pointer of allocated UTF-8 string on
 *                          success, and is always set to NULL on failure.
 *                          The returned pointer must be freed using RTStrFree().
 */
#define RTUtf16LittleToUtf8(pwszString, ppszString)     RTUtf16LittleToUtf8Tag((pwszString), (ppszString), RTSTR_TAG)

/**
 * Translate a UTF-16LE string into a UTF-8 allocating the result buffer.
 *
 * This differs from RTUtf16ToUtf8Tag in that the input is always a
 * little-endian string.
 *
 * @returns iprt status code.
 * @param   pwszString      UTF-16LE string to convert.
 * @param   ppszString      Receives pointer of allocated UTF-8 string on
 *                          success, and is always set to NULL on failure.
 *                          The returned pointer must be freed using RTStrFree().
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTUtf16LittleToUtf8Tag(PCRTUTF16 pwszString, char **ppszString, const char *pszTag);


/**
 * Translates UTF-16 to UTF-8 using buffer provided by the caller or a fittingly
 * sized buffer allocated by the function (default tag).
 *
 * @returns iprt status code.
 * @param   pwszString      The UTF-16 string to convert.
 * @param   cwcString       The number of RTUTF16 items to translate from pwszString.
 *                          The translation will stop when reaching cwcString or the terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppsz            If cch is non-zero, this must either be pointing to a pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppsz is NULL or cch is zero a buffer of at least cch chars
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTStrFree().
 * @param   cch             The buffer size in chars (the type). This includes the terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 */
#define RTUtf16ToUtf8Ex(pwszString, cwcString, ppsz, cch, pcch) \
    RTUtf16ToUtf8ExTag((pwszString), (cwcString), (ppsz), (cch), (pcch), RTSTR_TAG)

/**
 * Translates UTF-16 to UTF-8 using buffer provided by the caller or a fittingly
 * sized buffer allocated by the function (custom tag).
 *
 * @returns iprt status code.
 * @param   pwszString      The UTF-16 string to convert.
 * @param   cwcString       The number of RTUTF16 items to translate from pwszString.
 *                          The translation will stop when reaching cwcString or the terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppsz            If cch is non-zero, this must either be pointing to a pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppsz is NULL or cch is zero a buffer of at least cch chars
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTStrFree().
 * @param   cch             The buffer size in chars (the type). This includes the terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTUtf16ToUtf8ExTag(PCRTUTF16 pwszString, size_t cwcString, char **ppsz, size_t cch, size_t *pcch, const char *pszTag);

/**
 * Translates UTF-16BE to UTF-8 using buffer provided by the caller or a
 * fittingly sized buffer allocated by the function (default tag).
 *
 * This differs from RTUtf16ToUtf8Ex in that the input is always a
 * big-endian string.
 *
 * @returns iprt status code.
 * @param   pwszString      The UTF-16BE string to convert.
 * @param   cwcString       The number of RTUTF16 items to translate from pwszString.
 *                          The translation will stop when reaching cwcString or the terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppsz            If cch is non-zero, this must either be pointing to a pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppsz is NULL or cch is zero a buffer of at least cch chars
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTStrFree().
 * @param   cch             The buffer size in chars (the type). This includes the terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 */
#define RTUtf16BigToUtf8Ex(pwszString, cwcString, ppsz, cch, pcch) \
    RTUtf16BigToUtf8ExTag((pwszString), (cwcString), (ppsz), (cch), (pcch), RTSTR_TAG)

/**
 * Translates UTF-16BE to UTF-8 using buffer provided by the caller or a
 * fittingly sized buffer allocated by the function (custom tag).
 *
 * This differs from RTUtf16ToUtf8ExTag in that the input is always a
 * big-endian string.
 *
 * @returns iprt status code.
 * @param   pwszString      The UTF-16BE string to convert.
 * @param   cwcString       The number of RTUTF16 items to translate from pwszString.
 *                          The translation will stop when reaching cwcString or the terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppsz            If cch is non-zero, this must either be pointing to a pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppsz is NULL or cch is zero a buffer of at least cch chars
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTStrFree().
 * @param   cch             The buffer size in chars (the type). This includes the terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int) RTUtf16BigToUtf8ExTag(PCRTUTF16 pwszString, size_t cwcString, char **ppsz, size_t cch, size_t *pcch, const char *pszTag);

/**
 * Translates UTF-16LE to UTF-8 using buffer provided by the caller or a
 * fittingly sized buffer allocated by the function (default tag).
 *
 * This differs from RTUtf16ToUtf8Ex in that the input is always a
 * little-endian string.
 *
 * @returns iprt status code.
 * @param   pwszString      The UTF-16LE string to convert.
 * @param   cwcString       The number of RTUTF16 items to translate from pwszString.
 *                          The translation will stop when reaching cwcString or the terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppsz            If cch is non-zero, this must either be pointing to a pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppsz is NULL or cch is zero a buffer of at least cch chars
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTStrFree().
 * @param   cch             The buffer size in chars (the type). This includes the terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 */
#define RTUtf16LittleToUtf8Ex(pwszString, cwcString, ppsz, cch, pcch) \
    RTUtf16LittleToUtf8ExTag((pwszString), (cwcString), (ppsz), (cch), (pcch), RTSTR_TAG)

/**
 * Translates UTF-16LE to UTF-8 using buffer provided by the caller or a
 * fittingly sized buffer allocated by the function (custom tag).
 *
 * This differs from RTUtf16ToUtf8ExTag in that the input is always a
 * little-endian string.
 *
 * @returns iprt status code.
 * @param   pwszString      The UTF-16LE string to convert.
 * @param   cwcString       The number of RTUTF16 items to translate from pwszString.
 *                          The translation will stop when reaching cwcString or the terminator ('\\0').
 *                          Use RTSTR_MAX to translate the entire string.
 * @param   ppsz            If cch is non-zero, this must either be pointing to a pointer to
 *                          a buffer of the specified size, or pointer to a NULL pointer.
 *                          If *ppsz is NULL or cch is zero a buffer of at least cch chars
 *                          will be allocated to hold the translated string.
 *                          If a buffer was requested it must be freed using RTStrFree().
 * @param   cch             The buffer size in chars (the type). This includes the terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int) RTUtf16LittleToUtf8ExTag(PCRTUTF16 pwszString, size_t cwcString, char **ppsz, size_t cch, size_t *pcch,
                                     const char *pszTag);

/**
 * Calculates the length of the UTF-16 string in UTF-8 chars (bytes).
 *
 * This function will validate the string, and incorrectly encoded UTF-16
 * strings will be rejected. The primary purpose of this function is to
 * help allocate buffers for RTUtf16ToUtf8() of the correct size. For most
 * other purposes RTUtf16ToUtf8Ex() should be used.
 *
 * @returns Number of char (bytes).
 * @returns 0 if the string was incorrectly encoded.
 * @param   pwsz        The UTF-16 string.
 */
RTDECL(size_t) RTUtf16CalcUtf8Len(PCRTUTF16 pwsz);

/**
 * Calculates the length of the UTF-16BE string in UTF-8 chars (bytes).
 *
 * This function will validate the string, and incorrectly encoded UTF-16BE
 * strings will be rejected.  The primary purpose of this function is to
 * help allocate buffers for RTUtf16BigToUtf8() of the correct size.  For most
 * other purposes RTUtf16BigToUtf8Ex() should be used.
 *
 * @returns Number of char (bytes).
 * @returns 0 if the string was incorrectly encoded.
 * @param   pwsz        The UTF-16BE string.
 */
RTDECL(size_t) RTUtf16BigCalcUtf8Len(PCRTUTF16 pwsz);

/**
 * Calculates the length of the UTF-16LE string in UTF-8 chars (bytes).
 *
 * This function will validate the string, and incorrectly encoded UTF-16LE
 * strings will be rejected.  The primary purpose of this function is to
 * help allocate buffers for RTUtf16LittleToUtf8() of the correct size.  For
 * most other purposes RTUtf16LittleToUtf8Ex() should be used.
 *
 * @returns Number of char (bytes).
 * @returns 0 if the string was incorrectly encoded.
 * @param   pwsz        The UTF-16LE string.
 */
RTDECL(size_t) RTUtf16LittleCalcUtf8Len(PCRTUTF16 pwsz);

/**
 * Calculates the length of the UTF-16 string in UTF-8 chars (bytes).
 *
 * This function will validate the string, and incorrectly encoded UTF-16
 * strings will be rejected.
 *
 * @returns iprt status code.
 * @param   pwsz        The string.
 * @param   cwc         The max string length. Use RTSTR_MAX to process the entire string.
 * @param   pcch        Where to store the string length (in bytes). Optional.
 *                      This is undefined on failure.
 */
RTDECL(int) RTUtf16CalcUtf8LenEx(PCRTUTF16 pwsz, size_t cwc, size_t *pcch);

/**
 * Calculates the length of the UTF-16BE string in UTF-8 chars (bytes).
 *
 * This function will validate the string, and incorrectly encoded UTF-16BE
 * strings will be rejected.
 *
 * @returns iprt status code.
 * @param   pwsz        The string.
 * @param   cwc         The max string length. Use RTSTR_MAX to process the entire string.
 * @param   pcch        Where to store the string length (in bytes). Optional.
 *                      This is undefined on failure.
 */
RTDECL(int) RTUtf16BigCalcUtf8LenEx(PCRTUTF16 pwsz, size_t cwc, size_t *pcch);

/**
 * Calculates the length of the UTF-16LE string in UTF-8 chars (bytes).
 *
 * This function will validate the string, and incorrectly encoded UTF-16LE
 * strings will be rejected.
 *
 * @returns iprt status code.
 * @param   pwsz        The string.
 * @param   cwc         The max string length. Use RTSTR_MAX to process the entire string.
 * @param   pcch        Where to store the string length (in bytes). Optional.
 *                      This is undefined on failure.
 */
RTDECL(int) RTUtf16LittleCalcUtf8LenEx(PCRTUTF16 pwsz, size_t cwc, size_t *pcch);

/**
 * Translate a UTF-16 string into a Latin-1 (ISO-8859-1) allocating the result
 * buffer (default tag).
 *
 * @returns iprt status code.
 * @param   pwszString      UTF-16 string to convert.
 * @param   ppszString      Receives pointer of allocated Latin1 string on
 *                          success, and is always set to NULL on failure.
 *                          The returned pointer must be freed using RTStrFree().
 */
#define RTUtf16ToLatin1(pwszString, ppszString)     RTUtf16ToLatin1Tag((pwszString), (ppszString), RTSTR_TAG)

/**
 * Translate a UTF-16 string into a Latin-1 (ISO-8859-1) allocating the result
 * buffer (custom tag).
 *
 * @returns iprt status code.
 * @param   pwszString      UTF-16 string to convert.
 * @param   ppszString      Receives pointer of allocated Latin1 string on
 *                          success, and is always set to NULL on failure.
 *                          The returned pointer must be freed using RTStrFree().
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTUtf16ToLatin1Tag(PCRTUTF16 pwszString, char **ppszString, const char *pszTag);

/**
 * Translates UTF-16 to Latin-1 (ISO-8859-1) using buffer provided by the caller
 * or a fittingly sized buffer allocated by the function (default tag).
 *
 * @returns iprt status code.
 * @param   pwszString      The UTF-16 string to convert.
 * @param   cwcString       The number of RTUTF16 items to translate from
 *                          pwszString. The translation will stop when reaching
 *                          cwcString or the terminator ('\\0'). Use RTSTR_MAX
 *                          to translate the entire string.
 * @param   ppsz            Pointer to the pointer to the Latin-1 string. The
 *                          buffer can optionally be preallocated by the caller.
 *
 *                          If cch is zero, *ppsz is undefined.
 *
 *                          If cch is non-zero and *ppsz is not NULL, then this
 *                          will be used as the output buffer.
 *                          VERR_BUFFER_OVERFLOW will be returned if this is
 *                          insufficient.
 *
 *                          If cch is zero or *ppsz is NULL, then a buffer of
 *                          sufficient size is allocated. cch can be used to
 *                          specify a minimum size of this buffer. Use
 *                          RTUtf16Free() to free the result.
 *
 * @param   cch             The buffer size in chars (the type). This includes
 *                          the terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 */
#define RTUtf16ToLatin1Ex(pwszString, cwcString, ppsz, cch, pcch) \
    RTUtf16ToLatin1ExTag((pwszString), (cwcString), (ppsz), (cch), (pcch), RTSTR_TAG)

/**
 * Translates UTF-16 to Latin-1 (ISO-8859-1) using buffer provided by the caller
 * or a fittingly sized buffer allocated by the function (custom tag).
 *
 * @returns iprt status code.
 * @param   pwszString      The UTF-16 string to convert.
 * @param   cwcString       The number of RTUTF16 items to translate from
 *                          pwszString. The translation will stop when reaching
 *                          cwcString or the terminator ('\\0'). Use RTSTR_MAX
 *                          to translate the entire string.
 * @param   ppsz            Pointer to the pointer to the Latin-1 string. The
 *                          buffer can optionally be preallocated by the caller.
 *
 *                          If cch is zero, *ppsz is undefined.
 *
 *                          If cch is non-zero and *ppsz is not NULL, then this
 *                          will be used as the output buffer.
 *                          VERR_BUFFER_OVERFLOW will be returned if this is
 *                          insufficient.
 *
 *                          If cch is zero or *ppsz is NULL, then a buffer of
 *                          sufficient size is allocated. cch can be used to
 *                          specify a minimum size of this buffer. Use
 *                          RTUtf16Free() to free the result.
 *
 * @param   cch             The buffer size in chars (the type). This includes
 *                          the terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTUtf16ToLatin1ExTag(PCRTUTF16 pwszString, size_t cwcString, char **ppsz, size_t cch, size_t *pcch, const char *pszTag);

/**
 * Calculates the length of the UTF-16 string in Latin-1 (ISO-8859-1) chars.
 *
 * This function will validate the string, and incorrectly encoded UTF-16
 * strings will be rejected. The primary purpose of this function is to
 * help allocate buffers for RTUtf16ToLatin1() of the correct size. For most
 * other purposes RTUtf16ToLatin1Ex() should be used.
 *
 * @returns Number of char (bytes).
 * @returns 0 if the string was incorrectly encoded.
 * @param   pwsz        The UTF-16 string.
 */
RTDECL(size_t) RTUtf16CalcLatin1Len(PCRTUTF16 pwsz);

/**
 * Calculates the length of the UTF-16 string in Latin-1 (ISO-8859-1) chars.
 *
 * This function will validate the string, and incorrectly encoded UTF-16
 * strings will be rejected.
 *
 * @returns iprt status code.
 * @param   pwsz        The string.
 * @param   cwc         The max string length. Use RTSTR_MAX to process the
 *                      entire string.
 * @param   pcch        Where to store the string length (in bytes). Optional.
 *                      This is undefined on failure.
 */
RTDECL(int) RTUtf16CalcLatin1LenEx(PCRTUTF16 pwsz, size_t cwc, size_t *pcch);

/**
 * Get the unicode code point at the given string position.
 *
 * @returns unicode code point.
 * @returns RTUNICP_INVALID if the encoding is invalid.
 * @param   pwsz        The string.
 *
 * @remark  This is an internal worker for RTUtf16GetCp().
 */
RTDECL(RTUNICP) RTUtf16GetCpInternal(PCRTUTF16 pwsz);

/**
 * Get the unicode code point at the given string position.
 *
 * @returns iprt status code.
 * @param   ppwsz       Pointer to the string pointer. This will be updated to
 *                      point to the char following the current code point.
 * @param   pCp         Where to store the code point.
 *                      RTUNICP_INVALID is stored here on failure.
 *
 * @remark  This is an internal worker for RTUtf16GetCpEx().
 */
RTDECL(int) RTUtf16GetCpExInternal(PCRTUTF16 *ppwsz, PRTUNICP pCp);

/**
 * Get the unicode code point at the given string position, big endian.
 *
 * @returns iprt status code.
 * @param   ppwsz       Pointer to the string pointer. This will be updated to
 *                      point to the char following the current code point.
 * @param   pCp         Where to store the code point.
 *                      RTUNICP_INVALID is stored here on failure.
 *
 * @remark  This is an internal worker for RTUtf16BigGetCpEx().
 */
RTDECL(int) RTUtf16BigGetCpExInternal(PCRTUTF16 *ppwsz, PRTUNICP pCp);

/**
 * Put the unicode code point at the given string position
 * and return the pointer to the char following it.
 *
 * This function will not consider anything at or following the
 * buffer area pointed to by pwsz. It is therefore not suitable for
 * inserting code points into a string, only appending/overwriting.
 *
 * @returns pointer to the char following the written code point.
 * @param   pwsz        The string.
 * @param   CodePoint   The code point to write.
 *                      This should not be RTUNICP_INVALID or any other
 *                      character out of the UTF-16 range.
 *
 * @remark  This is an internal worker for RTUtf16GetCpEx().
 */
RTDECL(PRTUTF16) RTUtf16PutCpInternal(PRTUTF16 pwsz, RTUNICP CodePoint);

/**
 * Get the unicode code point at the given string position.
 *
 * @returns unicode code point.
 * @returns RTUNICP_INVALID if the encoding is invalid.
 * @param   pwsz        The string.
 *
 * @remark  We optimize this operation by using an inline function for
 *          everything which isn't a surrogate pair or an endian indicator.
 */
DECLINLINE(RTUNICP) RTUtf16GetCp(PCRTUTF16 pwsz)
{
    const RTUTF16 wc = *pwsz;
    if (wc < 0xd800 || (wc > 0xdfff && wc < 0xfffe))
        return wc;
    return RTUtf16GetCpInternal(pwsz);
}

/**
 * Get the unicode code point at the given string position.
 *
 * @returns iprt status code.
 * @param   ppwsz       Pointer to the string pointer. This will be updated to
 *                      point to the char following the current code point.
 * @param   pCp         Where to store the code point.
 *                      RTUNICP_INVALID is stored here on failure.
 *
 * @remark  We optimize this operation by using an inline function for
 *          everything which isn't a surrogate pair or and endian indicator.
 */
DECLINLINE(int) RTUtf16GetCpEx(PCRTUTF16 *ppwsz, PRTUNICP pCp)
{
    const RTUTF16 wc = **ppwsz;
    if (wc < 0xd800 || (wc > 0xdfff && wc < 0xfffe))
    {
        (*ppwsz)++;
        *pCp = wc;
        return VINF_SUCCESS;
    }
    return RTUtf16GetCpExInternal(ppwsz, pCp);
}

/**
 * Get the unicode code point at the given string position, big endian version.
 *
 * @returns iprt status code.
 * @param   ppwsz       Pointer to the string pointer. This will be updated to
 *                      point to the char following the current code point.
 * @param   pCp         Where to store the code point.
 *                      RTUNICP_INVALID is stored here on failure.
 *
 * @remark  We optimize this operation by using an inline function for
 *          everything which isn't a surrogate pair or and endian indicator.
 */
DECLINLINE(int) RTUtf16BigGetCpEx(PCRTUTF16 *ppwsz, PRTUNICP pCp)
{
#ifdef RT_BIG_ENDIAN
    return RTUtf16GetCpEx(ppwsz, pCp);
#else
# ifdef ___iprt_asm_h
    const RTUTF16 wc = RT_BE2H_U16(**ppwsz);
    if (wc < 0xd800 || (wc > 0xdfff && wc < 0xfffe))
    {
        (*ppwsz)++;
        *pCp = wc;
        return VINF_SUCCESS;
    }
# endif
    return RTUtf16BigGetCpExInternal(ppwsz, pCp);
#endif
}

/**
 * Put the unicode code point at the given string position
 * and return the pointer to the char following it.
 *
 * This function will not consider anything at or following the
 * buffer area pointed to by pwsz. It is therefore not suitable for
 * inserting code points into a string, only appending/overwriting.
 *
 * @returns pointer to the char following the written code point.
 * @param   pwsz        The string.
 * @param   CodePoint   The code point to write.
 *                      This should not be RTUNICP_INVALID or any other
 *                      character out of the UTF-16 range.
 *
 * @remark  We optimize this operation by using an inline function for
 *          everything which isn't a surrogate pair or and endian indicator.
 */
DECLINLINE(PRTUTF16) RTUtf16PutCp(PRTUTF16 pwsz, RTUNICP CodePoint)
{
    if (CodePoint < 0xd800 || (CodePoint > 0xd800 && CodePoint < 0xfffe))
    {
        *pwsz++ = (RTUTF16)CodePoint;
        return pwsz;
    }
    return RTUtf16PutCpInternal(pwsz, CodePoint);
}

/**
 * Skips ahead, past the current code point.
 *
 * @returns Pointer to the char after the current code point.
 * @param   pwsz    Pointer to the current code point.
 * @remark  This will not move the next valid code point, only past the current one.
 */
DECLINLINE(PRTUTF16) RTUtf16NextCp(PCRTUTF16 pwsz)
{
    RTUNICP Cp;
    RTUtf16GetCpEx(&pwsz, &Cp);
    return (PRTUTF16)pwsz;
}

/**
 * Skips backwards, to the previous code point.
 *
 * @returns Pointer to the char after the current code point.
 * @param   pwszStart   Pointer to the start of the string.
 * @param   pwsz        Pointer to the current code point.
 */
RTDECL(PRTUTF16) RTUtf16PrevCp(PCRTUTF16 pwszStart, PCRTUTF16 pwsz);


/**
 * Checks if the UTF-16 char is the high surrogate char (i.e.
 * the 1st char in the pair).
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   wc      The character to investigate.
 */
DECLINLINE(bool) RTUtf16IsHighSurrogate(RTUTF16 wc)
{
    return wc >= 0xd800 && wc <= 0xdbff;
}

/**
 * Checks if the UTF-16 char is the low surrogate char (i.e.
 * the 2nd char in the pair).
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   wc      The character to investigate.
 */
DECLINLINE(bool) RTUtf16IsLowSurrogate(RTUTF16 wc)
{
    return wc >= 0xdc00 && wc <= 0xdfff;
}


/**
 * Checks if the two UTF-16 chars form a valid surrogate pair.
 *
 * @returns true if they do.
 * @returns false if they doesn't.
 * @param   wcHigh      The high (1st) character.
 * @param   wcLow       The low (2nd) character.
 */
DECLINLINE(bool) RTUtf16IsSurrogatePair(RTUTF16 wcHigh, RTUTF16 wcLow)
{
    return RTUtf16IsHighSurrogate(wcHigh)
        && RTUtf16IsLowSurrogate(wcLow);
}

/**
 * Formats a buffer stream as hex bytes.
 *
 * The default is no separating spaces or line breaks or anything.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_POINTER if any of the pointers are wrong.
 * @retval  VERR_BUFFER_OVERFLOW if the buffer is insufficent to hold the bytes.
 *
 * @param   pwszBuf     Output string buffer.
 * @param   cwcBuf      The size of the output buffer in RTUTF16 units.
 * @param   pv          Pointer to the bytes to stringify.
 * @param   cb          The number of bytes to stringify.
 * @param   fFlags      Combination of RTSTRPRINTHEXBYTES_F_XXX values.
 * @sa      RTStrPrintHexBytes.
 */
RTDECL(int) RTUtf16PrintHexBytes(PRTUTF16 pwszBuf, size_t cwcBuf, void const *pv, size_t cb, uint32_t fFlags);

/** @} */


RT_C_DECLS_END

/** @} */

#endif

