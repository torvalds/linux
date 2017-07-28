#ifndef __NET_GEN_STATS_H
#define __NET_GEN_STATS_H

#include <linux/gen_stats.h>
#include <linux/socket.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>

struct gnet_stats_basic_cpu {
	struct gnet_stats_basic_packed bstats;
	struct u64_stats_sync syncp;
};

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

int gnet_stats_start_copy(struct sk_buff *skb, int type, spinlock_t *lock,
			  struct gnet_dump *d, int padattr);

int gnet_stats_start_copy_compat(struct sk_buff *skb, int type,
				 int tc_stats_type, int xstats_type,
				 spinlock_t *lock, struct gnet_dump *d,
				 int padattr);

int gnet_stats_copy_basic(const seqcount_t *running,
			  struct gnet_dump *d,
			  struct gnet_stats_basic_cpu __percpu *cpu,
			  struct gnet_stats_basic_packed *b);
void __gnet_stats_copy_basic(const seqcount_t *running,
			     struct gnet_stats_basic_packed *bstats,
			     struct gnet_stats_basic_cpu __percpu *cpu,
			     struct gnet_stats_basic_packed *b);
int gnet_stats_copy_rate_est(struct gnet_dump *d,
			     struct net_rate_estimator __rcu **ptr);
int gnet_stats_copy_queue(struct gnet_dump *d,
			  struct gnet_stats_queue __percpu *cpu_q,
			  struct gnet_stats_queue *q, __u32 qlen);
int gnet_stats_copy_app(struct gnet_dump *d, void *st, int len);

int gnet_stats_finish_copy(struct gnet_dump *d);

int gen_new_estimator(struct gnet_stats_basic_packed *bstats,
		      struct gnet_stats_basic_cpu __percpu *cpu_bstats,
		      struct net_rate_estimator __rcu **rate_est,
		      spinlock_t *stats_lock,
		      seqcount_t *running, struct nlattr *opt);
void gen_kill_estimator(struct net_rate_estimator __rcu **ptr);
int gen_replace_estimator(struct gnet_stats_basic_packed *bstats,
			  struct gnet_stats_basic_cpu __percpu *cpu_bstats,
			  struct net_rate_estimator __rcu **ptr,
			  spinlock_t *stats_lock,
			  seqcount_t *running, struct nlattr *opt);
bool gen_estimator_active(struct net_rate_estimator __rcu **ptr);
bool gen_estimator_read(struct net_rate_estimator __rcu **ptr,
			struct gnet_stats_rate_est64 *sample);
#endif
