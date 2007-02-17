/* internal.h: internal Rx RPC stuff
 *
 * Copyright (c) 2002   David Howells (dhowells@redhat.com).
 */

#ifndef RXRPC_INTERNAL_H
#define RXRPC_INTERNAL_H

#include <linux/compiler.h>
#include <linux/kernel.h>

/*
 * debug accounting
 */
#if 1
#define __RXACCT_DECL(X) X
#define __RXACCT(X) do { X; } while(0)
#else
#define __RXACCT_DECL(X)
#define __RXACCT(X) do { } while(0)
#endif

__RXACCT_DECL(extern atomic_t rxrpc_transport_count);
__RXACCT_DECL(extern atomic_t rxrpc_peer_count);
__RXACCT_DECL(extern atomic_t rxrpc_connection_count);
__RXACCT_DECL(extern atomic_t rxrpc_call_count);
__RXACCT_DECL(extern atomic_t rxrpc_message_count);

/*
 * debug tracing
 */
#define kenter(FMT, a...)	printk("==> %s("FMT")\n",__FUNCTION__ , ##a)
#define kleave(FMT, a...)	printk("<== %s()"FMT"\n",__FUNCTION__ , ##a)
#define kdebug(FMT, a...)	printk("    "FMT"\n" , ##a)
#define kproto(FMT, a...)	printk("### "FMT"\n" , ##a)
#define knet(FMT, a...)		printk("    "FMT"\n" , ##a)

#if 0
#define _enter(FMT, a...)	kenter(FMT , ##a)
#define _leave(FMT, a...)	kleave(FMT , ##a)
#define _debug(FMT, a...)	kdebug(FMT , ##a)
#define _proto(FMT, a...)	kproto(FMT , ##a)
#define _net(FMT, a...)		knet(FMT , ##a)
#else
#define _enter(FMT, a...)	do { if (rxrpc_ktrace) kenter(FMT , ##a); } while(0)
#define _leave(FMT, a...)	do { if (rxrpc_ktrace) kleave(FMT , ##a); } while(0)
#define _debug(FMT, a...)	do { if (rxrpc_kdebug) kdebug(FMT , ##a); } while(0)
#define _proto(FMT, a...)	do { if (rxrpc_kproto) kproto(FMT , ##a); } while(0)
#define _net(FMT, a...)		do { if (rxrpc_knet)   knet  (FMT , ##a); } while(0)
#endif

static inline void rxrpc_discard_my_signals(void)
{
	while (signal_pending(current)) {
		siginfo_t sinfo;

		spin_lock_irq(&current->sighand->siglock);
		dequeue_signal(current, &current->blocked, &sinfo);
		spin_unlock_irq(&current->sighand->siglock);
	}
}

/*
 * call.c
 */
extern struct list_head rxrpc_calls;
extern struct rw_semaphore rxrpc_calls_sem;

/*
 * connection.c
 */
extern struct list_head rxrpc_conns;
extern struct rw_semaphore rxrpc_conns_sem;
extern unsigned long rxrpc_conn_timeout;

extern void rxrpc_conn_clearall(struct rxrpc_peer *peer);

/*
 * peer.c
 */
extern struct list_head rxrpc_peers;
extern struct rw_semaphore rxrpc_peers_sem;
extern unsigned long rxrpc_peer_timeout;

extern void rxrpc_peer_calculate_rtt(struct rxrpc_peer *peer,
				     struct rxrpc_message *msg,
				     struct rxrpc_message *resp);

extern void rxrpc_peer_clearall(struct rxrpc_transport *trans);


/*
 * proc.c
 */
#ifdef CONFIG_PROC_FS
extern int rxrpc_proc_init(void);
extern void rxrpc_proc_cleanup(void);
#endif

/*
 * transport.c
 */
extern struct list_head rxrpc_proc_transports;
extern struct rw_semaphore rxrpc_proc_transports_sem;

#endif /* RXRPC_INTERNAL_H */
