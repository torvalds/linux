/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Philip Paeps <philip@FreeBSD.org>
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
 * $FreeBSD$
 */

#define PFIOC_USE_LATEST

#include <sys/queue.h>
#include <bsnmp/snmpmod.h>

#include <net/pfvar.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define	SNMPTREE_TYPES
#include "pf_oid.h"
#include "pf_tree.h"

struct lmodule *module;

static int dev = -1;
static int started;
static uint64_t pf_tick;

static struct pf_status pfs;

enum { IN, OUT };
enum { IPV4, IPV6 };
enum { PASS, BLOCK };

#define PFI_IFTYPE_GROUP	0
#define PFI_IFTYPE_INSTANCE	1
#define PFI_IFTYPE_DETACHED	2

struct pfi_entry {
	struct pfi_kif	pfi;
	u_int		index;
	TAILQ_ENTRY(pfi_entry) link;
};
TAILQ_HEAD(pfi_table, pfi_entry);

static struct pfi_table pfi_table;
static time_t pfi_table_age;
static int pfi_table_count;

#define PFI_TABLE_MAXAGE	5

struct pft_entry {
	struct pfr_tstats pft;
	u_int		index;
	TAILQ_ENTRY(pft_entry) link;
};
TAILQ_HEAD(pft_table, pft_entry);

static struct pft_table pft_table;
static time_t pft_table_age;
static int pft_table_count;

#define PFT_TABLE_MAXAGE	5

struct pfa_entry {
	struct pfr_astats pfas;
	u_int		index;
	TAILQ_ENTRY(pfa_entry) link;
};
TAILQ_HEAD(pfa_table, pfa_entry);

static struct pfa_table pfa_table;
static time_t pfa_table_age;
static int pfa_table_count;

#define	PFA_TABLE_MAXAGE	5

struct pfq_entry {
	struct pf_altq	altq;
	u_int		index;
	TAILQ_ENTRY(pfq_entry) link;
};
TAILQ_HEAD(pfq_table, pfq_entry);

static struct pfq_table pfq_table;
static time_t pfq_table_age;
static int pfq_table_count;

static int altq_enabled = 0;

#define PFQ_TABLE_MAXAGE	5

struct pfl_entry {
	char		name[MAXPATHLEN + PF_RULE_LABEL_SIZE];
	u_int64_t	evals;
	u_int64_t	bytes[2];
	u_int64_t	pkts[2];
	u_int		index;
	TAILQ_ENTRY(pfl_entry) link;
};
TAILQ_HEAD(pfl_table, pfl_entry);

static struct pfl_table pfl_table;
static time_t pfl_table_age;
static int pfl_table_count;

#define	PFL_TABLE_MAXAGE	5

/* Forward declarations */
static int pfi_refresh(void);
static int pfq_refresh(void);
static int pfs_refresh(void);
static int pft_refresh(void);
static int pfa_refresh(void);
static int pfl_refresh(void);
static struct pfi_entry * pfi_table_find(u_int idx);
static struct pfq_entry * pfq_table_find(u_int idx);
static struct pft_entry * pft_table_find(u_int idx);
static struct pfa_entry * pfa_table_find(u_int idx);
static struct pfl_entry * pfl_table_find(u_int idx);

static int altq_is_enabled(int pfdevice);

int
pf_status(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];
	time_t		runtime;
	unsigned char	str[128];

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		if (pfs_refresh() == -1)
			return (SNMP_ERR_GENERR);

		switch (which) {
			case LEAF_pfStatusRunning:
			    val->v.uint32 = pfs.running;
			    break;
			case LEAF_pfStatusRuntime:
			    runtime = (pfs.since > 0) ?
				time(NULL) - pfs.since : 0;
			    val->v.uint32 = runtime * 100;
			    break;
			case LEAF_pfStatusDebug:
			    val->v.uint32 = pfs.debug;
			    break;
			case LEAF_pfStatusHostId:
			    sprintf(str, "0x%08x", ntohl(pfs.hostid));
			    return (string_get(val, str, strlen(str)));

			default:
			    return (SNMP_ERR_NOSUCHNAME);
		}

		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
pf_counter(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		if (pfs_refresh() == -1)
			return (SNMP_ERR_GENERR);

		switch (which) {
			case LEAF_pfCounterMatch:
				val->v.counter64 = pfs.counters[PFRES_MATCH];
				break;
			case LEAF_pfCounterBadOffset:
				val->v.counter64 = pfs.counters[PFRES_BADOFF];
				break;
			case LEAF_pfCounterFragment:
				val->v.counter64 = pfs.counters[PFRES_FRAG];
				break;
			case LEAF_pfCounterShort:
				val->v.counter64 = pfs.counters[PFRES_SHORT];
				break;
			case LEAF_pfCounterNormalize:
				val->v.counter64 = pfs.counters[PFRES_NORM];
				break;
			case LEAF_pfCounterMemDrop:
				val->v.counter64 = pfs.counters[PFRES_MEMORY];
				break;

			default:
				return (SNMP_ERR_NOSUCHNAME);
		}

		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
pf_statetable(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		if (pfs_refresh() == -1)
			return (SNMP_ERR_GENERR);

		switch (which) {
			case LEAF_pfStateTableCount:
				val->v.uint32 = pfs.states;
				break;
			case LEAF_pfStateTableSearches:
				val->v.counter64 =
				    pfs.fcounters[FCNT_STATE_SEARCH];
				break;
			case LEAF_pfStateTableInserts:
				val->v.counter64 =
				    pfs.fcounters[FCNT_STATE_INSERT];
				break;
			case LEAF_pfStateTableRemovals:
				val->v.counter64 =
				    pfs.fcounters[FCNT_STATE_REMOVALS];
				break;

			default:
				return (SNMP_ERR_NOSUCHNAME);
		}

		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
pf_srcnodes(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		if (pfs_refresh() == -1)
			return (SNMP_ERR_GENERR);

		switch (which) {
			case LEAF_pfSrcNodesCount:
				val->v.uint32 = pfs.src_nodes;
				break;
			case LEAF_pfSrcNodesSearches:
				val->v.counter64 =
				    pfs.scounters[SCNT_SRC_NODE_SEARCH];
				break;
			case LEAF_pfSrcNodesInserts:
				val->v.counter64 =
				    pfs.scounters[SCNT_SRC_NODE_INSERT];
				break;
			case LEAF_pfSrcNodesRemovals:
				val->v.counter64 =
				    pfs.scounters[SCNT_SRC_NODE_REMOVALS];
				break;

			default:
				return (SNMP_ERR_NOSUCHNAME);
		}

		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
pf_limits(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t		which = val->var.subs[sub - 1];
	struct pfioc_limit	pl;

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		bzero(&pl, sizeof(struct pfioc_limit));

		switch (which) {
			case LEAF_pfLimitsStates:
				pl.index = PF_LIMIT_STATES;
				break;
			case LEAF_pfLimitsSrcNodes:
				pl.index = PF_LIMIT_SRC_NODES;
				break;
			case LEAF_pfLimitsFrags:
				pl.index = PF_LIMIT_FRAGS;
				break;

			default:
				return (SNMP_ERR_NOSUCHNAME);
		}

		if (ioctl(dev, DIOCGETLIMIT, &pl)) {
			syslog(LOG_ERR, "pf_limits(): ioctl(): %s",
			    strerror(errno));
			return (SNMP_ERR_GENERR);
		}

		val->v.uint32 = pl.limit;

		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
pf_timeouts(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];
	struct pfioc_tm	pt;

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		bzero(&pt, sizeof(struct pfioc_tm));

		switch (which) {
			case LEAF_pfTimeoutsTcpFirst:
				pt.timeout = PFTM_TCP_FIRST_PACKET;
				break;
			case LEAF_pfTimeoutsTcpOpening:
				pt.timeout = PFTM_TCP_OPENING;
				break;
			case LEAF_pfTimeoutsTcpEstablished:
				pt.timeout = PFTM_TCP_ESTABLISHED;
				break;
			case LEAF_pfTimeoutsTcpClosing:
				pt.timeout = PFTM_TCP_CLOSING;
				break;
			case LEAF_pfTimeoutsTcpFinWait:
				pt.timeout = PFTM_TCP_FIN_WAIT;
				break;
			case LEAF_pfTimeoutsTcpClosed:
				pt.timeout = PFTM_TCP_CLOSED;
				break;
			case LEAF_pfTimeoutsUdpFirst:
				pt.timeout = PFTM_UDP_FIRST_PACKET;
				break;
			case LEAF_pfTimeoutsUdpSingle:
				pt.timeout = PFTM_UDP_SINGLE;
				break;
			case LEAF_pfTimeoutsUdpMultiple:
				pt.timeout = PFTM_UDP_MULTIPLE;
				break;
			case LEAF_pfTimeoutsIcmpFirst:
				pt.timeout = PFTM_ICMP_FIRST_PACKET;
				break;
			case LEAF_pfTimeoutsIcmpError:
				pt.timeout = PFTM_ICMP_ERROR_REPLY;
				break;
			case LEAF_pfTimeoutsOtherFirst:
				pt.timeout = PFTM_OTHER_FIRST_PACKET;
				break;
			case LEAF_pfTimeoutsOtherSingle:
				pt.timeout = PFTM_OTHER_SINGLE;
				break;
			case LEAF_pfTimeoutsOtherMultiple:
				pt.timeout = PFTM_OTHER_MULTIPLE;
				break;
			case LEAF_pfTimeoutsFragment:
				pt.timeout = PFTM_FRAG;
				break;
			case LEAF_pfTimeoutsInterval:
				pt.timeout = PFTM_INTERVAL;
				break;
			case LEAF_pfTimeoutsAdaptiveStart:
				pt.timeout = PFTM_ADAPTIVE_START;
				break;
			case LEAF_pfTimeoutsAdaptiveEnd:
				pt.timeout = PFTM_ADAPTIVE_END;
				break;
			case LEAF_pfTimeoutsSrcNode:
				pt.timeout = PFTM_SRC_NODE;
				break;

			default:
				return (SNMP_ERR_NOSUCHNAME);
		}

		if (ioctl(dev, DIOCGETTIMEOUT, &pt)) {
			syslog(LOG_ERR, "pf_timeouts(): ioctl(): %s",
			    strerror(errno));
			return (SNMP_ERR_GENERR);
		}

		val->v.integer = pt.seconds;

		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
pf_logif(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];
	unsigned char	str[IFNAMSIZ];

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		if (pfs_refresh() == -1)
			return (SNMP_ERR_GENERR);

		switch (which) {
	 		case LEAF_pfLogInterfaceName:
				strlcpy(str, pfs.ifname, sizeof str);
				return (string_get(val, str, strlen(str)));
			case LEAF_pfLogInterfaceIp4BytesIn:
				val->v.counter64 = pfs.bcounters[IPV4][IN];
				break;
			case LEAF_pfLogInterfaceIp4BytesOut:
				val->v.counter64 = pfs.bcounters[IPV4][OUT];
				break;
			case LEAF_pfLogInterfaceIp4PktsInPass:
				val->v.counter64 =
				    pfs.pcounters[IPV4][IN][PF_PASS];
				break;
			case LEAF_pfLogInterfaceIp4PktsInDrop:
				val->v.counter64 =
				    pfs.pcounters[IPV4][IN][PF_DROP];
				break;
			case LEAF_pfLogInterfaceIp4PktsOutPass:
				val->v.counter64 =
				    pfs.pcounters[IPV4][OUT][PF_PASS];
				break;
			case LEAF_pfLogInterfaceIp4PktsOutDrop:
				val->v.counter64 =
				    pfs.pcounters[IPV4][OUT][PF_DROP];
				break;
			case LEAF_pfLogInterfaceIp6BytesIn:
				val->v.counter64 = pfs.bcounters[IPV6][IN];
				break;
			case LEAF_pfLogInterfaceIp6BytesOut:
				val->v.counter64 = pfs.bcounters[IPV6][OUT];
				break;
			case LEAF_pfLogInterfaceIp6PktsInPass:
				val->v.counter64 =
				    pfs.pcounters[IPV6][IN][PF_PASS];
				break;
			case LEAF_pfLogInterfaceIp6PktsInDrop:
				val->v.counter64 =
				    pfs.pcounters[IPV6][IN][PF_DROP];
				break;
			case LEAF_pfLogInterfaceIp6PktsOutPass:
				val->v.counter64 =
				    pfs.pcounters[IPV6][OUT][PF_PASS];
				break;
			case LEAF_pfLogInterfaceIp6PktsOutDrop:
				val->v.counter64 =
				    pfs.pcounters[IPV6][OUT][PF_DROP];
				break;

			default:
				return (SNMP_ERR_NOSUCHNAME);
		}

		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
pf_interfaces(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		if ((time(NULL) - pfi_table_age) > PFI_TABLE_MAXAGE)
			if (pfi_refresh() == -1)
			    return (SNMP_ERR_GENERR);

		switch (which) {
			case LEAF_pfInterfacesIfNumber:
				val->v.uint32 = pfi_table_count;
				break;

			default:
				return (SNMP_ERR_NOSUCHNAME);
		}

		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
pf_iftable(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];
	struct pfi_entry *e = NULL;

	if ((time(NULL) - pfi_table_age) > PFI_TABLE_MAXAGE)
		pfi_refresh();

	switch (op) {
		case SNMP_OP_SET:
			return (SNMP_ERR_NOT_WRITEABLE);
		case SNMP_OP_GETNEXT:
			if ((e = NEXT_OBJECT_INT(&pfi_table,
			    &val->var, sub)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			val->var.len = sub + 1;
			val->var.subs[sub] = e->index;
			break;
		case SNMP_OP_GET:
			if (val->var.len - sub != 1)
				return (SNMP_ERR_NOSUCHNAME);
			if ((e = pfi_table_find(val->var.subs[sub])) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			break;

		case SNMP_OP_COMMIT:
		case SNMP_OP_ROLLBACK:
		default:
			abort();
	}

	switch (which) {
		case LEAF_pfInterfacesIfDescr:
			return (string_get(val, e->pfi.pfik_name, -1));
		case LEAF_pfInterfacesIfType:
			val->v.integer = PFI_IFTYPE_INSTANCE;
			break;
		case LEAF_pfInterfacesIfTZero:
			val->v.uint32 =
			    (time(NULL) - e->pfi.pfik_tzero) * 100;
			break;
		case LEAF_pfInterfacesIfRefsRule:
			val->v.uint32 = e->pfi.pfik_rulerefs;
			break;
		case LEAF_pfInterfacesIf4BytesInPass:
			val->v.counter64 =
			    e->pfi.pfik_bytes[IPV4][IN][PASS];
			break;
		case LEAF_pfInterfacesIf4BytesInBlock:
			val->v.counter64 =
			    e->pfi.pfik_bytes[IPV4][IN][BLOCK];
			break;
		case LEAF_pfInterfacesIf4BytesOutPass:
			val->v.counter64 =
			    e->pfi.pfik_bytes[IPV4][OUT][PASS];
			break;
		case LEAF_pfInterfacesIf4BytesOutBlock:
			val->v.counter64 =
			    e->pfi.pfik_bytes[IPV4][OUT][BLOCK];
			break;
		case LEAF_pfInterfacesIf4PktsInPass:
			val->v.counter64 =
			    e->pfi.pfik_packets[IPV4][IN][PASS];
			break;
		case LEAF_pfInterfacesIf4PktsInBlock:
			val->v.counter64 =
			    e->pfi.pfik_packets[IPV4][IN][BLOCK];
			break;
		case LEAF_pfInterfacesIf4PktsOutPass:
			val->v.counter64 =
			    e->pfi.pfik_packets[IPV4][OUT][PASS];
			break;
		case LEAF_pfInterfacesIf4PktsOutBlock:
			val->v.counter64 =
			    e->pfi.pfik_packets[IPV4][OUT][BLOCK];
			break;
		case LEAF_pfInterfacesIf6BytesInPass:
			val->v.counter64 =
			    e->pfi.pfik_bytes[IPV6][IN][PASS];
			break;
		case LEAF_pfInterfacesIf6BytesInBlock:
			val->v.counter64 =
			    e->pfi.pfik_bytes[IPV6][IN][BLOCK];
			break;
		case LEAF_pfInterfacesIf6BytesOutPass:
			val->v.counter64 =
			    e->pfi.pfik_bytes[IPV6][OUT][PASS];
			break;
		case LEAF_pfInterfacesIf6BytesOutBlock:
			val->v.counter64 =
			    e->pfi.pfik_bytes[IPV6][OUT][BLOCK];
			break;
		case LEAF_pfInterfacesIf6PktsInPass:
			val->v.counter64 =
			    e->pfi.pfik_packets[IPV6][IN][PASS];
			break;
		case LEAF_pfInterfacesIf6PktsInBlock:
			val->v.counter64 =
			    e->pfi.pfik_packets[IPV6][IN][BLOCK];
			break;
		case LEAF_pfInterfacesIf6PktsOutPass:
			val->v.counter64 =
			    e->pfi.pfik_packets[IPV6][OUT][PASS];
			break;
		case LEAF_pfInterfacesIf6PktsOutBlock:
			val->v.counter64 =
			    e->pfi.pfik_packets[IPV6][OUT][BLOCK];
			break;

		default:
			return (SNMP_ERR_NOSUCHNAME);
	}

	return (SNMP_ERR_NOERROR);
}

int
pf_tables(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		if ((time(NULL) - pft_table_age) > PFT_TABLE_MAXAGE)
			if (pft_refresh() == -1)
			    return (SNMP_ERR_GENERR);

		switch (which) {
			case LEAF_pfTablesTblNumber:
				val->v.uint32 = pft_table_count;
				break;

			default:
				return (SNMP_ERR_NOSUCHNAME);
		}

		return (SNMP_ERR_NOERROR);
	}

	abort();
}

int
pf_tbltable(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];
	struct pft_entry *e = NULL;

	if ((time(NULL) - pft_table_age) > PFT_TABLE_MAXAGE)
		pft_refresh();

	switch (op) {
		case SNMP_OP_SET:
			return (SNMP_ERR_NOT_WRITEABLE);
		case SNMP_OP_GETNEXT:
			if ((e = NEXT_OBJECT_INT(&pft_table,
			    &val->var, sub)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			val->var.len = sub + 1;
			val->var.subs[sub] = e->index;
			break;
		case SNMP_OP_GET:
			if (val->var.len - sub != 1)
				return (SNMP_ERR_NOSUCHNAME);
			if ((e = pft_table_find(val->var.subs[sub])) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			break;

		case SNMP_OP_COMMIT:
		case SNMP_OP_ROLLBACK:
		default:
			abort();
	}

	switch (which) {
		case LEAF_pfTablesTblDescr:
			return (string_get(val, e->pft.pfrts_name, -1));
		case LEAF_pfTablesTblCount:
			val->v.integer = e->pft.pfrts_cnt;
			break;
		case LEAF_pfTablesTblTZero:
			val->v.uint32 =
			    (time(NULL) - e->pft.pfrts_tzero) * 100;
			break;
		case LEAF_pfTablesTblRefsAnchor:
			val->v.integer =
			    e->pft.pfrts_refcnt[PFR_REFCNT_ANCHOR];
			break;
		case LEAF_pfTablesTblRefsRule:
			val->v.integer =
			    e->pft.pfrts_refcnt[PFR_REFCNT_RULE];
			break;
		case LEAF_pfTablesTblEvalMatch:
			val->v.counter64 = e->pft.pfrts_match;
			break;
		case LEAF_pfTablesTblEvalNoMatch:
			val->v.counter64 = e->pft.pfrts_nomatch;
			break;
		case LEAF_pfTablesTblBytesInPass:
			val->v.counter64 =
			    e->pft.pfrts_bytes[PFR_DIR_IN][PFR_OP_PASS];
			break;
		case LEAF_pfTablesTblBytesInBlock:
			val->v.counter64 =
			    e->pft.pfrts_bytes[PFR_DIR_IN][PFR_OP_BLOCK];
			break;
		case LEAF_pfTablesTblBytesInXPass:
			val->v.counter64 =
			    e->pft.pfrts_bytes[PFR_DIR_IN][PFR_OP_XPASS];
			break;
		case LEAF_pfTablesTblBytesOutPass:
			val->v.counter64 =
			    e->pft.pfrts_bytes[PFR_DIR_OUT][PFR_OP_PASS];
			break;
		case LEAF_pfTablesTblBytesOutBlock:
			val->v.counter64 =
			    e->pft.pfrts_bytes[PFR_DIR_OUT][PFR_OP_BLOCK];
			break;
		case LEAF_pfTablesTblBytesOutXPass:
			val->v.counter64 =
			    e->pft.pfrts_bytes[PFR_DIR_OUT][PFR_OP_XPASS];
			break;
		case LEAF_pfTablesTblPktsInPass:
			val->v.counter64 =
			    e->pft.pfrts_packets[PFR_DIR_IN][PFR_OP_PASS];
			break;
		case LEAF_pfTablesTblPktsInBlock:
			val->v.counter64 =
			    e->pft.pfrts_packets[PFR_DIR_IN][PFR_OP_BLOCK];
			break;
		case LEAF_pfTablesTblPktsInXPass:
			val->v.counter64 =
			    e->pft.pfrts_packets[PFR_DIR_IN][PFR_OP_XPASS];
			break;
		case LEAF_pfTablesTblPktsOutPass:
			val->v.counter64 =
			    e->pft.pfrts_packets[PFR_DIR_OUT][PFR_OP_PASS];
			break;
		case LEAF_pfTablesTblPktsOutBlock:
			val->v.counter64 =
			    e->pft.pfrts_packets[PFR_DIR_OUT][PFR_OP_BLOCK];
			break;
		case LEAF_pfTablesTblPktsOutXPass:
			val->v.counter64 =
			    e->pft.pfrts_packets[PFR_DIR_OUT][PFR_OP_XPASS];
			break;

		default:
			return (SNMP_ERR_NOSUCHNAME);
	}

	return (SNMP_ERR_NOERROR);
}

int
pf_tbladdr(struct snmp_context __unused *ctx, struct snmp_value __unused *val,
	u_int __unused sub, u_int __unused vindex, enum snmp_op __unused op)
{
	asn_subid_t	which = val->var.subs[sub - 1];
	struct pfa_entry *e = NULL;

	if ((time(NULL) - pfa_table_age) > PFA_TABLE_MAXAGE)
		pfa_refresh();

	switch (op) {
		case SNMP_OP_SET:
			return (SNMP_ERR_NOT_WRITEABLE);
		case SNMP_OP_GETNEXT:
			if ((e = NEXT_OBJECT_INT(&pfa_table,
			    &val->var, sub)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			val->var.len = sub + 1;
			val->var.subs[sub] = e->index;
			break;
		case SNMP_OP_GET:
			if (val->var.len - sub != 1)
				return (SNMP_ERR_NOSUCHNAME);
			if ((e = pfa_table_find(val->var.subs[sub])) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			break;

		case SNMP_OP_COMMIT:
		case SNMP_OP_ROLLBACK:
		default:
			abort();
	}

	switch (which) {
		case LEAF_pfTablesAddrNetType:
			if (e->pfas.pfras_a.pfra_af == AF_INET)
				val->v.integer = pfTablesAddrNetType_ipv4;
			else if (e->pfas.pfras_a.pfra_af == AF_INET6)
				val->v.integer = pfTablesAddrNetType_ipv6;
			else
				return (SNMP_ERR_GENERR);
			break;
		case LEAF_pfTablesAddrNet:
			if (e->pfas.pfras_a.pfra_af == AF_INET) {
				return (string_get(val,
				    (u_char *)&e->pfas.pfras_a.pfra_ip4addr, 4));
			} else if (e->pfas.pfras_a.pfra_af == AF_INET6)
				return (string_get(val,
				    (u_char *)&e->pfas.pfras_a.pfra_ip6addr, 16));
			else
				return (SNMP_ERR_GENERR);
			break;
		case LEAF_pfTablesAddrPrefix:
			val->v.integer = (int32_t) e->pfas.pfras_a.pfra_net;
			break;
		case LEAF_pfTablesAddrTZero:
			val->v.uint32 =
			    (time(NULL) - e->pfas.pfras_tzero) * 100;
			break;
		case LEAF_pfTablesAddrBytesInPass:
			val->v.counter64 =
			    e->pfas.pfras_bytes[PFR_DIR_IN][PFR_OP_PASS];
			break;
		case LEAF_pfTablesAddrBytesInBlock:
			val->v.counter64 =
			    e->pfas.pfras_bytes[PFR_DIR_IN][PFR_OP_BLOCK];
			break;
		case LEAF_pfTablesAddrBytesOutPass:
			val->v.counter64 =
			    e->pfas.pfras_bytes[PFR_DIR_OUT][PFR_OP_PASS];
			break;
		case LEAF_pfTablesAddrBytesOutBlock:
			val->v.counter64 =
			    e->pfas.pfras_bytes[PFR_DIR_OUT][PFR_OP_BLOCK];
			break;
		case LEAF_pfTablesAddrPktsInPass:
			val->v.counter64 =
			    e->pfas.pfras_packets[PFR_DIR_IN][PFR_OP_PASS];
			break;
		case LEAF_pfTablesAddrPktsInBlock:
			val->v.counter64 =
			    e->pfas.pfras_packets[PFR_DIR_IN][PFR_OP_BLOCK];
			break;
		case LEAF_pfTablesAddrPktsOutPass:
			val->v.counter64 =
			    e->pfas.pfras_packets[PFR_DIR_OUT][PFR_OP_PASS];
			break;
		case LEAF_pfTablesAddrPktsOutBlock:
			val->v.counter64 =
			    e->pfas.pfras_packets[PFR_DIR_OUT][PFR_OP_BLOCK];
			break;
		default:
			return (SNMP_ERR_NOSUCHNAME);
	}

	return (SNMP_ERR_NOERROR);
}

int
pf_altq(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];

	if (!altq_enabled)
	   return (SNMP_ERR_NOSUCHNAME);

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		if ((time(NULL) - pfq_table_age) > PFQ_TABLE_MAXAGE)
			if (pfq_refresh() == -1)
			    return (SNMP_ERR_GENERR);

		switch (which) {
			case LEAF_pfAltqQueueNumber:
				val->v.uint32 = pfq_table_count;
				break;

			default:
				return (SNMP_ERR_NOSUCHNAME);
		}

		return (SNMP_ERR_NOERROR);
	}

	abort();
	return (SNMP_ERR_GENERR);
}

int
pf_altqq(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];
	struct pfq_entry *e = NULL;

	if (!altq_enabled)
	   return (SNMP_ERR_NOSUCHNAME);

	if ((time(NULL) - pfq_table_age) > PFQ_TABLE_MAXAGE)
		pfq_refresh();

	switch (op) {
		case SNMP_OP_SET:
			return (SNMP_ERR_NOT_WRITEABLE);
		case SNMP_OP_GETNEXT:
			if ((e = NEXT_OBJECT_INT(&pfq_table,
			    &val->var, sub)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			val->var.len = sub + 1;
			val->var.subs[sub] = e->index;
			break;
		case SNMP_OP_GET:
			if (val->var.len - sub != 1)
				return (SNMP_ERR_NOSUCHNAME);
			if ((e = pfq_table_find(val->var.subs[sub])) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			break;

		case SNMP_OP_COMMIT:
		case SNMP_OP_ROLLBACK:
		default:
			abort();
	}

	switch (which) {
		case LEAF_pfAltqQueueDescr:
			return (string_get(val, e->altq.qname, -1));
		case LEAF_pfAltqQueueParent:
			return (string_get(val, e->altq.parent, -1));
		case LEAF_pfAltqQueueScheduler:
			val->v.integer = e->altq.scheduler;
			break;
		case LEAF_pfAltqQueueBandwidth:
			val->v.uint32 = (e->altq.bandwidth > UINT_MAX) ?
			    UINT_MAX : (u_int32_t)e->altq.bandwidth;
			break;
		case LEAF_pfAltqQueuePriority:
			val->v.integer = e->altq.priority;
			break;
		case LEAF_pfAltqQueueLimit:
			val->v.integer = e->altq.qlimit;
			break;

		default:
			return (SNMP_ERR_NOSUCHNAME);
	}

	return (SNMP_ERR_NOERROR);
}

int
pf_labels(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];

	if (op == SNMP_OP_SET)
		return (SNMP_ERR_NOT_WRITEABLE);

	if (op == SNMP_OP_GET) {
		if ((time(NULL) - pfl_table_age) > PFL_TABLE_MAXAGE)
			if (pfl_refresh() == -1)
				return (SNMP_ERR_GENERR);

		switch (which) {
			case LEAF_pfLabelsLblNumber:
				val->v.uint32 = pfl_table_count;
				break;

			default:
				return (SNMP_ERR_NOSUCHNAME);
		}

		return (SNMP_ERR_NOERROR);
	}

	abort();
	return (SNMP_ERR_GENERR);
}

int
pf_lbltable(struct snmp_context __unused *ctx, struct snmp_value *val,
	u_int sub, u_int __unused vindex, enum snmp_op op)
{
	asn_subid_t	which = val->var.subs[sub - 1];
	struct pfl_entry *e = NULL;

	if ((time(NULL) - pfl_table_age) > PFL_TABLE_MAXAGE)
		pfl_refresh();

	switch (op) {
		case SNMP_OP_SET:
			return (SNMP_ERR_NOT_WRITEABLE);
		case SNMP_OP_GETNEXT:
			if ((e = NEXT_OBJECT_INT(&pfl_table,
			    &val->var, sub)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			val->var.len = sub + 1;
			val->var.subs[sub] = e->index;
			break;
		case SNMP_OP_GET:
			if (val->var.len - sub != 1)
				return (SNMP_ERR_NOSUCHNAME);
			if ((e = pfl_table_find(val->var.subs[sub])) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			break;

		case SNMP_OP_COMMIT:
		case SNMP_OP_ROLLBACK:
		default:
			abort();
	}

	switch (which) {
		case LEAF_pfLabelsLblName:
			return (string_get(val, e->name, -1));
		case LEAF_pfLabelsLblEvals:
			val->v.counter64 = e->evals;
			break;
		case LEAF_pfLabelsLblBytesIn:
			val->v.counter64 = e->bytes[IN];
			break;
		case LEAF_pfLabelsLblBytesOut:
			val->v.counter64 = e->bytes[OUT];
			break;
		case LEAF_pfLabelsLblPktsIn:
			val->v.counter64 = e->pkts[IN];
			break;
		case LEAF_pfLabelsLblPktsOut:
			val->v.counter64 = e->pkts[OUT];
			break;
		default:
			return (SNMP_ERR_NOSUCHNAME);
	}

	return (SNMP_ERR_NOERROR);
}

static struct pfi_entry *
pfi_table_find(u_int idx)
{
	struct pfi_entry *e;

	TAILQ_FOREACH(e, &pfi_table, link)
		if (e->index == idx)
			return (e);
	return (NULL);
}

static struct pfq_entry *
pfq_table_find(u_int idx)
{
	struct pfq_entry *e;

	TAILQ_FOREACH(e, &pfq_table, link)
		if (e->index == idx)
			return (e);
	return (NULL);
}

static struct pft_entry *
pft_table_find(u_int idx)
{
	struct pft_entry *e;

	TAILQ_FOREACH(e, &pft_table, link)
		if (e->index == idx)
			return (e);
	return (NULL);
}

static struct pfa_entry *
pfa_table_find(u_int idx)
{
	struct pfa_entry *e;

	TAILQ_FOREACH(e, &pfa_table, link)
		if (e->index == idx)
			return (e);
	return (NULL);
}

static struct pfl_entry *
pfl_table_find(u_int idx)
{
	struct pfl_entry *e;

	TAILQ_FOREACH(e, &pfl_table, link)
		if (e->index == idx)
			return (e);

	return (NULL);
}

static int
pfi_refresh(void)
{
	struct pfioc_iface io;
	struct pfi_kif *p = NULL;
	struct pfi_entry *e;
	int i, numifs = 1;

	if (started && this_tick <= pf_tick)
		return (0);

	while (!TAILQ_EMPTY(&pfi_table)) {
		e = TAILQ_FIRST(&pfi_table);
		TAILQ_REMOVE(&pfi_table, e, link);
		free(e);
	}

	bzero(&io, sizeof(io));
	io.pfiio_esize = sizeof(struct pfi_kif);

	for (;;) {
		p = reallocf(p, numifs * sizeof(struct pfi_kif));
		if (p == NULL) {
			syslog(LOG_ERR, "pfi_refresh(): reallocf() numifs=%d: %s",
			    numifs, strerror(errno));
			goto err2;
		}
		io.pfiio_size = numifs;
		io.pfiio_buffer = p;

		if (ioctl(dev, DIOCIGETIFACES, &io)) {
			syslog(LOG_ERR, "pfi_refresh(): ioctl(): %s",
			    strerror(errno));
			goto err2;
		}

		if (numifs >= io.pfiio_size)
			break;

		numifs = io.pfiio_size;
	}

	for (i = 0; i < numifs; i++) {
		e = malloc(sizeof(struct pfi_entry));
		if (e == NULL)
			goto err1;
		e->index = i + 1;
		memcpy(&e->pfi, p+i, sizeof(struct pfi_kif));
		TAILQ_INSERT_TAIL(&pfi_table, e, link);
	}

	pfi_table_age = time(NULL);
	pfi_table_count = numifs;
	pf_tick = this_tick;

	free(p);
	return (0);

err1:
	while (!TAILQ_EMPTY(&pfi_table)) {
		e = TAILQ_FIRST(&pfi_table);
		TAILQ_REMOVE(&pfi_table, e, link);
		free(e);
	}
err2:
	free(p);
	return(-1);
}

static int
pfq_refresh(void)
{
	struct pfioc_altq pa;
	struct pfq_entry *e;
	int i, numqs, ticket;

	if (started && this_tick <= pf_tick)
		return (0);

	while (!TAILQ_EMPTY(&pfq_table)) {
		e = TAILQ_FIRST(&pfq_table);
		TAILQ_REMOVE(&pfq_table, e, link);
		free(e);
	}

	bzero(&pa, sizeof(pa));
	pa.version = PFIOC_ALTQ_VERSION;
	if (ioctl(dev, DIOCGETALTQS, &pa)) {
		syslog(LOG_ERR, "pfq_refresh: ioctl(DIOCGETALTQS): %s",
		    strerror(errno));
		return (-1);
	}

	numqs = pa.nr;
	ticket = pa.ticket;

	for (i = 0; i < numqs; i++) {
		e = malloc(sizeof(struct pfq_entry));
		if (e == NULL) {
			syslog(LOG_ERR, "pfq_refresh(): "
			    "malloc(): %s",
			    strerror(errno));
			goto err;
		}
		pa.ticket = ticket;
		pa.nr = i;

		if (ioctl(dev, DIOCGETALTQ, &pa)) {
			syslog(LOG_ERR, "pfq_refresh(): "
			    "ioctl(DIOCGETALTQ): %s",
			    strerror(errno));
			goto err;
		}

		if (pa.altq.qid > 0) {
			memcpy(&e->altq, &pa.altq, sizeof(struct pf_altq));
			e->index = pa.altq.qid;
			pfq_table_count = i;
			INSERT_OBJECT_INT_LINK_INDEX(e, &pfq_table, link, index);
		}
	}

	pfq_table_age = time(NULL);
	pf_tick = this_tick;

	return (0);
err:
	free(e);
	while (!TAILQ_EMPTY(&pfq_table)) {
		e = TAILQ_FIRST(&pfq_table);
		TAILQ_REMOVE(&pfq_table, e, link);
		free(e);
	}
	return(-1);
}

static int
pfs_refresh(void)
{
	if (started && this_tick <= pf_tick)
		return (0);

	bzero(&pfs, sizeof(struct pf_status));

	if (ioctl(dev, DIOCGETSTATUS, &pfs)) {
		syslog(LOG_ERR, "pfs_refresh(): ioctl(): %s",
		    strerror(errno));
		return (-1);
	}

	pf_tick = this_tick;
	return (0);
}

static int
pft_refresh(void)
{
	struct pfioc_table io;
	struct pfr_tstats *t = NULL;
	struct pft_entry *e;
	int i, numtbls = 1;

	if (started && this_tick <= pf_tick)
		return (0);

	while (!TAILQ_EMPTY(&pft_table)) {
		e = TAILQ_FIRST(&pft_table);
		TAILQ_REMOVE(&pft_table, e, link);
		free(e);
	}

	bzero(&io, sizeof(io));
	io.pfrio_esize = sizeof(struct pfr_tstats);

	for (;;) {
		t = reallocf(t, numtbls * sizeof(struct pfr_tstats));
		if (t == NULL) {
			syslog(LOG_ERR, "pft_refresh(): reallocf() numtbls=%d: %s",
			    numtbls, strerror(errno));
			goto err2;
		}
		io.pfrio_size = numtbls;
		io.pfrio_buffer = t;

		if (ioctl(dev, DIOCRGETTSTATS, &io)) {
			syslog(LOG_ERR, "pft_refresh(): ioctl(): %s",
			    strerror(errno));
			goto err2;
		}

		if (numtbls >= io.pfrio_size)
			break;

		numtbls = io.pfrio_size;
	}

	for (i = 0; i < numtbls; i++) {
		e = malloc(sizeof(struct pft_entry));
		if (e == NULL)
			goto err1;
		e->index = i + 1;
		memcpy(&e->pft, t+i, sizeof(struct pfr_tstats));
		TAILQ_INSERT_TAIL(&pft_table, e, link);
	}

	pft_table_age = time(NULL);
	pft_table_count = numtbls;
	pf_tick = this_tick;

	free(t);
	return (0);
err1:
	while (!TAILQ_EMPTY(&pft_table)) {
		e = TAILQ_FIRST(&pft_table);
		TAILQ_REMOVE(&pft_table, e, link);
		free(e);
	}
err2:
	free(t);
	return(-1);
}

static int
pfa_table_addrs(u_int sidx, struct pfr_table *pt)
{
	struct pfioc_table io;
	struct pfr_astats *t = NULL;
	struct pfa_entry *e;
	int i, numaddrs = 1;

	if (pt == NULL)
		return (-1);

	memset(&io, 0, sizeof(io));
	strlcpy(io.pfrio_table.pfrt_name, pt->pfrt_name,
	    sizeof(io.pfrio_table.pfrt_name));

	for (;;) {
		t = reallocf(t, numaddrs * sizeof(struct pfr_astats));
		if (t == NULL) {
			syslog(LOG_ERR, "pfa_table_addrs(): reallocf(): %s",
			    strerror(errno));
			numaddrs = -1;
			goto error;
		}

		memset(t, 0, sizeof(*t));
		io.pfrio_size = numaddrs;
		io.pfrio_buffer = t;
		io.pfrio_esize = sizeof(struct pfr_astats);

		if (ioctl(dev, DIOCRGETASTATS, &io)) {
			syslog(LOG_ERR, "pfa_table_addrs(): ioctl() on %s: %s",
			    pt->pfrt_name, strerror(errno));
			numaddrs = -1;
			break;
		}

		if (numaddrs >= io.pfrio_size)
			break;

		numaddrs = io.pfrio_size;
	}

	for (i = 0; i < numaddrs; i++) {
		if ((t + i)->pfras_a.pfra_af != AF_INET &&
		    (t + i)->pfras_a.pfra_af != AF_INET6) {
			numaddrs = i;
			break;
		}

		e = (struct pfa_entry *)malloc(sizeof(struct pfa_entry));
		if (e == NULL) {
			syslog(LOG_ERR, "pfa_table_addrs(): malloc(): %s",
			    strerror(errno));
			numaddrs = -1;
			break;
		}
		e->index = sidx + i;
		memcpy(&e->pfas, t + i, sizeof(struct pfr_astats));
		TAILQ_INSERT_TAIL(&pfa_table, e, link);
	}

	free(t);
error:
	return (numaddrs);
}

static int
pfa_refresh(void)
{
	struct pfioc_table io;
	struct pfr_table *pt = NULL, *it = NULL;
	struct pfa_entry *e;
	int i, numtbls = 1, cidx, naddrs;

	if (started && this_tick <= pf_tick)
		return (0);

	while (!TAILQ_EMPTY(&pfa_table)) {
		e = TAILQ_FIRST(&pfa_table);
		TAILQ_REMOVE(&pfa_table, e, link);
		free(e);
	}

	memset(&io, 0, sizeof(io));
	io.pfrio_esize = sizeof(struct pfr_table);

	for (;;) {
		pt = reallocf(pt, numtbls * sizeof(struct pfr_table));
		if (pt == NULL) {
			syslog(LOG_ERR, "pfa_refresh(): reallocf() %s",
			    strerror(errno));
			return (-1);
		}
		memset(pt, 0, sizeof(*pt));
		io.pfrio_size = numtbls;
		io.pfrio_buffer = pt;

		if (ioctl(dev, DIOCRGETTABLES, &io)) {
			syslog(LOG_ERR, "pfa_refresh(): ioctl(): %s",
			    strerror(errno));
			goto err2;
		}

		if (numtbls >= io.pfrio_size)
			break;

		numtbls = io.pfrio_size;
	}

	cidx = 1;

	for (it = pt, i = 0; i < numtbls; it++, i++) {
		/*
		 * Skip the table if not active - ioctl(DIOCRGETASTATS) will
		 * return ESRCH for this entry anyway.
		 */
		if (!(it->pfrt_flags & PFR_TFLAG_ACTIVE))
			continue;

		if ((naddrs = pfa_table_addrs(cidx, it)) < 0)
			goto err1;

		cidx += naddrs;
	}

	pfa_table_age = time(NULL);
	pfa_table_count = cidx;
	pf_tick = this_tick;

	free(pt);
	return (0);
err1:
	while (!TAILQ_EMPTY(&pfa_table)) {
		e = TAILQ_FIRST(&pfa_table);
		TAILQ_REMOVE(&pfa_table, e, link);
		free(e);
	}

err2:
	free(pt);
	return (-1);
}

static int
pfl_scan_ruleset(const char *path)
{
	struct pfioc_rule pr;
	struct pfl_entry *e;
	u_int32_t nr, i;

	bzero(&pr, sizeof(pr));
	strlcpy(pr.anchor, path, sizeof(pr.anchor));
	pr.rule.action = PF_PASS;
	if (ioctl(dev, DIOCGETRULES, &pr)) {
		syslog(LOG_ERR, "pfl_scan_ruleset: ioctl(DIOCGETRULES): %s",
		    strerror(errno));
		goto err;
	}

	for (nr = pr.nr, i = 0; i < nr; i++) {
		pr.nr = i;
		if (ioctl(dev, DIOCGETRULE, &pr)) {
			syslog(LOG_ERR, "pfl_scan_ruleset: ioctl(DIOCGETRULE):"
			    " %s", strerror(errno));
			goto err;
		}

		if (pr.rule.label[0]) {
			e = (struct pfl_entry *)malloc(sizeof(*e));
			if (e == NULL)
				goto err;

			strlcpy(e->name, path, sizeof(e->name));
			if (path[0])
				strlcat(e->name, "/", sizeof(e->name));
			strlcat(e->name, pr.rule.label, sizeof(e->name));

			e->evals = pr.rule.evaluations;
			e->bytes[IN] = pr.rule.bytes[IN];
			e->bytes[OUT] = pr.rule.bytes[OUT];
			e->pkts[IN] = pr.rule.packets[IN];
			e->pkts[OUT] = pr.rule.packets[OUT];
			e->index = ++pfl_table_count;

			TAILQ_INSERT_TAIL(&pfl_table, e, link);
		}
	}

	return (0);

err:
	return (-1);
}

static int
pfl_walk_rulesets(const char *path)
{
	struct pfioc_ruleset prs;
	char newpath[MAXPATHLEN];
	u_int32_t nr, i;

	if (pfl_scan_ruleset(path))
		goto err;

	bzero(&prs, sizeof(prs));
	strlcpy(prs.path, path, sizeof(prs.path));
	if (ioctl(dev, DIOCGETRULESETS, &prs)) {
		syslog(LOG_ERR, "pfl_walk_rulesets: ioctl(DIOCGETRULESETS): %s",
		    strerror(errno));
		goto err;
	}

	for (nr = prs.nr, i = 0; i < nr; i++) {
		prs.nr = i;
		if (ioctl(dev, DIOCGETRULESET, &prs)) {
			syslog(LOG_ERR, "pfl_walk_rulesets: ioctl(DIOCGETRULESET):"
			    " %s", strerror(errno));
			goto err;
		}

		if (strcmp(prs.name, PF_RESERVED_ANCHOR) == 0)
			continue;

		strlcpy(newpath, path, sizeof(newpath));
		if (path[0])
			strlcat(newpath, "/", sizeof(newpath));

		strlcat(newpath, prs.name, sizeof(newpath));
		if (pfl_walk_rulesets(newpath))
			goto err;
	}

	return (0);

err:
	return (-1);
}

static int
pfl_refresh(void)
{
	struct pfl_entry *e;

	if (started && this_tick <= pf_tick)
		return (0);

	while (!TAILQ_EMPTY(&pfl_table)) {
		e = TAILQ_FIRST(&pfl_table);
		TAILQ_REMOVE(&pfl_table, e, link);
		free(e);
	}
	pfl_table_count = 0;

	if (pfl_walk_rulesets(""))
		goto err;

	pfl_table_age = time(NULL);
	pf_tick = this_tick;

	return (0);

err:
	while (!TAILQ_EMPTY(&pfl_table)) {
		e = TAILQ_FIRST(&pfl_table);
		TAILQ_REMOVE(&pfl_table, e, link);
		free(e);
	}
	pfl_table_count = 0;

	return (-1);
}

/*
 * check whether altq support is enabled in kernel
 */

static int
altq_is_enabled(int pfdev)
{
	struct pfioc_altq pa;

	errno = 0;
	pa.version = PFIOC_ALTQ_VERSION;
	if (ioctl(pfdev, DIOCGETALTQS, &pa)) {
		if (errno == ENODEV) {
			syslog(LOG_INFO, "No ALTQ support in kernel\n"
			    "ALTQ related functions disabled\n");
			return (0);
		} else
			syslog(LOG_ERR, "DIOCGETALTQS returned an error: %s",
			    strerror(errno));
			return (-1);
	}
	return (1);
}

/*
 * Implement the bsnmpd module interface
 */
static int
pf_init(struct lmodule *mod, int __unused argc, char __unused *argv[])
{
	module = mod;

	if ((dev = open("/dev/pf", O_RDONLY)) == -1) {
		syslog(LOG_ERR, "pf_init(): open(): %s\n",
		    strerror(errno));
		return (-1);
	}

	if ((altq_enabled = altq_is_enabled(dev)) == -1) {
		syslog(LOG_ERR, "pf_init(): altq test failed");
		return (-1);
	}

	/* Prepare internal state */
	TAILQ_INIT(&pfi_table);
	TAILQ_INIT(&pfq_table);
	TAILQ_INIT(&pft_table);
	TAILQ_INIT(&pfa_table);
	TAILQ_INIT(&pfl_table);

	pfi_refresh();
	if (altq_enabled) {
		pfq_refresh();
	}

	pfs_refresh();
	pft_refresh();
	pfa_refresh();
	pfl_refresh();

	started = 1;

	return (0);
}

static int
pf_fini(void)
{
	struct pfi_entry *i1, *i2;
	struct pfq_entry *q1, *q2;
	struct pft_entry *t1, *t2;
	struct pfa_entry *a1, *a2;
	struct pfl_entry *l1, *l2;

	/* Empty the list of interfaces */
	i1 = TAILQ_FIRST(&pfi_table);
	while (i1 != NULL) {
		i2 = TAILQ_NEXT(i1, link);
		free(i1);
		i1 = i2;
	}

	/* List of queues */
	q1 = TAILQ_FIRST(&pfq_table);
	while (q1 != NULL) {
		q2 = TAILQ_NEXT(q1, link);
		free(q1);
		q1 = q2;
	}

	/* List of tables */
	t1 = TAILQ_FIRST(&pft_table);
	while (t1 != NULL) {
		t2 = TAILQ_NEXT(t1, link);
		free(t1);
		t1 = t2;
	}

	/* List of table addresses */
	a1 = TAILQ_FIRST(&pfa_table);
	while (a1 != NULL) {
		a2 = TAILQ_NEXT(a1, link);
		free(a1);
		a1 = a2;
	}

	/* And the list of labeled filter rules */
	l1 = TAILQ_FIRST(&pfl_table);
	while (l1 != NULL) {
		l2 = TAILQ_NEXT(l1, link);
		free(l1);
		l1 = l2;
	}

	close(dev);
	return (0);
}

static void
pf_dump(void)
{
	pfi_refresh();
	if (altq_enabled) {
		pfq_refresh();
	}
	pft_refresh();
	pfa_refresh();
	pfl_refresh();

	syslog(LOG_ERR, "Dump: pfi_table_age = %jd",
	    (intmax_t)pfi_table_age);
	syslog(LOG_ERR, "Dump: pfi_table_count = %d",
	    pfi_table_count);

	syslog(LOG_ERR, "Dump: pfq_table_age = %jd",
	    (intmax_t)pfq_table_age);
	syslog(LOG_ERR, "Dump: pfq_table_count = %d",
	    pfq_table_count);

	syslog(LOG_ERR, "Dump: pft_table_age = %jd",
	    (intmax_t)pft_table_age);
	syslog(LOG_ERR, "Dump: pft_table_count = %d",
	    pft_table_count);

	syslog(LOG_ERR, "Dump: pfa_table_age = %jd",
	    (intmax_t)pfa_table_age);
	syslog(LOG_ERR, "Dump: pfa_table_count = %d",
	    pfa_table_count);

	syslog(LOG_ERR, "Dump: pfl_table_age = %jd",
	    (intmax_t)pfl_table_age);
	syslog(LOG_ERR, "Dump: pfl_table_count = %d",
	    pfl_table_count);
}

const struct snmp_module config = {
	.comment = "This module implements a MIB for the pf packet filter.",
	.init =		pf_init,
	.fini =		pf_fini,
	.tree =		pf_ctree,
	.dump =		pf_dump,
	.tree_size =	pf_CTREE_SIZE,
};
