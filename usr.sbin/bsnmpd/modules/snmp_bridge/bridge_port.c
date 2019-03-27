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
 * Bridge ports.
 *
 * $FreeBSD$
 */

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_mib.h>

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#include <bsnmp/snmpmod.h>
#include <bsnmp/snmp_mibII.h>

#define	SNMPTREE_TYPES
#include "bridge_tree.h"
#include "bridge_snmp.h"

TAILQ_HEAD(bridge_ports, bridge_port);

/*
 * Free the bridge base ports list.
 */
static void
bridge_ports_free(struct bridge_ports *headp)
{
	struct bridge_port *bp;

	while ((bp = TAILQ_FIRST(headp)) != NULL) {
		TAILQ_REMOVE(headp, bp, b_p);
		free(bp);
	}
}

/*
 * Free the bridge base ports from the base ports list,
 * members of a specified bridge interface only.
 */
static void
bridge_port_memif_free(struct bridge_ports *headp,
	struct bridge_if *bif)
{
	struct bridge_port *bp;

	while (bif->f_bp != NULL && bif->sysindex == bif->f_bp->sysindex) {
		bp = TAILQ_NEXT(bif->f_bp, b_p);
		TAILQ_REMOVE(headp, bif->f_bp, b_p);
		free(bif->f_bp);
		bif->f_bp = bp;
	}
}

/*
 * Insert a port entry in the base port TAILQ starting to search
 * for its place from the position of the first bridge port for the bridge
 * interface. Update the first bridge port if necessary.
 */
static void
bridge_port_insert_at(struct bridge_ports *headp,
	struct bridge_port *bp, struct bridge_port **f_bp)
{
	struct bridge_port *t1;

	assert(f_bp != NULL);

	for (t1 = *f_bp;
	    t1 != NULL && bp->sysindex == t1->sysindex;
	    t1 = TAILQ_NEXT(t1, b_p)) {
		if (bp->if_idx < t1->if_idx) {
			TAILQ_INSERT_BEFORE(t1, bp, b_p);
			if (*f_bp == t1)
				*f_bp = bp;
			return;
		}
	}

	/*
	 * Handle the case when our first port was actually the
	 * last element of the TAILQ.
	 */
	if (t1 == NULL)
		TAILQ_INSERT_TAIL(headp, bp, b_p);
	else
		TAILQ_INSERT_BEFORE(t1, bp, b_p);
}

/*
 * Find a port entry's position in the ports list according
 * to it's parent bridge interface name. Returns a NULL if
 * we should be at the TAILQ head, otherwise the entry after
 * which we should be inserted.
 */
static struct bridge_port *
bridge_port_find_pos(struct bridge_ports *headp, uint32_t b_idx)
{
	uint32_t t_idx;
	struct bridge_port *t1;

	if ((t1 = TAILQ_FIRST(headp)) == NULL ||
	    bridge_compare_sysidx(b_idx, t1->sysindex) < 0)
		return (NULL);

	t_idx = t1->sysindex;

	for (t1 = TAILQ_NEXT(t1, b_p); t1 != NULL; t1 = TAILQ_NEXT(t1, b_p)) {
		if (t1->sysindex != t_idx) {
			if (bridge_compare_sysidx(b_idx, t1->sysindex) < 0)
				return (TAILQ_PREV(t1, bridge_ports, b_p));
			else
				t_idx = t1->sysindex;
		}
	}

	if (t1 == NULL)
		t1 = TAILQ_LAST(headp, bridge_ports);

	return (t1);
}

/*
 * Insert a bridge member interface in the ports TAILQ.
 */
static void
bridge_port_memif_insert(struct bridge_ports *headp,
	struct bridge_port *bp, struct bridge_port **f_bp)
{
	struct bridge_port *temp;

	if (*f_bp != NULL)
		bridge_port_insert_at(headp, bp, f_bp);
	else {
		temp = bridge_port_find_pos(headp, bp->sysindex);

		if (temp == NULL)
			TAILQ_INSERT_HEAD(headp, bp, b_p);
		else
			TAILQ_INSERT_AFTER(headp, temp, bp, b_p);
		*f_bp = bp;
	}
}

/* The global ports list. */
static struct bridge_ports bridge_ports = TAILQ_HEAD_INITIALIZER(bridge_ports);
static time_t ports_list_age;

void
bridge_ports_update_listage(void)
{
	ports_list_age = time(NULL);
}

void
bridge_ports_fini(void)
{
	bridge_ports_free(&bridge_ports);
}

void
bridge_members_free(struct bridge_if *bif)
{
	bridge_port_memif_free(&bridge_ports, bif);
}

/*
 * Find the first port in the ports list.
 */
static struct bridge_port *
bridge_port_first(void)
{
	return (TAILQ_FIRST(&bridge_ports));
}

/*
 * Find the next port in the ports list.
 */
static struct bridge_port *
bridge_port_next(struct bridge_port *bp)
{
	return (TAILQ_NEXT(bp, b_p));
}

/*
 * Find the first member of the specified bridge interface.
 */
struct bridge_port *
bridge_port_bif_first(struct bridge_if *bif)
{
	return (bif->f_bp);
}

/*
 * Find the next member of the specified bridge interface.
 */
struct bridge_port *
bridge_port_bif_next(struct bridge_port *bp)
{
	struct bridge_port *bp_next;

	if ((bp_next = TAILQ_NEXT(bp, b_p)) == NULL ||
	    bp_next->sysindex != bp->sysindex)
		return (NULL);

	return (bp_next);
}

/*
 * Remove a bridge port from the ports list.
 */
void
bridge_port_remove(struct bridge_port *bp, struct bridge_if *bif)
{
	if (bif->f_bp == bp)
		bif->f_bp = bridge_port_bif_next(bp);

	TAILQ_REMOVE(&bridge_ports, bp, b_p);
	free(bp);
}

/*
 * Allocate memory for a new bridge port and insert it
 * in the base ports list. Return a pointer to the port's
 * structure in case we want to do anything else with it.
 */
struct bridge_port *
bridge_new_port(struct mibif *mif, struct bridge_if *bif)
{
	struct bridge_port *bp;

	if ((bp = (struct bridge_port *) malloc(sizeof(*bp))) == NULL) {
		syslog(LOG_ERR, "bridge new member: failed: %s",
			strerror(errno));
		return (NULL);
	}

	bzero(bp, sizeof(*bp));

	bp->sysindex = bif->sysindex;
	bp->if_idx = mif->index;
	bp->port_no = mif->sysindex;
	strlcpy(bp->p_name, mif->name, IFNAMSIZ);
	bp->circuit = oid_zeroDotZero;

	/*
	 * Initialize all rstpMib specific values to false/default.
	 * These will be set to their true values later if the bridge
	 * supports RSTP.
	 */
	bp->proto_migr = TruthValue_false;
	bp->admin_edge = TruthValue_false;
	bp->oper_edge = TruthValue_false;
	bp->oper_ptp = TruthValue_false;
	bp->admin_ptp = StpPortAdminPointToPointType_auto;

	bridge_port_memif_insert(&bridge_ports, bp, &(bif->f_bp));

	return (bp);
}

/*
 * Update our info from the corresponding mibII interface info.
 */
void
bridge_port_getinfo_mibif(struct mibif *m_if, struct bridge_port *bp)
{
	bp->max_info = m_if->mib.ifmd_data.ifi_mtu;
	bp->in_frames = m_if->mib.ifmd_data.ifi_ipackets;
	bp->out_frames = m_if->mib.ifmd_data.ifi_opackets;
	bp->in_drops = m_if->mib.ifmd_data.ifi_iqdrops;
}

/*
 * Find a port, whose SNMP's mibII ifIndex matches one of the ports,
 * members of the specified bridge interface.
 */
struct bridge_port *
bridge_port_find(int32_t if_idx, struct bridge_if *bif)
{
	struct bridge_port *bp;

	for (bp = bif->f_bp; bp != NULL; bp = TAILQ_NEXT(bp, b_p)) {
		if (bp->sysindex != bif->sysindex) {
			bp = NULL;
			break;
		}

		if (bp->if_idx == if_idx)
			break;
	}

	return (bp);
}

void
bridge_ports_dump(struct bridge_if *bif)
{
	struct bridge_port *bp;

	for (bp = bridge_port_bif_first(bif); bp != NULL;
	    bp = bridge_port_bif_next(bp)) {
		syslog(LOG_ERR, "memif - %s, index - %d",
		bp->p_name, bp->port_no);
	}
}

/*
 * RFC4188 specifics.
 */
int
op_dot1d_base_port(struct snmp_context *c __unused, struct snmp_value *val,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;
	struct bridge_port *bp;

	if ((bif = bridge_get_default()) == NULL)
		return (SNMP_ERR_NOSUCHNAME);

	if (time(NULL) - bif->ports_age > bridge_get_data_maxage() &&
	    bridge_update_memif(bif) <= 0)
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
		case SNMP_OP_GET:
		    if (val->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		    if ((bp = bridge_port_find(val->var.subs[sub],
			bif)) == NULL)
			    return (SNMP_ERR_NOSUCHNAME);
		    goto get;

		case SNMP_OP_GETNEXT:
		    if (val->var.len - sub == 0) {
			if ((bp = bridge_port_bif_first(bif)) == NULL)
			    return (SNMP_ERR_NOSUCHNAME);
		    } else {
			if ((bp = bridge_port_find(val->var.subs[sub],
			    bif)) == NULL ||
			    (bp = bridge_port_bif_next(bp)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
		    }
		    val->var.len = sub + 1;
		    val->var.subs[sub] = bp->port_no;
		    goto get;

		case SNMP_OP_SET:
		    return (SNMP_ERR_NOT_WRITEABLE);

		case SNMP_OP_ROLLBACK:
		case SNMP_OP_COMMIT:
		    break;
	}
	abort();

get:
	switch (val->var.subs[sub - 1]) {
	    case LEAF_dot1dBasePort:
		val->v.integer = bp->port_no;
		return (SNMP_ERR_NOERROR);

	    case LEAF_dot1dBasePortIfIndex:
		val->v.integer = bp->if_idx;
		return (SNMP_ERR_NOERROR);

	    case LEAF_dot1dBasePortCircuit:
		val->v.oid = bp->circuit;
		return (SNMP_ERR_NOERROR);

	    case LEAF_dot1dBasePortDelayExceededDiscards:
		val->v.uint32 = bp->dly_ex_drops;
		return (SNMP_ERR_NOERROR);

	    case LEAF_dot1dBasePortMtuExceededDiscards:
		val->v.uint32 = bp->dly_mtu_drops;
		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
op_dot1d_stp_port(struct snmp_context *ctx, struct snmp_value *val,
	 uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;
	struct bridge_port *bp;

	if ((bif = bridge_get_default()) == NULL)
		return (SNMP_ERR_NOSUCHNAME);

	if (time(NULL) - bif->ports_age > bridge_get_data_maxage() &&
	    bridge_update_memif(bif) <= 0)
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
		case SNMP_OP_GET:
		    if (val->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		    if ((bp = bridge_port_find(val->var.subs[sub],
			bif)) == NULL)
			    return (SNMP_ERR_NOSUCHNAME);
		    goto get;

		case SNMP_OP_GETNEXT:
		    if (val->var.len - sub == 0) {
			if ((bp = bridge_port_bif_first(bif)) == NULL)
			    return (SNMP_ERR_NOSUCHNAME);
		    } else {
			if ((bp = bridge_port_find(val->var.subs[sub],
			    bif)) == NULL ||
			    (bp = bridge_port_bif_next(bp)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
		    }
		    val->var.len = sub + 1;
		    val->var.subs[sub] = bp->port_no;
		    goto get;

		case SNMP_OP_SET:
		    if (val->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		    if ((bp = bridge_port_find(val->var.subs[sub],
			bif)) == NULL)
			    return (SNMP_ERR_NOSUCHNAME);

		    switch (val->var.subs[sub - 1]) {
			case LEAF_dot1dStpPortPriority:
			    if (val->v.integer < 0 || val->v.integer > 255)
				return (SNMP_ERR_WRONG_VALUE);

			    ctx->scratch->int1 = bp->priority;
			    if (bridge_port_set_priority(bif->bif_name, bp,
				val->v.integer) < 0)
				return (SNMP_ERR_GENERR);
			    return (SNMP_ERR_NOERROR);

			case LEAF_dot1dStpPortEnable:
			    if (val->v.integer != dot1dStpPortEnable_enabled &&
				val->v.integer != dot1dStpPortEnable_disabled)
				return (SNMP_ERR_WRONG_VALUE);

			    ctx->scratch->int1 = bp->enable;
			    if (bridge_port_set_stp_enable(bif->bif_name,
				bp, val->v.integer) < 0)
				return (SNMP_ERR_GENERR);
			    return (SNMP_ERR_NOERROR);

			case LEAF_dot1dStpPortPathCost:
			    if (val->v.integer < SNMP_PORT_MIN_PATHCOST ||
				val->v.integer > SNMP_PORT_MAX_PATHCOST)
				return (SNMP_ERR_WRONG_VALUE);

			    ctx->scratch->int1 = bp->path_cost;
			    if (bridge_port_set_path_cost(bif->bif_name, bp,
				val->v.integer) < 0)
				return (SNMP_ERR_GENERR);
			    return (SNMP_ERR_NOERROR);

			case LEAF_dot1dStpPort:
			case LEAF_dot1dStpPortState:
			case LEAF_dot1dStpPortDesignatedRoot:
			case LEAF_dot1dStpPortDesignatedCost:
			case LEAF_dot1dStpPortDesignatedBridge:
			case LEAF_dot1dStpPortDesignatedPort:
			case LEAF_dot1dStpPortForwardTransitions:
			    return (SNMP_ERR_NOT_WRITEABLE);
		    }
		    abort();

		case SNMP_OP_ROLLBACK:
		    if ((bp = bridge_port_find(val->var.subs[sub],
			bif)) == NULL)
			    return (SNMP_ERR_GENERR);
		    switch (val->var.subs[sub - 1]) {
			case LEAF_dot1dStpPortPriority:
			    bridge_port_set_priority(bif->bif_name, bp,
				ctx->scratch->int1);
			    break;
			case LEAF_dot1dStpPortEnable:
			    bridge_port_set_stp_enable(bif->bif_name, bp,
				ctx->scratch->int1);
			    break;
			case LEAF_dot1dStpPortPathCost:
			    bridge_port_set_path_cost(bif->bif_name, bp,
				ctx->scratch->int1);
			    break;
		    }
		    return (SNMP_ERR_NOERROR);

		case SNMP_OP_COMMIT:
		    return (SNMP_ERR_NOERROR);
	}
	abort();

get:
	switch (val->var.subs[sub - 1]) {
		case LEAF_dot1dStpPort:
			val->v.integer = bp->port_no;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortPriority:
			val->v.integer = bp->priority;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortState:
			val->v.integer = bp->state;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortEnable:
			val->v.integer = bp->enable;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortPathCost:
			val->v.integer = bp->path_cost;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortDesignatedRoot:
			return (string_get(val, bp->design_root,
			    SNMP_BRIDGE_ID_LEN));

		case LEAF_dot1dStpPortDesignatedCost:
			val->v.integer = bp->design_cost;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortDesignatedBridge:
			return (string_get(val, bp->design_bridge,
			    SNMP_BRIDGE_ID_LEN));

		case LEAF_dot1dStpPortDesignatedPort:
			return (string_get(val, bp->design_port, 2));

		case LEAF_dot1dStpPortForwardTransitions:
			val->v.uint32 = bp->fwd_trans;
			return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
op_dot1d_stp_ext_port(struct snmp_context *ctx, struct snmp_value *val,
    uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;
	struct bridge_port *bp;

	if ((bif = bridge_get_default()) == NULL)
		return (SNMP_ERR_NOSUCHNAME);

	if (time(NULL) - bif->ports_age > bridge_get_data_maxage() &&
	    bridge_update_memif(bif) <= 0)
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
		case SNMP_OP_GET:
		    if (val->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		    if ((bp = bridge_port_find(val->var.subs[sub],
			bif)) == NULL)
			    return (SNMP_ERR_NOSUCHNAME);
		    goto get;

		case SNMP_OP_GETNEXT:
		    if (val->var.len - sub == 0) {
			if ((bp = bridge_port_bif_first(bif)) == NULL)
			    return (SNMP_ERR_NOSUCHNAME);
		    } else {
			if ((bp = bridge_port_find(val->var.subs[sub],
			    bif)) == NULL ||
			    (bp = bridge_port_bif_next(bp)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
		    }
		    val->var.len = sub + 1;
		    val->var.subs[sub] = bp->port_no;
		    goto get;

		case SNMP_OP_SET:
		    if (val->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		    if ((bp = bridge_port_find(val->var.subs[sub],
			bif)) == NULL)
			    return (SNMP_ERR_NOSUCHNAME);

		    switch (val->var.subs[sub - 1]) {
			case LEAF_dot1dStpPortAdminEdgePort:
			    if (val->v.integer != TruthValue_true &&
				val->v.integer != TruthValue_false)
				return (SNMP_ERR_WRONG_VALUE);

			    ctx->scratch->int1 = bp->admin_edge;
			    if (bridge_port_set_admin_edge(bif->bif_name, bp,
				val->v.integer) < 0)
				return (SNMP_ERR_GENERR);
			    return (SNMP_ERR_NOERROR);

			case LEAF_dot1dStpPortAdminPointToPoint:
			    if (val->v.integer < 0 || val->v.integer >
				StpPortAdminPointToPointType_auto)
				return (SNMP_ERR_WRONG_VALUE);

			    ctx->scratch->int1 = bp->admin_ptp;
			    if (bridge_port_set_admin_ptp(bif->bif_name, bp,
				val->v.integer) < 0)
				return (SNMP_ERR_GENERR);
			    return (SNMP_ERR_NOERROR);

			case LEAF_dot1dStpPortAdminPathCost:
			    if (val->v.integer < SNMP_PORT_MIN_PATHCOST ||
				val->v.integer > SNMP_PORT_MAX_PATHCOST)
				return (SNMP_ERR_WRONG_VALUE);

			    ctx->scratch->int1 = bp->admin_path_cost;
			    if (bridge_port_set_path_cost(bif->bif_name, bp,
				val->v.integer) < 0)
				return (SNMP_ERR_GENERR);
			    return (SNMP_ERR_NOERROR);

			case LEAF_dot1dStpPortProtocolMigration:
			case LEAF_dot1dStpPortOperEdgePort:
			case LEAF_dot1dStpPortOperPointToPoint:
			    return (SNMP_ERR_NOT_WRITEABLE);
		    }
		    abort();

		case SNMP_OP_ROLLBACK:
		    if ((bp = bridge_port_find(val->var.subs[sub],
			bif)) == NULL)
			    return (SNMP_ERR_GENERR);

		    switch (val->var.subs[sub - 1]) {
			case LEAF_dot1dStpPortAdminEdgePort:
			    bridge_port_set_admin_edge(bif->bif_name, bp,
				ctx->scratch->int1);
			    break;
			case LEAF_dot1dStpPortAdminPointToPoint:
			    bridge_port_set_admin_ptp(bif->bif_name, bp,
				ctx->scratch->int1);
			    break;
			case LEAF_dot1dStpPortAdminPathCost:
			    bridge_port_set_path_cost(bif->bif_name, bp,
				ctx->scratch->int1);
			    break;
		    }
		    return (SNMP_ERR_NOERROR);

		case SNMP_OP_COMMIT:
		    return (SNMP_ERR_NOERROR);
	}
	abort();

get:
	switch (val->var.subs[sub - 1]) {
		case LEAF_dot1dStpPortProtocolMigration:
			val->v.integer = bp->proto_migr;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortAdminEdgePort:
			val->v.integer = bp->admin_edge;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortOperEdgePort:
			val->v.integer = bp->oper_edge;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortAdminPointToPoint:
			val->v.integer = bp->admin_ptp;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortOperPointToPoint:
			val->v.integer = bp->oper_ptp;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dStpPortAdminPathCost:
			val->v.integer = bp->admin_path_cost;
			return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
op_dot1d_tp_port(struct snmp_context *c __unused, struct snmp_value *val,
    uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;
	struct bridge_port *bp;

	if ((bif = bridge_get_default()) == NULL)
		return (SNMP_ERR_NOSUCHNAME);

	if (time(NULL) - bif->ports_age > bridge_get_data_maxage() &&
	    bridge_update_memif(bif) <= 0)
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
		case SNMP_OP_GET:
		    if (val->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		    if ((bp = bridge_port_find(val->var.subs[sub],
			bif)) == NULL)
			    return (SNMP_ERR_NOSUCHNAME);
		    goto get;

		case SNMP_OP_GETNEXT:
		    if (val->var.len - sub == 0) {
			if ((bp = bridge_port_bif_first(bif)) == NULL)
			    return (SNMP_ERR_NOSUCHNAME);
		    } else {
			if ((bp = bridge_port_find(val->var.subs[sub],
			    bif)) == NULL ||
			    (bp = bridge_port_bif_next(bp)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
		    }
		    val->var.len = sub + 1;
		    val->var.subs[sub] = bp->port_no;
		    goto get;

		case SNMP_OP_SET:
		    return (SNMP_ERR_NOT_WRITEABLE);

		case SNMP_OP_ROLLBACK:
		case SNMP_OP_COMMIT:
		    break;
	}
	abort();

get:
	switch (val->var.subs[sub - 1]) {
		case LEAF_dot1dTpPort:
			val->v.integer = bp->port_no;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dTpPortMaxInfo:
			val->v.integer = bp->max_info;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dTpPortInFrames:
			val->v.uint32 = bp->in_frames;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dTpPortOutFrames:
			val->v.uint32 = bp->out_frames;
			return (SNMP_ERR_NOERROR);

		case LEAF_dot1dTpPortInDiscards:
			val->v.uint32 = bp->in_drops;
			return (SNMP_ERR_NOERROR);
	}

	abort();
}

/*
 * Private BEGEMOT-BRIDGE-MIB specifics.
 */

/*
 * Construct a bridge port entry index.
 */
static int
bridge_port_index_append(struct asn_oid *oid, uint sub,
	const struct bridge_port *bp)
{
	uint i;
	const char *b_name;

	if ((b_name = bridge_if_find_name(bp->sysindex)) == NULL)
		return (-1);

	oid->len = sub + strlen(b_name) + 1 + 1;
	oid->subs[sub] = strlen(b_name);

	for (i = 1; i <= strlen(b_name); i++)
		oid->subs[sub + i] = b_name[i - 1];

	oid->subs[sub + i] = bp->port_no;

	return (0);
}

/*
 * Get the port entry from an entry's index.
 */
static struct bridge_port *
bridge_port_index_get(const struct asn_oid *oid, uint sub, int8_t status)
{
	uint i;
	int32_t port_no;
	char bif_name[IFNAMSIZ];
	struct bridge_if *bif;
	struct bridge_port *bp;

	if (oid->len - sub != oid->subs[sub] + 2 ||
	    oid->subs[sub] >= IFNAMSIZ)
		return (NULL);

	for (i = 0; i < oid->subs[sub]; i++)
		bif_name[i] = oid->subs[sub + i + 1];
	bif_name[i] = '\0';

	port_no = oid->subs[sub + i + 1];

	if ((bif = bridge_if_find_ifname(bif_name)) == NULL)
		return (NULL);

	if ((bp = bridge_port_find(port_no, bif)) == NULL ||
	    (status == 0 && bp->status != RowStatus_active))
		return (NULL);

	return (bp);
}

/*
 * Get the next port entry from an entry's index.
 */
static struct bridge_port *
bridge_port_index_getnext(const struct asn_oid *oid, uint sub, int8_t status)
{
	uint i;
	int32_t port_no;
	char bif_name[IFNAMSIZ];
	struct bridge_if *bif;
	struct bridge_port *bp;

	if (oid->len - sub == 0)
		bp = bridge_port_first();
	else {
		if (oid->len - sub != oid->subs[sub] + 2 ||
		    oid->subs[sub] >= IFNAMSIZ)
			return (NULL);

		for (i = 0; i < oid->subs[sub]; i++)
			bif_name[i] = oid->subs[sub + i + 1];
		bif_name[i] = '\0';

		port_no = oid->subs[sub + i + 1];

		if ((bif = bridge_if_find_ifname(bif_name)) == NULL ||
		    (bp = bridge_port_find(port_no, bif)) == NULL)
			return (NULL);

		bp = bridge_port_next(bp);
	}

	if (status == 1)
		return (bp);

	while (bp != NULL) {
		if (bp->status == RowStatus_active)
			break;
		bp = bridge_port_next(bp);
	}

	return (bp);
}

/*
 * Read the bridge name and port index from a ASN OID structure.
 */
static int
bridge_port_index_decode(const struct asn_oid *oid, uint sub,
	char *b_name, int32_t *idx)
{
	uint i;

	if (oid->len - sub != oid->subs[sub] + 2 ||
	    oid->subs[sub] >= IFNAMSIZ)
		return (-1);

	for (i = 0; i < oid->subs[sub]; i++)
		b_name[i] = oid->subs[sub + i + 1];
	b_name[i] = '\0';

	*idx = oid->subs[sub + i + 1];
	return (0);
}

static int
bridge_port_set_status(struct snmp_context *ctx,
	struct snmp_value *val, uint sub)
{
	int32_t if_idx;
	char b_name[IFNAMSIZ];
	struct bridge_if *bif;
	struct bridge_port *bp;
	struct mibif *mif;

	if (bridge_port_index_decode(&val->var, sub, b_name, &if_idx) < 0)
		return (SNMP_ERR_INCONS_VALUE);

	if ((bif = bridge_if_find_ifname(b_name)) == NULL ||
	    (mif = mib_find_if(if_idx)) == NULL)
		return (SNMP_ERR_INCONS_VALUE);

	bp = bridge_port_find(if_idx, bif);

	switch (val->v.integer) {
	    case RowStatus_active:
		if (bp == NULL)
		    return (SNMP_ERR_INCONS_VALUE);

		if (bp->span_enable == 0)
		    return (SNMP_ERR_INCONS_VALUE);

		ctx->scratch->int1 = bp->status;
		bp->status = RowStatus_active;
		break;

	    case RowStatus_notInService:
		if (bp == NULL || bp->span_enable == 0 ||
		    bp->status == RowStatus_active)
			return (SNMP_ERR_INCONS_VALUE);

		ctx->scratch->int1 = bp->status;
		bp->status = RowStatus_notInService;

	    case RowStatus_notReady:
		/* FALLTHROUGH */
	    case RowStatus_createAndGo:
		return (SNMP_ERR_INCONS_VALUE);

	    case RowStatus_createAndWait:
		if (bp != NULL)
		    return (SNMP_ERR_INCONS_VALUE);

		if ((bp = bridge_new_port(mif, bif)) == NULL)
			return (SNMP_ERR_GENERR);

		ctx->scratch->int1 = RowStatus_destroy;
		bp->status = RowStatus_notReady;
		break;

	    case RowStatus_destroy:
		if (bp == NULL)
		    return (SNMP_ERR_INCONS_VALUE);

		ctx->scratch->int1 = bp->status;
		bp->status = RowStatus_destroy;
		break;
	}

	return (SNMP_ERR_NOERROR);
}

static int
bridge_port_rollback_status(struct snmp_context *ctx,
	struct snmp_value *val, uint sub)
{
	int32_t if_idx;
	char b_name[IFNAMSIZ];
	struct bridge_if *bif;
	struct bridge_port *bp;

	if (bridge_port_index_decode(&val->var, sub, b_name, &if_idx) < 0)
		return (SNMP_ERR_GENERR);

	if ((bif = bridge_if_find_ifname(b_name)) == NULL ||
	    (bp = bridge_port_find(if_idx, bif)) == NULL)
		return (SNMP_ERR_GENERR);

	if (ctx->scratch->int1 == RowStatus_destroy)
		bridge_port_remove(bp, bif);
	else
		bp->status = ctx->scratch->int1;

	return (SNMP_ERR_NOERROR);
}

static int
bridge_port_commit_status(struct snmp_value *val, uint sub)
{
	int32_t if_idx;
	char b_name[IFNAMSIZ];
	struct bridge_if *bif;
	struct bridge_port *bp;

	if (bridge_port_index_decode(&val->var, sub, b_name, &if_idx) < 0)
		return (SNMP_ERR_GENERR);

	if ((bif = bridge_if_find_ifname(b_name)) == NULL ||
	    (bp = bridge_port_find(if_idx, bif)) == NULL)
		return (SNMP_ERR_GENERR);

	switch (bp->status) {
		case RowStatus_active:
			if (bridge_port_addm(bp, b_name) < 0)
				return (SNMP_ERR_COMMIT_FAILED);
			break;

		case RowStatus_destroy:
			if (bridge_port_delm(bp, b_name) < 0)
				return (SNMP_ERR_COMMIT_FAILED);
			bridge_port_remove(bp, bif);
			break;
	}

	return (SNMP_ERR_NOERROR);
}

static int
bridge_port_set_span_enable(struct snmp_context *ctx,
		struct snmp_value *val, uint sub)
{
	int32_t if_idx;
	char b_name[IFNAMSIZ];
	struct bridge_if *bif;
	struct bridge_port *bp;
	struct mibif *mif;

	if (val->v.integer != begemotBridgeBaseSpanEnabled_enabled &&
	    val->v.integer != begemotBridgeBaseSpanEnabled_disabled)
		return (SNMP_ERR_BADVALUE);

	if (bridge_port_index_decode(&val->var, sub, b_name, &if_idx) < 0)
		return (SNMP_ERR_INCONS_VALUE);

	if ((bif = bridge_if_find_ifname(b_name)) == NULL)
		return (SNMP_ERR_INCONS_VALUE);

	if ((bp = bridge_port_find(if_idx, bif)) == NULL) {
		if ((mif = mib_find_if(if_idx)) == NULL)
			return (SNMP_ERR_INCONS_VALUE);

		if ((bp = bridge_new_port(mif, bif)) == NULL)
			return (SNMP_ERR_GENERR);

		ctx->scratch->int1 = RowStatus_destroy;
	} else if (bp->status == RowStatus_active) {
		return (SNMP_ERR_INCONS_VALUE);
	} else {
		ctx->scratch->int1 = bp->status;
	}

	bp->span_enable = val->v.integer;
	bp->status = RowStatus_notInService;

	return (SNMP_ERR_NOERROR);
}

int
op_begemot_base_port(struct snmp_context *ctx, struct snmp_value *val,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	int8_t status, which;
	const char *bname;
	struct bridge_port *bp;

	if (time(NULL) - ports_list_age > bridge_get_data_maxage())
		bridge_update_all_ports();

	which = val->var.subs[sub - 1];
	status = 0;

	switch (op) {
	    case SNMP_OP_GET:
		if (which == LEAF_begemotBridgeBaseSpanEnabled ||
		    which == LEAF_begemotBridgeBasePortStatus)
			status = 1;
		if ((bp = bridge_port_index_get(&val->var, sub,
		    status)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_GETNEXT:
		if (which == LEAF_begemotBridgeBaseSpanEnabled ||
		    which == LEAF_begemotBridgeBasePortStatus)
			status = 1;
		if ((bp = bridge_port_index_getnext(&val->var, sub,
		    status)) == NULL ||
		    bridge_port_index_append(&val->var, sub, bp) < 0)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_SET:
		switch (which) {
		    case LEAF_begemotBridgeBaseSpanEnabled:
			return (bridge_port_set_span_enable(ctx, val, sub));

		    case LEAF_begemotBridgeBasePortStatus:
			return (bridge_port_set_status(ctx, val, sub));

		    case LEAF_begemotBridgeBasePortPrivate:
			if ((bp = bridge_port_index_get(&val->var, sub,
			    status)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			if ((bname = bridge_if_find_name(bp->sysindex)) == NULL)
				return (SNMP_ERR_GENERR);
			ctx->scratch->int1 = bp->priv_set;
			return (bridge_port_set_private(bname, bp,
			    val->v.integer));

		    case LEAF_begemotBridgeBasePort:
		    case LEAF_begemotBridgeBasePortIfIndex:
		    case LEAF_begemotBridgeBasePortDelayExceededDiscards:
		    case LEAF_begemotBridgeBasePortMtuExceededDiscards:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		abort();

	    case SNMP_OP_ROLLBACK:
		switch (which) {
		    case LEAF_begemotBridgeBaseSpanEnabled:
			/* FALLTHROUGH */
		    case LEAF_begemotBridgeBasePortStatus:
			return (bridge_port_rollback_status(ctx, val, sub));
		    case LEAF_begemotBridgeBasePortPrivate:
			if ((bp = bridge_port_index_get(&val->var, sub,
			    status)) == NULL)
				return (SNMP_ERR_GENERR);
			if ((bname = bridge_if_find_name(bp->sysindex)) == NULL)
				return (SNMP_ERR_GENERR);
			return (bridge_port_set_private(bname, bp,
			    ctx->scratch->int1));
		}
		return (SNMP_ERR_NOERROR);

	    case SNMP_OP_COMMIT:
		if (which == LEAF_begemotBridgeBasePortStatus)
			return (bridge_port_commit_status(val, sub));

		return (SNMP_ERR_NOERROR);
	}
	abort();

get:
	switch (which) {
	    case LEAF_begemotBridgeBasePort:
		val->v.integer = bp->port_no;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeBasePortIfIndex:
		val->v.integer = bp->if_idx;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeBaseSpanEnabled:
		val->v.integer = bp->span_enable;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeBasePortDelayExceededDiscards:
		val->v.uint32 = bp->dly_ex_drops;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeBasePortMtuExceededDiscards:
		val->v.uint32 = bp->dly_mtu_drops;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeBasePortStatus:
		val->v.integer = bp->status;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeBasePortPrivate:
		val->v.integer = bp->priv_set;
		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
op_begemot_stp_port(struct snmp_context *ctx, struct snmp_value *val,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_port *bp;
	const char *b_name;

	if (time(NULL) - ports_list_age > bridge_get_data_maxage())
		bridge_update_all_ports();

	switch (op) {
	    case SNMP_OP_GET:
		if ((bp = bridge_port_index_get(&val->var, sub, 0)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_GETNEXT:
		if ((bp = bridge_port_index_getnext(&val->var, sub, 0)) ==
		    NULL || bridge_port_index_append(&val->var, sub, bp) < 0)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_SET:
		if ((bp = bridge_port_index_get(&val->var, sub, 0)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if ((b_name = bridge_if_find_name(bp->sysindex)) == NULL)
			return (SNMP_ERR_GENERR);

		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeStpPortPriority:
			if (val->v.integer < 0 || val->v.integer > 255)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bp->priority;
			if (bridge_port_set_priority(b_name, bp,
			    val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpPortEnable:
			if (val->v.integer !=
			    begemotBridgeStpPortEnable_enabled ||
			    val->v.integer !=
			    begemotBridgeStpPortEnable_disabled)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bp->enable;
			if (bridge_port_set_stp_enable(b_name, bp,
			    val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpPortPathCost:
			if (val->v.integer < SNMP_PORT_MIN_PATHCOST ||
			    val->v.integer > SNMP_PORT_MAX_PATHCOST)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bp->path_cost;
			if (bridge_port_set_path_cost(b_name, bp,
			    val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpPort:
		    case LEAF_begemotBridgeStpPortState:
		    case LEAF_begemotBridgeStpPortDesignatedRoot:
		    case LEAF_begemotBridgeStpPortDesignatedCost:
		    case LEAF_begemotBridgeStpPortDesignatedBridge:
		    case LEAF_begemotBridgeStpPortDesignatedPort:
		    case LEAF_begemotBridgeStpPortForwardTransitions:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		abort();

	    case SNMP_OP_ROLLBACK:
		if ((bp = bridge_port_index_get(&val->var, sub, 0)) == NULL ||
		    (b_name = bridge_if_find_name(bp->sysindex)) == NULL)
			return (SNMP_ERR_GENERR);

		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeStpPortPriority:
			bridge_port_set_priority(b_name, bp,
			    ctx->scratch->int1);
			break;
		    case LEAF_begemotBridgeStpPortEnable:
			bridge_port_set_stp_enable(b_name, bp,
			    ctx->scratch->int1);
			break;
		    case LEAF_begemotBridgeStpPortPathCost:
			bridge_port_set_path_cost(b_name, bp,
			    ctx->scratch->int1);
			break;
		}
		return (SNMP_ERR_NOERROR);

	    case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	}
	abort();

get:
	switch (val->var.subs[sub - 1]) {
	    case LEAF_begemotBridgeStpPort:
		val->v.integer = bp->port_no;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpPortPriority:
		val->v.integer = bp->priority;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpPortState:
		val->v.integer = bp->state;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpPortEnable:
		val->v.integer = bp->enable;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpPortPathCost:
		val->v.integer = bp->path_cost;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpPortDesignatedRoot:
		return (string_get(val, bp->design_root, SNMP_BRIDGE_ID_LEN));

	    case LEAF_begemotBridgeStpPortDesignatedCost:
		val->v.integer = bp->design_cost;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeStpPortDesignatedBridge:
		return (string_get(val, bp->design_bridge, SNMP_BRIDGE_ID_LEN));

	    case LEAF_begemotBridgeStpPortDesignatedPort:
		return (string_get(val, bp->design_port, 2));

	    case LEAF_begemotBridgeStpPortForwardTransitions:
		val->v.uint32 = bp->fwd_trans;
		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
op_begemot_stp_ext_port(struct snmp_context *ctx, struct snmp_value *val,
    uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_port *bp;
	const char *b_name;

	if (time(NULL) - ports_list_age > bridge_get_data_maxage())
		bridge_update_all_ports();

	switch (op) {
	    case SNMP_OP_GET:
		if ((bp = bridge_port_index_get(&val->var, sub, 0)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_GETNEXT:
		if ((bp = bridge_port_index_getnext(&val->var, sub, 0)) ==
		    NULL || bridge_port_index_append(&val->var, sub, bp) < 0)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_SET:
		if ((bp = bridge_port_index_get(&val->var, sub, 0)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		if ((b_name = bridge_if_find_name(bp->sysindex)) == NULL)
			return (SNMP_ERR_GENERR);

		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeStpPortAdminEdgePort:
			if (val->v.integer != TruthValue_true &&
			    val->v.integer != TruthValue_false)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bp->admin_edge;
			if (bridge_port_set_admin_edge(b_name, bp,
			    val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpPortAdminPointToPoint:
			if (val->v.integer < 0 || val->v.integer >
			    StpPortAdminPointToPointType_auto)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bp->admin_ptp;
			if (bridge_port_set_admin_ptp(b_name, bp,
			    val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpPortAdminPathCost:
			if (val->v.integer < SNMP_PORT_MIN_PATHCOST ||
			    val->v.integer > SNMP_PORT_MAX_PATHCOST)
			    return (SNMP_ERR_WRONG_VALUE);

			ctx->scratch->int1 = bp->admin_path_cost;
			if (bridge_port_set_path_cost(b_name, bp,
			    val->v.integer) < 0)
			    return (SNMP_ERR_GENERR);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeStpPortProtocolMigration:
		    case LEAF_begemotBridgeStpPortOperEdgePort:
		    case LEAF_begemotBridgeStpPortOperPointToPoint:
			return (SNMP_ERR_NOT_WRITEABLE);
		}
		abort();

	    case SNMP_OP_ROLLBACK:
		if ((bp = bridge_port_index_get(&val->var, sub, 0)) == NULL ||
		    (b_name = bridge_if_find_name(bp->sysindex)) == NULL)
			return (SNMP_ERR_GENERR);

		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeStpPortAdminEdgePort:
			bridge_port_set_admin_edge(b_name, bp,
			    ctx->scratch->int1);
			break;
		    case LEAF_begemotBridgeStpPortAdminPointToPoint:
			bridge_port_set_admin_ptp(b_name, bp,
			    ctx->scratch->int1);
			break;
		    case LEAF_begemotBridgeStpPortAdminPathCost:
			bridge_port_set_path_cost(b_name, bp,
			    ctx->scratch->int1);
			break;
		}
		return (SNMP_ERR_NOERROR);

	    case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	}
	abort();

get:
	switch (val->var.subs[sub - 1]) {
		case LEAF_begemotBridgeStpPortProtocolMigration:
			val->v.integer = bp->proto_migr;
			return (SNMP_ERR_NOERROR);

		case LEAF_begemotBridgeStpPortAdminEdgePort:
			val->v.integer = bp->admin_edge;
			return (SNMP_ERR_NOERROR);

		case LEAF_begemotBridgeStpPortOperEdgePort:
			val->v.integer = bp->oper_edge;
			return (SNMP_ERR_NOERROR);

		case LEAF_begemotBridgeStpPortAdminPointToPoint:
			val->v.integer = bp->admin_ptp;
			return (SNMP_ERR_NOERROR);

		case LEAF_begemotBridgeStpPortOperPointToPoint:
			val->v.integer = bp->oper_ptp;
			return (SNMP_ERR_NOERROR);

		case LEAF_begemotBridgeStpPortAdminPathCost:
			val->v.integer = bp->admin_path_cost;
			return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
op_begemot_tp_port(struct snmp_context *c __unused, struct snmp_value *val,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_port *bp;

	if (time(NULL) - ports_list_age > bridge_get_data_maxage())
		bridge_update_all_ports();

	switch (op) {
	    case SNMP_OP_GET:
		if ((bp = bridge_port_index_get(&val->var, sub, 0)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_GETNEXT:
		if ((bp = bridge_port_index_getnext(&val->var, sub, 0)) ==
		    NULL || bridge_port_index_append(&val->var, sub, bp) < 0)
		    return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);

	    case SNMP_OP_ROLLBACK:
	    case SNMP_OP_COMMIT:
		break;
	}
	abort();

get:
	switch (val->var.subs[sub - 1]) {
	    case LEAF_begemotBridgeTpPort:
		val->v.integer = bp->port_no;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeTpPortMaxInfo:
		val->v.integer = bp->max_info;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeTpPortInFrames:
		val->v.uint32 = bp->in_frames;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeTpPortOutFrames:
		val->v.uint32 = bp->out_frames;
		return (SNMP_ERR_NOERROR);

	    case LEAF_begemotBridgeTpPortInDiscards:
		val->v.uint32 = bp->in_drops;
		return (SNMP_ERR_NOERROR);
	}

	abort();
}
