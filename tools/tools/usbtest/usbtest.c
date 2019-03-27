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

#include <stdio.h>
#include <stdint.h>
#include <err.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/sysctl.h>

#include <dev/usb/usb_ioctl.h>

#include "usbtest.h"

#include <g_keyboard.h>
#include <g_mouse.h>
#include <g_modem.h>
#include <g_audio.h>

static uint8_t usb_ts_select[USB_TS_MAX_LEVELS];

const char *indent[USB_TS_MAX_LEVELS] = {
	" ",
	"   ",
	"     ",
	"       ",
	"         ",
	"           ",
	"             ",
	"               ",
};

/* a perceptual white noise generator (after HPS' invention) */

int32_t
usb_ts_rand_noise(void)
{
	uint32_t temp;
	const uint32_t prime = 0xFFFF1D;
	static uint32_t noise_rem = 1;

	if (noise_rem & 1) {
		noise_rem += prime;
	}
	noise_rem /= 2;

	temp = noise_rem;

	/* unsigned to signed conversion */

	temp ^= 0x800000;
	if (temp & 0x800000) {
		temp |= (-0x800000);
	}
	return temp;
}

uint8_t
usb_ts_show_menu(uint8_t level, const char *title, const char *fmt,...)
{
	va_list args;
	uint8_t x;
	uint8_t retval;
	char *pstr;
	char buf[16];
	char menu[80 * 20];

	va_start(args, fmt);
	vsnprintf(menu, sizeof(menu), fmt, args);
	va_end(args);

	printf("[");

	for (x = 0; x != level; x++) {
		if ((x + 1) == level)
			printf("%d", usb_ts_select[x]);
		else
			printf("%d.", usb_ts_select[x]);
	}

	printf("] - %s:\n\n", title);

	x = 1;
	for (pstr = menu; *pstr; pstr++) {
		if (x != 0) {
			printf("%s", indent[level]);
			x = 0;
		}
		printf("%c", *pstr);

		if (*pstr == '\n')
			x = 1;
	}

	printf("\n>");

	if (fgets(buf, sizeof(buf), stdin) == NULL)
		err(1, "Cannot read input");

	if (buf[0] == 'x')
		retval = 255;
	else
		retval = atoi(buf);

	usb_ts_select[level] = retval;

	return (retval);
}

void
get_string(char *ptr, int size)
{
	printf("\nEnter string>");

	if (fgets(ptr, size, stdin) == NULL)
		err(1, "Cannot read input");

	ptr[size - 1] = 0;

	size = strlen(ptr);

	/* strip trailing newline, if any */
	if (size == 0)
		return;
	else if (ptr[size - 1] == '\n')
		ptr[size - 1] = 0;
}

int
get_integer(void)
{
	char buf[32];

	printf("\nEnter integer value>");

	if (fgets(buf, sizeof(buf), stdin) == NULL)
		err(1, "Cannot read input");

	if (strcmp(buf, "x\n") == 0)
		return (-1);
	if (strcmp(buf, "r\n") == 0)
		return (-2);

	return ((int)strtol(buf, 0, 0));
}

static void
set_template(int template)
{
	int error;

	error = sysctlbyname("hw.usb.template", NULL, NULL,
	    &template, sizeof(template));

	if (error != 0) {
		printf("WARNING: Could not set USB template "
		    "to %d (error=%d)\n", template, errno);
	}
}

static void
show_default_audio_select(uint8_t level)
{
	int error;
	int retval;
	int mode = 0;
	int pattern_interval = 128;
	int throughput = 0;
	size_t len;
	char pattern[G_AUDIO_MAX_STRLEN] = {"0123456789abcdef"};

	set_template(USB_TEMP_AUDIO);

	while (1) {

		error = sysctlbyname("hw.usb.g_audio.mode", NULL, NULL,
		    &mode, sizeof(mode));

		if (error != 0) {
			printf("WARNING: Could not set audio mode "
			    "to %d (error=%d)\n", mode, errno);
		}
		error = sysctlbyname("hw.usb.g_audio.pattern_interval", NULL, NULL,
		    &pattern_interval, sizeof(pattern_interval));

		if (error != 0) {
			printf("WARNING: Could not set pattern interval "
			    "to %d (error=%d)\n", pattern_interval, errno);
		}
		len = sizeof(throughput);

		error = sysctlbyname("hw.usb.g_audio.throughput",
		    &throughput, &len, 0, 0);

		if (error != 0) {
			printf("WARNING: Could not get throughput "
			    "(error=%d)\n", errno);
		}
		error = sysctlbyname("hw.usb.g_audio.pattern", NULL, NULL,
		    &pattern, strlen(pattern));

		if (error != 0) {
			printf("WARNING: Could not set audio pattern "
			    "to '%s' (error=%d)\n", pattern, errno);
		}
		retval = usb_ts_show_menu(level, "Default Audio Settings",
		    "1) Set Silent mode %s\n"
		    "2) Set Dump mode %s\n"
		    "3) Set Loop mode %s\n"
		    "4) Set Pattern mode %s\n"
		    "5) Change DTMF pattern: '%s'\n"
		    "6) Change pattern advance interval: %d ms\n"
		    "x) Return to previous menu\n"
		    "s: Ready for enumeration\n"
		    "t: Throughput: %d bytes/second\n",
		    (mode == G_AUDIO_MODE_SILENT) ? "(selected)" : "",
		    (mode == G_AUDIO_MODE_DUMP) ? "(selected)" : "",
		    (mode == G_AUDIO_MODE_LOOP) ? "(selected)" : "",
		    (mode == G_AUDIO_MODE_PATTERN) ? "(selected)" : "",
		    pattern, pattern_interval, throughput);

		switch (retval) {
		case 0:
			break;
		case 1:
			mode = G_AUDIO_MODE_SILENT;
			break;
		case 2:
			mode = G_AUDIO_MODE_DUMP;
			break;
		case 3:
			mode = G_AUDIO_MODE_LOOP;
			break;
		case 4:
			mode = G_AUDIO_MODE_PATTERN;
			break;
		case 5:
			get_string(pattern, sizeof(pattern));
			break;
		case 6:
			pattern_interval = get_integer();
			break;
		default:
			return;
		}
	}
}

static void
show_device_audio_select(uint8_t level)
{
	uint8_t retval;

	while (1) {

		retval = usb_ts_show_menu(level, "Select Audio Device Model",
		    "1) Generic Audio Device\n"
		    "x) Return to previous menu\n");

		switch (retval) {
		case 0:
			break;
		case 1:
			show_default_audio_select(level + 1);
			break;
		default:
			return;
		}
	}
}

static void
show_device_msc_select(uint8_t level)
{
	set_template(USB_TEMP_MSC);
}

static void
show_device_ethernet_select(uint8_t level)
{
	set_template(USB_TEMP_CDCE);
}

static void
show_default_keyboard_select(uint8_t level)
{
	int error;
	int retval;
	int mode = 0;
	int interval = 1023;
	char pattern[G_KEYBOARD_MAX_STRLEN] = {"abcdefpattern"};

	set_template(USB_TEMP_KBD);

	while (1) {

		error = sysctlbyname("hw.usb.g_keyboard.mode", NULL, NULL,
		    &mode, sizeof(mode));

		if (error != 0) {
			printf("WARNING: Could not set keyboard mode "
			    " to %d (error=%d) \n", mode, errno);
		}
		error = sysctlbyname("hw.usb.g_keyboard.key_press_interval", NULL, NULL,
		    &interval, sizeof(interval));

		if (error != 0) {
			printf("WARNING: Could not set key press interval "
			    "to %d (error=%d)\n", interval, errno);
		}
		error = sysctlbyname("hw.usb.g_keyboard.key_press_pattern", NULL, NULL,
		    &pattern, strlen(pattern));

		if (error != 0) {
			printf("WARNING: Could not set key pattern "
			    "to '%s' (error=%d)\n", pattern, errno);
		}
		retval = usb_ts_show_menu(level, "Default Keyboard Settings",
		    "1) Set silent mode %s\n"
		    "2) Set pattern mode %s\n"
		    "3) Change pattern: '%s'\n"
		    "4) Change key press interval: %d ms\n"
		    "x) Return to previous menu\n"
		    "s: Ready for enumeration\n",
		    (mode == G_KEYBOARD_MODE_SILENT) ? "(selected)" : "",
		    (mode == G_KEYBOARD_MODE_PATTERN) ? "(selected)" : "",
		    pattern, interval);

		switch (retval) {
		case 0:
			break;
		case 1:
			mode = G_KEYBOARD_MODE_SILENT;
			break;
		case 2:
			mode = G_KEYBOARD_MODE_PATTERN;
			break;
		case 3:
			get_string(pattern, sizeof(pattern));
			break;
		case 4:
			interval = get_integer();
			break;
		default:
			return;
		}
	}
}

static void
show_device_keyboard_select(uint8_t level)
{
	uint8_t retval;

	while (1) {

		retval = usb_ts_show_menu(level, "Select Keyboard Model",
		    "1) Generic Keyboard \n"
		    "x) Return to previous menu \n");

		switch (retval) {
		case 0:
			break;
		case 1:
			show_default_keyboard_select(level + 1);
			break;
		default:
			return;
		}
	}
}

static void
show_default_mouse_select(uint8_t level)
{
	int error;
	int retval;
	int mode = 0;
	int cursor_interval = 128;
	int cursor_radius = 75;
	int button_interval = 0;

	set_template(USB_TEMP_MOUSE);

	while (1) {

		error = sysctlbyname("hw.usb.g_mouse.mode", NULL, NULL,
		    &mode, sizeof(mode));

		if (error != 0) {
			printf("WARNING: Could not set mouse mode "
			    "to %d (error=%d)\n", mode, errno);
		}
		error = sysctlbyname("hw.usb.g_mouse.cursor_update_interval", NULL, NULL,
		    &cursor_interval, sizeof(cursor_interval));

		if (error != 0) {
			printf("WARNING: Could not set cursor update interval "
			    "to %d (error=%d)\n", cursor_interval, errno);
		}
		error = sysctlbyname("hw.usb.g_mouse.button_press_interval", NULL, NULL,
		    &button_interval, sizeof(button_interval));

		if (error != 0) {
			printf("WARNING: Could not set button press interval "
			    "to %d (error=%d)\n", button_interval, errno);
		}
		error = sysctlbyname("hw.usb.g_mouse.cursor_radius", NULL, NULL,
		    &cursor_radius, sizeof(cursor_radius));

		if (error != 0) {
			printf("WARNING: Could not set cursor radius "
			    "to %d (error=%d)\n", cursor_radius, errno);
		}
		retval = usb_ts_show_menu(level, "Default Mouse Settings",
		    "1) Set Silent mode %s\n"
		    "2) Set Circle mode %s\n"
		    "3) Set Square mode %s\n"
		    "4) Set Spiral mode %s\n"
		    "5) Change cursor radius: %d pixels\n"
		    "6) Change cursor update interval: %d ms\n"
		    "7) Change button[0] press interval: %d ms\n"
		    "x) Return to previous menu\n"
		    "s: Ready for enumeration\n",
		    (mode == G_MOUSE_MODE_SILENT) ? "(selected)" : "",
		    (mode == G_MOUSE_MODE_CIRCLE) ? "(selected)" : "",
		    (mode == G_MOUSE_MODE_BOX) ? "(selected)" : "",
		    (mode == G_MOUSE_MODE_SPIRAL) ? "(selected)" : "",
		    cursor_radius, cursor_interval, button_interval);

		switch (retval) {
		case 0:
			break;
		case 1:
			mode = G_MOUSE_MODE_SILENT;
			break;
		case 2:
			mode = G_MOUSE_MODE_CIRCLE;
			break;
		case 3:
			mode = G_MOUSE_MODE_BOX;
			break;
		case 4:
			mode = G_MOUSE_MODE_SPIRAL;
			break;
		case 5:
			cursor_radius = get_integer();
			break;
		case 6:
			cursor_interval = get_integer();
			break;
		case 7:
			button_interval = get_integer();
			break;
		default:
			return;
		}
	}
}

static void
show_device_mouse_select(uint8_t level)
{
	uint8_t retval;

	while (1) {

		retval = usb_ts_show_menu(level, "Select Mouse Model",
		    "1) Generic Mouse\n"
		    "x) Return to previous menu\n");

		switch (retval) {
		case 0:
			break;
		case 1:
			show_default_mouse_select(level + 1);
			break;
		default:
			return;
		}
	}
}

static void
show_device_mtp_select(uint8_t level)
{
	set_template(USB_TEMP_MTP);
}

static void
show_default_modem_select(uint8_t level)
{
	int error;
	int retval;
	int mode = 0;
	int pattern_interval = 128;
	int throughput = 0;
	size_t len;
	char pattern[G_MODEM_MAX_STRLEN] = {"abcdefpattern"};

	set_template(USB_TEMP_MODEM);

	while (1) {

		error = sysctlbyname("hw.usb.g_modem.mode", NULL, NULL,
		    &mode, sizeof(mode));

		if (error != 0) {
			printf("WARNING: Could not set modem mode "
			    "to %d (error=%d)\n", mode, errno);
		}
		error = sysctlbyname("hw.usb.g_modem.pattern_interval", NULL, NULL,
		    &pattern_interval, sizeof(pattern_interval));

		if (error != 0) {
			printf("WARNING: Could not set pattern interval "
			    "to %d (error=%d)\n", pattern_interval, errno);
		}
		len = sizeof(throughput);

		error = sysctlbyname("hw.usb.g_modem.throughput",
		    &throughput, &len, 0, 0);

		if (error != 0) {
			printf("WARNING: Could not get throughput "
			    "(error=%d)\n", errno);
		}
		error = sysctlbyname("hw.usb.g_modem.pattern", NULL, NULL,
		    &pattern, strlen(pattern));

		if (error != 0) {
			printf("WARNING: Could not set modem pattern "
			    "to '%s' (error=%d)\n", pattern, errno);
		}
		retval = usb_ts_show_menu(level, "Default Modem Settings",
		    "1) Set Silent mode %s\n"
		    "2) Set Dump mode %s\n"
		    "3) Set Loop mode %s\n"
		    "4) Set Pattern mode %s\n"
		    "5) Change test pattern: '%s'\n"
		    "6) Change data transmit interval: %d ms\n"
		    "x) Return to previous menu\n"
		    "s: Ready for enumeration\n"
		    "t: Throughput: %d bytes/second\n",
		    (mode == G_MODEM_MODE_SILENT) ? "(selected)" : "",
		    (mode == G_MODEM_MODE_DUMP) ? "(selected)" : "",
		    (mode == G_MODEM_MODE_LOOP) ? "(selected)" : "",
		    (mode == G_MODEM_MODE_PATTERN) ? "(selected)" : "",
		    pattern, pattern_interval, throughput);

		switch (retval) {
		case 0:
			break;
		case 1:
			mode = G_MODEM_MODE_SILENT;
			break;
		case 2:
			mode = G_MODEM_MODE_DUMP;
			break;
		case 3:
			mode = G_MODEM_MODE_LOOP;
			break;
		case 4:
			mode = G_MODEM_MODE_PATTERN;
			break;
		case 5:
			get_string(pattern, sizeof(pattern));
			break;
		case 6:
			pattern_interval = get_integer();
			break;
		default:
			return;
		}
	}
}

static void
show_device_modem_select(uint8_t level)
{
	uint8_t retval;

	while (1) {

		retval = usb_ts_show_menu(level, "Select Modem Model",
		    "1) Generic Modem\n"
		    "x) Return to previous menu\n");

		switch (retval) {
		case 0:
			break;
		case 1:
			show_default_modem_select(level + 1);
			break;
		default:
			return;
		}
	}
}

static void
show_device_generic_select(uint8_t level)
{
}

static void
show_device_select(uint8_t level)
{
	uint8_t retval;

	while (1) {

		retval = usb_ts_show_menu(level, "Select Device Mode Test Group",
		    "1) Audio (UAUDIO)\n"
		    "2) Mass Storage (MSC)\n"
		    "3) Ethernet (CDCE)\n"
		    "4) Keyboard Input Device (UKBD)\n"
		    "5) Mouse Input Device (UMS)\n"
		    "6) Message Transfer Protocol (MTP)\n"
		    "7) Modem (CDC)\n"
		    "8) Generic Endpoint Loopback (GENERIC)\n"
		    "x) Return to previous menu\n");

		switch (retval) {
		case 0:
			break;
		case 1:
			show_device_audio_select(level + 1);
			break;
		case 2:
			show_device_msc_select(level + 1);
			break;
		case 3:
			show_device_ethernet_select(level + 1);
			break;
		case 4:
			show_device_keyboard_select(level + 1);
			break;
		case 5:
			show_device_mouse_select(level + 1);
			break;
		case 6:
			show_device_mtp_select(level + 1);
			break;
		case 7:
			show_device_modem_select(level + 1);
			break;
		case 8:
			show_device_generic_select(level + 1);
			break;
		default:
			return;
		}
	}
}

static void
show_host_select(uint8_t level)
{
	int force_fs = 0;
	int error;
	uint32_t duration = 60;

	uint16_t dev_vid = 0;
	uint16_t dev_pid = 0;
	uint8_t retval;

	while (1) {

		error = sysctlbyname("hw.usb.ehci.no_hs", NULL, NULL,
		    &force_fs, sizeof(force_fs));

		if (error != 0) {
			printf("WARNING: Could not set non-FS mode "
			    "to %d (error=%d)\n", force_fs, errno);
		}
		retval = usb_ts_show_menu(level, "Select Host Mode Test (via LibUSB)",
		    " 1) Select USB device (VID=0x%04x, PID=0x%04x)\n"
		    " 2) Manually enter USB vendor and product ID\n"
		    " 3) Force FULL speed operation: <%s>\n"
		    " 4) Mass Storage (UMASS)\n"
		    " 5) Modem (UMODEM)\n"
		    "10) Start String Descriptor Test\n"
		    "11) Start Port Reset Test\n"
		    "12) Start Set Config Test\n"
		    "13) Start Get Descriptor Test\n"
		    "14) Start Suspend and Resume Test\n"
		    "15) Start Set and Clear Endpoint Stall Test\n"
		    "16) Start Set Alternate Interface Setting Test\n"
		    "17) Start Invalid Control Request Test\n"
		    "30) Duration: <%d> seconds\n"
		    "x) Return to previous menu\n",
		    dev_vid, dev_pid,
		    force_fs ? "YES" : "NO",
		    (int)duration);

		switch (retval) {
		case 0:
			break;
		case 1:
			show_host_device_selection(level + 1, &dev_vid, &dev_pid);
			break;
		case 2:
			dev_vid = get_integer() & 0xFFFF;
			dev_pid = get_integer() & 0xFFFF;
			break;
		case 3:
			force_fs ^= 1;
			break;
		case 4:
			show_host_msc_test(level + 1, dev_vid, dev_pid, duration);
			break;
		case 5:
			show_host_modem_test(level + 1, dev_vid, dev_pid, duration);
			break;
		case 10:
			usb_get_string_desc_test(dev_vid, dev_pid);
			break;
		case 11:
			usb_port_reset_test(dev_vid, dev_pid, duration);
			break;
		case 12:
			usb_set_config_test(dev_vid, dev_pid, duration);
			break;
		case 13:
			usb_get_descriptor_test(dev_vid, dev_pid, duration);
			break;
		case 14:
			usb_suspend_resume_test(dev_vid, dev_pid, duration);
			break;
		case 15:
			usb_set_and_clear_stall_test(dev_vid, dev_pid);
			break;
		case 16:
			usb_set_alt_interface_test(dev_vid, dev_pid);
			break;
		case 17:
			usb_control_ep_error_test(dev_vid, dev_pid);
			break;
		case 30:
			duration = get_integer();
			break;
		default:
			return;
		}
	}
}

static void
show_mode_select(uint8_t level)
{
	uint8_t retval;

	while (1) {

		retval = usb_ts_show_menu(level, "Select Computer Mode",
		    "1) This computer is Running the Device Side\n"
		    "2) This computer is Running the Host Side\n"
		    "x) Return to previous menu\n");

		switch (retval) {
		case 0:
			break;
		case 1:
			show_device_select(level + 1);
			break;
		case 2:
			show_host_select(level + 1);
			break;
		default:
			return;
		}
	}
}

int
main(int argc, char **argv)
{
	show_mode_select(1);

	return (0);
}
