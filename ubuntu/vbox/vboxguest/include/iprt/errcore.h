/** @file
 * IPRT - Status Codes Core.
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

#ifndef IPRT_INCLUDED_errcore_h
#define IPRT_INCLUDED_errcore_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/stdarg.h>


/** @defgroup grp_rt_err_core       Status Codes Core
 * @ingroup grp_rt_err
 * @{
 */

/** @def RTERR_STRICT_RC
 * Indicates that RT_SUCCESS_NP, RT_SUCCESS, RT_FAILURE_NP and RT_FAILURE should
 * make type enforcing at compile time.
 *
 * @remarks     Only define this for C++ code.
 */
#if defined(__cplusplus) \
 && !defined(RTERR_STRICT_RC) \
 && !defined(RTERR_NO_STRICT_RC) \
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
 * @param   iNativeCode    errno code.
 */
RTDECL(int)  RTErrConvertFromErrno(int iNativeCode);

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
#ifndef LOG_DISABLED
# define RTERRINFO_LOG_SET(  a_pErrInfo, a_rc, a_pszMsg)            RTErrInfoLogAndSet( a_pErrInfo, a_rc, LOG_GROUP, 0, a_pszMsg)
# define RTERRINFO_LOG_SET_V(a_pErrInfo, a_rc, a_pszMsg, a_va)      RTErrInfoLogAndSetV(a_pErrInfo, a_rc, LOG_GROUP, 0, a_pszMsg, a_va)
# define RTERRINFO_LOG_ADD(  a_pErrInfo, a_rc, a_pszMsg)            RTErrInfoLogAndAdd( a_pErrInfo, a_rc, LOG_GROUP, 0, a_pszMsg)
# define RTERRINFO_LOG_ADD_V(a_pErrInfo, a_rc, a_pszMsg, a_va)      RTErrInfoLogAndAddV(a_pErrInfo, a_rc, LOG_GROUP, 0, a_pszMsg, a_va)
# ifdef RT_COMPILER_SUPPORTS_VA_ARGS
#  define RTERRINFO_LOG_ADD_F(a_pErrInfo, a_rc, ...)                RTErrInfoLogAndAddF(a_pErrInfo, a_rc, LOG_GROUP, 0, __VA_ARGS__)
#  define RTERRINFO_LOG_SET_F(a_pErrInfo, a_rc, ...)                RTErrInfoLogAndSetF(a_pErrInfo, a_rc, LOG_GROUP, 0, __VA_ARGS__)
# else
#  define RTERRINFO_LOG_ADD_F                                       RTErrInfoSetF
#  define RTERRINFO_LOG_SET_F                                       RTErrInfoAddF
# endif
#else
# define RTERRINFO_LOG_SET(  a_pErrInfo, a_rc, a_pszMsg)            RTErrInfoSet( a_pErrInfo, a_rc, a_pszMsg)
# define RTERRINFO_LOG_SET_V(a_pErrInfo, a_rc, a_pszMsg, a_va)      RTErrInfoSetV(a_pErrInfo, a_rc, a_pszMsg, a_va)
# define RTERRINFO_LOG_ADD(  a_pErrInfo, a_rc, a_pszMsg)            RTErrInfoAdd( a_pErrInfo, a_rc, a_pszMsg)
# define RTERRINFO_LOG_ADD_V(a_pErrInfo, a_rc, a_pszMsg, a_va)      RTErrInfoAddV(a_pErrInfo, a_rc, a_pszMsg, a_va)
# define RTERRINFO_LOG_ADD_F                                        RTErrInfoSetF
# define RTERRINFO_LOG_SET_F                                        RTErrInfoAddF
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


/* We duplicate a handful of very commonly used status codes from err.h here.
   Needless to say, these needs to match the err.h definition exactly: */

/** Success.
 * @ingroup grp_rt_err  */
#define VINF_SUCCESS                        0

/** General failure - DON'T USE THIS!!!
 * @ingroup grp_rt_err */
#define VERR_GENERAL_FAILURE                (-1)
/** Invalid parameter.
 * @ingroup grp_rt_err */
#define VERR_INVALID_PARAMETER              (-2)
/** Invalid parameter.
 * @ingroup grp_rt_err */
#define VWRN_INVALID_PARAMETER              2
/** Invalid magic or cookie.
 * @ingroup grp_rt_err */
#define VERR_INVALID_MAGIC                  (-3)
/** Invalid magic or cookie.
 * @ingroup grp_rt_err */
#define VWRN_INVALID_MAGIC                  3
/** Invalid loader handle.
 * @ingroup grp_rt_err */
#define VERR_INVALID_HANDLE                 (-4)
/** Invalid loader handle.
 * @ingroup grp_rt_err */
#define VWRN_INVALID_HANDLE                 4
/** Invalid memory pointer. */
#define VERR_INVALID_POINTER                (-6)
/** Memory allocation failed.
 * @ingroup grp_rt_err */
#define VERR_NO_MEMORY                      (-8)
/** Permission denied.
 * @ingroup grp_rt_err */
#define VERR_PERMISSION_DENIED              (-10)
/** Permission denied.
 * @ingroup grp_rt_err */
#define VINF_PERMISSION_DENIED              10
/** Version mismatch.
 * @ingroup grp_rt_err */
#define VERR_VERSION_MISMATCH               (-11)
/** The request function is not implemented.
 * @ingroup grp_rt_err */
#define VERR_NOT_IMPLEMENTED                (-12)
/** Invalid flags was given.
 * @ingroup grp_rt_err */
#define VERR_INVALID_FLAGS                  (-13)
/** Incorrect call order.
 * @ingroup grp_rt_err */
#define VERR_WRONG_ORDER                    (-22)
/** Invalid function.
 * @ingroup grp_rt_err */
#define VERR_INVALID_FUNCTION               (-36)
/** Not supported.
 * @ingroup grp_rt_err */
#define VERR_NOT_SUPPORTED                  (-37)
/** Not supported.
 * @ingroup grp_rt_err */
#define VINF_NOT_SUPPORTED                  37
/** Access denied.
 * @ingroup grp_rt_err */
#define VERR_ACCESS_DENIED                  (-38)
/** Call interrupted.
 * @ingroup grp_rt_err */
#define VERR_INTERRUPTED                    (-39)
/** Call interrupted.
 * @ingroup grp_rt_err */
#define VINF_INTERRUPTED                    39
/** Timeout.
 * @ingroup grp_rt_err */
#define VERR_TIMEOUT                        (-40)
/** Timeout.
 * @ingroup grp_rt_err */
#define VINF_TIMEOUT                        40
/** Buffer too small to save result.
 * @ingroup grp_rt_err */
#define VERR_BUFFER_OVERFLOW                (-41)
/** Buffer too small to save result.
 * @ingroup grp_rt_err */
#define VINF_BUFFER_OVERFLOW                41
/** Data size overflow.
 * @ingroup grp_rt_err */
#define VERR_TOO_MUCH_DATA                  (-42)
/** Retry the operation.
 * @ingroup grp_rt_err */
#define VERR_TRY_AGAIN                      (-52)
/** Retry the operation.
 * @ingroup grp_rt_err */
#define VINF_TRY_AGAIN                      52
/** Generic parse error.
 * @ingroup grp_rt_err */
#define VERR_PARSE_ERROR                    (-53)
/** Value out of range.
 * @ingroup grp_rt_err */
#define VERR_OUT_OF_RANGE                   (-54)
/** A numeric conversion encountered a value which was too big for the target.
 * @ingroup grp_rt_err */
#define VERR_NUMBER_TOO_BIG                 (-55)
/** A numeric conversion encountered a value which was too big for the target.
 * @ingroup grp_rt_err */
#define VWRN_NUMBER_TOO_BIG                 55
/** The operation was cancelled by the user (copy) or another thread (local ipc).
 * @ingroup grp_rt_err */
#define VERR_CANCELLED                      (-70)
/** Trailing characters.
 * @ingroup grp_rt_err */
#define VERR_TRAILING_CHARS                 (-76)
/** Trailing characters.
 * @ingroup grp_rt_err */
#define VWRN_TRAILING_CHARS                 76
/** Trailing spaces.
 * @ingroup grp_rt_err */
#define VERR_TRAILING_SPACES                (-77)
/** Trailing spaces.
 * @ingroup grp_rt_err */
#define VWRN_TRAILING_SPACES                77
/** Generic not found error.
 * @ingroup grp_rt_err */
#define VERR_NOT_FOUND                      (-78)
/** Generic not found warning.
 * @ingroup grp_rt_err */
#define VWRN_NOT_FOUND                      78
/** Generic invalid state error.
 * @ingroup grp_rt_err */
#define VERR_INVALID_STATE                  (-79)
/** Generic invalid state warning.
 * @ingroup grp_rt_err */
#define VWRN_INVALID_STATE                  79
/** Generic out of resources error.
 * @ingroup grp_rt_err */
#define VERR_OUT_OF_RESOURCES               (-80)
/** Generic out of resources warning.
 * @ingroup grp_rt_err */
#define VWRN_OUT_OF_RESOURCES               80
/** End of string.
 * @ingroup grp_rt_err */
#define VERR_END_OF_STRING                  (-83)
/** Return instigated by a callback or similar.
 * @ingroup grp_rt_err */
#define VERR_CALLBACK_RETURN                (-88)
/** Return instigated by a callback or similar.
 * @ingroup grp_rt_err */
#define VINF_CALLBACK_RETURN                88
/** Duplicate something.
 * @ingroup grp_rt_err */
#define VERR_DUPLICATE                      (-98)
/** Something is missing.
 * @ingroup grp_rt_err */
#define VERR_MISSING                        (-99)
/** Buffer underflow.
 * @ingroup grp_rt_err */
#define VERR_BUFFER_UNDERFLOW               (-22401)
/** Buffer underflow.
 * @ingroup grp_rt_err */
#define VINF_BUFFER_UNDERFLOW               22401
/** Something is not available or not working properly.
 * @ingroup grp_rt_err */
#define VERR_NOT_AVAILABLE                  (-22403)
/** Mismatch.
 * @ingroup grp_rt_err */
#define VERR_MISMATCH                       (-22408)
/** Wrong type.
 * @ingroup grp_rt_err */
#define VERR_WRONG_TYPE                     (-22409)
/** Wrong type.
 * @ingroup grp_rt_err */
#define VWRN_WRONG_TYPE                     (22409)
/** Wrong parameter count.
 * @ingroup grp_rt_err */
#define VERR_WRONG_PARAMETER_COUNT          (-22415)
/** Wrong parameter type.
 * @ingroup grp_rt_err */
#define VERR_WRONG_PARAMETER_TYPE           (-22416)
/** Invalid client ID.
 * @ingroup grp_rt_err */
#define VERR_INVALID_CLIENT_ID              (-22417)
/** Invalid session ID.
 * @ingroup grp_rt_err */
#define VERR_INVALID_SESSION_ID             (-22418)
/** Incompatible configuration requested.
 * @ingroup grp_rt_err */
#define VERR_INCOMPATIBLE_CONFIG            (-22420)
/** Internal error - this should never happen.
 * @ingroup grp_rt_err */
#define VERR_INTERNAL_ERROR                 (-225)
/** RTGetOpt: Not an option.
 * @ingroup grp_rt_err */
#define VINF_GETOPT_NOT_OPTION              828
/** RTGetOpt: Command line option not recognized.
 * @ingroup grp_rt_err */
#define VERR_GETOPT_UNKNOWN_OPTION          (-825)

/** @} */

#endif /* !IPRT_INCLUDED_errcore_h */

