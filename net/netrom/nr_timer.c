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
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/netrom.h>

static void nr_heartbeat_expiry(struct timer_list *);
static void nr_t1timer_expiry(struct timer_list *);
static void nr_t2timer_expiry(struct timer_list *);
static void nr_t4timer_expiry(struct timer_list *);
static void nr_idletimer_expiry(struct timer_list *);

void nr_init_timers(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	timer_setup(&nr->t1timer, nr_t1timer_expiry, 0);
	timer_setup(&nr->t2timer, nr_t2timer_expiry, 0);
	timer_setup(&nr->t4timer, nr_t4timer_expiry, 0);
	timer_setup(&nr->idletimer, nr_idletimer_expiry, 0);

	/* initialized by sock_init_data */
	sk->sk_timer.function = nr_heartbeat_expiry;
}

void nr_start_t1timer(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	sk_reset_timer(sk, &nr->t1timer, jiffies + nr->t1);
}

void nr_start_t2timer(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	sk_reset_timer(sk, &nr->t2timer, jiffies + nr->t2);
}

void nr_start_t4timer(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	sk_reset_timer(sk, &nr->t4timer, jiffies + nr->t4);
}

void nr_start_idletimer(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	if (nr->idle > 0)
		sk_reset_timer(sk, &nr->idletimer, jiffies + nr->idle);
}

void nr_start_heartbeat(struct sock *sk)
{
	sk_reset_timer(sk, &sk->sk_timer, jiffies + 5 * HZ);
}

void nr_stop_t1timer(struct sock *sk)
{
	sk_stop_timer(sk, &nr_sk(sk)->t1timer);
}

void nr_stop_t2timer(struct sock *sk)
{
	sk_stop_timer(sk, &nr_sk(sk)->t2timer);
}

void nr_stop_t4timer(struct sock *sk)
{
	sk_stop_timer(sk, &nr_sk(sk)->t4timer);
}

void nr_stop_idletimer(struct sock *sk)
{
	sk_stop_timer(sk, &nr_sk(sk)->idletimer);
}

void nr_stop_heartbeat(struct sock *sk)
{
	sk_stop_timer(sk, &sk->sk_timer);
}

int nr_t1timer_running(struct sock *sk)
{
	return timer_pending(&nr_sk(sk)->t1timer);
}

static void nr_heartbeat_expiry(struct timer_list *t)
{
	struct sock *sk = from_timer(sk, t, sk_timer);
	struct nr_sock *nr = nr_sk(sk);

	bh_lock_sock(sk);
	switch (nr->state) {
	case NR_STATE_0:
		/* Magic here: If we listen() and a new link dies before it
		   is accepted() it isn't 'dead' so doesn't get removed. */
		if (sock_flag(sk, SOCK_DESTROY) ||
		    (sk->sk_state == TCP_LISTEN && sock_flag(sk, SOCK_DEAD))) {
			if (sk->sk_state == TCP_LISTEN)
				sock_hold(sk);
			bh_unlock_sock(sk);
			nr_destroy_socket(sk);
			goto out;
		}
		break;

	case NR_STATE_3:
		/*
		 * Check for the state of the receive buffer.
		 */
		if (atomic_read(&sk->sk_rmem_alloc) < (sk->sk_rcvbuf / 2) &&
		    (nr->condition & NR_COND_OWN_RX_BUSY)) {
			nr->condition &= ~NR_COND_OWN_RX_BUSY;
			nr->condition &= ~NR_COND_ACK_PENDING;
			nr->vl         = nr->vr;
			nr_write_internal(sk, NR_INFOACK);
			break;
		}
		break;
	}

	nr_start_heartbeat(sk);
	bh_unlock_sock(sk);
out:
	sock_put(sk);
}

static void nr_t2timer_expiry(struct timer_list *t)
{
	struct nr_sock *nr = from_timer(nr, t, t2timer);
	struct sock *sk = &nr->sock;

	bh_lock_sock(sk);
	if (nr->condition & NR_COND_ACK_PENDING) {
		nr->condition &= ~NR_COND_ACK_PENDING;
		nr_enquiry_response(sk);
	}
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void nr_t4timer_expiry(struct timer_list *t)
{
	struct nr_sock *nr = from_timer(nr, t, t4timer);
	struct sock *sk = &nr->sock;

	bh_lock_sock(sk);
	nr_sk(sk)->condition &= ~NR_COND_PEER_RX_BUSY;
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void nr_idletimer_expiry(struct timer_list *t)
{
	struct nr_sock *nr = from_timer(nr, t, idletimer);
	struct sock *sk = &nr->sock;

	bh_lock_sock(sk);

	nr_clear_queues(sk);

	nr->n2count = 0;
	nr_write_internal(sk, NR_DISCREQ);
	nr->state = NR_STATE_2;

	nr_start_t1timer(sk);
	nr_stop_t2timer(sk);
	nr_stop_t4timer(sk);

	sk->sk_state     = TCP_CLOSE;
	sk->sk_err       = 0;
	sk->sk_shutdown |= SEND_SHUTDOWN;

	if (!sock_flag(sk, SOCK_DEAD)) {
		sk->sk_state_change(sk);
		sock_set_flag(sk, SOCK_DEAD);
	}
	bh_unlock_sock(sk);
	sock_put(sk);
}

static void nr_t1timer_expiry(struct timer_list *t)
{
	struct nr_sock *nr = from_timer(nr, t, t1timer);
	struct sock *sk = &nr->sock;

	bh_lock_sock(sk);
	switch (nr->state) {
	case NR_STATE_1:
		if (nr->n2count == nr->n2) {
			nr_disconnect(sk, ETIMEDOUT);
			goto out;
		} else {
			nr->n2count++;
			nr_write_internal(sk, NR_CONNREQ);
		}
		break;

	case NR_STATE_2:
		if (nr->n2count == nr->n2) {
			nr_disconnect(sk, ETIMEDOUT);
			goto out;
		} else {
			nr->n2count++;
			nr_write_internal(sk, NR_DISCREQ);
		}
		break;

	case NR_STATE_3:
		if (nr->n2count == nr->n2) {
			nr_disconnect(sk, ETIMEDOUT);
			goto out;
		} else {
			nr->n2count++;
			nr_requeue_frames(sk);
		}
		break;
	}

	nr_start_t1timer(sk);
out:
	bh_unlock_sock(sk);
	sock_put(sk);
}
