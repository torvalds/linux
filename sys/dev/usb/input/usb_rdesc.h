/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Nick Hibma <n_hibma@FreeBSD.org>
 * All rights reserved.
 *
 * Copyright (c) 2005 Ed Schouten <ed@FreeBSD.org>
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
 *
 * $FreeBSD$
 *
 * This file contains replacements for broken HID report descriptors.
 */

#define	UHID_GRAPHIRE_REPORT_DESCR(...) \
    0x05, 0x0d,                    /*  USAGE_PAGE (Digitizers)		*/\
    0x09, 0x01,                    /*  USAGE (Digitizer)		*/\
    0xa1, 0x01,                    /*  COLLECTION (Application)		*/\
    0x85, 0x02,                    /*    REPORT_ID (2)			*/\
    0x05, 0x0d,                    /*    USAGE_PAGE (Digitizers)	*/\
    0x09, 0x01,                    /*    USAGE (Digitizer)		*/\
    0xa1, 0x00,                    /*    COLLECTION (Physical)		*/\
    0x15, 0x00,                    /*      LOGICAL_MINIMUM (0)		*/\
    0x25, 0x01,                    /*      LOGICAL_MAXIMUM (1)		*/\
    0x09, 0x33,                    /*      USAGE (Touch)		*/\
    0x95, 0x01,                    /*      REPORT_COUNT (1)		*/\
    0x75, 0x01,                    /*      REPORT_SIZE (1)		*/\
    0x81, 0x02,                    /*      INPUT (Data,Var,Abs)		*/\
    0x09, 0x44,                    /*      USAGE (Barrel Switch)	*/\
    0x95, 0x02,                    /*      REPORT_COUNT (2)		*/\
    0x75, 0x01,                    /*      REPORT_SIZE (1)		*/\
    0x81, 0x02,                    /*      INPUT (Data,Var,Abs)		*/\
    0x09, 0x00,                    /*      USAGE (Undefined)		*/\
    0x95, 0x02,                    /*      REPORT_COUNT (2)		*/\
    0x75, 0x01,                    /*      REPORT_SIZE (1)		*/\
    0x81, 0x03,                    /*      INPUT (Cnst,Var,Abs)		*/\
    0x09, 0x3c,                    /*      USAGE (Invert)		*/\
    0x95, 0x01,                    /*      REPORT_COUNT (1)		*/\
    0x75, 0x01,                    /*      REPORT_SIZE (1)		*/\
    0x81, 0x02,                    /*      INPUT (Data,Var,Abs)		*/\
    0x09, 0x38,                    /*      USAGE (Transducer Index)	*/\
    0x95, 0x01,                    /*      REPORT_COUNT (1)		*/\
    0x75, 0x01,                    /*      REPORT_SIZE (1)		*/\
    0x81, 0x02,                    /*      INPUT (Data,Var,Abs)		*/\
    0x09, 0x32,                    /*      USAGE (In Range)		*/\
    0x95, 0x01,                    /*      REPORT_COUNT (1)		*/\
    0x75, 0x01,                    /*      REPORT_SIZE (1)		*/\
    0x81, 0x02,                    /*      INPUT (Data,Var,Abs)		*/\
    0x05, 0x01,                    /*      USAGE_PAGE (Generic Desktop)	*/\
    0x09, 0x30,                    /*      USAGE (X)			*/\
    0x15, 0x00,                    /*      LOGICAL_MINIMUM (0)		*/\
    0x26, 0xde, 0x27,              /*      LOGICAL_MAXIMUM (10206)	*/\
    0x95, 0x01,                    /*      REPORT_COUNT (1)		*/\
    0x75, 0x10,                    /*      REPORT_SIZE (16)		*/\
    0x81, 0x02,                    /*      INPUT (Data,Var,Abs)		*/\
    0x09, 0x31,                    /*      USAGE (Y)			*/\
    0x26, 0xfe, 0x1c,              /*      LOGICAL_MAXIMUM (7422)	*/\
    0x95, 0x01,                    /*      REPORT_COUNT (1)		*/\
    0x75, 0x10,                    /*      REPORT_SIZE (16)		*/\
    0x81, 0x02,                    /*      INPUT (Data,Var,Abs)		*/\
    0x05, 0x0d,                    /*      USAGE_PAGE (Digitizers)	*/\
    0x09, 0x30,                    /*      USAGE (Tip Pressure)		*/\
    0x26, 0xff, 0x01,              /*      LOGICAL_MAXIMUM (511)	*/\
    0x95, 0x01,                    /*      REPORT_COUNT (1)		*/\
    0x75, 0x10,                    /*      REPORT_SIZE (16)		*/\
    0x81, 0x02,                    /*      INPUT (Data,Var,Abs)		*/\
    0xc0,                          /*    END_COLLECTION			*/\
    0x05, 0x0d,                    /*    USAGE_PAGE (Digitizers)	*/\
    0x09, 0x00,                    /*    USAGE (Undefined)		*/\
    0x85, 0x02,                    /*    REPORT_ID (2)			*/\
    0x95, 0x01,                    /*    REPORT_COUNT (1)		*/\
    0xb1, 0x02,                    /*    FEATURE (Data,Var,Abs)		*/\
    0x09, 0x00,                    /*    USAGE (Undefined)		*/\
    0x85, 0x03,                    /*    REPORT_ID (3)			*/\
    0x95, 0x01,                    /*    REPORT_COUNT (1)		*/\
    0xb1, 0x02,                    /*    FEATURE (Data,Var,Abs)		*/\
    0xc0,                          /*  END_COLLECTION			*/\

#define	UHID_GRAPHIRE3_4X5_REPORT_DESCR(...) \
    0x05, 0x01,                    /* USAGE_PAGE (Generic Desktop)	*/\
    0x09, 0x02,                    /* USAGE (Mouse)			*/\
    0xa1, 0x01,                    /* COLLECTION (Application)		*/\
    0x85, 0x01,                    /*   REPORT_ID (1)			*/\
    0x09, 0x01,                    /*   USAGE (Pointer)			*/\
    0xa1, 0x00,                    /*   COLLECTION (Physical)		*/\
    0x05, 0x09,                    /*     USAGE_PAGE (Button)		*/\
    0x19, 0x01,                    /*     USAGE_MINIMUM (Button 1)	*/\
    0x29, 0x03,                    /*     USAGE_MAXIMUM (Button 3)	*/\
    0x15, 0x00,                    /*     LOGICAL_MINIMUM (0)		*/\
    0x25, 0x01,                    /*     LOGICAL_MAXIMUM (1)		*/\
    0x95, 0x03,                    /*     REPORT_COUNT (3)		*/\
    0x75, 0x01,                    /*     REPORT_SIZE (1)		*/\
    0x81, 0x02,                    /*     INPUT (Data,Var,Abs)		*/\
    0x95, 0x01,                    /*     REPORT_COUNT (1)		*/\
    0x75, 0x05,                    /*     REPORT_SIZE (5)		*/\
    0x81, 0x01,                    /*     INPUT (Cnst,Ary,Abs)		*/\
    0x05, 0x01,                    /*     USAGE_PAGE (Generic Desktop)	*/\
    0x09, 0x30,                    /*     USAGE (X)			*/\
    0x09, 0x31,                    /*     USAGE (Y)			*/\
    0x09, 0x38,                    /*     USAGE (Wheel)			*/\
    0x15, 0x81,                    /*     LOGICAL_MINIMUM (-127)	*/\
    0x25, 0x7f,                    /*     LOGICAL_MAXIMUM (127)		*/\
    0x75, 0x08,                    /*     REPORT_SIZE (8)		*/\
    0x95, 0x03,                    /*     REPORT_COUNT (3)		*/\
    0x81, 0x06,                    /*     INPUT (Data,Var,Rel)		*/\
    0xc0,                          /*   END_COLLECTION			*/\
    0xc0,                          /* END_COLLECTION			*/\
    0x05, 0x0d,                    /* USAGE_PAGE (Digitizers)		*/\
    0x09, 0x01,                    /* USAGE (Pointer)			*/\
    0xa1, 0x01,                    /* COLLECTION (Applicaption)		*/\
    0x85, 0x02,                    /*   REPORT_ID (2)			*/\
    0x05, 0x0d,                    /*   USAGE_PAGE (Digitizers)		*/\
    0x09, 0x01,                    /*   USAGE (Digitizer)		*/\
    0xa1, 0x00,                    /*   COLLECTION (Physical)		*/\
    0x09, 0x33,                    /*     USAGE (Touch)			*/\
    0x09, 0x44,                    /*     USAGE (Barrel Switch)		*/\
    0x09, 0x44,                    /*     USAGE (Barrel Switch)		*/\
    0x15, 0x00,                    /*     LOGICAL_MINIMUM (0)		*/\
    0x25, 0x01,                    /*     LOGICAL_MAXIMUM (1)		*/\
    0x75, 0x01,                    /*     REPORT_SIZE (1)		*/\
    0x95, 0x03,                    /*     REPORT_COUNT (3)		*/\
    0x81, 0x02,                    /*     INPUT (Data,Var,Abs)		*/\
    0x75, 0x01,                    /*     REPORT_SIZE (1)		*/\
    0x95, 0x02,                    /*     REPORT_COUNT (2)		*/\
    0x81, 0x01,                    /*     INPUT (Cnst,Ary,Abs)		*/\
    0x09, 0x3c,                    /*     USAGE (Invert)		*/\
    0x09, 0x38,                    /*     USAGE (Transducer Index)	*/\
    0x09, 0x32,                    /*     USAGE (In Range)		*/\
    0x75, 0x01,                    /*     REPORT_SIZE (1)		*/\
    0x95, 0x03,                    /*     REPORT_COUNT (3)		*/\
    0x81, 0x02,                    /*     INPUT (Data,Var,Abs)		*/\
    0x05, 0x01,                    /*     USAGE_PAGE (Generic Desktop)	*/\
    0x09, 0x30,                    /*     USAGE (X)			*/\
    0x15, 0x00,                    /*     LOGICAL_MINIMUM (0)		*/\
    0x26, 0xde, 0x27,              /*     LOGICAL_MAXIMUM (10206)	*/\
    0x75, 0x10,                    /*     REPORT_SIZE (16)		*/\
    0x95, 0x01,                    /*     REPORT_COUNT (1)		*/\
    0x81, 0x02,                    /*     INPUT (Data,Var,Abs)		*/\
    0x09, 0x31,                    /*     USAGE (Y)			*/\
    0x26, 0xfe, 0x1c,              /*     LOGICAL_MAXIMUM (7422)	*/\
    0x75, 0x10,                    /*     REPORT_SIZE (16)		*/\
    0x95, 0x01,                    /*     REPORT_COUNT (1)		*/\
    0x81, 0x02,                    /*     INPUT (Data,Var,Abs)		*/\
    0x05, 0x0d,                    /*     USAGE_PAGE (Digitizers)	*/\
    0x09, 0x30,                    /*     USAGE (Tip Pressure)		*/\
    0x26, 0xff, 0x01,              /*     LOGICAL_MAXIMUM (511)		*/\
    0x75, 0x10,                    /*     REPORT_SIZE (16)		*/\
    0x95, 0x01,                    /*     REPORT_COUNT (1)		*/\
    0x81, 0x02,                    /*     INPUT (Data,Var,Abs)		*/\
    0xc0,                          /*   END_COLLECTION			*/\
    0x05, 0x0d,                    /*   USAGE_PAGE (Digitizers)		*/\
    0x09, 0x00,                    /*   USAGE (Undefined)		*/\
    0x85, 0x02,                    /*   REPORT_ID (2)			*/\
    0x95, 0x01,                    /*   REPORT_COUNT (1)		*/\
    0xb1, 0x02,                    /*   FEATURE (Data,Var,Abs)		*/\
    0x09, 0x00,                    /*   USAGE (Undefined)		*/\
    0x85, 0x03,                    /*   REPORT_ID (3)			*/\
    0x95, 0x01,                    /*   REPORT_COUNT (1)		*/\
    0xb1, 0x02,                    /*   FEATURE (Data,Var,Abs)		*/\
    0xc0                           /* END_COLLECTION			*/\

/*
 * The descriptor has no output report format, thus preventing you from
 * controlling the LEDs and the built-in rumblers.
 */
#define	UHID_XB360GP_REPORT_DESCR(...) \
    0x05, 0x01,		/* USAGE PAGE (Generic Desktop)		*/\
    0x09, 0x05,		/* USAGE (Gamepad)			*/\
    0xa1, 0x01,		/* COLLECTION (Application)		*/\
    /* Unused */\
    0x75, 0x08,		/*  REPORT SIZE (8)			*/\
    0x95, 0x01,		/*  REPORT COUNT (1)			*/\
    0x81, 0x01,		/*  INPUT (Constant)			*/\
    /* Byte count */\
    0x75, 0x08,		/*  REPORT SIZE (8)			*/\
    0x95, 0x01,		/*  REPORT COUNT (1)			*/\
    0x05, 0x01,		/*  USAGE PAGE (Generic Desktop)	*/\
    0x09, 0x3b,		/*  USAGE (Byte Count)			*/\
    0x81, 0x01,		/*  INPUT (Constant)			*/\
    /* D-Pad */\
    0x05, 0x01,		/*  USAGE PAGE (Generic Desktop)	*/\
    0x09, 0x01,		/*  USAGE (Pointer)			*/\
    0xa1, 0x00,		/*  COLLECTION (Physical)		*/\
    0x75, 0x01,		/*   REPORT SIZE (1)			*/\
    0x15, 0x00,		/*   LOGICAL MINIMUM (0)		*/\
    0x25, 0x01,		/*   LOGICAL MAXIMUM (1)		*/\
    0x35, 0x00,		/*   PHYSICAL MINIMUM (0)		*/\
    0x45, 0x01,		/*   PHYSICAL MAXIMUM (1)		*/\
    0x95, 0x04,		/*   REPORT COUNT (4)			*/\
    0x05, 0x01,		/*   USAGE PAGE (Generic Desktop)	*/\
    0x09, 0x90,		/*   USAGE (D-Pad Up)			*/\
    0x09, 0x91,		/*   USAGE (D-Pad Down)			*/\
    0x09, 0x93,		/*   USAGE (D-Pad Left)			*/\
    0x09, 0x92,		/*   USAGE (D-Pad Right)		*/\
    0x81, 0x02,		/*   INPUT (Data, Variable, Absolute)	*/\
    0xc0,		/*  END COLLECTION			*/\
    /* Buttons 5-11 */\
    0x75, 0x01,		/*  REPORT SIZE (1)			*/\
    0x15, 0x00,		/*  LOGICAL MINIMUM (0)			*/\
    0x25, 0x01,		/*  LOGICAL MAXIMUM (1)			*/\
    0x35, 0x00,		/*  PHYSICAL MINIMUM (0)		*/\
    0x45, 0x01,		/*  PHYSICAL MAXIMUM (1)		*/\
    0x95, 0x07,		/*  REPORT COUNT (7)			*/\
    0x05, 0x09,		/*  USAGE PAGE (Button)			*/\
    0x09, 0x08,		/*  USAGE (Button 8)			*/\
    0x09, 0x07,		/*  USAGE (Button 7)			*/\
    0x09, 0x09,		/*  USAGE (Button 9)			*/\
    0x09, 0x0a,		/*  USAGE (Button 10)			*/\
    0x09, 0x05,		/*  USAGE (Button 5)			*/\
    0x09, 0x06,		/*  USAGE (Button 6)			*/\
    0x09, 0x0b,		/*  USAGE (Button 11)			*/\
    0x81, 0x02,		/*  INPUT (Data, Variable, Absolute)	*/\
    /* Unused */\
    0x75, 0x01,		/*  REPORT SIZE (1)			*/\
    0x95, 0x01,		/*  REPORT COUNT (1)			*/\
    0x81, 0x01,		/*  INPUT (Constant)			*/\
    /* Buttons 1-4 */\
    0x75, 0x01,		/*  REPORT SIZE (1)			*/\
    0x15, 0x00,		/*  LOGICAL MINIMUM (0)			*/\
    0x25, 0x01,		/*  LOGICAL MAXIMUM (1)			*/\
    0x35, 0x00,		/*  PHYSICAL MINIMUM (0)		*/\
    0x45, 0x01,		/*  PHYSICAL MAXIMUM (1)		*/\
    0x95, 0x04,		/*  REPORT COUNT (4)			*/\
    0x05, 0x09,		/*  USAGE PAGE (Button)			*/\
    0x19, 0x01,		/*  USAGE MINIMUM (Button 1)		*/\
    0x29, 0x04,		/*  USAGE MAXIMUM (Button 4)		*/\
    0x81, 0x02,		/*  INPUT (Data, Variable, Absolute)	*/\
    /* Triggers */\
    0x75, 0x08,		/*  REPORT SIZE (8)			*/\
    0x15, 0x00,		/*  LOGICAL MINIMUM (0)			*/\
    0x26, 0xff, 0x00,	/*  LOGICAL MAXIMUM (255)		*/\
    0x35, 0x00,		/*  PHYSICAL MINIMUM (0)		*/\
    0x46, 0xff, 0x00,	/*  PHYSICAL MAXIMUM (255)		*/\
    0x95, 0x02,		/*  REPORT SIZE (2)			*/\
    0x05, 0x01,		/*  USAGE PAGE (Generic Desktop)	*/\
    0x09, 0x32,		/*  USAGE (Z)				*/\
    0x09, 0x35,		/*  USAGE (Rz)				*/\
    0x81, 0x02,		/*  INPUT (Data, Variable, Absolute)	*/\
    /* Sticks */\
    0x75, 0x10,		/*  REPORT SIZE (16)			*/\
    0x16, 0x00, 0x80,	/*  LOGICAL MINIMUM (-32768)		*/\
    0x26, 0xff, 0x7f,	/*  LOGICAL MAXIMUM (32767)		*/\
    0x36, 0x00, 0x80,	/*  PHYSICAL MINIMUM (-32768)		*/\
    0x46, 0xff, 0x7f,	/*  PHYSICAL MAXIMUM (32767)		*/\
    0x95, 0x04,		/*  REPORT COUNT (4)			*/\
    0x05, 0x01,		/*  USAGE PAGE (Generic Desktop)	*/\
    0x09, 0x30,		/*  USAGE (X)				*/\
    0x09, 0x31,		/*  USAGE (Y)				*/\
    0x09, 0x33,		/*  USAGE (Rx)				*/\
    0x09, 0x34,		/*  USAGE (Ry)				*/\
    0x81, 0x02,		/*  INPUT (Data, Variable, Absolute)	*/\
    /* Unused */\
    0x75, 0x30,		/*  REPORT SIZE (48)			*/\
    0x95, 0x01,		/*  REPORT COUNT (1)			*/\
    0x81, 0x01,		/*  INPUT (Constant)			*/\
    0xc0		/* END COLLECTION			*/\

/* Fixed report descriptor for Super Nintendo gamepads */
#define	UHID_SNES_REPORT_DESCR(...) \
    0x05, 0x01,         /*  Usage Page (Desktop),           */\
    0x09, 0x04,         /*  Usage (Joystik),                */\
    0xA1, 0x01,         /*  Collection (Application),       */\
    0xA1, 0x02,         /*      Collection (Logical),       */\
    0x14,               /*          Logical Minimum (0),    */\
    0x75, 0x08,         /*          Report Size (8),        */\
    0x95, 0x03,         /*          Report Count (3),       */\
    0x81, 0x01,         /*          Input (Constant),       */\
    0x26, 0xFF, 0x00,   /*          Logical Maximum (255),  */\
    0x95, 0x02,         /*          Report Count (2),       */\
    0x09, 0x30,         /*          Usage (X),              */\
    0x09, 0x31,         /*          Usage (Y),              */\
    0x81, 0x02,         /*          Input (Variable),       */\
    0x75, 0x01,         /*          Report Size (1),        */\
    0x95, 0x04,         /*          Report Count (4),       */\
    0x81, 0x01,         /*          Input (Constant),       */\
    0x25, 0x01,         /*          Logical Maximum (1),    */\
    0x95, 0x0A,         /*          Report Count (10),      */\
    0x05, 0x09,         /*          Usage Page (Button),    */\
    0x19, 0x01,         /*          Usage Minimum (01h),    */\
    0x29, 0x0A,         /*          Usage Maximum (0Ah),    */\
    0x81, 0x02,         /*          Input (Variable),       */\
    0x95, 0x0A,         /*          Report Count (10),      */\
    0x81, 0x01,         /*          Input (Constant),       */\
    0xC0,               /*      End Collection,             */\
    0xC0                /*  End Collection                  */
