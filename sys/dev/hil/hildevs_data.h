/*	$OpenBSD: hildevs_data.h,v 1.4 2006/08/10 23:48:06 miod Exp $	*/

/*
 * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.
 *
 * generated from:
 *	OpenBSD: hildevs,v 1.3 2006/08/10 23:44:16 miod Exp 
 */
/*
 * Copyright (c) 2003, Miodrag Vallat.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

const struct hildevice hildevs[] = {
	{ 0x00, 0x1f, HIL_DEVICE_BUTTONBOX, "Keypad" },
	{ 0x2f, 0x2f, HIL_DEVICE_BUTTONBOX, "LPFK Button box" },
	{ 0x30, 0x33, HIL_DEVICE_BUTTONBOX, "Button box" },	/* 31-33 rumored not to exist */
	{ 0x34, 0x34, HIL_DEVICE_IDMODULE, "ID module" },
	{ 0x35, 0x3f, HIL_DEVICE_BUTTONBOX, "Button box" },
	{ 0x5c, 0x5f, HIL_DEVICE_BUTTONBOX, "Barcode reader" },
	{ 0x60, 0x60, HIL_DEVICE_MOUSE, "Single knob" },
	{ 0x61, 0x61, HIL_DEVICE_MOUSE, "Nine knob" },	/* can also be quadrature */
	{ 0x62, 0x67, HIL_DEVICE_MOUSE, "Quadrature" },
	{ 0x68, 0x6b, HIL_DEVICE_MOUSE, "Mouse" },
	{ 0x6c, 0x6f, HIL_DEVICE_MOUSE, "Trackball" },
	{ 0x70, 0x70, HIL_DEVICE_MOUSE, "Knob box" },
	{ 0x71, 0x71, HIL_DEVICE_MOUSE, "Spaceball" },
	{ 0x88, 0x8b, HIL_DEVICE_MOUSE, "Touchpad" },
	{ 0x8c, 0x8f, HIL_DEVICE_MOUSE, "Touchscreen" },
	{ 0x90, 0x97, HIL_DEVICE_MOUSE, "Tablet" },
	{ 0x98, 0x98, HIL_DEVICE_MOUSE, "MMII 1812 Tablet" },
	{ 0x99, 0x99, HIL_DEVICE_MOUSE, "MMII 1201 Tablet" },
	{ 0xa0, 0xbf, HIL_DEVICE_KEYBOARD, "93-key keyboard" },
	{ 0xc0, 0xdf, HIL_DEVICE_KEYBOARD, "109-key keyboard" },
	{ 0xe0, 0xff, HIL_DEVICE_KEYBOARD, "87-key keyboard" },
	{ -1, -1, -1, NULL }
};
