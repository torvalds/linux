/*
 *	Declarations of X.25 Packet Layer type objects.
 *
 * 	History
 *	nov/17/96	Jonathan Naylor	  Initial version.		
 *	mar/20/00	Daniela Squassoni Disabling/enabling of facilities 
 *					  negotiation.
 */

#ifndef _X25_H
#define _X25_H 
#include <linux/x25.h>
#include <linux/slab.h>
#include <net/sock.h>

#define	X25_ADDR_LEN			16

#define	X25_MAX_L2_LEN			18	/* 802.2 LLC */

#define	X25_STD_MIN_LEN			3
#define	X25_EXT_MIN_LEN			4

#define	X25_GFI_SEQ_MASK		0x30
#define	X25_GFI_STDSEQ			0x10
#define	X25_GFI_EXTSEQ			0x20

#define	X25_Q_BIT			0x80
#define	X25_D_BIT			0x40
#define	X25_STD_M_BIT			0x10
#define	X25_EXT_M_BIT			0x01

#define	X25_CALL_REQUEST		0x0B
#define	X25_CALL_ACCEPTED		0x0F
#define	X25_CLEAR_REQUEST		0x13
#define	X25_CLEAR_CONFIRMATION		0x17
#define	X25_DATA			0x00
#define	X25_INTERRUPT			0x23
#define	X25_INTERRUPT_CONFIRMATION	0x27
#define	X25_RR				0x01
#define	X25_RNR				0x05
#define	X25_REJ				0x09
#define	X25_RESET_REQUEST		0x1B
#define	X25_RESET_CONFIRMATION		0x1F
#define	X25_REGISTRATION_REQUEST	0xF3
#define	X25_REGISTRATION_CONFIRMATION	0xF7
#define	X25_RESTART_REQUEST		0xFB
#define	X25_RESTART_CONFIRMATION	0xFF
#define	X25_DIAGNOSTIC			0xF1
#define	X25_ILLEGAL			0xFD

/* Define the various conditions that may exist */

#define	X25_COND_ACK_PENDING	0x01
#define	X25_COND_OWN_RX_BUSY	0x02
#define	X25_COND_PEER_RX_BUSY	0x04

/* Define Link State constants. */
enum {
	X25_STATE_0,		/* Ready */
	X25_STATE_1,		/* Awaiting Call Accepted */
	X25_STATE_2,		/* Awaiting Clear Confirmation */
	X25_STATE_3,		/* Data Transfer */
	X25_STATE_4		/* Awaiting Reset Confirmation */
};

enum {
	X25_LINK_STATE_0,
	X25_LINK_STATE_1,
	X25_LINK_STATE_2,
	X25_LINK_STATE_3
};

#define X25_DEFAULT_T20		(180 * HZ)		/* Default T20 value */
#define X25_DEFAULT_T21		(200 * HZ)		/* Default T21 value */
#define X25_DEFAULT_T22		(180 * HZ)		/* Default T22 value */
#define	X25_DEFAULT_T23		(180 * HZ)		/* Default T23 value */
#define	X25_DEFAULT_T2		(3   * HZ)		/* Default ack holdback value */

#define	X25_DEFAULT_WINDOW_SIZE	2			/* Default Window Size	*/
#define	X25_DEFAULT_PACKET_SIZE	X25_PS128		/* Default Packet Size */
#define	X25_DEFAULT_THROUGHPUT	0x0A			/* Deafult Throughput */
#define	X25_DEFAULT_REVERSE	0x00			/* Default Reverse Charging */
#define X25_DENY_ACCPT_APPRV   0x01			/* Default value */
#define X25_ALLOW_ACCPT_APPRV  0x00			/* Control enabled */

#define X25_SMODULUS 		8
#define	X25_EMODULUS		128

/*
 *	X.25 Facilities constants.
 */

#define	X25_FAC_CLASS_MASK	0xC0

#define	X25_FAC_CLASS_A		0x00
#define	X25_FAC_CLASS_B		0x40
#define	X25_FAC_CLASS_C		0x80
#define	X25_FAC_CLASS_D		0xC0

#define	X25_FAC_REVERSE		0x01			/* also fast select */
#define	X25_FAC_THROUGHPUT	0x02
#define	X25_FAC_PACKET_SIZE	0x42
#define	X25_FAC_WINDOW_SIZE	0x43

#define X25_MAX_FAC_LEN 	60
#define	X25_MAX_CUD_LEN		128

#define X25_FAC_CALLING_AE 	0xCB
#define X25_FAC_CALLED_AE 	0xC9

#define X25_MARKER 		0x00
#define X25_DTE_SERVICES 	0x0F
#define X25_MAX_AE_LEN 		40			/* Max num of semi-octets in AE - OSI Nw */
#define X25_MAX_DTE_FACIL_LEN	21			/* Max length of DTE facility params */

/**
 *	struct x25_route - x25 routing entry
 *	@node - entry in x25_list_lock
 *	@address - Start of address range
 *	@sigdigits - Number of sig digits
 *	@dev - More than one for MLP
 *	@refcnt - reference counter
 */
struct x25_route {
	struct list_head	node;		
	struct x25_address	address;
	unsigned int		sigdigits;
	struct net_device	*dev;
	atomic_t		refcnt;
};

struct x25_neigh {
	struct list_head	node;
	struct net_device	*dev;
	unsigned int		state;
	unsigned int		extended;
	struct sk_buff_head	queue;
	unsigned long		t20;
	struct timer_list	t20timer;
	unsigned long		global_facil_mask;
	atomic_t		refcnt;
};

struct x25_sock {
	struct sock		sk;
	struct x25_address	source_addr, dest_addr;
	struct x25_neigh	*neighbour;
	unsigned int		lci, cudmatchlength;
	unsigned char		state, condition, qbitincl, intflag, accptapprv;
	unsigned short		vs, vr, va, vl;
	unsigned long		t2, t21, t22, t23;
	unsigned short		fraglen;
	struct sk_buff_head	ack_queue;
	struct sk_buff_head	fragment_queue;
	struct sk_buff_head	interrupt_in_queue;
	struct sk_buff_head	interrupt_out_queue;
	struct timer_list	timer;
	struct x25_causediag	causediag;
	struct x25_facilities	facilities;
	struct x25_dte_facilities dte_facilities;
	struct x25_calluserdata	calluserdata;
	unsigned long 		vc_facil_mask;	/* inc_call facilities mask */
};

struct x25_forward {
	struct list_head	node;
	unsigned int		lci;
	struct net_device	*dev1;
	struct net_device	*dev2;
	atomic_t		refcnt;
};

static inline struct x25_sock *x25_sk(const struct sock *sk)
{
	return (struct x25_sock *)sk;
}

/* af_x25.c */
extern int  sysctl_x25_restart_request_timeout;
extern int  sysctl_x25_call_request_timeout;
extern int  sysctl_x25_reset_request_timeout;
extern int  sysctl_x25_clear_request_timeout;
extern int  sysctl_x25_ack_holdback_timeout;
extern int  sysctl_x25_forward;

extern int x25_parse_address_block(struct sk_buff *skb,
		struct x25_address *called_addr,
		struct x25_address *calling_addr);

extern int  x25_addr_ntoa(unsigned char *, struct x25_address *,
			  struct x25_address *);
extern int  x25_addr_aton(unsigned char *, struct x25_address *,
			  struct x25_address *);
extern struct sock *x25_find_socket(unsigned int, struct x25_neigh *);
extern void x25_destroy_socket_from_timer(struct sock *);
extern int  x25_rx_call_request(struct sk_buff *, struct x25_neigh *, unsigned int);
extern void x25_kill_by_neigh(struct x25_neigh *);

/* x25_dev.c */
extern void x25_send_frame(struct sk_buff *, struct x25_neigh *);
extern int  x25_lapb_receive_frame(struct sk_buff *, struct net_device *, struct packet_type *, struct net_device *);
extern void x25_establish_link(struct x25_neigh *);
extern void x25_terminate_link(struct x25_neigh *);

/* x25_facilities.c */
extern int x25_parse_facilities(struct sk_buff *, struct x25_facilities *,
				struct x25_dte_facilities *, unsigned long *);
extern int x25_create_facilities(unsigned char *, struct x25_facilities *,
				struct x25_dte_facilities *, unsigned long);
extern int x25_negotiate_facilities(struct sk_buff *, struct sock *,
				struct x25_facilities *,
				struct x25_dte_facilities *);
extern void x25_limit_facilities(struct x25_facilities *, struct x25_neigh *);

/* x25_forward.c */
extern void x25_clear_forward_by_lci(unsigned int lci);
extern void x25_clear_forward_by_dev(struct net_device *);
extern int x25_forward_data(int, struct x25_neigh *, struct sk_buff *);
extern int x25_forward_call(struct x25_address *, struct x25_neigh *,
				struct sk_buff *, int);

/* x25_in.c */
extern int  x25_process_rx_frame(struct sock *, struct sk_buff *);
extern int  x25_backlog_rcv(struct sock *, struct sk_buff *);

/* x25_link.c */
extern void x25_link_control(struct sk_buff *, struct x25_neigh *, unsigned short);
extern void x25_link_device_up(struct net_device *);
extern void x25_link_device_down(struct net_device *);
extern void x25_link_established(struct x25_neigh *);
extern void x25_link_terminated(struct x25_neigh *);
extern void x25_transmit_clear_request(struct x25_neigh *, unsigned int, unsigned char);
extern void x25_transmit_link(struct sk_buff *, struct x25_neigh *);
extern int  x25_subscr_ioctl(unsigned int, void __user *);
extern struct x25_neigh *x25_get_neigh(struct net_device *);
extern void x25_link_free(void);

/* x25_neigh.c */
static __inline__ void x25_neigh_hold(struct x25_neigh *nb)
{
	atomic_inc(&nb->refcnt);
}

static __inline__ void x25_neigh_put(struct x25_neigh *nb)
{
	if (atomic_dec_and_test(&nb->refcnt))
		kfree(nb);
}

/* x25_out.c */
extern  int x25_output(struct sock *, struct sk_buff *);
extern void x25_kick(struct sock *);
extern void x25_enquiry_response(struct sock *);

/* x25_route.c */
extern struct x25_route *x25_get_route(struct x25_address *addr);
extern struct net_device *x25_dev_get(char *);
extern void x25_route_device_down(struct net_device *dev);
extern int  x25_route_ioctl(unsigned int, void __user *);
extern void x25_route_free(void);

static __inline__ void x25_route_hold(struct x25_route *rt)
{
	atomic_inc(&rt->refcnt);
}

static __inline__ void x25_route_put(struct x25_route *rt)
{
	if (atomic_dec_and_test(&rt->refcnt))
		kfree(rt);
}

/* x25_subr.c */
extern void x25_clear_queues(struct sock *);
extern void x25_frames_acked(struct sock *, unsigned short);
extern void x25_requeue_frames(struct sock *);
extern int  x25_validate_nr(struct sock *, unsigned short);
extern void x25_write_internal(struct sock *, int);
extern int  x25_decode(struct sock *, struct sk_buff *, int *, int *, int *, int *, int *);
extern void x25_disconnect(struct sock *, int, unsigned char, unsigned char);

/* x25_timer.c */
extern void x25_init_timers(struct sock *sk);
extern void x25_start_heartbeat(struct sock *);
extern void x25_start_t2timer(struct sock *);
extern void x25_start_t21timer(struct sock *);
extern void x25_start_t22timer(struct sock *);
extern void x25_start_t23timer(struct sock *);
extern void x25_stop_heartbeat(struct sock *);
extern void x25_stop_timer(struct sock *);
extern unsigned long x25_display_timer(struct sock *);
extern void x25_check_rbuf(struct sock *);

/* sysctl_net_x25.c */
#ifdef CONFIG_SYSCTL
extern void x25_register_sysctl(void);
extern void x25_unregister_sysctl(void);
#else
static inline void x25_register_sysctl(void) {};
static inline void x25_unregister_sysctl(void) {};
#endif /* CONFIG_SYSCTL */

struct x25_skb_cb {
	unsigned flags;
};
#define X25_SKB_CB(s) ((struct x25_skb_cb *) ((s)->cb))

extern struct hlist_head x25_list;
extern rwlock_t x25_list_lock;
extern struct list_head x25_route_list;
extern rwlock_t x25_route_list_lock;
extern struct list_head x25_forward_list;
extern rwlock_t x25_forward_list_lock;

extern int x25_proc_init(void);
extern void x25_proc_exit(void);
#endif
