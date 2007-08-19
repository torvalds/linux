/*
 * include/asm-sh/cpu-sh3/ubc.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 * Copyright (C) 2003 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH3_UBC_H
#define __ASM_CPU_SH3_UBC_H

#if defined(CONFIG_CPU_SUBTYPE_SH7710) || \
    defined(CONFIG_CPU_SUBTYPE_SH7720)
#define UBC_BARA		0xa4ffffb0
#define UBC_BAMRA		0xa4ffffb4
#define UBC_BBRA		0xa4ffffb8
#define UBC_BASRA		0xffffffe4
#define UBC_BARB		0xa4ffffa0
#define UBC_BAMRB		0xa4ffffa4
#define UBC_BBRB		0xa4ffffa8
#define UBC_BASRB		0xffffffe8
#define UBC_BDRB		0xa4ffff90
#define UBC_BDMRB		0xa4ffff94
#define UBC_BRCR		0xa4ffff98
#else
#define UBC_BARA                0xffffffb0
#define UBC_BAMRA               0xffffffb4
#define UBC_BBRA                0xffffffb8
#define UBC_BASRA               0xffffffe4
#define UBC_BARB                0xffffffa0
#define UBC_BAMRB               0xffffffa4
#define UBC_BBRB                0xffffffa8
#define UBC_BASRB               0xffffffe8
#define UBC_BDRB                0xffffff90
#define UBC_BDMRB               0xffffff94
#define UBC_BRCR                0xffffff98
#endif

#endif /* __ASM_CPU_SH3_UBC_H */
