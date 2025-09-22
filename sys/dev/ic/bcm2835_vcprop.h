/*     $OpenBSD: bcm2835_vcprop.h,v 1.3 2025/08/26 15:29:11 kettenis Exp $ */

/*
 * Copyright (c) 2020 Tobias Heider <tobhe@openbsd.org>
 * Copyright (c) 2019 Neil Ashford <ashfordneil0@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nick Hudson
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Mailbox property interface
 */

#ifndef BCM2835_VCPROP_H
#define BCM2835_VCPROP_H

struct vcprop_tag {
	uint32_t vpt_tag;
#define	VCPROPTAG_NULL			0x00000000
#define	VCPROPTAG_GET_FIRMWAREREV	0x00000001
#define	VCPROPTAG_GET_BOARDMODEL	0x00010001
#define	VCPROPTAG_GET_BOARDREVISION	0x00010002
#define	VCPROPTAG_GET_MACADDRESS	0x00010003
#define	VCPROPTAG_GET_BOARDSERIAL	0x00010004
#define	VCPROPTAG_GET_ARMMEMORY		0x00010005
#define	VCPROPTAG_GET_VCMEMORY		0x00010006
#define	VCPROPTAG_GET_CLOCKS		0x00010007

#define VCPROPTAG_GET_POWERSTATE	0x00020001
#define VCPROPTAG_GET_POWERTIMING	0x00020002
#define VCPROPTAG_SET_POWERSTATE	0x00028001

#define	VCPROPTAG_GET_CLOCKSTATE	0x00030001
#define	VCPROPTAG_SET_CLOCKSTATE	0x00038001
#define	VCPROPTAG_GET_CLOCKRATE		0x00030002
#define	VCPROPTAG_SET_CLOCKRATE		0x00038002
#define	VCPROPTAG_GET_MIN_CLOCKRATE	0x00030007
#define	VCPROPTAG_GET_MAX_CLOCKRATE	0x00030004
#define	VCPROPTAG_GET_TURBO		0x00030009
#define	VCPROPTAG_SET_TURBO		0x00038009

#define VCPROPTAG_GET_VOLTAGE		0x00030003
#define VCPROPTAG_SET_VOLTAGE		0x00038003
#define VCPROPTAG_GET_MIN_VOLTAGE	0x00030008
#define VCPROPTAG_GET_MAX_VOLTAGE	0x00030005

#define VCPROPTAG_GET_TEMPERATURE	0x00030006
#define VCPROPTAG_GET_MAX_TEMPERATURE	0x0003000a

#define VCPROPTAG_NOTIFY_XHCI_RESET	0x00030058

#define VCPROPTAG_GET_RTC_REG		0x00030087
#define VCPROPTAG_SET_RTC_REG		0x00038087

#define	VCPROPTAG_GET_CMDLINE		0x00050001
#define	VCPROPTAG_GET_DMACHAN		0x00060001

#define	VCPROPTAG_ALLOCATE_BUFFER	0x00040001
#define	VCPROPTAG_BLANK_SCREEN		0x00040002
#define	VCPROPTAG_GET_FB_RES		0x00040003
#define	VCPROPTAG_SET_FB_RES		0x00048003
#define	VCPROPTAG_GET_FB_VRES		0x00040004
#define	VCPROPTAG_SET_FB_VRES		0x00048004
#define	VCPROPTAG_GET_FB_DEPTH		0x00040005
#define	VCPROPTAG_SET_FB_DEPTH		0x00048005
#define	VCPROPTAG_GET_FB_PIXEL_ORDER	0x00040006
#define	VCPROPTAG_SET_FB_PIXEL_ORDER	0x00048006
#define	VCPROPTAG_GET_FB_ALPHA_MODE	0x00040007
#define	VCPROPTAG_SET_FB_ALPHA_MODE	0x00048007
#define	VCPROPTAG_GET_FB_PITCH		0x00040008

#define	VCPROPTAG_GET_EDID_BLOCK	0x00030020

#define	VCPROPTAG_ALLOCMEM		0x0003000c
#define	VCPROPTAG_LOCKMEM		0x0003000d
#define	VCPROPTAG_UNLOCKMEM		0x0003000e
#define	VCPROPTAG_RELEASEMEM		0x0003000f
#define	VCPROPTAG_EXECUTE_CODE		0x00030010
#define	VCPROPTAG_EXECUTE_QPU		0x00030011
#define	VCPROPTAG_SET_ENABLE_QPU	0x00030012
#define	VCPROPTAG_GET_DISPMANX_HANDLE	0x00030014

#define	VCPROPTAG_SET_CURSOR_INFO	0x00008010
#define	VCPROPTAG_SET_CURSOR_STATE	0x00008011

	uint32_t vpt_len;
	uint32_t vpt_rcode;
#define	VCPROPTAG_REQUEST	(0U << 31)
#define	VCPROPTAG_RESPONSE	(1U << 31)

} __packed;

#define VCPROPTAG_LEN(x) (sizeof((x)) - sizeof(struct vcprop_tag))

struct vcprop_memory {
	uint32_t base;
	uint32_t size;
};

#define	VCPROP_MAXMEMBLOCKS 4
struct vcprop_tag_memory {
	struct vcprop_tag tag;
	struct vcprop_memory mem[VCPROP_MAXMEMBLOCKS];
};

struct vcprop_tag_fwrev {
	struct vcprop_tag tag;
	uint32_t rev;
};

struct vcprop_tag_boardmodel {
	struct vcprop_tag tag;
	uint32_t model;
};

struct vcprop_tag_boardrev {
	struct vcprop_tag tag;
	uint32_t rev;
};
#define	VCPROP_REV_PCBREV	15
#define	VCPROP_REV_MODEL	(255 << 4)
#define	 RPI_MODEL_A		0
#define	 RPI_MODEL_B		1
#define	 RPI_MODEL_A_PLUS	2
#define	 RPI_MODEL_B_PLUS	3
#define	 RPI_MODEL_B_PI2	4
#define	 RPI_MODEL_ALPHA	5
#define	 RPI_MODEL_COMPUTE	6
#define	 RPI_MODEL_ZERO		7
#define	 RPI_MODEL_B_PI3	8
#define	 RPI_MODEL_COMPUTE_PI3	9
#define	 RPI_MODEL_ZERO_W	10
#define	VCPROP_REV_PROCESSOR	(15 << 12)
#define	 RPI_PROCESSOR_BCM2835	0
#define	 RPI_PROCESSOR_BCM2836	1
#define	 RPI_PROCESSOR_BCM2837	2
#define	VCPROP_REV_MANUF	(15 << 16)
#define	VCPROP_REV_MEMSIZE	(7 << 20)
#define	VCPROP_REV_ENCFLAG	(1 << 23)
#define	VCPROP_REV_WARRANTY	(3 << 24)

struct vcprop_tag_macaddr {
	struct vcprop_tag tag;
	uint64_t addr;
} __packed;

struct vcprop_tag_boardserial {
	struct vcprop_tag tag;
	uint64_t sn;
} __packed;

#define	VCPROP_CLK_EMMC		1
#define	VCPROP_CLK_UART		2
#define	VCPROP_CLK_ARM		3
#define	VCPROP_CLK_CORE		4
#define	VCPROP_CLK_V3D		5
#define	VCPROP_CLK_H264		6
#define	VCPROP_CLK_ISP		7
#define	VCPROP_CLK_SDRAM	8
#define	VCPROP_CLK_PIXEL	9
#define	VCPROP_CLK_PWM		10
#define	VCPROP_CLK_EMMC2	12

struct vcprop_clock {
	uint32_t pclk;
	uint32_t cclk;
};

#define	VCPROP_MAXCLOCKS 16
struct vcprop_tag_clock {
	struct vcprop_tag tag;
	struct vcprop_clock clk[VCPROP_MAXCLOCKS];
};

#ifndef	VCPROP_MAXCMDLINE
#define	VCPROP_MAXCMDLINE 1024
#endif
struct vcprop_tag_cmdline {
	struct vcprop_tag tag;
	uint8_t cmdline[VCPROP_MAXCMDLINE];
};

struct vcprop_tag_dmachan {
	struct vcprop_tag tag;
	uint32_t mask;
};

struct vcprop_tag_clockstate {
	struct vcprop_tag tag;
	uint32_t id;
	uint32_t state;
};

struct vcprop_tag_clockrate {
	struct vcprop_tag tag;
	uint32_t id;
	uint32_t rate;
	uint32_t noturbo;
} __packed;

#define VCPROP_VOLTAGE_CORE	1
#define VCPROP_VOLTAGE_SDRAM_C	2
#define VCPROP_VOLTAGE_SDRAM_P	3
#define VCPROP_VOLTAGE_SDRAM_I	4

struct vcprop_tag_voltage {
	struct vcprop_tag tag;
	uint32_t id;
	uint32_t value;
};

#define VCPROP_TEMP_SOC		0

struct vcprop_tag_temperature {
	struct vcprop_tag tag;
	uint32_t id;
	uint32_t value;
};

#define VCPROP_RTC_TIME			0
#define VCPROP_RTC_ALARM		1
#define VCPROP_RTC_ALARM_PENDING	2
#define VCPROP_RTC_ALARM_ENABLE		3
#define VCPROP_RTC_BBAT_CHG_VOLTS	4
#define VCPROP_RTC_BBAT_CHG_VOLTS_MIN	5
#define VCPROP_RTC_BBAT_CHG_VOLTS_MAX	6
#define VCPROP_RTC_BBAT_VOLTS		7

struct vcprop_tag_rtc {
	struct vcprop_tag tag;
	uint32_t id;
	uint32_t data;
};

#define	VCPROP_POWER_SDCARD	0
#define	VCPROP_POWER_UART0	1
#define	VCPROP_POWER_UART1	2
#define	VCPROP_POWER_USB	3
#define	VCPROP_POWER_I2C0	4
#define	VCPROP_POWER_I2C1	5
#define	VCPROP_POWER_I2C2	6
#define	VCPROP_POWER_SPI	7
#define	VCPROP_POWER_CCP2TX	8

struct vcprop_tag_powertiming {
	struct vcprop_tag tag;
	uint32_t id;
	uint32_t waitusec;
};

struct vcprop_tag_powerstate {
	struct vcprop_tag tag;
	uint32_t id;
	uint32_t state;
};

struct vcprop_tag_allocbuf {
	struct vcprop_tag tag;
	uint32_t address;	/* alignment for request */
	uint32_t size;
};

#define VCPROP_BLANK_OFF	0
#define VCPROP_BLANK_ON		1

struct vcprop_tag_blankscreen {
	struct vcprop_tag tag;
	uint32_t state;
};

struct vcprop_tag_fbres {
	struct vcprop_tag tag;
	uint32_t width;
	uint32_t height;
};

struct vcprop_tag_fbdepth {
	struct vcprop_tag tag;
	uint32_t bpp;
};

#define VCPROP_PIXEL_BGR	0
#define VCPROP_PIXEL_RGB	1

struct vcprop_tag_fbpixelorder {
	struct vcprop_tag tag;
	uint32_t state;
};

struct vcprop_tag_fbpitch {
	struct vcprop_tag tag;
	uint32_t linebytes;
};

#define VCPROP_ALPHA_ENABLED	0
#define VCPROP_ALPHA_REVERSED	1
#define VCPROP_ALPHA_IGNORED	2

struct vcprop_tag_fbalpha {
	struct vcprop_tag tag;
	uint32_t state;
};

struct vcprop_tag_edidblock {
	struct vcprop_tag tag;
	uint32_t blockno;
	uint32_t status;
	uint8_t data[128];
};

struct vcprop_tag_cursorinfo {
	struct vcprop_tag tag;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint32_t pixels;	/* bus address in VC memory */
	uint32_t hotspot_x;
	uint32_t hotspot_y;
};

struct vcprop_tag_cursorstate {
	struct vcprop_tag tag;
	uint32_t enable;	/* 1 - visible */
	uint32_t x;
	uint32_t y;
	uint32_t flags;		/* 0 - display coord. 1 - fb coord. */
};

struct vcprop_tag_allocmem {
	struct vcprop_tag tag;
	uint32_t size;	/* handle returned here */
	uint32_t align;
	uint32_t flags;
/*
 * flag definitions from
 * https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
 */
#define MEM_FLAG_DISCARDABLE	(1 << 0) /* can be resized to 0 at any time. Use for cached data */
#define MEM_FLAG_NORMAL		(0 << 2) /* normal allocating alias. Don't use from ARM */
#define MEM_FLAG_DIRECT		(1 << 2) /* 0xC alias uncached */
#define MEM_FLAG_COHERENT	(2 << 2) /* 0x8 alias. Non-allocating in L2 but coherent */
#define MEM_FLAG_L1_NONALLOCATING (MEM_FLAG_DIRECT | MEM_FLAG_COHERENT) /* Allocating in L2 */
#define MEM_FLAG_ZERO		(1 << 4)  /* initialise buffer to all zeros */
#define MEM_FLAG_NO_INIT	(1 << 5) /* don't initialise (default is initialise to all ones */
#define MEM_FLAG_HINT_PERMALOCK	(1 << 6) /* Likely to be locked for long periods of time. */
};

/* also for unlock and release */
struct vcprop_tag_lockmem {
	struct vcprop_tag tag;
	uint32_t handle;	/* bus address returned here */
};

struct vcprop_tag_notifyxhcireset {
	struct vcprop_tag tag;
	uint32_t deviceaddress;
};

struct vcprop_buffer_hdr {
	uint32_t vpb_len;
	uint32_t vpb_rcode;
#define	VCPROP_PROCESS_REQUEST 0
#define VCPROP_REQ_SUCCESS	(1U << 31)
#define VCPROP_REQ_EPARSE	(1U << 0)
} __packed;

static inline bool
vcprop_buffer_success_p(struct vcprop_buffer_hdr *vpbh)
{
	return (vpbh->vpb_rcode & VCPROP_REQ_SUCCESS);
}

static inline bool
vcprop_tag_success_p(struct vcprop_tag *vpbt)
{
	return (vpbt->vpt_rcode & VCPROPTAG_RESPONSE);
}

static inline size_t
vcprop_tag_resplen(struct vcprop_tag *vpbt)
{
	return (vpbt->vpt_rcode & ~VCPROPTAG_RESPONSE);
}

#define BCMMBOX_CHANPM 0
#define BCMMBOX_CHANFB 1 /* will be deprecated */
#define BCMMBOX_CHANVUART 2
#define BCMMBOX_CHANVCHIQ 3
#define BCMMBOX_CHANLEDS 4
#define BCMMBOX_CHANBUTTONS 5
#define BCMMBOX_CHANTOUCHSCR 6
#define BCMMBOX_CHANARM2VC 8
#define BCMMBOX_CHANVC2ARM 9

#endif /* BCM2835_VCPROP_H */
