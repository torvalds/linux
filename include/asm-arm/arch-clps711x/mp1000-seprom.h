#ifndef MP1000_SEPROM_H
#define MP1000_SEPROM_H

/*
 * mp1000-seprom.h
 *
 *
 *  This file contains the Serial EEPROM definitions for the MP1000 board
 *
 *  Copyright (C) 2005 Comdial Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define COMMAND_ERASE		(0x1C0)
#define COMMAND_ERASE_ALL	(0x120)
#define COMMAND_WRITE_DISABLE	(0x100)
#define COMMAND_WRITE_ENABLE	(0x130)
#define COMMAND_READ		(0x180)
#define COMMAND_WRITE		(0x140)
#define COMMAND_WRITE_ALL	(0x110)

//
// Serial EEPROM data format
//

#define PACKED __attribute__ ((packed))

typedef struct _EEPROM {
	union {
		unsigned char eprom_byte_data[128];
		unsigned short eprom_short_data[64];
		struct {
			unsigned char version PACKED;	// EEPROM Version "1" for now
			unsigned char box_id PACKED; 	// Box ID (Standalone, SOHO, embedded, etc)
			unsigned char major_hw_version PACKED;	// Major Hardware version (Hex)
			unsigned char minor_hw_version PACKED;	// Minor Hardware Version (Hex)
			unsigned char mfg_id[3] PACKED;	// Manufacturer ID (3 character Alphabetic)
			unsigned char mfg_serial_number[10] PACKED;	// Manufacturer Serial number
			unsigned char mfg_date[3] PACKED;	// Date of Mfg (Formatted YY:MM:DD)
			unsigned char country PACKED;	// Country of deployment
			unsigned char mac_Address[6] PACKED;	// MAC Address
			unsigned char oem_string[20] PACKED;	// OEM ID string
			unsigned short feature_bits1 PACKED;	// Feature Bits 1
			unsigned short feature_bits2 PACKED;	// Feature Bits 2
			unsigned char filler[75] PACKED;		// Unused/Undefined	“0” initialized
			unsigned short checksum PACKED;		// byte accumulated short checksum
		} eprom_struct;
	} variant;
}  eeprom_struct;

/* These settings must be mutually exclusive */
#define	FEATURE_BITS1_DRAMSIZE_16MEG	0x0001  /* 0 signifies 4 MEG system */
#define	FEATURE_BITS1_DRAMSIZE_8MEG	0x0002  /* 1 in bit 1 = 8MEG system */
#define	FEATURE_BITS1_DRAMSIZE_64MEG	0x0004  /* 1 in bit 2 = 64MEG system */

#define FEATURE_BITS1_CPUIS90MEG     0x0010

extern void seprom_init(void);
extern eeprom_struct* get_seprom_ptr(void);
extern unsigned char* get_eeprom_mac_address(void);

#endif /* MP1000_SEPROM_H */

