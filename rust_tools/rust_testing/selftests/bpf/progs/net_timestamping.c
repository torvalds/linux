#include "vmlinux.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "bpf_kfuncs.h"
#include <errno.h>

__u32 monitored_pid = 0;

int nr_active;
int nr_snd;
int nr_passive;
int nr_sched;
int nr_txsw;
int nr_ack;

struct sk_stg {
	__u64 sendmsg_ns;	/* record ts when sendmsg is called */
};

struct sk_tskey {
	u64 cookie;
	u32 tskey;
};

struct delay_info {
	u64 sendmsg_ns;		/* record ts when sendmsg is called */
	u32 sched_delay;	/* SCHED_CB - sendmsg_ns */
	u32 snd_sw_delay;	/* SND_SW_CB - SCHED_CB */
	u32 ack_delay;		/* ACK_CB - SND_SW_CB */
};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct sk_stg);
} sk_stg_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, struct sk_tskey);
	__type(value, struct delay_info);
	__uint(max_entries, 1024);
} time_map SEC(".maps");

static u64 delay_tolerance_nsec = 10000000000; /* 10 second as an example */

extern int bpf_sock_ops_enable_tx_tstamp(struct bpf_sock_ops_kern *skops, u64 flags) __ksym;

static int bpf_test_sockopt(void *ctx, const struct sock *sk, int expected)
{
	int tmp, new = SK_BPF_CB_TX_TIMESTAMPING;
	int opt = SK_BPF_CB_FLAGS;
	int level = SOL_SOCKET;

	if (bpf_setsockopt(ctx, level, opt, &new, sizeof(new)) != expected)
		return 1;

	if (bpf_getsockopt(ctx, level, opt, &tmp, sizeof(tmp)) != expected ||
	    (!expected && tmp != new))
		return 1;

	return 0;
}

static bool bpf_test_access_sockopt(void *ctx, const struct sock *sk)
{
	if (bpf_test_sockopt(ctx, sk, -EOPNOTSUPP))
		return true;
	return false;
}

static bool bpf_test_access_load_hdr_opt(struct bpf_sock_ops *skops)
{
	u8 opt[3] = {0};
	int load_flags = 0;
	int ret;

	ret = bpf_load_hdr_opt(skops, opt, sizeof(opt), load_flags);
	if (ret != -EOPNOTSUPP)
		return true;

	return false;
}

static bool bpf_test_access_cb_flags_set(struct bpf_sock_ops *skops)
{
	int ret;

	ret = bpf_sock_ops_cb_flags_set(skops, 0);
	if (ret != -EOPNOTSUPP)
		return true;

	return false;
}

/* In the timestamping callbacks, we're not allowed to call the following
 * BPF CALLs for the safety concern. Return false if expected.
 */
static bool bpf_test_access_bpf_calls(struct bpf_sock_ops *skops,
				      const struct sock *sk)
{
	if (bpf_test_access_sockopt(skops, sk))
		return true;

	if (bpf_test_access_load_hdr_opt(skops))
		return true;

	if (bpf_test_access_cb_flags_set(skops))
		return true;

	return false;
}

static bool bpf_test_delay(struct bpf_sock_ops *skops, const struct sock *sk)
{
	struct bpf_sock_ops_kern *skops_kern;
	u64 timestamp = bpf_ktime_get_ns();
	struct skb_shared_info *shinfo;
	struct delay_info dinfo = {0};
	struct sk_tskey key = {0};
	struct delay_info *val;
	struct sk_buff *skb;
	struct sk_stg *stg;
	u64 prior_ts, delay;

	if (bpf_test_access_bpf_calls(skops, sk))
		return false;

	skops_kern = bpf_cast_to_kern_ctx(skops);
	skb = skops_kern->skb;
	shinfo = bpf_core_cast(skb->head + skb->end, struct skb_shared_info);

	key.cookie = bpf_get_socket_cookie(skops);
	if (!key.cookie)
		return false;

	if (skops->op == BPF_SOCK_OPS_TSTAMP_SENDMSG_CB) {
		stg = bpf_sk_storage_get(&sk_stg_map, (void *)sk, 0, 0);
		if (!stg)
			return false;
		dinfo.sendmsg_ns = stg->sendmsg_ns;
		bpf_sock_ops_enable_tx_tstamp(skops_kern, 0);
		key.tskey = shinfo->tskey;
		if (!key.tskey)
			return false;
		bpf_map_update_elem(&time_map, &key, &dinfo, BPF_ANY);
		return true;
	}

	key.tskey = shinfo->tskey;
	if (!key.tskey)
		return false;

	val = bpf_map_lookup_elem(&time_map, &key);
	if (!val)
		return false;

	switch (skops->op) {
	case BPF_SOCK_OPS_TSTAMP_SCHED_CB:
		val->sched_delay = timestamp - val->sendmsg_ns;
		delay = val->sched_delay;
		break;
	case BPF_SOCK_OPS_TSTAMP_SND_SW_CB:
		prior_ts = val->sched_delay + val->sendmsg_ns;
		val->snd_sw_delay = timestamp - prior_ts;
		delay = val->snd_sw_delay;
		break;
	case BPF_SOCK_OPS_TSTAMP_ACK_CB:
		prior_ts = val->snd_sw_delay + val->sched_delay + val->sendmsg_ns;
		val->ack_delay = timestamp - prior_ts;
		delay = val->ack_delay;
		break;
	}

	if (delay >= delay_tolerance_nsec)
		return false;

	/* Since it's the last one, remove from the map after latency check */
	if (skops->op == BPF_SOCK_OPS_TSTAMP_ACK_CB)
		bpf_map_delete_elem(&time_map, &key);

	return true;
}

SEC("fentry/tcp_sendmsg_locked")
int BPF_PROG(trace_tcp_sendmsg_locked, struct sock *sk, struct msghdr *msg,
	     size_t size)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	u64 timestamp = bpf_ktime_get_ns();
	u32 flag = sk->sk_bpf_cb_flags;
	struct sk_stg *stg;

	if (pid != monitored_pid || !flag)
		return 0;

	stg = bpf_sk_storage_get(&sk_stg_map, sk, 0,
				 BPF_SK_STORAGE_GET_F_CREATE);
	if (!stg)
		return 0;

	stg->sendmsg_ns = timestamp;
	nr_snd += 1;
	return 0;
}

SEC("sockops")
int skops_sockopt(struct bpf_sock_ops *skops)
{
	struct bpf_sock *bpf_sk = skops->sk;
	const struct sock *sk;

	if (!bpf_sk)
		return 1;

	sk = (struct sock *)bpf_skc_to_tcp_sock(bpf_sk);
	if (!sk)
		return 1;

	switch (skops->op) {
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		nr_active += !bpf_test_sockopt(skops, sk, 0);
		break;
	case BPF_SOCK_OPS_TSTAMP_SENDMSG_CB:
		if (bpf_test_delay(skops, sk))
			nr_snd += 1;
		break;
	case BPF_SOCK_OPS_TSTAMP_SCHED_CB:
		if (bpf_test_delay(skops, sk))
			nr_sched += 1;
		break;
	case BPF_SOCK_OPS_TSTAMP_SND_SW_CB:
		if (bpf_test_delay(skops, sk))
			nr_txsw += 1;
		break;
	case BPF_SOCK_OPS_TSTAMP_ACK_CB:
		if (bpf_test_delay(skops, sk))
			nr_ack += 1;
		break;
	}

	return 1;
}

char _license[] SEC("license") = "GPL";
