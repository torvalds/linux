/*-
 * Copyright (c) 2016 Andrey V. Elsukov <ae@FreeBSD.org>
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
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/sockopt.h>
#include <sys/syslog.h>
#include <sys/proc.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>

#include <netipsec/ipsec.h>
#include <netipsec/ipsec6.h>
#include <netipsec/key.h>
#include <netipsec/key_debug.h>

#include <netipsec/ipsec_support.h>

#ifdef INET
static const struct ipsec_methods ipv4_methods = {
	.input = ipsec4_input,
	.forward = ipsec4_forward,
	.output = ipsec4_output,
	.pcbctl = ipsec4_pcbctl,
	.capability = ipsec4_capability,
	.check_policy = ipsec4_in_reject,
	.hdrsize = ipsec_hdrsiz_inpcb,
	.udp_input = udp_ipsec_input,
	.udp_pcbctl = udp_ipsec_pcbctl,
};
#ifndef KLD_MODULE
static const struct ipsec_support ipv4_ipsec = {
	.enabled = IPSEC_MODULE_ENABLED,
	.methods = &ipv4_methods
};
const struct ipsec_support * const ipv4_ipsec_support = &ipv4_ipsec;
#endif /* !KLD_MODULE */
#endif /* INET */

#ifdef INET6
static const struct ipsec_methods ipv6_methods = {
	.input = ipsec6_input,
	.forward = ipsec6_forward,
	.output = ipsec6_output,
	.pcbctl = ipsec6_pcbctl,
	.capability = ipsec6_capability,
	.check_policy = ipsec6_in_reject,
	.hdrsize = ipsec_hdrsiz_inpcb,
};
#ifndef KLD_MODULE
static const struct ipsec_support ipv6_ipsec = {
	.enabled = IPSEC_MODULE_ENABLED,
	.methods = &ipv6_methods
};
const struct ipsec_support * const ipv6_ipsec_support = &ipv6_ipsec;
#endif /* !KLD_MODULE */
#endif /* INET6 */

/*
 * Always register ipsec module.
 * Even when IPsec is build in the kernel, we need to have
 * module registered. This will prevent to load ipsec.ko.
 */
static int
ipsec_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		/* All xforms are registered via SYSINIT */
		if (!ipsec_initialized())
			return (ENOMEM);
#ifdef KLD_MODULE
#ifdef INET
		ipsec_support_enable(ipv4_ipsec_support, &ipv4_methods);
#endif
#ifdef INET6
		ipsec_support_enable(ipv6_ipsec_support, &ipv6_methods);
#endif
#endif /* KLD_MODULE */
		break;
	case MOD_UNLOAD:
		/* All xforms are unregistered via SYSUNINIT */
#ifdef KLD_MODULE
#ifdef INET
		ipsec_support_disable(ipv4_ipsec_support);
#endif
#ifdef INET6
		ipsec_support_disable(ipv6_ipsec_support);
#endif
#endif /* KLD_MODULE */
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t ipsec_mod = {
	"ipsec",
	ipsec_modevent,
	0
};

DECLARE_MODULE(ipsec, ipsec_mod, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY);
MODULE_VERSION(ipsec, 1);
#ifdef KLD_MODULE
MODULE_DEPEND(ipsec, ipsec_support, 1, 1, 1);
#endif
