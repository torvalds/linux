/*
 * include/asm-sh/cpu-sh2/ubc.h
 *
 * Copyright (C) 2003 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH2_UBC_H
#define __ASM_CPU_SH2_UBC_H

#define UBC_BARA                0xffffff40
#define UBC_BAMRA               0xffffff44
#define UBC_BBRA                0xffffff48
#define UBC_BARB                0xffffff60
#define UBC_BAMRB               0xffffff64
#define UBC_BBRB                0xffffff68
#define UBC_BDRB                0xffffff70
#define UBC_BDMRB               0xffffff74
#define UBC_BRCR                0xffffff78

/*
 * We don't have any ASID changes to make in the UBC on the SH-2.
 *
 * Make these purposely invalid to track misuse.
 */
#define UBC_BASRA		0x00000000
#define UBC_BASRB		0x00000000

#endif /* __ASM_CPU_SH2_UBC_H */

