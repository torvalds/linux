/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_SCTP_H__
#define __NETNS_SCTP_H__

struct sock;
struct proc_dir_entry;
struct sctp_mib;
struct ctl_table_header;

struct netns_sctp {
	DEFINE_SNMP_STAT(struct sctp_mib, sctp_statistics);

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc_net_sctp;
#endif
#ifdef CONFIG_SYSCTL
	struct ctl_table_header *sysctl_header;
#endif
	/* This is the global socket data structure used for responding to
	 * the Out-of-the-blue (OOTB) packets.  A control sock will be created
	 * for this socket at the initialization time.
	 */
	struct sock *ctl_sock;

	/* This is the global local address list.
	 * We actively maintain this complete list of addresses on
	 * the system by catching address add/delete events.
	 *
	 * It is a list of sctp_sockaddr_entry.
	 */
	struct list_head local_addr_list;
	struct list_head addr_waitq;
	struct timer_list addr_wq_timer;
	struct list_head auto_asconf_splist;
	/* Lock that protects both addr_waitq and auto_asconf_splist */
	spinlock_t addr_wq_lock;

	/* Lock that protects the local_addr_list writers */
	spinlock_t local_addr_lock;

	/* RFC2960 Section 14. Suggested SCTP Protocol Parameter Values
	 *
	 * The following protocol parameters are RECOMMENDED:
	 *
	 * RTO.Initial		    - 3	 seconds
	 * RTO.Min		    - 1	 second
	 * RTO.Max		   -  60 seconds
	 * RTO.Alpha		    - 1/8  (3 when converted to right shifts.)
	 * RTO.Beta		    - 1/4  (2 when converted to right shifts.)
	 */
	unsigned int rto_initial;
	unsigned int rto_min;
	unsigned int rto_max;

	/* Note: rto_alpha and rto_beta are really defined as inverse
	 * powers of two to facilitate integer operations.
	 */
	int rto_alpha;
	int rto_beta;

	/* Max.Burst		    - 4 */
	int max_burst;

	/* Whether Cookie Preservative is enabled(1) or not(0) */
	int cookie_preserve_enable;

	/* The namespace default hmac alg */
	char *sctp_hmac_alg;

	/* Valid.Cookie.Life	    - 60  seconds  */
	unsigned int valid_cookie_life;

	/* Delayed SACK timeout  200ms default*/
	unsigned int sack_timeout;

	/* HB.interval		    - 30 seconds  */
	unsigned int hb_interval;

	/* Association.Max.Retrans  - 10 attempts
	 * Path.Max.Retrans	    - 5	 attempts (per destination address)
	 * Max.Init.Retransmits	    - 8	 attempts
	 */
	int max_retrans_association;
	int max_retrans_path;
	int max_retrans_init;
	/* Potentially-Failed.Max.Retrans sysctl value
	 * taken from:
	 * http://tools.ietf.org/html/draft-nishida-tsvwg-sctp-failover-05
	 */
	int pf_retrans;

	/*
	 * Disable Potentially-Failed feature, the feature is enabled by default
	 * pf_enable	-  0  : disable pf
	 *		- >0  : enable pf
	 */
	int pf_enable;

	/*
	 * Policy for preforming sctp/socket accounting
	 * 0   - do socket level accounting, all assocs share sk_sndbuf
	 * 1   - do sctp accounting, each asoc may use sk_sndbuf bytes
	 */
	int sndbuf_policy;

	/*
	 * Policy for preforming sctp/socket accounting
	 * 0   - do socket level accounting, all assocs share sk_rcvbuf
	 * 1   - do sctp accounting, each asoc may use sk_rcvbuf bytes
	 */
	int rcvbuf_policy;

	int default_auto_asconf;

	/* Flag to indicate if addip is enabled. */
	int addip_enable;
	int addip_noauth;

	/* Flag to indicate if PR-SCTP is enabled. */
	int prsctp_enable;

	/* Flag to indicate if PR-CONFIG is enabled. */
	int reconf_enable;

	/* Flag to indicate if SCTP-AUTH is enabled */
	int auth_enable;

	/* Flag to indicate if stream interleave is enabled */
	int intl_enable;

	/*
	 * Policy to control SCTP IPv4 address scoping
	 * 0   - Disable IPv4 address scoping
	 * 1   - Enable IPv4 address scoping
	 * 2   - Selectively allow only IPv4 private addresses
	 * 3   - Selectively allow only IPv4 link local address
	 */
	int scope_policy;

	/* Threshold for rwnd update SACKS.  Receive buffer shifted this many
	 * bits is an indicator of when to send and window update SACK.
	 */
	int rwnd_upd_shift;

	/* Threshold for autoclose timeout, in seconds. */
	unsigned long max_autoclose;
};

#endif /* __NETNS_SCTP_H__ */
