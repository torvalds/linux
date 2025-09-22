/*	$OpenBSD: openfirm.h,v 1.5 2019/09/02 23:40:29 kettenis Exp $	*/
/*	$NetBSD: openfirm.h,v 1.1 1997/04/16 20:29:23 thorpej Exp $	*/

/*
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
/*
 * Prototypes for Openfirmware Interface Routines
 */

#include <sys/types.h>

int OF_finddevice(char *name);
int OF_instance_to_package(int ihandle);
int OF_getprop(int handle, char *prop, void *buf, int buflen);
#ifdef	__notyet__
int OF_setprop(int handle, char *prop, void *buf, int len);
#endif
int OF_open(char *dname);
void OF_close(int handle);
int OF_write(int handle, void *addr, int len);
int OF_read(int handle, void *addr, int len);
int OF_seek(int handle, u_quad_t pos);
void *OF_claim(void *virt, u_int size, u_int align);
void OF_release(void *virt, u_int size);
int OF_milliseconds(void);
void OF_chain(void *addr, u_int size, void (*entry)(), void *parm, u_int parmlen);
int OF_call_method(char *method, int ihandle, int nargs, int nreturns, ...);
