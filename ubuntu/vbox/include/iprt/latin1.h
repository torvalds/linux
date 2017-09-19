/** @file
 * IPRT - String Manipulation, Latin-1 (ISO-8859-1) encoding.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
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

#ifndef ___iprt_latin1_h
#define ___iprt_latin1_h

#include <iprt/string.h>

RT_C_DECLS_BEGIN


/** @defgroup rt_str_latin1     Latin-1 (ISO-8859-1) String Manipulation
 * @ingroup grp_rt_str
 *
 * Deals with Latin-1 encoded strings.
 *
 * @warning Make sure to name all variables dealing with Latin-1 strings
 *          suchthat there is no way to mistake them for normal UTF-8 strings.
 *          There may be severe security issues resulting from mistaking Latin-1
 *          for UTF-8!
 *
 * @{
 */

/**
 * Get the unicode code point at the given string position.
 *
 * @returns unicode code point.
 * @returns RTUNICP_INVALID if the encoding is invalid.
 * @param   pszLatin1   The Latin-1 string.
 */
DECLINLINE(RTUNICP) RTLatin1GetCp(const char *pszLatin1)
{
    return *(const unsigned char *)pszLatin1;
}

/**
 * Get the unicode code point at the given string position.
 *
 * @returns iprt status code.
 * @param   ppszLatin1  Pointer to the string pointer. This will be updated to
 *                      point to the char following the current code point. This
 *                      is advanced one character forward on failure.
 * @param   pCp         Where to store the code point. RTUNICP_INVALID is stored
 *                      here on failure.
 */
DECLINLINE(int) RTLatin1GetCpEx(const char **ppszLatin1, PRTUNICP pCp)
{
    const unsigned char uch = **(const unsigned char **)ppszLatin1;
    (*ppszLatin1)++;
    *pCp = uch;
    return VINF_SUCCESS;
}

/**
 * Get the unicode code point at the given string position for a string of a
 * given maximum length.
 *
 * @returns iprt status code.
 * @retval  VERR_END_OF_STRING if *pcch is 0. *pCp is set to RTUNICP_INVALID.
 *
 * @param   ppszLatin1  Pointer to the string pointer. This will be updated to
 *                      point to the char following the current code point.
 * @param   pcchLatin1  Pointer to the maximum string length.  This will be
 *                      decremented by the size of the code point found.
 * @param   pCp         Where to store the code point.
 *                      RTUNICP_INVALID is stored here on failure.
 */
DECLINLINE(int) RTLatin1GetCpNEx(const char **ppszLatin1, size_t *pcchLatin1, PRTUNICP pCp)
{
    if (RT_LIKELY(*pcchLatin1 != 0))
    {
        const unsigned char uch = **(const unsigned char **)ppszLatin1;
        (*ppszLatin1)++;
        (*pcchLatin1)--;
        *pCp = uch;
        return VINF_SUCCESS;
    }
    *pCp = RTUNICP_INVALID;
    return VERR_END_OF_STRING;
}

/**
 * Get the Latin-1 size in characters of a given Unicode code point.
 *
 * The code point is expected to be a valid Unicode one, but not necessarily in
 * the range supported by Latin-1.
 *
 * @returns the size in characters, or zero if there is no Latin-1 encoding
 */
DECLINLINE(size_t) RTLatin1CpSize(RTUNICP CodePoint)
{
    if (CodePoint < 0x100)
        return 1;
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
 * @param   pszLatin1   The string.
 * @param   CodePoint   The code point to write.
 *                      This should not be RTUNICP_INVALID or any other
 *                      character out of the Latin-1 range.
 */
DECLINLINE(char *) RTLatin1PutCp(char *pszLatin1, RTUNICP CodePoint)
{
    AssertReturn(CodePoint < 0x100, NULL);
    *pszLatin1++ = (unsigned char)CodePoint;
    return pszLatin1;
}

/**
 * Skips ahead, past the current code point.
 *
 * @returns Pointer to the char after the current code point.
 * @param   pszLatin1   Pointer to the current code point.
 * @remark  This will not move the next valid code point, only past the current one.
 */
DECLINLINE(char *) RTLatin1NextCp(const char *pszLatin1)
{
    pszLatin1++;
    return (char *)pszLatin1;
}

/**
 * Skips back to the previous code point.
 *
 * @returns Pointer to the char before the current code point.
 * @returns pszLatin1Start on failure.
 * @param   pszLatin1Start  Pointer to the start of the string.
 * @param   pszLatin1       Pointer to the current code point.
 */
DECLINLINE(char *) RTLatin1PrevCp(const char *pszLatin1Start, const char *pszLatin1)
{
    if ((uintptr_t)pszLatin1 > (uintptr_t)pszLatin1Start)
    {
        pszLatin1--;
        return (char *)pszLatin1;
    }
    return (char *)pszLatin1Start;
}

/**
 * Translate a Latin1 string into a UTF-8 allocating the result buffer (default
 * tag).
 *
 * @returns iprt status code.
 * @param   pszLatin1       Latin1 string to convert.
 * @param   ppszString      Receives pointer of allocated UTF-8 string on
 *                          success, and is always set to NULL on failure.
 *                          The returned pointer must be freed using RTStrFree().
 */
#define RTLatin1ToUtf8(pszLatin1, ppszString)       RTLatin1ToUtf8Tag((pszLatin1), (ppszString), RTSTR_TAG)

/**
 * Translate a Latin-1 string into a UTF-8 allocating the result buffer.
 *
 * @returns iprt status code.
 * @param   pszLatin1       Latin-1 string to convert.
 * @param   ppszString      Receives pointer of allocated UTF-8 string on
 *                          success, and is always set to NULL on failure.
 *                          The returned pointer must be freed using RTStrFree().
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int)  RTLatin1ToUtf8Tag(const char *pszLatin1, char **ppszString, const char *pszTag);

/**
 * Translates Latin-1 to UTF-8 using buffer provided by the caller or a fittingly
 * sized buffer allocated by the function (default tag).
 *
 * @returns iprt status code.
 * @param   pszLatin1       The Latin-1 string to convert.
 * @param   cchLatin1       The number of Latin-1 characters to translate from
 *                          pszLatin1. The translation will stop when reaching
 *                          cchLatin1 or the terminator ('\\0').  Use RTSTR_MAX
 *                          to translate the entire string.
 * @param   ppsz            If @a cch is non-zero, this must either be pointing
 *                          to a pointer to a buffer of the specified size, or
 *                          pointer to a NULL pointer.  If *ppsz is NULL or
 *                          @a cch is zero a buffer of at least @a cch chars
 *                          will be allocated to hold the translated string. If
 *                          a buffer was requested it must be freed using
 *                          RTStrFree().
 * @param   cch             The buffer size in chars (the type). This includes the terminator.
 * @param   pcch            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 */
#define RTLatin1ToUtf8Ex(pszLatin1, cchLatin1, ppsz, cch, pcch) \
    RTLatin1ToUtf8ExTag((pszLatin1), (cchLatin1), (ppsz), (cch), (pcch), RTSTR_TAG)

/**
 * Translates Latin1 to UTF-8 using buffer provided by the caller or a fittingly
 * sized buffer allocated by the function (custom tag).
 *
 * @returns iprt status code.
 * @param   pszLatin1       The Latin1 string to convert.
 * @param   cchLatin1       The number of Latin1 characters to translate from
 *                          pwszString.  The translation will stop when
 *                          reaching cchLatin1 or the terminator ('\\0').  Use
 *                          RTSTR_MAX to translate the entire string.
 * @param   ppsz            If cch is non-zero, this must either be pointing to
 *                          a pointer to a buffer of the specified size, or
 *                          pointer to a NULL pointer.  If *ppsz is NULL or cch
 *                          is zero a buffer of at least cch chars will be
 *                          allocated to hold the translated string.  If a
 *                          buffer was requested it must be freed using
 *                          RTStrFree().
 * @param   cch             The buffer size in chars (the type).  This includes
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
RTDECL(int)  RTLatin1ToUtf8ExTag(const char *pszLatin1, size_t cchLatin1, char **ppsz, size_t cch, size_t *pcch,
                                 const char *pszTag);

/**
 * Calculates the length of the Latin-1 string in UTF-8 chars (bytes).
 *
 * The primary purpose of this function is to help allocate buffers for
 * RTLatin1ToUtf8() of the correct size. For most other purposes
 * RTLatin1ToUtf8Ex() should be used.
 *
 * @returns Number of chars (bytes).
 * @returns 0 if the string was incorrectly encoded.
 * @param   pszLatin1     The Latin-1 string.
 */
RTDECL(size_t) RTLatin1CalcUtf8Len(const char *pszLatin1);

/**
 * Calculates the length of the Latin-1 string in UTF-8 chars (bytes).
 *
 * @returns iprt status code.
 * @param   pszLatin1   The Latin-1 string.
 * @param   cchLatin1   The max string length. Use RTSTR_MAX to process the
 *                      entire string.
 * @param   pcch        Where to store the string length (in bytes).  Optional.
 *                      This is undefined on failure.
 */
RTDECL(int) RTLatin1CalcUtf8LenEx(const char *pszLatin1, size_t cchLatin1, size_t *pcch);

/**
 * Calculates the length of the Latin-1 (ISO-8859-1) string in RTUTF16 items.
 *
 * @returns Number of RTUTF16 items.
 * @param   pszLatin1       The Latin-1 string.
 */
RTDECL(size_t) RTLatin1CalcUtf16Len(const char *pszLatin1);

/**
 * Calculates the length of the Latin-1 (ISO-8859-1) string in RTUTF16 items.
 *
 * @returns iprt status code.
 * @param   pszLatin1       The Latin-1 string.
 * @param   cchLatin1       The max string length. Use RTSTR_MAX to process the
 *                          entire string.
 * @param   pcwc            Where to store the string length. Optional.
 *                          This is undefined on failure.
 */
RTDECL(int) RTLatin1CalcUtf16LenEx(const char *pszLatin1, size_t cchLatin1, size_t *pcwc);

/**
 * Translate a Latin-1 (ISO-8859-1) string into a UTF-16 allocating the result
 * buffer (default tag).
 *
 * @returns iprt status code.
 * @param   pszLatin1       The Latin-1 string to convert.
 * @param   ppwszString     Receives pointer to the allocated UTF-16 string. The
 *                          returned string must be freed using RTUtf16Free().
 */
#define RTLatin1ToUtf16(pszLatin1, ppwszString)     RTLatin1ToUtf16Tag((pszLatin1), (ppwszString), RTSTR_TAG)

/**
 * Translate a Latin-1 (ISO-8859-1) string into a UTF-16 allocating the result
 * buffer (custom tag).
 *
 * @returns iprt status code.
 * @param   pszLatin1       The Latin-1 string to convert.
 * @param   ppwszString     Receives pointer to the allocated UTF-16 string. The
 *                          returned string must be freed using RTUtf16Free().
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int) RTLatin1ToUtf16Tag(const char *pszLatin1, PRTUTF16 *ppwszString, const char *pszTag);

/**
 * Translates pszLatin1 from Latin-1 (ISO-8859-1) to UTF-16, allocating the
 * result buffer if requested (default tag).
 *
 * @returns iprt status code.
 * @param   pszLatin1       The Latin-1 string to convert.
 * @param   cchLatin1       The maximum size in chars (the type) to convert. The
 *                          conversion stops when it reaches cchLatin1 or the
 *                          string terminator ('\\0'). Use RTSTR_MAX to
 *                          translate the entire string.
 * @param   ppwsz           If cwc is non-zero, this must either be pointing
 *                          to pointer to a buffer of the specified size, or
 *                          pointer to a NULL pointer.
 *                          If *ppwsz is NULL or cwc is zero a buffer of at
 *                          least cwc items will be allocated to hold the
 *                          translated string. If a buffer was requested it
 *                          must be freed using RTUtf16Free().
 * @param   cwc             The buffer size in RTUTF16s. This includes the
 *                          terminator.
 * @param   pcwc            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 */
#define RTLatin1ToUtf16Ex(pszLatin1, cchLatin1, ppwsz, cwc, pcwc) \
    RTLatin1ToUtf16ExTag((pszLatin1), (cchLatin1), (ppwsz), (cwc), (pcwc), RTSTR_TAG)

/**
 * Translates pszLatin1 from Latin-1 (ISO-8859-1) to UTF-16, allocating the
 * result buffer if requested.
 *
 * @returns iprt status code.
 * @param   pszLatin1       The Latin-1 string to convert.
 * @param   cchLatin1       The maximum size in chars (the type) to convert. The
 *                          conversion stops when it reaches cchLatin1 or the
 *                          string terminator ('\\0'). Use RTSTR_MAX to
 *                          translate the entire string.
 * @param   ppwsz           If cwc is non-zero, this must either be pointing
 *                          to pointer to a buffer of the specified size, or
 *                          pointer to a NULL pointer.
 *                          If *ppwsz is NULL or cwc is zero a buffer of at
 *                          least cwc items will be allocated to hold the
 *                          translated string. If a buffer was requested it
 *                          must be freed using RTUtf16Free().
 * @param   cwc             The buffer size in RTUTF16s. This includes the
 *                          terminator.
 * @param   pcwc            Where to store the length of the translated string,
 *                          excluding the terminator. (Optional)
 *
 *                          This may be set under some error conditions,
 *                          however, only for VERR_BUFFER_OVERFLOW and
 *                          VERR_NO_STR_MEMORY will it contain a valid string
 *                          length that can be used to resize the buffer.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTDECL(int) RTLatin1ToUtf16ExTag(const char *pszLatin1, size_t cchLatin1,
                                 PRTUTF16 *ppwsz, size_t cwc, size_t *pcwc, const char *pszTag);

/** @} */

RT_C_DECLS_END

/** @} */

#endif

