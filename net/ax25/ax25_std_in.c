/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) Alan Cox GW4PTS (alan@lxorguk.ukuu.org.uk)
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright (C) Joerg Reuter DL1BKE (jreuter@yaina.de)
 * Copyright (C) Hans-Joachim Hetscher DD8NE (dd8ne@bnv-bamberg.de)
 *
 * Most of this code is based on the SDL diagrams published in the 7th ARRL
 * Computer Networking Conference papers. The diagrams have mistakes in them,
 * but are mostly correct. Before you modify the code could you read the SDL
 * diagrams as the code is not obvious and probably very easy to break.
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/ip.h>			/* For ip_rcv */
#include <net/tcp.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

/*
 *	State machine for state 1, Awaiting Connection State.
 *	The handling of the timer(s) is in file ax25_std_timer.c.
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_std_state1_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int pf, int type)
{
	switch (frametype) {
	case AX25_SABM:
		ax25->modulus = AX25_MODULUS;
		ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
		ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
		break;

	case AX25_SABME:
		ax25->modulus = AX25_EMODULUS;
		ax25->window  = ax25->ax25_dev->values[AX25_VALUES_EWINDOW];
		ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
		break;

	case AX25_DISC:
		ax25_send_control(ax25, AX25_DM, pf, AX25_RESPONSE);
		break;

	case AX25_UA:
		if (pf) {
			ax25_calculate_rtt(ax25);
			ax25_stop_t1timer(ax25);
			ax25_start_t3timer(ax25);
			ax25_start_idletimer(ax25);
			ax25->vs      = 0;
			ax25->va      = 0;
			ax25->vr      = 0;
			ax25->state   = AX25_STATE_3;
			ax25->n2count = 0;
			if (ax25->sk != NULL) {
				bh_lock_sock(ax25->sk);
				ax25->sk->sk_state = TCP_ESTABLISHED;
				/* For WAIT_SABM connections we will produce an accept ready socket here */
				if (!sock_flag(ax25->sk, SOCK_DEAD))
					ax25->sk->sk_state_change(ax25->sk);
				bh_unlock_sock(ax25->sk);
			}
		}
		break;

	case AX25_DM:
		if (pf) {
			if (ax25->modulus == AX25_MODULUS) {
				ax25_disconnect(ax25, ECONNREFUSED);
			} else {
				ax25->modulus = AX25_MODULUS;
				ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
			}
		}
		break;

	default:
		break;
	}

	return 0;
}

/*
 *	State machine for state 2, Awaiting Release State.
 *	The handling of the timer(s) is in file ax25_std_timer.c
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_std_state2_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int pf, int type)
{
	switch (frametype) {
	case AX25_SABM:
	case AX25_SABME:
		ax25_send_control(ax25, AX25_DM, pf, AX25_RESPONSE);
		break;

	case AX25_DISC:
		ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
		ax25_disconnect(ax25, 0);
		break;

	case AX25_DM:
	case AX25_UA:
		if (pf)
			ax25_disconnect(ax25, 0);
		break;

	case AX25_I:
	case AX25_REJ:
	case AX25_RNR:
	case AX25_RR:
		if (pf) ax25_send_control(ax25, AX25_DM, AX25_POLLON, AX25_RESPONSE);
		break;

	default:
		break;
	}

	return 0;
}

/*
 *	State machine for state 3, Connected State.
 *	The handling of the timer(s) is in file ax25_std_timer.c
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_std_state3_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int ns, int nr, int pf, int type)
{
	int queued = 0;

	switch (frametype) {
	case AX25_SABM:
	case AX25_SABME:
		if (frametype == AX25_SABM) {
			ax25->modulus = AX25_MODULUS;
			ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
		} else {
			ax25->modulus = AX25_EMODULUS;
			ax25->window  = ax25->ax25_dev->values[AX25_VALUES_EWINDOW];
		}
		ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
		ax25_stop_t1timer(ax25);
		ax25_stop_t2timer(ax25);
		ax25_start_t3timer(ax25);
		ax25_start_idletimer(ax25);
		ax25->condition = 0x00;
		ax25->vs        = 0;
		ax25->va        = 0;
		ax25->vr        = 0;
		ax25_requeue_frames(ax25);
		break;

	case AX25_DISC:
		ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
		ax25_disconnect(ax25, 0);
		break;

	case AX25_DM:
		ax25_disconnect(ax25, ECONNRESET);
		break;

	case AX25_RR:
	case AX25_RNR:
		if (frametype == AX25_RR)
			ax25->condition &= ~AX25_COND_PEER_RX_BUSY;
		else
			ax25->condition |= AX25_COND_PEER_RX_BUSY;
		if (type == AX25_COMMAND && pf)
			ax25_std_enquiry_response(ax25);
		if (ax25_validate_nr(ax25, nr)) {
			ax25_check_iframes_acked(ax25, nr);
		} else {
			ax25_std_nr_error_recovery(ax25);
			ax25->state = AX25_STATE_1;
		}
		break;

	case AX25_REJ:
		ax25->condition &= ~AX25_COND_PEER_RX_BUSY;
		if (type == AX25_COMMAND && pf)
			ax25_std_enquiry_response(ax25);
		if (ax25_validate_nr(ax25, nr)) {
			ax25_frames_acked(ax25, nr);
			ax25_calculate_rtt(ax25);
			ax25_stop_t1timer(ax25);
			ax25_start_t3timer(ax25);
			ax25_requeue_frames(ax25);
		} else {
			ax25_std_nr_error_recovery(ax25);
			ax25->state = AX25_STATE_1;
		}
		break;

	case AX25_I:
		if (!ax25_validate_nr(ax25, nr)) {
			ax25_std_nr_error_recovery(ax25);
			ax25->state = AX25_STATE_1;
			break;
		}
		if (ax25->condition & AX25_COND_PEER_RX_BUSY) {
			ax25_frames_acked(ax25, nr);
		} else {
			ax25_check_iframes_acked(ax25, nr);
		}
		if (ax25->condition & AX25_COND_OWN_RX_BUSY) {
			if (pf) ax25_std_enquiry_response(ax25);
			break;
		}
		if (ns == ax25->vr) {
			ax25->vr = (ax25->vr + 1) % ax25->modulus;
			queued = ax25_rx_iframe(ax25, skb);
			if (ax25->condition & AX25_COND_OWN_RX_BUSY)
				ax25->vr = ns;	/* ax25->vr - 1 */
			ax25->condition &= ~AX25_COND_REJECT;
			if (pf) {
				ax25_std_enquiry_response(ax25);
			} else {
				if (!(ax25->condition & AX25_COND_ACK_PENDING)) {
					ax25->condition |= AX25_COND_ACK_PENDING;
					ax25_start_t2timer(ax25);
				}
			}
		} else {
			if (ax25->condition & AX25_COND_REJECT) {
				if (pf) ax25_std_enquiry_response(ax25);
			} else {
				ax25->condition |= AX25_COND_REJECT;
				ax25_send_control(ax25, AX25_REJ, pf, AX25_RESPONSE);
				ax25->condition &= ~AX25_COND_ACK_PENDING;
			}
		}
		break;

	case AX25_FRMR:
	case AX25_ILLEGAL:
		ax25_std_establish_data_link(ax25);
		ax25->state = AX25_STATE_1;
		break;

	default:
		break;
	}

	return queued;
}

/*
 *	State machine for state 4, Timer Recovery State.
 *	The handling of the timer(s) is in file ax25_std_timer.c
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_std_state4_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int ns, int nr, int pf, int type)
{
	int queued = 0;

	switch (frametype) {
	case AX25_SABM:
	case AX25_SABME:
		if (frametype == AX25_SABM) {
			ax25->modulus = AX25_MODULUS;
			ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
		} else {
			ax25->modulus = AX25_EMODULUS;
			ax25->window  = ax25->ax25_dev->values[AX25_VALUES_EWINDOW];
		}
		ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
		ax25_stop_t1timer(ax25);
		ax25_stop_t2timer(ax25);
		ax25_start_t3timer(ax25);
		ax25_start_idletimer(ax25);
		ax25->condition = 0x00;
		ax25->vs        = 0;
		ax25->va        = 0;
		ax25->vr        = 0;
		ax25->state     = AX25_STATE_3;
		ax25->n2count   = 0;
		ax25_requeue_frames(ax25);
		break;

	case AX25_DISC:
		ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
		ax25_disconnect(ax25, 0);
		break;

	case AX25_DM:
		ax25_disconnect(ax25, ECONNRESET);
		break;

	case AX25_RR:
	case AX25_RNR:
		if (frametype == AX25_RR)
			ax25->condition &= ~AX25_COND_PEER_RX_BUSY;
		else
			ax25->condition |= AX25_COND_PEER_RX_BUSY;
		if (type == AX25_RESPONSE && pf) {
			ax25_stop_t1timer(ax25);
			ax25->n2count = 0;
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
				if (ax25->vs == ax25->va) {
					ax25_start_t3timer(ax25);
					ax25->state   = AX25_STATE_3;
				} else {
					ax25_requeue_frames(ax25);
				}
			} else {
				ax25_std_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;
		}
		if (type == AX25_COMMAND && pf)
			ax25_std_enquiry_response(ax25);
		if (ax25_validate_nr(ax25, nr)) {
			ax25_frames_acked(ax25, nr);
		} else {
			ax25_std_nr_error_recovery(ax25);
			ax25->state = AX25_STATE_1;
		}
		break;

	case AX25_REJ:
		ax25->condition &= ~AX25_COND_PEER_RX_BUSY;
		if (pf && type == AX25_RESPONSE) {
			ax25_stop_t1timer(ax25);
			ax25->n2count = 0;
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
				if (ax25->vs == ax25->va) {
					ax25_start_t3timer(ax25);
					ax25->state   = AX25_STATE_3;
				} else {
					ax25_requeue_frames(ax25);
				}
			} else {
				ax25_std_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;
		}
		if (type == AX25_COMMAND && pf)
			ax25_std_enquiry_response(ax25);
		if (ax25_validate_nr(ax25, nr)) {
			ax25_frames_acked(ax25, nr);
			ax25_requeue_frames(ax25);
		} else {
			ax25_std_nr_error_recovery(ax25);
			ax25->state = AX25_STATE_1;
		}
		break;

	case AX25_I:
		if (!ax25_validate_nr(ax25, nr)) {
			ax25_std_nr_error_recovery(ax25);
			ax25->state = AX25_STATE_1;
			break;
		}
		ax25_frames_acked(ax25, nr);
		if (ax25->condition & AX25_COND_OWN_RX_BUSY) {
			if (pf)
				ax25_std_enquiry_response(ax25);
			break;
		}
		if (ns == ax25->vr) {
			ax25->vr = (ax25->vr + 1) % ax25->modulus;
			queued = ax25_rx_iframe(ax25, skb);
			if (ax25->condition & AX25_COND_OWN_RX_BUSY)
				ax25->vr = ns;	/* ax25->vr - 1 */
			ax25->condition &= ~AX25_COND_REJECT;
			if (pf) {
				ax25_std_enquiry_response(ax25);
			} else {
				if (!(ax25->condition & AX25_COND_ACK_PENDING)) {
					ax25->condition |= AX25_COND_ACK_PENDING;
					ax25_start_t2timer(ax25);
				}
			}
		} else {
			if (ax25->condition & AX25_COND_REJECT) {
				if (pf) ax25_std_enquiry_response(ax25);
			} else {
				ax25->condition |= AX25_COND_REJECT;
				ax25_send_control(ax25, AX25_REJ, pf, AX25_RESPONSE);
				ax25->condition &= ~AX25_COND_ACK_PENDING;
			}
		}
		break;

	case AX25_FRMR:
	case AX25_ILLEGAL:
		ax25_std_establish_data_link(ax25);
		ax25->state = AX25_STATE_1;
		break;

	default:
		break;
	}

	return queued;
}

/*
 *	Higher level upcall for a LAPB frame
 */
int ax25_std_frame_in(ax25_cb *ax25, struct sk_buff *skb, int type)
{
	int queued = 0, frametype, ns, nr, pf;

	frametype = ax25_decode(ax25, skb, &ns, &nr, &pf);

	switch (ax25->state) {
	case AX25_STATE_1:
		queued = ax25_std_state1_machine(ax25, skb, frametype, pf, type);
		break;
	case AX25_STATE_2:
		queued = ax25_std_state2_machine(ax25, skb, frametype, pf, type);
		break;
	case AX25_STATE_3:
		queued = ax25_std_state3_machine(ax25, skb, frametype, ns, nr, pf, type);
		break;
	case AX25_STATE_4:
		queued = ax25_std_state4_machine(ax25, skb, frametype, ns, nr, pf, type);
		break;
	}

	ax25_kick(ax25);

	return queued;
}
