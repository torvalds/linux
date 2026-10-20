// SPDX-License-Identifier: GPL-2.0

#include <assert.h>
#include <elf.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <linux/stddef.h>
#include <linux/io_uring.h>
#include <linux/swab.h>
#include <linux/coredump.h>
#include <linux/fs.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../filesystems/wrappers.h"
#include "coredump_notify_signal.h"

#include "coredump_test_helpers.h"

#if __ELF_NATIVE_CLASS == 64
#define COREDUMP_ELFCLASS ELFCLASS64
#else
#define COREDUMP_ELFCLASS ELFCLASS32
#endif

void *do_nothing(void *arg)
{
	(void)arg;
	while (1)
		pause();

	return NULL;
}

void crashing_child(void)
{
	pthread_t thread;
	int i;

	for (i = 0; i < NUM_THREAD_SPAWN; ++i)
		pthread_create(&thread, NULL, do_nothing, NULL);

	/* crash on purpose */
	i = *(volatile int *)NULL;
}

void crashing_child_sparse(size_t size)
{
	char *p;

	/*
	 * Touch the first and the last page. This will cause the whole mapping
	 * to be dumped because it has been written to. Everything between
	 * those two pages is a hole though.
	 */
	p = mmap(NULL, size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (p != MAP_FAILED) {
		p[0] = 'x';
		p[size - 1] = 'x';
	}

	/* crash on purpose */
	*(volatile int *)NULL = 0;
}

/* Select @types through the caller's own /proc/self/coredump_filter. */
static bool set_coredump_filter(__u64 types)
{
	char buf[32];
	int fd, len;
	bool ok;

	fd = open("/proc/self/coredump_filter", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	len = snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)types);
	ok = write_nointr(fd, buf, len) == len;
	close(fd);
	return ok;
}

/*
 * Map shared anonymous memory, touch it, tell the server where it is and
 * crash. A @task_filter other than FILTER_TASK_INHERIT is selected first.
 */
void crashing_child_memory(__u64 task_filter, int fd_addr)
{
	char *p;

	if (task_filter != FILTER_TASK_INHERIT && !set_coredump_filter(task_filter))
		_exit(EXIT_FAILURE);

	p = mmap(NULL, MEMORY_MAPPING_SIZE, PROT_READ | PROT_WRITE,
		 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		_exit(EXIT_FAILURE);
	p[0] = 'x';

	if (write_nointr(fd_addr, &p, sizeof(p)) != sizeof(p))
		_exit(EXIT_FAILURE);
	close(fd_addr);

	/* crash on purpose */
	*(volatile int *)NULL = 0;
}

/* Sink a reassembled record stream is handed to, record by record. */
struct coredump_record_sink {
	/* @len bytes of coredump data that belong at @offset. */
	int (*data)(void *ctx, const void *buf, size_t len, __u64 offset);
	/* @len zero bytes that belong at @offset. */
	int (*zero)(void *ctx, __u64 offset, __u64 len);
	void *ctx;
};

/* Read @len bytes off the socket and hand them to @sink, if there is one. */
static ssize_t recv_record_bytes(int fd_coredump, __u64 len,
				 const struct coredump_record_sink *sink,
				 __u64 offset)
{
	ssize_t received = 0;

	while (len) {
		char buffer[PAGE_SIZE];
		size_t chunk = len < sizeof(buffer) ? len : sizeof(buffer);
		ssize_t ret;

		ret = recv(fd_coredump, buffer, chunk, MSG_WAITALL);
		if (ret <= 0) {
			fprintf(stderr, "%s: short read %zd: %m\n",
				__func__, ret);
			return -1;
		}

		if (sink && sink->data(sink->ctx, buffer, ret, offset + received))
			return -1;

		received += ret;
		len -= ret;
	}

	return received;
}

/* Put the data where the records say it goes and leave the holes alone. */
static int file_sink_data(void *ctx, const void *buf, size_t len, __u64 offset)
{
	int fd = *(int *)ctx;

	if (pwrite(fd, buf, len, offset) != (ssize_t)len) {
		fprintf(stderr, "%s: pwrite failed: %m\n", __func__);
		return -1;
	}

	return 0;
}

static int file_sink_zero(void *ctx, __u64 offset, __u64 len)
{
	/* Nothing has to be written for a hole. */
	return 0;
}

/*
 * Read a coredump strea and funnel it into @sink. Allow to pass in a
 * @fd_peer_pidfd to simulate coredump truncation by killing it after having
 * received a coredump record.
 */
static ssize_t __recv_coredump_records(int fd_coredump,
				       const struct coredump_record_sink *sink,
				       off_t *coredump_size, bool *truncated,
				       int fd_peer_pidfd)
{
	ssize_t received = 0;
	off_t size = 0;
	bool is_truncated = false;
	bool ended = false;
	char trailing;

	while (!ended) {
		struct coredump_record_header record = {};
		size_t known_size;
		ssize_t ret;

		/* Peek the header size the way read_coredump_req() does. */
		ret = recv(fd_coredump, &record, sizeof(record.size),
			   MSG_PEEK | MSG_WAITALL);
		if (ret == 0) {
			/* Nothing closed the stream, so the coredump was cut short. */
			if (truncated) {
				is_truncated = true;
				break;
			}
			fprintf(stderr, "%s: stream ended without an end record\n",
				__func__);
			return -1;
		}
		if (ret != sizeof(record.size)) {
			fprintf(stderr, "%s: short record peek %zd: %m\n",
				__func__, ret);
			return -1;
		}

		if (record.size < COREDUMP_RECORD_HEADER_SIZE_VER0) {
			fprintf(stderr, "%s: header size %u below minimum %u\n",
				__func__, record.size,
				COREDUMP_RECORD_HEADER_SIZE_VER0);
			return -1;
		}

		/* Consume as much of the header as we know about. */
		known_size = record.size < sizeof(record) ? record.size : sizeof(record);
		ret = recv(fd_coredump, &record, known_size, MSG_WAITALL);
		if (ret != (ssize_t)known_size) {
			fprintf(stderr, "%s: short record read %zd: %m\n",
				__func__, ret);
			return -1;
		}
		received += ret;

		/*
		 * A flag changes what the record means, so refuse one we
		 * don't know rather than guess.
		 */
		if (record.flags) {
			fprintf(stderr, "%s: unknown header flags 0x%llx\n",
				__func__, (unsigned long long)record.flags);
			return -1;
		}

		/* Discard any part of the header we have no use for. */
		ret = recv_record_bytes(fd_coredump, record.size - known_size,
					NULL, 0);
		if (ret < 0)
			return -1;
		received += ret;

		/* Records are sent in order and they don't leave gaps. */
		if (record.offset != (__u64)size) {
			fprintf(stderr, "%s: record at %llu, expected %llu\n",
				__func__, (unsigned long long)record.offset,
				(unsigned long long)size);
			return -1;
		}

		switch (record.type) {
		case COREDUMP_RECORD_ZERO:
			/* A hole. It comes with no data and needs none. */
			if (sink->zero(sink->ctx, record.offset, record.len))
				return -1;
			break;
		case COREDUMP_RECORD_DATA:
			ret = recv_record_bytes(fd_coredump, record.len, sink,
						record.offset);
			if (ret < 0)
				return -1;
			received += ret;
			if (fd_peer_pidfd >= 0) {
				if (sys_pidfd_send_signal(fd_peer_pidfd, SIGKILL,
							  NULL, 0)) {
					fprintf(stderr, "%s: kill failed: %m\n",
						__func__);
					return -1;
				}
				fd_peer_pidfd = -1;
			}
			break;
		case COREDUMP_RECORD_END:
			/* The coredump ends here and nothing follows it. */
			if (record.len) {
				fprintf(stderr, "%s: end record covers %llu bytes\n",
					__func__,
					(unsigned long long)record.len);
				return -1;
			}
			ended = true;
			break;
		default:
			fprintf(stderr, "%s: unknown record type %u\n",
				__func__, record.type);
			return -1;
		}

		size += record.len;
	}

	/* The end record is the last thing on the wire. */
	if (recv(fd_coredump, &trailing, sizeof(trailing), MSG_DONTWAIT) > 0) {
		fprintf(stderr, "%s: data after the end record\n", __func__);
		return -1;
	}

	if (truncated)
		*truncated = is_truncated;

	*coredump_size = size;

	fprintf(stderr, "Received %zd bytes for a %s coredump of %llu bytes\n",
		received, is_truncated ? "truncated" : "complete",
		(unsigned long long)size);
	return received;
}

/* Reassemble a record stream into the coredump it describes. */
ssize_t recv_coredump_records(int fd_coredump, int fd_core_file,
			      off_t *coredump_size, bool *truncated,
			      int fd_peer_pidfd)
{
	struct coredump_record_sink sink = {
		.data	= file_sink_data,
		.zero	= file_sink_zero,
		.ctx	= &fd_core_file,
	};
	ssize_t received;
	off_t size = 0;

	received = __recv_coredump_records(fd_coredump, &sink, &size, truncated,
					   fd_peer_pidfd);
	if (received < 0)
		return -1;

	/*
	 * Nothing is written for a hole, so grow the file to the size the
	 * records describe in case the coredump ended in one.
	 */
	if (ftruncate(fd_core_file, size) < 0) {
		fprintf(stderr, "%s: ftruncate to %llu failed: %m\n",
			__func__, (unsigned long long)size);
		return -1;
	}

	if (coredump_size)
		*coredump_size = size;

	return received;
}

/* The ELF header of a native core file. */
static bool is_core_ehdr(const ElfW(Ehdr) *ehdr)
{
	return !memcmp(ehdr->e_ident, ELFMAG, SELFMAG) &&
	       ehdr->e_ident[EI_CLASS] == COREDUMP_ELFCLASS &&
	       ehdr->e_type == ET_CORE;
}

/* Whatever the server ends up with has to be an ELF core file. */
bool is_elf_core(int fd)
{
	ElfW(Ehdr) ehdr;

	if (pread(fd, &ehdr, sizeof(ehdr), 0) != sizeof(ehdr)) {
		fprintf(stderr, "%s: short read: %m\n", __func__);
		return false;
	}

	if (!is_core_ehdr(&ehdr)) {
		fprintf(stderr, "%s: not an ELF core file\n", __func__);
		return false;
	}

	return true;
}

/*
 * A coredump server that uploads to a blob store can't upload a sparse
 * file and can't seek in the object it is uploading. It streams the data
 * records into the object as they arrive, remembers the holes it left
 * out, and uploads the program header table that describes the result
 * last. What comes out is an ordinary ELF core file without the holes.
 */

/* A run of the coredump the object doesn't carry. */
struct compact_hole {
	__u64 offset;
	__u64 len;
};

/* A program header of the object and where its bytes sat in the coredump. */
struct compact_piece {
	ElfW(Phdr) phdr;
	__u64 src;
};

struct compact_ctx {
	int fd_body;			/* the object's payload, append only */
	int fd_reference;		/* the coredump with its holes, for the test */
	unsigned char *head;		/* everything ahead of the segment data */
	size_t head_len;
	size_t head_cap;
	__u64 data_offset;		/* where the segment data starts, 0 while unknown */
	struct compact_hole *holes;
	size_t nr_holes;
	size_t holes_cap;
	__u64 body_len;
};

/* Write @len bytes out, short writes and all. */
static int compact_write(int fd, const void *buf, size_t len)
{
	const unsigned char *pos = buf;

	while (len) {
		ssize_t ret = write(fd, pos, len);

		if (ret <= 0) {
			fprintf(stderr, "%s: write failed: %m\n", __func__);
			return -1;
		}

		pos += ret;
		len -= ret;
	}

	return 0;
}

/* Keep @len bytes of the head, or @len zeroes if @buf is NULL. */
static int compact_head_append(struct compact_ctx *ctx, const void *buf,
			       size_t len)
{
	if (ctx->head_len + len > ctx->head_cap) {
		size_t cap = ctx->head_cap ? ctx->head_cap : PAGE_SIZE;
		unsigned char *head;

		while (cap < ctx->head_len + len)
			cap *= 2;

		head = realloc(ctx->head, cap);
		if (!head) {
			fprintf(stderr, "%s: out of memory\n", __func__);
			return -1;
		}
		ctx->head = head;
		ctx->head_cap = cap;
	}

	if (buf)
		memcpy(ctx->head + ctx->head_len, buf, len);
	else
		memset(ctx->head + ctx->head_len, 0, len);
	ctx->head_len += len;

	return 0;
}

/* Remember a hole so the program header table can account for it later. */
static int compact_keep_hole(struct compact_ctx *ctx, __u64 offset, __u64 len)
{
	if (ctx->nr_holes == ctx->holes_cap) {
		size_t cap = ctx->holes_cap ? ctx->holes_cap * 2 : 64;
		struct compact_hole *holes;

		holes = realloc(ctx->holes, cap * sizeof(*holes));
		if (!holes) {
			fprintf(stderr, "%s: out of memory\n", __func__);
			return -1;
		}
		ctx->holes = holes;
		ctx->holes_cap = cap;
	}

	ctx->holes[ctx->nr_holes].offset = offset;
	ctx->holes[ctx->nr_holes].len = len;
	ctx->nr_holes++;

	return 0;
}

/* The segment data starts where the first PT_LOAD points. */
static int compact_probe(struct compact_ctx *ctx)
{
	const ElfW(Ehdr) *ehdr = (const ElfW(Ehdr) *)ctx->head;
	const ElfW(Phdr) *phdr;
	size_t i;

	if (ctx->data_offset || ctx->head_len < sizeof(*ehdr))
		return 0;

	if (!is_core_ehdr(ehdr)) {
		fprintf(stderr, "%s: not an ELF core file\n", __func__);
		return -1;
	}

	if (ehdr->e_phoff != sizeof(*ehdr) ||
	    ehdr->e_phentsize != sizeof(ElfW(Phdr)) ||
	    ehdr->e_phnum == 0 || ehdr->e_phnum == PN_XNUM) {
		fprintf(stderr, "%s: unhandled program header table\n", __func__);
		return -1;
	}

	if (ctx->head_len < ehdr->e_phoff +
			    (size_t)ehdr->e_phnum * ehdr->e_phentsize)
		return 0;

	phdr = (const ElfW(Phdr) *)(ctx->head + ehdr->e_phoff);
	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type != PT_LOAD)
			continue;
		if (!ctx->data_offset || phdr[i].p_offset < ctx->data_offset)
			ctx->data_offset = phdr[i].p_offset;
	}

	if (!ctx->data_offset) {
		fprintf(stderr, "%s: coredump without a single segment\n",
			__func__);
		return -1;
	}

	return 0;
}

/*
 * Take whatever of [@offset, @offset + @len) still belongs to the head.
 * @buf is NULL for a hole. Returns how much was taken.
 */
static ssize_t compact_head_take(struct compact_ctx *ctx, const void *buf,
				 __u64 offset, __u64 len)
{
	__u64 chunk;

	if (!len || (ctx->data_offset && offset >= ctx->data_offset))
		return 0;

	chunk = len;
	if (ctx->data_offset && offset + chunk > ctx->data_offset)
		chunk = ctx->data_offset - offset;

	if (offset != ctx->head_len) {
		fprintf(stderr, "%s: head has a gap at %llu\n", __func__,
			(unsigned long long)offset);
		return -1;
	}

	if (compact_head_append(ctx, buf, chunk))
		return -1;

	return chunk;
}

static int compact_data(void *arg, const void *buf, size_t len, __u64 offset)
{
	struct compact_ctx *ctx = arg;
	const unsigned char *pos = buf;
	ssize_t head;

	/* Only the test needs a coredump with the holes still in it. */
	if (pwrite(ctx->fd_reference, pos, len, offset) != (ssize_t)len) {
		fprintf(stderr, "%s: pwrite failed: %m\n", __func__);
		return -1;
	}

	/* The head has to be rewritten at the end, so hold on to it. */
	head = compact_head_take(ctx, pos, offset, len);
	if (head < 0)
		return -1;
	if (head && compact_probe(ctx))
		return -1;

	pos += head;
	len -= head;
	if (!len)
		return 0;

	/* Everything else goes into the object as it arrives. */
	if (compact_write(ctx->fd_body, pos, len))
		return -1;
	ctx->body_len += len;

	return 0;
}

static int compact_zero(void *arg, __u64 offset, __u64 len)
{
	struct compact_ctx *ctx = arg;
	ssize_t head;

	/* A hole in the head is alignment padding. Write it out. */
	head = compact_head_take(ctx, NULL, offset, len);
	if (head < 0)
		return -1;

	offset += head;
	len -= head;
	if (!len)
		return 0;

	/* This is what the object doesn't have to carry. */
	return compact_keep_hole(ctx, offset, len);
}

/* Where @offset ends up in the object once the holes ahead of it are gone. */
static __u64 compact_offset(const struct compact_ctx *ctx, __u64 body_start,
			    __u64 offset)
{
	__u64 elided = 0;
	size_t i;

	for (i = 0; i < ctx->nr_holes; i++) {
		__u64 len = ctx->holes[i].len;

		if (ctx->holes[i].offset >= offset)
			break;
		if (ctx->holes[i].offset + len > offset)
			len = offset - ctx->holes[i].offset;
		elided += len;
	}

	return body_start + (offset - ctx->data_offset) - elided;
}

/* A run of segment data that made it into the object. */
static void compact_add_data(struct compact_piece *pieces, size_t *nr,
			     const ElfW(Phdr) *phdr, __u64 start, __u64 end)
{
	struct compact_piece *piece = &pieces[(*nr)++];

	piece->phdr = *phdr;
	piece->phdr.p_vaddr = phdr->p_vaddr + (start - phdr->p_offset);
	piece->phdr.p_paddr = 0;
	piece->phdr.p_filesz = end - start;
	piece->phdr.p_memsz = end - start;
	piece->src = start;
}

/*
 * A run of @len bytes the object doesn't carry. It grows the piece in
 * front of it if this segment already has one, because everything a
 * segment covers past p_filesz is zeroes anyway.
 */
static void compact_add_zero(struct compact_piece *pieces, size_t *nr,
			     size_t first, const ElfW(Phdr) *phdr, __u64 vaddr,
			     __u64 len)
{
	struct compact_piece *piece;

	if (*nr > first) {
		pieces[*nr - 1].phdr.p_memsz += len;
		return;
	}

	piece = &pieces[(*nr)++];
	piece->phdr = *phdr;
	piece->phdr.p_vaddr = vaddr;
	piece->phdr.p_paddr = 0;
	piece->phdr.p_filesz = 0;
	piece->phdr.p_memsz = len;
	piece->src = 0;
}

/* Split the segments at the holes and write out what the object became. */
static int compact_build(struct compact_ctx *ctx, int fd_object)
{
	__u64 note_offset = 0, note_len = 0, note_new;
	__u64 align = 0, head_len, body_start, pos;
	size_t nr_old, nr_new = 0, note_piece = 0, i;
	struct compact_piece *pieces;
	char buffer[PAGE_SIZE];
	const ElfW(Phdr) *old;
	ElfW(Ehdr) ehdr;
	int ret = -1;

	if (!ctx->data_offset) {
		fprintf(stderr, "%s: coredump without segment data\n", __func__);
		return -1;
	}

	memcpy(&ehdr, ctx->head, sizeof(ehdr));
	if (ehdr.e_shoff) {
		fprintf(stderr, "%s: section headers are not handled\n",
			__func__);
		return -1;
	}

	old = (const ElfW(Phdr) *)(ctx->head + ehdr.e_phoff);
	nr_old = ehdr.e_phnum;

	pieces = calloc(nr_old + 2 * ctx->nr_holes + 1, sizeof(*pieces));
	if (!pieces) {
		fprintf(stderr, "%s: out of memory\n", __func__);
		return -1;
	}

	for (i = 0; i < nr_old; i++) {
		ElfW(Phdr) phdr = old[i];
		__u64 end = phdr.p_offset + phdr.p_filesz;
		__u64 cur = phdr.p_offset;
		size_t first = nr_new, h;

		/* The notes move because the table in front of them grows. */
		if (phdr.p_type == PT_NOTE) {
			if (note_len) {
				fprintf(stderr, "%s: more than one note segment\n",
					__func__);
				goto out;
			}
			note_offset = phdr.p_offset;
			note_len = phdr.p_filesz;
			note_piece = nr_new;
			pieces[nr_new].phdr = phdr;
			pieces[nr_new++].src = 0;
			continue;
		}

		if (phdr.p_type != PT_LOAD) {
			if (phdr.p_filesz && phdr.p_offset < ctx->data_offset) {
				fprintf(stderr, "%s: segment %zu is in the head\n",
					__func__, i);
				goto out;
			}
			pieces[nr_new].phdr = phdr;
			pieces[nr_new++].src = phdr.p_offset;
			continue;
		}

		if (!align)
			align = phdr.p_align;

		for (h = 0; h < ctx->nr_holes && cur < end; h++) {
			__u64 start = ctx->holes[h].offset;
			__u64 stop = start + ctx->holes[h].len;

			if (stop <= cur)
				continue;
			if (start >= end)
				break;

			/* A hole can span more than this one segment. */
			if (start < cur)
				start = cur;
			if (stop > end)
				stop = end;

			if (start > cur) {
				compact_add_data(pieces, &nr_new, &phdr, cur,
						 start);
				cur = start;
			}
			compact_add_zero(pieces, &nr_new, first, &phdr,
					 phdr.p_vaddr + (cur - phdr.p_offset),
					 stop - cur);
			cur = stop;
		}

		if (cur < end)
			compact_add_data(pieces, &nr_new, &phdr, cur, end);

		/* Whatever the kernel didn't dump of this mapping. */
		if (phdr.p_memsz > phdr.p_filesz)
			compact_add_zero(pieces, &nr_new, first, &phdr,
					 phdr.p_vaddr + phdr.p_filesz,
					 phdr.p_memsz - phdr.p_filesz);
	}

	if (!note_len || note_offset + note_len > ctx->head_len) {
		fprintf(stderr, "%s: notes aren't where they should be\n",
			__func__);
		goto out;
	}

	if (nr_new >= PN_XNUM) {
		fprintf(stderr, "%s: %zu program headers don't fit\n", __func__,
			nr_new);
		goto out;
	}

	if (!align || (align & (align - 1)))
		align = sysconf(_SC_PAGESIZE);

	note_new = sizeof(ehdr) + (__u64)nr_new * sizeof(ElfW(Phdr));
	head_len = note_new + note_len;
	body_start = (head_len + align - 1) & ~(align - 1);

	for (i = 0; i < nr_new; i++) {
		struct compact_piece *piece = &pieces[i];

		if (i == note_piece)
			piece->phdr.p_offset = note_new;
		else if (piece->phdr.p_filesz)
			piece->phdr.p_offset = compact_offset(ctx, body_start,
							      piece->src);
		else
			piece->phdr.p_offset = 0;
	}

	/* Only now is the head known. That's why it is uploaded last. */
	ehdr.e_phnum = nr_new;
	if (compact_write(fd_object, &ehdr, sizeof(ehdr)))
		goto out;

	for (i = 0; i < nr_new; i++)
		if (compact_write(fd_object, &pieces[i].phdr,
				  sizeof(pieces[i].phdr)))
			goto out;

	if (compact_write(fd_object, ctx->head + note_offset, note_len))
		goto out;

	/* Keep the segments aligned the way a debugger expects them. */
	memset(buffer, 0, sizeof(buffer));
	for (pos = head_len; pos < body_start; ) {
		__u64 chunk = body_start - pos;

		if (chunk > sizeof(buffer))
			chunk = sizeof(buffer);
		if (compact_write(fd_object, buffer, chunk))
			goto out;
		pos += chunk;
	}

	/* Putting the parts together is the blob store's job. Do it here. */
	for (pos = 0; pos < ctx->body_len; ) {
		ssize_t chunk = pread(ctx->fd_body, buffer, sizeof(buffer), pos);

		if (chunk <= 0) {
			fprintf(stderr, "%s: short read %zd: %m\n", __func__,
				chunk);
			goto out;
		}
		if (compact_write(fd_object, buffer, chunk))
			goto out;
		pos += chunk;
	}

	fprintf(stderr, "Object is %llu bytes in %zu program headers, %zu holes left out\n",
		(unsigned long long)(body_start + ctx->body_len), nr_new,
		ctx->nr_holes);
	ret = 0;
out:
	free(pieces);
	return ret;
}

/*
 * Reassemble a record stream into an ELF core file that has no holes in
 * it, the way a coredump server that uploads to a blob store has to. If
 * @fd_reference is valid it gets the coredump the records describe,
 * holes and all, so the test can compare the two.
 */
ssize_t recv_coredump_compact(int fd_coredump, int fd_object, int fd_reference,
			      off_t *coredump_size)
{
	struct compact_ctx ctx = {
		.fd_body	= -1,
		.fd_reference	= fd_reference,
	};
	struct coredump_record_sink sink = {
		.data	= compact_data,
		.zero	= compact_zero,
		.ctx	= &ctx,
	};
	ssize_t received;
	off_t size = 0;
	FILE *body;

	body = tmpfile();
	if (!body) {
		fprintf(stderr, "%s: tmpfile failed: %m\n", __func__);
		return -1;
	}
	ctx.fd_body = fileno(body);

	/* An upload is appended to. Make sure nothing here can seek. */
	if (fcntl(ctx.fd_body, F_SETFL, O_APPEND)) {
		fprintf(stderr, "%s: F_SETFL failed: %m\n", __func__);
		received = -1;
		goto out;
	}

	received = __recv_coredump_records(fd_coredump, &sink, &size, NULL, -1);
	if (received < 0)
		goto out;

	/*
	 * Nothing is written for a hole, so grow the reference to the size
	 * the records describe in case the coredump ended in one.
	 */
	if (ftruncate(fd_reference, size) < 0) {
		fprintf(stderr, "%s: ftruncate to %llu failed: %m\n",
			__func__, (unsigned long long)size);
		received = -1;
		goto out;
	}

	if (compact_build(&ctx, fd_object)) {
		received = -1;
		goto out;
	}

	if (coredump_size)
		*coredump_size = size;
out:
	fclose(body);
	free(ctx.head);
	free(ctx.holes);
	return received;
}

/* Read the ELF header and the program header table of @fd. */
static ElfW(Phdr) *read_phdrs(int fd, size_t *nr)
{
	ElfW(Ehdr) ehdr;
	ElfW(Phdr) *phdr;
	size_t size;

	if (pread(fd, &ehdr, sizeof(ehdr), 0) != sizeof(ehdr)) {
		fprintf(stderr, "%s: no ELF header: %m\n", __func__);
		return NULL;
	}

	if (!is_core_ehdr(&ehdr) || !ehdr.e_phnum ||
	    ehdr.e_phentsize != sizeof(*phdr)) {
		fprintf(stderr, "%s: not an ELF core file\n", __func__);
		return NULL;
	}

	size = (size_t)ehdr.e_phnum * ehdr.e_phentsize;
	phdr = malloc(size);
	if (!phdr) {
		fprintf(stderr, "%s: out of memory\n", __func__);
		return NULL;
	}

	if (pread(fd, phdr, size, ehdr.e_phoff) != (ssize_t)size) {
		fprintf(stderr, "%s: short program header table: %m\n", __func__);
		free(phdr);
		return NULL;
	}

	*nr = ehdr.e_phnum;
	return phdr;
}

/* The segment @vaddr falls into. */
static const ElfW(Phdr) *find_segment(const ElfW(Phdr) *phdr, size_t nr,
				      __u64 vaddr)
{
	size_t i;

	for (i = 0; i < nr; i++) {
		if (phdr[i].p_type != PT_LOAD)
			continue;
		if (vaddr >= phdr[i].p_vaddr &&
		    vaddr < phdr[i].p_vaddr + phdr[i].p_memsz)
			return &phdr[i];
	}

	return NULL;
}

/* The PT_LOAD segment @vaddr falls into. */
bool find_coredump_segment(int fd, __u64 vaddr, ElfW(Phdr) *segment)
{
	const ElfW(Phdr) *found;
	ElfW(Phdr) *phdr;
	size_t nr;

	phdr = read_phdrs(fd, &nr);
	if (!phdr)
		return false;

	found = find_segment(phdr, nr, vaddr);
	if (found)
		*segment = *found;
	else
		fprintf(stderr, "%s: no segment for 0x%llx\n", __func__,
			(unsigned long long)vaddr);

	free(phdr);
	return found;
}

/* How many bytes the PT_LOAD and the PT_NOTE segments of @fd carry. */
bool sum_coredump_segments(int fd, __u64 *data, __u64 *notes)
{
	ElfW(Phdr) *phdr;
	size_t nr, i;

	phdr = read_phdrs(fd, &nr);
	if (!phdr)
		return false;

	*data = 0;
	*notes = 0;
	for (i = 0; i < nr; i++) {
		if (phdr[i].p_type == PT_LOAD)
			*data += phdr[i].p_filesz;
		else if (phdr[i].p_type == PT_NOTE)
			*notes += phdr[i].p_filesz;
	}

	free(phdr);
	return true;
}

/* The coredump in @fd is at least as long as every segment it declares. */
bool check_coredump_extent(int fd)
{
	ElfW(Phdr) *phdr;
	struct stat st;
	size_t nr, i;
	bool ok = true;

	if (fstat(fd, &st)) {
		fprintf(stderr, "%s: fstat: %m\n", __func__);
		return false;
	}

	phdr = read_phdrs(fd, &nr);
	if (!phdr)
		return false;

	for (i = 0; i < nr; i++) {
		if (phdr[i].p_offset + phdr[i].p_filesz <= (__u64)st.st_size)
			continue;
		fprintf(stderr, "%s: segment %zu ends at %llu, the coredump at %llu\n",
			__func__, i,
			(unsigned long long)(phdr[i].p_offset + phdr[i].p_filesz),
			(unsigned long long)st.st_size);
		ok = false;
	}

	free(phdr);
	return ok;
}

/* The next stretch of memory the segments cover, split ones merged back. */
static bool next_range(const ElfW(Phdr) *phdr, size_t nr, size_t *i,
		       __u64 *start, __u64 *end)
{
	while (*i < nr && phdr[*i].p_type != PT_LOAD)
		(*i)++;

	if (*i >= nr)
		return false;

	*start = phdr[*i].p_vaddr;
	*end = phdr[*i].p_vaddr + phdr[*i].p_memsz;
	(*i)++;

	while (*i < nr) {
		if (phdr[*i].p_type != PT_LOAD) {
			(*i)++;
			continue;
		}
		if (phdr[*i].p_vaddr != *end)
			break;
		*end = phdr[*i].p_vaddr + phdr[*i].p_memsz;
		(*i)++;
	}

	return true;
}

/* Compare @len bytes at @offset against @len bytes at @offset_ref. */
static int compare_range(int fd, __u64 offset, int fd_ref, __u64 offset_ref,
			 __u64 len)
{
	char buffer[PAGE_SIZE], buffer_ref[PAGE_SIZE];

	while (len) {
		size_t chunk = len < sizeof(buffer) ? len : sizeof(buffer);

		if (pread(fd, buffer, chunk, offset) != (ssize_t)chunk ||
		    pread(fd_ref, buffer_ref, chunk, offset_ref) != (ssize_t)chunk) {
			fprintf(stderr, "%s: short read at %llu: %m\n",
				__func__, (unsigned long long)offset);
			return -1;
		}

		if (memcmp(buffer, buffer_ref, chunk)) {
			fprintf(stderr, "%s: %llu differs from %llu\n", __func__,
				(unsigned long long)offset,
				(unsigned long long)offset_ref);
			return -1;
		}

		offset += chunk;
		offset_ref += chunk;
		len -= chunk;
	}

	return 0;
}

/* The @len bytes at @offset the object left out have to have been zeroes. */
static int check_zero_range(int fd, __u64 offset, __u64 len)
{
	static const char zeroes[PAGE_SIZE];
	char buffer[PAGE_SIZE];

	while (len) {
		size_t chunk = len < sizeof(buffer) ? len : sizeof(buffer);

		if (pread(fd, buffer, chunk, offset) != (ssize_t)chunk) {
			fprintf(stderr, "%s: short read at %llu: %m\n",
				__func__, (unsigned long long)offset);
			return -1;
		}

		if (memcmp(buffer, zeroes, chunk)) {
			fprintf(stderr, "%s: %llu isn't a hole\n", __func__,
				(unsigned long long)offset);
			return -1;
		}

		offset += chunk;
		len -= chunk;
	}

	return 0;
}

/*
 * The object has to describe the same memory as the coredump it was built
 * from, and it has to describe it correctly.
 */
int check_compact_coredump(int fd_object, int fd_reference)
{
	ElfW(Phdr) *object = NULL, *reference = NULL;
	size_t nr_object, nr_reference, i;
	size_t io = 0, ir = 0;
	int ret = -1;

	object = read_phdrs(fd_object, &nr_object);
	reference = read_phdrs(fd_reference, &nr_reference);
	if (!object || !reference)
		goto out;

	/* Nothing may have been dropped and nothing may have been added. */
	for (;;) {
		__u64 start = 0, end = 0, start_ref = 0, end_ref = 0;
		bool has, has_ref;

		has = next_range(object, nr_object, &io, &start, &end);
		has_ref = next_range(reference, nr_reference, &ir, &start_ref,
				     &end_ref);
		if (!has && !has_ref)
			break;

		if (has != has_ref || start != start_ref || end != end_ref) {
			fprintf(stderr, "%s: object covers 0x%llx-0x%llx, coredump 0x%llx-0x%llx\n",
				__func__, (unsigned long long)start,
				(unsigned long long)end,
				(unsigned long long)start_ref,
				(unsigned long long)end_ref);
			goto out;
		}
	}

	for (i = 0; i < nr_object; i++) {
		const ElfW(Phdr) *segment;
		__u64 offset, dumped;

		if (object[i].p_type != PT_LOAD || !object[i].p_memsz)
			continue;

		segment = find_segment(reference, nr_reference,
				       object[i].p_vaddr);
		if (!segment) {
			fprintf(stderr, "%s: 0x%llx isn't in the coredump\n",
				__func__,
				(unsigned long long)object[i].p_vaddr);
			goto out;
		}

		offset = object[i].p_vaddr - segment->p_vaddr;
		dumped = offset < segment->p_filesz ?
				 segment->p_filesz - offset : 0;

		/* What the object carries is what the coredump had. */
		if (object[i].p_filesz > dumped) {
			fprintf(stderr, "%s: object carries %llu bytes the coredump doesn't have\n",
				__func__,
				(unsigned long long)(object[i].p_filesz - dumped));
			goto out;
		}

		if (compare_range(fd_object, object[i].p_offset, fd_reference,
				  segment->p_offset + offset,
				  object[i].p_filesz))
			goto out;

		/* And what it left out was a hole. */
		if (object[i].p_memsz > object[i].p_filesz &&
		    dumped > object[i].p_filesz) {
			__u64 left_out = dumped - object[i].p_filesz;

			if (left_out > object[i].p_memsz - object[i].p_filesz)
				left_out = object[i].p_memsz - object[i].p_filesz;

			if (check_zero_range(fd_reference,
					     segment->p_offset + offset +
					     object[i].p_filesz, left_out))
				goto out;
		}
	}

	ret = 0;
out:
	free(object);
	free(reference);
	return ret;
}

/* Read a plain coredump byte stream to end-of-file. */
ssize_t recv_coredump_bytes(int fd_coredump, int fd_core_file)
{
	ssize_t received = 0;

	for (;;) {
		char buffer[PAGE_SIZE];
		ssize_t ret = read_nointr(fd_coredump, buffer, sizeof(buffer));

		if (ret < 0) {
			fprintf(stderr, "%s: read failed: %m\n", __func__);
			return -1;
		}
		if (ret == 0)
			break;

		if (write_nointr(fd_core_file, buffer, ret) != ret) {
			fprintf(stderr, "%s: write failed: %m\n", __func__);
			return -1;
		}
		received += ret;
	}

	fprintf(stderr, "Received %zd bytes of coredump\n", received);
	return received;
}

int create_detached_tmpfs(void)
{
	int fd_context, fd_tmpfs;

	fd_context = sys_fsopen("tmpfs", 0);
	if (fd_context < 0)
		return -1;

	if (sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0) < 0)
		return -1;

	fd_tmpfs = sys_fsmount(fd_context, 0, 0);
	close(fd_context);
	return fd_tmpfs;
}

int create_and_listen_unix_socket(const char *path)
{
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	assert(strlen(path) < sizeof(addr.sun_path) - 1);
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	size_t addr_len =
		offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1;
	int fd, ret;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		goto out;

	ret = bind(fd, (const struct sockaddr *)&addr, addr_len);
	if (ret < 0)
		goto out;

	ret = listen(fd, 128);
	if (ret < 0)
		goto out;

	return fd;

out:
	fprintf(stderr, "%s: %s: %m\n", __func__, path);
	if (fd >= 0)
		close(fd);
	return -1;
}

bool set_core_pattern(const char *pattern)
{
	int fd;
	ssize_t ret;

	fd = open("/proc/sys/kernel/core_pattern", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	ret = write(fd, pattern, strlen(pattern));
	close(fd);
	if (ret < 0)
		return false;

	fprintf(stderr, "Set core_pattern to '%s' | %zu == %zu\n", pattern, ret, strlen(pattern));
	return ret == strlen(pattern);
}

int get_peer_pidfd(int fd)
{
	int fd_peer_pidfd;
	socklen_t fd_peer_pidfd_len = sizeof(fd_peer_pidfd);
	int ret = getsockopt(fd, SOL_SOCKET, SO_PEERPIDFD, &fd_peer_pidfd,
			     &fd_peer_pidfd_len);
	if (ret < 0) {
		fprintf(stderr, "get_peer_pidfd: getsockopt(SO_PEERPIDFD) failed: %m\n");
		return -1;
	}
	fprintf(stderr, "get_peer_pidfd: successfully retrieved pidfd %d\n", fd_peer_pidfd);
	return fd_peer_pidfd;
}

bool get_pidfd_info(int fd_peer_pidfd, struct pidfd_info *info)
{
	int ret;
	memset(info, 0, sizeof(*info));
	info->mask = PIDFD_INFO_EXIT | PIDFD_INFO_COREDUMP | PIDFD_INFO_COREDUMP_SIGNAL;
	ret = ioctl(fd_peer_pidfd, PIDFD_GET_INFO, info);
	if (ret < 0) {
		fprintf(stderr, "get_pidfd_info: ioctl(PIDFD_GET_INFO) failed: %m\n");
		return false;
	}
	fprintf(stderr, "get_pidfd_info: mask=0x%llx, coredump_mask=0x%x, coredump_signal=%d, coredump_code=%d\n",
		(unsigned long long)info->mask, info->coredump_mask, info->coredump_signal, info->coredump_code);
	return true;
}

/*
 * How much the peer has mapped. The task is parked in the coredump
 * handshake, so its mm is still there to be looked at.
 */
ssize_t peer_vm_size(int fd_peer_pidfd)
{
	struct pidfd_info info = {};
	unsigned long pages;
	char path[64];
	FILE *f;

	if (!get_pidfd_info(fd_peer_pidfd, &info))
		return -1;

	snprintf(path, sizeof(path), "/proc/%d/statm", info.pid);
	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "%s: %s: %m\n", __func__, path);
		return -1;
	}

	if (fscanf(f, "%lu", &pages) != 1) {
		fprintf(stderr, "%s: %s: no size\n", __func__, path);
		fclose(f);
		return -1;
	}
	fclose(f);

	return (ssize_t)pages * sysconf(_SC_PAGESIZE);
}

/* Protocol helper functions */

/* The peer's /proc/<pid>/coredump_filter, which is in memory types. */
bool peer_coredump_filter(int fd_peer_pidfd, __u64 *memory_types)
{
	struct pidfd_info info = {};
	unsigned long value;
	char path[64];
	FILE *f;
	int ret;

	if (!get_pidfd_info(fd_peer_pidfd, &info))
		return false;

	snprintf(path, sizeof(path), "/proc/%d/coredump_filter", info.pid);
	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "%s: %s: %m\n", __func__, path);
		return false;
	}

	ret = fscanf(f, "%lx", &value);
	fclose(f);
	if (ret != 1) {
		fprintf(stderr, "%s: %s: no value\n", __func__, path);
		return false;
	}

	*memory_types = value;
	return true;
}

/* Read @len bytes at @addr from the peer's /proc/<pid>/mem. */
ssize_t peer_read_mem(int fd_peer_pidfd, __u64 addr, void *buf, size_t len)
{
	struct pidfd_info info = {};
	char path[64];
	ssize_t ret;
	int fd;

	if (!get_pidfd_info(fd_peer_pidfd, &info))
		return -1;

	snprintf(path, sizeof(path), "/proc/%d/mem", info.pid);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "%s: %s: %m\n", __func__, path);
		return -1;
	}

	ret = pread(fd, buf, len, addr);
	if (ret < 0)
		fprintf(stderr, "%s: %s at 0x%llx: %m\n", __func__, path,
			(unsigned long long)addr);
	close(fd);
	return ret;
}

ssize_t recv_marker(int fd)
{
	enum coredump_mark mark = COREDUMP_MARK_REQACK;
	ssize_t ret;

	ret = recv(fd, &mark, sizeof(mark), MSG_WAITALL);
	if (ret != sizeof(mark))
		return -1;

	switch (mark) {
	case COREDUMP_MARK_REQACK:
		fprintf(stderr, "Received marker: ReqAck\n");
		return COREDUMP_MARK_REQACK;
	case COREDUMP_MARK_MINSIZE:
		fprintf(stderr, "Received marker: MinSize\n");
		return COREDUMP_MARK_MINSIZE;
	case COREDUMP_MARK_MAXSIZE:
		fprintf(stderr, "Received marker: MaxSize\n");
		return COREDUMP_MARK_MAXSIZE;
	case COREDUMP_MARK_UNSUPPORTED:
		fprintf(stderr, "Received marker: Unsupported\n");
		return COREDUMP_MARK_UNSUPPORTED;
	case COREDUMP_MARK_CONFLICTING:
		fprintf(stderr, "Received marker: Conflicting\n");
		return COREDUMP_MARK_CONFLICTING;
	default:
		fprintf(stderr, "Received unknown marker: %u\n", mark);
		break;
	}
	return -1;
}

bool read_marker(int fd, enum coredump_mark mark)
{
	ssize_t ret;

	ret = recv_marker(fd);
	if (ret < 0)
		return false;
	return ret == mark;
}

/*
 * The kernel hung up without sending anything more: end of stream, or a
 * reset if it refused the ack on its peeked size and never read it.
 */
bool read_hangup(int fd)
{
	ssize_t ret;
	char c;

	ret = recv(fd, &c, sizeof(c), MSG_WAITALL);
	if (ret == 0) {
		fprintf(stderr, "Kernel closed the connection\n");
		return true;
	}
	if (ret < 0 && errno == ECONNRESET) {
		fprintf(stderr, "Kernel closed the connection with the ack unread\n");
		return true;
	}

	fprintf(stderr, "%s: expected a hangup, got %zd: %m\n", __func__, ret);
	return false;
}

/* Read the request as a server built with a @user_size byte struct does. */
bool read_coredump_req_sized(int fd, struct coredump_req *req, size_t user_size)
{
	ssize_t ret;
	size_t field_size, known_size, kernel_size, remaining_size;

	memset(req, 0, sizeof(*req));
	field_size = sizeof(req->size);

	/* Peek the size of the coredump request. */
	ret = recv(fd, req, field_size, MSG_PEEK | MSG_WAITALL);
	if (ret != field_size) {
		fprintf(stderr, "%s: peek failed (got %zd, expected %zu): %m\n", __func__,
			ret, field_size);
		return false;
	}
	kernel_size = req->size;

	if (kernel_size < COREDUMP_REQ_SIZE_VER0) {
		fprintf(stderr, "%s: kernel_size %zu < min %d\n", __func__,
			kernel_size, COREDUMP_REQ_SIZE_VER0);
		return false;
	}
	if (kernel_size >= PAGE_SIZE) {
		fprintf(stderr, "%s: kernel_size %zu >= PAGE_SIZE %d\n", __func__,
			kernel_size, PAGE_SIZE);
		return false;
	}

	/* Consume as much of the request as we know about. */
	known_size = user_size < kernel_size ? user_size : kernel_size;
	ret = recv(fd, req, known_size, MSG_WAITALL);
	if (ret != known_size)
		return false;

	fprintf(stderr, "Read coredump request with size %u and mask 0x%llx\n",
		req->size, (unsigned long long)req->mask);

	if (kernel_size > user_size)
		remaining_size = kernel_size - user_size;
	else
		remaining_size = 0;

	if (PAGE_SIZE <= remaining_size)
		return false;

	/*
	 * Discard any additional data if the kernel's request was larger than
	 * what we knew about or cared about.
	 */
	if (remaining_size) {
		char buffer[PAGE_SIZE];

		ret = recv(fd, buffer, remaining_size, MSG_WAITALL);
		if (ret != remaining_size)
			return false;
		fprintf(stderr, "Discarded %zu bytes of data after coredump request\n", remaining_size);
	}

	return true;
}

bool read_coredump_req(int fd, struct coredump_req *req)
{
	return read_coredump_req_sized(fd, req, sizeof(*req));
}

/* Send @len bytes of @ack as they are, more than the struct if asked to. */
bool send_coredump_ack_bytes(int fd, const struct coredump_ack *ack, size_t len)
{
	ssize_t ret;
	/*
	 * Wrap struct coredump_ack in a larger struct so we can
	 * simulate sending to much data to the kernel.
	 */
	struct large_ack_for_size_testing {
		struct coredump_ack ack;
		char buffer[PAGE_SIZE];
	} large_ack = {};

	if (len > sizeof(large_ack))
		return false;

	large_ack.ack = *ack;
	ret = send(fd, &large_ack, len, MSG_NOSIGNAL);
	if (ret != len) {
		fprintf(stderr, "%s: short send %zd: %m\n", __func__, ret);
		return false;
	}

	fprintf(stderr, "Sent %zu bytes of coredump ack: size %u, mask 0x%llx, types 0x%llx\n",
		len, ack->size, (unsigned long long)ack->mask,
		(unsigned long long)ack->memory_types);
	return true;
}

bool send_coredump_ack_types(int fd, const struct coredump_req *req,
			      __u64 mask, __u64 memory_types, size_t size_ack)
{
	struct coredump_ack ack = {
		.mask = mask,
		.memory_types = memory_types,
	};

	if (!size_ack)
		size_ack = sizeof(struct coredump_ack) < req->size_ack ?
				   sizeof(struct coredump_ack) :
				   req->size_ack;
	ack.size = size_ack;
	return send_coredump_ack_bytes(fd, &ack, size_ack);
}

bool send_coredump_ack(int fd, const struct coredump_req *req,
		       __u64 mask, size_t size_ack)
{
	return send_coredump_ack_types(fd, req, mask, 0, size_ack);
}

/* Every option the kernel is expected to advertise in coredump_req->mask. */
#define TEST_REQ_MASK_ALL					\
	(COREDUMP_KERNEL | COREDUMP_USERSPACE |			\
	 COREDUMP_REJECT | COREDUMP_WAIT |			\
	 COREDUMP_RECORDS | COREDUMP_SPARSE | COREDUMP_MEMORY_TYPES)

bool check_coredump_req(const struct coredump_req *req)
{
	if (req->size != COREDUMP_REQ_SIZE_VER1) {
		fprintf(stderr, "%s: size %u, expected %d\n",
			__func__, req->size, COREDUMP_REQ_SIZE_VER1);
		return false;
	}
	if (req->size_ack != COREDUMP_ACK_SIZE_VER1) {
		fprintf(stderr, "%s: size_ack %u, expected %d\n",
			__func__, req->size_ack, COREDUMP_ACK_SIZE_VER1);
		return false;
	}
	if (req->mask != TEST_REQ_MASK_ALL) {
		fprintf(stderr, "%s: mask 0x%llx, expected 0x%llx\n",
			__func__, (unsigned long long)req->mask,
			(unsigned long long)TEST_REQ_MASK_ALL);
		return false;
	}
	if (req->memory_types_mask != TEST_MEMORY_ALL) {
		fprintf(stderr, "%s: memory_types_mask 0x%llx, expected 0x%llx\n",
			__func__, (unsigned long long)req->memory_types_mask,
			(unsigned long long)TEST_MEMORY_ALL);
		return false;
	}
	return true;
}

int open_coredump_tmpfile(int fd_tmpfs_detached)
{
	return openat(fd_tmpfs_detached, ".", O_TMPFILE | O_RDWR | O_EXCL, 0600);
}

void process_coredump_worker(int fd_coredump, int fd_peer_pidfd, int fd_core_file)
{
	int epfd = -1;
	int exit_code = EXIT_FAILURE;
	struct epoll_event ev;
	int flags;

	/* Set socket to non-blocking mode for edge-triggered epoll */
	flags = fcntl(fd_coredump, F_GETFL, 0);
	if (flags < 0) {
		fprintf(stderr, "Worker: fcntl(F_GETFL) failed: %m\n");
		goto out;
	}
	if (fcntl(fd_coredump, F_SETFL, flags | O_NONBLOCK) < 0) {
		fprintf(stderr, "Worker: fcntl(F_SETFL, O_NONBLOCK) failed: %m\n");
		goto out;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		fprintf(stderr, "Worker: epoll_create1() failed: %m\n");
		goto out;
	}

	ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
	ev.data.fd = fd_coredump;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd_coredump, &ev) < 0) {
		fprintf(stderr, "Worker: epoll_ctl(EPOLL_CTL_ADD) failed: %m\n");
		goto out;
	}

	for (;;) {
		struct epoll_event events[1];
		int n = epoll_wait(epfd, events, 1, -1);
		if (n < 0) {
			fprintf(stderr, "Worker: epoll_wait() failed: %m\n");
			break;
		}

		if (events[0].events & (EPOLLIN | EPOLLRDHUP)) {
			for (;;) {
				char buffer[4096];
				ssize_t bytes_read = read(fd_coredump, buffer, sizeof(buffer));
				if (bytes_read < 0) {
					if (errno == EAGAIN || errno == EWOULDBLOCK)
						break;
					fprintf(stderr, "Worker: read() failed: %m\n");
					goto out;
				}
				if (bytes_read == 0)
					goto done;
				ssize_t bytes_write = write(fd_core_file, buffer, bytes_read);
				if (bytes_write != bytes_read) {
					if (bytes_write < 0 && errno == ENOSPC)
						continue;
					fprintf(stderr, "Worker: write() failed (read=%zd, write=%zd): %m\n",
						bytes_read, bytes_write);
					goto out;
				}
			}
		}
	}

done:
	exit_code = EXIT_SUCCESS;
	fprintf(stderr, "Worker: completed successfully\n");
out:
	if (epfd >= 0)
		close(epfd);
	if (fd_core_file >= 0)
		close(fd_core_file);
	if (fd_peer_pidfd >= 0)
		close(fd_peer_pidfd);
	if (fd_coredump >= 0)
		close(fd_coredump);
	_exit(exit_code);
}

/*
 * TIF_NOTIFY_SIGNAL coredump helpers.
 *
 * __dump_emit() takes anything short of a full write as the end of the
 * dump, so every emit that blocks on a full transport can lose the rest
 * of it. The NT_FILE note is the one emit that is certain to block,
 * because it is the only one larger than the transport, so these helpers
 * build a note large enough for that and then raise TIF_NOTIFY_SIGNAL on
 * the dumping task while that write is in flight.
 */

static int io_uring_setup_raw(unsigned int entries, struct io_uring_params *p)
{
	return syscall(__NR_io_uring_setup, entries, p);
}

/* io_uring reads poll32_events back through swahw32() on big endian. */
static __u32 notify_poll_mask(__u32 events)
{
#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN
	return __swahw32(events);
#else
	return events;
#endif
}

bool coredump_io_uring_available(void)
{
	struct io_uring_params p = {};
	int fd;

	fd = io_uring_setup_raw(1, &p);
	if (fd < 0)
		return false;
	close(fd);
	return true;
}

/*
 * Arm a poll on @fd. io_uring leaves ctx->notify_method at TWA_SIGNAL
 * unless the ring asks for SQPOLL or COOP_TASKRUN, so the completion runs
 * set_notify_signal() against the task that submitted it. That is us, and
 * we are about to become the coredumping task.
 */
static int arm_poll_notify(int trigger_fd)
{
	struct io_uring_params p = {};
	unsigned int *sq_tail, *sq_array;
	struct io_uring_sqe *sqes;
	size_t sqring_sz;
	void *sq;
	int ring;

	ring = io_uring_setup_raw(8, &p);
	if (ring < 0)
		return -1;

	sqring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned int);
	sq = mmap(NULL, sqring_sz, PROT_READ | PROT_WRITE,
		  MAP_SHARED | MAP_POPULATE, ring, IORING_OFF_SQ_RING);
	if (sq == MAP_FAILED)
		return -1;

	sqes = mmap(NULL, p.sq_entries * sizeof(*sqes), PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_POPULATE, ring, IORING_OFF_SQES);
	if (sqes == MAP_FAILED)
		return -1;

	sq_tail = (unsigned int *)((char *)sq + p.sq_off.tail);
	sq_array = (unsigned int *)((char *)sq + p.sq_off.array);

	memset(&sqes[0], 0, sizeof(sqes[0]));
	sqes[0].opcode = IORING_OP_POLL_ADD;
	sqes[0].fd = trigger_fd;
	sqes[0].poll32_events = notify_poll_mask(POLLIN);

	sq_array[0] = 0;
	__atomic_store_n(sq_tail, 1, __ATOMIC_RELEASE);

	if (syscall(__NR_io_uring_enter, ring, 1, 0, 0, NULL, 0) < 0)
		return -1;

	/* Deliberately leaked, we are about to crash. */
	return 0;
}

/*
 * Adjacent mappings with identical flags and contiguous file offsets are
 * merged into one VMA, which would collapse NT_FILE back to nothing, so
 * alternate the protection to keep every mapping an entry of its own.
 * Not PROT_EXEC, /tmp is often mounted noexec. Nothing is ever written
 * through these so they get no anon_vma and stay out of the dump itself.
 */
static int make_file_mappings(void)
{
	long pgsz = sysconf(_SC_PAGESIZE);
	int fd, i;

	fd = open(NOTIFY_SIGNAL_MAPFILE,
		  O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return 0;
	if (ftruncate(fd, (off_t)NOTIFY_SIGNAL_MAP_COUNT * pgsz)) {
		close(fd);
		return 0;
	}

	for (i = 0; i < NOTIFY_SIGNAL_MAP_COUNT; i++) {
		int prot = (i & 1) ? PROT_READ : (PROT_READ | PROT_WRITE);

		if (mmap(NULL, pgsz, prot, MAP_PRIVATE, fd,
			 (off_t)i * pgsz) == MAP_FAILED)
			break;
	}
	close(fd);
	return i;
}

void crashing_child_notify_signal(void)
{
	long pgsz = sysconf(_SC_PAGESIZE);
	unsigned char *p;
	int trigger_fd;
	unsigned long off;

	/* Open the read side first so the reader's open() cannot block. */
	trigger_fd = open(NOTIFY_SIGNAL_TRIGGER, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

	/* Exit rather than crash: a dump without the poll armed proves nothing. */
	if (trigger_fd < 0)
		_exit(EXIT_FAILURE);

	if (make_file_mappings() < NOTIFY_SIGNAL_MAP_COUNT)
		_exit(EXIT_FAILURE);

	p = mmap(NULL, NOTIFY_SIGNAL_ANON_BYTES, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		_exit(EXIT_FAILURE);
	for (off = 0; off < NOTIFY_SIGNAL_ANON_BYTES; off += pgsz)
		p[off] = 1;

	if (arm_poll_notify(trigger_fd))
		_exit(EXIT_FAILURE);

	/* crash on purpose */
	*(volatile int *)NULL = 0;
	_exit(EXIT_FAILURE);
}

static int pull_trigger(void)
{
	int fd;

	fd = open(NOTIFY_SIGNAL_TRIGGER, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "%s: open failed: %m\n", __func__);
		return -1;
	}
	if (write(fd, "x", 1) != 1) {
		fprintf(stderr, "%s: write failed: %m\n", __func__);
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

/*
 * phdr[0] is the PT_NOTE entry: elf_core_dump() emits it right after the
 * ELF header. Its p_offset is where the notes begin.
 */
static long long note_offset(const unsigned char *hdr)
{
	ElfW(Phdr) ph;
	ElfW(Ehdr) eh;

	memcpy(&eh, hdr, sizeof(eh));
	if (memcmp(eh.e_ident, ELFMAG, SELFMAG) || eh.e_type != ET_CORE)
		return -1;
	memcpy(&ph, hdr + sizeof(eh), sizeof(ph));
	if (ph.p_type != PT_NOTE)
		return -1;
	return (long long)ph.p_offset;
}

/*
 * Drain a coredump off @fd, counting what arrives. Once the notes have
 * started the kernel is inside the one big note write, so poke the fifo
 * the crashing task is polling and stop reading, which keeps the
 * transport full and the write blocked with a partial count when the
 * wakeup lands. Then carry on to end of file.
 *
 * What arrives is written to @fd_out when that is not negative.
 * Returns the number of bytes received, or -1. Failing to trip the fifo
 * is an error too: a dump that was never interrupted proves nothing.
 */
ssize_t recv_coredump_notify_signal(int fd, int fd_out, bool arm)
{
	unsigned char hdr[sizeof(ElfW(Ehdr)) + sizeof(ElfW(Phdr))];
	static char buf[64 << 10];
	long pgsz = sysconf(_SC_PAGESIZE);
	long long note_off = 0;
	size_t hdrlen = 0;
	ssize_t total = 0;
	bool armed = false;

	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			break;
		if (fd_out >= 0 && write(fd_out, buf, n) != n)
			return -1;

		if (hdrlen < sizeof(hdr)) {
			size_t want = sizeof(hdr) - hdrlen;

			if (want > (size_t)n)
				want = (size_t)n;
			memcpy(hdr + hdrlen, buf, want);
			hdrlen += want;
			if (hdrlen == sizeof(hdr))
				note_off = note_offset(hdr);
		}

		total += n;

		if (arm && !armed && note_off > 0 &&
		    total > note_off + (long long)pgsz) {
			if (pull_trigger())
				return -1;
			armed = true;
			usleep(NOTIFY_SIGNAL_STALL_US);
			continue;
		}
	}

	if (arm && !armed)
		return -1;

	return total;
}

/*
 * How large the dump was meant to be. The ELF header and the program
 * headers are the first thing emitted, so even a truncated dump says how
 * far it should have run: the end is max(p_offset + p_filesz).
 *
 * That end is exact even when the last segment ends in a hole:
 * coredump_write() flushes the pending cprm->to_skip with a final one
 * byte emit and __dump_skip() writes zeroes for transports that cannot
 * seek, so a whole dump carries every byte the headers promise.
 */
long long coredump_expected_size(const char *path)
{
	ElfW(Phdr) *phdr = NULL;
	long long expected = 0;
	unsigned int nphdr, i;
	ElfW(Ehdr) eh;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (read(fd, &eh, sizeof(eh)) != sizeof(eh))
		goto err;
	if (memcmp(eh.e_ident, ELFMAG, SELFMAG) || eh.e_type != ET_CORE)
		goto err;
	if (!eh.e_phnum || eh.e_phentsize != sizeof(*phdr))
		goto err;

	nphdr = eh.e_phnum;
	phdr = calloc(nphdr, sizeof(*phdr));
	if (!phdr)
		goto err;
	if (pread(fd, phdr, (size_t)nphdr * sizeof(*phdr), (off_t)eh.e_phoff) !=
	    (ssize_t)((size_t)nphdr * sizeof(*phdr)))
		goto err;

	for (i = 0; i < nphdr; i++) {
		long long end = (long long)phdr[i].p_offset +
				(long long)phdr[i].p_filesz;
		if (end > expected)
			expected = end;
	}

	free(phdr);
	close(fd);
	return expected;
err:
	free(phdr);
	close(fd);
	return -1;
}
