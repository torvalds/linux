/*	$OpenBSD: tib.h,v 1.10 2023/12/08 19:14:36 miod Exp $	*/
/*
 * Copyright (c) 2011,2014 Philip Guenther <guenther@openbsd.org>
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
 * Thread Information Block (TIB) and Thread Local Storage (TLS) handling
 * (the TCB, Thread Control Block, is part of the TIB)
 */

#ifndef	_TIB_H_
#define	_TIB_H_

#include <sys/types.h>
#include <machine/tcb.h>

#include <stddef.h>


/*
 * This header defines struct tib and at least eight macros:
 *	TLS_VARIANT
 *		Either 1 or 2  (Actually defined by <machine/tcb.h>)
 *
 *	TCB_SET(tcb)
 *		Set the TCB pointer for this thread to 'tcb'
 *
 *	TCB_GET()
 *		Return the TCB pointer for this thread
 *
 *	TCB_TO_TIB(tcb)
 *		Given a TCB pointer, return the matching TIB pointer
 *
 *	TIB_TO_TCB(tib)
 *		Given a TIB pointer, return the matching TCB pointer
 *
 *	TIB_INIT(tib, dtv, thread)
 *		Initializes a TIB for a new thread, using the supplied
 *		values for its dtv and thread pointers
 *
 *	TIB_GET()
 *		Short-hand for TCB_TO_TIB(TCB_GET())
 *
 *	TIB_EXTRA_ALIGN
 *		On TLS variant 2 archs, what alignment is sufficient
 *		for the extra space that will be used for struct pthread?
 *
 * The following functions are provided by either ld.so (dynamic) or
 * libc (static) for allocating and freeing a common memory block that
 * will hold both the TIB and the pthread structure:
 *	_dl_allocate_tib(sizeof(struct pthread))
 *		Allocates a combined TIB and pthread memory region.
 *		The argument is the amount of space to reserve
 *		for the pthread structure.  Returns a pointer to
 *		the TIB inside the allocated block.
 *
 * 	_dl_free_tib(tib, sizeof(struct pthread))
 *		Frees a TIB and pthread block previously allocated
 *		with _dl_allocate_tib().  Must be passed the return
 *		value of that previous call.
 */

/*
 * Regarding <machine/tcb.h>:
 *  - it must define the TLS_VARIANT macro
 *  - it may define TCB_OFFSET if the TCB address in the kernel and/or
 *    register is offset from the actual TCB address.  TCB_OFFSET > 0
 *    means the kernel/register points to *after* the real data.
 *  - if there's a faster way to get or set the TCB pointer for the thread
 *    than the __{get,set}_tcb() syscalls, it should define either or both
 *    the TCB_{GET,SET} macros to do so.
 */


/* All archs but mips64 have fast TCB_GET() and don't need caching */
#ifndef	__mips64__
# define TCB_HAVE_MD_GET	1
#endif
#ifdef	TCB_SET
# define TCB_HAVE_MD_SET	1
#else
# define TCB_SET(tcb)		__set_tcb(tcb)
#endif
#ifndef TCB_OFFSET
# define TCB_OFFSET	0
#endif

/*
 * tib_cantcancel values is non-zero if the thread should skip all
 * cancellation processing
 */
#define CANCEL_DISABLED	1
#define CANCEL_DYING	2

/*
 * tib_cancel_point is non-zero if we're in a cancel point; its modified
 * by the cancel point code and read by the cancellation signal handler
 */
#define CANCEL_POINT		1
#define CANCEL_POINT_DELAYED	2


#if TLS_VARIANT == 1
/*
 * ABI specifies that the static TLS data starts two words after the
 * (notional) thread pointer, with the first of those two words being
 * the TLS dtv pointer.  The other (second) word is reserved for the
 * implementation, so we place the pointer to the thread structure there,
 * but we place our actual thread bits before the TCB, at negative offsets
 * from the TCB pointer.  Ergo, memory is laid out, low to high, as:
 *
 *	[pthread structure]
 *	TIB {
 *		...cancelation and other int-sized info...
 *		int errno
 *		void *locale
 *		TCB (- TCB_OFFSET) {
 *			void *dtv
 *			struct pthread *thread
 *		}
 *	}
 *	static TLS data
 */

struct tib {
	void	*tib_atexit;
	int	tib_thread_flags;	/* internal to libpthread */
	pid_t	tib_tid;
	int	tib_cantcancel;
	int	tib_cancel_point;
	int	tib_canceled;
	int	tib_errno;
	void	*tib_locale;
#ifdef __powerpc64__
	void	*tib_thread;
	void	*tib_dtv;		/* internal to the runtime linker */
#else
	void	*tib_dtv;		/* internal to the runtime linker */
	void	*tib_thread;
#endif
};


#elif TLS_VARIANT == 2
/*
 * ABI specifies that the static TLS data occupies the memory before
 * the TCB pointer, at negative offsets, and that on i386 and amd64
 * the word the TCB points to contains a pointer to itself.  So,
 * we place errno and our thread bits after that.  Memory is laid
 * out, low to high, as:
 *	static TLS data
 *	TIB {
 *		TCB (- TCB_OFFSET) {
 *			self pointer [i386/amd64 only]
 *			void *dtv
 *		}
 *		struct pthread *thread
 *		void *locale
 *		int errno
 *		...cancelation and other int-sized info...
 *	}
 *	[pthread structure]
 */

struct tib {
#if defined(__i386) || defined(__amd64)
	struct	tib *__tib_self;
# define __tib_tcb __tib_self
#endif
	void	*tib_dtv;		/* internal to the runtime linker */
	void	*tib_thread;
	void	*tib_locale;
	int	tib_errno;
	int	tib_canceled;
	int	tib_cancel_point;
	int	tib_cantcancel;
	pid_t	tib_tid;
	int	tib_thread_flags;	/* internal to libpthread */
	void	*tib_atexit;
};

#if defined(__i386) || defined(__amd64)
# define _TIB_PREP(tib)	\
	((void)((tib)->__tib_self = (tib)))
#endif

#define	TIB_EXTRA_ALIGN		sizeof(void *)

#else
# error "unknown TLS variant"
#endif

/* nothing to do by default */
#ifndef	_TIB_PREP
# define _TIB_PREP(tib)	((void)0)
#endif

#define	TIB_INIT(tib, dtv, thread)	do {		\
		(tib)->tib_thread	= (thread);	\
		(tib)->tib_atexit	= NULL;		\
		(tib)->tib_locale	= NULL;		\
		(tib)->tib_cantcancel	= 0;		\
		(tib)->tib_cancel_point	= 0;		\
		(tib)->tib_canceled	= 0;		\
		(tib)->tib_dtv		= (dtv);	\
		(tib)->tib_errno	= 0;		\
		(tib)->tib_thread_flags = 0;		\
		_TIB_PREP(tib);				\
	} while (0)

#ifndef	__tib_tcb
# define __tib_tcb		tib_dtv
#endif
#define	_TIBO_TCB		(offsetof(struct tib, __tib_tcb) + TCB_OFFSET)

#define	TCB_TO_TIB(tcb)		((struct tib *)((char *)(tcb) - _TIBO_TCB))
#define	TIB_TO_TCB(tib)		((char *)(tib) + _TIBO_TCB)
#define	TIB_GET()		TCB_TO_TIB(TCB_GET())


__BEGIN_DECLS
struct dl_info;
struct dl_phdr_info;
struct dl_cb_0 {
	void	*(*dl_allocate_tib)(size_t);
	void	 (*dl_free_tib)(void *, size_t);
	void	 (*dl_clean_boot)(void);
	void	*(*dlopen)(const char *, int);
	int	 (*dlclose)(void *);
	void	*(*dlsym)(void *, const char *);
	int	 (*dladdr)(const void *, struct dl_info *);
	int	 (*dlctl)(void *, int, void *);
	char	*(*dlerror)(void);
	int	 (*dl_iterate_phdr)(int (*)(struct dl_phdr_info *,
		    size_t, void *), void *);
};

#define	DL_CB_CUR	0
typedef	struct dl_cb_0	dl_cb;

/* type of function passed to init functions that returns a dl_cb */
typedef	const void *dl_cb_cb(int _version);

void	*_dl_allocate_tib(size_t _extra) __dso_public;
void	_dl_free_tib(void *_tib, size_t _extra) __dso_public;

/* The actual syscalls */
void	*__get_tcb(void);
void	__set_tcb(void *_tcb);
__END_DECLS

#endif /* _TIB_H_ */
