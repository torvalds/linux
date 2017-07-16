/*
 *	Declarations of NET/ROM type objects.
 *
 *	Jonathan Naylor G4KLX	9/4/95
 */

#ifndef _NETROM_H
#define _NETROM_H 

#include <linux/netrom.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <linux/refcount.h>

#define	NR_NETWORK_LEN			15
#define	NR_TRANSPORT_LEN		5

#define	NR_PROTO_IP			0x0C

#define	NR_PROTOEXT			0x00
#define	NR_CONNREQ			0x01
#define	NR_CONNACK			0x02
#define	NR_DISCREQ			0x03
#define	NR_DISCACK			0x04
#define	NR_INFO				0x05
#define	NR_INFOACK			0x06
#define	NR_RESET			0x07

#define	NR_CHOKE_FLAG			0x80
#define	NR_NAK_FLAG			0x40
#define	NR_MORE_FLAG			0x20

/* Define Link State constants. */
enum {
	NR_STATE_0,
	NR_STATE_1,
	NR_STATE_2,
	NR_STATE_3
};

#define	NR_COND_ACK_PENDING		0x01
#define	NR_COND_REJECT			0x02
#define	NR_COND_PEER_RX_BUSY		0x04
#define	NR_COND_OWN_RX_BUSY		0x08

#define NR_DEFAULT_T1			120000		/* Outstanding frames - 120 seconds */
#define NR_DEFAULT_T2			5000		/* Response delay     - 5 seconds */
#define NR_DEFAULT_N2			3		/* Number of Retries - 3 */
#define	NR_DEFAULT_T4			180000		/* Busy Delay - 180 seconds */
#define	NR_DEFAULT_IDLE			0		/* No Activity Timeout - none */
#define	NR_DEFAULT_WINDOW		4		/* Default Window Size - 4 */
#define	NR_DEFAULT_OBS			6		/* Default Obsolescence Count - 6 */
#define	NR_DEFAULT_QUAL			10		/* Default Neighbour Quality - 10 */
#define	NR_DEFAULT_TTL			16		/* Default Time To Live - 16 */
#define	NR_DEFAULT_ROUTING		1		/* Is routing enabled ? */
#define	NR_DEFAULT_FAILS		2		/* Link fails until route fails */
#define	NR_DEFAULT_RESET		0		/* Sent / accept reset cmds? */

#define NR_MODULUS 			256
#define NR_MAX_WINDOW_SIZE		127			/* Maximum Window Allowable - 127 */
#define	NR_MAX_PACKET_SIZE		236			/* Maximum Packet Length - 236 */

struct nr_sock {
	struct sock		sock;
	ax25_address		user_addr, source_addr, dest_addr;
	struct net_device		*device;
	unsigned char		my_index,   my_id;
	unsigned char		your_index, your_id;
	unsigned char		state, condition, bpqext, window;
	unsigned short		vs, vr, va, vl;
	unsigned char		n2, n2count;
	unsigned long		t1, t2, t4, idle;
	unsigned short		fraglen;
	struct timer_list	t1timer;
	struct timer_list	t2timer;
	struct timer_list	t4timer;
	struct timer_list	idletimer;
	struct sk_buff_head	ack_queue;
	struct sk_buff_head	reseq_queue;
	struct sk_buff_head	frag_queue;
};

#define nr_sk(sk) ((struct nr_sock *)(sk))

struct nr_neigh {
	struct hlist_node	neigh_node;
	ax25_address		callsign;
	ax25_digi		*digipeat;
	ax25_cb			*ax25;
	struct net_device	*dev;
	unsigned char		quality;
	unsigned char		locked;
	unsigned short		count;
	unsigned int		number;
	unsigned char		failed;
	refcount_t		refcount;
};

struct nr_route {
	unsigned char   quality;
	unsigned char   obs_count;
	struct nr_neigh *neighbour;
};

struct nr_node {
	struct hlist_node	node_node;
	ax25_address		callsign;
	char			mnemonic[7];
	unsigned char		which;
	unsigned char		count;
	struct nr_route		routes[3];
	refcount_t		refcount;
	spinlock_t		node_lock;
};

/*********************************************************************
 *	nr_node & nr_neigh lists, refcounting and locking
 *********************************************************************/

#define nr_node_hold(__nr_node) \
	refcount_inc(&((__nr_node)->refcount))

static __inline__ void nr_node_put(struct nr_node *nr_node)
{
	if (refcount_dec_and_test(&nr_node->refcount)) {
		kfree(nr_node);
	}
}

#define nr_neigh_hold(__nr_neigh) \
	refcount_inc(&((__nr_neigh)->refcount))

static __inline__ void nr_neigh_put(struct nr_neigh *nr_neigh)
{
	if (refcount_dec_and_test(&nr_neigh->refcount)) {
		if (nr_neigh->ax25)
			ax25_cb_put(nr_neigh->ax25);
		kfree(nr_neigh->digipeat);
		kfree(nr_neigh);
	}
}

/* nr_node_lock and nr_node_unlock also hold/put the node's refcounter.
 */
static __inline__ void nr_node_lock(struct nr_node *nr_node)
{
	nr_node_hold(nr_node);
	spin_lock_bh(&nr_node->node_lock);
}

static __inline__ void nr_node_unlock(struct nr_node *nr_node)
{
	spin_unlock_bh(&nr_node->node_lock);
	nr_node_put(nr_node);
}

#define nr_neigh_for_each(__nr_neigh, list) \
	hlist_for_each_entry(__nr_neigh, list, neigh_node)

#define nr_neigh_for_each_safe(__nr_neigh, node2, list) \
	hlist_for_each_entry_safe(__nr_neigh, node2, list, neigh_node)

#define nr_node_for_each(__nr_node, list) \
	hlist_for_each_entry(__nr_node, list, node_node)

#define nr_node_for_each_safe(__nr_node, node2, list) \
	hlist_for_each_entry_safe(__nr_node, node2, list, node_node)


/*********************************************************************/

/* af_netrom.c */
extern int  sysctl_netrom_default_path_quality;
extern int  sysctl_netrom_obsolescence_count_initialiser;
extern int  sysctl_netrom_network_ttl_initialiser;
extern int  sysctl_netrom_transport_timeout;
extern int  sysctl_netrom_transport_maximum_tries;
extern int  sysctl_netrom_transport_acknowledge_delay;
extern int  sysctl_netrom_transport_busy_delay;
extern int  sysctl_netrom_transport_requested_window_size;
extern int  sysctl_netrom_transport_no_activity_timeout;
extern int  sysctl_netrom_routing_control;
extern int  sysctl_netrom_link_fails_count;
extern int  sysctl_netrom_reset_circuit;

int nr_rx_frame(struct sk_buff *, struct net_device *);
void nr_destroy_socket(struct sock *);

/* nr_dev.c */
int nr_rx_ip(struct sk_buff *, struct net_device *);
void nr_setup(struct net_device *);

/* nr_in.c */
int nr_process_rx_frame(struct sock *, struct sk_buff *);

/* nr_loopback.c */
void nr_loopback_init(void);
void nr_loopback_clear(void);
int nr_loopback_queue(struct sk_buff *);

/* nr_out.c */
void nr_output(struct sock *, struct sk_buff *);
void nr_send_nak_frame(struct sock *);
void nr_kick(struct sock *);
void nr_transmit_buffer(struct sock *, struct sk_buff *);
void nr_establish_data_link(struct sock *);
void nr_enquiry_response(struct sock *);
void nr_check_iframes_acked(struct sock *, unsigned short);

/* nr_route.c */
void nr_rt_device_down(struct net_device *);
struct net_device *nr_dev_first(void);
struct net_device *nr_dev_get(ax25_address *);
int nr_rt_ioctl(unsigned int, void __user *);
void nr_link_failed(ax25_cb *, int);
int nr_route_frame(struct sk_buff *, ax25_cb *);
extern const struct file_operations nr_nodes_fops;
extern const struct file_operations nr_neigh_fops;
void nr_rt_free(void);

/* nr_subr.c */
void nr_clear_queues(struct sock *);
void nr_frames_acked(struct sock *, unsigned short);
void nr_requeue_frames(struct sock *);
int nr_validate_nr(struct sock *, unsigned short);
int nr_in_rx_window(struct sock *, unsigned short);
void nr_write_internal(struct sock *, int);

void __nr_transmit_reply(struct sk_buff *skb, int mine, unsigned char cmdflags);

/*
 * This routine is called when a Connect Acknowledge with the Choke Flag
 * set is needed to refuse a connection.
 */
#define nr_transmit_refusal(skb, mine)					\
do {									\
	__nr_transmit_reply((skb), (mine), NR_CONNACK | NR_CHOKE_FLAG);	\
} while (0)

/*
 * This routine is called when we don't have a circuit matching an incoming
 * NET/ROM packet.  This is an G8PZT Xrouter extension.
 */
#define nr_transmit_reset(skb, mine)					\
do {									\
	__nr_transmit_reply((skb), (mine), NR_RESET);			\
} while (0)

void nr_disconnect(struct sock *, int);

/* nr_timer.c */
void nr_init_timers(struct sock *sk);
void nr_start_heartbeat(struct sock *);
void nr_start_t1timer(struct sock *);
void nr_start_t2timer(struct sock *);
void nr_start_t4timer(struct sock *);
void nr_start_idletimer(struct sock *);
void nr_stop_heartbeat(struct sock *);
void nr_stop_t1timer(struct sock *);
void nr_stop_t2timer(struct sock *);
void nr_stop_t4timer(struct sock *);
void nr_stop_idletimer(struct sock *);
int nr_t1timer_running(struct sock *);

/* sysctl_net_netrom.c */
void nr_register_sysctl(void);
void nr_unregister_sysctl(void);

#endif
