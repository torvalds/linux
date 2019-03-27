/* $FreeBSD$ */
/*-
 * Copyright (c) 2013 Hans Petter Selasky. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _BSD_GLOBAL_H_
#define	_BSD_GLOBAL_H_

#include <bsd_kernel.h>

#include <sys/gpio.h>

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_freebsd_loader.h>
#include <dev/usb/usb_endian.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_core.h>
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_dynamic.h>
#include <dev/usb/usb_transfer.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_hub.h>
#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usb_cdc.h>
#include <dev/usb/usb_dev.h>
#include <dev/usb/usb_mbuf.h>
#include <dev/usb/usb_msctest.h>
#include <dev/usb/usb_pci.h>
#include <dev/usb/usb_pf.h>
#include <dev/usb/usb_request.h>
#include <dev/usb/usb_util.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usb_generic.h>
#include <dev/usb/quirk/usb_quirk.h>
#include <dev/usb/template/usb_template.h>
#include <dev/usb/controller/ehci.h>
#include <dev/usb/controller/ehcireg.h>

extern struct usb_process usb_process[USB_PROC_MAX];

#endif					/* _BSD_GLOBAL_H_ */
