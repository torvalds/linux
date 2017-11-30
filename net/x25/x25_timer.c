/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine,
 *	randomly fail to work with new releases, misbehave and/or generally
 *	screw up. It might even work.
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	X.25 001	Jonathan Naylor	Started coding.
 *	X.25 002	Jonathan Naylor	New timer architecture.
 *					Centralised disconnection processing.
 */

#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/x25.h>

static void x25_heartbeat_expiry(struct timer_list *t);
static void x25_timer_expiry(struct timer_list *t);

void x25_init_timers(struct sock *sk)
{
	struct x25_sock *x25 = x25_sk(sk);

	timer_setup(&x25->timer, x25_timer_expiry, 0);

	/* initialized by sock_init_data */
	sk->sk_timer.function = x25_heartbeat_expiry;
}

void x25_start_heartbeat(struct sock *sk)
{
	mod_timer(&sk->sk_timer, jiffies + 5 * HZ);
}

void x25_stop_heartbeat(struct sock *sk)
{
	del_timer(&sk->sk_timer);
}

void x25_start_t2timer(struct sock *sk)
{
	struct x25_sock *x25 = x25_sk(sk);

	mod_timer(&x25->timer, jiffies + x25->t2);
}

void x25_start_t21timer(struct sock *sk)
{
	struct x25_sock *x25 = x25_sk(sk);

	mod_timer(&x25->timer, jiffies + x25->t21);
}

void x25_start_t22timer(struct sock *sk)
{
	struct x25_sock *x25 = x25_sk(sk);

	mod_timer(&x25->timer, jiffies + x25->t22);
}

void x25_start_t23timer(struct sock *sk)
{
	struct x25_sock *x25 = x25_sk(sk);

	mod_timer(&x25->timer, jiffies + x25->t23);
}

void x25_stop_timer(struct sock *sk)
{
	del_timer(&x25_sk(sk)->timer);
}

unsigned long x25_display_timer(struct sock *sk)
{
	struct x25_sock *x25 = x25_sk(sk);

	if (!timer_pending(&x25->timer))
		return 0;

	return x25->timer.expires - jiffies;
}

static void x25_heartbeat_expiry(struct timer_list *t)
{
	struct sock *sk = from_timer(sk, t, sk_timer);

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) /* can currently only occur in state 3 */
		goto restart_heartbeat;

	switch (x25_sk(sk)->state) {

		case X25_STATE_0:
			/*
			 * Magic here: If we listen() and a new link dies
			 * before it is accepted() it isn't 'dead' so doesn't
			 * get removed.
			 */
			if (sock_flag(sk, SOCK_DESTROY) ||
			    (sk->sk_state == TCP_LISTEN &&
			     sock_flag(sk, SOCK_DEAD))) {
				bh_unlock_sock(sk);
				x25_destroy_socket_from_timer(sk);
				return;
			}
			break;

		case X25_STATE_3:
			/*
			 * Check for the state of the receive buffer.
			 */
			x25_check_rbuf(sk);
			break;
	}
restart_heartbeat:
	x25_start_heartbeat(sk);
	bh_unlock_sock(sk);
}

/*
 *	Timer has expired, it may have been T2, T21, T22, or T23. We can tell
 *	by the state machine state.
 */
static inline void x25_do_timer_expiry(struct sock * sk)
{
	struct x25_sock *x25 = x25_sk(sk);

	switch (x25->state) {

		case X25_STATE_3:	/* T2 */
			if (x25->condition & X25_COND_ACK_PENDING) {
				x25->condition &= ~X25_COND_ACK_PENDING;
				x25_enquiry_response(sk);
			}
			break;

		case X25_STATE_1:	/* T21 */
		case X25_STATE_4:	/* T22 */
			x25_write_internal(sk, X25_CLEAR_REQUEST);
			x25->state = X25_STATE_2;
			x25_start_t23timer(sk);
			break;

		case X25_STATE_2:	/* T23 */
			x25_disconnect(sk, ETIMEDOUT, 0, 0);
			break;
	}
}

static void x25_timer_expiry(struct timer_list *t)
{
	struct x25_sock *x25 = from_timer(x25, t, timer);
	struct sock *sk = &x25->sk;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) { /* can currently only occur in state 3 */
		if (x25_sk(sk)->state == X25_STATE_3)
			x25_start_t2timer(sk);
	} else
		x25_do_timer_expiry(sk);
	bh_unlock_sock(sk);
}
