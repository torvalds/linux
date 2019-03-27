/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * Copyright (c) 2014, Rui Paulo <rpaulo@FreeBSD.org>
 * Copyright (c) 2015, Emmanuel Vadot <manu@bidouilliste.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#include <fcntl.h>
#include <getopt.h>
#include <paths.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libgpio.h>

#define PIN_TYPE_NUMBER		1
#define PIN_TYPE_NAME		2

struct flag_desc {
	const char *name;
	uint32_t flag;
};

static struct flag_desc gpio_flags[] = {
	{ "IN", GPIO_PIN_INPUT },
	{ "OUT", GPIO_PIN_OUTPUT },
	{ "OD", GPIO_PIN_OPENDRAIN },
	{ "PP", GPIO_PIN_PUSHPULL },
	{ "TS", GPIO_PIN_TRISTATE },
	{ "PU", GPIO_PIN_PULLUP },
	{ "PD", GPIO_PIN_PULLDOWN },
	{ "II", GPIO_PIN_INVIN },
	{ "IO", GPIO_PIN_INVOUT },
	{ "PULSE", GPIO_PIN_PULSATE },
	{ NULL, 0 },
};

int str2cap(const char *str);

static void
usage(void)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\tgpioctl [-f ctldev] -l [-v]\n");
	fprintf(stderr, "\tgpioctl [-f ctldev] [-pN] -t pin\n");
	fprintf(stderr, "\tgpioctl [-f ctldev] [-pN] -c pin flag ...\n");
	fprintf(stderr, "\tgpioctl [-f ctldev] [-pN] -n pin pin-name\n");
	fprintf(stderr, "\tgpioctl [-f ctldev] [-pN] pin [0|1]\n");
	exit(1);
}

static const char *
cap2str(uint32_t cap)
{
	struct flag_desc * pdesc = gpio_flags;
	while (pdesc->name) {
		if (pdesc->flag == cap)
			return pdesc->name;
		pdesc++;
	}

	return "UNKNOWN";
}

int
str2cap(const char *str)
{
	struct flag_desc * pdesc = gpio_flags;
	while (pdesc->name) {
		if (strcasecmp(str, pdesc->name) == 0)
			return pdesc->flag;
		pdesc++;
	}

	return (-1);
}

/*
 * Our handmade function for converting string to number
 */
static int
str2int(const char *s, int *ok)
{
	char *endptr;
	int res = strtod(s, &endptr);
	if (endptr != s + strlen(s) )
		*ok = 0;
	else
		*ok = 1;

	return res;
}

static void
print_caps(int caps)
{
	int i, need_coma;

	need_coma = 0;
	printf("<");
	for (i = 0; i < 32; i++) {
		if (caps & (1 << i)) {
			if (need_coma)
				printf(",");
			printf("%s", cap2str(1 << i));
			need_coma = 1;
		}
	}
	printf(">");
}

static void
dump_pins(gpio_handle_t handle, int verbose)
{
	int i, maxpin, pinv;
	gpio_config_t *cfgs;
	gpio_config_t *pin;

	maxpin = gpio_pin_list(handle, &cfgs);
	if (maxpin < 0) {
		perror("gpio_pin_list");
		exit(1);
	}

	for (i = 0; i <= maxpin; i++) {
		pin = cfgs + i;
		pinv = gpio_pin_get(handle, pin->g_pin);
		printf("pin %02d:\t%d\t%s", pin->g_pin, pinv,
		    pin->g_name);

		print_caps(pin->g_flags);

		if (verbose) {
			printf(", caps:");
			print_caps(pin->g_caps);
		}
		printf("\n");
	}
	free(cfgs);
}

static int
get_pinnum_by_name(gpio_handle_t handle, const char *name) {
	int i, maxpin, pinn;
	gpio_config_t *cfgs;
	gpio_config_t *pin;

	pinn = -1;
	maxpin = gpio_pin_list(handle, &cfgs);
	if (maxpin < 0) {
		perror("gpio_pin_list");
		exit(1);
	}

	for (i = 0; i <= maxpin; i++) {
		pin = cfgs + i;
		gpio_pin_get(handle, pin->g_pin);
		if (!strcmp(name, pin->g_name)) {
			pinn = i;
			break;
		}
	}
	free(cfgs);

	return pinn;
}

static void
fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

int
main(int argc, char **argv)
{
	int i;
	gpio_config_t pin;
	gpio_handle_t handle;
	char *ctlfile = NULL;
	int pinn, pinv, pin_type, ch;
	int flags, flag, ok;
	int config, list, name, toggle, verbose;

	config = toggle = verbose = list = name = pin_type = 0;

	while ((ch = getopt(argc, argv, "cf:lntvNp")) != -1) {
		switch (ch) {
		case 'c':
			config = 1;
			break;
		case 'f':
			ctlfile = optarg;
			break;
		case 'l':
			list = 1;
			break;
		case 'n':
			name = 1;
			break;
		case 'N':
			pin_type = PIN_TYPE_NAME;
			break;
		case'p':
			pin_type = PIN_TYPE_NUMBER;
			break;
		case 't':
			toggle = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			break;
		}
	}
	argv += optind;
	argc -= optind;
	if (ctlfile == NULL)
		handle = gpio_open(0);
	else
		handle = gpio_open_device(ctlfile);
	if (handle == GPIO_INVALID_HANDLE) {
		perror("gpio_open");
		exit(1);
	}

	if (list) {
		dump_pins(handle, verbose);
		gpio_close(handle);
		exit(0);
	}

	if (argc == 0)
		usage();

	/* Find the pin number by the name */
	switch (pin_type) {
	default:
		/* First test if it is a pin number */
		pinn = str2int(argv[0], &ok);
		if (ok) {
			/* Test if we have any pin named by this number and tell the user */
			if (get_pinnum_by_name(handle, argv[0]) != -1)
				fail("%s is also a pin name, use -p or -N\n", argv[0]);
		} else {
			/* Test if it is a name */
			if ((pinn = get_pinnum_by_name(handle, argv[0])) == -1)
				fail("Can't find pin named \"%s\"\n", argv[0]);
		}
		break;
	case PIN_TYPE_NUMBER:
		pinn = str2int(argv[0], &ok);
		if (!ok)
			fail("Invalid pin number: %s\n", argv[0]);
		break;
	case PIN_TYPE_NAME:
		if ((pinn = get_pinnum_by_name(handle, argv[0])) == -1)
			fail("Can't find pin named \"%s\"\n", argv[0]);
		break;
	}

	/* Set the pin name. */
	if (name) {
		if (argc != 2)
			usage();
		if (gpio_pin_set_name(handle, pinn, argv[1]) < 0) {
			perror("gpio_pin_set_name");
			exit(1);
		}
		exit(0);
	}

	if (toggle) {
		/*
                * -t pin assumes no additional arguments
                */
		if (argc > 1)
			usage();
		if (gpio_pin_toggle(handle, pinn) < 0) {
			perror("gpio_pin_toggle");
			exit(1);
		}
		gpio_close(handle);
		exit(0);
	}

	if (config) {
		flags = 0;
		for (i = 1; i < argc; i++) {
			flag = 	str2cap(argv[i]);
			if (flag < 0)
				fail("Invalid flag: %s\n", argv[i]);
			flags |= flag;
		}
		pin.g_pin = pinn;
		pin.g_flags = flags;
		if (gpio_pin_set_flags(handle, &pin) < 0) {
			perror("gpio_pin_set_flags");
			exit(1);
		}
		exit(0);
	}

	/*
	 * Last two cases - set value or print value
	 */
	if ((argc == 0) || (argc > 2))
		usage();

	/*
	 * Read pin value
	 */
	if (argc == 1) {
		pinv = gpio_pin_get(handle, pinn);
		if (pinv < 0) {
			perror("gpio_pin_get");
			exit(1);
		}
		printf("%d\n", pinv);
		exit(0);
	}

	/* Is it valid number (0 or 1) ? */
	pinv = str2int(argv[1], &ok);
	if (ok == 0 || ((pinv != 0) && (pinv != 1)))
		fail("Invalid pin value: %s\n", argv[1]);

	/*
	 * Set pin value
	 */
	if (gpio_pin_set(handle, pinn, pinv) < 0) {
		perror("gpio_pin_set");
		exit(1);
	}

	gpio_close(handle);
	exit(0);
}
