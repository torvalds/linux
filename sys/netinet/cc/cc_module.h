/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 Lawrence Stewart <lstewart@freebsd.org>
 * All rights reserved.
 *
 * This software was developed by Lawrence Stewart while studying at the Centre
 * for Advanced Internet Architectures, Swinburne University of Technology, made
 * possible in part by a grant from the Cisco University Research Program Fund
 * at Community Foundation Silicon Valley.
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
 * This software was first released in 2009 by Lawrence Stewart as part of the
 * NewTCP research project at Swinburne University of Technology's Centre for
 * Advanced Internet Architectures, Melbourne, Australia, which was made
 * possible in part by a grant from the Cisco University Research Program Fund
 * at Community Foundation Silicon Valley. More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 */

#ifndef _NETINET_CC_MODULE_H_
#define _NETINET_CC_MODULE_H_

/*
 * Allows a CC algorithm to manipulate a commonly named CC variable regardless
 * of the transport protocol and associated C struct.
 * XXXLAS: Out of action until the work to support SCTP is done.
 *
#define	CCV(ccv, what)							\
(*(									\
	(ccv)->type == IPPROTO_TCP ?	&(ccv)->ccvc.tcp->what :	\
					&(ccv)->ccvc.sctp->what		\
))
 */
#define	CCV(ccv, what) (ccv)->ccvc.tcp->what

#define	DECLARE_CC_MODULE(ccname, ccalgo) 				\
	static moduledata_t cc_##ccname = {				\
		.name = #ccname,					\
		.evhand = cc_modevent,					\
		.priv = ccalgo						\
	};								\
	DECLARE_MODULE(ccname, cc_##ccname,				\
	    SI_SUB_PROTO_IFATTACHDOMAIN, SI_ORDER_ANY)

int	cc_modevent(module_t mod, int type, void *data);

#endif /* _NETINET_CC_MODULE_H_ */
