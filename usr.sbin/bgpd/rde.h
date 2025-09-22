/*	$OpenBSD: rde.h,v 1.316 2025/06/04 09:12:34 claudio Exp $ */

/*
 * Copyright (c) 2003, 2004 Claudio Jeker <claudio@openbsd.org> and
 *                          Andre Oppermann <oppermann@networx.ch>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __RDE_H__
#define __RDE_H__

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <stdint.h>
#include <stddef.h>

#include "bgpd.h"
#include "log.h"

/* rde internal structures */

enum peer_state {
	PEER_NONE,
	PEER_DOWN,
	PEER_UP,
	PEER_ERR	/* error occurred going to PEER_DOWN state */
};

LIST_HEAD(prefix_list, prefix);
TAILQ_HEAD(prefix_queue, prefix);
RB_HEAD(rib_tree, rib_entry);

struct rib_entry {
	RB_ENTRY(rib_entry)	 rib_e;
	struct prefix_queue	 prefix_h;
	struct pt_entry		*prefix;
	uint16_t		 rib_id;
	uint16_t		 lock;
};

struct rib {
	struct rib_tree		tree;
	char			name[PEER_DESCR_LEN];
	struct filter_head	*in_rules;
	struct filter_head	*in_rules_tmp;
	u_int			rtableid;
	u_int			rtableid_tmp;
	enum reconf_action	state, fibstate;
	uint16_t		id;
	uint16_t		flags;
	uint16_t		flags_tmp;
};

#define RIB_ADJ_IN	0
#define RIB_LOC_START	1
#define RIB_NOTFOUND	0xffff

/*
 * How do we identify peers between the session handler and the rde?
 * Currently I assume that we can do that with the neighbor_ip...
 */
RB_HEAD(peer_tree, rde_peer);
RB_HEAD(prefix_tree, prefix);
RB_HEAD(prefix_index, prefix);
struct iq;

struct rde_peer {
	RB_ENTRY(rde_peer)		 entry;
	SIMPLEQ_HEAD(, iq)		 imsg_queue;
	struct peer_config		 conf;
	struct rde_peer_stats		 stats;
	struct bgpd_addr		 remote_addr;
	struct bgpd_addr		 local_v4_addr;
	struct bgpd_addr		 local_v6_addr;
	struct capabilities		 capa;
	struct addpath_eval		 eval;
	struct prefix_index		 adj_rib_out;
	struct prefix_tree		 updates[AID_MAX];
	struct prefix_tree		 withdraws[AID_MAX];
	struct filter_head		*out_rules;
	struct ibufqueue		*ibufq;
	monotime_t			 staletime[AID_MAX];
	uint32_t			 remote_bgpid;
	uint32_t			 path_id_tx;
	unsigned int			 local_if_scope;
	enum peer_state			 state;
	enum export_type		 export_type;
	enum role			 role;
	uint16_t			 loc_rib_id;
	uint16_t			 short_as;
	uint16_t			 mrt_idx;
	uint8_t				 recv_eor;	/* bitfield per AID */
	uint8_t				 sent_eor;	/* bitfield per AID */
	uint8_t				 reconf_out;	/* out filter changed */
	uint8_t				 reconf_rib;	/* rib changed */
	uint8_t				 throttled;
	uint8_t				 flags;
};

struct rde_aspa;
struct rde_aspa_state {
	uint8_t		onlyup;
	uint8_t		downup;
};

#define AS_SET			1
#define AS_SEQUENCE		2
#define AS_CONFED_SEQUENCE	3
#define AS_CONFED_SET		4
#define ASPATH_HEADER_SIZE	(offsetof(struct aspath, data))

struct aspath {
	uint32_t		source_as;	/* cached source_as */
	uint16_t		len;	/* total length of aspath in octets */
	uint16_t		ascnt;	/* number of AS hops in data */
	u_char			data[1]; /* placeholder for actual data */
};

enum attrtypes {
	ATTR_UNDEF,
	ATTR_ORIGIN,
	ATTR_ASPATH,
	ATTR_NEXTHOP,
	ATTR_MED,
	ATTR_LOCALPREF,
	ATTR_ATOMIC_AGGREGATE,
	ATTR_AGGREGATOR,
	ATTR_COMMUNITIES,
	ATTR_ORIGINATOR_ID,
	ATTR_CLUSTER_LIST,
	ATTR_MP_REACH_NLRI=14,
	ATTR_MP_UNREACH_NLRI=15,
	ATTR_EXT_COMMUNITIES=16,
	ATTR_AS4_PATH=17,
	ATTR_AS4_AGGREGATOR=18,
	ATTR_PMSI_TUNNEL=22,
	ATTR_LARGE_COMMUNITIES=32,
	ATTR_OTC=35,
	ATTR_FIRST_UNKNOWN,	/* after this all attributes are unknown */
};

/* attribute flags. 4 low order bits reserved */
#define	ATTR_EXTLEN		0x10
#define ATTR_PARTIAL		0x20
#define ATTR_TRANSITIVE		0x40
#define ATTR_OPTIONAL		0x80
#define ATTR_RESERVED		0x0f
/* by default mask the reserved bits and the ext len bit */
#define ATTR_DEFMASK		(ATTR_RESERVED | ATTR_EXTLEN)

/* default attribute flags for well-known attributes */
#define ATTR_WELL_KNOWN		ATTR_TRANSITIVE

struct attr {
	RB_ENTRY(attr)			 entry;
	u_char				*data;
	int				 refcnt;
	uint16_t			 len;
	uint8_t				 flags;
	uint8_t				 type;
};

struct rde_community {
	RB_ENTRY(rde_community)		entry;
	int				size;
	int				nentries;
	int				flags;
	int				refcnt;
	struct community		*communities;
};

#define	PARTIAL_COMMUNITIES		0x01
#define	PARTIAL_LARGE_COMMUNITIES	0x02
#define	PARTIAL_EXT_COMMUNITIES		0x04

#define	F_ATTR_ORIGIN		0x00001
#define	F_ATTR_ASPATH		0x00002
#define	F_ATTR_NEXTHOP		0x00004
#define	F_ATTR_LOCALPREF	0x00008
#define	F_ATTR_MED		0x00010
#define	F_ATTR_MED_ANNOUNCE	0x00020
#define	F_ATTR_MP_REACH		0x00040
#define	F_ATTR_MP_UNREACH	0x00080
#define	F_ATTR_AS4BYTE_NEW	0x00100	/* AS4_PATH or AS4_AGGREGATOR */
#define	F_ATTR_LOOP		0x00200 /* path would cause a route loop */
#define	F_PREFIX_ANNOUNCED	0x00400
#define	F_ANN_DYNAMIC		0x00800
#define	F_ATTR_OTC		0x01000	/* OTC present */
#define	F_ATTR_OTC_LEAK		0x02000 /* otc leak, not eligible */
#define	F_ATTR_PARSE_ERR	0x10000 /* parse error, not eligible */
#define	F_ATTR_LINKED		0x20000 /* if set path is on various lists */

#define ORIGIN_IGP		0
#define ORIGIN_EGP		1
#define ORIGIN_INCOMPLETE	2

#define DEFAULT_LPREF		100

struct rde_aspath {
	RB_ENTRY(rde_aspath)		 entry;
	struct attr			**others;
	struct aspath			*aspath;
	struct rde_aspa_state		 aspa_state;
	int				 refcnt;
	uint32_t			 flags;		/* internally used */
	uint32_t			 med;		/* multi exit disc */
	uint32_t			 lpref;		/* local pref */
	uint32_t			 weight;	/* low prio lpref */
	uint16_t			 rtlabelid;	/* route label id */
	uint16_t			 pftableid;	/* pf table id */
	uint8_t				 origin;
	uint8_t				 others_len;
	uint8_t				 aspa_generation;
};

enum nexthop_state {
	NEXTHOP_LOOKUP,
	NEXTHOP_UNREACH,
	NEXTHOP_REACH,
	NEXTHOP_FLAPPED		/* only used by oldstate */
};

struct nexthop {
	RB_ENTRY(nexthop)	entry;
	TAILQ_ENTRY(nexthop)	runner_l;
	struct prefix_list	prefix_h;
	struct prefix		*next_prefix;
	struct bgpd_addr	exit_nexthop;
	struct bgpd_addr	true_nexthop;
	struct bgpd_addr	nexthop_net;
#if 0
	/*
	 * currently we use the boolean nexthop state, this could be exchanged
	 * with a variable cost with a max for unreachable.
	 */
	uint32_t		costs;
#endif
	int			refcnt;
	enum nexthop_state	state;
	enum nexthop_state	oldstate;
	uint8_t			nexthop_netlen;
	uint8_t			flags;
#define NEXTHOP_CONNECTED	0x01
};

/* generic entry without address specific part */
struct pt_entry {
	RB_ENTRY(pt_entry)		 pt_e;
	uint8_t				 aid;
	uint8_t				 prefixlen;
	uint16_t			 len;
	uint32_t			 refcnt;
	uint8_t				 data[4]; /* data depending on aid */
};

struct prefix {
	union {
		struct {
			TAILQ_ENTRY(prefix)	 rib;
			LIST_ENTRY(prefix)	 nexthop;
			struct rib_entry	*re;
		} list;
		struct {
			RB_ENTRY(prefix)	 index, update;
		} tree;
	}				 entry;
	struct pt_entry			*pt;
	struct rde_aspath		*aspath;
	struct rde_community		*communities;
	struct rde_peer			*peer;
	struct nexthop			*nexthop;	/* may be NULL */
	monotime_t			 lastchange;
	uint32_t			 path_id;
	uint32_t			 path_id_tx;
	uint16_t			 flags;
	uint8_t				 validation_state;
	uint8_t				 nhflags;
	int8_t				 dmetric;	/* decision metric */
};
#define	PREFIX_FLAG_WITHDRAW	0x0001	/* enqueued on withdraw queue */
#define	PREFIX_FLAG_UPDATE	0x0002	/* enqueued on update queue */
#define	PREFIX_FLAG_DEAD	0x0004	/* locked but removed */
#define	PREFIX_FLAG_STALE	0x0008	/* stale entry (for addpath) */
#define	PREFIX_FLAG_MASK	0x000f	/* mask for the prefix types */
#define	PREFIX_FLAG_ADJOUT	0x0010	/* prefix is in the adj-out rib */
#define	PREFIX_FLAG_EOR		0x0020	/* prefix is EoR */
#define	PREFIX_NEXTHOP_LINKED	0x0040	/* prefix is linked onto nexthop list */
#define	PREFIX_FLAG_LOCKED	0x0080	/* locked by rib walker */
#define	PREFIX_FLAG_FILTERED	0x0100	/* prefix is filtered (ineligible) */

#define	PREFIX_DMETRIC_NONE	0
#define	PREFIX_DMETRIC_INVALID	1
#define	PREFIX_DMETRIC_VALID	2
#define	PREFIX_DMETRIC_AS_WIDE	3
#define	PREFIX_DMETRIC_ECMP	4
#define	PREFIX_DMETRIC_BEST	5

/* possible states for nhflags */
#define	NEXTHOP_SELF		0x01
#define	NEXTHOP_REJECT		0x02
#define	NEXTHOP_BLACKHOLE	0x04
#define	NEXTHOP_NOMODIFY	0x08
#define	NEXTHOP_MASK		0x0f
#define	NEXTHOP_VALID		0x80

struct filterstate {
	struct rde_aspath	 aspath;
	struct rde_community	 communities;
	struct nexthop		*nexthop;
	uint8_t			 nhflags;
	uint8_t			 vstate;
};

enum eval_mode {
	EVAL_DEFAULT,
	EVAL_ALL,
	EVAL_RECONF,
};

extern struct rde_memstats rdemem;

/* prototypes */
/* mrt.c */
int		mrt_dump_v2_hdr(struct mrt *, struct bgpd_config *);
void		mrt_dump_upcall(struct rib_entry *, void *);

/* rde.c */
void		 rde_update_err(struct rde_peer *, uint8_t , uint8_t,
		    struct ibuf *);
void		 rde_update_log(const char *, uint16_t,
		    const struct rde_peer *, const struct bgpd_addr *,
		    const struct bgpd_addr *, uint8_t);
void		rde_send_kroute_flush(struct rib *);
void		rde_send_kroute(struct rib *, struct prefix *, struct prefix *);
void		rde_send_nexthop(struct bgpd_addr *, int);
void		rde_pftable_add(uint16_t, struct prefix *);
void		rde_pftable_del(uint16_t, struct prefix *);

int		rde_evaluate_all(void);
uint32_t	rde_local_as(void);
int		rde_decisionflags(void);
void		rde_peer_send_rrefresh(struct rde_peer *, uint8_t, uint8_t);
int		rde_match_peer(struct rde_peer *, struct ctl_neighbor *);

/* rde_peer.c */
int		 peer_has_as4byte(struct rde_peer *);
int		 peer_has_add_path(struct rde_peer *, uint8_t, int);
int		 peer_has_ext_msg(struct rde_peer *);
int		 peer_has_ext_nexthop(struct rde_peer *, uint8_t);
int		 peer_permit_as_set(struct rde_peer *);
void		 peer_init(struct filter_head *);
void		 peer_shutdown(void);
void		 peer_foreach(void (*)(struct rde_peer *, void *), void *);
struct rde_peer	*peer_get(uint32_t);
struct rde_peer *peer_match(struct ctl_neighbor *, uint32_t);
struct rde_peer	*peer_add(uint32_t, struct peer_config *, struct filter_head *);
struct filter_head	*peer_apply_out_filter(struct rde_peer *,
			    struct filter_head *);

void		 rde_generate_updates(struct rib_entry *, struct prefix *,
		    struct prefix *, enum eval_mode);

void		 peer_up(struct rde_peer *, struct session_up *);
void		 peer_down(struct rde_peer *);
void		 peer_delete(struct rde_peer *);
void		 peer_flush(struct rde_peer *, uint8_t, monotime_t);
void		 peer_stale(struct rde_peer *, uint8_t, int);
void		 peer_blast(struct rde_peer *, uint8_t);
void		 peer_dump(struct rde_peer *, uint8_t);
void		 peer_begin_rrefresh(struct rde_peer *, uint8_t);
int		 peer_work_pending(void);
void		 peer_reaper(struct rde_peer *);

void		 peer_imsg_push(struct rde_peer *, struct imsg *);
int		 peer_imsg_pop(struct rde_peer *, struct imsg *);
void		 peer_imsg_flush(struct rde_peer *);

static inline int
peer_is_up(struct rde_peer *peer)
{
	return (peer->state == PEER_UP);
}

RB_PROTOTYPE(peer_tree, rde_peer, entry, peer_cmp);

/* rde_attr.c */
int		 attr_writebuf(struct ibuf *, uint8_t, uint8_t, void *,
		    uint16_t);
void		 attr_shutdown(void);
int		 attr_optadd(struct rde_aspath *, uint8_t, uint8_t,
		    void *, uint16_t);
struct attr	*attr_optget(const struct rde_aspath *, uint8_t);
void		 attr_copy(struct rde_aspath *, const struct rde_aspath *);
int		 attr_compare(struct rde_aspath *, struct rde_aspath *);
void		 attr_freeall(struct rde_aspath *);
void		 attr_free(struct rde_aspath *, struct attr *);

struct aspath	*aspath_get(void *, uint16_t);
struct aspath	*aspath_copy(struct aspath *);
void		 aspath_put(struct aspath *);
u_char		*aspath_deflate(u_char *, uint16_t *, int *);
void		 aspath_merge(struct rde_aspath *, struct attr *);
uint32_t	 aspath_neighbor(struct aspath *);
int		 aspath_loopfree(struct aspath *, uint32_t);
int		 aspath_compare(struct aspath *, struct aspath *);
int		 aspath_match(struct aspath *, struct filter_as *, uint32_t);
u_char		*aspath_prepend(struct aspath *, uint32_t, int, uint16_t *);
u_char		*aspath_override(struct aspath *, uint32_t, uint32_t,
		    uint16_t *);
int		 aspath_lenmatch(struct aspath *, enum aslen_spec, u_int);

static inline u_char *
aspath_dump(struct aspath *aspath)
{
	return (aspath->data);
}

static inline uint16_t
aspath_length(struct aspath *aspath)
{
	return (aspath->len);
}

static inline uint32_t
aspath_origin(struct aspath *aspath)
{
	return (aspath->source_as);
}

/* rde_community.c */
int	community_match(struct rde_community *, struct community *,
	    struct rde_peer *);
int	community_count(struct rde_community *, uint8_t type);
int	community_set(struct rde_community *, struct community *,
	    struct rde_peer *);
void	community_delete(struct rde_community *, struct community *,
	    struct rde_peer *);

int	community_add(struct rde_community *, int, struct ibuf *);
int	community_large_add(struct rde_community *, int, struct ibuf *);
int	community_ext_add(struct rde_community *, int, int, struct ibuf *);
int	community_writebuf(struct rde_community *, uint8_t, int, struct ibuf *);

void			 communities_shutdown(void);
struct rde_community	*communities_lookup(struct rde_community *);
struct rde_community	*communities_link(struct rde_community *);
void			 communities_unlink(struct rde_community *);

int	 communities_equal(struct rde_community *, struct rde_community *);
void	 communities_copy(struct rde_community *, struct rde_community *);
void	 communities_clean(struct rde_community *);

static inline struct rde_community *
communities_ref(struct rde_community *comm)
{
	if (comm->refcnt == 0)
		fatalx("%s: not-referenced community", __func__);
	comm->refcnt++;
	rdemem.comm_refs++;
	return comm;
}

static inline void
communities_unref(struct rde_community *comm)
{
	if (comm == NULL)
		return;
	rdemem.comm_refs--;
	if (--comm->refcnt == 1)	/* last ref is hold internally */
		communities_unlink(comm);
}

int	community_to_rd(struct community *, uint64_t *);

/* rde_decide.c */
int		 prefix_eligible(struct prefix *);
struct prefix	*prefix_best(struct rib_entry *);
void		 prefix_evaluate(struct rib_entry *, struct prefix *,
		    struct prefix *);
void		 prefix_evaluate_nexthop(struct prefix *, enum nexthop_state,
		    enum nexthop_state);

/* rde_filter.c */
void	rde_apply_set(struct filter_set_head *, struct rde_peer *,
	    struct rde_peer *, struct filterstate *, u_int8_t);
void	rde_filterstate_init(struct filterstate *);
void	rde_filterstate_prep(struct filterstate *, struct prefix *);
void	rde_filterstate_copy(struct filterstate *, struct filterstate *);
void	rde_filterstate_set_vstate(struct filterstate *, uint8_t, uint8_t);
void	rde_filterstate_clean(struct filterstate *);
int	rde_filter_skip_rule(struct rde_peer *, struct filter_rule *);
int	rde_filter_equal(struct filter_head *, struct filter_head *);
void	rde_filter_calc_skip_steps(struct filter_head *);
enum filter_actions rde_filter(struct filter_head *, struct rde_peer *,
	    struct rde_peer *, struct bgpd_addr *, uint8_t,
	    struct filterstate *);

/* rde_prefix.c */
void	 pt_init(void);
void	 pt_shutdown(void);
void	 pt_getaddr(struct pt_entry *, struct bgpd_addr *);
int	 pt_getflowspec(struct pt_entry *, uint8_t **);
struct pt_entry	*pt_fill(struct bgpd_addr *, int);
struct pt_entry	*pt_get(struct bgpd_addr *, int);
struct pt_entry *pt_add(struct bgpd_addr *, int);
struct pt_entry	*pt_get_flow(struct flowspec *);
struct pt_entry	*pt_add_flow(struct flowspec *);
void	 pt_remove(struct pt_entry *);
struct pt_entry	*pt_lookup(struct bgpd_addr *);
int	 pt_prefix_cmp(const struct pt_entry *, const struct pt_entry *);
int	 pt_writebuf(struct ibuf *, struct pt_entry *, int, int, uint32_t);

static inline struct pt_entry *
pt_ref(struct pt_entry *pt)
{
	++pt->refcnt;
	if (pt->refcnt == 0)
		fatalx("pt_ref: overflow");
	return pt;
}

static inline void
pt_unref(struct pt_entry *pt)
{
	if (pt->refcnt == 0)
		fatalx("pt_unref: underflow");
	if (--pt->refcnt == 0)
		pt_remove(pt);
}

/* rde_rib.c */
extern uint16_t	rib_size;

struct rib	*rib_new(char *, u_int, uint16_t);
int		 rib_update(struct rib *);
struct rib	*rib_byid(uint16_t);
uint16_t	 rib_find(char *);
void		 rib_free(struct rib *);
void		 rib_shutdown(void);
struct rib_entry *rib_get(struct rib *, struct pt_entry *);
struct rib_entry *rib_get_addr(struct rib *, struct bgpd_addr *, int);
struct rib_entry *rib_match(struct rib *, struct bgpd_addr *);
int		 rib_dump_pending(void);
void		 rib_dump_runner(void);
int		 rib_dump_new(uint16_t, uint8_t, unsigned int, void *,
		    void (*)(struct rib_entry *, void *),
		    void (*)(void *, uint8_t),
		    int (*)(void *));
int		 rib_dump_subtree(uint16_t, struct bgpd_addr *, uint8_t,
		    unsigned int count, void *arg,
		    void (*)(struct rib_entry *, void *),
		    void (*)(void *, uint8_t),
		    int (*)(void *));
void		 rib_dump_terminate(void *);

extern struct rib flowrib;

static inline struct rib *
re_rib(struct rib_entry *re)
{
	if (re->prefix->aid == AID_FLOWSPECv4 ||
	    re->prefix->aid == AID_FLOWSPECv6)
		return &flowrib;
	return rib_byid(re->rib_id);
}

void		 path_shutdown(void);
struct rde_aspath *path_copy(struct rde_aspath *, const struct rde_aspath *);
struct rde_aspath *path_prep(struct rde_aspath *);
struct rde_aspath *path_get(void);
void		 path_clean(struct rde_aspath *);
void		 path_put(struct rde_aspath *);

#define	PREFIX_SIZE(x)	(((x) + 7) / 8 + 1)
struct prefix	*prefix_get(struct rib *, struct rde_peer *, uint32_t,
		    struct bgpd_addr *, int);
struct prefix	*prefix_adjout_get(struct rde_peer *, uint32_t,
		    struct pt_entry *);
struct prefix	*prefix_adjout_first(struct rde_peer *, struct pt_entry *);
struct prefix	*prefix_adjout_next(struct rde_peer *, struct prefix *);
struct prefix	*prefix_adjout_lookup(struct rde_peer *, struct bgpd_addr *,
		    int);
struct prefix	*prefix_adjout_match(struct rde_peer *, struct bgpd_addr *);
int		 prefix_update(struct rib *, struct rde_peer *, uint32_t,
		    uint32_t, struct filterstate *, int, struct bgpd_addr *,
		    int);
int		 prefix_withdraw(struct rib *, struct rde_peer *, uint32_t,
		    struct bgpd_addr *, int);
int		 prefix_flowspec_update(struct rde_peer *, struct filterstate *,
		    struct pt_entry *, uint32_t);
int		 prefix_flowspec_withdraw(struct rde_peer *, struct pt_entry *);
void		 prefix_flowspec_dump(uint8_t, void *,
		    void (*)(struct rib_entry *, void *),
		    void (*)(void *, uint8_t));
void		 prefix_add_eor(struct rde_peer *, uint8_t);
void		 prefix_adjout_update(struct prefix *, struct rde_peer *,
		    struct filterstate *, struct pt_entry *, uint32_t);
void		 prefix_adjout_withdraw(struct prefix *);
void		 prefix_adjout_destroy(struct prefix *);
void		 prefix_adjout_flush_pending(struct rde_peer *);
int		 prefix_adjout_reaper(struct rde_peer *);
int		 prefix_dump_new(struct rde_peer *, uint8_t, unsigned int,
		    void *, void (*)(struct prefix *, void *),
		    void (*)(void *, uint8_t), int (*)(void *));
int		 prefix_dump_subtree(struct rde_peer *, struct bgpd_addr *,
		    uint8_t, unsigned int, void *,
		    void (*)(struct prefix *, void *),
		    void (*)(void *, uint8_t), int (*)(void *));
struct prefix	*prefix_bypeer(struct rib_entry *, struct rde_peer *,
		    uint32_t);
void		 prefix_destroy(struct prefix *);

RB_PROTOTYPE(prefix_tree, prefix, entry, prefix_cmp)

static inline struct rde_peer *
prefix_peer(struct prefix *p)
{
	return (p->peer);
}

static inline struct rde_aspath *
prefix_aspath(struct prefix *p)
{
	return (p->aspath);
}

static inline struct rde_community *
prefix_communities(struct prefix *p)
{
	return (p->communities);
}

static inline struct nexthop *
prefix_nexthop(struct prefix *p)
{
	return (p->nexthop);
}

static inline uint8_t
prefix_nhflags(struct prefix *p)
{
	return (p->nhflags & NEXTHOP_MASK);
}

static inline int
prefix_nhvalid(struct prefix *p)
{
	return ((p->nhflags & NEXTHOP_VALID) != 0);
}

static inline uint8_t
prefix_roa_vstate(struct prefix *p)
{
	return (p->validation_state & ROA_MASK);
}

static inline uint8_t
prefix_aspa_vstate(struct prefix *p)
{
	return (p->validation_state >> 4);
}

static inline void
prefix_set_vstate(struct prefix *p, uint8_t roa_vstate, uint8_t aspa_vstate)
{
	p->validation_state = roa_vstate & ROA_MASK;
	p->validation_state |= aspa_vstate << 4;
}

static inline struct rib_entry *
prefix_re(struct prefix *p)
{
	if (p->flags & PREFIX_FLAG_ADJOUT)
		return NULL;
	return (p->entry.list.re);
}

static inline int
prefix_filtered(struct prefix *p)
{
	return ((p->flags & PREFIX_FLAG_FILTERED) != 0);
}

void		 nexthop_shutdown(void);
int		 nexthop_pending(void);
void		 nexthop_runner(void);
void		 nexthop_modify(struct nexthop *, enum action_types, uint8_t,
		    struct nexthop **, uint8_t *);
void		 nexthop_link(struct prefix *);
void		 nexthop_unlink(struct prefix *);
void		 nexthop_update(struct kroute_nexthop *);
struct nexthop	*nexthop_get(struct bgpd_addr *);
struct nexthop	*nexthop_ref(struct nexthop *);
int		 nexthop_unref(struct nexthop *);

/* rde_update.c */
void		 up_generate_updates(struct rde_peer *, struct rib_entry *);
void		 up_generate_addpath(struct rde_peer *, struct rib_entry *);
void		 up_generate_addpath_all(struct rde_peer *, struct rib_entry *,
		    struct prefix *, struct prefix *);
void		 up_generate_default(struct rde_peer *, uint8_t);
int		 up_is_eor(struct rde_peer *, uint8_t);
void		 up_dump_withdraws(struct imsgbuf *, struct rde_peer *,
		    uint8_t);
void		 up_dump_update(struct imsgbuf *, struct rde_peer *, uint8_t);

/* rde_aspa.c */
void		 aspa_validation(struct rde_aspa *, struct aspath *,
		    struct rde_aspa_state *);
struct rde_aspa	*aspa_table_prep(uint32_t, size_t);
void		 aspa_add_set(struct rde_aspa *, uint32_t, const uint32_t *,
		    uint32_t);
void		 aspa_table_free(struct rde_aspa *);
void		 aspa_table_stats(const struct rde_aspa *,
		    struct ctl_show_set *);
int		 aspa_table_equal(const struct rde_aspa *,
		    const struct rde_aspa *);
void		 aspa_table_unchanged(struct rde_aspa *,
		    const struct rde_aspa *);

#endif /* __RDE_H__ */
