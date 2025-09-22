/*	$OpenBSD: hostctl.c,v 1.6 2023/01/07 06:40:21 asou Exp $	*/

/*
 * Copyright (c) 2016 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/ioctl.h>
#include <sys/types.h>

#include <dev/pv/pvvar.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <vis.h>

#define KVBUFSZ	 4096				/* arbitrary value */

char		*path_pvbus = "/dev/pvbus0";	/* the first hv interface */
int		 qflag = 0;			/* quiet */
int		 tflag = 0;			/* show type */

__dead void	 usage(void);
int		 kvsetstr(char *, const char *, size_t);
int		 kvsetfile(char *, const char *, size_t);

__dead void
usage(void)
{
	extern char	*__progname;
	fprintf(stderr, "usage: %s [-qt] [-f device] "
	    "[-i input] [-o output] key [value]\n", __progname);
	exit(1);
}

int
kvsetstr(char *dst, const char *src, size_t dstlen)
{
	size_t	sz;

	/* Sanitize the string before sending it to the kernel and host */
	if ((sz = strnvis(dst, src, dstlen, VIS_SAFE | VIS_CSTYLE)) >= dstlen)
		return (-1);

	/* Remove trailing newline */
	dst[strcspn(dst, "\n")] = '\0';

	return (0);
}

int
kvsetfile(char *dst, const char *input, size_t dstlen)
{
	char	*buf = NULL;
	int	 ret = -1;
	FILE	*fp;

	if (strcmp("-", input) == 0)
		fp = stdin;
	else if ((fp = fopen(input, "r")) == NULL)
		return (-1);

	if ((buf = calloc(1, dstlen)) == NULL)
		goto done;
	if (fread(buf, 1, dstlen - 1, fp) == 0)
		goto done;
	if (kvsetstr(dst, buf, dstlen) == -1)
		goto done;

	ret = 0;
 done:
	free(buf);
	if (fp != stdin)
		fclose(fp);
	return (ret);
}

int
main(int argc, char *argv[])
{
	const char		*key, *value, *in = NULL, *out = NULL;
	FILE			*outfp = stdout;
	int			fd, ret;
	struct pvbus_req	pvr;
	int			ch;
	unsigned long		cmd = 0;
	char			*str;

	while ((ch = getopt(argc, argv, "f:i:o:qt")) != -1) {
		switch (ch) {
		case 'f':
			path_pvbus = optarg;
			break;
		case 'i':
			in = optarg;
			break;
		case 'o':
			out = optarg;
			break;
		case 'q':
			qflag++;
			break;
		case 't':
			tflag++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if ((fd = open(path_pvbus, O_RDONLY)) == -1)
		err(1, "open: %s", path_pvbus);

	if (out != NULL) {
		if (strcmp("-", out) == 0)
			outfp = stdout;
		else if ((outfp = fopen(out, "w")) == NULL)
			err(1, "fopen: %s", out);
	}

	memset(&pvr, 0, sizeof(pvr));
	pvr.pvr_keylen = pvr.pvr_valuelen = KVBUFSZ;
	if ((pvr.pvr_key = calloc(1, pvr.pvr_keylen)) == NULL ||
	    (pvr.pvr_value = calloc(1, pvr.pvr_valuelen)) == NULL)
		err(1, "calloc");

	if (tflag) {
		if (ioctl(fd, PVBUSIOC_TYPE, &pvr, sizeof(pvr)) == -1)
			err(1, "ioctl");

		/* The returned type should be a simple single-line key */
		if (stravis(&str, pvr.pvr_key,
		    VIS_WHITE | VIS_DQ | VIS_CSTYLE) == -1)
			err(1, "stravis");
		fprintf(outfp, "%s: %s\n", path_pvbus, str);
		free(str);
		goto done;
	}

	if (argc < 1)
		usage();
	key = argv[0];

	if (kvsetstr(pvr.pvr_key, key, pvr.pvr_keylen) == -1)
		errx(1, "key too long");

	/* Validate command line options for reading or writing */
	if (argc == 2 && in == NULL) {
		cmd = PVBUSIOC_KVWRITE;
		value = argv[1];
		if (kvsetstr(pvr.pvr_value, value, pvr.pvr_valuelen) == -1)
			errx(1, "value too long");
	} else if (argc == 1 && in != NULL) {
		cmd = PVBUSIOC_KVWRITE;
		if (kvsetfile(pvr.pvr_value, in, pvr.pvr_valuelen) == -1)
			errx(1, "input file");
	} else if (argc == 1) {
		cmd = cmd == 0 ? PVBUSIOC_KVREAD : cmd;
	} else
		usage();

	/* Re-open read-writable */
	if (cmd != PVBUSIOC_KVREAD) {
		close(fd);
		if ((fd = open(path_pvbus, O_RDWR)) == -1)
			err(1, "open: %s", path_pvbus);
		if ((ret = ioctl(fd, cmd, &pvr, sizeof(pvr))) == -1)
			err(1, "ioctl");
	} else {
		while (1) {
			if ((ret = ioctl(fd, cmd, &pvr, sizeof(pvr))) == 0)
				break;
			if (errno == ERANGE &&
			    pvr.pvr_valuelen < PVBUS_KVOP_MAXSIZE) {
				/* the buffer is not enough, expand it */
				pvr.pvr_valuelen *= 2;
				if ((pvr.pvr_value = realloc(pvr.pvr_value,
				    pvr.pvr_valuelen)) == NULL)
					err(1, "realloc");
				continue;
			}
			err(1, "ioctl");
		}
	}

	if (!qflag && strlen(pvr.pvr_value)) {
		/*
		 * The value can contain newlines and basically anything;
		 * only encode the unsafe characters that could perform
		 * unexpected functions on the terminal.
		 */
		if (stravis(&str, pvr.pvr_value, VIS_SAFE | VIS_CSTYLE) == -1)
			err(1, "stravis");
		fprintf(outfp, "%s\n", str);
		free(str);
	}

 done:
	if (outfp != stdout)
		fclose(outfp);
	free(pvr.pvr_value);
	free(pvr.pvr_key);
	close(fd);

	return (0);
}
