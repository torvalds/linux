/* SPDX-License-Identifier: GPL-2.0-or-later */
/***************************************************************************
 *            au88x0_cxtalk.h
 *
 *  Wed Nov 19 19:07:17 2003
 *  Copyright  2003  mjander
 *  mjander@users.sourceforge.org
 ****************************************************************************/

/*
 */

/* The crosstalk canceler supports 5 stereo input channels. The result is 
   available at one single output route pair (stereo). */

#ifndef _AU88X0_CXTALK_H
#define _AU88X0_CXTALK_H

#include "au88x0.h"

#define XTDLINE_SZ 32
#define XTGAINS_SZ 10
#define XTINST_SZ 4

#define XT_HEADPHONE	1
#define XT_SPEAKER0		2
#define XT_SPEAKER1		3
#define XT_DIAMOND		4

typedef u32 xtalk_dline_t[XTDLINE_SZ];
typedef u16 xtalk_gains_t[XTGAINS_SZ];
typedef u16 xtalk_instate_t[XTINST_SZ];
typedef u16 xtalk_coefs_t[5][5];
typedef u16 xtalk_state_t[5][4];

static void vortex_XtalkHw_SetGains(vortex_t * vortex,
				    xtalk_gains_t const gains);
static void vortex_XtalkHw_SetGainsAllChan(vortex_t * vortex);
static void vortex_XtalkHw_SetSampleRate(vortex_t * vortex, u32 sr);
static void vortex_XtalkHw_ProgramPipe(vortex_t * vortex);
static void vortex_XtalkHw_ProgramPipe(vortex_t * vortex);
static void vortex_XtalkHw_ProgramXtalkWide(vortex_t * vortex);
static void vortex_XtalkHw_ProgramXtalkNarrow(vortex_t * vortex);
static void vortex_XtalkHw_ProgramDiamondXtalk(vortex_t * vortex);
static void vortex_XtalkHw_Enable(vortex_t * vortex);
static void vortex_XtalkHw_Disable(vortex_t * vortex);
static void vortex_XtalkHw_init(vortex_t * vortex);

#endif				/* _AU88X0_CXTALK_H */
