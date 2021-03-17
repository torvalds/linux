/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * max8903_charger.h - Maxim 8903 USB/Adapter Charger Driver
 *
 * Copyright (C) 2011 Samsung Electronics
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 */

#ifndef __MAX8903_CHARGER_H__
#define __MAX8903_CHARGER_H__

struct max8903_pdata {
	/*
	 * GPIOs
	 * cen, chg, flt, dcm and usus are optional.
	 * dok and uok are not optional depending on the status of
	 * dc_valid and usb_valid.
	 */
	int cen;	/* Charger Enable input */
	int dok;	/* DC(Adapter) Power OK output */
	int uok;	/* USB Power OK output */
	int chg;	/* Charger status output */
	int flt;	/* Fault output */
	int dcm;	/* Current-Limit Mode input (1: DC, 2: USB) */
	int usus;	/* USB Suspend Input (1: suspended) */

	/*
	 * DC(Adapter/TA) is wired
	 * When dc_valid is true,
	 *	dok should be valid.
	 *
	 * At least one of dc_valid or usb_valid should be true.
	 */
	bool dc_valid;
	/*
	 * USB is wired
	 * When usb_valid is true,
	 *	uok should be valid.
	 */
	bool usb_valid;
};

#endif /* __MAX8903_CHARGER_H__ */
