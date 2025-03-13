// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright (C) 2002 Ralf Baechle DO1GRB (ralf@gnu.org)
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
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
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/rose.h>

static void rose_heartbeat_expiry(struct timer_list *t);
static void rose_timer_expiry(struct timer_list *);
static void rose_idletimer_expiry(struct timer_list *);

void rose_start_heartbeat(struct sock *sk)
{
	sk_stop_timer(sk, &sk->sk_timer);

	sk->sk_timer.function = rose_heartbeat_expiry;
	sk->sk_timer.expires  = jiffies + 5 * HZ;

	sk_reset_timer(sk, &sk->sk_timer, sk->sk_timer.expires);
}

void rose_start_t1timer(struct sock *sk)
{
	struct rose_sock *rose = rose_sk(sk);

	sk_stop_timer(sk, &rose->timer);

	rose->timer.function = rose_timer_expiry;
	rose->timer.expires  = jiffies + rose->t1;

	sk_reset_timer(sk, &rose->timer, rose->timer.expires);
}

void rose_start_t2timer(struct sock *sk)
{
	struct rose_sock *rose = rose_sk(sk);

	sk_stop_timer(sk, &rose->timer);

	rose->timer.function = rose_timer_expiry;
	rose->timer.expires  = jiffies + rose->t2;

	sk_reset_timer(sk, &rose->timer, rose->timer.expires);
}

void rose_start_t3timer(struct sock *sk)
{
	struct rose_sock *rose = rose_sk(sk);

	sk_stop_timer(sk, &rose->timer);

	rose->timer.function = rose_timer_expiry;
	rose->timer.expires  = jiffies + rose->t3;

	sk_reset_timer(sk, &rose->timer, rose->timer.expires);
}

void rose_start_hbtimer(struct sock *sk)
{
	struct rose_sock *rose = rose_sk(sk);

	sk_stop_timer(sk, &rose->timer);

	rose->timer.function = rose_timer_expiry;
	rose->timer.expires  = jiffies + rose->hb;

	sk_reset_timer(sk, &rose->timer, rose->timer.expires);
}

void rose_start_idletimer(struct sock *sk)
{
	struct rose_sock *rose = rose_sk(sk);

	sk_stop_timer(sk, &rose->idletimer);

	if (rose->idle > 0) {
		rose->idletimer.function = rose_idletimer_expiry;
		rose->idletimer.expires  = jiffies + rose->idle;

		sk_reset_timer(sk, &rose->idletimer, rose->idletimer.expires);
	}
}

void rose_stop_heartbeat(struct sock *sk)
{
	sk_stop_timer(sk, &sk->sk_timer);
}

void rose_stop_timer(struct sock *sk)
{
	sk_stop_timer(sk, &rose_sk(sk)->timer);
}

void rose_stop_idletimer(struct sock *sk)
{
	sk_stop_timer(sk, &rose_sk(sk)->idletimer);
}

static void rose_heartbeat_expiry(struct timer_list *t)
{
	struct sock *sk = from_timer(sk, t, sk_timer);
	struct rose_sock *rose = rose_sk(sk);

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		sk_reset_timer(sk, &sk->sk_timer, jiffies + HZ/20);
		goto out;
	}
	switch (rose->state) {
	case ROSE_STATE_0:
		/* Magic here: If we listen() and a new link dies before it
		   is accepted() it isn't 'dead' so doesn't get removed. */
		if (sock_flag(sk, SOCK_DESTROY) ||
		    (sk->sk_state == TCP_LISTEN && sock_flag(sk, SOCK_DEAD))) {
			bh_unlock_sock(sk);
			rose_destroy_socket(sk);
			sock_put(sk);
			return;
		}
		break;

	case ROSE_STATE_3:
		/*
		 * Check for the state of the receive buffer.
		 */
		if (atomic_read(&sk->sk_rmem_alloc) < (sk->sk_rcvbuf / 2) &&
		    (rose->condition & ROSE_COND_OWN_RX_BUSY)) {
			rose->condition &= ~ROSE_COND_OWN_RX_BUSY;
			rose->condition &= ~ROSE_COND_ACK_PENDING;
			rose->vl         = rose->vr;
			rose_write_internal(sk, ROSE_RR);
			rose_stop_timer(sk);	/* HB */
			break;
		}
		break;
	}

	rose_start_heartbeat(sk);
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void rose_timer_expiry(struct timer_list *t)
{
	struct rose_sock *rose = from_timer(rose, t, timer);
	struct sock *sk = &rose->sock;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		sk_reset_timer(sk, &rose->timer, jiffies + HZ/20);
		goto out;
	}
	switch (rose->state) {
	case ROSE_STATE_1:	/* T1 */
	case ROSE_STATE_4:	/* T2 */
		rose_write_internal(sk, ROSE_CLEAR_REQUEST);
		rose->state = ROSE_STATE_2;
		rose_start_t3timer(sk);
		break;

	case ROSE_STATE_2:	/* T3 */
		rose->neighbour->use--;
		rose_disconnect(sk, ETIMEDOUT, -1, -1);
		break;

	case ROSE_STATE_3:	/* HB */
		if (rose->condition & ROSE_COND_ACK_PENDING) {
			rose->condition &= ~ROSE_COND_ACK_PENDING;
			rose_enquiry_response(sk);
		}
		break;
	}
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void rose_idletimer_expiry(struct timer_list *t)
{
	struct rose_sock *rose = from_timer(rose, t, idletimer);
	struct sock *sk = &rose->sock;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		sk_reset_timer(sk, &rose->idletimer, jiffies + HZ/20);
		goto out;
	}
	rose_clear_queues(sk);

	rose_write_internal(sk, ROSE_CLEAR_REQUEST);
	rose_sk(sk)->state = ROSE_STATE_2;

	rose_start_t3timer(sk);

	sk->sk_state     = TCP_CLOSE;
	sk->sk_err       = 0;
	sk->sk_shutdown |= SEND_SHUTDOWN;

	if (!sock_flag(sk, SOCK_DEAD)) {
		sk->sk_state_change(sk);
		sock_set_flag(sk, SOCK_DEAD);
	}
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}
