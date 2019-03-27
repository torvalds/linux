/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#include <dev/gpio/gpiokeys.h>
#include <gnu/dts/include/dt-bindings/input/linux-event-codes.h>

struct gpiokeys_codemap_entry {
	uint32_t	linux_code;
	uint32_t	bsd_code;
};

static struct gpiokeys_codemap_entry gpiokeys_codes_map[] = {
	{ KEY_ESC,		1},
	{ KEY_1,		2},
	{ KEY_2,		3},
	{ KEY_3,		4},
	{ KEY_4,		5},
	{ KEY_5,		6},
	{ KEY_6,		7},
	{ KEY_7,		8},
	{ KEY_8,		9},
	{ KEY_9,		10},
	{ KEY_0,		11},
	{ KEY_MINUS,		12},
	{ KEY_EQUAL,		13},
	{ KEY_BACKSPACE,	14},

	{ KEY_TAB,		15},
	{ KEY_Q,		16},
	{ KEY_W,		17},
	{ KEY_E,		18},
	{ KEY_R,		19},
	{ KEY_T,		20},
	{ KEY_Y,		21},
	{ KEY_U,		22},
	{ KEY_I,		23},
	{ KEY_O,		24},
	{ KEY_P,		25},
	{ KEY_LEFTBRACE,	26},
	{ KEY_RIGHTBRACE,	27},

	{ KEY_ENTER,		28},
	{ KEY_LEFTCTRL,		29},
	{ KEY_A,		30},
	{ KEY_S,		31},
	{ KEY_D,		32},
	{ KEY_F,		33},
	{ KEY_G,		34},
	{ KEY_H,		35},
	{ KEY_J,		36},
	{ KEY_K,		37},
	{ KEY_L,		38},
	{ KEY_SEMICOLON,	39},
	{ KEY_APOSTROPHE,	40},
	{ KEY_GRAVE,		41},

	{ KEY_LEFTSHIFT,	42},
	{ KEY_BACKSLASH,	43},
	{ KEY_Z,		44},
	{ KEY_X,		45},
	{ KEY_C,		46},
	{ KEY_V,		47},
	{ KEY_B,		48},
	{ KEY_N,		49},
	{ KEY_M,		50},
	{ KEY_COMMA,		51},
	{ KEY_DOT,		52},
	{ KEY_SLASH,		53},
	{ KEY_RIGHTSHIFT,	54},

	{ KEY_LEFTALT,		56},
	{ KEY_SPACE,		57},
	{ KEY_CAPSLOCK,		58},

	{ KEY_F1,		59},
	{ KEY_F2,		60},
	{ KEY_F3,		61},
	{ KEY_F4,		62},
	{ KEY_F5,		63},
	{ KEY_F6,		64},
	{ KEY_F7,		65},
	{ KEY_F8,		66},
	{ KEY_F9,		67},
	{ KEY_F10,		68},
	{ KEY_F11,		87},
	{ KEY_F12,		88},

	{ KEY_RIGHTCTRL,	90},
	{ KEY_SYSRQ,		92},
	{ KEY_RIGHTALT,		93},

	{ KEY_HOME,		GPIOKEY_E0(71)},
	{ KEY_UP,		GPIOKEY_E0(72)},
	{ KEY_PAGEUP,		GPIOKEY_E0(73)},
	{ KEY_LEFT,		GPIOKEY_E0(75)},
	{ KEY_RIGHT,		GPIOKEY_E0(77)},
	{ KEY_END,		GPIOKEY_E0(79)},
	{ KEY_DOWN,		GPIOKEY_E0(80)},
	{ KEY_PAGEDOWN,		GPIOKEY_E0(81)},
	{ KEY_INSERT,		GPIOKEY_E0(82)},
	{ KEY_DELETE,		GPIOKEY_E0(83)},

	{ GPIOKEY_NONE,	GPIOKEY_NONE}
};

uint32_t
gpiokey_map_linux_code(uint32_t linux_code)
{
	int i;

	for (i = 0; gpiokeys_codes_map[i].linux_code != GPIOKEY_NONE; i++) {
		if (gpiokeys_codes_map[i].linux_code == linux_code)
			return (gpiokeys_codes_map[i].bsd_code);
	}

	return (GPIOKEY_NONE);
}
