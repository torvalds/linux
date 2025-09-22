/*	$OpenBSD: priv.c,v 1.29 2025/08/13 10:26:31 dv Exp $	*/

/*
 * Copyright (c) 2016 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#include <net/if_bridge.h>

#include <arpa/inet.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "proc.h"
#include "vmd.h"

int	 priv_dispatch_parent(int, struct privsep_proc *, struct imsg *);
void	 priv_run(struct privsep *, struct privsep_proc *, void *);

static struct privsep_proc procs[] = {
	{ "parent",	PROC_PARENT,	priv_dispatch_parent }
};

void
priv(struct privsep *ps, struct privsep_proc *p)
{
	proc_run(ps, p, procs, nitems(procs), priv_run, NULL);
}

void
priv_run(struct privsep *ps, struct privsep_proc *p, void *arg)
{
	struct vmd		*env = ps->ps_env;

	/*
	 * no pledge(2) in the "priv" process:
	 * write ioctls are not permitted by pledge.
	 */

	/* Open our own socket for generic interface ioctls */
	if ((env->vmd_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		fatal("socket");

	/* But we need a different fd for IPv6 */
	if ((env->vmd_fd6 = socket(AF_INET6, SOCK_DGRAM, 0)) == -1)
		fatal("socket6");
}

int
priv_dispatch_parent(int fd, struct privsep_proc *p, struct imsg *imsg)
{
	const char		*desct[] = { "tap", "bridge", "veb", NULL };
	struct privsep		*ps = p->p_ps;
	struct vmop_ifreq	 vfr;
	struct vmd		*env = ps->ps_env;
	struct ifreq		 ifr;
	struct ifbreq		 ifbr;
	struct ifgroupreq	 ifgr;
	struct ifaliasreq	 ifra;
	struct in6_aliasreq	 in6_ifra;
	struct vmop_addr_req	 vareq;
	struct vmop_addr_result	 varesult;
	char			 type[IF_NAMESIZE];
	int			 ifd;
	uint32_t		 imsg_type, peer_id;
	unsigned int		 mode;

	imsg_type = imsg_get_type(imsg);
	peer_id = imsg_get_id(imsg);

	switch (imsg_type) {
	case IMSG_VMDOP_PRIV_IFDESCR:
	case IMSG_VMDOP_PRIV_IFRDOMAIN:
	case IMSG_VMDOP_PRIV_IFEXISTS:
	case IMSG_VMDOP_PRIV_IFADD:
	case IMSG_VMDOP_PRIV_IFUP:
	case IMSG_VMDOP_PRIV_IFDOWN:
	case IMSG_VMDOP_PRIV_IFGROUP:
	case IMSG_VMDOP_PRIV_IFADDR:
	case IMSG_VMDOP_PRIV_IFADDR6:
		vmop_ifreq_read(imsg, &vfr);

		/* We should not get malicious requests from the parent */
		if (priv_getiftype(vfr.vfr_name, type, NULL) == -1 ||
		    priv_findname(type, desct) == -1)
			fatalx("%s: rejected priv operation on interface: %s",
			    __func__, vfr.vfr_name);
		break;
	case IMSG_VMDOP_CONFIG:
	case IMSG_CTL_RESET:
	case IMSG_VMDOP_PRIV_GET_ADDR:
		break;
	default:
		return (-1);
	}

	switch (imsg_type) {
	case IMSG_VMDOP_PRIV_IFDESCR:
		/* Set the interface description */
		strlcpy(ifr.ifr_name, vfr.vfr_name, sizeof(ifr.ifr_name));
		ifr.ifr_data = (caddr_t)vfr.vfr_value;
		if (ioctl(env->vmd_fd, SIOCSIFDESCR, &ifr) == -1)
			log_warn("SIOCSIFDESCR");
		break;
	case IMSG_VMDOP_PRIV_IFRDOMAIN:
		strlcpy(ifr.ifr_name, vfr.vfr_name, sizeof(ifr.ifr_name));
		ifr.ifr_rdomainid = vfr.vfr_id;
		if (ioctl(env->vmd_fd, SIOCSIFRDOMAIN, &ifr) == -1)
			log_warn("SIOCSIFRDOMAIN");
		break;
	case IMSG_VMDOP_PRIV_IFADD:
		if (priv_getiftype(vfr.vfr_value, type, NULL) == -1)
			fatalx("%s: rejected to add interface: %s",
			    __func__, vfr.vfr_value);

		/* Attach the device to the bridge */
		strlcpy(ifbr.ifbr_name, vfr.vfr_name,
		    sizeof(ifbr.ifbr_name));
		strlcpy(ifbr.ifbr_ifsname, vfr.vfr_value,
		    sizeof(ifbr.ifbr_ifsname));
		if (ioctl(env->vmd_fd, SIOCBRDGADD, &ifbr) == -1 &&
		    errno != EEXIST)
			log_warn("SIOCBRDGADD");
		break;
	case IMSG_VMDOP_PRIV_IFEXISTS:
		/* Determine if bridge exists */
		strlcpy(ifr.ifr_name, vfr.vfr_name, sizeof(ifr.ifr_name));
		if (ioctl(env->vmd_fd, SIOCGIFFLAGS, &ifr) == -1)
			fatalx("%s: bridge \"%s\" does not exist",
			    __func__, vfr.vfr_name);
		break;
	case IMSG_VMDOP_PRIV_IFUP:
	case IMSG_VMDOP_PRIV_IFDOWN:
		/* Set the interface status */
		strlcpy(ifr.ifr_name, vfr.vfr_name, sizeof(ifr.ifr_name));
		if (ioctl(env->vmd_fd, SIOCGIFFLAGS, &ifr) == -1) {
			log_warn("SIOCGIFFLAGS");
			break;
		}
		if (imsg_type == IMSG_VMDOP_PRIV_IFUP)
			ifr.ifr_flags |= IFF_UP;
		else
			ifr.ifr_flags &= ~IFF_UP;
		if (ioctl(env->vmd_fd, SIOCSIFFLAGS, &ifr) == -1)
			log_warn("SIOCSIFFLAGS");
		break;
	case IMSG_VMDOP_PRIV_IFGROUP:
		if (priv_validgroup(vfr.vfr_value) == -1)
			fatalx("%s: invalid group name", __func__);

		if (strlcpy(ifgr.ifgr_name, vfr.vfr_name,
		    sizeof(ifgr.ifgr_name)) >= sizeof(ifgr.ifgr_name) ||
		    strlcpy(ifgr.ifgr_group, vfr.vfr_value,
		    sizeof(ifgr.ifgr_group)) >= sizeof(ifgr.ifgr_group))
			fatalx("%s: group name too long", __func__);

		if (ioctl(env->vmd_fd, SIOCAIFGROUP, &ifgr) == -1 &&
		    errno != EEXIST)
			log_warn("SIOCAIFGROUP");
		break;
	case IMSG_VMDOP_PRIV_IFADDR:
		memset(&ifra, 0, sizeof(ifra));

		if (vfr.vfr_addr.ss_family != AF_INET ||
		    vfr.vfr_addr.ss_family != vfr.vfr_mask.ss_family)
			fatalx("%s: invalid address family", __func__);

		/* Set the interface address */
		strlcpy(ifra.ifra_name, vfr.vfr_name, sizeof(ifra.ifra_name));

		memcpy(&ifra.ifra_addr, &vfr.vfr_addr,
		    sizeof(ifra.ifra_addr));
		memcpy(&ifra.ifra_mask, &vfr.vfr_mask,
		    sizeof(ifra.ifra_mask));

		if (ioctl(env->vmd_fd, SIOCAIFADDR, &ifra) == -1)
			log_warn("SIOCAIFADDR");
		break;
	case IMSG_VMDOP_PRIV_IFADDR6:
		memset(&in6_ifra, 0, sizeof(in6_ifra));

		if (vfr.vfr_addr.ss_family != AF_INET6 ||
		    vfr.vfr_addr.ss_family != vfr.vfr_mask.ss_family)
			fatalx("%s: invalid address family", __func__);

		/* Set the interface address */
		strlcpy(in6_ifra.ifra_name, vfr.vfr_name,
		    sizeof(in6_ifra.ifra_name));

		memcpy(&in6_ifra.ifra_addr, &vfr.vfr_addr,
		    sizeof(in6_ifra.ifra_addr));
		memcpy(&in6_ifra.ifra_prefixmask, &vfr.vfr_mask,
		    sizeof(in6_ifra.ifra_prefixmask));
		in6_ifra.ifra_prefixmask.sin6_scope_id = 0;

		in6_ifra.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
		in6_ifra.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;

		if (ioctl(env->vmd_fd6, SIOCDIFADDR_IN6, &in6_ifra) == -1 &&
		    errno != EADDRNOTAVAIL)
			log_warn("SIOCDIFADDR_IN6");

		if (ioctl(env->vmd_fd6, SIOCAIFADDR_IN6, &in6_ifra) == -1)
			log_warn("SIOCAIFADDR_IN6");
		break;
	case IMSG_VMDOP_PRIV_GET_ADDR:
		vmop_addr_req_read(imsg, &vareq);

		varesult.var_vmid = vareq.var_vmid;
		varesult.var_nic_idx = vareq.var_nic_idx;

		ifd = imsg_get_fd(imsg);
		/* resolve lladdr for the tap(4) and send back to parent */
		if (ioctl(ifd, SIOCGIFADDR, &varesult.var_addr) != 0)
			log_warn("SIOCGIFADDR");
		else
			proc_compose_imsg(ps, PROC_PARENT,
			    IMSG_VMDOP_PRIV_GET_ADDR_RESPONSE, peer_id,
			    -1, &varesult, sizeof(varesult));
		close(ifd);
		break;
	case IMSG_VMDOP_CONFIG:
		config_getconfig(env, imsg);
		break;
	case IMSG_CTL_RESET:
		mode = imsg_uint_read(imsg);
		config_purge(env, mode);
		break;
	default:
		return (-1);
	}

	return (0);
}

int
priv_getiftype(char *ifname, char *type, unsigned int *unitptr)
{
	const char	*errstr;
	size_t		 span;
	unsigned int	 unit;

	/* Extract the name part */
	span = strcspn(ifname, "0123456789");
	if (span == 0 || span >= strlen(ifname) || span >= (IF_NAMESIZE - 1))
		return (-1);
	memcpy(type, ifname, span);
	type[span] = 0;

	/* Now parse the unit (we don't strictly validate the format here) */
	unit = strtonum(ifname + span, 0, UINT_MAX, &errstr);
	if (errstr != NULL)
		return (-1);
	if (unitptr != NULL)
		*unitptr = unit;

	return (0);
}

int
priv_findname(const char *name, const char **names)
{
	unsigned int	 i;

	for (i = 0; names[i] != NULL; i++) {
		if (strcmp(name, names[i]) == 0)
			return (0);
	}

	return (-1);
}

int
priv_validgroup(const char *name)
{
	const size_t len = strnlen(name, IF_NAMESIZE);

	if (len == IF_NAMESIZE)
		return (-1);
	/* Group can not end with a digit */
	if (len > 0 && isdigit((unsigned char)name[len - 1]))
		return (-1);
	return (0);
}

/*
 * Called from the Parent process to setup vm interface(s)
 * - ensure the interface has the description set (tracking purposes)
 * - if interface is to be attached to a switch, attach it
 * - check if rdomain is set on interface and switch
 *   - if interface only or both, use interface rdomain
 *   - if switch only, use switch rdomain
 * - check if group is set on interface and switch
 *   - if interface, add it
 *   - if switch, add it
 * - ensure the interface is up/down
 * - if local interface, set address
 */
int
vm_priv_ifconfig(struct privsep *ps, struct vmd_vm *vm)
{
	char			 name[64];
	struct vmd		*env = ps->ps_env;
	struct vm_create_params	*vcp = &vm->vm_params.vmc_params;
	struct vmd_if		*vif;
	struct vmd_switch	*vsw;
	unsigned int		 i;
	struct vmop_ifreq	 vfr, vfbr;
	struct sockaddr_in	*sin4;
	struct sockaddr_in6	*sin6;

	for (i = 0; i < VM_MAX_NICS_PER_VM; i++) {
		vif = &vm->vm_ifs[i];

		if (vif->vif_name == NULL)
			break;

		memset(&vfr, 0, sizeof(vfr));
		if (strlcpy(vfr.vfr_name, vif->vif_name,
		    sizeof(vfr.vfr_name)) >= sizeof(vfr.vfr_name))
			return (-1);

		/* Description can be truncated */
		(void)snprintf(vfr.vfr_value, sizeof(vfr.vfr_value),
		    "vm%u-if%u-%s", vm->vm_vmid, i, vcp->vcp_name);

		log_debug("%s: interface %s description %s", __func__,
		    vfr.vfr_name, vfr.vfr_value);

		proc_compose(ps, PROC_PRIV, IMSG_VMDOP_PRIV_IFDESCR,
		    &vfr, sizeof(vfr));

		/* set default rdomain */
		vfr.vfr_id = getrtable();

		vsw = switch_getbyname(vif->vif_switch);

		/* Check if switch should exist */
		if (vsw == NULL && vif->vif_switch != NULL)
			log_warnx("switch \"%s\" not found", vif->vif_switch);

		/* Add interface to switch and set proper rdomain */
		if (vsw != NULL) {
			memset(&vfbr, 0, sizeof(vfbr));

			if (strlcpy(vfbr.vfr_name, vsw->sw_ifname,
			    sizeof(vfbr.vfr_name)) >= sizeof(vfbr.vfr_name))
				return (-1);
			if (strlcpy(vfbr.vfr_value, vif->vif_name,
			    sizeof(vfbr.vfr_value)) >= sizeof(vfbr.vfr_value))
				return (-1);

			log_debug("%s: switch \"%s\" interface %s add %s",
			    __func__, vsw->sw_name, vfbr.vfr_name,
			    vfbr.vfr_value);

			proc_compose(ps, PROC_PRIV, IMSG_VMDOP_PRIV_IFADD,
			    &vfbr, sizeof(vfbr));

			/* Check rdomain properties */
			if (vif->vif_flags & VMIFF_RDOMAIN)
				vfr.vfr_id = vif->vif_rdomain;
			else if (vsw->sw_flags & VMIFF_RDOMAIN)
				vfr.vfr_id = vsw->sw_rdomain;
		} else {
			/* No switch to attach case */
			if (vif->vif_flags & VMIFF_RDOMAIN)
				vfr.vfr_id = vif->vif_rdomain;
		}

		/* Set rdomain on interface */
		if (vfr.vfr_id != 0)
			log_debug("%s: interface %s rdomain %u", __func__,
			    vfr.vfr_name, vfr.vfr_id);

		proc_compose(ps, PROC_PRIV, IMSG_VMDOP_PRIV_IFRDOMAIN,
		    &vfr, sizeof(vfr));

		/* First group is defined per-interface */
		if (vif->vif_group) {
			if (strlcpy(vfr.vfr_value, vif->vif_group,
			    sizeof(vfr.vfr_value)) >= sizeof(vfr.vfr_value))
				return (-1);

			log_debug("%s: interface %s group %s", __func__,
			    vfr.vfr_name, vfr.vfr_value);

			proc_compose(ps, PROC_PRIV, IMSG_VMDOP_PRIV_IFGROUP,
			    &vfr, sizeof(vfr));
		}

		/* The second group is defined per-switch */
		if (vsw != NULL && vsw->sw_group != NULL) {
			if (strlcpy(vfr.vfr_value, vsw->sw_group,
			    sizeof(vfr.vfr_value)) >= sizeof(vfr.vfr_value))
				return (-1);

			log_debug("%s: interface %s group %s switch \"%s\"",
			    __func__, vfr.vfr_name, vfr.vfr_value,
			    vsw->sw_name);

			proc_compose(ps, PROC_PRIV, IMSG_VMDOP_PRIV_IFGROUP,
			    &vfr, sizeof(vfr));
		}

		/* Set the new interface status to up or down */
		proc_compose(ps, PROC_PRIV, (vif->vif_flags & VMIFF_UP) ?
		    IMSG_VMDOP_PRIV_IFUP : IMSG_VMDOP_PRIV_IFDOWN,
		    &vfr, sizeof(vfr));

		/* Set interface address if it is a local interface */
		if (vm->vm_params.vmc_ifflags[i] & VMIFF_LOCAL) {
			memset(&vfr.vfr_mask, 0, sizeof(vfr.vfr_mask));
			memset(&vfr.vfr_addr, 0, sizeof(vfr.vfr_addr));

			/* local IPv4 address with a /31 mask */
			sin4 = (struct sockaddr_in *)&vfr.vfr_mask;
			sin4->sin_family = AF_INET;
			sin4->sin_len = sizeof(*sin4);
			sin4->sin_addr.s_addr = htonl(0xfffffffe);

			sin4 = (struct sockaddr_in *)&vfr.vfr_addr;
			sin4->sin_family = AF_INET;
			sin4->sin_len = sizeof(*sin4);
			if ((sin4->sin_addr.s_addr =
			    vm_priv_addr(&env->vmd_cfg.cfg_localprefix,
			    vm->vm_vmid, i, 0)) == 0)
				return (-1);

			inet_ntop(AF_INET, &sin4->sin_addr,
			    name, sizeof(name));
			log_debug("%s: interface %s address %s/31",
			    __func__, vfr.vfr_name, name);

			proc_compose(ps, PROC_PRIV, IMSG_VMDOP_PRIV_IFADDR,
			    &vfr, sizeof(vfr));
		}
		if ((vm->vm_params.vmc_ifflags[i] & VMIFF_LOCAL) &&
		    (env->vmd_cfg.cfg_flags & VMD_CFG_INET6)) {
			memset(&vfr.vfr_mask, 0, sizeof(vfr.vfr_mask));
			memset(&vfr.vfr_addr, 0, sizeof(vfr.vfr_addr));

			/* local IPv6 address with a /96 mask */
			sin6 = ss2sin6(&vfr.vfr_mask);
			sin6->sin6_family = AF_INET6;
			sin6->sin6_len = sizeof(*sin6);
			memset(&sin6->sin6_addr.s6_addr[0], 0xff, 12);
			memset(&sin6->sin6_addr.s6_addr[12], 0, 4);

			sin6 = ss2sin6(&vfr.vfr_addr);
			sin6->sin6_family = AF_INET6;
			sin6->sin6_len = sizeof(*sin6);
			if (vm_priv_addr6(&env->vmd_cfg.cfg_localprefix,
			    vm->vm_vmid, i, 0, &sin6->sin6_addr) == -1)
				return (-1);

			inet_ntop(AF_INET6, &sin6->sin6_addr,
			    name, sizeof(name));
			log_debug("%s: interface %s address %s/96",
			    __func__, vfr.vfr_name, name);

			proc_compose(ps, PROC_PRIV, IMSG_VMDOP_PRIV_IFADDR6,
			    &vfr, sizeof(vfr));
		}
	}

	return (0);
}

/*
 * Called from the Parent process to setup underlying switch interface
 * - ensure the interface exists
 * - ensure the interface has the correct rdomain set
 * - ensure the interface has the description set (tracking purposes)
 * - ensure the interface is up/down
 */
int
vm_priv_brconfig(struct privsep *ps, struct vmd_switch *vsw)
{
	struct vmop_ifreq	 vfr;

	memset(&vfr, 0, sizeof(vfr));

	if (strlcpy(vfr.vfr_name, vsw->sw_ifname,
	    sizeof(vfr.vfr_name)) >= sizeof(vfr.vfr_name))
		return (-1);

	/* ensure bridge exists */
	proc_compose(ps, PROC_PRIV, IMSG_VMDOP_PRIV_IFEXISTS,
	    &vfr, sizeof(vfr));

	/* Use the configured rdomain or get it from the process */
	if (vsw->sw_flags & VMIFF_RDOMAIN)
		vfr.vfr_id = vsw->sw_rdomain;
	else
		vfr.vfr_id = getrtable();
	if (vfr.vfr_id != 0)
		log_debug("%s: interface %s rdomain %u", __func__,
		    vfr.vfr_name, vfr.vfr_id);

	/* ensure bridge has the correct rdomain */
	proc_compose(ps, PROC_PRIV, IMSG_VMDOP_PRIV_IFRDOMAIN,
	    &vfr, sizeof(vfr));

	/* Description can be truncated */
	(void)snprintf(vfr.vfr_value, sizeof(vfr.vfr_value),
	    "switch%u-%s", vsw->sw_id, vsw->sw_name);

	log_debug("%s: interface %s description %s", __func__,
	    vfr.vfr_name, vfr.vfr_value);

	proc_compose(ps, PROC_PRIV, IMSG_VMDOP_PRIV_IFDESCR,
	    &vfr, sizeof(vfr));

	/* Set the new interface status to up or down */
	proc_compose(ps, PROC_PRIV, (vsw->sw_flags & VMIFF_UP) ?
	    IMSG_VMDOP_PRIV_IFUP : IMSG_VMDOP_PRIV_IFDOWN,
	    &vfr, sizeof(vfr));

	vsw->sw_running = 1;
	return (0);
}

uint32_t
vm_priv_addr(struct local_prefix *p, uint32_t vmid, int idx, int isvm)
{
	in_addr_t		 addr;

	/* Encode the VM ID as a per-VM subnet range N, 100.64.N.0/24. */
	addr = vmid << 8;

	/*
	 * Assign a /31 subnet M per VM interface, 100.64.N.M/31.
	 * Each subnet contains exactly two IP addresses; skip the
	 * first subnet to avoid a gateway address ending with .0.
	 */
	addr |= (idx + 1) * 2;

	/* Use the first address for the gateway, the second for the VM. */
	if (isvm)
		addr++;

	/* Convert to network byte order and add the prefix. */
	addr = htonl(addr) | p->lp_in.s_addr;

	/*
	 * Validate the results:
	 * - the address should not exceed the prefix (eg. VM ID to high).
	 * - up to 126 interfaces can be encoded per VM.
	 */
	if (p->lp_in.s_addr != (addr & p->lp_mask.s_addr) || idx >= 0x7f) {
		log_warnx("%s: dhcp address range exceeded,"
		    " vm id %u interface %d", __func__, vmid, idx);
		return (0);
	}

	return (addr);
}

int
vm_priv_addr6(struct local_prefix *p, uint32_t vmid, int idx, int isvm,
    struct in6_addr *out)
{
	struct in6_addr		 addr;
	in_addr_t		 addr4;

	/* Start with the IPv6 prefix. */
	memcpy(&addr, &p->lp_in6, sizeof(addr));

	/* Encode the VM IPv4 address as subnet, fd00::NN:NN:0:0/96. */
	if ((addr4 = vm_priv_addr(p, vmid, idx, 1)) == 0)
		return (0);
	memcpy(&addr.s6_addr[8], &addr4, sizeof(addr4));

	/*
	 * Set the last octet to 1 (host) or 2 (VM).
	 * The latter is currently not used inside vmd as we don't
	 * answer rtsol requests ourselves.
	 */
	if (!isvm)
		addr.s6_addr[15] = 1;
	else
		addr.s6_addr[15] = 2;

	memcpy(out, &addr, sizeof(*out));

	return (0);
}
