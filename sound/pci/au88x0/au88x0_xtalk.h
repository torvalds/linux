/***************************************************************************
 *            au88x0_cxtalk.h
 *
 *  Wed Nov 19 19:07:17 2003
 *  Copyright  2003  mjander
 *  mjander@users.sourceforge.org
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
