/*-
 * Copyright (c) 2014, 2015 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_PROTO_H_
#define _DEV_PROTO_H_

#define	PROTO_RES_MAX	16

#define	PROTO_RES_UNUSED	0
#define	PROTO_RES_PCICFG	10
#define	PROTO_RES_BUSDMA	11

struct proto_res {
	int		r_type;
	int		r_rid;
	union {
		struct resource *res;
		void *busdma;
	} r_d;
	u_long		r_size;
	union {
		void		*cookie;
		struct cdev	*cdev;
	} r_u;
	uintptr_t	r_opened;
};

struct proto_softc {
	device_t	sc_dev;
	struct proto_res sc_res[PROTO_RES_MAX];
	int		sc_rescnt;
};

extern devclass_t proto_devclass;
extern char proto_driver_name[];

int proto_add_resource(struct proto_softc *, int, int, struct resource *);

int proto_probe(device_t dev, const char *prefix, char ***devnamesp);
int proto_attach(device_t dev);
int proto_detach(device_t dev);

#endif /* _DEV_PROTO_H_ */
