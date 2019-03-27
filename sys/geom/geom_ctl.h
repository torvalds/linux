/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Poul-Henning Kamp
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 */

#ifndef _GEOM_GEOM_CTL_H_
#define _GEOM_GEOM_CTL_H_

#include <sys/ioccom.h>

/*
 * Version number.  Used to check consistency between kernel and libgeom.
 */
#define GCTL_VERSION	2

struct gctl_req_arg {
	u_int				nlen;
	char				*name;
	off_t				offset;
	int				flag;
	int				len;
	void				*value;
	/* kernel only fields */
	void				*kvalue;
};

#define GCTL_PARAM_RD		1	/* Must match VM_PROT_READ */
#define GCTL_PARAM_WR		2	/* Must match VM_PROT_WRITE */
#define GCTL_PARAM_RW		(GCTL_PARAM_RD | GCTL_PARAM_WR)
#define GCTL_PARAM_ASCII	4

/* These are used in the kernel only */
#define GCTL_PARAM_NAMEKERNEL	8
#define GCTL_PARAM_VALUEKERNEL	16
#define GCTL_PARAM_CHANGED	32

struct gctl_req {
	u_int				version;
	u_int				serial;
	u_int				narg;
	struct gctl_req_arg		*arg;
	u_int				lerror;
	char				*error;
	struct gctl_req_table		*reqt;

	/* kernel only fields */
	int				nerror;
	struct sbuf			*serror;
};

#define GEOM_CTL	_IOW('G', GCTL_VERSION, struct gctl_req)

#define GEOM_CTL_ARG_MAX 2048	/* maximum number of parameters */

#define PATH_GEOM_CTL	"geom.ctl"

#endif /* _GEOM_GEOM_CTL_H_ */
