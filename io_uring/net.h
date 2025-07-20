// SPDX-License-Identifier: GPL-2.0

#include <linux/net.h>
#include <linux/uio.h>
#include <linux/io_uring_types.h>

struct io_async_msghdr {
#if defined(CONFIG_NET)
	struct iou_vec				vec;

	struct_group(clear,
		int				namelen;
		struct iovec			fast_iov;
		__kernel_size_t			controllen;
		__kernel_size_t			payloadlen;
		struct sockaddr __user		*uaddr;
		struct msghdr			msg;
		struct sockaddr_storage		addr;
	);
#else
	struct_group(clear);
#endif
};

#if defined(CONFIG_NET)

int io_shutdown_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_shutdown(struct io_kiocb *req, unsigned int issue_flags);

void io_sendmsg_recvmsg_cleanup(struct io_kiocb *req);
int io_sendmsg_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_sendmsg(struct io_kiocb *req, unsigned int issue_flags);

int io_send(struct io_kiocb *req, unsigned int issue_flags);

int io_recvmsg_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_recvmsg(struct io_kiocb *req, unsigned int issue_flags);
int io_recv(struct io_kiocb *req, unsigned int issue_flags);

void io_sendrecv_fail(struct io_kiocb *req);

int io_accept_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_accept(struct io_kiocb *req, unsigned int issue_flags);

int io_socket_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_socket(struct io_kiocb *req, unsigned int issue_flags);

int io_connect_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_connect(struct io_kiocb *req, unsigned int issue_flags);

int io_send_zc(struct io_kiocb *req, unsigned int issue_flags);
int io_sendmsg_zc(struct io_kiocb *req, unsigned int issue_flags);
int io_send_zc_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
void io_send_zc_cleanup(struct io_kiocb *req);

int io_bind_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_bind(struct io_kiocb *req, unsigned int issue_flags);

int io_listen_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_listen(struct io_kiocb *req, unsigned int issue_flags);

void io_netmsg_cache_free(const void *entry);
#else
static inline void io_netmsg_cache_free(const void *entry)
{
}
#endif
