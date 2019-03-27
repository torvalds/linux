/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2006-2014 QLogic Corporation
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_BCEREG_H_DEFINED
#define _BCEREG_H_DEFINED

/****************************************************************************/
/* Conversion to FreeBSD type definitions.                                  */
/****************************************************************************/
#define u64 uint64_t
#define u32 uint32_t
#define u16 uint16_t
#define u8  uint8_t

#if BYTE_ORDER == BIG_ENDIAN
#define __BIG_ENDIAN 1
#undef  __LITTLE_ENDIAN
#else
#undef  __BIG_ENDIAN
#define __LITTLE_ENDIAN 1
#endif

#define BCE_DWORD_PRINTFB	\
	"\020"			\
	"\40b31"		\
	"\37b30"		\
	"\36b29"		\
	"\35b28"		\
	"\34b27"		\
	"\33b26"		\
	"\32b25"		\
	"\31b24"		\
	"\30b23"		\
	"\27b22"		\
	"\26b21"		\
	"\25b20"		\
	"\24b19"		\
	"\23b18"		\
	"\22b17"		\
	"\21b16"		\
	"\20b15"		\
	"\17b14"		\
	"\16b13"		\
	"\15b12"		\
	"\14b11"		\
	"\13b10"		\
	"\12b9"			\
	"\11b8"			\
	"\10b7"			\
	"\07b6"			\
	"\06b5"			\
	"\05b4"			\
	"\04b3"			\
	"\03b2"			\
	"\02b1"			\
	"\01b0"

/* MII Control Register 0x0 */
#define BCE_BMCR_PRINTFB	\
	"\020"			\
	"\20Reset"		\
	"\17Loopback"		\
	"\16Spd0"		\
	"\15AnegEna"		\
	"\14PwrDn"		\
	"\13Isolate"		\
	"\12RstrtAneg"		\
	"\11FD"			\
	"\10CollTst"		\
	"\07Spd1"		\
	"\06Rsrvd"		\
	"\05Rsrvd"		\
	"\04Rsrvd"		\
	"\03Rsrvd"		\
	"\02Rsrvd"		\
	"\01Rsrvd"

/* MII Status Register 0x1 */
#define BCE_BMSR_PRINTFB	\
	"\020"			\
	"\20Cap100T4"		\
	"\17Cap100XFD"		\
	"\16Cap100XHD"		\
	"\15Cap10FD"		\
	"\14Cap10HD"		\
	"\13Cap100T2FD"		\
	"\12Cap100T2HD"		\
	"\11ExtStsPrsnt"	\
	"\10Rsrvd"		\
	"\07PrmblSupp"		\
	"\06AnegCmpl"		\
	"\05RemFaultDet"	\
	"\04AnegCap"		\
	"\03LnkUp"		\
	"\02JabberDet"		\
	"\01ExtCapSupp"

/* MII Autoneg Advertisement Register 0x4 */
#define BCE_ANAR_PRINTFB	\
	"\020"			\
	"\20AdvNxtPg"		\
	"\17Rsrvd"		\
	"\16AdvRemFault"	\
	"\15Rsrvd"		\
	"\14AdvAsymPause"	\
	"\13AdvPause"		\
	"\12Adv100T4"		\
	"\11Adv100FD"		\
	"\10Adv100HD"		\
	"\07Adv10FD"		\
	"\06Adv10HD"		\
	"\05Rsrvd"		\
	"\04Rsrvd"		\
	"\03Rsrvd"		\
	"\02Rsrvd"		\
	"\01Adv802.3"

/* MII Autoneg Link Partner Ability Register 0x5 */
#define BCE_ANLPAR_PRINTFB	\
	"\020"			\
	"\20CapNxtPg"		\
	"\17Ack"		\
	"\16CapRemFault"	\
	"\15Rsrvd"		\
	"\14CapAsymPause"	\
	"\13CapPause"		\
	"\12Cap100T4"		\
	"\11Cap100FD"		\
	"\10Cap100HD"		\
	"\07Cap10FD"		\
	"\06Cap10HD"		\
	"\05Rsrvd"		\
	"\04Rsrvd"		\
	"\03Rsrvd"		\
	"\02Rsrvd"		\
	"\01Cap802.3"

/* 1000Base-T Control Register 0x09 */
#define BCE_1000CTL_PRINTFB	\
	"\020"			\
	"\20Test3"		\
	"\17Test2"		\
	"\16Test1"		\
	"\15MasterSlave"	\
	"\14ForceMaster"	\
	"\13SwitchDev" 		\
	"\12Adv1000TFD"		\
	"\11Adv1000THD"		\
	"\10Rsrvd"		\
	"\07Rsrvd"		\
	"\06Rsrvd"		\
	"\05Rsrvd"		\
	"\04Rsrvd"		\
	"\03Rsrvd"		\
	"\02Rsrvd"		\
	"\01Rsrvd"

/* MII 1000Base-T Status Register 0x0a */
#define BCE_1000STS_PRINTFB	\
	"\020"			\
	"\20MstrSlvFault"	\
	"\17Master"		\
	"\16LclRcvrOk"		\
	"\15RemRcvrOk"		\
	"\14Cap1000FD"		\
	"\13Cpa1000HD"		\
	"\12Rsrvd"		\
	"\11Rsrvd"

/* MII Extended Status Register 0x0f */
#define BCE_EXTSTS_PRINTFB	\
	"\020"			\
	"\20b15"		\
	"\17b14"		\
	"\16b13"		\
	"\15b12"		\
	"\14Rsrvd"		\
	"\13Rsrvd"		\
	"\12Rsrvd"		\
	"\11Rsrvd"		\
	"\10Rsrvd"		\
	"\07Rsrvd"		\
	"\06Rsrvd" 		\
	"\05Rsrvd"		\
	"\04Rsrvd"		\
	"\03Rsrvd"		\
	"\02Rsrvd"		\
	"\01Rsrvd"

/* MII Autoneg Link Partner Ability Register 0x19 */
#define BCE_AUXSTS_PRINTFB	\
	"\020"			\
	"\20AnegCmpl"		\
	"\17AnegCmplAck"	\
	"\16AnegAckDet"		\
	"\15AnegAblDet"		\
	"\14AnegNextPgWait"	\
	"\13HCD"		\
	"\12HCD" 		\
	"\11HCD" 		\
	"\10PrlDetFault"	\
	"\07RemFault"		\
	"\06PgRcvd"		\
	"\05LnkPrtnrAnegAbl"	\
	"\04LnkPrtnrNPAbl"	\
	"\03LnkUp"		\
	"\02EnaPauseRcv"	\
	"\01EnaPausXmit"

/*
 * Remove before release:
 *
 * #define BCE_DEBUG
 * #define BCE_NVRAM_WRITE_SUPPORT
 */

/****************************************************************************/
/* Debugging macros and definitions.                                        */
/****************************************************************************/

#define BCE_CP_LOAD 		0x00000001
#define BCE_CP_SEND		0x00000002
#define BCE_CP_RECV		0x00000004
#define BCE_CP_INTR		0x00000008
#define BCE_CP_UNLOAD		0x00000010
#define BCE_CP_RESET		0x00000020
#define BCE_CP_PHY			0x00000040
#define BCE_CP_NVRAM		0x00000080
#define BCE_CP_FIRMWARE	0x00000100
#define BCE_CP_CTX			0x00000200
#define BCE_CP_REG			0x00000400
#define BCE_CP_MISC		0x00400000
#define BCE_CP_SPECIAL		0x00800000
#define BCE_CP_ALL			0x00FFFFFF

#define BCE_CP_MASK		0x00FFFFFF

#define BCE_LEVEL_FATAL	0x00000000
#define BCE_LEVEL_WARN		0x01000000
#define BCE_LEVEL_INFO		0x02000000
#define BCE_LEVEL_VERBOSE	0x03000000
#define BCE_LEVEL_EXTREME	0x04000000
#define BCE_LEVEL_INSANE	0x05000000

#define BCE_LEVEL_MASK		0xFF000000

#define BCE_WARN_LOAD		(BCE_CP_LOAD | BCE_LEVEL_WARN)
#define BCE_INFO_LOAD		(BCE_CP_LOAD | BCE_LEVEL_INFO)
#define BCE_VERBOSE_LOAD	(BCE_CP_LOAD | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_LOAD	(BCE_CP_LOAD | BCE_LEVEL_EXTREME)
#define BCE_INSANE_LOAD	(BCE_CP_LOAD | BCE_LEVEL_INSANE)

#define BCE_WARN_SEND		(BCE_CP_SEND | BCE_LEVEL_WARN)
#define BCE_INFO_SEND		(BCE_CP_SEND | BCE_LEVEL_INFO)
#define BCE_VERBOSE_SEND	(BCE_CP_SEND | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_SEND	(BCE_CP_SEND | BCE_LEVEL_EXTREME)
#define BCE_INSANE_SEND	(BCE_CP_SEND | BCE_LEVEL_INSANE)

#define BCE_WARN_RECV		(BCE_CP_RECV | BCE_LEVEL_WARN)
#define BCE_INFO_RECV		(BCE_CP_RECV | BCE_LEVEL_INFO)
#define BCE_VERBOSE_RECV	(BCE_CP_RECV | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_RECV	(BCE_CP_RECV | BCE_LEVEL_EXTREME)
#define BCE_INSANE_RECV	(BCE_CP_RECV | BCE_LEVEL_INSANE)

#define BCE_WARN_INTR		(BCE_CP_INTR | BCE_LEVEL_WARN)
#define BCE_INFO_INTR		(BCE_CP_INTR | BCE_LEVEL_INFO)
#define BCE_VERBOSE_INTR	(BCE_CP_INTR | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_INTR	(BCE_CP_INTR | BCE_LEVEL_EXTREME)
#define BCE_INSANE_INTR	(BCE_CP_INTR | BCE_LEVEL_INSANE)

#define BCE_WARN_UNLOAD	(BCE_CP_UNLOAD | BCE_LEVEL_WARN)
#define BCE_INFO_UNLOAD	(BCE_CP_UNLOAD | BCE_LEVEL_INFO)
#define BCE_VERBOSE_UNLOAD	(BCE_CP_UNLOAD | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_UNLOAD	(BCE_CP_UNLOAD | BCE_LEVEL_EXTREME)
#define BCE_INSANE_UNLOAD	(BCE_CP_UNLOAD | BCE_LEVEL_INSANE)

#define BCE_WARN_RESET		(BCE_CP_RESET | BCE_LEVEL_WARN)
#define BCE_INFO_RESET		(BCE_CP_RESET | BCE_LEVEL_INFO)
#define BCE_VERBOSE_RESET	(BCE_CP_RESET | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_RESET	(BCE_CP_RESET | BCE_LEVEL_EXTREME)
#define BCE_INSANE_RESET	(BCE_CP_RESET | BCE_LEVEL_INSANE)

#define BCE_WARN_PHY		(BCE_CP_PHY | BCE_LEVEL_WARN)
#define BCE_INFO_PHY		(BCE_CP_PHY | BCE_LEVEL_INFO)
#define BCE_VERBOSE_PHY	(BCE_CP_PHY | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_PHY	(BCE_CP_PHY | BCE_LEVEL_EXTREME)
#define BCE_INSANE_PHY		(BCE_CP_PHY | BCE_LEVEL_INSANE)

#define BCE_WARN_NVRAM		(BCE_CP_NVRAM | BCE_LEVEL_WARN)
#define BCE_INFO_NVRAM		(BCE_CP_NVRAM | BCE_LEVEL_INFO)
#define BCE_VERBOSE_NVRAM	(BCE_CP_NVRAM | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_NVRAM	(BCE_CP_NVRAM | BCE_LEVEL_EXTREME)
#define BCE_INSANE_NVRAM	(BCE_CP_NVRAM | BCE_LEVEL_INSANE)

#define BCE_WARN_FIRMWARE	(BCE_CP_FIRMWARE | BCE_LEVEL_WARN)
#define BCE_INFO_FIRMWARE	(BCE_CP_FIRMWARE | BCE_LEVEL_INFO)
#define BCE_VERBOSE_FIRMWARE (BCE_CP_FIRMWARE | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_FIRMWARE (BCE_CP_FIRMWARE | BCE_LEVEL_EXTREME)
#define BCE_INSANE_FIRMWARE (BCE_CP_FIRMWARE | BCE_LEVEL_INSANE)

#define BCE_WARN_CTX		(BCE_CP_CTX | BCE_LEVEL_WARN)
#define BCE_INFO_CTX		(BCE_CP_CTX | BCE_LEVEL_INFO)
#define BCE_VERBOSE_CTX	(BCE_CP_CTX | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_CTX	(BCE_CP_CTX | BCE_LEVEL_EXTREME)
#define BCE_INSANE_CTX		(BCE_CP_CTX | BCE_LEVEL_INSANE)

#define BCE_WARN_REG		(BCE_CP_REG | BCE_LEVEL_WARN)
#define BCE_INFO_REG		(BCE_CP_REG | BCE_LEVEL_INFO)
#define BCE_VERBOSE_REG	(BCE_CP_REG | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_REG	(BCE_CP_REG | BCE_LEVEL_EXTREME)
#define BCE_INSANE_REG		(BCE_CP_REG | BCE_LEVEL_INSANE)

#define BCE_WARN_MISC		(BCE_CP_MISC | BCE_LEVEL_WARN)
#define BCE_INFO_MISC		(BCE_CP_MISC | BCE_LEVEL_INFO)
#define BCE_VERBOSE_MISC	(BCE_CP_MISC | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_MISC	(BCE_CP_MISC | BCE_LEVEL_EXTREME)
#define BCE_INSANE_MISC	(BCE_CP_MISC | BCE_LEVEL_INSANE)

#define BCE_WARN_SPECIAL	(BCE_CP_SPECIAL | BCE_LEVEL_WARN)
#define BCE_INFO_SPECIAL	(BCE_CP_SPECIAL | BCE_LEVEL_INFO)
#define BCE_VERBOSE_SPECIAL (BCE_CP_SPECIAL | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME_SPECIAL (BCE_CP_SPECIAL | BCE_LEVEL_EXTREME)
#define BCE_INSANE_SPECIAL	(BCE_CP_SPECIAL | BCE_LEVEL_INSANE)

#define BCE_FATAL			(BCE_CP_ALL | BCE_LEVEL_FATAL)
#define BCE_WARN			(BCE_CP_ALL | BCE_LEVEL_WARN)
#define BCE_INFO			(BCE_CP_ALL | BCE_LEVEL_INFO)
#define BCE_VERBOSE		(BCE_CP_ALL | BCE_LEVEL_VERBOSE)
#define BCE_EXTREME		(BCE_CP_ALL | BCE_LEVEL_EXTREME)
#define BCE_INSANE			(BCE_CP_ALL | BCE_LEVEL_INSANE)

#define BCE_CODE_PATH(cp)	((cp & BCE_CP_MASK) & bce_debug)
#define BCE_MSG_LEVEL(lv)	\
    ((lv & BCE_LEVEL_MASK) <= (bce_debug & BCE_LEVEL_MASK))
#define BCE_LOG_MSG(m)		(BCE_CODE_PATH(m) && BCE_MSG_LEVEL(m))

#ifdef BCE_DEBUG

/* Print a message based on the logging level and code path. */
#define DBPRINT(sc, level, format, args...)			\
	if (BCE_LOG_MSG(level)) {				\
		device_printf(sc->bce_dev, format, ## args);	\
	}

/* Runs a particular command when debugging is enabled. */
#define DBRUN(args...)						\
	do {							\
		args;						\
	} while (0)

/* Runs a particular command based on the logging level and code path. */
#define DBRUNMSG(msg, args...)					\
	if (BCE_LOG_MSG(msg)) {					\
		args;						\
	}

/* Runs a particular command based on the logging level. */
#define DBRUNLV(level, args...) 				\
	if (BCE_MSG_LEVEL(level)) { 				\
		args;						\
	}

/* Runs a particular command based on the code path. */
#define DBRUNCP(cp, args...)					\
	if (BCE_CODE_PATH(cp)) { 				\
		args; 						\
	}

/* Runs a particular command based on a condition. */
#define DBRUNIF(cond, args...)					\
	if (cond) {						\
		args;						\
	}

/* Announces function entry. */
#define DBENTER(cond)						\
	DBPRINT(sc, (cond), "%s(enter)\n", __FUNCTION__)

/* Announces function exit. */
#define DBEXIT(cond)						\
	DBPRINT(sc, (cond), "%s(exit)\n", __FUNCTION__)

/* Temporarily override the debug level. */
#define DBPUSH(cond)						\
	u32 bce_debug_temp = bce_debug;				\
	bce_debug |= cond;

/* Restore the previously overriden debug level. */
#define DBPOP()							\
	bce_debug = bce_debug_temp;

/* Needed for random() function which is only used in debugging. */
#include <sys/random.h>

/* Returns FALSE in "defects" per 2^31 - 1 calls, otherwise returns TRUE. */
#define DB_RANDOMFALSE(defects)        (random() > defects)
#define DB_OR_RANDOMFALSE(defects)  || (random() > defects)
#define DB_AND_RANDOMFALSE(defects) && (random() > defects)

/* Returns TRUE in "defects" per 2^31 - 1 calls, otherwise returns FALSE. */
#define DB_RANDOMTRUE(defects)         (random() < defects)
#define DB_OR_RANDOMTRUE(defects)   || (random() < defects)
#define DB_AND_RANDOMTRUE(defects)  && (random() < defects)

#define DB_PRINT_PHY_REG(reg, val)					\
switch(reg) {								\
case 0x00: DBPRINT(sc, BCE_INSANE_PHY,					\
	"%s(): phy = %d, reg = 0x%04X (BMCR   ), val = 0x%b\n",		\
	__FUNCTION__, phy, (u16) reg & 0xffff, (u16) val & 0xffff,	\
	BCE_BMCR_PRINTFB); break;					\
case 0x01: DBPRINT(sc, BCE_INSANE_PHY,					\
	"%s(): phy = %d, reg = 0x%04X (BMSR   ), val = 0x%b\n",		\
	__FUNCTION__, phy, (u16) reg & 0xffff, (u16) val & 0xffff,	\
	BCE_BMSR_PRINTFB); break;					\
case 0x04: DBPRINT(sc, BCE_INSANE_PHY,					\
	"%s(): phy = %d, reg = 0x%04X (ANAR   ), val = 0x%b\n",		\
	__FUNCTION__, phy, (u16) reg & 0xffff, (u16) val & 0xffff,	\
	BCE_ANAR_PRINTFB); break;					\
case 0x05: DBPRINT(sc, BCE_INSANE_PHY,					\
	"%s(): phy = %d, reg = 0x%04X (ANLPAR ), val = 0x%b\n",		\
	__FUNCTION__, phy, (u16) reg & 0xffff, (u16) val & 0xffff,	\
	BCE_ANLPAR_PRINTFB); break;					\
case 0x09: DBPRINT(sc, BCE_INSANE_PHY,					\
	"%s(): phy = %d, reg = 0x%04X (1000CTL), val = 0x%b\n",		\
	__FUNCTION__, phy, (u16) reg & 0xffff, (u16) val & 0xffff,	\
	BCE_1000CTL_PRINTFB); break;					\
case 0x0a: DBPRINT(sc, BCE_INSANE_PHY,					\
	"%s(): phy = %d, reg = 0x%04X (1000STS), val = 0x%b\n",		\
	__FUNCTION__, phy, (u16) reg & 0xffff, (u16) val & 0xffff,	\
	BCE_1000STS_PRINTFB); break;					\
case 0x0f: DBPRINT(sc, BCE_INSANE_PHY,					\
	"%s(): phy = %d, reg = 0x%04X (EXTSTS ), val = 0x%b\n",		\
	__FUNCTION__, phy, (u16) reg & 0xffff, (u16) val & 0xffff,	\
	BCE_EXTSTS_PRINTFB); break;					\
case 0x19: DBPRINT(sc, BCE_INSANE_PHY,					\
	"%s(): phy = %d, reg = 0x%04X (AUXSTS ), val = 0x%b\n",		\
	__FUNCTION__, phy, (u16) reg & 0xffff, (u16) val & 0xffff,	\
	BCE_AUXSTS_PRINTFB); break;					\
default: DBPRINT(sc, BCE_INSANE_PHY,					\
	"%s(): phy = %d, reg = 0x%04X, val = 0x%04X\n",			\
	__FUNCTION__, phy, (u16) reg & 0xffff, (u16) val & 0xffff);	\
	}

#else

#define DBPRINT(level, format, args...)
#define DBRUN(args...)
#define DBRUNMSG(msg, args...)
#define DBRUNLV(level, args...)
#define DBRUNCP(cp, args...)
#define DBRUNIF(cond, args...)
#define DBENTER(cond)
#define DBEXIT(cond)
#define DBPUSH(cond)
#define DBPOP()
#define DB_RANDOMFALSE(defects)
#define DB_OR_RANDOMFALSE(percent)
#define DB_AND_RANDOMFALSE(percent)
#define DB_RANDOMTRUE(defects)
#define DB_OR_RANDOMTRUE(percent)
#define DB_AND_RANDOMTRUE(percent)
#define DB_PRINT_PHY_REG(reg, val)

#endif /* BCE_DEBUG */


#if __FreeBSD_version < 800054
#if defined(__i386__) || defined(__amd64__)
#define mb()    __asm volatile("mfence" ::: "memory")
#define wmb()   __asm volatile("sfence" ::: "memory")
#define rmb()   __asm volatile("lfence" ::: "memory")
#else
#define mb()
#define rmb()
#define wmb()
#endif
#endif

/****************************************************************************/
/* Device identification definitions.                                       */
/****************************************************************************/
#define BRCM_VENDORID				0x14E4
#define BRCM_DEVICEID_BCM5706			0x164A
#define BRCM_DEVICEID_BCM5706S			0x16AA
#define BRCM_DEVICEID_BCM5708			0x164C
#define BRCM_DEVICEID_BCM5708S			0x16AC
#define BRCM_DEVICEID_BCM5709			0x1639
#define BRCM_DEVICEID_BCM5709S			0x163A
#define BRCM_DEVICEID_BCM5716			0x163B

#define HP_VENDORID				0x103C

#define PCI_ANY_ID				(u_int16_t) (~0U)

/* chip num:16-31, rev:12-15, metal:4-11, bond_id:0-3 */

#define BCE_CHIP_NUM(sc)		(((sc)->bce_chipid) & 0xffff0000)
#define BCE_CHIP_NUM_5706		0x57060000
#define BCE_CHIP_NUM_5708		0x57080000
#define BCE_CHIP_NUM_5709		0x57090000

#define BCE_CHIP_REV(sc)		(((sc)->bce_chipid) & 0x0000f000)
#define BCE_CHIP_REV_Ax			0x00000000
#define BCE_CHIP_REV_Bx			0x00001000
#define BCE_CHIP_REV_Cx			0x00002000

#define BCE_CHIP_METAL(sc)		(((sc)->bce_chipid) & 0x00000ff0)
#define BCE_CHIP_BOND(bp)		(((sc)->bce_chipid) & 0x0000000f)

#define BCE_CHIP_ID(sc)			(((sc)->bce_chipid) & 0xfffffff0)
#define BCE_CHIP_ID_5706_A0		0x57060000
#define BCE_CHIP_ID_5706_A1		0x57060010
#define BCE_CHIP_ID_5706_A2		0x57060020
#define BCE_CHIP_ID_5706_A3		0x57060030
#define BCE_CHIP_ID_5708_A0		0x57080000
#define BCE_CHIP_ID_5708_B0		0x57081000
#define BCE_CHIP_ID_5708_B1		0x57081010
#define BCE_CHIP_ID_5708_B2		0x57081020
#define BCE_CHIP_ID_5709_A0		0x57090000
#define BCE_CHIP_ID_5709_A1		0x57090010
#define BCE_CHIP_ID_5709_B0		0x57091000
#define BCE_CHIP_ID_5709_B1		0x57091010
#define BCE_CHIP_ID_5709_B2		0x57091020
#define BCE_CHIP_ID_5709_C0		0x57092000

#define BCE_CHIP_BOND_ID(sc)		(((sc)->bce_chipid) & 0xf)

/* A serdes chip will have the first bit of the bond id set. */
#define BCE_CHIP_BOND_ID_SERDES_BIT	0x01


/* shorthand one */
#define BCE_ASICREV(x)			((x) >> 28)
#define BCE_ASICREV_BCM5700		0x06

/* chip revisions */
#define BCE_CHIPREV(x)			((x) >> 24)
#define BCE_CHIPREV_5700_AX		0x70
#define BCE_CHIPREV_5700_BX		0x71
#define BCE_CHIPREV_5700_CX		0x72
#define BCE_CHIPREV_5701_AX		0x00

struct bce_type {
	u_int16_t bce_vid;
	u_int16_t bce_did;
	u_int16_t bce_svid;
	u_int16_t bce_sdid;
	const char *bce_name;
};

/****************************************************************************/
/* Byte order conversions.                                                  */
/****************************************************************************/
#if __FreeBSD_version >= 500000
#define bce_htobe16(x) htobe16(x)
#define bce_htobe32(x) htobe32(x)
#define bce_htobe64(x) htobe64(x)
#define bce_htole16(x) htole16(x)
#define bce_htole32(x) htole32(x)
#define bce_htole64(x) htole64(x)

#define bce_be16toh(x) be16toh(x)
#define bce_be32toh(x) be32toh(x)
#define bce_be64toh(x) be64toh(x)
#define bce_le16toh(x) le16toh(x)
#define bce_le32toh(x) le32toh(x)
#define bce_le64toh(x) le64toh(x)
#else
#define bce_htobe16(x) (x)
#define bce_htobe32(x) (x)
#define bce_htobe64(x) (x)
#define bce_htole16(x) (x)
#define bce_htole32(x) (x)
#define bce_htole64(x) (x)

#define bce_be16toh(x) (x)
#define bce_be32toh(x) (x)
#define bce_be64toh(x) (x)
#define bce_le16toh(x) (x)
#define bce_le32toh(x) (x)
#define bce_le64toh(x) (x)
#endif


/****************************************************************************/
/* NVRAM Access                                                             */
/****************************************************************************/

/* Buffered flash (Atmel: AT45DB011B) specific information */
#define SEEPROM_PAGE_BITS		2
#define SEEPROM_PHY_PAGE_SIZE		(1 << SEEPROM_PAGE_BITS)
#define SEEPROM_BYTE_ADDR_MASK		(SEEPROM_PHY_PAGE_SIZE-1)
#define SEEPROM_PAGE_SIZE		4
#define SEEPROM_TOTAL_SIZE		65536

#define BUFFERED_FLASH_PAGE_BITS	9
#define BUFFERED_FLASH_PHY_PAGE_SIZE	(1 << BUFFERED_FLASH_PAGE_BITS)
#define BUFFERED_FLASH_BYTE_ADDR_MASK	(BUFFERED_FLASH_PHY_PAGE_SIZE-1)
#define BUFFERED_FLASH_PAGE_SIZE	264
#define BUFFERED_FLASH_TOTAL_SIZE	0x21000

#define SAIFUN_FLASH_PAGE_BITS		8
#define SAIFUN_FLASH_PHY_PAGE_SIZE	(1 << SAIFUN_FLASH_PAGE_BITS)
#define SAIFUN_FLASH_BYTE_ADDR_MASK	(SAIFUN_FLASH_PHY_PAGE_SIZE-1)
#define SAIFUN_FLASH_PAGE_SIZE		256
#define SAIFUN_FLASH_BASE_TOTAL_SIZE	65536

#define ST_MICRO_FLASH_PAGE_BITS	8
#define ST_MICRO_FLASH_PHY_PAGE_SIZE	(1 << ST_MICRO_FLASH_PAGE_BITS)
#define ST_MICRO_FLASH_BYTE_ADDR_MASK	(ST_MICRO_FLASH_PHY_PAGE_SIZE-1)
#define ST_MICRO_FLASH_PAGE_SIZE	256
#define ST_MICRO_FLASH_BASE_TOTAL_SIZE	65536

#define BCM5709_FLASH_PAGE_BITS		8
#define BCM5709_FLASH_PHY_PAGE_SIZE	(1 << BCM5709_FLASH_PAGE_BITS)
#define BCM5709_FLASH_BYTE_ADDR_MASK	(BCM5709_FLASH_PHY_PAGE_SIZE-1)
#define BCM5709_FLASH_PAGE_SIZE		256

#define NVRAM_TIMEOUT_COUNT		30000
#define BCE_FLASHDESC_MAX		64

#define FLASH_STRAP_MASK	(BCE_NVM_CFG1_FLASH_MODE |	\
    BCE_NVM_CFG1_BUFFER_MODE | BCE_NVM_CFG1_PROTECT_MODE |	\
    BCE_NVM_CFG1_FLASH_SIZE)

#define FLASH_BACKUP_STRAP_MASK		(0xf << 26)

struct flash_spec {
	u32 strapping;
	u32 config1;
	u32 config2;
	u32 config3;
	u32 write1;
#define BCE_NV_BUFFERED		0x00000001
#define BCE_NV_TRANSLATE	0x00000002
#define BCE_NV_WREN		0x00000004
	u32 flags;
	u32 page_bits;
	u32 page_size;
	u32 addr_mask;
	u32 total_size;
	const u8 *name;
};


/****************************************************************************/
/* Shared Memory layout                                                     */
/* The BCE bootcode will initialize this data area with port configurtion   */
/* information which can be accessed by the driver.                         */
/****************************************************************************/

/*
 * This value (in milliseconds) determines the frequency of the driver
 * issuing the PULSE message code.  The firmware monitors this periodic
 * pulse to determine when to switch to an OS-absent mode.
 */
#define DRV_PULSE_PERIOD_MS                 250

/*
 * This value (in milliseconds) determines how long the driver should
 * wait for an acknowledgement from the firmware before timing out.  Once
 * the firmware has timed out, the driver will assume there is no firmware
 * running and there won't be any firmware-driver synchronization during a
 * driver reset.
 */
#define FW_ACK_TIME_OUT_MS			1000


#define BCE_DRV_RESET_SIGNATURE			0x00000000
#define BCE_DRV_RESET_SIGNATURE_MAGIC		0x4841564b /* HAVK */

#define BCE_DRV_MB				0x00000004
#define BCE_DRV_MSG_CODE	 		0xff000000
#define BCE_DRV_MSG_CODE_RESET		 	0x01000000
#define BCE_DRV_MSG_CODE_UNLOAD			0x02000000
#define BCE_DRV_MSG_CODE_SHUTDOWN	 	0x03000000
#define BCE_DRV_MSG_CODE_SUSPEND_WOL		0x04000000
#define BCE_DRV_MSG_CODE_FW_TIMEOUT	 	0x05000000
#define BCE_DRV_MSG_CODE_PULSE		 	0x06000000
#define BCE_DRV_MSG_CODE_DIAG		 	0x07000000
#define BCE_DRV_MSG_CODE_SUSPEND_NO_WOL	 	0x09000000
#define BCE_DRV_MSG_CODE_UNLOAD_LNK_DN		0x0b000000
#define BCE_DRV_MSG_CODE_CMD_SET_LINK		0x10000000

#define BCE_DRV_MSG_DATA			0x00ff0000
#define BCE_DRV_MSG_DATA_WAIT0		 	0x00010000
#define BCE_DRV_MSG_DATA_WAIT1			0x00020000
#define BCE_DRV_MSG_DATA_WAIT2			0x00030000
#define BCE_DRV_MSG_DATA_WAIT3			0x00040000

#define BCE_DRV_MSG_SEQ				0x0000ffff

#define BCE_FW_MB				0x00000008
#define BCE_FW_MSG_ACK				 0x0000ffff
#define BCE_FW_MSG_STATUS_MASK			 0x00ff0000
#define BCE_FW_MSG_STATUS_OK			 0x00000000
#define BCE_FW_MSG_STATUS_INVALID_ARGS		 0x00010000
#define BCE_FW_MSG_STATUS_DRV_PRSNT		 0x00020000
#define BCE_FW_MSG_STATUS_FAILURE		 0x00ff0000

#define BCE_LINK_STATUS				0x0000000c
#define BCE_LINK_STATUS_INIT_VALUE		 0xffffffff
#define BCE_LINK_STATUS_LINK_UP		 	 0x1
#define BCE_LINK_STATUS_LINK_DOWN		 0x0
#define BCE_LINK_STATUS_SPEED_MASK		 0x1e
#define BCE_LINK_STATUS_AN_INCOMPLETE		 (0<<1)
#define BCE_LINK_STATUS_10HALF			 (1<<1)
#define BCE_LINK_STATUS_10FULL			 (2<<1)
#define BCE_LINK_STATUS_100HALF			 (3<<1)
#define BCE_LINK_STATUS_100BASE_T4		 (4<<1)
#define BCE_LINK_STATUS_100FULL			 (5<<1)
#define BCE_LINK_STATUS_1000HALF		 (6<<1)
#define BCE_LINK_STATUS_1000FULL		 (7<<1)
#define BCE_LINK_STATUS_2500HALF		 (8<<1)
#define BCE_LINK_STATUS_2500FULL		 (9<<1)
#define BCE_LINK_STATUS_AN_ENABLED		 (1<<5)
#define BCE_LINK_STATUS_AN_COMPLETE		 (1<<6)
#define BCE_LINK_STATUS_PARALLEL_DET		 (1<<7)
#define BCE_LINK_STATUS_RESERVED		 (1<<8)
#define BCE_LINK_STATUS_PARTNER_AD_1000FULL	 (1<<9)
#define BCE_LINK_STATUS_PARTNER_AD_1000HALF	 (1<<10)
#define BCE_LINK_STATUS_PARTNER_AD_100BT4	 (1<<11)
#define BCE_LINK_STATUS_PARTNER_AD_100FULL	 (1<<12)
#define BCE_LINK_STATUS_PARTNER_AD_100HALF	 (1<<13)
#define BCE_LINK_STATUS_PARTNER_AD_10FULL	 (1<<14)
#define BCE_LINK_STATUS_PARTNER_AD_10HALF	 (1<<15)
#define BCE_LINK_STATUS_TX_FC_ENABLED		 (1<<16)
#define BCE_LINK_STATUS_RX_FC_ENABLED		 (1<<17)
#define BCE_LINK_STATUS_PARTNER_SYM_PAUSE_CAP	 (1<<18)
#define BCE_LINK_STATUS_PARTNER_ASYM_PAUSE_CAP	 (1<<19)
#define BCE_LINK_STATUS_SERDES_LINK		 (1<<20)
#define BCE_LINK_STATUS_PARTNER_AD_2500FULL	 (1<<21)
#define BCE_LINK_STATUS_PARTNER_AD_2500HALF	 (1<<22)

#define BCE_DRV_PULSE_MB			0x00000010
#define BCE_DRV_PULSE_SEQ_MASK			 0x00007fff

#define BCE_MB_ARGS_0				0x00000014
#define	BCE_NETLINK_SPEED_10HALF		 (1<<0)
#define	BCE_NETLINK_SPEED_10FULL		 (1<<1)
#define	BCE_NETLINK_SPEED_100HALF		 (1<<2)
#define	BCE_NETLINK_SPEED_100FULL		 (1<<3)
#define	BCE_NETLINK_SPEED_1000HALF		 (1<<4)
#define	BCE_NETLINK_SPEED_1000FULL		 (1<<5)
#define	BCE_NETLINK_SPEED_2500HALF		 (1<<6)
#define	BCE_NETLINK_SPEED_2500FULL		 (1<<7)
#define	BCE_NETLINK_SPEED_10GHALF		 (1<<8)
#define	BCE_NETLINK_SPEED_10GFULL		 (1<<9)
#define	BCE_NETLINK_ANEG_ENB		 	 (1<<10)
#define	BCE_NETLINK_PHY_APP_REMOTE	 	 (1<<11)
#define	BCE_NETLINK_FC_PAUSE_SYM	 	 (1<<12)
#define	BCE_NETLINK_FC_PAUSE_ASYM	 	 (1<<13)
#define	BCE_NETLINK_ETH_AT_WIRESPEED	 	 (1<<14)
#define	BCE_NETLINK_PHY_RESET	 	 	 (1<<15)

#define BCE_MB_ARGS_1				0x00000018

/* Indicate to the firmware not to go into the
 * OS absent when it is not getting driver pulse.
 * This is used for debugging. */
#define BCE_DRV_MSG_DATA_PULSE_CODE_ALWAYS_ALIVE	 0x00080000

#define BCE_DEV_INFO_SIGNATURE			0x00000020
#define BCE_DEV_INFO_SIGNATURE_MAGIC		 0x44564900
#define BCE_DEV_INFO_SIGNATURE_MAGIC_MASK	 0xffffff00
#define BCE_DEV_INFO_FEATURE_CFG_VALID		 0x01
#define BCE_DEV_INFO_SECONDARY_PORT		 0x80
#define BCE_DEV_INFO_DRV_ALWAYS_ALIVE		 0x40

#define BCE_SHARED_HW_CFG_PART_NUM		0x00000024

#define BCE_SHARED_HW_CFG_POWER_DISSIPATED	0x00000034
#define BCE_SHARED_HW_CFG_POWER_STATE_D3_MASK	 0xff000000
#define BCE_SHARED_HW_CFG_POWER_STATE_D2_MASK	 0xff0000
#define BCE_SHARED_HW_CFG_POWER_STATE_D1_MASK	 0xff00
#define BCE_SHARED_HW_CFG_POWER_STATE_D0_MASK	 0xff

#define BCE_SHARED_HW_CFG_POWER_CONSUMED	0x00000038
#define BCE_SHARED_HW_CFG_CONFIG		0x0000003c
#define BCE_SHARED_HW_CFG_DESIGN_NIC		 0
#define BCE_SHARED_HW_CFG_DESIGN_LOM		 0x1
#define BCE_SHARED_HW_CFG_PHY_COPPER		 0
#define BCE_SHARED_HW_CFG_PHY_FIBER		 0x2
#define BCE_SHARED_HW_CFG_PHY_2_5G		 0x20
#define BCE_SHARED_HW_CFG_PHY_BACKPLANE		 0x40
#define BCE_SHARED_HW_CFG_LED_MODE_SHIFT_BITS	 8
#define BCE_SHARED_HW_CFG_LED_MODE_MASK		 0x300
#define BCE_SHARED_HW_CFG_LED_MODE_MAC		 0
#define BCE_SHARED_HW_CFG_LED_MODE_GPHY1	 0x100
#define BCE_SHARED_HW_CFG_LED_MODE_GPHY2	 0x200

#define BCE_SHARED_HW_CFG_CONFIG2		0x00000040
#define BCE_SHARED_HW_CFG2_NVM_SIZE_MASK	 0x00fff000

#define BCE_DEV_INFO_BC_REV			0x0000004c

#define BCE_PORT_HW_CFG_MAC_UPPER		0x00000050
#define BCE_PORT_HW_CFG_UPPERMAC_MASK		 0xffff

#define BCE_PORT_HW_CFG_MAC_LOWER		0x00000054
#define BCE_PORT_HW_CFG_CONFIG			0x00000058
#define BCE_PORT_HW_CFG_CFG_TXCTL3_MASK		 0x0000ffff
#define BCE_PORT_HW_CFG_CFG_DFLT_LINK_MASK	 0x001f0000
#define BCE_PORT_HW_CFG_CFG_DFLT_LINK_AN	 0x00000000
#define BCE_PORT_HW_CFG_CFG_DFLT_LINK_1G	 0x00030000
#define BCE_PORT_HW_CFG_CFG_DFLT_LINK_2_5G	 0x00040000

#define BCE_PORT_HW_CFG_IMD_MAC_A_UPPER		0x00000068
#define BCE_PORT_HW_CFG_IMD_MAC_A_LOWER		0x0000006c
#define BCE_PORT_HW_CFG_IMD_MAC_B_UPPER		0x00000070
#define BCE_PORT_HW_CFG_IMD_MAC_B_LOWER		0x00000074
#define BCE_PORT_HW_CFG_ISCSI_MAC_UPPER		0x00000078
#define BCE_PORT_HW_CFG_ISCSI_MAC_LOWER		0x0000007c

#define BCE_DEV_INFO_PER_PORT_HW_CONFIG2	0x000000b4

#define BCE_DEV_INFO_FORMAT_REV			0x000000c4
#define BCE_DEV_INFO_FORMAT_REV_MASK		 0xff000000
#define BCE_DEV_INFO_FORMAT_REV_ID		 ('A' << 24)

#define BCE_SHARED_FEATURE			0x000000c8
#define BCE_SHARED_FEATURE_MASK			 0xffffffff

#define BCE_PORT_FEATURE			0x000000d8
#define BCE_PORT2_FEATURE			0x00000014c
#define BCE_PORT_FEATURE_WOL_ENABLED		 0x01000000
#define BCE_PORT_FEATURE_MBA_ENABLED		 0x02000000
#define BCE_PORT_FEATURE_ASF_ENABLED		 0x04000000
#define BCE_PORT_FEATURE_IMD_ENABLED		 0x08000000
#define BCE_PORT_FEATURE_BAR1_SIZE_MASK		 0xf
#define BCE_PORT_FEATURE_BAR1_SIZE_DISABLED	 0x0
#define BCE_PORT_FEATURE_BAR1_SIZE_64K		 0x1
#define BCE_PORT_FEATURE_BAR1_SIZE_128K		 0x2
#define BCE_PORT_FEATURE_BAR1_SIZE_256K		 0x3
#define BCE_PORT_FEATURE_BAR1_SIZE_512K		 0x4
#define BCE_PORT_FEATURE_BAR1_SIZE_1M		 0x5
#define BCE_PORT_FEATURE_BAR1_SIZE_2M		 0x6
#define BCE_PORT_FEATURE_BAR1_SIZE_4M		 0x7
#define BCE_PORT_FEATURE_BAR1_SIZE_8M		 0x8
#define BCE_PORT_FEATURE_BAR1_SIZE_16M		 0x9
#define BCE_PORT_FEATURE_BAR1_SIZE_32M		 0xa
#define BCE_PORT_FEATURE_BAR1_SIZE_64M		 0xb
#define BCE_PORT_FEATURE_BAR1_SIZE_128M		 0xc
#define BCE_PORT_FEATURE_BAR1_SIZE_256M		 0xd
#define BCE_PORT_FEATURE_BAR1_SIZE_512M		 0xe
#define BCE_PORT_FEATURE_BAR1_SIZE_1G		 0xf

#define BCE_PORT_FEATURE_WOL			0xdc
#define BCE_PORT2_FEATURE_WOL			0x150
#define BCE_PORT_FEATURE_WOL_DEFAULT_SHIFT_BITS	 4
#define BCE_PORT_FEATURE_WOL_DEFAULT_MASK	 0x30
#define BCE_PORT_FEATURE_WOL_DEFAULT_DISABLE	 0
#define BCE_PORT_FEATURE_WOL_DEFAULT_MAGIC	 0x10
#define BCE_PORT_FEATURE_WOL_DEFAULT_ACPI	 0x20
#define BCE_PORT_FEATURE_WOL_DEFAULT_MAGIC_AND_ACPI	 0x30
#define BCE_PORT_FEATURE_WOL_LINK_SPEED_MASK	 0xf
#define BCE_PORT_FEATURE_WOL_LINK_SPEED_AUTONEG	 0
#define BCE_PORT_FEATURE_WOL_LINK_SPEED_10HALF	 1
#define BCE_PORT_FEATURE_WOL_LINK_SPEED_10FULL	 2
#define BCE_PORT_FEATURE_WOL_LINK_SPEED_100HALF	 3
#define BCE_PORT_FEATURE_WOL_LINK_SPEED_100FULL	 4
#define BCE_PORT_FEATURE_WOL_LINK_SPEED_1000HALF	 5
#define BCE_PORT_FEATURE_WOL_LINK_SPEED_1000FULL	 6
#define BCE_PORT_FEATURE_WOL_AUTONEG_ADVERTISE_1000	 0x40
#define BCE_PORT_FEATURE_WOL_RESERVED_PAUSE_CAP	 0x400
#define BCE_PORT_FEATURE_WOL_RESERVED_ASYM_PAUSE_CAP	 0x800

#define BCE_PORT_FEATURE_MBA			0xe0
#define BCE_PORT2_FEATURE_MBA			0x154
#define BCE_PORT_FEATURE_MBA_BOOT_AGENT_TYPE_SHIFT_BITS	 0
#define BCE_PORT_FEATURE_MBA_BOOT_AGENT_TYPE_MASK	 0x3
#define BCE_PORT_FEATURE_MBA_BOOT_AGENT_TYPE_PXE	 0
#define BCE_PORT_FEATURE_MBA_BOOT_AGENT_TYPE_RPL	 1
#define BCE_PORT_FEATURE_MBA_BOOT_AGENT_TYPE_BOOTP	 2
#define BCE_PORT_FEATURE_MBA_LINK_SPEED_SHIFT_BITS	 2
#define BCE_PORT_FEATURE_MBA_LINK_SPEED_MASK	 0x3c
#define BCE_PORT_FEATURE_MBA_LINK_SPEED_AUTONEG	 0
#define BCE_PORT_FEATURE_MBA_LINK_SPEED_10HALF	 0x4
#define BCE_PORT_FEATURE_MBA_LINK_SPEED_10FULL	 0x8
#define BCE_PORT_FEATURE_MBA_LINK_SPEED_100HALF	 0xc
#define BCE_PORT_FEATURE_MBA_LINK_SPEED_100FULL	 0x10
#define BCE_PORT_FEATURE_MBA_LINK_SPEED_1000HALF 0x14
#define BCE_PORT_FEATURE_MBA_LINK_SPEED_1000FULL 0x18
#define BCE_PORT_FEATURE_MBA_SETUP_PROMPT_ENABLE 0x40
#define BCE_PORT_FEATURE_MBA_HOTKEY_CTRL_S	 0
#define BCE_PORT_FEATURE_MBA_HOTKEY_CTRL_B	 0x80
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_SHIFT_BITS	 8
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_MASK	 0xff00
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_DISABLED	 0
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_1K	 0x100
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_2K	 0x200
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_4K	 0x300
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_8K	 0x400
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_16K	 0x500
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_32K	 0x600
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_64K	 0x700
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_128K	 0x800
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_256K	 0x900
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_512K	 0xa00
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_1M	 0xb00
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_2M	 0xc00
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_4M	 0xd00
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_8M	 0xe00
#define BCE_PORT_FEATURE_MBA_EXP_ROM_SIZE_16M	 0xf00
#define BCE_PORT_FEATURE_MBA_MSG_TIMEOUT_SHIFT_BITS	 16
#define BCE_PORT_FEATURE_MBA_MSG_TIMEOUT_MASK	 0xf0000
#define BCE_PORT_FEATURE_MBA_BIOS_BOOTSTRAP_SHIFT_BITS	 20
#define BCE_PORT_FEATURE_MBA_BIOS_BOOTSTRAP_MASK	 0x300000
#define BCE_PORT_FEATURE_MBA_BIOS_BOOTSTRAP_AUTO	 0
#define BCE_PORT_FEATURE_MBA_BIOS_BOOTSTRAP_BBS	 0x100000
#define BCE_PORT_FEATURE_MBA_BIOS_BOOTSTRAP_INT18H	 0x200000
#define BCE_PORT_FEATURE_MBA_BIOS_BOOTSTRAP_INT19H	 0x300000

#define BCE_PORT_FEATURE_IMD			0xe4
#define BCE_PORT2_FEATURE_IMD			0x158
#define BCE_PORT_FEATURE_IMD_LINK_OVERRIDE_DEFAULT	 0
#define BCE_PORT_FEATURE_IMD_LINK_OVERRIDE_ENABLE	 1

#define BCE_PORT_FEATURE_VLAN			0xe8
#define BCE_PORT2_FEATURE_VLAN			0x15c
#define BCE_PORT_FEATURE_MBA_VLAN_TAG_MASK	 0xffff
#define BCE_PORT_FEATURE_MBA_VLAN_ENABLE	 0x10000

#define BCE_MFW_VER_PTR				0x00000014c

#define BCE_BC_STATE_RESET_TYPE			0x000001c0
#define BCE_BC_STATE_RESET_TYPE_SIG		 0x00005254
#define BCE_BC_STATE_RESET_TYPE_SIG_MASK	 0x0000ffff

#define BCE_BC_STATE_RESET_TYPE_NONE 			\
    (BCE_BC_STATE_RESET_TYPE_SIG | 0x00010000)
#define BCE_BC_STATE_RESET_TYPE_PCI			\
    (BCE_BC_STATE_RESET_TYPE_SIG | 0x00020000)
#define BCE_BC_STATE_RESET_TYPE_VAUX			\
    (BCE_BC_STATE_RESET_TYPE_SIG | 0x00030000)
#define BCE_BC_STATE_RESET_TYPE_DRV_MASK DRV_MSG_CODE
#define BCE_BC_STATE_RESET_TYPE_DRV_RESET		\
    (BCE_BC_STATE_RESET_TYPE_SIG | DRV_MSG_CODE_RESET)
#define BCE_BC_STATE_RESET_TYPE_DRV_UNLOAD		\
    (BCE_BC_STATE_RESET_TYPE_SIG | DRV_MSG_CODE_UNLOAD)
#define BCE_BC_STATE_RESET_TYPE_DRV_SHUTDOWN		\
    (BCE_BC_STATE_RESET_TYPE_SIG | DRV_MSG_CODE_SHUTDOWN)
#define BCE_BC_STATE_RESET_TYPE_DRV_WOL			\
    (BCE_BC_STATE_RESET_TYPE_SIG | DRV_MSG_CODE_WOL)
#define BCE_BC_STATE_RESET_TYPE_DRV_DIAG		\
    (BCE_BC_STATE_RESET_TYPE_SIG | DRV_MSG_CODE_DIAG)
#define BCE_BC_STATE_RESET_TYPE_VALUE(msg)		\
    (BCE_BC_STATE_RESET_TYPE_SIG | (msg))

#define BCE_BC_RESET_TYPE			0x000001c0

#define BCE_BC_STATE				0x000001c4
#define BCE_BC_STATE_ERR_MASK			0x0000ff00
#define BCE_BC_STATE_SIGN			0x42530000
#define BCE_BC_STATE_SIGN_MASK			0xffff0000
#define BCE_BC_STATE_BC1_START			(BCE_BC_STATE_SIGN | 0x1)
#define BCE_BC_STATE_GET_NVM_CFG1		(BCE_BC_STATE_SIGN | 0x2)
#define BCE_BC_STATE_PROG_BAR			(BCE_BC_STATE_SIGN | 0x3)
#define BCE_BC_STATE_INIT_VID			(BCE_BC_STATE_SIGN | 0x4)
#define BCE_BC_STATE_GET_NVM_CFG2		(BCE_BC_STATE_SIGN | 0x5)
#define BCE_BC_STATE_APPLY_WKARND		(BCE_BC_STATE_SIGN | 0x6)
#define BCE_BC_STATE_LOAD_BC2			(BCE_BC_STATE_SIGN | 0x7)
#define BCE_BC_STATE_GOING_BC2			(BCE_BC_STATE_SIGN | 0x8)
#define BCE_BC_STATE_GOING_DIAG			(BCE_BC_STATE_SIGN | 0x9)
#define BCE_BC_STATE_RT_FINAL_INIT		(BCE_BC_STATE_SIGN | 0x81)
#define BCE_BC_STATE_RT_WKARND			(BCE_BC_STATE_SIGN | 0x82)
#define BCE_BC_STATE_RT_DRV_PULSE		(BCE_BC_STATE_SIGN | 0x83)
#define BCE_BC_STATE_RT_FIOEVTS			(BCE_BC_STATE_SIGN | 0x84)
#define BCE_BC_STATE_RT_DRV_CMD			(BCE_BC_STATE_SIGN | 0x85)
#define BCE_BC_STATE_RT_LOW_POWER		(BCE_BC_STATE_SIGN | 0x86)
#define BCE_BC_STATE_RT_SET_WOL			(BCE_BC_STATE_SIGN | 0x87)
#define BCE_BC_STATE_RT_OTHER_FW		(BCE_BC_STATE_SIGN | 0x88)
#define BCE_BC_STATE_RT_GOING_D3		(BCE_BC_STATE_SIGN | 0x89)
#define BCE_BC_STATE_ERR_BAD_VERSION		(BCE_BC_STATE_SIGN | 0x0100)
#define BCE_BC_STATE_ERR_BAD_BC2_CRC		(BCE_BC_STATE_SIGN | 0x0200)
#define BCE_BC_STATE_ERR_BC1_LOOP		(BCE_BC_STATE_SIGN | 0x0300)
#define BCE_BC_STATE_ERR_UNKNOWN_CMD		(BCE_BC_STATE_SIGN | 0x0400)
#define BCE_BC_STATE_ERR_DRV_DEAD		(BCE_BC_STATE_SIGN | 0x0500)
#define BCE_BC_STATE_ERR_NO_RXP			(BCE_BC_STATE_SIGN | 0x0600)
#define BCE_BC_STATE_ERR_TOO_MANY_RBUF		(BCE_BC_STATE_SIGN | 0x0700)

#define BCE_BC_STATE_CONDITION			0x000001c8
#define BCE_CONDITION_INIT_POR			0x00000001
#define BCE_CONDITION_INIT_VAUX_AVAIL		0x00000002
#define BCE_CONDITION_INIT_PCI_AVAIL		0x00000004
#define BCE_CONDITION_INIT_PCI_RESET		0x00000008
#define BCE_CONDITION_INIT_HD_RESET		0x00000010 /* 5709/16 only */
#define BCE_CONDITION_DRV_PRESENT		0x00000100
#define BCE_CONDITION_LOW_POWER_LINK		0x00000200
#define BCE_CONDITION_CORE_RST_OCCURRED		0x00000400 /* 5709/16 only */
#define BCE_CONDITION_UNUSED			0x00000800
#define BCE_CONDITION_BUSY_EXPROM		0x00001000 /* 5706/08 only */

#define BCE_CONDITION_MFW_RUN_UNKNOWN		0x00000000
#define BCE_CONDITION_MFW_RUN_IPMI		0x00002000
#define BCE_CONDITION_MFW_RUN_UMP		0x00004000
#define BCE_CONDITION_MFW_RUN_NCSI		0x00006000
#define BCE_CONDITION_MFW_RUN_NONE		0x0000e000
#define BCE_CONDITION_MFW_RUN_MASK		0x0000e000

/* 5709/16 only */
#define BCE_CONDITION_PM_STATE_MASK		0x00030000
#define BCE_CONDITION_PM_STATE_FULL		0x00030000
#define BCE_CONDITION_PM_STATE_PREP		0x00020000
#define BCE_CONDITION_PM_STATE_UNPREP		0x00010000
#define BCE_CONDITION_PM_RESERVED		0x00000000

/* 5709/16 only */
#define BCE_CONDITION_RXMODE_KEEP_VLAN		0x00040000
#define BCE_CONDITION_DRV_WOL_ENABLED		0x00080000
#define BCE_CONDITION_PORT_DISABLED		0x00100000
#define BCE_CONDITION_DRV_MAYBE_OUT		0x00200000
#define BCE_CONDITION_DPFW_DEAD			0x00400000

#define BCE_BC_STATE_DEBUG_CMD			0x000001dc
#define BCE_BC_STATE_BC_DBG_CMD_SIGNATURE	0x42440000
#define BCE_BC_STATE_BC_DBG_CMD_SIGNATURE_MASK	0xffff0000
#define BCE_BC_STATE_BC_DBG_CMD_LOOP_CNT_MASK	0xffff
#define BCE_BC_STATE_BC_DBG_CMD_LOOP_INFINITE	0xffff

#define	BCE_FW_EVT_CODE_MB			0x00000354
#define	BCE_FW_EVT_CODE_SW_TIMER_EXPIRE_EVENT	0x00000000
#define	BCE_FW_EVT_CODE_LINK_EVENT		0x00000001

#define	BCE_DRV_ACK_CAP_MB			0x00000364
#define	BCE_DRV_ACK_CAP_SIGNATURE_MAGIC		0x35450000

#define	BCE_FW_CAP_MB				0x00000368
#define	BCE_FW_CAP_SIGNATURE_MAGIC		0xaa550000
#define	BCE_FW_ACK_SIGNATURE_MAGIC		0x52500000
#define	BCE_FW_CAP_SIGNATURE_MAGIC_MASK		0xffff0000
#define	BCE_FW_CAP_REMOTE_PHY_CAP		0x00000001
#define	BCE_FW_CAP_REMOTE_PHY_PRESENT		0x00000002
#define	BCE_FW_CAP_MFW_KEEP_VLAN		0x00000008
#define	BCE_FW_CAP_BC_KEEP_VLAN			0x00000010

#define	BCE_RPHY_SERDES_LINK			0x00000374

#define	BCE_RPHY_COPPER_LINK			0x00000378

#define HOST_VIEW_SHMEM_BASE			0x167c00

/*
 * PCI registers defined in the PCI 2.2 spec.
 */
#define BCE_PCI_PCIX_CMD		0x42


/****************************************************************************/
/* Convenience definitions.                                                 */
/****************************************************************************/
#define BCE_PRINTF(fmt, args...)			\
    device_printf(sc->bce_dev, fmt, ##args)

#define	BCE_LOCK_INIT(_sc, _name)			\
    mtx_init(&(_sc)->bce_mtx, _name, MTX_NETWORK_LOCK, MTX_DEF)
#define	BCE_LOCK(_sc)			mtx_lock(&(_sc)->bce_mtx)
#define	BCE_LOCK_ASSERT(_sc)		mtx_assert(&(_sc)->bce_mtx, MA_OWNED)
#define	BCE_UNLOCK(_sc)			mtx_unlock(&(_sc)->bce_mtx)
#define	BCE_LOCK_DESTROY(_sc)		mtx_destroy(&(_sc)->bce_mtx)

#ifdef BCE_DEBUG
#define	REG_WR(sc, offset, val)		bce_reg_wr(sc, offset, val)
#define	REG_WR16(sc, offset, val)	bce_reg_wr16(sc, offset, val)
#define	REG_RD(sc, offset)		bce_reg_rd(sc, offset)
#else
#define	REG_WR(sc, offset, val)				\
    bus_space_write_4(sc->bce_btag, sc->bce_bhandle, offset, val)
#define	REG_WR16(sc, offset, val)			\
    bus_space_write_2(sc->bce_btag, sc->bce_bhandle, offset, val)
#define	REG_RD(sc, offset)	 			\
    bus_space_read_4(sc->bce_btag, sc->bce_bhandle, offset)
#endif

#define	REG_RD_IND(sc, offset)		bce_reg_rd_ind(sc, offset)
#define	REG_WR_IND(sc, offset, val)	bce_reg_wr_ind(sc, offset, val)
#define	CTX_WR(sc, cid_addr, offset, val)bce_ctx_wr(sc, cid_addr, offset, val)
#define	CTX_RD(sc, cid_addr, offset)	bce_ctx_rd(sc, cid_addr, offset)

#define	BCE_SETBIT(sc, reg, x)				\
    REG_WR(sc, reg, (REG_RD(sc, reg) | (x)))
#define	BCE_CLRBIT(sc, reg, x)				\
    REG_WR(sc, reg, (REG_RD(sc, reg) & ~(x)))
#define	PCI_SETBIT(dev, reg, x, s)			\
    pci_write_config(dev, reg, (pci_read_config(dev, reg, s) | (x)), s)
#define	PCI_CLRBIT(dev, reg, x, s)			\
    pci_write_config(dev, reg, (pci_read_config(dev, reg, s) & ~(x)), s)

#define	BCE_STATS(x)			(u_long) stats->stat_ ## x ## _lo

#if (BUS_SPACE_MAXADDR > 0xFFFFFFFF)
#define	BCE_ADDR_LO(y)			((u64) (y) & 0xFFFFFFFF)
#define	BCE_ADDR_HI(y)			((u64) (y) >> 32)
#else
#define	BCE_ADDR_LO(y)			((u32)y)
#define	BCE_ADDR_HI(y)			(0)
#endif


/****************************************************************************/
/* Do not modify any of the following data structures, they are generated   */
/* from RTL code.                                                           */
/*                                                                          */
/* Begin machine generated definitions.                                     */
/****************************************************************************/

/*
 *  tx_bd definition
 */
struct tx_bd {
	u32 tx_bd_haddr_hi;
	u32 tx_bd_haddr_lo;
	u32 tx_bd_mss_nbytes;
	u16 tx_bd_flags;
#define TX_BD_FLAGS_CONN_FAULT		(1<<0)
#define TX_BD_FLAGS_TCP_UDP_CKSUM	(1<<1)
#define TX_BD_FLAGS_IP_CKSUM		(1<<2)
#define TX_BD_FLAGS_VLAN_TAG		(1<<3)
#define TX_BD_FLAGS_COAL_NOW		(1<<4)
#define TX_BD_FLAGS_DONT_GEN_CRC	(1<<5)
#define TX_BD_FLAGS_END			(1<<6)
#define TX_BD_FLAGS_START			(1<<7)
#define TX_BD_FLAGS_SW_OPTION_WORD	(0x1f<<8)
#define TX_BD_FLAGS_SW_FLAGS		(1<<13)
#define TX_BD_FLAGS_SW_SNAP		(1<<14)
#define TX_BD_FLAGS_SW_LSO			(1<<15)
	u16 tx_bd_vlan_tag;
};


/*
 *  rx_bd definition
 */
struct rx_bd {
	u32 rx_bd_haddr_hi;
	u32 rx_bd_haddr_lo;
	u32 rx_bd_len;
	u32 rx_bd_flags;
#define RX_BD_FLAGS_NOPUSH		(1<<0)
#define RX_BD_FLAGS_DUMMY		(1<<1)
#define RX_BD_FLAGS_END		(1<<2)
#define RX_BD_FLAGS_START		(1<<3)
};


/*
 *  status_block definition
 */
struct status_block {
	u32 status_attn_bits;
		#define STATUS_ATTN_BITS_LINK_STATE		(1L<<0)
		#define STATUS_ATTN_BITS_TX_SCHEDULER_ABORT	(1L<<1)
		#define STATUS_ATTN_BITS_TX_BD_READ_ABORT	(1L<<2)
		#define STATUS_ATTN_BITS_TX_BD_CACHE_ABORT	(1L<<3)
		#define STATUS_ATTN_BITS_TX_PROCESSOR_ABORT	(1L<<4)
		#define STATUS_ATTN_BITS_TX_DMA_ABORT		(1L<<5)
		#define STATUS_ATTN_BITS_TX_PATCHUP_ABORT	(1L<<6)
		#define STATUS_ATTN_BITS_TX_ASSEMBLER_ABORT	(1L<<7)
		#define STATUS_ATTN_BITS_RX_PARSER_MAC_ABORT	(1L<<8)
		#define STATUS_ATTN_BITS_RX_PARSER_CATCHUP_ABORT (1L<<9)
		#define STATUS_ATTN_BITS_RX_MBUF_ABORT		(1L<<10)
		#define STATUS_ATTN_BITS_RX_LOOKUP_ABORT	(1L<<11)
		#define STATUS_ATTN_BITS_RX_PROCESSOR_ABORT	(1L<<12)
		#define STATUS_ATTN_BITS_RX_V2P_ABORT		(1L<<13)
		#define STATUS_ATTN_BITS_RX_BD_CACHE_ABORT	(1L<<14)
		#define STATUS_ATTN_BITS_RX_DMA_ABORT		(1L<<15)
		#define STATUS_ATTN_BITS_COMPLETION_ABORT	(1L<<16)
		#define STATUS_ATTN_BITS_HOST_COALESCE_ABORT	(1L<<17)
		#define STATUS_ATTN_BITS_MAILBOX_QUEUE_ABORT	(1L<<18)
		#define STATUS_ATTN_BITS_CONTEXT_ABORT		(1L<<19)
		#define STATUS_ATTN_BITS_CMD_SCHEDULER_ABORT	(1L<<20)
		#define STATUS_ATTN_BITS_CMD_PROCESSOR_ABORT	(1L<<21)
		#define STATUS_ATTN_BITS_MGMT_PROCESSOR_ABORT	(1L<<22)
		#define STATUS_ATTN_BITS_MAC_ABORT		(1L<<23)
		#define STATUS_ATTN_BITS_TIMER_ABORT		(1L<<24)
		#define STATUS_ATTN_BITS_DMAE_ABORT		(1L<<25)
		#define STATUS_ATTN_BITS_FLSH_ABORT		(1L<<26)
		#define STATUS_ATTN_BITS_GRC_ABORT		(1L<<27)
		#define STATUS_ATTN_BITS_PARITY_ERROR		(1L<<31)

	u32 status_attn_bits_ack;
#if defined(__BIG_ENDIAN)
	u16 status_tx_quick_consumer_index0;
	u16 status_tx_quick_consumer_index1;
	u16 status_tx_quick_consumer_index2;
	u16 status_tx_quick_consumer_index3;
	u16 status_rx_quick_consumer_index0;
	u16 status_rx_quick_consumer_index1;
	u16 status_rx_quick_consumer_index2;
	u16 status_rx_quick_consumer_index3;
	u16 status_rx_quick_consumer_index4;
	u16 status_rx_quick_consumer_index5;
	u16 status_rx_quick_consumer_index6;
	u16 status_rx_quick_consumer_index7;
	u16 status_rx_quick_consumer_index8;
	u16 status_rx_quick_consumer_index9;
	u16 status_rx_quick_consumer_index10;
	u16 status_rx_quick_consumer_index11;
	u16 status_rx_quick_consumer_index12;
	u16 status_rx_quick_consumer_index13;
	u16 status_rx_quick_consumer_index14;
	u16 status_rx_quick_consumer_index15;
	u16 status_completion_producer_index;
	u16 status_cmd_consumer_index;
	u16 status_idx;
	u16 status_unused;
#elif defined(__LITTLE_ENDIAN)
	u16 status_tx_quick_consumer_index1;
	u16 status_tx_quick_consumer_index0;
	u16 status_tx_quick_consumer_index3;
	u16 status_tx_quick_consumer_index2;
	u16 status_rx_quick_consumer_index1;
	u16 status_rx_quick_consumer_index0;
	u16 status_rx_quick_consumer_index3;
	u16 status_rx_quick_consumer_index2;
	u16 status_rx_quick_consumer_index5;
	u16 status_rx_quick_consumer_index4;
	u16 status_rx_quick_consumer_index7;
	u16 status_rx_quick_consumer_index6;
	u16 status_rx_quick_consumer_index9;
	u16 status_rx_quick_consumer_index8;
	u16 status_rx_quick_consumer_index11;
	u16 status_rx_quick_consumer_index10;
	u16 status_rx_quick_consumer_index13;
	u16 status_rx_quick_consumer_index12;
	u16 status_rx_quick_consumer_index15;
	u16 status_rx_quick_consumer_index14;
	u16 status_cmd_consumer_index;
	u16 status_completion_producer_index;
	u16 status_unused;
	u16 status_idx;
#endif
};


/*
 *  statistics_block definition
 */
struct statistics_block {
	u32 stat_IfHCInOctets_hi;
	u32 stat_IfHCInOctets_lo;
	u32 stat_IfHCInBadOctets_hi;
	u32 stat_IfHCInBadOctets_lo;
	u32 stat_IfHCOutOctets_hi;
	u32 stat_IfHCOutOctets_lo;
	u32 stat_IfHCOutBadOctets_hi;
	u32 stat_IfHCOutBadOctets_lo;
	u32 stat_IfHCInUcastPkts_hi;
	u32 stat_IfHCInUcastPkts_lo;
	u32 stat_IfHCInMulticastPkts_hi;
	u32 stat_IfHCInMulticastPkts_lo;
	u32 stat_IfHCInBroadcastPkts_hi;
	u32 stat_IfHCInBroadcastPkts_lo;
	u32 stat_IfHCOutUcastPkts_hi;
	u32 stat_IfHCOutUcastPkts_lo;
	u32 stat_IfHCOutMulticastPkts_hi;
	u32 stat_IfHCOutMulticastPkts_lo;
	u32 stat_IfHCOutBroadcastPkts_hi;
	u32 stat_IfHCOutBroadcastPkts_lo;
	u32 stat_emac_tx_stat_dot3statsinternalmactransmiterrors;
	u32 stat_Dot3StatsCarrierSenseErrors;
	u32 stat_Dot3StatsFCSErrors;
	u32 stat_Dot3StatsAlignmentErrors;
	u32 stat_Dot3StatsSingleCollisionFrames;
	u32 stat_Dot3StatsMultipleCollisionFrames;
	u32 stat_Dot3StatsDeferredTransmissions;
	u32 stat_Dot3StatsExcessiveCollisions;
	u32 stat_Dot3StatsLateCollisions;
	u32 stat_EtherStatsCollisions;
	u32 stat_EtherStatsFragments;
	u32 stat_EtherStatsJabbers;
	u32 stat_EtherStatsUndersizePkts;
	u32 stat_EtherStatsOversizePkts;
	u32 stat_EtherStatsPktsRx64Octets;
	u32 stat_EtherStatsPktsRx65Octetsto127Octets;
	u32 stat_EtherStatsPktsRx128Octetsto255Octets;
	u32 stat_EtherStatsPktsRx256Octetsto511Octets;
	u32 stat_EtherStatsPktsRx512Octetsto1023Octets;
	u32 stat_EtherStatsPktsRx1024Octetsto1522Octets;
	u32 stat_EtherStatsPktsRx1523Octetsto9022Octets;
	u32 stat_EtherStatsPktsTx64Octets;
	u32 stat_EtherStatsPktsTx65Octetsto127Octets;
	u32 stat_EtherStatsPktsTx128Octetsto255Octets;
	u32 stat_EtherStatsPktsTx256Octetsto511Octets;
	u32 stat_EtherStatsPktsTx512Octetsto1023Octets;
	u32 stat_EtherStatsPktsTx1024Octetsto1522Octets;
	u32 stat_EtherStatsPktsTx1523Octetsto9022Octets;
	u32 stat_XonPauseFramesReceived;
	u32 stat_XoffPauseFramesReceived;
	u32 stat_OutXonSent;
	u32 stat_OutXoffSent;
	u32 stat_FlowControlDone;
	u32 stat_MacControlFramesReceived;
	u32 stat_XoffStateEntered;
	u32 stat_IfInFramesL2FilterDiscards;
	u32 stat_IfInRuleCheckerDiscards;
	u32 stat_IfInFTQDiscards;
	u32 stat_IfInMBUFDiscards;
	u32 stat_IfInRuleCheckerP4Hit;
	u32 stat_CatchupInRuleCheckerDiscards;
	u32 stat_CatchupInFTQDiscards;
	u32 stat_CatchupInMBUFDiscards;
	u32 stat_CatchupInRuleCheckerP4Hit;
	u32 stat_GenStat00;
	u32 stat_GenStat01;
	u32 stat_GenStat02;
	u32 stat_GenStat03;
	u32 stat_GenStat04;
	u32 stat_GenStat05;
	u32 stat_GenStat06;
	u32 stat_GenStat07;
	u32 stat_GenStat08;
	u32 stat_GenStat09;
	u32 stat_GenStat10;
	u32 stat_GenStat11;
	u32 stat_GenStat12;
	u32 stat_GenStat13;
	u32 stat_GenStat14;
	u32 stat_GenStat15;
};


/*
 *  l2_fhdr definition
 */
struct l2_fhdr {
	u32 l2_fhdr_status;
		#define L2_FHDR_STATUS_RULE_CLASS	(0x7<<0)
		#define L2_FHDR_STATUS_RULE_P2		(1<<3)
		#define L2_FHDR_STATUS_RULE_P3		(1<<4)
		#define L2_FHDR_STATUS_RULE_P4		(1<<5)
		#define L2_FHDR_STATUS_L2_VLAN_TAG	(1<<6)
		#define L2_FHDR_STATUS_L2_LLC_SNAP	(1<<7)
		#define L2_FHDR_STATUS_RSS_HASH		(1<<8)
		#define L2_FHDR_STATUS_IP_DATAGRAM	(1<<13)
		#define L2_FHDR_STATUS_TCP_SEGMENT	(1<<14)
		#define L2_FHDR_STATUS_UDP_DATAGRAM	(1<<15)

		#define L2_FHDR_STATUS_SPLIT		(1<<16)
		#define L2_FHDR_ERRORS_BAD_CRC		(1<<17)
		#define L2_FHDR_ERRORS_PHY_DECODE	(1<<18)
		#define L2_FHDR_ERRORS_ALIGNMENT	(1<<19)
		#define L2_FHDR_ERRORS_TOO_SHORT	(1<<20)
		#define L2_FHDR_ERRORS_GIANT_FRAME	(1<<21)
		#define L2_FHDR_ERRORS_IPV4_BAD_LEN	(1<<22)
		#define L2_FHDR_ERRORS_TCP_XSUM		(1<<28)
		#define L2_FHDR_ERRORS_UDP_XSUM		(1<<31)

	u32 l2_fhdr_hash;
#if defined(__BIG_ENDIAN)
	u16 l2_fhdr_pkt_len;
	u16 l2_fhdr_vlan_tag;
	u16 l2_fhdr_ip_xsum;
	u16 l2_fhdr_tcp_udp_xsum;
#elif defined(__LITTLE_ENDIAN)
	u16 l2_fhdr_vlan_tag;
	u16 l2_fhdr_pkt_len;
	u16 l2_fhdr_tcp_udp_xsum;
	u16 l2_fhdr_ip_xsum;
#endif
};

#define BCE_L2FHDR_PRINTFB	\
	"\20"				\
	"\40UDP_XSUM_ERR"	\
	"\37b30"			\
	"\36b29"			\
	"\35TCP_XSUM_ERR"	\
	"\34b27"			\
	"\33b26"			\
	"\32b25"			\
	"\31b24"			\
	"\30b23"			\
	"\27IPv4_BAL_LEN"	\
	"\26GIANT_ERR"		\
	"\25SHORT_ERR"		\
	"\24ALIGN_ERR"		\
	"\23PHY_ERR"		\
	"\22CRC_ERR"		\
	"\21SPLIT"			\
	"\20UDP"			\
	"\17TCP"			\
	"\16IP"				\
	"\15SORT_b3"		\
	"\14SORT_b2"		\
	"\13SORT_b1"		\
	"\12SORT_b0"		\
	"\11RSS"			\
	"\10SNAP"			\
	"\07VLAN"			\
	"\06P4"				\
	"\05P3"				\
	"\04P2"				\
	"\03RULE_b2"		\
	"\02RULE_b1"		\
	"\01RULE_b0"


/*
 *  l2_tx_context definition (5706 and 5708)
 */
#define BCE_L2CTX_TX_TYPE			0x00000000
#define BCE_L2CTX_TX_TYPE_SIZE_L2		((0xc0/0x20)<<16)
#define BCE_L2CTX_TX_TYPE_TYPE			(0xf<<28)
#define BCE_L2CTX_TX_TYPE_TYPE_EMPTY		(0<<28)
#define BCE_L2CTX_TX_TYPE_TYPE_L2		(1<<28)

#define BCE_L2CTX_TX_HOST_BIDX			0x00000088
#define BCE_L2CTX_TX_EST_NBD			0x00000088
#define BCE_L2CTX_TX_CMD_TYPE			0x00000088
#define BCE_L2CTX_TX_CMD_TYPE_TYPE		(0xf<<24)
#define BCE_L2CTX_TX_CMD_TYPE_TYPE_L2		(0<<24)
#define BCE_L2CTX_TX_CMD_TYPE_TYPE_TCP		(1<<24)

#define BCE_L2CTX_TX_HOST_BSEQ			0x00000090
#define BCE_L2CTX_TX_TSCH_BSEQ			0x00000094
#define BCE_L2CTX_TX_TBDR_BSEQ			0x00000098
#define BCE_L2CTX_TX_TBDR_BOFF			0x0000009c
#define BCE_L2CTX_TX_TBDR_BIDX			0x0000009c
#define BCE_L2CTX_TX_TBDR_BHADDR_HI		0x000000a0
#define BCE_L2CTX_TX_TBDR_BHADDR_LO		0x000000a4
#define BCE_L2CTX_TX_TXP_BOFF			0x000000a8
#define BCE_L2CTX_TX_TXP_BIDX			0x000000a8
#define BCE_L2CTX_TX_TXP_BSEQ			0x000000ac

/*
 *  l2_tx_context definition (5709 and 5716)
 */
#define BCE_L2CTX_TX_TYPE_XI			0x00000080
#define BCE_L2CTX_TX_TYPE_SIZE_L2_XI		((0xc0/0x20)<<16)
#define BCE_L2CTX_TX_TYPE_TYPE_XI		(0xf<<28)
#define BCE_L2CTX_TX_TYPE_TYPE_EMPTY_XI		(0<<28)
#define BCE_L2CTX_TX_TYPE_TYPE_L2_XI		(1<<28)

#define BCE_L2CTX_TX_CMD_TYPE_XI		0x00000240
#define BCE_L2CTX_TX_CMD_TYPE_TYPE_XI		(0xf<<24)
#define BCE_L2CTX_TX_CMD_TYPE_TYPE_L2_XI	(0<<24)
#define BCE_L2CTX_TX_CMD_TYPE_TYPE_TCP_XI	(1<<24)

#define BCE_L2CTX_TX_HOST_BIDX_XI		0x00000240
#define BCE_L2CTX_TX_HOST_BSEQ_XI		0x00000248
#define BCE_L2CTX_TX_TBDR_BHADDR_HI_XI		0x00000258
#define BCE_L2CTX_TX_TBDR_BHADDR_LO_XI		0x0000025c


/*
 *  l2_rx_context definition (5706, 5708, 5709, and 5716)
 */
#define BCE_L2CTX_RX_WATER_MARK			0x00000000
#define BCE_L2CTX_RX_LO_WATER_MARK_SHIFT	0
#define BCE_L2CTX_RX_LO_WATER_MARK_DEFAULT	32
#define BCE_L2CTX_RX_LO_WATER_MARK_SCALE	4
#define BCE_L2CTX_RX_LO_WATER_MARK_DIS		0
#define BCE_L2CTX_RX_HI_WATER_MARK_SHIFT	4
#define BCE_L2CTX_RX_HI_WATER_MARK_SCALE	16
#define BCE_L2CTX_RX_WATER_MARKS_MSK		0x000000ff

#define BCE_L2CTX_RX_BD_PRE_READ		0x00000000
#define BCE_L2CTX_RX_BD_PRE_READ_SHIFT		8

#define BCE_L2CTX_RX_CTX_SIZE			0x00000000
#define BCE_L2CTX_RX_CTX_SIZE_SHIFT		16
#define BCE_L2CTX_RX_CTX_TYPE_SIZE_L2	\
    ((0x20/20)<<BCE_L2CTX_RX_CTX_SIZE_SHIFT)

#define BCE_L2CTX_RX_CTX_TYPE			0x00000000
#define BCE_L2CTX_RX_CTX_TYPE_SHIFT		24

#define BCE_L2CTX_RX_CTX_TYPE_CTX_BD_CHN_TYPE	(0xf<<28)
#define BCE_L2CTX_RX_CTX_TYPE_CTX_BD_CHN_TYPE_UNDEFINED	(0<<28)
#define BCE_L2CTX_RX_CTX_TYPE_CTX_BD_CHN_TYPE_VALUE	(1<<28)

#define BCE_L2CTX_RX_HOST_BDIDX			0x00000004
#define BCE_L2CTX_RX_HOST_BSEQ			0x00000008
#define BCE_L2CTX_RX_NX_BSEQ			0x0000000c
#define BCE_L2CTX_RX_NX_BDHADDR_HI		0x00000010
#define BCE_L2CTX_RX_NX_BDHADDR_LO		0x00000014
#define BCE_L2CTX_RX_NX_BDIDX			0x00000018

#define BCE_L2CTX_RX_HOST_PG_BDIDX		0x00000044
#define BCE_L2CTX_RX_PG_BUF_SIZE		0x00000048
#define BCE_L2CTX_RX_RBDC_KEY			0x0000004c
#define BCE_L2CTX_RX_RBDC_JUMBO_KEY		0x3ffe
#define BCE_L2CTX_RX_NX_PG_BDHADDR_HI		0x00000050
#define BCE_L2CTX_RX_NX_PG_BDHADDR_LO		0x00000054
#define BCE_L2CTX_RX_NX_PG_BDIDX		0x00000058


/*
 *  l2_mq definitions (5706, 5708, 5709, and 5716)
 */

#define BCE_L2MQ_RX_HOST_BDIDX			0x00000004
#define BCE_L2MQ_RX_HOST_BSEQ			0x00000008
#define BCE_L2MQ_RX_HOST_PG_BDIDX		0x00000044

#define BCE_L2MQ_TX_HOST_BIDX			0x00000088
#define BCE_L2MQ_TX_HOST_BSEQ			0x00000090

/*
 *  pci_config_l definition
 *  offset: 0000
 */
#define BCE_PCICFG_MISC_CONFIG				0x00000068
#define BCE_PCICFG_MISC_CONFIG_TARGET_BYTE_SWAP	 		(1L<<2)
#define BCE_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP	 (1L<<3)
#define BCE_PCICFG_MISC_CONFIG_CLOCK_CTL_ENA		 (1L<<5)
#define BCE_PCICFG_MISC_CONFIG_TARGET_GRC_WORD_SWAP	 (1L<<6)
#define BCE_PCICFG_MISC_CONFIG_REG_WINDOW_ENA		 (1L<<7)
#define BCE_PCICFG_MISC_CONFIG_CORE_RST_REQ		 (1L<<8)
#define BCE_PCICFG_MISC_CONFIG_CORE_RST_BSY		 (1L<<9)
#define BCE_PCICFG_MISC_CONFIG_ASIC_METAL_REV		 (0xffL<<16)
#define BCE_PCICFG_MISC_CONFIG_ASIC_BASE_REV		 (0xfL<<24)
#define BCE_PCICFG_MISC_CONFIG_ASIC_ID			 (0xfL<<28)
#define BCE_PCICFG_MISC_CONFIG_ASIC_REV			 (0xffffL<<16)

#define BCE_PCICFG_MISC_STATUS				0x0000006c
#define BCE_PCICFG_MISC_STATUS_INTA_VALUE		 (1L<<0)
#define BCE_PCICFG_MISC_STATUS_32BIT_DET		 (1L<<1)
#define BCE_PCICFG_MISC_STATUS_M66EN			 (1L<<2)
#define BCE_PCICFG_MISC_STATUS_PCIX_DET		 (1L<<3)
#define BCE_PCICFG_MISC_STATUS_PCIX_SPEED		 (0x3L<<4)
#define BCE_PCICFG_MISC_STATUS_PCIX_SPEED_66		 (0L<<4)
#define BCE_PCICFG_MISC_STATUS_PCIX_SPEED_100		 (1L<<4)
#define BCE_PCICFG_MISC_STATUS_PCIX_SPEED_133		 (2L<<4)
#define BCE_PCICFG_MISC_STATUS_PCIX_SPEED_PCI_MODE	 (3L<<4)

#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS		0x00000070
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET	 (0xfL<<0)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_32MHZ	 (0L<<0)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_38MHZ	 (1L<<0)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_48MHZ	 (2L<<0)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_55MHZ	 (3L<<0)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_66MHZ	 (4L<<0)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_80MHZ	 (5L<<0)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_95MHZ	 (6L<<0)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_133MHZ	 (7L<<0)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_LOW	 (0xfL<<0)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_DISABLE	 (1L<<6)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_ALT	 (1L<<7)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_ALT_SRC	 (0x7L<<8)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_ALT_SRC_UNDEF	 (0L<<8)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_ALT_SRC_12	 (1L<<8)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_ALT_SRC_6	 (2L<<8)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_ALT_SRC_62	 (4L<<8)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PLAY_DEAD	 (1L<<11)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED	 (0xfL<<12)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_100	 (0L<<12)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_80	 (1L<<12)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_50	 (2L<<12)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_40	 (4L<<12)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_25	 (8L<<12)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_CORE_CLK_PLL_STOP	 (1L<<16)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_PLL_STOP	 (1L<<17)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_RESERVED_18	 (1L<<18)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_USE_SPD_DET	 (1L<<19)
#define BCE_PCICFG_PCI_CLOCK_CONTROL_BITS_RESERVED	 (0xfffL<<20)

#define BCE_PCICFG_REG_WINDOW_ADDRESS			0x00000078
#define BCE_PCICFG_REG_WINDOW				0x00000080
#define BCE_PCICFG_INT_ACK_CMD				0x00000084
#define BCE_PCICFG_INT_ACK_CMD_INDEX			 (0xffffL<<0)
#define BCE_PCICFG_INT_ACK_CMD_INDEX_VALID		 (1L<<16)
#define BCE_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM	 (1L<<17)
#define BCE_PCICFG_INT_ACK_CMD_MASK_INT		 (1L<<18)

#define BCE_PCICFG_STATUS_BIT_SET_CMD			0x00000088
#define BCE_PCICFG_STATUS_BIT_CLEAR_CMD		0x0000008c
#define BCE_PCICFG_MAILBOX_QUEUE_ADDR			0x00000090
#define BCE_PCICFG_MAILBOX_QUEUE_DATA			0x00000094


/*
 *  pci_reg definition
 *  offset: 0x400
 */
#define BCE_PCI_GRC_WINDOW_ADDR			0x00000400
#define BCE_PCI_GRC_WINDOW_ADDR_PCI_GRC_WINDOW_ADDR_VALUE	 (0x3ffffL<<8)

#define BCE_PCI_CONFIG_1				0x00000404
#define BCE_PCI_CONFIG_1_READ_BOUNDARY			 (0x7L<<8)
#define BCE_PCI_CONFIG_1_READ_BOUNDARY_OFF		 (0L<<8)
#define BCE_PCI_CONFIG_1_READ_BOUNDARY_16		 (1L<<8)
#define BCE_PCI_CONFIG_1_READ_BOUNDARY_32		 (2L<<8)
#define BCE_PCI_CONFIG_1_READ_BOUNDARY_64		 (3L<<8)
#define BCE_PCI_CONFIG_1_READ_BOUNDARY_128		 (4L<<8)
#define BCE_PCI_CONFIG_1_READ_BOUNDARY_256		 (5L<<8)
#define BCE_PCI_CONFIG_1_READ_BOUNDARY_512		 (6L<<8)
#define BCE_PCI_CONFIG_1_READ_BOUNDARY_1024		 (7L<<8)
#define BCE_PCI_CONFIG_1_WRITE_BOUNDARY		 (0x7L<<11)
#define BCE_PCI_CONFIG_1_WRITE_BOUNDARY_OFF		 (0L<<11)
#define BCE_PCI_CONFIG_1_WRITE_BOUNDARY_16		 (1L<<11)
#define BCE_PCI_CONFIG_1_WRITE_BOUNDARY_32		 (2L<<11)
#define BCE_PCI_CONFIG_1_WRITE_BOUNDARY_64		 (3L<<11)
#define BCE_PCI_CONFIG_1_WRITE_BOUNDARY_128		 (4L<<11)
#define BCE_PCI_CONFIG_1_WRITE_BOUNDARY_256		 (5L<<11)
#define BCE_PCI_CONFIG_1_WRITE_BOUNDARY_512		 (6L<<11)
#define BCE_PCI_CONFIG_1_WRITE_BOUNDARY_1024		 (7L<<11)

#define BCE_PCI_CONFIG_2				0x00000408
#define BCE_PCI_CONFIG_2_BAR1_SIZE			 (0xfL<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_DISABLED		 (0L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_64K			 (1L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_128K		 (2L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_256K		 (3L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_512K		 (4L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_1M			 (5L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_2M			 (6L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_4M			 (7L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_8M			 (8L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_16M			 (9L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_32M			 (10L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_64M			 (11L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_128M		 (12L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_256M		 (13L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_512M		 (14L<<0)
#define BCE_PCI_CONFIG_2_BAR1_SIZE_1G			 (15L<<0)
#define BCE_PCI_CONFIG_2_BAR1_64ENA			 (1L<<4)
#define BCE_PCI_CONFIG_2_EXP_ROM_RETRY			 (1L<<5)
#define BCE_PCI_CONFIG_2_CFG_CYCLE_RETRY		 (1L<<6)
#define BCE_PCI_CONFIG_2_FIRST_CFG_DONE		 (1L<<7)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE			 (0xffL<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_DISABLED		 (0L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_1K		 (1L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_2K		 (2L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_4K		 (3L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_8K		 (4L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_16K		 (5L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_32K		 (6L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_64K		 (7L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_128K		 (8L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_256K		 (9L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_512K		 (10L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_1M		 (11L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_2M		 (12L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_4M		 (13L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_8M		 (14L<<8)
#define BCE_PCI_CONFIG_2_EXP_ROM_SIZE_16M		 (15L<<8)
#define BCE_PCI_CONFIG_2_MAX_SPLIT_LIMIT		 (0x1fL<<16)
#define BCE_PCI_CONFIG_2_MAX_READ_LIMIT		 (0x3L<<21)
#define BCE_PCI_CONFIG_2_MAX_READ_LIMIT_512		 (0L<<21)
#define BCE_PCI_CONFIG_2_MAX_READ_LIMIT_1K		 (1L<<21)
#define BCE_PCI_CONFIG_2_MAX_READ_LIMIT_2K		 (2L<<21)
#define BCE_PCI_CONFIG_2_MAX_READ_LIMIT_4K		 (3L<<21)
#define BCE_PCI_CONFIG_2_FORCE_32_BIT_MSTR		 (1L<<23)
#define BCE_PCI_CONFIG_2_FORCE_32_BIT_TGT		 (1L<<24)
#define BCE_PCI_CONFIG_2_KEEP_REQ_ASSERT		 (1L<<25)

#define BCE_PCI_CONFIG_3				0x0000040c
#define BCE_PCI_CONFIG_3_STICKY_BYTE			 (0xffL<<0)
#define BCE_PCI_CONFIG_3_FORCE_PME			 (1L<<24)
#define BCE_PCI_CONFIG_3_PME_STATUS			 (1L<<25)
#define BCE_PCI_CONFIG_3_PME_ENABLE			 (1L<<26)
#define BCE_PCI_CONFIG_3_PM_STATE			 (0x3L<<27)
#define BCE_PCI_CONFIG_3_VAUX_PRESET			 (1L<<30)
#define BCE_PCI_CONFIG_3_PCI_POWER			 (1L<<31)

#define BCE_PCI_PM_DATA_A				0x00000410
#define BCE_PCI_PM_DATA_A_PM_DATA_0_PRG		 (0xffL<<0)
#define BCE_PCI_PM_DATA_A_PM_DATA_1_PRG		 (0xffL<<8)
#define BCE_PCI_PM_DATA_A_PM_DATA_2_PRG		 (0xffL<<16)
#define BCE_PCI_PM_DATA_A_PM_DATA_3_PRG		 (0xffL<<24)

#define BCE_PCI_PM_DATA_B				0x00000414
#define BCE_PCI_PM_DATA_B_PM_DATA_4_PRG		 (0xffL<<0)
#define BCE_PCI_PM_DATA_B_PM_DATA_5_PRG		 (0xffL<<8)
#define BCE_PCI_PM_DATA_B_PM_DATA_6_PRG		 (0xffL<<16)
#define BCE_PCI_PM_DATA_B_PM_DATA_7_PRG		 (0xffL<<24)

#define BCE_PCI_SWAP_DIAG0				0x00000418
#define BCE_PCI_SWAP_DIAG1				0x0000041c
#define BCE_PCI_EXP_ROM_ADDR				0x00000420
#define BCE_PCI_EXP_ROM_ADDR_ADDRESS			 (0x3fffffL<<2)
#define BCE_PCI_EXP_ROM_ADDR_REQ			 (1L<<31)

#define BCE_PCI_EXP_ROM_DATA				0x00000424
#define BCE_PCI_VPD_INTF				0x00000428
#define BCE_PCI_VPD_INTF_INTF_REQ			 (1L<<0)

#define BCE_PCI_VPD_ADDR_FLAG				0x0000042c
#define BCE_PCI_VPD_ADDR_FLAG_ADDRESS			 (0x1fff<<2)
#define BCE_PCI_VPD_ADDR_FLAG_WR			 (1<<15)

#define BCE_PCI_VPD_DATA				0x00000430
#define BCE_PCI_ID_VAL1				0x00000434
#define BCE_PCI_ID_VAL1_DEVICE_ID			 (0xffffL<<0)
#define BCE_PCI_ID_VAL1_VENDOR_ID			 (0xffffL<<16)

#define BCE_PCI_ID_VAL2				0x00000438
#define BCE_PCI_ID_VAL2_SUBSYSTEM_VENDOR_ID		 (0xffffL<<0)
#define BCE_PCI_ID_VAL2_SUBSYSTEM_ID			 (0xffffL<<16)

#define BCE_PCI_ID_VAL3				0x0000043c
#define BCE_PCI_ID_VAL3_CLASS_CODE			 (0xffffffL<<0)
#define BCE_PCI_ID_VAL3_REVISION_ID			 (0xffL<<24)

#define BCE_PCI_ID_VAL4				0x00000440
#define BCE_PCI_ID_VAL4_CAP_ENA			 (0xfL<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_0			 (0L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_1			 (1L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_2			 (2L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_3			 (3L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_4			 (4L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_5			 (5L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_6			 (6L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_7			 (7L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_8			 (8L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_9			 (9L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_10			 (10L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_11			 (11L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_12			 (12L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_13			 (13L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_14			 (14L<<0)
#define BCE_PCI_ID_VAL4_CAP_ENA_15			 (15L<<0)
#define BCE_PCI_ID_VAL4_PM_SCALE_PRG			 (0x3L<<6)
#define BCE_PCI_ID_VAL4_PM_SCALE_PRG_0			 (0L<<6)
#define BCE_PCI_ID_VAL4_PM_SCALE_PRG_1			 (1L<<6)
#define BCE_PCI_ID_VAL4_PM_SCALE_PRG_2			 (2L<<6)
#define BCE_PCI_ID_VAL4_PM_SCALE_PRG_3			 (3L<<6)
#define BCE_PCI_ID_VAL4_MSI_LIMIT			 (0x7L<<9)
#define BCE_PCI_ID_VAL4_MSI_ADVERTIZE			 (0x7L<<12)
#define BCE_PCI_ID_VAL4_MSI_ENABLE			 (1L<<15)
#define BCE_PCI_ID_VAL4_MAX_64_ADVERTIZE		 (1L<<16)
#define BCE_PCI_ID_VAL4_MAX_133_ADVERTIZE		 (1L<<17)
#define BCE_PCI_ID_VAL4_MAX_MEM_READ_SIZE		 (0x3L<<21)
#define BCE_PCI_ID_VAL4_MAX_SPLIT_SIZE			 (0x7L<<23)
#define BCE_PCI_ID_VAL4_MAX_CUMULATIVE_SIZE		 (0x7L<<26)

#define BCE_PCI_ID_VAL5				0x00000444
#define BCE_PCI_ID_VAL5_D1_SUPPORT			 (1L<<0)
#define BCE_PCI_ID_VAL5_D2_SUPPORT			 (1L<<1)
#define BCE_PCI_ID_VAL5_PME_IN_D0			 (1L<<2)
#define BCE_PCI_ID_VAL5_PME_IN_D1			 (1L<<3)
#define BCE_PCI_ID_VAL5_PME_IN_D2			 (1L<<4)
#define BCE_PCI_ID_VAL5_PME_IN_D3_HOT			 (1L<<5)

#define BCE_PCI_PCIX_EXTENDED_STATUS			0x00000448
#define BCE_PCI_PCIX_EXTENDED_STATUS_NO_SNOOP		 (1L<<8)
#define BCE_PCI_PCIX_EXTENDED_STATUS_LONG_BURST	 (1L<<9)
#define BCE_PCI_PCIX_EXTENDED_STATUS_SPLIT_COMP_MSG_CLASS	 (0xfL<<16)
#define BCE_PCI_PCIX_EXTENDED_STATUS_SPLIT_COMP_MSG_IDX	 (0xffL<<24)

#define BCE_PCI_ID_VAL6				0x0000044c
#define BCE_PCI_ID_VAL6_MAX_LAT			 (0xffL<<0)
#define BCE_PCI_ID_VAL6_MIN_GNT			 (0xffL<<8)
#define BCE_PCI_ID_VAL6_BIST				 (0xffL<<16)

#define BCE_PCI_MSI_DATA				0x00000450
#define BCE_PCI_MSI_DATA_PCI_MSI_DATA			 (0xffffL<<0)

#define BCE_PCI_MSI_ADDR_H				0x00000454
#define BCE_PCI_MSI_ADDR_L				0x00000458


/*
 *  misc_reg definition
 *  offset: 0x800
 */
#define BCE_MISC_COMMAND						0x00000800
#define BCE_MISC_COMMAND_ENABLE_ALL				(1L<<0)
#define BCE_MISC_COMMAND_DISABLE_ALL			(1L<<1)
#define BCE_MISC_COMMAND_SW_RESET				(1L<<4)
#define BCE_MISC_COMMAND_POR_RESET				(1L<<5)
#define BCE_MISC_COMMAND_HD_RESET				(1L<<6)
#define BCE_MISC_COMMAND_CMN_SW_RESET			(1L<<7)
#define BCE_MISC_COMMAND_PAR_ERROR				(1L<<8)
#define BCE_MISC_COMMAND_CS16_ERR				(1L<<9)
#define BCE_MISC_COMMAND_CS16_ERR_LOC			(0xfL<<12)
#define BCE_MISC_COMMAND_PAR_ERR_RAM			(0x7fL<<16)
#define BCE_MISC_COMMAND_POWERDOWN_EVENT		(1L<<23)
#define BCE_MISC_COMMAND_SW_SHUTDOWN			(1L<<24)
#define BCE_MISC_COMMAND_SHUTDOWN_EN			(1L<<25)
#define BCE_MISC_COMMAND_DINTEG_ATTN_EN			(1L<<26)
#define BCE_MISC_COMMAND_PCIE_LINK_IN_L23		(1L<<27)
#define BCE_MISC_COMMAND_PCIE_DIS				(1L<<28)

#define BCE_MISC_CFG							0x00000804
#define BCE_MISC_CFG_GRC_TMOUT					(1L<<0)
#define BCE_MISC_CFG_NVM_WR_EN					(0x3L<<1)
#define BCE_MISC_CFG_NVM_WR_EN_PROTECT			(0L<<1)
#define BCE_MISC_CFG_NVM_WR_EN_PCI				(1L<<1)
#define BCE_MISC_CFG_NVM_WR_EN_ALLOW			(2L<<1)
#define BCE_MISC_CFG_NVM_WR_EN_ALLOW2			(3L<<1)
#define BCE_MISC_CFG_BIST_EN					(1L<<3)
#define BCE_MISC_CFG_CK25_OUT_ALT_SRC			(1L<<4)
#define BCE_MISC_CFG_RESERVED5_TE				(1L<<5)
#define BCE_MISC_CFG_RESERVED6_TE				(1L<<6)
#define BCE_MISC_CFG_CLK_CTL_OVERRIDE			(1L<<7)
#define BCE_MISC_CFG_LEDMODE					(0x7L<<8)
#define BCE_MISC_CFG_LEDMODE_MAC				(0L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY1_TE			(1L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY2_TE			(2L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY3_TE			(3L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY4_TE			(4L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY5_TE			(5L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY6_TE			(6L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY7_TE			(7L<<8)
#define BCE_MISC_CFG_MCP_GRC_TMOUT_TE			(1L<<11)
#define BCE_MISC_CFG_DBU_GRC_TMOUT_TE			(1L<<12)
#define BCE_MISC_CFG_LEDMODE_XI					(0xfL<<8)
#define BCE_MISC_CFG_LEDMODE_MAC_XI				(0L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY1_XI			(1L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY2_XI			(2L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY3_XI			(3L<<8)
#define BCE_MISC_CFG_LEDMODE_MAC2_XI			(4L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY4_XI			(5L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY5_XI			(6L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY6_XI			(7L<<8)
#define BCE_MISC_CFG_LEDMODE_MAC3_XI			(8L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY7_XI			(9L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY8_XI			(10L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY9_XI			(11L<<8)
#define BCE_MISC_CFG_LEDMODE_MAC4_XI			(12L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY10_XI			(13L<<8)
#define BCE_MISC_CFG_LEDMODE_PHY11_XI			(14L<<8)
#define BCE_MISC_CFG_LEDMODE_UNUSED_XI			(15L<<8)
#define BCE_MISC_CFG_PORT_SELECT_XI				(1L<<13)
#define BCE_MISC_CFG_PARITY_MODE_XI				(1L<<14)

#define BCE_MISC_ID								0x00000808
#define BCE_MISC_ID_BOND_ID						(0xfL<<0)
#define BCE_MISC_ID_BOND_ID_X					(0L<<0)
#define BCE_MISC_ID_BOND_ID_C					(3L<<0)
#define BCE_MISC_ID_BOND_ID_S					(12L<<0)
#define BCE_MISC_ID_CHIP_METAL					(0xffL<<4)
#define BCE_MISC_ID_CHIP_REV					(0xfL<<12)
#define BCE_MISC_ID_CHIP_NUM					(0xffffL<<16)

#define BCE_MISC_ENABLE_STATUS_BITS				0x0000080c
#define BCE_MISC_ENABLE_STATUS_BITS_TX_SCHEDULER_ENABLE	 (1L<<0)
#define BCE_MISC_ENABLE_STATUS_BITS_TX_BD_READ_ENABLE	 (1L<<1)
#define BCE_MISC_ENABLE_STATUS_BITS_TX_BD_CACHE_ENABLE	 (1L<<2)
#define BCE_MISC_ENABLE_STATUS_BITS_TX_PROCESSOR_ENABLE	 (1L<<3)
#define BCE_MISC_ENABLE_STATUS_BITS_TX_DMA_ENABLE		 (1L<<4)
#define BCE_MISC_ENABLE_STATUS_BITS_TX_PATCHUP_ENABLE	 (1L<<5)
#define BCE_MISC_ENABLE_STATUS_BITS_TX_PAYLOAD_Q_ENABLE	 (1L<<6)
#define BCE_MISC_ENABLE_STATUS_BITS_TX_HEADER_Q_ENABLE	 (1L<<7)
#define BCE_MISC_ENABLE_STATUS_BITS_TX_ASSEMBLER_ENABLE	 (1L<<8)
#define BCE_MISC_ENABLE_STATUS_BITS_EMAC_ENABLE			 (1L<<9)
#define BCE_MISC_ENABLE_STATUS_BITS_RX_PARSER_MAC_ENABLE (1L<<10)
#define BCE_MISC_ENABLE_STATUS_BITS_RX_PARSER_CATCHUP_ENABLE	 (1L<<11)
#define BCE_MISC_ENABLE_STATUS_BITS_RX_MBUF_ENABLE		(1L<<12)
#define BCE_MISC_ENABLE_STATUS_BITS_RX_LOOKUP_ENABLE	(1L<<13)
#define BCE_MISC_ENABLE_STATUS_BITS_RX_PROCESSOR_ENABLE	(1L<<14)
#define BCE_MISC_ENABLE_STATUS_BITS_RX_V2P_ENABLE		(1L<<15)
#define BCE_MISC_ENABLE_STATUS_BITS_RX_BD_CACHE_ENABLE	(1L<<16)
#define BCE_MISC_ENABLE_STATUS_BITS_RX_DMA_ENABLE		(1L<<17)
#define BCE_MISC_ENABLE_STATUS_BITS_COMPLETION_ENABLE	(1L<<18)
#define BCE_MISC_ENABLE_STATUS_BITS_HOST_COALESCE_ENABLE	 (1L<<19)
#define BCE_MISC_ENABLE_STATUS_BITS_MAILBOX_QUEUE_ENABLE	 (1L<<20)
#define BCE_MISC_ENABLE_STATUS_BITS_CONTEXT_ENABLE		(1L<<21)
#define BCE_MISC_ENABLE_STATUS_BITS_CMD_SCHEDULER_ENABLE	 (1L<<22)
#define BCE_MISC_ENABLE_STATUS_BITS_CMD_PROCESSOR_ENABLE	 (1L<<23)
#define BCE_MISC_ENABLE_STATUS_BITS_MGMT_PROCESSOR_ENABLE	 (1L<<24)
#define BCE_MISC_ENABLE_STATUS_BITS_TIMER_ENABLE		(1L<<25)
#define BCE_MISC_ENABLE_STATUS_BITS_DMA_ENGINE_ENABLE	(1L<<26)
#define BCE_MISC_ENABLE_STATUS_BITS_UMP_ENABLE			(1L<<27)
#define BCE_MISC_ENABLE_STATUS_BITS_RV2P_CMD_SCHEDULER_ENABLE	 (1L<<28)
#define BCE_MISC_ENABLE_STATUS_BITS_RSVD_FUTURE_ENABLE	(0x7L<<29)

#define BCE_MISC_ENABLE_SET_BITS						0x00000810
#define BCE_MISC_ENABLE_SET_BITS_TX_SCHEDULER_ENABLE	(1L<<0)
#define BCE_MISC_ENABLE_SET_BITS_TX_BD_READ_ENABLE		(1L<<1)
#define BCE_MISC_ENABLE_SET_BITS_TX_BD_CACHE_ENABLE		(1L<<2)
#define BCE_MISC_ENABLE_SET_BITS_TX_PROCESSOR_ENABLE	(1L<<3)
#define BCE_MISC_ENABLE_SET_BITS_TX_DMA_ENABLE			(1L<<4)
#define BCE_MISC_ENABLE_SET_BITS_TX_PATCHUP_ENABLE		(1L<<5)
#define BCE_MISC_ENABLE_SET_BITS_TX_PAYLOAD_Q_ENABLE	(1L<<6)
#define BCE_MISC_ENABLE_SET_BITS_TX_HEADER_Q_ENABLE		(1L<<7)
#define BCE_MISC_ENABLE_SET_BITS_TX_ASSEMBLER_ENABLE	(1L<<8)
#define BCE_MISC_ENABLE_SET_BITS_EMAC_ENABLE			(1L<<9)
#define BCE_MISC_ENABLE_SET_BITS_RX_PARSER_MAC_ENABLE	(1L<<10)
#define BCE_MISC_ENABLE_SET_BITS_RX_PARSER_CATCHUP_ENABLE	(1L<<11)
#define BCE_MISC_ENABLE_SET_BITS_RX_MBUF_ENABLE			(1L<<12)
#define BCE_MISC_ENABLE_SET_BITS_RX_LOOKUP_ENABLE		(1L<<13)
#define BCE_MISC_ENABLE_SET_BITS_RX_PROCESSOR_ENABLE	(1L<<14)
#define BCE_MISC_ENABLE_SET_BITS_RX_V2P_ENABLE			(1L<<15)
#define BCE_MISC_ENABLE_SET_BITS_RX_BD_CACHE_ENABLE		(1L<<16)
#define BCE_MISC_ENABLE_SET_BITS_RX_DMA_ENABLE			(1L<<17)
#define BCE_MISC_ENABLE_SET_BITS_COMPLETION_ENABLE		(1L<<18)
#define BCE_MISC_ENABLE_SET_BITS_HOST_COALESCE_ENABLE	(1L<<19)
#define BCE_MISC_ENABLE_SET_BITS_MAILBOX_QUEUE_ENABLE	(1L<<20)
#define BCE_MISC_ENABLE_SET_BITS_CONTEXT_ENABLE			(1L<<21)
#define BCE_MISC_ENABLE_SET_BITS_CMD_SCHEDULER_ENABLE	(1L<<22)
#define BCE_MISC_ENABLE_SET_BITS_CMD_PROCESSOR_ENABLE	(1L<<23)
#define BCE_MISC_ENABLE_SET_BITS_MGMT_PROCESSOR_ENABLE	(1L<<24)
#define BCE_MISC_ENABLE_SET_BITS_TIMER_ENABLE			(1L<<25)
#define BCE_MISC_ENABLE_SET_BITS_DMA_ENGINE_ENABLE		(1L<<26)
#define BCE_MISC_ENABLE_SET_BITS_UMP_ENABLE				(1L<<27)
#define BCE_MISC_ENABLE_SET_BITS_RV2P_CMD_SCHEDULER_ENABLE	(1L<<28)
#define BCE_MISC_ENABLE_SET_BITS_RSVD_FUTURE_ENABLE		(0x7L<<29)

#define BCE_MISC_ENABLE_DEFAULT							0x05ffffff
#define BCE_MISC_ENABLE_DEFAULT_XI			  			0x17ffffff

#define BCE_MISC_ENABLE_CLR_BITS						0x00000814
#define BCE_MISC_ENABLE_CLR_BITS_TX_SCHEDULER_ENABLE	(1L<<0)
#define BCE_MISC_ENABLE_CLR_BITS_TX_BD_READ_ENABLE		(1L<<1)
#define BCE_MISC_ENABLE_CLR_BITS_TX_BD_CACHE_ENABLE		(1L<<2)
#define BCE_MISC_ENABLE_CLR_BITS_TX_PROCESSOR_ENABLE	(1L<<3)
#define BCE_MISC_ENABLE_CLR_BITS_TX_DMA_ENABLE			(1L<<4)
#define BCE_MISC_ENABLE_CLR_BITS_TX_PATCHUP_ENABLE		(1L<<5)
#define BCE_MISC_ENABLE_CLR_BITS_TX_PAYLOAD_Q_ENABLE	(1L<<6)
#define BCE_MISC_ENABLE_CLR_BITS_TX_HEADER_Q_ENABLE		(1L<<7)
#define BCE_MISC_ENABLE_CLR_BITS_TX_ASSEMBLER_ENABLE	(1L<<8)
#define BCE_MISC_ENABLE_CLR_BITS_EMAC_ENABLE			(1L<<9)
#define BCE_MISC_ENABLE_CLR_BITS_RX_PARSER_MAC_ENABLE	(1L<<10)
#define BCE_MISC_ENABLE_CLR_BITS_RX_PARSER_CATCHUP_ENABLE	(1L<<11)
#define BCE_MISC_ENABLE_CLR_BITS_RX_MBUF_ENABLE			(1L<<12)
#define BCE_MISC_ENABLE_CLR_BITS_RX_LOOKUP_ENABLE		(1L<<13)
#define BCE_MISC_ENABLE_CLR_BITS_RX_PROCESSOR_ENABLE	(1L<<14)
#define BCE_MISC_ENABLE_CLR_BITS_RX_V2P_ENABLE			(1L<<15)
#define BCE_MISC_ENABLE_CLR_BITS_RX_BD_CACHE_ENABLE		(1L<<16)
#define BCE_MISC_ENABLE_CLR_BITS_RX_DMA_ENABLE			(1L<<17)
#define BCE_MISC_ENABLE_CLR_BITS_COMPLETION_ENABLE		(1L<<18)
#define BCE_MISC_ENABLE_CLR_BITS_HOST_COALESCE_ENABLE	(1L<<19)
#define BCE_MISC_ENABLE_CLR_BITS_MAILBOX_QUEUE_ENABLE	(1L<<20)
#define BCE_MISC_ENABLE_CLR_BITS_CONTEXT_ENABLE			(1L<<21)
#define BCE_MISC_ENABLE_CLR_BITS_CMD_SCHEDULER_ENABLE	(1L<<22)
#define BCE_MISC_ENABLE_CLR_BITS_CMD_PROCESSOR_ENABLE	(1L<<23)
#define BCE_MISC_ENABLE_CLR_BITS_MGMT_PROCESSOR_ENABLE	(1L<<24)
#define BCE_MISC_ENABLE_CLR_BITS_TIMER_ENABLE			(1L<<25)
#define BCE_MISC_ENABLE_CLR_BITS_DMA_ENGINE_ENABLE		(1L<<26)
#define BCE_MISC_ENABLE_CLR_BITS_UMP_ENABLE				(1L<<27)
#define BCE_MISC_ENABLE_CLR_BITS_RV2P_CMD_SCHEDULER_ENABLE	(1L<<28)
#define BCE_MISC_ENABLE_CLR_BITS_RSVD_FUTURE_ENABLE		(0x7L<<29)

#define BCE_MISC_ENABLE_CLR_DEFAULT						0x17ffffff

#define BCE_MISC_CLOCK_CONTROL_BITS			0x00000818
#define BCE_MISC_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET	 (0xfL<<0)
#define BCE_MISC_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_32MHZ	 (0L<<0)
#define BCE_MISC_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_38MHZ	 (1L<<0)
#define BCE_MISC_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_48MHZ	 (2L<<0)
#define BCE_MISC_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_55MHZ	 (3L<<0)
#define BCE_MISC_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_66MHZ	 (4L<<0)
#define BCE_MISC_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_80MHZ	 (5L<<0)
#define BCE_MISC_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_95MHZ	 (6L<<0)
#define BCE_MISC_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_133MHZ	 (7L<<0)
#define BCE_MISC_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_LOW	 (0xfL<<0)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_DISABLE	 (1L<<6)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_ALT	 (1L<<7)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_ALT_SRC	 (0x7L<<8)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_ALT_SRC_UNDEF	 (0L<<8)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_ALT_SRC_12	 (1L<<8)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_ALT_SRC_6	 (2L<<8)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_ALT_SRC_62	 (4L<<8)
#define BCE_MISC_CLOCK_CONTROL_BITS_RESERVED0_XI	 (0x7L<<8)
#define BCE_MISC_CLOCK_CONTROL_BITS_MIN_POWER		 (1L<<11)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED	 (0xfL<<12)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_100	 (0L<<12)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_80	 (1L<<12)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_50	 (2L<<12)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_40	 (4L<<12)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_25	 (8L<<12)
#define BCE_MISC_CLOCK_CONTROL_BITS_RESERVED1_XI	 (0xfL<<12)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_PLL_STOP	 (1L<<16)
#define BCE_MISC_CLOCK_CONTROL_BITS_RESERVED_17_TE	 (1L<<17)
#define BCE_MISC_CLOCK_CONTROL_BITS_RESERVED_18_TE	 (1L<<18)
#define BCE_MISC_CLOCK_CONTROL_BITS_RESERVED_19_TE	 (1L<<19)
#define BCE_MISC_CLOCK_CONTROL_BITS_RESERVED_TE	 (0xfffL<<20)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_ALT_MGMT_XI	 (1L<<17)
#define BCE_MISC_CLOCK_CONTROL_BITS_RESERVED2_XI	 (0x3fL<<18)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_PLL_VCO_XI	 (0x7L<<24)
#define BCE_MISC_CLOCK_CONTROL_BITS_RESERVED3_XI	 (1L<<27)
#define BCE_MISC_CLOCK_CONTROL_BITS_CORE_CLK_PLL_SPEED_XI	 (0xfL<<28)

#define BCE_MISC_SPIO					0x0000081c
#define BCE_MISC_SPIO_VALUE				 (0xffL<<0)
#define BCE_MISC_SPIO_SET				 (0xffL<<8)
#define BCE_MISC_SPIO_CLR				 (0xffL<<16)
#define BCE_MISC_SPIO_FLOAT				 (0xffL<<24)

#define BCE_MISC_SPIO_INT				0x00000820
#define BCE_MISC_SPIO_INT_INT_STATE_TE			 (0xfL<<0)
#define BCE_MISC_SPIO_INT_OLD_VALUE_TE			 (0xfL<<8)
#define BCE_MISC_SPIO_INT_OLD_SET_TE			 (0xfL<<16)
#define BCE_MISC_SPIO_INT_OLD_CLR_TE			 (0xfL<<24)
#define BCE_MISC_SPIO_INT_INT_STATE_XI			 (0xffL<<0)
#define BCE_MISC_SPIO_INT_OLD_VALUE_XI			 (0xffL<<8)
#define BCE_MISC_SPIO_INT_OLD_SET_XI			 (0xffL<<16)
#define BCE_MISC_SPIO_INT_OLD_CLR_XI			 (0xffL<<24)

#define BCE_MISC_CONFIG_LFSR				0x00000824
#define BCE_MISC_CONFIG_LFSR_DIV			 (0xffffL<<0)

#define BCE_MISC_LFSR_MASK_BITS			0x00000828
#define BCE_MISC_LFSR_MASK_BITS_TX_SCHEDULER_ENABLE	 (1L<<0)
#define BCE_MISC_LFSR_MASK_BITS_TX_BD_READ_ENABLE	 (1L<<1)
#define BCE_MISC_LFSR_MASK_BITS_TX_BD_CACHE_ENABLE	 (1L<<2)
#define BCE_MISC_LFSR_MASK_BITS_TX_PROCESSOR_ENABLE	 (1L<<3)
#define BCE_MISC_LFSR_MASK_BITS_TX_DMA_ENABLE		 (1L<<4)
#define BCE_MISC_LFSR_MASK_BITS_TX_PATCHUP_ENABLE	 (1L<<5)
#define BCE_MISC_LFSR_MASK_BITS_TX_PAYLOAD_Q_ENABLE	 (1L<<6)
#define BCE_MISC_LFSR_MASK_BITS_TX_HEADER_Q_ENABLE	 (1L<<7)
#define BCE_MISC_LFSR_MASK_BITS_TX_ASSEMBLER_ENABLE	 (1L<<8)
#define BCE_MISC_LFSR_MASK_BITS_EMAC_ENABLE		 (1L<<9)
#define BCE_MISC_LFSR_MASK_BITS_RX_PARSER_MAC_ENABLE	 (1L<<10)
#define BCE_MISC_LFSR_MASK_BITS_RX_PARSER_CATCHUP_ENABLE	 (1L<<11)
#define BCE_MISC_LFSR_MASK_BITS_RX_MBUF_ENABLE		 (1L<<12)
#define BCE_MISC_LFSR_MASK_BITS_RX_LOOKUP_ENABLE	 (1L<<13)
#define BCE_MISC_LFSR_MASK_BITS_RX_PROCESSOR_ENABLE	 (1L<<14)
#define BCE_MISC_LFSR_MASK_BITS_RX_V2P_ENABLE		 (1L<<15)
#define BCE_MISC_LFSR_MASK_BITS_RX_BD_CACHE_ENABLE	 (1L<<16)
#define BCE_MISC_LFSR_MASK_BITS_RX_DMA_ENABLE		 (1L<<17)
#define BCE_MISC_LFSR_MASK_BITS_COMPLETION_ENABLE	 (1L<<18)
#define BCE_MISC_LFSR_MASK_BITS_HOST_COALESCE_ENABLE	 (1L<<19)
#define BCE_MISC_LFSR_MASK_BITS_MAILBOX_QUEUE_ENABLE	 (1L<<20)
#define BCE_MISC_LFSR_MASK_BITS_CONTEXT_ENABLE		 (1L<<21)
#define BCE_MISC_LFSR_MASK_BITS_CMD_SCHEDULER_ENABLE	 (1L<<22)
#define BCE_MISC_LFSR_MASK_BITS_CMD_PROCESSOR_ENABLE	 (1L<<23)
#define BCE_MISC_LFSR_MASK_BITS_MGMT_PROCESSOR_ENABLE	 (1L<<24)
#define BCE_MISC_LFSR_MASK_BITS_TIMER_ENABLE		 (1L<<25)
#define BCE_MISC_LFSR_MASK_BITS_DMA_ENGINE_ENABLE	 (1L<<26)
#define BCE_MISC_LFSR_MASK_BITS_UMP_ENABLE		 (1L<<27)
#define BCE_MISC_LFSR_MASK_BITS_RV2P_CMD_SCHEDULER_ENABLE	 (1L<<28)
#define BCE_MISC_LFSR_MASK_BITS_RSVD_FUTURE_ENABLE	 (0x7L<<29)

#define BCE_MISC_ARB_REQ0				0x0000082c
#define BCE_MISC_ARB_REQ1				0x00000830
#define BCE_MISC_ARB_REQ2				0x00000834
#define BCE_MISC_ARB_REQ3				0x00000838
#define BCE_MISC_ARB_REQ4				0x0000083c
#define BCE_MISC_ARB_FREE0				0x00000840
#define BCE_MISC_ARB_FREE1				0x00000844
#define BCE_MISC_ARB_FREE2				0x00000848
#define BCE_MISC_ARB_FREE3				0x0000084c
#define BCE_MISC_ARB_FREE4				0x00000850
#define BCE_MISC_ARB_REQ_STATUS0			0x00000854
#define BCE_MISC_ARB_REQ_STATUS1			0x00000858
#define BCE_MISC_ARB_REQ_STATUS2			0x0000085c
#define BCE_MISC_ARB_REQ_STATUS3			0x00000860
#define BCE_MISC_ARB_REQ_STATUS4			0x00000864
#define BCE_MISC_ARB_GNT0				0x00000868
#define BCE_MISC_ARB_GNT0_0				 (0x7L<<0)
#define BCE_MISC_ARB_GNT0_1				 (0x7L<<4)
#define BCE_MISC_ARB_GNT0_2				 (0x7L<<8)
#define BCE_MISC_ARB_GNT0_3				 (0x7L<<12)
#define BCE_MISC_ARB_GNT0_4				 (0x7L<<16)
#define BCE_MISC_ARB_GNT0_5				 (0x7L<<20)
#define BCE_MISC_ARB_GNT0_6				 (0x7L<<24)
#define BCE_MISC_ARB_GNT0_7				 (0x7L<<28)

#define BCE_MISC_ARB_GNT1				0x0000086c
#define BCE_MISC_ARB_GNT1_8				 (0x7L<<0)
#define BCE_MISC_ARB_GNT1_9				 (0x7L<<4)
#define BCE_MISC_ARB_GNT1_10				 (0x7L<<8)
#define BCE_MISC_ARB_GNT1_11				 (0x7L<<12)
#define BCE_MISC_ARB_GNT1_12				 (0x7L<<16)
#define BCE_MISC_ARB_GNT1_13				 (0x7L<<20)
#define BCE_MISC_ARB_GNT1_14				 (0x7L<<24)
#define BCE_MISC_ARB_GNT1_15				 (0x7L<<28)

#define BCE_MISC_ARB_GNT2				0x00000870
#define BCE_MISC_ARB_GNT2_16				 (0x7L<<0)
#define BCE_MISC_ARB_GNT2_17				 (0x7L<<4)
#define BCE_MISC_ARB_GNT2_18				 (0x7L<<8)
#define BCE_MISC_ARB_GNT2_19				 (0x7L<<12)
#define BCE_MISC_ARB_GNT2_20				 (0x7L<<16)
#define BCE_MISC_ARB_GNT2_21				 (0x7L<<20)
#define BCE_MISC_ARB_GNT2_22				 (0x7L<<24)
#define BCE_MISC_ARB_GNT2_23				 (0x7L<<28)

#define BCE_MISC_ARB_GNT3				0x00000874
#define BCE_MISC_ARB_GNT3_24				 (0x7L<<0)
#define BCE_MISC_ARB_GNT3_25				 (0x7L<<4)
#define BCE_MISC_ARB_GNT3_26				 (0x7L<<8)
#define BCE_MISC_ARB_GNT3_27				 (0x7L<<12)
#define BCE_MISC_ARB_GNT3_28				 (0x7L<<16)
#define BCE_MISC_ARB_GNT3_29				 (0x7L<<20)
#define BCE_MISC_ARB_GNT3_30				 (0x7L<<24)
#define BCE_MISC_ARB_GNT3_31				 (0x7L<<28)

#define BCE_MISC_RESERVED1				0x00000878
#define BCE_MISC_RESERVED1_MISC_RESERVED1_VALUE	 (0x3fL<<0)

#define BCE_MISC_RESERVED2				0x0000087c
#define BCE_MISC_RESERVED2_PCIE_DIS			 (1L<<0)
#define BCE_MISC_RESERVED2_LINK_IN_L23			 (1L<<1)

#define BCE_MISC_SM_ASF_CONTROL			0x00000880
#define BCE_MISC_SM_ASF_CONTROL_ASF_RST		 (1L<<0)
#define BCE_MISC_SM_ASF_CONTROL_TSC_EN			 (1L<<1)
#define BCE_MISC_SM_ASF_CONTROL_WG_TO			 (1L<<2)
#define BCE_MISC_SM_ASF_CONTROL_HB_TO			 (1L<<3)
#define BCE_MISC_SM_ASF_CONTROL_PA_TO			 (1L<<4)
#define BCE_MISC_SM_ASF_CONTROL_PL_TO			 (1L<<5)
#define BCE_MISC_SM_ASF_CONTROL_RT_TO			 (1L<<6)
#define BCE_MISC_SM_ASF_CONTROL_SMB_EVENT		 (1L<<7)
#define BCE_MISC_SM_ASF_CONTROL_STRETCH_EN		 (1L<<8)
#define BCE_MISC_SM_ASF_CONTROL_STRETCH_PULSE		 (1L<<9)
#define BCE_MISC_SM_ASF_CONTROL_RES			 (0x3L<<10)
#define BCE_MISC_SM_ASF_CONTROL_SMB_EN			 (1L<<12)
#define BCE_MISC_SM_ASF_CONTROL_SMB_BB_EN		 (1L<<13)
#define BCE_MISC_SM_ASF_CONTROL_SMB_NO_ADDR_FILT	 (1L<<14)
#define BCE_MISC_SM_ASF_CONTROL_SMB_AUTOREAD		 (1L<<15)
#define BCE_MISC_SM_ASF_CONTROL_NIC_SMB_ADDR1		 (0x7fL<<16)
#define BCE_MISC_SM_ASF_CONTROL_NIC_SMB_ADDR2		 (0x7fL<<23)
#define BCE_MISC_SM_ASF_CONTROL_EN_NIC_SMB_ADDR_0	 (1L<<30)
#define BCE_MISC_SM_ASF_CONTROL_SMB_EARLY_ATTN		 (1L<<31)

#define BCE_MISC_SMB_IN				0x00000884
#define BCE_MISC_SMB_IN_DAT_IN				 (0xffL<<0)
#define BCE_MISC_SMB_IN_RDY				 (1L<<8)
#define BCE_MISC_SMB_IN_DONE				 (1L<<9)
#define BCE_MISC_SMB_IN_FIRSTBYTE			 (1L<<10)
#define BCE_MISC_SMB_IN_STATUS				 (0x7L<<11)
#define BCE_MISC_SMB_IN_STATUS_OK			 (0x0L<<11)
#define BCE_MISC_SMB_IN_STATUS_PEC			 (0x1L<<11)
#define BCE_MISC_SMB_IN_STATUS_OFLOW			 (0x2L<<11)
#define BCE_MISC_SMB_IN_STATUS_STOP			 (0x3L<<11)
#define BCE_MISC_SMB_IN_STATUS_TIMEOUT			 (0x4L<<11)

#define BCE_MISC_SMB_OUT				0x00000888
#define BCE_MISC_SMB_OUT_DAT_OUT			 (0xffL<<0)
#define BCE_MISC_SMB_OUT_RDY				 (1L<<8)
#define BCE_MISC_SMB_OUT_START				 (1L<<9)
#define BCE_MISC_SMB_OUT_LAST				 (1L<<10)
#define BCE_MISC_SMB_OUT_ACC_TYPE			 (1L<<11)
#define BCE_MISC_SMB_OUT_ENB_PEC			 (1L<<12)
#define BCE_MISC_SMB_OUT_GET_RX_LEN			 (1L<<13)
#define BCE_MISC_SMB_OUT_SMB_READ_LEN			 (0x3fL<<14)
#define BCE_MISC_SMB_OUT_SMB_OUT_STATUS		 (0xfL<<20)
#define BCE_MISC_SMB_OUT_SMB_OUT_STATUS_OK		 (0L<<20)
#define BCE_MISC_SMB_OUT_SMB_OUT_STATUS_FIRST_NACK	 (1L<<20)
#define BCE_MISC_SMB_OUT_SMB_OUT_STATUS_UFLOW		 (2L<<20)
#define BCE_MISC_SMB_OUT_SMB_OUT_STATUS_STOP		 (3L<<20)
#define BCE_MISC_SMB_OUT_SMB_OUT_STATUS_TIMEOUT	 (4L<<20)
#define BCE_MISC_SMB_OUT_SMB_OUT_STATUS_FIRST_LOST	 (5L<<20)
#define BCE_MISC_SMB_OUT_SMB_OUT_STATUS_BADACK		 (6L<<20)
#define BCE_MISC_SMB_OUT_SMB_OUT_STATUS_SUB_NACK	 (9L<<20)
#define BCE_MISC_SMB_OUT_SMB_OUT_STATUS_SUB_LOST	 (0xdL<<20)
#define BCE_MISC_SMB_OUT_SMB_OUT_SLAVEMODE		 (1L<<24)
#define BCE_MISC_SMB_OUT_SMB_OUT_DAT_EN		 (1L<<25)
#define BCE_MISC_SMB_OUT_SMB_OUT_DAT_IN		 (1L<<26)
#define BCE_MISC_SMB_OUT_SMB_OUT_CLK_EN		 (1L<<27)
#define BCE_MISC_SMB_OUT_SMB_OUT_CLK_IN		 (1L<<28)

#define BCE_MISC_SMB_WATCHDOG				0x0000088c
#define BCE_MISC_SMB_WATCHDOG_WATCHDOG			 (0xffffL<<0)

#define BCE_MISC_SMB_HEARTBEAT				0x00000890
#define BCE_MISC_SMB_HEARTBEAT_HEARTBEAT		 (0xffffL<<0)

#define BCE_MISC_SMB_POLL_ASF				0x00000894
#define BCE_MISC_SMB_POLL_ASF_POLL_ASF			 (0xffffL<<0)

#define BCE_MISC_SMB_POLL_LEGACY			0x00000898
#define BCE_MISC_SMB_POLL_LEGACY_POLL_LEGACY		 (0xffffL<<0)

#define BCE_MISC_SMB_RETRAN				0x0000089c
#define BCE_MISC_SMB_RETRAN_RETRAN			 (0xffL<<0)

#define BCE_MISC_SMB_TIMESTAMP				0x000008a0
#define BCE_MISC_SMB_TIMESTAMP_TIMESTAMP		 (0xffffffffL<<0)

#define BCE_MISC_PERR_ENA0				0x000008a4
#define BCE_MISC_PERR_ENA0_COM_MISC_CTXC		 (1L<<0)
#define BCE_MISC_PERR_ENA0_COM_MISC_REGF		 (1L<<1)
#define BCE_MISC_PERR_ENA0_COM_MISC_SCPAD		 (1L<<2)
#define BCE_MISC_PERR_ENA0_CP_MISC_CTXC		 (1L<<3)
#define BCE_MISC_PERR_ENA0_CP_MISC_REGF		 (1L<<4)
#define BCE_MISC_PERR_ENA0_CP_MISC_SCPAD		 (1L<<5)
#define BCE_MISC_PERR_ENA0_CS_MISC_TMEM		 (1L<<6)
#define BCE_MISC_PERR_ENA0_CTX_MISC_ACCM0		 (1L<<7)
#define BCE_MISC_PERR_ENA0_CTX_MISC_ACCM1		 (1L<<8)
#define BCE_MISC_PERR_ENA0_CTX_MISC_ACCM2		 (1L<<9)
#define BCE_MISC_PERR_ENA0_CTX_MISC_ACCM3		 (1L<<10)
#define BCE_MISC_PERR_ENA0_CTX_MISC_ACCM4		 (1L<<11)
#define BCE_MISC_PERR_ENA0_CTX_MISC_ACCM5		 (1L<<12)
#define BCE_MISC_PERR_ENA0_CTX_MISC_PGTBL		 (1L<<13)
#define BCE_MISC_PERR_ENA0_DMAE_MISC_DR0		 (1L<<14)
#define BCE_MISC_PERR_ENA0_DMAE_MISC_DR1		 (1L<<15)
#define BCE_MISC_PERR_ENA0_DMAE_MISC_DR2		 (1L<<16)
#define BCE_MISC_PERR_ENA0_DMAE_MISC_DR3		 (1L<<17)
#define BCE_MISC_PERR_ENA0_DMAE_MISC_DR4		 (1L<<18)
#define BCE_MISC_PERR_ENA0_DMAE_MISC_DW0		 (1L<<19)
#define BCE_MISC_PERR_ENA0_DMAE_MISC_DW1		 (1L<<20)
#define BCE_MISC_PERR_ENA0_DMAE_MISC_DW2		 (1L<<21)
#define BCE_MISC_PERR_ENA0_HC_MISC_DMA			 (1L<<22)
#define BCE_MISC_PERR_ENA0_MCP_MISC_REGF		 (1L<<23)
#define BCE_MISC_PERR_ENA0_MCP_MISC_SCPAD		 (1L<<24)
#define BCE_MISC_PERR_ENA0_MQ_MISC_CTX			 (1L<<25)
#define BCE_MISC_PERR_ENA0_RBDC_MISC			 (1L<<26)
#define BCE_MISC_PERR_ENA0_RBUF_MISC_MB		 (1L<<27)
#define BCE_MISC_PERR_ENA0_RBUF_MISC_PTR		 (1L<<28)
#define BCE_MISC_PERR_ENA0_RDE_MISC_RPC		 (1L<<29)
#define BCE_MISC_PERR_ENA0_RDE_MISC_RPM		 (1L<<30)
#define BCE_MISC_PERR_ENA0_RV2P_MISC_CB0REGS		 (1L<<31)
#define BCE_MISC_PERR_ENA0_COM_DMAE_PERR_EN_XI		 (1L<<0)
#define BCE_MISC_PERR_ENA0_CP_DMAE_PERR_EN_XI		 (1L<<1)
#define BCE_MISC_PERR_ENA0_RPM_ACPIBEMEM_PERR_EN_XI	 (1L<<2)
#define BCE_MISC_PERR_ENA0_CTX_USAGE_CNT_PERR_EN_XI	 (1L<<3)
#define BCE_MISC_PERR_ENA0_CTX_PGTBL_PERR_EN_XI	 (1L<<4)
#define BCE_MISC_PERR_ENA0_CTX_CACHE_PERR_EN_XI	 (1L<<5)
#define BCE_MISC_PERR_ENA0_CTX_MIRROR_PERR_EN_XI	 (1L<<6)
#define BCE_MISC_PERR_ENA0_COM_CTXC_PERR_EN_XI		 (1L<<7)
#define BCE_MISC_PERR_ENA0_COM_SCPAD_PERR_EN_XI	 (1L<<8)
#define BCE_MISC_PERR_ENA0_CP_CTXC_PERR_EN_XI		 (1L<<9)
#define BCE_MISC_PERR_ENA0_CP_SCPAD_PERR_EN_XI		 (1L<<10)
#define BCE_MISC_PERR_ENA0_RXP_RBUFC_PERR_EN_XI	 (1L<<11)
#define BCE_MISC_PERR_ENA0_RXP_CTXC_PERR_EN_XI		 (1L<<12)
#define BCE_MISC_PERR_ENA0_RXP_SCPAD_PERR_EN_XI	 (1L<<13)
#define BCE_MISC_PERR_ENA0_TPAT_SCPAD_PERR_EN_XI	 (1L<<14)
#define BCE_MISC_PERR_ENA0_TXP_CTXC_PERR_EN_XI		 (1L<<15)
#define BCE_MISC_PERR_ENA0_TXP_SCPAD_PERR_EN_XI	 (1L<<16)
#define BCE_MISC_PERR_ENA0_CS_TMEM_PERR_EN_XI		 (1L<<17)
#define BCE_MISC_PERR_ENA0_MQ_CTX_PERR_EN_XI		 (1L<<18)
#define BCE_MISC_PERR_ENA0_RPM_DFIFOMEM_PERR_EN_XI	 (1L<<19)
#define BCE_MISC_PERR_ENA0_RPC_DFIFOMEM_PERR_EN_XI	 (1L<<20)
#define BCE_MISC_PERR_ENA0_RBUF_PTRMEM_PERR_EN_XI	 (1L<<21)
#define BCE_MISC_PERR_ENA0_RBUF_DATAMEM_PERR_EN_XI	 (1L<<22)
#define BCE_MISC_PERR_ENA0_RV2P_P2IRAM_PERR_EN_XI	 (1L<<23)
#define BCE_MISC_PERR_ENA0_RV2P_P1IRAM_PERR_EN_XI	 (1L<<24)
#define BCE_MISC_PERR_ENA0_RV2P_CB1REGS_PERR_EN_XI	 (1L<<25)
#define BCE_MISC_PERR_ENA0_RV2P_CB0REGS_PERR_EN_XI	 (1L<<26)
#define BCE_MISC_PERR_ENA0_TPBUF_PERR_EN_XI		 (1L<<27)
#define BCE_MISC_PERR_ENA0_THBUF_PERR_EN_XI		 (1L<<28)
#define BCE_MISC_PERR_ENA0_TDMA_PERR_EN_XI		 (1L<<29)
#define BCE_MISC_PERR_ENA0_TBDC_PERR_EN_XI		 (1L<<30)
#define BCE_MISC_PERR_ENA0_TSCH_LR_PERR_EN_XI		 (1L<<31)

#define BCE_MISC_PERR_ENA1				0x000008a8
#define BCE_MISC_PERR_ENA1_RV2P_MISC_CB1REGS		 (1L<<0)
#define BCE_MISC_PERR_ENA1_RV2P_MISC_P1IRAM		 (1L<<1)
#define BCE_MISC_PERR_ENA1_RV2P_MISC_P2IRAM		 (1L<<2)
#define BCE_MISC_PERR_ENA1_RXP_MISC_CTXC		 (1L<<3)
#define BCE_MISC_PERR_ENA1_RXP_MISC_REGF		 (1L<<4)
#define BCE_MISC_PERR_ENA1_RXP_MISC_SCPAD		 (1L<<5)
#define BCE_MISC_PERR_ENA1_RXP_MISC_RBUFC		 (1L<<6)
#define BCE_MISC_PERR_ENA1_TBDC_MISC			 (1L<<7)
#define BCE_MISC_PERR_ENA1_TDMA_MISC			 (1L<<8)
#define BCE_MISC_PERR_ENA1_THBUF_MISC_MB0		 (1L<<9)
#define BCE_MISC_PERR_ENA1_THBUF_MISC_MB1		 (1L<<10)
#define BCE_MISC_PERR_ENA1_TPAT_MISC_REGF		 (1L<<11)
#define BCE_MISC_PERR_ENA1_TPAT_MISC_SCPAD		 (1L<<12)
#define BCE_MISC_PERR_ENA1_TPBUF_MISC_MB		 (1L<<13)
#define BCE_MISC_PERR_ENA1_TSCH_MISC_LR		 (1L<<14)
#define BCE_MISC_PERR_ENA1_TXP_MISC_CTXC		 (1L<<15)
#define BCE_MISC_PERR_ENA1_TXP_MISC_REGF		 (1L<<16)
#define BCE_MISC_PERR_ENA1_TXP_MISC_SCPAD		 (1L<<17)
#define BCE_MISC_PERR_ENA1_UMP_MISC_FIORX		 (1L<<18)
#define BCE_MISC_PERR_ENA1_UMP_MISC_FIOTX		 (1L<<19)
#define BCE_MISC_PERR_ENA1_UMP_MISC_RX			 (1L<<20)
#define BCE_MISC_PERR_ENA1_UMP_MISC_TX			 (1L<<21)
#define BCE_MISC_PERR_ENA1_RDMAQ_MISC			 (1L<<22)
#define BCE_MISC_PERR_ENA1_CSQ_MISC			 (1L<<23)
#define BCE_MISC_PERR_ENA1_CPQ_MISC			 (1L<<24)
#define BCE_MISC_PERR_ENA1_MCPQ_MISC			 (1L<<25)
#define BCE_MISC_PERR_ENA1_RV2PMQ_MISC			 (1L<<26)
#define BCE_MISC_PERR_ENA1_RV2PPQ_MISC			 (1L<<27)
#define BCE_MISC_PERR_ENA1_RV2PTQ_MISC			 (1L<<28)
#define BCE_MISC_PERR_ENA1_RXPQ_MISC			 (1L<<29)
#define BCE_MISC_PERR_ENA1_RXPCQ_MISC			 (1L<<30)
#define BCE_MISC_PERR_ENA1_RLUPQ_MISC			 (1L<<31)
#define BCE_MISC_PERR_ENA1_RBDC_PERR_EN_XI		 (1L<<0)
#define BCE_MISC_PERR_ENA1_RDMA_DFIFO_PERR_EN_XI	 (1L<<2)
#define BCE_MISC_PERR_ENA1_HC_STATS_PERR_EN_XI		 (1L<<3)
#define BCE_MISC_PERR_ENA1_HC_MSIX_PERR_EN_XI		 (1L<<4)
#define BCE_MISC_PERR_ENA1_HC_PRODUCSTB_PERR_EN_XI	 (1L<<5)
#define BCE_MISC_PERR_ENA1_HC_CONSUMSTB_PERR_EN_XI	 (1L<<6)
#define BCE_MISC_PERR_ENA1_TPATQ_PERR_EN_XI		 (1L<<7)
#define BCE_MISC_PERR_ENA1_MCPQ_PERR_EN_XI		 (1L<<8)
#define BCE_MISC_PERR_ENA1_TDMAQ_PERR_EN_XI		 (1L<<9)
#define BCE_MISC_PERR_ENA1_TXPQ_PERR_EN_XI		 (1L<<10)
#define BCE_MISC_PERR_ENA1_COMTQ_PERR_EN_XI		 (1L<<11)
#define BCE_MISC_PERR_ENA1_COMQ_PERR_EN_XI		 (1L<<12)
#define BCE_MISC_PERR_ENA1_RLUPQ_PERR_EN_XI		 (1L<<13)
#define BCE_MISC_PERR_ENA1_RXPQ_PERR_EN_XI		 (1L<<14)
#define BCE_MISC_PERR_ENA1_RV2PPQ_PERR_EN_XI		 (1L<<15)
#define BCE_MISC_PERR_ENA1_RDMAQ_PERR_EN_XI		 (1L<<16)
#define BCE_MISC_PERR_ENA1_TASQ_PERR_EN_XI		 (1L<<17)
#define BCE_MISC_PERR_ENA1_TBDRQ_PERR_EN_XI		 (1L<<18)
#define BCE_MISC_PERR_ENA1_TSCHQ_PERR_EN_XI		 (1L<<19)
#define BCE_MISC_PERR_ENA1_COMXQ_PERR_EN_XI		 (1L<<20)
#define BCE_MISC_PERR_ENA1_RXPCQ_PERR_EN_XI		 (1L<<21)
#define BCE_MISC_PERR_ENA1_RV2PTQ_PERR_EN_XI		 (1L<<22)
#define BCE_MISC_PERR_ENA1_RV2PMQ_PERR_EN_XI		 (1L<<23)
#define BCE_MISC_PERR_ENA1_CPQ_PERR_EN_XI		 (1L<<24)
#define BCE_MISC_PERR_ENA1_CSQ_PERR_EN_XI		 (1L<<25)
#define BCE_MISC_PERR_ENA1_RLUP_CID_PERR_EN_XI		 (1L<<26)
#define BCE_MISC_PERR_ENA1_RV2PCS_TMEM_PERR_EN_XI	 (1L<<27)
#define BCE_MISC_PERR_ENA1_RV2PCSQ_PERR_EN_XI		 (1L<<28)
#define BCE_MISC_PERR_ENA1_MQ_IDX_PERR_EN_XI		 (1L<<29)

#define BCE_MISC_PERR_ENA2				0x000008ac
#define BCE_MISC_PERR_ENA2_COMQ_MISC			 (1L<<0)
#define BCE_MISC_PERR_ENA2_COMXQ_MISC			 (1L<<1)
#define BCE_MISC_PERR_ENA2_COMTQ_MISC			 (1L<<2)
#define BCE_MISC_PERR_ENA2_TSCHQ_MISC			 (1L<<3)
#define BCE_MISC_PERR_ENA2_TBDRQ_MISC			 (1L<<4)
#define BCE_MISC_PERR_ENA2_TXPQ_MISC			 (1L<<5)
#define BCE_MISC_PERR_ENA2_TDMAQ_MISC			 (1L<<6)
#define BCE_MISC_PERR_ENA2_TPATQ_MISC			 (1L<<7)
#define BCE_MISC_PERR_ENA2_TASQ_MISC			 (1L<<8)
#define BCE_MISC_PERR_ENA2_TGT_FIFO_PERR_EN_XI		 (1L<<0)
#define BCE_MISC_PERR_ENA2_UMP_TX_PERR_EN_XI		 (1L<<1)
#define BCE_MISC_PERR_ENA2_UMP_RX_PERR_EN_XI		 (1L<<2)
#define BCE_MISC_PERR_ENA2_MCP_ROM_PERR_EN_XI		 (1L<<3)
#define BCE_MISC_PERR_ENA2_MCP_SCPAD_PERR_EN_XI	 (1L<<4)
#define BCE_MISC_PERR_ENA2_HB_MEM_PERR_EN_XI		 (1L<<5)
#define BCE_MISC_PERR_ENA2_PCIE_REPLAY_PERR_EN_XI	 (1L<<6)

#define BCE_MISC_DEBUG_VECTOR_SEL			0x000008b0
#define BCE_MISC_DEBUG_VECTOR_SEL_0			 (0xfffL<<0)
#define BCE_MISC_DEBUG_VECTOR_SEL_1			 (0xfffL<<12)
#define BCE_MISC_DEBUG_VECTOR_SEL_1_XI			 (0xfffL<<15)

#define BCE_MISC_VREG_CONTROL				0x000008b4
#define BCE_MISC_VREG_CONTROL_1_2			 (0xfL<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_XI		 (0xfL<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_PLUS14_XI	 (0L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_PLUS12_XI	 (1L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_PLUS10_XI	 (2L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_PLUS8_XI	 (3L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_PLUS6_XI	 (4L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_PLUS4_XI	 (5L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_PLUS2_XI	 (6L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_NOM_XI		 (7L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_MINUS2_XI	 (8L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_MINUS4_XI	 (9L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_MINUS6_XI	 (10L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_MINUS8_XI	 (11L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_MINUS10_XI	 (12L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_MINUS12_XI	 (13L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_MINUS14_XI	 (14L<<0)
#define BCE_MISC_VREG_CONTROL_1_0_MAIN_MINUS16_XI	 (15L<<0)
#define BCE_MISC_VREG_CONTROL_2_5			 (0xfL<<4)
#define BCE_MISC_VREG_CONTROL_2_5_PLUS14		 (0L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_PLUS12		 (1L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_PLUS10		 (2L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_PLUS8		 (3L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_PLUS6		 (4L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_PLUS4		 (5L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_PLUS2		 (6L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_NOM			 (7L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_MINUS2		 (8L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_MINUS4		 (9L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_MINUS6		 (10L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_MINUS8		 (11L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_MINUS10		 (12L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_MINUS12		 (13L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_MINUS14		 (14L<<4)
#define BCE_MISC_VREG_CONTROL_2_5_MINUS16		 (15L<<4)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT			 (0xfL<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_PLUS14		 (0L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_PLUS12		 (1L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_PLUS10		 (2L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_PLUS8		 (3L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_PLUS6		 (4L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_PLUS4		 (5L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_PLUS2		 (6L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_NOM		 (7L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_MINUS2		 (8L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_MINUS4		 (9L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_MINUS6		 (10L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_MINUS8		 (11L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_MINUS10		 (12L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_MINUS12		 (13L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_MINUS14		 (14L<<8)
#define BCE_MISC_VREG_CONTROL_1_0_MGMT_MINUS16		 (15L<<8)

#define BCE_MISC_FINAL_CLK_CTL_VAL			0x000008b8
#define BCE_MISC_FINAL_CLK_CTL_VAL_MISC_FINAL_CLK_CTL_VAL	 (0x3ffffffL<<6)

#define BCE_MISC_GP_HW_CTL0				0x000008bc
#define BCE_MISC_GP_HW_CTL0_TX_DRIVE			 (1L<<0)
#define BCE_MISC_GP_HW_CTL0_RMII_MODE			 (1L<<1)
#define BCE_MISC_GP_HW_CTL0_RMII_CRSDV_SEL		 (1L<<2)
#define BCE_MISC_GP_HW_CTL0_RVMII_MODE			 (1L<<3)
#define BCE_MISC_GP_HW_CTL0_FLASH_SAMP_SCLK_NEGEDGE_TE	 (1L<<4)
#define BCE_MISC_GP_HW_CTL0_HIDDEN_REVISION_ID_TE	 (1L<<5)
#define BCE_MISC_GP_HW_CTL0_HC_CNTL_TMOUT_CTR_RST_TE	 (1L<<6)
#define BCE_MISC_GP_HW_CTL0_RESERVED1_XI		 (0x7L<<4)
#define BCE_MISC_GP_HW_CTL0_ENA_CORE_RST_ON_MAIN_PWR_GOING_AWAY	 (1L<<7)
#define BCE_MISC_GP_HW_CTL0_ENA_SEL_VAUX_B_IN_L2_TE	 (1L<<8)
#define BCE_MISC_GP_HW_CTL0_GRC_BNK_FREE_FIX_TE	 (1L<<9)
#define BCE_MISC_GP_HW_CTL0_LED_ACT_SEL_TE		 (1L<<10)
#define BCE_MISC_GP_HW_CTL0_RESERVED2_XI		 (0x7L<<8)
#define BCE_MISC_GP_HW_CTL0_UP1_DEF0			 (1L<<11)
#define BCE_MISC_GP_HW_CTL0_FIBER_MODE_DIS_DEF		 (1L<<12)
#define BCE_MISC_GP_HW_CTL0_FORCE2500_DEF		 (1L<<13)
#define BCE_MISC_GP_HW_CTL0_AUTODETECT_DIS_DEF		 (1L<<14)
#define BCE_MISC_GP_HW_CTL0_PARALLEL_DETECT_DEF	 (1L<<15)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_DAI		 (0xfL<<16)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_DAI_3MA		 (0L<<16)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_DAI_2P5MA		 (1L<<16)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_DAI_2P0MA		 (3L<<16)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_DAI_1P5MA		 (5L<<16)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_DAI_1P0MA		 (7L<<16)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_DAI_PWRDN		 (15L<<16)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_PRE2DIS		 (1L<<20)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_PRE1DIS		 (1L<<21)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_CTAT		 (0x3L<<22)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_CTAT_M6P		 (0L<<22)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_CTAT_M0P		 (1L<<22)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_CTAT_P0P		 (2L<<22)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_CTAT_P6P		 (3L<<22)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_PTAT		 (0x3L<<24)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_PTAT_M6P		 (0L<<24)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_PTAT_M0P		 (1L<<24)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_PTAT_P0P		 (2L<<24)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_PTAT_P6P		 (3L<<24)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_IAMP_ADJ		 (0x3L<<26)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_IAMP_ADJ_240UA	 (0L<<26)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_IAMP_ADJ_160UA	 (1L<<26)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_IAMP_ADJ_400UA	 (2L<<26)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_IAMP_ADJ_320UA	 (3L<<26)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_ICBUF_ADJ		 (0x3L<<28)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_ICBUF_ADJ_240UA	 (0L<<28)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_ICBUF_ADJ_160UA	 (1L<<28)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_ICBUF_ADJ_400UA	 (2L<<28)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_ICBUF_ADJ_320UA	 (3L<<28)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_XTAL_ADJ		 (0x3L<<30)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_XTAL_ADJ_1P57	 (0L<<30)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_XTAL_ADJ_1P45	 (1L<<30)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_XTAL_ADJ_1P62	 (2L<<30)
#define BCE_MISC_GP_HW_CTL0_OSCCTRL_XTAL_ADJ_1P66	 (3L<<30)

#define BCE_MISC_GP_HW_CTL1				0x000008c0
#define BCE_MISC_GP_HW_CTL1_1_ATTN_BTN_PRSNT_TE	 (1L<<0)
#define BCE_MISC_GP_HW_CTL1_1_ATTN_IND_PRSNT_TE	 (1L<<1)
#define BCE_MISC_GP_HW_CTL1_1_PWR_IND_PRSNT_TE		 (1L<<2)
#define BCE_MISC_GP_HW_CTL1_0_PCIE_LOOPBACK_TE		 (1L<<3)
#define BCE_MISC_GP_HW_CTL1_RESERVED_SOFT_XI		 (0xffffL<<0)
#define BCE_MISC_GP_HW_CTL1_RESERVED_HARD_XI		 (0xffffL<<16)

#define BCE_MISC_NEW_HW_CTL				0x000008c4
#define BCE_MISC_NEW_HW_CTL_MAIN_POR_BYPASS		 (1L<<0)
#define BCE_MISC_NEW_HW_CTL_RINGOSC_ENABLE		 (1L<<1)
#define BCE_MISC_NEW_HW_CTL_RINGOSC_SEL0		 (1L<<2)
#define BCE_MISC_NEW_HW_CTL_RINGOSC_SEL1		 (1L<<3)
#define BCE_MISC_NEW_HW_CTL_RESERVED_SHARED		 (0xfffL<<4)
#define BCE_MISC_NEW_HW_CTL_RESERVED_SPLIT		 (0xffffL<<16)

#define BCE_MISC_NEW_CORE_CTL				0x000008c8
#define BCE_MISC_NEW_CORE_CTL_LINK_HOLDOFF_SUCCESS	 (1L<<0)
#define BCE_MISC_NEW_CORE_CTL_LINK_HOLDOFF_REQ		 (1L<<1)
#define BCE_MISC_NEW_CORE_CTL_DMA_ENABLE		 (1L<<16)
#define BCE_MISC_NEW_CORE_CTL_RESERVED_CMN		 (0x3fffL<<2)
#define BCE_MISC_NEW_CORE_CTL_RESERVED_TC		 (0xffffL<<16)

#define BCE_MISC_ECO_HW_CTL				0x000008cc
#define BCE_MISC_ECO_HW_CTL_LARGE_GRC_TMOUT_EN		 (1L<<0)
#define BCE_MISC_ECO_HW_CTL_RESERVED_SOFT		 (0x7fffL<<1)
#define BCE_MISC_ECO_HW_CTL_RESERVED_HARD		 (0xffffL<<16)

#define BCE_MISC_ECO_CORE_CTL				0x000008d0
#define BCE_MISC_ECO_CORE_CTL_RESERVED_SOFT		 (0xffffL<<0)
#define BCE_MISC_ECO_CORE_CTL_RESERVED_HARD		 (0xffffL<<16)

#define BCE_MISC_PPIO					0x000008d4
#define BCE_MISC_PPIO_VALUE				 (0xfL<<0)
#define BCE_MISC_PPIO_SET				 (0xfL<<8)
#define BCE_MISC_PPIO_CLR				 (0xfL<<16)
#define BCE_MISC_PPIO_FLOAT				 (0xfL<<24)

#define BCE_MISC_PPIO_INT				0x000008d8
#define BCE_MISC_PPIO_INT_INT_STATE			 (0xfL<<0)
#define BCE_MISC_PPIO_INT_OLD_VALUE			 (0xfL<<8)
#define BCE_MISC_PPIO_INT_OLD_SET			 (0xfL<<16)
#define BCE_MISC_PPIO_INT_OLD_CLR			 (0xfL<<24)

#define BCE_MISC_RESET_NUMS				0x000008dc
#define BCE_MISC_RESET_NUMS_NUM_HARD_RESETS		 (0x7L<<0)
#define BCE_MISC_RESET_NUMS_NUM_PCIE_RESETS		 (0x7L<<4)
#define BCE_MISC_RESET_NUMS_NUM_PERSTB_RESETS		 (0x7L<<8)
#define BCE_MISC_RESET_NUMS_NUM_CMN_RESETS		 (0x7L<<12)
#define BCE_MISC_RESET_NUMS_NUM_PORT_RESETS		 (0x7L<<16)

#define BCE_MISC_CS16_ERR				0x000008e0
#define BCE_MISC_CS16_ERR_ENA_PCI			 (1L<<0)
#define BCE_MISC_CS16_ERR_ENA_RDMA			 (1L<<1)
#define BCE_MISC_CS16_ERR_ENA_TDMA			 (1L<<2)
#define BCE_MISC_CS16_ERR_ENA_EMAC			 (1L<<3)
#define BCE_MISC_CS16_ERR_ENA_CTX			 (1L<<4)
#define BCE_MISC_CS16_ERR_ENA_TBDR			 (1L<<5)
#define BCE_MISC_CS16_ERR_ENA_RBDC			 (1L<<6)
#define BCE_MISC_CS16_ERR_ENA_COM			 (1L<<7)
#define BCE_MISC_CS16_ERR_ENA_CP			 (1L<<8)
#define BCE_MISC_CS16_ERR_STA_PCI			 (1L<<16)
#define BCE_MISC_CS16_ERR_STA_RDMA			 (1L<<17)
#define BCE_MISC_CS16_ERR_STA_TDMA			 (1L<<18)
#define BCE_MISC_CS16_ERR_STA_EMAC			 (1L<<19)
#define BCE_MISC_CS16_ERR_STA_CTX			 (1L<<20)
#define BCE_MISC_CS16_ERR_STA_TBDR			 (1L<<21)
#define BCE_MISC_CS16_ERR_STA_RBDC			 (1L<<22)
#define BCE_MISC_CS16_ERR_STA_COM			 (1L<<23)
#define BCE_MISC_CS16_ERR_STA_CP			 (1L<<24)

#define BCE_MISC_SPIO_EVENT				0x000008e4
#define BCE_MISC_SPIO_EVENT_ENABLE			 (0xffL<<0)

#define BCE_MISC_PPIO_EVENT				0x000008e8
#define BCE_MISC_PPIO_EVENT_ENABLE			 (0xfL<<0)

#define BCE_MISC_DUAL_MEDIA_CTRL			0x000008ec
#define BCE_MISC_DUAL_MEDIA_CTRL_BOND_ID		 (0xffL<<0)
#define BCE_MISC_DUAL_MEDIA_CTRL_BOND_ID_X		 (0L<<0)
#define BCE_MISC_DUAL_MEDIA_CTRL_BOND_ID_C		 (3L<<0)
#define BCE_MISC_DUAL_MEDIA_CTRL_BOND_ID_S		 (12L<<0)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY_CTRL_STRAP	 (0x7L<<8)
#define BCE_MISC_DUAL_MEDIA_CTRL_PORT_SWAP_PIN		 (1L<<11)
#define BCE_MISC_DUAL_MEDIA_CTRL_SERDES1_SIGDET	 (1L<<12)
#define BCE_MISC_DUAL_MEDIA_CTRL_SERDES0_SIGDET	 (1L<<13)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY1_SIGDET		 (1L<<14)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY0_SIGDET		 (1L<<15)
#define BCE_MISC_DUAL_MEDIA_CTRL_LCPLL_RST		 (1L<<16)
#define BCE_MISC_DUAL_MEDIA_CTRL_SERDES1_RST		 (1L<<17)
#define BCE_MISC_DUAL_MEDIA_CTRL_SERDES0_RST		 (1L<<18)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY1_RST		 (1L<<19)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY0_RST		 (1L<<20)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY_CTRL		 (0x7L<<21)
#define BCE_MISC_DUAL_MEDIA_CTRL_PORT_SWAP		 (1L<<24)
#define BCE_MISC_DUAL_MEDIA_CTRL_STRAP_OVERRIDE	 (1L<<25)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY_SERDES_IDDQ	 (0xfL<<26)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY_SERDES_IDDQ_SER1_IDDQ	 (1L<<26)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY_SERDES_IDDQ_SER0_IDDQ	 (2L<<26)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY_SERDES_IDDQ_PHY1_IDDQ	 (4L<<26)
#define BCE_MISC_DUAL_MEDIA_CTRL_PHY_SERDES_IDDQ_PHY0_IDDQ	 (8L<<26)

#define BCE_MISC_OTP_CMD1				0x000008f0
#define BCE_MISC_OTP_CMD1_FMODE			 (0x7L<<0)
#define BCE_MISC_OTP_CMD1_FMODE_IDLE			 (0L<<0)
#define BCE_MISC_OTP_CMD1_FMODE_WRITE			 (1L<<0)
#define BCE_MISC_OTP_CMD1_FMODE_INIT			 (2L<<0)
#define BCE_MISC_OTP_CMD1_FMODE_SET			 (3L<<0)
#define BCE_MISC_OTP_CMD1_FMODE_RST			 (4L<<0)
#define BCE_MISC_OTP_CMD1_FMODE_VERIFY			 (5L<<0)
#define BCE_MISC_OTP_CMD1_FMODE_RESERVED0		 (6L<<0)
#define BCE_MISC_OTP_CMD1_FMODE_RESERVED1		 (7L<<0)
#define BCE_MISC_OTP_CMD1_USEPINS			 (1L<<8)
#define BCE_MISC_OTP_CMD1_PROGSEL			 (1L<<9)
#define BCE_MISC_OTP_CMD1_PROGSTART			 (1L<<10)
#define BCE_MISC_OTP_CMD1_PCOUNT			 (0x7L<<16)
#define BCE_MISC_OTP_CMD1_PBYP				 (1L<<19)
#define BCE_MISC_OTP_CMD1_VSEL				 (0xfL<<20)
#define BCE_MISC_OTP_CMD1_TM				 (0x7L<<27)
#define BCE_MISC_OTP_CMD1_SADBYP			 (1L<<30)
#define BCE_MISC_OTP_CMD1_DEBUG			 (1L<<31)

#define BCE_MISC_OTP_CMD2				0x000008f4
#define BCE_MISC_OTP_CMD2_OTP_ROM_ADDR			 (0x3ffL<<0)
#define BCE_MISC_OTP_CMD2_DOSEL			 (0x7fL<<16)
#define BCE_MISC_OTP_CMD2_DOSEL_0			 (0L<<16)
#define BCE_MISC_OTP_CMD2_DOSEL_1			 (1L<<16)
#define BCE_MISC_OTP_CMD2_DOSEL_127			 (127L<<16)

#define BCE_MISC_OTP_STATUS				0x000008f8
#define BCE_MISC_OTP_STATUS_DATA			 (0xffL<<0)
#define BCE_MISC_OTP_STATUS_VALID			 (1L<<8)
#define BCE_MISC_OTP_STATUS_BUSY			 (1L<<9)
#define BCE_MISC_OTP_STATUS_BUSYSM			 (1L<<10)
#define BCE_MISC_OTP_STATUS_DONE			 (1L<<11)

#define BCE_MISC_OTP_SHIFT1_CMD			0x000008fc
#define BCE_MISC_OTP_SHIFT1_CMD_RESET_MODE_N		 (1L<<0)
#define BCE_MISC_OTP_SHIFT1_CMD_SHIFT_DONE		 (1L<<1)
#define BCE_MISC_OTP_SHIFT1_CMD_SHIFT_START		 (1L<<2)
#define BCE_MISC_OTP_SHIFT1_CMD_LOAD_DATA		 (1L<<3)
#define BCE_MISC_OTP_SHIFT1_CMD_SHIFT_SELECT		 (0x1fL<<8)

#define BCE_MISC_OTP_SHIFT1_DATA			0x00000900
#define BCE_MISC_OTP_SHIFT2_CMD			0x00000904
#define BCE_MISC_OTP_SHIFT2_CMD_RESET_MODE_N		 (1L<<0)
#define BCE_MISC_OTP_SHIFT2_CMD_SHIFT_DONE		 (1L<<1)
#define BCE_MISC_OTP_SHIFT2_CMD_SHIFT_START		 (1L<<2)
#define BCE_MISC_OTP_SHIFT2_CMD_LOAD_DATA		 (1L<<3)
#define BCE_MISC_OTP_SHIFT2_CMD_SHIFT_SELECT		 (0x1fL<<8)

#define BCE_MISC_OTP_SHIFT2_DATA			0x00000908
#define BCE_MISC_BIST_CS0				0x0000090c
#define BCE_MISC_BIST_CS0_MBIST_EN			 (1L<<0)
#define BCE_MISC_BIST_CS0_BIST_SETUP			 (0x3L<<1)
#define BCE_MISC_BIST_CS0_MBIST_ASYNC_RESET		 (1L<<3)
#define BCE_MISC_BIST_CS0_MBIST_DONE			 (1L<<8)
#define BCE_MISC_BIST_CS0_MBIST_GO			 (1L<<9)
#define BCE_MISC_BIST_CS0_BIST_OVERRIDE		 (1L<<31)

#define BCE_MISC_BIST_MEMSTATUS0			0x00000910
#define BCE_MISC_BIST_CS1				0x00000914
#define BCE_MISC_BIST_CS1_MBIST_EN			 (1L<<0)
#define BCE_MISC_BIST_CS1_BIST_SETUP			 (0x3L<<1)
#define BCE_MISC_BIST_CS1_MBIST_ASYNC_RESET		 (1L<<3)
#define BCE_MISC_BIST_CS1_MBIST_DONE			 (1L<<8)
#define BCE_MISC_BIST_CS1_MBIST_GO			 (1L<<9)

#define BCE_MISC_BIST_MEMSTATUS1			0x00000918
#define BCE_MISC_BIST_CS2				0x0000091c
#define BCE_MISC_BIST_CS2_MBIST_EN			 (1L<<0)
#define BCE_MISC_BIST_CS2_BIST_SETUP			 (0x3L<<1)
#define BCE_MISC_BIST_CS2_MBIST_ASYNC_RESET		 (1L<<3)
#define BCE_MISC_BIST_CS2_MBIST_DONE			 (1L<<8)
#define BCE_MISC_BIST_CS2_MBIST_GO			 (1L<<9)

#define BCE_MISC_BIST_MEMSTATUS2			0x00000920
#define BCE_MISC_BIST_CS3				0x00000924
#define BCE_MISC_BIST_CS3_MBIST_EN			 (1L<<0)
#define BCE_MISC_BIST_CS3_BIST_SETUP			 (0x3L<<1)
#define BCE_MISC_BIST_CS3_MBIST_ASYNC_RESET		 (1L<<3)
#define BCE_MISC_BIST_CS3_MBIST_DONE			 (1L<<8)
#define BCE_MISC_BIST_CS3_MBIST_GO			 (1L<<9)

#define BCE_MISC_BIST_MEMSTATUS3			0x00000928
#define BCE_MISC_BIST_CS4				0x0000092c
#define BCE_MISC_BIST_CS4_MBIST_EN			 (1L<<0)
#define BCE_MISC_BIST_CS4_BIST_SETUP			 (0x3L<<1)
#define BCE_MISC_BIST_CS4_MBIST_ASYNC_RESET		 (1L<<3)
#define BCE_MISC_BIST_CS4_MBIST_DONE			 (1L<<8)
#define BCE_MISC_BIST_CS4_MBIST_GO			 (1L<<9)

#define BCE_MISC_BIST_MEMSTATUS4			0x00000930
#define BCE_MISC_BIST_CS5				0x00000934
#define BCE_MISC_BIST_CS5_MBIST_EN			 (1L<<0)
#define BCE_MISC_BIST_CS5_BIST_SETUP			 (0x3L<<1)
#define BCE_MISC_BIST_CS5_MBIST_ASYNC_RESET		 (1L<<3)
#define BCE_MISC_BIST_CS5_MBIST_DONE			 (1L<<8)
#define BCE_MISC_BIST_CS5_MBIST_GO			 (1L<<9)

#define BCE_MISC_BIST_MEMSTATUS5			0x00000938
#define BCE_MISC_MEM_TM0				0x0000093c
#define BCE_MISC_MEM_TM0_PCIE_REPLAY_TM		 (0xfL<<0)
#define BCE_MISC_MEM_TM0_MCP_SCPAD			 (0xfL<<8)
#define BCE_MISC_MEM_TM0_UMP_TM			 (0xffL<<16)
#define BCE_MISC_MEM_TM0_HB_MEM_TM			 (0xfL<<24)

#define BCE_MISC_USPLL_CTRL				0x00000940
#define BCE_MISC_USPLL_CTRL_PH_DET_DIS			 (1L<<0)
#define BCE_MISC_USPLL_CTRL_FREQ_DET_DIS		 (1L<<1)
#define BCE_MISC_USPLL_CTRL_LCPX			 (0x3fL<<2)
#define BCE_MISC_USPLL_CTRL_RX				 (0x3L<<8)
#define BCE_MISC_USPLL_CTRL_VC_EN			 (1L<<10)
#define BCE_MISC_USPLL_CTRL_VCO_MG			 (0x3L<<11)
#define BCE_MISC_USPLL_CTRL_KVCO_XF			 (0x7L<<13)
#define BCE_MISC_USPLL_CTRL_KVCO_XS			 (0x7L<<16)
#define BCE_MISC_USPLL_CTRL_TESTD_EN			 (1L<<19)
#define BCE_MISC_USPLL_CTRL_TESTD_SEL			 (0x7L<<20)
#define BCE_MISC_USPLL_CTRL_TESTA_EN			 (1L<<23)
#define BCE_MISC_USPLL_CTRL_TESTA_SEL			 (0x3L<<24)
#define BCE_MISC_USPLL_CTRL_ATTEN_FREF			 (1L<<26)
#define BCE_MISC_USPLL_CTRL_DIGITAL_RST		 (1L<<27)
#define BCE_MISC_USPLL_CTRL_ANALOG_RST			 (1L<<28)
#define BCE_MISC_USPLL_CTRL_LOCK			 (1L<<29)

#define BCE_MISC_PERR_STATUS0				0x00000944
#define BCE_MISC_PERR_STATUS0_COM_DMAE_PERR		 (1L<<0)
#define BCE_MISC_PERR_STATUS0_CP_DMAE_PERR		 (1L<<1)
#define BCE_MISC_PERR_STATUS0_RPM_ACPIBEMEM_PERR	 (1L<<2)
#define BCE_MISC_PERR_STATUS0_CTX_USAGE_CNT_PERR	 (1L<<3)
#define BCE_MISC_PERR_STATUS0_CTX_PGTBL_PERR		 (1L<<4)
#define BCE_MISC_PERR_STATUS0_CTX_CACHE_PERR		 (1L<<5)
#define BCE_MISC_PERR_STATUS0_CTX_MIRROR_PERR		 (1L<<6)
#define BCE_MISC_PERR_STATUS0_COM_CTXC_PERR		 (1L<<7)
#define BCE_MISC_PERR_STATUS0_COM_SCPAD_PERR		 (1L<<8)
#define BCE_MISC_PERR_STATUS0_CP_CTXC_PERR		 (1L<<9)
#define BCE_MISC_PERR_STATUS0_CP_SCPAD_PERR		 (1L<<10)
#define BCE_MISC_PERR_STATUS0_RXP_RBUFC_PERR		 (1L<<11)
#define BCE_MISC_PERR_STATUS0_RXP_CTXC_PERR		 (1L<<12)
#define BCE_MISC_PERR_STATUS0_RXP_SCPAD_PERR		 (1L<<13)
#define BCE_MISC_PERR_STATUS0_TPAT_SCPAD_PERR		 (1L<<14)
#define BCE_MISC_PERR_STATUS0_TXP_CTXC_PERR		 (1L<<15)
#define BCE_MISC_PERR_STATUS0_TXP_SCPAD_PERR		 (1L<<16)
#define BCE_MISC_PERR_STATUS0_CS_TMEM_PERR		 (1L<<17)
#define BCE_MISC_PERR_STATUS0_MQ_CTX_PERR		 (1L<<18)
#define BCE_MISC_PERR_STATUS0_RPM_DFIFOMEM_PERR	 (1L<<19)
#define BCE_MISC_PERR_STATUS0_RPC_DFIFOMEM_PERR	 (1L<<20)
#define BCE_MISC_PERR_STATUS0_RBUF_PTRMEM_PERR		 (1L<<21)
#define BCE_MISC_PERR_STATUS0_RBUF_DATAMEM_PERR	 (1L<<22)
#define BCE_MISC_PERR_STATUS0_RV2P_P2IRAM_PERR		 (1L<<23)
#define BCE_MISC_PERR_STATUS0_RV2P_P1IRAM_PERR		 (1L<<24)
#define BCE_MISC_PERR_STATUS0_RV2P_CB1REGS_PERR	 (1L<<25)
#define BCE_MISC_PERR_STATUS0_RV2P_CB0REGS_PERR	 (1L<<26)
#define BCE_MISC_PERR_STATUS0_TPBUF_PERR		 (1L<<27)
#define BCE_MISC_PERR_STATUS0_THBUF_PERR		 (1L<<28)
#define BCE_MISC_PERR_STATUS0_TDMA_PERR		 (1L<<29)
#define BCE_MISC_PERR_STATUS0_TBDC_PERR		 (1L<<30)
#define BCE_MISC_PERR_STATUS0_TSCH_LR_PERR		 (1L<<31)

#define BCE_MISC_PERR_STATUS1				0x00000948
#define BCE_MISC_PERR_STATUS1_RBDC_PERR		 (1L<<0)
#define BCE_MISC_PERR_STATUS1_RDMA_DFIFO_PERR		 (1L<<2)
#define BCE_MISC_PERR_STATUS1_HC_STATS_PERR		 (1L<<3)
#define BCE_MISC_PERR_STATUS1_HC_MSIX_PERR		 (1L<<4)
#define BCE_MISC_PERR_STATUS1_HC_PRODUCSTB_PERR	 (1L<<5)
#define BCE_MISC_PERR_STATUS1_HC_CONSUMSTB_PERR	 (1L<<6)
#define BCE_MISC_PERR_STATUS1_TPATQ_PERR		 (1L<<7)
#define BCE_MISC_PERR_STATUS1_MCPQ_PERR		 (1L<<8)
#define BCE_MISC_PERR_STATUS1_TDMAQ_PERR		 (1L<<9)
#define BCE_MISC_PERR_STATUS1_TXPQ_PERR		 (1L<<10)
#define BCE_MISC_PERR_STATUS1_COMTQ_PERR		 (1L<<11)
#define BCE_MISC_PERR_STATUS1_COMQ_PERR		 (1L<<12)
#define BCE_MISC_PERR_STATUS1_RLUPQ_PERR		 (1L<<13)
#define BCE_MISC_PERR_STATUS1_RXPQ_PERR		 (1L<<14)
#define BCE_MISC_PERR_STATUS1_RV2PPQ_PERR		 (1L<<15)
#define BCE_MISC_PERR_STATUS1_RDMAQ_PERR		 (1L<<16)
#define BCE_MISC_PERR_STATUS1_TASQ_PERR		 (1L<<17)
#define BCE_MISC_PERR_STATUS1_TBDRQ_PERR		 (1L<<18)
#define BCE_MISC_PERR_STATUS1_TSCHQ_PERR		 (1L<<19)
#define BCE_MISC_PERR_STATUS1_COMXQ_PERR		 (1L<<20)
#define BCE_MISC_PERR_STATUS1_RXPCQ_PERR		 (1L<<21)
#define BCE_MISC_PERR_STATUS1_RV2PTQ_PERR		 (1L<<22)
#define BCE_MISC_PERR_STATUS1_RV2PMQ_PERR		 (1L<<23)
#define BCE_MISC_PERR_STATUS1_CPQ_PERR			 (1L<<24)
#define BCE_MISC_PERR_STATUS1_CSQ_PERR			 (1L<<25)
#define BCE_MISC_PERR_STATUS1_RLUP_CID_PERR		 (1L<<26)
#define BCE_MISC_PERR_STATUS1_RV2PCS_TMEM_PERR		 (1L<<27)
#define BCE_MISC_PERR_STATUS1_RV2PCSQ_PERR		 (1L<<28)
#define BCE_MISC_PERR_STATUS1_MQ_IDX_PERR		 (1L<<29)

#define BCE_MISC_PERR_STATUS2				0x0000094c
#define BCE_MISC_PERR_STATUS2_TGT_FIFO_PERR		 (1L<<0)
#define BCE_MISC_PERR_STATUS2_UMP_TX_PERR		 (1L<<1)
#define BCE_MISC_PERR_STATUS2_UMP_RX_PERR		 (1L<<2)
#define BCE_MISC_PERR_STATUS2_MCP_ROM_PERR		 (1L<<3)
#define BCE_MISC_PERR_STATUS2_MCP_SCPAD_PERR		 (1L<<4)
#define BCE_MISC_PERR_STATUS2_HB_MEM_PERR		 (1L<<5)
#define BCE_MISC_PERR_STATUS2_PCIE_REPLAY_PERR		 (1L<<6)

#define BCE_MISC_LCPLL_CTRL0				0x00000950
#define BCE_MISC_LCPLL_CTRL0_OAC			 (0x7L<<0)
#define BCE_MISC_LCPLL_CTRL0_OAC_NEGTWENTY		 (0L<<0)
#define BCE_MISC_LCPLL_CTRL0_OAC_ZERO			 (1L<<0)
#define BCE_MISC_LCPLL_CTRL0_OAC_TWENTY		 (3L<<0)
#define BCE_MISC_LCPLL_CTRL0_OAC_FORTY			 (7L<<0)
#define BCE_MISC_LCPLL_CTRL0_ICP_CTRL			 (0x7L<<3)
#define BCE_MISC_LCPLL_CTRL0_ICP_CTRL_360		 (0L<<3)
#define BCE_MISC_LCPLL_CTRL0_ICP_CTRL_480		 (1L<<3)
#define BCE_MISC_LCPLL_CTRL0_ICP_CTRL_600		 (3L<<3)
#define BCE_MISC_LCPLL_CTRL0_ICP_CTRL_720		 (7L<<3)
#define BCE_MISC_LCPLL_CTRL0_BIAS_CTRL			 (0x3L<<6)
#define BCE_MISC_LCPLL_CTRL0_PLL_OBSERVE		 (0x7L<<8)
#define BCE_MISC_LCPLL_CTRL0_VTH_CTRL			 (0x3L<<11)
#define BCE_MISC_LCPLL_CTRL0_VTH_CTRL_0		 (0L<<11)
#define BCE_MISC_LCPLL_CTRL0_VTH_CTRL_1		 (1L<<11)
#define BCE_MISC_LCPLL_CTRL0_VTH_CTRL_2		 (2L<<11)
#define BCE_MISC_LCPLL_CTRL0_PLLSEQSTART		 (1L<<13)
#define BCE_MISC_LCPLL_CTRL0_RESERVED			 (1L<<14)
#define BCE_MISC_LCPLL_CTRL0_CAPRETRY_EN		 (1L<<15)
#define BCE_MISC_LCPLL_CTRL0_FREQMONITOR_EN		 (1L<<16)
#define BCE_MISC_LCPLL_CTRL0_FREQDETRESTART_EN		 (1L<<17)
#define BCE_MISC_LCPLL_CTRL0_FREQDETRETRY_EN		 (1L<<18)
#define BCE_MISC_LCPLL_CTRL0_PLLFORCEFDONE_EN		 (1L<<19)
#define BCE_MISC_LCPLL_CTRL0_PLLFORCEFDONE		 (1L<<20)
#define BCE_MISC_LCPLL_CTRL0_PLLFORCEFPASS		 (1L<<21)
#define BCE_MISC_LCPLL_CTRL0_PLLFORCECAPDONE_EN	 (1L<<22)
#define BCE_MISC_LCPLL_CTRL0_PLLFORCECAPDONE		 (1L<<23)
#define BCE_MISC_LCPLL_CTRL0_PLLFORCECAPPASS_EN	 (1L<<24)
#define BCE_MISC_LCPLL_CTRL0_PLLFORCECAPPASS		 (1L<<25)
#define BCE_MISC_LCPLL_CTRL0_CAPRESTART		 (1L<<26)
#define BCE_MISC_LCPLL_CTRL0_CAPSELECTM_EN		 (1L<<27)

#define BCE_MISC_LCPLL_CTRL1				0x00000954
#define BCE_MISC_LCPLL_CTRL1_CAPSELECTM		 (0x1fL<<0)
#define BCE_MISC_LCPLL_CTRL1_CAPFORCESLOWDOWN_EN	 (1L<<5)
#define BCE_MISC_LCPLL_CTRL1_CAPFORCESLOWDOWN		 (1L<<6)
#define BCE_MISC_LCPLL_CTRL1_SLOWDN_XOR		 (1L<<7)

#define BCE_MISC_LCPLL_STATUS				0x00000958
#define BCE_MISC_LCPLL_STATUS_FREQDONE_SM		 (1L<<0)
#define BCE_MISC_LCPLL_STATUS_FREQPASS_SM		 (1L<<1)
#define BCE_MISC_LCPLL_STATUS_PLLSEQDONE		 (1L<<2)
#define BCE_MISC_LCPLL_STATUS_PLLSEQPASS		 (1L<<3)
#define BCE_MISC_LCPLL_STATUS_PLLSTATE			 (0x7L<<4)
#define BCE_MISC_LCPLL_STATUS_CAPSTATE			 (0x7L<<7)
#define BCE_MISC_LCPLL_STATUS_CAPSELECT		 (0x1fL<<10)
#define BCE_MISC_LCPLL_STATUS_SLOWDN_INDICATOR		 (1L<<15)
#define BCE_MISC_LCPLL_STATUS_SLOWDN_INDICATOR_0	 (0L<<15)
#define BCE_MISC_LCPLL_STATUS_SLOWDN_INDICATOR_1	 (1L<<15)

#define BCE_MISC_OSCFUNDS_CTRL				0x0000095c
#define BCE_MISC_OSCFUNDS_CTRL_FREQ_MON		 (1L<<5)
#define BCE_MISC_OSCFUNDS_CTRL_FREQ_MON_OFF		 (0L<<5)
#define BCE_MISC_OSCFUNDS_CTRL_FREQ_MON_ON		 (1L<<5)
#define BCE_MISC_OSCFUNDS_CTRL_XTAL_ADJCM		 (0x3L<<6)
#define BCE_MISC_OSCFUNDS_CTRL_XTAL_ADJCM_0		 (0L<<6)
#define BCE_MISC_OSCFUNDS_CTRL_XTAL_ADJCM_1		 (1L<<6)
#define BCE_MISC_OSCFUNDS_CTRL_XTAL_ADJCM_2		 (2L<<6)
#define BCE_MISC_OSCFUNDS_CTRL_XTAL_ADJCM_3		 (3L<<6)
#define BCE_MISC_OSCFUNDS_CTRL_ICBUF_ADJ		 (0x3L<<8)
#define BCE_MISC_OSCFUNDS_CTRL_ICBUF_ADJ_0		 (0L<<8)
#define BCE_MISC_OSCFUNDS_CTRL_ICBUF_ADJ_1		 (1L<<8)
#define BCE_MISC_OSCFUNDS_CTRL_ICBUF_ADJ_2		 (2L<<8)
#define BCE_MISC_OSCFUNDS_CTRL_ICBUF_ADJ_3		 (3L<<8)
#define BCE_MISC_OSCFUNDS_CTRL_IAMP_ADJ		 (0x3L<<10)
#define BCE_MISC_OSCFUNDS_CTRL_IAMP_ADJ_0		 (0L<<10)
#define BCE_MISC_OSCFUNDS_CTRL_IAMP_ADJ_1		 (1L<<10)
#define BCE_MISC_OSCFUNDS_CTRL_IAMP_ADJ_2		 (2L<<10)
#define BCE_MISC_OSCFUNDS_CTRL_IAMP_ADJ_3		 (3L<<10)


/*
 *  dma_reg definition
 *  offset: 0xc00
 */
#define BCE_DMA_COMMAND				0x00000c00
#define BCE_DMA_COMMAND_ENABLE				 (1L<<0)

#define BCE_DMA_STATUS					0x00000c04
#define BCE_DMA_STATUS_PAR_ERROR_STATE			 (1L<<0)
#define BCE_DMA_STATUS_READ_TRANSFERS_STAT		 (1L<<16)
#define BCE_DMA_STATUS_READ_DELAY_PCI_CLKS_STAT	 (1L<<17)
#define BCE_DMA_STATUS_BIG_READ_TRANSFERS_STAT		 (1L<<18)
#define BCE_DMA_STATUS_BIG_READ_DELAY_PCI_CLKS_STAT	 (1L<<19)
#define BCE_DMA_STATUS_BIG_READ_RETRY_AFTER_DATA_STAT	 (1L<<20)
#define BCE_DMA_STATUS_WRITE_TRANSFERS_STAT		 (1L<<21)
#define BCE_DMA_STATUS_WRITE_DELAY_PCI_CLKS_STAT	 (1L<<22)
#define BCE_DMA_STATUS_BIG_WRITE_TRANSFERS_STAT	 (1L<<23)
#define BCE_DMA_STATUS_BIG_WRITE_DELAY_PCI_CLKS_STAT	 (1L<<24)
#define BCE_DMA_STATUS_BIG_WRITE_RETRY_AFTER_DATA_STAT	 (1L<<25)

#define BCE_DMA_CONFIG					0x00000c08
#define BCE_DMA_CONFIG_DATA_BYTE_SWAP			 (1L<<0)
#define BCE_DMA_CONFIG_DATA_WORD_SWAP			 (1L<<1)
#define BCE_DMA_CONFIG_CNTL_BYTE_SWAP			 (1L<<4)
#define BCE_DMA_CONFIG_CNTL_WORD_SWAP			 (1L<<5)
#define BCE_DMA_CONFIG_ONE_DMA				 (1L<<6)
#define BCE_DMA_CONFIG_CNTL_TWO_DMA			 (1L<<7)
#define BCE_DMA_CONFIG_CNTL_FPGA_MODE			 (1L<<8)
#define BCE_DMA_CONFIG_CNTL_PING_PONG_DMA		 (1L<<10)
#define BCE_DMA_CONFIG_CNTL_PCI_COMP_DLY		 (1L<<11)
#define BCE_DMA_CONFIG_NO_RCHANS_IN_USE		 (0xfL<<12)
#define BCE_DMA_CONFIG_NO_WCHANS_IN_USE		 (0xfL<<16)
#define BCE_DMA_CONFIG_PCI_CLK_CMP_BITS		 (0x7L<<20)
#define BCE_DMA_CONFIG_PCI_FAST_CLK_CMP		 (1L<<23)
#define BCE_DMA_CONFIG_BIG_SIZE			 (0xfL<<24)
#define BCE_DMA_CONFIG_BIG_SIZE_NONE			 (0x0L<<24)
#define BCE_DMA_CONFIG_BIG_SIZE_64			 (0x1L<<24)
#define BCE_DMA_CONFIG_BIG_SIZE_128			 (0x2L<<24)
#define BCE_DMA_CONFIG_BIG_SIZE_256			 (0x4L<<24)
#define BCE_DMA_CONFIG_BIG_SIZE_512			 (0x8L<<24)

#define BCE_DMA_BLACKOUT				0x00000c0c
#define BCE_DMA_BLACKOUT_RD_RETRY_BLACKOUT		 (0xffL<<0)
#define BCE_DMA_BLACKOUT_2ND_RD_RETRY_BLACKOUT		 (0xffL<<8)
#define BCE_DMA_BLACKOUT_WR_RETRY_BLACKOUT		 (0xffL<<16)

#define BCE_DMA_RCHAN_STAT				0x00000c30
#define BCE_DMA_RCHAN_STAT_COMP_CODE_0			 (0x7L<<0)
#define BCE_DMA_RCHAN_STAT_PAR_ERR_0			 (1L<<3)
#define BCE_DMA_RCHAN_STAT_COMP_CODE_1			 (0x7L<<4)
#define BCE_DMA_RCHAN_STAT_PAR_ERR_1			 (1L<<7)
#define BCE_DMA_RCHAN_STAT_COMP_CODE_2			 (0x7L<<8)
#define BCE_DMA_RCHAN_STAT_PAR_ERR_2			 (1L<<11)
#define BCE_DMA_RCHAN_STAT_COMP_CODE_3			 (0x7L<<12)
#define BCE_DMA_RCHAN_STAT_PAR_ERR_3			 (1L<<15)
#define BCE_DMA_RCHAN_STAT_COMP_CODE_4			 (0x7L<<16)
#define BCE_DMA_RCHAN_STAT_PAR_ERR_4			 (1L<<19)
#define BCE_DMA_RCHAN_STAT_COMP_CODE_5			 (0x7L<<20)
#define BCE_DMA_RCHAN_STAT_PAR_ERR_5			 (1L<<23)
#define BCE_DMA_RCHAN_STAT_COMP_CODE_6			 (0x7L<<24)
#define BCE_DMA_RCHAN_STAT_PAR_ERR_6			 (1L<<27)
#define BCE_DMA_RCHAN_STAT_COMP_CODE_7			 (0x7L<<28)
#define BCE_DMA_RCHAN_STAT_PAR_ERR_7			 (1L<<31)

#define BCE_DMA_WCHAN_STAT				0x00000c34
#define BCE_DMA_WCHAN_STAT_COMP_CODE_0			 (0x7L<<0)
#define BCE_DMA_WCHAN_STAT_PAR_ERR_0			 (1L<<3)
#define BCE_DMA_WCHAN_STAT_COMP_CODE_1			 (0x7L<<4)
#define BCE_DMA_WCHAN_STAT_PAR_ERR_1			 (1L<<7)
#define BCE_DMA_WCHAN_STAT_COMP_CODE_2			 (0x7L<<8)
#define BCE_DMA_WCHAN_STAT_PAR_ERR_2			 (1L<<11)
#define BCE_DMA_WCHAN_STAT_COMP_CODE_3			 (0x7L<<12)
#define BCE_DMA_WCHAN_STAT_PAR_ERR_3			 (1L<<15)
#define BCE_DMA_WCHAN_STAT_COMP_CODE_4			 (0x7L<<16)
#define BCE_DMA_WCHAN_STAT_PAR_ERR_4			 (1L<<19)
#define BCE_DMA_WCHAN_STAT_COMP_CODE_5			 (0x7L<<20)
#define BCE_DMA_WCHAN_STAT_PAR_ERR_5			 (1L<<23)
#define BCE_DMA_WCHAN_STAT_COMP_CODE_6			 (0x7L<<24)
#define BCE_DMA_WCHAN_STAT_PAR_ERR_6			 (1L<<27)
#define BCE_DMA_WCHAN_STAT_COMP_CODE_7			 (0x7L<<28)
#define BCE_DMA_WCHAN_STAT_PAR_ERR_7			 (1L<<31)

#define BCE_DMA_RCHAN_ASSIGNMENT			0x00000c38
#define BCE_DMA_RCHAN_ASSIGNMENT_0			 (0xfL<<0)
#define BCE_DMA_RCHAN_ASSIGNMENT_1			 (0xfL<<4)
#define BCE_DMA_RCHAN_ASSIGNMENT_2			 (0xfL<<8)
#define BCE_DMA_RCHAN_ASSIGNMENT_3			 (0xfL<<12)
#define BCE_DMA_RCHAN_ASSIGNMENT_4			 (0xfL<<16)
#define BCE_DMA_RCHAN_ASSIGNMENT_5			 (0xfL<<20)
#define BCE_DMA_RCHAN_ASSIGNMENT_6			 (0xfL<<24)
#define BCE_DMA_RCHAN_ASSIGNMENT_7			 (0xfL<<28)

#define BCE_DMA_WCHAN_ASSIGNMENT			0x00000c3c
#define BCE_DMA_WCHAN_ASSIGNMENT_0			 (0xfL<<0)
#define BCE_DMA_WCHAN_ASSIGNMENT_1			 (0xfL<<4)
#define BCE_DMA_WCHAN_ASSIGNMENT_2			 (0xfL<<8)
#define BCE_DMA_WCHAN_ASSIGNMENT_3			 (0xfL<<12)
#define BCE_DMA_WCHAN_ASSIGNMENT_4			 (0xfL<<16)
#define BCE_DMA_WCHAN_ASSIGNMENT_5			 (0xfL<<20)
#define BCE_DMA_WCHAN_ASSIGNMENT_6			 (0xfL<<24)
#define BCE_DMA_WCHAN_ASSIGNMENT_7			 (0xfL<<28)

#define BCE_DMA_RCHAN_STAT_00				0x00000c40
#define BCE_DMA_RCHAN_STAT_00_RCHAN_STA_HOST_ADDR_LOW	 (0xffffffffL<<0)

#define BCE_DMA_RCHAN_STAT_01				0x00000c44
#define BCE_DMA_RCHAN_STAT_01_RCHAN_STA_HOST_ADDR_HIGH	 (0xffffffffL<<0)

#define BCE_DMA_RCHAN_STAT_02				0x00000c48
#define BCE_DMA_RCHAN_STAT_02_LENGTH			 (0xffffL<<0)
#define BCE_DMA_RCHAN_STAT_02_WORD_SWAP		 (1L<<16)
#define BCE_DMA_RCHAN_STAT_02_BYTE_SWAP		 (1L<<17)
#define BCE_DMA_RCHAN_STAT_02_PRIORITY_LVL		 (1L<<18)

#define BCE_DMA_RCHAN_STAT_10				0x00000c4c
#define BCE_DMA_RCHAN_STAT_11				0x00000c50
#define BCE_DMA_RCHAN_STAT_12				0x00000c54
#define BCE_DMA_RCHAN_STAT_20				0x00000c58
#define BCE_DMA_RCHAN_STAT_21				0x00000c5c
#define BCE_DMA_RCHAN_STAT_22				0x00000c60
#define BCE_DMA_RCHAN_STAT_30				0x00000c64
#define BCE_DMA_RCHAN_STAT_31				0x00000c68
#define BCE_DMA_RCHAN_STAT_32				0x00000c6c
#define BCE_DMA_RCHAN_STAT_40				0x00000c70
#define BCE_DMA_RCHAN_STAT_41				0x00000c74
#define BCE_DMA_RCHAN_STAT_42				0x00000c78
#define BCE_DMA_RCHAN_STAT_50				0x00000c7c
#define BCE_DMA_RCHAN_STAT_51				0x00000c80
#define BCE_DMA_RCHAN_STAT_52				0x00000c84
#define BCE_DMA_RCHAN_STAT_60				0x00000c88
#define BCE_DMA_RCHAN_STAT_61				0x00000c8c
#define BCE_DMA_RCHAN_STAT_62				0x00000c90
#define BCE_DMA_RCHAN_STAT_70				0x00000c94
#define BCE_DMA_RCHAN_STAT_71				0x00000c98
#define BCE_DMA_RCHAN_STAT_72				0x00000c9c
#define BCE_DMA_WCHAN_STAT_00				0x00000ca0
#define BCE_DMA_WCHAN_STAT_00_WCHAN_STA_HOST_ADDR_LOW	 (0xffffffffL<<0)

#define BCE_DMA_WCHAN_STAT_01				0x00000ca4
#define BCE_DMA_WCHAN_STAT_01_WCHAN_STA_HOST_ADDR_HIGH	 (0xffffffffL<<0)

#define BCE_DMA_WCHAN_STAT_02				0x00000ca8
#define BCE_DMA_WCHAN_STAT_02_LENGTH			 (0xffffL<<0)
#define BCE_DMA_WCHAN_STAT_02_WORD_SWAP		 (1L<<16)
#define BCE_DMA_WCHAN_STAT_02_BYTE_SWAP		 (1L<<17)
#define BCE_DMA_WCHAN_STAT_02_PRIORITY_LVL		 (1L<<18)

#define BCE_DMA_WCHAN_STAT_10				0x00000cac
#define BCE_DMA_WCHAN_STAT_11				0x00000cb0
#define BCE_DMA_WCHAN_STAT_12				0x00000cb4
#define BCE_DMA_WCHAN_STAT_20				0x00000cb8
#define BCE_DMA_WCHAN_STAT_21				0x00000cbc
#define BCE_DMA_WCHAN_STAT_22				0x00000cc0
#define BCE_DMA_WCHAN_STAT_30				0x00000cc4
#define BCE_DMA_WCHAN_STAT_31				0x00000cc8
#define BCE_DMA_WCHAN_STAT_32				0x00000ccc
#define BCE_DMA_WCHAN_STAT_40				0x00000cd0
#define BCE_DMA_WCHAN_STAT_41				0x00000cd4
#define BCE_DMA_WCHAN_STAT_42				0x00000cd8
#define BCE_DMA_WCHAN_STAT_50				0x00000cdc
#define BCE_DMA_WCHAN_STAT_51				0x00000ce0
#define BCE_DMA_WCHAN_STAT_52				0x00000ce4
#define BCE_DMA_WCHAN_STAT_60				0x00000ce8
#define BCE_DMA_WCHAN_STAT_61				0x00000cec
#define BCE_DMA_WCHAN_STAT_62				0x00000cf0
#define BCE_DMA_WCHAN_STAT_70				0x00000cf4
#define BCE_DMA_WCHAN_STAT_71				0x00000cf8
#define BCE_DMA_WCHAN_STAT_72				0x00000cfc
#define BCE_DMA_ARB_STAT_00				0x00000d00
#define BCE_DMA_ARB_STAT_00_MASTER			 (0xffffL<<0)
#define BCE_DMA_ARB_STAT_00_MASTER_ENC			 (0xffL<<16)
#define BCE_DMA_ARB_STAT_00_CUR_BINMSTR		 (0xffL<<24)

#define BCE_DMA_ARB_STAT_01				0x00000d04
#define BCE_DMA_ARB_STAT_01_LPR_RPTR			 (0xfL<<0)
#define BCE_DMA_ARB_STAT_01_LPR_WPTR			 (0xfL<<4)
#define BCE_DMA_ARB_STAT_01_LPB_RPTR			 (0xfL<<8)
#define BCE_DMA_ARB_STAT_01_LPB_WPTR			 (0xfL<<12)
#define BCE_DMA_ARB_STAT_01_HPR_RPTR			 (0xfL<<16)
#define BCE_DMA_ARB_STAT_01_HPR_WPTR			 (0xfL<<20)
#define BCE_DMA_ARB_STAT_01_HPB_RPTR			 (0xfL<<24)
#define BCE_DMA_ARB_STAT_01_HPB_WPTR			 (0xfL<<28)

#define BCE_DMA_FUSE_CTRL0_CMD				0x00000f00
#define BCE_DMA_FUSE_CTRL0_CMD_PWRUP_DONE		 (1L<<0)
#define BCE_DMA_FUSE_CTRL0_CMD_SHIFT_DONE		 (1L<<1)
#define BCE_DMA_FUSE_CTRL0_CMD_SHIFT			 (1L<<2)
#define BCE_DMA_FUSE_CTRL0_CMD_LOAD			 (1L<<3)
#define BCE_DMA_FUSE_CTRL0_CMD_SEL			 (0xfL<<8)

#define BCE_DMA_FUSE_CTRL0_DATA			0x00000f04
#define BCE_DMA_FUSE_CTRL1_CMD				0x00000f08
#define BCE_DMA_FUSE_CTRL1_CMD_PWRUP_DONE		 (1L<<0)
#define BCE_DMA_FUSE_CTRL1_CMD_SHIFT_DONE		 (1L<<1)
#define BCE_DMA_FUSE_CTRL1_CMD_SHIFT			 (1L<<2)
#define BCE_DMA_FUSE_CTRL1_CMD_LOAD			 (1L<<3)
#define BCE_DMA_FUSE_CTRL1_CMD_SEL			 (0xfL<<8)

#define BCE_DMA_FUSE_CTRL1_DATA			0x00000f0c
#define BCE_DMA_FUSE_CTRL2_CMD				0x00000f10
#define BCE_DMA_FUSE_CTRL2_CMD_PWRUP_DONE		 (1L<<0)
#define BCE_DMA_FUSE_CTRL2_CMD_SHIFT_DONE		 (1L<<1)
#define BCE_DMA_FUSE_CTRL2_CMD_SHIFT			 (1L<<2)
#define BCE_DMA_FUSE_CTRL2_CMD_LOAD			 (1L<<3)
#define BCE_DMA_FUSE_CTRL2_CMD_SEL			 (0xfL<<8)

#define BCE_DMA_FUSE_CTRL2_DATA			0x00000f14


/*
 *  context_reg definition
 *  offset: 0x1000
 */
#define BCE_CTX_COMMAND									0x00001000
#define BCE_CTX_COMMAND_ENABLED							(1L<<0)
#define BCE_CTX_COMMAND_DISABLE_USAGE_CNT				(1L<<1)
#define BCE_CTX_COMMAND_DISABLE_PLRU					(1L<<2)
#define BCE_CTX_COMMAND_DISABLE_COMBINE_READ			(1L<<3)
#define BCE_CTX_COMMAND_FLUSH_AHEAD						(0x1fL<<8)
#define BCE_CTX_COMMAND_MEM_INIT						(1L<<13)
#define BCE_CTX_COMMAND_PAGE_SIZE						(0xfL<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_256					(0L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_512					(1L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_1K					(2L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_2K					(3L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_4K					(4L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_8K					(5L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_16K					(6L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_32K					(7L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_64K					(8L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_128K					(9L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_256K					(10L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_512K					(11L<<16)
#define BCE_CTX_COMMAND_PAGE_SIZE_1M					(12L<<16)

#define BCE_CTX_STATUS									0x00001004
#define BCE_CTX_STATUS_LOCK_WAIT						(1L<<0)
#define BCE_CTX_STATUS_READ_STAT						(1L<<16)
#define BCE_CTX_STATUS_WRITE_STAT						(1L<<17)
#define BCE_CTX_STATUS_ACC_STALL_STAT					(1L<<18)
#define BCE_CTX_STATUS_LOCK_STALL_STAT					(1L<<19)
#define BCE_CTX_STATUS_EXT_READ_STAT					(1L<<20)
#define BCE_CTX_STATUS_EXT_WRITE_STAT					(1L<<21)
#define BCE_CTX_STATUS_MISS_STAT						(1L<<22)
#define BCE_CTX_STATUS_HIT_STAT							(1L<<23)
#define BCE_CTX_STATUS_DEAD_LOCK						(1L<<24)
#define BCE_CTX_STATUS_USAGE_CNT_ERR					(1L<<25)
#define BCE_CTX_STATUS_INVALID_PAGE						(1L<<26)

#define BCE_CTX_VIRT_ADDR								0x00001008
#define BCE_CTX_VIRT_ADDR_VIRT_ADDR						(0x7fffL<<6)

#define BCE_CTX_PAGE_TBL								0x0000100c
#define BCE_CTX_PAGE_TBL_PAGE_TBL						(0x3fffL<<6)

#define BCE_CTX_DATA_ADR								0x00001010
#define BCE_CTX_DATA_ADR_DATA_ADR						(0x7ffffL<<2)

#define BCE_CTX_DATA									0x00001014
#define BCE_CTX_LOCK									0x00001018
#define BCE_CTX_LOCK_TYPE								(0x7L<<0)
#define BCE_CTX_LOCK_TYPE_LOCK_TYPE_VOID				(0x0L<<0)
#define BCE_CTX_LOCK_TYPE_LOCK_TYPE_PROTOCOL			(0x1L<<0)
#define BCE_CTX_LOCK_TYPE_LOCK_TYPE_TX					(0x2L<<0)
#define BCE_CTX_LOCK_TYPE_LOCK_TYPE_TIMER				(0x4L<<0)
#define BCE_CTX_LOCK_TYPE_LOCK_TYPE_COMPLETE			(0x7L<<0)
#define BCE_CTX_LOCK_TYPE_VOID_XI						(0L<<0)
#define BCE_CTX_LOCK_TYPE_PROTOCOL_XI					(1L<<0)
#define BCE_CTX_LOCK_TYPE_TX_XI							(2L<<0)
#define BCE_CTX_LOCK_TYPE_TIMER_XI						(4L<<0)
#define BCE_CTX_LOCK_TYPE_COMPLETE_XI					(7L<<0)
#define BCE_CTX_LOCK_CID_VALUE							(0x3fffL<<7)
#define BCE_CTX_LOCK_GRANTED							(1L<<26)
#define BCE_CTX_LOCK_MODE								(0x7L<<27)
#define BCE_CTX_LOCK_MODE_UNLOCK						(0x0L<<27)
#define BCE_CTX_LOCK_MODE_IMMEDIATE						(0x1L<<27)
#define BCE_CTX_LOCK_MODE_SURE							(0x2L<<27)
#define BCE_CTX_LOCK_STATUS								(1L<<30)
#define BCE_CTX_LOCK_REQ								(1L<<31)

#define BCE_CTX_CTX_CTRL								0x0000101c
#define BCE_CTX_CTX_CTRL_CTX_ADDR						(0x7ffffL<<2)
#define BCE_CTX_CTX_CTRL_MOD_USAGE_CNT					(0x3L<<21)
#define BCE_CTX_CTX_CTRL_NO_RAM_ACC						(1L<<23)
#define BCE_CTX_CTX_CTRL_PREFETCH_SIZE					(0x3L<<24)
#define BCE_CTX_CTX_CTRL_ATTR							(1L<<26)
#define BCE_CTX_CTX_CTRL_WRITE_REQ						(1L<<30)
#define BCE_CTX_CTX_CTRL_READ_REQ						(1L<<31)

#define BCE_CTX_CTX_DATA								0x00001020
#define BCE_CTX_ACCESS_STATUS							0x00001040
#define BCE_CTX_ACCESS_STATUS_MASTERENCODED				(0xfL<<0)
#define BCE_CTX_ACCESS_STATUS_ACCESSMEMORYSM			(0x3L<<10)
#define BCE_CTX_ACCESS_STATUS_PAGETABLEINITSM			(0x3L<<12)
#define BCE_CTX_ACCESS_STATUS_ACCESSMEMORYINITSM		(0x3L<<14)
#define BCE_CTX_ACCESS_STATUS_QUALIFIED_REQUEST			(0x7ffL<<17)
#define BCE_CTX_ACCESS_STATUS_CAMMASTERENCODED_XI		(0x1fL<<0)
#define BCE_CTX_ACCESS_STATUS_CACHEMASTERENCODED_XI		(0x1fL<<5)
#define BCE_CTX_ACCESS_STATUS_REQUEST_XI				(0x3fffffL<<10)

#define BCE_CTX_DBG_LOCK_STATUS							0x00001044
#define BCE_CTX_DBG_LOCK_STATUS_SM						(0x3ffL<<0)
#define BCE_CTX_DBG_LOCK_STATUS_MATCH					(0x3ffL<<22)

#define BCE_CTX_CACHE_CTRL_STATUS						0x00001048
#define BCE_CTX_CACHE_CTRL_STATUS_RFIFO_OVERFLOW		(1L<<0)
#define BCE_CTX_CACHE_CTRL_STATUS_INVALID_READ_COMP		(1L<<1)
#define BCE_CTX_CACHE_CTRL_STATUS_FLUSH_START			(1L<<6)
#define BCE_CTX_CACHE_CTRL_STATUS_FREE_ENTRY_CNT		(0x3fL<<7)
#define BCE_CTX_CACHE_CTRL_STATUS_CACHE_ENTRY_NEEDED	(0x3fL<<13)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN0_ACTIVE		(1L<<19)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN1_ACTIVE		(1L<<20)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN2_ACTIVE		(1L<<21)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN3_ACTIVE		(1L<<22)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN4_ACTIVE		(1L<<23)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN5_ACTIVE		(1L<<24)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN6_ACTIVE		(1L<<25)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN7_ACTIVE		(1L<<26)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN8_ACTIVE		(1L<<27)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN9_ACTIVE		(1L<<28)
#define BCE_CTX_CACHE_CTRL_STATUS_RD_CHAN10_ACTIVE		(1L<<29)

#define BCE_CTX_CACHE_CTRL_SM_STATUS					0x0000104c
#define BCE_CTX_CACHE_CTRL_SM_STATUS_CS_DWC				(0x7L<<0)
#define BCE_CTX_CACHE_CTRL_SM_STATUS_CS_WFIFOC			(0x7L<<3)
#define BCE_CTX_CACHE_CTRL_SM_STATUS_CS_RTAGC			(0x7L<<6)
#define BCE_CTX_CACHE_CTRL_SM_STATUS_CS_RFIFOC			(0x7L<<9)
#define BCE_CTX_CACHE_CTRL_SM_STATUS_INVALID_BLK_ADDR	(0x7fffL<<16)

#define BCE_CTX_CACHE_STATUS							0x00001050
#define BCE_CTX_CACHE_STATUS_HELD_ENTRIES				(0x3ffL<<0)
#define BCE_CTX_CACHE_STATUS_MAX_HELD_ENTRIES			(0x3ffL<<16)

#define BCE_CTX_DMA_STATUS								0x00001054
#define BCE_CTX_DMA_STATUS_RD_CHAN0_STATUS				(0x3L<<0)
#define BCE_CTX_DMA_STATUS_RD_CHAN1_STATUS				(0x3L<<2)
#define BCE_CTX_DMA_STATUS_RD_CHAN2_STATUS				(0x3L<<4)
#define BCE_CTX_DMA_STATUS_RD_CHAN3_STATUS				(0x3L<<6)
#define BCE_CTX_DMA_STATUS_RD_CHAN4_STATUS				(0x3L<<8)
#define BCE_CTX_DMA_STATUS_RD_CHAN5_STATUS				(0x3L<<10)
#define BCE_CTX_DMA_STATUS_RD_CHAN6_STATUS				(0x3L<<12)
#define BCE_CTX_DMA_STATUS_RD_CHAN7_STATUS				(0x3L<<14)
#define BCE_CTX_DMA_STATUS_RD_CHAN8_STATUS				(0x3L<<16)
#define BCE_CTX_DMA_STATUS_RD_CHAN9_STATUS				(0x3L<<18)
#define BCE_CTX_DMA_STATUS_RD_CHAN10_STATUS				(0x3L<<20)

#define BCE_CTX_REP_STATUS								0x00001058
#define BCE_CTX_REP_STATUS_ERROR_ENTRY					(0x3ffL<<0)
#define BCE_CTX_REP_STATUS_ERROR_CLIENT_ID				(0x1fL<<10)
#define BCE_CTX_REP_STATUS_USAGE_CNT_MAX_ERR			(1L<<16)
#define BCE_CTX_REP_STATUS_USAGE_CNT_MIN_ERR			(1L<<17)
#define BCE_CTX_REP_STATUS_USAGE_CNT_MISS_ERR			(1L<<18)

#define BCE_CTX_CKSUM_ERROR_STATUS						0x0000105c
#define BCE_CTX_CKSUM_ERROR_STATUS_CALCULATED			(0xffffL<<0)
#define BCE_CTX_CKSUM_ERROR_STATUS_EXPECTED				(0xffffL<<16)

#define BCE_CTX_CHNL_LOCK_STATUS_0						0x00001080
#define BCE_CTX_CHNL_LOCK_STATUS_0_CID					(0x3fffL<<0)
#define BCE_CTX_CHNL_LOCK_STATUS_0_TYPE					(0x3L<<14)
#define BCE_CTX_CHNL_LOCK_STATUS_0_MODE					(1L<<16)
#define BCE_CTX_CHNL_LOCK_STATUS_0_MODE_XI				(1L<<14)
#define BCE_CTX_CHNL_LOCK_STATUS_0_TYPE_XI				(0x7L<<15)

#define BCE_CTX_CHNL_LOCK_STATUS_1						0x00001084
#define BCE_CTX_CHNL_LOCK_STATUS_2						0x00001088
#define BCE_CTX_CHNL_LOCK_STATUS_3						0x0000108c
#define BCE_CTX_CHNL_LOCK_STATUS_4						0x00001090
#define BCE_CTX_CHNL_LOCK_STATUS_5						0x00001094
#define BCE_CTX_CHNL_LOCK_STATUS_6						0x00001098
#define BCE_CTX_CHNL_LOCK_STATUS_7						0x0000109c
#define BCE_CTX_CHNL_LOCK_STATUS_8						0x000010a0
#define BCE_CTX_CHNL_LOCK_STATUS_9						0x000010a4

#define BCE_CTX_CACHE_DATA								0x000010c4
#define BCE_CTX_HOST_PAGE_TBL_CTRL						0x000010c8
#define BCE_CTX_HOST_PAGE_TBL_CTRL_PAGE_TBL_ADDR		(0x1ffL<<0)
#define BCE_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ			(1L<<30)
#define BCE_CTX_HOST_PAGE_TBL_CTRL_READ_REQ				(1L<<31)

#define BCE_CTX_HOST_PAGE_TBL_DATA0						0x000010cc
#define BCE_CTX_HOST_PAGE_TBL_DATA0_VALID				(1L<<0)
#define BCE_CTX_HOST_PAGE_TBL_DATA0_VALUE				(0xffffffL<<8)

#define BCE_CTX_HOST_PAGE_TBL_DATA1						0x000010d0
#define BCE_CTX_CAM_CTRL								0x000010d4
#define BCE_CTX_CAM_CTRL_CAM_ADDR						(0x3ffL<<0)
#define BCE_CTX_CAM_CTRL_RESET							(1L<<27)
#define BCE_CTX_CAM_CTRL_INVALIDATE						(1L<<28)
#define BCE_CTX_CAM_CTRL_SEARCH							(1L<<29)
#define BCE_CTX_CAM_CTRL_WRITE_REQ						(1L<<30)
#define BCE_CTX_CAM_CTRL_READ_REQ						(1L<<31)


/*
 *  emac_reg definition
 *  offset: 0x1400
 */
#define BCE_EMAC_MODE					0x00001400
#define BCE_EMAC_MODE_RESET				 (1L<<0)
#define BCE_EMAC_MODE_HALF_DUPLEX			 (1L<<1)
#define BCE_EMAC_MODE_PORT				 (0x3L<<2)
#define BCE_EMAC_MODE_PORT_NONE			 (0L<<2)
#define BCE_EMAC_MODE_PORT_MII				 (1L<<2)
#define BCE_EMAC_MODE_PORT_GMII			 (2L<<2)
#define BCE_EMAC_MODE_PORT_MII_10			 (3L<<2)
#define BCE_EMAC_MODE_MAC_LOOP				 (1L<<4)
#define BCE_EMAC_MODE_25G				 (1L<<5)
#define BCE_EMAC_MODE_TAGGED_MAC_CTL			 (1L<<7)
#define BCE_EMAC_MODE_TX_BURST				 (1L<<8)
#define BCE_EMAC_MODE_MAX_DEFER_DROP_ENA		 (1L<<9)
#define BCE_EMAC_MODE_EXT_LINK_POL			 (1L<<10)
#define BCE_EMAC_MODE_FORCE_LINK			 (1L<<11)
#define BCE_EMAC_MODE_MPKT				 (1L<<18)
#define BCE_EMAC_MODE_MPKT_RCVD			 (1L<<19)
#define BCE_EMAC_MODE_ACPI_RCVD			 (1L<<20)

#define BCE_EMAC_STATUS				0x00001404
#define BCE_EMAC_STATUS_LINK				 (1L<<11)
#define BCE_EMAC_STATUS_LINK_CHANGE			 (1L<<12)
#define BCE_EMAC_STATUS_MI_COMPLETE			 (1L<<22)
#define BCE_EMAC_STATUS_MI_INT				 (1L<<23)
#define BCE_EMAC_STATUS_AP_ERROR			 (1L<<24)
#define BCE_EMAC_STATUS_PARITY_ERROR_STATE		 (1L<<31)

#define BCE_EMAC_ATTENTION_ENA				0x00001408
#define BCE_EMAC_ATTENTION_ENA_LINK			 (1L<<11)
#define BCE_EMAC_ATTENTION_ENA_MI_COMPLETE		 (1L<<22)
#define BCE_EMAC_ATTENTION_ENA_MI_INT			 (1L<<23)
#define BCE_EMAC_ATTENTION_ENA_AP_ERROR		 (1L<<24)

#define BCE_EMAC_LED					0x0000140c
#define BCE_EMAC_LED_OVERRIDE				 (1L<<0)
#define BCE_EMAC_LED_1000MB_OVERRIDE			 (1L<<1)
#define BCE_EMAC_LED_100MB_OVERRIDE			 (1L<<2)
#define BCE_EMAC_LED_10MB_OVERRIDE			 (1L<<3)
#define BCE_EMAC_LED_TRAFFIC_OVERRIDE			 (1L<<4)
#define BCE_EMAC_LED_BLNK_TRAFFIC			 (1L<<5)
#define BCE_EMAC_LED_TRAFFIC				 (1L<<6)
#define BCE_EMAC_LED_1000MB				 (1L<<7)
#define BCE_EMAC_LED_100MB				 (1L<<8)
#define BCE_EMAC_LED_10MB				 (1L<<9)
#define BCE_EMAC_LED_TRAFFIC_STAT			 (1L<<10)
#define BCE_EMAC_LED_BLNK_RATE				 (0xfffL<<19)
#define BCE_EMAC_LED_BLNK_RATE_ENA			 (1L<<31)

#define BCE_EMAC_MAC_MATCH0				0x00001410
#define BCE_EMAC_MAC_MATCH1				0x00001414
#define BCE_EMAC_MAC_MATCH2				0x00001418
#define BCE_EMAC_MAC_MATCH3				0x0000141c
#define BCE_EMAC_MAC_MATCH4				0x00001420
#define BCE_EMAC_MAC_MATCH5				0x00001424
#define BCE_EMAC_MAC_MATCH6				0x00001428
#define BCE_EMAC_MAC_MATCH7				0x0000142c
#define BCE_EMAC_MAC_MATCH8				0x00001430
#define BCE_EMAC_MAC_MATCH9				0x00001434
#define BCE_EMAC_MAC_MATCH10				0x00001438
#define BCE_EMAC_MAC_MATCH11				0x0000143c
#define BCE_EMAC_MAC_MATCH12				0x00001440
#define BCE_EMAC_MAC_MATCH13				0x00001444
#define BCE_EMAC_MAC_MATCH14				0x00001448
#define BCE_EMAC_MAC_MATCH15				0x0000144c
#define BCE_EMAC_MAC_MATCH16				0x00001450
#define BCE_EMAC_MAC_MATCH17				0x00001454
#define BCE_EMAC_MAC_MATCH18				0x00001458
#define BCE_EMAC_MAC_MATCH19				0x0000145c
#define BCE_EMAC_MAC_MATCH20				0x00001460
#define BCE_EMAC_MAC_MATCH21				0x00001464
#define BCE_EMAC_MAC_MATCH22				0x00001468
#define BCE_EMAC_MAC_MATCH23				0x0000146c
#define BCE_EMAC_MAC_MATCH24				0x00001470
#define BCE_EMAC_MAC_MATCH25				0x00001474
#define BCE_EMAC_MAC_MATCH26				0x00001478
#define BCE_EMAC_MAC_MATCH27				0x0000147c
#define BCE_EMAC_MAC_MATCH28				0x00001480
#define BCE_EMAC_MAC_MATCH29				0x00001484
#define BCE_EMAC_MAC_MATCH30				0x00001488
#define BCE_EMAC_MAC_MATCH31				0x0000148c
#define BCE_EMAC_BACKOFF_SEED				0x00001498
#define BCE_EMAC_BACKOFF_SEED_EMAC_BACKOFF_SEED	 (0x3ffL<<0)

#define BCE_EMAC_RX_MTU_SIZE				0x0000149c
#define BCE_EMAC_RX_MTU_SIZE_MTU_SIZE			 (0xffffL<<0)
#define BCE_EMAC_RX_MTU_SIZE_JUMBO_ENA			 (1L<<31)

#define BCE_EMAC_SERDES_CNTL				0x000014a4
#define BCE_EMAC_SERDES_CNTL_RXR			 (0x7L<<0)
#define BCE_EMAC_SERDES_CNTL_RXG			 (0x3L<<3)
#define BCE_EMAC_SERDES_CNTL_RXCKSEL			 (1L<<6)
#define BCE_EMAC_SERDES_CNTL_TXBIAS			 (0x7L<<7)
#define BCE_EMAC_SERDES_CNTL_BGMAX			 (1L<<10)
#define BCE_EMAC_SERDES_CNTL_BGMIN			 (1L<<11)
#define BCE_EMAC_SERDES_CNTL_TXMODE			 (1L<<12)
#define BCE_EMAC_SERDES_CNTL_TXEDGE			 (1L<<13)
#define BCE_EMAC_SERDES_CNTL_SERDES_MODE		 (1L<<14)
#define BCE_EMAC_SERDES_CNTL_PLLTEST			 (1L<<15)
#define BCE_EMAC_SERDES_CNTL_CDET_EN			 (1L<<16)
#define BCE_EMAC_SERDES_CNTL_TBI_LBK			 (1L<<17)
#define BCE_EMAC_SERDES_CNTL_REMOTE_LBK		 (1L<<18)
#define BCE_EMAC_SERDES_CNTL_REV_PHASE			 (1L<<19)
#define BCE_EMAC_SERDES_CNTL_REGCTL12			 (0x3L<<20)
#define BCE_EMAC_SERDES_CNTL_REGCTL25			 (0x3L<<22)

#define BCE_EMAC_SERDES_STATUS				0x000014a8
#define BCE_EMAC_SERDES_STATUS_RX_STAT			 (0xffL<<0)
#define BCE_EMAC_SERDES_STATUS_COMMA_DET		 (1L<<8)

#define BCE_EMAC_MDIO_COMM				0x000014ac
#define BCE_EMAC_MDIO_COMM_DATA			 (0xffffL<<0)
#define BCE_EMAC_MDIO_COMM_REG_ADDR			 (0x1fL<<16)
#define BCE_EMAC_MDIO_COMM_PHY_ADDR			 (0x1fL<<21)
#define BCE_EMAC_MDIO_COMM_COMMAND			 (0x3L<<26)
#define BCE_EMAC_MDIO_COMM_COMMAND_UNDEFINED_0		 (0L<<26)
#define BCE_EMAC_MDIO_COMM_COMMAND_WRITE		 (1L<<26)
#define BCE_EMAC_MDIO_COMM_COMMAND_READ		 (2L<<26)
#define BCE_EMAC_MDIO_COMM_COMMAND_UNDEFINED_3		 (3L<<26)
#define BCE_EMAC_MDIO_COMM_FAIL			 (1L<<28)
#define BCE_EMAC_MDIO_COMM_START_BUSY			 (1L<<29)
#define BCE_EMAC_MDIO_COMM_DISEXT			 (1L<<30)

#define BCE_EMAC_MDIO_STATUS				0x000014b0
#define BCE_EMAC_MDIO_STATUS_LINK			 (1L<<0)
#define BCE_EMAC_MDIO_STATUS_10MB			 (1L<<1)

#define BCE_EMAC_MDIO_MODE				0x000014b4
#define BCE_EMAC_MDIO_MODE_SHORT_PREAMBLE		 (1L<<1)
#define BCE_EMAC_MDIO_MODE_AUTO_POLL			 (1L<<4)
#define BCE_EMAC_MDIO_MODE_BIT_BANG			 (1L<<8)
#define BCE_EMAC_MDIO_MODE_MDIO			 (1L<<9)
#define BCE_EMAC_MDIO_MODE_MDIO_OE			 (1L<<10)
#define BCE_EMAC_MDIO_MODE_MDC				 (1L<<11)
#define BCE_EMAC_MDIO_MODE_MDINT			 (1L<<12)
#define BCE_EMAC_MDIO_MODE_CLOCK_CNT			 (0x1fL<<16)

#define BCE_EMAC_MDIO_AUTO_STATUS			0x000014b8
#define BCE_EMAC_MDIO_AUTO_STATUS_AUTO_ERR		 (1L<<0)

#define BCE_EMAC_TX_MODE				0x000014bc
#define BCE_EMAC_TX_MODE_RESET				 (1L<<0)
#define BCE_EMAC_TX_MODE_EXT_PAUSE_EN			 (1L<<3)
#define BCE_EMAC_TX_MODE_FLOW_EN			 (1L<<4)
#define BCE_EMAC_TX_MODE_BIG_BACKOFF			 (1L<<5)
#define BCE_EMAC_TX_MODE_LONG_PAUSE			 (1L<<6)
#define BCE_EMAC_TX_MODE_LINK_AWARE			 (1L<<7)

#define BCE_EMAC_TX_STATUS				0x000014c0
#define BCE_EMAC_TX_STATUS_XOFFED			 (1L<<0)
#define BCE_EMAC_TX_STATUS_XOFF_SENT			 (1L<<1)
#define BCE_EMAC_TX_STATUS_XON_SENT			 (1L<<2)
#define BCE_EMAC_TX_STATUS_LINK_UP			 (1L<<3)
#define BCE_EMAC_TX_STATUS_UNDERRUN			 (1L<<4)

#define BCE_EMAC_TX_LENGTHS				0x000014c4
#define BCE_EMAC_TX_LENGTHS_SLOT			 (0xffL<<0)
#define BCE_EMAC_TX_LENGTHS_IPG			 (0xfL<<8)
#define BCE_EMAC_TX_LENGTHS_IPG_CRS			 (0x3L<<12)

#define BCE_EMAC_RX_MODE				0x000014c8
#define BCE_EMAC_RX_MODE_RESET				 (1L<<0)
#define BCE_EMAC_RX_MODE_FLOW_EN			 (1L<<2)
#define BCE_EMAC_RX_MODE_KEEP_MAC_CONTROL		 (1L<<3)
#define BCE_EMAC_RX_MODE_KEEP_PAUSE			 (1L<<4)
#define BCE_EMAC_RX_MODE_ACCEPT_OVERSIZE		 (1L<<5)
#define BCE_EMAC_RX_MODE_ACCEPT_RUNTS			 (1L<<6)
#define BCE_EMAC_RX_MODE_LLC_CHK			 (1L<<7)
#define BCE_EMAC_RX_MODE_PROMISCUOUS			 (1L<<8)
#define BCE_EMAC_RX_MODE_NO_CRC_CHK			 (1L<<9)
#define BCE_EMAC_RX_MODE_KEEP_VLAN_TAG			 (1L<<10)
#define BCE_EMAC_RX_MODE_FILT_BROADCAST		 (1L<<11)
#define BCE_EMAC_RX_MODE_SORT_MODE			 (1L<<12)

#define BCE_EMAC_RX_STATUS				0x000014cc
#define BCE_EMAC_RX_STATUS_FFED			 (1L<<0)
#define BCE_EMAC_RX_STATUS_FF_RECEIVED			 (1L<<1)
#define BCE_EMAC_RX_STATUS_N_RECEIVED			 (1L<<2)

#define BCE_EMAC_MULTICAST_HASH0			0x000014d0
#define BCE_EMAC_MULTICAST_HASH1			0x000014d4
#define BCE_EMAC_MULTICAST_HASH2			0x000014d8
#define BCE_EMAC_MULTICAST_HASH3			0x000014dc
#define BCE_EMAC_MULTICAST_HASH4			0x000014e0
#define BCE_EMAC_MULTICAST_HASH5			0x000014e4
#define BCE_EMAC_MULTICAST_HASH6			0x000014e8
#define BCE_EMAC_MULTICAST_HASH7			0x000014ec
#define BCE_EMAC_RX_STAT_IFHCINOCTETS			0x00001500
#define BCE_EMAC_RX_STAT_IFHCINBADOCTETS		0x00001504
#define BCE_EMAC_RX_STAT_ETHERSTATSFRAGMENTS		0x00001508
#define BCE_EMAC_RX_STAT_IFHCINUCASTPKTS		0x0000150c
#define BCE_EMAC_RX_STAT_IFHCINMULTICASTPKTS		0x00001510
#define BCE_EMAC_RX_STAT_IFHCINBROADCASTPKTS		0x00001514
#define BCE_EMAC_RX_STAT_DOT3STATSFCSERRORS		0x00001518
#define BCE_EMAC_RX_STAT_DOT3STATSALIGNMENTERRORS	0x0000151c
#define BCE_EMAC_RX_STAT_DOT3STATSCARRIERSENSEERRORS	0x00001520
#define BCE_EMAC_RX_STAT_XONPAUSEFRAMESRECEIVED	0x00001524
#define BCE_EMAC_RX_STAT_XOFFPAUSEFRAMESRECEIVED	0x00001528
#define BCE_EMAC_RX_STAT_MACCONTROLFRAMESRECEIVED	0x0000152c
#define BCE_EMAC_RX_STAT_XOFFSTATEENTERED		0x00001530
#define BCE_EMAC_RX_STAT_DOT3STATSFRAMESTOOLONG	0x00001534
#define BCE_EMAC_RX_STAT_ETHERSTATSJABBERS		0x00001538
#define BCE_EMAC_RX_STAT_ETHERSTATSUNDERSIZEPKTS	0x0000153c
#define BCE_EMAC_RX_STAT_ETHERSTATSPKTS64OCTETS	0x00001540
#define BCE_EMAC_RX_STAT_ETHERSTATSPKTS65OCTETSTO127OCTETS	0x00001544
#define BCE_EMAC_RX_STAT_ETHERSTATSPKTS128OCTETSTO255OCTETS	0x00001548
#define BCE_EMAC_RX_STAT_ETHERSTATSPKTS256OCTETSTO511OCTETS	0x0000154c
#define BCE_EMAC_RX_STAT_ETHERSTATSPKTS512OCTETSTO1023OCTETS	0x00001550
#define BCE_EMAC_RX_STAT_ETHERSTATSPKTS1024OCTETSTO1522OCTETS	0x00001554
#define BCE_EMAC_RX_STAT_ETHERSTATSPKTS1523OCTETSTO9022OCTETS	0x00001558
#define BCE_EMAC_RXMAC_DEBUG0				0x0000155c
#define BCE_EMAC_RXMAC_DEBUG1				0x00001560
#define BCE_EMAC_RXMAC_DEBUG1_LENGTH_NE_BYTE_COUNT	 (1L<<0)
#define BCE_EMAC_RXMAC_DEBUG1_LENGTH_OUT_RANGE		 (1L<<1)
#define BCE_EMAC_RXMAC_DEBUG1_BAD_CRC			 (1L<<2)
#define BCE_EMAC_RXMAC_DEBUG1_RX_ERROR			 (1L<<3)
#define BCE_EMAC_RXMAC_DEBUG1_ALIGN_ERROR		 (1L<<4)
#define BCE_EMAC_RXMAC_DEBUG1_LAST_DATA		 (1L<<5)
#define BCE_EMAC_RXMAC_DEBUG1_ODD_BYTE_START		 (1L<<6)
#define BCE_EMAC_RXMAC_DEBUG1_BYTE_COUNT		 (0xffffL<<7)
#define BCE_EMAC_RXMAC_DEBUG1_SLOT_TIME		 (0xffL<<23)

#define BCE_EMAC_RXMAC_DEBUG2				0x00001564
#define BCE_EMAC_RXMAC_DEBUG2_SM_STATE			 (0x7L<<0)
#define BCE_EMAC_RXMAC_DEBUG2_SM_STATE_IDLE		 (0x0L<<0)
#define BCE_EMAC_RXMAC_DEBUG2_SM_STATE_SFD		 (0x1L<<0)
#define BCE_EMAC_RXMAC_DEBUG2_SM_STATE_DATA		 (0x2L<<0)
#define BCE_EMAC_RXMAC_DEBUG2_SM_STATE_SKEEP		 (0x3L<<0)
#define BCE_EMAC_RXMAC_DEBUG2_SM_STATE_EXT		 (0x4L<<0)
#define BCE_EMAC_RXMAC_DEBUG2_SM_STATE_DROP		 (0x5L<<0)
#define BCE_EMAC_RXMAC_DEBUG2_SM_STATE_SDROP		 (0x6L<<0)
#define BCE_EMAC_RXMAC_DEBUG2_SM_STATE_FC		 (0x7L<<0)
#define BCE_EMAC_RXMAC_DEBUG2_IDI_STATE		 (0xfL<<3)
#define BCE_EMAC_RXMAC_DEBUG2_IDI_STATE_IDLE		 (0x0L<<3)
#define BCE_EMAC_RXMAC_DEBUG2_IDI_STATE_DATA0		 (0x1L<<3)
#define BCE_EMAC_RXMAC_DEBUG2_IDI_STATE_DATA1		 (0x2L<<3)
#define BCE_EMAC_RXMAC_DEBUG2_IDI_STATE_DATA2		 (0x3L<<3)
#define BCE_EMAC_RXMAC_DEBUG2_IDI_STATE_DATA3		 (0x4L<<3)
#define BCE_EMAC_RXMAC_DEBUG2_IDI_STATE_ABORT		 (0x5L<<3)
#define BCE_EMAC_RXMAC_DEBUG2_IDI_STATE_WAIT		 (0x6L<<3)
#define BCE_EMAC_RXMAC_DEBUG2_IDI_STATE_STATUS		 (0x7L<<3)
#define BCE_EMAC_RXMAC_DEBUG2_IDI_STATE_LAST		 (0x8L<<3)
#define BCE_EMAC_RXMAC_DEBUG2_BYTE_IN			 (0xffL<<7)
#define BCE_EMAC_RXMAC_DEBUG2_FALSEC			 (1L<<15)
#define BCE_EMAC_RXMAC_DEBUG2_TAGGED			 (1L<<16)
#define BCE_EMAC_RXMAC_DEBUG2_PAUSE_STATE		 (1L<<18)
#define BCE_EMAC_RXMAC_DEBUG2_PAUSE_STATE_IDLE		 (0L<<18)
#define BCE_EMAC_RXMAC_DEBUG2_PAUSE_STATE_PAUSED	 (1L<<18)
#define BCE_EMAC_RXMAC_DEBUG2_SE_COUNTER		 (0xfL<<19)
#define BCE_EMAC_RXMAC_DEBUG2_QUANTA			 (0x1fL<<23)

#define BCE_EMAC_RXMAC_DEBUG3				0x00001568
#define BCE_EMAC_RXMAC_DEBUG3_PAUSE_CTR		 (0xffffL<<0)
#define BCE_EMAC_RXMAC_DEBUG3_TMP_PAUSE_CTR		 (0xffffL<<16)

#define BCE_EMAC_RXMAC_DEBUG4				0x0000156c
#define BCE_EMAC_RXMAC_DEBUG4_TYPE_FIELD		 (0xffffL<<0)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE		 (0x3fL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_IDLE		 (0x0L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_UMAC2		 (0x1L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_UMAC3		 (0x2L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_UNI		 (0x3L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_MMAC2		 (0x7L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_MMAC3		 (0x5L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_PSA1		 (0x6L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_PSA2		 (0x7L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_PSA3		 (0x8L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_MC2		 (0x9L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_MC3		 (0xaL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_MWAIT1	 (0xeL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_MWAIT2	 (0xfL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_MCHECK	 (0x10L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_MC		 (0x11L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_BC2		 (0x12L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_BC3		 (0x13L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_BSA1		 (0x14L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_BSA2		 (0x15L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_BSA3		 (0x16L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_BTYPE		 (0x17L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_BC		 (0x18L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_PTYPE		 (0x19L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_CMD		 (0x1aL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_MAC		 (0x1bL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_LATCH		 (0x1cL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_XOFF		 (0x1dL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_XON		 (0x1eL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_PAUSED	 (0x1fL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_NPAUSED	 (0x20L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_TTYPE		 (0x21L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_TVAL		 (0x22L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_USA1		 (0x23L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_USA2		 (0x24L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_USA3		 (0x25L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_UTYPE		 (0x26L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_UTTYPE	 (0x27L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_UTVAL		 (0x28L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_MTYPE		 (0x29L<<16)
#define BCE_EMAC_RXMAC_DEBUG4_FILT_STATE_DROP		 (0x2aL<<16)
#define BCE_EMAC_RXMAC_DEBUG4_DROP_PKT			 (1L<<22)
#define BCE_EMAC_RXMAC_DEBUG4_SLOT_FILLED		 (1L<<23)
#define BCE_EMAC_RXMAC_DEBUG4_FALSE_CARRIER		 (1L<<24)
#define BCE_EMAC_RXMAC_DEBUG4_LAST_DATA		 (1L<<25)
#define BCE_EMAC_RXMAC_DEBUG4_sfd_FOUND		 (1L<<26)
#define BCE_EMAC_RXMAC_DEBUG4_ADVANCE			 (1L<<27)
#define BCE_EMAC_RXMAC_DEBUG4_START			 (1L<<28)

#define BCE_EMAC_RXMAC_DEBUG5				0x00001570
#define BCE_EMAC_RXMAC_DEBUG5_PS_IDISM			 (0x7L<<0)
#define BCE_EMAC_RXMAC_DEBUG5_PS_IDISM_IDLE		 (0L<<0)
#define BCE_EMAC_RXMAC_DEBUG5_PS_IDISM_WAIT_EOF	 (1L<<0)
#define BCE_EMAC_RXMAC_DEBUG5_PS_IDISM_WAIT_STAT	 (2L<<0)
#define BCE_EMAC_RXMAC_DEBUG5_PS_IDISM_SET_EOF4FCRC	 (3L<<0)
#define BCE_EMAC_RXMAC_DEBUG5_PS_IDISM_SET_EOF4RDE	 (4L<<0)
#define BCE_EMAC_RXMAC_DEBUG5_PS_IDISM_SET_EOF4ALL	 (5L<<0)
#define BCE_EMAC_RXMAC_DEBUG5_PS_IDISM_1WD_WAIT_STAT	 (6L<<0)
#define BCE_EMAC_RXMAC_DEBUG5_CCODE_BUF1		 (0x7L<<4)
#define BCE_EMAC_RXMAC_DEBUG5_CCODE_BUF1_VDW		 (0x0L<<4)
#define BCE_EMAC_RXMAC_DEBUG5_CCODE_BUF1_STAT		 (0x1L<<4)
#define BCE_EMAC_RXMAC_DEBUG5_CCODE_BUF1_AEOF		 (0x2L<<4)
#define BCE_EMAC_RXMAC_DEBUG5_CCODE_BUF1_NEOF		 (0x3L<<4)
#define BCE_EMAC_RXMAC_DEBUG5_CCODE_BUF1_SOF		 (0x4L<<4)
#define BCE_EMAC_RXMAC_DEBUG5_CCODE_BUF1_SAEOF		 (0x6L<<4)
#define BCE_EMAC_RXMAC_DEBUG5_CCODE_BUF1_SNEOF		 (0x7L<<4)
#define BCE_EMAC_RXMAC_DEBUG5_EOF_DETECTED		 (1L<<7)
#define BCE_EMAC_RXMAC_DEBUG5_CCODE_BUF0		 (0x7L<<8)
#define BCE_EMAC_RXMAC_DEBUG5_RPM_IDI_FIFO_FULL	 (1L<<11)
#define BCE_EMAC_RXMAC_DEBUG5_LOAD_CCODE		 (1L<<12)
#define BCE_EMAC_RXMAC_DEBUG5_LOAD_DATA		 (1L<<13)
#define BCE_EMAC_RXMAC_DEBUG5_LOAD_STAT		 (1L<<14)
#define BCE_EMAC_RXMAC_DEBUG5_CLR_STAT			 (1L<<15)
#define BCE_EMAC_RXMAC_DEBUG5_IDI_RPM_CCODE		 (0x3L<<16)
#define BCE_EMAC_RXMAC_DEBUG5_IDI_RPM_ACCEPT		 (1L<<19)
#define BCE_EMAC_RXMAC_DEBUG5_FMLEN			 (0xfffL<<20)

#define BCE_EMAC_RX_STAT_AC0				0x00001580
#define BCE_EMAC_RX_STAT_AC1				0x00001584
#define BCE_EMAC_RX_STAT_AC2				0x00001588
#define BCE_EMAC_RX_STAT_AC3				0x0000158c
#define BCE_EMAC_RX_STAT_AC4				0x00001590
#define BCE_EMAC_RX_STAT_AC5				0x00001594
#define BCE_EMAC_RX_STAT_AC6				0x00001598
#define BCE_EMAC_RX_STAT_AC7				0x0000159c
#define BCE_EMAC_RX_STAT_AC8				0x000015a0
#define BCE_EMAC_RX_STAT_AC9				0x000015a4
#define BCE_EMAC_RX_STAT_AC10				0x000015a8
#define BCE_EMAC_RX_STAT_AC11				0x000015ac
#define BCE_EMAC_RX_STAT_AC12				0x000015b0
#define BCE_EMAC_RX_STAT_AC13				0x000015b4
#define BCE_EMAC_RX_STAT_AC14				0x000015b8
#define BCE_EMAC_RX_STAT_AC15				0x000015bc
#define BCE_EMAC_RX_STAT_AC16				0x000015c0
#define BCE_EMAC_RX_STAT_AC17				0x000015c4
#define BCE_EMAC_RX_STAT_AC18				0x000015c8
#define BCE_EMAC_RX_STAT_AC19				0x000015cc
#define BCE_EMAC_RX_STAT_AC20				0x000015d0
#define BCE_EMAC_RX_STAT_AC21				0x000015d4
#define BCE_EMAC_RX_STAT_AC22				0x000015d8
#define BCE_EMAC_RXMAC_SUC_DBG_OVERRUNVEC		0x000015dc
#define BCE_EMAC_TX_STAT_IFHCOUTOCTETS			0x00001600
#define BCE_EMAC_TX_STAT_IFHCOUTBADOCTETS		0x00001604
#define BCE_EMAC_TX_STAT_ETHERSTATSCOLLISIONS		0x00001608
#define BCE_EMAC_TX_STAT_OUTXONSENT			0x0000160c
#define BCE_EMAC_TX_STAT_OUTXOFFSENT			0x00001610
#define BCE_EMAC_TX_STAT_FLOWCONTROLDONE		0x00001614
#define BCE_EMAC_TX_STAT_DOT3STATSSINGLECOLLISIONFRAMES	0x00001618
#define BCE_EMAC_TX_STAT_DOT3STATSMULTIPLECOLLISIONFRAMES	0x0000161c
#define BCE_EMAC_TX_STAT_DOT3STATSDEFERREDTRANSMISSIONS	0x00001620
#define BCE_EMAC_TX_STAT_DOT3STATSEXCESSIVECOLLISIONS	0x00001624
#define BCE_EMAC_TX_STAT_DOT3STATSLATECOLLISIONS	0x00001628
#define BCE_EMAC_TX_STAT_IFHCOUTUCASTPKTS		0x0000162c
#define BCE_EMAC_TX_STAT_IFHCOUTMULTICASTPKTS		0x00001630
#define BCE_EMAC_TX_STAT_IFHCOUTBROADCASTPKTS		0x00001634
#define BCE_EMAC_TX_STAT_ETHERSTATSPKTS64OCTETS	0x00001638
#define BCE_EMAC_TX_STAT_ETHERSTATSPKTS65OCTETSTO127OCTETS	0x0000163c
#define BCE_EMAC_TX_STAT_ETHERSTATSPKTS128OCTETSTO255OCTETS	0x00001640
#define BCE_EMAC_TX_STAT_ETHERSTATSPKTS256OCTETSTO511OCTETS	0x00001644
#define BCE_EMAC_TX_STAT_ETHERSTATSPKTS512OCTETSTO1023OCTETS	0x00001648
#define BCE_EMAC_TX_STAT_ETHERSTATSPKTS1024OCTETSTO1522OCTETS	0x0000164c
#define BCE_EMAC_TX_STAT_ETHERSTATSPKTS1523OCTETSTO9022OCTETS	0x00001650
#define BCE_EMAC_TX_STAT_DOT3STATSINTERNALMACTRANSMITERRORS	0x00001654
#define BCE_EMAC_TXMAC_DEBUG0				0x00001658
#define BCE_EMAC_TXMAC_DEBUG1				0x0000165c
#define BCE_EMAC_TXMAC_DEBUG1_ODI_STATE		 (0xfL<<0)
#define BCE_EMAC_TXMAC_DEBUG1_ODI_STATE_IDLE		 (0x0L<<0)
#define BCE_EMAC_TXMAC_DEBUG1_ODI_STATE_START0		 (0x1L<<0)
#define BCE_EMAC_TXMAC_DEBUG1_ODI_STATE_DATA0		 (0x4L<<0)
#define BCE_EMAC_TXMAC_DEBUG1_ODI_STATE_DATA1		 (0x5L<<0)
#define BCE_EMAC_TXMAC_DEBUG1_ODI_STATE_DATA2		 (0x6L<<0)
#define BCE_EMAC_TXMAC_DEBUG1_ODI_STATE_DATA3		 (0x7L<<0)
#define BCE_EMAC_TXMAC_DEBUG1_ODI_STATE_WAIT0		 (0x8L<<0)
#define BCE_EMAC_TXMAC_DEBUG1_ODI_STATE_WAIT1		 (0x9L<<0)
#define BCE_EMAC_TXMAC_DEBUG1_CRS_ENABLE		 (1L<<4)
#define BCE_EMAC_TXMAC_DEBUG1_BAD_CRC			 (1L<<5)
#define BCE_EMAC_TXMAC_DEBUG1_SE_COUNTER		 (0xfL<<6)
#define BCE_EMAC_TXMAC_DEBUG1_SEND_PAUSE		 (1L<<10)
#define BCE_EMAC_TXMAC_DEBUG1_LATE_COLLISION		 (1L<<11)
#define BCE_EMAC_TXMAC_DEBUG1_MAX_DEFER		 (1L<<12)
#define BCE_EMAC_TXMAC_DEBUG1_DEFERRED			 (1L<<13)
#define BCE_EMAC_TXMAC_DEBUG1_ONE_BYTE			 (1L<<14)
#define BCE_EMAC_TXMAC_DEBUG1_IPG_TIME			 (0xfL<<15)
#define BCE_EMAC_TXMAC_DEBUG1_SLOT_TIME		 (0xffL<<19)

#define BCE_EMAC_TXMAC_DEBUG2				0x00001660
#define BCE_EMAC_TXMAC_DEBUG2_BACK_OFF			 (0x3ffL<<0)
#define BCE_EMAC_TXMAC_DEBUG2_BYTE_COUNT		 (0xffffL<<10)
#define BCE_EMAC_TXMAC_DEBUG2_COL_COUNT		 (0x1fL<<26)
#define BCE_EMAC_TXMAC_DEBUG2_COL_BIT			 (1L<<31)

#define BCE_EMAC_TXMAC_DEBUG3				0x00001664
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE			 (0xfL<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_IDLE		 (0x0L<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_PRE1		 (0x1L<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_PRE2		 (0x2L<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_SFD		 (0x3L<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_DATA		 (0x4L<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_CRC1		 (0x5L<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_CRC2		 (0x6L<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_EXT		 (0x7L<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_STATB		 (0x8L<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_STATG		 (0x9L<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_JAM		 (0xaL<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_EJAM		 (0xbL<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_BJAM		 (0xcL<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_SWAIT		 (0xdL<<0)
#define BCE_EMAC_TXMAC_DEBUG3_SM_STATE_BACKOFF		 (0xeL<<0)
#define BCE_EMAC_TXMAC_DEBUG3_FILT_STATE		 (0x7L<<4)
#define BCE_EMAC_TXMAC_DEBUG3_FILT_STATE_IDLE		 (0x0L<<4)
#define BCE_EMAC_TXMAC_DEBUG3_FILT_STATE_WAIT		 (0x1L<<4)
#define BCE_EMAC_TXMAC_DEBUG3_FILT_STATE_UNI		 (0x2L<<4)
#define BCE_EMAC_TXMAC_DEBUG3_FILT_STATE_MC		 (0x3L<<4)
#define BCE_EMAC_TXMAC_DEBUG3_FILT_STATE_BC2		 (0x4L<<4)
#define BCE_EMAC_TXMAC_DEBUG3_FILT_STATE_BC3		 (0x5L<<4)
#define BCE_EMAC_TXMAC_DEBUG3_FILT_STATE_BC		 (0x6L<<4)
#define BCE_EMAC_TXMAC_DEBUG3_CRS_DONE			 (1L<<7)
#define BCE_EMAC_TXMAC_DEBUG3_XOFF			 (1L<<8)
#define BCE_EMAC_TXMAC_DEBUG3_SE_COUNTER		 (0xfL<<9)
#define BCE_EMAC_TXMAC_DEBUG3_QUANTA_COUNTER		 (0x1fL<<13)

#define BCE_EMAC_TXMAC_DEBUG4				0x00001668
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_COUNTER		 (0xffffL<<0)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE		 (0xfL<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_IDLE		 (0x0L<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_MCA1		 (0x2L<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_MCA2		 (0x3L<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_MCA3		 (0x6L<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_SRC1		 (0x7L<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_SRC2		 (0x5L<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_SRC3		 (0x4L<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_TYPE		 (0xcL<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_CMD		 (0xeL<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_TIME		 (0xaL<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_CRC1		 (0x8L<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_CRC2		 (0x9L<<16)
#define BCE_EMAC_TXMAC_DEBUG4_PAUSE_STATE_WAIT		 (0xdL<<16)
#define BCE_EMAC_TXMAC_DEBUG4_STATS0_VALID		 (1L<<20)
#define BCE_EMAC_TXMAC_DEBUG4_APPEND_CRC		 (1L<<21)
#define BCE_EMAC_TXMAC_DEBUG4_SLOT_FILLED		 (1L<<22)
#define BCE_EMAC_TXMAC_DEBUG4_MAX_DEFER		 (1L<<23)
#define BCE_EMAC_TXMAC_DEBUG4_SEND_EXTEND		 (1L<<24)
#define BCE_EMAC_TXMAC_DEBUG4_SEND_PADDING		 (1L<<25)
#define BCE_EMAC_TXMAC_DEBUG4_EOF_LOC			 (1L<<26)
#define BCE_EMAC_TXMAC_DEBUG4_COLLIDING		 (1L<<27)
#define BCE_EMAC_TXMAC_DEBUG4_COL_IN			 (1L<<28)
#define BCE_EMAC_TXMAC_DEBUG4_BURSTING			 (1L<<29)
#define BCE_EMAC_TXMAC_DEBUG4_ADVANCE			 (1L<<30)
#define BCE_EMAC_TXMAC_DEBUG4_GO			 (1L<<31)

#define BCE_EMAC_TX_STAT_AC0				0x00001680
#define BCE_EMAC_TX_STAT_AC1				0x00001684
#define BCE_EMAC_TX_STAT_AC2				0x00001688
#define BCE_EMAC_TX_STAT_AC3				0x0000168c
#define BCE_EMAC_TX_STAT_AC4				0x00001690
#define BCE_EMAC_TX_STAT_AC5				0x00001694
#define BCE_EMAC_TX_STAT_AC6				0x00001698
#define BCE_EMAC_TX_STAT_AC7				0x0000169c
#define BCE_EMAC_TX_STAT_AC8				0x000016a0
#define BCE_EMAC_TX_STAT_AC9				0x000016a4
#define BCE_EMAC_TX_STAT_AC10				0x000016a8
#define BCE_EMAC_TX_STAT_AC11				0x000016ac
#define BCE_EMAC_TX_STAT_AC12				0x000016b0
#define BCE_EMAC_TX_STAT_AC13				0x000016b4
#define BCE_EMAC_TX_STAT_AC14				0x000016b8
#define BCE_EMAC_TX_STAT_AC15				0x000016bc
#define BCE_EMAC_TX_STAT_AC16				0x000016c0
#define BCE_EMAC_TX_STAT_AC17				0x000016c4
#define BCE_EMAC_TX_STAT_AC18				0x000016c8
#define BCE_EMAC_TX_STAT_AC19				0x000016cc
#define BCE_EMAC_TX_STAT_AC20				0x000016d0
#define BCE_EMAC_TX_STAT_AC21				0x000016d4
#define BCE_EMAC_TXMAC_SUC_DBG_OVERRUNVEC		0x000016d8


/*
 *  rpm_reg definition
 *  offset: 0x1800
 */
#define BCE_RPM_COMMAND				0x00001800
#define BCE_RPM_COMMAND_ENABLED			 (1L<<0)
#define BCE_RPM_COMMAND_OVERRUN_ABORT			 (1L<<4)

#define BCE_RPM_STATUS					0x00001804
#define BCE_RPM_STATUS_MBUF_WAIT			 (1L<<0)
#define BCE_RPM_STATUS_FREE_WAIT			 (1L<<1)

#define BCE_RPM_CONFIG					0x00001808
#define BCE_RPM_CONFIG_NO_PSD_HDR_CKSUM		 (1L<<0)
#define BCE_RPM_CONFIG_ACPI_ENA			 (1L<<1)
#define BCE_RPM_CONFIG_ACPI_KEEP			 (1L<<2)
#define BCE_RPM_CONFIG_MP_KEEP				 (1L<<3)
#define BCE_RPM_CONFIG_SORT_VECT_VAL			 (0xfL<<4)
#define BCE_RPM_CONFIG_IGNORE_VLAN			 (1L<<31)

#define BCE_RPM_MGMT_PKT_CTRL					0x0000180c
#define BCE_RPM_MGMT_PKT_CTRL_MGMT_DISCARD_EN	 (1L<<30)
#define BCE_RPM_MGMT_PKT_CTRL_MGMT_EN   		 (1L<<31)

#define BCE_RPM_VLAN_MATCH0				0x00001810
#define BCE_RPM_VLAN_MATCH0_RPM_VLAN_MTCH0_VALUE	 (0xfffL<<0)

#define BCE_RPM_VLAN_MATCH1				0x00001814
#define BCE_RPM_VLAN_MATCH1_RPM_VLAN_MTCH1_VALUE	 (0xfffL<<0)

#define BCE_RPM_VLAN_MATCH2				0x00001818
#define BCE_RPM_VLAN_MATCH2_RPM_VLAN_MTCH2_VALUE	 (0xfffL<<0)

#define BCE_RPM_VLAN_MATCH3				0x0000181c
#define BCE_RPM_VLAN_MATCH3_RPM_VLAN_MTCH3_VALUE	 (0xfffL<<0)

#define BCE_RPM_SORT_USER0				0x00001820
#define BCE_RPM_SORT_USER0_PM_EN			 (0xffffL<<0)
#define BCE_RPM_SORT_USER0_BC_EN			 (1L<<16)
#define BCE_RPM_SORT_USER0_MC_EN			 (1L<<17)
#define BCE_RPM_SORT_USER0_MC_HSH_EN			 (1L<<18)
#define BCE_RPM_SORT_USER0_PROM_EN			 (1L<<19)
#define BCE_RPM_SORT_USER0_VLAN_EN			 (0xfL<<20)
#define BCE_RPM_SORT_USER0_PROM_VLAN			 (1L<<24)
#define BCE_RPM_SORT_USER0_ENA				 (1L<<31)

#define BCE_RPM_SORT_USER1				0x00001824
#define BCE_RPM_SORT_USER1_PM_EN			 (0xffffL<<0)
#define BCE_RPM_SORT_USER1_BC_EN			 (1L<<16)
#define BCE_RPM_SORT_USER1_MC_EN			 (1L<<17)
#define BCE_RPM_SORT_USER1_MC_HSH_EN			 (1L<<18)
#define BCE_RPM_SORT_USER1_PROM_EN			 (1L<<19)
#define BCE_RPM_SORT_USER1_VLAN_EN			 (0xfL<<20)
#define BCE_RPM_SORT_USER1_PROM_VLAN			 (1L<<24)
#define BCE_RPM_SORT_USER1_ENA				 (1L<<31)

#define BCE_RPM_SORT_USER2				0x00001828
#define BCE_RPM_SORT_USER2_PM_EN			 (0xffffL<<0)
#define BCE_RPM_SORT_USER2_BC_EN			 (1L<<16)
#define BCE_RPM_SORT_USER2_MC_EN			 (1L<<17)
#define BCE_RPM_SORT_USER2_MC_HSH_EN			 (1L<<18)
#define BCE_RPM_SORT_USER2_PROM_EN			 (1L<<19)
#define BCE_RPM_SORT_USER2_VLAN_EN			 (0xfL<<20)
#define BCE_RPM_SORT_USER2_PROM_VLAN			 (1L<<24)
#define BCE_RPM_SORT_USER2_ENA				 (1L<<31)

#define BCE_RPM_SORT_USER3				0x0000182c
#define BCE_RPM_SORT_USER3_PM_EN			 (0xffffL<<0)
#define BCE_RPM_SORT_USER3_BC_EN			 (1L<<16)
#define BCE_RPM_SORT_USER3_MC_EN			 (1L<<17)
#define BCE_RPM_SORT_USER3_MC_HSH_EN			 (1L<<18)
#define BCE_RPM_SORT_USER3_PROM_EN			 (1L<<19)
#define BCE_RPM_SORT_USER3_VLAN_EN			 (0xfL<<20)
#define BCE_RPM_SORT_USER3_PROM_VLAN			 (1L<<24)
#define BCE_RPM_SORT_USER3_ENA				 (1L<<31)

#define BCE_RPM_STAT_L2_FILTER_DISCARDS		0x00001840
#define BCE_RPM_STAT_RULE_CHECKER_DISCARDS		0x00001844
#define BCE_RPM_STAT_IFINFTQDISCARDS			0x00001848
#define BCE_RPM_STAT_IFINMBUFDISCARD			0x0000184c
#define BCE_RPM_STAT_RULE_CHECKER_P4_HIT		0x00001850
#define BCE_RPM_STAT_AC0				0x00001880
#define BCE_RPM_STAT_AC1				0x00001884
#define BCE_RPM_STAT_AC2				0x00001888
#define BCE_RPM_STAT_AC3				0x0000188c
#define BCE_RPM_STAT_AC4				0x00001890
#define BCE_RPM_RC_CNTL_0				0x00001900
#define BCE_RPM_RC_CNTL_0_OFFSET			 (0xffL<<0)
#define BCE_RPM_RC_CNTL_0_CLASS			 (0x7L<<8)
#define BCE_RPM_RC_CNTL_0_PRIORITY			 (1L<<11)
#define BCE_RPM_RC_CNTL_0_P4				 (1L<<12)
#define BCE_RPM_RC_CNTL_0_HDR_TYPE			 (0x7L<<13)
#define BCE_RPM_RC_CNTL_0_HDR_TYPE_START		 (0L<<13)
#define BCE_RPM_RC_CNTL_0_HDR_TYPE_IP			 (1L<<13)
#define BCE_RPM_RC_CNTL_0_HDR_TYPE_TCP			 (2L<<13)
#define BCE_RPM_RC_CNTL_0_HDR_TYPE_UDP			 (3L<<13)
#define BCE_RPM_RC_CNTL_0_HDR_TYPE_DATA		 (4L<<13)
#define BCE_RPM_RC_CNTL_0_COMP				 (0x3L<<16)
#define BCE_RPM_RC_CNTL_0_COMP_EQUAL			 (0L<<16)
#define BCE_RPM_RC_CNTL_0_COMP_NEQUAL			 (1L<<16)
#define BCE_RPM_RC_CNTL_0_COMP_GREATER			 (2L<<16)
#define BCE_RPM_RC_CNTL_0_COMP_LESS			 (3L<<16)
#define BCE_RPM_RC_CNTL_0_SBIT				 (1L<<19)
#define BCE_RPM_RC_CNTL_0_CMDSEL			 (0xfL<<20)
#define BCE_RPM_RC_CNTL_0_MAP				 (1L<<24)
#define BCE_RPM_RC_CNTL_0_DISCARD			 (1L<<25)
#define BCE_RPM_RC_CNTL_0_MASK				 (1L<<26)
#define BCE_RPM_RC_CNTL_0_P1				 (1L<<27)
#define BCE_RPM_RC_CNTL_0_P2				 (1L<<28)
#define BCE_RPM_RC_CNTL_0_P3				 (1L<<29)
#define BCE_RPM_RC_CNTL_0_NBIT				 (1L<<30)

#define BCE_RPM_RC_VALUE_MASK_0			0x00001904
#define BCE_RPM_RC_VALUE_MASK_0_VALUE			 (0xffffL<<0)
#define BCE_RPM_RC_VALUE_MASK_0_MASK			 (0xffffL<<16)

#define BCE_RPM_RC_CNTL_1				0x00001908
#define BCE_RPM_RC_CNTL_1_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_1_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_1			0x0000190c
#define BCE_RPM_RC_CNTL_2				0x00001910
#define BCE_RPM_RC_CNTL_2_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_2_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_2			0x00001914
#define BCE_RPM_RC_CNTL_3				0x00001918
#define BCE_RPM_RC_CNTL_3_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_3_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_3			0x0000191c
#define BCE_RPM_RC_CNTL_4				0x00001920
#define BCE_RPM_RC_CNTL_4_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_4_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_4			0x00001924
#define BCE_RPM_RC_CNTL_5				0x00001928
#define BCE_RPM_RC_CNTL_5_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_5_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_5			0x0000192c
#define BCE_RPM_RC_CNTL_6				0x00001930
#define BCE_RPM_RC_CNTL_6_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_6_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_6			0x00001934
#define BCE_RPM_RC_CNTL_7				0x00001938
#define BCE_RPM_RC_CNTL_7_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_7_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_7			0x0000193c
#define BCE_RPM_RC_CNTL_8				0x00001940
#define BCE_RPM_RC_CNTL_8_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_8_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_8			0x00001944
#define BCE_RPM_RC_CNTL_9				0x00001948
#define BCE_RPM_RC_CNTL_9_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_9_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_9			0x0000194c
#define BCE_RPM_RC_CNTL_10				0x00001950
#define BCE_RPM_RC_CNTL_10_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_10_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_10			0x00001954
#define BCE_RPM_RC_CNTL_11				0x00001958
#define BCE_RPM_RC_CNTL_11_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_11_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_11			0x0000195c
#define BCE_RPM_RC_CNTL_12				0x00001960
#define BCE_RPM_RC_CNTL_12_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_12_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_12			0x00001964
#define BCE_RPM_RC_CNTL_13				0x00001968
#define BCE_RPM_RC_CNTL_13_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_13_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_13			0x0000196c
#define BCE_RPM_RC_CNTL_14				0x00001970
#define BCE_RPM_RC_CNTL_14_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_14_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_14			0x00001974
#define BCE_RPM_RC_CNTL_15				0x00001978
#define BCE_RPM_RC_CNTL_15_A				 (0x3ffffL<<0)
#define BCE_RPM_RC_CNTL_15_B				 (0xfffL<<19)

#define BCE_RPM_RC_VALUE_MASK_15			0x0000197c
#define BCE_RPM_RC_CONFIG				0x00001980
#define BCE_RPM_RC_CONFIG_RULE_ENABLE			 (0xffffL<<0)
#define BCE_RPM_RC_CONFIG_DEF_CLASS			 (0x7L<<24)

#define BCE_RPM_DEBUG0					0x00001984
#define BCE_RPM_DEBUG0_FM_BCNT				 (0xffffL<<0)
#define BCE_RPM_DEBUG0_T_DATA_OFST_VLD			 (1L<<16)
#define BCE_RPM_DEBUG0_T_UDP_OFST_VLD			 (1L<<17)
#define BCE_RPM_DEBUG0_T_TCP_OFST_VLD			 (1L<<18)
#define BCE_RPM_DEBUG0_T_IP_OFST_VLD			 (1L<<19)
#define BCE_RPM_DEBUG0_IP_MORE_FRGMT			 (1L<<20)
#define BCE_RPM_DEBUG0_T_IP_NO_TCP_UDP_HDR		 (1L<<21)
#define BCE_RPM_DEBUG0_LLC_SNAP			 (1L<<22)
#define BCE_RPM_DEBUG0_FM_STARTED			 (1L<<23)
#define BCE_RPM_DEBUG0_DONE				 (1L<<24)
#define BCE_RPM_DEBUG0_WAIT_4_DONE			 (1L<<25)
#define BCE_RPM_DEBUG0_USE_TPBUF_CKSUM			 (1L<<26)
#define BCE_RPM_DEBUG0_RX_NO_PSD_HDR_CKSUM		 (1L<<27)
#define BCE_RPM_DEBUG0_IGNORE_VLAN			 (1L<<28)
#define BCE_RPM_DEBUG0_RP_ENA_ACTIVE			 (1L<<31)

#define BCE_RPM_DEBUG1					0x00001988
#define BCE_RPM_DEBUG1_FSM_CUR_ST			 (0xffffL<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_IDLE			 (0L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_ETYPE_B6_ALL		 (1L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_ETYPE_B2_IPLLC	 (2L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_ETYPE_B6_IP		 (4L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_ETYPE_B2_IP		 (8L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_IP_START		 (16L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_IP			 (32L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_TCP			 (64L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_UDP			 (128L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_AH			 (256L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_ESP			 (512L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_ESP_PAYLOAD		 (1024L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_DATA			 (2048L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_ADD_CARRY		 (0x2000L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_ADD_CARRYOUT		 (0x4000L<<0)
#define BCE_RPM_DEBUG1_FSM_CUR_ST_LATCH_RESULT		 (0x8000L<<0)
#define BCE_RPM_DEBUG1_HDR_BCNT			 (0x7ffL<<16)
#define BCE_RPM_DEBUG1_UNKNOWN_ETYPE_D			 (1L<<28)
#define BCE_RPM_DEBUG1_VLAN_REMOVED_D2			 (1L<<29)
#define BCE_RPM_DEBUG1_VLAN_REMOVED_D1			 (1L<<30)
#define BCE_RPM_DEBUG1_EOF_0XTRA_WD			 (1L<<31)

#define BCE_RPM_DEBUG2					0x0000198c
#define BCE_RPM_DEBUG2_CMD_HIT_VEC			 (0xffffL<<0)
#define BCE_RPM_DEBUG2_IP_BCNT				 (0xffL<<16)
#define BCE_RPM_DEBUG2_THIS_CMD_M4			 (1L<<24)
#define BCE_RPM_DEBUG2_THIS_CMD_M3			 (1L<<25)
#define BCE_RPM_DEBUG2_THIS_CMD_M2			 (1L<<26)
#define BCE_RPM_DEBUG2_THIS_CMD_M1			 (1L<<27)
#define BCE_RPM_DEBUG2_IPIPE_EMPTY			 (1L<<28)
#define BCE_RPM_DEBUG2_FM_DISCARD			 (1L<<29)
#define BCE_RPM_DEBUG2_LAST_RULE_IN_FM_D2		 (1L<<30)
#define BCE_RPM_DEBUG2_LAST_RULE_IN_FM_D1		 (1L<<31)

#define BCE_RPM_DEBUG3					0x00001990
#define BCE_RPM_DEBUG3_AVAIL_MBUF_PTR			 (0x1ffL<<0)
#define BCE_RPM_DEBUG3_RDE_RLUPQ_WR_REQ_INT		 (1L<<9)
#define BCE_RPM_DEBUG3_RDE_RBUF_WR_LAST_INT		 (1L<<10)
#define BCE_RPM_DEBUG3_RDE_RBUF_WR_REQ_INT		 (1L<<11)
#define BCE_RPM_DEBUG3_RDE_RBUF_FREE_REQ		 (1L<<12)
#define BCE_RPM_DEBUG3_RDE_RBUF_ALLOC_REQ		 (1L<<13)
#define BCE_RPM_DEBUG3_DFSM_MBUF_NOTAVAIL		 (1L<<14)
#define BCE_RPM_DEBUG3_RBUF_RDE_SOF_DROP		 (1L<<15)
#define BCE_RPM_DEBUG3_DFIFO_VLD_ENTRY_CT		 (0xfL<<16)
#define BCE_RPM_DEBUG3_RDE_SRC_FIFO_ALMFULL		 (1L<<21)
#define BCE_RPM_DEBUG3_DROP_NXT_VLD			 (1L<<22)
#define BCE_RPM_DEBUG3_DROP_NXT			 (1L<<23)
#define BCE_RPM_DEBUG3_FTQ_FSM				 (0x3L<<24)
#define BCE_RPM_DEBUG3_FTQ_FSM_IDLE			 (0x0L<<24)
#define BCE_RPM_DEBUG3_FTQ_FSM_WAIT_ACK		 (0x1L<<24)
#define BCE_RPM_DEBUG3_FTQ_FSM_WAIT_FREE		 (0x2L<<24)
#define BCE_RPM_DEBUG3_MBWRITE_FSM			 (0x3L<<26)
#define BCE_RPM_DEBUG3_MBWRITE_FSM_WAIT_SOF		 (0x0L<<26)
#define BCE_RPM_DEBUG3_MBWRITE_FSM_GET_MBUF		 (0x1L<<26)
#define BCE_RPM_DEBUG3_MBWRITE_FSM_DMA_DATA		 (0x2L<<26)
#define BCE_RPM_DEBUG3_MBWRITE_FSM_WAIT_DATA		 (0x3L<<26)
#define BCE_RPM_DEBUG3_MBWRITE_FSM_WAIT_EOF		 (0x4L<<26)
#define BCE_RPM_DEBUG3_MBWRITE_FSM_WAIT_MF_ACK		 (0x5L<<26)
#define BCE_RPM_DEBUG3_MBWRITE_FSM_WAIT_DROP_NXT_VLD	 (0x6L<<26)
#define BCE_RPM_DEBUG3_MBWRITE_FSM_DONE		 (0x7L<<26)
#define BCE_RPM_DEBUG3_MBFREE_FSM			 (1L<<29)
#define BCE_RPM_DEBUG3_MBFREE_FSM_IDLE			 (0L<<29)
#define BCE_RPM_DEBUG3_MBFREE_FSM_WAIT_ACK		 (1L<<29)
#define BCE_RPM_DEBUG3_MBALLOC_FSM			 (1L<<30)
#define BCE_RPM_DEBUG3_MBALLOC_FSM_ET_MBUF		 (0x0L<<30)
#define BCE_RPM_DEBUG3_MBALLOC_FSM_IVE_MBUF		 (0x1L<<30)
#define BCE_RPM_DEBUG3_CCODE_EOF_ERROR			 (1L<<31)

#define BCE_RPM_DEBUG4					0x00001994
#define BCE_RPM_DEBUG4_DFSM_MBUF_CLUSTER		 (0x1ffffffL<<0)
#define BCE_RPM_DEBUG4_DFIFO_CUR_CCODE			 (0x7L<<25)
#define BCE_RPM_DEBUG4_MBWRITE_FSM			 (0x7L<<28)
#define BCE_RPM_DEBUG4_DFIFO_EMPTY			 (1L<<31)

#define BCE_RPM_DEBUG5					0x00001998
#define BCE_RPM_DEBUG5_RDROP_WPTR			 (0x1fL<<0)
#define BCE_RPM_DEBUG5_RDROP_ACPI_RPTR			 (0x1fL<<5)
#define BCE_RPM_DEBUG5_RDROP_MC_RPTR			 (0x1fL<<10)
#define BCE_RPM_DEBUG5_RDROP_RC_RPTR			 (0x1fL<<15)
#define BCE_RPM_DEBUG5_RDROP_ACPI_EMPTY		 (1L<<20)
#define BCE_RPM_DEBUG5_RDROP_MC_EMPTY			 (1L<<21)
#define BCE_RPM_DEBUG5_RDROP_AEOF_VEC_AT_RDROP_MC_RPTR	 (1L<<22)
#define BCE_RPM_DEBUG5_HOLDREG_WOL_DROP_INT		 (1L<<23)
#define BCE_RPM_DEBUG5_HOLDREG_DISCARD			 (1L<<24)
#define BCE_RPM_DEBUG5_HOLDREG_MBUF_NOTAVAIL		 (1L<<25)
#define BCE_RPM_DEBUG5_HOLDREG_MC_EMPTY		 (1L<<26)
#define BCE_RPM_DEBUG5_HOLDREG_RC_EMPTY		 (1L<<27)
#define BCE_RPM_DEBUG5_HOLDREG_FC_EMPTY		 (1L<<28)
#define BCE_RPM_DEBUG5_HOLDREG_ACPI_EMPTY		 (1L<<29)
#define BCE_RPM_DEBUG5_HOLDREG_FULL_T			 (1L<<30)
#define BCE_RPM_DEBUG5_HOLDREG_RD			 (1L<<31)

#define BCE_RPM_DEBUG6					0x0000199c
#define BCE_RPM_DEBUG6_ACPI_VEC			 (0xffffL<<0)
#define BCE_RPM_DEBUG6_VEC				 (0xffffL<<16)

#define BCE_RPM_DEBUG7					0x000019a0
#define BCE_RPM_DEBUG7_RPM_DBG7_LAST_CRC		 (0xffffffffL<<0)

#define BCE_RPM_DEBUG8					0x000019a4
#define BCE_RPM_DEBUG8_PS_ACPI_FSM			 (0xfL<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_IDLE		 (0L<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_SOF_W1_ADDR		 (1L<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_SOF_W2_ADDR		 (2L<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_SOF_W3_ADDR		 (3L<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_SOF_WAIT_THBUF	 (4L<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_W3_DATA		 (5L<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_W0_ADDR		 (6L<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_W1_ADDR		 (7L<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_W2_ADDR		 (8L<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_W3_ADDR		 (9L<<0)
#define BCE_RPM_DEBUG8_PS_ACPI_FSM_WAIT_THBUF		 (10L<<0)
#define BCE_RPM_DEBUG8_COMPARE_AT_W0			 (1L<<4)
#define BCE_RPM_DEBUG8_COMPARE_AT_W3_DATA		 (1L<<5)
#define BCE_RPM_DEBUG8_COMPARE_AT_SOF_WAIT		 (1L<<6)
#define BCE_RPM_DEBUG8_COMPARE_AT_SOF_W3		 (1L<<7)
#define BCE_RPM_DEBUG8_COMPARE_AT_SOF_W2		 (1L<<8)
#define BCE_RPM_DEBUG8_EOF_W_LTEQ6_VLDBYTES		 (1L<<9)
#define BCE_RPM_DEBUG8_EOF_W_LTEQ4_VLDBYTES		 (1L<<10)
#define BCE_RPM_DEBUG8_NXT_EOF_W_12_VLDBYTES		 (1L<<11)
#define BCE_RPM_DEBUG8_EOF_DET				 (1L<<12)
#define BCE_RPM_DEBUG8_SOF_DET				 (1L<<13)
#define BCE_RPM_DEBUG8_WAIT_4_SOF			 (1L<<14)
#define BCE_RPM_DEBUG8_ALL_DONE			 (1L<<15)
#define BCE_RPM_DEBUG8_THBUF_ADDR			 (0x7fL<<16)
#define BCE_RPM_DEBUG8_BYTE_CTR			 (0xffL<<24)

#define BCE_RPM_DEBUG9					0x000019a8
#define BCE_RPM_DEBUG9_OUTFIFO_COUNT			 (0x7L<<0)
#define BCE_RPM_DEBUG9_RDE_ACPI_RDY			 (1L<<3)
#define BCE_RPM_DEBUG9_VLD_RD_ENTRY_CT			 (0x7L<<4)
#define BCE_RPM_DEBUG9_OUTFIFO_OVERRUN_OCCURRED	 (1L<<28)
#define BCE_RPM_DEBUG9_INFIFO_OVERRUN_OCCURRED		 (1L<<29)
#define BCE_RPM_DEBUG9_ACPI_MATCH_INT			 (1L<<30)
#define BCE_RPM_DEBUG9_ACPI_ENABLE_SYN			 (1L<<31)

#define BCE_RPM_ACPI_DBG_BUF_W00			0x000019c0
#define BCE_RPM_ACPI_DBG_BUF_W01			0x000019c4
#define BCE_RPM_ACPI_DBG_BUF_W02			0x000019c8
#define BCE_RPM_ACPI_DBG_BUF_W03			0x000019cc
#define BCE_RPM_ACPI_DBG_BUF_W10			0x000019d0
#define BCE_RPM_ACPI_DBG_BUF_W11			0x000019d4
#define BCE_RPM_ACPI_DBG_BUF_W12			0x000019d8
#define BCE_RPM_ACPI_DBG_BUF_W13			0x000019dc
#define BCE_RPM_ACPI_DBG_BUF_W20			0x000019e0
#define BCE_RPM_ACPI_DBG_BUF_W21			0x000019e4
#define BCE_RPM_ACPI_DBG_BUF_W22			0x000019e8
#define BCE_RPM_ACPI_DBG_BUF_W23			0x000019ec
#define BCE_RPM_ACPI_DBG_BUF_W30			0x000019f0
#define BCE_RPM_ACPI_DBG_BUF_W31			0x000019f4
#define BCE_RPM_ACPI_DBG_BUF_W32			0x000019f8
#define BCE_RPM_ACPI_DBG_BUF_W33			0x000019fc


/*
 *  rlup_reg definition
 *  offset: 0x2000
 */
#define BCE_RLUP_FTQ_CMD					0x000023f8
#define BCE_RLUP_FTQ_CTL					0x000023fc
#define BCE_RLUP_FTQ_CTL_MAX_DEPTH			(0x3ffL<<12)
#define BCE_RLUP_FTQ_CTL_CUR_DEPTH			(0x3ffL<<22)


/*
 *  rv2pcsr_reg definition
 *  offset: 0x2400
 */
#define BCE_RV2PCSR_FTQ_CMD					0x000027f8
#define BCE_RV2PCSR_FTQ_CTL					0x000027fc
#define BCE_RV2PCSR_FTQ_CTL_MAX_DEPTH		(0x3ffL<<12)
#define BCE_RV2PCSR_FTQ_CTL_CUR_DEPTH		(0x3ffL<<22)


/*
 *  rdma_reg definition
 *  offset: 0x2c00
 */
#define BCE_RDMA_FTQ_CMD					0x00002ff8
#define BCE_RDMA_FTQ_CTL					0x00002ffc
#define BCE_RDMA_FTQ_CTL_MAX_DEPTH			(0x3ffL<<12)
#define BCE_RDMA_FTQ_CTL_CUR_DEPTH			(0x3ffL<<22)



/*
 *  timer_reg definition
 *  offset: 0x4400
 */

#define BCE_TIMER_COMMAND					0x00004400
#define BCE_TIMER_COMMAND_ENABLED			(1L<<0)

#define BCE_TIMER_STATUS					0x00004404
#define BCE_TIMER_STATUS_CMP_FTQ_WAIT 		(1L<<0)
#define BCE_TIMER_STATUS_POLL_PASS_CNT		(1L<<8)
#define BCE_TIMER_STATUS_TMR1_CNT			(1L<<9)
#define BCE_TIMER_STATUS_TMR2_CNT			(1L<<10)
#define BCE_TIMER_STATUS_TMR3_CNT			(1L<<11)
#define BCE_TIMER_STATUS_TMR4_CNT			(1L<<12)
#define BCE_TIMER_STATUS_TMR5_CNT			(1L<<13)

#define BCE_TIMER_25MHZ_FREE_RUN			0x00004448


/*
 *  tsch_reg definition
 *  offset: 0x4c00
 */

#define BCE_TSCH_FTQ_CMD					0x00004ff8
#define BCE_TSCH_FTQ_CTL					0x00004ffc
#define BCE_TSCH_FTQ_CTL_MAX_DEPTH			(0x3ffL<<12)
#define BCE_TSCH_FTQ_CTL_CUR_DEPTH			(0x3ffL<<22)



/*
 *  rbuf_reg definition
 *  offset: 0x200000
 */
#define BCE_RBUF_COMMAND				0x00200000
#define BCE_RBUF_COMMAND_ENABLED			 (1L<<0)
#define BCE_RBUF_COMMAND_FREE_INIT			 (1L<<1)
#define BCE_RBUF_COMMAND_RAM_INIT			 (1L<<2)
#define BCE_RBUF_COMMAND_OVER_FREE			 (1L<<4)
#define BCE_RBUF_COMMAND_ALLOC_REQ			 (1L<<5)

#define BCE_RBUF_STATUS1				0x00200004
#define BCE_RBUF_STATUS1_FREE_COUNT			 (0x3ffL<<0)

#define BCE_RBUF_STATUS2				0x00200008
#define BCE_RBUF_STATUS2_FREE_TAIL			 (0x3ffL<<0)
#define BCE_RBUF_STATUS2_FREE_HEAD			 (0x3ffL<<16)

#define BCE_RBUF_CONFIG				0x0020000c
#define BCE_RBUF_CONFIG_XOFF_TRIP			 (0x3ffL<<0)
#define BCE_RBUF_CONFIG_XON_TRIP			 (0x3ffL<<16)

#define BCE_RBUF_FW_BUF_ALLOC				0x00200010
#define BCE_RBUF_FW_BUF_ALLOC_VALUE			 (0x1ffL<<7)

#define BCE_RBUF_FW_BUF_FREE				0x00200014
#define BCE_RBUF_FW_BUF_FREE_COUNT			 (0x7fL<<0)
#define BCE_RBUF_FW_BUF_FREE_TAIL			 (0x1ffL<<7)
#define BCE_RBUF_FW_BUF_FREE_HEAD			 (0x1ffL<<16)

#define BCE_RBUF_FW_BUF_SEL				0x00200018
#define BCE_RBUF_FW_BUF_SEL_COUNT			 (0x7fL<<0)
#define BCE_RBUF_FW_BUF_SEL_TAIL			 (0x1ffL<<7)
#define BCE_RBUF_FW_BUF_SEL_HEAD			 (0x1ffL<<16)

#define BCE_RBUF_CONFIG2				0x0020001c
#define BCE_RBUF_CONFIG2_MAC_DROP_TRIP			 (0x3ffL<<0)
#define BCE_RBUF_CONFIG2_MAC_KEEP_TRIP			 (0x3ffL<<16)

#define BCE_RBUF_CONFIG3				0x00200020
#define BCE_RBUF_CONFIG3_CU_DROP_TRIP			 (0x3ffL<<0)
#define BCE_RBUF_CONFIG3_CU_KEEP_TRIP			 (0x3ffL<<16)

#define BCE_RBUF_PKT_DATA				0x00208000
#define BCE_RBUF_CLIST_DATA				0x00210000
#define BCE_RBUF_BUF_DATA				0x00220000


/*
 *  rv2p_reg definition
 *  offset: 0x2800
 */
#define BCE_RV2P_COMMAND				0x00002800
#define BCE_RV2P_COMMAND_ENABLED			 (1L<<0)
#define BCE_RV2P_COMMAND_PROC1_INTRPT			 (1L<<1)
#define BCE_RV2P_COMMAND_PROC2_INTRPT			 (1L<<2)
#define BCE_RV2P_COMMAND_ABORT0			 (1L<<4)
#define BCE_RV2P_COMMAND_ABORT1			 (1L<<5)
#define BCE_RV2P_COMMAND_ABORT2			 (1L<<6)
#define BCE_RV2P_COMMAND_ABORT3			 (1L<<7)
#define BCE_RV2P_COMMAND_ABORT4			 (1L<<8)
#define BCE_RV2P_COMMAND_ABORT5			 (1L<<9)
#define BCE_RV2P_COMMAND_PROC1_RESET			 (1L<<16)
#define BCE_RV2P_COMMAND_PROC2_RESET			 (1L<<17)
#define BCE_RV2P_COMMAND_CTXIF_RESET			 (1L<<18)

#define BCE_RV2P_STATUS				0x00002804
#define BCE_RV2P_STATUS_ALWAYS_0			 (1L<<0)
#define BCE_RV2P_STATUS_RV2P_GEN_STAT0_CNT		 (1L<<8)
#define BCE_RV2P_STATUS_RV2P_GEN_STAT1_CNT		 (1L<<9)
#define BCE_RV2P_STATUS_RV2P_GEN_STAT2_CNT		 (1L<<10)
#define BCE_RV2P_STATUS_RV2P_GEN_STAT3_CNT		 (1L<<11)
#define BCE_RV2P_STATUS_RV2P_GEN_STAT4_CNT		 (1L<<12)
#define BCE_RV2P_STATUS_RV2P_GEN_STAT5_CNT		 (1L<<13)

#define BCE_RV2P_CONFIG				0x00002808
#define BCE_RV2P_CONFIG_STALL_PROC1			 (1L<<0)
#define BCE_RV2P_CONFIG_STALL_PROC2			 (1L<<1)
#define BCE_RV2P_CONFIG_PROC1_STALL_ON_ABORT0		 (1L<<8)
#define BCE_RV2P_CONFIG_PROC1_STALL_ON_ABORT1		 (1L<<9)
#define BCE_RV2P_CONFIG_PROC1_STALL_ON_ABORT2		 (1L<<10)
#define BCE_RV2P_CONFIG_PROC1_STALL_ON_ABORT3		 (1L<<11)
#define BCE_RV2P_CONFIG_PROC1_STALL_ON_ABORT4		 (1L<<12)
#define BCE_RV2P_CONFIG_PROC1_STALL_ON_ABORT5		 (1L<<13)
#define BCE_RV2P_CONFIG_PROC2_STALL_ON_ABORT0		 (1L<<16)
#define BCE_RV2P_CONFIG_PROC2_STALL_ON_ABORT1		 (1L<<17)
#define BCE_RV2P_CONFIG_PROC2_STALL_ON_ABORT2		 (1L<<18)
#define BCE_RV2P_CONFIG_PROC2_STALL_ON_ABORT3		 (1L<<19)
#define BCE_RV2P_CONFIG_PROC2_STALL_ON_ABORT4		 (1L<<20)
#define BCE_RV2P_CONFIG_PROC2_STALL_ON_ABORT5		 (1L<<21)
#define BCE_RV2P_CONFIG_PAGE_SIZE			 (0xfL<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_256			 (0L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_512			 (1L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_1K			 (2L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_2K			 (3L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_4K			 (4L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_8K			 (5L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_16K			 (6L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_32K			 (7L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_64K			 (8L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_128K			 (9L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_256K			 (10L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_512K			 (11L<<24)
#define BCE_RV2P_CONFIG_PAGE_SIZE_1M			 (12L<<24)

#define BCE_RV2P_GEN_BFR_ADDR_0			0x00002810
#define BCE_RV2P_GEN_BFR_ADDR_0_VALUE			 (0xffffL<<16)

#define BCE_RV2P_GEN_BFR_ADDR_1			0x00002814
#define BCE_RV2P_GEN_BFR_ADDR_1_VALUE			 (0xffffL<<16)

#define BCE_RV2P_GEN_BFR_ADDR_2			0x00002818
#define BCE_RV2P_GEN_BFR_ADDR_2_VALUE			 (0xffffL<<16)

#define BCE_RV2P_GEN_BFR_ADDR_3			0x0000281c
#define BCE_RV2P_GEN_BFR_ADDR_3_VALUE			 (0xffffL<<16)

#define BCE_RV2P_INSTR_HIGH				0x00002830
#define BCE_RV2P_INSTR_HIGH_HIGH			 (0x1fL<<0)

#define BCE_RV2P_INSTR_LOW				0x00002834
#define BCE_RV2P_PROC1_ADDR_CMD			0x00002838
#define BCE_RV2P_PROC1_ADDR_CMD_ADD			 (0x3ffL<<0)
#define BCE_RV2P_PROC1_ADDR_CMD_RDWR			 (1L<<31)

#define BCE_RV2P_PROC2_ADDR_CMD			0x0000283c
#define BCE_RV2P_PROC2_ADDR_CMD_ADD			 (0x3ffL<<0)
#define BCE_RV2P_PROC2_ADDR_CMD_RDWR			 (1L<<31)

#define BCE_RV2P_PROC1_GRC_DEBUG			0x00002840
#define BCE_RV2P_PROC2_GRC_DEBUG			0x00002844
#define BCE_RV2P_GRC_PROC_DEBUG			0x00002848
#define BCE_RV2P_DEBUG_VECT_PEEK			0x0000284c
#define BCE_RV2P_DEBUG_VECT_PEEK_1_VALUE		 (0x7ffL<<0)
#define BCE_RV2P_DEBUG_VECT_PEEK_1_PEEK_EN		 (1L<<11)
#define BCE_RV2P_DEBUG_VECT_PEEK_1_SEL			 (0xfL<<12)
#define BCE_RV2P_DEBUG_VECT_PEEK_2_VALUE		 (0x7ffL<<16)
#define BCE_RV2P_DEBUG_VECT_PEEK_2_PEEK_EN		 (1L<<27)
#define BCE_RV2P_DEBUG_VECT_PEEK_2_SEL			 (0xfL<<28)

#define BCE_RV2P_PFTQ_DATA				0x00002b40
#define BCE_RV2P_PFTQ_CMD				0x00002b78
#define BCE_RV2P_PFTQ_CMD_OFFSET			 (0x3ffL<<0)
#define BCE_RV2P_PFTQ_CMD_WR_TOP			 (1L<<10)
#define BCE_RV2P_PFTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_RV2P_PFTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_RV2P_PFTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_RV2P_PFTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_RV2P_PFTQ_CMD_ADD_INTERVEN			 (1L<<27)
#define BCE_RV2P_PFTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_RV2P_PFTQ_CMD_INTERVENE_CLR		 (1L<<29)
#define BCE_RV2P_PFTQ_CMD_POP				 (1L<<30)
#define BCE_RV2P_PFTQ_CMD_BUSY				 (1L<<31)

#define BCE_RV2P_PFTQ_CTL				0x00002b7c
#define BCE_RV2P_PFTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_RV2P_PFTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_RV2P_PFTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_RV2P_PFTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_RV2P_PFTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)

#define BCE_RV2P_TFTQ_DATA				0x00002b80
#define BCE_RV2P_TFTQ_CMD				0x00002bb8
#define BCE_RV2P_TFTQ_CMD_OFFSET			 (0x3ffL<<0)
#define BCE_RV2P_TFTQ_CMD_WR_TOP			 (1L<<10)
#define BCE_RV2P_TFTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_RV2P_TFTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_RV2P_TFTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_RV2P_TFTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_RV2P_TFTQ_CMD_ADD_INTERVEN			 (1L<<27)
#define BCE_RV2P_TFTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_RV2P_TFTQ_CMD_INTERVENE_CLR		 (1L<<29)
#define BCE_RV2P_TFTQ_CMD_POP				 (1L<<30)
#define BCE_RV2P_TFTQ_CMD_BUSY				 (1L<<31)

#define BCE_RV2P_TFTQ_CTL				0x00002bbc
#define BCE_RV2P_TFTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_RV2P_TFTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_RV2P_TFTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_RV2P_TFTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_RV2P_TFTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)

#define BCE_RV2P_MFTQ_DATA				0x00002bc0
#define BCE_RV2P_MFTQ_CMD				0x00002bf8
#define BCE_RV2P_MFTQ_CMD_OFFSET			 (0x3ffL<<0)
#define BCE_RV2P_MFTQ_CMD_WR_TOP			 (1L<<10)
#define BCE_RV2P_MFTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_RV2P_MFTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_RV2P_MFTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_RV2P_MFTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_RV2P_MFTQ_CMD_ADD_INTERVEN			 (1L<<27)
#define BCE_RV2P_MFTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_RV2P_MFTQ_CMD_INTERVENE_CLR		 (1L<<29)
#define BCE_RV2P_MFTQ_CMD_POP				 (1L<<30)
#define BCE_RV2P_MFTQ_CMD_BUSY				 (1L<<31)

#define BCE_RV2P_MFTQ_CTL				0x00002bfc
#define BCE_RV2P_MFTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_RV2P_MFTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_RV2P_MFTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_RV2P_MFTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_RV2P_MFTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)


/*
 *  mq_reg definition
 *  offset: 0x3c00
 */
#define BCE_MQ_COMMAND								0x00003c00
#define BCE_MQ_COMMAND_ENABLED						(1L<<0)
#define BCE_MQ_COMMAND_INIT							(1L<<1)
#define BCE_MQ_COMMAND_OVERFLOW						(1L<<4)
#define BCE_MQ_COMMAND_WR_ERROR						(1L<<5)
#define BCE_MQ_COMMAND_RD_ERROR						(1L<<6)
#define BCE_MQ_COMMAND_IDB_CFG_ERROR				(1L<<7)
#define BCE_MQ_COMMAND_IDB_OVERFLOW					(1L<<10)
#define BCE_MQ_COMMAND_NO_BIN_ERROR					(1L<<11)
#define BCE_MQ_COMMAND_NO_MAP_ERROR					(1L<<12)

#define BCE_MQ_STATUS								0x00003c04
#define BCE_MQ_STATUS_CTX_ACCESS_STAT				(1L<<16)
#define BCE_MQ_STATUS_CTX_ACCESS64_STAT				(1L<<17)
#define BCE_MQ_STATUS_PCI_STALL_STAT				(1L<<18)
#define BCE_MQ_STATUS_IDB_OFLOW_STAT				(1L<<19)

#define BCE_MQ_CONFIG								0x00003c08
#define BCE_MQ_CONFIG_TX_HIGH_PRI					(1L<<0)
#define BCE_MQ_CONFIG_HALT_DIS						(1L<<1)
#define BCE_MQ_CONFIG_BIN_MQ_MODE					(1L<<2)
#define BCE_MQ_CONFIG_DIS_IDB_DROP					(1L<<3)
#define BCE_MQ_CONFIG_KNL_BYP_BLK_SIZE				(0x7L<<4)
#define BCE_MQ_CONFIG_KNL_BYP_BLK_SIZE_256			(0L<<4)
#define BCE_MQ_CONFIG_KNL_BYP_BLK_SIZE_512			(1L<<4)
#define BCE_MQ_CONFIG_KNL_BYP_BLK_SIZE_1K			(2L<<4)
#define BCE_MQ_CONFIG_KNL_BYP_BLK_SIZE_2K			(3L<<4)
#define BCE_MQ_CONFIG_KNL_BYP_BLK_SIZE_4K			(4L<<4)
#define BCE_MQ_CONFIG_MAX_DEPTH						(0x7fL<<8)
#define BCE_MQ_CONFIG_CUR_DEPTH						(0x7fL<<20)

#define BCE_MQ_ENQUEUE1								0x00003c0c
#define BCE_MQ_ENQUEUE1_OFFSET						(0x3fL<<2)
#define BCE_MQ_ENQUEUE1_CID							(0x3fffL<<8)
#define BCE_MQ_ENQUEUE1_BYTE_MASK					(0xfL<<24)
#define BCE_MQ_ENQUEUE1_KNL_MODE					(1L<<28)

#define BCE_MQ_ENQUEUE2								0x00003c10
#define BCE_MQ_BAD_WR_ADDR							0x00003c14
#define BCE_MQ_BAD_RD_ADDR							0x00003c18
#define BCE_MQ_KNL_BYP_WIND_START					0x00003c1c
#define BCE_MQ_KNL_BYP_WIND_START_VALUE				(0xfffffL<<12)

#define BCE_MQ_KNL_WIND_END							0x00003c20
#define BCE_MQ_KNL_WIND_END_VALUE					(0xffffffL<<8)

#define BCE_MQ_KNL_WRITE_MASK1						0x00003c24
#define BCE_MQ_KNL_TX_MASK1							0x00003c28
#define BCE_MQ_KNL_CMD_MASK1						0x00003c2c
#define BCE_MQ_KNL_COND_ENQUEUE_MASK1				0x00003c30
#define BCE_MQ_KNL_RX_V2P_MASK1						0x00003c34
#define BCE_MQ_KNL_WRITE_MASK2						0x00003c38
#define BCE_MQ_KNL_TX_MASK2							0x00003c3c
#define BCE_MQ_KNL_CMD_MASK2						0x00003c40
#define BCE_MQ_KNL_COND_ENQUEUE_MASK2				0x00003c44
#define BCE_MQ_KNL_RX_V2P_MASK2						0x00003c48
#define BCE_MQ_KNL_BYP_WRITE_MASK1					0x00003c4c
#define BCE_MQ_KNL_BYP_TX_MASK1						0x00003c50
#define BCE_MQ_KNL_BYP_CMD_MASK1					0x00003c54
#define BCE_MQ_KNL_BYP_COND_ENQUEUE_MASK1			0x00003c58
#define BCE_MQ_KNL_BYP_RX_V2P_MASK1					0x00003c5c
#define BCE_MQ_KNL_BYP_WRITE_MASK2					0x00003c60
#define BCE_MQ_KNL_BYP_TX_MASK2						0x00003c64
#define BCE_MQ_KNL_BYP_CMD_MASK2					0x00003c68
#define BCE_MQ_KNL_BYP_COND_ENQUEUE_MASK2			0x00003c6c
#define BCE_MQ_KNL_BYP_RX_V2P_MASK2					0x00003c70
#define BCE_MQ_MEM_WR_ADDR							0x00003c74
#define BCE_MQ_MEM_WR_ADDR_VALUE					(0x3fL<<0)

#define BCE_MQ_MEM_WR_DATA0							0x00003c78
#define BCE_MQ_MEM_WR_DATA0_VALUE					(0xffffffffL<<0)

#define BCE_MQ_MEM_WR_DATA1							0x00003c7c
#define BCE_MQ_MEM_WR_DATA1_VALUE					(0xffffffffL<<0)

#define BCE_MQ_MEM_WR_DATA2							0x00003c80
#define BCE_MQ_MEM_WR_DATA2_VALUE					(0x3fffffffL<<0)
#define BCE_MQ_MEM_WR_DATA2_VALUE_XI				(0x7fffffffL<<0)

#define BCE_MQ_MEM_RD_ADDR							0x00003c84
#define BCE_MQ_MEM_RD_ADDR_VALUE					(0x3fL<<0)

#define BCE_MQ_MEM_RD_DATA0							0x00003c88
#define BCE_MQ_MEM_RD_DATA0_VALUE					(0xffffffffL<<0)

#define BCE_MQ_MEM_RD_DATA1							0x00003c8c
#define BCE_MQ_MEM_RD_DATA1_VALUE					(0xffffffffL<<0)

#define BCE_MQ_MEM_RD_DATA2							0x00003c90
#define BCE_MQ_MEM_RD_DATA2_VALUE					(0x3fffffffL<<0)
#define BCE_MQ_MEM_RD_DATA2_VALUE_XI				(0x7fffffffL<<0)

#define BCE_MQ_CONFIG2								0x00003d00
#define BCE_MQ_CONFIG2_CONT_SZ						(0x7L<<4)
#define BCE_MQ_CONFIG2_FIRST_L4L5					(0x1fL<<8)

#define BCE_MQ_MAP_L2_3								0x00003d2c
#define BCE_MQ_MAP_L2_3_MQ_OFFSET					(0xffL<<0)
#define BCE_MQ_MAP_L2_3_SZ							(0x3L<<8)
#define BCE_MQ_MAP_L2_3_CTX_OFFSET					(0x2ffL<<10)
#define BCE_MQ_MAP_L2_3_BIN_OFFSET					(0x7L<<23)
#define BCE_MQ_MAP_L2_3_ARM							(0x3L<<26)
#define BCE_MQ_MAP_L2_3_ENA							(0x1L<<31)
#define BCE_MQ_MAP_L2_3_DEFAULT						0x82004646

#define BCE_MQ_MAP_L2_5								0x00003d34
#define BCE_MQ_MAP_L2_5_MQ_OFFSET					(0xffL<<0)
#define BCE_MQ_MAP_L2_5_SZ							(0x3L<<8)
#define BCE_MQ_MAP_L2_5_CTX_OFFSET					(0x2ffL<<10)
#define BCE_MQ_MAP_L2_5_BIN_OFFSET					(0x7L<<23)
#define BCE_MQ_MAP_L2_5_ARM							(0x3L<<26)
#define BCE_MQ_MAP_L2_5_ENA							(0x1L<<31)
#define BCE_MQ_MAP_L2_5_DEFAULT						0x83000b08


/*
 *  csch_reg definition
 *  offset: 0x4000
 */
#define BCE_CSCH_COMMAND				0x00004000
#define BCE_CSCH_CH_FTQ_CMD				0x000043f8
#define BCE_CSCH_CH_FTQ_CTL				0x000043fc
#define BCE_CSCH_CH_FTQ_CTL_MAX_DEPTH			(0x3ffL<<12)
#define BCE_CSCH_CH_FTQ_CTL_CUR_DEPTH			(0x3ffL<<22)


/*
 *  tbdr_reg definition
 *  offset: 0x5000
 */
#define BCE_TBDR_COMMAND				0x00005000
#define BCE_TBDR_COMMAND_ENABLE				(1L<<0)
#define BCE_TBDR_COMMAND_SOFT_RST			(1L<<1)
#define BCE_TBDR_COMMAND_MSTR_ABORT			(1L<<4)

#define BCE_TBDR_STATUS					0x00005004
#define BCE_TBDR_STATUS_DMA_WAIT			(1L<<0)
#define BCE_TBDR_STATUS_FTQ_WAIT			(1L<<1)
#define BCE_TBDR_STATUS_FIFO_OVERFLOW			(1L<<2)
#define BCE_TBDR_STATUS_FIFO_UNDERFLOW			(1L<<3)
#define BCE_TBDR_STATUS_SEARCHMISS_ERROR		(1L<<4)
#define BCE_TBDR_STATUS_FTQ_ENTRY_CNT			(1L<<5)
#define BCE_TBDR_STATUS_BURST_CNT			(1L<<6)

#define BCE_TBDR_CONFIG					0x00005008
#define BCE_TBDR_CONFIG_MAX_BDS				(0xffL<<0)
#define BCE_TBDR_CONFIG_SWAP_MODE			(1L<<8)
#define BCE_TBDR_CONFIG_PRIORITY			(1L<<9)
#define BCE_TBDR_CONFIG_CACHE_NEXT_PAGE_PTRS		(1L<<10)
#define BCE_TBDR_CONFIG_PAGE_SIZE			(0xfL<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_256			(0L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_512			(1L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_1K			(2L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_2K			(3L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_4K			(4L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_8K			(5L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_16K			(6L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_32K			(7L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_64K			(8L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_128K			(9L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_256K			(10L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_512K			(11L<<24)
#define BCE_TBDR_CONFIG_PAGE_SIZE_1M			(12L<<24)

#define BCE_TBDR_DEBUG_VECT_PEEK			0x0000500c
#define BCE_TBDR_DEBUG_VECT_PEEK_1_VALUE		(0x7ffL<<0)
#define BCE_TBDR_DEBUG_VECT_PEEK_1_PEEK_EN		(1L<<11)
#define BCE_TBDR_DEBUG_VECT_PEEK_1_SEL			(0xfL<<12)
#define BCE_TBDR_DEBUG_VECT_PEEK_2_VALUE		(0x7ffL<<16)
#define BCE_TBDR_DEBUG_VECT_PEEK_2_PEEK_EN		(1L<<27)
#define BCE_TBDR_DEBUG_VECT_PEEK_2_SEL			(0xfL<<28)

#define BCE_TBDR_FTQ_DATA				0x000053c0
#define BCE_TBDR_FTQ_CMD				0x000053f8
#define BCE_TBDR_FTQ_CMD_OFFSET				(0x3ffL<<0)
#define BCE_TBDR_FTQ_CMD_WR_TOP				(1L<<10)
#define BCE_TBDR_FTQ_CMD_WR_TOP_0			(0L<<10)
#define BCE_TBDR_FTQ_CMD_WR_TOP_1			(1L<<10)
#define BCE_TBDR_FTQ_CMD_SFT_RESET			(1L<<25)
#define BCE_TBDR_FTQ_CMD_RD_DATA			(1L<<26)
#define BCE_TBDR_FTQ_CMD_ADD_INTERVEN			(1L<<27)
#define BCE_TBDR_FTQ_CMD_ADD_DATA			(1L<<28)
#define BCE_TBDR_FTQ_CMD_INTERVENE_CLR			(1L<<29)
#define BCE_TBDR_FTQ_CMD_POP				(1L<<30)
#define BCE_TBDR_FTQ_CMD_BUSY				(1L<<31)

#define BCE_TBDR_FTQ_CTL				0x000053fc
#define BCE_TBDR_FTQ_CTL_INTERVENE			(1L<<0)
#define BCE_TBDR_FTQ_CTL_OVERFLOW			(1L<<1)
#define BCE_TBDR_FTQ_CTL_FORCE_INTERVENE		(1L<<2)
#define BCE_TBDR_FTQ_CTL_MAX_DEPTH			(0x3ffL<<12)
#define BCE_TBDR_FTQ_CTL_CUR_DEPTH			(0x3ffL<<22)


/*
 *  tdma_reg definition
 *  offset: 0x5c00
 */
#define BCE_TDMA_COMMAND				0x00005c00
#define BCE_TDMA_COMMAND_ENABLED			 (1L<<0)
#define BCE_TDMA_COMMAND_MASTER_ABORT			 (1L<<4)
#define BCE_TDMA_COMMAND_BAD_L2_LENGTH_ABORT		 (1L<<7)

#define BCE_TDMA_STATUS					0x00005c04
#define BCE_TDMA_STATUS_DMA_WAIT			 (1L<<0)
#define BCE_TDMA_STATUS_PAYLOAD_WAIT			 (1L<<1)
#define BCE_TDMA_STATUS_PATCH_FTQ_WAIT			 (1L<<2)
#define BCE_TDMA_STATUS_LOCK_WAIT			 (1L<<3)
#define BCE_TDMA_STATUS_FTQ_ENTRY_CNT			 (1L<<16)
#define BCE_TDMA_STATUS_BURST_CNT			 (1L<<17)

#define BCE_TDMA_CONFIG					0x00005c08
#define BCE_TDMA_CONFIG_ONE_DMA				 (1L<<0)
#define BCE_TDMA_CONFIG_ONE_RECORD			 (1L<<1)
#define BCE_TDMA_CONFIG_LIMIT_SZ			 (0xfL<<4)
#define BCE_TDMA_CONFIG_LIMIT_SZ_64			 (0L<<4)
#define BCE_TDMA_CONFIG_LIMIT_SZ_128			 (0x4L<<4)
#define BCE_TDMA_CONFIG_LIMIT_SZ_256			 (0x6L<<4)
#define BCE_TDMA_CONFIG_LIMIT_SZ_512			 (0x8L<<4)
#define BCE_TDMA_CONFIG_LINE_SZ				 (0xfL<<8)
#define BCE_TDMA_CONFIG_LINE_SZ_64			 (0L<<8)
#define BCE_TDMA_CONFIG_LINE_SZ_128			 (4L<<8)
#define BCE_TDMA_CONFIG_LINE_SZ_256			 (6L<<8)
#define BCE_TDMA_CONFIG_LINE_SZ_512			 (8L<<8)
#define BCE_TDMA_CONFIG_ALIGN_ENA			 (1L<<15)
#define BCE_TDMA_CONFIG_CHK_L2_BD			 (1L<<16)
#define BCE_TDMA_CONFIG_FIFO_CMP			 (0xfL<<20)

#define BCE_TDMA_PAYLOAD_PROD				0x00005c0c
#define BCE_TDMA_PAYLOAD_PROD_VALUE			 (0x1fffL<<3)

#define BCE_TDMA_DBG_WATCHDOG				0x00005c10
#define BCE_TDMA_DBG_TRIGGER				0x00005c14
#define BCE_TDMA_DMAD_FSM				0x00005c80
#define BCE_TDMA_DMAD_FSM_BD_INVLD			 (1L<<0)
#define BCE_TDMA_DMAD_FSM_PUSH				 (0xfL<<4)
#define BCE_TDMA_DMAD_FSM_ARB_TBDC			 (0x3L<<8)
#define BCE_TDMA_DMAD_FSM_ARB_CTX			 (1L<<12)
#define BCE_TDMA_DMAD_FSM_DR_INTF			 (1L<<16)
#define BCE_TDMA_DMAD_FSM_DMAD				 (0x7L<<20)
#define BCE_TDMA_DMAD_FSM_BD				 (0xfL<<24)

#define BCE_TDMA_DMAD_STATUS				0x00005c84
#define BCE_TDMA_DMAD_STATUS_RHOLD_PUSH_ENTRY		 (0x3L<<0)
#define BCE_TDMA_DMAD_STATUS_RHOLD_DMAD_ENTRY		 (0x3L<<4)
#define BCE_TDMA_DMAD_STATUS_RHOLD_BD_ENTRY		 (0x3L<<8)
#define BCE_TDMA_DMAD_STATUS_IFTQ_ENUM			 (0xfL<<12)

#define BCE_TDMA_DR_INTF_FSM				0x00005c88
#define BCE_TDMA_DR_INTF_FSM_L2_COMP			 (0x3L<<0)
#define BCE_TDMA_DR_INTF_FSM_TPATQ			 (0x7L<<4)
#define BCE_TDMA_DR_INTF_FSM_TPBUF			 (0x3L<<8)
#define BCE_TDMA_DR_INTF_FSM_DR_BUF			 (0x7L<<12)
#define BCE_TDMA_DR_INTF_FSM_DMAD			 (0x7L<<16)

#define BCE_TDMA_DR_INTF_STATUS				0x00005c8c
#define BCE_TDMA_DR_INTF_STATUS_HOLE_PHASE		 (0x7L<<0)
#define BCE_TDMA_DR_INTF_STATUS_DATA_AVAIL		 (0x3L<<4)
#define BCE_TDMA_DR_INTF_STATUS_SHIFT_ADDR		 (0x7L<<8)
#define BCE_TDMA_DR_INTF_STATUS_NXT_PNTR		 (0xfL<<12)
#define BCE_TDMA_DR_INTF_STATUS_BYTE_COUNT		 (0x7L<<16)

#define BCE_TDMA_FTQ_DATA				0x00005fc0
#define BCE_TDMA_FTQ_CMD				0x00005ff8
#define BCE_TDMA_FTQ_CMD_OFFSET				 (0x3ffL<<0)
#define BCE_TDMA_FTQ_CMD_WR_TOP				 (1L<<10)
#define BCE_TDMA_FTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_TDMA_FTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_TDMA_FTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_TDMA_FTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_TDMA_FTQ_CMD_ADD_INTERVEN			 (1L<<27)
#define BCE_TDMA_FTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_TDMA_FTQ_CMD_INTERVENE_CLR			 (1L<<29)
#define BCE_TDMA_FTQ_CMD_POP				 (1L<<30)
#define BCE_TDMA_FTQ_CMD_BUSY				 (1L<<31)

#define BCE_TDMA_FTQ_CTL				0x00005ffc
#define BCE_TDMA_FTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_TDMA_FTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_TDMA_FTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_TDMA_FTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_TDMA_FTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)


/*
 *  nvm_reg definition
 *  offset: 0x6400
 */
#define BCE_NVM_COMMAND					0x00006400
#define BCE_NVM_COMMAND_RST				 (1L<<0)
#define BCE_NVM_COMMAND_DONE				 (1L<<3)
#define BCE_NVM_COMMAND_DOIT				 (1L<<4)
#define BCE_NVM_COMMAND_WR				 (1L<<5)
#define BCE_NVM_COMMAND_ERASE				 (1L<<6)
#define BCE_NVM_COMMAND_FIRST				 (1L<<7)
#define BCE_NVM_COMMAND_LAST				 (1L<<8)
#define BCE_NVM_COMMAND_WREN				 (1L<<16)
#define BCE_NVM_COMMAND_WRDI				 (1L<<17)
#define BCE_NVM_COMMAND_EWSR				 (1L<<18)
#define BCE_NVM_COMMAND_WRSR				 (1L<<19)

#define BCE_NVM_STATUS					0x00006404
#define BCE_NVM_STATUS_PI_FSM_STATE			 (0xfL<<0)
#define BCE_NVM_STATUS_EE_FSM_STATE			 (0xfL<<4)
#define BCE_NVM_STATUS_EQ_FSM_STATE			 (0xfL<<8)

#define BCE_NVM_WRITE					0x00006408
#define BCE_NVM_WRITE_NVM_WRITE_VALUE			 (0xffffffffL<<0)
#define BCE_NVM_WRITE_NVM_WRITE_VALUE_BIT_BANG		 (0L<<0)
#define BCE_NVM_WRITE_NVM_WRITE_VALUE_EECLK		 (1L<<0)
#define BCE_NVM_WRITE_NVM_WRITE_VALUE_EEDATA		 (2L<<0)
#define BCE_NVM_WRITE_NVM_WRITE_VALUE_SCLK		 (4L<<0)
#define BCE_NVM_WRITE_NVM_WRITE_VALUE_CS_B		 (8L<<0)
#define BCE_NVM_WRITE_NVM_WRITE_VALUE_SO		 (16L<<0)
#define BCE_NVM_WRITE_NVM_WRITE_VALUE_SI		 (32L<<0)

#define BCE_NVM_ADDR					0x0000640c
#define BCE_NVM_ADDR_NVM_ADDR_VALUE			 (0xffffffL<<0)
#define BCE_NVM_ADDR_NVM_ADDR_VALUE_BIT_BANG		 (0L<<0)
#define BCE_NVM_ADDR_NVM_ADDR_VALUE_EECLK		 (1L<<0)
#define BCE_NVM_ADDR_NVM_ADDR_VALUE_EEDATA		 (2L<<0)
#define BCE_NVM_ADDR_NVM_ADDR_VALUE_SCLK		 (4L<<0)
#define BCE_NVM_ADDR_NVM_ADDR_VALUE_CS_B		 (8L<<0)
#define BCE_NVM_ADDR_NVM_ADDR_VALUE_SO			 (16L<<0)
#define BCE_NVM_ADDR_NVM_ADDR_VALUE_SI			 (32L<<0)

#define BCE_NVM_READ					0x00006410
#define BCE_NVM_READ_NVM_READ_VALUE			 (0xffffffffL<<0)
#define BCE_NVM_READ_NVM_READ_VALUE_BIT_BANG		 (0L<<0)
#define BCE_NVM_READ_NVM_READ_VALUE_EECLK		 (1L<<0)
#define BCE_NVM_READ_NVM_READ_VALUE_EEDATA		 (2L<<0)
#define BCE_NVM_READ_NVM_READ_VALUE_SCLK		 (4L<<0)
#define BCE_NVM_READ_NVM_READ_VALUE_CS_B		 (8L<<0)
#define BCE_NVM_READ_NVM_READ_VALUE_SO			 (16L<<0)
#define BCE_NVM_READ_NVM_READ_VALUE_SI			 (32L<<0)

#define BCE_NVM_CFG1					0x00006414
#define BCE_NVM_CFG1_FLASH_MODE				 (1L<<0)
#define BCE_NVM_CFG1_BUFFER_MODE			 (1L<<1)
#define BCE_NVM_CFG1_PASS_MODE				 (1L<<2)
#define BCE_NVM_CFG1_BITBANG_MODE			 (1L<<3)
#define BCE_NVM_CFG1_STATUS_BIT				 (0x7L<<4)
#define BCE_NVM_CFG1_STATUS_BIT_FLASH_RDY		 (0L<<4)
#define BCE_NVM_CFG1_STATUS_BIT_BUFFER_RDY		 (7L<<4)
#define BCE_NVM_CFG1_SPI_CLK_DIV			 (0xfL<<7)
#define BCE_NVM_CFG1_SEE_CLK_DIV			 (0x7ffL<<11)
#define BCE_NVM_CFG1_PROTECT_MODE			 (1L<<24)
#define BCE_NVM_CFG1_FLASH_SIZE				 (1L<<25)
#define BCE_NVM_CFG1_COMPAT_BYPASSS			 (1L<<31)

#define BCE_NVM_CFG2					0x00006418
#define BCE_NVM_CFG2_ERASE_CMD				 (0xffL<<0)
#define BCE_NVM_CFG2_DUMMY				 (0xffL<<8)
#define BCE_NVM_CFG2_STATUS_CMD				 (0xffL<<16)

#define BCE_NVM_CFG3					0x0000641c
#define BCE_NVM_CFG3_BUFFER_RD_CMD			 (0xffL<<0)
#define BCE_NVM_CFG3_WRITE_CMD				 (0xffL<<8)
#define BCE_NVM_CFG3_BUFFER_WRITE_CMD			 (0xffL<<16)
#define BCE_NVM_CFG3_READ_CMD				 (0xffL<<24)

#define BCE_NVM_SW_ARB					0x00006420
#define BCE_NVM_SW_ARB_ARB_REQ_SET0			 (1L<<0)
#define BCE_NVM_SW_ARB_ARB_REQ_SET1			 (1L<<1)
#define BCE_NVM_SW_ARB_ARB_REQ_SET2			 (1L<<2)
#define BCE_NVM_SW_ARB_ARB_REQ_SET3			 (1L<<3)
#define BCE_NVM_SW_ARB_ARB_REQ_CLR0			 (1L<<4)
#define BCE_NVM_SW_ARB_ARB_REQ_CLR1			 (1L<<5)
#define BCE_NVM_SW_ARB_ARB_REQ_CLR2			 (1L<<6)
#define BCE_NVM_SW_ARB_ARB_REQ_CLR3			 (1L<<7)
#define BCE_NVM_SW_ARB_ARB_ARB0				 (1L<<8)
#define BCE_NVM_SW_ARB_ARB_ARB1				 (1L<<9)
#define BCE_NVM_SW_ARB_ARB_ARB2				 (1L<<10)
#define BCE_NVM_SW_ARB_ARB_ARB3				 (1L<<11)
#define BCE_NVM_SW_ARB_REQ0				 (1L<<12)
#define BCE_NVM_SW_ARB_REQ1				 (1L<<13)
#define BCE_NVM_SW_ARB_REQ2				 (1L<<14)
#define BCE_NVM_SW_ARB_REQ3				 (1L<<15)

#define BCE_NVM_ACCESS_ENABLE				0x00006424
#define BCE_NVM_ACCESS_ENABLE_EN			 (1L<<0)
#define BCE_NVM_ACCESS_ENABLE_WR_EN			 (1L<<1)

#define BCE_NVM_WRITE1					0x00006428
#define BCE_NVM_WRITE1_WREN_CMD				 (0xffL<<0)
#define BCE_NVM_WRITE1_WRDI_CMD				 (0xffL<<8)
#define BCE_NVM_WRITE1_SR_DATA				 (0xffL<<16)


/*
 *  hc_reg definition
 *  offset: 0x6800
 */
#define BCE_HC_COMMAND					0x00006800
#define BCE_HC_COMMAND_ENABLE				 (1L<<0)
#define BCE_HC_COMMAND_SKIP_ABORT			 (1L<<4)
#define BCE_HC_COMMAND_COAL_NOW			 	 (1L<<16)
#define BCE_HC_COMMAND_COAL_NOW_WO_INT			 (1L<<17)
#define BCE_HC_COMMAND_STATS_NOW			 (1L<<18)
#define BCE_HC_COMMAND_FORCE_INT			 (0x3L<<19)
#define BCE_HC_COMMAND_FORCE_INT_NULL			 (0L<<19)
#define BCE_HC_COMMAND_FORCE_INT_HIGH			 (1L<<19)
#define BCE_HC_COMMAND_FORCE_INT_LOW			 (2L<<19)
#define BCE_HC_COMMAND_FORCE_INT_FREE			 (3L<<19)
#define BCE_HC_COMMAND_CLR_STAT_NOW			 (1L<<21)
#define BCE_HC_COMMAND_MAIN_PWR_INT			 (1L<<22)
#define BCE_HC_COMMAND_COAL_ON_NEXT_EVENT		 (1L<<27)

#define BCE_HC_STATUS					0x00006804
#define BCE_HC_STATUS_MASTER_ABORT			 (1L<<0)
#define BCE_HC_STATUS_PARITY_ERROR_STATE		 (1L<<1)
#define BCE_HC_STATUS_PCI_CLK_CNT_STAT			 (1L<<16)
#define BCE_HC_STATUS_CORE_CLK_CNT_STAT			 (1L<<17)
#define BCE_HC_STATUS_NUM_STATUS_BLOCKS_STAT		 (1L<<18)
#define BCE_HC_STATUS_NUM_INT_GEN_STAT			 (1L<<19)
#define BCE_HC_STATUS_NUM_INT_MBOX_WR_STAT		 (1L<<20)
#define BCE_HC_STATUS_CORE_CLKS_TO_HW_INTACK_STAT	 (1L<<23)
#define BCE_HC_STATUS_CORE_CLKS_TO_SW_INTACK_STAT	 (1L<<24)
#define BCE_HC_STATUS_CORE_CLKS_DURING_SW_INTACK_STAT	 (1L<<25)

#define BCE_HC_CONFIG					0x00006808
#define BCE_HC_CONFIG_COLLECT_STATS			 (1L<<0)
#define BCE_HC_CONFIG_RX_TMR_MODE			 (1L<<1)
#define BCE_HC_CONFIG_TX_TMR_MODE			 (1L<<2)
#define BCE_HC_CONFIG_COM_TMR_MODE			 (1L<<3)
#define BCE_HC_CONFIG_CMD_TMR_MODE			 (1L<<4)
#define BCE_HC_CONFIG_STATISTIC_PRIORITY		 (1L<<5)
#define BCE_HC_CONFIG_STATUS_PRIORITY			 (1L<<6)
#define BCE_HC_CONFIG_STAT_MEM_ADDR			 (0xffL<<8)
#define BCE_HC_CONFIG_PER_MODE				 (1L<<16)
#define BCE_HC_CONFIG_ONE_SHOT				 (1L<<17)
#define BCE_HC_CONFIG_USE_INT_PARAM			 (1L<<18)
#define BCE_HC_CONFIG_SET_MASK_AT_RD			 (1L<<19)
#define BCE_HC_CONFIG_PER_COLLECT_LIMIT			 (0xfL<<20)
#define BCE_HC_CONFIG_SB_ADDR_INC			 (0x7L<<24)
#define BCE_HC_CONFIG_SB_ADDR_INC_64B			 (0L<<24)
#define BCE_HC_CONFIG_SB_ADDR_INC_128B			 (1L<<24)
#define BCE_HC_CONFIG_SB_ADDR_INC_256B			 (2L<<24)
#define BCE_HC_CONFIG_SB_ADDR_INC_512B			 (3L<<24)
#define BCE_HC_CONFIG_SB_ADDR_INC_1024B			 (4L<<24)
#define BCE_HC_CONFIG_SB_ADDR_INC_2048B			 (5L<<24)
#define BCE_HC_CONFIG_SB_ADDR_INC_4096B			 (6L<<24)
#define BCE_HC_CONFIG_SB_ADDR_INC_8192B			 (7L<<24)
#define BCE_HC_CONFIG_GEN_STAT_AVG_INTR			 (1L<<29)
#define BCE_HC_CONFIG_UNMASK_ALL			 (1L<<30)
#define BCE_HC_CONFIG_TX_SEL				 (1L<<31)

#define BCE_HC_ATTN_BITS_ENABLE				0x0000680c
#define BCE_HC_STATUS_ADDR_L				0x00006810
#define BCE_HC_STATUS_ADDR_H				0x00006814
#define BCE_HC_STATISTICS_ADDR_L			0x00006818
#define BCE_HC_STATISTICS_ADDR_H			0x0000681c
#define BCE_HC_TX_QUICK_CONS_TRIP			0x00006820
#define BCE_HC_TX_QUICK_CONS_TRIP_VALUE			 (0xffL<<0)
#define BCE_HC_TX_QUICK_CONS_TRIP_INT			 (0xffL<<16)

#define BCE_HC_COMP_PROD_TRIP				0x00006824
#define BCE_HC_COMP_PROD_TRIP_VALUE			 (0xffL<<0)
#define BCE_HC_COMP_PROD_TRIP_INT			 (0xffL<<16)

#define BCE_HC_RX_QUICK_CONS_TRIP			0x00006828
#define BCE_HC_RX_QUICK_CONS_TRIP_VALUE			 (0xffL<<0)
#define BCE_HC_RX_QUICK_CONS_TRIP_INT			 (0xffL<<16)

#define BCE_HC_RX_TICKS					0x0000682c
#define BCE_HC_RX_TICKS_VALUE				 (0x3ffL<<0)
#define BCE_HC_RX_TICKS_INT				 (0x3ffL<<16)

#define BCE_HC_TX_TICKS					0x00006830
#define BCE_HC_TX_TICKS_VALUE				 (0x3ffL<<0)
#define BCE_HC_TX_TICKS_INT				 (0x3ffL<<16)

#define BCE_HC_COM_TICKS				0x00006834
#define BCE_HC_COM_TICKS_VALUE				 (0x3ffL<<0)
#define BCE_HC_COM_TICKS_INT				 (0x3ffL<<16)

#define BCE_HC_CMD_TICKS				0x00006838
#define BCE_HC_CMD_TICKS_VALUE				 (0x3ffL<<0)
#define BCE_HC_CMD_TICKS_INT				 (0x3ffL<<16)

#define BCE_HC_PERIODIC_TICKS				0x0000683c
#define BCE_HC_PERIODIC_TICKS_HC_PERIODIC_TICKS		 (0xffffL<<0)
#define BCE_HC_PERIODIC_TICKS_HC_INT_PERIODIC_TICKS	 (0xffffL<<16)

#define BCE_HC_STAT_COLLECT_TICKS			0x00006840
#define BCE_HC_STAT_COLLECT_TICKS_HC_STAT_COLL_TICKS	 (0xffL<<4)

#define BCE_HC_STATS_TICKS				0x00006844
#define BCE_HC_STATS_TICKS_HC_STAT_TICKS		 (0xffffL<<8)

#define BCE_HC_STATS_INTERRUPT_STATUS			0x00006848
#define BCE_HC_STATS_INTERRUPT_STATUS_SB_STATUS		 (0x1ffL<<0)
#define BCE_HC_STATS_INTERRUPT_STATUS_INT_STATUS	 (0x1ffL<<16)

#define BCE_HC_STAT_MEM_DATA				0x0000684c
#define BCE_HC_STAT_GEN_SEL_0				0x00006850
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0			 (0x7fL<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT0	 (0L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT1	 (1L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT2	 (2L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT3	 (3L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT4	 (4L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT5	 (5L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT6	 (6L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT7	 (7L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT8	 (8L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT9	 (9L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT10	 (10L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXP_STAT11	 (11L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TXP_STAT0	 (12L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TXP_STAT1	 (13L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TXP_STAT2	 (14L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TXP_STAT3	 (15L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TXP_STAT4	 (16L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TXP_STAT5	 (17L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TXP_STAT6	 (18L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TXP_STAT7	 (19L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT0	 (20L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT1	 (21L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT2	 (22L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT3	 (23L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT4	 (24L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT5	 (25L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT6	 (26L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT7	 (27L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT8	 (28L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT9	 (29L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT10	 (30L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COM_STAT11	 (31L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TPAT_STAT0	 (32L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TPAT_STAT1	 (33L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TPAT_STAT2	 (34L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TPAT_STAT3	 (35L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CP_STAT0	 (36L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CP_STAT1	 (37L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CP_STAT2	 (38L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CP_STAT3	 (39L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CP_STAT4	 (40L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CP_STAT5	 (41L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CP_STAT6	 (42L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CP_STAT7	 (43L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MCP_STAT0	 (44L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MCP_STAT1	 (45L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MCP_STAT2	 (46L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MCP_STAT3	 (47L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MCP_STAT4	 (48L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MCP_STAT5	 (49L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MCP_STAT6	 (50L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MCP_STAT7	 (51L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_PCI_CLK_CNT	 (52L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CORE_CLK_CNT	 (53L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_STATUS_BLOCKS	 (54L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_GEN	 (55L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_MBOX_WR	 (56L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_HW_INTACK	 (59L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_SW_INTACK	 (60L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_DURING_SW_INTACK	 (61L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TSCH_CMD_CNT	 (62L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TSCH_SLOT_CNT	 (63L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CSCH_CMD_CNT	 (64L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CSCH_SLOT_CNT	 (65L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RLUPQ_VALID_CNT	 (66L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXPQ_VALID_CNT	 (67L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RXPCQ_VALID_CNT	 (68L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2PPQ_VALID_CNT (69L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2PMQ_VALID_CNT (70L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2PTQ_VALID_CNT (71L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RDMAQ_VALID_CNT	 (72L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TSCHQ_VALID_CNT	 (73L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TBDRQ_VALID_CNT	 (74L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TXPQ_VALID_CNT	 (75L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TDMAQ_VALID_CNT	 (76L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TPATQ_VALID_CNT	 (77L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TASQ_VALID_CNT	 (78L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CSQ_VALID_CNT	 (79L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CPQ_VALID_CNT	 (80L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COMXQ_VALID_CNT	 (81L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COMTQ_VALID_CNT	 (82L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_COMQ_VALID_CNT	 (83L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MGMQ_VALID_CNT	 (84L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_DMAE_READ_TRANSFERS_CNT	 (85L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_DMAE_READ_DELAY_PCI_CLKS_CNT	 (86L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_DMAE_BIG_READ_TRANSFERS_CNT	 (87L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_DMAE_BIG_READ_DELAY_PCI_CLKS_CNT	 (88L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_DMAE_BIG_READ_RETRY_AFTER_DATA_CNT	 (89L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_DMAE_WRITE_TRANSFERS_CNT	 (90L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_DMAE_WRITE_DELAY_PCI_CLKS_CNT	 (91L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_DMAE_BIG_WRITE_TRANSFERS_CNT	 (92L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_DMAE_BIG_WRITE_DELAY_PCI_CLKS_CNT	 (93L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_DMAE_BIG_WRITE_RETRY_AFTER_DATA_CNT	 (94L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CTX_WR_CNT64	 (95L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CTX_RD_CNT64	 (96L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CTX_ACC_STALL_CLKS	 (97L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CTX_LOCK_STALL_CLKS	 (98L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MBQ_CTX_ACCESS_STAT	 (99L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MBQ_CTX_ACCESS64_STAT	 (100L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MBQ_PCI_STALL_STAT	 (101L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TBDR_FTQ_ENTRY_CNT	 (102L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TBDR_BURST_CNT	 (103L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TDMA_FTQ_ENTRY_CNT	 (104L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TDMA_BURST_CNT	 (105L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RDMA_FTQ_ENTRY_CNT	 (106L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RDMA_BURST_CNT	 (107L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RLUP_MATCH_CNT	 (108L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TMR_POLL_PASS_CNT	 (109L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TMR_TMR1_CNT	 (110L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TMR_TMR2_CNT	 (111L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TMR_TMR3_CNT	 (112L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TMR_TMR4_CNT	 (113L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_TMR_TMR5_CNT	 (114L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2P_STAT0	 (115L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2P_STAT1	 (116L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2P_STAT2	 (117L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2P_STAT3	 (118L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2P_STAT4	 (119L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2P_STAT5	 (120L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RBDC_PROC1_MISS	 (121L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RBDC_PROC2_MISS	 (122L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RBDC_BURST_CNT	 (127L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_1		 (0x7fL<<8)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_2		 (0x7fL<<16)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_3		 (0x7fL<<24)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_XI		 (0xffL<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UMP_RX_FRAME_DROP_XI	 (52L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S0_XI	 (57L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S1_XI	 (58L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S2_XI	 (85L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S3_XI	 (86L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S4_XI	 (87L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S5_XI	 (88L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S6_XI	 (89L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S7_XI	 (90L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S8_XI	 (91L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S9_XI	 (92L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_UNUSED_S10_XI	 (93L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_MQ_IDB_OFLOW_XI	 (94L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CTX_BLK_RD_CNT_XI	 (123L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CTX_BLK_WR_CNT_XI	 (124L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CTX_HITS_XI	 (125L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_CTX_MISSES_XI	 (126L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_STATUS_BLOCKS_VEC1_XI	 (128L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_GEN_VEC1_XI	 (129L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_MBOX_WR_VEC1_XI	 (130L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_HW_INTACK_VEC1_XI	 (131L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_SW_INTACK_VEC1_XI	 (132L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_DURING_SW_INTACK_VEC1_XI	 (133L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_STATUS_BLOCKS_VEC2_XI	 (134L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_GEN_VEC2_XI	 (135L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_MBOX_WR_VEC2_XI	 (136L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_HW_INTACK_VEC2_XI	 (137L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_SW_INTACK_VEC2_XI	 (138L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_DURING_SW_INTACK_VEC2_XI	 (139L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_STATUS_BLOCKS_VEC3_XI	 (140L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_GEN_VEC3_XI	 (141L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_MBOX_WR_VEC3_XI	 (142L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_HW_INTACK_VEC3_XI	 (143L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_SW_INTACK_VEC3_XI	 (144L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_DURING_SW_INTACK_VEC3_XI	 (145L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_STATUS_BLOCKS_VEC4_XI	 (146L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_GEN_VEC4_XI	 (147L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_MBOX_WR_VEC4_XI	 (148L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_HW_INTACK_VEC4_XI	 (149L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_SW_INTACK_VEC4_XI	 (150L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_DURING_SW_INTACK_VEC4_XI	 (151L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_STATUS_BLOCKS_VEC5_XI	 (152L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_GEN_VEC5_XI	 (153L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_MBOX_WR_VEC5_XI	 (154L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_HW_INTACK_VEC5_XI	 (155L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_SW_INTACK_VEC5_XI	 (156L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_DURING_SW_INTACK_VEC5_XI	 (157L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_STATUS_BLOCKS_VEC6_XI	 (158L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_GEN_VEC6_XI	 (159L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_MBOX_WR_VEC6_XI	 (160L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_HW_INTACK_VEC6_XI	 (161L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_SW_INTACK_VEC6_XI	 (162L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_DURING_SW_INTACK_VEC6_XI	 (163L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_STATUS_BLOCKS_VEC7_XI	 (164L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_GEN_VEC7_XI	 (165L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_MBOX_WR_VEC7_XI	 (166L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_HW_INTACK_VEC7_XI	 (167L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_SW_INTACK_VEC7_XI	 (168L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_DURING_SW_INTACK_VEC7_XI	 (169L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_STATUS_BLOCKS_VEC8_XI	 (170L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_GEN_VEC8_XI	 (171L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_NUM_INT_MBOX_WR_VEC8_XI	 (172L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_HW_INTACK_VEC8_XI	 (173L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_TO_SW_INTACK_VEC8_XI	 (174L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_HC_CORE_CLKS_DURING_SW_INTACK_VEC8_XI	 (175L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2PCS_CMD_CNT_XI	 (176L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2PCS_SLOT_CNT_XI	 (177L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_0_RV2PCSQ_VALID_CNT_XI	 (178L<<0)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_1_XI		 (0xffL<<8)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_2_XI		 (0xffL<<16)
#define BCE_HC_STAT_GEN_SEL_0_GEN_SEL_3_XI		 (0xffL<<24)

#define BCE_HC_STAT_GEN_SEL_1				0x00006854
#define BCE_HC_STAT_GEN_SEL_1_GEN_SEL_4		 (0x7fL<<0)
#define BCE_HC_STAT_GEN_SEL_1_GEN_SEL_5		 (0x7fL<<8)
#define BCE_HC_STAT_GEN_SEL_1_GEN_SEL_6		 (0x7fL<<16)
#define BCE_HC_STAT_GEN_SEL_1_GEN_SEL_7		 (0x7fL<<24)
#define BCE_HC_STAT_GEN_SEL_1_GEN_SEL_4_XI		 (0xffL<<0)
#define BCE_HC_STAT_GEN_SEL_1_GEN_SEL_5_XI		 (0xffL<<8)
#define BCE_HC_STAT_GEN_SEL_1_GEN_SEL_6_XI		 (0xffL<<16)
#define BCE_HC_STAT_GEN_SEL_1_GEN_SEL_7_XI		 (0xffL<<24)

#define BCE_HC_STAT_GEN_SEL_2				0x00006858
#define BCE_HC_STAT_GEN_SEL_2_GEN_SEL_8		 (0x7fL<<0)
#define BCE_HC_STAT_GEN_SEL_2_GEN_SEL_9		 (0x7fL<<8)
#define BCE_HC_STAT_GEN_SEL_2_GEN_SEL_10		 (0x7fL<<16)
#define BCE_HC_STAT_GEN_SEL_2_GEN_SEL_11		 (0x7fL<<24)
#define BCE_HC_STAT_GEN_SEL_2_GEN_SEL_8_XI		 (0xffL<<0)
#define BCE_HC_STAT_GEN_SEL_2_GEN_SEL_9_XI		 (0xffL<<8)
#define BCE_HC_STAT_GEN_SEL_2_GEN_SEL_10_XI		 (0xffL<<16)
#define BCE_HC_STAT_GEN_SEL_2_GEN_SEL_11_XI		 (0xffL<<24)

#define BCE_HC_STAT_GEN_SEL_3				0x0000685c
#define BCE_HC_STAT_GEN_SEL_3_GEN_SEL_12		 (0x7fL<<0)
#define BCE_HC_STAT_GEN_SEL_3_GEN_SEL_13		 (0x7fL<<8)
#define BCE_HC_STAT_GEN_SEL_3_GEN_SEL_14		 (0x7fL<<16)
#define BCE_HC_STAT_GEN_SEL_3_GEN_SEL_15		 (0x7fL<<24)
#define BCE_HC_STAT_GEN_SEL_3_GEN_SEL_12_XI		 (0xffL<<0)
#define BCE_HC_STAT_GEN_SEL_3_GEN_SEL_13_XI		 (0xffL<<8)
#define BCE_HC_STAT_GEN_SEL_3_GEN_SEL_14_XI		 (0xffL<<16)
#define BCE_HC_STAT_GEN_SEL_3_GEN_SEL_15_XI		 (0xffL<<24)

#define BCE_HC_STAT_GEN_STAT0				0x00006888
#define BCE_HC_STAT_GEN_STAT1				0x0000688c
#define BCE_HC_STAT_GEN_STAT2				0x00006890
#define BCE_HC_STAT_GEN_STAT3				0x00006894
#define BCE_HC_STAT_GEN_STAT4				0x00006898
#define BCE_HC_STAT_GEN_STAT5				0x0000689c
#define BCE_HC_STAT_GEN_STAT6				0x000068a0
#define BCE_HC_STAT_GEN_STAT7				0x000068a4
#define BCE_HC_STAT_GEN_STAT8				0x000068a8
#define BCE_HC_STAT_GEN_STAT9				0x000068ac
#define BCE_HC_STAT_GEN_STAT10				0x000068b0
#define BCE_HC_STAT_GEN_STAT11				0x000068b4
#define BCE_HC_STAT_GEN_STAT12				0x000068b8
#define BCE_HC_STAT_GEN_STAT13				0x000068bc
#define BCE_HC_STAT_GEN_STAT14				0x000068c0
#define BCE_HC_STAT_GEN_STAT15				0x000068c4
#define BCE_HC_STAT_GEN_STAT_AC0			0x000068c8
#define BCE_HC_STAT_GEN_STAT_AC1			0x000068cc
#define BCE_HC_STAT_GEN_STAT_AC2			0x000068d0
#define BCE_HC_STAT_GEN_STAT_AC3			0x000068d4
#define BCE_HC_STAT_GEN_STAT_AC4			0x000068d8
#define BCE_HC_STAT_GEN_STAT_AC5			0x000068dc
#define BCE_HC_STAT_GEN_STAT_AC6			0x000068e0
#define BCE_HC_STAT_GEN_STAT_AC7			0x000068e4
#define BCE_HC_STAT_GEN_STAT_AC8			0x000068e8
#define BCE_HC_STAT_GEN_STAT_AC9			0x000068ec
#define BCE_HC_STAT_GEN_STAT_AC10			0x000068f0
#define BCE_HC_STAT_GEN_STAT_AC11			0x000068f4
#define BCE_HC_STAT_GEN_STAT_AC12			0x000068f8
#define BCE_HC_STAT_GEN_STAT_AC13			0x000068fc
#define BCE_HC_STAT_GEN_STAT_AC14			0x00006900
#define BCE_HC_STAT_GEN_STAT_AC15			0x00006904
#define BCE_HC_STAT_GEN_STAT_AC			0x000068c8
#define BCE_HC_VIS					0x00006908
#define BCE_HC_VIS_STAT_BUILD_STATE			 (0xfL<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_IDLE		 (0L<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_START		 (1L<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_REQUEST		 (2L<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_UPDATE64		 (3L<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_UPDATE32		 (4L<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_UPDATE_DONE	 (5L<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_DMA		 (6L<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_MSI_CONTROL	 (7L<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_MSI_LOW		 (8L<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_MSI_HIGH		 (9L<<0)
#define BCE_HC_VIS_STAT_BUILD_STATE_MSI_DATA		 (10L<<0)
#define BCE_HC_VIS_DMA_STAT_STATE			 (0xfL<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_IDLE			 (0L<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_STATUS_PARAM		 (1L<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_STATUS_DMA		 (2L<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_WRITE_COMP		 (3L<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_COMP			 (4L<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_STATISTIC_PARAM	 (5L<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_STATISTIC_DMA	 (6L<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_WRITE_COMP_1		 (7L<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_WRITE_COMP_2		 (8L<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_WAIT			 (9L<<8)
#define BCE_HC_VIS_DMA_STAT_STATE_ABORT		 (15L<<8)
#define BCE_HC_VIS_DMA_MSI_STATE			 (0x7L<<12)
#define BCE_HC_VIS_STATISTIC_DMA_EN_STATE		 (0x3L<<15)
#define BCE_HC_VIS_STATISTIC_DMA_EN_STATE_IDLE		 (0L<<15)
#define BCE_HC_VIS_STATISTIC_DMA_EN_STATE_COUNT	 (1L<<15)
#define BCE_HC_VIS_STATISTIC_DMA_EN_STATE_START	 (2L<<15)

#define BCE_HC_VIS_1					0x0000690c
#define BCE_HC_VIS_1_HW_INTACK_STATE			 (1L<<4)
#define BCE_HC_VIS_1_HW_INTACK_STATE_IDLE		 (0L<<4)
#define BCE_HC_VIS_1_HW_INTACK_STATE_COUNT		 (1L<<4)
#define BCE_HC_VIS_1_SW_INTACK_STATE			 (1L<<5)
#define BCE_HC_VIS_1_SW_INTACK_STATE_IDLE		 (0L<<5)
#define BCE_HC_VIS_1_SW_INTACK_STATE_COUNT		 (1L<<5)
#define BCE_HC_VIS_1_DURING_SW_INTACK_STATE		 (1L<<6)
#define BCE_HC_VIS_1_DURING_SW_INTACK_STATE_IDLE	 (0L<<6)
#define BCE_HC_VIS_1_DURING_SW_INTACK_STATE_COUNT	 (1L<<6)
#define BCE_HC_VIS_1_MAILBOX_COUNT_STATE		 (1L<<7)
#define BCE_HC_VIS_1_MAILBOX_COUNT_STATE_IDLE		 (0L<<7)
#define BCE_HC_VIS_1_MAILBOX_COUNT_STATE_COUNT		 (1L<<7)
#define BCE_HC_VIS_1_RAM_RD_ARB_STATE			 (0xfL<<17)
#define BCE_HC_VIS_1_RAM_RD_ARB_STATE_IDLE		 (0L<<17)
#define BCE_HC_VIS_1_RAM_RD_ARB_STATE_DMA		 (1L<<17)
#define BCE_HC_VIS_1_RAM_RD_ARB_STATE_UPDATE		 (2L<<17)
#define BCE_HC_VIS_1_RAM_RD_ARB_STATE_ASSIGN		 (3L<<17)
#define BCE_HC_VIS_1_RAM_RD_ARB_STATE_WAIT		 (4L<<17)
#define BCE_HC_VIS_1_RAM_RD_ARB_STATE_REG_UPDATE	 (5L<<17)
#define BCE_HC_VIS_1_RAM_RD_ARB_STATE_REG_ASSIGN	 (6L<<17)
#define BCE_HC_VIS_1_RAM_RD_ARB_STATE_REG_WAIT		 (7L<<17)
#define BCE_HC_VIS_1_RAM_WR_ARB_STATE			 (0x3L<<21)
#define BCE_HC_VIS_1_RAM_WR_ARB_STATE_NORMAL		 (0L<<21)
#define BCE_HC_VIS_1_RAM_WR_ARB_STATE_CLEAR		 (1L<<21)
#define BCE_HC_VIS_1_INT_GEN_STATE			 (1L<<23)
#define BCE_HC_VIS_1_INT_GEN_STATE_DLE			 (0L<<23)
#define BCE_HC_VIS_1_INT_GEN_STATE_NTERRUPT		 (1L<<23)
#define BCE_HC_VIS_1_STAT_CHAN_ID			 (0x7L<<24)
#define BCE_HC_VIS_1_INT_B				 (1L<<27)

#define BCE_HC_DEBUG_VECT_PEEK				0x00006910
#define BCE_HC_DEBUG_VECT_PEEK_1_VALUE			 (0x7ffL<<0)
#define BCE_HC_DEBUG_VECT_PEEK_1_PEEK_EN		 (1L<<11)
#define BCE_HC_DEBUG_VECT_PEEK_1_SEL			 (0xfL<<12)
#define BCE_HC_DEBUG_VECT_PEEK_2_VALUE			 (0x7ffL<<16)
#define BCE_HC_DEBUG_VECT_PEEK_2_PEEK_EN		 (1L<<27)
#define BCE_HC_DEBUG_VECT_PEEK_2_SEL			 (0xfL<<28)

#define BCE_HC_COALESCE_NOW				0x00006914
#define BCE_HC_COALESCE_NOW_COAL_NOW			 (0x1ffL<<1)
#define BCE_HC_COALESCE_NOW_COAL_NOW_WO_INT		 (0x1ffL<<11)
#define BCE_HC_COALESCE_NOW_COAL_ON_NXT_EVENT		 (0x1ffL<<21)

#define BCE_HC_MSIX_BIT_VECTOR				0x00006918
#define BCE_HC_MSIX_BIT_VECTOR_VAL			 (0x1ffL<<0)

#define BCE_HC_SB_CONFIG_1				0x00006a00
#define BCE_HC_SB_CONFIG_1_RX_TMR_MODE			 (1L<<1)
#define BCE_HC_SB_CONFIG_1_TX_TMR_MODE			 (1L<<2)
#define BCE_HC_SB_CONFIG_1_COM_TMR_MODE		 (1L<<3)
#define BCE_HC_SB_CONFIG_1_CMD_TMR_MODE		 (1L<<4)
#define BCE_HC_SB_CONFIG_1_PER_MODE			 (1L<<16)
#define BCE_HC_SB_CONFIG_1_ONE_SHOT			 (1L<<17)
#define BCE_HC_SB_CONFIG_1_USE_INT_PARAM		 (1L<<18)
#define BCE_HC_SB_CONFIG_1_PER_COLLECT_LIMIT		 (0xfL<<20)

#define BCE_HC_TX_QUICK_CONS_TRIP_1			0x00006a04
#define BCE_HC_TX_QUICK_CONS_TRIP_1_VALUE		 (0xffL<<0)
#define BCE_HC_TX_QUICK_CONS_TRIP_1_INT		 (0xffL<<16)

#define BCE_HC_COMP_PROD_TRIP_1			0x00006a08
#define BCE_HC_COMP_PROD_TRIP_1_VALUE			 (0xffL<<0)
#define BCE_HC_COMP_PROD_TRIP_1_INT			 (0xffL<<16)

#define BCE_HC_RX_QUICK_CONS_TRIP_1			0x00006a0c
#define BCE_HC_RX_QUICK_CONS_TRIP_1_VALUE		 (0xffL<<0)
#define BCE_HC_RX_QUICK_CONS_TRIP_1_INT		 (0xffL<<16)

#define BCE_HC_RX_TICKS_1				0x00006a10
#define BCE_HC_RX_TICKS_1_VALUE			 (0x3ffL<<0)
#define BCE_HC_RX_TICKS_1_INT				 (0x3ffL<<16)

#define BCE_HC_TX_TICKS_1				0x00006a14
#define BCE_HC_TX_TICKS_1_VALUE			 (0x3ffL<<0)
#define BCE_HC_TX_TICKS_1_INT				 (0x3ffL<<16)

#define BCE_HC_COM_TICKS_1				0x00006a18
#define BCE_HC_COM_TICKS_1_VALUE			 (0x3ffL<<0)
#define BCE_HC_COM_TICKS_1_INT				 (0x3ffL<<16)

#define BCE_HC_CMD_TICKS_1				0x00006a1c
#define BCE_HC_CMD_TICKS_1_VALUE			 (0x3ffL<<0)
#define BCE_HC_CMD_TICKS_1_INT				 (0x3ffL<<16)

#define BCE_HC_PERIODIC_TICKS_1			0x00006a20
#define BCE_HC_PERIODIC_TICKS_1_HC_PERIODIC_TICKS	 (0xffffL<<0)
#define BCE_HC_PERIODIC_TICKS_1_HC_INT_PERIODIC_TICKS	 (0xffffL<<16)

#define BCE_HC_SB_CONFIG_2				0x00006a24
#define BCE_HC_SB_CONFIG_2_RX_TMR_MODE			 (1L<<1)
#define BCE_HC_SB_CONFIG_2_TX_TMR_MODE			 (1L<<2)
#define BCE_HC_SB_CONFIG_2_COM_TMR_MODE		 (1L<<3)
#define BCE_HC_SB_CONFIG_2_CMD_TMR_MODE		 (1L<<4)
#define BCE_HC_SB_CONFIG_2_PER_MODE			 (1L<<16)
#define BCE_HC_SB_CONFIG_2_ONE_SHOT			 (1L<<17)
#define BCE_HC_SB_CONFIG_2_USE_INT_PARAM		 (1L<<18)
#define BCE_HC_SB_CONFIG_2_PER_COLLECT_LIMIT		 (0xfL<<20)

#define BCE_HC_TX_QUICK_CONS_TRIP_2			0x00006a28
#define BCE_HC_TX_QUICK_CONS_TRIP_2_VALUE		 (0xffL<<0)
#define BCE_HC_TX_QUICK_CONS_TRIP_2_INT		 (0xffL<<16)

#define BCE_HC_COMP_PROD_TRIP_2			0x00006a2c
#define BCE_HC_COMP_PROD_TRIP_2_VALUE			 (0xffL<<0)
#define BCE_HC_COMP_PROD_TRIP_2_INT			 (0xffL<<16)

#define BCE_HC_RX_QUICK_CONS_TRIP_2			0x00006a30
#define BCE_HC_RX_QUICK_CONS_TRIP_2_VALUE		 (0xffL<<0)
#define BCE_HC_RX_QUICK_CONS_TRIP_2_INT		 (0xffL<<16)

#define BCE_HC_RX_TICKS_2				0x00006a34
#define BCE_HC_RX_TICKS_2_VALUE			 (0x3ffL<<0)
#define BCE_HC_RX_TICKS_2_INT				 (0x3ffL<<16)

#define BCE_HC_TX_TICKS_2				0x00006a38
#define BCE_HC_TX_TICKS_2_VALUE			 (0x3ffL<<0)
#define BCE_HC_TX_TICKS_2_INT				 (0x3ffL<<16)

#define BCE_HC_COM_TICKS_2				0x00006a3c
#define BCE_HC_COM_TICKS_2_VALUE			 (0x3ffL<<0)
#define BCE_HC_COM_TICKS_2_INT				 (0x3ffL<<16)

#define BCE_HC_CMD_TICKS_2				0x00006a40
#define BCE_HC_CMD_TICKS_2_VALUE			 (0x3ffL<<0)
#define BCE_HC_CMD_TICKS_2_INT				 (0x3ffL<<16)

#define BCE_HC_PERIODIC_TICKS_2			0x00006a44
#define BCE_HC_PERIODIC_TICKS_2_HC_PERIODIC_TICKS	 (0xffffL<<0)
#define BCE_HC_PERIODIC_TICKS_2_HC_INT_PERIODIC_TICKS	 (0xffffL<<16)

#define BCE_HC_SB_CONFIG_3				0x00006a48
#define BCE_HC_SB_CONFIG_3_RX_TMR_MODE			 (1L<<1)
#define BCE_HC_SB_CONFIG_3_TX_TMR_MODE			 (1L<<2)
#define BCE_HC_SB_CONFIG_3_COM_TMR_MODE		 (1L<<3)
#define BCE_HC_SB_CONFIG_3_CMD_TMR_MODE		 (1L<<4)
#define BCE_HC_SB_CONFIG_3_PER_MODE			 (1L<<16)
#define BCE_HC_SB_CONFIG_3_ONE_SHOT			 (1L<<17)
#define BCE_HC_SB_CONFIG_3_USE_INT_PARAM		 (1L<<18)
#define BCE_HC_SB_CONFIG_3_PER_COLLECT_LIMIT		 (0xfL<<20)

#define BCE_HC_TX_QUICK_CONS_TRIP_3			0x00006a4c
#define BCE_HC_TX_QUICK_CONS_TRIP_3_VALUE		 (0xffL<<0)
#define BCE_HC_TX_QUICK_CONS_TRIP_3_INT		 (0xffL<<16)

#define BCE_HC_COMP_PROD_TRIP_3			0x00006a50
#define BCE_HC_COMP_PROD_TRIP_3_VALUE			 (0xffL<<0)
#define BCE_HC_COMP_PROD_TRIP_3_INT			 (0xffL<<16)

#define BCE_HC_RX_QUICK_CONS_TRIP_3			0x00006a54
#define BCE_HC_RX_QUICK_CONS_TRIP_3_VALUE		 (0xffL<<0)
#define BCE_HC_RX_QUICK_CONS_TRIP_3_INT		 (0xffL<<16)

#define BCE_HC_RX_TICKS_3				0x00006a58
#define BCE_HC_RX_TICKS_3_VALUE			 (0x3ffL<<0)
#define BCE_HC_RX_TICKS_3_INT				 (0x3ffL<<16)

#define BCE_HC_TX_TICKS_3				0x00006a5c
#define BCE_HC_TX_TICKS_3_VALUE			 (0x3ffL<<0)
#define BCE_HC_TX_TICKS_3_INT				 (0x3ffL<<16)

#define BCE_HC_COM_TICKS_3				0x00006a60
#define BCE_HC_COM_TICKS_3_VALUE			 (0x3ffL<<0)
#define BCE_HC_COM_TICKS_3_INT				 (0x3ffL<<16)

#define BCE_HC_CMD_TICKS_3				0x00006a64
#define BCE_HC_CMD_TICKS_3_VALUE			 (0x3ffL<<0)
#define BCE_HC_CMD_TICKS_3_INT				 (0x3ffL<<16)

#define BCE_HC_PERIODIC_TICKS_3			0x00006a68
#define BCE_HC_PERIODIC_TICKS_3_HC_PERIODIC_TICKS	 (0xffffL<<0)
#define BCE_HC_PERIODIC_TICKS_3_HC_INT_PERIODIC_TICKS	 (0xffffL<<16)

#define BCE_HC_SB_CONFIG_4				0x00006a6c
#define BCE_HC_SB_CONFIG_4_RX_TMR_MODE			 (1L<<1)
#define BCE_HC_SB_CONFIG_4_TX_TMR_MODE			 (1L<<2)
#define BCE_HC_SB_CONFIG_4_COM_TMR_MODE		 (1L<<3)
#define BCE_HC_SB_CONFIG_4_CMD_TMR_MODE		 (1L<<4)
#define BCE_HC_SB_CONFIG_4_PER_MODE			 (1L<<16)
#define BCE_HC_SB_CONFIG_4_ONE_SHOT			 (1L<<17)
#define BCE_HC_SB_CONFIG_4_USE_INT_PARAM		 (1L<<18)
#define BCE_HC_SB_CONFIG_4_PER_COLLECT_LIMIT		 (0xfL<<20)

#define BCE_HC_TX_QUICK_CONS_TRIP_4			0x00006a70
#define BCE_HC_TX_QUICK_CONS_TRIP_4_VALUE		 (0xffL<<0)
#define BCE_HC_TX_QUICK_CONS_TRIP_4_INT		 (0xffL<<16)

#define BCE_HC_COMP_PROD_TRIP_4			0x00006a74
#define BCE_HC_COMP_PROD_TRIP_4_VALUE			 (0xffL<<0)
#define BCE_HC_COMP_PROD_TRIP_4_INT			 (0xffL<<16)

#define BCE_HC_RX_QUICK_CONS_TRIP_4			0x00006a78
#define BCE_HC_RX_QUICK_CONS_TRIP_4_VALUE		 (0xffL<<0)
#define BCE_HC_RX_QUICK_CONS_TRIP_4_INT		 (0xffL<<16)

#define BCE_HC_RX_TICKS_4				0x00006a7c
#define BCE_HC_RX_TICKS_4_VALUE			 (0x3ffL<<0)
#define BCE_HC_RX_TICKS_4_INT				 (0x3ffL<<16)

#define BCE_HC_TX_TICKS_4				0x00006a80
#define BCE_HC_TX_TICKS_4_VALUE			 (0x3ffL<<0)
#define BCE_HC_TX_TICKS_4_INT				 (0x3ffL<<16)

#define BCE_HC_COM_TICKS_4				0x00006a84
#define BCE_HC_COM_TICKS_4_VALUE			 (0x3ffL<<0)
#define BCE_HC_COM_TICKS_4_INT				 (0x3ffL<<16)

#define BCE_HC_CMD_TICKS_4				0x00006a88
#define BCE_HC_CMD_TICKS_4_VALUE			 (0x3ffL<<0)
#define BCE_HC_CMD_TICKS_4_INT				 (0x3ffL<<16)

#define BCE_HC_PERIODIC_TICKS_4			0x00006a8c
#define BCE_HC_PERIODIC_TICKS_4_HC_PERIODIC_TICKS	 (0xffffL<<0)
#define BCE_HC_PERIODIC_TICKS_4_HC_INT_PERIODIC_TICKS	 (0xffffL<<16)

#define BCE_HC_SB_CONFIG_5				0x00006a90
#define BCE_HC_SB_CONFIG_5_RX_TMR_MODE			 (1L<<1)
#define BCE_HC_SB_CONFIG_5_TX_TMR_MODE			 (1L<<2)
#define BCE_HC_SB_CONFIG_5_COM_TMR_MODE		 (1L<<3)
#define BCE_HC_SB_CONFIG_5_CMD_TMR_MODE		 (1L<<4)
#define BCE_HC_SB_CONFIG_5_PER_MODE			 (1L<<16)
#define BCE_HC_SB_CONFIG_5_ONE_SHOT			 (1L<<17)
#define BCE_HC_SB_CONFIG_5_USE_INT_PARAM		 (1L<<18)
#define BCE_HC_SB_CONFIG_5_PER_COLLECT_LIMIT		 (0xfL<<20)

#define BCE_HC_TX_QUICK_CONS_TRIP_5			0x00006a94
#define BCE_HC_TX_QUICK_CONS_TRIP_5_VALUE		 (0xffL<<0)
#define BCE_HC_TX_QUICK_CONS_TRIP_5_INT		 (0xffL<<16)

#define BCE_HC_COMP_PROD_TRIP_5			0x00006a98
#define BCE_HC_COMP_PROD_TRIP_5_VALUE			 (0xffL<<0)
#define BCE_HC_COMP_PROD_TRIP_5_INT			 (0xffL<<16)

#define BCE_HC_RX_QUICK_CONS_TRIP_5			0x00006a9c
#define BCE_HC_RX_QUICK_CONS_TRIP_5_VALUE		 (0xffL<<0)
#define BCE_HC_RX_QUICK_CONS_TRIP_5_INT		 (0xffL<<16)

#define BCE_HC_RX_TICKS_5				0x00006aa0
#define BCE_HC_RX_TICKS_5_VALUE			 (0x3ffL<<0)
#define BCE_HC_RX_TICKS_5_INT				 (0x3ffL<<16)

#define BCE_HC_TX_TICKS_5				0x00006aa4
#define BCE_HC_TX_TICKS_5_VALUE			 (0x3ffL<<0)
#define BCE_HC_TX_TICKS_5_INT				 (0x3ffL<<16)

#define BCE_HC_COM_TICKS_5				0x00006aa8
#define BCE_HC_COM_TICKS_5_VALUE			 (0x3ffL<<0)
#define BCE_HC_COM_TICKS_5_INT				 (0x3ffL<<16)

#define BCE_HC_CMD_TICKS_5				0x00006aac
#define BCE_HC_CMD_TICKS_5_VALUE			 (0x3ffL<<0)
#define BCE_HC_CMD_TICKS_5_INT				 (0x3ffL<<16)

#define BCE_HC_PERIODIC_TICKS_5			0x00006ab0
#define BCE_HC_PERIODIC_TICKS_5_HC_PERIODIC_TICKS	 (0xffffL<<0)
#define BCE_HC_PERIODIC_TICKS_5_HC_INT_PERIODIC_TICKS	 (0xffffL<<16)

#define BCE_HC_SB_CONFIG_6				0x00006ab4
#define BCE_HC_SB_CONFIG_6_RX_TMR_MODE			 (1L<<1)
#define BCE_HC_SB_CONFIG_6_TX_TMR_MODE			 (1L<<2)
#define BCE_HC_SB_CONFIG_6_COM_TMR_MODE		 (1L<<3)
#define BCE_HC_SB_CONFIG_6_CMD_TMR_MODE		 (1L<<4)
#define BCE_HC_SB_CONFIG_6_PER_MODE			 (1L<<16)
#define BCE_HC_SB_CONFIG_6_ONE_SHOT			 (1L<<17)
#define BCE_HC_SB_CONFIG_6_USE_INT_PARAM		 (1L<<18)
#define BCE_HC_SB_CONFIG_6_PER_COLLECT_LIMIT		 (0xfL<<20)

#define BCE_HC_TX_QUICK_CONS_TRIP_6			0x00006ab8
#define BCE_HC_TX_QUICK_CONS_TRIP_6_VALUE		 (0xffL<<0)
#define BCE_HC_TX_QUICK_CONS_TRIP_6_INT		 (0xffL<<16)

#define BCE_HC_COMP_PROD_TRIP_6			0x00006abc
#define BCE_HC_COMP_PROD_TRIP_6_VALUE			 (0xffL<<0)
#define BCE_HC_COMP_PROD_TRIP_6_INT			 (0xffL<<16)

#define BCE_HC_RX_QUICK_CONS_TRIP_6			0x00006ac0
#define BCE_HC_RX_QUICK_CONS_TRIP_6_VALUE		 (0xffL<<0)
#define BCE_HC_RX_QUICK_CONS_TRIP_6_INT		 (0xffL<<16)

#define BCE_HC_RX_TICKS_6				0x00006ac4
#define BCE_HC_RX_TICKS_6_VALUE			 (0x3ffL<<0)
#define BCE_HC_RX_TICKS_6_INT				 (0x3ffL<<16)

#define BCE_HC_TX_TICKS_6				0x00006ac8
#define BCE_HC_TX_TICKS_6_VALUE			 (0x3ffL<<0)
#define BCE_HC_TX_TICKS_6_INT				 (0x3ffL<<16)

#define BCE_HC_COM_TICKS_6				0x00006acc
#define BCE_HC_COM_TICKS_6_VALUE			 (0x3ffL<<0)
#define BCE_HC_COM_TICKS_6_INT				 (0x3ffL<<16)

#define BCE_HC_CMD_TICKS_6				0x00006ad0
#define BCE_HC_CMD_TICKS_6_VALUE			 (0x3ffL<<0)
#define BCE_HC_CMD_TICKS_6_INT				 (0x3ffL<<16)

#define BCE_HC_PERIODIC_TICKS_6			0x00006ad4
#define BCE_HC_PERIODIC_TICKS_6_HC_PERIODIC_TICKS	 (0xffffL<<0)
#define BCE_HC_PERIODIC_TICKS_6_HC_INT_PERIODIC_TICKS	 (0xffffL<<16)

#define BCE_HC_SB_CONFIG_7				0x00006ad8
#define BCE_HC_SB_CONFIG_7_RX_TMR_MODE			 (1L<<1)
#define BCE_HC_SB_CONFIG_7_TX_TMR_MODE			 (1L<<2)
#define BCE_HC_SB_CONFIG_7_COM_TMR_MODE		 (1L<<3)
#define BCE_HC_SB_CONFIG_7_CMD_TMR_MODE		 (1L<<4)
#define BCE_HC_SB_CONFIG_7_PER_MODE			 (1L<<16)
#define BCE_HC_SB_CONFIG_7_ONE_SHOT			 (1L<<17)
#define BCE_HC_SB_CONFIG_7_USE_INT_PARAM		 (1L<<18)
#define BCE_HC_SB_CONFIG_7_PER_COLLECT_LIMIT		 (0xfL<<20)

#define BCE_HC_TX_QUICK_CONS_TRIP_7			0x00006adc
#define BCE_HC_TX_QUICK_CONS_TRIP_7_VALUE		 (0xffL<<0)
#define BCE_HC_TX_QUICK_CONS_TRIP_7_INT		 (0xffL<<16)

#define BCE_HC_COMP_PROD_TRIP_7			0x00006ae0
#define BCE_HC_COMP_PROD_TRIP_7_VALUE			 (0xffL<<0)
#define BCE_HC_COMP_PROD_TRIP_7_INT			 (0xffL<<16)

#define BCE_HC_RX_QUICK_CONS_TRIP_7			0x00006ae4
#define BCE_HC_RX_QUICK_CONS_TRIP_7_VALUE		 (0xffL<<0)
#define BCE_HC_RX_QUICK_CONS_TRIP_7_INT		 (0xffL<<16)

#define BCE_HC_RX_TICKS_7				0x00006ae8
#define BCE_HC_RX_TICKS_7_VALUE			 (0x3ffL<<0)
#define BCE_HC_RX_TICKS_7_INT				 (0x3ffL<<16)

#define BCE_HC_TX_TICKS_7				0x00006aec
#define BCE_HC_TX_TICKS_7_VALUE			 (0x3ffL<<0)
#define BCE_HC_TX_TICKS_7_INT				 (0x3ffL<<16)

#define BCE_HC_COM_TICKS_7				0x00006af0
#define BCE_HC_COM_TICKS_7_VALUE			 (0x3ffL<<0)
#define BCE_HC_COM_TICKS_7_INT				 (0x3ffL<<16)

#define BCE_HC_CMD_TICKS_7				0x00006af4
#define BCE_HC_CMD_TICKS_7_VALUE			 (0x3ffL<<0)
#define BCE_HC_CMD_TICKS_7_INT				 (0x3ffL<<16)

#define BCE_HC_PERIODIC_TICKS_7			0x00006af8
#define BCE_HC_PERIODIC_TICKS_7_HC_PERIODIC_TICKS	 (0xffffL<<0)
#define BCE_HC_PERIODIC_TICKS_7_HC_INT_PERIODIC_TICKS	 (0xffffL<<16)

#define BCE_HC_SB_CONFIG_8				0x00006afc
#define BCE_HC_SB_CONFIG_8_RX_TMR_MODE			 (1L<<1)
#define BCE_HC_SB_CONFIG_8_TX_TMR_MODE			 (1L<<2)
#define BCE_HC_SB_CONFIG_8_COM_TMR_MODE		 (1L<<3)
#define BCE_HC_SB_CONFIG_8_CMD_TMR_MODE		 (1L<<4)
#define BCE_HC_SB_CONFIG_8_PER_MODE			 (1L<<16)
#define BCE_HC_SB_CONFIG_8_ONE_SHOT			 (1L<<17)
#define BCE_HC_SB_CONFIG_8_USE_INT_PARAM		 (1L<<18)
#define BCE_HC_SB_CONFIG_8_PER_COLLECT_LIMIT		 (0xfL<<20)

#define BCE_HC_TX_QUICK_CONS_TRIP_8			0x00006b00
#define BCE_HC_TX_QUICK_CONS_TRIP_8_VALUE		 (0xffL<<0)
#define BCE_HC_TX_QUICK_CONS_TRIP_8_INT		 (0xffL<<16)

#define BCE_HC_COMP_PROD_TRIP_8			0x00006b04
#define BCE_HC_COMP_PROD_TRIP_8_VALUE			 (0xffL<<0)
#define BCE_HC_COMP_PROD_TRIP_8_INT			 (0xffL<<16)

#define BCE_HC_RX_QUICK_CONS_TRIP_8			0x00006b08
#define BCE_HC_RX_QUICK_CONS_TRIP_8_VALUE		 (0xffL<<0)
#define BCE_HC_RX_QUICK_CONS_TRIP_8_INT		 (0xffL<<16)

#define BCE_HC_RX_TICKS_8				0x00006b0c
#define BCE_HC_RX_TICKS_8_VALUE			 (0x3ffL<<0)
#define BCE_HC_RX_TICKS_8_INT				 (0x3ffL<<16)

#define BCE_HC_TX_TICKS_8				0x00006b10
#define BCE_HC_TX_TICKS_8_VALUE			 (0x3ffL<<0)
#define BCE_HC_TX_TICKS_8_INT				 (0x3ffL<<16)

#define BCE_HC_COM_TICKS_8				0x00006b14
#define BCE_HC_COM_TICKS_8_VALUE			 (0x3ffL<<0)
#define BCE_HC_COM_TICKS_8_INT				 (0x3ffL<<16)

#define BCE_HC_CMD_TICKS_8				0x00006b18
#define BCE_HC_CMD_TICKS_8_VALUE			 (0x3ffL<<0)
#define BCE_HC_CMD_TICKS_8_INT				 (0x3ffL<<16)

#define BCE_HC_PERIODIC_TICKS_8			0x00006b1c
#define BCE_HC_PERIODIC_TICKS_8_HC_PERIODIC_TICKS	 (0xffffL<<0)
#define BCE_HC_PERIODIC_TICKS_8_HC_INT_PERIODIC_TICKS	 (0xffffL<<16)


/*
 *  txp_reg definition
 *  offset: 0x40000
 */
#define BCE_TXP_CPU_MODE				0x00045000
#define BCE_TXP_CPU_MODE_LOCAL_RST			 (1L<<0)
#define BCE_TXP_CPU_MODE_STEP_ENA			 (1L<<1)
#define BCE_TXP_CPU_MODE_PAGE_0_DATA_ENA		 (1L<<2)
#define BCE_TXP_CPU_MODE_PAGE_0_INST_ENA		 (1L<<3)
#define BCE_TXP_CPU_MODE_MSG_BIT1			 (1L<<6)
#define BCE_TXP_CPU_MODE_INTERRUPT_ENA			 (1L<<7)
#define BCE_TXP_CPU_MODE_SOFT_HALT			 (1L<<10)
#define BCE_TXP_CPU_MODE_BAD_DATA_HALT_ENA		 (1L<<11)
#define BCE_TXP_CPU_MODE_BAD_INST_HALT_ENA		 (1L<<12)
#define BCE_TXP_CPU_MODE_FIO_ABORT_HALT_ENA		 (1L<<13)
#define BCE_TXP_CPU_MODE_SPAD_UNDERFLOW_HALT_ENA	 (1L<<15)

#define BCE_TXP_CPU_STATE				0x00045004
#define BCE_TXP_CPU_STATE_BREAKPOINT			 (1L<<0)
#define BCE_TXP_CPU_STATE_BAD_INST_HALTED		 (1L<<2)
#define BCE_TXP_CPU_STATE_PAGE_0_DATA_HALTED		 (1L<<3)
#define BCE_TXP_CPU_STATE_PAGE_0_INST_HALTED		 (1L<<4)
#define BCE_TXP_CPU_STATE_BAD_DATA_ADDR_HALTED		 (1L<<5)
#define BCE_TXP_CPU_STATE_BAD_pc_HALTED		 (1L<<6)
#define BCE_TXP_CPU_STATE_ALIGN_HALTED			 (1L<<7)
#define BCE_TXP_CPU_STATE_FIO_ABORT_HALTED		 (1L<<8)
#define BCE_TXP_CPU_STATE_SOFT_HALTED			 (1L<<10)
#define BCE_TXP_CPU_STATE_SPAD_UNDERFLOW		 (1L<<11)
#define BCE_TXP_CPU_STATE_INTERRRUPT			 (1L<<12)
#define BCE_TXP_CPU_STATE_DATA_ACCESS_STALL		 (1L<<14)
#define BCE_TXP_CPU_STATE_INST_FETCH_STALL		 (1L<<15)
#define BCE_TXP_CPU_STATE_BLOCKED_READ			 (1L<<31)

#define BCE_TXP_CPU_EVENT_MASK				0x00045008
#define BCE_TXP_CPU_EVENT_MASK_BREAKPOINT_MASK		 (1L<<0)
#define BCE_TXP_CPU_EVENT_MASK_BAD_INST_HALTED_MASK	 (1L<<2)
#define BCE_TXP_CPU_EVENT_MASK_PAGE_0_DATA_HALTED_MASK	 (1L<<3)
#define BCE_TXP_CPU_EVENT_MASK_PAGE_0_INST_HALTED_MASK	 (1L<<4)
#define BCE_TXP_CPU_EVENT_MASK_BAD_DATA_ADDR_HALTED_MASK	 (1L<<5)
#define BCE_TXP_CPU_EVENT_MASK_BAD_PC_HALTED_MASK	 (1L<<6)
#define BCE_TXP_CPU_EVENT_MASK_ALIGN_HALTED_MASK	 (1L<<7)
#define BCE_TXP_CPU_EVENT_MASK_FIO_ABORT_MASK		 (1L<<8)
#define BCE_TXP_CPU_EVENT_MASK_SOFT_HALTED_MASK	 (1L<<10)
#define BCE_TXP_CPU_EVENT_MASK_SPAD_UNDERFLOW_MASK	 (1L<<11)
#define BCE_TXP_CPU_EVENT_MASK_INTERRUPT_MASK		 (1L<<12)

#define BCE_TXP_CPU_PROGRAM_COUNTER			0x0004501c
#define BCE_TXP_CPU_INSTRUCTION			0x00045020
#define BCE_TXP_CPU_DATA_ACCESS			0x00045024
#define BCE_TXP_CPU_INTERRUPT_ENABLE			0x00045028
#define BCE_TXP_CPU_INTERRUPT_VECTOR			0x0004502c
#define BCE_TXP_CPU_INTERRUPT_SAVED_PC			0x00045030
#define BCE_TXP_CPU_HW_BREAKPOINT			0x00045034
#define BCE_TXP_CPU_HW_BREAKPOINT_DISABLE		 (1L<<0)
#define BCE_TXP_CPU_HW_BREAKPOINT_ADDRESS		 (0x3fffffffL<<2)

#define BCE_TXP_CPU_REG_FILE				0x00045200
#define BCE_TXP_FTQ_DATA				0x000453c0
#define BCE_TXP_FTQ_CMD				0x000453f8
#define BCE_TXP_FTQ_CMD_OFFSET				 (0x3ffL<<0)
#define BCE_TXP_FTQ_CMD_WR_TOP				 (1L<<10)
#define BCE_TXP_FTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_TXP_FTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_TXP_FTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_TXP_FTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_TXP_FTQ_CMD_ADD_INTERVEN			 (1L<<27)
#define BCE_TXP_FTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_TXP_FTQ_CMD_INTERVENE_CLR			 (1L<<29)
#define BCE_TXP_FTQ_CMD_POP				 (1L<<30)
#define BCE_TXP_FTQ_CMD_BUSY				 (1L<<31)

#define BCE_TXP_FTQ_CTL				0x000453fc
#define BCE_TXP_FTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_TXP_FTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_TXP_FTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_TXP_FTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_TXP_FTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)

#define BCE_TXP_SCRATCH				0x00060000


/*
 *  tpat_reg definition
 *  offset: 0x80000
 */
#define BCE_TPAT_CPU_MODE				0x00085000
#define BCE_TPAT_CPU_MODE_LOCAL_RST			 (1L<<0)
#define BCE_TPAT_CPU_MODE_STEP_ENA			 (1L<<1)
#define BCE_TPAT_CPU_MODE_PAGE_0_DATA_ENA		 (1L<<2)
#define BCE_TPAT_CPU_MODE_PAGE_0_INST_ENA		 (1L<<3)
#define BCE_TPAT_CPU_MODE_MSG_BIT1			 (1L<<6)
#define BCE_TPAT_CPU_MODE_INTERRUPT_ENA		 (1L<<7)
#define BCE_TPAT_CPU_MODE_SOFT_HALT			 (1L<<10)
#define BCE_TPAT_CPU_MODE_BAD_DATA_HALT_ENA		 (1L<<11)
#define BCE_TPAT_CPU_MODE_BAD_INST_HALT_ENA		 (1L<<12)
#define BCE_TPAT_CPU_MODE_FIO_ABORT_HALT_ENA		 (1L<<13)
#define BCE_TPAT_CPU_MODE_SPAD_UNDERFLOW_HALT_ENA	 (1L<<15)

#define BCE_TPAT_CPU_STATE				0x00085004
#define BCE_TPAT_CPU_STATE_BREAKPOINT			 (1L<<0)
#define BCE_TPAT_CPU_STATE_BAD_INST_HALTED		 (1L<<2)
#define BCE_TPAT_CPU_STATE_PAGE_0_DATA_HALTED		 (1L<<3)
#define BCE_TPAT_CPU_STATE_PAGE_0_INST_HALTED		 (1L<<4)
#define BCE_TPAT_CPU_STATE_BAD_DATA_ADDR_HALTED	 (1L<<5)
#define BCE_TPAT_CPU_STATE_BAD_pc_HALTED		 (1L<<6)
#define BCE_TPAT_CPU_STATE_ALIGN_HALTED		 (1L<<7)
#define BCE_TPAT_CPU_STATE_FIO_ABORT_HALTED		 (1L<<8)
#define BCE_TPAT_CPU_STATE_SOFT_HALTED			 (1L<<10)
#define BCE_TPAT_CPU_STATE_SPAD_UNDERFLOW		 (1L<<11)
#define BCE_TPAT_CPU_STATE_INTERRRUPT			 (1L<<12)
#define BCE_TPAT_CPU_STATE_DATA_ACCESS_STALL		 (1L<<14)
#define BCE_TPAT_CPU_STATE_INST_FETCH_STALL		 (1L<<15)
#define BCE_TPAT_CPU_STATE_BLOCKED_READ		 (1L<<31)

#define BCE_TPAT_CPU_EVENT_MASK			0x00085008
#define BCE_TPAT_CPU_EVENT_MASK_BREAKPOINT_MASK	 (1L<<0)
#define BCE_TPAT_CPU_EVENT_MASK_BAD_INST_HALTED_MASK	 (1L<<2)
#define BCE_TPAT_CPU_EVENT_MASK_PAGE_0_DATA_HALTED_MASK	 (1L<<3)
#define BCE_TPAT_CPU_EVENT_MASK_PAGE_0_INST_HALTED_MASK	 (1L<<4)
#define BCE_TPAT_CPU_EVENT_MASK_BAD_DATA_ADDR_HALTED_MASK	 (1L<<5)
#define BCE_TPAT_CPU_EVENT_MASK_BAD_PC_HALTED_MASK	 (1L<<6)
#define BCE_TPAT_CPU_EVENT_MASK_ALIGN_HALTED_MASK	 (1L<<7)
#define BCE_TPAT_CPU_EVENT_MASK_FIO_ABORT_MASK		 (1L<<8)
#define BCE_TPAT_CPU_EVENT_MASK_SOFT_HALTED_MASK	 (1L<<10)
#define BCE_TPAT_CPU_EVENT_MASK_SPAD_UNDERFLOW_MASK	 (1L<<11)
#define BCE_TPAT_CPU_EVENT_MASK_INTERRUPT_MASK		 (1L<<12)

#define BCE_TPAT_CPU_PROGRAM_COUNTER			0x0008501c
#define BCE_TPAT_CPU_INSTRUCTION			0x00085020
#define BCE_TPAT_CPU_DATA_ACCESS			0x00085024
#define BCE_TPAT_CPU_INTERRUPT_ENABLE			0x00085028
#define BCE_TPAT_CPU_INTERRUPT_VECTOR			0x0008502c
#define BCE_TPAT_CPU_INTERRUPT_SAVED_PC		0x00085030
#define BCE_TPAT_CPU_HW_BREAKPOINT			0x00085034
#define BCE_TPAT_CPU_HW_BREAKPOINT_DISABLE		 (1L<<0)
#define BCE_TPAT_CPU_HW_BREAKPOINT_ADDRESS		 (0x3fffffffL<<2)
#define BCE_TPAT_CPU_REG_FILE				0x00085200
#define BCE_TPAT_FTQ_DATA				0x000853c0
#define BCE_TPAT_FTQ_CMD				0x000853f8
#define BCE_TPAT_FTQ_CMD_OFFSET			 (0x3ffL<<0)
#define BCE_TPAT_FTQ_CMD_WR_TOP			 (1L<<10)
#define BCE_TPAT_FTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_TPAT_FTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_TPAT_FTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_TPAT_FTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_TPAT_FTQ_CMD_ADD_INTERVEN			 (1L<<27)
#define BCE_TPAT_FTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_TPAT_FTQ_CMD_INTERVENE_CLR			 (1L<<29)
#define BCE_TPAT_FTQ_CMD_POP				 (1L<<30)
#define BCE_TPAT_FTQ_CMD_BUSY				 (1L<<31)

#define BCE_TPAT_FTQ_CTL				0x000853fc
#define BCE_TPAT_FTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_TPAT_FTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_TPAT_FTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_TPAT_FTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_TPAT_FTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)

#define BCE_TPAT_SCRATCH				0x000a0000


/*
 *  rxp_reg definition
 *  offset: 0xc0000
 */
#define BCE_RXP_CPU_MODE				0x000c5000
#define BCE_RXP_CPU_MODE_LOCAL_RST			 (1L<<0)
#define BCE_RXP_CPU_MODE_STEP_ENA			 (1L<<1)
#define BCE_RXP_CPU_MODE_PAGE_0_DATA_ENA		 (1L<<2)
#define BCE_RXP_CPU_MODE_PAGE_0_INST_ENA		 (1L<<3)
#define BCE_RXP_CPU_MODE_MSG_BIT1			 (1L<<6)
#define BCE_RXP_CPU_MODE_INTERRUPT_ENA			 (1L<<7)
#define BCE_RXP_CPU_MODE_SOFT_HALT			 (1L<<10)
#define BCE_RXP_CPU_MODE_BAD_DATA_HALT_ENA		 (1L<<11)
#define BCE_RXP_CPU_MODE_BAD_INST_HALT_ENA		 (1L<<12)
#define BCE_RXP_CPU_MODE_FIO_ABORT_HALT_ENA		 (1L<<13)
#define BCE_RXP_CPU_MODE_SPAD_UNDERFLOW_HALT_ENA	 (1L<<15)

#define BCE_RXP_CPU_STATE				0x000c5004
#define BCE_RXP_CPU_STATE_BREAKPOINT			 (1L<<0)
#define BCE_RXP_CPU_STATE_BAD_INST_HALTED		 (1L<<2)
#define BCE_RXP_CPU_STATE_PAGE_0_DATA_HALTED		 (1L<<3)
#define BCE_RXP_CPU_STATE_PAGE_0_INST_HALTED		 (1L<<4)
#define BCE_RXP_CPU_STATE_BAD_DATA_ADDR_HALTED		 (1L<<5)
#define BCE_RXP_CPU_STATE_BAD_pc_HALTED		 (1L<<6)
#define BCE_RXP_CPU_STATE_ALIGN_HALTED			 (1L<<7)
#define BCE_RXP_CPU_STATE_FIO_ABORT_HALTED		 (1L<<8)
#define BCE_RXP_CPU_STATE_SOFT_HALTED			 (1L<<10)
#define BCE_RXP_CPU_STATE_SPAD_UNDERFLOW		 (1L<<11)
#define BCE_RXP_CPU_STATE_INTERRRUPT			 (1L<<12)
#define BCE_RXP_CPU_STATE_DATA_ACCESS_STALL		 (1L<<14)
#define BCE_RXP_CPU_STATE_INST_FETCH_STALL		 (1L<<15)
#define BCE_RXP_CPU_STATE_BLOCKED_READ			 (1L<<31)

#define BCE_RXP_CPU_EVENT_MASK				0x000c5008
#define BCE_RXP_CPU_EVENT_MASK_BREAKPOINT_MASK		 (1L<<0)
#define BCE_RXP_CPU_EVENT_MASK_BAD_INST_HALTED_MASK	 (1L<<2)
#define BCE_RXP_CPU_EVENT_MASK_PAGE_0_DATA_HALTED_MASK	 (1L<<3)
#define BCE_RXP_CPU_EVENT_MASK_PAGE_0_INST_HALTED_MASK	 (1L<<4)
#define BCE_RXP_CPU_EVENT_MASK_BAD_DATA_ADDR_HALTED_MASK	 (1L<<5)
#define BCE_RXP_CPU_EVENT_MASK_BAD_PC_HALTED_MASK	 (1L<<6)
#define BCE_RXP_CPU_EVENT_MASK_ALIGN_HALTED_MASK	 (1L<<7)
#define BCE_RXP_CPU_EVENT_MASK_FIO_ABORT_MASK		 (1L<<8)
#define BCE_RXP_CPU_EVENT_MASK_SOFT_HALTED_MASK	 (1L<<10)
#define BCE_RXP_CPU_EVENT_MASK_SPAD_UNDERFLOW_MASK	 (1L<<11)
#define BCE_RXP_CPU_EVENT_MASK_INTERRUPT_MASK		 (1L<<12)

#define BCE_RXP_CPU_PROGRAM_COUNTER			0x000c501c
#define BCE_RXP_CPU_INSTRUCTION			0x000c5020
#define BCE_RXP_CPU_DATA_ACCESS			0x000c5024
#define BCE_RXP_CPU_INTERRUPT_ENABLE			0x000c5028
#define BCE_RXP_CPU_INTERRUPT_VECTOR			0x000c502c
#define BCE_RXP_CPU_INTERRUPT_SAVED_PC			0x000c5030
#define BCE_RXP_CPU_HW_BREAKPOINT			0x000c5034
#define BCE_RXP_CPU_HW_BREAKPOINT_DISABLE		 (1L<<0)
#define BCE_RXP_CPU_HW_BREAKPOINT_ADDRESS		 (0x3fffffffL<<2)

#define BCE_RXP_CPU_REG_FILE				0x000c5200
#define BCE_RXP_CFTQ_DATA				0x000c5380
#define BCE_RXP_CFTQ_CMD				0x000c53b8
#define BCE_RXP_CFTQ_CMD_OFFSET			 (0x3ffL<<0)
#define BCE_RXP_CFTQ_CMD_WR_TOP			 (1L<<10)
#define BCE_RXP_CFTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_RXP_CFTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_RXP_CFTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_RXP_CFTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_RXP_CFTQ_CMD_ADD_INTERVEN			 (1L<<27)
#define BCE_RXP_CFTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_RXP_CFTQ_CMD_INTERVENE_CLR			 (1L<<29)
#define BCE_RXP_CFTQ_CMD_POP				 (1L<<30)
#define BCE_RXP_CFTQ_CMD_BUSY				 (1L<<31)

#define BCE_RXP_CFTQ_CTL				0x000c53bc
#define BCE_RXP_CFTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_RXP_CFTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_RXP_CFTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_RXP_CFTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_RXP_CFTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)

#define BCE_RXP_FTQ_DATA				0x000c53c0
#define BCE_RXP_FTQ_CMD				0x000c53f8
#define BCE_RXP_FTQ_CMD_OFFSET				 (0x3ffL<<0)
#define BCE_RXP_FTQ_CMD_WR_TOP				 (1L<<10)
#define BCE_RXP_FTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_RXP_FTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_RXP_FTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_RXP_FTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_RXP_FTQ_CMD_ADD_INTERVEN			 (1L<<27)
#define BCE_RXP_FTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_RXP_FTQ_CMD_INTERVENE_CLR			 (1L<<29)
#define BCE_RXP_FTQ_CMD_POP				 (1L<<30)
#define BCE_RXP_FTQ_CMD_BUSY				 (1L<<31)

#define BCE_RXP_FTQ_CTL				0x000c53fc
#define BCE_RXP_FTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_RXP_FTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_RXP_FTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_RXP_FTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_RXP_FTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)

#define BCE_RXP_SCRATCH				0x000e0000


/*
 *  com_reg definition
 *  offset: 0x100000
 */
#define BCE_COM_CPU_MODE				0x00105000
#define BCE_COM_CPU_MODE_LOCAL_RST			 (1L<<0)
#define BCE_COM_CPU_MODE_STEP_ENA			 (1L<<1)
#define BCE_COM_CPU_MODE_PAGE_0_DATA_ENA		 (1L<<2)
#define BCE_COM_CPU_MODE_PAGE_0_INST_ENA		 (1L<<3)
#define BCE_COM_CPU_MODE_MSG_BIT1			 (1L<<6)
#define BCE_COM_CPU_MODE_INTERRUPT_ENA			 (1L<<7)
#define BCE_COM_CPU_MODE_SOFT_HALT			 (1L<<10)
#define BCE_COM_CPU_MODE_BAD_DATA_HALT_ENA		 (1L<<11)
#define BCE_COM_CPU_MODE_BAD_INST_HALT_ENA		 (1L<<12)
#define BCE_COM_CPU_MODE_FIO_ABORT_HALT_ENA		 (1L<<13)
#define BCE_COM_CPU_MODE_SPAD_UNDERFLOW_HALT_ENA	 (1L<<15)

#define BCE_COM_CPU_STATE				0x00105004
#define BCE_COM_CPU_STATE_BREAKPOINT			 (1L<<0)
#define BCE_COM_CPU_STATE_BAD_INST_HALTED		 (1L<<2)
#define BCE_COM_CPU_STATE_PAGE_0_DATA_HALTED		 (1L<<3)
#define BCE_COM_CPU_STATE_PAGE_0_INST_HALTED		 (1L<<4)
#define BCE_COM_CPU_STATE_BAD_DATA_ADDR_HALTED		 (1L<<5)
#define BCE_COM_CPU_STATE_BAD_pc_HALTED		 (1L<<6)
#define BCE_COM_CPU_STATE_ALIGN_HALTED			 (1L<<7)
#define BCE_COM_CPU_STATE_FIO_ABORT_HALTED		 (1L<<8)
#define BCE_COM_CPU_STATE_SOFT_HALTED			 (1L<<10)
#define BCE_COM_CPU_STATE_SPAD_UNDERFLOW		 (1L<<11)
#define BCE_COM_CPU_STATE_INTERRRUPT			 (1L<<12)
#define BCE_COM_CPU_STATE_DATA_ACCESS_STALL		 (1L<<14)
#define BCE_COM_CPU_STATE_INST_FETCH_STALL		 (1L<<15)
#define BCE_COM_CPU_STATE_BLOCKED_READ			 (1L<<31)

#define BCE_COM_CPU_EVENT_MASK				0x00105008
#define BCE_COM_CPU_EVENT_MASK_BREAKPOINT_MASK		 (1L<<0)
#define BCE_COM_CPU_EVENT_MASK_BAD_INST_HALTED_MASK	 (1L<<2)
#define BCE_COM_CPU_EVENT_MASK_PAGE_0_DATA_HALTED_MASK	 (1L<<3)
#define BCE_COM_CPU_EVENT_MASK_PAGE_0_INST_HALTED_MASK	 (1L<<4)
#define BCE_COM_CPU_EVENT_MASK_BAD_DATA_ADDR_HALTED_MASK	 (1L<<5)
#define BCE_COM_CPU_EVENT_MASK_BAD_PC_HALTED_MASK	 (1L<<6)
#define BCE_COM_CPU_EVENT_MASK_ALIGN_HALTED_MASK	 (1L<<7)
#define BCE_COM_CPU_EVENT_MASK_FIO_ABORT_MASK		 (1L<<8)
#define BCE_COM_CPU_EVENT_MASK_SOFT_HALTED_MASK	 (1L<<10)
#define BCE_COM_CPU_EVENT_MASK_SPAD_UNDERFLOW_MASK	 (1L<<11)
#define BCE_COM_CPU_EVENT_MASK_INTERRUPT_MASK		 (1L<<12)

#define BCE_COM_CPU_PROGRAM_COUNTER			0x0010501c
#define BCE_COM_CPU_INSTRUCTION			0x00105020
#define BCE_COM_CPU_DATA_ACCESS			0x00105024
#define BCE_COM_CPU_INTERRUPT_ENABLE			0x00105028
#define BCE_COM_CPU_INTERRUPT_VECTOR			0x0010502c
#define BCE_COM_CPU_INTERRUPT_SAVED_PC			0x00105030
#define BCE_COM_CPU_HW_BREAKPOINT			0x00105034
#define BCE_COM_CPU_HW_BREAKPOINT_DISABLE		 (1L<<0)
#define BCE_COM_CPU_HW_BREAKPOINT_ADDRESS		 (0x3fffffffL<<2)

#define BCE_COM_CPU_REG_FILE				0x00105200
#define BCE_COM_COMXQ_FTQ_DATA				0x00105340
#define BCE_COM_COMXQ_FTQ_CMD				0x00105378
#define BCE_COM_COMXQ_FTQ_CMD_OFFSET			 (0x3ffL<<0)
#define BCE_COM_COMXQ_FTQ_CMD_WR_TOP			 (1L<<10)
#define BCE_COM_COMXQ_FTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_COM_COMXQ_FTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_COM_COMXQ_FTQ_CMD_SFT_RESET		 (1L<<25)
#define BCE_COM_COMXQ_FTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_COM_COMXQ_FTQ_CMD_ADD_INTERVEN		 (1L<<27)
#define BCE_COM_COMXQ_FTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_COM_COMXQ_FTQ_CMD_INTERVENE_CLR		 (1L<<29)
#define BCE_COM_COMXQ_FTQ_CMD_POP			 (1L<<30)
#define BCE_COM_COMXQ_FTQ_CMD_BUSY			 (1L<<31)

#define BCE_COM_COMXQ_FTQ_CTL				0x0010537c
#define BCE_COM_COMXQ_FTQ_CTL_INTERVENE		 (1L<<0)
#define BCE_COM_COMXQ_FTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_COM_COMXQ_FTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_COM_COMXQ_FTQ_CTL_MAX_DEPTH		 (0x3ffL<<12)
#define BCE_COM_COMXQ_FTQ_CTL_CUR_DEPTH		 (0x3ffL<<22)

#define BCE_COM_COMTQ_FTQ_DATA				0x00105380
#define BCE_COM_COMTQ_FTQ_CMD				0x001053b8
#define BCE_COM_COMTQ_FTQ_CMD_OFFSET			 (0x3ffL<<0)
#define BCE_COM_COMTQ_FTQ_CMD_WR_TOP			 (1L<<10)
#define BCE_COM_COMTQ_FTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_COM_COMTQ_FTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_COM_COMTQ_FTQ_CMD_SFT_RESET		 (1L<<25)
#define BCE_COM_COMTQ_FTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_COM_COMTQ_FTQ_CMD_ADD_INTERVEN		 (1L<<27)
#define BCE_COM_COMTQ_FTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_COM_COMTQ_FTQ_CMD_INTERVENE_CLR		 (1L<<29)
#define BCE_COM_COMTQ_FTQ_CMD_POP			 (1L<<30)
#define BCE_COM_COMTQ_FTQ_CMD_BUSY			 (1L<<31)

#define BCE_COM_COMTQ_FTQ_CTL				0x001053bc
#define BCE_COM_COMTQ_FTQ_CTL_INTERVENE		 (1L<<0)
#define BCE_COM_COMTQ_FTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_COM_COMTQ_FTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_COM_COMTQ_FTQ_CTL_MAX_DEPTH		 (0x3ffL<<12)
#define BCE_COM_COMTQ_FTQ_CTL_CUR_DEPTH		 (0x3ffL<<22)

#define BCE_COM_COMQ_FTQ_DATA				0x001053c0
#define BCE_COM_COMQ_FTQ_CMD				0x001053f8
#define BCE_COM_COMQ_FTQ_CMD_OFFSET			 (0x3ffL<<0)
#define BCE_COM_COMQ_FTQ_CMD_WR_TOP			 (1L<<10)
#define BCE_COM_COMQ_FTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_COM_COMQ_FTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_COM_COMQ_FTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_COM_COMQ_FTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_COM_COMQ_FTQ_CMD_ADD_INTERVEN		 (1L<<27)
#define BCE_COM_COMQ_FTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_COM_COMQ_FTQ_CMD_INTERVENE_CLR		 (1L<<29)
#define BCE_COM_COMQ_FTQ_CMD_POP			 (1L<<30)
#define BCE_COM_COMQ_FTQ_CMD_BUSY			 (1L<<31)

#define BCE_COM_COMQ_FTQ_CTL				0x001053fc
#define BCE_COM_COMQ_FTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_COM_COMQ_FTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_COM_COMQ_FTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_COM_COMQ_FTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_COM_COMQ_FTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)

#define BCE_COM_SCRATCH				0x00120000


/*
 *  cp_reg definition
 *  offset: 0x180000
 */
#define BCE_CP_CPU_MODE				0x00185000
#define BCE_CP_CPU_MODE_LOCAL_RST			 (1L<<0)
#define BCE_CP_CPU_MODE_STEP_ENA			 (1L<<1)
#define BCE_CP_CPU_MODE_PAGE_0_DATA_ENA		 (1L<<2)
#define BCE_CP_CPU_MODE_PAGE_0_INST_ENA		 (1L<<3)
#define BCE_CP_CPU_MODE_MSG_BIT1			 (1L<<6)
#define BCE_CP_CPU_MODE_INTERRUPT_ENA			 (1L<<7)
#define BCE_CP_CPU_MODE_SOFT_HALT			 (1L<<10)
#define BCE_CP_CPU_MODE_BAD_DATA_HALT_ENA		 (1L<<11)
#define BCE_CP_CPU_MODE_BAD_INST_HALT_ENA		 (1L<<12)
#define BCE_CP_CPU_MODE_FIO_ABORT_HALT_ENA		 (1L<<13)
#define BCE_CP_CPU_MODE_SPAD_UNDERFLOW_HALT_ENA	 (1L<<15)

#define BCE_CP_CPU_STATE				0x00185004
#define BCE_CP_CPU_STATE_BREAKPOINT			 (1L<<0)
#define BCE_CP_CPU_STATE_BAD_INST_HALTED		 (1L<<2)
#define BCE_CP_CPU_STATE_PAGE_0_DATA_HALTED		 (1L<<3)
#define BCE_CP_CPU_STATE_PAGE_0_INST_HALTED		 (1L<<4)
#define BCE_CP_CPU_STATE_BAD_DATA_ADDR_HALTED		 (1L<<5)
#define BCE_CP_CPU_STATE_BAD_pc_HALTED			 (1L<<6)
#define BCE_CP_CPU_STATE_ALIGN_HALTED			 (1L<<7)
#define BCE_CP_CPU_STATE_FIO_ABORT_HALTED		 (1L<<8)
#define BCE_CP_CPU_STATE_SOFT_HALTED			 (1L<<10)
#define BCE_CP_CPU_STATE_SPAD_UNDERFLOW		 (1L<<11)
#define BCE_CP_CPU_STATE_INTERRRUPT			 (1L<<12)
#define BCE_CP_CPU_STATE_DATA_ACCESS_STALL		 (1L<<14)
#define BCE_CP_CPU_STATE_INST_FETCH_STALL		 (1L<<15)
#define BCE_CP_CPU_STATE_BLOCKED_READ			 (1L<<31)

#define BCE_CP_CPU_EVENT_MASK				0x00185008
#define BCE_CP_CPU_EVENT_MASK_BREAKPOINT_MASK		 (1L<<0)
#define BCE_CP_CPU_EVENT_MASK_BAD_INST_HALTED_MASK	 (1L<<2)
#define BCE_CP_CPU_EVENT_MASK_PAGE_0_DATA_HALTED_MASK	 (1L<<3)
#define BCE_CP_CPU_EVENT_MASK_PAGE_0_INST_HALTED_MASK	 (1L<<4)
#define BCE_CP_CPU_EVENT_MASK_BAD_DATA_ADDR_HALTED_MASK	 (1L<<5)
#define BCE_CP_CPU_EVENT_MASK_BAD_PC_HALTED_MASK	 (1L<<6)
#define BCE_CP_CPU_EVENT_MASK_ALIGN_HALTED_MASK	 (1L<<7)
#define BCE_CP_CPU_EVENT_MASK_FIO_ABORT_MASK		 (1L<<8)
#define BCE_CP_CPU_EVENT_MASK_SOFT_HALTED_MASK		 (1L<<10)
#define BCE_CP_CPU_EVENT_MASK_SPAD_UNDERFLOW_MASK	 (1L<<11)
#define BCE_CP_CPU_EVENT_MASK_INTERRUPT_MASK		 (1L<<12)

#define BCE_CP_CPU_PROGRAM_COUNTER			0x0018501c
#define BCE_CP_CPU_INSTRUCTION				0x00185020
#define BCE_CP_CPU_DATA_ACCESS				0x00185024
#define BCE_CP_CPU_INTERRUPT_ENABLE			0x00185028
#define BCE_CP_CPU_INTERRUPT_VECTOR			0x0018502c
#define BCE_CP_CPU_INTERRUPT_SAVED_PC			0x00185030
#define BCE_CP_CPU_HW_BREAKPOINT			0x00185034
#define BCE_CP_CPU_HW_BREAKPOINT_DISABLE		 (1L<<0)
#define BCE_CP_CPU_HW_BREAKPOINT_ADDRESS		 (0x3fffffffL<<2)

#define BCE_CP_CPU_REG_FILE				0x00185200
#define BCE_CP_CPQ_FTQ_DATA				0x001853c0
#define BCE_CP_CPQ_FTQ_CMD				0x001853f8
#define BCE_CP_CPQ_FTQ_CMD_OFFSET			 (0x3ffL<<0)
#define BCE_CP_CPQ_FTQ_CMD_WR_TOP			 (1L<<10)
#define BCE_CP_CPQ_FTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_CP_CPQ_FTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_CP_CPQ_FTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_CP_CPQ_FTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_CP_CPQ_FTQ_CMD_ADD_INTERVEN		 (1L<<27)
#define BCE_CP_CPQ_FTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_CP_CPQ_FTQ_CMD_INTERVENE_CLR		 (1L<<29)
#define BCE_CP_CPQ_FTQ_CMD_POP				 (1L<<30)
#define BCE_CP_CPQ_FTQ_CMD_BUSY			 (1L<<31)

#define BCE_CP_CPQ_FTQ_CTL				0x001853fc
#define BCE_CP_CPQ_FTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_CP_CPQ_FTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_CP_CPQ_FTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_CP_CPQ_FTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_CP_CPQ_FTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)

#define BCE_CP_SCRATCH					0x001a0000


/*
 *  tas_reg definition
 *  offset: 0x1c0000
 */
#define BCE_TAS_FTQ_CMD						0x001c03f8
#define BCE_TAS_FTQ_CTL						0x001c03fc
#define BCE_TAS_FTQ_CTL_MAX_DEPTH			(0x3ffL<<12)
#define BCE_TAS_FTQ_CTL_CUR_DEPTH			(0x3ffL<<22)


/*
 *  mcp_reg definition
 *  offset: 0x140000
 */
#define BCE_MCP_CPU_MODE				0x00145000
#define BCE_MCP_CPU_MODE_LOCAL_RST			 (1L<<0)
#define BCE_MCP_CPU_MODE_STEP_ENA			 (1L<<1)
#define BCE_MCP_CPU_MODE_PAGE_0_DATA_ENA		 (1L<<2)
#define BCE_MCP_CPU_MODE_PAGE_0_INST_ENA		 (1L<<3)
#define BCE_MCP_CPU_MODE_MSG_BIT1			 (1L<<6)
#define BCE_MCP_CPU_MODE_INTERRUPT_ENA			 (1L<<7)
#define BCE_MCP_CPU_MODE_SOFT_HALT			 (1L<<10)
#define BCE_MCP_CPU_MODE_BAD_DATA_HALT_ENA		 (1L<<11)
#define BCE_MCP_CPU_MODE_BAD_INST_HALT_ENA		 (1L<<12)
#define BCE_MCP_CPU_MODE_FIO_ABORT_HALT_ENA		 (1L<<13)
#define BCE_MCP_CPU_MODE_SPAD_UNDERFLOW_HALT_ENA	 (1L<<15)

#define BCE_MCP_CPU_STATE				0x00145004
#define BCE_MCP_CPU_STATE_BREAKPOINT			 (1L<<0)
#define BCE_MCP_CPU_STATE_BAD_INST_HALTED		 (1L<<2)
#define BCE_MCP_CPU_STATE_PAGE_0_DATA_HALTED		 (1L<<3)
#define BCE_MCP_CPU_STATE_PAGE_0_INST_HALTED		 (1L<<4)
#define BCE_MCP_CPU_STATE_BAD_DATA_ADDR_HALTED		 (1L<<5)
#define BCE_MCP_CPU_STATE_BAD_pc_HALTED		 (1L<<6)
#define BCE_MCP_CPU_STATE_ALIGN_HALTED			 (1L<<7)
#define BCE_MCP_CPU_STATE_FIO_ABORT_HALTED		 (1L<<8)
#define BCE_MCP_CPU_STATE_SOFT_HALTED			 (1L<<10)
#define BCE_MCP_CPU_STATE_SPAD_UNDERFLOW		 (1L<<11)
#define BCE_MCP_CPU_STATE_INTERRRUPT			 (1L<<12)
#define BCE_MCP_CPU_STATE_DATA_ACCESS_STALL		 (1L<<14)
#define BCE_MCP_CPU_STATE_INST_FETCH_STALL		 (1L<<15)
#define BCE_MCP_CPU_STATE_BLOCKED_READ			 (1L<<31)

#define BCE_MCP_CPU_EVENT_MASK				0x00145008
#define BCE_MCP_CPU_EVENT_MASK_BREAKPOINT_MASK		 (1L<<0)
#define BCE_MCP_CPU_EVENT_MASK_BAD_INST_HALTED_MASK	 (1L<<2)
#define BCE_MCP_CPU_EVENT_MASK_PAGE_0_DATA_HALTED_MASK	 (1L<<3)
#define BCE_MCP_CPU_EVENT_MASK_PAGE_0_INST_HALTED_MASK	 (1L<<4)
#define BCE_MCP_CPU_EVENT_MASK_BAD_DATA_ADDR_HALTED_MASK	 (1L<<5)
#define BCE_MCP_CPU_EVENT_MASK_BAD_PC_HALTED_MASK	 (1L<<6)
#define BCE_MCP_CPU_EVENT_MASK_ALIGN_HALTED_MASK	 (1L<<7)
#define BCE_MCP_CPU_EVENT_MASK_FIO_ABORT_MASK		 (1L<<8)
#define BCE_MCP_CPU_EVENT_MASK_SOFT_HALTED_MASK	 (1L<<10)
#define BCE_MCP_CPU_EVENT_MASK_SPAD_UNDERFLOW_MASK	 (1L<<11)
#define BCE_MCP_CPU_EVENT_MASK_INTERRUPT_MASK		 (1L<<12)

#define BCE_MCP_CPU_PROGRAM_COUNTER			0x0014501c
#define BCE_MCP_CPU_INSTRUCTION			0x00145020
#define BCE_MCP_CPU_DATA_ACCESS			0x00145024
#define BCE_MCP_CPU_INTERRUPT_ENABLE			0x00145028
#define BCE_MCP_CPU_INTERRUPT_VECTOR			0x0014502c
#define BCE_MCP_CPU_INTERRUPT_SAVED_PC			0x00145030
#define BCE_MCP_CPU_HW_BREAKPOINT			0x00145034
#define BCE_MCP_CPU_HW_BREAKPOINT_DISABLE		 (1L<<0)
#define BCE_MCP_CPU_HW_BREAKPOINT_ADDRESS		 (0x3fffffffL<<2)

#define BCE_MCP_CPU_REG_FILE				0x00145200
#define BCE_MCP_MCPQ_FTQ_DATA				0x001453c0
#define BCE_MCP_MCPQ_FTQ_CMD				0x001453f8
#define BCE_MCP_MCPQ_FTQ_CMD_OFFSET			 (0x3ffL<<0)
#define BCE_MCP_MCPQ_FTQ_CMD_WR_TOP			 (1L<<10)
#define BCE_MCP_MCPQ_FTQ_CMD_WR_TOP_0			 (0L<<10)
#define BCE_MCP_MCPQ_FTQ_CMD_WR_TOP_1			 (1L<<10)
#define BCE_MCP_MCPQ_FTQ_CMD_SFT_RESET			 (1L<<25)
#define BCE_MCP_MCPQ_FTQ_CMD_RD_DATA			 (1L<<26)
#define BCE_MCP_MCPQ_FTQ_CMD_ADD_INTERVEN		 (1L<<27)
#define BCE_MCP_MCPQ_FTQ_CMD_ADD_DATA			 (1L<<28)
#define BCE_MCP_MCPQ_FTQ_CMD_INTERVENE_CLR		 (1L<<29)
#define BCE_MCP_MCPQ_FTQ_CMD_POP			 (1L<<30)
#define BCE_MCP_MCPQ_FTQ_CMD_BUSY			 (1L<<31)

#define BCE_MCP_MCPQ_FTQ_CTL				0x001453fc
#define BCE_MCP_MCPQ_FTQ_CTL_INTERVENE			 (1L<<0)
#define BCE_MCP_MCPQ_FTQ_CTL_OVERFLOW			 (1L<<1)
#define BCE_MCP_MCPQ_FTQ_CTL_FORCE_INTERVENE		 (1L<<2)
#define BCE_MCP_MCPQ_FTQ_CTL_MAX_DEPTH			 (0x3ffL<<12)
#define BCE_MCP_MCPQ_FTQ_CTL_CUR_DEPTH			 (0x3ffL<<22)

#define BCE_MCP_ROM								0x00150000
#define BCE_MCP_SCRATCH							0x00160000

#define BCE_SHM_HDR_SIGNATURE					BCE_MCP_SCRATCH
#define BCE_SHM_HDR_SIGNATURE_SIG_MASK			0xffff0000
#define BCE_SHM_HDR_SIGNATURE_SIG				0x53530000
#define BCE_SHM_HDR_SIGNATURE_VER_MASK			0x000000ff
#define BCE_SHM_HDR_SIGNATURE_VER_ONE			0x00000001

#define BCE_SHM_HDR_ADDR_0				BCE_MCP_SCRATCH + 4
#define BCE_SHM_HDR_ADDR_1				BCE_MCP_SCRATCH + 8

/****************************************************************************/
/* End machine generated definitions.                                     */
/****************************************************************************/

/****************************************************************************/
/* Begin firmware definitions.                                              */
/****************************************************************************/
/* The following definitions refer to pre-defined locations in processor    */
/* memory space which allows the driver to enable particular functionality  */
/* within the firmware or read specific information about the running        */
/* firmware.                                                                */
/****************************************************************************/

/*
 * Perfect match control register.
 * 0 = Default.  All received unicst packets matching MAC address
 *     BCE_EMAC_MAC_MATCH[0:1,8:9,10:11,12:13,14:15] are sent to receive queue
 *     0, all other perfect match registers are reserved.
 * 1 = All received unicast packets matching MAC address
 *     BCE_EMAC_MAC_MATCH[0:1] are mapped to receive queue 0,
 *     BCE_EMAC_MAC_MATCH[2:3] is mapped to receive queue 1, etc.
 * 2 = All received unicast packets matching any BCE_EMAC_MAC_MATCH[] register
 *     are sent to receive queue 0.
 */
#define BCE_RXP_PM_CTRL			0x0e00d0

/*
 * This firmware statistic records the number of frames that
 * were dropped because there were no buffers available in the
 * receive chain.
 */
#define BCE_COM_NO_BUFFERS		0x120084
/****************************************************************************/
/* End firmware definitions.                                                */
/****************************************************************************/

#define NUM_MC_HASH_REGISTERS   8

#define DMA_READ_CHANS	5
#define DMA_WRITE_CHANS	3

/* Use the natural page size of the host CPU. */
/* XXX: This has only been tested on amd64/i386 systems using 4KB pages. */
#define BCM_PAGE_BITS	PAGE_SHIFT
#define BCM_PAGE_SIZE	PAGE_SIZE
#define BCM_PAGE_MASK	(BCM_PAGE_SIZE - 1)
#define BCM_PAGES(x)	((((x) + BCM_PAGE_SIZE - 1) & \
    BCM_PAGE_MASK) >> BCM_PAGE_BITS)

/*
 * Page count must remain a power of 2 for all
 * of the math to work correctly.
 */
#define DEFAULT_TX_PAGES		2
#define MAX_TX_PAGES			8
#define TOTAL_TX_BD_PER_PAGE	(BCM_PAGE_SIZE / sizeof(struct tx_bd))
#define USABLE_TX_BD_PER_PAGE	(TOTAL_TX_BD_PER_PAGE - 1)
#define MAX_TX_BD_AVAIL		(MAX_TX_PAGES * TOTAL_TX_BD_PER_PAGE)
#define TOTAL_TX_BD_ALLOC		(TOTAL_TX_BD_PER_PAGE * sc->tx_pages)
#define USABLE_TX_BD_ALLOC		(USABLE_TX_BD_PER_PAGE * sc->tx_pages)
#define MAX_TX_BD_ALLOC		(TOTAL_TX_BD_ALLOC - 1)

/* Advance to the next tx_bd, skipping any next page pointers. */
#define NEXT_TX_BD(x) (((x) & USABLE_TX_BD_PER_PAGE) ==	\
    (USABLE_TX_BD_PER_PAGE - 1)) ? (x) + 2 : (x) + 1

#define TX_CHAIN_IDX(x) ((x) & MAX_TX_BD_ALLOC)

#define TX_PAGE(x) (((x) & ~USABLE_TX_BD_PER_PAGE) >> (BCM_PAGE_BITS - 4))
#define TX_IDX(x) ((x) & USABLE_TX_BD_PER_PAGE)

/*
 * Page count must remain a power of 2 for all
 * of the math to work correctly.
 */
#define DEFAULT_RX_PAGES		2
#define MAX_RX_PAGES			8
#define TOTAL_RX_BD_PER_PAGE	(BCM_PAGE_SIZE / sizeof(struct rx_bd))
#define USABLE_RX_BD_PER_PAGE	(TOTAL_RX_BD_PER_PAGE - 1)
#define MAX_RX_BD_AVAIL		(MAX_RX_PAGES * TOTAL_RX_BD_PER_PAGE)
#define TOTAL_RX_BD_ALLOC		(TOTAL_RX_BD_PER_PAGE * sc->rx_pages)
#define USABLE_RX_BD_ALLOC		(USABLE_RX_BD_PER_PAGE * sc->rx_pages)
#define MAX_RX_BD_ALLOC		(TOTAL_RX_BD_ALLOC - 1)

/* Advance to the next rx_bd, skipping any next page pointers. */
#define NEXT_RX_BD(x) (((x) & USABLE_RX_BD_PER_PAGE) ==	\
    (USABLE_RX_BD_PER_PAGE - 1)) ? (x) + 2 : (x) + 1

#define RX_CHAIN_IDX(x) ((x) & MAX_RX_BD_ALLOC)

#define RX_PAGE(x) (((x) & ~USABLE_RX_BD_PER_PAGE) >> (BCM_PAGE_BITS - 4))
#define RX_IDX(x) ((x) & USABLE_RX_BD_PER_PAGE)

/*
 * To accommodate jumbo frames, the page chain should
 * be 4 times larger than the receive chain.
 */
#define DEFAULT_PG_PAGES		(DEFAULT_RX_PAGES * 4)
#define MAX_PG_PAGES			(MAX_RX_PAGES * 4)
#define TOTAL_PG_BD_PER_PAGE	(BCM_PAGE_SIZE / sizeof(struct rx_bd))
#define USABLE_PG_BD_PER_PAGE	(TOTAL_PG_BD_PER_PAGE - 1)
#define MAX_PG_BD_AVAIL		(MAX_PG_PAGES * TOTAL_PG_BD_PER_PAGE)
#define TOTAL_PG_BD_ALLOC		(TOTAL_PG_BD_PER_PAGE * sc->pg_pages)
#define USABLE_PG_BD_ALLOC		(USABLE_PG_BD_PER_PAGE * sc->pg_pages)
#define MAX_PG_BD_ALLOC		(TOTAL_PG_BD_ALLOC - 1)

/* Advance to the next pg_bd, skipping any next page pointers. */
#define NEXT_PG_BD(x) (((x) & USABLE_PG_BD_PER_PAGE) ==	\
    (USABLE_PG_BD_PER_PAGE - 1)) ? (x) + 2 : (x) + 1

#define PG_CHAIN_IDX(x) ((x) & MAX_PG_BD_ALLOC)

#define PG_PAGE(x) (((x) & ~USABLE_PG_BD_PER_PAGE) >> (BCM_PAGE_BITS - 4))
#define PG_IDX(x) ((x) & USABLE_PG_BD_PER_PAGE)

#define CTX_INIT_RETRY_COUNT        10

/* Context size. */
#define CTX_SHIFT		7
#define CTX_SIZE		(1 << CTX_SHIFT)
#define CTX_MASK		(CTX_SIZE - 1)
#define GET_CID_ADDR(_cid)	((_cid) << CTX_SHIFT)
#define GET_CID(_cid_addr)	((_cid_addr) >> CTX_SHIFT)

#define PHY_CTX_SHIFT		6
#define PHY_CTX_SIZE		(1 << PHY_CTX_SHIFT)
#define PHY_CTX_MASK		(PHY_CTX_SIZE - 1)
#define GET_PCID_ADDR(_pcid)	((_pcid) << PHY_CTX_SHIFT)
#define GET_PCID(_pcid_addr)	((_pcid_addr) >> PHY_CTX_SHIFT)

#define MB_KERNEL_CTX_SHIFT	8
#define MB_KERNEL_CTX_SIZE	(1 << MB_KERNEL_CTX_SHIFT)
#define MB_KERNEL_CTX_MASK	(MB_KERNEL_CTX_SIZE - 1)
#define MB_GET_CID_ADDR(_cid)	(0x10000 + ((_cid) << MB_KERNEL_CTX_SHIFT))

#define MAX_CID_CNT		0x4000
#define MAX_CID_ADDR		(GET_CID_ADDR(MAX_CID_CNT))
#define INVALID_CID_ADDR	0xffffffff

#define TX_CID			16
#define RX_CID			0

#define DEFAULT_TX_QUICK_CONS_TRIP_INT	20
#define DEFAULT_TX_QUICK_CONS_TRIP		20
#define DEFAULT_TX_TICKS_INT			80
#define DEFAULT_TX_TICKS				80
#define DEFAULT_RX_QUICK_CONS_TRIP_INT	6
#define DEFAULT_RX_QUICK_CONS_TRIP		6
#define DEFAULT_RX_TICKS_INT			18
#define DEFAULT_RX_TICKS				18

/****************************************************************************/
/* BCE Processor Firmwware Load Definitions                                 */
/****************************************************************************/

struct cpu_reg {
	u32 mode;
	u32 mode_value_halt;
	u32 mode_value_sstep;

	u32 state;
	u32 state_value_clear;

	u32 gpr0;
	u32 evmask;
	u32 pc;
	u32 inst;
	u32 bp;

	u32 spad_base;

	u32 mips_view_base;
};

struct fw_info {
	u32 ver_major;
	u32 ver_minor;
	u32 ver_fix;

	u32 start_addr;

	/* Text section. */
	u32 text_addr;
	u32 text_len;
	u32 text_index;
	const u32 *text;

	/* Data section. */
	u32 data_addr;
	u32 data_len;
	u32 data_index;
	const u32 *data;

	/* SBSS section. */
	u32 sbss_addr;
	u32 sbss_len;
	u32 sbss_index;
	const u32 *sbss;

	/* BSS section. */
	u32 bss_addr;
	u32 bss_len;
	u32 bss_index;
	const u32 *bss;

	/* Read-only section. */
	u32 rodata_addr;
	u32 rodata_len;
	u32 rodata_index;
	const u32 *rodata;
};

#define RV2P_PROC1		0
#define RV2P_PROC2		1

#define BCE_MIREG(x)		((x & 0x1F) << 16)
#define BCE_MIPHY(x)		((x & 0x1F) << 21)
#define BCE_PHY_TIMEOUT		50

#define BCE_NVRAM_SIZE		0x200
#define BCE_NVRAM_MAGIC		0x669955aa
#define BCE_CRC32_RESIDUAL	0xdebb20e3

#define BCE_TX_TIMEOUT		5

#define BCE_MAX_SEGMENTS	35
#define BCE_TSO_MAX_SIZE	(65535 + sizeof(struct ether_vlan_header))
#define BCE_TSO_MAX_SEG_SIZE	4096

#define BCE_DMA_ALIGN		8
#define BCE_DMA_BOUNDARY	0
#define BCE_RX_BUF_ALIGN	16

#define BCE_MAX_CONTEXT		4

/* The BCM5708 has a problem with addresses greater that 40bits. */
/* Handle the sizing issue in an architecture agnostic fashion.  */
#if (BUS_SPACE_MAXADDR < 0xFFFFFFFFFF)
#define BCE_BUS_SPACE_MAXADDR		BUS_SPACE_MAXADDR
#else
#define BCE_BUS_SPACE_MAXADDR		0xFFFFFFFFFF
#endif

/*
 * XXX Checksum offload involving IP fragments seems to cause problems on
 * transmit.  Disable it for now, hopefully there will be a more elegant
 * solution later.
 */
#ifdef BCE_IP_CSUM
#define BCE_IF_HWASSIST	(CSUM_IP | CSUM_TCP | CSUM_UDP)
#else
#define BCE_IF_HWASSIST	(CSUM_TCP | CSUM_UDP)
#endif

#if __FreeBSD_version < 700000
#define BCE_IF_CAPABILITIES (IFCAP_VLAN_MTU | 			\
    IFCAP_VLAN_HWTAGGING | IFCAP_HWCSUM | IFCAP_JUMBO_MTU)
#else
#define BCE_IF_CAPABILITIES (IFCAP_VLAN_MTU |			\
    IFCAP_VLAN_HWTAGGING | IFCAP_HWCSUM |			\
    IFCAP_JUMBO_MTU | IFCAP_VLAN_HWCSUM)
#endif

#define BCE_MIN_MTU			60
#define BCE_MIN_ETHER_MTU		64

#define BCE_MAX_STD_MTU			1500
#define BCE_MAX_STD_ETHER_MTU		1518
#define BCE_MAX_STD_ETHER_MTU_VLAN	1522

#define BCE_MAX_JUMBO_MTU		9000
#define BCE_MAX_JUMBO_ETHER_MTU		9018
#define BCE_MAX_JUMBO_ETHER_MTU_VLAN 	9022

// #define BCE_MAX_MTU		ETHER_MAX_LEN_JUMBO + ETHER_VLAN_ENCAP_LEN	/* 9022 */

/****************************************************************************/
/* BCE Device State Data Structure                                          */
/****************************************************************************/

#define BCE_STATUS_BLK_SZ	sizeof(struct status_block)
#define BCE_STATS_BLK_SZ	sizeof(struct statistics_block)
#define BCE_TX_CHAIN_PAGE_SZ	BCM_PAGE_SIZE
#define BCE_RX_CHAIN_PAGE_SZ	BCM_PAGE_SIZE
#define BCE_PG_CHAIN_PAGE_SZ	BCM_PAGE_SIZE

struct bce_softc
{
	struct mtx		bce_mtx;

	/* Interface info */
	struct ifnet		*bce_ifp;

	/* Parent device handle */
	device_t		bce_dev;

	/* Interface number */
	u_int8_t		bce_unit;

	/* Device resource handle */
	struct resource		*bce_res_mem;

	/* TBI media info */
	struct ifmedia		bce_ifmedia;

	/* Device bus tag */
	bus_space_tag_t		bce_btag;

	/* Device bus handle */
	bus_space_handle_t	bce_bhandle;

	/* Device virtual memory handle */
	vm_offset_t		bce_vhandle;

	/* IRQ Resource Handle */
	struct resource		*bce_res_irq;

	/* Interrupt handler. */
	void			*bce_intrhand;

	/* ASIC Chip ID. */
	u32			bce_chipid;

	/* General controller flags. */
	u32			bce_flags;
#define BCE_PCIX_FLAG				0x00000001
#define BCE_PCI_32BIT_FLAG 			0x00000002
#define BCE_RESERVED_FLAG			0x00000004
#define BCE_NO_WOL_FLAG				0x00000008
#define BCE_USING_DAC_FLAG			0x00000010
#define BCE_USING_MSI_FLAG 			0x00000020
#define BCE_MFW_ENABLE_FLAG			0x00000040
#define BCE_ONE_SHOT_MSI_FLAG			0x00000080
#define BCE_USING_MSIX_FLAG			0x00000100
#define BCE_PCIE_FLAG				0x00000200
#define BCE_USING_TX_FLOW_CONTROL		0x00000400
#define BCE_USING_RX_FLOW_CONTROL		0x00000800

	/* Controller capability flags. */
	u32			bce_cap_flags;
#define BCE_MSI_CAPABLE_FLAG			0x00000001
#define BCE_MSIX_CAPABLE_FLAG			0x00000002
#define BCE_PCIE_CAPABLE_FLAG			0x00000004
#define BCE_PCIX_CAPABLE_FLAG			0x00000008

	/* PHY specific flags. */
	u32			bce_phy_flags;
#define BCE_PHY_SERDES_FLAG			0x00000001
#define BCE_PHY_CRC_FIX_FLAG			0x00000002
#define BCE_PHY_PARALLEL_DETECT_FLAG		0x00000004
#define BCE_PHY_2_5G_CAPABLE_FLAG		0x00000008
#define BCE_PHY_INT_MODE_MASK_FLAG		0x00000300
#define BCE_PHY_INT_MODE_AUTO_POLLING_FLAG	0x00000100
#define BCE_PHY_INT_MODE_LINK_READY_FLAG	0x00000200
#define BCE_PHY_IEEE_CLAUSE_45_FLAG		0x00000400
#define	BCE_PHY_REMOTE_CAP_FLAG			0x00000800
#define	BCE_PHY_REMOTE_PORT_FIBER_FLAG		0x00001000

	/* Values that need to be shared with the PHY driver. */
	u32			bce_shared_hw_cfg;
	u32			bce_port_hw_cfg;

	bus_addr_t		max_bus_addr;

	/* PCI bus speed */
	u16			bus_speed_mhz;

	/* PCIe link width */
	u16			link_width;

	/* PCIe link speed */
	u16			link_speed;

	/* Flash NVRAM settings */
	const struct flash_spec	*bce_flash_info;

	/* Flash NVRAM size */
	u32			bce_flash_size;

	/* Shared Memory base address */
	u32			bce_shmem_base;

	/* Name string */
	const char		*bce_name;

	/* Tracks the version of bootcode firmware. */
	char			bce_bc_ver[32];

	/* Tracks the version of management firmware. */
	char			bce_mfw_ver[32];

	/*
	 * Tracks the state of the firmware.  0 = Running while any
	 * other value indicates that the firmware is not responding.
	 */
	u16			bce_fw_timed_out;

	/*
	 * An incrementing sequence used to coordinate messages passed
	 * from the driver to the firmware.
	 */
	u16			bce_fw_wr_seq;

	/*
	 * An incrementing sequence used to let the firmware know that
	 * the driver is still operating.  Without the pulse, management
	 * firmware such as IPMI or UMP will operate in OS absent state.
	 */
	u16			bce_fw_drv_pulse_wr_seq;

	/* Tracks whether firmware has lost the driver's pulse. */
	u16			bce_drv_cardiac_arrest;

	/* Ethernet MAC address. */
	u_char			eaddr[6];

	/*
	 * These setting are used by the host coalescing (HC) block to
	 * to control how often the status block, statistics block and
	 * interrupts are generated.
	 */
	u16			bce_tx_quick_cons_trip_int;
	u16			bce_tx_quick_cons_trip;
	u16			bce_rx_quick_cons_trip_int;
	u16			bce_rx_quick_cons_trip;
	u16			bce_tx_ticks_int;
	u16			bce_tx_ticks;
	u16			bce_rx_ticks_int;
	u16			bce_rx_ticks;
	u32			bce_stats_ticks;

	/* The address of the integrated PHY on the MII bus. */
	int			bce_phy_addr;

	/* The device handle for the MII bus child device. */
	device_t		bce_miibus;

	/* Driver maintained RX chain pointers and byte counter. */
	u16			rx_prod;
	u16			rx_cons;

	/* Counts the bytes used in the RX chain. */
	u32			rx_prod_bseq;

	/* Driver maintained TX chain pointers and byte counter. */
	u16			tx_prod;
	u16			tx_cons;

	/* Counts the bytes used in the TX chain. */
	u32			tx_prod_bseq;

	/* Driver maintained PG chain pointers. */
	u16			pg_prod;
	u16			pg_cons;

	int			bce_link_up;
	struct		callout bce_tick_callout;
	struct		callout bce_pulse_callout;

	/* Ticks until chip reset */
	int			watchdog_timer;

	/* Frame size and mbuf allocation size for RX frames. */
	int			rx_bd_mbuf_alloc_size;
	int			rx_bd_mbuf_data_len;
	int			rx_bd_mbuf_align_pad;

	/* Receive mode settings (i.e promiscuous, multicast, etc.). */
	u32			rx_mode;

	/* Bus tag for the bce controller. */
	bus_dma_tag_t		parent_tag;

	/* H/W maintained TX buffer descriptor chain structure. */
	int					tx_pages;
	bus_dma_tag_t		tx_bd_chain_tag;
	bus_dmamap_t		tx_bd_chain_map[MAX_TX_PAGES];
	struct tx_bd		*tx_bd_chain[MAX_TX_PAGES];
	bus_addr_t			tx_bd_chain_paddr[MAX_TX_PAGES];

	/* H/W maintained RX buffer descriptor chain structure. */
	int					rx_pages;
	bus_dma_tag_t		rx_bd_chain_tag;
	bus_dmamap_t		rx_bd_chain_map[MAX_RX_PAGES];
	struct rx_bd		*rx_bd_chain[MAX_RX_PAGES];
	bus_addr_t			rx_bd_chain_paddr[MAX_RX_PAGES];

	/* H/W maintained page buffer descriptor chain structure. */
	int					pg_pages;
	bus_dma_tag_t		pg_bd_chain_tag;
	bus_dmamap_t		pg_bd_chain_map[MAX_PG_PAGES];
	struct rx_bd		*pg_bd_chain[MAX_PG_PAGES];
	bus_addr_t			pg_bd_chain_paddr[MAX_PG_PAGES];

	/* H/W maintained status block. */
	bus_dma_tag_t		status_tag;
	bus_dmamap_t		status_map;
	struct status_block	*status_block;
	bus_addr_t			status_block_paddr;

	/* Driver maintained status block values. */
	u16			last_status_idx;
	u16			hw_rx_cons;
	u16			hw_tx_cons;

	/* H/W maintained statistics block. */
	bus_dma_tag_t		stats_tag;
	bus_dmamap_t		stats_map;
	struct statistics_block *stats_block;
	bus_addr_t			stats_block_paddr;

	/* H/W maintained context block. */
	int					ctx_pages;
	bus_dma_tag_t		ctx_tag;

	/* BCM5709/16 use host memory for context. */
	bus_dmamap_t		ctx_map[BCE_MAX_CONTEXT];
	void				*ctx_block[BCE_MAX_CONTEXT];
	bus_addr_t			ctx_paddr[BCE_MAX_CONTEXT];

	/* Bus tag for RX/TX mbufs. */
	bus_dma_tag_t		rx_mbuf_tag;
	bus_dma_tag_t		tx_mbuf_tag;
	bus_dma_tag_t		pg_mbuf_tag;

	/* S/W maintained mbuf TX chain structure. */
	bus_dmamap_t		tx_mbuf_map[MAX_TX_BD_AVAIL];
	struct mbuf			*tx_mbuf_ptr[MAX_TX_BD_AVAIL];

	/* S/W maintained mbuf RX chain structure. */
	bus_dmamap_t		rx_mbuf_map[MAX_RX_BD_AVAIL];
	struct mbuf			*rx_mbuf_ptr[MAX_RX_BD_AVAIL];

	/* S/W maintained mbuf page chain structure. */
	bus_dmamap_t		pg_mbuf_map[MAX_PG_BD_AVAIL];
	struct mbuf			*pg_mbuf_ptr[MAX_PG_BD_AVAIL];

	/* Track the number of buffer descriptors in use. */
	u16			free_rx_bd;
	u16			max_rx_bd;
	u16			used_tx_bd;
	u16			max_tx_bd;
	u16			free_pg_bd;
	u16			max_pg_bd;

	/* Provides access to hardware statistics through sysctl. */
	u64			stat_IfHCInOctets;
	u64			stat_IfHCInBadOctets;
	u64			stat_IfHCOutOctets;
	u64			stat_IfHCOutBadOctets;
	u64			stat_IfHCInUcastPkts;
	u64			stat_IfHCInMulticastPkts;
	u64			stat_IfHCInBroadcastPkts;
	u64			stat_IfHCOutUcastPkts;
	u64			stat_IfHCOutMulticastPkts;
	u64			stat_IfHCOutBroadcastPkts;

	u32	stat_emac_tx_stat_dot3statsinternalmactransmiterrors;
	u32			stat_Dot3StatsCarrierSenseErrors;
	u32			stat_Dot3StatsFCSErrors;
	u32			stat_Dot3StatsAlignmentErrors;
	u32			stat_Dot3StatsSingleCollisionFrames;
	u32			stat_Dot3StatsMultipleCollisionFrames;
	u32			stat_Dot3StatsDeferredTransmissions;
	u32			stat_Dot3StatsExcessiveCollisions;
	u32			stat_Dot3StatsLateCollisions;
	u32			stat_EtherStatsCollisions;
	u32			stat_EtherStatsFragments;
	u32			stat_EtherStatsJabbers;
	u32			stat_EtherStatsUndersizePkts;
	u32			stat_EtherStatsOversizePkts;
	u32			stat_EtherStatsPktsRx64Octets;
	u32			stat_EtherStatsPktsRx65Octetsto127Octets;
	u32			stat_EtherStatsPktsRx128Octetsto255Octets;
	u32			stat_EtherStatsPktsRx256Octetsto511Octets;
	u32			stat_EtherStatsPktsRx512Octetsto1023Octets;
	u32			stat_EtherStatsPktsRx1024Octetsto1522Octets;
	u32			stat_EtherStatsPktsRx1523Octetsto9022Octets;
	u32			stat_EtherStatsPktsTx64Octets;
	u32			stat_EtherStatsPktsTx65Octetsto127Octets;
	u32			stat_EtherStatsPktsTx128Octetsto255Octets;
	u32			stat_EtherStatsPktsTx256Octetsto511Octets;
	u32			stat_EtherStatsPktsTx512Octetsto1023Octets;
	u32			stat_EtherStatsPktsTx1024Octetsto1522Octets;
	u32			stat_EtherStatsPktsTx1523Octetsto9022Octets;
	u32			stat_XonPauseFramesReceived;
	u32			stat_XoffPauseFramesReceived;
	u32			stat_OutXonSent;
	u32			stat_OutXoffSent;
	u32			stat_FlowControlDone;
	u32			stat_MacControlFramesReceived;
	u32			stat_XoffStateEntered;
	u32			stat_IfInFramesL2FilterDiscards;
	u32			stat_IfInRuleCheckerDiscards;
	u32			stat_IfInFTQDiscards;
	u32			stat_IfInMBUFDiscards;
	u32			stat_IfInRuleCheckerP4Hit;
	u32			stat_CatchupInRuleCheckerDiscards;
	u32			stat_CatchupInFTQDiscards;
	u32			stat_CatchupInMBUFDiscards;
	u32			stat_CatchupInRuleCheckerP4Hit;

	/* Provides access to certain firmware statistics. */
	u32			com_no_buffers;

	/* Recoverable failure counters. */
	u32			mbuf_alloc_failed_count;
	u32			mbuf_frag_count;
	u32			unexpected_attention_count;
	u32			l2fhdr_error_count;
	u32			dma_map_addr_tx_failed_count;
	u32			dma_map_addr_rx_failed_count;
	u32			watchdog_timeouts;

	/* Host coalescing block command register */
	u32			hc_command;

	/* Bootcode state */
	u32			bc_state;

#ifdef BCE_DEBUG
	/* Simulated recoverable failure counters. */
	u32			mbuf_alloc_failed_sim_count;
	u32			unexpected_attention_sim_count;
	u32			l2fhdr_error_sim_count;
	u32			dma_map_addr_failed_sim_count;

	/* Track the number of enqueued mbufs. */
	int			debug_tx_mbuf_alloc;
	int			debug_rx_mbuf_alloc;
	int			debug_pg_mbuf_alloc;

	/* Track how many and what type of interrupts are generated. */
	u64			interrupts_generated;
	u64			interrupts_handled;
	u64			interrupts_rx;
	u64			interrupts_tx;
	u64			phy_interrupts;

	/* Lowest number of rx_bd's free. */
	u16			rx_low_watermark;

	/* Number of times the RX chain was empty. */
	u64			rx_empty_count;

	/* Lowest number of pages free. */
	u16			pg_low_watermark;

	/* Number of times the page chain was empty. */
	u64			pg_empty_count;

	/* Greatest number of tx_bd's used. */
	u16			tx_hi_watermark;

	/* Number of times the TX chain was full. */
	u64			tx_full_count;

	/* Number of TSO frames requested. */
	u64			tso_frames_requested;

	/* Number of TSO frames completed. */
	u64			tso_frames_completed;

	/* Number of TSO frames failed. */
	u64			tso_frames_failed;

	/* Number of IP checksum offload frames.*/
	u64			csum_offload_ip;

	/* Number of TCP/UDP checksum offload frames.*/
	u64			csum_offload_tcp_udp;

	/* Number of VLAN tagged frames received. */
	u64			vlan_tagged_frames_rcvd;

	/* Number of VLAN tagged frames stripped. */
	u64			vlan_tagged_frames_stripped;

	/* Number of split header frames received. */
	u64			split_header_frames_rcvd;

	/* Number of split header TCP frames received. */
	u64			split_header_tcp_frames_rcvd;

	/* Buffer with NVRAM contents for the NIC. */
	u8			*nvram_buf;
#endif /* BCE_DEBUG */
};

#endif /* __BCEREG_H_DEFINED */
