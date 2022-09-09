/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_GEN_STATS_H
#define __NET_GEN_STATS_H

#include <linux/gen_stats.h>
#include <linux/socket.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>

/* Throughput stats.
 * Must be initialized beforehand with gnet_stats_basic_sync_init().
 *
 * If no reads can ever occur parallel to writes (e.g. stack-allocated
 * bstats), then the internal stat values can be written to and read
 * from directly. Otherwise, use _bstats_set/update() for writes and
 * gnet_stats_add_basic() for reads.
 */
struct gnet_stats_basic_sync {
	u64_stats_t bytes;
	u64_stats_t packets;
	struct u64_stats_sync syncp;
} __aligned(2 * sizeof(u64));

struct net_rate_estimator;

struct gnet_dump {
	spinlock_t *      lock;
	struct sk_buff *  skb;
	struct nlattr *   tail;

	/* Backward compatibility */
	int               compat_tc_stats;
	int               compat_xstats;
	int               padattr;
	void *            xstats;
	int               xstats_len;
	struct tc_stats   tc_stats;
};

void gnet_stats_basic_sync_init(struct gnet_stats_basic_sync *b);
int gnet_stats_start_copy(struct sk_buff *skb, int type, spinlock_t *lock,
			  struct gnet_dump *d, int padattr);

int gnet_stats_start_copy_compat(struct sk_buff *skb, int type,
				 int tc_stats_type, int xstats_type,
				 spinlock_t *lock, struct gnet_dump *d,
				 int padattr);

int gnet_stats_copy_basic(struct gnet_dump *d,
			  struct gnet_stats_basic_sync __percpu *cpu,
			  struct gnet_stats_basic_sync *b, bool running);
void gnet_stats_add_basic(struct gnet_stats_basic_sync *bstats,
			  struct gnet_stats_basic_sync __percpu *cpu,
			  struct gnet_stats_basic_sync *b, bool running);
int gnet_stats_copy_basic_hw(struct gnet_dump *d,
			     struct gnet_stats_basic_sync __percpu *cpu,
			     struct gnet_stats_basic_sync *b, bool running);
int gnet_stats_copy_rate_est(struct gnet_dump *d,
			     struct net_rate_estimator __rcu **ptr);
int gnet_stats_copy_queue(struct gnet_dump *d,
			  struct gnet_stats_queue __percpu *cpu_q,
			  struct gnet_stats_queue *q, __u32 qlen);
void gnet_stats_add_queue(struct gnet_stats_queue *qstats,
			  const struct gnet_stats_queue __percpu *cpu_q,
			  const struct gnet_stats_queue *q);
int gnet_stats_copy_app(struct gnet_dump *d, void *st, int len);

int gnet_stats_finish_copy(struct gnet_dump *d);

int gen_new_estimator(struct gnet_stats_basic_sync *bstats,
		      struct gnet_stats_basic_sync __percpu *cpu_bstats,
		      struct net_rate_estimator __rcu **rate_est,
		      spinlock_t *lock,
		      bool running, struct nlattr *opt);
void gen_kill_estimator(struct net_rate_estimator __rcu **ptr);
int gen_replace_estimator(struct gnet_stats_basic_sync *bstats,
			  struct gnet_stats_basic_sync __percpu *cpu_bstats,
			  struct net_rate_estimator __rcu **ptr,
			  spinlock_t *lock,
			  bool running, struct nlattr *opt);
bool gen_estimator_active(struct net_rate_estimator __rcu **ptr);
bool gen_estimator_read(struct net_rate_estimator __rcu **ptr,
			struct gnet_stats_rate_est64 *sample);
#endif
