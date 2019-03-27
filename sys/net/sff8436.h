/*-
 * Copyright (c) 2014 Yandex LLC.
 *
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
 */

/*
 * The following set of constants are from Document SFF-8436
 * "QSFP+ 10 Gbs 4X PLUGGABLE TRANSCEIVER" revision 4.8 dated October 31, 2013
 *
 * This SFF standard defines the following QSFP+ memory address module:
 *
 * 1) 256-byte addressable block and 128-byte pages
 * 2) Lower 128-bytes addresses always refer to the same page
 * 3) Upper address space may refer to different pages depending on
 *   "page select" byte value.
 *
 * Map description:
 *
 * Serial address 0xA02:
 *
 * Lower bits
 * 0-127   Monitoring data & page select byte
 * 128-255:
 *
 * Page 00:
 * 128-191 Base ID Fields
 * 191-223 Extended ID
 * 223-255 Vendor Specific ID
 *
 * Page 01 (optional):
 * 128-255 App-specific data
 *
 * Page 02 (optional):
 * 128-255 User EEPROM Data
 *
 * Page 03 (optional for Cable Assmeblies)
 * 128-223 Thresholds
 * 225-237 Vendor Specific
 * 238-253 Channel Controls/Monitor
 * 254-255 Reserverd
 *
 * All these values are read across an I2C (i squared C) bus.
 */

#define	SFF_8436_BASE	0xA0	/* Base address for all requests */

/* Table 17 - Lower Memory Map */
enum {
	SFF_8436_MID		= 0,	/* Copy of SFF_8436_ID field */
	SFF_8436_STATUS		= 1,	/* 2-bytes status (Table 18) */
	SFF_8436_INTR_START	= 3,	/* Interrupt flags (Tables 19-21) */
	SFF_8436_INTR_END	= 21,
	SFF_8436_MODMON_START	= 22,	/* Module monitors (Table 22 */
	SFF_8436_TEMP		= 22,	/* Internally measured module temp */
	SFF_8436_VCC		= 26,	/* Internally mesasure module
					* supplied voltage */
	SFF_8436_MODMON_END	= 33,
	SFF_8436_CHMON_START	= 34,	/* Channel monitors (Table 23) */
	SFF_8436_RX_CH1_MSB	= 34,	/* Internally measured RX input power */
	SFF_8436_RX_CH1_LSB	= 35,	/* for channel 1 */
	SFF_8436_RX_CH2_MSB	= 36,	/* Internally measured RX input power */
	SFF_8436_RX_CH2_LSB	= 37,	/* for channel 2 */
	SFF_8436_RX_CH3_MSB	= 38,	/* Internally measured RX input power */
	SFF_8436_RX_CH3_LSB	= 39,	/* for channel 3 */
	SFF_8436_RX_CH4_MSB	= 40,	/* Internally measured RX input power */
	SFF_8436_RX_CH4_LSB	= 41,	/* for channel 4 */
	SFF_8436_TX_CH1_MSB	= 42,	/* Internally measured TX bias */
	SFF_8436_TX_CH1_LSB	= 43,	/* for channel 1 */
	SFF_8436_TX_CH2_MSB	= 44,	/* Internally measured TX bias */
	SFF_8436_TX_CH2_LSB	= 45,	/* for channel 2 */
	SFF_8436_TX_CH3_MSB	= 46,	/* Internally measured TX bias */
	SFF_8436_TX_CH3_LSB	= 47,	/* for channel 3 */
	SFF_8436_TX_CH4_MSB	= 48,	/* Internally measured TX bias */
	SFF_8436_TX_CH4_LSB	= 49,	/* for channel 4 */
	SFF_8436_CHANMON_END	= 81,
	SFF_8436_CONTROL_START	= 86,	/* Control (Table 24) */
	SFF_8436_CONTROL_END	= 97,
	SFF_8436_MASKS_START	= 100,	/* Module/channel masks (Table 25) */
	SFF_8436_MASKS_END	= 106,
	SFF_8436_CHPASSWORD	= 119,	/* Password change entry (4 bytes) */
	SFF_8436_PASSWORD	= 123,	/* Password entry area (4 bytes) */
	SFF_8436_PAGESEL	= 127,	/* Page select byte */
};

/* Table 18 - Status Indicators bits */
/* Byte 1: all bits reserved */

/* Byte 2 bits */
#define	SFF_8436_STATUS_FLATMEM	(1 << 2)	/* Upper memory flat or paged
						* 0 = paging, 1=Page 0 only */
#define	SFF_8436_STATUS_INTL	(1 << 1)	/* Digital state of the intL
						* Interrupt output pin */
#define	SFF_8436_STATUS_NOTREADY 1		/* Module has not yet achieved
						* power up and memory data is not
						* ready. 0=data is ready */
/*
 * Upper page 0 definitions:
 * Table 29 - Serial ID: Data fields.
 *
 * Note that this table is mostly the same as used in SFF-8472.
 * The only differenee is address shift: +128 bytes.
 */
enum {
	SFF_8436_ID		= 128,  /* Module Type (defined in sff8472.h) */
	SFF_8436_EXT_ID		= 129,  /* Extended transceiver type
					 * (Table 31) */
	SFF_8436_CONNECTOR	= 130,  /* Connector type (Table 32) */
	SFF_8436_TRANS_START	= 131,  /* Electric or Optical Compatibility
					 * (Table 33) */
	SFF_8436_CODE_E1040100G	= 131,	/* 10/40/100G Ethernet Compliance Code */
	SFF_8436_CODE_SONET	= 132,	/* SONET Compliance codes */
	SFF_8436_CODE_SATA	= 133,	/* SAS/SATA compliance codes */
	SFF_8436_CODE_E1G	= 134,	/* Gigabit Ethernet Compliant codes */
	SFF_8436_CODE_FC_START	= 135,	/* FC link/media/speed */
	SFF_8436_CODE_FC_END	= 138,
	SFF_8436_TRANS_END	= 138,
	SFF_8436_ENCODING	= 139,	/* Encoding Code for high speed
					* serial encoding algorithm (see
					* Table 34) */
	SFF_8436_BITRATE	= 140,	/* Nominal signaling rate, units
					* of 100MBd. */
	SFF_8436_RATEID		= 141,	/* Extended RateSelect Compliance
					* (see Table 35) */
	SFF_8436_LEN_SMF_KM	= 142,	/* Link length supported for single
					* mode fiber, units of km */
	SFF_8436_LEN_OM3	= 143,	/* Link length supported for 850nm
					* 50um multimode fiber, units of 2 m */
	SFF_8436_LEN_OM2	= 144, 	/* Link length supported for 50 um
					* OM2 fiber, units of 1 m */
	SFF_8436_LEN_OM1	= 145,	/* Link length supported for 1310 nm
					 * 50um multi-mode fiber, units of 1m*/
	SFF_8436_LEN_ASM	= 144, /* Link length of passive cable assembly
					* Length is specified as in the INF
					* 8074, units of 1m. 0 means this is
					* not value assembly. Value of 255
					* means thet the Module supports length
					* greater than 254 m. */
	SFF_8436_DEV_TECH	= 147,	/* Device/transmitter technology,
					* see Table 36/37 */
	SFF_8436_VENDOR_START	= 148,	/* Vendor name, 16 bytes, padded
					* right with 0x20 */
	SFF_8436_VENDOR_END	= 163,
	SFF_8436_EXTMODCODE	= 164,	/* Extended module code, Table 164 */
	SFF_8436_VENDOR_OUI_START	= 165 , /* Vendor OUI SFP vendor IEEE
					* company ID */
	SFF_8436_VENDOR_OUI_END	= 167,
	SFF_8436_PN_START 	= 168,	/* Vendor PN, padded right with 0x20 */
	SFF_8436_PN_END 	= 183,
	SFF_8436_REV_START 	= 184,	/* Vendor Revision, padded right 0x20 */
	SFF_8436_REV_END 	= 185,
	SFF_8436_WAVELEN_START	= 186,	/* Wavelength Laser wavelength
					* (Passive/Active Cable
					* Specification Compliance) */
	SFF_8436_WAVELEN_END	= 189,
	SFF_8436_MAX_CASE_TEMP	= 190,	/* Allows to specify maximum temp
					* above 70C. Maximum case temperature is
					* an 8-bit value in Degrees C. A value
					*of 0 implies the standard 70C rating.*/
	SFF_8436_CC_BASE	= 191,	/* CC_BASE Check code for Base ID
					* Fields (first 63 bytes) */
	/* Extended ID fields */
	SFF_8436_OPTIONS_START	= 192, /* Options Indicates which optional
					* transceiver signals are
					* implemented (see Table 39) */
	SFF_8436_OPTIONS_END	= 195,
	SFF_8436_SN_START 	= 196,	/* Vendor SN, riwght padded with 0x20 */
	SFF_8436_SN_END 	= 211,
	SFF_8436_DATE_START	= 212,	/* Vendor’s manufacturing date code
					* (see Table 40) */
	SFF_8436_DATE_END	= 219,
	SFF_8436_DIAG_TYPE	= 220,	/* Diagnostic Monitoring Type
					* Indicates which type of
					* diagnostic monitoring is
					* implemented (if any) in the
					* transceiver (see Table 41) */

	SFF_8436_ENHANCED	= 221,	/* Enhanced Options Indicates which
					* optional features are implemented
					* (if any) in the transceiver
					* (see Table 42) */
	SFF_8636_BITRATE	= 222,	/* Nominal bit rate per channel, units
					* of 250 Mbps */
	SFF_8436_CC_EXT		= 223,	/* Check code for the Extended ID
					* Fields (bytes 192-222 incl) */
	SFF_8436_VENDOR_RSRVD_START	= 224,
	SFF_8436_VENDOR_RSRVD_END	= 255,
};


