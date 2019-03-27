/*	$NetBSD: openfirm.h,v 1.1 1998/05/15 10:16:00 tsubai Exp $	*/

/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (C) 2000 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _OPENFIRM_H_
#define	_OPENFIRM_H_
/*
 * Prototypes for Open Firmware Interface Routines
 */

#include <sys/cdefs.h>
#include <sys/types.h>

typedef	unsigned int		ihandle_t;
typedef unsigned int		phandle_t;
typedef unsigned long int	cell_t;

extern int		(*openfirmware)(void *);
extern phandle_t	chosen;
extern ihandle_t	memory, mmu;
extern int		real_mode;

/*
 * This isn't actually an Open Firmware function, but it seemed like the right
 * place for it to go.
 */
void		OF_init(int (*openfirm)(void *));

/* Generic functions */
int		OF_test(char *);
void		OF_quiesce(); /* Disable firmware */

/* Device tree functions */
phandle_t	OF_peer(phandle_t);
phandle_t	OF_child(phandle_t);
phandle_t	OF_parent(phandle_t);
phandle_t	OF_instance_to_package(ihandle_t);
int		OF_getproplen(phandle_t, const char *);
int		OF_getprop(phandle_t, const char *, void *, int);
int		OF_nextprop(phandle_t, const char *, char *);
int		OF_setprop(phandle_t, const char *, void *, int);
int		OF_canon(const char *, char *, int);
phandle_t	OF_finddevice(const char *);
int		OF_instance_to_path(ihandle_t, char *, int);
int		OF_package_to_path(phandle_t, char *, int);
int		OF_call_method(char *, ihandle_t, int, int, ...);

/* Device I/O functions */
ihandle_t	OF_open(char *);
void		OF_close(ihandle_t);
int		OF_read(ihandle_t, void *, int);
int		OF_write(ihandle_t, void *, int);
int		OF_seek(ihandle_t, u_quad_t);
unsigned int	OF_blocks(ihandle_t);
int		OF_block_size(ihandle_t);

/* Memory functions */
void 		*OF_claim(void *, u_int, u_int);
void		OF_release(void *, u_int);

/* Control transfer functions */
void		OF_boot(char *);
void		OF_enter(void);
void		OF_exit(void) __attribute__((noreturn));
void		OF_chain(void *, u_int, void (*)(), void *, u_int);

/* Time function */
int		OF_milliseconds(void);

#endif /* _OPENFIRM_H_ */
