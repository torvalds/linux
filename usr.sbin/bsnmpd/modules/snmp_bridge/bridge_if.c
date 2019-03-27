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
 * Bridge interface objects.
 *
 * $FreeBSD$
 */

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_mib.h>
#include <net/if_types.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <bsnmp/snmpmod.h>
#include <bsnmp/snmp_mibII.h>

#define	SNMPTREE_TYPES
#include "bridge_tree.h"
#include "bridge_snmp.h"
#include "bridge_oid.h"

static const struct asn_oid oid_newRoot = OIDX_newRoot;
static const struct asn_oid oid_TopologyChange = OIDX_topologyChange;
static const struct asn_oid oid_begemotBrigeName = \
			OIDX_begemotBridgeBaseName;
static const struct asn_oid oid_begemotNewRoot = OIDX_begemotBridgeNewRoot;
static const struct asn_oid oid_begemotTopologyChange = \
			OIDX_begemotBridgeTopologyChange;

TAILQ_HEAD(bridge_ifs, bridge_if);

/*
 * Free the bridge interface list.
 */
static void
bridge_ifs_free(struct bridge_ifs *headp)
{
	struct bridge_if *b;

	while ((b = TAILQ_FIRST(headp)) != NULL) {
		TAILQ_REMOVE(headp, b, b_if);
		free(b);
	}
}

/*
 * Insert an entry in the bridge interface TAILQ. Keep the
 * TAILQ sorted by the bridge's interface name.
 */
static void
bridge_ifs_insert(struct bridge_ifs *headp,
	struct bridge_if *b)
{
	struct bridge_if *temp;

	if ((temp = TAILQ_FIRST(headp)) == NULL ||
	    strcmp(b->bif_name, temp->bif_name) < 0) {
		TAILQ_INSERT_HEAD(headp, b, b_if);
		return;
	}

	TAILQ_FOREACH(temp, headp, b_if)
		if(strcmp(b->bif_name, temp->bif_name) < 0)
			TAILQ_INSERT_BEFORE(temp, b, b_if);

	TAILQ_INSERT_TAIL(headp, b, b_if);
}

/* The global bridge interface list. */
static struct bridge_ifs bridge_ifs = TAILQ_HEAD_INITIALIZER(bridge_ifs);
static time_t bridge_list_age;

/*
 * Free the global list.
 */
void
bridge_ifs_fini(void)
{
	bridge_ifs_free(&bridge_ifs);
}

/*
 * Find a bridge interface entry by the bridge interface system index.
 */
struct bridge_if *
bridge_if_find_ifs(uint32_t sysindex)
{
	struct bridge_if *b;

	TAILQ_FOREACH(b, &bridge_ifs, b_if)
		if (b->sysindex == sysindex)
			return (b);

	return (NULL);
}

/*
 * Find a bridge interface entry by the bridge interface name.
 */
struct bridge_if *
bridge_if_find_ifname(const char *b_name)
{
	struct bridge_if *b;

	TAILQ_FOREACH(b, &bridge_ifs, b_if)
		if (strcmp(b_name, b->bif_name) == 0)
			return (b);

	return (NULL);
}

/*
 * Find a bridge name by the bridge interface system index.
 */
const char *
bridge_if_find_name(uint32_t sysindex)
{
	struct bridge_if *b;

	TAILQ_FOREACH(b, &bridge_ifs, b_if)
		if (b->sysindex == sysindex)
			return (b->bif_name);

	return (NULL);
}

/*
 * Given two bridge interfaces' system indexes, find their
 * corresponding names and return the result of the name
 * comparison. Returns:
 * error : -2
 * i1 < i2 : -1
 * i1 > i2 : +1
 * i1 = i2 : 0
 */
int
bridge_compare_sysidx(uint32_t i1, uint32_t i2)
{
	int c;
	const char *b1, *b2;

	if (i1 == i2)
		return (0);

	if ((b1 = bridge_if_find_name(i1)) == NULL) {
		syslog(LOG_ERR, "Bridge interface %d does not exist", i1);
		return (-2);
	}

	if ((b2 = bridge_if_find_name(i2)) == NULL) {
		syslog(LOG_ERR, "Bridge interface %d does not exist", i2);
		return (-2);
	}

	if ((c = strcmp(b1, b2)) < 0)
		return (-1);
	else if (c > 0)
		return (1);

	return (0);
}

/*
 * Fetch the first bridge interface from the list.
 */
struct bridge_if *
bridge_first_bif(void)
{
	return (TAILQ_FIRST(&bridge_ifs));
}

/*
 * Fetch the next bridge interface from the list.
 */
struct bridge_if *
bridge_next_bif(struct bridge_if *b_pr)
{
	return (TAILQ_NEXT(b_pr, b_if));
}

/*
 * Create a new entry for a bridge interface and insert
 * it in the list.
 */
static struct bridge_if *
bridge_new_bif(const char *bif_n, uint32_t sysindex, const u_char *physaddr)
{
	struct bridge_if *bif;

	if ((bif = (struct bridge_if *) malloc(sizeof(*bif)))== NULL) {
		syslog(LOG_ERR, "bridge new interface failed: %s",
		    strerror(errno));
		return (NULL);
	}

	bzero(bif, sizeof(struct bridge_if));
	strlcpy(bif->bif_name, bif_n, IFNAMSIZ);
	bcopy(physaddr, bif->br_addr.octet, ETHER_ADDR_LEN);
	bif->sysindex = sysindex;
	bif->br_type = BaseType_transparent_only;
	/* 1 - all bridges default hold time * 100 - centi-seconds */
	bif->hold_time = 1 * 100;
	bif->prot_spec = dot1dStpProtocolSpecification_ieee8021d;
	bridge_ifs_insert(&bridge_ifs, bif);

	return (bif);
}

/*
 * Remove a bridge interface from the list, freeing all it's ports
 * and address entries.
 */
void
bridge_remove_bif(struct bridge_if *bif)
{
	bridge_members_free(bif);
	bridge_addrs_free(bif);
	TAILQ_REMOVE(&bridge_ifs, bif, b_if);
	free(bif);
}


/*
 * Prepare the variable (bridge interface name) for the private
 * begemot notifications.
 */
static struct snmp_value*
bridge_basename_var(struct bridge_if *bif, struct snmp_value* b_val)
{
	uint i;

	b_val->var = oid_begemotBrigeName;
	b_val->var.subs[b_val->var.len++] = strlen(bif->bif_name);

	if ((b_val->v.octetstring.octets = (u_char *)
	    malloc(strlen(bif->bif_name))) == NULL)
		return (NULL);

	for (i = 0; i < strlen(bif->bif_name); i++)
		b_val->var.subs[b_val->var.len++] = bif->bif_name[i];

	b_val->v.octetstring.len = strlen(bif->bif_name);
	bcopy(bif->bif_name, b_val->v.octetstring.octets,
	    strlen(bif->bif_name));
	b_val->syntax = SNMP_SYNTAX_OCTETSTRING;

	return (b_val);
}

/*
 * Compare the values of the old and the new root port and
 * send a new root notification, if they are not matching.
 */
static void
bridge_new_root(struct bridge_if *bif)
{
	struct snmp_value bif_idx;

	if (bridge_get_default() == bif)
		snmp_send_trap(&oid_newRoot, (struct snmp_value *) NULL);

	if (bridge_basename_var(bif, &bif_idx) == NULL)
		return;

	snmp_send_trap(&oid_begemotTopologyChange,
	    &bif_idx, (struct snmp_value *) NULL);
}

/*
 * Compare the new and old topology change times and send a
 * topology change notification if necessary.
 */
static void
bridge_top_change(struct bridge_if *bif)
{
	struct snmp_value bif_idx;

	if (bridge_get_default() == bif)
		snmp_send_trap(&oid_TopologyChange,
		    (struct snmp_value *) NULL);

	if (bridge_basename_var(bif, &bif_idx) == NULL)
		return;

	snmp_send_trap(&oid_begemotNewRoot,
	    &bif_idx, (struct snmp_value *) NULL);
}

static int
bridge_if_create(const char* b_name, int8_t up)
{
	if (bridge_create(b_name) < 0)
		return (-1);

	if (up == 1 && (bridge_set_if_up(b_name, 1) < 0))
		return (-1);

	/*
	 * Do not create a new bridge entry here -
	 * wait until the mibII module notifies us.
	 */
	return (0);
}

static int
bridge_if_destroy(struct bridge_if *bif)
{
	if (bridge_destroy(bif->bif_name) < 0)
		return (-1);

	bridge_remove_bif(bif);

	return (0);
}

/*
 * Calculate the timeticks since the last topology change.
 */
static int
bridge_get_time_since_tc(struct bridge_if *bif, uint32_t *ticks)
{
	struct timeval ct;

	if (gettimeofday(&ct, NULL) < 0) {
		syslog(LOG_ERR, "bridge get time since last TC:"
		    "gettimeofday failed: %s", strerror(errno));
		return (-1);
	}

	if (ct.tv_usec - bif->last_tc_time.tv_usec < 0) {
		ct.tv_sec -= 1;
		ct.tv_usec += 1000000;
	}

	ct.tv_sec -= bif->last_tc_time.tv_sec;
	ct.tv_usec -= bif->last_tc_time.tv_usec;

	*ticks = ct.tv_sec * 100 + ct.tv_usec/10000;

	return (0);
}

/*
 * Update the info we have for a single bridge interface.
 * Return:
 * 1, if successful
 * 0, if the interface was deleted
 * -1, error occurred while fetching the info from the kernel.
 */
static int
bridge_update_bif(struct bridge_if *bif)
{
	struct mibif *ifp;

	/* Walk through the mibII interface list. */
	for (ifp = mib_first_if(); ifp != NULL; ifp = mib_next_if(ifp))
		if (strcmp(ifp->name, bif->bif_name) == 0)
			break;

	if (ifp == NULL) {
		/* Ops, we do not exist anymore. */
		bridge_remove_bif(bif);
		return (0);
	}

	if (ifp->physaddr != NULL )
		bcopy(ifp->physaddr, bif->br_addr.octet, ETHER_ADDR_LEN);
	else
		bridge_get_basemac(bif->bif_name, bif->br_addr.octet,
		    ETHER_ADDR_LEN);

	if (ifp->mib.ifmd_flags & IFF_RUNNING)
		bif->if_status = RowStatus_active;
	else
		bif->if_status = RowStatus_notInService;

	switch (bridge_getinfo_bif(bif)) {
		case 2:
			bridge_new_root(bif);
			break;
		case 1:
			bridge_top_change(bif);
			break;
		case -1:
			bridge_remove_bif(bif);
			return (-1);
		default:
			break;
	}

	/*
	 * The number of ports is accessible via SNMP -
	 * update the ports each time the bridge interface data
	 * is refreshed too.
	 */
	bif->num_ports = bridge_update_memif(bif);
	bif->entry_age = time(NULL);

	return (1);
}

/*
 * Update all bridge interfaces' ports only -
 * make sure each bridge interface exists first.
 */
void
bridge_update_all_ports(void)
{
	struct mibif *ifp;
	struct bridge_if *bif, *t_bif;

	for (bif = bridge_first_bif(); bif != NULL; bif = t_bif) {
		t_bif = bridge_next_bif(bif);

		for (ifp = mib_first_if(); ifp != NULL;
		    ifp = mib_next_if(ifp))
			if (strcmp(ifp->name, bif->bif_name) == 0)
				break;

		if (ifp != NULL)
			bif->num_ports = bridge_update_memif(bif);
		else  /* Ops, we do not exist anymore. */
			bridge_remove_bif(bif);
	}

	bridge_ports_update_listage();
}

/*
 * Update all addresses only.
 */
void
bridge_update_all_addrs(void)
{
	struct mibif *ifp;
	struct bridge_if *bif, *t_bif;

	for (bif = bridge_first_bif(); bif != NULL; bif = t_bif) {
		t_bif = bridge_next_bif(bif);

		for (ifp = mib_first_if(); ifp != NULL;
		    ifp = mib_next_if(ifp))
			if (strcmp(ifp->name, bif->bif_name) == 0)
				break;

		if (ifp != NULL)
			bif->num_addrs = bridge_update_addrs(bif);
		else  /* Ops, we don't exist anymore. */
			bridge_remove_bif(bif);
	}

	bridge_addrs_update_listage();
}

/*
 * Update only the bridge interfaces' data - skip addresses.
 */
void
bridge_update_all_ifs(void)
{
	struct bridge_if *bif, *t_bif;

	for (bif = bridge_first_bif(); bif != NULL; bif = t_bif) {
		t_bif = bridge_next_bif(bif);
		bridge_update_bif(bif);
	}

	bridge_ports_update_listage();
	bridge_list_age = time(NULL);
}

/*
 * Update all info we have for all bridges.
 */
void
bridge_update_all(void *arg __unused)
{
	struct bridge_if *bif, *t_bif;

	for (bif = bridge_first_bif(); bif != NULL; bif = t_bif) {
		t_bif = bridge_next_bif(bif);
		if (bridge_update_bif(bif) <= 0)
			continue;

		/* Update our learnt addresses. */
		bif->num_addrs = bridge_update_addrs(bif);
	}

	bridge_list_age = time(NULL);
	bridge_ports_update_listage();
	bridge_addrs_update_listage();
}

/*
 * Callback for polling our last topology change time -
 * check whether we are root or whether a TC was detected once every
 * 30 seconds, so that we can send the newRoot and TopologyChange traps
 * on time. The rest of the data is polled only once every 5 min.
 */
void
bridge_update_tc_time(void *arg __unused)
{
	struct bridge_if *bif;
	struct mibif *ifp;

	TAILQ_FOREACH(bif, &bridge_ifs, b_if) {
		/* Walk through the mibII interface list. */
		for (ifp = mib_first_if(); ifp != NULL; ifp = mib_next_if(ifp))
			if (strcmp(ifp->name, bif->bif_name) == 0)
				break;

		if (ifp == NULL) {
			bridge_remove_bif(bif);
			continue;
		}

		switch (bridge_get_op_param(bif)) {
			case 2:
				bridge_new_root(bif);
				break;
			case 1:
				bridge_top_change(bif);
				break;
		}
	}
}

/*
 * Callback for handling new bridge interface creation.
 */
int
bridge_attach_newif(struct mibif *ifp)
{
	u_char mac[ETHER_ADDR_LEN];
	struct bridge_if *bif;

	if (ifp->mib.ifmd_data.ifi_type != IFT_BRIDGE)
		return (0);

	/* Make sure it does not exist in our list. */
	TAILQ_FOREACH(bif, &bridge_ifs, b_if)
		if(strcmp(bif->bif_name, ifp->name) == 0) {
			syslog(LOG_ERR, "bridge interface %s already "
			    "in list", bif->bif_name);
			return (-1);
		}

	if (ifp->physaddr == NULL) {
		if (bridge_get_basemac(ifp->name, mac, sizeof(mac)) == NULL) {
			syslog(LOG_ERR, "bridge attach new %s failed - "
			    "no bridge mac address", ifp->name);
			return (-1);
		}
	} else
		bcopy(ifp->physaddr, &mac, sizeof(mac));

	if ((bif = bridge_new_bif(ifp->name, ifp->sysindex, mac)) == NULL)
		return (-1);

	if (ifp->mib.ifmd_flags & IFF_RUNNING)
		bif->if_status = RowStatus_active;
	else
		bif->if_status = RowStatus_notInService;

	/* Skip sending notifications if the interface was just created. */
	if (bridge_getinfo_bif(bif) < 0 ||
	    (bif->num_ports = bridge_getinfo_bif_ports(bif)) < 0 ||
	    (bif->num_addrs = bridge_getinfo_bif_addrs(bif)) < 0) {
		bridge_remove_bif(bif);
		return (-1);
	}

	/* Check whether we are the default bridge interface. */
	if (strcmp(ifp->name, bridge_get_default_name()) == 0)
		bridge_set_default(bif);

	return (0);
}

void
bridge_ifs_dump(void)
{
	struct bridge_if *bif;

	for (bif = bridge_first_bif(); bif != NULL;
		bif = bridge_next_bif(bif)) {
		syslog(LOG_ERR, "Bridge %s, index - %d", bif->bif_name,
		    bif->sysindex);
		bridge_ports_dump(bif);
		bridge_addrs_dump(bif);
	}
}

/*
 * RFC4188 specifics.
 */
int
op_dot1d_base(struct snmp_context *ctx __unused, struct snmp_value *value,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;

	if ((bif = bridge_get_default()) == NULL)
		return (SNMP_ERR_NOSUCHNAME);

	if (time(NULL) - bif->entry_age > bridge_get_data_maxage() &&
	    bridge_update_bif(bif) <= 0) /* It was just deleted. */
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
	    case SNMP_OP_GET:
		switch (value->var.subs[sub - 1]) {
		    case LEAF_dot1dBaseBridgeAddress:
			return (string_get(value, bif->br_addr.octet,
			    ETHER_ADDR_LEN));
		    case LEAF_dot1dBaseNumPorts:
			value->v.integer = bif->num_ports;
			return (SNMP_ERR_NOERROR);
		    case LEAF_dot1dBaseType:
			value->v.integer = bif->br_type;
			return (SNMP_ERR_NOERROR);
		}
		abort();

		case SNMP_OP_SET:
		    return (SNMP_ERR_NOT_WRITEABLE);

		case SNMP_OP_GETNEXT:
		case SNMP_OP_ROLLBACK:
		case SNMP_OP_COMMIT:
		   break;
	}

	abort();
}

int
op_dot1d_stp(struct snmp_context *ctx, struct snmp_value *val, uint sub,
    uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;

	if ((bif = bridge_get_default()) == NULL)
		return (SNMP_ERR_NOSUCHNAME);

	if (time(NULL) - bif->entry_age > bridge_get_data_maxage() &&
	    bridge_update_bif(bif) <= 0) /* It was just deleted. */
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
	    case SNMP_OP_GET:
		switch (val->var.subs[sub - 1]) {
		    case LEAF_dot1dStpProtocolSpecification:
			val->v.integer = bif->prot_spec;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpPriority:
			val->v.integer = bif->priority;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpTimeSinceTopologyChange:
			if (bridge_get_time_since_tc(bif,
			    &(val->v.uint32)) < 0)
				return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpTopChanges:
			val->v.uint32 = bif->top_changes;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpDesignatedRoot:
			return (string_get(val, bif->design_root,
			    SNMP_BRIDGE_ID_LEN));

		    case LEAF_dot1dStpRootCost:
			val->v.integer = bif->root_cost;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpRootPort:
			val->v.integer = bif->root_port;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpMaxAge:
			val->v.integer = bif->max_age;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpHelloTime:
			val->v.integer = bif->hello_time;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpHoldTime:
			val->v.integer = bif->hold_time;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpForwardDelay:
			val->v.integer = bif->fwd_delay;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpBridgeMaxAge:
			val->v.integer = bif->bridge_max_age;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpBridgeHelloTime:
			val->v.integer = bif->bridge_hello_time;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpBridgeForwardDelay:
			val->v.integer = bif->bridge_fwd_delay;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpVersion:
			val->v.integer = bif->stp_version;
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpTxHoldCount:
			val->v.integer = bif->tx_hold_count;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	    case SNMP_OP_GETNEXT:
		abort();

	    case SNMP_OP_SET:
		switch (val->var.subs[sub - 1]) {
		    case LEAF_dot1dStpPriority:
			if (val->v.integer > SNMP_BRIDGE_MAX_PRIORITY ||
			    val->v.integer % 4096 != 0)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->priority;
			if (bridge_set_priority(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpBridgeMaxAge:
			if (val->v.integer < SNMP_BRIDGE_MIN_MAGE ||
			    val->v.integer > SNMP_BRIDGE_MAX_MAGE)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->bridge_max_age;
			if (bridge_set_maxage(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpBridgeHelloTime:
			if (val->v.integer < SNMP_BRIDGE_MIN_HTIME ||
			    val->v.integer > SNMP_BRIDGE_MAX_HTIME)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->bridge_hello_time;
			if (bridge_set_hello_time(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpBridgeForwardDelay:
			if (val->v.integer < SNMP_BRIDGE_MIN_FDELAY ||
			    val->v.integer > SNMP_BRIDGE_MAX_FDELAY)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->bridge_fwd_delay;
			if (bridge_set_forward_delay(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpVersion:
			if (val->v.integer != dot1dStpVersion_stpCompatible &&
			    val->v.integer != dot1dStpVersion_rstp)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->stp_version;
			if (bridge_set_stp_version(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpTxHoldCount:
			if (val->v.integer < SNMP_BRIDGE_MIN_TXHC ||
			    val->v.integer > SNMP_BRIDGE_MAX_TXHC)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->tx_hold_count;
			if (bridge_set_tx_hold_count(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_dot1dStpProtocolSpecification:
		    case LEAF_dot1dStpTimeSinceTopologyChange:
		    case LEAF_dot1dStpTopChanges:
		    case LEAF_dot1dStpDesignatedRoot:
		    case LEAF_dot1dStpRootCost:
		    case LEAF_dot1dStpRootPort:
		    case LEAF_dot1dStpMaxAge:
		    case LEAF_dot1dStpHelloTime:
		    case LEAF_dot1dStpHoldTime:
		    case LEAF_dot1dStpForwardDelay:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		abort();

	    case SNMP_OP_ROLLBACK:
		switch (val->var.subs[sub - 1]) {
		    case LEAF_dot1dStpPriority:
			bridge_set_priority(bif, ctx->scratch->int1);
			break;
		    case LEAF_dot1dStpBridgeMaxAge:
			bridge_set_maxage(bif, ctx->scratch->int1);
			break;
		    case LEAF_dot1dStpBridgeHelloTime:
			bridge_set_hello_time(bif, ctx->scratch->int1);
			break;
		    case LEAF_dot1dStpBridgeForwardDelay:
			bridge_set_forward_delay(bif, ctx->scratch->int1);
			break;
		    case LEAF_dot1dStpVersion:
			bridge_set_stp_version(bif, ctx->scratch->int1);
			break;
		    case LEAF_dot1dStpTxHoldCount:
			bridge_set_tx_hold_count(bif, ctx->scratch->int1);
			break;
		}
		return (SNMP_ERR_NOERROR);

	    case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
op_dot1d_tp(struct snmp_context *ctx, struct snmp_value *value,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;

	if ((bif = bridge_get_default()) == NULL)
		return (SNMP_ERR_NOSUCHNAME);

	if (time(NULL) - bif->entry_age > bridge_get_data_maxage() &&
	    bridge_update_bif(bif) <= 0) /* It was just deleted. */
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
	    case SNMP_OP_GET:
		switch (value->var.subs[sub - 1]) {
		    case LEAF_dot1dTpLearnedEntryDiscards:
			value->v.uint32 = bif->lrnt_drops;
			return (SNMP_ERR_NOERROR);
		    case LEAF_dot1dTpAgingTime:
			value->v.integer = bif->age_time;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	    case SNMP_OP_GETNEXT:
		abort();

	    case SNMP_OP_SET:
		switch (value->var.subs[sub - 1]) {
		    case LEAF_dot1dTpLearnedEntryDiscards:
			return (SNMP_ERR_NOT_WRITEABLE);

		    case LEAF_dot1dTpAgingTime:
			if (value->v.integer < SNMP_BRIDGE_MIN_AGE_TIME ||
			    value->v.integer > SNMP_BRIDGE_MAX_AGE_TIME)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->age_time;
			if (bridge_set_aging_time(bif, value->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);
		}
		abort();

	    case SNMP_OP_ROLLBACK:
		if (value->var.subs[sub - 1] == LEAF_dot1dTpAgingTime)
		    bridge_set_aging_time(bif, ctx->scratch->int1);
		return (SNMP_ERR_NOERROR);

	    case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	}

	abort();
}

/*
 * Private BEGEMOT-BRIDGE-MIB specifics.
 */

/*
 * Get the bridge name from an OID index.
 */
static char *
bridge_name_index_get(const struct asn_oid *oid, uint sub, char *b_name)
{
	uint i;

	if (oid->len - sub != oid->subs[sub] + 1 || oid->subs[sub] >= IFNAMSIZ)
		return (NULL);

	for (i = 0; i < oid->subs[sub]; i++)
		b_name[i] = oid->subs[sub + i + 1];
	b_name[i] = '\0';

	return (b_name);
}

static void
bridge_if_index_append(struct asn_oid *oid, uint sub,
	const struct bridge_if *bif)
{
	uint i;

	oid->len = sub + strlen(bif->bif_name) + 1;
	oid->subs[sub] = strlen(bif->bif_name);

	for (i = 1; i <= strlen(bif->bif_name); i++)
		oid->subs[sub + i] = bif->bif_name[i - 1];
}

static struct bridge_if *
bridge_if_index_get(const struct asn_oid *oid, uint sub)
{
	uint i;
	char bif_name[IFNAMSIZ];

	if (oid->len - sub != oid->subs[sub] + 1 || oid->subs[sub] >= IFNAMSIZ)
		return (NULL);

	for (i = 0; i < oid->subs[sub]; i++)
		bif_name[i] = oid->subs[sub + i + 1];
	bif_name[i] = '\0';

	return (bridge_if_find_ifname(bif_name));
}

static struct bridge_if *
bridge_if_index_getnext(const struct asn_oid *oid, uint sub)
{
	uint i;
	char bif_name[IFNAMSIZ];
	struct bridge_if *bif;

	if (oid->len - sub == 0)
		return (bridge_first_bif());

	if (oid->len - sub != oid->subs[sub] + 1 || oid->subs[sub] >= IFNAMSIZ)
		return (NULL);

	for (i = 0; i < oid->subs[sub]; i++)
		bif_name[i] = oid->subs[sub + i + 1];
	bif_name[i] = '\0';

	if ((bif = bridge_if_find_ifname(bif_name)) == NULL)
		return (NULL);

	return (bridge_next_bif(bif));
}

static int
bridge_set_if_status(struct snmp_context *ctx,
	struct snmp_value *val, uint sub)
{
	struct bridge_if *bif;
	char bif_name[IFNAMSIZ];

	bif = bridge_if_index_get(&val->var, sub);

	switch (val->v.integer) {
	    case RowStatus_active:
		if (bif == NULL)
		    return (SNMP_ERR_INCONS_VALUE);

		ctx->scratch->int1 = bif->if_status;

		switch (bif->if_status) {
		    case RowStatus_active:
			return (SNMP_ERR_NOERROR);
		    case RowStatus_notInService:
			if (bridge_set_if_up(bif->bif_name, 1) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);
		    default:
			break;
		}
		return (SNMP_ERR_INCONS_VALUE);

	    case RowStatus_notInService:
		if (bif == NULL)
		    return (SNMP_ERR_INCONS_VALUE);

		ctx->scratch->int1 = bif->if_status;

		switch (bif->if_status) {
		    case RowStatus_active:
			if (bridge_set_if_up(bif->bif_name, 1) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);
		    case RowStatus_notInService:
			return (SNMP_ERR_NOERROR);
		    default:
			break;
		}
		return (SNMP_ERR_INCONS_VALUE);

	    case RowStatus_notReady:
		return (SNMP_ERR_INCONS_VALUE);

	    case RowStatus_createAndGo:
		if (bif != NULL)
		    return (SNMP_ERR_INCONS_VALUE);

		ctx->scratch->int1 = RowStatus_destroy;

		if (bridge_name_index_get(&val->var, sub, bif_name) == NULL)
		    return (SNMP_ERR_BADVALUE);
		if (bridge_if_create(bif_name, 1) < 0)
		    return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	    case RowStatus_createAndWait:
		if (bif != NULL)
		    return (SNMP_ERR_INCONS_VALUE);

		if (bridge_name_index_get(&val->var, sub, bif_name) == NULL)
		    return (SNMP_ERR_BADVALUE);

		ctx->scratch->int1 = RowStatus_destroy;

		if (bridge_if_create(bif_name, 0) < 0)
		    return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	    case RowStatus_destroy:
		if (bif == NULL)
		    return (SNMP_ERR_NOSUCHNAME);

		ctx->scratch->int1 = bif->if_status;
		bif->if_status = RowStatus_destroy;
	}

	return (SNMP_ERR_NOERROR);
}

static int
bridge_rollback_if_status(struct snmp_context *ctx,
	struct snmp_value *val, uint sub)
{
	struct bridge_if *bif;

	if ((bif = bridge_if_index_get(&val->var, sub)) == NULL)
		return (SNMP_ERR_GENERR);

	switch (ctx->scratch->int1) {
		case RowStatus_destroy:
			bridge_if_destroy(bif);
			return (SNMP_ERR_NOERROR);

		case RowStatus_notInService:
			if (bif->if_status != ctx->scratch->int1)
				bridge_set_if_up(bif->bif_name, 0);
			bif->if_status = RowStatus_notInService;
			return (SNMP_ERR_NOERROR);

		case RowStatus_active:
			if (bif->if_status != ctx->scratch->int1)
				bridge_set_if_up(bif->bif_name, 1);
			bif->if_status = RowStatus_active;
			return (SNMP_ERR_NOERROR);
	}

	abort();
}

static int
bridge_commit_if_status(struct snmp_value *val, uint sub)
{
	struct bridge_if *bif;

	if ((bif = bridge_if_index_get(&val->var, sub)) == NULL)
		return (SNMP_ERR_GENERR);

	if (bif->if_status == RowStatus_destroy &&
	    bridge_if_destroy(bif) < 0)
		return (SNMP_ERR_COMMIT_FAILED);

	return (SNMP_ERR_NOERROR);
}

int
op_begemot_base_bridge(struct snmp_context *ctx, struct snmp_value *val,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;

	if (time(NULL) - bridge_list_age > bridge_get_data_maxage())
		bridge_update_all_ifs();

	switch (op) {
	    case SNMP_OP_GET:
		if ((bif = bridge_if_index_get(&val->var, sub)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_GETNEXT:
		if ((bif = bridge_if_index_getnext(&val->var, sub)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);
		bridge_if_index_append(&val->var, sub, bif);
		goto get;

	    case SNMP_OP_SET:
		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeBaseStatus:
			return (bridge_set_if_status(ctx, val, sub));
		    case LEAF_begemotBridgeBaseName:
		    case LEAF_begemotBridgeBaseAddress:
		    case LEAF_begemotBridgeBaseNumPorts:
		    case LEAF_begemotBridgeBaseType:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		abort();

	    case SNMP_OP_ROLLBACK:
		return (bridge_rollback_if_status(ctx, val, sub));

	    case SNMP_OP_COMMIT:
		return (bridge_commit_if_status(val, sub));
	}
	abort();

get:
	switch (val->var.subs[sub - 1]) {
	    case LEAF_begemotBridgeBaseName:
		return (string_get(val, bif->bif_name, -1));

	    case LEAF_begemotBridgeBaseAddress:
		return (string_get(val, bif->br_addr.octet, ETHER_ADDR_LEN));

	    case LEAF_begemotBridgeBaseNumPorts:
		val->v.integer = bif->num_ports;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeBaseType:
		val->v.integer = bif->br_type;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeBaseStatus:
		val->v.integer = bif->if_status;
		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
op_begemot_stp(struct snmp_context *ctx, struct snmp_value *val,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;

	if (time(NULL) - bridge_list_age > bridge_get_data_maxage())
		bridge_update_all_ifs();

	switch (op) {
	    case SNMP_OP_GET:
		if ((bif = bridge_if_index_get(&val->var, sub)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_GETNEXT:
		if ((bif = bridge_if_index_getnext(&val->var, sub)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);
		bridge_if_index_append(&val->var, sub, bif);
		goto get;

	    case SNMP_OP_SET:
		if ((bif = bridge_if_index_get(&val->var, sub)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);

		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeStpPriority:
			if (val->v.integer > SNMP_BRIDGE_MAX_PRIORITY ||
			    val->v.integer % 4096 != 0)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->priority;
			if (bridge_set_priority(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpBridgeMaxAge:
			if (val->v.integer < SNMP_BRIDGE_MIN_MAGE ||
			    val->v.integer > SNMP_BRIDGE_MAX_MAGE)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->bridge_max_age;
			if (bridge_set_maxage(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpBridgeHelloTime:
			if (val->v.integer < SNMP_BRIDGE_MIN_HTIME ||
			    val->v.integer > SNMP_BRIDGE_MAX_HTIME)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->bridge_hello_time;
			if (bridge_set_hello_time(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpBridgeForwardDelay:
			if (val->v.integer < SNMP_BRIDGE_MIN_FDELAY ||
			    val->v.integer > SNMP_BRIDGE_MAX_FDELAY)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->bridge_fwd_delay;
			if (bridge_set_forward_delay(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpVersion:
			if (val->v.integer !=
			    begemotBridgeStpVersion_stpCompatible &&
			    val->v.integer != begemotBridgeStpVersion_rstp)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->stp_version;
			if (bridge_set_stp_version(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpTxHoldCount:
			if (val->v.integer < SNMP_BRIDGE_MIN_TXHC ||
			    val->v.integer > SNMP_BRIDGE_MAX_TXHC)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->tx_hold_count;
			if (bridge_set_tx_hold_count(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpProtocolSpecification:
		    case LEAF_begemotBridgeStpTimeSinceTopologyChange:
		    case LEAF_begemotBridgeStpTopChanges:
		    case LEAF_begemotBridgeStpDesignatedRoot:
		    case LEAF_begemotBridgeStpRootCost:
		    case LEAF_begemotBridgeStpRootPort:
		    case LEAF_begemotBridgeStpMaxAge:
		    case LEAF_begemotBridgeStpHelloTime:
		    case LEAF_begemotBridgeStpHoldTime:
		    case LEAF_begemotBridgeStpForwardDelay:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		abort();

	    case SNMP_OP_ROLLBACK:
		if ((bif = bridge_if_index_get(&val->var, sub)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);

		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeStpPriority:
			bridge_set_priority(bif, ctx->scratch->int1);
			break;

		    case LEAF_begemotBridgeStpBridgeMaxAge:
			bridge_set_maxage(bif, ctx->scratch->int1);
			break;

		    case LEAF_begemotBridgeStpBridgeHelloTime:
			bridge_set_hello_time(bif, ctx->scratch->int1);
			break;

		    case LEAF_begemotBridgeStpBridgeForwardDelay:
			bridge_set_forward_delay(bif, ctx->scratch->int1);
			break;

		    case LEAF_begemotBridgeStpVersion:
			bridge_set_stp_version(bif, ctx->scratch->int1);
			break;

		    case LEAF_begemotBridgeStpTxHoldCount:
			bridge_set_tx_hold_count(bif, ctx->scratch->int1);
			break;
		}
		return (SNMP_ERR_NOERROR);

	    case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	}
	abort();

get:
	switch (val->var.subs[sub - 1]) {
	    case LEAF_begemotBridgeStpProtocolSpecification:
		val->v.integer = bif->prot_spec;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpPriority:
		val->v.integer = bif->priority;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpTimeSinceTopologyChange:
		if (bridge_get_time_since_tc(bif, &(val->v.uint32)) < 0)
		    return (SNMP_ERR_GENERR);
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpTopChanges:
		val->v.uint32 = bif->top_changes;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpDesignatedRoot:
		return (string_get(val, bif->design_root, SNMP_BRIDGE_ID_LEN));

	    case LEAF_begemotBridgeStpRootCost:
		val->v.integer = bif->root_cost;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpRootPort:
		val->v.integer = bif->root_port;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpMaxAge:
		val->v.integer = bif->max_age;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpHelloTime:
		val->v.integer = bif->hello_time;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpHoldTime:
		val->v.integer = bif->hold_time;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpForwardDelay:
		val->v.integer = bif->fwd_delay;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpBridgeMaxAge:
		val->v.integer = bif->bridge_max_age;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpBridgeHelloTime:
		val->v.integer = bif->bridge_hello_time;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpBridgeForwardDelay:
		val->v.integer = bif->bridge_fwd_delay;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpVersion:
		val->v.integer = bif->stp_version;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpTxHoldCount:
		val->v.integer = bif->tx_hold_count;
		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
op_begemot_tp(struct snmp_context *ctx, struct snmp_value *val,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;

	if (time(NULL) - bridge_list_age > bridge_get_data_maxage())
		bridge_update_all_ifs();

	switch (op) {
	    case SNMP_OP_GET:
		if ((bif = bridge_if_index_get(&val->var, sub)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_GETNEXT:
		if ((bif = bridge_if_index_getnext(&val->var, sub)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);
		bridge_if_index_append(&val->var, sub, bif);
		goto get;

	    case SNMP_OP_SET:
		if ((bif = bridge_if_index_get(&val->var, sub)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);

		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeTpAgingTime:
			if (val->v.integer < SNMP_BRIDGE_MIN_AGE_TIME ||
			    val->v.integer > SNMP_BRIDGE_MAX_AGE_TIME)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bif->age_time;
			if (bridge_set_aging_time(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeTpMaxAddresses:
			ctx->scratch->int1 = bif->max_addrs;
			if (bridge_set_max_cache(bif, val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeTpLearnedEntryDiscards:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		abort();

	    case SNMP_OP_ROLLBACK:
		if ((bif = bridge_if_index_get(&val->var, sub)) == NULL)
		    return (SNMP_ERR_GENERR);

		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeTpAgingTime:
			bridge_set_aging_time(bif, ctx->scratch->int1);
			break;

		    case LEAF_begemotBridgeTpMaxAddresses:
			bridge_set_max_cache(bif, ctx->scratch->int1);
			break;
		}
		return (SNMP_ERR_NOERROR);

	    case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	}
	abort();

get:
	switch (val->var.subs[sub - 1]) {
	    case LEAF_begemotBridgeTpLearnedEntryDiscards:
		val->v.uint32 = bif->lrnt_drops;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeTpAgingTime:
		val->v.integer = bif->age_time;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeTpMaxAddresses:
		val->v.integer = bif->max_addrs;
		return (SNMP_ERR_NOERROR);
	}

	abort();
}
