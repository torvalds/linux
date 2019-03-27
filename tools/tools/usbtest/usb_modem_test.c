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

static struct modem {
	struct libusb20_transfer *xfer_in;
	struct libusb20_transfer *xfer_out;
	struct libusb20_device *usb_dev;

	struct bps rx_bytes;
	struct bps tx_bytes;
	uint32_t c0;
	uint32_t c1;
	uint32_t out_state;
	uint32_t in_last;
	uint32_t in_synced;
	uint32_t duration;
	uint32_t errors;

	uint8_t use_vendor_specific;
	uint8_t	loop_data;
	uint8_t	modem_at_mode;
	uint8_t	data_stress_test;
	uint8_t	control_ep_test;
	uint8_t	usb_iface;
	uint8_t	random_tx_length;
	uint8_t	random_tx_delay;

}	modem;

static void
set_defaults(struct modem *p)
{
	memset(p, 0, sizeof(*p));

	p->data_stress_test = 1;
	p->control_ep_test = 1;
	p->duration = 60;		/* seconds */
}

void
do_bps(const char *desc, struct bps *bps, uint32_t len)
{
	bps->bytes += len;
}

static void
modem_out_state(uint8_t *buf)
{
	if (modem.modem_at_mode) {
		switch (modem.out_state & 3) {
		case 0:
			*buf = 'A';
			break;
		case 1:
			*buf = 'T';
			break;
		case 2:
			*buf = '\r';
			break;
		default:
			*buf = '\n';
			modem.c0++;
			break;
		}
		modem.out_state++;
	} else {
		*buf = modem.out_state;
		modem.out_state++;
		modem.out_state %= 255;
	}
}

static void
modem_in_state(uint8_t buf, uint32_t counter)
{
	if ((modem.in_last == 'O') && (buf == 'K')) {
		modem.c1++;
		modem.in_last = buf;
	} else if (buf == modem.in_last) {
		modem.c1++;
		modem.in_last++;
		modem.in_last %= 255;
		if (modem.in_synced == 0) {
			if (modem.errors < 64) {
				printf("Got sync\n");
			}
			modem.in_synced = 1;
		}
	} else {
		if (modem.in_synced) {
			if (modem.errors < 64) {
				printf("Lost sync @ %d, 0x%02x != 0x%02x\n",
				    counter % 512, buf, modem.in_last);
			}
			modem.in_synced = 0;
			modem.errors++;
		}
		modem.in_last = buf;
		modem.in_last++;
		modem.in_last %= 255;
	}
}

static void
modem_write(uint8_t *buf, uint32_t len)
{
	uint32_t n;

	for (n = 0; n != len; n++) {
		modem_out_state(buf + n);
	}

	do_bps("transmitted", &modem.tx_bytes, len);
}

static void
modem_read(uint8_t *buf, uint32_t len)
{
	uint32_t n;

	for (n = 0; n != len; n++) {
		modem_in_state(buf[n], n);
	}

	do_bps("received", &modem.rx_bytes, len);
}

static void
usb_modem_control_ep_test(struct modem *p, uint32_t duration, uint8_t flag)
{
	struct timeval sub_tv;
	struct timeval ref_tv;
	struct timeval res_tv;
	struct LIBUSB20_CONTROL_SETUP_DECODED setup;
	struct usb_cdc_abstract_state ast;
	struct usb_cdc_line_state ls;
	uint16_t feature = UCDC_ABSTRACT_STATE;
	uint16_t state = UCDC_DATA_MULTIPLEXED;
	uint8_t iface_no;
	uint8_t buf[4];
	int id = 0;
	int iter = 0;

	time_t last_sec;

	iface_no = p->usb_iface - 1;

	gettimeofday(&ref_tv, 0);

	last_sec = ref_tv.tv_sec;

	printf("\nTest=%d\n", (int)flag);

	while (1) {

		gettimeofday(&sub_tv, 0);

		if (last_sec != sub_tv.tv_sec) {

			printf("STATUS: ID=%u, COUNT=%u tests/sec ERR=%u\n",
			    (int)id,
			    (int)iter,
			    (int)p->errors);

			fflush(stdout);

			last_sec = sub_tv.tv_sec;

			id++;

			iter = 0;
		}
		timersub(&sub_tv, &ref_tv, &res_tv);

		if ((res_tv.tv_sec < 0) || (res_tv.tv_sec >= (int)duration))
			break;

		LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &setup);

		if (flag & 1) {
			setup.bmRequestType = UT_READ_CLASS_INTERFACE;
			setup.bRequest = 0x03;
			setup.wValue = 0x0001;
			setup.wIndex = iface_no;
			setup.wLength = 0x0002;

			if (libusb20_dev_request_sync(p->usb_dev, &setup, buf, NULL, 250, 0)) {
				p->errors++;
			}
		}
		if (flag & 2) {
			setup.bmRequestType = UT_WRITE_CLASS_INTERFACE;
			setup.bRequest = UCDC_SET_COMM_FEATURE;
			setup.wValue = feature;
			setup.wIndex = iface_no;
			setup.wLength = UCDC_ABSTRACT_STATE_LENGTH;
			USETW(ast.wState, state);

			if (libusb20_dev_request_sync(p->usb_dev, &setup, &ast, NULL, 250, 0)) {
				p->errors++;
			}
		}
		if (flag & 4) {
			USETDW(ls.dwDTERate, 115200);
			ls.bCharFormat = UCDC_STOP_BIT_1;
			ls.bParityType = UCDC_PARITY_NONE;
			ls.bDataBits = 8;

			setup.bmRequestType = UT_WRITE_CLASS_INTERFACE;
			setup.bRequest = UCDC_SET_LINE_CODING;
			setup.wValue = 0;
			setup.wIndex = iface_no;
			setup.wLength = sizeof(ls);

			if (libusb20_dev_request_sync(p->usb_dev, &setup, &ls, NULL, 250, 0)) {
				p->errors++;
			}
		}
		iter++;
	}

	printf("\nModem control endpoint test done!\n");
}

static void
usb_modem_data_stress_test(struct modem *p, uint32_t duration)
{
	struct timeval sub_tv;
	struct timeval ref_tv;
	struct timeval res_tv;

	time_t last_sec;

	uint8_t in_pending = 0;
	uint8_t in_ready = 0;
	uint8_t out_pending = 0;

	uint32_t id = 0;

	uint32_t in_max;
	uint32_t out_max;
	uint32_t io_max;

	uint8_t *in_buffer = 0;
	uint8_t *out_buffer = 0;

	gettimeofday(&ref_tv, 0);

	last_sec = ref_tv.tv_sec;

	printf("\n");

	in_max = libusb20_tr_get_max_total_length(p->xfer_in);
	out_max = libusb20_tr_get_max_total_length(p->xfer_out);

	/* get the smallest buffer size and use that */
	io_max = (in_max < out_max) ? in_max : out_max;

	if (in_max != out_max)
		printf("WARNING: Buffer sizes are un-equal: %u vs %u\n", in_max, out_max);

	in_buffer = malloc(io_max);
	if (in_buffer == NULL)
		goto fail;

	out_buffer = malloc(io_max);
	if (out_buffer == NULL)
		goto fail;

	while (1) {

		gettimeofday(&sub_tv, 0);

		if (last_sec != sub_tv.tv_sec) {

			printf("STATUS: ID=%u, RX=%u bytes/sec, TX=%u bytes/sec, ERR=%d\n",
			    (int)id,
			    (int)p->rx_bytes.bytes,
			    (int)p->tx_bytes.bytes,
			    (int)p->errors);

			p->rx_bytes.bytes = 0;
			p->tx_bytes.bytes = 0;

			fflush(stdout);

			last_sec = sub_tv.tv_sec;

			id++;
		}
		timersub(&sub_tv, &ref_tv, &res_tv);

		if ((res_tv.tv_sec < 0) || (res_tv.tv_sec >= (int)duration))
			break;

		libusb20_dev_process(p->usb_dev);

		if (!libusb20_tr_pending(p->xfer_in)) {
			if (in_pending) {
				if (libusb20_tr_get_status(p->xfer_in) == 0) {
					modem_read(in_buffer, libusb20_tr_get_length(p->xfer_in, 0));
				} else {
					p->errors++;
					usleep(10000);
				}
				in_pending = 0;
				in_ready = 1;
			}
			if (p->loop_data == 0) {
				libusb20_tr_setup_bulk(p->xfer_in, in_buffer, io_max, 0);
				libusb20_tr_start(p->xfer_in);
				in_pending = 1;
				in_ready = 0;
			}
		}
		if (!libusb20_tr_pending(p->xfer_out)) {

			uint32_t len;
			uint32_t dly;

			if (out_pending) {
				if (libusb20_tr_get_status(p->xfer_out) != 0) {
					p->errors++;
					usleep(10000);
				}
			}
			if (p->random_tx_length) {
				len = ((uint32_t)usb_ts_rand_noise()) % ((uint32_t)io_max);
			} else {
				len = io_max;
			}

			if (p->random_tx_delay) {
				dly = ((uint32_t)usb_ts_rand_noise()) % 16000U;
			} else {
				dly = 0;
			}

			if (p->loop_data != 0) {
				if (in_ready != 0) {
					len = libusb20_tr_get_length(p->xfer_in, 0);
					memcpy(out_buffer, in_buffer, len);
					in_ready = 0;
				} else {
					len = io_max + 1;
				}
				if (!libusb20_tr_pending(p->xfer_in)) {
					libusb20_tr_setup_bulk(p->xfer_in, in_buffer, io_max, 0);
					libusb20_tr_start(p->xfer_in);
					in_pending = 1;
				}
			} else {
				modem_write(out_buffer, len);
			}

			if (len <= io_max) {
				libusb20_tr_setup_bulk(p->xfer_out, out_buffer, len, 0);

				if (dly != 0)
					usleep(dly);

				libusb20_tr_start(p->xfer_out);

				out_pending = 1;
			}
		}
		libusb20_dev_wait_process(p->usb_dev, 500);

		if (libusb20_dev_check_connected(p->usb_dev) != 0) {
			printf("Device disconnected\n");
			break;
		}
	}

	libusb20_tr_stop(p->xfer_in);
	libusb20_tr_stop(p->xfer_out);

	printf("\nData stress test done!\n");

fail:
	if (in_buffer)
		free(in_buffer);
	if (out_buffer)
		free(out_buffer);
}

static void
exec_host_modem_test(struct modem *p, uint16_t vid, uint16_t pid)
{
	struct libusb20_device *pdev;

	uint8_t ntest = 0;
	uint8_t x;
	uint8_t in_ep;
	uint8_t out_ep;
	uint8_t iface;

	int error;

	pdev = find_usb_device(vid, pid);
	if (pdev == NULL) {
		printf("USB device not found\n");
		return;
	}

	if (p->use_vendor_specific)
		find_usb_endpoints(pdev, 255, 255, 255, 0, &iface, &in_ep, &out_ep, 0);
	else
		find_usb_endpoints(pdev, 2, 2, 1, 0, &iface, &in_ep, &out_ep, 1);

	if ((in_ep == 0) || (out_ep == 0)) {
		printf("Could not find USB endpoints\n");
		libusb20_dev_free(pdev);
		return;
	}
	printf("Attaching to: %s @ iface %d\n",
	    libusb20_dev_get_desc(pdev), iface);

	if (libusb20_dev_open(pdev, 2)) {
		printf("Could not open USB device\n");
		libusb20_dev_free(pdev);
		return;
	}
	if (libusb20_dev_detach_kernel_driver(pdev, iface)) {
		printf("WARNING: Could not detach kernel driver\n");
	}
	p->xfer_in = libusb20_tr_get_pointer(pdev, 0);
	error = libusb20_tr_open(p->xfer_in, 65536 / 4, 1, in_ep);
	if (error) {
		printf("Could not open USB endpoint %d\n", in_ep);
		libusb20_dev_free(pdev);
		return;
	}
	p->xfer_out = libusb20_tr_get_pointer(pdev, 1);
	error = libusb20_tr_open(p->xfer_out, 65536 / 4, 1, out_ep);
	if (error) {
		printf("Could not open USB endpoint %d\n", out_ep);
		libusb20_dev_free(pdev);
		return;
	}
	p->usb_dev = pdev;
	p->usb_iface = iface;
	p->errors = 0;

	if (p->control_ep_test)
		ntest += 7;

	if (p->data_stress_test)
		ntest += 1;

	if (ntest == 0) {
		printf("No tests selected\n");
	} else {

		if (p->control_ep_test) {
			for (x = 1; x != 8; x++) {
				usb_modem_control_ep_test(p,
				    (p->duration + ntest - 1) / ntest, x);
			}
		}
		if (p->data_stress_test) {
			usb_modem_data_stress_test(p,
			    (p->duration + ntest - 1) / ntest);
		}
	}

	printf("\nDone\n");

	libusb20_dev_free(pdev);
}

void
show_host_modem_test(uint8_t level, uint16_t vid, uint16_t pid, uint32_t duration)
{
	uint8_t retval;

	set_defaults(&modem);

	modem.duration = duration;

	while (1) {

		retval = usb_ts_show_menu(level, "Modem Test Parameters",
		    " 1) Execute Data Stress Test: <%s>\n"
		    " 2) Execute Modem Control Endpoint Test: <%s>\n"
		    " 3) Use random transmit length: <%s>\n"
		    " 4) Use random transmit delay: <%s> ms\n"
		    " 5) Use vendor specific interface: <%s>\n"
		    "10) Loop data: <%s>\n"
		    "13) Set test duration: <%d> seconds\n"
		    "20) Reset parameters\n"
		    "30) Start test (VID=0x%04x, PID=0x%04x)\n"
		    "40) Select another device\n"
		    " x) Return to previous menu \n",
		    (modem.data_stress_test ? "YES" : "NO"),
		    (modem.control_ep_test ? "YES" : "NO"),
		    (modem.random_tx_length ? "YES" : "NO"),
		    (modem.random_tx_delay ? "16" : "0"),
		    (modem.use_vendor_specific ? "YES" : "NO"),
		    (modem.loop_data ? "YES" : "NO"),
		    (int)(modem.duration),
		    (int)vid, (int)pid);

		switch (retval) {
		case 0:
			break;
		case 1:
			modem.data_stress_test ^= 1;
			break;
		case 2:
			modem.control_ep_test ^= 1;
			break;
		case 3:
			modem.random_tx_length ^= 1;
			break;
		case 4:
			modem.random_tx_delay ^= 1;
			break;
		case 5:
			modem.use_vendor_specific ^= 1;
			modem.control_ep_test = 0;
			break;
		case 10:
			modem.loop_data ^= 1;
			break;
		case 13:
			modem.duration = get_integer();
			break;
		case 20:
			set_defaults(&modem);
			break;
		case 30:
			exec_host_modem_test(&modem, vid, pid);
			break;
		case 40:
			show_host_device_selection(level + 1, &vid, &pid);
			break;
		default:
			return;
		}
	}
}
