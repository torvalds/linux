/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright (C) Joerg Reuter DL1BKE (jreuter@yaina.de)
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/spinlock.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/tcp_states.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

static void ax25_ds_timeout(unsigned long);

/*
 *	Add DAMA slave timeout timer to timer list.
 *	Unlike the connection based timers the timeout function gets
 *	triggered every second. Please note that NET_AX25_DAMA_SLAVE_TIMEOUT
 *	(aka /proc/sys/net/ax25/{dev}/dama_slave_timeout) is still in
 *	1/10th of a second.
 */

static void ax25_ds_add_timer(ax25_dev *ax25_dev)
{
	struct timer_list *t = &ax25_dev->dama.slave_timer;
	t->data		= (unsigned long) ax25_dev;
	t->function	= &ax25_ds_timeout;
	t->expires	= jiffies + HZ;
	add_timer(t);
}

void ax25_ds_del_timer(ax25_dev *ax25_dev)
{
	if (ax25_dev)
		del_timer(&ax25_dev->dama.slave_timer);
}

void ax25_ds_set_timer(ax25_dev *ax25_dev)
{
	if (ax25_dev == NULL)		/* paranoia */
		return;

	del_timer(&ax25_dev->dama.slave_timer);
	ax25_dev->dama.slave_timeout =
		msecs_to_jiffies(ax25_dev->values[AX25_VALUES_DS_TIMEOUT]) / 10;
	ax25_ds_add_timer(ax25_dev);
}

/*
 *	DAMA Slave Timeout
 *	Silently discard all (slave) connections in case our master forgot us...
 */

static void ax25_ds_timeout(unsigned long arg)
{
	ax25_dev *ax25_dev = (struct ax25_dev *) arg;
	ax25_cb *ax25;
	struct hlist_node *node;

	if (ax25_dev == NULL || !ax25_dev->dama.slave)
		return;			/* Yikes! */

	if (!ax25_dev->dama.slave_timeout || --ax25_dev->dama.slave_timeout) {
		ax25_ds_set_timer(ax25_dev);
		return;
	}

	spin_lock_bh(&ax25_list_lock);
	ax25_for_each(ax25, node, &ax25_list) {
		if (ax25->ax25_dev != ax25_dev || !(ax25->condition & AX25_COND_DAMA_MODE))
			continue;

		ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
		ax25_disconnect(ax25, ETIMEDOUT);
	}
	spin_unlock_bh(&ax25_list_lock);

	ax25_dev_dama_off(ax25_dev);
}

void ax25_ds_heartbeat_expiry(ax25_cb *ax25)
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
				sock_put(sk);
				bh_unlock_sock(sk);
			} else
				ax25_destroy_socket(ax25);
			return;
		}
		break;

	case AX25_STATE_3:
		/*
		 * Check the state of the receive buffer.
		 */
		if (sk != NULL) {
			if (atomic_read(&sk->sk_rmem_alloc) <
			    (sk->sk_rcvbuf / 2) &&
			    (ax25->condition & AX25_COND_OWN_RX_BUSY)) {
				ax25->condition &= ~AX25_COND_OWN_RX_BUSY;
				ax25->condition &= ~AX25_COND_ACK_PENDING;
				break;
			}
		}
		break;
	}

	if (sk)
		bh_unlock_sock(sk);

	ax25_start_heartbeat(ax25);
}

/* dl1bke 960114: T3 works much like the IDLE timeout, but
 *                gets reloaded with every frame for this
 *		  connection.
 */
void ax25_ds_t3timer_expiry(ax25_cb *ax25)
{
	ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
	ax25_dama_off(ax25);
	ax25_disconnect(ax25, ETIMEDOUT);
}

/* dl1bke 960228: close the connection when IDLE expires.
 *		  unlike T3 this timer gets reloaded only on
 *		  I frames.
 */
void ax25_ds_idletimer_expiry(ax25_cb *ax25)
{
	ax25_clear_queues(ax25);

	ax25->n2count = 0;
	ax25->state = AX25_STATE_2;

	ax25_calculate_t1(ax25);
	ax25_start_t1timer(ax25);
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

/* dl1bke 960114: The DAMA protocol requires to send data and SABM/DISC
 *                within the poll of any connected channel. Remember
 *                that we are not allowed to send anything unless we
 *                get polled by the Master.
 *
 *                Thus we'll have to do parts of our T1 handling in
 *                ax25_enquiry_response().
 */
void ax25_ds_t1_timeout(ax25_cb *ax25)
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
				ax25_send_control(ax25, AX25_SABM, AX25_POLLOFF, AX25_COMMAND);
			}
		} else {
			ax25->n2count++;
			if (ax25->modulus == AX25_MODULUS)
				ax25_send_control(ax25, AX25_SABM, AX25_POLLOFF, AX25_COMMAND);
			else
				ax25_send_control(ax25, AX25_SABME, AX25_POLLOFF, AX25_COMMAND);
		}
		break;

	case AX25_STATE_2:
		if (ax25->n2count == ax25->n2) {
			ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
			ax25_disconnect(ax25, ETIMEDOUT);
			return;
		} else {
			ax25->n2count++;
		}
		break;

	case AX25_STATE_3:
		if (ax25->n2count == ax25->n2) {
			ax25_send_control(ax25, AX25_DM, AX25_POLLON, AX25_RESPONSE);
			ax25_disconnect(ax25, ETIMEDOUT);
			return;
		} else {
			ax25->n2count++;
		}
		break;
	}

	ax25_calculate_t1(ax25);
	ax25_start_t1timer(ax25);
}
