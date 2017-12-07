/** @file
 * IPRT - Status Codes.
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

#ifndef ___iprt_err_h
#define ___iprt_err_h

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/stdarg.h>


/** @defgroup grp_rt_err            RTErr - Status Codes
 * @ingroup grp_rt
 *
 * The IPRT status codes are in two ranges: {0..999} and {22000..32766}.  The
 * IPRT users are free to use the range {1000..21999}.  See RTERR_RANGE1_FIRST,
 * RTERR_RANGE1_LAST, RTERR_RANGE2_FIRST, RTERR_RANGE2_LAST, RTERR_USER_FIRST
 * and RTERR_USER_LAST.
 *
 * @{
 */

/** @defgroup grp_rt_err_hlp        Status Code Helpers
 * @{
 */

#ifdef __cplusplus
/**
 * Strict type validation class.
 *
 * This is only really useful for type checking the arguments to RT_SUCCESS,
 * RT_SUCCESS_NP, RT_FAILURE and RT_FAILURE_NP.  The RTErrStrictType2
 * constructor is for integration with external status code strictness regimes.
 */
class RTErrStrictType
{
protected:
    int32_t m_rc;

public:
    /**
     * Constructor for interaction with external status code strictness regimes.
     *
     * This is a special constructor for helping external return code validator
     * classes interact cleanly with RT_SUCCESS, RT_SUCCESS_NP, RT_FAILURE and
     * RT_FAILURE_NP while barring automatic cast to integer.
     *
     * @param   rcObj       IPRT status code object from an automatic cast.
     */
    RTErrStrictType(RTErrStrictType2 const rcObj)
        : m_rc(rcObj.getValue())
    {
    }

    /**
     * Integer constructor used by RT_SUCCESS_NP.
     *
     * @param   rc          IPRT style status code.
     */
    RTErrStrictType(int32_t rc)
        : m_rc(rc)
    {
    }

#if 0 /** @todo figure where int32_t is long instead of int. */
    /**
     * Integer constructor used by RT_SUCCESS_NP.
     *
     * @param   rc          IPRT style status code.
     */
    RTErrStrictType(signed int rc)
        : m_rc(rc)
    {
    }
#endif

    /**
     * Test for success.
     */
    bool success() const
    {
        return m_rc >= 0;
    }

private:
    /** @name Try ban a number of wrong types.
     * @{ */
    RTErrStrictType(uint8_t rc)         : m_rc(-999) { NOREF(rc); }
    RTErrStrictType(uint16_t rc)        : m_rc(-999) { NOREF(rc); }
    RTErrStrictType(uint32_t rc)        : m_rc(-999) { NOREF(rc); }
    RTErrStrictType(uint64_t rc)        : m_rc(-999) { NOREF(rc); }
    RTErrStrictType(int8_t rc)          : m_rc(-999) { NOREF(rc); }
    RTErrStrictType(int16_t rc)         : m_rc(-999) { NOREF(rc); }
    RTErrStrictType(int64_t rc)         : m_rc(-999) { NOREF(rc); }
    /** @todo fight long here - clashes with int32_t/int64_t on some platforms. */
    /** @} */
};
#endif /* __cplusplus */


/** @def RTERR_STRICT_RC
 * Indicates that RT_SUCCESS_NP, RT_SUCCESS, RT_FAILURE_NP and RT_FAILURE should
 * make type enforcing at compile time.
 *
 * @remarks     Only define this for C++ code.
 */
#if defined(__cplusplus) \
 && !defined(RTERR_STRICT_RC) \
 && (   defined(DOXYGEN_RUNNING) \
     || defined(DEBUG) \
     || defined(RT_STRICT) )
# define RTERR_STRICT_RC        1
#endif


/** @def RT_SUCCESS
 * Check for success. We expect success in normal cases, that is the code path depending on
 * this check is normally taken. To prevent any prediction use RT_SUCCESS_NP instead.
 *
 * @returns true if rc indicates success.
 * @returns false if rc indicates failure.
 *
 * @param   rc  The iprt status code to test.
 */
#define RT_SUCCESS(rc)      ( RT_LIKELY(RT_SUCCESS_NP(rc)) )

/** @def RT_SUCCESS_NP
 * Check for success. Don't predict the result.
 *
 * @returns true if rc indicates success.
 * @returns false if rc indicates failure.
 *
 * @param   rc  The iprt status code to test.
 */
#ifdef RTERR_STRICT_RC
# define RT_SUCCESS_NP(rc)   ( RTErrStrictType(rc).success() )
#else
# define RT_SUCCESS_NP(rc)   ( (int)(rc) >= VINF_SUCCESS )
#endif

/** @def RT_FAILURE
 * Check for failure, predicting unlikely.
 *
 * We don't expect in normal cases, that is the code path depending on this
 * check is normally NOT taken. To prevent any prediction use RT_FAILURE_NP
 * instead.
 *
 * @returns true if rc indicates failure.
 * @returns false if rc indicates success.
 *
 * @param   rc  The iprt status code to test.
 *
 * @remarks Please structure your code to use the RT_SUCCESS() macro instead of
 *          RT_FAILURE() where possible, as that gives us a better shot at good
 *          code with the windows compilers.
 */
#define RT_FAILURE(rc)      ( RT_UNLIKELY(!RT_SUCCESS_NP(rc)) )

/** @def RT_FAILURE_NP
 * Check for failure, no prediction.
 *
 * @returns true if rc indicates failure.
 * @returns false if rc indicates success.
 *
 * @param   rc  The iprt status code to test.
 */
#define RT_FAILURE_NP(rc)   ( !RT_SUCCESS_NP(rc) )

RT_C_DECLS_BEGIN

/**
 * Converts a Darwin HRESULT error to an iprt status code.
 *
 * @returns iprt status code.
 * @param   iNativeCode    HRESULT error code.
 * @remark  Darwin ring-3 only.
 */
RTDECL(int)  RTErrConvertFromDarwinCOM(int32_t iNativeCode);

/**
 * Converts a Darwin IOReturn error to an iprt status code.
 *
 * @returns iprt status code.
 * @param   iNativeCode    IOReturn error code.
 * @remark  Darwin only.
 */
RTDECL(int)  RTErrConvertFromDarwinIO(int iNativeCode);

/**
 * Converts a Darwin kern_return_t error to an iprt status code.
 *
 * @returns iprt status code.
 * @param   iNativeCode    kern_return_t error code.
 * @remark  Darwin only.
 */
RTDECL(int)  RTErrConvertFromDarwinKern(int iNativeCode);

/**
 * Converts a Darwin error to an iprt status code.
 *
 * This will consult RTErrConvertFromDarwinKern, RTErrConvertFromDarwinIO
 * and RTErrConvertFromDarwinCOM in this order. The latter is ring-3 only as it
 * doesn't apply elsewhere.
 *
 * @returns iprt status code.
 * @param   iNativeCode    Darwin error code.
 * @remarks Darwin only.
 * @remarks This is recommended over RTErrConvertFromDarwinKern and RTErrConvertFromDarwinIO
 *          since these are really just subsets of the same error space.
 */
RTDECL(int)  RTErrConvertFromDarwin(int iNativeCode);

/**
 * Converts errno to iprt status code.
 *
 * @returns iprt status code.
 * @param   uNativeCode    errno code.
 */
RTDECL(int)  RTErrConvertFromErrno(unsigned uNativeCode);

/**
 * Converts a L4 errno to a iprt status code.
 *
 * @returns iprt status code.
 * @param   uNativeCode l4 errno.
 * @remark  L4 only.
 */
RTDECL(int)  RTErrConvertFromL4Errno(unsigned uNativeCode);

/**
 * Converts NT status code to iprt status code.
 *
 * Needless to say, this is only available on NT and winXX targets.
 *
 * @returns iprt status code.
 * @param   lNativeCode    NT status code.
 * @remark  Windows only.
 */
RTDECL(int)  RTErrConvertFromNtStatus(long lNativeCode);

/**
 * Converts OS/2 error code to iprt status code.
 *
 * @returns iprt status code.
 * @param   uNativeCode    OS/2 error code.
 * @remark  OS/2 only.
 */
RTDECL(int)  RTErrConvertFromOS2(unsigned uNativeCode);

/**
 * Converts Win32 error code to iprt status code.
 *
 * @returns iprt status code.
 * @param   uNativeCode    Win32 error code.
 * @remark  Windows only.
 */
RTDECL(int)  RTErrConvertFromWin32(unsigned uNativeCode);

/**
 * Converts an iprt status code to a errno status code.
 *
 * @returns errno status code.
 * @param   iErr    iprt status code.
 */
RTDECL(int)  RTErrConvertToErrno(int iErr);

#ifdef IN_RING3

/**
 * iprt status code message.
 */
typedef struct RTSTATUSMSG
{
    /** Pointer to the short message string. */
    const char *pszMsgShort;
    /** Pointer to the full message string. */
    const char *pszMsgFull;
    /** Pointer to the define string. */
    const char *pszDefine;
    /** Status code number. */
    int         iCode;
} RTSTATUSMSG;
/** Pointer to iprt status code message. */
typedef RTSTATUSMSG *PRTSTATUSMSG;
/** Pointer to const iprt status code message. */
typedef const RTSTATUSMSG *PCRTSTATUSMSG;

/**
 * Get the message structure corresponding to a given iprt status code.
 *
 * @returns Pointer to read-only message description.
 * @param   rc      The status code.
 */
RTDECL(PCRTSTATUSMSG) RTErrGet(int rc);

/**
 * Get the define corresponding to a given iprt status code.
 *
 * @returns Pointer to read-only string with the \#define identifier.
 * @param   rc      The status code.
 */
#define RTErrGetDefine(rc)      (RTErrGet(rc)->pszDefine)

/**
 * Get the short description corresponding to a given iprt status code.
 *
 * @returns Pointer to read-only string with the description.
 * @param   rc      The status code.
 */
#define RTErrGetShort(rc)       (RTErrGet(rc)->pszMsgShort)

/**
 * Get the full description corresponding to a given iprt status code.
 *
 * @returns Pointer to read-only string with the description.
 * @param   rc      The status code.
 */
#define RTErrGetFull(rc)        (RTErrGet(rc)->pszMsgFull)

#ifdef RT_OS_WINDOWS
/**
 * Windows error code message.
 */
typedef struct RTWINERRMSG
{
    /** Pointer to the full message string. */
    const char *pszMsgFull;
    /** Pointer to the define string. */
    const char *pszDefine;
    /** Error code number. */
    long        iCode;
} RTWINERRMSG;
/** Pointer to Windows error code message. */
typedef RTWINERRMSG *PRTWINERRMSG;
/** Pointer to const Windows error code message. */
typedef const RTWINERRMSG *PCRTWINERRMSG;

/**
 * Get the message structure corresponding to a given Windows error code.
 *
 * @returns Pointer to read-only message description.
 * @param   rc      The status code.
 */
RTDECL(PCRTWINERRMSG) RTErrWinGet(long rc);

/** On windows COM errors are part of the Windows error database. */
typedef RTWINERRMSG RTCOMERRMSG;

#else  /* !RT_OS_WINDOWS */

/**
 * COM/XPCOM error code message.
 */
typedef struct RTCOMERRMSG
{
    /** Pointer to the full message string. */
    const char *pszMsgFull;
    /** Pointer to the define string. */
    const char *pszDefine;
    /** Error code number. */
    uint32_t    iCode;
} RTCOMERRMSG;
#endif /* !RT_OS_WINDOWS */
/** Pointer to a XPCOM/COM error code message. */
typedef RTCOMERRMSG *PRTCOMERRMSG;
/** Pointer to const a XPCOM/COM error code message. */
typedef const RTCOMERRMSG *PCRTCOMERRMSG;

/**
 * Get the message structure corresponding to a given COM/XPCOM error code.
 *
 * @returns Pointer to read-only message description.
 * @param   rc      The status code.
 */
RTDECL(PCRTCOMERRMSG) RTErrCOMGet(uint32_t rc);

#endif /* IN_RING3 */

/** @defgroup RTERRINFO_FLAGS_XXX   RTERRINFO::fFlags
 * @{ */
/** Custom structure (the default). */
#define RTERRINFO_FLAGS_T_CUSTOM    UINT32_C(0)
/** Static structure (RTERRINFOSTATIC). */
#define RTERRINFO_FLAGS_T_STATIC    UINT32_C(1)
/** Allocated structure (RTErrInfoAlloc). */
#define RTERRINFO_FLAGS_T_ALLOC     UINT32_C(2)
/** Reserved type. */
#define RTERRINFO_FLAGS_T_RESERVED  UINT32_C(3)
/** Type mask. */
#define RTERRINFO_FLAGS_T_MASK      UINT32_C(3)
/** Error info is set. */
#define RTERRINFO_FLAGS_SET         RT_BIT_32(2)
/** Fixed flags (magic). */
#define RTERRINFO_FLAGS_MAGIC       UINT32_C(0xbabe0000)
/** The bit mask for the magic value. */
#define RTERRINFO_FLAGS_MAGIC_MASK  UINT32_C(0xffff0000)
/** @} */

/**
 * Initializes an error info structure.
 *
 * @returns @a pErrInfo.
 * @param   pErrInfo            The error info structure to init.
 * @param   pszMsg              The message buffer.  Must be at least one byte.
 * @param   cbMsg               The size of the message buffer.
 */
DECLINLINE(PRTERRINFO) RTErrInfoInit(PRTERRINFO pErrInfo, char *pszMsg, size_t cbMsg)
{
    *pszMsg = '\0';

    pErrInfo->fFlags         = RTERRINFO_FLAGS_T_CUSTOM | RTERRINFO_FLAGS_MAGIC;
    pErrInfo->rc             = /*VINF_SUCCESS*/ 0;
    pErrInfo->pszMsg         = pszMsg;
    pErrInfo->cbMsg          = cbMsg;
    pErrInfo->apvReserved[0] = NULL;
    pErrInfo->apvReserved[1] = NULL;

    return pErrInfo;
}

/**
 * Initialize a static error info structure.
 *
 * @returns Pointer to the core error info structure.
 * @param   pStaticErrInfo      The static error info structure to init.
 */
DECLINLINE(PRTERRINFO) RTErrInfoInitStatic(PRTERRINFOSTATIC pStaticErrInfo)
{
    RTErrInfoInit(&pStaticErrInfo->Core, pStaticErrInfo->szMsg, sizeof(pStaticErrInfo->szMsg));
    pStaticErrInfo->Core.fFlags = RTERRINFO_FLAGS_T_STATIC | RTERRINFO_FLAGS_MAGIC;
    return &pStaticErrInfo->Core;
}

/**
 * Allocates a error info structure with a buffer at least the given size.
 *
 * @returns Pointer to an error info structure on success, NULL on failure.
 *
 * @param   cbMsg               The minimum message buffer size.  Use 0 to get
 *                              the default buffer size.
 */
RTDECL(PRTERRINFO)  RTErrInfoAlloc(size_t cbMsg);

/**
 * Same as RTErrInfoAlloc, except that an IPRT status code is returned.
 *
 * @returns IPRT status code.
 *
 * @param   cbMsg               The minimum message buffer size.  Use 0 to get
 *                              the default buffer size.
 * @param   ppErrInfo           Where to store the pointer to the allocated
 *                              error info structure on success.  This is
 *                              always set to NULL.
 */
RTDECL(int)         RTErrInfoAllocEx(size_t cbMsg, PRTERRINFO *ppErrInfo);

/**
 * Frees an error info structure allocated by RTErrInfoAlloc or
 * RTErrInfoAllocEx.
 *
 * @param   pErrInfo            The error info structure.
 */
RTDECL(void)        RTErrInfoFree(PRTERRINFO pErrInfo);

/**
 * Fills in the error info details.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   pszMsg              The error message string.
 */
RTDECL(int)         RTErrInfoSet(PRTERRINFO pErrInfo, int rc, const char *pszMsg);

/**
 * Fills in the error info details, with a sprintf style message.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   pszFormat           The format string.
 * @param   ...                 The format arguments.
 */
RTDECL(int)         RTErrInfoSetF(PRTERRINFO pErrInfo, int rc, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Fills in the error info details, with a vsprintf style message.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   pszFormat           The format string.
 * @param   va                  The format arguments.
 */
RTDECL(int)         RTErrInfoSetV(PRTERRINFO pErrInfo, int rc, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/**
 * Adds more error info details.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   pszMsg              The error message string to add.
 */
RTDECL(int)         RTErrInfoAdd(PRTERRINFO pErrInfo, int rc, const char *pszMsg);

/**
 * Adds more error info details, with a sprintf style message.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   pszFormat           The format string to add.
 * @param   ...                 The format arguments.
 */
RTDECL(int)         RTErrInfoAddF(PRTERRINFO pErrInfo, int rc, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);

/**
 * Adds more error info details, with a vsprintf style message.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   pszFormat           The format string to add.
 * @param   va                  The format arguments.
 */
RTDECL(int)         RTErrInfoAddV(PRTERRINFO pErrInfo, int rc, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);

/** @name RTERRINFO_LOG_F_XXX
 * @{ */
/** Both debug and release log.   */
#define RTERRINFO_LOG_F_RELEASE         RT_BIT_32(0)
/** @} */

/**
 * Fills in the error info details.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   iLogGroup           The logging group.
 * @param   fFlags              RTERRINFO_LOG_F_XXX.
 * @param   pszMsg              The error message string.
 */
RTDECL(int)         RTErrInfoLogAndSet(PRTERRINFO pErrInfo, int rc, uint32_t iLogGroup, uint32_t fFlags, const char *pszMsg);

/**
 * Fills in the error info details, with a sprintf style message.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   iLogGroup           The logging group.
 * @param   fFlags              RTERRINFO_LOG_F_XXX.
 * @param   pszFormat           The format string.
 * @param   ...                 The format arguments.
 */
RTDECL(int)         RTErrInfoLogAndSetF(PRTERRINFO pErrInfo, int rc, uint32_t iLogGroup, uint32_t fFlags, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(5, 6);

/**
 * Fills in the error info details, with a vsprintf style message.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   iLogGroup           The logging group.
 * @param   fFlags              RTERRINFO_LOG_F_XXX.
 * @param   pszFormat           The format string.
 * @param   va                  The format arguments.
 */
RTDECL(int)         RTErrInfoLogAndSetV(PRTERRINFO pErrInfo, int rc, uint32_t iLogGroup, uint32_t fFlags, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(5, 0);

/**
 * Adds more error info details.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   iLogGroup           The logging group.
 * @param   fFlags              RTERRINFO_LOG_F_XXX.
 * @param   pszMsg              The error message string to add.
 */
RTDECL(int)         RTErrInfoLogAndAdd(PRTERRINFO pErrInfo, int rc, uint32_t iLogGroup, uint32_t fFlags, const char *pszMsg);

/**
 * Adds more error info details, with a sprintf style message.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   iLogGroup           The logging group.
 * @param   fFlags              RTERRINFO_LOG_F_XXX.
 * @param   pszFormat           The format string to add.
 * @param   ...                 The format arguments.
 */
RTDECL(int)         RTErrInfoLogAndAddF(PRTERRINFO pErrInfo, int rc, uint32_t iLogGroup, uint32_t fFlags, const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(5, 6);

/**
 * Adds more error info details, with a vsprintf style message.
 *
 * @returns @a rc.
 *
 * @param   pErrInfo            The error info structure to fill in.
 * @param   rc                  The status code to return.
 * @param   iLogGroup           The logging group.
 * @param   fFlags              RTERRINFO_LOG_F_XXX.
 * @param   pszFormat           The format string to add.
 * @param   va                  The format arguments.
 */
RTDECL(int)         RTErrInfoLogAndAddV(PRTERRINFO pErrInfo, int rc, uint32_t iLogGroup, uint32_t fFlags, const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(5, 0);

/** @name Macros wrapping the RTErrInfoLog* functions.
 * @{ */
#define RTERRINFO_LOG_SET(  a_pErrInfo, a_rc, a_pszMsg)             RTErrInfoLogAndSet( a_pErrInfo, a_rc, LOG_GROUP, 0, a_pszMsg)
#define RTERRINFO_LOG_SET_V(a_pErrInfo, a_rc, a_pszMsg, a_va)       RTErrInfoLogAndSetV(a_pErrInfo, a_rc, LOG_GROUP, 0, a_pszMsg, a_va)
#define RTERRINFO_LOG_ADD(  a_pErrInfo, a_rc, a_pszMsg)             RTErrInfoLogAndAdd( a_pErrInfo, a_rc, LOG_GROUP, 0, a_pszMsg)
#define RTERRINFO_LOG_ADD_V(a_pErrInfo, a_rc, a_pszMsg, a_va)       RTErrInfoLogAndAddV(a_pErrInfo, a_rc, LOG_GROUP, 0, a_pszMsg, a_va)
#ifdef RT_COMPILER_SUPPORTS_VA_ARGS
# define RTERRINFO_LOG_ADD_F(a_pErrInfo, a_rc, ...)                 RTErrInfoLogAndAddF(a_pErrInfo, a_rc, LOG_GROUP, 0, __VA_ARGS__)
# define RTERRINFO_LOG_SET_F(a_pErrInfo, a_rc, ...)                 RTErrInfoLogAndSetF(a_pErrInfo, a_rc, LOG_GROUP, 0, __VA_ARGS__)
#endif

#define RTERRINFO_LOG_REL_SET(  a_pErrInfo, a_rc, a_pszMsg)         RTErrInfoLogAndSet( a_pErrInfo, a_rc, LOG_GROUP, RTERRINFO_LOG_F_RELEASE, a_pszMsg)
#define RTERRINFO_LOG_REL_SET_V(a_pErrInfo, a_rc, a_pszMsg, a_va)   RTErrInfoLogAndSetV(a_pErrInfo, a_rc, LOG_GROUP, RTERRINFO_LOG_F_RELEASE, a_pszMsg, a_va)
#define RTERRINFO_LOG_REL_ADD(  a_pErrInfo, a_rc, a_pszMsg)         RTErrInfoLogAndAdd( a_pErrInfo, a_rc, LOG_GROUP, RTERRINFO_LOG_F_RELEASE, a_pszMsg)
#define RTERRINFO_LOG_REL_ADD_V(a_pErrInfo, a_rc, a_pszMsg, a_va)   RTErrInfoLogAndAddV(a_pErrInfo, a_rc, LOG_GROUP, RTERRINFO_LOG_F_RELEASE, a_pszMsg, a_va)
#ifdef RT_COMPILER_SUPPORTS_VA_ARGS
# define RTERRINFO_LOG_REL_ADD_F(a_pErrInfo, a_rc, ...)             RTErrInfoLogAndAddF(a_pErrInfo, a_rc, LOG_GROUP, RTERRINFO_LOG_F_RELEASE, __VA_ARGS__)
# define RTERRINFO_LOG_REL_SET_F(a_pErrInfo, a_rc, ...)             RTErrInfoLogAndSetF(a_pErrInfo, a_rc, LOG_GROUP, RTERRINFO_LOG_F_RELEASE, __VA_ARGS__)
#else
# define RTERRINFO_LOG_REL_ADD_F                                    RTErrInfoSetF
# define RTERRINFO_LOG_REL_SET_F                                    RTErrInfoAddF
#endif
/** @} */


/**
 * Checks if the error info is set.
 *
 * @returns true if set, false if not.
 * @param   pErrInfo            The error info structure. NULL is OK.
 */
DECLINLINE(bool)    RTErrInfoIsSet(PCRTERRINFO pErrInfo)
{
    if (!pErrInfo)
        return false;
    return (pErrInfo->fFlags & (RTERRINFO_FLAGS_MAGIC_MASK | RTERRINFO_FLAGS_SET))
        == (RTERRINFO_FLAGS_MAGIC | RTERRINFO_FLAGS_SET);
}

/**
 * Clears the error info structure.
 *
 * @param   pErrInfo            The error info structure. NULL is OK.
 */
DECLINLINE(void)    RTErrInfoClear(PRTERRINFO pErrInfo)
{
    if (pErrInfo)
    {
        pErrInfo->fFlags &= ~RTERRINFO_FLAGS_SET;
        pErrInfo->rc      = /*VINF_SUCCESS*/0;
        *pErrInfo->pszMsg = '\0';
    }
}

/**
 * Storage for error variables.
 *
 * @remarks Do NOT touch the members!  They are platform specific and what's
 *          where may change at any time!
 */
typedef union RTERRVARS
{
    int8_t  ai8Vars[32];
    int16_t ai16Vars[16];
    int32_t ai32Vars[8];
    int64_t ai64Vars[4];
} RTERRVARS;
/** Pointer to an error variable storage union.  */
typedef RTERRVARS *PRTERRVARS;
/** Pointer to a const error variable storage union.  */
typedef RTERRVARS const *PCRTERRVARS;

/**
 * Saves the error variables.
 *
 * @returns @a pVars.
 * @param   pVars       The variable storage union.
 */
RTDECL(PRTERRVARS) RTErrVarsSave(PRTERRVARS pVars);

/**
 * Restores the error variables.
 *
 * @param   pVars       The variable storage union.
 */
RTDECL(void) RTErrVarsRestore(PCRTERRVARS pVars);

/**
 * Checks if the first variable set equals the second.
 *
 * @returns true if they are equal, false if not.
 * @param   pVars1      The first variable storage union.
 * @param   pVars2      The second variable storage union.
 */
RTDECL(bool) RTErrVarsAreEqual(PCRTERRVARS pVars1, PCRTERRVARS pVars2);

/**
 * Checks if the (live) error variables have changed since we saved them.
 *
 * @returns @c true if they have changed, @c false if not.
 * @param   pVars       The saved variables to compare the current state
 *                      against.
 */
RTDECL(bool) RTErrVarsHaveChanged(PCRTERRVARS pVars);

RT_C_DECLS_END

/** @} */

/** @name Status Code Ranges
 * @{ */
/** The first status code in the primary IPRT range. */
#define RTERR_RANGE1_FIRST                  0
/** The last status code in the primary IPRT range. */
#define RTERR_RANGE1_LAST                   999

/** The first status code in the secondary IPRT range. */
#define RTERR_RANGE2_FIRST                  22000
/** The last status code in the secondary IPRT range. */
#define RTERR_RANGE2_LAST                   32766

/** The first status code in the user range. */
#define RTERR_USER_FIRST                    1000
/** The last status code in the user range. */
#define RTERR_USER_LAST                     21999
/** @}  */


/* SED-START */

/** @name Misc. Status Codes
 * @{
 */
/** Success. */
#define VINF_SUCCESS                        0

/** General failure - DON'T USE THIS!!! */
#define VERR_GENERAL_FAILURE                (-1)
/** Invalid parameter. */
#define VERR_INVALID_PARAMETER              (-2)
/** Invalid parameter. */
#define VWRN_INVALID_PARAMETER              2
/** Invalid magic or cookie. */
#define VERR_INVALID_MAGIC                  (-3)
/** Invalid magic or cookie. */
#define VWRN_INVALID_MAGIC                  3
/** Invalid loader handle. */
#define VERR_INVALID_HANDLE                 (-4)
/** Invalid loader handle. */
#define VWRN_INVALID_HANDLE                 4
/** Failed to lock the address range. */
#define VERR_LOCK_FAILED                    (-5)
/** Invalid memory pointer. */
#define VERR_INVALID_POINTER                (-6)
/** Failed to patch the IDT. */
#define VERR_IDT_FAILED                     (-7)
/** Memory allocation failed. */
#define VERR_NO_MEMORY                      (-8)
/** Already loaded. */
#define VERR_ALREADY_LOADED                 (-9)
/** Permission denied. */
#define VERR_PERMISSION_DENIED              (-10)
/** Permission denied. */
#define VINF_PERMISSION_DENIED              10
/** Version mismatch. */
#define VERR_VERSION_MISMATCH               (-11)
/** The request function is not implemented. */
#define VERR_NOT_IMPLEMENTED                (-12)
/** Invalid flags was given. */
#define VERR_INVALID_FLAGS                  (-13)

/** Not equal. */
#define VERR_NOT_EQUAL                      (-18)
/** The specified path does not point at a symbolic link. */
#define VERR_NOT_SYMLINK                    (-19)
/** Failed to allocate temporary memory. */
#define VERR_NO_TMP_MEMORY                  (-20)
/** Invalid file mode mask (RTFMODE). */
#define VERR_INVALID_FMODE                  (-21)
/** Incorrect call order. */
#define VERR_WRONG_ORDER                    (-22)
/** There is no TLS (thread local storage) available for storing the current thread. */
#define VERR_NO_TLS_FOR_SELF                (-23)
/** Failed to set the TLS (thread local storage) entry which points to our thread structure. */
#define VERR_FAILED_TO_SET_SELF_TLS         (-24)
/** Not able to allocate contiguous memory. */
#define VERR_NO_CONT_MEMORY                 (-26)
/** No memory available for page table or page directory. */
#define VERR_NO_PAGE_MEMORY                 (-27)
/** Already initialized. */
#define VINF_ALREADY_INITIALIZED            28
/** The specified thread is dead. */
#define VERR_THREAD_IS_DEAD                 (-29)
/** The specified thread is not waitable. */
#define VERR_THREAD_NOT_WAITABLE            (-30)
/** Pagetable not present. */
#define VERR_PAGE_TABLE_NOT_PRESENT         (-31)
/** Invalid context.
 * Typically an API was used by the wrong thread. */
#define VERR_INVALID_CONTEXT                (-32)
/** The per process timer is busy. */
#define VERR_TIMER_BUSY                     (-33)
/** Address conflict. */
#define VERR_ADDRESS_CONFLICT               (-34)
/** Unresolved (unknown) host platform error. */
#define VERR_UNRESOLVED_ERROR               (-35)
/** Invalid function. */
#define VERR_INVALID_FUNCTION               (-36)
/** Not supported. */
#define VERR_NOT_SUPPORTED                  (-37)
/** Not supported. */
#define VINF_NOT_SUPPORTED                  37
/** Access denied. */
#define VERR_ACCESS_DENIED                  (-38)
/** Call interrupted. */
#define VERR_INTERRUPTED                    (-39)
/** Call interrupted. */
#define VINF_INTERRUPTED                    39
/** Timeout. */
#define VERR_TIMEOUT                        (-40)
/** Timeout. */
#define VINF_TIMEOUT                        40
/** Buffer too small to save result. */
#define VERR_BUFFER_OVERFLOW                (-41)
/** Buffer too small to save result. */
#define VINF_BUFFER_OVERFLOW                41
/** Data size overflow. */
#define VERR_TOO_MUCH_DATA                  (-42)
/** Max threads number reached. */
#define VERR_MAX_THRDS_REACHED              (-43)
/** Max process number reached. */
#define VERR_MAX_PROCS_REACHED              (-44)
/** The recipient process has refused the signal. */
#define VERR_SIGNAL_REFUSED                 (-45)
/** A signal is already pending. */
#define VERR_SIGNAL_PENDING                 (-46)
/** The signal being posted is not correct. */
#define VERR_SIGNAL_INVALID                 (-47)
/** The state changed.
 * This is a generic error message and needs a context to make sense. */
#define VERR_STATE_CHANGED                  (-48)
/** Warning, the state changed.
 * This is a generic error message and needs a context to make sense. */
#define VWRN_STATE_CHANGED                  48
/** Error while parsing UUID string */
#define VERR_INVALID_UUID_FORMAT            (-49)
/** The specified process was not found. */
#define VERR_PROCESS_NOT_FOUND              (-50)
/** The process specified to a non-block wait had not exited. */
#define VERR_PROCESS_RUNNING                (-51)
/** Retry the operation. */
#define VERR_TRY_AGAIN                      (-52)
/** Retry the operation. */
#define VINF_TRY_AGAIN                      52
/** Generic parse error. */
#define VERR_PARSE_ERROR                    (-53)
/** Value out of range. */
#define VERR_OUT_OF_RANGE                   (-54)
/** A numeric conversion encountered a value which was too big for the target. */
#define VERR_NUMBER_TOO_BIG                 (-55)
/** A numeric conversion encountered a value which was too big for the target. */
#define VWRN_NUMBER_TOO_BIG                 55
/** The number begin converted (string) contained no digits. */
#define VERR_NO_DIGITS                      (-56)
/** The number begin converted (string) contained no digits. */
#define VWRN_NO_DIGITS                      56
/** Encountered a '-' during conversion to an unsigned value. */
#define VERR_NEGATIVE_UNSIGNED              (-57)
/** Encountered a '-' during conversion to an unsigned value. */
#define VWRN_NEGATIVE_UNSIGNED              57
/** Error while characters translation (unicode and so). */
#define VERR_NO_TRANSLATION                 (-58)
/** Error while characters translation (unicode and so). */
#define VWRN_NO_TRANSLATION                 58
/** Encountered unicode code point which is reserved for use as endian indicator (0xffff or 0xfffe). */
#define VERR_CODE_POINT_ENDIAN_INDICATOR    (-59)
/** Encountered unicode code point in the surrogate range (0xd800 to 0xdfff). */
#define VERR_CODE_POINT_SURROGATE           (-60)
/** A string claiming to be UTF-8 is incorrectly encoded. */
#define VERR_INVALID_UTF8_ENCODING          (-61)
/** A string claiming to be in UTF-16 is incorrectly encoded. */
#define VERR_INVALID_UTF16_ENCODING         (-62)
/** Encountered a unicode code point which cannot be represented as UTF-16. */
#define VERR_CANT_RECODE_AS_UTF16           (-63)
/** Got an out of memory condition trying to allocate a string. */
#define VERR_NO_STR_MEMORY                  (-64)
/** Got an out of memory condition trying to allocate a UTF-16 (/UCS-2) string. */
#define VERR_NO_UTF16_MEMORY                (-65)
/** Get an out of memory condition trying to allocate a code point array. */
#define VERR_NO_CODE_POINT_MEMORY           (-66)
/** Can't free the memory because it's used in mapping. */
#define VERR_MEMORY_BUSY                    (-67)
/** The timer can't be started because it's already active. */
#define VERR_TIMER_ACTIVE                   (-68)
/** The timer can't be stopped because it's already suspended. */
#define VERR_TIMER_SUSPENDED                (-69)
/** The operation was cancelled by the user (copy) or another thread (local ipc). */
#define VERR_CANCELLED                      (-70)
/** Failed to initialize a memory object.
 * Exactly what this means is OS specific. */
#define VERR_MEMOBJ_INIT_FAILED             (-71)
/** Out of memory condition when allocating memory with low physical backing. */
#define VERR_NO_LOW_MEMORY                  (-72)
/** Out of memory condition when allocating physical memory (without mapping). */
#define VERR_NO_PHYS_MEMORY                 (-73)
/** The address (virtual or physical) is too big. */
#define VERR_ADDRESS_TOO_BIG                (-74)
/** Failed to map a memory object. */
#define VERR_MAP_FAILED                     (-75)
/** Trailing characters. */
#define VERR_TRAILING_CHARS                 (-76)
/** Trailing characters. */
#define VWRN_TRAILING_CHARS                 76
/** Trailing spaces. */
#define VERR_TRAILING_SPACES                (-77)
/** Trailing spaces. */
#define VWRN_TRAILING_SPACES                77
/** Generic not found error. */
#define VERR_NOT_FOUND                      (-78)
/** Generic not found warning. */
#define VWRN_NOT_FOUND                      78
/** Generic invalid state error. */
#define VERR_INVALID_STATE                  (-79)
/** Generic invalid state warning. */
#define VWRN_INVALID_STATE                  79
/** Generic out of resources error. */
#define VERR_OUT_OF_RESOURCES               (-80)
/** Generic out of resources warning. */
#define VWRN_OUT_OF_RESOURCES               80
/** No more handles available, too many open handles. */
#define VERR_NO_MORE_HANDLES                (-81)
/** Preemption is disabled.
 * The requested operation can only be performed when preemption is enabled. */
#define VERR_PREEMPT_DISABLED               (-82)
/** End of string. */
#define VERR_END_OF_STRING                  (-83)
/** End of string. */
#define VINF_END_OF_STRING                  83
/** A page count is out of range. */
#define VERR_PAGE_COUNT_OUT_OF_RANGE        (-84)
/** Generic object destroyed status. */
#define VERR_OBJECT_DESTROYED               (-85)
/** Generic object was destroyed by the call status. */
#define VINF_OBJECT_DESTROYED               85
/** Generic dangling objects status. */
#define VERR_DANGLING_OBJECTS               (-86)
/** Generic dangling objects status. */
#define VWRN_DANGLING_OBJECTS               86
/** Invalid Base64 encoding. */
#define VERR_INVALID_BASE64_ENCODING        (-87)
/** Return instigated by a callback or similar. */
#define VERR_CALLBACK_RETURN                (-88)
/** Return instigated by a callback or similar. */
#define VINF_CALLBACK_RETURN                88
/** Authentication failure. */
#define VERR_AUTHENTICATION_FAILURE         (-89)
/** Not a power of two. */
#define VERR_NOT_POWER_OF_TWO               (-90)
/** Status code, typically given as a parameter, that isn't supposed to be used. */
#define VERR_IGNORED                        (-91)
/** Concurrent access to the object is not allowed. */
#define VERR_CONCURRENT_ACCESS              (-92)
/** The caller does not have a reference to the object.
 * This status is used when two threads is caught sharing the same object
 * reference. */
#define VERR_CALLER_NO_REFERENCE            (-93)
/** Generic no change error. */
#define VERR_NO_CHANGE                      (-95)
/** Generic no change info. */
#define VINF_NO_CHANGE                      95
/** Out of memory condition when allocating executable memory. */
#define VERR_NO_EXEC_MEMORY                 (-96)
/** The alignment is not supported. */
#define VERR_UNSUPPORTED_ALIGNMENT          (-97)
/** The alignment is not really supported, however we got lucky with this
 * allocation. */
#define VINF_UNSUPPORTED_ALIGNMENT          97
/** Duplicate something. */
#define VERR_DUPLICATE                      (-98)
/** Something is missing. */
#define VERR_MISSING                        (-99)
/** An unexpected (/unknown) exception was caught. */
#define VERR_UNEXPECTED_EXCEPTION           (-22400)
/** Buffer underflow. */
#define VERR_BUFFER_UNDERFLOW               (-22401)
/** Buffer underflow. */
#define VINF_BUFFER_UNDERFLOW               22401
/** Uneven input. */
#define VERR_UNEVEN_INPUT                   (-22402)
/** Something is not available or not working properly. */
#define VERR_NOT_AVAILABLE                  (-22403)
/** The RTPROC_FLAGS_DETACHED flag isn't supported. */
#define VERR_PROC_DETACH_NOT_SUPPORTED      (-22404)
/** An account is restricted in a certain way. */
#define VERR_ACCOUNT_RESTRICTED             (-22405)
/** An account is restricted in a certain way. */
#define VINF_ACCOUNT_RESTRICTED             22405
/** Not able satisfy all the requirements of the request. */
#define VERR_UNABLE_TO_SATISFY_REQUIREMENTS (-22406)
/** Not able satisfy all the requirements of the request. */
#define VWRN_UNABLE_TO_SATISFY_REQUIREMENTS 22406
/** The requested allocation is too big. */
#define VERR_ALLOCATION_TOO_BIG             (-22407)
/** Mismatch. */
#define VERR_MISMATCH                       (-22408)
/** Wrong type. */
#define VERR_WRONG_TYPE                     (-22409)
/** This indicates that the process does not have sufficient privileges to
 * perform the operation. */
#define VERR_PRIVILEGE_NOT_HELD             (-22410)
/** Process does not have the trusted code base (TCB) privilege needed for user
 * authentication or/and process creation as a given user.  TCB is also called
 * 'Act as part of the operating system'. */
#define VERR_PROC_TCB_PRIV_NOT_HELD         (-22411)
/** Process does not have the assign primary token (APT) privilege needed
 * for creating process as a given user.  APT is also called 'Replace a process
 * level token'. */
#define VERR_PROC_APT_PRIV_NOT_HELD         (-22412)
/** Process does not have the increase quota (IQ) privilege needed for
 * creating a process as a given user. IQ is also called 'Increase quotas'. */
#define VERR_PROC_IQ_PRIV_NOT_HELD          (-22413)
/** The system has too many CPUs. */
#define VERR_MP_TOO_MANY_CPUS               (-22414)
/** @} */


/** @name Common File/Disk/Pipe/etc Status Codes
 * @{
 */
/** Unresolved (unknown) file i/o error. */
#define VERR_FILE_IO_ERROR                  (-100)
/** File/Device open failed. */
#define VERR_OPEN_FAILED                    (-101)
/** File not found. */
#define VERR_FILE_NOT_FOUND                 (-102)
/** Path not found. */
#define VERR_PATH_NOT_FOUND                 (-103)
/** Invalid (malformed) file/path name. */
#define VERR_INVALID_NAME                   (-104)
/** The object in question already exists. */
#define VERR_ALREADY_EXISTS                 (-105)
/** The object in question already exists. */
#define VWRN_ALREADY_EXISTS                 105
/** Too many open files. */
#define VERR_TOO_MANY_OPEN_FILES            (-106)
/** Seek error. */
#define VERR_SEEK                           (-107)
/** Seek below file start. */
#define VERR_NEGATIVE_SEEK                  (-108)
/** Trying to seek on device. */
#define VERR_SEEK_ON_DEVICE                 (-109)
/** Reached the end of the file. */
#define VERR_EOF                            (-110)
/** Reached the end of the file. */
#define VINF_EOF                            110
/** Generic file read error. */
#define VERR_READ_ERROR                     (-111)
/** Generic file write error. */
#define VERR_WRITE_ERROR                    (-112)
/** Write protect error. */
#define VERR_WRITE_PROTECT                  (-113)
/** Sharing violation, file is being used by another process. */
#define VERR_SHARING_VIOLATION              (-114)
/** Unable to lock a region of a file. */
#define VERR_FILE_LOCK_FAILED               (-115)
/** File access error, another process has locked a portion of the file. */
#define VERR_FILE_LOCK_VIOLATION            (-116)
/** File or directory can't be created. */
#define VERR_CANT_CREATE                    (-117)
/** Directory can't be deleted. */
#define VERR_CANT_DELETE_DIRECTORY          (-118)
/** Can't move file to another disk. */
#define VERR_NOT_SAME_DEVICE                (-119)
/** The filename or extension is too long. */
#define VERR_FILENAME_TOO_LONG              (-120)
/** Media not present in drive. */
#define VERR_MEDIA_NOT_PRESENT              (-121)
/** The type of media was not recognized. Not formatted? */
#define VERR_MEDIA_NOT_RECOGNIZED           (-122)
/** Can't unlock - region was not locked. */
#define VERR_FILE_NOT_LOCKED                (-123)
/** Unrecoverable error: lock was lost. */
#define VERR_FILE_LOCK_LOST                 (-124)
/** Can't delete directory with files. */
#define VERR_DIR_NOT_EMPTY                  (-125)
/** A directory operation was attempted on a non-directory object. */
#define VERR_NOT_A_DIRECTORY                (-126)
/** A non-directory operation was attempted on a directory object. */
#define VERR_IS_A_DIRECTORY                 (-127)
/** Tried to grow a file beyond the limit imposed by the process or the filesystem. */
#define VERR_FILE_TOO_BIG                   (-128)
/** No pending request the aio context has to wait for completion. */
#define VERR_FILE_AIO_NO_REQUEST            (-129)
/** The request could not be canceled or prepared for another transfer
 *  because it is still in progress. */
#define VERR_FILE_AIO_IN_PROGRESS           (-130)
/** The request could not be canceled because it already completed. */
#define VERR_FILE_AIO_COMPLETED             (-131)
/** The I/O context couldn't be destroyed because there are still pending requests. */
#define VERR_FILE_AIO_BUSY                  (-132)
/** The requests couldn't be submitted because that would exceed the capacity of the context. */
#define VERR_FILE_AIO_LIMIT_EXCEEDED        (-133)
/** The request was canceled. */
#define VERR_FILE_AIO_CANCELED              (-134)
/** The request wasn't submitted so it can't be canceled. */
#define VERR_FILE_AIO_NOT_SUBMITTED         (-135)
/** A request was not prepared and thus could not be submitted. */
#define VERR_FILE_AIO_NOT_PREPARED          (-136)
/** Not all requests could be submitted due to resource shortage. */
#define VERR_FILE_AIO_INSUFFICIENT_RESSOURCES (-137)
/** Device or resource is busy. */
#define VERR_RESOURCE_BUSY                  (-138)
/** A file operation was attempted on a non-file object. */
#define VERR_NOT_A_FILE                     (-139)
/** A non-file operation was attempted on a file object. */
#define VERR_IS_A_FILE                      (-140)
/** Unexpected filesystem object type. */
#define VERR_UNEXPECTED_FS_OBJ_TYPE         (-141)
/** A path does not start with a root specification. */
#define VERR_PATH_DOES_NOT_START_WITH_ROOT  (-142)
/** A path is relative, expected an absolute path. */
#define VERR_PATH_IS_RELATIVE               (-143)
/** A path is not relative (start with root), expected an relative path. */
#define VERR_PATH_IS_NOT_RELATIVE           (-144)
/** Zero length path. */
#define VERR_PATH_ZERO_LENGTH               (-145)
/** There are not enough events available on the host to create the I/O context.
 * This exact meaning is host platform dependent. */
#define VERR_FILE_AIO_INSUFFICIENT_EVENTS   (-146)
/** @} */


/** @name Generic Filesystem I/O Status Codes
 * @{
 */
/** Unresolved (unknown) disk i/o error.  */
#define VERR_DISK_IO_ERROR                  (-150)
/** Invalid drive number. */
#define VERR_INVALID_DRIVE                  (-151)
/** Disk is full. */
#define VERR_DISK_FULL                      (-152)
/** Disk was changed. */
#define VERR_DISK_CHANGE                    (-153)
/** Drive is locked. */
#define VERR_DRIVE_LOCKED                   (-154)
/** The specified disk or diskette cannot be accessed. */
#define VERR_DISK_INVALID_FORMAT            (-155)
/** Too many symbolic links. */
#define VERR_TOO_MANY_SYMLINKS              (-156)
/** The OS does not support setting the time stamps on a symbolic link. */
#define VERR_NS_SYMLINK_SET_TIME            (-157)
/** The OS does not support changing the owner of a symbolic link. */
#define VERR_NS_SYMLINK_CHANGE_OWNER        (-158)
/** Symbolic link not allowed. */
#define VERR_SYMLINK_NOT_ALLOWED            (-159)
/** @} */


/** @name Generic Directory Enumeration Status Codes
 * @{
 */
/** Unresolved (unknown) search error. */
#define VERR_SEARCH_ERROR                   (-200)
/** No more files found. */
#define VERR_NO_MORE_FILES                  (-201)
/** No more search handles available. */
#define VERR_NO_MORE_SEARCH_HANDLES         (-202)
/** RTDirReadEx() failed to retrieve the extra data which was requested. */
#define VWRN_NO_DIRENT_INFO                 203
/** @} */


/** @name Internal Processing Errors
 * @{
 */
/** Internal error - this should never happen.  */
#define VERR_INTERNAL_ERROR                 (-225)
/** Internal error no. 2. */
#define VERR_INTERNAL_ERROR_2               (-226)
/** Internal error no. 3. */
#define VERR_INTERNAL_ERROR_3               (-227)
/** Internal error no. 4. */
#define VERR_INTERNAL_ERROR_4               (-228)
/** Internal error no. 5. */
#define VERR_INTERNAL_ERROR_5               (-229)
/** Internal error: Unexpected status code. */
#define VERR_IPE_UNEXPECTED_STATUS          (-230)
/** Internal error: Unexpected status code. */
#define VERR_IPE_UNEXPECTED_INFO_STATUS     (-231)
/** Internal error: Unexpected status code. */
#define VERR_IPE_UNEXPECTED_ERROR_STATUS    (-232)
/** Internal error: Uninitialized status code.
 * @remarks This is used by value elsewhere.  */
#define VERR_IPE_UNINITIALIZED_STATUS       (-233)
/** Internal error: Supposedly unreachable default case in a switch. */
#define VERR_IPE_NOT_REACHED_DEFAULT_CASE   (-234)
/** @} */


/** @name Generic Device I/O Status Codes
 * @{
 */
/** Unresolved (unknown) device i/o error. */
#define VERR_DEV_IO_ERROR                   (-250)
/** Device i/o: Bad unit. */
#define VERR_IO_BAD_UNIT                    (-251)
/** Device i/o: Not ready. */
#define VERR_IO_NOT_READY                   (-252)
/** Device i/o: Bad command. */
#define VERR_IO_BAD_COMMAND                 (-253)
/** Device i/o: CRC error. */
#define VERR_IO_CRC                         (-254)
/** Device i/o: Bad length. */
#define VERR_IO_BAD_LENGTH                  (-255)
/** Device i/o: Sector not found. */
#define VERR_IO_SECTOR_NOT_FOUND            (-256)
/** Device i/o: General failure. */
#define VERR_IO_GEN_FAILURE                 (-257)
/** @} */


/** @name Generic Pipe I/O Status Codes
 * @{
 */
/** Unresolved (unknown) pipe i/o error. */
#define VERR_PIPE_IO_ERROR                  (-300)
/** Broken pipe. */
#define VERR_BROKEN_PIPE                    (-301)
/** Bad pipe. */
#define VERR_BAD_PIPE                       (-302)
/** Pipe is busy. */
#define VERR_PIPE_BUSY                      (-303)
/** No data in pipe. */
#define VERR_NO_DATA                        (-304)
/** Pipe is not connected. */
#define VERR_PIPE_NOT_CONNECTED             (-305)
/** More data available in pipe. */
#define VERR_MORE_DATA                      (-306)
/** Expected read pipe, got a write pipe instead. */
#define VERR_PIPE_NOT_READ                  (-307)
/** Expected write pipe, got a read pipe instead. */
#define VERR_PIPE_NOT_WRITE                 (-308)
/** @} */


/** @name Generic Semaphores Status Codes
 * @{
 */
/** Unresolved (unknown) semaphore error. */
#define VERR_SEM_ERROR                      (-350)
/** Too many semaphores. */
#define VERR_TOO_MANY_SEMAPHORES            (-351)
/** Exclusive semaphore is owned by another process. */
#define VERR_EXCL_SEM_ALREADY_OWNED         (-352)
/** The semaphore is set and cannot be closed. */
#define VERR_SEM_IS_SET                     (-353)
/** The semaphore cannot be set again. */
#define VERR_TOO_MANY_SEM_REQUESTS          (-354)
/** Attempt to release mutex not owned by caller. */
#define VERR_NOT_OWNER                      (-355)
/** The semaphore has been opened too many times. */
#define VERR_TOO_MANY_OPENS                 (-356)
/** The maximum posts for the event semaphore has been reached. */
#define VERR_TOO_MANY_POSTS                 (-357)
/** The event semaphore has already been posted. */
#define VERR_ALREADY_POSTED                 (-358)
/** The event semaphore has already been reset. */
#define VERR_ALREADY_RESET                  (-359)
/** The semaphore is in use. */
#define VERR_SEM_BUSY                       (-360)
/** The previous ownership of this semaphore has ended. */
#define VERR_SEM_OWNER_DIED                 (-361)
/** Failed to open semaphore by name - not found. */
#define VERR_SEM_NOT_FOUND                  (-362)
/** Semaphore destroyed while waiting. */
#define VERR_SEM_DESTROYED                  (-363)
/** Nested ownership requests are not permitted for this semaphore type. */
#define VERR_SEM_NESTED                     (-364)
/** The release call only release a semaphore nesting, i.e. the caller is still
 * holding the semaphore. */
#define VINF_SEM_NESTED                     (364)
/** Deadlock detected. */
#define VERR_DEADLOCK                       (-365)
/** Ping-Pong listen or speak out of turn error. */
#define VERR_SEM_OUT_OF_TURN                (-366)
/** Tried to take a semaphore in a bad context. */
#define VERR_SEM_BAD_CONTEXT                (-367)
/** Don't spin for the semaphore, but it is safe to try grab it. */
#define VINF_SEM_BAD_CONTEXT                (367)
/** Wrong locking order detected. */
#define VERR_SEM_LV_WRONG_ORDER             (-368)
/** Wrong release order detected. */
#define VERR_SEM_LV_WRONG_RELEASE_ORDER     (-369)
/** Attempt to recursively enter a non-recursive lock. */
#define VERR_SEM_LV_NESTED                  (-370)
/** Invalid parameters passed to the lock validator. */
#define VERR_SEM_LV_INVALID_PARAMETER       (-371)
/** The lock validator detected a deadlock. */
#define VERR_SEM_LV_DEADLOCK                (-372)
/** The lock validator detected an existing deadlock.
 * The deadlock was not caused by the current operation, but existed already. */
#define VERR_SEM_LV_EXISTING_DEADLOCK       (-373)
/** Not the lock owner according our records. */
#define VERR_SEM_LV_NOT_OWNER               (-374)
/** An illegal lock upgrade was attempted. */
#define VERR_SEM_LV_ILLEGAL_UPGRADE         (-375)
/** The thread is not a valid signaller of the event. */
#define VERR_SEM_LV_NOT_SIGNALLER           (-376)
/** Internal error in the lock validator or related components. */
#define VERR_SEM_LV_INTERNAL_ERROR          (-377)
/** @} */


/** @name Generic Network I/O Status Codes
 * @{
 */
/** Unresolved (unknown) network error. */
#define VERR_NET_IO_ERROR                       (-400)
/** The network is busy or is out of resources. */
#define VERR_NET_OUT_OF_RESOURCES               (-401)
/** Net host name not found. */
#define VERR_NET_HOST_NOT_FOUND                 (-402)
/** Network path not found. */
#define VERR_NET_PATH_NOT_FOUND                 (-403)
/** General network printing error. */
#define VERR_NET_PRINT_ERROR                    (-404)
/** The machine is not on the network. */
#define VERR_NET_NO_NETWORK                     (-405)
/** Name is not unique on the network. */
#define VERR_NET_NOT_UNIQUE_NAME                (-406)

/* These are BSD networking error codes - numbers correspond, don't mess! */
/** Operation in progress. */
#define VERR_NET_IN_PROGRESS                    (-436)
/** Operation already in progress. */
#define VERR_NET_ALREADY_IN_PROGRESS            (-437)
/** Attempted socket operation with a non-socket handle.
 * (This includes closed handles.) */
#define VERR_NET_NOT_SOCKET                     (-438)
/** Destination address required. */
#define VERR_NET_DEST_ADDRESS_REQUIRED          (-439)
/** Message too long. */
#define VERR_NET_MSG_SIZE                       (-440)
/** Protocol wrong type for socket. */
#define VERR_NET_PROTOCOL_TYPE                  (-441)
/** Protocol not available. */
#define VERR_NET_PROTOCOL_NOT_AVAILABLE         (-442)
/** Protocol not supported. */
#define VERR_NET_PROTOCOL_NOT_SUPPORTED         (-443)
/** Socket type not supported. */
#define VERR_NET_SOCKET_TYPE_NOT_SUPPORTED      (-444)
/** Operation not supported. */
#define VERR_NET_OPERATION_NOT_SUPPORTED        (-445)
/** Protocol family not supported. */
#define VERR_NET_PROTOCOL_FAMILY_NOT_SUPPORTED  (-446)
/** Address family not supported by protocol family. */
#define VERR_NET_ADDRESS_FAMILY_NOT_SUPPORTED   (-447)
/** Address already in use. */
#define VERR_NET_ADDRESS_IN_USE                 (-448)
/** Can't assign requested address. */
#define VERR_NET_ADDRESS_NOT_AVAILABLE          (-449)
/** Network is down. */
#define VERR_NET_DOWN                           (-450)
/** Network is unreachable. */
#define VERR_NET_UNREACHABLE                    (-451)
/** Network dropped connection on reset. */
#define VERR_NET_CONNECTION_RESET               (-452)
/** Software caused connection abort. */
#define VERR_NET_CONNECTION_ABORTED             (-453)
/** Connection reset by peer. */
#define VERR_NET_CONNECTION_RESET_BY_PEER       (-454)
/** No buffer space available. */
#define VERR_NET_NO_BUFFER_SPACE                (-455)
/** Socket is already connected. */
#define VERR_NET_ALREADY_CONNECTED              (-456)
/** Socket is not connected. */
#define VERR_NET_NOT_CONNECTED                  (-457)
/** Can't send after socket shutdown. */
#define VERR_NET_SHUTDOWN                       (-458)
/** Too many references: can't splice. */
#define VERR_NET_TOO_MANY_REFERENCES            (-459)
/** Too many references: can't splice. */
#define VERR_NET_CONNECTION_TIMED_OUT           (-460)
/** Connection refused. */
#define VERR_NET_CONNECTION_REFUSED             (-461)
/* ELOOP is not net. */
/* ENAMETOOLONG is not net. */
/** Host is down. */
#define VERR_NET_HOST_DOWN                      (-464)
/** No route to host. */
#define VERR_NET_HOST_UNREACHABLE               (-465)
/** Protocol error. */
#define VERR_NET_PROTOCOL_ERROR                 (-466)
/** Incomplete packet was submitted by guest. */
#define VERR_NET_INCOMPLETE_TX_PACKET           (-467)
/** @} */


/** @name TCP Status Codes
 * @{
 */
/** Stop the TCP server. */
#define VERR_TCP_SERVER_STOP                    (-500)
/** The server was stopped. */
#define VINF_TCP_SERVER_STOP                    500
/** The TCP server was shut down using RTTcpServerShutdown. */
#define VERR_TCP_SERVER_SHUTDOWN                (-501)
/** The TCP server was destroyed. */
#define VERR_TCP_SERVER_DESTROYED               (-502)
/** The TCP server has no client associated with it. */
#define VINF_TCP_SERVER_NO_CLIENT               503
/** @} */


/** @name UDP Status Codes
 * @{
 */
/** Stop the UDP server. */
#define VERR_UDP_SERVER_STOP                    (-520)
/** The server was stopped. */
#define VINF_UDP_SERVER_STOP                    520
/** The UDP server was shut down using RTUdpServerShutdown. */
#define VERR_UDP_SERVER_SHUTDOWN                (-521)
/** The UDP server was destroyed. */
#define VERR_UDP_SERVER_DESTROYED               (-522)
/** The UDP server has no client associated with it. */
#define VINF_UDP_SERVER_NO_CLIENT               523
/** @} */


/** @name L4 Specific Status Codes
 * @{
 */
/** Invalid offset in an L4 dataspace */
#define VERR_L4_INVALID_DS_OFFSET               (-550)
/** IPC error */
#define VERR_IPC                                (-551)
/** Item already used */
#define VERR_RESOURCE_IN_USE                    (-552)
/** Source/destination not found */
#define VERR_IPC_PROCESS_NOT_FOUND              (-553)
/** Receive timeout */
#define VERR_IPC_RECEIVE_TIMEOUT                (-554)
/** Send timeout */
#define VERR_IPC_SEND_TIMEOUT                   (-555)
/** Receive cancelled */
#define VERR_IPC_RECEIVE_CANCELLED              (-556)
/** Send cancelled */
#define VERR_IPC_SEND_CANCELLED                 (-557)
/** Receive aborted */
#define VERR_IPC_RECEIVE_ABORTED                (-558)
/** Send aborted */
#define VERR_IPC_SEND_ABORTED                   (-559)
/** Couldn't map pages during receive */
#define VERR_IPC_RECEIVE_MAP_FAILED             (-560)
/** Couldn't map pages during send */
#define VERR_IPC_SEND_MAP_FAILED                (-561)
/** Send pagefault timeout in receive */
#define VERR_IPC_RECEIVE_SEND_PF_TIMEOUT        (-562)
/** Send pagefault timeout in send */
#define VERR_IPC_SEND_SEND_PF_TIMEOUT           (-563)
/** (One) receive buffer was too small, or too few buffers */
#define VINF_IPC_RECEIVE_MSG_CUT                564
/** (One) send buffer was too small, or too few buffers */
#define VINF_IPC_SEND_MSG_CUT                   565
/** Dataspace manager server not found */
#define VERR_L4_DS_MANAGER_NOT_FOUND            (-566)
/** @} */


/** @name Loader Status Codes.
 * @{
 */
/** Invalid executable signature. */
#define VERR_INVALID_EXE_SIGNATURE              (-600)
/** The iprt loader recognized a ELF image, but doesn't support loading it. */
#define VERR_ELF_EXE_NOT_SUPPORTED              (-601)
/** The iprt loader recognized a PE image, but doesn't support loading it. */
#define VERR_PE_EXE_NOT_SUPPORTED               (-602)
/** The iprt loader recognized a LX image, but doesn't support loading it. */
#define VERR_LX_EXE_NOT_SUPPORTED               (-603)
/** The iprt loader recognized a LE image, but doesn't support loading it. */
#define VERR_LE_EXE_NOT_SUPPORTED               (-604)
/** The iprt loader recognized a NE image, but doesn't support loading it. */
#define VERR_NE_EXE_NOT_SUPPORTED               (-605)
/** The iprt loader recognized a MZ image, but doesn't support loading it. */
#define VERR_MZ_EXE_NOT_SUPPORTED               (-606)
/** The iprt loader recognized an a.out image, but doesn't support loading it. */
#define VERR_AOUT_EXE_NOT_SUPPORTED             (-607)
/** Bad executable. */
#define VERR_BAD_EXE_FORMAT                     (-608)
/** Symbol (export) not found. */
#define VERR_SYMBOL_NOT_FOUND                   (-609)
/** Module not found. */
#define VERR_MODULE_NOT_FOUND                   (-610)
/** The loader resolved an external symbol to an address to big for the image format. */
#define VERR_SYMBOL_VALUE_TOO_BIG               (-611)
/** The image is too big. */
#define VERR_IMAGE_TOO_BIG                      (-612)
/** The image base address is to high for this image type. */
#define VERR_IMAGE_BASE_TOO_HIGH                (-614)
/** Mismatching architecture. */
#define VERR_LDR_ARCH_MISMATCH                  (-615)
/** Mismatch between IPRT and native loader. */
#define VERR_LDR_MISMATCH_NATIVE                (-616)
/** Failed to resolve an imported (external) symbol. */
#define VERR_LDR_IMPORTED_SYMBOL_NOT_FOUND      (-617)
/** Generic loader failure. */
#define VERR_LDR_GENERAL_FAILURE                (-618)
/** Code signing error.  */
#define VERR_LDR_IMAGE_HASH                     (-619)
/** The PE loader encountered delayed imports, a feature which hasn't been implemented yet. */
#define VERR_LDRPE_DELAY_IMPORT                 (-620)
/** The PE loader encountered a malformed certificate. */
#define VERR_LDRPE_CERT_MALFORMED               (-621)
/** The PE loader encountered a certificate with an unsupported type or structure revision. */
#define VERR_LDRPE_CERT_UNSUPPORTED             (-622)
/** The PE loader doesn't know how to deal with the global pointer data directory entry yet. */
#define VERR_LDRPE_GLOBALPTR                    (-623)
/** The PE loader doesn't support the TLS data directory yet. */
#define VERR_LDRPE_TLS                          (-624)
/** The PE loader doesn't grok the COM descriptor data directory entry. */
#define VERR_LDRPE_COM_DESCRIPTOR               (-625)
/** The PE loader encountered an unknown load config directory/header size. */
#define VERR_LDRPE_LOAD_CONFIG_SIZE             (-626)
/** The PE loader encountered a lock prefix table, a feature which hasn't been implemented yet. */
#define VERR_LDRPE_LOCK_PREFIX_TABLE            (-627)
/** The PE loader encountered some Guard CF stuff in the load config.   */
#define VERR_LDRPE_GUARD_CF_STUFF               (-628)
/** The ELF loader doesn't handle foreign endianness. */
#define VERR_LDRELF_ODD_ENDIAN                  (-630)
/** The ELF image is 'dynamic', the ELF loader can only deal with 'relocatable' images at present. */
#define VERR_LDRELF_DYN                         (-631)
/** The ELF image is 'executable', the ELF loader can only deal with 'relocatable' images at present. */
#define VERR_LDRELF_EXEC                        (-632)
/** The ELF image was created for an unsupported target machine type. */
#define VERR_LDRELF_MACHINE                     (-633)
/** The ELF version is not supported. */
#define VERR_LDRELF_VERSION                     (-634)
/** The ELF loader cannot handle multiple SYMTAB sections. */
#define VERR_LDRELF_MULTIPLE_SYMTABS            (-635)
/** The ELF loader encountered a relocation type which is not implemented. */
#define VERR_LDRELF_RELOCATION_NOT_SUPPORTED    (-636)
/** The ELF loader encountered a bad symbol index. */
#define VERR_LDRELF_INVALID_SYMBOL_INDEX        (-637)
/** The ELF loader encountered an invalid symbol name offset. */
#define VERR_LDRELF_INVALID_SYMBOL_NAME_OFFSET  (-638)
/** The ELF loader encountered an invalid relocation offset. */
#define VERR_LDRELF_INVALID_RELOCATION_OFFSET   (-639)
/** The ELF loader didn't find the symbol/string table for the image. */
#define VERR_LDRELF_NO_SYMBOL_OR_NO_STRING_TABS (-640)
/** Invalid link address. */
#define VERR_LDR_INVALID_LINK_ADDRESS           (-647)
/** Invalid image relative virtual address. */
#define VERR_LDR_INVALID_RVA                    (-648)
/** Invalid segment:offset address. */
#define VERR_LDR_INVALID_SEG_OFFSET             (-649)
/** @}*/

/** @name Debug Info Reader Status Codes.
 * @{
 */
/** The module contains no line number information. */
#define VERR_DBG_NO_LINE_NUMBERS                (-650)
/** The module contains no symbol information. */
#define VERR_DBG_NO_SYMBOLS                     (-651)
/** The specified segment:offset address was invalid. Typically an attempt at
 * addressing outside the segment boundary. */
#define VERR_DBG_INVALID_ADDRESS                (-652)
/** Invalid segment index. */
#define VERR_DBG_INVALID_SEGMENT_INDEX          (-653)
/** Invalid segment offset. */
#define VERR_DBG_INVALID_SEGMENT_OFFSET         (-654)
/** Invalid image relative virtual address. */
#define VERR_DBG_INVALID_RVA                    (-655)
/** Invalid image relative virtual address. */
#define VERR_DBG_SPECIAL_SEGMENT                (-656)
/** Address conflict within a module/segment.
 * Attempted to add a segment, symbol or line number that fully or partially
 * overlaps with an existing one. */
#define VERR_DBG_ADDRESS_CONFLICT               (-657)
/** Duplicate symbol within the module.
 * Attempted to add a symbol which name already exists within the module.  */
#define VERR_DBG_DUPLICATE_SYMBOL               (-658)
/** The segment index specified when adding a new segment is already in use. */
#define VERR_DBG_SEGMENT_INDEX_CONFLICT         (-659)
/** No line number was found for the specified address/ordinal/whatever. */
#define VERR_DBG_LINE_NOT_FOUND                 (-660)
/** The length of the symbol name is out of range.
 * This means it is an empty string or that it's greater or equal to
 * RTDBG_SYMBOL_NAME_LENGTH. */
#define VERR_DBG_SYMBOL_NAME_OUT_OF_RANGE       (-661)
/** The length of the file name is out of range.
 * This means it is an empty string or that it's greater or equal to
 * RTDBG_FILE_NAME_LENGTH. */
#define VERR_DBG_FILE_NAME_OUT_OF_RANGE         (-662)
/** The length of the segment name is out of range.
 * This means it is an empty string or that it is greater or equal to
 * RTDBG_SEGMENT_NAME_LENGTH. */
#define VERR_DBG_SEGMENT_NAME_OUT_OF_RANGE      (-663)
/** The specified address range wraps around. */
#define VERR_DBG_ADDRESS_WRAP                   (-664)
/** The file is not a valid NM map file. */
#define VERR_DBG_NOT_NM_MAP_FILE                (-665)
/** The file is not a valid /proc/kallsyms file. */
#define VERR_DBG_NOT_LINUX_KALLSYMS             (-666)
/** No debug module interpreter matching the debug info. */
#define VERR_DBG_NO_MATCHING_INTERPRETER        (-667)
/** Bad DWARF line number header. */
#define VERR_DWARF_BAD_LINE_NUMBER_HEADER       (-668)
/** Unexpected end of DWARF unit. */
#define VERR_DWARF_UNEXPECTED_END               (-669)
/** DWARF LEB value overflows the decoder type. */
#define VERR_DWARF_LEB_OVERFLOW                 (-670)
/** Bad DWARF extended line number opcode. */
#define VERR_DWARF_BAD_LNE                      (-671)
/** Bad DWARF string. */
#define VERR_DWARF_BAD_STRING                   (-672)
/** Bad DWARF position. */
#define VERR_DWARF_BAD_POS                      (-673)
/** Bad DWARF info. */
#define VERR_DWARF_BAD_INFO                     (-674)
/** Bad DWARF abbreviation data. */
#define VERR_DWARF_BAD_ABBREV                   (-675)
/** A DWARF abbreviation was not found. */
#define VERR_DWARF_ABBREV_NOT_FOUND             (-676)
/** Encountered an unknown attribute form. */
#define VERR_DWARF_UNKNOWN_FORM                 (-677)
/** Encountered an unexpected attribute form. */
#define VERR_DWARF_UNEXPECTED_FORM              (-678)
/** Unfinished code. */
#define VERR_DWARF_TODO                         (-679)
/** Unknown location opcode. */
#define VERR_DWARF_UNKNOWN_LOC_OPCODE           (-680)
/** Expression stack overflow. */
#define VERR_DWARF_STACK_OVERFLOW               (-681)
/** Expression stack underflow. */
#define VERR_DWARF_STACK_UNDERFLOW              (-682)
/** Internal processing error in the DWARF code. */
#define VERR_DWARF_IPE                          (-683)
/** Invalid configuration property value. */
#define VERR_DBG_CFG_INVALID_VALUE              (-684)
/** Not an integer property. */
#define VERR_DBG_CFG_NOT_UINT_PROP              (-685)
/** Deferred loading of information failed. */
#define VERR_DBG_DEFERRED_LOAD_FAILED           (-686)
/** Unfinished debug info reader code. */
#define VERR_DBG_TODO                           (-687)
/** Found file, but it didn't match the search criteria. */
#define VERR_DBG_FILE_MISMATCH                  (-688)
/** Internal processing error in the debug module reader code. */
#define VERR_DBG_MOD_IPE                        (-689)
/** The symbol size was adjusted while adding it. */
#define VINF_DBG_ADJUSTED_SYM_SIZE              690
/** Unable to parse the CodeView debug information. */
#define VERR_CV_BAD_FORMAT                      (-691)
/** Unfinished CodeView debug information feature. */
#define VERR_CV_TODO                            (-692)
/** Internal processing error the CodeView debug information reader. */
#define VERR_CV_IPE                             (-693)
/** @} */

/** @name Request Packet Status Codes.
 * @{
 */
/** Invalid RT request type.
 * For the RTReqAlloc() case, the caller just specified an illegal enmType. For
 * all the other occurrences it means indicates corruption, broken logic, or stupid
 * interface user. */
#define VERR_RT_REQUEST_INVALID_TYPE            (-700)
/** Invalid RT request state.
 * The state of the request packet was not the expected and accepted one(s). Either
 * the interface user screwed up, or we've got corruption/broken logic. */
#define VERR_RT_REQUEST_STATE                   (-701)
/** Invalid RT request packet.
 * One or more of the RT controlled packet members didn't contain the correct
 * values. Some thing's broken. */
#define VERR_RT_REQUEST_INVALID_PACKAGE         (-702)
/** The status field has not been updated yet as the request is still
 * pending completion. Someone queried the iStatus field before the request
 * has been fully processed. */
#define VERR_RT_REQUEST_STATUS_STILL_PENDING    (-703)
/** The request has been freed, don't read the status now.
 * Someone is reading the iStatus field of a freed request packet. */
#define VERR_RT_REQUEST_STATUS_FREED            (-704)
/** @} */

/** @name Environment Status Code
 * @{
 */
/** The specified environment variable was not found. (RTEnvGetEx) */
#define VERR_ENV_VAR_NOT_FOUND                  (-750)
/** The specified environment variable was not found. (RTEnvUnsetEx) */
#define VINF_ENV_VAR_NOT_FOUND                  (750)
/** Unable to translate all the variables in the default environment due to
 * codeset issues (LANG / LC_ALL / LC_CTYPE). */
#define VWRN_ENV_NOT_FULLY_TRANSLATED           (751)
/** Invalid environment variable name. */
#define VERR_ENV_INVALID_VAR_NAME               (-752)
/** The environment variable is an unset record. */
#define VINF_ENV_VAR_UNSET                      (753)
/** The environment variable has been recorded as being unset. */
#define VERR_ENV_VAR_UNSET                      (-753)
/** @} */

/** @name Multiprocessor Status Codes.
 * @{
 */
/** The specified cpu is offline. */
#define VERR_CPU_OFFLINE                        (-800)
/** The specified cpu was not found. */
#define VERR_CPU_NOT_FOUND                      (-801)
/** Not all of the requested CPUs showed up in the PFNRTMPWORKER. */
#define VERR_NOT_ALL_CPUS_SHOWED                (-802)
/** Internal processing error in the RTMp code.*/
#define VERR_CPU_IPE_1                          (-803)
/** @} */

/** @name RTGetOpt status codes
 * @{ */
/** RTGetOpt: Command line option not recognized. */
#define VERR_GETOPT_UNKNOWN_OPTION              (-825)
/** RTGetOpt: Command line option needs argument. */
#define VERR_GETOPT_REQUIRED_ARGUMENT_MISSING   (-826)
/** RTGetOpt: Command line option has argument with bad format. */
#define VERR_GETOPT_INVALID_ARGUMENT_FORMAT     (-827)
/** RTGetOpt: Not an option. */
#define VINF_GETOPT_NOT_OPTION                  828
/** RTGetOpt: Command line option needs an index. */
#define VERR_GETOPT_INDEX_MISSING               (-829)
/** @} */

/** @name RTCache status codes
 * @{ */
/** RTCache: cache is full. */
#define VERR_CACHE_FULL                         (-850)
/** RTCache: cache is empty. */
#define VERR_CACHE_EMPTY                        (-851)
/** @} */

/** @name RTMemCache status codes
 * @{ */
/** Reached the max cache size. */
#define VERR_MEM_CACHE_MAX_SIZE                 (-855)
/** @} */

/** @name RTS3 status codes
 * @{ */
/** Access denied error. */
#define VERR_S3_ACCESS_DENIED                   (-875)
/** The bucket/key wasn't found. */
#define VERR_S3_NOT_FOUND                       (-876)
/** Bucket already exists. */
#define VERR_S3_BUCKET_ALREADY_EXISTS           (-877)
/** Can't delete bucket with keys. */
#define VERR_S3_BUCKET_NOT_EMPTY                (-878)
/** The current operation was canceled. */
#define VERR_S3_CANCELED                        (-879)
/** @} */

/** @name HTTP status codes
 * @{ */
/** HTTP initialization failed. */
#define VERR_HTTP_INIT_FAILED                   (-885)
/** The server has not found anything matching the URI given. */
#define VERR_HTTP_NOT_FOUND                     (-886)
/** The request is for something forbidden. Authorization will not help. */
#define VERR_HTTP_ACCESS_DENIED                 (-887)
/** The server did not understand the request due to bad syntax. */
#define VERR_HTTP_BAD_REQUEST                   (-888)
/** Couldn't connect to the server (proxy?). */
#define VERR_HTTP_COULDNT_CONNECT               (-889)
/** SSL connection error. */
#define VERR_HTTP_SSL_CONNECT_ERROR             (-890)
/** CAcert is missing or has the wrong format. */
#define VERR_HTTP_CACERT_WRONG_FORMAT           (-891)
/** Certificate cannot be authenticated with the given CA certificates. */
#define VERR_HTTP_CACERT_CANNOT_AUTHENTICATE    (-892)
/** The current HTTP request was forcefully aborted */
#define VERR_HTTP_ABORTED                       (-893)
/** Request was redirected. */
#define VERR_HTTP_REDIRECTED                    (-894)
/** Proxy couldn't be resolved. */
#define VERR_HTTP_PROXY_NOT_FOUND               (-895)
/** The remote host couldn't be resolved. */
#define VERR_HTTP_HOST_NOT_FOUND                (-896)
/** Unexpected cURL error configure the proxy. */
#define VERR_HTTP_CURL_PROXY_CONFIG             (-897)
/** Generic CURL error. */
#define VERR_HTTP_CURL_ERROR                    (-899)
/** @} */

/** @name RTManifest status codes
 * @{ */
/** A digest type used in the manifest file isn't supported. */
#define VERR_MANIFEST_UNSUPPORTED_DIGEST_TYPE   (-900)
/** An entry in the manifest file couldn't be interpreted correctly. */
#define VERR_MANIFEST_WRONG_FILE_FORMAT         (-901)
/** A digest doesn't match the corresponding file. */
#define VERR_MANIFEST_DIGEST_MISMATCH           (-902)
/** The file list doesn't match to the content of the manifest file. */
#define VERR_MANIFEST_FILE_MISMATCH             (-903)
/** The specified attribute (name) was not found in the manifest.  */
#define VERR_MANIFEST_ATTR_NOT_FOUND            (-904)
/** The attribute type did not match. */
#define VERR_MANIFEST_ATTR_TYPE_MISMATCH        (-905)
/** No attribute of the specified types was found. */
#define VERR_MANIFEST_ATTR_TYPE_NOT_FOUND        (-906)
/** @} */

/** @name RTTar status codes
 * @{ */
/** The checksum of a tar header record doesn't match. */
#define VERR_TAR_CHKSUM_MISMATCH                (-925)
/** The tar end of file record was read. */
#define VERR_TAR_END_OF_FILE                    (-926)
/** The tar file ended unexpectedly. */
#define VERR_TAR_UNEXPECTED_EOS                 (-927)
/** The tar termination records was encountered without reaching the end of
  * the input stream. */
#define VERR_TAR_EOS_MORE_INPUT                 (-928)
/** A number tar header field was malformed.  */
#define VERR_TAR_BAD_NUM_FIELD                  (-929)
/** A numeric tar header field was not terminated correctly. */
#define VERR_TAR_BAD_NUM_FIELD_TERM             (-930)
/** A number tar header field was encoded using base-256 which this
 * tar implementation currently does not support.  */
#define VERR_TAR_BASE_256_NOT_SUPPORTED         (-931)
/** A number tar header field yielded a value too large for the internal
 * variable of the tar interpreter. */
#define VERR_TAR_NUM_VALUE_TOO_LARGE            (-932)
/** The combined minor and major device number type is too small to hold the
 * value stored in the tar header.  */
#define VERR_TAR_DEV_VALUE_TOO_LARGE            (-933)
/** The mode field in a tar header is bad. */
#define VERR_TAR_BAD_MODE_FIELD                 (-934)
/** The mode field should not include the type. */
#define VERR_TAR_MODE_WITH_TYPE                 (-935)
/** The size field should be zero for links and symlinks. */
#define VERR_TAR_SIZE_NOT_ZERO                  (-936)
/** Encountered an unknown type flag. */
#define VERR_TAR_UNKNOWN_TYPE_FLAG              (-937)
/** The tar header is all zeros. */
#define VERR_TAR_ZERO_HEADER                    (-938)
/** Not a uniform standard tape v0.0 archive header. */
#define VERR_TAR_NOT_USTAR_V00                  (-939)
/** The name is empty. */
#define VERR_TAR_EMPTY_NAME                     (-940)
/** A non-directory entry has a name ending with a slash. */
#define VERR_TAR_NON_DIR_ENDS_WITH_SLASH        (-941)
/** Encountered an unsupported portable archive exchange (pax) header. */
#define VERR_TAR_UNSUPPORTED_PAX_TYPE           (-942)
/** Encountered an unsupported Solaris Tar extension. */
#define VERR_TAR_UNSUPPORTED_SOLARIS_HDR_TYPE   (-943)
/** Encountered an unsupported GNU Tar extension. */
#define VERR_TAR_UNSUPPORTED_GNU_HDR_TYPE       (-944)
/** Malformed checksum field in the tar header. */
#define VERR_TAR_BAD_CHKSUM_FIELD               (-945)
/** Malformed checksum field in the tar header. */
#define VERR_TAR_MALFORMED_GNU_LONGXXXX         (-946)
/** Too long name or link string. */
#define VERR_TAR_NAME_TOO_LONG                  (-947)
/** A directory entry in the archive. */
#define VINF_TAR_DIR_PATH                        (948)
/** @} */

/** @name RTPoll status codes
 * @{ */
/** The handle is not pollable. */
#define VERR_POLL_HANDLE_NOT_POLLABLE           (-950)
/** The handle ID is already present in the poll set. */
#define VERR_POLL_HANDLE_ID_EXISTS              (-951)
/** The handle ID was not found in the set. */
#define VERR_POLL_HANDLE_ID_NOT_FOUND           (-952)
/** The poll set is full. */
#define VERR_POLL_SET_IS_FULL                   (-953)
/** @} */

/** @name Pkzip status codes
 * @{ */
/** No end of central directory record found. */
#define VERR_PKZIP_NO_EOCB                      (-960)
/** Too long name string. */
#define VERR_PKZIP_NAME_TOO_LONG                (-961)
/** Local file header corrupt. */
#define VERR_PKZIP_BAD_LF_HEADER                (-962)
/** Central directory file header corrupt. */
#define VERR_PKZIP_BAD_CDF_HEADER               (-963)
/** Encountered an unknown type flag. */
#define VERR_PKZIP_UNKNOWN_TYPE_FLAG            (-964)
/** Found a ZIP64 Extra Information Field in a ZIP32 file. */
#define VERR_PKZIP_ZIP64EX_IN_ZIP32             (-965)


/** @name RTZip status codes
 * @{ */
/** Generic zip error. */
#define VERR_ZIP_ERROR                          (-22000)
/** The compressed data was corrupted. */
#define VERR_ZIP_CORRUPTED                      (-22001)
/** Ran out of memory while compressing or uncompressing. */
#define VERR_ZIP_NO_MEMORY                      (-22002)
/** The compression format version is unsupported. */
#define VERR_ZIP_UNSUPPORTED_VERSION            (-22003)
/** The compression method is unsupported. */
#define VERR_ZIP_UNSUPPORTED_METHOD             (-22004)
/** The compressed data started with a bad header. */
#define VERR_ZIP_BAD_HEADER                     (-22005)
/** @} */

/** @name RTVfs status codes
 * @{ */
/** The VFS chain specification does not have a valid prefix. */
#define VERR_VFS_CHAIN_NO_PREFIX                    (-22100)
/** The VFS chain specification is empty. */
#define VERR_VFS_CHAIN_EMPTY                        (-22101)
/** Expected an element. */
#define VERR_VFS_CHAIN_EXPECTED_ELEMENT             (-22102)
/** The VFS object type is not known. */
#define VERR_VFS_CHAIN_UNKNOWN_TYPE                 (-22103)
/** Expected a left parentheses. */
#define VERR_VFS_CHAIN_EXPECTED_LEFT_PARENTHESES    (-22104)
/** Expected a right parentheses. */
#define VERR_VFS_CHAIN_EXPECTED_RIGHT_PARENTHESES   (-22105)
/** Expected a provider name. */
#define VERR_VFS_CHAIN_EXPECTED_PROVIDER_NAME       (-22106)
/** Expected an element separator (| or :). */
#define VERR_VFS_CHAIN_EXPECTED_SEPARATOR           (-22107)
/** Leading element separator not permitted. */
#define VERR_VFS_CHAIN_LEADING_SEPARATOR            (-22108)
/** Trailing element separator not permitted. */
#define VERR_VFS_CHAIN_TRAILING_SEPARATOR           (-22109)
/** The provider is only allowed as the first element. */
#define VERR_VFS_CHAIN_MUST_BE_FIRST_ELEMENT        (-22110)
/** The provider cannot be the first element. */
#define VERR_VFS_CHAIN_CANNOT_BE_FIRST_ELEMENT      (-22111)
/** VFS object cast failed. */
#define VERR_VFS_CHAIN_CAST_FAILED                  (-22112)
/** Internal error in the VFS chain code. */
#define VERR_VFS_CHAIN_IPE                          (-22113)
/** VFS chain element provider not found. */
#define VERR_VFS_CHAIN_PROVIDER_NOT_FOUND           (-22114)
/** VFS chain does not terminate with the desired object type. */
#define VERR_VFS_CHAIN_FINAL_TYPE_MISMATCH          (-22115)
/** VFS chain element takes no arguments.  */
#define VERR_VFS_CHAIN_NO_ARGS                      (-22116)
/** VFS chain element takes exactly one argument. */
#define VERR_VFS_CHAIN_ONE_ARG                      (-22117)
/** VFS chain element expected at most one argument.  */
#define VERR_VFS_CHAIN_AT_MOST_ONE_ARG              (-22118)
/** VFS chain element expected at least one argument.  */
#define VERR_VFS_CHAIN_AT_LEAST_ONE_ARG             (-22119)
/** VFS chain element takes exactly two arguments. */
#define VERR_VFS_CHAIN_TWO_ARGS                     (-22120)
/** VFS chain element expected at least two arguments.  */
#define VERR_VFS_CHAIN_AT_LEAST_TWO_ARGS            (-22121)
/** VFS chain element expected at most two arguments.  */
#define VERR_VFS_CHAIN_AT_MOST_TWO_ARGS             (-22122)
/** VFS chain element takes exactly three arguments. */
#define VERR_VFS_CHAIN_THREE_ARGS                   (-22123)
/** VFS chain element expected at least three arguments.  */
#define VERR_VFS_CHAIN_AT_LEAST_THREE_ARGS          (-22124)
/** VFS chain element expected at most three arguments.  */
#define VERR_VFS_CHAIN_AT_MOST_THREE_ARGS           (-22125)
/** VFS chain element takes exactly four arguments. */
#define VERR_VFS_CHAIN_FOUR_ARGS                    (-22126)
/** VFS chain element expected at least four arguments.  */
#define VERR_VFS_CHAIN_AT_LEAST_FOUR_ARGS           (-22127)
/** VFS chain element expected at most four arguments.  */
#define VERR_VFS_CHAIN_AT_MOST_FOUR_ARGS            (-22128)
/** VFS chain element takes exactly five arguments. */
#define VERR_VFS_CHAIN_FIVE_ARGS                    (-22129)
/** VFS chain element expected at least five arguments.  */
#define VERR_VFS_CHAIN_AT_LEAST_FIVE_ARGS           (-22130)
/** VFS chain element expected at most five arguments.  */
#define VERR_VFS_CHAIN_AT_MOST_FIVE_ARGS            (-22131)
/** VFS chain element takes exactly six arguments. */
#define VERR_VFS_CHAIN_SIX_ARGS                     (-22132)
/** VFS chain element expected at least six arguments.  */
#define VERR_VFS_CHAIN_AT_LEAST_SIX_ARGS            (-22133)
/** VFS chain element expected at most six arguments.  */
#define VERR_VFS_CHAIN_AT_MOST_SIX_ARGS             (-22134)
/** VFS chain element expected at most six arguments.  */
#define VERR_VFS_CHAIN_TOO_FEW_ARGS                 (-22135)
/** VFS chain element expected at most six arguments.  */
#define VERR_VFS_CHAIN_TOO_MANY_ARGS                (-22136)
/** VFS chain element expected non-empty argument. */
#define VERR_VFS_CHAIN_EMPTY_ARG                    (-22137)
/** Invalid argument to VFS chain element. */
#define VERR_VFS_CHAIN_INVALID_ARGUMENT             (-22138)
/** VFS chain element only provides file and I/O stream (ios) objects. */
#define VERR_VFS_CHAIN_ONLY_FILE_OR_IOS             (-22139)
/** VFS chain element only provides I/O stream (ios) objects. */
#define VERR_VFS_CHAIN_ONLY_IOS                     (-22140)
/** VFS chain element only provides directory (dir) objects. */
#define VERR_VFS_CHAIN_ONLY_DIR                     (-22141)
/** VFS chain element only provides file system stream (fss) objects. */
#define VERR_VFS_CHAIN_ONLY_FSS                     (-22142)
/** VFS chain element only provides file system (vfs) objects. */
#define VERR_VFS_CHAIN_ONLY_VFS                     (-22143)
/** VFS chain element only provides file, I/O stream (ios), or
 * directory (dir) objects. */
#define VERR_VFS_CHAIN_ONLY_FILE_OR_IOS_OR_DIR      (-22144)
/** VFS chain element only provides file, I/O stream (ios), or
 * directory (dir) objects. */
#define VERR_VFS_CHAIN_ONLY_DIR_OR_VFS              (-22145)
/** VFS chain element takes a file object as input. */
#define VERR_VFS_CHAIN_TAKES_FILE                   (-22146)
/** VFS chain element takes a file or I/O stream (ios) object as input. */
#define VERR_VFS_CHAIN_TAKES_FILE_OR_IOS            (-22147)
/** VFS chain element takes a directory (dir) object as input. */
#define VERR_VFS_CHAIN_TAKES_DIR                    (-22148)
/** VFS chain element takes a file system stream (fss) object as input. */
#define VERR_VFS_CHAIN_TAKES_FSS                    (-22149)
/** VFS chain element takes a file system (vfs) object as input. */
#define VERR_VFS_CHAIN_TAKES_VFS                    (-22150)
/** VFS chain element takes a directory (dir) or file system (vfs)
 * object as input. */
#define VERR_VFS_CHAIN_TAKES_DIR_OR_VFS             (-22151)
/** VFS chain element takes a directory (dir), file system stream (fss),
 * or file system (vfs) object as input. */
#define VERR_VFS_CHAIN_TAKES_DIR_OR_FSS_OR_VFS      (-22152)
/** VFS chain element only provides a read-only I/O stream, while the chain
 * requires write access. */
#define VERR_VFS_CHAIN_READ_ONLY_IOS                (-22153)
/** VFS chain element only provides a read-only I/O stream, while the chain
 * read access. */
#define VERR_VFS_CHAIN_WRITE_ONLY_IOS               (-22154)
/** VFS chain only has a single element and it is just a path, need to be
 * treated as a normal file system request. */
#define VERR_VFS_CHAIN_PATH_ONLY                    (-22155)
/** VFS chain element preceding the final path needs to be a directory, file
 * system or file system stream. */
#define VERR_VFS_CHAIN_TYPE_MISMATCH_PATH_ONLY      (-22156)
/** VFS chain doesn't end with a path only element. */
#define VERR_VFS_CHAIN_NOT_PATH_ONLY                (-22157)
/** The path only element at the end of the VFS chain is too short to make out
 *  the parent directory. */
#define VERR_VFS_CHAIN_TOO_SHORT_FOR_PARENT         (-22158)
/** @} */

/** @name RTDvm status codes
 * @{ */
/** The volume map doesn't contain any valid volume. */
#define VERR_DVM_MAP_EMPTY                          (-22200)
/** There is no volume behind the current one. */
#define VERR_DVM_MAP_NO_VOLUME                      (-22201)
/** @} */

/** @name Logger status codes
 * @{ */
/** The internal logger revision did not match. */
#define VERR_LOG_REVISION_MISMATCH                  (-22300)
/** @} */

/* see above, 22400..22499 is used for misc codes! */

/** @name Logger status codes
 * @{ */
/** Power off is not supported by the hardware or the OS. */
#define VERR_SYS_CANNOT_POWER_OFF                   (-22500)
/** The halt action was requested, but the OS may actually power
 * off the machine. */
#define VINF_SYS_MAY_POWER_OFF                      (22501)
/** Shutdown failed. */
#define VERR_SYS_SHUTDOWN_FAILED                    (-22502)
/** @} */

/** @name Filesystem status codes
 * @{ */
/** Filesystem can't be opened because it is corrupt. */
#define VERR_FILESYSTEM_CORRUPT                     (-22600)
/** @} */

/** @name RTZipXar status codes.
 * @{ */
/** Wrong magic value. */
#define VERR_XAR_WRONG_MAGIC                        (-22700)
/** Bad header size. */
#define VERR_XAR_BAD_HDR_SIZE                       (-22701)
/** Unsupported version. */
#define VERR_XAR_UNSUPPORTED_VERSION                (-22702)
/** Unsupported hashing function. */
#define VERR_XAR_UNSUPPORTED_HASH_FUNCTION          (-22703)
/** The table of content (TOC) is too small and therefore can't be valid. */
#define VERR_XAR_TOC_TOO_SMALL                      (-22704)
/** The table of content (TOC) is too big. */
#define VERR_XAR_TOC_TOO_BIG                        (-22705)
/** The compressed table of content is too big. */
#define VERR_XAR_TOC_TOO_BIG_COMPRESSED             (-22706)
/** The uncompressed table of content size in the header didn't match what
 * ZLib returned. */
#define VERR_XAR_TOC_UNCOMP_SIZE_MISMATCH           (-22707)
/** The table of content string length didn't match the size specified in the
 *  header. */
#define VERR_XAR_TOC_STRLEN_MISMATCH                (-22708)
/** The table of content isn't valid UTF-8. */
#define VERR_XAR_TOC_UTF8_ENCODING                  (-22709)
/** XML error while parsing the table of content. */
#define VERR_XAR_TOC_XML_PARSE_ERROR                (-22710)
/** The table of content XML document does not have a toc element. */
#define VERR_XML_TOC_ELEMENT_MISSING                (-22711)
/** The table of content XML element (toc) has siblings, we expected it to be
 *  an only child or the root element (xar). */
#define VERR_XML_TOC_ELEMENT_HAS_SIBLINGS           (-22712)
/** The XAR table of content digest doesn't match. */
#define VERR_XAR_TOC_DIGEST_MISMATCH                (-22713)
/** Bad or missing XAR checksum element. */
#define VERR_XAR_BAD_CHECKSUM_ELEMENT               (-22714)
/** The hash function in the header doesn't match the one in the table of
 *  content. */
#define VERR_XAR_HASH_FUNCTION_MISMATCH             (-22715)
/** Bad digest length encountered in the table of content.  */
#define VERR_XAR_BAD_DIGEST_LENGTH                  (-22716)
/** The order of elements in the XAR file does not lend it self to expansion
 *  from via an I/O stream. */
#define VERR_XAR_NOT_STREAMBLE_ELEMENT_ORDER        (-22717)
/** Missing offset element in table of content sub-element. */
#define VERR_XAR_MISSING_OFFSET_ELEMENT             (-22718)
/** Bad offset element in table of content sub-element. */
#define VERR_XAR_BAD_OFFSET_ELEMENT                 (-22719)
/** Missing size element in table of content sub-element. */
#define VERR_XAR_MISSING_SIZE_ELEMENT               (-22720)
/** Bad size element in table of content sub-element. */
#define VERR_XAR_BAD_SIZE_ELEMENT                   (-22721)
/** Missing length element in table of content sub-element. */
#define VERR_XAR_MISSING_LENGTH_ELEMENT             (-22722)
/** Bad length element in table of content sub-element. */
#define VERR_XAR_BAD_LENGTH_ELEMENT                 (-22723)
/** Bad file element in XAR table of content. */
#define VERR_XAR_BAD_FILE_ELEMENT                   (-22724)
/** Missing data element for XAR file. */
#define VERR_XAR_MISSING_DATA_ELEMENT               (-22725)
/** Unknown XAR file type value. */
#define VERR_XAR_UNKNOWN_FILE_TYPE                  (-22726)
/** Missing encoding element for XAR data stream. */
#define VERR_XAR_NO_ENCODING                        (-22727)
/** Bad timestamp for XAR file. */
#define VERR_XAR_BAD_FILE_TIMESTAMP                 (-22728)
/** Bad file mode for XAR file. */
#define VERR_XAR_BAD_FILE_MODE                      (-22729)
/** Bad file user id for XAR file. */
#define VERR_XAR_BAD_FILE_UID                       (-22730)
/** Bad file group id for XAR file. */
#define VERR_XAR_BAD_FILE_GID                       (-22731)
/** Bad file inode device number for XAR file. */
#define VERR_XAR_BAD_FILE_DEVICE_NO                 (-22732)
/** Bad file inode number for XAR file. */
#define VERR_XAR_BAD_FILE_INODE                     (-22733)
/** Invalid name for XAR file. */
#define VERR_XAR_INVALID_FILE_NAME                  (-22734)
/** The message digest of the extracted data does not match the one supplied. */
#define VERR_XAR_EXTRACTED_HASH_MISMATCH            (-22735)
/** The extracted data has exceeded the expected size. */
#define VERR_XAR_EXTRACTED_SIZE_EXCEEDED            (-22736)
/** The message digest of the archived data does not match the one supplied. */
#define VERR_XAR_ARCHIVED_HASH_MISMATCH             (-22737)
/** The decompressor completed without using all the input data. */
#define VERR_XAR_UNUSED_ARCHIVED_DATA               (-22738)
/** Expected the archived and extracted XAR data sizes to be the same for
 * uncompressed data. */
#define VERR_XAR_ARCHIVED_AND_EXTRACTED_SIZES_MISMATCH (-22739)
/** @} */

/** @name RTX509 status codes
 * @{ */
/** Error reading a certificate in PEM format from BIO. */
#define VERR_X509_READING_CERT_FROM_BIO                  (-23100)
/** Error extracting a public key from the certificate. */
#define VERR_X509_EXTRACT_PUBKEY_FROM_CERT               (-23101)
/** Error extracting RSA from the public key. */
#define VERR_X509_EXTRACT_RSA_FROM_PUBLIC_KEY            (-23102)
/** Signature verification failed. */
#define VERR_X509_RSA_VERIFICATION_FUILURE               (-23103)
/** Basic constraints were not found. */
#define VERR_X509_NO_BASIC_CONSTARAINTS                  (-23104)
/** Error getting extensions from the certificate. */
#define VERR_X509_GETTING_EXTENSION_FROM_CERT            (-23105)
/** Error getting a data from the extension. */
#define VERR_X509_GETTING_DATA_FROM_EXTENSION            (-23106)
/** Error formatting an extension. */
#define VERR_X509_PRINT_EXTENSION_TO_BIO                 (-23107)
/** X509 certificate verification error. */
#define VERR_X509_CERTIFICATE_VERIFICATION_FAILURE       (-23108)
/** X509 certificate isn't self signed. */
#define VERR_X509_NOT_SELFSIGNED_CERTIFICATE             (-23109)
/** Warning X509 certificate isn't self signed.  */
#define VINF_X509_NOT_SELFSIGNED_CERTIFICATE             23109
/** @} */

/** @name RTAsn1 status codes
 * @{ */
/** Temporary place holder.  */
#define VERR_ASN1_ERROR                             (-22800)
/** Encountered an ASN.1 string type that is not supported. */
#define VERR_ASN1_STRING_TYPE_NOT_IMPLEMENTED       (-22801)
/** Invalid ASN.1 UTF-8 STRING encoding. */
#define VERR_ASN1_INVALID_UTF8_STRING_ENCODING      (-22802)
/** Invalid ASN.1 NUMERIC STRING encoding. */
#define VERR_ASN1_INVALID_NUMERIC_STRING_ENCODING   (-22803)
/** Invalid ASN.1 PRINTABLE STRING encoding. */
#define VERR_ASN1_INVALID_PRINTABLE_STRING_ENCODING (-22804)
/** Invalid ASN.1 T61/TELETEX STRING encoding. */
#define VERR_ASN1_INVALID_T61_STRING_ENCODING       (-22805)
/** Invalid ASN.1 VIDEOTEX STRING encoding. */
#define VERR_ASN1_INVALID_VIDEOTEX_STRING_ENCODING  (-22806)
/** Invalid ASN.1 IA5 STRING encoding. */
#define VERR_ASN1_INVALID_IA5_STRING_ENCODING       (-22807)
/** Invalid ASN.1 GRAPHIC STRING encoding. */
#define VERR_ASN1_INVALID_GRAPHIC_STRING_ENCODING   (-22808)
/** Invalid ASN.1 ISO-646/VISIBLE STRING encoding. */
#define VERR_ASN1_INVALID_VISIBLE_STRING_ENCODING   (-22809)
/** Invalid ASN.1 GENERAL STRING encoding. */
#define VERR_ASN1_INVALID_GENERAL_STRING_ENCODING   (-22810)
/** Invalid ASN.1 UNIVERSAL STRING encoding. */
#define VERR_ASN1_INVALID_UNIVERSAL_STRING_ENCODING (-22811)
/** Invalid ASN.1 BMP STRING encoding. */
#define VERR_ASN1_INVALID_BMP_STRING_ENCODING       (-22812)
/** Invalid ASN.1 OBJECT IDENTIFIER encoding. */
#define VERR_ASN1_INVALID_OBJID_ENCODING            (-22813)
/** A component value of an ASN.1 OBJECT IDENTIFIER is too big for our
 * internal representation (32-bits). */
#define VERR_ASN1_OBJID_COMPONENT_TOO_BIG           (-22814)
/** Too many components in an ASN.1 OBJECT IDENTIFIER for our internal
 * representation. */
#define VERR_ASN1_OBJID_TOO_MANY_COMPONENTS         (-22815)
/** The dotted-string representation of an ASN.1 OBJECT IDENTIFIER would be too
 * long for our internal representation. */
#define VERR_ASN1_OBJID_TOO_LONG_STRING_FORM        (-22816)
/** Invalid dotted string. */
#define VERR_ASN1_OBJID_INVALID_DOTTED_STRING       (-22817)
/** Constructed string type not implemented. */
#define VERR_ASN1_CONSTRUCTED_STRING_NOT_IMPL       (-22818)
/** Expected a different string tag. */
#define VERR_ASN1_STRING_TAG_MISMATCH               (-22819)
/** Expected a different time tag. */
#define VERR_ASN1_TIME_TAG_MISMATCH                 (-22820)
/** More unconsumed data available. */
#define VINF_ASN1_MORE_DATA                         (22821)
/** RTAsnEncodeWriteHeader return code indicating that nothing was written
 *  and the content should be skipped as well. */
#define VINF_ASN1_NOT_ENCODED                       (22822)
/** Unknown escape sequence encountered in TeletexString. */
#define VERR_ASN1_TELETEX_UNKNOWN_ESC_SEQ           (-22823)
/** Unsupported escape sequence encountered in TeletexString. */
#define VERR_ASN1_TELETEX_UNSUPPORTED_ESC_SEQ       (-22824)
/** Unsupported character set. */
#define VERR_ASN1_TELETEX_UNSUPPORTED_CHARSET       (-22825)
/** ASN.1 object has no virtual method table. */
#define VERR_ASN1_NO_VTABLE                         (-22826)
/** ASN.1 object has no pfnCheckSanity method.  */
#define VERR_ASN1_NO_CHECK_SANITY_METHOD            (-22827)
/** ASN.1 object is not present */
#define VERR_ASN1_NOT_PRESENT                       (-22828)
/** There are unconsumed bytes after decoding an ASN.1 object. */
#define VERR_ASN1_CURSOR_NOT_AT_END                 (-22829)
/** Long ASN.1 tag form is not implemented. */
#define VERR_ASN1_CURSOR_LONG_TAG                   (-22830)
/** Bad ASN.1 object length encoding. */
#define VERR_ASN1_CURSOR_BAD_LENGTH_ENCODING        (-22831)
/** Indefinite length form is against the rules. */
#define VERR_ASN1_CURSOR_ILLEGAL_IDEFINITE_LENGTH   (-22832)
/** Indefinite length form is not implemented. */
#define VERR_ASN1_CURSOR_IDEFINITE_LENGTH_NOT_SUP   (-22833)
/** ASN.1 object length goes beyond the end of the byte stream being decoded. */
#define VERR_ASN1_CURSOR_BAD_LENGTH                 (-22834)
/** Not more data in ASN.1 byte stream. */
#define VERR_ASN1_CURSOR_NO_MORE_DATA               (-22835)
/** Too little data in ASN.1 byte stream. */
#define VERR_ASN1_CURSOR_TOO_LITTLE_DATA_LEFT       (-22836)
/** Constructed string is not according to the encoding rules. */
#define VERR_ASN1_CURSOR_ILLEGAL_CONSTRUCTED_STRING (-22837)
/** Unexpected ASN.1 tag encountered while decoding. */
#define VERR_ASN1_CURSOR_TAG_MISMATCH               (-22838)
/** Unexpected ASN.1 tag class/flag encountered while decoding. */
#define VERR_ASN1_CURSOR_TAG_FLAG_CLASS_MISMATCH    (-22839)
/** ASN.1 bit string object is out of bounds. */
#define VERR_ASN1_BITSTRING_OUT_OF_BOUNDS           (-22840)
/** Bad ASN.1 time object. */
#define VERR_ASN1_TIME_BAD_NORMALIZE_INPUT          (-22841)
/** Failed to normalize ASN.1 time object. */
#define VERR_ASN1_TIME_NORMALIZE_ERROR              (-22842)
/** Normalization of ASN.1 time object didn't work out. */
#define VERR_ASN1_TIME_NORMALIZE_MISMATCH           (-22843)
/** Invalid ASN.1 UTC TIME encoding. */
#define VERR_ASN1_INVALID_UTC_TIME_ENCODING         (-22844)
/** Invalid ASN.1 GENERALIZED TIME encoding. */
#define VERR_ASN1_INVALID_GENERALIZED_TIME_ENCODING (-22845)
/** Invalid ASN.1 BOOLEAN encoding. */
#define VERR_ASN1_INVALID_BOOLEAN_ENCODING          (-22846)
/** Invalid ASN.1 NULL encoding. */
#define VERR_ASN1_INVALID_NULL_ENCODING             (-22847)
/** Invalid ASN.1 BIT STRING encoding. */
#define VERR_ASN1_INVALID_BITSTRING_ENCODING        (-22848)
/** Unimplemented ASN.1 tag reached the RTAsn1DynType code. */
#define VERR_ASN1_DYNTYPE_TAG_NOT_IMPL              (-22849)
/** ASN.1 tag and flags/class mismatch in RTAsn1DynType code. */
#define VERR_ASN1_DYNTYPE_BAD_TAG                   (-22850)
/** Unexpected ASN.1 fake/dummy object. */
#define VERR_ASN1_DUMMY_OBJECT                      (-22851)
/** ASN.1 object is too long. */
#define VERR_ASN1_TOO_LONG                          (-22852)
/** Expected primitive ASN.1 object. */
#define VERR_ASN1_EXPECTED_PRIMITIVE                (-22853)
/** Expected valid data pointer for ASN.1 object. */
#define VERR_ASN1_INVALID_DATA_POINTER              (-22854)
/** The ASN.1 encoding is too deeply nested for the decoder. */
#define VERR_ASN1_TOO_DEEPLY_NESTED                 (-22855)
/** Generic unexpected object ID error. */
#define VERR_ASN1_UNEXPECTED_OBJ_ID                 (-22856)

/** ANS.1 internal error 1. */
#define VERR_ASN1_INTERNAL_ERROR_1                  (-22895)
/** ANS.1 internal error 2. */
#define VERR_ASN1_INTERNAL_ERROR_2                  (-22896)
/** ANS.1 internal error 3. */
#define VERR_ASN1_INTERNAL_ERROR_3                  (-22897)
/** ANS.1 internal error 4. */
#define VERR_ASN1_INTERNAL_ERROR_4                  (-22898)
/** ANS.1 internal error 5. */
#define VERR_ASN1_INTERNAL_ERROR_5                  (-22899)
/** @} */

/** @name More RTLdr status codes.
 * @{ */
/** Image Verification Failure: No Authenticode Signature. */
#define VERR_LDRVI_NOT_SIGNED                       (-22900)
/** Image Verification Warning: No Authenticode Signature, but on whitelist. */
#define VINF_LDRVI_NOT_SIGNED                       (22900)
/** Image Verification Failure: Error reading image headers.  */
#define VERR_LDRVI_READ_ERROR_HDR                   (-22901)
/** Image Verification Failure: Error reading section headers. */
#define VERR_LDRVI_READ_ERROR_SHDRS                 (-22902)
/** Image Verification Failure: Error reading authenticode signature data. */
#define VERR_LDRVI_READ_ERROR_SIGNATURE             (-22903)
/** Image Verification Failure: Error reading file for hashing. */
#define VERR_LDRVI_READ_ERROR_HASH                  (-22904)
/** Image Verification Failure: Error determining the file length. */
#define VERR_LDRVI_FILE_LENGTH_ERROR                (-22905)
/** Image Verification Failure: Error allocating memory for state data. */
#define VERR_LDRVI_NO_MEMORY_STATE                  (-22906)
/** Image Verification Failure: Error allocating memory for authenticode
 *  signature data. */
#define VERR_LDRVI_NO_MEMORY_SIGNATURE              (-22907)
/** Image Verification Failure: Error allocating memory for section headers. */
#define VERR_LDRVI_NO_MEMORY_SHDRS                  (-22908)
/** Image Verification Failure: Authenticode parsing output. */
#define VERR_LDRVI_NO_MEMORY_PARSE_OUTPUT           (-22909)
/** Image Verification Failure: Invalid security directory entry. */
#define VERR_LDRVI_INVALID_SECURITY_DIR_ENTRY       (-22910)
/** Image Verification Failure:  */
#define VERR_LDRVI_BAD_CERT_HDR_LENGTH              (-22911)
/** Image Verification Failure:  */
#define VERR_LDRVI_BAD_CERT_HDR_REVISION            (-22912)
/** Image Verification Failure:  */
#define VERR_LDRVI_BAD_CERT_HDR_TYPE                (-22913)
/** Image Verification Failure: More than one certificate table entry.  */
#define VERR_LDRVI_BAD_CERT_MULTIPLE                (-22914)

/** Image Verification Failure:  */
#define VERR_LDRVI_BAD_MZ_OFFSET                    (-22915)
/** Image Verification Failure: Invalid section count. */
#define VERR_LDRVI_INVALID_SECTION_COUNT            (-22916)
/** Image Verification Failure: Raw data offsets and sizes are out of range. */
#define VERR_LDRVI_SECTION_RAW_DATA_VALUES          (-22917)
/** Optional header magic and target machine does not match. */
#define VERR_LDRVI_MACHINE_OPT_HDR_MAGIC_MISMATCH   (-22918)
/** Unsupported image target architecture. */
#define VERR_LDRVI_UNSUPPORTED_ARCH                 (-22919)

/** Image Verification Failure: Internal error in signature parser. */
#define VERR_LDRVI_PARSE_IPE                        (-22921)
/** Generic BER parse error. Will be refined later. */
#define VERR_LDRVI_PARSE_BER_ERROR                  (-22922)

/** Expected the signed data content to be the object ID of
 * SpcIndirectDataContent, found something else instead. */
#define VERR_LDRVI_EXPECTED_INDIRECT_DATA_CONTENT_OID (-22923)
/** Page hash table size overflow. */
#define VERR_LDRVI_PAGE_HASH_TAB_SIZE_OVERFLOW      (-22924)
/** Page hash table is too long (covers signature data, i.e. itself). */
#define VERR_LDRVI_PAGE_HASH_TAB_TOO_LONG           (-22925)
/** The page hash table is not strictly ordered by offset. */
#define VERR_LDRVI_PAGE_HASH_TAB_NOT_STRICTLY_SORTED (-22926)
/** The page hash table hashes data outside the defined and implicit sections. */
#define VERR_PAGE_HASH_TAB_HASHES_NON_SECTION_DATA  (-22927)
/** Page hash mismatch. */
#define VERR_LDRVI_PAGE_HASH_MISMATCH               (-22928)
/** Image hash mismatch. */
#define VERR_LDRVI_IMAGE_HASH_MISMATCH              (-22929)

/** Cannot resolve symbol because it's a forwarder. */
#define VERR_LDR_FORWARDER                          (-22950)
/** The symbol is not a forwarder. */
#define VERR_LDR_NOT_FORWARDER                      (-22951)
/** Malformed forwarder entry. */
#define VERR_LDR_BAD_FORWARDER                      (-22952)
/** Too long forwarder chain or there is a loop. */
#define VERR_LDR_FORWARDER_CHAIN_TOO_LONG           (-22953)
/** Support for forwarders has not been implemented. */
#define VERR_LDR_FORWARDERS_NOT_SUPPORTED           (-22954)
/** @} */

/** @name RTCrX509 status codes.
 * @{ */
/** Generic X.509 error. */
#define VERR_CR_X509_GENERIC_ERROR                  (-23000)
/** Internal error in the X.509 code. */
#define VERR_CR_X509_INTERNAL_ERROR                 (-23001)
/** Internal error in the X.509 certificate path building and verification
 * code. */
#define VERR_CR_X509_CERTPATHS_INTERNAL_ERROR       (-23002)
/** Path not verified yet. */
#define VERR_CR_X509_NOT_VERIFIED                   (-23003)
/** The certificate path has no trust anchor. */
#define VERR_CR_X509_NO_TRUST_ANCHOR                (-23004)
/** Unknown X.509 certificate signature algorithm. */
#define VERR_CR_X509_UNKNOWN_CERT_SIGN_ALGO         (-23005)
/** Certificate signature algorithm mismatch. */
#define VERR_CR_X509_CERT_SIGN_ALGO_MISMATCH        (-23006)
/** The signature algorithm in the to-be-signed certificate part does not match
 * the one associated with the signature. */
#define VERR_CR_X509_CERT_TBS_SIGN_ALGO_MISMATCH    (-23007)
/** Certificate extensions requires certificate version 3 or later.  */
#define VERR_CR_X509_TBSCERT_EXTS_REQ_V3            (-23008)
/** Unique issuer and subject IDs require version certificate 2. */
#define VERR_CR_X509_TBSCERT_UNIQUE_IDS_REQ_V2      (-23009)
/** Certificate serial number length is out of bounds. */
#define VERR_CR_X509_TBSCERT_SERIAL_NUMBER_OUT_OF_BOUNDS (-23010)
/** Unsupported X.509 certificate version. */
#define VERR_CR_X509_TBSCERT_UNSUPPORTED_VERSION    (-23011)
/** Public key is too small. */
#define VERR_CR_X509_PUBLIC_KEY_TOO_SMALL           (-23012)
/** Invalid string tag for a X.509 name object. */
#define VERR_CR_X509_INVALID_NAME_STRING_TAG        (-23013)
/** Empty string in X.509 name object. */
#define VERR_CR_X509_NAME_EMPTY_STRING              (-23014)
/** Non-string object inside X.509 name object. */
#define VERR_CR_X509_NAME_NOT_STRING                (-23015)
/** Empty set inside X.509 name. */
#define VERR_CR_X509_NAME_EMPTY_SET                 (-23016)
/** Empty sub-string set inside X.509 name. */
#define VERR_CR_X509_NAME_EMPTY_SUB_SET             (-23017)
/** The NotBefore and NotAfter values of an X.509 Validity object seems to
 * have been swapped around. */
#define VERR_CR_X509_VALIDITY_SWAPPED               (-23018)
/** Duplicate certificate extension. */
#define VERR_CR_X509_TBSCERT_DUPLICATE_EXTENSION    (-23019)
/** Missing relative distinguished name map entry. */
#define VERR_CR_X509_NAME_MISSING_RDN_MAP_ENTRY     (-23020)
/** Certificate path validator: No trusted certificate paths. */
#define VERR_CR_X509_CPV_NO_TRUSTED_PATHS           (-23021)
/** Certificate path validator: No valid certificate policy. */
#define VERR_CR_X509_CPV_NO_VALID_POLICY            (-23022)
/** Certificate path validator: Unknown critical certificate extension. */
#define VERR_CR_X509_CPV_UNKNOWN_CRITICAL_EXTENSION (-23023)
/** Certificate path validator: Intermediate certificate is missing the
 *  KeyCertSign usage flag. */
#define VERR_CR_X509_CPV_MISSING_KEY_CERT_SIGN      (-23024)
/** Certificate path validator: Hit the max certificate path length before
 *  reaching trust anchor. */
#define VERR_CR_X509_CPV_MAX_PATH_LENGTH            (-23025)
/** Certificate path validator: Intermediate certificate is not marked as a
 *  certificate authority (CA). */
#define VERR_CR_X509_CPV_NOT_CA_CERT                (-23026)
/** Certificate path validator: Intermediate certificate is not a version 3
 *  certificate. */
#define VERR_CR_X509_CPV_NOT_V3_CERT                (-23027)
/** Certificate path validator: Invalid policy mapping (to/from anyPolicy). */
#define VERR_CR_X509_CPV_INVALID_POLICY_MAPPING     (-23028)
/** Certificate path validator: Name constraints permits no names. */
#define VERR_CR_X509_CPV_NO_PERMITTED_NAMES         (-23029)
/** Certificate path validator: Name constraints does not permits the
 *  certificate name. */
#define VERR_CR_X509_CPV_NAME_NOT_PERMITTED         (-23030)
/** Certificate path validator: Name constraints does not permits the
 *  alternative certificate name. */
#define VERR_CR_X509_CPV_ALT_NAME_NOT_PERMITTED     (-23031)
/** Certificate path validator: Intermediate certificate subject does not
 *  match child issuer property. */
#define VERR_CR_X509_CPV_ISSUER_MISMATCH            (-23032)
/** Certificate path validator: The certificate is not valid at the
 *  specified time. */
#define VERR_CR_X509_CPV_NOT_VALID_AT_TIME          (-23033)
/** Certificate path validator: Unexpected choice found in general subtree
 *  object (name constraints). */
#define VERR_CR_X509_CPV_UNEXP_GENERAL_SUBTREE_CHOICE (-23034)
/** Certificate path validator: Unexpected minimum value found in general
 *  subtree object (name constraints). */
#define VERR_CR_X509_CPV_UNEXP_GENERAL_SUBTREE_MIN  (-23035)
/** Certificate path validator: Unexpected maximum value found in
 *  general subtree object (name constraints). */
#define VERR_CR_X509_CPV_UNEXP_GENERAL_SUBTREE_MAX  (-23036)
/** Certificate path builder: Encountered bad certificate context. */
#define VERR_CR_X509_CPB_BAD_CERT_CTX               (-23037)
/** OpenSSL d2i_X509 failed. */
#define VERR_CR_X509_OSSL_D2I_FAILED                (-23090)
/** @} */

/** @name RTCrPkcs7 status codes.
 * @{ */
/** Generic PKCS \#7 error. */
#define VERR_CR_PKCS7_GENERIC_ERROR                             (-23300)
/** Signed data verification failed because there are zero signer infos. */
#define VERR_CR_PKCS7_NO_SIGNER_INFOS                           (-23301)
/** Signed data certificate not found. */
#define VERR_CR_PKCS7_SIGNED_DATA_CERT_NOT_FOUND                (-23302)
/** Signed data verification failed due to key usage issues. */
#define VERR_CR_PKCS7_KEY_USAGE_MISMATCH                        (-23303)
/** Signed data verification failed because of missing (or duplicate)
 * authenticated content-type attribute. */
#define VERR_CR_PKCS7_MISSING_CONTENT_TYPE_ATTRIB               (-23304)
/** Signed data verification failed because of the authenticated content-type
 *  attribute did not match. */
#define VERR_CR_PKCS7_CONTENT_TYPE_ATTRIB_MISMATCH              (-23305)
/** Signed data verification failed because of a malformed authenticated
 *  content-type attribute. */
#define VERR_CR_PKCS7_BAD_CONTENT_TYPE_ATTRIB                   (-23306)
/** Signed data verification failed because of missing (or duplicate)
 * authenticated message-digest attribute. */
#define VERR_CR_PKCS7_MISSING_MESSAGE_DIGEST_ATTRIB             (-23307)
/** Signed data verification failed because the authenticated message-digest
 *  attribute did not match. */
#define VERR_CR_PKCS7_MESSAGE_DIGEST_ATTRIB_MISMATCH            (-23308)
/** Signed data verification failed because of a malformed authenticated
 *  message-digest attribute. */
#define VERR_CR_PKCS7_BAD_MESSAGE_DIGEST_ATTRIB                 (-23309)
/** Signature verification failed. */
#define VERR_CR_PKCS7_SIGNATURE_VERIFICATION_FAILED             (-23310)
/** Internal PKCS \#7 error. */
#define VERR_CR_PKCS7_INTERNAL_ERROR                            (-22311)
/** OpenSSL d2i_PKCS7 failed. */
#define VERR_CR_PKCS7_OSSL_D2I_FAILED                           (-22312)
/** OpenSSL PKCS \#7 verification failed. */
#define VERR_CR_PKCS7_OSSL_VERIFY_FAILED                        (-22313)
/** Digest algorithm parameters are not supported by the PKCS \#7 code. */
#define VERR_CR_PKCS7_DIGEST_PARAMS_NOT_IMPL                    (-22314)
/** The digest algorithm of a signer info entry was not found in the list of
 *  digest algorithms in the signed data. */
#define VERR_CR_PKCS7_DIGEST_ALGO_NOT_FOUND_IN_LIST             (-22315)
/** The PKCS \#7 content is not signed data. */
#define VERR_CR_PKCS7_NOT_SIGNED_DATA                           (-22316)
/** No digest algorithms listed in PKCS \#7 signed data. */
#define VERR_CR_PKCS7_NO_DIGEST_ALGORITHMS                      (-22317)
/** Too many digest algorithms used by PKCS \#7 signed data.  This is an
 * internal limitation of the code that aims at saving kernel stack space. */
#define VERR_CR_PKCS7_TOO_MANY_DIGEST_ALGORITHMS                (-22318)
/** Error creating digest algorithm calculator. */
#define VERR_CR_PKCS7_DIGEST_CREATE_ERROR                       (-22319)
/** Error while calculating a digest for a PKCS \#7 verification operation. */
#define VERR_CR_PKCS7_DIGEST_CALC_ERROR                         (-22320)
/** Unsupported PKCS \#7 signed data version. */
#define VERR_CR_PKCS7_SIGNED_DATA_VERSION                       (-22350)
/** PKCS \#7 signed data has no digest algorithms listed. */
#define VERR_CR_PKCS7_SIGNED_DATA_NO_DIGEST_ALGOS               (-22351)
/** Unknown digest algorithm used by PKCS \#7 object. */
#define VERR_CR_PKCS7_UNKNOWN_DIGEST_ALGORITHM                  (-22352)
/** Expected PKCS \#7 object to ship at least one certificate. */
#define VERR_CR_PKCS7_NO_CERTIFICATES                           (-22353)
/** Expected PKCS \#7 object to not contain any CRLs. */
#define VERR_CR_PKCS7_EXPECTED_NO_CRLS                          (-22354)
/** Expected PKCS \#7 object to contain exactly on signer info entry. */
#define VERR_CR_PKCS7_EXPECTED_ONE_SIGNER_INFO                  (-22355)
/** Unsupported PKCS \#7 signer info version. */
#define VERR_CR_PKCS7_SIGNER_INFO_VERSION                       (-22356)
/** PKCS \#7 singer info contains no issuer serial number. */
#define VERR_CR_PKCS7_SIGNER_INFO_NO_ISSUER_SERIAL_NO           (-22357)
/** Expected PKCS \#7 object to ship the signer certificate(s). */
#define VERR_CR_PKCS7_SIGNER_CERT_NOT_SHIPPED                   (-22358)
/** The encrypted digest algorithm does not match the one in the certificate. */
#define VERR_CR_PKCS7_SIGNER_INFO_DIGEST_ENCRYPT_MISMATCH       (-22359)
/** @} */

/** @name RTCrSpc status codes.
 * @{ */
/** Generic SPC error. */
#define VERR_CR_SPC_GENERIC_ERROR                               (-23400)
/** SPC requires there to be exactly one SignerInfo entry. */
#define VERR_CR_SPC_NOT_EXACTLY_ONE_SIGNER_INFOS                (-23401)
/** There shall be exactly one digest algorithm to go with the single
 *  SingerInfo entry required by SPC. */
#define VERR_CR_SPC_NOT_EXACTLY_ONE_DIGEST_ALGO                 (-23402)
/** The digest algorithm in the SignerInfo does not match the one in the
 *  indirect data. */
#define VERR_CR_SPC_SIGNED_IND_DATA_DIGEST_ALGO_MISMATCH        (-23403)
/** The digest algorithm in the indirect data was not found in the list of
 * digest algorithms in the signed data structure. */
#define VERR_CR_SPC_IND_DATA_DIGEST_ALGO_NOT_IN_DIGEST_ALGOS    (-23404)
/** The digest algorithm is not known to us. */
#define VERR_CR_SPC_UNKNOWN_DIGEST_ALGO                         (-23405)
/** The indirect data digest size does not match the digest algorithm. */
#define VERR_CR_SPC_IND_DATA_DIGEST_SIZE_MISMATCH               (-23406)
/** Expected PE image data inside indirect data object. */
#define VERR_CR_SPC_EXPECTED_PE_IMAGE_DATA                      (-23407)
/** Internal SPC error: The PE image data is missing.  */
#define VERR_CR_SPC_PEIMAGE_DATA_NOT_PRESENT                    (-23408)
/** Bad SPC object moniker UUID field. */
#define VERR_CR_SPC_BAD_MONIKER_UUID                            (-23409)
/** Unknown SPC object moniker UUID. */
#define VERR_CR_SPC_UNKNOWN_MONIKER_UUID                        (-23410)
/** Internal SPC error: Bad object moniker choice value. */
#define VERR_CR_SPC_BAD_MONIKER_CHOICE                          (-23411)
/** Internal SPC error: Bad object moniker data pointer. */
#define VERR_CR_SPC_MONIKER_BAD_DATA                             (-23412)
/** Multiple PE image page hash tables. */
#define VERR_CR_SPC_PEIMAGE_MULTIPLE_HASH_TABS                  (-23413)
/** Unknown SPC PE image attribute. */
#define VERR_CR_SPC_PEIMAGE_UNKNOWN_ATTRIBUTE                   (-23414)
/** URL not expected in SPC PE image data. */
#define VERR_CR_SPC_PEIMAGE_URL_UNEXPECTED                      (-23415)
/** PE image data without any valid content was not expected. */
#define VERR_CR_SPC_PEIMAGE_NO_CONTENT                          (-23416)
/** @} */

/** @name RTCrPkix status codes.
 * @{ */
/** Generic PKCS \#7 error. */
#define VERR_CR_PKIX_GENERIC_ERROR                  (-23500)
/** Parameters was presented to a signature schema that does not take any. */
#define VERR_CR_PKIX_SIGNATURE_TAKES_NO_PARAMETERS  (-23501)
/** Unknown hash digest type. */
#define VERR_CR_PKIX_UNKNOWN_DIGEST_TYPE            (-23502)
/** Internal error. */
#define VERR_CR_PKIX_INTERNAL_ERROR                 (-23503)
/** The hash is too long for the key used when signing/verifying. */
#define VERR_CR_PKIX_HASH_TOO_LONG_FOR_KEY          (-23504)
/** The signature is too long for the scratch buffer. */
#define VERR_CR_PKIX_SIGNATURE_TOO_LONG             (-23505)
/** The signature is greater than or equal to the key. */
#define VERR_CR_PKIX_SIGNATURE_GE_KEY               (-23506)
/** The signature is negative. */
#define VERR_CR_PKIX_SIGNATURE_NEGATIVE             (-23507)
/** Invalid signature length. */
#define VERR_CR_PKIX_INVALID_SIGNATURE_LENGTH       (-23508)
/** PKIX signature no does not match up to the current data. */
#define VERR_CR_PKIX_SIGNATURE_MISMATCH             (-23509)
/** PKIX cipher algorithm parameters are not implemented. */
#define VERR_CR_PKIX_CIPHER_ALGO_PARAMS_NOT_IMPL    (-23510)
/** Cipher algorithm is not known to us. */
#define VERR_CR_PKIX_CIPHER_ALGO_NOT_KNOWN          (-23511)
/** PKIX cipher algorithm is not known to OpenSSL. */
#define VERR_CR_PKIX_OSSL_CIPHER_ALGO_NOT_KNOWN     (-23512)
/** PKIX cipher algorithm is not known to OpenSSL EVP API. */
#define VERR_CR_PKIX_OSSL_CIPHER_ALGO_NOT_KNOWN_EVP (-23513)
/** OpenSSL failed to init PKIX cipher algorithm context. */
#define VERR_CR_PKIX_OSSL_CIPHER_ALOG_INIT_FAILED   (-23514)
/** Final OpenSSL PKIX verification failed. */
#define VERR_CR_PKIX_OSSL_VERIFY_FINAL_FAILED       (-23515)
/** OpenSSL failed to decode the public key. */
#define VERR_CR_PKIX_OSSL_D2I_PUBLIC_KEY_FAILED     (-23516)
/** The EVP_PKEY_type API in OpenSSL failed.  */
#define VERR_CR_PKIX_OSSL_EVP_PKEY_TYPE_ERROR       (-23517)
/** @} */

/** @name RTCrStore status codes.
 * @{ */
/** Generic store error. */
#define VERR_CR_STORE_GENERIC_ERROR                 (-23700)
/** @} */

/** @name RTCrRsa status codes.
 * @{ */
/** Generic RSA error. */
#define VERR_CR_RSA_GENERIC_ERROR                   (-23900)
/** @} */

/** @name RTBigNum status codes.
 * @{ */
/** Sensitive input requires the result(s) to be initialized as sensitive. */
#define VERR_BIGNUM_SENSITIVE_INPUT                 (-24000)
/** Attempt to divide by zero. */
#define VERR_BIGNUM_DIV_BY_ZERO                     (-24001)
/** Negative exponent makes no sense to integer math. */
#define VERR_BIGNUM_NEGATIVE_EXPONENT               (-24002)

/** @} */

/** @name RTCrDigest status codes.
 * @{ */
/** OpenSSL failed to initialize the digest algorithm context. */
#define VERR_CR_DIGEST_OSSL_DIGEST_INIT_ERROR       (-24200)
/** OpenSSL failed to clone the digest algorithm context. */
#define VERR_CR_DIGEST_OSSL_DIGEST_CTX_COPY_ERROR   (-24201)
/** @} */

/** @name RTPath  status codes.
 * @{ */
/** Unknown glob variable.  */
#define VERR_PATH_MATCH_UNKNOWN_VARIABLE            (-24400)
/** The specified glob variable must be first in the pattern. */
#define VERR_PATH_MATCH_VARIABLE_MUST_BE_FIRST      (-24401)
/** Hit unimplemented glob pattern matching feature.  */
#define VERR_PATH_MATCH_FEATURE_NOT_IMPLEMENTED     (-24402)
/** Unknown character class in glob pattern.   */
#define VERR_PATH_GLOB_UNKNOWN_CHAR_CLASS           (-24403)
/** @} */

/** @name RTUri status codes.
 * @{ */
/** The URI is empty */
#define VERR_URI_EMPTY                              (-24600)
/** The URI is too short to be a valid URI. */
#define VERR_URI_TOO_SHORT                          (-24601)
/** Invalid scheme.  */
#define VERR_URI_INVALID_SCHEME                     (-24602)
/** Invalid port number.  */
#define VERR_URI_INVALID_PORT_NUMBER                (-24603)
/** Invalid escape sequence.  */
#define VERR_URI_INVALID_ESCAPE_SEQ                 (-24604)
/** Escape URI char decodes as zero (the C string terminator). */
#define VERR_URI_ESCAPED_ZERO                       (-24605)
/** Escaped URI characters does not decode to valid UTF-8. */
#define VERR_URI_ESCAPED_CHARS_NOT_VALID_UTF8       (-24606)
/** Escaped URI character is not a valid UTF-8 lead byte. */
#define VERR_URI_INVALID_ESCAPED_UTF8_LEAD_BYTE     (-24607)
/** Escaped URI character sequence with invalid UTF-8 continutation byte. */
#define VERR_URI_INVALID_ESCAPED_UTF8_CONTINUATION_BYTE (-24608)
/** Missing UTF-8 continutation in escaped URI character sequence. */
#define VERR_URI_MISSING_UTF8_CONTINUATION_BYTE     (-24609)
/** Expected URI using the 'file:' scheme. */
#define VERR_URI_NOT_FILE_SCHEME                    (-24610)
/** @} */

/** @name RTJson status codes.
 * @{ */
/** The called method does not work with the value type of the given JSON value. */
#define VERR_JSON_VALUE_INVALID_TYPE                (-24700)
/** The iterator reached the end. */
#define VERR_JSON_ITERATOR_END                      (-24701)
/** The JSON document is malformed. */
#define VERR_JSON_MALFORMED                         (-24702)
/** @} */

/** @name RTVfs status codes.
 * @{ */
/** Unknown file system format. */
#define VERR_VFS_UNKNOWN_FORMAT                     (-24800)
/** Found bogus values in the file system. */
#define VERR_VFS_BOGUS_FORMAT                       (-24801)
/** Found bogus offset in the file system. */
#define VERR_VFS_BOGUS_OFFSET                       (-24802)
/** Unsupported file system format. */
#define VERR_VFS_UNSUPPORTED_FORMAT                 (-24803)
/** @} */

/** @name RTFsIsoMaker status codes.
 * @{ */
/** No validation entry in the boot catalog. */
#define VERR_ISOMK_BOOT_CAT_NO_VALIDATION_ENTRY         (-25000)
/** No default entry in the boot catalog. */
#define VERR_ISOMK_BOOT_CAT_NO_DEFAULT_ENTRY            (-25001)
/** Expected section header. */
#define VERR_ISOMK_BOOT_CAT_EXPECTED_SECTION_HEADER     (-25002)
/** Entry in a boot catalog section is empty. */
#define VERR_ISOMK_BOOT_CAT_EMPTY_ENTRY                 (-25003)
/** Entry in a boot catalog section is another section. */
#define VERR_ISOMK_BOOT_CAT_INVALID_SECTION_SIZE        (-25004)
/** Unsectioned boot catalog entry. */
#define VERR_ISOMK_BOOT_CAT_ERRATIC_ENTRY               (-25005)
/** The file is too big for the current ISO level (4GB+ sized files
 * requires ISO level 3). */
#define VERR_ISOMK_FILE_TOO_BIG_REQ_ISO_LEVEL_3         (-25006)
/** Cannot add symbolic link to namespace which isn't configured to support it. */
#define VERR_ISOMK_SYMLINK_REQ_ROCK_RIDGE               (-25007)
/** Cannot add symbolic link to one of the selected namespaces. */
#define VINF_ISOMK_SYMLINK_REQ_ROCK_RIDGE               (25007)
/** Cannot add symbolic link because no namespace is configured to support it. */
#define VERR_ISOMK_SYMLINK_SUPPORT_DISABLED             (-25008)
/** No space for rock ridge 'CE' entry in directory record. */
#define VERR_ISOMK_RR_NO_SPACE_FOR_CE                   (-25009)
/** Internal ISO maker error: Rock ridge read problem. */
#define VERR_ISOMK_IPE_RR_READ                          (-25010)
/** Internal ISO maker error: Buggy namespace table. */
#define VERR_ISOMK_IPE_TABLE                            (-25011)
/** Internal ISO maker error: Namespace problem \#1. */
#define VERR_ISOMK_IPE_NAMESPACE_1                      (-25012)
/** Internal ISO maker error: Namespace problem \#2. */
#define VERR_ISOMK_IPE_NAMESPACE_2                      (-25013)
/** Internal ISO maker error: Namespace problem \#3. */
#define VERR_ISOMK_IPE_NAMESPACE_3                      (-25014)
/** Internal ISO maker error: Namespace problem \#4. */
#define VERR_ISOMK_IPE_NAMESPACE_4                      (-25015)
/** Internal ISO maker error: Namespace problem \#5. */
#define VERR_ISOMK_IPE_NAMESPACE_5                      (-25016)
/** Internal ISO maker error: Namespace problem \#6. */
#define VERR_ISOMK_IPE_NAMESPACE_6                      (-25017)
/** Internal ISO maker error: Empty path. */
#define VERR_ISOMK_IPE_EMPTY_PATH                       (-25018)
/** Internal ISO maker error: Unexpected empty component. */
#define VERR_ISOMK_IPE_EMPTY_COMPONENT                  (-25019)
/** Internal ISO maker error: Expected path to start with root slash. */
#define VERR_ISOMK_IPE_ROOT_SLASH                       (-25020)
/** Internal ISO maker error: Descriptor miscounting. */
#define VERR_ISOMK_IPE_DESC_COUNT                       (-25021)
/** Internal ISO maker error: Buffer size. */
#define VERR_ISOMK_IPE_BUFFER_SIZE                      (-25022)
/** Internal ISO maker error: Boot catalog file handle problem. */
#define VERR_ISOMK_IPE_BOOT_CAT_FILE                    (-25023)
/** Internal ISO maker error: Inconsistency produing trans.tbl file. */
#define VERR_ISOMK_IPE_PRODUCE_TRANS_TBL                (-25024)
/** Internal ISO maker error: Read file data probem \#1.  */
#define VERR_ISOMK_IPE_READ_FILE_DATA_1                 (-25025)
/** Internal ISO maker error: Read file data probem \#2.  */
#define VERR_ISOMK_IPE_READ_FILE_DATA_2                 (-25026)
/** Internal ISO maker error: Read file data probem \#3.  */
#define VERR_ISOMK_IPE_READ_FILE_DATA_3                 (-25027)
/** Internal ISO maker error: Finalization problem \#1.  */
#define VERR_ISOMK_IPE_FINALIZE_1                       (-25028)
/** The spill file grew larger than 4GB. */
#define VERR_ISOMK_RR_SPILL_FILE_FULL                   (-25029)

/** Requested to import an unknown ISO format. */
#define VERR_ISOMK_IMPORT_UNKNOWN_FORMAT                (-25100)
/** Too many volume descriptors in the import ISO. */
#define VERR_ISOMK_IMPORT_TOO_MANY_VOL_DESCS            (-25101)
/** Import ISO contains a bad volume descriptor header.   */
#define VERR_ISOMK_IMPORT_INVALID_VOL_DESC_HDR          (-25102)
/** Import ISO contains more than one primary volume descriptor. */
#define VERR_ISOMK_IMPORT_MULTIPLE_PRIMARY_VOL_DESCS    (-25103)
/** Import ISO contains more than one el torito descriptor. */
#define VERR_ISOMK_IMPORT_MULTIPLE_EL_TORITO_DESCS      (-25104)
/** Import ISO contains more than one joliet volume descriptor. */
#define VERR_ISOMK_IMPORT_MULTIPLE_JOLIET_VOL_DESCS     (-25105)
/** Import ISO starts with supplementary volume descriptor before any
 * primary ones. */
#define VERR_ISOMK_IMPORT_SUPPLEMENTARY_BEFORE_PRIMARY  (-25106)
/** Import ISO contains an unsupported primary volume descriptor version. */
#define VERR_IOSMK_IMPORT_PRIMARY_VOL_DESC_VER          (-25107)
/** Import ISO contains a bad primary volume descriptor. */
#define VERR_ISOMK_IMPORT_BAD_PRIMARY_VOL_DESC          (-25108)
/** Import ISO contains an unsupported supplementary volume descriptor
 *  version. */
#define VERR_IOSMK_IMPORT_SUP_VOL_DESC_VER              (-25109)
/** Import ISO contains a bad supplementary volume descriptor. */
#define VERR_ISOMK_IMPORT_BAD_SUP_VOL_DESC              (-25110)
/** Import ISO uses a logical block size other than 2KB. */
#define VERR_ISOMK_IMPORT_LOGICAL_BLOCK_SIZE_NOT_2KB    (-25111)
/** Import ISO contains more than volume. */
#define VERR_ISOMK_IMPORT_MORE_THAN_ONE_VOLUME_IN_SET   (-25112)
/** Import ISO uses invalid volume sequence number. */
#define VERR_ISOMK_IMPORT_INVALID_VOLUMNE_SEQ_NO        (-25113)
/** Import ISO has different volume space sizes of primary and supplementary
 * volume descriptors. */
#define VERR_ISOMK_IMPORT_VOLUME_SPACE_SIZE_MISMATCH    (-25114)
/** Import ISO has different volume set sizes of primary and supplementary
 * volume descriptors. */
#define VERR_ISOMK_IMPORT_VOLUME_IN_SET_MISMATCH        (-25115)
/** Import ISO contains a bad root directory record. */
#define VERR_ISOMK_IMPORT_BAD_ROOT_DIR_REC              (-25116)
/** Import ISO contains a zero sized root directory. */
#define VERR_ISOMK_IMPORT_ZERO_SIZED_ROOT_DIR           (-25117)
/** Import ISO contains a root directory with a mismatching volume sequence
 *  number. */
#define VERR_ISOMK_IMPORT_ROOT_VOLUME_SEQ_NO            (-25118)
/** Import ISO contains a root directory with an out of bounds data extent. */
#define VERR_ISOMK_IMPORT_ROOT_DIR_EXTENT_OUT_OF_BOUNDS (-25119)
/** Import ISO contains a root directory with a bad record length. */
#define VERR_ISOMK_IMPORT_BAD_ROOT_DIR_REC_LENGTH       (-25120)
/** Import ISO contains a root directory without the directory flag set. */
#define VERR_ISOMK_IMPORT_ROOT_DIR_WITHOUT_DIR_FLAG     (-25121)
/** Import ISO contains a root directory with multiple extents. */
#define VERR_ISOMK_IMPORT_ROOT_DIR_IS_MULTI_EXTENT      (-25122)
/** Import ISO contains a too deep directory subtree. */
#define VERR_ISOMK_IMPORT_TOO_DEEP_DIR_TREE             (-25123)
/** Import ISO contains a bad directory record. */
#define VERR_ISOMK_IMPORT_BAD_DIR_REC                   (-25124)
/** Import ISO contains a directory record with a mismatching volume sequence
 *  number. */
#define VERR_ISOMK_IMPORT_DIR_REC_VOLUME_SEQ_NO         (-25125)
/** Import ISO contains a directory with an extent that is out of bounds. */
#define VERR_ISOMK_IMPORT_DIR_REC_EXTENT_OUT_OF_BOUNDS  (-25126)
/** Import ISO contains a directory with a bad record length. */
#define VERR_ISOMK_IMPORT_BAD_DIR_REC_LENGTH            (-25127)
/** Import ISO contains a '.' or '..' directory record with a bad name
 *  length. */
#define VERR_ISOMK_IMPORT_DOT_DIR_REC_BAD_NAME_LENGTH   (-25128)
/** Import ISO contains a '.' or '..' directory record with a bad name. */
#define VERR_ISOMK_IMPORT_DOT_DIR_REC_BAD_NAME          (-25129)
/** Import ISO contains a directory with a more than one extent, that's
 * currently not supported. */
#define VERR_ISOMK_IMPORT_DIR_WITH_MORE_EXTENTS         (-25130)
/** Import ISO contains a multi-extent directory record that differs
 *  significantly from first record. */
#define VERR_ISOMK_IMPORT_MISMATCHING_MULTI_EXTENT_REC  (-25131)
/** Import ISO contains a non-final multi-extent directory record with a
 *  size that isn't block aligned. */
#define VERR_ISOMK_IMPORT_MISALIGNED_MULTI_EXTENT       (-25132)
/** Import ISO contains a non-contigiuous multi-extent data, this is
 * currently not supported. */
#define VERR_ISOMK_IMPORT_NON_CONTIGUOUS_MULTI_EXTENT   (-25133)

/** The boot catalog block in the import ISO is out of bounds. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_BAD_OUT_OF_BOUNDS                (-25140)
/** The boot catalog block in the import ISO has an incorrect validation
 *  header ID. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_BAD_VALIDATION_HEADER_ID         (-25141)
/** The boot catalog validation entry in the import ISO has incorrect keys. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_BAD_VALIDATION_KEYS              (-25142)
/** The boot catalog validation entry in the import ISO has an incorrect checksum. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_BAD_VALIDATION_CHECKSUM          (-25143)
/** A boot catalog entry in the import ISO has an unknown type. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_UNKNOWN_HEADER_ID                (-25144)
/** A boot catalog entry in the import ISO has an invalid boot media type. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_INVALID_BOOT_MEDIA_TYPE          (-25145)
/** The default boot catalog entry in the import ISO has invalid flags set. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_DEF_ENTRY_INVALID_FLAGS          (-25146)
/** A boot catalog entry in the import ISO has reserved flag set. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_RESERVED_FLAG              (-25147)
/** A boot catalog entry in the import ISO is using the unused field. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_USES_UNUSED_FIELD          (-25148)
/** A boot catalog entry in the import ISO points to a block after the end of
 * the image input file. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_IMAGE_OUT_OF_BOUNDS        (-25149)
/** A boot catalog entry in the import ISO has an image with an
 * indeterminate size. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_UNKNOWN_IMAGE_SIZE         (-25150)
/** The boot catalog in the import ISO is larger than a sector or it is
 *  missing the final section header entry. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_MISSING_FINAL_OR_TOO_BIG         (-25151)
/** The default boot catalog entry in the import ISO an invalid boot
 *  indicator value. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_DEF_ENTRY_INVALID_BOOT_IND       (-25152)
/** A boot catalog extension entry in the import ISO was either flagged
 *  incorrectly in the previous entry or has an invalid header ID. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_EXT_ENTRY_INVALID_ID             (-25153)
/** A boot catalog extension entry in the import ISO uses undefined flags
 *  which will be lost. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_EXT_ENTRY_UNDEFINED_FLAGS        (-25154)
/** A boot catalog extension entry in the import ISO indicates more entries when
 *  we reached the end of the boot catalog sector. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_EXT_ENTRY_END_OF_SECTOR          (-25155)
/** A boot catalog entry in the import ISO sets the continuation flag when using
 * NONE as the selection criteria type. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_CONTINUATION_WITH_NONE     (-25156)
/** A boot catalog entry in the import ISO sets the continuation flag when
 *  we reached the ned of the boot catalog secotr. */
#define VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_CONTINUATION_EOS           (-25157)

/** @} */


/** @name RTFsIsoVol status codes
 * @{ */
/** Descriptor tag is all zeros. */
#define VERR_ISOFS_TAG_IS_ALL_ZEROS                     (-25300)
/** Unsupported descriptor tag version. */
#define VERR_ISOFS_UNSUPPORTED_TAG_VERSION              (-25301)
/** Bad descriptor tag checksum. */
#define VERR_ISOFS_BAD_TAG_CHECKSUM                     (-25302)
/** Descriptor tag sector number mismatch. */
#define VERR_ISOFS_TAG_SECTOR_MISMATCH                  (-25303)
/** Descriptor CRC mismatch. */
#define VERR_ISOFS_DESC_CRC_MISMATCH                    (-25304)
/** Insufficient data to check descriptor CRC. */
#define VERR_ISOFS_INSUFFICIENT_DATA_FOR_DESC_CRC       (-25305)
/** Unexpected/unknown/bad descriptor in volume descriptor sequence. */
#define VERR_ISOFS_UNEXPECTED_VDS_DESC                  (-25306)
/** Too many primary volume descriptors. */
#define VERR_ISOFS_TOO_MANY_PVDS                        (-25307)
/** Too many logical volume descriptors. */
#define VERR_ISOFS_TOO_MANY_LVDS                        (-25308)
/** Too many partition descriptors. */
#define VERR_ISOFS_TOO_MANY_PDS                         (-25309)
/** The logical volume descriptor has a too big partition map. */
#define VERR_ISOFS_TOO_BIT_PARTMAP_IN_LVD               (-25310)
/** No primary volume descriptors found. */
#define VERR_ISOFS_NO_PVD                               (-25311)
/** No logical volume descriptors found. */
#define VERR_ISOFS_NO_LVD                               (-25312)
/** No partition descriptors found. */
#define VERR_ISOFS_NO_PD                                (-25313)
/** Multiple primary volume descriptors found, we can only deal with one. */
#define VERR_ISOFS_MULTIPLE_PVDS                        (-25314)
/** Multiple logical volume descriptors found, we can only deal with one. */
#define VERR_ISOFS_MULTIPLE_LVDS                        (-25315)
/** Too many partition maps in the logical volume descriptor. */
#define VERR_ISOFS_TOO_MANY_PART_MAPS                   (-25316)
/** Malformed partition map table in the logical volume descriptor. */
#define VERR_ISOFS_MALFORMED_PART_MAP_TABLE             (-25317)
/** Unable to find partition descriptor for a partition map table entry. */
#define VERR_ISOFS_PARTITION_NOT_FOUND                  (-25318)
/** Partition mapping table is shorted than described. */
#define VERR_ISOFS_INCOMPLETE_PART_MAP_TABLE            (-25319)
/** Unknown partition map entry type. */
#define VERR_ISOFS_UNKNOWN_PART_MAP_ENTRY_TYPE          (-25320)
/** Unkonwn paritition ID found in the partition map table. */
#define VERR_ISOFS_UNKNOWN_PART_MAP_TYPE_ID             (-25321)
/** Support for virtual partitions as not yet been implemented. */
#define VERR_ISOFS_VPM_NOT_SUPPORTED                    (-25322)
/** Support for sparable partitions as not yet been implemented. */
#define VERR_ISOFS_SPM_NOT_SUPPORTED                    (-25323)
/** Support for metadata partitions as not yet been implemented. */
#define VERR_ISOFS_MPM_NOT_SUPPORTED                    (-25324)
/** Invalid or unsupported logical block size. */
#define VERR_ISOFS_UNSUPPORTED_LOGICAL_BLOCK_SIZE       (-25325)
/** Unsupported domain ID in logical volume descriptor. */
#define VERR_ISOFS_BAD_LVD_DOMAIN_ID                    (-25326)
/** Malformed or invalid file set descriptor location. */
#define VERR_ISOFS_BAD_LVD_FILE_SET_DESC_LOCATION       (-25327)
/** Non-standard descriptor character set in the logical volume descriptor. */
#define VERR_ISOFS_BAD_LVD_DESC_CHAR_SET                (-25329)
/** Invalid partition index in a location. */
#define VERR_ISOFS_INVALID_PARTITION_INDEX              (-25330)
/** Unsupported file system charset. */
#define VERR_ISOFS_FSD_UNSUPPORTED_CHAR_SET             (-25331)
/** File set descriptor has an zero length or invalid root dir extent. */
#define VERR_ISOFS_FSD_ZERO_ROOT_DIR                    (-25332)
/** File set descriptor has a next extent member. */
#define VERR_ISOFS_FSD_NEXT_EXTENT                      (-25333)
/** The ICB for is too big. */
#define VERR_ISOFS_ICB_TOO_BIG                          (-25334)
/** The ICB for is too small. */
#define VERR_ISOFS_ICB_TOO_SMALL                        (-25335)
/** No direct ICB entries found. */
#define VERR_ISOFS_NO_DIRECT_ICB_ENTRIES                (-25336)
/** Too many ICB indirections, possibly a loop. */
#define VERR_ISOFS_TOO_MANY_ICB_INDIRECTIONS            (-25337)
/** Too deep ICB recursion. */
#define VERR_ISOFS_TOO_DEEP_ICB_RECURSION               (-25338)
/** ICB is too small to contain anything useful.   */
#define VERR_ISOFS_ICB_ENTRY_TOO_SMALL                  (-25339)
/** Unsupported tag encountered in ICB. */
#define VERR_ISOFS_UNSUPPORTED_ICB                      (-25340)
/** Bad file entry (ICB). */
#define VERR_ISOFS_BAD_FILE_ENTRY                       (-25341)
/** Unknown allocation descriptor type.   */
#define VERR_ISO_FS_UNKNOWN_AD_TYPE                     (-25342)
/** Malformed extended allocation descriptor. */
#define VERR_ISOFS_BAD_EXTAD                            (-25343)
/** Wrong file type. */
#define VERR_ISOFS_WRONG_FILE_TYPE                      (-25344)
/** Unknow file type. */
#define VERR_ISOFS_UNKNOWN_FILE_TYPE                    (-25345)

/** Not implemented for UDF. */
#define VERR_ISOFS_UDF_NOT_IMPLEMENTED                  (-25390)
/** Internal processing error \#1.  */
#define VERR_ISOFS_IPE_1                                (-25391)
/** Internal processing error \#2.  */
#define VERR_ISOFS_IPE_2                                (-25392)
/** Internal processing error \#3.  */
#define VERR_ISOFS_IPE_3                                (-25393)
/** Internal processing error \#4.  */
#define VERR_ISOFS_IPE_4                                (-25394)
/** Internal processing error \#5.  */
#define VERR_ISOFS_IPE_5                                (-25395)

/** @} */

/* SED-END */

/** @} */

#endif

