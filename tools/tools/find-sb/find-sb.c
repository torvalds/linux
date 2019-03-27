/*
 * This program, created 2002-10-03 by Garrett A. Wollman
 * <wollman@FreeBSD.org>, is in the public domain.  Use at your own risk.
 *
 * $FreeBSD$
 */

#ifdef __FreeBSD__
#include <sys/param.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>
#else
#include "mini_ufs.h"
#endif

#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static union {
	char buf[SBLOCKSIZE];
	struct fs sblock;
} u;

int
main(int argc, char **argv)
{
	off_t end, last1, last2;
	size_t len;
	ssize_t justread;
	int fd;
	char *ch;
	char c;
	intmax_t offset;

	offset = 0;
	while ((c = getopt(argc, argv, "o:")) != -1) {
		switch (c) {
		case 'o':
			if (optarg[0] == '\0')
				errx(1, "usage");
			offset = strtoimax(optarg, &ch, 10);
			if (*ch != '\0' || offset < 0)
				errx(1, "usage");
			offset -= offset % DEV_BSIZE;
			break;

		default:
			errx(1, "usage");
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		errx(1, "usage");

	fd = open(argv[0], O_RDONLY, 0);
	if (fd < 0)
		err(1, "%s", argv[0]);

	if (offset != 0) {
		end = lseek(fd, offset, SEEK_SET);
		if (end == -1)
			err(1, "%s", argv[0]);
	} else {
		end = 0;
	}
	len = 0;
	last1 = last2 = -1;

	while (1) {
		justread = read(fd, &u.buf[len], DEV_BSIZE);
		if (justread != DEV_BSIZE) {
			if (justread == 0) {
				printf("reached end-of-file at %jd\n",
				       (intmax_t)end);
				exit (0);
			}
			if (justread < 0)
				err(1, "read");
			errx(1, "short read %jd (wanted %d) at %jd",
			     (intmax_t)justread, DEV_BSIZE, (intmax_t)end);
		}
		len += DEV_BSIZE;
		end += DEV_BSIZE;
		if (len >= sizeof(struct fs)) {
			offset = end - len;

			if (u.sblock.fs_magic == FS_UFS1_MAGIC) {
				intmax_t fsbegin = offset - SBLOCK_UFS1;
				printf("Found UFS1 superblock at offset %jd, "
				       "block %jd\n", offset,
				       offset / DEV_BSIZE);
				printf("Filesystem might begin at offset %jd, "
				       "block %jd\n", fsbegin,
				       fsbegin / DEV_BSIZE);
				if (last1 >= 0) {
					printf("%jd blocks from last guess\n",
					       fsbegin / DEV_BSIZE - last1);
				}
				last1 = fsbegin / DEV_BSIZE;
				len -= DEV_BSIZE;
				memmove(u.buf, &u.buf[DEV_BSIZE], len);
			} else if (u.sblock.fs_magic == FS_UFS2_MAGIC) {
				intmax_t fsbegin = offset - SBLOCK_UFS2;
				printf("Found UFS2 superblock at offset %jd, "
				       "block %jd\n", offset,
				       offset / DEV_BSIZE);
				printf("Filesystem might begin at offset %jd, "
				       "block %jd\n", fsbegin,
				       fsbegin / DEV_BSIZE);
				if (last2 >= 0) {
					printf("%jd blocks from last guess\n",
					       fsbegin / DEV_BSIZE - last2);
				}
				last2 = fsbegin / DEV_BSIZE;
				len -= DEV_BSIZE;
				memmove(u.buf, &u.buf[DEV_BSIZE], len);
			}
		}
		if (len >= SBLOCKSIZE) {
			memmove(u.buf, &u.buf[DEV_BSIZE], 
				SBLOCKSIZE - DEV_BSIZE);
			len -= DEV_BSIZE;
		}
	}
}
