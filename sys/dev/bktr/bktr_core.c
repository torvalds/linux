/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * 1. Redistributions of source code must retain the 
 * Copyright (c) 1997 Amancio Hasty, 1999 Roger Hardiman
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Amancio Hasty and
 *      Roger Hardiman
 * 4. The name of the author may not be used to endorse or promote products 
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * 1. Redistributions of source code must retain the 
 * Copyright (c) 1995 Mark Tinguely and Jim Lowe
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Mark Tinguely and Jim Lowe
 * 4. The name of the author may not be used to endorse or promote products 
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This is part of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman and Amancio Hasty.
 *
 * bktr_core : This deals with the Bt848/849/878/879 PCI Frame Grabber,
 *               Handles all the open, close, ioctl and read userland calls.
 *               Sets the Bt848 registers and generates RISC pograms.
 *               Controls the i2c bus and GPIO interface.
 *               Contains the interface to the kernel.
 *               (eg probe/attach and open/close/ioctl)
 */

 /*
   The Brooktree BT848 Driver driver is based upon Mark Tinguely and
   Jim Lowe's driver for the Matrox Meteor PCI card . The 
   Philips SAA 7116 and SAA 7196 are very different chipsets than
   the BT848.

   The original copyright notice by Mark and Jim is included mostly
   to honor their fantastic work in the Matrox Meteor driver!
 */

#include "opt_bktr.h"		/* Include any kernel config options */

#if (                                                            \
       (defined(__FreeBSD__))                                    \
    || (defined(__bsdi__))                                       \
    || (defined(__OpenBSD__))                                    \
    || (defined(__NetBSD__))                                     \
    )


/*******************/
/* *** FreeBSD *** */
/*******************/
#ifdef __FreeBSD__

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/selinfo.h>
#include <sys/uio.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <sys/bus.h>		/* used by smbus and newbus */

#if (__FreeBSD_version < 500000)
#include <machine/clock.h>              /* for DELAY */
#define	PROC_LOCK(p)
#define	PROC_UNLOCK(p)
#include <pci/pcivar.h>
#else
#include <dev/pci/pcivar.h>
#endif

#include <machine/bus.h>
#include <sys/bus.h>

#include <dev/bktr/ioctl_meteor.h>
#include <dev/bktr/ioctl_bt848.h>	/* extensions to ioctl_meteor.h */
#include <dev/bktr/bktr_reg.h>
#include <dev/bktr/bktr_tuner.h>
#include <dev/bktr/bktr_card.h>
#include <dev/bktr/bktr_audio.h>
#include <dev/bktr/bktr_os.h>
#include <dev/bktr/bktr_core.h>
#if defined(BKTR_FREEBSD_MODULE)
#include <dev/bktr/bktr_mem.h>
#endif

#if defined(BKTR_USE_FREEBSD_SMBUS)
#include <dev/bktr/bktr_i2c.h>
#include <dev/smbus/smbconf.h>
#include <dev/iicbus/iiconf.h>
#include "smbus_if.h"
#include "iicbus_if.h"
#endif

const char *
bktr_name(bktr_ptr_t bktr)
{
  return bktr->bktr_xname;
}


#endif  /* __FreeBSD__ */


/****************/
/* *** BSDI *** */
/****************/
#ifdef __bsdi__
#define	PROC_LOCK(p)
#define	PROC_UNLOCK(p)
#endif /* __bsdi__ */


/**************************/
/* *** OpenBSD/NetBSD *** */
/**************************/
#if defined(__NetBSD__) || defined(__OpenBSD__)

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>

#ifdef __NetBSD__
#include <uvm/uvm_extern.h>
#else
#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#endif

#include <sys/inttypes.h>		/* uintptr_t */
#include <dev/ic/bt8xx.h>
#include <dev/pci/bktr/bktr_reg.h>
#include <dev/pci/bktr/bktr_tuner.h>
#include <dev/pci/bktr/bktr_card.h>
#include <dev/pci/bktr/bktr_audio.h>
#include <dev/pci/bktr/bktr_core.h>
#include <dev/pci/bktr/bktr_os.h>

static int bt848_format = -1;

const char *
bktr_name(bktr_ptr_t bktr)
{
        return (bktr->bktr_dev.dv_xname);
}

#define	PROC_LOCK(p)
#define	PROC_UNLOCK(p)

#endif /* __NetBSD__ || __OpenBSD__ */


typedef u_char bool_t;

#define BKTRPRI (PZERO+8)|PCATCH
#define VBIPRI  (PZERO-4)|PCATCH


/*
 * memory allocated for DMA programs
 */
#define DMA_PROG_ALLOC		(8 * PAGE_SIZE)

/* When to split a dma transfer , the bt848 has timing as well as
   dma transfer size limitations so that we have to split dma
   transfers into two dma requests 
   */
#define DMA_BT848_SPLIT 319*2

/* 
 * Allocate enough memory for:
 *	768x576 RGB 16 or YUV (16 storage bits/pixel) = 884736 = 216 pages
 *
 * You may override this using the options "BROOKTREE_ALLOC_PAGES=value"
 * in your  kernel configuration file.
 */

#ifndef BROOKTREE_ALLOC_PAGES
#define BROOKTREE_ALLOC_PAGES	217*4
#endif
#define BROOKTREE_ALLOC		(BROOKTREE_ALLOC_PAGES * PAGE_SIZE)

/* Definitions for VBI capture.
 * There are 16 VBI lines in a PAL video field (32 in a frame),
 * and we take 2044 samples from each line (placed in a 2048 byte buffer
 * for alignment).
 * VBI lines are held in a circular buffer before being read by a
 * user program from /dev/vbi.
 */

#define MAX_VBI_LINES	      16   /* Maximum for all vidoe formats */
#define	VBI_LINE_SIZE         2048 /* Store up to 2048 bytes per line */
#define VBI_BUFFER_ITEMS      20   /* Number of frames we buffer */
#define VBI_DATA_SIZE         (VBI_LINE_SIZE * MAX_VBI_LINES * 2)
#define VBI_BUFFER_SIZE       (VBI_DATA_SIZE * VBI_BUFFER_ITEMS)


/*  Defines for fields  */
#define ODD_F  0x01
#define EVEN_F 0x02


/*
 * Parameters describing size of transmitted image.
 */

static struct format_params format_params[] = {
/* # define BT848_IFORM_F_AUTO             (0x0) - don't matter. */
  { 525, 26, 480,  910, 135, 754, 640,  780, 30, 0x68, 0x5d, BT848_IFORM_X_AUTO,
    12,  1600 },
/* # define BT848_IFORM_F_NTSCM            (0x1) */
  { 525, 26, 480,  910, 135, 754, 640,  780, 30, 0x68, 0x5d, BT848_IFORM_X_XT0,
    12, 1600 },
/* # define BT848_IFORM_F_NTSCJ            (0x2) */
  { 525, 22, 480,  910, 135, 754, 640,  780, 30, 0x68, 0x5d, BT848_IFORM_X_XT0,
    12, 1600 },
/* # define BT848_IFORM_F_PALBDGHI         (0x3) */
  { 625, 32, 576, 1135, 186, 924, 768,  944, 25, 0x7f, 0x72, BT848_IFORM_X_XT1,
    16,  2044 },
/* # define BT848_IFORM_F_PALM             (0x4) */
  { 525, 22, 480,  910, 135, 754, 640,  780, 30, 0x68, 0x5d, BT848_IFORM_X_XT0,
    12, 1600 },
/* # define BT848_IFORM_F_PALN             (0x5) */
  { 625, 32, 576, 1135, 186, 924, 768,  944, 25, 0x7f, 0x72, BT848_IFORM_X_XT1,
    16, 2044 },
/* # define BT848_IFORM_F_SECAM            (0x6) */
  { 625, 32, 576, 1135, 186, 924, 768,  944, 25, 0x7f, 0xa0, BT848_IFORM_X_XT1,
    16, 2044 },
/* # define BT848_IFORM_F_RSVD             (0x7) - ???? */
  { 625, 32, 576, 1135, 186, 924, 768,  944, 25, 0x7f, 0x72, BT848_IFORM_X_XT0,
    16, 2044 },
};

/*
 * Table of supported Pixel Formats 
 */

static struct meteor_pixfmt_internal {
	struct meteor_pixfmt public;
	u_int                color_fmt;
} pixfmt_table[] = {

{ { 0, METEOR_PIXTYPE_RGB, 2, {   0x7c00,  0x03e0,  0x001f }, 0,0 }, 0x33 },
{ { 0, METEOR_PIXTYPE_RGB, 2, {   0x7c00,  0x03e0,  0x001f }, 1,0 }, 0x33 },

{ { 0, METEOR_PIXTYPE_RGB, 2, {   0xf800,  0x07e0,  0x001f }, 0,0 }, 0x22 },
{ { 0, METEOR_PIXTYPE_RGB, 2, {   0xf800,  0x07e0,  0x001f }, 1,0 }, 0x22 },

{ { 0, METEOR_PIXTYPE_RGB, 3, { 0xff0000,0x00ff00,0x0000ff }, 1,0 }, 0x11 },

{ { 0, METEOR_PIXTYPE_RGB, 4, { 0xff0000,0x00ff00,0x0000ff }, 0,0 }, 0x00 },
{ { 0, METEOR_PIXTYPE_RGB, 4, { 0xff0000,0x00ff00,0x0000ff }, 0,1 }, 0x00 },
{ { 0, METEOR_PIXTYPE_RGB, 4, { 0xff0000,0x00ff00,0x0000ff }, 1,0 }, 0x00 },
{ { 0, METEOR_PIXTYPE_RGB, 4, { 0xff0000,0x00ff00,0x0000ff }, 1,1 }, 0x00 },
{ { 0, METEOR_PIXTYPE_YUV, 2, { 0xff0000,0x00ff00,0x0000ff }, 1,1 }, 0x88 },
{ { 0, METEOR_PIXTYPE_YUV_PACKED, 2, { 0xff0000,0x00ff00,0x0000ff }, 0,1 }, 0x44 },
{ { 0, METEOR_PIXTYPE_YUV_12, 2, { 0xff0000,0x00ff00,0x0000ff }, 1,1 }, 0x88 },

};
#define	PIXFMT_TABLE_SIZE nitems(pixfmt_table)

/*
 * Table of Meteor-supported Pixel Formats (for SETGEO compatibility)
 */

/*  FIXME:  Also add YUV_422 and YUV_PACKED as well  */
static struct {
	u_long               meteor_format;
	struct meteor_pixfmt public;
} meteor_pixfmt_table[] = {
    { METEOR_GEO_YUV_12,
      { 0, METEOR_PIXTYPE_YUV_12, 2, { 0xff0000,0x00ff00,0x0000ff }, 1,1 }
    },

      /* FIXME: Should byte swap flag be on for this one; negative in drvr? */
    { METEOR_GEO_YUV_422,
      { 0, METEOR_PIXTYPE_YUV, 2, { 0xff0000,0x00ff00,0x0000ff }, 1,1 }
    },
    { METEOR_GEO_YUV_PACKED,
      { 0, METEOR_PIXTYPE_YUV_PACKED, 2, { 0xff0000,0x00ff00,0x0000ff }, 0,1 }
    },
    { METEOR_GEO_RGB16,
      { 0, METEOR_PIXTYPE_RGB, 2, {   0x7c00,   0x03e0,   0x001f }, 0, 0 }
    },
    { METEOR_GEO_RGB24,
      { 0, METEOR_PIXTYPE_RGB, 4, { 0xff0000, 0x00ff00, 0x0000ff }, 0, 0 }
    },

};
#define	METEOR_PIXFMT_TABLE_SIZE nitems(meteor_pixfmt_table)


#define BSWAP (BT848_COLOR_CTL_BSWAP_ODD | BT848_COLOR_CTL_BSWAP_EVEN)
#define WSWAP (BT848_COLOR_CTL_WSWAP_ODD | BT848_COLOR_CTL_WSWAP_EVEN)



/* sync detect threshold */
#if 0
#define SYNC_LEVEL		(BT848_ADC_RESERVED |	\
				 BT848_ADC_CRUSH)	/* threshold ~125 mV */
#else
#define SYNC_LEVEL		(BT848_ADC_RESERVED |	\
				 BT848_ADC_SYNC_T)	/* threshold ~75 mV */
#endif




/* debug utility for holding previous INT_STAT contents */
#define STATUS_SUM
static u_long	status_sum = 0;

/*
 * defines to make certain bit-fiddles understandable
 */
#define FIFO_ENABLED		BT848_DMA_CTL_FIFO_EN
#define RISC_ENABLED		BT848_DMA_CTL_RISC_EN
#define FIFO_RISC_ENABLED	(BT848_DMA_CTL_FIFO_EN | BT848_DMA_CTL_RISC_EN)
#define FIFO_RISC_DISABLED	0

#define ALL_INTS_DISABLED	0
#define ALL_INTS_CLEARED	0xffffffff
#define CAPTURE_OFF		0

#define BIT_SEVEN_HIGH		(1<<7)
#define BIT_EIGHT_HIGH		(1<<8)

#define I2C_BITS		(BT848_INT_RACK | BT848_INT_I2CDONE)
#define TDEC_BITS               (BT848_INT_FDSR | BT848_INT_FBUS)



static int		oformat_meteor_to_bt( u_long format );

static u_int		pixfmt_swap_flags( int pixfmt );

/*
 * bt848 RISC programming routines.
 */
#ifdef BT848_DUMP
static int	dump_bt848( bktr_ptr_t bktr );
#endif

static void	yuvpack_prog( bktr_ptr_t bktr, char i_flag, int cols,
			      int rows,  int interlace );
static void	yuv422_prog( bktr_ptr_t bktr, char i_flag, int cols,
			     int rows, int interlace );
static void	yuv12_prog( bktr_ptr_t bktr, char i_flag, int cols,
			     int rows, int interlace );
static void	rgb_prog( bktr_ptr_t bktr, char i_flag, int cols,
			  int rows, int interlace );
static void	rgb_vbi_prog( bktr_ptr_t bktr, char i_flag, int cols,
			  int rows, int interlace );
static void	build_dma_prog( bktr_ptr_t bktr, char i_flag );

static bool_t   getline(bktr_reg_t *, int);
static bool_t   notclipped(bktr_reg_t * , int , int);     
static bool_t   split(bktr_reg_t *, volatile uint32_t **, int, u_long, int, 
		      volatile u_char ** , int  );

static void	start_capture( bktr_ptr_t bktr, unsigned type );
static void	set_fps( bktr_ptr_t bktr, u_short fps );



/*
 * Remote Control Functions
 */
static void	remote_read(bktr_ptr_t bktr, struct bktr_remote *remote);


/*
 * ioctls common to both video & tuner.
 */
static int	common_ioctl( bktr_ptr_t bktr, ioctl_cmd_t cmd, caddr_t arg );


#if !defined(BKTR_USE_FREEBSD_SMBUS)
/*
 * i2c primitives for low level control of i2c bus. Added for MSP34xx control
 */
static void     i2c_start( bktr_ptr_t bktr);
static void     i2c_stop( bktr_ptr_t bktr);
static int      i2c_write_byte( bktr_ptr_t bktr, unsigned char data);
static int      i2c_read_byte( bktr_ptr_t bktr, unsigned char *data, int last );
#endif



/*
 * the common attach code, used by all OS versions.
 */
void 
common_bktr_attach( bktr_ptr_t bktr, int unit, u_long pci_id, u_int rev )
{
	vm_offset_t	buf = 0;
	int		need_to_allocate_memory = 1;
#ifdef BKTR_NEW_MSP34XX_DRIVER
	int 		err;
#endif

/***************************************/
/* *** OS Specific memory routines *** */
/***************************************/
#if defined(__NetBSD__) || defined(__OpenBSD__)
        /* allocate space for dma program */
        bktr->dma_prog = get_bktr_mem(bktr, &bktr->dm_prog,
				      DMA_PROG_ALLOC);
        bktr->odd_dma_prog = get_bktr_mem(bktr, &bktr->dm_oprog,
					  DMA_PROG_ALLOC);

	/* allocate space for the VBI buffer */
	bktr->vbidata  = get_bktr_mem(bktr, &bktr->dm_vbidata,
				      VBI_DATA_SIZE);
	bktr->vbibuffer = get_bktr_mem(bktr, &bktr->dm_vbibuffer,
				       VBI_BUFFER_SIZE);

        /* allocate space for pixel buffer */
        if ( BROOKTREE_ALLOC )
                buf = get_bktr_mem(bktr, &bktr->dm_mem, BROOKTREE_ALLOC);
        else
                buf = 0;
#endif

#if defined(__FreeBSD__) || defined(__bsdi__)

/* If this is a module, check if there is any currently saved contiguous memory */
#if defined(BKTR_FREEBSD_MODULE)
	if (bktr_has_stored_addresses(unit) == 1) {
		/* recover the addresses */
		bktr->dma_prog     = bktr_retrieve_address(unit, BKTR_MEM_DMA_PROG);
		bktr->odd_dma_prog = bktr_retrieve_address(unit, BKTR_MEM_ODD_DMA_PROG);
		bktr->vbidata      = bktr_retrieve_address(unit, BKTR_MEM_VBIDATA);
		bktr->vbibuffer    = bktr_retrieve_address(unit, BKTR_MEM_VBIBUFFER);
		buf                = bktr_retrieve_address(unit, BKTR_MEM_BUF);
		need_to_allocate_memory = 0;
	}
#endif

	if (need_to_allocate_memory == 1) {
		/* allocate space for dma program */
		bktr->dma_prog     = get_bktr_mem(unit, DMA_PROG_ALLOC);
		bktr->odd_dma_prog = get_bktr_mem(unit, DMA_PROG_ALLOC);

		/* allocte space for the VBI buffer */
		bktr->vbidata  = get_bktr_mem(unit, VBI_DATA_SIZE);
		bktr->vbibuffer = get_bktr_mem(unit, VBI_BUFFER_SIZE);

		/* allocate space for pixel buffer */
		if ( BROOKTREE_ALLOC )
			buf = get_bktr_mem(unit, BROOKTREE_ALLOC);
		else
			buf = 0;
	}
#endif	/* FreeBSD or BSDi */

#ifdef USE_VBIMUTEX
	mtx_init(&bktr->vbimutex, "bktr vbi lock", NULL, MTX_DEF);
#endif

/* If this is a module, save the current contiguous memory */
#if defined(BKTR_FREEBSD_MODULE)
bktr_store_address(unit, BKTR_MEM_DMA_PROG,     bktr->dma_prog);
bktr_store_address(unit, BKTR_MEM_ODD_DMA_PROG, bktr->odd_dma_prog);
bktr_store_address(unit, BKTR_MEM_VBIDATA,      bktr->vbidata);
bktr_store_address(unit, BKTR_MEM_VBIBUFFER,    bktr->vbibuffer);
bktr_store_address(unit, BKTR_MEM_BUF,          buf);
#endif


	if ( bootverbose ) {
		printf("%s: buffer size %d, addr %p\n",
			bktr_name(bktr), (int)BROOKTREE_ALLOC,
			(void *)(uintptr_t)vtophys(buf));
	}

	if ( buf != 0 ) {
		bktr->bigbuf = buf;
		bktr->alloc_pages = BROOKTREE_ALLOC_PAGES;
		bzero((caddr_t) bktr->bigbuf, BROOKTREE_ALLOC);
	} else {
		bktr->alloc_pages = 0;
	}
		

	bktr->flags = METEOR_INITALIZED | METEOR_AUTOMODE |
		      METEOR_DEV0 | METEOR_RGB16;
	bktr->dma_prog_loaded = FALSE;
	bktr->cols = 640;
	bktr->rows = 480;
	bktr->frames = 1;		/* one frame */
	bktr->format = METEOR_GEO_RGB16;
	bktr->pixfmt = oformat_meteor_to_bt( bktr->format );
	bktr->pixfmt_compat = TRUE;


	bktr->vbiinsert = 0;
	bktr->vbistart = 0;
	bktr->vbisize = 0;
	bktr->vbiflags = 0;

 
	/* using the pci device id and revision id */
	/* and determine the card type            */
	if (BKTR_PCI_VENDOR(pci_id) == PCI_VENDOR_BROOKTREE)
	{
		switch (BKTR_PCI_PRODUCT(pci_id)) {
		case PCI_PRODUCT_BROOKTREE_BT848:
			if (rev == 0x12)
				bktr->id = BROOKTREE_848A;
			else
				bktr->id = BROOKTREE_848;
			break;
		case PCI_PRODUCT_BROOKTREE_BT849:
			bktr->id = BROOKTREE_849A;
			break;
		case PCI_PRODUCT_BROOKTREE_BT878:
			bktr->id = BROOKTREE_878;
			break;
		case PCI_PRODUCT_BROOKTREE_BT879:
			bktr->id = BROOKTREE_879;
			break;
		}
	}

	bktr->clr_on_start = FALSE;

	/* defaults for the tuner section of the card */
	bktr->tflags = TUNER_INITALIZED;
	bktr->tuner.frequency = 0;
	bktr->tuner.channel = 0;
	bktr->tuner.chnlset = DEFAULT_CHNLSET;
	bktr->tuner.afc = 0;
	bktr->tuner.radio_mode = 0;
	bktr->audio_mux_select = 0;
	bktr->audio_mute_state = FALSE;
	bktr->bt848_card = -1;
	bktr->bt848_tuner = -1;
	bktr->reverse_mute = -1;
	bktr->slow_msp_audio = 0;
	bktr->msp_use_mono_source = 0;
        bktr->msp_source_selected = -1;
	bktr->audio_mux_present = 1;

#if defined(__FreeBSD__) 
#ifdef BKTR_NEW_MSP34XX_DRIVER
	/* get hint on short programming of the msp34xx, so we know */
	/* if the decision what thread to start should be overwritten */
	if ( (err = resource_int_value("bktr", unit, "mspsimple",
			&(bktr->mspsimple)) ) != 0 )
		bktr->mspsimple = -1;	/* fall back to default */
#endif
#endif

	probeCard( bktr, TRUE, unit );

	/* Initialise any MSP34xx or TDA98xx audio chips */
	init_audio_devices( bktr );

#ifdef BKTR_NEW_MSP34XX_DRIVER
	/* setup the kernel thread */
	err = msp_attach( bktr );
	if ( err != 0 ) /* error doing kernel thread stuff, disable msp3400c */
		bktr->card.msp3400c = 0;
#endif


}


/* Copy the vbi lines from 'vbidata' into the circular buffer, 'vbibuffer'.
 * The circular buffer holds 'n' fixed size data blocks. 
 * vbisize   is the number of bytes in the circular buffer 
 * vbiread   is the point we reading data out of the circular buffer 
 * vbiinsert is the point we insert data into the circular buffer 
 */
static void vbidecode(bktr_ptr_t bktr) {
        unsigned char *dest;
	unsigned int *seq_dest;

	/* Check if there is room in the buffer to insert the data. */
	if (bktr->vbisize + VBI_DATA_SIZE > VBI_BUFFER_SIZE) return;

	/* Copy the VBI data into the next free slot in the buffer. */
	/* 'dest' is the point in vbibuffer where we want to insert new data */
        dest = (unsigned char *)bktr->vbibuffer + bktr->vbiinsert;
        memcpy(dest, (unsigned char*)bktr->vbidata, VBI_DATA_SIZE);

	/* Write the VBI sequence number to the end of the vbi data */
	/* This is used by the AleVT teletext program */
	seq_dest = (unsigned int *)((unsigned char *)bktr->vbibuffer
			+ bktr->vbiinsert
			+ (VBI_DATA_SIZE - sizeof(bktr->vbi_sequence_number)));
	*seq_dest = bktr->vbi_sequence_number;

	/* And increase the VBI sequence number */
	/* This can wrap around */
	bktr->vbi_sequence_number++;


	/* Increment the vbiinsert pointer */
	/* This can wrap around */
	bktr->vbiinsert += VBI_DATA_SIZE;
	bktr->vbiinsert = (bktr->vbiinsert % VBI_BUFFER_SIZE);

	/* And increase the amount of vbi data in the buffer */
	bktr->vbisize = bktr->vbisize + VBI_DATA_SIZE;

}


/*
 * the common interrupt handler.
 * Returns a 0 or 1 depending on whether the interrupt has handled.
 * In the OS specific section, bktr_intr() is defined which calls this
 * common interrupt handler.
 */
int 
common_bktr_intr( void *arg )
{ 
	bktr_ptr_t		bktr;
	u_long			bktr_status;
	u_char			dstatus;
	u_long                  field;
	u_long                  w_field;
	u_long                  req_field;

	bktr = (bktr_ptr_t) arg;

	/*
	 * check to see if any interrupts are unmasked on this device.  If
	 * none are, then we likely got here by way of being on a PCI shared
	 * interrupt dispatch list.
	 */
	if (INL(bktr, BKTR_INT_MASK) == ALL_INTS_DISABLED)
	  	return 0;	/* bail out now, before we do something we
				   shouldn't */

	if (!(bktr->flags & METEOR_OPEN)) {
		OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_RISC_DISABLED);
		OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);
		/* return; ?? */
	}

	/* record and clear the INTerrupt status bits */
	bktr_status = INL(bktr, BKTR_INT_STAT);
	OUTL(bktr, BKTR_INT_STAT, bktr_status & ~I2C_BITS);	/* don't touch i2c */

	/* record and clear the device status register */
	dstatus = INB(bktr, BKTR_DSTATUS);
	OUTB(bktr, BKTR_DSTATUS, 0x00);

#if defined( STATUS_SUM )
	/* add any new device status or INTerrupt status bits */
	status_sum |= (bktr_status & ~(BT848_INT_RSV0|BT848_INT_RSV1));
	status_sum |= ((dstatus & (BT848_DSTATUS_COF|BT848_DSTATUS_LOF)) << 6);
#endif /* STATUS_SUM */
	/* printf( "%s: STATUS %x %x %x \n", bktr_name(bktr),
		dstatus, bktr_status, INL(bktr, BKTR_RISC_COUNT) );
	*/


	/* if risc was disabled re-start process again */
	/* if there was one of the following errors re-start again */
	if ( !(bktr_status & BT848_INT_RISC_EN) ||
	     ((bktr_status &(/* BT848_INT_FBUS   | */
			     /* BT848_INT_FTRGT  | */
			     /* BT848_INT_FDSR   | */
			      BT848_INT_PPERR  |
			      BT848_INT_RIPERR | BT848_INT_PABORT |
			      BT848_INT_OCERR  | BT848_INT_SCERR) ) != 0) 
		|| ((INB(bktr, BKTR_TDEC) == 0) && (bktr_status & TDEC_BITS)) ) { 

		u_short	tdec_save = INB(bktr, BKTR_TDEC);

		OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_RISC_DISABLED);
		OUTB(bktr, BKTR_CAP_CTL, CAPTURE_OFF);

		OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);

		/*  Reset temporal decimation counter  */
		OUTB(bktr, BKTR_TDEC, 0);
		OUTB(bktr, BKTR_TDEC, tdec_save);
		
		/*  Reset to no-fields captured state  */
		if (bktr->flags & (METEOR_CONTIN | METEOR_SYNCAP)) {
			switch(bktr->flags & METEOR_ONLY_FIELDS_MASK) {
			case METEOR_ONLY_ODD_FIELDS:
				bktr->flags |= METEOR_WANT_ODD;
				break;
			case METEOR_ONLY_EVEN_FIELDS:
				bktr->flags |= METEOR_WANT_EVEN;
				break;
			default:
				bktr->flags |= METEOR_WANT_MASK;
				break;
			}
		}

		OUTL(bktr, BKTR_RISC_STRT_ADD, vtophys(bktr->dma_prog));
		OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_ENABLED);
		OUTW(bktr, BKTR_GPIO_DMA_CTL, bktr->capcontrol);

		OUTL(bktr, BKTR_INT_MASK, BT848_INT_MYSTERYBIT |
				    BT848_INT_RISCI      |
				    BT848_INT_VSYNC      |
				    BT848_INT_FMTCHG);

		OUTB(bktr, BKTR_CAP_CTL, bktr->bktr_cap_ctl);
		return 1;
	}

	/* If this is not a RISC program interrupt, return */
	if (!(bktr_status & BT848_INT_RISCI))
		return 0;

/**
	printf( "%s: intr status %x %x %x\n", bktr_name(bktr),
		bktr_status, dstatus, INL(bktr, BKTR_RISC_COUNT) );
 */


	/*
	 * Disable future interrupts if a capture mode is not selected.
	 * This can happen when we are in the process of closing or 
	 * changing capture modes, otherwise it shouldn't happen.
	 */
	if (!(bktr->flags & METEOR_CAP_MASK))
		OUTB(bktr, BKTR_CAP_CTL, CAPTURE_OFF);


	/* Determine which field generated this interrupt */
	field = ( bktr_status & BT848_INT_FIELD ) ? EVEN_F : ODD_F;


	/*
	 * Process the VBI data if it is being captured. We do this once
	 * both Odd and Even VBI data is captured. Therefore we do this
	 * in the Even field interrupt handler.
	 */
	LOCK_VBI(bktr);
	if (  (bktr->vbiflags & VBI_CAPTURE)
	    &&(bktr->vbiflags & VBI_OPEN)
            &&(field==EVEN_F)) {
		/* Put VBI data into circular buffer */
               	vbidecode(bktr);

		/* If someone is blocked on reading from /dev/vbi, wake them */
		if (bktr->vbi_read_blocked) {
			bktr->vbi_read_blocked = FALSE;
          	     	wakeup(VBI_SLEEP);
		}

		/* If someone has a select() on /dev/vbi, inform them */
		if (SEL_WAITING(&bktr->vbi_select)) {
			selwakeuppri(&bktr->vbi_select, VBIPRI);
		}


	}
	UNLOCK_VBI(bktr);

	/*
	 *  Register the completed field
	 *    (For dual-field mode, require fields from the same frame)
	 */
	switch ( bktr->flags & METEOR_WANT_MASK ) {
		case METEOR_WANT_ODD  : w_field = ODD_F         ;  break;
		case METEOR_WANT_EVEN : w_field = EVEN_F        ;  break;
		default               : w_field = (ODD_F|EVEN_F);  break;
	}
	switch ( bktr->flags & METEOR_ONLY_FIELDS_MASK ) {
		case METEOR_ONLY_ODD_FIELDS  : req_field = ODD_F  ;  break;
		case METEOR_ONLY_EVEN_FIELDS : req_field = EVEN_F ;  break;
		default                      : req_field = (ODD_F|EVEN_F);  
			                       break;
	}

	if (( field == EVEN_F ) && ( w_field == EVEN_F ))
		bktr->flags &= ~METEOR_WANT_EVEN;
	else if (( field == ODD_F ) && ( req_field == ODD_F ) &&
		 ( w_field == ODD_F ))
		bktr->flags &= ~METEOR_WANT_ODD;
	else if (( field == ODD_F ) && ( req_field == (ODD_F|EVEN_F) ) &&
		 ( w_field == (ODD_F|EVEN_F) ))
		bktr->flags &= ~METEOR_WANT_ODD;
	else if (( field == ODD_F ) && ( req_field == (ODD_F|EVEN_F) ) &&
		 ( w_field == ODD_F )) {
		bktr->flags &= ~METEOR_WANT_ODD;
		bktr->flags |=  METEOR_WANT_EVEN;
	}
	else {
		/*  We're out of sync.  Start over.  */
		if (bktr->flags & (METEOR_CONTIN | METEOR_SYNCAP)) {
			switch(bktr->flags & METEOR_ONLY_FIELDS_MASK) {
			case METEOR_ONLY_ODD_FIELDS:
				bktr->flags |= METEOR_WANT_ODD;
				break;
			case METEOR_ONLY_EVEN_FIELDS:
				bktr->flags |= METEOR_WANT_EVEN;
				break;
			default:
				bktr->flags |= METEOR_WANT_MASK;
				break;
			}
		}
		return 1;
	}

	/*
	 * If we have a complete frame.
	 */
	if (!(bktr->flags & METEOR_WANT_MASK)) {
		bktr->frames_captured++;
		/*
		 * post the completion time. 
		 */
		if (bktr->flags & METEOR_WANT_TS) {
			struct timeval *ts;
			
			if ((u_int) bktr->alloc_pages * PAGE_SIZE
			   <= (bktr->frame_size + sizeof(struct timeval))) {
				ts =(struct timeval *)bktr->bigbuf +
				  bktr->frame_size;
				/* doesn't work in synch mode except
				 *  for first frame */
				/* XXX */
				microtime(ts);
			}
		}
	

		/*
		 * Wake up the user in single capture mode.
		 */
		if (bktr->flags & METEOR_SINGLE) {

			/* stop dma */
			OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);

			/* disable risc, leave fifo running */
			OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_ENABLED);
			wakeup(BKTR_SLEEP);
		}

		/*
		 * If the user requested to be notified via signal,
		 * let them know the frame is complete.
		 */

		if (bktr->proc != NULL) {
			PROC_LOCK(bktr->proc);
			kern_psignal( bktr->proc, bktr->signal);
			PROC_UNLOCK(bktr->proc);
		}

		/*
		 * Reset the want flags if in continuous or
		 * synchronous capture mode.
		 */
/*
* XXX NOTE (Luigi):
* currently we only support 3 capture modes: odd only, even only,
* odd+even interlaced (odd field first). A fourth mode (non interlaced,
* either even OR odd) could provide 60 (50 for PAL) pictures per
* second, but it would require this routine to toggle the desired frame
* each time, and one more different DMA program for the Bt848.
* As a consequence, this fourth mode is currently unsupported.
*/

		if (bktr->flags & (METEOR_CONTIN | METEOR_SYNCAP)) {
			switch(bktr->flags & METEOR_ONLY_FIELDS_MASK) {
			case METEOR_ONLY_ODD_FIELDS:
				bktr->flags |= METEOR_WANT_ODD;
				break;
			case METEOR_ONLY_EVEN_FIELDS:
				bktr->flags |= METEOR_WANT_EVEN;
				break;
			default:
				bktr->flags |= METEOR_WANT_MASK;
				break;
			}
		}
	}

	return 1;
}




/*
 * 
 */
extern int bt848_format; /* used to set the default format, PAL or NTSC */
int
video_open( bktr_ptr_t bktr )
{
	int frame_rate, video_format=0;

	if (bktr->flags & METEOR_OPEN)		/* device is busy */
		return( EBUSY );

	bktr->flags |= METEOR_OPEN;

#ifdef BT848_DUMP
	dump_bt848(bktr);
#endif

        bktr->clr_on_start = FALSE;

	OUTB(bktr, BKTR_DSTATUS, 0x00);			/* clear device status reg. */

	OUTB(bktr, BKTR_ADC, SYNC_LEVEL);

#if defined(BKTR_SYSTEM_DEFAULT) && BKTR_SYSTEM_DEFAULT == BROOKTREE_PAL
	video_format = 0;
#else
	video_format = 1;
#endif

	if (bt848_format == 0 ) 
	  video_format = 0;

	if (bt848_format == 1 ) 
	  video_format = 1;

	if (video_format == 1 ) {
	  OUTB(bktr, BKTR_IFORM, BT848_IFORM_F_NTSCM);
	  bktr->format_params = BT848_IFORM_F_NTSCM;

	} else {
	  OUTB(bktr, BKTR_IFORM, BT848_IFORM_F_PALBDGHI);
	  bktr->format_params = BT848_IFORM_F_PALBDGHI;

	}

	OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) | format_params[bktr->format_params].iform_xtsel);

	/* work around for new Hauppauge 878 cards */
	if ((bktr->card.card_id == CARD_HAUPPAUGE) &&
	    (bktr->id==BROOKTREE_878 || bktr->id==BROOKTREE_879) )
		OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) | BT848_IFORM_M_MUX3);
	else
		OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) | BT848_IFORM_M_MUX1);

	OUTB(bktr, BKTR_ADELAY, format_params[bktr->format_params].adelay);
	OUTB(bktr, BKTR_BDELAY, format_params[bktr->format_params].bdelay);
	frame_rate    = format_params[bktr->format_params].frame_rate;

	/* enable PLL mode using 28Mhz crystal for PAL/SECAM users */
	if (bktr->xtal_pll_mode == BT848_USE_PLL) {
		OUTB(bktr, BKTR_TGCTRL, 0);
		OUTB(bktr, BKTR_PLL_F_LO, 0xf9);
		OUTB(bktr, BKTR_PLL_F_HI, 0xdc);
		OUTB(bktr, BKTR_PLL_F_XCI, 0x8e);
	}

	bktr->flags = (bktr->flags & ~METEOR_DEV_MASK) | METEOR_DEV0;

	bktr->max_clip_node = 0;

	OUTB(bktr, BKTR_COLOR_CTL, BT848_COLOR_CTL_GAMMA | BT848_COLOR_CTL_RGB_DED);

	OUTB(bktr, BKTR_E_HSCALE_LO, 170);
	OUTB(bktr, BKTR_O_HSCALE_LO, 170);

	OUTB(bktr, BKTR_E_DELAY_LO, 0x72);
	OUTB(bktr, BKTR_O_DELAY_LO, 0x72);
	OUTB(bktr, BKTR_E_SCLOOP, 0);
	OUTB(bktr, BKTR_O_SCLOOP, 0);

	OUTB(bktr, BKTR_VBI_PACK_SIZE, 0);
	OUTB(bktr, BKTR_VBI_PACK_DEL, 0);

	bktr->fifo_errors = 0;
	bktr->dma_errors = 0;
	bktr->frames_captured = 0;
	bktr->even_fields_captured = 0;
	bktr->odd_fields_captured = 0;
	bktr->proc = NULL;
	set_fps(bktr, frame_rate);
	bktr->video.addr = 0;
	bktr->video.width = 0;
	bktr->video.banksize = 0;
	bktr->video.ramsize = 0;
	bktr->pixfmt_compat = TRUE;
	bktr->format = METEOR_GEO_RGB16;
	bktr->pixfmt = oformat_meteor_to_bt( bktr->format );

	bktr->capture_area_enabled = FALSE;

	OUTL(bktr, BKTR_INT_MASK, BT848_INT_MYSTERYBIT);	/* if you take this out triton
                                                   based motherboards will 
						   operate unreliably */
	return( 0 );
}

int
vbi_open( bktr_ptr_t bktr )
{

	LOCK_VBI(bktr);

	if (bktr->vbiflags & VBI_OPEN) {	/* device is busy */
		UNLOCK_VBI(bktr);
		return( EBUSY );
	}

	bktr->vbiflags |= VBI_OPEN;

	/* reset the VBI circular buffer pointers and clear the buffers */
	bktr->vbiinsert = 0;
	bktr->vbistart = 0;
	bktr->vbisize = 0;
	bktr->vbi_sequence_number = 0;
	bktr->vbi_read_blocked = FALSE;

	bzero((caddr_t) bktr->vbibuffer, VBI_BUFFER_SIZE);
	bzero((caddr_t) bktr->vbidata,  VBI_DATA_SIZE);

	UNLOCK_VBI(bktr);

	return( 0 );
}

/*
 * 
 */
int
tuner_open( bktr_ptr_t bktr )
{
	if ( !(bktr->tflags & TUNER_INITALIZED) )	/* device not found */
		return( ENXIO );	

	if ( bktr->tflags & TUNER_OPEN )		/* already open */
		return( 0 );

	bktr->tflags |= TUNER_OPEN;
	bktr->tuner.frequency = 0;
	bktr->tuner.channel = 0;
	bktr->tuner.chnlset = DEFAULT_CHNLSET;
	bktr->tuner.afc = 0;
	bktr->tuner.radio_mode = 0;

	/* enable drivers on the GPIO port that control the MUXes */
	OUTL(bktr, BKTR_GPIO_OUT_EN, INL(bktr, BKTR_GPIO_OUT_EN) | bktr->card.gpio_mux_bits);

	/* unmute the audio stream */
	set_audio( bktr, AUDIO_UNMUTE );

	/* Initialise any audio chips, eg MSP34xx or TDA98xx */
	init_audio_devices( bktr );
	
	return( 0 );
}




/*
 * 
 */
int
video_close( bktr_ptr_t bktr )
{
	bktr->flags &= ~(METEOR_OPEN     |
			 METEOR_SINGLE   |
			 METEOR_CAP_MASK |
			 METEOR_WANT_MASK);

	OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_RISC_DISABLED);
	OUTB(bktr, BKTR_CAP_CTL, CAPTURE_OFF);

	bktr->dma_prog_loaded = FALSE;
	OUTB(bktr, BKTR_TDEC, 0);
	OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);

/** FIXME: is 0xf magic, wouldn't 0x00 work ??? */
	OUTL(bktr, BKTR_SRESET, 0xf);
	OUTL(bktr, BKTR_INT_STAT, ALL_INTS_CLEARED);

	return( 0 );
}


/*
 * tuner close handle,
 *  place holder for tuner specific operations on a close.
 */
int
tuner_close( bktr_ptr_t bktr )
{
	bktr->tflags &= ~TUNER_OPEN;

	/* mute the audio by switching the mux */
	set_audio( bktr, AUDIO_MUTE );

	/* disable drivers on the GPIO port that control the MUXes */
	OUTL(bktr, BKTR_GPIO_OUT_EN, INL(bktr, BKTR_GPIO_OUT_EN) & ~bktr->card.gpio_mux_bits);

	return( 0 );
}

int
vbi_close( bktr_ptr_t bktr )
{

	LOCK_VBI(bktr);

	bktr->vbiflags &= ~VBI_OPEN;

	UNLOCK_VBI(bktr);

	return( 0 );
}

/*
 *
 */
int
video_read(bktr_ptr_t bktr, int unit, struct cdev *dev, struct uio *uio)
{
        int             status;
        int             count;


	if (bktr->bigbuf == 0)	/* no frame buffer allocated (ioctl failed) */
		return( ENOMEM );

	if (bktr->flags & METEOR_CAP_MASK)
		return( EIO );	/* already capturing */

        OUTB(bktr, BKTR_CAP_CTL, bktr->bktr_cap_ctl);


	count = bktr->rows * bktr->cols * 
		pixfmt_table[ bktr->pixfmt ].public.Bpp;

	if ((int) uio->uio_iov->iov_len < count)
		return( EINVAL );

	bktr->flags &= ~(METEOR_CAP_MASK | METEOR_WANT_MASK);

	/* capture one frame */
	start_capture(bktr, METEOR_SINGLE);
	/* wait for capture to complete */
	OUTL(bktr, BKTR_INT_STAT, ALL_INTS_CLEARED);
	OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_ENABLED);
	OUTW(bktr, BKTR_GPIO_DMA_CTL, bktr->capcontrol);
	OUTL(bktr, BKTR_INT_MASK, BT848_INT_MYSTERYBIT |
                            BT848_INT_RISCI      |
                            BT848_INT_VSYNC      |
                            BT848_INT_FMTCHG);


	status = tsleep(BKTR_SLEEP, BKTRPRI, "captur", 0);
	if (!status)		/* successful capture */
		status = uiomove((caddr_t)bktr->bigbuf, count, uio);
	else
		printf ("%s: read: tsleep error %d\n",
			bktr_name(bktr), status);

	bktr->flags &= ~(METEOR_SINGLE | METEOR_WANT_MASK);

	return( status );
}

/*
 * Read VBI data from the vbi circular buffer
 * The buffer holds vbi data blocks which are the same size
 * vbiinsert is the position we will insert the next item into the buffer
 * vbistart is the actual position in the buffer we want to read from
 * vbisize is the exact number of bytes in the buffer left to read 
 */
int
vbi_read(bktr_ptr_t bktr, struct uio *uio, int ioflag)
{
	int             readsize, readsize2, start;
	int             status;

	/*
	 * XXX - vbi_read() should be protected against being re-entered
	 * while it is unlocked for the uiomove.
	 */
	LOCK_VBI(bktr);

	while(bktr->vbisize == 0) {
		if (ioflag & FNDELAY) {
			status = EWOULDBLOCK;
			goto out;
		}

		bktr->vbi_read_blocked = TRUE;
#ifdef USE_VBIMUTEX
		if ((status = msleep(VBI_SLEEP, &bktr->vbimutex, VBIPRI, "vbi",
		    0))) {
			goto out;
		}
#else
		if ((status = tsleep(VBI_SLEEP, VBIPRI, "vbi", 0))) {
			goto out;
		}
#endif
	}

	/* Now we have some data to give to the user */
			
	/* We cannot read more bytes than there are in
	 * the circular buffer
	 */
	readsize = (int)uio->uio_iov->iov_len;

	if (readsize > bktr->vbisize) readsize = bktr->vbisize;

	/* Check if we can read this number of bytes without having
	 * to wrap around the circular buffer */
	if((bktr->vbistart + readsize) >= VBI_BUFFER_SIZE) {
		/* We need to wrap around */

		readsize2 = VBI_BUFFER_SIZE - bktr->vbistart;
		start =  bktr->vbistart;
		UNLOCK_VBI(bktr);
               	status = uiomove((caddr_t)bktr->vbibuffer + start, readsize2, uio);
		if (status == 0)
			status = uiomove((caddr_t)bktr->vbibuffer, (readsize - readsize2), uio);
	} else {
		UNLOCK_VBI(bktr);
		/* We do not need to wrap around */
		status = uiomove((caddr_t)bktr->vbibuffer + bktr->vbistart, readsize, uio);
	}

	LOCK_VBI(bktr);

	/* Update the number of bytes left to read */
	bktr->vbisize -= readsize;

	/* Update vbistart */
	bktr->vbistart += readsize;
	bktr->vbistart = bktr->vbistart % VBI_BUFFER_SIZE; /* wrap around if needed */

out:
	UNLOCK_VBI(bktr);

	return( status );

}



/*
 * video ioctls
 */
int
video_ioctl( bktr_ptr_t bktr, int unit, ioctl_cmd_t cmd, caddr_t arg, struct thread* td )
{
	volatile u_char		c_temp;
	unsigned int		temp;
	unsigned int		temp_iform;
	unsigned int		error;
	struct meteor_geomet	*geo;
	struct meteor_counts	*counts;
	struct meteor_video	*video;
	struct bktr_capture_area *cap_area;
	vm_offset_t		buf;
	int                     i;
	int			sig;
	char                    char_temp;

	switch ( cmd ) {

	case BT848SCLIP: /* set clip region */
	    bktr->max_clip_node = 0;
	    memcpy(&bktr->clip_list, arg, sizeof(bktr->clip_list));

	    for (i = 0; i < BT848_MAX_CLIP_NODE; i++) {
		if (bktr->clip_list[i].y_min ==  0 &&
		    bktr->clip_list[i].y_max == 0)
		    break;
	    }
	    bktr->max_clip_node = i;

	    /* make sure that the list contains a valid clip secquence */
	    /* the clip rectangles should be sorted by x then by y as the
               second order sort key */

	    /* clip rectangle list is terminated by y_min and y_max set to 0 */

	    /* to disable clipping set  y_min and y_max to 0 in the first
               clip rectangle . The first clip rectangle is clip_list[0].
             */

             
                
	    if (bktr->max_clip_node == 0 && 
		(bktr->clip_list[0].y_min != 0 && 
		 bktr->clip_list[0].y_max != 0)) {
		return EINVAL;
	    }

	    for (i = 0; i < BT848_MAX_CLIP_NODE - 1 ; i++) {
		if (bktr->clip_list[i].y_min == 0 &&
		    bktr->clip_list[i].y_max == 0) {
		    break;
		}
		if ( bktr->clip_list[i+1].y_min != 0 &&
		     bktr->clip_list[i+1].y_max != 0 &&
		     bktr->clip_list[i].x_min > bktr->clip_list[i+1].x_min ) {

		    bktr->max_clip_node = 0;
		    return (EINVAL);

		 }

		if (bktr->clip_list[i].x_min >= bktr->clip_list[i].x_max ||
		    bktr->clip_list[i].y_min >= bktr->clip_list[i].y_max ||
		    bktr->clip_list[i].x_min < 0 ||
		    bktr->clip_list[i].x_max < 0 || 
		    bktr->clip_list[i].y_min < 0 ||
		    bktr->clip_list[i].y_max < 0 ) {
		    bktr->max_clip_node = 0;
		    return (EINVAL);
		}
	    }

	    bktr->dma_prog_loaded = FALSE;

	    break;

	case METEORSTATUS:	/* get Bt848 status */
		c_temp = INB(bktr, BKTR_DSTATUS);
		temp = 0;
		if (!(c_temp & 0x40)) temp |= METEOR_STATUS_HCLK;
		if (!(c_temp & 0x10)) temp |= METEOR_STATUS_FIDT;
		*(u_short *)arg = temp;
		break;

	case BT848SFMT:		/* set input format */
		temp = *(unsigned long*)arg & BT848_IFORM_FORMAT;
		temp_iform = INB(bktr, BKTR_IFORM);
		temp_iform &= ~BT848_IFORM_FORMAT;
		temp_iform &= ~BT848_IFORM_XTSEL;
		OUTB(bktr, BKTR_IFORM, (temp_iform | temp | format_params[temp].iform_xtsel));
		switch( temp ) {
		case BT848_IFORM_F_AUTO:
			bktr->flags = (bktr->flags & ~METEOR_FORM_MASK) |
			METEOR_AUTOMODE;
			break;

		case BT848_IFORM_F_NTSCM:
		case BT848_IFORM_F_NTSCJ:
			bktr->flags = (bktr->flags & ~METEOR_FORM_MASK) |
				METEOR_NTSC;
			OUTB(bktr, BKTR_ADELAY, format_params[temp].adelay);
			OUTB(bktr, BKTR_BDELAY, format_params[temp].bdelay);
			bktr->format_params = temp;
			break;

		case BT848_IFORM_F_PALBDGHI:
		case BT848_IFORM_F_PALN:
		case BT848_IFORM_F_SECAM:
		case BT848_IFORM_F_RSVD:
		case BT848_IFORM_F_PALM:
			bktr->flags = (bktr->flags & ~METEOR_FORM_MASK) |
				METEOR_PAL;
			OUTB(bktr, BKTR_ADELAY, format_params[temp].adelay);
			OUTB(bktr, BKTR_BDELAY, format_params[temp].bdelay);
			bktr->format_params = temp;
			break;

		}
		bktr->dma_prog_loaded = FALSE;		
		break;

	case METEORSFMT:	/* set input format */
		temp_iform = INB(bktr, BKTR_IFORM);
		temp_iform &= ~BT848_IFORM_FORMAT;
		temp_iform &= ~BT848_IFORM_XTSEL;
		switch(*(unsigned long *)arg & METEOR_FORM_MASK ) {
		case 0:		/* default */
		case METEOR_FMT_NTSC:
			bktr->flags = (bktr->flags & ~METEOR_FORM_MASK) |
				METEOR_NTSC;
			OUTB(bktr, BKTR_IFORM, temp_iform | BT848_IFORM_F_NTSCM | 
		                         format_params[BT848_IFORM_F_NTSCM].iform_xtsel);
			OUTB(bktr, BKTR_ADELAY, format_params[BT848_IFORM_F_NTSCM].adelay);
			OUTB(bktr, BKTR_BDELAY, format_params[BT848_IFORM_F_NTSCM].bdelay);
			bktr->format_params = BT848_IFORM_F_NTSCM;
			break;

		case METEOR_FMT_PAL:
			bktr->flags = (bktr->flags & ~METEOR_FORM_MASK) |
				METEOR_PAL;
			OUTB(bktr, BKTR_IFORM, temp_iform | BT848_IFORM_F_PALBDGHI |
		                         format_params[BT848_IFORM_F_PALBDGHI].iform_xtsel);
			OUTB(bktr, BKTR_ADELAY, format_params[BT848_IFORM_F_PALBDGHI].adelay);
			OUTB(bktr, BKTR_BDELAY, format_params[BT848_IFORM_F_PALBDGHI].bdelay);
			bktr->format_params = BT848_IFORM_F_PALBDGHI;
			break;

		case METEOR_FMT_AUTOMODE:
			bktr->flags = (bktr->flags & ~METEOR_FORM_MASK) |
				METEOR_AUTOMODE;
			OUTB(bktr, BKTR_IFORM, temp_iform | BT848_IFORM_F_AUTO |
		                         format_params[BT848_IFORM_F_AUTO].iform_xtsel);
			break;

		default:
			return( EINVAL );
		}
		bktr->dma_prog_loaded = FALSE;		
		break;

	case METEORGFMT:	/* get input format */
		*(u_long *)arg = bktr->flags & METEOR_FORM_MASK;
		break;


	case BT848GFMT:		/* get input format */
	        *(u_long *)arg = INB(bktr, BKTR_IFORM) & BT848_IFORM_FORMAT;
		break;
 
	case METEORSCOUNT:	/* (re)set error counts */
		counts = (struct meteor_counts *) arg;
		bktr->fifo_errors = counts->fifo_errors;
		bktr->dma_errors = counts->dma_errors;
		bktr->frames_captured = counts->frames_captured;
		bktr->even_fields_captured = counts->even_fields_captured;
		bktr->odd_fields_captured = counts->odd_fields_captured;
		break;

	case METEORGCOUNT:	/* get error counts */
		counts = (struct meteor_counts *) arg;
		counts->fifo_errors = bktr->fifo_errors;
		counts->dma_errors = bktr->dma_errors;
		counts->frames_captured = bktr->frames_captured;
		counts->even_fields_captured = bktr->even_fields_captured;
		counts->odd_fields_captured = bktr->odd_fields_captured;
		break;

	case METEORGVIDEO:
		video = (struct meteor_video *)arg;
		video->addr = bktr->video.addr;
		video->width = bktr->video.width;
		video->banksize = bktr->video.banksize;
		video->ramsize = bktr->video.ramsize;
		break;

	case METEORSVIDEO:
		video = (struct meteor_video *)arg;
		bktr->video.addr = video->addr;
		bktr->video.width = video->width;
		bktr->video.banksize = video->banksize;
		bktr->video.ramsize = video->ramsize;
		break;

	case METEORSFPS:
		set_fps(bktr, *(u_short *)arg);
		break;

	case METEORGFPS:
		*(u_short *)arg = bktr->fps;
		break;

	case METEORSHUE:	/* set hue */
		OUTB(bktr, BKTR_HUE, (*(u_char *) arg) & 0xff);
		break;

	case METEORGHUE:	/* get hue */
		*(u_char *)arg = INB(bktr, BKTR_HUE);
		break;

	case METEORSBRIG:	/* set brightness */
	        char_temp =    ( *(u_char *)arg & 0xff) - 128;
		OUTB(bktr, BKTR_BRIGHT, char_temp);
		
		break;

	case METEORGBRIG:	/* get brightness */
		*(u_char *)arg = INB(bktr, BKTR_BRIGHT) + 128;
		break;

	case METEORSCSAT:	/* set chroma saturation */
		temp = (int)*(u_char *)arg;

		OUTB(bktr, BKTR_SAT_U_LO, (temp << 1) & 0xff);
		OUTB(bktr, BKTR_SAT_V_LO, (temp << 1) & 0xff);
		OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL)
		                     & ~(BT848_E_CONTROL_SAT_U_MSB
					 | BT848_E_CONTROL_SAT_V_MSB));
		OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL)
		                     & ~(BT848_O_CONTROL_SAT_U_MSB |
					 BT848_O_CONTROL_SAT_V_MSB));

		if ( temp & BIT_SEVEN_HIGH ) {
		        OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL)
			                     | (BT848_E_CONTROL_SAT_U_MSB
						| BT848_E_CONTROL_SAT_V_MSB));
			OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL)
			                     | (BT848_O_CONTROL_SAT_U_MSB
						| BT848_O_CONTROL_SAT_V_MSB));
		}
		break;

	case METEORGCSAT:	/* get chroma saturation */
		temp = (INB(bktr, BKTR_SAT_V_LO) >> 1) & 0xff;
		if ( INB(bktr, BKTR_E_CONTROL) & BT848_E_CONTROL_SAT_V_MSB )
			temp |= BIT_SEVEN_HIGH;
		*(u_char *)arg = (u_char)temp;
		break;

	case METEORSCONT:	/* set contrast */
		temp = (int)*(u_char *)arg & 0xff;
		temp <<= 1;
		OUTB(bktr, BKTR_CONTRAST_LO, temp & 0xff);
		OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL) & ~BT848_E_CONTROL_CON_MSB);
		OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL) & ~BT848_O_CONTROL_CON_MSB);
		OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL) |
			(((temp & 0x100) >> 6 ) & BT848_E_CONTROL_CON_MSB));
		OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL) |
			(((temp & 0x100) >> 6 ) & BT848_O_CONTROL_CON_MSB));
		break;

	case METEORGCONT:	/* get contrast */
		temp = (int)INB(bktr, BKTR_CONTRAST_LO) & 0xff;
		temp |= ((int)INB(bktr, BKTR_O_CONTROL) & 0x04) << 6;
		*(u_char *)arg = (u_char)((temp >> 1) & 0xff);
		break;

	case BT848SCBUF:	/* set Clear-Buffer-on-start flag */
		bktr->clr_on_start = (*(int *)arg != 0);
		break;

	case BT848GCBUF:	/* get Clear-Buffer-on-start flag */
		*(int *)arg = (int) bktr->clr_on_start;
		break;

	case METEORSSIGNAL:
		sig = *(int *)arg;
		/* Historically, applications used METEOR_SIG_MODE_MASK
		 * to reset signal delivery.
		 */
		if (sig == METEOR_SIG_MODE_MASK)
			sig = 0;
		if (sig < 0 || sig > _SIG_MAXSIG)
			return (EINVAL);
		bktr->signal = sig;
		bktr->proc = sig ? td->td_proc : NULL;
		break;

	case METEORGSIGNAL:
		*(int *)arg = bktr->signal;
		break;

	case METEORCAPTUR:
		temp = bktr->flags;
		switch (*(int *) arg) {
		case METEOR_CAP_SINGLE:

			if (bktr->bigbuf==0)	/* no frame buffer allocated */
				return( ENOMEM );
			/* already capturing */
			if (temp & METEOR_CAP_MASK)
				return( EIO );



			start_capture(bktr, METEOR_SINGLE);

			/* wait for capture to complete */
			OUTL(bktr, BKTR_INT_STAT, ALL_INTS_CLEARED);
			OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_ENABLED);
			OUTW(bktr, BKTR_GPIO_DMA_CTL, bktr->capcontrol);

			OUTL(bktr, BKTR_INT_MASK, BT848_INT_MYSTERYBIT |
			     		    BT848_INT_RISCI      |
					    BT848_INT_VSYNC      |
					    BT848_INT_FMTCHG);

			OUTB(bktr, BKTR_CAP_CTL, bktr->bktr_cap_ctl);
			error = tsleep(BKTR_SLEEP, BKTRPRI, "captur", hz);
			if (error && (error != ERESTART)) {
				/*  Here if we didn't get complete frame  */
#ifdef DIAGNOSTIC
				printf( "%s: ioctl: tsleep error %d %x\n",
					bktr_name(bktr), error,
					INL(bktr, BKTR_RISC_COUNT));
#endif

				/* stop dma */
				OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);

				/* disable risc, leave fifo running */
				OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_ENABLED);
			}

			bktr->flags &= ~(METEOR_SINGLE|METEOR_WANT_MASK);
			/* FIXME: should we set bt848->int_stat ??? */
			break;

		case METEOR_CAP_CONTINOUS:
			if (bktr->bigbuf==0)	/* no frame buffer allocated */
				return( ENOMEM );
			/* already capturing */
			if (temp & METEOR_CAP_MASK)
			    return( EIO );


			start_capture(bktr, METEOR_CONTIN);

			/* Clear the interrypt status register */
			OUTL(bktr, BKTR_INT_STAT, INL(bktr, BKTR_INT_STAT));

			OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_ENABLED);
			OUTW(bktr, BKTR_GPIO_DMA_CTL, bktr->capcontrol);
			OUTB(bktr, BKTR_CAP_CTL, bktr->bktr_cap_ctl);

			OUTL(bktr, BKTR_INT_MASK, BT848_INT_MYSTERYBIT |
					    BT848_INT_RISCI      |
			                    BT848_INT_VSYNC      |
					    BT848_INT_FMTCHG);
#ifdef BT848_DUMP
			dump_bt848(bktr);
#endif
			break;
		
		case METEOR_CAP_STOP_CONT:
			if (bktr->flags & METEOR_CONTIN) {
				/* turn off capture */
				OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_RISC_DISABLED);
				OUTB(bktr, BKTR_CAP_CTL, CAPTURE_OFF);
				OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);
				bktr->flags &=
					~(METEOR_CONTIN | METEOR_WANT_MASK);

			}
		}
		break;

	case METEORSETGEO:
		/* can't change parameters while capturing */
		if (bktr->flags & METEOR_CAP_MASK)
			return( EBUSY );


		geo = (struct meteor_geomet *) arg;

		error = 0;
		/* Either even or odd, if even & odd, then these a zero */
		if ((geo->oformat & METEOR_GEO_ODD_ONLY) &&
			(geo->oformat & METEOR_GEO_EVEN_ONLY)) {
			printf( "%s: ioctl: Geometry odd or even only.\n",
				bktr_name(bktr));
			return( EINVAL );
		}

		/* set/clear even/odd flags */
		if (geo->oformat & METEOR_GEO_ODD_ONLY)
			bktr->flags |= METEOR_ONLY_ODD_FIELDS;
		else
			bktr->flags &= ~METEOR_ONLY_ODD_FIELDS;
		if (geo->oformat & METEOR_GEO_EVEN_ONLY)
			bktr->flags |= METEOR_ONLY_EVEN_FIELDS;
		else
			bktr->flags &= ~METEOR_ONLY_EVEN_FIELDS;

		if (geo->columns <= 0) {
			printf(
			"%s: ioctl: %d: columns must be greater than zero.\n",
				bktr_name(bktr), geo->columns);
			error = EINVAL;
		}
		else if ((geo->columns & 0x3fe) != geo->columns) {
			printf(
			"%s: ioctl: %d: columns too large or not even.\n",
				bktr_name(bktr), geo->columns);
			error = EINVAL;
		}

		if (geo->rows <= 0) {
			printf(
			"%s: ioctl: %d: rows must be greater than zero.\n",
				bktr_name(bktr), geo->rows);
			error = EINVAL;
		}
		else if (((geo->rows & 0x7fe) != geo->rows) ||
			((geo->oformat & METEOR_GEO_FIELD_MASK) &&
				((geo->rows & 0x3fe) != geo->rows)) ) {
			printf(
			"%s: ioctl: %d: rows too large or not even.\n",
				bktr_name(bktr), geo->rows);
			error = EINVAL;
		}

		if (geo->frames > 32) {
			printf("%s: ioctl: too many frames.\n",
			       bktr_name(bktr));

			error = EINVAL;
		}

		if (error)
			return( error );

		bktr->dma_prog_loaded = FALSE;
		OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_RISC_DISABLED);

		OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);

		if ((temp=(geo->rows * geo->columns * geo->frames * 2))) {
			if (geo->oformat & METEOR_GEO_RGB24) temp = temp * 2;

			/* meteor_mem structure for SYNC Capture */
			if (geo->frames > 1) temp += PAGE_SIZE;

			temp = btoc(temp);
			if ((int) temp > bktr->alloc_pages
			    && bktr->video.addr == 0) {

/*****************************/
/* *** OS Dependent code *** */
/*****************************/
#if defined(__NetBSD__) || defined(__OpenBSD__)
                                bus_dmamap_t dmamap;

                                buf = get_bktr_mem(bktr, &dmamap,
                                                   temp * PAGE_SIZE);
                                if (buf != 0) {
                                        free_bktr_mem(bktr, bktr->dm_mem,
                                                      bktr->bigbuf);
                                        bktr->dm_mem = dmamap;

#else
                                buf = get_bktr_mem(unit, temp*PAGE_SIZE);
                                if (buf != 0) {
					contigfree(
					  (void *)(uintptr_t)bktr->bigbuf,
                                          (bktr->alloc_pages * PAGE_SIZE),
					  M_DEVBUF);
#endif                                          

					bktr->bigbuf = buf;
					bktr->alloc_pages = temp;
					if (bootverbose)
						printf("%s: ioctl: Allocating %d bytes\n",
							bktr_name(bktr), (int)(temp*PAGE_SIZE));
				}
				else
					error = ENOMEM;
			}
		}

		if (error)
			return error;

		bktr->rows = geo->rows;
		bktr->cols = geo->columns;
		bktr->frames = geo->frames;

		/*  Pixel format (if in meteor pixfmt compatibility mode)  */
		if ( bktr->pixfmt_compat ) {
			bktr->format = METEOR_GEO_YUV_422;
			switch (geo->oformat & METEOR_GEO_OUTPUT_MASK) {
			case 0:			/* default */
			case METEOR_GEO_RGB16:
				    bktr->format = METEOR_GEO_RGB16;
				    break;
			case METEOR_GEO_RGB24:
				    bktr->format = METEOR_GEO_RGB24;
				    break;
			case METEOR_GEO_YUV_422:
				    bktr->format = METEOR_GEO_YUV_422;
                                    if (geo->oformat & METEOR_GEO_YUV_12) 
					bktr->format = METEOR_GEO_YUV_12;
				    break;
			case METEOR_GEO_YUV_PACKED:
				    bktr->format = METEOR_GEO_YUV_PACKED;
				    break;
			}
			bktr->pixfmt = oformat_meteor_to_bt( bktr->format );
		}

		if (bktr->flags & METEOR_CAP_MASK) {

			if (bktr->flags & (METEOR_CONTIN|METEOR_SYNCAP)) {
				switch(bktr->flags & METEOR_ONLY_FIELDS_MASK) {
				case METEOR_ONLY_ODD_FIELDS:
					bktr->flags |= METEOR_WANT_ODD;
					break;
				case METEOR_ONLY_EVEN_FIELDS:
					bktr->flags |= METEOR_WANT_EVEN;
					break;
				default:
					bktr->flags |= METEOR_WANT_MASK;
					break;
				}

				start_capture(bktr, METEOR_CONTIN);
				OUTL(bktr, BKTR_INT_STAT, INL(bktr, BKTR_INT_STAT));
				OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_ENABLED);
				OUTW(bktr, BKTR_GPIO_DMA_CTL, bktr->capcontrol);
				OUTL(bktr, BKTR_INT_MASK, BT848_INT_MYSTERYBIT |
						    BT848_INT_VSYNC      |
						    BT848_INT_FMTCHG);
			}
		}
		break;
	/* end of METEORSETGEO */

	/* FIXME. The Capture Area currently has the following restrictions:
	GENERAL
	 y_offset may need to be even in interlaced modes
	RGB24 - Interlaced mode
	 x_size must be greater than or equal to 1.666*METEORSETGEO width (cols)
	 y_size must be greater than or equal to METEORSETGEO height (rows)
	RGB24 - Even Only (or Odd Only) mode
	 x_size must be greater than or equal to 1.666*METEORSETGEO width (cols)
	 y_size must be greater than or equal to 2*METEORSETGEO height (rows)
	YUV12 - Interlaced mode
	 x_size must be greater than or equal to METEORSETGEO width (cols)
	 y_size must be greater than or equal to METEORSETGEO height (rows)
	YUV12 - Even Only (or Odd Only) mode
	 x_size must be greater than or equal to METEORSETGEO width (cols)
	 y_size must be greater than or equal to 2*METEORSETGEO height (rows)
	*/

	case BT848_SCAPAREA: /* set capture area of each video frame */
		/* can't change parameters while capturing */
		if (bktr->flags & METEOR_CAP_MASK)
			return( EBUSY );

		cap_area = (struct bktr_capture_area *) arg;
		bktr->capture_area_x_offset = cap_area->x_offset;
		bktr->capture_area_y_offset = cap_area->y_offset;
		bktr->capture_area_x_size   = cap_area->x_size;
		bktr->capture_area_y_size   = cap_area->y_size;
		bktr->capture_area_enabled  = TRUE;
 
		bktr->dma_prog_loaded = FALSE;
		break;
   
	case BT848_GCAPAREA: /* get capture area of each video frame */
		cap_area = (struct bktr_capture_area *) arg;
		if (bktr->capture_area_enabled == FALSE) {
			cap_area->x_offset = 0;
			cap_area->y_offset = 0;
			cap_area->x_size   = format_params[
				bktr->format_params].scaled_hactive;
			cap_area->y_size   = format_params[
				bktr->format_params].vactive;
		} else {
			cap_area->x_offset = bktr->capture_area_x_offset;
			cap_area->y_offset = bktr->capture_area_y_offset;
			cap_area->x_size   = bktr->capture_area_x_size;
			cap_area->y_size   = bktr->capture_area_y_size;
		}
		break;

	default:
		return common_ioctl( bktr, cmd, arg );
	}

	return( 0 );
}

/*
 * tuner ioctls
 */
int
tuner_ioctl( bktr_ptr_t bktr, int unit, ioctl_cmd_t cmd, caddr_t arg, struct thread* td )
{
	int		tmp_int;
	int		temp, temp1;
	int		offset;
	int		count;
	u_char		*buf;
	u_long          par;
	u_char          write;
	int             i2c_addr;
	int             i2c_port;
	u_long          data;

	switch ( cmd ) {

	case REMOTE_GETKEY:
		/* Read the last key pressed by the Remote Control */
		if (bktr->remote_control == 0) return (EINVAL);
		remote_read(bktr, (struct bktr_remote *)arg);
		break;

#if defined( TUNER_AFC )
	case TVTUNER_SETAFC:
		bktr->tuner.afc = (*(int *)arg != 0);
		break;

	case TVTUNER_GETAFC:
		*(int *)arg = bktr->tuner.afc;
		/* XXX Perhaps use another bit to indicate AFC success? */
		break;
#endif /* TUNER_AFC */

	case TVTUNER_SETCHNL:
		temp_mute( bktr, TRUE );
		temp = tv_channel( bktr, (int)*(unsigned long *)arg );
		if ( temp < 0 ) {
			temp_mute( bktr, FALSE );
			return( EINVAL );
		}
		*(unsigned long *)arg = temp;

		/* after every channel change, we must restart the MSP34xx */
		/* audio chip to reselect NICAM STEREO or MONO audio */
		if ( bktr->card.msp3400c )
		  msp_autodetect( bktr );

		/* after every channel change, we must restart the DPL35xx */
		if ( bktr->card.dpl3518a )
		  dpl_autodetect( bktr );

		temp_mute( bktr, FALSE );
		break;

	case TVTUNER_GETCHNL:
		*(unsigned long *)arg = bktr->tuner.channel;
		break;

	case TVTUNER_SETTYPE:
		temp = *(unsigned long *)arg;
		if ( (temp < CHNLSET_MIN) || (temp > CHNLSET_MAX) )
			return( EINVAL );
		bktr->tuner.chnlset = temp;
		break;

	case TVTUNER_GETTYPE:
		*(unsigned long *)arg = bktr->tuner.chnlset;
		break;

	case TVTUNER_GETSTATUS:
		temp = get_tuner_status( bktr );
		*(unsigned long *)arg = temp & 0xff;
		break;

	case TVTUNER_SETFREQ:
		temp_mute( bktr, TRUE );
		temp = tv_freq( bktr, (int)*(unsigned long *)arg, TV_FREQUENCY);
		temp_mute( bktr, FALSE );
		if ( temp < 0 ) {
			temp_mute( bktr, FALSE );
			return( EINVAL );
		}
		*(unsigned long *)arg = temp;

		/* after every channel change, we must restart the MSP34xx */
		/* audio chip to reselect NICAM STEREO or MONO audio */
		if ( bktr->card.msp3400c )
		  msp_autodetect( bktr );

		/* after every channel change, we must restart the DPL35xx */
		if ( bktr->card.dpl3518a )
		  dpl_autodetect( bktr );

		temp_mute( bktr, FALSE );
		break;

	case TVTUNER_GETFREQ:
		*(unsigned long *)arg = bktr->tuner.frequency;
		break;

	case TVTUNER_GETCHNLSET:
		return tuner_getchnlset((struct bktr_chnlset *)arg);

	case BT848_SAUDIO:	/* set audio channel */
		if ( set_audio( bktr, *(int*)arg ) < 0 )
			return( EIO );
		break;

	/* hue is a 2's compliment number, -90' to +89.3' in 0.7' steps */
	case BT848_SHUE:	/* set hue */
		OUTB(bktr, BKTR_HUE, (u_char)(*(int*)arg & 0xff));
		break;

	case BT848_GHUE:	/* get hue */
		*(int*)arg = (signed char)(INB(bktr, BKTR_HUE) & 0xff);
		break;

	/* brightness is a 2's compliment #, -50 to +%49.6% in 0.39% steps */
	case BT848_SBRIG:	/* set brightness */
		OUTB(bktr, BKTR_BRIGHT, (u_char)(*(int *)arg & 0xff));
		break;

	case BT848_GBRIG:	/* get brightness */
		*(int *)arg = (signed char)(INB(bktr, BKTR_BRIGHT) & 0xff);
		break;

	/*  */
	case BT848_SCSAT:	/* set chroma saturation */
		tmp_int = *(int*)arg;

		temp = INB(bktr, BKTR_E_CONTROL);
		temp1 = INB(bktr, BKTR_O_CONTROL);
		if ( tmp_int & BIT_EIGHT_HIGH ) {
			temp |= (BT848_E_CONTROL_SAT_U_MSB |
				 BT848_E_CONTROL_SAT_V_MSB);
			temp1 |= (BT848_O_CONTROL_SAT_U_MSB |
				  BT848_O_CONTROL_SAT_V_MSB);
		}
		else {
			temp &= ~(BT848_E_CONTROL_SAT_U_MSB |
				  BT848_E_CONTROL_SAT_V_MSB);
			temp1 &= ~(BT848_O_CONTROL_SAT_U_MSB |
				   BT848_O_CONTROL_SAT_V_MSB);
		}

		OUTB(bktr, BKTR_SAT_U_LO, (u_char)(tmp_int & 0xff));
		OUTB(bktr, BKTR_SAT_V_LO, (u_char)(tmp_int & 0xff));
		OUTB(bktr, BKTR_E_CONTROL, temp);
		OUTB(bktr, BKTR_O_CONTROL, temp1);
		break;

	case BT848_GCSAT:	/* get chroma saturation */
		tmp_int = (int)(INB(bktr, BKTR_SAT_V_LO) & 0xff);
		if ( INB(bktr, BKTR_E_CONTROL) & BT848_E_CONTROL_SAT_V_MSB )
			tmp_int |= BIT_EIGHT_HIGH;
		*(int*)arg = tmp_int;
		break;

	/*  */
	case BT848_SVSAT:	/* set chroma V saturation */
		tmp_int = *(int*)arg;

		temp = INB(bktr, BKTR_E_CONTROL);
		temp1 = INB(bktr, BKTR_O_CONTROL);
		if ( tmp_int & BIT_EIGHT_HIGH) {
			temp |= BT848_E_CONTROL_SAT_V_MSB;
			temp1 |= BT848_O_CONTROL_SAT_V_MSB;
		}
		else {
			temp &= ~BT848_E_CONTROL_SAT_V_MSB;
			temp1 &= ~BT848_O_CONTROL_SAT_V_MSB;
		}

		OUTB(bktr, BKTR_SAT_V_LO, (u_char)(tmp_int & 0xff));
		OUTB(bktr, BKTR_E_CONTROL, temp);
		OUTB(bktr, BKTR_O_CONTROL, temp1);
		break;

	case BT848_GVSAT:	/* get chroma V saturation */
		tmp_int = (int)INB(bktr, BKTR_SAT_V_LO) & 0xff;
		if ( INB(bktr, BKTR_E_CONTROL) & BT848_E_CONTROL_SAT_V_MSB )
			tmp_int |= BIT_EIGHT_HIGH;
		*(int*)arg = tmp_int;
		break;

	/*  */
	case BT848_SUSAT:	/* set chroma U saturation */
		tmp_int = *(int*)arg;

		temp = INB(bktr, BKTR_E_CONTROL);
		temp1 = INB(bktr, BKTR_O_CONTROL);
		if ( tmp_int & BIT_EIGHT_HIGH ) {
			temp |= BT848_E_CONTROL_SAT_U_MSB;
			temp1 |= BT848_O_CONTROL_SAT_U_MSB;
		}
		else {
			temp &= ~BT848_E_CONTROL_SAT_U_MSB;
			temp1 &= ~BT848_O_CONTROL_SAT_U_MSB;
		}

		OUTB(bktr, BKTR_SAT_U_LO, (u_char)(tmp_int & 0xff));
		OUTB(bktr, BKTR_E_CONTROL, temp);
		OUTB(bktr, BKTR_O_CONTROL, temp1);
		break;

	case BT848_GUSAT:	/* get chroma U saturation */
		tmp_int = (int)INB(bktr, BKTR_SAT_U_LO) & 0xff;
		if ( INB(bktr, BKTR_E_CONTROL) & BT848_E_CONTROL_SAT_U_MSB )
			tmp_int |= BIT_EIGHT_HIGH;
		*(int*)arg = tmp_int;
		break;

/* lr 970528 luma notch etc - 3 high bits of e_control/o_control */

	case BT848_SLNOTCH:	/* set luma notch */
		tmp_int = (*(int *)arg & 0x7) << 5 ;
		OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL) & ~0xe0);
		OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL) & ~0xe0);
		OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL) | tmp_int);
		OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL) | tmp_int);
		break;

	case BT848_GLNOTCH:	/* get luma notch */
		*(int *)arg = (int) ( (INB(bktr, BKTR_E_CONTROL) & 0xe0) >> 5) ;
		break;


	/*  */
	case BT848_SCONT:	/* set contrast */
		tmp_int = *(int*)arg;

		temp = INB(bktr, BKTR_E_CONTROL);
		temp1 = INB(bktr, BKTR_O_CONTROL);
		if ( tmp_int & BIT_EIGHT_HIGH ) {
			temp |= BT848_E_CONTROL_CON_MSB;
			temp1 |= BT848_O_CONTROL_CON_MSB;
		}
		else {
			temp &= ~BT848_E_CONTROL_CON_MSB;
			temp1 &= ~BT848_O_CONTROL_CON_MSB;
		}

		OUTB(bktr, BKTR_CONTRAST_LO, (u_char)(tmp_int & 0xff));
		OUTB(bktr, BKTR_E_CONTROL, temp);
		OUTB(bktr, BKTR_O_CONTROL, temp1);
		break;

	case BT848_GCONT:	/* get contrast */
		tmp_int = (int)INB(bktr, BKTR_CONTRAST_LO) & 0xff;
		if ( INB(bktr, BKTR_E_CONTROL) & BT848_E_CONTROL_CON_MSB )
			tmp_int |= BIT_EIGHT_HIGH;
		*(int*)arg = tmp_int;
		break;

		/*  FIXME:  SCBARS and CCBARS require a valid int *        */
		/*    argument to succeed, but its not used; consider      */
		/*    using the arg to store the on/off state so           */
		/*    there's only one ioctl() needed to turn cbars on/off */
	case BT848_SCBARS:	/* set colorbar output */
		OUTB(bktr, BKTR_COLOR_CTL, INB(bktr, BKTR_COLOR_CTL) | BT848_COLOR_CTL_COLOR_BARS);
		break;

	case BT848_CCBARS:	/* clear colorbar output */
		OUTB(bktr, BKTR_COLOR_CTL, INB(bktr, BKTR_COLOR_CTL) & ~(BT848_COLOR_CTL_COLOR_BARS));
		break;

	case BT848_GAUDIO:	/* get audio channel */
		temp = bktr->audio_mux_select;
		if ( bktr->audio_mute_state == TRUE )
			temp |= AUDIO_MUTE;
		*(int*)arg = temp;
		break;

	case BT848_SBTSC:	/* set audio channel */
		if ( set_BTSC( bktr, *(int*)arg ) < 0 )
			return( EIO );
		break;

	case BT848_WEEPROM:	/* write eeprom */
		offset = (((struct eeProm *)arg)->offset);
		count = (((struct eeProm *)arg)->count);
		buf = &(((struct eeProm *)arg)->bytes[ 0 ]);
		if ( writeEEProm( bktr, offset, count, buf ) < 0 )
			return( EIO );
		break;

	case BT848_REEPROM:	/* read eeprom */
		offset = (((struct eeProm *)arg)->offset);
		count = (((struct eeProm *)arg)->count);
		buf = &(((struct eeProm *)arg)->bytes[ 0 ]);
		if ( readEEProm( bktr, offset, count, buf ) < 0 )
			return( EIO );
		break;

	case BT848_SIGNATURE:
		offset = (((struct eeProm *)arg)->offset);
		count = (((struct eeProm *)arg)->count);
		buf = &(((struct eeProm *)arg)->bytes[ 0 ]);
		if ( signCard( bktr, offset, count, buf ) < 0 )
			return( EIO );
		break;

        /* Ioctl's for direct gpio access */
#ifdef BKTR_GPIO_ACCESS
        case BT848_GPIO_GET_EN:
                *(int*)arg = INL(bktr, BKTR_GPIO_OUT_EN);
                break;

        case BT848_GPIO_SET_EN:
                OUTL(bktr, BKTR_GPIO_OUT_EN, *(int*)arg);
                break;

        case BT848_GPIO_GET_DATA:
                *(int*)arg = INL(bktr, BKTR_GPIO_DATA);
                break;

        case BT848_GPIO_SET_DATA:
                OUTL(bktr, BKTR_GPIO_DATA, *(int*)arg);
                break;
#endif /* BKTR_GPIO_ACCESS */

	/* Ioctl's for running the tuner device in radio mode		*/

	case RADIO_GETMODE:
            *(unsigned char *)arg = bktr->tuner.radio_mode;
	    break;

	case RADIO_SETMODE:
            bktr->tuner.radio_mode = *(unsigned char *)arg;
            break;

 	case RADIO_GETFREQ:
            *(unsigned long *)arg = bktr->tuner.frequency;
            break;

	case RADIO_SETFREQ:
	    /* The argument to this ioctl is NOT freq*16. It is
	    ** freq*100.
	    */

            temp=(int)*(unsigned long *)arg;

#ifdef BKTR_RADIO_DEBUG
	    printf("%s: arg=%d temp=%d\n", bktr_name(bktr),
		   (int)*(unsigned long *)arg, temp);
#endif

#ifndef BKTR_RADIO_NOFREQCHECK
	    /* According to the spec. sheet the band: 87.5MHz-108MHz	*/
	    /* is supported.						*/
	    if(temp<8750 || temp>10800) {
	      printf("%s: Radio frequency out of range\n", bktr_name(bktr));
	      return(EINVAL);
	      }
#endif
	    temp_mute( bktr, TRUE );
	    temp = tv_freq( bktr, temp, FM_RADIO_FREQUENCY );
	    temp_mute( bktr, FALSE );
#ifdef BKTR_RADIO_DEBUG
  if(temp)
    printf("%s: tv_freq returned: %d\n", bktr_name(bktr), temp);
#endif
	    if ( temp < 0 )
		    return( EINVAL );
	    *(unsigned long *)arg = temp;
	    break;

	/* Luigi's I2CWR ioctl */ 
	case BT848_I2CWR:
		par = *(u_long *)arg;
		write = (par >> 24) & 0xff ;
		i2c_addr = (par >> 16) & 0xff ;
		i2c_port = (par >> 8) & 0xff ;
		data = (par) & 0xff ;
 
		if (write) { 
			i2cWrite( bktr, i2c_addr, i2c_port, data);
		} else {
			data = i2cRead( bktr, i2c_addr);
		}
		*(u_long *)arg = (par & 0xffffff00) | ( data & 0xff );
		break;


#ifdef BT848_MSP_READ
	/* I2C ioctls to allow userland access to the MSP chip */
	case BT848_MSP_READ:
		{
		struct bktr_msp_control *msp;
		msp = (struct bktr_msp_control *) arg;
		msp->data = msp_dpl_read(bktr, bktr->msp_addr,
		                         msp->function, msp->address);
		break;
		}

	case BT848_MSP_WRITE:
		{
		struct bktr_msp_control *msp;
		msp = (struct bktr_msp_control *) arg;
		msp_dpl_write(bktr, bktr->msp_addr, msp->function,
		             msp->address, msp->data );
		break;
		}

	case BT848_MSP_RESET:
		msp_dpl_reset(bktr, bktr->msp_addr);
		break;
#endif

	default:
		return common_ioctl( bktr, cmd, arg );
	}

	return( 0 );
}


/*
 * common ioctls
 */
static int
common_ioctl( bktr_ptr_t bktr, ioctl_cmd_t cmd, caddr_t arg )
{
        int                           pixfmt;
	unsigned int	              temp;
	struct meteor_pixfmt          *pf_pub;

	switch (cmd) {

	case METEORSINPUT:	/* set input device */
		/*Bt848 has 3 MUX Inputs. Bt848A/849A/878/879 has 4 MUX Inputs*/
		/* On the original bt848 boards, */
		/*   Tuner is MUX0, RCA is MUX1, S-Video is MUX2 */
		/* On the Hauppauge bt878 boards, */
		/*   Tuner is MUX0, RCA is MUX3 */
		/* Unfortunately Meteor driver codes DEV_RCA as DEV_0, so we */
		/* stick with this system in our Meteor Emulation */

		switch(*(unsigned long *)arg & METEOR_DEV_MASK) {

		/* this is the RCA video input */
		case 0:		/* default */
		case METEOR_INPUT_DEV0:
		  /* METEOR_INPUT_DEV_RCA: */
		        bktr->flags = (bktr->flags & ~METEOR_DEV_MASK)
			  | METEOR_DEV0;
			OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM)
			                 & ~BT848_IFORM_MUXSEL);

			/* work around for new Hauppauge 878 cards */
			if ((bktr->card.card_id == CARD_HAUPPAUGE) &&
				(bktr->id==BROOKTREE_878 ||
				 bktr->id==BROOKTREE_879) )
				OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) | BT848_IFORM_M_MUX3);
			else
				OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) | BT848_IFORM_M_MUX1);

			OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL) & ~BT848_E_CONTROL_COMP);
			OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL) & ~BT848_O_CONTROL_COMP);
			set_audio( bktr, AUDIO_EXTERN );
			break;

		/* this is the tuner input */
		case METEOR_INPUT_DEV1:
			bktr->flags = (bktr->flags & ~METEOR_DEV_MASK)
				| METEOR_DEV1;
			OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) & ~BT848_IFORM_MUXSEL);
			OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) | BT848_IFORM_M_MUX0);
			OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL) & ~BT848_E_CONTROL_COMP);
			OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL) & ~BT848_O_CONTROL_COMP);
			set_audio( bktr, AUDIO_TUNER );
			break;

		/* this is the S-VHS input, but with a composite camera */
		case METEOR_INPUT_DEV2:
			bktr->flags = (bktr->flags & ~METEOR_DEV_MASK)
				| METEOR_DEV2;
			OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) & ~BT848_IFORM_MUXSEL);
			OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) | BT848_IFORM_M_MUX2);
			OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL) & ~BT848_E_CONTROL_COMP);
			OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_E_CONTROL) & ~BT848_O_CONTROL_COMP);
			set_audio( bktr, AUDIO_EXTERN );
			break;

		/* this is the S-VHS input */
		case METEOR_INPUT_DEV_SVIDEO:
			bktr->flags = (bktr->flags & ~METEOR_DEV_MASK)
				| METEOR_DEV_SVIDEO;
			OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) & ~BT848_IFORM_MUXSEL);
			OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) | BT848_IFORM_M_MUX2);
			OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL) | BT848_E_CONTROL_COMP);
			OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL) | BT848_O_CONTROL_COMP);
			set_audio( bktr, AUDIO_EXTERN );
			break;

		case METEOR_INPUT_DEV3:
		  if ((bktr->id == BROOKTREE_848A) ||
		      (bktr->id == BROOKTREE_849A) ||
		      (bktr->id == BROOKTREE_878) ||
		      (bktr->id == BROOKTREE_879) ) {
			bktr->flags = (bktr->flags & ~METEOR_DEV_MASK)
				| METEOR_DEV3;
			OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) & ~BT848_IFORM_MUXSEL);

			/* work around for new Hauppauge 878 cards */
			if ((bktr->card.card_id == CARD_HAUPPAUGE) &&
				(bktr->id==BROOKTREE_878 ||
				 bktr->id==BROOKTREE_879) )
				OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) | BT848_IFORM_M_MUX1);
			else
				OUTB(bktr, BKTR_IFORM, INB(bktr, BKTR_IFORM) | BT848_IFORM_M_MUX3);

			OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL) & ~BT848_E_CONTROL_COMP);
			OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL) & ~BT848_O_CONTROL_COMP);
			set_audio( bktr, AUDIO_EXTERN );

			break;
		  }	

		default:
			return( EINVAL );
		}
		break;

	case METEORGINPUT:	/* get input device */
		*(u_long *)arg = bktr->flags & METEOR_DEV_MASK;
		break;

	case METEORSACTPIXFMT:
		if (( *(int *)arg < 0 ) ||
		    ( *(int *)arg >= PIXFMT_TABLE_SIZE ))
			return( EINVAL );

		bktr->pixfmt          = *(int *)arg;
		OUTB(bktr, BKTR_COLOR_CTL, (INB(bktr, BKTR_COLOR_CTL) & 0xf0)
		     | pixfmt_swap_flags( bktr->pixfmt ));
		bktr->pixfmt_compat   = FALSE;
		break;
	
	case METEORGACTPIXFMT:
		*(int *)arg = bktr->pixfmt;
		break;

	case METEORGSUPPIXFMT :
		pf_pub = (struct meteor_pixfmt *)arg;
		pixfmt = pf_pub->index;

		if (( pixfmt < 0 ) || ( pixfmt >= PIXFMT_TABLE_SIZE ))
			return( EINVAL );

		memcpy( pf_pub, &pixfmt_table[ pixfmt ].public, 
			sizeof( *pf_pub ) );

		/*  Patch in our format index  */
		pf_pub->index       = pixfmt;
		break;

#if defined( STATUS_SUM )
	case BT848_GSTATUS:	/* reap status */
		{
                DECLARE_INTR_MASK(s);
		DISABLE_INTR(s);
		temp = status_sum;
		status_sum = 0;
		ENABLE_INTR(s);
		*(u_int*)arg = temp;
		break;
		}
#endif /* STATUS_SUM */

	default:
		return( ENOTTY );
	}

	return( 0 );
}




/******************************************************************************
 * bt848 RISC programming routines:
 */


/*
 * 
 */
#if defined(BT848_DEBUG) || defined(BT848_DUMP)
static int
dump_bt848( bktr_ptr_t bktr )
{
	int	r[60]={
			   4,    8, 0xc, 0x8c, 0x10, 0x90, 0x14, 0x94, 
			0x18, 0x98, 0x1c, 0x9c, 0x20, 0xa0, 0x24, 0xa4,
			0x28, 0x2c, 0xac, 0x30, 0x34, 0x38, 0x3c, 0x40,
			0xc0, 0x48, 0x4c, 0xcc, 0x50, 0xd0, 0xd4, 0x60,
			0x64, 0x68, 0x6c, 0xec, 0xd8, 0xdc, 0xe0, 0xe4,
			0,	 0,    0,    0
		   };
	int	i;

	for (i = 0; i < 40; i+=4) {
		printf("%s: Reg:value : \t%x:%x \t%x:%x \t %x:%x \t %x:%x\n",
		       bktr_name(bktr), 
		       r[i], INL(bktr, r[i]),
		       r[i+1], INL(bktr, r[i+1]),
		       r[i+2], INL(bktr, r[i+2]),
		       r[i+3], INL(bktr, r[i+3]));
	}

	printf("%s: INT STAT %x \n", bktr_name(bktr),
	       INL(bktr, BKTR_INT_STAT)); 
	printf("%s: Reg INT_MASK %x \n", bktr_name(bktr),
	       INL(bktr, BKTR_INT_MASK));
	printf("%s: Reg GPIO_DMA_CTL %x \n", bktr_name(bktr),
	       INW(bktr, BKTR_GPIO_DMA_CTL));

	return( 0 );
}

#endif

/*
 * build write instruction
 */
#define BKTR_FM1      0x6	/* packed data to follow */
#define BKTR_FM3      0xe	/* planar data to follow */
#define BKTR_VRE      0x4	/* Marks the end of the even field */
#define BKTR_VRO      0xC	/* Marks the end of the odd field */
#define BKTR_PXV      0x0	/* valid word (never used) */
#define BKTR_EOL      0x1	/* last dword, 4 bytes */
#define BKTR_SOL      0x2	/* first dword */

#define OP_WRITE      (0x1 << 28)
#define OP_SKIP       (0x2 << 28)
#define OP_WRITEC     (0x5 << 28)
#define OP_JUMP	      (0x7 << 28)
#define OP_SYNC	      (0x8 << 28)
#define OP_WRITE123   (0x9 << 28)
#define OP_WRITES123  (0xb << 28)
#define OP_SOL	      (1 << 27)		/* first instr for scanline */
#define OP_EOL	      (1 << 26)

#define BKTR_RESYNC   (1 << 15)
#define BKTR_GEN_IRQ  (1 << 24)

/*
 * The RISC status bits can be set/cleared in the RISC programs
 * and tested in the Interrupt Handler
 */
#define BKTR_SET_RISC_STATUS_BIT0 (1 << 16)
#define BKTR_SET_RISC_STATUS_BIT1 (1 << 17)
#define BKTR_SET_RISC_STATUS_BIT2 (1 << 18)
#define BKTR_SET_RISC_STATUS_BIT3 (1 << 19)

#define BKTR_CLEAR_RISC_STATUS_BIT0 (1 << 20)
#define BKTR_CLEAR_RISC_STATUS_BIT1 (1 << 21)
#define BKTR_CLEAR_RISC_STATUS_BIT2 (1 << 22)
#define BKTR_CLEAR_RISC_STATUS_BIT3 (1 << 23)

#define BKTR_TEST_RISC_STATUS_BIT0 (1 << 28)
#define BKTR_TEST_RISC_STATUS_BIT1 (1 << 29)
#define BKTR_TEST_RISC_STATUS_BIT2 (1 << 30)
#define BKTR_TEST_RISC_STATUS_BIT3 (1U << 31)

static bool_t notclipped (bktr_reg_t * bktr, int x, int width) {
    int i;
    bktr_clip_t * clip_node;
    bktr->clip_start = -1;
    bktr->last_y = 0;
    bktr->y = 0;
    bktr->y2 = width;
    bktr->line_length = width;
    bktr->yclip = -1;
    bktr->yclip2 = -1;
    bktr->current_col = 0;
    
    if (bktr->max_clip_node == 0 ) return TRUE;
    clip_node = (bktr_clip_t *) &bktr->clip_list[0];


    for (i = 0; i < bktr->max_clip_node; i++ ) {
	clip_node = (bktr_clip_t *) &bktr->clip_list[i];
	if (x >= clip_node->x_min && x <= clip_node->x_max  ) {
	    bktr->clip_start = i;
	    return FALSE;
	}
    }	
    
    return TRUE;
}	

static bool_t getline(bktr_reg_t *bktr, int x ) {
    int i, j;
    bktr_clip_t * clip_node ;
    
    if (bktr->line_length == 0 || 
	bktr->current_col >= bktr->line_length) return FALSE;

    bktr->y = min(bktr->last_y, bktr->line_length);
    bktr->y2 = bktr->line_length;

    bktr->yclip = bktr->yclip2 = -1;
    for (i = bktr->clip_start; i < bktr->max_clip_node; i++ ) {
	clip_node = (bktr_clip_t *) &bktr->clip_list[i];
	if (x >= clip_node->x_min && x <= clip_node->x_max) {
	    if (bktr->last_y <= clip_node->y_min) {
		bktr->y =      min(bktr->last_y, bktr->line_length);
		bktr->y2 =     min(clip_node->y_min, bktr->line_length);
		bktr->yclip =  min(clip_node->y_min, bktr->line_length);
		bktr->yclip2 = min(clip_node->y_max, bktr->line_length);
		bktr->last_y = bktr->yclip2;
		bktr->clip_start = i;
		
		for (j = i+1; j  < bktr->max_clip_node; j++ ) {
		    clip_node = (bktr_clip_t *) &bktr->clip_list[j];
		    if (x >= clip_node->x_min && x <= clip_node->x_max) {
			if (bktr->last_y >= clip_node->y_min) {
			    bktr->yclip2 = min(clip_node->y_max, bktr->line_length);
			    bktr->last_y = bktr->yclip2;
			    bktr->clip_start = j;
			}	
		    } else break  ;
		}	
		return TRUE;
	    }	
	}
    }

    if (bktr->current_col <= bktr->line_length) {
	bktr->current_col = bktr->line_length;
	return TRUE;
    }
    return FALSE;
}
    
static bool_t split(bktr_reg_t * bktr, volatile uint32_t **dma_prog, int width ,
		    u_long operation, int pixel_width,
		    volatile u_char ** target_buffer, int cols ) {

 u_long flag, flag2;
 struct meteor_pixfmt *pf = &pixfmt_table[ bktr->pixfmt ].public;
 u_int  skip, start_skip;

  /*  For RGB24, we need to align the component in FIFO Byte Lane 0         */
  /*    to the 1st byte in the mem dword containing our start addr.         */
  /*    BTW, we know this pixfmt's 1st byte is Blue; thus the start addr    */
  /*     must be Blue.                                                      */
  start_skip = 0;
  if (( pf->type == METEOR_PIXTYPE_RGB ) && ( pf->Bpp == 3 ))
	  switch ( ((uintptr_t) (volatile void *) *target_buffer) % 4 ) {
	  case 2 : start_skip = 4 ; break;
	  case 1 : start_skip = 8 ; break;
	  }

 if ((width * pixel_width) < DMA_BT848_SPLIT ) {
     if (  width == cols) {
	 flag = OP_SOL | OP_EOL;
       } else if (bktr->current_col == 0 ) {
	    flag  = OP_SOL;
       } else if (bktr->current_col == cols) {
	    flag = OP_EOL;
       } else flag = 0;	

     skip = 0;
     if (( flag & OP_SOL ) && ( start_skip > 0 )) {
	     *(*dma_prog)++ = OP_SKIP | OP_SOL | start_skip;
	     flag &= ~OP_SOL;
	     skip = start_skip;
     }

     *(*dma_prog)++ = operation | flag  | (width * pixel_width - skip);
     if (operation != OP_SKIP ) 
	 *(*dma_prog)++ = (uintptr_t) (volatile void *) *target_buffer;

     *target_buffer += width * pixel_width;
     bktr->current_col += width;

 } else {

	if (bktr->current_col == 0 && width == cols) {
	    flag = OP_SOL ;
	    flag2 = OP_EOL;
        } else if (bktr->current_col == 0 ) {
	    flag = OP_SOL;
	    flag2 = 0;
	} else if (bktr->current_col >= cols)  {
	    flag =  0;
	    flag2 = OP_EOL;
	} else {
	    flag =  0;
	    flag2 = 0;
	}

	skip = 0;
	if (( flag & OP_SOL ) && ( start_skip > 0 )) {
		*(*dma_prog)++ = OP_SKIP | OP_SOL | start_skip;
		flag &= ~OP_SOL;
		skip = start_skip;
	}

	*(*dma_prog)++ = operation  | flag |
	      (width * pixel_width / 2 - skip);
	if (operation != OP_SKIP ) 
	      *(*dma_prog)++ = (uintptr_t) (volatile void *) *target_buffer ;
	*target_buffer +=  (width * pixel_width / 2) ;

	if ( operation == OP_WRITE )
		operation = OP_WRITEC;
	*(*dma_prog)++ = operation | flag2 |
	    (width * pixel_width / 2);
	*target_buffer +=  (width * pixel_width / 2) ;
	  bktr->current_col += width;

    }
 return TRUE;
}


/*
 * Generate the RISC instructions to capture both VBI and video images
 */
static void
rgb_vbi_prog( bktr_ptr_t bktr, char i_flag, int cols, int rows, int interlace )
{
	int			i;
	volatile uint32_t	target_buffer, buffer, target,width;
	volatile uint32_t	pitch;
	volatile uint32_t	*dma_prog;	/* DMA prog is an array of 
						32 bit RISC instructions */
	volatile uint32_t	*loop_point;
        struct meteor_pixfmt_internal *pf_int = &pixfmt_table[ bktr->pixfmt ];
	u_int                   Bpp = pf_int->public.Bpp;
	unsigned int            vbisamples;     /* VBI samples per line */
	unsigned int            vbilines;       /* VBI lines per field */
	unsigned int            num_dwords;     /* DWORDS per line */

	vbisamples = format_params[bktr->format_params].vbi_num_samples;
	vbilines   = format_params[bktr->format_params].vbi_num_lines;
	num_dwords = vbisamples/4;

	OUTB(bktr, BKTR_COLOR_FMT, pf_int->color_fmt);
	OUTB(bktr, BKTR_ADC, SYNC_LEVEL);
	OUTB(bktr, BKTR_VBI_PACK_SIZE, ((num_dwords)) & 0xff);
	OUTB(bktr, BKTR_VBI_PACK_DEL, ((num_dwords)>> 8) & 0x01); /* no hdelay    */
							    /* no ext frame */

	OUTB(bktr, BKTR_OFORM, 0x00);

 	OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) | 0x40); /* set chroma comb */
 	OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) | 0x40);
	OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) & ~0x80); /* clear Ycomb */
	OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) & ~0x80);

 	/* disable gamma correction removal */
 	OUTB(bktr, BKTR_COLOR_CTL, INB(bktr, BKTR_COLOR_CTL) | BT848_COLOR_CTL_GAMMA);

	if (cols > 385 ) {
	    OUTB(bktr, BKTR_E_VTC, 0);
	    OUTB(bktr, BKTR_O_VTC, 0);
	} else {
	    OUTB(bktr, BKTR_E_VTC, 1);
	    OUTB(bktr, BKTR_O_VTC, 1);
	}
	bktr->capcontrol = 3 << 2 |  3;

	dma_prog = (uint32_t *) bktr->dma_prog;

	/* Construct Write */

	if (bktr->video.addr) {
		target_buffer = (u_long) bktr->video.addr;
		pitch = bktr->video.width;
	}
	else {
		target_buffer = (u_long) vtophys(bktr->bigbuf);
		pitch = cols*Bpp;
	}

	buffer = target_buffer;

	/* Wait for the VRE sync marking the end of the Even and
	 * the start of the Odd field. Resync here.
	 */
	*dma_prog++ = OP_SYNC | BKTR_RESYNC |BKTR_VRE;
	*dma_prog++ = 0;

	loop_point = dma_prog;

	/* store the VBI data */
	/* look for sync with packed data */
	*dma_prog++ = OP_SYNC | BKTR_FM1;
	*dma_prog++ = 0;
	for(i = 0; i < vbilines; i++) {
		*dma_prog++ = OP_WRITE | OP_SOL | OP_EOL | vbisamples;
		*dma_prog++ = (u_long) vtophys((caddr_t)bktr->vbidata +
					(i * VBI_LINE_SIZE));
	}

	if ( (i_flag == 2/*Odd*/) || (i_flag==3) /*interlaced*/ ) { 
		/* store the Odd field video image */
		/* look for sync with packed data */
		*dma_prog++ = OP_SYNC  | BKTR_FM1;
		*dma_prog++ = 0;  /* NULL WORD */
		width = cols;
		for (i = 0; i < (rows/interlace); i++) {
		    target = target_buffer;
		    if ( notclipped(bktr, i, width)) {
			split(bktr, (volatile uint32_t **) &dma_prog,
			      bktr->y2 - bktr->y, OP_WRITE,
			      Bpp, (volatile u_char **)(uintptr_t)&target,  cols);
	
		    } else {
			while(getline(bktr, i)) {
			    if (bktr->y != bktr->y2 ) {
				split(bktr, (volatile uint32_t **) &dma_prog,
				      bktr->y2 - bktr->y, OP_WRITE,
				      Bpp, (volatile u_char **)(uintptr_t)&target, cols);
			    }
			    if (bktr->yclip != bktr->yclip2 ) {
				split(bktr,(volatile uint32_t **) &dma_prog,
				      bktr->yclip2 - bktr->yclip,
				      OP_SKIP,
				      Bpp, (volatile u_char **)(uintptr_t)&target,  cols);
			    }
			}
			
		    }
	
		    target_buffer += interlace * pitch;
	
		}

	} /* end if */

	/* Grab the Even field */
	/* Look for the VRO, end of Odd field, marker */
	*dma_prog++ = OP_SYNC | BKTR_GEN_IRQ | BKTR_RESYNC | BKTR_VRO;
	*dma_prog++ = 0;  /* NULL WORD */

	/* store the VBI data */
	/* look for sync with packed data */
	*dma_prog++ = OP_SYNC | BKTR_FM1;
	*dma_prog++ = 0;
	for(i = 0; i < vbilines; i++) {
		*dma_prog++ = OP_WRITE | OP_SOL | OP_EOL | vbisamples;
		*dma_prog++ = (u_long) vtophys((caddr_t)bktr->vbidata +
				((i+MAX_VBI_LINES) * VBI_LINE_SIZE));
	}

	/* store the video image */
	if (i_flag == 1) /*Even Only*/
	        target_buffer = buffer;
	if (i_flag == 3) /*interlaced*/
	        target_buffer = buffer+pitch;


	if ((i_flag == 1) /*Even Only*/ || (i_flag==3) /*interlaced*/) {
		/* look for sync with packed data */
		*dma_prog++ = OP_SYNC | BKTR_FM1;
		*dma_prog++ = 0;  /* NULL WORD */
		width = cols;
		for (i = 0; i < (rows/interlace); i++) {
		    target = target_buffer;
		    if ( notclipped(bktr, i, width)) {
			split(bktr, (volatile uint32_t **) &dma_prog,
			      bktr->y2 - bktr->y, OP_WRITE,
			      Bpp, (volatile u_char **)(uintptr_t)&target,  cols);
		    } else {
			while(getline(bktr, i)) {
			    if (bktr->y != bktr->y2 ) {
				split(bktr, (volatile uint32_t **) &dma_prog,
				      bktr->y2 - bktr->y, OP_WRITE,
				      Bpp, (volatile u_char **)(uintptr_t)&target,
				      cols);
			    }	
			    if (bktr->yclip != bktr->yclip2 ) {
				split(bktr, (volatile uint32_t **) &dma_prog,
				      bktr->yclip2 - bktr->yclip, OP_SKIP,
				      Bpp, (volatile u_char **)(uintptr_t) &target,  cols);
			    }	

			}	

		    }

		    target_buffer += interlace * pitch;

		}
	}

	/* Look for end of 'Even Field' */
	*dma_prog++ = OP_SYNC | BKTR_GEN_IRQ | BKTR_RESYNC | BKTR_VRE;
	*dma_prog++ = 0;  /* NULL WORD */

	*dma_prog++ = OP_JUMP ;
	*dma_prog++ = (u_long ) vtophys(loop_point) ;
	*dma_prog++ = 0;  /* NULL WORD */

}




static void
rgb_prog( bktr_ptr_t bktr, char i_flag, int cols, int rows, int interlace )
{
	int			i;
	volatile uint32_t		target_buffer, buffer, target,width;
	volatile uint32_t	pitch;
	volatile  uint32_t	*dma_prog;
        struct meteor_pixfmt_internal *pf_int = &pixfmt_table[ bktr->pixfmt ];
	u_int                   Bpp = pf_int->public.Bpp;

	OUTB(bktr, BKTR_COLOR_FMT, pf_int->color_fmt);
	OUTB(bktr, BKTR_VBI_PACK_SIZE, 0);
	OUTB(bktr, BKTR_VBI_PACK_DEL, 0);
	OUTB(bktr, BKTR_ADC, SYNC_LEVEL);

	OUTB(bktr, BKTR_OFORM, 0x00);

 	OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) | 0x40); /* set chroma comb */
 	OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) | 0x40);
	OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) & ~0x80); /* clear Ycomb */
	OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) & ~0x80);

 	/* disable gamma correction removal */
	OUTB(bktr, BKTR_COLOR_CTL, INB(bktr, BKTR_COLOR_CTL) | BT848_COLOR_CTL_GAMMA);

	if (cols > 385 ) {
	    OUTB(bktr, BKTR_E_VTC, 0);
	    OUTB(bktr, BKTR_O_VTC, 0);
	} else {
	    OUTB(bktr, BKTR_E_VTC, 1);
	    OUTB(bktr, BKTR_O_VTC, 1);
	}
	bktr->capcontrol = 3 << 2 |  3;

	dma_prog = (uint32_t *) bktr->dma_prog;

	/* Construct Write */

	if (bktr->video.addr) {
		target_buffer = (uint32_t) bktr->video.addr;
		pitch = bktr->video.width;
	}
	else {
		target_buffer = (uint32_t) vtophys(bktr->bigbuf);
		pitch = cols*Bpp;
	}

	buffer = target_buffer;

	/* contruct sync : for video packet format */
	*dma_prog++ = OP_SYNC  | BKTR_RESYNC | BKTR_FM1;

	/* sync, mode indicator packed data */
	*dma_prog++ = 0;  /* NULL WORD */
	width = cols;
	for (i = 0; i < (rows/interlace); i++) {
	    target = target_buffer;
	    if ( notclipped(bktr, i, width)) {
		split(bktr, (volatile uint32_t **) &dma_prog,
		      bktr->y2 - bktr->y, OP_WRITE,
		      Bpp, (volatile u_char **)(uintptr_t)&target,  cols);

	    } else {
		while(getline(bktr, i)) {
		    if (bktr->y != bktr->y2 ) {
			split(bktr, (volatile uint32_t **) &dma_prog,
			      bktr->y2 - bktr->y, OP_WRITE,
			      Bpp, (volatile u_char **)(uintptr_t)&target, cols);
		    }
		    if (bktr->yclip != bktr->yclip2 ) {
			split(bktr,(volatile uint32_t **) &dma_prog,
			      bktr->yclip2 - bktr->yclip,
			      OP_SKIP,
			      Bpp, (volatile u_char **)(uintptr_t)&target,  cols);
		    }
		}

	    }

	    target_buffer += interlace * pitch;

	}

	switch (i_flag) {
	case 1:
		/* sync vre */
		*dma_prog++ = OP_SYNC | BKTR_GEN_IRQ | BKTR_VRO;
		*dma_prog++ = 0;  /* NULL WORD */

		*dma_prog++ = OP_JUMP;
		*dma_prog++ = (uint32_t ) vtophys(bktr->dma_prog);
		return;

	case 2:
		/* sync vro */
		*dma_prog++ = OP_SYNC | BKTR_GEN_IRQ | BKTR_VRE;
		*dma_prog++ = 0;  /* NULL WORD */

		*dma_prog++ = OP_JUMP;
		*dma_prog++ = (uint32_t ) vtophys(bktr->dma_prog);
		return;

	case 3:
		/* sync vro */
		*dma_prog++ = OP_SYNC | BKTR_GEN_IRQ | BKTR_RESYNC | BKTR_VRO;
		*dma_prog++ = 0;  /* NULL WORD */
		*dma_prog++ = OP_JUMP;
		*dma_prog = (uint32_t ) vtophys(bktr->odd_dma_prog);
		break;
	}

	if (interlace == 2) {

	        target_buffer = buffer + pitch; 

		dma_prog = (uint32_t *) bktr->odd_dma_prog;

		/* sync vre IRQ bit */
		*dma_prog++ = OP_SYNC | BKTR_RESYNC | BKTR_FM1;
		*dma_prog++ = 0;  /* NULL WORD */
                width = cols;
		for (i = 0; i < (rows/interlace); i++) {
		    target = target_buffer;
		    if ( notclipped(bktr, i, width)) {
			split(bktr, (volatile uint32_t **) &dma_prog,
			      bktr->y2 - bktr->y, OP_WRITE,
			      Bpp, (volatile u_char **)(uintptr_t)&target,  cols);
		    } else {
			while(getline(bktr, i)) {
			    if (bktr->y != bktr->y2 ) {
				split(bktr, (volatile uint32_t **) &dma_prog,
				      bktr->y2 - bktr->y, OP_WRITE,
				      Bpp, (volatile u_char **)(uintptr_t)&target,
				      cols);
			    }	
			    if (bktr->yclip != bktr->yclip2 ) {
				split(bktr, (volatile uint32_t **) &dma_prog,
				      bktr->yclip2 - bktr->yclip, OP_SKIP,
				      Bpp, (volatile u_char **)(uintptr_t)&target,  cols);
			    }	

			}	

		    }

		    target_buffer += interlace * pitch;

		}
	}

	/* sync vre IRQ bit */
	*dma_prog++ = OP_SYNC | BKTR_GEN_IRQ | BKTR_RESYNC | BKTR_VRE;
	*dma_prog++ = 0;  /* NULL WORD */
	*dma_prog++ = OP_JUMP ;
	*dma_prog++ = (uint32_t ) vtophys(bktr->dma_prog) ;
	*dma_prog++ = 0;  /* NULL WORD */
}


/*
 * 
 */
static void
yuvpack_prog( bktr_ptr_t bktr, char i_flag,
	      int cols, int rows, int interlace )
{
	int			i;
	volatile unsigned int	inst;
	volatile unsigned int	inst3;
	volatile uint32_t	target_buffer, buffer;
	volatile  uint32_t	*dma_prog;
        struct meteor_pixfmt_internal *pf_int = &pixfmt_table[ bktr->pixfmt ];
	int			b;

	OUTB(bktr, BKTR_COLOR_FMT, pf_int->color_fmt);

	OUTB(bktr, BKTR_E_SCLOOP, INB(bktr, BKTR_E_SCLOOP) | BT848_E_SCLOOP_CAGC); /* enable chroma comb */
	OUTB(bktr, BKTR_O_SCLOOP, INB(bktr, BKTR_O_SCLOOP) | BT848_O_SCLOOP_CAGC);

	OUTB(bktr, BKTR_COLOR_CTL, INB(bktr, BKTR_COLOR_CTL) | BT848_COLOR_CTL_RGB_DED | BT848_COLOR_CTL_GAMMA);
	OUTB(bktr, BKTR_ADC, SYNC_LEVEL);

	bktr->capcontrol = 3 << 2 |  3;

	dma_prog = (uint32_t *) bktr->dma_prog;

	/* Construct Write */
    
	/* write , sol, eol */
	inst = OP_WRITE	 | OP_SOL | (cols);
	/* write , sol, eol */
	inst3 = OP_WRITE | OP_EOL | (cols);

	if (bktr->video.addr)
		target_buffer = (uint32_t) bktr->video.addr;
	else
		target_buffer = (uint32_t) vtophys(bktr->bigbuf);

	buffer = target_buffer;

	/* contruct sync : for video packet format */
	/* sync, mode indicator packed data */
	*dma_prog++ = OP_SYNC | BKTR_RESYNC | BKTR_FM1;
	*dma_prog++ = 0;  /* NULL WORD */

	b = cols;

	for (i = 0; i < (rows/interlace); i++) {
		*dma_prog++ = inst;
		*dma_prog++ = target_buffer;
		*dma_prog++ = inst3;
		*dma_prog++ = target_buffer + b; 
		target_buffer += interlace*(cols * 2);
	}

	switch (i_flag) {
	case 1:
		/* sync vre */
		*dma_prog++ = OP_SYNC  | BKTR_GEN_IRQ | BKTR_VRE;
		*dma_prog++ = 0;  /* NULL WORD */

		*dma_prog++ = OP_JUMP;
		*dma_prog++ = (uint32_t) vtophys(bktr->dma_prog);
		return;

	case 2:
		/* sync vro */
		*dma_prog++ = OP_SYNC  | BKTR_GEN_IRQ | BKTR_VRO;
		*dma_prog++ = 0;  /* NULL WORD */
		*dma_prog++ = OP_JUMP;
		*dma_prog++ = (uint32_t) vtophys(bktr->dma_prog);
		return;

	case 3:
		/* sync vro */
		*dma_prog++ = OP_SYNC | BKTR_GEN_IRQ | BKTR_RESYNC | BKTR_VRO;
		*dma_prog++ = 0;  /* NULL WORD */
		*dma_prog++ = OP_JUMP  ;
		*dma_prog = (uint32_t) vtophys(bktr->odd_dma_prog);
		break;
	}

	if (interlace == 2) {

		target_buffer =	 (uint32_t) buffer + cols*2;

		dma_prog = (uint32_t *) bktr->odd_dma_prog;

		/* sync vre */
		*dma_prog++ = OP_SYNC | BKTR_RESYNC | BKTR_FM1;
		*dma_prog++ = 0;  /* NULL WORD */

		for (i = 0; i < (rows/interlace) ; i++) {
			*dma_prog++ = inst;
			*dma_prog++ = target_buffer;
			*dma_prog++ = inst3;
			*dma_prog++ = target_buffer + b;
			target_buffer += interlace * ( cols*2);
		}
	}

	/* sync vro IRQ bit */
	*dma_prog++ = OP_SYNC   |  BKTR_GEN_IRQ  | BKTR_RESYNC |  BKTR_VRE;
	*dma_prog++ = 0;  /* NULL WORD */
	*dma_prog++ = OP_JUMP ;
	*dma_prog++ = (uint32_t) vtophys(bktr->dma_prog);

	*dma_prog++ = OP_JUMP;
	*dma_prog++ = (uint32_t) vtophys(bktr->dma_prog);
	*dma_prog++ = 0;  /* NULL WORD */
}


/*
 * 
 */
static void
yuv422_prog( bktr_ptr_t bktr, char i_flag,
	     int cols, int rows, int interlace ){

	int			i;
	volatile unsigned int	inst;
	volatile uint32_t	target_buffer, t1, buffer;
	volatile uint32_t	*dma_prog;
        struct meteor_pixfmt_internal *pf_int = &pixfmt_table[ bktr->pixfmt ];

	OUTB(bktr, BKTR_COLOR_FMT, pf_int->color_fmt);

	dma_prog = (uint32_t*) bktr->dma_prog;

	bktr->capcontrol =   1 << 6 | 1 << 4 |	3;

	OUTB(bktr, BKTR_ADC, SYNC_LEVEL);
	OUTB(bktr, BKTR_OFORM, 0x00);

	OUTB(bktr, BKTR_E_CONTROL, INB(bktr, BKTR_E_CONTROL) | BT848_E_CONTROL_LDEC); /* disable luma decimation */
	OUTB(bktr, BKTR_O_CONTROL, INB(bktr, BKTR_O_CONTROL) | BT848_O_CONTROL_LDEC);

	OUTB(bktr, BKTR_E_SCLOOP, INB(bktr, BKTR_E_SCLOOP) | BT848_E_SCLOOP_CAGC);	/* chroma agc enable */
	OUTB(bktr, BKTR_O_SCLOOP, INB(bktr, BKTR_O_SCLOOP) | BT848_O_SCLOOP_CAGC);

	OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) & ~0x80); /* clear Ycomb */
	OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) & ~0x80);
	OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) | 0x40); /* set chroma comb */
	OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) | 0x40);

	/* disable gamma correction removal */
	OUTB(bktr, BKTR_COLOR_CTL, INB(bktr, BKTR_COLOR_CTL) | BT848_COLOR_CTL_GAMMA);

	/* Construct Write */
	inst  = OP_WRITE123  | OP_SOL | OP_EOL |  (cols); 
	if (bktr->video.addr)
		target_buffer = (uint32_t) bktr->video.addr;
	else
		target_buffer = (uint32_t) vtophys(bktr->bigbuf);
    
	buffer = target_buffer;

	t1 = buffer;

	/* contruct sync : for video packet format */
	*dma_prog++ = OP_SYNC  | 1 << 15 |	BKTR_FM3; /*sync, mode indicator packed data*/
	*dma_prog++ = 0;  /* NULL WORD */

	for (i = 0; i < (rows/interlace ) ; i++) {
		*dma_prog++ = inst;
		*dma_prog++ = cols/2 | cols/2 << 16;
		*dma_prog++ = target_buffer;
		*dma_prog++ = t1 + (cols*rows) + i*cols/2 * interlace;
		*dma_prog++ = t1 + (cols*rows) + (cols*rows/2) + i*cols/2 * interlace;
		target_buffer += interlace*cols;
	}

	switch (i_flag) {
	case 1:
		*dma_prog++ = OP_SYNC  | 1 << 24 | BKTR_VRE;  /*sync vre*/
		*dma_prog++ = 0;  /* NULL WORD */

		*dma_prog++ = OP_JUMP ;
		*dma_prog++ = (uint32_t) vtophys(bktr->dma_prog);
		return;

	case 2:
		*dma_prog++ = OP_SYNC  | 1 << 24 | BKTR_VRO;  /*sync vre*/
		*dma_prog++ = 0;  /* NULL WORD */

		*dma_prog++ = OP_JUMP;
		*dma_prog++ = (uint32_t) vtophys(bktr->dma_prog);
		return;

	case 3:
		*dma_prog++ = OP_SYNC	| 1 << 24 |  1 << 15 |   BKTR_VRO; 
		*dma_prog++ = 0;  /* NULL WORD */

		*dma_prog++ = OP_JUMP  ;
		*dma_prog = (uint32_t) vtophys(bktr->odd_dma_prog);
		break;
	}

	if (interlace == 2) {

		dma_prog = (uint32_t *) bktr->odd_dma_prog;

		target_buffer  = (uint32_t) buffer + cols;
		t1 = buffer + cols/2;
		*dma_prog++ = OP_SYNC	|   1 << 15 | BKTR_FM3; 
		*dma_prog++ = 0;  /* NULL WORD */

		for (i = 0; i < (rows/interlace )  ; i++) {
			*dma_prog++ = inst;
			*dma_prog++ = cols/2 | cols/2 << 16;
			*dma_prog++ = target_buffer;
			*dma_prog++ = t1 + (cols*rows) + i*cols/2 * interlace;
			*dma_prog++ = t1 + (cols*rows) + (cols*rows/2) + i*cols/2 * interlace;
			target_buffer += interlace*cols;
		}
	}
    
	*dma_prog++ = OP_SYNC  | 1 << 24 | 1 << 15 |   BKTR_VRE; 
	*dma_prog++ = 0;  /* NULL WORD */
	*dma_prog++ = OP_JUMP ;
	*dma_prog++ = (uint32_t) vtophys(bktr->dma_prog) ;
	*dma_prog++ = 0;  /* NULL WORD */
}


/*
 * 
 */
static void
yuv12_prog( bktr_ptr_t bktr, char i_flag,
	     int cols, int rows, int interlace ){

	int			i;
	volatile unsigned int	inst;
	volatile unsigned int	inst1;
	volatile uint32_t	target_buffer, t1, buffer;
	volatile uint32_t	*dma_prog;
        struct meteor_pixfmt_internal *pf_int = &pixfmt_table[ bktr->pixfmt ];

	OUTB(bktr, BKTR_COLOR_FMT, pf_int->color_fmt);

	dma_prog = (uint32_t *) bktr->dma_prog;

	bktr->capcontrol =   1 << 6 | 1 << 4 |	3;

	OUTB(bktr, BKTR_ADC, SYNC_LEVEL);
	OUTB(bktr, BKTR_OFORM, 0x0);
 
	/* Construct Write */
 	inst  = OP_WRITE123  | OP_SOL | OP_EOL |  (cols); 
 	inst1  = OP_WRITES123  | OP_SOL | OP_EOL |  (cols); 
 	if (bktr->video.addr)
 		target_buffer = (uint32_t) bktr->video.addr;
 	else
 		target_buffer = (uint32_t) vtophys(bktr->bigbuf);
     
	buffer = target_buffer;
 	t1 = buffer;
 
 	*dma_prog++ = OP_SYNC  | 1 << 15 |	BKTR_FM3; /*sync, mode indicator packed data*/
 	*dma_prog++ = 0;  /* NULL WORD */
 	       
 	for (i = 0; i < (rows/interlace )/2 ; i++) {
		*dma_prog++ = inst;
 		*dma_prog++ = cols/2 | (cols/2 << 16);
 		*dma_prog++ = target_buffer;
 		*dma_prog++ = t1 + (cols*rows) + i*cols/2 * interlace;
 		*dma_prog++ = t1 + (cols*rows) + (cols*rows/4) + i*cols/2 * interlace;
 		target_buffer += interlace*cols;
 		*dma_prog++ = inst1;
 		*dma_prog++ = cols/2 | (cols/2 << 16);
 		*dma_prog++ = target_buffer;
 		target_buffer += interlace*cols;
 
 	}
 
 	switch (i_flag) {
 	case 1:
 		*dma_prog++ = OP_SYNC  | 1 << 24 | BKTR_VRE;  /*sync vre*/
 		*dma_prog++ = 0;  /* NULL WORD */

		*dma_prog++ = OP_JUMP;
		*dma_prog++ = (uint32_t) vtophys(bktr->dma_prog);
 		return;

 	case 2:
 		*dma_prog++ = OP_SYNC  | 1 << 24 | BKTR_VRO;  /*sync vro*/
 		*dma_prog++ = 0;  /* NULL WORD */

		*dma_prog++ = OP_JUMP;
		*dma_prog++ = (uint32_t) vtophys(bktr->dma_prog);
 		return;
 
 	case 3:
 		*dma_prog++ = OP_SYNC |  1 << 24 | 1 << 15 | BKTR_VRO;
		*dma_prog++ = 0;  /* NULL WORD */
		*dma_prog++ = OP_JUMP ;
		*dma_prog = (uint32_t) vtophys(bktr->odd_dma_prog);
		break;
	}

	if (interlace == 2) {

		dma_prog = (uint32_t *) bktr->odd_dma_prog;

		target_buffer  = (uint32_t) buffer + cols;
		t1 = buffer + cols/2;
		*dma_prog++ = OP_SYNC   | 1 << 15 | BKTR_FM3; 
		*dma_prog++ = 0;  /* NULL WORD */

		for (i = 0; i < ((rows/interlace )/2 ) ; i++) {
		    *dma_prog++ = inst;
		    *dma_prog++ = cols/2 | (cols/2 << 16);
         	    *dma_prog++ = target_buffer;
		    *dma_prog++ = t1 + (cols*rows) + i*cols/2 * interlace;
		    *dma_prog++ = t1 + (cols*rows) + (cols*rows/4) + i*cols/2 * interlace;
		    target_buffer += interlace*cols;
		    *dma_prog++ = inst1;
		    *dma_prog++ = cols/2 | (cols/2 << 16);
		    *dma_prog++ = target_buffer;
		    target_buffer += interlace*cols;

		}	

	
	}
    
	*dma_prog++ = OP_SYNC |  1 << 24 | 1 << 15 | BKTR_VRE;
	*dma_prog++ = 0;  /* NULL WORD */
	*dma_prog++ = OP_JUMP;
	*dma_prog++ = (uint32_t) vtophys(bktr->dma_prog);
	*dma_prog++ = 0;  /* NULL WORD */
}
  


/*
 * 
 */
static void
build_dma_prog( bktr_ptr_t bktr, char i_flag )
{
	int			rows, cols,  interlace;
	int			tmp_int;
	unsigned int		temp;	
	struct format_params	*fp;
        struct meteor_pixfmt_internal *pf_int = &pixfmt_table[ bktr->pixfmt ];
	

	fp = &format_params[bktr->format_params];

	OUTL(bktr, BKTR_INT_MASK,  ALL_INTS_DISABLED);

	/* disable FIFO & RISC, leave other bits alone */
	OUTW(bktr, BKTR_GPIO_DMA_CTL, INW(bktr, BKTR_GPIO_DMA_CTL) & ~FIFO_RISC_ENABLED);

	/* set video parameters */
	if (bktr->capture_area_enabled)
	  temp = ((quad_t ) fp->htotal* (quad_t) bktr->capture_area_x_size * 4096
		  / fp->scaled_htotal / bktr->cols) -  4096;
	else
	  temp = ((quad_t ) fp->htotal* (quad_t) fp->scaled_hactive * 4096
		  / fp->scaled_htotal / bktr->cols) -  4096;

	/* printf("%s: HSCALE value is %d\n", bktr_name(bktr), temp); */
	OUTB(bktr, BKTR_E_HSCALE_LO, temp & 0xff);
	OUTB(bktr, BKTR_O_HSCALE_LO, temp & 0xff);
	OUTB(bktr, BKTR_E_HSCALE_HI, (temp >> 8) & 0xff);
	OUTB(bktr, BKTR_O_HSCALE_HI, (temp >> 8) & 0xff);
 
	/* horizontal active */
	temp = bktr->cols;
	/* printf("%s: HACTIVE value is %d\n", bktr_name(bktr), temp); */
	OUTB(bktr, BKTR_E_HACTIVE_LO, temp & 0xff);
	OUTB(bktr, BKTR_O_HACTIVE_LO, temp & 0xff);
	OUTB(bktr, BKTR_E_CROP, INB(bktr, BKTR_E_CROP) & ~0x3);
	OUTB(bktr, BKTR_O_CROP, INB(bktr, BKTR_O_CROP) & ~0x3);
	OUTB(bktr, BKTR_E_CROP, INB(bktr, BKTR_E_CROP) | ((temp >> 8) & 0x3));
	OUTB(bktr, BKTR_O_CROP, INB(bktr, BKTR_O_CROP) | ((temp >> 8) & 0x3));
 
	/* horizontal delay */
	if (bktr->capture_area_enabled)
	  temp = ( (fp->hdelay* fp->scaled_hactive + bktr->capture_area_x_offset* fp->scaled_htotal)
		 * bktr->cols) / (bktr->capture_area_x_size * fp->hactive);
	else
	  temp = (fp->hdelay * bktr->cols) / fp->hactive;

	temp = temp & 0x3fe;

	/* printf("%s: HDELAY value is %d\n", bktr_name(bktr), temp); */
	OUTB(bktr, BKTR_E_DELAY_LO, temp & 0xff);
	OUTB(bktr, BKTR_O_DELAY_LO, temp & 0xff);
	OUTB(bktr, BKTR_E_CROP, INB(bktr, BKTR_E_CROP) & ~0xc);
	OUTB(bktr, BKTR_O_CROP, INB(bktr, BKTR_O_CROP) & ~0xc);
	OUTB(bktr, BKTR_E_CROP, INB(bktr, BKTR_E_CROP) | ((temp >> 6) & 0xc));
	OUTB(bktr, BKTR_O_CROP, INB(bktr, BKTR_O_CROP) | ((temp >> 6) & 0xc));

	/* vertical scale */

	if (bktr->capture_area_enabled) {
	  if (bktr->flags  & METEOR_ONLY_ODD_FIELDS ||
	      bktr->flags & METEOR_ONLY_EVEN_FIELDS)
	    tmp_int = 65536 -
	    (((bktr->capture_area_y_size  * 256 + (bktr->rows/2)) / bktr->rows) - 512);
	  else {
	    tmp_int = 65536 -
	    (((bktr->capture_area_y_size * 512 + (bktr->rows / 2)) /  bktr->rows) - 512);
	  }
	} else {
	  if (bktr->flags  & METEOR_ONLY_ODD_FIELDS ||
	      bktr->flags & METEOR_ONLY_EVEN_FIELDS)
	    tmp_int = 65536 -
	    (((fp->vactive  * 256 + (bktr->rows/2)) / bktr->rows) - 512);
	  else {
	    tmp_int = 65536  -
	    (((fp->vactive * 512 + (bktr->rows / 2)) /  bktr->rows) - 512);
	  }
	}

	tmp_int &= 0x1fff;
	/* printf("%s: VSCALE value is %d\n", bktr_name(bktr), tmp_int); */
	OUTB(bktr, BKTR_E_VSCALE_LO, tmp_int & 0xff);
	OUTB(bktr, BKTR_O_VSCALE_LO, tmp_int & 0xff);
	OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) & ~0x1f);
	OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) & ~0x1f);
	OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) | ((tmp_int >> 8) & 0x1f));
	OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) | ((tmp_int >> 8) & 0x1f));


	/* vertical active */
	if (bktr->capture_area_enabled)
	  temp = bktr->capture_area_y_size;
	else
	  temp = fp->vactive;
	/* printf("%s: VACTIVE is %d\n", bktr_name(bktr), temp); */
	OUTB(bktr, BKTR_E_CROP, INB(bktr, BKTR_E_CROP) & ~0x30);
	OUTB(bktr, BKTR_E_CROP, INB(bktr, BKTR_E_CROP) | ((temp >> 4) & 0x30));
	OUTB(bktr, BKTR_E_VACTIVE_LO, temp & 0xff);
	OUTB(bktr, BKTR_O_CROP, INB(bktr, BKTR_O_CROP) & ~0x30);
	OUTB(bktr, BKTR_O_CROP, INB(bktr, BKTR_O_CROP) | ((temp >> 4) & 0x30));
	OUTB(bktr, BKTR_O_VACTIVE_LO, temp & 0xff);
 
	/* vertical delay */
	if (bktr->capture_area_enabled)
	  temp = fp->vdelay + (bktr->capture_area_y_offset);
	else
	  temp = fp->vdelay;
	/* printf("%s: VDELAY is %d\n", bktr_name(bktr), temp); */
	OUTB(bktr, BKTR_E_CROP, INB(bktr, BKTR_E_CROP) & ~0xC0);
	OUTB(bktr, BKTR_E_CROP, INB(bktr, BKTR_E_CROP) | ((temp >> 2) & 0xC0));
	OUTB(bktr, BKTR_E_VDELAY_LO, temp & 0xff);
	OUTB(bktr, BKTR_O_CROP, INB(bktr, BKTR_O_CROP) & ~0xC0);
	OUTB(bktr, BKTR_O_CROP, INB(bktr, BKTR_O_CROP) | ((temp >> 2) & 0xC0));
	OUTB(bktr, BKTR_O_VDELAY_LO, temp & 0xff);

	/* end of video params */

	if ((bktr->xtal_pll_mode == BT848_USE_PLL)
	   && (fp->iform_xtsel==BT848_IFORM_X_XT1)) {
		OUTB(bktr, BKTR_TGCTRL, BT848_TGCTRL_TGCKI_PLL); /* Select PLL mode */
	} else {
		OUTB(bktr, BKTR_TGCTRL, BT848_TGCTRL_TGCKI_XTAL); /* Select Normal xtal 0/xtal 1 mode */
	}

	/* capture control */
	switch (i_flag) {
	case 1:
	        bktr->bktr_cap_ctl = 
		    (BT848_CAP_CTL_DITH_FRAME | BT848_CAP_CTL_EVEN);
		OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) & ~0x20);
		OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) & ~0x20);
		interlace = 1;
		break;
	 case 2:
 	        bktr->bktr_cap_ctl =
			(BT848_CAP_CTL_DITH_FRAME | BT848_CAP_CTL_ODD);
		OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) & ~0x20);
		OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) & ~0x20);
		interlace = 1;
		break;
	 default:
 	        bktr->bktr_cap_ctl = 
			(BT848_CAP_CTL_DITH_FRAME |
			 BT848_CAP_CTL_EVEN | BT848_CAP_CTL_ODD);
		OUTB(bktr, BKTR_E_VSCALE_HI, INB(bktr, BKTR_E_VSCALE_HI) | 0x20);
		OUTB(bktr, BKTR_O_VSCALE_HI, INB(bktr, BKTR_O_VSCALE_HI) | 0x20);
		interlace = 2;
		break;
	}

	OUTL(bktr, BKTR_RISC_STRT_ADD, vtophys(bktr->dma_prog));

	rows = bktr->rows;
	cols = bktr->cols;

	bktr->vbiflags &= ~VBI_CAPTURE;	/* default - no vbi capture */

	/* RGB Grabs. If /dev/vbi is already open, or we are a PAL/SECAM */
	/* user, then use the rgb_vbi RISC program. */
	/* Otherwise, use the normal rgb RISC program */
	if (pf_int->public.type == METEOR_PIXTYPE_RGB) {
		if ( (bktr->vbiflags & VBI_OPEN)
		   ||(bktr->format_params == BT848_IFORM_F_PALBDGHI)
		   ||(bktr->format_params == BT848_IFORM_F_SECAM)
                   ){
			bktr->bktr_cap_ctl |=
		                BT848_CAP_CTL_VBI_EVEN | BT848_CAP_CTL_VBI_ODD;
			bktr->vbiflags |= VBI_CAPTURE;
			rgb_vbi_prog(bktr, i_flag, cols, rows, interlace);
			return;
		} else {
			rgb_prog(bktr, i_flag, cols, rows, interlace);
			return;
		}
	}

	if ( pf_int->public.type  == METEOR_PIXTYPE_YUV ) {
		yuv422_prog(bktr, i_flag, cols, rows, interlace);
		OUTB(bktr, BKTR_COLOR_CTL, (INB(bktr, BKTR_COLOR_CTL) & 0xf0)
		     | pixfmt_swap_flags( bktr->pixfmt ));
		return;
	}

	if ( pf_int->public.type  == METEOR_PIXTYPE_YUV_PACKED ) {
		yuvpack_prog(bktr, i_flag, cols, rows, interlace);
		OUTB(bktr, BKTR_COLOR_CTL, (INB(bktr, BKTR_COLOR_CTL) & 0xf0)
		     | pixfmt_swap_flags( bktr->pixfmt ));
		return;
	}

	if ( pf_int->public.type  == METEOR_PIXTYPE_YUV_12 ) {
		yuv12_prog(bktr, i_flag, cols, rows, interlace);
		OUTB(bktr, BKTR_COLOR_CTL, (INB(bktr, BKTR_COLOR_CTL) & 0xf0)
		     | pixfmt_swap_flags( bktr->pixfmt ));
		return;
	}
	return;
}


/******************************************************************************
 * video & video capture specific routines:
 */


/*
 * 
 */
static void
start_capture( bktr_ptr_t bktr, unsigned type )
{
	u_char			i_flag;
	struct format_params   *fp;

	fp = &format_params[bktr->format_params];

	/*  If requested, clear out capture buf first  */
	if (bktr->clr_on_start && (bktr->video.addr == 0)) {
		bzero((caddr_t)bktr->bigbuf, 
		      (size_t)bktr->rows * bktr->cols * bktr->frames *
			pixfmt_table[ bktr->pixfmt ].public.Bpp);
	}

	OUTB(bktr, BKTR_DSTATUS,  0);
	OUTL(bktr, BKTR_INT_STAT, INL(bktr, BKTR_INT_STAT));

	bktr->flags |= type;
	bktr->flags &= ~METEOR_WANT_MASK;
	switch(bktr->flags & METEOR_ONLY_FIELDS_MASK) {
	case METEOR_ONLY_EVEN_FIELDS:
		bktr->flags |= METEOR_WANT_EVEN;
		i_flag = 1;
		break;
	case METEOR_ONLY_ODD_FIELDS:
		bktr->flags |= METEOR_WANT_ODD;
		i_flag = 2;
		break;
	default:
		bktr->flags |= METEOR_WANT_MASK;
		i_flag = 3;
		break;
	}

	/*  TDEC is only valid for continuous captures  */
	if ( type == METEOR_SINGLE ) {
		u_short	fps_save = bktr->fps;

		set_fps(bktr, fp->frame_rate);
		bktr->fps = fps_save;
	}
	else
		set_fps(bktr, bktr->fps);

	if (bktr->dma_prog_loaded == FALSE) {
		build_dma_prog(bktr, i_flag);
		bktr->dma_prog_loaded = TRUE;
	}
	

	OUTL(bktr, BKTR_RISC_STRT_ADD, vtophys(bktr->dma_prog));

}


/*
 * Set the temporal decimation register to get the desired frame rate.
 * We use the 'skip frame' modus always and always start dropping on an
 * odd field.
 */
static void
set_fps( bktr_ptr_t bktr, u_short fps )
{
	struct format_params	*fp;

	fp = &format_params[bktr->format_params];

	switch(bktr->flags & METEOR_ONLY_FIELDS_MASK) {
	case METEOR_ONLY_EVEN_FIELDS:
		bktr->flags |= METEOR_WANT_EVEN;
		break;
	case METEOR_ONLY_ODD_FIELDS:
		bktr->flags |= METEOR_WANT_ODD;
		break;
	default:
		bktr->flags |= METEOR_WANT_MASK;
		break;
	}

	OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_RISC_DISABLED);
	OUTL(bktr, BKTR_INT_STAT, ALL_INTS_CLEARED);

	bktr->fps = fps;
	OUTB(bktr, BKTR_TDEC, 0);

	if (fps < fp->frame_rate)
		OUTB(bktr, BKTR_TDEC, (fp->frame_rate - fps) & 0x3f);
	else
		OUTB(bktr, BKTR_TDEC, 0);
	return;

}





/* 
 * Given a pixfmt index, compute the bt848 swap_flags necessary to 
 *   achieve the specified swapping.
 * Note that without bt swapping, 2Bpp and 3Bpp modes are written 
 *   byte-swapped, and 4Bpp modes are byte and word swapped (see Table 6 
 *   and read R->L).  
 * Note also that for 3Bpp, we may additionally need to do some creative 
 *   SKIPing to align the FIFO bytelines with the target buffer (see split()).
 * This is abstracted here: e.g. no swaps = RGBA; byte & short swap = ABGR
 *   as one would expect.
 */

static u_int pixfmt_swap_flags( int pixfmt )
{
	struct meteor_pixfmt *pf = &pixfmt_table[ pixfmt ].public;
	u_int		      swapf = 0;

	switch ( pf->Bpp ) {
	case 2 : swapf = ( pf->swap_bytes ? 0 : BSWAP );
		 break;

	case 3 : /* no swaps supported for 3bpp - makes no sense w/ bt848 */
		 break;
		 
	case 4 : if ( pf->swap_bytes )
			swapf = pf->swap_shorts ? 0 : WSWAP;
		 else
			swapf = pf->swap_shorts ? BSWAP : (BSWAP | WSWAP);
		 break;
	}
	return swapf;
}



/* 
 * Converts meteor-defined pixel formats (e.g. METEOR_GEO_RGB16) into
 *   our pixfmt_table indices.
 */

static int oformat_meteor_to_bt( u_long format )
{
	int    i;
        struct meteor_pixfmt *pf1, *pf2;

	/*  Find format in compatibility table  */
	for ( i = 0; i < METEOR_PIXFMT_TABLE_SIZE; i++ )
		if ( meteor_pixfmt_table[i].meteor_format == format )
			break;

	if ( i >= METEOR_PIXFMT_TABLE_SIZE )
		return -1;
	pf1 = &meteor_pixfmt_table[i].public;

	/*  Match it with an entry in master pixel format table  */
	for ( i = 0; i < PIXFMT_TABLE_SIZE; i++ ) {
		pf2 = &pixfmt_table[i].public;

		if (( pf1->type        == pf2->type        ) &&
		    ( pf1->Bpp         == pf2->Bpp         ) &&
		    !bcmp( pf1->masks, pf2->masks, sizeof( pf1->masks )) &&
		    ( pf1->swap_bytes  == pf2->swap_bytes  ) &&
		    ( pf1->swap_shorts == pf2->swap_shorts )) 
			break;
	}
	if ( i >= PIXFMT_TABLE_SIZE )
		return -1;

	return i;
}

/******************************************************************************
 * i2c primitives:
 */

/* */
#define I2CBITTIME		(0x5<<4)	/* 5 * 0.48uS */
#define I2CBITTIME_878              (1 << 7)
#define I2C_READ		0x01
#define I2C_COMMAND		(I2CBITTIME |			\
				 BT848_DATA_CTL_I2CSCL |	\
				 BT848_DATA_CTL_I2CSDA)

#define I2C_COMMAND_878		(I2CBITTIME_878 |			\
				 BT848_DATA_CTL_I2CSCL |	\
				 BT848_DATA_CTL_I2CSDA)

/* Select between old i2c code and new iicbus / smbus code */
#if defined(BKTR_USE_FREEBSD_SMBUS)

/*
 * The hardware interface is actually SMB commands
 */
int
i2cWrite( bktr_ptr_t bktr, int addr, int byte1, int byte2 )
{
	char cmd;

	if (bktr->id == BROOKTREE_848  ||
	    bktr->id == BROOKTREE_848A ||
	    bktr->id == BROOKTREE_849A)
		cmd = I2C_COMMAND;
	else
		cmd = I2C_COMMAND_878;

	if (byte2 != -1) {
		if (smbus_writew(bktr->i2c_sc.smbus, addr, cmd,
			(short)(((byte2 & 0xff) << 8) | (byte1 & 0xff))))
			return (-1);
	} else {
		if (smbus_writeb(bktr->i2c_sc.smbus, addr, cmd,
			(char)(byte1 & 0xff)))
			return (-1);
	}

	/* return OK */
	return( 0 );
}

int
i2cRead( bktr_ptr_t bktr, int addr )
{
	char result;
	char cmd;

	if (bktr->id == BROOKTREE_848  ||
	    bktr->id == BROOKTREE_848A ||
	    bktr->id == BROOKTREE_849A)
		cmd = I2C_COMMAND;
	else
		cmd = I2C_COMMAND_878;

	if (smbus_readb(bktr->i2c_sc.smbus, addr, cmd, &result))
		return (-1);

	return ((int)((unsigned char)result));
}

#define IICBUS(bktr) ((bktr)->i2c_sc.iicbb)

/* The MSP34xx and DPL35xx Audio chip require i2c bus writes of up */
/* to 5 bytes which the bt848 automated i2c bus controller cannot handle */
/* Therefore we need low level control of the i2c bus hardware */

/* Write to the MSP or DPL registers */
void
msp_dpl_write(bktr_ptr_t bktr, int i2c_addr,  unsigned char dev, unsigned int addr, unsigned int data)
{
	unsigned char addr_l, addr_h, data_h, data_l ;

	addr_h = (addr >>8) & 0xff;
	addr_l = addr & 0xff;
	data_h = (data >>8) & 0xff;
	data_l = data & 0xff;

	iicbus_start(IICBUS(bktr), i2c_addr, 0 /* no timeout? */);

	iicbus_write_byte(IICBUS(bktr), dev, 0);
	iicbus_write_byte(IICBUS(bktr), addr_h, 0);
	iicbus_write_byte(IICBUS(bktr), addr_l, 0);
	iicbus_write_byte(IICBUS(bktr), data_h, 0);
	iicbus_write_byte(IICBUS(bktr), data_l, 0);

	iicbus_stop(IICBUS(bktr));

	return;
}

/* Read from the MSP or DPL registers */
unsigned int
msp_dpl_read(bktr_ptr_t bktr, int i2c_addr, unsigned char dev, unsigned int addr)
{
	unsigned int data;
	unsigned char addr_l, addr_h, dev_r;
	int read;
	u_char data_read[2];

	addr_h = (addr >>8) & 0xff;
	addr_l = addr & 0xff;
	dev_r = dev+1;

	/* XXX errors ignored */
	iicbus_start(IICBUS(bktr), i2c_addr, 0 /* no timeout? */);

	iicbus_write_byte(IICBUS(bktr), dev_r, 0);
	iicbus_write_byte(IICBUS(bktr), addr_h, 0);
	iicbus_write_byte(IICBUS(bktr), addr_l, 0);

	iicbus_repeated_start(IICBUS(bktr), i2c_addr +1, 0 /* no timeout? */);
	iicbus_read(IICBUS(bktr), data_read, 2, &read, IIC_LAST_READ, 0);
	iicbus_stop(IICBUS(bktr));

	data = (data_read[0]<<8) | data_read[1];

	return (data);
}

/* Reset the MSP or DPL chip */
/* The user can block the reset (which is handy if you initialise the
 * MSP and/or DPL audio in another operating system first (eg in Windows)
 */
void
msp_dpl_reset( bktr_ptr_t bktr, int i2c_addr )
{

#ifndef BKTR_NO_MSP_RESET
	/* put into reset mode */
	iicbus_start(IICBUS(bktr), i2c_addr, 0 /* no timeout? */);
	iicbus_write_byte(IICBUS(bktr), 0x00, 0);
	iicbus_write_byte(IICBUS(bktr), 0x80, 0);
	iicbus_write_byte(IICBUS(bktr), 0x00, 0);
	iicbus_stop(IICBUS(bktr));

	/* put back to operational mode */
	iicbus_start(IICBUS(bktr), i2c_addr, 0 /* no timeout? */);
	iicbus_write_byte(IICBUS(bktr), 0x00, 0);
	iicbus_write_byte(IICBUS(bktr), 0x00, 0);
	iicbus_write_byte(IICBUS(bktr), 0x00, 0);
	iicbus_stop(IICBUS(bktr));
#endif
	return;
}

static void remote_read(bktr_ptr_t bktr, struct bktr_remote *remote) {
	int read;

	/* XXX errors ignored */
	iicbus_start(IICBUS(bktr), bktr->remote_control_addr, 0 /* no timeout? */);
	iicbus_read(IICBUS(bktr),  remote->data, 3, &read, IIC_LAST_READ, 0);
	iicbus_stop(IICBUS(bktr));

	return;
}

#else /* defined(BKTR_USE_FREEBSD_SMBUS) */

/*
 * Program the i2c bus directly
 */
int
i2cWrite( bktr_ptr_t bktr, int addr, int byte1, int byte2 )
{
	u_long		x;
	u_long		data;

	/* clear status bits */
	OUTL(bktr, BKTR_INT_STAT, BT848_INT_RACK | BT848_INT_I2CDONE);

	/* build the command datum */
	if (bktr->id == BROOKTREE_848  ||
	    bktr->id == BROOKTREE_848A ||
	    bktr->id == BROOKTREE_849A) {
	  data = ((addr & 0xff) << 24) | ((byte1 & 0xff) << 16) | I2C_COMMAND;
	} else {
	  data = ((addr & 0xff) << 24) | ((byte1 & 0xff) << 16) | I2C_COMMAND_878;
	}
	if ( byte2 != -1 ) {
		data |= ((byte2 & 0xff) << 8);
		data |= BT848_DATA_CTL_I2CW3B;
	}

	/* write the address and data */
	OUTL(bktr, BKTR_I2C_DATA_CTL, data);

	/* wait for completion */
	for ( x = 0x7fffffff; x; --x ) {	/* safety valve */
		if ( INL(bktr, BKTR_INT_STAT) & BT848_INT_I2CDONE )
			break;
	}

	/* check for ACK */
	if ( !x || !(INL(bktr, BKTR_INT_STAT) & BT848_INT_RACK) )
		return( -1 );

	/* return OK */
	return( 0 );
}


/*
 * 
 */
int
i2cRead( bktr_ptr_t bktr, int addr )
{
	u_long		x;

	/* clear status bits */
	OUTL(bktr, BKTR_INT_STAT, BT848_INT_RACK | BT848_INT_I2CDONE);

	/* write the READ address */
	/* The Bt878 and Bt879  differed on the treatment of i2c commands */
	   
	if (bktr->id == BROOKTREE_848  ||
	    bktr->id == BROOKTREE_848A ||
	    bktr->id == BROOKTREE_849A) {
		OUTL(bktr, BKTR_I2C_DATA_CTL, ((addr & 0xff) << 24) | I2C_COMMAND);
	} else {
		OUTL(bktr, BKTR_I2C_DATA_CTL, ((addr & 0xff) << 24) | I2C_COMMAND_878);
	}

	/* wait for completion */
	for ( x = 0x7fffffff; x; --x ) {	/* safety valve */
		if ( INL(bktr, BKTR_INT_STAT) & BT848_INT_I2CDONE )
			break;
	}

	/* check for ACK */
	if ( !x || !(INL(bktr, BKTR_INT_STAT) & BT848_INT_RACK) )
		return( -1 );

	/* it was a read */
	return( (INL(bktr, BKTR_I2C_DATA_CTL) >> 8) & 0xff );
}

/* The MSP34xx Audio chip require i2c bus writes of up to 5 bytes which the */
/* bt848 automated i2c bus controller cannot handle */
/* Therefore we need low level control of the i2c bus hardware */
/* Idea for the following functions are from elsewhere in this driver and */
/* from the Linux BTTV i2c driver by Gerd Knorr <kraxel@cs.tu-berlin.de> */

#define BITD    40
static void i2c_start( bktr_ptr_t bktr) {
        OUTL(bktr, BKTR_I2C_DATA_CTL, 1); DELAY( BITD ); /* release data */
        OUTL(bktr, BKTR_I2C_DATA_CTL, 3); DELAY( BITD ); /* release clock */
        OUTL(bktr, BKTR_I2C_DATA_CTL, 2); DELAY( BITD ); /* lower data */
        OUTL(bktr, BKTR_I2C_DATA_CTL, 0); DELAY( BITD ); /* lower clock */
}

static void i2c_stop( bktr_ptr_t bktr) {
        OUTL(bktr, BKTR_I2C_DATA_CTL, 0); DELAY( BITD ); /* lower clock & data */
        OUTL(bktr, BKTR_I2C_DATA_CTL, 2); DELAY( BITD ); /* release clock */
        OUTL(bktr, BKTR_I2C_DATA_CTL, 3); DELAY( BITD ); /* release data */
}

static int i2c_write_byte( bktr_ptr_t bktr, unsigned char data) {
        int x;
        int status;

        /* write out the byte */
        for ( x = 7; x >= 0; --x ) {
                if ( data & (1<<x) ) {
			OUTL(bktr, BKTR_I2C_DATA_CTL, 1);
                        DELAY( BITD );          /* assert HI data */
			OUTL(bktr, BKTR_I2C_DATA_CTL, 3);
                        DELAY( BITD );          /* strobe clock */
			OUTL(bktr, BKTR_I2C_DATA_CTL, 1);
                        DELAY( BITD );          /* release clock */
                }
                else {
			OUTL(bktr, BKTR_I2C_DATA_CTL, 0);
                        DELAY( BITD );          /* assert LO data */
			OUTL(bktr, BKTR_I2C_DATA_CTL, 2);
                        DELAY( BITD );          /* strobe clock */
			OUTL(bktr, BKTR_I2C_DATA_CTL, 0);
                        DELAY( BITD );          /* release clock */
                }
        }

        /* look for an ACK */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 1); DELAY( BITD ); /* float data */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 3); DELAY( BITD ); /* strobe clock */
        status = INL(bktr, BKTR_I2C_DATA_CTL) & 1;       /* read the ACK bit */
        OUTL(bktr, BKTR_I2C_DATA_CTL, 1); DELAY( BITD ); /* release clock */

        return( status );
}

static int i2c_read_byte( bktr_ptr_t bktr, unsigned char *data, int last ) {
        int x;
        int bit;
        int byte = 0;

        /* read in the byte */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 1);
        DELAY( BITD );                          /* float data */
        for ( x = 7; x >= 0; --x ) {
		OUTL(bktr, BKTR_I2C_DATA_CTL, 3);
                DELAY( BITD );                  /* strobe clock */
                bit = INL(bktr, BKTR_I2C_DATA_CTL) & 1;  /* read the data bit */
                if ( bit ) byte |= (1<<x);
		OUTL(bktr, BKTR_I2C_DATA_CTL, 1);
                DELAY( BITD );                  /* release clock */
        }
        /* After reading the byte, send an ACK */
        /* (unless that was the last byte, for which we send a NAK */
        if (last) { /* send NAK - same a writing a 1 */
		OUTL(bktr, BKTR_I2C_DATA_CTL, 1);
                DELAY( BITD );                  /* set data bit */
		OUTL(bktr, BKTR_I2C_DATA_CTL, 3);
                DELAY( BITD );                  /* strobe clock */
		OUTL(bktr, BKTR_I2C_DATA_CTL, 1);
                DELAY( BITD );                  /* release clock */
        } else { /* send ACK - same as writing a 0 */
		OUTL(bktr, BKTR_I2C_DATA_CTL, 0);
                DELAY( BITD );                  /* set data bit */
		OUTL(bktr, BKTR_I2C_DATA_CTL, 2);
                DELAY( BITD );                  /* strobe clock */
		OUTL(bktr, BKTR_I2C_DATA_CTL, 0);
                DELAY( BITD );                  /* release clock */
        }

        *data=byte;
	return 0;
}
#undef BITD

/* Write to the MSP or DPL registers */
void msp_dpl_write( bktr_ptr_t bktr, int i2c_addr, unsigned char dev, unsigned int addr,
		    unsigned int data){
	unsigned int msp_w_addr = i2c_addr;
	unsigned char addr_l, addr_h, data_h, data_l ;
	addr_h = (addr >>8) & 0xff;
	addr_l = addr & 0xff;
	data_h = (data >>8) & 0xff;
	data_l = data & 0xff;

	i2c_start(bktr);
	i2c_write_byte(bktr, msp_w_addr);
	i2c_write_byte(bktr, dev);
	i2c_write_byte(bktr, addr_h);
	i2c_write_byte(bktr, addr_l);
	i2c_write_byte(bktr, data_h);
	i2c_write_byte(bktr, data_l);
	i2c_stop(bktr);
}

/* Read from the MSP or DPL registers */
unsigned int msp_dpl_read(bktr_ptr_t bktr, int i2c_addr, unsigned char dev, unsigned int addr){
	unsigned int data;
	unsigned char addr_l, addr_h, data_1, data_2, dev_r ;
	addr_h = (addr >>8) & 0xff;
	addr_l = addr & 0xff;
	dev_r = dev+1;

	i2c_start(bktr);
	i2c_write_byte(bktr,i2c_addr);
	i2c_write_byte(bktr,dev_r);
	i2c_write_byte(bktr,addr_h);
	i2c_write_byte(bktr,addr_l);

	i2c_start(bktr);
	i2c_write_byte(bktr,i2c_addr+1);
	i2c_read_byte(bktr,&data_1, 0);
	i2c_read_byte(bktr,&data_2, 1);
	i2c_stop(bktr);
	data = (data_1<<8) | data_2;
	return data;
}

/* Reset the MSP or DPL chip */
/* The user can block the reset (which is handy if you initialise the
 * MSP audio in another operating system first (eg in Windows)
 */
void msp_dpl_reset( bktr_ptr_t bktr, int i2c_addr ) {

#ifndef BKTR_NO_MSP_RESET
	/* put into reset mode */
	i2c_start(bktr);
	i2c_write_byte(bktr, i2c_addr);
	i2c_write_byte(bktr, 0x00);
	i2c_write_byte(bktr, 0x80);
	i2c_write_byte(bktr, 0x00);
	i2c_stop(bktr);

	/* put back to operational mode */
	i2c_start(bktr);
	i2c_write_byte(bktr, i2c_addr);
	i2c_write_byte(bktr, 0x00);
	i2c_write_byte(bktr, 0x00);
	i2c_write_byte(bktr, 0x00);
	i2c_stop(bktr);
#endif
	return;

}

static void remote_read(bktr_ptr_t bktr, struct bktr_remote *remote) {

	/* XXX errors ignored */
	i2c_start(bktr);
	i2c_write_byte(bktr,bktr->remote_control_addr);
	i2c_read_byte(bktr,&(remote->data[0]), 0);
	i2c_read_byte(bktr,&(remote->data[1]), 0);
	i2c_read_byte(bktr,&(remote->data[2]), 0);
	i2c_stop(bktr);

	return;
}

#endif /* defined(BKTR_USE_FREEBSD_SMBUS) */


#if defined( I2C_SOFTWARE_PROBE )

/*
 * we are keeping this around for any parts that we need to probe
 * but that CANNOT be probed via an i2c read.
 * this is necessary because the hardware i2c mechanism
 * cannot be programmed for 1 byte writes.
 * currently there are no known i2c parts that we need to probe
 * and that cannot be safely read.
 */
static int	i2cProbe( bktr_ptr_t bktr, int addr );
#define BITD		40
#define EXTRA_START

/*
 * probe for an I2C device at addr.
 */
static int
i2cProbe( bktr_ptr_t bktr, int addr )
{
	int		x, status;

	/* the START */
#if defined( EXTRA_START )
	OUTL(bktr, BKTR_I2C_DATA_CTL, 1); DELAY( BITD );	/* release data */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 3); DELAY( BITD );	/* release clock */
#endif /* EXTRA_START */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 2); DELAY( BITD );	/* lower data */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 0); DELAY( BITD );	/* lower clock */

	/* write addr */
	for ( x = 7; x >= 0; --x ) {
		if ( addr & (1<<x) ) {
			OUTL(bktr, BKTR_I2C_DATA_CTL, 1);
			DELAY( BITD );		/* assert HI data */
			OUTL(bktr, BKTR_I2C_DATA_CTL, 3);
			DELAY( BITD );		/* strobe clock */
			OUTL(bktr, BKTR_I2C_DATA_CTL, 1);
			DELAY( BITD );		/* release clock */
		}
		else {
			OUTL(bktr, BKTR_I2C_DATA_CTL, 0);
			DELAY( BITD );		/* assert LO data */
			OUTL(bktr, BKTR_I2C_DATA_CTL, 2);
			DELAY( BITD );		/* strobe clock */
			OUTL(bktr, BKTR_I2C_DATA_CTL, 0);
			DELAY( BITD );		/* release clock */
		}
	}

	/* look for an ACK */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 1); DELAY( BITD );	/* float data */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 3); DELAY( BITD );	/* strobe clock */
	status = INL(bktr, BKTR_I2C_DATA_CTL) & 1;	/* read the ACK bit */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 1); DELAY( BITD );	/* release clock */

	/* the STOP */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 0); DELAY( BITD );	/* lower clock & data */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 2); DELAY( BITD );	/* release clock */
	OUTL(bktr, BKTR_I2C_DATA_CTL, 3); DELAY( BITD );	/* release data */

	return( status );
}
#undef EXTRA_START
#undef BITD

#endif /* I2C_SOFTWARE_PROBE */


#define ABSENT		(-1)

#endif /* FreeBSD, BSDI, NetBSD, OpenBSD */

