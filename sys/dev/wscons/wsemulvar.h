/* $OpenBSD: wsemulvar.h,v 1.20 2024/11/05 08:12:08 miod Exp $ */
/* $NetBSD: wsemulvar.h,v 1.6 1999/01/17 15:46:15 drochner Exp $ */

/*
 * Copyright (c) 2009 Miodrag Vallat.
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
 * Copyright (c) 1996, 1997 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef	_KERNEL

#include <dev/wscons/wscons_features.h>

struct device;
struct wsdisplay_emulops;

enum wsemul_resetops {
	WSEMUL_RESET,
	WSEMUL_SYNCFONT,
	WSEMUL_CLEARSCREEN,
	WSEMUL_CLEARCURSOR
};

struct wsemul_ops {
	char name[WSEMUL_NAME_SIZE];

	void	*(*cnattach)(const struct wsscreen_descr *, void *,
				  int, int, uint32_t);
	void	*(*attach)(int, const struct wsscreen_descr *, void *,
				int, int, void *, uint32_t);
	u_int	(*output)(void *, const u_char *, u_int, int);
	int	(*translate)(void *, kbd_t, keysym_t, const u_char **);
	void	(*detach)(void *, u_int *, u_int *);
	void    (*reset)(void *, enum wsemul_resetops);
};

/*
 * Structure carrying the state of multi-byte character sequences
 * decoding.
 */
struct wsemul_inputstate {
	uint32_t	inchar;	/* character being reconstructed */
	uint32_t	lbound;	/* lower bound of above */
	u_int		mbleft;	/* multibyte bytes left until char complete */

	uint32_t	last_output;	/* last printable character */
					/* (used by vt100 emul only) */
};

extern const struct wsemul_ops wsemul_dumb_ops;
extern const struct wsemul_ops wsemul_sun_ops;
extern const struct wsemul_ops wsemul_vt100_ops;

const struct wsemul_ops *wsemul_pick(const char *);
const char *wsemul_getname(int);

/*
 * Callbacks from the emulation code to the display interface driver.
 */
void	wsdisplay_emulbell(void *v);
void	wsdisplay_emulinput(void *v, const u_char *, u_int);

/*
 * Get characters from an input stream and update the input state.
 * Processing stops when the stream is empty, or a complete character
 * sequence has been recognized, in which case it returns zero.
 */
int	wsemul_getchar(const u_char **, u_int *, struct wsemul_inputstate *,
	    int);

/*
 * Keysym to UTF-8 sequence translation function.
 */
int	wsemul_utf8_translate(u_int32_t, kbd_t, u_char *, int);

/*
 * emulops failure abort/recovery state
 *
 * The tty layer needs a character output to be atomic.  Since this may
 * expand to multiple emulops operations, which may fail, it is necessary
 * for each emulation code to keep state of its current processing, so
 * that if an operation fails, the whole character from the tty layer is
 * reported as not having been output, while it has in fact been partly
 * processed.
 *
 * When the tty layer will try to retransmit the character, this state
 * information is used to not retrig the emulops which have been issued
 * successfully already.
 *
 * In order to make things more confusing, there is a particular failure
 * case, when all characters have been processed successfully, but
 * displaying the cursor image fails.
 *
 * Since there might not be tty output in a while, we need to report
 * failure, so we pretend not having been able to issue the last character.
 * When the tty layer tries again to display this character (really to get
 * the cursor image back), it will directly be skipped. This is done with
 * a special state value.
 */

struct wsemul_abortstate {
	enum {
		ABORT_OK,
		ABORT_FAILED_CURSOR,
		ABORT_FAILED_JUMP_SCROLL,
		ABORT_FAILED_OTHER
	} state;
	int	skip;	/* emulops to skip before reaching resume point */
	int	done;	/* emulops completed */
	int	lines;	/* jump scroll lines */
};

/* start character processing, assuming cursor or jump scroll failure condition
   has been taken care of */
static inline void
wsemul_resume_abort(struct wsemul_abortstate *was)
{
	was->state = ABORT_OK;
	was->done = 0;
}

/* register processing failure points */
static inline void
wsemul_abort_cursor(struct wsemul_abortstate *was)
{
	was->state = ABORT_FAILED_CURSOR;
}

static inline void
wsemul_abort_jump_scroll(struct wsemul_abortstate *was, int lines)
{
	was->state = ABORT_FAILED_JUMP_SCROLL;
	was->skip = was->done;
	was->lines = lines;
}

static inline void
wsemul_abort_other(struct wsemul_abortstate *was)
{
	was->state = ABORT_FAILED_OTHER;
	was->skip = was->done;
}

/* initialize abortstate structure */
static inline void
wsemul_reset_abortstate(struct wsemul_abortstate *was)
{
	was->state = ABORT_OK;
	was->skip = 0;
	/* was->done = 0; */
}

/*
 * Wrapper macro to handle failing emulops calls consistently.
 */

#ifdef HAVE_RESTARTABLE_EMULOPS
#define	WSEMULOP(rc, edp, was, rutin, args) \
do { \
	if ((was)->skip != 0) { \
		(was)->skip--; \
		(rc) = 0; \
	} else { \
		(rc) = (*(edp)->emulops->rutin) args ; \
	} \
	if ((rc) == 0) \
		(was)->done++; \
} while (0)
#else
#define	WSEMULOP(rc, edp, was, rutin, args) \
do { \
	(void)(*(edp)->emulops->rutin) args ; \
	(rc) = 0; \
} while(0)
#endif

#endif	/* _KERNEL */
