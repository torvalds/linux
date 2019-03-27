/*
 * $FreeBSD$
 *
 * Copyright (c) 2011-2013, 2015, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 *
 * Definitions for the Verified Executables kernel function.
 *
 */
#ifndef _DEV_VERIEXEC_VERIEXEC_IOCTL_H
#define _DEV_VERIEXEC_VERIEXEC_IOCTL_H

#include <sys/param.h>
#include <security/mac_veriexec/mac_veriexec.h>

#define	VERIEXEC_FPTYPELEN	16

struct verified_exec_params  {
	unsigned char flags;
	char fp_type[VERIEXEC_FPTYPELEN];	/* type of fingerprint */
	char file[MAXPATHLEN];
	unsigned char fingerprint[MAXFINGERPRINTLEN];
};

#define VERIEXEC_LOAD		_IOW('S', 0x1, struct verified_exec_params)
#define VERIEXEC_ACTIVE		_IO('S', 0x2)	/* start checking */
#define VERIEXEC_ENFORCE 	_IO('S', 0x3)	/* fail exec */
#define VERIEXEC_LOCK		_IO('S', 0x4)	/* don't allow new sigs */
#define VERIEXEC_DEBUG_ON	_IOWR('S', 0x5, int) /* set/get debug level */
#define VERIEXEC_DEBUG_OFF 	_IO('S', 0x6)	/* reset debug */
#define VERIEXEC_GETSTATE 	_IOR('S', 0x7, int) /* get state */
#define VERIEXEC_SIGNED_LOAD	_IOW('S', 0x8, struct verified_exec_params)

#define	_PATH_DEV_VERIEXEC	_PATH_DEV "veriexec"

#endif
