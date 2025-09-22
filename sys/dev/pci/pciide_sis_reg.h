/*	$OpenBSD: pciide_sis_reg.h,v 1.8 2010/07/23 07:47:13 jsg Exp $	*/
/*	$NetBSD: pciide_sis_reg.h,v 1.6 2000/05/15 08:46:01 bouyer Exp $	*/

/*
 * Copyright (c) 1998 Manuel Bouyer.
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

#ifndef _DEV_PCI_PCIIDE_SIS_REG_H_
#define _DEV_PCI_PCIIDE_SIS_REG_H_

/*
 * Registers definitions for SiS SiS5597/98 PCI IDE controller.
 * Available from http://www.sis.com.tw/html/databook.html
 */

/* IDE timing control registers (32 bits), for all but 96x */
#define SIS_TIM(channel) (0x40 + (channel * 4))
/* for 730, 630 and older (66, 100OLD) */
#define SIS_TIM66_REC_OFF(drive) (16 * (drive))
#define SIS_TIM66_ACT_OFF(drive) (8 + 16 * (drive))
#define SIS_TIM66_UDMA_TIME_OFF(drive) (12 + 16 * (drive))
/* for older than 96x (100NEW, 133OLD) */
#define SIS_TIM100_REC_OFF(drive) (16 * (drive))
#define SIS_TIM100_ACT_OFF(drive) (4 + 16 * (drive))
#define SIS_TIM100_UDMA_TIME_OFF(drive) (8 + 16 * (drive))

/*
 * From FreeBSD: on 96x, the timing registers may start from 0x40 or 0x70
 * depending on the value from register 0x57. 32bits of timing info for
 * each drive.
 */
#define SIS_TIM133(reg57, channel, drive) \
    ((((reg57) & 0x40) ? 0x70 : 0x40) + ((channel) << 3) + ((drive) << 2))

/* IDE general control register 0 (8 bits) */
#define SIS_CTRL0 0x4a
#define SIS_CTRL0_PCIBURST	0x80
#define SIS_CTRL0_FAST_PW	0x20
#define SIS_CTRL0_BO		0x08
#define SIS_CTRL0_CHAN0_EN	0x02 /* manual (v2.0) is wrong!!! */
#define SIS_CTRL0_CHAN1_EN	0x04 /* manual (v2.0) is wrong!!! */

/* IDE general control register 1 (8 bits) */
#define SIS_CTRL1 0x4b
#define SIS_CTRL1_POSTW_EN(chan, drv) (0x10 << ((drv) + 2 * (chan)))
#define SIS_CTRL1_PREFETCH_EN(chan, drv) (0x01 << ((drv) + 2 * (chan)))

/* IDE misc control register (8 bit) */
#define SIS_MISC 0x52
#define SIS_MISC_TIM_SEL	0x08
#define SIS_MISC_GTC		0x04
#define SIS_MISC_FIFO_SIZE	0x01

/* following are from FreeBSD (sorry, no description) */
#define SIS_REG_49	0x49
#define SIS_REG_50	0x50
#define SIS_REG_51	0x51
#define SIS_REG_52	0x52
#define SIS_REG_53	0x53
#define SIS_REG_57	0x57

#define SIS_REG_CBL 0x48
#define SIS_REG_CBL_33(channel) (0x10 << (channel))
#define SIS96x_REG_CBL(channel) (0x51 + (channel) * 2)
#define SIS96x_REG_CBL_33 0x80

#define SIS_PRODUCT_5518 0x5518

/* Private data */
struct pciide_sis {
	u_int8_t sis_type;
};

/* timings values, mostly from FreeBSD */
/* PIO timings, for all up to 133NEW */
static const u_int8_t sis_pio_act[] =
    {12, 6, 4, 3, 3};
static const u_int8_t sis_pio_rec[] =
    {11, 7, 4, 3, 1};
/* DMA timings for 66 and 100OLD */
static const u_int8_t sis_udma66_tim[] =
    {15, 13, 11, 10, 9, 8};
/* DMA timings for 100NEW */
static const u_int8_t sis_udma100new_tim[] =
    {0x8b, 0x87, 0x85, 0x84, 0x82, 0x81};
/* DMA timings for 133OLD */
static const u_int8_t sis_udma133old_tim[] =
    {0x8f, 0x8a, 0x87, 0x85, 0x83, 0x82, 0x81};
/* PIO, DMA and UDMA timings for 133NEW */
static const u_int32_t sis_pio133new_tim[] =
    {0x28269008, 0x0c266008, 0x4263008, 0x0c0a3008, 0x05093008};
static const u_int32_t sis_dma133new_tim[] =
    {0x22196008, 0x0c0a3008, 0x05093008};
static const u_int32_t sis_udma133new_tim[] =
    {0x9f4, 0x64a, 0x474, 0x254, 0x234, 0x224, 0x214};

#endif	/* !_DEV_PCI_PCIIDE_SIS_REG_H_ */
