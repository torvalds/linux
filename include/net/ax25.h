/*
 *	Declarations of AX.25 type objects.
 *
 *	Alan Cox (GW4PTS) 	10/11/93
 */
#ifndef _AX25_H
#define _AX25_H 

#include <linux/ax25.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <asm/atomic.h>

#define	AX25_T1CLAMPLO  		1
#define	AX25_T1CLAMPHI 			(30 * HZ)

#define	AX25_BPQ_HEADER_LEN		16
#define	AX25_KISS_HEADER_LEN		1

#define	AX25_HEADER_LEN			17
#define	AX25_ADDR_LEN			7
#define	AX25_DIGI_HEADER_LEN		(AX25_MAX_DIGIS * AX25_ADDR_LEN)
#define	AX25_MAX_HEADER_LEN		(AX25_HEADER_LEN + AX25_DIGI_HEADER_LEN)

/* AX.25 Protocol IDs */
#define AX25_P_ROSE			0x01
#define AX25_P_VJCOMP			0x06	/* Compressed TCP/IP packet   */
						/* Van Jacobsen (RFC 1144)    */
#define AX25_P_VJUNCOMP			0x07	/* Uncompressed TCP/IP packet */
						/* Van Jacobsen (RFC 1144)    */
#define	AX25_P_SEGMENT			0x08	/* Segmentation fragment      */
#define AX25_P_TEXNET			0xc3	/* TEXTNET datagram protocol  */
#define AX25_P_LQ			0xc4	/* Link Quality Protocol      */
#define AX25_P_ATALK			0xca	/* Appletalk                  */
#define AX25_P_ATALK_ARP		0xcb	/* Appletalk ARP              */
#define AX25_P_IP			0xcc	/* ARPA Internet Protocol     */
#define AX25_P_ARP			0xcd	/* ARPA Adress Resolution     */
#define AX25_P_FLEXNET			0xce	/* FlexNet                    */
#define AX25_P_NETROM 			0xcf	/* NET/ROM                    */
#define AX25_P_TEXT 			0xF0	/* No layer 3 protocol impl.  */

/* AX.25 Segment control values */
#define	AX25_SEG_REM			0x7F
#define	AX25_SEG_FIRST			0x80

#define AX25_CBIT			0x80	/* Command/Response bit */
#define AX25_EBIT			0x01	/* HDLC Address Extension bit */
#define AX25_HBIT			0x80	/* Has been repeated bit */

#define AX25_SSSID_SPARE		0x60	/* Unused bits in SSID for standard AX.25 */
#define AX25_ESSID_SPARE		0x20	/* Unused bits in SSID for extended AX.25 */
#define AX25_DAMA_FLAG			0x20	/* Well, it is *NOT* unused! (dl1bke 951121 */

#define	AX25_COND_ACK_PENDING		0x01
#define	AX25_COND_REJECT		0x02
#define	AX25_COND_PEER_RX_BUSY		0x04
#define	AX25_COND_OWN_RX_BUSY		0x08
#define	AX25_COND_DAMA_MODE		0x10

#ifndef _LINUX_NETDEVICE_H
#include <linux/netdevice.h>
#endif

/* Upper sub-layer (LAPB) definitions */

/* Control field templates */
#define	AX25_I			0x00	/* Information frames */
#define	AX25_S			0x01	/* Supervisory frames */
#define	AX25_RR			0x01	/* Receiver ready */
#define	AX25_RNR		0x05	/* Receiver not ready */
#define	AX25_REJ		0x09	/* Reject */
#define	AX25_U			0x03	/* Unnumbered frames */
#define	AX25_SABM		0x2f	/* Set Asynchronous Balanced Mode */
#define	AX25_SABME		0x6f	/* Set Asynchronous Balanced Mode Extended */
#define	AX25_DISC		0x43	/* Disconnect */
#define	AX25_DM			0x0f	/* Disconnected mode */
#define	AX25_UA			0x63	/* Unnumbered acknowledge */
#define	AX25_FRMR		0x87	/* Frame reject */
#define	AX25_UI			0x03	/* Unnumbered information */
#define	AX25_XID		0xaf	/* Exchange information */
#define	AX25_TEST		0xe3	/* Test */

#define	AX25_PF			0x10	/* Poll/final bit for standard AX.25 */
#define	AX25_EPF		0x01	/* Poll/final bit for extended AX.25 */

#define AX25_ILLEGAL		0x100	/* Impossible to be a real frame type */

#define	AX25_POLLOFF		0
#define	AX25_POLLON		1

/* AX25 L2 C-bit */
#define AX25_COMMAND		1
#define AX25_RESPONSE		2

/* Define Link State constants. */

enum { 
	AX25_STATE_0,			/* Listening */
	AX25_STATE_1,			/* SABM sent */
	AX25_STATE_2,			/* DISC sent */
	AX25_STATE_3,			/* Established */
	AX25_STATE_4			/* Recovery */
};

#define AX25_MODULUS 		8	/*  Standard AX.25 modulus */
#define	AX25_EMODULUS		128	/*  Extended AX.25 modulus */

enum {
	AX25_PROTO_STD_SIMPLEX,
	AX25_PROTO_STD_DUPLEX,
#ifdef CONFIG_AX25_DAMA_SLAVE
	AX25_PROTO_DAMA_SLAVE,
#ifdef CONFIG_AX25_DAMA_MASTER
	AX25_PROTO_DAMA_MASTER,
#define AX25_PROTO_MAX AX25_PROTO_DAMA_MASTER
#endif
#endif
	__AX25_PROTO_MAX,
	AX25_PROTO_MAX = __AX25_PROTO_MAX -1
};

enum {
	AX25_VALUES_IPDEFMODE,	/* 0=DG 1=VC */
	AX25_VALUES_AXDEFMODE,	/* 0=Normal 1=Extended Seq Nos */
	AX25_VALUES_BACKOFF,	/* 0=None 1=Linear 2=Exponential */
	AX25_VALUES_CONMODE,	/* Allow connected modes - 0=No 1=no "PID text" 2=all PIDs */
	AX25_VALUES_WINDOW,	/* Default window size for standard AX.25 */
	AX25_VALUES_EWINDOW,	/* Default window size for extended AX.25 */
	AX25_VALUES_T1,		/* Default T1 timeout value */
	AX25_VALUES_T2,		/* Default T2 timeout value */
	AX25_VALUES_T3,		/* Default T3 timeout value */
	AX25_VALUES_IDLE,	/* Connected mode idle timer */
	AX25_VALUES_N2,		/* Default N2 value */
	AX25_VALUES_PACLEN,	/* AX.25 MTU */
	AX25_VALUES_PROTOCOL,	/* Std AX.25, DAMA Slave, DAMA Master */
	AX25_VALUES_DS_TIMEOUT,	/* DAMA Slave timeout */
	AX25_MAX_VALUES		/* THIS MUST REMAIN THE LAST ENTRY OF THIS LIST */
};

#define	AX25_DEF_IPDEFMODE	0			/* Datagram */
#define	AX25_DEF_AXDEFMODE	0			/* Normal */
#define	AX25_DEF_BACKOFF	1			/* Linear backoff */
#define	AX25_DEF_CONMODE	2			/* Connected mode allowed */
#define	AX25_DEF_WINDOW		2			/* Window=2 */
#define	AX25_DEF_EWINDOW	32			/* Module-128 Window=32 */
#define	AX25_DEF_T1		(10 * HZ)		/* T1=10s */
#define	AX25_DEF_T2		(3 * HZ)		/* T2=3s  */
#define	AX25_DEF_T3		(300 * HZ)		/* T3=300s */
#define	AX25_DEF_N2		10			/* N2=10 */
#define AX25_DEF_IDLE		(0 * 60 * HZ)		/* Idle=None */
#define AX25_DEF_PACLEN		256			/* Paclen=256 */
#define	AX25_DEF_PROTOCOL	AX25_PROTO_STD_SIMPLEX	/* Standard AX.25 */
#define AX25_DEF_DS_TIMEOUT	(3 * 60 * HZ)		/* DAMA timeout 3 minutes */

typedef struct ax25_uid_assoc {
	struct hlist_node	uid_node;
	atomic_t		refcount;
	uid_t			uid;
	ax25_address		call;
} ax25_uid_assoc;

#define ax25_uid_for_each(__ax25, node, list) \
	hlist_for_each_entry(__ax25, node, list, uid_node)

#define ax25_uid_hold(ax25) \
	atomic_inc(&((ax25)->refcount))

static inline void ax25_uid_put(ax25_uid_assoc *assoc)
{
	if (atomic_dec_and_test(&assoc->refcount)) {
		kfree(assoc);
	}
}

typedef struct {
	ax25_address		calls[AX25_MAX_DIGIS];
	unsigned char		repeated[AX25_MAX_DIGIS];
	unsigned char		ndigi;
	signed char		lastrepeat;
} ax25_digi;

typedef struct ax25_route {
	struct ax25_route	*next;
	atomic_t		ref;
	ax25_address		callsign;
	struct net_device	*dev;
	ax25_digi		*digipeat;
	char			ip_mode;
	struct timer_list	timer;
} ax25_route;

typedef struct {
	char			slave;			/* slave_mode?   */
	struct timer_list	slave_timer;		/* timeout timer */
	unsigned short		slave_timeout;		/* when? */
} ax25_dama_info;

struct ctl_table;

typedef struct ax25_dev {
	struct ax25_dev		*next;
	struct net_device	*dev;
	struct net_device	*forward;
	struct ctl_table	*systable;
	int			values[AX25_MAX_VALUES];
#if defined(CONFIG_AX25_DAMA_SLAVE) || defined(CONFIG_AX25_DAMA_MASTER)
	ax25_dama_info		dama;
#endif
} ax25_dev;

typedef struct ax25_cb {
	struct hlist_node	ax25_node;
	ax25_address		source_addr, dest_addr;
	ax25_digi		*digipeat;
	ax25_dev		*ax25_dev;
	unsigned char		iamdigi;
	unsigned char		state, modulus, pidincl;
	unsigned short		vs, vr, va;
	unsigned char		condition, backoff;
	unsigned char		n2, n2count;
	struct timer_list	t1timer, t2timer, t3timer, idletimer;
	unsigned long		t1, t2, t3, idle, rtt;
	unsigned short		paclen, fragno, fraglen;
	struct sk_buff_head	write_queue;
	struct sk_buff_head	reseq_queue;
	struct sk_buff_head	ack_queue;
	struct sk_buff_head	frag_queue;
	unsigned char		window;
	struct timer_list	timer, dtimer;
	struct sock		*sk;		/* Backlink to socket */
	atomic_t		refcount;
} ax25_cb;

#define ax25_sk(__sk) ((ax25_cb *)(__sk)->sk_protinfo)

#define ax25_for_each(__ax25, node, list) \
	hlist_for_each_entry(__ax25, node, list, ax25_node)

#define ax25_cb_hold(__ax25) \
	atomic_inc(&((__ax25)->refcount))

static __inline__ void ax25_cb_put(ax25_cb *ax25)
{
	if (atomic_dec_and_test(&ax25->refcount)) {
		kfree(ax25->digipeat);
		kfree(ax25);
	}
}

static inline __be16 ax25_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	skb->dev      = dev;
	skb->pkt_type = PACKET_HOST;
	skb->mac.raw  = skb->data;
	return htons(ETH_P_AX25);
}

/* af_ax25.c */
extern struct hlist_head ax25_list;
extern spinlock_t ax25_list_lock;
extern void ax25_cb_add(ax25_cb *);
struct sock *ax25_find_listener(ax25_address *, int, struct net_device *, int);
struct sock *ax25_get_socket(ax25_address *, ax25_address *, int);
extern ax25_cb *ax25_find_cb(ax25_address *, ax25_address *, ax25_digi *, struct net_device *);
extern void ax25_send_to_raw(ax25_address *, struct sk_buff *, int);
extern void ax25_destroy_socket(ax25_cb *);
extern ax25_cb *ax25_create_cb(void);
extern void ax25_fillin_cb(ax25_cb *, ax25_dev *);
extern struct sock *ax25_make_new(struct sock *, struct ax25_dev *);

/* ax25_addr.c */
extern ax25_address null_ax25_address;
extern char *ax2asc(char *buf, ax25_address *);
extern void asc2ax(ax25_address *addr, char *callsign);
extern int  ax25cmp(ax25_address *, ax25_address *);
extern int  ax25digicmp(ax25_digi *, ax25_digi *);
extern unsigned char *ax25_addr_parse(unsigned char *, int, ax25_address *, ax25_address *, ax25_digi *, int *, int *);
extern int  ax25_addr_build(unsigned char *, ax25_address *, ax25_address *, ax25_digi *, int, int);
extern int  ax25_addr_size(ax25_digi *);
extern void ax25_digi_invert(ax25_digi *, ax25_digi *);

/* ax25_dev.c */
extern ax25_dev *ax25_dev_list;
extern spinlock_t ax25_dev_lock;

static inline ax25_dev *ax25_dev_ax25dev(struct net_device *dev)
{
	return dev->ax25_ptr;
}

extern ax25_dev *ax25_addr_ax25dev(ax25_address *);
extern void ax25_dev_device_up(struct net_device *);
extern void ax25_dev_device_down(struct net_device *);
extern int  ax25_fwd_ioctl(unsigned int, struct ax25_fwd_struct *);
extern struct net_device *ax25_fwd_dev(struct net_device *);
extern void ax25_dev_free(void);

/* ax25_ds_in.c */
extern int  ax25_ds_frame_in(ax25_cb *, struct sk_buff *, int);

/* ax25_ds_subr.c */
extern void ax25_ds_nr_error_recovery(ax25_cb *);
extern void ax25_ds_enquiry_response(ax25_cb *);
extern void ax25_ds_establish_data_link(ax25_cb *);
extern void ax25_dev_dama_off(ax25_dev *);
extern void ax25_dama_on(ax25_cb *);
extern void ax25_dama_off(ax25_cb *);

/* ax25_ds_timer.c */
extern void ax25_ds_set_timer(ax25_dev *);
extern void ax25_ds_del_timer(ax25_dev *);
extern void ax25_ds_timer(ax25_cb *);
extern void ax25_ds_t1_timeout(ax25_cb *);
extern void ax25_ds_heartbeat_expiry(ax25_cb *);
extern void ax25_ds_t3timer_expiry(ax25_cb *);
extern void ax25_ds_idletimer_expiry(ax25_cb *);

/* ax25_iface.c */
extern int  ax25_protocol_register(unsigned int, int (*)(struct sk_buff *, ax25_cb *));
extern void ax25_protocol_release(unsigned int);
extern int  ax25_linkfail_register(void (*)(ax25_cb *, int));
extern void ax25_linkfail_release(void (*)(ax25_cb *, int));
extern int  ax25_listen_register(ax25_address *, struct net_device *);
extern void ax25_listen_release(ax25_address *, struct net_device *);
extern int  (*ax25_protocol_function(unsigned int))(struct sk_buff *, ax25_cb *);
extern int  ax25_listen_mine(ax25_address *, struct net_device *);
extern void ax25_link_failed(ax25_cb *, int);
extern int  ax25_protocol_is_registered(unsigned int);

/* ax25_in.c */
extern int  ax25_rx_iframe(ax25_cb *, struct sk_buff *);
extern int  ax25_kiss_rcv(struct sk_buff *, struct net_device *, struct packet_type *, struct net_device *);

/* ax25_ip.c */
extern int  ax25_hard_header(struct sk_buff *, struct net_device *, unsigned short, void *, void *, unsigned int);
extern int  ax25_rebuild_header(struct sk_buff *);

/* ax25_out.c */
extern ax25_cb *ax25_send_frame(struct sk_buff *, int, ax25_address *, ax25_address *, ax25_digi *, struct net_device *);
extern void ax25_output(ax25_cb *, int, struct sk_buff *);
extern void ax25_kick(ax25_cb *);
extern void ax25_transmit_buffer(ax25_cb *, struct sk_buff *, int);
extern void ax25_queue_xmit(struct sk_buff *skb, struct net_device *dev);
extern int  ax25_check_iframes_acked(ax25_cb *, unsigned short);

/* ax25_route.c */
extern void ax25_rt_device_down(struct net_device *);
extern int  ax25_rt_ioctl(unsigned int, void __user *);
extern struct file_operations ax25_route_fops;
extern int  ax25_rt_autobind(ax25_cb *, ax25_address *);
extern ax25_route *ax25_rt_find_route(ax25_route *, ax25_address *,
	struct net_device *);
extern struct sk_buff *ax25_rt_build_path(struct sk_buff *, ax25_address *, ax25_address *, ax25_digi *);
extern void ax25_rt_free(void);

static inline void ax25_put_route(ax25_route *ax25_rt)
{
	atomic_dec(&ax25_rt->ref);
}

/* ax25_std_in.c */
extern int  ax25_std_frame_in(ax25_cb *, struct sk_buff *, int);

/* ax25_std_subr.c */
extern void ax25_std_nr_error_recovery(ax25_cb *);
extern void ax25_std_establish_data_link(ax25_cb *);
extern void ax25_std_transmit_enquiry(ax25_cb *);
extern void ax25_std_enquiry_response(ax25_cb *);
extern void ax25_std_timeout_response(ax25_cb *);

/* ax25_std_timer.c */
extern void ax25_std_heartbeat_expiry(ax25_cb *);
extern void ax25_std_t1timer_expiry(ax25_cb *);
extern void ax25_std_t2timer_expiry(ax25_cb *);
extern void ax25_std_t3timer_expiry(ax25_cb *);
extern void ax25_std_idletimer_expiry(ax25_cb *);

/* ax25_subr.c */
extern void ax25_clear_queues(ax25_cb *);
extern void ax25_frames_acked(ax25_cb *, unsigned short);
extern void ax25_requeue_frames(ax25_cb *);
extern int  ax25_validate_nr(ax25_cb *, unsigned short);
extern int  ax25_decode(ax25_cb *, struct sk_buff *, int *, int *, int *);
extern void ax25_send_control(ax25_cb *, int, int, int);
extern void ax25_return_dm(struct net_device *, ax25_address *, ax25_address *, ax25_digi *);
extern void ax25_calculate_t1(ax25_cb *);
extern void ax25_calculate_rtt(ax25_cb *);
extern void ax25_disconnect(ax25_cb *, int);

/* ax25_timer.c */
extern void ax25_start_heartbeat(ax25_cb *);
extern void ax25_start_t1timer(ax25_cb *);
extern void ax25_start_t2timer(ax25_cb *);
extern void ax25_start_t3timer(ax25_cb *);
extern void ax25_start_idletimer(ax25_cb *);
extern void ax25_stop_heartbeat(ax25_cb *);
extern void ax25_stop_t1timer(ax25_cb *);
extern void ax25_stop_t2timer(ax25_cb *);
extern void ax25_stop_t3timer(ax25_cb *);
extern void ax25_stop_idletimer(ax25_cb *);
extern int  ax25_t1timer_running(ax25_cb *);
extern unsigned long ax25_display_timer(struct timer_list *);

/* ax25_uid.c */
extern int  ax25_uid_policy;
extern ax25_uid_assoc *ax25_findbyuid(uid_t);
extern int  ax25_uid_ioctl(int, struct sockaddr_ax25 *);
extern struct file_operations ax25_uid_fops;
extern void ax25_uid_free(void);

/* sysctl_net_ax25.c */
#ifdef CONFIG_SYSCTL
extern void ax25_register_sysctl(void);
extern void ax25_unregister_sysctl(void);
#else
static inline void ax25_register_sysctl(void) {};
static inline void ax25_unregister_sysctl(void) {};
#endif /* CONFIG_SYSCTL */

#endif
