// SPDX-License-Identifier: GPL-2.0
/*
 * sysctl_net_ipv4.c: sysctl interface to net IPV4 subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ipv4 directory entry (empty =) ). [MS]
 */

#include <linux/sysctl.h>
#include <linux/seqlock.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/ip_fib.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/cipso_ipv4.h>
#include <net/ping.h>
#include <net/protocol.h>
#include <net/netevent.h>

static int tcp_retr1_max = 255;
static int ip_local_port_range_min[] = { 1, 1 };
static int ip_local_port_range_max[] = { 65535, 65535 };
static int tcp_adv_win_scale_min = -31;
static int tcp_adv_win_scale_max = 31;
static int tcp_app_win_max = 31;
static int tcp_min_snd_mss_min = TCP_MIN_SND_MSS;
static int tcp_min_snd_mss_max = 65535;
static int ip_privileged_port_min;
static int ip_privileged_port_max = 65535;
static int ip_ttl_min = 1;
static int ip_ttl_max = 255;
static int tcp_syn_retries_min = 1;
static int tcp_syn_retries_max = MAX_TCP_SYNCNT;
static int tcp_syn_linear_timeouts_max = MAX_TCP_SYNCNT;
static unsigned long ip_ping_group_range_min[] = { 0, 0 };
static unsigned long ip_ping_group_range_max[] = { GID_T_MAX, GID_T_MAX };
static u32 u32_max_div_HZ = UINT_MAX / HZ;
static int one_day_secs = 24 * 3600;
static u32 fib_multipath_hash_fields_all_mask __maybe_unused =
	FIB_MULTIPATH_HASH_FIELD_ALL_MASK;
static unsigned int tcp_child_ehash_entries_max = 16 * 1024 * 1024;
static unsigned int udp_child_hash_entries_max = UDP_HTABLE_SIZE_MAX;
static int tcp_plb_max_rounds = 31;
static int tcp_plb_max_cong_thresh = 256;

/* obsolete */
static int sysctl_tcp_low_latency __read_mostly;

/* Update system visible IP port range */
static void set_local_port_range(struct net *net, int range[2])
{
	bool same_parity = !((range[0] ^ range[1]) & 1);

	write_seqlock_bh(&net->ipv4.ip_local_ports.lock);
	if (same_parity && !net->ipv4.ip_local_ports.warned) {
		net->ipv4.ip_local_ports.warned = true;
		pr_err_ratelimited("ip_local_port_range: prefer different parity for start/end values.\n");
	}
	net->ipv4.ip_local_ports.range[0] = range[0];
	net->ipv4.ip_local_ports.range[1] = range[1];
	write_sequnlock_bh(&net->ipv4.ip_local_ports.lock);
}

/* Validate changes from /proc interface. */
static int ipv4_local_port_range(struct ctl_table *table, int write,
				 void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net =
		container_of(table->data, struct net, ipv4.ip_local_ports.range);
	int ret;
	int range[2];
	struct ctl_table tmp = {
		.data = &range,
		.maxlen = sizeof(range),
		.mode = table->mode,
		.extra1 = &ip_local_port_range_min,
		.extra2 = &ip_local_port_range_max,
	};

	inet_get_local_port_range(net, &range[0], &range[1]);

	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);

	if (write && ret == 0) {
		/* Ensure that the upper limit is not smaller than the lower,
		 * and that the lower does not encroach upon the privileged
		 * port limit.
		 */
		if ((range[1] < range[0]) ||
		    (range[0] < READ_ONCE(net->ipv4.sysctl_ip_prot_sock)))
			ret = -EINVAL;
		else
			set_local_port_range(net, range);
	}

	return ret;
}

/* Validate changes from /proc interface. */
static int ipv4_privileged_ports(struct ctl_table *table, int write,
				void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net = container_of(table->data, struct net,
	    ipv4.sysctl_ip_prot_sock);
	int ret;
	int pports;
	int range[2];
	struct ctl_table tmp = {
		.data = &pports,
		.maxlen = sizeof(pports),
		.mode = table->mode,
		.extra1 = &ip_privileged_port_min,
		.extra2 = &ip_privileged_port_max,
	};

	pports = READ_ONCE(net->ipv4.sysctl_ip_prot_sock);

	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);

	if (write && ret == 0) {
		inet_get_local_port_range(net, &range[0], &range[1]);
		/* Ensure that the local port range doesn't overlap with the
		 * privileged port range.
		 */
		if (range[0] < pports)
			ret = -EINVAL;
		else
			WRITE_ONCE(net->ipv4.sysctl_ip_prot_sock, pports);
	}

	return ret;
}

static void inet_get_ping_group_range_table(struct ctl_table *table, kgid_t *low, kgid_t *high)
{
	kgid_t *data = table->data;
	struct net *net =
		container_of(table->data, struct net, ipv4.ping_group_range.range);
	unsigned int seq;
	do {
		seq = read_seqbegin(&net->ipv4.ping_group_range.lock);

		*low = data[0];
		*high = data[1];
	} while (read_seqretry(&net->ipv4.ping_group_range.lock, seq));
}

/* Update system visible IP port range */
static void set_ping_group_range(struct ctl_table *table, kgid_t low, kgid_t high)
{
	kgid_t *data = table->data;
	struct net *net =
		container_of(table->data, struct net, ipv4.ping_group_range.range);
	write_seqlock(&net->ipv4.ping_group_range.lock);
	data[0] = low;
	data[1] = high;
	write_sequnlock(&net->ipv4.ping_group_range.lock);
}

/* Validate changes from /proc interface. */
static int ipv4_ping_group_range(struct ctl_table *table, int write,
				 void *buffer, size_t *lenp, loff_t *ppos)
{
	struct user_namespace *user_ns = current_user_ns();
	int ret;
	unsigned long urange[2];
	kgid_t low, high;
	struct ctl_table tmp = {
		.data = &urange,
		.maxlen = sizeof(urange),
		.mode = table->mode,
		.extra1 = &ip_ping_group_range_min,
		.extra2 = &ip_ping_group_range_max,
	};

	inet_get_ping_group_range_table(table, &low, &high);
	urange[0] = from_kgid_munged(user_ns, low);
	urange[1] = from_kgid_munged(user_ns, high);
	ret = proc_doulongvec_minmax(&tmp, write, buffer, lenp, ppos);

	if (write && ret == 0) {
		low = make_kgid(user_ns, urange[0]);
		high = make_kgid(user_ns, urange[1]);
		if (!gid_valid(low) || !gid_valid(high))
			return -EINVAL;
		if (urange[1] < urange[0] || gid_lt(high, low)) {
			low = make_kgid(&init_user_ns, 1);
			high = make_kgid(&init_user_ns, 0);
		}
		set_ping_group_range(table, low, high);
	}

	return ret;
}

static int ipv4_fwd_update_priority(struct ctl_table *table, int write,
				    void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net;
	int ret;

	net = container_of(table->data, struct net,
			   ipv4.sysctl_ip_fwd_update_priority);
	ret = proc_dou8vec_minmax(table, write, buffer, lenp, ppos);
	if (write && ret == 0)
		call_netevent_notifiers(NETEVENT_IPV4_FWD_UPDATE_PRIORITY_UPDATE,
					net);

	return ret;
}

static int proc_tcp_congestion_control(struct ctl_table *ctl, int write,
				       void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net = container_of(ctl->data, struct net,
				       ipv4.tcp_congestion_control);
	char val[TCP_CA_NAME_MAX];
	struct ctl_table tbl = {
		.data = val,
		.maxlen = TCP_CA_NAME_MAX,
	};
	int ret;

	tcp_get_default_congestion_control(net, val);

	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	if (write && ret == 0)
		ret = tcp_set_default_congestion_control(net, val);
	return ret;
}

static int proc_tcp_available_congestion_control(struct ctl_table *ctl,
						 int write, void *buffer,
						 size_t *lenp, loff_t *ppos)
{
	struct ctl_table tbl = { .maxlen = TCP_CA_BUF_MAX, };
	int ret;

	tbl.data = kmalloc(tbl.maxlen, GFP_USER);
	if (!tbl.data)
		return -ENOMEM;
	tcp_get_available_congestion_control(tbl.data, TCP_CA_BUF_MAX);
	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	kfree(tbl.data);
	return ret;
}

static int proc_allowed_congestion_control(struct ctl_table *ctl,
					   int write, void *buffer,
					   size_t *lenp, loff_t *ppos)
{
	struct ctl_table tbl = { .maxlen = TCP_CA_BUF_MAX };
	int ret;

	tbl.data = kmalloc(tbl.maxlen, GFP_USER);
	if (!tbl.data)
		return -ENOMEM;

	tcp_get_allowed_congestion_control(tbl.data, tbl.maxlen);
	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	if (write && ret == 0)
		ret = tcp_set_allowed_congestion_control(tbl.data);
	kfree(tbl.data);
	return ret;
}

static int sscanf_key(char *buf, __le32 *key)
{
	u32 user_key[4];
	int i, ret = 0;

	if (sscanf(buf, "%x-%x-%x-%x", user_key, user_key + 1,
		   user_key + 2, user_key + 3) != 4) {
		ret = -EINVAL;
	} else {
		for (i = 0; i < ARRAY_SIZE(user_key); i++)
			key[i] = cpu_to_le32(user_key[i]);
	}
	pr_debug("proc TFO key set 0x%x-%x-%x-%x <- 0x%s: %u\n",
		 user_key[0], user_key[1], user_key[2], user_key[3], buf, ret);

	return ret;
}

static int proc_tcp_fastopen_key(struct ctl_table *table, int write,
				 void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net = container_of(table->data, struct net,
	    ipv4.sysctl_tcp_fastopen);
	/* maxlen to print the list of keys in hex (*2), with dashes
	 * separating doublewords and a comma in between keys.
	 */
	struct ctl_table tbl = { .maxlen = ((TCP_FASTOPEN_KEY_LENGTH *
					    2 * TCP_FASTOPEN_KEY_MAX) +
					    (TCP_FASTOPEN_KEY_MAX * 5)) };
	u32 user_key[TCP_FASTOPEN_KEY_BUF_LENGTH / sizeof(u32)];
	__le32 key[TCP_FASTOPEN_KEY_BUF_LENGTH / sizeof(__le32)];
	char *backup_data;
	int ret, i = 0, off = 0, n_keys;

	tbl.data = kmalloc(tbl.maxlen, GFP_KERNEL);
	if (!tbl.data)
		return -ENOMEM;

	n_keys = tcp_fastopen_get_cipher(net, NULL, (u64 *)key);
	if (!n_keys) {
		memset(&key[0], 0, TCP_FASTOPEN_KEY_LENGTH);
		n_keys = 1;
	}

	for (i = 0; i < n_keys * 4; i++)
		user_key[i] = le32_to_cpu(key[i]);

	for (i = 0; i < n_keys; i++) {
		off += snprintf(tbl.data + off, tbl.maxlen - off,
				"%08x-%08x-%08x-%08x",
				user_key[i * 4],
				user_key[i * 4 + 1],
				user_key[i * 4 + 2],
				user_key[i * 4 + 3]);

		if (WARN_ON_ONCE(off >= tbl.maxlen - 1))
			break;

		if (i + 1 < n_keys)
			off += snprintf(tbl.data + off, tbl.maxlen - off, ",");
	}

	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);

	if (write && ret == 0) {
		backup_data = strchr(tbl.data, ',');
		if (backup_data) {
			*backup_data = '\0';
			backup_data++;
		}
		if (sscanf_key(tbl.data, key)) {
			ret = -EINVAL;
			goto bad_key;
		}
		if (backup_data) {
			if (sscanf_key(backup_data, key + 4)) {
				ret = -EINVAL;
				goto bad_key;
			}
		}
		tcp_fastopen_reset_cipher(net, NULL, key,
					  backup_data ? key + 4 : NULL);
	}

bad_key:
	kfree(tbl.data);
	return ret;
}

static int proc_tfo_blackhole_detect_timeout(struct ctl_table *table,
					     int write, void *buffer,
					     size_t *lenp, loff_t *ppos)
{
	struct net *net = container_of(table->data, struct net,
	    ipv4.sysctl_tcp_fastopen_blackhole_timeout);
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (write && ret == 0)
		atomic_set(&net->ipv4.tfo_active_disable_times, 0);

	return ret;
}

static int proc_tcp_available_ulp(struct ctl_table *ctl,
				  int write, void *buffer, size_t *lenp,
				  loff_t *ppos)
{
	struct ctl_table tbl = { .maxlen = TCP_ULP_BUF_MAX, };
	int ret;

	tbl.data = kmalloc(tbl.maxlen, GFP_USER);
	if (!tbl.data)
		return -ENOMEM;
	tcp_get_available_ulp(tbl.data, TCP_ULP_BUF_MAX);
	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	kfree(tbl.data);

	return ret;
}

static int proc_tcp_ehash_entries(struct ctl_table *table, int write,
				  void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net = container_of(table->data, struct net,
				       ipv4.sysctl_tcp_child_ehash_entries);
	struct inet_hashinfo *hinfo = net->ipv4.tcp_death_row.hashinfo;
	int tcp_ehash_entries;
	struct ctl_table tbl;

	tcp_ehash_entries = hinfo->ehash_mask + 1;

	/* A negative number indicates that the child netns
	 * shares the global ehash.
	 */
	if (!net_eq(net, &init_net) && !hinfo->pernet)
		tcp_ehash_entries *= -1;

	memset(&tbl, 0, sizeof(tbl));
	tbl.data = &tcp_ehash_entries;
	tbl.maxlen = sizeof(int);

	return proc_dointvec(&tbl, write, buffer, lenp, ppos);
}

static int proc_udp_hash_entries(struct ctl_table *table, int write,
				 void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net = container_of(table->data, struct net,
				       ipv4.sysctl_udp_child_hash_entries);
	int udp_hash_entries;
	struct ctl_table tbl;

	udp_hash_entries = net->ipv4.udp_table->mask + 1;

	/* A negative number indicates that the child netns
	 * shares the global udp_table.
	 */
	if (!net_eq(net, &init_net) && net->ipv4.udp_table == &udp_table)
		udp_hash_entries *= -1;

	memset(&tbl, 0, sizeof(tbl));
	tbl.data = &udp_hash_entries;
	tbl.maxlen = sizeof(int);

	return proc_dointvec(&tbl, write, buffer, lenp, ppos);
}

#ifdef CONFIG_IP_ROUTE_MULTIPATH
static int proc_fib_multipath_hash_policy(struct ctl_table *table, int write,
					  void *buffer, size_t *lenp,
					  loff_t *ppos)
{
	struct net *net = container_of(table->data, struct net,
	    ipv4.sysctl_fib_multipath_hash_policy);
	int ret;

	ret = proc_dou8vec_minmax(table, write, buffer, lenp, ppos);
	if (write && ret == 0)
		call_netevent_notifiers(NETEVENT_IPV4_MPATH_HASH_UPDATE, net);

	return ret;
}

static int proc_fib_multipath_hash_fields(struct ctl_table *table, int write,
					  void *buffer, size_t *lenp,
					  loff_t *ppos)
{
	struct net *net;
	int ret;

	net = container_of(table->data, struct net,
			   ipv4.sysctl_fib_multipath_hash_fields);
	ret = proc_douintvec_minmax(table, write, buffer, lenp, ppos);
	if (write && ret == 0)
		call_netevent_notifiers(NETEVENT_IPV4_MPATH_HASH_UPDATE, net);

	return ret;
}
#endif

static struct ctl_table ipv4_table[] = {
	{
		.procname	= "tcp_max_orphans",
		.data		= &sysctl_tcp_max_orphans,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "inet_peer_threshold",
		.data		= &inet_peer_threshold,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "inet_peer_minttl",
		.data		= &inet_peer_minttl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "inet_peer_maxttl",
		.data		= &inet_peer_maxttl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "tcp_mem",
		.maxlen		= sizeof(sysctl_tcp_mem),
		.data		= &sysctl_tcp_mem,
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "tcp_low_latency",
		.data		= &sysctl_tcp_low_latency,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
#ifdef CONFIG_NETLABEL
	{
		.procname	= "cipso_cache_enable",
		.data		= &cipso_v4_cache_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "cipso_cache_bucket_size",
		.data		= &cipso_v4_cache_bucketsize,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "cipso_rbm_optfmt",
		.data		= &cipso_v4_rbm_optfmt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "cipso_rbm_strictvalid",
		.data		= &cipso_v4_rbm_strictvalid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif /* CONFIG_NETLABEL */
	{
		.procname	= "tcp_available_ulp",
		.maxlen		= TCP_ULP_BUF_MAX,
		.mode		= 0444,
		.proc_handler   = proc_tcp_available_ulp,
	},
	{
		.procname	= "icmp_msgs_per_sec",
		.data		= &sysctl_icmp_msgs_per_sec,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname	= "icmp_msgs_burst",
		.data		= &sysctl_icmp_msgs_burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname	= "udp_mem",
		.data		= &sysctl_udp_mem,
		.maxlen		= sizeof(sysctl_udp_mem),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "fib_sync_mem",
		.data		= &sysctl_fib_sync_mem,
		.maxlen		= sizeof(sysctl_fib_sync_mem),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= &sysctl_fib_sync_mem_min,
		.extra2		= &sysctl_fib_sync_mem_max,
	},
	{ }
};

static struct ctl_table ipv4_net_table[] = {
	{
		.procname	= "tcp_max_tw_buckets",
		.data		= &init_net.ipv4.tcp_death_row.sysctl_max_tw_buckets,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "icmp_echo_ignore_all",
		.data		= &init_net.ipv4.sysctl_icmp_echo_ignore_all,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE
	},
	{
		.procname	= "icmp_echo_enable_probe",
		.data		= &init_net.ipv4.sysctl_icmp_echo_enable_probe,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE
	},
	{
		.procname	= "icmp_echo_ignore_broadcasts",
		.data		= &init_net.ipv4.sysctl_icmp_echo_ignore_broadcasts,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE
	},
	{
		.procname	= "icmp_ignore_bogus_error_responses",
		.data		= &init_net.ipv4.sysctl_icmp_ignore_bogus_error_responses,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE
	},
	{
		.procname	= "icmp_errors_use_inbound_ifaddr",
		.data		= &init_net.ipv4.sysctl_icmp_errors_use_inbound_ifaddr,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE
	},
	{
		.procname	= "icmp_ratelimit",
		.data		= &init_net.ipv4.sysctl_icmp_ratelimit,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_ms_jiffies,
	},
	{
		.procname	= "icmp_ratemask",
		.data		= &init_net.ipv4.sysctl_icmp_ratemask,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "ping_group_range",
		.data		= &init_net.ipv4.ping_group_range.range,
		.maxlen		= sizeof(gid_t)*2,
		.mode		= 0644,
		.proc_handler	= ipv4_ping_group_range,
	},
#ifdef CONFIG_NET_L3_MASTER_DEV
	{
		.procname	= "raw_l3mdev_accept",
		.data		= &init_net.ipv4.sysctl_raw_l3mdev_accept,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
#endif
	{
		.procname	= "tcp_ecn",
		.data		= &init_net.ipv4.sysctl_tcp_ecn,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
	{
		.procname	= "tcp_ecn_fallback",
		.data		= &init_net.ipv4.sysctl_tcp_ecn_fallback,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "ip_dynaddr",
		.data		= &init_net.ipv4.sysctl_ip_dynaddr,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "ip_early_demux",
		.data		= &init_net.ipv4.sysctl_ip_early_demux,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname       = "udp_early_demux",
		.data           = &init_net.ipv4.sysctl_udp_early_demux,
		.maxlen         = sizeof(u8),
		.mode           = 0644,
		.proc_handler   = proc_dou8vec_minmax,
	},
	{
		.procname       = "tcp_early_demux",
		.data           = &init_net.ipv4.sysctl_tcp_early_demux,
		.maxlen         = sizeof(u8),
		.mode           = 0644,
		.proc_handler   = proc_dou8vec_minmax,
	},
	{
		.procname       = "nexthop_compat_mode",
		.data           = &init_net.ipv4.sysctl_nexthop_compat_mode,
		.maxlen         = sizeof(u8),
		.mode           = 0644,
		.proc_handler   = proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "ip_default_ttl",
		.data		= &init_net.ipv4.sysctl_ip_default_ttl,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= &ip_ttl_min,
		.extra2		= &ip_ttl_max,
	},
	{
		.procname	= "ip_local_port_range",
		.maxlen		= sizeof(init_net.ipv4.ip_local_ports.range),
		.data		= &init_net.ipv4.ip_local_ports.range,
		.mode		= 0644,
		.proc_handler	= ipv4_local_port_range,
	},
	{
		.procname	= "ip_local_reserved_ports",
		.data		= &init_net.ipv4.sysctl_local_reserved_ports,
		.maxlen		= 65536,
		.mode		= 0644,
		.proc_handler	= proc_do_large_bitmap,
	},
	{
		.procname	= "ip_no_pmtu_disc",
		.data		= &init_net.ipv4.sysctl_ip_no_pmtu_disc,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "ip_forward_use_pmtu",
		.data		= &init_net.ipv4.sysctl_ip_fwd_use_pmtu,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "ip_forward_update_priority",
		.data		= &init_net.ipv4.sysctl_ip_fwd_update_priority,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler   = ipv4_fwd_update_priority,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "ip_nonlocal_bind",
		.data		= &init_net.ipv4.sysctl_ip_nonlocal_bind,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "ip_autobind_reuse",
		.data		= &init_net.ipv4.sysctl_ip_autobind_reuse,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1         = SYSCTL_ZERO,
		.extra2         = SYSCTL_ONE,
	},
	{
		.procname	= "fwmark_reflect",
		.data		= &init_net.ipv4.sysctl_fwmark_reflect,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_fwmark_accept",
		.data		= &init_net.ipv4.sysctl_tcp_fwmark_accept,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
#ifdef CONFIG_NET_L3_MASTER_DEV
	{
		.procname	= "tcp_l3mdev_accept",
		.data		= &init_net.ipv4.sysctl_tcp_l3mdev_accept,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
#endif
	{
		.procname	= "tcp_mtu_probing",
		.data		= &init_net.ipv4.sysctl_tcp_mtu_probing,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_base_mss",
		.data		= &init_net.ipv4.sysctl_tcp_base_mss,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "tcp_min_snd_mss",
		.data		= &init_net.ipv4.sysctl_tcp_min_snd_mss,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &tcp_min_snd_mss_min,
		.extra2		= &tcp_min_snd_mss_max,
	},
	{
		.procname	= "tcp_mtu_probe_floor",
		.data		= &init_net.ipv4.sysctl_tcp_mtu_probe_floor,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &tcp_min_snd_mss_min,
		.extra2		= &tcp_min_snd_mss_max,
	},
	{
		.procname	= "tcp_probe_threshold",
		.data		= &init_net.ipv4.sysctl_tcp_probe_threshold,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "tcp_probe_interval",
		.data		= &init_net.ipv4.sysctl_tcp_probe_interval,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra2		= &u32_max_div_HZ,
	},
	{
		.procname	= "igmp_link_local_mcast_reports",
		.data		= &init_net.ipv4.sysctl_igmp_llm_reports,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "igmp_max_memberships",
		.data		= &init_net.ipv4.sysctl_igmp_max_memberships,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "igmp_max_msf",
		.data		= &init_net.ipv4.sysctl_igmp_max_msf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
#ifdef CONFIG_IP_MULTICAST
	{
		.procname	= "igmp_qrv",
		.data		= &init_net.ipv4.sysctl_igmp_qrv,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE
	},
#endif
	{
		.procname	= "tcp_congestion_control",
		.data		= &init_net.ipv4.tcp_congestion_control,
		.mode		= 0644,
		.maxlen		= TCP_CA_NAME_MAX,
		.proc_handler	= proc_tcp_congestion_control,
	},
	{
		.procname	= "tcp_available_congestion_control",
		.maxlen		= TCP_CA_BUF_MAX,
		.mode		= 0444,
		.proc_handler   = proc_tcp_available_congestion_control,
	},
	{
		.procname	= "tcp_allowed_congestion_control",
		.maxlen		= TCP_CA_BUF_MAX,
		.mode		= 0644,
		.proc_handler   = proc_allowed_congestion_control,
	},
	{
		.procname	= "tcp_keepalive_time",
		.data		= &init_net.ipv4.sysctl_tcp_keepalive_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "tcp_keepalive_probes",
		.data		= &init_net.ipv4.sysctl_tcp_keepalive_probes,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_keepalive_intvl",
		.data		= &init_net.ipv4.sysctl_tcp_keepalive_intvl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "tcp_syn_retries",
		.data		= &init_net.ipv4.sysctl_tcp_syn_retries,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= &tcp_syn_retries_min,
		.extra2		= &tcp_syn_retries_max
	},
	{
		.procname	= "tcp_synack_retries",
		.data		= &init_net.ipv4.sysctl_tcp_synack_retries,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
#ifdef CONFIG_SYN_COOKIES
	{
		.procname	= "tcp_syncookies",
		.data		= &init_net.ipv4.sysctl_tcp_syncookies,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
#endif
	{
		.procname	= "tcp_migrate_req",
		.data		= &init_net.ipv4.sysctl_tcp_migrate_req,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE
	},
	{
		.procname	= "tcp_reordering",
		.data		= &init_net.ipv4.sysctl_tcp_reordering,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "tcp_retries1",
		.data		= &init_net.ipv4.sysctl_tcp_retries1,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra2		= &tcp_retr1_max
	},
	{
		.procname	= "tcp_retries2",
		.data		= &init_net.ipv4.sysctl_tcp_retries2,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_orphan_retries",
		.data		= &init_net.ipv4.sysctl_tcp_orphan_retries,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_fin_timeout",
		.data		= &init_net.ipv4.sysctl_tcp_fin_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "tcp_notsent_lowat",
		.data		= &init_net.ipv4.sysctl_tcp_notsent_lowat,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec,
	},
	{
		.procname	= "tcp_tw_reuse",
		.data		= &init_net.ipv4.sysctl_tcp_tw_reuse,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
	{
		.procname	= "tcp_max_syn_backlog",
		.data		= &init_net.ipv4.sysctl_max_syn_backlog,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "tcp_fastopen",
		.data		= &init_net.ipv4.sysctl_tcp_fastopen,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "tcp_fastopen_key",
		.mode		= 0600,
		.data		= &init_net.ipv4.sysctl_tcp_fastopen,
		/* maxlen to print the list of keys in hex (*2), with dashes
		 * separating doublewords and a comma in between keys.
		 */
		.maxlen		= ((TCP_FASTOPEN_KEY_LENGTH *
				   2 * TCP_FASTOPEN_KEY_MAX) +
				   (TCP_FASTOPEN_KEY_MAX * 5)),
		.proc_handler	= proc_tcp_fastopen_key,
	},
	{
		.procname	= "tcp_fastopen_blackhole_timeout_sec",
		.data		= &init_net.ipv4.sysctl_tcp_fastopen_blackhole_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_tfo_blackhole_detect_timeout,
		.extra1		= SYSCTL_ZERO,
	},
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	{
		.procname	= "fib_multipath_use_neigh",
		.data		= &init_net.ipv4.sysctl_fib_multipath_use_neigh,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "fib_multipath_hash_policy",
		.data		= &init_net.ipv4.sysctl_fib_multipath_hash_policy,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_fib_multipath_hash_policy,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_THREE,
	},
	{
		.procname	= "fib_multipath_hash_fields",
		.data		= &init_net.ipv4.sysctl_fib_multipath_hash_fields,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= proc_fib_multipath_hash_fields,
		.extra1		= SYSCTL_ONE,
		.extra2		= &fib_multipath_hash_fields_all_mask,
	},
#endif
	{
		.procname	= "ip_unprivileged_port_start",
		.maxlen		= sizeof(int),
		.data		= &init_net.ipv4.sysctl_ip_prot_sock,
		.mode		= 0644,
		.proc_handler	= ipv4_privileged_ports,
	},
#ifdef CONFIG_NET_L3_MASTER_DEV
	{
		.procname	= "udp_l3mdev_accept",
		.data		= &init_net.ipv4.sysctl_udp_l3mdev_accept,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
#endif
	{
		.procname	= "tcp_sack",
		.data		= &init_net.ipv4.sysctl_tcp_sack,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_window_scaling",
		.data		= &init_net.ipv4.sysctl_tcp_window_scaling,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_timestamps",
		.data		= &init_net.ipv4.sysctl_tcp_timestamps,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_early_retrans",
		.data		= &init_net.ipv4.sysctl_tcp_early_retrans,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_FOUR,
	},
	{
		.procname	= "tcp_recovery",
		.data		= &init_net.ipv4.sysctl_tcp_recovery,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname       = "tcp_thin_linear_timeouts",
		.data           = &init_net.ipv4.sysctl_tcp_thin_linear_timeouts,
		.maxlen         = sizeof(u8),
		.mode           = 0644,
		.proc_handler   = proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_slow_start_after_idle",
		.data		= &init_net.ipv4.sysctl_tcp_slow_start_after_idle,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_retrans_collapse",
		.data		= &init_net.ipv4.sysctl_tcp_retrans_collapse,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_stdurg",
		.data		= &init_net.ipv4.sysctl_tcp_stdurg,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_rfc1337",
		.data		= &init_net.ipv4.sysctl_tcp_rfc1337,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_abort_on_overflow",
		.data		= &init_net.ipv4.sysctl_tcp_abort_on_overflow,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_fack",
		.data		= &init_net.ipv4.sysctl_tcp_fack,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_max_reordering",
		.data		= &init_net.ipv4.sysctl_tcp_max_reordering,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "tcp_dsack",
		.data		= &init_net.ipv4.sysctl_tcp_dsack,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_app_win",
		.data		= &init_net.ipv4.sysctl_tcp_app_win,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &tcp_app_win_max,
	},
	{
		.procname	= "tcp_adv_win_scale",
		.data		= &init_net.ipv4.sysctl_tcp_adv_win_scale,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &tcp_adv_win_scale_min,
		.extra2		= &tcp_adv_win_scale_max,
	},
	{
		.procname	= "tcp_frto",
		.data		= &init_net.ipv4.sysctl_tcp_frto,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_no_metrics_save",
		.data		= &init_net.ipv4.sysctl_tcp_nometrics_save,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_no_ssthresh_metrics_save",
		.data		= &init_net.ipv4.sysctl_tcp_no_ssthresh_metrics_save,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "tcp_moderate_rcvbuf",
		.data		= &init_net.ipv4.sysctl_tcp_moderate_rcvbuf,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_tso_win_divisor",
		.data		= &init_net.ipv4.sysctl_tcp_tso_win_divisor,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_workaround_signed_windows",
		.data		= &init_net.ipv4.sysctl_tcp_workaround_signed_windows,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_limit_output_bytes",
		.data		= &init_net.ipv4.sysctl_tcp_limit_output_bytes,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "tcp_challenge_ack_limit",
		.data		= &init_net.ipv4.sysctl_tcp_challenge_ack_limit,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "tcp_min_tso_segs",
		.data		= &init_net.ipv4.sysctl_tcp_min_tso_segs,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ONE,
	},
	{
		.procname	= "tcp_tso_rtt_log",
		.data		= &init_net.ipv4.sysctl_tcp_tso_rtt_log,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{
		.procname	= "tcp_min_rtt_wlen",
		.data		= &init_net.ipv4.sysctl_tcp_min_rtt_wlen,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &one_day_secs
	},
	{
		.procname	= "tcp_autocorking",
		.data		= &init_net.ipv4.sysctl_tcp_autocorking,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "tcp_invalid_ratelimit",
		.data		= &init_net.ipv4.sysctl_tcp_invalid_ratelimit,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_ms_jiffies,
	},
	{
		.procname	= "tcp_pacing_ss_ratio",
		.data		= &init_net.ipv4.sysctl_tcp_pacing_ss_ratio,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE_THOUSAND,
	},
	{
		.procname	= "tcp_pacing_ca_ratio",
		.data		= &init_net.ipv4.sysctl_tcp_pacing_ca_ratio,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE_THOUSAND,
	},
	{
		.procname	= "tcp_wmem",
		.data		= &init_net.ipv4.sysctl_tcp_wmem,
		.maxlen		= sizeof(init_net.ipv4.sysctl_tcp_wmem),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
	},
	{
		.procname	= "tcp_rmem",
		.data		= &init_net.ipv4.sysctl_tcp_rmem,
		.maxlen		= sizeof(init_net.ipv4.sysctl_tcp_rmem),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
	},
	{
		.procname	= "tcp_comp_sack_delay_ns",
		.data		= &init_net.ipv4.sysctl_tcp_comp_sack_delay_ns,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "tcp_comp_sack_slack_ns",
		.data		= &init_net.ipv4.sysctl_tcp_comp_sack_slack_ns,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "tcp_comp_sack_nr",
		.data		= &init_net.ipv4.sysctl_tcp_comp_sack_nr,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname       = "tcp_reflect_tos",
		.data           = &init_net.ipv4.sysctl_tcp_reflect_tos,
		.maxlen         = sizeof(u8),
		.mode           = 0644,
		.proc_handler   = proc_dou8vec_minmax,
		.extra1         = SYSCTL_ZERO,
		.extra2         = SYSCTL_ONE,
	},
	{
		.procname	= "tcp_ehash_entries",
		.data		= &init_net.ipv4.sysctl_tcp_child_ehash_entries,
		.mode		= 0444,
		.proc_handler	= proc_tcp_ehash_entries,
	},
	{
		.procname	= "tcp_child_ehash_entries",
		.data		= &init_net.ipv4.sysctl_tcp_child_ehash_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &tcp_child_ehash_entries_max,
	},
	{
		.procname	= "udp_hash_entries",
		.data		= &init_net.ipv4.sysctl_udp_child_hash_entries,
		.mode		= 0444,
		.proc_handler	= proc_udp_hash_entries,
	},
	{
		.procname	= "udp_child_hash_entries",
		.data		= &init_net.ipv4.sysctl_udp_child_hash_entries,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &udp_child_hash_entries_max,
	},
	{
		.procname	= "udp_rmem_min",
		.data		= &init_net.ipv4.sysctl_udp_rmem_min,
		.maxlen		= sizeof(init_net.ipv4.sysctl_udp_rmem_min),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE
	},
	{
		.procname	= "udp_wmem_min",
		.data		= &init_net.ipv4.sysctl_udp_wmem_min,
		.maxlen		= sizeof(init_net.ipv4.sysctl_udp_wmem_min),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE
	},
	{
		.procname	= "fib_notify_on_flag_change",
		.data		= &init_net.ipv4.sysctl_fib_notify_on_flag_change,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
	{
		.procname       = "tcp_plb_enabled",
		.data           = &init_net.ipv4.sysctl_tcp_plb_enabled,
		.maxlen         = sizeof(u8),
		.mode           = 0644,
		.proc_handler   = proc_dou8vec_minmax,
		.extra1         = SYSCTL_ZERO,
		.extra2         = SYSCTL_ONE,
	},
	{
		.procname       = "tcp_plb_idle_rehash_rounds",
		.data           = &init_net.ipv4.sysctl_tcp_plb_idle_rehash_rounds,
		.maxlen         = sizeof(u8),
		.mode           = 0644,
		.proc_handler   = proc_dou8vec_minmax,
		.extra2		= &tcp_plb_max_rounds,
	},
	{
		.procname       = "tcp_plb_rehash_rounds",
		.data           = &init_net.ipv4.sysctl_tcp_plb_rehash_rounds,
		.maxlen         = sizeof(u8),
		.mode           = 0644,
		.proc_handler   = proc_dou8vec_minmax,
		.extra2         = &tcp_plb_max_rounds,
	},
	{
		.procname       = "tcp_plb_suspend_rto_sec",
		.data           = &init_net.ipv4.sysctl_tcp_plb_suspend_rto_sec,
		.maxlen         = sizeof(u8),
		.mode           = 0644,
		.proc_handler   = proc_dou8vec_minmax,
	},
	{
		.procname       = "tcp_plb_cong_thresh",
		.data           = &init_net.ipv4.sysctl_tcp_plb_cong_thresh,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = SYSCTL_ZERO,
		.extra2         = &tcp_plb_max_cong_thresh,
	},
	{
		.procname	= "tcp_syn_linear_timeouts",
		.data		= &init_net.ipv4.sysctl_tcp_syn_linear_timeouts,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &tcp_syn_linear_timeouts_max,
	},
	{
		.procname	= "tcp_shrink_window",
		.data		= &init_net.ipv4.sysctl_tcp_shrink_window,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{ }
};

static __net_init int ipv4_sysctl_init_net(struct net *net)
{
	struct ctl_table *table;

	table = ipv4_net_table;
	if (!net_eq(net, &init_net)) {
		int i;

		table = kmemdup(table, sizeof(ipv4_net_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;

		for (i = 0; i < ARRAY_SIZE(ipv4_net_table) - 1; i++) {
			if (table[i].data) {
				/* Update the variables to point into
				 * the current struct net
				 */
				table[i].data += (void *)net - (void *)&init_net;
			} else {
				/* Entries without data pointer are global;
				 * Make them read-only in non-init_net ns
				 */
				table[i].mode &= ~0222;
			}
		}
	}

	net->ipv4.ipv4_hdr = register_net_sysctl_sz(net, "net/ipv4", table,
						    ARRAY_SIZE(ipv4_net_table));
	if (!net->ipv4.ipv4_hdr)
		goto err_reg;

	net->ipv4.sysctl_local_reserved_ports = kzalloc(65536 / 8, GFP_KERNEL);
	if (!net->ipv4.sysctl_local_reserved_ports)
		goto err_ports;

	return 0;

err_ports:
	unregister_net_sysctl_table(net->ipv4.ipv4_hdr);
err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static __net_exit void ipv4_sysctl_exit_net(struct net *net)
{
	struct ctl_table *table;

	kfree(net->ipv4.sysctl_local_reserved_ports);
	table = net->ipv4.ipv4_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv4.ipv4_hdr);
	kfree(table);
}

static __net_initdata struct pernet_operations ipv4_sysctl_ops = {
	.init = ipv4_sysctl_init_net,
	.exit = ipv4_sysctl_exit_net,
};

static __init int sysctl_ipv4_init(void)
{
	struct ctl_table_header *hdr;

	hdr = register_net_sysctl(&init_net, "net/ipv4", ipv4_table);
	if (!hdr)
		return -ENOMEM;

	if (register_pernet_subsys(&ipv4_sysctl_ops)) {
		unregister_net_sysctl_table(hdr);
		return -ENOMEM;
	}

	return 0;
}

__initcall(sysctl_ipv4_init);
