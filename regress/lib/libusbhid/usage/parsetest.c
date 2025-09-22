/*	$OpenBSD: parsetest.c,v 1.1 2018/07/09 09:03:29 mpi Exp $	*/
/*
 * Copyright (c) 2018 David Bern <david.ml.bern@gmail.com>
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

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <usbhid.h>

int
main(int argc, char *argv[])
{
	char *table = NULL;
	char usage[1024];
	int testval;
	const char *errstr;

	if (hid_start(table) == -1)
		errx(1, "\nUnable to load table");

	/* No args given, just test if able to load table */
	if (argc < 2)
		return 0;

	testval = strtonum(argv[1], 0x0, 0xFFFFFFFF, &errstr);
	if (errstr != NULL)
		errx(1, "\nInvalid argument");

	snprintf(usage, sizeof(usage), "%s:%s",
	    hid_usage_page(HID_PAGE(testval)),
	    hid_usage_in_page(testval));

	return (hid_parse_usage_in_page(usage) != testval);
}
