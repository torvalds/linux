// SPDX-License-Identifier: GPL-2.0-or-later

/* P9 gunzip sample code for demonstrating the P9 NX hardware
 * interface.  Not intended for productive uses or for performance or
 * compression ratio measurements.  Note also that /dev/crypto/gzip,
 * VAS and skiboot support are required
 *
 * Copyright 2020 IBM Corp.
 *
 * Author: Bulent Abali <abali@us.ibm.com>
 *
 * https://github.com/libnxz/power-gzip for zlib api and other utils
 * Definitions of acronyms used here.  See
 * P9 NX Gzip Accelerator User's Manual for details:
 * https://github.com/libnxz/power-gzip/blob/develop/doc/power_nx_gzip_um.pdf
 *
 * adler/crc: 32 bit checksums appended to stream tail
 * ce:       completion extension
 * cpb:      coprocessor parameter block (metadata)
 * crb:      coprocessor request block (command)
 * csb:      coprocessor status block (status)
 * dht:      dynamic huffman table
 * dde:      data descriptor element (address, length)
 * ddl:      list of ddes
 * dh/fh:    dynamic and fixed huffman types
 * fc:       coprocessor function code
 * histlen:  history/dictionary length
 * history:  sliding window of up to 32KB of data
 * lzcount:  Deflate LZ symbol counts
 * rembytecnt: remaining byte count
 * sfbt:     source final block type; last block's type during decomp
 * spbc:     source processed byte count
 * subc:     source unprocessed bit count
 * tebc:     target ending bit count; valid bits in the last byte
 * tpbc:     target processed byte count
 * vas:      virtual accelerator switch; the user mode interface
 */

#define _ISOC11_SOURCE	// For aligned_alloc()
#define _DEFAULT_SOURCE	// For endian.h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <endian.h>
#include <bits/endian.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include "nxu.h"
#include "nx.h"
#include "crb.h"

int nx_dbg;
FILE *nx_gzip_log;

#define NX_MIN(X, Y) (((X) < (Y))?(X):(Y))
#define NX_MAX(X, Y) (((X) > (Y))?(X):(Y))

#define GETINPC(X) fgetc(X)
#define FNAME_MAX 1024

/* fifo queue management */
#define fifo_used_bytes(used) (used)
#define fifo_free_bytes(used, len) ((len)-(used))
/* amount of free bytes in the first and last parts */
#define fifo_free_first_bytes(cur, used, len)  ((((cur)+(used)) <= (len)) \
						  ? (len)-((cur)+(used)) : 0)
#define fifo_free_last_bytes(cur, used, len)   ((((cur)+(used)) <= (len)) \
						  ? (cur) : (len)-(used))
/* amount of used bytes in the first and last parts */
#define fifo_used_first_bytes(cur, used, len)  ((((cur)+(used)) <= (len)) \
						  ? (used) : (len)-(cur))
#define fifo_used_last_bytes(cur, used, len)   ((((cur)+(used)) <= (len)) \
						  ? 0 : ((used)+(cur))-(len))
/* first and last free parts start here */
#define fifo_free_first_offset(cur, used)      ((cur)+(used))
#define fifo_free_last_offset(cur, used, len)  \
					   fifo_used_last_bytes(cur, used, len)
/* first and last used parts start here */
#define fifo_used_first_offset(cur)            (cur)
#define fifo_used_last_offset(cur)             (0)

const int fifo_in_len = 1<<24;
const int fifo_out_len = 1<<24;
const int page_sz = 1<<16;
const int line_sz = 1<<7;
const int window_max = 1<<15;

/*
 * Adds an (address, len) pair to the list of ddes (ddl) and updates
 * the base dde.  ddl[0] is the only dde in a direct dde which
 * contains a single (addr,len) pair.  For more pairs, ddl[0] becomes
 * the indirect (base) dde that points to a list of direct ddes.
 * See Section 6.4 of the NX-gzip user manual for DDE description.
 * Addr=NULL, len=0 clears the ddl[0].  Returns the total number of
 * bytes in ddl.  Caller is responsible for allocting the array of
 * nx_dde_t *ddl.  If N addresses are required in the scatter-gather
 * list, the ddl array must have N+1 entries minimum.
 */
static inline uint32_t nx_append_dde(struct nx_dde_t *ddl, void *addr,
					uint32_t len)
{
	uint32_t ddecnt;
	uint32_t bytes;

	if (addr == NULL && len == 0) {
		clearp_dde(ddl);
		return 0;
	}

	NXPRT(fprintf(stderr, "%d: %s addr %p len %x\n", __LINE__, addr,
			__func__, len));

	/* Number of ddes in the dde list ; == 0 when it is a direct dde */
	ddecnt = getpnn(ddl, dde_count);
	bytes = getp32(ddl, ddebc);

	if (ddecnt == 0 && bytes == 0) {
		/* First dde is unused; make it a direct dde */
		bytes = len;
		putp32(ddl, ddebc, bytes);
		putp64(ddl, ddead, (uint64_t) addr);
	} else if (ddecnt == 0) {
		/* Converting direct to indirect dde
		 * ddl[0] becomes head dde of ddl
		 * copy direct to indirect first.
		 */
		ddl[1] = ddl[0];

		/* Add the new dde next */
		clear_dde(ddl[2]);
		put32(ddl[2], ddebc, len);
		put64(ddl[2], ddead, (uint64_t) addr);

		/* Ddl head points to 2 direct ddes */
		ddecnt = 2;
		putpnn(ddl, dde_count, ddecnt);
		bytes = bytes + len;
		putp32(ddl, ddebc, bytes);
		/* Pointer to the first direct dde */
		putp64(ddl, ddead, (uint64_t) &ddl[1]);
	} else {
		/* Append a dde to an existing indirect ddl */
		++ddecnt;
		clear_dde(ddl[ddecnt]);
		put64(ddl[ddecnt], ddead, (uint64_t) addr);
		put32(ddl[ddecnt], ddebc, len);

		putpnn(ddl, dde_count, ddecnt);
		bytes = bytes + len;
		putp32(ddl, ddebc, bytes); /* byte sum of all dde */
	}
	return bytes;
}

/*
 * Touch specified number of pages represented in number bytes
 * beginning from the first buffer in a dde list.
 * Do not touch the pages past buf_sz-th byte's page.
 *
 * Set buf_sz = 0 to touch all pages described by the ddep.
 */
static int nx_touch_pages_dde(struct nx_dde_t *ddep, long buf_sz, long page_sz,
				int wr)
{
	uint32_t indirect_count;
	uint32_t buf_len;
	long total;
	uint64_t buf_addr;
	struct nx_dde_t *dde_list;
	int i;

	assert(!!ddep);

	indirect_count = getpnn(ddep, dde_count);

	NXPRT(fprintf(stderr, "%s dde_count %d request len ", __func__,
			indirect_count));
	NXPRT(fprintf(stderr, "0x%lx\n", buf_sz));

	if (indirect_count == 0) {
		/* Direct dde */
		buf_len = getp32(ddep, ddebc);
		buf_addr = getp64(ddep, ddead);

		NXPRT(fprintf(stderr, "touch direct ddebc 0x%x ddead %p\n",
				buf_len, (void *)buf_addr));

		if (buf_sz == 0)
			nxu_touch_pages((void *)buf_addr, buf_len, page_sz, wr);
		else
			nxu_touch_pages((void *)buf_addr, NX_MIN(buf_len,
					buf_sz), page_sz, wr);

		return ERR_NX_OK;
	}

	/* Indirect dde */
	if (indirect_count > MAX_DDE_COUNT)
		return ERR_NX_EXCESSIVE_DDE;

	/* First address of the list */
	dde_list = (struct nx_dde_t *) getp64(ddep, ddead);

	if (buf_sz == 0)
		buf_sz = getp32(ddep, ddebc);

	total = 0;
	for (i = 0; i < indirect_count; i++) {
		buf_len = get32(dde_list[i], ddebc);
		buf_addr = get64(dde_list[i], ddead);
		total += buf_len;

		NXPRT(fprintf(stderr, "touch loop len 0x%x ddead %p total ",
				buf_len, (void *)buf_addr));
		NXPRT(fprintf(stderr, "0x%lx\n", total));

		/* Touching fewer pages than encoded in the ddebc */
		if (total > buf_sz) {
			buf_len = NX_MIN(buf_len, total - buf_sz);
			nxu_touch_pages((void *)buf_addr, buf_len, page_sz, wr);
			NXPRT(fprintf(stderr, "touch loop break len 0x%x ",
				      buf_len));
			NXPRT(fprintf(stderr, "ddead %p\n", (void *)buf_addr));
			break;
		}
		nxu_touch_pages((void *)buf_addr, buf_len, page_sz, wr);
	}
	return ERR_NX_OK;
}

/*
 * Src and dst buffers are supplied in scatter gather lists.
 * NX function code and other parameters supplied in cmdp.
 */
static int nx_submit_job(struct nx_dde_t *src, struct nx_dde_t *dst,
			 struct nx_gzip_crb_cpb_t *cmdp, void *handle)
{
	uint64_t csbaddr;

	memset((void *)&cmdp->crb.csb, 0, sizeof(cmdp->crb.csb));

	cmdp->crb.source_dde = *src;
	cmdp->crb.target_dde = *dst;

	/* Status, output byte count in tpbc */
	csbaddr = ((uint64_t) &cmdp->crb.csb) & csb_address_mask;
	put64(cmdp->crb, csb_address, csbaddr);

	/* NX reports input bytes in spbc; cleared */
	cmdp->cpb.out_spbc_comp_wrap = 0;
	cmdp->cpb.out_spbc_comp_with_count = 0;
	cmdp->cpb.out_spbc_decomp = 0;

	/* Clear output */
	put32(cmdp->cpb, out_crc, INIT_CRC);
	put32(cmdp->cpb, out_adler, INIT_ADLER);

	/* Submit the crb, the job descriptor, to the accelerator. */
	return nxu_submit_job(cmdp, handle);
}

int decompress_file(int argc, char **argv, void *devhandle)
{
	FILE *inpf = NULL;
	FILE *outf = NULL;

	int c, expect, i, cc, rc = 0;
	char gzfname[FNAME_MAX];

	/* Queuing, file ops, byte counting */
	char *fifo_in, *fifo_out;
	int used_in, cur_in, used_out, cur_out, read_sz, n;
	int first_free, last_free, first_used, last_used;
	int first_offset, last_offset;
	int write_sz, free_space, source_sz;
	int source_sz_estimate, target_sz_estimate;
	uint64_t last_comp_ratio = 0; /* 1000 max */
	uint64_t total_out = 0;
	int is_final, is_eof;

	/* nx hardware */
	int sfbt, subc, spbc, tpbc, nx_ce, fc, resuming = 0;
	int history_len = 0;
	struct nx_gzip_crb_cpb_t cmd, *cmdp;
	struct nx_dde_t *ddl_in;
	struct nx_dde_t dde_in[6] __aligned(128);
	struct nx_dde_t *ddl_out;
	struct nx_dde_t dde_out[6] __aligned(128);
	int pgfault_retries;

	/* when using mmap'ed files */
	off_t input_file_offset;

	if (argc > 2) {
		fprintf(stderr, "usage: %s <fname> or stdin\n", argv[0]);
		fprintf(stderr, "    writes to stdout or <fname>.nx.gunzip\n");
		return -1;
	}

	if (argc == 1) {
		inpf = stdin;
		outf = stdout;
	} else if (argc == 2) {
		char w[1024];
		char *wp;

		inpf = fopen(argv[1], "r");
		if (inpf == NULL) {
			perror(argv[1]);
			return -1;
		}

		/* Make a new file name to write to.  Ignoring '.gz' */
		wp = (NULL != (wp = strrchr(argv[1], '/'))) ? (wp+1) : argv[1];
		strcpy(w, wp);
		strcat(w, ".nx.gunzip");

		outf = fopen(w, "w");
		if (outf == NULL) {
			perror(w);
			return -1;
		}
	}

	/* Decode the gzip header */
	c = GETINPC(inpf); expect = 0x1f; /* ID1 */
	if (c != expect)
		goto err1;

	c = GETINPC(inpf); expect = 0x8b; /* ID2 */
	if (c != expect)
		goto err1;

	c = GETINPC(inpf); expect = 0x08; /* CM */
	if (c != expect)
		goto err1;

	int flg = GETINPC(inpf); /* FLG */

	if (flg & 0xE0 || flg & 0x4 || flg == EOF)
		goto err2;

	fprintf(stderr, "gzHeader FLG %x\n", flg);

	/* Read 6 bytes; ignoring the MTIME, XFL, OS fields in this
	 * sample code.
	 */
	for (i = 0; i < 6; i++) {
		char tmp[10];

		tmp[i] = GETINPC(inpf);
		if (tmp[i] == EOF)
			goto err3;
		fprintf(stderr, "%02x ", tmp[i]);
		if (i == 5)
			fprintf(stderr, "\n");
	}
	fprintf(stderr, "gzHeader MTIME, XFL, OS ignored\n");

	/* FNAME */
	if (flg & 0x8) {
		int k = 0;

		do {
			c = GETINPC(inpf);
			if (c == EOF || k >= FNAME_MAX)
				goto err3;
			gzfname[k++] = c;
		} while (c);
		fprintf(stderr, "gzHeader FNAME: %s\n", gzfname);
	}

	/* FHCRC */
	if (flg & 0x2) {
		c = GETINPC(inpf);
		if (c == EOF)
			goto err3;
		c = GETINPC(inpf);
		if (c == EOF)
			goto err3;
		fprintf(stderr, "gzHeader FHCRC: ignored\n");
	}

	used_in = cur_in = used_out = cur_out = 0;
	is_final = is_eof = 0;

	/* Allocate one page larger to prevent page faults due to NX
	 * overfetching.
	 * Either do this (char*)(uintptr_t)aligned_alloc or use
	 * -std=c11 flag to make the int-to-pointer warning go away.
	 */
	assert((fifo_in  = (char *)(uintptr_t)aligned_alloc(line_sz,
				   fifo_in_len + page_sz)) != NULL);
	assert((fifo_out = (char *)(uintptr_t)aligned_alloc(line_sz,
				   fifo_out_len + page_sz + line_sz)) != NULL);
	/* Leave unused space due to history rounding rules */
	fifo_out = fifo_out + line_sz;
	nxu_touch_pages(fifo_out, fifo_out_len, page_sz, 1);

	ddl_in  = &dde_in[0];
	ddl_out = &dde_out[0];
	cmdp = &cmd;
	memset(&cmdp->crb, 0, sizeof(cmdp->crb));

read_state:

	/* Read from .gz file */

	NXPRT(fprintf(stderr, "read_state:\n"));

	if (is_eof != 0)
		goto write_state;

	/* We read in to fifo_in in two steps: first: read in to from
	 * cur_in to the end of the buffer.  last: if free space wrapped
	 * around, read from fifo_in offset 0 to offset cur_in.
	 */

	/* Reset fifo head to reduce unnecessary wrap arounds */
	cur_in = (used_in == 0) ? 0 : cur_in;

	/* Free space total is reduced by a gap */
	free_space = NX_MAX(0, fifo_free_bytes(used_in, fifo_in_len)
			    - line_sz);

	/* Free space may wrap around as first and last */
	first_free = fifo_free_first_bytes(cur_in, used_in, fifo_in_len);
	last_free  = fifo_free_last_bytes(cur_in, used_in, fifo_in_len);

	/* Start offsets of the free memory */
	first_offset = fifo_free_first_offset(cur_in, used_in);
	last_offset  = fifo_free_last_offset(cur_in, used_in, fifo_in_len);

	/* Reduce read_sz because of the line_sz gap */
	read_sz = NX_MIN(free_space, first_free);
	n = 0;
	if (read_sz > 0) {
		/* Read in to offset cur_in + used_in */
		n = fread(fifo_in + first_offset, 1, read_sz, inpf);
		used_in = used_in + n;
		free_space = free_space - n;
		assert(n <= read_sz);
		if (n != read_sz) {
			/* Either EOF or error; exit the read loop */
			is_eof = 1;
			goto write_state;
		}
	}

	/* If free space wrapped around */
	if (last_free > 0) {
		/* Reduce read_sz because of the line_sz gap */
		read_sz = NX_MIN(free_space, last_free);
		n = 0;
		if (read_sz > 0) {
			n = fread(fifo_in + last_offset, 1, read_sz, inpf);
			used_in = used_in + n;       /* Increase used space */
			free_space = free_space - n; /* Decrease free space */
			assert(n <= read_sz);
			if (n != read_sz) {
				/* Either EOF or error; exit the read loop */
				is_eof = 1;
				goto write_state;
			}
		}
	}

	/* At this point we have used_in bytes in fifo_in with the
	 * data head starting at cur_in and possibly wrapping around.
	 */

write_state:

	/* Write decompressed data to output file */

	NXPRT(fprintf(stderr, "write_state:\n"));

	if (used_out == 0)
		goto decomp_state;

	/* If fifo_out has data waiting, write it out to the file to
	 * make free target space for the accelerator used bytes in
	 * the first and last parts of fifo_out.
	 */

	first_used = fifo_used_first_bytes(cur_out, used_out, fifo_out_len);
	last_used  = fifo_used_last_bytes(cur_out, used_out, fifo_out_len);

	write_sz = first_used;

	n = 0;
	if (write_sz > 0) {
		n = fwrite(fifo_out + cur_out, 1, write_sz, outf);
		used_out = used_out - n;
		/* Move head of the fifo */
		cur_out = (cur_out + n) % fifo_out_len;
		assert(n <= write_sz);
		if (n != write_sz) {
			fprintf(stderr, "error: write\n");
			rc = -1;
			goto err5;
		}
	}

	if (last_used > 0) { /* If more data available in the last part */
		write_sz = last_used; /* Keep it here for later */
		n = 0;
		if (write_sz > 0) {
			n = fwrite(fifo_out, 1, write_sz, outf);
			used_out = used_out - n;
			cur_out = (cur_out + n) % fifo_out_len;
			assert(n <= write_sz);
			if (n != write_sz) {
				fprintf(stderr, "error: write\n");
				rc = -1;
				goto err5;
			}
		}
	}

decomp_state:

	/* NX decompresses input data */

	NXPRT(fprintf(stderr, "decomp_state:\n"));

	if (is_final)
		goto finish_state;

	/* Address/len lists */
	clearp_dde(ddl_in);
	clearp_dde(ddl_out);

	/* FC, CRC, HistLen, Table 6-6 */
	if (resuming) {
		/* Resuming a partially decompressed input.
		 * The key to resume is supplying the 32KB
		 * dictionary (history) to NX, which is basically
		 * the last 32KB of output produced.
		 */
		fc = GZIP_FC_DECOMPRESS_RESUME;

		cmdp->cpb.in_crc   = cmdp->cpb.out_crc;
		cmdp->cpb.in_adler = cmdp->cpb.out_adler;

		/* Round up the history size to quadword.  Section 2.10 */
		history_len = (history_len + 15) / 16;
		putnn(cmdp->cpb, in_histlen, history_len);
		history_len = history_len * 16; /* bytes */

		if (history_len > 0) {
			/* Chain in the history buffer to the DDE list */
			if (cur_out >= history_len) {
				nx_append_dde(ddl_in, fifo_out
					      + (cur_out - history_len),
					      history_len);
			} else {
				nx_append_dde(ddl_in, fifo_out
					      + ((fifo_out_len + cur_out)
					      - history_len),
					      history_len - cur_out);
				/* Up to 32KB history wraps around fifo_out */
				nx_append_dde(ddl_in, fifo_out, cur_out);
			}

		}
	} else {
		/* First decompress job */
		fc = GZIP_FC_DECOMPRESS;

		history_len = 0;
		/* Writing 0 clears out subc as well */
		cmdp->cpb.in_histlen = 0;
		total_out = 0;

		put32(cmdp->cpb, in_crc, INIT_CRC);
		put32(cmdp->cpb, in_adler, INIT_ADLER);
		put32(cmdp->cpb, out_crc, INIT_CRC);
		put32(cmdp->cpb, out_adler, INIT_ADLER);

		/* Assuming 10% compression ratio initially; use the
		 * most recently measured compression ratio as a
		 * heuristic to estimate the input and output
		 * sizes.  If we give too much input, the target buffer
		 * overflows and NX cycles are wasted, and then we
		 * must retry with smaller input size.  1000 is 100%.
		 */
		last_comp_ratio = 100UL;
	}
	cmdp->crb.gzip_fc = 0;
	putnn(cmdp->crb, gzip_fc, fc);

	/*
	 * NX source buffers
	 */
	first_used = fifo_used_first_bytes(cur_in, used_in, fifo_in_len);
	last_used = fifo_used_last_bytes(cur_in, used_in, fifo_in_len);

	if (first_used > 0)
		nx_append_dde(ddl_in, fifo_in + cur_in, first_used);

	if (last_used > 0)
		nx_append_dde(ddl_in, fifo_in, last_used);

	/*
	 * NX target buffers
	 */
	first_free = fifo_free_first_bytes(cur_out, used_out, fifo_out_len);
	last_free = fifo_free_last_bytes(cur_out, used_out, fifo_out_len);

	/* Reduce output free space amount not to overwrite the history */
	int target_max = NX_MAX(0, fifo_free_bytes(used_out, fifo_out_len)
				- (1<<16));

	NXPRT(fprintf(stderr, "target_max %d (0x%x)\n", target_max,
		      target_max));

	first_free = NX_MIN(target_max, first_free);
	if (first_free > 0) {
		first_offset = fifo_free_first_offset(cur_out, used_out);
		nx_append_dde(ddl_out, fifo_out + first_offset, first_free);
	}

	if (last_free > 0) {
		last_free = NX_MIN(target_max - first_free, last_free);
		if (last_free > 0) {
			last_offset = fifo_free_last_offset(cur_out, used_out,
							    fifo_out_len);
			nx_append_dde(ddl_out, fifo_out + last_offset,
				      last_free);
		}
	}

	/* Target buffer size is used to limit the source data size
	 * based on previous measurements of compression ratio.
	 */

	/* source_sz includes history */
	source_sz = getp32(ddl_in, ddebc);
	assert(source_sz > history_len);
	source_sz = source_sz - history_len;

	/* Estimating how much source is needed to 3/4 fill a
	 * target_max size target buffer.  If we overshoot, then NX
	 * must repeat the job with smaller input and we waste
	 * bandwidth.  If we undershoot then we use more NX calls than
	 * necessary.
	 */

	source_sz_estimate = ((uint64_t)target_max * last_comp_ratio * 3UL)
				/ 4000;

	if (source_sz_estimate < source_sz) {
		/* Target might be small, therefore limiting the
		 * source data.
		 */
		source_sz = source_sz_estimate;
		target_sz_estimate = target_max;
	} else {
		/* Source file might be small, therefore limiting target
		 * touch pages to a smaller value to save processor cycles.
		 */
		target_sz_estimate = ((uint64_t)source_sz * 1000UL)
					/ (last_comp_ratio + 1);
		target_sz_estimate = NX_MIN(2 * target_sz_estimate,
					    target_max);
	}

	source_sz = source_sz + history_len;

	/* Some NX condition codes require submitting the NX job again.
	 * Kernel doesn't handle NX page faults. Expects user code to
	 * touch pages.
	 */
	pgfault_retries = NX_MAX_FAULTS;

restart_nx:

	putp32(ddl_in, ddebc, source_sz);

	/* Fault in pages */
	nxu_touch_pages(cmdp, sizeof(struct nx_gzip_crb_cpb_t), page_sz, 1);
	nx_touch_pages_dde(ddl_in, 0, page_sz, 0);
	nx_touch_pages_dde(ddl_out, target_sz_estimate, page_sz, 1);

	/* Send job to NX */
	cc = nx_submit_job(ddl_in, ddl_out, cmdp, devhandle);

	switch (cc) {

	case ERR_NX_TRANSLATION:

		/* We touched the pages ahead of time.  In the most common case
		 * we shouldn't be here.  But may be some pages were paged out.
		 * Kernel should have placed the faulting address to fsaddr.
		 */
		NXPRT(fprintf(stderr, "ERR_NX_TRANSLATION %p\n",
			      (void *)cmdp->crb.csb.fsaddr));

		if (pgfault_retries == NX_MAX_FAULTS) {
			/* Try once with exact number of pages */
			--pgfault_retries;
			goto restart_nx;
		} else if (pgfault_retries > 0) {
			/* If still faulting try fewer input pages
			 * assuming memory outage
			 */
			if (source_sz > page_sz)
				source_sz = NX_MAX(source_sz / 2, page_sz);
			--pgfault_retries;
			goto restart_nx;
		} else {
			fprintf(stderr, "cannot make progress; too many ");
			fprintf(stderr, "page fault retries cc= %d\n", cc);
			rc = -1;
			goto err5;
		}

	case ERR_NX_DATA_LENGTH:

		NXPRT(fprintf(stderr, "ERR_NX_DATA_LENGTH; "));
		NXPRT(fprintf(stderr, "stream may have trailing data\n"));

		/* Not an error in the most common case; it just says
		 * there is trailing data that we must examine.
		 *
		 * CC=3 CE(1)=0 CE(0)=1 indicates partial completion
		 * Fig.6-7 and Table 6-8.
		 */
		nx_ce = get_csb_ce_ms3b(cmdp->crb.csb);

		if (!csb_ce_termination(nx_ce) &&
		    csb_ce_partial_completion(nx_ce)) {
			/* Check CPB for more information
			 * spbc and tpbc are valid
			 */
			sfbt = getnn(cmdp->cpb, out_sfbt); /* Table 6-4 */
			subc = getnn(cmdp->cpb, out_subc); /* Table 6-4 */
			spbc = get32(cmdp->cpb, out_spbc_decomp);
			tpbc = get32(cmdp->crb.csb, tpbc);
			assert(target_max >= tpbc);

			goto ok_cc3; /* not an error */
		} else {
			/* History length error when CE(1)=1 CE(0)=0. */
			rc = -1;
			fprintf(stderr, "history length error cc= %d\n", cc);
			goto err5;
		}

	case ERR_NX_TARGET_SPACE:

		/* Target buffer not large enough; retry smaller input
		 * data; give at least 1 byte.  SPBC/TPBC are not valid.
		 */
		assert(source_sz > history_len);
		source_sz = ((source_sz - history_len + 2) / 2) + history_len;
		NXPRT(fprintf(stderr, "ERR_NX_TARGET_SPACE; retry with "));
		NXPRT(fprintf(stderr, "smaller input data src %d hist %d\n",
			      source_sz, history_len));
		goto restart_nx;

	case ERR_NX_OK:

		/* This should not happen for gzip formatted data;
		 * we need trailing crc and isize
		 */
		fprintf(stderr, "ERR_NX_OK\n");
		spbc = get32(cmdp->cpb, out_spbc_decomp);
		tpbc = get32(cmdp->crb.csb, tpbc);
		assert(target_max >= tpbc);
		assert(spbc >= history_len);
		source_sz = spbc - history_len;
		goto offsets_state;

	default:
		fprintf(stderr, "error: cc= %d\n", cc);
		rc = -1;
		goto err5;
	}

ok_cc3:

	NXPRT(fprintf(stderr, "cc3: sfbt: %x\n", sfbt));

	assert(spbc > history_len);
	source_sz = spbc - history_len;

	/* Table 6-4: Source Final Block Type (SFBT) describes the
	 * last processed deflate block and clues the software how to
	 * resume the next job.  SUBC indicates how many input bits NX
	 * consumed but did not process.  SPBC indicates how many
	 * bytes of source were given to the accelerator including
	 * history bytes.
	 */

	switch (sfbt) {
		int dhtlen;

	case 0x0: /* Deflate final EOB received */

		/* Calculating the checksum start position. */

		source_sz = source_sz - subc / 8;
		is_final = 1;
		break;

		/* Resume decompression cases are below. Basically
		 * indicates where NX has suspended and how to resume
		 * the input stream.
		 */

	case 0x8: /* Within a literal block; use rembytecount */
	case 0x9: /* Within a literal block; use rembytecount; bfinal=1 */

		/* Supply the partially processed source byte again */
		source_sz = source_sz - ((subc + 7) / 8);

		/* SUBC LS 3bits: number of bits in the first source byte need
		 * to be processed.
		 * 000 means all 8 bits;  Table 6-3
		 * Clear subc, histlen, sfbt, rembytecnt, dhtlen
		 */
		cmdp->cpb.in_subc = 0;
		cmdp->cpb.in_sfbt = 0;
		putnn(cmdp->cpb, in_subc, subc % 8);
		putnn(cmdp->cpb, in_sfbt, sfbt);
		putnn(cmdp->cpb, in_rembytecnt, getnn(cmdp->cpb,
						      out_rembytecnt));
		break;

	case 0xA: /* Within a FH block; */
	case 0xB: /* Within a FH block; bfinal=1 */

		source_sz = source_sz - ((subc + 7) / 8);

		/* Clear subc, histlen, sfbt, rembytecnt, dhtlen */
		cmdp->cpb.in_subc = 0;
		cmdp->cpb.in_sfbt = 0;
		putnn(cmdp->cpb, in_subc, subc % 8);
		putnn(cmdp->cpb, in_sfbt, sfbt);
		break;

	case 0xC: /* Within a DH block; */
	case 0xD: /* Within a DH block; bfinal=1 */

		source_sz = source_sz - ((subc + 7) / 8);

		/* Clear subc, histlen, sfbt, rembytecnt, dhtlen */
		cmdp->cpb.in_subc = 0;
		cmdp->cpb.in_sfbt = 0;
		putnn(cmdp->cpb, in_subc, subc % 8);
		putnn(cmdp->cpb, in_sfbt, sfbt);

		dhtlen = getnn(cmdp->cpb, out_dhtlen);
		putnn(cmdp->cpb, in_dhtlen, dhtlen);
		assert(dhtlen >= 42);

		/* Round up to a qword */
		dhtlen = (dhtlen + 127) / 128;

		while (dhtlen > 0) { /* Copy dht from cpb.out to cpb.in */
			--dhtlen;
			cmdp->cpb.in_dht[dhtlen] = cmdp->cpb.out_dht[dhtlen];
		}
		break;

	case 0xE: /* Within a block header; bfinal=0; */
		     /* Also given if source data exactly ends (SUBC=0) with
		      * EOB code with BFINAL=0.  Means the next byte will
		      * contain a block header.
		      */
	case 0xF: /* within a block header with BFINAL=1. */

		source_sz = source_sz - ((subc + 7) / 8);

		/* Clear subc, histlen, sfbt, rembytecnt, dhtlen */
		cmdp->cpb.in_subc = 0;
		cmdp->cpb.in_sfbt = 0;
		putnn(cmdp->cpb, in_subc, subc % 8);
		putnn(cmdp->cpb, in_sfbt, sfbt);

		/* Engine did not process any data */
		if (is_eof && (source_sz == 0))
			is_final = 1;
	}

offsets_state:

	/* Adjust the source and target buffer offsets and lengths  */

	NXPRT(fprintf(stderr, "offsets_state:\n"));

	/* Delete input data from fifo_in */
	used_in = used_in - source_sz;
	cur_in = (cur_in + source_sz) % fifo_in_len;
	input_file_offset = input_file_offset + source_sz;

	/* Add output data to fifo_out */
	used_out = used_out + tpbc;

	assert(used_out <= fifo_out_len);

	total_out = total_out + tpbc;

	/* Deflate history is 32KB max.  No need to supply more
	 * than 32KB on a resume.
	 */
	history_len = (total_out > window_max) ? window_max : total_out;

	/* To estimate expected expansion in the next NX job; 500 means 50%.
	 * Deflate best case is around 1 to 1000.
	 */
	last_comp_ratio = (1000UL * ((uint64_t)source_sz + 1))
			  / ((uint64_t)tpbc + 1);
	last_comp_ratio = NX_MAX(NX_MIN(1000UL, last_comp_ratio), 1);
	NXPRT(fprintf(stderr, "comp_ratio %ld source_sz %d spbc %d tpbc %d\n",
		      last_comp_ratio, source_sz, spbc, tpbc));

	resuming = 1;

finish_state:

	NXPRT(fprintf(stderr, "finish_state:\n"));

	if (is_final) {
		if (used_out)
			goto write_state; /* More data to write out */
		else if (used_in < 8) {
			/* Need at least 8 more bytes containing gzip crc
			 * and isize.
			 */
			rc = -1;
			goto err4;
		} else {
			/* Compare checksums and exit */
			int i;
			unsigned char tail[8];
			uint32_t cksum, isize;

			for (i = 0; i < 8; i++)
				tail[i] = fifo_in[(cur_in + i) % fifo_in_len];
			fprintf(stderr, "computed checksum %08x isize %08x\n",
				cmdp->cpb.out_crc, (uint32_t) (total_out
				% (1ULL<<32)));
			cksum = ((uint32_t) tail[0] | (uint32_t) tail[1]<<8
				 | (uint32_t) tail[2]<<16
				 | (uint32_t) tail[3]<<24);
			isize = ((uint32_t) tail[4] | (uint32_t) tail[5]<<8
				 | (uint32_t) tail[6]<<16
				 | (uint32_t) tail[7]<<24);
			fprintf(stderr, "stored   checksum %08x isize %08x\n",
				cksum, isize);

			if (cksum == cmdp->cpb.out_crc && isize == (uint32_t)
			    (total_out % (1ULL<<32))) {
				rc = 0;	goto ok1;
			} else {
				rc = -1; goto err4;
			}
		}
	} else
		goto read_state;

	return -1;

err1:
	fprintf(stderr, "error: not a gzip file, expect %x, read %x\n",
		expect, c);
	return -1;

err2:
	fprintf(stderr, "error: the FLG byte is wrong or not being handled\n");
	return -1;

err3:
	fprintf(stderr, "error: gzip header\n");
	return -1;

err4:
	fprintf(stderr, "error: checksum missing or mismatch\n");

err5:
ok1:
	fprintf(stderr, "decomp is complete: fclose\n");
	fclose(outf);

	return rc;
}


int main(int argc, char **argv)
{
	int rc;
	struct sigaction act;
	void *handle;

	nx_dbg = 0;
	nx_gzip_log = NULL;
	act.sa_handler = 0;
	act.sa_sigaction = nxu_sigsegv_handler;
	act.sa_flags = SA_SIGINFO;
	act.sa_restorer = 0;
	sigemptyset(&act.sa_mask);
	sigaction(SIGSEGV, &act, NULL);

	handle = nx_function_begin(NX_FUNC_COMP_GZIP, 0);
	if (!handle) {
		fprintf(stderr, "Unable to init NX, errno %d\n", errno);
		exit(-1);
	}

	rc = decompress_file(argc, argv, handle);

	nx_function_end(handle);

	return rc;
}
