// SPDX-License-Identifier: GPL-2.0
/*
 * Exercise the RDS getsockopt() paths that were converted to the
 * getsockopt_iter() / sockopt_t callback.
 *
 * Three distinct paths are covered:
 *
 *   - RDS_RECVERR and SO_RDS_TRANSPORT, which now return their int value
 *     through copy_to_iter() and report the written length in opt->optlen.
 *
 *   - RDS_INFO_*, which pins the userspace buffer with
 *     iov_iter_extract_pages() (including a non-zero starting page offset)
 *     and lets the info producers memcpy the snapshot in under a spinlock.
 *
 * The kvec (in-kernel buffer) -> -EOPNOTSUPP path of rds_info_getsockopt()
 * is not reachable from a userspace getsockopt() and so is not tested here.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/rds.h>

#include "../../kselftest_harness.h"

#ifndef AF_RDS
#define AF_RDS 21
#endif

FIXTURE(rds) {
	int fd;
};

FIXTURE_SETUP(rds)
{
	self->fd = socket(AF_RDS, SOCK_SEQPACKET, 0);
	if (self->fd < 0)
		SKIP(return, "AF_RDS unavailable (errno %d) - load the rds module",
		     errno);
}

FIXTURE_TEARDOWN(rds)
{
	if (self->fd >= 0)
		close(self->fd);
}

/* RDS_RECVERR defaults to 0 and is reported back as a 4-byte int. */
TEST_F(rds, recverr_default)
{
	socklen_t len = sizeof(int);
	int val = 0xdeadbeef;

	ASSERT_EQ(0, getsockopt(self->fd, SOL_RDS, RDS_RECVERR, &val, &len));
	EXPECT_EQ(sizeof(int), len);
	EXPECT_EQ(0, val);
}

/* A value set via setsockopt() must be readable back unchanged. */
TEST_F(rds, recverr_set_get)
{
	socklen_t len = sizeof(int);
	int val = 1;

	ASSERT_EQ(0, setsockopt(self->fd, SOL_RDS, RDS_RECVERR, &val, len));

	val = 0;
	ASSERT_EQ(0, getsockopt(self->fd, SOL_RDS, RDS_RECVERR, &val, &len));
	EXPECT_EQ(sizeof(int), len);
	EXPECT_EQ(1, val);
}

/* A buffer smaller than an int is rejected with EINVAL, not silently. */
TEST_F(rds, recverr_short_buffer)
{
	socklen_t len = sizeof(int) - 1;
	char buf[sizeof(int)];

	EXPECT_EQ(-1, getsockopt(self->fd, SOL_RDS, RDS_RECVERR, buf, &len));
	EXPECT_EQ(EINVAL, errno);
}

/* An unbound socket reports RDS_TRANS_NONE for SO_RDS_TRANSPORT. */
TEST_F(rds, transport_unbound)
{
	socklen_t len = sizeof(int);
	int val = 0;

	ASSERT_EQ(0, getsockopt(self->fd, SOL_RDS, SO_RDS_TRANSPORT, &val,
				&len));
	EXPECT_EQ(sizeof(int), len);
	EXPECT_EQ(RDS_TRANS_NONE, (unsigned int)val);
}

TEST_F(rds, transport_short_buffer)
{
	socklen_t len = sizeof(int) - 1;
	char buf[sizeof(int)];

	EXPECT_EQ(-1, getsockopt(self->fd, SOL_RDS, SO_RDS_TRANSPORT, buf,
				 &len));
	EXPECT_EQ(EINVAL, errno);
}

/*
 * RDS_INFO_COUNTERS with a zero-length buffer is the "probe" call: it must
 * fail with ENOSPC and report the required snapshot size in optlen.
 */
TEST_F(rds, info_counters_probe)
{
	socklen_t len = 0;

	EXPECT_EQ(-1, getsockopt(self->fd, SOL_RDS, RDS_INFO_COUNTERS, NULL,
				 &len));
	EXPECT_EQ(ENOSPC, errno);
	EXPECT_GT(len, 0);
	/* The snapshot is an array of fixed-size counter records. */
	EXPECT_EQ(0, len % (socklen_t)sizeof(struct rds_info_counter));
}

/*
 * A real snapshot into an unaligned userspace buffer exercises the
 * iov_iter_extract_pages() path, including the non-zero offset0 handling
 * that the patch reworked. Place the buffer at a non-page-aligned address
 * spanning into the next page to make sure multi-page pinning works too.
 */
TEST_F(rds, info_counters_snapshot)
{
	struct rds_info_counter *ctr;
	socklen_t need = 0, len;
	long pagesz = sysconf(_SC_PAGESIZE);
	size_t offset, map_len;
	unsigned int i, n;
	char *region, *buf;
	int ret;

	/* Probe for the required size. */
	getsockopt(self->fd, SOL_RDS, RDS_INFO_COUNTERS, NULL, &need);
	ASSERT_GT(need, 0);

	/*
	 * Place the buffer at a non-page-aligned offset that runs past the
	 * first page boundary, and size the mapping from the probed length so
	 * the test keeps working if the counter set grows.
	 */
	offset = pagesz - 64;
	map_len = ((offset + need + pagesz - 1) / pagesz) * pagesz;

	region = mmap(NULL, map_len, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(MAP_FAILED, region);

	buf = region + offset;

	/*
	 * On success the RDS_INFO path returns the positive per-element size
	 * (lens.each) rather than 0, and writes the full snapshot length back
	 * into optlen.
	 */
	len = need;
	ret = getsockopt(self->fd, SOL_RDS, RDS_INFO_COUNTERS, buf, &len);
	ASSERT_GE(ret, 0) {
		TH_LOG("getsockopt snapshot failed: errno %d", errno);
	}
	EXPECT_EQ(sizeof(struct rds_info_counter), ret);
	EXPECT_EQ(need, len);

	/* The counter names must be NUL-terminated, non-empty strings. */
	ctr = (struct rds_info_counter *)buf;
	n = len / sizeof(*ctr);
	ASSERT_GT(n, 0);
	for (i = 0; i < n; i++) {
		size_t namelen = strnlen((char *)ctr[i].name,
					 sizeof(ctr[i].name));

		EXPECT_GT(namelen, 0);
		EXPECT_LT(namelen, sizeof(ctr[i].name));
	}

	munmap(region, map_len);
}

/*
 * A non-zero but too-small buffer must report ENOSPC and the full required
 * length, without corrupting memory past the buffer.
 */
TEST_F(rds, info_counters_short_buffer)
{
	socklen_t need = 0, len;
	char small[sizeof(struct rds_info_counter)];

	getsockopt(self->fd, SOL_RDS, RDS_INFO_COUNTERS, NULL, &need);
	ASSERT_GT(need, 0);

	/* Ask with a buffer guaranteed smaller than the full snapshot. */
	if (need <= (socklen_t)sizeof(small))
		SKIP(return, "snapshot fits in one record; nothing to test");

	len = 1; /* < sizeof(struct rds_info_counter) */
	EXPECT_EQ(-1, getsockopt(self->fd, SOL_RDS, RDS_INFO_COUNTERS, small,
				 &len));
	EXPECT_EQ(ENOSPC, errno);
	EXPECT_EQ(need, len);
}

TEST_HARNESS_MAIN
