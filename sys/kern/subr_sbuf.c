/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2008 Poul-Henning Kamp
 * Copyright (c) 2000-2008 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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

#include <sys/param.h>

#ifdef _KERNEL
#include <sys/ctype.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <machine/stdarg.h>
#else /* _KERNEL */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif /* _KERNEL */

#include <sys/sbuf.h>

#ifdef _KERNEL
static MALLOC_DEFINE(M_SBUF, "sbuf", "string buffers");
#define	SBMALLOC(size)		malloc(size, M_SBUF, M_WAITOK|M_ZERO)
#define	SBFREE(buf)		free(buf, M_SBUF)
#else /* _KERNEL */
#define	KASSERT(e, m)
#define	SBMALLOC(size)		calloc(1, size)
#define	SBFREE(buf)		free(buf)
#endif /* _KERNEL */

/*
 * Predicates
 */
#define	SBUF_ISDYNAMIC(s)	((s)->s_flags & SBUF_DYNAMIC)
#define	SBUF_ISDYNSTRUCT(s)	((s)->s_flags & SBUF_DYNSTRUCT)
#define	SBUF_ISFINISHED(s)	((s)->s_flags & SBUF_FINISHED)
#define	SBUF_HASROOM(s)		((s)->s_len < (s)->s_size - 1)
#define	SBUF_FREESPACE(s)	((s)->s_size - ((s)->s_len + 1))
#define	SBUF_CANEXTEND(s)	((s)->s_flags & SBUF_AUTOEXTEND)
#define	SBUF_ISSECTION(s)	((s)->s_flags & SBUF_INSECTION)
#define	SBUF_NULINCLUDED(s)	((s)->s_flags & SBUF_INCLUDENUL)
#define	SBUF_ISDRAINTOEOR(s)	((s)->s_flags & SBUF_DRAINTOEOR)
#define	SBUF_DODRAINTOEOR(s)	(SBUF_ISSECTION(s) && SBUF_ISDRAINTOEOR(s))

/*
 * Set / clear flags
 */
#define	SBUF_SETFLAG(s, f)	do { (s)->s_flags |= (f); } while (0)
#define	SBUF_CLEARFLAG(s, f)	do { (s)->s_flags &= ~(f); } while (0)

#define	SBUF_MINSIZE		 2		/* Min is 1 byte + nulterm. */
#define	SBUF_MINEXTENDSIZE	16		/* Should be power of 2. */

#ifdef PAGE_SIZE
#define	SBUF_MAXEXTENDSIZE	PAGE_SIZE
#define	SBUF_MAXEXTENDINCR	PAGE_SIZE
#else
#define	SBUF_MAXEXTENDSIZE	4096
#define	SBUF_MAXEXTENDINCR	4096
#endif

/*
 * Debugging support
 */
#if defined(_KERNEL) && defined(INVARIANTS)

static void
_assert_sbuf_integrity(const char *fun, struct sbuf *s)
{

	KASSERT(s != NULL,
	    ("%s called with a NULL sbuf pointer", fun));
	KASSERT(s->s_buf != NULL,
	    ("%s called with uninitialized or corrupt sbuf", fun));
	if (SBUF_ISFINISHED(s) && SBUF_NULINCLUDED(s)) {
		KASSERT(s->s_len <= s->s_size,
		    ("wrote past end of sbuf (%jd >= %jd)",
		    (intmax_t)s->s_len, (intmax_t)s->s_size));
	} else {
		KASSERT(s->s_len < s->s_size,
		    ("wrote past end of sbuf (%jd >= %jd)",
		    (intmax_t)s->s_len, (intmax_t)s->s_size));
	}
}

static void
_assert_sbuf_state(const char *fun, struct sbuf *s, int state)
{

	KASSERT((s->s_flags & SBUF_FINISHED) == state,
	    ("%s called with %sfinished or corrupt sbuf", fun,
	    (state ? "un" : "")));
}

#define	assert_sbuf_integrity(s) _assert_sbuf_integrity(__func__, (s))
#define	assert_sbuf_state(s, i)	 _assert_sbuf_state(__func__, (s), (i))

#else /* _KERNEL && INVARIANTS */

#define	assert_sbuf_integrity(s) do { } while (0)
#define	assert_sbuf_state(s, i)	 do { } while (0)

#endif /* _KERNEL && INVARIANTS */

#ifdef CTASSERT
CTASSERT(powerof2(SBUF_MAXEXTENDSIZE));
CTASSERT(powerof2(SBUF_MAXEXTENDINCR));
#endif

static int
sbuf_extendsize(int size)
{
	int newsize;

	if (size < (int)SBUF_MAXEXTENDSIZE) {
		newsize = SBUF_MINEXTENDSIZE;
		while (newsize < size)
			newsize *= 2;
	} else {
		newsize = roundup2(size, SBUF_MAXEXTENDINCR);
	}
	KASSERT(newsize >= size, ("%s: %d < %d\n", __func__, newsize, size));
	return (newsize);
}

/*
 * Extend an sbuf.
 */
static int
sbuf_extend(struct sbuf *s, int addlen)
{
	char *newbuf;
	int newsize;

	if (!SBUF_CANEXTEND(s))
		return (-1);
	newsize = sbuf_extendsize(s->s_size + addlen);
	newbuf = SBMALLOC(newsize);
	if (newbuf == NULL)
		return (-1);
	memcpy(newbuf, s->s_buf, s->s_size);
	if (SBUF_ISDYNAMIC(s))
		SBFREE(s->s_buf);
	else
		SBUF_SETFLAG(s, SBUF_DYNAMIC);
	s->s_buf = newbuf;
	s->s_size = newsize;
	return (0);
}

/*
 * Initialize the internals of an sbuf.
 * If buf is non-NULL, it points to a static or already-allocated string
 * big enough to hold at least length characters.
 */
static struct sbuf *
sbuf_newbuf(struct sbuf *s, char *buf, int length, int flags)
{

	memset(s, 0, sizeof(*s));
	s->s_flags = flags;
	s->s_size = length;
	s->s_buf = buf;

	if ((s->s_flags & SBUF_AUTOEXTEND) == 0) {
		KASSERT(s->s_size >= SBUF_MINSIZE,
		    ("attempt to create an sbuf smaller than %d bytes",
		    SBUF_MINSIZE));
	}

	if (s->s_buf != NULL)
		return (s);

	if ((flags & SBUF_AUTOEXTEND) != 0)
		s->s_size = sbuf_extendsize(s->s_size);

	s->s_buf = SBMALLOC(s->s_size);
	if (s->s_buf == NULL)
		return (NULL);
	SBUF_SETFLAG(s, SBUF_DYNAMIC);
	return (s);
}

/*
 * Initialize an sbuf.
 * If buf is non-NULL, it points to a static or already-allocated string
 * big enough to hold at least length characters.
 */
struct sbuf *
sbuf_new(struct sbuf *s, char *buf, int length, int flags)
{

	KASSERT(length >= 0,
	    ("attempt to create an sbuf of negative length (%d)", length));
	KASSERT((flags & ~SBUF_USRFLAGMSK) == 0,
	    ("%s called with invalid flags", __func__));

	flags &= SBUF_USRFLAGMSK;
	if (s != NULL)
		return (sbuf_newbuf(s, buf, length, flags));

	s = SBMALLOC(sizeof(*s));
	if (s == NULL)
		return (NULL);
	if (sbuf_newbuf(s, buf, length, flags) == NULL) {
		SBFREE(s);
		return (NULL);
	}
	SBUF_SETFLAG(s, SBUF_DYNSTRUCT);
	return (s);
}

#ifdef _KERNEL
/*
 * Create an sbuf with uio data
 */
struct sbuf *
sbuf_uionew(struct sbuf *s, struct uio *uio, int *error)
{

	KASSERT(uio != NULL,
	    ("%s called with NULL uio pointer", __func__));
	KASSERT(error != NULL,
	    ("%s called with NULL error pointer", __func__));

	s = sbuf_new(s, NULL, uio->uio_resid + 1, 0);
	if (s == NULL) {
		*error = ENOMEM;
		return (NULL);
	}
	*error = uiomove(s->s_buf, uio->uio_resid, uio);
	if (*error != 0) {
		sbuf_delete(s);
		return (NULL);
	}
	s->s_len = s->s_size - 1;
	if (SBUF_ISSECTION(s))
		s->s_sect_len = s->s_size - 1;
	*error = 0;
	return (s);
}
#endif

int
sbuf_get_flags(struct sbuf *s)
{

	return (s->s_flags & SBUF_USRFLAGMSK);
}

void
sbuf_clear_flags(struct sbuf *s, int flags)
{

	s->s_flags &= ~(flags & SBUF_USRFLAGMSK);
}

void
sbuf_set_flags(struct sbuf *s, int flags)
{


	s->s_flags |= (flags & SBUF_USRFLAGMSK);
}

/*
 * Clear an sbuf and reset its position.
 */
void
sbuf_clear(struct sbuf *s)
{

	assert_sbuf_integrity(s);
	/* don't care if it's finished or not */

	SBUF_CLEARFLAG(s, SBUF_FINISHED);
	s->s_error = 0;
	s->s_len = 0;
	s->s_rec_off = 0;
	s->s_sect_len = 0;
}

/*
 * Set the sbuf's end position to an arbitrary value.
 * Effectively truncates the sbuf at the new position.
 */
int
sbuf_setpos(struct sbuf *s, ssize_t pos)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	KASSERT(pos >= 0,
	    ("attempt to seek to a negative position (%jd)", (intmax_t)pos));
	KASSERT(pos < s->s_size,
	    ("attempt to seek past end of sbuf (%jd >= %jd)",
	    (intmax_t)pos, (intmax_t)s->s_size));
	KASSERT(!SBUF_ISSECTION(s),
	    ("attempt to seek when in a section"));

	if (pos < 0 || pos > s->s_len)
		return (-1);
	s->s_len = pos;
	return (0);
}

/*
 * Set up a drain function and argument on an sbuf to flush data to
 * when the sbuf buffer overflows.
 */
void
sbuf_set_drain(struct sbuf *s, sbuf_drain_func *func, void *ctx)
{

	assert_sbuf_state(s, 0);
	assert_sbuf_integrity(s);
	KASSERT(func == s->s_drain_func || s->s_len == 0,
	    ("Cannot change drain to %p on non-empty sbuf %p", func, s));
	s->s_drain_func = func;
	s->s_drain_arg = ctx;
}

/*
 * Call the drain and process the return.
 */
static int
sbuf_drain(struct sbuf *s)
{
	int len;

	KASSERT(s->s_len > 0, ("Shouldn't drain empty sbuf %p", s));
	KASSERT(s->s_error == 0, ("Called %s with error on %p", __func__, s));
	if (SBUF_DODRAINTOEOR(s) && s->s_rec_off == 0)
		return (s->s_error = EDEADLK);
	len = s->s_drain_func(s->s_drain_arg, s->s_buf,
	    SBUF_DODRAINTOEOR(s) ? s->s_rec_off : s->s_len);
	if (len <= 0) {
		s->s_error = len ? -len : EDEADLK;
		return (s->s_error);
	}
	KASSERT(len > 0 && len <= s->s_len,
	    ("Bad drain amount %d for sbuf %p", len, s));
	s->s_len -= len;
	s->s_rec_off -= len;
	/*
	 * Fast path for the expected case where all the data was
	 * drained.
	 */
	if (s->s_len == 0)
		return (0);
	/*
	 * Move the remaining characters to the beginning of the
	 * string.
	 */
	memmove(s->s_buf, s->s_buf + len, s->s_len);
	return (0);
}

/*
 * Append bytes to an sbuf.  This is the core function for appending
 * to an sbuf and is the main place that deals with extending the
 * buffer and marking overflow.
 */
static void
sbuf_put_bytes(struct sbuf *s, const char *buf, size_t len)
{
	size_t n;

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	if (s->s_error != 0)
		return;
	while (len > 0) {
		if (SBUF_FREESPACE(s) <= 0) {
			/*
			 * If there is a drain, use it, otherwise extend the
			 * buffer.
			 */
			if (s->s_drain_func != NULL)
				(void)sbuf_drain(s);
			else if (sbuf_extend(s, len > INT_MAX ? INT_MAX : len)
			    < 0)
				s->s_error = ENOMEM;
			if (s->s_error != 0)
				return;
		}
		n = SBUF_FREESPACE(s);
		if (len < n)
			n = len;
		memcpy(&s->s_buf[s->s_len], buf, n);
		s->s_len += n;
		if (SBUF_ISSECTION(s))
			s->s_sect_len += n;
		len -= n;
		buf += n;
	}
}

static void
sbuf_put_byte(struct sbuf *s, char c)
{

	sbuf_put_bytes(s, &c, 1);
}

/*
 * Append a byte string to an sbuf.
 */
int
sbuf_bcat(struct sbuf *s, const void *buf, size_t len)
{

	sbuf_put_bytes(s, buf, len);
	if (s->s_error != 0)
		return (-1);
	return (0);
}

#ifdef _KERNEL
/*
 * Copy a byte string from userland into an sbuf.
 */
int
sbuf_bcopyin(struct sbuf *s, const void *uaddr, size_t len)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);
	KASSERT(s->s_drain_func == NULL,
	    ("Nonsensical copyin to sbuf %p with a drain", s));

	if (s->s_error != 0)
		return (-1);
	if (len == 0)
		return (0);
	if (len > SBUF_FREESPACE(s)) {
		sbuf_extend(s, len - SBUF_FREESPACE(s));
		if (SBUF_FREESPACE(s) < len)
			len = SBUF_FREESPACE(s);
	}
	if (copyin(uaddr, s->s_buf + s->s_len, len) != 0)
		return (-1);
	s->s_len += len;

	return (0);
}
#endif

/*
 * Copy a byte string into an sbuf.
 */
int
sbuf_bcpy(struct sbuf *s, const void *buf, size_t len)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	sbuf_clear(s);
	return (sbuf_bcat(s, buf, len));
}

/*
 * Append a string to an sbuf.
 */
int
sbuf_cat(struct sbuf *s, const char *str)
{
	size_t n;

	n = strlen(str);
	sbuf_put_bytes(s, str, n);
	if (s->s_error != 0)
		return (-1);
	return (0);
}

#ifdef _KERNEL
/*
 * Append a string from userland to an sbuf.
 */
int
sbuf_copyin(struct sbuf *s, const void *uaddr, size_t len)
{
	size_t done;

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);
	KASSERT(s->s_drain_func == NULL,
	    ("Nonsensical copyin to sbuf %p with a drain", s));

	if (s->s_error != 0)
		return (-1);

	if (len == 0)
		len = SBUF_FREESPACE(s);	/* XXX return 0? */
	if (len > SBUF_FREESPACE(s)) {
		sbuf_extend(s, len);
		if (SBUF_FREESPACE(s) < len)
			len = SBUF_FREESPACE(s);
	}
	switch (copyinstr(uaddr, s->s_buf + s->s_len, len + 1, &done)) {
	case ENAMETOOLONG:
		s->s_error = ENOMEM;
		/* fall through */
	case 0:
		s->s_len += done - 1;
		if (SBUF_ISSECTION(s))
			s->s_sect_len += done - 1;
		break;
	default:
		return (-1);	/* XXX */
	}

	return (done);
}
#endif

/*
 * Copy a string into an sbuf.
 */
int
sbuf_cpy(struct sbuf *s, const char *str)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	sbuf_clear(s);
	return (sbuf_cat(s, str));
}

/*
 * Format the given argument list and append the resulting string to an sbuf.
 */
#ifdef _KERNEL

/*
 * Append a non-NUL character to an sbuf.  This prototype signature is
 * suitable for use with kvprintf(9).
 */
static void
sbuf_putc_func(int c, void *arg)
{

	if (c != '\0')
		sbuf_put_byte(arg, c);
}

int
sbuf_vprintf(struct sbuf *s, const char *fmt, va_list ap)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	KASSERT(fmt != NULL,
	    ("%s called with a NULL format string", __func__));

	(void)kvprintf(fmt, sbuf_putc_func, s, 10, ap);
	if (s->s_error != 0)
		return (-1);
	return (0);
}
#else /* !_KERNEL */
int
sbuf_vprintf(struct sbuf *s, const char *fmt, va_list ap)
{
	va_list ap_copy;
	int error, len;

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	KASSERT(fmt != NULL,
	    ("%s called with a NULL format string", __func__));

	if (s->s_error != 0)
		return (-1);

	/*
	 * For the moment, there is no way to get vsnprintf(3) to hand
	 * back a character at a time, to push everything into
	 * sbuf_putc_func() as was done for the kernel.
	 *
	 * In userspace, while drains are useful, there's generally
	 * not a problem attempting to malloc(3) on out of space.  So
	 * expand a userland sbuf if there is not enough room for the
	 * data produced by sbuf_[v]printf(3).
	 */

	error = 0;
	do {
		va_copy(ap_copy, ap);
		len = vsnprintf(&s->s_buf[s->s_len], SBUF_FREESPACE(s) + 1,
		    fmt, ap_copy);
		if (len < 0) {
			s->s_error = errno;
			return (-1);
		}
		va_end(ap_copy);

		if (SBUF_FREESPACE(s) >= len)
			break;
		/* Cannot print with the current available space. */
		if (s->s_drain_func != NULL && s->s_len > 0)
			error = sbuf_drain(s); /* sbuf_drain() sets s_error. */
		else if (sbuf_extend(s, len - SBUF_FREESPACE(s)) != 0)
			s->s_error = error = ENOMEM;
	} while (error == 0);

	/*
	 * s->s_len is the length of the string, without the terminating nul.
	 * When updating s->s_len, we must subtract 1 from the length that
	 * we passed into vsnprintf() because that length includes the
	 * terminating nul.
	 *
	 * vsnprintf() returns the amount that would have been copied,
	 * given sufficient space, so don't over-increment s_len.
	 */
	if (SBUF_FREESPACE(s) < len)
		len = SBUF_FREESPACE(s);
	s->s_len += len;
	if (SBUF_ISSECTION(s))
		s->s_sect_len += len;

	KASSERT(s->s_len < s->s_size,
	    ("wrote past end of sbuf (%d >= %d)", s->s_len, s->s_size));

	if (s->s_error != 0)
		return (-1);
	return (0);
}
#endif /* _KERNEL */

/*
 * Format the given arguments and append the resulting string to an sbuf.
 */
int
sbuf_printf(struct sbuf *s, const char *fmt, ...)
{
	va_list ap;
	int result;

	va_start(ap, fmt);
	result = sbuf_vprintf(s, fmt, ap);
	va_end(ap);
	return (result);
}

/*
 * Append a character to an sbuf.
 */
int
sbuf_putc(struct sbuf *s, int c)
{

	sbuf_put_byte(s, c);
	if (s->s_error != 0)
		return (-1);
	return (0);
}

/*
 * Trim whitespace characters from end of an sbuf.
 */
int
sbuf_trim(struct sbuf *s)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);
	KASSERT(s->s_drain_func == NULL,
	    ("%s makes no sense on sbuf %p with drain", __func__, s));

	if (s->s_error != 0)
		return (-1);

	while (s->s_len > 0 && isspace(s->s_buf[s->s_len-1])) {
		--s->s_len;
		if (SBUF_ISSECTION(s))
			s->s_sect_len--;
	}

	return (0);
}

/*
 * Check if an sbuf has an error.
 */
int
sbuf_error(const struct sbuf *s)
{

	return (s->s_error);
}

/*
 * Finish off an sbuf.
 */
int
sbuf_finish(struct sbuf *s)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	s->s_buf[s->s_len] = '\0';
	if (SBUF_NULINCLUDED(s))
		s->s_len++;
	if (s->s_drain_func != NULL) {
		while (s->s_len > 0 && s->s_error == 0)
			s->s_error = sbuf_drain(s);
	}
	SBUF_SETFLAG(s, SBUF_FINISHED);
#ifdef _KERNEL
	return (s->s_error);
#else
	if (s->s_error != 0) {
		errno = s->s_error;
		return (-1);
	}
	return (0);
#endif
}

/*
 * Return a pointer to the sbuf data.
 */
char *
sbuf_data(struct sbuf *s)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, SBUF_FINISHED);
	KASSERT(s->s_drain_func == NULL,
	    ("%s makes no sense on sbuf %p with drain", __func__, s));

	return (s->s_buf);
}

/*
 * Return the length of the sbuf data.
 */
ssize_t
sbuf_len(struct sbuf *s)
{

	assert_sbuf_integrity(s);
	/* don't care if it's finished or not */
	KASSERT(s->s_drain_func == NULL,
	    ("%s makes no sense on sbuf %p with drain", __func__, s));

	if (s->s_error != 0)
		return (-1);

	/* If finished, nulterm is already in len, else add one. */
	if (SBUF_NULINCLUDED(s) && !SBUF_ISFINISHED(s))
		return (s->s_len + 1);
	return (s->s_len);
}

/*
 * Clear an sbuf, free its buffer if necessary.
 */
void
sbuf_delete(struct sbuf *s)
{
	int isdyn;

	assert_sbuf_integrity(s);
	/* don't care if it's finished or not */

	if (SBUF_ISDYNAMIC(s))
		SBFREE(s->s_buf);
	isdyn = SBUF_ISDYNSTRUCT(s);
	memset(s, 0, sizeof(*s));
	if (isdyn)
		SBFREE(s);
}

/*
 * Check if an sbuf has been finished.
 */
int
sbuf_done(const struct sbuf *s)
{

	return (SBUF_ISFINISHED(s));
}

/*
 * Start a section.
 */
void
sbuf_start_section(struct sbuf *s, ssize_t *old_lenp)
{

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);

	if (!SBUF_ISSECTION(s)) {
		KASSERT(s->s_sect_len == 0,
		    ("s_sect_len != 0 when starting a section"));
		if (old_lenp != NULL)
			*old_lenp = -1;
		s->s_rec_off = s->s_len;
		SBUF_SETFLAG(s, SBUF_INSECTION);
	} else {
		KASSERT(old_lenp != NULL,
		    ("s_sect_len should be saved when starting a subsection"));
		*old_lenp = s->s_sect_len;
		s->s_sect_len = 0;
	}
}

/*
 * End the section padding to the specified length with the specified
 * character.
 */
ssize_t
sbuf_end_section(struct sbuf *s, ssize_t old_len, size_t pad, int c)
{
	ssize_t len;

	assert_sbuf_integrity(s);
	assert_sbuf_state(s, 0);
	KASSERT(SBUF_ISSECTION(s),
	    ("attempt to end a section when not in a section"));

	if (pad > 1) {
		len = roundup(s->s_sect_len, pad) - s->s_sect_len;
		for (; s->s_error == 0 && len > 0; len--)
			sbuf_put_byte(s, c);
	}
	len = s->s_sect_len;
	if (old_len == -1) {
		s->s_rec_off = s->s_sect_len = 0;
		SBUF_CLEARFLAG(s, SBUF_INSECTION);
	} else {
		s->s_sect_len += old_len;
	}
	if (s->s_error != 0)
		return (-1);
	return (len);
}
