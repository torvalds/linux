/*
 * Driver include for the ST21NFCA NFC chip.
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

#ifndef _ST21NFCA_HCI_H_
#define _ST21NFCA_HCI_H_

#include <linux/i2c.h>

#define ST21NFCA_HCI_DRIVER_NAME "st21nfca_hci"

struct st21nfca_nfc_platform_data {
	unsigned int gpio_ena;
	unsigned int irq_polarity;
	bool is_ese_present;
	bool is_uicc_present;
};

#endif /* _ST21NFCA_HCI_H_ */
