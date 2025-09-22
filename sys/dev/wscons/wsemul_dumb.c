/* $OpenBSD: wsemul_dumb.c,v 1.14 2020/05/25 09:55:49 jsg Exp $ */
/* $NetBSD: wsemul_dumb.c,v 1.7 2000/01/05 11:19:36 drochner Exp $ */

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsemulvar.h>
#include <dev/wscons/ascii.h>

void	*wsemul_dumb_cnattach(const struct wsscreen_descr *, void *,
				   int, int, uint32_t);
void	*wsemul_dumb_attach(int, const struct wsscreen_descr *,
				 void *, int, int, void *, uint32_t);
u_int	wsemul_dumb_output(void *, const u_char *, u_int, int);
int	wsemul_dumb_translate(void *, kbd_t, keysym_t, const u_char **);
void	wsemul_dumb_detach(void *, u_int *, u_int *);
void	wsemul_dumb_resetop(void *, enum wsemul_resetops);

const struct wsemul_ops wsemul_dumb_ops = {
	"dumb",
	wsemul_dumb_cnattach,
	wsemul_dumb_attach,
	wsemul_dumb_output,
	wsemul_dumb_translate,
	wsemul_dumb_detach,
	wsemul_dumb_resetop
};

struct wsemul_dumb_emuldata {
	const struct wsdisplay_emulops *emulops;
	struct wsemul_abortstate abortstate;
	void *emulcookie;
	void *cbcookie;
	int crippled;
	u_int nrows, ncols, crow, ccol;
	uint32_t defattr;
};

struct wsemul_dumb_emuldata wsemul_dumb_console_emuldata;

void *
wsemul_dumb_cnattach(const struct wsscreen_descr *type, void *cookie, int ccol,
    int crow, uint32_t defattr)
{
	struct wsemul_dumb_emuldata *edp;
	const struct wsdisplay_emulops *emulops;

	edp = &wsemul_dumb_console_emuldata;

	edp->emulops = emulops = type->textops;
	edp->emulcookie = cookie;
	edp->nrows = type->nrows;
	edp->ncols = type->ncols;
	edp->crow = crow;
	edp->ccol = ccol;
	edp->defattr = defattr;
	edp->cbcookie = NULL;
	edp->crippled = emulops->cursor == NULL ||
	    emulops->copycols == NULL || emulops->copyrows == NULL ||
	    emulops->erasecols == NULL || emulops->eraserows == NULL;
	wsemul_reset_abortstate(&edp->abortstate);

	return (edp);
}

void *
wsemul_dumb_attach(int console, const struct wsscreen_descr *type, void *cookie,
    int ccol, int crow, void *cbcookie, uint32_t defattr)
{
	struct wsemul_dumb_emuldata *edp;

	if (console)
		edp = &wsemul_dumb_console_emuldata;
	else {
		edp = malloc(sizeof *edp, M_DEVBUF, M_WAITOK);

		edp->emulops = type->textops;
		edp->emulcookie = cookie;
		edp->nrows = type->nrows;
		edp->ncols = type->ncols;
		edp->crow = crow;
		edp->ccol = ccol;
		edp->defattr = defattr;
		wsemul_reset_abortstate(&edp->abortstate);
	}

	edp->cbcookie = cbcookie;

	return (edp);
}

u_int
wsemul_dumb_output(void *cookie, const u_char *data, u_int count, int kernel)
{
	struct wsemul_dumb_emuldata *edp = cookie;
	u_int processed = 0;
	u_char c;
	int n;
	int rc = 0;

	if (edp->crippled) {
		while (count-- > 0) {
			wsemul_resume_abort(&edp->abortstate);

			c = *data++;
			if (c == ASCII_BEL)
				wsdisplay_emulbell(edp->cbcookie);
			else {
				WSEMULOP(rc, edp, &edp->abortstate, putchar,
				    (edp->emulcookie, 0, 0, c, 0));
				if (rc != 0)
					break;
			}
			processed++;
		}
		if (rc != 0)
			wsemul_abort_other(&edp->abortstate);
		return processed;
	}

	switch (edp->abortstate.state) {
	case ABORT_FAILED_CURSOR:
		/*
		 * If we could not display the cursor back, we pretended not
		 * having been able to display the last character. But this
		 * is a lie, so compensate here.
		 */
		data++, count--;
		processed++;
		wsemul_reset_abortstate(&edp->abortstate);
		break;
	case ABORT_OK:
		/* remove cursor image */
		rc = (*edp->emulops->cursor)
		    (edp->emulcookie, 0, edp->crow, edp->ccol);
		if (rc != 0)
			return 0;
		break;
	default:
		break;
	}

	while (count-- > 0) {
		wsemul_resume_abort(&edp->abortstate);

		c = *data++;
		switch (c) {
		case ASCII_BEL:
			wsdisplay_emulbell(edp->cbcookie);
			break;

		case ASCII_BS:
			if (edp->ccol > 0)
				edp->ccol--;
			break;

		case ASCII_CR:
			edp->ccol = 0;
			break;

		case ASCII_HT:
			n = min(8 - (edp->ccol & 7),
			    edp->ncols - edp->ccol - 1);
			WSEMULOP(rc, edp, &edp->abortstate, erasecols,
			     (edp->emulcookie, edp->crow, edp->ccol, n,
			      edp->defattr));
			if (rc != 0)
				break;
			edp->ccol += n;
			break;

		case ASCII_FF:
			WSEMULOP(rc, edp, &edp->abortstate, eraserows,
			    (edp->emulcookie, 0, edp->nrows, edp->defattr));
			if (rc != 0)
				break;
			edp->ccol = 0;
			edp->crow = 0;
			break;

		case ASCII_VT:
			if (edp->crow > 0)
				edp->crow--;
			break;

		default:
			WSEMULOP(rc, edp, &edp->abortstate, putchar,
			    (edp->emulcookie, edp->crow, edp->ccol, c,
			     edp->defattr));
			if (rc != 0)
				break;
			edp->ccol++;

			/* if cur col is still on cur line, done. */
			if (edp->ccol < edp->ncols)
				break;

			/* wrap the column around. */
			edp->ccol = 0;

                	/* FALLTHROUGH */

		case ASCII_LF:
	                /* if the cur line isn't the last, incr and leave. */
			if (edp->crow < edp->nrows - 1) {
				edp->crow++;
				break;
			}
			n = 1;		/* number of lines to scroll */
			WSEMULOP(rc, edp, &edp->abortstate, copyrows,
			    (edp->emulcookie, n, 0, edp->nrows - n));
			if (rc == 0)
				WSEMULOP(rc, edp, &edp->abortstate, eraserows,
				    (edp->emulcookie, edp->nrows - n, n,
				     edp->defattr));
			if (rc != 0) {
				/* undo wrap-at-eol processing if necessary */
				if (c != ASCII_LF)
					edp->ccol = edp->ncols - 1;
				break;
			}
			edp->crow -= n - 1;
			break;
		}
		if (rc != 0)
			break;
		processed++;
	}

	if (rc != 0)
		wsemul_abort_other(&edp->abortstate);
	else {
		/* put cursor image back */
		rc = (*edp->emulops->cursor)
		    (edp->emulcookie, 1, edp->crow, edp->ccol);
		if (rc != 0) {
			/*
			 * Fail the last character output, remembering that
			 * only the cursor operation really needs to be done.
			 */
			wsemul_abort_cursor(&edp->abortstate);
			processed--;
		}
	}

	if (rc == 0)
		wsemul_reset_abortstate(&edp->abortstate);

	return processed;
}

int
wsemul_dumb_translate(void *cookie, kbd_t layout, keysym_t in,
    const u_char **out)
{
	return (0);
}

void
wsemul_dumb_detach(void *cookie, u_int *crowp, u_int *ccolp)
{
	struct wsemul_dumb_emuldata *edp = cookie;

	*crowp = edp->crow;
	*ccolp = edp->ccol;
	if (edp != &wsemul_dumb_console_emuldata)
		free(edp, M_DEVBUF, sizeof *edp);
}

void
wsemul_dumb_resetop(void *cookie, enum wsemul_resetops op)
{
	struct wsemul_dumb_emuldata *edp = cookie;

	if (edp->crippled)
		return;

	switch (op) {
	case WSEMUL_CLEARSCREEN:
		(*edp->emulops->eraserows)(edp->emulcookie, 0, edp->nrows,
					   edp->defattr);
		edp->ccol = edp->crow = 0;
		(*edp->emulops->cursor)(edp->emulcookie, 1, 0, 0);
		break;
	case WSEMUL_CLEARCURSOR:
		(*edp->emulops->cursor)(edp->emulcookie, 0,
		    edp->crow, edp->ccol);
		break;
	default:
		break;
	}
}
