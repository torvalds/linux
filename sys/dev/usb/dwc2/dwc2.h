/*	$OpenBSD: dwc2.h,v 1.16 2022/09/04 08:42:39 mglocker Exp $	*/
/*	$NetBSD: dwc2.h,v 1.4 2014/12/23 16:20:06 macallan Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
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

#ifndef _EXTERNAL_BSD_DWC2_DWC2_H_
#define _EXTERNAL_BSD_DWC2_DWC2_H_

#include <sys/param.h>
#include <sys/kernel.h>

#include <sys/task.h>

#include <lib/libkern/libkern.h>

#define STATIC

// #define DWC2_DEBUG
// #define VERBOSE_DEBUG
// #define CONFIG_USB_DWC2_TRACK_MISSED_SOFS

#define CONFIG_USB_DWC2_HOST		1
#define CONFIG_USB_DWC2_DUAL_ROLE	0
#define CONFIG_USB_DWC2_PERIPHERAL	0

typedef int irqreturn_t;
#define	IRQ_NONE 0
#define IRQ_HANDLED 1

#define	u8	uint8_t
#define	u16	uint16_t
#define	s16	int16_t
#define	u32	uint32_t
#define	u64	uint64_t

#define	dma_addr_t	bus_addr_t

#ifdef DWC2_DEBUG
extern int dwc2debug;
#define	dev_info(d,fmt,...) do {			\
	printf("%s: " fmt, device_xname(d), 		\
	    ## __VA_ARGS__);				\
} while (0)
#define	dev_warn(d,fmt,...) do {			\
	printf("%s: " fmt, device_xname(d), 		\
	    ## __VA_ARGS__);				\
} while (0)
#define	dev_err(d,fmt,...) do {				\
	printf("%s: " fmt, device_xname(d), 		\
	    ## __VA_ARGS__);				\
} while (0)
#define	dev_dbg(d,fmt,...) do {				\
	if (dwc2debug >= 1) {				\
	    printf("%s: " fmt, device_xname(d), 	\
		    ## __VA_ARGS__);			\
	}						\
} while (0)
#define	dev_vdbg(d,fmt,...) do {			\
	if (dwc2debug >= 2) {				\
	    printf("%s: " fmt, device_xname(d), 	\
		    ## __VA_ARGS__);			\
	}						\
} while (0)
#else
#define	dev_info(...) do { } while (0)
#define	dev_warn(...) do { } while (0)
#define	dev_err(...) do { } while (0)
#define	dev_dbg(...) do { } while (0)
#define	dev_vdbg(...) do { } while (0)
#endif

enum usb_otg_state {
	OTG_STATE_RESERVED = 0,

	OTG_STATE_A_HOST,
	OTG_STATE_A_PERIPHERAL,
	OTG_STATE_A_SUSPEND,
	OTG_STATE_B_HOST,
	OTG_STATE_B_PERIPHERAL,
};

#define spinlock_t		struct mutex
#define spin_lock_init(lock)	mtx_init(lock, IPL_VM)
#define spin_lock(l)		do { mtx_enter(l); } while (0)
#define spin_unlock(l)		do { mtx_leave(l); } while (0)

#define	spin_lock_irqsave(l, f)		\
	do { mtx_enter(l); (void)(f); } while (0)

#define	spin_trylock_irqsave(l, f)	mtx_enter_try(l)

#define	spin_unlock_irqrestore(l, f)	\
	do { mtx_leave(l); (void)(f); } while (0)

#define	IRQ_RETVAL(r)	(r)

#define	USB_ENDPOINT_XFER_CONTROL	UE_CONTROL		/* 0 */
#define	USB_ENDPOINT_XFER_ISOC		UE_ISOCHRONOUS		/* 1 */
#define	USB_ENDPOINT_XFER_BULK		UE_BULK			/* 2 */
#define	USB_ENDPOINT_XFER_INT		UE_INTERRUPT		/* 3 */

#define USB_DIR_IN			UE_DIR_IN
#define USB_DIR_OUT			UE_DIR_OUT

#define	USB_PORT_FEAT_CONNECTION	UHF_PORT_CONNECTION
#define	USB_PORT_FEAT_ENABLE		UHF_PORT_ENABLE
#define	USB_PORT_FEAT_SUSPEND		UHF_PORT_SUSPEND
#define	USB_PORT_FEAT_OVER_CURRENT	UHF_PORT_OVER_CURRENT
#define	USB_PORT_FEAT_RESET		UHF_PORT_RESET
// #define	USB_PORT_FEAT_L1		5	/* L1 suspend */
#define	USB_PORT_FEAT_POWER		UHF_PORT_POWER
#define	USB_PORT_FEAT_LOWSPEED		UHF_PORT_LOW_SPEED
#define	USB_PORT_FEAT_C_CONNECTION	UHF_C_PORT_CONNECTION
#define	USB_PORT_FEAT_C_ENABLE		UHF_C_PORT_ENABLE
#define	USB_PORT_FEAT_C_SUSPEND		UHF_C_PORT_SUSPEND
#define	USB_PORT_FEAT_C_OVER_CURRENT	UHF_C_PORT_OVER_CURRENT
#define	USB_PORT_FEAT_C_RESET		UHF_C_PORT_RESET
#define	USB_PORT_FEAT_TEST              UHF_PORT_TEST
#define	USB_PORT_FEAT_INDICATOR         UHF_PORT_INDICATOR
#define	USB_PORT_FEAT_C_PORT_L1         UHF_C_PORT_L1

#define	C_HUB_LOCAL_POWER		UHF_C_HUB_LOCAL_POWER
#define	C_HUB_OVER_CURRENT		UHF_C_HUB_OVER_CURRENT

#define USB_REQ_GET_STATUS		UR_GET_STATUS
#define USB_REQ_CLEAR_FEATURE		UR_CLEAR_FEATURE
#define USB_REQ_SET_FEATURE		UR_SET_FEATURE
#define USB_REQ_GET_DESCRIPTOR		UR_GET_DESCRIPTOR

#define	ClearHubFeature		((UT_WRITE_CLASS_DEVICE << 8) | USB_REQ_CLEAR_FEATURE)
#define	ClearPortFeature	((UT_WRITE_CLASS_OTHER << 8) | USB_REQ_CLEAR_FEATURE)
#define	GetHubDescriptor	((UT_READ_CLASS_DEVICE << 8) | USB_REQ_GET_DESCRIPTOR)
#define	GetHubStatus		((UT_READ_CLASS_DEVICE << 8) | USB_REQ_GET_STATUS)
#define	GetPortStatus		((UT_READ_CLASS_OTHER << 8) | USB_REQ_GET_STATUS)
#define	SetHubFeature		((UT_WRITE_CLASS_DEVICE << 8) | USB_REQ_SET_FEATURE)
#define	SetPortFeature		((UT_WRITE_CLASS_OTHER << 8) | USB_REQ_SET_FEATURE)

#define	USB_PORT_STAT_CONNECTION	UPS_CURRENT_CONNECT_STATUS
#define	USB_PORT_STAT_ENABLE		UPS_PORT_ENABLED
#define	USB_PORT_STAT_SUSPEND		UPS_SUSPEND
#define	USB_PORT_STAT_OVERCURRENT	UPS_OVERCURRENT_INDICATOR
#define	USB_PORT_STAT_RESET		UPS_RESET
#define	USB_PORT_STAT_L1		UPS_PORT_L1
#define	USB_PORT_STAT_POWER		UPS_PORT_POWER
#define	USB_PORT_STAT_LOW_SPEED		UPS_LOW_SPEED
#define	USB_PORT_STAT_HIGH_SPEED        UPS_HIGH_SPEED
#define	USB_PORT_STAT_TEST              UPS_PORT_TEST
#define	USB_PORT_STAT_INDICATOR         UPS_PORT_INDICATOR

#define	USB_PORT_STAT_C_CONNECTION	UPS_C_CONNECT_STATUS
#define	USB_PORT_STAT_C_ENABLE		UPS_C_PORT_ENABLED
#define	USB_PORT_STAT_C_SUSPEND		UPS_C_SUSPEND
#define	USB_PORT_STAT_C_OVERCURRENT	UPS_C_OVERCURRENT_INDICATOR
#define	USB_PORT_STAT_C_RESET		UPS_C_PORT_RESET
#define	USB_PORT_STAT_C_L1		UPS_C_PORT_L1

#define	USB_DT_HUB			UDESC_HUB

/* See USB 2.0 spec Table 11-13, offset 3 */
#define HUB_CHAR_LPSM		UHD_PWR
#define HUB_CHAR_COMMON_LPSM	UHD_PWR_GANGED
#define HUB_CHAR_INDV_PORT_LPSM	UHD_PWR_INDIVIDUAL
#define HUB_CHAR_NO_LPSM	UHD_PWR_NO_SWITCH

#define HUB_CHAR_COMPOUND	UHD_COMPOUND

#define HUB_CHAR_OCPM		UHD_OC
#define HUB_CHAR_COMMON_OCPM	UHD_OC_GLOBAL
#define HUB_CHAR_INDV_PORT_OCPM	UHD_OC_INDIVIDUAL
#define HUB_CHAR_NO_OCPM	UHD_OC_NONE

#define HUB_CHAR_TTTT		UHD_TT_THINK
#define HUB_CHAR_PORTIND	UHD_PORT_IND

enum usb_dr_mode {
	USB_DR_MODE_UNKNOWN,
	USB_DR_MODE_HOST,
	USB_DR_MODE_PERIPHERAL,
	USB_DR_MODE_OTG,
};

struct usb_phy;
struct usb_hcd;

static inline int
usb_phy_set_suspend(struct usb_phy *x, int suspend)
{

	return 0;
}

static inline void
usb_hcd_resume_root_hub(struct usb_hcd *hcd)
{

	return;
}

static inline int
usb_disabled(void)
{

	return 0;
}

static inline void
udelay(unsigned long usecs)
{
	DELAY(usecs);
}

static inline void
mdelay(unsigned long msecs)
{
	int loops = msecs;
	while (loops--)
		DELAY(1000);
}

static inline void
usleep_range(unsigned long min, unsigned long max)
{
	DELAY((min + max) / 2);
}

#define dwc2_msleep(x)	mdelay(x)

#define	EREMOTEIO	EIO
#define	ECOMM		EIO
#define	ENOTSUPP	ENOTSUP

void dw_timeout(void *);

struct delayed_work {
	struct task work;
	struct timeout dw_timer;

	struct taskq *dw_wq;
	void (*dw_fn)(void *);
	void *dw_arg;
};

static inline void
INIT_DELAYED_WORK(struct delayed_work *dw, void (*fn)(void *), void *arg)
{
	dw->dw_fn = fn;
	dw->dw_arg = arg;
	timeout_set(&dw->dw_timer, dw_timeout, dw);
}

static inline void
queue_delayed_work(struct taskq *wq, struct delayed_work *dw, int j)
{
	dw->dw_wq = wq;
	timeout_add(&dw->dw_timer, j);
}

#define USB_RESUME_TIMEOUT	40 /* ms */

#endif
