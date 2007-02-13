/*
 * sysctl_net_ipv4.c: sysctl interface to net IPV4 subsystem.
 *
 * $Id: sysctl_net_ipv4.c,v 1.50 2001/10/20 00:00:11 davem Exp $
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ipv4 directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/igmp.h>
#include <linux/inetdevice.h>
#include <net/snmp.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/cipso_ipv4.h>

/* From af_inet.c */
extern int sysctl_ip_nonlocal_bind;

#ifdef CONFIG_SYSCTL
static int zero;
static int tcp_retr1_max = 255;
static int ip_local_port_range_min[] = { 1, 1 };
static int ip_local_port_range_max[] = { 65535, 65535 };
#endif

struct ipv4_config ipv4_config;

#ifdef CONFIG_SYSCTL

static
int ipv4_sysctl_forward(ctl_table *ctl, int write, struct file * filp,
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int val = ipv4_devconf.forwarding;
	int ret;

	ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);

	if (write && ipv4_devconf.forwarding != val)
		inet_forward_change();

	return ret;
}

static int ipv4_sysctl_forward_strategy(ctl_table *table,
			 int __user *name, int nlen,
			 void __user *oldval, size_t __user *oldlenp,
			 void __user *newval, size_t newlen)
{
	int *valp = table->data;
	int new;

	if (!newval || !newlen)
		return 0;

	if (newlen != sizeof(int))
		return -EINVAL;

	if (get_user(new, (int __user *)newval))
		return -EFAULT;

	if (new == *valp)
		return 0;

	if (oldval && oldlenp) {
		size_t len;

		if (get_user(len, oldlenp))
			return -EFAULT;

		if (len) {
			if (len > table->maxlen)
				len = table->maxlen;
			if (copy_to_user(oldval, valp, len))
				return -EFAULT;
			if (put_user(len, oldlenp))
				return -EFAULT;
		}
	}

	*valp = new;
	inet_forward_change();
	return 1;
}

static int proc_tcp_congestion_control(ctl_table *ctl, int write, struct file * filp,
				       void __user *buffer, size_t *lenp, loff_t *ppos)
{
	char val[TCP_CA_NAME_MAX];
	ctl_table tbl = {
		.data = val,
		.maxlen = TCP_CA_NAME_MAX,
	};
	int ret;

	tcp_get_default_congestion_control(val);

	ret = proc_dostring(&tbl, write, filp, buffer, lenp, ppos);
	if (write && ret == 0)
		ret = tcp_set_default_congestion_control(val);
	return ret;
}

static int sysctl_tcp_congestion_control(ctl_table *table, int __user *name,
					 int nlen, void __user *oldval,
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
	ret = sysctl_string(&tbl, name, nlen, oldval, oldlenp, newval, newlen);
	if (ret == 0 && newval && newlen)
		ret = tcp_set_default_congestion_control(val);
	return ret;
}

static int proc_tcp_available_congestion_control(ctl_table *ctl,
						 int write, struct file * filp,
						 void __user *buffer, size_t *lenp,
						 loff_t *ppos)
{
	ctl_table tbl = { .maxlen = TCP_CA_BUF_MAX, };
	int ret;

	tbl.data = kmalloc(tbl.maxlen, GFP_USER);
	if (!tbl.data)
		return -ENOMEM;
	tcp_get_available_congestion_control(tbl.data, TCP_CA_BUF_MAX);
	ret = proc_dostring(&tbl, write, filp, buffer, lenp, ppos);
	kfree(tbl.data);
	return ret;
}

static int proc_allowed_congestion_control(ctl_table *ctl,
					   int write, struct file * filp,
					   void __user *buffer, size_t *lenp,
					   loff_t *ppos)
{
	ctl_table tbl = { .maxlen = TCP_CA_BUF_MAX };
	int ret;

	tbl.data = kmalloc(tbl.maxlen, GFP_USER);
	if (!tbl.data)
		return -ENOMEM;

	tcp_get_allowed_congestion_control(tbl.data, tbl.maxlen);
	ret = proc_dostring(&tbl, write, filp, buffer, lenp, ppos);
	if (write && ret == 0)
		ret = tcp_set_allowed_congestion_control(tbl.data);
	kfree(tbl.data);
	return ret;
}

static int strategy_allowed_congestion_control(ctl_table *table, int __user *name,
					       int nlen, void __user *oldval,
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
	ret = sysctl_string(&tbl, name, nlen, oldval, oldlenp, newval, newlen);
	if (ret == 0 && newval && newlen)
		ret = tcp_set_allowed_congestion_control(tbl.data);
	kfree(tbl.data);

	return ret;

}

ctl_table ipv4_table[] = {
	{
		.ctl_name	= NET_IPV4_TCP_TIMESTAMPS,
		.procname	= "tcp_timestamps",
		.data		= &sysctl_tcp_timestamps,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_WINDOW_SCALING,
		.procname	= "tcp_window_scaling",
		.data		= &sysctl_tcp_window_scaling,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_SACK,
		.procname	= "tcp_sack",
		.data		= &sysctl_tcp_sack,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_RETRANS_COLLAPSE,
		.procname	= "tcp_retrans_collapse",
		.data		= &sysctl_tcp_retrans_collapse,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_FORWARD,
		.procname	= "ip_forward",
		.data		= &ipv4_devconf.forwarding,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &ipv4_sysctl_forward,
		.strategy	= &ipv4_sysctl_forward_strategy
	},
	{
		.ctl_name	= NET_IPV4_DEFAULT_TTL,
		.procname	= "ip_default_ttl",
		.data		= &sysctl_ip_default_ttl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &ipv4_doint_and_flush,
		.strategy	= &ipv4_doint_and_flush_strategy,
	},
	{
		.ctl_name	= NET_IPV4_NO_PMTU_DISC,
		.procname	= "ip_no_pmtu_disc",
		.data		= &ipv4_config.no_pmtu_disc,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_NONLOCAL_BIND,
		.procname	= "ip_nonlocal_bind",
		.data		= &sysctl_ip_nonlocal_bind,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_SYN_RETRIES,
		.procname	= "tcp_syn_retries",
		.data		= &sysctl_tcp_syn_retries,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_SYNACK_RETRIES,
		.procname	= "tcp_synack_retries",
		.data		= &sysctl_tcp_synack_retries,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_MAX_ORPHANS,
		.procname	= "tcp_max_orphans",
		.data		= &sysctl_tcp_max_orphans,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_MAX_TW_BUCKETS,
		.procname	= "tcp_max_tw_buckets",
		.data		= &tcp_death_row.sysctl_max_tw_buckets,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_IPFRAG_HIGH_THRESH,
		.procname	= "ipfrag_high_thresh",
		.data		= &sysctl_ipfrag_high_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_IPFRAG_LOW_THRESH,
		.procname	= "ipfrag_low_thresh",
		.data		= &sysctl_ipfrag_low_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_DYNADDR,
		.procname	= "ip_dynaddr",
		.data		= &sysctl_ip_dynaddr,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_IPFRAG_TIME,
		.procname	= "ipfrag_time",
		.data		= &sysctl_ipfrag_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_TCP_KEEPALIVE_TIME,
		.procname	= "tcp_keepalive_time",
		.data		= &sysctl_tcp_keepalive_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_TCP_KEEPALIVE_PROBES,
		.procname	= "tcp_keepalive_probes",
		.data		= &sysctl_tcp_keepalive_probes,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_KEEPALIVE_INTVL,
		.procname	= "tcp_keepalive_intvl",
		.data		= &sysctl_tcp_keepalive_intvl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_TCP_RETRIES1,
		.procname	= "tcp_retries1",
		.data		= &sysctl_tcp_retries1,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra2		= &tcp_retr1_max
	},
	{
		.ctl_name	= NET_IPV4_TCP_RETRIES2,
		.procname	= "tcp_retries2",
		.data		= &sysctl_tcp_retries2,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_TCP_FIN_TIMEOUT,
		.procname	= "tcp_fin_timeout",
		.data		= &sysctl_tcp_fin_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
	},
#ifdef CONFIG_SYN_COOKIES
	{
		.ctl_name	= NET_TCP_SYNCOOKIES,
		.procname	= "tcp_syncookies",
		.data		= &sysctl_tcp_syncookies,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#endif
	{
		.ctl_name	= NET_TCP_TW_RECYCLE,
		.procname	= "tcp_tw_recycle",
		.data		= &tcp_death_row.sysctl_tw_recycle,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_ABORT_ON_OVERFLOW,
		.procname	= "tcp_abort_on_overflow",
		.data		= &sysctl_tcp_abort_on_overflow,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_STDURG,
		.procname	= "tcp_stdurg",
		.data		= &sysctl_tcp_stdurg,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_RFC1337,
		.procname	= "tcp_rfc1337",
		.data		= &sysctl_tcp_rfc1337,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_MAX_SYN_BACKLOG,
		.procname	= "tcp_max_syn_backlog",
		.data		= &sysctl_max_syn_backlog,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_LOCAL_PORT_RANGE,
		.procname	= "ip_local_port_range",
		.data		= &sysctl_local_port_range,
		.maxlen		= sizeof(sysctl_local_port_range),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= ip_local_port_range_min,
		.extra2		= ip_local_port_range_max
	},
	{
		.ctl_name	= NET_IPV4_ICMP_ECHO_IGNORE_ALL,
		.procname	= "icmp_echo_ignore_all",
		.data		= &sysctl_icmp_echo_ignore_all,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_ICMP_ECHO_IGNORE_BROADCASTS,
		.procname	= "icmp_echo_ignore_broadcasts",
		.data		= &sysctl_icmp_echo_ignore_broadcasts,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_ICMP_IGNORE_BOGUS_ERROR_RESPONSES,
		.procname	= "icmp_ignore_bogus_error_responses",
		.data		= &sysctl_icmp_ignore_bogus_error_responses,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_ICMP_ERRORS_USE_INBOUND_IFADDR,
		.procname	= "icmp_errors_use_inbound_ifaddr",
		.data		= &sysctl_icmp_errors_use_inbound_ifaddr,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_ROUTE,
		.procname	= "route",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= ipv4_route_table
	},
#ifdef CONFIG_IP_MULTICAST
	{
		.ctl_name	= NET_IPV4_IGMP_MAX_MEMBERSHIPS,
		.procname	= "igmp_max_memberships",
		.data		= &sysctl_igmp_max_memberships,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},

#endif
	{
		.ctl_name	= NET_IPV4_IGMP_MAX_MSF,
		.procname	= "igmp_max_msf",
		.data		= &sysctl_igmp_max_msf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_INET_PEER_THRESHOLD,
		.procname	= "inet_peer_threshold",
		.data		= &inet_peer_threshold,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_INET_PEER_MINTTL,
		.procname	= "inet_peer_minttl",
		.data		= &inet_peer_minttl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_INET_PEER_MAXTTL,
		.procname	= "inet_peer_maxttl",
		.data		= &inet_peer_maxttl,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_INET_PEER_GC_MINTIME,
		.procname	= "inet_peer_gc_mintime",
		.data		= &inet_peer_gc_mintime,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_INET_PEER_GC_MAXTIME,
		.procname	= "inet_peer_gc_maxtime",
		.data		= &inet_peer_gc_maxtime,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
	},
	{
		.ctl_name	= NET_TCP_ORPHAN_RETRIES,
		.procname	= "tcp_orphan_retries",
		.data		= &sysctl_tcp_orphan_retries,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_FACK,
		.procname	= "tcp_fack",
		.data		= &sysctl_tcp_fack,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_REORDERING,
		.procname	= "tcp_reordering",
		.data		= &sysctl_tcp_reordering,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_ECN,
		.procname	= "tcp_ecn",
		.data		= &sysctl_tcp_ecn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_DSACK,
		.procname	= "tcp_dsack",
		.data		= &sysctl_tcp_dsack,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_MEM,
		.procname	= "tcp_mem",
		.data		= &sysctl_tcp_mem,
		.maxlen		= sizeof(sysctl_tcp_mem),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_WMEM,
		.procname	= "tcp_wmem",
		.data		= &sysctl_tcp_wmem,
		.maxlen		= sizeof(sysctl_tcp_wmem),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_RMEM,
		.procname	= "tcp_rmem",
		.data		= &sysctl_tcp_rmem,
		.maxlen		= sizeof(sysctl_tcp_rmem),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_APP_WIN,
		.procname	= "tcp_app_win",
		.data		= &sysctl_tcp_app_win,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_ADV_WIN_SCALE,
		.procname	= "tcp_adv_win_scale",
		.data		= &sysctl_tcp_adv_win_scale,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_ICMP_RATELIMIT,
		.procname	= "icmp_ratelimit",
		.data		= &sysctl_icmp_ratelimit,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_ICMP_RATEMASK,
		.procname	= "icmp_ratemask",
		.data		= &sysctl_icmp_ratemask,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_TW_REUSE,
		.procname	= "tcp_tw_reuse",
		.data		= &sysctl_tcp_tw_reuse,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_FRTO,
		.procname	= "tcp_frto",
		.data		= &sysctl_tcp_frto,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_TCP_LOW_LATENCY,
		.procname	= "tcp_low_latency",
		.data		= &sysctl_tcp_low_latency,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IPV4_IPFRAG_SECRET_INTERVAL,
		.procname	= "ipfrag_secret_interval",
		.data		= &sysctl_ipfrag_secret_interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies
	},
	{
		.ctl_name	= NET_IPV4_IPFRAG_MAX_DIST,
		.procname	= "ipfrag_max_dist",
		.data		= &sysctl_ipfrag_max_dist,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.extra1		= &zero
	},
	{
		.ctl_name	= NET_TCP_NO_METRICS_SAVE,
		.procname	= "tcp_no_metrics_save",
		.data		= &sysctl_tcp_nometrics_save,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_TCP_MODERATE_RCVBUF,
		.procname	= "tcp_moderate_rcvbuf",
		.data		= &sysctl_tcp_moderate_rcvbuf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_TCP_TSO_WIN_DIVISOR,
		.procname	= "tcp_tso_win_divisor",
		.data		= &sysctl_tcp_tso_win_divisor,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_TCP_CONG_CONTROL,
		.procname	= "tcp_congestion_control",
		.mode		= 0644,
		.maxlen		= TCP_CA_NAME_MAX,
		.proc_handler	= &proc_tcp_congestion_control,
		.strategy	= &sysctl_tcp_congestion_control,
	},
	{
		.ctl_name	= NET_TCP_ABC,
		.procname	= "tcp_abc",
		.data		= &sysctl_tcp_abc,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_TCP_MTU_PROBING,
		.procname	= "tcp_mtu_probing",
		.data		= &sysctl_tcp_mtu_probing,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_TCP_BASE_MSS,
		.procname	= "tcp_base_mss",
		.data		= &sysctl_tcp_base_mss,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_TCP_WORKAROUND_SIGNED_WINDOWS,
		.procname	= "tcp_workaround_signed_windows",
		.data		= &sysctl_tcp_workaround_signed_windows,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#ifdef CONFIG_NET_DMA
	{
		.ctl_name	= NET_TCP_DMA_COPYBREAK,
		.procname	= "tcp_dma_copybreak",
		.data		= &sysctl_tcp_dma_copybreak,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#endif
	{
		.ctl_name	= NET_TCP_SLOW_START_AFTER_IDLE,
		.procname	= "tcp_slow_start_after_idle",
		.data		= &sysctl_tcp_slow_start_after_idle,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#ifdef CONFIG_NETLABEL
	{
		.ctl_name	= NET_CIPSOV4_CACHE_ENABLE,
		.procname	= "cipso_cache_enable",
		.data		= &cipso_v4_cache_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_CIPSOV4_CACHE_BUCKET_SIZE,
		.procname	= "cipso_cache_bucket_size",
		.data		= &cipso_v4_cache_bucketsize,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_CIPSOV4_RBM_OPTFMT,
		.procname	= "cipso_rbm_optfmt",
		.data		= &cipso_v4_rbm_optfmt,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_CIPSOV4_RBM_STRICTVALID,
		.procname	= "cipso_rbm_strictvalid",
		.data		= &cipso_v4_rbm_strictvalid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif /* CONFIG_NETLABEL */
	{
		.ctl_name	= NET_TCP_AVAIL_CONG_CONTROL,
		.procname	= "tcp_available_congestion_control",
		.maxlen		= TCP_CA_BUF_MAX,
		.mode		= 0444,
		.proc_handler   = &proc_tcp_available_congestion_control,
	},
	{
		.ctl_name	= NET_TCP_ALLOWED_CONG_CONTROL,
		.procname	= "tcp_allowed_congestion_control",
		.maxlen		= TCP_CA_BUF_MAX,
		.mode		= 0644,
		.proc_handler   = &proc_allowed_congestion_control,
		.strategy	= &strategy_allowed_congestion_control,
	},
	{ .ctl_name = 0 }
};

#endif /* CONFIG_SYSCTL */

EXPORT_SYMBOL(ipv4_config);
