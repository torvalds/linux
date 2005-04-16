/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Offsets for the ROM header locations for
 * TURBOchannel cards
 *
 * created from:
 *
 * TURBOchannel Firmware Specification
 *
 * EK-TCAAD-FS-004
 * from Digital Equipment Corporation
 *
 * Jan.1998 Harald Koerfgen
 */
#ifndef __ASM_DEC_TCMODULE_H
#define __ASM_DEC_TCMODULE_H

#define OLDCARD 0x3c0000
#define NEWCARD 0x000000

#define TC_ROM_WIDTH	0x3e0
#define TC_ROM_STRIDE	0x3e4
#define TC_ROM_SIZE	0x3e8
#define TC_SLOT_SIZE	0x3ec
#define TC_PATTERN0	0x3f0
#define TC_PATTERN1	0x3f4
#define TC_PATTERN2	0x3f8
#define TC_PATTERN3	0x3fc
#define TC_FIRM_VER	0x400
#define TC_VENDOR	0x420
#define TC_MODULE	0x440
#define TC_FIRM_TYPE	0x460
#define TC_FLAGS	0x470
#define TC_ROM_OBJECTS	0x480

#endif /* __ASM_DEC_TCMODULE_H */
