/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) Alan Cox GW4PTS (alan@lxorguk.ukuu.org.uk)
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright (C) Joerg Reuter DL1BKE (jreuter@yaina.de)
 * Copyright (C) Frederic Rible F1OAT (frible@teaser.fr)
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

void ax25_std_heartbeat_expiry(ax25_cb *ax25)
{
	struct sock *sk=ax25->sk;

	if (sk)
		bh_lock_sock(sk);

	switch (ax25->state) {
	case AX25_STATE_0:
		/* Magic here: If we listen() and a new link dies before it
		   is accepted() it isn't 'dead' so doesn't get removed. */
		if (!sk || sock_flag(sk, SOCK_DESTROY) ||
		    (sk->sk_state == TCP_LISTEN &&
		     sock_flag(sk, SOCK_DEAD))) {
			if (sk) {
				sock_hold(sk);
				ax25_destroy_socket(ax25);
				bh_unlock_sock(sk);
				sock_put(sk);
			} else
				ax25_destroy_socket(ax25);
			return;
		}
		break;

	case AX25_STATE_3:
	case AX25_STATE_4:
		/*
		 * Check the state of the receive buffer.
		 */
		if (sk != NULL) {
			if (atomic_read(&sk->sk_rmem_alloc) <
			    (sk->sk_rcvbuf / 2) &&
			    (ax25->condition & AX25_COND_OWN_RX_BUSY)) {
				ax25->condition &= ~AX25_COND_OWN_RX_BUSY;
				ax25->condition &= ~AX25_COND_ACK_PENDING;
				ax25_send_control(ax25, AX25_RR, AX25_POLLOFF, AX25_RESPONSE);
				break;
			}
		}
	}

	if (sk)
		bh_unlock_sock(sk);

	ax25_start_heartbeat(ax25);
}

void ax25_std_t2timer_expiry(ax25_cb *ax25)
{
	if (ax25->condition & AX25_COND_ACK_PENDING) {
		ax25->condition &= ~AX25_COND_ACK_PENDING;
		ax25_std_timeout_response(ax25);
	}
}

void ax25_std_t3timer_expiry(ax25_cb *ax25)
{
	ax25->n2count = 0;
	ax25_std_transmit_enquiry(ax25);
	ax25->state   = AX25_STATE_4;
}

void ax25_std_idletimer_expiry(ax25_cb *ax25)
{
	ax25_clear_queues(ax25);

	ax25->n2count = 0;
	ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
	ax25->state   = AX25_STATE_2;

	ax25_calculate_t1(ax25);
	ax25_start_t1timer(ax25);
	ax25_stop_t2timer(ax25);
	ax25_stop_t3timer(ax25);

	if (ax25->sk != NULL) {
		bh_lock_sock(ax25->sk);
		ax25->sk->sk_state     = TCP_CLOSE;
		ax25->sk->sk_err       = 0;
		ax25->sk->sk_shutdown |= SEND_SHUTDOWN;
		if (!sock_flag(ax25->sk, SOCK_DEAD)) {
			ax25->sk->sk_state_change(ax25->sk);
			sock_set_flag(ax25->sk, SOCK_DEAD);
		}
		bh_unlock_sock(ax25->sk);
	}
}

void ax25_std_t1timer_expiry(ax25_cb *ax25)
{
	switch (ax25->state) {
	case AX25_STATE_1:
		if (ax25->n2count == ax25->n2) {
			if (ax25->modulus == AX25_MODULUS) {
				ax25_disconnect(ax25, ETIMEDOUT);
				return;
			} else {
				ax25->modulus = AX25_MODULUS;
				ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
				ax25->n2count = 0;
				ax25_send_control(ax25, AX25_SABM, AX25_POLLON, AX25_COMMAND);
			}
		} else {
			ax25->n2count++;
			if (ax25->modulus == AX25_MODULUS)
				ax25_send_control(ax25, AX25_SABM, AX25_POLLON, AX25_COMMAND);
			else
				ax25_send_control(ax25, AX25_SABME, AX25_POLLON, AX25_COMMAND);
		}
		break;

	case AX25_STATE_2:
		if (ax25->n2count == ax25->n2) {
			ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
			ax25_disconnect(ax25, ETIMEDOUT);
			return;
		} else {
			ax25->n2count++;
			ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
		}
		break;

	case AX25_STATE_3:
		ax25->n2count = 1;
		ax25_std_transmit_enquiry(ax25);
		ax25->state   = AX25_STATE_4;
		break;

	case AX25_STATE_4:
		if (ax25->n2count == ax25->n2) {
			ax25_send_control(ax25, AX25_DM, AX25_POLLON, AX25_RESPONSE);
			ax25_disconnect(ax25, ETIMEDOUT);
			return;
		} else {
			ax25->n2count++;
			ax25_std_transmit_enquiry(ax25);
		}
		break;
	}

	ax25_calculate_t1(ax25);
	ax25_start_t1timer(ax25);
}
