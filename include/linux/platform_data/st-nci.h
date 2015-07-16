/*
 * Driver include for ST NCI NFC chip family.
 *
 * Copyright (C) 2014-2015  STMicroelectronics SAS. All rights reserved.
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

#ifndef _ST_NCI_H_
#define _ST_NCI_H_

#define ST_NCI_DRIVER_NAME "st_nci"

struct st_nci_nfc_platform_data {
	unsigned int gpio_reset;
	unsigned int irq_polarity;
};

#endif /* _ST_NCI_H_ */
