// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Authors:
 * Copyright 2001, 2002 by Robert Olsson <robert.olsson@its.uu.se>
 *                             Uppsala University and
 *                             Swedish University of Agricultural Sciences
 *
 * Alexey Kuznetsov  <kuznet@ms2.inr.ac.ru>
 * Ben Greear <greearb@candelatech.com>
 * Jens Låås <jens.laas@data.slu.se>
 *
 * A tool for loading the network with preconfigurated packets.
 * The tool is implemented as a linux module.  Parameters are output
 * device, delay (to hard_xmit), number of packets, and whether
 * to use multiple SKBs or just the same one.
 * pktgen uses the installed interface's output routine.
 *
 * Additional hacking by:
 *
 * Jens.Laas@data.slu.se
 * Improved by ANK. 010120.
 * Improved by ANK even more. 010212.
 * MAC address typo fixed. 010417 --ro
 * Integrated.  020301 --DaveM
 * Added multiskb option 020301 --DaveM
 * Scaling of results. 020417--sigurdur@linpro.no
 * Significant re-work of the module:
 *   *  Convert to threaded model to more efficiently be able to transmit
 *       and receive on multiple interfaces at once.
 *   *  Converted many counters to __u64 to allow longer runs.
 *   *  Allow configuration of ranges, like min/max IP address, MACs,
 *       and UDP-ports, for both source and destination, and can
 *       set to use a random distribution or sequentially walk the range.
 *   *  Can now change most values after starting.
 *   *  Place 12-byte packet in UDP payload with magic number,
 *       sequence number, and timestamp.
 *   *  Add receiver code that detects dropped pkts, re-ordered pkts, and
 *       latencies (with micro-second) precision.
 *   *  Add IOCTL interface to easily get counters & configuration.
 *   --Ben Greear <greearb@candelatech.com>
 *
 * Renamed multiskb to clone_skb and cleaned up sending core for two distinct
 * skb modes. A clone_skb=0 mode for Ben "ranges" work and a clone_skb != 0
 * as a "fastpath" with a configurable number of clones after alloc's.
 * clone_skb=0 means all packets are allocated this also means ranges time
 * stamps etc can be used. clone_skb=100 means 1 malloc is followed by 100
 * clones.
 *
 * Also moved to /proc/net/pktgen/
 * --ro
 *
 * Sept 10:  Fixed threading/locking.  Lots of bone-headed and more clever
 *    mistakes.  Also merged in DaveM's patch in the -pre6 patch.
 * --Ben Greear <greearb@candelatech.com>
 *
 * Integrated to 2.5.x 021029 --Lucio Maciel (luciomaciel@zipmail.com.br)
 *
 * 021124 Finished major redesign and rewrite for new functionality.
 * See Documentation/networking/pktgen.txt for how to use this.
 *
 * The new operation:
 * For each CPU one thread/process is created at start. This process checks
 * for running devices in the if_list and sends packets until count is 0 it
 * also the thread checks the thread->control which is used for inter-process
 * communication. controlling process "posts" operations to the threads this
 * way.
 * The if_list is RCU protected, and the if_lock remains to protect updating
 * of if_list, from "add_device" as it invoked from userspace (via proc write).
 *
 * By design there should only be *one* "controlling" process. In practice
 * multiple write accesses gives unpredictable result. Understood by "write"
 * to /proc gives result code thats should be read be the "writer".
 * For practical use this should be no problem.
 *
 * Note when adding devices to a specific CPU there good idea to also assign
 * /proc/irq/XX/smp_affinity so TX-interrupts gets bound to the same CPU.
 * --ro
 *
 * Fix refcount off by one if first packet fails, potential null deref,
 * memleak 030710- KJP
 *
 * First "ranges" functionality for ipv6 030726 --ro
 *
 * Included flow support. 030802 ANK.
 *
 * Fixed unaligned access on IA-64 Grant Grundler <grundler@parisc-linux.org>
 *
 * Remove if fix from added Harald Welte <laforge@netfilter.org> 040419
 * ia64 compilation fix from  Aron Griffis <aron@hp.com> 040604
 *
 * New xmit() return, do_div and misc clean up by Stephen Hemminger
 * <shemminger@osdl.org> 040923
 *
 * Randy Dunlap fixed u64 printk compiler warning
 *
 * Remove FCS from BW calculation.  Lennert Buytenhek <buytenh@wantstofly.org>
 * New time handling. Lennert Buytenhek <buytenh@wantstofly.org> 041213
 *
 * Corrections from Nikolai Malykh (nmalykh@bilim.com)
 * Removed unused flags F_SET_SRCMAC & F_SET_SRCIP 041230
 *
 * interruptible_sleep_on_timeout() replaced Nishanth Aravamudan <nacc@us.ibm.com>
 * 050103
 *
 * MPLS support by Steven Whitehouse <steve@chygwyn.com>
 *
 * 802.1Q/Q-in-Q support by Francesco Fondelli (FF) <francesco.fondelli@gmail.com>
 *
 * Fixed src_mac command to set source mac of packet to value specified in
 * command by Adit Ranadive <adit.262@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sys.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/capability.h>
#include <linux/hrtimer.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/inet.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/wait.h>
#include <linux/etherdevice.h>
#include <linux/kthread.h>
#include <linux/prefetch.h>
#include <linux/mmzone.h>
#include <net/net_namespace.h>
#include <net/checksum.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <net/ip6_checksum.h>
#include <net/addrconf.h>
#ifdef CONFIG_XFRM
#include <net/xfrm.h>
#endif
#include <net/netns/generic.h>
#include <asm/byteorder.h>
#include <linux/rcupdate.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/timex.h>
#include <linux/uaccess.h>
#include <asm/dma.h>
#include <asm/div64.h>		/* do_div */

#define VERSION	"2.75"
#define IP_NAME_SZ 32
#define MAX_MPLS_LABELS 16 /* This is the max label stack depth */
#define MPLS_STACK_BOTTOM htonl(0x00000100)

#define func_enter() pr_debug("entering %s\n", __func__);

#define PKT_FLAGS							\
	pf(IPV6)		/* Interface in IPV6 Mode */		\
	pf(IPSRC_RND)		/* IP-Src Random  */			\
	pf(IPDST_RND)		/* IP-Dst Random  */			\
	pf(TXSIZE_RND)		/* Transmit size is random */		\
	pf(UDPSRC_RND)		/* UDP-Src Random */			\
	pf(UDPDST_RND)		/* UDP-Dst Random */			\
	pf(UDPCSUM)		/* Include UDP checksum */		\
	pf(NO_TIMESTAMP)	/* Don't timestamp packets (default TS) */ \
	pf(MPLS_RND)		/* Random MPLS labels */		\
	pf(QUEUE_MAP_RND)	/* queue map Random */			\
	pf(QUEUE_MAP_CPU)	/* queue map mirrors smp_processor_id() */ \
	pf(FLOW_SEQ)		/* Sequential flows */			\
	pf(IPSEC)		/* ipsec on for flows */		\
	pf(MACSRC_RND)		/* MAC-Src Random */			\
	pf(MACDST_RND)		/* MAC-Dst Random */			\
	pf(VID_RND)		/* Random VLAN ID */			\
	pf(SVID_RND)		/* Random SVLAN ID */			\
	pf(NODE)		/* Node memory alloc*/			\

#define pf(flag)		flag##_SHIFT,
enum pkt_flags {
	PKT_FLAGS
};
#undef pf

/* Device flag bits */
#define pf(flag)		static const __u32 F_##flag = (1<<flag##_SHIFT);
PKT_FLAGS
#undef pf

#define pf(flag)		__stringify(flag),
static char *pkt_flag_names[] = {
	PKT_FLAGS
};
#undef pf

#define NR_PKT_FLAGS		ARRAY_SIZE(pkt_flag_names)

/* Thread control flag bits */
#define T_STOP        (1<<0)	/* Stop run */
#define T_RUN         (1<<1)	/* Start run */
#define T_REMDEVALL   (1<<2)	/* Remove all devs */
#define T_REMDEV      (1<<3)	/* Remove one dev */

/* Xmit modes */
#define M_START_XMIT		0	/* Default normal TX */
#define M_NETIF_RECEIVE 	1	/* Inject packets into stack */
#define M_QUEUE_XMIT		2	/* Inject packet into qdisc */

/* If lock -- protects updating of if_list */
#define   if_lock(t)           mutex_lock(&(t->if_lock));
#define   if_unlock(t)           mutex_unlock(&(t->if_lock));

/* Used to help with determining the pkts on receive */
#define PKTGEN_MAGIC 0xbe9be955
#define PG_PROC_DIR "pktgen"
#define PGCTRL	    "pgctrl"

#define MAX_CFLOWS  65536

#define VLAN_TAG_SIZE(x) ((x)->vlan_id == 0xffff ? 0 : 4)
#define SVLAN_TAG_SIZE(x) ((x)->svlan_id == 0xffff ? 0 : 4)

struct flow_state {
	__be32 cur_daddr;
	int count;
#ifdef CONFIG_XFRM
	struct xfrm_state *x;
#endif
	__u32 flags;
};

/* flow flag bits */
#define F_INIT   (1<<0)		/* flow has been initialized */

struct pktgen_dev {
	/*
	 * Try to keep frequent/infrequent used vars. separated.
	 */
	struct proc_dir_entry *entry;	/* proc file */
	struct pktgen_thread *pg_thread;/* the owner */
	struct list_head list;		/* chaining in the thread's run-queue */
	struct rcu_head	 rcu;		/* freed by RCU */

	int running;		/* if false, the test will stop */

	/* If min != max, then we will either do a linear iteration, or
	 * we will do a random selection from within the range.
	 */
	__u32 flags;
	int xmit_mode;
	int min_pkt_size;
	int max_pkt_size;
	int pkt_overhead;	/* overhead for MPLS, VLANs, IPSEC etc */
	int nfrags;
	int removal_mark;	/* non-zero => the device is marked for
				 * removal by worker thread */

	struct page *page;
	u64 delay;		/* nano-seconds */

	__u64 count;		/* Default No packets to send */
	__u64 sofar;		/* How many pkts we've sent so far */
	__u64 tx_bytes;		/* How many bytes we've transmitted */
	__u64 errors;		/* Errors when trying to transmit, */

	/* runtime counters relating to clone_skb */

	__u32 clone_count;
	int last_ok;		/* Was last skb sent?
				 * Or a failed transmit of some sort?
				 * This will keep sequence numbers in order
				 */
	ktime_t next_tx;
	ktime_t started_at;
	ktime_t stopped_at;
	u64	idle_acc;	/* nano-seconds */

	__u32 seq_num;

	int clone_skb;		/*
				 * Use multiple SKBs during packet gen.
				 * If this number is greater than 1, then
				 * that many copies of the same packet will be
				 * sent before a new packet is allocated.
				 * If you want to send 1024 identical packets
				 * before creating a new packet,
				 * set clone_skb to 1024.
				 */

	char dst_min[IP_NAME_SZ];	/* IP, ie 1.2.3.4 */
	char dst_max[IP_NAME_SZ];	/* IP, ie 1.2.3.4 */
	char src_min[IP_NAME_SZ];	/* IP, ie 1.2.3.4 */
	char src_max[IP_NAME_SZ];	/* IP, ie 1.2.3.4 */

	struct in6_addr in6_saddr;
	struct in6_addr in6_daddr;
	struct in6_addr cur_in6_daddr;
	struct in6_addr cur_in6_saddr;
	/* For ranges */
	struct in6_addr min_in6_daddr;
	struct in6_addr max_in6_daddr;
	struct in6_addr min_in6_saddr;
	struct in6_addr max_in6_saddr;

	/* If we're doing ranges, random or incremental, then this
	 * defines the min/max for those ranges.
	 */
	__be32 saddr_min;	/* inclusive, source IP address */
	__be32 saddr_max;	/* exclusive, source IP address */
	__be32 daddr_min;	/* inclusive, dest IP address */
	__be32 daddr_max;	/* exclusive, dest IP address */

	__u16 udp_src_min;	/* inclusive, source UDP port */
	__u16 udp_src_max;	/* exclusive, source UDP port */
	__u16 udp_dst_min;	/* inclusive, dest UDP port */
	__u16 udp_dst_max;	/* exclusive, dest UDP port */

	/* DSCP + ECN */
	__u8 tos;            /* six MSB of (former) IPv4 TOS
				are for dscp codepoint */
	__u8 traffic_class;  /* ditto for the (former) Traffic Class in IPv6
				(see RFC 3260, sec. 4) */

	/* MPLS */
	unsigned int nr_labels;	/* Depth of stack, 0 = no MPLS */
	__be32 labels[MAX_MPLS_LABELS];

	/* VLAN/SVLAN (802.1Q/Q-in-Q) */
	__u8  vlan_p;
	__u8  vlan_cfi;
	__u16 vlan_id;  /* 0xffff means no vlan tag */

	__u8  svlan_p;
	__u8  svlan_cfi;
	__u16 svlan_id; /* 0xffff means no svlan tag */

	__u32 src_mac_count;	/* How many MACs to iterate through */
	__u32 dst_mac_count;	/* How many MACs to iterate through */

	unsigned char dst_mac[ETH_ALEN];
	unsigned char src_mac[ETH_ALEN];

	__u32 cur_dst_mac_offset;
	__u32 cur_src_mac_offset;
	__be32 cur_saddr;
	__be32 cur_daddr;
	__u16 ip_id;
	__u16 cur_udp_dst;
	__u16 cur_udp_src;
	__u16 cur_queue_map;
	__u32 cur_pkt_size;
	__u32 last_pkt_size;

	__u8 hh[14];
	/* = {
	   0x00, 0x80, 0xC8, 0x79, 0xB3, 0xCB,

	   We fill in SRC address later
	   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	   0x08, 0x00
	   };
	 */
	__u16 pad;		/* pad out the hh struct to an even 16 bytes */

	struct sk_buff *skb;	/* skb we are to transmit next, used for when we
				 * are transmitting the same one multiple times
				 */
	struct net_device *odev; /* The out-going device.
				  * Note that the device should have it's
				  * pg_info pointer pointing back to this
				  * device.
				  * Set when the user specifies the out-going
				  * device name (not when the inject is
				  * started as it used to do.)
				  */
	char odevname[32];
	struct flow_state *flows;
	unsigned int cflows;	/* Concurrent flows (config) */
	unsigned int lflow;		/* Flow length  (config) */
	unsigned int nflows;	/* accumulated flows (stats) */
	unsigned int curfl;		/* current sequenced flow (state)*/

	u16 queue_map_min;
	u16 queue_map_max;
	__u32 skb_priority;	/* skb priority field */
	unsigned int burst;	/* number of duplicated packets to burst */
	int node;               /* Memory node */

#ifdef CONFIG_XFRM
	__u8	ipsmode;		/* IPSEC mode (config) */
	__u8	ipsproto;		/* IPSEC type (config) */
	__u32	spi;
	struct xfrm_dst xdst;
	struct dst_ops dstops;
#endif
	char result[512];
};

struct pktgen_hdr {
	__be32 pgh_magic;
	__be32 seq_num;
	__be32 tv_sec;
	__be32 tv_usec;
};


static unsigned int pg_net_id __read_mostly;

struct pktgen_net {
	struct net		*net;
	struct proc_dir_entry	*proc_dir;
	struct list_head	pktgen_threads;
	bool			pktgen_exiting;
};

struct pktgen_thread {
	struct mutex if_lock;		/* for list of devices */
	struct list_head if_list;	/* All device here */
	struct list_head th_list;
	struct task_struct *tsk;
	char result[512];

	/* Field for thread to receive "posted" events terminate,
	   stop ifs etc. */

	u32 control;
	int cpu;

	wait_queue_head_t queue;
	struct completion start_done;
	struct pktgen_net *net;
};

#define REMOVE 1
#define FIND   0

static const char version[] =
	"Packet Generator for packet performance testing. "
	"Version: " VERSION "\n";

static int pktgen_remove_device(struct pktgen_thread *t, struct pktgen_dev *i);
static int pktgen_add_device(struct pktgen_thread *t, const char *ifname);
static struct pktgen_dev *pktgen_find_dev(struct pktgen_thread *t,
					  const char *ifname, bool exact);
static int pktgen_device_event(struct notifier_block *, unsigned long, void *);
static void pktgen_run_all_threads(struct pktgen_net *pn);
static void pktgen_reset_all_threads(struct pktgen_net *pn);
static void pktgen_stop_all_threads_ifs(struct pktgen_net *pn);

static void pktgen_stop(struct pktgen_thread *t);
static void pktgen_clear_counters(struct pktgen_dev *pkt_dev);

/* Module parameters, defaults. */
static int pg_count_d __read_mostly = 1000;
static int pg_delay_d __read_mostly;
static int pg_clone_skb_d  __read_mostly;
static int debug  __read_mostly;

static DEFINE_MUTEX(pktgen_thread_lock);

static struct notifier_block pktgen_notifier_block = {
	.notifier_call = pktgen_device_event,
};

/*
 * /proc handling functions
 *
 */

static int pgctrl_show(struct seq_file *seq, void *v)
{
	seq_puts(seq, version);
	return 0;
}

static ssize_t pgctrl_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	char data[128];
	struct pktgen_net *pn = net_generic(current->nsproxy->net_ns, pg_net_id);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (count == 0)
		return -EINVAL;

	if (count > sizeof(data))
		count = sizeof(data);

	if (copy_from_user(data, buf, count))
		return -EFAULT;

	data[count - 1] = 0;	/* Strip trailing '\n' and terminate string */

	if (!strcmp(data, "stop"))
		pktgen_stop_all_threads_ifs(pn);

	else if (!strcmp(data, "start"))
		pktgen_run_all_threads(pn);

	else if (!strcmp(data, "reset"))
		pktgen_reset_all_threads(pn);

	else
		return -EINVAL;

	return count;
}

static int pgctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, pgctrl_show, PDE_DATA(inode));
}

static const struct file_operations pktgen_fops = {
	.open    = pgctrl_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.write   = pgctrl_write,
	.release = single_release,
};

static int pktgen_if_show(struct seq_file *seq, void *v)
{
	const struct pktgen_dev *pkt_dev = seq->private;
	ktime_t stopped;
	unsigned int i;
	u64 idle;

	seq_printf(seq,
		   "Params: count %llu  min_pkt_size: %u  max_pkt_size: %u\n",
		   (unsigned long long)pkt_dev->count, pkt_dev->min_pkt_size,
		   pkt_dev->max_pkt_size);

	seq_printf(seq,
		   "     frags: %d  delay: %llu  clone_skb: %d  ifname: %s\n",
		   pkt_dev->nfrags, (unsigned long long) pkt_dev->delay,
		   pkt_dev->clone_skb, pkt_dev->odevname);

	seq_printf(seq, "     flows: %u flowlen: %u\n", pkt_dev->cflows,
		   pkt_dev->lflow);

	seq_printf(seq,
		   "     queue_map_min: %u  queue_map_max: %u\n",
		   pkt_dev->queue_map_min,
		   pkt_dev->queue_map_max);

	if (pkt_dev->skb_priority)
		seq_printf(seq, "     skb_priority: %u\n",
			   pkt_dev->skb_priority);

	if (pkt_dev->flags & F_IPV6) {
		seq_printf(seq,
			   "     saddr: %pI6c  min_saddr: %pI6c  max_saddr: %pI6c\n"
			   "     daddr: %pI6c  min_daddr: %pI6c  max_daddr: %pI6c\n",
			   &pkt_dev->in6_saddr,
			   &pkt_dev->min_in6_saddr, &pkt_dev->max_in6_saddr,
			   &pkt_dev->in6_daddr,
			   &pkt_dev->min_in6_daddr, &pkt_dev->max_in6_daddr);
	} else {
		seq_printf(seq,
			   "     dst_min: %s  dst_max: %s\n",
			   pkt_dev->dst_min, pkt_dev->dst_max);
		seq_printf(seq,
			   "     src_min: %s  src_max: %s\n",
			   pkt_dev->src_min, pkt_dev->src_max);
	}

	seq_puts(seq, "     src_mac: ");

	seq_printf(seq, "%pM ",
		   is_zero_ether_addr(pkt_dev->src_mac) ?
			     pkt_dev->odev->dev_addr : pkt_dev->src_mac);

	seq_puts(seq, "dst_mac: ");
	seq_printf(seq, "%pM\n", pkt_dev->dst_mac);

	seq_printf(seq,
		   "     udp_src_min: %d  udp_src_max: %d"
		   "  udp_dst_min: %d  udp_dst_max: %d\n",
		   pkt_dev->udp_src_min, pkt_dev->udp_src_max,
		   pkt_dev->udp_dst_min, pkt_dev->udp_dst_max);

	seq_printf(seq,
		   "     src_mac_count: %d  dst_mac_count: %d\n",
		   pkt_dev->src_mac_count, pkt_dev->dst_mac_count);

	if (pkt_dev->nr_labels) {
		seq_puts(seq, "     mpls: ");
		for (i = 0; i < pkt_dev->nr_labels; i++)
			seq_printf(seq, "%08x%s", ntohl(pkt_dev->labels[i]),
				   i == pkt_dev->nr_labels-1 ? "\n" : ", ");
	}

	if (pkt_dev->vlan_id != 0xffff)
		seq_printf(seq, "     vlan_id: %u  vlan_p: %u  vlan_cfi: %u\n",
			   pkt_dev->vlan_id, pkt_dev->vlan_p,
			   pkt_dev->vlan_cfi);

	if (pkt_dev->svlan_id != 0xffff)
		seq_printf(seq, "     svlan_id: %u  vlan_p: %u  vlan_cfi: %u\n",
			   pkt_dev->svlan_id, pkt_dev->svlan_p,
			   pkt_dev->svlan_cfi);

	if (pkt_dev->tos)
		seq_printf(seq, "     tos: 0x%02x\n", pkt_dev->tos);

	if (pkt_dev->traffic_class)
		seq_printf(seq, "     traffic_class: 0x%02x\n", pkt_dev->traffic_class);

	if (pkt_dev->burst > 1)
		seq_printf(seq, "     burst: %d\n", pkt_dev->burst);

	if (pkt_dev->node >= 0)
		seq_printf(seq, "     node: %d\n", pkt_dev->node);

	if (pkt_dev->xmit_mode == M_NETIF_RECEIVE)
		seq_puts(seq, "     xmit_mode: netif_receive\n");
	else if (pkt_dev->xmit_mode == M_QUEUE_XMIT)
		seq_puts(seq, "     xmit_mode: xmit_queue\n");

	seq_puts(seq, "     Flags: ");

	for (i = 0; i < NR_PKT_FLAGS; i++) {
		if (i == F_FLOW_SEQ)
			if (!pkt_dev->cflows)
				continue;

		if (pkt_dev->flags & (1 << i))
			seq_printf(seq, "%s  ", pkt_flag_names[i]);
		else if (i == F_FLOW_SEQ)
			seq_puts(seq, "FLOW_RND  ");

#ifdef CONFIG_XFRM
		if (i == F_IPSEC && pkt_dev->spi)
			seq_printf(seq, "spi:%u", pkt_dev->spi);
#endif
	}

	seq_puts(seq, "\n");

	/* not really stopped, more like last-running-at */
	stopped = pkt_dev->running ? ktime_get() : pkt_dev->stopped_at;
	idle = pkt_dev->idle_acc;
	do_div(idle, NSEC_PER_USEC);

	seq_printf(seq,
		   "Current:\n     pkts-sofar: %llu  errors: %llu\n",
		   (unsigned long long)pkt_dev->sofar,
		   (unsigned long long)pkt_dev->errors);

	seq_printf(seq,
		   "     started: %lluus  stopped: %lluus idle: %lluus\n",
		   (unsigned long long) ktime_to_us(pkt_dev->started_at),
		   (unsigned long long) ktime_to_us(stopped),
		   (unsigned long long) idle);

	seq_printf(seq,
		   "     seq_num: %d  cur_dst_mac_offset: %d  cur_src_mac_offset: %d\n",
		   pkt_dev->seq_num, pkt_dev->cur_dst_mac_offset,
		   pkt_dev->cur_src_mac_offset);

	if (pkt_dev->flags & F_IPV6) {
		seq_printf(seq, "     cur_saddr: %pI6c  cur_daddr: %pI6c\n",
				&pkt_dev->cur_in6_saddr,
				&pkt_dev->cur_in6_daddr);
	} else
		seq_printf(seq, "     cur_saddr: %pI4  cur_daddr: %pI4\n",
			   &pkt_dev->cur_saddr, &pkt_dev->cur_daddr);

	seq_printf(seq, "     cur_udp_dst: %d  cur_udp_src: %d\n",
		   pkt_dev->cur_udp_dst, pkt_dev->cur_udp_src);

	seq_printf(seq, "     cur_queue_map: %u\n", pkt_dev->cur_queue_map);

	seq_printf(seq, "     flows: %u\n", pkt_dev->nflows);

	if (pkt_dev->result[0])
		seq_printf(seq, "Result: %s\n", pkt_dev->result);
	else
		seq_puts(seq, "Result: Idle\n");

	return 0;
}


static int hex32_arg(const char __user *user_buffer, unsigned long maxlen,
		     __u32 *num)
{
	int i = 0;
	*num = 0;

	for (; i < maxlen; i++) {
		int value;
		char c;
		*num <<= 4;
		if (get_user(c, &user_buffer[i]))
			return -EFAULT;
		value = hex_to_bin(c);
		if (value >= 0)
			*num |= value;
		else
			break;
	}
	return i;
}

static int count_trail_chars(const char __user * user_buffer,
			     unsigned int maxlen)
{
	int i;

	for (i = 0; i < maxlen; i++) {
		char c;
		if (get_user(c, &user_buffer[i]))
			return -EFAULT;
		switch (c) {
		case '\"':
		case '\n':
		case '\r':
		case '\t':
		case ' ':
		case '=':
			break;
		default:
			goto done;
		}
	}
done:
	return i;
}

static long num_arg(const char __user *user_buffer, unsigned long maxlen,
				unsigned long *num)
{
	int i;
	*num = 0;

	for (i = 0; i < maxlen; i++) {
		char c;
		if (get_user(c, &user_buffer[i]))
			return -EFAULT;
		if ((c >= '0') && (c <= '9')) {
			*num *= 10;
			*num += c - '0';
		} else
			break;
	}
	return i;
}

static int strn_len(const char __user * user_buffer, unsigned int maxlen)
{
	int i;

	for (i = 0; i < maxlen; i++) {
		char c;
		if (get_user(c, &user_buffer[i]))
			return -EFAULT;
		switch (c) {
		case '\"':
		case '\n':
		case '\r':
		case '\t':
		case ' ':
			goto done_str;
		default:
			break;
		}
	}
done_str:
	return i;
}

static ssize_t get_labels(const char __user *buffer, struct pktgen_dev *pkt_dev)
{
	unsigned int n = 0;
	char c;
	ssize_t i = 0;
	int len;

	pkt_dev->nr_labels = 0;
	do {
		__u32 tmp;
		len = hex32_arg(&buffer[i], 8, &tmp);
		if (len <= 0)
			return len;
		pkt_dev->labels[n] = htonl(tmp);
		if (pkt_dev->labels[n] & MPLS_STACK_BOTTOM)
			pkt_dev->flags |= F_MPLS_RND;
		i += len;
		if (get_user(c, &buffer[i]))
			return -EFAULT;
		i++;
		n++;
		if (n >= MAX_MPLS_LABELS)
			return -E2BIG;
	} while (c == ',');

	pkt_dev->nr_labels = n;
	return i;
}

static __u32 pktgen_read_flag(const char *f, bool *disable)
{
	__u32 i;

	if (f[0] == '!') {
		*disable = true;
		f++;
	}

	for (i = 0; i < NR_PKT_FLAGS; i++) {
		if (!IS_ENABLED(CONFIG_XFRM) && i == IPSEC_SHIFT)
			continue;

		/* allow only disabling ipv6 flag */
		if (!*disable && i == IPV6_SHIFT)
			continue;

		if (strcmp(f, pkt_flag_names[i]) == 0)
			return 1 << i;
	}

	if (strcmp(f, "FLOW_RND") == 0) {
		*disable = !*disable;
		return F_FLOW_SEQ;
	}

	return 0;
}

static ssize_t pktgen_if_write(struct file *file,
			       const char __user * user_buffer, size_t count,
			       loff_t * offset)
{
	struct seq_file *seq = file->private_data;
	struct pktgen_dev *pkt_dev = seq->private;
	int i, max, len;
	char name[16], valstr[32];
	unsigned long value = 0;
	char *pg_result = NULL;
	int tmp = 0;
	char buf[128];

	pg_result = &(pkt_dev->result[0]);

	if (count < 1) {
		pr_warn("wrong command format\n");
		return -EINVAL;
	}

	max = count;
	tmp = count_trail_chars(user_buffer, max);
	if (tmp < 0) {
		pr_warn("illegal format\n");
		return tmp;
	}
	i = tmp;

	/* Read variable name */

	len = strn_len(&user_buffer[i], sizeof(name) - 1);
	if (len < 0)
		return len;

	memset(name, 0, sizeof(name));
	if (copy_from_user(name, &user_buffer[i], len))
		return -EFAULT;
	i += len;

	max = count - i;
	len = count_trail_chars(&user_buffer[i], max);
	if (len < 0)
		return len;

	i += len;

	if (debug) {
		size_t copy = min_t(size_t, count + 1, 1024);
		char *tp = strndup_user(user_buffer, copy);

		if (IS_ERR(tp))
			return PTR_ERR(tp);

		pr_debug("%s,%zu  buffer -:%s:-\n", name, count, tp);
		kfree(tp);
	}

	if (!strcmp(name, "min_pkt_size")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (value < 14 + 20 + 8)
			value = 14 + 20 + 8;
		if (value != pkt_dev->min_pkt_size) {
			pkt_dev->min_pkt_size = value;
			pkt_dev->cur_pkt_size = value;
		}
		sprintf(pg_result, "OK: min_pkt_size=%u",
			pkt_dev->min_pkt_size);
		return count;
	}

	if (!strcmp(name, "max_pkt_size")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (value < 14 + 20 + 8)
			value = 14 + 20 + 8;
		if (value != pkt_dev->max_pkt_size) {
			pkt_dev->max_pkt_size = value;
			pkt_dev->cur_pkt_size = value;
		}
		sprintf(pg_result, "OK: max_pkt_size=%u",
			pkt_dev->max_pkt_size);
		return count;
	}

	/* Shortcut for min = max */

	if (!strcmp(name, "pkt_size")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (value < 14 + 20 + 8)
			value = 14 + 20 + 8;
		if (value != pkt_dev->min_pkt_size) {
			pkt_dev->min_pkt_size = value;
			pkt_dev->max_pkt_size = value;
			pkt_dev->cur_pkt_size = value;
		}
		sprintf(pg_result, "OK: pkt_size=%u", pkt_dev->min_pkt_size);
		return count;
	}

	if (!strcmp(name, "debug")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		debug = value;
		sprintf(pg_result, "OK: debug=%u", debug);
		return count;
	}

	if (!strcmp(name, "frags")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		pkt_dev->nfrags = value;
		sprintf(pg_result, "OK: frags=%u", pkt_dev->nfrags);
		return count;
	}
	if (!strcmp(name, "delay")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (value == 0x7FFFFFFF)
			pkt_dev->delay = ULLONG_MAX;
		else
			pkt_dev->delay = (u64)value;

		sprintf(pg_result, "OK: delay=%llu",
			(unsigned long long) pkt_dev->delay);
		return count;
	}
	if (!strcmp(name, "rate")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (!value)
			return len;
		pkt_dev->delay = pkt_dev->min_pkt_size*8*NSEC_PER_USEC/value;
		if (debug)
			pr_info("Delay set at: %llu ns\n", pkt_dev->delay);

		sprintf(pg_result, "OK: rate=%lu", value);
		return count;
	}
	if (!strcmp(name, "ratep")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (!value)
			return len;
		pkt_dev->delay = NSEC_PER_SEC/value;
		if (debug)
			pr_info("Delay set at: %llu ns\n", pkt_dev->delay);

		sprintf(pg_result, "OK: rate=%lu", value);
		return count;
	}
	if (!strcmp(name, "udp_src_min")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (value != pkt_dev->udp_src_min) {
			pkt_dev->udp_src_min = value;
			pkt_dev->cur_udp_src = value;
		}
		sprintf(pg_result, "OK: udp_src_min=%u", pkt_dev->udp_src_min);
		return count;
	}
	if (!strcmp(name, "udp_dst_min")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (value != pkt_dev->udp_dst_min) {
			pkt_dev->udp_dst_min = value;
			pkt_dev->cur_udp_dst = value;
		}
		sprintf(pg_result, "OK: udp_dst_min=%u", pkt_dev->udp_dst_min);
		return count;
	}
	if (!strcmp(name, "udp_src_max")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (value != pkt_dev->udp_src_max) {
			pkt_dev->udp_src_max = value;
			pkt_dev->cur_udp_src = value;
		}
		sprintf(pg_result, "OK: udp_src_max=%u", pkt_dev->udp_src_max);
		return count;
	}
	if (!strcmp(name, "udp_dst_max")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (value != pkt_dev->udp_dst_max) {
			pkt_dev->udp_dst_max = value;
			pkt_dev->cur_udp_dst = value;
		}
		sprintf(pg_result, "OK: udp_dst_max=%u", pkt_dev->udp_dst_max);
		return count;
	}
	if (!strcmp(name, "clone_skb")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;
		if ((value > 0) &&
		    ((pkt_dev->xmit_mode == M_NETIF_RECEIVE) ||
		     !(pkt_dev->odev->priv_flags & IFF_TX_SKB_SHARING)))
			return -ENOTSUPP;
		i += len;
		pkt_dev->clone_skb = value;

		sprintf(pg_result, "OK: clone_skb=%d", pkt_dev->clone_skb);
		return count;
	}
	if (!strcmp(name, "count")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		pkt_dev->count = value;
		sprintf(pg_result, "OK: count=%llu",
			(unsigned long long)pkt_dev->count);
		return count;
	}
	if (!strcmp(name, "src_mac_count")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (pkt_dev->src_mac_count != value) {
			pkt_dev->src_mac_count = value;
			pkt_dev->cur_src_mac_offset = 0;
		}
		sprintf(pg_result, "OK: src_mac_count=%d",
			pkt_dev->src_mac_count);
		return count;
	}
	if (!strcmp(name, "dst_mac_count")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (pkt_dev->dst_mac_count != value) {
			pkt_dev->dst_mac_count = value;
			pkt_dev->cur_dst_mac_offset = 0;
		}
		sprintf(pg_result, "OK: dst_mac_count=%d",
			pkt_dev->dst_mac_count);
		return count;
	}
	if (!strcmp(name, "burst")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if ((value > 1) &&
		    ((pkt_dev->xmit_mode == M_QUEUE_XMIT) ||
		     ((pkt_dev->xmit_mode == M_START_XMIT) &&
		     (!(pkt_dev->odev->priv_flags & IFF_TX_SKB_SHARING)))))
			return -ENOTSUPP;
		pkt_dev->burst = value < 1 ? 1 : value;
		sprintf(pg_result, "OK: burst=%d", pkt_dev->burst);
		return count;
	}
	if (!strcmp(name, "node")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;

		if (node_possible(value)) {
			pkt_dev->node = value;
			sprintf(pg_result, "OK: node=%d", pkt_dev->node);
			if (pkt_dev->page) {
				put_page(pkt_dev->page);
				pkt_dev->page = NULL;
			}
		}
		else
			sprintf(pg_result, "ERROR: node not possible");
		return count;
	}
	if (!strcmp(name, "xmit_mode")) {
		char f[32];

		memset(f, 0, 32);
		len = strn_len(&user_buffer[i], sizeof(f) - 1);
		if (len < 0)
			return len;

		if (copy_from_user(f, &user_buffer[i], len))
			return -EFAULT;
		i += len;

		if (strcmp(f, "start_xmit") == 0) {
			pkt_dev->xmit_mode = M_START_XMIT;
		} else if (strcmp(f, "netif_receive") == 0) {
			/* clone_skb set earlier, not supported in this mode */
			if (pkt_dev->clone_skb > 0)
				return -ENOTSUPP;

			pkt_dev->xmit_mode = M_NETIF_RECEIVE;

			/* make sure new packet is allocated every time
			 * pktgen_xmit() is called
			 */
			pkt_dev->last_ok = 1;

			/* override clone_skb if user passed default value
			 * at module loading time
			 */
			pkt_dev->clone_skb = 0;
		} else if (strcmp(f, "queue_xmit") == 0) {
			pkt_dev->xmit_mode = M_QUEUE_XMIT;
			pkt_dev->last_ok = 1;
		} else {
			sprintf(pg_result,
				"xmit_mode -:%s:- unknown\nAvailable modes: %s",
				f, "start_xmit, netif_receive\n");
			return count;
		}
		sprintf(pg_result, "OK: xmit_mode=%s", f);
		return count;
	}
	if (!strcmp(name, "flag")) {
		__u32 flag;
		char f[32];
		bool disable = false;

		memset(f, 0, 32);
		len = strn_len(&user_buffer[i], sizeof(f) - 1);
		if (len < 0)
			return len;

		if (copy_from_user(f, &user_buffer[i], len))
			return -EFAULT;
		i += len;

		flag = pktgen_read_flag(f, &disable);

		if (flag) {
			if (disable)
				pkt_dev->flags &= ~flag;
			else
				pkt_dev->flags |= flag;
		} else {
			sprintf(pg_result,
				"Flag -:%s:- unknown\nAvailable flags, (prepend ! to un-set flag):\n%s",
				f,
				"IPSRC_RND, IPDST_RND, UDPSRC_RND, UDPDST_RND, "
				"MACSRC_RND, MACDST_RND, TXSIZE_RND, IPV6, "
				"MPLS_RND, VID_RND, SVID_RND, FLOW_SEQ, "
				"QUEUE_MAP_RND, QUEUE_MAP_CPU, UDPCSUM, "
				"NO_TIMESTAMP, "
#ifdef CONFIG_XFRM
				"IPSEC, "
#endif
				"NODE_ALLOC\n");
			return count;
		}
		sprintf(pg_result, "OK: flags=0x%x", pkt_dev->flags);
		return count;
	}
	if (!strcmp(name, "dst_min") || !strcmp(name, "dst")) {
		len = strn_len(&user_buffer[i], sizeof(pkt_dev->dst_min) - 1);
		if (len < 0)
			return len;

		if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
		buf[len] = 0;
		if (strcmp(buf, pkt_dev->dst_min) != 0) {
			memset(pkt_dev->dst_min, 0, sizeof(pkt_dev->dst_min));
			strcpy(pkt_dev->dst_min, buf);
			pkt_dev->daddr_min = in_aton(pkt_dev->dst_min);
			pkt_dev->cur_daddr = pkt_dev->daddr_min;
		}
		if (debug)
			pr_debug("dst_min set to: %s\n", pkt_dev->dst_min);
		i += len;
		sprintf(pg_result, "OK: dst_min=%s", pkt_dev->dst_min);
		return count;
	}
	if (!strcmp(name, "dst_max")) {
		len = strn_len(&user_buffer[i], sizeof(pkt_dev->dst_max) - 1);
		if (len < 0)
			return len;

		if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
		buf[len] = 0;
		if (strcmp(buf, pkt_dev->dst_max) != 0) {
			memset(pkt_dev->dst_max, 0, sizeof(pkt_dev->dst_max));
			strcpy(pkt_dev->dst_max, buf);
			pkt_dev->daddr_max = in_aton(pkt_dev->dst_max);
			pkt_dev->cur_daddr = pkt_dev->daddr_max;
		}
		if (debug)
			pr_debug("dst_max set to: %s\n", pkt_dev->dst_max);
		i += len;
		sprintf(pg_result, "OK: dst_max=%s", pkt_dev->dst_max);
		return count;
	}
	if (!strcmp(name, "dst6")) {
		len = strn_len(&user_buffer[i], sizeof(buf) - 1);
		if (len < 0)
			return len;

		pkt_dev->flags |= F_IPV6;

		if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
		buf[len] = 0;

		in6_pton(buf, -1, pkt_dev->in6_daddr.s6_addr, -1, NULL);
		snprintf(buf, sizeof(buf), "%pI6c", &pkt_dev->in6_daddr);

		pkt_dev->cur_in6_daddr = pkt_dev->in6_daddr;

		if (debug)
			pr_debug("dst6 set to: %s\n", buf);

		i += len;
		sprintf(pg_result, "OK: dst6=%s", buf);
		return count;
	}
	if (!strcmp(name, "dst6_min")) {
		len = strn_len(&user_buffer[i], sizeof(buf) - 1);
		if (len < 0)
			return len;

		pkt_dev->flags |= F_IPV6;

		if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
		buf[len] = 0;

		in6_pton(buf, -1, pkt_dev->min_in6_daddr.s6_addr, -1, NULL);
		snprintf(buf, sizeof(buf), "%pI6c", &pkt_dev->min_in6_daddr);

		pkt_dev->cur_in6_daddr = pkt_dev->min_in6_daddr;
		if (debug)
			pr_debug("dst6_min set to: %s\n", buf);

		i += len;
		sprintf(pg_result, "OK: dst6_min=%s", buf);
		return count;
	}
	if (!strcmp(name, "dst6_max")) {
		len = strn_len(&user_buffer[i], sizeof(buf) - 1);
		if (len < 0)
			return len;

		pkt_dev->flags |= F_IPV6;

		if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
		buf[len] = 0;

		in6_pton(buf, -1, pkt_dev->max_in6_daddr.s6_addr, -1, NULL);
		snprintf(buf, sizeof(buf), "%pI6c", &pkt_dev->max_in6_daddr);

		if (debug)
			pr_debug("dst6_max set to: %s\n", buf);

		i += len;
		sprintf(pg_result, "OK: dst6_max=%s", buf);
		return count;
	}
	if (!strcmp(name, "src6")) {
		len = strn_len(&user_buffer[i], sizeof(buf) - 1);
		if (len < 0)
			return len;

		pkt_dev->flags |= F_IPV6;

		if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
		buf[len] = 0;

		in6_pton(buf, -1, pkt_dev->in6_saddr.s6_addr, -1, NULL);
		snprintf(buf, sizeof(buf), "%pI6c", &pkt_dev->in6_saddr);

		pkt_dev->cur_in6_saddr = pkt_dev->in6_saddr;

		if (debug)
			pr_debug("src6 set to: %s\n", buf);

		i += len;
		sprintf(pg_result, "OK: src6=%s", buf);
		return count;
	}
	if (!strcmp(name, "src_min")) {
		len = strn_len(&user_buffer[i], sizeof(pkt_dev->src_min) - 1);
		if (len < 0)
			return len;

		if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
		buf[len] = 0;
		if (strcmp(buf, pkt_dev->src_min) != 0) {
			memset(pkt_dev->src_min, 0, sizeof(pkt_dev->src_min));
			strcpy(pkt_dev->src_min, buf);
			pkt_dev->saddr_min = in_aton(pkt_dev->src_min);
			pkt_dev->cur_saddr = pkt_dev->saddr_min;
		}
		if (debug)
			pr_debug("src_min set to: %s\n", pkt_dev->src_min);
		i += len;
		sprintf(pg_result, "OK: src_min=%s", pkt_dev->src_min);
		return count;
	}
	if (!strcmp(name, "src_max")) {
		len = strn_len(&user_buffer[i], sizeof(pkt_dev->src_max) - 1);
		if (len < 0)
			return len;

		if (copy_from_user(buf, &user_buffer[i], len))
			return -EFAULT;
		buf[len] = 0;
		if (strcmp(buf, pkt_dev->src_max) != 0) {
			memset(pkt_dev->src_max, 0, sizeof(pkt_dev->src_max));
			strcpy(pkt_dev->src_max, buf);
			pkt_dev->saddr_max = in_aton(pkt_dev->src_max);
			pkt_dev->cur_saddr = pkt_dev->saddr_max;
		}
		if (debug)
			pr_debug("src_max set to: %s\n", pkt_dev->src_max);
		i += len;
		sprintf(pg_result, "OK: src_max=%s", pkt_dev->src_max);
		return count;
	}
	if (!strcmp(name, "dst_mac")) {
		len = strn_len(&user_buffer[i], sizeof(valstr) - 1);
		if (len < 0)
			return len;

		memset(valstr, 0, sizeof(valstr));
		if (copy_from_user(valstr, &user_buffer[i], len))
			return -EFAULT;

		if (!mac_pton(valstr, pkt_dev->dst_mac))
			return -EINVAL;
		/* Set up Dest MAC */
		ether_addr_copy(&pkt_dev->hh[0], pkt_dev->dst_mac);

		sprintf(pg_result, "OK: dstmac %pM", pkt_dev->dst_mac);
		return count;
	}
	if (!strcmp(name, "src_mac")) {
		len = strn_len(&user_buffer[i], sizeof(valstr) - 1);
		if (len < 0)
			return len;

		memset(valstr, 0, sizeof(valstr));
		if (copy_from_user(valstr, &user_buffer[i], len))
			return -EFAULT;

		if (!mac_pton(valstr, pkt_dev->src_mac))
			return -EINVAL;
		/* Set up Src MAC */
		ether_addr_copy(&pkt_dev->hh[6], pkt_dev->src_mac);

		sprintf(pg_result, "OK: srcmac %pM", pkt_dev->src_mac);
		return count;
	}

	if (!strcmp(name, "clear_counters")) {
		pktgen_clear_counters(pkt_dev);
		sprintf(pg_result, "OK: Clearing counters.\n");
		return count;
	}

	if (!strcmp(name, "flows")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		if (value > MAX_CFLOWS)
			value = MAX_CFLOWS;

		pkt_dev->cflows = value;
		sprintf(pg_result, "OK: flows=%u", pkt_dev->cflows);
		return count;
	}
#ifdef CONFIG_XFRM
	if (!strcmp(name, "spi")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		pkt_dev->spi = value;
		sprintf(pg_result, "OK: spi=%u", pkt_dev->spi);
		return count;
	}
#endif
	if (!strcmp(name, "flowlen")) {
		len = num_arg(&user_buffer[i], 10, &value);
		if (len < 0)
			return len;

		i += len;
		pkt_dev->lflow = value;
		sprintf(pg_result, "OK: flowlen=%u", pkt_dev->lflow);
		return count;
	}

	if (!strcmp(name, "queue_map_min")) {
		len = num_arg(&user_buffer[i], 5, &value);
		if (len < 0)
			return len;

		i += len;
		pkt_dev->queue_map_min = value;
		sprintf(pg_result, "OK: queue_map_min=%u", pkt_dev->queue_map_min);
		return count;
	}

	if (!strcmp(name, "queue_map_max")) {
		len = num_arg(&user_buffer[i], 5, &value);
		if (len < 0)
			return len;

		i += len;
		pkt_dev->queue_map_max = value;
		sprintf(pg_result, "OK: queue_map_max=%u", pkt_dev->queue_map_max);
		return count;
	}

	if (!strcmp(name, "mpls")) {
		unsigned int n, cnt;

		len = get_labels(&user_buffer[i], pkt_dev);
		if (len < 0)
			return len;
		i += len;
		cnt = sprintf(pg_result, "OK: mpls=");
		for (n = 0; n < pkt_dev->nr_labels; n++)
			cnt += sprintf(pg_result + cnt,
				       "%08x%s", ntohl(pkt_dev->labels[n]),
				       n == pkt_dev->nr_labels-1 ? "" : ",");

		if (pkt_dev->nr_labels && pkt_dev->vlan_id != 0xffff) {
			pkt_dev->vlan_id = 0xffff; /* turn off VLAN/SVLAN */
			pkt_dev->svlan_id = 0xffff;

			if (debug)
				pr_debug("VLAN/SVLAN auto turned off\n");
		}
		return count;
	}

	if (!strcmp(name, "vlan_id")) {
		len = num_arg(&user_buffer[i], 4, &value);
		if (len < 0)
			return len;

		i += len;
		if (value <= 4095) {
			pkt_dev->vlan_id = value;  /* turn on VLAN */

			if (debug)
				pr_debug("VLAN turned on\n");

			if (debug && pkt_dev->nr_labels)
				pr_debug("MPLS auto turned off\n");

			pkt_dev->nr_labels = 0;    /* turn off MPLS */
			sprintf(pg_result, "OK: vlan_id=%u", pkt_dev->vlan_id);
		} else {
			pkt_dev->vlan_id = 0xffff; /* turn off VLAN/SVLAN */
			pkt_dev->svlan_id = 0xffff;

			if (debug)
				pr_debug("VLAN/SVLAN turned off\n");
		}
		return count;
	}

	if (!strcmp(name, "vlan_p")) {
		len = num_arg(&user_buffer[i], 1, &value);
		if (len < 0)
			return len;

		i += len;
		if ((value <= 7) && (pkt_dev->vlan_id != 0xffff)) {
			pkt_dev->vlan_p = value;
			sprintf(pg_result, "OK: vlan_p=%u", pkt_dev->vlan_p);
		} else {
			sprintf(pg_result, "ERROR: vlan_p must be 0-7");
		}
		return count;
	}

	if (!strcmp(name, "vlan_cfi")) {
		len = num_arg(&user_buffer[i], 1, &value);
		if (len < 0)
			return len;

		i += len;
		if ((value <= 1) && (pkt_dev->vlan_id != 0xffff)) {
			pkt_dev->vlan_cfi = value;
			sprintf(pg_result, "OK: vlan_cfi=%u", pkt_dev->vlan_cfi);
		} else {
			sprintf(pg_result, "ERROR: vlan_cfi must be 0-1");
		}
		return count;
	}

	if (!strcmp(name, "svlan_id")) {
		len = num_arg(&user_buffer[i], 4, &value);
		if (len < 0)
			return len;

		i += len;
		if ((value <= 4095) && ((pkt_dev->vlan_id != 0xffff))) {
			pkt_dev->svlan_id = value;  /* turn on SVLAN */

			if (debug)
				pr_debug("SVLAN turned on\n");

			if (debug && pkt_dev->nr_labels)
				pr_debug("MPLS auto turned off\n");

			pkt_dev->nr_labels = 0;    /* turn off MPLS */
			sprintf(pg_result, "OK: svlan_id=%u", pkt_dev->svlan_id);
		} else {
			pkt_dev->vlan_id = 0xffff; /* turn off VLAN/SVLAN */
			pkt_dev->svlan_id = 0xffff;

			if (debug)
				pr_debug("VLAN/SVLAN turned off\n");
		}
		return count;
	}

	if (!strcmp(name, "svlan_p")) {
		len = num_arg(&user_buffer[i], 1, &value);
		if (len < 0)
			return len;

		i += len;
		if ((value <= 7) && (pkt_dev->svlan_id != 0xffff)) {
			pkt_dev->svlan_p = value;
			sprintf(pg_result, "OK: svlan_p=%u", pkt_dev->svlan_p);
		} else {
			sprintf(pg_result, "ERROR: svlan_p must be 0-7");
		}
		return count;
	}

	if (!strcmp(name, "svlan_cfi")) {
		len = num_arg(&user_buffer[i], 1, &value);
		if (len < 0)
			return len;

		i += len;
		if ((value <= 1) && (pkt_dev->svlan_id != 0xffff)) {
			pkt_dev->svlan_cfi = value;
			sprintf(pg_result, "OK: svlan_cfi=%u", pkt_dev->svlan_cfi);
		} else {
			sprintf(pg_result, "ERROR: svlan_cfi must be 0-1");
		}
		return count;
	}

	if (!strcmp(name, "tos")) {
		__u32 tmp_value = 0;
		len = hex32_arg(&user_buffer[i], 2, &tmp_value);
		if (len < 0)
			return len;

		i += len;
		if (len == 2) {
			pkt_dev->tos = tmp_value;
			sprintf(pg_result, "OK: tos=0x%02x", pkt_dev->tos);
		} else {
			sprintf(pg_result, "ERROR: tos must be 00-ff");
		}
		return count;
	}

	if (!strcmp(name, "traffic_class")) {
		__u32 tmp_value = 0;
		len = hex32_arg(&user_buffer[i], 2, &tmp_value);
		if (len < 0)
			return len;

		i += len;
		if (len == 2) {
			pkt_dev->traffic_class = tmp_value;
			sprintf(pg_result, "OK: traffic_class=0x%02x", pkt_dev->traffic_class);
		} else {
			sprintf(pg_result, "ERROR: traffic_class must be 00-ff");
		}
		return count;
	}

	if (!strcmp(name, "skb_priority")) {
		len = num_arg(&user_buffer[i], 9, &value);
		if (len < 0)
			return len;

		i += len;
		pkt_dev->skb_priority = value;
		sprintf(pg_result, "OK: skb_priority=%i",
			pkt_dev->skb_priority);
		return count;
	}

	sprintf(pkt_dev->result, "No such parameter \"%s\"", name);
	return -EINVAL;
}

static int pktgen_if_open(struct inode *inode, struct file *file)
{
	return single_open(file, pktgen_if_show, PDE_DATA(inode));
}

static const struct file_operations pktgen_if_fops = {
	.open    = pktgen_if_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.write   = pktgen_if_write,
	.release = single_release,
};

static int pktgen_thread_show(struct seq_file *seq, void *v)
{
	struct pktgen_thread *t = seq->private;
	const struct pktgen_dev *pkt_dev;

	BUG_ON(!t);

	seq_puts(seq, "Running: ");

	rcu_read_lock();
	list_for_each_entry_rcu(pkt_dev, &t->if_list, list)
		if (pkt_dev->running)
			seq_printf(seq, "%s ", pkt_dev->odevname);

	seq_puts(seq, "\nStopped: ");

	list_for_each_entry_rcu(pkt_dev, &t->if_list, list)
		if (!pkt_dev->running)
			seq_printf(seq, "%s ", pkt_dev->odevname);

	if (t->result[0])
		seq_printf(seq, "\nResult: %s\n", t->result);
	else
		seq_puts(seq, "\nResult: NA\n");

	rcu_read_unlock();

	return 0;
}

static ssize_t pktgen_thread_write(struct file *file,
				   const char __user * user_buffer,
				   size_t count, loff_t * offset)
{
	struct seq_file *seq = file->private_data;
	struct pktgen_thread *t = seq->private;
	int i, max, len, ret;
	char name[40];
	char *pg_result;

	if (count < 1) {
		//      sprintf(pg_result, "Wrong command format");
		return -EINVAL;
	}

	max = count;
	len = count_trail_chars(user_buffer, max);
	if (len < 0)
		return len;

	i = len;

	/* Read variable name */

	len = strn_len(&user_buffer[i], sizeof(name) - 1);
	if (len < 0)
		return len;

	memset(name, 0, sizeof(name));
	if (copy_from_user(name, &user_buffer[i], len))
		return -EFAULT;
	i += len;

	max = count - i;
	len = count_trail_chars(&user_buffer[i], max);
	if (len < 0)
		return len;

	i += len;

	if (debug)
		pr_debug("t=%s, count=%lu\n", name, (unsigned long)count);

	if (!t) {
		pr_err("ERROR: No thread\n");
		ret = -EINVAL;
		goto out;
	}

	pg_result = &(t->result[0]);

	if (!strcmp(name, "add_device")) {
		char f[32];
		memset(f, 0, 32);
		len = strn_len(&user_buffer[i], sizeof(f) - 1);
		if (len < 0) {
			ret = len;
			goto out;
		}
		if (copy_from_user(f, &user_buffer[i], len))
			return -EFAULT;
		i += len;
		mutex_lock(&pktgen_thread_lock);
		ret = pktgen_add_device(t, f);
		mutex_unlock(&pktgen_thread_lock);
		if (!ret) {
			ret = count;
			sprintf(pg_result, "OK: add_device=%s", f);
		} else
			sprintf(pg_result, "ERROR: can not add device %s", f);
		goto out;
	}

	if (!strcmp(name, "rem_device_all")) {
		mutex_lock(&pktgen_thread_lock);
		t->control |= T_REMDEVALL;
		mutex_unlock(&pktgen_thread_lock);
		schedule_timeout_interruptible(msecs_to_jiffies(125));	/* Propagate thread->control  */
		ret = count;
		sprintf(pg_result, "OK: rem_device_all");
		goto out;
	}

	if (!strcmp(name, "max_before_softirq")) {
		sprintf(pg_result, "OK: Note! max_before_softirq is obsoleted -- Do not use");
		ret = count;
		goto out;
	}

	ret = -EINVAL;
out:
	return ret;
}

static int pktgen_thread_open(struct inode *inode, struct file *file)
{
	return single_open(file, pktgen_thread_show, PDE_DATA(inode));
}

static const struct file_operations pktgen_thread_fops = {
	.open    = pktgen_thread_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.write   = pktgen_thread_write,
	.release = single_release,
};

/* Think find or remove for NN */
static struct pktgen_dev *__pktgen_NN_threads(const struct pktgen_net *pn,
					      const char *ifname, int remove)
{
	struct pktgen_thread *t;
	struct pktgen_dev *pkt_dev = NULL;
	bool exact = (remove == FIND);

	list_for_each_entry(t, &pn->pktgen_threads, th_list) {
		pkt_dev = pktgen_find_dev(t, ifname, exact);
		if (pkt_dev) {
			if (remove) {
				pkt_dev->removal_mark = 1;
				t->control |= T_REMDEV;
			}
			break;
		}
	}
	return pkt_dev;
}

/*
 * mark a device for removal
 */
static void pktgen_mark_device(const struct pktgen_net *pn, const char *ifname)
{
	struct pktgen_dev *pkt_dev = NULL;
	const int max_tries = 10, msec_per_try = 125;
	int i = 0;

	mutex_lock(&pktgen_thread_lock);
	pr_debug("%s: marking %s for removal\n", __func__, ifname);

	while (1) {

		pkt_dev = __pktgen_NN_threads(pn, ifname, REMOVE);
		if (pkt_dev == NULL)
			break;	/* success */

		mutex_unlock(&pktgen_thread_lock);
		pr_debug("%s: waiting for %s to disappear....\n",
			 __func__, ifname);
		schedule_timeout_interruptible(msecs_to_jiffies(msec_per_try));
		mutex_lock(&pktgen_thread_lock);

		if (++i >= max_tries) {
			pr_err("%s: timed out after waiting %d msec for device %s to be removed\n",
			       __func__, msec_per_try * i, ifname);
			break;
		}

	}

	mutex_unlock(&pktgen_thread_lock);
}

static void pktgen_change_name(const struct pktgen_net *pn, struct net_device *dev)
{
	struct pktgen_thread *t;

	mutex_lock(&pktgen_thread_lock);

	list_for_each_entry(t, &pn->pktgen_threads, th_list) {
		struct pktgen_dev *pkt_dev;

		if_lock(t);
		list_for_each_entry(pkt_dev, &t->if_list, list) {
			if (pkt_dev->odev != dev)
				continue;

			proc_remove(pkt_dev->entry);

			pkt_dev->entry = proc_create_data(dev->name, 0600,
							  pn->proc_dir,
							  &pktgen_if_fops,
							  pkt_dev);
			if (!pkt_dev->entry)
				pr_err("can't move proc entry for '%s'\n",
				       dev->name);
			break;
		}
		if_unlock(t);
	}
	mutex_unlock(&pktgen_thread_lock);
}

static int pktgen_device_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct pktgen_net *pn = net_generic(dev_net(dev), pg_net_id);

	if (pn->pktgen_exiting)
		return NOTIFY_DONE;

	/* It is OK that we do not hold the group lock right now,
	 * as we run under the RTNL lock.
	 */

	switch (event) {
	case NETDEV_CHANGENAME:
		pktgen_change_name(pn, dev);
		break;

	case NETDEV_UNREGISTER:
		pktgen_mark_device(pn, dev->name);
		break;
	}

	return NOTIFY_DONE;
}

static struct net_device *pktgen_dev_get_by_name(const struct pktgen_net *pn,
						 struct pktgen_dev *pkt_dev,
						 const char *ifname)
{
	char b[IFNAMSIZ+5];
	int i;

	for (i = 0; ifname[i] != '@'; i++) {
		if (i == IFNAMSIZ)
			break;

		b[i] = ifname[i];
	}
	b[i] = 0;

	return dev_get_by_name(pn->net, b);
}


/* Associate pktgen_dev with a device. */

static int pktgen_setup_dev(const struct pktgen_net *pn,
			    struct pktgen_dev *pkt_dev, const char *ifname)
{
	struct net_device *odev;
	int err;

	/* Clean old setups */
	if (pkt_dev->odev) {
		dev_put(pkt_dev->odev);
		pkt_dev->odev = NULL;
	}

	odev = pktgen_dev_get_by_name(pn, pkt_dev, ifname);
	if (!odev) {
		pr_err("no such netdevice: \"%s\"\n", ifname);
		return -ENODEV;
	}

	if (odev->type != ARPHRD_ETHER) {
		pr_err("not an ethernet device: \"%s\"\n", ifname);
		err = -EINVAL;
	} else if (!netif_running(odev)) {
		pr_err("device is down: \"%s\"\n", ifname);
		err = -ENETDOWN;
	} else {
		pkt_dev->odev = odev;
		return 0;
	}

	dev_put(odev);
	return err;
}

/* Read pkt_dev from the interface and set up internal pktgen_dev
 * structure to have the right information to create/send packets
 */
static void pktgen_setup_inject(struct pktgen_dev *pkt_dev)
{
	int ntxq;

	if (!pkt_dev->odev) {
		pr_err("ERROR: pkt_dev->odev == NULL in setup_inject\n");
		sprintf(pkt_dev->result,
			"ERROR: pkt_dev->odev == NULL in setup_inject.\n");
		return;
	}

	/* make sure that we don't pick a non-existing transmit queue */
	ntxq = pkt_dev->odev->real_num_tx_queues;

	if (ntxq <= pkt_dev->queue_map_min) {
		pr_warn("WARNING: Requested queue_map_min (zero-based) (%d) exceeds valid range [0 - %d] for (%d) queues on %s, resetting\n",
			pkt_dev->queue_map_min, (ntxq ?: 1) - 1, ntxq,
			pkt_dev->odevname);
		pkt_dev->queue_map_min = (ntxq ?: 1) - 1;
	}
	if (pkt_dev->queue_map_max >= ntxq) {
		pr_warn("WARNING: Requested queue_map_max (zero-based) (%d) exceeds valid range [0 - %d] for (%d) queues on %s, resetting\n",
			pkt_dev->queue_map_max, (ntxq ?: 1) - 1, ntxq,
			pkt_dev->odevname);
		pkt_dev->queue_map_max = (ntxq ?: 1) - 1;
	}

	/* Default to the interface's mac if not explicitly set. */

	if (is_zero_ether_addr(pkt_dev->src_mac))
		ether_addr_copy(&(pkt_dev->hh[6]), pkt_dev->odev->dev_addr);

	/* Set up Dest MAC */
	ether_addr_copy(&(pkt_dev->hh[0]), pkt_dev->dst_mac);

	if (pkt_dev->flags & F_IPV6) {
		int i, set = 0, err = 1;
		struct inet6_dev *idev;

		if (pkt_dev->min_pkt_size == 0) {
			pkt_dev->min_pkt_size = 14 + sizeof(struct ipv6hdr)
						+ sizeof(struct udphdr)
						+ sizeof(struct pktgen_hdr)
						+ pkt_dev->pkt_overhead;
		}

		for (i = 0; i < sizeof(struct in6_addr); i++)
			if (pkt_dev->cur_in6_saddr.s6_addr[i]) {
				set = 1;
				break;
			}

		if (!set) {

			/*
			 * Use linklevel address if unconfigured.
			 *
			 * use ipv6_get_lladdr if/when it's get exported
			 */

			rcu_read_lock();
			idev = __in6_dev_get(pkt_dev->odev);
			if (idev) {
				struct inet6_ifaddr *ifp;

				read_lock_bh(&idev->lock);
				list_for_each_entry(ifp, &idev->addr_list, if_list) {
					if ((ifp->scope & IFA_LINK) &&
					    !(ifp->flags & IFA_F_TENTATIVE)) {
						pkt_dev->cur_in6_saddr = ifp->addr;
						err = 0;
						break;
					}
				}
				read_unlock_bh(&idev->lock);
			}
			rcu_read_unlock();
			if (err)
				pr_err("ERROR: IPv6 link address not available\n");
		}
	} else {
		if (pkt_dev->min_pkt_size == 0) {
			pkt_dev->min_pkt_size = 14 + sizeof(struct iphdr)
						+ sizeof(struct udphdr)
						+ sizeof(struct pktgen_hdr)
						+ pkt_dev->pkt_overhead;
		}

		pkt_dev->saddr_min = 0;
		pkt_dev->saddr_max = 0;
		if (strlen(pkt_dev->src_min) == 0) {

			struct in_device *in_dev;

			rcu_read_lock();
			in_dev = __in_dev_get_rcu(pkt_dev->odev);
			if (in_dev) {
				const struct in_ifaddr *ifa;

				ifa = rcu_dereference(in_dev->ifa_list);
				if (ifa) {
					pkt_dev->saddr_min = ifa->ifa_address;
					pkt_dev->saddr_max = pkt_dev->saddr_min;
				}
			}
			rcu_read_unlock();
		} else {
			pkt_dev->saddr_min = in_aton(pkt_dev->src_min);
			pkt_dev->saddr_max = in_aton(pkt_dev->src_max);
		}

		pkt_dev->daddr_min = in_aton(pkt_dev->dst_min);
		pkt_dev->daddr_max = in_aton(pkt_dev->dst_max);
	}
	/* Initialize current values. */
	pkt_dev->cur_pkt_size = pkt_dev->min_pkt_size;
	if (pkt_dev->min_pkt_size > pkt_dev->max_pkt_size)
		pkt_dev->max_pkt_size = pkt_dev->min_pkt_size;

	pkt_dev->cur_dst_mac_offset = 0;
	pkt_dev->cur_src_mac_offset = 0;
	pkt_dev->cur_saddr = pkt_dev->saddr_min;
	pkt_dev->cur_daddr = pkt_dev->daddr_min;
	pkt_dev->cur_udp_dst = pkt_dev->udp_dst_min;
	pkt_dev->cur_udp_src = pkt_dev->udp_src_min;
	pkt_dev->nflows = 0;
}


static void spin(struct pktgen_dev *pkt_dev, ktime_t spin_until)
{
	ktime_t start_time, end_time;
	s64 remaining;
	struct hrtimer_sleeper t;

	hrtimer_init_sleeper_on_stack(&t, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	hrtimer_set_expires(&t.timer, spin_until);

	remaining = ktime_to_ns(hrtimer_expires_remaining(&t.timer));
	if (remaining <= 0)
		goto out;

	start_time = ktime_get();
	if (remaining < 100000) {
		/* for small delays (<100us), just loop until limit is reached */
		do {
			end_time = ktime_get();
		} while (ktime_compare(end_time, spin_until) < 0);
	} else {
		do {
			set_current_state(TASK_INTERRUPTIBLE);
			hrtimer_sleeper_start_expires(&t, HRTIMER_MODE_ABS);

			if (likely(t.task))
				schedule();

			hrtimer_cancel(&t.timer);
		} while (t.task && pkt_dev->running && !signal_pending(current));
		__set_current_state(TASK_RUNNING);
		end_time = ktime_get();
	}

	pkt_dev->idle_acc += ktime_to_ns(ktime_sub(end_time, start_time));
out:
	pkt_dev->next_tx = ktime_add_ns(spin_until, pkt_dev->delay);
	destroy_hrtimer_on_stack(&t.timer);
}

static inline void set_pkt_overhead(struct pktgen_dev *pkt_dev)
{
	pkt_dev->pkt_overhead = 0;
	pkt_dev->pkt_overhead += pkt_dev->nr_labels*sizeof(u32);
	pkt_dev->pkt_overhead += VLAN_TAG_SIZE(pkt_dev);
	pkt_dev->pkt_overhead += SVLAN_TAG_SIZE(pkt_dev);
}

static inline int f_seen(const struct pktgen_dev *pkt_dev, int flow)
{
	return !!(pkt_dev->flows[flow].flags & F_INIT);
}

static inline int f_pick(struct pktgen_dev *pkt_dev)
{
	int flow = pkt_dev->curfl;

	if (pkt_dev->flags & F_FLOW_SEQ) {
		if (pkt_dev->flows[flow].count >= pkt_dev->lflow) {
			/* reset time */
			pkt_dev->flows[flow].count = 0;
			pkt_dev->flows[flow].flags = 0;
			pkt_dev->curfl += 1;
			if (pkt_dev->curfl >= pkt_dev->cflows)
				pkt_dev->curfl = 0; /*reset */
		}
	} else {
		flow = prandom_u32() % pkt_dev->cflows;
		pkt_dev->curfl = flow;

		if (pkt_dev->flows[flow].count > pkt_dev->lflow) {
			pkt_dev->flows[flow].count = 0;
			pkt_dev->flows[flow].flags = 0;
		}
	}

	return pkt_dev->curfl;
}


#ifdef CONFIG_XFRM
/* If there was already an IPSEC SA, we keep it as is, else
 * we go look for it ...
*/
#define DUMMY_MARK 0
static void get_ipsec_sa(struct pktgen_dev *pkt_dev, int flow)
{
	struct xfrm_state *x = pkt_dev->flows[flow].x;
	struct pktgen_net *pn = net_generic(dev_net(pkt_dev->odev), pg_net_id);
	if (!x) {

		if (pkt_dev->spi) {
			/* We need as quick as possible to find the right SA
			 * Searching with minimum criteria to archieve this.
			 */
			x = xfrm_state_lookup_byspi(pn->net, htonl(pkt_dev->spi), AF_INET);
		} else {
			/* slow path: we dont already have xfrm_state */
			x = xfrm_stateonly_find(pn->net, DUMMY_MARK, 0,
						(xfrm_address_t *)&pkt_dev->cur_daddr,
						(xfrm_address_t *)&pkt_dev->cur_saddr,
						AF_INET,
						pkt_dev->ipsmode,
						pkt_dev->ipsproto, 0);
		}
		if (x) {
			pkt_dev->flows[flow].x = x;
			set_pkt_overhead(pkt_dev);
			pkt_dev->pkt_overhead += x->props.header_len;
		}

	}
}
#endif
static void set_cur_queue_map(struct pktgen_dev *pkt_dev)
{

	if (pkt_dev->flags & F_QUEUE_MAP_CPU)
		pkt_dev->cur_queue_map = smp_processor_id();

	else if (pkt_dev->queue_map_min <= pkt_dev->queue_map_max) {
		__u16 t;
		if (pkt_dev->flags & F_QUEUE_MAP_RND) {
			t = prandom_u32() %
				(pkt_dev->queue_map_max -
				 pkt_dev->queue_map_min + 1)
				+ pkt_dev->queue_map_min;
		} else {
			t = pkt_dev->cur_queue_map + 1;
			if (t > pkt_dev->queue_map_max)
				t = pkt_dev->queue_map_min;
		}
		pkt_dev->cur_queue_map = t;
	}
	pkt_dev->cur_queue_map  = pkt_dev->cur_queue_map % pkt_dev->odev->real_num_tx_queues;
}

/* Increment/randomize headers according to flags and current values
 * for IP src/dest, UDP src/dst port, MAC-Addr src/dst
 */
static void mod_cur_headers(struct pktgen_dev *pkt_dev)
{
	__u32 imn;
	__u32 imx;
	int flow = 0;

	if (pkt_dev->cflows)
		flow = f_pick(pkt_dev);

	/*  Deal with source MAC */
	if (pkt_dev->src_mac_count > 1) {
		__u32 mc;
		__u32 tmp;

		if (pkt_dev->flags & F_MACSRC_RND)
			mc = prandom_u32() % pkt_dev->src_mac_count;
		else {
			mc = pkt_dev->cur_src_mac_offset++;
			if (pkt_dev->cur_src_mac_offset >=
			    pkt_dev->src_mac_count)
				pkt_dev->cur_src_mac_offset = 0;
		}

		tmp = pkt_dev->src_mac[5] + (mc & 0xFF);
		pkt_dev->hh[11] = tmp;
		tmp = (pkt_dev->src_mac[4] + ((mc >> 8) & 0xFF) + (tmp >> 8));
		pkt_dev->hh[10] = tmp;
		tmp = (pkt_dev->src_mac[3] + ((mc >> 16) & 0xFF) + (tmp >> 8));
		pkt_dev->hh[9] = tmp;
		tmp = (pkt_dev->src_mac[2] + ((mc >> 24) & 0xFF) + (tmp >> 8));
		pkt_dev->hh[8] = tmp;
		tmp = (pkt_dev->src_mac[1] + (tmp >> 8));
		pkt_dev->hh[7] = tmp;
	}

	/*  Deal with Destination MAC */
	if (pkt_dev->dst_mac_count > 1) {
		__u32 mc;
		__u32 tmp;

		if (pkt_dev->flags & F_MACDST_RND)
			mc = prandom_u32() % pkt_dev->dst_mac_count;

		else {
			mc = pkt_dev->cur_dst_mac_offset++;
			if (pkt_dev->cur_dst_mac_offset >=
			    pkt_dev->dst_mac_count) {
				pkt_dev->cur_dst_mac_offset = 0;
			}
		}

		tmp = pkt_dev->dst_mac[5] + (mc & 0xFF);
		pkt_dev->hh[5] = tmp;
		tmp = (pkt_dev->dst_mac[4] + ((mc >> 8) & 0xFF) + (tmp >> 8));
		pkt_dev->hh[4] = tmp;
		tmp = (pkt_dev->dst_mac[3] + ((mc >> 16) & 0xFF) + (tmp >> 8));
		pkt_dev->hh[3] = tmp;
		tmp = (pkt_dev->dst_mac[2] + ((mc >> 24) & 0xFF) + (tmp >> 8));
		pkt_dev->hh[2] = tmp;
		tmp = (pkt_dev->dst_mac[1] + (tmp >> 8));
		pkt_dev->hh[1] = tmp;
	}

	if (pkt_dev->flags & F_MPLS_RND) {
		unsigned int i;
		for (i = 0; i < pkt_dev->nr_labels; i++)
			if (pkt_dev->labels[i] & MPLS_STACK_BOTTOM)
				pkt_dev->labels[i] = MPLS_STACK_BOTTOM |
					     ((__force __be32)prandom_u32() &
						      htonl(0x000fffff));
	}

	if ((pkt_dev->flags & F_VID_RND) && (pkt_dev->vlan_id != 0xffff)) {
		pkt_dev->vlan_id = prandom_u32() & (4096 - 1);
	}

	if ((pkt_dev->flags & F_SVID_RND) && (pkt_dev->svlan_id != 0xffff)) {
		pkt_dev->svlan_id = prandom_u32() & (4096 - 1);
	}

	if (pkt_dev->udp_src_min < pkt_dev->udp_src_max) {
		if (pkt_dev->flags & F_UDPSRC_RND)
			pkt_dev->cur_udp_src = prandom_u32() %
				(pkt_dev->udp_src_max - pkt_dev->udp_src_min)
				+ pkt_dev->udp_src_min;

		else {
			pkt_dev->cur_udp_src++;
			if (pkt_dev->cur_udp_src >= pkt_dev->udp_src_max)
				pkt_dev->cur_udp_src = pkt_dev->udp_src_min;
		}
	}

	if (pkt_dev->udp_dst_min < pkt_dev->udp_dst_max) {
		if (pkt_dev->flags & F_UDPDST_RND) {
			pkt_dev->cur_udp_dst = prandom_u32() %
				(pkt_dev->udp_dst_max - pkt_dev->udp_dst_min)
				+ pkt_dev->udp_dst_min;
		} else {
			pkt_dev->cur_udp_dst++;
			if (pkt_dev->cur_udp_dst >= pkt_dev->udp_dst_max)
				pkt_dev->cur_udp_dst = pkt_dev->udp_dst_min;
		}
	}

	if (!(pkt_dev->flags & F_IPV6)) {

		imn = ntohl(pkt_dev->saddr_min);
		imx = ntohl(pkt_dev->saddr_max);
		if (imn < imx) {
			__u32 t;
			if (pkt_dev->flags & F_IPSRC_RND)
				t = prandom_u32() % (imx - imn) + imn;
			else {
				t = ntohl(pkt_dev->cur_saddr);
				t++;
				if (t > imx)
					t = imn;

			}
			pkt_dev->cur_saddr = htonl(t);
		}

		if (pkt_dev->cflows && f_seen(pkt_dev, flow)) {
			pkt_dev->cur_daddr = pkt_dev->flows[flow].cur_daddr;
		} else {
			imn = ntohl(pkt_dev->daddr_min);
			imx = ntohl(pkt_dev->daddr_max);
			if (imn < imx) {
				__u32 t;
				__be32 s;
				if (pkt_dev->flags & F_IPDST_RND) {

					do {
						t = prandom_u32() %
							(imx - imn) + imn;
						s = htonl(t);
					} while (ipv4_is_loopback(s) ||
						ipv4_is_multicast(s) ||
						ipv4_is_lbcast(s) ||
						ipv4_is_zeronet(s) ||
						ipv4_is_local_multicast(s));
					pkt_dev->cur_daddr = s;
				} else {
					t = ntohl(pkt_dev->cur_daddr);
					t++;
					if (t > imx) {
						t = imn;
					}
					pkt_dev->cur_daddr = htonl(t);
				}
			}
			if (pkt_dev->cflows) {
				pkt_dev->flows[flow].flags |= F_INIT;
				pkt_dev->flows[flow].cur_daddr =
				    pkt_dev->cur_daddr;
#ifdef CONFIG_XFRM
				if (pkt_dev->flags & F_IPSEC)
					get_ipsec_sa(pkt_dev, flow);
#endif
				pkt_dev->nflows++;
			}
		}
	} else {		/* IPV6 * */

		if (!ipv6_addr_any(&pkt_dev->min_in6_daddr)) {
			int i;

			/* Only random destinations yet */

			for (i = 0; i < 4; i++) {
				pkt_dev->cur_in6_daddr.s6_addr32[i] =
				    (((__force __be32)prandom_u32() |
				      pkt_dev->min_in6_daddr.s6_addr32[i]) &
				     pkt_dev->max_in6_daddr.s6_addr32[i]);
			}
		}
	}

	if (pkt_dev->min_pkt_size < pkt_dev->max_pkt_size) {
		__u32 t;
		if (pkt_dev->flags & F_TXSIZE_RND) {
			t = prandom_u32() %
				(pkt_dev->max_pkt_size - pkt_dev->min_pkt_size)
				+ pkt_dev->min_pkt_size;
		} else {
			t = pkt_dev->cur_pkt_size + 1;
			if (t > pkt_dev->max_pkt_size)
				t = pkt_dev->min_pkt_size;
		}
		pkt_dev->cur_pkt_size = t;
	}

	set_cur_queue_map(pkt_dev);

	pkt_dev->flows[flow].count++;
}


#ifdef CONFIG_XFRM
static u32 pktgen_dst_metrics[RTAX_MAX + 1] = {

	[RTAX_HOPLIMIT] = 0x5, /* Set a static hoplimit */
};

static int pktgen_output_ipsec(struct sk_buff *skb, struct pktgen_dev *pkt_dev)
{
	struct xfrm_state *x = pkt_dev->flows[pkt_dev->curfl].x;
	int err = 0;
	struct net *net = dev_net(pkt_dev->odev);

	if (!x)
		return 0;
	/* XXX: we dont support tunnel mode for now until
	 * we resolve the dst issue */
	if ((x->props.mode != XFRM_MODE_TRANSPORT) && (pkt_dev->spi == 0))
		return 0;

	/* But when user specify an valid SPI, transformation
	 * supports both transport/tunnel mode + ESP/AH type.
	 */
	if ((x->props.mode == XFRM_MODE_TUNNEL) && (pkt_dev->spi != 0))
		skb->_skb_refdst = (unsigned long)&pkt_dev->xdst.u.dst | SKB_DST_NOREF;

	rcu_read_lock_bh();
	err = pktgen_xfrm_outer_mode_output(x, skb);
	rcu_read_unlock_bh();
	if (err) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTSTATEMODEERROR);
		goto error;
	}
	err = x->type->output(x, skb);
	if (err) {
		XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTSTATEPROTOERROR);
		goto error;
	}
	spin_lock_bh(&x->lock);
	x->curlft.bytes += skb->len;
	x->curlft.packets++;
	spin_unlock_bh(&x->lock);
error:
	return err;
}

static void free_SAs(struct pktgen_dev *pkt_dev)
{
	if (pkt_dev->cflows) {
		/* let go of the SAs if we have them */
		int i;
		for (i = 0; i < pkt_dev->cflows; i++) {
			struct xfrm_state *x = pkt_dev->flows[i].x;
			if (x) {
				xfrm_state_put(x);
				pkt_dev->flows[i].x = NULL;
			}
		}
	}
}

static int process_ipsec(struct pktgen_dev *pkt_dev,
			      struct sk_buff *skb, __be16 protocol)
{
	if (pkt_dev->flags & F_IPSEC) {
		struct xfrm_state *x = pkt_dev->flows[pkt_dev->curfl].x;
		int nhead = 0;
		if (x) {
			struct ethhdr *eth;
			struct iphdr *iph;
			int ret;

			nhead = x->props.header_len - skb_headroom(skb);
			if (nhead > 0) {
				ret = pskb_expand_head(skb, nhead, 0, GFP_ATOMIC);
				if (ret < 0) {
					pr_err("Error expanding ipsec packet %d\n",
					       ret);
					goto err;
				}
			}

			/* ipsec is not expecting ll header */
			skb_pull(skb, ETH_HLEN);
			ret = pktgen_output_ipsec(skb, pkt_dev);
			if (ret) {
				pr_err("Error creating ipsec packet %d\n", ret);
				goto err;
			}
			/* restore ll */
			eth = skb_push(skb, ETH_HLEN);
			memcpy(eth, pkt_dev->hh, 2 * ETH_ALEN);
			eth->h_proto = protocol;

			/* Update IPv4 header len as well as checksum value */
			iph = ip_hdr(skb);
			iph->tot_len = htons(skb->len - ETH_HLEN);
			ip_send_check(iph);
		}
	}
	return 1;
err:
	kfree_skb(skb);
	return 0;
}
#endif

static void mpls_push(__be32 *mpls, struct pktgen_dev *pkt_dev)
{
	unsigned int i;
	for (i = 0; i < pkt_dev->nr_labels; i++)
		*mpls++ = pkt_dev->labels[i] & ~MPLS_STACK_BOTTOM;

	mpls--;
	*mpls |= MPLS_STACK_BOTTOM;
}

static inline __be16 build_tci(unsigned int id, unsigned int cfi,
			       unsigned int prio)
{
	return htons(id | (cfi << 12) | (prio << 13));
}

static void pktgen_finalize_skb(struct pktgen_dev *pkt_dev, struct sk_buff *skb,
				int datalen)
{
	struct timespec64 timestamp;
	struct pktgen_hdr *pgh;

	pgh = skb_put(skb, sizeof(*pgh));
	datalen -= sizeof(*pgh);

	if (pkt_dev->nfrags <= 0) {
		skb_put_zero(skb, datalen);
	} else {
		int frags = pkt_dev->nfrags;
		int i, len;
		int frag_len;


		if (frags > MAX_SKB_FRAGS)
			frags = MAX_SKB_FRAGS;
		len = datalen - frags * PAGE_SIZE;
		if (len > 0) {
			skb_put_zero(skb, len);
			datalen = frags * PAGE_SIZE;
		}

		i = 0;
		frag_len = (datalen/frags) < PAGE_SIZE ?
			   (datalen/frags) : PAGE_SIZE;
		while (datalen > 0) {
			if (unlikely(!pkt_dev->page)) {
				int node = numa_node_id();

				if (pkt_dev->node >= 0 && (pkt_dev->flags & F_NODE))
					node = pkt_dev->node;
				pkt_dev->page = alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, 0);
				if (!pkt_dev->page)
					break;
			}
			get_page(pkt_dev->page);
			skb_frag_set_page(skb, i, pkt_dev->page);
			skb_frag_off_set(&skb_shinfo(skb)->frags[i], 0);
			/*last fragment, fill rest of data*/
			if (i == (frags - 1))
				skb_frag_size_set(&skb_shinfo(skb)->frags[i],
				    (datalen < PAGE_SIZE ? datalen : PAGE_SIZE));
			else
				skb_frag_size_set(&skb_shinfo(skb)->frags[i], frag_len);
			datalen -= skb_frag_size(&skb_shinfo(skb)->frags[i]);
			skb->len += skb_frag_size(&skb_shinfo(skb)->frags[i]);
			skb->data_len += skb_frag_size(&skb_shinfo(skb)->frags[i]);
			i++;
			skb_shinfo(skb)->nr_frags = i;
		}
	}

	/* Stamp the time, and sequence number,
	 * convert them to network byte order
	 */
	pgh->pgh_magic = htonl(PKTGEN_MAGIC);
	pgh->seq_num = htonl(pkt_dev->seq_num);

	if (pkt_dev->flags & F_NO_TIMESTAMP) {
		pgh->tv_sec = 0;
		pgh->tv_usec = 0;
	} else {
		/*
		 * pgh->tv_sec wraps in y2106 when interpreted as unsigned
		 * as done by wireshark, or y2038 when interpreted as signed.
		 * This is probably harmless, but if anyone wants to improve
		 * it, we could introduce a variant that puts 64-bit nanoseconds
		 * into the respective header bytes.
		 * This would also be slightly faster to read.
		 */
		ktime_get_real_ts64(&timestamp);
		pgh->tv_sec = htonl(timestamp.tv_sec);
		pgh->tv_usec = htonl(timestamp.tv_nsec / NSEC_PER_USEC);
	}
}

static struct sk_buff *pktgen_alloc_skb(struct net_device *dev,
					struct pktgen_dev *pkt_dev)
{
	unsigned int extralen = LL_RESERVED_SPACE(dev);
	struct sk_buff *skb = NULL;
	unsigned int size;

	size = pkt_dev->cur_pkt_size + 64 + extralen + pkt_dev->pkt_overhead;
	if (pkt_dev->flags & F_NODE) {
		int node = pkt_dev->node >= 0 ? pkt_dev->node : numa_node_id();

		skb = __alloc_skb(NET_SKB_PAD + size, GFP_NOWAIT, 0, node);
		if (likely(skb)) {
			skb_reserve(skb, NET_SKB_PAD);
			skb->dev = dev;
		}
	} else {
		 skb = __netdev_alloc_skb(dev, size, GFP_NOWAIT);
	}

	/* the caller pre-fetches from skb->data and reserves for the mac hdr */
	if (likely(skb))
		skb_reserve(skb, extralen - 16);

	return skb;
}

static struct sk_buff *fill_packet_ipv4(struct net_device *odev,
					struct pktgen_dev *pkt_dev)
{
	struct sk_buff *skb = NULL;
	__u8 *eth;
	struct udphdr *udph;
	int datalen, iplen;
	struct iphdr *iph;
	__be16 protocol = htons(ETH_P_IP);
	__be32 *mpls;
	__be16 *vlan_tci = NULL;                 /* Encapsulates priority and VLAN ID */
	__be16 *vlan_encapsulated_proto = NULL;  /* packet type ID field (or len) for VLAN tag */
	__be16 *svlan_tci = NULL;                /* Encapsulates priority and SVLAN ID */
	__be16 *svlan_encapsulated_proto = NULL; /* packet type ID field (or len) for SVLAN tag */
	u16 queue_map;

	if (pkt_dev->nr_labels)
		protocol = htons(ETH_P_MPLS_UC);

	if (pkt_dev->vlan_id != 0xffff)
		protocol = htons(ETH_P_8021Q);

	/* Update any of the values, used when we're incrementing various
	 * fields.
	 */
	mod_cur_headers(pkt_dev);
	queue_map = pkt_dev->cur_queue_map;

	skb = pktgen_alloc_skb(odev, pkt_dev);
	if (!skb) {
		sprintf(pkt_dev->result, "No memory");
		return NULL;
	}

	prefetchw(skb->data);
	skb_reserve(skb, 16);

	/*  Reserve for ethernet and IP header  */
	eth = skb_push(skb, 14);
	mpls = skb_put(skb, pkt_dev->nr_labels * sizeof(__u32));
	if (pkt_dev->nr_labels)
		mpls_push(mpls, pkt_dev);

	if (pkt_dev->vlan_id != 0xffff) {
		if (pkt_dev->svlan_id != 0xffff) {
			svlan_tci = skb_put(skb, sizeof(__be16));
			*svlan_tci = build_tci(pkt_dev->svlan_id,
					       pkt_dev->svlan_cfi,
					       pkt_dev->svlan_p);
			svlan_encapsulated_proto = skb_put(skb,
							   sizeof(__be16));
			*svlan_encapsulated_proto = htons(ETH_P_8021Q);
		}
		vlan_tci = skb_put(skb, sizeof(__be16));
		*vlan_tci = build_tci(pkt_dev->vlan_id,
				      pkt_dev->vlan_cfi,
				      pkt_dev->vlan_p);
		vlan_encapsulated_proto = skb_put(skb, sizeof(__be16));
		*vlan_encapsulated_proto = htons(ETH_P_IP);
	}

	skb_reset_mac_header(skb);
	skb_set_network_header(skb, skb->len);
	iph = skb_put(skb, sizeof(struct iphdr));

	skb_set_transport_header(skb, skb->len);
	udph = skb_put(skb, sizeof(struct udphdr));
	skb_set_queue_mapping(skb, queue_map);
	skb->priority = pkt_dev->skb_priority;

	memcpy(eth, pkt_dev->hh, 12);
	*(__be16 *) & eth[12] = protocol;

	/* Eth + IPh + UDPh + mpls */
	datalen = pkt_dev->cur_pkt_size - 14 - 20 - 8 -
		  pkt_dev->pkt_overhead;
	if (datalen < 0 || datalen < sizeof(struct pktgen_hdr))
		datalen = sizeof(struct pktgen_hdr);

	udph->source = htons(pkt_dev->cur_udp_src);
	udph->dest = htons(pkt_dev->cur_udp_dst);
	udph->len = htons(datalen + 8);	/* DATA + udphdr */
	udph->check = 0;

	iph->ihl = 5;
	iph->version = 4;
	iph->ttl = 32;
	iph->tos = pkt_dev->tos;
	iph->protocol = IPPROTO_UDP;	/* UDP */
	iph->saddr = pkt_dev->cur_saddr;
	iph->daddr = pkt_dev->cur_daddr;
	iph->id = htons(pkt_dev->ip_id);
	pkt_dev->ip_id++;
	iph->frag_off = 0;
	iplen = 20 + 8 + datalen;
	iph->tot_len = htons(iplen);
	ip_send_check(iph);
	skb->protocol = protocol;
	skb->dev = odev;
	skb->pkt_type = PACKET_HOST;

	pktgen_finalize_skb(pkt_dev, skb, datalen);

	if (!(pkt_dev->flags & F_UDPCSUM)) {
		skb->ip_summed = CHECKSUM_NONE;
	} else if (odev->features & (NETIF_F_HW_CSUM | NETIF_F_IP_CSUM)) {
		skb->ip_summed = CHECKSUM_PARTIAL;
		skb->csum = 0;
		udp4_hwcsum(skb, iph->saddr, iph->daddr);
	} else {
		__wsum csum = skb_checksum(skb, skb_transport_offset(skb), datalen + 8, 0);

		/* add protocol-dependent pseudo-header */
		udph->check = csum_tcpudp_magic(iph->saddr, iph->daddr,
						datalen + 8, IPPROTO_UDP, csum);

		if (udph->check == 0)
			udph->check = CSUM_MANGLED_0;
	}

#ifdef CONFIG_XFRM
	if (!process_ipsec(pkt_dev, skb, protocol))
		return NULL;
#endif

	return skb;
}

static struct sk_buff *fill_packet_ipv6(struct net_device *odev,
					struct pktgen_dev *pkt_dev)
{
	struct sk_buff *skb = NULL;
	__u8 *eth;
	struct udphdr *udph;
	int datalen, udplen;
	struct ipv6hdr *iph;
	__be16 protocol = htons(ETH_P_IPV6);
	__be32 *mpls;
	__be16 *vlan_tci = NULL;                 /* Encapsulates priority and VLAN ID */
	__be16 *vlan_encapsulated_proto = NULL;  /* packet type ID field (or len) for VLAN tag */
	__be16 *svlan_tci = NULL;                /* Encapsulates priority and SVLAN ID */
	__be16 *svlan_encapsulated_proto = NULL; /* packet type ID field (or len) for SVLAN tag */
	u16 queue_map;

	if (pkt_dev->nr_labels)
		protocol = htons(ETH_P_MPLS_UC);

	if (pkt_dev->vlan_id != 0xffff)
		protocol = htons(ETH_P_8021Q);

	/* Update any of the values, used when we're incrementing various
	 * fields.
	 */
	mod_cur_headers(pkt_dev);
	queue_map = pkt_dev->cur_queue_map;

	skb = pktgen_alloc_skb(odev, pkt_dev);
	if (!skb) {
		sprintf(pkt_dev->result, "No memory");
		return NULL;
	}

	prefetchw(skb->data);
	skb_reserve(skb, 16);

	/*  Reserve for ethernet and IP header  */
	eth = skb_push(skb, 14);
	mpls = skb_put(skb, pkt_dev->nr_labels * sizeof(__u32));
	if (pkt_dev->nr_labels)
		mpls_push(mpls, pkt_dev);

	if (pkt_dev->vlan_id != 0xffff) {
		if (pkt_dev->svlan_id != 0xffff) {
			svlan_tci = skb_put(skb, sizeof(__be16));
			*svlan_tci = build_tci(pkt_dev->svlan_id,
					       pkt_dev->svlan_cfi,
					       pkt_dev->svlan_p);
			svlan_encapsulated_proto = skb_put(skb,
							   sizeof(__be16));
			*svlan_encapsulated_proto = htons(ETH_P_8021Q);
		}
		vlan_tci = skb_put(skb, sizeof(__be16));
		*vlan_tci = build_tci(pkt_dev->vlan_id,
				      pkt_dev->vlan_cfi,
				      pkt_dev->vlan_p);
		vlan_encapsulated_proto = skb_put(skb, sizeof(__be16));
		*vlan_encapsulated_proto = htons(ETH_P_IPV6);
	}

	skb_reset_mac_header(skb);
	skb_set_network_header(skb, skb->len);
	iph = skb_put(skb, sizeof(struct ipv6hdr));

	skb_set_transport_header(skb, skb->len);
	udph = skb_put(skb, sizeof(struct udphdr));
	skb_set_queue_mapping(skb, queue_map);
	skb->priority = pkt_dev->skb_priority;

	memcpy(eth, pkt_dev->hh, 12);
	*(__be16 *) &eth[12] = protocol;

	/* Eth + IPh + UDPh + mpls */
	datalen = pkt_dev->cur_pkt_size - 14 -
		  sizeof(struct ipv6hdr) - sizeof(struct udphdr) -
		  pkt_dev->pkt_overhead;

	if (datalen < 0 || datalen < sizeof(struct pktgen_hdr)) {
		datalen = sizeof(struct pktgen_hdr);
		net_info_ratelimited("increased datalen to %d\n", datalen);
	}

	udplen = datalen + sizeof(struct udphdr);
	udph->source = htons(pkt_dev->cur_udp_src);
	udph->dest = htons(pkt_dev->cur_udp_dst);
	udph->len = htons(udplen);
	udph->check = 0;

	*(__be32 *) iph = htonl(0x60000000);	/* Version + flow */

	if (pkt_dev->traffic_class) {
		/* Version + traffic class + flow (0) */
		*(__be32 *)iph |= htonl(0x60000000 | (pkt_dev->traffic_class << 20));
	}

	iph->hop_limit = 32;

	iph->payload_len = htons(udplen);
	iph->nexthdr = IPPROTO_UDP;

	iph->daddr = pkt_dev->cur_in6_daddr;
	iph->saddr = pkt_dev->cur_in6_saddr;

	skb->protocol = protocol;
	skb->dev = odev;
	skb->pkt_type = PACKET_HOST;

	pktgen_finalize_skb(pkt_dev, skb, datalen);

	if (!(pkt_dev->flags & F_UDPCSUM)) {
		skb->ip_summed = CHECKSUM_NONE;
	} else if (odev->features & (NETIF_F_HW_CSUM | NETIF_F_IPV6_CSUM)) {
		skb->ip_summed = CHECKSUM_PARTIAL;
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct udphdr, check);
		udph->check = ~csum_ipv6_magic(&iph->saddr, &iph->daddr, udplen, IPPROTO_UDP, 0);
	} else {
		__wsum csum = skb_checksum(skb, skb_transport_offset(skb), udplen, 0);

		/* add protocol-dependent pseudo-header */
		udph->check = csum_ipv6_magic(&iph->saddr, &iph->daddr, udplen, IPPROTO_UDP, csum);

		if (udph->check == 0)
			udph->check = CSUM_MANGLED_0;
	}

	return skb;
}

static struct sk_buff *fill_packet(struct net_device *odev,
				   struct pktgen_dev *pkt_dev)
{
	if (pkt_dev->flags & F_IPV6)
		return fill_packet_ipv6(odev, pkt_dev);
	else
		return fill_packet_ipv4(odev, pkt_dev);
}

static void pktgen_clear_counters(struct pktgen_dev *pkt_dev)
{
	pkt_dev->seq_num = 1;
	pkt_dev->idle_acc = 0;
	pkt_dev->sofar = 0;
	pkt_dev->tx_bytes = 0;
	pkt_dev->errors = 0;
}

/* Set up structure for sending pkts, clear counters */

static void pktgen_run(struct pktgen_thread *t)
{
	struct pktgen_dev *pkt_dev;
	int started = 0;

	func_enter();

	rcu_read_lock();
	list_for_each_entry_rcu(pkt_dev, &t->if_list, list) {

		/*
		 * setup odev and create initial packet.
		 */
		pktgen_setup_inject(pkt_dev);

		if (pkt_dev->odev) {
			pktgen_clear_counters(pkt_dev);
			pkt_dev->skb = NULL;
			pkt_dev->started_at = pkt_dev->next_tx = ktime_get();

			set_pkt_overhead(pkt_dev);

			strcpy(pkt_dev->result, "Starting");
			pkt_dev->running = 1;	/* Cranke yeself! */
			started++;
		} else
			strcpy(pkt_dev->result, "Error starting");
	}
	rcu_read_unlock();
	if (started)
		t->control &= ~(T_STOP);
}

static void pktgen_stop_all_threads_ifs(struct pktgen_net *pn)
{
	struct pktgen_thread *t;

	func_enter();

	mutex_lock(&pktgen_thread_lock);

	list_for_each_entry(t, &pn->pktgen_threads, th_list)
		t->control |= T_STOP;

	mutex_unlock(&pktgen_thread_lock);
}

static int thread_is_running(const struct pktgen_thread *t)
{
	const struct pktgen_dev *pkt_dev;

	rcu_read_lock();
	list_for_each_entry_rcu(pkt_dev, &t->if_list, list)
		if (pkt_dev->running) {
			rcu_read_unlock();
			return 1;
		}
	rcu_read_unlock();
	return 0;
}

static int pktgen_wait_thread_run(struct pktgen_thread *t)
{
	while (thread_is_running(t)) {

		/* note: 't' will still be around even after the unlock/lock
		 * cycle because pktgen_thread threads are only cleared at
		 * net exit
		 */
		mutex_unlock(&pktgen_thread_lock);
		msleep_interruptible(100);
		mutex_lock(&pktgen_thread_lock);

		if (signal_pending(current))
			goto signal;
	}
	return 1;
signal:
	return 0;
}

static int pktgen_wait_all_threads_run(struct pktgen_net *pn)
{
	struct pktgen_thread *t;
	int sig = 1;

	/* prevent from racing with rmmod */
	if (!try_module_get(THIS_MODULE))
		return sig;

	mutex_lock(&pktgen_thread_lock);

	list_for_each_entry(t, &pn->pktgen_threads, th_list) {
		sig = pktgen_wait_thread_run(t);
		if (sig == 0)
			break;
	}

	if (sig == 0)
		list_for_each_entry(t, &pn->pktgen_threads, th_list)
			t->control |= (T_STOP);

	mutex_unlock(&pktgen_thread_lock);
	module_put(THIS_MODULE);
	return sig;
}

static void pktgen_run_all_threads(struct pktgen_net *pn)
{
	struct pktgen_thread *t;

	func_enter();

	mutex_lock(&pktgen_thread_lock);

	list_for_each_entry(t, &pn->pktgen_threads, th_list)
		t->control |= (T_RUN);

	mutex_unlock(&pktgen_thread_lock);

	/* Propagate thread->control  */
	schedule_timeout_interruptible(msecs_to_jiffies(125));

	pktgen_wait_all_threads_run(pn);
}

static void pktgen_reset_all_threads(struct pktgen_net *pn)
{
	struct pktgen_thread *t;

	func_enter();

	mutex_lock(&pktgen_thread_lock);

	list_for_each_entry(t, &pn->pktgen_threads, th_list)
		t->control |= (T_REMDEVALL);

	mutex_unlock(&pktgen_thread_lock);

	/* Propagate thread->control  */
	schedule_timeout_interruptible(msecs_to_jiffies(125));

	pktgen_wait_all_threads_run(pn);
}

static void show_results(struct pktgen_dev *pkt_dev, int nr_frags)
{
	__u64 bps, mbps, pps;
	char *p = pkt_dev->result;
	ktime_t elapsed = ktime_sub(pkt_dev->stopped_at,
				    pkt_dev->started_at);
	ktime_t idle = ns_to_ktime(pkt_dev->idle_acc);

	p += sprintf(p, "OK: %llu(c%llu+d%llu) usec, %llu (%dbyte,%dfrags)\n",
		     (unsigned long long)ktime_to_us(elapsed),
		     (unsigned long long)ktime_to_us(ktime_sub(elapsed, idle)),
		     (unsigned long long)ktime_to_us(idle),
		     (unsigned long long)pkt_dev->sofar,
		     pkt_dev->cur_pkt_size, nr_frags);

	pps = div64_u64(pkt_dev->sofar * NSEC_PER_SEC,
			ktime_to_ns(elapsed));

	bps = pps * 8 * pkt_dev->cur_pkt_size;

	mbps = bps;
	do_div(mbps, 1000000);
	p += sprintf(p, "  %llupps %lluMb/sec (%llubps) errors: %llu",
		     (unsigned long long)pps,
		     (unsigned long long)mbps,
		     (unsigned long long)bps,
		     (unsigned long long)pkt_dev->errors);
}

/* Set stopped-at timer, remove from running list, do counters & statistics */
static int pktgen_stop_device(struct pktgen_dev *pkt_dev)
{
	int nr_frags = pkt_dev->skb ? skb_shinfo(pkt_dev->skb)->nr_frags : -1;

	if (!pkt_dev->running) {
		pr_warn("interface: %s is already stopped\n",
			pkt_dev->odevname);
		return -EINVAL;
	}

	pkt_dev->running = 0;
	kfree_skb(pkt_dev->skb);
	pkt_dev->skb = NULL;
	pkt_dev->stopped_at = ktime_get();

	show_results(pkt_dev, nr_frags);

	return 0;
}

static struct pktgen_dev *next_to_run(struct pktgen_thread *t)
{
	struct pktgen_dev *pkt_dev, *best = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(pkt_dev, &t->if_list, list) {
		if (!pkt_dev->running)
			continue;
		if (best == NULL)
			best = pkt_dev;
		else if (ktime_compare(pkt_dev->next_tx, best->next_tx) < 0)
			best = pkt_dev;
	}
	rcu_read_unlock();

	return best;
}

static void pktgen_stop(struct pktgen_thread *t)
{
	struct pktgen_dev *pkt_dev;

	func_enter();

	rcu_read_lock();

	list_for_each_entry_rcu(pkt_dev, &t->if_list, list) {
		pktgen_stop_device(pkt_dev);
	}

	rcu_read_unlock();
}

/*
 * one of our devices needs to be removed - find it
 * and remove it
 */
static void pktgen_rem_one_if(struct pktgen_thread *t)
{
	struct list_head *q, *n;
	struct pktgen_dev *cur;

	func_enter();

	list_for_each_safe(q, n, &t->if_list) {
		cur = list_entry(q, struct pktgen_dev, list);

		if (!cur->removal_mark)
			continue;

		kfree_skb(cur->skb);
		cur->skb = NULL;

		pktgen_remove_device(t, cur);

		break;
	}
}

static void pktgen_rem_all_ifs(struct pktgen_thread *t)
{
	struct list_head *q, *n;
	struct pktgen_dev *cur;

	func_enter();

	/* Remove all devices, free mem */

	list_for_each_safe(q, n, &t->if_list) {
		cur = list_entry(q, struct pktgen_dev, list);

		kfree_skb(cur->skb);
		cur->skb = NULL;

		pktgen_remove_device(t, cur);
	}
}

static void pktgen_rem_thread(struct pktgen_thread *t)
{
	/* Remove from the thread list */
	remove_proc_entry(t->tsk->comm, t->net->proc_dir);
}

static void pktgen_resched(struct pktgen_dev *pkt_dev)
{
	ktime_t idle_start = ktime_get();
	schedule();
	pkt_dev->idle_acc += ktime_to_ns(ktime_sub(ktime_get(), idle_start));
}

static void pktgen_wait_for_skb(struct pktgen_dev *pkt_dev)
{
	ktime_t idle_start = ktime_get();

	while (refcount_read(&(pkt_dev->skb->users)) != 1) {
		if (signal_pending(current))
			break;

		if (need_resched())
			pktgen_resched(pkt_dev);
		else
			cpu_relax();
	}
	pkt_dev->idle_acc += ktime_to_ns(ktime_sub(ktime_get(), idle_start));
}

static void pktgen_xmit(struct pktgen_dev *pkt_dev)
{
	unsigned int burst = READ_ONCE(pkt_dev->burst);
	struct net_device *odev = pkt_dev->odev;
	struct netdev_queue *txq;
	struct sk_buff *skb;
	int ret;

	/* If device is offline, then don't send */
	if (unlikely(!netif_running(odev) || !netif_carrier_ok(odev))) {
		pktgen_stop_device(pkt_dev);
		return;
	}

	/* This is max DELAY, this has special meaning of
	 * "never transmit"
	 */
	if (unlikely(pkt_dev->delay == ULLONG_MAX)) {
		pkt_dev->next_tx = ktime_add_ns(ktime_get(), ULONG_MAX);
		return;
	}

	/* If no skb or clone count exhausted then get new one */
	if (!pkt_dev->skb || (pkt_dev->last_ok &&
			      ++pkt_dev->clone_count >= pkt_dev->clone_skb)) {
		/* build a new pkt */
		kfree_skb(pkt_dev->skb);

		pkt_dev->skb = fill_packet(odev, pkt_dev);
		if (pkt_dev->skb == NULL) {
			pr_err("ERROR: couldn't allocate skb in fill_packet\n");
			schedule();
			pkt_dev->clone_count--;	/* back out increment, OOM */
			return;
		}
		pkt_dev->last_pkt_size = pkt_dev->skb->len;
		pkt_dev->clone_count = 0;	/* reset counter */
	}

	if (pkt_dev->delay && pkt_dev->last_ok)
		spin(pkt_dev, pkt_dev->next_tx);

	if (pkt_dev->xmit_mode == M_NETIF_RECEIVE) {
		skb = pkt_dev->skb;
		skb->protocol = eth_type_trans(skb, skb->dev);
		refcount_add(burst, &skb->users);
		local_bh_disable();
		do {
			ret = netif_receive_skb(skb);
			if (ret == NET_RX_DROP)
				pkt_dev->errors++;
			pkt_dev->sofar++;
			pkt_dev->seq_num++;
			if (refcount_read(&skb->users) != burst) {
				/* skb was queued by rps/rfs or taps,
				 * so cannot reuse this skb
				 */
				WARN_ON(refcount_sub_and_test(burst - 1, &skb->users));
				/* get out of the loop and wait
				 * until skb is consumed
				 */
				break;
			}
			/* skb was 'freed' by stack, so clean few
			 * bits and reuse it
			 */
			skb_reset_tc(skb);
		} while (--burst > 0);
		goto out; /* Skips xmit_mode M_START_XMIT */
	} else if (pkt_dev->xmit_mode == M_QUEUE_XMIT) {
		local_bh_disable();
		refcount_inc(&pkt_dev->skb->users);

		ret = dev_queue_xmit(pkt_dev->skb);
		switch (ret) {
		case NET_XMIT_SUCCESS:
			pkt_dev->sofar++;
			pkt_dev->seq_num++;
			pkt_dev->tx_bytes += pkt_dev->last_pkt_size;
			break;
		case NET_XMIT_DROP:
		case NET_XMIT_CN:
		/* These are all valid return codes for a qdisc but
		 * indicate packets are being dropped or will likely
		 * be dropped soon.
		 */
		case NETDEV_TX_BUSY:
		/* qdisc may call dev_hard_start_xmit directly in cases
		 * where no queues exist e.g. loopback device, virtual
		 * devices, etc. In this case we need to handle
		 * NETDEV_TX_ codes.
		 */
		default:
			pkt_dev->errors++;
			net_info_ratelimited("%s xmit error: %d\n",
					     pkt_dev->odevname, ret);
			break;
		}
		goto out;
	}

	txq = skb_get_tx_queue(odev, pkt_dev->skb);

	local_bh_disable();

	HARD_TX_LOCK(odev, txq, smp_processor_id());

	if (unlikely(netif_xmit_frozen_or_drv_stopped(txq))) {
		ret = NETDEV_TX_BUSY;
		pkt_dev->last_ok = 0;
		goto unlock;
	}
	refcount_add(burst, &pkt_dev->skb->users);

xmit_more:
	ret = netdev_start_xmit(pkt_dev->skb, odev, txq, --burst > 0);

	switch (ret) {
	case NETDEV_TX_OK:
		pkt_dev->last_ok = 1;
		pkt_dev->sofar++;
		pkt_dev->seq_num++;
		pkt_dev->tx_bytes += pkt_dev->last_pkt_size;
		if (burst > 0 && !netif_xmit_frozen_or_drv_stopped(txq))
			goto xmit_more;
		break;
	case NET_XMIT_DROP:
	case NET_XMIT_CN:
		/* skb has been consumed */
		pkt_dev->errors++;
		break;
	default: /* Drivers are not supposed to return other values! */
		net_info_ratelimited("%s xmit error: %d\n",
				     pkt_dev->odevname, ret);
		pkt_dev->errors++;
		/* fall through */
	case NETDEV_TX_BUSY:
		/* Retry it next time */
		refcount_dec(&(pkt_dev->skb->users));
		pkt_dev->last_ok = 0;
	}
	if (unlikely(burst))
		WARN_ON(refcount_sub_and_test(burst, &pkt_dev->skb->users));
unlock:
	HARD_TX_UNLOCK(odev, txq);

out:
	local_bh_enable();

	/* If pkt_dev->count is zero, then run forever */
	if ((pkt_dev->count != 0) && (pkt_dev->sofar >= pkt_dev->count)) {
		pktgen_wait_for_skb(pkt_dev);

		/* Done with this */
		pktgen_stop_device(pkt_dev);
	}
}

/*
 * Main loop of the thread goes here
 */

static int pktgen_thread_worker(void *arg)
{
	DEFINE_WAIT(wait);
	struct pktgen_thread *t = arg;
	struct pktgen_dev *pkt_dev = NULL;
	int cpu = t->cpu;

	BUG_ON(smp_processor_id() != cpu);

	init_waitqueue_head(&t->queue);
	complete(&t->start_done);

	pr_debug("starting pktgen/%d:  pid=%d\n", cpu, task_pid_nr(current));

	set_freezable();

	while (!kthread_should_stop()) {
		pkt_dev = next_to_run(t);

		if (unlikely(!pkt_dev && t->control == 0)) {
			if (t->net->pktgen_exiting)
				break;
			wait_event_interruptible_timeout(t->queue,
							 t->control != 0,
							 HZ/10);
			try_to_freeze();
			continue;
		}

		if (likely(pkt_dev)) {
			pktgen_xmit(pkt_dev);

			if (need_resched())
				pktgen_resched(pkt_dev);
			else
				cpu_relax();
		}

		if (t->control & T_STOP) {
			pktgen_stop(t);
			t->control &= ~(T_STOP);
		}

		if (t->control & T_RUN) {
			pktgen_run(t);
			t->control &= ~(T_RUN);
		}

		if (t->control & T_REMDEVALL) {
			pktgen_rem_all_ifs(t);
			t->control &= ~(T_REMDEVALL);
		}

		if (t->control & T_REMDEV) {
			pktgen_rem_one_if(t);
			t->control &= ~(T_REMDEV);
		}

		try_to_freeze();
	}

	pr_debug("%s stopping all device\n", t->tsk->comm);
	pktgen_stop(t);

	pr_debug("%s removing all device\n", t->tsk->comm);
	pktgen_rem_all_ifs(t);

	pr_debug("%s removing thread\n", t->tsk->comm);
	pktgen_rem_thread(t);

	return 0;
}

static struct pktgen_dev *pktgen_find_dev(struct pktgen_thread *t,
					  const char *ifname, bool exact)
{
	struct pktgen_dev *p, *pkt_dev = NULL;
	size_t len = strlen(ifname);

	rcu_read_lock();
	list_for_each_entry_rcu(p, &t->if_list, list)
		if (strncmp(p->odevname, ifname, len) == 0) {
			if (p->odevname[len]) {
				if (exact || p->odevname[len] != '@')
					continue;
			}
			pkt_dev = p;
			break;
		}

	rcu_read_unlock();
	pr_debug("find_dev(%s) returning %p\n", ifname, pkt_dev);
	return pkt_dev;
}

/*
 * Adds a dev at front of if_list.
 */

static int add_dev_to_thread(struct pktgen_thread *t,
			     struct pktgen_dev *pkt_dev)
{
	int rv = 0;

	/* This function cannot be called concurrently, as its called
	 * under pktgen_thread_lock mutex, but it can run from
	 * userspace on another CPU than the kthread.  The if_lock()
	 * is used here to sync with concurrent instances of
	 * _rem_dev_from_if_list() invoked via kthread, which is also
	 * updating the if_list */
	if_lock(t);

	if (pkt_dev->pg_thread) {
		pr_err("ERROR: already assigned to a thread\n");
		rv = -EBUSY;
		goto out;
	}

	pkt_dev->running = 0;
	pkt_dev->pg_thread = t;
	list_add_rcu(&pkt_dev->list, &t->if_list);

out:
	if_unlock(t);
	return rv;
}

/* Called under thread lock */

static int pktgen_add_device(struct pktgen_thread *t, const char *ifname)
{
	struct pktgen_dev *pkt_dev;
	int err;
	int node = cpu_to_node(t->cpu);

	/* We don't allow a device to be on several threads */

	pkt_dev = __pktgen_NN_threads(t->net, ifname, FIND);
	if (pkt_dev) {
		pr_err("ERROR: interface already used\n");
		return -EBUSY;
	}

	pkt_dev = kzalloc_node(sizeof(struct pktgen_dev), GFP_KERNEL, node);
	if (!pkt_dev)
		return -ENOMEM;

	strcpy(pkt_dev->odevname, ifname);
	pkt_dev->flows = vzalloc_node(array_size(MAX_CFLOWS,
						 sizeof(struct flow_state)),
				      node);
	if (pkt_dev->flows == NULL) {
		kfree(pkt_dev);
		return -ENOMEM;
	}

	pkt_dev->removal_mark = 0;
	pkt_dev->nfrags = 0;
	pkt_dev->delay = pg_delay_d;
	pkt_dev->count = pg_count_d;
	pkt_dev->sofar = 0;
	pkt_dev->udp_src_min = 9;	/* sink port */
	pkt_dev->udp_src_max = 9;
	pkt_dev->udp_dst_min = 9;
	pkt_dev->udp_dst_max = 9;
	pkt_dev->vlan_p = 0;
	pkt_dev->vlan_cfi = 0;
	pkt_dev->vlan_id = 0xffff;
	pkt_dev->svlan_p = 0;
	pkt_dev->svlan_cfi = 0;
	pkt_dev->svlan_id = 0xffff;
	pkt_dev->burst = 1;
	pkt_dev->node = NUMA_NO_NODE;

	err = pktgen_setup_dev(t->net, pkt_dev, ifname);
	if (err)
		goto out1;
	if (pkt_dev->odev->priv_flags & IFF_TX_SKB_SHARING)
		pkt_dev->clone_skb = pg_clone_skb_d;

	pkt_dev->entry = proc_create_data(ifname, 0600, t->net->proc_dir,
					  &pktgen_if_fops, pkt_dev);
	if (!pkt_dev->entry) {
		pr_err("cannot create %s/%s procfs entry\n",
		       PG_PROC_DIR, ifname);
		err = -EINVAL;
		goto out2;
	}
#ifdef CONFIG_XFRM
	pkt_dev->ipsmode = XFRM_MODE_TRANSPORT;
	pkt_dev->ipsproto = IPPROTO_ESP;

	/* xfrm tunnel mode needs additional dst to extract outter
	 * ip header protocol/ttl/id field, here creat a phony one.
	 * instead of looking for a valid rt, which definitely hurting
	 * performance under such circumstance.
	 */
	pkt_dev->dstops.family = AF_INET;
	pkt_dev->xdst.u.dst.dev = pkt_dev->odev;
	dst_init_metrics(&pkt_dev->xdst.u.dst, pktgen_dst_metrics, false);
	pkt_dev->xdst.child = &pkt_dev->xdst.u.dst;
	pkt_dev->xdst.u.dst.ops = &pkt_dev->dstops;
#endif

	return add_dev_to_thread(t, pkt_dev);
out2:
	dev_put(pkt_dev->odev);
out1:
#ifdef CONFIG_XFRM
	free_SAs(pkt_dev);
#endif
	vfree(pkt_dev->flows);
	kfree(pkt_dev);
	return err;
}

static int __net_init pktgen_create_thread(int cpu, struct pktgen_net *pn)
{
	struct pktgen_thread *t;
	struct proc_dir_entry *pe;
	struct task_struct *p;

	t = kzalloc_node(sizeof(struct pktgen_thread), GFP_KERNEL,
			 cpu_to_node(cpu));
	if (!t) {
		pr_err("ERROR: out of memory, can't create new thread\n");
		return -ENOMEM;
	}

	mutex_init(&t->if_lock);
	t->cpu = cpu;

	INIT_LIST_HEAD(&t->if_list);

	list_add_tail(&t->th_list, &pn->pktgen_threads);
	init_completion(&t->start_done);

	p = kthread_create_on_node(pktgen_thread_worker,
				   t,
				   cpu_to_node(cpu),
				   "kpktgend_%d", cpu);
	if (IS_ERR(p)) {
		pr_err("kernel_thread() failed for cpu %d\n", t->cpu);
		list_del(&t->th_list);
		kfree(t);
		return PTR_ERR(p);
	}
	kthread_bind(p, cpu);
	t->tsk = p;

	pe = proc_create_data(t->tsk->comm, 0600, pn->proc_dir,
			      &pktgen_thread_fops, t);
	if (!pe) {
		pr_err("cannot create %s/%s procfs entry\n",
		       PG_PROC_DIR, t->tsk->comm);
		kthread_stop(p);
		list_del(&t->th_list);
		kfree(t);
		return -EINVAL;
	}

	t->net = pn;
	get_task_struct(p);
	wake_up_process(p);
	wait_for_completion(&t->start_done);

	return 0;
}

/*
 * Removes a device from the thread if_list.
 */
static void _rem_dev_from_if_list(struct pktgen_thread *t,
				  struct pktgen_dev *pkt_dev)
{
	struct list_head *q, *n;
	struct pktgen_dev *p;

	if_lock(t);
	list_for_each_safe(q, n, &t->if_list) {
		p = list_entry(q, struct pktgen_dev, list);
		if (p == pkt_dev)
			list_del_rcu(&p->list);
	}
	if_unlock(t);
}

static int pktgen_remove_device(struct pktgen_thread *t,
				struct pktgen_dev *pkt_dev)
{
	pr_debug("remove_device pkt_dev=%p\n", pkt_dev);

	if (pkt_dev->running) {
		pr_warn("WARNING: trying to remove a running interface, stopping it now\n");
		pktgen_stop_device(pkt_dev);
	}

	/* Dis-associate from the interface */

	if (pkt_dev->odev) {
		dev_put(pkt_dev->odev);
		pkt_dev->odev = NULL;
	}

	/* Remove proc before if_list entry, because add_device uses
	 * list to determine if interface already exist, avoid race
	 * with proc_create_data() */
	proc_remove(pkt_dev->entry);

	/* And update the thread if_list */
	_rem_dev_from_if_list(t, pkt_dev);

#ifdef CONFIG_XFRM
	free_SAs(pkt_dev);
#endif
	vfree(pkt_dev->flows);
	if (pkt_dev->page)
		put_page(pkt_dev->page);
	kfree_rcu(pkt_dev, rcu);
	return 0;
}

static int __net_init pg_net_init(struct net *net)
{
	struct pktgen_net *pn = net_generic(net, pg_net_id);
	struct proc_dir_entry *pe;
	int cpu, ret = 0;

	pn->net = net;
	INIT_LIST_HEAD(&pn->pktgen_threads);
	pn->pktgen_exiting = false;
	pn->proc_dir = proc_mkdir(PG_PROC_DIR, pn->net->proc_net);
	if (!pn->proc_dir) {
		pr_warn("cannot create /proc/net/%s\n", PG_PROC_DIR);
		return -ENODEV;
	}
	pe = proc_create(PGCTRL, 0600, pn->proc_dir, &pktgen_fops);
	if (pe == NULL) {
		pr_err("cannot create %s procfs entry\n", PGCTRL);
		ret = -EINVAL;
		goto remove;
	}

	for_each_online_cpu(cpu) {
		int err;

		err = pktgen_create_thread(cpu, pn);
		if (err)
			pr_warn("Cannot create thread for cpu %d (%d)\n",
				   cpu, err);
	}

	if (list_empty(&pn->pktgen_threads)) {
		pr_err("Initialization failed for all threads\n");
		ret = -ENODEV;
		goto remove_entry;
	}

	return 0;

remove_entry:
	remove_proc_entry(PGCTRL, pn->proc_dir);
remove:
	remove_proc_entry(PG_PROC_DIR, pn->net->proc_net);
	return ret;
}

static void __net_exit pg_net_exit(struct net *net)
{
	struct pktgen_net *pn = net_generic(net, pg_net_id);
	struct pktgen_thread *t;
	struct list_head *q, *n;
	LIST_HEAD(list);

	/* Stop all interfaces & threads */
	pn->pktgen_exiting = true;

	mutex_lock(&pktgen_thread_lock);
	list_splice_init(&pn->pktgen_threads, &list);
	mutex_unlock(&pktgen_thread_lock);

	list_for_each_safe(q, n, &list) {
		t = list_entry(q, struct pktgen_thread, th_list);
		list_del(&t->th_list);
		kthread_stop(t->tsk);
		put_task_struct(t->tsk);
		kfree(t);
	}

	remove_proc_entry(PGCTRL, pn->proc_dir);
	remove_proc_entry(PG_PROC_DIR, pn->net->proc_net);
}

static struct pernet_operations pg_net_ops = {
	.init = pg_net_init,
	.exit = pg_net_exit,
	.id   = &pg_net_id,
	.size = sizeof(struct pktgen_net),
};

static int __init pg_init(void)
{
	int ret = 0;

	pr_info("%s", version);
	ret = register_pernet_subsys(&pg_net_ops);
	if (ret)
		return ret;
	ret = register_netdevice_notifier(&pktgen_notifier_block);
	if (ret)
		unregister_pernet_subsys(&pg_net_ops);

	return ret;
}

static void __exit pg_cleanup(void)
{
	unregister_netdevice_notifier(&pktgen_notifier_block);
	unregister_pernet_subsys(&pg_net_ops);
	/* Don't need rcu_barrier() due to use of kfree_rcu() */
}

module_init(pg_init);
module_exit(pg_cleanup);

MODULE_AUTHOR("Robert Olsson <robert.olsson@its.uu.se>");
MODULE_DESCRIPTION("Packet Generator tool");
MODULE_LICENSE("GPL");
MODULE_VERSION(VERSION);
module_param(pg_count_d, int, 0);
MODULE_PARM_DESC(pg_count_d, "Default number of packets to inject");
module_param(pg_delay_d, int, 0);
MODULE_PARM_DESC(pg_delay_d, "Default delay between packets (nanoseconds)");
module_param(pg_clone_skb_d, int, 0);
MODULE_PARM_DESC(pg_clone_skb_d, "Default number of copies of the same packet");
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Enable debugging of pktgen module");
