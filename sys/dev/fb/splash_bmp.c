/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <sys/fbio.h>

#include <dev/fb/fbreg.h>
#include <dev/fb/splashreg.h>
#include <dev/fb/vgareg.h>

#include <isa/isareg.h>

#define FADE_TIMEOUT	15	/* sec */
#define FADE_LEVELS	10

static int splash_mode = -1;
static int splash_on = FALSE;

static int bmp_start(video_adapter_t *adp);
static int bmp_end(video_adapter_t *adp);
static int bmp_splash(video_adapter_t *adp, int on);
static int bmp_Init(char *data, int swidth, int sheight, int sdepth);
static int bmp_Draw(video_adapter_t *adp);

static splash_decoder_t bmp_decoder = {
    "splash_bmp", bmp_start, bmp_end, bmp_splash, SPLASH_IMAGE,
};

SPLASH_DECODER(splash_bmp, bmp_decoder);

static int 
bmp_start(video_adapter_t *adp)
{
    /* currently only 256-color modes are supported XXX */
    static int		modes[] = {
			M_VESA_CG640x480,
			M_VESA_CG800x600,
			M_VESA_CG1024x768,
			M_CG640x480,
    			/*
			 * As 320x200 doesn't generally look great,
			 * it's least preferred here.
			 */
			M_VGA_CG320,
			-1,
    };
    video_info_t 	info;
    int			i;

    if ((bmp_decoder.data == NULL) || (bmp_decoder.data_size <= 0)) {
	printf("splash_bmp: No bitmap file found\n");
	return ENODEV;
    }
    for (i = 0; modes[i] >= 0; ++i) {
	if ((vidd_get_info(adp, modes[i], &info) == 0) && 
	    (bmp_Init((u_char *)bmp_decoder.data, info.vi_width,
		      info.vi_height, info.vi_depth) == 0))
	    break;
    }
    splash_mode = modes[i];
    if (splash_mode < 0)
	printf("splash_bmp: No appropriate video mode found\n");
    if (bootverbose)
	printf("bmp_start(): splash_mode:%d\n", splash_mode);
    return ((splash_mode < 0) ? ENODEV : 0);
}

static int
bmp_end(video_adapter_t *adp)
{
    /* nothing to do */
    return 0;
}

static int
bmp_splash(video_adapter_t *adp, int on)
{
    static u_char	pal[256*3];
    static long		time_stamp;
    u_char		tpal[256*3];
    static int		fading = TRUE, brightness = FADE_LEVELS;
    struct timeval	tv;
    int			i;

    if (on) {
	if (!splash_on) {
	    /* set up the video mode and draw something */
	    if (vidd_set_mode(adp, splash_mode))
		return 1;
	    if (bmp_Draw(adp))
		return 1;
	    vidd_save_palette(adp, pal);
	    time_stamp = 0;
	    splash_on = TRUE;
	}
	/*
	 * This is a kludge to fade the image away.  This section of the 
	 * code takes effect only after the system is completely up.
	 * FADE_TIMEOUT should be configurable.
	 */
	if (!cold) {
	    getmicrotime(&tv);
	    if (time_stamp == 0)
		time_stamp = tv.tv_sec;
	    if (tv.tv_sec > time_stamp + FADE_TIMEOUT) {
		if (fading)
		    if (brightness == 0) {
			fading = FALSE;
			brightness++;
		    }
		    else brightness--;
		else
		    if (brightness == FADE_LEVELS) {
			fading = TRUE;
			brightness--;
		    }
		    else brightness++;
		for (i = 0; i < sizeof(pal); ++i) {
		    tpal[i] = pal[i] * brightness / FADE_LEVELS;
		}
		vidd_load_palette(adp, tpal);
		time_stamp = tv.tv_sec;
	    }
	}
	return 0;
    } else {
	/* the video mode will be restored by the caller */
	splash_on = FALSE;
	return 0;
    }
}

/*
** Code to handle Microsoft DIB (".BMP") format images.
**
** Blame me (msmith@freebsd.org) if this is broken, not Soren.
*/

typedef struct tagBITMAPFILEHEADER {    /* bmfh */
    u_short	bfType;
    int		bfSize;
    u_short	bfReserved1;
    u_short	bfReserved2;
    int		bfOffBits;
} __packed BITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER {    /* bmih */
    int		biSize;
    int		biWidth;
    int		biHeight;
    short	biPlanes;
    short	biBitCount;
    int		biCompression;
    int		biSizeImage;
    int		biXPelsPerMeter;
    int		biYPelsPerMeter;
    int		biClrUsed;
    int		biClrImportant;
} __packed BITMAPINFOHEADER;

typedef struct tagRGBQUAD {     /* rgbq */
    u_char	rgbBlue;
    u_char	rgbGreen;
    u_char	rgbRed;
    u_char	rgbReserved;
} __packed RGBQUAD;

typedef struct tagBITMAPINFO {  /* bmi */
    BITMAPINFOHEADER	bmiHeader;
    RGBQUAD		bmiColors[256];
} __packed BITMAPINFO;

typedef struct tagBITMAPF
{
    BITMAPFILEHEADER	bmfh;
    BITMAPINFO		bmfi;
} __packed BITMAPF;

#define BI_RGB		0
#define BI_RLE8		1
#define BI_RLE4		2

/* 
** all we actually care about the image
*/
typedef struct
{
    int		width,height;		/* image dimensions */
    int		swidth,sheight;		/* screen dimensions for the current mode */
    u_char	depth;			/* image depth (1, 4, 8, 24 bits) */
    u_char	sdepth;			/* screen depth (1, 4, 8 bpp) */
    int		ncols;			/* number of colours */
    u_char	palette[256][3];	/* raw palette data */
    u_char	format;			/* one of the BI_* constants above */
    u_char	*data;			/* pointer to the raw data */
    u_char	*index;			/* running pointer to the data while drawing */
    u_char	*vidmem;		/* video memory allocated for drawing */
    video_adapter_t *adp;
    int		bank;
} BMP_INFO;

static BMP_INFO bmp_info;

/*
** bmp_SetPix
**
** Given (info), set the pixel at (x),(y) to (val) 
**
*/
static void
bmp_SetPix(BMP_INFO *info, int x, int y, u_char val)
{
    int		sofs, bofs;
    int		newbank;

    /*
     * range check to avoid explosions
     */
    if ((x < 0) || (x >= info->swidth) || (y < 0) || (y >= info->sheight))
	return;
    
    /* 
     * calculate offset into video memory;
     * because 0,0 is bottom-left for DIB, we have to convert.
     */
    sofs = ((info->height - (y+1) + (info->sheight - info->height) / 2) 
		* info->adp->va_line_width);
    x += (info->swidth - info->width) / 2;

    switch(info->sdepth) {
    case 4:
    case 1:
	/* EGA/VGA planar modes */
	sofs += (x >> 3);
	newbank = sofs/info->adp->va_window_size;
	if (info->bank != newbank) {
	    vidd_set_win_org(info->adp, newbank*info->adp->va_window_size);
	    info->bank = newbank;
	}
	sofs %= info->adp->va_window_size;
	bofs = x & 0x7;				/* offset within byte */
	outw(GDCIDX, (0x8000 >> bofs) | 0x08);	/* bit mask */
	outw(GDCIDX, (val << 8) | 0x00);	/* set/reset */
	*(info->vidmem + sofs) ^= 0xff;		/* read-modify-write */
	break;

    case 8:
	sofs += x;
	newbank = sofs/info->adp->va_window_size;
	if (info->bank != newbank) {
	    vidd_set_win_org(info->adp, newbank*info->adp->va_window_size);
	    info->bank = newbank;
	}
	sofs %= info->adp->va_window_size;
	*(info->vidmem+sofs) = val;
	break;
    }
}
    
/*
** bmp_DecodeRLE4
**
** Given (data) pointing to a line of RLE4-format data and (line) being the starting
** line onscreen, decode the line.
*/
static void
bmp_DecodeRLE4(BMP_INFO *info, int line)
{
    int		count;		/* run count */
    u_char	val;
    int		x,y;		/* screen position */
    
    x = 0;			/* starting position */
    y = line;
    
    /* loop reading data */
    for (;;) {
	/*
	 * encoded mode starts with a run length, and then a byte with
	 * two colour indexes to alternate between for the run
	 */
	if (*info->index) {
	    for (count = 0; count < *info->index; count++, x++) {
		if (count & 1) {		/* odd count, low nybble */
		    bmp_SetPix(info, x, y, *(info->index+1) & 0x0f);
		} else {			/* even count, high nybble */
		    bmp_SetPix(info, x, y, (*(info->index+1) >>4) & 0x0f);
		}
	    }
	    info->index += 2;
        /* 
	 * A leading zero is an escape; it may signal the end of the 
	 * bitmap, a cursor move, or some absolute data.
	 */
	} else {	/* zero tag may be absolute mode or an escape */
	    switch (*(info->index+1)) {
	    case 0:				/* end of line */
		info->index += 2;
		return;
	    case 1:				/* end of bitmap */
		info->index = NULL;
		return;
	    case 2:				/* move */
		x += *(info->index + 2);	/* new coords */
		y += *(info->index + 3);
		info->index += 4;
		break;
	    default:				/* literal bitmap data */
		for (count = 0; count < *(info->index + 1); count++, x++) {
		    val = *(info->index + 2 + (count / 2));	/* byte with nybbles */
		    if (count & 1) {
			val &= 0xf;		/* get low nybble */
		    } else {
			val = (val >> 4);	/* get high nybble */
		    }
		    bmp_SetPix(info, x, y, val);
		}
		/* warning, this depends on integer truncation, do not hand-optimise! */
		info->index += 2 + ((count + 3) / 4) * 2;
		break;
	    }
	}
    }
}

/*
** bmp_DecodeRLE8
** Given (data) pointing to a line of RLE8-format data and (line) being the starting
** line onscreen, decode the line.
*/
static void
bmp_DecodeRLE8(BMP_INFO *info, int line)
{
    int		count;		/* run count */
    int		x,y;		/* screen position */
    
    x = 0;			/* starting position */
    y = line;
    
    /* loop reading data */
    for(;;) {
	/*
	 * encoded mode starts with a run length, and then a byte with
	 * two colour indexes to alternate between for the run
	 */
	if (*info->index) {
	    for (count = 0; count < *info->index; count++, x++)
		bmp_SetPix(info, x, y, *(info->index+1));
	    info->index += 2;
        /* 
	 * A leading zero is an escape; it may signal the end of the 
	 * bitmap, a cursor move, or some absolute data.
	 */
	} else {	/* zero tag may be absolute mode or an escape */
	    switch(*(info->index+1)) {
	    case 0:				/* end of line */
		info->index += 2;
		return;
	    case 1:				/* end of bitmap */
		info->index = NULL;
		return;
	    case 2:				/* move */
		x += *(info->index + 2);	/* new coords */
		y += *(info->index + 3);
		info->index += 4;
		break;
	    default:				/* literal bitmap data */
		for (count = 0; count < *(info->index + 1); count++, x++)
		    bmp_SetPix(info, x, y, *(info->index + 2 + count));
		/* must be an even count */
		info->index += 2 + count + (count & 1);
		break;
	    }
	}
    }
}

/*
** bmp_DecodeLine
**
** Given (info) pointing to an image being decoded, (line) being the line currently
** being displayed, decode a line of data.
*/
static void
bmp_DecodeLine(BMP_INFO *info, int line)
{
    int		x;
    u_char	val, mask, *p;

    switch(info->format) {
    case BI_RGB:
    	switch(info->depth) {
	case 8:
	    for (x = 0; x < info->width; x++, info->index++)
		bmp_SetPix(info, x, line, *info->index);
	    info->index += 3 - (--x % 4);
	    break;
	case 4:
	    p = info->index;
	    for (x = 0; x < info->width; x++) {
		if (x & 1) {
		    val = *p & 0xf;	/* get low nybble */
		    p++;
		} else {
		    val = *p >> 4;	/* get high nybble */
		}
		bmp_SetPix(info, x, line, val);
	    }
	    /* warning, this depends on integer truncation, do not hand-optimise! */
	    info->index += ((x + 7) / 8) * 4;
	    break;
	case 1:
	    p = info->index;
	    mask = 0x80;
	    for (x = 0; x < info->width; x++) {
		val = (*p & mask) ? 1 : 0;
		mask >>= 1;
		if (mask == 0) {
		    mask = 0x80;
		    p++;
		}
		bmp_SetPix(info, x, line, val);
	    }
	    /* warning, this depends on integer truncation, do not hand-optimise! */
	    info->index += ((x + 31) / 32) * 4;
	    break;
	}
	break;
    case BI_RLE4:
	bmp_DecodeRLE4(info, line);
	break;
    case BI_RLE8:
	bmp_DecodeRLE8(info, line);
	break;
    }
}

/*
** bmp_Init
**
** Given a pointer (data) to the image of a BMP file, fill in bmp_info with what
** can be learnt from it.  Return nonzero if the file isn't usable.
**
** Take screen dimensions (swidth), (sheight) and (sdepth) and make sure we
** can work with these.
*/
static int
bmp_Init(char *data, int swidth, int sheight, int sdepth)
{
    BITMAPF	*bmf = (BITMAPF *)data;
    int		pind;

    bmp_info.data = NULL;	/* assume setup failed */

    /* check file ID */
    if (bmf->bmfh.bfType != 0x4d42) {
	printf("splash_bmp: not a BMP file\n");
	return(1);		/* XXX check word ordering for big-endian ports? */
    }

    /* do we understand this bitmap format? */
    if (bmf->bmfi.bmiHeader.biSize > sizeof(bmf->bmfi.bmiHeader)) {
	printf("splash_bmp: unsupported BMP format (size=%d)\n",
		bmf->bmfi.bmiHeader.biSize);
	return(1);
    }

    /* save what we know about the screen */
    bmp_info.swidth = swidth;
    bmp_info.sheight = sheight;
    bmp_info.sdepth = sdepth;

    /* where's the data? */
    bmp_info.data = (u_char *)data + bmf->bmfh.bfOffBits;

    /* image parameters */
    bmp_info.width = bmf->bmfi.bmiHeader.biWidth;
    bmp_info.height = bmf->bmfi.bmiHeader.biHeight;
    bmp_info.depth = bmf->bmfi.bmiHeader.biBitCount;
    bmp_info.format = bmf->bmfi.bmiHeader.biCompression;

    switch(bmp_info.format) {	/* check compression format */
    case BI_RGB:
    case BI_RLE4:
    case BI_RLE8:
	break;
    default:
	printf("splash_bmp: unsupported compression format\n");
	return(1);		/* unsupported compression format */
    }
    
    /* palette details */
    bmp_info.ncols = (bmf->bmfi.bmiHeader.biClrUsed);
    bzero(bmp_info.palette,sizeof(bmp_info.palette));
    if (bmp_info.ncols == 0) {	/* uses all of them */
	bmp_info.ncols = 1 << bmf->bmfi.bmiHeader.biBitCount;
    }
    if ((bmp_info.height > bmp_info.sheight) ||
	(bmp_info.width > bmp_info.swidth) ||
	(bmp_info.ncols > (1 << sdepth))) {
	if (bootverbose)
	    printf("splash_bmp: beyond screen capacity (%dx%d, %d colors)\n",
		   bmp_info.width, bmp_info.height, bmp_info.ncols);
	return(1);
    }

    /* read palette */
    for (pind = 0; pind < bmp_info.ncols; pind++) {
	bmp_info.palette[pind][0] = bmf->bmfi.bmiColors[pind].rgbRed;
	bmp_info.palette[pind][1] = bmf->bmfi.bmiColors[pind].rgbGreen;
	bmp_info.palette[pind][2] = bmf->bmfi.bmiColors[pind].rgbBlue;
    }
    return(0);
}

/*
** bmp_Draw
**
** Render the image.  Return nonzero if that's not possible.
**
*/
static int
bmp_Draw(video_adapter_t *adp)
{
    int		line;
#if 0
    int		i;
#endif

    if (bmp_info.data == NULL) {	/* init failed, do nothing */
	return(1);
    }
    
    /* clear the screen */
    bmp_info.vidmem = (u_char *)adp->va_window;
    bmp_info.adp = adp;
    vidd_clear(adp);
    vidd_set_win_org(adp, 0);
    bmp_info.bank = 0;

    /* initialise the info structure for drawing */
    bmp_info.index = bmp_info.data;
    
    /* set the palette for our image */
    vidd_load_palette(adp, (u_char *)&bmp_info.palette);

#if 0
    /* XXX: this is ugly, but necessary for EGA/VGA 1bpp/4bpp modes */
    if ((adp->va_type == KD_EGA) || (adp->va_type == KD_VGA)) {
	inb(adp->va_crtc_addr + 6);		/* reset flip-flop */
	outb(ATC, 0x14);
	outb(ATC, 0);
	for (i = 0; i < 16; ++i) {
	    outb(ATC, i);
	    outb(ATC, i);
	}
	inb(adp->va_crtc_addr + 6);		/* reset flip-flop */
	outb(ATC, 0x20);			/* enable palette */

	outw(GDCIDX, 0x0f01);			/* set/reset enable */

	if (bmp_info.sdepth == 1)
	    outw(TSIDX, 0x0102);		/* unmask plane #0 */
    }
#endif

    for (line = 0; (line < bmp_info.height) && bmp_info.index; line++) {
	bmp_DecodeLine(&bmp_info, line);
    }
    return(0);
}
