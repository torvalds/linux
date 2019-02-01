/** @file
 * VirtualBox - Common C and C++ definition.
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

#ifndef VBOX_INCLUDED_cdefs_h
#define VBOX_INCLUDED_cdefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>


/** @defgroup grp_vbox_cdefs    VBox Common Defintions and Macros
 * @{
 */

/** @def VBOX_WITH_STATISTICS
 * When defined all statistics will be included in the build.
 * This is enabled by default in all debug builds.
 */
#ifndef VBOX_WITH_STATISTICS
# ifdef DEBUG
#  define VBOX_WITH_STATISTICS
# endif
#endif

/** @def VBOX_STRICT
 * Alias for RT_STRICT.
 */
#ifdef RT_STRICT
# ifndef VBOX_STRICT
#  define VBOX_STRICT
# endif
#endif

/** @def VBOX_STRICT_GUEST
 * Be strict on guest input.  This can be overriden on the compiler command line
 * or per source file by defining VBOX_NO_STRICT_GUEST.
 *
 * @sa VBox/assert.h and its ASSERT_GUEST_XXXX macros.
 */
#ifndef VBOX_STRICT_GUEST
# ifdef VBOX_STRICT
#  define VBOX_STRICT_GUEST
# endif
#endif
/** @def VBOX_NO_STRICT_GUEST
 * Define to override VBOX_STRICT_GUEST, disabling asserting on guest input. */
#ifdef VBOX_NO_STRICT_GUEST
# undef VBOX_STRICT_GUEST
#endif


/*
 * Shut up DOXYGEN warnings and guide it properly thru the code.
 */
#ifdef DOXYGEN_RUNNING
#define VBOX_WITH_STATISTICS
#define VBOX_STRICT
#define VBOX_STRICT_GUEST
#define VBOX_NO_STRICT_GUEST
#define IN_DBG
#define IN_DIS
#define IN_INTNET_R0
#define IN_INTNET_R3
#define IN_PCIRAW_R0
#define IN_PCIRAW_R3
#define IN_REM_R3
#define IN_SUP_R0
#define IN_SUP_R3
#define IN_SUP_RC
#define IN_SUP_STATIC
#define IN_USBLIB
#define IN_VBOXDDU
#define IN_VMM_RC
#define IN_VMM_R0
#define IN_VMM_R3
#define IN_VMM_STATIC
#endif




/** @def VBOXCALL
 * The standard calling convention for VBOX interfaces.
 */
#define VBOXCALL   RTCALL



/** @def IN_DIS
 * Used to indicate whether we're inside the same link module as the
 * disassembler.
 */
/** @def DISDECL(type)
 * Disassembly export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#if defined(IN_DIS)
# ifdef IN_DIS_STATIC
#  define DISDECL(type)     DECLHIDDEN(type) VBOXCALL
# else
#  define DISDECL(type)     DECLEXPORT(type) VBOXCALL
# endif
#else
# define DISDECL(type)      DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_DBG
 * Used to indicate whether we're inside the same link module as the debugger
 * console, gui, and related things (ring-3).
 */
/** @def DBGDECL(type)
 * Debugger module export or import declaration.
 * Functions declared using this exists only in R3 since the
 * debugger modules is R3 only.
 * @param   type    The return type of the function declaration.
 */
#if defined(IN_DBG_R3) || defined(IN_DBG)
# define DBGDECL(type)      DECLEXPORT(type) VBOXCALL
#else
# define DBGDECL(type)      DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_INTNET_R3
 * Used to indicate whether we're inside the same link module as the Ring-3
 * Internal Networking Service.
 */
/** @def INTNETR3DECL(type)
 * Internal Networking Service export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_INTNET_R3
# define INTNETR3DECL(type) DECLEXPORT(type) VBOXCALL
#else
# define INTNETR3DECL(type) DECLIMPORT(type) VBOXCALL
#endif

/** @def IN_INTNET_R0
 * Used to indicate whether we're inside the same link module as the R0
 * Internal Network Service.
 */
/** @def INTNETR0DECL(type)
 * Internal Networking Service export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_INTNET_R0
# define INTNETR0DECL(type) DECLEXPORT(type) VBOXCALL
#else
# define INTNETR0DECL(type) DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_PCIRAW_R3
 * Used to indicate whether we're inside the same link module as the Ring-3
 * PCI passthrough support.
 */
/** @def PCIRAWR3DECL(type)
 * PCI passthrough export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_PCIRAW_R3
# define PCIRAWR3DECL(type) DECLEXPORT(type) VBOXCALL
#else
# define PCIRAWR3DECL(type) DECLIMPORT(type) VBOXCALL
#endif

/** @def IN_PCIRAW_R0
 * Used to indicate whether we're inside the same link module as the R0
 * PCI passthrough support.
 */
/** @def PCIRAWR0DECL(type)
 * PCI passthroug export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_PCIRAW_R0
# define PCIRAWR0DECL(type) DECLEXPORT(type) VBOXCALL
#else
# define PCIRAWR0DECL(type) DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_REM_R3
 * Used to indicate whether we're inside the same link module as
 * the HC Ring-3 Recompiled Execution Manager.
 */
/** @def REMR3DECL(type)
 * Recompiled Execution Manager HC Ring-3 export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_REM_R3
# define REMR3DECL(type)    DECLEXPORT(type) VBOXCALL
#else
# define REMR3DECL(type)    DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_SUP_R3
 * Used to indicate whether we're inside the same link module as the Ring-3
 * Support Library or not.
 */
/** @def SUPR3DECL(type)
 * Support library export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_SUP_R3
# ifdef IN_SUP_STATIC
#  define SUPR3DECL(type)   DECLHIDDEN(type) VBOXCALL
# else
#  define SUPR3DECL(type)   DECLEXPORT(type) VBOXCALL
# endif
#else
# ifdef IN_SUP_STATIC
#  define SUPR3DECL(type)   DECLHIDDEN(type) VBOXCALL
# else
#  define SUPR3DECL(type)   DECLIMPORT(type) VBOXCALL
# endif
#endif

/** @def IN_SUP_R0
 * Used to indicate whether we're inside the same link module as the Ring-0
 * Support Library or not.
 */
/** @def IN_SUP_STATIC
 * Used to indicate that the Support Library is built or used as a static
 * library.
 */
/** @def SUPR0DECL(type)
 * Support library export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_SUP_R0
# ifdef IN_SUP_STATIC
#  define SUPR0DECL(type)   DECLHIDDEN(type) VBOXCALL
# else
#  define SUPR0DECL(type)   DECLEXPORT(type) VBOXCALL
# endif
#else
# ifdef IN_SUP_STATIC
#  define SUPR0DECL(type)   DECLHIDDEN(type) VBOXCALL
# else
#  define SUPR0DECL(type)   DECLIMPORT(type) VBOXCALL
# endif
#endif

/** @def IN_SUP_RC
 * Used to indicate whether we're inside the same link module as the RC Support
 * Library or not.
 */
/** @def SUPRCDECL(type)
 * Support library export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_SUP_RC
# define SUPRCDECL(type)    DECLEXPORT(type) VBOXCALL
#else
# define SUPRCDECL(type)    DECLIMPORT(type) VBOXCALL
#endif

/** @def IN_SUP_R0
 * Used to indicate whether we're inside the same link module as the Ring-0
 * Support Library or not.
 */
/** @def SUPR0DECL(type)
 * Support library export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#if defined(IN_SUP_R0) || defined(IN_SUP_R3) || defined(IN_SUP_RC)
# define SUPDECL(type)      DECLEXPORT(type) VBOXCALL
#else
# define SUPDECL(type)      DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_USBLIB
 * Used to indicate whether we're inside the same link module as the USBLib.
 */
/** @def USBLIB_DECL
 * USBLIB export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_RING0
# define USBLIB_DECL(type)   type VBOXCALL
#elif defined(IN_USBLIB)
# define USBLIB_DECL(type)   DECLEXPORT(type) VBOXCALL
#else
# define USBLIB_DECL(type)   DECLIMPORT(type) VBOXCALL
#endif



/** @def IN_VMM_STATIC
 * Used to indicate that the virtual machine monitor is built or used as a
 * static library.
 */
/** @def IN_VMM_R3
 * Used to indicate whether we're inside the same link module as the ring 3 part of the
 * virtual machine monitor or not.
 */
/** @def VMMR3DECL
 * Ring-3 VMM export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VMM_R3
# ifdef IN_VMM_STATIC
#  define VMMR3DECL(type)           DECLHIDDEN(type) VBOXCALL
# else
#  define VMMR3DECL(type)           DECLEXPORT(type) VBOXCALL
# endif
#elif defined(IN_RING3)
# ifdef IN_VMM_STATIC
#  define VMMR3DECL(type)           DECLHIDDEN(type) VBOXCALL
# else
#  define VMMR3DECL(type)           DECLIMPORT(type) VBOXCALL
# endif
#else
# define VMMR3DECL(type)            DECL_INVALID(type)
#endif

/** @def IN_VMM_R0
 * Used to indicate whether we're inside the same link module as the ring-0 part
 * of the virtual machine monitor or not.
 */
/** @def VMMR0DECL
 * Ring-0 VMM export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VMM_R0
# define VMMR0DECL(type)            DECLEXPORT(type) VBOXCALL
#elif defined(IN_RING0)
# define VMMR0DECL(type)            DECLIMPORT(type) VBOXCALL
#else
# define VMMR0DECL(type)            DECL_INVALID(type)
#endif

/** @def IN_VMM_RC
 * Used to indicate whether we're inside the same link module as the raw-mode
 * context part of the virtual machine monitor or not.
 */
/** @def VMMRCDECL
 * Raw-mode context VMM export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VMM_RC
# define VMMRCDECL(type)            DECLEXPORT(type) VBOXCALL
#elif defined(IN_RC)
# define VMMRCDECL(type)            DECLIMPORT(type) VBOXCALL
#else
# define VMMRCDECL(type)            DECL_INVALID(type)
#endif

/** @def VMMRZDECL
 * Ring-0 and Raw-mode context VMM export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#if defined(IN_VMM_R0) || defined(IN_VMM_RC)
# define VMMRZDECL(type)            DECLEXPORT(type) VBOXCALL
#elif defined(IN_RING0) || defined(IN_RZ)
# define VMMRZDECL(type)            DECLIMPORT(type) VBOXCALL
#else
# define VMMRZDECL(type)            DECL_INVALID(type)
#endif

/** @def VMMDECL
 * VMM export or import declaration.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VMM_STATIC
# define VMMDECL(type)              DECLHIDDEN(type) VBOXCALL
#elif defined(IN_VMM_R3) || defined(IN_VMM_R0) || defined(IN_VMM_RC)
# define VMMDECL(type)              DECLEXPORT(type) VBOXCALL
#else
# define VMMDECL(type)              DECLIMPORT(type) VBOXCALL
#endif

/** @def VMM_INT_DECL
 * VMM internal function.
 * @param   type    The return type of the function declaration.
 */
#if defined(IN_VMM_R3) || defined(IN_VMM_R0) || defined(IN_VMM_RC)
# define VMM_INT_DECL(type)         DECLHIDDEN(type) VBOXCALL
#else
# define VMM_INT_DECL(type)         DECL_INVALID(type)
#endif

/** @def VMMR3_INT_DECL
 * VMM internal function, ring-3.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VMM_R3
# define VMMR3_INT_DECL(type)       DECLHIDDEN(type) VBOXCALL
#else
# define VMMR3_INT_DECL(type)       DECL_INVALID(type)
#endif

/** @def VMMR0_INT_DECL
 * VMM internal function, ring-0.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VMM_R0
# define VMMR0_INT_DECL(type)       DECLHIDDEN(type) VBOXCALL
#else
# define VMMR0_INT_DECL(type)       DECL_INVALID(type)
#endif

/** @def VMMRC_INT_DECL
 * VMM internal function, raw-mode context.
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VMM_RC
# define VMMRC_INT_DECL(type)       DECLHIDDEN(type) VBOXCALL
#else
# define VMMRC_INT_DECL(type)       DECL_INVALID(type)
#endif

/** @def VMMRZ_INT_DECL
 * VMM internal function, ring-0 + raw-mode context.
 * @param   type    The return type of the function declaration.
 */
#if defined(IN_VMM_RC) || defined(IN_VMM_R0)
# define VMMRZ_INT_DECL(type)       DECLHIDDEN(type) VBOXCALL
#else
# define VMMRZ_INT_DECL(type)       DECL_INVALID(type)
#endif



/** @def IN_VBOXDDU
 * Used to indicate whether we're inside the VBoxDDU shared object.
 */
/** @def VBOXDDU_DECL(type)
 * VBoxDDU export or import (ring-3).
 * @param   type    The return type of the function declaration.
 */
#ifdef IN_VBOXDDU
# ifdef IN_VBOXDDU_STATIC
#  define VBOXDDU_DECL(type) type
# else
#  define VBOXDDU_DECL(type) DECLEXPORT(type) VBOXCALL
# endif
#else
# define VBOXDDU_DECL(type) DECLIMPORT(type) VBOXCALL
#endif

/** @} */


/** @defgroup grp_devdrv    Device Emulations and Drivers
 * @{ */
/** @} */

#endif /* !VBOX_INCLUDED_cdefs_h */

