/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Aleksandr Rybalko under sponsorship from the
 * FreeBSD Foundation.
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

#include <dev/vt/vt.h>

#ifndef SC_NO_CUTPASTE
struct vt_mouse_cursor vt_default_mouse_pointer = {
	.map = {
		0x00, 0x00, /* "__        " */
		0x40, 0x00, /* "_*_       " */
		0x60, 0x00, /* "_**_      " */
		0x70, 0x00, /* "_***_     " */
		0x78, 0x00, /* "_****_    " */
		0x7c, 0x00, /* "_*****_   " */
		0x7e, 0x00, /* "_******_  " */
		0x7f, 0x00, /* "_*******_ " */
		0x7f, 0x80, /* "_********_" */
		0x7c, 0x00, /* "_*****____" */
		0x6c, 0x00, /* "_**_**_   " */
		0x46, 0x00, /* "_*_ _**_  " */
		0x06, 0x00, /* "__  _**_  " */
		0x03, 0x00, /* "     _**_ " */
		0x03, 0x00, /* "     _**_ " */
		0x00, 0x00, /* "      __  " */
	},
	.mask = {
		0xc0, 0x00, /* "__        " */
		0xe0, 0x00, /* "___       " */
		0xf0, 0x00, /* "____      " */
		0xf8, 0x00, /* "_____     " */
		0xfc, 0x00, /* "______    " */
		0xfe, 0x00, /* "_______   " */
		0xff, 0x00, /* "________  " */
		0xff, 0x80, /* "_________ " */
		0xff, 0xc0, /* "__________" */
		0xff, 0xc0, /* "__________" */
		0xfe, 0x00, /* "_______   " */
		0xef, 0x00, /* "___ ____  " */
		0xcf, 0x00, /* "__  ____  " */
		0x07, 0x80, /* "     ____ " */
		0x07, 0x80, /* "     ____ " */
		0x03, 0x00, /* "      __  " */
	},
	.width = 10,
	.height = 16,
};
#endif
