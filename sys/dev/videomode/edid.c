/* $NetBSD: edid.c,v 1.12 2013/02/08 16:35:10 skrll Exp $ */
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/libkern.h>
#include <dev/videomode/videomode.h>
#include <dev/videomode/ediddevs.h>
#include <dev/videomode/edidreg.h>
#include <dev/videomode/edidvar.h>
#include <dev/videomode/vesagtf.h>

#define	EDIDVERBOSE	1
#define	DIVIDE(x,y)	(((x) + ((y) / 2)) / (y))

/* These are reversed established timing order */
static const char *_edid_modes[] =  {
	"1280x1024x75",
	"1024x768x75",
	"1024x768x70",
	"1024x768x60",
	"1024x768x87i",
	"832x624x74",	/* rounding error, 74.55 Hz aka "832x624x75" */
	"800x600x75",
	"800x600x72",
	"800x600x60",
	"800x600x56",
	"640x480x75",
	"640x480x72",
	"640x480x67",
	"640x480x60",
	"720x400x87",	/* rounding error, 87.85 Hz aka "720x400x88" */
	"720x400x70",
};

#ifdef	EDIDVERBOSE
struct edid_vendor {
	const char	*vendor;
	const char	*name;
};

struct edid_product {
	const char	*vendor;
	uint16_t	product;
	const char	*name;
};

#include <dev/videomode/ediddevs_data.h>
#endif	/* EDIDVERBOSE */

static const char *
edid_findvendor(const char *vendor)
{
#ifdef	EDIDVERBOSE
	int	n;

	for (n = 0; n < edid_nvendors; n++)
		if (memcmp(edid_vendors[n].vendor, vendor, 3) == 0)
			return edid_vendors[n].name;
#endif
	return NULL;
}

static const char *
edid_findproduct(const char *vendor, uint16_t product)
{
#ifdef	EDIDVERBOSE
	int	n;

	for (n = 0; n < edid_nproducts; n++)
		if (edid_products[n].product == product &&
		    memcmp(edid_products[n].vendor, vendor, 3) == 0)
			return edid_products[n].name;
#endif	/* EDIDVERBOSE */
	return NULL;

}

static void
edid_strchomp(char *ptr)
{
	for (;;) {
		switch (*ptr) {
		case '\0':
			return;
		case '\r':
		case '\n':
			*ptr = '\0';
			return;
		}
		ptr++;
	}
}

int
edid_is_valid(uint8_t *d)
{
	int sum = 0, i;
	uint8_t sig[8] = EDID_SIGNATURE;
	
	if (memcmp(d, sig, 8) != 0)
		return EINVAL;
	
	for (i = 0; i < 128; i++)
		sum += d[i];
	if ((sum & 0xff) != 0)
		return EINVAL;
		
	return 0;
}

void
edid_print(struct edid_info *edid)
{
	int	i;

	if (edid == NULL)
		return;
	printf("Vendor: [%s] %s\n", edid->edid_vendor, edid->edid_vendorname);
	printf("Product: [%04X] %s\n", edid->edid_product,
	    edid->edid_productname);
	printf("Serial number: %s\n", edid->edid_serial);
	printf("Manufactured %d Week %d\n",
	    edid->edid_year, edid->edid_week);
	printf("EDID Version %d.%d\n", edid->edid_version,
	    edid->edid_revision);
	printf("EDID Comment: %s\n", edid->edid_comment);

	printf("Video Input: %x\n", edid->edid_video_input);
	if (edid->edid_video_input & EDID_VIDEO_INPUT_DIGITAL) {
		printf("\tDigital");
		if (edid->edid_video_input & EDID_VIDEO_INPUT_DFP1_COMPAT)
			printf(" (DFP 1.x compatible)");
		printf("\n");
	} else {
		printf("\tAnalog\n");
		switch (EDID_VIDEO_INPUT_LEVEL(edid->edid_video_input)) {
		case 0:
			printf("\t-0.7, 0.3V\n");
			break;
		case 1:
			printf("\t-0.714, 0.286V\n");
			break;
		case 2:
			printf("\t-1.0, 0.4V\n");
			break;
		case 3:
			printf("\t-0.7, 0.0V\n");
			break;
		}
		if (edid->edid_video_input & EDID_VIDEO_INPUT_BLANK_TO_BLACK)
			printf("\tBlank-to-black setup\n");
		if (edid->edid_video_input & EDID_VIDEO_INPUT_SEPARATE_SYNCS)
			printf("\tSeperate syncs\n");
		if (edid->edid_video_input & EDID_VIDEO_INPUT_COMPOSITE_SYNC)
			printf("\tComposite sync\n");
		if (edid->edid_video_input & EDID_VIDEO_INPUT_SYNC_ON_GRN)
			printf("\tSync on green\n");
		if (edid->edid_video_input & EDID_VIDEO_INPUT_SERRATION)
			printf("\tSerration vsync\n");
	}

	printf("Gamma: %d.%02d\n",
	    edid->edid_gamma / 100, edid->edid_gamma % 100);

	printf("Max Size: %d cm x %d cm\n",
	    edid->edid_max_hsize, edid->edid_max_vsize);

	printf("Features: %x\n", edid->edid_features);
	if (edid->edid_features & EDID_FEATURES_STANDBY)
		printf("\tDPMS standby\n");
	if (edid->edid_features & EDID_FEATURES_SUSPEND)
		printf("\tDPMS suspend\n");
	if (edid->edid_features & EDID_FEATURES_ACTIVE_OFF)
		printf("\tDPMS active-off\n");
	switch (EDID_FEATURES_DISP_TYPE(edid->edid_features)) {
	case EDID_FEATURES_DISP_TYPE_MONO:
		printf("\tMonochrome\n");
		break;
	case EDID_FEATURES_DISP_TYPE_RGB:
		printf("\tRGB\n");
		break;
	case EDID_FEATURES_DISP_TYPE_NON_RGB:
		printf("\tMulticolor\n");
		break;
	case EDID_FEATURES_DISP_TYPE_UNDEFINED:
		printf("\tUndefined monitor type\n");
		break;
	}
	if (edid->edid_features & EDID_FEATURES_STD_COLOR)
		printf("\tStandard color space\n");
	if (edid->edid_features & EDID_FEATURES_PREFERRED_TIMING)
		printf("\tPreferred timing\n");
	if (edid->edid_features & EDID_FEATURES_DEFAULT_GTF)
		printf("\tDefault GTF supported\n");

	printf("Chroma Info:\n");
	printf("\tRed X: 0.%03d\n", edid->edid_chroma.ec_redx);
	printf("\tRed Y: 0.%03d\n", edid->edid_chroma.ec_redy);
	printf("\tGrn X: 0.%03d\n", edid->edid_chroma.ec_greenx);
	printf("\tGrn Y: 0.%03d\n", edid->edid_chroma.ec_greeny);
	printf("\tBlu X: 0.%03d\n", edid->edid_chroma.ec_bluex);
	printf("\tBlu Y: 0.%03d\n", edid->edid_chroma.ec_bluey);
	printf("\tWht X: 0.%03d\n", edid->edid_chroma.ec_whitex);
	printf("\tWht Y: 0.%03d\n", edid->edid_chroma.ec_whitey);

	if (edid->edid_have_range) {
		printf("Range:\n");
		printf("\tHorizontal: %d - %d kHz\n",
		    edid->edid_range.er_min_hfreq,
		    edid->edid_range.er_max_hfreq);
		printf("\tVertical: %d - %d Hz\n",
		    edid->edid_range.er_min_vfreq,
		    edid->edid_range.er_max_vfreq);
		printf("\tMax Dot Clock: %d MHz\n",
		    edid->edid_range.er_max_clock);
		if (edid->edid_range.er_have_gtf2) {
			printf("\tGTF2 hfreq: %d\n",
			    edid->edid_range.er_gtf2_hfreq);
			printf("\tGTF2 C: %d\n", edid->edid_range.er_gtf2_c);
			printf("\tGTF2 M: %d\n", edid->edid_range.er_gtf2_m);
			printf("\tGTF2 J: %d\n", edid->edid_range.er_gtf2_j);
			printf("\tGTF2 K: %d\n", edid->edid_range.er_gtf2_k);
		}
	}
	printf("Video modes:\n");
	for (i = 0; i < edid->edid_nmodes; i++) {
		printf("\t%dx%d @ %dHz",
		    edid->edid_modes[i].hdisplay,
		    edid->edid_modes[i].vdisplay,
		    DIVIDE(DIVIDE(edid->edid_modes[i].dot_clock * 1000,
		    edid->edid_modes[i].htotal), edid->edid_modes[i].vtotal));
		printf(" (%d %d %d %d %d %d %d",
		    edid->edid_modes[i].dot_clock,
		    edid->edid_modes[i].hsync_start,
		    edid->edid_modes[i].hsync_end,
		    edid->edid_modes[i].htotal,
		    edid->edid_modes[i].vsync_start,
		    edid->edid_modes[i].vsync_end,
		    edid->edid_modes[i].vtotal);
		printf(" %s%sH %s%sV)\n",
		    edid->edid_modes[i].flags & VID_PHSYNC ? "+" : "",
		    edid->edid_modes[i].flags & VID_NHSYNC ? "-" : "",
		    edid->edid_modes[i].flags & VID_PVSYNC ? "+" : "",
		    edid->edid_modes[i].flags & VID_NVSYNC ? "-" : "");
	}
	if (edid->edid_preferred_mode)
		printf("Preferred mode: %dx%d @ %dHz\n",
		    edid->edid_preferred_mode->hdisplay,
		    edid->edid_preferred_mode->vdisplay,
		    DIVIDE(DIVIDE(edid->edid_preferred_mode->dot_clock * 1000,
		    edid->edid_preferred_mode->htotal),
		    edid->edid_preferred_mode->vtotal));

	printf("Number of extension blocks: %d\n", edid->edid_ext_block_count);
}

static const struct videomode *
edid_mode_lookup_list(const char *name)
{
	int	i;

	for (i = 0; i < videomode_count; i++)
		if (strcmp(name, videomode_list[i].name) == 0)
			return &videomode_list[i];
	return NULL;
}

static struct videomode *
edid_search_mode(struct edid_info *edid, const struct videomode *mode)
{
	int	refresh, i;

	refresh = DIVIDE(DIVIDE(mode->dot_clock * 1000,
	    mode->htotal), mode->vtotal);
	for (i = 0; i < edid->edid_nmodes; i++) {
		if (mode->hdisplay == edid->edid_modes[i].hdisplay &&
		    mode->vdisplay == edid->edid_modes[i].vdisplay &&
		    refresh == DIVIDE(DIVIDE(
		    edid->edid_modes[i].dot_clock * 1000,
		    edid->edid_modes[i].htotal), edid->edid_modes[i].vtotal)) {
			return &edid->edid_modes[i];
		}
	}
	return NULL;
}

static int
edid_std_timing(uint8_t *data, struct videomode *vmp)
{
	unsigned			x, y, f;
	const struct videomode		*lookup;
	char				name[80];

	if ((data[0] == 1 && data[1] == 1) ||
	    (data[0] == 0 && data[1] == 0) ||
	    (data[0] == 0x20 && data[1] == 0x20))
		return 0;

	x = EDID_STD_TIMING_HRES(data);
	switch (EDID_STD_TIMING_RATIO(data)) {
	case EDID_STD_TIMING_RATIO_16_10:
		y = x * 10 / 16;
		break;
	case EDID_STD_TIMING_RATIO_4_3:
		y = x * 3 / 4;
		break;
	case EDID_STD_TIMING_RATIO_5_4:
		y = x * 4 / 5;
		break;
	case EDID_STD_TIMING_RATIO_16_9:
	default:
		y = x * 9 / 16;
		break;
	}
	f = EDID_STD_TIMING_VFREQ(data);

	/* first try to lookup the mode as a DMT timing */
	snprintf(name, sizeof(name), "%dx%dx%d", x, y, f);
	if ((lookup = edid_mode_lookup_list(name)) != NULL) {
		*vmp = *lookup;
	} else {
		/* failing that, calculate it using gtf */
		/*
		 * Hmm. I'm not using alternate GTF timings, which
		 * could, in theory, be present.
		 */
		vesagtf_mode(x, y, f, vmp);
	}
	return 1;
}

static int
edid_det_timing(uint8_t *data, struct videomode *vmp)
{
	unsigned	hactive, hblank, hsyncwid, hsyncoff;
	unsigned	vactive, vblank, vsyncwid, vsyncoff;
	uint8_t		flags;

	flags = EDID_DET_TIMING_FLAGS(data);

	/* we don't support stereo modes (for now) */
	if (flags & (EDID_DET_TIMING_FLAG_STEREO |
		EDID_DET_TIMING_FLAG_STEREO_MODE))
		return 0;

	vmp->dot_clock = EDID_DET_TIMING_DOT_CLOCK(data) / 1000;

	hactive = EDID_DET_TIMING_HACTIVE(data);
	hblank = EDID_DET_TIMING_HBLANK(data);
	hsyncwid = EDID_DET_TIMING_HSYNC_WIDTH(data);
	hsyncoff = EDID_DET_TIMING_HSYNC_OFFSET(data);

	vactive = EDID_DET_TIMING_VACTIVE(data);
	vblank = EDID_DET_TIMING_VBLANK(data);
	vsyncwid = EDID_DET_TIMING_VSYNC_WIDTH(data);
	vsyncoff = EDID_DET_TIMING_VSYNC_OFFSET(data);
	
	/* Borders are contained within the blank areas. */

	vmp->hdisplay = hactive;
	vmp->htotal = hactive + hblank;
	vmp->hsync_start = hactive + hsyncoff;
	vmp->hsync_end = vmp->hsync_start + hsyncwid;

	vmp->vdisplay = vactive;
	vmp->vtotal = vactive + vblank;
	vmp->vsync_start = vactive + vsyncoff;
	vmp->vsync_end = vmp->vsync_start + vsyncwid;

	vmp->flags = 0;

	if (flags & EDID_DET_TIMING_FLAG_INTERLACE)
		vmp->flags |= VID_INTERLACE;
	if (flags & EDID_DET_TIMING_FLAG_HSYNC_POSITIVE)
		vmp->flags |= VID_PHSYNC;
	else
		vmp->flags |= VID_NHSYNC;

	if (flags & EDID_DET_TIMING_FLAG_VSYNC_POSITIVE)
		vmp->flags |= VID_PVSYNC;
	else
		vmp->flags |= VID_NVSYNC;

	return 1;
}

static void
edid_block(struct edid_info *edid, uint8_t *data)
{
	int			i;
	struct videomode	mode, *exist_mode;

	if (EDID_BLOCK_IS_DET_TIMING(data)) {
		if (!edid_det_timing(data, &mode))
			return;
		/* Does this mode already exist? */
		exist_mode = edid_search_mode(edid, &mode);
		if (exist_mode != NULL) {
			*exist_mode = mode;
			if (edid->edid_preferred_mode == NULL)
				edid->edid_preferred_mode = exist_mode;
		} else {
			edid->edid_modes[edid->edid_nmodes] = mode;
			if (edid->edid_preferred_mode == NULL)
				edid->edid_preferred_mode =
				    &edid->edid_modes[edid->edid_nmodes];
			edid->edid_nmodes++;	
		}
		return;
	}

	switch (EDID_BLOCK_TYPE(data)) {
	case EDID_DESC_BLOCK_TYPE_SERIAL:
		memcpy(edid->edid_serial, data + EDID_DESC_ASCII_DATA_OFFSET,
		    EDID_DESC_ASCII_DATA_LEN);
		edid->edid_serial[sizeof(edid->edid_serial) - 1] = 0;
		break;

	case EDID_DESC_BLOCK_TYPE_ASCII:
		memcpy(edid->edid_comment, data + EDID_DESC_ASCII_DATA_OFFSET,
		    EDID_DESC_ASCII_DATA_LEN);
		edid->edid_comment[sizeof(edid->edid_comment) - 1] = 0;
		break;

	case EDID_DESC_BLOCK_TYPE_RANGE:
		edid->edid_have_range = 1;
		edid->edid_range.er_min_vfreq =	EDID_DESC_RANGE_MIN_VFREQ(data);
		edid->edid_range.er_max_vfreq =	EDID_DESC_RANGE_MAX_VFREQ(data);
		edid->edid_range.er_min_hfreq =	EDID_DESC_RANGE_MIN_HFREQ(data);
		edid->edid_range.er_max_hfreq =	EDID_DESC_RANGE_MAX_HFREQ(data);
		edid->edid_range.er_max_clock = EDID_DESC_RANGE_MAX_CLOCK(data);
		if (!EDID_DESC_RANGE_HAVE_GTF2(data))
			break;
		edid->edid_range.er_have_gtf2 = 1;
		edid->edid_range.er_gtf2_hfreq =
		    EDID_DESC_RANGE_GTF2_HFREQ(data);
		edid->edid_range.er_gtf2_c = EDID_DESC_RANGE_GTF2_C(data);
		edid->edid_range.er_gtf2_m = EDID_DESC_RANGE_GTF2_M(data);
		edid->edid_range.er_gtf2_j = EDID_DESC_RANGE_GTF2_J(data);
		edid->edid_range.er_gtf2_k = EDID_DESC_RANGE_GTF2_K(data);
		break;

	case EDID_DESC_BLOCK_TYPE_NAME:
		/* copy the product name into place */
		memcpy(edid->edid_productname,
		    data + EDID_DESC_ASCII_DATA_OFFSET,
		    EDID_DESC_ASCII_DATA_LEN);
		break;

	case EDID_DESC_BLOCK_TYPE_STD_TIMING:
		data += EDID_DESC_STD_TIMING_START;
		for (i = 0; i < EDID_DESC_STD_TIMING_COUNT; i++) {
			if (edid_std_timing(data, &mode)) {
				/* Does this mode already exist? */
				exist_mode = edid_search_mode(edid, &mode);
				if (exist_mode == NULL) {
					edid->edid_modes[edid->edid_nmodes] =
					    mode;
					edid->edid_nmodes++;
				}
			}
			data += 2;
		}
		break;

	case EDID_DESC_BLOCK_TYPE_COLOR_POINT:
		/* XXX: not implemented yet */
		break;
	}
}

/*
 * Gets EDID version in BCD, e.g. EDID v1.3  returned as 0x0103
 */
int
edid_parse(uint8_t *data, struct edid_info *edid)
{
	uint16_t		manfid, estmodes;
	const struct videomode	*vmp;
	int			i;
	const char		*name;
	int max_dotclock = 0;
	int mhz;

	if (edid_is_valid(data) != 0)
		return -1;

	/* get product identification */
	manfid = EDID_VENDOR_ID(data);
	edid->edid_vendor[0] = EDID_MANFID_0(manfid);
	edid->edid_vendor[1] = EDID_MANFID_1(manfid);
	edid->edid_vendor[2] = EDID_MANFID_2(manfid);
	edid->edid_vendor[3] = 0;	/* null terminate for convenience */

	edid->edid_product = data[EDID_OFFSET_PRODUCT_ID] + 
	    (data[EDID_OFFSET_PRODUCT_ID + 1] << 8);

	name = edid_findvendor(edid->edid_vendor);
	if (name != NULL)
		strlcpy(edid->edid_vendorname, name,
		    sizeof(edid->edid_vendorname));
	else
		edid->edid_vendorname[0] = '\0';

	name = edid_findproduct(edid->edid_vendor, edid->edid_product);
	if (name != NULL)
		strlcpy(edid->edid_productname, name,
		    sizeof(edid->edid_productname));
	else
	    edid->edid_productname[0] = '\0';

	snprintf(edid->edid_serial, sizeof(edid->edid_serial), "%08x",
	    EDID_SERIAL_NUMBER(data));

	edid->edid_week = EDID_WEEK(data);
	edid->edid_year = EDID_YEAR(data);

	/* get edid revision */
	edid->edid_version = EDID_VERSION(data);
	edid->edid_revision = EDID_REVISION(data);

	edid->edid_video_input = EDID_VIDEO_INPUT(data);
	edid->edid_max_hsize = EDID_MAX_HSIZE(data);
	edid->edid_max_vsize = EDID_MAX_VSIZE(data);

	edid->edid_gamma = EDID_GAMMA(data);
	edid->edid_features = EDID_FEATURES(data);

	edid->edid_chroma.ec_redx = EDID_CHROMA_REDX(data);
	edid->edid_chroma.ec_redy = EDID_CHROMA_REDX(data);
	edid->edid_chroma.ec_greenx = EDID_CHROMA_GREENX(data);
	edid->edid_chroma.ec_greeny = EDID_CHROMA_GREENY(data);
	edid->edid_chroma.ec_bluex = EDID_CHROMA_BLUEX(data);
	edid->edid_chroma.ec_bluey = EDID_CHROMA_BLUEY(data);
	edid->edid_chroma.ec_whitex = EDID_CHROMA_WHITEX(data);
	edid->edid_chroma.ec_whitey = EDID_CHROMA_WHITEY(data);

	edid->edid_ext_block_count = EDID_EXT_BLOCK_COUNT(data);

	/* lookup established modes */
	edid->edid_nmodes = 0;
	edid->edid_preferred_mode = NULL;
	estmodes = EDID_EST_TIMING(data);
	/* Iterate in esztablished timing order */
	for (i = 15; i >= 0; i--) {
		if (estmodes & (1 << i)) {
			vmp = edid_mode_lookup_list(_edid_modes[i]);
			if (vmp != NULL) {
				edid->edid_modes[edid->edid_nmodes] = *vmp;
				edid->edid_nmodes++;
			}
#ifdef DIAGNOSTIC
			  else
				printf("no data for est. mode %s\n",
				    _edid_modes[i]);
#endif
		}
	}

	/* do standard timing section */
	for (i = 0; i < EDID_STD_TIMING_COUNT; i++) {
		struct videomode	mode, *exist_mode;
		if (edid_std_timing(data + EDID_OFFSET_STD_TIMING + i * 2,
			&mode)) {
			/* Does this mode already exist? */
			exist_mode = edid_search_mode(edid, &mode);
			if (exist_mode == NULL) {
				edid->edid_modes[edid->edid_nmodes] = mode;
				edid->edid_nmodes++;
			}
		}
	}

	/* do detailed timings and descriptors */
	for (i = 0; i < EDID_BLOCK_COUNT; i++) {
		edid_block(edid, data + EDID_OFFSET_DESC_BLOCK +
		    i * EDID_BLOCK_SIZE);
	}

	edid_strchomp(edid->edid_vendorname);
	edid_strchomp(edid->edid_productname);
	edid_strchomp(edid->edid_serial);
	edid_strchomp(edid->edid_comment);

	/*
	 * XXX
	 * some monitors lie about their maximum supported dot clock
	 * by claiming to support modes which need a higher dot clock
	 * than the stated maximum.
	 * For sanity's sake we bump it to the highest dot clock we find
	 * in the list of supported modes
	 */
	for (i = 0; i < edid->edid_nmodes; i++)
		if (edid->edid_modes[i].dot_clock > max_dotclock)
			max_dotclock = edid->edid_modes[i].dot_clock;
	if (bootverbose) {
		printf("edid: max_dotclock according to supported modes: %d\n",
		    max_dotclock);
	}
	mhz = (max_dotclock + 999) / 1000;

	if (edid->edid_have_range) {
		if (mhz > edid->edid_range.er_max_clock)
			edid->edid_range.er_max_clock = mhz;
	} else
		edid->edid_range.er_max_clock = mhz;

	return 0;
}

