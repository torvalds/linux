/*      $OpenBSD: test1.C,v 1.1 2007/09/03 14:42:44 millert Exp $	*/

/*
 * Copyright (c) 2007 Kurt Miller <kurt@openbsd.org>
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

#include <dlfcn.h>
#include <err.h>
#include <stdio.h>

int check1, check2;

int
main()
{
	void *libgd1, *libgd2;
	void (*gd_test)();
	int i;

	for (i=0; i < 50; i++) {
		check1 = check2 = 1;

		libgd1 = dlopen(LIBGD1, RTLD_LAZY);
		if (libgd1 == NULL)
                	errx(1, "dlopen(%s, RTLD_LAZY) FAILED\n", LIBGD1);

        	gd_test = (void (*)())dlsym(libgd1, "gd_test1");
		if (gd_test == NULL)
			errx(1, "dlsym(libgd1, \"gd_test1\") FAILED\n");

		(*gd_test)();

		libgd2 = dlopen(LIBGD2, RTLD_LAZY);
		if (libgd2 == NULL)
                	errx(1, "dlopen(%s, RTLD_LAZY) FAILED\n", LIBGD2);

        	gd_test = (void (*)())dlsym(libgd2, "gd_test2");
		if (gd_test == NULL)
			errx(1, "dlsym(libgd2, \"gd_test2\") FAILED\n");

		(*gd_test)();

		dlclose(libgd1);
		dlclose(libgd2);

		if (check1 || check2)
			errx(1, "global destructors not called\n");
	}

	return (0);
}
