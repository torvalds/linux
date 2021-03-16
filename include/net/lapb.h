/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LAPB_H
#define _LAPB_H 
#include <linux/lapb.h>
#include <linux/refcount.h>

#define	LAPB_HEADER_LEN	20		/* LAPB over Ethernet + a bit more */

#define	LAPB_ACK_PENDING_CONDITION	0x01
#define	LAPB_REJECT_CONDITION		0x02
#define	LAPB_PEER_RX_BUSY_CONDITION	0x04

/* Control field templates */
#define	LAPB_I		0x00	/* Information frames */
#define	LAPB_S		0x01	/* Supervisory frames */
#define	LAPB_U		0x03	/* Unnumbered frames */

#define	LAPB_RR		0x01	/* Receiver ready */
#define	LAPB_RNR	0x05	/* Receiver not ready */
#define	LAPB_REJ	0x09	/* Reject */

#define	LAPB_SABM	0x2F	/* Set Asynchronous Balanced Mode */
#define	LAPB_SABME	0x6F	/* Set Asynchronous Balanced Mode Extended */
#define	LAPB_DISC	0x43	/* Disconnect */
#define	LAPB_DM		0x0F	/* Disconnected mode */
#define	LAPB_UA		0x63	/* Unnumbered acknowledge */
#define	LAPB_FRMR	0x87	/* Frame reject */

#define LAPB_ILLEGAL	0x100	/* Impossible to be a real frame type */

#define	LAPB_SPF	0x10	/* Poll/final bit for standard LAPB */
#define	LAPB_EPF	0x01	/* Poll/final bit for extended LAPB */

#define	LAPB_FRMR_W	0x01	/* Control field invalid	*/
#define	LAPB_FRMR_X	0x02	/* I field invalid		*/
#define	LAPB_FRMR_Y	0x04	/* I field too long		*/
#define	LAPB_FRMR_Z	0x08	/* Invalid N(R)			*/

#define	LAPB_POLLOFF	0
#define	LAPB_POLLON	1

/* LAPB C-bit */
#define LAPB_COMMAND	1
#define LAPB_RESPONSE	2

#define	LAPB_ADDR_A	0x03
#define	LAPB_ADDR_B	0x01
#define	LAPB_ADDR_C	0x0F
#define	LAPB_ADDR_D	0x07

/* Define Link State constants. */
enum {
	LAPB_STATE_0,	/* Disconnected State		*/
	LAPB_STATE_1,	/* Awaiting Connection State	*/
	LAPB_STATE_2,	/* Awaiting Disconnection State	*/
	LAPB_STATE_3,	/* Data Transfer State		*/
	LAPB_STATE_4	/* Frame Reject State		*/
};

#define	LAPB_DEFAULT_MODE		(LAPB_STANDARD | LAPB_SLP | LAPB_DTE)
#define	LAPB_DEFAULT_WINDOW		7		/* Window=7 */
#define	LAPB_DEFAULT_T1			(5 * HZ)	/* T1=5s    */
#define	LAPB_DEFAULT_T2			(1 * HZ)	/* T2=1s    */
#define	LAPB_DEFAULT_N2			20		/* N2=20    */

#define	LAPB_SMODULUS	8
#define	LAPB_EMODULUS	128

/*
 *	Information about the current frame.
 */
struct lapb_frame {
	unsigned short		type;		/* Parsed type		*/
	unsigned short		nr, ns;		/* N(R), N(S)		*/
	unsigned char		cr;		/* Command/Response	*/
	unsigned char		pf;		/* Poll/Final		*/
	unsigned char		control[2];	/* Original control data*/
};

/*
 *	The per LAPB connection control structure.
 */
struct lapb_cb {
	struct list_head	node;
	struct net_device	*dev;

	/* Link status fields */
	unsigned int		mode;
	unsigned char		state;
	unsigned short		vs, vr, va;
	unsigned char		condition;
	unsigned short		n2, n2count;
	unsigned short		t1, t2;
	struct timer_list	t1timer, t2timer;
	bool			t1timer_stop, t2timer_stop;

	/* Internal control information */
	struct sk_buff_head	write_queue;
	struct sk_buff_head	ack_queue;
	unsigned char		window;
	const struct lapb_register_struct *callbacks;

	/* FRMR control information */
	struct lapb_frame	frmr_data;
	unsigned char		frmr_type;

	spinlock_t		lock;
	refcount_t		refcnt;
};

/* lapb_iface.c */
void lapb_connect_confirmation(struct lapb_cb *lapb, int);
void lapb_connect_indication(struct lapb_cb *lapb, int);
void lapb_disconnect_confirmation(struct lapb_cb *lapb, int);
void lapb_disconnect_indication(struct lapb_cb *lapb, int);
int lapb_data_indication(struct lapb_cb *lapb, struct sk_buff *);
int lapb_data_transmit(struct lapb_cb *lapb, struct sk_buff *);

/* lapb_in.c */
void lapb_data_input(struct lapb_cb *lapb, struct sk_buff *);

/* lapb_out.c */
void lapb_kick(struct lapb_cb *lapb);
void lapb_transmit_buffer(struct lapb_cb *lapb, struct sk_buff *, int);
void lapb_establish_data_link(struct lapb_cb *lapb);
void lapb_enquiry_response(struct lapb_cb *lapb);
void lapb_timeout_response(struct lapb_cb *lapb);
void lapb_check_iframes_acked(struct lapb_cb *lapb, unsigned short);
void lapb_check_need_response(struct lapb_cb *lapb, int, int);

/* lapb_subr.c */
void lapb_clear_queues(struct lapb_cb *lapb);
void lapb_frames_acked(struct lapb_cb *lapb, unsigned short);
void lapb_requeue_frames(struct lapb_cb *lapb);
int lapb_validate_nr(struct lapb_cb *lapb, unsigned short);
int lapb_decode(struct lapb_cb *lapb, struct sk_buff *, struct lapb_frame *);
void lapb_send_control(struct lapb_cb *lapb, int, int, int);
void lapb_transmit_frmr(struct lapb_cb *lapb);

/* lapb_timer.c */
void lapb_start_t1timer(struct lapb_cb *lapb);
void lapb_start_t2timer(struct lapb_cb *lapb);
void lapb_stop_t1timer(struct lapb_cb *lapb);
void lapb_stop_t2timer(struct lapb_cb *lapb);
int lapb_t1timer_running(struct lapb_cb *lapb);

/*
 * Debug levels.
 *	0 = Off
 *	1 = State Changes
 *	2 = Packets I/O and State Changes
 *	3 = Hex dumps, Packets I/O and State Changes.
 */
#define	LAPB_DEBUG	0

#define lapb_dbg(level, fmt, ...)			\
do {							\
	if (level < LAPB_DEBUG)				\
		pr_debug(fmt, ##__VA_ARGS__);		\
} while (0)

#endif
