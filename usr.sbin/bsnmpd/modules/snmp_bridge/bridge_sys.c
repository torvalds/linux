/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Shteryana Shopova <syrinx@FreeBSD.org>
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
 * Bridge MIB implementation for SNMPd.
 * Bridge OS specific ioctls.
 *
 * $FreeBSD$
 */

#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/bridgestp.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_bridgevar.h>
#include <net/if_dl.h>
#include <net/if_mib.h>
#include <net/if_types.h>
#include <netinet/in.h>

#include <errno.h>
#include <ifaddrs.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <bsnmp/snmpmod.h>
#include <bsnmp/snmp_mibII.h>

#define	SNMPTREE_TYPES
#include "bridge_tree.h"
#include "bridge_snmp.h"

int sock = -1;

int
bridge_ioctl_init(void)
{
	if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_ERR, "cannot open socket : %s", strerror(errno));
		return (-1);
	}

	return (0);
}

/*
 * Load the if_bridge.ko module in kernel if not already there.
 */
int
bridge_kmod_load(void)
{
	int fileid, modid;
	const char mod_name[] = "if_bridge";
	struct module_stat mstat;

	/* Scan files in kernel. */
	mstat.version = sizeof(struct module_stat);
	for (fileid = kldnext(0); fileid > 0; fileid = kldnext(fileid)) {
		/* Scan modules in file. */
		for (modid = kldfirstmod(fileid); modid > 0;
			modid = modfnext(modid)) {

			if (modstat(modid, &mstat) < 0)
				continue;

			if (strcmp(mod_name, mstat.name) == 0)
				return (0);
		}
	}

	/* Not present - load it. */
	if (kldload(mod_name) < 0) {
		syslog(LOG_ERR, "failed to load %s kernel module", mod_name);
		return (-1);
	}

	return (1);
}

/************************************************************************
 * Bridge interfaces.
 */

/*
 * Convert the kernel uint64_t value for a bridge id
 */
static void
snmp_uint64_to_bridgeid(uint64_t id, bridge_id b_id)
{
	int i;
	u_char *o;

	o = (u_char *) &id;

	for (i = 0; i < SNMP_BRIDGE_ID_LEN; i++, o++)
		b_id[SNMP_BRIDGE_ID_LEN - i - 1] = *o;
}

/*
 * Fetch the bridge configuration parameters from the kernel excluding
 * it's base MAC address.
 */
static int
bridge_get_conf_param(struct bridge_if *bif)
{
	struct ifdrv ifd;
	struct ifbrparam b_param;

	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_len = sizeof(b_param);
	ifd.ifd_data = &b_param;

	/* Bridge priority. */
	ifd.ifd_cmd = BRDGGPRI;
	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "update bridge: ioctl(BRDGGPRI) failed: %s",
		    strerror(errno));
		return (-1);
	}

	bif->priority = b_param.ifbrp_prio;

	/* Configured max age. */
	ifd.ifd_cmd = BRDGGMA;
	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "update bridge: ioctl(BRDGGMA) failed: %s",
		    strerror(errno));
		return (-1);
	}

	/* Centi-seconds. */
	bif->bridge_max_age = 100 * b_param.ifbrp_maxage;

	/* Configured hello time. */
	ifd.ifd_cmd = BRDGGHT;
	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "update bridge: ioctl(BRDGGHT) failed: %s",
		    strerror(errno));
		return (-1);
	}
	bif->bridge_hello_time = 100 * b_param.ifbrp_hellotime;

	/* Forward delay. */
	ifd.ifd_cmd = BRDGGFD;
	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "update bridge: ioctl(BRDGGFD) failed: %s",
		    strerror(errno));
		return (-1);
	}
	bif->bridge_fwd_delay = 100 * b_param.ifbrp_fwddelay;

	/* Number of dropped addresses. */
	ifd.ifd_cmd = BRDGGRTE;
	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "update bridge: ioctl(BRDGGRTE) failed: %s",
		    strerror(errno));
		return (-1);
	}
	bif->lrnt_drops = b_param.ifbrp_cexceeded;

	/* Address table timeout. */
	ifd.ifd_cmd = BRDGGTO;
	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "update bridge: ioctl(BRDGGTO) failed: %s",
		    strerror(errno));
		return (-1);
	}
	bif->age_time = b_param.ifbrp_ctime;

	/* Address table size. */
	ifd.ifd_cmd = BRDGGCACHE;
	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "update bridge: ioctl(BRDGGCACHE) "
		    "failed: %s", strerror(errno));
		return (-1);
	}
	bif->max_addrs = b_param.ifbrp_csize;

	return (0);
}

/*
 * Fetch the current bridge STP operational parameters.
 * Returns: -1 - on error;
 *	     0 - old TC time and Root Port values are same;
 *	     1 - topologyChange notification should be sent;
 *	     2 - newRoot notification should be sent.
 */
int
bridge_get_op_param(struct bridge_if *bif)
{
	int new_root_send;
	struct ifdrv ifd;
	struct ifbropreq b_req;

	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_len = sizeof(b_req);
	ifd.ifd_data = &b_req;
	ifd.ifd_cmd = BRDGPARAM;

	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "update bridge: ioctl(BRDGPARAM) failed: %s",
		    strerror(errno));
		return (-1);
	}

	bif->max_age = 100 * b_req.ifbop_maxage;
	bif->hello_time = 100 * b_req.ifbop_hellotime;
	bif->fwd_delay = 100 * b_req.ifbop_fwddelay;
	bif->stp_version = b_req.ifbop_protocol;
	bif->tx_hold_count = b_req.ifbop_holdcount;

	if (b_req.ifbop_root_port == 0 &&
	    bif->root_port != b_req.ifbop_root_port)
		new_root_send = 2;
	else
		new_root_send = 0;

	bif->root_port = b_req.ifbop_root_port;
	bif->root_cost = b_req.ifbop_root_path_cost;
	snmp_uint64_to_bridgeid(b_req.ifbop_designated_root,
	    bif->design_root);

	if (bif->last_tc_time.tv_sec != b_req.ifbop_last_tc_time.tv_sec) {
		bif->top_changes++;
		bif->last_tc_time.tv_sec = b_req.ifbop_last_tc_time.tv_sec;
		bif->last_tc_time.tv_usec = b_req.ifbop_last_tc_time.tv_usec;

		/*
		 * "The trap is not sent if a (begemotBridge)NewRoot
		 * trap is sent for the same transition."
		 */
		if (new_root_send == 0)
			return (1);
	}

	return (new_root_send);
}

int
bridge_getinfo_bif(struct bridge_if *bif)
{
	if (bridge_get_conf_param(bif) < 0)
		return (-1);

	return (bridge_get_op_param(bif));
}

int
bridge_set_priority(struct bridge_if *bif, int32_t priority)
{
	struct ifdrv ifd;
	struct ifbrparam b_param;

	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_len = sizeof(b_param);
	ifd.ifd_data = &b_param;
	b_param.ifbrp_prio = (uint32_t) priority;
	ifd.ifd_cmd = BRDGSPRI;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set bridge param: ioctl(BRDGSPRI) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	/*
	 * Re-fetching the data from the driver after that might be a good
	 * idea, since changing our bridge's priority should invoke
	 * recalculation of the active spanning tree topology in the network.
	 */
	bif->priority = priority;
	return (0);
}

/*
 * Convert 1/100 of seconds to 1/256 of seconds.
 * Timeout ::= TEXTUAL-CONVENTION.
 * To convert a Timeout value into a value in units of
 * 1/256 seconds, the following algorithm should be used:
 *	b = floor( (n * 256) / 100)
 * The conversion to 1/256 of a second happens in the kernel -
 * just make sure we correctly convert the seconds to Timout
 * and vice versa.
 */
static uint32_t
snmp_timeout2_sec(int32_t secs)
{
	return (secs / 100);
}

int
bridge_set_maxage(struct bridge_if *bif, int32_t max_age)
{
	struct ifdrv ifd;
	struct ifbrparam b_param;

	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_len = sizeof(b_param);
	ifd.ifd_data = &b_param;
	b_param.ifbrp_maxage = snmp_timeout2_sec(max_age);
	ifd.ifd_cmd = BRDGSMA;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set bridge param: ioctl(BRDGSMA) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	bif->bridge_max_age = max_age;
	return (0);
}

int
bridge_set_hello_time(struct bridge_if *bif, int32_t hello_time)
{
	struct ifdrv ifd;
	struct ifbrparam b_param;

	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_len = sizeof(b_param);
	ifd.ifd_data = &b_param;
	b_param.ifbrp_hellotime = snmp_timeout2_sec(hello_time);
	ifd.ifd_cmd = BRDGSHT;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set bridge param: ioctl(BRDGSHT) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	bif->bridge_hello_time = b_param.ifbrp_hellotime;
	return (0);
}

int
bridge_set_forward_delay(struct bridge_if *bif, int32_t fwd_delay)
{
	struct ifdrv ifd;
	struct ifbrparam b_param;

	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_len = sizeof(b_param);
	ifd.ifd_data = &b_param;
	b_param.ifbrp_fwddelay = snmp_timeout2_sec(fwd_delay);
	ifd.ifd_cmd = BRDGSFD;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set bridge param: ioctl(BRDGSFD) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	bif->bridge_fwd_delay = b_param.ifbrp_fwddelay;
	return (0);
}

int
bridge_set_aging_time(struct bridge_if *bif, int32_t age_time)
{
	struct ifdrv ifd;
	struct ifbrparam b_param;

	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_len = sizeof(b_param);
	ifd.ifd_data = &b_param;
	b_param.ifbrp_ctime = (uint32_t) age_time;
	ifd.ifd_cmd = BRDGSTO;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set bridge param: ioctl(BRDGSTO) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	bif->age_time = age_time;
	return (0);
}

int
bridge_set_max_cache(struct bridge_if *bif, int32_t max_cache)
{
	struct ifdrv ifd;
	struct ifbrparam b_param;

	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_len = sizeof(b_param);
	ifd.ifd_data = &b_param;
	b_param.ifbrp_csize = max_cache;
	ifd.ifd_cmd = BRDGSCACHE;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set bridge param: ioctl(BRDGSCACHE) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	bif->max_addrs = b_param.ifbrp_csize;
	return (0);
}

int
bridge_set_tx_hold_count(struct bridge_if *bif, int32_t tx_hc)
{
	struct ifdrv ifd;
	struct ifbrparam b_param;

	if (tx_hc < SNMP_BRIDGE_MIN_TXHC || tx_hc > SNMP_BRIDGE_MAX_TXHC)
		return (-1);

	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_len = sizeof(b_param);
	ifd.ifd_data = &b_param;
	b_param.ifbrp_txhc = tx_hc;
	ifd.ifd_cmd = BRDGSTXHC;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set bridge param: ioctl(BRDGSTXHC) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	bif->tx_hold_count = b_param.ifbrp_txhc;
	return (0);
}

int
bridge_set_stp_version(struct bridge_if *bif, int32_t stp_proto)
{
	struct ifdrv ifd;
	struct ifbrparam b_param;

	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_len = sizeof(b_param);
	ifd.ifd_data = &b_param;
	b_param.ifbrp_proto = stp_proto;
	ifd.ifd_cmd = BRDGSPROTO;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set bridge param: ioctl(BRDGSPROTO) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	bif->stp_version = b_param.ifbrp_proto;
	return (0);
}

/*
 * Set the bridge interface status to up/down.
 */
int
bridge_set_if_up(const char* b_name, int8_t up)
{
	int	flags;
	struct ifreq ifr;

	bzero(&ifr, sizeof(ifr));
	strlcpy(ifr.ifr_name, b_name, sizeof(ifr.ifr_name));
	if (ioctl(sock, SIOCGIFFLAGS, (caddr_t) &ifr) < 0) {
		syslog(LOG_ERR, "set bridge up: ioctl(SIOCGIFFLAGS) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
	if (up == 1)
		flags |= IFF_UP;
	else
		flags &= ~IFF_UP;

	ifr.ifr_flags = flags & 0xffff;
	ifr.ifr_flagshigh = flags >> 16;
	if (ioctl(sock, SIOCSIFFLAGS, (caddr_t) &ifr) < 0) {
		syslog(LOG_ERR, "set bridge up: ioctl(SIOCSIFFLAGS) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	return (0);
}

int
bridge_create(const char *b_name)
{
	char *new_name;
	struct ifreq ifr;

	bzero(&ifr, sizeof(ifr));
	strlcpy(ifr.ifr_name, b_name, sizeof(ifr.ifr_name));

	if (ioctl(sock, SIOCIFCREATE, &ifr) < 0) {
		syslog(LOG_ERR, "create bridge: ioctl(SIOCIFCREATE) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	if (strcmp(b_name, ifr.ifr_name) == 0)
		return (0);

	if ((new_name = strdup(b_name)) == NULL) {
		syslog(LOG_ERR, "create bridge: strdup() failed");
		return (-1);
	}

	ifr.ifr_data = new_name;
	if (ioctl(sock, SIOCSIFNAME, (caddr_t) &ifr) < 0) {
		syslog(LOG_ERR, "create bridge: ioctl(SIOCSIFNAME) "
		    "failed: %s", strerror(errno));
		free(new_name);
		return (-1);
	}

	return (0);
}

int
bridge_destroy(const char *b_name)
{
	struct ifreq ifr;

	bzero(&ifr, sizeof(ifr));
	strlcpy(ifr.ifr_name, b_name, sizeof(ifr.ifr_name));

	if (ioctl(sock, SIOCIFDESTROY, &ifr) < 0) {
		syslog(LOG_ERR, "destroy bridge: ioctl(SIOCIFDESTROY) "
		    "failed: %s", strerror(errno));
		return (-1);
	}

	return (0);
}

/*
 * Fetch the bridge base MAC address. Return pointer to the
 * buffer containing the MAC address, NULL on failure.
 */
u_char *
bridge_get_basemac(const char *bif_name, u_char *mac, size_t mlen)
{
	int len;
	char if_name[IFNAMSIZ];
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_dl sdl;

	if (getifaddrs(&ifap) != 0) {
		syslog(LOG_ERR, "bridge get mac: getifaddrs() failed - %s",
		    strerror(errno));
		return (NULL);
	}

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;

		/*
		 * Not just casting because of alignment constraints
		 * on sparc64.
		 */
		bcopy(ifa->ifa_addr, &sdl, sizeof(struct sockaddr_dl));

		if (sdl.sdl_alen > mlen)
			continue;

		if ((len = sdl.sdl_nlen) >= IFNAMSIZ)
			len = IFNAMSIZ - 1;

		bcopy(sdl.sdl_data, if_name, len);
		if_name[len] = '\0';

		if (strcmp(bif_name, if_name) == 0) {
			bcopy(sdl.sdl_data + sdl.sdl_nlen, mac, sdl.sdl_alen);
			freeifaddrs(ifap);
			return (mac);
		}
	}

	freeifaddrs(ifap);
	return (NULL);
}

/************************************************************************
 * Bridge ports.
 */

/*
 * Convert the kernel STP port state into
 * the corresopnding enumerated type from SNMP Bridge MIB.
 */
static int
state2snmp_st(uint8_t ifbr_state)
{
	switch (ifbr_state) {
		case BSTP_IFSTATE_DISABLED:
			return (StpPortState_disabled);
		case BSTP_IFSTATE_LISTENING:
			return (StpPortState_listening);
		case BSTP_IFSTATE_LEARNING:
			return (StpPortState_learning);
		case BSTP_IFSTATE_FORWARDING:
			return (StpPortState_forwarding);
		case BSTP_IFSTATE_BLOCKING:
		case BSTP_IFSTATE_DISCARDING:
			return (StpPortState_blocking);
	}

	return (StpPortState_broken);
}

/*
 * Fill in a bridge member information according to data polled from kernel.
 */
static void
bridge_port_getinfo_conf(struct ifbreq *k_info, struct bridge_port *bp)
{
	bp->state = state2snmp_st(k_info->ifbr_state);
	bp->priority = k_info->ifbr_priority;

	/*
	 * RFC 4188:
	 * "New implementations should support dot1dStpPortPathCost32.
	 * If the port path costs exceeds the maximum value of this
	 * object then this object should report the maximum value,
	 * namely 65535.  Applications should try to read the
	 * dot1dStpPortPathCost32 object if this object reports
	 * the maximum value."
	 */

	if (k_info->ifbr_ifsflags & IFBIF_BSTP_ADMCOST)
		bp->admin_path_cost = k_info->ifbr_path_cost;
	else
		bp->admin_path_cost = 0;

	bp->path_cost = k_info->ifbr_path_cost;

	if (k_info->ifbr_ifsflags & IFBIF_STP)
		bp->enable = dot1dStpPortEnable_enabled;
	else
		bp->enable = dot1dStpPortEnable_disabled;

	/* Begemot Bridge MIB only. */
	if (k_info->ifbr_ifsflags & IFBIF_SPAN)
		bp->span_enable = begemotBridgeBaseSpanEnabled_enabled;
	else
		bp->span_enable = begemotBridgeBaseSpanEnabled_disabled;

	if (k_info->ifbr_ifsflags & IFBIF_PRIVATE)
		bp->priv_set = TruthValue_true;
	else
		bp->priv_set = TruthValue_false;

	if (k_info->ifbr_ifsflags & IFBIF_BSTP_ADMEDGE)
		bp->admin_edge = TruthValue_true;
	else
		bp->admin_edge = TruthValue_false;

	if (k_info->ifbr_ifsflags & IFBIF_BSTP_EDGE)
		bp->oper_edge = TruthValue_true;
	else
		bp->oper_edge = TruthValue_false;

	if (k_info->ifbr_ifsflags & IFBIF_BSTP_AUTOPTP) {
		bp->admin_ptp = StpPortAdminPointToPointType_auto;
		if (k_info->ifbr_ifsflags & IFBIF_BSTP_PTP)
			bp->oper_ptp = TruthValue_true;
		else
			bp->oper_ptp = TruthValue_false;
	} else if (k_info->ifbr_ifsflags & IFBIF_BSTP_PTP) {
		bp->admin_ptp = StpPortAdminPointToPointType_forceTrue;
		bp->oper_ptp = TruthValue_true;
	} else {
		bp->admin_ptp = StpPortAdminPointToPointType_forceFalse;
		bp->oper_ptp = TruthValue_false;
	}
}

/*
 * Fill in a bridge interface STP information according to
 * data polled from kernel.
 */
static void
bridge_port_getinfo_opstp(struct ifbpstpreq *bp_stp, struct bridge_port *bp)
{
	bp->enable = dot1dStpPortEnable_enabled;
	bp->fwd_trans = bp_stp->ifbp_fwd_trans;
	bp->design_cost = bp_stp->ifbp_design_cost;
	snmp_uint64_to_bridgeid(bp_stp->ifbp_design_root, bp->design_root);
	snmp_uint64_to_bridgeid(bp_stp->ifbp_design_bridge, bp->design_bridge);
	bcopy(&(bp_stp->ifbp_design_port), &(bp->design_port),
	    sizeof(uint16_t));
}

/*
 * Clear a bridge interface STP information.
 */
static void
bridge_port_clearinfo_opstp(struct bridge_port *bp)
{
	if (bp->enable == dot1dStpPortEnable_enabled) {
		bp->design_cost = 0;
		bzero(&(bp->design_root), sizeof(bridge_id));
		bzero(&(bp->design_bridge), sizeof(bridge_id));
		bzero(&(bp->design_port), sizeof(port_id));
		bp->fwd_trans = 0;
	}

	bp->enable = dot1dStpPortEnable_disabled;
}

/*
 * Set a bridge member priority.
 */
int
bridge_port_set_priority(const char *bif_name, struct bridge_port *bp,
	int32_t priority)
{
	struct ifdrv ifd;
	struct ifbreq b_req;

	strlcpy(ifd.ifd_name, bif_name, sizeof(ifd.ifd_name));
	ifd.ifd_len = sizeof(b_req);
	ifd.ifd_data = &b_req;
	strlcpy(b_req.ifbr_ifsname, bp->p_name, sizeof(b_req.ifbr_ifsname));

	b_req.ifbr_priority = (uint8_t) priority;
	ifd.ifd_cmd = BRDGSIFPRIO;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set member %s param: ioctl(BRDGSIFPRIO) "
		    "failed: %s", bp->p_name, strerror(errno));
		return (-1);
	}

	bp->priority = priority;
	return (0);
}

/*
 * Set a bridge member STP-enabled flag.
 */
int
bridge_port_set_stp_enable(const char *bif_name, struct bridge_port *bp,
	uint32_t enable)
{
	struct ifdrv ifd;
	struct ifbreq b_req;

	if (bp->enable == enable)
		return (0);

	bzero(&b_req, sizeof(b_req));
	strlcpy(ifd.ifd_name, bif_name, sizeof(ifd.ifd_name));
	ifd.ifd_len = sizeof(b_req);
	ifd.ifd_data = &b_req;
	strlcpy(b_req.ifbr_ifsname, bp->p_name, sizeof(b_req.ifbr_ifsname));
	ifd.ifd_cmd = BRDGGIFFLGS;

	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "get member %s param: ioctl(BRDGGIFFLGS) "
		    "failed: %s", bp->p_name, strerror(errno));
		return (-1);
	}

	if (enable == dot1dStpPortEnable_enabled)
		b_req.ifbr_ifsflags |= IFBIF_STP;
	else
		b_req.ifbr_ifsflags &= ~IFBIF_STP;

	ifd.ifd_cmd = BRDGSIFFLGS;
	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set member %s param: ioctl(BRDGSIFFLGS) "
		    "failed: %s", bp->p_name, strerror(errno));
		return (-1);
	}

	bp->enable = enable;
	return (0);
}

/*
 * Set a bridge member STP path cost.
 */
int
bridge_port_set_path_cost(const char *bif_name, struct bridge_port *bp,
	int32_t path_cost)
{
	struct ifdrv ifd;
	struct ifbreq b_req;

	if (path_cost < SNMP_PORT_MIN_PATHCOST ||
	    path_cost > SNMP_PORT_PATHCOST_OBSOLETE)
		return (-2);

	strlcpy(ifd.ifd_name, bif_name, sizeof(ifd.ifd_name));
	ifd.ifd_len = sizeof(b_req);
	ifd.ifd_data = &b_req;
	strlcpy(b_req.ifbr_ifsname, bp->p_name, sizeof(b_req.ifbr_ifsname));

	b_req.ifbr_path_cost = path_cost;
	ifd.ifd_cmd = BRDGSIFCOST;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set member %s param: ioctl(BRDGSIFCOST) "
		    "failed: %s", bp->p_name, strerror(errno));
		return (-1);
	}

	bp->admin_path_cost = path_cost;

	return (0);
}

/*
 * Set the PonitToPoint status of the link administratively.
 */
int
bridge_port_set_admin_ptp(const char *bif_name, struct bridge_port *bp,
    uint32_t admin_ptp)
{
	struct ifdrv ifd;
	struct ifbreq b_req;

	if (bp->admin_ptp == admin_ptp)
		return (0);

	bzero(&b_req, sizeof(b_req));
	strlcpy(ifd.ifd_name, bif_name, sizeof(ifd.ifd_name));
	ifd.ifd_len = sizeof(b_req);
	ifd.ifd_data = &b_req;
	strlcpy(b_req.ifbr_ifsname, bp->p_name, sizeof(b_req.ifbr_ifsname));
	ifd.ifd_cmd = BRDGGIFFLGS;

	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "get member %s param: ioctl(BRDGGIFFLGS) "
		    "failed: %s", bp->p_name, strerror(errno));
		return (-1);
	}

	switch (admin_ptp) {
		case StpPortAdminPointToPointType_forceTrue:
			b_req.ifbr_ifsflags &= ~IFBIF_BSTP_AUTOPTP;
			b_req.ifbr_ifsflags |= IFBIF_BSTP_PTP;
			break;
		case StpPortAdminPointToPointType_forceFalse:
			b_req.ifbr_ifsflags &= ~IFBIF_BSTP_AUTOPTP;
			b_req.ifbr_ifsflags &= ~IFBIF_BSTP_PTP;
			break;
		case StpPortAdminPointToPointType_auto:
			b_req.ifbr_ifsflags |= IFBIF_BSTP_AUTOPTP;
			break;
	}

	ifd.ifd_cmd = BRDGSIFFLGS;
	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set member %s param: ioctl(BRDGSIFFLGS) "
		    "failed: %s", bp->p_name, strerror(errno));
		return (-1);
	}

	bp->admin_ptp = admin_ptp;
	return (0);
}

/*
 * Set admin edge.
 */
int
bridge_port_set_admin_edge(const char *bif_name, struct bridge_port *bp,
    uint32_t enable)
{
	struct ifdrv ifd;
	struct ifbreq b_req;

	if (bp->admin_edge == enable)
		return (0);

	bzero(&b_req, sizeof(b_req));
	strlcpy(ifd.ifd_name, bif_name, sizeof(ifd.ifd_name));
	ifd.ifd_len = sizeof(b_req);
	ifd.ifd_data = &b_req;
	strlcpy(b_req.ifbr_ifsname, bp->p_name, sizeof(b_req.ifbr_ifsname));
	ifd.ifd_cmd = BRDGGIFFLGS;

	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "get member %s param: ioctl(BRDGGIFFLGS) "
		    "failed: %s", bp->p_name, strerror(errno));
		return (-1);
	}

	if (enable == TruthValue_true) {
		b_req.ifbr_ifsflags &= ~IFBIF_BSTP_AUTOEDGE;
		b_req.ifbr_ifsflags |= IFBIF_BSTP_EDGE;
	} else
		b_req.ifbr_ifsflags &= ~IFBIF_BSTP_EDGE;

	ifd.ifd_cmd = BRDGSIFFLGS;
	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set member %s param: ioctl(BRDGSIFFLGS) "
		    "failed: %s", bp->p_name, strerror(errno));
		return (-1);
	}

	bp->admin_edge = enable;

	return (0);
}

/*
 * Set 'private' flag.
 */
int
bridge_port_set_private(const char *bif_name, struct bridge_port *bp,
    uint32_t priv_set)
{
	struct ifdrv ifd;
	struct ifbreq b_req;

	if (bp->priv_set == priv_set)
		return (0);

	bzero(&b_req, sizeof(b_req));
	strlcpy(ifd.ifd_name, bif_name, sizeof(ifd.ifd_name));
	ifd.ifd_len = sizeof(b_req);
	ifd.ifd_data = &b_req;
	strlcpy(b_req.ifbr_ifsname, bp->p_name, sizeof(b_req.ifbr_ifsname));
	ifd.ifd_cmd = BRDGGIFFLGS;

	if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "get member %s param: ioctl(BRDGGIFFLGS) "
		    "failed: %s", bp->p_name, strerror(errno));
		return (-1);
	}

	if (priv_set == TruthValue_true)
		b_req.ifbr_ifsflags |= IFBIF_PRIVATE;
	else if (priv_set == TruthValue_false)
		b_req.ifbr_ifsflags &= ~IFBIF_PRIVATE;
	else
		return (SNMP_ERR_WRONG_VALUE);

	ifd.ifd_cmd = BRDGSIFFLGS;
	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "set member %s param: ioctl(BRDGSIFFLGS) "
		    "failed: %s", bp->p_name, strerror(errno));
		return (-1);
	}

	bp->priv_set = priv_set;

	return (0);
}


/*
 * Add a bridge member port.
 */
int
bridge_port_addm(struct bridge_port *bp, const char *b_name)
{
	struct ifdrv ifd;
	struct ifbreq b_req;

	bzero(&ifd, sizeof(ifd));
	bzero(&b_req, sizeof(b_req));

	strlcpy(ifd.ifd_name, b_name, sizeof(ifd.ifd_name));
	ifd.ifd_len = sizeof(b_req);
	ifd.ifd_data = &b_req;
	strlcpy(b_req.ifbr_ifsname, bp->p_name, sizeof(b_req.ifbr_ifsname));

	if (bp->span_enable == begemotBridgeBaseSpanEnabled_enabled)
		ifd.ifd_cmd = BRDGADDS;
	else
		ifd.ifd_cmd = BRDGADD;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "%s - add member : ioctl(%s) failed: %s",
		    bp->p_name,
		    (ifd.ifd_cmd == BRDGADDS ? "BRDGADDS" : "BRDGADD"),
		    strerror(errno));
		return (-1);
	}

	return (0);
}

/*
 * Delete a bridge member port.
 */
int
bridge_port_delm(struct bridge_port *bp, const char *b_name)
{
	struct ifdrv ifd;
	struct ifbreq b_req;

	bzero(&ifd, sizeof(ifd));
	bzero(&b_req, sizeof(b_req));

	strlcpy(ifd.ifd_name, b_name, sizeof(ifd.ifd_name));
	ifd.ifd_len = sizeof(b_req);
	ifd.ifd_data = &b_req;
	strlcpy(b_req.ifbr_ifsname, bp->p_name, sizeof(b_req.ifbr_ifsname));

	if (bp->span_enable == begemotBridgeBaseSpanEnabled_enabled)
		ifd.ifd_cmd = BRDGDELS;
	else
		ifd.ifd_cmd = BRDGDEL;

	if (ioctl(sock, SIOCSDRVSPEC, &ifd) < 0) {
		syslog(LOG_ERR, "%s - add member : ioctl(%s) failed: %s",
		    bp->p_name,
		    (ifd.ifd_cmd == BRDGDELS ? "BRDGDELS" : "BRDGDEL"),
		    strerror(errno));
		return (-1);
	}

	return (0);
}

/*
 * Fetch the bridge member list from kernel.
 * Return -1 on error, or buffer len if successful.
 */
static int32_t
bridge_port_get_iflist(struct bridge_if *bif, struct ifbreq **buf)
{
	int n = 128;
	uint32_t len;
	struct ifbreq *ninbuf;
	struct ifbifconf ifbc;
	struct ifdrv ifd;

	*buf = NULL;
	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_cmd = BRDGGIFS;
	ifd.ifd_len = sizeof(ifbc);
	ifd.ifd_data = &ifbc;

	for ( ; ; ) {
		len = n * sizeof(struct ifbreq);
		if ((ninbuf = (struct ifbreq *)realloc(*buf, len)) == NULL) {
			syslog(LOG_ERR, "get bridge member list: "
			    "realloc failed: %s", strerror(errno));
			free(*buf);
			*buf = NULL;
			return (-1);
		}

		ifbc.ifbic_len = len;
		ifbc.ifbic_req = *buf = ninbuf;

		if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
			syslog(LOG_ERR, "get bridge member list: ioctl "
			    "(BRDGGIFS) failed: %s", strerror(errno));
			free(*buf);
			buf = NULL;
			return (-1);
		}

		if ((ifbc.ifbic_len + sizeof(struct ifbreq)) < len)
			break;

		n += 64;
	}

	return (ifbc.ifbic_len);
}

/*
 * Fetch the bridge STP member list from kernel.
 * Return -1 on error, or buffer len if successful.
 */
static int32_t
bridge_port_get_ifstplist(struct bridge_if *bif, struct ifbpstpreq **buf)
{
	int n = 128;
	uint32_t len;
	struct ifbpstpreq *ninbuf;
	struct ifbpstpconf ifbstp;
	struct ifdrv ifd;

	*buf = NULL;
	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_cmd = BRDGGIFSSTP;
	ifd.ifd_len = sizeof(ifbstp);
	ifd.ifd_data = &ifbstp;

	for ( ; ; ) {
		len = n * sizeof(struct ifbpstpreq);
		if ((ninbuf = (struct ifbpstpreq *)
		    realloc(*buf, len)) == NULL) {
			syslog(LOG_ERR, "get bridge STP ports list: "
			    "realloc failed: %s", strerror(errno));
			free(*buf);
			*buf = NULL;
			return (-1);
		}

		ifbstp.ifbpstp_len = len;
		ifbstp.ifbpstp_req = *buf = ninbuf;

		if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
			syslog(LOG_ERR, "get bridge STP ports list: ioctl "
			    "(BRDGGIFSSTP) failed: %s", strerror(errno));
			free(*buf);
			buf = NULL;
			return (-1);
		}

		if ((ifbstp.ifbpstp_len + sizeof(struct ifbpstpreq)) < len)
			break;

		n += 64;
	}

	return (ifbstp.ifbpstp_len);
}

/*
 * Locate a bridge if STP params structure in a buffer.
 */
static struct ifbpstpreq *
bridge_port_find_ifstplist(uint8_t port_no, struct ifbpstpreq *buf,
    uint32_t buf_len)
{
	uint32_t i;
	struct ifbpstpreq *bstp;

	for (i = 0; i < buf_len / sizeof(struct ifbpstpreq); i++) {
		bstp = buf + i;
		if (bstp->ifbp_portno == port_no)
			return (bstp);
	}

	return (NULL);
}

/*
 * Read the initial info for all members of a bridge interface.
 * Returns the number of ports, 0 - if none, otherwise
 * -1 if some other error occurred.
 */
int
bridge_getinfo_bif_ports(struct bridge_if *bif)
{
	uint32_t i;
	int32_t buf_len;
	struct ifbreq *b_req_buf, *b_req;
	struct ifbpstpreq *bs_req_buf, *bs_req;
	struct bridge_port *bp;
	struct mibif *m_if;

	if ((buf_len = bridge_port_get_iflist(bif, &b_req_buf)) < 0)
		return (-1);

	for (i = 0; i < buf_len / sizeof(struct ifbreq); i++) {
		b_req = b_req_buf + i;

		if ((m_if = mib_find_if_sys(b_req->ifbr_portno)) != NULL) {
			/* Hopefully we will not fail here. */
			if ((bp = bridge_new_port(m_if, bif)) != NULL) {
				bp->status = RowStatus_active;
				bridge_port_getinfo_conf(b_req, bp);
				bridge_port_getinfo_mibif(m_if, bp);
			}
		} else {
			syslog(LOG_ERR, "bridge member %s not present "
			    "in mibII ifTable", b_req->ifbr_ifsname);
		}
	}
	free(b_req_buf);

	if ((buf_len = bridge_port_get_ifstplist(bif, &bs_req_buf)) < 0)
		return (-1);

	for (bp = bridge_port_bif_first(bif); bp != NULL;
	    bp = bridge_port_bif_next(bp)) {
		if ((bs_req = bridge_port_find_ifstplist(bp->port_no,
		    bs_req_buf, buf_len)) == NULL)
			bridge_port_clearinfo_opstp(bp);
		else
			bridge_port_getinfo_opstp(bs_req, bp);
	}
	free(bs_req_buf);

	return (i);
}

/*
 * Update the information for the bridge interface members.
 */
int
bridge_update_memif(struct bridge_if *bif)
{
	int added, updated;
	uint32_t i;
	int32_t buf_len;
	struct ifbreq *b_req_buf, *b_req;
	struct ifbpstpreq *bs_req_buf, *bs_req;
	struct bridge_port *bp, *bp_next;
	struct mibif *m_if;

	if ((buf_len = bridge_port_get_iflist(bif, &b_req_buf)) < 0)
		return (-1);

	added = updated = 0;

#define	BP_FOUND	0x01
	for (i = 0; i < buf_len / sizeof(struct ifbreq); i++) {
		b_req = b_req_buf + i;

		if ((m_if = mib_find_if_sys(b_req->ifbr_portno)) == NULL) {
			syslog(LOG_ERR, "bridge member %s not present "
			    "in mibII ifTable", b_req->ifbr_ifsname);
			continue;
		}

		if ((bp = bridge_port_find(m_if->index, bif)) == NULL &&
		    (bp = bridge_new_port(m_if, bif)) != NULL) {
			bp->status = RowStatus_active;
			added++;
		}

		if (bp != NULL) {
			updated++;
			bridge_port_getinfo_conf(b_req, bp);
			bridge_port_getinfo_mibif(m_if, bp);
			bp->flags |= BP_FOUND;
		}
	}
	free(b_req_buf);

	/* Clean up list. */
	for (bp = bridge_port_bif_first(bif); bp != NULL; bp = bp_next) {
		bp_next  = bridge_port_bif_next(bp);

		if ((bp->flags & BP_FOUND) == 0 &&
		    bp->status == RowStatus_active)
			bridge_port_remove(bp, bif);
		else
			bp->flags |= ~BP_FOUND;
	}
#undef	BP_FOUND

	if ((buf_len = bridge_port_get_ifstplist(bif, &bs_req_buf)) < 0)
		return (-1);

	for (bp = bridge_port_bif_first(bif); bp != NULL;
	    bp = bridge_port_bif_next(bp)) {
		if ((bs_req = bridge_port_find_ifstplist(bp->port_no,
		    bs_req_buf, buf_len)) == NULL)
			bridge_port_clearinfo_opstp(bp);
		else
			bridge_port_getinfo_opstp(bs_req, bp);
	}
	free(bs_req_buf);
	bif->ports_age = time(NULL);

	return (updated);
}

/************************************************************************
 * Bridge addresses.
 */

/*
 * Update the bridge address info according to the polled data.
 */
static void
bridge_addrs_info_ifaddrlist(struct ifbareq *ifba, struct tp_entry *tpe)
{
	tpe->port_no = if_nametoindex(ifba->ifba_ifsname);

	if ((ifba->ifba_flags & IFBAF_TYPEMASK) == IFBAF_STATIC)
		tpe->status = TpFdbStatus_mgmt;
	else
		tpe->status = TpFdbStatus_learned;
}

/*
 * Read the bridge addresses from kernel.
 * Return -1 on error, or buffer len if successful.
 */
static int32_t
bridge_addrs_getinfo_ifalist(struct bridge_if *bif, struct ifbareq **buf)
{
	int n = 128;
	uint32_t len;
	struct ifbareq *ninbuf;
	struct ifbaconf bac;
	struct ifdrv ifd;

	*buf = NULL;
	strlcpy(ifd.ifd_name, bif->bif_name, IFNAMSIZ);
	ifd.ifd_cmd = BRDGRTS;
	ifd.ifd_len = sizeof(bac);
	ifd.ifd_data = &bac;

	for ( ; ; ) {
		len = n * sizeof(struct ifbareq);
		if ((ninbuf = (struct ifbareq *)realloc(*buf, len)) == NULL) {
			syslog(LOG_ERR, "get bridge address list: "
			    " realloc failed: %s", strerror(errno));
			free(*buf);
			*buf = NULL;
			return (-1);
		}

		bac.ifbac_len = len;
		bac.ifbac_req = *buf = ninbuf;

		if (ioctl(sock, SIOCGDRVSPEC, &ifd) < 0) {
			syslog(LOG_ERR, "get bridge address list: "
			    "ioctl(BRDGRTS) failed: %s", strerror(errno));
			free(*buf);
			buf = NULL;
			return (-1);
		}

		if ((bac.ifbac_len + sizeof(struct ifbareq)) < len)
			break;

		n += 64;
	}

	return (bac.ifbac_len);
}

/*
 * Read the initial info for all addresses on a bridge interface.
 * Returns the number of addresses, 0 - if none, otherwise
 * -1 if some other error occurred.
 */
int
bridge_getinfo_bif_addrs(struct bridge_if *bif)
{
	uint32_t i;
	int32_t buf_len;
	struct ifbareq *addr_req_buf, *addr_req;
	struct tp_entry *te;

	if ((buf_len = bridge_addrs_getinfo_ifalist(bif, &addr_req_buf)) < 0)
		return (-1);

	for (i = 0; i < buf_len / sizeof(struct ifbareq); i++) {
		addr_req = addr_req_buf + i;

		if ((te = bridge_new_addrs(addr_req->ifba_dst, bif)) != NULL)
			bridge_addrs_info_ifaddrlist(addr_req, te);
	}

	free(addr_req_buf);
	return (i);
}

/*
 * Update the addresses for the bridge interface.
 */
int
bridge_update_addrs(struct bridge_if *bif)
{
	int added, updated;
	uint32_t i;
	int32_t buf_len;
	struct tp_entry *te, *te_next;
	struct ifbareq *addr_req_buf, *addr_req;

	if ((buf_len = bridge_addrs_getinfo_ifalist(bif, &addr_req_buf)) < 0)
		return (-1);

	added = updated = 0;

#define	BA_FOUND	0x01
	for (i = 0; i < buf_len / sizeof(struct ifbareq); i++) {
		addr_req = addr_req_buf + i;

		if ((te = bridge_addrs_find(addr_req->ifba_dst, bif)) == NULL) {
			added++;

			if ((te = bridge_new_addrs(addr_req->ifba_dst, bif))
			    == NULL)
				continue;
		} else
			updated++;

		bridge_addrs_info_ifaddrlist(addr_req, te);
		te-> flags |= BA_FOUND;
	}
	free(addr_req_buf);

	for (te = bridge_addrs_bif_first(bif); te != NULL; te = te_next) {
		te_next = bridge_addrs_bif_next(te);

		if ((te-> flags & BA_FOUND) == 0)
			bridge_addrs_remove(te, bif);
		else
			te-> flags &= ~BA_FOUND;
	}
#undef	BA_FOUND

	bif->addrs_age = time(NULL);
	return (updated + added);
}

/************************************************************************
 * Bridge packet filtering.
 */
const char bridge_sysctl[] = "net.link.bridge.";

static struct {
	int32_t val;
	const char *name;
} bridge_pf_sysctl[] = {
	{ 1, "pfil_bridge" },
	{ 1, "pfil_member" },
	{ 1, "pfil_onlyip" },
	{ 0, "ipfw" },
};

int32_t
bridge_get_pfval(uint8_t which)
{

	if (which > nitems(bridge_pf_sysctl) || which < 1)
		return (-1);

	return (bridge_pf_sysctl[which - 1].val);
}

int32_t
bridge_do_pfctl(int32_t bridge_ctl, enum snmp_op op, int32_t *val)
{
	char *mib_oid;
	size_t len, s_len;
	int32_t i, s_i;

	if (bridge_ctl >= LEAF_begemotBridgeLayer2PfStatus)
		return (-2);

	if (op == SNMP_OP_SET) {
		s_i = *val;
		s_len = sizeof(s_i);
	} else
		s_len = 0;

	len = sizeof(i);

	asprintf(&mib_oid, "%s%s", bridge_sysctl,
	    bridge_pf_sysctl[bridge_ctl].name);
	if (mib_oid == NULL)
		return (-1);

	if (sysctlbyname(mib_oid, &i, &len, (op == SNMP_OP_SET ? &s_i : NULL),
	    s_len) == -1) {
		syslog(LOG_ERR, "sysctl(%s) failed - %s", mib_oid,
		    strerror(errno));
		free(mib_oid);
		return (-1);
	}

	bridge_pf_sysctl[bridge_ctl].val = i;
	*val = i;

	free(mib_oid);

	return (i);
}

void
bridge_pf_dump(void)
{
	uint8_t i;

	for (i = 0; i < nitems(bridge_pf_sysctl); i++) {
		syslog(LOG_ERR, "%s%s = %d", bridge_sysctl,
		    bridge_pf_sysctl[i].name, bridge_pf_sysctl[i].val);
	}
}
