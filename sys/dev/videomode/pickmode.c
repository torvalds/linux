/* $NetBSD: pickmode.c,v 1.3 2011/04/09 18:22:31 jdc Exp $ */
/* $FreeBSD$ */

/*-
 * Copyright (c) 2006 The NetBSD Foundation
 * All rights reserved.
 *
 * this code was contributed to The NetBSD Foundation by Michael Lorenz
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
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE NETBSD FOUNDATION BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */ 

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/libkern.h>
#include <dev/videomode/videomode.h>
#include "opt_videomode.h"

#ifdef PICKMODE_DEBUG
#define DPRINTF printf
#else
#define DPRINTF while (0) printf
#endif

const struct videomode *
pick_mode_by_dotclock(int width, int height, int dotclock)
{
	const struct videomode *this, *best = NULL;
	int i;

	DPRINTF("%s: looking for %d x %d at up to %d kHz\n", __func__, width,
	    height, dotclock);
	for (i = 0; i < videomode_count; i++) {
		this = &videomode_list[i];
		if ((this->hdisplay != width) || (this->vdisplay != height) ||
		    (this->dot_clock > dotclock))
			continue;
		if (best != NULL) {
			if (this->dot_clock > best->dot_clock)
				best = this;
		} else
			best = this;
	}
	if (best != NULL)
		DPRINTF("found %s\n", best->name);

	return best;
}

const struct videomode *
pick_mode_by_ref(int width, int height, int refresh)
{
	const struct videomode *this, *best = NULL;
	int mref, closest = 1000, i, diff;

	DPRINTF("%s: looking for %d x %d at up to %d Hz\n", __func__, width,
	    height, refresh);
	for (i = 0; i < videomode_count; i++) {

		this = &videomode_list[i];
		mref = this->dot_clock * 1000 / (this->htotal * this->vtotal);
		diff = abs(mref - refresh);
		if ((this->hdisplay != width) || (this->vdisplay != height))
			continue;
		DPRINTF("%s in %d hz, diff %d\n", this->name, mref, diff);
		if (best != NULL) {
			if (diff < closest) {
				best = this;
				closest = diff;
			}
		} else {
			best = this;
			closest = diff;
		}
	}
	if (best != NULL)
		DPRINTF("found %s %d\n", best->name, best->dot_clock);

	return best;
}

static inline void
swap_modes(struct videomode *left, struct videomode *right)
{
	struct videomode temp;

	temp = *left;
	*left = *right;
	*right = temp;
}

/*
 * Sort modes by refresh rate, aspect ratio (*), then resolution.
 * Preferred mode or largest mode is first in the list and other modes
 * are sorted on closest match to that mode.
 * (*) Note that the aspect ratio calculation treats "close" aspect ratios
 * (within 12.5%) as the same for this purpose.
 */
#define	DIVIDE(x, y)	(((x) + ((y) / 2)) / (y))
void
sort_modes(struct videomode *modes, struct videomode **preferred, int nmodes)
{
	int aspect, refresh, hbest, vbest, abest, atemp, rbest, rtemp;
	int i, j;
	struct videomode *mtemp = NULL;

	if (nmodes < 2)
		return;

	if (*preferred != NULL) {
		/* Put the preferred mode first in the list */
		aspect = (*preferred)->hdisplay * 100 / (*preferred)->vdisplay;
		refresh = DIVIDE(DIVIDE((*preferred)->dot_clock * 1000,
		    (*preferred)->htotal), (*preferred)->vtotal);
		if (*preferred != modes) {
			swap_modes(*preferred, modes);
			*preferred = modes;
		}
	} else {
		/*
		 * Find the largest horizontal and vertical mode and put that
		 * first in the list.  Preferred refresh rate is taken from
		 * the first mode of this size.
		 */
		hbest = 0;
		vbest = 0;
		for (i = 0; i < nmodes; i++) {
			if (modes[i].hdisplay > hbest) {
				hbest = modes[i].hdisplay;
				vbest = modes[i].vdisplay;
				mtemp = &modes[i];
			} else if (modes[i].hdisplay == hbest &&
			    modes[i].vdisplay > vbest) {
				vbest = modes[i].vdisplay;
				mtemp = &modes[i];
			}
		}
		aspect = mtemp->hdisplay * 100 / mtemp->vdisplay;
		refresh = DIVIDE(DIVIDE(mtemp->dot_clock * 1000,
		    mtemp->htotal), mtemp->vtotal);
		if (mtemp != modes)
			swap_modes(mtemp, modes);
	}

	/* Sort other modes by refresh rate, aspect ratio, then resolution */
	for (j = 1; j < nmodes - 1; j++) {
		rbest = 1000;
		abest = 1000;
		hbest = 0;
		vbest = 0;
		for (i = j; i < nmodes; i++) {
			rtemp = abs(refresh -
			    DIVIDE(DIVIDE(modes[i].dot_clock * 1000,
			    modes[i].htotal), modes[i].vtotal));
			atemp = (modes[i].hdisplay * 100 / modes[i].vdisplay);
			if (rtemp < rbest) {
				rbest = rtemp;
				mtemp = &modes[i];
			}
			if (rtemp == rbest) {
				/* Treat "close" aspect ratios as identical */
				if (abs(abest - atemp) > (abest / 8) &&
				    abs(aspect - atemp) < abs(aspect - abest)) {
					abest = atemp;
					mtemp = &modes[i];
				}
				if (atemp == abest ||
				    abs(abest - atemp) <= (abest / 8)) {
					if (modes[i].hdisplay > hbest) {
						hbest = modes[i].hdisplay;
						mtemp = &modes[i];
					}
					if (modes[i].hdisplay == hbest &&
					    modes[i].vdisplay > vbest) {
						vbest = modes[i].vdisplay;
						mtemp = &modes[i];
					}
				}
			}
		}
		if (mtemp != &modes[j])
			swap_modes(mtemp, &modes[j]);
	}
}
