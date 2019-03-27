/*-
 * Copyright (c) 2013-2014 Devin Teske <dteske@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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

#include <sys/types.h>

#define _BSD_SOURCE /* to get dprintf() prototype in stdio.h below */
#include <dialog.h>
#include <err.h>
#include <libutil.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string_m.h>
#include <unistd.h>

#include "dialog_util.h"
#include "dialogrc.h"
#include "dprompt.h"
#include "dpv.h"
#include "dpv_private.h"

#define FLABEL_MAX 1024

static int fheight = 0; /* initialized by dprompt_init() */
static char dprompt[PROMPT_MAX + 1] = "";
static char *dprompt_pos = (char *)(0); /* treated numerically */

/* Display characteristics */
#define FM_DONE 0x01
#define FM_FAIL 0x02
#define FM_PEND 0x04
static uint8_t dprompt_free_mask;
static char *done = NULL;
static char *fail = NULL;
static char *pend = NULL;
int display_limit = DISPLAY_LIMIT_DEFAULT;	/* Max entries to show */
int label_size    = LABEL_SIZE_DEFAULT;		/* Max width for labels */
int pbar_size     = PBAR_SIZE_DEFAULT;		/* Mini-progressbar size */
static int gauge_percent = 0;
static int done_size, done_lsize, done_rsize;
static int fail_size, fail_lsize, fail_rsize;
static int mesg_size, mesg_lsize, mesg_rsize;
static int pend_size, pend_lsize, pend_rsize;
static int pct_lsize, pct_rsize;
static void *gauge = NULL;
#define SPIN_SIZE 4
static char spin[SPIN_SIZE + 1] = "/-\\|";
static char msg[PROMPT_MAX + 1];
static char *spin_cp = spin;

/* Function prototypes */
static char	spin_char(void);
static int	dprompt_add_files(struct dpv_file_node *file_list,
		    struct dpv_file_node *curfile, int pct);

/*
 * Returns a pointer to the current spin character in the spin string and
 * advances the global position to the next character for the next call.
 */
static char
spin_char(void)
{
	char ch;

	if (*spin_cp == '\0')
		spin_cp = spin;
	ch = *spin_cp;

	/* Advance the spinner to the next char */
	if (++spin_cp >= (spin + SPIN_SIZE))
		spin_cp = spin;

	return (ch);
}

/*
 * Initialize heights and widths based on various strings and environment
 * variables (such as ENV_USE_COLOR).
 */
void
dprompt_init(struct dpv_file_node *file_list)
{
	uint8_t nls = 0;
	int len;
	int max_cols;
	int max_rows;
	int nthfile;
	int numlines;
	struct dpv_file_node *curfile;

	/*
	 * Initialize dialog(3) `colors' support and draw backtitle
	 */
	if (use_libdialog && !debug) {
		init_dialog(stdin, stdout);
		dialog_vars.colors = 1;
		if (backtitle != NULL) {
			dialog_vars.backtitle = (char *)backtitle;
			dlg_put_backtitle();
		}
	}

	/* Calculate width of dialog(3) or [X]dialog(1) --gauge box */
	dwidth = label_size + pbar_size + 9;

	/*
	 * Calculate height of dialog(3) or [X]dialog(1) --gauge box
	 */
	dheight = 5;
	max_rows = dialog_maxrows();
	/* adjust max_rows for backtitle and/or dialog(3) statusLine */
	if (backtitle != NULL)
		max_rows -= use_shadow ? 3 : 2;
	if (use_libdialog && use_shadow)
		max_rows -= 2;
	/* add lines for `-p text' */
	numlines = dialog_prompt_numlines(pprompt, 0);
	if (debug)
		warnx("`-p text' is %i line%s long", numlines,
		    numlines == 1 ? "" : "s");
	dheight += numlines;
	/* adjust dheight for various implementations */
	if (use_dialog) {
		dheight -= dialog_prompt_nlstate(pprompt);
		nls = dialog_prompt_nlstate(pprompt);
	} else if (use_xdialog) {
		if (pprompt == NULL || *pprompt == '\0')
			dheight++;
	} else if (use_libdialog) {
		if (pprompt != NULL && *pprompt != '\0')
			dheight--;
	}
	/* limit the number of display items (necessary per dialog(1,3)) */
	if (display_limit == 0 || display_limit > DPV_DISPLAY_LIMIT)
		display_limit = DPV_DISPLAY_LIMIT;
	/* verify fheight will fit (stop if we hit 1) */
	for (; display_limit > 0; display_limit--) {
		nthfile = numlines = 0;
		fheight = (int)dpv_nfiles > display_limit ?
		    (unsigned int)display_limit : dpv_nfiles;
		for (curfile = file_list; curfile != NULL;
		    curfile = curfile->next) {
			nthfile++;
			numlines += dialog_prompt_numlines(curfile->name, nls);
			if ((nthfile % display_limit) == 0) {
				if (numlines > fheight)
					fheight = numlines;
				numlines = nthfile = 0;
			}
		}
		if (numlines > fheight)
			fheight = numlines;
		if ((dheight + fheight +
		    (int)dialog_prompt_numlines(aprompt, use_dialog) -
		    (use_dialog ? (int)dialog_prompt_nlstate(aprompt) : 0))
		    <= max_rows)
			break;	
	}
	/* don't show any items if we run the risk of hitting a blank set */
	if ((max_rows - (use_shadow ? 5 : 4)) >= fheight)
		dheight += fheight;
	else
		fheight = 0;
	/* add lines for `-a text' */
	numlines = dialog_prompt_numlines(aprompt, use_dialog);
	if (debug)
		warnx("`-a text' is %i line%s long", numlines,
		    numlines == 1 ? "" : "s");
	dheight += numlines;

	/* If using Xdialog(1), adjust accordingly (based on testing) */
	if (use_xdialog)
		dheight += dheight / 4;

	/* For wide mode, long prefix (`pprompt') or append (`aprompt')
	 * strings will bump width */
	if (wide) {
		len = (int)dialog_prompt_longestline(pprompt, 0); /* !nls */
		if ((len + 4) > dwidth)
			dwidth = len + 4;
		len = (int)dialog_prompt_longestline(aprompt, 1); /* nls */
		if ((len + 4) > dwidth)
			dwidth = len + 4;
	}

	/* Enforce width constraints to maximum values */
	max_cols = dialog_maxcols();
	if (max_cols > 0 && dwidth > max_cols)
		dwidth = max_cols;

	/* Optimize widths to sane values*/
	if (pbar_size > dwidth - 9) {
		pbar_size = dwidth - 9;
		label_size = 0;
		/* -9 = "|  - [" ... "] |" */
	}
	if (pbar_size < 0)
		label_size = dwidth - 8;
		/* -8 = "|  " ... " -  |" */
	else if (label_size > (dwidth - pbar_size - 9) || wide)
		label_size = no_labels ? 0 : dwidth - pbar_size - 9;
		/* -9 = "| " ... " - [" ... "] |" */

	/* Hide labels if requested */
	if (no_labels)
		label_size = 0;

	/* Touch up the height (now that we know dwidth) */
	dheight += dialog_prompt_wrappedlines(pprompt, dwidth - 4, 0);
	dheight += dialog_prompt_wrappedlines(aprompt, dwidth - 4, 1);

	if (debug)
		warnx("dheight = %i dwidth = %i fheight = %i",
		    dheight, dwidth, fheight);

	/* Calculate left/right portions of % */
	pct_lsize = (pbar_size - 4) / 2; /* -4 == printf("%-3s%%", pct) */
	pct_rsize = pct_lsize;
	/* If not evenly divisible by 2, increment the right-side */
	if ((pct_rsize + pct_rsize + 4) != pbar_size)
		pct_rsize++;

	/* Initialize "Done" text */
	if (done == NULL && (done = msg_done) == NULL) {
		if ((done = getenv(ENV_MSG_DONE)) != NULL)
			done_size = strlen(done);
		else {
			done_size = strlen(DPV_DONE_DEFAULT);
			if ((done = malloc(done_size + 1)) == NULL)
				errx(EXIT_FAILURE, "Out of memory?!");
			dprompt_free_mask |= FM_DONE;
			snprintf(done, done_size + 1, DPV_DONE_DEFAULT);
		}
	}
	if (pbar_size < done_size) {
		done_lsize = done_rsize = 0;
		*(done + pbar_size) = '\0';
		done_size = pbar_size;
	} else {
		/* Calculate left/right portions for mini-progressbar */
		done_lsize = (pbar_size - done_size) / 2;
		done_rsize = done_lsize;
		/* If not evenly divisible by 2, increment the right-side */
		if ((done_rsize + done_size + done_lsize) != pbar_size)
			done_rsize++;
	}

	/* Initialize "Fail" text */
	if (fail == NULL && (fail = msg_fail) == NULL) {
		if ((fail = getenv(ENV_MSG_FAIL)) != NULL)
			fail_size = strlen(fail);
		else {
			fail_size = strlen(DPV_FAIL_DEFAULT);
			if ((fail = malloc(fail_size + 1)) == NULL)
				errx(EXIT_FAILURE, "Out of memory?!");
			dprompt_free_mask |= FM_FAIL;
			snprintf(fail, fail_size + 1, DPV_FAIL_DEFAULT);
		}
	}
	if (pbar_size < fail_size) {
		fail_lsize = fail_rsize = 0;
		*(fail + pbar_size) = '\0';
		fail_size = pbar_size;
	} else {
		/* Calculate left/right portions for mini-progressbar */
		fail_lsize = (pbar_size - fail_size) / 2;
		fail_rsize = fail_lsize;
		/* If not evenly divisible by 2, increment the right-side */
		if ((fail_rsize + fail_size + fail_lsize) != pbar_size)
			fail_rsize++;
	}

	/* Initialize "Pending" text */
	if (pend == NULL && (pend = msg_pending) == NULL) {
		if ((pend = getenv(ENV_MSG_PENDING)) != NULL)
			pend_size = strlen(pend);
		else {
			pend_size = strlen(DPV_PENDING_DEFAULT);
			if ((pend = malloc(pend_size + 1)) == NULL)
				errx(EXIT_FAILURE, "Out of memory?!");
			dprompt_free_mask |= FM_PEND;
			snprintf(pend, pend_size + 1, DPV_PENDING_DEFAULT);
		}
	}
	if (pbar_size < pend_size) {
		pend_lsize = pend_rsize = 0;
		*(pend + pbar_size) = '\0';
		pend_size = pbar_size;
	} else {
		/* Calculate left/right portions for mini-progressbar */
		pend_lsize = (pbar_size - pend_size) / 2;
		pend_rsize = pend_lsize;
		/* If not evenly divisible by 2, increment the right-side */
		if ((pend_rsize + pend_lsize + pend_size) != pbar_size)
			pend_rsize++;
	}

	if (debug)
		warnx("label_size = %i pbar_size = %i", label_size, pbar_size);

	dprompt_clear();
}

/*
 * Clear the [X]dialog(1) `--gauge' prompt buffer.
 */
void
dprompt_clear(void)
{

	*dprompt = '\0';
	dprompt_pos = dprompt;
}

/*
 * Append to the [X]dialog(1) `--gauge' prompt buffer. Syntax is like printf(3)
 * and returns the number of bytes appended to the buffer.
 */
int
dprompt_add(const char *format, ...)
{
	int len;
	va_list ap;

	if (dprompt_pos >= (dprompt + PROMPT_MAX))
		return (0);

	va_start(ap, format);
	len = vsnprintf(dprompt_pos, (size_t)(PROMPT_MAX -
	    (dprompt_pos - dprompt)), format, ap);
	va_end(ap);
	if (len == -1)
		errx(EXIT_FAILURE, "%s: Oops, dprompt buffer overflow",
		    __func__);

	if ((dprompt_pos + len) < (dprompt + PROMPT_MAX))
		dprompt_pos += len;
	else
		dprompt_pos = dprompt + PROMPT_MAX;

	return (len);
}

/*
 * Append active files to the [X]dialog(1) `--gauge' prompt buffer. Syntax
 * requires a pointer to the head of the dpv_file_node linked-list. Returns the
 * number of files processed successfully.
 */
static int
dprompt_add_files(struct dpv_file_node *file_list,
    struct dpv_file_node *curfile, int pct)
{
	char c;
	char bold_code = 'b'; /* default: enabled */
	char color_code = '4'; /* default: blue */
	uint8_t after_curfile = curfile != NULL ? FALSE : TRUE;
	uint8_t nls = 0;
	char *cp;
	char *lastline;
	char *name;
	const char *bg_code;
	const char *estext;
	const char *format;
	enum dprompt_state dstate;
	int estext_lsize;
	int estext_rsize;
	int flabel_size;
	int hlen;
	int lsize;
	int nlines = 0;
	int nthfile = 0;
	int pwidth;
	int rsize;
	struct dpv_file_node *fp;
	char flabel[FLABEL_MAX + 1];
	char human[32];
	char pbar[pbar_size + 16]; /* +15 for optional color */
	char pbar_cap[sizeof(pbar)];
	char pbar_fill[sizeof(pbar)];


	/* Override color defaults with that of main progress bar */
	if (use_colors || use_shadow) { /* NB: shadow enables color */
		color_code = gauge_color[0];
		/* NB: str[1] aka bg is unused */
		bold_code = gauge_color[2];
	}

	/*
	 * Create mini-progressbar for current file (if applicable)
	 */
	*pbar = '\0';
	if (pbar_size >= 0 && pct >= 0 && curfile != NULL &&
	    (curfile->length >= 0 || dialog_test)) {
		snprintf(pbar, pbar_size + 1, "%*s%3u%%%*s", pct_lsize, "",
		    pct, pct_rsize, "");
		if (use_color) {
			/* Calculate the fill-width of progressbar */
			pwidth = pct * pbar_size / 100;
			/* Round up based on one-tenth of a percent */
			if ((pct * pbar_size % 100) > 50)
				pwidth++;

			/*
			 * Make two copies of pbar. Make one represent the fill
			 * and the other the remainder (cap). We'll insert the
			 * ANSI delimiter in between.
			 */
			*pbar_fill = '\0';
			*pbar_cap = '\0';
			strncat(pbar_fill, (const char *)(pbar), dwidth);
			*(pbar_fill + pwidth) = '\0';
			strncat(pbar_cap, (const char *)(pbar+pwidth), dwidth);

			/* Finalize the mini [color] progressbar */
			snprintf(pbar, sizeof(pbar),
			    "\\Z%c\\Zr\\Z%c%s%s%s\\Zn", bold_code, color_code,
			    pbar_fill, "\\ZR", pbar_cap);
		}
	}

	for (fp = file_list; fp != NULL; fp = fp->next) {
		flabel_size = label_size;
		name = fp->name;
		nthfile++;

		/*
		 * Support multiline filenames (where the filename is taken as
		 * the last line and the text leading up to the last line can
		 * be used as (for example) a heading/separator between files.
		 */
		if (use_dialog)
			nls = dialog_prompt_nlstate(pprompt);
		nlines += dialog_prompt_numlines(name, nls);
		lastline = dialog_prompt_lastline(name, 1);
		if (name != lastline) {
			c = *lastline;
			*lastline = '\0';
			dprompt_add("%s", name);
			*lastline = c;
			name = lastline;
		}

		/* Support color codes (for dialog(1,3)) in file names */
		if ((use_dialog || use_libdialog) && use_color) {
			cp = name;
			while (*cp != '\0') {
				if (*cp == '\\' && *(cp + 1) != '\0' &&
				    *(++cp) == 'Z' && *(cp + 1) != '\0') {
					cp++;
					flabel_size += 3;
				}
				cp++;
			}
			if (flabel_size > FLABEL_MAX)
				flabel_size = FLABEL_MAX;
		}

		/* If no mini-progressbar, increase label width */
		if (pbar_size < 0 && flabel_size <= FLABEL_MAX - 2 &&
		    no_labels == FALSE)
			flabel_size += 2;

		/* If name is too long, add an ellipsis */
		if (snprintf(flabel, flabel_size + 1, "%s", name) >
		    flabel_size) sprintf(flabel + flabel_size - 3, "...");

		/*
		 * Append the label (processing the current file differently)
		 */
		if (fp == curfile && pct < 100) {
			/*
			 * Add an ellipsis to current file name if it will fit.
			 * There may be an ellipsis already from truncating the
			 * label (in which case, we already have one).
			 */
			cp = flabel + strlen(flabel);
			if (cp < (flabel + flabel_size))
				snprintf(cp, flabel_size -
				    (cp - flabel) + 1, "...");

			/* Append label (with spinner and optional color) */
			dprompt_add("%s%-*s%s %c", use_color ? "\\Zb" : "",
			    flabel_size, flabel, use_color ? "\\Zn" : "",
			    spin_char());
		} else
			dprompt_add("%-*s%s %s", flabel_size,
			    flabel, use_color ? "\\Zn" : "", " ");

		/*
		 * Append pbar/status (processing the current file differently)
		 */
		dstate = DPROMPT_NONE;
		if (fp->msg != NULL)
			dstate = DPROMPT_CUSTOM_MSG;
		else if (pbar_size < 0)
			dstate = DPROMPT_NONE;
		else if (pbar_size < 4)
			dstate = DPROMPT_MINIMAL;
		else if (after_curfile)
			dstate = DPROMPT_PENDING;
		else if (fp == curfile) {
			if (*pbar == '\0') {
				if (fp->length < 0)
					dstate = DPROMPT_DETAILS;
				else if (fp->status == DPV_STATUS_RUNNING)
					dstate = DPROMPT_DETAILS;
				else
					dstate = DPROMPT_END_STATE;
			}
			else if (dialog_test) /* status/length ignored */
				dstate = pct < 100 ?
				    DPROMPT_PBAR : DPROMPT_END_STATE;
			else if (fp->status == DPV_STATUS_RUNNING)
				dstate = fp->length < 0 ?
				    DPROMPT_DETAILS : DPROMPT_PBAR;
			else /* not running */
				dstate = fp->length < 0 ?
				    DPROMPT_DETAILS : DPROMPT_END_STATE;
		} else { /* before curfile */
			if (dialog_test)
				dstate = DPROMPT_END_STATE;
			else
				dstate = fp->length < 0 ?
				    DPROMPT_DETAILS : DPROMPT_END_STATE;
		}
		format = use_color ?
		    " [\\Z%c%s%-*s%s%-*s\\Zn]\\n" :
		    " [%-*s%s%-*s]\\n";
		if (fp->status == DPV_STATUS_FAILED) {
			bg_code = "\\Zr\\Z1"; /* Red */
			estext_lsize = fail_lsize;
			estext_rsize = fail_rsize;
			estext = fail;
		} else { /* e.g., DPV_STATUS_DONE */
			bg_code = "\\Zr\\Z2"; /* Green */
			estext_lsize = done_lsize;
			estext_rsize = done_rsize;
			estext = done;
		}
		switch (dstate) {
		case DPROMPT_PENDING: /* Future file(s) */
			dprompt_add(" [%-*s%s%-*s]\\n",
			    pend_lsize, "", pend, pend_rsize, "");
			break;
		case DPROMPT_PBAR: /* Current file */
			dprompt_add(" [%s]\\n", pbar);
			break;
		case DPROMPT_END_STATE: /* Past/Current file(s) */
			if (use_color)
				dprompt_add(format, bold_code, bg_code,
				    estext_lsize, "", estext,
				    estext_rsize, "");
			else
				dprompt_add(format,
				    estext_lsize, "", estext,
				    estext_rsize, "");
			break;
		case DPROMPT_DETAILS: /* Past/Current file(s) */
			humanize_number(human, pbar_size + 2, fp->read, "",
			    HN_AUTOSCALE, HN_NOSPACE | HN_DIVISOR_1000);

			/* Calculate center alignment */
			hlen = (int)strlen(human);
			lsize = (pbar_size - hlen) / 2;
			rsize = lsize;
			if ((lsize+hlen+rsize) != pbar_size)
				rsize++;

			if (use_color)
				dprompt_add(format, bold_code, bg_code,
				    lsize, "", human, rsize, "");
			else
				dprompt_add(format,
				    lsize, "", human, rsize, "");
			break;
		case DPROMPT_CUSTOM_MSG: /* File-specific message override */
			snprintf(msg, PROMPT_MAX + 1, "%s", fp->msg);
			if (pbar_size < (mesg_size = strlen(msg))) {
				mesg_lsize = mesg_rsize = 0;
				*(msg + pbar_size) = '\0';
				mesg_size = pbar_size;
			} else {
				mesg_lsize = (pbar_size - mesg_size) / 2;
				mesg_rsize = mesg_lsize;
				if ((mesg_rsize + mesg_size + mesg_lsize)
				    != pbar_size)
					mesg_rsize++;
			}
			if (use_color)
				dprompt_add(format, bold_code, bg_code,
				    mesg_lsize, "", msg, mesg_rsize, "");
			else
				dprompt_add(format, mesg_lsize, "", msg,
				    mesg_rsize, "");
			break;
		case DPROMPT_MINIMAL: /* Short progress bar, minimal room */
			if (use_color)
				dprompt_add(format, bold_code, bg_code,
				    pbar_size, "", "", 0, "");
			else
				dprompt_add(format, pbar_size, "", "", 0, "");
			break;
		case DPROMPT_NONE: /* pbar_size < 0 */
			/* FALLTHROUGH */
		default:
			dprompt_add(" \\n");
			/*
			 * NB: Leading space required for the case when
			 * spin_char() returns a single backslash [\] which
			 * without the space, changes the meaning of `\n'
			 */
		}

		/* Stop building if we've hit the internal limit */
		if (nthfile >= display_limit)
			break;

		/* If this is the current file, all others are pending */
		if (fp == curfile)
			after_curfile = TRUE;
	}

	/*
	 * Since we cannot change the height/width of the [X]dialog(1) widget
	 * after spawn, to make things look nice let's pad the height so that
	 * the `-a text' always appears in the same spot.
	 *
	 * NOTE: fheight is calculated in dprompt_init(). It represents the
	 * maximum height required to display the set of items (broken up into
	 * pieces of display_limit chunks) whose names contain the most
	 * newlines for any given set.
	 */
	while (nlines < fheight) {
		dprompt_add("\n");
		nlines++;
	}

	return (nthfile);
}

/*
 * Process the dpv_file_node linked-list of named files, re-generating the
 * [X]dialog(1) `--gauge' prompt text for the current state of transfers.
 */
void
dprompt_recreate(struct dpv_file_node *file_list,
    struct dpv_file_node *curfile, int pct)
{
	size_t len;

	/*
	 * Re-Build the prompt text
	 */
	dprompt_clear();
	if (display_limit > 0)
		dprompt_add_files(file_list, curfile, pct);

	/* Xdialog(1) requires newlines (a) escaped and (b) in triplicate */
	if (use_xdialog) {
		/* Replace `\n' with `\n\\n\n' in dprompt */
		len = strlen(dprompt);
		len += strcount(dprompt, "\\n") * 5; /* +5 chars per count */
		if (len > PROMPT_MAX)
			errx(EXIT_FAILURE, "%s: Oops, dprompt buffer overflow "
			    "(%zu > %i)", __func__, len, PROMPT_MAX);
		if (replaceall(dprompt, "\\n", "\n\\n\n") < 0)
			err(EXIT_FAILURE, "%s: replaceall()", __func__);
	}
	else if (use_libdialog)
		strexpandnl(dprompt);
}

/*
 * Print the [X]dialog(1) `--gauge' prompt text to a buffer.
 */
int
dprompt_sprint(char * restrict str, const char *prefix, const char *append)
{

	return (snprintf(str, PROMPT_MAX, "%s%s%s%s", use_color ? "\\Zn" : "",
	    prefix ? prefix : "", dprompt, append ? append : ""));
}

/*
 * Print the [X]dialog(1) `--gauge' prompt text to file descriptor fd (could
 * be STDOUT_FILENO or a pipe(2) file descriptor to actual [X]dialog(1)).
 */
void
dprompt_dprint(int fd, const char *prefix, const char *append, int overall)
{
	int percent = gauge_percent;

	if (overall >= 0 && overall <= 100)
		gauge_percent = percent = overall;
	dprintf(fd, "XXX\n%s%s%s%s\nXXX\n%i\n", use_color ? "\\Zn" : "",
	    prefix ? prefix : "", dprompt, append ? append : "", percent);
	fsync(fd);
}

/*
 * Print the dialog(3) `gauge' prompt text using libdialog.
 */
void
dprompt_libprint(const char *prefix, const char *append, int overall)
{
	int percent = gauge_percent;
	char buf[DPV_PPROMPT_MAX + DPV_APROMPT_MAX + DPV_DISPLAY_LIMIT * 1024];

	dprompt_sprint(buf, prefix, append);

	if (overall >= 0 && overall <= 100)
		gauge_percent = percent = overall;
	gauge = dlg_reallocate_gauge(gauge, title == NULL ? "" : title,
	    buf, dheight, dwidth, percent);
	dlg_update_gauge(gauge, percent);
}

/*
 * Free allocated items initialized by dprompt_init()
 */
void
dprompt_free(void)
{
	if ((dprompt_free_mask & FM_DONE) != 0) {
		dprompt_free_mask ^= FM_DONE;
		free(done);
		done = NULL;
	}
	if ((dprompt_free_mask & FM_FAIL) != 0) {
		dprompt_free_mask ^= FM_FAIL;
		free(fail);
		fail = NULL;
	}
	if ((dprompt_free_mask & FM_PEND) != 0) {
		dprompt_free_mask ^= FM_PEND;
		free(pend);
		pend = NULL;
	}
}
