#ifndef _NET_DN_H
#define _NET_DN_H

#include <linux/dn.h>
#include <net/sock.h>
#include <net/flow.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

struct dn_scp                                   /* Session Control Port */
{
        unsigned char           state;
#define DN_O     1                      /* Open                 */
#define DN_CR    2                      /* Connect Receive      */
#define DN_DR    3                      /* Disconnect Reject    */
#define DN_DRC   4                      /* Discon. Rej. Complete*/
#define DN_CC    5                      /* Connect Confirm      */
#define DN_CI    6                      /* Connect Initiate     */
#define DN_NR    7                      /* No resources         */
#define DN_NC    8                      /* No communication     */
#define DN_CD    9                      /* Connect Delivery     */
#define DN_RJ    10                     /* Rejected             */
#define DN_RUN   11                     /* Running              */
#define DN_DI    12                     /* Disconnect Initiate  */
#define DN_DIC   13                     /* Disconnect Complete  */
#define DN_DN    14                     /* Disconnect Notificat */
#define DN_CL    15                     /* Closed               */
#define DN_CN    16                     /* Closed Notification  */

        __le16          addrloc;
        __le16          addrrem;
        __u16          numdat;
        __u16          numoth;
        __u16          numoth_rcv;
        __u16          numdat_rcv;
        __u16          ackxmt_dat;
        __u16          ackxmt_oth;
        __u16          ackrcv_dat;
        __u16          ackrcv_oth;
        __u8           flowrem_sw;
	__u8           flowloc_sw;
#define DN_SEND         2
#define DN_DONTSEND     1
#define DN_NOCHANGE     0
	__u16		flowrem_dat;
	__u16		flowrem_oth;
	__u16		flowloc_dat;
	__u16		flowloc_oth;
	__u8		services_rem;
	__u8		services_loc;
	__u8		info_rem;
	__u8		info_loc;

	__u16		segsize_rem;
	__u16		segsize_loc;

	__u8		nonagle;
	__u8		multi_ireq;
	__u8		accept_mode;
	unsigned long		seg_total; /* Running total of current segment */

	struct optdata_dn     conndata_in;
	struct optdata_dn     conndata_out;
	struct optdata_dn     discdata_in;
	struct optdata_dn     discdata_out;
        struct accessdata_dn  accessdata;

        struct sockaddr_dn addr; /* Local address  */
	struct sockaddr_dn peer; /* Remote address */

	/*
	 * In this case the RTT estimation is not specified in the
	 * docs, nor is any back off algorithm. Here we follow well
	 * known tcp algorithms with a few small variations.
	 *
	 * snd_window: Max number of packets we send before we wait for
	 *             an ack to come back. This will become part of a
	 *             more complicated scheme when we support flow
	 *             control.
	 *
	 * nsp_srtt:   Round-Trip-Time (x8) in jiffies. This is a rolling
	 *             average.
	 * nsp_rttvar: Round-Trip-Time-Varience (x4) in jiffies. This is the
	 *             varience of the smoothed average (but calculated in
	 *             a simpler way than for normal statistical varience
	 *             calculations).
	 *
	 * nsp_rxtshift: Backoff counter. Value is zero normally, each time
	 *               a packet is lost is increases by one until an ack
	 *               is received. Its used to index an array of backoff
	 *               multipliers.
	 */
#define NSP_MIN_WINDOW 1
#define NSP_MAX_WINDOW (0x07fe)
	unsigned long max_window;
	unsigned long snd_window;
#define NSP_INITIAL_SRTT (HZ)
	unsigned long nsp_srtt;
#define NSP_INITIAL_RTTVAR (HZ*3)
	unsigned long nsp_rttvar;
#define NSP_MAXRXTSHIFT 12
	unsigned long nsp_rxtshift;

	/*
	 * Output queues, one for data, one for otherdata/linkservice
	 */
	struct sk_buff_head data_xmit_queue;
	struct sk_buff_head other_xmit_queue;

	/*
	 * Input queue for other data
	 */
	struct sk_buff_head other_receive_queue;
	int other_report;

	/*
	 * Stuff to do with the slow timer
	 */
	unsigned long stamp;          /* time of last transmit */
	unsigned long persist;
	int (*persist_fxn)(struct sock *sk);
	unsigned long keepalive;
	void (*keepalive_fxn)(struct sock *sk);

	/*
	 * This stuff is for the fast timer for delayed acks
	 */
	struct timer_list delack_timer;
	int delack_pending;
	void (*delack_fxn)(struct sock *sk);

};

static inline struct dn_scp *DN_SK(struct sock *sk)
{
	return (struct dn_scp *)(sk + 1);
}

/*
 * src,dst : Source and Destination DECnet addresses
 * hops : Number of hops through the network
 * dst_port, src_port : NSP port numbers
 * services, info : Useful data extracted from conninit messages
 * rt_flags : Routing flags byte
 * nsp_flags : NSP layer flags byte
 * segsize : Size of segment
 * segnum : Number, for data, otherdata and linkservice
 * xmit_count : Number of times we've transmitted this skb
 * stamp : Time stamp of most recent transmission, used in RTT calculations
 * iif: Input interface number
 *
 * As a general policy, this structure keeps all addresses in network
 * byte order, and all else in host byte order. Thus dst, src, dst_port
 * and src_port are in network order. All else is in host order.
 * 
 */
#define DN_SKB_CB(skb) ((struct dn_skb_cb *)(skb)->cb)
struct dn_skb_cb {
	__le16 dst;
	__le16 src;
	__u16 hops;
	__le16 dst_port;
	__le16 src_port;
	__u8 services;
	__u8 info;
	__u8 rt_flags;
	__u8 nsp_flags;
	__u16 segsize;
	__u16 segnum;
	__u16 xmit_count;
	unsigned long stamp;
	int iif;
};

static inline __le16 dn_eth2dn(unsigned char *ethaddr)
{
	return get_unaligned((__le16 *)(ethaddr + 4));
}

static inline __le16 dn_saddr2dn(struct sockaddr_dn *saddr)
{
	return *(__le16 *)saddr->sdn_nodeaddr;
}

static inline void dn_dn2eth(unsigned char *ethaddr, __le16 addr)
{
	__u16 a = le16_to_cpu(addr);
	ethaddr[0] = 0xAA;
	ethaddr[1] = 0x00;
	ethaddr[2] = 0x04;
	ethaddr[3] = 0x00;
	ethaddr[4] = (__u8)(a & 0xff);
	ethaddr[5] = (__u8)(a >> 8);
}

static inline void dn_sk_ports_copy(struct flowidn *fld, struct dn_scp *scp)
{
	fld->fld_sport = scp->addrloc;
	fld->fld_dport = scp->addrrem;
}

unsigned int dn_mss_from_pmtu(struct net_device *dev, int mtu);
void dn_register_sysctl(void);
void dn_unregister_sysctl(void);

#define DN_MENUVER_ACC 0x01
#define DN_MENUVER_USR 0x02
#define DN_MENUVER_PRX 0x04
#define DN_MENUVER_UIC 0x08

struct sock *dn_sklist_find_listener(struct sockaddr_dn *addr);
struct sock *dn_find_by_skb(struct sk_buff *skb);
#define DN_ASCBUF_LEN 9
char *dn_addr2asc(__u16, char *);
int dn_destroy_timer(struct sock *sk);

int dn_sockaddr2username(struct sockaddr_dn *addr, unsigned char *buf,
			 unsigned char type);
int dn_username2sockaddr(unsigned char *data, int len, struct sockaddr_dn *addr,
			 unsigned char *type);

void dn_start_slow_timer(struct sock *sk);
void dn_stop_slow_timer(struct sock *sk);

extern __le16 decnet_address;
extern int decnet_debug_level;
extern int decnet_time_wait;
extern int decnet_dn_count;
extern int decnet_di_count;
extern int decnet_dr_count;
extern int decnet_no_fc_max_cwnd;

extern long sysctl_decnet_mem[3];
extern int sysctl_decnet_wmem[3];
extern int sysctl_decnet_rmem[3];

#endif /* _NET_DN_H */
