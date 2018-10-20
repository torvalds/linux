/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BLK_CGROUP_H
#define _BLK_CGROUP_H
/*
 * Common Block IO controller cgroup interface
 *
 * Based on ideas and code from CFQ, CFS and BFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2009 Vivek Goyal <vgoyal@redhat.com>
 * 	              Nauman Rafique <nauman@google.com>
 */

#include <linux/cgroup.h>
#include <linux/percpu_counter.h>
#include <linux/seq_file.h>
#include <linux/radix-tree.h>
#include <linux/blkdev.h>
#include <linux/atomic.h>
#include <linux/kthread.h>

/* percpu_counter batch for blkg_[rw]stats, per-cpu drift doesn't matter */
#define BLKG_STAT_CPU_BATCH	(INT_MAX / 2)

/* Max limits for throttle policy */
#define THROTL_IOPS_MAX		UINT_MAX

#ifdef CONFIG_BLK_CGROUP

enum blkg_rwstat_type {
	BLKG_RWSTAT_READ,
	BLKG_RWSTAT_WRITE,
	BLKG_RWSTAT_SYNC,
	BLKG_RWSTAT_ASYNC,
	BLKG_RWSTAT_DISCARD,

	BLKG_RWSTAT_NR,
	BLKG_RWSTAT_TOTAL = BLKG_RWSTAT_NR,
};

struct blkcg_gq;

struct blkcg {
	struct cgroup_subsys_state	css;
	spinlock_t			lock;

	struct radix_tree_root		blkg_tree;
	struct blkcg_gq	__rcu		*blkg_hint;
	struct hlist_head		blkg_list;

	struct blkcg_policy_data	*cpd[BLKCG_MAX_POLS];

	struct list_head		all_blkcgs_node;
#ifdef CONFIG_CGROUP_WRITEBACK
	struct list_head		cgwb_list;
	refcount_t			cgwb_refcnt;
#endif
};

/*
 * blkg_[rw]stat->aux_cnt is excluded for local stats but included for
 * recursive.  Used to carry stats of dead children, and, for blkg_rwstat,
 * to carry result values from read and sum operations.
 */
struct blkg_stat {
	struct percpu_counter		cpu_cnt;
	atomic64_t			aux_cnt;
};

struct blkg_rwstat {
	struct percpu_counter		cpu_cnt[BLKG_RWSTAT_NR];
	atomic64_t			aux_cnt[BLKG_RWSTAT_NR];
};

/*
 * A blkcg_gq (blkg) is association between a block cgroup (blkcg) and a
 * request_queue (q).  This is used by blkcg policies which need to track
 * information per blkcg - q pair.
 *
 * There can be multiple active blkcg policies and each blkg:policy pair is
 * represented by a blkg_policy_data which is allocated and freed by each
 * policy's pd_alloc/free_fn() methods.  A policy can allocate private data
 * area by allocating larger data structure which embeds blkg_policy_data
 * at the beginning.
 */
struct blkg_policy_data {
	/* the blkg and policy id this per-policy data belongs to */
	struct blkcg_gq			*blkg;
	int				plid;
};

/*
 * Policies that need to keep per-blkcg data which is independent from any
 * request_queue associated to it should implement cpd_alloc/free_fn()
 * methods.  A policy can allocate private data area by allocating larger
 * data structure which embeds blkcg_policy_data at the beginning.
 * cpd_init() is invoked to let each policy handle per-blkcg data.
 */
struct blkcg_policy_data {
	/* the blkcg and policy id this per-policy data belongs to */
	struct blkcg			*blkcg;
	int				plid;
};

/* association between a blk cgroup and a request queue */
struct blkcg_gq {
	/* Pointer to the associated request_queue */
	struct request_queue		*q;
	struct list_head		q_node;
	struct hlist_node		blkcg_node;
	struct blkcg			*blkcg;

	/*
	 * Each blkg gets congested separately and the congestion state is
	 * propagated to the matching bdi_writeback_congested.
	 */
	struct bdi_writeback_congested	*wb_congested;

	/* all non-root blkcg_gq's are guaranteed to have access to parent */
	struct blkcg_gq			*parent;

	/* request allocation list for this blkcg-q pair */
	struct request_list		rl;

	/* reference count */
	struct percpu_ref		refcnt;

	/* is this blkg online? protected by both blkcg and q locks */
	bool				online;

	struct blkg_rwstat		stat_bytes;
	struct blkg_rwstat		stat_ios;

	struct blkg_policy_data		*pd[BLKCG_MAX_POLS];

	struct rcu_head			rcu_head;

	atomic_t			use_delay;
	atomic64_t			delay_nsec;
	atomic64_t			delay_start;
	u64				last_delay;
	int				last_use;
};

typedef struct blkcg_policy_data *(blkcg_pol_alloc_cpd_fn)(gfp_t gfp);
typedef void (blkcg_pol_init_cpd_fn)(struct blkcg_policy_data *cpd);
typedef void (blkcg_pol_free_cpd_fn)(struct blkcg_policy_data *cpd);
typedef void (blkcg_pol_bind_cpd_fn)(struct blkcg_policy_data *cpd);
typedef struct blkg_policy_data *(blkcg_pol_alloc_pd_fn)(gfp_t gfp, int node);
typedef void (blkcg_pol_init_pd_fn)(struct blkg_policy_data *pd);
typedef void (blkcg_pol_online_pd_fn)(struct blkg_policy_data *pd);
typedef void (blkcg_pol_offline_pd_fn)(struct blkg_policy_data *pd);
typedef void (blkcg_pol_free_pd_fn)(struct blkg_policy_data *pd);
typedef void (blkcg_pol_reset_pd_stats_fn)(struct blkg_policy_data *pd);
typedef size_t (blkcg_pol_stat_pd_fn)(struct blkg_policy_data *pd, char *buf,
				      size_t size);

struct blkcg_policy {
	int				plid;
	/* cgroup files for the policy */
	struct cftype			*dfl_cftypes;
	struct cftype			*legacy_cftypes;

	/* operations */
	blkcg_pol_alloc_cpd_fn		*cpd_alloc_fn;
	blkcg_pol_init_cpd_fn		*cpd_init_fn;
	blkcg_pol_free_cpd_fn		*cpd_free_fn;
	blkcg_pol_bind_cpd_fn		*cpd_bind_fn;

	blkcg_pol_alloc_pd_fn		*pd_alloc_fn;
	blkcg_pol_init_pd_fn		*pd_init_fn;
	blkcg_pol_online_pd_fn		*pd_online_fn;
	blkcg_pol_offline_pd_fn		*pd_offline_fn;
	blkcg_pol_free_pd_fn		*pd_free_fn;
	blkcg_pol_reset_pd_stats_fn	*pd_reset_stats_fn;
	blkcg_pol_stat_pd_fn		*pd_stat_fn;
};

extern struct blkcg blkcg_root;
extern struct cgroup_subsys_state * const blkcg_root_css;

struct blkcg_gq *blkg_lookup_slowpath(struct blkcg *blkcg,
				      struct request_queue *q, bool update_hint);
struct blkcg_gq *__blkg_lookup_create(struct blkcg *blkcg,
				      struct request_queue *q);
struct blkcg_gq *blkg_lookup_create(struct blkcg *blkcg,
				    struct request_queue *q);
int blkcg_init_queue(struct request_queue *q);
void blkcg_drain_queue(struct request_queue *q);
void blkcg_exit_queue(struct request_queue *q);

/* Blkio controller policy registration */
int blkcg_policy_register(struct blkcg_policy *pol);
void blkcg_policy_unregister(struct blkcg_policy *pol);
int blkcg_activate_policy(struct request_queue *q,
			  const struct blkcg_policy *pol);
void blkcg_deactivate_policy(struct request_queue *q,
			     const struct blkcg_policy *pol);

const char *blkg_dev_name(struct blkcg_gq *blkg);
void blkcg_print_blkgs(struct seq_file *sf, struct blkcg *blkcg,
		       u64 (*prfill)(struct seq_file *,
				     struct blkg_policy_data *, int),
		       const struct blkcg_policy *pol, int data,
		       bool show_total);
u64 __blkg_prfill_u64(struct seq_file *sf, struct blkg_policy_data *pd, u64 v);
u64 __blkg_prfill_rwstat(struct seq_file *sf, struct blkg_policy_data *pd,
			 const struct blkg_rwstat *rwstat);
u64 blkg_prfill_stat(struct seq_file *sf, struct blkg_policy_data *pd, int off);
u64 blkg_prfill_rwstat(struct seq_file *sf, struct blkg_policy_data *pd,
		       int off);
int blkg_print_stat_bytes(struct seq_file *sf, void *v);
int blkg_print_stat_ios(struct seq_file *sf, void *v);
int blkg_print_stat_bytes_recursive(struct seq_file *sf, void *v);
int blkg_print_stat_ios_recursive(struct seq_file *sf, void *v);

u64 blkg_stat_recursive_sum(struct blkcg_gq *blkg,
			    struct blkcg_policy *pol, int off);
struct blkg_rwstat blkg_rwstat_recursive_sum(struct blkcg_gq *blkg,
					     struct blkcg_policy *pol, int off);

struct blkg_conf_ctx {
	struct gendisk			*disk;
	struct blkcg_gq			*blkg;
	char				*body;
};

int blkg_conf_prep(struct blkcg *blkcg, const struct blkcg_policy *pol,
		   char *input, struct blkg_conf_ctx *ctx);
void blkg_conf_finish(struct blkg_conf_ctx *ctx);

/**
 * blkcg_css - find the current css
 *
 * Find the css associated with either the kthread or the current task.
 * This may return a dying css, so it is up to the caller to use tryget logic
 * to confirm it is alive and well.
 */
static inline struct cgroup_subsys_state *blkcg_css(void)
{
	struct cgroup_subsys_state *css;

	css = kthread_blkcg();
	if (css)
		return css;
	return task_css(current, io_cgrp_id);
}

static inline struct blkcg *css_to_blkcg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct blkcg, css) : NULL;
}

/**
 * __bio_blkcg - internal version of bio_blkcg for bfq and cfq
 *
 * DO NOT USE.
 * There is a flaw using this version of the function.  In particular, this was
 * used in a broken paradigm where association was called on the given css.  It
 * is possible though that the returned css from task_css() is in the process
 * of dying due to migration of the current task.  So it is improper to assume
 * *_get() is going to succeed.  Both BFQ and CFQ rely on this logic and will
 * take additional work to handle more gracefully.
 */
static inline struct blkcg *__bio_blkcg(struct bio *bio)
{
	if (bio && bio->bi_blkg)
		return bio->bi_blkg->blkcg;
	return css_to_blkcg(blkcg_css());
}

/**
 * bio_blkcg - grab the blkcg associated with a bio
 * @bio: target bio
 *
 * This returns the blkcg associated with a bio, NULL if not associated.
 * Callers are expected to either handle NULL or know association has been
 * done prior to calling this.
 */
static inline struct blkcg *bio_blkcg(struct bio *bio)
{
	if (bio && bio->bi_blkg)
		return bio->bi_blkg->blkcg;
	return NULL;
}

static inline bool blk_cgroup_congested(void)
{
	struct cgroup_subsys_state *css;
	bool ret = false;

	rcu_read_lock();
	css = kthread_blkcg();
	if (!css)
		css = task_css(current, io_cgrp_id);
	while (css) {
		if (atomic_read(&css->cgroup->congestion_count)) {
			ret = true;
			break;
		}
		css = css->parent;
	}
	rcu_read_unlock();
	return ret;
}

/**
 * bio_issue_as_root_blkg - see if this bio needs to be issued as root blkg
 * @return: true if this bio needs to be submitted with the root blkg context.
 *
 * In order to avoid priority inversions we sometimes need to issue a bio as if
 * it were attached to the root blkg, and then backcharge to the actual owning
 * blkg.  The idea is we do bio_blkcg() to look up the actual context for the
 * bio and attach the appropriate blkg to the bio.  Then we call this helper and
 * if it is true run with the root blkg for that queue and then do any
 * backcharging to the originating cgroup once the io is complete.
 */
static inline bool bio_issue_as_root_blkg(struct bio *bio)
{
	return (bio->bi_opf & (REQ_META | REQ_SWAP)) != 0;
}

/**
 * blkcg_parent - get the parent of a blkcg
 * @blkcg: blkcg of interest
 *
 * Return the parent blkcg of @blkcg.  Can be called anytime.
 */
static inline struct blkcg *blkcg_parent(struct blkcg *blkcg)
{
	return css_to_blkcg(blkcg->css.parent);
}

/**
 * __blkg_lookup - internal version of blkg_lookup()
 * @blkcg: blkcg of interest
 * @q: request_queue of interest
 * @update_hint: whether to update lookup hint with the result or not
 *
 * This is internal version and shouldn't be used by policy
 * implementations.  Looks up blkgs for the @blkcg - @q pair regardless of
 * @q's bypass state.  If @update_hint is %true, the caller should be
 * holding @q->queue_lock and lookup hint is updated on success.
 */
static inline struct blkcg_gq *__blkg_lookup(struct blkcg *blkcg,
					     struct request_queue *q,
					     bool update_hint)
{
	struct blkcg_gq *blkg;

	if (blkcg == &blkcg_root)
		return q->root_blkg;

	blkg = rcu_dereference(blkcg->blkg_hint);
	if (blkg && blkg->q == q)
		return blkg;

	return blkg_lookup_slowpath(blkcg, q, update_hint);
}

/**
 * blkg_lookup - lookup blkg for the specified blkcg - q pair
 * @blkcg: blkcg of interest
 * @q: request_queue of interest
 *
 * Lookup blkg for the @blkcg - @q pair.  This function should be called
 * under RCU read lock and is guaranteed to return %NULL if @q is bypassing
 * - see blk_queue_bypass_start() for details.
 */
static inline struct blkcg_gq *blkg_lookup(struct blkcg *blkcg,
					   struct request_queue *q)
{
	WARN_ON_ONCE(!rcu_read_lock_held());

	if (unlikely(blk_queue_bypass(q)))
		return NULL;
	return __blkg_lookup(blkcg, q, false);
}

/**
 * blk_queue_root_blkg - return blkg for the (blkcg_root, @q) pair
 * @q: request_queue of interest
 *
 * Lookup blkg for @q at the root level. See also blkg_lookup().
 */
static inline struct blkcg_gq *blk_queue_root_blkg(struct request_queue *q)
{
	return q->root_blkg;
}

/**
 * blkg_to_pdata - get policy private data
 * @blkg: blkg of interest
 * @pol: policy of interest
 *
 * Return pointer to private data associated with the @blkg-@pol pair.
 */
static inline struct blkg_policy_data *blkg_to_pd(struct blkcg_gq *blkg,
						  struct blkcg_policy *pol)
{
	return blkg ? blkg->pd[pol->plid] : NULL;
}

static inline struct blkcg_policy_data *blkcg_to_cpd(struct blkcg *blkcg,
						     struct blkcg_policy *pol)
{
	return blkcg ? blkcg->cpd[pol->plid] : NULL;
}

/**
 * pdata_to_blkg - get blkg associated with policy private data
 * @pd: policy private data of interest
 *
 * @pd is policy private data.  Determine the blkg it's associated with.
 */
static inline struct blkcg_gq *pd_to_blkg(struct blkg_policy_data *pd)
{
	return pd ? pd->blkg : NULL;
}

static inline struct blkcg *cpd_to_blkcg(struct blkcg_policy_data *cpd)
{
	return cpd ? cpd->blkcg : NULL;
}

extern void blkcg_destroy_blkgs(struct blkcg *blkcg);

#ifdef CONFIG_CGROUP_WRITEBACK

/**
 * blkcg_cgwb_get - get a reference for blkcg->cgwb_list
 * @blkcg: blkcg of interest
 *
 * This is used to track the number of active wb's related to a blkcg.
 */
static inline void blkcg_cgwb_get(struct blkcg *blkcg)
{
	refcount_inc(&blkcg->cgwb_refcnt);
}

/**
 * blkcg_cgwb_put - put a reference for @blkcg->cgwb_list
 * @blkcg: blkcg of interest
 *
 * This is used to track the number of active wb's related to a blkcg.
 * When this count goes to zero, all active wb has finished so the
 * blkcg can continue destruction by calling blkcg_destroy_blkgs().
 * This work may occur in cgwb_release_workfn() on the cgwb_release
 * workqueue.
 */
static inline void blkcg_cgwb_put(struct blkcg *blkcg)
{
	if (refcount_dec_and_test(&blkcg->cgwb_refcnt))
		blkcg_destroy_blkgs(blkcg);
}

#else

static inline void blkcg_cgwb_get(struct blkcg *blkcg) { }

static inline void blkcg_cgwb_put(struct blkcg *blkcg)
{
	/* wb isn't being accounted, so trigger destruction right away */
	blkcg_destroy_blkgs(blkcg);
}

#endif

/**
 * blkg_path - format cgroup path of blkg
 * @blkg: blkg of interest
 * @buf: target buffer
 * @buflen: target buffer length
 *
 * Format the path of the cgroup of @blkg into @buf.
 */
static inline int blkg_path(struct blkcg_gq *blkg, char *buf, int buflen)
{
	return cgroup_path(blkg->blkcg->css.cgroup, buf, buflen);
}

/**
 * blkg_get - get a blkg reference
 * @blkg: blkg to get
 *
 * The caller should be holding an existing reference.
 */
static inline void blkg_get(struct blkcg_gq *blkg)
{
	percpu_ref_get(&blkg->refcnt);
}

/**
 * blkg_tryget - try and get a blkg reference
 * @blkg: blkg to get
 *
 * This is for use when doing an RCU lookup of the blkg.  We may be in the midst
 * of freeing this blkg, so we can only use it if the refcnt is not zero.
 */
static inline bool blkg_tryget(struct blkcg_gq *blkg)
{
	return percpu_ref_tryget(&blkg->refcnt);
}

/**
 * blkg_tryget_closest - try and get a blkg ref on the closet blkg
 * @blkg: blkg to get
 *
 * This walks up the blkg tree to find the closest non-dying blkg and returns
 * the blkg that it did association with as it may not be the passed in blkg.
 */
static inline struct blkcg_gq *blkg_tryget_closest(struct blkcg_gq *blkg)
{
	while (!percpu_ref_tryget(&blkg->refcnt))
		blkg = blkg->parent;

	return blkg;
}

/**
 * blkg_put - put a blkg reference
 * @blkg: blkg to put
 */
static inline void blkg_put(struct blkcg_gq *blkg)
{
	percpu_ref_put(&blkg->refcnt);
}

/**
 * blkg_for_each_descendant_pre - pre-order walk of a blkg's descendants
 * @d_blkg: loop cursor pointing to the current descendant
 * @pos_css: used for iteration
 * @p_blkg: target blkg to walk descendants of
 *
 * Walk @c_blkg through the descendants of @p_blkg.  Must be used with RCU
 * read locked.  If called under either blkcg or queue lock, the iteration
 * is guaranteed to include all and only online blkgs.  The caller may
 * update @pos_css by calling css_rightmost_descendant() to skip subtree.
 * @p_blkg is included in the iteration and the first node to be visited.
 */
#define blkg_for_each_descendant_pre(d_blkg, pos_css, p_blkg)		\
	css_for_each_descendant_pre((pos_css), &(p_blkg)->blkcg->css)	\
		if (((d_blkg) = __blkg_lookup(css_to_blkcg(pos_css),	\
					      (p_blkg)->q, false)))

/**
 * blkg_for_each_descendant_post - post-order walk of a blkg's descendants
 * @d_blkg: loop cursor pointing to the current descendant
 * @pos_css: used for iteration
 * @p_blkg: target blkg to walk descendants of
 *
 * Similar to blkg_for_each_descendant_pre() but performs post-order
 * traversal instead.  Synchronization rules are the same.  @p_blkg is
 * included in the iteration and the last node to be visited.
 */
#define blkg_for_each_descendant_post(d_blkg, pos_css, p_blkg)		\
	css_for_each_descendant_post((pos_css), &(p_blkg)->blkcg->css)	\
		if (((d_blkg) = __blkg_lookup(css_to_blkcg(pos_css),	\
					      (p_blkg)->q, false)))

/**
 * blk_get_rl - get request_list to use
 * @q: request_queue of interest
 * @bio: bio which will be attached to the allocated request (may be %NULL)
 *
 * The caller wants to allocate a request from @q to use for @bio.  Find
 * the request_list to use and obtain a reference on it.  Should be called
 * under queue_lock.  This function is guaranteed to return non-%NULL
 * request_list.
 */
static inline struct request_list *blk_get_rl(struct request_queue *q,
					      struct bio *bio)
{
	struct blkcg *blkcg;
	struct blkcg_gq *blkg;

	rcu_read_lock();

	if (bio && bio->bi_blkg) {
		blkcg = bio->bi_blkg->blkcg;
		if (blkcg == &blkcg_root)
			goto rl_use_root;

		blkg_get(bio->bi_blkg);
		rcu_read_unlock();
		return &bio->bi_blkg->rl;
	}

	blkcg = css_to_blkcg(blkcg_css());
	if (blkcg == &blkcg_root)
		goto rl_use_root;

	blkg = blkg_lookup(blkcg, q);
	if (unlikely(!blkg))
		blkg = __blkg_lookup_create(blkcg, q);

	if (blkg->blkcg == &blkcg_root || !blkg_tryget(blkg))
		goto rl_use_root;

	rcu_read_unlock();
	return &blkg->rl;

	/*
	 * Each blkg has its own request_list, however, the root blkcg
	 * uses the request_queue's root_rl.  This is to avoid most
	 * overhead for the root blkcg.
	 */
rl_use_root:
	rcu_read_unlock();
	return &q->root_rl;
}

/**
 * blk_put_rl - put request_list
 * @rl: request_list to put
 *
 * Put the reference acquired by blk_get_rl().  Should be called under
 * queue_lock.
 */
static inline void blk_put_rl(struct request_list *rl)
{
	if (rl->blkg->blkcg != &blkcg_root)
		blkg_put(rl->blkg);
}

/**
 * blk_rq_set_rl - associate a request with a request_list
 * @rq: request of interest
 * @rl: target request_list
 *
 * Associate @rq with @rl so that accounting and freeing can know the
 * request_list @rq came from.
 */
static inline void blk_rq_set_rl(struct request *rq, struct request_list *rl)
{
	rq->rl = rl;
}

/**
 * blk_rq_rl - return the request_list a request came from
 * @rq: request of interest
 *
 * Return the request_list @rq is allocated from.
 */
static inline struct request_list *blk_rq_rl(struct request *rq)
{
	return rq->rl;
}

struct request_list *__blk_queue_next_rl(struct request_list *rl,
					 struct request_queue *q);
/**
 * blk_queue_for_each_rl - iterate through all request_lists of a request_queue
 *
 * Should be used under queue_lock.
 */
#define blk_queue_for_each_rl(rl, q)	\
	for ((rl) = &(q)->root_rl; (rl); (rl) = __blk_queue_next_rl((rl), (q)))

static inline int blkg_stat_init(struct blkg_stat *stat, gfp_t gfp)
{
	int ret;

	ret = percpu_counter_init(&stat->cpu_cnt, 0, gfp);
	if (ret)
		return ret;

	atomic64_set(&stat->aux_cnt, 0);
	return 0;
}

static inline void blkg_stat_exit(struct blkg_stat *stat)
{
	percpu_counter_destroy(&stat->cpu_cnt);
}

/**
 * blkg_stat_add - add a value to a blkg_stat
 * @stat: target blkg_stat
 * @val: value to add
 *
 * Add @val to @stat.  The caller must ensure that IRQ on the same CPU
 * don't re-enter this function for the same counter.
 */
static inline void blkg_stat_add(struct blkg_stat *stat, uint64_t val)
{
	percpu_counter_add_batch(&stat->cpu_cnt, val, BLKG_STAT_CPU_BATCH);
}

/**
 * blkg_stat_read - read the current value of a blkg_stat
 * @stat: blkg_stat to read
 */
static inline uint64_t blkg_stat_read(struct blkg_stat *stat)
{
	return percpu_counter_sum_positive(&stat->cpu_cnt);
}

/**
 * blkg_stat_reset - reset a blkg_stat
 * @stat: blkg_stat to reset
 */
static inline void blkg_stat_reset(struct blkg_stat *stat)
{
	percpu_counter_set(&stat->cpu_cnt, 0);
	atomic64_set(&stat->aux_cnt, 0);
}

/**
 * blkg_stat_add_aux - add a blkg_stat into another's aux count
 * @to: the destination blkg_stat
 * @from: the source
 *
 * Add @from's count including the aux one to @to's aux count.
 */
static inline void blkg_stat_add_aux(struct blkg_stat *to,
				     struct blkg_stat *from)
{
	atomic64_add(blkg_stat_read(from) + atomic64_read(&from->aux_cnt),
		     &to->aux_cnt);
}

static inline int blkg_rwstat_init(struct blkg_rwstat *rwstat, gfp_t gfp)
{
	int i, ret;

	for (i = 0; i < BLKG_RWSTAT_NR; i++) {
		ret = percpu_counter_init(&rwstat->cpu_cnt[i], 0, gfp);
		if (ret) {
			while (--i >= 0)
				percpu_counter_destroy(&rwstat->cpu_cnt[i]);
			return ret;
		}
		atomic64_set(&rwstat->aux_cnt[i], 0);
	}
	return 0;
}

static inline void blkg_rwstat_exit(struct blkg_rwstat *rwstat)
{
	int i;

	for (i = 0; i < BLKG_RWSTAT_NR; i++)
		percpu_counter_destroy(&rwstat->cpu_cnt[i]);
}

/**
 * blkg_rwstat_add - add a value to a blkg_rwstat
 * @rwstat: target blkg_rwstat
 * @op: REQ_OP and flags
 * @val: value to add
 *
 * Add @val to @rwstat.  The counters are chosen according to @rw.  The
 * caller is responsible for synchronizing calls to this function.
 */
static inline void blkg_rwstat_add(struct blkg_rwstat *rwstat,
				   unsigned int op, uint64_t val)
{
	struct percpu_counter *cnt;

	if (op_is_discard(op))
		cnt = &rwstat->cpu_cnt[BLKG_RWSTAT_DISCARD];
	else if (op_is_write(op))
		cnt = &rwstat->cpu_cnt[BLKG_RWSTAT_WRITE];
	else
		cnt = &rwstat->cpu_cnt[BLKG_RWSTAT_READ];

	percpu_counter_add_batch(cnt, val, BLKG_STAT_CPU_BATCH);

	if (op_is_sync(op))
		cnt = &rwstat->cpu_cnt[BLKG_RWSTAT_SYNC];
	else
		cnt = &rwstat->cpu_cnt[BLKG_RWSTAT_ASYNC];

	percpu_counter_add_batch(cnt, val, BLKG_STAT_CPU_BATCH);
}

/**
 * blkg_rwstat_read - read the current values of a blkg_rwstat
 * @rwstat: blkg_rwstat to read
 *
 * Read the current snapshot of @rwstat and return it in the aux counts.
 */
static inline struct blkg_rwstat blkg_rwstat_read(struct blkg_rwstat *rwstat)
{
	struct blkg_rwstat result;
	int i;

	for (i = 0; i < BLKG_RWSTAT_NR; i++)
		atomic64_set(&result.aux_cnt[i],
			     percpu_counter_sum_positive(&rwstat->cpu_cnt[i]));
	return result;
}

/**
 * blkg_rwstat_total - read the total count of a blkg_rwstat
 * @rwstat: blkg_rwstat to read
 *
 * Return the total count of @rwstat regardless of the IO direction.  This
 * function can be called without synchronization and takes care of u64
 * atomicity.
 */
static inline uint64_t blkg_rwstat_total(struct blkg_rwstat *rwstat)
{
	struct blkg_rwstat tmp = blkg_rwstat_read(rwstat);

	return atomic64_read(&tmp.aux_cnt[BLKG_RWSTAT_READ]) +
		atomic64_read(&tmp.aux_cnt[BLKG_RWSTAT_WRITE]);
}

/**
 * blkg_rwstat_reset - reset a blkg_rwstat
 * @rwstat: blkg_rwstat to reset
 */
static inline void blkg_rwstat_reset(struct blkg_rwstat *rwstat)
{
	int i;

	for (i = 0; i < BLKG_RWSTAT_NR; i++) {
		percpu_counter_set(&rwstat->cpu_cnt[i], 0);
		atomic64_set(&rwstat->aux_cnt[i], 0);
	}
}

/**
 * blkg_rwstat_add_aux - add a blkg_rwstat into another's aux count
 * @to: the destination blkg_rwstat
 * @from: the source
 *
 * Add @from's count including the aux one to @to's aux count.
 */
static inline void blkg_rwstat_add_aux(struct blkg_rwstat *to,
				       struct blkg_rwstat *from)
{
	u64 sum[BLKG_RWSTAT_NR];
	int i;

	for (i = 0; i < BLKG_RWSTAT_NR; i++)
		sum[i] = percpu_counter_sum_positive(&from->cpu_cnt[i]);

	for (i = 0; i < BLKG_RWSTAT_NR; i++)
		atomic64_add(sum[i] + atomic64_read(&from->aux_cnt[i]),
			     &to->aux_cnt[i]);
}

#ifdef CONFIG_BLK_DEV_THROTTLING
extern bool blk_throtl_bio(struct request_queue *q, struct blkcg_gq *blkg,
			   struct bio *bio);
#else
static inline bool blk_throtl_bio(struct request_queue *q, struct blkcg_gq *blkg,
				  struct bio *bio) { return false; }
#endif


static inline void blkcg_bio_issue_init(struct bio *bio)
{
	bio_issue_init(&bio->bi_issue, bio_sectors(bio));
}

static inline bool blkcg_bio_issue_check(struct request_queue *q,
					 struct bio *bio)
{
	struct blkcg_gq *blkg;
	bool throtl = false;

	rcu_read_lock();

	bio_associate_create_blkg(q, bio);
	blkg = bio->bi_blkg;

	throtl = blk_throtl_bio(q, blkg, bio);

	if (!throtl) {
		/*
		 * If the bio is flagged with BIO_QUEUE_ENTERED it means this
		 * is a split bio and we would have already accounted for the
		 * size of the bio.
		 */
		if (!bio_flagged(bio, BIO_QUEUE_ENTERED))
			blkg_rwstat_add(&blkg->stat_bytes, bio->bi_opf,
					bio->bi_iter.bi_size);
		blkg_rwstat_add(&blkg->stat_ios, bio->bi_opf, 1);
	}

	blkcg_bio_issue_init(bio);

	rcu_read_unlock();
	return !throtl;
}

static inline void blkcg_use_delay(struct blkcg_gq *blkg)
{
	if (atomic_add_return(1, &blkg->use_delay) == 1)
		atomic_inc(&blkg->blkcg->css.cgroup->congestion_count);
}

static inline int blkcg_unuse_delay(struct blkcg_gq *blkg)
{
	int old = atomic_read(&blkg->use_delay);

	if (old == 0)
		return 0;

	/*
	 * We do this song and dance because we can race with somebody else
	 * adding or removing delay.  If we just did an atomic_dec we'd end up
	 * negative and we'd already be in trouble.  We need to subtract 1 and
	 * then check to see if we were the last delay so we can drop the
	 * congestion count on the cgroup.
	 */
	while (old) {
		int cur = atomic_cmpxchg(&blkg->use_delay, old, old - 1);
		if (cur == old)
			break;
		old = cur;
	}

	if (old == 0)
		return 0;
	if (old == 1)
		atomic_dec(&blkg->blkcg->css.cgroup->congestion_count);
	return 1;
}

static inline void blkcg_clear_delay(struct blkcg_gq *blkg)
{
	int old = atomic_read(&blkg->use_delay);
	if (!old)
		return;
	/* We only want 1 person clearing the congestion count for this blkg. */
	while (old) {
		int cur = atomic_cmpxchg(&blkg->use_delay, old, 0);
		if (cur == old) {
			atomic_dec(&blkg->blkcg->css.cgroup->congestion_count);
			break;
		}
		old = cur;
	}
}

void blkcg_add_delay(struct blkcg_gq *blkg, u64 now, u64 delta);
void blkcg_schedule_throttle(struct request_queue *q, bool use_memdelay);
void blkcg_maybe_throttle_current(void);
#else	/* CONFIG_BLK_CGROUP */

struct blkcg {
};

struct blkg_policy_data {
};

struct blkcg_policy_data {
};

struct blkcg_gq {
};

struct blkcg_policy {
};

#define blkcg_root_css	((struct cgroup_subsys_state *)ERR_PTR(-EINVAL))

static inline void blkcg_maybe_throttle_current(void) { }
static inline bool blk_cgroup_congested(void) { return false; }

#ifdef CONFIG_BLOCK

static inline void blkcg_schedule_throttle(struct request_queue *q, bool use_memdelay) { }

static inline struct blkcg_gq *blkg_lookup(struct blkcg *blkcg, void *key) { return NULL; }
static inline struct blkcg_gq *blk_queue_root_blkg(struct request_queue *q)
{ return NULL; }
static inline int blkcg_init_queue(struct request_queue *q) { return 0; }
static inline void blkcg_drain_queue(struct request_queue *q) { }
static inline void blkcg_exit_queue(struct request_queue *q) { }
static inline int blkcg_policy_register(struct blkcg_policy *pol) { return 0; }
static inline void blkcg_policy_unregister(struct blkcg_policy *pol) { }
static inline int blkcg_activate_policy(struct request_queue *q,
					const struct blkcg_policy *pol) { return 0; }
static inline void blkcg_deactivate_policy(struct request_queue *q,
					   const struct blkcg_policy *pol) { }

static inline struct blkcg *__bio_blkcg(struct bio *bio) { return NULL; }
static inline struct blkcg *bio_blkcg(struct bio *bio) { return NULL; }

static inline struct blkg_policy_data *blkg_to_pd(struct blkcg_gq *blkg,
						  struct blkcg_policy *pol) { return NULL; }
static inline struct blkcg_gq *pd_to_blkg(struct blkg_policy_data *pd) { return NULL; }
static inline char *blkg_path(struct blkcg_gq *blkg) { return NULL; }
static inline void blkg_get(struct blkcg_gq *blkg) { }
static inline void blkg_put(struct blkcg_gq *blkg) { }

static inline struct request_list *blk_get_rl(struct request_queue *q,
					      struct bio *bio) { return &q->root_rl; }
static inline void blk_put_rl(struct request_list *rl) { }
static inline void blk_rq_set_rl(struct request *rq, struct request_list *rl) { }
static inline struct request_list *blk_rq_rl(struct request *rq) { return &rq->q->root_rl; }

static inline void blkcg_bio_issue_init(struct bio *bio) { }
static inline bool blkcg_bio_issue_check(struct request_queue *q,
					 struct bio *bio) { return true; }

#define blk_queue_for_each_rl(rl, q)	\
	for ((rl) = &(q)->root_rl; (rl); (rl) = NULL)

#endif	/* CONFIG_BLOCK */
#endif	/* CONFIG_BLK_CGROUP */
#endif	/* _BLK_CGROUP_H */
