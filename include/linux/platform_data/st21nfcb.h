/*
 * Driver include for the ST21NFCB NFC chip.
 *
 * Copyright (C) 2014  STMicroelectronics SAS. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ST21NFCB_NCI_H_
#define _ST21NFCB_NCI_H_

#include <linux/i2c.h>

#define ST21NFCB_NCI_DRIVER_NAME "st21nfcb_nci"

struct st21nfcb_nfc_platform_data {
	unsigned int gpio_irq;
	unsigned int gpio_reset;
	unsigned int irq_polarity;
};

#endif /* _ST21NFCA_HCI_H_ */
