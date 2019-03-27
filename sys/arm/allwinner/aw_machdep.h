/*-
 * Copyright (c) 2015 Emmanuel Vadot <manu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef AW_MACHDEP_H
#define	AW_MACHDEP_H

#define	ALLWINNERSOC_A10	0x10000000
#define	ALLWINNERSOC_A13	0x13000000
#define	ALLWINNERSOC_A10S	0x10000001
#define	ALLWINNERSOC_A20	0x20000000
#define	ALLWINNERSOC_H3		0x30000000
#define	ALLWINNERSOC_A31	0x31000000
#define	ALLWINNERSOC_A31S	0x31000001
#define	ALLWINNERSOC_A33	0x33000000
#define	ALLWINNERSOC_A83T	0x83000000

#define	ALLWINNERSOC_SUN4I	0x40000000
#define	ALLWINNERSOC_SUN5I	0x50000000
#define	ALLWINNERSOC_SUN6I	0x60000000
#define	ALLWINNERSOC_SUN7I	0x70000000
#define	ALLWINNERSOC_SUN8I	0x80000000

u_int allwinner_soc_type(void);
u_int allwinner_soc_family(void);

#endif /* AW_MACHDEP_H */
