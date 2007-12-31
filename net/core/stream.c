/*
 *     SUCS NET3:
 *
 *     Generic stream handling routines. These are generic for most
 *     protocols. Even IP. Tonight 8-).
 *     This is used because TCP, LLC (others too) layer all have mostly
 *     identical sendmsg() and recvmsg() code.
 *     So we (will) share it here.
 *
 *     Authors:        Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *                     (from old tcp.c code)
 *                     Alan Cox <alan@redhat.com> (Borrowed comments 8-))
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/signal.h>
#include <linux/tcp.h>
#include <linux/wait.h>
#include <net/sock.h>

/**
 * sk_stream_write_space - stream socket write_space callback.
 * @sk: socket
 *
 * FIXME: write proper description
 */
void sk_stream_write_space(struct sock *sk)
{
	struct socket *sock = sk->sk_socket;

	if (sk_stream_wspace(sk) >= sk_stream_min_wspace(sk) && sock) {
		clear_bit(SOCK_NOSPACE, &sock->flags);

		if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
			wake_up_interruptible(sk->sk_sleep);
		if (sock->fasync_list && !(sk->sk_shutdown & SEND_SHUTDOWN))
			sock_wake_async(sock, SOCK_WAKE_SPACE, POLL_OUT);
	}
}

EXPORT_SYMBOL(sk_stream_write_space);

/**
 * sk_stream_wait_connect - Wait for a socket to get into the connected state
 * @sk: sock to wait on
 * @timeo_p: for how long to wait
 *
 * Must be called with the socket locked.
 */
int sk_stream_wait_connect(struct sock *sk, long *timeo_p)
{
	struct task_struct *tsk = current;
	DEFINE_WAIT(wait);
	int done;

	do {
		int err = sock_error(sk);
		if (err)
			return err;
		if ((1 << sk->sk_state) & ~(TCPF_SYN_SENT | TCPF_SYN_RECV))
			return -EPIPE;
		if (!*timeo_p)
			return -EAGAIN;
		if (signal_pending(tsk))
			return sock_intr_errno(*timeo_p);

		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);
		sk->sk_write_pending++;
		done = sk_wait_event(sk, timeo_p,
				     !sk->sk_err &&
				     !((1 << sk->sk_state) &
				       ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)));
		finish_wait(sk->sk_sleep, &wait);
		sk->sk_write_pending--;
	} while (!done);
	return 0;
}

EXPORT_SYMBOL(sk_stream_wait_connect);

/**
 * sk_stream_closing - Return 1 if we still have things to send in our buffers.
 * @sk: socket to verify
 */
static inline int sk_stream_closing(struct sock *sk)
{
	return (1 << sk->sk_state) &
	       (TCPF_FIN_WAIT1 | TCPF_CLOSING | TCPF_LAST_ACK);
}

void sk_stream_wait_close(struct sock *sk, long timeout)
{
	if (timeout) {
		DEFINE_WAIT(wait);

		do {
			prepare_to_wait(sk->sk_sleep, &wait,
					TASK_INTERRUPTIBLE);
			if (sk_wait_event(sk, &timeout, !sk_stream_closing(sk)))
				break;
		} while (!signal_pending(current) && timeout);

		finish_wait(sk->sk_sleep, &wait);
	}
}

EXPORT_SYMBOL(sk_stream_wait_close);

/**
 * sk_stream_wait_memory - Wait for more memory for a socket
 * @sk: socket to wait for memory
 * @timeo_p: for how long
 */
int sk_stream_wait_memory(struct sock *sk, long *timeo_p)
{
	int err = 0;
	long vm_wait = 0;
	long current_timeo = *timeo_p;
	DEFINE_WAIT(wait);

	if (sk_stream_memory_free(sk))
		current_timeo = vm_wait = (net_random() % (HZ / 5)) + 2;

	while (1) {
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);

		if (sk->sk_err || (sk->sk_shutdown & SEND_SHUTDOWN))
			goto do_error;
		if (!*timeo_p)
			goto do_nonblock;
		if (signal_pending(current))
			goto do_interrupted;
		clear_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);
		if (sk_stream_memory_free(sk) && !vm_wait)
			break;

		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		sk->sk_write_pending++;
		sk_wait_event(sk, &current_timeo, !sk->sk_err &&
						  !(sk->sk_shutdown & SEND_SHUTDOWN) &&
						  sk_stream_memory_free(sk) &&
						  vm_wait);
		sk->sk_write_pending--;

		if (vm_wait) {
			vm_wait -= current_timeo;
			current_timeo = *timeo_p;
			if (current_timeo != MAX_SCHEDULE_TIMEOUT &&
			    (current_timeo -= vm_wait) < 0)
				current_timeo = 0;
			vm_wait = 0;
		}
		*timeo_p = current_timeo;
	}
out:
	finish_wait(sk->sk_sleep, &wait);
	return err;

do_error:
	err = -EPIPE;
	goto out;
do_nonblock:
	err = -EAGAIN;
	goto out;
do_interrupted:
	err = sock_intr_errno(*timeo_p);
	goto out;
}

EXPORT_SYMBOL(sk_stream_wait_memory);

int sk_stream_error(struct sock *sk, int flags, int err)
{
	if (err == -EPIPE)
		err = sock_error(sk) ? : -EPIPE;
	if (err == -EPIPE && !(flags & MSG_NOSIGNAL))
		send_sig(SIGPIPE, current, 0);
	return err;
}

EXPORT_SYMBOL(sk_stream_error);

void sk_stream_kill_queues(struct sock *sk)
{
	/* First the read buffer. */
	__skb_queue_purge(&sk->sk_receive_queue);

	/* Next, the error queue. */
	__skb_queue_purge(&sk->sk_error_queue);

	/* Next, the write queue. */
	BUG_TRAP(skb_queue_empty(&sk->sk_write_queue));

	/* Account for returned memory. */
	sk_mem_reclaim(sk);

	BUG_TRAP(!sk->sk_wmem_queued);
	BUG_TRAP(!sk->sk_forward_alloc);

	/* It is _impossible_ for the backlog to contain anything
	 * when we get here.  All user references to this socket
	 * have gone away, only the net layer knows can touch it.
	 */
}

EXPORT_SYMBOL(sk_stream_kill_queues);
