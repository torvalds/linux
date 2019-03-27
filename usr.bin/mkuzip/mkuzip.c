/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2016 Maxim Sobolev <sobomax@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/endian.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mkuzip.h"
#include "mkuz_cloop.h"
#include "mkuz_blockcache.h"
#include "mkuz_zlib.h"
#include "mkuz_lzma.h"
#include "mkuz_blk.h"
#include "mkuz_cfg.h"
#include "mkuz_conveyor.h"
#include "mkuz_format.h"
#include "mkuz_fqueue.h"
#include "mkuz_time.h"
#include "mkuz_insize.h"

#define DEFAULT_CLSTSIZE	16384

static struct mkuz_format uzip_fmt = {
	.magic = CLOOP_MAGIC_ZLIB,
	.default_sufx = DEFAULT_SUFX_ZLIB,
	.f_init = &mkuz_zlib_init,
	.f_compress = &mkuz_zlib_compress
};

static struct mkuz_format ulzma_fmt = {
        .magic = CLOOP_MAGIC_LZMA,
        .default_sufx = DEFAULT_SUFX_LZMA,
        .f_init = &mkuz_lzma_init,
        .f_compress = &mkuz_lzma_compress
};

static struct mkuz_blk *readblock(int, u_int32_t);
static void usage(void);
static void cleanup(void);

static char *cleanfile = NULL;

static int
cmp_blkno(const struct mkuz_blk *bp, void *p)
{
	uint32_t *ap;

	ap = (uint32_t *)p;

	return (bp->info.blkno == *ap);
}

int main(int argc, char **argv)
{
	struct mkuz_cfg cfs;
	char *oname;
	uint64_t *toc;
	int i, io, opt, tmp;
	struct {
		int en;
		FILE *f;
	} summary;
	struct iovec iov[2];
	uint64_t offset, last_offset;
	struct cloop_header hdr;
	struct mkuz_conveyor *cvp;
        void *c_ctx;
	struct mkuz_blk_info *chit;
	size_t ncpusz, ncpu, magiclen;
	double st, et;

	st = getdtime();

	ncpusz = sizeof(size_t);
	if (sysctlbyname("hw.ncpu", &ncpu, &ncpusz, NULL, 0) < 0) {
		ncpu = 1;
	} else if (ncpu > MAX_WORKERS_AUTO) {
		ncpu = MAX_WORKERS_AUTO;
	}

	memset(&hdr, 0, sizeof(hdr));
	cfs.blksz = DEFAULT_CLSTSIZE;
	oname = NULL;
	cfs.verbose = 0;
	cfs.no_zcomp = 0;
	cfs.en_dedup = 0;
	summary.en = 0;
	summary.f = stderr;
	cfs.handler = &uzip_fmt;
	cfs.nworkers = ncpu;
	struct mkuz_blk *iblk, *oblk;

	while((opt = getopt(argc, argv, "o:s:vZdLSj:")) != -1) {
		switch(opt) {
		case 'o':
			oname = optarg;
			break;

		case 's':
			tmp = atoi(optarg);
			if (tmp <= 0) {
				errx(1, "invalid cluster size specified: %s",
				    optarg);
				/* Not reached */
			}
			cfs.blksz = tmp;
			break;

		case 'v':
			cfs.verbose = 1;
			break;

		case 'Z':
			cfs.no_zcomp = 1;
			break;

		case 'd':
			cfs.en_dedup = 1;
			break;

		case 'L':
			cfs.handler = &ulzma_fmt;
			break;

		case 'S':
			summary.en = 1;
			summary.f = stdout;
			break;

		case 'j':
			tmp = atoi(optarg);
			if (tmp <= 0) {
				errx(1, "invalid number of compression threads"
                                    " specified: %s", optarg);
				/* Not reached */
			}
			cfs.nworkers = tmp;
			break;

		default:
			usage();
			/* Not reached */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1) {
		usage();
		/* Not reached */
	}

	magiclen = strlcpy(hdr.magic, cfs.handler->magic, sizeof(hdr.magic));
	assert(magiclen < sizeof(hdr.magic));

	if (cfs.en_dedup != 0) {
		hdr.magic[CLOOP_OFS_VERSN] = CLOOP_MAJVER_3;
		hdr.magic[CLOOP_OFS_COMPR] =
		    tolower(hdr.magic[CLOOP_OFS_COMPR]);
	}

	c_ctx = cfs.handler->f_init(cfs.blksz);

	cfs.iname = argv[0];
	if (oname == NULL) {
		asprintf(&oname, "%s%s", cfs.iname, cfs.handler->default_sufx);
		if (oname == NULL) {
			err(1, "can't allocate memory");
			/* Not reached */
		}
	}

	signal(SIGHUP, exit);
	signal(SIGINT, exit);
	signal(SIGTERM, exit);
	signal(SIGXCPU, exit);
	signal(SIGXFSZ, exit);
	atexit(cleanup);

	cfs.fdr = open(cfs.iname, O_RDONLY);
	if (cfs.fdr < 0) {
		err(1, "open(%s)", cfs.iname);
		/* Not reached */
	}
	cfs.isize = mkuz_get_insize(&cfs);
	if (cfs.isize < 0) {
		errx(1, "can't determine input image size");
		/* Not reached */
	}
	hdr.nblocks = cfs.isize / cfs.blksz;
	if ((cfs.isize % cfs.blksz) != 0) {
		if (cfs.verbose != 0)
			fprintf(stderr, "file size is not multiple "
			"of %d, padding data\n", cfs.blksz);
		hdr.nblocks++;
	}
	toc = mkuz_safe_malloc((hdr.nblocks + 1) * sizeof(*toc));

	cfs.fdw = open(oname, (cfs.en_dedup ? O_RDWR : O_WRONLY) | O_TRUNC | O_CREAT,
		   S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (cfs.fdw < 0) {
		err(1, "open(%s)", oname);
		/* Not reached */
	}
	cleanfile = oname;

	/* Prepare header that we will write later when we have index ready. */
	iov[0].iov_base = (char *)&hdr;
	iov[0].iov_len = sizeof(hdr);
	iov[1].iov_base = (char *)toc;
	iov[1].iov_len = (hdr.nblocks + 1) * sizeof(*toc);
	offset = iov[0].iov_len + iov[1].iov_len;

	/* Reserve space for header */
	lseek(cfs.fdw, offset, SEEK_SET);

	if (cfs.verbose != 0) {
		fprintf(stderr, "data size %ju bytes, number of clusters "
		    "%u, index length %zu bytes\n", cfs.isize,
		    hdr.nblocks, iov[1].iov_len);
	}

	cvp = mkuz_conveyor_ctor(&cfs);

	last_offset = 0;
        iblk = oblk = NULL;
	for(i = io = 0; iblk != MKUZ_BLK_EOF; i++) {
		iblk = readblock(cfs.fdr, cfs.blksz);
		mkuz_fqueue_enq(cvp->wrk_queue, iblk);
		if (iblk != MKUZ_BLK_EOF &&
		    (i < (cfs.nworkers * ITEMS_PER_WORKER))) {
			continue;
		}
drain:
		oblk = mkuz_fqueue_deq_when(cvp->results, cmp_blkno, &io);
		assert(oblk->info.blkno == (unsigned)io);
		oblk->info.offset = offset;
		chit = NULL;
		if (cfs.en_dedup != 0 && oblk->info.len > 0) {
			chit = mkuz_blkcache_regblock(cfs.fdw, oblk);
			/*
			 * There should be at least one non-empty block
			 * between us and the backref'ed offset, otherwise
			 * we won't be able to parse that sequence correctly
			 * as it would be indistinguishible from another
			 * empty block.
			 */
			if (chit != NULL && chit->offset == last_offset) {
				chit = NULL;
			}
		}
		if (chit != NULL) {
			toc[io] = htobe64(chit->offset);
			oblk->info.len = 0;
		} else {
			if (oblk->info.len > 0 && write(cfs.fdw, oblk->data,
			    oblk->info.len) < 0) {
				err(1, "write(%s)", oname);
				/* Not reached */
			}
			toc[io] = htobe64(offset);
			last_offset = offset;
			offset += oblk->info.len;
		}
		if (cfs.verbose != 0) {
			fprintf(stderr, "cluster #%d, in %u bytes, "
			    "out len=%lu offset=%lu", io, cfs.blksz,
			    (u_long)oblk->info.len, (u_long)be64toh(toc[io]));
			if (chit != NULL) {
				fprintf(stderr, " (backref'ed to #%d)",
				    chit->blkno);
			}
			fprintf(stderr, "\n");
		}
		free(oblk);
		io += 1;
		if (iblk == MKUZ_BLK_EOF) {
			if (io < i)
				goto drain;
			/* Last block, see if we need to add some padding */
			if ((offset % DEV_BSIZE) == 0)
				continue;
			oblk = mkuz_blk_ctor(DEV_BSIZE - (offset % DEV_BSIZE));
			oblk->info.blkno = io;
			oblk->info.len = oblk->alen;
			if (cfs.verbose != 0) {
				fprintf(stderr, "padding data with %lu bytes "
				    "so that file size is multiple of %d\n",
				    (u_long)oblk->alen, DEV_BSIZE);
			}
			mkuz_fqueue_enq(cvp->results, oblk);
			goto drain;
		}
	}

	close(cfs.fdr);

	if (cfs.verbose != 0 || summary.en != 0) {
		et = getdtime();
		fprintf(summary.f, "compressed data to %ju bytes, saved %lld "
		    "bytes, %.2f%% decrease, %.2f bytes/sec.\n", offset,
		    (long long)(cfs.isize - offset),
		    100.0 * (long long)(cfs.isize - offset) /
		    (float)cfs.isize, (float)cfs.isize / (et - st));
	}

	/* Convert to big endian */
	hdr.blksz = htonl(cfs.blksz);
	hdr.nblocks = htonl(hdr.nblocks);
	/* Write headers into pre-allocated space */
	lseek(cfs.fdw, 0, SEEK_SET);
	if (writev(cfs.fdw, iov, 2) < 0) {
		err(1, "writev(%s)", oname);
		/* Not reached */
	}
	cleanfile = NULL;
	close(cfs.fdw);

	exit(0);
}

static struct mkuz_blk *
readblock(int fd, u_int32_t clstsize)
{
	int numread;
	struct mkuz_blk *rval;
	static int blockcnt;
	off_t cpos;

	rval = mkuz_blk_ctor(clstsize);

	rval->info.blkno = blockcnt;
	blockcnt += 1;
	cpos = lseek(fd, 0, SEEK_CUR);
	if (cpos < 0) {
		err(1, "readblock: lseek() failed");
		/* Not reached */
	}
	rval->info.offset = cpos;

	numread = read(fd, rval->data, clstsize);
	if (numread < 0) {
		err(1, "readblock: read() failed");
		/* Not reached */
	}
	if (numread == 0) {
		free(rval);
		return MKUZ_BLK_EOF;
	}
	rval->info.len = numread;
	return rval;
}

static void
usage(void)
{

	fprintf(stderr, "usage: mkuzip [-vZdLS] [-o outfile] [-s cluster_size] "
	    "[-j ncompr] infile\n");
	exit(1);
}

void *
mkuz_safe_malloc(size_t size)
{
	void *retval;

	retval = malloc(size);
	if (retval == NULL) {
		err(1, "can't allocate memory");
		/* Not reached */
	}
	return retval;
}

void *
mkuz_safe_zmalloc(size_t size)
{
	void *retval;

	retval = mkuz_safe_malloc(size);
	bzero(retval, size);
	return retval;
}

static void
cleanup(void)
{

	if (cleanfile != NULL)
		unlink(cleanfile);
}

int
mkuz_memvcmp(const void *memory, unsigned char val, size_t size)
{
    const u_char *mm;

    mm = (const u_char *)memory;
    return (*mm == val) && memcmp(mm, mm + 1, size - 1) == 0;
}
