/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acgccex.h - Extra GCC specific defines, etc.
 *
 * Copyright (C) 2000 - 2025, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACGCCEX_H__
#define __ACGCCEX_H__

/*
 * Some versions of gcc implement strchr() with a buggy macro. So,
 * undef it here. Prevents error messages of this form (usually from the
 * file getopt.c):
 *
 * error: logical '&&' with non-zero constant will always evaluate as true
 */
#ifdef strchr
#undef strchr
#endif

#endif				/* __ACGCCEX_H__ */
