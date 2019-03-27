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
 * Bridge addresses.
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

TAILQ_HEAD(tp_entries, tp_entry);

/*
 * Free the bridge address list.
 */
static void
bridge_tpe_free(struct tp_entries *headp)
{
	struct tp_entry *t;

	while ((t = TAILQ_FIRST(headp)) != NULL) {
		TAILQ_REMOVE(headp, t, tp_e);
		free(t);
	}
}

/*
 * Free the bridge address entries from the address list,
 * for the specified bridge interface only.
 */
static void
bridge_tpe_bif_free(struct tp_entries *headp,
	struct bridge_if *bif)
{
	struct tp_entry *tp;

	while (bif->f_tpa != NULL && bif->sysindex == bif->f_tpa->sysindex) {
		tp = TAILQ_NEXT(bif->f_tpa, tp_e);
		TAILQ_REMOVE(headp, bif->f_tpa, tp_e);
		free(bif->f_tpa);
		bif->f_tpa = tp;
	}
}

/*
 * Compare two mac addresses.
 * m1 < m2 : -1
 * m1 > m2 : +1
 * m1 = m2 :  0
 */
static int
bridge_compare_macs(const uint8_t *m1, const uint8_t *m2)
{
	int i;

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		if (m1[i] < m2[i])
			return (-1);
		if (m1[i] > m2[i])
			return (1);
	}

	return (0);
}

/*
 * Insert an address entry in the bridge address TAILQ starting to search
 * for its place from the position of the first bridge address for the bridge
 * interface. Update the first bridge address if necessary.
 */
static void
bridge_addrs_insert_at(struct tp_entries *headp,
	struct tp_entry *ta, struct tp_entry **f_tpa)
{
	struct tp_entry *t1;

	assert(f_tpa != NULL);

	for (t1 = *f_tpa;
	    t1 != NULL && ta->sysindex == t1->sysindex;
	    t1 = TAILQ_NEXT(t1, tp_e)) {
		if (bridge_compare_macs(ta->tp_addr, t1->tp_addr) < 0) {
			TAILQ_INSERT_BEFORE(t1, ta, tp_e);
			if (*f_tpa == t1)
				(*f_tpa) = ta;
			return;
		}
	}

	if (t1 == NULL)
		TAILQ_INSERT_TAIL(headp, ta, tp_e);
	else
		TAILQ_INSERT_BEFORE(t1, ta, tp_e);
}

/*
 * Find an address entry's position in the address list
 * according to bridge interface name.
 */
static struct tp_entry *
bridge_addrs_find_pos(struct tp_entries *headp, uint32_t b_idx)
{
	uint32_t t_idx;
	struct tp_entry *t1;

	if ((t1 = TAILQ_FIRST(headp)) == NULL ||
	    bridge_compare_sysidx(b_idx, t1->sysindex) < 0)
		return (NULL);

	t_idx = t1->sysindex;

	for (t1 = TAILQ_NEXT(t1, tp_e); t1 != NULL; t1 = TAILQ_NEXT(t1, tp_e)) {

		if (t1->sysindex != t_idx) {
			if (bridge_compare_sysidx(b_idx, t1->sysindex) < 0)
				return (TAILQ_PREV(t1, tp_entries, tp_e));
			else
				t_idx = t1->sysindex;
		}
	}

	if (t1 == NULL)
		t1 = TAILQ_LAST(headp, tp_entries);

	return (t1);
}

/*
 * Insert a bridge address in the bridge addresses list.
 */
static void
bridge_addrs_bif_insert(struct tp_entries *headp, struct tp_entry *te,
    struct tp_entry **f_tpa)
{
	struct tp_entry *temp;

	if (*f_tpa != NULL)
		bridge_addrs_insert_at(headp, te, f_tpa);
	else {
		temp = bridge_addrs_find_pos(headp, te->sysindex);

		if (temp == NULL)
			TAILQ_INSERT_HEAD(headp, te, tp_e);
		else
			TAILQ_INSERT_AFTER(headp, temp, te, tp_e);
		*f_tpa = te;
	}
}

static struct tp_entries tp_entries = TAILQ_HEAD_INITIALIZER(tp_entries);
static time_t address_list_age;

void
bridge_addrs_update_listage(void)
{
	address_list_age = time(NULL);
}

void
bridge_addrs_fini(void)
{
	bridge_tpe_free(&tp_entries);
}

void
bridge_addrs_free(struct bridge_if *bif)
{
	bridge_tpe_bif_free(&tp_entries, bif);
}

/*
 * Find the first address in the list.
 */
static struct tp_entry *
bridge_addrs_first(void)
{
	return (TAILQ_FIRST(&tp_entries));
}

/*
 * Find the next address in the list.
 */
static struct tp_entry *
bridge_addrs_next(struct tp_entry *te)
{
	return (TAILQ_NEXT(te, tp_e));
}

/*
 * Find the first address, learnt by the specified bridge interface.
 */
struct tp_entry *
bridge_addrs_bif_first(struct bridge_if *bif)
{
	return (bif->f_tpa);
}

/*
 * Find the next address, learnt by the specified bridge interface.
 */
struct tp_entry *
bridge_addrs_bif_next(struct tp_entry *te)
{
	struct tp_entry *te_next;

	if ((te_next = TAILQ_NEXT(te, tp_e)) == NULL ||
	    te_next->sysindex != te->sysindex)
		return (NULL);

	return (te_next);
}

/*
 * Remove a bridge address from the list.
 */
void
bridge_addrs_remove(struct tp_entry *te, struct bridge_if *bif)
{
	if (bif->f_tpa == te)
		bif->f_tpa = bridge_addrs_bif_next(te);

	TAILQ_REMOVE(&tp_entries, te, tp_e);
	free(te);
}

/*
 * Allocate memory for a new bridge address and insert it in the list.
 */
struct tp_entry *
bridge_new_addrs(uint8_t *mac, struct bridge_if *bif)
{
	struct tp_entry *te;

	if ((te = (struct tp_entry *) malloc(sizeof(*te))) == NULL) {
		syslog(LOG_ERR, "bridge new address: failed: %s",
		    strerror(errno));
		return (NULL);
	}

	bzero(te, sizeof(*te));

	te->sysindex = bif->sysindex;
	bcopy(mac, te->tp_addr, ETHER_ADDR_LEN);
	bridge_addrs_bif_insert(&tp_entries, te, &(bif->f_tpa));

	return (te);
}

/*
 * Given a mac address, learnt on a bridge,
 * find the corrsponding TP entry for it.
 */
struct tp_entry *
bridge_addrs_find(uint8_t *mac, struct bridge_if *bif)
{
	struct tp_entry *te;

	for (te = bif->f_tpa; te != NULL; te = TAILQ_NEXT(te, tp_e)) {
		if (te->sysindex != bif->sysindex) {
			te = NULL;
			break;
		}

		if (bridge_compare_macs(te->tp_addr, mac) == 0)
			break;
	}

	return (te);
}

void
bridge_addrs_dump(struct bridge_if *bif)
{
	struct tp_entry *te;

	syslog(LOG_ERR, "Addresses count - %d", bif->num_addrs);
	for (te = bridge_addrs_bif_first(bif); te != NULL;
	    te = bridge_addrs_bif_next(te)) {
		syslog(LOG_ERR, "address %x:%x:%x:%x:%x:%x on port %d.%d",
		    te->tp_addr[0], te->tp_addr[1], te->tp_addr[2],
		    te->tp_addr[3], te->tp_addr[4], te->tp_addr[5],
		    te->sysindex, te->port_no);
	}
}

/*
 * RFC4188 specifics.
 */

/*
 * Construct the SNMP index from the address DST Mac.
 */
static void
bridge_addrs_index_append(struct asn_oid *oid, uint sub,
	const struct tp_entry *te)
{
	int i;

	oid->len = sub + ETHER_ADDR_LEN + 1;
	oid->subs[sub] = ETHER_ADDR_LEN;

	for (i = 1; i <= ETHER_ADDR_LEN; i++)
		oid->subs[sub + i] = te->tp_addr[i - 1];
}

/*
 * Find the address entry for the SNMP index from the default bridge only.
 */
static struct tp_entry *
bridge_addrs_get(const struct asn_oid *oid, uint sub,
	struct bridge_if *bif)
{
	int i;
	uint8_t tp_addr[ETHER_ADDR_LEN];

	if (oid->len - sub != ETHER_ADDR_LEN + 1 ||
	    oid->subs[sub] != ETHER_ADDR_LEN)
		return (NULL);

	for (i = 0; i < ETHER_ADDR_LEN; i++)
		tp_addr[i] = oid->subs[sub + i + 1];

	return (bridge_addrs_find(tp_addr, bif));
}

/*
 * Find the next address entry for the SNMP index
 * from the default bridge only.
 */
static struct tp_entry *
bridge_addrs_getnext(const struct asn_oid *oid, uint sub,
	struct bridge_if *bif)
{
	int i;
	uint8_t tp_addr[ETHER_ADDR_LEN];
	static struct tp_entry *te;

	if (oid->len - sub == 0)
		return (bridge_addrs_bif_first(bif));

	if (oid->len - sub != ETHER_ADDR_LEN + 1 ||
	    oid->subs[sub] != ETHER_ADDR_LEN)
		return (NULL);

	for (i = 0; i < ETHER_ADDR_LEN; i++)
		tp_addr[i] = oid->subs[sub + i + 1];

	if ((te = bridge_addrs_find(tp_addr, bif)) == NULL)
		return (NULL);

	return (bridge_addrs_bif_next(te));
}

int
op_dot1d_tp_fdb(struct snmp_context *c __unused, struct snmp_value *val,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	struct bridge_if *bif;
	struct tp_entry *te;

	if ((bif = bridge_get_default()) == NULL)
		return (SNMP_ERR_NOSUCHNAME);

	if (time(NULL) - bif->addrs_age > bridge_get_data_maxage() &&
	    bridge_update_addrs(bif) <= 0)
		return (SNMP_ERR_NOSUCHNAME);

	switch (op) {
	    case SNMP_OP_GET:
		if ((te = bridge_addrs_get(&val->var, sub, bif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_GETNEXT:
		if ((te = bridge_addrs_getnext(&val->var, sub, bif)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		bridge_addrs_index_append(&val->var, sub, te);
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
		case LEAF_dot1dTpFdbAddress:
			return (string_get(val, te->tp_addr, ETHER_ADDR_LEN));
		case LEAF_dot1dTpFdbPort :
			val->v.integer = te->port_no;
			return (SNMP_ERR_NOERROR);
		case LEAF_dot1dTpFdbStatus:
			val->v.integer = te->status;
			return (SNMP_ERR_NOERROR);
	}

	abort();
}

/*
 * Private BEGEMOT-BRIDGE-MIB specifics.
 */

/*
 * Construct the SNMP index from the bridge interface name
 * and the address DST Mac.
 */
static int
bridge_addrs_begemot_index_append(struct asn_oid *oid, uint sub,
	const struct tp_entry *te)
{
	uint i, n_len;
	const char *b_name;

	if ((b_name = bridge_if_find_name(te->sysindex)) == NULL)
		return (-1);

	n_len = strlen(b_name);
	oid->len = sub++;
	oid->subs[oid->len++] = n_len;

	for (i = 1; i <= n_len; i++)
		oid->subs[oid->len++] = b_name[i - 1];

	oid->subs[oid->len++] = ETHER_ADDR_LEN;
	for (i = 1 ; i <= ETHER_ADDR_LEN; i++)
		oid->subs[oid->len++] = te->tp_addr[i - 1];

	return (0);
}

/*
 * Find a bridge address entry by the bridge interface name
 * and the address DST Mac.
 */
static struct tp_entry *
bridge_addrs_begemot_get(const struct asn_oid *oid, uint sub)
{
	uint i, n_len;
	uint8_t tp_addr[ETHER_ADDR_LEN];
	char bif_name[IFNAMSIZ];
	struct bridge_if *bif;

	n_len = oid->subs[sub];
	if (oid->len - sub != n_len + ETHER_ADDR_LEN + 3 ||
	    n_len >= IFNAMSIZ || oid->subs[sub + n_len + 1] != ETHER_ADDR_LEN)
		return (NULL);

	for (i = 0; i < n_len; i++)
		bif_name[i] = oid->subs[n_len + i + 1];
	bif_name[i] = '\0';

	for (i = 1; i <= ETHER_ADDR_LEN; i++)
		tp_addr[i - 1] = oid->subs[n_len + i + 1];

	if ((bif = bridge_if_find_ifname(bif_name)) == NULL)
		return (NULL);

	return (bridge_addrs_find(tp_addr, bif));
}

/*
 * Find the next bridge address entry by the bridge interface name
 * and the address DST Mac.
 */
static struct tp_entry *
bridge_addrs_begemot_getnext(const struct asn_oid *oid, uint sub)
{
	uint i, n_len;
	uint8_t tp_addr[ETHER_ADDR_LEN];
	char bif_name[IFNAMSIZ];
	struct bridge_if *bif;
	struct tp_entry *tp;

	if (oid->len - sub == 0)
		return (bridge_addrs_first());

	n_len = oid->subs[sub];
	if (oid->len - sub != n_len + ETHER_ADDR_LEN + 2 ||
	    n_len >= IFNAMSIZ || oid->subs[sub + n_len + 1] != ETHER_ADDR_LEN)
		return (NULL);

	for (i = 1; i <= n_len; i++)
		bif_name[i - 1] = oid->subs[sub + i];

	bif_name[i - 1] = '\0';

	for (i = 1; i <= ETHER_ADDR_LEN; i++)
		tp_addr[i - 1] = oid->subs[sub + n_len + i + 1];

	if ((bif = bridge_if_find_ifname(bif_name)) == NULL ||
	    (tp = bridge_addrs_find(tp_addr, bif)) == NULL)
		return (NULL);

	return (bridge_addrs_next(tp));
}

int
op_begemot_tp_fdb(struct snmp_context *c __unused, struct snmp_value *val,
	uint sub, uint iidx __unused, enum snmp_op op)
{
	struct tp_entry *te;

	if (time(NULL) - address_list_age > bridge_get_data_maxage())
		bridge_update_all_addrs();

	switch (op) {
	    case SNMP_OP_GET:
		if ((te = bridge_addrs_begemot_get(&val->var, sub)) == NULL)
		    return (SNMP_ERR_NOSUCHNAME);
		goto get;

	    case SNMP_OP_GETNEXT:
		if ((te = bridge_addrs_begemot_getnext(&val->var,
		    sub)) == NULL ||
		    bridge_addrs_begemot_index_append(&val->var,
		    sub, te) < 0)
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
	    case LEAF_begemotBridgeTpFdbAddress:
		return (string_get(val, te->tp_addr, ETHER_ADDR_LEN));
	    case LEAF_begemotBridgeTpFdbPort:
		val->v.integer = te->port_no;
		return (SNMP_ERR_NOERROR);
	    case LEAF_begemotBridgeTpFdbStatus:
		val->v.integer = te->status;
		return (SNMP_ERR_NOERROR);
	}

	abort();
}
