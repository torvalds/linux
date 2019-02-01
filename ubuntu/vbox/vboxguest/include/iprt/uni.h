/** @file
 * IPRT - Unicode Code Points.
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

#ifndef IPRT_INCLUDED_uni_h
#define IPRT_INCLUDED_uni_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @defgroup grp_rt_uni    RTUniCp - Unicode Code Points
 * @ingroup grp_rt
 * @{
 */

/** @def RTUNI_USE_WCTYPE
 * Define RTUNI_USE_WCTYPE to not use the IPRT unicode data but the
 * data which the C runtime library provides. */
#ifdef DOXYGEN_RUNNING
# define RTUNI_USE_WCTYPE
#endif

#include <iprt/types.h>
#ifdef RTUNI_USE_WCTYPE
# include <wctype.h>
#endif

RT_C_DECLS_BEGIN


#ifndef RTUNI_USE_WCTYPE

/**
 * A unicode flags range.
 * @internal
 */
typedef struct RTUNIFLAGSRANGE
{
    /** The first code point of the range. */
    RTUNICP         BeginCP;
    /** The last + 1 code point of the range. */
    RTUNICP         EndCP;
    /** Pointer to the array of case folded code points. */
    const uint8_t  *pafFlags;
} RTUNIFLAGSRANGE;
/** Pointer to a flags range.
 * @internal */
typedef RTUNIFLAGSRANGE *PRTUNIFLAGSRANGE;
/** Pointer to a const flags range.
 * @internal */
typedef const RTUNIFLAGSRANGE *PCRTUNIFLAGSRANGE;

/**
 * A unicode case folded range.
 * @internal
 */
typedef struct RTUNICASERANGE
{
    /** The first code point of the range. */
    RTUNICP         BeginCP;
    /** The last + 1 code point of the range. */
    RTUNICP         EndCP;
    /** Pointer to the array of case folded code points. */
    PCRTUNICP       paFoldedCPs;
} RTUNICASERANGE;
/** Pointer to a case folded range.
 * @internal */
typedef RTUNICASERANGE *PRTUNICASERANGE;
/** Pointer to a const case folded range.
 * @internal */
typedef const RTUNICASERANGE *PCRTUNICASERANGE;

/** @name Unicode Code Point Flags.
 * @internal
 * @{ */
#define RTUNI_UPPER         RT_BIT(0)
#define RTUNI_LOWER         RT_BIT(1)
#define RTUNI_ALPHA         RT_BIT(2)
#define RTUNI_XDIGIT        RT_BIT(3)
#define RTUNI_DDIGIT        RT_BIT(4)
#define RTUNI_WSPACE        RT_BIT(5)
/*#define RTUNI_BSPACE RT_BIT(6) - later */
/** When set, the codepoint requires further checking wrt NFC and NFD
 * normalization. I.e. set when either of QC_NFD and QC_NFC are not Y. */
#define RTUNI_QC_NFX        RT_BIT(7)
/** @} */


/**
 * Array of flags ranges.
 * @internal
 */
extern RTDATADECL(const RTUNIFLAGSRANGE) g_aRTUniFlagsRanges[];

/**
 * Gets the flags for a unicode code point.
 *
 * @returns The flag mask. (RTUNI_*)
 * @param   CodePoint       The unicode code point.
 * @internal
 */
DECLINLINE(RTUNICP) rtUniCpFlags(RTUNICP CodePoint)
{
    PCRTUNIFLAGSRANGE pCur = &g_aRTUniFlagsRanges[0];
    do
    {
        if (pCur->EndCP > CodePoint)
        {
            if (pCur->BeginCP <= CodePoint)
                return pCur->pafFlags[CodePoint - pCur->BeginCP];
            break;
        }
        pCur++;
    } while (pCur->EndCP != RTUNICP_MAX);
    return 0;
}


/**
 * Checks if a unicode code point is upper case.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsUpper(RTUNICP CodePoint)
{
    return (rtUniCpFlags(CodePoint) & RTUNI_UPPER) != 0;
}


/**
 * Checks if a unicode code point is lower case.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsLower(RTUNICP CodePoint)
{
    return (rtUniCpFlags(CodePoint) & RTUNI_LOWER) != 0;
}


/**
 * Checks if a unicode code point is case foldable.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsFoldable(RTUNICP CodePoint)
{
    /* Right enough. */
    return (rtUniCpFlags(CodePoint) & (RTUNI_LOWER | RTUNI_UPPER)) != 0;
}


/**
 * Checks if a unicode code point is alphabetic.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsAlphabetic(RTUNICP CodePoint)
{
    return (rtUniCpFlags(CodePoint) & RTUNI_ALPHA) != 0;
}


/**
 * Checks if a unicode code point is a decimal digit.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsDecDigit(RTUNICP CodePoint)
{
    return (rtUniCpFlags(CodePoint) & RTUNI_DDIGIT) != 0;
}


/**
 * Checks if a unicode code point is a hexadecimal digit.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsHexDigit(RTUNICP CodePoint)
{
    return (rtUniCpFlags(CodePoint) & RTUNI_XDIGIT) != 0;
}


/**
 * Checks if a unicode code point is white space.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsSpace(RTUNICP CodePoint)
{
    return (rtUniCpFlags(CodePoint) & RTUNI_WSPACE) != 0;
}



/**
 * Array of uppercase ranges.
 * @internal
 */
extern RTDATADECL(const RTUNICASERANGE) g_aRTUniUpperRanges[];

/**
 * Array of lowercase ranges.
 * @internal
 */
extern RTDATADECL(const RTUNICASERANGE) g_aRTUniLowerRanges[];


/**
 * Folds a unicode code point using the specified range array.
 *
 * @returns FOlded code point.
 * @param   CodePoint       The unicode code point to fold.
 * @param   pCur            The case folding range to use.
 */
DECLINLINE(RTUNICP) rtUniCpFold(RTUNICP CodePoint, PCRTUNICASERANGE pCur)
{
    do
    {
        if (pCur->EndCP > CodePoint)
        {
            if (pCur->BeginCP <= CodePoint)
                CodePoint = pCur->paFoldedCPs[CodePoint - pCur->BeginCP];
            break;
        }
        pCur++;
    } while (pCur->EndCP != RTUNICP_MAX);
    return CodePoint;
}


/**
 * Folds a unicode code point to upper case.
 *
 * @returns Folded code point.
 * @param   CodePoint       The unicode code point to fold.
 */
DECLINLINE(RTUNICP) RTUniCpToUpper(RTUNICP CodePoint)
{
    return rtUniCpFold(CodePoint, &g_aRTUniUpperRanges[0]);
}


/**
 * Folds a unicode code point to lower case.
 *
 * @returns Folded code point.
 * @param   CodePoint       The unicode code point to fold.
 */
DECLINLINE(RTUNICP) RTUniCpToLower(RTUNICP CodePoint)
{
    return rtUniCpFold(CodePoint, &g_aRTUniLowerRanges[0]);
}


#else /* RTUNI_USE_WCTYPE */


/**
 * Checks if a unicode code point is upper case.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsUpper(RTUNICP CodePoint)
{
    return !!iswupper(CodePoint);
}


/**
 * Checks if a unicode code point is lower case.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsLower(RTUNICP CodePoint)
{
    return !!iswlower(CodePoint);
}


/**
 * Checks if a unicode code point is case foldable.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsFoldable(RTUNICP CodePoint)
{
    /* Right enough. */
    return iswupper(CodePoint) || iswlower(CodePoint);
}


/**
 * Checks if a unicode code point is alphabetic.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsAlphabetic(RTUNICP CodePoint)
{
    return !!iswalpha(CodePoint);
}


/**
 * Checks if a unicode code point is a decimal digit.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsDecDigit(RTUNICP CodePoint)
{
    return !!iswdigit(CodePoint);
}


/**
 * Checks if a unicode code point is a hexadecimal digit.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsHexDigit(RTUNICP CodePoint)
{
    return !!iswxdigit(CodePoint);
}


/**
 * Checks if a unicode code point is white space.
 *
 * @returns true if it is.
 * @returns false if it isn't.
 * @param   CodePoint       The code point.
 */
DECLINLINE(bool) RTUniCpIsSpace(RTUNICP CodePoint)
{
    return !!iswspace(CodePoint);
}


/**
 * Folds a unicode code point to upper case.
 *
 * @returns Folded code point.
 * @param   CodePoint       The unicode code point to fold.
 */
DECLINLINE(RTUNICP) RTUniCpToUpper(RTUNICP CodePoint)
{
    return towupper(CodePoint);
}


/**
 * Folds a unicode code point to lower case.
 *
 * @returns Folded code point.
 * @param   CodePoint       The unicode code point to fold.
 */
DECLINLINE(RTUNICP) RTUniCpToLower(RTUNICP CodePoint)
{
    return towlower(CodePoint);
}


#endif /* RTUNI_USE_WCTYPE */


/**
 * Frees a unicode string.
 *
 * @param   pusz        The string to free.
 */
RTDECL(void) RTUniFree(PRTUNICP pusz);


/**
 * Checks if a code point valid.
 *
 * Any code point (defined or not) within the 17 unicode planes (0 thru 16),
 * except surrogates will be considered valid code points by this function.
 *
 * @returns true if in range, false if not.
 * @param   CodePoint       The unicode code point to validate.
 */
DECLINLINE(bool) RTUniCpIsValid(RTUNICP CodePoint)
{
    return CodePoint <= 0x00d7ff
        || (   CodePoint <= 0x10ffff
            && CodePoint >= 0x00e000);
}


/**
 * Checks if the given code point is in the BMP range.
 *
 * Surrogates are not considered in the BMP range by this function.
 *
 * @returns true if in BMP, false if not.
 * @param   CodePoint       The unicode code point to consider.
 */
DECLINLINE(bool) RTUniCpIsBMP(RTUNICP CodePoint)
{
    return CodePoint <= 0xd7ff
        || (   CodePoint <= 0xffff
            && CodePoint >= 0xe000);
}


/**
 * Folds a unicode code point to lower case.
 *
 * @returns Folded code point.
 * @param   CodePoint       The unicode code point to fold.
 */
DECLINLINE(size_t) RTUniCpCalcUtf8Len(RTUNICP CodePoint)
{
    if (CodePoint < 0x80)
        return 1;
    return 2
        + (CodePoint >= 0x00000800)
        + (CodePoint >= 0x00010000)
        + (CodePoint >= 0x00200000)
        + (CodePoint >= 0x04000000)
        + (CodePoint >= 0x80000000) /* illegal */;
}



RT_C_DECLS_END
/** @} */


#endif /* !IPRT_INCLUDED_uni_h */

