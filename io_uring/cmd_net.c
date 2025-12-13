#include <asm/ioctls.h>
#include <linux/io_uring/net.h>
#include <linux/errqueue.h>
#include <net/sock.h>

#include "uring_cmd.h"
#include "io_uring.h"

static inline int io_uring_cmd_getsockopt(struct socket *sock,
					  struct io_uring_cmd *cmd,
					  unsigned int issue_flags)
{
	const struct io_uring_sqe *sqe = cmd->sqe;
	bool compat = !!(issue_flags & IO_URING_F_COMPAT);
	int optlen, optname, level, err;
	void __user *optval;

	level = READ_ONCE(sqe->level);
	if (level != SOL_SOCKET)
		return -EOPNOTSUPP;

	optval = u64_to_user_ptr(READ_ONCE(sqe->optval));
	optname = READ_ONCE(sqe->optname);
	optlen = READ_ONCE(sqe->optlen);

	err = do_sock_getsockopt(sock, compat, level, optname,
				 USER_SOCKPTR(optval),
				 KERNEL_SOCKPTR(&optlen));
	if (err)
		return err;

	/* On success, return optlen */
	return optlen;
}

static inline int io_uring_cmd_setsockopt(struct socket *sock,
					  struct io_uring_cmd *cmd,
					  unsigned int issue_flags)
{
	const struct io_uring_sqe *sqe = cmd->sqe;
	bool compat = !!(issue_flags & IO_URING_F_COMPAT);
	int optname, optlen, level;
	void __user *optval;
	sockptr_t optval_s;

	optval = u64_to_user_ptr(READ_ONCE(sqe->optval));
	optname = READ_ONCE(sqe->optname);
	optlen = READ_ONCE(sqe->optlen);
	level = READ_ONCE(sqe->level);
	optval_s = USER_SOCKPTR(optval);

	return do_sock_setsockopt(sock, compat, level, optname, optval_s,
				  optlen);
}

static bool io_process_timestamp_skb(struct io_uring_cmd *cmd, struct sock *sk,
				     struct sk_buff *skb, unsigned issue_flags)
{
	struct sock_exterr_skb *serr = SKB_EXT_ERR(skb);
	struct io_uring_cqe cqe[2];
	struct io_timespec *iots;
	struct timespec64 ts;
	u32 tstype, tskey;
	int ret;

	BUILD_BUG_ON(sizeof(struct io_uring_cqe) != sizeof(struct io_timespec));

	ret = skb_get_tx_timestamp(skb, sk, &ts);
	if (ret < 0)
		return false;

	tskey = serr->ee.ee_data;
	tstype = serr->ee.ee_info;

	cqe->user_data = 0;
	cqe->res = tskey;
	cqe->flags = IORING_CQE_F_MORE | ctx_cqe32_flags(cmd_to_io_kiocb(cmd)->ctx);
	cqe->flags |= tstype << IORING_TIMESTAMP_TYPE_SHIFT;
	if (ret == SOF_TIMESTAMPING_TX_HARDWARE)
		cqe->flags |= IORING_CQE_F_TSTAMP_HW;

	iots = (struct io_timespec *)&cqe[1];
	iots->tv_sec = ts.tv_sec;
	iots->tv_nsec = ts.tv_nsec;
	return io_uring_cmd_post_mshot_cqe32(cmd, issue_flags, cqe);
}

static int io_uring_cmd_timestamp(struct socket *sock,
				  struct io_uring_cmd *cmd,
				  unsigned int issue_flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff_head *q = &sk->sk_error_queue;
	struct sk_buff *skb, *tmp;
	struct sk_buff_head list;
	int ret;

	if (!(issue_flags & IO_URING_F_CQE32))
		return -EINVAL;
	ret = io_cmd_poll_multishot(cmd, issue_flags, EPOLLERR);
	if (unlikely(ret))
		return ret;

	if (skb_queue_empty_lockless(q))
		return -EAGAIN;
	__skb_queue_head_init(&list);

	scoped_guard(spinlock_irq, &q->lock) {
		skb_queue_walk_safe(q, skb, tmp) {
			/* don't support skbs with payload */
			if (!skb_has_tx_timestamp(skb, sk) || skb->len)
				continue;
			__skb_unlink(skb, q);
			__skb_queue_tail(&list, skb);
		}
	}

	while (1) {
		skb = skb_peek(&list);
		if (!skb)
			break;
		if (!io_process_timestamp_skb(cmd, sk, skb, issue_flags))
			break;
		__skb_dequeue(&list);
		consume_skb(skb);
	}

	if (!unlikely(skb_queue_empty(&list))) {
		scoped_guard(spinlock_irqsave, &q->lock)
			skb_queue_splice(&list, q);
	}
	return -EAGAIN;
}

int io_uring_cmd_sock(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	struct socket *sock = cmd->file->private_data;
	struct sock *sk = sock->sk;
	struct proto *prot = READ_ONCE(sk->sk_prot);
	int ret, arg = 0;

	if (!prot || !prot->ioctl)
		return -EOPNOTSUPP;

	switch (cmd->cmd_op) {
	case SOCKET_URING_OP_SIOCINQ:
		ret = prot->ioctl(sk, SIOCINQ, &arg);
		if (ret)
			return ret;
		return arg;
	case SOCKET_URING_OP_SIOCOUTQ:
		ret = prot->ioctl(sk, SIOCOUTQ, &arg);
		if (ret)
			return ret;
		return arg;
	case SOCKET_URING_OP_GETSOCKOPT:
		return io_uring_cmd_getsockopt(sock, cmd, issue_flags);
	case SOCKET_URING_OP_SETSOCKOPT:
		return io_uring_cmd_setsockopt(sock, cmd, issue_flags);
	case SOCKET_URING_OP_TX_TIMESTAMP:
		return io_uring_cmd_timestamp(sock, cmd, issue_flags);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(io_uring_cmd_sock);
