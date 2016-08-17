/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 1998-2007, Brian Gladman, Worcester, UK. All rights reserved.
 *
 * LICENSE TERMS
 *
 * The free distribution and use of this software is allowed (with or without
 * changes) provided that:
 *
 *  1. source code distributions include the above copyright notice, this
 *     list of conditions and the following disclaimer;
 *
 *  2. binary distributions include the above copyright notice, this list
 *     of conditions and the following disclaimer in their documentation;
 *
 *  3. the name of the copyright holder is not used to endorse products
 *     built using this software without specific written permission.
 *
 * DISCLAIMER
 *
 * This software is provided 'as is' with no explicit or implied warranties
 * in respect of its properties, including, but not limited to, correctness
 * and/or fitness for purpose.
 * ---------------------------------------------------------------------------
 * Issue Date: 20/12/2007
 *
 * This file contains the code for declaring the tables needed to implement
 * AES. The file aesopt.h is assumed to be included before this header file.
 * If there are no global variables, the definitions here can be used to put
 * the AES tables in a structure so that a pointer can then be added to the
 * AES context to pass them to the AES routines that need them.   If this
 * facility is used, the calling program has to ensure that this pointer is
 * managed appropriately.  In particular, the value of the t_dec(in, it) item
 * in the table structure must be set to zero in order to ensure that the
 * tables are initialised. In practice the three code sequences in aeskey.c
 * that control the calls to aes_init() and the aes_init() routine itself will
 * have to be changed for a specific implementation. If global variables are
 * available it will generally be preferable to use them with the precomputed
 * FIXED_TABLES option that uses static global tables.
 *
 * The following defines can be used to control the way the tables
 * are defined, initialised and used in embedded environments that
 * require special features for these purposes
 *
 *    the 't_dec' construction is used to declare fixed table arrays
 *    the 't_set' construction is used to set fixed table values
 *    the 't_use' construction is used to access fixed table values
 *
 *    256 byte tables:
 *
 *        t_xxx(s, box)    => forward S box
 *        t_xxx(i, box)    => inverse S box
 *
 *    256 32-bit word OR 4 x 256 32-bit word tables:
 *
 *        t_xxx(f, n)      => forward normal round
 *        t_xxx(f, l)      => forward last round
 *        t_xxx(i, n)      => inverse normal round
 *        t_xxx(i, l)      => inverse last round
 *        t_xxx(l, s)      => key schedule table
 *        t_xxx(i, m)      => key schedule table
 *
 *    Other variables and tables:
 *
 *        t_xxx(r, c)      => the rcon table
 */

/*
 * OpenSolaris OS modifications
 *
 * 1. Added __cplusplus and _AESTAB_H header guards
 * 2. Added header file sys/types.h
 * 3. Remove code defined for _MSC_VER
 * 4. Changed all variables to "static const"
 * 5. Changed uint_8t and uint_32t to uint8_t and uint32_t
 * 6. Cstyled and hdrchk code
 */

#ifndef _AESTAB_H
#define	_AESTAB_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define	t_dec(m, n) t_##m##n
#define	t_set(m, n) t_##m##n
#define	t_use(m, n) t_##m##n

#if defined(DO_TABLES) && defined(FIXED_TABLES)
#define	d_1(t, n, b, e)		 static const t n[256]    =   b(e)
#define	d_4(t, n, b, e, f, g, h) static const t n[4][256] = \
					{b(e), b(f), b(g), b(h)}
static const uint32_t t_dec(r, c)[RC_LENGTH] = rc_data(w0);
#else
#define	d_1(t, n, b, e)			static const t n[256]
#define	d_4(t, n, b, e, f, g, h)	static const t n[4][256]
static const uint32_t t_dec(r, c)[RC_LENGTH];
#endif

#if defined(SBX_SET)
	d_1(uint8_t, t_dec(s, box), sb_data, h0);
#endif
#if defined(ISB_SET)
	d_1(uint8_t, t_dec(i, box), isb_data, h0);
#endif

#if defined(FT1_SET)
	d_1(uint32_t, t_dec(f, n), sb_data, u0);
#endif
#if defined(FT4_SET)
	d_4(uint32_t, t_dec(f, n), sb_data, u0, u1, u2, u3);
#endif

#if defined(FL1_SET)
	d_1(uint32_t, t_dec(f, l), sb_data, w0);
#endif
#if defined(FL4_SET)
	d_4(uint32_t, t_dec(f, l), sb_data, w0, w1, w2, w3);
#endif

#if defined(IT1_SET)
	d_1(uint32_t, t_dec(i, n), isb_data, v0);
#endif
#if defined(IT4_SET)
	d_4(uint32_t, t_dec(i, n), isb_data, v0, v1, v2, v3);
#endif

#if defined(IL1_SET)
	d_1(uint32_t, t_dec(i, l), isb_data, w0);
#endif
#if defined(IL4_SET)
	d_4(uint32_t, t_dec(i, l), isb_data, w0, w1, w2, w3);
#endif

#if defined(LS1_SET)
#if defined(FL1_SET)
#undef  LS1_SET
#else
	d_1(uint32_t, t_dec(l, s), sb_data, w0);
#endif
#endif

#if defined(LS4_SET)
#if defined(FL4_SET)
#undef  LS4_SET
#else
	d_4(uint32_t, t_dec(l, s), sb_data, w0, w1, w2, w3);
#endif
#endif

#if defined(IM1_SET)
	d_1(uint32_t, t_dec(i, m), mm_data, v0);
#endif
#if defined(IM4_SET)
	d_4(uint32_t, t_dec(i, m), mm_data, v0, v1, v2, v3);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _AESTAB_H */
