/*
 * AMLOGIC descrambler driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef _AMDSC_H
#define _AMDSC_H

#include <asm/types.h>

typedef enum {
	AM_DSC_EVEN_KEY,
	AM_DSC_ODD_KEY
} am_dsc_key_type_t;

struct am_dsc_key {
	am_dsc_key_type_t    type;
	__u8                 key[8];
};

#define AMDSC_IOC_MAGIC  'D'

#define AMDSC_IOC_SET_PID      _IO(AMDSC_IOC_MAGIC, 0x00)
#define AMDSC_IOC_SET_KEY      _IOW(AMDSC_IOC_MAGIC, 0x01, struct am_dsc_key)

#endif

