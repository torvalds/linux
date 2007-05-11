/*
 * Copyright 2006 IBM Corporation
 * IUCV protocol stack for Linux on zSeries
 * Version 1.0
 * Author(s): Jennifer Hunt <jenhunt@us.ibm.com>
 *
 */

#ifndef __AFIUCV_H
#define __AFIUCV_H

#include <asm/types.h>
#include <asm/byteorder.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/socket.h>

#ifndef AF_IUCV
#define AF_IUCV		32
#define PF_IUCV		AF_IUCV
#endif

/* Connection and socket states */
enum {
	IUCV_CONNECTED = 1,
	IUCV_OPEN,
	IUCV_BOUND,
	IUCV_LISTEN,
	IUCV_SEVERED,
	IUCV_DISCONN,
	IUCV_CLOSING,
	IUCV_CLOSED
};

#define IUCV_QUEUELEN_DEFAULT	65535
#define IUCV_CONN_TIMEOUT	(HZ * 40)
#define IUCV_DISCONN_TIMEOUT	(HZ * 2)
#define IUCV_CONN_IDLE_TIMEOUT	(HZ * 60)
#define IUCV_BUFSIZE_DEFAULT	32768

/* IUCV socket address */
struct sockaddr_iucv {
	sa_family_t	siucv_family;
	unsigned short	siucv_port;		/* Reserved */
	unsigned int	siucv_addr;		/* Reserved */
	char		siucv_nodeid[8];	/* Reserved */
	char		siucv_user_id[8];	/* Guest User Id */
	char		siucv_name[8];		/* Application Name */
};


/* Common socket structures and functions */

#define iucv_sk(__sk) ((struct iucv_sock *) __sk)

struct iucv_sock {
	struct sock		sk;
	char			src_user_id[8];
	char			src_name[8];
	char			dst_user_id[8];
	char			dst_name[8];
	struct list_head	accept_q;
	struct sock		*parent;
	struct iucv_path	*path;
	struct sk_buff_head	send_skb_q;
	struct sk_buff_head	backlog_skb_q;
	unsigned int		send_tag;
};

struct iucv_sock_list {
	struct hlist_head head;
	rwlock_t	  lock;
	atomic_t	  autobind_name;
};

static void iucv_sock_destruct(struct sock *sk);
static void iucv_sock_cleanup_listen(struct sock *parent);
static void iucv_sock_kill(struct sock *sk);
static void iucv_sock_close(struct sock *sk);
static int  iucv_sock_create(struct socket *sock, int proto);
static int  iucv_sock_bind(struct socket *sock, struct sockaddr *addr,
			int addr_len);
static int  iucv_sock_connect(struct socket *sock, struct sockaddr *addr,
			      int alen, int flags);
static int  iucv_sock_listen(struct socket *sock, int backlog);
static int  iucv_sock_accept(struct socket *sock, struct socket *newsock,
			     int flags);
static int  iucv_sock_getname(struct socket *sock, struct sockaddr *addr,
			      int *len, int peer);
static int  iucv_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
			      struct msghdr *msg, size_t len);
static int  iucv_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
			      struct msghdr *msg, size_t len, int flags);
unsigned int iucv_sock_poll(struct file *file, struct socket *sock,
			    poll_table *wait);
static int iucv_sock_release(struct socket *sock);
static int iucv_sock_shutdown(struct socket *sock, int how);

void iucv_sock_link(struct iucv_sock_list *l, struct sock *s);
void iucv_sock_unlink(struct iucv_sock_list *l, struct sock *s);
int  iucv_sock_wait_state(struct sock *sk, int state, int state2,
			  unsigned long timeo);
int  iucv_sock_wait_cnt(struct sock *sk, unsigned long timeo);
void iucv_accept_enqueue(struct sock *parent, struct sock *sk);
void iucv_accept_unlink(struct sock *sk);
struct sock *iucv_accept_dequeue(struct sock *parent, struct socket *newsock);

#endif /* __IUCV_H */
