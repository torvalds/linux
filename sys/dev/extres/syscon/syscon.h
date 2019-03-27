/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Kyle Evans <kevans@FreeBSD.org>
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef DEV_SYSCON_H
#define DEV_SYSCON_H

#include "opt_platform.h"

#include <sys/types.h>
#include <sys/kobj.h>
#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#endif

struct syscon {
	KOBJ_FIELDS;

	TAILQ_ENTRY(syscon)	syscon_link;   /* Global list entry */

	device_t		pdev;		/* provider device */
#ifdef FDT
	phandle_t		ofw_node;	/* OFW node for syscon */
#endif
	void			*softc;		/* provider softc */
};

/*
 * Shorthands for constructing method tables.
 */
#define	SYSCONMETHOD		KOBJMETHOD
#define	SYSCONMETHOD_END	KOBJMETHOD_END
#define	syscon_method_t		kobj_method_t
#define	syscon_class_t		kobj_class_t
DECLARE_CLASS(syscon_class);

void *syscon_get_softc(struct syscon *syscon);

/*
 * Provider interface
 */
struct syscon *syscon_create(device_t pdev, syscon_class_t syscon_class);
struct syscon *syscon_register(struct syscon *syscon);
int syscon_unregister(struct syscon *syscon);

#ifdef FDT
struct syscon *syscon_create_ofw_node(device_t pdev,
    syscon_class_t syscon_class, phandle_t node);
phandle_t syscon_get_ofw_node(struct syscon *syscon);
int syscon_get_by_ofw_property(device_t consumer, phandle_t node, char *name,
    struct syscon **syscon);
#endif

#endif /* DEV_SYSCON_H */
