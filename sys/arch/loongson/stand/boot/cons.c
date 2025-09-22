/*	$OpenBSD: cons.c,v 1.2 2010/02/16 21:28:39 miod Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
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

#include <lib/libkern/libkern.h>
#include "libsa.h"
#include <dev/cons.h>
#include <machine/cpu.h>
#include <machine/pmon.h>

/*
 * PMON console
 */

void
pmon_cnprobe(struct consdev *cn)
{
	cn->cn_pri = CN_HIGHPRI;
}

void
pmon_cninit(struct consdev *cn)
{
}

int
pmon_cngetc(dev_t dev)
{
	/*
	 * PMON does not give us a getc routine.  So try to get a whole line
	 * and return it char by char, trying not to lose the \n.  Kind
	 * of ugly but should work.
	 *
	 * Note that one could theoretically use pmon_read(STDIN, &c, 1)
	 * but the value of STDIN within PMON is not a constant and there
	 * does not seem to be a way of letting us know which value to use.
	 */
	static char buf[1 + PMON_MAXLN];
	static char *bufpos = buf;
	int c;

	if (*bufpos == '\0') {
		bufpos = buf;
		if (pmon_gets(buf) == NULL) {
			/* either an empty line or EOF, assume the former */
			strlcpy(buf, "\n", sizeof buf);
		} else {
			/* put back the \n sign */
			buf[strlen(buf)] = '\n';
		}
	}

	c = (int)*bufpos;
	if ((dev & 0x80) == 0) {
		bufpos++;
		if (bufpos - buf > PMON_MAXLN) {
			bufpos = buf;
			*bufpos = '\0';
		}
	}

	return c;
}

void
pmon_cnputc(dev_t dev, int c)
{
	if (c == '\n')
		pmon_printf("\n");
	else
		pmon_printf("%c", c);
}
