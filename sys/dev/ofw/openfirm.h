/*	$OpenBSD: openfirm.h,v 1.20 2024/11/08 12:48:00 miod Exp $	*/
/*	$NetBSD: openfirm.h,v 1.1 1996/09/30 16:35:10 ws Exp $	*/

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
 * Prototypes for OpenFirmware Interface Routines
 */

#include <sys/param.h>
#include <sys/device.h>

#define OFMAXPARAM	64

int openfirmware(void *);

extern char OF_buf[];

int OF_peer(int phandle);
int OF_child(int phandle);
int OF_parent(int phandle);
int OF_instance_to_package(int ihandle);
int OF_getproplen(int handle, char *prop);
int OF_getprop(int handle, char *prop, void *buf, int buflen);
int OF_getpropbool(int handle, char *);
uint32_t OF_getpropint(int handle, char *, uint32_t);
int OF_getpropintarray(int, char *, uint32_t *, int);
uint64_t OF_getpropint64(int handle, char *, uint64_t);
int OF_getpropint64array(int, char *, uint64_t *, int);
int OF_setprop(int, char *, const void *, int);
int OF_nextprop(int, char *, void *);
int OF_finddevice(char *name);
int OF_is_compatible(int, const char *);
int OF_call_method_1(char *method, int ihandle, int nargs, ...);
int OF_call_method(char *method, int ihandle, int nargs, int nreturns, ...);
int OF_open(char *dname);
void OF_close(int handle);
int OF_read(int handle, void *addr, int len);
int OF_write(int handle, void *addr, int len);
int OF_seek(int handle, u_quad_t pos);
void OF_boot(char *bootspec);
void OF_enter(void);
void OF_exit(void) __attribute__((__noreturn__));
int OF_interpret(char *cmd, int nreturns, ...);
#if 0
void (*OF_set_callback(void (*newfunc)(void *))) ();
#endif
int OF_getnodebyname(int, const char *);
int OF_getnodebyphandle(uint32_t);
int OF_getindex(int, const char *, const char *);

/*
 * Generic OpenFirmware probe argument.
 * This is how all probe structures must start
 * in order to support generic OpenFirmware device drivers.
 */
struct ofprobe {
	int phandle;
	/*
	 * Special unit field for disk devices.
	 * This is a KLUDGE to work around the fact that OpenFirmware
	 * doesn't probe the scsi bus completely.
	 * YES, I THINK THIS IS A BUG IN THE OPENFIRMWARE DEFINITION!!!	XXX
	 * See also ofdisk.c.
	 */
	int unit;
};

/*
 * The softc structure for devices we might be booted from (i.e. we might
 * want to set root/swap to) needs to start with these fields:		XXX
 */
struct ofb_softc {
	struct device sc_dev;
	int sc_phandle;
	int sc_unit;		/* Might be missing for non-disk devices */
};
