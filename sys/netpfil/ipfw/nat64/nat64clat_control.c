/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Yandex LLC
 * Copyright (c) 2019 Andrey V. Elsukov <ae@FreeBSD.org>
 * Copyright (c) 2019 Boris N. Lytochkin <lytboris@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rmlock.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/sockopt.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet/ip_fw.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#include <netinet6/ip_fw_nat64.h>

#include <netpfil/ipfw/ip_fw_private.h>

#include "nat64clat.h"

VNET_DEFINE(uint16_t, nat64clat_eid) = 0;

static struct nat64clat_cfg *nat64clat_alloc_config(const char *name,
    uint8_t set);
static void nat64clat_free_config(struct nat64clat_cfg *cfg);
static struct nat64clat_cfg *nat64clat_find(struct namedobj_instance *ni,
    const char *name, uint8_t set);

static struct nat64clat_cfg *
nat64clat_alloc_config(const char *name, uint8_t set)
{
	struct nat64clat_cfg *cfg;

	cfg = malloc(sizeof(struct nat64clat_cfg), M_IPFW, M_WAITOK | M_ZERO);
	COUNTER_ARRAY_ALLOC(cfg->base.stats.cnt, NAT64STATS, M_WAITOK);
	cfg->no.name = cfg->name;
	cfg->no.etlv = IPFW_TLV_NAT64CLAT_NAME;
	cfg->no.set = set;
	strlcpy(cfg->name, name, sizeof(cfg->name));
	return (cfg);
}

static void
nat64clat_free_config(struct nat64clat_cfg *cfg)
{

	COUNTER_ARRAY_FREE(cfg->base.stats.cnt, NAT64STATS);
	free(cfg, M_IPFW);
}

static void
nat64clat_export_config(struct ip_fw_chain *ch, struct nat64clat_cfg *cfg,
    ipfw_nat64clat_cfg *uc)
{
	uc->plat_prefix = cfg->base.plat_prefix;
	uc->plat_plen = cfg->base.plat_plen;
	uc->clat_prefix = cfg->base.clat_prefix;
	uc->clat_plen = cfg->base.clat_plen;
	uc->flags = cfg->base.flags & NAT64CLAT_FLAGSMASK;
	uc->set = cfg->no.set;
	strlcpy(uc->name, cfg->no.name, sizeof(uc->name));
}

struct nat64clat_dump_arg {
	struct ip_fw_chain *ch;
	struct sockopt_data *sd;
};

static int
export_config_cb(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{
	struct nat64clat_dump_arg *da = (struct nat64clat_dump_arg *)arg;
	ipfw_nat64clat_cfg *uc;

	uc = (ipfw_nat64clat_cfg *)ipfw_get_sopt_space(da->sd, sizeof(*uc));
	nat64clat_export_config(da->ch, (struct nat64clat_cfg *)no, uc);
	return (0);
}

static struct nat64clat_cfg *
nat64clat_find(struct namedobj_instance *ni, const char *name, uint8_t set)
{
	struct nat64clat_cfg *cfg;

	cfg = (struct nat64clat_cfg *)ipfw_objhash_lookup_name_type(ni, set,
	    IPFW_TLV_NAT64CLAT_NAME, name);

	return (cfg);
}

/*
 * Creates new consumer-side nat64 translator instance.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_lheader ipfw_nat64clat_cfg ]
 *
 * Returns 0 on success
 */
static int
nat64clat_create(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_lheader *olh;
	ipfw_nat64clat_cfg *uc;
	struct namedobj_instance *ni;
	struct nat64clat_cfg *cfg;

	if (sd->valsize != sizeof(*olh) + sizeof(*uc))
		return (EINVAL);

	olh = (ipfw_obj_lheader *)sd->kbuf;
	uc = (ipfw_nat64clat_cfg *)(olh + 1);

	if (ipfw_check_object_name_generic(uc->name) != 0)
		return (EINVAL);

	if (uc->set >= IPFW_MAX_SETS ||
	    nat64_check_prefix6(&uc->plat_prefix, uc->plat_plen) != 0 ||
	    nat64_check_prefix6(&uc->clat_prefix, uc->clat_plen) != 0)
		return (EINVAL);

	ni = CHAIN_TO_SRV(ch);

	IPFW_UH_RLOCK(ch);
	if (nat64clat_find(ni, uc->name, uc->set) != NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (EEXIST);
	}
	IPFW_UH_RUNLOCK(ch);

	cfg = nat64clat_alloc_config(uc->name, uc->set);
	cfg->base.plat_prefix = uc->plat_prefix;
	cfg->base.plat_plen = uc->plat_plen;
	cfg->base.clat_prefix = uc->clat_prefix;
	cfg->base.clat_plen = uc->clat_plen;
	cfg->base.flags = (uc->flags & NAT64CLAT_FLAGSMASK) |
	    NAT64_CLATPFX | NAT64_PLATPFX;
	if (IN6_IS_ADDR_WKPFX(&cfg->base.plat_prefix))
		cfg->base.flags |= NAT64_WKPFX;

	IPFW_UH_WLOCK(ch);

	if (nat64clat_find(ni, uc->name, uc->set) != NULL) {
		IPFW_UH_WUNLOCK(ch);
		nat64clat_free_config(cfg);
		return (EEXIST);
	}

	if (ipfw_objhash_alloc_idx(ni, &cfg->no.kidx) != 0) {
		IPFW_UH_WUNLOCK(ch);
		nat64clat_free_config(cfg);
		return (ENOSPC);
	}
	ipfw_objhash_add(CHAIN_TO_SRV(ch), &cfg->no);
	/* Okay, let's link data */
	SRV_OBJECT(ch, cfg->no.kidx) = cfg;
	IPFW_UH_WUNLOCK(ch);

	return (0);
}

/*
 * Change existing nat64clat instance configuration.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ipfw_nat64clat_cfg ]
 * Reply: [ ipfw_obj_header ipfw_nat64clat_cfg ]
 *
 * Returns 0 on success
 */
static int
nat64clat_config(struct ip_fw_chain *ch, ip_fw3_opheader *op,
    struct sockopt_data *sd)
{
	ipfw_obj_header *oh;
	ipfw_nat64clat_cfg *uc;
	struct nat64clat_cfg *cfg;
	struct namedobj_instance *ni;
	uint32_t flags;

	if (sd->valsize != sizeof(*oh) + sizeof(*uc))
		return (EINVAL);

	oh = (ipfw_obj_header *)ipfw_get_sopt_space(sd,
	    sizeof(*oh) + sizeof(*uc));
	uc = (ipfw_nat64clat_cfg *)(oh + 1);

	if (ipfw_check_object_name_generic(oh->ntlv.name) != 0 ||
	    oh->ntlv.set >= IPFW_MAX_SETS)
		return (EINVAL);

	ni = CHAIN_TO_SRV(ch);
	if (sd->sopt->sopt_dir == SOPT_GET) {
		IPFW_UH_RLOCK(ch);
		cfg = nat64clat_find(ni, oh->ntlv.name, oh->ntlv.set);
		if (cfg == NULL) {
			IPFW_UH_RUNLOCK(ch);
			return (ENOENT);
		}
		nat64clat_export_config(ch, cfg, uc);
		IPFW_UH_RUNLOCK(ch);
		return (0);
	}

	IPFW_UH_WLOCK(ch);
	cfg = nat64clat_find(ni, oh->ntlv.name, oh->ntlv.set);
	if (cfg == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ENOENT);
	}

	/*
	 * For now allow to change only following values:
	 *  plat_prefix, plat_plen, clat_prefix, clat_plen, flags.
	 */
	flags = 0;
	if (uc->plat_plen != cfg->base.plat_plen ||
	    !IN6_ARE_ADDR_EQUAL(&uc->plat_prefix, &cfg->base.plat_prefix)) {
		if (nat64_check_prefix6(&uc->plat_prefix, uc->plat_plen) != 0) {
			IPFW_UH_WUNLOCK(ch);
			return (EINVAL);
		}
		flags |= NAT64_PLATPFX;
	}

	if (uc->clat_plen != cfg->base.clat_plen ||
	    !IN6_ARE_ADDR_EQUAL(&uc->clat_prefix, &cfg->base.clat_prefix)) {
		if (nat64_check_prefix6(&uc->clat_prefix, uc->clat_plen) != 0) {
			IPFW_UH_WUNLOCK(ch);
			return (EINVAL);
		}
		flags |= NAT64_CLATPFX;
	}

	if (flags != 0) {
		IPFW_WLOCK(ch);
		if (flags & NAT64_PLATPFX) {
			cfg->base.plat_prefix = uc->plat_prefix;
			cfg->base.plat_plen = uc->plat_plen;
		}
		if (flags & NAT64_CLATPFX) {
			cfg->base.clat_prefix = uc->clat_prefix;
			cfg->base.clat_plen = uc->clat_plen;
		}
		IPFW_WUNLOCK(ch);
	}

	cfg->base.flags &= ~NAT64CLAT_FLAGSMASK;
	cfg->base.flags |= uc->flags & NAT64CLAT_FLAGSMASK;

	IPFW_UH_WUNLOCK(ch);
	return (0);
}

static void
nat64clat_detach_config(struct ip_fw_chain *ch, struct nat64clat_cfg *cfg)
{

	IPFW_UH_WLOCK_ASSERT(ch);

	ipfw_objhash_del(CHAIN_TO_SRV(ch), &cfg->no);
	ipfw_objhash_free_idx(CHAIN_TO_SRV(ch), cfg->no.kidx);
}

/*
 * Destroys nat64 instance.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ]
 *
 * Returns 0 on success
 */
static int
nat64clat_destroy(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_header *oh;
	struct nat64clat_cfg *cfg;

	if (sd->valsize != sizeof(*oh))
		return (EINVAL);

	oh = (ipfw_obj_header *)sd->kbuf;
	if (ipfw_check_object_name_generic(oh->ntlv.name) != 0)
		return (EINVAL);

	IPFW_UH_WLOCK(ch);
	cfg = nat64clat_find(CHAIN_TO_SRV(ch), oh->ntlv.name, oh->ntlv.set);
	if (cfg == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ENOENT);
	}
	if (cfg->no.refcnt > 0) {
		IPFW_UH_WUNLOCK(ch);
		return (EBUSY);
	}

	ipfw_reset_eaction_instance(ch, V_nat64clat_eid, cfg->no.kidx);
	SRV_OBJECT(ch, cfg->no.kidx) = NULL;
	nat64clat_detach_config(ch, cfg);
	IPFW_UH_WUNLOCK(ch);

	nat64clat_free_config(cfg);
	return (0);
}

/*
 * Lists all nat64clat instances currently available in kernel.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_lheader ]
 * Reply: [ ipfw_obj_lheader ipfw_nat64clat_cfg x N ]
 *
 * Returns 0 on success
 */
static int
nat64clat_list(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd)
{
	ipfw_obj_lheader *olh;
	struct nat64clat_dump_arg da;

	/* Check minimum header size */
	if (sd->valsize < sizeof(ipfw_obj_lheader))
		return (EINVAL);

	olh = (ipfw_obj_lheader *)ipfw_get_sopt_header(sd, sizeof(*olh));

	IPFW_UH_RLOCK(ch);
	olh->count = ipfw_objhash_count_type(CHAIN_TO_SRV(ch),
	    IPFW_TLV_NAT64CLAT_NAME);
	olh->objsize = sizeof(ipfw_nat64clat_cfg);
	olh->size = sizeof(*olh) + olh->count * olh->objsize;

	if (sd->valsize < olh->size) {
		IPFW_UH_RUNLOCK(ch);
		return (ENOMEM);
	}
	memset(&da, 0, sizeof(da));
	da.ch = ch;
	da.sd = sd;
	ipfw_objhash_foreach_type(CHAIN_TO_SRV(ch), export_config_cb,
	    &da, IPFW_TLV_NAT64CLAT_NAME);
	IPFW_UH_RUNLOCK(ch);

	return (0);
}

#define	__COPY_STAT_FIELD(_cfg, _stats, _field)	\
	(_stats)->_field = NAT64STAT_FETCH(&(_cfg)->base.stats, _field)
static void
export_stats(struct ip_fw_chain *ch, struct nat64clat_cfg *cfg,
    struct ipfw_nat64clat_stats *stats)
{

	__COPY_STAT_FIELD(cfg, stats, opcnt64);
	__COPY_STAT_FIELD(cfg, stats, opcnt46);
	__COPY_STAT_FIELD(cfg, stats, ofrags);
	__COPY_STAT_FIELD(cfg, stats, ifrags);
	__COPY_STAT_FIELD(cfg, stats, oerrors);
	__COPY_STAT_FIELD(cfg, stats, noroute4);
	__COPY_STAT_FIELD(cfg, stats, noroute6);
	__COPY_STAT_FIELD(cfg, stats, noproto);
	__COPY_STAT_FIELD(cfg, stats, nomem);
	__COPY_STAT_FIELD(cfg, stats, dropped);
}

/*
 * Get nat64clat statistics.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ]
 * Reply: [ ipfw_obj_header ipfw_obj_ctlv [ uint64_t x N ]]
 *
 * Returns 0 on success
 */
static int
nat64clat_stats(struct ip_fw_chain *ch, ip_fw3_opheader *op,
    struct sockopt_data *sd)
{
	struct ipfw_nat64clat_stats stats;
	struct nat64clat_cfg *cfg;
	ipfw_obj_header *oh;
	ipfw_obj_ctlv *ctlv;
	size_t sz;

	sz = sizeof(ipfw_obj_header) + sizeof(ipfw_obj_ctlv) + sizeof(stats);
	if (sd->valsize % sizeof(uint64_t))
		return (EINVAL);
	if (sd->valsize < sz)
		return (ENOMEM);
	oh = (ipfw_obj_header *)ipfw_get_sopt_header(sd, sz);
	if (oh == NULL)
		return (EINVAL);
	memset(&stats, 0, sizeof(stats));

	IPFW_UH_RLOCK(ch);
	cfg = nat64clat_find(CHAIN_TO_SRV(ch), oh->ntlv.name, oh->ntlv.set);
	if (cfg == NULL) {
		IPFW_UH_RUNLOCK(ch);
		return (ENOENT);
	}
	export_stats(ch, cfg, &stats);
	IPFW_UH_RUNLOCK(ch);

	ctlv = (ipfw_obj_ctlv *)(oh + 1);
	memset(ctlv, 0, sizeof(*ctlv));
	ctlv->head.type = IPFW_TLV_COUNTERS;
	ctlv->head.length = sz - sizeof(ipfw_obj_header);
	ctlv->count = sizeof(stats) / sizeof(uint64_t);
	ctlv->objsize = sizeof(uint64_t);
	ctlv->version = IPFW_NAT64_VERSION;
	memcpy(ctlv + 1, &stats, sizeof(stats));
	return (0);
}

/*
 * Reset nat64clat statistics.
 * Data layout (v0)(current):
 * Request: [ ipfw_obj_header ]
 *
 * Returns 0 on success
 */
static int
nat64clat_reset_stats(struct ip_fw_chain *ch, ip_fw3_opheader *op,
    struct sockopt_data *sd)
{
	struct nat64clat_cfg *cfg;
	ipfw_obj_header *oh;

	if (sd->valsize != sizeof(*oh))
		return (EINVAL);
	oh = (ipfw_obj_header *)sd->kbuf;
	if (ipfw_check_object_name_generic(oh->ntlv.name) != 0 ||
	    oh->ntlv.set >= IPFW_MAX_SETS)
		return (EINVAL);

	IPFW_UH_WLOCK(ch);
	cfg = nat64clat_find(CHAIN_TO_SRV(ch), oh->ntlv.name, oh->ntlv.set);
	if (cfg == NULL) {
		IPFW_UH_WUNLOCK(ch);
		return (ENOENT);
	}
	COUNTER_ARRAY_ZERO(cfg->base.stats.cnt, NAT64STATS);
	IPFW_UH_WUNLOCK(ch);
	return (0);
}

static struct ipfw_sopt_handler	scodes[] = {

	{ IP_FW_NAT64CLAT_CREATE, 0,	HDIR_SET,	nat64clat_create },
	{ IP_FW_NAT64CLAT_DESTROY,0,	HDIR_SET,	nat64clat_destroy },
	{ IP_FW_NAT64CLAT_CONFIG, 0,	HDIR_BOTH,	nat64clat_config },
	{ IP_FW_NAT64CLAT_LIST,   0,	HDIR_GET,	nat64clat_list },
	{ IP_FW_NAT64CLAT_STATS,  0,	HDIR_GET,	nat64clat_stats },
	{ IP_FW_NAT64CLAT_RESET_STATS,0,	HDIR_SET,	nat64clat_reset_stats },
};

static int
nat64clat_classify(ipfw_insn *cmd, uint16_t *puidx, uint8_t *ptype)
{
	ipfw_insn *icmd;

	icmd = cmd - 1;
	if (icmd->opcode != O_EXTERNAL_ACTION ||
	    icmd->arg1 != V_nat64clat_eid)
		return (1);

	*puidx = cmd->arg1;
	*ptype = 0;
	return (0);
}

static void
nat64clat_update_arg1(ipfw_insn *cmd, uint16_t idx)
{

	cmd->arg1 = idx;
}

static int
nat64clat_findbyname(struct ip_fw_chain *ch, struct tid_info *ti,
    struct named_object **pno)
{
	int err;

	err = ipfw_objhash_find_type(CHAIN_TO_SRV(ch), ti,
	    IPFW_TLV_NAT64CLAT_NAME, pno);
	return (err);
}

static struct named_object *
nat64clat_findbykidx(struct ip_fw_chain *ch, uint16_t idx)
{
	struct namedobj_instance *ni;
	struct named_object *no;

	IPFW_UH_WLOCK_ASSERT(ch);
	ni = CHAIN_TO_SRV(ch);
	no = ipfw_objhash_lookup_kidx(ni, idx);
	KASSERT(no != NULL, ("NAT with index %d not found", idx));

	return (no);
}

static int
nat64clat_manage_sets(struct ip_fw_chain *ch, uint16_t set, uint8_t new_set,
    enum ipfw_sets_cmd cmd)
{

	return (ipfw_obj_manage_sets(CHAIN_TO_SRV(ch), IPFW_TLV_NAT64CLAT_NAME,
	    set, new_set, cmd));
}

static struct opcode_obj_rewrite opcodes[] = {
	{
		.opcode = O_EXTERNAL_INSTANCE,
		.etlv = IPFW_TLV_EACTION /* just show it isn't table */,
		.classifier = nat64clat_classify,
		.update = nat64clat_update_arg1,
		.find_byname = nat64clat_findbyname,
		.find_bykidx = nat64clat_findbykidx,
		.manage_sets = nat64clat_manage_sets,
	},
};

static int
destroy_config_cb(struct namedobj_instance *ni, struct named_object *no,
    void *arg)
{
	struct nat64clat_cfg *cfg;
	struct ip_fw_chain *ch;

	ch = (struct ip_fw_chain *)arg;
	cfg = (struct nat64clat_cfg *)SRV_OBJECT(ch, no->kidx);
	SRV_OBJECT(ch, no->kidx) = NULL;
	nat64clat_detach_config(ch, cfg);
	nat64clat_free_config(cfg);
	return (0);
}

int
nat64clat_init(struct ip_fw_chain *ch, int first)
{

	V_nat64clat_eid = ipfw_add_eaction(ch, ipfw_nat64clat, "nat64clat");
	if (V_nat64clat_eid == 0)
		return (ENXIO);
	IPFW_ADD_SOPT_HANDLER(first, scodes);
	IPFW_ADD_OBJ_REWRITER(first, opcodes);
	return (0);
}

void
nat64clat_uninit(struct ip_fw_chain *ch, int last)
{

	IPFW_DEL_OBJ_REWRITER(last, opcodes);
	IPFW_DEL_SOPT_HANDLER(last, scodes);
	ipfw_del_eaction(ch, V_nat64clat_eid);
	/*
	 * Since we already have deregistered external action,
	 * our named objects become unaccessible via rules, because
	 * all rules were truncated by ipfw_del_eaction().
	 * So, we can unlink and destroy our named objects without holding
	 * IPFW_WLOCK().
	 */
	IPFW_UH_WLOCK(ch);
	ipfw_objhash_foreach_type(CHAIN_TO_SRV(ch), destroy_config_cb, ch,
	    IPFW_TLV_NAT64CLAT_NAME);
	V_nat64clat_eid = 0;
	IPFW_UH_WUNLOCK(ch);
}

