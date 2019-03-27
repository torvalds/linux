/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Ruslan Ermilov and Vsevolod Lobko.
 * Copyright (c) 2014 Yandex LLC
 * Copyright (c) 2014 Alexander V. Chernikov
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Lookup table support for ipfw.
 *
 * This file contains handlers for all generic tables' operations:
 * add/del/flush entries, list/dump tables etc..
 *
 * Table data modification is protected by both UH and runtime lock
 * while reading configuration/data is protected by UH lock.
 *
 * Lookup algorithms for all table types are located in ip_fw_table_algo.c
 */

#include "opt_ipfw.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/queue.h>
#include <net/if.h>	/* ip_fw.h requires IFNAMSIZ */

#include <netinet/in.h>
#include <netinet/ip_var.h>	/* struct ipfw_rule_ref */
#include <netinet/ip_fw.h>

#include <netpfil/ipfw/ip_fw_private.h>
#include <netpfil/ipfw/ip_fw_table.h>

 /*
 * Table has the following `type` concepts:
 *
 * `no.type` represents lookup key type (addr, ifp, uid, etc..)
 * vmask represents bitmask of table values which are present at the moment.
 * Special IPFW_VTYPE_LEGACY ( (uint32_t)-1 ) represents old
 * single-value-for-all approach.
 */
struct table_config {
	struct named_object	no;
	uint8_t		tflags;		/* type flags */
	uint8_t		locked;		/* 1 if locked from changes */
	uint8_t		linked;		/* 1 if already linked */
	uint8_t		ochanged;	/* used by set swapping */
	uint8_t		vshared;	/* 1 if using shared value array */
	uint8_t		spare[3];
	uint32_t	count;		/* Number of records */
	uint32_t	limit;		/* Max number of records */
	uint32_t	vmask;		/* bitmask with supported values */
	uint32_t	ocount;		/* used by set swapping */
	uint64_t	gencnt;		/* generation count */
	char		tablename[64];	/* table name */
	struct table_algo	*ta;	/* Callbacks for given algo */
	void		*astate;	/* algorithm state */
	struct table_info	ti_copy;	/* data to put to table_info */
	struct namedobj_instance	*vi;
};

static int find_table_err(struct namedobj_instance *ni, struct tid_info *ti,
    struct table_config **tc);
static struct table_config *find_table(struct namedobj_instance *ni,
    struct tid_info *ti);
static struct table_config *alloc_table_config(struct ip_fw_chain *ch,
    struct tid_info *ti, struct table_algo *ta, char *adata, uint8_t tflags);
static void free_table_config(struct namedobj_instance *ni,
    struct table_config *tc);
static int create_table_internal(struct ip_fw_chain *ch, struct tid_info *ti,
    char *aname, ipfw_xtable_info *i, uint16_t *pkidx, int ref);
static void link_table(struct ip_fw_chain *ch, struct table_config *tc);
static void unlink_table(struct ip_fw_chain *ch, struct table_config *tc);
static int find_ref_table(struct ip_fw_chain *ch, struct tid_info *ti,
    struct tentry_info *tei, uint32_t count, int op, struct table_config **ptc);
#define	OP_ADD	1
#define	OP_DEL	0
static int export_tables(struct ip_fw_chain *ch, ipfw_obj_lheader *olh,
    struct sockopt_data *sd);
static void export_table_info(struct ip_fw_chain *ch, struct table_config *tc,
    ipfw_xtable_info *i);
static int dump_table_tentry(void *e, void *arg);
static int dump_table_xentry(void *e, void *arg);

static int swap_tables(struct ip_fw_chain *ch, struct tid_info *a,
    struct tid_info *b);

static int check_table_name(const char *name);
static int check_table_space(struct ip_fw_chain *ch, struct tableop_state *ts,
    struct table_config *tc, struct table_info *ti, uint32_t count);
static int destroy_table(struct ip_fw_chain *ch, struct tid_info *ti);

static struct table_algo *find_table_algo(struct tables_config *tableconf,
    struct tid_info *ti, char *name);

static void objheader_to_ti(struct _ipfw_obj_header *oh, struct tid_info *ti);
static void ntlv_to_ti(struct _ipfw_obj_ntlv *ntlv, struct tid_info *ti);

#define	CHAIN_TO_NI(chain)	(CHAIN_TO_TCFG(chain)->namehash)
#define	KIDX_TO_TI(ch, k)	(&(((struct table_info *)(ch)->tablestate)[k]))

#define	TA_BUF_SZ	128	/* On-stack buffer for add/delete state */

void
rollback_toperation_state(struct ip_fw_chain *ch, void *object)
{
	struct tables_config *tcfg;
	struct op_state *os;

	tcfg = CHAIN_TO_TCFG(ch);
	TAILQ_FOREACH(os, &tcfg->state_list, next)
		os->func(object, os);
}

void
add_toperation_state(struct ip_fw_chain *ch, struct tableop_state *ts)
{
	struct tables_config *tcfg;

	tcfg = CHAIN_TO_TCFG(ch);
	TAILQ_INSERT_HEAD(&tcfg->state_list, &ts->opstate, next);
}

void
del_toperation_state(struct ip_fw_chain *ch, struct tableop_state *ts)
{
	struct tables_config *tcfg;

	tcfg = CHAIN_TO_TCFG(ch);
	TAILQ_REMOVE(&tcfg->state_list, &ts->opstate, next);
}

void
tc_ref(struct table_config *tc)
{

	tc->no.refcnt++;
}

void
tc_unref(struct table_config *tc)
{

	tc->no.refcnt--;
}

static struct table_value *
get_table_value(struct ip_fw_chain *ch, struct table_config *tc, uint32_t kidx)
{
	struct table_value *pval;

	pval = (struct table_value *)ch->valuestate;

	return (&pval[kidx]);
}


/*
 * Checks if we're able to insert/update entry @tei into table
 * w.r.t @tc limits.
 * May alter @tei to indicate insertion error / insert
 * options.
 *
 * Returns 0 if operation can be performed/
 */
static int
check_table_limit(struct table_config *tc, struct tentry_info *tei)
{

	if (tc->limit == 0 || tc->count < tc->limit)
		return (0);

	if ((tei->flags & TEI_FLAGS_UPDATE) == 0) {
		/* Notify userland on error cause */
		tei->flags |= TEI_FLAGS_LIMIT;
		return (EFBIG);
	}

	/*
	 * We have UPDATE flag set.
	 * Permit updating record (if found),
	 * but restrict adding new one since we've
	 * already hit the limit.
	 */
	tei->flags |= TEI_FLAGS_DONTADD;

	return (0);
}

/*
 * Convert algorithm callback return code into
 * one of pre-defined states known by userland.
 */
static void
store_tei_result(struct tentry_info *tei, int op, int error, uint32_t num)
{
	int flag;

	flag = 0;

	switch (error) {
	case 0:
		if (op == OP_ADD && num != 0)
			flag = TEI_FLAGS_ADDED;
		if (op == OP_DEL)
			flag = TEI_FLAGS_DELETED;
		break;
	case ENOENT:
		flag = TEI_FLAGS_NOTFOUND;
		break;
	case EEXIST:
		flag = TEI_FLAGS_EXISTS;
		break;
	default:
		flag = TEI_FLAGS_ERROR;
	}

	tei->flags |= flag;
}

/*
 * Creates and references table with default parameters.
 * Saves table config, algo and allocated kidx info @ptc, @pta and
 * @pkidx if non-zero.
 * Used for table auto-creation to support old binaries.
 *
 * Returns 0 on success.
 */
static int
create_table_compat(struct ip_fw_chain *ch, struct tid_info *ti,
    uint16_t *pkidx)
{
	ipfw_xtable_info xi;
	int error;

	memset(&xi, 0, sizeof(xi));
	/* Set default value mask for legacy clients */
	xi.vmask = IPFW_VTYPE_LEGACY;

	error = create_table_internal(ch, ti, NULL, &xi, pkidx, 1);
	if (error != 0)
		return (error);

	return (0);
}

/*
 * Find and reference existing table optionally
 * creating new one.
 *
 * Saves found table config into @ptc.
 * Note function may drop/acquire UH_WLOCK.
 * Returns 0 if table was found/created and referenced
 * or non-zero return code.
 */
static int
find_ref_table(struct ip_fw_chain *ch, struct tid_info *ti,
    struct tentry_info *tei, uint32_t count, int op,
    struct table_config **ptc)
{
	struct namedobj_instance *ni;
	struct table_config *tc;
	uint16_t kidx;
	int error;

	IPFW_UH_WLOCK_ASSERT(ch);

	ni = CHAIN_TO_NI(ch);
	tc = NULL;
	if ((tc = find_table(ni, ti)) != NULL) {
		/* check table type */
		if (tc->no.subtype != ti->type)
			return (EINVAL);

		if (tc->locked != 0)
			return (EACCES);

		/* Try to exit early on limit hit */
		if (op == OP_ADD && count == 1 &&
		    check_table_limit(tc, tei) != 0)
			return (EFBIG);

		/* Reference and return */
		tc->no.refcnt++;
		*ptc = tc;
		return (0);
	}

	if (op == OP_DEL)
		return (ESRCH);

	/* Compatibility mode: create new table for old clients */
	if ((tei->flags & TEI_FLAGS_COMPAT) == 0)
		return (ESRCH);

	IPFW_UH_WUNLOCK(ch);
	error = create_table_compat(ch, ti, &kidx);
	IPFW_UH_WLOCK(ch);
	
	if (error != 0)
		return (error);

	tc = (struct table_config *)ipfw_objhash_lookup_kidx(ni, kidx);
	KASSERT(tc != NULL, ("create_table_compat returned bad idx %d", kidx));

	/* OK, now we've got referenced table. */
	*ptc = tc;
	return (0);
}

/*
 * Rolls back already @added to @tc entries using state array @ta_buf_m.
 * Assume the following layout:
 * 1) ADD state (ta_buf_m[0] ... t_buf_m[added - 1]) for handling update cases
 * 2) DEL state (ta_buf_m[count[ ... t_buf_m[count + added - 1])
 *   for storing deleted state
 */
static void
rollback_added_entries(struct ip_fw_chain *ch, struct table_config *tc,
    struct table_info *tinfo, struct tentry_info *tei, caddr_t ta_buf_m,
    uint32_t count, uint32_t added)
{
	struct table_algo *ta;
	struct tentry_info *ptei;
	caddr_t v, vv;
	size_t ta_buf_sz;
	int error, i;
	uint32_t num;

	IPFW_UH_WLOCK_ASSERT(ch);

	ta = tc->ta;
	ta_buf_sz = ta->ta_buf_size;
	v = ta_buf_m;
	vv = v + count * ta_buf_sz;
	for (i = 0; i < added; i++, v += ta_buf_sz, vv += ta_buf_sz) {
		ptei = &tei[i];
		if ((ptei->flags & TEI_FLAGS_UPDATED) != 0) {

			/*
			 * We have old value stored by previous
			 * call in @ptei->value. Do add once again
			 * to restore it.
			 */
			error = ta->add(tc->astate, tinfo, ptei, v, &num);
			KASSERT(error == 0, ("rollback UPDATE fail"));
			KASSERT(num == 0, ("rollback UPDATE fail2"));
			continue;
		}

		error = ta->prepare_del(ch, ptei, vv);
		KASSERT(error == 0, ("pre-rollback INSERT failed"));
		error = ta->del(tc->astate, tinfo, ptei, vv, &num);
		KASSERT(error == 0, ("rollback INSERT failed"));
		tc->count -= num;
	}
}

/*
 * Prepares add/del state for all @count entries in @tei.
 * Uses either stack buffer (@ta_buf) or allocates a new one.
 * Stores pointer to allocated buffer back to @ta_buf.
 *
 * Returns 0 on success.
 */
static int
prepare_batch_buffer(struct ip_fw_chain *ch, struct table_algo *ta,
    struct tentry_info *tei, uint32_t count, int op, caddr_t *ta_buf)
{
	caddr_t ta_buf_m, v;
	size_t ta_buf_sz, sz;
	struct tentry_info *ptei;
	int error, i;

	error = 0;
	ta_buf_sz = ta->ta_buf_size;
	if (count == 1) {
		/* Single add/delete, use on-stack buffer */
		memset(*ta_buf, 0, TA_BUF_SZ);
		ta_buf_m = *ta_buf;
	} else {

		/*
		 * Multiple adds/deletes, allocate larger buffer
		 *
		 * Note we need 2xcount buffer for add case:
		 * we have hold both ADD state
		 * and DELETE state (this may be needed
		 * if we need to rollback all changes)
		 */
		sz = count * ta_buf_sz;
		ta_buf_m = malloc((op == OP_ADD) ? sz * 2 : sz, M_TEMP,
		    M_WAITOK | M_ZERO);
	}

	v = ta_buf_m;
	for (i = 0; i < count; i++, v += ta_buf_sz) {
		ptei = &tei[i];
		error = (op == OP_ADD) ?
		    ta->prepare_add(ch, ptei, v) : ta->prepare_del(ch, ptei, v);

		/*
		 * Some syntax error (incorrect mask, or address, or
		 * anything). Return error regardless of atomicity
		 * settings.
		 */
		if (error != 0)
			break;
	}

	*ta_buf = ta_buf_m;
	return (error);
}

/*
 * Flushes allocated state for each @count entries in @tei.
 * Frees @ta_buf_m if differs from stack buffer @ta_buf.
 */
static void
flush_batch_buffer(struct ip_fw_chain *ch, struct table_algo *ta,
    struct tentry_info *tei, uint32_t count, int rollback,
    caddr_t ta_buf_m, caddr_t ta_buf)
{
	caddr_t v;
	struct tentry_info *ptei;
	size_t ta_buf_sz;
	int i;

	ta_buf_sz = ta->ta_buf_size;

	/* Run cleaning callback anyway */
	v = ta_buf_m;
	for (i = 0; i < count; i++, v += ta_buf_sz) {
		ptei = &tei[i];
		ta->flush_entry(ch, ptei, v);
		if (ptei->ptv != NULL) {
			free(ptei->ptv, M_IPFW);
			ptei->ptv = NULL;
		}
	}

	/* Clean up "deleted" state in case of rollback */
	if (rollback != 0) {
		v = ta_buf_m + count * ta_buf_sz;
		for (i = 0; i < count; i++, v += ta_buf_sz)
			ta->flush_entry(ch, &tei[i], v);
	}

	if (ta_buf_m != ta_buf)
		free(ta_buf_m, M_TEMP);
}


static void
rollback_add_entry(void *object, struct op_state *_state)
{
	struct ip_fw_chain *ch;
	struct tableop_state *ts;

	ts = (struct tableop_state *)_state;

	if (ts->tc != object && ts->ch != object)
		return;

	ch = ts->ch;

	IPFW_UH_WLOCK_ASSERT(ch);

	/* Call specifid unlockers */
	rollback_table_values(ts);

	/* Indicate we've called */
	ts->modified = 1;
}

/*
 * Adds/updates one or more entries in table @ti.
 *
 * Function may drop/reacquire UH wlock multiple times due to
 * items alloc, algorithm callbacks (check_space), value linkage
 * (new values, value storage realloc), etc..
 * Other processes like other adds (which may involve storage resize),
 * table swaps (which changes table data and may change algo type),
 * table modify (which may change value mask) may be executed
 * simultaneously so we need to deal with it.
 *
 * The following approach was implemented:
 * we have per-chain linked list, protected with UH lock.
 * add_table_entry prepares special on-stack structure wthich is passed
 * to its descendants. Users add this structure to this list before unlock.
 * After performing needed operations and acquiring UH lock back, each user
 * checks if structure has changed. If true, it rolls local state back and
 * returns without error to the caller.
 * add_table_entry() on its own checks if structure has changed and restarts
 * its operation from the beginning (goto restart).
 *
 * Functions which are modifying fields of interest (currently
 *   resize_shared_value_storage() and swap_tables() )
 * traverses given list while holding UH lock immediately before
 * performing their operations calling function provided be list entry
 * ( currently rollback_add_entry  ) which performs rollback for all necessary
 * state and sets appropriate values in structure indicating rollback
 * has happened.
 *
 * Algo interaction:
 * Function references @ti first to ensure table won't
 * disappear or change its type.
 * After that, prepare_add callback is called for each @tei entry.
 * Next, we try to add each entry under UH+WHLOCK
 * using add() callback.
 * Finally, we free all state by calling flush_entry callback
 * for each @tei.
 *
 * Returns 0 on success.
 */
int
add_table_entry(struct ip_fw_chain *ch, struct tid_info *ti,
    struct tentry_info *tei, uint8_t flags, uint32_t count)
{
	struct table_config *tc;
	struct table_algo *ta;
	uint16_t kidx;
	int error, first_error, i, rollback;
	uint32_t num, numadd;
	struct tentry_info *ptei;
	struct tableop_state ts;
	char ta_buf[TA_BUF_SZ];
	caddr_t ta_buf_m, v;

	memset(&ts, 0, sizeof(ts));
	ta = NULL;
	IPFW_UH_WLOCK(ch);

	/*
	 * Find and reference existing table.
	 */
restart:
	if (ts.modified != 0) {
		IPFW_UH_WUNLOCK(ch);
		flush_batch_buffer(ch, ta, tei, count, rollback,
		    ta_buf_m, ta_buf);
		memset(&ts, 0, sizeof(ts));
		ta = NULL;
		IPFW_UH_WLOCK(ch);
	}

	error = find_ref_table(ch, ti, tei, count, OP_ADD, &tc);
	if (error != 0) {
		IPFW_UH_WUNLOCK(ch);
		return (error);
	}
	ta = tc->ta;

	/* Fill in tablestate */
	ts.ch = ch;
	ts.opstate.func = rollback_add_entry;
	ts.tc = tc;
	ts.vshared = tc->vshared;
	ts.vmask = tc->vmask;
	ts.ta = ta;
	ts.tei = tei;
	ts.count = count;
	rollback = 0;
	add_toperation_state(ch, &ts);
	IPFW_UH_WUNLOCK(ch);

	/* Allocate memory and prepare record(s) */
	/* Pass stack buffer by default */
	ta_buf_m = ta_buf;
	error = prepare_batch_buffer(ch, ta, tei, count, OP_ADD, &ta_buf_m);

	IPFW_UH_WLOCK(ch);
	del_toperation_state(ch, &ts);
	/* Drop reference we've used in first search */
	tc->no.refcnt--;

	/* Check prepare_batch_buffer() error */
	if (error != 0)
		goto cleanup;

	/*
	 * Check if table swap has happened.
	 * (so table algo might be changed).
	 * Restart operation to achieve consistent behavior.
	 */
	if (ts.modified != 0)
		goto restart;

	/*
	 * Link all values values to shared/per-table value array.
	 *
	 * May release/reacquire UH_WLOCK.
	 */
	error = ipfw_link_table_values(ch, &ts);
	if (error != 0)
		goto cleanup;
	if (ts.modified != 0)
		goto restart;

	/*
	 * Ensure we are able to add all entries without additional
	 * memory allocations. May release/reacquire UH_WLOCK.
	 */
	kidx = tc->no.kidx;
	error = check_table_space(ch, &ts, tc, KIDX_TO_TI(ch, kidx), count);
	if (error != 0)
		goto cleanup;
	if (ts.modified != 0)
		goto restart;

	/* We've got valid table in @tc. Let's try to add data */
	kidx = tc->no.kidx;
	ta = tc->ta;
	numadd = 0;
	first_error = 0;

	IPFW_WLOCK(ch);

	v = ta_buf_m;
	for (i = 0; i < count; i++, v += ta->ta_buf_size) {
		ptei = &tei[i];
		num = 0;
		/* check limit before adding */
		if ((error = check_table_limit(tc, ptei)) == 0) {
			error = ta->add(tc->astate, KIDX_TO_TI(ch, kidx),
			    ptei, v, &num);
			/* Set status flag to inform userland */
			store_tei_result(ptei, OP_ADD, error, num);
		}
		if (error == 0) {
			/* Update number of records to ease limit checking */
			tc->count += num;
			numadd += num;
			continue;
		}

		if (first_error == 0)
			first_error = error;

		/*
		 * Some error have happened. Check our atomicity
		 * settings: continue if atomicity is not required,
		 * rollback changes otherwise.
		 */
		if ((flags & IPFW_CTF_ATOMIC) == 0)
			continue;

		rollback_added_entries(ch, tc, KIDX_TO_TI(ch, kidx),
		    tei, ta_buf_m, count, i);

		rollback = 1;
		break;
	}

	IPFW_WUNLOCK(ch);

	ipfw_garbage_table_values(ch, tc, tei, count, rollback);

	/* Permit post-add algorithm grow/rehash. */
	if (numadd != 0)
		check_table_space(ch, NULL, tc, KIDX_TO_TI(ch, kidx), 0);

	/* Return first error to user, if any */
	error = first_error;

cleanup:
	IPFW_UH_WUNLOCK(ch);

	flush_batch_buffer(ch, ta, tei, count, rollback, ta_buf_m, ta_buf);
	
	return (error);
}

/*
 * Deletes one or more entries in table @ti.
 *
 * Returns 0 on success.
 */
int
del_table_entry(struct ip_fw_chain *ch, struct tid_info *ti,
    struct tentry_info *tei, uint8_t flags, uint32_t count)
{
	struct table_config *tc;
	struct table_algo *ta;
	struct tentry_info *ptei;
	uint16_t kidx;
	int error, first_error, i;
	uint32_t num, numdel;
	char ta_buf[TA_BUF_SZ];
	caddr_t ta_buf_m, v;

	/*
	 * Find and reference existing table.
	 */
	IPFW_UH_WLOCK(ch);
	error = find_ref_table(ch, ti, tei, count, OP_DEL, &tc);
	if (error != 0) {
		IPFW_UH_WUNLOCK(ch);
		return (error);
	}
	ta = tc->ta;
	IPFW_UH_WUNLOCK(ch);

	/* Allocate memory and prepare record(s) */
	/* Pass stack buffer by default */
	ta_buf_m = ta_buf;
	error = prepare_batch_buffer(ch, ta, tei, count, OP_DEL, &ta_buf_m);
	if (error != 0)
		goto cleanup;

	IPFW_UH_WLOCK(ch);

	/* Drop reference we've used in first search */
	tc->no.refcnt--;

	/*
	 * Check if table algo is still the same.
	 * (changed ta may be the result of table swap).
	 */
	if (ta != tc->ta) {
		IPFW_UH_WUNLOCK(ch);
		error = EINVAL;
		goto cleanup;
	}

	kidx = tc->no.kidx;
	numdel = 0;
	first_error = 0;

	IPFW_WLOCK(ch);
	v = ta_buf_m;
	for (i = 0; i < count; i++, v += ta->ta_buf_size) {
		ptei = &tei[i];
		num = 0;
		error = ta->del(tc->astate, KIDX_TO_TI(ch, kidx), ptei, v,
		    &num);
		/* Save state for userland */
		store_tei_result(ptei, OP_DEL, error, num);
		if (error != 0 && first_error == 0)
			first_error = error;
		tc->count -= num;
		numdel += num;
	}
	IPFW_WUNLOCK(ch);

	/* Unlink non-used values */
	ipfw_garbage_table_values(ch, tc, tei, count, 0);

	if (numdel != 0) {
		/* Run post-del hook to permit shrinking */
		check_table_space(ch, NULL, tc, KIDX_TO_TI(ch, kidx), 0);
	}

	IPFW_UH_WUNLOCK(ch);

	/* Return first error to user, if any */
	error = first_error;

cleanup:
	flush_batch_buffer(ch, ta, tei, count, 0, ta_buf_m, ta_buf);

	return (error);
}

/*
 * Ensure that table @tc has enough space to add @count entries without
 * need for reallocation.
 *
 * Callbacks order:
 * 0) need_modify() (UH_WLOCK) - checks if @count items can be added w/o resize.
 *
 * 1) alloc_modify (no locks, M_WAITOK) - alloc new state based on @pflags.
 * 2) prepare_modifyt (UH_WLOCK) - copy old data into new storage
 * 3) modify (UH_WLOCK + WLOCK) - switch pointers
 * 4) flush_modify (UH_WLOCK) - free state, if needed
 *
 * Returns 0 on success.
 */
static int
check_table_space(struct ip_fw_chain *ch, struct tableop_state *ts,
    struct table_config *tc, struct table_info *ti, uint32_t count)
{
	struct table_algo *ta;
	uint64_t pflags;
	char ta_buf[TA_BUF_SZ];
	int error;

	IPFW_UH_WLOCK_ASSERT(ch);

	error = 0;
	ta = tc->ta;
	if (ta->need_modify == NULL)
		return (0);

	/* Acquire reference not to loose @tc between locks/unlocks */
	tc->no.refcnt++;

	/*
	 * TODO: think about avoiding race between large add/large delete
	 * operation on algorithm which implements shrinking along with
	 * growing.
	 */
	while (true) {
		pflags = 0;
		if (ta->need_modify(tc->astate, ti, count, &pflags) == 0) {
			error = 0;
			break;
		}

		/* We have to shrink/grow table */
		if (ts != NULL)
			add_toperation_state(ch, ts);
		IPFW_UH_WUNLOCK(ch);

		memset(&ta_buf, 0, sizeof(ta_buf));
		error = ta->prepare_mod(ta_buf, &pflags);

		IPFW_UH_WLOCK(ch);
		if (ts != NULL)
			del_toperation_state(ch, ts);

		if (error != 0)
			break;

		if (ts != NULL && ts->modified != 0) {

			/*
			 * Swap operation has happened
			 * so we're currently operating on other
			 * table data. Stop doing this.
			 */
			ta->flush_mod(ta_buf);
			break;
		}

		/* Check if we still need to alter table */
		ti = KIDX_TO_TI(ch, tc->no.kidx);
		if (ta->need_modify(tc->astate, ti, count, &pflags) == 0) {
			IPFW_UH_WUNLOCK(ch);

			/*
			 * Other thread has already performed resize.
			 * Flush our state and return.
			 */
			ta->flush_mod(ta_buf);
			break;
		}
	
		error = ta->fill_mod(tc->astate, ti, ta_buf, &pflags);
		if (error == 0) {
			/* Do actual modification */
			IPFW_WLOCK(ch);
			ta->modify(tc->astate, ti, ta_buf, pflags);
			IPFW_WUNLOCK(ch);
		}

		/* Anyway, flush data and retry */
		ta->flush_mod(ta_buf);
	}

	tc->no.refcnt--;
	return (error);
}

/*
 * Adds or deletes record in table.
 * Data layout (v0):
 * Request: [ ip_fw3_opheader ipfw_table_xentry ]
 *
 * Returns 0 on success
 */
static int
manage_table_ent_v0(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_table_xentry *xent;
	struct tentry_info tei;
	struct tid_info ti;
	struct table_value v;
	int error, hdrlen, read;

	hdrlen = offsetof(ipfw_table_xentry, k);

	/* Check minimum header size */
	if (sd->valsize < (sizeof(*op3) + hdrlen))
		return (EINVAL);

	read = sizeof(ip_fw3_opheader);

	/* Check if xentry len field is valid */
	xent = (ipfw_table_xentry *)(op3 + 1);
	if (xent->len < hdrlen || xent->len + read > sd->valsize)
		return (EINVAL);
	
	memset(&tei, 0, sizeof(tei));
	tei.paddr = &xent->k;
	tei.masklen = xent->masklen;
	ipfw_import_table_value_legacy(xent->value, &v);
	tei.pvalue = &v;
	/* Old requests compatibility */
	tei.flags = TEI_FLAGS_COMPAT;
	if (xent->type == IPFW_TABLE_ADDR) {
		if (xent->len - hdrlen == sizeof(in_addr_t))
			tei.subtype = AF_INET;
		else
			tei.subtype = AF_INET6;
	}

	memset(&ti, 0, sizeof(ti));
	ti.uidx = xent->tbl;
	ti.type = xent->type;

	error = (op3->opcode == IP_FW_TABLE_XADD) ?
	    add_table_entry(ch, &ti, &tei, 0, 1) :
	    del_table_entry(ch, &ti, &tei, 0, 1);

	return (error);
}

/*
 * Adds or deletes record in table.
 * Data layout (v1)(current):
 * Request: [ ipfw_obj_header
 *   ipfw_obj_ctlv(IPFW_TLV_TBLENT_LIST) [ ipfw_obj_tentry x N ]
 * ]
 *
 * Returns 0 on success
 */
static int
manage_table_ent_v1(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_tentry *tent, *ptent;
	ipfw_obj_ctlv *ctlv;
	ipfw_obj_header *oh;
	struct tentry_info *ptei, tei, *tei_buf;
	struct tid_info ti;
	int error, i, kidx, read;

	/* Check minimum header size */
	if (sd->valsize < (sizeof(*oh) + sizeof(*ctlv)))
		return (EINVAL);

	/* Check if passed data is too long */
	if (sd->valsize != sd->kavail)
		return (EINVAL);

	oh = (ipfw_obj_header *)sd->kbuf;

	/* Basic length checks for TLVs */
	if (oh->ntlv.head.length != sizeof(oh->ntlv))
		return (EINVAL);

	read = sizeof(*oh);

	ctlv = (ipfw_obj_ctlv *)(oh + 1);
	if (ctlv->head.length + read != sd->valsize)
		return (EINVAL);

	read += sizeof(*ctlv);
	tent = (ipfw_obj_tentry *)(ctlv + 1);
	if (ctlv->count * sizeof(*tent) + read != sd->valsize)
		return (EINVAL);

	if (ctlv->count == 0)
		return (0);

	/*
	 * Mark entire buffer as "read".
	 * This instructs sopt api write it back
	 * after function return.
	 */
	ipfw_get_sopt_header(sd, sd->valsize);

	/* Perform basic checks for each entry */
	ptent = tent;
	kidx = tent->idx;
	for (i = 0; i < ctlv->count; i++, ptent++) {
		if (ptent->head.length != sizeof(*ptent))
			return (EINVAL);
		if (ptent->idx != kidx)
			return (ENOTSUP);
	}

	/* Convert data into kernel request objects */
	objheader_to_ti(oh, &ti);
	ti.type = oh->ntlv.type;
	ti.uidx = kidx;

	/* Use on-stack buffer for single add/del */
	if (ctlv->count == 1) {
		memset(&tei, 0, sizeof(tei));
		tei_buf = &tei;
	} else
		tei_buf = malloc(ctlv->count * sizeof(tei), M_TEMP,
		    M_WAITOK | M_ZERO);

	ptei = tei_buf;
	ptent = tent;
	for (i = 0; i < ctlv->count; i++, ptent++, ptei++) {
		ptei->paddr = &ptent->k;
		ptei->subtype = ptent->subtype;
		ptei->masklen = ptent->masklen;
		if (ptent->head.flags & IPFW_TF_UPDATE)
			ptei->flags |= TEI_FLAGS_UPDATE;

		ipfw_import_table_value_v1(&ptent->v.value);
		ptei->pvalue = (struct table_value *)&ptent->v.value;
	}

	error = (oh->opheader.opcode == IP_FW_TABLE_XADD) ?
	    add_table_entry(ch, &ti, tei_buf, ctlv->flags, ctlv->count) :
	    del_table_entry(ch, &ti, tei_buf, ctlv->flags, ctlv->count);

	/* Translate result back to userland */
	ptei = tei_buf;
	ptent = tent;
	for (i = 0; i < ctlv->count; i++, ptent++, ptei++) {
		if (ptei->flags & TEI_FLAGS_ADDED)
			ptent->result = IPFW_TR_ADDED;
		else if (ptei->flags & TEI_FLAGS_DELETED)
			ptent->result = IPFW_TR_DELETED;
		else if (ptei->flags & TEI_FLAGS_UPDATED)
			ptent->result = IPFW_TR_UPDATED;
		else if (ptei->flags & TEI_FLAGS_LIMIT)
			ptent->result = IPFW_TR_LIMIT;
		else if (ptei->flags & TEI_FLAGS_ERROR)
			ptent->result = IPFW_TR_ERROR;
		else if (ptei->flags & TEI_FLAGS_NOTFOUND)
			ptent->result = IPFW_TR_NOTFOUND;
		else if (ptei->flags & TEI_FLAGS_EXISTS)
			ptent->result = IPFW_TR_EXISTS;
		ipfw_export_table_value_v1(ptei->pvalue, &ptent->v.value);
	}

	if (tei_buf != &tei)
		free(tei_buf, M_TEMP);

	return (error);
}

/*
 * Looks up an entry in given table.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_obj_tentry ]
 * Reply: [ ipfw_obj_header ipfw_obj_tentry ]
 *
 * Returns 0 on success
 */
static int
find_table_entry(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_tentry *tent;
	ipfw_obj_header *oh;
	struct tid_info ti;
	struct table_config *tc;
	struct table_algo *ta;
	struct table_info *kti;
	struct table_value *pval;
	struct namedobj_instance *ni;
	int error;
	size_t sz;

	/* Check minimum header size */
	sz = sizeof(*oh) + sizeof(*tent);
	if (sd->valsize != sz)
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)ipfw_get_sopt_header(sd, sz);
	tent = (ipfw_obj_tentry *)(oh + 1);

	/* Basic length checks for TLVs */
	if (oh->ntlv.head.length != sizeof(oh->ntlv))
		return (EINVAL);

	objheader_to_ti(oh, &ti);
	ti.type = oh->ntlv.type;
	ti.uidx = tent->idx;

	IPFW_UH_RLOCK(ch);
	ni = CHAIN_TO_NI(ch);

	/*
	 * Find existing table and check its type .
	 */
	ta = NULL;
	if ((tc = find_table(ni, &ti)) == NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (ESRCH);
	}

	/* check table type */
	if (tc->no.subtype != ti.type) {
		IPFW_UH_RUNLOCK(ch);
		return (EINVAL);
	}

	kti = KIDX_TO_TI(ch, tc->no.kidx);
	ta = tc->ta;

	if (ta->find_tentry == NULL)
		return (ENOTSUP);

	error = ta->find_tentry(tc->astate, kti, tent);
	if (error == 0) {
		pval = get_table_value(ch, tc, tent->v.kidx);
		ipfw_export_table_value_v1(pval, &tent->v.value);
	}
	IPFW_UH_RUNLOCK(ch);

	return (error);
}

/*
 * Flushes all entries or destroys given table.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ]
 *
 * Returns 0 on success
 */
static int
flush_table_v0(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	int error;
	struct _ipfw_obj_header *oh;
	struct tid_info ti;

	if (sd->valsize != sizeof(*oh))
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)op3;
	objheader_to_ti(oh, &ti);

	if (op3->opcode == IP_FW_TABLE_XDESTROY)
		error = destroy_table(ch, &ti);
	else if (op3->opcode == IP_FW_TABLE_XFLUSH)
		error = flush_table(ch, &ti);
	else
		return (ENOTSUP);

	return (error);
}

static void
restart_flush(void *object, struct op_state *_state)
{
	struct tableop_state *ts;

	ts = (struct tableop_state *)_state;

	if (ts->tc != object)
		return;

	/* Indicate we've called */
	ts->modified = 1;
}

/*
 * Flushes given table.
 *
 * Function create new table instance with the same
 * parameters, swaps it with old one and
 * flushes state without holding runtime WLOCK.
 *
 * Returns 0 on success.
 */
int
flush_table(struct ip_fw_chain *ch, struct tid_info *ti)
{
	struct namedobj_instance *ni;
	struct table_config *tc;
	struct table_algo *ta;
	struct table_info ti_old, ti_new, *tablestate;
	void *astate_old, *astate_new;
	char algostate[64], *pstate;
	struct tableop_state ts;
	int error, need_gc;
	uint16_t kidx;
	uint8_t tflags;

	/*
	 * Stage 1: save table algorithm.
	 * Reference found table to ensure it won't disappear.
	 */
	IPFW_UH_WLOCK(ch);
	ni = CHAIN_TO_NI(ch);
	if ((tc = find_table(ni, ti)) == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}
	need_gc = 0;
	astate_new = NULL;
	memset(&ti_new, 0, sizeof(ti_new));
restart:
	/* Set up swap handler */
	memset(&ts, 0, sizeof(ts));
	ts.opstate.func = restart_flush;
	ts.tc = tc;

	ta = tc->ta;
	/* Do not flush readonly tables */
	if ((ta->flags & TA_FLAG_READONLY) != 0) {
		IPFW_UH_WUNLOCK(ch);
		return (EACCES);
	}
	/* Save startup algo parameters */
	if (ta->print_config != NULL) {
		ta->print_config(tc->astate, KIDX_TO_TI(ch, tc->no.kidx),
		    algostate, sizeof(algostate));
		pstate = algostate;
	} else
		pstate = NULL;
	tflags = tc->tflags;
	tc->no.refcnt++;
	add_toperation_state(ch, &ts);
	IPFW_UH_WUNLOCK(ch);

	/*
	 * Stage 1.5: if this is not the first attempt, destroy previous state
	 */
	if (need_gc != 0) {
		ta->destroy(astate_new, &ti_new);
		need_gc = 0;
	}

	/*
	 * Stage 2: allocate new table instance using same algo.
	 */
	memset(&ti_new, 0, sizeof(struct table_info));
	error = ta->init(ch, &astate_new, &ti_new, pstate, tflags);

	/*
	 * Stage 3: swap old state pointers with newly-allocated ones.
	 * Decrease refcount.
	 */
	IPFW_UH_WLOCK(ch);
	tc->no.refcnt--;
	del_toperation_state(ch, &ts);

	if (error != 0) {
		IPFW_UH_WUNLOCK(ch);
		return (error);
	}

	/*
	 * Restart operation if table swap has happened:
	 * even if algo may be the same, algo init parameters
	 * may change. Restart operation instead of doing
	 * complex checks.
	 */
	if (ts.modified != 0) {
		/* Delay destroying data since we're holding UH lock */
		need_gc = 1;
		goto restart;
	}

	ni = CHAIN_TO_NI(ch);
	kidx = tc->no.kidx;
	tablestate = (struct table_info *)ch->tablestate;

	IPFW_WLOCK(ch);
	ti_old = tablestate[kidx];
	tablestate[kidx] = ti_new;
	IPFW_WUNLOCK(ch);

	astate_old = tc->astate;
	tc->astate = astate_new;
	tc->ti_copy = ti_new;
	tc->count = 0;

	/* Notify algo on real @ti address */
	if (ta->change_ti != NULL)
		ta->change_ti(tc->astate, &tablestate[kidx]);

	/*
	 * Stage 4: unref values.
	 */
	ipfw_unref_table_values(ch, tc, ta, astate_old, &ti_old);
	IPFW_UH_WUNLOCK(ch);

	/*
	 * Stage 5: perform real flush/destroy.
	 */
	ta->destroy(astate_old, &ti_old);

	return (0);
}

/*
 * Swaps two tables.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_obj_ntlv ]
 *
 * Returns 0 on success
 */
static int
swap_table(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	int error;
	struct _ipfw_obj_header *oh;
	struct tid_info ti_a, ti_b;

	if (sd->valsize != sizeof(*oh) + sizeof(ipfw_obj_ntlv))
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)op3;
	ntlv_to_ti(&oh->ntlv, &ti_a);
	ntlv_to_ti((ipfw_obj_ntlv *)(oh + 1), &ti_b);

	error = swap_tables(ch, &ti_a, &ti_b);

	return (error);
}

/*
 * Swaps two tables of the same type/valtype.
 *
 * Checks if tables are compatible and limits
 * permits swap, than actually perform swap.
 *
 * Each table consists of 2 different parts:
 * config:
 *   @tc (with name, set, kidx) and rule bindings, which is "stable".
 *   number of items
 *   table algo
 * runtime:
 *   runtime data @ti (ch->tablestate)
 *   runtime cache in @tc
 *   algo-specific data (@tc->astate)
 *
 * So we switch:
 *  all runtime data
 *   number of items
 *   table algo
 *
 * After that we call @ti change handler for each table.
 *
 * Note that referencing @tc won't protect tc->ta from change.
 * XXX: Do we need to restrict swap between locked tables?
 * XXX: Do we need to exchange ftype?
 *
 * Returns 0 on success.
 */
static int
swap_tables(struct ip_fw_chain *ch, struct tid_info *a,
    struct tid_info *b)
{
	struct namedobj_instance *ni;
	struct table_config *tc_a, *tc_b;
	struct table_algo *ta;
	struct table_info ti, *tablestate;
	void *astate;
	uint32_t count;

	/*
	 * Stage 1: find both tables and ensure they are of
	 * the same type.
	 */
	IPFW_UH_WLOCK(ch);
	ni = CHAIN_TO_NI(ch);
	if ((tc_a = find_table(ni, a)) == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}
	if ((tc_b = find_table(ni, b)) == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}

	/* It is very easy to swap between the same table */
	if (tc_a == tc_b) {
		IPFW_UH_WUNLOCK(ch);
		return (0);
	}

	/* Check type and value are the same */
	if (tc_a->no.subtype!=tc_b->no.subtype || tc_a->tflags!=tc_b->tflags) {
		IPFW_UH_WUNLOCK(ch);
		return (EINVAL);
	}

	/* Check limits before swap */
	if ((tc_a->limit != 0 && tc_b->count > tc_a->limit) ||
	    (tc_b->limit != 0 && tc_a->count > tc_b->limit)) {
		IPFW_UH_WUNLOCK(ch);
		return (EFBIG);
	}

	/* Check if one of the tables is readonly */
	if (((tc_a->ta->flags | tc_b->ta->flags) & TA_FLAG_READONLY) != 0) {
		IPFW_UH_WUNLOCK(ch);
		return (EACCES);
	}

	/* Notify we're going to swap */
	rollback_toperation_state(ch, tc_a);
	rollback_toperation_state(ch, tc_b);

	/* Everything is fine, prepare to swap */
	tablestate = (struct table_info *)ch->tablestate;
	ti = tablestate[tc_a->no.kidx];
	ta = tc_a->ta;
	astate = tc_a->astate;
	count = tc_a->count;

	IPFW_WLOCK(ch);
	/* a <- b */
	tablestate[tc_a->no.kidx] = tablestate[tc_b->no.kidx];
	tc_a->ta = tc_b->ta;
	tc_a->astate = tc_b->astate;
	tc_a->count = tc_b->count;
	/* b <- a */
	tablestate[tc_b->no.kidx] = ti;
	tc_b->ta = ta;
	tc_b->astate = astate;
	tc_b->count = count;
	IPFW_WUNLOCK(ch);

	/* Ensure tc.ti copies are in sync */
	tc_a->ti_copy = tablestate[tc_a->no.kidx];
	tc_b->ti_copy = tablestate[tc_b->no.kidx];

	/* Notify both tables on @ti change */
	if (tc_a->ta->change_ti != NULL)
		tc_a->ta->change_ti(tc_a->astate, &tablestate[tc_a->no.kidx]);
	if (tc_b->ta->change_ti != NULL)
		tc_b->ta->change_ti(tc_b->astate, &tablestate[tc_b->no.kidx]);

	IPFW_UH_WUNLOCK(ch);

	return (0);
}

/*
 * Destroys table specified by @ti.
 * Data layout (v0)(current):
 * Request: [ ip_fw3_opheader ]
 *
 * Returns 0 on success
 */
static int
destroy_table(struct ip_fw_chain *ch, struct tid_info *ti)
{
	struct namedobj_instance *ni;
	struct table_config *tc;

	IPFW_UH_WLOCK(ch);

	ni = CHAIN_TO_NI(ch);
	if ((tc = find_table(ni, ti)) == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}

	/* Do not permit destroying referenced tables */
	if (tc->no.refcnt > 0) {
		IPFW_UH_WUNLOCK(ch);
		return (EBUSY);
	}

	IPFW_WLOCK(ch);
	unlink_table(ch, tc);
	IPFW_WUNLOCK(ch);

	/* Free obj index */
	if (ipfw_objhash_free_idx(ni, tc->no.kidx) != 0)
		printf("Error unlinking kidx %d from table %s\n",
		    tc->no.kidx, tc->tablename);

	/* Unref values used in tables while holding UH lock */
	ipfw_unref_table_values(ch, tc, tc->ta, tc->astate, &tc->ti_copy);
	IPFW_UH_WUNLOCK(ch);

	free_table_config(ni, tc);

	return (0);
}

static uint32_t
roundup2p(uint32_t v)
{

	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;

	return (v);
}

/*
 * Grow tables index.
 *
 * Returns 0 on success.
 */
int
ipfw_resize_tables(struct ip_fw_chain *ch, unsigned int ntables)
{
	unsigned int ntables_old, tbl;
	struct namedobj_instance *ni;
	void *new_idx, *old_tablestate, *tablestate;
	struct table_info *ti;
	struct table_config *tc;
	int i, new_blocks;

	/* Check new value for validity */
	if (ntables == 0)
		return (EINVAL);
	if (ntables > IPFW_TABLES_MAX)
		ntables = IPFW_TABLES_MAX;
	/* Alight to nearest power of 2 */
	ntables = (unsigned int)roundup2p(ntables); 

	/* Allocate new pointers */
	tablestate = malloc(ntables * sizeof(struct table_info),
	    M_IPFW, M_WAITOK | M_ZERO);

	ipfw_objhash_bitmap_alloc(ntables, (void *)&new_idx, &new_blocks);

	IPFW_UH_WLOCK(ch);

	tbl = (ntables >= V_fw_tables_max) ? V_fw_tables_max : ntables;
	ni = CHAIN_TO_NI(ch);

	/* Temporary restrict decreasing max_tables */
	if (ntables < V_fw_tables_max) {

		/*
		 * FIXME: Check if we really can shrink
		 */
		IPFW_UH_WUNLOCK(ch);
		return (EINVAL);
	}

	/* Copy table info/indices */
	memcpy(tablestate, ch->tablestate, sizeof(struct table_info) * tbl);
	ipfw_objhash_bitmap_merge(ni, &new_idx, &new_blocks);

	IPFW_WLOCK(ch);

	/* Change pointers */
	old_tablestate = ch->tablestate;
	ch->tablestate = tablestate;
	ipfw_objhash_bitmap_swap(ni, &new_idx, &new_blocks);

	ntables_old = V_fw_tables_max;
	V_fw_tables_max = ntables;

	IPFW_WUNLOCK(ch);

	/* Notify all consumers that their @ti pointer has changed */
	ti = (struct table_info *)ch->tablestate;
	for (i = 0; i < tbl; i++, ti++) {
		if (ti->lookup == NULL)
			continue;
		tc = (struct table_config *)ipfw_objhash_lookup_kidx(ni, i);
		if (tc == NULL || tc->ta->change_ti == NULL)
			continue;

		tc->ta->change_ti(tc->astate, ti);
	}

	IPFW_UH_WUNLOCK(ch);

	/* Free old pointers */
	free(old_tablestate, M_IPFW);
	ipfw_objhash_bitmap_free(new_idx, new_blocks);

	return (0);
}

/*
 * Lookup table's named object by its @kidx.
 */
struct named_object *
ipfw_objhash_lookup_table_kidx(struct ip_fw_chain *ch, uint16_t kidx)
{

	return (ipfw_objhash_lookup_kidx(CHAIN_TO_NI(ch), kidx));
}

/*
 * Take reference to table specified in @ntlv.
 * On success return its @kidx.
 */
int
ipfw_ref_table(struct ip_fw_chain *ch, ipfw_obj_ntlv *ntlv, uint16_t *kidx)
{
	struct tid_info ti;
	struct table_config *tc;
	int error;

	IPFW_UH_WLOCK_ASSERT(ch);

	ntlv_to_ti(ntlv, &ti);
	error = find_table_err(CHAIN_TO_NI(ch), &ti, &tc);
	if (error != 0)
		return (error);

	if (tc == NULL)
		return (ESRCH);

	tc_ref(tc);
	*kidx = tc->no.kidx;

	return (0);
}

void
ipfw_unref_table(struct ip_fw_chain *ch, uint16_t kidx)
{

	struct namedobj_instance *ni;
	struct named_object *no;

	IPFW_UH_WLOCK_ASSERT(ch);
	ni = CHAIN_TO_NI(ch);
	no = ipfw_objhash_lookup_kidx(ni, kidx);
	KASSERT(no != NULL, ("Table with index %d not found", kidx));
	no->refcnt--;
}

/*
 * Lookup an arbitrary key @paddr of length @plen in table @tbl.
 * Stores found value in @val.
 *
 * Returns 1 if key was found.
 */
int
ipfw_lookup_table(struct ip_fw_chain *ch, uint16_t tbl, uint16_t plen,
    void *paddr, uint32_t *val)
{
	struct table_info *ti;

	ti = KIDX_TO_TI(ch, tbl);

	return (ti->lookup(ti, paddr, plen, val));
}

/*
 * Info/List/dump support for tables.
 *
 */

/*
 * High-level 'get' cmds sysctl handlers
 */

/*
 * Lists all tables currently available in kernel.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_lheader ], size = ipfw_obj_lheader.size
 * Reply: [ ipfw_obj_lheader ipfw_xtable_info x N ]
 *
 * Returns 0 on success
 */
static int
list_tables(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct _ipfw_obj_lheader *olh;
	int error;

	olh = (struct _ipfw_obj_lheader *)ipfw_get_sopt_header(sd,sizeof(*olh));
	if (olh == NULL)
		return (EINVAL);
	if (sd->valsize < olh->size)
		return (EINVAL);

	IPFW_UH_RLOCK(ch);
	error = export_tables(ch, olh, sd);
	IPFW_UH_RUNLOCK(ch);

	return (error);
}

/*
 * Store table info to buffer provided by @sd.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_xtable_info(empty)]
 * Reply: [ ipfw_obj_header ipfw_xtable_info ]
 *
 * Returns 0 on success.
 */
static int
describe_table(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct _ipfw_obj_header *oh;
	struct table_config *tc;
	struct tid_info ti;
	size_t sz;

	sz = sizeof(*oh) + sizeof(ipfw_xtable_info);
	oh = (struct _ipfw_obj_header *)ipfw_get_sopt_header(sd, sz);
	if (oh == NULL)
		return (EINVAL);

	objheader_to_ti(oh, &ti);

	IPFW_UH_RLOCK(ch);
	if ((tc = find_table(CHAIN_TO_NI(ch), &ti)) == NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (ESRCH);
	}

	export_table_info(ch, tc, (ipfw_xtable_info *)(oh + 1));
	IPFW_UH_RUNLOCK(ch);

	return (0);
}

/*
 * Modifies existing table.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_xtable_info ]
 *
 * Returns 0 on success
 */
static int
modify_table(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct _ipfw_obj_header *oh;
	ipfw_xtable_info *i;
	char *tname;
	struct tid_info ti;
	struct namedobj_instance *ni;
	struct table_config *tc;

	if (sd->valsize != sizeof(*oh) + sizeof(ipfw_xtable_info))
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)sd->kbuf;
	i = (ipfw_xtable_info *)(oh + 1);

	/*
	 * Verify user-supplied strings.
	 * Check for null-terminated/zero-length strings/
	 */
	tname = oh->ntlv.name;
	if (check_table_name(tname) != 0)
		return (EINVAL);

	objheader_to_ti(oh, &ti);
	ti.type = i->type;

	IPFW_UH_WLOCK(ch);
	ni = CHAIN_TO_NI(ch);
	if ((tc = find_table(ni, &ti)) == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ESRCH);
	}

	/* Do not support any modifications for readonly tables */
	if ((tc->ta->flags & TA_FLAG_READONLY) != 0) {
		IPFW_UH_WUNLOCK(ch);
		return (EACCES);
	}

	if ((i->mflags & IPFW_TMFLAGS_LIMIT) != 0)
		tc->limit = i->limit;
	if ((i->mflags & IPFW_TMFLAGS_LOCK) != 0)
		tc->locked = ((i->flags & IPFW_TGFLAGS_LOCKED) != 0);
	IPFW_UH_WUNLOCK(ch);

	return (0);
}

/*
 * Creates new table.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_xtable_info ]
 *
 * Returns 0 on success
 */
static int
create_table(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct _ipfw_obj_header *oh;
	ipfw_xtable_info *i;
	char *tname, *aname;
	struct tid_info ti;
	struct namedobj_instance *ni;

	if (sd->valsize != sizeof(*oh) + sizeof(ipfw_xtable_info))
		return (EINVAL);

	oh = (struct _ipfw_obj_header *)sd->kbuf;
	i = (ipfw_xtable_info *)(oh + 1);

	/*
	 * Verify user-supplied strings.
	 * Check for null-terminated/zero-length strings/
	 */
	tname = oh->ntlv.name;
	aname = i->algoname;
	if (check_table_name(tname) != 0 ||
	    strnlen(aname, sizeof(i->algoname)) == sizeof(i->algoname))
		return (EINVAL);

	if (aname[0] == '\0') {
		/* Use default algorithm */
		aname = NULL;
	}

	objheader_to_ti(oh, &ti);
	ti.type = i->type;

	ni = CHAIN_TO_NI(ch);

	IPFW_UH_RLOCK(ch);
	if (find_table(ni, &ti) != NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (EEXIST);
	}
	IPFW_UH_RUNLOCK(ch);

	return (create_table_internal(ch, &ti, aname, i, NULL, 0));
}

/*
 * Creates new table based on @ti and @aname.
 *
 * Assume @aname to be checked and valid.
 * Stores allocated table kidx inside @pkidx (if non-NULL).
 * Reference created table if @compat is non-zero.
 *
 * Returns 0 on success.
 */
static int
create_table_internal(struct ip_fw_chain *ch, struct tid_info *ti,
    char *aname, ipfw_xtable_info *i, uint16_t *pkidx, int compat)
{
	struct namedobj_instance *ni;
	struct table_config *tc, *tc_new, *tmp;
	struct table_algo *ta;
	uint16_t kidx;

	ni = CHAIN_TO_NI(ch);

	ta = find_table_algo(CHAIN_TO_TCFG(ch), ti, aname);
	if (ta == NULL)
		return (ENOTSUP);

	tc = alloc_table_config(ch, ti, ta, aname, i->tflags);
	if (tc == NULL)
		return (ENOMEM);

	tc->vmask = i->vmask;
	tc->limit = i->limit;
	if (ta->flags & TA_FLAG_READONLY)
		tc->locked = 1;
	else
		tc->locked = (i->flags & IPFW_TGFLAGS_LOCKED) != 0;

	IPFW_UH_WLOCK(ch);

	/* Check if table has been already created */
	tc_new = find_table(ni, ti);
	if (tc_new != NULL) {

		/*
		 * Compat: do not fail if we're
		 * requesting to create existing table
		 * which has the same type
		 */
		if (compat == 0 || tc_new->no.subtype != tc->no.subtype) {
			IPFW_UH_WUNLOCK(ch);
			free_table_config(ni, tc);
			return (EEXIST);
		}

		/* Exchange tc and tc_new for proper refcounting & freeing */
		tmp = tc;
		tc = tc_new;
		tc_new = tmp;
	} else {
		/* New table */
		if (ipfw_objhash_alloc_idx(ni, &kidx) != 0) {
			IPFW_UH_WUNLOCK(ch);
			printf("Unable to allocate table index."
			    " Consider increasing net.inet.ip.fw.tables_max");
			free_table_config(ni, tc);
			return (EBUSY);
		}
		tc->no.kidx = kidx;
		tc->no.etlv = IPFW_TLV_TBL_NAME;

		link_table(ch, tc);
	}

	if (compat != 0)
		tc->no.refcnt++;
	if (pkidx != NULL)
		*pkidx = tc->no.kidx;

	IPFW_UH_WUNLOCK(ch);

	if (tc_new != NULL)
		free_table_config(ni, tc_new);

	return (0);
}

static void
ntlv_to_ti(ipfw_obj_ntlv *ntlv, struct tid_info *ti)
{

	memset(ti, 0, sizeof(struct tid_info));
	ti->set = ntlv->set;
	ti->uidx = ntlv->idx;
	ti->tlvs = ntlv;
	ti->tlen = ntlv->head.length;
}

static void
objheader_to_ti(struct _ipfw_obj_header *oh, struct tid_info *ti)
{

	ntlv_to_ti(&oh->ntlv, ti);
}

struct namedobj_instance *
ipfw_get_table_objhash(struct ip_fw_chain *ch)
{

	return (CHAIN_TO_NI(ch));
}

/*
 * Exports basic table info as name TLV.
 * Used inside dump_static_rules() to provide info
 * about all tables referenced by current ruleset.
 *
 * Returns 0 on success.
 */
int
ipfw_export_table_ntlv(struct ip_fw_chain *ch, uint16_t kidx,
    struct sockopt_data *sd)
{
	struct namedobj_instance *ni;
	struct named_object *no;
	ipfw_obj_ntlv *ntlv;

	ni = CHAIN_TO_NI(ch);

	no = ipfw_objhash_lookup_kidx(ni, kidx);
	KASSERT(no != NULL, ("invalid table kidx passed"));

	ntlv = (ipfw_obj_ntlv *)ipfw_get_sopt_space(sd, sizeof(*ntlv));
	if (ntlv == NULL)
		return (ENOMEM);

	ntlv->head.type = IPFW_TLV_TBL_NAME;
	ntlv->head.length = sizeof(*ntlv);
	ntlv->idx = no->kidx;
	strlcpy(ntlv->name, no->name, sizeof(ntlv->name));

	return (0);
}

struct dump_args {
	struct ip_fw_chain *ch;
	struct table_info *ti;
	struct table_config *tc;
	struct sockopt_data *sd;
	uint32_t cnt;
	uint16_t uidx;
	int error;
	uint32_t size;
	ipfw_table_entry *ent;
	ta_foreach_f *f;
	void *farg;
	ipfw_obj_tentry tent;
};

static int
count_ext_entries(void *e, void *arg)
{
	struct dump_args *da;

	da = (struct dump_args *)arg;
	da->cnt++;

	return (0);
}

/*
 * Gets number of items from table either using
 * internal counter or calling algo callback for
 * externally-managed tables.
 *
 * Returns number of records.
 */
static uint32_t
table_get_count(struct ip_fw_chain *ch, struct table_config *tc)
{
	struct table_info *ti;
	struct table_algo *ta;
	struct dump_args da;

	ti = KIDX_TO_TI(ch, tc->no.kidx);
	ta = tc->ta;

	/* Use internal counter for self-managed tables */
	if ((ta->flags & TA_FLAG_READONLY) == 0)
		return (tc->count);

	/* Use callback to quickly get number of items */
	if ((ta->flags & TA_FLAG_EXTCOUNTER) != 0)
		return (ta->get_count(tc->astate, ti));

	/* Count number of iterms ourselves */
	memset(&da, 0, sizeof(da));
	ta->foreach(tc->astate, ti, count_ext_entries, &da);

	return (da.cnt);
}

/*
 * Exports table @tc info into standard ipfw_xtable_info format.
 */
static void
export_table_info(struct ip_fw_chain *ch, struct table_config *tc,
    ipfw_xtable_info *i)
{
	struct table_info *ti;
	struct table_algo *ta;
	
	i->type = tc->no.subtype;
	i->tflags = tc->tflags;
	i->vmask = tc->vmask;
	i->set = tc->no.set;
	i->kidx = tc->no.kidx;
	i->refcnt = tc->no.refcnt;
	i->count = table_get_count(ch, tc);
	i->limit = tc->limit;
	i->flags |= (tc->locked != 0) ? IPFW_TGFLAGS_LOCKED : 0;
	i->size = i->count * sizeof(ipfw_obj_tentry);
	i->size += sizeof(ipfw_obj_header) + sizeof(ipfw_xtable_info);
	strlcpy(i->tablename, tc->tablename, sizeof(i->tablename));
	ti = KIDX_TO_TI(ch, tc->no.kidx);
	ta = tc->ta;
	if (ta->print_config != NULL) {
		/* Use algo function to print table config to string */
		ta->print_config(tc->astate, ti, i->algoname,
		    sizeof(i->algoname));
	} else
		strlcpy(i->algoname, ta->name, sizeof(i->algoname));
	/* Dump algo-specific data, if possible */
	if (ta->dump_tinfo != NULL) {
		ta->dump_tinfo(tc->astate, ti, &i->ta_info);
		i->ta_info.flags |= IPFW_TATFLAGS_DATA;
	}
}

struct dump_table_args {
	struct ip_fw_chain *ch;
	struct sockopt_data *sd;
};

static int
export_table_internal(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{
	ipfw_xtable_info *i;
	struct dump_table_args *dta;

	dta = (struct dump_table_args *)arg;

	i = (ipfw_xtable_info *)ipfw_get_sopt_space(dta->sd, sizeof(*i));
	KASSERT(i != NULL, ("previously checked buffer is not enough"));

	export_table_info(dta->ch, (struct table_config *)no, i);
	return (0);
}

/*
 * Export all tables as ipfw_xtable_info structures to
 * storage provided by @sd.
 *
 * If supplied buffer is too small, fills in required size
 * and returns ENOMEM.
 * Returns 0 on success.
 */
static int
export_tables(struct ip_fw_chain *ch, ipfw_obj_lheader *olh,
    struct sockopt_data *sd)
{
	uint32_t size;
	uint32_t count;
	struct dump_table_args dta;

	count = ipfw_objhash_count(CHAIN_TO_NI(ch));
	size = count * sizeof(ipfw_xtable_info) + sizeof(ipfw_obj_lheader);

	/* Fill in header regadless of buffer size */
	olh->count = count;
	olh->objsize = sizeof(ipfw_xtable_info);

	if (size > olh->size) {
		olh->size = size;
		return (ENOMEM);
	}

	olh->size = size;

	dta.ch = ch;
	dta.sd = sd;

	ipfw_objhash_foreach(CHAIN_TO_NI(ch), export_table_internal, &dta);

	return (0);
}

/*
 * Dumps all table data
 * Data layout (v1)(current):
 * Request: [ ipfw_obj_header ], size = ipfw_xtable_info.size
 * Reply: [ ipfw_obj_header ipfw_xtable_info ipfw_obj_tentry x N ]
 *
 * Returns 0 on success
 */
static int
dump_table_v1(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct _ipfw_obj_header *oh;
	ipfw_xtable_info *i;
	struct tid_info ti;
	struct table_config *tc;
	struct table_algo *ta;
	struct dump_args da;
	uint32_t sz;

	sz = sizeof(ipfw_obj_header) + sizeof(ipfw_xtable_info);
	oh = (struct _ipfw_obj_header *)ipfw_get_sopt_header(sd, sz);
	if (oh == NULL)
		return (EINVAL);

	i = (ipfw_xtable_info *)(oh + 1);
	objheader_to_ti(oh, &ti);

	IPFW_UH_RLOCK(ch);
	if ((tc = find_table(CHAIN_TO_NI(ch), &ti)) == NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (ESRCH);
	}
	export_table_info(ch, tc, i);

	if (sd->valsize < i->size) {

		/*
		 * Submitted buffer size is not enough.
		 * WE've already filled in @i structure with
		 * relevant table info including size, so we
		 * can return. Buffer will be flushed automatically.
		 */
		IPFW_UH_RUNLOCK(ch);
		return (ENOMEM);
	}

	/*
	 * Do the actual dump in eXtended format
	 */
	memset(&da, 0, sizeof(da));
	da.ch = ch;
	da.ti = KIDX_TO_TI(ch, tc->no.kidx);
	da.tc = tc;
	da.sd = sd;

	ta = tc->ta;

	ta->foreach(tc->astate, da.ti, dump_table_tentry, &da);
	IPFW_UH_RUNLOCK(ch);

	return (da.error);
}

/*
 * Dumps all table data
 * Data layout (version 0)(legacy):
 * Request: [ ipfw_xtable ], size = IP_FW_TABLE_XGETSIZE()
 * Reply: [ ipfw_xtable ipfw_table_xentry x N ]
 *
 * Returns 0 on success
 */
static int
dump_table_v0(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_xtable *xtbl;
	struct tid_info ti;
	struct table_config *tc;
	struct table_algo *ta;
	struct dump_args da;
	size_t sz, count;

	xtbl = (ipfw_xtable *)ipfw_get_sopt_header(sd, sizeof(ipfw_xtable));
	if (xtbl == NULL)
		return (EINVAL);

	memset(&ti, 0, sizeof(ti));
	ti.uidx = xtbl->tbl;
	
	IPFW_UH_RLOCK(ch);
	if ((tc = find_table(CHAIN_TO_NI(ch), &ti)) == NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (0);
	}
	count = table_get_count(ch, tc);
	sz = count * sizeof(ipfw_table_xentry) + sizeof(ipfw_xtable);

	xtbl->cnt = count;
	xtbl->size = sz;
	xtbl->type = tc->no.subtype;
	xtbl->tbl = ti.uidx;

	if (sd->valsize < sz) {

		/*
		 * Submitted buffer size is not enough.
		 * WE've already filled in @i structure with
		 * relevant table info including size, so we
		 * can return. Buffer will be flushed automatically.
		 */
		IPFW_UH_RUNLOCK(ch);
		return (ENOMEM);
	}

	/* Do the actual dump in eXtended format */
	memset(&da, 0, sizeof(da));
	da.ch = ch;
	da.ti = KIDX_TO_TI(ch, tc->no.kidx);
	da.tc = tc;
	da.sd = sd;

	ta = tc->ta;

	ta->foreach(tc->astate, da.ti, dump_table_xentry, &da);
	IPFW_UH_RUNLOCK(ch);

	return (0);
}

/*
 * Legacy function to retrieve number of items in table.
 */
static int
get_table_size(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	uint32_t *tbl;
	struct tid_info ti;
	size_t sz;
	int error;

	sz = sizeof(*op3) + sizeof(uint32_t);
	op3 = (ip_fw3_opheader *)ipfw_get_sopt_header(sd, sz);
	if (op3 == NULL)
		return (EINVAL);

	tbl = (uint32_t *)(op3 + 1);
	memset(&ti, 0, sizeof(ti));
	ti.uidx = *tbl;
	IPFW_UH_RLOCK(ch);
	error = ipfw_count_xtable(ch, &ti, tbl);
	IPFW_UH_RUNLOCK(ch);
	return (error);
}

/*
 * Legacy IP_FW_TABLE_GETSIZE handler
 */
int
ipfw_count_table(struct ip_fw_chain *ch, struct tid_info *ti, uint32_t *cnt)
{
	struct table_config *tc;

	if ((tc = find_table(CHAIN_TO_NI(ch), ti)) == NULL)
		return (ESRCH);
	*cnt = table_get_count(ch, tc);
	return (0);
}

/*
 * Legacy IP_FW_TABLE_XGETSIZE handler
 */
int
ipfw_count_xtable(struct ip_fw_chain *ch, struct tid_info *ti, uint32_t *cnt)
{
	struct table_config *tc;
	uint32_t count;

	if ((tc = find_table(CHAIN_TO_NI(ch), ti)) == NULL) {
		*cnt = 0;
		return (0); /* 'table all list' requires success */
	}

	count = table_get_count(ch, tc);
	*cnt = count * sizeof(ipfw_table_xentry);
	if (count > 0)
		*cnt += sizeof(ipfw_xtable);
	return (0);
}

static int
dump_table_entry(void *e, void *arg)
{
	struct dump_args *da;
	struct table_config *tc;
	struct table_algo *ta;
	ipfw_table_entry *ent;
	struct table_value *pval;
	int error;

	da = (struct dump_args *)arg;

	tc = da->tc;
	ta = tc->ta;

	/* Out of memory, returning */
	if (da->cnt == da->size)
		return (1);
	ent = da->ent++;
	ent->tbl = da->uidx;
	da->cnt++;

	error = ta->dump_tentry(tc->astate, da->ti, e, &da->tent);
	if (error != 0)
		return (error);

	ent->addr = da->tent.k.addr.s_addr;
	ent->masklen = da->tent.masklen;
	pval = get_table_value(da->ch, da->tc, da->tent.v.kidx);
	ent->value = ipfw_export_table_value_legacy(pval);

	return (0);
}

/*
 * Dumps table in pre-8.1 legacy format.
 */
int
ipfw_dump_table_legacy(struct ip_fw_chain *ch, struct tid_info *ti,
    ipfw_table *tbl)
{
	struct table_config *tc;
	struct table_algo *ta;
	struct dump_args da;

	tbl->cnt = 0;

	if ((tc = find_table(CHAIN_TO_NI(ch), ti)) == NULL)
		return (0);	/* XXX: We should return ESRCH */

	ta = tc->ta;

	/* This dump format supports IPv4 only */
	if (tc->no.subtype != IPFW_TABLE_ADDR)
		return (0);

	memset(&da, 0, sizeof(da));
	da.ch = ch;
	da.ti = KIDX_TO_TI(ch, tc->no.kidx);
	da.tc = tc;
	da.ent = &tbl->ent[0];
	da.size = tbl->size;

	tbl->cnt = 0;
	ta->foreach(tc->astate, da.ti, dump_table_entry, &da);
	tbl->cnt = da.cnt;

	return (0);
}

/*
 * Dumps table entry in eXtended format (v1)(current).
 */
static int
dump_table_tentry(void *e, void *arg)
{
	struct dump_args *da;
	struct table_config *tc;
	struct table_algo *ta;
	struct table_value *pval;
	ipfw_obj_tentry *tent;
	int error;

	da = (struct dump_args *)arg;

	tc = da->tc;
	ta = tc->ta;

	tent = (ipfw_obj_tentry *)ipfw_get_sopt_space(da->sd, sizeof(*tent));
	/* Out of memory, returning */
	if (tent == NULL) {
		da->error = ENOMEM;
		return (1);
	}
	tent->head.length = sizeof(ipfw_obj_tentry);
	tent->idx = da->uidx;

	error = ta->dump_tentry(tc->astate, da->ti, e, tent);
	if (error != 0)
		return (error);

	pval = get_table_value(da->ch, da->tc, tent->v.kidx);
	ipfw_export_table_value_v1(pval, &tent->v.value);

	return (0);
}

/*
 * Dumps table entry in eXtended format (v0).
 */
static int
dump_table_xentry(void *e, void *arg)
{
	struct dump_args *da;
	struct table_config *tc;
	struct table_algo *ta;
	ipfw_table_xentry *xent;
	ipfw_obj_tentry *tent;
	struct table_value *pval;
	int error;

	da = (struct dump_args *)arg;

	tc = da->tc;
	ta = tc->ta;

	xent = (ipfw_table_xentry *)ipfw_get_sopt_space(da->sd, sizeof(*xent));
	/* Out of memory, returning */
	if (xent == NULL)
		return (1);
	xent->len = sizeof(ipfw_table_xentry);
	xent->tbl = da->uidx;

	memset(&da->tent, 0, sizeof(da->tent));
	tent = &da->tent;
	error = ta->dump_tentry(tc->astate, da->ti, e, tent);
	if (error != 0)
		return (error);

	/* Convert current format to previous one */
	xent->masklen = tent->masklen;
	pval = get_table_value(da->ch, da->tc, da->tent.v.kidx);
	xent->value = ipfw_export_table_value_legacy(pval);
	/* Apply some hacks */
	if (tc->no.subtype == IPFW_TABLE_ADDR && tent->subtype == AF_INET) {
		xent->k.addr6.s6_addr32[3] = tent->k.addr.s_addr;
		xent->flags = IPFW_TCF_INET;
	} else
		memcpy(&xent->k, &tent->k, sizeof(xent->k));

	return (0);
}

/*
 * Helper function to export table algo data
 * to tentry format before calling user function.
 *
 * Returns 0 on success.
 */
static int
prepare_table_tentry(void *e, void *arg)
{
	struct dump_args *da;
	struct table_config *tc;
	struct table_algo *ta;
	int error;

	da = (struct dump_args *)arg;

	tc = da->tc;
	ta = tc->ta;

	error = ta->dump_tentry(tc->astate, da->ti, e, &da->tent);
	if (error != 0)
		return (error);

	da->f(&da->tent, da->farg);

	return (0);
}

/*
 * Allow external consumers to read table entries in standard format.
 */
int
ipfw_foreach_table_tentry(struct ip_fw_chain *ch, uint16_t kidx,
    ta_foreach_f *f, void *arg)
{
	struct namedobj_instance *ni;
	struct table_config *tc;
	struct table_algo *ta;
	struct dump_args da;

	ni = CHAIN_TO_NI(ch);

	tc = (struct table_config *)ipfw_objhash_lookup_kidx(ni, kidx);
	if (tc == NULL)
		return (ESRCH);

	ta = tc->ta;

	memset(&da, 0, sizeof(da));
	da.ch = ch;
	da.ti = KIDX_TO_TI(ch, tc->no.kidx);
	da.tc = tc;
	da.f = f;
	da.farg = arg;

	ta->foreach(tc->astate, da.ti, prepare_table_tentry, &da);

	return (0);
}

/*
 * Table algorithms
 */ 

/*
 * Finds algorithm by index, table type or supplied name.
 *
 * Returns pointer to algo or NULL.
 */
static struct table_algo *
find_table_algo(struct tables_config *tcfg, struct tid_info *ti, char *name)
{
	int i, l;
	struct table_algo *ta;

	if (ti->type > IPFW_TABLE_MAXTYPE)
		return (NULL);

	/* Search by index */
	if (ti->atype != 0) {
		if (ti->atype > tcfg->algo_count)
			return (NULL);
		return (tcfg->algo[ti->atype]);
	}

	if (name == NULL) {
		/* Return default algorithm for given type if set */
		return (tcfg->def_algo[ti->type]);
	}

	/* Search by name */
	/* TODO: better search */
	for (i = 1; i <= tcfg->algo_count; i++) {
		ta = tcfg->algo[i];

		/*
		 * One can supply additional algorithm
		 * parameters so we compare only the first word
		 * of supplied name:
		 * 'addr:chash hsize=32'
		 * '^^^^^^^^^'
		 *
		 */
		l = strlen(ta->name);
		if (strncmp(name, ta->name, l) != 0)
			continue;
		if (name[l] != '\0' && name[l] != ' ')
			continue;
		/* Check if we're requesting proper table type */
		if (ti->type != 0 && ti->type != ta->type)
			return (NULL);
		return (ta);
	}

	return (NULL);
}

/*
 * Register new table algo @ta.
 * Stores algo id inside @idx.
 *
 * Returns 0 on success.
 */
int
ipfw_add_table_algo(struct ip_fw_chain *ch, struct table_algo *ta, size_t size,
    int *idx)
{
	struct tables_config *tcfg;
	struct table_algo *ta_new;
	size_t sz;

	if (size > sizeof(struct table_algo))
		return (EINVAL);

	/* Check for the required on-stack size for add/del */
	sz = roundup2(ta->ta_buf_size, sizeof(void *));
	if (sz > TA_BUF_SZ)
		return (EINVAL);

	KASSERT(ta->type <= IPFW_TABLE_MAXTYPE,("Increase IPFW_TABLE_MAXTYPE"));

	/* Copy algorithm data to stable storage. */
	ta_new = malloc(sizeof(struct table_algo), M_IPFW, M_WAITOK | M_ZERO);
	memcpy(ta_new, ta, size);

	tcfg = CHAIN_TO_TCFG(ch);

	KASSERT(tcfg->algo_count < 255, ("Increase algo array size"));

	tcfg->algo[++tcfg->algo_count] = ta_new;
	ta_new->idx = tcfg->algo_count;

	/* Set algorithm as default one for given type */
	if ((ta_new->flags & TA_FLAG_DEFAULT) != 0 &&
	    tcfg->def_algo[ta_new->type] == NULL)
		tcfg->def_algo[ta_new->type] = ta_new;

	*idx = ta_new->idx;
	
	return (0);
}

/*
 * Unregisters table algo using @idx as id.
 * XXX: It is NOT safe to call this function in any place
 * other than ipfw instance destroy handler.
 */
void
ipfw_del_table_algo(struct ip_fw_chain *ch, int idx)
{
	struct tables_config *tcfg;
	struct table_algo *ta;

	tcfg = CHAIN_TO_TCFG(ch);

	KASSERT(idx <= tcfg->algo_count, ("algo idx %d out of range 1..%d",
	    idx, tcfg->algo_count));

	ta = tcfg->algo[idx];
	KASSERT(ta != NULL, ("algo idx %d is NULL", idx));

	if (tcfg->def_algo[ta->type] == ta)
		tcfg->def_algo[ta->type] = NULL;

	free(ta, M_IPFW);
}

/*
 * Lists all table algorithms currently available.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_lheader ], size = ipfw_obj_lheader.size
 * Reply: [ ipfw_obj_lheader ipfw_ta_info x N ]
 *
 * Returns 0 on success
 */
static int
list_table_algo(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct _ipfw_obj_lheader *olh;
	struct tables_config *tcfg;
	ipfw_ta_info *i;
	struct table_algo *ta;
	uint32_t count, n, size;

	olh = (struct _ipfw_obj_lheader *)ipfw_get_sopt_header(sd,sizeof(*olh));
	if (olh == NULL)
		return (EINVAL);
	if (sd->valsize < olh->size)
		return (EINVAL);

	IPFW_UH_RLOCK(ch);
	tcfg = CHAIN_TO_TCFG(ch);
	count = tcfg->algo_count;
	size = count * sizeof(ipfw_ta_info) + sizeof(ipfw_obj_lheader);

	/* Fill in header regadless of buffer size */
	olh->count = count;
	olh->objsize = sizeof(ipfw_ta_info);

	if (size > olh->size) {
		olh->size = size;
		IPFW_UH_RUNLOCK(ch);
		return (ENOMEM);
	}
	olh->size = size;

	for (n = 1; n <= count; n++) {
		i = (ipfw_ta_info *)ipfw_get_sopt_space(sd, sizeof(*i));
		KASSERT(i != NULL, ("previously checked buffer is not enough"));
		ta = tcfg->algo[n];
		strlcpy(i->algoname, ta->name, sizeof(i->algoname));
		i->type = ta->type;
		i->refcnt = ta->refcnt;
	}

	IPFW_UH_RUNLOCK(ch);

	return (0);
}

static int
classify_srcdst(ipfw_insn *cmd, uint16_t *puidx, uint8_t *ptype)
{
	/* Basic IPv4/IPv6 or u32 lookups */
	*puidx = cmd->arg1;
	/* Assume ADDR by default */
	*ptype = IPFW_TABLE_ADDR;
	int v;
		
	if (F_LEN(cmd) > F_INSN_SIZE(ipfw_insn_u32)) {
		/*
		 * generic lookup. The key must be
		 * in 32bit big-endian format.
		 */
		v = ((ipfw_insn_u32 *)cmd)->d[1];
		switch (v) {
		case 0:
		case 1:
			/* IPv4 src/dst */
			break;
		case 2:
		case 3:
			/* src/dst port */
			*ptype = IPFW_TABLE_NUMBER;
			break;
		case 4:
			/* uid/gid */
			*ptype = IPFW_TABLE_NUMBER;
			break;
		case 5:
			/* jid */
			*ptype = IPFW_TABLE_NUMBER;
			break;
		case 6:
			/* dscp */
			*ptype = IPFW_TABLE_NUMBER;
			break;
		}
	}

	return (0);
}

static int
classify_via(ipfw_insn *cmd, uint16_t *puidx, uint8_t *ptype)
{
	ipfw_insn_if *cmdif;

	/* Interface table, possibly */
	cmdif = (ipfw_insn_if *)cmd;
	if (cmdif->name[0] != '\1')
		return (1);

	*ptype = IPFW_TABLE_INTERFACE;
	*puidx = cmdif->p.kidx;

	return (0);
}

static int
classify_flow(ipfw_insn *cmd, uint16_t *puidx, uint8_t *ptype)
{

	*puidx = cmd->arg1;
	*ptype = IPFW_TABLE_FLOW;

	return (0);
}

static void
update_arg1(ipfw_insn *cmd, uint16_t idx)
{

	cmd->arg1 = idx;
}

static void
update_via(ipfw_insn *cmd, uint16_t idx)
{
	ipfw_insn_if *cmdif;

	cmdif = (ipfw_insn_if *)cmd;
	cmdif->p.kidx = idx;
}

static int
table_findbyname(struct ip_fw_chain *ch, struct tid_info *ti,
    struct named_object **pno)
{
	struct table_config *tc;
	int error;

	IPFW_UH_WLOCK_ASSERT(ch);

	error = find_table_err(CHAIN_TO_NI(ch), ti, &tc);
	if (error != 0)
		return (error);

	*pno = &tc->no;
	return (0);
}

/* XXX: sets-sets! */
static struct named_object *
table_findbykidx(struct ip_fw_chain *ch, uint16_t idx)
{
	struct namedobj_instance *ni;
	struct table_config *tc;

	IPFW_UH_WLOCK_ASSERT(ch);
	ni = CHAIN_TO_NI(ch);
	tc = (struct table_config *)ipfw_objhash_lookup_kidx(ni, idx);
	KASSERT(tc != NULL, ("Table with index %d not found", idx));

	return (&tc->no);
}

static int
table_manage_sets(struct ip_fw_chain *ch, uint16_t set, uint8_t new_set,
    enum ipfw_sets_cmd cmd)
{

	switch (cmd) {
	case SWAP_ALL:
	case TEST_ALL:
	case MOVE_ALL:
		/*
		 * Always return success, the real action and decision
		 * should make table_manage_sets_all().
		 */
		return (0);
	case TEST_ONE:
	case MOVE_ONE:
		/*
		 * NOTE: we need to use ipfw_objhash_del/ipfw_objhash_add
		 * if set number will be used in hash function. Currently
		 * we can just use generic handler that replaces set value.
		 */
		if (V_fw_tables_sets == 0)
			return (0);
		break;
	case COUNT_ONE:
		/*
		 * Return EOPNOTSUPP for COUNT_ONE when per-set sysctl is
		 * disabled. This allow skip table's opcodes from additional
		 * checks when specific rules moved to another set.
		 */
		if (V_fw_tables_sets == 0)
			return (EOPNOTSUPP);
	}
	/* Use generic sets handler when per-set sysctl is enabled. */
	return (ipfw_obj_manage_sets(CHAIN_TO_NI(ch), IPFW_TLV_TBL_NAME,
	    set, new_set, cmd));
}

/*
 * We register several opcode rewriters for lookup tables.
 * All tables opcodes have the same ETLV type, but different subtype.
 * To avoid invoking sets handler several times for XXX_ALL commands,
 * we use separate manage_sets handler. O_RECV has the lowest value,
 * so it should be called first.
 */
static int
table_manage_sets_all(struct ip_fw_chain *ch, uint16_t set, uint8_t new_set,
    enum ipfw_sets_cmd cmd)
{

	switch (cmd) {
	case SWAP_ALL:
	case TEST_ALL:
		/*
		 * Return success for TEST_ALL, since nothing prevents
		 * move rules from one set to another. All tables are
		 * accessible from all sets when per-set tables sysctl
		 * is disabled.
		 */
	case MOVE_ALL:
		if (V_fw_tables_sets == 0)
			return (0);
		break;
	default:
		return (table_manage_sets(ch, set, new_set, cmd));
	}
	/* Use generic sets handler when per-set sysctl is enabled. */
	return (ipfw_obj_manage_sets(CHAIN_TO_NI(ch), IPFW_TLV_TBL_NAME,
	    set, new_set, cmd));
}

static struct opcode_obj_rewrite opcodes[] = {
	{
		.opcode = O_IP_SRC_LOOKUP,
		.etlv = IPFW_TLV_TBL_NAME,
		.classifier = classify_srcdst,
		.update = update_arg1,
		.find_byname = table_findbyname,
		.find_bykidx = table_findbykidx,
		.create_object = create_table_compat,
		.manage_sets = table_manage_sets,
	},
	{
		.opcode = O_IP_DST_LOOKUP,
		.etlv = IPFW_TLV_TBL_NAME,
		.classifier = classify_srcdst,
		.update = update_arg1,
		.find_byname = table_findbyname,
		.find_bykidx = table_findbykidx,
		.create_object = create_table_compat,
		.manage_sets = table_manage_sets,
	},
	{
		.opcode = O_IP_FLOW_LOOKUP,
		.etlv = IPFW_TLV_TBL_NAME,
		.classifier = classify_flow,
		.update = update_arg1,
		.find_byname = table_findbyname,
		.find_bykidx = table_findbykidx,
		.create_object = create_table_compat,
		.manage_sets = table_manage_sets,
	},
	{
		.opcode = O_XMIT,
		.etlv = IPFW_TLV_TBL_NAME,
		.classifier = classify_via,
		.update = update_via,
		.find_byname = table_findbyname,
		.find_bykidx = table_findbykidx,
		.create_object = create_table_compat,
		.manage_sets = table_manage_sets,
	},
	{
		.opcode = O_RECV,
		.etlv = IPFW_TLV_TBL_NAME,
		.classifier = classify_via,
		.update = update_via,
		.find_byname = table_findbyname,
		.find_bykidx = table_findbykidx,
		.create_object = create_table_compat,
		.manage_sets = table_manage_sets_all,
	},
	{
		.opcode = O_VIA,
		.etlv = IPFW_TLV_TBL_NAME,
		.classifier = classify_via,
		.update = update_via,
		.find_byname = table_findbyname,
		.find_bykidx = table_findbykidx,
		.create_object = create_table_compat,
		.manage_sets = table_manage_sets,
	},
};

static int
test_sets_cb(struct namedobj_instance *ni __unused, struct named_object *no,
    void *arg __unused)
{

	/* Check that there aren't any tables in not default set */
	if (no->set != 0)
		return (EBUSY);
	return (0);
}

/*
 * Switch between "set 0" and "rule's set" table binding,
 * Check all ruleset bindings and permits changing
 * IFF each binding has both rule AND table in default set (set 0).
 *
 * Returns 0 on success.
 */
int
ipfw_switch_tables_namespace(struct ip_fw_chain *ch, unsigned int sets)
{
	struct opcode_obj_rewrite *rw;
	struct namedobj_instance *ni;
	struct named_object *no;
	struct ip_fw *rule;
	ipfw_insn *cmd;
	int cmdlen, i, l;
	uint16_t kidx;
	uint8_t subtype;

	IPFW_UH_WLOCK(ch);

	if (V_fw_tables_sets == sets) {
		IPFW_UH_WUNLOCK(ch);
		return (0);
	}
	ni = CHAIN_TO_NI(ch);
	if (sets == 0) {
		/*
		 * Prevent disabling sets support if we have some tables
		 * in not default sets.
		 */
		if (ipfw_objhash_foreach_type(ni, test_sets_cb,
		    NULL, IPFW_TLV_TBL_NAME) != 0) {
			IPFW_UH_WUNLOCK(ch);
			return (EBUSY);
		}
	}
	/*
	 * Scan all rules and examine tables opcodes.
	 */
	for (i = 0; i < ch->n_rules; i++) {
		rule = ch->map[i];

		l = rule->cmd_len;
		cmd = rule->cmd;
		cmdlen = 0;
		for ( ;	l > 0 ; l -= cmdlen, cmd += cmdlen) {
			cmdlen = F_LEN(cmd);
			/* Check only tables opcodes */
			for (kidx = 0, rw = opcodes;
			    rw < opcodes + nitems(opcodes); rw++) {
				if (rw->opcode != cmd->opcode)
					continue;
				if (rw->classifier(cmd, &kidx, &subtype) == 0)
					break;
			}
			if (kidx == 0)
				continue;
			no = ipfw_objhash_lookup_kidx(ni, kidx);
			/* Check if both table object and rule has the set 0 */
			if (no->set != 0 || rule->set != 0) {
				IPFW_UH_WUNLOCK(ch);
				return (EBUSY);
			}

		}
	}
	V_fw_tables_sets = sets;
	IPFW_UH_WUNLOCK(ch);
	return (0);
}

/*
 * Checks table name for validity.
 * Enforce basic length checks, the rest
 * should be done in userland.
 *
 * Returns 0 if name is considered valid.
 */
static int
check_table_name(const char *name)
{

	/*
	 * TODO: do some more complicated checks
	 */
	return (ipfw_check_object_name_generic(name));
}

/*
 * Finds table config based on either legacy index
 * or name in ntlv.
 * Note @ti structure contains unchecked data from userland.
 *
 * Returns 0 in success and fills in @tc with found config
 */
static int
find_table_err(struct namedobj_instance *ni, struct tid_info *ti,
    struct table_config **tc)
{
	char *name, bname[16];
	struct named_object *no;
	ipfw_obj_ntlv *ntlv;
	uint32_t set;

	if (ti->tlvs != NULL) {
		ntlv = ipfw_find_name_tlv_type(ti->tlvs, ti->tlen, ti->uidx,
		    IPFW_TLV_TBL_NAME);
		if (ntlv == NULL)
			return (EINVAL);
		name = ntlv->name;

		/*
		 * Use set provided by @ti instead of @ntlv one.
		 * This is needed due to different sets behavior
		 * controlled by V_fw_tables_sets.
		 */
		set = (V_fw_tables_sets != 0) ? ti->set : 0;
	} else {
		snprintf(bname, sizeof(bname), "%d", ti->uidx);
		name = bname;
		set = 0;
	}

	no = ipfw_objhash_lookup_name(ni, set, name);
	*tc = (struct table_config *)no;

	return (0);
}

/*
 * Finds table config based on either legacy index
 * or name in ntlv.
 * Note @ti structure contains unchecked data from userland.
 *
 * Returns pointer to table_config or NULL.
 */
static struct table_config *
find_table(struct namedobj_instance *ni, struct tid_info *ti)
{
	struct table_config *tc;

	if (find_table_err(ni, ti, &tc) != 0)
		return (NULL);

	return (tc);
}

/*
 * Allocate new table config structure using
 * specified @algo and @aname.
 *
 * Returns pointer to config or NULL.
 */
static struct table_config *
alloc_table_config(struct ip_fw_chain *ch, struct tid_info *ti,
    struct table_algo *ta, char *aname, uint8_t tflags)
{
	char *name, bname[16];
	struct table_config *tc;
	int error;
	ipfw_obj_ntlv *ntlv;
	uint32_t set;

	if (ti->tlvs != NULL) {
		ntlv = ipfw_find_name_tlv_type(ti->tlvs, ti->tlen, ti->uidx,
		    IPFW_TLV_TBL_NAME);
		if (ntlv == NULL)
			return (NULL);
		name = ntlv->name;
		set = (V_fw_tables_sets == 0) ? 0 : ntlv->set;
	} else {
		/* Compat part: convert number to string representation */
		snprintf(bname, sizeof(bname), "%d", ti->uidx);
		name = bname;
		set = 0;
	}

	tc = malloc(sizeof(struct table_config), M_IPFW, M_WAITOK | M_ZERO);
	tc->no.name = tc->tablename;
	tc->no.subtype = ta->type;
	tc->no.set = set;
	tc->tflags = tflags;
	tc->ta = ta;
	strlcpy(tc->tablename, name, sizeof(tc->tablename));
	/* Set "shared" value type by default */
	tc->vshared = 1;

	/* Preallocate data structures for new tables */
	error = ta->init(ch, &tc->astate, &tc->ti_copy, aname, tflags);
	if (error != 0) {
		free(tc, M_IPFW);
		return (NULL);
	}
	
	return (tc);
}

/*
 * Destroys table state and config.
 */
static void
free_table_config(struct namedobj_instance *ni, struct table_config *tc)
{

	KASSERT(tc->linked == 0, ("free() on linked config"));
	/* UH lock MUST NOT be held */

	/*
	 * We're using ta without any locking/referencing.
	 * TODO: fix this if we're going to use unloadable algos.
	 */
	tc->ta->destroy(tc->astate, &tc->ti_copy);
	free(tc, M_IPFW);
}

/*
 * Links @tc to @chain table named instance.
 * Sets appropriate type/states in @chain table info.
 */
static void
link_table(struct ip_fw_chain *ch, struct table_config *tc)
{
	struct namedobj_instance *ni;
	struct table_info *ti;
	uint16_t kidx;

	IPFW_UH_WLOCK_ASSERT(ch);

	ni = CHAIN_TO_NI(ch);
	kidx = tc->no.kidx;

	ipfw_objhash_add(ni, &tc->no);

	ti = KIDX_TO_TI(ch, kidx);
	*ti = tc->ti_copy;

	/* Notify algo on real @ti address */
	if (tc->ta->change_ti != NULL)
		tc->ta->change_ti(tc->astate, ti);

	tc->linked = 1;
	tc->ta->refcnt++;
}

/*
 * Unlinks @tc from @chain table named instance.
 * Zeroes states in @chain and stores them in @tc.
 */
static void
unlink_table(struct ip_fw_chain *ch, struct table_config *tc)
{
	struct namedobj_instance *ni;
	struct table_info *ti;
	uint16_t kidx;

	IPFW_UH_WLOCK_ASSERT(ch);
	IPFW_WLOCK_ASSERT(ch);

	ni = CHAIN_TO_NI(ch);
	kidx = tc->no.kidx;

	/* Clear state. @ti copy is already saved inside @tc */
	ipfw_objhash_del(ni, &tc->no);
	ti = KIDX_TO_TI(ch, kidx);
	memset(ti, 0, sizeof(struct table_info));
	tc->linked = 0;
	tc->ta->refcnt--;

	/* Notify algo on real @ti address */
	if (tc->ta->change_ti != NULL)
		tc->ta->change_ti(tc->astate, NULL);
}

static struct ipfw_sopt_handler	scodes[] = {
	{ IP_FW_TABLE_XCREATE,	0,	HDIR_SET,	create_table },
	{ IP_FW_TABLE_XDESTROY,	0,	HDIR_SET,	flush_table_v0 },
	{ IP_FW_TABLE_XFLUSH,	0,	HDIR_SET,	flush_table_v0 },
	{ IP_FW_TABLE_XMODIFY,	0,	HDIR_BOTH,	modify_table },
	{ IP_FW_TABLE_XINFO,	0,	HDIR_GET,	describe_table },
	{ IP_FW_TABLES_XLIST,	0,	HDIR_GET,	list_tables },
	{ IP_FW_TABLE_XLIST,	0,	HDIR_GET,	dump_table_v0 },
	{ IP_FW_TABLE_XLIST,	1,	HDIR_GET,	dump_table_v1 },
	{ IP_FW_TABLE_XADD,	0,	HDIR_BOTH,	manage_table_ent_v0 },
	{ IP_FW_TABLE_XADD,	1,	HDIR_BOTH,	manage_table_ent_v1 },
	{ IP_FW_TABLE_XDEL,	0,	HDIR_BOTH,	manage_table_ent_v0 },
	{ IP_FW_TABLE_XDEL,	1,	HDIR_BOTH,	manage_table_ent_v1 },
	{ IP_FW_TABLE_XFIND,	0,	HDIR_GET,	find_table_entry },
	{ IP_FW_TABLE_XSWAP,	0,	HDIR_SET,	swap_table },
	{ IP_FW_TABLES_ALIST,	0,	HDIR_GET,	list_table_algo },
	{ IP_FW_TABLE_XGETSIZE,	0,	HDIR_GET,	get_table_size },
};

static int
destroy_table_locked(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{

	unlink_table((struct ip_fw_chain *)arg, (struct table_config *)no);
	if (ipfw_objhash_free_idx(ni, no->kidx) != 0)
		printf("Error unlinking kidx %d from table %s\n",
		    no->kidx, no->name);
	free_table_config(ni, (struct table_config *)no);
	return (0);
}

/*
 * Shuts tables module down.
 */
void
ipfw_destroy_tables(struct ip_fw_chain *ch, int last)
{

	IPFW_DEL_SOPT_HANDLER(last, scodes);
	IPFW_DEL_OBJ_REWRITER(last, opcodes);

	/* Remove all tables from working set */
	IPFW_UH_WLOCK(ch);
	IPFW_WLOCK(ch);
	ipfw_objhash_foreach(CHAIN_TO_NI(ch), destroy_table_locked, ch);
	IPFW_WUNLOCK(ch);
	IPFW_UH_WUNLOCK(ch);

	/* Free pointers itself */
	free(ch->tablestate, M_IPFW);

	ipfw_table_value_destroy(ch, last);
	ipfw_table_algo_destroy(ch);

	ipfw_objhash_destroy(CHAIN_TO_NI(ch));
	free(CHAIN_TO_TCFG(ch), M_IPFW);
}

/*
 * Starts tables module.
 */
int
ipfw_init_tables(struct ip_fw_chain *ch, int first)
{
	struct tables_config *tcfg;

	/* Allocate pointers */
	ch->tablestate = malloc(V_fw_tables_max * sizeof(struct table_info),
	    M_IPFW, M_WAITOK | M_ZERO);

	tcfg = malloc(sizeof(struct tables_config), M_IPFW, M_WAITOK | M_ZERO);
	tcfg->namehash = ipfw_objhash_create(V_fw_tables_max);
	ch->tblcfg = tcfg;

	ipfw_table_value_init(ch, first);
	ipfw_table_algo_init(ch);

	IPFW_ADD_OBJ_REWRITER(first, opcodes);
	IPFW_ADD_SOPT_HANDLER(first, scodes);
	return (0);
}



