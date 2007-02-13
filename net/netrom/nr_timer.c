/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/netrom.h>

static void nr_heartbeat_expiry(unsigned long);
static void nr_t1timer_expiry(unsigned long);
static void nr_t2timer_expiry(unsigned long);
static void nr_t4timer_expiry(unsigned long);
static void nr_idletimer_expiry(unsigned long);

void nr_init_timers(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	init_timer(&nr->t1timer);
	nr->t1timer.data     = (unsigned long)sk;
	nr->t1timer.function = &nr_t1timer_expiry;

	init_timer(&nr->t2timer);
	nr->t2timer.data     = (unsigned long)sk;
	nr->t2timer.function = &nr_t2timer_expiry;

	init_timer(&nr->t4timer);
	nr->t4timer.data     = (unsigned long)sk;
	nr->t4timer.function = &nr_t4timer_expiry;

	init_timer(&nr->idletimer);
	nr->idletimer.data     = (unsigned long)sk;
	nr->idletimer.function = &nr_idletimer_expiry;

	/* initialized by sock_init_data */
	sk->sk_timer.data     = (unsigned long)sk;
	sk->sk_timer.function = &nr_heartbeat_expiry;
}

void nr_start_t1timer(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	mod_timer(&nr->t1timer, jiffies + nr->t1);
}

void nr_start_t2timer(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	mod_timer(&nr->t2timer, jiffies + nr->t2);
}

void nr_start_t4timer(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	mod_timer(&nr->t4timer, jiffies + nr->t4);
}

void nr_start_idletimer(struct sock *sk)
{
	struct nr_sock *nr = nr_sk(sk);

	if (nr->idle > 0)
		mod_timer(&nr->idletimer, jiffies + nr->idle);
}

void nr_start_heartbeat(struct sock *sk)
{
	mod_timer(&sk->sk_timer, jiffies + 5 * HZ);
}

void nr_stop_t1timer(struct sock *sk)
{
	del_timer(&nr_sk(sk)->t1timer);
}

void nr_stop_t2timer(struct sock *sk)
{
	del_timer(&nr_sk(sk)->t2timer);
}

void nr_stop_t4timer(struct sock *sk)
{
	del_timer(&nr_sk(sk)->t4timer);
}

void nr_stop_idletimer(struct sock *sk)
{
	del_timer(&nr_sk(sk)->idletimer);
}

void nr_stop_heartbeat(struct sock *sk)
{
	del_timer(&sk->sk_timer);
}

int nr_t1timer_running(struct sock *sk)
{
	return timer_pending(&nr_sk(sk)->t1timer);
}

static void nr_heartbeat_expiry(unsigned long param)
{
	struct sock *sk = (struct sock *)param;
	struct nr_sock *nr = nr_sk(sk);

	bh_lock_sock(sk);
	switch (nr->state) {
	case NR_STATE_0:
		/* Magic here: If we listen() and a new link dies before it
		   is accepted() it isn't 'dead' so doesn't get removed. */
		if (sock_flag(sk, SOCK_DESTROY) ||
		    (sk->sk_state == TCP_LISTEN && sock_flag(sk, SOCK_DEAD))) {
			sock_hold(sk);
			bh_unlock_sock(sk);
			nr_destroy_socket(sk);
			sock_put(sk);
			return;
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
}

static void nr_t2timer_expiry(unsigned long param)
{
	struct sock *sk = (struct sock *)param;
	struct nr_sock *nr = nr_sk(sk);

	bh_lock_sock(sk);
	if (nr->condition & NR_COND_ACK_PENDING) {
		nr->condition &= ~NR_COND_ACK_PENDING;
		nr_enquiry_response(sk);
	}
	bh_unlock_sock(sk);
}

static void nr_t4timer_expiry(unsigned long param)
{
	struct sock *sk = (struct sock *)param;

	bh_lock_sock(sk);
	nr_sk(sk)->condition &= ~NR_COND_PEER_RX_BUSY;
	bh_unlock_sock(sk);
}

static void nr_idletimer_expiry(unsigned long param)
{
	struct sock *sk = (struct sock *)param;
	struct nr_sock *nr = nr_sk(sk);

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
}

static void nr_t1timer_expiry(unsigned long param)
{
	struct sock *sk = (struct sock *)param;
	struct nr_sock *nr = nr_sk(sk);

	bh_lock_sock(sk);
	switch (nr->state) {
	case NR_STATE_1:
		if (nr->n2count == nr->n2) {
			nr_disconnect(sk, ETIMEDOUT);
			bh_unlock_sock(sk);
			return;
		} else {
			nr->n2count++;
			nr_write_internal(sk, NR_CONNREQ);
		}
		break;

	case NR_STATE_2:
		if (nr->n2count == nr->n2) {
			nr_disconnect(sk, ETIMEDOUT);
			bh_unlock_sock(sk);
			return;
		} else {
			nr->n2count++;
			nr_write_internal(sk, NR_DISCREQ);
		}
		break;

	case NR_STATE_3:
		if (nr->n2count == nr->n2) {
			nr_disconnect(sk, ETIMEDOUT);
			bh_unlock_sock(sk);
			return;
		} else {
			nr->n2count++;
			nr_requeue_frames(sk);
		}
		break;
	}

	nr_start_t1timer(sk);
	bh_unlock_sock(sk);
}
