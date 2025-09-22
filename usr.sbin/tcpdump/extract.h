/*	$OpenBSD: extract.h,v 1.10 2021/09/16 12:34:12 visa Exp $	*/

/*
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @(#) $Id: extract.h,v 1.10 2021/09/16 12:34:12 visa Exp $ (LBL)
 */

/* Network to host order macros */

#define EXTRACT_16BITS(p) \
	((u_int16_t)*((const u_int8_t *)(p) + 0) << 8 | \
	(u_int16_t)*((const u_int8_t *)(p) + 1))

#define EXTRACT_32BITS(p) \
	((u_int32_t)*((const u_int8_t *)(p) + 0) << 24 | \
	(u_int32_t)*((const u_int8_t *)(p) + 1) << 16 | \
	(u_int32_t)*((const u_int8_t *)(p) + 2) << 8 | \
	(u_int32_t)*((const u_int8_t *)(p) + 3))

#define EXTRACT_24BITS(p) \
	((u_int32_t)*((const u_int8_t *)(p) + 0) << 16 | \
	(u_int32_t)*((const u_int8_t *)(p) + 1) << 8 | \
	(u_int32_t)*((const u_int8_t *)(p) + 2))

/* Little endian protocol host order macros */

#define EXTRACT_LE_8BITS(p) (*(p))
#define EXTRACT_LE_16BITS(p) \
	((u_int16_t)*((const u_int8_t *)(p) + 1) << 8 | \
	(u_int16_t)*((const u_int8_t *)(p) + 0))
#define EXTRACT_LE_32BITS(p) \
	((u_int32_t)*((const u_int8_t *)(p) + 3) << 24 | \
	(u_int32_t)*((const u_int8_t *)(p) + 2) << 16 | \
	(u_int32_t)*((const u_int8_t *)(p) + 1) << 8 | \
	(u_int32_t)*((const u_int8_t *)(p) + 0))
#define EXTRACT_LE_64BITS(p) \
	((u_int64_t)*((const u_int8_t *)(p) + 7) << 56 | \
	(u_int64_t)*((const u_int8_t *)(p) + 6) << 48 | \
	(u_int64_t)*((const u_int8_t *)(p) + 5) << 40 | \
	(u_int64_t)*((const u_int8_t *)(p) + 4) << 32 | \
	(u_int64_t)*((const u_int8_t *)(p) + 3) << 24 | \
	(u_int64_t)*((const u_int8_t *)(p) + 2) << 16 | \
	(u_int64_t)*((const u_int8_t *)(p) + 1) << 8 | \
	(u_int64_t)*((const u_int8_t *)(p) + 0))
