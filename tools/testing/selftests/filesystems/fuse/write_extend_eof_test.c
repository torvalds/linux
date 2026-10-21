// SPDX-License-Identifier: GPL-2.0
/*
 * Regression test for the fuse write-extend partial-EOF-page zeroing bug.
 *
 * A buffered write that extends i_size past a non-page-aligned EOF must zero
 * the tail of the old last page.  If an application has mmap'd that page and
 * stored into the post-EOF region (undefined until the file grows), the
 * now-in-bounds tail must read back as zero, not as the stale stored bytes.
 *
 * The bug is exposed on a non-writeback_cache server that keeps the page cache
 * across the write (FOPEN_KEEP_CACHE without FOPEN_DIRECT_IO).  This test is a
 * raw /dev/fuse server in that mode; the backing data is always zero in the
 * hole, so any non-zero byte a read sees is stale page-cache data.
 *
 * Requires root to mount fuse.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/falloc.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <linux/fuse.h>

#include "../../kselftest_harness.h"

#define FUSE_ROOT_ID	1
#define FILE_INO	2
#define MAX_WRITE	(128 * 1024)
#define BACKING_SIZE	(4 * 1024 * 1024)
#define POLLUTE		0xee

/* Server-side state, shared with the responder thread. */
struct server {
	int fd;
	unsigned char backing[BACKING_SIZE];	/* authoritative bytes */
	uint64_t size;
};

static void reply(int fd, uint64_t unique, int error, void *data, size_t len)
{
	struct fuse_out_header oh = {
		.len = sizeof(oh) + (data ? len : 0),
		.error = error,
		.unique = unique,
	};
	struct iovec iov[2] = { { &oh, sizeof(oh) }, { data, len } };

	/* Errors here are teardown races (device closed on unmount); ignore. */
	if (writev(fd, iov, data ? 2 : 1) < 0)
		return;
}

static void fill_attr(struct fuse_attr *a, uint64_t ino, uint32_t mode,
		      uint64_t size)
{
	memset(a, 0, sizeof(*a));
	a->ino = ino;
	a->mode = mode;
	a->nlink = 1;
	a->size = size;
	a->blksize = sysconf(_SC_PAGESIZE);
}

static void *server_thread(void *arg)
{
	struct server *s = arg;
	static char buf[MAX_WRITE + 4096];

	for (;;) {
		ssize_t n = read(s->fd, buf, sizeof(buf));
		struct fuse_in_header *ih = (void *)buf;

		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return NULL;	/* device closed on unmount */
		}
		if (n < (ssize_t)sizeof(*ih))
			continue;

		switch (ih->opcode) {
		case FUSE_INIT: {
			struct fuse_init_in *in = (void *)(ih + 1);
			struct fuse_init_out out = {0};

			/* No FUSE_WRITEBACK_CACHE: the exposed configuration. */
			out.major = FUSE_KERNEL_VERSION;
			out.minor = FUSE_KERNEL_MINOR_VERSION;
			out.max_readahead = in->max_readahead;
			out.max_write = MAX_WRITE;
			out.max_background = 16;
			out.congestion_threshold = 12;
			out.flags = FUSE_MAX_PAGES;
			out.max_pages = MAX_WRITE / sysconf(_SC_PAGESIZE);
			reply(s->fd, ih->unique, 0, &out, sizeof(out));
			break;
		}
		case FUSE_GETATTR: {
			struct fuse_attr_out out = {0};
			int root = ih->nodeid == FUSE_ROOT_ID;

			out.attr_valid = 3600;
			fill_attr(&out.attr, ih->nodeid,
				  root ? (S_IFDIR | 0755) : (S_IFREG | 0644),
				  root ? 0 : s->size);
			reply(s->fd, ih->unique, 0, &out, sizeof(out));
			break;
		}
		case FUSE_LOOKUP: {
			struct fuse_entry_out out = {0};

			out.nodeid = FILE_INO;
			out.attr_valid = 3600;
			out.entry_valid = 3600;
			fill_attr(&out.attr, FILE_INO, S_IFREG | 0644, s->size);
			reply(s->fd, ih->unique, 0, &out, sizeof(out));
			break;
		}
		case FUSE_OPEN:
		case FUSE_OPENDIR: {
			struct fuse_open_out out = {0};

			/* Keep the cache across the write, but not direct I/O. */
			out.open_flags = FOPEN_KEEP_CACHE;
			reply(s->fd, ih->unique, 0, &out, sizeof(out));
			break;
		}
		case FUSE_READ: {
			struct fuse_read_in *in = (void *)(ih + 1);
			uint64_t off = in->offset;
			uint32_t size = in->size;

			if (off >= BACKING_SIZE)
				size = 0;
			else if (off + size > BACKING_SIZE)
				size = BACKING_SIZE - off;
			reply(s->fd, ih->unique, 0, s->backing + off, size);
			break;
		}
		case FUSE_WRITE: {
			struct fuse_write_in *in = (void *)(ih + 1);
			struct fuse_write_out out = {0};
			uint64_t off = in->offset;
			uint32_t size = in->size;

			if (off < BACKING_SIZE) {
				uint32_t c = size;

				if (off + c > BACKING_SIZE)
					c = BACKING_SIZE - off;
				memcpy(s->backing + off, in + 1, c);
				if (off + c > s->size)
					s->size = off + c;
			}
			out.size = size;
			reply(s->fd, ih->unique, 0, &out, sizeof(out));
			break;
		}
		case FUSE_SETATTR: {
			struct fuse_setattr_in *in = (void *)(ih + 1);
			struct fuse_attr_out out = {0};

			if ((in->valid & FATTR_SIZE) && in->size <= BACKING_SIZE) {
				if (in->size > s->size)
					memset(s->backing + s->size, 0,
					       in->size - s->size);
				s->size = in->size;
			}
			out.attr_valid = 3600;
			fill_attr(&out.attr, ih->nodeid, S_IFREG | 0644, s->size);
			reply(s->fd, ih->unique, 0, &out, sizeof(out));
			break;
		}
		case FUSE_FALLOCATE: {
			struct fuse_fallocate_in *in = (void *)(ih + 1);
			uint64_t end = in->offset + in->length;

			/* Only plain (size-extending) fallocate is used here. */
			if (!(in->mode & FALLOC_FL_KEEP_SIZE) &&
			    end <= BACKING_SIZE && end > s->size) {
				memset(s->backing + s->size, 0, end - s->size);
				s->size = end;
			}
			reply(s->fd, ih->unique, 0, NULL, 0);
			break;
		}
		case FUSE_FLUSH:
		case FUSE_RELEASE:
		case FUSE_RELEASEDIR:
		case FUSE_FSYNC:
		case FUSE_ACCESS:
			reply(s->fd, ih->unique, 0, NULL, 0);
			break;
		case FUSE_FORGET:
			break;
		default:
			reply(s->fd, ih->unique, -EOPNOTSUPP, NULL, 0);
			break;
		}
	}
}

FIXTURE(fuse)
{
	struct server *srv;
	pthread_t thread;
	char dir[64];
	long page;		/* runtime page size */
	off_t eof;		/* mid-page EOF, page-relative */
	int fd;			/* open test file */
	char *map;		/* mmap of the EOF page */
	int mounted;
};

FIXTURE_SETUP(fuse)
{
	char opts[128];
	pthread_t t;

	if (geteuid() != 0)
		SKIP(return, "need root to mount fuse");

	self->page = sysconf(_SC_PAGESIZE);
	self->fd = -1;
	self->map = MAP_FAILED;

	self->srv = mmap(NULL, sizeof(*self->srv), PROT_READ | PROT_WRITE,
			 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(MAP_FAILED, self->srv);

	self->srv->fd = open("/dev/fuse", O_RDWR);
	ASSERT_GE(self->srv->fd, 0);

	strcpy(self->dir, "/tmp/fuse_weof_XXXXXX");
	ASSERT_NE(NULL, mkdtemp(self->dir));

	snprintf(opts, sizeof(opts),
		 "fd=%d,rootmode=40000,user_id=0,group_id=0",
		 self->srv->fd);
	ASSERT_EQ(0, mount("fuse", self->dir, "fuse", 0, opts));
	self->mounted = 1;

	ASSERT_EQ(0, pthread_create(&t, NULL, server_thread, self->srv));
	self->thread = t;
}

FIXTURE_TEARDOWN(fuse)
{
	if (self->map != MAP_FAILED)
		munmap(self->map, self->page);
	if (self->fd >= 0)
		close(self->fd);
	if (self->mounted)
		umount2(self->dir, MNT_DETACH);
	if (self->srv && self->srv != MAP_FAILED) {
		if (self->srv->fd > 0)
			close(self->srv->fd);
		munmap(self->srv, sizeof(*self->srv));
	}
	if (self->dir[0])
		rmdir(self->dir);
}

/*
 * Create the test file with a mid-page EOF and mmap-store POLLUTE into its
 * post-EOF tail (a legal store, undefined until the file grows).  Leaves the
 * file open and the EOF page mapped in the fixture for the caller to extend.
 */
static void pollute_eof_tail(struct __test_metadata *_metadata,
			     FIXTURE_DATA(fuse) * self)
{
	off_t eof = 2 * self->page + self->page / 4;
	char path[128];
	char *buf;

	snprintf(path, sizeof(path), "%s/file", self->dir);
	self->fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	ASSERT_GE(self->fd, 0);
	self->eof = eof;

	buf = malloc(eof);
	ASSERT_NE(NULL, buf);
	memset(buf, 'A', eof);
	ASSERT_EQ(eof, pwrite(self->fd, buf, eof, 0));
	free(buf);

	self->map = mmap(NULL, self->page, PROT_READ | PROT_WRITE, MAP_SHARED,
			 self->fd, eof & ~(self->page - 1));
	ASSERT_NE(MAP_FAILED, self->map);
	memset(self->map + (eof & (self->page - 1)), POLLUTE,
	       self->page - (eof & (self->page - 1)));
}

/* Assert the old post-EOF tail [eof, end of its page) now reads back as zero. */
static void assert_tail_zeroed(struct __test_metadata *_metadata,
			       FIXTURE_DATA(fuse) * self)
{
	off_t base = self->eof & ~(self->page - 1);
	char *tail = malloc(self->page);
	int i;

	ASSERT_NE(NULL, tail);
	ASSERT_EQ(self->page, pread(self->fd, tail, self->page, base));
	for (i = self->eof & (self->page - 1); i < self->page; i++)
		ASSERT_EQ(0, tail[i]);
	free(tail);
}

/* Basic: pollute the post-EOF tail, extend past it by a later write. */
TEST_F(fuse, write_extend)
{
	pollute_eof_tail(_metadata, self);
	ASSERT_EQ(4, pwrite(self->fd, "data", 4, 5 * self->page + self->page / 3));
	assert_tail_zeroed(_metadata, self);
}

/* Extend via ftruncate() rather than a write. */
TEST_F(fuse, ftruncate_extend)
{
	pollute_eof_tail(_metadata, self);
	ASSERT_EQ(0, ftruncate(self->fd, 8 * self->page));
	assert_tail_zeroed(_metadata, self);
}

/* Extend via fallocate() starting at the old EOF. */
TEST_F(fuse, fallocate_extend)
{
	pollute_eof_tail(_metadata, self);
	ASSERT_EQ(0, fallocate(self->fd, 0, self->eof, 4 * self->page));
	assert_tail_zeroed(_metadata, self);
}

/* A write landing inside the old EOF page must not clobber its own data. */
TEST_F(fuse, extend_into_eof_page_preserves_data)
{
	off_t base, wr;
	char *buf, *rd;
	int i;

	pollute_eof_tail(_metadata, self);
	base = self->eof & ~(self->page - 1);
	wr = base + 3 * self->page / 4;		/* starts in the EOF page */

	buf = malloc(2 * self->page);
	ASSERT_NE(NULL, buf);
	memset(buf, 'B', 2 * self->page);
	ASSERT_EQ(2 * self->page, pwrite(self->fd, buf, 2 * self->page, wr));
	free(buf);

	rd = malloc(self->page);
	ASSERT_NE(NULL, rd);
	ASSERT_EQ(self->page, pread(self->fd, rd, self->page, base));
	/* [eof, wr) is hole -> zero; [wr, page) is written data -> 'B'. */
	for (i = self->eof & (self->page - 1); i < wr - base; i++)
		ASSERT_EQ(0, rd[i]);
	for (i = wr - base; i < self->page; i++)
		ASSERT_EQ('B', rd[i]);
	free(rd);
}

TEST_HARNESS_MAIN
