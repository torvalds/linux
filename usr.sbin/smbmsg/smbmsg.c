/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2004 Joerg Wunsch
 * All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Send or receive messages over an SMBus.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/ioctl.h>

#include <dev/smbus/smb.h>

#include "pathnames.h"

static const char *dev = PATH_DEFAULTSMBDEV;
static const char *bytefmt = "0x%02x";
static const char *wordfmt = "0x%04x";
static const char *fmt;

static int fd;			/* file descriptor for /dev/smbX */
static int cflag = -1;		/* SMBus cmd */
static int iflag = -1;		/* input data */
static int oflag = -1;		/* output data */
static int pflag;		/* probe bus */
static int slave = -1;		/* slave address */
static int wflag;		/* word IO */

static unsigned char ibuf[SMB_MAXBLOCKSIZE];
static unsigned char obuf[SMB_MAXBLOCKSIZE];
static unsigned short oword;

/*
 * The I2C specs say that all addresses below 16 and above or equal
 * 240 are reserved.  Address 0 is the global address, but we do not
 * care for this detail.
 */
#define MIN_I2C_ADDR	16
#define MAX_I2C_ADDR	240

static int	do_io(void);
static int	getnum(const char *s);
static void	probe_i2c(void);
static void	usage(void);

static void
usage(void)
{
	fprintf(stderr,
		"usage: smbmsg [-f dev] -p\n"
		"       smbmsg [-f dev] -s slave [-F fmt] [-c cmd] [-w] "
		"[-i incnt] [-o outcnt] [outdata ...]\n");
	exit(EX_USAGE);
}

static int
getnum(const char *s)
{
	char *endp;
	unsigned long l;

	l = strtoul(s, &endp, 0);
	if (*s != '\0' && *endp == '\0')
		return (int)l;
	return (-1);
}

static void
probe_i2c(void)
{
	unsigned char addr;
	int flags;
#define IS_READABLE	1
#define IS_WRITEABLE	2
	struct smbcmd c;

	printf("Probing for devices on %s:\n", dev);

	for (addr = MIN_I2C_ADDR; addr < MAX_I2C_ADDR; addr += 2) {
		c.slave = addr;
		flags = 0;
		if (ioctl(fd, SMB_RECVB, &c) != -1)
			flags = IS_READABLE;
		if (ioctl(fd, SMB_QUICK_WRITE, &c) != -1)
			flags |= IS_WRITEABLE;
		if (flags != 0) {
			printf("Device @0x%02x: ", addr);
			if (flags & IS_READABLE)
				putchar('r');
			if (flags & IS_WRITEABLE)
				putchar('w');
			putchar('\n');
		}
	}
}

static int
do_io(void)
{
	struct smbcmd c;
	int i;

	c.slave = slave;
	c.cmd = cflag;
	c.rcount = 0;
	c.wcount = 0;

	if (fmt == NULL && iflag > 0)
		fmt = wflag? wordfmt: bytefmt;

	if (cflag == -1) {
		/* operations that do not require a command byte */
		if (iflag == -1 && oflag == 0)
			/* 0 bytes output: quick write operation */
			return (ioctl(fd, SMB_QUICK_WRITE, &c));
		else if (iflag == 0 && oflag == -1)
			/* 0 bytes input: quick read operation */
			return (ioctl(fd, SMB_QUICK_READ, &c));
		else if (iflag == 1 && oflag == -1) {
			/* no command, 1 byte input: receive byte op. */
			if (ioctl(fd, SMB_RECVB, &c) == -1)
				return (-1);
			printf(fmt, (unsigned char)c.cmd);
			putchar('\n');
			return (0);
		} else if (iflag == -1 && oflag == 1) {
			/* no command, 1 byte output: send byte op. */
			c.cmd = obuf[0];
			return (ioctl(fd, SMB_SENDB, &c));
		} else
			return (-2);
	}
	if (iflag == 1 && oflag == -1) {
		/* command + 1 byte input: read byte op. */
		if (ioctl(fd, SMB_READB, &c) == -1)
			return (-1);
		printf(fmt, (unsigned char)c.rdata.byte);
		putchar('\n');
		return (0);
	} else if (iflag == -1 && oflag == 1) {
		/* command + 1 byte output: write byte op. */
		c.wdata.byte = obuf[0];
		return (ioctl(fd, SMB_WRITEB, &c));
	} else if (wflag && iflag == 2 && oflag == -1) {
		/* command + 2 bytes input: read word op. */
		if (ioctl(fd, SMB_READW, &c) == -1)
			return (-1);
		printf(fmt, (unsigned short)c.rdata.word);
		putchar('\n');
		return (0);
	} else if (wflag && iflag == -1 && oflag == 2) {
		/* command + 2 bytes output: write word op. */
		c.wdata.word = oword;
		return (ioctl(fd, SMB_WRITEW, &c));
	} else if (wflag && iflag == 2 && oflag == 2) {
		/*
		 * command + 2 bytes output + 2 bytes input:
		 * "process call" op.
		 */
		c.wdata.word = oword;
		if (ioctl(fd, SMB_PCALL, &c) == -1)
			return (-1);
		printf(fmt, (unsigned short)c.rdata.word);
		putchar('\n');
		return (0);
	} else if (iflag > 1 && oflag == -1) {
		/* command + > 1 bytes of input: block read */
		c.rbuf = ibuf;
		c.rcount = iflag;
		if (ioctl(fd, SMB_BREAD, &c) == -1)
			return (-1);
		for (i = 0; i < c.rcount; i++) {
			if (i != 0)
				putchar(' ');
			printf(fmt, ibuf[i]);
		}
		putchar('\n');
		return (0);
	} else if (iflag == -1 && oflag > 1) {
		/* command + > 1 bytes of output: block write */
		c.wbuf = obuf;
		c.wcount = oflag;
		return (ioctl(fd, SMB_BWRITE, &c));
	}

	return (-2);
}


int
main(int argc, char **argv)
{
	int i, n, errs = 0;
	int savederrno;

	while ((i = getopt(argc, argv, "F:c:f:i:o:ps:w")) != -1)
		switch (i) {
		case 'F':
			fmt = optarg;
			break;

		case 'c':
			if ((cflag = getnum(optarg)) == -1)
				errx(EX_USAGE, "Invalid number: %s", optarg);
			if (cflag < 0 || cflag >= 256)
				errx(EX_USAGE,
				     "CMD out of range: %d",
				     cflag);
			break;

		case 'f':
			dev = optarg;
			break;

		case 'i':
			if ((iflag = getnum(optarg)) == -1)
				errx(EX_USAGE, "Invalid number: %s", optarg);
			if (iflag < 0 || iflag > SMB_MAXBLOCKSIZE)
				errx(EX_USAGE,
				     "# input bytes out of range: %d",
				     iflag);
			break;

		case 'o':
			if ((oflag = getnum(optarg)) == -1)
				errx(EX_USAGE, "Invalid number: %s", optarg);
			if (oflag < 0 || oflag > SMB_MAXBLOCKSIZE)
				errx(EX_USAGE,
				     "# output bytes out of range: %d",
				     oflag);
			break;

		case 'p':
			pflag = 1;
			break;

		case 's':
			if ((slave = getnum(optarg)) == -1)
				errx(EX_USAGE, "Invalid number: %s", optarg);

			if (slave < MIN_I2C_ADDR || slave >= MAX_I2C_ADDR)
				errx(EX_USAGE,
				     "Slave address out of range: %d",
				     slave);
			break;

		case 'w':
			wflag = 1;
			break;

		default:
			errs++;
		}
	argc -= optind;
	argv += optind;
	if (errs || (slave != -1 && pflag) || (slave == -1 && !pflag))
		usage();
	if (wflag &&
	    !((iflag == 2 && oflag == -1) ||
	      (iflag == -1 && oflag == 2) ||
	      (iflag == 2 && oflag == 2)))
		errx(EX_USAGE, "Illegal # IO bytes for word IO");
	if (!pflag && iflag == -1 && oflag == -1)
		errx(EX_USAGE, "Nothing to do");
	if (pflag && (cflag != -1 || iflag != -1 || oflag != -1 || wflag != 0))
		usage();
	if (oflag > 0) {
		if (oflag == 2 && wflag) {
			if (argc == 0)
				errx(EX_USAGE, "Too few arguments for -o count");
			if ((n = getnum(*argv)) == -1)
				errx(EX_USAGE, "Invalid number: %s", *argv);
			if (n < 0 || n >= 65535)
				errx(EX_USAGE, "Value out of range: %d", n);
			oword = n;
			argc--;
			argv++;
		} else for (i = 0; i < oflag; i++, argv++, argc--) {
			if (argc == 0)
				errx(EX_USAGE, "Too few arguments for -o count");
			if ((n = getnum(*argv)) == -1)
				errx(EX_USAGE, "Invalid number: %s", *argv);
			if (n < 0 || n >= 256)
				errx(EX_USAGE, "Value out of range: %d", n);
			obuf[i] = n;
		}
	}
	if (argc != 0)
		usage();

	if ((fd = open(dev, O_RDWR)) == -1)
		err(EX_UNAVAILABLE, "Cannot open %s", dev);

	i = 0;
	if (pflag)
		probe_i2c();
	else
		i = do_io();

	savederrno = errno;
	close(fd);
	errno = savederrno;

	if (i == -1)
		err(EX_UNAVAILABLE, "Error performing SMBus IO");
	else if (i == -2)
		errx(EX_USAGE, "Invalid option combination");

	return (0);
}
