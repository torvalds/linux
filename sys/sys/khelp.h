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

/*
 * A KPI for managing kernel helper modules which perform useful functionality
 * within the kernel. Originally released as part of the NewTCP research project
 * at Swinburne University of Technology's Centre for Advanced Internet
 * Architectures, Melbourne, Australia, which was made possible in part by a
 * grant from the Cisco University Research Program Fund at Community Foundation
 * Silicon Valley. More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 */

#ifndef	_SYS_KHELP_H_
#define	_SYS_KHELP_H_

struct helper;
struct hookinfo;
struct osd;

/* Helper classes. */
#define	HELPER_CLASS_TCP	0x00000001
#define	HELPER_CLASS_SOCKET	0x00000002

/* Public KPI functions. */
int	khelp_register_helper(struct helper *h);

int	khelp_deregister_helper(struct helper *h);

int	khelp_init_osd(uint32_t classes, struct osd *hosd);

int	khelp_destroy_osd(struct osd *hosd);

void *	khelp_get_osd(struct osd *hosd, int32_t id);

int32_t	khelp_get_id(char *hname);

int	khelp_add_hhook(struct hookinfo *hki, uint32_t flags);

int	khelp_remove_hhook(struct hookinfo *hki);

#endif /* _SYS_KHELP_H_ */
