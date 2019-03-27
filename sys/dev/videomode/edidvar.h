/*	$NetBSD: edidvar.h,v 1.2 2006/05/11 19:05:41 gdamore Exp $	*/
/*	$FreeBSD$	*/

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

#ifndef _DEV_VIDEOMODE_EDIDVAR_H
#define _DEV_VIDEOMODE_EDIDVAR_H

struct edid_chroma {
	uint16_t	ec_redx;
	uint16_t	ec_redy;
	uint16_t	ec_greenx;
	uint16_t	ec_greeny;
	uint16_t	ec_bluex;
	uint16_t	ec_bluey;
	uint16_t	ec_whitex;
	uint16_t	ec_whitey;
};

struct edid_range {
	uint16_t	er_min_vfreq;	/* Hz */
	uint16_t	er_max_vfreq;	/* Hz */
	uint16_t	er_min_hfreq;	/* kHz */
	uint16_t	er_max_hfreq;	/* kHz */
	uint16_t	er_max_clock;	/* MHz */
	int		er_have_gtf2;
	uint16_t	er_gtf2_hfreq;
	uint16_t	er_gtf2_c;
	uint16_t	er_gtf2_m;
	uint16_t	er_gtf2_k;
	uint16_t	er_gtf2_j;
};

struct edid_info {
	uint8_t		edid_vendor[4];
	char		edid_vendorname[16];
	char		edid_productname[16];
	char		edid_comment[16];
	char		edid_serial[16];
	uint16_t	edid_product;
	uint8_t		edid_version;
	uint8_t		edid_revision;
	int		edid_year;
	int		edid_week;
	uint8_t		edid_video_input;	/* see edidregs.h */
	uint8_t		edid_max_hsize;		/* in cm */
	uint8_t		edid_max_vsize;		/* in cm */
	uint8_t		edid_gamma;
	uint8_t		edid_features;
	uint8_t		edid_ext_block_count;

	int			edid_have_range;
	struct edid_range	edid_range;

	struct edid_chroma	edid_chroma;

	/* parsed modes */
	struct videomode	*edid_preferred_mode;
	int			edid_nmodes;
	struct videomode	edid_modes[64];
};

int edid_is_valid(uint8_t *);
int edid_parse(uint8_t *, struct edid_info *);
void edid_print(struct edid_info *);

#endif /* _DEV_VIDEOMODE_EDIDVAR_H */
