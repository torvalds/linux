/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 S.F.T. Inc.
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
#include <sys/ioccom.h>
#include <sys/spigenio.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <unistd.h>

#define	DEFAULT_DEVICE_NAME	"/dev/spigen0.0"

#define	DEFAULT_BUFFER_SIZE	8192

#define	DIR_READ		0
#define	DIR_WRITE		1
#define	DIR_READWRITE		2
#define	DIR_NONE		-1

struct spi_options {
	int	mode;		/* mode (0,1,2,3, -1 == use default) */
	int	speed;		/* speed (in Hz, -1 == use default) */
	int	count;		/* count (0 through 'n' bytes, negative for
				 * stdin length) */
	int	binary;		/* non-zero for binary output or zero for
				 * ASCII output when ASCII != 0 */
	int	ASCII;		/* zero for binary input and output.
				 * non-zero for ASCII input, 'binary'
				 * determines output */
	int	lsb;		/* non-zero for LSB order (default order is
				 * MSB) */
	int	verbose;	/* non-zero for verbosity */
	int	ncmd;		/* bytes to skip for incoming data */
	uint8_t	*pcmd;		/* command data (NULL if none) */
};

static void	usage(void);
static int	interpret_command_bytes(const char *parg, struct spi_options *popt);
static void *	prep_write_buffer(struct spi_options *popt);
static int	_read_write(int hdev, void *bufw, void *bufr, int cbrw, int lsb);
static int	_do_data_output(void *pr, struct spi_options *popt);
static int	get_info(int hdev, const char *dev_name);
static int	set_mode(int hdev, struct spi_options *popt);
static int	set_speed(int hdev, struct spi_options *popt);
static int	hexval(char c);
static int	perform_read(int hdev, struct spi_options *popt);
static int	perform_write(int hdev, struct spi_options *popt);
static int	perform_readwrite(int hdev, struct spi_options *popt);
static void	verbose_dump_buffer(void *pbuf, int icount, int lsb);

/*
 * LSB array - reversebits[n] is the LSB value of n as an MSB.  Use this array
 * to obtain a reversed bit pattern of the index value when bits must
 * be sent/received in an LSB order vs the default MSB
 */
static uint8_t reversebits[256] = {
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
	0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
	0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
	0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
	0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
	0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
	0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
	0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
	0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
	0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
	0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
	0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
	0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
	0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
	0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
	0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
	0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
	0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
	0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
	0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
	0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
	0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
	0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
	0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
};


static void
usage(void)
{
	fputs(getprogname(), stderr);
	fputs(" - communicate on SPI bus with slave devices\n"
	      "Usage:\n"
	      "        spi [-f device] [-d r|w|rw] [-m mode] [-s max-speed] [-c count]\n"
	      "            [-C \"command bytes\"] [-A] [-b] [-L] [-v]\n"
	      "        spi -i [-f device] [-v]\n"
	      "        spi -h\n"
	      " where\n"
	      "        -f specifies the device (default is spigen0.0)\n"
	      "        -d specifies the operation (r, w, or rw; default is rw)\n"
	      "        -m specifies the mode (0, 1, 2, or 3)\n"
	      "        -s specifies the maximum speed (default is 0, device default)\n"
	      "        -c specifies the number of data bytes to transfer (default 0, i.e. none)\n"
	      "           A negative value uses the length of the input data\n"
	      "        -C specifies 'command bytes' to be sent, as 2 byte hexadecimal values\n"
	      "           (these should be quoted, separated by optional white space)\n"
	      "        -L specifies 'LSB' order on the SPI bus (default is MSB)\n"
	      "        -i query information about the device\n"
	      "        -A uses ASCII for input/output as 2-digit hex values\n"
	      "        -b Override output format as binary (only valid with '-A')\n"
	      "        -v verbose output\n"
	      "        -h prints this message\n"
	      "\n"
	      "NOTE:  setting the mode and/or speed is 'sticky'.  Subsequent transactions\n"
	      "       on that device will, by default, use the previously set values.\n"
	      "\n",
	      stderr);
}

int
main(int argc, char *argv[], char *envp[] __unused)
{
	struct spi_options opt;
	int err, ch, hdev, finfo, fdir;
	char *pstr;
	char dev_name[PATH_MAX * 2 + 5];

	finfo = 0;
	fdir = DIR_NONE;

	hdev = -1;
	err = 0;

	dev_name[0] = 0;

	opt.mode = -1;
	opt.speed = -1;
	opt.count = 0;
	opt.ASCII = 0;
	opt.binary = 0;
	opt.lsb = 0;
	opt.verbose = 0;
	opt.ncmd = 0;
	opt.pcmd = NULL;

	while (!err && (ch = getopt(argc, argv, "f:d:m:s:c:C:AbLvih")) != -1) {
		switch (ch) {
		case 'd':
			if (optarg[0] == 'r') {
				if (optarg[1] == 'w' && optarg[2] == 0) {
					fdir = DIR_READWRITE;
				}
				else if (optarg[1] == 0) {
					fdir = DIR_READ;
				}
			}
			else if (optarg[0] == 'w' && optarg[1] == 0) {
				fdir = DIR_WRITE;
			}
			else {
				err = 1;
			}
			break;

		case 'f':
			if (!optarg[0]) {	/* unlikely */
				fputs("error - missing device name\n", stderr);
				err = 1;
			}
			else {
				if (optarg[0] == '/')
					strlcpy(dev_name, optarg,
					    sizeof(dev_name));
				else
					snprintf(dev_name, sizeof(dev_name),
					    "/dev/%s", optarg);
			}
			break;

		case 'm':
			opt.mode = (int)strtol(optarg, &pstr, 10);

			if (!pstr || *pstr || opt.mode < 0 || opt.mode > 3) {
				fprintf(stderr, "Invalid mode specified: %s\n",
				    optarg);
				err = 1;
			}
			break;

		case 's':
			opt.speed = (int)strtol(optarg, &pstr, 10);

			if (!pstr || *pstr || opt.speed < 0) {
				fprintf(stderr, "Invalid speed specified: %s\n",
				    optarg);
				err = 1;
			}
			break;

		case 'c':
			opt.count = (int)strtol(optarg, &pstr, 10);

			if (!pstr || *pstr) {
				fprintf(stderr, "Invalid count specified: %s\n",
				    optarg);
				err = 1;
			}
			break;

		case 'C':
			if(opt.pcmd) /* specified more than once */
				err = 1;
			else {
				/* get malloc'd buffer or error */
				if (interpret_command_bytes(optarg, &opt))
					err = 1;
			}

			break;

		case 'A':
			opt.ASCII = 1;
			break;

		case 'b':
			opt.binary = 1;
			break;

		case 'L':
			opt.lsb = 1;
			break;

		case 'v':
			opt.verbose++;
			break;

		case 'i':
			finfo = 1;
			break;

		default:
			err = 1;
			/* FALLTHROUGH */
		case 'h':
			usage();
			goto the_end;
		}
	}

	argc -= optind;
	argv += optind;

	if (err ||
	    (fdir == DIR_NONE && !finfo && opt.mode == -1 && opt.speed == -1 && opt.count == 0)) {
		/*
		 * if any of the direction, mode, speed, or count not specified,
		 * print usage
		 */

		usage();
		goto the_end;
	}

	if ((opt.count != 0 || opt.ncmd != 0) && fdir == DIR_NONE) {
		/*
		 * count was specified, but direction was not.  default is
		 * read/write
		 */
		/*
		 * this includes a negative count, which implies write from
		 * stdin
		 */
		if (opt.count == 0)
			fdir = DIR_WRITE;
		else
			fdir = DIR_READWRITE;
	}

	if (opt.count < 0 && fdir != DIR_READWRITE && fdir != DIR_WRITE) {
		fprintf(stderr, "Invalid length %d when not writing data\n",
		    opt.count);

		err = 1;
		usage();
		goto the_end;
	}


	if (!dev_name[0])	/* no device name specified */
		strlcpy(dev_name, DEFAULT_DEVICE_NAME, sizeof(dev_name));

	hdev = open(dev_name, O_RDWR);

	if (hdev == -1) {
		fprintf(stderr, "Error - unable to open '%s', errno=%d\n",
		    dev_name, errno);
		err = 1;
		goto the_end;
	}

	if (finfo) {
		err = get_info(hdev, dev_name);
		goto the_end;
	}

	/* check and assign mode, speed */

	if (opt.mode != -1) {
		err = set_mode(hdev, &opt);

		if (err)
			goto the_end;
	}

	if (opt.speed != -1) {
		err = set_speed(hdev, &opt);

		if (err)
			goto the_end;
	}

	/* do data transfer */

	if (fdir == DIR_READ) {
		err = perform_read(hdev, &opt);
	}
	else if (fdir == DIR_WRITE) {
		err = perform_write(hdev, &opt);
	}
	else if (fdir == DIR_READWRITE) {
		err = perform_readwrite(hdev, &opt);
	}

the_end:

	if (hdev != -1)
		close(hdev);

	free(opt.pcmd);

	return (err);
}

static int
interpret_command_bytes(const char *parg, struct spi_options *popt)
{
	int ch, ch2, ctr, cbcmd, err;
	const char *ppos;
	void *ptemp;
	uint8_t *pcur;

	err = 0;
	cbcmd = DEFAULT_BUFFER_SIZE; /* initial cmd buffer size */
	popt->pcmd = (uint8_t *)malloc(cbcmd);

	if (!popt->pcmd)
		return 1;

	pcur = popt->pcmd;

	ctr = 0;
	ppos = parg;

	while (*ppos) {
		while (*ppos && *ppos <= ' ') {
			ppos++; /* skip (optional) leading white space */
		}

		if (!*ppos)
			break; /* I am done */

		ch = hexval(*(ppos++));
		if (ch < 0 || !*ppos) { /* must be valid pair of hex characters */
			err = 1;
			goto the_end;
		}
		
		ch2 = hexval(*(ppos++));
		if (ch2 < 0) {
			err = 1;
			goto the_end;
		}

		ch = (ch * 16 + ch2) & 0xff; /* convert to byte */

		if (ctr >= cbcmd) { /* need re-alloc buffer? (unlikely) */
			cbcmd += 8192; /* increase by additional 8k */
			ptemp = realloc(popt->pcmd, cbcmd);

			if (!ptemp) {
				err = 1;
				fprintf(stderr,
					"Not enough memory to interpret command bytes, errno=%d\n",
					errno);
				goto the_end;
			}

			popt->pcmd = (uint8_t *)ptemp;
			pcur = popt->pcmd + ctr;
		}

		if (popt->lsb)
			*pcur = reversebits[ch];
		else
			*pcur = (uint8_t)ch;

		pcur++;
		ctr++;
	}

	popt->ncmd = ctr; /* record num bytes in '-C' argument */

the_end:

	/* at this point popt->pcmd is NULL or a valid pointer */

	return err;
}

static int
get_info(int hdev, const char *dev_name)
{
	uint32_t fmode, fspeed;
	int err;
	char temp_buf[PATH_MAX], cpath[PATH_MAX];

	if (!realpath(dev_name, cpath)) /* get canonical name for info purposes */
		strlcpy(cpath, temp_buf, sizeof(cpath));  /* this shouldn't happen */

	err = ioctl(hdev, SPIGENIOC_GET_SPI_MODE, &fmode);

	if (err == 0)
		err = ioctl(hdev, SPIGENIOC_GET_CLOCK_SPEED, &fspeed);

	if (err == 0) {
		fprintf(stderr,
		        "Device name:   %s\n"
		        "Device mode:   %d\n"
		        "Device speed:  %d\n",
		        cpath, fmode, fspeed);//, max_cmd, max_data, temp_buf);
	}
	else
		fprintf(stderr, "Unable to query info (err=%d), errno=%d\n",
		    err, errno);

	return err;
}

static int
set_mode(int hdev, struct spi_options *popt)
{
	uint32_t fmode = popt->mode;

	if (popt->mode < 0)	/* use default? */
		return 0;

	return ioctl(hdev, SPIGENIOC_SET_SPI_MODE, &fmode);
}

static int
set_speed(int hdev, struct spi_options *popt)
{
	uint32_t clock_speed = popt->speed;

	if (popt->speed < 0)
		return 0;

	return ioctl(hdev, SPIGENIOC_SET_CLOCK_SPEED, &clock_speed);
}

static int
hexval(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	} else if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	} else if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return -1;
}

static void *
prep_write_buffer(struct spi_options *popt)
{
	int ch, ch2, ch3, ncmd, lsb, err;
	uint8_t *pdata, *pdat2;
	size_t cbdata, cbread;
	const char *szbytes;

	ncmd = popt->ncmd; /* num command bytes (can be zero) */

	if (ncmd == 0 && popt->count == 0)
		return NULL;	/* always since it's an error if it happens
				 * now */

	if (popt->count < 0) {
		cbdata = DEFAULT_BUFFER_SIZE;
	}
	else {
		cbdata = popt->count;
	}

	lsb = popt->lsb; /* non-zero if LSB order; else MSB */

	pdata = malloc(cbdata + ncmd + 1);
	cbread = 0;

	err = 0;

	if (!pdata)
		return NULL;

	if (popt->pcmd && ncmd > 0) {
		memcpy(pdata, popt->pcmd, ncmd); /* copy command bytes */
		pdat2 = pdata + ncmd;
	}
	else
		pdat2 = pdata; /* no prepended command data */

	/*
	 * read up to 'cbdata' bytes.  If I get an EOF, do one of two things:
	 * a) change the data count to match how many bytes I read in b) fill
	 * the rest of the input buffer with zeros
	 *
	 * If the specified length is negative, I do 'a', else 'b'
	 */

	while (!err && cbread < cbdata && (ch = fgetc(stdin)) != EOF) {
		if (popt->ASCII) {
			/* skip consecutive white space */

			while (ch <= ' ') {
				if ((ch = fgetc(stdin)) == EOF)
					break;
			}

			if (ch != EOF) {
				ch2 = hexval(ch);

				if (ch2 < 0) {
invalid_character:
					fprintf(stderr,
					    "Invalid input character '%c'\n", ch);
					err = 1;
					break;
				}

				ch = fgetc(stdin);

				if (ch != EOF) {
					ch3 = hexval(ch);

					if (ch3 < 0)
						goto invalid_character;

					ch = ch2 * 16 + ch3;
				}
			}

			if (err || ch == EOF)
				break;
		}

		/* for LSB, flip the bits - otherwise, just copy the value */
		if (lsb)
			pdat2[cbread] = reversebits[ch];
		else
			pdat2[cbread] = (uint8_t) ch;

		cbread++; /* increment num bytes read so far */
	}

	/* if it was an error, not an EOF, that ended the I/O, return NULL */

	if (err || ferror(stdin)) {
		free(pdata);
		return NULL;
	}

	if (popt->verbose > 0) {
		const char *sz_bytes;

		if (cbread != 1)
			sz_bytes = "bytes";	/* correct plurality of 'byte|bytes' */
		else
			sz_bytes = "byte";

		if (popt->ASCII)
			fprintf(stderr, "ASCII input of %zd %s\n", cbread,
			    sz_bytes);
		else
			fprintf(stderr, "Binary input of %zd %s\n", cbread,
			    sz_bytes);
	}

	/*
	 * if opt.count is negative, copy actual byte count to opt.count which does
	 * not include any of the 'command' bytes that are being sent.  Can be zero.
	 */
	if (popt->count < 0) {
		popt->count = cbread;
	}
	/*
	 * for everything else, fill the rest of the read buffer with '0'
	 * bytes, as per the standard practice for SPI
	 */
	else {
		while (cbread < cbdata)
			pdat2[cbread++] = 0;
	}

	/*
	 * popt->count bytes will be sent and read from the SPI, preceded by the
	 * 'popt->ncmd' command bytes (if any).
	 * So we must use 'popt->count' and 'popt->ncmd' from this point on in
	 * the code.
	 */

	if (popt->verbose > 0 && popt->count + popt->ncmd) {
		if ((popt->count + popt->ncmd) == 1)
			szbytes = "byte";
		else
			szbytes = "bytes";

		fprintf(stderr, "Writing %d %s to SPI device\n",
		        popt->count + popt->ncmd, szbytes);

		verbose_dump_buffer(pdata, popt->count + popt->ncmd, lsb);
	}

	return pdata;
}

static int
_read_write(int hdev, void *bufw, void *bufr, int cbrw, int lsb)
{
	int	err, ctr;
	struct spigen_transfer spi;

	if (!cbrw)
		return 0;

	if (!bufr)
		bufr = bufw;
	else
		memcpy(bufr, bufw, cbrw);	/* transaction uses bufr for
						 * both R and W */

	bzero(&spi, sizeof(spi));	/* zero structure first */

	/* spigen code seems to suggest there must be at least 1 command byte */

	spi.st_command.iov_base = bufr;
	spi.st_command.iov_len = cbrw;

	/*
	 * The remaining members for spi.st_data are zero - all bytes are
	 * 'command' for this. The driver doesn't really do anything different
	 * for 'command' vs 'data' and at least one command byte must be sent in
	 * the transaction.
	 */

	err = ioctl(hdev, SPIGENIOC_TRANSFER, &spi) < 0 ? -1 : 0;

	if (!err && lsb) {
		/* flip the bits for 'lsb' mode */
		for (ctr = 0; ctr < cbrw; ctr++) {
			((uint8_t *) bufr)[ctr] =
			    reversebits[((uint8_t *)bufr)[ctr]];
		}
	}

	if (err)
		fprintf(stderr, "Error performing SPI transaction, errno=%d\n",
		    errno);

	return err;
}

static int
_do_data_output(void *pr, struct spi_options *popt)
{
	int	err, idx, icount;
	const char *sz_bytes, *sz_byte2;
	const uint8_t *pbuf;

	pbuf = (uint8_t *)pr + popt->ncmd; /* only the data we want */
	icount = popt->count;
	err = 0;

	if (icount <= 0) {
		return -1; /* should not but could happen */
	}

	if (icount != 1)
		sz_bytes = "bytes";	/* correct plurality of 'byte|bytes' */
	else
		sz_bytes = "byte";

	if (popt->ncmd != 1)
		sz_byte2 = "bytes";
	else
		sz_byte2 = "byte";

	/* binary on stdout */
	if (popt->binary || !popt->ASCII) {
		if (popt->verbose > 0)
			fprintf(stderr, "Binary output of %d %s\n", icount,
			    sz_bytes);

		err = (int)fwrite(pbuf, 1, icount, stdout) != icount;
	}
	else if (icount > 0) {
		if (popt->verbose > 0)
			fprintf(stderr, "ASCII output of %d %s\n", icount,
			    sz_bytes);

		/* ASCII output */
		for (idx = 0; !err && idx < icount; idx++) {
			if (idx) {
				/*
				 * not the first time, insert separating space
				 */
				err = fputc(' ', stdout) == EOF;
			}

			if (!err)
				err = fprintf(stdout, "%02hhx", pbuf[idx]) < 0;
		}

		if (!err)
			err = fputc('\n', stdout) == EOF;
	}

	/* verbose text out on stderr */

	if (err)
		fprintf(stderr, "Error writing to stdout, errno=%d\n", errno);
	else if (popt->verbose > 0 && icount) {
		fprintf(stderr, 
		    "%d command %s and %d data %s read from SPI device\n",
		    popt->ncmd, sz_byte2, icount, sz_bytes);

		/* verbose output will show the command bytes as well */
		verbose_dump_buffer(pr, icount + popt->ncmd, popt->lsb);
	}

	return err;
}

static int
perform_read(int hdev, struct spi_options *popt)
{
	int icount, err;
	void   *pr, *pw;

	pr = NULL;
	icount = popt->count + popt->ncmd;

	/* prep write buffer filled with 0 bytes */
	pw = malloc(icount);

	if (!pw) {
		err = -1;
		goto the_end;
	}

	bzero(pw, icount);

	/* if I included a command sequence, copy bytes to the write buf */
	if (popt->pcmd && popt->ncmd > 0)
		memcpy(pw, popt->pcmd, popt->ncmd);

	pr = malloc(icount + 1);

	if (!pr) {
		err = -2;
		goto the_end;
	}

	bzero(pr, icount);

	err = _read_write(hdev, pw, pr, icount, popt->lsb);

	if (!err && popt->count > 0)
		err = _do_data_output(pr, popt);

the_end:

	free(pr);
	free(pw);

	return err;
}

static int
perform_write(int hdev, struct spi_options *popt)
{
	int err;
	void   *pw;

	/* read data from cmd buf and stdin and write to 'write' buffer */

	pw = prep_write_buffer(popt);

	if (!pw) {
		err = -1;
		goto the_end;
	}

	err = _read_write(hdev, pw, NULL, popt->count + popt->ncmd, popt->lsb);

the_end:

	free(pw);

	return err;
}

static int
perform_readwrite(int hdev, struct spi_options *popt)
{
	int icount, err;
	void   *pr, *pw;

	pr = NULL;

	pw = prep_write_buffer(popt);
	icount = popt->count + popt->ncmd; /* assign after fn call */

	if (!pw) {
		err = -1;
		goto the_end;
	}

	pr = malloc(icount + 1);

	if (!pr) {
		err = -2;
		goto the_end;
	}

	bzero(pr, icount);

	err = _read_write(hdev, pw, pr, icount, popt->lsb);

	if (!err)
		err = _do_data_output(pr, popt);

the_end:

	free(pr);
	free(pw);

	return err;
}


static void
verbose_dump_buffer(void *pbuf, int icount, int lsb)
{
	uint8_t	ch;
	int	ictr, ictr2, idx;

	fputs("        |  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F "
	      "|                  |\n", stderr);

	for (ictr = 0; ictr < icount; ictr += 16) {
		fprintf(stderr, " %6x | ", ictr & 0xfffff0);

		for (ictr2 = 0; ictr2 < 16; ictr2++) {
			idx = ictr + ictr2;

			if (idx < icount) {
				ch = ((uint8_t *) pbuf)[idx];

				if (lsb)
					ch = reversebits[ch];

				fprintf(stderr, "%02hhx ", ch);
			}
			else {
				fputs("   ", stderr);
			}
		}

		fputs("| ", stderr);

		for (ictr2 = 0; ictr2 < 16; ictr2++) {
			idx = ictr + ictr2;

			if (idx < icount) {
				ch = ((uint8_t *) pbuf)[idx];

				if (lsb)
					ch = reversebits[ch];

				if (ch < ' ' || ch > 127)
					goto out_of_range;

				fprintf(stderr, "%c", ch);
			}
			else if (idx < icount) {
		out_of_range:
				fputc('.', stderr);
			}
			else {
				fputc(' ', stderr);
			}
		}

		fputs(" |\n", stderr);
	}

	fflush(stderr);
}
