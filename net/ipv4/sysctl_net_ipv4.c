/*
 * sysctl_net_ipv4.c: sysctl interface to net IPV4 subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ipv4 directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/igmp.h>
#include <linux/inetdevice.h>
#include <linux/seqlock.h>
#include <linux/init.h>
#include <net/snmp.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/cipso_ipv4.h>
#include <net/inet_frag.h>

static int zero;
static int tcp_retr1_max = 255;
static int ip_local_port_range_min[] = { 1, 1 };
static int ip_local_port_range_max[] = { 65535, 65535 };

/* Update system visible IP port range */
static void set_local_port_range(int range[2])
{
	write_seqlock(&sysctl_local_ports.lock);
	sysctl_local_ports.range[0] = range[0];
	sysctl_local_ports.range[1] = range[1];
	write_sequnlock(&sysctl_local_ports.lock);
}

/* Validate changes from /proc interface. */
static int ipv4_local_port_range(ctl_table *table, int write,
				 void __user *buffer,
				 size_t *lenp, loff_t *ppos)
{
	int ret;
	int range[2];
	ctl_table tmp = {
		.data = &range,
		.maxlen = sizeof(range),
		.mode = table->mode,
		.extra1 = &ip_local_port_range_min,
		.extra2 = &ip_local_port_range_max,
	};

	inet_get_local_port_range(range, range + 1);
	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);

	if (write && ret == 0) {
		if (range[1] < range[0])
			ret = -EINVAL;
		else
			set_local_port_range(range);
	}

	return ret;
}

/* Validate changes from sysctl interface. */
static int ipv4_sysctl_local_port_range(ctl_table *table,
					 void __user *oldval,
					 size_t __user *oldlenp,
					void __user *newval, size_t newlen)
{
	int ret;
	int range[2];
	ctl_table tmp = {
		.data = &range,
		.maxlen = sizeof(range),
		.mode = table->mode,
		.extra1 = &ip_local_port_range_min,
		.extra2 = &ip_local_port_range_max,
	};

	inet_get_local_port_range(range, range + 1);
	ret = sysctl_intvec(&tmp, oldval, oldlenp, newval, newlen);
	if (ret == 0 && newval && newlen) {
		if (range[1] < range[0])
			ret = -EINVAL;
		else
			set_local_port_range(range);
	}
	return ret;
}


static int proc_tcp_congestion_control(ctl_table *ctl, int write,
				       void __user *buffer, size_t *lenp, loff_t *ppos)
{
	char val[TCP_CA_NAME_MAX];
	ctl_table tbl = {
		.data = val,
		.maxlen = TCP_CA_NAME_MAX,
	};
	int ret;

	tcp_get_default_congestion_control(val);

	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	if (write && ret == 0)
		ret = tcp_set_default_congestion_control(val);
	return ret;
}

static int sysctl_tcp_congestion_control(ctl_table *table,
					 void __user *oldval,
					 size_t __user *oldlenp,
					 void __user *newval, size_t newlen)
{
	char val[TCP_CA_NAME_MAX];
	ctl_table tbl = {
		.data = val,
		.maxlen = TCP_CA_NAME_MAX,
	};
	int ret;

	tcp_get_default_congestion_control(val);
	ret = sysctl_string(&tbl, oldval, oldlenp, newval, newlen);
	if (ret == 1 && newval && newlen)
		ret = tcp_set_default_congestion_control(val);
	return ret;
}

static int proc_tcp_available_congestion_control(ctl_table *ctl,
						 int write,
						 void __user *buffer, size_t *lenp,
						 loff_t *ppos)
{
	ctl_table tbl = { .maxlen = TCP_CA_BUF_MAX, };
	int ret;

	tbl.data = kmalloc(tbl.maxlen, GFP_USER);
	if (!tbl.data)
		return -ENOMEM;
	tcp_get_available_congestion_control(tbl.data, TCP_CA_BUF_MAX);
	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	kfree(tbl.data);
	return ret;
}

static int proc_allowed_congestion_control(ctl_table *ctl,
					   int write,
					   void __user *buffer, size_t *lenp,
					   loff_t *ppos)
{
	ctl_table tbl = { .maxlen = TCP_CA_BUF_MAX };
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

static int strategy_allowed_congestion_control(ctl_table *table,
					       void __user *oldval,
					       size_t __user *oldlenp,
					       void __user *newval,
					       size_t newlen)
{
	ctl_table tbl = { .maxlen = TCP_CA_BUF_MAX };
	int ret;

	tbl.data = kmalloc(tbl.maxlen, GFP_USER);
	if (!tbl.data)
		return -ENOMEM;

	tcp_get_available_congestion_control(tbl.data, tbl.maxlen);
	ret = sysctl_string(&tbl, oldval, oldlenp, newval, newlen);
	if (ret == 1 && newval && newlen)
		ret = tcp_set_allowed_congestion_control(tbl.data);
	kfree(tbl.data);

	return ret;

}

static struct ctl_table ipv4_table[] = {
	{
		.ctl_name	= NET_IPV4_TCP_TIMESTAMPS,
		.procname	= "tcp_timestamps",
		.data		= &sysctl_tcp_timestamps,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_WINDOW_SCALING,
		.procname	= "tcp_window_scaling",
		.data		= &sysctl_tcp_window_scaling,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_SACK,
		.procname	= "tcp_sack",
		.data		= &sysctl_tcp_sack,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_RETRANS_COLLAPSE,
		.procname	= "tcp_retrans_collapse",
		.data		= &sysctl_tcp_retrans_collapse,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_DEFAULT_TTL,
		.procname	= "ip_default_ttl",
		.data		= &sysctl_ip_default_ttl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= ipv4_doint_and_flush,
		.strategy	= ipv4_doint_and_flush_strategy,
		.extra2		= &init_net,
	},
	{
		.ctl_name	= NET_IPV4_NO_PMTU_DISC,
		.procname	= "ip_no_pmtu_disc",
		.data		= &ipv4_config.no_pmtu_disc,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_NONLOCAL_BIND,
		.procname	= "ip_nonlocal_bind",
		.data		= &sysctl_ip_nonlocal_bind,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_SYN_RETRIES,
		.procname	= "tcp_syn_retries",
		.data		= &sysctl_tcp_syn_retries,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_SYNACK_RETRIES,
		.procname	= "tcp_synack_retries",
		.data		= &sysctl_tcp_synack_retries,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_MAX_ORPHANS,
		.procname	= "tcp_max_orphans",
		.data		= &sysctl_tcp_max_orphans,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_MAX_TW_BUCKETS,
		.procname	= "tcp_max_tw_buckets",
		.data		= &tcp_death_row.sysctl_max_tw_buckets,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_DYNADDR,
		.procname	= "ip_dynaddr",
		.data		= &sysctl_ip_dynaddr,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_KEEPALIVE_TIME,
		.procname	= "tcp_keepalive_time",
		.data		= &sysctl_tcp_keepalive_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
		.strategy	= sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_TCP_KEEPALIVE_PROBES,
		.procname	= "tcp_keepalive_probes",
		.data		= &sysctl_tcp_keepalive_probes,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_KEEPALIVE_INTVL,
		.procname	= "tcp_keepalive_intvl",
		.data		= &sysctl_tcp_keepalive_intvl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
		.strategy	= sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_TCP_RETRIES1,
		.procname	= "tcp_retries1",
		.data		= &sysctl_tcp_retries1,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra2		= &tcp_retr1_max
	},
	{
		.ctl_name	= NET_IPV4_TCP_RETRIES2,
		.procname	= "tcp_retries2",
		.data		= &sysctl_tcp_retries2,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_FIN_TIMEOUT,
		.procname	= "tcp_fin_timeout",
		.data		= &sysctl_tcp_fin_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
		.strategy	= sysctl_jiffies
	},
#ifdef CONFIG_SYN_COOKIES
	{
		.ctl_name	= NET_TCP_SYNCOOKIES,
		.procname	= "tcp_syncookies",
		.data		= &sysctl_tcp_syncookies,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
#endif
	{
		.ctl_name	= NET_TCP_TW_RECYCLE,
		.procname	= "tcp_tw_recycle",
		.data		= &tcp_death_row.sysctl_tw_recycle,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_ABORT_ON_OVERFLOW,
		.procname	= "tcp_abort_on_overflow",
		.data		= &sysctl_tcp_abort_on_overflow,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_STDURG,
		.procname	= "tcp_stdurg",
		.data		= &sysctl_tcp_stdurg,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_RFC1337,
		.procname	= "tcp_rfc1337",
		.data		= &sysctl_tcp_rfc1337,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_MAX_SYN_BACKLOG,
		.procname	= "tcp_max_syn_backlog",
		.data		= &sysctl_max_syn_backlog,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_LOCAL_PORT_RANGE,
		.procname	= "ip_local_port_range",
		.data		= &sysctl_local_ports.range,
		.maxlen		= sizeof(sysctl_local_ports.range),
		.mode		= 0644,
		.proc_handler	= ipv4_local_port_range,
		.strategy	= ipv4_sysctl_local_port_range,
	},
#ifdef CONFIG_IP_MULTICAST
	{
		.ctl_name	= NET_IPV4_IGMP_MAX_MEMBERSHIPS,
		.procname	= "igmp_max_memberships",
		.data		= &sysctl_igmp_max_memberships,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},

#endif
	{
		.ctl_name	= NET_IPV4_IGMP_MAX_MSF,
		.procname	= "igmp_max_msf",
		.data		= &sysctl_igmp_max_msf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_INET_PEER_THRESHOLD,
		.procname	= "inet_peer_threshold",
		.data		= &inet_peer_threshold,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_INET_PEER_MINTTL,
		.procname	= "inet_peer_minttl",
		.data		= &inet_peer_minttl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
		.strategy	= sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_INET_PEER_MAXTTL,
		.procname	= "inet_peer_maxttl",
		.data		= &inet_peer_maxttl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
		.strategy	= sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_INET_PEER_GC_MINTIME,
		.procname	= "inet_peer_gc_mintime",
		.data		= &inet_peer_gc_mintime,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
		.strategy	= sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_INET_PEER_GC_MAXTIME,
		.procname	= "inet_peer_gc_maxtime",
		.data		= &inet_peer_gc_maxtime,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
		.strategy	= sysctl_jiffies
	},
	{
		.ctl_name	= NET_TCP_ORPHAN_RETRIES,
		.procname	= "tcp_orphan_retries",
		.data		= &sysctl_tcp_orphan_retries,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_FACK,
		.procname	= "tcp_fack",
		.data		= &sysctl_tcp_fack,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_REORDERING,
		.procname	= "tcp_reordering",
		.data		= &sysctl_tcp_reordering,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_ECN,
		.procname	= "tcp_ecn",
		.data		= &sysctl_tcp_ecn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_DSACK,
		.procname	= "tcp_dsack",
		.data		= &sysctl_tcp_dsack,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_MEM,
		.procname	= "tcp_mem",
		.data		= &sysctl_tcp_mem,
		.maxlen		= sizeof(sysctl_tcp_mem),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_WMEM,
		.procname	= "tcp_wmem",
		.data		= &sysctl_tcp_wmem,
		.maxlen		= sizeof(sysctl_tcp_wmem),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_RMEM,
		.procname	= "tcp_rmem",
		.data		= &sysctl_tcp_rmem,
		.maxlen		= sizeof(sysctl_tcp_rmem),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_APP_WIN,
		.procname	= "tcp_app_win",
		.data		= &sysctl_tcp_app_win,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_ADV_WIN_SCALE,
		.procname	= "tcp_adv_win_scale",
		.data		= &sysctl_tcp_adv_win_scale,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_TW_REUSE,
		.procname	= "tcp_tw_reuse",
		.data		= &sysctl_tcp_tw_reuse,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_FRTO,
		.procname	= "tcp_frto",
		.data		= &sysctl_tcp_frto,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_FRTO_RESPONSE,
		.procname	= "tcp_frto_response",
		.data		= &sysctl_tcp_frto_response,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_LOW_LATENCY,
		.procname	= "tcp_low_latency",
		.data		= &sysctl_tcp_low_latency,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_NO_METRICS_SAVE,
		.procname	= "tcp_no_metrics_save",
		.data		= &sysctl_tcp_nometrics_save,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= NET_TCP_MODERATE_RCVBUF,
		.procname	= "tcp_moderate_rcvbuf",
		.data		= &sysctl_tcp_moderate_rcvbuf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= NET_TCP_TSO_WIN_DIVISOR,
		.procname	= "tcp_tso_win_divisor",
		.data		= &sysctl_tcp_tso_win_divisor,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= NET_TCP_CONG_CONTROL,
		.procname	= "tcp_congestion_control",
		.mode		= 0644,
		.maxlen		= TCP_CA_NAME_MAX,
		.proc_handler	= proc_tcp_congestion_control,
		.strategy	= sysctl_tcp_congestion_control,
	},
	{
		.ctl_name	= NET_TCP_ABC,
		.procname	= "tcp_abc",
		.data		= &sysctl_tcp_abc,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= NET_TCP_MTU_PROBING,
		.procname	= "tcp_mtu_probing",
		.data		= &sysctl_tcp_mtu_probing,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= NET_TCP_BASE_MSS,
		.procname	= "tcp_base_mss",
		.data		= &sysctl_tcp_base_mss,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_TCP_WORKAROUND_SIGNED_WINDOWS,
		.procname	= "tcp_workaround_signed_windows",
		.data		= &sysctl_tcp_workaround_signed_windows,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
#ifdef CONFIG_NET_DMA
	{
		.ctl_name	= NET_TCP_DMA_COPYBREAK,
		.procname	= "tcp_dma_copybreak",
		.data		= &sysctl_tcp_dma_copybreak,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
#endif
	{
		.ctl_name	= NET_TCP_SLOW_START_AFTER_IDLE,
		.procname	= "tcp_slow_start_after_idle",
		.data		= &sysctl_tcp_slow_start_after_idle,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
#ifdef CONFIG_NETLABEL
	{
		.ctl_name	= NET_CIPSOV4_CACHE_ENABLE,
		.procname	= "cipso_cache_enable",
		.data		= &cipso_v4_cache_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= NET_CIPSOV4_CACHE_BUCKET_SIZE,
		.procname	= "cipso_cache_bucket_size",
		.data		= &cipso_v4_cache_bucketsize,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= NET_CIPSOV4_RBM_OPTFMT,
		.procname	= "cipso_rbm_optfmt",
		.data		= &cipso_v4_rbm_optfmt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= NET_CIPSOV4_RBM_STRICTVALID,
		.procname	= "cipso_rbm_strictvalid",
		.data		= &cipso_v4_rbm_strictvalid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif /* CONFIG_NETLABEL */
	{
		.procname	= "tcp_available_congestion_control",
		.maxlen		= TCP_CA_BUF_MAX,
		.mode		= 0444,
		.proc_handler   = proc_tcp_available_congestion_control,
	},
	{
		.ctl_name	= NET_TCP_ALLOWED_CONG_CONTROL,
		.procname	= "tcp_allowed_congestion_control",
		.maxlen		= TCP_CA_BUF_MAX,
		.mode		= 0644,
		.proc_handler   = proc_allowed_congestion_control,
		.strategy	= strategy_allowed_congestion_control,
	},
	{
		.ctl_name	= NET_TCP_MAX_SSTHRESH,
		.procname	= "tcp_max_ssthresh",
		.data		= &sysctl_tcp_max_ssthresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "udp_mem",
		.data		= &sysctl_udp_mem,
		.maxlen		= sizeof(sysctl_udp_mem),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &zero
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "udp_rmem_min",
		.data		= &sysctl_udp_rmem_min,
		.maxlen		= sizeof(sysctl_udp_rmem_min),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &zero
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "udp_wmem_min",
		.data		= &sysctl_udp_wmem_min,
		.maxlen		= sizeof(sysctl_udp_wmem_min),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &zero
	},
	{ .ctl_name = 0 }
};

static struct ctl_table ipv4_net_table[] = {
	{
		.ctl_name	= NET_IPV4_ICMP_ECHO_IGNORE_ALL,
		.procname	= "icmp_echo_ignore_all",
		.data		= &init_net.ipv4.sysctl_icmp_echo_ignore_all,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_ICMP_ECHO_IGNORE_BROADCASTS,
		.procname	= "icmp_echo_ignore_broadcasts",
		.data		= &init_net.ipv4.sysctl_icmp_echo_ignore_broadcasts,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_ICMP_IGNORE_BOGUS_ERROR_RESPONSES,
		.procname	= "icmp_ignore_bogus_error_responses",
		.data		= &init_net.ipv4.sysctl_icmp_ignore_bogus_error_responses,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_ICMP_ERRORS_USE_INBOUND_IFADDR,
		.procname	= "icmp_errors_use_inbound_ifaddr",
		.data		= &init_net.ipv4.sysctl_icmp_errors_use_inbound_ifaddr,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_ICMP_RATELIMIT,
		.procname	= "icmp_ratelimit",
		.data		= &init_net.ipv4.sysctl_icmp_ratelimit,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_ms_jiffies,
		.strategy	= sysctl_ms_jiffies
	},
	{
		.ctl_name	= NET_IPV4_ICMP_RATEMASK,
		.procname	= "icmp_ratemask",
		.data		= &init_net.ipv4.sysctl_icmp_ratemask,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "rt_cache_rebuild_count",
		.data		= &init_net.ipv4.sysctl_rt_cache_rebuild_count,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{ }
};

struct ctl_path net_ipv4_ctl_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "ipv4", .ctl_name = NET_IPV4, },
	{ },
};
EXPORT_SYMBOL_GPL(net_ipv4_ctl_path);

static __net_init int ipv4_sysctl_init_net(struct net *net)
{
	struct ctl_table *table;

	table = ipv4_net_table;
	if (net != &init_net) {
		table = kmemdup(table, sizeof(ipv4_net_table), GFP_KERNEL);
		if (table == NULL)
			goto err_alloc;

		table[0].data =
			&net->ipv4.sysctl_icmp_echo_ignore_all;
		table[1].data =
			&net->ipv4.sysctl_icmp_echo_ignore_broadcasts;
		table[2].data =
			&net->ipv4.sysctl_icmp_ignore_bogus_error_responses;
		table[3].data =
			&net->ipv4.sysctl_icmp_errors_use_inbound_ifaddr;
		table[4].data =
			&net->ipv4.sysctl_icmp_ratelimit;
		table[5].data =
			&net->ipv4.sysctl_icmp_ratemask;
		table[6].data =
			&net->ipv4.sysctl_rt_cache_rebuild_count;
	}

	net->ipv4.sysctl_rt_cache_rebuild_count = 4;

	net->ipv4.ipv4_hdr = register_net_sysctl_table(net,
			net_ipv4_ctl_path, table);
	if (net->ipv4.ipv4_hdr == NULL)
		goto err_reg;

	return 0;

err_reg:
	if (net != &init_net)
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static __net_exit void ipv4_sysctl_exit_net(struct net *net)
{
	struct ctl_table *table;

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

	hdr = register_sysctl_paths(net_ipv4_ctl_path, ipv4_table);
	if (hdr == NULL)
		return -ENOMEM;

	if (register_pernet_subsys(&ipv4_sysctl_ops)) {
		unregister_sysctl_table(hdr);
		return -ENOMEM;
	}

	return 0;
}

__initcall(sysctl_ipv4_init);
