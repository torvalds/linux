/*-
 * Copyright (c) 2017 Yandex LLC
 * Copyright (c) 2017 Andrey V. Elsukov <ae@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rwlock.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet/ip_fw.h>

#include <netpfil/ipfw/ip_fw_private.h>
#include <netpfil/ipfw/pmod/pmod.h>

static int
vnet_ipfw_pmod_init(const void *arg __unused)
{
	int error;

	error = tcpmod_init(&V_layer3_chain, IS_DEFAULT_VNET(curvnet));
	return (error);
}

static int
vnet_ipfw_pmod_uninit(const void *arg __unused)
{

	tcpmod_uninit(&V_layer3_chain, IS_DEFAULT_VNET(curvnet));
	return (0);
}

static int
ipfw_pmod_modevent(module_t mod, int type, void *unused)
{

	switch (type) {
	case MOD_LOAD:
	case MOD_UNLOAD:
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t ipfw_pmod_mod = {
	"ipfw_pmod",
	ipfw_pmod_modevent,
	0
};

/* Define startup order. */
#define	IPFW_PMOD_SI_SUB_FIREWALL	SI_SUB_PROTO_IFATTACHDOMAIN
#define	IPFW_PMOD_MODEVENT_ORDER	(SI_ORDER_ANY - 128) /* after ipfw */
#define	IPFW_PMOD_MODULE_ORDER		(IPFW_PMOD_MODEVENT_ORDER + 1)
#define	IPFW_PMOD_VNET_ORDER		(IPFW_PMOD_MODEVENT_ORDER + 2)

DECLARE_MODULE(ipfw_pmod, ipfw_pmod_mod, IPFW_PMOD_SI_SUB_FIREWALL,
    IPFW_PMOD_MODULE_ORDER);
MODULE_DEPEND(ipfw_pmod, ipfw, 3, 3, 3);
MODULE_VERSION(ipfw_pmod, 1);

VNET_SYSINIT(vnet_ipfw_pmod_init, IPFW_PMOD_SI_SUB_FIREWALL,
    IPFW_PMOD_VNET_ORDER, vnet_ipfw_pmod_init, NULL);
VNET_SYSUNINIT(vnet_ipfw_pmod_uninit, IPFW_PMOD_SI_SUB_FIREWALL,
    IPFW_PMOD_VNET_ORDER, vnet_ipfw_pmod_uninit, NULL);
