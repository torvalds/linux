/* $NetBSD: vesagtf.c,v 1.2 2013/09/15 15:56:07 martin Exp $ */
/* $FreeBSD$ */

/*-
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Written by Garrett D'Amore for Itronix Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Itronix Inc. may not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ITRONIX INC. ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL ITRONIX INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */ 

/*
 * This was derived from a userland GTF program supplied by NVIDIA.
 * NVIDIA's original boilerplate follows. 
 *
 * Note that I have heavily modified the program for use in the EDID
 * kernel code for NetBSD, including removing the use of floating
 * point operations and making significant adjustments to minimize
 * error propagation while operating with integer only math.
 *
 * This has required the use of 64-bit integers in a few places, but
 * the upshot is that for a calculation of 1920x1200x85 (as an
 * example), the error deviates by only ~.004% relative to the
 * floating point version.  This error is *well* within VESA
 * tolerances.
 */

/*
 * Copyright (c) 2001, Andy Ritger  aritger@nvidia.com
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * o Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * o Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * o Neither the name of NVIDIA nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * 
 *
 * This program is based on the Generalized Timing Formula(GTF TM)
 * Standard Version: 1.0, Revision: 1.0
 *
 * The GTF Document contains the following Copyright information:
 *
 * Copyright (c) 1994, 1995, 1996 - Video Electronics Standards
 * Association. Duplication of this document within VESA member
 * companies for review purposes is permitted. All other rights
 * reserved.
 *
 * While every precaution has been taken in the preparation
 * of this standard, the Video Electronics Standards Association and
 * its contributors assume no responsibility for errors or omissions,
 * and make no warranties, expressed or implied, of functionality
 * of suitability for any purpose. The sample code contained within
 * this standard may be used without restriction.
 *
 * 
 *
 * The GTF EXCEL(TM) SPREADSHEET, a sample (and the definitive)
 * implementation of the GTF Timing Standard, is available at:
 *
 * ftp://ftp.vesa.org/pub/GTF/GTF_V1R1.xls
 *
 *
 *
 * This program takes a desired resolution and vertical refresh rate,
 * and computes mode timings according to the GTF Timing Standard.
 * These mode timings can then be formatted as an XFree86 modeline
 * or a mode description for use by fbset(8).
 *
 *
 *
 * NOTES:
 *
 * The GTF allows for computation of "margins" (the visible border
 * surrounding the addressable video); on most non-overscan type
 * systems, the margin period is zero.  I've implemented the margin
 * computations but not enabled it because 1) I don't really have
 * any experience with this, and 2) neither XFree86 modelines nor
 * fbset fb.modes provide an obvious way for margin timings to be
 * included in their mode descriptions (needs more investigation).
 * 
 * The GTF provides for computation of interlaced mode timings;
 * I've implemented the computations but not enabled them, yet.
 * I should probably enable and test this at some point.
 *
 * 
 *
 * TODO:
 *
 * o Add support for interlaced modes.
 *
 * o Implement the other portions of the GTF: compute mode timings
 *   given either the desired pixel clock or the desired horizontal
 *   frequency.
 *
 * o It would be nice if this were more general purpose to do things
 *   outside the scope of the GTF: like generate double scan mode
 *   timings, for example.
 *   
 * o Printing digits to the right of the decimal point when the
 *   digits are 0 annoys me.
 *
 * o Error checking.
 *
 */


#ifdef	_KERNEL
#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <dev/videomode/videomode.h>
#include <dev/videomode/vesagtf.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "videomode.h"
#include "vesagtf.h"

void print_xf86_mode(struct videomode *m);
#endif

#define CELL_GRAN         8     /* assumed character cell granularity        */

/* C' and M' are part of the Blanking Duty Cycle computation */
/*
 * #define C_PRIME           (((C - J) * K/256.0) + J)
 * #define M_PRIME           (K/256.0 * M)
 */

/*
 * C' and M' multiplied by 256 to give integer math.  Make sure to
 * scale results using these back down, appropriately.
 */
#define	C_PRIME256(p)	  (((p->C - p->J) * p->K) + (p->J * 256))
#define	M_PRIME256(p)	  (p->K * p->M)

#define	DIVIDE(x,y)	(((x) + ((y) / 2)) / (y))

/*
 * print_value() - print the result of the named computation; this is
 * useful when comparing against the GTF EXCEL spreadsheet.
 */

#ifdef GTFDEBUG

static void
print_value(int n, const char *name, unsigned val)
{
        printf("%2d: %-27s: %u\n", n, name, val);
}
#else
#define	print_value(n, name, val)
#endif


/*
 * vert_refresh() - as defined by the GTF Timing Standard, compute the
 * Stage 1 Parameters using the vertical refresh frequency.  In other
 * words: input a desired resolution and desired refresh rate, and
 * output the GTF mode timings.
 *
 * XXX All the code is in place to compute interlaced modes, but I don't
 * feel like testing it right now.
 *
 * XXX margin computations are implemented but not tested (nor used by
 * XFree86 of fbset mode descriptions, from what I can tell).
 */

void
vesagtf_mode_params(unsigned h_pixels, unsigned v_lines, unsigned freq,
    struct vesagtf_params *params, int flags, struct videomode *vmp)
{
    unsigned v_field_rqd;
    unsigned top_margin;
    unsigned bottom_margin;
    unsigned interlace;
    uint64_t h_period_est;
    unsigned vsync_plus_bp;
    unsigned v_back_porch __unused;
    unsigned total_v_lines;
    uint64_t v_field_est;
    uint64_t h_period;
    unsigned v_field_rate;
    unsigned v_frame_rate __unused;
    unsigned left_margin;
    unsigned right_margin;
    unsigned total_active_pixels;
    uint64_t ideal_duty_cycle;
    unsigned h_blank;
    unsigned total_pixels;
    unsigned pixel_freq;

    unsigned h_sync;
    unsigned h_front_porch;
    unsigned v_odd_front_porch_lines;

#ifdef	GTFDEBUG
    unsigned h_freq;
#endif
    
    /*  1. In order to give correct results, the number of horizontal
     *  pixels requested is first processed to ensure that it is divisible
     *  by the character size, by rounding it to the nearest character
     *  cell boundary:
     *
     *  [H PIXELS RND] = ((ROUND([H PIXELS]/[CELL GRAN RND],0))*[CELLGRAN RND])
     */
    
    h_pixels = DIVIDE(h_pixels, CELL_GRAN) * CELL_GRAN;
    
    print_value(1, "[H PIXELS RND]", h_pixels);

    
    /*  2. If interlace is requested, the number of vertical lines assumed
     *  by the calculation must be halved, as the computation calculates
     *  the number of vertical lines per field. In either case, the
     *  number of lines is rounded to the nearest integer.
     *   
     *  [V LINES RND] = IF([INT RQD?]="y", ROUND([V LINES]/2,0),
     *                                     ROUND([V LINES],0))
     */

    v_lines = (flags & VESAGTF_FLAG_ILACE) ? DIVIDE(v_lines, 2) : v_lines;
    
    print_value(2, "[V LINES RND]", v_lines);
    
    
    /*  3. Find the frame rate required:
     *
     *  [V FIELD RATE RQD] = IF([INT RQD?]="y", [I/P FREQ RQD]*2,
     *                                          [I/P FREQ RQD])
     */

    v_field_rqd = (flags & VESAGTF_FLAG_ILACE) ? (freq * 2) : (freq);

    print_value(3, "[V FIELD RATE RQD]", v_field_rqd);
    

    /*  4. Find number of lines in Top margin:
     *  5. Find number of lines in Bottom margin:
     *
     *  [TOP MARGIN (LINES)] = IF([MARGINS RQD?]="Y",
     *          ROUND(([MARGIN%]/100*[V LINES RND]),0),
     *          0)
     *
     *  Ditto for bottom margin.  Note that instead of %, we use PPT, which
     *  is parts per thousand.  This helps us with integer math.
     */

    top_margin = bottom_margin = (flags & VESAGTF_FLAG_MARGINS) ?
	DIVIDE(v_lines * params->margin_ppt, 1000) : 0;

    print_value(4, "[TOP MARGIN (LINES)]", top_margin);
    print_value(5, "[BOT MARGIN (LINES)]", bottom_margin);

    
    /*  6. If interlace is required, then set variable [INTERLACE]=0.5:
     *   
     *  [INTERLACE]=(IF([INT RQD?]="y",0.5,0))
     *
     *  To make this integer friendly, we use some special hacks in step
     *  7 below.  Please read those comments to understand why I am using
     *  a whole number of 1.0 instead of 0.5 here.
     */
    interlace = (flags & VESAGTF_FLAG_ILACE) ? 1 : 0;

    print_value(6, "[2*INTERLACE]", interlace);
    

    /*  7. Estimate the Horizontal period
     *
     *  [H PERIOD EST] = ((1/[V FIELD RATE RQD]) - [MIN VSYNC+BP]/1000000) /
     *                    ([V LINES RND] + (2*[TOP MARGIN (LINES)]) +
     *                     [MIN PORCH RND]+[INTERLACE]) * 1000000
     *
     *  To make it integer friendly, we pre-multiply the 1000000 to get to
     *  usec.  This gives us:
     *
     *  [H PERIOD EST] = ((1000000/[V FIELD RATE RQD]) - [MIN VSYNC+BP]) /
     *			([V LINES RND] + (2 * [TOP MARGIN (LINES)]) +
     *			 [MIN PORCH RND]+[INTERLACE])
     *
     *  The other problem is that the interlace value is wrong.  To get
     *  the interlace to a whole number, we multiply both the numerator and
     *  divisor by 2, so we can use a value of either 1 or 0 for the interlace
     *  factor.
     *
     * This gives us:
     *
     * [H PERIOD EST] = ((2*((1000000/[V FIELD RATE RQD]) - [MIN VSYNC+BP])) /
     *			 (2*([V LINES RND] + (2*[TOP MARGIN (LINES)]) +
     *			  [MIN PORCH RND]) + [2*INTERLACE]))
     *
     * Finally we multiply by another 1000, to get value in picosec.
     * Why picosec?  To minimize rounding errors.  Gotta love integer
     * math and error propagation.
     */

    h_period_est = DIVIDE(((DIVIDE(2000000000000ULL, v_field_rqd)) -
			      (2000000 * params->min_vsbp)),
	((2 * (v_lines + (2 * top_margin) + params->min_porch)) + interlace));

    print_value(7, "[H PERIOD EST (ps)]", h_period_est);
    

    /*  8. Find the number of lines in V sync + back porch:
     *
     *  [V SYNC+BP] = ROUND(([MIN VSYNC+BP]/[H PERIOD EST]),0)
     *
     *  But recall that h_period_est is in psec. So multiply by 1000000.
     */

    vsync_plus_bp = DIVIDE(params->min_vsbp * 1000000, h_period_est);

    print_value(8, "[V SYNC+BP]", vsync_plus_bp);
    
    
    /*  9. Find the number of lines in V back porch alone:
     *
     *  [V BACK PORCH] = [V SYNC+BP] - [V SYNC RND]
     *
     *  XXX is "[V SYNC RND]" a typo? should be [V SYNC RQD]?
     */
    
    v_back_porch = vsync_plus_bp - params->vsync_rqd;
    
    print_value(9, "[V BACK PORCH]", v_back_porch);
    

    /*  10. Find the total number of lines in Vertical field period:
     *
     *  [TOTAL V LINES] = [V LINES RND] + [TOP MARGIN (LINES)] +
     *                    [BOT MARGIN (LINES)] + [V SYNC+BP] + [INTERLACE] +
     *                    [MIN PORCH RND]
     */

    total_v_lines = v_lines + top_margin + bottom_margin + vsync_plus_bp +
        interlace + params->min_porch;
    
    print_value(10, "[TOTAL V LINES]", total_v_lines);
    

    /*  11. Estimate the Vertical field frequency:
     *
     *  [V FIELD RATE EST] = 1 / [H PERIOD EST] / [TOTAL V LINES] * 1000000
     *
     *  Again, we want to pre multiply by 10^9 to convert for nsec, thereby
     *  making it usable in integer math.
     *
     *  So we get:
     *
     *  [V FIELD RATE EST] = 1000000000 / [H PERIOD EST] / [TOTAL V LINES]
     *
     *  This is all scaled to get the result in uHz.  Again, we're trying to
     *  minimize error propagation.
     */
    v_field_est = DIVIDE(DIVIDE(1000000000000000ULL, h_period_est),
	total_v_lines);
    
    print_value(11, "[V FIELD RATE EST(uHz)]", v_field_est);
    

    /*  12. Find the actual horizontal period:
     *
     *  [H PERIOD] = [H PERIOD EST] / ([V FIELD RATE RQD] / [V FIELD RATE EST])
     */

    h_period = DIVIDE(h_period_est * v_field_est, v_field_rqd * 1000);
    
    print_value(12, "[H PERIOD(ps)]", h_period);
    

    /*  13. Find the actual Vertical field frequency:
     *
     *  [V FIELD RATE] = 1 / [H PERIOD] / [TOTAL V LINES] * 1000000
     *
     *  And again, we convert to nsec ahead of time, giving us:
     *
     *  [V FIELD RATE] = 1000000 / [H PERIOD] / [TOTAL V LINES]
     *
     *  And another rescaling back to mHz.  Gotta love it.
     */

    v_field_rate = DIVIDE(1000000000000ULL, h_period * total_v_lines);

    print_value(13, "[V FIELD RATE]", v_field_rate);
    

    /*  14. Find the Vertical frame frequency:
     *
     *  [V FRAME RATE] = (IF([INT RQD?]="y", [V FIELD RATE]/2, [V FIELD RATE]))
     *
     *  N.B. that the result here is in mHz.
     */

    v_frame_rate = (flags & VESAGTF_FLAG_ILACE) ?
	v_field_rate / 2 : v_field_rate;

    print_value(14, "[V FRAME RATE]", v_frame_rate);
    

    /*  15. Find number of pixels in left margin:
     *  16. Find number of pixels in right margin:
     *
     *  [LEFT MARGIN (PIXELS)] = (IF( [MARGINS RQD?]="Y",
     *          (ROUND( ([H PIXELS RND] * [MARGIN%] / 100 /
     *                   [CELL GRAN RND]),0)) * [CELL GRAN RND],
     *          0))
     *
     *  Again, we deal with margin percentages as PPT (parts per thousand).
     *  And the calculations for left and right are the same.
     */

    left_margin = right_margin = (flags & VESAGTF_FLAG_MARGINS) ?
	DIVIDE(DIVIDE(h_pixels * params->margin_ppt, 1000),
	    CELL_GRAN) * CELL_GRAN : 0;

    print_value(15, "[LEFT MARGIN (PIXELS)]", left_margin);
    print_value(16, "[RIGHT MARGIN (PIXELS)]", right_margin);
    

    /*  17. Find total number of active pixels in image and left and right
     *  margins:
     *
     *  [TOTAL ACTIVE PIXELS] = [H PIXELS RND] + [LEFT MARGIN (PIXELS)] +
     *                          [RIGHT MARGIN (PIXELS)]
     */

    total_active_pixels = h_pixels + left_margin + right_margin;
    
    print_value(17, "[TOTAL ACTIVE PIXELS]", total_active_pixels);
    
    
    /*  18. Find the ideal blanking duty cycle from the blanking duty cycle
     *  equation:
     *
     *  [IDEAL DUTY CYCLE] = [C'] - ([M']*[H PERIOD]/1000)
     *
     *  However, we have modified values for [C'] as [256*C'] and
     *  [M'] as [256*M'].  Again the idea here is to get good scaling.
     *  We use 256 as the factor to make the math fast.
     *
     *  Note that this means that we have to scale it appropriately in
     *  later calculations.
     *
     *  The ending result is that our ideal_duty_cycle is 256000x larger
     *  than the duty cycle used by VESA.  But again, this reduces error
     *  propagation.
     */

    ideal_duty_cycle =
	((C_PRIME256(params) * 1000) -
	    (M_PRIME256(params) * h_period / 1000000));
    
    print_value(18, "[IDEAL DUTY CYCLE]", ideal_duty_cycle);
    

    /*  19. Find the number of pixels in the blanking time to the nearest
     *  double character cell:
     *
     *  [H BLANK (PIXELS)] = (ROUND(([TOTAL ACTIVE PIXELS] *
     *                               [IDEAL DUTY CYCLE] /
     *                               (100-[IDEAL DUTY CYCLE]) /
     *                               (2*[CELL GRAN RND])), 0))
     *                       * (2*[CELL GRAN RND])
     *
     *  Of course, we adjust to make this rounding work in integer math.
     */

    h_blank = DIVIDE(DIVIDE(total_active_pixels * ideal_duty_cycle,
			 (256000 * 100ULL) - ideal_duty_cycle),
	2 * CELL_GRAN) * (2 * CELL_GRAN);

    print_value(19, "[H BLANK (PIXELS)]", h_blank);
    

    /*  20. Find total number of pixels:
     *
     *  [TOTAL PIXELS] = [TOTAL ACTIVE PIXELS] + [H BLANK (PIXELS)]
     */

    total_pixels = total_active_pixels + h_blank;
    
    print_value(20, "[TOTAL PIXELS]", total_pixels);
    

    /*  21. Find pixel clock frequency:
     *
     *  [PIXEL FREQ] = [TOTAL PIXELS] / [H PERIOD]
     *
     *  We calculate this in Hz rather than MHz, to get a value that
     *  is usable with integer math.  Recall that the [H PERIOD] is in
     *  nsec.
     */
    
    pixel_freq = DIVIDE(total_pixels * 1000000, DIVIDE(h_period, 1000));
    
    print_value(21, "[PIXEL FREQ]", pixel_freq);
    

    /*  22. Find horizontal frequency:
     *
     *  [H FREQ] = 1000 / [H PERIOD]
     *
     *  I've ifdef'd this out, because we don't need it for any of
     *  our calculations.
     *  We calculate this in Hz rather than kHz, to avoid rounding
     *  errors.  Recall that the [H PERIOD] is in usec.
     */

#ifdef	GTFDEBUG
    h_freq = 1000000000 / h_period;
    
    print_value(22, "[H FREQ]", h_freq);
#endif
    


    /* Stage 1 computations are now complete; I should really pass
       the results to another function and do the Stage 2
       computations, but I only need a few more values so I'll just
       append the computations here for now */

    

    /*  17. Find the number of pixels in the horizontal sync period:
     *
     *  [H SYNC (PIXELS)] =(ROUND(([H SYNC%] / 100 * [TOTAL PIXELS] /
     *                             [CELL GRAN RND]),0))*[CELL GRAN RND]
     *
     *  Rewriting for integer math:
     *
     *  [H SYNC (PIXELS)]=(ROUND((H SYNC%] * [TOTAL PIXELS] / 100 /
     *				   [CELL GRAN RND),0))*[CELL GRAN RND]
     */

    h_sync = DIVIDE(((params->hsync_pct * total_pixels) / 100), CELL_GRAN) *
	CELL_GRAN;

    print_value(17, "[H SYNC (PIXELS)]", h_sync);
    

    /*  18. Find the number of pixels in the horizontal front porch period:
     *
     *  [H FRONT PORCH (PIXELS)] = ([H BLANK (PIXELS)]/2)-[H SYNC (PIXELS)]
     *
     *  Note that h_blank is always an even number of characters (i.e.
     *  h_blank % (CELL_GRAN * 2) == 0)
     */

    h_front_porch = (h_blank / 2) - h_sync;

    print_value(18, "[H FRONT PORCH (PIXELS)]", h_front_porch);
    
    
    /*  36. Find the number of lines in the odd front porch period:
     *
     *  [V ODD FRONT PORCH(LINES)]=([MIN PORCH RND]+[INTERLACE])
     *
     *  Adjusting for the fact that the interlace is scaled:
     *
     *  [V ODD FRONT PORCH(LINES)]=(([MIN PORCH RND] * 2) + [2*INTERLACE]) / 2
     */
    
    v_odd_front_porch_lines = ((2 * params->min_porch) + interlace) / 2;
    
    print_value(36, "[V ODD FRONT PORCH(LINES)]", v_odd_front_porch_lines);
    

    /* finally, pack the results in the mode struct */

    vmp->hsync_start = h_pixels + h_front_porch;
    vmp->hsync_end = vmp->hsync_start + h_sync;
    vmp->htotal = total_pixels;
    vmp->hdisplay = h_pixels;

    vmp->vsync_start = v_lines + v_odd_front_porch_lines;
    vmp->vsync_end = vmp->vsync_start + params->vsync_rqd;
    vmp->vtotal = total_v_lines;
    vmp->vdisplay = v_lines;

    vmp->dot_clock = pixel_freq;
    
}

void
vesagtf_mode(unsigned x, unsigned y, unsigned refresh, struct videomode *vmp)
{
	struct vesagtf_params	params;

	params.margin_ppt = VESAGTF_MARGIN_PPT;
	params.min_porch = VESAGTF_MIN_PORCH;
	params.vsync_rqd = VESAGTF_VSYNC_RQD;
	params.hsync_pct = VESAGTF_HSYNC_PCT;
	params.min_vsbp = VESAGTF_MIN_VSBP;
	params.M = VESAGTF_M;
	params.C = VESAGTF_C;
	params.K = VESAGTF_K;
	params.J = VESAGTF_J;

	vesagtf_mode_params(x, y, refresh, &params, 0, vmp);
}

/*
 * The tidbit here is so that you can compile this file as a
 * standalone user program to generate X11 modelines using VESA GTF.
 * This also allows for testing of the code itself, without
 * necessitating a full kernel recompile.
 */

/* print_xf86_mode() - print the XFree86 modeline, given mode timings. */

#ifndef _KERNEL
void
print_xf86_mode (struct videomode *vmp)
{
	float	vf, hf;

	hf = 1000.0 * vmp->dot_clock / vmp->htotal;
	vf = 1.0 * hf / vmp->vtotal;

    printf("\n");
    printf("  # %dx%d @ %.2f Hz (GTF) hsync: %.2f kHz; pclk: %.2f MHz\n",
	vmp->hdisplay, vmp->vdisplay, vf, hf, vmp->dot_clock / 1000.0);
    
    printf("  Modeline \"%dx%d_%.2f\"  %.2f"
	"  %d %d %d %d"
	"  %d %d %d %d"
	"  -HSync +Vsync\n\n",
	vmp->hdisplay, vmp->vdisplay, vf, (vmp->dot_clock / 1000.0),
	vmp->hdisplay, vmp->hsync_start, vmp->hsync_end, vmp->htotal,
	vmp->vdisplay, vmp->vsync_start, vmp->vsync_end, vmp->vtotal);
}

int
main (int argc, char *argv[])
{
	struct videomode m;

	if (argc != 4) {
		printf("usage: %s x y refresh\n", argv[0]);
		exit(1);
	}
    
	vesagtf_mode(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), &m);

        print_xf86_mode(&m);
    
	return 0;
    
}
#endif
