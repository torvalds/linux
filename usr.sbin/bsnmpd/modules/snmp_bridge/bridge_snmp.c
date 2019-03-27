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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_mib.h>
#include <net/if_types.h>

#include <errno.h>
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
#include "bridge_oid.h"

static struct lmodule *bridge_module;

/* For the registration. */
static const struct asn_oid oid_dot1Bridge = OIDX_dot1dBridge;
/* The registration. */
static uint reg_bridge;

/* Periodic timer for polling all bridges' data. */
static void *bridge_data_timer;
static void *bridge_tc_timer;

static int bridge_data_maxage = SNMP_BRIDGE_DATA_MAXAGE;
static int bridge_poll_ticks = SNMP_BRIDGE_POLL_INTERVAL * 100;
static int bridge_tc_poll_ticks = SNMP_BRIDGE_TC_POLL_INTERVAL * 100;

/*
 * Our default bridge, whose info will be visible under
 * the dot1dBridge subtree and functions to set/fetch it.
 */
static char bif_default_name[IFNAMSIZ] = "bridge0";
static struct bridge_if *bif_default;

struct bridge_if *
bridge_get_default(void)
{
	struct mibif *ifp;

	if (bif_default != NULL) {

		/* Walk through the mibII interface list. */
		for (ifp = mib_first_if(); ifp != NULL; ifp = mib_next_if(ifp))
			if (strcmp(ifp->name, bif_default->bif_name) == 0)
				break;

		if (ifp == NULL)
			bif_default = NULL;
	}

	return (bif_default);
}

void
bridge_set_default(struct bridge_if *bif)
{
	bif_default = bif;

	syslog(LOG_ERR, "Set default bridge interface to: %s",
	    bif == NULL ? "(none)" : bif->bif_name);
}

const char *
bridge_get_default_name(void)
{
	return (bif_default_name);
}

static int
bridge_set_default_name(const char *bif_name, uint len)
{
	struct bridge_if *bif;

	if (len >= IFNAMSIZ)
		return (-1);

	bcopy(bif_name, bif_default_name, len);
	bif_default_name[len] = '\0';

	if ((bif = bridge_if_find_ifname(bif_default_name)) == NULL) {
		bif_default = NULL;
		return (0);
	}

	bif_default = bif;
	return (1);
}

int
bridge_get_data_maxage(void)
{
	return (bridge_data_maxage);
}

static void
bridge_set_poll_ticks(int poll_ticks)
{
	if (bridge_data_timer != NULL)
		timer_stop(bridge_data_timer);

	bridge_poll_ticks = poll_ticks;
	bridge_data_timer = timer_start_repeat(bridge_poll_ticks,
	    bridge_poll_ticks, bridge_update_all, NULL, bridge_module);
}
/*
 * The bridge module configuration via SNMP.
 */
static int
bridge_default_name_save(struct snmp_context *ctx, const char *bridge_default)
{
	if ((ctx->scratch->int1 = strlen(bridge_default)) >= IFNAMSIZ)
		return (-1);

	if ((ctx->scratch->ptr1 = malloc(IFNAMSIZ)) == NULL)
		return (-1);

	strncpy(ctx->scratch->ptr1, bridge_default, ctx->scratch->int1);
	return (0);
}

int
op_begemot_bridge_config(struct snmp_context *ctx, struct snmp_value *val,
    uint sub, uint iidx __unused, enum snmp_op op)
{
	switch (op) {
	    case SNMP_OP_GET:
		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeDefaultBridgeIf:
			return (string_get(val, bridge_get_default_name(), -1));

		    case LEAF_begemotBridgeDataUpdate:
			val->v.integer = bridge_data_maxage;
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeDataPoll:
			val->v.integer = bridge_poll_ticks / 100;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	    case SNMP_OP_GETNEXT:
		abort();

	    case SNMP_OP_SET:
		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeDefaultBridgeIf:
			/*
			 * Cannot use string_save() here - requires either
			 * a fixed-sized or var-length string - not less
			 * than or equal.
			 */
			if (bridge_default_name_save(ctx,
			    bridge_get_default_name()) < 0)
				return (SNMP_ERR_RES_UNAVAIL);

			if (bridge_set_default_name(val->v.octetstring.octets,
			    val->v.octetstring.len) < 0)
				return (SNMP_ERR_BADVALUE);
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeDataUpdate:
			if (val->v.integer < SNMP_BRIDGE_DATA_MAXAGE_MIN ||
			    val->v.integer > SNMP_BRIDGE_DATA_MAXAGE_MAX)
				return (SNMP_ERR_WRONG_VALUE);
			ctx->scratch->int1 = bridge_data_maxage;
			bridge_data_maxage = val->v.integer;
			return (SNMP_ERR_NOERROR);

		    case LEAF_begemotBridgeDataPoll:
			if (val->v.integer < SNMP_BRIDGE_POLL_INTERVAL_MIN ||
			    val->v.integer > SNMP_BRIDGE_POLL_INTERVAL_MAX)
				return (SNMP_ERR_WRONG_VALUE);
			ctx->scratch->int1 = val->v.integer;
			return (SNMP_ERR_NOERROR);
		}
		abort();

	    case SNMP_OP_ROLLBACK:
		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeDefaultBridgeIf:
			bridge_set_default_name(ctx->scratch->ptr1,
			    ctx->scratch->int1);
			free(ctx->scratch->ptr1);
			break;
		    case LEAF_begemotBridgeDataUpdate:
			bridge_data_maxage = ctx->scratch->int1;
			break;
		}
		return (SNMP_ERR_NOERROR);

	    case SNMP_OP_COMMIT:
		switch (val->var.subs[sub - 1]) {
		    case LEAF_begemotBridgeDefaultBridgeIf:
			free(ctx->scratch->ptr1);
			break;
		    case LEAF_begemotBridgeDataPoll:
			bridge_set_poll_ticks(ctx->scratch->int1 * 100);
			break;
		}
		return (SNMP_ERR_NOERROR);
	}

	abort();
}

/*
 * Bridge mib module initialization hook.
 * Returns 0 on success, < 0 on error.
 */
static int
bridge_init(struct lmodule * mod, int argc __unused, char *argv[] __unused)
{
	bridge_module = mod;

	if (bridge_kmod_load() < 0)
		return (-1);

	if (bridge_ioctl_init() < 0)
		return (-1);

	/* Register to get creation messages for bridge interfaces. */
	if (mib_register_newif(bridge_attach_newif, bridge_module)) {
		syslog(LOG_ERR, "Cannot register newif function: %s",
		    strerror(errno));
		return (-1);
	}

	return (0);
}

/*
 * Bridge mib module finalization hook.
 */
static int
bridge_fini(void)
{
	mib_unregister_newif(bridge_module);
	or_unregister(reg_bridge);

	if (bridge_data_timer != NULL) {
		timer_stop(bridge_data_timer);
		bridge_data_timer = NULL;
	}

	if (bridge_tc_timer != NULL) {
		timer_stop(bridge_tc_timer);
		bridge_tc_timer = NULL;
	}

	bridge_ifs_fini();
	bridge_ports_fini();
	bridge_addrs_fini();

	return (0);
}

/*
 * Bridge mib module start operation.
 */
static void
bridge_start(void)
{
	reg_bridge = or_register(&oid_dot1Bridge,
	    "The IETF MIB for Bridges (RFC 4188).", bridge_module);

	bridge_data_timer = timer_start_repeat(bridge_poll_ticks,
	    bridge_poll_ticks, bridge_update_all, NULL, bridge_module);

	bridge_tc_timer = timer_start_repeat(bridge_tc_poll_ticks,
	    bridge_tc_poll_ticks, bridge_update_tc_time, NULL, bridge_module);
}

static void
bridge_dump(void)
{
	struct bridge_if *bif;

	if ((bif = bridge_get_default()) == NULL)
		syslog(LOG_ERR, "Dump: no default bridge interface");
	else
		syslog(LOG_ERR, "Dump: default bridge interface %s",
		     bif->bif_name);

	bridge_ifs_dump();
	bridge_pf_dump();
}

const struct snmp_module config = {
	.comment = "This module implements the bridge mib (RFC 4188).",
	.init =		bridge_init,
	.fini =		bridge_fini,
	.start =	bridge_start,
	.tree =		bridge_ctree,
	.dump =		bridge_dump,
	.tree_size =	bridge_CTREE_SIZE,
};
