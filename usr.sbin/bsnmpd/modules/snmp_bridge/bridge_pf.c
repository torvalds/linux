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
 * Bridge pfil controls.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_mib.h>
#include <net/if_types.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#include <bsnmp/snmpmod.h>
#include <bsnmp/snmp_mibII.h>

#define	SNMPTREE_TYPES
#include "bridge_tree.h"
#include "bridge_snmp.h"

static int
val2snmp_truth(uint8_t val)
{
	if (val == 0)
		return (2);

	return (1);
}

static int
snmp_truth2val(int32_t truth)
{
	if (truth == 2)
		return (0);
	else if (truth == 1)
		return (1);

	return (-1);
}

int
op_begemot_bridge_pf(struct snmp_context *ctx, struct snmp_value *val,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	int k_val;

	if (val->var.subs[sub - 1] > LEAF_begemotBridgeLayer2PfStatus)
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
		case SNMP_OP_GETNEXT:
			abort();
		case SNMP_OP_ROLLBACK:
			bridge_do_pfctl(val->var.subs[sub - 1] - 1,
			    op, &(ctx->scratch->int1));
				return (SNMP_ERR_NOERROR);

		case SNMP_OP_COMMIT:
			return (SNMP_ERR_NOERROR);

		case SNMP_OP_SET:
			ctx->scratch->int1 =
			    bridge_get_pfval(val->var.subs[sub - 1]);

			if ((k_val = snmp_truth2val(val->v.integer)) < 0)
				return (SNMP_ERR_BADVALUE);
			return (SNMP_ERR_NOERROR);

		case SNMP_OP_GET:
			switch (val->var.subs[sub - 1]) {
			    case LEAF_begemotBridgePfilStatus:
			    case LEAF_begemotBridgePfilMembers:
			    case LEAF_begemotBridgePfilIpOnly:
			    case LEAF_begemotBridgeLayer2PfStatus:
				if (bridge_do_pfctl(val->var.subs[sub - 1] - 1,
				    op, &k_val) < 0)
					return (SNMP_ERR_GENERR);
				val->v.integer = val2snmp_truth(k_val);
				return (SNMP_ERR_NOERROR);
			}
			abort();
	}

	abort();
}
