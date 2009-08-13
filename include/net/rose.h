/*
 *	Declarations of Rose type objects.
 *
 *	Jonathan Naylor G4KLX	25/8/96
 */

#ifndef _ROSE_H
#define _ROSE_H 

#include <linux/rose.h>
#include <net/sock.h>

#define	ROSE_ADDR_LEN			5

#define	ROSE_MIN_LEN			3

#define	ROSE_GFI			0x10
#define	ROSE_Q_BIT			0x80
#define	ROSE_D_BIT			0x40
#define	ROSE_M_BIT			0x10

#define	ROSE_CALL_REQUEST		0x0B
#define	ROSE_CALL_ACCEPTED		0x0F
#define	ROSE_CLEAR_REQUEST		0x13
#define	ROSE_CLEAR_CONFIRMATION		0x17
#define	ROSE_DATA			0x00
#define	ROSE_INTERRUPT			0x23
#define	ROSE_INTERRUPT_CONFIRMATION	0x27
#define	ROSE_RR				0x01
#define	ROSE_RNR			0x05
#define	ROSE_REJ			0x09
#define	ROSE_RESET_REQUEST		0x1B
#define	ROSE_RESET_CONFIRMATION		0x1F
#define	ROSE_REGISTRATION_REQUEST	0xF3
#define	ROSE_REGISTRATION_CONFIRMATION	0xF7
#define	ROSE_RESTART_REQUEST		0xFB
#define	ROSE_RESTART_CONFIRMATION	0xFF
#define	ROSE_DIAGNOSTIC			0xF1
#define	ROSE_ILLEGAL			0xFD

/* Define Link State constants. */

enum {
	ROSE_STATE_0,			/* Ready */
	ROSE_STATE_1,			/* Awaiting Call Accepted */
	ROSE_STATE_2,			/* Awaiting Clear Confirmation */
	ROSE_STATE_3,			/* Data Transfer */
	ROSE_STATE_4,			/* Awaiting Reset Confirmation */
	ROSE_STATE_5			/* Deferred Call Acceptance */
};

#define ROSE_DEFAULT_T0			180000		/* Default T10 T20 value */
#define ROSE_DEFAULT_T1			200000		/* Default T11 T21 value */
#define ROSE_DEFAULT_T2			180000		/* Default T12 T22 value */
#define	ROSE_DEFAULT_T3			180000		/* Default T13 T23 value */
#define	ROSE_DEFAULT_HB			5000		/* Default Holdback value */
#define	ROSE_DEFAULT_IDLE		0		/* No Activity Timeout - none */
#define	ROSE_DEFAULT_ROUTING		1		/* Default routing flag */
#define	ROSE_DEFAULT_FAIL_TIMEOUT	120000		/* Time until link considered usable */
#define	ROSE_DEFAULT_MAXVC		50		/* Maximum number of VCs per neighbour */
#define	ROSE_DEFAULT_WINDOW_SIZE	7		/* Default window size */

#define ROSE_MODULUS 			8
#define	ROSE_MAX_PACKET_SIZE		251		/* Maximum packet size */

#define	ROSE_COND_ACK_PENDING		0x01
#define	ROSE_COND_PEER_RX_BUSY		0x02
#define	ROSE_COND_OWN_RX_BUSY		0x04

#define	FAC_NATIONAL			0x00
#define	FAC_CCITT			0x0F

#define	FAC_NATIONAL_RAND		0x7F
#define	FAC_NATIONAL_FLAGS		0x3F
#define	FAC_NATIONAL_DEST_DIGI		0xE9
#define	FAC_NATIONAL_SRC_DIGI		0xEB
#define	FAC_NATIONAL_FAIL_CALL		0xED
#define	FAC_NATIONAL_FAIL_ADD		0xEE
#define	FAC_NATIONAL_DIGIS			0xEF

#define	FAC_CCITT_DEST_NSAP		0xC9
#define	FAC_CCITT_SRC_NSAP		0xCB

struct rose_neigh {
	struct rose_neigh	*next;
	ax25_address		callsign;
	ax25_digi		*digipeat;
	ax25_cb			*ax25;
	struct net_device		*dev;
	unsigned short		count;
	unsigned short		use;
	unsigned int		number;
	char			restarted;
	char			dce_mode;
	char			loopback;
	struct sk_buff_head	queue;
	struct timer_list	t0timer;
	struct timer_list	ftimer;
};

struct rose_node {
	struct rose_node	*next;
	rose_address		address;
	unsigned short		mask;
	unsigned char		count;
	char			loopback;
	struct rose_neigh	*neighbour[3];
};

struct rose_route {
	struct rose_route	*next;
	unsigned int		lci1, lci2;
	rose_address		src_addr, dest_addr;
	ax25_address		src_call, dest_call;
	struct rose_neigh 	*neigh1, *neigh2;
	unsigned int		rand;
};

struct rose_sock {
	struct sock		sock;
	rose_address		source_addr,   dest_addr;
	ax25_address		source_call,   dest_call;
	unsigned char		source_ndigis, dest_ndigis;
	ax25_address		source_digis[ROSE_MAX_DIGIS];
	ax25_address		dest_digis[ROSE_MAX_DIGIS];
	struct rose_neigh	*neighbour;
	struct net_device		*device;
	unsigned int		lci, rand;
	unsigned char		state, condition, qbitincl, defer;
	unsigned char		cause, diagnostic;
	unsigned short		vs, vr, va, vl;
	unsigned long		t1, t2, t3, hb, idle;
#ifdef M_BIT
	unsigned short		fraglen;
	struct sk_buff_head	frag_queue;
#endif
	struct sk_buff_head	ack_queue;
	struct rose_facilities_struct facilities;
	struct timer_list	timer;
	struct timer_list	idletimer;
};

#define rose_sk(sk) ((struct rose_sock *)(sk))

/* af_rose.c */
extern ax25_address rose_callsign;
extern int  sysctl_rose_restart_request_timeout;
extern int  sysctl_rose_call_request_timeout;
extern int  sysctl_rose_reset_request_timeout;
extern int  sysctl_rose_clear_request_timeout;
extern int  sysctl_rose_no_activity_timeout;
extern int  sysctl_rose_ack_hold_back_timeout;
extern int  sysctl_rose_routing_control;
extern int  sysctl_rose_link_fail_timeout;
extern int  sysctl_rose_maximum_vcs;
extern int  sysctl_rose_window_size;
extern int  rosecmp(rose_address *, rose_address *);
extern int  rosecmpm(rose_address *, rose_address *, unsigned short);
extern char *rose2asc(char *buf, const rose_address *);
extern struct sock *rose_find_socket(unsigned int, struct rose_neigh *);
extern void rose_kill_by_neigh(struct rose_neigh *);
extern unsigned int rose_new_lci(struct rose_neigh *);
extern int  rose_rx_call_request(struct sk_buff *, struct net_device *, struct rose_neigh *, unsigned int);
extern void rose_destroy_socket(struct sock *);

/* rose_dev.c */
extern void  rose_setup(struct net_device *);

/* rose_in.c */
extern int  rose_process_rx_frame(struct sock *, struct sk_buff *);

/* rose_link.c */
extern void rose_start_ftimer(struct rose_neigh *);
extern void rose_stop_ftimer(struct rose_neigh *);
extern void rose_stop_t0timer(struct rose_neigh *);
extern int  rose_ftimer_running(struct rose_neigh *);
extern void rose_link_rx_restart(struct sk_buff *, struct rose_neigh *, unsigned short);
extern void rose_transmit_clear_request(struct rose_neigh *, unsigned int, unsigned char, unsigned char);
extern void rose_transmit_link(struct sk_buff *, struct rose_neigh *);

/* rose_loopback.c */
extern void rose_loopback_init(void);
extern void rose_loopback_clear(void);
extern int  rose_loopback_queue(struct sk_buff *, struct rose_neigh *);

/* rose_out.c */
extern void rose_kick(struct sock *);
extern void rose_enquiry_response(struct sock *);

/* rose_route.c */
extern struct rose_neigh *rose_loopback_neigh;
extern const struct file_operations rose_neigh_fops;
extern const struct file_operations rose_nodes_fops;
extern const struct file_operations rose_routes_fops;

extern void rose_add_loopback_neigh(void);
extern int __must_check rose_add_loopback_node(rose_address *);
extern void rose_del_loopback_node(rose_address *);
extern void rose_rt_device_down(struct net_device *);
extern void rose_link_device_down(struct net_device *);
extern struct net_device *rose_dev_first(void);
extern struct net_device *rose_dev_get(rose_address *);
extern struct rose_route *rose_route_free_lci(unsigned int, struct rose_neigh *);
extern struct rose_neigh *rose_get_neigh(rose_address *, unsigned char *, unsigned char *, int);
extern int  rose_rt_ioctl(unsigned int, void __user *);
extern void rose_link_failed(ax25_cb *, int);
extern int  rose_route_frame(struct sk_buff *, ax25_cb *);
extern void rose_rt_free(void);

/* rose_subr.c */
extern void rose_clear_queues(struct sock *);
extern void rose_frames_acked(struct sock *, unsigned short);
extern void rose_requeue_frames(struct sock *);
extern int  rose_validate_nr(struct sock *, unsigned short);
extern void rose_write_internal(struct sock *, int);
extern int  rose_decode(struct sk_buff *, int *, int *, int *, int *, int *);
extern int  rose_parse_facilities(unsigned char *, struct rose_facilities_struct *);
extern void rose_disconnect(struct sock *, int, int, int);

/* rose_timer.c */
extern void rose_start_heartbeat(struct sock *);
extern void rose_start_t1timer(struct sock *);
extern void rose_start_t2timer(struct sock *);
extern void rose_start_t3timer(struct sock *);
extern void rose_start_hbtimer(struct sock *);
extern void rose_start_idletimer(struct sock *);
extern void rose_stop_heartbeat(struct sock *);
extern void rose_stop_timer(struct sock *);
extern void rose_stop_idletimer(struct sock *);

/* sysctl_net_rose.c */
extern void rose_register_sysctl(void);
extern void rose_unregister_sysctl(void);

#endif
