// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init_syscalls.h>
#include <linux/stringify.h>
#include <linux/timekeeping.h>
#include "initramfs_internal.h"

struct initramfs_test_cpio {
	char *magic;
	unsigned int ino;
	unsigned int mode;
	unsigned int uid;
	unsigned int gid;
	unsigned int nlink;
	unsigned int mtime;
	unsigned int filesize;
	unsigned int devmajor;
	unsigned int devminor;
	unsigned int rdevmajor;
	unsigned int rdevminor;
	unsigned int namesize;
	unsigned int csum;
	char *fname;
	char *data;
};

static size_t fill_cpio(struct initramfs_test_cpio *cs, size_t csz, char *out)
{
	int i;
	size_t off = 0;

	for (i = 0; i < csz; i++) {
		char *pos = &out[off];
		struct initramfs_test_cpio *c = &cs[i];
		size_t thislen;

		/* +1 to account for nulterm */
		thislen = sprintf(pos, "%s"
			"%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x"
			"%s",
			c->magic, c->ino, c->mode, c->uid, c->gid, c->nlink,
			c->mtime, c->filesize, c->devmajor, c->devminor,
			c->rdevmajor, c->rdevminor, c->namesize, c->csum,
			c->fname) + 1;
		pr_debug("packing (%zu): %.*s\n", thislen, (int)thislen, pos);
		off += thislen;
		while (off & 3)
			out[off++] = '\0';

		memcpy(&out[off], c->data, c->filesize);
		off += c->filesize;
		while (off & 3)
			out[off++] = '\0';
	}

	return off;
}

static void __init initramfs_test_extract(struct kunit *test)
{
	char *err, *cpio_srcbuf;
	size_t len;
	struct timespec64 ts_before, ts_after;
	struct kstat st = {};
	struct initramfs_test_cpio c[] = { {
		.magic = "070701",
		.ino = 1,
		.mode = S_IFREG | 0777,
		.uid = 12,
		.gid = 34,
		.nlink = 1,
		.mtime = 56,
		.filesize = 0,
		.devmajor = 0,
		.devminor = 1,
		.rdevmajor = 0,
		.rdevminor = 0,
		.namesize = sizeof("initramfs_test_extract"),
		.csum = 0,
		.fname = "initramfs_test_extract",
	}, {
		.magic = "070701",
		.ino = 2,
		.mode = S_IFDIR | 0777,
		.nlink = 1,
		.mtime = 57,
		.devminor = 1,
		.namesize = sizeof("initramfs_test_extract_dir"),
		.fname = "initramfs_test_extract_dir",
	}, {
		.magic = "070701",
		.namesize = sizeof("TRAILER!!!"),
		.fname = "TRAILER!!!",
	} };

	/* +3 to cater for any 4-byte end-alignment */
	cpio_srcbuf = kzalloc(ARRAY_SIZE(c) * (CPIO_HDRLEN + PATH_MAX + 3),
			      GFP_KERNEL);
	len = fill_cpio(c, ARRAY_SIZE(c), cpio_srcbuf);

	ktime_get_real_ts64(&ts_before);
	err = unpack_to_rootfs(cpio_srcbuf, len);
	ktime_get_real_ts64(&ts_after);
	if (err) {
		KUNIT_FAIL(test, "unpack failed %s", err);
		goto out;
	}

	KUNIT_EXPECT_EQ(test, init_stat(c[0].fname, &st, 0), 0);
	KUNIT_EXPECT_TRUE(test, S_ISREG(st.mode));
	KUNIT_EXPECT_TRUE(test, uid_eq(st.uid, KUIDT_INIT(c[0].uid)));
	KUNIT_EXPECT_TRUE(test, gid_eq(st.gid, KGIDT_INIT(c[0].gid)));
	KUNIT_EXPECT_EQ(test, st.nlink, 1);
	if (IS_ENABLED(CONFIG_INITRAMFS_PRESERVE_MTIME)) {
		KUNIT_EXPECT_EQ(test, st.mtime.tv_sec, c[0].mtime);
	} else {
		KUNIT_EXPECT_GE(test, st.mtime.tv_sec, ts_before.tv_sec);
		KUNIT_EXPECT_LE(test, st.mtime.tv_sec, ts_after.tv_sec);
	}
	KUNIT_EXPECT_EQ(test, st.blocks, c[0].filesize);

	KUNIT_EXPECT_EQ(test, init_stat(c[1].fname, &st, 0), 0);
	KUNIT_EXPECT_TRUE(test, S_ISDIR(st.mode));
	if (IS_ENABLED(CONFIG_INITRAMFS_PRESERVE_MTIME)) {
		KUNIT_EXPECT_EQ(test, st.mtime.tv_sec, c[1].mtime);
	} else {
		KUNIT_EXPECT_GE(test, st.mtime.tv_sec, ts_before.tv_sec);
		KUNIT_EXPECT_LE(test, st.mtime.tv_sec, ts_after.tv_sec);
	}

	KUNIT_EXPECT_EQ(test, init_unlink(c[0].fname), 0);
	KUNIT_EXPECT_EQ(test, init_rmdir(c[1].fname), 0);
out:
	kfree(cpio_srcbuf);
}

/*
 * Don't terminate filename. Previously, the cpio filename field was passed
 * directly to filp_open(collected, O_CREAT|..) without nulterm checks. See
 * https://lore.kernel.org/linux-fsdevel/20241030035509.20194-2-ddiss@suse.de
 */
static void __init initramfs_test_fname_overrun(struct kunit *test)
{
	char *err, *cpio_srcbuf;
	size_t len, suffix_off;
	struct initramfs_test_cpio c[] = { {
		.magic = "070701",
		.ino = 1,
		.mode = S_IFREG | 0777,
		.uid = 0,
		.gid = 0,
		.nlink = 1,
		.mtime = 1,
		.filesize = 0,
		.devmajor = 0,
		.devminor = 1,
		.rdevmajor = 0,
		.rdevminor = 0,
		.namesize = sizeof("initramfs_test_fname_overrun"),
		.csum = 0,
		.fname = "initramfs_test_fname_overrun",
	} };

	/*
	 * poison cpio source buffer, so we can detect overrun. source
	 * buffer is used by read_into() when hdr or fname
	 * are already available (e.g. no compression).
	 */
	cpio_srcbuf = kmalloc(CPIO_HDRLEN + PATH_MAX + 3, GFP_KERNEL);
	memset(cpio_srcbuf, 'B', CPIO_HDRLEN + PATH_MAX + 3);
	/* limit overrun to avoid crashes / filp_open() ENAMETOOLONG */
	cpio_srcbuf[CPIO_HDRLEN + strlen(c[0].fname) + 20] = '\0';

	len = fill_cpio(c, ARRAY_SIZE(c), cpio_srcbuf);
	/* overwrite trailing fname terminator and padding */
	suffix_off = len - 1;
	while (cpio_srcbuf[suffix_off] == '\0') {
		cpio_srcbuf[suffix_off] = 'P';
		suffix_off--;
	}

	err = unpack_to_rootfs(cpio_srcbuf, len);
	KUNIT_EXPECT_NOT_NULL(test, err);

	kfree(cpio_srcbuf);
}

static void __init initramfs_test_data(struct kunit *test)
{
	char *err, *cpio_srcbuf;
	size_t len;
	struct file *file;
	struct initramfs_test_cpio c[] = { {
		.magic = "070701",
		.ino = 1,
		.mode = S_IFREG | 0777,
		.uid = 0,
		.gid = 0,
		.nlink = 1,
		.mtime = 1,
		.filesize = sizeof("ASDF") - 1,
		.devmajor = 0,
		.devminor = 1,
		.rdevmajor = 0,
		.rdevminor = 0,
		.namesize = sizeof("initramfs_test_data"),
		.csum = 0,
		.fname = "initramfs_test_data",
		.data = "ASDF",
	} };

	/* +6 for max name and data 4-byte padding */
	cpio_srcbuf = kmalloc(CPIO_HDRLEN + c[0].namesize + c[0].filesize + 6,
			      GFP_KERNEL);

	len = fill_cpio(c, ARRAY_SIZE(c), cpio_srcbuf);

	err = unpack_to_rootfs(cpio_srcbuf, len);
	KUNIT_EXPECT_NULL(test, err);

	file = filp_open(c[0].fname, O_RDONLY, 0);
	if (IS_ERR(file)) {
		KUNIT_FAIL(test, "open failed");
		goto out;
	}

	/* read back file contents into @cpio_srcbuf and confirm match */
	len = kernel_read(file, cpio_srcbuf, c[0].filesize, NULL);
	KUNIT_EXPECT_EQ(test, len, c[0].filesize);
	KUNIT_EXPECT_MEMEQ(test, cpio_srcbuf, c[0].data, len);

	fput(file);
	KUNIT_EXPECT_EQ(test, init_unlink(c[0].fname), 0);
out:
	kfree(cpio_srcbuf);
}

static void __init initramfs_test_csum(struct kunit *test)
{
	char *err, *cpio_srcbuf;
	size_t len;
	struct initramfs_test_cpio c[] = { {
		/* 070702 magic indicates a valid csum is present */
		.magic = "070702",
		.ino = 1,
		.mode = S_IFREG | 0777,
		.nlink = 1,
		.filesize = sizeof("ASDF") - 1,
		.devminor = 1,
		.namesize = sizeof("initramfs_test_csum"),
		.csum = 'A' + 'S' + 'D' + 'F',
		.fname = "initramfs_test_csum",
		.data = "ASDF",
	}, {
		/* mix csum entry above with no-csum entry below */
		.magic = "070701",
		.ino = 2,
		.mode = S_IFREG | 0777,
		.nlink = 1,
		.filesize = sizeof("ASDF") - 1,
		.devminor = 1,
		.namesize = sizeof("initramfs_test_csum_not_here"),
		/* csum ignored */
		.csum = 5555,
		.fname = "initramfs_test_csum_not_here",
		.data = "ASDF",
	} };

	cpio_srcbuf = kmalloc(8192, GFP_KERNEL);

	len = fill_cpio(c, ARRAY_SIZE(c), cpio_srcbuf);

	err = unpack_to_rootfs(cpio_srcbuf, len);
	KUNIT_EXPECT_NULL(test, err);

	KUNIT_EXPECT_EQ(test, init_unlink(c[0].fname), 0);
	KUNIT_EXPECT_EQ(test, init_unlink(c[1].fname), 0);

	/* mess up the csum and confirm that unpack fails */
	c[0].csum--;
	len = fill_cpio(c, ARRAY_SIZE(c), cpio_srcbuf);

	err = unpack_to_rootfs(cpio_srcbuf, len);
	KUNIT_EXPECT_NOT_NULL(test, err);

	/*
	 * file (with content) is still retained in case of bad-csum abort.
	 * Perhaps we should change this.
	 */
	KUNIT_EXPECT_EQ(test, init_unlink(c[0].fname), 0);
	KUNIT_EXPECT_EQ(test, init_unlink(c[1].fname), -ENOENT);
	kfree(cpio_srcbuf);
}

/*
 * hardlink hashtable may leak when the archive omits a trailer:
 * https://lore.kernel.org/r/20241107002044.16477-10-ddiss@suse.de/
 */
static void __init initramfs_test_hardlink(struct kunit *test)
{
	char *err, *cpio_srcbuf;
	size_t len;
	struct kstat st0, st1;
	struct initramfs_test_cpio c[] = { {
		.magic = "070701",
		.ino = 1,
		.mode = S_IFREG | 0777,
		.nlink = 2,
		.devminor = 1,
		.namesize = sizeof("initramfs_test_hardlink"),
		.fname = "initramfs_test_hardlink",
	}, {
		/* hardlink data is present in last archive entry */
		.magic = "070701",
		.ino = 1,
		.mode = S_IFREG | 0777,
		.nlink = 2,
		.filesize = sizeof("ASDF") - 1,
		.devminor = 1,
		.namesize = sizeof("initramfs_test_hardlink_link"),
		.fname = "initramfs_test_hardlink_link",
		.data = "ASDF",
	} };

	cpio_srcbuf = kmalloc(8192, GFP_KERNEL);

	len = fill_cpio(c, ARRAY_SIZE(c), cpio_srcbuf);

	err = unpack_to_rootfs(cpio_srcbuf, len);
	KUNIT_EXPECT_NULL(test, err);

	KUNIT_EXPECT_EQ(test, init_stat(c[0].fname, &st0, 0), 0);
	KUNIT_EXPECT_EQ(test, init_stat(c[1].fname, &st1, 0), 0);
	KUNIT_EXPECT_EQ(test, st0.ino, st1.ino);
	KUNIT_EXPECT_EQ(test, st0.nlink, 2);
	KUNIT_EXPECT_EQ(test, st1.nlink, 2);

	KUNIT_EXPECT_EQ(test, init_unlink(c[0].fname), 0);
	KUNIT_EXPECT_EQ(test, init_unlink(c[1].fname), 0);

	kfree(cpio_srcbuf);
}

#define INITRAMFS_TEST_MANY_LIMIT 1000
#define INITRAMFS_TEST_MANY_PATH_MAX (sizeof("initramfs_test_many-") \
			+ sizeof(__stringify(INITRAMFS_TEST_MANY_LIMIT)))
static void __init initramfs_test_many(struct kunit *test)
{
	char *err, *cpio_srcbuf, *p;
	size_t len = INITRAMFS_TEST_MANY_LIMIT *
		     (CPIO_HDRLEN + INITRAMFS_TEST_MANY_PATH_MAX + 3);
	char thispath[INITRAMFS_TEST_MANY_PATH_MAX];
	int i;

	p = cpio_srcbuf = kmalloc(len, GFP_KERNEL);

	for (i = 0; i < INITRAMFS_TEST_MANY_LIMIT; i++) {
		struct initramfs_test_cpio c = {
			.magic = "070701",
			.ino = i,
			.mode = S_IFREG | 0777,
			.nlink = 1,
			.devminor = 1,
			.fname = thispath,
		};

		c.namesize = 1 + sprintf(thispath, "initramfs_test_many-%d", i);
		p += fill_cpio(&c, 1, p);
	}

	len = p - cpio_srcbuf;
	err = unpack_to_rootfs(cpio_srcbuf, len);
	KUNIT_EXPECT_NULL(test, err);

	for (i = 0; i < INITRAMFS_TEST_MANY_LIMIT; i++) {
		sprintf(thispath, "initramfs_test_many-%d", i);
		KUNIT_EXPECT_EQ(test, init_unlink(thispath), 0);
	}

	kfree(cpio_srcbuf);
}

/*
 * The kunit_case/_suite struct cannot be marked as __initdata as this will be
 * used in debugfs to retrieve results after test has run.
 */
static struct kunit_case __refdata initramfs_test_cases[] = {
	KUNIT_CASE(initramfs_test_extract),
	KUNIT_CASE(initramfs_test_fname_overrun),
	KUNIT_CASE(initramfs_test_data),
	KUNIT_CASE(initramfs_test_csum),
	KUNIT_CASE(initramfs_test_hardlink),
	KUNIT_CASE(initramfs_test_many),
	{},
};

static struct kunit_suite initramfs_test_suite = {
	.name = "initramfs",
	.test_cases = initramfs_test_cases,
};
kunit_test_init_section_suites(&initramfs_test_suite);

MODULE_DESCRIPTION("Initramfs KUnit test suite");
MODULE_LICENSE("GPL v2");
