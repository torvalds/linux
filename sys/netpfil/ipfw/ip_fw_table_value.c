/*-
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
 * Multi-field value support for ipfw tables.
 *
 * This file contains necessary functions to convert
 * large multi-field values into u32 indices suitable to be fed
 * to various table algorithms. Other machinery like proper refcounting,
 * internal structures resizing are also kept here.
 */

#include "opt_ipfw.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/hash.h>
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

static uint32_t hash_table_value(struct namedobj_instance *ni, const void *key,
    uint32_t kopt);
static int cmp_table_value(struct named_object *no, const void *key,
    uint32_t kopt);

static int list_table_values(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd);

static struct ipfw_sopt_handler	scodes[] = {
	{ IP_FW_TABLE_VLIST,	0,	HDIR_GET,	list_table_values },
};

#define	CHAIN_TO_VI(chain)	(CHAIN_TO_TCFG(chain)->valhash)

struct table_val_link
{
	struct named_object	no;
	struct table_value	*pval;	/* Pointer to real table value */
};
#define	VALDATA_START_SIZE	64	/* Allocate 64-items array by default */

struct vdump_args {
	struct ip_fw_chain *ch;
	struct sockopt_data *sd;
	struct table_value *pval;
	int error;
};


static uint32_t
hash_table_value(struct namedobj_instance *ni, const void *key, uint32_t kopt)
{

	return (hash32_buf(key, 56, 0));
}

static int
cmp_table_value(struct named_object *no, const void *key, uint32_t kopt)
{

	return (memcmp(((struct table_val_link *)no)->pval, key, 56));
}

static void
mask_table_value(struct table_value *src, struct table_value *dst,
    uint32_t mask)
{
#define	_MCPY(f, b)	if ((mask & (b)) != 0) { dst->f = src->f; }

	memset(dst, 0, sizeof(*dst));
	_MCPY(tag, IPFW_VTYPE_TAG);
	_MCPY(pipe, IPFW_VTYPE_PIPE);
	_MCPY(divert, IPFW_VTYPE_DIVERT);
	_MCPY(skipto, IPFW_VTYPE_SKIPTO);
	_MCPY(netgraph, IPFW_VTYPE_NETGRAPH);
	_MCPY(fib, IPFW_VTYPE_FIB);
	_MCPY(nat, IPFW_VTYPE_NAT);
	_MCPY(dscp, IPFW_VTYPE_DSCP);
	_MCPY(nh4, IPFW_VTYPE_NH4);
	_MCPY(nh6, IPFW_VTYPE_NH6);
	_MCPY(zoneid, IPFW_VTYPE_NH6);
#undef	_MCPY
}

static void
get_value_ptrs(struct ip_fw_chain *ch, struct table_config *tc, int vshared,
    struct table_value **ptv, struct namedobj_instance **pvi)
{
	struct table_value *pval;
	struct namedobj_instance *vi;

	if (vshared != 0) {
		pval = (struct table_value *)ch->valuestate;
		vi = CHAIN_TO_VI(ch);
	} else {
		pval = NULL;
		vi = NULL;
		//pval = (struct table_value *)&tc->ti.data;
	}

	if (ptv != NULL)
		*ptv = pval;
	if (pvi != NULL)
		*pvi = vi;
}

/*
 * Update pointers to real vaues after @pval change.
 */
static int
update_tvalue(struct namedobj_instance *ni, struct named_object *no, void *arg)
{
	struct vdump_args *da;
	struct table_val_link *ptv;
	struct table_value *pval;

	da = (struct vdump_args *)arg;
	ptv = (struct table_val_link *)no;

	pval = da->pval;
	ptv->pval = &pval[ptv->no.kidx];
	ptv->no.name = (char *)&pval[ptv->no.kidx];
	return (0);
}

/*
 * Grows value storage shared among all tables.
 * Drops/reacquires UH locks.
 * Notifies other running adds on @ch shared storage resize.
 * Note function does not guarantee that free space
 * will be available after invocation, so one caller needs
 * to roll cycle himself.
 *
 * Returns 0 if case of no errors.
 */
static int
resize_shared_value_storage(struct ip_fw_chain *ch)
{
	struct tables_config *tcfg;
	struct namedobj_instance *vi;
	struct table_value *pval, *valuestate, *old_valuestate;
	void *new_idx;
	struct vdump_args da;
	int new_blocks;
	int val_size, val_size_old;

	IPFW_UH_WLOCK_ASSERT(ch);

	valuestate = NULL;
	new_idx = NULL;

	pval = (struct table_value *)ch->valuestate;
	vi = CHAIN_TO_VI(ch);
	tcfg = CHAIN_TO_TCFG(ch);

	val_size = tcfg->val_size * 2;

	if (val_size == (1 << 30))
		return (ENOSPC);

	IPFW_UH_WUNLOCK(ch);

	valuestate = malloc(sizeof(struct table_value) * val_size, M_IPFW,
	    M_WAITOK | M_ZERO);
	ipfw_objhash_bitmap_alloc(val_size, (void *)&new_idx,
	    &new_blocks);

	IPFW_UH_WLOCK(ch);

	/*
	 * Check if we still need to resize
	 */
	if (tcfg->val_size >= val_size)
		goto done;

	/* Update pointers and notify everyone we're changing @ch */
	pval = (struct table_value *)ch->valuestate;
	rollback_toperation_state(ch, ch);

	/* Good. Let's merge */
	memcpy(valuestate, pval, sizeof(struct table_value) * tcfg->val_size);
	ipfw_objhash_bitmap_merge(CHAIN_TO_VI(ch), &new_idx, &new_blocks);

	IPFW_WLOCK(ch);
	/* Change pointers */
	old_valuestate = ch->valuestate;
	ch->valuestate = valuestate;
	valuestate = old_valuestate;
	ipfw_objhash_bitmap_swap(CHAIN_TO_VI(ch), &new_idx, &new_blocks);

	val_size_old = tcfg->val_size;
	tcfg->val_size = val_size;
	val_size = val_size_old;
	IPFW_WUNLOCK(ch);
	/* Update pointers to reflect resize */
	memset(&da, 0, sizeof(da));
	da.pval = (struct table_value *)ch->valuestate;
	ipfw_objhash_foreach(vi, update_tvalue, &da);

done:
	free(valuestate, M_IPFW);
	ipfw_objhash_bitmap_free(new_idx, new_blocks);

	return (0);
}

/*
 * Drops reference for table value with index @kidx, stored in @pval and
 * @vi. Frees value if it has no references.
 */
static void
unref_table_value(struct namedobj_instance *vi, struct table_value *pval,
    uint32_t kidx)
{
	struct table_val_link *ptvl;

	KASSERT(pval[kidx].refcnt > 0, ("Refcount is 0 on kidx %d", kidx));
	if (--pval[kidx].refcnt > 0)
		return;

	/* Last reference, delete item */
	ptvl = (struct table_val_link *)ipfw_objhash_lookup_kidx(vi, kidx);
	KASSERT(ptvl != NULL, ("lookup on value kidx %d failed", kidx));
	ipfw_objhash_del(vi, &ptvl->no);
	ipfw_objhash_free_idx(vi, kidx);
	free(ptvl, M_IPFW);
}

struct flush_args {
	struct ip_fw_chain *ch;
	struct table_algo *ta;
	struct table_info *ti;
	void *astate;
	ipfw_obj_tentry tent;
};

static int
unref_table_value_cb(void *e, void *arg)
{
	struct flush_args *fa;
	struct ip_fw_chain *ch;
	struct table_algo *ta;
	ipfw_obj_tentry *tent;
	int error;

	fa = (struct flush_args *)arg;

	ta = fa->ta;
	memset(&fa->tent, 0, sizeof(fa->tent));
	tent = &fa->tent;
	error = ta->dump_tentry(fa->astate, fa->ti, e, tent);
	if (error != 0)
		return (error);

	ch = fa->ch;

	unref_table_value(CHAIN_TO_VI(ch),
	    (struct table_value *)ch->valuestate, tent->v.kidx);

	return (0);
}

/*
 * Drop references for each value used in @tc.
 */
void
ipfw_unref_table_values(struct ip_fw_chain *ch, struct table_config *tc,
    struct table_algo *ta, void *astate, struct table_info *ti)
{
	struct flush_args fa;

	IPFW_UH_WLOCK_ASSERT(ch);

	memset(&fa, 0, sizeof(fa));
	fa.ch = ch;
	fa.ta = ta;
	fa.astate = astate;
	fa.ti = ti;

	ta->foreach(astate, ti, unref_table_value_cb, &fa);
}

/*
 * Table operation state handler.
 * Called when we are going to change something in @tc which
 * may lead to inconsistencies in on-going table data addition.
 *
 * Here we rollback all already committed state (table values, currently)
 * and set "modified" field to non-zero value to indicate
 * that we need to restart original operation.
 */
void
rollback_table_values(struct tableop_state *ts)
{
	struct ip_fw_chain *ch;
	struct table_value *pval;
	struct tentry_info *ptei;
	struct namedobj_instance *vi;
	int i;

	ch = ts->ch;

	IPFW_UH_WLOCK_ASSERT(ch);

	/* Get current table value pointer */
	get_value_ptrs(ch, ts->tc, ts->vshared, &pval, &vi);

	for (i = 0; i < ts->count; i++) {
		ptei = &ts->tei[i];

		if (ptei->value == 0)
			continue;

		unref_table_value(vi, pval, ptei->value);
	}
}

/*
 * Allocate new value index in either shared or per-table array.
 * Function may drop/reacquire UH lock.
 *
 * Returns 0 on success.
 */
static int
alloc_table_vidx(struct ip_fw_chain *ch, struct tableop_state *ts,
    struct namedobj_instance *vi, uint16_t *pvidx)
{
	int error, vlimit;
	uint16_t vidx;

	IPFW_UH_WLOCK_ASSERT(ch);

	error = ipfw_objhash_alloc_idx(vi, &vidx);
	if (error != 0) {

		/*
		 * We need to resize array. This involves
		 * lock/unlock, so we need to check "modified"
		 * state.
		 */
		ts->opstate.func(ts->tc, &ts->opstate);
		error = resize_shared_value_storage(ch);
		return (error); /* ts->modified should be set, we will restart */
	}

	vlimit = ts->ta->vlimit;
	if (vlimit != 0 && vidx >= vlimit) {

		/*
		 * Algorithm is not able to store given index.
		 * We have to rollback state, start using
		 * per-table value array or return error
		 * if we're already using it.
		 *
		 * TODO: do not rollback state if
		 * atomicity is not required.
		 */
		if (ts->vshared != 0) {
			/* shared -> per-table  */
			return (ENOSPC); /* TODO: proper error */
		}

		/* per-table. Fail for now. */
		return (ENOSPC); /* TODO: proper error */
	}

	*pvidx = vidx;
	return (0);
}

/*
 * Drops value reference for unused values (updates, deletes, partially
 * successful adds or rollbacks).
 */
void
ipfw_garbage_table_values(struct ip_fw_chain *ch, struct table_config *tc,
    struct tentry_info *tei, uint32_t count, int rollback)
{
	int i;
	struct tentry_info *ptei;
	struct table_value *pval;
	struct namedobj_instance *vi;

	/*
	 * We have two slightly different ADD cases here:
	 * either (1) we are successful / partially successful,
	 * in that case we need
	 * * to ignore ADDED entries values
	 * * rollback every other values (either UPDATED since
	 *   old value has been stored there, or some failure like
	 *   EXISTS or LIMIT or simply "ignored" case.
	 *
	 * (2): atomic rollback of partially successful operation
	 * in that case we simply need to unref all entries.
	 *
	 * DELETE case is simpler: no atomic support there, so
	 * we simply unref all non-zero values.
	 */

	/*
	 * Get current table value pointers.
	 * XXX: Properly read vshared
	 */
	get_value_ptrs(ch, tc, 1, &pval, &vi);

	for (i = 0; i < count; i++) {
		ptei = &tei[i];

		if (ptei->value == 0) {

			/*
			 * We may be deleting non-existing record.
			 * Skip.
			 */
			continue;
		}

		if ((ptei->flags & TEI_FLAGS_ADDED) != 0 && rollback == 0) {
			ptei->value = 0;
			continue;
		}

		unref_table_value(vi, pval, ptei->value);
		ptei->value = 0;
	}
}

/*
 * Main function used to link values of entries going to be added,
 * to the index. Since we may perform many UH locks drops/acquires,
 * handle changes by checking tablestate "modified" field.
 *
 * Success: return 0.
 */
int
ipfw_link_table_values(struct ip_fw_chain *ch, struct tableop_state *ts)
{
	int error, i, found;
	struct namedobj_instance *vi;
	struct table_config *tc;
	struct tentry_info *tei, *ptei;
	uint32_t count, vlimit;
	uint16_t vidx;
	struct table_val_link *ptv;
	struct table_value tval, *pval;

	/*
	 * Stage 1: reference all existing values and
	 * save their indices.
	 */
	IPFW_UH_WLOCK_ASSERT(ch);
	get_value_ptrs(ch, ts->tc, ts->vshared, &pval, &vi);

	error = 0;
	found = 0;
	vlimit = ts->ta->vlimit;
	vidx = 0;
	tc = ts->tc;
	tei = ts->tei;
	count = ts->count;
	for (i = 0; i < count; i++) {
		ptei = &tei[i];
		ptei->value = 0; /* Ensure value is always 0 in the beginning */
		mask_table_value(ptei->pvalue, &tval, ts->vmask);
		ptv = (struct table_val_link *)ipfw_objhash_lookup_name(vi, 0,
		    (char *)&tval);
		if (ptv == NULL)
			continue;
		/* Deal with vlimit later */
		if (vlimit > 0 && vlimit <= ptv->no.kidx)
			continue;

		/* Value found. Bump refcount */
		ptv->pval->refcnt++;
		ptei->value = ptv->no.kidx;
		found++;
	}

	if (ts->count == found) {
		/* We've found all values , no need ts create new ones */
		return (0);
	}

	/*
	 * we have added some state here, let's attach operation
	 * state ts the list ts be able ts rollback if necessary.
	 */
	add_toperation_state(ch, ts);
	/* Ensure table won't disappear */
	tc_ref(tc);
	IPFW_UH_WUNLOCK(ch);

	/*
	 * Stage 2: allocate objects for non-existing values.
	 */
	for (i = 0; i < count; i++) {
		ptei = &tei[i];
		if (ptei->value != 0)
			continue;
		if (ptei->ptv != NULL)
			continue;
		ptei->ptv = malloc(sizeof(struct table_val_link), M_IPFW,
		    M_WAITOK | M_ZERO);
	}

	/*
	 * Stage 3: allocate index numbers for new values
	 * and link them to index.
	 */
	IPFW_UH_WLOCK(ch);
	tc_unref(tc);
	del_toperation_state(ch, ts);
	if (ts->modified != 0) {

		/*
		 * In general, we should free all state/indexes here
		 * and return. However, we keep allocated state instead
		 * to ensure we achieve some progress on each restart.
		 */
		return (0);
	}

	KASSERT(pval == ch->valuestate, ("resize_storage() notify failure"));

	/* Let's try to link values */
	for (i = 0; i < count; i++) {
		ptei = &tei[i];

		/* Check if record has appeared */
		mask_table_value(ptei->pvalue, &tval, ts->vmask);
		ptv = (struct table_val_link *)ipfw_objhash_lookup_name(vi, 0,
		    (char *)&tval);
		if (ptv != NULL) {
			ptv->pval->refcnt++;
			ptei->value = ptv->no.kidx;
			continue;
		}

		/* May perform UH unlock/lock */
		error = alloc_table_vidx(ch, ts, vi, &vidx);
		if (error != 0) {
			ts->opstate.func(ts->tc, &ts->opstate);
			return (error);
		}
		/* value storage resize has happened, return */
		if (ts->modified != 0)
			return (0);

		/* Finally, we have allocated valid index, let's add entry */
		ptei->value = vidx;
		ptv = (struct table_val_link *)ptei->ptv;
		ptei->ptv = NULL;

		ptv->no.kidx = vidx;
		ptv->no.name = (char *)&pval[vidx];
		ptv->pval = &pval[vidx];
		memcpy(ptv->pval, &tval, sizeof(struct table_value));
		pval[vidx].refcnt = 1;
		ipfw_objhash_add(vi, &ptv->no);
	}

	return (0);
}

/*
 * Compatibility function used to import data from old
 * IP_FW_TABLE_ADD / IP_FW_TABLE_XADD opcodes.
 */
void
ipfw_import_table_value_legacy(uint32_t value, struct table_value *v)
{

	memset(v, 0, sizeof(*v));
	v->tag = value;
	v->pipe = value;
	v->divert = value;
	v->skipto = value;
	v->netgraph = value;
	v->fib = value;
	v->nat = value;
	v->nh4 = value; /* host format */
	v->dscp = value;
	v->limit = value;
}

/*
 * Export data to legacy table dumps opcodes.
 */
uint32_t
ipfw_export_table_value_legacy(struct table_value *v)
{

	/*
	 * TODO: provide more compatibility depending on
	 * vmask value.
	 */
	return (v->tag);
}

/*
 * Imports table value from current userland format.
 * Saves value in kernel format to the same place.
 */
void
ipfw_import_table_value_v1(ipfw_table_value *iv)
{
	struct table_value v;

	memset(&v, 0, sizeof(v));
	v.tag = iv->tag;
	v.pipe = iv->pipe;
	v.divert = iv->divert;
	v.skipto = iv->skipto;
	v.netgraph = iv->netgraph;
	v.fib = iv->fib;
	v.nat = iv->nat;
	v.dscp = iv->dscp;
	v.nh4 = iv->nh4;
	v.nh6 = iv->nh6;
	v.limit = iv->limit;
	v.zoneid = iv->zoneid;

	memcpy(iv, &v, sizeof(ipfw_table_value));
}

/*
 * Export real table value @v to current userland format.
 * Note that @v and @piv may point to the same memory.
 */
void
ipfw_export_table_value_v1(struct table_value *v, ipfw_table_value *piv)
{
	ipfw_table_value iv;

	memset(&iv, 0, sizeof(iv));
	iv.tag = v->tag;
	iv.pipe = v->pipe;
	iv.divert = v->divert;
	iv.skipto = v->skipto;
	iv.netgraph = v->netgraph;
	iv.fib = v->fib;
	iv.nat = v->nat;
	iv.dscp = v->dscp;
	iv.limit = v->limit;
	iv.nh4 = v->nh4;
	iv.nh6 = v->nh6;
	iv.zoneid = v->zoneid;

	memcpy(piv, &iv, sizeof(iv));
}

/*
 * Exports real value data into ipfw_table_value structure.
 * Utilizes "spare1" field to store kernel index.
 */
static int
dump_tvalue(struct namedobj_instance *ni, struct named_object *no, void *arg)
{
	struct vdump_args *da;
	struct table_val_link *ptv;
	struct table_value *v;

	da = (struct vdump_args *)arg;
	ptv = (struct table_val_link *)no;

	v = (struct table_value *)ipfw_get_sopt_space(da->sd, sizeof(*v));
	/* Out of memory, returning */
	if (v == NULL) {
		da->error = ENOMEM;
		return (ENOMEM);
	}

	memcpy(v, ptv->pval, sizeof(*v));
	v->spare1 = ptv->no.kidx;
	return (0);
}

/*
 * Dumps all shared/table value data
 * Data layout (v1)(current):
 * Request: [ ipfw_obj_lheader ], size = ipfw_obj_lheader.size
 * Reply: [ ipfw_obj_lheader ipfw_table_value x N ]
 *
 * Returns 0 on success
 */
static int
list_table_values(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	struct _ipfw_obj_lheader *olh;
	struct namedobj_instance *vi;
	struct vdump_args da;
	uint32_t count, size;

	olh = (struct _ipfw_obj_lheader *)ipfw_get_sopt_header(sd,sizeof(*olh));
	if (olh == NULL)
		return (EINVAL);
	if (sd->valsize < olh->size)
		return (EINVAL);

	IPFW_UH_RLOCK(ch);
	vi = CHAIN_TO_VI(ch);

	count = ipfw_objhash_count(vi);
	size = count * sizeof(ipfw_table_value) + sizeof(ipfw_obj_lheader);

	/* Fill in header regadless of buffer size */
	olh->count = count;
	olh->objsize = sizeof(ipfw_table_value);

	if (size > olh->size) {
		olh->size = size;
		IPFW_UH_RUNLOCK(ch);
		return (ENOMEM);
	}
	olh->size = size;

	/*
	 * Do the actual value dump
	 */
	memset(&da, 0, sizeof(da));
	da.ch = ch;
	da.sd = sd;
	ipfw_objhash_foreach(vi, dump_tvalue, &da);

	IPFW_UH_RUNLOCK(ch);

	return (0);
}

void
ipfw_table_value_init(struct ip_fw_chain *ch, int first)
{
	struct tables_config *tcfg;

	ch->valuestate = malloc(VALDATA_START_SIZE * sizeof(struct table_value),
	    M_IPFW, M_WAITOK | M_ZERO);

	tcfg = ch->tblcfg;

	tcfg->val_size = VALDATA_START_SIZE;
	tcfg->valhash = ipfw_objhash_create(tcfg->val_size);
	ipfw_objhash_set_funcs(tcfg->valhash, hash_table_value,
	    cmp_table_value);

	IPFW_ADD_SOPT_HANDLER(first, scodes);
}

static int
destroy_value(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{

	free(no, M_IPFW);
	return (0);
}

void
ipfw_table_value_destroy(struct ip_fw_chain *ch, int last)
{

	IPFW_DEL_SOPT_HANDLER(last, scodes);

	free(ch->valuestate, M_IPFW);
	ipfw_objhash_foreach(CHAIN_TO_VI(ch), destroy_value, ch);
	ipfw_objhash_destroy(CHAIN_TO_VI(ch));
}

