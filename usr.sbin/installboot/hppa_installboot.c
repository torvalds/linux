/*	$OpenBSD: hppa_installboot.c,v 1.4 2021/07/20 14:51:56 kettenis Exp $	*/

/*
 * Copyright (c) 2013 Joel Sing <jsing@openbsd.org>
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

#include "installboot.h"

void
md_init(void)
{
	stages = 1;
	stage1 = "/usr/mdec/boot.lif";
}

void
md_loadboot(void)
{
}

void
md_prepareboot(int devfd, char *dev)
{
}

void
md_installboot(int devfd, char *dev)
{
	bootstrap(devfd, dev, stage1);
}
