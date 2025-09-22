/*	$OpenBSD: tib.c,v 1.3 2022/12/27 17:10:06 jmc Exp $ */
/*
 * Copyright (c) 2016 Philip Guenther <guenther@openbsd.org>
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

#include <tib.h>

#ifndef PIC
# include <stdlib.h>		/* posix_memalign and free */
#endif

#define ELF_ROUND(x,malign)	(((x) + (malign)-1) & ~((malign)-1))


/*
 * The functions here are weak so that the ld.so versions are used
 * in dynamic links, whether or not libc is static
 */
void	*_dl_allocate_tib(size_t _extra) __attribute__((weak));
void	_dl_free_tib(void *_tib, size_t _extra) __attribute__((weak));

/*
 * Allocate a TIB for passing to __tfork for a new thread.  'extra'
 * is the amount of space to allocate on the side of the TIB opposite
 * of the TLS data: before the TIB for variant 1 and after the TIB
 * for variant 2.  If non-zero, tib_thread is set to point to that area.
 */
void *
_dl_allocate_tib(size_t extra)
{
#ifdef PIC
	return NULL;			/* overridden by ld.so */
#else
	void *base;
	char *thread;

# if TLS_VARIANT == 1
	/* round up the extra size to align the TIB and TLS data after it */
	extra = (extra <= _static_tls_align_offset) ? 0 :
	    ELF_ROUND(extra - _static_tls_align_offset, _static_tls_align);
	if (posix_memalign(&base, _static_tls_align, extra +
	    _static_tls_align_offset + sizeof(struct tib) +
	    _static_tls_size) != 0)
		return NULL;
	thread = base;
	base = (char *)base + extra;

# elif TLS_VARIANT == 2
	/* round up the TIB size to align the extra area after it */
	if (posix_memalign(&base, _static_tls_align,
	    _static_tls_align_offset + _static_tls_size +
	    ELF_ROUND(sizeof(struct tib), TIB_EXTRA_ALIGN) + extra) != 0)
		return NULL;
	thread = (char *)base + _static_tls_align_offset + _static_tls_size +
	    ELF_ROUND(sizeof(struct tib), TIB_EXTRA_ALIGN);
# endif

	return _static_tls_init(base, thread);
#endif /* !PIC */
}

void
_dl_free_tib(void *tib, size_t extra)
{
#ifndef PIC
	size_t tib_offset;

# if TLS_VARIANT == 1
	tib_offset = (extra <= _static_tls_align_offset) ? 0 :
	    ELF_ROUND(extra - _static_tls_align_offset, _static_tls_align);
# elif TLS_VARIANT == 2
	tib_offset = _static_tls_size;
# endif
	tib_offset += _static_tls_align_offset;

	free((char *)tib - tib_offset);
#endif /* !PIC */
}
