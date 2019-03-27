/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003 Hidetoshi Shimokawa
 * Copyright (c) 1998-2002 Katsushi Kobayashi and Hidetoshi Shimokawa
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the acknowledgement as bellow:
 *
 *    This product includes software developed by K. Kobayashi and H. Shimokawa
 *
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD$
 *
 */

#define	STATE_CLEAR	0x0000
#define	STATE_SET	0x0004
#define	NODE_IDS	0x0008
#define	RESET_START	0x000c
#define	SPLIT_TIMEOUT_HI	0x0018
#define	SPLIT_TIMEOUT_LO	0x001c
#define	CYCLE_TIME	0x0200
#define	BUS_TIME	0x0204
#define	BUSY_TIMEOUT	0x0210
#define	PRIORITY_BUDGET 0x0218
#define	BUS_MGR_ID	0x021c
#define	BANDWIDTH_AV	0x0220
#define	CHANNELS_AV_HI	0x0224
#define	CHANNELS_AV_LO	0x0228
#define	IP_CHANNELS	0x0234

#define	CONF_ROM	0x0400

#define	TOPO_MAP	0x1000
#define	SPED_MAP	0x2000

#define CSRTYPE_SHIFT	6
#define CSRTYPE_MASK	(3 << CSRTYPE_SHIFT)
#define CSRTYPE_I	(0 << CSRTYPE_SHIFT) /* Immediate */
#define CSRTYPE_C	(1 << CSRTYPE_SHIFT) /* CSR offset */
#define CSRTYPE_L	(2 << CSRTYPE_SHIFT) /* Leaf */
#define CSRTYPE_D	(3 << CSRTYPE_SHIFT) /* Directory */

/*
 * CSR keys
 * 00 - 2F: defined by CSR architecture standards.
 * 30 - 37: defined by BUS starndards
 * 38 - 3F: defined by Vendor/Specifier
 */
#define CSRKEY_MASK	0x3f
#define CSRKEY_DESC	0x01 /* Descriptor */
#define CSRKEY_BDINFO	0x02 /* Bus_Dependent_Info */
#define CSRKEY_VENDOR	0x03 /* Vendor */
#define CSRKEY_HW	0x04 /* Hardware_Version */
#define CSRKEY_MODULE	0x07 /* Module */
#define CSRKEY_NCAP	0x0c /* Node_Capabilities */
#define CSRKEY_EUI64	0x0d /* EUI_64 */
#define CSRKEY_UNIT	0x11 /* Unit */
#define CSRKEY_SPEC	0x12 /* Specifier_ID */
#define CSRKEY_VER	0x13 /* Version */
#define CSRKEY_DINFO	0x14 /* Dependent_Info */
#define CSRKEY_ULOC	0x15 /* Unit_Location */
#define CSRKEY_MODEL	0x17 /* Model */
#define CSRKEY_INST	0x18 /* Instance */
#define CSRKEY_KEYW	0x19 /* Keyword */
#define CSRKEY_FEAT	0x1a /* Feature */
#define CSRKEY_EROM	0x1b /* Extended_ROM */
#define CSRKEY_EKSID	0x1c /* Extended_Key_Specifier_ID */
#define CSRKEY_EKEY	0x1d /* Extended_Key */
#define CSRKEY_EDATA	0x1e /* Extended_Data */
#define CSRKEY_MDESC	0x1f /* Modifiable_Descriptor */
#define CSRKEY_DID	0x20 /* Directory_ID */
#define CSRKEY_REV	0x21 /* Revision */

#define CSRKEY_FIRM_VER	0x3c /* Firmware version */
#define CSRKEY_UNIT_CH	0x3a /* Unit characteristics */
#define CSRKEY_COM_SPEC	0x38 /* Command set revision */
#define CSRKEY_COM_SET	0x39 /* Command set */

#define CROM_UDIR	(CSRTYPE_D | CSRKEY_UNIT)  /* 0x81 Unit directory */
#define CROM_TEXTLEAF	(CSRTYPE_L | CSRKEY_DESC)  /* 0x81 Text leaf */
#define CROM_LUN	(CSRTYPE_I | CSRKEY_DINFO) /* 0x14 Logical unit num. */
#define CROM_MGM	(CSRTYPE_C | CSRKEY_DINFO) /* 0x54 Management agent */

#define CSRVAL_VENDOR_PRIVATE	0xacde48
#define CSRVAL_1394TA	0x00a02d
#define CSRVAL_ANSIT10	0x00609e
#define CSRVAL_IETF	0x00005e

#define CSR_PROTAVC	0x010001
#define CSR_PROTCAL	0x010002
#define CSR_PROTEHS	0x010004
#define CSR_PROTHAVI	0x010008
#define CSR_PROTCAM104	0x000100
#define CSR_PROTCAM120	0x000101
#define CSR_PROTCAM130	0x000102
#define CSR_PROTDPP	0x0a6be2
#define CSR_PROTIICP	0x4b661f

#define CSRVAL_T10SBP2	0x010483
#define CSRVAL_SCSI	0x0104d8

struct csrreg {
#if BYTE_ORDER == BIG_ENDIAN
	uint32_t key:8,
		 val:24;
#else
	uint32_t val:24,
		 key:8;
#endif
};
struct csrhdr {
#if BYTE_ORDER == BIG_ENDIAN
	uint32_t info_len:8,
		 crc_len:8,
		 crc:16;
#else
	uint32_t crc:16,
		 crc_len:8,
		 info_len:8;
#endif
};
struct csrdirectory {
	BIT16x2(crc_len, crc);
	struct csrreg entry[0];
};
struct csrtext {
	BIT16x2(crc_len, crc);
#if BYTE_ORDER == BIG_ENDIAN
	uint32_t spec_type:8,
		 spec_id:24;
#else
	uint32_t spec_id:24,
		 spec_type:8;
#endif
	uint32_t lang_id;
	uint32_t text[0];
};

struct bus_info {
#define	CSR_BUS_NAME_IEEE1394	0x31333934
	uint32_t bus_name;	
#if BYTE_ORDER == BIG_ENDIAN
	uint32_t irmc:1,		/* iso. resource manager capable */
		 cmc:1,			/* cycle master capable */
		 isc:1,			/* iso. operation support */
		 bmc:1,			/* bus manager capable */
		 pmc:1,			/* power manager capable */
		 :3,
		 cyc_clk_acc:8,		/* 0 <= ppm <= 100 */
		 max_rec:4,		/* (2 << max_rec) bytes */
		 :2,
		 max_rom:2,
		 generation:4,
		 :1,
		 link_spd:3;
#else
	uint32_t link_spd:3,
		 :1,
		 generation:4,
		 max_rom:2,
		 :2,
		 max_rec:4,		/* (2 << max_rec) bytes */
		 cyc_clk_acc:8,		/* 0 <= ppm <= 100 */
		 :3,
		 pmc:1,			/* power manager capable */
		 bmc:1,			/* bus manager capable */
		 isc:1,			/* iso. operation support */
		 cmc:1,			/* cycle master capable */
		 irmc:1;		/* iso. resource manager capable */
#endif
	struct fw_eui64 eui64;
};
/* max_rom */
#define MAXROM_4	0
#define MAXROM_64	1
#define MAXROM_1024	2

#define CROM_MAX_DEPTH	10
struct crom_ptr {
	struct csrdirectory *dir;
	int index;
};

struct crom_context {
	int depth;
	struct crom_ptr stack[CROM_MAX_DEPTH];
};

void crom_init_context(struct crom_context *, uint32_t *);
struct csrreg *crom_get(struct crom_context *);
void crom_next(struct crom_context *);
void crom_parse_text(struct crom_context *, char *, int);
uint16_t crom_crc(uint32_t *r, int);
struct csrreg *crom_search_key(struct crom_context *, uint8_t);
int crom_has_specver(uint32_t *, uint32_t, uint32_t);

#if !defined(_KERNEL) && !defined(_BOOT)
char *crom_desc(struct crom_context *, char *, int);
#endif

/* For CROM build */
#if defined(_KERNEL) || defined(_BOOT) || defined(TEST)
#define CROM_MAX_CHUNK_LEN 20
struct crom_src {
	struct csrhdr hdr;
	struct bus_info businfo;
	STAILQ_HEAD(, crom_chunk) chunk_list;
};

struct crom_chunk {
	STAILQ_ENTRY(crom_chunk) link;
	struct crom_chunk *ref_chunk; 
	int ref_index; 
	int offset;
	struct {
		BIT16x2(crc_len, crc);
		uint32_t buf[CROM_MAX_CHUNK_LEN]; 
	} data;
};

extern int crom_add_quad(struct crom_chunk *, uint32_t);
extern int crom_add_entry(struct crom_chunk *, int, int);
extern int crom_add_chunk(struct crom_src *src, struct crom_chunk *,
					struct crom_chunk *, int);
extern int crom_add_simple_text(struct crom_src *src, struct crom_chunk *,
					struct crom_chunk *, char *);
extern int crom_load(struct crom_src *, uint32_t *, int);
#endif
