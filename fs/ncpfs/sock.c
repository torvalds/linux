/*
 *  linux/fs/ncpfs/sock.c
 *
 *  Copyright (C) 1992, 1993  Rick Sladkey
 *
 *  Modified 1995, 1996 by Volker Lendecke to be usable for ncp
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *
 */


#include <linux/time.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <asm/uaccess.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/signal.h>
#include <net/scm.h>
#include <net/sock.h>
#include <linux/ipx.h>
#include <linux/poll.h>
#include <linux/file.h>

#include <linux/ncp_fs.h>

#include "ncpsign_kernel.h"

static int _recv(struct socket *sock, void *buf, int size, unsigned flags)
{
	struct msghdr msg = {NULL, };
	struct kvec iov = {buf, size};
	return kernel_recvmsg(sock, &msg, &iov, 1, size, flags);
}

static inline int do_send(struct socket *sock, struct kvec *vec, int count,
			  int len, unsigned flags)
{
	struct msghdr msg = { .msg_flags = flags };
	return kernel_sendmsg(sock, &msg, vec, count, len);
}

static int _send(struct socket *sock, const void *buff, int len)
{
	struct kvec vec;
	vec.iov_base = (void *) buff;
	vec.iov_len = len;
	return do_send(sock, &vec, 1, len, 0);
}

struct ncp_request_reply {
	struct list_head req;
	wait_queue_head_t wq;
	struct ncp_reply_header* reply_buf;
	size_t datalen;
	int result;
	enum { RQ_DONE, RQ_INPROGRESS, RQ_QUEUED, RQ_IDLE } status;
	struct kvec* tx_ciov;
	size_t tx_totallen;
	size_t tx_iovlen;
	struct kvec tx_iov[3];
	u_int16_t tx_type;
	u_int32_t sign[6];
};

void ncp_tcp_data_ready(struct sock *sk, int len)
{
	struct ncp_server *server = sk->sk_user_data;

	server->data_ready(sk, len);
	schedule_work(&server->rcv.tq);
}

void ncp_tcp_error_report(struct sock *sk)
{
	struct ncp_server *server = sk->sk_user_data;
	
	server->error_report(sk);
	schedule_work(&server->rcv.tq);
}

void ncp_tcp_write_space(struct sock *sk)
{
	struct ncp_server *server = sk->sk_user_data;
	
	/* We do not need any locking: we first set tx.creq, and then we do sendmsg,
	   not vice versa... */
	server->write_space(sk);
	if (server->tx.creq)
		schedule_work(&server->tx.tq);
}

void ncpdgram_timeout_call(unsigned long v)
{
	struct ncp_server *server = (void*)v;
	
	schedule_work(&server->timeout_tq);
}

static inline void ncp_finish_request(struct ncp_request_reply *req, int result)
{
	req->result = result;
	req->status = RQ_DONE;
	wake_up_all(&req->wq);
}

static void __abort_ncp_connection(struct ncp_server *server, struct ncp_request_reply *aborted, int err)
{
	struct ncp_request_reply *req;

	ncp_invalidate_conn(server);
	del_timer(&server->timeout_tm);
	while (!list_empty(&server->tx.requests)) {
		req = list_entry(server->tx.requests.next, struct ncp_request_reply, req);
		
		list_del_init(&req->req);
		if (req == aborted) {
			ncp_finish_request(req, err);
		} else {
			ncp_finish_request(req, -EIO);
		}
	}
	req = server->rcv.creq;
	if (req) {
		server->rcv.creq = NULL;
		if (req == aborted) {
			ncp_finish_request(req, err);
		} else {
			ncp_finish_request(req, -EIO);
		}
		server->rcv.ptr = NULL;
		server->rcv.state = 0;
	}
	req = server->tx.creq;
	if (req) {
		server->tx.creq = NULL;
		if (req == aborted) {
			ncp_finish_request(req, err);
		} else {
			ncp_finish_request(req, -EIO);
		}
	}
}

static inline int get_conn_number(struct ncp_reply_header *rp)
{
	return rp->conn_low | (rp->conn_high << 8);
}

static inline void __ncp_abort_request(struct ncp_server *server, struct ncp_request_reply *req, int err)
{
	/* If req is done, we got signal, but we also received answer... */
	switch (req->status) {
		case RQ_IDLE:
		case RQ_DONE:
			break;
		case RQ_QUEUED:
			list_del_init(&req->req);
			ncp_finish_request(req, err);
			break;
		case RQ_INPROGRESS:
			__abort_ncp_connection(server, req, err);
			break;
	}
}

static inline void ncp_abort_request(struct ncp_server *server, struct ncp_request_reply *req, int err)
{
	mutex_lock(&server->rcv.creq_mutex);
	__ncp_abort_request(server, req, err);
	mutex_unlock(&server->rcv.creq_mutex);
}

static inline void __ncptcp_abort(struct ncp_server *server)
{
	__abort_ncp_connection(server, NULL, 0);
}

static int ncpdgram_send(struct socket *sock, struct ncp_request_reply *req)
{
	struct kvec vec[3];
	/* sock_sendmsg updates iov pointers for us :-( */
	memcpy(vec, req->tx_ciov, req->tx_iovlen * sizeof(vec[0]));
	return do_send(sock, vec, req->tx_iovlen,
		       req->tx_totallen, MSG_DONTWAIT);
}

static void __ncptcp_try_send(struct ncp_server *server)
{
	struct ncp_request_reply *rq;
	struct kvec *iov;
	struct kvec iovc[3];
	int result;

	rq = server->tx.creq;
	if (!rq)
		return;

	/* sock_sendmsg updates iov pointers for us :-( */
	memcpy(iovc, rq->tx_ciov, rq->tx_iovlen * sizeof(iov[0]));
	result = do_send(server->ncp_sock, iovc, rq->tx_iovlen,
			 rq->tx_totallen, MSG_NOSIGNAL | MSG_DONTWAIT);

	if (result == -EAGAIN)
		return;

	if (result < 0) {
		printk(KERN_ERR "ncpfs: tcp: Send failed: %d\n", result);
		__ncp_abort_request(server, rq, result);
		return;
	}
	if (result >= rq->tx_totallen) {
		server->rcv.creq = rq;
		server->tx.creq = NULL;
		return;
	}
	rq->tx_totallen -= result;
	iov = rq->tx_ciov;
	while (iov->iov_len <= result) {
		result -= iov->iov_len;
		iov++;
		rq->tx_iovlen--;
	}
	iov->iov_base += result;
	iov->iov_len -= result;
	rq->tx_ciov = iov;
}

static inline void ncp_init_header(struct ncp_server *server, struct ncp_request_reply *req, struct ncp_request_header *h)
{
	req->status = RQ_INPROGRESS;
	h->conn_low = server->connection;
	h->conn_high = server->connection >> 8;
	h->sequence = ++server->sequence;
}
	
static void ncpdgram_start_request(struct ncp_server *server, struct ncp_request_reply *req)
{
	size_t signlen;
	struct ncp_request_header* h;
	
	req->tx_ciov = req->tx_iov + 1;

	h = req->tx_iov[1].iov_base;
	ncp_init_header(server, req, h);
	signlen = sign_packet(server, req->tx_iov[1].iov_base + sizeof(struct ncp_request_header) - 1, 
			req->tx_iov[1].iov_len - sizeof(struct ncp_request_header) + 1,
			cpu_to_le32(req->tx_totallen), req->sign);
	if (signlen) {
		req->tx_ciov[1].iov_base = req->sign;
		req->tx_ciov[1].iov_len = signlen;
		req->tx_iovlen += 1;
		req->tx_totallen += signlen;
	}
	server->rcv.creq = req;
	server->timeout_last = server->m.time_out;
	server->timeout_retries = server->m.retry_count;
	ncpdgram_send(server->ncp_sock, req);
	mod_timer(&server->timeout_tm, jiffies + server->m.time_out);
}

#define NCP_TCP_XMIT_MAGIC	(0x446D6454)
#define NCP_TCP_XMIT_VERSION	(1)
#define NCP_TCP_RCVD_MAGIC	(0x744E6350)

static void ncptcp_start_request(struct ncp_server *server, struct ncp_request_reply *req)
{
	size_t signlen;
	struct ncp_request_header* h;

	req->tx_ciov = req->tx_iov;
	h = req->tx_iov[1].iov_base;
	ncp_init_header(server, req, h);
	signlen = sign_packet(server, req->tx_iov[1].iov_base + sizeof(struct ncp_request_header) - 1,
			req->tx_iov[1].iov_len - sizeof(struct ncp_request_header) + 1,
			cpu_to_be32(req->tx_totallen + 24), req->sign + 4) + 16;

	req->sign[0] = htonl(NCP_TCP_XMIT_MAGIC);
	req->sign[1] = htonl(req->tx_totallen + signlen);
	req->sign[2] = htonl(NCP_TCP_XMIT_VERSION);
	req->sign[3] = htonl(req->datalen + 8);
	req->tx_iov[0].iov_base = req->sign;
	req->tx_iov[0].iov_len = signlen;
	req->tx_iovlen += 1;
	req->tx_totallen += signlen;

	server->tx.creq = req;
	__ncptcp_try_send(server);
}

static inline void __ncp_start_request(struct ncp_server *server, struct ncp_request_reply *req)
{
	if (server->ncp_sock->type == SOCK_STREAM)
		ncptcp_start_request(server, req);
	else
		ncpdgram_start_request(server, req);
}

static int ncp_add_request(struct ncp_server *server, struct ncp_request_reply *req)
{
	mutex_lock(&server->rcv.creq_mutex);
	if (!ncp_conn_valid(server)) {
		mutex_unlock(&server->rcv.creq_mutex);
		printk(KERN_ERR "ncpfs: tcp: Server died\n");
		return -EIO;
	}
	if (server->tx.creq || server->rcv.creq) {
		req->status = RQ_QUEUED;
		list_add_tail(&req->req, &server->tx.requests);
		mutex_unlock(&server->rcv.creq_mutex);
		return 0;
	}
	__ncp_start_request(server, req);
	mutex_unlock(&server->rcv.creq_mutex);
	return 0;
}

static void __ncp_next_request(struct ncp_server *server)
{
	struct ncp_request_reply *req;

	server->rcv.creq = NULL;
	if (list_empty(&server->tx.requests)) {
		return;
	}
	req = list_entry(server->tx.requests.next, struct ncp_request_reply, req);
	list_del_init(&req->req);
	__ncp_start_request(server, req);
}

static void info_server(struct ncp_server *server, unsigned int id, const void * data, size_t len)
{
	if (server->info_sock) {
		struct kvec iov[2];
		__be32 hdr[2];
	
		hdr[0] = cpu_to_be32(len + 8);
		hdr[1] = cpu_to_be32(id);
	
		iov[0].iov_base = hdr;
		iov[0].iov_len = 8;
		iov[1].iov_base = (void *) data;
		iov[1].iov_len = len;

		do_send(server->info_sock, iov, 2, len + 8, MSG_NOSIGNAL);
	}
}

void ncpdgram_rcv_proc(struct work_struct *work)
{
	struct ncp_server *server =
		container_of(work, struct ncp_server, rcv.tq);
	struct socket* sock;
	
	sock = server->ncp_sock;
	
	while (1) {
		struct ncp_reply_header reply;
		int result;

		result = _recv(sock, &reply, sizeof(reply), MSG_PEEK | MSG_DONTWAIT);
		if (result < 0) {
			break;
		}
		if (result >= sizeof(reply)) {
			struct ncp_request_reply *req;
	
			if (reply.type == NCP_WATCHDOG) {
				unsigned char buf[10];

				if (server->connection != get_conn_number(&reply)) {
					goto drop;
				}
				result = _recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
				if (result < 0) {
					DPRINTK("recv failed with %d\n", result);
					continue;
				}
				if (result < 10) {
					DPRINTK("too short (%u) watchdog packet\n", result);
					continue;
				}
				if (buf[9] != '?') {
					DPRINTK("bad signature (%02X) in watchdog packet\n", buf[9]);
					continue;
				}
				buf[9] = 'Y';
				_send(sock, buf, sizeof(buf));
				continue;
			}
			if (reply.type != NCP_POSITIVE_ACK && reply.type != NCP_REPLY) {
				result = _recv(sock, server->unexpected_packet.data, sizeof(server->unexpected_packet.data), MSG_DONTWAIT);
				if (result < 0) {
					continue;
				}
				info_server(server, 0, server->unexpected_packet.data, result);
				continue;
			}
			mutex_lock(&server->rcv.creq_mutex);
			req = server->rcv.creq;
			if (req && (req->tx_type == NCP_ALLOC_SLOT_REQUEST || (server->sequence == reply.sequence && 
					server->connection == get_conn_number(&reply)))) {
				if (reply.type == NCP_POSITIVE_ACK) {
					server->timeout_retries = server->m.retry_count;
					server->timeout_last = NCP_MAX_RPC_TIMEOUT;
					mod_timer(&server->timeout_tm, jiffies + NCP_MAX_RPC_TIMEOUT);
				} else if (reply.type == NCP_REPLY) {
					result = _recv(sock, (void*)req->reply_buf, req->datalen, MSG_DONTWAIT);
#ifdef CONFIG_NCPFS_PACKET_SIGNING
					if (result >= 0 && server->sign_active && req->tx_type != NCP_DEALLOC_SLOT_REQUEST) {
						if (result < 8 + 8) {
							result = -EIO;
						} else {
							unsigned int hdrl;
							
							result -= 8;
							hdrl = sock->sk->sk_family == AF_INET ? 8 : 6;
							if (sign_verify_reply(server, ((char*)req->reply_buf) + hdrl, result - hdrl, cpu_to_le32(result), ((char*)req->reply_buf) + result)) {
								printk(KERN_INFO "ncpfs: Signature violation\n");
								result = -EIO;
							}
						}
					}
#endif
					del_timer(&server->timeout_tm);
				     	server->rcv.creq = NULL;
					ncp_finish_request(req, result);
					__ncp_next_request(server);
					mutex_unlock(&server->rcv.creq_mutex);
					continue;
				}
			}
			mutex_unlock(&server->rcv.creq_mutex);
		}
drop:;		
		_recv(sock, &reply, sizeof(reply), MSG_DONTWAIT);
	}
}

static void __ncpdgram_timeout_proc(struct ncp_server *server)
{
	/* If timer is pending, we are processing another request... */
	if (!timer_pending(&server->timeout_tm)) {
		struct ncp_request_reply* req;
		
		req = server->rcv.creq;
		if (req) {
			int timeout;
			
			if (server->m.flags & NCP_MOUNT_SOFT) {
				if (server->timeout_retries-- == 0) {
					__ncp_abort_request(server, req, -ETIMEDOUT);
					return;
				}
			}
			/* Ignore errors */
			ncpdgram_send(server->ncp_sock, req);
			timeout = server->timeout_last << 1;
			if (timeout > NCP_MAX_RPC_TIMEOUT) {
				timeout = NCP_MAX_RPC_TIMEOUT;
			}
			server->timeout_last = timeout;
			mod_timer(&server->timeout_tm, jiffies + timeout);
		}
	}
}

void ncpdgram_timeout_proc(struct work_struct *work)
{
	struct ncp_server *server =
		container_of(work, struct ncp_server, timeout_tq);
	mutex_lock(&server->rcv.creq_mutex);
	__ncpdgram_timeout_proc(server);
	mutex_unlock(&server->rcv.creq_mutex);
}

static inline void ncp_init_req(struct ncp_request_reply* req)
{
	init_waitqueue_head(&req->wq);
	req->status = RQ_IDLE;
}

static int do_tcp_rcv(struct ncp_server *server, void *buffer, size_t len)
{
	int result;
	
	if (buffer) {
		result = _recv(server->ncp_sock, buffer, len, MSG_DONTWAIT);
	} else {
		static unsigned char dummy[1024];
			
		if (len > sizeof(dummy)) {
			len = sizeof(dummy);
		}
		result = _recv(server->ncp_sock, dummy, len, MSG_DONTWAIT);
	}
	if (result < 0) {
		return result;
	}
	if (result > len) {
		printk(KERN_ERR "ncpfs: tcp: bug in recvmsg (%u > %Zu)\n", result, len);
		return -EIO;			
	}
	return result;
}	

static int __ncptcp_rcv_proc(struct ncp_server *server)
{
	/* We have to check the result, so store the complete header */
	while (1) {
		int result;
		struct ncp_request_reply *req;
		int datalen;
		int type;

		while (server->rcv.len) {
			result = do_tcp_rcv(server, server->rcv.ptr, server->rcv.len);
			if (result == -EAGAIN) {
				return 0;
			}
			if (result <= 0) {
				req = server->rcv.creq;
				if (req) {
					__ncp_abort_request(server, req, -EIO);
				} else {
					__ncptcp_abort(server);
				}
				if (result < 0) {
					printk(KERN_ERR "ncpfs: tcp: error in recvmsg: %d\n", result);
				} else {
					DPRINTK(KERN_ERR "ncpfs: tcp: EOF\n");
				}
				return -EIO;
			}
			if (server->rcv.ptr) {
				server->rcv.ptr += result;
			}
			server->rcv.len -= result;
		}
		switch (server->rcv.state) {
			case 0:
				if (server->rcv.buf.magic != htonl(NCP_TCP_RCVD_MAGIC)) {
					printk(KERN_ERR "ncpfs: tcp: Unexpected reply type %08X\n", ntohl(server->rcv.buf.magic));
					__ncptcp_abort(server);
					return -EIO;
				}
				datalen = ntohl(server->rcv.buf.len) & 0x0FFFFFFF;
				if (datalen < 10) {
					printk(KERN_ERR "ncpfs: tcp: Unexpected reply len %d\n", datalen);
					__ncptcp_abort(server);
					return -EIO;
				}
#ifdef CONFIG_NCPFS_PACKET_SIGNING				
				if (server->sign_active) {
					if (datalen < 18) {
						printk(KERN_ERR "ncpfs: tcp: Unexpected reply len %d\n", datalen);
						__ncptcp_abort(server);
						return -EIO;
					}
					server->rcv.buf.len = datalen - 8;
					server->rcv.ptr = (unsigned char*)&server->rcv.buf.p1;
					server->rcv.len = 8;
					server->rcv.state = 4;
					break;
				}
#endif				
				type = ntohs(server->rcv.buf.type);
#ifdef CONFIG_NCPFS_PACKET_SIGNING				
cont:;				
#endif
				if (type != NCP_REPLY) {
					if (datalen - 8 <= sizeof(server->unexpected_packet.data)) {
						*(__u16*)(server->unexpected_packet.data) = htons(type);
						server->unexpected_packet.len = datalen - 8;

						server->rcv.state = 5;
						server->rcv.ptr = server->unexpected_packet.data + 2;
						server->rcv.len = datalen - 10;
						break;
					}					
					DPRINTK("ncpfs: tcp: Unexpected NCP type %02X\n", type);
skipdata2:;
					server->rcv.state = 2;
skipdata:;
					server->rcv.ptr = NULL;
					server->rcv.len = datalen - 10;
					break;
				}
				req = server->rcv.creq;
				if (!req) {
					DPRINTK(KERN_ERR "ncpfs: Reply without appropriate request\n");
					goto skipdata2;
				}
				if (datalen > req->datalen + 8) {
					printk(KERN_ERR "ncpfs: tcp: Unexpected reply len %d (expected at most %Zd)\n", datalen, req->datalen + 8);
					server->rcv.state = 3;
					goto skipdata;
				}
				req->datalen = datalen - 8;
				req->reply_buf->type = NCP_REPLY;
				server->rcv.ptr = (unsigned char*)(req->reply_buf) + 2;
				server->rcv.len = datalen - 10;
				server->rcv.state = 1;
				break;
#ifdef CONFIG_NCPFS_PACKET_SIGNING				
			case 4:
				datalen = server->rcv.buf.len;
				type = ntohs(server->rcv.buf.type2);
				goto cont;
#endif
			case 1:
				req = server->rcv.creq;
				if (req->tx_type != NCP_ALLOC_SLOT_REQUEST) {
					if (req->reply_buf->sequence != server->sequence) {
						printk(KERN_ERR "ncpfs: tcp: Bad sequence number\n");
						__ncp_abort_request(server, req, -EIO);
						return -EIO;
					}
					if ((req->reply_buf->conn_low | (req->reply_buf->conn_high << 8)) != server->connection) {
						printk(KERN_ERR "ncpfs: tcp: Connection number mismatch\n");
						__ncp_abort_request(server, req, -EIO);
						return -EIO;
					}
				}
#ifdef CONFIG_NCPFS_PACKET_SIGNING				
				if (server->sign_active && req->tx_type != NCP_DEALLOC_SLOT_REQUEST) {
					if (sign_verify_reply(server, (unsigned char*)(req->reply_buf) + 6, req->datalen - 6, cpu_to_be32(req->datalen + 16), &server->rcv.buf.type)) {
						printk(KERN_ERR "ncpfs: tcp: Signature violation\n");
						__ncp_abort_request(server, req, -EIO);
						return -EIO;
					}
				}
#endif				
				ncp_finish_request(req, req->datalen);
			nextreq:;
				__ncp_next_request(server);
			case 2:
			next:;
				server->rcv.ptr = (unsigned char*)&server->rcv.buf;
				server->rcv.len = 10;
				server->rcv.state = 0;
				break;
			case 3:
				ncp_finish_request(server->rcv.creq, -EIO);
				goto nextreq;
			case 5:
				info_server(server, 0, server->unexpected_packet.data, server->unexpected_packet.len);
				goto next;
		}
	}
}

void ncp_tcp_rcv_proc(struct work_struct *work)
{
	struct ncp_server *server =
		container_of(work, struct ncp_server, rcv.tq);

	mutex_lock(&server->rcv.creq_mutex);
	__ncptcp_rcv_proc(server);
	mutex_unlock(&server->rcv.creq_mutex);
}

void ncp_tcp_tx_proc(struct work_struct *work)
{
	struct ncp_server *server =
		container_of(work, struct ncp_server, tx.tq);
	
	mutex_lock(&server->rcv.creq_mutex);
	__ncptcp_try_send(server);
	mutex_unlock(&server->rcv.creq_mutex);
}

static int do_ncp_rpc_call(struct ncp_server *server, int size,
		struct ncp_reply_header* reply_buf, int max_reply_size)
{
	int result;
	struct ncp_request_reply req;

	ncp_init_req(&req);
	req.reply_buf = reply_buf;
	req.datalen = max_reply_size;
	req.tx_iov[1].iov_base = server->packet;
	req.tx_iov[1].iov_len = size;
	req.tx_iovlen = 1;
	req.tx_totallen = size;
	req.tx_type = *(u_int16_t*)server->packet;

	result = ncp_add_request(server, &req);
	if (result < 0) {
		return result;
	}
	if (wait_event_interruptible(req.wq, req.status == RQ_DONE)) {
		ncp_abort_request(server, &req, -EIO);
	}
	return req.result;
}

/*
 * We need the server to be locked here, so check!
 */

static int ncp_do_request(struct ncp_server *server, int size,
		void* reply, int max_reply_size)
{
	int result;

	if (server->lock == 0) {
		printk(KERN_ERR "ncpfs: Server not locked!\n");
		return -EIO;
	}
	if (!ncp_conn_valid(server)) {
		printk(KERN_ERR "ncpfs: Connection invalid!\n");
		return -EIO;
	}
	{
		sigset_t old_set;
		unsigned long mask, flags;

		spin_lock_irqsave(&current->sighand->siglock, flags);
		old_set = current->blocked;
		if (current->flags & PF_EXITING)
			mask = 0;
		else
			mask = sigmask(SIGKILL);
		if (server->m.flags & NCP_MOUNT_INTR) {
			/* FIXME: This doesn't seem right at all.  So, like,
			   we can't handle SIGINT and get whatever to stop?
			   What if we've blocked it ourselves?  What about
			   alarms?  Why, in fact, are we mucking with the
			   sigmask at all? -- r~ */
			if (current->sighand->action[SIGINT - 1].sa.sa_handler == SIG_DFL)
				mask |= sigmask(SIGINT);
			if (current->sighand->action[SIGQUIT - 1].sa.sa_handler == SIG_DFL)
				mask |= sigmask(SIGQUIT);
		}
		siginitsetinv(&current->blocked, mask);
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, flags);
		
		result = do_ncp_rpc_call(server, size, reply, max_reply_size);

		spin_lock_irqsave(&current->sighand->siglock, flags);
		current->blocked = old_set;
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, flags);
	}

	DDPRINTK("do_ncp_rpc_call returned %d\n", result);

	if (result < 0) {
		/* There was a problem with I/O, so the connections is
		 * no longer usable. */
		ncp_invalidate_conn(server);
	}
	return result;
}

/* ncp_do_request assures that at least a complete reply header is
 * received. It assumes that server->current_size contains the ncp
 * request size
 */
int ncp_request2(struct ncp_server *server, int function, 
		void* rpl, int size)
{
	struct ncp_request_header *h;
	struct ncp_reply_header* reply = rpl;
	int result;

	h = (struct ncp_request_header *) (server->packet);
	if (server->has_subfunction != 0) {
		*(__u16 *) & (h->data[0]) = htons(server->current_size - sizeof(*h) - 2);
	}
	h->type = NCP_REQUEST;
	/*
	 * The server shouldn't know or care what task is making a
	 * request, so we always use the same task number.
	 */
	h->task = 2; /* (current->pid) & 0xff; */
	h->function = function;

	result = ncp_do_request(server, server->current_size, reply, size);
	if (result < 0) {
		DPRINTK("ncp_request_error: %d\n", result);
		goto out;
	}
	server->completion = reply->completion_code;
	server->conn_status = reply->connection_state;
	server->reply_size = result;
	server->ncp_reply_size = result - sizeof(struct ncp_reply_header);

	result = reply->completion_code;

	if (result != 0)
		PPRINTK("ncp_request: completion code=%x\n", result);
out:
	return result;
}

int ncp_connect(struct ncp_server *server)
{
	struct ncp_request_header *h;
	int result;

	server->connection = 0xFFFF;
	server->sequence = 255;

	h = (struct ncp_request_header *) (server->packet);
	h->type = NCP_ALLOC_SLOT_REQUEST;
	h->task		= 2; /* see above */
	h->function	= 0;

	result = ncp_do_request(server, sizeof(*h), server->packet, server->packet_size);
	if (result < 0)
		goto out;
	server->connection = h->conn_low + (h->conn_high * 256);
	result = 0;
out:
	return result;
}

int ncp_disconnect(struct ncp_server *server)
{
	struct ncp_request_header *h;

	h = (struct ncp_request_header *) (server->packet);
	h->type = NCP_DEALLOC_SLOT_REQUEST;
	h->task		= 2; /* see above */
	h->function	= 0;

	return ncp_do_request(server, sizeof(*h), server->packet, server->packet_size);
}

void ncp_lock_server(struct ncp_server *server)
{
	mutex_lock(&server->mutex);
	if (server->lock)
		printk(KERN_WARNING "ncp_lock_server: was locked!\n");
	server->lock = 1;
}

void ncp_unlock_server(struct ncp_server *server)
{
	if (!server->lock) {
		printk(KERN_WARNING "ncp_unlock_server: was not locked!\n");
		return;
	}
	server->lock = 0;
	mutex_unlock(&server->mutex);
}
