/*	$OpenBSD: entropy.c,v 1.1 2021/05/27 18:18:41 bluhm Exp $	*/

/*
 * Copyright (c) 2021 Alexander Bluhm <bluhm@openbsd.org>
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

#include <err.h>
#include <expat.h>
#include <string.h>

int
main(int argc, char *argv[])
{
	XML_Parser p;
	enum XML_Status s;

	if (setenv("EXPAT_ENTROPY_DEBUG", "1", 1) != 0)
		err(1, "setenv EXPAT_ENTROPY_DEBUG");

	p = XML_ParserCreate(NULL);
	if (p == NULL)
		errx(1, "XML_ParserCreate");
	s = XML_Parse(p, "", 0, 0);
	if (s != XML_STATUS_OK)
		errx(1, "XML_Parse: %d", s);

	return 0;
}
