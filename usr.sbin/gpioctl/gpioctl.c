/*	$OpenBSD: gpioctl.c,v 1.17 2015/12/26 20:52:03 mmcc Exp $	*/
/*
 * Copyright (c) 2008 Marc Balmer <mbalmer@openbsd.org>
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
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

/*
 * Program to control GPIO devices.
 */

#include <sys/types.h>
#include <sys/gpio.h>
#include <sys/ioctl.h>
#include <sys/limits.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


char *dev;
int devfd = -1;
int quiet = 0;

void	getinfo(void);
void	pinread(int, char *);
void	pinwrite(int, char *, int);
void	pinset(int pin, char *name, int flags, char *alias);
void	unset(int pin, char *name);
void	devattach(char *, int, u_int32_t, u_int32_t);
void	devdetach(char *);

__dead void usage(void);

const struct bitstr {
	unsigned int mask;
	const char *string;
} pinflags[] = {
	{ GPIO_PIN_INPUT, "in" },
	{ GPIO_PIN_OUTPUT, "out" },
	{ GPIO_PIN_INOUT, "inout" },
	{ GPIO_PIN_OPENDRAIN, "od" },
	{ GPIO_PIN_PUSHPULL, "pp" },
	{ GPIO_PIN_TRISTATE, "tri" },
	{ GPIO_PIN_PULLUP, "pu" },
	{ GPIO_PIN_PULLDOWN, "pd" },
	{ GPIO_PIN_INVIN, "iin" },
	{ GPIO_PIN_INVOUT, "iout" },
	{ 0, NULL },
};

int
main(int argc, char *argv[])
{
	const struct bitstr *bs;
	long lval;
	u_int32_t ga_mask = 0, ga_flags = 0;
	int pin, ch, ga_offset = -1, n, fl = 0, value = 0;
	const char *errstr;
	char *ep, *flags, *nam = NULL;
	char devn[32];

	while ((ch = getopt(argc, argv, "q")) != -1)
		switch (ch) {
		case 'q':
			quiet = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();
	dev = argv[0];

	if (strncmp(_PATH_DEV, dev, sizeof(_PATH_DEV) - 1)) {
		(void)snprintf(devn, sizeof(devn), "%s%s", _PATH_DEV, dev);
		dev = devn;
	}

	if ((devfd = open(dev, O_RDWR)) == -1)
		err(1, "%s", dev);

	if (argc == 1) {
		getinfo();
		return 0;
	}

	if (!strcmp(argv[1], "attach")) {
		char *driver, *offset, *mask;

		if (argc != 5 && argc != 6)
			usage();

		driver = argv[2];
		offset = argv[3];
		mask = argv[4];
		flags = argc == 6 ? argv[5] : NULL;

		ga_offset = strtonum(offset, 0, INT_MAX, &errstr);
		if (errstr)
			errx(1, "offset is %s: %s", errstr, offset);

		lval = strtol(mask, &ep, 0);
		if (*mask == '\0' || *ep != '\0')
			errx(1, "invalid mask (not a number)");
		if ((errno == ERANGE && (lval == LONG_MAX
		    || lval == LONG_MIN)) || lval > UINT_MAX)
			errx(1, "mask out of range");
		ga_mask = lval;
		if (flags != NULL) {
			lval = strtonum(flags, 0, UINT_MAX, &errstr);
			if (errstr)
				errx(1, "flags is %s: %s", errstr, flags);
			ga_flags = lval;
		}
		devattach(driver, ga_offset, ga_mask, ga_flags);
		return 0;
	} else if (!strcmp(argv[1], "detach")) {
		if (argc != 3)
			usage();
		devdetach(argv[2]);
	} else {
		char *nm = NULL;

		/* expecting a pin number or name */
		pin = strtonum(argv[1], 0, INT_MAX, &errstr);
		if (errstr)
			nm = argv[1];	/* try named pin */
		if (argc > 2) {
			if (!strcmp(argv[2], "set")) {
				for (n = 3; n < argc; n++) {
					for (bs = pinflags; bs->string != NULL;
					     bs++) {
						if (!strcmp(argv[n],
						    bs->string)) {
							fl |= bs->mask;
							break;
						}
					}
					if (bs->string == NULL)
						nam = argv[n];
				}
				pinset(pin, nm, fl, nam);
			} else if (!strcmp(argv[2], "unset")) {
				unset(pin, nm);
			} else {
				value = strtonum(argv[2], INT_MIN, INT_MAX,
				   &errstr);
				if (errstr) {
					if (!strcmp(argv[2], "on"))
						value = 1;
					else if (!strcmp(argv[2], "off"))
						value = 0;
					else if (!strcmp(argv[2], "toggle"))
						value = 2;
					else
						errx(1, "%s: invalid value",
						    argv[2]);
				}
				pinwrite(pin, nm, value);
			}
		} else
			pinread(pin, nm);
	}

	return (0);
}

void
getinfo(void)
{
	struct gpio_info info;

	memset(&info, 0, sizeof(info));
	if (ioctl(devfd, GPIOINFO, &info) == -1)
		err(1, "GPIOINFO");

	if (quiet)
		return;

	printf("%s: %d pins\n", dev, info.gpio_npins);
}

void
pinread(int pin, char *gp_name)
{
	struct gpio_pin_op op;

	memset(&op, 0, sizeof(op));
	if (gp_name != NULL)
		strlcpy(op.gp_name, gp_name, sizeof(op.gp_name));
	else
		op.gp_pin = pin;

	if (ioctl(devfd, GPIOPINREAD, &op) == -1)
		err(1, "GPIOPINREAD");

	if (quiet)
		return;

	if (gp_name)
		printf("pin %s: state %d\n", gp_name, op.gp_value);
	else
		printf("pin %d: state %d\n", pin, op.gp_value);
}

void
pinwrite(int pin, char *gp_name, int value)
{
	struct gpio_pin_op op;

	if (value < 0 || value > 2)
		errx(1, "%d: invalid value", value);

	memset(&op, 0, sizeof(op));
	if (gp_name != NULL)
		strlcpy(op.gp_name, gp_name, sizeof(op.gp_name));
	else
		op.gp_pin = pin;
	op.gp_value = (value == 0 ? GPIO_PIN_LOW : GPIO_PIN_HIGH);
	if (value < 2) {
		if (ioctl(devfd, GPIOPINWRITE, &op) == -1)
			err(1, "GPIOPINWRITE");
	} else {
		if (ioctl(devfd, GPIOPINTOGGLE, &op) == -1)
			err(1, "GPIOPINTOGGLE");
	}

	if (quiet)
		return;

	if (gp_name)
		printf("pin %s: state %d -> %d\n", gp_name, op.gp_value,
		    (value < 2 ? value : 1 - op.gp_value));
	else
		printf("pin %d: state %d -> %d\n", pin, op.gp_value,
		    (value < 2 ? value : 1 - op.gp_value));
}

void
pinset(int pin, char *name, int fl, char *alias)
{
	struct gpio_pin_set set;
	const struct bitstr *bs;

	memset(&set, 0, sizeof(set));
	if (name != NULL)
		strlcpy(set.gp_name, name, sizeof(set.gp_name));
	else
		set.gp_pin = pin;
	set.gp_flags = fl;

	if (alias != NULL)
		strlcpy(set.gp_name2, alias, sizeof(set.gp_name2));

	if (ioctl(devfd, GPIOPINSET, &set) == -1)
		err(1, "GPIOPINSET");

	if (quiet)
		return;

	if (name != NULL)
		printf("pin %s: caps:", name);
	else
		printf("pin %d: caps:", pin);
	for (bs = pinflags; bs->string != NULL; bs++)
		if (set.gp_caps & bs->mask)
			printf(" %s", bs->string);
	printf(", flags:");
	for (bs = pinflags; bs->string != NULL; bs++)
		if (set.gp_flags & bs->mask)
			printf(" %s", bs->string);
	if (fl > 0) {
		printf(" ->");
		for (bs = pinflags; bs->string != NULL; bs++)
			if (fl & bs->mask)
				printf(" %s", bs->string);
	}
	printf("\n");
}

void
unset(int pin, char *name)
{
	struct gpio_pin_set set;

	memset(&set, 0, sizeof(set));
	if (name != NULL)
		strlcpy(set.gp_name, name, sizeof(set.gp_name));
	else
		set.gp_pin = pin;

	if (ioctl(devfd, GPIOPINUNSET, &set) == -1)
		err(1, "GPIOPINUNSET");
}

void
devattach(char *dvname, int offset, u_int32_t mask, u_int32_t flags)
{
	struct gpio_attach attach;

	memset(&attach, 0, sizeof(attach));
	strlcpy(attach.ga_dvname, dvname, sizeof(attach.ga_dvname));
	attach.ga_offset = offset;
	attach.ga_mask = mask;
	attach.ga_flags = flags;
	if (ioctl(devfd, GPIOATTACH, &attach) == -1)
		err(1, "GPIOATTACH");
}

void
devdetach(char *dvname)
{
	struct gpio_attach attach;

	memset(&attach, 0, sizeof(attach));
	strlcpy(attach.ga_dvname, dvname, sizeof(attach.ga_dvname));
	if (ioctl(devfd, GPIODETACH, &attach) == -1)
		err(1, "GPIODETACH");
}
void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-q] device pin [0 | 1 | 2 | "
	    "on | off | toggle]\n", __progname);
	fprintf(stderr, "       %s [-q] device pin set [flags] [name]\n",
	    __progname);
	fprintf(stderr, "       %s [-q] device pin unset\n", __progname);
	fprintf(stderr, "       %s [-q] device attach device offset mask "
	    "[flag]\n", __progname);
	fprintf(stderr, "       %s [-q] device detach device\n", __progname);

	exit(1);
}
