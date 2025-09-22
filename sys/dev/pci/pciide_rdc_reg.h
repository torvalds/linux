/*      $OpenBSD: pciide_rdc_reg.h,v 1.1 2014/07/13 23:19:51 sasano Exp $    */
/*      $NetBSD: rdcide_reg.h,v 1.1 2011/04/04 14:33:51 bouyer Exp $    */

/*
 * Copyright (c) 2011 Manuel Bouyer.
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
 *
 */

/*
 * register definitions for the RDC ide controller as found in the
 * PMX-1000 SoC
 */
/* ATA Timing Register */
#define RDCIDE_PATR 0x40
#define RDCIDE_PATR_EN(chan)		(0x8000 << ((chan) * 16))
#define RDCIDE_PATR_DEV1_TEN(chan)	(0x4000 << ((chan) * 16))
#define RDCIDE_PATR_SETUP(val, chan)	(((val) << 12) << ((chan) * 16))
#define RDCIDE_PATR_SETUP_MASK(chan)	(0x3000 << ((chan) * 16))
#define RDCIDE_PATR_HOLD(val, chan)	(((val) << 8) << ((chan) * 16))
#define RDCIDE_PATR_HOLD_MASK(chan)	(0x0300 << ((chan) * 16))
#define RDCIDE_PATR_DMAEN(chan, drv)	((0x0008 << (drv * 4)) << ((chan) * 16))
#define RDCIDE_PATR_ATA(chan, drv)	((0x0004 << (drv * 4)) << ((chan) * 16))
#define RDCIDE_PATR_IORDY(chan, drv)	((0x0002 << (drv * 4)) << ((chan) * 16))
#define RDCIDE_PATR_FTIM(chan, drv)	((0x0001 << (drv * 4)) << ((chan) * 16))

/* Primary and Secondary Device 1 ATA Timing */
#define RDCIDE_PSD1ATR 0x44
#define RDCIDE_PSD1ATR_SETUP(val, chan)	(((val) << 2) << (chan * 4))
#define RDCIDE_PSD1ATR_SETUP_MASK(chan)	(0x0c << (chan * 4))
#define RDCIDE_PSD1ATR_HOLD(val, chan)	(((val) << 0) << (chan * 4))
#define RDCIDE_PSD1ATR_HOLD_MASK(chan)	(0x03 << (chan * 4))

const uint8_t rdcide_setup[] = {0, 0, 1, 2, 2};
const uint8_t rdcide_hold[] = {0, 0, 0, 1, 3};

/* Ultra DMA Control and timing Register */
#define RDCIDE_UDCCR	0x48
#define RDCIDE_UDCCR_EN(chan, drv)	((1 << (drv)) << (chan * 2))
#define RDCIDE_UDCCR_TIM(val, chan, drv) (((val) << ((drv) * 4)) << (chan * 8))
#define RDCIDE_UDCCR_TIM_MASK(chan, drv) ((0x3 << ((drv) * 4)) << (chan * 8))

const uint8_t rdcide_udmatim[] = {0, 1, 2, 1, 2, 1};

/* IDE I/O Configuration Registers */
#define RDCIDE_IIOCR	0x54
#define RDCIDE_IIOCR_CABLE(chan, drv)	((0x10 << (drv)) << (chan * 2))
#define RDCIDE_IIOCR_CLK(val, chan, drv) (((val) << drv) << (chan * 2))
#define RDCIDE_IIOCR_CLK_MASK(chan, drv) ((0x1001 << drv) << (chan * 2))

const uint32_t rdcide_udmaclk[] =
    {0x0000, 0x0000, 0x0000, 0x0001, 0x0001, 0x1000};

/* Miscellaneous Control Register */
#define RDCIDE_MCR	0x90
#define RDCIDE_MCR_RESET(chan)	(0x01000000 << (chan))
