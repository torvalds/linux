/** @file
 * VBox Version Management.
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

#ifndef VBOX_INCLUDED_version_h
#define VBOX_INCLUDED_version_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Product info. */
#include <product-generated.h>
#include <version-generated.h>

#ifdef RC_INVOKED
/* Some versions of RC has trouble with cdefs.h, so we duplicate these two here. */
# define RT_STR(str)             #str
# define RT_XSTR(str)            RT_STR(str)
#else  /* !RC_INVOKED */

/** Combined version number. */
# define VBOX_VERSION                    (VBOX_VERSION_MAJOR << 16 | VBOX_VERSION_MINOR)
/** Get minor version from combined version. */
# define VBOX_GET_VERSION_MINOR(uVer)    ((uVer) & 0xffff)
/** Get major version from combined version. */
# define VBOX_GET_VERSION_MAJOR(uVer)    ((uVer) >> 16)

/**
 * Make a full version number.
 *
 * The returned number can be used in normal integer comparsions and will yield
 * the expected results.
 *
 * @param   uMajor      The major version number.
 * @param   uMinor      The minor version number.
 * @param   uBuild      The build number.
 * @returns Full version number.
 */
# define VBOX_FULL_VERSION_MAKE(uMajor, uMinor, uBuild) \
    (  (uint32_t)((uMajor) &   0xff) << 24 \
     | (uint32_t)((uMinor) &   0xff) << 16 \
     | (uint32_t)((uBuild) & 0xffff)       \
    )

/** Combined version number. */
# define VBOX_FULL_VERSION              \
    VBOX_FULL_VERSION_MAKE(VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD)
/** Get the major version number from a VBOX_FULL_VERSION style number. */
# define VBOX_FULL_VERSION_GET_MAJOR(uFullVer)  ( ((uFullVer) >> 24) &   0xffU )
/** Get the minor version number from a VBOX_FULL_VERSION style number. */
# define VBOX_FULL_VERSION_GET_MINOR(uFullVer)  ( ((uFullVer) >> 16) &   0xffU )
/** Get the build version number from a VBOX_FULL_VERSION style number. */
# define VBOX_FULL_VERSION_GET_BUILD(uFullVer)  ( ((uFullVer)      ) & 0xffffU )

/**
 * Make a short version number for use in 16 bit version fields.
 *
 * The returned number can be used in normal integer comparsions and will yield
 * the expected results.
 *
 * @param   uMajor      The major version number.
 * @param   uMinor      The minor version number.
 * @returns Short version number.
 */
# define VBOX_SHORT_VERSION_MAKE(uMajor, uMinor) \
    (  (uint16_t)((uMajor) &   0xff) << 8 \
     | (uint16_t)((uMinor) &   0xff)      \
    )

/** Combined short version number. */
# define VBOX_SHORT_VERSION               \
    VBOX_SHORT_VERSION_MAKE(VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR)
/** Get the major version number from a VBOX_SHORT_VERSION style number. */
# define VBOX_SHORT_VERSION_GET_MAJOR(uShortVer)  ( ((uShortVer) >> 8) &   0xffU )
/** Get the minor version number from a VBOX_SHORT_VERSION style number. */
# define VBOX_SHORT_VERSION_GET_MINOR(uShortVer)  ( (uShortVer) &   0xffU )

#endif /* !RC_INVOKED */

/** @name Prefined strings for Windows resource files
 * @{ */
#define VBOX_RC_COMPANY_NAME            VBOX_VENDOR
#define VBOX_RC_LEGAL_COPYRIGHT         "Copyright (C) 2009-" VBOX_C_YEAR " Oracle Corporation\0"
#define VBOX_RC_PRODUCT_NAME                    VBOX_PRODUCT
#define VBOX_RC_PRODUCT_NAME_GA                 VBOX_PRODUCT " Guest Additions"
#define VBOX_RC_PRODUCT_NAME_PUEL_EXTPACK       VBOX_PRODUCT " Extension Pack"
#define VBOX_RC_PRODUCT_NAME_DTRACE_EXTPACK     VBOX_PRODUCT " VBoxDTrace Extension Pack"
#define VBOX_RC_PRODUCT_NAME_STR                VBOX_RC_PRODUCT_NAME "\0"
#define VBOX_RC_PRODUCT_NAME_GA_STR             VBOX_RC_PRODUCT_NAME_GA "\0"
#define VBOX_RC_PRODUCT_NAME_PUEL_EXTPACK_STR   VBOX_RC_PRODUCT_NAME_PUEL_EXTPACK "\0"
#define VBOX_RC_PRODUCT_NAME_DTRACE_EXTPACK_STR VBOX_RC_PRODUCT_NAME_DTRACE_EXTPACK "\0"
#define VBOX_RC_PRODUCT_VERSION         VBOX_VERSION_MAJOR , VBOX_VERSION_MINOR , VBOX_VERSION_BUILD , VBOX_SVN_REV_MOD_5K
#define VBOX_RC_FILE_VERSION            VBOX_VERSION_MAJOR , VBOX_VERSION_MINOR , VBOX_VERSION_BUILD , VBOX_SVN_REV_MOD_5K
#ifndef VBOX_VERSION_PRERELEASE
# define VBOX_RC_PRODUCT_VERSION_STR    RT_XSTR(VBOX_VERSION_MAJOR) "." RT_XSTR(VBOX_VERSION_MINOR) "." RT_XSTR(VBOX_VERSION_BUILD) "." RT_XSTR(VBOX_SVN_REV) "\0"
# define VBOX_RC_FILE_VERSION_STR       RT_XSTR(VBOX_VERSION_MAJOR) "." RT_XSTR(VBOX_VERSION_MINOR) "." RT_XSTR(VBOX_VERSION_BUILD) "." RT_XSTR(VBOX_SVN_REV) "\0"
#else
# define VBOX_RC_PRODUCT_VERSION_STR    RT_XSTR(VBOX_VERSION_MAJOR) "." RT_XSTR(VBOX_VERSION_MINOR) "." RT_XSTR(VBOX_VERSION_BUILD) "." RT_XSTR(VBOX_SVN_REV) " (" VBOX_VERSION_PRERELEASE ")\0"
# define VBOX_RC_FILE_VERSION_STR       RT_XSTR(VBOX_VERSION_MAJOR) "." RT_XSTR(VBOX_VERSION_MINOR) "." RT_XSTR(VBOX_VERSION_BUILD) "." RT_XSTR(VBOX_SVN_REV) " (" VBOX_VERSION_PRERELEASE ")\0"
#endif
#define VBOX_RC_FILE_OS                 VOS_NT_WINDOWS32
#define VBOX_RC_TYPE_DLL                VFT_DLL
#define VBOX_RC_TYPE_APP                VFT_APP
#define VBOX_RC_TYPE_DRV                VFT_DRV
/* Flags and extra strings depending on the build type and who's building. */
#if defined(DEBUG) || defined(LOG_ENABLED) || defined(RT_STRICT) || defined(VBOX_STRICT) || defined(VBOX_WITH_STATISTICS)
# define VBOX_RC_FILE_FLAGS_DEBUG       VS_FF_DEBUG
#else
# define VBOX_RC_FILE_FLAGS_DEBUG       0
#endif
#if VBOX_VERSION_MINOR >= 51 || defined(VBOX_VERSION_PRERELEASE)
# define VBOX_RC_FILE_FLAGS_PRERELEASE  VS_FF_PRERELEASE
#else
# define VBOX_RC_FILE_FLAGS_PRERELEASE  0
#endif
#if defined(VBOX_BUILD_SERVER_BUILD) && (VBOX_VERSION_MINOR & 1) == 0
# define VBOX_RC_FILE_FLAGS_BUILD       0
# define VBOX_RC_MORE_STRINGS
#elif defined(VBOX_BUILD_SERVER_BUILD)
# define VBOX_RC_FILE_FLAGS_BUILD       VS_FF_SPECIALBUILD
# define VBOX_RC_MORE_STRINGS           VALUE "SpecialBuild", "r" RT_XSTR(VBOX_SVN_REV) "\0"
#else
# define VBOX_RC_FILE_FLAGS_BUILD       VS_FF_PRIVATEBUILD
# ifdef VBOX_PRIVATE_BUILD_DESC
#  define VBOX_RC_MORE_STRINGS          VALUE "PrivateBuild", VBOX_PRIVATE_BUILD_DESC "\0"
# else
#  define VBOX_RC_MORE_STRINGS          VALUE "PrivateBuild", "r" RT_XSTR(VBOX_SVN_REV) "\0"
# error
# endif
#endif
#define VBOX_RC_FILE_FLAGS              (VBOX_RC_FILE_FLAGS_DEBUG | VBOX_RC_FILE_FLAGS_PRERELEASE | VBOX_RC_FILE_FLAGS_BUILD)
/** @} */

#endif /* !VBOX_INCLUDED_version_h */

