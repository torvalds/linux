/* $FreeBSD$ */
/*-
 * Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
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

#ifndef _USBTEST_H_
#define	_USBTEST_H_

#define	USB_DEVICES_MAX		128
#define	USB_TS_MAX_LEVELS	8

struct libusb20_device;

struct bps {
	uint32_t bytes;
	time_t	time;
};

extern void usb_get_string_desc_test(uint16_t, uint16_t);
extern void usb_port_reset_test(uint16_t, uint16_t, uint32_t);
extern void usb_set_config_test(uint16_t, uint16_t, uint32_t);
extern void usb_get_descriptor_test(uint16_t, uint16_t, uint32_t);
extern void usb_control_ep_error_test(uint16_t, uint16_t);
extern void usb_set_and_clear_stall_test(uint16_t, uint16_t);
extern void usb_set_alt_interface_test(uint16_t, uint16_t);

extern void usb_suspend_resume_test(uint16_t, uint16_t, uint32_t);
extern void do_bps(const char *, struct bps *, uint32_t len);
extern const char *indent[USB_TS_MAX_LEVELS];
extern void show_host_msc_test(uint8_t, uint16_t, uint16_t, uint32_t);
extern void show_host_modem_test(uint8_t, uint16_t, uint16_t, uint32_t);
extern void show_host_device_selection(uint8_t, uint16_t *, uint16_t *);
extern struct libusb20_device *find_usb_device(uint16_t, uint16_t);
extern void find_usb_endpoints(struct libusb20_device *, uint8_t, uint8_t,
    uint8_t, uint8_t, uint8_t *, uint8_t *, uint8_t *, uint8_t);
extern void get_string(char *, int);
extern int get_integer(void);
extern uint8_t usb_ts_show_menu(uint8_t, const char *, const char *,...);
extern int32_t usb_ts_rand_noise(void);

#endif				/* _USBTEST_H_ */
