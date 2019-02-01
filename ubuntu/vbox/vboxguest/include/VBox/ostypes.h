/** @file
 * VirtualBox - Global Guest Operating System definition.
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

#ifndef VBOX_INCLUDED_ostypes_h
#define VBOX_INCLUDED_ostypes_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN

/**
 * Global list of guest operating system types.
 *
 * They are grouped into families. A family identifer is always has
 * mod 0x10000 == 0. New entries can be added, however other components
 * depend on the values (e.g. the Qt GUI and guest additions) so the
 * existing values MUST stay the same.
 *
 * Note: distinguish between 32 & 64 bits guest OSes by checking bit 8 (mod 0x100)
 */
typedef enum VBOXOSTYPE
{
    VBOXOSTYPE_Unknown          = 0,
    VBOXOSTYPE_Unknown_x64      = 0x00100,
    VBOXOSTYPE_DOS              = 0x10000,
    VBOXOSTYPE_Win31            = 0x15000,
    VBOXOSTYPE_Win9x            = 0x20000,
    VBOXOSTYPE_Win95            = 0x21000,
    VBOXOSTYPE_Win98            = 0x22000,
    VBOXOSTYPE_WinMe            = 0x23000,
    VBOXOSTYPE_WinNT            = 0x30000,
    VBOXOSTYPE_WinNT_x64        = 0x30100,
    VBOXOSTYPE_WinNT3x          = 0x30800,
    VBOXOSTYPE_WinNT4           = 0x31000,
    VBOXOSTYPE_Win2k            = 0x32000,
    VBOXOSTYPE_WinXP            = 0x33000,
    VBOXOSTYPE_WinXP_x64        = 0x33100,
    VBOXOSTYPE_Win2k3           = 0x34000,
    VBOXOSTYPE_Win2k3_x64       = 0x34100,
    VBOXOSTYPE_WinVista         = 0x35000,
    VBOXOSTYPE_WinVista_x64     = 0x35100,
    VBOXOSTYPE_Win2k8           = 0x36000,
    VBOXOSTYPE_Win2k8_x64       = 0x36100,
    VBOXOSTYPE_Win7             = 0x37000,
    VBOXOSTYPE_Win7_x64         = 0x37100,
    VBOXOSTYPE_Win8             = 0x38000,
    VBOXOSTYPE_Win8_x64         = 0x38100,
    VBOXOSTYPE_Win2k12_x64      = 0x39100,
    VBOXOSTYPE_Win81            = 0x3A000,
    VBOXOSTYPE_Win81_x64        = 0x3A100,
    VBOXOSTYPE_Win10            = 0x3B000,
    VBOXOSTYPE_Win10_x64        = 0x3B100,
    VBOXOSTYPE_Win2k16_x64      = 0x3C100,
    VBOXOSTYPE_OS2              = 0x40000,
    VBOXOSTYPE_OS2Warp3         = 0x41000,
    VBOXOSTYPE_OS2Warp4         = 0x42000,
    VBOXOSTYPE_OS2Warp45        = 0x43000,
    VBOXOSTYPE_ECS              = 0x44000,
    VBOXOSTYPE_OS21x            = 0x48000,
    VBOXOSTYPE_Linux            = 0x50000,
    VBOXOSTYPE_Linux_x64        = 0x50100,
    VBOXOSTYPE_Linux22          = 0x51000,
    VBOXOSTYPE_Linux24          = 0x52000,
    VBOXOSTYPE_Linux24_x64      = 0x52100,
    VBOXOSTYPE_Linux26          = 0x53000,
    VBOXOSTYPE_Linux26_x64      = 0x53100,
    VBOXOSTYPE_ArchLinux        = 0x54000,
    VBOXOSTYPE_ArchLinux_x64    = 0x54100,
    VBOXOSTYPE_Debian           = 0x55000,
    VBOXOSTYPE_Debian_x64       = 0x55100,
    VBOXOSTYPE_OpenSUSE         = 0x56000,
    VBOXOSTYPE_OpenSUSE_x64     = 0x56100,
    VBOXOSTYPE_FedoraCore       = 0x57000,
    VBOXOSTYPE_FedoraCore_x64   = 0x57100,
    VBOXOSTYPE_Gentoo           = 0x58000,
    VBOXOSTYPE_Gentoo_x64       = 0x58100,
    VBOXOSTYPE_Mandriva         = 0x59000,
    VBOXOSTYPE_Mandriva_x64     = 0x59100,
    VBOXOSTYPE_RedHat           = 0x5A000,
    VBOXOSTYPE_RedHat_x64       = 0x5A100,
    VBOXOSTYPE_Turbolinux       = 0x5B000,
    VBOXOSTYPE_Turbolinux_x64   = 0x5B100,
    VBOXOSTYPE_Ubuntu           = 0x5C000,
    VBOXOSTYPE_Ubuntu_x64       = 0x5C100,
    VBOXOSTYPE_Xandros          = 0x5D000,
    VBOXOSTYPE_Xandros_x64      = 0x5D100,
    VBOXOSTYPE_Oracle           = 0x5E000,
    VBOXOSTYPE_Oracle_x64       = 0x5E100,
    VBOXOSTYPE_FreeBSD          = 0x60000,
    VBOXOSTYPE_FreeBSD_x64      = 0x60100,
    VBOXOSTYPE_OpenBSD          = 0x61000,
    VBOXOSTYPE_OpenBSD_x64      = 0x61100,
    VBOXOSTYPE_NetBSD           = 0x62000,
    VBOXOSTYPE_NetBSD_x64       = 0x62100,
    VBOXOSTYPE_Netware          = 0x70000,
    VBOXOSTYPE_Solaris          = 0x80000,
    VBOXOSTYPE_Solaris_x64      = 0x80100,
    VBOXOSTYPE_OpenSolaris      = 0x81000,
    VBOXOSTYPE_OpenSolaris_x64  = 0x81100,
    VBOXOSTYPE_Solaris11_x64    = 0x82100,
    VBOXOSTYPE_L4               = 0x90000,
    VBOXOSTYPE_QNX              = 0xA0000,
    VBOXOSTYPE_MacOS            = 0xB0000,
    VBOXOSTYPE_MacOS_x64        = 0xB0100,
    VBOXOSTYPE_MacOS106         = 0xB2000,
    VBOXOSTYPE_MacOS106_x64     = 0xB2100,
    VBOXOSTYPE_MacOS107_x64     = 0xB3100,
    VBOXOSTYPE_MacOS108_x64     = 0xB4100,
    VBOXOSTYPE_MacOS109_x64     = 0xB5100,
    VBOXOSTYPE_MacOS1010_x64    = 0xB6100,
    VBOXOSTYPE_MacOS1011_x64    = 0xB7100,
    VBOXOSTYPE_MacOS1012_x64    = 0xB8100,
    VBOXOSTYPE_MacOS1013_x64    = 0xB9100,
    VBOXOSTYPE_JRockitVE        = 0xC0000,
    VBOXOSTYPE_Haiku            = 0xD0000,
    VBOXOSTYPE_Haiku_x64        = 0xD0100,
    VBOXOSTYPE_VBoxBS_x64       = 0xE0100,
/** The bit number which indicates 64-bit or 32-bit. */
#define VBOXOSTYPE_x64_BIT       8
    /** The mask which indicates 64-bit. */
    VBOXOSTYPE_x64            = 1 << VBOXOSTYPE_x64_BIT,
    /** The usual 32-bit hack. */
    VBOXOSTYPE_32BIT_HACK = 0x7fffffff
} VBOXOSTYPE;


/**
 * Global list of guest OS families.
 */
typedef enum VBOXOSFAMILY
{
    VBOXOSFAMILY_Unknown          = 0,
    VBOXOSFAMILY_Windows32        = 1,
    VBOXOSFAMILY_Windows64        = 2,
    VBOXOSFAMILY_Linux32          = 3,
    VBOXOSFAMILY_Linux64          = 4,
    VBOXOSFAMILY_FreeBSD32        = 5,
    VBOXOSFAMILY_FreeBSD64        = 6,
    VBOXOSFAMILY_Solaris32        = 7,
    VBOXOSFAMILY_Solaris64        = 8,
    VBOXOSFAMILY_MacOSX32         = 9,
    VBOXOSFAMILY_MacOSX64         = 10,
    /** The usual 32-bit hack. */
    VBOXOSFAMILY_32BIT_HACK = 0x7fffffff
} VBOXOSFAMILY;

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_ostypes_h */
