/*
 * Copyright (c) 2017, Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * thislist of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_lagg.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <netinet/ip_carp.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>

#include <err.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libifconfig.h>

static const char *carp_states[] = { CARP_STATES };

static void
print_carp(ifconfig_handle_t *lifh, struct ifaddrs *ifa)
{
	struct carpreq carpr[CARP_MAXVHID];
	int i;

	if (ifconfig_carp_get_info(lifh, ifa->ifa_name, carpr, CARP_MAXVHID)) {
		return; /* Probably not configured on this interface */
	}
	for (i = 0; i < carpr[0].carpr_count; i++) {
		printf("\tcarp: %s vhid %d advbase %d advskew %d",
		    carp_states[carpr[i].carpr_state], carpr[i].carpr_vhid,
		    carpr[i].carpr_advbase, carpr[i].carpr_advskew);
		printf("\n");
	}
}

static void
print_inet4_addr(ifconfig_handle_t *lifh, struct ifaddrs *ifa)
{
	struct ifconfig_inet_addr addr;
	char addr_buf[NI_MAXHOST];

	if (ifconfig_inet_get_addrinfo(lifh, ifa->ifa_name, ifa, &addr) != 0) {
		return;
	}

	inet_ntop(AF_INET, &addr.sin->sin_addr, addr_buf, sizeof(addr_buf));
	printf("\tinet %s", addr_buf);

	if (addr.dst) {
		printf(" --> %s", inet_ntoa(addr.dst->sin_addr));
	}

	printf(" netmask 0x%x ", ntohl(addr.netmask->sin_addr.s_addr));

	if ((addr.broadcast != NULL) &&
	    (addr.broadcast->sin_addr.s_addr != 0)) {
		printf("broadcast %s ", inet_ntoa(addr.broadcast->sin_addr));
	}

	if (addr.vhid != 0) {
		printf("vhid %d ", addr.vhid);
	}
	printf("\n");
}

static void
print_inet6_addr(ifconfig_handle_t *lifh, struct ifaddrs *ifa)
{
	struct ifconfig_inet6_addr addr;
	char addr_buf[NI_MAXHOST];
	struct timespec now;

	/* Print the address */
	if (ifconfig_inet6_get_addrinfo(lifh, ifa->ifa_name, ifa, &addr) != 0) {
		err(1, "ifconfig_inet6_get_addrinfo");
	}
	if (0 != getnameinfo((struct sockaddr *)addr.sin6, addr.sin6->sin6_len,
	    addr_buf, sizeof(addr_buf), NULL, 0, NI_NUMERICHOST)) {
		inet_ntop(AF_INET6, &addr.sin6->sin6_addr, addr_buf,
		    sizeof(addr_buf));
	}
	printf("\tinet6 %s", addr_buf);

	if (addr.dstin6) {
		inet_ntop(AF_INET6, addr.dstin6, addr_buf, sizeof(addr_buf));
		printf(" --> %s", addr_buf);
	}

	/* Print the netmask */
	printf(" prefixlen %d ", addr.prefixlen);

	/* Print the scopeid*/
	if (addr.sin6->sin6_scope_id) {
		printf("scopeid 0x%x ", addr.sin6->sin6_scope_id);
	}

	/* Print the flags */
	if ((addr.flags & IN6_IFF_ANYCAST) != 0) {
		printf("anycast ");
	}
	if ((addr.flags & IN6_IFF_TENTATIVE) != 0) {
		printf("tentative ");
	}
	if ((addr.flags & IN6_IFF_DUPLICATED) != 0) {
		printf("duplicated ");
	}
	if ((addr.flags & IN6_IFF_DETACHED) != 0) {
		printf("detached ");
	}
	if ((addr.flags & IN6_IFF_DEPRECATED) != 0) {
		printf("deprecated ");
	}
	if ((addr.flags & IN6_IFF_AUTOCONF) != 0) {
		printf("autoconf ");
	}
	if ((addr.flags & IN6_IFF_TEMPORARY) != 0) {
		printf("temporary ");
	}
	if ((addr.flags & IN6_IFF_PREFER_SOURCE) != 0) {
		printf("prefer_source ");
	}

	/* Print the lifetimes */
	clock_gettime(CLOCK_MONOTONIC_FAST, &now);
	if (addr.lifetime.ia6t_preferred || addr.lifetime.ia6t_expire) {
		printf("pltime ");
		if (addr.lifetime.ia6t_preferred) {
			printf("%ld ", MAX(0l,
			    addr.lifetime.ia6t_preferred - now.tv_sec));
		} else {
			printf("infty ");
		}

		printf("vltime ");
		if (addr.lifetime.ia6t_expire) {
			printf("%ld ", MAX(0l,
			    addr.lifetime.ia6t_expire - now.tv_sec));
		} else {
			printf("infty ");
		}
	}

	/* Print the vhid */
	if (addr.vhid != 0) {
		printf("vhid %d ", addr.vhid);
	}
	printf("\n");
}

static void
print_link_addr(ifconfig_handle_t *lifh, struct ifaddrs *ifa)
{
	char addr_buf[NI_MAXHOST];
	struct sockaddr_dl *sdl;
	int n;

	sdl = (struct sockaddr_dl *)ifa->ifa_addr;
	if ((sdl != NULL) && (sdl->sdl_alen > 0)) {
		if (((sdl->sdl_type == IFT_ETHER) ||
		    (sdl->sdl_type == IFT_L2VLAN) ||
		    (sdl->sdl_type == IFT_BRIDGE)) &&
		    (sdl->sdl_alen == ETHER_ADDR_LEN)) {
			ether_ntoa_r((struct ether_addr *)LLADDR(sdl),
			    addr_buf);
			printf("\tether %s\n", addr_buf);
		} else {
			n = sdl->sdl_nlen > 0 ? sdl->sdl_nlen + 1 : 0;

			printf("\tlladdr %s\n", link_ntoa(sdl) + n);
		}
	}
}

static void
print_ifaddr(ifconfig_handle_t *lifh, struct ifaddrs *ifa, void *udata __unused)
{
	switch (ifa->ifa_addr->sa_family) {
	case AF_INET:
		print_inet4_addr(lifh, ifa);
		break;
	case AF_INET6:

		/*
		 * printing AF_INET6 status requires calling SIOCGIFAFLAG_IN6
		 * and SIOCGIFALIFETIME_IN6.  TODO: figure out the best way to
		 * do that from within libifconfig
		 */
		print_inet6_addr(lifh, ifa);
		break;
	case AF_LINK:
		print_link_addr(lifh, ifa);
		break;
	case AF_LOCAL:
	case AF_UNSPEC:
	default:
		/* TODO */
		break;
	}
}

static void
print_nd6(ifconfig_handle_t *lifh, struct ifaddrs *ifa)
{
	struct in6_ndireq nd;

	if (ifconfig_get_nd6(lifh, ifa->ifa_name, &nd) == 0) {
		printf("\tnd6 options=%x\n", nd.ndi.flags);
	} else {
		err(1, "Failed to get nd6 options");
	}
}

static void
print_fib(ifconfig_handle_t *lifh, struct ifaddrs *ifa)
{
	int fib;

	if (ifconfig_get_fib(lifh, ifa->ifa_name, &fib) == 0) {
		printf("\tfib: %d\n", fib);
	} else {
		err(1, "Failed to get interface FIB");
	}
}

static void
print_lagg(ifconfig_handle_t *lifh, struct ifaddrs *ifa)
{
	struct lagg_protos lpr[] = LAGG_PROTOS;
	struct ifconfig_lagg_status *ls;
	struct lacp_opreq *lp;
	const char *proto = "<unknown>";
	int i;

	if (ifconfig_lagg_get_lagg_status(lifh, ifa->ifa_name, &ls) < 0) {
		if (ifconfig_err_errno(lifh) == EINVAL) {
			return;
		}
		err(1, "Failed to get interface lagg status");
	}

	/* First print the proto */
	for (i = 0; i < nitems(lpr); i++) {
		if (ls->ra->ra_proto == lpr[i].lpr_proto) {
			proto = lpr[i].lpr_name;
			break;
		}
	}
	printf("\tlaggproto %s", proto);

	/* Now print the lagg hash */
	if (ls->rf->rf_flags & LAGG_F_HASHMASK) {
		const char *sep = "";

		printf(" lagghash ");
		if (ls->rf->rf_flags & LAGG_F_HASHL2) {
			printf("%sl2", sep);
			sep = ",";
		}
		if (ls->rf->rf_flags & LAGG_F_HASHL3) {
			printf("%sl3", sep);
			sep = ",";
		}
		if (ls->rf->rf_flags & LAGG_F_HASHL4) {
			printf("%sl4", sep);
			sep = ",";
		}
	}
	putchar('\n');
	printf("\tlagg options:\n");
	printf("\t\tflags=%x", ls->ro->ro_opts);
	putchar('\n');
	printf("\t\tflowid_shift: %d\n", ls->ro->ro_flowid_shift);
	if (ls->ra->ra_proto == LAGG_PROTO_ROUNDROBIN) {
		printf("\t\trr_limit: %d\n", ls->ro->ro_bkt);
	}
	printf("\tlagg statistics:\n");
	printf("\t\tactive ports: %d\n", ls->ro->ro_active);
	printf("\t\tflapping: %u\n", ls->ro->ro_flapping);
	for (i = 0; i < ls->ra->ra_ports; i++) {
		lp = (struct lacp_opreq *)&ls->ra->ra_port[i].rp_lacpreq;
		printf("\tlaggport: %s ", ls->ra->ra_port[i].rp_portname);
		printf("flags=%x", ls->ra->ra_port[i].rp_flags);
		if (ls->ra->ra_proto == LAGG_PROTO_LACP) {
			printf(" state=%x", lp->actor_state);
		}
		putchar('\n');
	}

	printf("\n");
	ifconfig_lagg_free_lagg_status(ls);
}

static void
print_laggport(ifconfig_handle_t *lifh, struct ifaddrs *ifa)
{
	struct lagg_reqport rp;

	if (ifconfig_lagg_get_laggport_status(lifh, ifa->ifa_name, &rp) < 0) {
		if ((ifconfig_err_errno(lifh) == EINVAL) ||
		    (ifconfig_err_errno(lifh) == ENOENT)) {
			return;
		} else {
			err(1, "Failed to get lagg port status");
		}
	}

	printf("\tlaggdev: %s\n", rp.rp_ifname);
}

static void
print_groups(ifconfig_handle_t *lifh, struct ifaddrs *ifa)
{
	struct ifgroupreq ifgr;
	struct ifg_req *ifg;
	int len;
	int cnt = 0;

	if (ifconfig_get_groups(lifh, ifa->ifa_name, &ifgr) != 0) {
		err(1, "Failed to get groups");
	}

	ifg = ifgr.ifgr_groups;
	len = ifgr.ifgr_len;
	for (; ifg && len >= sizeof(struct ifg_req); ifg++) {
		len -= sizeof(struct ifg_req);
		if (strcmp(ifg->ifgrq_group, "all")) {
			if (cnt == 0) {
				printf("\tgroups: ");
			}
			cnt++;
			printf("%s ", ifg->ifgrq_group);
		}
	}
	if (cnt) {
		printf("\n");
	}

	free(ifgr.ifgr_groups);
}

static void
print_media(ifconfig_handle_t *lifh, struct ifaddrs *ifa)
{
	int i;

	/* Outline:
	 * 1) Determine whether the iface supports SIOGIFMEDIA or SIOGIFXMEDIA
	 * 2) Get the full media list
	 * 3) Print the current media word
	 * 4) Print the active media word, if different
	 * 5) Print the status
	 * 6) Print the supported media list
	 *
	 * How to print the media word:
	 * 1) Get the top-level interface type and description
	 * 2) Print the subtype
	 * 3) For current word only, print the top type, if it exists
	 * 4) Print options list
	 * 5) Print the instance, if there is one
	 *
	 * How to get the top-level interface type
	 * 1) Shift ifmw right by 0x20 and index into IFM_TYPE_DESCRIPTIONS
	 *
	 * How to get the top-level interface subtype
	 * 1) Shift ifmw right by 0x20, index into ifmedia_types_to_subtypes
	 * 2) Iterate through the resulting table's subtypes table, ignoring
	 *    aliases.  Iterate through the resulting ifmedia_description
	 *    tables,  finding an entry with the right media subtype
	 */
	struct ifmediareq *ifmr;
	char opts[80];

	if (ifconfig_media_get_mediareq(lifh, ifa->ifa_name, &ifmr) != 0) {
		if (ifconfig_err_errtype(lifh) != OK) {
			err(1, "Failed to get media info");
		} else {
			return; /* Interface doesn't support media info */
		}
	}

	printf("\tmedia: %s %s", ifconfig_media_get_type(ifmr->ifm_current),
	    ifconfig_media_get_subtype(ifmr->ifm_current));
	if (ifmr->ifm_active != ifmr->ifm_current) {
		printf(" (%s", ifconfig_media_get_subtype(ifmr->ifm_active));
		ifconfig_media_get_options_string(ifmr->ifm_active, opts,
		    sizeof(opts));
		if (opts[0] != '\0') {
			printf(" <%s>)\n", opts);
		} else {
			printf(")\n");
		}
	} else {
		printf("\n");
	}

	if (ifmr->ifm_status & IFM_AVALID) {
		printf("\tstatus: %s\n",
		    ifconfig_media_get_status(ifmr));
	}

	printf("\tsupported media:\n");
	for (i = 0; i < ifmr->ifm_count; i++) {
		printf("\t\tmedia %s",
		    ifconfig_media_get_subtype(ifmr->ifm_ulist[i]));
		ifconfig_media_get_options_string(ifmr->ifm_ulist[i], opts,
		    sizeof(opts));
		if (opts[0] != '\0') {
			printf(" mediaopt %s\n", opts);
		} else {
			printf("\n");
		}
	}
	free(ifmr);
}

static void
print_iface(ifconfig_handle_t *lifh, struct ifaddrs *ifa, void *udata __unused)
{
	int metric, mtu;
	char *description = NULL;
	struct ifconfig_capabilities caps;
	struct ifstat ifs;

	printf("%s: flags=%x ", ifa->ifa_name, ifa->ifa_flags);

	if (ifconfig_get_metric(lifh, ifa->ifa_name, &metric) == 0) {
		printf("metric %d ", metric);
	} else {
		err(1, "Failed to get interface metric");
	}

	if (ifconfig_get_mtu(lifh, ifa->ifa_name, &mtu) == 0) {
		printf("mtu %d\n", mtu);
	} else {
		err(1, "Failed to get interface MTU");
	}

	if (ifconfig_get_description(lifh, ifa->ifa_name, &description) == 0) {
		printf("\tdescription: %s\n", description);
	}

	if (ifconfig_get_capability(lifh, ifa->ifa_name, &caps) == 0) {
		if (caps.curcap != 0) {
			printf("\toptions=%x\n", caps.curcap);
		}
		if (caps.reqcap != 0) {
			printf("\tcapabilities=%x\n", caps.reqcap);
		}
	} else {
		err(1, "Failed to get interface capabilities");
	}

	ifconfig_foreach_ifaddr(lifh, ifa, print_ifaddr, NULL);

	/* This paragraph is equivalent to ifconfig's af_other_status funcs */
	print_nd6(lifh, ifa);
	print_media(lifh, ifa);
	print_groups(lifh, ifa);
	print_fib(lifh, ifa);
	print_carp(lifh, ifa);
	print_lagg(lifh, ifa);
	print_laggport(lifh, ifa);

	if (ifconfig_get_ifstatus(lifh, ifa->ifa_name, &ifs) == 0) {
		printf("%s", ifs.ascii);
	}

	free(description);
}

int
main(int argc, char *argv[])
{
	ifconfig_handle_t *lifh;

	if (argc != 1) {
		errx(1, "Usage: example_status");
	}

	lifh = ifconfig_open();
	if (lifh == NULL) {
		errx(1, "Failed to open libifconfig handle.");
	}

	if (ifconfig_foreach_iface(lifh, print_iface, NULL) != 0) {
		err(1, "Failed to get interfaces");
	}

	ifconfig_close(lifh);
	lifh = NULL;
	return (-1);
}
