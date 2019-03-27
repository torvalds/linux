/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Lawrence Stewart <lstewart@freebsd.org>
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Lawrence Stewart while studying at the Centre
 * for Advanced Internet Architectures, Swinburne University of Technology, made
 * possible in part by grants from the FreeBSD Foundation and Cisco University
 * Research Program Fund at Community Foundation Silicon Valley.
 *
 * Portions of this software were developed at the Centre for Advanced
 * Internet Architectures, Swinburne University of Technology, Melbourne,
 * Australia by Lawrence Stewart under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _SYS_MODULE_KHELP_H_
#define _SYS_MODULE_KHELP_H_

/* XXXLAS: Needed for uma related typedefs. */
#include <vm/uma.h>

/* Helper flags. */
#define	HELPER_NEEDS_OSD	0x0001

struct helper {
	int (*mod_init) (void);
	int (*mod_destroy) (void);
#define	HELPER_NAME_MAXLEN 16
	char			h_name[HELPER_NAME_MAXLEN];
	uma_zone_t		h_zone;
	struct hookinfo		*h_hooks;
	uint32_t		h_nhooks;
	uint32_t		h_classes;
	int32_t			h_id;
	volatile uint32_t	h_refcount;
	uint16_t		h_flags;
	TAILQ_ENTRY(helper)	h_next;
};

struct khelp_modevent_data {
	char			name[HELPER_NAME_MAXLEN];
	struct helper		*helper;
	struct hookinfo		*hooks;
	int			nhooks;
	int			uma_zsize;
	uma_ctor		umactor;
	uma_dtor		umadtor;
};

#define	KHELP_DECLARE_MOD_UMA(hname, hdata, hhooks, version, size, ctor, dtor) \
	static struct khelp_modevent_data kmd_##hname = {		\
		.name = #hname,						\
		.helper = hdata,					\
		.hooks = hhooks,					\
		.nhooks = sizeof(hhooks) / sizeof(hhooks[0]),		\
		.uma_zsize = size,					\
		.umactor = ctor,					\
		.umadtor = dtor						\
	};								\
	static moduledata_t h_##hname = {				\
		.name = #hname,						\
		.evhand = khelp_modevent,				\
		.priv = &kmd_##hname					\
	};								\
	DECLARE_MODULE(hname, h_##hname, SI_SUB_KLD, SI_ORDER_ANY);	\
	MODULE_VERSION(hname, version)

#define	KHELP_DECLARE_MOD(hname, hdata, hhooks, version)		\
	KHELP_DECLARE_MOD_UMA(hname, hdata, hhooks, version, 0, NULL, NULL)

int	khelp_modevent(module_t mod, int type, void *data);

#endif /* _SYS_MODULE_KHELP_H_ */
