/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008-2009 Semihalf, Michal Hajduk and Bartlomiej Sieka
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <err.h>
#include <errno.h>
#include <sysexits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <dev/iicbus/iic.h>

#define	I2C_DEV			"/dev/iic0"
#define	I2C_MODE_NOTSET		0
#define	I2C_MODE_NONE		1
#define	I2C_MODE_STOP_START	2
#define	I2C_MODE_REPEATED_START	3

struct options {
	int	width;
	int	count;
	int	verbose;
	int	addr_set;
	int	binary;
	int	scan;
	int	skip;
	int	reset;
	int	mode;
	char	dir;
	uint32_t	addr;
	uint32_t	off;
};

struct skip_range {
	int	start;
	int	end;
};

__dead2 static void
usage(void)
{

	fprintf(stderr, "usage: %s -a addr [-f device] [-d [r|w]] [-o offset] "
	    "[-w [0|8|16]] [-c count] [-m [ss|rs|no]] [-b] [-v]\n",
	    getprogname());
	fprintf(stderr, "       %s -s [-f device] [-n skip_addr] -v\n",
	    getprogname());
	fprintf(stderr, "       %s -r [-f device] -v\n", getprogname());
	exit(EX_USAGE);
}

static struct skip_range
skip_get_range(char *skip_addr)
{
	struct skip_range addr_range;
	char *token;

	addr_range.start = 0;
	addr_range.end = 0;

	token = strsep(&skip_addr, "..");
	if (token) {
		addr_range.start = strtoul(token, 0, 16);
		token = strsep(&skip_addr, "..");
		if ((token != NULL) && !atoi(token)) {
			token = strsep(&skip_addr, "..");
			if (token)
				addr_range.end = strtoul(token, 0, 16);
		}
	}

	return (addr_range);
}

/* Parse the string to get hex 7 bits addresses */
static int
skip_get_tokens(char *skip_addr, int *sk_addr, int max_index)
{
	char *token;
	int i;

	for (i = 0; i < max_index; i++) {
		token = strsep(&skip_addr, ":");
		if (token == NULL)
			break;
		sk_addr[i] = strtoul(token, 0, 16);
	}
	return (i);
}

static int
scan_bus(struct iiccmd cmd, char *dev, int skip, char *skip_addr)
{
	struct iic_msg rdmsg;
	struct iic_rdwr_data rdwrdata;
	struct skip_range addr_range = { 0, 0 };
	int *tokens, fd, error, i, index, j;
	int len = 0, do_skip = 0, no_range = 1, num_found = 0, use_read_xfer = 0;
	uint8_t rdbyte;

	fd = open(dev, O_RDWR);
	if (fd == -1) {
		fprintf(stderr, "Error opening I2C controller (%s) for "
		    "scanning: %s\n", dev, strerror(errno));
		return (EX_NOINPUT);
	}

	if (skip) {
		len = strlen(skip_addr);
		if (strstr(skip_addr, "..") != NULL) {
			addr_range = skip_get_range(skip_addr);
			no_range = 0;
		} else {
			tokens = (int *)malloc((len / 2 + 1) * sizeof(int));
			if (tokens == NULL) {
				fprintf(stderr, "Error allocating tokens "
				    "buffer\n");
				error = -1;
				goto out;
			}
			index = skip_get_tokens(skip_addr, tokens,
			    len / 2 + 1);
		}

		if (!no_range && (addr_range.start > addr_range.end)) {
			fprintf(stderr, "Skip address out of range\n");
			error = -1;
			goto out;
		}
	}

	printf("Scanning I2C devices on %s: ", dev);

start_over:
	if (use_read_xfer) {
		fprintf(stderr, 
		    "Hardware may not support START/STOP scanning; "
		    "trying less-reliable read method.\n");
	}

	for (i = 1; i < 127; i++) {

		if (skip && ( addr_range.start < addr_range.end)) {
			if (i >= addr_range.start && i <= addr_range.end)
				continue;

		} else if (skip && no_range)
			for (j = 0; j < index; j++) {
				if (tokens[j] == i) {
					do_skip = 1;
					break;
				}
			}

		if (do_skip) {
			do_skip = 0;
			continue;
		}

		cmd.slave = i << 1;
		cmd.last = 1;
		cmd.count = 0;
		error = ioctl(fd, I2CRSTCARD, &cmd);
		if (error) {
			fprintf(stderr, "Controller reset failed\n");
			goto out;
		}
		if (use_read_xfer) {
			rdmsg.buf = &rdbyte;
			rdmsg.len = 1;
			rdmsg.flags = IIC_M_RD;
			rdmsg.slave = i << 1;
			rdwrdata.msgs = &rdmsg;
			rdwrdata.nmsgs = 1;
			error = ioctl(fd, I2CRDWR, &rdwrdata);
		} else {
			cmd.slave = i << 1;
			cmd.last = 1;
			error = ioctl(fd, I2CSTART, &cmd);
			if (errno == ENODEV || errno == EOPNOTSUPP) {
				/* If START not supported try reading. */
				use_read_xfer = 1;
				goto start_over;
			}
			ioctl(fd, I2CSTOP);
		}
		if (error == 0) {
			++num_found;
			printf("%02x ", i);
		}
	}

	/*
	 * If we found nothing, maybe START is not supported and returns a
	 * generic error code such as EIO or ENXIO, so try again using reads.
	 */
	if (num_found == 0) {
		if (!use_read_xfer) {
			use_read_xfer = 1;
			goto start_over;
		}
		printf("<none found>");
	}
	printf("\n");

	error = ioctl(fd, I2CRSTCARD, &cmd);
out:
	close(fd);
	if (skip && no_range)
		free(tokens);

	if (error) {
		fprintf(stderr, "Error scanning I2C controller (%s): %s\n",
		    dev, strerror(errno));
		return (EX_NOINPUT);
	} else
		return (EX_OK);
}

static int
reset_bus(struct iiccmd cmd, char *dev)
{
	int fd, error;

	fd = open(dev, O_RDWR);
	if (fd == -1) {
		fprintf(stderr, "Error opening I2C controller (%s) for "
		    "resetting: %s\n", dev, strerror(errno));
		return (EX_NOINPUT);
	}

	printf("Resetting I2C controller on %s: ", dev);
	error = ioctl(fd, I2CRSTCARD, &cmd);
	close (fd);

	if (error) {
		printf("error: %s\n", strerror(errno));
		return (EX_IOERR);
	} else {
		printf("OK\n");
		return (EX_OK);
	}
}

static char *
prepare_buf(int size, uint32_t off)
{
	char *buf;

	buf = malloc(size);
	if (buf == NULL)
		return (buf);

	if (size == 1)
		buf[0] = off & 0xff;
	else if (size == 2) {
		buf[0] = (off >> 8) & 0xff;
		buf[1] = off & 0xff;
	}

	return (buf);
}

static int
i2c_write(char *dev, struct options i2c_opt, char *i2c_buf)
{
	struct iiccmd cmd;
	int ch, i, error, fd, bufsize;
	char *err_msg, *buf;

	/*
	 * Read data to be written to the chip from stdin
	 */
	if (i2c_opt.verbose && !i2c_opt.binary)
		fprintf(stderr, "Enter %u bytes of data: ", i2c_opt.count);

	for (i = 0; i < i2c_opt.count; i++) {
		ch = getchar();
		if (ch == EOF) {
			free(i2c_buf);
			err(1, "not enough data, exiting\n");
		}
		i2c_buf[i] = ch;
	}

	fd = open(dev, O_RDWR);
	if (fd == -1) {
		free(i2c_buf);
		err(1, "open failed");
	}

	cmd.slave = i2c_opt.addr;
	error = ioctl(fd, I2CSTART, &cmd);
	if (error == -1) {
		err_msg = "ioctl: error sending start condition";
		goto err1;
	}

	if (i2c_opt.width) {
		bufsize = i2c_opt.width / 8;
		buf = prepare_buf(bufsize, i2c_opt.off);
		if (buf == NULL) {
			err_msg = "error: offset malloc";
			goto err1;
		}
	} else {
		bufsize = 0;
		buf = NULL;
	}

	switch(i2c_opt.mode) {
	case I2C_MODE_STOP_START:
		/*
		 * Write offset where the data will go
		 */
		if (i2c_opt.width) {
			cmd.count = bufsize;
			cmd.buf = buf;
			error = ioctl(fd, I2CWRITE, &cmd);
			free(buf);
			if (error == -1) {
				err_msg = "ioctl: error writing offset";
				goto err1;
			}
		}

		error = ioctl(fd, I2CSTOP);
		if (error == -1) {
			err_msg = "ioctl: error sending stop condition";
			goto err2;
		}
		cmd.slave = i2c_opt.addr;
		error = ioctl(fd, I2CSTART, &cmd);
		if (error == -1) {
			err_msg = "ioctl: error sending start condition";
			goto err1;
		}

		/*
		 * Write the data
		 */
		cmd.count = i2c_opt.count;
		cmd.buf = i2c_buf;
		cmd.last = 0;
		error = ioctl(fd, I2CWRITE, &cmd);
		if (error == -1) {
			err_msg = "ioctl: error writing";
			goto err1;
		}
		break;

	case I2C_MODE_REPEATED_START:
		/*
		 * Write offset where the data will go
		 */
		if (i2c_opt.width) {
			cmd.count = bufsize;
			cmd.buf = buf;
			error = ioctl(fd, I2CWRITE, &cmd);
			free(buf);
			if (error == -1) {
				err_msg = "ioctl: error writing offset";
				goto err1;
			}
		}

		cmd.slave = i2c_opt.addr;
		error = ioctl(fd, I2CRPTSTART, &cmd);
		if (error == -1) {
			err_msg = "ioctl: error sending repeated start "
			    "condition";
			goto err1;
		}

		/*
		 * Write the data
		 */
		cmd.count = i2c_opt.count;
		cmd.buf = i2c_buf;
		cmd.last = 0;
		error = ioctl(fd, I2CWRITE, &cmd);
		if (error == -1) {
			err_msg = "ioctl: error writing";
			goto err1;
		}
		break;

	case I2C_MODE_NONE: /* fall through */
	default:		
		buf = realloc(buf, bufsize + i2c_opt.count);
		if (buf == NULL) {
			err_msg = "error: data malloc";
			goto err1;
		}

		memcpy(buf + bufsize, i2c_buf, i2c_opt.count);
		/*
		 * Write offset and data
		 */
		cmd.count = bufsize + i2c_opt.count;
		cmd.buf = buf;
		cmd.last = 0;
		error = ioctl(fd, I2CWRITE, &cmd);
		free(buf);
		if (error == -1) {
			err_msg = "ioctl: error writing";
			goto err1;
		}
		break;
	}
	error = ioctl(fd, I2CSTOP);
	if (error == -1) {
		err_msg = "ioctl: error sending stop condition";
		goto err2;
	}

	close(fd);
	return (0);

err1:
	error = ioctl(fd, I2CSTOP);
	if (error == -1)
		fprintf(stderr, "error sending stop condition\n");
err2:
	if (err_msg)
		fprintf(stderr, "%s\n", err_msg);

	close(fd);
	return (1);
}

static int
i2c_read(char *dev, struct options i2c_opt, char *i2c_buf)
{
	struct iiccmd cmd;
	int fd, error, bufsize;
	char *err_msg, data = 0, *buf;

	fd = open(dev, O_RDWR);
	if (fd == -1)
		err(1, "open failed");

	bzero(&cmd, sizeof(cmd));

	if (i2c_opt.width) {
		cmd.slave = i2c_opt.addr;
		cmd.count = 1;
		cmd.last = 0;
		cmd.buf = &data;
		error = ioctl(fd, I2CSTART, &cmd);
		if (error == -1) {
			err_msg = "ioctl: error sending start condition";
			goto err1;
		}
		bufsize = i2c_opt.width / 8;
		buf = prepare_buf(bufsize, i2c_opt.off);
		if (buf == NULL) {
			err_msg = "error: offset malloc";
			goto err1;
		}

		cmd.count = bufsize;
		cmd.buf = buf;
		cmd.last = 0;
		error = ioctl(fd, I2CWRITE, &cmd);
		free(buf);
		if (error == -1) {
			err_msg = "ioctl: error writing offset";
			goto err1;
		}

		if (i2c_opt.mode == I2C_MODE_STOP_START) {
			error = ioctl(fd, I2CSTOP);
			if (error == -1) {
				err_msg = "error sending stop condition";
				goto err2;
			}
		}
	}
	cmd.slave = i2c_opt.addr | 1;
	cmd.count = 1;
	cmd.last = 0;
	cmd.buf = &data;
	if (i2c_opt.mode == I2C_MODE_STOP_START || i2c_opt.width == 0) {
		error = ioctl(fd, I2CSTART, &cmd);
		if (error == -1) {
			err_msg = "ioctl: error sending start condition";
			goto err2;
		}
	} else if (i2c_opt.mode == I2C_MODE_REPEATED_START) {
		error = ioctl(fd, I2CRPTSTART, &cmd);
		if (error == -1) {
			err_msg = "ioctl: error sending repeated start "
			    "condition";
			goto err1;
		}
	}

	cmd.count = i2c_opt.count;
	cmd.buf = i2c_buf;
	cmd.last = 1;
	error = ioctl(fd, I2CREAD, &cmd);
	if (error == -1) {
		err_msg = "ioctl: error while reading";
		goto err1;
	}

	error = ioctl(fd, I2CSTOP);
	if (error == -1) {
		err_msg = "error sending stop condtion\n";
		goto err2;
	}

	close(fd);
	return (0);

err1:
	error = ioctl(fd, I2CSTOP);
	if (error == -1)
		fprintf(stderr, "error sending stop condition\n");
err2:
	if (err_msg)
		fprintf(stderr, "%s\n", err_msg);

	close(fd);
	return (1);
}

int
main(int argc, char** argv)
{
	struct iiccmd cmd;
	struct options i2c_opt;
	char *dev, *skip_addr, *i2c_buf;
	int error, chunk_size, i, j, ch;

	errno = 0;
	error = 0;

	/* Line-break the output every chunk_size bytes */
	chunk_size = 16;

	dev = I2C_DEV;

	/* Default values */
	i2c_opt.addr_set = 0;
	i2c_opt.off = 0;
	i2c_opt.verbose = 0;
	i2c_opt.dir = 'r';	/* direction = read */
	i2c_opt.width = 8;
	i2c_opt.count = 1;
	i2c_opt.binary = 0;	/* ASCII text output */
	i2c_opt.scan = 0;	/* no bus scan */
	i2c_opt.skip = 0;	/* scan all addresses */
	i2c_opt.reset = 0;	/* no bus reset */
	i2c_opt.mode = I2C_MODE_NOTSET;

	while ((ch = getopt(argc, argv, "a:f:d:o:w:c:m:n:sbvrh")) != -1) {
		switch(ch) {
		case 'a':
			i2c_opt.addr = (strtoul(optarg, 0, 16) << 1);
			if (i2c_opt.addr == 0 && errno == EINVAL)
				i2c_opt.addr_set = 0;
			else
				i2c_opt.addr_set = 1;
			break;
		case 'f':
			dev = optarg;
			break;
		case 'd':
			i2c_opt.dir = optarg[0];
			break;
		case 'o':
			i2c_opt.off = strtoul(optarg, 0, 16);
			if (i2c_opt.off == 0 && errno == EINVAL)
				error = 1;
			break;
		case 'w':
			i2c_opt.width = atoi(optarg);
			break;
		case 'c':
			i2c_opt.count = atoi(optarg);
			break;
		case 'm':
			if (!strcmp(optarg, "no"))
				i2c_opt.mode = I2C_MODE_NONE;
			else if (!strcmp(optarg, "ss"))
				i2c_opt.mode = I2C_MODE_STOP_START;
			else if (!strcmp(optarg, "rs"))
				i2c_opt.mode = I2C_MODE_REPEATED_START;
			else
				usage();
			break;
		case 'n':
			i2c_opt.skip = 1;
			skip_addr = optarg;
			break;
		case 's':
			i2c_opt.scan = 1;
			break;
		case 'b':
			i2c_opt.binary = 1;
			break;
		case 'v':
			i2c_opt.verbose = 1;
			break;
		case 'r':
			i2c_opt.reset = 1;
			break;
		case 'h':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* Set default mode if option -m is not specified */
	if (i2c_opt.mode == I2C_MODE_NOTSET) {
		if (i2c_opt.dir == 'r')
			i2c_opt.mode = I2C_MODE_STOP_START;
		else if (i2c_opt.dir == 'w')
			i2c_opt.mode = I2C_MODE_NONE;
	}

	/* Basic sanity check of command line arguments */
	if (i2c_opt.scan) {
		if (i2c_opt.addr_set)
			usage();
	} else if (i2c_opt.reset) {
		if (i2c_opt.addr_set)
			usage();
	} else if (error) {
		usage();
	} else if ((i2c_opt.dir == 'r' || i2c_opt.dir == 'w')) {
		if ((i2c_opt.addr_set == 0) ||
		    !(i2c_opt.width == 0 || i2c_opt.width == 8 ||
		    i2c_opt.width == 16))
		usage();
	}

	if (i2c_opt.verbose)
		fprintf(stderr, "dev: %s, addr: 0x%x, r/w: %c, "
		    "offset: 0x%02x, width: %u, count: %u\n", dev,
		    i2c_opt.addr >> 1, i2c_opt.dir, i2c_opt.off,
		    i2c_opt.width, i2c_opt.count);

	if (i2c_opt.scan)
		exit(scan_bus(cmd, dev, i2c_opt.skip, skip_addr));

	if (i2c_opt.reset)
		exit(reset_bus(cmd, dev));

	i2c_buf = malloc(i2c_opt.count);
	if (i2c_buf == NULL)
		err(1, "data malloc");

	if (i2c_opt.dir == 'w') {
		error = i2c_write(dev, i2c_opt, i2c_buf);
		if (error) {
			free(i2c_buf);
			return (1);
		}
	}
	if (i2c_opt.dir == 'r') {
		error = i2c_read(dev, i2c_opt, i2c_buf);
		if (error) {
			free(i2c_buf);
			return (1);
		}
	}

	if (i2c_opt.verbose)
		fprintf(stderr, "\nData %s (hex):\n", i2c_opt.dir == 'r' ?
		    "read" : "written");

	i = 0;
	j = 0;
	while (i < i2c_opt.count) {
		if (i2c_opt.verbose || (i2c_opt.dir == 'r' &&
		    !i2c_opt.binary))
			fprintf (stderr, "%02hhx ", i2c_buf[i++]);

		if (i2c_opt.dir == 'r' && i2c_opt.binary) {
			fprintf(stdout, "%c", i2c_buf[j++]);
			if(!i2c_opt.verbose)
				i++;
		}
		if (!i2c_opt.verbose && (i2c_opt.dir == 'w'))
			break;
		if ((i % chunk_size) == 0)
			fprintf(stderr, "\n");
	}
	if ((i % chunk_size) != 0)
		fprintf(stderr, "\n");

	free(i2c_buf);
	return (0);
}
