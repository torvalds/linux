/* SPDX-License-Identifier: MIT */

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct io_sq_ring {
	unsigned int *head;
	unsigned int *tail;
	unsigned int *ring_mask;
	unsigned int *ring_entries;
	unsigned int *flags;
	unsigned int *array;
};

struct io_cq_ring {
	unsigned int *head;
	unsigned int *tail;
	unsigned int *ring_mask;
	unsigned int *ring_entries;
	struct io_uring_cqe *cqes;
};

struct io_uring_sq {
	unsigned int *khead;
	unsigned int *ktail;
	unsigned int *kring_mask;
	unsigned int *kring_entries;
	unsigned int *kflags;
	unsigned int *kdropped;
	unsigned int *array;
	struct io_uring_sqe *sqes;

	unsigned int sqe_head;
	unsigned int sqe_tail;

	size_t ring_sz;
};

struct io_uring_cq {
	unsigned int *khead;
	unsigned int *ktail;
	unsigned int *kring_mask;
	unsigned int *kring_entries;
	unsigned int *koverflow;
	struct io_uring_cqe *cqes;

	size_t ring_sz;
};

struct io_uring {
	struct io_uring_sq sq;
	struct io_uring_cq cq;
	int ring_fd;
};

#if defined(__x86_64) || defined(__i386__)
#define read_barrier()	__asm__ __volatile__("":::"memory")
#define write_barrier()	__asm__ __volatile__("":::"memory")
#else
#define read_barrier()	__sync_synchronize()
#define write_barrier()	__sync_synchronize()
#endif

static inline int io_uring_mmap(int fd, struct io_uring_params *p,
				struct io_uring_sq *sq, struct io_uring_cq *cq)
{
	size_t size;
	void *ptr;
	int ret;

	sq->ring_sz = p->sq_off.array + p->sq_entries * sizeof(unsigned int);
	ptr = mmap(0, sq->ring_sz, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
	if (ptr == MAP_FAILED)
		return -errno;
	sq->khead = ptr + p->sq_off.head;
	sq->ktail = ptr + p->sq_off.tail;
	sq->kring_mask = ptr + p->sq_off.ring_mask;
	sq->kring_entries = ptr + p->sq_off.ring_entries;
	sq->kflags = ptr + p->sq_off.flags;
	sq->kdropped = ptr + p->sq_off.dropped;
	sq->array = ptr + p->sq_off.array;

	size = p->sq_entries * sizeof(struct io_uring_sqe);
	sq->sqes = mmap(0, size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
	if (sq->sqes == MAP_FAILED) {
		ret = -errno;
err:
		munmap(sq->khead, sq->ring_sz);
		return ret;
	}

	cq->ring_sz = p->cq_off.cqes + p->cq_entries * sizeof(struct io_uring_cqe);
	ptr = mmap(0, cq->ring_sz, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
	if (ptr == MAP_FAILED) {
		ret = -errno;
		munmap(sq->sqes, p->sq_entries * sizeof(struct io_uring_sqe));
		goto err;
	}
	cq->khead = ptr + p->cq_off.head;
	cq->ktail = ptr + p->cq_off.tail;
	cq->kring_mask = ptr + p->cq_off.ring_mask;
	cq->kring_entries = ptr + p->cq_off.ring_entries;
	cq->koverflow = ptr + p->cq_off.overflow;
	cq->cqes = ptr + p->cq_off.cqes;
	return 0;
}

static inline int io_uring_setup(unsigned int entries,
				 struct io_uring_params *p)
{
	return syscall(__NR_io_uring_setup, entries, p);
}

static inline int io_uring_enter(int fd, unsigned int to_submit,
				 unsigned int min_complete,
				 unsigned int flags, sigset_t *sig)
{
	return syscall(__NR_io_uring_enter, fd, to_submit, min_complete,
		       flags, sig, _NSIG / 8);
}

static inline int io_uring_queue_init(unsigned int entries,
				      struct io_uring *ring,
				      unsigned int flags)
{
	struct io_uring_params p;
	int fd, ret;

	memset(ring, 0, sizeof(*ring));
	memset(&p, 0, sizeof(p));
	p.flags = flags;

	fd = io_uring_setup(entries, &p);
	if (fd < 0)
		return fd;
	ret = io_uring_mmap(fd, &p, &ring->sq, &ring->cq);
	if (!ret)
		ring->ring_fd = fd;
	else
		close(fd);
	return ret;
}

/* Get a sqe */
static inline struct io_uring_sqe *io_uring_get_sqe(struct io_uring *ring)
{
	struct io_uring_sq *sq = &ring->sq;

	if (sq->sqe_tail + 1 - sq->sqe_head > *sq->kring_entries)
		return NULL;
	return &sq->sqes[sq->sqe_tail++ & *sq->kring_mask];
}

static inline int io_uring_wait_cqe(struct io_uring *ring,
				    struct io_uring_cqe **cqe_ptr)
{
	struct io_uring_cq *cq = &ring->cq;
	const unsigned int mask = *cq->kring_mask;
	unsigned int head = *cq->khead;
	int ret;

	*cqe_ptr = NULL;
	do {
		read_barrier();
		if (head != *cq->ktail) {
			*cqe_ptr = &cq->cqes[head & mask];
			break;
		}
		ret = io_uring_enter(ring->ring_fd, 0, 1,
				     IORING_ENTER_GETEVENTS, NULL);
		if (ret < 0)
			return -errno;
	} while (1);

	return 0;
}

static inline int io_uring_submit(struct io_uring *ring)
{
	struct io_uring_sq *sq = &ring->sq;
	const unsigned int mask = *sq->kring_mask;
	unsigned int ktail, submitted, to_submit;
	int ret;

	read_barrier();
	if (*sq->khead != *sq->ktail) {
		submitted = *sq->kring_entries;
		goto submit;
	}
	if (sq->sqe_head == sq->sqe_tail)
		return 0;

	ktail = *sq->ktail;
	to_submit = sq->sqe_tail - sq->sqe_head;
	for (submitted = 0; submitted < to_submit; submitted++) {
		read_barrier();
		sq->array[ktail++ & mask] = sq->sqe_head++ & mask;
	}
	if (!submitted)
		return 0;

	if (*sq->ktail != ktail) {
		write_barrier();
		*sq->ktail = ktail;
		write_barrier();
	}
submit:
	ret = io_uring_enter(ring->ring_fd, submitted, 0,
			     IORING_ENTER_GETEVENTS, NULL);
	return ret < 0 ? -errno : ret;
}

static inline void io_uring_queue_exit(struct io_uring *ring)
{
	struct io_uring_sq *sq = &ring->sq;

	munmap(sq->sqes, *sq->kring_entries * sizeof(struct io_uring_sqe));
	munmap(sq->khead, sq->ring_sz);
	close(ring->ring_fd);
}

/* Prepare and send the SQE */
static inline void io_uring_prep_cmd(struct io_uring_sqe *sqe, int op,
				     int sockfd,
				     int level, int optname,
				     const void *optval,
				     int optlen)
{
	memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = (__u8)IORING_OP_URING_CMD;
	sqe->fd = sockfd;
	sqe->cmd_op = op;

	sqe->level = level;
	sqe->optname = optname;
	sqe->optval = (unsigned long long)optval;
	sqe->optlen = optlen;
}

static inline int io_uring_register_buffers(struct io_uring *ring,
					    const struct iovec *iovecs,
					    unsigned int nr_iovecs)
{
	int ret;

	ret = syscall(__NR_io_uring_register, ring->ring_fd,
		      IORING_REGISTER_BUFFERS, iovecs, nr_iovecs);
	return (ret < 0) ? -errno : ret;
}

static inline void io_uring_prep_send(struct io_uring_sqe *sqe, int sockfd,
				      const void *buf, size_t len, int flags)
{
	memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = (__u8)IORING_OP_SEND;
	sqe->fd = sockfd;
	sqe->addr = (unsigned long)buf;
	sqe->len = len;
	sqe->msg_flags = (__u32)flags;
}

static inline void io_uring_prep_sendzc(struct io_uring_sqe *sqe, int sockfd,
					const void *buf, size_t len, int flags,
					unsigned int zc_flags)
{
	io_uring_prep_send(sqe, sockfd, buf, len, flags);
	sqe->opcode = (__u8)IORING_OP_SEND_ZC;
	sqe->ioprio = zc_flags;
}

static inline void io_uring_cqe_seen(struct io_uring *ring)
{
	*(&ring->cq)->khead += 1;
	write_barrier();
}
