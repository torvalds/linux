/* $FreeBSD$ */
/*-
 * Copyright (c) 2007-2010 Hans Petter Selasky. All rights reserved.
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/sysctl.h>
#include <sys/time.h>

#include <libusb20.h>
#include <libusb20_desc.h>

#include <dev/usb/usb_endian.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_cdc.h>

#include "usbtest.h"

static void
set_ctrl_ep_fail(int bus, int dev, int ds_fail, int ss_fail)
{
	int error;

	error = sysctlbyname("hw.usb.ctrl_bus_fail", NULL, NULL,
	    &bus, sizeof(bus));
	if (error != 0)
		goto emissing;

	error = sysctlbyname("hw.usb.ctrl_dev_fail", NULL, NULL,
	    &dev, sizeof(dev));
	if (error != 0)
		goto emissing;

	error = sysctlbyname("hw.usb.ctrl_ds_fail", NULL, NULL,
	    &ds_fail, sizeof(ds_fail));
	if (error != 0)
		goto emissing;

	error = sysctlbyname("hw.usb.ctrl_ss_fail", NULL, NULL,
	    &ss_fail, sizeof(ss_fail));
	if (error != 0)
		goto emissing;
	return;

emissing:
	printf("Cannot set USB sysctl, missing USB_REQ_DEBUG option?\n");
}

void
usb_control_ep_error_test(uint16_t vid, uint16_t pid)
{
	struct LIBUSB20_CONTROL_SETUP_DECODED req;
	struct libusb20_device *pdev;
	uint8_t buffer[256];
	int error;
	int fail = 0;
	int bus;
	int dev;
	int cfg;

	pdev = find_usb_device(vid, pid);
	if (pdev == NULL) {
		printf("USB device not found\n");
		return;
	}
	error = libusb20_dev_open(pdev, 0);
	if (error) {
		printf("Could not open USB device\n");
		libusb20_dev_free(pdev);
		return;
	}

	bus = libusb20_dev_get_bus_number(pdev);
	dev = libusb20_dev_get_address(pdev);

	for (cfg = 0; cfg != 255; cfg++) {

		LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &req);
		req.bmRequestType = 0x80; /* read */
		req.bRequest = 0x06; /* descriptor */
		req.wValue = 0x0200 | cfg; /* config descriptor */
		req.wIndex = 0;
		req.wLength = 255;

		printf("Test #%d.1/3 ...\n", cfg);

		set_ctrl_ep_fail(-1,-1,0,0);

		error = libusb20_dev_request_sync(pdev, &req, buffer,
		    NULL, 1000, 0);
		if (error != 0) {
			printf("Last configuration index is: %d\n", cfg - 1);
			break;
		}

		printf("Test #%d.2/3 ...\n", cfg);

		set_ctrl_ep_fail(bus,dev,1,1);

		error = libusb20_dev_request_sync(pdev, &req, buffer,
		    NULL, 1000, 0);

		set_ctrl_ep_fail(-1,-1,0,0);

		error = libusb20_dev_request_sync(pdev, &req, buffer,
		    NULL, 1000, 0);
		if (error != 0) {
			printf("Cannot fetch descriptor (unexpected)\n");
			fail++;
		}

		printf("Test #%d.3/3 ...\n", cfg);

		set_ctrl_ep_fail(bus,dev,0,1);

		error = libusb20_dev_request_sync(pdev, &req, buffer,
		    NULL, 1000, 0);

		set_ctrl_ep_fail(-1,-1,0,0);

		error = libusb20_dev_request_sync(pdev, &req, buffer,
		    NULL, 1000, 0);
		if (error != 0) {
			printf("Cannot fetch descriptor (unexpected)\n");
			fail++;
		}
	}

	libusb20_dev_close(pdev);
	libusb20_dev_free(pdev);

	printf("Test completed detecting %d failures\nDone\n\n", fail);
}

void
usb_get_string_desc_test(uint16_t vid, uint16_t pid)
{
	struct libusb20_device *pdev;
	uint32_t x;
	uint32_t y;
	uint32_t valid;
	uint8_t *buf;
	int error;

	pdev = find_usb_device(vid, pid);
	if (pdev == NULL) {
		printf("USB device not found\n");
		return;
	}
	error = libusb20_dev_open(pdev, 0);
	if (error) {
		printf("Could not open USB device\n");
		libusb20_dev_free(pdev);
		return;
	}
	buf = malloc(256);
	if (buf == NULL) {
		printf("Cannot allocate memory\n");
		libusb20_dev_free(pdev);
		return;
	}
	valid = 0;

	printf("Starting string descriptor test for "
	    "VID=0x%04x PID=0x%04x\n", vid, pid);

	for (x = 0; x != 256; x++) {

		if (libusb20_dev_check_connected(pdev) != 0) {
			printf("Device disconnected\n");
			break;
		}
		printf("%d .. ", (int)x);

		fflush(stdout);

		error = libusb20_dev_req_string_simple_sync(pdev, x, buf, 255);

		if (error == 0) {
			printf("\nINDEX=%d, STRING='%s' (Default language)\n", (int)x, buf);
			fflush(stdout);
		} else {
			continue;
		}

		valid = 0;

		for (y = 0; y != 65536; y++) {

			if (libusb20_dev_check_connected(pdev) != 0) {
				printf("Device disconnected\n");
				break;
			}
			error = libusb20_dev_req_string_sync(pdev, x, y, buf, 256);
			if (error == 0)
				valid++;
		}

		printf("String at INDEX=%d responds to %d "
		    "languages\n", (int)x, (int)valid);
	}

	printf("\nDone\n");

	free(buf);

	libusb20_dev_free(pdev);
}

void
usb_port_reset_test(uint16_t vid, uint16_t pid, uint32_t duration)
{
	struct timeval sub_tv;
	struct timeval ref_tv;
	struct timeval res_tv;

	struct libusb20_device *pdev;

	int error;
	int iter;
	int errcnt;

	time_t last_sec;

	/* sysctl() - no set config */

	pdev = find_usb_device(vid, pid);
	if (pdev == NULL) {
		printf("USB device not found\n");
		return;
	}
	error = libusb20_dev_open(pdev, 0);
	if (error) {
		libusb20_dev_free(pdev);
		printf("Could not open USB device\n");
		return;
	}
	iter = 0;

	errcnt = 0;

	gettimeofday(&ref_tv, 0);

	last_sec = ref_tv.tv_sec;

	while (1) {

		gettimeofday(&sub_tv, 0);

		if (last_sec != sub_tv.tv_sec) {

			printf("STATUS: ID=%u, ERR=%u\n",
			    (int)iter, (int)errcnt);

			fflush(stdout);

			last_sec = sub_tv.tv_sec;
		}
		timersub(&sub_tv, &ref_tv, &res_tv);

		if ((res_tv.tv_sec < 0) || (res_tv.tv_sec >= (int)duration))
			break;

		if (libusb20_dev_reset(pdev)) {
			errcnt++;
			usleep(50000);
		}
		if (libusb20_dev_check_connected(pdev) != 0) {
			printf("Device disconnected\n");
			break;
		}
		iter++;
	}

	libusb20_dev_reset(pdev);

	libusb20_dev_free(pdev);
}

void
usb_set_config_test(uint16_t vid, uint16_t pid, uint32_t duration)
{
	struct libusb20_device *pdev;
	struct LIBUSB20_DEVICE_DESC_DECODED *ddesc;
	int x;
	int error;
	int failed;
	int exp;

	pdev = find_usb_device(vid, pid);
	if (pdev == NULL) {
		printf("USB device not found\n");
		return;
	}
	error = libusb20_dev_open(pdev, 0);
	if (error) {
		printf("Could not open USB device\n");
		libusb20_dev_free(pdev);
		return;
	}
	failed = 0;

	printf("Starting set config test for "
	    "VID=0x%04x PID=0x%04x\n", vid, pid);

	for (x = 255; x > -1; x--) {

		error = libusb20_dev_set_config_index(pdev, x);
		if (error == 0) {
			if (x == 255) {
				printf("Unconfiguring USB device "
				    "was successful\n");
			} else {
				printf("Setting configuration %d "
				    "was successful\n", x);
			}
		} else {
			failed++;
		}
	}

	ddesc = libusb20_dev_get_device_desc(pdev);
	if (ddesc != NULL)
		exp = ddesc->bNumConfigurations + 1;
	else
		exp = 1;

	printf("\n\n"
	    "Set configuration summary\n"
	    "Valid count:  %d/%d %s\n"
	    "Failed count: %d\n",
	    256 - failed, exp,
	    (exp == (256 - failed)) ? "(expected)" : "(unexpected)",
	    failed);

	libusb20_dev_free(pdev);
}

void
usb_get_descriptor_test(uint16_t vid, uint16_t pid, uint32_t duration)
{
	struct libusb20_device *pdev;

	pdev = find_usb_device(vid, pid);
	if (pdev == NULL) {
		printf("USB device not found\n");
		return;
	}
	libusb20_dev_free(pdev);
}

void
usb_suspend_resume_test(uint16_t vid, uint16_t pid, uint32_t duration)
{
	struct timeval sub_tv;
	struct timeval ref_tv;
	struct timeval res_tv;

	struct libusb20_device *pdev;

	time_t last_sec;

	int iter;
	int error;
	int ptimo;
	int errcnt;
	int power_old;

	ptimo = 1;			/* second(s) */

	error = sysctlbyname("hw.usb.power_timeout", NULL, NULL,
	    &ptimo, sizeof(ptimo));

	if (error != 0) {
		printf("WARNING: Could not set power "
		    "timeout to 1 (error=%d) \n", errno);
	}
	pdev = find_usb_device(vid, pid);
	if (pdev == NULL) {
		printf("USB device not found\n");
		return;
	}
	error = libusb20_dev_open(pdev, 0);
	if (error) {
		printf("Could not open USB device\n");
		libusb20_dev_free(pdev);
		return;
	}
	power_old = libusb20_dev_get_power_mode(pdev);

	printf("Starting suspend and resume "
	    "test for VID=0x%04x PID=0x%04x\n", vid, pid);

	iter = 0;
	errcnt = 0;

	gettimeofday(&ref_tv, 0);

	last_sec = ref_tv.tv_sec;

	while (1) {

		if (libusb20_dev_check_connected(pdev) != 0) {
			printf("Device disconnected\n");
			break;
		}
		gettimeofday(&sub_tv, 0);

		if (last_sec != sub_tv.tv_sec) {

			printf("STATUS: ID=%u, ERR=%u\n",
			    (int)iter, (int)errcnt);

			fflush(stdout);

			last_sec = sub_tv.tv_sec;
		}
		timersub(&sub_tv, &ref_tv, &res_tv);

		if ((res_tv.tv_sec < 0) || (res_tv.tv_sec >= (int)duration))
			break;

		error = libusb20_dev_set_power_mode(pdev, (iter & 1) ?
		    LIBUSB20_POWER_ON : LIBUSB20_POWER_SAVE);

		if (error)
			errcnt++;

		/* wait before switching power mode */
		usleep(4100000 +
		    (((uint32_t)usb_ts_rand_noise()) % 2000000U));

		iter++;
	}

	/* restore default power mode */
	libusb20_dev_set_power_mode(pdev, power_old);

	libusb20_dev_free(pdev);
}

void
usb_set_and_clear_stall_test(uint16_t vid, uint16_t pid)
{
	struct libusb20_device *pdev;
	struct libusb20_transfer *pxfer;

	int iter;
	int error;
	int errcnt;
	int ep;

	pdev = find_usb_device(vid, pid);
	if (pdev == NULL) {
		printf("USB device not found\n");
		return;
	}
	error = libusb20_dev_open(pdev, 1);
	if (error) {
		printf("Could not open USB device\n");
		libusb20_dev_free(pdev);
		return;
	}
	printf("Starting set and clear stall test "
	    "for VID=0x%04x PID=0x%04x\n", vid, pid);

	iter = 0;
	errcnt = 0;

	for (ep = 2; ep != 32; ep++) {

		struct LIBUSB20_CONTROL_SETUP_DECODED setup_set_stall;
		struct LIBUSB20_CONTROL_SETUP_DECODED setup_get_status;

		uint8_t epno = ((ep / 2) | ((ep & 1) << 7));
		uint8_t buf[1];

		LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &setup_set_stall);
		setup_set_stall.bmRequestType = 0x02;	/* write endpoint */
		setup_set_stall.bRequest = 0x03;	/* set feature */
		setup_set_stall.wValue = 0x00;	/* UF_ENDPOINT_HALT */
		setup_set_stall.wIndex = epno;
		setup_set_stall.wLength = 0;

		LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &setup_get_status);
		setup_get_status.bmRequestType = 0x82;	/* read endpoint */
		setup_get_status.bRequest = 0x00;	/* get status */
		setup_get_status.wValue = 0x00;
		setup_get_status.wIndex = epno;
		setup_get_status.wLength = 1;

		if (libusb20_dev_check_connected(pdev) != 0) {
			printf("Device disconnected\n");
			break;
		}
		pxfer = libusb20_tr_get_pointer(pdev, 0);

		error = libusb20_tr_open(pxfer, 1, 1, epno);

		if (error != 0) {
			printf("Endpoint 0x%02x does not exist "
			    "in current setting. (%s, ignored)\n",
			    epno, libusb20_strerror(error));
			continue;
		}
		printf("Stalling endpoint 0x%02x\n", epno);

		/* set stall */
		error = libusb20_dev_request_sync(pdev,
		    &setup_set_stall, NULL, NULL, 250, 0);

		if (error != 0) {
			printf("Endpoint 0x%02x does not allow "
			    "setting of stall. (%s)\n",
			    epno, libusb20_strerror(error));
			errcnt++;
		}
		/* get EP status */
		buf[0] = 0;
		error = libusb20_dev_request_sync(pdev,
		    &setup_get_status, buf, NULL, 250, 0);

		if (error != 0) {
			printf("Endpoint 0x%02x does not allow "
			    "reading status. (%s)\n",
			    epno, libusb20_strerror(error));
			errcnt++;
		} else {
			if (!(buf[0] & 1)) {
				printf("Endpoint 0x%02x status is "
				    "not set to stalled\n", epno);
				errcnt++;
			}
		}

		buf[0] = 0;
		error = libusb20_tr_bulk_intr_sync(pxfer, buf, 1, NULL, 250);
		if (error != LIBUSB20_TRANSFER_STALL) {
			printf("Endpoint 0x%02x does not appear to "
			    "have stalled. Missing stall PID!\n", epno);
			errcnt++;
		}
		printf("Unstalling endpoint 0x%02x\n", epno);

		libusb20_tr_clear_stall_sync(pxfer);

		/* get EP status */
		buf[0] = 0;
		error = libusb20_dev_request_sync(pdev,
		    &setup_get_status, buf, NULL, 250, 0);

		if (error != 0) {
			printf("Endpoint 0x%02x does not allow "
			    "reading status. (%s)\n",
			    epno, libusb20_strerror(error));
			errcnt++;
		} else {
			if (buf[0] & 1) {
				printf("Endpoint 0x%02x status is "
				    "still stalled\n", epno);
				errcnt++;
			}
		}

		libusb20_tr_close(pxfer);
		iter++;
	}

	libusb20_dev_free(pdev);

	printf("\n"
	    "Test summary\n"
	    "============\n"
	    "Endpoints tested: %d\n"
	    "Errors: %d\n", iter, errcnt);
}

void
usb_set_alt_interface_test(uint16_t vid, uint16_t pid)
{
	struct libusb20_device *pdev;
	struct libusb20_config *config;

	int iter;
	int error;
	int errcnt;
	int n;
	int m;

	pdev = find_usb_device(vid, pid);
	if (pdev == NULL) {
		printf("USB device not found\n");
		return;
	}
	printf("Starting set alternate setting test "
	    "for VID=0x%04x PID=0x%04x\n", vid, pid);

	config = libusb20_dev_alloc_config(pdev,
	    libusb20_dev_get_config_index(pdev));
	if (config == NULL) {
		printf("Could not get configuration descriptor\n");
		libusb20_dev_free(pdev);
		return;
	}
	iter = 0;
	errcnt = 0;

	for (n = 0; n != config->num_interface; n++) {
		/* detach kernel driver */
		libusb20_dev_detach_kernel_driver(pdev, n);

		error = libusb20_dev_open(pdev, 0);
		if (error)
			printf("ERROR could not open device\n");

		/* Try the alternate settings */
		for (m = 0; m != config->interface[n].num_altsetting; m++) {

			iter++;

			if (libusb20_dev_set_alt_index(pdev, n, m + 1)) {
				printf("ERROR on interface %d alt %d\n", n, m + 1);
				errcnt++;
			}
		}

		/* Restore to default */

		iter++;

		if (libusb20_dev_set_alt_index(pdev, n, 0)) {
			printf("ERROR on interface %d alt %d\n", n, 0);
			errcnt++;
		}
		libusb20_dev_close(pdev);
	}

	libusb20_dev_free(pdev);

	printf("\n"
	    "Test summary\n"
	    "============\n"
	    "Interfaces tested: %d\n"
	    "Errors: %d\n", iter, errcnt);
}
