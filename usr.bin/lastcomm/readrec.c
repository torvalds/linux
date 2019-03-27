/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Diomidis Spinellis
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/acct.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int	 readrec_forward(FILE *f, struct acctv3 *av2);
int	 readrec_backward(FILE *f, struct acctv3 *av2);

/*
 * Reverse offsetof: return the offset of field f
 * from the end of the structure s.
 */
#define roffsetof(s, f) (sizeof(s) - offsetof(s, f))

/*
 * Read exactly one record of size size from stream f into ptr.
 * Failure to read the complete record is considered a file format error,
 * and will set errno to EFTYPE.
 * Return 0 on success, EOF on end of file or error.
 */
static int
fread_record(void *ptr, size_t size, FILE *f)
{
	size_t rv;

	if ((rv = fread(ptr, 1, size, f)) == size)
		return (0);
	else if (ferror(f) || rv == 0)
		return (EOF);
	else {
		/* Short read. */
		errno = EFTYPE;
		return (EOF);
	}
}

/*
 * Return the value of a comp_t field.
 */
static float
decode_comp(comp_t v)
{
	int result, exp;

	result = v & 017777;
	for (exp = v >> 13; exp; exp--)
		result <<= 3;
	return ((double)result / AHZV1);
}

/*
 * Read a v1 accounting record stored at the current
 * position of stream f.
 * Convert the data to the current record format.
 * Return EOF on error or end-of-file.
 */
static int
readrec_v1(FILE *f, struct acctv3 *av3)
{
	struct acctv1 av1;
	int rv;

	if ((rv = fread_record(&av1, sizeof(av1), f)) == EOF)
		return (EOF);
	av3->ac_zero = 0;
	av3->ac_version = 3;
	av3->ac_len = av3->ac_len2 = sizeof(*av3);
	memcpy(av3->ac_comm, av1.ac_comm, AC_COMM_LEN);
	av3->ac_utime = decode_comp(av1.ac_utime) * 1000000;
	av3->ac_stime = decode_comp(av1.ac_stime) * 1000000;
	av3->ac_etime = decode_comp(av1.ac_etime) * 1000000;
	av3->ac_btime = av1.ac_btime;
	av3->ac_uid = av1.ac_uid;
	av3->ac_gid = av1.ac_gid;
	av3->ac_mem = av1.ac_mem;
	av3->ac_io = decode_comp(av1.ac_io);
	av3->ac_tty = av1.ac_tty;
	av3->ac_flagx = av1.ac_flag | ANVER;
	return (0);
}

/*
 * Read an v2 accounting record stored at the current
 * position of stream f.
 * Return EOF on error or end-of-file.
 */
static int
readrec_v2(FILE *f, struct acctv3 *av3)
{
	struct acctv2 av2;
	int rv;

	if ((rv = fread_record(&av2, sizeof(av2), f)) == EOF)
		return (EOF);
	av3->ac_zero = 0;
	av3->ac_version = 3;
	av3->ac_len = av3->ac_len2 = sizeof(*av3);
	memcpy(av3->ac_comm, av2.ac_comm, AC_COMM_LEN);
	av3->ac_utime = av2.ac_utime;
	av3->ac_stime = av2.ac_stime;
	av3->ac_etime = av2.ac_etime;
	av3->ac_btime = av2.ac_btime;
	av3->ac_uid = av2.ac_uid;
	av3->ac_gid = av2.ac_gid;
	av3->ac_mem = av2.ac_mem;
	av3->ac_io = av2.ac_io;
	av3->ac_tty = av2.ac_tty;
	av3->ac_flagx = av2.ac_flagx;
	return (0);
}

/*
 * Read an v2 accounting record stored at the current
 * position of stream f.
 * Return EOF on error or end-of-file.
 */
static int
readrec_v3(FILE *f, struct acctv3 *av3)
{

	return (fread_record(av3, sizeof(*av3), f));
}

/*
 * Read a new-style (post-v1) accounting record stored at
 * the current position of stream f.
 * Convert the data to the current record format.
 * Return EOF on error or end-of-file.
 */
static int
readrec_vx(FILE *f, struct acctv3 *av3)
{
	uint8_t magic, version;

	if (fread_record(&magic, sizeof(magic), f) == EOF ||
	    fread_record(&version, sizeof(version), f) == EOF ||
	    ungetc(version, f) == EOF ||
	    ungetc(magic, f) == EOF)
		return (EOF);
	switch (version) {
	case 2:
		return (readrec_v2(f, av3));
	case 3:
		return (readrec_v3(f, av3));

	/* Add handling for more versions here. */

	default:
		errno = EFTYPE;
		return (EOF);
	}
}

/*
 * Read an accounting record stored at the current
 * position of stream f.
 * Old-format records are converted to the current record
 * format.
 * Return the number of records read (1 or 0 at the end-of-file),
 * or EOF on error.
 */
int
readrec_forward(FILE *f, struct acctv3 *av3)
{
	int magic, rv;

	if ((magic = getc(f)) == EOF)
		return (ferror(f) ? EOF : 0);
	if (ungetc(magic, f) == EOF)
		return (EOF);
	if (magic != 0)
		/* Old record format. */
		rv = readrec_v1(f, av3);
	else
		/* New record formats. */
		rv = readrec_vx(f, av3);
	return (rv == EOF ? EOF : 1);
}

/*
 * Read an accounting record ending at the current
 * position of stream f.
 * Old-format records are converted to the current record
 * format.
 * The file pointer is positioned at the beginning of the
 * record read.
 * Return the number of records read (1 or 0 at the end-of-file),
 * or EOF on error.
 */
int
readrec_backward(FILE *f, struct acctv3 *av3)
{
	off_t pos;
	int c;
	uint16_t len;

	if ((pos = ftell(f)) == -1)
		return (EOF);
	if (pos == 0)
		return (0);
	if (fseek(f, -roffsetof(struct acctv3, ac_trailer),
	    SEEK_CUR) == EOF ||
	    (c = getc(f)) == EOF)
		return (EOF);
	if (c & ANVER) {
		/*
		 * New record formats.  For v2 and v3 offset from the
		 * end for ac_len2 should be same.
		 */
		if (fseeko(f, pos - roffsetof(struct acctv2, ac_len2),
		    SEEK_SET) == EOF ||
		    fread_record(&len, sizeof(len), f) == EOF ||
		    fseeko(f, pos - len, SEEK_SET) == EOF ||
		    readrec_vx(f, av3) == EOF ||
		    fseeko(f, pos - len, SEEK_SET) == EOF)
			return (EOF);
		else
			return (1);
	} else {
		/* Old record format. */
		if (fseeko(f, pos - sizeof(struct acctv1), SEEK_SET) == EOF ||
		    readrec_v1(f, av3) == EOF ||
		    fseeko(f, pos - sizeof(struct acctv1), SEEK_SET) == EOF)
			return (EOF);
		else
			return (1);
	}
}
