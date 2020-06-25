// SPDX-License-Identifier: GPL-2.0-or-later

/* P9 gzip sample code for demonstrating the P9 NX hardware interface.
 * Not intended for productive uses or for performance or compression
 * ratio measurements.  For simplicity of demonstration, this sample
 * code compresses in to fixed Huffman blocks only (Deflate btype=1)
 * and has very simple memory management.  Dynamic Huffman blocks
 * (Deflate btype=2) are more involved as detailed in the user guide.
 * Note also that /dev/crypto/gzip, VAS and skiboot support are
 * required.
 *
 * Copyright 2020 IBM Corp.
 *
 * https://github.com/libnxz/power-gzip for zlib api and other utils
 *
 * Author: Bulent Abali <abali@us.ibm.com>
 *
 * Definitions of acronyms used here. See
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

int nx_dbg;
FILE *nx_gzip_log;

#define NX_MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define FNAME_MAX 1024
#define FEXT ".nx.gz"

/*
 * LZ counts returned in the user supplied nx_gzip_crb_cpb_t structure.
 */
static int compress_fht_sample(char *src, uint32_t srclen, char *dst,
				uint32_t dstlen, int with_count,
				struct nx_gzip_crb_cpb_t *cmdp, void *handle)
{
	uint32_t fc;

	assert(!!cmdp);

	put32(cmdp->crb, gzip_fc, 0);  /* clear */
	fc = (with_count) ? GZIP_FC_COMPRESS_RESUME_FHT_COUNT :
			    GZIP_FC_COMPRESS_RESUME_FHT;
	putnn(cmdp->crb, gzip_fc, fc);
	putnn(cmdp->cpb, in_histlen, 0); /* resuming with no history */
	memset((void *) &cmdp->crb.csb, 0, sizeof(cmdp->crb.csb));

	/* Section 6.6 programming notes; spbc may be in two different
	 * places depending on FC.
	 */
	if (!with_count)
		put32(cmdp->cpb, out_spbc_comp, 0);
	else
		put32(cmdp->cpb, out_spbc_comp_with_count, 0);

	/* Figure 6-3 6-4; CSB location */
	put64(cmdp->crb, csb_address, 0);
	put64(cmdp->crb, csb_address,
	      (uint64_t) &cmdp->crb.csb & csb_address_mask);

	/* Source direct dde (scatter-gather list) */
	clear_dde(cmdp->crb.source_dde);
	putnn(cmdp->crb.source_dde, dde_count, 0);
	put32(cmdp->crb.source_dde, ddebc, srclen);
	put64(cmdp->crb.source_dde, ddead, (uint64_t) src);

	/* Target direct dde (scatter-gather list) */
	clear_dde(cmdp->crb.target_dde);
	putnn(cmdp->crb.target_dde, dde_count, 0);
	put32(cmdp->crb.target_dde, ddebc, dstlen);
	put64(cmdp->crb.target_dde, ddead, (uint64_t) dst);

	/* Submit the crb, the job descriptor, to the accelerator */
	return nxu_submit_job(cmdp, handle);
}

/*
 * Prepares a blank no filename no timestamp gzip header and returns
 * the number of bytes written to buf.
 * Gzip specification at https://tools.ietf.org/html/rfc1952
 */
int gzip_header_blank(char *buf)
{
	int i = 0;

	buf[i++] = 0x1f; /* ID1 */
	buf[i++] = 0x8b; /* ID2 */
	buf[i++] = 0x08; /* CM  */
	buf[i++] = 0x00; /* FLG */
	buf[i++] = 0x00; /* MTIME */
	buf[i++] = 0x00; /* MTIME */
	buf[i++] = 0x00; /* MTIME */
	buf[i++] = 0x00; /* MTIME */
	buf[i++] = 0x04; /* XFL 4=fastest */
	buf[i++] = 0x03; /* OS UNIX */

	return i;
}

/* Caller must free the allocated buffer return nonzero on error. */
int read_alloc_input_file(char *fname, char **buf, size_t *bufsize)
{
	struct stat statbuf;
	FILE *fp;
	char *p;
	size_t num_bytes;

	if (stat(fname, &statbuf)) {
		perror(fname);
		return(-1);
	}
	fp = fopen(fname, "r");
	if (fp == NULL) {
		perror(fname);
		return(-1);
	}
	assert(NULL != (p = (char *) malloc(statbuf.st_size)));
	num_bytes = fread(p, 1, statbuf.st_size, fp);
	if (ferror(fp) || (num_bytes != statbuf.st_size)) {
		perror(fname);
		return(-1);
	}
	*buf = p;
	*bufsize = num_bytes;
	return 0;
}

/* Returns nonzero on error */
int write_output_file(char *fname, char *buf, size_t bufsize)
{
	FILE *fp;
	size_t num_bytes;

	fp = fopen(fname, "w");
	if (fp == NULL) {
		perror(fname);
		return(-1);
	}
	num_bytes = fwrite(buf, 1, bufsize, fp);
	if (ferror(fp) || (num_bytes != bufsize)) {
		perror(fname);
		return(-1);
	}
	fclose(fp);
	return 0;
}

/*
 * Z_SYNC_FLUSH as described in zlib.h.
 * Returns number of appended bytes
 */
int append_sync_flush(char *buf, int tebc, int final)
{
	uint64_t flush;
	int shift = (tebc & 0x7);

	if (tebc > 0) {
		/* Last byte is partially full */
		buf = buf - 1;
		*buf = *buf & (unsigned char) ((1<<tebc)-1);
	} else
		*buf = 0;
	flush = ((0x1ULL & final) << shift) | *buf;
	shift = shift + 3; /* BFINAL and BTYPE written */
	shift = (shift <= 8) ? 8 : 16;
	flush |= (0xFFFF0000ULL) << shift; /* Zero length block */
	shift = shift + 32;
	while (shift > 0) {
		*buf++ = (unsigned char) (flush & 0xffULL);
		flush = flush >> 8;
		shift = shift - 8;
	}
	return(((tebc > 5) || (tebc == 0)) ? 5 : 4);
}

/*
 * Final deflate block bit.  This call assumes the block
 * beginning is byte aligned.
 */
static void set_bfinal(void *buf, int bfinal)
{
	char *b = buf;

	if (bfinal)
		*b = *b | (unsigned char) 0x01;
	else
		*b = *b & (unsigned char) 0xfe;
}

int compress_file(int argc, char **argv, void *handle)
{
	char *inbuf, *outbuf, *srcbuf, *dstbuf;
	char outname[FNAME_MAX];
	uint32_t srclen, dstlen;
	uint32_t flushlen, chunk;
	size_t inlen, outlen, dsttotlen, srctotlen;
	uint32_t crc, spbc, tpbc, tebc;
	int lzcounts = 0;
	int cc;
	int num_hdr_bytes;
	struct nx_gzip_crb_cpb_t *cmdp;
	uint32_t pagelen = 65536;
	int fault_tries = NX_MAX_FAULTS;

	cmdp = (void *)(uintptr_t)
		aligned_alloc(sizeof(struct nx_gzip_crb_cpb_t),
			      sizeof(struct nx_gzip_crb_cpb_t));

	if (argc != 2) {
		fprintf(stderr, "usage: %s <fname>\n", argv[0]);
		exit(-1);
	}
	if (read_alloc_input_file(argv[1], &inbuf, &inlen))
		exit(-1);
	fprintf(stderr, "file %s read, %ld bytes\n", argv[1], inlen);

	/* Generous output buffer for header/trailer */
	outlen = 2 * inlen + 1024;

	assert(NULL != (outbuf = (char *)malloc(outlen)));
	nxu_touch_pages(outbuf, outlen, pagelen, 1);

	/* Compress piecemeal in smallish chunks */
	chunk = 1<<22;

	/* Write the gzip header to the stream */
	num_hdr_bytes = gzip_header_blank(outbuf);
	dstbuf    = outbuf + num_hdr_bytes;
	outlen    = outlen - num_hdr_bytes;
	dsttotlen = num_hdr_bytes;

	srcbuf    = inbuf;
	srctotlen = 0;

	/* Init the CRB, the coprocessor request block */
	memset(&cmdp->crb, 0, sizeof(cmdp->crb));

	/* Initial gzip crc32 */
	put32(cmdp->cpb, in_crc, 0);

	while (inlen > 0) {

		/* Submit chunk size source data per job */
		srclen = NX_MIN(chunk, inlen);
		/* Supply large target in case data expands */
		dstlen = NX_MIN(2*srclen, outlen);

		/* Page faults are handled by the user code */

		/* Fault-in pages; an improved code wouldn't touch so
		 * many pages but would try to estimate the
		 * compression ratio and adjust both the src and dst
		 * touch amounts.
		 */
		nxu_touch_pages(cmdp, sizeof(struct nx_gzip_crb_cpb_t), pagelen,
				1);
		nxu_touch_pages(srcbuf, srclen, pagelen, 0);
		nxu_touch_pages(dstbuf, dstlen, pagelen, 1);

		cc = compress_fht_sample(
			srcbuf, srclen,
			dstbuf, dstlen,
			lzcounts, cmdp, handle);

		if (cc != ERR_NX_OK && cc != ERR_NX_TPBC_GT_SPBC &&
		    cc != ERR_NX_TRANSLATION) {
			fprintf(stderr, "nx error: cc= %d\n", cc);
			exit(-1);
		}

		/* Page faults are handled by the user code */
		if (cc == ERR_NX_TRANSLATION) {
			NXPRT(fprintf(stderr, "page fault: cc= %d, ", cc));
			NXPRT(fprintf(stderr, "try= %d, fsa= %08llx\n",
				  fault_tries,
				  (unsigned long long) cmdp->crb.csb.fsaddr));
			fault_tries--;
			if (fault_tries > 0) {
				continue;
			} else {
				fprintf(stderr, "error: cannot progress; ");
				fprintf(stderr, "too many faults\n");
				exit(-1);
			};
		}

		fault_tries = NX_MAX_FAULTS; /* Reset for the next chunk */

		inlen     = inlen - srclen;
		srcbuf    = srcbuf + srclen;
		srctotlen = srctotlen + srclen;

		/* Two possible locations for spbc depending on the function
		 * code.
		 */
		spbc = (!lzcounts) ? get32(cmdp->cpb, out_spbc_comp) :
			get32(cmdp->cpb, out_spbc_comp_with_count);
		assert(spbc == srclen);

		/* Target byte count */
		tpbc = get32(cmdp->crb.csb, tpbc);
		/* Target ending bit count */
		tebc = getnn(cmdp->cpb, out_tebc);
		NXPRT(fprintf(stderr, "compressed chunk %d ", spbc));
		NXPRT(fprintf(stderr, "to %d bytes, tebc= %d\n", tpbc, tebc));

		if (inlen > 0) { /* More chunks to go */
			set_bfinal(dstbuf, 0);
			dstbuf    = dstbuf + tpbc;
			dsttotlen = dsttotlen + tpbc;
			outlen    = outlen - tpbc;
			/* Round up to the next byte with a flush
			 * block; do not set the BFINAqL bit.
			 */
			flushlen  = append_sync_flush(dstbuf, tebc, 0);
			dsttotlen = dsttotlen + flushlen;
			outlen    = outlen - flushlen;
			dstbuf    = dstbuf + flushlen;
			NXPRT(fprintf(stderr, "added sync_flush %d bytes\n",
					flushlen));
		} else {  /* Done */
			/* Set the BFINAL bit of the last block per Deflate
			 * specification.
			 */
			set_bfinal(dstbuf, 1);
			dstbuf    = dstbuf + tpbc;
			dsttotlen = dsttotlen + tpbc;
			outlen    = outlen - tpbc;
		}

		/* Resuming crc32 for the next chunk */
		crc = get32(cmdp->cpb, out_crc);
		put32(cmdp->cpb, in_crc, crc);
		crc = be32toh(crc);
	}

	/* Append crc32 and ISIZE to the end */
	memcpy(dstbuf, &crc, 4);
	memcpy(dstbuf+4, &srctotlen, 4);
	dsttotlen = dsttotlen + 8;
	outlen    = outlen - 8;

	assert(FNAME_MAX > (strlen(argv[1]) + strlen(FEXT)));
	strcpy(outname, argv[1]);
	strcat(outname, FEXT);
	if (write_output_file(outname, outbuf, dsttotlen)) {
		fprintf(stderr, "write error: %s\n", outname);
		exit(-1);
	}

	fprintf(stderr, "compressed %ld to %ld bytes total, ", srctotlen,
		dsttotlen);
	fprintf(stderr, "crc32 checksum = %08x\n", crc);

	if (inbuf != NULL)
		free(inbuf);

	if (outbuf != NULL)
		free(outbuf);

	return 0;
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

	rc = compress_file(argc, argv, handle);

	nx_function_end(handle);

	return rc;
}
