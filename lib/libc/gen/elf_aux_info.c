/*	$OpenBSD: elf_aux_info.c,v 1.1 2024/07/14 09:48:48 jca Exp $	*/

/*
 * Copyright (c) 2024 Jeremie Courreges-Anglas <jca@wxcvbn.org>
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

#include <sys/types.h>
#include <sys/auxv.h>

#include <errno.h>

extern int _pagesize;
extern unsigned long _hwcap, _hwcap2;
extern int _hwcap_avail, _hwcap2_avail;

int
elf_aux_info(int request, void *buf, int buflen)
{
	int ret = 0;

	if (buflen < 0)
		return EINVAL;

	switch (request) {
	case AT_HWCAP:
		if (buflen != sizeof(unsigned long))
			ret = EINVAL;
		else if (!_hwcap_avail)
			ret = ENOENT;
		else
			*(unsigned long *)buf = _hwcap;
		break;
	case AT_HWCAP2:
		if (buflen != sizeof(unsigned long))
			ret = EINVAL;
		else if (!_hwcap2_avail)
			ret = ENOENT;
		else
			*(unsigned long *)buf = _hwcap2;
		break;
	case AT_PAGESZ:
		if (buflen != sizeof(int))
			ret = EINVAL;
		else if (!_pagesize)
			ret = ENOENT;
		else
			*(int *)buf = _pagesize;
		break;
	default:
		if (request < 0 || request >= AT_COUNT)
			ret = EINVAL;
		else
			ret = ENOENT;
		break;
	}

	return ret;
}
