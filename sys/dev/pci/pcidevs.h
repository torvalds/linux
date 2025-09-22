/*
 * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.
 *
 * generated from:
 *	OpenBSD: pcidevs,v 1.2111 2025/09/19 00:39:59 kevlo Exp 
 */
/*	$NetBSD: pcidevs,v 1.30 1997/06/24 06:20:24 thorpej Exp $	*/

/*
 * Copyright (c) 1995, 1996 Christopher G. Demetriou
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
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * NOTE: a fairly complete list of PCI codes can be found at:
 *
 *	http://www.pcidatabase.com/
 *
 * There is a Vendor ID search engine available at:
 *
 *	https://pcisig.com/membership/member-companies
 */

/*
 * List of known PCI vendors
 */

#define	PCI_VENDOR_MARTINMARIETTA	0x003d		/* Martin-Marietta */
#define	PCI_VENDOR_HAUPPAUGE	0x0070		/* Hauppauge */
#define	PCI_VENDOR_DLINK2	0x00ec		/* D-Link */
#define	PCI_VENDOR_TTTECH	0x0357		/* TTTech */
#define	PCI_VENDOR_DYNALINK	0x0675		/* Dynalink */
#define	PCI_VENDOR_COREGA2	0x07aa		/* Corega */
#define	PCI_VENDOR_RHINO	0x0b0b		/* Rhino Equipment */
#define	PCI_VENDOR_OPENBSD	0x0b5d		/* OpenBSD */
#define	PCI_VENDOR_COMPAQ	0x0e11		/* Compaq */
#define	PCI_VENDOR_SYMBIOS	0x1000		/* Symbios Logic */
#define	PCI_VENDOR_ATI	0x1002		/* ATI */
#define	PCI_VENDOR_ULSI	0x1003		/* ULSI Systems */
#define	PCI_VENDOR_VLSI	0x1004		/* VLSI */
#define	PCI_VENDOR_AVANCE	0x1005		/* Avance Logic */
#define	PCI_VENDOR_NS	0x100b		/* NS */
#define	PCI_VENDOR_TSENG	0x100c		/* Tseng Labs */
#define	PCI_VENDOR_WEITEK	0x100e		/* Weitek */
#define	PCI_VENDOR_DEC	0x1011		/* DEC */
#define	PCI_VENDOR_CIRRUS	0x1013		/* Cirrus Logic */
#define	PCI_VENDOR_IBM	0x1014		/* IBM */
#define	PCI_VENDOR_WD	0x101c		/* Western Digital */
#define	PCI_VENDOR_AMI	0x101e		/* AMI */
#define	PCI_VENDOR_AMD	0x1022		/* AMD */
#define	PCI_VENDOR_TRIDENT	0x1023		/* Trident */
#define	PCI_VENDOR_ACER	0x1025		/* Acer */
#define	PCI_VENDOR_DELL	0x1028		/* Dell */
#define	PCI_VENDOR_SNI	0x1029		/* Siemens Nixdorf AG */
#define	PCI_VENDOR_MATROX	0x102b		/* Matrox */
#define	PCI_VENDOR_CHIPS	0x102c		/* Chips and Technologies */
#define	PCI_VENDOR_TOSHIBA	0x102f		/* Toshiba */
#define	PCI_VENDOR_MIRO	0x1031		/* Miro Computer Products AG */
#define	PCI_VENDOR_NEC	0x1033		/* NEC */
#define	PCI_VENDOR_FUTUREDOMAIN	0x1036		/* Future Domain */
#define	PCI_VENDOR_HITACHI_M	0x1037		/* Hitachi Micro */
#define	PCI_VENDOR_SIS	0x1039		/* SiS */
#define	PCI_VENDOR_HP	0x103c		/* Hewlett-Packard */
#define	PCI_VENDOR_PCTECH	0x1042		/* PC Technology */
#define	PCI_VENDOR_ASUSTEK	0x1043		/* Asustek */
#define	PCI_VENDOR_DPT	0x1044		/* DPT */
#define	PCI_VENDOR_OPTI	0x1045		/* Opti */
#define	PCI_VENDOR_ELSA	0x1048		/* Elsa */
#define	PCI_VENDOR_SGSTHOMSON	0x104a		/* SGS Thomson */
#define	PCI_VENDOR_BUSLOGIC	0x104b		/* BusLogic */
#define	PCI_VENDOR_TI	0x104c		/* TI */
#define	PCI_VENDOR_SONY	0x104d		/* Sony */
#define	PCI_VENDOR_OAKTECH	0x104e		/* Oak Technology */
#define	PCI_VENDOR_WINBOND	0x1050		/* Winbond */
#define	PCI_VENDOR_HITACHI	0x1054		/* Hitachi */
#define	PCI_VENDOR_SMSC	0x1055		/* SMSC */
#define	PCI_VENDOR_MOT	0x1057		/* Motorola */
#define	PCI_VENDOR_PROMISE	0x105a		/* Promise */
#define	PCI_VENDOR_FOXCONN	0x105b		/* Foxconn */
#define	PCI_VENDOR_NUMBER9	0x105d		/* Number 9 */
#define	PCI_VENDOR_UMC	0x1060		/* UMC */
#define	PCI_VENDOR_ITT	0x1061		/* ITT */
#define	PCI_VENDOR_PICOPOWER	0x1066		/* Picopower */
#define	PCI_VENDOR_MYLEX	0x1069		/* Mylex */
#define	PCI_VENDOR_APPLE	0x106b		/* Apple */
#define	PCI_VENDOR_MITAC	0x1071		/* Mitac */
#define	PCI_VENDOR_YAMAHA	0x1073		/* Yamaha */
#define	PCI_VENDOR_NEXGEN	0x1074		/* NexGen Microsystems */
#define	PCI_VENDOR_QLOGIC	0x1077		/* QLogic */
#define	PCI_VENDOR_CYRIX	0x1078		/* Cyrix */
#define	PCI_VENDOR_LEADTEK	0x107d		/* LeadTek */
#define	PCI_VENDOR_INTERPHASE	0x107e		/* Interphase */
#define	PCI_VENDOR_CONTAQ	0x1080		/* Contaq Microsystems */
#define	PCI_VENDOR_BIT3	0x108a		/* Bit3 */
#define	PCI_VENDOR_OLICOM	0x108d		/* Olicom */
#define	PCI_VENDOR_SUN	0x108e		/* Sun */
#define	PCI_VENDOR_INTERGRAPH	0x1091		/* Intergraph */
#define	PCI_VENDOR_DIAMOND	0x1092		/* Diamond Multimedia */
#define	PCI_VENDOR_NATINST	0x1093		/* National Instruments */
#define	PCI_VENDOR_CMDTECH	0x1095		/* CMD Technology */
#define	PCI_VENDOR_QUANTUMDESIGNS	0x1098		/* Quantum Designs */
#define	PCI_VENDOR_BROOKTREE	0x109e		/* Brooktree */
#define	PCI_VENDOR_SGI	0x10a9		/* SGI */
#define	PCI_VENDOR_ACC	0x10aa		/* ACC Microelectronics */
#define	PCI_VENDOR_SYMPHONY	0x10ad		/* Symphony Labs */
#define	PCI_VENDOR_STB	0x10b4		/* STB Systems */
#define	PCI_VENDOR_PLX	0x10b5		/* PLX */
#define	PCI_VENDOR_MADGE	0x10b6		/* Madge Networks */
#define	PCI_VENDOR_3COM	0x10b7		/* 3Com */
#define	PCI_VENDOR_SMC	0x10b8		/* SMC */
#define	PCI_VENDOR_ALI	0x10b9		/* Acer Labs */
#define	PCI_VENDOR_MITSUBISHIELEC	0x10ba		/* Mitsubishi Electronics */
#define	PCI_VENDOR_SURECOM	0x10bd		/* Surecom */
#define	PCI_VENDOR_NEOMAGIC	0x10c8		/* Neomagic */
#define	PCI_VENDOR_MENTOR	0x10cc		/* Mentor ARC */
#define	PCI_VENDOR_ADVSYS	0x10cd		/* Advansys */
#define	PCI_VENDOR_FUJITSU	0x10cf		/* Fujitsu */
#define	PCI_VENDOR_MOLEX	0x10d2		/* Molex */
#define	PCI_VENDOR_MACRONIX	0x10d9		/* Macronix */
#define	PCI_VENDOR_ES	0x10dd		/* Evans & Sutherland */
#define	PCI_VENDOR_NVIDIA	0x10de		/* NVIDIA */
#define	PCI_VENDOR_EMULEX	0x10df		/* Emulex */
#define	PCI_VENDOR_IMS	0x10e0		/* Integrated Micro Solutions */
#define	PCI_VENDOR_TEKRAM	0x10e1		/* Tekram (1st ID) */
#define	PCI_VENDOR_NEWBRIDGE	0x10e3		/* Newbridge */
#define	PCI_VENDOR_AMCIRCUITS	0x10e8		/* Applied Micro Circuits */
#define	PCI_VENDOR_TVIA	0x10ea		/* Tvia */
#define	PCI_VENDOR_REALTEK	0x10ec		/* Realtek */
#define	PCI_VENDOR_NKK	0x10f5		/* NKK */
#define	PCI_VENDOR_IODATA	0x10fc		/* IO Data Device */
#define	PCI_VENDOR_INITIO	0x1101		/* Initio */
#define	PCI_VENDOR_CREATIVELABS	0x1102		/* Creative Labs */
#define	PCI_VENDOR_TRIONES	0x1103		/* HighPoint */
#define	PCI_VENDOR_SIGMA	0x1105		/* Sigma Designs */
#define	PCI_VENDOR_VIATECH	0x1106		/* VIA */
#define	PCI_VENDOR_COGENT	0x1109		/* Cogent Data */
#define	PCI_VENDOR_SIEMENS	0x110a		/* Siemens */
#define	PCI_VENDOR_ZNYX	0x110d		/* Znyx Networks */
#define	PCI_VENDOR_ACCTON	0x1113		/* Accton */
#define	PCI_VENDOR_ATMEL	0x1114		/* Atmel */
#define	PCI_VENDOR_VORTEX	0x1119		/* Vortex */
#define	PCI_VENDOR_EFFICIENTNETS	0x111a		/* Efficent Networks */
#define	PCI_VENDOR_IDT	0x111d		/* IDT */
#define	PCI_VENDOR_FORE	0x1127		/* FORE Systems */
#define	PCI_VENDOR_PHILIPS	0x1131		/* Philips */
#define	PCI_VENDOR_CISCO	0x1137		/* Cisco */
#define	PCI_VENDOR_ZIATECH	0x1138		/* Ziatech */
#define	PCI_VENDOR_CYCLONE	0x113c		/* Cyclone */
#define	PCI_VENDOR_EQUINOX	0x113f		/* Equinox */
#define	PCI_VENDOR_ALLIANCE	0x1142		/* Alliance */
#define	PCI_VENDOR_WORKBIT	0x1145		/* Workbit */
#define	PCI_VENDOR_SCHNEIDERKOCH	0x1148		/* Schneider & Koch */
#define	PCI_VENDOR_DIGI	0x114f		/* Digi */
#define	PCI_VENDOR_MUTECH	0x1159		/* Mutech */
#define	PCI_VENDOR_XIRCOM	0x115d		/* Xircom */
#define	PCI_VENDOR_RENDITION	0x1163		/* Rendition */
#define	PCI_VENDOR_RCC	0x1166		/* ServerWorks */
#define	PCI_VENDOR_ALTERA	0x1172		/* Altera */
#define	PCI_VENDOR_TOSHIBA2	0x1179		/* Toshiba */
#define	PCI_VENDOR_RICOH	0x1180		/* Ricoh */
#define	PCI_VENDOR_DLINK	0x1186		/* D-Link */
#define	PCI_VENDOR_COROLLARY	0x118c		/* Corollary */
#define	PCI_VENDOR_ACARD	0x1191		/* Acard */
#define	PCI_VENDOR_ZEINET	0x1193		/* Zeinet */
#define	PCI_VENDOR_OMEGA	0x119b		/* Omega Micro */
#define	PCI_VENDOR_MARVELL	0x11ab		/* Marvell */
#define	PCI_VENDOR_LITEON	0x11ad		/* Lite-On */
#define	PCI_VENDOR_V3	0x11b0		/* V3 Semiconductor */
#define	PCI_VENDOR_PINNACLE	0x11bd		/* Pinnacle Systems */
#define	PCI_VENDOR_LUCENT	0x11c1		/* AT&T/Lucent */
#define	PCI_VENDOR_DOLPHIN	0x11c8		/* Dolphin */
#define	PCI_VENDOR_MRTMAGMA	0x11c9		/* Mesa Ridge (MAGMA) */
#define	PCI_VENDOR_AD	0x11d4		/* Analog Devices */
#define	PCI_VENDOR_ZORAN	0x11de		/* Zoran */
#define	PCI_VENDOR_PIJNENBURG	0x11e3		/* Pijnenburg */
#define	PCI_VENDOR_COMPEX	0x11f6		/* Compex */
#define	PCI_VENDOR_CYCLADES	0x120e		/* Cyclades */
#define	PCI_VENDOR_ESSENTIAL	0x120f		/* Essential Communications */
#define	PCI_VENDOR_O2MICRO	0x1217		/* O2 Micro */
#define	PCI_VENDOR_3DFX	0x121a		/* 3DFX */
#define	PCI_VENDOR_ATML	0x121b		/* ATML */
#define	PCI_VENDOR_BOCHS	0x1234		/* Bochs */
#define	PCI_VENDOR_CCUBE	0x123f		/* C-Cube */
#define	PCI_VENDOR_AVM	0x1244		/* AVM */
#define	PCI_VENDOR_STALLION	0x124d		/* Stallion */
#define	PCI_VENDOR_COREGA	0x1259		/* Corega */
#define	PCI_VENDOR_ASIX	0x125b		/* ASIX */
#define	PCI_VENDOR_ESSTECH	0x125d		/* ESS */
#define	PCI_VENDOR_INTERSIL	0x1260		/* Intersil */
#define	PCI_VENDOR_NORTEL	0x126c		/* Nortel Networks */
#define	PCI_VENDOR_SMI	0x126f		/* Silicon Motion */
#define	PCI_VENDOR_ENSONIQ	0x1274		/* Ensoniq */
#define	PCI_VENDOR_TRANSMETA	0x1279		/* Transmeta */
#define	PCI_VENDOR_ROCKWELL	0x127a		/* Rockwell */
#define	PCI_VENDOR_DAVICOM	0x1282		/* Davicom */
#define	PCI_VENDOR_ITEXPRESS	0x1283		/* ITExpress */
#define	PCI_VENDOR_PLATFORM	0x1285		/* Platform */
#define	PCI_VENDOR_LUXSONOR	0x1287		/* LuxSonor */
#define	PCI_VENDOR_TRITECH	0x1292		/* TriTech */
#define	PCI_VENDOR_ALTEON	0x12ae		/* Alteon */
#define	PCI_VENDOR_USR	0x12b9		/* US Robotics */
#define	PCI_VENDOR_STB2	0x12d2		/* NVIDIA/SGS-Thomson */
#define	PCI_VENDOR_PERICOM	0x12d8		/* Pericom */
#define	PCI_VENDOR_AUREAL	0x12eb		/* Aureal */
#define	PCI_VENDOR_ADMTEK	0x1317		/* ADMtek */
#define	PCI_VENDOR_PE	0x1318		/* Packet Engines */
#define	PCI_VENDOR_FORTEMEDIA	0x1319		/* Forte Media */
#define	PCI_VENDOR_SIIG	0x131f		/* SIIG */
#define	PCI_VENDOR_MICRON	0x1344		/* Micron Technology */
#define	PCI_VENDOR_DTCTECH	0x134a		/* DTC Tech */
#define	PCI_VENDOR_PCTEL	0x134d		/* PCTEL */
#define	PCI_VENDOR_BRAINBOXES	0x135a		/* Brainboxes */
#define	PCI_VENDOR_MEINBERG	0x1360		/* Meinberg Funkuhren */
#define	PCI_VENDOR_CNET	0x1371		/* CNet */
#define	PCI_VENDOR_SILICOM	0x1374		/* Silicom */
#define	PCI_VENDOR_LMC	0x1376		/* LAN Media */
#define	PCI_VENDOR_NETGEAR	0x1385		/* Netgear */
#define	PCI_VENDOR_MOXA	0x1393		/* Moxa */
#define	PCI_VENDOR_LEVEL1	0x1394		/* Level 1 */
#define	PCI_VENDOR_HIFN	0x13a3		/* Hifn */
#define	PCI_VENDOR_EXAR	0x13a8		/* Exar */
#define	PCI_VENDOR_3WARE	0x13c1		/* 3ware */
#define	PCI_VENDOR_TECHSAN	0x13d0		/* Techsan Electronics */
#define	PCI_VENDOR_ABOCOM	0x13d1		/* Abocom */
#define	PCI_VENDOR_SUNDANCE	0x13f0		/* Sundance */
#define	PCI_VENDOR_CMI	0x13f6		/* C-Media Electronics */
#define	PCI_VENDOR_LAVA	0x1407		/* Lava */
#define	PCI_VENDOR_SUNIX	0x1409		/* Sunix */
#define	PCI_VENDOR_ICENSEMBLE	0x1412		/* IC Ensemble */
#define	PCI_VENDOR_MICROSOFT	0x1414		/* Microsoft */
#define	PCI_VENDOR_OXFORD2	0x1415		/* Oxford */
#define	PCI_VENDOR_CHELSIO	0x1425		/* Chelsio */
#define	PCI_VENDOR_EDIMAX	0x1432		/* Edimax */
#define	PCI_VENDOR_TAMARACK	0x143d		/* Tamarack */
#define	PCI_VENDOR_SAMSUNG2	0x144d		/* Samsung */
#define	PCI_VENDOR_ASKEY	0x144f		/* Askey */
#define	PCI_VENDOR_AVERMEDIA	0x1461		/* Avermedia */
#define	PCI_VENDOR_MSI	0x1462		/* MSI */
#define	PCI_VENDOR_LITEON2	0x14a4		/* Lite-On */
#define	PCI_VENDOR_AIRONET	0x14b9		/* Aironet */
#define	PCI_VENDOR_GLOBESPAN	0x14bc		/* Globespan */
#define	PCI_VENDOR_MYRICOM	0x14c1		/* Myricom */
#define	PCI_VENDOR_MEDIATEK	0x14c3		/* MediaTek */
#define	PCI_VENDOR_OXFORD	0x14d2		/* VScom */
#define	PCI_VENDOR_AVLAB	0x14db		/* Avlab */
#define	PCI_VENDOR_INVERTEX	0x14e1		/* Invertex */
#define	PCI_VENDOR_BROADCOM	0x14e4		/* Broadcom */
#define	PCI_VENDOR_PLANEX	0x14ea		/* Planex */
#define	PCI_VENDOR_CONEXANT	0x14f1		/* Conexant */
#define	PCI_VENDOR_DELTA	0x1500		/* Delta */
#define	PCI_VENDOR_MYSON	0x1516		/* Myson Century */
#define	PCI_VENDOR_TOPIC	0x151f		/* Topic/SmartLink */
#define	PCI_VENDOR_ENE	0x1524		/* ENE */
#define	PCI_VENDOR_ARALION	0x1538		/* Aralion */
#define	PCI_VENDOR_TERRATEC	0x153b		/* TerraTec */
#define	PCI_VENDOR_PLDA	0x1556		/* PLDA */
#define	PCI_VENDOR_PERLE	0x155f		/* Perle */
#define	PCI_VENDOR_SYMBOL	0x1562		/* Symbol */
#define	PCI_VENDOR_SYBA	0x1592		/* Syba */
#define	PCI_VENDOR_BLUESTEEL	0x15ab		/* Bluesteel */
#define	PCI_VENDOR_VMWARE	0x15ad		/* VMware */
#define	PCI_VENDOR_ZOLTRIX	0x15b0		/* Zoltrix */
#define	PCI_VENDOR_MELLANOX	0x15b3		/* Mellanox */
#define	PCI_VENDOR_SANDISK	0x15b7		/* SanDisk */
#define	PCI_VENDOR_AGILENT	0x15bc		/* Agilent */
#define	PCI_VENDOR_QUICKNET	0x15e2		/* Quicknet Technologies */
#define	PCI_VENDOR_NDC	0x15e8		/* National Datacomm */
#define	PCI_VENDOR_PDC	0x15e9		/* Pacific Data */
#define	PCI_VENDOR_EUMITCOM	0x1638		/* Eumitcom */
#define	PCI_VENDOR_BROCADE	0x1657		/* Brocade */
#define	PCI_VENDOR_NETSEC	0x1660		/* NetSec */
#define	PCI_VENDOR_ZYDAS	0x167b		/* ZyDAS */
#define	PCI_VENDOR_SAMSUNG	0x167d		/* Samsung */
#define	PCI_VENDOR_ATHEROS	0x168c		/* Atheros */
#define	PCI_VENDOR_GLOBALSUN	0x16ab		/* Global Sun */
#define	PCI_VENDOR_SAFENET	0x16ae		/* SafeNet */
#define	PCI_VENDOR_SYNOPSYS	0x16c3		/* Synopsys */
#define	PCI_VENDOR_MICREL	0x16c6		/* Micrel */
#define	PCI_VENDOR_USR2	0x16ec		/* US Robotics */
#define	PCI_VENDOR_NETOCTAVE	0x170b		/* Netoctave */
#define	PCI_VENDOR_VITESSE	0x1725		/* Vitesse */
#define	PCI_VENDOR_LINKSYS	0x1737		/* Linksys */
#define	PCI_VENDOR_ALTIMA	0x173b		/* Altima */
#define	PCI_VENDOR_ANTARES	0x1754		/* Antares Microsystems */
#define	PCI_VENDOR_CAVIUM	0x177d		/* Cavium */
#define	PCI_VENDOR_BELKIN2	0x1799		/* Belkin */
#define	PCI_VENDOR_GENESYS	0x17a0		/* Genesys Logic */
#define	PCI_VENDOR_LENOVO	0x17aa		/* Lenovo */
#define	PCI_VENDOR_HAWKING	0x17b3		/* Hawking Technology */
#define	PCI_VENDOR_QUALCOMM	0x17cb		/* Qualcomm */
#define	PCI_VENDOR_NETCHIP	0x17cc		/* NetChip Technology */
#define	PCI_VENDOR_CADENCE	0x17cd		/* Cadence */
#define	PCI_VENDOR_I4	0x17cf		/* I4 */
#define	PCI_VENDOR_ARECA	0x17d3		/* Areca */
#define	PCI_VENDOR_NETERION	0x17d5		/* Neterion */
#define	PCI_VENDOR_RDC	0x17f3		/* RDC */
#define	PCI_VENDOR_INPROCOMM	0x17fe		/* INPROCOMM */
#define	PCI_VENDOR_LANERGY	0x1812		/* Lanergy */
#define	PCI_VENDOR_RALINK	0x1814		/* Ralink */
#define	PCI_VENDOR_XGI	0x18ca		/* XGI Technology */
#define	PCI_VENDOR_SILAN	0x1904		/* Silan */
#define	PCI_VENDOR_RENESAS	0x1912		/* Renesas */
#define	PCI_VENDOR_SANGOMA	0x1923		/* Sangoma */
#define	PCI_VENDOR_SOLARFLARE	0x1924		/* Solarflare */
#define	PCI_VENDOR_OPTION	0x1931		/* Option */
#define	PCI_VENDOR_FREESCALE	0x1957		/* Freescale */
#define	PCI_VENDOR_ATTANSIC	0x1969		/* Attansic Technology */
#define	PCI_VENDOR_AGEIA	0x1971		/* Ageia */
#define	PCI_VENDOR_JMICRON	0x197b		/* JMicron */
#define	PCI_VENDOR_PHISON	0x1987		/* Phison */
#define	PCI_VENDOR_SERVERENGINES	0x19a2		/* ServerEngines */
#define	PCI_VENDOR_HUAWEI	0x19e5		/* Huawei */
#define	PCI_VENDOR_ASPEED	0x1a03		/* ASPEED Technology */
#define	PCI_VENDOR_AWT	0x1a3b		/* AWT */
#define	PCI_VENDOR_PARALLELS2	0x1ab8		/* Parallels */
#define	PCI_VENDOR_FUSIONIO	0x1aed		/* Fusion-io */
#define	PCI_VENDOR_QUMRANET	0x1af4		/* Qumranet */
#define	PCI_VENDOR_ASMEDIA	0x1b21		/* ASMedia */
#define	PCI_VENDOR_REDHAT	0x1b36		/* Red Hat */
#define	PCI_VENDOR_MARVELL2	0x1b4b		/* Marvell */
#define	PCI_VENDOR_ETRON	0x1b6f		/* Etron */
#define	PCI_VENDOR_FRESCO	0x1b73		/* Fresco Logic */
#define	PCI_VENDOR_WCH2	0x1c00		/* Nanjing QinHeng Electronics */
#define	PCI_VENDOR_SYMPHONY2	0x1c1c		/* Symphony Labs */
#define	PCI_VENDOR_SKHYNIX	0x1c5c		/* SK hynix */
#define	PCI_VENDOR_ADATA	0x1cc1		/* ADATA Technology */
#define	PCI_VENDOR_UMIS	0x1cc4		/* Union Memory */
#define	PCI_VENDOR_ZHAOXIN	0x1d17		/* Zhaoxin */
#define	PCI_VENDOR_BAIKAL	0x1d39		/* Baikal Electronics */
#define	PCI_VENDOR_AQUANTIA	0x1d6a		/* Aquantia */
#define	PCI_VENDOR_ROCKCHIP	0x1d87		/* Rockchip */
#define	PCI_VENDOR_LONGSYS	0x1d97		/* Longsys */
#define	PCI_VENDOR_TEKRAM2	0x1de1		/* Tekram */
#define	PCI_VENDOR_RPI	0x1de4		/* Raspberry Pi */
#define	PCI_VENDOR_AMPERE	0x1def		/* Ampere */
#define	PCI_VENDOR_KIOXIA	0x1e0f		/* Kioxia */
#define	PCI_VENDOR_YMTC	0x1e49		/* YMTC */
#define	PCI_VENDOR_SSSTC	0x1e95		/* SSSTC */
#define	PCI_VENDOR_QUECTEL	0x1eac		/* Quectel */
#define	PCI_VENDOR_TEHUTI	0x1fc9		/* Tehuti Networks */
#define	PCI_VENDOR_SUNIX2	0x1fd4		/* Sunix */
#define	PCI_VENDOR_KINGSTON	0x2646		/* Kingston */
#define	PCI_VENDOR_HINT	0x3388		/* Hint */
#define	PCI_VENDOR_3DLABS	0x3d3d		/* 3D Labs */
#define	PCI_VENDOR_AVANCE2	0x4005		/* Avance Logic */
#define	PCI_VENDOR_ADDTRON	0x4033		/* Addtron */
#define	PCI_VENDOR_NETXEN	0x4040		/* NetXen */
#define	PCI_VENDOR_WCH	0x4348		/* Nanjing QinHeng Electronics */
#define	PCI_VENDOR_TXIC	0x4651		/* TXIC */
#define	PCI_VENDOR_INDCOMPSRC	0x494f		/* Industrial Computer Source */
#define	PCI_VENDOR_NETVIN	0x4a14		/* NetVin */
#define	PCI_VENDOR_GEMTEK	0x5046		/* Gemtek */
#define	PCI_VENDOR_TURTLEBEACH	0x5053		/* Turtle Beach */
#define	PCI_VENDOR_S3	0x5333		/* S3 */
#define	PCI_VENDOR_MOSCHIP	0x5372		/* MosChip */
#define	PCI_VENDOR_XENSOURCE	0x5853		/* XenSource */
#define	PCI_VENDOR_C4T	0x6374		/* c't Magazin */
#define	PCI_VENDOR_DCI	0x6666		/* Decision Computer */
#define	PCI_VENDOR_QUANCOM	0x8008		/* Quancom Informationssysteme */
#define	PCI_VENDOR_INTEL	0x8086		/* Intel */
#define	PCI_VENDOR_WANGXUN	0x8088		/* Beijing WangXun Technology */
#define	PCI_VENDOR_INNOTEK	0x80ee		/* InnoTek */
#define	PCI_VENDOR_SIGMATEL	0x8384		/* Sigmatel */
#define	PCI_VENDOR_WINBOND2	0x8c4a		/* Winbond */
#define	PCI_VENDOR_KTI	0x8e2e		/* KTI */
#define	PCI_VENDOR_ADP	0x9004		/* Adaptec */
#define	PCI_VENDOR_ADP2	0x9005		/* Adaptec */
#define	PCI_VENDOR_ATRONICS	0x907f		/* Atronics */
#define	PCI_VENDOR_NETMOS	0x9710		/* NetMos */
#define	PCI_VENDOR_3COM2	0xa727		/* 3Com */
#define	PCI_VENDOR_PARALLELS	0xaaaa		/* Parallels */
#define	PCI_VENDOR_CRUCIAL	0xc0a9		/* Crucial */
#define	PCI_VENDOR_TIGERJET	0xe159		/* TigerJet Network */
#define	PCI_VENDOR_ENDACE	0xeace		/* Endace */
#define	PCI_VENDOR_BELKIN	0xec80		/* Belkin Components */
#define	PCI_VENDOR_ARC	0xedd8		/* ARC Logic */
#define	PCI_VENDOR_SIFIVE	0xf15e		/* SiFive */
#define	PCI_VENDOR_INVALID	0xffff		/* INVALID VENDOR ID */

/*
 * List of known products.  Grouped by vendor.
 */

/* 3Com Products */
#define	PCI_PRODUCT_3COM_3C985	0x0001		/* 3c985 */
#define	PCI_PRODUCT_3COM_3C996	0x0003		/* 3c996 */
#define	PCI_PRODUCT_3COM_3CRDAG675	0x0013		/* 3CRDAG675 */
#define	PCI_PRODUCT_3COM_3C_MPCI_MODEM	0x1007		/* Modem */
#define	PCI_PRODUCT_3COM_3C940	0x1700		/* 3c940 */
#define	PCI_PRODUCT_3COM_3C339	0x3390		/* 3c339 */
#define	PCI_PRODUCT_3COM_3C359	0x3590		/* 3c359 */
#define	PCI_PRODUCT_3COM_3C450	0x4500		/* 3c450 */
#define	PCI_PRODUCT_3COM_3C555	0x5055		/* 3c555 */
#define	PCI_PRODUCT_3COM_3C575	0x5057		/* 3c575 */
#define	PCI_PRODUCT_3COM_3CCFE575BT	0x5157		/* 3CCFE575BT */
#define	PCI_PRODUCT_3COM_3CCFE575CT	0x5257		/* 3CCFE575CT */
#define	PCI_PRODUCT_3COM_3C590	0x5900		/* 3c590 */
#define	PCI_PRODUCT_3COM_3C595TX	0x5950		/* 3c595 */
#define	PCI_PRODUCT_3COM_3C595T4	0x5951		/* 3c595 */
#define	PCI_PRODUCT_3COM_3C595MII	0x5952		/* 3c595 */
#define	PCI_PRODUCT_3COM_3CRSHPW796	0x6000		/* 3CRSHPW796 802.11b */
#define	PCI_PRODUCT_3COM_3CRWE154G72	0x6001		/* 3CRWE154G72 802.11g */
#define	PCI_PRODUCT_3COM_3C556	0x6055		/* 3c556 */
#define	PCI_PRODUCT_3COM_3C556B	0x6056		/* 3c556B */
#define	PCI_PRODUCT_3COM_3CCFEM656	0x6560		/* 3CCFEM656 */
#define	PCI_PRODUCT_3COM_3CCFEM656B	0x6562		/* 3CCFEM656B */
#define	PCI_PRODUCT_3COM_MODEM56	0x6563		/* Modem */
#define	PCI_PRODUCT_3COM_3CCFEM656C	0x6564		/* 3CCFEM656C */
#define	PCI_PRODUCT_3COM_GLOBALMODEM56	0x6565		/* Modem */
#define	PCI_PRODUCT_3COM_3CSOHO100TX	0x7646		/* 3cSOHO-TX */
#define	PCI_PRODUCT_3COM_3CRWE777A	0x7770		/* 3crwe777a AirConnect */
#define	PCI_PRODUCT_3COM_3C940B	0x80eb		/* 3c940B */
#define	PCI_PRODUCT_3COM_3C900TPO	0x9000		/* 3c900 */
#define	PCI_PRODUCT_3COM_3C900COMBO	0x9001		/* 3c900 */
#define	PCI_PRODUCT_3COM_3C900B	0x9004		/* 3c900B */
#define	PCI_PRODUCT_3COM_3C900BCOMBO	0x9005		/* 3c900B */
#define	PCI_PRODUCT_3COM_3C900BTPC	0x9006		/* 3c900B */
#define	PCI_PRODUCT_3COM_3C900BFL	0x900a		/* 3c900B */
#define	PCI_PRODUCT_3COM_3C905TX	0x9050		/* 3c905 */
#define	PCI_PRODUCT_3COM_3C905T4	0x9051		/* 3c905 */
#define	PCI_PRODUCT_3COM_3C905BTX	0x9055		/* 3c905B */
#define	PCI_PRODUCT_3COM_3C905BT4	0x9056		/* 3c905B */
#define	PCI_PRODUCT_3COM_3C905BCOMBO	0x9058		/* 3c905B */
#define	PCI_PRODUCT_3COM_3C905BFX	0x905a		/* 3c905B */
#define	PCI_PRODUCT_3COM_3C905CTX	0x9200		/* 3c905C */
#define	PCI_PRODUCT_3COM_3C9201	0x9201		/* 3c9201 */
#define	PCI_PRODUCT_3COM_3C920BEMBW	0x9202		/* 3c920B-EMB-WNM */
#define	PCI_PRODUCT_3COM_3CSHO100BTX	0x9300		/* 3cSOHO */
#define	PCI_PRODUCT_3COM_3C980TX	0x9800		/* 3c980 */
#define	PCI_PRODUCT_3COM_3C980CTX	0x9805		/* 3c980C */
#define	PCI_PRODUCT_3COM_3CR990	0x9900		/* 3cr990 */
#define	PCI_PRODUCT_3COM_3CR990TX	0x9901		/* 3cr990-TX */
#define	PCI_PRODUCT_3COM_3CR990TX95	0x9902		/* 3cr990-TX-95 */
#define	PCI_PRODUCT_3COM_3CR990TX97	0x9903		/* 3cr990-TX-97 */
#define	PCI_PRODUCT_3COM_3C990BTXM	0x9904		/* 3c990b-TX-M */
#define	PCI_PRODUCT_3COM_3CR990FX	0x9905		/* 3cr990-FX */
#define	PCI_PRODUCT_3COM_3CR990SVR95	0x9908		/* 3cr990SVR95 */
#define	PCI_PRODUCT_3COM_3CR990SVR97	0x9909		/* 3cr990SVR97 */
#define	PCI_PRODUCT_3COM_3C990BSVR	0x990a		/* 3c990BSVR */
#define	PCI_PRODUCT_3COM2_3CRPAG175	0x0013		/* 3CRPAG175 */

/* 3DFX Interactive */
#define	PCI_PRODUCT_3DFX_VOODOO	0x0001		/* Voodoo */
#define	PCI_PRODUCT_3DFX_VOODOO2	0x0002		/* Voodoo2 */
#define	PCI_PRODUCT_3DFX_BANSHEE	0x0003		/* Banshee */
#define	PCI_PRODUCT_3DFX_VOODOO32000	0x0004		/* Voodoo3 */
#define	PCI_PRODUCT_3DFX_VOODOO3	0x0005		/* Voodoo3 */
#define	PCI_PRODUCT_3DFX_VOODOO4	0x0007		/* Voodoo4 */
#define	PCI_PRODUCT_3DFX_VOODOO5	0x0009		/* Voodoo5 */
#define	PCI_PRODUCT_3DFX_VOODOO44200	0x000b		/* Voodoo4 */


/* 3D Labs products */
#define	PCI_PRODUCT_3DLABS_GLINT_300SX	0x0001		/* GLINT 300SX */
#define	PCI_PRODUCT_3DLABS_GLINT_500TX	0x0002		/* GLINT 500TX */
#define	PCI_PRODUCT_3DLABS_GLINT_DELTA	0x0003		/* GLINT Delta */
#define	PCI_PRODUCT_3DLABS_PERMEDIA	0x0004		/* Permedia */
#define	PCI_PRODUCT_3DLABS_GLINT_MX	0x0006		/* GLINT MX */
#define	PCI_PRODUCT_3DLABS_PERMEDIA2	0x0007		/* Permedia 2 */
#define	PCI_PRODUCT_3DLABS_GLINT_GAMMA	0x0008		/* GLINT Gamma */
#define	PCI_PRODUCT_3DLABS_PERMEDIA2V	0x0009		/* Permedia 2v */
#define	PCI_PRODUCT_3DLABS_PERMEDIA3	0x000a		/* Permedia 3 */
#define	PCI_PRODUCT_3DLABS_WILDCAT_6210	0x07a1		/* Wildcat III 6210 */
#define	PCI_PRODUCT_3DLABS_WILDCAT_5110	0x07a2		/* Wildcat 5110 */
#define	PCI_PRODUCT_3DLABS_WILDCAT_7210	0x07a3		/* Wildcat IV 7210 */

/* 3ware products */
#define	PCI_PRODUCT_3WARE_ESCALADE	0x1000		/* 5000/6000 RAID */
#define	PCI_PRODUCT_3WARE_ESCALADE_ASIC	0x1001		/* 7000/8000 RAID */
#define	PCI_PRODUCT_3WARE_9000	0x1002		/* 9000 RAID */
#define	PCI_PRODUCT_3WARE_9500	0x1003		/* 9500 RAID */

/* Abocom products */
#define	PCI_PRODUCT_ABOCOM_FE2500	0xab02		/* FE2500 */
#define	PCI_PRODUCT_ABOCOM_PCM200	0xab03		/* PCM200 */
#define	PCI_PRODUCT_ABOCOM_FE2000VX	0xab06		/* FE2000VX */
#define	PCI_PRODUCT_ABOCOM_FE2500MX	0xab08		/* FE2500MX */

/* Acard products */
#define	PCI_PRODUCT_ACARD_ATP850U	0x0005		/* ATP850U/UF */
#define	PCI_PRODUCT_ACARD_ATP860	0x0006		/* ATP860 */
#define	PCI_PRODUCT_ACARD_ATP860A	0x0007		/* ATP860-A */
#define	PCI_PRODUCT_ACARD_ATP865A	0x0008		/* ATP865-A */
#define	PCI_PRODUCT_ACARD_ATP865R	0x0009		/* ATP865-R */
#define	PCI_PRODUCT_ACARD_AEC6710	0x8002		/* AEC6710 */
#define	PCI_PRODUCT_ACARD_AEC6712UW	0x8010		/* AEC6712UW */
#define	PCI_PRODUCT_ACARD_AEC6712U	0x8020		/* AEC6712U */
#define	PCI_PRODUCT_ACARD_AEC6712S	0x8030		/* AEC6712S */
#define	PCI_PRODUCT_ACARD_AEC6710D	0x8040		/* AEC6710D */
#define	PCI_PRODUCT_ACARD_AEC6715UW	0x8050		/* AEC6715UW */

/* ACC Products */
#define	PCI_PRODUCT_ACC_2188	0x0000		/* ACCM 2188 VL-PCI */
#define	PCI_PRODUCT_ACC_2051_HB	0x2051		/* 2051 PCI */
#define	PCI_PRODUCT_ACC_2051_ISA	0x5842		/* 2051 ISA */

/* Accton products */
#define	PCI_PRODUCT_ACCTON_5030	0x1211		/* MPX 5030/5038 */
#define	PCI_PRODUCT_ACCTON_EN2242	0x1216		/* EN2242 */
#define	PCI_PRODUCT_ACCTON_EN1217	0x1217		/* EN1217 */

/* Acer products */
#define	PCI_PRODUCT_ACER_M1435	0x1435		/* M1435 VL-PCI */

/* Analog Devices */
#define	PCI_PRODUCT_AD_SP21535	0x1535		/* ADSP 21535 DSP */
#define	PCI_PRODUCT_AD_1889	0x1889		/* AD1889 Audio */
#define	PCI_PRODUCT_AD_SP2141	0x2f44		/* SafeNet ADSP 2141 */

/* ADATA products */
#define	PCI_PRODUCT_ADATA_SX8200PRO	0x8201		/* SX8200 Pro */

/* Addtron products */
#define	PCI_PRODUCT_ADDTRON_RHINEII	0x1320		/* RhineII */
#define	PCI_PRODUCT_ADDTRON_8139	0x1360		/* rtl8139 */
#define	PCI_PRODUCT_ADDTRON_AWA100	0x7001		/* AWA-100 */

/* ADMtek products */
#define	PCI_PRODUCT_ADMTEK_AL981	0x0981		/* AL981 */
#define	PCI_PRODUCT_ADMTEK_AN983	0x0985		/* AN983 */
#define	PCI_PRODUCT_ADMTEK_AN985	0x1985		/* AN985 */
#define	PCI_PRODUCT_ADMTEK_ADM8211	0x8201		/* ADM8211 */
#define	PCI_PRODUCT_ADMTEK_ADM9511	0x9511		/* ADM9511 */
#define	PCI_PRODUCT_ADMTEK_ADM9513	0x9513		/* ADM9513 */

/* Adaptec products */
#define	PCI_PRODUCT_ADP_AIC7810	0x1078		/* AIC-7810 */
#define	PCI_PRODUCT_ADP_2940AU_CN	0x2178		/* AHA-2940AU/CN */
#define	PCI_PRODUCT_ADP_2930CU	0x3860		/* AHA-2930CU */
#define	PCI_PRODUCT_ADP_AIC7850	0x5078		/* AIC-7850 */
#define	PCI_PRODUCT_ADP_AIC7855	0x5578		/* AIC-7855 */
#define	PCI_PRODUCT_ADP_AIC5900	0x5900		/* AIC-5900 ATM */
#define	PCI_PRODUCT_ADP_AIC5905	0x5905		/* AIC-5905 ATM */
#define	PCI_PRODUCT_ADP_1480	0x6075		/* APA-1480 */
#define	PCI_PRODUCT_ADP_AIC7860	0x6078		/* AIC-7860 */
#define	PCI_PRODUCT_ADP_2940AU	0x6178		/* AHA-2940AU */
#define	PCI_PRODUCT_ADP_AIC6915	0x6915		/* AIC-6915 */
#define	PCI_PRODUCT_ADP_AIC7870	0x7078		/* AIC-7870 */
#define	PCI_PRODUCT_ADP_2940	0x7178		/* AHA-2940 */
#define	PCI_PRODUCT_ADP_3940	0x7278		/* AHA-3940 */
#define	PCI_PRODUCT_ADP_3985	0x7378		/* AHA-3985 */
#define	PCI_PRODUCT_ADP_2944	0x7478		/* AHA-2944 */
#define	PCI_PRODUCT_ADP_AIC7815	0x7815		/* AIC-7815 */
#define	PCI_PRODUCT_ADP_7895	0x7895		/* AIC-7895 */
#define	PCI_PRODUCT_ADP_AIC7880	0x8078		/* AIC-7880 */
#define	PCI_PRODUCT_ADP_2940U	0x8178		/* AHA-2940U */
#define	PCI_PRODUCT_ADP_3940U	0x8278		/* AHA-3940U */
#define	PCI_PRODUCT_ADP_398XU	0x8378		/* AHA-398XU */
#define	PCI_PRODUCT_ADP_2944U	0x8478		/* AHA-2944U */
#define	PCI_PRODUCT_ADP_2940UWPRO	0x8778		/* AHA-2940UWPro */

#define	PCI_PRODUCT_ADP2_2940U2	0x0010		/* AHA-2940U2 U2 */
#define	PCI_PRODUCT_ADP2_2930U2	0x0011		/* AHA-2930U2 U2 */
#define	PCI_PRODUCT_ADP2_AAA131U2	0x0013		/* AAA-131U2 U2 */
#define	PCI_PRODUCT_ADP2_AIC7890	0x001f		/* AIC-7890/1 U2 */
#define	PCI_PRODUCT_ADP2_3950U2B	0x0050		/* AHA-3950U2B U2 */
#define	PCI_PRODUCT_ADP2_3950U2D	0x0051		/* AHA-3950U2D U2 */
#define	PCI_PRODUCT_ADP2_AIC7896	0x005f		/* AIC-7896/7 U2 */
#define	PCI_PRODUCT_ADP2_29160	0x0080		/* AHA-29160 U160 */
#define	PCI_PRODUCT_ADP2_19160B	0x0081		/* AHA-19160B U160 */
#define	PCI_PRODUCT_ADP2_2930LP	0x0082		/* AVA-2930LP */
#define	PCI_PRODUCT_ADP2_AIC7892	0x008f		/* AIC-7892 U160 */
#define	PCI_PRODUCT_ADP2_3960D	0x00c0		/* AHA-3960D U160 */
#define	PCI_PRODUCT_ADP2_AIC7899B	0x00c1		/* AIC-7899B */
#define	PCI_PRODUCT_ADP2_AIC7899D	0x00c3		/* AIC-7899D */
#define	PCI_PRODUCT_ADP2_AIC7899F	0x00c5		/* AIC-7899F */
#define	PCI_PRODUCT_ADP2_AIC7899	0x00cf		/* AIC-7899 U160 */
#define	PCI_PRODUCT_ADP2_SERVERAID	0x0250		/* ServeRAID */
#define	PCI_PRODUCT_ADP2_AAC2622	0x0282		/* AAC-2622 */
#define	PCI_PRODUCT_ADP2_ASR2200S	0x0285		/* ASR-2200S */
#define	PCI_PRODUCT_ADP2_ASR2120S	0x0286		/* ASR-2120S */
#define	PCI_PRODUCT_ADP2_AAC364	0x0364		/* AAC-364 */
#define	PCI_PRODUCT_ADP2_AAC3642	0x0365		/* AAC-3642 */
#define	PCI_PRODUCT_ADP2_PERC_2QC	0x1364		/* Dell PERC 2/QC */
#define	PCI_PRODUCT_ADP2_AHA29320A	0x8000		/* AHA-29320A U320 */
#define	PCI_PRODUCT_ADP2_AIC7901	0x800f		/* AIC-7901 U320 */
#define	PCI_PRODUCT_ADP2_AHA39320	0x8010		/* AHA-39320 U320 */
#define	PCI_PRODUCT_ADP2_AHA39320D	0x8011		/* AHA-39320D U320 */
#define	PCI_PRODUCT_ADP2_AHA29320	0x8012		/* AHA-29320 U320 */
#define	PCI_PRODUCT_ADP2_AHA29320B	0x8013		/* AHA-29320B U320 */
#define	PCI_PRODUCT_ADP2_AHA29320LP2	0x8014		/* AHA-29320LP U320 */
#define	PCI_PRODUCT_ADP2_AHA39320B	0x8015		/* AHA-39320B U320 */
#define	PCI_PRODUCT_ADP2_AHA39320A	0x8016		/* AHA-39320A U320 */
#define	PCI_PRODUCT_ADP2_AHA29320LP	0x8017		/* AHA-29320LP U320 */
#define	PCI_PRODUCT_ADP2_AHA39320DB	0x801c		/* AHA-39320DB U320 */
#define	PCI_PRODUCT_ADP2_AIC7902_B	0x801d		/* AIC-7902B U320 */
#define	PCI_PRODUCT_ADP2_AIC7901A	0x801e		/* AIC-7901A U320 */
#define	PCI_PRODUCT_ADP2_AIC7902	0x801f		/* AIC-7902 U320 */

/* Advanced System Products */
#define	PCI_PRODUCT_ADVSYS_1200A	0x1100		/* 1200A */
#define	PCI_PRODUCT_ADVSYS_1200B	0x1200		/* 1200B */
#define	PCI_PRODUCT_ADVSYS_ULTRA	0x1300		/* ABP-930/40UA */
#define	PCI_PRODUCT_ADVSYS_WIDE	0x2300		/* ABP-940UW */
#define	PCI_PRODUCT_ADVSYS_U2W	0x2500		/* ASP-3940U2W */
#define	PCI_PRODUCT_ADVSYS_U3W	0x2700		/* ASP-3940U3W */

/* Ageia */
#define	PCI_PRODUCT_AGEIA_PHYSX	0x1011		/* PhysX */

/* Aironet Products */
#define	PCI_PRODUCT_AIRONET_PC4800_1	0x0001		/* PC4800 */
#define	PCI_PRODUCT_AIRONET_PCI352	0x0350		/* PCI35x */
#define	PCI_PRODUCT_AIRONET_PC4500	0x4500		/* PC4500 */
#define	PCI_PRODUCT_AIRONET_PC4800	0x4800		/* PC4800 */
#define	PCI_PRODUCT_AIRONET_MPI350	0xa504		/* MPI-350 */

/* Acer Labs products */
#define	PCI_PRODUCT_ALI_M1445	0x1445		/* M1445 VL-PCI */
#define	PCI_PRODUCT_ALI_M1449	0x1449		/* M1449 ISA */
#define	PCI_PRODUCT_ALI_M1451	0x1451		/* M1451 PCI */
#define	PCI_PRODUCT_ALI_M1461	0x1461		/* M1461 PCI */
#define	PCI_PRODUCT_ALI_M1489	0x1489		/* M1489 PCI */
#define	PCI_PRODUCT_ALI_M1511	0x1511		/* M1511 PCI */
#define	PCI_PRODUCT_ALI_M1513	0x1513		/* M1513 ISA */
#define	PCI_PRODUCT_ALI_M1521	0x1521		/* M1523 PCI */
#define	PCI_PRODUCT_ALI_M1523	0x1523		/* M1523 ISA */
#define	PCI_PRODUCT_ALI_M1531	0x1531		/* M1531 PCI */
#define	PCI_PRODUCT_ALI_M1533	0x1533		/* M1533 ISA */
#define	PCI_PRODUCT_ALI_M1535	0x1535		/* M1535 PCI */
#define	PCI_PRODUCT_ALI_M1541	0x1541		/* M1541 PCI */
#define	PCI_PRODUCT_ALI_M1543	0x1543		/* M1543 ISA */
#define	PCI_PRODUCT_ALI_M1563	0x1563		/* M1563 ISA */
#define	PCI_PRODUCT_ALI_M1573	0x1573		/* M1573 ISA */
#define	PCI_PRODUCT_ALI_M1575	0x1575		/* M1575 ISA */
#define	PCI_PRODUCT_ALI_M1621	0x1621		/* M1621 PCI */
#define	PCI_PRODUCT_ALI_M1631	0x1631		/* M1631 PCI */
#define	PCI_PRODUCT_ALI_M1644	0x1644		/* M1644 PCI */
#define	PCI_PRODUCT_ALI_M1647	0x1647		/* M1647 PCI */
#define	PCI_PRODUCT_ALI_M1689	0x1689		/* M1689 PCI */
#define	PCI_PRODUCT_ALI_M1695	0x1695		/* M1695 PCI */
#define	PCI_PRODUCT_ALI_M3309	0x3309		/* M3309 MPEG */
#define	PCI_PRODUCT_ALI_M4803	0x5215		/* M4803 */
#define	PCI_PRODUCT_ALI_M5219	0x5219		/* M5219 IDE */
#define	PCI_PRODUCT_ALI_M5229	0x5229		/* M5229 IDE */
#define	PCI_PRODUCT_ALI_M5237	0x5237		/* M5237 USB */
#define	PCI_PRODUCT_ALI_M5239	0x5239		/* M5239 USB2 */
#define	PCI_PRODUCT_ALI_M5243	0x5243		/* M5243 AGP/PCI-PCI */
#define	PCI_PRODUCT_ALI_M5246	0x5246		/* M5246 AGP */
#define	PCI_PRODUCT_ALI_M5247	0x5247		/* M5247 AGP/PCI-PC */
#define	PCI_PRODUCT_ALI_M5249	0x5249		/* M5249 */
#define	PCI_PRODUCT_ALI_M524B	0x524b		/* M524B PCIE */
#define	PCI_PRODUCT_ALI_M524C	0x524c		/* M524C PCIE */
#define	PCI_PRODUCT_ALI_M524D	0x524d		/* M524D PCIE */
#define	PCI_PRODUCT_ALI_M5261	0x5261		/* M5261 LAN */
#define	PCI_PRODUCT_ALI_M5263	0x5263		/* M5263 LAN */
#define	PCI_PRODUCT_ALI_M5281	0x5281		/* M5281 SATA */
#define	PCI_PRODUCT_ALI_M5287	0x5287		/* M5287 SATA */
#define	PCI_PRODUCT_ALI_M5288	0x5288		/* M5288 SATA */
#define	PCI_PRODUCT_ALI_M5289	0x5289		/* M5289 SATA */
#define	PCI_PRODUCT_ALI_M5451	0x5451		/* M5451 Audio */
#define	PCI_PRODUCT_ALI_M5455	0x5455		/* M5455 Audio */
#define	PCI_PRODUCT_ALI_M5457	0x5457		/* M5457 Modem */
#define	PCI_PRODUCT_ALI_M5461	0x5461		/* M5461 HD Audio */
#define	PCI_PRODUCT_ALI_M7101	0x7101		/* M7101 Power */

/* Alliance products */
#define	PCI_PRODUCT_ALLIANCE_AT22	0x6422		/* AT22 */
#define	PCI_PRODUCT_ALLIANCE_AT24	0x6424		/* AT24 */

/* Alteon products */
#define	PCI_PRODUCT_ALTEON_ACENIC	0x0001		/* Acenic */
#define	PCI_PRODUCT_ALTEON_ACENICT	0x0002		/* Acenic Copper */
#define	PCI_PRODUCT_ALTEON_BCM5700	0x0003		/* BCM5700 */
#define	PCI_PRODUCT_ALTEON_BCM5701	0x0004		/* BCM5701 */

/* Altera products */
#define	PCI_PRODUCT_ALTERA_EBUS	0x0000		/* EBus */

/* Altima products */
#define	PCI_PRODUCT_ALTIMA_AC1000	0x03e8		/* AC1000 */
#define	PCI_PRODUCT_ALTIMA_AC1001	0x03e9		/* AC1001 */
#define	PCI_PRODUCT_ALTIMA_AC9100	0x03ea		/* AC9100 */
#define	PCI_PRODUCT_ALTIMA_AC1003	0x03eb		/* AC1003 */

/* Applied Micro Circuits products */
#define	PCI_PRODUCT_AMCIRCUITS_S5933	0x4750		/* S5933 PCI Matchmaker */
#define	PCI_PRODUCT_AMCIRCUITS_LANAI	0x8043		/* Myrinet LANai */

/* AMD products */
#define	PCI_PRODUCT_AMD_0F_HT	0x1100		/* 0Fh HyperTransport */
#define	PCI_PRODUCT_AMD_0F_ADDR	0x1101		/* 0Fh Address Map */
#define	PCI_PRODUCT_AMD_0F_DRAM	0x1102		/* 0Fh DRAM Cfg */
#define	PCI_PRODUCT_AMD_0F_MISC	0x1103		/* 0Fh Misc Cfg */
#define	PCI_PRODUCT_AMD_10_HT	0x1200		/* 10h HyperTransport */
#define	PCI_PRODUCT_AMD_10_ADDR	0x1201		/* 10h Address Map */
#define	PCI_PRODUCT_AMD_10_DRAM	0x1202		/* 10h DRAM Cfg */
#define	PCI_PRODUCT_AMD_10_MISC	0x1203		/* 10h Misc Cfg */
#define	PCI_PRODUCT_AMD_10_LINK	0x1204		/* 10h Link Cfg */
#define	PCI_PRODUCT_AMD_19_78_DF_1	0x12f8		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_78_DF_2	0x12f9		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_78_DF_3	0x12fa		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_78_DF_4	0x12fb		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_78_DF_5	0x12fc		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_78_DF_6	0x12fd		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_78_DF_7	0x12fe		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_78_DF_8	0x12ff		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_11_HT	0x1300		/* 11h HyperTransport */
#define	PCI_PRODUCT_AMD_11_ADDR	0x1301		/* 11h Address Map */
#define	PCI_PRODUCT_AMD_11_DRAM	0x1302		/* 11h DRAM Cfg */
#define	PCI_PRODUCT_AMD_11_MISC	0x1303		/* 11h Misc Cfg */
#define	PCI_PRODUCT_AMD_11_LINK	0x1304		/* 11h Link Cfg */
#define	PCI_PRODUCT_AMD_15_1X_LINK	0x1400		/* 15/1xh Link Cfg */
#define	PCI_PRODUCT_AMD_15_1X_ADDR	0x1401		/* 15/1xh Address Map */
#define	PCI_PRODUCT_AMD_15_1X_DRAM	0x1402		/* 15/1xh DRAM Cfg */
#define	PCI_PRODUCT_AMD_15_1X_MISC	0x1403		/* 15/1xh Misc Cfg */
#define	PCI_PRODUCT_AMD_15_1X_CPU_PM	0x1404		/* 15/1xh CPU Power */
#define	PCI_PRODUCT_AMD_15_1X_NB_PM	0x1405		/* 15/1xh NB Power */
#define	PCI_PRODUCT_AMD_15_1X_HB	0x1410		/* 15/1xh Host */
#define	PCI_PRODUCT_AMD_15_1X_PCIE_1	0x1412		/* 15/1xh PCIE */
#define	PCI_PRODUCT_AMD_15_1X_PCIE_2	0x1413		/* 15/1xh PCIE */
#define	PCI_PRODUCT_AMD_15_1X_PCIE_3	0x1414		/* 15/1xh PCIE */
#define	PCI_PRODUCT_AMD_15_1X_PCIE_4	0x1415		/* 15/1xh PCIE */
#define	PCI_PRODUCT_AMD_15_1X_PCIE_5	0x1416		/* 15/1xh PCIE */
#define	PCI_PRODUCT_AMD_15_1X_PCIE_6	0x1417		/* 15/1xh PCIE */
#define	PCI_PRODUCT_AMD_15_1X_PCIE_7	0x1418		/* 15/1xh PCIE */
#define	PCI_PRODUCT_AMD_15_1X_IOMMU	0x1419		/* 15/1xh IOMMU */
#define	PCI_PRODUCT_AMD_15_3X_LINK	0x141a		/* 15h Link Cfg */
#define	PCI_PRODUCT_AMD_15_3X_ADDR	0x141b		/* 15h Address Map */
#define	PCI_PRODUCT_AMD_15_3X_DRAM	0x141c		/* 15h DRAM Cfg */
#define	PCI_PRODUCT_AMD_15_3X_MISC	0x141d		/* 15h Misc Cfg */
#define	PCI_PRODUCT_AMD_15_3X_CPU_PM	0x141e		/* 15h CPU Power */
#define	PCI_PRODUCT_AMD_15_3X_MISC_2	0x141f		/* 15h Misc Cfg */
#define	PCI_PRODUCT_AMD_15_3X_RC	0x1422		/* 15h Root Complex */
#define	PCI_PRODUCT_AMD_15_3X_IOMMU	0x1423		/* 15h IOMMU */
#define	PCI_PRODUCT_AMD_15_3X_PCIE_1	0x1424		/* 15h PCIE */
#define	PCI_PRODUCT_AMD_15_3X_PCIE_2	0x1425		/* 15h PCIE */
#define	PCI_PRODUCT_AMD_15_3X_PCIE_3	0x1426		/* 15h PCIE */
#define	PCI_PRODUCT_AMD_16_PCIE	0x1439		/* 16h PCIE */
#define	PCI_PRODUCT_AMD_17_7X_DF_1	0x1440		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_7X_DF_2	0x1441		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_7X_DF_3	0x1442		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_7X_DF_4	0x1443		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_7X_DF_5	0x1444		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_7X_DF_6	0x1445		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_7X_DF_7	0x1446		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_7X_DF_8	0x1447		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_6X_DF_0	0x1448		/* 17h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_6X_DF_1	0x1449		/* 17h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_6X_DF_2	0x144a		/* 17h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_6X_DF_3	0x144b		/* 17h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_6X_DF_4	0x144c		/* 17h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_6X_DF_5	0x144d		/* 17h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_6X_DF_6	0x144e		/* 17h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_6X_DF_7	0x144f		/* 17h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_RC	0x1450		/* 17h Root Complex */
#define	PCI_PRODUCT_AMD_17_IOMMU	0x1451		/* 17h IOMMU */
#define	PCI_PRODUCT_AMD_17_PCIE_1	0x1452		/* 17h PCIE */
#define	PCI_PRODUCT_AMD_17_PCIE_2	0x1453		/* 17h PCIE */
#define	PCI_PRODUCT_AMD_17_PCIE_3	0x1454		/* 17h PCIE */
#define	PCI_PRODUCT_AMD_17_CCP_1	0x1456		/* 17h Crypto */
#define	PCI_PRODUCT_AMD_17_HDA	0x1457		/* 17h HD Audio */
#define	PCI_PRODUCT_AMD_EPYC_TENGB	0x1458		/* EPYC Embedded 3000 10GbE */
#define	PCI_PRODUCT_AMD_17_XHCI_1	0x145c		/* 17h xHCI */
#define	PCI_PRODUCT_AMD_17_XHCI_2	0x145f		/* 17h xHCI */
#define	PCI_PRODUCT_AMD_17_DF_1	0x1460		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_DF_2	0x1461		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_DF_3	0x1462		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_DF_4	0x1463		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_DF_5	0x1464		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_DF_6	0x1465		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_DF_7	0x1466		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_DF_8	0x1467		/* 17h Data Fabric */
#define	PCI_PRODUCT_AMD_17_CCP_2	0x1468		/* 17h Crypto */
#define	PCI_PRODUCT_AMD_17_PCIE_4	0x1470		/* 17h PCIE */
#define	PCI_PRODUCT_AMD_17_PCIE_5	0x1471		/* 17h PCIE */
#define	PCI_PRODUCT_AMD_17_3X_RC	0x1480		/* 17h Root Complex */
#define	PCI_PRODUCT_AMD_17_7X_IOMMU	0x1481		/* 17h IOMMU */
#define	PCI_PRODUCT_AMD_17_7X_HB	0x1482		/* 17h Host */
#define	PCI_PRODUCT_AMD_17_7X_PCIE_1	0x1483		/* 17h PCIE */
#define	PCI_PRODUCT_AMD_17_7X_PCIE_2	0x1484		/* 17h PCIE */
#define	PCI_PRODUCT_AMD_17_3X_CCP	0x1486		/* 17h Crypto */
#define	PCI_PRODUCT_AMD_17_3X_HDA	0x1487		/* 17h HD Audio */
#define	PCI_PRODUCT_AMD_17_7X_XHCI	0x149c		/* 17h xHCI */
#define	PCI_PRODUCT_AMD_19_1X_IOMMU	0x149e		/* 19h/1xh IOMMU */
#define	PCI_PRODUCT_AMD_19_1X_PCIE	0x149f		/* 19h/1xh PCIE */
#define	PCI_PRODUCT_AMD_19_1X_RC	0x14a4		/* 19h/1xh Root Complex */
#define	PCI_PRODUCT_AMD_19_1X_PCIE_1	0x14a5		/* 19h/1xh PCIE */
#define	PCI_PRODUCT_AMD_19_1X_RCEC	0x14a6		/* 19h/1xh RCEC */
#define	PCI_PRODUCT_AMD_19_1X_PCIE_2	0x14a7		/* 19h/1xh PCIE */
#define	PCI_PRODUCT_AMD_19_1X_PCIE_3	0x14aa		/* 19h/1xh PCIE */
#define	PCI_PRODUCT_AMD_19_1X_PCIE_4	0x14ab		/* 19h/1xh PCIE */
#define	PCI_PRODUCT_AMD_19_1X_PCIE_5	0x14ac		/* 19h/1xh PCIE */
#define	PCI_PRODUCT_AMD_19_1X_DF_1	0x14ad		/* 19h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_1X_DF_2	0x14ae		/* 19h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_1X_DF_3	0x14af		/* 19h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_1X_DF_4	0x14b0		/* 19h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_1X_DF_5	0x14b1		/* 19h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_1X_DF_6	0x14b2		/* 19h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_1X_DF_7	0x14b3		/* 19h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_1X_DF_8	0x14b4		/* 19h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_4X_RC	0x14b5		/* 19h/4xh Root Complex */
#define	PCI_PRODUCT_AMD_19_4X_IOMMU	0x14b6		/* 19h/4xh IOMMU */
#define	PCI_PRODUCT_AMD_19_4X_HB_1	0x14b7		/* 19h/4xh Host */
#define	PCI_PRODUCT_AMD_19_4X_PCIE_1	0x14b9		/* 19h/4xh PCIE */
#define	PCI_PRODUCT_AMD_19_4X_PCIE_2	0x14ba		/* 19h/4xh PCIE */
#define	PCI_PRODUCT_AMD_19_1X_XHCI	0x14c9		/* 19h/1xh xHCI */
#define	PCI_PRODUCT_AMD_19_1X_PSP	0x14ca		/* 19h/1xh PSP */
#define	PCI_PRODUCT_AMD_19_6X_RC	0x14d8		/* 19h/6xh Root Complex */
#define	PCI_PRODUCT_AMD_19_6X_IOMMU	0x14d9		/* 19h/6xh IOMMU */
#define	PCI_PRODUCT_AMD_19_6X_HB	0x14da		/* 19h/6xh Host */
#define	PCI_PRODUCT_AMD_19_6X_PCIE_1	0x14db		/* 19h/6xh PCIE */
#define	PCI_PRODUCT_AMD_19_6X_PCIE_2	0x14dd		/* 19h/6xh PCIE */
#define	PCI_PRODUCT_AMD_19_6X_DF_1	0x14e0		/* 19h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_6X_DF_2	0x14e1		/* 19h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_6X_DF_3	0x14e2		/* 19h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_6X_DF_4	0x14e3		/* 19h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_6X_DF_5	0x14e4		/* 19h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_6X_DF_6	0x14e5		/* 19h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_6X_DF_7	0x14e6		/* 19h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_6X_DF_8	0x14e7		/* 19h/6xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_7X_RC	0x14e8		/* 19h/7xh Root Complex */
#define	PCI_PRODUCT_AMD_19_7X_IOMMU	0x14e9		/* 19h/7xh IOMMU */
#define	PCI_PRODUCT_AMD_19_7X_HB	0x14ea		/* 19h/7xh Host */
#define	PCI_PRODUCT_AMD_19_7X_PCIE_1	0x14eb		/* 19h/7xh PCIE */
#define	PCI_PRODUCT_AMD_19_7X_PCIE_2	0x14ed		/* 19h/7xh PCIE */
#define	PCI_PRODUCT_AMD_19_7X_PCIE_3	0x14ee		/* 19h/7xh PCIE */
#define	PCI_PRODUCT_AMD_19_7X_PCIE_4	0x14ef		/* 19h/7xh PCIE */
#define	PCI_PRODUCT_AMD_19_7X_DF_1	0x14f0		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_7X_DF_2	0x14f1		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_7X_DF_3	0x14f2		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_7X_DF_4	0x14f3		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_7X_DF_5	0x14f4		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_7X_DF_6	0x14f5		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_7X_DF_7	0x14f6		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_7X_DF_8	0x14f7		/* 19h/7xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_7X_IPU	0x1502		/* 19h/7xh IPU */
#define	PCI_PRODUCT_AMD_14_HB	0x1510		/* 14h Host */
#define	PCI_PRODUCT_AMD_14_PCIE_1	0x1512		/* 14h PCIE */
#define	PCI_PRODUCT_AMD_14_PCIE_2	0x1513		/* 14h PCIE */
#define	PCI_PRODUCT_AMD_14_PCIE_3	0x1514		/* 14h PCIE */
#define	PCI_PRODUCT_AMD_14_PCIE_4	0x1515		/* 14h PCIE */
#define	PCI_PRODUCT_AMD_14_PCIE_5	0x1516		/* 14h PCIE */
#define	PCI_PRODUCT_AMD_16_LINK	0x1530		/* 16h Link Cfg */
#define	PCI_PRODUCT_AMD_16_ADDR	0x1531		/* 16h Address Map */
#define	PCI_PRODUCT_AMD_16_DRAM	0x1532		/* 16h DRAM Cfg */
#define	PCI_PRODUCT_AMD_16_MISC	0x1533		/* 16h Misc Cfg */
#define	PCI_PRODUCT_AMD_16_CPU_PM	0x1534		/* 16h CPU Power */
#define	PCI_PRODUCT_AMD_16_HB	0x1536		/* 16h Host */
#define	PCI_PRODUCT_AMD_16_CCP	0x1537		/* 16h Crypto */
#define	PCI_PRODUCT_AMD_16_3X_RC	0x1566		/* 16h Root Complex */
#define	PCI_PRODUCT_AMD_16_3X_HB	0x156b		/* 16h Host */
#define	PCI_PRODUCT_AMD_15_6X_LINK	0x1570		/* 15h Link Cfg */
#define	PCI_PRODUCT_AMD_15_6X_ADDR	0x1571		/* 15h Address Map */
#define	PCI_PRODUCT_AMD_15_6X_DRAM	0x1572		/* 15h DRAM Cfg */
#define	PCI_PRODUCT_AMD_15_6X_MISC	0x1573		/* 15h Misc Cfg */
#define	PCI_PRODUCT_AMD_15_6X_CPU_PM	0x1574		/* 15h CPU Power */
#define	PCI_PRODUCT_AMD_15_6X_MISC_2	0x1575		/* 15h Misc Cfg */
#define	PCI_PRODUCT_AMD_15_6X_RC	0x1576		/* 15h Root Complex */
#define	PCI_PRODUCT_AMD_15_6X_IOMMU	0x1577		/* 15h IOMMU */
#define	PCI_PRODUCT_AMD_15_6X_PSP	0x1578		/* 15h PSP 2.0 */
#define	PCI_PRODUCT_AMD_15_6X_AUDIO	0x157a		/* 15h HD Audio */
#define	PCI_PRODUCT_AMD_15_6X_HB_1	0x157b		/* 15h Host */
#define	PCI_PRODUCT_AMD_15_6X_PCIE_1	0x157c		/* 15h PCIE */
#define	PCI_PRODUCT_AMD_15_6X_HB_2	0x157d		/* 15h Host */
#define	PCI_PRODUCT_AMD_16_3X_LINK	0x1580		/* 16h Link Cfg */
#define	PCI_PRODUCT_AMD_16_3X_ADDR	0x1581		/* 16h Address Map */
#define	PCI_PRODUCT_AMD_16_3X_DRAM	0x1582		/* 16h DRAM Cfg */
#define	PCI_PRODUCT_AMD_16_3X_MISC	0x1583		/* 16h Misc Cfg */
#define	PCI_PRODUCT_AMD_16_3X_CPU_PM	0x1584		/* 16h CPU Power */
#define	PCI_PRODUCT_AMD_16_3X_MISC_2	0x1585		/* 16h Misc Cfg */
#define	PCI_PRODUCT_AMD_19_6X_XHCI_1	0x15b6		/* 19h/6xh xHCI */
#define	PCI_PRODUCT_AMD_19_6X_XHCI_2	0x15b7		/* 19h/6xh xHCI */
#define	PCI_PRODUCT_AMD_19_6X_XHCI_3	0x15b8		/* 19h/6xh xHCI */
#define	PCI_PRODUCT_AMD_19_7X_XHCI_1	0x15b9		/* 19h/7xh xHCI */
#define	PCI_PRODUCT_AMD_19_7X_XHCI_2	0x15ba		/* 19h/7xh xHCI */
#define	PCI_PRODUCT_AMD_19_78_XHCI_1	0x15bb		/* 19h/7xh xHCI */
#define	PCI_PRODUCT_AMD_19_78_XHCI_2	0x15bd		/* 19h/7xh xHCI */
#define	PCI_PRODUCT_AMD_19_7X_XHCI_3	0x15c0		/* 19h/7xh xHCI */
#define	PCI_PRODUCT_AMD_19_7X_XHCI_4	0x15c1		/* 19h/7xh xHCI */
#define	PCI_PRODUCT_AMD_19_78_XHCI_3	0x15c2		/* 19h/7xh xHCI */
#define	PCI_PRODUCT_AMD_19_78_XHCI_4	0x15c3		/* 19h/7xh xHCI */
#define	PCI_PRODUCT_AMD_19_7X_PSP	0x15c7		/* 19h/7xh PSP */
#define	PCI_PRODUCT_AMD_17_1X_RC	0x15d0		/* 17h/1xh Root Complex */
#define	PCI_PRODUCT_AMD_17_1X_IOMMU	0x15d1		/* 17h/1xh IOMMU */
#define	PCI_PRODUCT_AMD_17_1X_PCIE_1	0x15d3		/* 17h/1xh PCIE */
#define	PCI_PRODUCT_AMD_19_4X_XHCI_4	0x15d6		/* 19h/4xh xHCI */
#define	PCI_PRODUCT_AMD_19_4X_XHCI_5	0x15d7		/* 19h/4xh xHCI */
#define	PCI_PRODUCT_AMD_17_1X_PCIE_2	0x15db		/* 17h/1xh PCIE */
#define	PCI_PRODUCT_AMD_17_1X_PCIE_3	0x15dc		/* 17h/1xh PCIE */
#define	PCI_PRODUCT_AMD_17_1X_CCP	0x15df		/* 17h/1xh Crypto */
#define	PCI_PRODUCT_AMD_17_1X_XHCI_1	0x15e0		/* 17h/1xh xHCI */
#define	PCI_PRODUCT_AMD_17_1X_XHCI_2	0x15e1		/* 17h/1xh xHCI */
#define	PCI_PRODUCT_AMD_17_1X_ACP	0x15e2		/* 17h/1xh I2S Audio */
#define	PCI_PRODUCT_AMD_17_1X_HDA	0x15e3		/* 17h/1xh HD Audio */
#define	PCI_PRODUCT_AMD_17_1X_SFH	0x15e6		/* 17h/1xh SFH */
#define	PCI_PRODUCT_AMD_17_1X_DF_0	0x15e8		/* 17h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_1X_DF_1	0x15e9		/* 17h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_1X_DF_2	0x15ea		/* 17h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_1X_DF_3	0x15eb		/* 17h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_1X_DF_4	0x15ec		/* 17h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_1X_DF_5	0x15ed		/* 17h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_1X_DF_6	0x15ee		/* 17h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_17_1X_DF_7	0x15ef		/* 17h/1xh Data Fabric */
#define	PCI_PRODUCT_AMD_15_0X_LINK	0x1600		/* 15/0xh Link Cfg */
#define	PCI_PRODUCT_AMD_15_0X_ADDR	0x1601		/* 15/0xh Address Map */
#define	PCI_PRODUCT_AMD_15_0X_DRAM	0x1602		/* 15/0xh DRAM Cfg */
#define	PCI_PRODUCT_AMD_15_0X_MISC	0x1603		/* 15/0xh Misc Cfg */
#define	PCI_PRODUCT_AMD_15_0X_CPU_PM	0x1604		/* 15/0xh CPU Power */
#define	PCI_PRODUCT_AMD_15_0X_HB	0x1605		/* 15/0xh Host */
#define	PCI_PRODUCT_AMD_19_4X_XHCI_1	0x161d		/* 19h/4xh xHCI */
#define	PCI_PRODUCT_AMD_19_4X_XHCI_2	0x161e		/* 19h/4xh xHCI */
#define	PCI_PRODUCT_AMD_19_4X_XHCI_3	0x161f		/* 19h/4xh xHCI */
#define	PCI_PRODUCT_AMD_17_90_XHCI_1	0x162c		/* 17h/90h xHCI */
#define	PCI_PRODUCT_AMD_19_4X_USB4_1	0x162e		/* 19h/4xh USB4 */
#define	PCI_PRODUCT_AMD_19_4X_USB4_2	0x162f		/* 19h/4xh USB4 */
#define	PCI_PRODUCT_AMD_17_6X_RC	0x1630		/* 17h/6xh Root Complex */
#define	PCI_PRODUCT_AMD_17_6X_IOMMU	0x1631		/* 17h/6xh IOMMU */
#define	PCI_PRODUCT_AMD_17_6X_HB	0x1632		/* 17h/6xh Host */
#define	PCI_PRODUCT_AMD_17_6X_PCIE_1	0x1633		/* 17h/6xh PCIE */
#define	PCI_PRODUCT_AMD_17_6X_PCIE_2	0x1634		/* 17h/6xh PCIE */
#define	PCI_PRODUCT_AMD_17_6X_PCIE_3	0x1635		/* 17h/6xh PCIE */
#define	PCI_PRODUCT_AMD_17_6X_XHCI	0x1639		/* 17h/6xh xHCI */
#define	PCI_PRODUCT_AMD_17_90_XHCI_2	0x163b		/* 17h/90h xHCI */
#define	PCI_PRODUCT_AMD_17_90_HB	0x1645		/* 17h/90h Host */
#define	PCI_PRODUCT_AMD_17_90_PCIE_1	0x1647		/* 17h/90h PCIE */
#define	PCI_PRODUCT_AMD_17_90_PCIE_2	0x1648		/* 17h/90h PCIE */
#define	PCI_PRODUCT_AMD_17_90_CCP	0x1649		/* 17h/90h Crypto */
#define	PCI_PRODUCT_AMD_17_90_DF_0	0x1660		/* 17h/90h Data Fabric */
#define	PCI_PRODUCT_AMD_17_90_DF_1	0x1661		/* 17h/90h Data Fabric */
#define	PCI_PRODUCT_AMD_17_90_DF_2	0x1662		/* 17h/90h Data Fabric */
#define	PCI_PRODUCT_AMD_17_90_DF_3	0x1663		/* 17h/90h Data Fabric */
#define	PCI_PRODUCT_AMD_17_90_DF_4	0x1664		/* 17h/90h Data Fabric */
#define	PCI_PRODUCT_AMD_17_90_DF_5	0x1665		/* 17h/90h Data Fabric */
#define	PCI_PRODUCT_AMD_17_90_DF_6	0x1666		/* 17h/90h Data Fabric */
#define	PCI_PRODUCT_AMD_17_90_DF_7	0x1667		/* 17h/90h Data Fabric */
#define	PCI_PRODUCT_AMD_19_7X_USB4_1	0x1668		/* 19h/7xh USB4 */
#define	PCI_PRODUCT_AMD_19_7X_USB4_2	0x1669		/* 19h/7xh USB4 */
#define	PCI_PRODUCT_AMD_19_5X_DF_0	0x166a		/* 19h/5xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_5X_DF_1	0x166b		/* 19h/5xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_5X_DF_2	0x166c		/* 19h/5xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_5X_DF_3	0x166d		/* 19h/5xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_5X_DF_4	0x166e		/* 19h/5xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_5X_DF_5	0x166f		/* 19h/5xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_5X_DF_6	0x1670		/* 19h/5xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_5X_DF_7	0x1671		/* 19h/5xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_4X_DF_0	0x1679		/* 19h/4xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_4X_DF_1	0x167a		/* 19h/4xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_4X_DF_2	0x167b		/* 19h/4xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_4X_DF_3	0x167c		/* 19h/4xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_4X_DF_4	0x167d		/* 19h/4xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_4X_DF_5	0x167e		/* 19h/4xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_4X_DF_6	0x167f		/* 19h/4xh Data Fabric */
#define	PCI_PRODUCT_AMD_19_4X_DF_7	0x1680		/* 19h/4xh Data Fabric */
#define	PCI_PRODUCT_AMD_14_LINK	0x1700		/* 14h Link Cfg */
#define	PCI_PRODUCT_AMD_14_ADDR	0x1701		/* 14h Address Map */
#define	PCI_PRODUCT_AMD_14_DRAM	0x1702		/* 14h DRAM Cfg */
#define	PCI_PRODUCT_AMD_14_MISC	0x1703		/* 14h Misc Cfg */
#define	PCI_PRODUCT_AMD_14_CPU_PM	0x1704		/* 14h CPU Power */
#define	PCI_PRODUCT_AMD_12_HB	0x1705		/* 12h Host */
#define	PCI_PRODUCT_AMD_12_PCIE_1	0x1707		/* 12h PCIE */
#define	PCI_PRODUCT_AMD_12_PCIE_2	0x1708		/* 12h PCIE */
#define	PCI_PRODUCT_AMD_12_PCIE_3	0x1709		/* 12h PCIE */
#define	PCI_PRODUCT_AMD_12_PCIE_4	0x170a		/* 12h PCIE */
#define	PCI_PRODUCT_AMD_12_PCIE_5	0x170b		/* 12h PCIE */
#define	PCI_PRODUCT_AMD_12_PCIE_6	0x170c		/* 12h PCIE */
#define	PCI_PRODUCT_AMD_12_PCIE_7	0x170d		/* 12h PCIE */
#define	PCI_PRODUCT_AMD_14_NB_PM	0x1716		/* 14h NB Power */
#define	PCI_PRODUCT_AMD_14_RESERVED_1	0x1718		/* 14h Reserved */
#define	PCI_PRODUCT_AMD_14_RESERVED_2	0x1719		/* 14h Reserved */
#define	PCI_PRODUCT_AMD_A1100_HB_1	0x1a00		/* A1100 Host */
#define	PCI_PRODUCT_AMD_A1100_HB_2	0x1a01		/* A1100 Host */
#define	PCI_PRODUCT_AMD_A1100_PCIE_1	0x1a02		/* A1100 PCIE */
#define	PCI_PRODUCT_AMD_PCNET_PCI	0x2000		/* 79c970 PCnet-PCI */
#define	PCI_PRODUCT_AMD_PCHOME_PCI	0x2001		/* 79c978 PChome-PCI */
#define	PCI_PRODUCT_AMD_PCSCSI_PCI	0x2020		/* 53c974 PCscsi-PCI */
#define	PCI_PRODUCT_AMD_PCNETS_PCI	0x2040		/* 79C974 PCnet-PCI */
#define	PCI_PRODUCT_AMD_GEODE_LX_PCHB	0x2080		/* Geode LX */
#define	PCI_PRODUCT_AMD_GEODE_LX_VIDEO	0x2081		/* Geode LX Video */
#define	PCI_PRODUCT_AMD_GEODE_LX_CRYPTO	0x2082		/* Geode LX Crypto */
#define	PCI_PRODUCT_AMD_CS5536_PCISB	0x208f		/* CS5536 PCI */
#define	PCI_PRODUCT_AMD_CS5536_PCIB	0x2090		/* CS5536 ISA */
#define	PCI_PRODUCT_AMD_CS5536_AUDIO	0x2093		/* CS5536 Audio */
#define	PCI_PRODUCT_AMD_CS5536_OHCI	0x2094		/* CS5536 USB */
#define	PCI_PRODUCT_AMD_CS5536_EHCI	0x2095		/* CS5536 USB */
#define	PCI_PRODUCT_AMD_CS5536_IDE	0x209a		/* CS5536 IDE */
#define	PCI_PRODUCT_AMD_ELANSC520	0x3000		/* ElanSC520 PCI */
#define	PCI_PRODUCT_AMD_HUDSON2_PCIE_1	0x43a0		/* Hudson-2 PCIE */
#define	PCI_PRODUCT_AMD_HUDSON2_PCIE_2	0x43a1		/* Hudson-2 PCIE */
#define	PCI_PRODUCT_AMD_HUDSON2_PCIE_3	0x43a2		/* Hudson-2 PCIE */
#define	PCI_PRODUCT_AMD_HUDSON2_PCIE_4	0x43a3		/* Hudson-2 PCIE */
#define	PCI_PRODUCT_AMD_300SERIES_PCIE	0x43b4		/* 300 Series PCIE */
#define	PCI_PRODUCT_AMD_300SERIES_SATA	0x43b7		/* 300 Series SATA */
#define	PCI_PRODUCT_AMD_300SERIES_XHCI	0x43bb		/* 300 Series xHCI */
#define	PCI_PRODUCT_AMD_400SERIES_PCIE_1	0x43c6		/* 400 Series PCIE */
#define	PCI_PRODUCT_AMD_400SERIES_PCIE_2	0x43c7		/* 400 Series PCIE */
#define	PCI_PRODUCT_AMD_400SERIES_AHCI	0x43c8		/* 400 Series AHCI */
#define	PCI_PRODUCT_AMD_400SERIES_XHCI_1	0x43d0		/* 400 Series xHCI */
#define	PCI_PRODUCT_AMD_400SERIES_XHCI_2	0x43d1		/* 400 Series xHCI */
#define	PCI_PRODUCT_AMD_500SERIES_PCIE_1	0x43e9		/* 500 Series PCIE */
#define	PCI_PRODUCT_AMD_500SERIES_PCIE_2	0x43ea		/* 500 Series PCIE */
#define	PCI_PRODUCT_AMD_500SERIES_AHCI	0x43eb		/* 500 Series AHCI */
#define	PCI_PRODUCT_AMD_500SERIES_XHCI	0x43ee		/* 500 Series xHCI */
#define	PCI_PRODUCT_AMD_600SERIES_PCIE_1	0x43f4		/* 600 Series PCIE */
#define	PCI_PRODUCT_AMD_600SERIES_PCIE_2	0x43f5		/* 600 Series PCIE */
#define	PCI_PRODUCT_AMD_600SERIES_AHCI	0x43f6		/* 600 Series AHCI */
#define	PCI_PRODUCT_AMD_600SERIES_XHCI	0x43f7		/* 600 Series xHCI */
#define	PCI_PRODUCT_AMD_500SERIES_PCIE_3	0x57a3		/* 500 Series PCIE */
#define	PCI_PRODUCT_AMD_500SERIES_PCIE_4	0x57a4		/* 500 Series PCIE */
#define	PCI_PRODUCT_AMD_500SERIES_PCIE_5	0x57ad		/* 500 Series PCIE */
/* http://www.amd.com/products/cpg/athlon/techdocs/pdf/21910.pdf */
#define	PCI_PRODUCT_AMD_SC751_SC	0x7006		/* 751 System */
#define	PCI_PRODUCT_AMD_SC751_PPB	0x7007		/* 751 */
/* http://www.amd.com/products/cpg/athlon/techdocs/pdf/24462.pdf */
#define	PCI_PRODUCT_AMD_762_PCHB	0x700c		/* 762 PCI */
#define	PCI_PRODUCT_AMD_762_PPB	0x700d		/* 762 */
#define	PCI_PRODUCT_AMD_761_PCHB	0x700e		/* 761 PCI */
#define	PCI_PRODUCT_AMD_761_PPB	0x700f		/* 761 */
#define	PCI_PRODUCT_AMD_755_ISA	0x7400		/* 755 ISA */
#define	PCI_PRODUCT_AMD_755_IDE	0x7401		/* 755 IDE */
#define	PCI_PRODUCT_AMD_755_PMC	0x7403		/* 755 Power */
#define	PCI_PRODUCT_AMD_755_USB	0x7404		/* 755 USB */
/* http://www.amd.com/products/cpg/athlon/techdocs/pdf/22548.pdf */
#define	PCI_PRODUCT_AMD_PBC756_ISA	0x7408		/* 756 ISA */
#define	PCI_PRODUCT_AMD_PBC756_IDE	0x7409		/* 756 IDE */
#define	PCI_PRODUCT_AMD_PBC756_PMC	0x740b		/* 756 Power */
#define	PCI_PRODUCT_AMD_PBC756_USB	0x740c		/* 756 USB Host */
#define	PCI_PRODUCT_AMD_766_ISA	0x7410		/* 766 ISA */
#define	PCI_PRODUCT_AMD_766_IDE	0x7411		/* 766 IDE */
#define	PCI_PRODUCT_AMD_766_USB	0x7412		/* 766 USB */
#define	PCI_PRODUCT_AMD_766_PMC	0x7413		/* 766 Power */
#define	PCI_PRODUCT_AMD_766_USB_HCI	0x7414		/* 766 USB OpenHCI */
#define	PCI_PRODUCT_AMD_PBC768_ISA	0x7440		/* 768 ISA */
#define	PCI_PRODUCT_AMD_PBC768_IDE	0x7441		/* 768 IDE */
#define	PCI_PRODUCT_AMD_PBC768_PMC	0x7443		/* 768 Power */
#define	PCI_PRODUCT_AMD_PBC768_ACA	0x7445		/* 768 AC97 */
#define	PCI_PRODUCT_AMD_PBC768_MD	0x7446		/* 768 Modem */
#define	PCI_PRODUCT_AMD_PBC768_PPB	0x7448		/* 768 */
#define	PCI_PRODUCT_AMD_PBC768_USB	0x7449		/* 768 USB */
#define	PCI_PRODUCT_AMD_8131_PCIX	0x7450		/* 8131 PCIX */
#define	PCI_PRODUCT_AMD_8131_PCIX_IOAPIC	0x7451		/* 8131 PCIX IOAPIC */
#define	PCI_PRODUCT_AMD_8151_SC	0x7454		/* 8151 Sys Control */
#define	PCI_PRODUCT_AMD_8151_AGP	0x7455		/* 8151 AGP */
#define	PCI_PRODUCT_AMD_8132_PCIX	0x7458		/* 8132 PCIX */
#define	PCI_PRODUCT_AMD_8132_PCIX_IOAPIC	0x7459		/* 8132 PCIX IOAPIC */
#define	PCI_PRODUCT_AMD_8111_PPB	0x7460		/* 8111 */
#define	PCI_PRODUCT_AMD_8111_ETHER	0x7462		/* 8111 Ether */
#define	PCI_PRODUCT_AMD_8111_EHCI	0x7463		/* 8111 USB */
#define	PCI_PRODUCT_AMD_8111_USB	0x7464		/* 8111 USB */
#define	PCI_PRODUCT_AMD_PBC8111_LPC	0x7468		/* 8111 LPC */
#define	PCI_PRODUCT_AMD_8111_IDE	0x7469		/* 8111 IDE */
#define	PCI_PRODUCT_AMD_8111_SMB	0x746a		/* 8111 SMBus */
#define	PCI_PRODUCT_AMD_8111_PMC	0x746b		/* 8111 Power */
#define	PCI_PRODUCT_AMD_8111_ACA	0x746d		/* 8111 AC97 */
#define	PCI_PRODUCT_AMD_HUDSON2_SATA_1	0x7800		/* Hudson-2 SATA */
#define	PCI_PRODUCT_AMD_HUDSON2_SATA_2	0x7801		/* Hudson-2 SATA */
#define	PCI_PRODUCT_AMD_HUDSON2_SATA_3	0x7802		/* Hudson-2 SATA */
#define	PCI_PRODUCT_AMD_HUDSON2_SATA_4	0x7803		/* Hudson-2 SATA */
#define	PCI_PRODUCT_AMD_HUDSON2_SATA_5	0x7804		/* Hudson-2 SATA */
#define	PCI_PRODUCT_AMD_HUDSON2_SATA_6	0x7805		/* Hudson-2 SATA */
#define	PCI_PRODUCT_AMD_HUDSON2_SD	0x7806		/* Hudson-2 SD Host Controller */
#define	PCI_PRODUCT_AMD_HUDSON2_OHCI_1	0x7807		/* Hudson-2 USB */
#define	PCI_PRODUCT_AMD_HUDSON2_EHCI	0x7808		/* Hudson-2 USB2 */
#define	PCI_PRODUCT_AMD_HUDSON2_OHCI_2	0x7809		/* Hudson-2 USB */
#define	PCI_PRODUCT_AMD_HUDSON2_SMB	0x780b		/* Hudson-2 SMBus */
#define	PCI_PRODUCT_AMD_HUDSON2_IDE	0x780c		/* Hudson-2 IDE */
#define	PCI_PRODUCT_AMD_HUDSON2_HDA	0x780d		/* Hudson-2 HD Audio */
#define	PCI_PRODUCT_AMD_HUDSON2_LPC	0x780e		/* Hudson-2 LPC */
#define	PCI_PRODUCT_AMD_HUDSON2_PCI	0x780f		/* Hudson-2 PCI */
#define	PCI_PRODUCT_AMD_HUDSON2_XHCI	0x7812		/* Hudson-2 xHCI */
#define	PCI_PRODUCT_AMD_BOLTON_SDMMC	0x7813		/* Bolton SD/MMC */
#define	PCI_PRODUCT_AMD_BOLTON_XHCI	0x7814		/* Bolton xHCI */
#define	PCI_PRODUCT_AMD_KERNCZ_SATA_1	0x7900		/* FCH SATA */
#define	PCI_PRODUCT_AMD_KERNCZ_AHCI_1	0x7901		/* FCH AHCI */
#define	PCI_PRODUCT_AMD_KERNCZ_RAID_1	0x7902		/* FCH RAID */
#define	PCI_PRODUCT_AMD_KERNCZ_RAID_2	0x7903		/* FCH RAID */
#define	PCI_PRODUCT_AMD_KERNCZ_AHCI_2	0x7904		/* FCH AHCI */
#define	PCI_PRODUCT_AMD_KERNCZ_EHCI	0x7908		/* FCH USB2 */
#define	PCI_PRODUCT_AMD_KERNCZ_SMB	0x790b		/* FCH SMBus */
#define	PCI_PRODUCT_AMD_KERNCZ_LPC	0x790e		/* FCH LPC */
#define	PCI_PRODUCT_AMD_KERNCZ_XHCI	0x7914		/* FCH xHCI */
#define	PCI_PRODUCT_AMD_RS780_HB	0x9600		/* RS780 Host */
#define	PCI_PRODUCT_AMD_RS880_HB	0x9601		/* RS880 Host */
#define	PCI_PRODUCT_AMD_RS780_PCIE_1	0x9602		/* RS780 PCIE */
#define	PCI_PRODUCT_AMD_RS780_PCIE_2	0x9603		/* RS780 PCIE */
#define	PCI_PRODUCT_AMD_RS780_PCIE_3	0x9604		/* RS780 PCIE */
#define	PCI_PRODUCT_AMD_RS780_PCIE_4	0x9605		/* RS780 PCIE */
#define	PCI_PRODUCT_AMD_RS780_PCIE_5	0x9606		/* RS780 PCIE */
#define	PCI_PRODUCT_AMD_RS780_PCIE_6	0x9607		/* RS780 PCIE */
#define	PCI_PRODUCT_AMD_RS780_PCIE_7	0x9608		/* RS780 PCIE */
#define	PCI_PRODUCT_AMD_RS780_PCIE_8	0x9609		/* RS780 PCIE */
#define	PCI_PRODUCT_AMD_RS780_PCIE_9	0x960b		/* RS780 PCIE */

/* AMI */
#define	PCI_PRODUCT_AMI_MEGARAID	0x1960		/* MegaRAID */
#define	PCI_PRODUCT_AMI_MEGARAID428	0x9010		/* MegaRAID Series 428 */
#define	PCI_PRODUCT_AMI_MEGARAID434	0x9060		/* MegaRAID Series 434 */

/* Ampere Computing */
#define	PCI_PRODUCT_AMPERE_EMAG_PCIE_1	0xe005		/* eMAG PCIe */
#define	PCI_PRODUCT_AMPERE_EMAG_PCIE_2	0xe006		/* eMAG PCIe */
#define	PCI_PRODUCT_AMPERE_EMAG_PCIE_3	0xe007		/* eMAG PCIe */
#define	PCI_PRODUCT_AMPERE_EMAG_PCIE_4	0xe008		/* eMAG PCIe */
#define	PCI_PRODUCT_AMPERE_EMAG_PCIE_5	0xe009		/* eMAG PCIe */
#define	PCI_PRODUCT_AMPERE_EMAG_PCIE_6	0xe00a		/* eMAG PCIe */
#define	PCI_PRODUCT_AMPERE_EMAG_PCIE_7	0xe00b		/* eMAG PCIe */
#define	PCI_PRODUCT_AMPERE_EMAG_PCIE_8	0xe00c		/* eMAG PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_A	0xe100		/* Altra PCIe Root */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_A0	0xe101		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_A1	0xe102		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_A2	0xe103		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_A3	0xe104		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_A4	0xe105		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_A5	0xe106		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_A6	0xe107		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_A7	0xe108		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_B	0xe110		/* Altra PCIe Root */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_B0	0xe111		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_B1	0xe112		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_B2	0xe113		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_B3	0xe114		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_B4	0xe115		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_B5	0xe116		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_B6	0xe117		/* Altra PCIe */
#define	PCI_PRODUCT_AMPERE_ALTRA_PCIE_B7	0xe118		/* Altra PCIe */

/* Antares Microsystems products */
#define	PCI_PRODUCT_ANTARES_TC9021	0x1021		/* TC9021 */

/* Apple products */
#define	PCI_PRODUCT_APPLE_BANDIT	0x0001		/* Bandit */
#define	PCI_PRODUCT_APPLE_GC	0x0002		/* GC */
#define	PCI_PRODUCT_APPLE_OHARE	0x0007		/* OHare */
#define	PCI_PRODUCT_APPLE_HEATHROW	0x0010		/* Heathrow */
#define	PCI_PRODUCT_APPLE_PADDINGTON	0x0017		/* Paddington */
#define	PCI_PRODUCT_APPLE_UNINORTHETH_FW	0x0018		/* Uni-N Eth Firewire */
#define	PCI_PRODUCT_APPLE_USB	0x0019		/* USB */
#define	PCI_PRODUCT_APPLE_UNINORTHETH	0x001e		/* Uni-N Eth */
#define	PCI_PRODUCT_APPLE_UNINORTH	0x001f		/* Uni-N */
#define	PCI_PRODUCT_APPLE_UNINORTH_AGP	0x0020		/* Uni-N AGP */
#define	PCI_PRODUCT_APPLE_UNINORTHGMAC	0x0021		/* Uni-N GMAC */
#define	PCI_PRODUCT_APPLE_KEYLARGO	0x0022		/* Keylargo */
#define	PCI_PRODUCT_APPLE_PANGEA_GMAC	0x0024		/* Pangea GMAC */
#define	PCI_PRODUCT_APPLE_PANGEA_MACIO	0x0025		/* Pangea Macio */
#define	PCI_PRODUCT_APPLE_PANGEA_OHCI	0x0026		/* Pangea USB */
#define	PCI_PRODUCT_APPLE_PANGEA_AGP	0x0027		/* Pangea AGP */
#define	PCI_PRODUCT_APPLE_PANGEA	0x0028		/* Pangea */
#define	PCI_PRODUCT_APPLE_PANGEA_PCI	0x0029		/* Pangea PCI */
#define	PCI_PRODUCT_APPLE_UNINORTH2_AGP	0x002d		/* Uni-N2 AGP */
#define	PCI_PRODUCT_APPLE_UNINORTH2	0x002e		/* Uni-N2 Host */
#define	PCI_PRODUCT_APPLE_UNINORTH2ETH	0x002f		/* Uni-N2 Host */
#define	PCI_PRODUCT_APPLE_PANGEA_FW	0x0030		/* Pangea FireWire */
#define	PCI_PRODUCT_APPLE_UNINORTH_FW	0x0031		/* UniNorth Firewire */
#define	PCI_PRODUCT_APPLE_UNINORTH2GMAC	0x0032		/* Uni-N2 GMAC */
#define	PCI_PRODUCT_APPLE_UNINORTH_ATA	0x0033		/* Uni-N ATA */
#define	PCI_PRODUCT_APPLE_UNINORTH_AGP3	0x0034		/* UniNorth AGP */
#define	PCI_PRODUCT_APPLE_UNINORTH5	0x0035		/* UniNorth PCI */
#define	PCI_PRODUCT_APPLE_UNINORTH6	0x0036		/* UniNorth PCI */
#define	PCI_PRODUCT_APPLE_INTREPID_ATA	0x003b		/* Intrepid ATA */
#define	PCI_PRODUCT_APPLE_INTREPID	0x003e		/* Intrepid */
#define	PCI_PRODUCT_APPLE_INTREPID_OHCI	0x003f		/* Intrepid USB */
#define	PCI_PRODUCT_APPLE_K2_USB	0x0040		/* K2 USB */
#define	PCI_PRODUCT_APPLE_K2_MACIO	0x0041		/* K2 Macio */
#define	PCI_PRODUCT_APPLE_K2_FW	0x0042		/* K2 Firewire */
#define	PCI_PRODUCT_APPLE_K2_ATA	0x0043		/* K2 ATA */
#define	PCI_PRODUCT_APPLE_U3_PPB1	0x0045		/* U3 */
#define	PCI_PRODUCT_APPLE_U3_PPB2	0x0046		/* U3 */
#define	PCI_PRODUCT_APPLE_U3_PPB3	0x0047		/* U3 */
#define	PCI_PRODUCT_APPLE_U3_PPB4	0x0048		/* U3 */
#define	PCI_PRODUCT_APPLE_U3_PPB5	0x0049		/* U3 */
#define	PCI_PRODUCT_APPLE_U3_HT	0x004a		/* U3 HyperTransport */
#define	PCI_PRODUCT_APPLE_U3_AGP	0x004b		/* U3 AGP */
#define	PCI_PRODUCT_APPLE_K2_GMAC	0x004c		/* K2 GMAC */
#define	PCI_PRODUCT_APPLE_SHASTA	0x004f		/* Shasta */
#define	PCI_PRODUCT_APPLE_SHASTA_ATA	0x0050		/* Shasta ATA */
#define	PCI_PRODUCT_APPLE_SHASTA_GMAC	0x0051		/* Shasta GMAC */
#define	PCI_PRODUCT_APPLE_SHASTA_FW	0x0052		/* Shasta Firewire */
#define	PCI_PRODUCT_APPLE_SHASTA_PCI1	0x0053		/* Shasta PCI */
#define	PCI_PRODUCT_APPLE_SHASTA_PCI2	0x0054		/* Shasta PCI */
#define	PCI_PRODUCT_APPLE_SHASTA_PCI3	0x0055		/* Shasta PCI */
#define	PCI_PRODUCT_APPLE_SHASTA_HT	0x0056		/* Shasta HyperTransport */
#define	PCI_PRODUCT_APPLE_K2	0x0057		/* K2 */
#define	PCI_PRODUCT_APPLE_U3L_AGP	0x0058		/* U3L AGP */
#define	PCI_PRODUCT_APPLE_K2_AGP	0x0059		/* K2 AGP */
#define	PCI_PRODUCT_APPLE_INTREPID2_AGP	0x0066		/* Intrepid 2 AGP */
#define	PCI_PRODUCT_APPLE_INTREPID2_PCI1	0x0067		/* Intrepid 2 PCI */
#define	PCI_PRODUCT_APPLE_INTREPID2_PCI2	0x0068		/* Intrepid 2 PCI */
#define	PCI_PRODUCT_APPLE_INTREPID2_ATA	0x0069		/* Intrepid 2 ATA */
#define	PCI_PRODUCT_APPLE_INTREPID2_FW	0x006a		/* Intrepid 2 FireWire */
#define	PCI_PRODUCT_APPLE_INTREPID2_GMAC	0x006b		/* Intrepid 2 GMAC */
#define	PCI_PRODUCT_APPLE_M1_PCIE	0x100c		/* M1 PCIe */
#define	PCI_PRODUCT_APPLE_BCM5701	0x1645		/* BCM5701 */
#define	PCI_PRODUCT_APPLE_NVME1	0x2001		/* NVMe */
#define	PCI_PRODUCT_APPLE_NVME2	0x2003		/* NVMe */

/* Aquantia Corp. */
#define	PCI_PRODUCT_AQUANTIA_AQC100	0x00b1		/* AQC100 */
#define	PCI_PRODUCT_AQUANTIA_AQC113	0x04c0		/* AQC113 */
#define	PCI_PRODUCT_AQUANTIA_AQC107	0x07b1		/* AQC107 */
#define	PCI_PRODUCT_AQUANTIA_AQC108	0x08b1		/* AQC108 */
#define	PCI_PRODUCT_AQUANTIA_AQC109	0x09b1		/* AQC109 */
#define	PCI_PRODUCT_AQUANTIA_AQC111	0x11b1		/* AQC111 */
#define	PCI_PRODUCT_AQUANTIA_AQC116C	0x11c0		/* AQC116C */
#define	PCI_PRODUCT_AQUANTIA_AQC112	0x12b1		/* AQC112 */
#define	PCI_PRODUCT_AQUANTIA_AQC115C	0x12c0		/* AQC115C */
#define	PCI_PRODUCT_AQUANTIA_AQC113C	0x14c0		/* AQC113C */
#define	PCI_PRODUCT_AQUANTIA_AQC113CA	0x34c0		/* AQC113CA */
#define	PCI_PRODUCT_AQUANTIA_AQC100S	0x80b1		/* AQC100S */
#define	PCI_PRODUCT_AQUANTIA_AQC107S	0x87b1		/* AQC107S */
#define	PCI_PRODUCT_AQUANTIA_AQC108S	0x88b1		/* AQC108S */
#define	PCI_PRODUCT_AQUANTIA_AQC109S	0x89b1		/* AQC109S */
#define	PCI_PRODUCT_AQUANTIA_AQC111S	0x91b1		/* AQC111S */
#define	PCI_PRODUCT_AQUANTIA_AQC112S	0x92b1		/* AQC112S */
#define	PCI_PRODUCT_AQUANTIA_AQC114CS	0x93c0		/* AQC114CS */
#define	PCI_PRODUCT_AQUANTIA_AQC113CS	0x94c0		/* AQC113CS */
#define	PCI_PRODUCT_AQUANTIA_D100	0xd100		/* D100 */
#define	PCI_PRODUCT_AQUANTIA_D107	0xd107		/* D107 */
#define	PCI_PRODUCT_AQUANTIA_D108	0xd108		/* D108 */
#define	PCI_PRODUCT_AQUANTIA_D109	0xd109		/* D109 */

/* Aralion products */
#define	PCI_PRODUCT_ARALION_ARS106S	0x0301		/* ARS106S */
#define	PCI_PRODUCT_ARALION_ARS0303D	0x0303		/* ARS0303D */

/* ARC Logic products */
#define	PCI_PRODUCT_ARC_USB	0x0003		/* USB */
#define	PCI_PRODUCT_ARC_1000PV	0xa091		/* 1000PV */
#define	PCI_PRODUCT_ARC_2000PV	0xa099		/* 2000PV */
#define	PCI_PRODUCT_ARC_2000MT	0xa0a1		/* 2000MT */
#define	PCI_PRODUCT_ARC_2000MI	0xa0a9		/* 2000MI */

/* Areca products */
#define	PCI_PRODUCT_ARECA_ARC1110	0x1110		/* ARC-1110 */
#define	PCI_PRODUCT_ARECA_ARC1120	0x1120		/* ARC-1120 */
#define	PCI_PRODUCT_ARECA_ARC1130	0x1130		/* ARC-1130 */
#define	PCI_PRODUCT_ARECA_ARC1160	0x1160		/* ARC-1160 */
#define	PCI_PRODUCT_ARECA_ARC1170	0x1170		/* ARC-1170 */
#define	PCI_PRODUCT_ARECA_ARC1200	0x1200		/* ARC-1200 */
#define	PCI_PRODUCT_ARECA_ARC1200_B	0x1201		/* ARC-1200B */
#define	PCI_PRODUCT_ARECA_ARC1202	0x1202		/* ARC-1202 */
#define	PCI_PRODUCT_ARECA_ARC1210	0x1210		/* ARC-1210 */
#define	PCI_PRODUCT_ARECA_ARC1214	0x1214		/* ARC-1214 */
#define	PCI_PRODUCT_ARECA_ARC1220	0x1220		/* ARC-1220 */
#define	PCI_PRODUCT_ARECA_ARC1230	0x1230		/* ARC-1230 */
#define	PCI_PRODUCT_ARECA_ARC1260	0x1260		/* ARC-1260 */
#define	PCI_PRODUCT_ARECA_ARC1270	0x1270		/* ARC-1270 */
#define	PCI_PRODUCT_ARECA_ARC1280	0x1280		/* ARC-1280 */
#define	PCI_PRODUCT_ARECA_ARC1380	0x1380		/* ARC-1380 */
#define	PCI_PRODUCT_ARECA_ARC1381	0x1381		/* ARC-1381 */
#define	PCI_PRODUCT_ARECA_ARC1680	0x1680		/* ARC-1680 */
#define	PCI_PRODUCT_ARECA_ARC1681	0x1681		/* ARC-1681 */
#define	PCI_PRODUCT_ARECA_ARC1880	0x1880		/* ARC-1880 */

/* ASIX Electronics products */
#define	PCI_PRODUCT_ASIX_AX88140A	0x1400		/* AX88140A/88141 */
#define	PCI_PRODUCT_ASIX_AX99100	0x9100		/* AX99100 */

/* ASMedia products */
#define	PCI_PRODUCT_ASMEDIA_ASM1061_SATA	0x0611		/* ASM1061 SATA */
#define	PCI_PRODUCT_ASMEDIA_ASM1061_AHCI	0x0612		/* ASM1061 AHCI */
#define	PCI_PRODUCT_ASMEDIA_ASM1042	0x1042		/* ASM1042 xHCI */
#define	PCI_PRODUCT_ASMEDIA_ASM1080	0x1080		/* ASM1083/1085 PCIE-PCI */
#define	PCI_PRODUCT_ASMEDIA_ASM1042A	0x1142		/* ASM1042A xHCI */
#define	PCI_PRODUCT_ASMEDIA_ASM1182E	0x1182		/* ASM1182e */
#define	PCI_PRODUCT_ASMEDIA_ASM1184E	0x1184		/* ASM1184e */
#define	PCI_PRODUCT_ASMEDIA_ASM1042AE	0x1242		/* ASM1042AE xHCI */
#define	PCI_PRODUCT_ASMEDIA_ASM1143	0x1343		/* ASM1143 xHCI */
#define	PCI_PRODUCT_ASMEDIA_ASM2142	0x2142		/* ASM2142 xHCI */
#define	PCI_PRODUCT_ASMEDIA_ASM2824	0x2824		/* ASM2824 */

/* ASPEED Technology products */
#define	PCI_PRODUCT_ASPEED_AST1150	0x1150		/* AST1150 PCI */
#define	PCI_PRODUCT_ASPEED_AST1180	0x1180		/* AST1180 */
#define	PCI_PRODUCT_ASPEED_AST2000	0x2000		/* AST2000 */
#define	PCI_PRODUCT_ASPEED_AST2100	0x2100		/* AST2100 */

/* Asustek products */
#define	PCI_PRODUCT_ASUSTEK_HFCPCI	0x0675		/* ISDN */

/* Atheros products */
#define	PCI_PRODUCT_ATHEROS_AR5210	0x0007		/* AR5210 */
#define	PCI_PRODUCT_ATHEROS_AR5311	0x0011		/* AR5311 */
#define	PCI_PRODUCT_ATHEROS_AR5211	0x0012		/* AR5211 */
#define	PCI_PRODUCT_ATHEROS_AR5212	0x0013		/* AR5212 */
#define	PCI_PRODUCT_ATHEROS_AR5212_2	0x0014		/* AR5212 */
#define	PCI_PRODUCT_ATHEROS_AR5212_3	0x0015		/* AR5212 */
#define	PCI_PRODUCT_ATHEROS_AR5212_4	0x0016		/* AR5212 */
#define	PCI_PRODUCT_ATHEROS_AR5212_5	0x0017		/* AR5212 */
#define	PCI_PRODUCT_ATHEROS_AR5212_6	0x0018		/* AR5212 */
#define	PCI_PRODUCT_ATHEROS_AR5212_7	0x0019		/* AR5212 */
#define	PCI_PRODUCT_ATHEROS_AR2413	0x001a		/* AR2413 */
#define	PCI_PRODUCT_ATHEROS_AR5413	0x001b		/* AR5413 */
#define	PCI_PRODUCT_ATHEROS_AR5424	0x001c		/* AR5424 */
#define	PCI_PRODUCT_ATHEROS_AR2417	0x001d		/* AR2417 */
#define	PCI_PRODUCT_ATHEROS_AR5416	0x0023		/* AR5416 */
#define	PCI_PRODUCT_ATHEROS_AR5418	0x0024		/* AR5418 */
#define	PCI_PRODUCT_ATHEROS_AR9160	0x0027		/* AR9160 */
#define	PCI_PRODUCT_ATHEROS_AR9280	0x0029		/* AR9280 */
#define	PCI_PRODUCT_ATHEROS_AR928X	0x002a		/* AR928X */
#define	PCI_PRODUCT_ATHEROS_AR9285	0x002b		/* AR9285 */
#define	PCI_PRODUCT_ATHEROS_AR2427	0x002c		/* AR2427 */
#define	PCI_PRODUCT_ATHEROS_AR9227	0x002d		/* AR9227 */
#define	PCI_PRODUCT_ATHEROS_AR9287	0x002e		/* AR9287 */
#define	PCI_PRODUCT_ATHEROS_AR9300	0x0030		/* AR9300 */
#define	PCI_PRODUCT_ATHEROS_AR9485	0x0032		/* AR9485 */
#define	PCI_PRODUCT_ATHEROS_AR9462	0x0034		/* AR9462 */
#define	PCI_PRODUCT_ATHEROS_AR9565	0x0036		/* AR9565 */
#define	PCI_PRODUCT_ATHEROS_QCA988X	0x003c		/* QCA986x/988x */
#define	PCI_PRODUCT_ATHEROS_QCA6174	0x003e		/* QCA6174 */
#define	PCI_PRODUCT_ATHEROS_QCA6164	0x0041		/* QCA6164 */
#define	PCI_PRODUCT_ATHEROS_QCA9377	0x0042		/* QCA9377 */
#define	PCI_PRODUCT_ATHEROS_AR5210_AP	0x0207		/* AR5210 */
#define	PCI_PRODUCT_ATHEROS_AR5212_IBM	0x1014		/* AR5212 */
#define	PCI_PRODUCT_ATHEROS_AR5210_DEFAULT	0x1107		/* AR5210 */
#define	PCI_PRODUCT_ATHEROS_AR5211_DEFAULT	0x1112		/* AR5211 */
#define	PCI_PRODUCT_ATHEROS_AR5212_DEFAULT	0x1113		/* AR5212 */
#define	PCI_PRODUCT_ATHEROS_AR5212_FPGA	0xf013		/* AR5212 */
#define	PCI_PRODUCT_ATHEROS_AR5211_FPGA11B	0xf11b		/* AR5211Ref */
#define	PCI_PRODUCT_ATHEROS_AR5211_LEGACY	0xff12		/* AR5211Ref */

/* ATI Technologies */
#define	PCI_PRODUCT_ATI_KRACKAN_POINT	0x1114		/* Krackan Point */
#define	PCI_PRODUCT_ATI_KAVERI_1	0x1304		/* Kaveri */
#define	PCI_PRODUCT_ATI_KAVERI_2	0x1305		/* Kaveri */
#define	PCI_PRODUCT_ATI_KAVERI_3	0x1306		/* Kaveri */
#define	PCI_PRODUCT_ATI_KAVERI_4	0x1307		/* Kaveri */
#define	PCI_PRODUCT_ATI_KAVERI_HDA	0x1308		/* Kaveri HD Audio */
#define	PCI_PRODUCT_ATI_KAVERI_5	0x1309		/* Kaveri Radeon R6/R7 */
#define	PCI_PRODUCT_ATI_KAVERI_6	0x130a		/* Kaveri Radeon R6 */
#define	PCI_PRODUCT_ATI_KAVERI_7	0x130b		/* Kaveri Radeon R4 */
#define	PCI_PRODUCT_ATI_KAVERI_8	0x130c		/* Kaveri Radeon R7 */
#define	PCI_PRODUCT_ATI_KAVERI_9	0x130d		/* Kaveri Radeon R6 */
#define	PCI_PRODUCT_ATI_KAVERI_10	0x130e		/* Kaveri Radeon R5 */
#define	PCI_PRODUCT_ATI_KAVERI_11	0x130f		/* Kaveri Radeon R7 */
#define	PCI_PRODUCT_ATI_KAVERI_12	0x1310		/* Kaveri */
#define	PCI_PRODUCT_ATI_KAVERI_13	0x1311		/* Kaveri */
#define	PCI_PRODUCT_ATI_KAVERI_14	0x1312		/* Kaveri */
#define	PCI_PRODUCT_ATI_KAVERI_15	0x1313		/* Kaveri Radeon R7 */
#define	PCI_PRODUCT_ATI_RADEON_HD6310_HDA	0x1314		/* Radeon HD 6310 HD Audio */
#define	PCI_PRODUCT_ATI_KAVERI_16	0x1315		/* Kaveri Radeon R5 */
#define	PCI_PRODUCT_ATI_KAVERI_17	0x1316		/* Kaveri Radeon R5 */
#define	PCI_PRODUCT_ATI_KAVERI_18	0x1317		/* Kaveri */
#define	PCI_PRODUCT_ATI_KAVERI_19	0x1318		/* Kaveri Radeon R5 */
#define	PCI_PRODUCT_ATI_KAVERI_20	0x131b		/* Kaveri Radeon R4 */
#define	PCI_PRODUCT_ATI_KAVERI_21	0x131c		/* Kaveri Radeon R7 */
#define	PCI_PRODUCT_ATI_KAVERI_22	0x131d		/* Kaveri Radeon R6 */
#define	PCI_PRODUCT_ATI_GRANITE_RIDGE	0x13c0		/* Granite Ridge */
#define	PCI_PRODUCT_ATI_CYAN_SKILLFISH_1	0x13fe		/* Cyan Skillfish */
#define	PCI_PRODUCT_ATI_VANGOGH_0932	0x1435		/* Van Gogh */
#define	PCI_PRODUCT_ATI_CYAN_SKILLFISH_2	0x143f		/* Cyan Skillfish */
#define	PCI_PRODUCT_ATI_PPB_1	0x1478		/* PCIE */
#define	PCI_PRODUCT_ATI_PPB_2	0x1479		/* PCIE */
#define	PCI_PRODUCT_ATI_MENDOCINO	0x1506		/* Mendocino */
#define	PCI_PRODUCT_ATI_STRIX_POINT	0x150e		/* Strix Point */
#define	PCI_PRODUCT_ATI_STRIX_HALO	0x1586		/* Strix Halo */
#define	PCI_PRODUCT_ATI_PHOENIX_1	0x15bf		/* Phoenix */
#define	PCI_PRODUCT_ATI_PHOENIX_2	0x15c8		/* Phoenix */
#define	PCI_PRODUCT_ATI_PICASSO	0x15d8		/* Picasso */
#define	PCI_PRODUCT_ATI_RAVEN_VEGA	0x15dd		/* Radeon Vega */
#define	PCI_PRODUCT_ATI_RAVEN_VEGA_HDA	0x15de		/* Radeon Vega HD Audio */
#define	PCI_PRODUCT_ATI_BARCELO	0x15e7		/* Barcelo */
#define	PCI_PRODUCT_ATI_RENOIR	0x1636		/* Renoir */
#define	PCI_PRODUCT_ATI_RENOIR_HDA	0x1637		/* Renoir HD Audio */
#define	PCI_PRODUCT_ATI_CEZANNE	0x1638		/* Cezanne */
#define	PCI_PRODUCT_ATI_VANGOGH_0405	0x163f		/* Van Gogh */
#define	PCI_PRODUCT_ATI_VANGOGH_HDA	0x1640		/* Van Gogh HD Audio */
#define	PCI_PRODUCT_ATI_LUCIENNE	0x164c		/* Lucienne */
#define	PCI_PRODUCT_ATI_YELLOW_CARP_1	0x164d		/* Rembrandt */
#define	PCI_PRODUCT_ATI_RAPHAEL	0x164e		/* Raphael */
#define	PCI_PRODUCT_ATI_YELLOW_CARP_2	0x1681		/* Rembrandt */
#define	PCI_PRODUCT_ATI_RADEON_HD6500D_HDA	0x1714		/* Radeon HD 6500D HD Audio */
#define	PCI_PRODUCT_ATI_HAWK_POINT_1	0x1900		/* Hawk Point */
#define	PCI_PRODUCT_ATI_HAWK_POINT_2	0x1901		/* Hawk Point */
#define	PCI_PRODUCT_ATI_RADEON_M241P	0x3150		/* Mobility Radeon X600 */
#define	PCI_PRODUCT_ATI_FIREMV_2400_1	0x3151		/* FireMV 2400 */
#define	PCI_PRODUCT_ATI_RADEON_X300M24	0x3152		/* Mobility Radeon X300 */
#define	PCI_PRODUCT_ATI_FIREGL_M24GL	0x3154		/* FireGL M24 GL */
#define	PCI_PRODUCT_ATI_FIREMV_2400_2	0x3155		/* FireMV 2400 */
#define	PCI_PRODUCT_ATI_RADEON_X600_RV380	0x3e50		/* Radeon X600 */
#define	PCI_PRODUCT_ATI_FIREGL_V3200	0x3e54		/* FireGL V3200 */
#define	PCI_PRODUCT_ATI_RADEON_X600_RV380_S	0x3e70		/* Radeon X600 Sec */
#define	PCI_PRODUCT_ATI_RADEON_IGP320	0x4136		/* Radeon IGP 320 */
#define	PCI_PRODUCT_ATI_RADEON_IGP340	0x4137		/* Radeon IGP 340 */
#define	PCI_PRODUCT_ATI_RADEON_9500PRO	0x4144		/* Radeon 9500 Pro */
#define	PCI_PRODUCT_ATI_RADEON_AE9700PRO	0x4145		/* Radeon AE 9700 Pro */
#define	PCI_PRODUCT_ATI_RADEON_AF9600TX	0x4146		/* Radeon AF 9600TX */
#define	PCI_PRODUCT_ATI_FIREGL_AGZ1	0x4147		/* FireGL AGZ1 */
#define	PCI_PRODUCT_ATI_RADEON_AH_9800SE	0x4148		/* Radeon AH 9800 SE */
#define	PCI_PRODUCT_ATI_RADEON_AI_9800	0x4149		/* Radeon AI 9800 */
#define	PCI_PRODUCT_ATI_RADEON_AJ_9800	0x414a		/* Radeon AJ 9800 */
#define	PCI_PRODUCT_ATI_FIREGL_AKX2	0x414b		/* FireGL AK X2 */
#define	PCI_PRODUCT_ATI_RADEON_9600PRO	0x4150		/* Radeon 9600 Pro */
#define	PCI_PRODUCT_ATI_RADEON_9600LE	0x4151		/* Radeon 9600 */
#define	PCI_PRODUCT_ATI_RADEON_9600XT	0x4152		/* Radeon 9600 */
#define	PCI_PRODUCT_ATI_RADEON_9550	0x4153		/* Radeon 9550 */
#define	PCI_PRODUCT_ATI_FIREGL_ATT2	0x4154		/* FireGL */
#define	PCI_PRODUCT_ATI_RADEON_9650	0x4155		/* Radeon 9650 */
#define	PCI_PRODUCT_ATI_FIREGL_AVT2	0x4156		/* FireGL */
#define	PCI_PRODUCT_ATI_MACH32	0x4158		/* Mach32 */
#define	PCI_PRODUCT_ATI_RADEON_9500PRO_S	0x4164		/* Radeon 9500 Pro Sec */
#define	PCI_PRODUCT_ATI_RADEON_9600PRO_S	0x4170		/* Radeon 9600 Pro Sec */
#define	PCI_PRODUCT_ATI_RADEON_9600LE_S	0x4171		/* Radeon 9600 LE Sec */
#define	PCI_PRODUCT_ATI_RADEON_9600XT_S	0x4172		/* Radeon 9600 XT Sec */
#define	PCI_PRODUCT_ATI_RADEON_9550_S	0x4173		/* Radeon 9550 Sec */
#define	PCI_PRODUCT_ATI_RADEON_IGP_RS250	0x4237		/* Radeon IGP */
#define	PCI_PRODUCT_ATI_R200_BB	0x4242		/* Radeon 8500 */
#define	PCI_PRODUCT_ATI_R200_BC	0x4243		/* Radeon BC R200 */
#define	PCI_PRODUCT_ATI_RADEON_IGP320M	0x4336		/* Radeon IGP 320M */
#define	PCI_PRODUCT_ATI_MOBILITY_M6	0x4337		/* Mobility M6 */
#define	PCI_PRODUCT_ATI_SB200_AUDIO	0x4341		/* SB200 AC97 */
#define	PCI_PRODUCT_ATI_SB200_PCI	0x4342		/* SB200 PCI */
#define	PCI_PRODUCT_ATI_SB200_EHCI	0x4345		/* SB200 USB2 */
#define	PCI_PRODUCT_ATI_SB200_OHCI_1	0x4347		/* SB200 USB */
#define	PCI_PRODUCT_ATI_SB200_OHCI_2	0x4348		/* SB200 USB */
#define	PCI_PRODUCT_ATI_SB200_IDE	0x4349		/* SB200 IDE */
#define	PCI_PRODUCT_ATI_SB200_ISA	0x434c		/* SB200 ISA */
#define	PCI_PRODUCT_ATI_SB200_MODEM	0x434d		/* SB200 Modem */
#define	PCI_PRODUCT_ATI_SB200_SMB	0x4353		/* SB200 SMBus */
#define	PCI_PRODUCT_ATI_MACH64_CT	0x4354		/* Mach64 CT */
#define	PCI_PRODUCT_ATI_MACH64_CX	0x4358		/* Mach64 CX */
#define	PCI_PRODUCT_ATI_SB300_AUDIO	0x4361		/* SB300 AC97 */
#define	PCI_PRODUCT_ATI_SB300_PCI	0x4362		/* SB300 PCI */
#define	PCI_PRODUCT_ATI_SB300_SMB	0x4363		/* SB300 SMBus */
#define	PCI_PRODUCT_ATI_SB300_EHCI	0x4365		/* SB300 USB2 */
#define	PCI_PRODUCT_ATI_SB300_OHCI_1	0x4367		/* SB300 USB */
#define	PCI_PRODUCT_ATI_SB300_OHCI_2	0x4368		/* SB300 USB */
#define	PCI_PRODUCT_ATI_SB300_IDE	0x4369		/* SB300 IDE */
#define	PCI_PRODUCT_ATI_SB300_ISA	0x436c		/* SB300 ISA */
#define	PCI_PRODUCT_ATI_SB300_MODEM	0x436d		/* SB300 Modem */
#define	PCI_PRODUCT_ATI_SB300_SATA	0x436e		/* SB300 SATA */
#define	PCI_PRODUCT_ATI_SB400_AUDIO	0x4370		/* SB400 AC97 */
#define	PCI_PRODUCT_ATI_SB400_PCI	0x4371		/* SB400 PCI */
#define	PCI_PRODUCT_ATI_SB400_SMB	0x4372		/* SB400 SMBus */
#define	PCI_PRODUCT_ATI_SB400_EHCI	0x4373		/* SB400 USB2 */
#define	PCI_PRODUCT_ATI_SB400_OHCI_1	0x4374		/* SB400 USB */
#define	PCI_PRODUCT_ATI_SB400_OHCI_2	0x4375		/* SB400 USB */
#define	PCI_PRODUCT_ATI_SB400_IDE	0x4376		/* SB400 IDE */
#define	PCI_PRODUCT_ATI_SB400_ISA	0x4377		/* SB400 ISA */
#define	PCI_PRODUCT_ATI_SB400_MODEM	0x4378		/* SB400 Modem */
#define	PCI_PRODUCT_ATI_SB400_SATA_1	0x4379		/* SB400 SATA */
#define	PCI_PRODUCT_ATI_SB400_SATA_2	0x437a		/* SB400 SATA */
#define	PCI_PRODUCT_ATI_SB450_HDA	0x437b		/* SB450 HD Audio */
#define	PCI_PRODUCT_ATI_SB600_SATA	0x4380		/* SB600 SATA */
#define	PCI_PRODUCT_ATI_SB600_AUDIO	0x4382		/* SB600 AC97 */
#define	PCI_PRODUCT_ATI_SBX00_HDA	0x4383		/* SBx00 HD Audio */
#define	PCI_PRODUCT_ATI_SB600_PCI	0x4384		/* SB600 PCI */
#define	PCI_PRODUCT_ATI_SBX00_SMB	0x4385		/* SBx00 SMBus */
#define	PCI_PRODUCT_ATI_SB600_EHCI	0x4386		/* SB600 USB2 */
#define	PCI_PRODUCT_ATI_SB600_OHCI_1	0x4387		/* SB600 USB */
#define	PCI_PRODUCT_ATI_SB600_OHCI_2	0x4388		/* SB600 USB */
#define	PCI_PRODUCT_ATI_SB600_OHCI_3	0x4389		/* SB600 USB */
#define	PCI_PRODUCT_ATI_SB600_OHCI_4	0x438a		/* SB600 USB */
#define	PCI_PRODUCT_ATI_SB600_OHCI_5	0x438b		/* SB600 USB */
#define	PCI_PRODUCT_ATI_SB600_IDE	0x438c		/* SB600 IDE */
#define	PCI_PRODUCT_ATI_SB600_ISA	0x438d		/* SB600 ISA */
#define	PCI_PRODUCT_ATI_SB600_MODEM	0x438e		/* SB600 Modem */
#define	PCI_PRODUCT_ATI_SBX00_SATA_1	0x4390		/* SBx00 SATA */
#define	PCI_PRODUCT_ATI_SBX00_SATA_2	0x4391		/* SBx00 SATA */
#define	PCI_PRODUCT_ATI_SBX00_SATA_3	0x4392		/* SBx00 SATA */
#define	PCI_PRODUCT_ATI_SBX00_SATA_4	0x4393		/* SBx00 SATA */
#define	PCI_PRODUCT_ATI_SBX00_SATA_5	0x4394		/* SBx00 SATA */
#define	PCI_PRODUCT_ATI_SBX00_SATA_6	0x4395		/* SBx00 SATA */
#define	PCI_PRODUCT_ATI_SB700_EHCI	0x4396		/* SB700 USB2 */
#define	PCI_PRODUCT_ATI_SB700_OHCI_1	0x4397		/* SB700 USB */
#define	PCI_PRODUCT_ATI_SB700_OHCI_2	0x4398		/* SB700 USB */
#define	PCI_PRODUCT_ATI_SB700_OHCI_3	0x4399		/* SB700 USB */
#define	PCI_PRODUCT_ATI_SB700_OHCI_4	0x439a		/* SB700 USB */
#define	PCI_PRODUCT_ATI_SB700_OHCI_5	0x439b		/* SB700 USB */
#define	PCI_PRODUCT_ATI_SB700_IDE	0x439c		/* SB700 IDE */
#define	PCI_PRODUCT_ATI_SB700_ISA	0x439d		/* SB700 ISA */
#define	PCI_PRODUCT_ATI_SB800_PCIE_1	0x43a0		/* SB800 PCIE */
#define	PCI_PRODUCT_ATI_SB800_PCIE_2	0x43a1		/* SB800 PCIE */
#define	PCI_PRODUCT_ATI_SB800_PCIE_3	0x43a2		/* SB800 PCIE */
#define	PCI_PRODUCT_ATI_SB800_PCIE_4	0x43a3		/* SB800 PCIE */
#define	PCI_PRODUCT_ATI_RADEON_MIGP_RS250	0x4437		/* Radeon Mobility IGP */
#define	PCI_PRODUCT_ATI_MACH64_ET	0x4554		/* Mach64 ET */
#define	PCI_PRODUCT_ATI_RAGEPRO	0x4742		/* Rage Pro */
#define	PCI_PRODUCT_ATI_MACH64_GD	0x4744		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GI	0x4749		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GL	0x474c		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GM	0x474d		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GN	0x474e		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GO	0x474f		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GP	0x4750		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GQ	0x4751		/* Mach64 */
#define	PCI_PRODUCT_ATI_RAGEXL	0x4752		/* Rage XL */
#define	PCI_PRODUCT_ATI_MACH64_GS	0x4753		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GT	0x4754		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GU	0x4755		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GV	0x4756		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GW	0x4757		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GX	0x4758		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GY	0x4759		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_GZ	0x475a		/* Mach64 */
#define	PCI_PRODUCT_ATI_RV250	0x4966		/* Radeon 9000 */
#define	PCI_PRODUCT_ATI_RADEON_IG9000	0x4967		/* Radeon 9000 */
#define	PCI_PRODUCT_ATI_RV250_S	0x496e		/* Radeon 9000 Sec */
#define	PCI_PRODUCT_ATI_RADEON_JHX800	0x4a48		/* Radeon X800 */
#define	PCI_PRODUCT_ATI_RADEON_X800PRO	0x4a49		/* Radeon X800 Pro */
#define	PCI_PRODUCT_ATI_RADEON_X800SE	0x4a4a		/* Radeon X800SE */
#define	PCI_PRODUCT_ATI_RADEON_X800XT	0x4a4b		/* Radeon X800XT */
#define	PCI_PRODUCT_ATI_RADEON_X800	0x4a4c		/* Radeon X800 */
#define	PCI_PRODUCT_ATI_FIREGL_X3256	0x4a4d		/* FireGL X3-256 */
#define	PCI_PRODUCT_ATI_MOBILITY_M18	0x4a4e		/* Radeon Mobility M18 */
#define	PCI_PRODUCT_ATI_RADEON_JOX800SE	0x4a4f		/* Radeon X800 SE */
#define	PCI_PRODUCT_ATI_RADEON_X800XTPE	0x4a50		/* Radeon X800 XT */
#define	PCI_PRODUCT_ATI_RADEON_AIW_X800VE	0x4a54		/* Radeon AIW X800 VE */
#define	PCI_PRODUCT_ATI_RADEON_X800PRO_S	0x4a69		/* Radeon X800 Pro Sec */
#define	PCI_PRODUCT_ATI_RADEON_X850	0x4b48		/* Radeon X850 */
#define	PCI_PRODUCT_ATI_RADEON_X850XT	0x4b49		/* Radeon X850 XT */
#define	PCI_PRODUCT_ATI_RADEON_X850SE	0x4b4a		/* Radeon X850 SE */
#define	PCI_PRODUCT_ATI_RADEON_X850PRO	0x4b4b		/* Radeon X850 Pro */
#define	PCI_PRODUCT_ATI_RADEON_X850XTPE	0x4b4c		/* Radeon X850 XT PE */
#define	PCI_PRODUCT_ATI_MACH64_LB	0x4c42		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_LD	0x4c44		/* Mach64 */
#define	PCI_PRODUCT_ATI_RAGE128_LE	0x4c45		/* Rage128 */
#define	PCI_PRODUCT_ATI_MOBILITY_M3	0x4c46		/* Mobility M3 */
#define	PCI_PRODUCT_ATI_MACH64_LG	0x4c47		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_LI	0x4c49		/* Mach64 */
#define	PCI_PRODUCT_ATI_MOBILITY_1	0x4c4d		/* Mobility 1 */
#define	PCI_PRODUCT_ATI_MACH64_LN	0x4c4e		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_LP	0x4c50		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_LQ	0x4c51		/* Mach64 */
#define	PCI_PRODUCT_ATI_RAGE_PM	0x4c52		/* Rage P/M */
#define	PCI_PRODUCT_ATI_MACH64LS	0x4c53		/* Mach64 */
#define	PCI_PRODUCT_ATI_RADEON_M7LW	0x4c57		/* Radeon Mobility M7 */
#define	PCI_PRODUCT_ATI_FIREGL_M7	0x4c58		/* FireGL Mobility 7800 M7 */
#define	PCI_PRODUCT_ATI_RADEON_M6LY	0x4c59		/* Radeon Mobility M6 */
#define	PCI_PRODUCT_ATI_RADEON_M6LZ	0x4c5a		/* Radeon Mobility M6 */
#define	PCI_PRODUCT_ATI_RADEON_M9LD	0x4c64		/* Radeon Mobility M9 */
#define	PCI_PRODUCT_ATI_RADEON_M9LF	0x4c66		/* Radeon Mobility M9 */
#define	PCI_PRODUCT_ATI_RADEON_M9LG	0x4c67		/* Radeon Mobility M9 */
#define	PCI_PRODUCT_ATI_FIREMV_2400_PCI	0x4c6e		/* FireMV 2400 PCI */
#define	PCI_PRODUCT_ATI_RAGE128_MF	0x4d46		/* Rage 128 Mobility */
#define	PCI_PRODUCT_ATI_RAGE128_ML	0x4d4c		/* Rage 128 Mobility */
#define	PCI_PRODUCT_ATI_R300	0x4e44		/* Radeon 9500/9700 */
#define	PCI_PRODUCT_ATI_RADEON9500_PRO	0x4e45		/* Radeon 9500 Pro */
#define	PCI_PRODUCT_ATI_RADEON9600TX	0x4e46		/* Radeon 9600 TX */
#define	PCI_PRODUCT_ATI_FIREGL_X1	0x4e47		/* FireGL X1 */
#define	PCI_PRODUCT_ATI_R350	0x4e48		/* Radeon 9800 Pro */
#define	PCI_PRODUCT_ATI_RADEON9800	0x4e49		/* Radeon 9800 */
#define	PCI_PRODUCT_ATI_RADEON_9800XT	0x4e4a		/* Radeon 9800 XT */
#define	PCI_PRODUCT_ATI_FIREGL_X2	0x4e4b		/* FireGL X2 */
#define	PCI_PRODUCT_ATI_RV350	0x4e50		/* Radeon Mobility M10 */
#define	PCI_PRODUCT_ATI_RV350NQ	0x4e51		/* Radeon Mobility M10 */
#define	PCI_PRODUCT_ATI_RV350NR	0x4e52		/* Radeon Mobility M10 */
#define	PCI_PRODUCT_ATI_RV350NS	0x4e53		/* Radeon Mobility M10 */
#define	PCI_PRODUCT_ATI_RV350_WS	0x4e54		/* Radeon Mobility M10 */
#define	PCI_PRODUCT_ATI_MOBILITY_9550	0x4e56		/* Radeon Mobility 9550 */
#define	PCI_PRODUCT_ATI_R300_S	0x4e64		/* Radeon 9500/9700 Sec */
#define	PCI_PRODUCT_ATI_FIREGL_X1_S	0x4e67		/* FireGL X1 Sec */
#define	PCI_PRODUCT_ATI_R350_S	0x4e68		/* Radeon 9800 Pro Sec */
#define	PCI_PRODUCT_ATI_RAGE128_PA	0x5041		/* Rage 128 Pro */
#define	PCI_PRODUCT_ATI_RAGE128_PB	0x5042		/* Rage 128 Pro */
#define	PCI_PRODUCT_ATI_RAGE128_PC	0x5043		/* Rage 128 Pro */
#define	PCI_PRODUCT_ATI_RAGE128_PD	0x5044		/* Rage 128 Pro */
#define	PCI_PRODUCT_ATI_RAGE128_PE	0x5045		/* Rage 128 Pro */
#define	PCI_PRODUCT_ATI_RAGE_FURY	0x5046		/* Rage Fury */
#define	PCI_PRODUCT_ATI_RAGE128_PG	0x5047		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PH	0x5048		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PI	0x5049		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PJ	0x504a		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PK	0x504b		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PL	0x504c		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PM	0x504d		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PN	0x504e		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PO	0x504f		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PP	0x5050		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PQ	0x5051		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PR	0x5052		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PS	0x5053		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PT	0x5054		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PU	0x5055		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PV	0x5056		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PW	0x5057		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_PX	0x5058		/* Rage 128 PX */
#define	PCI_PRODUCT_ATI_RADEON_AIW	0x5144		/* AIW Radeon */
#define	PCI_PRODUCT_ATI_RADEON_QE	0x5145		/* Radeon */
#define	PCI_PRODUCT_ATI_RADEON_QF	0x5146		/* Radeon */
#define	PCI_PRODUCT_ATI_RADEON_QG	0x5147		/* Radeon */
#define	PCI_PRODUCT_ATI_RADEON_QH	0x5148		/* Radeon */
#define	PCI_PRODUCT_ATI_R200_QL	0x514c		/* Radeon 8500 */
#define	PCI_PRODUCT_ATI_R200_QM	0x514d		/* Radeon 9100 */
#define	PCI_PRODUCT_ATI_R200_QN	0x514e		/* Radeon 8500 */
#define	PCI_PRODUCT_ATI_R200_QO	0x514f		/* Radeon 8500 */
#define	PCI_PRODUCT_ATI_RV200_QW	0x5157		/* Radeon 7500 */
#define	PCI_PRODUCT_ATI_RV200_QX	0x5158		/* Radeon 7500 */
#define	PCI_PRODUCT_ATI_RADEON_QY	0x5159		/* Radeon VE */
#define	PCI_PRODUCT_ATI_RADEON_QZ	0x515a		/* Radeon VE */
#define	PCI_PRODUCT_ATI_ES1000	0x515e		/* ES1000 */
#define	PCI_PRODUCT_ATI_R200_QL_2	0x516c		/* Radeon 8500 */
#define	PCI_PRODUCT_ATI_RAGE128_GL	0x5245		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE_MAGNUM	0x5246		/* Rage Magnum */
#define	PCI_PRODUCT_ATI_RAGE128_RG	0x5247		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_RK	0x524b		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_VR	0x524c		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_SH	0x5348		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_SK	0x534b		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_SL	0x534c		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_SM	0x534d		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128	0x534e		/* Rage 128 */
#define	PCI_PRODUCT_ATI_RAGE128_TF	0x5446		/* Rage 128 Pro */
#define	PCI_PRODUCT_ATI_RAGE128_TL	0x544c		/* Rage 128 Pro */
#define	PCI_PRODUCT_ATI_RAGE128_TR	0x5452		/* Rage 128 Pro */
#define	PCI_PRODUCT_ATI_RADEON_M300_M22	0x5460		/* Radeon Mobility M300 M22 */
#define	PCI_PRODUCT_ATI_RADEON_X600_M24C	0x5462		/* Radeon Mobility X600 M24C */
#define	PCI_PRODUCT_ATI_FIREGL_M44	0x5464		/* FireGL M44 GL 5464 */
#define	PCI_PRODUCT_ATI_RADEON_X800_RV423	0x5548		/* Radeon X800 */
#define	PCI_PRODUCT_ATI_RADEON_X800PRORV423	0x5549		/* Radeon X800 Pro */
#define	PCI_PRODUCT_ATI_RADEON_X800XT_RV423	0x554a		/* Radeon X800 XT PE */
#define	PCI_PRODUCT_ATI_RADEON_X800SE_RV423	0x554b		/* Radeon X800 SE */
#define	PCI_PRODUCT_ATI_RADEON_X800XTPRV430	0x554c		/* Radeon X800 XTP */
#define	PCI_PRODUCT_ATI_RADEON_X800XL_RV430	0x554d		/* Radeon X800 XL */
#define	PCI_PRODUCT_ATI_RADEON_X800SE_RV430	0x554e		/* Radeon X800 SE */
#define	PCI_PRODUCT_ATI_RADEON_X800_RV430	0x554f		/* Radeon X800 */
#define	PCI_PRODUCT_ATI_FIREGL_V7100_RV423	0x5550		/* FireGL V7100 */
#define	PCI_PRODUCT_ATI_FIREGL_V5100_RV423	0x5551		/* FireGL V5100 */
#define	PCI_PRODUCT_ATI_FIREGL_UR_RV423	0x5552		/* FireGL */
#define	PCI_PRODUCT_ATI_FIREGL_UT_RV423	0x5553		/* FireGL */
#define	PCI_PRODUCT_ATI_FIREGL_UT_R423	0x5554		/* FireGL */
#define	PCI_PRODUCT_ATI_RADEON_X800_RV430_S	0x556d		/* Radeon X800 Sec */
#define	PCI_PRODUCT_ATI_FIREGL_V5000_M26	0x564a		/* Mobility FireGL V5000 M26 */
#define	PCI_PRODUCT_ATI_FIREGL_V5000_M26B	0x564b		/* Mobility FireGL V5000 M26 */
#define	PCI_PRODUCT_ATI_RADEON_X700XL_M26	0x564f		/* Radeon Mobility X700 XL M26 */
#define	PCI_PRODUCT_ATI_RADEON_X700_M26_1	0x5652		/* Radeon Mobility X700 M26 */
#define	PCI_PRODUCT_ATI_RADEON_X700_M26_2	0x5653		/* Radeon Mobility X700 M26 */
#define	PCI_PRODUCT_ATI_MACH64_VT	0x5654		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_VU	0x5655		/* Mach64 */
#define	PCI_PRODUCT_ATI_MACH64_VV	0x5656		/* Mach64 */
#define	PCI_PRODUCT_ATI_RADEON_X550XTX	0x5657		/* Radeon X550XTX */
#define	PCI_PRODUCT_ATI_RADEON_X550XTX_S	0x5677		/* Radeon X550XTX Sec */
#define	PCI_PRODUCT_ATI_RS300_100_HB	0x5830		/* RS300_100 Host */
#define	PCI_PRODUCT_ATI_RS300_133_HB	0x5831		/* RS300_133 Host */
#define	PCI_PRODUCT_ATI_RS300_166_HB	0x5832		/* RS300_166 Host */
#define	PCI_PRODUCT_ATI_RADEON_IGP9100_HB	0x5833		/* Radeon IGP 9100 Host */
#define	PCI_PRODUCT_ATI_RADEON_IGP9100_IGP	0x5834		/* Radeon IGP 9100 */
#define	PCI_PRODUCT_ATI_RADEON_IGP9100	0x5835		/* Radeon Mobility IGP 9100 */
#define	PCI_PRODUCT_ATI_RADEON_IGP9100_AGP	0x5838		/* Radeon IGP 9100 AGP */
#define	PCI_PRODUCT_ATI_RADEON_RV280_PRO_S	0x5940		/* Radeon 9200 PRO Sec */
#define	PCI_PRODUCT_ATI_RADEON_RV280_S	0x5941		/* Radeon 9200 Sec */
#define	PCI_PRODUCT_ATI_RS480_HB	0x5950		/* RS480 Host */
#define	PCI_PRODUCT_ATI_RX480_HB	0x5951		/* RX480 Host */
#define	PCI_PRODUCT_ATI_RD580_HB	0x5952		/* RD580 Host */
#define	PCI_PRODUCT_ATI_RADEON_RS480	0x5954		/* Radeon XPRESS 200 */
#define	PCI_PRODUCT_ATI_RADEON_RS480_B	0x5955		/* Radeon XPRESS 200M */
#define	PCI_PRODUCT_ATI_RX780_HB	0x5957		/* RX780 Host */
#define	PCI_PRODUCT_ATI_RD780_HT_GFX	0x5958		/* RD780 HT-PCIE */
#define	PCI_PRODUCT_ATI_RADEON_RV280_PRO	0x5960		/* Radeon 9200 PRO */
#define	PCI_PRODUCT_ATI_RADEON_RV280	0x5961		/* Radeon 9200 */
#define	PCI_PRODUCT_ATI_RADEON_RV280_B	0x5962		/* Radeon 9200 */
#define	PCI_PRODUCT_ATI_RADEON_RV280_SE_S	0x5964		/* Radeon 9200 SE Sec */
#define	PCI_PRODUCT_ATI_FIREMV_2200	0x5965		/* FireMV 2200 */
#define	PCI_PRODUCT_ATI_ES1000_1	0x5969		/* ES1000 */
#define	PCI_PRODUCT_ATI_RADEON_RS482	0x5974		/* Radeon XPRESS 200 */
#define	PCI_PRODUCT_ATI_RADEON_RS482_B	0x5975		/* Radeon XPRESS 200M */
#define	PCI_PRODUCT_ATI_RD790_PCIE_1	0x5978		/* RD790 PCIE */
#define	PCI_PRODUCT_ATI_RD790_PCIE_3	0x597a		/* RD790 PCIE */
#define	PCI_PRODUCT_ATI_RD790_PCIE_6	0x597b		/* RD790 PCIE */
#define	PCI_PRODUCT_ATI_RD790_PCIE_2	0x597c		/* RD790 PCIE */
#define	PCI_PRODUCT_ATI_RD790_PCIE_7	0x597d		/* RD790 PCIE */
#define	PCI_PRODUCT_ATI_RD790_PCIE_5	0x597e		/* RD790 PCIE */
#define	PCI_PRODUCT_ATI_RD790_PCIE_4	0x597f		/* RD790 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_HB	0x5a10		/* SR5690 Host */
#define	PCI_PRODUCT_ATI_RD890_HB	0x5a11		/* RD890 Host */
#define	PCI_PRODUCT_ATI_SR5670_HB	0x5a12		/* SR5670 Host */
#define	PCI_PRODUCT_ATI_SR5650_HB	0x5a13		/* SR5650 Host */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_0	0x5a16		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_1	0x5a17		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_2	0x5a18		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_3	0x5a19		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_4	0x5a1a		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_5	0x5a1b		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_6	0x5a1c		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_7	0x5a1d		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_8	0x5a1e		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_9	0x5a1f		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_PCIE_A	0x5a20		/* SR5690 PCIE */
#define	PCI_PRODUCT_ATI_SR5690_IOMMU	0x5a23		/* SR5690 IOMMU */
#define	PCI_PRODUCT_ATI_RS400_HB	0x5a31		/* RS400 Host */
#define	PCI_PRODUCT_ATI_RC410_HB	0x5a33		/* RC410 Host */
#define	PCI_PRODUCT_ATI_RX480_PCIE	0x5a34		/* RX480 PCIE */
#define	PCI_PRODUCT_ATI_RS480_PCIE_2	0x5a36		/* RS480 PCIE */
#define	PCI_PRODUCT_ATI_RS480_PCIE_3	0x5a37		/* RS480 PCIE */
#define	PCI_PRODUCT_ATI_RX480_PCIE_2	0x5a38		/* RX480 PCIE */
#define	PCI_PRODUCT_ATI_RX480_PCIE_3	0x5a39		/* RX480 PCIE */
#define	PCI_PRODUCT_ATI_RS480_PCIE_1	0x5a3f		/* RS480 PCIE */
#define	PCI_PRODUCT_ATI_RADEON_RS400	0x5a41		/* Radeon XPRESS 200 */
#define	PCI_PRODUCT_ATI_RADEON_RS400_B	0x5a42		/* Radeon XPRESS 200M */
#define	PCI_PRODUCT_ATI_RADEON_RC410	0x5a61		/* Radeon XPRESS 200 */
#define	PCI_PRODUCT_ATI_RADEON_RC410_B	0x5a62		/* Radeon XPRESS 200M */
#define	PCI_PRODUCT_ATI_RADEON_X300	0x5b60		/* Radeon X300 */
#define	PCI_PRODUCT_ATI_RADEON_X600_RV370	0x5b62		/* Radeon X600 */
#define	PCI_PRODUCT_ATI_RADEON_X550	0x5b63		/* Radeon X550 */
#define	PCI_PRODUCT_ATI_FIREGL_RV370	0x5b64		/* FireGL V3100 */
#define	PCI_PRODUCT_ATI_FIREMV_2200_5B65	0x5b65		/* FireMV 2200 5B65 */
#define	PCI_PRODUCT_ATI_RADEON_X300_S	0x5b70		/* Radeon X300 Sec */
#define	PCI_PRODUCT_ATI_RADEON_X600_RV370_S	0x5b72		/* Radeon X600 Sec */
#define	PCI_PRODUCT_ATI_RADEON_X550_S	0x5b73		/* Radeon X550 Sec */
#define	PCI_PRODUCT_ATI_FIREGL_RV370_S	0x5b74		/* FireGL V3100 Sec */
#define	PCI_PRODUCT_ATI_FIREMV_2200_S	0x5b75		/* FireMV 2200 Sec */
#define	PCI_PRODUCT_ATI_RADEON_RV280_M	0x5c61		/* Radeon Mobility 9200 */
#define	PCI_PRODUCT_ATI_RADEON_M9PLUS	0x5c63		/* Radeon Mobility 9200 */
#define	PCI_PRODUCT_ATI_RADEON_RV280_SE	0x5d44		/* Radeon 9200 SE */
#define	PCI_PRODUCT_ATI_RADEON_X800XT_M28	0x5d48		/* Radeon X800 XT M28 */
#define	PCI_PRODUCT_ATI_FIREGL_V5100_M28	0x5d49		/* FireGL V5100 M28 */
#define	PCI_PRODUCT_ATI_MOBILITY_X800_M28	0x5d4a		/* Radeon Mobility X800 M28 */
#define	PCI_PRODUCT_ATI_RADEON_X850_R480	0x5d4c		/* Radeon X850 */
#define	PCI_PRODUCT_ATI_RADEON_X850XTPER480	0x5d4d		/* Radeon X850 XT PE */
#define	PCI_PRODUCT_ATI_RADEON_X850SE_R480	0x5d4e		/* Radeon X850 SE */
#define	PCI_PRODUCT_ATI_RADEON_X800_GTO	0x5d4f		/* Radeon X800 */
#define	PCI_PRODUCT_ATI_FIREGL_R480	0x5d50		/* FireGL R480 */
#define	PCI_PRODUCT_ATI_RADEON_X850XT_R480	0x5d52		/* Radeon X850XT */
#define	PCI_PRODUCT_ATI_RADEON_X800XT_R423	0x5d57		/* Radeon X800XT */
#define	PCI_PRODUCT_ATI_RADEON_X800_GTO_S	0x5d6f		/* Radeon X800 GTO Sec */
#define	PCI_PRODUCT_ATI_RADEON_X850XT_S	0x5d72		/* Radeon X850 XT Sec */
#define	PCI_PRODUCT_ATI_FIREGL_V5000_R410	0x5e48		/* FireGL V5000 */
#define	PCI_PRODUCT_ATI_RADEON_X700XT_R410	0x5e4a		/* FireGL X700 XT */
#define	PCI_PRODUCT_ATI_RADEON_X700PRO_R410	0x5e4b		/* FireGL X700 Pro */
#define	PCI_PRODUCT_ATI_RADEON_X700SE_R410	0x5e4c		/* FireGL X700 SE */
#define	PCI_PRODUCT_ATI_RADEON_X700_PCIE	0x5e4d		/* Radeon X700 PCIE */
#define	PCI_PRODUCT_ATI_RADEON_X700SE_PCIE	0x5e4f		/* Radeon X700 SE PCIE */
#define	PCI_PRODUCT_ATI_RADEON_X700_SE	0x5e4f		/* Radeon X700 SE */
#define	PCI_PRODUCT_ATI_RADEON_X700_PCIE_S	0x5e6d		/* Radeon X700 PCIE Sec */
#define	PCI_PRODUCT_ATI_RADEON_X700_SE_S	0x5e6f		/* Radeon X700 SE Sec */
#define	PCI_PRODUCT_ATI_RADEON_HD8670A	0x6600		/* Radeon HD 8670A */
#define	PCI_PRODUCT_ATI_RADEON_HD8730M	0x6601		/* Radeon HD 8730M */
#define	PCI_PRODUCT_ATI_OLAND_1	0x6602		/* Oland */
#define	PCI_PRODUCT_ATI_OLAND_2	0x6603		/* Oland */
#define	PCI_PRODUCT_ATI_OLAND_3	0x6604		/* Oland */
#define	PCI_PRODUCT_ATI_OLAND_4	0x6605		/* Oland */
#define	PCI_PRODUCT_ATI_RADEON_HD8790M	0x6606		/* Radeon HD 8790M */
#define	PCI_PRODUCT_ATI_RADEON_HD8530M	0x6607		/* Radeon HD 8530M */
#define	PCI_PRODUCT_ATI_OLAND_5	0x6608		/* Oland */
#define	PCI_PRODUCT_ATI_RADEON_HD8600	0x6610		/* Radeon HD 8600 */
#define	PCI_PRODUCT_ATI_RADEON_HD8570	0x6611		/* Radeon HD 8570 */
#define	PCI_PRODUCT_ATI_RADEON_HD8500	0x6613		/* Radeon HD 8500 */
#define	PCI_PRODUCT_ATI_OLAND_6	0x6617		/* Oland */
#define	PCI_PRODUCT_ATI_OLAND_7	0x6620		/* Oland */
#define	PCI_PRODUCT_ATI_OLAND_8	0x6621		/* Oland */
#define	PCI_PRODUCT_ATI_OLAND_9	0x6623		/* Oland */
#define	PCI_PRODUCT_ATI_OLAND_10	0x6631		/* Oland */
#define	PCI_PRODUCT_ATI_BONAIRE_1	0x6640		/* Bonaire */
#define	PCI_PRODUCT_ATI_BONAIRE_2	0x6641		/* Bonaire */
#define	PCI_PRODUCT_ATI_BONAIRE_3	0x6646		/* Bonaire */
#define	PCI_PRODUCT_ATI_BONAIRE_4	0x6647		/* Bonaire */
#define	PCI_PRODUCT_ATI_BONAIRE_5	0x6649		/* Bonaire */
#define	PCI_PRODUCT_ATI_BONAIRE_6	0x6650		/* Bonaire */
#define	PCI_PRODUCT_ATI_BONAIRE_7	0x6651		/* Bonaire */
#define	PCI_PRODUCT_ATI_BONAIRE_8	0x6658		/* Bonaire */
#define	PCI_PRODUCT_ATI_BONAIRE_9	0x665c		/* Bonaire */
#define	PCI_PRODUCT_ATI_BONAIRE_10	0x665d		/* Bonaire */
#define	PCI_PRODUCT_ATI_BONAIRE_11	0x665f		/* Bonaire */
#define	PCI_PRODUCT_ATI_RADEON_HD8670M	0x6660		/* Radeon HD 8670M */
#define	PCI_PRODUCT_ATI_RADEON_HD8500M_1	0x6663		/* Radeon HD 8500M */
#define	PCI_PRODUCT_ATI_HAINAN_1	0x6664		/* Hainan */
#define	PCI_PRODUCT_ATI_HAINAN_2	0x6665		/* Hainan */
#define	PCI_PRODUCT_ATI_HAINAN_3	0x6667		/* Hainan */
#define	PCI_PRODUCT_ATI_RADEON_HD8500M_2	0x666f		/* Radeon HD 8500M */
#define	PCI_PRODUCT_ATI_VEGA20_1	0x66a0		/* Vega 20 */
#define	PCI_PRODUCT_ATI_VEGA20_2	0x66a1		/* Vega 20 */
#define	PCI_PRODUCT_ATI_VEGA20_3	0x66a2		/* Vega 20 */
#define	PCI_PRODUCT_ATI_VEGA20_4	0x66a3		/* Vega 20 */
#define	PCI_PRODUCT_ATI_VEGA20_5	0x66a4		/* Vega 20 */
#define	PCI_PRODUCT_ATI_VEGA20_6	0x66a7		/* Vega 20 */
#define	PCI_PRODUCT_ATI_VEGA20_7	0x66af		/* Vega 20 */
#define	PCI_PRODUCT_ATI_CAYMAN_1	0x6700		/* Cayman */
#define	PCI_PRODUCT_ATI_CAYMAN_2	0x6701		/* Cayman */
#define	PCI_PRODUCT_ATI_CAYMAN_3	0x6702		/* Cayman */
#define	PCI_PRODUCT_ATI_CAYMAN_4	0x6703		/* Cayman */
#define	PCI_PRODUCT_ATI_FIREPRO_V7900	0x6704		/* FirePro V7900 */
#define	PCI_PRODUCT_ATI_CAYMAN_5	0x6705		/* Cayman */
#define	PCI_PRODUCT_ATI_CAYMAN_6	0x6706		/* Cayman */
#define	PCI_PRODUCT_ATI_FIREPRO_V5900	0x6707		/* FirePro V5900 */
#define	PCI_PRODUCT_ATI_CAYMAN_7	0x6708		/* Cayman */
#define	PCI_PRODUCT_ATI_CAYMAN_8	0x6709		/* Cayman */
#define	PCI_PRODUCT_ATI_RADEON_HD6970	0x6718		/* Radeon HD 6970 */
#define	PCI_PRODUCT_ATI_RADEON_HD6950	0x6719		/* Radeon HD 6950 */
#define	PCI_PRODUCT_ATI_RADEON_HD6990_1	0x671c		/* Radeon HD 6990 */
#define	PCI_PRODUCT_ATI_RADEON_HD6990_2	0x671d		/* Radeon HD 6990 */
#define	PCI_PRODUCT_ATI_RADEON_HD6930	0x671f		/* Radeon HD 6930 */
#define	PCI_PRODUCT_ATI_RADEON_HD6970M	0x6720		/* Radeon HD 6970M */
#define	PCI_PRODUCT_ATI_MOBILITY_HD6000_1	0x6721		/* Mobility Radeon HD 6000 */
#define	PCI_PRODUCT_ATI_BARTS_1	0x6722		/* Barts */
#define	PCI_PRODUCT_ATI_BARTS_2	0x6723		/* Barts */
#define	PCI_PRODUCT_ATI_MOBILITY_HD6000_2	0x6724		/* Mobility Radeon HD 6000 */
#define	PCI_PRODUCT_ATI_RADEON_HD6900M	0x6725		/* Radeon HD 6900M */
#define	PCI_PRODUCT_ATI_BARTS_3	0x6726		/* Barts */
#define	PCI_PRODUCT_ATI_BARTS_4	0x6727		/* Barts */
#define	PCI_PRODUCT_ATI_BARTS_5	0x6728		/* Barts */
#define	PCI_PRODUCT_ATI_BARTS_6	0x6729		/* Barts */
#define	PCI_PRODUCT_ATI_RADEON_HD6870	0x6738		/* Radeon HD 6870 */
#define	PCI_PRODUCT_ATI_RADEON_HD6850	0x6739		/* Radeon HD 6850 */
#define	PCI_PRODUCT_ATI_RADEON_HD6790	0x673e		/* Radeon HD 6790 */
#define	PCI_PRODUCT_ATI_RADEON_HD6730M	0x6740		/* Radeon HD 6730M */
#define	PCI_PRODUCT_ATI_RADEON_HD6600M	0x6741		/* Radeon HD 6600M */
#define	PCI_PRODUCT_ATI_RADEON_HD6610M	0x6742		/* Radeon HD 6610M */
#define	PCI_PRODUCT_ATI_RADEON_E6760	0x6743		/* Radeon E6760 */
#define	PCI_PRODUCT_ATI_TURKS_1	0x6744		/* Turks */
#define	PCI_PRODUCT_ATI_TURKS_2	0x6745		/* Turks */
#define	PCI_PRODUCT_ATI_TURKS_3	0x6746		/* Turks */
#define	PCI_PRODUCT_ATI_TURKS_4	0x6747		/* Turks */
#define	PCI_PRODUCT_ATI_TURKS_5	0x6748		/* Turks */
#define	PCI_PRODUCT_ATI_FIREPRO_V4900	0x6749		/* FirePro V4900 */
#define	PCI_PRODUCT_ATI_FIREPRO_V3900	0x674a		/* FirePro V3900 */
#define	PCI_PRODUCT_ATI_RADEON_HD6650A	0x6750		/* Radeon HD 6650A */
#define	PCI_PRODUCT_ATI_RADEON_HD7670A	0x6751		/* Radeon HD 7670A */
#define	PCI_PRODUCT_ATI_RADEON_HD6670	0x6758		/* Radeon HD 6670 */
#define	PCI_PRODUCT_ATI_RADEON_HD6570	0x6759		/* Radeon HD 6570 */
#define	PCI_PRODUCT_ATI_TURKS_6	0x675b		/* Turks */
#define	PCI_PRODUCT_ATI_RADEON_HD7570	0x675d		/* Radeon HD 7570 */
#define	PCI_PRODUCT_ATI_RADEON_HD6510	0x675f		/* Radeon HD 6510 */
#define	PCI_PRODUCT_ATI_RADEON_HD6400M	0x6760		/* Radeon HD 6400M */
#define	PCI_PRODUCT_ATI_RADEON_HD6430M	0x6761		/* Radeon HD 6430M */
#define	PCI_PRODUCT_ATI_CAICOS_1	0x6762		/* Caicos */
#define	PCI_PRODUCT_ATI_RADEON_E6460	0x6763		/* Radeon E6460 */
#define	PCI_PRODUCT_ATI_RADEON_HD6400M_1	0x6764		/* Radeon HD 6400M */
#define	PCI_PRODUCT_ATI_RADEON_HD6400M_2	0x6765		/* Radeon HD 6400M */
#define	PCI_PRODUCT_ATI_CAICOS_2	0x6766		/* Caicos */
#define	PCI_PRODUCT_ATI_CAICOS_3	0x6767		/* Caicos */
#define	PCI_PRODUCT_ATI_CAICOS_4	0x6768		/* Caicos */
#define	PCI_PRODUCT_ATI_RADEON_HD6450A	0x6770		/* Radeon HD 6450A */
#define	PCI_PRODUCT_ATI_RADEON_HD8490	0x6771		/* Radeon HD 8490 */
#define	PCI_PRODUCT_ATI_RADEON_HD7450A	0x6772		/* Radeon HD 7450A */
#define	PCI_PRODUCT_ATI_RADEON_HD7470	0x6778		/* Radeon HD 7470 */
#define	PCI_PRODUCT_ATI_RADEON_HD6450	0x6779		/* Radeon HD 6450 */
#define	PCI_PRODUCT_ATI_RADEON_HD7450	0x677b		/* Radeon HD 7450 */
#define	PCI_PRODUCT_ATI_FIREPRO_W9000	0x6780		/* FirePro W9000 */
#define	PCI_PRODUCT_ATI_FIREPRO_V_1	0x6784		/* FirePro V */
#define	PCI_PRODUCT_ATI_FIREPRO_V_2	0x6788		/* FirePro V */
#define	PCI_PRODUCT_ATI_TAHITI_1	0x678a		/* Tahiti */
#define	PCI_PRODUCT_ATI_TAHITI_2	0x6790		/* Tahiti */
#define	PCI_PRODUCT_ATI_TAHITI_3	0x6791		/* Tahiti */
#define	PCI_PRODUCT_ATI_TAHITI_4	0x6792		/* Tahiti */
#define	PCI_PRODUCT_ATI_RADEON_HD7970	0x6798		/* Radeon HD 7970 */
#define	PCI_PRODUCT_ATI_RADEON_HD7900	0x6799		/* Radeon HD 7900 */
#define	PCI_PRODUCT_ATI_RADEON_HD7950	0x679a		/* Radeon HD 7950 */
#define	PCI_PRODUCT_ATI_RADEON_HD7990	0x679b		/* Radeon HD 7990 */
#define	PCI_PRODUCT_ATI_RADEON_HD7870XT	0x679e		/* Radeon HD 7870 XT */
#define	PCI_PRODUCT_ATI_TAHITI_5	0x679f		/* Tahiti */
#define	PCI_PRODUCT_ATI_HAWAII_1	0x67a0		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_2	0x67a1		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_3	0x67a2		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_4	0x67a8		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_5	0x67a9		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_6	0x67aa		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_7	0x67b0		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_8	0x67b1		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_9	0x67b8		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_10	0x67b9		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_11	0x67ba		/* Hawaii */
#define	PCI_PRODUCT_ATI_HAWAII_12	0x67be		/* Hawaii */
#define	PCI_PRODUCT_ATI_POLARIS10_1	0x67c0		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_2	0x67c1		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_3	0x67c2		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_4	0x67c4		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_5	0x67c7		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_6	0x67c8		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_7	0x67c9		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_8	0x67ca		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_9	0x67cc		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_10	0x67cf		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_11	0x67d0		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS10_12	0x67df		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_POLARIS11_1	0x67e0		/* Polaris 11 */
#define	PCI_PRODUCT_ATI_POLARIS11_2	0x67e1		/* Polaris 11 */
#define	PCI_PRODUCT_ATI_POLARIS11_3	0x67e3		/* Polaris 11 */
#define	PCI_PRODUCT_ATI_POLARIS11_4	0x67e7		/* Polaris 11 */
#define	PCI_PRODUCT_ATI_POLARIS11_5	0x67e8		/* Radeon Pro WX 4130/4150 */
#define	PCI_PRODUCT_ATI_POLARIS11_6	0x67e9		/* Polaris 11 */
#define	PCI_PRODUCT_ATI_POLARIS11_7	0x67eb		/* Polaris 11 */
#define	PCI_PRODUCT_ATI_POLARIS11_8	0x67ef		/* Polaris 11 */
#define	PCI_PRODUCT_ATI_POLARIS11_9	0x67ff		/* Polaris 11 */
#define	PCI_PRODUCT_ATI_RADEON_HD7970M	0x6800		/* Radeon HD 7970M */
#define	PCI_PRODUCT_ATI_RADEON_HD8970M	0x6801		/* Radeon HD 8970M */
#define	PCI_PRODUCT_ATI_PITCAIRN_1	0x6802		/* Pitcairn */
#define	PCI_PRODUCT_ATI_PITCAIRN_2	0x6806		/* Pitcairn */
#define	PCI_PRODUCT_ATI_FIREPRO_W7000	0x6808		/* FirePro W7000 */
#define	PCI_PRODUCT_ATI_FIREPRO_W5000	0x6809		/* FirePro W5000 */
#define	PCI_PRODUCT_ATI_PITCAIRN_3	0x6810		/* Pitcairn */
#define	PCI_PRODUCT_ATI_PITCAIRN_4	0x6811		/* Pitcairn */
#define	PCI_PRODUCT_ATI_PITCAIRN_5	0x6816		/* Pitcairn */
#define	PCI_PRODUCT_ATI_PITCAIRN_6	0x6817		/* Pitcairn */
#define	PCI_PRODUCT_ATI_RADEON_HD7870	0x6818		/* Radeon HD 7870 */
#define	PCI_PRODUCT_ATI_RADEON_HD7850	0x6819		/* Radeon HD 7850 */
#define	PCI_PRODUCT_ATI_RADEON_HD8800M_1	0x6820		/* Radeon HD 8800M */
#define	PCI_PRODUCT_ATI_RADEON_HD8800M_2	0x6821		/* Radeon HD 8800M */
#define	PCI_PRODUCT_ATI_VERDE_1	0x6822		/* Cape Verde */
#define	PCI_PRODUCT_ATI_RADEON_HD8800M_3	0x6823		/* Radeon HD 8800M */
#define	PCI_PRODUCT_ATI_RADEON_HD7700M_1	0x6824		/* Radeon HD 7700M */
#define	PCI_PRODUCT_ATI_RADEON_HD7870M	0x6825		/* Radeon HD 7870M */
#define	PCI_PRODUCT_ATI_RADEON_HD7700M_2	0x6826		/* Radeon HD 7700M */
#define	PCI_PRODUCT_ATI_RADEON_HD7850M	0x6827		/* Radeon HD 7850M */
#define	PCI_PRODUCT_ATI_FIREPRO_W600	0x6828		/* FirePro W600 */
#define	PCI_PRODUCT_ATI_VERDE_2	0x6829		/* Cape Verde */
#define	PCI_PRODUCT_ATI_VERDE_3	0x682a		/* Cape Verde */
#define	PCI_PRODUCT_ATI_RADEON_HD8800M	0x682b		/* Radeon HD 8800M */
#define	PCI_PRODUCT_ATI_VERDE_4	0x682c		/* Cape Verde */
#define	PCI_PRODUCT_ATI_FIREPRO_M4000	0x682d		/* FirePro M4000 */
#define	PCI_PRODUCT_ATI_RADEON_HD7730M	0x682f		/* Radeon HD 7730M */
#define	PCI_PRODUCT_ATI_RADEON_HD7800M	0x6830		/* Radeon HD 7800M */
#define	PCI_PRODUCT_ATI_RADEON_HD7700M	0x6831		/* Radeon HD 7700M */
#define	PCI_PRODUCT_ATI_VERDE_5	0x6835		/* Cape Verde */
#define	PCI_PRODUCT_ATI_RADEON_HD7730	0x6837		/* Radeon HD 7730 */
#define	PCI_PRODUCT_ATI_VERDE_6	0x6838		/* Cape Verde */
#define	PCI_PRODUCT_ATI_VERDE_7	0x6839		/* Cape Verde */
#define	PCI_PRODUCT_ATI_RADEON_HD7700	0x683b		/* Radeon HD 7700 */
#define	PCI_PRODUCT_ATI_RADEON_HD7770	0x683d		/* Radeon HD 7770 */
#define	PCI_PRODUCT_ATI_RADEON_HD7750	0x683f		/* Radeon HD 7750 */
#define	PCI_PRODUCT_ATI_RADEON_HD7670M_1	0x6840		/* Radeon HD 7670M */
#define	PCI_PRODUCT_ATI_RADEON_HD7550M	0x6841		/* Radeon HD 7550M */
#define	PCI_PRODUCT_ATI_RADEON_HD7000M	0x6842		/* Radeon HD 7000M */
#define	PCI_PRODUCT_ATI_RADEON_HD7670M_2	0x6843		/* Radeon HD 7670M */
#define	PCI_PRODUCT_ATI_RADEON_HD7400	0x6849		/* Radeon HD 7400 */
#define	PCI_PRODUCT_ATI_PITCAIRN_7	0x684c		/* Pitcairn */
#define	PCI_PRODUCT_ATI_TURKS_7	0x6850		/* Turks */
#define	PCI_PRODUCT_ATI_TURKS_8	0x6858		/* Turks */
#define	PCI_PRODUCT_ATI_TURKS_9	0x6859		/* Turks */
#define	PCI_PRODUCT_ATI_VEGA10_1	0x6860		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_2	0x6861		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_3	0x6862		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_4	0x6863		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_5	0x6864		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_6	0x6867		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_7	0x6868		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_8	0x6869		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_9	0x686a		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_10	0x686b		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_11	0x686c		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_12	0x686d		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_13	0x686e		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_14	0x686f		/* Vega 10 */
#define	PCI_PRODUCT_ATI_VEGA10_15	0x687f		/* Radeon Rx Vega */
#define	PCI_PRODUCT_ATI_CYPRESS	0x6880		/* Cypress */
#define	PCI_PRODUCT_ATI_FIREPRO_V8800	0x6888		/* FirePro V8800 */
#define	PCI_PRODUCT_ATI_FIREPRO_V7800	0x6889		/* FirePro V7800 */
#define	PCI_PRODUCT_ATI_FIREPRO_V9800	0x688a		/* FirePro V9800 */
#define	PCI_PRODUCT_ATI_FIRESTREAM_9370	0x688c		/* FireStream 9370 */
#define	PCI_PRODUCT_ATI_FIRESTREAM_9350	0x688d		/* FireStream 9350 */
#define	PCI_PRODUCT_ATI_RADEON_HD5870	0x6898		/* Radeon HD 5870 */
#define	PCI_PRODUCT_ATI_RADEON_HD5850	0x6899		/* Radeon HD 5850 */
#define	PCI_PRODUCT_ATI_RADEON_HD6800	0x689b		/* Radeon HD 6800 */
#define	PCI_PRODUCT_ATI_RADEON_HD5970	0x689c		/* Radeon HD 5970 */
#define	PCI_PRODUCT_ATI_RADEON_HD5900	0x689d		/* Radeon HD 5900 */
#define	PCI_PRODUCT_ATI_RADEON_HD5830	0x689e		/* Radeon HD 5830 */
#define	PCI_PRODUCT_ATI_MOBILITY_HD5870	0x68a0		/* Mobility Radeon HD 5870 */
#define	PCI_PRODUCT_ATI_MOBILITY_HD5850	0x68a1		/* Mobility Radeon HD 5850 */
#define	PCI_PRODUCT_ATI_RADEON_HD6850M	0x68a8		/* Radeon HD 6850M */
#define	PCI_PRODUCT_ATI_FIREPRO_V5800	0x68a9		/* FirePro V5800 */
#define	PCI_PRODUCT_ATI_MOBILITY_HD5800	0x68b0		/* Mobility Radeon HD 5800 */
#define	PCI_PRODUCT_ATI_RADEON_HD5770	0x68b8		/* Radeon HD 5770 */
#define	PCI_PRODUCT_ATI_RADEON_HD5670_640SP	0x68b9		/* Radeon HD 5670 640SP */
#define	PCI_PRODUCT_ATI_RADEON_HD6770	0x68ba		/* Radeon HD 6770 */
#define	PCI_PRODUCT_ATI_RADEON_HD5750	0x68be		/* Radeon HD 5750 */
#define	PCI_PRODUCT_ATI_RADEON_HD6750	0x68bf		/* Radeon HD 6750 */
#define	PCI_PRODUCT_ATI_MOBILITY_HD5730	0x68c0		/* Mobility Radeon HD 5730 */
#define	PCI_PRODUCT_ATI_MOBILITY_HD5650	0x68c1		/* Mobility Radeon HD 5650 */
#define	PCI_PRODUCT_ATI_MOBILITY_HD5570	0x68c7		/* Mobility Radeon HD 5570 */
#define	PCI_PRODUCT_ATI_FIREPRO_V4800	0x68c8		/* FirePro V4800 */
#define	PCI_PRODUCT_ATI_FIREPRO_V3800	0x68c9		/* FirePro V3800 */
#define	PCI_PRODUCT_ATI_RADEON_HD5670	0x68d8		/* Radeon HD 5670 */
#define	PCI_PRODUCT_ATI_RADEON_HD5570	0x68d9		/* Radeon HD 5570 */
#define	PCI_PRODUCT_ATI_RADEON_HD5550	0x68da		/* Radeon HD 5550 */
#define	PCI_PRODUCT_ATI_REDWOOD	0x68de		/* Redwood */
#define	PCI_PRODUCT_ATI_MOBILITY_HD5470	0x68e0		/* Radeon Mobility HD 5470 */
#define	PCI_PRODUCT_ATI_MOBILITY_HD5430	0x68e1		/* Radeon Mobility HD 5430 */
#define	PCI_PRODUCT_ATI_RADEON_HD6370M	0x68e4		/* Radeon HD 6370M */
#define	PCI_PRODUCT_ATI_RADEON_HD6330M	0x68e5		/* Radeon HD 6330M */
#define	PCI_PRODUCT_ATI_CEDAR	0x68e8		/* Cedar */
#define	PCI_PRODUCT_ATI_FIREPRO_CEDAR	0x68e9		/* FirePro */
#define	PCI_PRODUCT_ATI_FIREPRO_2460	0x68f1		/* FirePro 2460 */
#define	PCI_PRODUCT_ATI_FIREPRO_2270	0x68f2		/* FirePro 2270 */
#define	PCI_PRODUCT_ATI_RADEON_HD7300	0x68f8		/* Radeon HD 7300 */
#define	PCI_PRODUCT_ATI_RADEON_HD5450	0x68f9		/* Radeon HD 5450 */
#define	PCI_PRODUCT_ATI_RADEON_HD7350	0x68fa		/* Radeon HD 7350 */
#define	PCI_PRODUCT_ATI_CEDAR_LE	0x68fe		/* Cedar LE */
#define	PCI_PRODUCT_ATI_TOPAZ_1	0x6900		/* Topaz */
#define	PCI_PRODUCT_ATI_TOPAZ_2	0x6901		/* Topaz */
#define	PCI_PRODUCT_ATI_TOPAZ_3	0x6902		/* Topaz */
#define	PCI_PRODUCT_ATI_TOPAZ_4	0x6903		/* Topaz */
#define	PCI_PRODUCT_ATI_TOPAZ_5	0x6907		/* Topaz */
#define	PCI_PRODUCT_ATI_TONGA_1	0x6920		/* Tonga */
#define	PCI_PRODUCT_ATI_TONGA_2	0x6921		/* Tonga */
#define	PCI_PRODUCT_ATI_TONGA_3	0x6928		/* Tonga */
#define	PCI_PRODUCT_ATI_TONGA_4	0x6929		/* Tonga */
#define	PCI_PRODUCT_ATI_TONGA_5	0x692b		/* Tonga */
#define	PCI_PRODUCT_ATI_TONGA_6	0x692f		/* Tonga */
#define	PCI_PRODUCT_ATI_TONGA_7	0x6930		/* Tonga */
#define	PCI_PRODUCT_ATI_TONGA_8	0x6938		/* Tonga */
#define	PCI_PRODUCT_ATI_TONGA_9	0x6939		/* Tonga */
#define	PCI_PRODUCT_ATI_VEGAM_1	0x694c		/* Vega M */
#define	PCI_PRODUCT_ATI_VEGAM_2	0x694e		/* Vega M */
#define	PCI_PRODUCT_ATI_VEGAM_3	0x694f		/* Vega M */
#define	PCI_PRODUCT_ATI_POLARIS12_1	0x6980		/* Polaris 12 */
#define	PCI_PRODUCT_ATI_POLARIS12_2	0x6981		/* Polaris 12 */
#define	PCI_PRODUCT_ATI_POLARIS12_3	0x6985		/* Polaris 12 */
#define	PCI_PRODUCT_ATI_POLARIS12_4	0x6986		/* Polaris 12 */
#define	PCI_PRODUCT_ATI_POLARIS12_5	0x6987		/* Polaris 12 */
#define	PCI_PRODUCT_ATI_POLARIS12_6	0x6995		/* Polaris 12 */
#define	PCI_PRODUCT_ATI_POLARIS12_7	0x6997		/* Polaris 12 */
#define	PCI_PRODUCT_ATI_POLARIS12_8	0x699f		/* Polaris 12 */
#define	PCI_PRODUCT_ATI_VEGA12_1	0x69a0		/* Vega 12 */
#define	PCI_PRODUCT_ATI_VEGA12_2	0x69a1		/* Vega 12 */
#define	PCI_PRODUCT_ATI_VEGA12_3	0x69a2		/* Vega 12 */
#define	PCI_PRODUCT_ATI_VEGA12_4	0x69a3		/* Vega 12 */
#define	PCI_PRODUCT_ATI_VEGA12_5	0x69af		/* Vega 12 */
#define	PCI_PRODUCT_ATI_POLARIS10_13	0x6fdf		/* Polaris 10 */
#define	PCI_PRODUCT_ATI_RS100_PCI	0x700f		/* RS100 PCI */
#define	PCI_PRODUCT_ATI_RS200_PCI	0x7010		/* RS200 PCI */
#define	PCI_PRODUCT_ATI_RADEON_X1800A	0x7100		/* Radeon X1800 */
#define	PCI_PRODUCT_ATI_RADEON_X1800XT	0x7101		/* Radeon X1800 XT */
#define	PCI_PRODUCT_ATI_MOBILITY_X1800	0x7102		/* Radeon Mobility X1800 */
#define	PCI_PRODUCT_ATI_FIREGL_M_V7200	0x7103		/* FireGL Mobility V7200 */
#define	PCI_PRODUCT_ATI_FIREGL_V7200	0x7104		/* FireGL V7200 */
#define	PCI_PRODUCT_ATI_FIREGL_V5300	0x7105		/* FireGL V5300 */
#define	PCI_PRODUCT_ATI_FIREGL_M_V7100	0x7106		/* FireGL Mobility V7100 */
#define	PCI_PRODUCT_ATI_RADEON_X1800B	0x7108		/* Radeon X1800 */
#define	PCI_PRODUCT_ATI_RADEON_X1800C	0x7109		/* Radeon X1800 */
#define	PCI_PRODUCT_ATI_RADEON_X1800D	0x710a		/* Radeon X1800 */
#define	PCI_PRODUCT_ATI_RADEON_X1800E	0x710b		/* Radeon X1800 */
#define	PCI_PRODUCT_ATI_RADEON_X1800F	0x710c		/* Radeon X1800 */
#define	PCI_PRODUCT_ATI_FIREGL_V7300	0x710e		/* FireGL V7300 */
#define	PCI_PRODUCT_ATI_FIREGL_V7350	0x710f		/* FireGL V7350 */
#define	PCI_PRODUCT_ATI_RADEON_X1600	0x7140		/* Radeon X1600 */
#define	PCI_PRODUCT_ATI_RV505_1	0x7141		/* RV505 */
#define	PCI_PRODUCT_ATI_RADEON_X1300_X1550	0x7142		/* Radeon X1300/X1550 */
#define	PCI_PRODUCT_ATI_RADEON_X1550	0x7143		/* Radeon X1550 */
#define	PCI_PRODUCT_ATI_M54_GL	0x7144		/* M54-GL */
#define	PCI_PRODUCT_ATI_RADEON_X1400	0x7145		/* Radeon Mobility X1400 */
#define	PCI_PRODUCT_ATI_RADEON_X1550_X1300	0x7146		/* Radeon X1300/X1550 */
#define	PCI_PRODUCT_ATI_RADEON_X1550_64	0x7147		/* Radeon X1550 64-bit */
#define	PCI_PRODUCT_ATI_RADEON_X1300_M52	0x7149		/* Radeon Mobility X1300 M52-64 */
#define	PCI_PRODUCT_ATI_MOBILITY_X1300_4A	0x714a		/* Radeon Mobility X1300 */
#define	PCI_PRODUCT_ATI_MOBILITY_X1300_4B	0x714b		/* Radeon Mobility X1300 */
#define	PCI_PRODUCT_ATI_MOBILITY_X1300_4C	0x714c		/* Radeon Mobility X1300 */
#define	PCI_PRODUCT_ATI_RADEON_X1300_4D	0x714d		/* Radeon X1300 */
#define	PCI_PRODUCT_ATI_RADEON_X1300_4E	0x714e		/* Radeon X1300 */
#define	PCI_PRODUCT_ATI_RV505_2	0x714f		/* Radeon X1300 */
#define	PCI_PRODUCT_ATI_RV505_3	0x7151		/* RV505 */
#define	PCI_PRODUCT_ATI_FIREGL_V3300	0x7152		/* FireGL V3300 */
#define	PCI_PRODUCT_ATI_FIREGL_V3350	0x7153		/* FireGL V3350 */
#define	PCI_PRODUCT_ATI_RADEON_X1300_5E	0x715e		/* Radeon X1300 */
#define	PCI_PRODUCT_ATI_RADEON_X1550_64_2	0x715f		/* Radeon X1550 */
#define	PCI_PRODUCT_ATI_RADEON_X1600_S	0x7160		/* Radeon X1600 Sec */
#define	PCI_PRODUCT_ATI_RADEON_X1300_X1550_S	0x7162		/* Radeon X1300/X1550 Sec */
#define	PCI_PRODUCT_ATI_RADEON_X1300X1550	0x7180		/* Radeon X1300/X1550 */
#define	PCI_PRODUCT_ATI_RADEON_X1600_81	0x7181		/* Radeon X1600 */
#define	PCI_PRODUCT_ATI_RADEON_X1300PRO	0x7183		/* Radeon X1300 Pro */
#define	PCI_PRODUCT_ATI_RADEON_X1450	0x7186		/* Radeon X1450 */
#define	PCI_PRODUCT_ATI_RADEON_X1300	0x7187		/* Radeon X1300 */
#define	PCI_PRODUCT_ATI_RADEON_X2300	0x7188		/* Radeon Mobility X2300 */
#define	PCI_PRODUCT_ATI_RADEON_X2300_2	0x718a		/* Radeon Mobility X2300 */
#define	PCI_PRODUCT_ATI_MOBILITY_X1350	0x718b		/* Radeon Mobility X1350 */
#define	PCI_PRODUCT_ATI_MOBILITY_X1350_2	0x718c		/* Radeon Mobility X1350 */
#define	PCI_PRODUCT_ATI_MOBILITY_X1450	0x718d		/* Radeon Mobility X1450 */
#define	PCI_PRODUCT_ATI_RADEON_X1300_8F	0x718f		/* Radeon X1300 */
#define	PCI_PRODUCT_ATI_RADEON_X1550_2	0x7193		/* Radeon X1550 */
#define	PCI_PRODUCT_ATI_MOBILITY_X1350_3	0x7196		/* Radeon Mobility X1350 */
#define	PCI_PRODUCT_ATI_FIREMV_2250	0x719b		/* FireMV 2250 */
#define	PCI_PRODUCT_ATI_RADEON_X1550_64_3	0x719f		/* Radeon X1550 64-bit */
#define	PCI_PRODUCT_ATI_RADEON_X1300PRO_S	0x71a3		/* Radeon X1300 Pro Sec */
#define	PCI_PRODUCT_ATI_RADEON_X1300_S	0x71a7		/* Radeon X1300 Sec */
#define	PCI_PRODUCT_ATI_RADEON_X1600_C0	0x71c0		/* Radeon X1600 */
#define	PCI_PRODUCT_ATI_RADEON_X1650	0x71c1		/* Radeon X1650 */
#define	PCI_PRODUCT_ATI_RADEON_X1600_PRO	0x71c2		/* Radeon X1600 Pro */
#define	PCI_PRODUCT_ATI_RADEON_X1600_C3	0x71c3		/* Radeon X1600 */
#define	PCI_PRODUCT_ATI_FIREGL_V5200	0x71c4		/* FireGL V5200 */
#define	PCI_PRODUCT_ATI_RADEON_X1600_M	0x71c5		/* Radeon Mobility X1600 */
#define	PCI_PRODUCT_ATI_RADEON_X1650_PRO	0x71c6		/* Radeon X1650 Pro */
#define	PCI_PRODUCT_ATI_RADEON_X1650_PRO2	0x71c7		/* Radeon X1650 Pro */
#define	PCI_PRODUCT_ATI_RADEON_X1600_CD	0x71cd		/* Radeon X1600 */
#define	PCI_PRODUCT_ATI_RADEON_X1300_XT	0x71ce		/* Radeon X1300 XT */
#define	PCI_PRODUCT_ATI_FIREGL_V3400	0x71d2		/* FireGL V3400 */
#define	PCI_PRODUCT_ATI_RV530_M56	0x71d4		/* Mobility FireGL V5250 */
#define	PCI_PRODUCT_ATI_RADEON_X1700	0x71d5		/* Radeon X1700 */
#define	PCI_PRODUCT_ATI_RADEON_X1700XT	0x71d6		/* Radeon X1700 XT */
#define	PCI_PRODUCT_ATI_FIREGL_V5200_1	0x71da		/* FireGL V5200 */
#define	PCI_PRODUCT_ATI_MOBILITY_X1700	0x71de		/* Radeon Mobility X1700 */
#define	PCI_PRODUCT_ATI_RADEON_X1600_PRO_S	0x71e2		/* Radeon X1600 Pro Sec */
#define	PCI_PRODUCT_ATI_RADEON_X1650_PRO_S	0x71e6		/* Radeon X1650 Pro Sec */
#define	PCI_PRODUCT_ATI_RADEON_X1650_PRO2_S	0x71e7		/* Radeon X1650 Pro Sec */
#define	PCI_PRODUCT_ATI_RADEON_X2300HD	0x7200		/* Radeon X2300HD */
#define	PCI_PRODUCT_ATI_MOBILITY_X2300HD	0x7210		/* Radeon Mobility X2300HD */
#define	PCI_PRODUCT_ATI_MOBILITY_X2300HD_1	0x7211		/* Radeon Mobility X2300HD */
#define	PCI_PRODUCT_ATI_RADEON_X1950_40	0x7240		/* Radeon X1950 */
#define	PCI_PRODUCT_ATI_RADEON_X1900_43	0x7243		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_RADEON_X1950_44	0x7244		/* Radeon X1950 */
#define	PCI_PRODUCT_ATI_RADEON_X1900_45	0x7245		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_RADEON_X1900_46	0x7246		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_RADEON_X1900_47	0x7247		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_RADEON_X1900_48	0x7248		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_RADEON_X1900_49	0x7249		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_RADEON_X1900_4A	0x724a		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_RADEON_X1900_4B	0x724b		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_RADEON_X1900_4C	0x724c		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_RADEON_X1900_4D	0x724d		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_STREAM_PROCESSOR	0x724e		/* AMD Stream Processor */
#define	PCI_PRODUCT_ATI_RADEON_X1900_4F	0x724f		/* Radeon X1900 */
#define	PCI_PRODUCT_ATI_RADEON_X1950_PRO	0x7280		/* Radeon X1950 Pro */
#define	PCI_PRODUCT_ATI_RV560	0x7281		/* RV560 */
#define	PCI_PRODUCT_ATI_RV560_1	0x7283		/* RV560 */
#define	PCI_PRODUCT_ATI_MOBILITY_X1900	0x7284		/* Radeon Mobility X1900 */
#define	PCI_PRODUCT_ATI_RV560_2	0x7287		/* RV560 */
#define	PCI_PRODUCT_ATI_RADEON_X1950GT	0x7288		/* Radeon X1950 GT */
#define	PCI_PRODUCT_ATI_RV570	0x7289		/* RV570 */
#define	PCI_PRODUCT_ATI_RV570_2	0x728b		/* RV570 */
#define	PCI_PRODUCT_ATI_FIREGL_V7400	0x728c		/* FireGL V7400 */
#define	PCI_PRODUCT_ATI_RV560_3	0x7290		/* RV560 */
#define	PCI_PRODUCT_ATI_RADEON_RX1650_XT	0x7291		/* Radeon RX1650 XT */
#define	PCI_PRODUCT_ATI_RADEON_X1650_1	0x7293		/* Radeon X1650 */
#define	PCI_PRODUCT_ATI_RV560_4	0x7297		/* RV560 */
#define	PCI_PRODUCT_ATI_RADEON_X1950_PRO_S	0x72a0		/* Radeon X1950 Pro Sec */
#define	PCI_PRODUCT_ATI_RADEON_RX1650_XT_2	0x72b1		/* Radeon RX1650 XT Sec */
#define	PCI_PRODUCT_ATI_FIJI_1	0x7300		/* Fiji */
#define	PCI_PRODUCT_ATI_FIJI_2	0x730f		/* Fiji */
#define	PCI_PRODUCT_ATI_NAVI10_1	0x7310		/* Navi 10 */
#define	PCI_PRODUCT_ATI_NAVI10_2	0x7312		/* Radeon Pro W5700 */
#define	PCI_PRODUCT_ATI_NAVI10_3	0x7318		/* Navi 10 */
#define	PCI_PRODUCT_ATI_NAVI10_4	0x7319		/* Navi 10 */
#define	PCI_PRODUCT_ATI_NAVI10_5	0x731a		/* Navi 10 */
#define	PCI_PRODUCT_ATI_NAVI10_6	0x731b		/* Navi 10 */
#define	PCI_PRODUCT_ATI_NAVI10_7	0x731e		/* Navi 10 */
#define	PCI_PRODUCT_ATI_NAVI10_8	0x731f		/* Navi 10 */
#define	PCI_PRODUCT_ATI_NAVI14_1	0x7340		/* Navi 14 */
#define	PCI_PRODUCT_ATI_NAVI14_2	0x7341		/* Radeon Pro W5500 */
#define	PCI_PRODUCT_ATI_NAVI14_3	0x7347		/* Radeon Pro W5500M */
#define	PCI_PRODUCT_ATI_NAVI14_4	0x734f		/* Navi 14 */
#define	PCI_PRODUCT_ATI_NAVI12_1	0x7360		/* Navi 12 */
#define	PCI_PRODUCT_ATI_NAVI12_2	0x7362		/* Navi 12 */
#define	PCI_PRODUCT_ATI_ARCTURUS_1	0x7388		/* Arcturus */
#define	PCI_PRODUCT_ATI_ARCTURUS_2	0x738c		/* Arcturus */
#define	PCI_PRODUCT_ATI_ARCTURUS_3	0x738e		/* Arcturus */
#define	PCI_PRODUCT_ATI_ARCTURUS_4	0x7390		/* Arcturus */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_1	0x73a0		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_2	0x73a1		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_3	0x73a2		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_4	0x73a3		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_5	0x73a5		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_6	0x73a8		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_7	0x73a9		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_8	0x73ab		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_9	0x73ac		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_10	0x73ad		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_11	0x73ae		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_12	0x73af		/* Navi 21 */
#define	PCI_PRODUCT_ATI_SIENNA_CICHLID_13	0x73bf		/* Navi 21 */
#define	PCI_PRODUCT_ATI_NAVY_FLOUNDER_1	0x73c0		/* Navi 22 */
#define	PCI_PRODUCT_ATI_NAVY_FLOUNDER_2	0x73c1		/* Navi 22 */
#define	PCI_PRODUCT_ATI_NAVY_FLOUNDER_3	0x73c3		/* Navi 22 */
#define	PCI_PRODUCT_ATI_NAVY_FLOUNDER_4	0x73da		/* Navi 22 */
#define	PCI_PRODUCT_ATI_NAVY_FLOUNDER_5	0x73db		/* Navi 22 */
#define	PCI_PRODUCT_ATI_NAVY_FLOUNDER_6	0x73dc		/* Navi 22 */
#define	PCI_PRODUCT_ATI_NAVY_FLOUNDER_7	0x73dd		/* Navi 22 */
#define	PCI_PRODUCT_ATI_NAVY_FLOUNDER_8	0x73de		/* Navi 22 */
#define	PCI_PRODUCT_ATI_NAVY_FLOUNDER_9	0x73df		/* Navi 22 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_1	0x73e0		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_2	0x73e1		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_3	0x73e2		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_4	0x73e3		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_5	0x73e8		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_6	0x73e9		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_7	0x73ea		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_8	0x73eb		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_9	0x73ec		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_10	0x73ed		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_11	0x73ef		/* Navi 23 */
#define	PCI_PRODUCT_ATI_DIMGREY_CAVEFISH_12	0x73ff		/* Navi 23 */
#define	PCI_PRODUCT_ATI_ALDEBARAN_1	0x7408		/* Aldebaran */
#define	PCI_PRODUCT_ATI_ALDEBARAN_2	0x740c		/* Aldebaran */
#define	PCI_PRODUCT_ATI_ALDEBARAN_3	0x740f		/* Aldebaran */
#define	PCI_PRODUCT_ATI_ALDEBARAN_4	0x7410		/* Aldebaran */
#define	PCI_PRODUCT_ATI_BEIGE_GOBY_1	0x7420		/* Navi 24 */
#define	PCI_PRODUCT_ATI_BEIGE_GOBY_2	0x7421		/* Navi 24 */
#define	PCI_PRODUCT_ATI_BEIGE_GOBY_3	0x7422		/* Navi 24 */
#define	PCI_PRODUCT_ATI_BEIGE_GOBY_4	0x7423		/* Navi 24 */
#define	PCI_PRODUCT_ATI_BEIGE_GOBY_5	0x7424		/* Navi 24 */
#define	PCI_PRODUCT_ATI_BEIGE_GOBY_6	0x743f		/* Navi 24 */
#define	PCI_PRODUCT_ATI_NAVI31_2	0x7448		/* Navi 31 */
#define	PCI_PRODUCT_ATI_NAVI31_5	0x7449		/* Navi 31 */
#define	PCI_PRODUCT_ATI_NAVI31_4	0x744a		/* Navi 31 */
#define	PCI_PRODUCT_ATI_NAVI31_6	0x744b		/* Navi 31 */
#define	PCI_PRODUCT_ATI_NAVI31_1	0x744c		/* Navi 31 */
#define	PCI_PRODUCT_ATI_NAVI31_3	0x745e		/* Navi 31 */
#define	PCI_PRODUCT_ATI_NAVI32_3	0x7460		/* Navi 32 */
#define	PCI_PRODUCT_ATI_NAVI32_4	0x7461		/* Navi 32 */
#define	PCI_PRODUCT_ATI_NAVI32_1	0x7470		/* Navi 32 */
#define	PCI_PRODUCT_ATI_NAVI32_2	0x747e		/* Navi 32 */
#define	PCI_PRODUCT_ATI_NAVI33_1	0x7480		/* Navi 33 */
#define	PCI_PRODUCT_ATI_NAVI33_2	0x7483		/* Navi 33 */
#define	PCI_PRODUCT_ATI_NAVI33_3	0x7489		/* Navi 33 */
#define	PCI_PRODUCT_ATI_NAVI33_4	0x7499		/* Navi 33 */
#define	PCI_PRODUCT_ATI_MI300A	0x74a0		/* MI300A */
#define	PCI_PRODUCT_ATI_MI300X	0x74a1		/* MI300X */
#define	PCI_PRODUCT_ATI_NAVI48_1	0x7550		/* Navi 48 */
#define	PCI_PRODUCT_ATI_NAVI48_2	0x7551		/* Navi 48 */
#define	PCI_PRODUCT_ATI_NAVI44	0x7590		/* Navi 44 */
#define	PCI_PRODUCT_ATI_RADEON_9000IGP	0x7834		/* Radeon 9000/9100 IGP */
#define	PCI_PRODUCT_ATI_RADEON_RS350IGP	0x7835		/* Radeon RS350IGP */
#define	PCI_PRODUCT_ATI_RS690_HB	0x7910		/* RS690 Host */
#define	PCI_PRODUCT_ATI_RS740_HB	0x7911		/* RS740 Host */
#define	PCI_PRODUCT_ATI_RS690_PCIE_1	0x7912		/* RS690 PCIE */
#define	PCI_PRODUCT_ATI_RS690M_PCIE_1	0x7913		/* RS690M PCIE */
#define	PCI_PRODUCT_ATI_RS690_PCIE_2	0x7914		/* RS690 PCIE */
#define	PCI_PRODUCT_ATI_RS690_PCIE_3	0x7915		/* RS690 PCIE */
#define	PCI_PRODUCT_ATI_RS690_PCIE_4	0x7916		/* RS690 PCIE */
#define	PCI_PRODUCT_ATI_RS690_PCIE_5	0x7917		/* RS690 PCIE */
#define	PCI_PRODUCT_ATI_RS690_HDA	0x7919		/* RS690 HD Audio */
#define	PCI_PRODUCT_ATI_RADEON_X1250_1	0x791e		/* Radeon X1250 */
#define	PCI_PRODUCT_ATI_RADEON_X1250IGP	0x791f		/* Radeon X1250 IGP */
#define	PCI_PRODUCT_ATI_RADEON_X1200_1	0x793f		/* Radeon X1200 */
#define	PCI_PRODUCT_ATI_RADEON_X1200_2	0x7941		/* Radeon X1200 */
#define	PCI_PRODUCT_ATI_RADEON_X1200_3	0x7942		/* Radeon X1200 */
#define	PCI_PRODUCT_ATI_RS740	0x796c		/* RS740 */
#define	PCI_PRODUCT_ATI_RS740M_1	0x796d		/* RS740M */
#define	PCI_PRODUCT_ATI_RADEON_2100	0x796e		/* Radeon 2100 */
#define	PCI_PRODUCT_ATI_RS740M_2	0x796f		/* RS740M */
#define	PCI_PRODUCT_ATI_RADEON_HD2900XT_1	0x9400		/* Radeon HD 2900 XT */
#define	PCI_PRODUCT_ATI_RADEON_HD2900XT_2	0x9401		/* Radeon HD 2900 XT */
#define	PCI_PRODUCT_ATI_RADEON_HD2900XT_3	0x9402		/* Radeon HD 2900 XT */
#define	PCI_PRODUCT_ATI_RADEON_HD2900PRO	0x9403		/* Radeon HD 2900 Pro */
#define	PCI_PRODUCT_ATI_RADEON_HD2900GT	0x9405		/* Radeon HD 2900 GT */
#define	PCI_PRODUCT_ATI_FIREGL_V8650	0x940a		/* FireGL V8650 */
#define	PCI_PRODUCT_ATI_FIREGL_V8600_1	0x940b		/* FireGL V8600 */
#define	PCI_PRODUCT_ATI_FIREGL_V8600_2	0x940f		/* FireGL V8600 */
#define	PCI_PRODUCT_ATI_RADEON_HD4870	0x9440		/* Radeon HD 4870 */
#define	PCI_PRODUCT_ATI_RADEON_HD4870_X2	0x9441		/* Radeon HD 4870 X2 */
#define	PCI_PRODUCT_ATI_RADEON_HD4850	0x9442		/* Radeon HD 4850 */
#define	PCI_PRODUCT_ATI_RADEON_HD4850_X2	0x9443		/* Radeon HD 4850 X2 */
#define	PCI_PRODUCT_ATI_FIREPRO_V8750	0x9444		/* FirePro V8750 */
#define	PCI_PRODUCT_ATI_FIREPRO_V7760	0x9446		/* FirePro V7760 */
#define	PCI_PRODUCT_ATI_RADEON_HD4850_M	0x944a		/* Mobility Radeon HD 4850 */
#define	PCI_PRODUCT_ATI_RADEON_HD4850_X2_M	0x944b		/* Mobility Radeon HD 4850 X2 */
#define	PCI_PRODUCT_ATI_RADEON_HD4800	0x944c		/* Radeon HD 4800 */
#define	PCI_PRODUCT_ATI_FIREPRO_RV770	0x944e		/* FirePro RV770 */
#define	PCI_PRODUCT_ATI_FIRESTREAM_9270	0x9450		/* FireStream 9270 */
#define	PCI_PRODUCT_ATI_FIRESTREAM_9250	0x9452		/* FireStream 9250 */
#define	PCI_PRODUCT_ATI_FIREPRO_V8700	0x9456		/* FirePro V8700 */
#define	PCI_PRODUCT_ATI_RADEON_HD4870_M98	0x945a		/* Mobility Radeon HD 4870 */
#define	PCI_PRODUCT_ATI_RADEON_M98_1	0x945b		/* Radeon M98 */
#define	PCI_PRODUCT_ATI_RADEON_HD4870_M	0x945e		/* Mobility Radeon HD 4870 */
#define	PCI_PRODUCT_ATI_RADEON_HD4890	0x9460		/* Radeon HD 4890 */
#define	PCI_PRODUCT_ATI_RADEON_HD4800_2	0x9462		/* Radeon HD 4800 */
#define	PCI_PRODUCT_ATI_FIREPRO_M7750	0x946a		/* FirePro M7750 */
#define	PCI_PRODUCT_ATI_RADEON_M98_2	0x946b		/* Radeon M98 */
#define	PCI_PRODUCT_ATI_RADEON_M98_3	0x947a		/* Radeon M98 */
#define	PCI_PRODUCT_ATI_RADEON_M98_4	0x947b		/* Radeon M98 */
#define	PCI_PRODUCT_ATI_RADEON_HD4650_M	0x9480		/* Mobility Radeon HD 4650 */
#define	PCI_PRODUCT_ATI_RV730_1	0x9487		/* RV730 */
#define	PCI_PRODUCT_ATI_RADEON_HD4670_M	0x9488		/* Mobility Radeon HD 4670 */
#define	PCI_PRODUCT_ATI_RV730_2	0x9489		/* RV730 */
#define	PCI_PRODUCT_ATI_RADEON_HD4670_M_2	0x948a		/* Mobility Radeon HD 4670 */
#define	PCI_PRODUCT_ATI_RV730_3	0x948f		/* RV730 */
#define	PCI_PRODUCT_ATI_RADEON_HD4670	0x9490		/* Radeon HD 4670 */
#define	PCI_PRODUCT_ATI_RADEON_E4600	0x9491		/* Radeon E4600 */
#define	PCI_PRODUCT_ATI_RADEON_HD4600	0x9495		/* Radeon HD 4600 */
#define	PCI_PRODUCT_ATI_RADEON_HD4650	0x9498		/* Radeon HD 4650 */
#define	PCI_PRODUCT_ATI_FIREPRO_V7750	0x949c		/* FirePro V7750 */
#define	PCI_PRODUCT_ATI_FIREPRO_V5700	0x949e		/* FirePro V5700 */
#define	PCI_PRODUCT_ATI_FIREPRO_V3750	0x949f		/* FirePro V3750 */
#define	PCI_PRODUCT_ATI_RADEON_HD4830_M	0x94a0		/* Mobility Radeon HD 4830 */
#define	PCI_PRODUCT_ATI_RADEON_HD4850_M_2	0x94a1		/* Mobility Radeon HD 4850 */
#define	PCI_PRODUCT_ATI_FIREPRO_M7740	0x94a3		/* FirePro M7740 */
#define	PCI_PRODUCT_ATI_RV740	0x94b1		/* RV740 */
#define	PCI_PRODUCT_ATI_RADEON_HD4770_1	0x94b3		/* Radeon HD 4770 */
#define	PCI_PRODUCT_ATI_RADEON_HD4700	0x94b4		/* Radeon HD 4700 */
#define	PCI_PRODUCT_ATI_RADEON_HD4770_2	0x94b5		/* Radeon HD 4770 */
#define	PCI_PRODUCT_ATI_FIREPRO_M5750	0x94b9		/* FirePro M5750 */
#define	PCI_PRODUCT_ATI_RV610_1	0x94c0		/* RV610 */
#define	PCI_PRODUCT_ATI_RADEON_HD2400_XT	0x94c1		/* Radeon HD 2400 XT */
#define	PCI_PRODUCT_ATI_RADEON_HD2400_PRO	0x94c3		/* Radeon HD 2400 Pro */
#define	PCI_PRODUCT_ATI_RADEON_HD2400PROAGP	0x94c4		/* Radeon HD 2400 Pro AGP */
#define	PCI_PRODUCT_ATI_FIREGL_V4000	0x94c5		/* FireGL V4000 */
#define	PCI_PRODUCT_ATI_RV610_2	0x94c6		/* RV610 */
#define	PCI_PRODUCT_ATI_RADEON_HD2350	0x94c7		/* Radeon HD 2350 */
#define	PCI_PRODUCT_ATI_RADEON_HD2400_XT_M	0x94c8		/* Mobility Radeon HD 2400 XT */
#define	PCI_PRODUCT_ATI_RADEON_HD2400_M72	0x94c9		/* Mobility Radeon HD 2400 */
#define	PCI_PRODUCT_ATI_RADEON_E2400	0x94cb		/* Radeon E2400 */
#define	PCI_PRODUCT_ATI_RADEON_HD2400PROPCI	0x94cc		/* Radeon HD 2400 Pro PCI */
#define	PCI_PRODUCT_ATI_FIREMV_2260	0x94cd		/* FireMV 2260 */
#define	PCI_PRODUCT_ATI_RV670_1	0x9500		/* RV670 */
#define	PCI_PRODUCT_ATI_RADEON_HD3870	0x9501		/* Radeon HD 3870 */
#define	PCI_PRODUCT_ATI_RADEON_HD3850_M	0x9504		/* Mobility Radeon HD 3850 */
#define	PCI_PRODUCT_ATI_RADEON_HD3850	0x9505		/* Radeon HD 3850 */
#define	PCI_PRODUCT_ATI_RADEON_HD3850_X2_M	0x9506		/* Mobility Radeon HD 3850 X2 */
#define	PCI_PRODUCT_ATI_RV670_2	0x9507		/* RV670 */
#define	PCI_PRODUCT_ATI_RADEON_HD3870_M	0x9508		/* Mobility Radeon HD 3870 */
#define	PCI_PRODUCT_ATI_RADEON_HD3870_X2_M	0x9509		/* Mobility Radeon HD 3870 X2 */
#define	PCI_PRODUCT_ATI_RADEON_HD3870_X2	0x950f		/* Radeon HD 3870 X2 */
#define	PCI_PRODUCT_ATI_FIREGL_V7700	0x9511		/* FireGL V7700 */
#define	PCI_PRODUCT_ATI_RADEON_HD3850_AGP	0x9515		/* Radeon HD 3850 AGP */
#define	PCI_PRODUCT_ATI_RADEON_HD3690	0x9517		/* Radeon HD 3690 */
#define	PCI_PRODUCT_ATI_FIRESTREAM_9170	0x9519		/* FireStream */
#define	PCI_PRODUCT_ATI_RADEON_HD4550	0x9540		/* Radeon HD 4550 */
#define	PCI_PRODUCT_ATI_RV710_1	0x9541		/* RV710 */
#define	PCI_PRODUCT_ATI_RV710_2	0x9542		/* RV710 */
#define	PCI_PRODUCT_ATI_RV710_3	0x954e		/* RV710 */
#define	PCI_PRODUCT_ATI_RADEON_HD4350	0x954f		/* Radeon HD 4350 */
#define	PCI_PRODUCT_ATI_RADEON_HD4300_M	0x9552		/* Mobility Radeon HD 4300 */
#define	PCI_PRODUCT_ATI_RADEON_HD4500_M	0x9553		/* Mobility Radeon HD 4500 */
#define	PCI_PRODUCT_ATI_RADEON_HD4500_M_2	0x9555		/* Mobility Radeon HD 4500 */
#define	PCI_PRODUCT_ATI_FIREPRO_RG220	0x9557		/* FirePro RG220 */
#define	PCI_PRODUCT_ATI_RADEON_HD4330_M	0x955f		/* Mobility Radeon HD 4330 */
#define	PCI_PRODUCT_ATI_RV630_1	0x9580		/* RV630 */
#define	PCI_PRODUCT_ATI_RADEON_HD2600_M76	0x9581		/* Mobility Radeon HD 2600 */
#define	PCI_PRODUCT_ATI_RADEON_HD2600_XT_M	0x9583		/* Mobility Radeon HD 2600 XT */
#define	PCI_PRODUCT_ATI_RADEON_HD2600XTAGP	0x9586		/* Radeon HD 2600 XT AGP */
#define	PCI_PRODUCT_ATI_RADEON_HD2600PROAGP	0x9587		/* Radeon HD 2600 Pro AGP */
#define	PCI_PRODUCT_ATI_RADEON_HD2600_XT	0x9588		/* Radeon HD 2600 XT */
#define	PCI_PRODUCT_ATI_RADEON_HD2600_PRO	0x9589		/* Radeon HD 2600 Pro */
#define	PCI_PRODUCT_ATI_RV630_2	0x958a		/* RV630 */
#define	PCI_PRODUCT_ATI_RV630_3	0x958b		/* RV630 */
#define	PCI_PRODUCT_ATI_FIREGL_V5600	0x958c		/* FireGL V5600 */
#define	PCI_PRODUCT_ATI_FIREGL_V3600	0x958d		/* FireGL V3600 */
#define	PCI_PRODUCT_ATI_RV630_4	0x958e		/* RV630 */
#define	PCI_PRODUCT_ATI_RV630_5	0x958f		/* RV630 */
#define	PCI_PRODUCT_ATI_RADEON_HD3600	0x9590		/* Radeon HD 3600 */
#define	PCI_PRODUCT_ATI_RADEON_HD3650_M	0x9591		/* Mobility Radeon HD 3650 */
#define	PCI_PRODUCT_ATI_RADEON_HD3670_M	0x9593		/* Mobility Radeon HD 3670 */
#define	PCI_PRODUCT_ATI_FIREGL_V5700_M	0x9595		/* Mobility FireGL V5700 */
#define	PCI_PRODUCT_ATI_RADEON_HD3650_AGP	0x9596		/* Radeon HD 3650 AGP */
#define	PCI_PRODUCT_ATI_RV635_1	0x9597		/* RV635 */
#define	PCI_PRODUCT_ATI_RADEON_HD3650	0x9598		/* Radeon HD 3650 */
#define	PCI_PRODUCT_ATI_RV635_2	0x9599		/* RV635 */
#define	PCI_PRODUCT_ATI_FIREGL_V5725_M	0x959b		/* Mobility FireGL V5725 */
#define	PCI_PRODUCT_ATI_RADEON_HD3470	0x95c0		/* Radeon HD 3470 */
#define	PCI_PRODUCT_ATI_RADEON_HD3430_M	0x95c2		/* Mobility Radeon HD 3430 */
#define	PCI_PRODUCT_ATI_RADEON_HD3400_M82	0x95c4		/* Mobility Radeon HD 3400 */
#define	PCI_PRODUCT_ATI_RADEON_HD3450	0x95c5		/* Radeon HD 3450 */
#define	PCI_PRODUCT_ATI_RADEON_HD3450_AGP	0x95c6		/* Radeon HD 3450 AGP */
#define	PCI_PRODUCT_ATI_RADEON_HD3430	0x95c7		/* Radeon HD 3430 */
#define	PCI_PRODUCT_ATI_RADEON_HD3450_PCI	0x95c9		/* Radeon HD 3450 PCI */
#define	PCI_PRODUCT_ATI_FIREPRO_V3700	0x95cc		/* FirePro V3700 */
#define	PCI_PRODUCT_ATI_FIREMV_2450	0x95cd		/* FireMV 2450 */
#define	PCI_PRODUCT_ATI_FIREMV_2260_1	0x95ce		/* FireMV 2260 */
#define	PCI_PRODUCT_ATI_FIREMV_2260_2	0x95cf		/* FireMV 2260 */
#define	PCI_PRODUCT_ATI_RS780_HDA	0x960f		/* RS780 HD Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD3200_1	0x9610		/* Radeon HD 3200 */
#define	PCI_PRODUCT_ATI_RADEON_HD3100	0x9611		/* Radeon HD 3100 */
#define	PCI_PRODUCT_ATI_RADEON_HD3200_2	0x9612		/* Radeon HD 3200 */
#define	PCI_PRODUCT_ATI_RADEON_3100	0x9613		/* Radeon 3100 */
#define	PCI_PRODUCT_ATI_RADEON_HD3300	0x9614		/* Radeon HD 3300 */
#define	PCI_PRODUCT_ATI_RADEON_HD3200_3	0x9615		/* Radeon HD 3200 */
#define	PCI_PRODUCT_ATI_RADEON_HD3000	0x9616		/* Radeon HD 3000 */
#define	PCI_PRODUCT_ATI_RADEON_HD6550D	0x9640		/* Radeon HD 6550D */
#define	PCI_PRODUCT_ATI_RADEON_HD6620G	0x9641		/* Radeon HD 6620G */
#define	PCI_PRODUCT_ATI_RADEON_HD6370D	0x9642		/* Radeon HD 6370D */
#define	PCI_PRODUCT_ATI_RADEON_HD6380G	0x9643		/* Radeon HD 6380G */
#define	PCI_PRODUCT_ATI_RADEON_HD6410D_1	0x9644		/* Radeon HD 6410D */
#define	PCI_PRODUCT_ATI_RADEON_HD6410D_2	0x9645		/* Radeon HD 6410D */
#define	PCI_PRODUCT_ATI_RADEON_HD6520G	0x9647		/* Radeon HD 6520G */
#define	PCI_PRODUCT_ATI_RADEON_HD6480G_1	0x9648		/* Radeon HD 6480G */
#define	PCI_PRODUCT_ATI_RADEON_HD6480G_2	0x9649		/* Radeon HD 6480G */
#define	PCI_PRODUCT_ATI_RADEON_HD6530D	0x964a		/* Radeon HD 6530D */
#define	PCI_PRODUCT_ATI_SUMO_1	0x964b		/* Sumo */
#define	PCI_PRODUCT_ATI_SUMO_2	0x964c		/* Sumo */
#define	PCI_PRODUCT_ATI_SUMO_3	0x964e		/* Sumo */
#define	PCI_PRODUCT_ATI_SUMO_4	0x964f		/* Sumo */
#define	PCI_PRODUCT_ATI_RADEON_HD4200_HDA	0x970f		/* Radeon HD 4200 HD Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD4200	0x9710		/* Radeon HD 4200 */
#define	PCI_PRODUCT_ATI_RADEON_HD4100	0x9711		/* Radeon HD 4100 */
#define	PCI_PRODUCT_ATI_RADEON_HD4200_M	0x9712		/* Mobility Radeon HD 4200 */
#define	PCI_PRODUCT_ATI_RADEON_HD4100_M	0x9713		/* Mobility Radeon HD 4100 */
#define	PCI_PRODUCT_ATI_RADEON_HD4290	0x9714		/* Radeon HD 4290 */
#define	PCI_PRODUCT_ATI_RADEON_HD4250	0x9715		/* Radeon HD 4250 */
#define	PCI_PRODUCT_ATI_RADEON_HD6310_1	0x9802		/* Radeon HD 6310 */
#define	PCI_PRODUCT_ATI_RADEON_HD6310_2	0x9803		/* Radeon HD 6310 */
#define	PCI_PRODUCT_ATI_RADEON_HD6250_1	0x9804		/* Radeon HD 6250 */
#define	PCI_PRODUCT_ATI_RADEON_HD6250_2	0x9805		/* Radeon HD 6250 */
#define	PCI_PRODUCT_ATI_RADEON_HD6320	0x9806		/* Radeon HD 6320 */
#define	PCI_PRODUCT_ATI_RADEON_HD6290	0x9807		/* Radeon HD 6290 */
#define	PCI_PRODUCT_ATI_RADEON_HD7340	0x9808		/* Radeon HD 7340 */
#define	PCI_PRODUCT_ATI_RADEON_HD7310	0x9809		/* Radeon HD 7310 */
#define	PCI_PRODUCT_ATI_RADEON_HD7290	0x980a		/* Radeon HD 7290 */
#define	PCI_PRODUCT_ATI_KABINI_1	0x9830		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_2	0x9831		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_3	0x9832		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_4	0x9833		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_5	0x9834		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_6	0x9835		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_7	0x9836		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_8	0x9837		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_9	0x9838		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_10	0x9839		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_11	0x983a		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_12	0x983b		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_13	0x983c		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_14	0x983d		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_15	0x983e		/* Kabini */
#define	PCI_PRODUCT_ATI_KABINI_16	0x983f		/* Kabini */
#define	PCI_PRODUCT_ATI_RADEON_HDA	0x9840		/* Radeon HD Audio */
#define	PCI_PRODUCT_ATI_MULLINS_1	0x9850		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_2	0x9851		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_3	0x9852		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_4	0x9853		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_5	0x9854		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_6	0x9855		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_7	0x9856		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_8	0x9857		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_9	0x9858		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_10	0x9859		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_11	0x985a		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_12	0x985b		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_13	0x985c		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_14	0x985d		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_15	0x985e		/* Mullins */
#define	PCI_PRODUCT_ATI_MULLINS_16	0x985f		/* Mullins */
#define	PCI_PRODUCT_ATI_CARRIZO_1	0x9870		/* Carrizo */
#define	PCI_PRODUCT_ATI_CARRIZO_2	0x9874		/* Carrizo */
#define	PCI_PRODUCT_ATI_CARRIZO_3	0x9875		/* Carrizo */
#define	PCI_PRODUCT_ATI_CARRIZO_4	0x9876		/* Carrizo */
#define	PCI_PRODUCT_ATI_CARRIZO_5	0x9877		/* Carrizo */
#define	PCI_PRODUCT_ATI_STONEY	0x98e4		/* Stoney Ridge */
#define	PCI_PRODUCT_ATI_ARUBA_1	0x9900		/* Aruba */
#define	PCI_PRODUCT_ATI_RADEON_HD7660D	0x9901		/* Radeon HD 7660D */
#define	PCI_PRODUCT_ATI_RADEON_HD7640G_1	0x9903		/* Radeon HD 7640G */
#define	PCI_PRODUCT_ATI_RADEON_HD7560D	0x9904		/* Radeon HD 7560D */
#define	PCI_PRODUCT_ATI_FIREPRO_A300_1	0x9905		/* FirePro A300 */
#define	PCI_PRODUCT_ATI_FIREPRO_A300_2	0x9906		/* FirePro A300 */
#define	PCI_PRODUCT_ATI_RADEON_HD7620G_1	0x9907		/* Radeon HD 7620G */
#define	PCI_PRODUCT_ATI_RADEON_HD7600G_1	0x9908		/* Radeon HD 7600G */
#define	PCI_PRODUCT_ATI_RADEON_HD7500G_1	0x9909		/* Radeon HD 7500G */
#define	PCI_PRODUCT_ATI_RADEON_HD7500G_2	0x990a		/* Radeon HD 7500G */
#define	PCI_PRODUCT_ATI_RADEON_HD8650G	0x990b		/* Radeon HD 8650G */
#define	PCI_PRODUCT_ATI_RADEON_HD8670D	0x990c		/* Radeon HD 8670D */
#define	PCI_PRODUCT_ATI_RADEON_HD8550G	0x990d		/* Radeon HD 8550G */
#define	PCI_PRODUCT_ATI_RADEON_HD8570D	0x990e		/* Radeon HD 8570D */
#define	PCI_PRODUCT_ATI_RADEON_HD8610G	0x990f		/* Radeon HD 8610G */
#define	PCI_PRODUCT_ATI_RADEON_HD7660G	0x9910		/* Radeon HD 7660G */
#define	PCI_PRODUCT_ATI_RADEON_HD7640G_2	0x9913		/* Radeon HD 7640G */
#define	PCI_PRODUCT_ATI_RADEON_HD7620G_2	0x9917		/* Radeon HD 7620G */
#define	PCI_PRODUCT_ATI_RADEON_HD7600G_2	0x9918		/* Radeon HD 7600G */
#define	PCI_PRODUCT_ATI_RADEON_HD7500G	0x9919		/* Radeon HD 7500G */
#define	PCI_PRODUCT_ATI_RADEON_HD7520G_1	0x9990		/* Radeon HD 7520G */
#define	PCI_PRODUCT_ATI_RADEON_HD7540D	0x9991		/* Radeon HD 7540D */
#define	PCI_PRODUCT_ATI_RADEON_HD7420G_1	0x9992		/* Radeon HD 7420G */
#define	PCI_PRODUCT_ATI_RADEON_HD7480D	0x9993		/* Radeon HD 7480D */
#define	PCI_PRODUCT_ATI_RADEON_HD7400G_1	0x9994		/* Radeon HD 7400G */
#define	PCI_PRODUCT_ATI_RADEON_HD8450G	0x9995		/* Radeon HD 8450G */
#define	PCI_PRODUCT_ATI_RADEON_HD8470D	0x9996		/* Radeon HD 8470D */
#define	PCI_PRODUCT_ATI_RADEON_HD8350G	0x9997		/* Radeon HD 8350G */
#define	PCI_PRODUCT_ATI_RADEON_HD8370D	0x9998		/* Radeon HD 8370D */
#define	PCI_PRODUCT_ATI_RADEON_HD8510G	0x9999		/* Radeon HD 8510G */
#define	PCI_PRODUCT_ATI_RADEON_HD8410G	0x999a		/* Radeon HD 8410G */
#define	PCI_PRODUCT_ATI_RADEON_HD8310G	0x999b		/* Radeon HD 8310G */
#define	PCI_PRODUCT_ATI_ARUBA_2	0x999c		/* Aruba */
#define	PCI_PRODUCT_ATI_ARUBA_3	0x999d		/* Aruba */
#define	PCI_PRODUCT_ATI_RADEON_HD7520G_2	0x99a0		/* Radeon HD 7520G */
#define	PCI_PRODUCT_ATI_RADEON_HD7420G_2	0x99a2		/* Radeon HD 7420G */
#define	PCI_PRODUCT_ATI_RADEON_HD7400G_2	0x99a4		/* Radeon HD 7400G */
#define	PCI_PRODUCT_ATI_RADEON_HD2600_HDA	0xaa08		/* Radeon HD 2600 HD Audio */
#define	PCI_PRODUCT_ATI_RS690M_HDA	0xaa10		/* RS690M HD Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD3870_HDA	0xaa18		/* Radeon HD 3870 HD Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD3600_HDA	0xaa20		/* Radeon HD 3600 HD Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD34XX_HDA	0xaa28		/* Radeon HD 34xx HD Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD48XX_HDA	0xaa30		/* Radeon HD 48xx HD Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD4000_HDA	0xaa38		/* Radeon HD 4000 HD Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD5800_HDA	0xaa50		/* Radeon HD 5800 Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD5700_HDA	0xaa58		/* Radeon HD 5700 Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD5600_HDA	0xaa60		/* Radeon HD 5600 Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD5470_HDA	0xaa68		/* Radeon HD 5470 Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD6670_HDA	0xaa90		/* Radeon HD 6670 Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD6400_HDA	0xaa98		/* Radeon HD 6400 Audio */
#define	PCI_PRODUCT_ATI_RADEON_HD7700_HDA	0xaab0		/* Radeon HD 7700 Audio */
#define	PCI_PRODUCT_ATI_RADEON_PRO_HDA	0xaae0		/* Radeon Pro Audio */
#define	PCI_PRODUCT_ATI_VEGA10_HDA_1	0xaaf8		/* Radeon Rx Vega HD Audio */
#define	PCI_PRODUCT_ATI_NAVI2X_HDA	0xab28		/* Navi 2x HD Audio */
#define	PCI_PRODUCT_ATI_NAVI10_HDA_1	0xab38		/* Navi 10 HD Audio */
#define	PCI_PRODUCT_ATI_RS100_AGP	0xcab0		/* RS100 AGP */
#define	PCI_PRODUCT_ATI_RS200_AGP	0xcab2		/* RS200 AGP */
#define	PCI_PRODUCT_ATI_RS250_AGP	0xcab3		/* RS250 AGP */
#define	PCI_PRODUCT_ATI_RS200M_AGP	0xcbb2		/* RS200M AGP */

/* Atmel products */
#define	PCI_PRODUCT_ATMEL_AT76C506	0x0506		/* AT76C506 */

/* Advanced Telecommunications Modules */
#define	PCI_PRODUCT_ATML_WAIKATO	0x3200		/* Waikato Dag3.2 */
#define	PCI_PRODUCT_ATML_DAG35	0x3500		/* Endace Dag3.5 */
#define	PCI_PRODUCT_ATML_DAG422GE	0x422e		/* Endace Dag4.22GE */
#define	PCI_PRODUCT_ATML_DAG423	0x4230		/* Endace Dag4.23 */

/* Atronics products */
#define	PCI_PRODUCT_ATRONICS_IDE_2015PL	0x2015		/* IDE-2015PL */

/* Attansic Technology products */
#define	PCI_PRODUCT_ATTANSIC_L1E	0x1026		/* L1E */
#define	PCI_PRODUCT_ATTANSIC_L1	0x1048		/* L1 */
#define	PCI_PRODUCT_ATTANSIC_L2C	0x1062		/* L2C */
#define	PCI_PRODUCT_ATTANSIC_L1C	0x1063		/* L1C */
#define	PCI_PRODUCT_ATTANSIC_L1D	0x1073		/* L1D */
#define	PCI_PRODUCT_ATTANSIC_L1D_1	0x1083		/* L1D */
#define	PCI_PRODUCT_ATTANSIC_AR8162	0x1090		/* AR8162 */
#define	PCI_PRODUCT_ATTANSIC_AR8161	0x1091		/* AR8161 */
#define	PCI_PRODUCT_ATTANSIC_AR8172	0x10a0		/* AR8172 */
#define	PCI_PRODUCT_ATTANSIC_AR8171	0x10a1		/* AR8171 */
#define	PCI_PRODUCT_ATTANSIC_L2	0x2048		/* L2 */
#define	PCI_PRODUCT_ATTANSIC_L2C_1	0x2060		/* L2C */
#define	PCI_PRODUCT_ATTANSIC_L2C_2	0x2062		/* L2C */
#define	PCI_PRODUCT_ATTANSIC_E2200	0xe091		/* E2200 */
#define	PCI_PRODUCT_ATTANSIC_E2400	0xe0a1		/* E2400 */
#define	PCI_PRODUCT_ATTANSIC_E2500	0xe0b1		/* E2500 */

/* Aureal products */
#define	PCI_PRODUCT_AUREAL_AU8820	0x0001		/* Vortex 1 */
#define	PCI_PRODUCT_AUREAL_AU8830	0x0002		/* Vortex 2 */
#define	PCI_PRODUCT_AUREAL_AU8810	0x0003		/* Vortex Advantage */

/* Avance Logic products */
#define	PCI_PRODUCT_AVANCE_AVL2301	0x2301		/* AVL2301 */
#define	PCI_PRODUCT_AVANCE_AVG2302	0x2302		/* AVG2302 */
#define	PCI_PRODUCT_AVANCE2_ALG2301	0x2301		/* ALG2301 */
#define	PCI_PRODUCT_AVANCE2_ALG2302	0x2302		/* ALG2302 */
#define	PCI_PRODUCT_AVANCE2_ALS4000	0x4000		/* ALS4000 */

/* AVlab products */
#define	PCI_PRODUCT_AVLAB_PCI2S	0x2130		/* Dual Serial */
#define	PCI_PRODUCT_AVLAB_LPPCI4S	0x2150		/* Quad Serial */
#define	PCI_PRODUCT_AVLAB_LPPCI4S_2	0x2152		/* Quad Serial */

/* AVM products */
#define	PCI_PRODUCT_AVM_B1	0x0700		/* BRI ISDN */
#define	PCI_PRODUCT_AVM_FRITZ_CARD	0x0a00		/* Fritz ISDN */
#define	PCI_PRODUCT_AVM_FRITZ_PCI_V2_ISDN	0x0e00		/* Fritz v2.0 ISDN */
#define	PCI_PRODUCT_AVM_T1	0x1200		/* PRI T1 ISDN */

/* AWT products */
#define	PCI_PRODUCT_AWT_RT2890	0x1059		/* RT2890 */

/* Baikal products */
#define	PCI_PRODUCT_BAIKAL_BE_M1000	0x8060		/* BE-M1000 */

/* Belkin Components products */
#define	PCI_PRODUCT_BELKIN_F5D6000	0xec00		/* F5D6000 */
#define	PCI_PRODUCT_BELKIN2_F5D6001	0x6001		/* F5D6001 */
#define	PCI_PRODUCT_BELKIN2_F5D6020V3	0x6020		/* F5D6020V3 */
#define	PCI_PRODUCT_BELKIN2_F5D7010	0x701f		/* F5D7010 */

/* Bit3 products */
#define	PCI_PRODUCT_BIT3_PCIVME617	0x0001		/* VME 617 */
#define	PCI_PRODUCT_BIT3_PCIVME2706	0x0300		/* VME 2706 */

/* Bluesteel Networks */
#define	PCI_PRODUCT_BLUESTEEL_5501	0x0000		/* 5501 */
#define	PCI_PRODUCT_BLUESTEEL_5601	0x5601		/* 5601 */

/* Bochs */
#define	PCI_PRODUCT_BOCHS_VGA	0x1111		/* VGA */

/* Brainboxes */
#define	PCI_PRODUCT_BRAINBOXES_IS200_BB16PCI958	0x0d80		/* IS200 BB16PCI958 */

/* Broadcom */
#define	PCI_PRODUCT_BROADCOM_BCM43224_1	0x0576		/* BCM43224 */
#define	PCI_PRODUCT_BROADCOM_BCM15700A2	0x1570		/* BCM15700A2 */
#define	PCI_PRODUCT_BROADCOM_BCM5752	0x1600		/* BCM5752 */
#define	PCI_PRODUCT_BROADCOM_BCM5752M	0x1601		/* BCM5752M */
#define	PCI_PRODUCT_BROADCOM_BCM5709	0x1639		/* BCM5709 */
#define	PCI_PRODUCT_BROADCOM_BCM5709S	0x163a		/* BCM5709S */
#define	PCI_PRODUCT_BROADCOM_BCM5716	0x163b		/* BCM5716 */
#define	PCI_PRODUCT_BROADCOM_BCM5716S	0x163c		/* BCM5716S */
#define	PCI_PRODUCT_BROADCOM_BCM57811	0x163d		/* BCM57811 */
#define	PCI_PRODUCT_BROADCOM_BCM57811_MF	0x163e		/* BCM57811 MF */
#define	PCI_PRODUCT_BROADCOM_BCM57811_VF	0x163f		/* BCM57811 VF */
#define	PCI_PRODUCT_BROADCOM_BCM57787	0x1641		/* BCM57787 */
#define	PCI_PRODUCT_BROADCOM_BCM57764	0x1642		/* BCM57764 */
#define	PCI_PRODUCT_BROADCOM_BCM5725	0x1643		/* BCM5725 */
#define	PCI_PRODUCT_BROADCOM_BCM5700	0x1644		/* BCM5700 */
#define	PCI_PRODUCT_BROADCOM_BCM5701	0x1645		/* BCM5701 */
#define	PCI_PRODUCT_BROADCOM_BCM5702	0x1646		/* BCM5702 */
#define	PCI_PRODUCT_BROADCOM_BCM5703	0x1647		/* BCM5703 */
#define	PCI_PRODUCT_BROADCOM_BCM5704C	0x1648		/* BCM5704C */
#define	PCI_PRODUCT_BROADCOM_BCM5704S_ALT	0x1649		/* BCM5704S Alt */
#define	PCI_PRODUCT_BROADCOM_BCM5706	0x164a		/* BCM5706 */
#define	PCI_PRODUCT_BROADCOM_BCM5708	0x164c		/* BCM5708 */
#define	PCI_PRODUCT_BROADCOM_BCM5702FE	0x164d		/* BCM5702FE */
#define	PCI_PRODUCT_BROADCOM_BCM57710	0x164e		/* BCM57710 */
#define	PCI_PRODUCT_BROADCOM_BCM57711	0x164f		/* BCM57711 */
#define	PCI_PRODUCT_BROADCOM_BCM57711E	0x1650		/* BCM57711E */
#define	PCI_PRODUCT_BROADCOM_BCM5705	0x1653		/* BCM5705 */
#define	PCI_PRODUCT_BROADCOM_BCM5705K	0x1654		/* BCM5705K */
#define	PCI_PRODUCT_BROADCOM_BCM5717	0x1655		/* BCM5717 */
#define	PCI_PRODUCT_BROADCOM_BCM5718	0x1656		/* BCM5718 */
#define	PCI_PRODUCT_BROADCOM_BCM5719	0x1657		/* BCM5719 */
#define	PCI_PRODUCT_BROADCOM_BCM5721	0x1659		/* BCM5721 */
#define	PCI_PRODUCT_BROADCOM_BCM5722	0x165a		/* BCM5722 */
#define	PCI_PRODUCT_BROADCOM_BCM5723	0x165b		/* BCM5723 */
#define	PCI_PRODUCT_BROADCOM_BCM5705M	0x165d		/* BCM5705M */
#define	PCI_PRODUCT_BROADCOM_BCM5705M_ALT	0x165e		/* BCM5705M Alt */
#define	PCI_PRODUCT_BROADCOM_BCM5720	0x165f		/* BCM5720 */
#define	PCI_PRODUCT_BROADCOM_BCM57712	0x1662		/* BCM57712 */
#define	PCI_PRODUCT_BROADCOM_BCM57712_MF	0x1663		/* BCM57712 MF */
#define	PCI_PRODUCT_BROADCOM_BCM5717C	0x1665		/* BCM5717C */
#define	PCI_PRODUCT_BROADCOM_BCM5714	0x1668		/* BCM5714 */
#define	PCI_PRODUCT_BROADCOM_BCM5714S	0x1669		/* BCM5714S */
#define	PCI_PRODUCT_BROADCOM_BCM5780	0x166a		/* BCM5780 */
#define	PCI_PRODUCT_BROADCOM_BCM5780S	0x166b		/* BCM5780S */
#define	PCI_PRODUCT_BROADCOM_BCM5705F	0x166e		/* BCM5705F */
#define	PCI_PRODUCT_BROADCOM_BCM57712_VF	0x166f		/* BCM57712 VF */
#define	PCI_PRODUCT_BROADCOM_BCM5754M	0x1672		/* BCM5754M */
#define	PCI_PRODUCT_BROADCOM_BCM5755M	0x1673		/* BCM5755M */
#define	PCI_PRODUCT_BROADCOM_BCM5756	0x1674		/* BCM5756 */
#define	PCI_PRODUCT_BROADCOM_BCM5751	0x1677		/* BCM5751 */
#define	PCI_PRODUCT_BROADCOM_BCM5715	0x1678		/* BCM5715 */
#define	PCI_PRODUCT_BROADCOM_BCM5715S	0x1679		/* BCM5715S */
#define	PCI_PRODUCT_BROADCOM_BCM5754	0x167a		/* BCM5754 */
#define	PCI_PRODUCT_BROADCOM_BCM5755	0x167b		/* BCM5755 */
#define	PCI_PRODUCT_BROADCOM_BCM5751M	0x167d		/* BCM5751M */
#define	PCI_PRODUCT_BROADCOM_BCM5751F	0x167e		/* BCM5751F */
#define	PCI_PRODUCT_BROADCOM_BCM5787F	0x167f		/* BCM5787F */
#define	PCI_PRODUCT_BROADCOM_BCM5761E	0x1680		/* BCM5761E */
#define	PCI_PRODUCT_BROADCOM_BCM5761	0x1681		/* BCM5761 */
#define	PCI_PRODUCT_BROADCOM_BCM57762	0x1682		/* BCM57762 */
#define	PCI_PRODUCT_BROADCOM_BCM57767	0x1683		/* BCM57767 */
#define	PCI_PRODUCT_BROADCOM_BCM5764	0x1684		/* BCM5764 */
#define	PCI_PRODUCT_BROADCOM_BCM57766	0x1686		/* BCM57766 */
#define	PCI_PRODUCT_BROADCOM_BCM5762	0x1687		/* BCM5762 */
#define	PCI_PRODUCT_BROADCOM_BCM5761S	0x1688		/* BCM5761S */
#define	PCI_PRODUCT_BROADCOM_BCM5761SE	0x1689		/* BCM5761SE */
#define	PCI_PRODUCT_BROADCOM_BCM57800	0x168a		/* BCM57800 */
#define	PCI_PRODUCT_BROADCOM_BCM57840_OBS	0x168d		/* BCM57840 OBS */
#define	PCI_PRODUCT_BROADCOM_BCM57810	0x168e		/* BCM57810 */
#define	PCI_PRODUCT_BROADCOM_BCM57760	0x1690		/* BCM57760 */
#define	PCI_PRODUCT_BROADCOM_BCM57788	0x1691		/* BCM57788 */
#define	PCI_PRODUCT_BROADCOM_BCM57780	0x1692		/* BCM57780 */
#define	PCI_PRODUCT_BROADCOM_BCM5787M	0x1693		/* BCM5787M */
#define	PCI_PRODUCT_BROADCOM_BCM57790	0x1694		/* BCM57790 */
#define	PCI_PRODUCT_BROADCOM_BCM5782	0x1696		/* BCM5782 */
#define	PCI_PRODUCT_BROADCOM_BCM5784	0x1698		/* BCM5784 */
#define	PCI_PRODUCT_BROADCOM_BCM5785G	0x1699		/* BCM5785G */
#define	PCI_PRODUCT_BROADCOM_BCM5786	0x169a		/* BCM5786 */
#define	PCI_PRODUCT_BROADCOM_BCM5787	0x169b		/* BCM5787 */
#define	PCI_PRODUCT_BROADCOM_BCM5788	0x169c		/* BCM5788 */
#define	PCI_PRODUCT_BROADCOM_BCM5789	0x169d		/* BCM5789 */
#define	PCI_PRODUCT_BROADCOM_BCM5785F	0x16a0		/* BCM5785F */
#define	PCI_PRODUCT_BROADCOM_BCM57840_4_10	0x16a1		/* BCM57840 */
#define	PCI_PRODUCT_BROADCOM_BCM57840_2_20	0x16a2		/* BCM57840 */
#define	PCI_PRODUCT_BROADCOM_BCM57840_MF	0x16a4		/* BCM57840 MF */
#define	PCI_PRODUCT_BROADCOM_BCM57800_MF	0x16a5		/* BCM57800 MF */
#define	PCI_PRODUCT_BROADCOM_BCM5702X	0x16a6		/* BCM5702X */
#define	PCI_PRODUCT_BROADCOM_BCM5703X	0x16a7		/* BCM5703X */
#define	PCI_PRODUCT_BROADCOM_BCM5704S	0x16a8		/* BCM5704S */
#define	PCI_PRODUCT_BROADCOM_BCM57800_VF	0x16a9		/* BCM57800 VF */
#define	PCI_PRODUCT_BROADCOM_BCM5706S	0x16aa		/* BCM5706S */
#define	PCI_PRODUCT_BROADCOM_BCM57840_OBS_MF	0x16ab		/* BCM57840 OBS MF */
#define	PCI_PRODUCT_BROADCOM_BCM5708S	0x16ac		/* BCM5708S */
#define	PCI_PRODUCT_BROADCOM_BCM57840_VF	0x16ad		/* BCM57840 VF */
#define	PCI_PRODUCT_BROADCOM_BCM57810_MF	0x16ae		/* BCM57810 MF */
#define	PCI_PRODUCT_BROADCOM_BCM57810_VF	0x16af		/* BCM57810 VF */
#define	PCI_PRODUCT_BROADCOM_BCM57761	0x16b0		/* BCM57761 */
#define	PCI_PRODUCT_BROADCOM_BCM57781	0x16b1		/* BCM57781 */
#define	PCI_PRODUCT_BROADCOM_BCM57791	0x16b2		/* BCM57791 */
#define	PCI_PRODUCT_BROADCOM_BCM57786	0x16b3		/* BCM57786 */
#define	PCI_PRODUCT_BROADCOM_BCM57765	0x16b4		/* BCM57765 */
#define	PCI_PRODUCT_BROADCOM_BCM57785	0x16b5		/* BCM57785 */
#define	PCI_PRODUCT_BROADCOM_BCM57795	0x16b6		/* BCM57795 */
#define	PCI_PRODUCT_BROADCOM_BCM57782	0x16b7		/* BCM57782 */
#define	PCI_PRODUCT_BROADCOM_SD	0x16bc		/* SD Host Controller */
#define	PCI_PRODUCT_BROADCOM_BCM5702_ALT	0x16c6		/* BCM5702 Alt */
#define	PCI_PRODUCT_BROADCOM_BCM5703_ALT	0x16c7		/* BCM5703 Alt */
#define	PCI_PRODUCT_BROADCOM_BCM57301	0x16c8		/* BCM57301 */
#define	PCI_PRODUCT_BROADCOM_BCM57302	0x16c9		/* BCM57302 */
#define	PCI_PRODUCT_BROADCOM_BCM57304	0x16ca		/* BCM57304 */
#define	PCI_PRODUCT_BROADCOM_BCM57311	0x16ce		/* BCM57311 */
#define	PCI_PRODUCT_BROADCOM_BCM57312	0x16cf		/* BCM57312 */
#define	PCI_PRODUCT_BROADCOM_BCM57402	0x16d0		/* BCM57402 */
#define	PCI_PRODUCT_BROADCOM_BCM57404	0x16d1		/* BCM57404 */
#define	PCI_PRODUCT_BROADCOM_BCM57406	0x16d2		/* BCM57406 */
#define	PCI_PRODUCT_BROADCOM_BCM57407	0x16d5		/* BCM57407 */
#define	PCI_PRODUCT_BROADCOM_BCM57412	0x16d6		/* BCM57412 */
#define	PCI_PRODUCT_BROADCOM_BCM57414	0x16d7		/* BCM57414 */
#define	PCI_PRODUCT_BROADCOM_BCM57416	0x16d8		/* BCM57416 */
#define	PCI_PRODUCT_BROADCOM_BCM57417	0x16d8		/* BCM57417 */
#define	PCI_PRODUCT_BROADCOM_BCM5781	0x16dd		/* BCM5781 */
#define	PCI_PRODUCT_BROADCOM_BCM57314	0x16df		/* BCM57314 */
#define	PCI_PRODUCT_BROADCOM_BCM57417_SFP	0x16e2		/* BCM57417 SFP */
#define	PCI_PRODUCT_BROADCOM_BCM57416_SFP	0x16e3		/* BCM57416 SFP */
#define	PCI_PRODUCT_BROADCOM_BCM57407_SFP	0x16e9		/* BCM57407 SFP */
#define	PCI_PRODUCT_BROADCOM_BCM5727	0x16f3		/* BCM5727 */
#define	PCI_PRODUCT_BROADCOM_BCM5753	0x16f7		/* BCM5753 */
#define	PCI_PRODUCT_BROADCOM_BCM5753M	0x16fd		/* BCM5753M */
#define	PCI_PRODUCT_BROADCOM_BCM5753F	0x16fe		/* BCM5753F */
#define	PCI_PRODUCT_BROADCOM_BCM5903M	0x16ff		/* BCM5903M */
#define	PCI_PRODUCT_BROADCOM_BCM4401B1	0x170c		/* BCM4401B1 */
#define	PCI_PRODUCT_BROADCOM_BCM5901	0x170d		/* BCM5901 */
#define	PCI_PRODUCT_BROADCOM_BCM5901A2	0x170e		/* BCM5901A2 */
#define	PCI_PRODUCT_BROADCOM_BCM5903F	0x170f		/* BCM5903F */
#define	PCI_PRODUCT_BROADCOM_BCM5906	0x1712		/* BCM5906 */
#define	PCI_PRODUCT_BROADCOM_BCM5906M	0x1713		/* BCM5906M */
#define	PCI_PRODUCT_BROADCOM_BCM2711	0x2711		/* BCM2711 */
#define	PCI_PRODUCT_BROADCOM_BCM2712	0x2712		/* BCM2712 */
#define	PCI_PRODUCT_BROADCOM_BCM4303	0x4301		/* BCM4303 */
#define	PCI_PRODUCT_BROADCOM_BCM4307	0x4307		/* BCM4307 */
#define	PCI_PRODUCT_BROADCOM_BCM4311	0x4311		/* BCM4311 */
#define	PCI_PRODUCT_BROADCOM_BCM4312	0x4312		/* BCM4312 */
#define	PCI_PRODUCT_BROADCOM_BCM4315	0x4315		/* BCM4315 */
#define	PCI_PRODUCT_BROADCOM_BCM4318	0x4318		/* BCM4318 */
#define	PCI_PRODUCT_BROADCOM_BCM4319	0x4319		/* BCM4319 */
#define	PCI_PRODUCT_BROADCOM_BCM4306	0x4320		/* BCM4306 */
#define	PCI_PRODUCT_BROADCOM_BCM4306_2	0x4321		/* BCM4306 */
#define	PCI_PRODUCT_BROADCOM_SERIAL_2	0x4322		/* Serial */
#define	PCI_PRODUCT_BROADCOM_BCM4309	0x4324		/* BCM4309 */
#define	PCI_PRODUCT_BROADCOM_BCM43XG	0x4325		/* BCM43XG */
#define	PCI_PRODUCT_BROADCOM_BCM4321	0x4328		/* BCM4321 */
#define	PCI_PRODUCT_BROADCOM_BCM4321_2	0x4329		/* BCM4321 */
#define	PCI_PRODUCT_BROADCOM_BCM4322	0x432b		/* BCM4322 */
#define	PCI_PRODUCT_BROADCOM_BCM4331	0x4331		/* BCM4331 */
#define	PCI_PRODUCT_BROADCOM_SERIAL	0x4333		/* Serial */
#define	PCI_PRODUCT_BROADCOM_SERIAL_GC	0x4344		/* Serial */
#define	PCI_PRODUCT_BROADCOM_BCM43224	0x4353		/* BCM43224 */
#define	PCI_PRODUCT_BROADCOM_BCM43225	0x4357		/* BCM43225 */
#define	PCI_PRODUCT_BROADCOM_BCM43227	0x4358		/* BCM43227 */
#define	PCI_PRODUCT_BROADCOM_BCM4360	0x43a0		/* BCM4360 */
#define	PCI_PRODUCT_BROADCOM_BCM4350	0x43a3		/* BCM4350 */
#define	PCI_PRODUCT_BROADCOM_BCM43602	0x43ba		/* BCM43602 */
#define	PCI_PRODUCT_BROADCOM_BCM4356	0x43ec		/* BCM4356 */
#define	PCI_PRODUCT_BROADCOM_BCM4401	0x4401		/* BCM4401 */
#define	PCI_PRODUCT_BROADCOM_BCM4401B0	0x4402		/* BCM4401B0 */
#define	PCI_PRODUCT_BROADCOM_BCM4371	0x440d		/* BCM4371 */
#define	PCI_PRODUCT_BROADCOM_BCM4378	0x4425		/* BCM4378 */
#define	PCI_PRODUCT_BROADCOM_BCM4387	0x4433		/* BCM4387 */
#define	PCI_PRODUCT_BROADCOM_BCM4388	0x4434		/* BCM4388 */
#define	PCI_PRODUCT_BROADCOM_BCM4313	0x4727		/* BCM4313 */
#define	PCI_PRODUCT_BROADCOM_5801	0x5801		/* 5801 */
#define	PCI_PRODUCT_BROADCOM_5802	0x5802		/* 5802 */
#define	PCI_PRODUCT_BROADCOM_5805	0x5805		/* 5805 */
#define	PCI_PRODUCT_BROADCOM_5820	0x5820		/* 5820 */
#define	PCI_PRODUCT_BROADCOM_5821	0x5821		/* 5821 */
#define	PCI_PRODUCT_BROADCOM_5822	0x5822		/* 5822 */
#define	PCI_PRODUCT_BROADCOM_5823	0x5823		/* 5823 */
#define	PCI_PRODUCT_BROADCOM_5825	0x5825		/* 5825 */
#define	PCI_PRODUCT_BROADCOM_5860	0x5860		/* 5860 */
#define	PCI_PRODUCT_BROADCOM_5861	0x5861		/* 5861 */
#define	PCI_PRODUCT_BROADCOM_5862	0x5862		/* 5862 */

/* Brocade products */
#define	PCI_PRODUCT_BROCADE_X2XFC	0x0013		/* 425/825/42B/82B */
#define	PCI_PRODUCT_BROCADE_1XXXCNA	0x0014		/* 1010/1020/1007/1741 */
#define	PCI_PRODUCT_BROCADE_X1XFC	0x0017		/* 415/815/41B/81B */
#define	PCI_PRODUCT_BROCADE_804	0x0021		/* 804 */
#define	PCI_PRODUCT_BROCADE_1860	0x0022		/* 1860 */
#define	PCI_PRODUCT_BROCADE_4X0FC	0x0646		/* 410/420 */

/* Brooktree products */
#define	PCI_PRODUCT_BROOKTREE_BT848	0x0350		/* BT848 */
#define	PCI_PRODUCT_BROOKTREE_BT849	0x0351		/* BT849 */
#define	PCI_PRODUCT_BROOKTREE_BT878	0x036e		/* BT878 */
#define	PCI_PRODUCT_BROOKTREE_BT879	0x036f		/* BT879 */
#define	PCI_PRODUCT_BROOKTREE_BT878_AU	0x0878		/* BT878 Audio */
#define	PCI_PRODUCT_BROOKTREE_BT879_AU	0x0879		/* BT879 Audio */
#define	PCI_PRODUCT_BROOKTREE_BT8474	0x8474		/* Bt8474 HDLC */

/* BusLogic products */
#define	PCI_PRODUCT_BUSLOGIC_MULTIMASTER_NC	0x0140		/* MultiMaster NC */
#define	PCI_PRODUCT_BUSLOGIC_MULTIMASTER	0x1040		/* MultiMaster */
#define	PCI_PRODUCT_BUSLOGIC_FLASHPOINT	0x8130		/* FlashPoint */

/* c't Magazin products */
#define	PCI_PRODUCT_C4T_GPPCI	0x6773		/* GPPCI */

/* Cavium products */
#define	PCI_PRODUCT_CAVIUM_NITROX	0x0001		/* NITROX XL */
#define	PCI_PRODUCT_CAVIUM_NITROX_LITE	0x0003		/* NITROX Lite */
#define	PCI_PRODUCT_CAVIUM_NITROX_PX	0x0010		/* NITROX PX */
#define	PCI_PRODUCT_CAVIUM_OCTEON_CN50XX	0x0070		/* OCTEON Plus CN50XX */

/* CCUBE products */
#define	PCI_PRODUCT_CCUBE_CINEMASTER	0x8888		/* Cinemaster */

/* Chelsio products */
#define	PCI_PRODUCT_CHELSIO_NX10	0x0006		/* Nx10 10GbE */
#define	PCI_PRODUCT_CHELSIO_PE9000	0x0020		/* PE9000 10GbE */
#define	PCI_PRODUCT_CHELSIO_T302E	0x0021		/* T302E 10GbE */
#define	PCI_PRODUCT_CHELSIO_T310E	0x0022		/* T310E 10GbE */
#define	PCI_PRODUCT_CHELSIO_T320X	0x0023		/* T320X 10GbE */
#define	PCI_PRODUCT_CHELSIO_T302X	0x0024		/* T302X 10GbE */
#define	PCI_PRODUCT_CHELSIO_T320E	0x0025		/* T320E 10GbE */
#define	PCI_PRODUCT_CHELSIO_T310X	0x0026		/* T310X 10GbE */
#define	PCI_PRODUCT_CHELSIO_T3B10	0x0030		/* T3B10 10GbE */
#define	PCI_PRODUCT_CHELSIO_T3B20	0x0031		/* T3B20 10GbE */
#define	PCI_PRODUCT_CHELSIO_T3B02	0x0032		/* T3B02 10GbE */

/* Chips and Technologies products */
#define	PCI_PRODUCT_CHIPS_64310	0x00b8		/* 64310 */
#define	PCI_PRODUCT_CHIPS_69000	0x00c0		/* 69000 */
#define	PCI_PRODUCT_CHIPS_65545	0x00d8		/* 65545 */
#define	PCI_PRODUCT_CHIPS_65548	0x00dc		/* 65548 */
#define	PCI_PRODUCT_CHIPS_65550	0x00e0		/* 65550 */
#define	PCI_PRODUCT_CHIPS_65554	0x00e4		/* 65554 */
#define	PCI_PRODUCT_CHIPS_65555	0x00e5		/* 65555 */
#define	PCI_PRODUCT_CHIPS_68554	0x00f4		/* 68554 */
#define	PCI_PRODUCT_CHIPS_69030	0x0c30		/* 69030 */

/* Cirrus Logic products */
#define	PCI_PRODUCT_CIRRUS_CL_GD7548	0x0038		/* CL-GD7548 */
#define	PCI_PRODUCT_CIRRUS_CL_GD5430	0x00a0		/* CL-GD5430 */
#define	PCI_PRODUCT_CIRRUS_CL_GD5434_4	0x00a4		/* CL-GD5434-4 */
#define	PCI_PRODUCT_CIRRUS_CL_GD5434_8	0x00a8		/* CL-GD5434-8 */
#define	PCI_PRODUCT_CIRRUS_CL_GD5436	0x00ac		/* CL-GD5436 */
#define	PCI_PRODUCT_CIRRUS_CL_GD5446	0x00b8		/* CL-GD5446 */
#define	PCI_PRODUCT_CIRRUS_CL_GD5480	0x00bc		/* CL-GD5480 */
#define	PCI_PRODUCT_CIRRUS_CL_GD5462	0x00d0		/* CL-GD5462 */
#define	PCI_PRODUCT_CIRRUS_CL_GD5464	0x00d4		/* CL-GD5464 */
#define	PCI_PRODUCT_CIRRUS_CL_GD5465	0x00d6		/* CL-GD5465 */
#define	PCI_PRODUCT_CIRRUS_CL_PD6729	0x1100		/* CL-PD6729 */
#define	PCI_PRODUCT_CIRRUS_CL_PD6832	0x1110		/* CL-PD6832 CardBus */
#define	PCI_PRODUCT_CIRRUS_CL_PD6833	0x1113		/* CL-PD6833 CardBus */
#define	PCI_PRODUCT_CIRRUS_CL_GD7542	0x1200		/* CL-GD7542 */
#define	PCI_PRODUCT_CIRRUS_CL_GD7543	0x1202		/* CL-GD7543 */
#define	PCI_PRODUCT_CIRRUS_CL_GD7541	0x1204		/* CL-GD7541 */
#define	PCI_PRODUCT_CIRRUS_CS4610	0x6001		/* CS4610 SoundFusion */
#define	PCI_PRODUCT_CIRRUS_CS4280	0x6003		/* CS4280/46xx CrystalClear */
#define	PCI_PRODUCT_CIRRUS_CS4615	0x6004		/* CS4615 */
#define	PCI_PRODUCT_CIRRUS_CS4281	0x6005		/* CS4281 CrystalClear */

/* Cisco products */
#define	PCI_PRODUCT_CISCO_VIC_PCIE_1	0x0023		/* VIC PCIE */
#define	PCI_PRODUCT_CISCO_VIC_PCIE_2	0x0040		/* VIC PCIE */
#define	PCI_PRODUCT_CISCO_VIC_PCIE_3	0x0041		/* VIC PCIE */
#define	PCI_PRODUCT_CISCO_VIC_MGMT	0x0042		/* VIC Management */
#define	PCI_PRODUCT_CISCO_VIC_ETH	0x0043		/* VIC Ethernet */
#define	PCI_PRODUCT_CISCO_VIC_FCOE	0x0045		/* VIC FCoE */

/* CMD Technology products -- info gleaned from www.cmd.com */
/* Fake product id for SiI3112 found on Adaptec 1210SA */
#define	PCI_PRODUCT_CMDTECH_AAR_1210SA	0x0240		/* AAR-1210SA */
/* Adaptec 1220SA is really a 3132 also */
#define	PCI_PRODUCT_CMDTECH_AAR_1220SA	0x0242		/* AAR-1220SA */
#define	PCI_PRODUCT_CMDTECH_AAR_1225SA	0x0244		/* AAR-1225SA */
#define	PCI_PRODUCT_CMDTECH_640	0x0640		/* PCI0640 */
#define	PCI_PRODUCT_CMDTECH_642	0x0642		/* PCI0642 */
#define	PCI_PRODUCT_CMDTECH_643	0x0643		/* PCI0643 */
#define	PCI_PRODUCT_CMDTECH_646	0x0646		/* PCI0646 */
#define	PCI_PRODUCT_CMDTECH_647	0x0647		/* PCI0647 */
#define	PCI_PRODUCT_CMDTECH_648	0x0648		/* PCI0648 */
#define	PCI_PRODUCT_CMDTECH_649	0x0649		/* PCI0649 */
/* Inclusion of 'A' in the following entry is probably wrong. */
/* No data on the CMD Tech. web site for the following as of Mar. 3 '98 */
#define	PCI_PRODUCT_CMDTECH_650A	0x0650		/* PCI0650A */
#define	PCI_PRODUCT_CMDTECH_670	0x0670		/* USB0670 */
#define	PCI_PRODUCT_CMDTECH_673	0x0673		/* USB0673 */
#define	PCI_PRODUCT_CMDTECH_680	0x0680		/* PCI0680 */
#define	PCI_PRODUCT_CMDTECH_3112	0x3112		/* SiI3112 SATA */
#define	PCI_PRODUCT_CMDTECH_3114	0x3114		/* SiI3114 SATA */
#define	PCI_PRODUCT_CMDTECH_3124	0x3124		/* SiI3124 SATA */
#define	PCI_PRODUCT_CMDTECH_3131	0x3131		/* SiI3131 SATA */
#define	PCI_PRODUCT_CMDTECH_3132	0x3132		/* SiI3132 SATA */
#define	PCI_PRODUCT_CMDTECH_3512	0x3512		/* SiI3512 SATA */
#define	PCI_PRODUCT_CMDTECH_3531	0x3531		/* SiI3531 SATA */

/* C-Media Electronics */
#define	PCI_PRODUCT_CMI_CMI8338A	0x0100		/* CMI8338A Audio */
#define	PCI_PRODUCT_CMI_CMI8338B	0x0101		/* CMI8338B Audio */
#define	PCI_PRODUCT_CMI_CMI8738	0x0111		/* CMI8738/C3DX Audio */
#define	PCI_PRODUCT_CMI_CMI8738B	0x0112		/* CMI8738B Audio */
#define	PCI_PRODUCT_CMI_HSP56	0x0211		/* HSP56 AMR */
#define	PCI_PRODUCT_CMI_CMI8788	0x8788		/* CMI8788 HD Audio */

/* CNet products */
#define	PCI_PRODUCT_CNET_GIGACARD	0x434e		/* GigaCard */

/* Cogent Data Technologies products */
#define	PCI_PRODUCT_COGENT_EM110TX	0x1400		/* EX110TX */

/* Compaq products */
#define	PCI_PRODUCT_COMPAQ_PCI_EISA_BRIDGE	0x0001		/* EISA */
#define	PCI_PRODUCT_COMPAQ_PCI_ISA_BRIDGE	0x0002		/* ISA */
#define	PCI_PRODUCT_COMPAQ_CSA64XX	0x0046		/* Smart Array 64xx */
#define	PCI_PRODUCT_COMPAQ_TRIFLEX1	0x1000		/* Triflex PCI */
#define	PCI_PRODUCT_COMPAQ_TRIFLEX2	0x2000		/* Triflex PCI */
#define	PCI_PRODUCT_COMPAQ_QVISION_V0	0x3032		/* QVision */
#define	PCI_PRODUCT_COMPAQ_QVISION_1280P	0x3033		/* QVision 1280/p */
#define	PCI_PRODUCT_COMPAQ_QVISION_V2	0x3034		/* QVision */
#define	PCI_PRODUCT_COMPAQ_TRIFLEX4	0x4000		/* Triflex PCI */
#define	PCI_PRODUCT_COMPAQ_CSA5300	0x4070		/* Smart Array 5300 */
#define	PCI_PRODUCT_COMPAQ_CSA5I	0x4080		/* Smart Array 5i */
#define	PCI_PRODUCT_COMPAQ_CSA532	0x4082		/* Smart Array 532 */
#define	PCI_PRODUCT_COMPAQ_CSA5312	0x4083		/* Smart Array 5312 */
#define	PCI_PRODUCT_COMPAQ_CSA6I	0x4091		/* Smart Array 6i */
#define	PCI_PRODUCT_COMPAQ_CSA641	0x409a		/* Smart Array 641 */
#define	PCI_PRODUCT_COMPAQ_CSA642	0x409b		/* Smart Array 642 */
#define	PCI_PRODUCT_COMPAQ_CSA6400	0x409c		/* Smart Array 6400 */
#define	PCI_PRODUCT_COMPAQ_CSA6400EM	0x409d		/* Smart Array 6400 EM */
#define	PCI_PRODUCT_COMPAQ_CSA6422	0x409e		/* Smart Array 6422 */
#define	PCI_PRODUCT_COMPAQ_HOTPLUG_PCI	0x6010		/* Hotplug PCI */
#define	PCI_PRODUCT_COMPAQ_USB	0x7020		/* USB */
#define	PCI_PRODUCT_COMPAQ_FXP	0xa0f0		/* Netelligent ASMC */
#define	PCI_PRODUCT_COMPAQ_PCI_ISA_BRIDGE1	0xa0f3		/* ISA */
#define	PCI_PRODUCT_COMPAQ_PCI_HOTPLUG	0xa0f7		/* PCI Hotplug */
#define	PCI_PRODUCT_COMPAQ_OHCI	0xa0f8		/* USB OpenHost */
#define	PCI_PRODUCT_COMPAQ_SMART2P	0xae10		/* SMART2P RAID */
#define	PCI_PRODUCT_COMPAQ_PCI_ISA_BRIDGE3	0xae29		/* ISA */
#define	PCI_PRODUCT_COMPAQ_PCI_ISAPNP	0xae2b		/* ISAPnP */
#define	PCI_PRODUCT_COMPAQ_N100TX	0xae32		/* Netelligent */
#define	PCI_PRODUCT_COMPAQ_IDE	0xae33		/* Netelligent IDE */
#define	PCI_PRODUCT_COMPAQ_N10T	0xae34		/* Netelligent 10 T */
#define	PCI_PRODUCT_COMPAQ_INTNF3P	0xae35		/* Integrated NetFlex 3/P */
#define	PCI_PRODUCT_COMPAQ_DPNET100TX	0xae40		/* DP Netelligent */
#define	PCI_PRODUCT_COMPAQ_INTPL100TX	0xae43		/* ProLiant Netelligent */
#define	PCI_PRODUCT_COMPAQ_PCI_ISA_BRIDGE2	0xae69		/* ISA */
#define	PCI_PRODUCT_COMPAQ_HOST_PCI_BRIDGE1	0xae6c		/* PCI */
#define	PCI_PRODUCT_COMPAQ_HOST_PCI_BRIDGE2	0xae6d		/* PCI */
#define	PCI_PRODUCT_COMPAQ_DP4000	0xb011		/* Embedded Netelligent */
#define	PCI_PRODUCT_COMPAQ_N10T2	0xb012		/* Netelligent 10 T/2 PCI */
#define	PCI_PRODUCT_COMPAQ_N10_TX_UTP	0xb030		/* Netelligent */
#define	PCI_PRODUCT_COMPAQ_CSA5300_2	0xb060		/* Smart Array 5300 rev.2 */
#define	PCI_PRODUCT_COMPAQ_CSA5I_2	0xb178		/* Smart Array 5i/532 rev.2 */
#define	PCI_PRODUCT_COMPAQ_ILO_1	0xb203		/* iLO */
#define	PCI_PRODUCT_COMPAQ_ILO_2	0xb204		/* iLO */
#define	PCI_PRODUCT_COMPAQ_NF3P	0xf130		/* NetFlex 3/P */
#define	PCI_PRODUCT_COMPAQ_NF3P_BNC	0xf150		/* NetFlex 3/PB */

/* Compex */
#define	PCI_PRODUCT_COMPEX_COMPEXE	0x1401		/* Compexe */
#define	PCI_PRODUCT_COMPEX_RL100ATX	0x2011		/* RL100-ATX */
#define	PCI_PRODUCT_COMPEX_98713	0x9881		/* PMAC 98713 */

/* Conexant products */
#define	PCI_PRODUCT_CONEXANT_56K_WINMODEM	0x1033		/* Winmodem */
#define	PCI_PRODUCT_CONEXANT_56K_WINMODEM2	0x1036		/* Winmodem */
#define	PCI_PRODUCT_CONEXANT_RS7112	0x1803		/* RS7112 */
#define	PCI_PRODUCT_CONEXANT_56K_WINMODEM3	0x1804		/* Winmodem */
#define	PCI_PRODUCT_CONEXANT_SOFTK56_PCI	0x2443		/* SoftK56 PCI */
#define	PCI_PRODUCT_CONEXANT_HSF_56K_HSFI	0x2f00		/* HSF 56k HSFi */
#define	PCI_PRODUCT_CONEXANT_MUSYCC8471	0x8471		/* MUSYCC CN8471 */
#define	PCI_PRODUCT_CONEXANT_MUSYCC8472	0x8472		/* MUSYCC CN8472 */
#define	PCI_PRODUCT_CONEXANT_MUSYCC8474	0x8474		/* MUSYCC CN8474 */
#define	PCI_PRODUCT_CONEXANT_MUSYCC8478	0x8478		/* MUSYCC CN8478 */
#define	PCI_PRODUCT_CONEXANT_CX2388X	0x8800		/* CX2388x */
#define	PCI_PRODUCT_CONEXANT_CX2388X_AUDIO	0x8801		/* CX2388x Audio */
#define	PCI_PRODUCT_CONEXANT_CX2388X_MPEG	0x8802		/* CX2388x MPEG */
#define	PCI_PRODUCT_CONEXANT_CX2388X_IR	0x8804		/* CX2388x IR */
#define	PCI_PRODUCT_CONEXANT_CX2388X_AUDIO2	0x8811		/* CX2388x Audio */
#define	PCI_PRODUCT_CONEXANT_CX23885	0x8852		/* CX23885 */

/* Contaq Microsystems products */
#define	PCI_PRODUCT_CONTAQ_82C599	0x0600		/* 82C599 VLB */
#define	PCI_PRODUCT_CONTAQ_82C693	0xc693		/* CY82C693U ISA */

/* Corega products */
#define	PCI_PRODUCT_COREGA_CB_TXD	0xa117		/* FEther CB-TXD */
#define	PCI_PRODUCT_COREGA_2CB_TXD	0xa11e		/* FEther II CB-TXD */
#define	PCI_PRODUCT_COREGA_CGLAPCIGT	0xc107		/* CG-LAPCIGT */
#define	PCI_PRODUCT_COREGA2_RTL8192E_1	0x0044		/* RTL8192E */
#define	PCI_PRODUCT_COREGA2_RTL8190P_1	0x0045		/* RTL8190P */
#define	PCI_PRODUCT_COREGA2_RTL8190P_2	0x0046		/* RTL8190P */
#define	PCI_PRODUCT_COREGA2_RTL8192E_2	0x0047		/* RTL8192E */

/* Corollary products */
#define	PCI_PRODUCT_COROLLARY_CBUSII_PCIB	0x0014		/* C-Bus II-PCI */
#define	PCI_PRODUCT_COROLLARY_CCF	0x1117		/* Cache Coherency Filter */

/* Creative Labs products */
#define	PCI_PRODUCT_CREATIVELABS_SBLIVE	0x0002		/* SoundBlaster Live */
#define	PCI_PRODUCT_CREATIVELABS_AWE64D	0x0003		/* SoundBlaster AWE64D */
#define	PCI_PRODUCT_CREATIVELABS_AUDIGY	0x0004		/* SoundBlaster Audigy */
#define	PCI_PRODUCT_CREATIVELABS_XFI	0x0005		/* SoundBlaster X-Fi */
#define	PCI_PRODUCT_CREATIVELABS_SBLIVE2	0x0006		/* SoundBlaster Live */
#define	PCI_PRODUCT_CREATIVELABS_AUDIGYLS	0x0007		/* SoundBlaster Audigy LS */
#define	PCI_PRODUCT_CREATIVELABS_AUDIGY2	0x0008		/* SoundBlaster Audigy 2 */
#define	PCI_PRODUCT_CREATIVELABS_XFI_XTREME	0x0009		/* SoundBlaster X-Fi Xtreme */
#define	PCI_PRODUCT_CREATIVELABS_FIWIRE	0x4001		/* Firewire */
#define	PCI_PRODUCT_CREATIVELABS_SBJOY	0x7002		/* PCI Gameport Joystick */
#define	PCI_PRODUCT_CREATIVELABS_AUDIGIN	0x7003		/* SoundBlaster Audigy Digital */
#define	PCI_PRODUCT_CREATIVELABS_SBJOY2	0x7004		/* PCI Gameport Joystick */
#define	PCI_PRODUCT_CREATIVELABS_SBJOY3	0x7005		/* PCI Gameport Joystick */
#define	PCI_PRODUCT_CREATIVELABS_PPB	0x7006		/* PCIE-PCI */
#define	PCI_PRODUCT_CREATIVELABS_EV1938	0x8938		/* Ectiva 1938 */

/* Crucial products */
#define	PCI_PRODUCT_CRUCIAL_P5PLUS	0x5407		/* P5 Plus */

/* Cyclades products */
#define	PCI_PRODUCT_CYCLADES_CYCLOMY_1	0x0100		/* Cyclom-Y below 1M */
#define	PCI_PRODUCT_CYCLADES_CYCLOMY_2	0x0101		/* Cyclom-Y */
#define	PCI_PRODUCT_CYCLADES_CYCLOM4Y_1	0x0102		/* Cyclom-4Y below 1M */
#define	PCI_PRODUCT_CYCLADES_CYCLOM4Y_2	0x0103		/* Cyclom-4Y */
#define	PCI_PRODUCT_CYCLADES_CYCLOM8Y_1	0x0104		/* Cyclom-8Y below 1M */
#define	PCI_PRODUCT_CYCLADES_CYCLOM8Y_2	0x0105		/* Cyclom-8Y */
#define	PCI_PRODUCT_CYCLADES_CYCLOMZ_1	0x0200		/* Cyclom-Z below 1M */
#define	PCI_PRODUCT_CYCLADES_CYCLOMZ_2	0x0201		/* Cyclom-Z */

/* Cyclone Microsystems products */
#define	PCI_PRODUCT_CYCLONE_PCI_700	0x0700		/* IQ80310 */

/* Cyrix/National Semiconductor products */
#define	PCI_PRODUCT_CYRIX_CX5510	0x0000		/* Cx5510 */
#define	PCI_PRODUCT_CYRIX_GXMPCI	0x0001		/* GXm PCI */
#define	PCI_PRODUCT_CYRIX_GXMISA	0x0002		/* GXm ISA */
#define	PCI_PRODUCT_CYRIX_CX5530_PCIB	0x0100		/* Cx5530 South */
#define	PCI_PRODUCT_CYRIX_CX5530_SMI	0x0101		/* Cx5530 SMI */
#define	PCI_PRODUCT_CYRIX_CX5530_IDE	0x0102		/* Cx5530 IDE */
#define	PCI_PRODUCT_CYRIX_CX5530_AUDIO	0x0103		/* Cx5530 XpressAUDIO */
#define	PCI_PRODUCT_CYRIX_CX5530_VIDEO	0x0104		/* Cx5530 Video */

/* Davicom Technologies */
#define	PCI_PRODUCT_DAVICOM_DM9009	0x9009		/* DM9009 */
#define	PCI_PRODUCT_DAVICOM_DM9100	0x9100		/* DM9100 */
#define	PCI_PRODUCT_DAVICOM_DM9102	0x9102		/* DM9102 */
#define	PCI_PRODUCT_DAVICOM_DM9132	0x9132		/* DM9132 */

/* Decision Computer Inc */
#define	PCI_PRODUCT_DCI_APCI4	0x0001		/* PCCOM 4-port */
#define	PCI_PRODUCT_DCI_APCI8	0x0002		/* PCCOM 8-port */
#define	PCI_PRODUCT_DCI_APCI2	0x0004		/* PCCOM 2-port */

/* DEC products */
#define	PCI_PRODUCT_DEC_21050	0x0001		/* 21050 */
#define	PCI_PRODUCT_DEC_21040	0x0002		/* 21040 */
#define	PCI_PRODUCT_DEC_21030	0x0004		/* 21030 */
#define	PCI_PRODUCT_DEC_NVRAM	0x0007		/* Zephyr NV-RAM */
#define	PCI_PRODUCT_DEC_KZPSA	0x0008		/* KZPSA */
#define	PCI_PRODUCT_DEC_21140	0x0009		/* 21140 */
#define	PCI_PRODUCT_DEC_PBXGB	0x000d		/* TGA2 */
#define	PCI_PRODUCT_DEC_DEFPA	0x000f		/* DEFPA */
#define	PCI_PRODUCT_DEC_21041	0x0014		/* 21041 */
#define	PCI_PRODUCT_DEC_DGLPB	0x0016		/* DGLPB */
#define	PCI_PRODUCT_DEC_ZLXPL2	0x0017		/* ZLXP-L2 */
#define	PCI_PRODUCT_DEC_MC	0x0018		/* Memory Channel Cluster Controller */
#define	PCI_PRODUCT_DEC_21142	0x0019		/* 21142/3 */
/* Farallon apparently used DEC's vendor ID by mistake */
#define	PCI_PRODUCT_DEC_PN9000SX	0x001a		/* Farallon PN9000SX */
#define	PCI_PRODUCT_DEC_21052	0x0021		/* 21052 */
#define	PCI_PRODUCT_DEC_21150	0x0022		/* 21150 */
#define	PCI_PRODUCT_DEC_21150_BC	0x0023		/* 21150-BC */
#define	PCI_PRODUCT_DEC_21152	0x0024		/* 21152 */
#define	PCI_PRODUCT_DEC_21153	0x0025		/* 21153 */
#define	PCI_PRODUCT_DEC_21154	0x0026		/* 21154 */
#define	PCI_PRODUCT_DEC_21554	0x0046		/* 21554 */
#define	PCI_PRODUCT_DEC_SWXCR	0x1065		/* SWXCR RAID */

/* Dell Computer products */
#define	PCI_PRODUCT_DELL_PERC_2SI	0x0001		/* PERC 2/Si */
#define	PCI_PRODUCT_DELL_PERC_3DI	0x0002		/* PERC 3/Di */
#define	PCI_PRODUCT_DELL_PERC_3SI	0x0003		/* PERC 3/Si */
#define	PCI_PRODUCT_DELL_PERC_3SI_2	0x0004		/* PERC 3/Si */
#define	PCI_PRODUCT_DELL_DRAC_3_ADDIN	0x0007		/* DRAC 3 Add-in */
#define	PCI_PRODUCT_DELL_DRAC_3_VUART	0x0008		/* DRAC 3 Virtual UART */
#define	PCI_PRODUCT_DELL_DRAC_3_EMBD	0x0009		/* DRAC 3 Embedded/Optional */
#define	PCI_PRODUCT_DELL_PERC_3DI_3	0x000a		/* PERC 3/Di */
#define	PCI_PRODUCT_DELL_DRAC_4_EMBD	0x000c		/* DRAC 4 Embedded/Optional */
#define	PCI_PRODUCT_DELL_DRAC_3_OPT	0x000d		/* DRAC 3 Optional */
#define	PCI_PRODUCT_DELL_PERC_4DI	0x000e		/* PERC 4/Di i960 */
#define	PCI_PRODUCT_DELL_PERC_4DI_2	0x000f		/* PERC 4/Di Verde */
#define	PCI_PRODUCT_DELL_DRAC_4	0x0011		/* DRAC 4 */
#define	PCI_PRODUCT_DELL_DRAC_4_VUART	0x0012		/* DRAC 4 Virtual UART */
#define	PCI_PRODUCT_DELL_PERC_4EDI	0x0013		/* PERC 4e/Di */
#define	PCI_PRODUCT_DELL_DRAC_4_SMIC	0x0014		/* DRAC 4 SMIC */
#define	PCI_PRODUCT_DELL_PERC5	0x0015		/* PERC 5 */
#define	PCI_PRODUCT_DELL_PERC_3DI_2_SUB	0x00cf		/* PERC 3/Di */
#define	PCI_PRODUCT_DELL_PERC_3SI_2_SUB	0x00d0		/* PERC 3/Si */
#define	PCI_PRODUCT_DELL_PERC_3DI_SUB2	0x00d1		/* PERC 3/Di */
#define	PCI_PRODUCT_DELL_PERC_3DI_SUB3	0x00d9		/* PERC 3/Di */
#define	PCI_PRODUCT_DELL_PERC_3DI_3_SUB	0x0106		/* PERC 3/Di */
#define	PCI_PRODUCT_DELL_PERC_3DI_3_SUB2	0x011b		/* PERC 3/Di */
#define	PCI_PRODUCT_DELL_PERC_3DI_3_SUB3	0x0121		/* PERC 3/Di */

/* Delta Electronics products */
#define	PCI_PRODUCT_DELTA_RHINEII	0x1320		/* RhineII */
#define	PCI_PRODUCT_DELTA_8139	0x1360		/* 8139 */

/* Diamond products */
#define	PCI_PRODUCT_DIAMOND_VIPER	0x9001		/* Viper/PCI */

/* Digi International */
#define	PCI_PRODUCT_DIGI_NEO4	0x00b0		/* Neo-4 */
#define	PCI_PRODUCT_DIGI_NEO8	0x00b1		/* Neo-8 */
#define	PCI_PRODUCT_DIGI_NEO8_PCIE	0x00f0		/* Neo-8 */

/* D-Link products */
#define	PCI_PRODUCT_DLINK_DFE550TX	0x1002		/* DFE-550TX */
#define	PCI_PRODUCT_DLINK_DFE530TXPLUS	0x1300		/* DFE-530TX+ */
#define	PCI_PRODUCT_DLINK_DFE690TXD	0x1340		/* DFE-690TXD */
#define	PCI_PRODUCT_DLINK_DRP32TXD	0x1561		/* DRP32TXD */
#define	PCI_PRODUCT_DLINK_DWL610	0x3300		/* DWL-610 */
#define	PCI_PRODUCT_DLINK_DGE550T	0x4000		/* DGE-550T */
#define	PCI_PRODUCT_DLINK_DGE550SX	0x4001		/* DGE-550SX */
#define	PCI_PRODUCT_DLINK_DFE520TX_C1	0x4200		/* DFE-520TX C1 */
#define	PCI_PRODUCT_DLINK_DGE528T	0x4300		/* DGE-528T */
#define	PCI_PRODUCT_DLINK_DGE530T_C1	0x4302		/* DGE-530T C1 */
#define	PCI_PRODUCT_DLINK_DGE560T	0x4b00		/* DGE-560T */
#define	PCI_PRODUCT_DLINK_DGE530T_B1	0x4b01		/* DGE-530T B1 */
#define	PCI_PRODUCT_DLINK_DGE560SX	0x4b02		/* DGE-560SX */
#define	PCI_PRODUCT_DLINK_DGE550T_B1	0x4b03		/* DGE-550T B1 */
#define	PCI_PRODUCT_DLINK_DGE530T_A1	0x4c00		/* DGE-530T A1 */
#define	PCI_PRODUCT_DLINK2_DFE530TXPLUS2	0x8139		/* DFE-530TX+ */

/* Dolphin products */
#define	PCI_PRODUCT_DOLPHIN_PCISCI	0x0658		/* PCI-SCI */

/* Distributed Processing Technology products */
#define	PCI_PRODUCT_DPT_MEMCTLR	0x1012		/* Memory Control */
#define	PCI_PRODUCT_DPT_SC_RAID	0xa400		/* SmartCache/Raid */
#define	PCI_PRODUCT_DPT_I960_PPB	0xa500		/* PCI-PCI */
#define	PCI_PRODUCT_DPT_RAID_I2O	0xa501		/* SmartRAID */
#define	PCI_PRODUCT_DPT_2005S	0xa511		/* SmartRAID 2005S */

/* DTC Technology Corp products */
#define	PCI_PRODUCT_DTCTECH_DMX3194U	0x0002		/* DMX3194U */

/* Dynalink products */
#define	PCI_PRODUCT_DYNALINK_IS64PH	0x1702		/* IS64PH ISDN */

/* Edimax products */
#define	PCI_PRODUCT_EDIMAX_RT2860_1	0x7708		/* RT2860 */
#define	PCI_PRODUCT_EDIMAX_RT2860_4	0x7727		/* RT2860 */
#define	PCI_PRODUCT_EDIMAX_RT2860_2	0x7728		/* RT2860 */
#define	PCI_PRODUCT_EDIMAX_RT2860_5	0x7738		/* RT2860 */
#define	PCI_PRODUCT_EDIMAX_RT2860_6	0x7748		/* RT2860 */
#define	PCI_PRODUCT_EDIMAX_RT2860_3	0x7758		/* RT2860 */
#define	PCI_PRODUCT_EDIMAX_RT2860_7	0x7768		/* RT2860 */

/* Efficient Networks products */
#define	PCI_PRODUCT_EFFICIENTNETS_ENI155PF	0x0000		/* 155P-MF1 ATM */
#define	PCI_PRODUCT_EFFICIENTNETS_ENI155PA	0x0002		/* 155P-MF1 ATM */
#define	PCI_PRODUCT_EFFICIENTNETS_EFSS25	0x0005		/* 25SS-3010 ATM */
#define	PCI_PRODUCT_EFFICIENTNETS_SS1023	0x1023		/* SpeedStream 1023 */

/* ELSA products */
#define	PCI_PRODUCT_ELSA_QS1PCI	0x1000		/* QuickStep 1000 ISDN */

/* Emulex products */
#define	PCI_PRODUCT_EMULEX_LPFC	0x10df		/* LPFC */
#define	PCI_PRODUCT_EMULEX_LP6000	0x1ae5		/* LP6000 */
#define	PCI_PRODUCT_EMULEX_XE201	0xe220		/* Lancer 10GbE */
#define	PCI_PRODUCT_EMULEX_XE201_VF	0xe228		/* Lancer 10GbE VF */
#define	PCI_PRODUCT_EMULEX_LPE121	0xf011		/* LPe121 */
#define	PCI_PRODUCT_EMULEX_LPE1250	0xf015		/* LPe1250 */
#define	PCI_PRODUCT_EMULEX_LP952	0xf095		/* LP952 */
#define	PCI_PRODUCT_EMULEX_LP982	0xf098		/* LP982 */
#define	PCI_PRODUCT_EMULEX_LP101	0xf0a1		/* LP101 */
#define	PCI_PRODUCT_EMULEX_LP1050	0xf0a5		/* LP1050 */
#define	PCI_PRODUCT_EMULEX_LP111	0xf0d1		/* LP111 */
#define	PCI_PRODUCT_EMULEX_LP1150	0xf0d5		/* LP1150 */
#define	PCI_PRODUCT_EMULEX_LPE111	0xf0e1		/* LPe111 */
#define	PCI_PRODUCT_EMULEX_LPE1150	0xf0e5		/* LPe1150 */
#define	PCI_PRODUCT_EMULEX_LPE1000	0xf0f5		/* LPe1000 */
#define	PCI_PRODUCT_EMULEX_LPE1000_SP	0xf0f6		/* LPe1000-SP */
#define	PCI_PRODUCT_EMULEX_LPE1002_SP	0xf0f7		/* LPe1002-SP */
#define	PCI_PRODUCT_EMULEX_LPE12000	0xf100		/* LPe12000 */
#define	PCI_PRODUCT_EMULEX_LPE12000_SP	0xf111		/* LPe12000-SP */
#define	PCI_PRODUCT_EMULEX_LPE12002_SP	0xf112		/* LPe12002-SP */
#define	PCI_PRODUCT_EMULEX_LP7000	0xf700		/* LP7000 */
#define	PCI_PRODUCT_EMULEX_LP8000	0xf800		/* LP8000 */
#define	PCI_PRODUCT_EMULEX_LP9000	0xf900		/* LP9000 */
#define	PCI_PRODUCT_EMULEX_LP9802	0xf980		/* LP9802 */
#define	PCI_PRODUCT_EMULEX_LP10000	0xfa00		/* LP10000 */
#define	PCI_PRODUCT_EMULEX_LPX10000	0xfb00		/* LPX10000 */
#define	PCI_PRODUCT_EMULEX_LP10000_S	0xfc00		/* LP10000-S */
#define	PCI_PRODUCT_EMULEX_LP11000_S	0xfc10		/* LP11000-S */
#define	PCI_PRODUCT_EMULEX_LPE11000_S	0xfc20		/* LPe11000-S */
#define	PCI_PRODUCT_EMULEX_LPE12000_S	0xfc40		/* LPe12000-S */
#define	PCI_PRODUCT_EMULEX_LP11000	0xfd00		/* LP11000 */
#define	PCI_PRODUCT_EMULEX_LP11000_SP	0xfd11		/* LP11000-SP */
#define	PCI_PRODUCT_EMULEX_LP11002_SP	0xfd12		/* LP11002-SP */
#define	PCI_PRODUCT_EMULEX_LPE11000	0xfe00		/* LPe11000 */
#define	PCI_PRODUCT_EMULEX_LPE11000_SP	0xfe11		/* LPe11000-SP */
#define	PCI_PRODUCT_EMULEX_LPE11002_SP	0xfe12		/* LPe11002-SP */

/* Endace Measurement Systems */
#define	PCI_PRODUCT_ENDACE_DAG35	0x3500		/* Endace Dag3.5 */
#define	PCI_PRODUCT_ENDACE_DAG36D	0x360d		/* Endace Dag3.6D */
#define	PCI_PRODUCT_ENDACE_DAG422GE	0x422e		/* Endace Dag4.22GE */
#define	PCI_PRODUCT_ENDACE_DAG423	0x4230		/* Endace Dag4.23 */
#define	PCI_PRODUCT_ENDACE_DAG423GE	0x423e		/* Endace Dag4.23GE */

/* ENE Technology products */
#define	PCI_PRODUCT_ENE_FLASH	0x0520		/* Flash memory */
#define	PCI_PRODUCT_ENE_MEMSTICK	0x0530		/* Memory Stick */
#define	PCI_PRODUCT_ENE_SDCARD	0x0550		/* SD */
#define	PCI_PRODUCT_ENE_SDMMC	0x0551		/* SD/MMC */
#define	PCI_PRODUCT_ENE_CB1211	0x1211		/* CB-1211 CardBus */
#define	PCI_PRODUCT_ENE_CB1225	0x1225		/* CB-1225 CardBus */
#define	PCI_PRODUCT_ENE_CB1410	0x1410		/* CB-1410 CardBus */
#define	PCI_PRODUCT_ENE_CB710	0x1411		/* CB-710 CardBus */
#define	PCI_PRODUCT_ENE_CB712	0x1412		/* CB-712 CardBus */
#define	PCI_PRODUCT_ENE_CB1420	0x1420		/* CB-1420 CardBus */
#define	PCI_PRODUCT_ENE_CB720	0x1421		/* CB-720 CardBus */
#define	PCI_PRODUCT_ENE_CB722	0x1422		/* CB-722 CardBus */

/* Ensoniq products */
#define	PCI_PRODUCT_ENSONIQ_AUDIOPCI97	0x1371		/* AudioPCI97 */
#define	PCI_PRODUCT_ENSONIQ_AUDIOPCI	0x5000		/* AudioPCI */
#define	PCI_PRODUCT_ENSONIQ_CT5880	0x5880		/* CT5880 */

/* Equinox Systems products */
#define	PCI_PRODUCT_EQUINOX_SST64	0x0808		/* SST-64P */
#define	PCI_PRODUCT_EQUINOX_SST128	0x1010		/* SST-128P */
#define	PCI_PRODUCT_EQUINOX_SST16A	0x80c0		/* SST-16P */
#define	PCI_PRODUCT_EQUINOX_SST16B	0x80c4		/* SST-16P */
#define	PCI_PRODUCT_EQUINOX_SST16C	0x80c8		/* SST-16P */
#define	PCI_PRODUCT_EQUINOX_SST4	0x8888		/* SST-4p */
#define	PCI_PRODUCT_EQUINOX_SST8	0x9090		/* SST-8p */

/* Evans & Sutherland products */
#define	PCI_PRODUCT_ES_FREEDOM	0x0001		/* Freedom GBus */

/* Essential Communications products */
#define	PCI_PRODUCT_ESSENTIAL_RR_HIPPI	0x0001		/* RoadRunner HIPPI */
#define	PCI_PRODUCT_ESSENTIAL_RR_GIGE	0x0005		/* RoadRunner Gig-E */

/* ESS Technology products */
#define	PCI_PRODUCT_ESSTECH_ES336H	0x0000		/* ES366H Modem */
#define	PCI_PRODUCT_ESSTECH_MAESTROII	0x1968		/* Maestro II */
#define	PCI_PRODUCT_ESSTECH_SOLO1	0x1969		/* SOLO-1 AudioDrive */
#define	PCI_PRODUCT_ESSTECH_MAESTRO2E	0x1978		/* Maestro 2E */
#define	PCI_PRODUCT_ESSTECH_ES1989	0x1988		/* ES1989 */
#define	PCI_PRODUCT_ESSTECH_ES1989M	0x1989		/* ES1989 Modem */
#define	PCI_PRODUCT_ESSTECH_MAESTRO3	0x1998		/* Maestro 3 */
#define	PCI_PRODUCT_ESSTECH_ES1983	0x1999		/* ES1983 Modem */
#define	PCI_PRODUCT_ESSTECH_MAESTRO3_2	0x199a		/* Maestro 3 Audio */
#define	PCI_PRODUCT_ESSTECH_ES336H_N	0x2808		/* ES366H Fax/Modem */
#define	PCI_PRODUCT_ESSTECH_SUPERLINK	0x2838		/* ES2838/2839 Modem */
#define	PCI_PRODUCT_ESSTECH_2898	0x2898		/* ES2898 Modem */

/* Etron products */
#define	PCI_PRODUCT_ETRON_EJ168_XHCI	0x7023		/* EJ168 xHCI */
#define	PCI_PRODUCT_ETRON_EJ188_XHCI	0x7052		/* EJ188 xHCI */

/* Eumitcom Technology products */
#define	PCI_PRODUCT_EUMITCOM_WL11000P	0x1100		/* WL11000P */

/* Exar products */
#define	PCI_PRODUCT_EXAR_XR17C152	0x0152		/* XR17C152 */
#define	PCI_PRODUCT_EXAR_XR17C154	0x0154		/* XR17C154 */
#define	PCI_PRODUCT_EXAR_XR17C158	0x0158		/* XR17C158 */
#define	PCI_PRODUCT_EXAR_XR17V352	0x0352		/* XR17V352 */
#define	PCI_PRODUCT_EXAR_XR17V354	0x0354		/* XR17V354 */

/* FORE products */
#define	PCI_PRODUCT_FORE_PCA200	0x0210		/* ATM PCA-200 */
#define	PCI_PRODUCT_FORE_PCA200E	0x0300		/* ATM PCA-200e */

/* Forte Media products */
#define	PCI_PRODUCT_FORTEMEDIA_FM801	0x0801		/* 801 Sound */

/* Freescale products */
#define	PCI_PRODUCT_FREESCALE_MPC8349E	0x0080		/* MPC8349E */
#define	PCI_PRODUCT_FREESCALE_MPC8349	0x0081		/* MPC8349 */
#define	PCI_PRODUCT_FREESCALE_MPC8347E_TBGA	0x0082		/* MPC8347E TBGA */
#define	PCI_PRODUCT_FREESCALE_MPC8347_TBGA	0x0083		/* MPC8347 TBGA */
#define	PCI_PRODUCT_FREESCALE_MPC8347E_PBGA	0x0084		/* MPC8347E PBGA */
#define	PCI_PRODUCT_FREESCALE_MPC8347_PBGA	0x0085		/* MPC8347 PBGA */
#define	PCI_PRODUCT_FREESCALE_MPC8343E	0x0086		/* MPC8343E */
#define	PCI_PRODUCT_FREESCALE_MPC8343	0x0087		/* MPC8343 */

/* Fresco Logic products */
#define	PCI_PRODUCT_FRESCO_FL1000	0x1000		/* FL1000 xHCI */
#define	PCI_PRODUCT_FRESCO_FL1009	0x1009		/* FL1009 xHCI */
#define	PCI_PRODUCT_FRESCO_FL1100	0x1100		/* FL1100 xHCI */
#define	PCI_PRODUCT_FRESCO_FL1400	0x1400		/* FL1400 xHCI */

/* Fujitsu products */
#define	PCI_PRODUCT_FUJITSU_PW008GE5	0x11a1		/* PW008GE5 */
#define	PCI_PRODUCT_FUJITSU_PW008GE4	0x11a2		/* PW008GE4 */
#define	PCI_PRODUCT_FUJITSU_PP250_450_LAN	0x11cc		/* PRIMEPOWER250/450 LAN */
#define	PCI_PRODUCT_FUJITSU_SPARC64X	0x16b7		/* SPARC64 X PCIe */

/* Fusion-io products */
#define	PCI_PRODUCT_FUSIONIO_IODRIVE_1_2	0x1003		/* ioDrive v1.2 */
#define	PCI_PRODUCT_FUSIONIO_IODRIVE	0x1005		/* ioDrive */
#define	PCI_PRODUCT_FUSIONIO_IOXTREME	0x1006		/* ioXtreme */
#define	PCI_PRODUCT_FUSIONIO_IOXTREME_PRO	0x1007		/* ioXtreme Pro */

/* Future Domain products */
#define	PCI_PRODUCT_FUTUREDOMAIN_TMC_18C30	0x0000		/* TMC-18C30 */

/* Guillemot products */
#define	PCI_PRODUCT_GEMTEK_PR103	0x1001		/* PR103 */

/* Genesys Logic products */
#define	PCI_PRODUCT_GENESYS_GL9755	0x9755		/* GL9755 */

/* Global Sun Technology products */
#define	PCI_PRODUCT_GLOBALSUN_GL24110P03	0x1100		/* GL24110P03 */
#define	PCI_PRODUCT_GLOBALSUN_GL24110P	0x1101		/* GL24110P */
#define	PCI_PRODUCT_GLOBALSUN_GL24110P02	0x1102		/* GL24110P02 */
#define	PCI_PRODUCT_GLOBALSUN_8031	0x1103		/* 8031 */

/* Globespan products */
#define	PCI_PRODUCT_GLOBESPAN_G7370	0xd002		/* Pulsar G7370 ADSL */

/* Hauppauge Computer Works */
#define	PCI_PRODUCT_HAUPPAUGE_WINTV	0x13eb		/* WinTV */

/* Hawking products */
#define	PCI_PRODUCT_HAWKING_PN672TX	0xab08		/* PN672TX */

/* Hifn products */
#define	PCI_PRODUCT_HIFN_7751	0x0005		/* 7751 */
#define	PCI_PRODUCT_HIFN_6500	0x0006		/* 6500 */
#define	PCI_PRODUCT_HIFN_7811	0x0007		/* 7811 */
#define	PCI_PRODUCT_HIFN_7951	0x0012		/* 7951 */
#define	PCI_PRODUCT_HIFN_78XX	0x0014		/* 7814/7851/7854 */
#define	PCI_PRODUCT_HIFN_8065	0x0016		/* 8065 */
#define	PCI_PRODUCT_HIFN_8165	0x0017		/* 8165 */
#define	PCI_PRODUCT_HIFN_8154	0x0018		/* 8154 */
#define	PCI_PRODUCT_HIFN_7956	0x001d		/* 7956 */
#define	PCI_PRODUCT_HIFN_7955	0x0020		/* 7955/7954 */

/* Hint products */
#define	PCI_PRODUCT_HINT_HB6_1	0x0020		/* HB6 */
#define	PCI_PRODUCT_HINT_HB6_2	0x0021		/* HB6 */
#define	PCI_PRODUCT_HINT_HB4	0x0022		/* HB4 */
#define	PCI_PRODUCT_HINT_VXPRO_II_HOST	0x8011		/* Host */
#define	PCI_PRODUCT_HINT_VXPRO_II_ISA	0x8012		/* ISA */
#define	PCI_PRODUCT_HINT_VXPRO_II_EIDE	0x8013		/* EIDE */

/* Hitachi products */
#define	PCI_PRODUCT_HITACHI_SWC	0x0101		/* MSVCC01 Video Capture */
#define	PCI_PRODUCT_HITACHI_SH7751	0x3505		/* SH7751 PCI */
#define	PCI_PRODUCT_HITACHI_SH7751R	0x350e		/* SH7751R PCI */

/* Hitachi Micro products */
#define	PCI_PRODUCT_HITACHI_M_ISP2100	0x2100		/* ISP2100 */

/* Hewlett-Packard products */
#define	PCI_PRODUCT_HP_VISUALIZE_EG	0x1005		/* Visualize EG */
#define	PCI_PRODUCT_HP_VISUALIZE_FX6	0x1006		/* Visualize FX6 */
#define	PCI_PRODUCT_HP_VISUALIZE_FX4	0x1008		/* Visualize FX4 */
#define	PCI_PRODUCT_HP_VISUALIZE_FX2	0x100a		/* Visualize FX2 */
#define	PCI_PRODUCT_HP_TACH_TL	0x1028		/* Tach TL FibreChannel */
#define	PCI_PRODUCT_HP_TACH_XL2	0x1029		/* Tach XL2 FibreChannel */
#define	PCI_PRODUCT_HP_J2585A	0x1030		/* J2585A */
#define	PCI_PRODUCT_HP_J2585B	0x1031		/* J2585B */
#define	PCI_PRODUCT_HP_DIVA	0x1048		/* Diva Serial Multiport */
#define	PCI_PRODUCT_HP_ELROY	0x1054		/* Elroy Ropes-PCI */
#define	PCI_PRODUCT_HP_VISUALIZE_FXE	0x108b		/* Visualize FXe */
#define	PCI_PRODUCT_HP_TOPTOOLS	0x10c1		/* TopTools */
#define	PCI_PRODUCT_HP_NETRAID_4M	0x10c2		/* NetRaid-4M */
#define	PCI_PRODUCT_HP_SMARTIRQ	0x10ed		/* NetServer SmartIRQ */
#define	PCI_PRODUCT_HP_82557B	0x1200		/* 82557B NIC */
#define	PCI_PRODUCT_HP_PLUTO	0x1229		/* Pluto MIO */
#define	PCI_PRODUCT_HP_ZX1_IOC	0x122a		/* zx1 IOC */
#define	PCI_PRODUCT_HP_MERCURY	0x122e		/* Mercury Ropes-PCI */
#define	PCI_PRODUCT_HP_QUICKSILVER	0x12b4		/* QuickSilver Ropes-PCI */
#define	PCI_PRODUCT_HP_HPSAP430I	0x1920		/* Smart Array P430i */
#define	PCI_PRODUCT_HP_HPSAP830I	0x1921		/* Smart Array P830i */
#define	PCI_PRODUCT_HP_HPSAP430	0x1922		/* Smart Array P430 */
#define	PCI_PRODUCT_HP_HPSAP431	0x1923		/* Smart Array P431 */
#define	PCI_PRODUCT_HP_HPSAP830	0x1924		/* Smart Array P830 */
#define	PCI_PRODUCT_HP_HPSAP731M	0x1926		/* Smart Array P731m */
#define	PCI_PRODUCT_HP_HPSAP230I	0x1928		/* Smart Array P230i */
#define	PCI_PRODUCT_HP_HPSAP530	0x1929		/* Smart Array P530 */
#define	PCI_PRODUCT_HP_HPSAP531	0x192a		/* Smart Array P531 */
#define	PCI_PRODUCT_HP_HPSAP224BR	0x21bd		/* Smart Array P244br */
#define	PCI_PRODUCT_HP_HPSAP741M	0x21be		/* Smart Array P741m */
#define	PCI_PRODUCT_HP_HPSAH240AR	0x21bf		/* Smart HBA H240ar */
#define	PCI_PRODUCT_HP_HPSAP440AR	0x21c0		/* Smart Array P440ar */
#define	PCI_PRODUCT_HP_HPSAP440	0x21c2		/* Smart Array P440 */
#define	PCI_PRODUCT_HP_HPSAP441	0x21c3		/* Smart Array P441 */
#define	PCI_PRODUCT_HP_HPSAP841	0x21c5		/* Smart Array P841 */
#define	PCI_PRODUCT_HP_HPSAH244BR	0x21c6		/* Smart HBA H244br */
#define	PCI_PRODUCT_HP_HPSAH240	0x21c7		/* Smart HBA H240 */
#define	PCI_PRODUCT_HP_HPSAH241	0x21c8		/* Smart HBA H241 */
#define	PCI_PRODUCT_HP_HPSAP246BR	0x21ca		/* Smart Array P246br */
#define	PCI_PRODUCT_HP_HPSAP840	0x21cb		/* Smart Array P840 */
#define	PCI_PRODUCT_HP_HPSAP542T	0x21cc		/* Smart Array P542t */
#define	PCI_PRODUCT_HP_HPSAP240TR	0x21cd		/* Smart Array P240tr */
#define	PCI_PRODUCT_HP_HPSAH240TR	0x21ce		/* Smart HBA H240tr */
#define	PCI_PRODUCT_HP_HPSAV100	0x3210		/* Smart Array V100 */
#define	PCI_PRODUCT_HP_HPSAE200I_1	0x3211		/* Smart Array E200i */
#define	PCI_PRODUCT_HP_HPSAE200	0x3212		/* Smart Array E200 */
#define	PCI_PRODUCT_HP_HPSAE200I_2	0x3213		/* Smart Array E200i */
#define	PCI_PRODUCT_HP_HPSAE200I_3	0x3214		/* Smart Array E200i */
#define	PCI_PRODUCT_HP_HPSAE200I_4	0x3215		/* Smart Array E200i */
#define	PCI_PRODUCT_HP_HPSA_1	0x3220		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSA_2	0x3222		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSAP800	0x3223		/* Smart Array P800 */
#define	PCI_PRODUCT_HP_HPSAP600	0x3225		/* Smart Array P600 */
#define	PCI_PRODUCT_HP_HPSA_3	0x3230		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSA_4	0x3231		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSA_5	0x3232		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSAE500_1	0x3233		/* Smart Array E500 */
#define	PCI_PRODUCT_HP_HPSAP400	0x3234		/* Smart Array P400 */
#define	PCI_PRODUCT_HP_HPSAP400I	0x3235		/* Smart Array P400i */
#define	PCI_PRODUCT_HP_HPSA_6	0x3236		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSAE500_2	0x3237		/* Smart Array E500 */
#define	PCI_PRODUCT_HP_HPSA_7	0x3238		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSA_8	0x3239		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSA_9	0x323a		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSA_10	0x323b		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSA_11	0x323c		/* Smart Array */
#define	PCI_PRODUCT_HP_HPSAP700M	0x323d		/* Smart Array P700m */
#define	PCI_PRODUCT_HP_HPSAP212	0x3241		/* Smart Array P212 */
#define	PCI_PRODUCT_HP_HPSAP410	0x3243		/* Smart Array P410 */
#define	PCI_PRODUCT_HP_HPSAP410I	0x3245		/* Smart Array P410i */
#define	PCI_PRODUCT_HP_HPSAP411	0x3247		/* Smart Array P411 */
#define	PCI_PRODUCT_HP_HPSAP812	0x3249		/* Smart Array P812 */
#define	PCI_PRODUCT_HP_HPSAP712M	0x324a		/* Smart Array P712m */
#define	PCI_PRODUCT_HP_HPSAP711M	0x324b		/* Smart Array P711m */
#define	PCI_PRODUCT_HP_USB	0x3300		/* USB */
#define	PCI_PRODUCT_HP_ILO3_SERIAL	0x3301		/* iLO3 Serial */
#define	PCI_PRODUCT_HP_IPMI	0x3302		/* IPMI */
#define	PCI_PRODUCT_HP_ILO3_SLAVE	0x3306		/* iLO3 Slave */
#define	PCI_PRODUCT_HP_ILO3_MGMT	0x3307		/* iLO3 Management */
#define	PCI_PRODUCT_HP_ILO3_WATCHDOG	0x3308		/* iLO3 Watchdog */
#define	PCI_PRODUCT_HP_HPSAP222	0x3350		/* Smart Array P222 */
#define	PCI_PRODUCT_HP_HPSAP420	0x3351		/* Smart Array P420 */
#define	PCI_PRODUCT_HP_HPSAP421	0x3352		/* Smart Array P421 */
#define	PCI_PRODUCT_HP_HPSAP822	0x3353		/* Smart Array P822 */
#define	PCI_PRODUCT_HP_HPSAP420I	0x3354		/* Smart Array P420i */
#define	PCI_PRODUCT_HP_HPSAP220I	0x3355		/* Smart Array P220i */
#define	PCI_PRODUCT_HP_HPSAP721M	0x3356		/* Smart Array P721m */

/* Huawei products */
#define	PCI_PRODUCT_HUAWEI_HIBMC_VGA	0x1711		/* HiBMC VGA */

/* IBM products */
#define	PCI_PRODUCT_IBM_GXT150P	0x001b		/* GXT-150P */
#define	PCI_PRODUCT_IBM_82G2675	0x001d		/* 82G2675 */
#define	PCI_PRODUCT_IBM_82351	0x0022		/* 82351 */
#define	PCI_PRODUCT_IBM_SERVERAID	0x002e		/* ServeRAID */
#define	PCI_PRODUCT_IBM_OLYMPIC	0x003e		/* Olympic */
#define	PCI_PRODUCT_IBM_I82557B	0x0057		/* i82557B */
#define	PCI_PRODUCT_IBM_RSA	0x010f		/* RSA */
#define	PCI_PRODUCT_IBM_FIREGL2	0x0170		/* FireGL2 */
#define	PCI_PRODUCT_IBM_133PCIX	0x01a7		/* 133 PCIX-PCIX */
#define	PCI_PRODUCT_IBM_SERVERAID2	0x01bd		/* ServeRAID */
#define	PCI_PRODUCT_IBM_4810_BSP	0x0295		/* 4810 BSP */
#define	PCI_PRODUCT_IBM_4810_SCC	0x0297		/* 4810 SCC */
#define	PCI_PRODUCT_IBM_CALGARY_IOMMU	0x02a1		/* Calgary IOMMU */
#define	PCI_PRODUCT_IBM_POWER8_HB	0x03dc		/* POWER8 Host */
#define	PCI_PRODUCT_IBM_POWER9_HB	0x04c1		/* POWER9 Host */

/* IC Ensemble */
#define	PCI_PRODUCT_ICENSEMBLE_ICE1712	0x1712		/* Envy24 I/O Ctrlr */
#define	PCI_PRODUCT_ICENSEMBLE_VT172X	0x1724		/* Envy24PT/HT Audio */

/* IDT products */
#define	PCI_PRODUCT_IDT_77201	0x0001		/* 77201/77211 ATM */
#define	PCI_PRODUCT_IDT_89HPES12N3A	0x8018		/* 89HPES12N3A */
#define	PCI_PRODUCT_IDT_89HPES24N3A	0x801c		/* 89HPES24N3A */
#define	PCI_PRODUCT_IDT_89HPES24T6	0x802e		/* 89HPES24T6 */
#define	PCI_PRODUCT_IDT_89HPES4T4	0x803a		/* 89HPES4T4 */
#define	PCI_PRODUCT_IDT_89HPES5T5	0x803c		/* 89HPES5T5 */
#define	PCI_PRODUCT_IDT_89HPES24T3G2	0x806a		/* 89HPES24T3G2 */
#define	PCI_PRODUCT_IDT_89HPES64H16G2	0x8077		/* 89HPES64H16G2 */
#define	PCI_PRODUCT_IDT_89HPES48H12G2	0x807a		/* 89HPES48H12G2 */
#define	PCI_PRODUCT_IDT_89H64H16G3	0x80bf		/* 89H64H16G3 */

/* Integrated Micro Solutions products */
#define	PCI_PRODUCT_IMS_5026	0x5026		/* 5026 */
#define	PCI_PRODUCT_IMS_5027	0x5027		/* 5027 */
#define	PCI_PRODUCT_IMS_5028	0x5028		/* 5028 */
#define	PCI_PRODUCT_IMS_8849	0x8849		/* 8849 */
#define	PCI_PRODUCT_IMS_8853	0x8853		/* 8853 */
#define	PCI_PRODUCT_IMS_TT128	0x9128		/* Twin Turbo 128 */
#define	PCI_PRODUCT_IMS_TT3D	0x9135		/* Twin Turbo 3D */

/* Industrial Computer Source */
#define	PCI_PRODUCT_INDCOMPSRC_WDT50X	0x22c0		/* WDT 50x Watchdog Timer */

/* Initio Corporation */
#define	PCI_PRODUCT_INITIO_INIC850	0x0850		/* INIC-850 */
#define	PCI_PRODUCT_INITIO_INIC1060	0x1060		/* INIC-1060 */
#define	PCI_PRODUCT_INITIO_INIC940	0x9400		/* INIC-940 */
#define	PCI_PRODUCT_INITIO_INIC941	0x9401		/* INIC-941 */
#define	PCI_PRODUCT_INITIO_INIC950	0x9500		/* INIC-950 */

/* InnoTek Systemberatung GmbH */
#define	PCI_PRODUCT_INNOTEK_VBNVME	0x4e56		/* NVMe */
#define	PCI_PRODUCT_INNOTEK_VBGA	0xbeef		/* Graphics Adapter */
#define	PCI_PRODUCT_INNOTEK_VBGS	0xcafe		/* Guest Service */

/* INPROCOMM products */
#define	PCI_PRODUCT_INPROCOMM_IPN2120	0x2120		/* IPN2120 */
#define	PCI_PRODUCT_INPROCOMM_IPN2220	0x2220		/* IPN2220 */

/* Intel products */
#define	PCI_PRODUCT_INTEL_EESISA	0x0008		/* EES ISA */
#define	PCI_PRODUCT_INTEL_21145	0x0039		/* 21145 */
#define	PCI_PRODUCT_INTEL_CORE_HB_0	0x0040		/* Core Host */
#define	PCI_PRODUCT_INTEL_CORE_PCIE_0	0x0041		/* Core PCIE */
#define	PCI_PRODUCT_INTEL_CLARKDALE_IGD	0x0042		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE_PCIE_1	0x0043		/* Core PCIE */
#define	PCI_PRODUCT_INTEL_CORE_HB_1	0x0044		/* Core Host */
#define	PCI_PRODUCT_INTEL_3400_PCIE	0x0045		/* 3400 PCIE */
#define	PCI_PRODUCT_INTEL_ARRANDALE_IGD	0x0046		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE_HB_2	0x0048		/* Core Host */
#define	PCI_PRODUCT_INTEL_CORE_PCIE_6	0x0049		/* Core PCIE */
#define	PCI_PRODUCT_INTEL_WL_6005_1	0x0082		/* Centrino Advanced-N 6205 */
#define	PCI_PRODUCT_INTEL_WL_1000_1	0x0083		/* WiFi Link 1000 */
#define	PCI_PRODUCT_INTEL_WL_1000_2	0x0084		/* WiFi Link 1000 */
#define	PCI_PRODUCT_INTEL_WL_6005_2	0x0085		/* Centrino Advanced-N 6205 */
#define	PCI_PRODUCT_INTEL_WL_6050_1	0x0087		/* Centrino Advanced-N 6250 */
#define	PCI_PRODUCT_INTEL_WL_6050_2	0x0089		/* Centrino Advanced-N 6250 */
#define	PCI_PRODUCT_INTEL_WL_1030_1	0x008a		/* WiFi Link 1030 */
#define	PCI_PRODUCT_INTEL_WL_1030_2	0x008b		/* WiFi Link 1030 */
#define	PCI_PRODUCT_INTEL_WL_6030_1	0x0090		/* Centrino Advanced-N 6030 */
#define	PCI_PRODUCT_INTEL_WL_6030_2	0x0091		/* Centrino Advanced-N 6030 */
#define	PCI_PRODUCT_INTEL_CORE2G_HB	0x0100		/* Core 2G Host */
#define	PCI_PRODUCT_INTEL_CORE2G_PCIE_1	0x0101		/* Core 2G PCIE */
#define	PCI_PRODUCT_INTEL_CORE2G_GT1	0x0102		/* HD Graphics 2000 */
#define	PCI_PRODUCT_INTEL_CORE2G_M_HB	0x0104		/* Core 2G Host */
#define	PCI_PRODUCT_INTEL_CORE2G_PCIE_2	0x0105		/* Core 2G PCIE */
#define	PCI_PRODUCT_INTEL_CORE2G_M_GT1	0x0106		/* HD Graphics 2000 */
#define	PCI_PRODUCT_INTEL_XEONE3_1200_HB	0x0108		/* Xeon E3-1200 Host */
#define	PCI_PRODUCT_INTEL_CORE2G_PCIE_3	0x0109		/* Core 2G PCIE */
#define	PCI_PRODUCT_INTEL_CORE2G_S_GT	0x010a		/* HD Graphics P3000 */
#define	PCI_PRODUCT_INTEL_CORE2G_PCIE_4	0x010d		/* Core 2G PCIE */
#define	PCI_PRODUCT_INTEL_CORE2G_GT2	0x0112		/* HD Graphics 3000 */
#define	PCI_PRODUCT_INTEL_CORE2G_M_GT2	0x0116		/* HD Graphics 3000 */
#define	PCI_PRODUCT_INTEL_CORE2G_GT2_PLUS	0x0122		/* HD Graphics 3000 */
#define	PCI_PRODUCT_INTEL_CORE2G_M_GT2_PLUS	0x0126		/* HD Graphics 3000 */
#define	PCI_PRODUCT_INTEL_MDFLD_IGD_0	0x0130		/* Medfield Video */
#define	PCI_PRODUCT_INTEL_MDFLD_IGD_1	0x0131		/* Medfield Video */
#define	PCI_PRODUCT_INTEL_MDFLD_IGD_2	0x0132		/* Medfield Video */
#define	PCI_PRODUCT_INTEL_MDFLD_IGD_3	0x0133		/* Medfield Video */
#define	PCI_PRODUCT_INTEL_MDFLD_IGD_4	0x0134		/* Medfield Video */
#define	PCI_PRODUCT_INTEL_MDFLD_IGD_5	0x0135		/* Medfield Video */
#define	PCI_PRODUCT_INTEL_MDFLD_IGD_6	0x0136		/* Medfield Video */
#define	PCI_PRODUCT_INTEL_MDFLD_IGD_7	0x0137		/* Medfield Video */
#define	PCI_PRODUCT_INTEL_CORE3G_HB	0x0150		/* Core 3G Host */
#define	PCI_PRODUCT_INTEL_CORE3G_PCIE_1	0x0151		/* Core 3G PCIE */
#define	PCI_PRODUCT_INTEL_CORE3G_D_GT1	0x0152		/* HD Graphics 2500 */
#define	PCI_PRODUCT_INTEL_CORE3G_THERM	0x0153		/* Core 3G Thermal */
#define	PCI_PRODUCT_INTEL_CORE3G_M_HB	0x0154		/* Core 3G Host */
#define	PCI_PRODUCT_INTEL_CORE3G_PCIE_2	0x0155		/* Core 3G PCIE */
#define	PCI_PRODUCT_INTEL_CORE3G_M_GT1	0x0156		/* HD Graphics 2500 */
#define	PCI_PRODUCT_INTEL_XEONE3_1200V2_HB	0x0158		/* Xeon E3-1200 v2 Host */
#define	PCI_PRODUCT_INTEL_CORE3G_PCIE_3	0x0159		/* Core 3G PCIE */
#define	PCI_PRODUCT_INTEL_CORE3G_S_GT1	0x015a		/* HD Graphics 2500 */
#define	PCI_PRODUCT_INTEL_CORE3G_PCIE_4	0x015d		/* Core 3G PCIE */
#define	PCI_PRODUCT_INTEL_CORE3G_D_GT2	0x0162		/* HD Graphics 4000 */
#define	PCI_PRODUCT_INTEL_CORE3G_M_GT2	0x0166		/* HD Graphics 4000 */
#define	PCI_PRODUCT_INTEL_CORE3G_S_GT2	0x016a		/* HD Graphics P4000 */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_LPC_1	0x0284		/* 400 Series LPC */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_LPC_2	0x0285		/* 400 Series LPC */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_P2SB	0x02a0		/* 400 Series P2SB */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PMC	0x02a1		/* 400 Series PMC */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_SMB	0x02a3		/* 400 Series SMBus */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_SPI_FLASH	0x02a4		/* 400 Series SPI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_TH	0x02a6		/* 400 Series TH */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_UART_1	0x02a8		/* 400 Series UART */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_UART_2	0x02a9		/* 400 Series UART */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_SPI_1	0x02aa		/* 400 Series SPI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_SPI_2	0x02ab		/* 400 Series SPI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_9	0x02b0		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_10	0x02b1		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_11	0x02b2		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_12	0x02b3		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_13	0x02b4		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_14	0x02b5		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_15	0x02b6		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_16	0x02b7		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_1	0x02b8		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_2	0x02b9		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_3	0x02ba		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_4	0x02bb		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_5	0x02bc		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_6	0x02bd		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_7	0x02be		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_PCIE_8	0x02bf		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_EMMC	0x02c4		/* 400 Series eMMC */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_I2C_1	0x02c5		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_I2C_2	0x02c6		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_UART_3	0x02c7		/* 400 Series UART */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_HDA	0x02c8		/* 400 Series HD Audio */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_AHCI	0x02d3		/* 400 Series AHCI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_RAID_1	0x02d5		/* 400 Series RAID */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_RAID_2	0x02d7		/* 400 Series RAID */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_MEI_1	0x02e0		/* 400 Series MEI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_MEI_2	0x02e1		/* 400 Series MEI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_IDER	0x02e2		/* 400 Series IDE-R */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_KT	0x02e3		/* 400 Series KT */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_MEI_3	0x02e4		/* 400 Series MEI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_MEI_4	0x02e5		/* 400 Series MEI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_I2C_3	0x02e8		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_I2C_4	0x02e9		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_I2C_5	0x02ea		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_I2C_6	0x02eb		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_XHCI	0x02ed		/* 400 Series xHCI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_XDCI	0x02ee		/* 400 Series xDCI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_SRAM	0x02ef		/* 400 Series Shared SRAM */
#define	PCI_PRODUCT_INTEL_WL_22500_2	0x02f0		/* Wi-Fi 6 AX201 */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_SDXC	0x02f5		/* 400 Series SDXC */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_THERM	0x02f9		/* 400 Series Thermal */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_SPI_3	0x02fb		/* 400 Series SPI */
#define	PCI_PRODUCT_INTEL_400SERIES_LP_ISH	0x02fc		/* 400 Series ISH */
#define	PCI_PRODUCT_INTEL_80303	0x0309		/* 80303 IOP */
#define	PCI_PRODUCT_INTEL_80312	0x030d		/* 80312 I/O Companion */
#define	PCI_PRODUCT_INTEL_IOXAPIC_A	0x0326		/* IOxAPIC */
#define	PCI_PRODUCT_INTEL_IOXAPIC_B	0x0327		/* IOxAPIC */
#define	PCI_PRODUCT_INTEL_6700PXH_A	0x0329		/* 6700PXH PCIE-PCIX */
#define	PCI_PRODUCT_INTEL_6700PXH_B	0x032a		/* 6700PXH PCIE-PCIX */
#define	PCI_PRODUCT_INTEL_6702PXH	0x032c		/* 6702PXH PCIE-PCIX */
#define	PCI_PRODUCT_INTEL_IOP332_A	0x0330		/* IOP332 PCIE-PCIX */
#define	PCI_PRODUCT_INTEL_IOP332_B	0x0332		/* IOP332 PCIE-PCIX */
#define	PCI_PRODUCT_INTEL_IOP331	0x0335		/* IOP331 PCIX-PCIX */
#define	PCI_PRODUCT_INTEL_41210_A	0x0340		/* 41210 PCIE-PCIX */
#define	PCI_PRODUCT_INTEL_41210_B	0x0341		/* 41210 PCIE-PCIX */
#define	PCI_PRODUCT_INTEL_IOP333_A	0x0370		/* IOP333 PCIE-PCIX */
#define	PCI_PRODUCT_INTEL_IOP333_B	0x0372		/* IOP333 PCIE-PCIX */
#define	PCI_PRODUCT_INTEL_CORE4G_D_GT1	0x0402		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_M_GT1	0x0406		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_S_GT1	0x040a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_GT1_1	0x040b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_GT1_2	0x040e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_D_GT2	0x0412		/* HD Graphics 4600 */
#define	PCI_PRODUCT_INTEL_CORE4G_M_GT2	0x0416		/* HD Graphics 4600 */
#define	PCI_PRODUCT_INTEL_CORE4G_S_GT2	0x041a		/* HD Graphics P4600 */
#define	PCI_PRODUCT_INTEL_CORE4G_R_GT2_1	0x041b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_GT2_2	0x041e		/* HD Graphics 4600 */
#define	PCI_PRODUCT_INTEL_CORE4G_D_GT3	0x0422		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_M_GT2_2	0x0426		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_S_GT3	0x042a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_GT3_1	0x042b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_GT3_2	0x042e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_DH89XXCC_SGMII	0x0438		/* DH89XXCC SGMII */
#define	PCI_PRODUCT_INTEL_DH89XXCC_SERDES	0x043a		/* DH89XXCC SerDes */
#define	PCI_PRODUCT_INTEL_DH89XXCC_BPLANE	0x043c		/* DH89XXCC Backplane */
#define	PCI_PRODUCT_INTEL_DH89XXCC_SFP	0x0440		/* DH89XXCC SFP */
#define	PCI_PRODUCT_INTEL_PCEB	0x0482		/* 82375EB EISA */
#define	PCI_PRODUCT_INTEL_CDC	0x0483		/* 82424ZX Cache/DRAM */
#define	PCI_PRODUCT_INTEL_SIO	0x0484		/* 82378IB ISA */
#define	PCI_PRODUCT_INTEL_82426EX	0x0486		/* 82426EX ISA */
#define	PCI_PRODUCT_INTEL_PCMC	0x04a3		/* 82434LX/NX */
#define	PCI_PRODUCT_INTEL_GDT_RAID1	0x0600		/* GDT RAID */
#define	PCI_PRODUCT_INTEL_GDT_RAID2	0x061f		/* GDT RAID */
#define	PCI_PRODUCT_INTEL_H470_LPC	0x0684		/* H470 LPC */
#define	PCI_PRODUCT_INTEL_Z490_LPC	0x0685		/* Z490 LPC */
#define	PCI_PRODUCT_INTEL_Q470_LPC	0x0687		/* Q470 LPC */
#define	PCI_PRODUCT_INTEL_QM480_LPC	0x068c		/* QM480 LPC */
#define	PCI_PRODUCT_INTEL_HM470_LPC	0x068d		/* HM470 LPC */
#define	PCI_PRODUCT_INTEL_WM490_LPC	0x068e		/* WM490 LPC */
#define	PCI_PRODUCT_INTEL_W480_LPC	0x0697		/* W480 LPC */
#define	PCI_PRODUCT_INTEL_400SERIES_P2SB	0x06a0		/* 400 Series P2SB */
#define	PCI_PRODUCT_INTEL_400SERIES_PMC	0x06a1		/* 400 Series PMC */
#define	PCI_PRODUCT_INTEL_400SERIES_SMB	0x06a3		/* 400 Series SMBus */
#define	PCI_PRODUCT_INTEL_400SERIES_SPI_FLASH	0x06a4		/* 400 Series SPI */
#define	PCI_PRODUCT_INTEL_400SERIES_TH	0x06a6		/* 400 Series TH */
#define	PCI_PRODUCT_INTEL_400SERIES_UART_1	0x06a8		/* 400 Series UART */
#define	PCI_PRODUCT_INTEL_400SERIES_UART_2	0x06a9		/* 400 Series UART */
#define	PCI_PRODUCT_INTEL_400SERIES_GSPI_1	0x06aa		/* 400 Series GSPI */
#define	PCI_PRODUCT_INTEL_400SERIES_GSPI_2	0x06ab		/* 400 Series GSPI */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_9	0x06b0		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_10	0x06b1		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_11	0x06b2		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_12	0x06b3		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_13	0x06b4		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_14	0x06b5		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_15	0x06b6		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_16	0x06b7		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_1	0x06b8		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_2	0x06b9		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_3	0x06ba		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_4	0x06bb		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_5	0x06bc		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_6	0x06bd		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_7	0x06be		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_8	0x06bf		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_17	0x06c0		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_18	0x06c1		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_19	0x06c2		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_PCIE_20	0x06c3		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_UART_3	0x06c7		/* 400 Series UART */
#define	PCI_PRODUCT_INTEL_400SERIES_CAVS	0x06c8		/* 400 Series cAVS */
#define	PCI_PRODUCT_INTEL_400SERIES_AHCI_1	0x06d2		/* 400 Series AHCI */
#define	PCI_PRODUCT_INTEL_400SERIES_AHCI_2	0x06d3		/* 400 Series AHCI */
#define	PCI_PRODUCT_INTEL_400SERIES_RAID_1	0x06d5		/* 400 Series RAID */
#define	PCI_PRODUCT_INTEL_400SERIES_RAID_2	0x06d7		/* 400 Series RAID */
#define	PCI_PRODUCT_INTEL_400SERIES_AHCI_3	0x06de		/* 400 Series AHCI */
#define	PCI_PRODUCT_INTEL_400SERIES_HECI_1	0x06e0		/* 400 Series HECI */
#define	PCI_PRODUCT_INTEL_400SERIES_HECI_2	0x06e1		/* 400 Series HECI */
#define	PCI_PRODUCT_INTEL_400SERIES_IDER	0x06e2		/* 400 Series IDE-R */
#define	PCI_PRODUCT_INTEL_400SERIES_KT	0x06e3		/* 400 Series KT */
#define	PCI_PRODUCT_INTEL_400SERIES_HECI_3	0x06e4		/* 400 Series HECI */
#define	PCI_PRODUCT_INTEL_400SERIES_HECI_4	0x06e5		/* 400 Series HECI */
#define	PCI_PRODUCT_INTEL_400SERIES_I2C_1	0x06e8		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_I2C_2	0x06e9		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_I2C_3	0x06ea		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_I2C_4	0x06eb		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_XHCI	0x06ed		/* 400 Series xHCI */
#define	PCI_PRODUCT_INTEL_400SERIES_SRAM	0x06ef		/* 400 Series Shared SRAM */
#define	PCI_PRODUCT_INTEL_WL_22500_5	0x06f0		/* Wi-Fi 6 AX201 */
#define	PCI_PRODUCT_INTEL_400SERIES_SDXC	0x06f5		/* 400 Series SDXC */
#define	PCI_PRODUCT_INTEL_400SERIES_THERM	0x06f9		/* 400 Series Thermal */
#define	PCI_PRODUCT_INTEL_400SERIES_GSPI_3	0x06fb		/* 400 Series GSPI */
#define	PCI_PRODUCT_INTEL_400SERIES_ISH	0x06fc		/* 400 Series ISH */
#define	PCI_PRODUCT_INTEL_WL_6150_1	0x0885		/* Centrino Wireless-N 6150 */
#define	PCI_PRODUCT_INTEL_WL_6150_2	0x0886		/* Centrino Wireless-N 6150 */
#define	PCI_PRODUCT_INTEL_WL_2230_1	0x0887		/* Centrino Wireless-N 2230 */
#define	PCI_PRODUCT_INTEL_WL_2230_2	0x0888		/* Centrino Wireless-N 2230 */
#define	PCI_PRODUCT_INTEL_WL_6235_1	0x088e		/* Centrino Advanced-N 6235 */
#define	PCI_PRODUCT_INTEL_WL_6235_2	0x088f		/* Centrino Advanced-N 6235 */
#define	PCI_PRODUCT_INTEL_WL_2200_1	0x0890		/* Centrino Wireless-N 2200 */
#define	PCI_PRODUCT_INTEL_WL_2200_2	0x0891		/* Centrino Wireless-N 2200 */
#define	PCI_PRODUCT_INTEL_WL_135_1	0x0892		/* Centrino Wireless-N 135 */
#define	PCI_PRODUCT_INTEL_WL_135_2	0x0893		/* Centrino Wireless-N 135 */
#define	PCI_PRODUCT_INTEL_WL_105_1	0x0894		/* Centrino Wireless-N 105 */
#define	PCI_PRODUCT_INTEL_WL_105_2	0x0895		/* Centrino Wireless-N 105 */
#define	PCI_PRODUCT_INTEL_WL_130_1	0x0896		/* Centrino Wireless-N 130 */
#define	PCI_PRODUCT_INTEL_WL_130_2	0x0897		/* Centrino Wireless-N 130 */
#define	PCI_PRODUCT_INTEL_WL_100_1	0x08ae		/* Centrino Wireless-N 100 */
#define	PCI_PRODUCT_INTEL_WL_100_2	0x08af		/* Centrino Wireless-N 100 */
#define	PCI_PRODUCT_INTEL_WL_7260_1	0x08b1		/* AC 7260 */
#define	PCI_PRODUCT_INTEL_WL_7260_2	0x08b2		/* AC 7260 */
#define	PCI_PRODUCT_INTEL_WL_3160_1	0x08b3		/* AC 3160 */
#define	PCI_PRODUCT_INTEL_WL_3160_2	0x08b4		/* AC 3160 */
#define	PCI_PRODUCT_INTEL_NVME	0x0953		/* NVMe */
#define	PCI_PRODUCT_INTEL_WL_7265_1	0x095a		/* AC 7265 */
#define	PCI_PRODUCT_INTEL_WL_7265_2	0x095b		/* AC 7265 */
#define	PCI_PRODUCT_INTEL_80960RP	0x0960		/* i960 RP */
#define	PCI_PRODUCT_INTEL_80960RM	0x0962		/* i960 RM */
#define	PCI_PRODUCT_INTEL_80960RN	0x0964		/* i960 RN */
#define	PCI_PRODUCT_INTEL_CORE4G_D_ULT_GT1	0x0a02		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_THERM	0x0a03		/* Core 4G Thermal */
#define	PCI_PRODUCT_INTEL_CORE4G_HB_1	0x0a04		/* Core 4G Host */
#define	PCI_PRODUCT_INTEL_CORE4G_M_ULT_GT1	0x0a06		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_S_ULT_GT1	0x0a0a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_ULT_GT1_1	0x0a0b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_HDA_1	0x0a0c		/* Core 4G HD Audio */
#define	PCI_PRODUCT_INTEL_CORE4G_R_ULT_GT1_2	0x0a0e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_D_ULT_GT2	0x0a12		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_M_ULT_GT2	0x0a16		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_S_ULT_GT2	0x0a1a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_ULT_GT2_1	0x0a1b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_ULT_GT2_2	0x0a1e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_D_ULT_GT3	0x0a22		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_M_ULT_GT3	0x0a26		/* HD Graphics 5000 */
#define	PCI_PRODUCT_INTEL_CORE4G_S_ULT_GT3	0x0a2a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_ULT_GT3_1	0x0a2b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_ULT_GT3_2	0x0a2e		/* Iris 5100 */
#define	PCI_PRODUCT_INTEL_NVME_5	0x0a54		/* SSD DC */
#define	PCI_PRODUCT_INTEL_BXT_IGD_1	0x0a84		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_GMA3600_0	0x0be0		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_D2000_IGD	0x0be1		/* Atom D2000/N2000 Video */
#define	PCI_PRODUCT_INTEL_GMA3600_2	0x0be2		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_3	0x0be3		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_4	0x0be4		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_5	0x0be5		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_6	0x0be6		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_7	0x0be7		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_8	0x0be8		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_9	0x0be9		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_A	0x0bea		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_B	0x0beb		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_C	0x0bec		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_D	0x0bed		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_E	0x0bee		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_GMA3600_F	0x0bef		/* GMA 3600 */
#define	PCI_PRODUCT_INTEL_D2000_HB_2	0x0bf1		/* Atom D2000/N2000 Host */
#define	PCI_PRODUCT_INTEL_D2000_HB_3	0x0bf2		/* Atom D2000/N2000 Host */
#define	PCI_PRODUCT_INTEL_D2000_HB_4	0x0bf3		/* Atom D2000/N2000 Host */
#define	PCI_PRODUCT_INTEL_D2000_HB	0x0bf5		/* Atom D2000/N2000 Host */
#define	PCI_PRODUCT_INTEL_CORE4G_HB_2	0x0c00		/* Core 4G Host */
#define	PCI_PRODUCT_INTEL_CORE4G_PCIE_1	0x0c01		/* Core 4G PCIE */
#define	PCI_PRODUCT_INTEL_CORE4G_D_SDV_GT1	0x0c02		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_HB_3	0x0c04		/* Core 4G Host */
#define	PCI_PRODUCT_INTEL_CORE4G_PCIE_2	0x0c05		/* Core 4G PCIE */
#define	PCI_PRODUCT_INTEL_CORE4G_M_SDV_GT1	0x0c06		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_XEONE3_1200V3_HB	0x0c08		/* Xeon E3-1200 v3 Host */
#define	PCI_PRODUCT_INTEL_CORE4G_PCIE_3	0x0c09		/* Core 4G PCIE */
#define	PCI_PRODUCT_INTEL_CORE4G_S_SDV_GT1	0x0c0a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_SDV_GT1_1	0x0c0b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_HDA_2	0x0c0c		/* Core 4G HD Audio */
#define	PCI_PRODUCT_INTEL_CORE4G_R_SDV_GT1_2	0x0c0e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_D_SDV_GT2	0x0c12		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_M_SDV_GT2	0x0c16		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_S_SDV_GT2	0x0c1a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_SDV_GT2_1	0x0c1b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_SDV_GT2_2	0x0c1e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_D_SDV_GT3	0x0c22		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_M_SDV_GT3	0x0c26		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_S_SDV_GT3	0x0c2a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_SDV_GT3_1	0x0c2b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_SDV_GT3_2	0x0c2e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_PCIE_1	0x0c46		/* Atom S1200 PCIE */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_PCIE_2	0x0c47		/* Atom S1200 PCIE */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_PCIE_3	0x0c48		/* Atom S1200 PCIE */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_PCIE_4	0x0c49		/* Atom S1200 PCIE */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_MEI	0x0c54		/* Atom S1200 MEI */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_SMB_1	0x0c59		/* Atom S1200 SMBus */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_SMB_2	0x0c5a		/* Atom S1200 SMBus */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_SMB_3	0x0c5b		/* Atom S1200 SMBus */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_SMB_4	0x0c5c		/* Atom S1200 SMBus */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_SMB_5	0x0c5d		/* Atom S1200 SMBus */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_SMB_6	0x0c5e		/* Atom S1200 SMBus */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_UART	0x0c5f		/* Atom S1200 UART */
#define	PCI_PRODUCT_INTEL_ATOM_S1200_ISA	0x0c60		/* Atom S1200 ISA */
#define	PCI_PRODUCT_INTEL_ATOM_S1240_HB	0x0c72		/* Atom S1240 Host */
#define	PCI_PRODUCT_INTEL_ATOM_S1220_HB	0x0c73		/* Atom S1220 Host */
#define	PCI_PRODUCT_INTEL_ATOM_S1260_HB	0x0c75		/* Atom S1260 Host */
#define	PCI_PRODUCT_INTEL_CORE4G_D_CRW_GT1	0x0d02		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_M_CRW_GT1	0x0d06		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_S_CRW_GT1	0x0d0a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_CRW_GT1_1	0x0d0b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_HDA_3	0x0d0c		/* Core 4G HD Audio */
#define	PCI_PRODUCT_INTEL_CORE4G_R_CRW_GT1_2	0x0d0e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_D_CRW_GT2	0x0d12		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_M_CRW_GT2	0x0d16		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_S_CRW_GT2	0x0d1a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_CRW_GT2_1	0x0d1b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_CRW_GT2_2	0x0d1e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_D_CRW_GT3	0x0d22		/* Iris Pro Graphics 5200 */
#define	PCI_PRODUCT_INTEL_CORE4G_M_CRW_GT3	0x0d26		/* Iris Pro Graphics 5200 */
#define	PCI_PRODUCT_INTEL_CORE4G_S_CRW_GT3	0x0d2a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_CRW_GT3_1	0x0d2b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE4G_R_CRW_GT3_2	0x0d2e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_I219_LM11	0x0d4c		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V11	0x0d4d		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM10	0x0d4e		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V10	0x0d4f		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM12	0x0d53		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V12	0x0d55		/* I219-V */
#define	PCI_PRODUCT_INTEL_I225_IT	0x0d9f		/* I225-IT */
#define	PCI_PRODUCT_INTEL_I219_LM23	0x0dc5		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V23	0x0dc6		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM22	0x0dc7		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V22	0x0dc8		/* I219-V */
#define	PCI_PRODUCT_INTEL_E5V2_HB	0x0e00		/* E5 v2 Host */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_1	0x0e01		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_2	0x0e02		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_3	0x0e03		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_4	0x0e04		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_5	0x0e05		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_6	0x0e06		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_7	0x0e07		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_8	0x0e08		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_9	0x0e09		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_10	0x0e0a		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_PCIE_11	0x0e0b		/* E5 v2 PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_R2PCIE	0x0e1d		/* E5 v2 R2PCIE */
#define	PCI_PRODUCT_INTEL_E5V2_UBOX_1	0x0e1e		/* E5 v2 UBOX */
#define	PCI_PRODUCT_INTEL_E5V2_UBOX_2	0x0e1f		/* E5 v2 UBOX */
#define	PCI_PRODUCT_INTEL_E5V2_IOAT_1	0x0e20		/* E5 v2 I/OAT */
#define	PCI_PRODUCT_INTEL_E5V2_IOAT_2	0x0e21		/* E5 v2 I/OAT */
#define	PCI_PRODUCT_INTEL_E5V2_IOAT_3	0x0e22		/* E5 v2 I/OAT */
#define	PCI_PRODUCT_INTEL_E5V2_IOAT_4	0x0e23		/* E5 v2 I/OAT */
#define	PCI_PRODUCT_INTEL_E5V2_IOAT_5	0x0e24		/* E5 v2 I/OAT */
#define	PCI_PRODUCT_INTEL_E5V2_IOAT_6	0x0e25		/* E5 v2 I/OAT */
#define	PCI_PRODUCT_INTEL_E5V2_IOAT_7	0x0e26		/* E5 v2 I/OAT */
#define	PCI_PRODUCT_INTEL_E5V2_IOAT_8	0x0e27		/* E5 v2 I/OAT */
#define	PCI_PRODUCT_INTEL_E5V2_ADDRMAP	0x0e28		/* E5 v2 Address Map */
#define	PCI_PRODUCT_INTEL_E5V2_IIO_RAS	0x0e2a		/* E5 v2 IIO RAS */
#define	PCI_PRODUCT_INTEL_E5V2_IOAPIC	0x0e2c		/* E5 v2 I/O APIC */
#define	PCI_PRODUCT_INTEL_E5V2_HA_2	0x0e30		/* E5 v2 Home Agent */
#define	PCI_PRODUCT_INTEL_E5V2_QPI_L_MON_0	0x0e34		/* E5 v2 QPI Link Monitor */
#define	PCI_PRODUCT_INTEL_E5V2_QPI_L_MON_1	0x0e36		/* E5 v2 QPI Link Monitor */
#define	PCI_PRODUCT_INTEL_E5V2_RAS	0x0e71		/* E5 v2 RAS */
#define	PCI_PRODUCT_INTEL_E5V2_QPI_L_0	0x0e80		/* E5 v2 QPI Link */
#define	PCI_PRODUCT_INTEL_E5V2_QPI	0x0e81		/* E5 v2 QPI */
#define	PCI_PRODUCT_INTEL_E5V2_QPI_L_1	0x0e90		/* E5 v2 QPI Link */
#define	PCI_PRODUCT_INTEL_E5V2_HA_1	0x0ea0		/* E5 v2 Home Agent */
#define	PCI_PRODUCT_INTEL_E5V2_TA	0x0ea8		/* E5 v2 TA */
#define	PCI_PRODUCT_INTEL_E5V2_TAD_1	0x0eaa		/* E5 v2 TAD */
#define	PCI_PRODUCT_INTEL_E5V2_TAD_2	0x0eab		/* E5 v2 TAD */
#define	PCI_PRODUCT_INTEL_E5V2_TAD_3	0x0eac		/* E5 v2 TAD */
#define	PCI_PRODUCT_INTEL_E5V2_TAD_4	0x0ead		/* E5 v2 TAD */
#define	PCI_PRODUCT_INTEL_E5V2_THERMAL_1	0x0eb0		/* E5 v2 Thermal */
#define	PCI_PRODUCT_INTEL_E5V2_THERMAL_2	0x0eb1		/* E5 v2 Thermal */
#define	PCI_PRODUCT_INTEL_E5V2_ERR_1	0x0eb2		/* E5 v2 Error */
#define	PCI_PRODUCT_INTEL_E5V2_ERR_2	0x0eb3		/* E5 v2 Error */
#define	PCI_PRODUCT_INTEL_E5V2_THERMAL_3	0x0eb4		/* E5 v2 Thermal */
#define	PCI_PRODUCT_INTEL_E5V2_THERMAL_4	0x0eb5		/* E5 v2 Thermal */
#define	PCI_PRODUCT_INTEL_E5V2_ERR_3	0x0eb6		/* E5 v2 Error */
#define	PCI_PRODUCT_INTEL_E5V2_ERR_4	0x0eb7		/* E5 v2 Error */
#define	PCI_PRODUCT_INTEL_E5V2_PCU_0	0x0ec0		/* E5 v2 PCU */
#define	PCI_PRODUCT_INTEL_E5V2_PCU_1	0x0ec1		/* E5 v2 PCU */
#define	PCI_PRODUCT_INTEL_E5V2_PCU_2	0x0ec2		/* E5 v2 PCU */
#define	PCI_PRODUCT_INTEL_E5V2_PCU_3	0x0ec3		/* E5 v2 PCU */
#define	PCI_PRODUCT_INTEL_E5V2_PCU_4	0x0ec4		/* E5 v2 PCU */
#define	PCI_PRODUCT_INTEL_E5V2_SAD_1	0x0ec8		/* E5 v2 SAD */
#define	PCI_PRODUCT_INTEL_E5V2_BROADCAST_1	0x0ec9		/* E5 v2 Broadcast */
#define	PCI_PRODUCT_INTEL_E5V2_BROADCAST_2	0x0eca		/* E5 v2 Broadcast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_1	0x0ee0		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_2	0x0ee1		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_3	0x0ee2		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_4	0x0ee3		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_5	0x0ee4		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_6	0x0ee5		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_7	0x0ee6		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_8	0x0ee7		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_9	0x0ee8		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_10	0x0ee9		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_11	0x0eea		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_12	0x0eeb		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_13	0x0eec		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_14	0x0eed		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_E5V2_UNICAST_15	0x0eee		/* E5 v2 Unicast */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_HB	0x0f00		/* Bay Trail Host */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_HDA	0x0f04		/* Bay Trail HD Audio */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_SIO_DMA	0x0f06		/* Bay Trail SIO DMA */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_PWM_1	0x0f08		/* Bay Trail PWM */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_PWM_2	0x0f09		/* Bay Trail PWM */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_HSUART_1	0x0f0a		/* Bay Trail HSUART */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_HSUART_2	0x0f0c		/* Bay Trail HSUART */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_SPI	0x0f0e		/* Bay Trail SPI */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_SMB	0x0f12		/* Bay Trail SMBus */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_SDIO	0x0f15		/* Bay Trail SDIO */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_SDMMC	0x0f16		/* Bay Trail SD/MMC */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_TXE	0x0f18		/* Bay Trail TXE */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_LPC	0x0f1c		/* Bay Trail LPC */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_SATA_1	0x0f20		/* Bay Trail SATA */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_SATA_2	0x0f21		/* Bay Trail SATA */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_AHCI	0x0f23		/* Bay Trail AHCI */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_LPA	0x0f28		/* Bay Trail Low Power Audio */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_IGD_4	0x0f30		/* Bay Trail Video */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_IGD_1	0x0f31		/* Bay Trail Video */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_IGD_2	0x0f32		/* Bay Trail Video */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_IGD_3	0x0f33		/* Bay Trail Video */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_EHCI	0x0f34		/* Bay Trail EHCI */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_XHCI	0x0f35		/* Bay Trail xHCI */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_8237	0x0f40		/* Bay Trail I2C DMA */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_I2C_1	0x0f41		/* Bay Trail I2C */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_I2C_2	0x0f42		/* Bay Trail I2C */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_I2C_3	0x0f43		/* Bay Trail I2C */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_I2C_4	0x0f44		/* Bay Trail I2C */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_I2C_5	0x0f45		/* Bay Trail I2C */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_I2C_6	0x0f46		/* Bay Trail I2C */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_I2C_7	0x0f47		/* Bay Trail I2C */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_PCIE_1	0x0f48		/* Bay Trail PCIE */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_PCIE_2	0x0f4a		/* Bay Trail PCIE */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_PCIE_3	0x0f4c		/* Bay Trail PCIE */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_PCIE_4	0x0f4e		/* Bay Trail PCIE */
#define	PCI_PRODUCT_INTEL_BAYTRAIL_EMMC	0x0f50		/* Bay Trail eMMC */
#define	PCI_PRODUCT_INTEL_82542	0x1000		/* 82542 */
#define	PCI_PRODUCT_INTEL_82543GC_FIBER	0x1001		/* 82543GC */
#define	PCI_PRODUCT_INTEL_MODEM56	0x1002		/* Modem */
#define	PCI_PRODUCT_INTEL_82543GC_COPPER	0x1004		/* 82543GC */
#define	PCI_PRODUCT_INTEL_82544EI_COPPER	0x1008		/* 82544EI */
#define	PCI_PRODUCT_INTEL_82544EI_FIBER	0x1009		/* 82544EI */
#define	PCI_PRODUCT_INTEL_82544GC_COPPER	0x100c		/* 82544GC */
#define	PCI_PRODUCT_INTEL_82544GC_LOM	0x100d		/* 82544GC */
#define	PCI_PRODUCT_INTEL_82540EM	0x100e		/* 82540EM */
#define	PCI_PRODUCT_INTEL_82545EM_COPPER	0x100f		/* 82545EM */
#define	PCI_PRODUCT_INTEL_82546EB_COPPER	0x1010		/* 82546EB */
#define	PCI_PRODUCT_INTEL_82545EM_FIBER	0x1011		/* 82545EM */
#define	PCI_PRODUCT_INTEL_82546EB_FIBER	0x1012		/* 82546EB */
#define	PCI_PRODUCT_INTEL_82541EI	0x1013		/* 82541EI */
#define	PCI_PRODUCT_INTEL_82541ER_LOM	0x1014		/* 82541EI */
#define	PCI_PRODUCT_INTEL_82540EM_LOM	0x1015		/* 82540EM */
#define	PCI_PRODUCT_INTEL_82540EP_LOM	0x1016		/* 82540EP */
#define	PCI_PRODUCT_INTEL_82540EP	0x1017		/* 82540EP */
#define	PCI_PRODUCT_INTEL_82541EI_MOBILE	0x1018		/* 82541EI */
#define	PCI_PRODUCT_INTEL_82547EI	0x1019		/* 82547EI */
#define	PCI_PRODUCT_INTEL_82547EI_MOBILE	0x101a		/* 82547EI */
#define	PCI_PRODUCT_INTEL_82546EB_QUAD_CPR	0x101d		/* 82546EB */
#define	PCI_PRODUCT_INTEL_82540EP_LP	0x101e		/* 82540EP */
#define	PCI_PRODUCT_INTEL_82545GM_COPPER	0x1026		/* 82545GM */
#define	PCI_PRODUCT_INTEL_82545GM_FIBER	0x1027		/* 82545GM */
#define	PCI_PRODUCT_INTEL_82545GM_SERDES	0x1028		/* 82545GM */
#define	PCI_PRODUCT_INTEL_PRO_100	0x1029		/* PRO/100 */
#define	PCI_PRODUCT_INTEL_82559	0x1030		/* 82559 */
#define	PCI_PRODUCT_INTEL_PRO_100_VE_0	0x1031		/* PRO/100 VE */
#define	PCI_PRODUCT_INTEL_PRO_100_VE_1	0x1032		/* PRO/100 VE */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_0	0x1033		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_1	0x1034		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_82562EH_HPNA_0	0x1035		/* 82562EH HomePNA */
#define	PCI_PRODUCT_INTEL_82562EH_HPNA_1	0x1036		/* 82562EH HomePNA */
#define	PCI_PRODUCT_INTEL_82562EH_HPNA_2	0x1037		/* 82562EH HomePNA */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_2	0x1038		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VE_2	0x1039		/* PRO/100 VE */
#define	PCI_PRODUCT_INTEL_82801DB_LAN	0x103a		/* 82801DB LAN */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_3	0x103b		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_4	0x103c		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VE_3	0x103d		/* PRO/100 VE */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_5	0x103e		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_536EP	0x1040		/* V.92 Modem */
#define	PCI_PRODUCT_INTEL_PRO_WL_2100	0x1043		/* PRO/Wireless 2100 */
#define	PCI_PRODUCT_INTEL_82597EX	0x1048		/* 82597EX */
#define	PCI_PRODUCT_INTEL_ICH8_IGP_M_AMT	0x1049		/* ICH8 IGP M AMT */
#define	PCI_PRODUCT_INTEL_ICH8_IGP_AMT	0x104a		/* ICH8 IGP AMT */
#define	PCI_PRODUCT_INTEL_ICH8_IGP_C	0x104b		/* ICH8 IGP C */
#define	PCI_PRODUCT_INTEL_ICH8_IFE	0x104c		/* ICH8 IFE */
#define	PCI_PRODUCT_INTEL_ICH8_IGP_M	0x104d		/* ICH8 IGP M */
#define	PCI_PRODUCT_INTEL_X710_10G_SFP_2	0x104e		/* X710 SFP+ */
#define	PCI_PRODUCT_INTEL_PRO_100_VE_4	0x1050		/* PRO/100 VE */
#define	PCI_PRODUCT_INTEL_PRO_100_VE_5	0x1051		/* PRO/100 VE */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_6	0x1052		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_7	0x1053		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_8	0x1054		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_9	0x1055		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_10	0x1056		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_11	0x1057		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_12	0x1058		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_M	0x1059		/* PRO/100 M */
#define	PCI_PRODUCT_INTEL_82571EB_COPPER	0x105e		/* 82571EB */
#define	PCI_PRODUCT_INTEL_82571EB_FIBER	0x105f		/* 82571EB */
#define	PCI_PRODUCT_INTEL_82571EB_SERDES	0x1060		/* 82571EB */
#define	PCI_PRODUCT_INTEL_82801FB_LAN_2	0x1064		/* 82801FB LAN */
#define	PCI_PRODUCT_INTEL_PRO_100_VE_6	0x1065		/* PRO/100 VE */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_13	0x1066		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_14	0x1067		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_82801FBM_LAN	0x1068		/* 82801FBM LAN */
#define	PCI_PRODUCT_INTEL_82801GB_LAN_2	0x1069		/* 82801GB LAN */
#define	PCI_PRODUCT_INTEL_PRO_100_VE_7	0x106a		/* PRO/100 VE */
#define	PCI_PRODUCT_INTEL_PRO_100_VE_8	0x106b		/* PRO/100 VE */
#define	PCI_PRODUCT_INTEL_82547GI	0x1075		/* 82547GI */
#define	PCI_PRODUCT_INTEL_82541GI	0x1076		/* 82541GI */
#define	PCI_PRODUCT_INTEL_82541GI_MOBILE	0x1077		/* 82541GI */
#define	PCI_PRODUCT_INTEL_82541ER	0x1078		/* 82541ER */
#define	PCI_PRODUCT_INTEL_82546GB_COPPER	0x1079		/* 82546GB */
#define	PCI_PRODUCT_INTEL_82546GB_FIBER	0x107a		/* 82546GB */
#define	PCI_PRODUCT_INTEL_82546GB_SERDES	0x107b		/* 82546GB */
#define	PCI_PRODUCT_INTEL_82541GI_LF	0x107c		/* 82541GI */
#define	PCI_PRODUCT_INTEL_82572EI_COPPER	0x107d		/* 82572EI */
#define	PCI_PRODUCT_INTEL_82572EI_FIBER	0x107e		/* 82572EI */
#define	PCI_PRODUCT_INTEL_82572EI_SERDES	0x107f		/* 82572EI */
#define	PCI_PRODUCT_INTEL_82546GB_PCIE	0x108a		/* 82546GB */
#define	PCI_PRODUCT_INTEL_82573E	0x108b		/* 82573E */
#define	PCI_PRODUCT_INTEL_82573E_IAMT	0x108c		/* 82573E */
#define	PCI_PRODUCT_INTEL_82573E_IDE	0x108d		/* 82573E IDE */
#define	PCI_PRODUCT_INTEL_82573E_KCS	0x108e		/* 82573E KCS */
#define	PCI_PRODUCT_INTEL_82573E_SERIAL	0x108f		/* 82573E Serial */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_15	0x1091		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_16	0x1092		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_17	0x1093		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_18	0x1094		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_PRO_100_VM_19	0x1095		/* PRO/100 VM */
#define	PCI_PRODUCT_INTEL_80003ES2LAN_CPR_DPT	0x1096		/* 80003ES2 */
#define	PCI_PRODUCT_INTEL_80003ES2LAN_SDS_DPT	0x1098		/* 80003ES2 */
#define	PCI_PRODUCT_INTEL_82546GB_QUAD_CPR	0x1099		/* 82546GB */
#define	PCI_PRODUCT_INTEL_82573L	0x109a		/* 82573L */
#define	PCI_PRODUCT_INTEL_82546GB_2	0x109b		/* 82546GB */
#define	PCI_PRODUCT_INTEL_82597EX_CX4	0x109e		/* 82597EX */
#define	PCI_PRODUCT_INTEL_82571EB_AT	0x10a0		/* 82571EB */
#define	PCI_PRODUCT_INTEL_82571EB_AF	0x10a1		/* 82571EB */
#define	PCI_PRODUCT_INTEL_82571EB_QUAD_CPR	0x10a4		/* 82571EB */
#define	PCI_PRODUCT_INTEL_82571EB_QUAD_FBR	0x10a5		/* 82571EB */
#define	PCI_PRODUCT_INTEL_82575EB_COPPER	0x10a7		/* 82575EB */
#define	PCI_PRODUCT_INTEL_82575EB_SERDES	0x10a9		/* 82575EB */
#define	PCI_PRODUCT_INTEL_82573L_PL_1	0x10b0		/* 82573L */
#define	PCI_PRODUCT_INTEL_82573V_PM	0x10b2		/* 82573V */
#define	PCI_PRODUCT_INTEL_82573E_PM	0x10b3		/* 82573E */
#define	PCI_PRODUCT_INTEL_82573L_PL_2	0x10b4		/* 82573L */
#define	PCI_PRODUCT_INTEL_82546GB_QUAD_CPR_K	0x10b5		/* 82546GB */
#define	PCI_PRODUCT_INTEL_82598	0x10b6		/* 82598 */
#define	PCI_PRODUCT_INTEL_82572EI	0x10b9		/* 82572EI */
#define	PCI_PRODUCT_INTEL_80003ES2LAN_CPR_SPT	0x10ba		/* 80003ES2 */
#define	PCI_PRODUCT_INTEL_80003ES2LAN_SDS_SPT	0x10bb		/* 80003ES2 */
#define	PCI_PRODUCT_INTEL_82571EB_QUAD_CPR_LP	0x10bc		/* 82571EB */
#define	PCI_PRODUCT_INTEL_ICH9_IGP_AMT	0x10bd		/* ICH9 IGP AMT */
#define	PCI_PRODUCT_INTEL_ICH9_IGP_M	0x10bf		/* ICH9 IGP M */
#define	PCI_PRODUCT_INTEL_ICH9_IFE	0x10c0		/* ICH9 IFE */
#define	PCI_PRODUCT_INTEL_ICH9_IFE_G	0x10c2		/* ICH9 IFE G */
#define	PCI_PRODUCT_INTEL_ICH9_IFE_GT	0x10c3		/* ICH9 IFE GT */
#define	PCI_PRODUCT_INTEL_ICH8_IFE_GT	0x10c4		/* ICH8 IFE GT */
#define	PCI_PRODUCT_INTEL_ICH8_IFE_G	0x10c5		/* ICH8 IFE G */
#define	PCI_PRODUCT_INTEL_82598AF_DUAL	0x10c6		/* 82598AF */
#define	PCI_PRODUCT_INTEL_82598AF	0x10c7		/* 82598AF */
#define	PCI_PRODUCT_INTEL_82598AT	0x10c8		/* 82598AT */
#define	PCI_PRODUCT_INTEL_82576	0x10c9		/* 82576 */
#define	PCI_PRODUCT_INTEL_ICH9_IGP_M_V	0x10cb		/* ICH9 IGP M V */
#define	PCI_PRODUCT_INTEL_ICH10_R_BM_LM	0x10cc		/* ICH10 R BM LM */
#define	PCI_PRODUCT_INTEL_ICH10_R_BM_LF	0x10cd		/* ICH10 R BM LF */
#define	PCI_PRODUCT_INTEL_ICH10_R_BM_V	0x10ce		/* ICH10 R BM V */
#define	PCI_PRODUCT_INTEL_82574L	0x10d3		/* 82574L */
#define	PCI_PRODUCT_INTEL_82571PT_QUAD_CPR	0x10d5		/* 82571PT */
#define	PCI_PRODUCT_INTEL_82575GB_QUAD_CPR	0x10d6		/* 82575GB */
#define	PCI_PRODUCT_INTEL_82598AT_DUAL	0x10d7		/* 82598AT */
#define	PCI_PRODUCT_INTEL_82571EB_SDS_DUAL	0x10d9		/* 82571EB */
#define	PCI_PRODUCT_INTEL_82571EB_SDS_QUAD	0x10da		/* 82571EB */
#define	PCI_PRODUCT_INTEL_82598EB_SFP	0x10db		/* 82598EB */
#define	PCI_PRODUCT_INTEL_82598EB_CX4	0x10dd		/* 82598EB */
#define	PCI_PRODUCT_INTEL_ICH10_D_BM_LM	0x10de		/* ICH10 D BM LM */
#define	PCI_PRODUCT_INTEL_ICH10_D_BM_LF	0x10df		/* ICH10 D BM LF */
#define	PCI_PRODUCT_INTEL_82598_SR_DUAL_EM	0x10e1		/* 82598 */
#define	PCI_PRODUCT_INTEL_82575GB_QP_PM	0x10e2		/* 82575GB */
#define	PCI_PRODUCT_INTEL_ICH9_BM	0x10e5		/* ICH9 BM */
#define	PCI_PRODUCT_INTEL_82576_FIBER	0x10e6		/* 82576 */
#define	PCI_PRODUCT_INTEL_82576_SERDES	0x10e7		/* 82576 */
#define	PCI_PRODUCT_INTEL_82576_QUAD_COPPER	0x10e8		/* 82576 */
#define	PCI_PRODUCT_INTEL_82577LM	0x10ea		/* 82577LM */
#define	PCI_PRODUCT_INTEL_82577LC	0x10eb		/* 82577LC */
#define	PCI_PRODUCT_INTEL_82598EB_CX4_DUAL	0x10ec		/* 82598EB */
#define	PCI_PRODUCT_INTEL_82599VF	0x10ed		/* 82599 */
#define	PCI_PRODUCT_INTEL_82578DM	0x10ef		/* 82578DM */
#define	PCI_PRODUCT_INTEL_82578DC	0x10f0		/* 82578DC */
#define	PCI_PRODUCT_INTEL_82598_DA_DUAL	0x10f1		/* 82598 */
#define	PCI_PRODUCT_INTEL_82598EB_XF_LR	0x10f4		/* 82598EB */
#define	PCI_PRODUCT_INTEL_ICH9_IGP_M_AMT	0x10f5		/* ICH9 IGP M AMT */
#define	PCI_PRODUCT_INTEL_82574LA	0x10f6		/* 82574L */
#define	PCI_PRODUCT_INTEL_82599_KX4	0x10f7		/* 82599 */
#define	PCI_PRODUCT_INTEL_82599_COMBO_BP	0x10f8		/* 82599 */
#define	PCI_PRODUCT_INTEL_82599_CX4	0x10f9		/* 82599 */
#define	PCI_PRODUCT_INTEL_82599_SFP	0x10fb		/* 82599 */
#define	PCI_PRODUCT_INTEL_82599_XAUI	0x10fc		/* 82599 */
#define	PCI_PRODUCT_INTEL_82552	0x10fe		/* 82552 */
#define	PCI_PRODUCT_INTEL_82815_HB	0x1130		/* 82815 Host */
#define	PCI_PRODUCT_INTEL_82815_AGP	0x1131		/* 82815 AGP */
#define	PCI_PRODUCT_INTEL_82815_IGD	0x1132		/* 82815 Video */
#define	PCI_PRODUCT_INTEL_82806AA_APIC	0x1161		/* 82806AA APIC */
#define	PCI_PRODUCT_INTEL_82559ER	0x1209		/* 82559ER */
#define	PCI_PRODUCT_INTEL_82092AA	0x1222		/* 82092AA IDE */
#define	PCI_PRODUCT_INTEL_SAA7116	0x1223		/* SAA7116 */
#define	PCI_PRODUCT_INTEL_82452_HB	0x1225		/* 82452KX/GX */
#define	PCI_PRODUCT_INTEL_82596	0x1226		/* EE Pro 10 PCI */
#define	PCI_PRODUCT_INTEL_EEPRO100	0x1227		/* EE Pro 100 */
#define	PCI_PRODUCT_INTEL_EEPRO100S	0x1228		/* EE Pro 100 Smart */
#define	PCI_PRODUCT_INTEL_8255X	0x1229		/* 8255x */
#define	PCI_PRODUCT_INTEL_82437FX	0x122d		/* 82437FX */
#define	PCI_PRODUCT_INTEL_82371FB_ISA	0x122e		/* 82371FB ISA */
#define	PCI_PRODUCT_INTEL_82371FB_IDE	0x1230		/* 82371FB IDE */
#define	PCI_PRODUCT_INTEL_82371MX	0x1234		/* 82371 ISA and IDE */
#define	PCI_PRODUCT_INTEL_82437MX	0x1235		/* 82437MX */
#define	PCI_PRODUCT_INTEL_82441FX	0x1237		/* 82441FX */
#define	PCI_PRODUCT_INTEL_82380AB	0x123c		/* 82380AB Mobile ISA */
#define	PCI_PRODUCT_INTEL_82380FB	0x124b		/* 82380FB Mobile */
#define	PCI_PRODUCT_INTEL_E823_L_SFP	0x124d		/* E823-L SFP */
#define	PCI_PRODUCT_INTEL_E823_L_10G	0x124e		/* E823-L/X557-AT */
#define	PCI_PRODUCT_INTEL_E823_L_1G	0x124f		/* E823-L 1GbE */
#define	PCI_PRODUCT_INTEL_82439HX	0x1250		/* 82439HX */
#define	PCI_PRODUCT_INTEL_I226_LM	0x125b		/* I226-LM */
#define	PCI_PRODUCT_INTEL_I226_V	0x125c		/* I226-V */
#define	PCI_PRODUCT_INTEL_I226_IT	0x125d		/* I226-IT */
#define	PCI_PRODUCT_INTEL_I221_V	0x125e		/* I221-V */
#define	PCI_PRODUCT_INTEL_I226_BLANK_NVM	0x125f		/* I226 */
#define	PCI_PRODUCT_INTEL_82806AA	0x1360		/* 82806AA */
#define	PCI_PRODUCT_INTEL_82870P2_PPB	0x1460		/* 82870P2 PCIX-PCIX */
#define	PCI_PRODUCT_INTEL_82870P2_IOXAPIC	0x1461		/* 82870P2 IOxAPIC */
#define	PCI_PRODUCT_INTEL_82870P2_HPLUG	0x1462		/* 82870P2 Hot Plug */
#define	PCI_PRODUCT_INTEL_ICH8_82567V_3	0x1501		/* ICH8 82567V-3 */
#define	PCI_PRODUCT_INTEL_82579LM	0x1502		/* 82579LM */
#define	PCI_PRODUCT_INTEL_82579V	0x1503		/* 82579V */
#define	PCI_PRODUCT_INTEL_82599_SFP_EM	0x1507		/* 82599 */
#define	PCI_PRODUCT_INTEL_82598_BX	0x1508		/* 82598 */
#define	PCI_PRODUCT_INTEL_82576_NS	0x150a		/* 82576NS */
#define	PCI_PRODUCT_INTEL_82598AT2	0x150b		/* 82598AT */
#define	PCI_PRODUCT_INTEL_82583V	0x150c		/* 82583V */
#define	PCI_PRODUCT_INTEL_82576_SERDES_QUAD	0x150d		/* 82576 SerDes QP */
#define	PCI_PRODUCT_INTEL_82580_COPPER	0x150e		/* 82580 */
#define	PCI_PRODUCT_INTEL_82580_FIBER	0x150f		/* 82580 */
#define	PCI_PRODUCT_INTEL_82580_SERDES	0x1510		/* 82580 */
#define	PCI_PRODUCT_INTEL_82580_SGMII	0x1511		/* 82580 */
#define	PCI_PRODUCT_INTEL_82524EF	0x1513		/* 82524EF Thunderbolt */
#define	PCI_PRODUCT_INTEL_82599_KX4_MEZZ	0x1514		/* 82599 */
#define	PCI_PRODUCT_INTEL_X540_VF	0x1515		/* X540 VF */
#define	PCI_PRODUCT_INTEL_82580_COPPER_DUAL	0x1516		/* 82580 */
#define	PCI_PRODUCT_INTEL_82599_KR	0x1517		/* 82599 */
#define	PCI_PRODUCT_INTEL_82576_NS_SERDES	0x1518		/* 82576NS */
#define	PCI_PRODUCT_INTEL_82599_T3_LOM	0x151c		/* 82599 T3 */
#define	PCI_PRODUCT_INTEL_E823_L_QSFP	0x151d		/* E823-L QSFP */
#define	PCI_PRODUCT_INTEL_I350_COPPER	0x1521		/* I350 */
#define	PCI_PRODUCT_INTEL_I350_FIBER	0x1522		/* I350 Fiber */
#define	PCI_PRODUCT_INTEL_I350_SERDES	0x1523		/* I350 SerDes */
#define	PCI_PRODUCT_INTEL_I350_SGMII	0x1524		/* I350 SGMII */
#define	PCI_PRODUCT_INTEL_ICH10_D_BM_V	0x1525		/* ICH10 D BM V */
#define	PCI_PRODUCT_INTEL_82576_QUAD_CU_ET2	0x1526		/* 82576 */
#define	PCI_PRODUCT_INTEL_82580_QUAD_FIBER	0x1527		/* 82580 QF */
#define	PCI_PRODUCT_INTEL_X540T	0x1528		/* X540T */
#define	PCI_PRODUCT_INTEL_82599_SFP_FCOE	0x1529		/* 82599 */
#define	PCI_PRODUCT_INTEL_82599_BPLANE_FCOE	0x152a		/* 82599 */
#define	PCI_PRODUCT_INTEL_82599_VF_HV	0x152e		/* 82599 VF HV */
#define	PCI_PRODUCT_INTEL_X540_VF_HV	0x1530		/* X540 VF HV */
#define	PCI_PRODUCT_INTEL_I210_COPPER	0x1533		/* I210 */
#define	PCI_PRODUCT_INTEL_I210_COPPER_OEM1	0x1534		/* I210 */
#define	PCI_PRODUCT_INTEL_I210_COPPER_IT	0x1535		/* I210 */
#define	PCI_PRODUCT_INTEL_I210_FIBER	0x1536		/* I210 Fiber */
#define	PCI_PRODUCT_INTEL_I210_SERDES	0x1537		/* I210 SerDes */
#define	PCI_PRODUCT_INTEL_I210_SGMII	0x1538		/* I210 SGMII */
#define	PCI_PRODUCT_INTEL_I211_COPPER	0x1539		/* I211 */
#define	PCI_PRODUCT_INTEL_I217_LM	0x153a		/* I217-LM */
#define	PCI_PRODUCT_INTEL_I217_V	0x153b		/* I217-V */
#define	PCI_PRODUCT_INTEL_DSL3510	0x1547		/* DSL3510 Thunderbolt */
#define	PCI_PRODUCT_INTEL_DSL3510_PCIE	0x1549		/* DSL3510 Thunderbolt */
#define	PCI_PRODUCT_INTEL_82599_SFP_SF_QP	0x154a		/* 82599 */
#define	PCI_PRODUCT_INTEL_XL710_VF	0x154c		/* XL710/X710 VF */
#define	PCI_PRODUCT_INTEL_82599_SFP_SF2	0x154d		/* 82599 */
#define	PCI_PRODUCT_INTEL_82599EN_SFP	0x1557		/* 82599EN */
#define	PCI_PRODUCT_INTEL_82599_QSFP_SF_QP	0x1558		/* 82599 QSFP+ */
#define	PCI_PRODUCT_INTEL_I218_V	0x1559		/* I218-V */
#define	PCI_PRODUCT_INTEL_I218_LM	0x155a		/* I218-LM */
#define	PCI_PRODUCT_INTEL_X540T1	0x1560		/* X540T */
#define	PCI_PRODUCT_INTEL_X550T	0x1563		/* X550T */
#define	PCI_PRODUCT_INTEL_X550_VF_HV	0x1564		/* X550 VF HV */
#define	PCI_PRODUCT_INTEL_X550_VF	0x1565		/* X550 VF */
#define	PCI_PRODUCT_INTEL_DSL5520	0x156c		/* DSL5520 Thunderbolt */
#define	PCI_PRODUCT_INTEL_DSL5520_PCIE	0x156d		/* DSL5520 Thunderbolt */
#define	PCI_PRODUCT_INTEL_I219_LM	0x156f		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V	0x1570		/* I219-V */
#define	PCI_PRODUCT_INTEL_XL710_VF_HV	0x1571		/* XL710/X710 VF */
#define	PCI_PRODUCT_INTEL_X710_10G_SFP	0x1572		/* X710 SFP+ */
#define	PCI_PRODUCT_INTEL_I210_COPPER_NF	0x157b		/* I210 */
#define	PCI_PRODUCT_INTEL_I210_SERDES_NF	0x157c		/* I210 SerDes */
#define	PCI_PRODUCT_INTEL_XL710_40G_BP	0x1580		/* XL710 40GbE Backplane */
#define	PCI_PRODUCT_INTEL_X710_10G_BP	0x1581		/* X710 10GbE Backplane */
#define	PCI_PRODUCT_INTEL_XL710_QSFP_1	0x1583		/* XL710 QSFP+ */
#define	PCI_PRODUCT_INTEL_XL710_QSFP_2	0x1584		/* XL710 QSFP+ */
#define	PCI_PRODUCT_INTEL_X710_10G_QSFP	0x1585		/* X710 QSFP+ */
#define	PCI_PRODUCT_INTEL_X710_10G_BASET	0x1586		/* X710 10GbaseT */
#define	PCI_PRODUCT_INTEL_XL710_20G_BP_1	0x1587		/* XL710 20GbE Backplane */
#define	PCI_PRODUCT_INTEL_XL710_20G_BP_2	0x1588		/* XL710 20GbE Backplane */
#define	PCI_PRODUCT_INTEL_X710_T4_10G	0x1589		/* X710-T4 10GbaseT */
#define	PCI_PRODUCT_INTEL_XXV710_25G_BP	0x158a		/* XXV710 25GbE Backplane */
#define	PCI_PRODUCT_INTEL_XXV710_25G_SFP28	0x158b		/* XXV710 SFP28 */
#define	PCI_PRODUCT_INTEL_E810_C_QSFP	0x1592		/* E810-C QSFP */
#define	PCI_PRODUCT_INTEL_E810_C_SFP	0x1593		/* E810-C SFP */
#define	PCI_PRODUCT_INTEL_E810_XXV_QSFP	0x159a		/* E810-XXV QSFP */
#define	PCI_PRODUCT_INTEL_E810_XXV_SFP	0x159b		/* E810-XXV SFP */
#define	PCI_PRODUCT_INTEL_I218_LM_2	0x15a0		/* I218-LM */
#define	PCI_PRODUCT_INTEL_I218_V_2	0x15a1		/* I218-V */
#define	PCI_PRODUCT_INTEL_I218_LM_3	0x15a2		/* I218-LM */
#define	PCI_PRODUCT_INTEL_I218_V_3	0x15a3		/* I218-V */
#define	PCI_PRODUCT_INTEL_X550EM_X_VF	0x15a8		/* X552 VF */
#define	PCI_PRODUCT_INTEL_X550EM_X_VF_HV	0x15a9		/* X552 VF HV */
#define	PCI_PRODUCT_INTEL_X550EM_X_KX4	0x15aa		/* X552 Backplane */
#define	PCI_PRODUCT_INTEL_X550EM_X_KR	0x15ab		/* X552 Backplane */
#define	PCI_PRODUCT_INTEL_X550EM_X_SFP	0x15ac		/* X552 SFP+ */
#define	PCI_PRODUCT_INTEL_X550EM_X_10G_T	0x15ad		/* X552/X557-AT */
#define	PCI_PRODUCT_INTEL_X550EM_X_1G_T	0x15ae		/* X552 1GbaseT */
#define	PCI_PRODUCT_INTEL_X550EM_A_VF_HV	0x15b4		/* X553 VF HV */
#define	PCI_PRODUCT_INTEL_I219_LM2	0x15b7		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V2	0x15b8		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM3	0x15b9		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_LM7	0x15bb		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V7	0x15bc		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM6	0x15bd		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V6	0x15be		/* I219-V */
#define	PCI_PRODUCT_INTEL_JHL6240	0x15bf		/* JHL6240 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_JHL6240_PCIE	0x15c0		/* JHL6240 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_JHL6240_XHCI	0x15c1		/* JHL6240 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_X550EM_A_KR	0x15c2		/* X553 Backplane */
#define	PCI_PRODUCT_INTEL_X550EM_A_KR_L	0x15c3		/* X553 Backplane */
#define	PCI_PRODUCT_INTEL_X550EM_A_SFP_N	0x15c4		/* X553 SFP+ */
#define	PCI_PRODUCT_INTEL_X550EM_A_VF	0x15c5		/* X553 VF */
#define	PCI_PRODUCT_INTEL_X550EM_A_SGMII	0x15c6		/* X553 SGMII */
#define	PCI_PRODUCT_INTEL_X550EM_A_SGMII_L	0x15c7		/* X553 SGMII */
#define	PCI_PRODUCT_INTEL_X550EM_A_10G_T	0x15c8		/* X553 10GBaseT */
#define	PCI_PRODUCT_INTEL_X550EM_A_SFP	0x15ce		/* X553 SFP+ */
#define	PCI_PRODUCT_INTEL_X550T1	0x15d1		/* X550T */
#define	PCI_PRODUCT_INTEL_JHL6540	0x15d2		/* JHL6540 Thunderbolt */
#define	PCI_PRODUCT_INTEL_JHL6540_PCIE	0x15d3		/* JHL6540 Thunderbolt */
#define	PCI_PRODUCT_INTEL_JHL6540_XHCI	0x15d4		/* JHL6540 Thunderbolt */
#define	PCI_PRODUCT_INTEL_I219_V5	0x15d6		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM4	0x15d7		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V4	0x15d8		/* I219-V */
#define	PCI_PRODUCT_INTEL_JHL6340	0x15d9		/* JHL6340 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_JHL6340_PCIE	0x15da		/* JHL6340 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_JHL6340_XHCI	0x15db		/* JHL6340 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_I219_LM8	0x15df		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V8	0x15e0		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM9	0x15e1		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V9	0x15e2		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM5	0x15e3		/* I219-LM */
#define	PCI_PRODUCT_INTEL_X550EM_A_1G_T	0x15e4		/* X553 SGMII */
#define	PCI_PRODUCT_INTEL_X550EM_A_1G_T_L	0x15e5		/* X553 SGMII */
#define	PCI_PRODUCT_INTEL_JHL7340_PCIE	0x15e7		/* JHL7340 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_JHL7340	0x15e8		/* JHL7340 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_JHL7340_XHCI	0x15e9		/* JHL7340 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_JHL7540_PCIE	0x15ea		/* JHL7540 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_JHL7540	0x15eb		/* JHL7540 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_JHL7540_XHCI	0x15ec		/* JHL7540 Thunderbolt 3 */
#define	PCI_PRODUCT_INTEL_I225_LM	0x15f2		/* I225-LM */
#define	PCI_PRODUCT_INTEL_I225_V	0x15f3		/* I225-V */
#define	PCI_PRODUCT_INTEL_I219_LM15	0x15f4		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V15	0x15f5		/* I219-V */
#define	PCI_PRODUCT_INTEL_I220_V	0x15f7		/* I220-V */
#define	PCI_PRODUCT_INTEL_I225_I	0x15f8		/* I225-I */
#define	PCI_PRODUCT_INTEL_I219_LM14	0x15f9		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V14	0x15fa		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM13	0x15fb		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V13	0x15fc		/* I219-V */
#define	PCI_PRODUCT_INTEL_I225_BLANK_NVM	0x15fd		/* I225 */
#define	PCI_PRODUCT_INTEL_X710_10G_T	0x15ff		/* X710 10GBaseT */
#define	PCI_PRODUCT_INTEL_CORE5G_H_PCIE_X16	0x1601		/* Core 5G PCIE */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT1_1	0x1602		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE5G_THERM	0x1603		/* Core 5G Thermal */
#define	PCI_PRODUCT_INTEL_CORE5G_HB_1	0x1604		/* Core 5G Host */
#define	PCI_PRODUCT_INTEL_CORE5G_H_PCIE_X8	0x1605		/* Core 5G PCIE */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT1_2	0x1606		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE5G_H_PCIE_X4	0x1609		/* Core 5G PCIE */
#define	PCI_PRODUCT_INTEL_CORE5G_D_GT1_1	0x160a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT1_3	0x160b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE5G_HDA_1	0x160c		/* Core 5G HD Audio */
#define	PCI_PRODUCT_INTEL_CORE5G_D_GT1_2	0x160d		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT1_4	0x160e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE5G_H_HB	0x1610		/* Core 5G Host */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT2_1	0x1612		/* HD Graphics 5600 */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT2_2	0x1616		/* HD Graphics 5500 */
#define	PCI_PRODUCT_INTEL_CORE5G_D_GT2_1	0x161a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT2_3	0x161b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE5G_D_GT2_2	0x161d		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT2_4	0x161e		/* HD Graphics 5300 */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT3_1	0x1622		/* Iris Pro 6200 */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT3_15W	0x1626		/* HD Graphics 6000 */
#define	PCI_PRODUCT_INTEL_CORE5G_D_GT3_1	0x162a		/* Iris Pro P6300 */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT3_28W	0x162b		/* Iris 6100 */
#define	PCI_PRODUCT_INTEL_CORE5G_D_GT3_2	0x162d		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE5G_M_GT3_4	0x162e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_BDW_RSVD_1	0x1632		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_BDW_ULT_RSVD_1	0x1636		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_BDW_RSVD_2	0x163a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_BDW_ULT_RSVD_2	0x163b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_BDW_RSVD_3	0x163d		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_BDW_ULX_RSVD_1	0x163e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_ADAPTIVE_VF	0x1889		/* Adaptive VF */
#define	PCI_PRODUCT_INTEL_CORE6G_H_HB_D	0x1900		/* Core 6G Host */
#define	PCI_PRODUCT_INTEL_CORE6G_H_PCIE_X16	0x1901		/* Core 6G PCIE */
#define	PCI_PRODUCT_INTEL_CORE6G_S_GT1_1	0x1902		/* HD Graphics 510 */
#define	PCI_PRODUCT_INTEL_CORE6G_THERM	0x1903		/* Core 6G Thermal */
#define	PCI_PRODUCT_INTEL_CORE6G_U_HB	0x1904		/* Core 6G Host */
#define	PCI_PRODUCT_INTEL_CORE6G_H_PCIE_X8	0x1905		/* Core 6G PCIE */
#define	PCI_PRODUCT_INTEL_CORE6G_U_GT1_1	0x1906		/* HD Graphics 510 */
#define	PCI_PRODUCT_INTEL_CORE6G_H_PCIE_X4	0x1909		/* Core 6G PCIE */
#define	PCI_PRODUCT_INTEL_CORE6G_S_GT1_2	0x190a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE6G_H_GT1	0x190b		/* HD Graphics 510 */
#define	PCI_PRODUCT_INTEL_CORE6G_Y_HB	0x190c		/* Core 6G Host */
#define	PCI_PRODUCT_INTEL_CORE6G_U_GT1_2	0x190e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE6G_S_HB_DUAL	0x190f		/* Core 6G Host */
#define	PCI_PRODUCT_INTEL_CORE6G_H_HB_Q	0x1910		/* Core 6G Host */
#define	PCI_PRODUCT_INTEL_CORE_GMM_1	0x1911		/* Core GMM */
#define	PCI_PRODUCT_INTEL_CORE6G_S_GT2_1	0x1912		/* HD Graphics 530 */
#define	PCI_PRODUCT_INTEL_SKL_ULT_GT	0x1913		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_SKL_ULX_GT	0x1915		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE6G_U_GT2	0x1916		/* HD Graphics 520 */
#define	PCI_PRODUCT_INTEL_SKL_GT1	0x1917		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_XEONE3_1200V5_HB	0x1918		/* Xeon E3-1200 v5 Host */
#define	PCI_PRODUCT_INTEL_CORE6G_S_GT2_2	0x191a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE6G_H_GT2	0x191b		/* HD Graphics 530 */
#define	PCI_PRODUCT_INTEL_XEONE3_1200V5_GT2	0x191d		/* HD Graphics P530 */
#define	PCI_PRODUCT_INTEL_CORE6G_Y_GT2	0x191e		/* HD Graphics 515 */
#define	PCI_PRODUCT_INTEL_CORE6G_HB	0x191f		/* Core 6G Host */
#define	PCI_PRODUCT_INTEL_CORE6G_U_GT2F	0x1921		/* HD Graphics 520 */
#define	PCI_PRODUCT_INTEL_CORE6G_U_GT3_1	0x1923		/* HD Graphics 535 */
#define	PCI_PRODUCT_INTEL_CORE6G_U_GT3_2	0x1926		/* Iris 540/550 */
#define	PCI_PRODUCT_INTEL_CORE6G_U_GT3E	0x1927		/* Iris 550 */
#define	PCI_PRODUCT_INTEL_CORE6G_S_GT4	0x192a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE6G_H_GT3	0x192b		/* Iris 555 */
#define	PCI_PRODUCT_INTEL_CORE6G_S_GT3	0x192d		/* Iris P555 */
#define	PCI_PRODUCT_INTEL_CORE6G_D_GT4	0x1932		/* Iris Pro 580 */
#define	PCI_PRODUCT_INTEL_CORE6G_S_GT4E	0x193a		/* Iris Pro P580 */
#define	PCI_PRODUCT_INTEL_CORE6G_H_GT4	0x193b		/* Iris Pro 580 */
#define	PCI_PRODUCT_INTEL_CORE6G_S_GT4_2	0x193d		/* Iris Pro P580 */
#define	PCI_PRODUCT_INTEL_80960RP_ATU	0x1960		/* 80960RP ATU */
#define	PCI_PRODUCT_INTEL_C3000_HB_1	0x1980		/* C3000 Host */
#define	PCI_PRODUCT_INTEL_C3000_GLREG	0x19a1		/* C3000 GLREG */
#define	PCI_PRODUCT_INTEL_C3000_RCEC	0x19a2		/* C3000 RCEC */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_1	0x19a3		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_2	0x19a4		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_3	0x19a5		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_4	0x19a6		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_5	0x19a7		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_6	0x19a8		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_7	0x19a9		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_8	0x19aa		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_9	0x19ab		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_SMB_1	0x19ac		/* C3000 SMBus */
#define	PCI_PRODUCT_INTEL_C3000_AHCI_1	0x19b2		/* C3000 AHCI */
#define	PCI_PRODUCT_INTEL_C3000_AHCI_2	0x19c2		/* C3000 AHCI */
#define	PCI_PRODUCT_INTEL_C3000_XHCI	0x19d0		/* C3000 xHCI */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_10	0x19d1		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_PCIE_11	0x19d2		/* C3000 PCIE */
#define	PCI_PRODUCT_INTEL_C3000_ME_HECI_1	0x19d3		/* C3000 ME HECI */
#define	PCI_PRODUCT_INTEL_C3000_ME_HECI_2	0x19d4		/* C3000 ME HECI */
#define	PCI_PRODUCT_INTEL_C3000_ME_KT	0x19d5		/* C3000 ME KT */
#define	PCI_PRODUCT_INTEL_C3000_ME_HECI_3	0x19d6		/* C3000 ME HECI */
#define	PCI_PRODUCT_INTEL_C3000_HSUART	0x19d8		/* C3000 UART */
#define	PCI_PRODUCT_INTEL_C3000_EMMC	0x19db		/* C3000 eMMC */
#define	PCI_PRODUCT_INTEL_C3000_LPC	0x19dc		/* C3000 LPC */
#define	PCI_PRODUCT_INTEL_C3000_PMC	0x19de		/* C3000 PMC */
#define	PCI_PRODUCT_INTEL_C3000_SMB_2	0x19df		/* C3000 SMBus */
#define	PCI_PRODUCT_INTEL_C3000_SPI	0x19e0		/* C3000 SPI */
#define	PCI_PRODUCT_INTEL_C3000_QAT	0x19e2		/* C3000 QAT */
#define	PCI_PRODUCT_INTEL_I219_LM17	0x1a1c		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V17	0x1a1d		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM16	0x1a1e		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V16	0x1a1f		/* I219-V */
#define	PCI_PRODUCT_INTEL_82840_HB	0x1a21		/* 82840 Host */
#define	PCI_PRODUCT_INTEL_82840_AGP	0x1a23		/* 82840 AGP */
#define	PCI_PRODUCT_INTEL_82840_PCI	0x1a24		/* 82840 PCI */
#define	PCI_PRODUCT_INTEL_82845_HB	0x1a30		/* 82845 Host */
#define	PCI_PRODUCT_INTEL_82845_AGP	0x1a31		/* 82845 AGP */
#define	PCI_PRODUCT_INTEL_IOAT	0x1a38		/* I/OAT */
#define	PCI_PRODUCT_INTEL_82597EX_SR	0x1a48		/* 82597EX */
#define	PCI_PRODUCT_INTEL_BXT_IGD_2	0x1a84		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_BXT_IGD_3	0x1a85		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_82597EX_LR	0x1b48		/* 82597EX */
#define	PCI_PRODUCT_INTEL_C741_ESPI	0x1b81		/* C741 eSPI */
#define	PCI_PRODUCT_INTEL_C740_PCIE_8	0x1bb0		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_9	0x1bb1		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_10	0x1bb2		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_11	0x1bb3		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_12	0x1bb4		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_13	0x1bb5		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_0	0x1bb8		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_1	0x1bb9		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_2	0x1bba		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_3	0x1bbb		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_4	0x1bbc		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_5	0x1bbd		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_6	0x1bbe		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_PCIE_7	0x1bbf		/* C740 PCIE */
#define	PCI_PRODUCT_INTEL_C740_P2SB	0x1bc6		/* C740 P2SB */
#define	PCI_PRODUCT_INTEL_C740_PMC_1	0x1bc7		/* C740 PMC */
#define	PCI_PRODUCT_INTEL_C740_SMB	0x1bc9		/* C740 SMBus */
#define	PCI_PRODUCT_INTEL_C740_SPI	0x1bca		/* C740 SPI */
#define	PCI_PRODUCT_INTEL_C740_TH	0x1bcc		/* C740 TH */
#define	PCI_PRODUCT_INTEL_C740_XHCI	0x1bcd		/* C740 xHCI */
#define	PCI_PRODUCT_INTEL_C740_PMC_2	0x1bce		/* C740 PMC */
#define	PCI_PRODUCT_INTEL_C740_AHCI_1	0x1bd2		/* C740 AHCI */
#define	PCI_PRODUCT_INTEL_C740_MROM	0x1be6		/* C740 MROM */
#define	PCI_PRODUCT_INTEL_C740_AHCI_2	0x1bf2		/* C740 AHCI */
#define	PCI_PRODUCT_INTEL_C740_DMA_SMB	0x1bff		/* C740 DMA SMBus */
#define	PCI_PRODUCT_INTEL_6SERIES_SATA_1	0x1c00		/* 6 Series SATA */
#define	PCI_PRODUCT_INTEL_6SERIES_SATA_2	0x1c01		/* 6 Series SATA */
#define	PCI_PRODUCT_INTEL_6SERIES_AHCI_1	0x1c02		/* 6 Series AHCI */
#define	PCI_PRODUCT_INTEL_6SERIES_AHCI_2	0x1c03		/* 6 Series AHCI */
#define	PCI_PRODUCT_INTEL_6SERIES_RAID_1	0x1c04		/* 6 Series RAID */
#define	PCI_PRODUCT_INTEL_6SERIES_RAID_2	0x1c05		/* 6 Series RAID */
#define	PCI_PRODUCT_INTEL_6SERIES_RAID_3	0x1c06		/* 6 Series RAID */
#define	PCI_PRODUCT_INTEL_6SERIES_SATA_3	0x1c08		/* 6 Series SATA */
#define	PCI_PRODUCT_INTEL_6SERIES_SATA_4	0x1c09		/* 6 Series SATA */
#define	PCI_PRODUCT_INTEL_6SERIES_PCIE_1	0x1c10		/* 6 Series PCIE */
#define	PCI_PRODUCT_INTEL_6SERIES_PCIE_2	0x1c12		/* 6 Series PCIE */
#define	PCI_PRODUCT_INTEL_6SERIES_PCIE_3	0x1c14		/* 6 Series PCIE */
#define	PCI_PRODUCT_INTEL_6SERIES_PCIE_4	0x1c16		/* 6 Series PCIE */
#define	PCI_PRODUCT_INTEL_6SERIES_PCIE_5	0x1c18		/* 6 Series PCIE */
#define	PCI_PRODUCT_INTEL_6SERIES_PCIE_6	0x1c1a		/* 6 Series PCIE */
#define	PCI_PRODUCT_INTEL_6SERIES_PCIE_7	0x1c1c		/* 6 Series PCIE */
#define	PCI_PRODUCT_INTEL_6SERIES_PCIE_8	0x1c1e		/* 6 Series PCIE */
#define	PCI_PRODUCT_INTEL_6SERIES_HDA	0x1c20		/* 6 Series HD Audio */
#define	PCI_PRODUCT_INTEL_6SERIES_SMB	0x1c22		/* 6 Series SMBus */
#define	PCI_PRODUCT_INTEL_6SERIES_THERM	0x1c24		/* 6 Series Thermal */
#define	PCI_PRODUCT_INTEL_6SERIES_DMI	0x1c25		/* 6 Series DMI-PCI */
#define	PCI_PRODUCT_INTEL_6SERIES_EHCI_1	0x1c26		/* 6 Series USB */
#define	PCI_PRODUCT_INTEL_6SERIES_UHCI_1	0x1c27		/* 6 Series USB */
#define	PCI_PRODUCT_INTEL_6SERIES_UHCI_2	0x1c2c		/* 6 Series USB */
#define	PCI_PRODUCT_INTEL_6SERIES_EHCI_2	0x1c2d		/* 6 Series USB */
#define	PCI_PRODUCT_INTEL_6SERIES_MEI	0x1c3a		/* 6 Series MEI */
#define	PCI_PRODUCT_INTEL_6SERIES_KT	0x1c3d		/* 6 Series KT */
#define	PCI_PRODUCT_INTEL_Z68_LPC	0x1c44		/* Z68 LPC */
#define	PCI_PRODUCT_INTEL_P67_LPC	0x1c46		/* P67 LPC */
#define	PCI_PRODUCT_INTEL_UM67_LPC	0x1c47		/* UM67 LPC */
#define	PCI_PRODUCT_INTEL_HM65_LPC	0x1c49		/* HM65 LPC */
#define	PCI_PRODUCT_INTEL_H67_LPC	0x1c4a		/* H67 LPC */
#define	PCI_PRODUCT_INTEL_HM67_LPC	0x1c4b		/* HM67 LPC */
#define	PCI_PRODUCT_INTEL_Q65_LPC	0x1c4c		/* Q65 LPC */
#define	PCI_PRODUCT_INTEL_QS67_LPC	0x1c4d		/* QS67 LPC */
#define	PCI_PRODUCT_INTEL_Q67_LPC	0x1c4e		/* Q67 LPC */
#define	PCI_PRODUCT_INTEL_QM67_LPC	0x1c4f		/* QM67 LPC */
#define	PCI_PRODUCT_INTEL_B65_LPC	0x1c50		/* B65 LPC */
#define	PCI_PRODUCT_INTEL_C202_LPC	0x1c52		/* C202 LPC */
#define	PCI_PRODUCT_INTEL_C204_LPC	0x1c54		/* C204 LPC */
#define	PCI_PRODUCT_INTEL_C206_LPC	0x1c56		/* C206 LPC */
#define	PCI_PRODUCT_INTEL_H61_LPC	0x1c5c		/* H61 LPC */
#define	PCI_PRODUCT_INTEL_C600_SATA	0x1d00		/* C600 SATA */
#define	PCI_PRODUCT_INTEL_C600_AHCI	0x1d02		/* C600 AHCI */
#define	PCI_PRODUCT_INTEL_C600_RAID_1	0x1d04		/* C600 RAID */
#define	PCI_PRODUCT_INTEL_C600_RAID_2	0x1d06		/* C600 RAID */
#define	PCI_PRODUCT_INTEL_C600_PCIE_1	0x1d10		/* C600 PCIE */
#define	PCI_PRODUCT_INTEL_C600_PCIE_2	0x1d12		/* C600 PCIE */
#define	PCI_PRODUCT_INTEL_C600_PCIE_3	0x1d14		/* C600 PCIE */
#define	PCI_PRODUCT_INTEL_C600_PCIE_4	0x1d16		/* C600 PCIE */
#define	PCI_PRODUCT_INTEL_C600_PCIE_5	0x1d18		/* C600 PCIE */
#define	PCI_PRODUCT_INTEL_C600_PCIE_6	0x1d1a		/* C600 PCIE */
#define	PCI_PRODUCT_INTEL_C600_PCIE_7	0x1d1c		/* C600 PCIE */
#define	PCI_PRODUCT_INTEL_C600_PCIE_8	0x1d1e		/* C600 PCIE */
#define	PCI_PRODUCT_INTEL_C600_HDA	0x1d20		/* C600 HD Audio */
#define	PCI_PRODUCT_INTEL_C600_SMB	0x1d22		/* C600 SMBus */
#define	PCI_PRODUCT_INTEL_C600_THERM	0x1d24		/* C600 Thermal */
#define	PCI_PRODUCT_INTEL_C600_EHCI_1	0x1d26		/* C600 USB */
#define	PCI_PRODUCT_INTEL_C600_EHCI_2	0x1d2d		/* C600 USB */
#define	PCI_PRODUCT_INTEL_C600_LAN	0x1d33		/* C600 LAN */
#define	PCI_PRODUCT_INTEL_C600_MEI_1	0x1d3a		/* C600 MEI */
#define	PCI_PRODUCT_INTEL_C600_MEI_2	0x1d3b		/* C600 MEI */
#define	PCI_PRODUCT_INTEL_C600_VPCIE	0x1d3e		/* C600 Virtual PCIE */
#define	PCI_PRODUCT_INTEL_C600_LPC	0x1d41		/* C600 LPC */
#define	PCI_PRODUCT_INTEL_C600_SMB_IDF_1	0x1d70		/* C600 SMBus */
#define	PCI_PRODUCT_INTEL_C600_SMB_IDF_2	0x1d71		/* C600 SMBus */
#define	PCI_PRODUCT_INTEL_C600_SMB_IDF_3	0x1d72		/* C600 SMBus */
#define	PCI_PRODUCT_INTEL_7SERIES_SATA_1	0x1e00		/* 7 Series SATA */
#define	PCI_PRODUCT_INTEL_7SERIES_SATA_2	0x1e01		/* 7 Series SATA */
#define	PCI_PRODUCT_INTEL_7SERIES_AHCI_1	0x1e02		/* 7 Series AHCI */
#define	PCI_PRODUCT_INTEL_7SERIES_AHCI_2	0x1e03		/* 7 Series AHCI */
#define	PCI_PRODUCT_INTEL_7SERIES_RAID_1	0x1e04		/* 7 Series RAID */
#define	PCI_PRODUCT_INTEL_7SERIES_RAID_2	0x1e06		/* 7 Series RAID */
#define	PCI_PRODUCT_INTEL_7SERIES_RAID_3	0x1e07		/* 7 Series RAID */
#define	PCI_PRODUCT_INTEL_7SERIES_SATA_3	0x1e08		/* 7 Series SATA */
#define	PCI_PRODUCT_INTEL_7SERIES_SATA_4	0x1e09		/* 7 Series SATA */
#define	PCI_PRODUCT_INTEL_7SERIES_RAID_4	0x1e0e		/* 7 Series RAID */
#define	PCI_PRODUCT_INTEL_7SERIES_PCIE_1	0x1e10		/* 7 Series PCIE */
#define	PCI_PRODUCT_INTEL_7SERIES_PCIE_2	0x1e12		/* 7 Series PCIE */
#define	PCI_PRODUCT_INTEL_7SERIES_PCIE_3	0x1e14		/* 7 Series PCIE */
#define	PCI_PRODUCT_INTEL_7SERIES_PCIE_4	0x1e16		/* 7 Series PCIE */
#define	PCI_PRODUCT_INTEL_7SERIES_PCIE_5	0x1e18		/* 7 Series PCIE */
#define	PCI_PRODUCT_INTEL_7SERIES_PCIE_6	0x1e1a		/* 7 Series PCIE */
#define	PCI_PRODUCT_INTEL_7SERIES_PCIE_7	0x1e1c		/* 7 Series PCIE */
#define	PCI_PRODUCT_INTEL_7SERIES_PCIE_8	0x1e1e		/* 7 Series PCIE */
#define	PCI_PRODUCT_INTEL_7SERIES_HDA	0x1e20		/* 7 Series HD Audio */
#define	PCI_PRODUCT_INTEL_7SERIES_SMB	0x1e22		/* 7 Series SMBus */
#define	PCI_PRODUCT_INTEL_7SERIES_THERM	0x1e24		/* 7 Series Thermal */
#define	PCI_PRODUCT_INTEL_7SERIES_EHCI_1	0x1e26		/* 7 Series USB */
#define	PCI_PRODUCT_INTEL_7SERIES_EHCI_2	0x1e2d		/* 7 Series USB */
#define	PCI_PRODUCT_INTEL_7SERIES_XHCI	0x1e31		/* 7 Series xHCI */
#define	PCI_PRODUCT_INTEL_7SERIES_MEI_1	0x1e3a		/* 7 Series MEI */
#define	PCI_PRODUCT_INTEL_7SERIES_MEI_2	0x1e3b		/* 7 Series MEI */
#define	PCI_PRODUCT_INTEL_7SERIES_KT	0x1e3d		/* 7 Series KT */
#define	PCI_PRODUCT_INTEL_Z77_LPC	0x1e44		/* Z77 LPC */
#define	PCI_PRODUCT_INTEL_Z75_LPC	0x1e46		/* Z75 LPC */
#define	PCI_PRODUCT_INTEL_B75_LPC	0x1e49		/* B75 LPC */
#define	PCI_PRODUCT_INTEL_H77_LPC	0x1e4a		/* H77 LPC */
#define	PCI_PRODUCT_INTEL_C216_LPC	0x1e53		/* C216 LPC */
#define	PCI_PRODUCT_INTEL_QM77_LPC	0x1e55		/* QM77 LPC */
#define	PCI_PRODUCT_INTEL_QS77_LPC	0x1e56		/* QS77 LPC */
#define	PCI_PRODUCT_INTEL_HM77_LPC	0x1e57		/* HM77 LPC */
#define	PCI_PRODUCT_INTEL_UM77_LPC	0x1e58		/* UM77 LPC */
#define	PCI_PRODUCT_INTEL_HM76_LPC	0x1e59		/* HM76 LPC */
#define	PCI_PRODUCT_INTEL_HM75_LPC	0x1e5d		/* HM75 LPC */
#define	PCI_PRODUCT_INTEL_HM70_LPC	0x1e5e		/* HM70 LPC */
#define	PCI_PRODUCT_INTEL_NM70_LPC	0x1e5f		/* NM70 LPC */
#define	PCI_PRODUCT_INTEL_ATOMC2000_HB_1	0x1f02		/* Atom C2000 Host */
#define	PCI_PRODUCT_INTEL_ATOMC2000_HB_2	0x1f08		/* Atom C2000 Host */
#define	PCI_PRODUCT_INTEL_ATOMC2000_HB_3	0x1f0b		/* Atom C2000 Host */
#define	PCI_PRODUCT_INTEL_ATOMC2000_PCIE_1	0x1f10		/* Atom C2000 PCIE */
#define	PCI_PRODUCT_INTEL_ATOMC2000_PCIE_2	0x1f11		/* Atom C2000 PCIE */
#define	PCI_PRODUCT_INTEL_ATOMC2000_PCIE_3	0x1f12		/* Atom C2000 PCIE */
#define	PCI_PRODUCT_INTEL_ATOMC2000_PCIE_4	0x1f13		/* Atom C2000 PCIE */
#define	PCI_PRODUCT_INTEL_ATOMC2000_RAS	0x1f14		/* Atom C2000 RAS */
#define	PCI_PRODUCT_INTEL_ATOMC2000_SMB	0x1f15		/* Atom C2000 SMBus */
#define	PCI_PRODUCT_INTEL_ATOMC2000_RCEC	0x1f16		/* Atom C2000 RCEC */
#define	PCI_PRODUCT_INTEL_ATOMC2000_SATA_1	0x1f20		/* Atom C2000 SATA */
#define	PCI_PRODUCT_INTEL_ATOMC2000_SATA_2	0x1f21		/* Atom C2000 SATA */
#define	PCI_PRODUCT_INTEL_ATOMC2000_AHCI_1	0x1f22		/* Atom C2000 AHCI */
#define	PCI_PRODUCT_INTEL_ATOMC2000_AHCI_2	0x1f23		/* Atom C2000 AHCI */
#define	PCI_PRODUCT_INTEL_ATOMC2000_EHCI	0x1f2c		/* Atom C2000 USB */
#define	PCI_PRODUCT_INTEL_ATOMC2000_SATA_3	0x1f30		/* Atom C2000 SATA */
#define	PCI_PRODUCT_INTEL_ATOMC2000_SATA_4	0x1f31		/* Atom C2000 SATA */
#define	PCI_PRODUCT_INTEL_ATOMC2000_AHCI_3	0x1f32		/* Atom C2000 AHCI */
#define	PCI_PRODUCT_INTEL_ATOMC2000_AHCI_4	0x1f33		/* Atom C2000 AHCI */
#define	PCI_PRODUCT_INTEL_ATOMC2000_PCU_1	0x1f38		/* Atom C2000 PCU */
#define	PCI_PRODUCT_INTEL_ATOMC2000_PCU_2	0x1f39		/* Atom C2000 PCU */
#define	PCI_PRODUCT_INTEL_ATOMC2000_PCU_3	0x1f3a		/* Atom C2000 PCU */
#define	PCI_PRODUCT_INTEL_ATOMC2000_PCU_4	0x1f3b		/* Atom C2000 PCU */
#define	PCI_PRODUCT_INTEL_ATOMC2000_PCU_SMB	0x1f3c		/* Atom C2000 PCU SMBus */
#define	PCI_PRODUCT_INTEL_I354_BP_1GBPS	0x1f40		/* I354 */
#define	PCI_PRODUCT_INTEL_I354_SGMII	0x1f41		/* I354 SGMII */
#define	PCI_PRODUCT_INTEL_I354_BP_2_5GBPS	0x1f45		/* I354 */
#define	PCI_PRODUCT_INTEL_XEONS_UBOX_1	0x2014		/* Xeon Scalable Ubox */
#define	PCI_PRODUCT_INTEL_XEONS_UBOX_2	0x2015		/* Xeon Scalable Ubox */
#define	PCI_PRODUCT_INTEL_XEONS_UBOX_3	0x2016		/* Xeon Scalable Ubox */
#define	PCI_PRODUCT_INTEL_XEONS_M2PCI	0x2018		/* Xeon Scalable M2PCI */
#define	PCI_PRODUCT_INTEL_XEONS_HB	0x2020		/* Xeon Scalable Host */
#define	PCI_PRODUCT_INTEL_XEONS_CBDMA	0x2021		/* Xeon Scalable CBDMA */
#define	PCI_PRODUCT_INTEL_XEONS_VTD_1	0x2024		/* Xeon Scalable VT-d */
#define	PCI_PRODUCT_INTEL_XEONS_RAS_1	0x2025		/* Xeon Scalable RAS */
#define	PCI_PRODUCT_INTEL_XEONS_IOAPIC	0x2026		/* Xeon Scalable I/O APIC */
#define	PCI_PRODUCT_INTEL_XEONS_PCIE_1	0x2030		/* Xeon Scalable PCIE */
#define	PCI_PRODUCT_INTEL_XEONS_PCIE_2	0x2031		/* Xeon Scalable PCIE */
#define	PCI_PRODUCT_INTEL_XEONS_PCIE_3	0x2032		/* Xeon Scalable PCIE */
#define	PCI_PRODUCT_INTEL_XEONS_PCIE_4	0x2033		/* Xeon Scalable PCIE */
#define	PCI_PRODUCT_INTEL_XEONS_VTD_2	0x2034		/* Xeon Scalable VT-d */
#define	PCI_PRODUCT_INTEL_XEONS_RAS_2	0x2035		/* Xeon Scalable RAS */
#define	PCI_PRODUCT_INTEL_XEONS_IOXAPIC	0x2036		/* Xeon Scalable IOxAPIC */
#define	PCI_PRODUCT_INTEL_XEONS_IMC_1	0x2040		/* Xeon Scalable IMC */
#define	PCI_PRODUCT_INTEL_XEONS_IMC_2	0x2041		/* Xeon Scalable IMC */
#define	PCI_PRODUCT_INTEL_XEONS_IMC_3	0x2042		/* Xeon Scalable IMC */
#define	PCI_PRODUCT_INTEL_XEONS_IMC_4	0x2043		/* Xeon Scalable IMC */
#define	PCI_PRODUCT_INTEL_XEONS_IMC_5	0x2044		/* Xeon Scalable IMC */
#define	PCI_PRODUCT_INTEL_XEONS_LM_C1	0x2045		/* Xeon Scalable LM */
#define	PCI_PRODUCT_INTEL_XEONS_LMS_C1	0x2046		/* Xeon Scalable LMS */
#define	PCI_PRODUCT_INTEL_XEONS_LMDP_C1	0x2047		/* Xeon Scalable LMDP */
#define	PCI_PRODUCT_INTEL_XEONS_DECS_C2	0x2048		/* Xeon Scalable DECS */
#define	PCI_PRODUCT_INTEL_XEONS_LM_C2	0x2049		/* Xeon Scalable LM */
#define	PCI_PRODUCT_INTEL_XEONS_LMS_C2	0x204a		/* Xeon Scalable LMS */
#define	PCI_PRODUCT_INTEL_XEONS_LMDP_C2	0x204b		/* Xeon Scalable LMDP */
#define	PCI_PRODUCT_INTEL_XEONS_M3KTI_1	0x204c		/* Xeon Scalable M3KTI */
#define	PCI_PRODUCT_INTEL_XEONS_M3KTI_2	0x204d		/* Xeon Scalable M3KTI */
#define	PCI_PRODUCT_INTEL_XEONS_M3KTI_3	0x204e		/* Xeon Scalable M3KTI */
#define	PCI_PRODUCT_INTEL_XEONS_CHA_1	0x2054		/* Xeon Scalable CHA */
#define	PCI_PRODUCT_INTEL_XEONS_CHA_2	0x2055		/* Xeon Scalable CHA */
#define	PCI_PRODUCT_INTEL_XEONS_CHA_3	0x2056		/* Xeon Scalable CHA */
#define	PCI_PRODUCT_INTEL_XEONS_CHA_4	0x2057		/* Xeon Scalable CHA */
#define	PCI_PRODUCT_INTEL_XEONS_KTI	0x2058		/* Xeon Scalable KTI */
#define	PCI_PRODUCT_INTEL_XEONS_UPI	0x2059		/* Xeon Scalable UPI */
#define	PCI_PRODUCT_INTEL_XEONS_IMC	0x2066		/* Xeon Scalable IMC */
#define	PCI_PRODUCT_INTEL_XEONS_DDRIO_1	0x2068		/* Xeon Scalable DDRIO */
#define	PCI_PRODUCT_INTEL_XEONS_DDRIO_2	0x2069		/* Xeon Scalable DDRIO */
#define	PCI_PRODUCT_INTEL_XEONS_DDRIO_3	0x206a		/* Xeon Scalable DDRIO */
#define	PCI_PRODUCT_INTEL_XEONS_DDRIO_4	0x206b		/* Xeon Scalable DDRIO */
#define	PCI_PRODUCT_INTEL_XEONS_DDRIO_5	0x206c		/* Xeon Scalable DDRIO */
#define	PCI_PRODUCT_INTEL_XEONS_DDRIO_6	0x206d		/* Xeon Scalable DDRIO */
#define	PCI_PRODUCT_INTEL_XEONS_DDRIO_7	0x206e		/* Xeon Scalable DDRIO */
#define	PCI_PRODUCT_INTEL_XEONS_PCU_1	0x2080		/* Xeon Scalable PCU */
#define	PCI_PRODUCT_INTEL_XEONS_PCU_2	0x2081		/* Xeon Scalable PCU */
#define	PCI_PRODUCT_INTEL_XEONS_PCU_3	0x2082		/* Xeon Scalable PCU */
#define	PCI_PRODUCT_INTEL_XEONS_PCU_4	0x2083		/* Xeon Scalable PCU */
#define	PCI_PRODUCT_INTEL_XEONS_PCU_5	0x2084		/* Xeon Scalable PCU */
#define	PCI_PRODUCT_INTEL_XEONS_PCU_6	0x2085		/* Xeon Scalable PCU */
#define	PCI_PRODUCT_INTEL_XEONS_PCU_7	0x2086		/* Xeon Scalable PCU */
#define	PCI_PRODUCT_INTEL_XEONS_M2PCIE	0x2088		/* Xeon Scalable M2PCIe */
#define	PCI_PRODUCT_INTEL_XEONS_CHA_5	0x208d		/* Xeon Scalable CHA */
#define	PCI_PRODUCT_INTEL_XEONS_CHA_6	0x208e		/* Xeon Scalable CHA */
#define	PCI_PRODUCT_INTEL_BSW_HB	0x2280		/* Braswell Host */
#define	PCI_PRODUCT_INTEL_BSW_HDA	0x2284		/* Braswell HD Audio */
#define	PCI_PRODUCT_INTEL_BSW_SIO_DMA_2	0x2286		/* Braswell SIO DMA */
#define	PCI_PRODUCT_INTEL_BSW_SIO_HSUART_1	0x228a		/* Braswell Serial */
#define	PCI_PRODUCT_INTEL_BSW_SIO_HSUART_2	0x228c		/* Braswell Serial */
#define	PCI_PRODUCT_INTEL_BRASWELL_SMB	0x2292		/* Braswell SMBus */
#define	PCI_PRODUCT_INTEL_BSW_SDIO	0x2295		/* Braswell SDIO */
#define	PCI_PRODUCT_INTEL_BSW_TXE	0x2298		/* Braswell TXE */
#define	PCI_PRODUCT_INTEL_BSW_PCU_LPC	0x229c		/* Braswell PCU LPC */
#define	PCI_PRODUCT_INTEL_BSW_AHCI	0x22a3		/* Braswell AHCI */
#define	PCI_PRODUCT_INTEL_BSW_AUDIO	0x22a8		/* Braswell LPE Audio */
#define	PCI_PRODUCT_INTEL_CHV_IGD_1	0x22b0		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CHV_IGD_2	0x22b1		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CHV_IGD_3	0x22b2		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CHV_IGD_4	0x22b3		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_BSW_XHCI	0x22b5		/* Braswell xHCI */
#define	PCI_PRODUCT_INTEL_BSW_USB_OTG	0x22b7		/* Braswell USB OTG */
#define	PCI_PRODUCT_INTEL_BSW_ISP	0x22b8		/* Braswell Camera ISP */
#define	PCI_PRODUCT_INTEL_BSW_SIO_DMA_1	0x22c0		/* Braswell SIO DMA */
#define	PCI_PRODUCT_INTEL_BSW_SIO_I2C_1	0x22c1		/* Braswell SIO I2C */
#define	PCI_PRODUCT_INTEL_BSW_SIO_I2C_2	0x22c2		/* Braswell SIO I2C */
#define	PCI_PRODUCT_INTEL_BSW_SIO_I2C_3	0x22c3		/* Braswell SIO I2C */
#define	PCI_PRODUCT_INTEL_BSW_SIO_I2C_4	0x22c4		/* Braswell SIO I2C */
#define	PCI_PRODUCT_INTEL_BSW_SIO_I2C_5	0x22c5		/* Braswell SIO I2C */
#define	PCI_PRODUCT_INTEL_BSW_SIO_I2C_6	0x22c6		/* Braswell SIO I2C */
#define	PCI_PRODUCT_INTEL_BSW_SIO_I2C_7	0x22c7		/* Braswell SIO I2C */
#define	PCI_PRODUCT_INTEL_BSW_PCIE_1	0x22c8		/* Braswell PCIE */
#define	PCI_PRODUCT_INTEL_BSW_PCIE_2	0x22ca		/* Braswell PCIE */
#define	PCI_PRODUCT_INTEL_BSW_PCIE_3	0x22cc		/* Braswell PCIE */
#define	PCI_PRODUCT_INTEL_BSW_PCIE_4	0x22ce		/* Braswell PCIE */
#define	PCI_PRODUCT_INTEL_BSW_SENSOR	0x22d8		/* Braswell Sensor Hub */
#define	PCI_PRODUCT_INTEL_BSW_PM	0x22dc		/* Braswell Power */
#define	PCI_PRODUCT_INTEL_DH8900_LPC	0x2310		/* DH8900 LPC */
#define	PCI_PRODUCT_INTEL_DH8900_AHCI	0x2323		/* DH8900 AHCI */
#define	PCI_PRODUCT_INTEL_DH8900_SATA_1	0x2326		/* DH8900 SATA */
#define	PCI_PRODUCT_INTEL_DH8900_SMB	0x2330		/* DH8900 SMBus */
#define	PCI_PRODUCT_INTEL_DH8900_TERM	0x2332		/* DH8900 Thermal */
#define	PCI_PRODUCT_INTEL_DH8900_EHCI_1	0x2334		/* DH8900 USB */
#define	PCI_PRODUCT_INTEL_DH8900_EHCI_2	0x2335		/* DH8900 USB */
#define	PCI_PRODUCT_INTEL_DH8900_PCIE_1	0x2342		/* DH8900 PCIE */
#define	PCI_PRODUCT_INTEL_DH8900_PCIE_2	0x2343		/* DH8900 PCIE */
#define	PCI_PRODUCT_INTEL_DH8900_PCIE_3	0x2344		/* DH8900 PCIE */
#define	PCI_PRODUCT_INTEL_DH8900_PCIE_4	0x2345		/* DH8900 PCIE */
#define	PCI_PRODUCT_INTEL_DH8900_PCIE_5	0x2346		/* DH8900 PCIE */
#define	PCI_PRODUCT_INTEL_DH8900_PCIE_6	0x2347		/* DH8900 PCIE */
#define	PCI_PRODUCT_INTEL_DH8900_PCIE_7	0x2348		/* DH8900 PCIE */
#define	PCI_PRODUCT_INTEL_DH8900_PCIE_8	0x2349		/* DH8900 PCIE */
#define	PCI_PRODUCT_INTEL_DH8900_WATCHDOG	0x2360		/* DH8900 Watchdog */
#define	PCI_PRODUCT_INTEL_DH8900_SATA_2	0x23a6		/* DH8900 SATA */
#define	PCI_PRODUCT_INTEL_82801AA_LPC	0x2410		/* 82801AA LPC */
#define	PCI_PRODUCT_INTEL_82801AA_IDE	0x2411		/* 82801AA IDE */
#define	PCI_PRODUCT_INTEL_82801AA_USB	0x2412		/* 82801AA USB */
#define	PCI_PRODUCT_INTEL_82801AA_SMB	0x2413		/* 82801AA SMBus */
#define	PCI_PRODUCT_INTEL_82801AA_ACA	0x2415		/* 82801AA AC97 */
#define	PCI_PRODUCT_INTEL_82801AA_ACM	0x2416		/* 82801AA Modem */
#define	PCI_PRODUCT_INTEL_82801AA_HPB	0x2418		/* 82801AA Hub-to-PCI */
#define	PCI_PRODUCT_INTEL_82801AB_LPC	0x2420		/* 82801AB LPC */
#define	PCI_PRODUCT_INTEL_82801AB_IDE	0x2421		/* 82801AB IDE */
#define	PCI_PRODUCT_INTEL_82801AB_USB	0x2422		/* 82801AB USB */
#define	PCI_PRODUCT_INTEL_82801AB_SMB	0x2423		/* 82801AB SMBus */
#define	PCI_PRODUCT_INTEL_82801AB_ACA	0x2425		/* 82801AB AC97 */
#define	PCI_PRODUCT_INTEL_82801AB_ACM	0x2426		/* 82801AB Modem */
#define	PCI_PRODUCT_INTEL_82801AB_HPB	0x2428		/* 82801AB Hub-to-PCI */
#define	PCI_PRODUCT_INTEL_82801BA_LPC	0x2440		/* 82801BA LPC */
#define	PCI_PRODUCT_INTEL_82801BA_USB	0x2442		/* 82801BA USB */
#define	PCI_PRODUCT_INTEL_82801BA_SMB	0x2443		/* 82801BA SMBus */
#define	PCI_PRODUCT_INTEL_82801BA_USB2	0x2444		/* 82801BA USB */
#define	PCI_PRODUCT_INTEL_82801BA_ACA	0x2445		/* 82801BA AC97 */
#define	PCI_PRODUCT_INTEL_82801BA_ACM	0x2446		/* 82801BA Modem */
#define	PCI_PRODUCT_INTEL_82801BAM_HPB	0x2448		/* 82801BAM Hub-to-PCI */
#define	PCI_PRODUCT_INTEL_82562	0x2449		/* 82562 */
#define	PCI_PRODUCT_INTEL_82801BAM_IDE	0x244a		/* 82801BAM IDE */
#define	PCI_PRODUCT_INTEL_82801BA_IDE	0x244b		/* 82801BA IDE */
#define	PCI_PRODUCT_INTEL_82801BAM_LPC	0x244c		/* 82801BAM LPC */
#define	PCI_PRODUCT_INTEL_82801BA_HPB	0x244e		/* 82801BA Hub-to-PCI */
#define	PCI_PRODUCT_INTEL_82801E_LPC	0x2450		/* 82801E LPC */
#define	PCI_PRODUCT_INTEL_82801E_USB	0x2452		/* 82801E USB */
#define	PCI_PRODUCT_INTEL_82801E_SMB	0x2453		/* 82801E SMBus */
#define	PCI_PRODUCT_INTEL_82801E_LAN_1	0x2459		/* 82801E LAN */
#define	PCI_PRODUCT_INTEL_82801E_LAN_2	0x245d		/* 82801E LAN */
#define	PCI_PRODUCT_INTEL_82801CA_LPC	0x2480		/* 82801CA LPC */
#define	PCI_PRODUCT_INTEL_82801CA_USB_1	0x2482		/* 82801CA/CAM USB */
#define	PCI_PRODUCT_INTEL_82801CA_SMB	0x2483		/* 82801CA/CAM SMBus */
#define	PCI_PRODUCT_INTEL_82801CA_USB_2	0x2484		/* 82801CA/CAM USB */
#define	PCI_PRODUCT_INTEL_82801CA_ACA	0x2485		/* 82801CA/CAM AC97 */
#define	PCI_PRODUCT_INTEL_82801CA_ACM	0x2486		/* 82801CA/CAM Modem */
#define	PCI_PRODUCT_INTEL_82801CA_USB_3	0x2487		/* 82801CA/CAM USB */
#define	PCI_PRODUCT_INTEL_82801CAM_IDE	0x248a		/* 82801CAM IDE */
#define	PCI_PRODUCT_INTEL_82801CA_IDE	0x248b		/* 82801CA IDE */
#define	PCI_PRODUCT_INTEL_82801CAM_LPC	0x248c		/* 82801CAM LPC */
#define	PCI_PRODUCT_INTEL_82801DB_LPC	0x24c0		/* 82801DB LPC */
#define	PCI_PRODUCT_INTEL_82801DBL_IDE	0x24c1		/* 82801DBL IDE */
#define	PCI_PRODUCT_INTEL_82801DB_USB_1	0x24c2		/* 82801DB USB */
#define	PCI_PRODUCT_INTEL_82801DB_SMB	0x24c3		/* 82801DB SMBus */
#define	PCI_PRODUCT_INTEL_82801DB_USB_2	0x24c4		/* 82801DB USB */
#define	PCI_PRODUCT_INTEL_82801DB_ACA	0x24c5		/* 82801DB AC97 */
#define	PCI_PRODUCT_INTEL_82801DB_ACM	0x24c6		/* 82801DB Modem */
#define	PCI_PRODUCT_INTEL_82801DB_USB_3	0x24c7		/* 82801DB USB */
#define	PCI_PRODUCT_INTEL_82801DBM_IDE	0x24ca		/* 82801DBM IDE */
#define	PCI_PRODUCT_INTEL_82801DB_IDE	0x24cb		/* 82801DB IDE */
#define	PCI_PRODUCT_INTEL_82801DBM_LPC	0x24cc		/* 82801DBM LPC */
#define	PCI_PRODUCT_INTEL_82801DB_USB_4	0x24cd		/* 82801DB USB */
#define	PCI_PRODUCT_INTEL_82801EB_LPC	0x24d0		/* 82801EB/ER LPC */
#define	PCI_PRODUCT_INTEL_82801EB_SATA	0x24d1		/* 82801EB SATA */
#define	PCI_PRODUCT_INTEL_82801EB_USB_1	0x24d2		/* 82801EB/ER USB */
#define	PCI_PRODUCT_INTEL_82801EB_SMB	0x24d3		/* 82801EB/ER SMBus */
#define	PCI_PRODUCT_INTEL_82801EB_USB_2	0x24d4		/* 82801EB/ER USB */
#define	PCI_PRODUCT_INTEL_82801EB_ACA	0x24d5		/* 82801EB/ER AC97 */
#define	PCI_PRODUCT_INTEL_82801EB_MODEM	0x24d6		/* 82801EB/ER Modem */
#define	PCI_PRODUCT_INTEL_82801EB_USB_3	0x24d7		/* 82801EB/ER USB */
#define	PCI_PRODUCT_INTEL_82801EB_IDE	0x24db		/* 82801EB/ER IDE */
#define	PCI_PRODUCT_INTEL_82801EB_USB_5	0x24dd		/* 82801EB/ER USB2 */
#define	PCI_PRODUCT_INTEL_82801EB_USB_4	0x24de		/* 82801EB/ER USB */
#define	PCI_PRODUCT_INTEL_82801ER_SATA	0x24df		/* 82801ER SATA */
#define	PCI_PRODUCT_INTEL_WL_8260_1	0x24f3		/* AC 8260 */
#define	PCI_PRODUCT_INTEL_WL_8260_2	0x24f4		/* AC 8260 */
#define	PCI_PRODUCT_INTEL_WL_4165_1	0x24f5		/* AC 4165 */
#define	PCI_PRODUCT_INTEL_WL_4165_2	0x24f6		/* AC 4165 */
#define	PCI_PRODUCT_INTEL_WL_3168_1	0x24fb		/* Dual Band Wireless-AC 3168 */
#define	PCI_PRODUCT_INTEL_WL_8265_1	0x24fd		/* Dual Band Wireless-AC 8265 */
#define	PCI_PRODUCT_INTEL_82820_HB	0x2501		/* 82820 Host */
#define	PCI_PRODUCT_INTEL_82820_AGP	0x250f		/* 82820 AGP */
#define	PCI_PRODUCT_INTEL_OPTANE	0x2522		/* Optane */
#define	PCI_PRODUCT_INTEL_P1600X	0x2525		/* P1600X */
#define	PCI_PRODUCT_INTEL_WL_9260_1	0x2526		/* Dual Band Wireless-AC 9260 */
#define	PCI_PRODUCT_INTEL_82850_HB	0x2530		/* 82850 Host */
#define	PCI_PRODUCT_INTEL_82860_HB	0x2531		/* 82860 Host */
#define	PCI_PRODUCT_INTEL_82850_AGP	0x2532		/* 82850/82860 AGP */
#define	PCI_PRODUCT_INTEL_82860_PCI1	0x2533		/* 82860 PCI */
#define	PCI_PRODUCT_INTEL_82860_PCI2	0x2534		/* 82860 PCI */
#define	PCI_PRODUCT_INTEL_82860_PCI3	0x2535		/* 82860 PCI */
#define	PCI_PRODUCT_INTEL_82860_PCI4	0x2536		/* 82860 PCI */
#define	PCI_PRODUCT_INTEL_E7500_HB	0x2540		/* E7500 Host */
#define	PCI_PRODUCT_INTEL_E7500_ERR	0x2541		/* E7500 Error Reporting */
#define	PCI_PRODUCT_INTEL_E7500_PCI_B1	0x2543		/* E7500 PCI */
#define	PCI_PRODUCT_INTEL_E7500_PCI_B2	0x2544		/* E7500 PCI */
#define	PCI_PRODUCT_INTEL_E7500_PCI_C1	0x2545		/* E7500 PCI */
#define	PCI_PRODUCT_INTEL_E7500_PCI_C2	0x2546		/* E7500 PCI */
#define	PCI_PRODUCT_INTEL_E7500_PCI_D1	0x2547		/* E7500 PCI */
#define	PCI_PRODUCT_INTEL_E7500_PCI_D2	0x2548		/* E7500 PCI */
#define	PCI_PRODUCT_INTEL_E7501_HB	0x254c		/* E7501 Host */
#define	PCI_PRODUCT_INTEL_E7505_HB	0x2550		/* E7505 Host */
#define	PCI_PRODUCT_INTEL_E7505_ERR	0x2551		/* E7505 Error Reporting */
#define	PCI_PRODUCT_INTEL_E7505_AGP	0x2552		/* E7505 AGP */
#define	PCI_PRODUCT_INTEL_E7505_PCI_B1	0x2553		/* E7505 PCI */
#define	PCI_PRODUCT_INTEL_E7505_PCI_B2	0x2554		/* E7505 PCI */
#define	PCI_PRODUCT_INTEL_82845G_HB	0x2560		/* 82845G Host */
#define	PCI_PRODUCT_INTEL_82845G_AGP	0x2561		/* 82845G AGP */
#define	PCI_PRODUCT_INTEL_82845G_IGD	0x2562		/* 82845G Video */
#define	PCI_PRODUCT_INTEL_82865G_HB	0x2570		/* 82865G Host */
#define	PCI_PRODUCT_INTEL_82865G_AGP	0x2571		/* 82865G AGP */
#define	PCI_PRODUCT_INTEL_82865G_IGD	0x2572		/* 82865G Video */
#define	PCI_PRODUCT_INTEL_82865G_CSA	0x2573		/* 82865G CSA */
#define	PCI_PRODUCT_INTEL_82865G_OVF	0x2576		/* 82865G Overflow */
#define	PCI_PRODUCT_INTEL_82875P_HB	0x2578		/* 82875P Host */
#define	PCI_PRODUCT_INTEL_82875P_AGP	0x2579		/* 82875P AGP */
#define	PCI_PRODUCT_INTEL_82875P_CSA	0x257b		/* 82875P CSA */
#define	PCI_PRODUCT_INTEL_82915G_HB	0x2580		/* 82915G Host */
#define	PCI_PRODUCT_INTEL_82915G_PCIE	0x2581		/* 82915G PCIE */
#define	PCI_PRODUCT_INTEL_82915G_IGD_1	0x2582		/* 82915G Video */
#define	PCI_PRODUCT_INTEL_82925X_HB	0x2584		/* 82925X Host */
#define	PCI_PRODUCT_INTEL_82925X_PCIE	0x2585		/* 82925X PCIE */
#define	PCI_PRODUCT_INTEL_E7221_HB	0x2588		/* E7221 Host */
#define	PCI_PRODUCT_INTEL_E7221_PCIE	0x2589		/* E7221 PCIE */
#define	PCI_PRODUCT_INTEL_E7221_IGD	0x258a		/* E7221 Video */
#define	PCI_PRODUCT_INTEL_82915GM_HB	0x2590		/* 82915GM Host */
#define	PCI_PRODUCT_INTEL_82915GM_PCIE	0x2591		/* 82915GM PCIE */
#define	PCI_PRODUCT_INTEL_82915GM_IGD_1	0x2592		/* 82915GM Video */
#define	PCI_PRODUCT_INTEL_6300ESB_LPC	0x25a1		/* 6300ESB LPC */
#define	PCI_PRODUCT_INTEL_6300ESB_IDE	0x25a2		/* 6300ESB IDE */
#define	PCI_PRODUCT_INTEL_6300ESB_SATA	0x25a3		/* 6300ESB SATA */
#define	PCI_PRODUCT_INTEL_6300ESB_SMB	0x25a4		/* 6300ESB SMBus */
#define	PCI_PRODUCT_INTEL_6300ESB_ACA	0x25a6		/* 6300ESB AC97 */
#define	PCI_PRODUCT_INTEL_6300ESB_ACM	0x25a7		/* 6300ESB Modem */
#define	PCI_PRODUCT_INTEL_6300ESB_USB_1	0x25a9		/* 6300ESB USB */
#define	PCI_PRODUCT_INTEL_6300ESB_USB_2	0x25aa		/* 6300ESB USB */
#define	PCI_PRODUCT_INTEL_6300ESB_WDT	0x25ab		/* 6300ESB WDT */
#define	PCI_PRODUCT_INTEL_6300ESB_APIC	0x25ac		/* 6300ESB APIC */
#define	PCI_PRODUCT_INTEL_6300ESB_USB2	0x25ad		/* 6300ESB USB */
#define	PCI_PRODUCT_INTEL_6300ESB_PCIX	0x25ae		/* 6300ESB PCIX */
#define	PCI_PRODUCT_INTEL_6300ESB_SATA2	0x25b0		/* 6300ESB SATA */
#define	PCI_PRODUCT_INTEL_5000X_HB	0x25c0		/* 5000X Host */
#define	PCI_PRODUCT_INTEL_5000Z_HB	0x25d0		/* 5000Z Host */
#define	PCI_PRODUCT_INTEL_5000V_HB	0x25d4		/* 5000V Host */
#define	PCI_PRODUCT_INTEL_5000P_HB	0x25d8		/* 5000P Host */
#define	PCI_PRODUCT_INTEL_5000_PCIE_1	0x25e2		/* 5000 PCIE */
#define	PCI_PRODUCT_INTEL_5000_PCIE_2	0x25e3		/* 5000 PCIE */
#define	PCI_PRODUCT_INTEL_5000_PCIE_3	0x25e4		/* 5000 PCIE */
#define	PCI_PRODUCT_INTEL_5000_PCIE_4	0x25e5		/* 5000 PCIE */
#define	PCI_PRODUCT_INTEL_5000_PCIE_5	0x25e6		/* 5000 PCIE */
#define	PCI_PRODUCT_INTEL_5000_PCIE_6	0x25e7		/* 5000 PCIE */
#define	PCI_PRODUCT_INTEL_5000_ERR	0x25f0		/* 5000 Error Reporting */
#define	PCI_PRODUCT_INTEL_5000_RESERVED_1	0x25f1		/* 5000 Reserved */
#define	PCI_PRODUCT_INTEL_5000_RESERVED_2	0x25f3		/* 5000 Reserved */
#define	PCI_PRODUCT_INTEL_5000_FBD_1	0x25f5		/* 5000 FBD */
#define	PCI_PRODUCT_INTEL_5000_FBD_2	0x25f6		/* 5000 FBD */
#define	PCI_PRODUCT_INTEL_5000_PCIE_7	0x25f7		/* 5000 PCIE x8 */
#define	PCI_PRODUCT_INTEL_5000_PCIE_8	0x25f8		/* 5000 PCIE x8 */
#define	PCI_PRODUCT_INTEL_5000_PCIE_9	0x25f9		/* 5000 PCIE x8 */
#define	PCI_PRODUCT_INTEL_5000_PCIE_10	0x25fa		/* 5000 PCIE x16 */
#define	PCI_PRODUCT_INTEL_E8500_HB	0x2600		/* E8500 Host */
#define	PCI_PRODUCT_INTEL_E8500_PCIE_1	0x2601		/* E8500 PCIE */
#define	PCI_PRODUCT_INTEL_E8500_PCIE_2	0x2602		/* E8500 PCIE */
#define	PCI_PRODUCT_INTEL_E8500_PCIE_3	0x2603		/* E8500 PCIE */
#define	PCI_PRODUCT_INTEL_E8500_PCIE_4	0x2604		/* E8500 PCIE */
#define	PCI_PRODUCT_INTEL_E8500_PCIE_5	0x2605		/* E8500 PCIE */
#define	PCI_PRODUCT_INTEL_E8500_PCIE_6	0x2606		/* E8500 PCIE */
#define	PCI_PRODUCT_INTEL_E8500_PCIE_7	0x2607		/* E8500 PCIE */
#define	PCI_PRODUCT_INTEL_E8500_PCIE_8	0x2608		/* E8500 PCIE x8 */
#define	PCI_PRODUCT_INTEL_E8500_PCIE_9	0x2609		/* E8500 PCIE x8 */
#define	PCI_PRODUCT_INTEL_E8500_PCIE_10	0x260a		/* E8500 PCIE x8 */
#define	PCI_PRODUCT_INTEL_E8500_IMI	0x260c		/* E8500 IMI */
#define	PCI_PRODUCT_INTEL_E8500_FSBINT	0x2610		/* E8500 FSB/Boot/Interrupt */
#define	PCI_PRODUCT_INTEL_E8500_AM	0x2611		/* E8500 Address Mapping */
#define	PCI_PRODUCT_INTEL_E8500_RAS	0x2612		/* E8500 RAS */
#define	PCI_PRODUCT_INTEL_E8500_MISC_1	0x2613		/* E8500 Misc */
#define	PCI_PRODUCT_INTEL_E8500_MISC_2	0x2614		/* E8500 Misc */
#define	PCI_PRODUCT_INTEL_E8500_MISC_3	0x2615		/* E8500 Misc */
#define	PCI_PRODUCT_INTEL_E8500_RES_1	0x2617		/* E8500 Reserved */
#define	PCI_PRODUCT_INTEL_E8500_RES_2	0x2618		/* E8500 Reserved */
#define	PCI_PRODUCT_INTEL_E8500_RES_3	0x2619		/* E8500 Reserved */
#define	PCI_PRODUCT_INTEL_E8500_RES_4	0x261a		/* E8500 Reserved */
#define	PCI_PRODUCT_INTEL_E8500_RES_5	0x261b		/* E8500 Reserved */
#define	PCI_PRODUCT_INTEL_E8500_RES_6	0x261c		/* E8500 Reserved */
#define	PCI_PRODUCT_INTEL_E8500_RES_7	0x261d		/* E8500 Reserved */
#define	PCI_PRODUCT_INTEL_E8500_RES_8	0x261e		/* E8500 Reserved */
#define	PCI_PRODUCT_INTEL_E8500_XMB_ID	0x2620		/* E8500 XMB */
#define	PCI_PRODUCT_INTEL_E8500_XMB_MISC	0x2621		/* E8500 XMB Misc */
#define	PCI_PRODUCT_INTEL_E8500_XMB_MAI	0x2622		/* E8500 XMB MAI */
#define	PCI_PRODUCT_INTEL_E8500_XMB_DDR	0x2623		/* E8500 XMB DDR */
#define	PCI_PRODUCT_INTEL_E8500_XMB_RES_1	0x2624		/* E8500 XMB Reserved */
#define	PCI_PRODUCT_INTEL_E8500_XMB_RES_2	0x2625		/* E8500 XMB Reserved */
#define	PCI_PRODUCT_INTEL_E8500_XMB_RES_3	0x2626		/* E8500 XMB Reserved */
#define	PCI_PRODUCT_INTEL_E8500_XMB_RES_4	0x2627		/* E8500 XMB Reserved */
#define	PCI_PRODUCT_INTEL_82801FB_LPC	0x2640		/* 82801FB LPC */
#define	PCI_PRODUCT_INTEL_82801FBM_LPC	0x2641		/* 82801FBM LPC */
#define	PCI_PRODUCT_INTEL_82801FB_SATA	0x2651		/* 82801FB SATA */
#define	PCI_PRODUCT_INTEL_82801FR_SATA	0x2652		/* 82801FR SATA */
#define	PCI_PRODUCT_INTEL_82801FBM_SATA	0x2653		/* 82801FBM SATA */
#define	PCI_PRODUCT_INTEL_82801FB_USB_1	0x2658		/* 82801FB USB */
#define	PCI_PRODUCT_INTEL_82801FB_USB_2	0x2659		/* 82801FB USB */
#define	PCI_PRODUCT_INTEL_82801FB_USB_3	0x265a		/* 82801FB USB */
#define	PCI_PRODUCT_INTEL_82801FB_USB_4	0x265b		/* 82801FB USB */
#define	PCI_PRODUCT_INTEL_82801FB_USB	0x265c		/* 82801FB USB */
#define	PCI_PRODUCT_INTEL_82801FB_PCIE_1	0x2660		/* 82801FB PCIE */
#define	PCI_PRODUCT_INTEL_82801FB_PCIE_2	0x2662		/* 82801FB PCIE */
#define	PCI_PRODUCT_INTEL_82801FB_PCIE_3	0x2664		/* 82801FB PCIE */
#define	PCI_PRODUCT_INTEL_82801FB_PCIE_4	0x2666		/* 82801FB PCIE */
#define	PCI_PRODUCT_INTEL_82801FB_HDA	0x2668		/* 82801FB HD Audio */
#define	PCI_PRODUCT_INTEL_82801FB_SMB	0x266a		/* 82801FB SMBus */
#define	PCI_PRODUCT_INTEL_82801FB_LAN	0x266c		/* 82801FB LAN */
#define	PCI_PRODUCT_INTEL_82801FB_ACM	0x266d		/* 82801FB Modem */
#define	PCI_PRODUCT_INTEL_82801FB_ACA	0x266e		/* 82801FB AC97 */
#define	PCI_PRODUCT_INTEL_82801FB_IDE	0x266f		/* 82801FB IDE */
#define	PCI_PRODUCT_INTEL_6321ESB_LPC	0x2670		/* 6321ESB LPC */
#define	PCI_PRODUCT_INTEL_6321ESB_SATA	0x2680		/* 6321ESB SATA */
#define	PCI_PRODUCT_INTEL_6321ESB_AHCI	0x2681		/* 6321ESB AHCI */
#define	PCI_PRODUCT_INTEL_6321ESB_RAID_1	0x2682		/* 6321ESB RAID */
#define	PCI_PRODUCT_INTEL_6321ESB_RAID_2	0x2683		/* 6321ESB RAID */
#define	PCI_PRODUCT_INTEL_6321ESB_USB_1	0x2688		/* 6321ESB USB */
#define	PCI_PRODUCT_INTEL_6321ESB_USB_2	0x2689		/* 6321ESB USB */
#define	PCI_PRODUCT_INTEL_6321ESB_USB_3	0x268a		/* 6321ESB USB */
#define	PCI_PRODUCT_INTEL_6321ESB_USB_4	0x268b		/* 6321ESB USB */
#define	PCI_PRODUCT_INTEL_6321ESB_USB_5	0x268c		/* 6321ESB USB */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_1	0x2690		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_2	0x2692		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_3	0x2694		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_4	0x2696		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_6321ESB_ACA	0x2698		/* 6321ESB AC97 */
#define	PCI_PRODUCT_INTEL_6321ESB_ACM	0x2699		/* 6321ESB Modem */
#define	PCI_PRODUCT_INTEL_6321ESB_HDA	0x269a		/* 6321ESB HD Audio */
#define	PCI_PRODUCT_INTEL_6321ESB_SMB	0x269b		/* 6321ESB SMBus */
#define	PCI_PRODUCT_INTEL_6321ESB_IDE	0x269e		/* 6321ESB IDE */
#define	PCI_PRODUCT_INTEL_OPTANE_9	0x2700		/* Optane 9 */
#define	PCI_PRODUCT_INTEL_WL_22500_1	0x2723		/* Wi-Fi 6 AX200 */
#define	PCI_PRODUCT_INTEL_WL_22500_9	0x2725		/* Wi-Fi 6 AX210 */
#define	PCI_PRODUCT_INTEL_WL_22500_10	0x2726		/* Wi-Fi 6 AX211 */
#define	PCI_PRODUCT_INTEL_82945G_HB	0x2770		/* 82945G Host */
#define	PCI_PRODUCT_INTEL_82945G_PCIE	0x2771		/* 82945G PCIE */
#define	PCI_PRODUCT_INTEL_82945G_IGD_1	0x2772		/* 82945G Video */
#define	PCI_PRODUCT_INTEL_82955X_HB	0x2774		/* 82955X Host */
#define	PCI_PRODUCT_INTEL_82955X_PCIE	0x2775		/* 82955X PCIE */
#define	PCI_PRODUCT_INTEL_82945G_IGD_2	0x2776		/* 82945G Video */
#define	PCI_PRODUCT_INTEL_E7230_HB	0x2778		/* E7230 Host */
#define	PCI_PRODUCT_INTEL_E7230_PCIE	0x2779		/* E7230 PCIE */
#define	PCI_PRODUCT_INTEL_82975X_PCIE_2	0x277a		/* 82975X PCIE */
#define	PCI_PRODUCT_INTEL_82975X_HB	0x277c		/* 82975X Host */
#define	PCI_PRODUCT_INTEL_82975X_PCIE	0x277d		/* 82975X PCIE */
#define	PCI_PRODUCT_INTEL_82915G_IGD_2	0x2782		/* 82915G Video */
#define	PCI_PRODUCT_INTEL_82915GM_IGD_2	0x2792		/* 82915GM Video */
#define	PCI_PRODUCT_INTEL_82945GM_HB	0x27a0		/* 82945GM Host */
#define	PCI_PRODUCT_INTEL_82945GM_PCIE	0x27a1		/* 82945GM PCIE */
#define	PCI_PRODUCT_INTEL_82945GM_IGD_1	0x27a2		/* 82945GM Video */
#define	PCI_PRODUCT_INTEL_82945GM_IGD_2	0x27a6		/* 82945GM Video */
#define	PCI_PRODUCT_INTEL_82945GME_HB	0x27ac		/* 82945GME Host */
#define	PCI_PRODUCT_INTEL_82945GME_IGD_1	0x27ae		/* 82945GME Video */
#define	PCI_PRODUCT_INTEL_82801GH_LPC	0x27b0		/* 82801GH LPC */
#define	PCI_PRODUCT_INTEL_82801GB_LPC	0x27b8		/* 82801GB LPC */
#define	PCI_PRODUCT_INTEL_82801GBM_LPC	0x27b9		/* 82801GBM LPC */
#define	PCI_PRODUCT_INTEL_NM10_LPC	0x27bc		/* NM10 LPC */
#define	PCI_PRODUCT_INTEL_82801GHM_LPC	0x27bd		/* 82801GHM LPC */
#define	PCI_PRODUCT_INTEL_82801GB_SATA	0x27c0		/* 82801GB SATA */
#define	PCI_PRODUCT_INTEL_82801GR_AHCI	0x27c1		/* 82801GR AHCI */
#define	PCI_PRODUCT_INTEL_82801GR_RAID	0x27c3		/* 82801GR RAID */
#define	PCI_PRODUCT_INTEL_82801GBM_SATA	0x27c4		/* 82801GBM SATA */
#define	PCI_PRODUCT_INTEL_82801GBM_AHCI	0x27c5		/* 82801GBM AHCI */
#define	PCI_PRODUCT_INTEL_82801GHM_RAID	0x27c6		/* 82801GHM RAID */
#define	PCI_PRODUCT_INTEL_82801GB_USB_1	0x27c8		/* 82801GB USB */
#define	PCI_PRODUCT_INTEL_82801GB_USB_2	0x27c9		/* 82801GB USB */
#define	PCI_PRODUCT_INTEL_82801GB_USB_3	0x27ca		/* 82801GB USB */
#define	PCI_PRODUCT_INTEL_82801GB_USB_4	0x27cb		/* 82801GB USB */
#define	PCI_PRODUCT_INTEL_82801GB_USB_5	0x27cc		/* 82801GB USB */
#define	PCI_PRODUCT_INTEL_82801GB_PCIE_1	0x27d0		/* 82801GB PCIE */
#define	PCI_PRODUCT_INTEL_82801GB_PCIE_2	0x27d2		/* 82801GB PCIE */
#define	PCI_PRODUCT_INTEL_82801GB_PCIE_3	0x27d4		/* 82801GB PCIE */
#define	PCI_PRODUCT_INTEL_82801GB_PCIE_4	0x27d6		/* 82801GB PCIE */
#define	PCI_PRODUCT_INTEL_82801GB_HDA	0x27d8		/* 82801GB HD Audio */
#define	PCI_PRODUCT_INTEL_82801GB_SMB	0x27da		/* 82801GB SMBus */
#define	PCI_PRODUCT_INTEL_82801GB_LAN	0x27dc		/* 82801GB LAN */
#define	PCI_PRODUCT_INTEL_82801GB_ACM	0x27dd		/* 82801GB Modem */
#define	PCI_PRODUCT_INTEL_82801GB_ACA	0x27de		/* 82801GB AC97 */
#define	PCI_PRODUCT_INTEL_82801GB_IDE	0x27df		/* 82801GB IDE */
#define	PCI_PRODUCT_INTEL_82801G_PCIE_5	0x27e0		/* 82801G PCIE */
#define	PCI_PRODUCT_INTEL_82801G_PCIE_6	0x27e2		/* 82801G PCIE */
#define	PCI_PRODUCT_INTEL_82801H_LPC	0x2810		/* 82801H LPC */
#define	PCI_PRODUCT_INTEL_82801HEM_LPC	0x2811		/* 82801HEM LPC */
#define	PCI_PRODUCT_INTEL_82801HH_LPC	0x2812		/* 82801HH LPC */
#define	PCI_PRODUCT_INTEL_82801HO_LPC	0x2814		/* 82801HO LPC */
#define	PCI_PRODUCT_INTEL_82801HBM_LPC	0x2815		/* 82801HBM LPC */
#define	PCI_PRODUCT_INTEL_82801H_SATA_1	0x2820		/* 82801H SATA */
#define	PCI_PRODUCT_INTEL_82801H_AHCI_6P	0x2821		/* 82801H AHCI */
#define	PCI_PRODUCT_INTEL_82801H_RAID	0x2822		/* 82801H RAID */
#define	PCI_PRODUCT_INTEL_82801H_AHCI_4P	0x2824		/* 82801H AHCI */
#define	PCI_PRODUCT_INTEL_82801H_SATA_2	0x2825		/* 82801H SATA */
#define	PCI_PRODUCT_INTEL_500SERIES_RAID_2	0x2826		/* 500 Series RAID */
#define	PCI_PRODUCT_INTEL_82801HBM_SATA	0x2828		/* 82801HBM SATA */
#define	PCI_PRODUCT_INTEL_82801HBM_AHCI	0x2829		/* 82801HBM AHCI */
#define	PCI_PRODUCT_INTEL_82801HBM_RAID	0x282a		/* 82801HBM RAID */
#define	PCI_PRODUCT_INTEL_82801H_UHCI_1	0x2830		/* 82801H USB */
#define	PCI_PRODUCT_INTEL_82801H_UHCI_2	0x2831		/* 82801H USB */
#define	PCI_PRODUCT_INTEL_82801H_UHCI_3	0x2832		/* 82801H USB */
#define	PCI_PRODUCT_INTEL_82801H_UHCI_6	0x2833		/* 82801H USB */
#define	PCI_PRODUCT_INTEL_82801H_UHCI_4	0x2834		/* 82801H USB */
#define	PCI_PRODUCT_INTEL_82801H_UHCI_5	0x2835		/* 82801H USB */
#define	PCI_PRODUCT_INTEL_82801H_EHCI_1	0x2836		/* 82801H USB */
#define	PCI_PRODUCT_INTEL_82801H_EHCI_2	0x283a		/* 82801H USB */
#define	PCI_PRODUCT_INTEL_82801H_SMB	0x283e		/* 82801H SMBus */
#define	PCI_PRODUCT_INTEL_82801H_PCIE_1	0x283f		/* 82801H PCIE */
#define	PCI_PRODUCT_INTEL_82801H_PCIE_2	0x2841		/* 82801H PCIE */
#define	PCI_PRODUCT_INTEL_82801H_PCIE_3	0x2843		/* 82801H PCIE */
#define	PCI_PRODUCT_INTEL_82801H_PCIE_4	0x2845		/* 82801H PCIE */
#define	PCI_PRODUCT_INTEL_82801H_PCIE_5	0x2847		/* 82801H PCIE */
#define	PCI_PRODUCT_INTEL_82801H_PCIE_6	0x2849		/* 82801H PCIE */
#define	PCI_PRODUCT_INTEL_82801H_HDA	0x284b		/* 82801H HD Audio */
#define	PCI_PRODUCT_INTEL_82801H_TS	0x284f		/* 82801H Thermal */
#define	PCI_PRODUCT_INTEL_82801HBM_IDE	0x2850		/* 82801HBM IDE */
#define	PCI_PRODUCT_INTEL_82801IH_LPC	0x2912		/* 82801IH LPC */
#define	PCI_PRODUCT_INTEL_82801IO_LPC	0x2914		/* 82801IO LPC */
#define	PCI_PRODUCT_INTEL_82801IR_LPC	0x2916		/* 82801IR LPC */
#define	PCI_PRODUCT_INTEL_82801IEM_LPC	0x2917		/* 82801IEM LPC */
#define	PCI_PRODUCT_INTEL_82801IB_LPC	0x2918		/* 82801IB LPC */
#define	PCI_PRODUCT_INTEL_82801IBM_LPC	0x2919		/* 82801IBM LPC */
#define	PCI_PRODUCT_INTEL_82801I_SATA_1	0x2920		/* 82801I SATA */
#define	PCI_PRODUCT_INTEL_82801I_SATA_2	0x2921		/* 82801I SATA */
#define	PCI_PRODUCT_INTEL_82801I_AHCI_1	0x2922		/* 82801I AHCI */
#define	PCI_PRODUCT_INTEL_82801I_AHCI_2	0x2923		/* 82801I AHCI */
#define	PCI_PRODUCT_INTEL_82801I_SATA_3	0x2926		/* 82801I SATA */
#define	PCI_PRODUCT_INTEL_82801I_SATA_4	0x2928		/* 82801I SATA */
#define	PCI_PRODUCT_INTEL_82801I_AHCI_3	0x2929		/* 82801I AHCI */
#define	PCI_PRODUCT_INTEL_82801I_RAID	0x292a		/* 82801I RAID */
#define	PCI_PRODUCT_INTEL_82801I_SATA_5	0x292d		/* 82801I SATA */
#define	PCI_PRODUCT_INTEL_82801I_SATA_6	0x292e		/* 82801I SATA */
#define	PCI_PRODUCT_INTEL_82801I_SMB	0x2930		/* 82801I SMBus */
#define	PCI_PRODUCT_INTEL_82801I_TS	0x2932		/* 82801I Thermal */
#define	PCI_PRODUCT_INTEL_82801I_UHCI_1	0x2934		/* 82801I USB */
#define	PCI_PRODUCT_INTEL_82801I_UHCI_2	0x2935		/* 82801I USB */
#define	PCI_PRODUCT_INTEL_82801I_UHCI_3	0x2936		/* 82801I USB */
#define	PCI_PRODUCT_INTEL_82801I_UHCI_4	0x2937		/* 82801I USB */
#define	PCI_PRODUCT_INTEL_82801I_UHCI_5	0x2938		/* 82801I USB */
#define	PCI_PRODUCT_INTEL_82801I_UHCI_6	0x2939		/* 82801I USB */
#define	PCI_PRODUCT_INTEL_82801I_EHCI_1	0x293a		/* 82801I USB */
#define	PCI_PRODUCT_INTEL_82801I_EHCI_2	0x293c		/* 82801I USB */
#define	PCI_PRODUCT_INTEL_82801I_HDA	0x293e		/* 82801I HD Audio */
#define	PCI_PRODUCT_INTEL_82801I_PCIE_1	0x2940		/* 82801I PCIE */
#define	PCI_PRODUCT_INTEL_82801I_PCIE_2	0x2942		/* 82801I PCIE */
#define	PCI_PRODUCT_INTEL_82801I_PCIE_3	0x2944		/* 82801I PCIE */
#define	PCI_PRODUCT_INTEL_82801I_PCIE_4	0x2946		/* 82801I PCIE */
#define	PCI_PRODUCT_INTEL_82801I_PCIE_5	0x2948		/* 82801I PCIE */
#define	PCI_PRODUCT_INTEL_82801I_PCIE_6	0x294a		/* 82801I PCIE */
#define	PCI_PRODUCT_INTEL_ICH9_IGP_C	0x294c		/* ICH9 IGP C */
#define	PCI_PRODUCT_INTEL_82946GZ_HB	0x2970		/* 82946GZ Host */
#define	PCI_PRODUCT_INTEL_82946GZ_PCIE	0x2971		/* 82946GZ PCIE */
#define	PCI_PRODUCT_INTEL_82946GZ_IGD_1	0x2972		/* 82946GZ Video */
#define	PCI_PRODUCT_INTEL_82946GZ_IGD_2	0x2973		/* 82946GZ Video */
#define	PCI_PRODUCT_INTEL_82946GZ_HECI_1	0x2974		/* 82946GZ HECI */
#define	PCI_PRODUCT_INTEL_82946GZ_HECI_2	0x2975		/* 82946GZ HECI */
#define	PCI_PRODUCT_INTEL_82946GZ_PT_IDER	0x2976		/* 82946GZ PT IDER */
#define	PCI_PRODUCT_INTEL_82946GZ_KT	0x2977		/* 82946GZ KT */
#define	PCI_PRODUCT_INTEL_82G35_HB	0x2980		/* 82G35 Host */
#define	PCI_PRODUCT_INTEL_82G35_PCIE	0x2981		/* 82G35 PCIE */
#define	PCI_PRODUCT_INTEL_82G35_IGD_1	0x2982		/* 82G35 Video */
#define	PCI_PRODUCT_INTEL_82G35_IGD_2	0x2983		/* 82G35 Video */
#define	PCI_PRODUCT_INTEL_82G35_HECI	0x2984		/* 82G35 HECI */
#define	PCI_PRODUCT_INTEL_82Q965_HB	0x2990		/* 82Q965 Host */
#define	PCI_PRODUCT_INTEL_82Q965_PCIE	0x2991		/* 82Q965 PCIE */
#define	PCI_PRODUCT_INTEL_82Q965_IGD_1	0x2992		/* 82Q965 Video */
#define	PCI_PRODUCT_INTEL_82Q965_IGD_2	0x2993		/* 82Q965 Video */
#define	PCI_PRODUCT_INTEL_82Q965_HECI_1	0x2994		/* 82Q965 HECI */
#define	PCI_PRODUCT_INTEL_82Q965_HECI_2	0x2995		/* 82Q965 HECI */
#define	PCI_PRODUCT_INTEL_82Q965_PT_IDER	0x2996		/* 82Q965 PT IDER */
#define	PCI_PRODUCT_INTEL_82Q965_KT	0x2997		/* 82Q965 KT */
#define	PCI_PRODUCT_INTEL_82G965_HB	0x29a0		/* 82G965 Host */
#define	PCI_PRODUCT_INTEL_82G965_PCIE	0x29a1		/* 82G965 PCIE */
#define	PCI_PRODUCT_INTEL_82G965_IGD_1	0x29a2		/* 82G965 Video */
#define	PCI_PRODUCT_INTEL_82G965_IGD_2	0x29a3		/* 82G965 Video */
#define	PCI_PRODUCT_INTEL_82G965_HECI_1	0x29a4		/* 82G965 HECI */
#define	PCI_PRODUCT_INTEL_82G965_HECI_2	0x29a5		/* 82G965 HECI */
#define	PCI_PRODUCT_INTEL_82G965_PT_IDER	0x29a6		/* 82G965 PT IDER */
#define	PCI_PRODUCT_INTEL_82G965_KT	0x29a7		/* 82G965 KT */
#define	PCI_PRODUCT_INTEL_82Q35_HB	0x29b0		/* 82Q35 Host */
#define	PCI_PRODUCT_INTEL_82Q35_PCIE	0x29b1		/* 82Q35 PCIE */
#define	PCI_PRODUCT_INTEL_82Q35_IGD_1	0x29b2		/* 82Q35 Video */
#define	PCI_PRODUCT_INTEL_82Q35_IGD_2	0x29b3		/* 82Q35 Video */
#define	PCI_PRODUCT_INTEL_82Q35_HECI_1	0x29b4		/* 82Q35 HECI */
#define	PCI_PRODUCT_INTEL_82Q35_HECI_2	0x29b5		/* 82Q35 HECI */
#define	PCI_PRODUCT_INTEL_82Q35_PT_IDER	0x29b6		/* 82Q35 PT IDER */
#define	PCI_PRODUCT_INTEL_82Q35_KT	0x29b7		/* 82Q35 KT */
#define	PCI_PRODUCT_INTEL_82G33_HB	0x29c0		/* 82G33 Host */
#define	PCI_PRODUCT_INTEL_82G33_PCIE	0x29c1		/* 82G33 PCIE */
#define	PCI_PRODUCT_INTEL_82G33_IGD_1	0x29c2		/* 82G33 Video */
#define	PCI_PRODUCT_INTEL_82G33_IGD_2	0x29c3		/* 82G33 Video */
#define	PCI_PRODUCT_INTEL_82G33_HECI_1	0x29c4		/* 82G33 HECI */
#define	PCI_PRODUCT_INTEL_82G33_HECI_2	0x29c5		/* 82G33 HECI */
#define	PCI_PRODUCT_INTEL_82G33_PT_IDER	0x29c6		/* 82G33 PT IDER */
#define	PCI_PRODUCT_INTEL_82G33_KT	0x29c7		/* 82G33 KT */
#define	PCI_PRODUCT_INTEL_82Q33_HB	0x29d0		/* 82Q33 Host */
#define	PCI_PRODUCT_INTEL_82Q33_PCIE	0x29d1		/* 82Q33 PCIE */
#define	PCI_PRODUCT_INTEL_82Q33_IGD_1	0x29d2		/* 82Q33 Video */
#define	PCI_PRODUCT_INTEL_82Q33_IGD_2	0x29d3		/* 82Q33 Video */
#define	PCI_PRODUCT_INTEL_82Q33_HECI_1	0x29d4		/* 82Q33 HECI */
#define	PCI_PRODUCT_INTEL_82Q33_HECI_2	0x29d5		/* 82Q33 HECI */
#define	PCI_PRODUCT_INTEL_82Q33_PT_IDER	0x29d6		/* 82Q33 PT IDER */
#define	PCI_PRODUCT_INTEL_82Q33_KT	0x29d7		/* 82Q33 KT */
#define	PCI_PRODUCT_INTEL_82X38_HB	0x29e0		/* 82X38 Host */
#define	PCI_PRODUCT_INTEL_82X38_PCIE_1	0x29e1		/* 82X38 PCIE */
#define	PCI_PRODUCT_INTEL_82X38_HECI_1	0x29e4		/* 82X38 HECI */
#define	PCI_PRODUCT_INTEL_82X38_HECI_2	0x29e5		/* 82X38 HECI */
#define	PCI_PRODUCT_INTEL_82X38_PT_IDER	0x29e6		/* 82X38 PT IDER */
#define	PCI_PRODUCT_INTEL_82X38_KT	0x29e7		/* 82X38 KT */
#define	PCI_PRODUCT_INTEL_82X38_PCIE_2	0x29e9		/* 82X38 PCIE */
#define	PCI_PRODUCT_INTEL_3200_HB	0x29f0		/* 3200/3210 Host */
#define	PCI_PRODUCT_INTEL_3200_PCIE	0x29f1		/* 3200/3210 PCIE */
#define	PCI_PRODUCT_INTEL_3210_PCIE	0x29f9		/* 3210 PCIE */
#define	PCI_PRODUCT_INTEL_82GM965_HB	0x2a00		/* GM965 Host */
#define	PCI_PRODUCT_INTEL_82GM965_PCIE	0x2a01		/* GM965 PCIE */
#define	PCI_PRODUCT_INTEL_82GM965_IGD_1	0x2a02		/* GM965 Video */
#define	PCI_PRODUCT_INTEL_82GM965_IGD_2	0x2a03		/* GM965 Video */
#define	PCI_PRODUCT_INTEL_82GM965_PT_IDER	0x2a06		/* GM965 PT IDER */
#define	PCI_PRODUCT_INTEL_82GM965_KT	0x2a07		/* GM965 KT */
#define	PCI_PRODUCT_INTEL_82GME965_HB	0x2a10		/* GME965 Host */
#define	PCI_PRODUCT_INTEL_82GME965_PCIE	0x2a11		/* GME965 PCIE */
#define	PCI_PRODUCT_INTEL_82GME965_IGD_1	0x2a12		/* GME965 Video */
#define	PCI_PRODUCT_INTEL_82GME965_IGD_2	0x2a13		/* GME965 Video */
#define	PCI_PRODUCT_INTEL_82GME965_HECI_1	0x2a14		/* GME965 HECI */
#define	PCI_PRODUCT_INTEL_82GME965_HECI_2	0x2a15		/* GME965 HECI */
#define	PCI_PRODUCT_INTEL_82GME965_PT_IDER	0x2a16		/* GME965 PT IDER */
#define	PCI_PRODUCT_INTEL_82GME965_KT	0x2a17		/* GME965 KT */
#define	PCI_PRODUCT_INTEL_82GM45_HB	0x2a40		/* GM45 Host */
#define	PCI_PRODUCT_INTEL_82GM45_PCIE	0x2a41		/* GM45 PCIE */
#define	PCI_PRODUCT_INTEL_82GM45_IGD_1	0x2a42		/* GM45 Video */
#define	PCI_PRODUCT_INTEL_82GM45_IGD_2	0x2a43		/* GM45 Video */
#define	PCI_PRODUCT_INTEL_82GM45_HECI_1	0x2a44		/* GM45 HECI */
#define	PCI_PRODUCT_INTEL_82GM45_HECI_2	0x2a45		/* GM45 HECI */
#define	PCI_PRODUCT_INTEL_82GM45_PT_IDER	0x2a46		/* GM45 PT IDER */
#define	PCI_PRODUCT_INTEL_82GM45_KT	0x2a47		/* GM45 KT */
#define	PCI_PRODUCT_INTEL_NCORE_QP_REG_1	0x2c61		/* QuickPath */
#define	PCI_PRODUCT_INTEL_NCORE_QP_REG_2	0x2c62		/* QuickPath */
#define	PCI_PRODUCT_INTEL_NCORE_QP_SAD	0x2d01		/* QuickPath */
#define	PCI_PRODUCT_INTEL_NCORE_QPI_LINK_0	0x2d10		/* QPI Link */
#define	PCI_PRODUCT_INTEL_NCORE_QPI_PHYS_0	0x2d11		/* QPI Physical */
#define	PCI_PRODUCT_INTEL_NCORE_RESERVED_1	0x2d12		/* Reserved */
#define	PCI_PRODUCT_INTEL_NCORE_RESERVED_2	0x2d13		/* Reserved */
#define	PCI_PRODUCT_INTEL_4SERIES_IGD	0x2e02		/* 4 Series Video */
#define	PCI_PRODUCT_INTEL_82Q45_HB	0x2e10		/* Q45 Host */
#define	PCI_PRODUCT_INTEL_82Q45_PCIE	0x2e11		/* Q45 PCIE */
#define	PCI_PRODUCT_INTEL_82Q45_IGD_1	0x2e12		/* Q45 Video */
#define	PCI_PRODUCT_INTEL_82Q45_IGD_2	0x2e13		/* Q45 Video */
#define	PCI_PRODUCT_INTEL_82Q45_HECI_1	0x2e14		/* Q45 HECI */
#define	PCI_PRODUCT_INTEL_82Q45_HECI_2	0x2e15		/* Q45 HECI */
#define	PCI_PRODUCT_INTEL_82Q45_PT_IDER	0x2e16		/* Q45 PT IDER */
#define	PCI_PRODUCT_INTEL_82Q45_KT	0x2e17		/* Q45 KT */
#define	PCI_PRODUCT_INTEL_82G45_HB	0x2e20		/* G45 Host */
#define	PCI_PRODUCT_INTEL_82G45_PCIE	0x2e21		/* G45 PCIE */
#define	PCI_PRODUCT_INTEL_82G45_IGD_1	0x2e22		/* G45 Video */
#define	PCI_PRODUCT_INTEL_82G45_IGD_2	0x2e23		/* G45 Video */
#define	PCI_PRODUCT_INTEL_82G45_PCIE_1	0x2e29		/* G45 PCIE */
#define	PCI_PRODUCT_INTEL_82G41_HB	0x2e30		/* G41 Host */
#define	PCI_PRODUCT_INTEL_82G45_PCIE_2	0x2e31		/* G45 PCIE */
#define	PCI_PRODUCT_INTEL_82G41_IGD_1	0x2e32		/* G41 Video */
#define	PCI_PRODUCT_INTEL_82G41_IGD_2	0x2e33		/* G41 Video */
#define	PCI_PRODUCT_INTEL_82B43_IGD_1	0x2e42		/* B43 Video */
#define	PCI_PRODUCT_INTEL_82B43_IGD_2	0x2e92		/* B43 Video */
#define	PCI_PRODUCT_INTEL_E5V3_HB	0x2f00		/* E5 v3 Host */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_1	0x2f01		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_2	0x2f02		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_3	0x2f03		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_4	0x2f04		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_5	0x2f05		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_6	0x2f06		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_7	0x2f07		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_8	0x2f08		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_9	0x2f09		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_10	0x2f0a		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIE_11	0x2f0b		/* E5 v3 PCIE */
#define	PCI_PRODUCT_INTEL_E5V3_PCIERING	0x2f1d		/* E5 v3 PCIE Ring */
#define	PCI_PRODUCT_INTEL_E5V3_SCRATCH_1	0x2f1e		/* E5 v3 Scratch */
#define	PCI_PRODUCT_INTEL_E5V3_SCRATCH_2	0x2f1f		/* E5 v3 Scratch */
#define	PCI_PRODUCT_INTEL_E5V3_DMA_0	0x2f20		/* E5 v3 DMA */
#define	PCI_PRODUCT_INTEL_E5V3_DMA_1	0x2f21		/* E5 v3 DMA */
#define	PCI_PRODUCT_INTEL_E5V3_DMA_2	0x2f22		/* E5 v3 DMA */
#define	PCI_PRODUCT_INTEL_E5V3_DMA_3	0x2f23		/* E5 v3 DMA */
#define	PCI_PRODUCT_INTEL_E5V3_DMA_4	0x2f24		/* E5 v3 DMA */
#define	PCI_PRODUCT_INTEL_E5V3_DMA_5	0x2f25		/* E5 v3 DMA */
#define	PCI_PRODUCT_INTEL_E5V3_DMA_6	0x2f26		/* E5 v3 DMA */
#define	PCI_PRODUCT_INTEL_E5V3_DMA_7	0x2f27		/* E5 v3 DMA */
#define	PCI_PRODUCT_INTEL_E5V3_ADDRMAP	0x2f28		/* E5 v3 Address Map */
#define	PCI_PRODUCT_INTEL_E5V3_HOTPLUG	0x2f29		/* E5 v3 Hot Plug */
#define	PCI_PRODUCT_INTEL_E5V3_ERR	0x2f2a		/* E5 v3 Error Reporting */
#define	PCI_PRODUCT_INTEL_E5V3_IOAPIC	0x2f2c		/* E5 v3 I/O APIC */
#define	PCI_PRODUCT_INTEL_E5V3_HA_1	0x2f30		/* E5 v3 Home Agent */
#define	PCI_PRODUCT_INTEL_E5V3_QPI_0	0x2f32		/* E5 v3 QPI */
#define	PCI_PRODUCT_INTEL_E5V3_QPI_1	0x2f33		/* E5 v3 QPI */
#define	PCI_PRODUCT_INTEL_E5V3_PCIEMON	0x2f34		/* E5 v3 PCIE Monitor */
#define	PCI_PRODUCT_INTEL_E5V3_QPIMON_1	0x2f36		/* E5 v3 QPI Monitor */
#define	PCI_PRODUCT_INTEL_E5V3_QPIMON_2	0x2f37		/* E5 v3 QPI Monitor */
#define	PCI_PRODUCT_INTEL_E5V3_TA	0x2f68		/* E5 v3 TA */
#define	PCI_PRODUCT_INTEL_E5V3_CBROADCAST_1	0x2f6e		/* E5 v3 DDR Broadcast */
#define	PCI_PRODUCT_INTEL_E5V3_GBROADCAST_1	0x2f6f		/* E5 v3 DDR Broadcast */
#define	PCI_PRODUCT_INTEL_E5V3_HA_DEBUG_1	0x2f70		/* E5 v3 Home Agent Debug */
#define	PCI_PRODUCT_INTEL_E5V3_RAS	0x2f71		/* E5 v3 RAS */
#define	PCI_PRODUCT_INTEL_E5V3_SCRATCH_3	0x2f7d		/* E5 v3 Scratch */
#define	PCI_PRODUCT_INTEL_E5V3_QPI_2	0x2f80		/* E5 v3 QPI */
#define	PCI_PRODUCT_INTEL_E5V3_QPIMON_3	0x2f81		/* E5 v3 QPI Monitor */
#define	PCI_PRODUCT_INTEL_E5V3_QPI_3	0x2f83		/* E5 v3 QPI */
#define	PCI_PRODUCT_INTEL_E5V3_VCU_1	0x2f88		/* E5 v3 VCU */
#define	PCI_PRODUCT_INTEL_E5V3_VCU_2	0x2f8a		/* E5 v3 VCU */
#define	PCI_PRODUCT_INTEL_E5V3_QPI_4	0x2f90		/* E5 v3 QPI */
#define	PCI_PRODUCT_INTEL_E5V3_QPI_5	0x2f93		/* E5 v3 QPI */
#define	PCI_PRODUCT_INTEL_E5V3_PCU_1	0x2f98		/* E5 v3 PCU */
#define	PCI_PRODUCT_INTEL_E5V3_PCU_2	0x2f99		/* E5 v3 PCU */
#define	PCI_PRODUCT_INTEL_E5V3_PCU_3	0x2f9a		/* E5 v3 PCU */
#define	PCI_PRODUCT_INTEL_E5V3_PCU_4	0x2f9c		/* E5 v3 PCU */
#define	PCI_PRODUCT_INTEL_E5V3_HA_2	0x2fa0		/* E5 v3 Home Agent */
#define	PCI_PRODUCT_INTEL_E5V3_MEM	0x2fa8		/* E5 v3 Memory */
#define	PCI_PRODUCT_INTEL_E5V3_TAD_1	0x2faa		/* E5 v3 TAD */
#define	PCI_PRODUCT_INTEL_E5V3_TAD_2	0x2fab		/* E5 v3 TAD */
#define	PCI_PRODUCT_INTEL_E5V3_TAD_3	0x2fac		/* E5 v3 TAD */
#define	PCI_PRODUCT_INTEL_E5V3_TAD_4	0x2fad		/* E5 v3 TAD */
#define	PCI_PRODUCT_INTEL_E5V3_CBROADCAST_2	0x2fae		/* E5 v3 DDR Broadcast */
#define	PCI_PRODUCT_INTEL_E5V3_GBROADCAST_2	0x2faf		/* E5 v3 DDR Broadcast */
#define	PCI_PRODUCT_INTEL_E5V3_THERMAL_1	0x2fb0		/* E5 v3 Thermal */
#define	PCI_PRODUCT_INTEL_E5V3_THERMAL_2	0x2fb1		/* E5 v3 Thermal */
#define	PCI_PRODUCT_INTEL_E5V3_ERROR_1	0x2fb2		/* E5 v3 Error */
#define	PCI_PRODUCT_INTEL_E5V3_ERROR_2	0x2fb3		/* E5 v3 Error */
#define	PCI_PRODUCT_INTEL_E5V3_THERMAL_3	0x2fb4		/* E5 v3 Thermal */
#define	PCI_PRODUCT_INTEL_E5V3_THERMAL_4	0x2fb5		/* E5 v3 Thermal */
#define	PCI_PRODUCT_INTEL_E5V3_ERROR_3	0x2fb6		/* E5 v3 Error */
#define	PCI_PRODUCT_INTEL_E5V3_ERROR_4	0x2fb7		/* E5 v3 Error */
#define	PCI_PRODUCT_INTEL_E5V3_DDRIO_1	0x2fb8		/* E5 v3 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V3_DDRIO_2	0x2fb9		/* E5 v3 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V3_DDRIO_3	0x2fba		/* E5 v3 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V3_DDRIO_4	0x2fbb		/* E5 v3 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V3_DDRIO_5	0x2fbc		/* E5 v3 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V3_DDRIO_6	0x2fbd		/* E5 v3 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V3_DDRIO_7	0x2fbe		/* E5 v3 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V3_DDRIO_8	0x2fbf		/* E5 v3 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V3_PCU_5	0x2fc0		/* E5 v3 PCU */
#define	PCI_PRODUCT_INTEL_E5V3_THERMAL_5	0x2fd0		/* E5 v3 Thermal */
#define	PCI_PRODUCT_INTEL_E5V3_THERMAL_6	0x2fd1		/* E5 v3 Thermal */
#define	PCI_PRODUCT_INTEL_E5V3_ERROR_5	0x2fd2		/* E5 v3 Error */
#define	PCI_PRODUCT_INTEL_E5V3_ERROR_6	0x2fd3		/* E5 v3 Error */
#define	PCI_PRODUCT_INTEL_E5V3_THERMAL_7	0x2fd4		/* E5 v3 Thermal */
#define	PCI_PRODUCT_INTEL_E5V3_THERMAL_8	0x2fd5		/* E5 v3 Thermal */
#define	PCI_PRODUCT_INTEL_E5V3_ERROR_7	0x2fd6		/* E5 v3 Error */
#define	PCI_PRODUCT_INTEL_E5V3_ERROR_8	0x2fd7		/* E5 v3 Error */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_1	0x2fe0		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_2	0x2fe1		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_3	0x2fe2		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_4	0x2fe3		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_5	0x2fe4		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_6	0x2fe5		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_7	0x2fe6		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_8	0x2fe7		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_9	0x2fe8		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_10	0x2fe9		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_11	0x2fea		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_12	0x2feb		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_13	0x2fec		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_14	0x2fed		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_15	0x2fee		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_16	0x2fef		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_17	0x2ff0		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_18	0x2ff1		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_19	0x2ff2		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_20	0x2ff3		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_21	0x2ff4		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_22	0x2ff5		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_23	0x2ff6		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_UNICAST_24	0x2ff7		/* E5 v3 Unicast */
#define	PCI_PRODUCT_INTEL_E5V3_RA_1	0x2ff8		/* E5 v3 Ring Agent */
#define	PCI_PRODUCT_INTEL_E5V3_RA_2	0x2ff9		/* E5 v3 Ring Agent */
#define	PCI_PRODUCT_INTEL_E5V3_RA_3	0x2ffa		/* E5 v3 Ring Agent */
#define	PCI_PRODUCT_INTEL_E5V3_RA_4	0x2ffb		/* E5 v3 Ring Agent */
#define	PCI_PRODUCT_INTEL_E5V3_SAD_1	0x2ffc		/* E5 v3 SAD */
#define	PCI_PRODUCT_INTEL_E5V3_SAD_2	0x2ffd		/* E5 v3 SAD */
#define	PCI_PRODUCT_INTEL_E5V3_SAD_3	0x2ffe		/* E5 v3 SAD */
#define	PCI_PRODUCT_INTEL_RCU32	0x3092		/* RCU32 I2O RAID */
#define	PCI_PRODUCT_INTEL_I225_K	0x3100		/* I225-K */
#define	PCI_PRODUCT_INTEL_I225_K2	0x3101		/* I225-K2 */
#define	PCI_PRODUCT_INTEL_3124	0x3124		/* 3124 SATA */
#define	PCI_PRODUCT_INTEL_WL_3165_1	0x3165		/* AC 3165 */
#define	PCI_PRODUCT_INTEL_WL_3165_2	0x3166		/* AC 3165 */
#define	PCI_PRODUCT_INTEL_GLK_UHD_605	0x3184		/* UHD Graphics 605 */
#define	PCI_PRODUCT_INTEL_GLK_UHD_600	0x3185		/* UHD Graphics 600 */
#define	PCI_PRODUCT_INTEL_GLK_DPTF	0x318c		/* Gemini Lake DPTF */
#define	PCI_PRODUCT_INTEL_GLK_GNA	0x3190		/* Gemini Lake GNA */
#define	PCI_PRODUCT_INTEL_GLK_PMC	0x3194		/* Gemini Lake PMC */
#define	PCI_PRODUCT_INTEL_GLK_HDA	0x3198		/* Gemini Lake HD Audio */
#define	PCI_PRODUCT_INTEL_GLK_MEI	0x319a		/* Gemini Lake MEI */
#define	PCI_PRODUCT_INTEL_GLK_XHCI	0x31a8		/* Gemini Lake xHCI */
#define	PCI_PRODUCT_INTEL_GLK_I2C_1	0x31ac		/* Gemini Lake I2C */
#define	PCI_PRODUCT_INTEL_GLK_I2C_2	0x31ae		/* Gemini Lake I2C */
#define	PCI_PRODUCT_INTEL_GLK_I2C_3	0x31b0		/* Gemini Lake I2C */
#define	PCI_PRODUCT_INTEL_GLK_I2C_4	0x31b2		/* Gemini Lake I2C */
#define	PCI_PRODUCT_INTEL_GLK_I2C_5	0x31b4		/* Gemini Lake I2C */
#define	PCI_PRODUCT_INTEL_GLK_I2C_6	0x31b6		/* Gemini Lake I2C */
#define	PCI_PRODUCT_INTEL_GLK_I2C_7	0x31b8		/* Gemini Lake I2C */
#define	PCI_PRODUCT_INTEL_GLK_I2C_8	0x31ba		/* Gemini Lake I2C */
#define	PCI_PRODUCT_INTEL_GLK_UART_1	0x31bc		/* Gemini Lake HSUART */
#define	PCI_PRODUCT_INTEL_GLK_UART_2	0x31be		/* Gemini Lake HSUART */
#define	PCI_PRODUCT_INTEL_GLK_UART_3	0x31c0		/* Gemini Lake HSUART */
#define	PCI_PRODUCT_INTEL_GLK_SPI_1	0x31c2		/* Gemini Lake SPI */
#define	PCI_PRODUCT_INTEL_GLK_SPI_2	0x31c4		/* Gemini Lake SPI */
#define	PCI_PRODUCT_INTEL_GLK_SPI_3	0x31c6		/* Gemini Lake SPI */
#define	PCI_PRODUCT_INTEL_GLK_SDMMC	0x31ca		/* Gemini Lake SD/MMC */
#define	PCI_PRODUCT_INTEL_GLK_EMMC	0x31cc		/* Gemini Lake eMMC */
#define	PCI_PRODUCT_INTEL_GLK_SDIO	0x31d0		/* Gemini Lake SDIO */
#define	PCI_PRODUCT_INTEL_GLK_SMB	0x31d4		/* Gemini Lake SMBus */
#define	PCI_PRODUCT_INTEL_GLK_PCIE_1	0x31d6		/* Gemini Lake PCIE */
#define	PCI_PRODUCT_INTEL_GLK_PCIE_2	0x31d7		/* Gemini Lake PCIE */
#define	PCI_PRODUCT_INTEL_GLK_PCIE_3	0x31d8		/* Gemini Lake PCIE */
#define	PCI_PRODUCT_INTEL_GLK_PCIE_4	0x31d9		/* Gemini Lake PCIE */
#define	PCI_PRODUCT_INTEL_GLK_PCIE_5	0x31da		/* Gemini Lake PCIE */
#define	PCI_PRODUCT_INTEL_GLK_PCIE_6	0x31db		/* Gemini Lake PCIE */
#define	PCI_PRODUCT_INTEL_WL_9560_3	0x31dc		/* AC 9560 */
#define	PCI_PRODUCT_INTEL_GLK_AHCI	0x31e3		/* Gemini Lake AHCI */
#define	PCI_PRODUCT_INTEL_GLK_LPC	0x31e8		/* Gemini Lake LPC */
#define	PCI_PRODUCT_INTEL_GLK_UART_4	0x31ee		/* Gemini Lake HSUART */
#define	PCI_PRODUCT_INTEL_GLK_HB	0x31f0		/* Gemini Lake Host */
#define	PCI_PRODUCT_INTEL_31244	0x3200		/* 31244 SATA */
#define	PCI_PRODUCT_INTEL_82855PM_HB	0x3340		/* 82855PM Host */
#define	PCI_PRODUCT_INTEL_82855PM_AGP	0x3341		/* 82855PM AGP */
#define	PCI_PRODUCT_INTEL_82855PM_PM	0x3342		/* 82855PM Power */
#define	PCI_PRODUCT_INTEL_5500_HB	0x3403		/* 5500 Host */
#define	PCI_PRODUCT_INTEL_82X58_HB	0x3405		/* X58 Host */
#define	PCI_PRODUCT_INTEL_825520_HB	0x3406		/* 5520 Host */
#define	PCI_PRODUCT_INTEL_82X58_PCIE_1	0x3408		/* X58 PCIE */
#define	PCI_PRODUCT_INTEL_82X58_PCIE_2	0x3409		/* X58 PCIE */
#define	PCI_PRODUCT_INTEL_82X58_PCIE_3	0x340a		/* X58 PCIE */
#define	PCI_PRODUCT_INTEL_82X58_PCIE_4	0x340b		/* X58 PCIE */
#define	PCI_PRODUCT_INTEL_82X58_PCIE_5	0x340c		/* X58 PCIE */
#define	PCI_PRODUCT_INTEL_82X58_PCIE_6	0x340d		/* X58 PCIE */
#define	PCI_PRODUCT_INTEL_82X58_PCIE_7	0x340e		/* X58 PCIE */
#define	PCI_PRODUCT_INTEL_82X58_PCIE_8	0x340f		/* X58 PCIE */
#define	PCI_PRODUCT_INTEL_82X58_PCIE_9	0x3410		/* X58 PCIE */
#define	PCI_PRODUCT_INTEL_82X58_PCIE_10	0x3411		/* X58 PCIE */
#define	PCI_PRODUCT_INTEL_82X58_QP0_PHY	0x3418		/* 5520/X58 QuickPath */
#define	PCI_PRODUCT_INTEL_5520_QP1_PHY	0x3419		/* 5520 QuickPath */
#define	PCI_PRODUCT_INTEL_82X58_GPIO	0x3422		/* X58 GPIO */
#define	PCI_PRODUCT_INTEL_82X58_RAS	0x3423		/* X58 RAS */
#define	PCI_PRODUCT_INTEL_82X58_QP0_P0	0x3425		/* X58 QuickPath */
#define	PCI_PRODUCT_INTEL_82X58_QP0_P1	0x3426		/* X58 QuickPath */
#define	PCI_PRODUCT_INTEL_82X58_QP1_P0	0x3427		/* X58 QuickPath */
#define	PCI_PRODUCT_INTEL_82X58_QP1_P1	0x3428		/* X58 QuickPath */
#define	PCI_PRODUCT_INTEL_82X58_QD_0	0x3429		/* X58 QuickData */
#define	PCI_PRODUCT_INTEL_82X58_QD_1	0x342a		/* X58 QuickData */
#define	PCI_PRODUCT_INTEL_82X58_QD_2	0x342b		/* X58 QuickData */
#define	PCI_PRODUCT_INTEL_82X58_QD_3	0x342c		/* X58 QuickData */
#define	PCI_PRODUCT_INTEL_82X58_IOXAPIC	0x342d		/* X58 IOxAPIC */
#define	PCI_PRODUCT_INTEL_82X58_MISC	0x342e		/* X58 Misc */
#define	PCI_PRODUCT_INTEL_82X58_QD_4	0x3430		/* X58 QuickData */
#define	PCI_PRODUCT_INTEL_82X58_QD_5	0x3431		/* X58 QuickData */
#define	PCI_PRODUCT_INTEL_82X58_QD_6	0x3432		/* X58 QuickData */
#define	PCI_PRODUCT_INTEL_82X58_QD_7	0x3433		/* X58 QuickData */
#define	PCI_PRODUCT_INTEL_82X58_THROTTLE	0x3438		/* X58 Throttle */
#define	PCI_PRODUCT_INTEL_82X58_TXT	0x343f		/* X58 TXT */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_ESPI_U	0x3482		/* 495 Series eSPI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_ESPI_Y	0x3487		/* 495 Series eSPI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_P2SB	0x34a0		/* 495 Series P2SB */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PMC	0x34a1		/* 495 Series PMC */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_SMB	0x34a3		/* 495 Series SMBus */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_SPI_FLASH	0x34a4		/* 495 Series SPI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_TH	0x34a6		/* 495 Series TH */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_UART_1	0x34a8		/* 495 Series UART */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_UART_2	0x34a9		/* 495 Series UART */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_GSPI_1	0x34aa		/* 495 Series GSPI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_GSPI_2	0x34ab		/* 495 Series GSPI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_9	0x34b0		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_10	0x34b1		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_11	0x34b2		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_12	0x34b3		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_13	0x34b4		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_14	0x34b5		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_15	0x34b6		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_16	0x34b7		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_1	0x34b8		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_2	0x34b9		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_3	0x34ba		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_4	0x34bb		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_5	0x34bc		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_6	0x34bd		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_7	0x34be		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_PCIE_8	0x34bf		/* 495 Series PCIE */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_EMMC	0x34c4		/* 495 Series eMMC */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_I2C_5	0x34c5		/* 495 Series I2C */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_I2C_6	0x34c6		/* 495 Series I2C */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_UART_3	0x34c7		/* 495 Series UART */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_HDA	0x34c8		/* 495 Series HD Audio */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_AHCI	0x34d3		/* 495 Series AHCI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_RAID_1	0x34d5		/* 495 Series RAID */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_RAID_2	0x34d7		/* 495 Series RAID */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_HECI_1	0x34e0		/* 495 Series HECI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_HECI_2	0x34e1		/* 495 Series HECI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_IDER	0x34e2		/* 495 Series IDE-R */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_KT	0x34e3		/* 495 Series KT */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_I2C_1	0x34e8		/* 495 Series I2C */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_I2C_2	0x34e9		/* 495 Series I2C */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_I2C_3	0x34ea		/* 495 Series I2C */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_I2C_4	0x34eb		/* 495 Series I2C */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_XHCI	0x34ed		/* 495 Series xHCI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_XDCI	0x34ee		/* 495 Series xDCI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_SRAM	0x34ef		/* 495 Series Shared SRAM */
#define	PCI_PRODUCT_INTEL_WL_22500_4	0x34f0		/* Wi-Fi 6 AX201 */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_SDXC	0x34f8		/* 495 Series SDXC */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_GSPI_3	0x34fb		/* 495 Series GSPI */
#define	PCI_PRODUCT_INTEL_495SERIES_LP_ISH	0x34fc		/* 495 Series ISH */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_5	0x3500		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_6321ESB_IOXAPIC	0x3504		/* 6321ESB IOxAPIC */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIX	0x350c		/* 6321ESB PCIE-PCIX */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_6	0x3510		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_7	0x3511		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_8	0x3514		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_9	0x3515		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_10	0x3518		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_6321ESB_PCIE_11	0x3519		/* 6321ESB PCIE */
#define	PCI_PRODUCT_INTEL_82830M_HB	0x3575		/* 82830M Host */
#define	PCI_PRODUCT_INTEL_82830M_AGP	0x3576		/* 82830M AGP */
#define	PCI_PRODUCT_INTEL_82830M_IGD	0x3577		/* 82830M Video */
#define	PCI_PRODUCT_INTEL_82855GM_HB	0x3580		/* 82855GM Host */
#define	PCI_PRODUCT_INTEL_82855GME_AGP	0x3581		/* 82855GME AGP */
#define	PCI_PRODUCT_INTEL_82855GM_IGD	0x3582		/* 82855GM Video */
#define	PCI_PRODUCT_INTEL_82855GM_MEM	0x3584		/* 82855GM Memory */
#define	PCI_PRODUCT_INTEL_82855GM_CFG	0x3585		/* 82855GM Config */
#define	PCI_PRODUCT_INTEL_82854_HB	0x358c		/* 82854 Host */
#define	PCI_PRODUCT_INTEL_82854_IGD	0x358e		/* 82854 Video */
#define	PCI_PRODUCT_INTEL_E7520_HB	0x3590		/* E7520 Host */
#define	PCI_PRODUCT_INTEL_E7520_ERR	0x3591		/* E7520 Error Reporting */
#define	PCI_PRODUCT_INTEL_E7320_HB	0x3592		/* E7320 Host */
#define	PCI_PRODUCT_INTEL_E7320_ERR	0x3593		/* E7320 Error Reporting */
#define	PCI_PRODUCT_INTEL_E7520_DMA	0x3594		/* E7520 DMA */
#define	PCI_PRODUCT_INTEL_E7520_PCIE_A0	0x3595		/* E7520 PCIE */
#define	PCI_PRODUCT_INTEL_E7520_PCIE_A1	0x3596		/* E7520 PCIE */
#define	PCI_PRODUCT_INTEL_E7520_PCIE_B0	0x3597		/* E7520 PCIE */
#define	PCI_PRODUCT_INTEL_E7520_PCIE_B1	0x3598		/* E7520 PCIE */
#define	PCI_PRODUCT_INTEL_E7520_PCIE_C0	0x3599		/* E7520 PCIE */
#define	PCI_PRODUCT_INTEL_E7520_PCIE_C1	0x359a		/* E7520 PCIE */
#define	PCI_PRODUCT_INTEL_E7520_CFG	0x359b		/* E7520 Config */
#define	PCI_PRODUCT_INTEL_E7525_HB	0x359e		/* E7525 Host */
#define	PCI_PRODUCT_INTEL_3100_HB	0x35b0		/* 3100 Host */
#define	PCI_PRODUCT_INTEL_3100_ERR	0x35b1		/* 3100 Error Reporting */
#define	PCI_PRODUCT_INTEL_3100_EDMA	0x35b6		/* 3100 EDMA */
#define	PCI_PRODUCT_INTEL_3100_PCIE_1	0x35b6		/* 3100 PCIE */
#define	PCI_PRODUCT_INTEL_3100_PCIE_2	0x35b7		/* 3100 PCIE */
#define	PCI_PRODUCT_INTEL_7300_HB	0x3600		/* 7300 Host */
#define	PCI_PRODUCT_INTEL_7300_PCIE_1	0x3604		/* 7300 PCIE */
#define	PCI_PRODUCT_INTEL_7300_PCIE_2	0x3605		/* 7300 PCIE */
#define	PCI_PRODUCT_INTEL_7300_PCIE_3	0x3606		/* 7300 PCIE */
#define	PCI_PRODUCT_INTEL_7300_PCIE_4	0x3607		/* 7300 PCIE */
#define	PCI_PRODUCT_INTEL_7300_PCIE_5	0x3608		/* 7300 PCIE */
#define	PCI_PRODUCT_INTEL_7300_PCIE_6	0x3609		/* 7300 PCIE */
#define	PCI_PRODUCT_INTEL_7300_PCIE_7	0x360a		/* 7300 PCIE */
#define	PCI_PRODUCT_INTEL_IOAT_CNB	0x360b		/* I/OAT CNB */
#define	PCI_PRODUCT_INTEL_7300_FSBINT	0x360c		/* 7300 FSB/Boot/Interrupt */
#define	PCI_PRODUCT_INTEL_7300_SNOOP	0x360d		/* 7300 Snoop Filter */
#define	PCI_PRODUCT_INTEL_7300_MISC	0x360e		/* 7300 Misc */
#define	PCI_PRODUCT_INTEL_7300_FBD_0	0x360f		/* 7300 FBD */
#define	PCI_PRODUCT_INTEL_7300_FBD_1	0x3610		/* 7300 FBD */
#define	PCI_PRODUCT_INTEL_X722_VF	0x37cd		/* X722 VF */
#define	PCI_PRODUCT_INTEL_X722_10G_KX	0x37ce		/* X722 KX */
#define	PCI_PRODUCT_INTEL_X722_10G_QSFP	0x37cf		/* X722 QSFP+ */
#define	PCI_PRODUCT_INTEL_X722_10G_SFP_1	0x37d0		/* X722 SFP+ */
#define	PCI_PRODUCT_INTEL_X722_1G	0x37d1		/* X722 1GbE */
#define	PCI_PRODUCT_INTEL_X722_10G_T	0x37d2		/* X722 10GBASE-T */
#define	PCI_PRODUCT_INTEL_X722_10G_SFP_2	0x37d3		/* X722 SFP+ */
#define	PCI_PRODUCT_INTEL_82801JD_SATA_1	0x3a00		/* 82801JD SATA */
#define	PCI_PRODUCT_INTEL_82801JD_AHCI	0x3a02		/* 82801JD AHCI */
#define	PCI_PRODUCT_INTEL_82801JD_RAID	0x3a05		/* 82801JD RAID */
#define	PCI_PRODUCT_INTEL_82801JD_SATA_2	0x3a06		/* 82801JD SATA */
#define	PCI_PRODUCT_INTEL_82801JDO_LPC	0x3a14		/* 82801JDO LPC */
#define	PCI_PRODUCT_INTEL_82801JIR_LPC	0x3a16		/* 82801JIR LPC */
#define	PCI_PRODUCT_INTEL_82801JIB_LPC	0x3a18		/* 82801JIB LPC */
#define	PCI_PRODUCT_INTEL_82801JD_LPC	0x3a1a		/* 82801JD LPC */
#define	PCI_PRODUCT_INTEL_82801JI_SATA_1	0x3a20		/* 82801JI SATA */
#define	PCI_PRODUCT_INTEL_82801JI_AHCI	0x3a22		/* 82801JI AHCI */
#define	PCI_PRODUCT_INTEL_82801JI_RAID	0x3a25		/* 82801JI RAID */
#define	PCI_PRODUCT_INTEL_82801JI_SATA_2	0x3a26		/* 82801JI SATA */
#define	PCI_PRODUCT_INTEL_82801JI_SMB	0x3a30		/* 82801JI SMBus */
#define	PCI_PRODUCT_INTEL_82801JI_UHCI_1	0x3a34		/* 82801JI USB */
#define	PCI_PRODUCT_INTEL_82801JI_UHCI_2	0x3a35		/* 82801JI USB */
#define	PCI_PRODUCT_INTEL_82801JI_UHCI_3	0x3a36		/* 82801JI USB */
#define	PCI_PRODUCT_INTEL_82801JI_UHCI_4	0x3a37		/* 82801JI USB */
#define	PCI_PRODUCT_INTEL_82801JI_UHCI_5	0x3a38		/* 82801JI USB */
#define	PCI_PRODUCT_INTEL_82801JI_UHCI_6	0x3a39		/* 82801JI USB */
#define	PCI_PRODUCT_INTEL_82801JI_EHCI_1	0x3a3a		/* 82801JI USB */
#define	PCI_PRODUCT_INTEL_82801JI_EHCI_2	0x3a3c		/* 82801JI USB */
#define	PCI_PRODUCT_INTEL_82801JI_HDA	0x3a3e		/* 82801JI HD Audio */
#define	PCI_PRODUCT_INTEL_82801JI_PCIE_1	0x3a40		/* 82801JI PCIE */
#define	PCI_PRODUCT_INTEL_82801JI_PCIE_2	0x3a42		/* 82801JI PCIE */
#define	PCI_PRODUCT_INTEL_82801JI_PCIE_3	0x3a44		/* 82801JI PCIE */
#define	PCI_PRODUCT_INTEL_82801JI_PCIE_4	0x3a46		/* 82801JI PCIE */
#define	PCI_PRODUCT_INTEL_82801JI_PCIE_5	0x3a48		/* 82801JI PCIE */
#define	PCI_PRODUCT_INTEL_82801JI_PCIE_6	0x3a4a		/* 82801JI PCIE */
#define	PCI_PRODUCT_INTEL_82801JDO_VECI	0x3a51		/* 82801JDO VECI */
#define	PCI_PRODUCT_INTEL_82801JD_VSATA	0x3a55		/* 82801JD Virtual SATA */
#define	PCI_PRODUCT_INTEL_82801JD_SMB	0x3a60		/* 82801JD SMBus */
#define	PCI_PRODUCT_INTEL_82801JD_THERMAL	0x3a62		/* 82801JD Thermal */
#define	PCI_PRODUCT_INTEL_82801JD_UHCI_1	0x3a64		/* 82801JD USB */
#define	PCI_PRODUCT_INTEL_82801JD_UHCI_2	0x3a65		/* 82801JD USB */
#define	PCI_PRODUCT_INTEL_82801JD_UHCI_3	0x3a66		/* 82801JD USB */
#define	PCI_PRODUCT_INTEL_82801JD_UHCI_4	0x3a67		/* 82801JD USB */
#define	PCI_PRODUCT_INTEL_82801JD_UHCI_5	0x3a68		/* 82801JD USB */
#define	PCI_PRODUCT_INTEL_82801JD_UHCI_6	0x3a69		/* 82801JD USB */
#define	PCI_PRODUCT_INTEL_82801JD_EHCI_1	0x3a6a		/* 82801JD USB */
#define	PCI_PRODUCT_INTEL_82801JD_EHCI_2	0x3a6c		/* 82801JD USB */
#define	PCI_PRODUCT_INTEL_82801JD_HDA	0x3a6e		/* 82801JD HD Audio */
#define	PCI_PRODUCT_INTEL_82801JD_PCIE_1	0x3a70		/* 82801JD PCIE */
#define	PCI_PRODUCT_INTEL_82801JD_PCIE_2	0x3a72		/* 82801JD PCIE */
#define	PCI_PRODUCT_INTEL_82801JD_PCIE_3	0x3a74		/* 82801JD PCIE */
#define	PCI_PRODUCT_INTEL_82801JD_PCIE_4	0x3a76		/* 82801JD PCIE */
#define	PCI_PRODUCT_INTEL_82801JD_PCIE_5	0x3a78		/* 82801JD PCIE */
#define	PCI_PRODUCT_INTEL_82801JD_PCIE_6	0x3a7a		/* 82801JD PCIE */
#define	PCI_PRODUCT_INTEL_82801JD_LAN	0x3a7c		/* 82801JD LAN */
#define	PCI_PRODUCT_INTEL_P55_LPC_1	0x3b00		/* P55 LPC */
#define	PCI_PRODUCT_INTEL_P55_LPC_2	0x3b02		/* P55 LPC */
#define	PCI_PRODUCT_INTEL_PM55_LPC	0x3b03		/* PM55 LPC */
#define	PCI_PRODUCT_INTEL_H55_LPC	0x3b06		/* H55 LPC */
#define	PCI_PRODUCT_INTEL_QM57_LPC	0x3b07		/* QM57 LPC */
#define	PCI_PRODUCT_INTEL_H57_LPC	0x3b08		/* H57 LPC */
#define	PCI_PRODUCT_INTEL_HM55_LPC	0x3b09		/* HM55 LPC */
#define	PCI_PRODUCT_INTEL_Q57_LPC	0x3b0a		/* Q57 LPC */
#define	PCI_PRODUCT_INTEL_HM57_LPC	0x3b0b		/* HM57 LPC */
#define	PCI_PRODUCT_INTEL_QS57_LPC	0x3b0f		/* QS57 LPC */
#define	PCI_PRODUCT_INTEL_3400_LPC	0x3b12		/* 3400 LPC */
#define	PCI_PRODUCT_INTEL_3420_LPC	0x3b14		/* 3420 LPC */
#define	PCI_PRODUCT_INTEL_3450_LPC	0x3b16		/* 3450 LPC */
#define	PCI_PRODUCT_INTEL_3400_SATA_1	0x3b20		/* 3400 SATA */
#define	PCI_PRODUCT_INTEL_3400_SATA_2	0x3b21		/* 3400 SATA */
#define	PCI_PRODUCT_INTEL_3400_AHCI_1	0x3b22		/* 3400 AHCI */
#define	PCI_PRODUCT_INTEL_3400_AHCI_2	0x3b23		/* 3400 AHCI */
#define	PCI_PRODUCT_INTEL_3400_RAID_1	0x3b25		/* 3400 RAID */
#define	PCI_PRODUCT_INTEL_3400_SATA_3	0x3b26		/* 3400 SATA */
#define	PCI_PRODUCT_INTEL_3400_SATA_4	0x3b28		/* 3400 SATA */
#define	PCI_PRODUCT_INTEL_3400_AHCI_3	0x3b29		/* 3400 AHCI */
#define	PCI_PRODUCT_INTEL_3400_RAID_2	0x3b2c		/* 3400 RAID */
#define	PCI_PRODUCT_INTEL_3400_SATA_5	0x3b2d		/* 3400 SATA */
#define	PCI_PRODUCT_INTEL_3400_SATA_6	0x3b2e		/* 3400 SATA */
#define	PCI_PRODUCT_INTEL_3400_AHCI_4	0x3b2f		/* 3400 AHCI */
#define	PCI_PRODUCT_INTEL_3400_SMB	0x3b30		/* 3400 SMBus */
#define	PCI_PRODUCT_INTEL_3400_THERMAL	0x3b32		/* 3400 Thermal */
#define	PCI_PRODUCT_INTEL_3400_EHCI_1	0x3b34		/* 3400 USB */
#define	PCI_PRODUCT_INTEL_3400_UHCI_1	0x3b36		/* 3400 USB */
#define	PCI_PRODUCT_INTEL_3400_UHCI_2	0x3b37		/* 3400 USB */
#define	PCI_PRODUCT_INTEL_3400_UHCI_3	0x3b38		/* 3400 USB */
#define	PCI_PRODUCT_INTEL_3400_UHCI_4	0x3b39		/* 3400 USB */
#define	PCI_PRODUCT_INTEL_3400_UHCI_5	0x3b3a		/* 3400 USB */
#define	PCI_PRODUCT_INTEL_3400_UHCI_6	0x3b3b		/* 3400 USB */
#define	PCI_PRODUCT_INTEL_3400_EHCI_2	0x3b3c		/* 3400 USB */
#define	PCI_PRODUCT_INTEL_3400_UHCI_7	0x3b3e		/* 3400 USB */
#define	PCI_PRODUCT_INTEL_3400_UHCI_8	0x3b3f		/* 3400 USB */
#define	PCI_PRODUCT_INTEL_3400_PCIE_1	0x3b42		/* 3400 PCIE */
#define	PCI_PRODUCT_INTEL_3400_PCIE_2	0x3b44		/* 3400 PCIE */
#define	PCI_PRODUCT_INTEL_3400_PCIE_3	0x3b46		/* 3400 PCIE */
#define	PCI_PRODUCT_INTEL_3400_PCIE_4	0x3b48		/* 3400 PCIE */
#define	PCI_PRODUCT_INTEL_3400_PCIE_5	0x3b4a		/* 3400 PCIE */
#define	PCI_PRODUCT_INTEL_3400_PCIE_6	0x3b4c		/* 3400 PCIE */
#define	PCI_PRODUCT_INTEL_3400_PCIE_7	0x3b4e		/* 3400 PCIE */
#define	PCI_PRODUCT_INTEL_3400_PCIE_8	0x3b50		/* 3400 PCIE */
#define	PCI_PRODUCT_INTEL_3400_HDA	0x3b56		/* 3400 HD Audio */
#define	PCI_PRODUCT_INTEL_QS57_HDA	0x3b57		/* QS57 HD Audio */
#define	PCI_PRODUCT_INTEL_3400_MEI_1	0x3b64		/* 3400 MEI */
#define	PCI_PRODUCT_INTEL_3400_MEI_2	0x3b65		/* 3400 MEI */
#define	PCI_PRODUCT_INTEL_3400_PT_IDER	0x3b66		/* 3400 PT IDER */
#define	PCI_PRODUCT_INTEL_3400_KT	0x3b67		/* 3400 KT */
#define	PCI_PRODUCT_INTEL_E5_HB	0x3c00		/* E5 Host */
#define	PCI_PRODUCT_INTEL_E5_PCIE_11	0x3c01		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_PCIE_1	0x3c02		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_PCIE_2	0x3c03		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_PCIE_3	0x3c04		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_PCIE_4	0x3c05		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_PCIE_5	0x3c06		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_PCIE_6	0x3c07		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_PCIE_7	0x3c08		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_PCIE_8	0x3c09		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_PCIE_9	0x3c0a		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_PCIE_10	0x3c0b		/* E5 PCIE */
#define	PCI_PRODUCT_INTEL_E5_DMA_1	0x3c20		/* E5 DMA */
#define	PCI_PRODUCT_INTEL_E5_DMA_2	0x3c21		/* E5 DMA */
#define	PCI_PRODUCT_INTEL_E5_DMA_3	0x3c22		/* E5 DMA */
#define	PCI_PRODUCT_INTEL_E5_DMA_4	0x3c23		/* E5 DMA */
#define	PCI_PRODUCT_INTEL_E5_DMA_5	0x3c24		/* E5 DMA */
#define	PCI_PRODUCT_INTEL_E5_DMA_6	0x3c25		/* E5 DMA */
#define	PCI_PRODUCT_INTEL_E5_DMA_7	0x3c26		/* E5 DMA */
#define	PCI_PRODUCT_INTEL_E5_DMA_8	0x3c27		/* E5 DMA */
#define	PCI_PRODUCT_INTEL_E5_ADDRMAP	0x3c28		/* E5 Address Map */
#define	PCI_PRODUCT_INTEL_E5_ERR	0x3c2a		/* E5 Error Reporting */
#define	PCI_PRODUCT_INTEL_E5_IOAPIC	0x3c2c		/* E5 I/O APIC */
#define	PCI_PRODUCT_INTEL_E5_R2PCIE_MON	0x3c43		/* E5 PCIE Monitor */
#define	PCI_PRODUCT_INTEL_E5_QPI_L_MON_0	0x3c44		/* E5 QPI Link Monitor */
#define	PCI_PRODUCT_INTEL_E5_QPI_L_MON_1	0x3c45		/* E5 QPI Link Monitor */
#define	PCI_PRODUCT_INTEL_E5_HA_2	0x3c46		/* E5 Home Agent */
#define	PCI_PRODUCT_INTEL_E5_RAS	0x3c71		/* E5 RAS */
#define	PCI_PRODUCT_INTEL_E5_QPI_L_0	0x3c80		/* E5 QPI Link */
#define	PCI_PRODUCT_INTEL_E5_QPI_L_1	0x3c90		/* E5 QPI Link */
#define	PCI_PRODUCT_INTEL_E5_HA_1	0x3ca0		/* E5 Home Agent */
#define	PCI_PRODUCT_INTEL_E5_TA	0x3ca8		/* E5 TA */
#define	PCI_PRODUCT_INTEL_E5_TAD_1	0x3caa		/* E5 TAD */
#define	PCI_PRODUCT_INTEL_E5_TAD_2	0x3cab		/* E5 TAD */
#define	PCI_PRODUCT_INTEL_E5_TAD_3	0x3cac		/* E5 TAD */
#define	PCI_PRODUCT_INTEL_E5_TAD_4	0x3cad		/* E5 TAD */
#define	PCI_PRODUCT_INTEL_E5_TAD_5	0x3cae		/* E5 TAD */
#define	PCI_PRODUCT_INTEL_E5_THERMAL_1	0x3cb0		/* E5 Thermal */
#define	PCI_PRODUCT_INTEL_E5_THERMAL_2	0x3cb1		/* E5 Thermal */
#define	PCI_PRODUCT_INTEL_E5_ERR_2	0x3cb2		/* E5 Error */
#define	PCI_PRODUCT_INTEL_E5_ERR_3	0x3cb3		/* E5 Error */
#define	PCI_PRODUCT_INTEL_E5_THERMAL_3	0x3cb4		/* E5 Thermal */
#define	PCI_PRODUCT_INTEL_E5_THERMAL_4	0x3cb5		/* E5 Thermal */
#define	PCI_PRODUCT_INTEL_E5_ERR_4	0x3cb6		/* E5 Error */
#define	PCI_PRODUCT_INTEL_E5_ERR_5	0x3cb7		/* E5 Error */
#define	PCI_PRODUCT_INTEL_E5_DDRIO	0x3cb8		/* E5 DDRIO */
#define	PCI_PRODUCT_INTEL_E5_PCU_0	0x3cc0		/* E5 PCU */
#define	PCI_PRODUCT_INTEL_E5_PCU_1	0x3cc1		/* E5 PCU */
#define	PCI_PRODUCT_INTEL_E5_PCU_2	0x3cc2		/* E5 PCU */
#define	PCI_PRODUCT_INTEL_E5_PCU_3	0x3cd0		/* E5 PCU */
#define	PCI_PRODUCT_INTEL_E5_SCRATCH_1	0x3ce0		/* E5 Scratch */
#define	PCI_PRODUCT_INTEL_E5_SCRATCH_2	0x3ce3		/* E5 Scratch */
#define	PCI_PRODUCT_INTEL_E5_R2PCIE	0x3ce4		/* E5 R2PCIE */
#define	PCI_PRODUCT_INTEL_E5_R3_QPI	0x3ce6		/* E5 QPI */
#define	PCI_PRODUCT_INTEL_E5_UNICAST	0x3ce8		/* E5 Unicast */
#define	PCI_PRODUCT_INTEL_E5_SAD_1	0x3cf4		/* E5 SAD */
#define	PCI_PRODUCT_INTEL_E5_BROADCAST	0x3cf5		/* E5 Broadcast */
#define	PCI_PRODUCT_INTEL_E5_SAD_2	0x3cf6		/* E5 SAD */
#define	PCI_PRODUCT_INTEL_WL_22500_7	0x3df0		/* Wi-Fi 6 AX201 */
#define	PCI_PRODUCT_INTEL_CORE8G_S_D_HB_2C	0x3e0f		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_H_HB_4C	0x3e10		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_S_W_HB_4C	0x3e18		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_S_D_HB_4C	0x3e1f		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_S_D_HB_8C	0x3e30		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_S_W_HB_8C	0x3e31		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_S_S_HB_8C	0x3e32		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_S_S_HB_4C	0x3e33		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_WHLU_HB_4C	0x3e34		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_WHLU_HB_2C	0x3e35		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_S_PCIE_X16	0x3e81		/* Core 8G PCIE */
#define	PCI_PRODUCT_INTEL_CORE8G_S_PCIE_X8	0x3e85		/* Core 8G PCIE */
#define	PCI_PRODUCT_INTEL_CORE8G_S_PCIE_X4	0x3e89		/* Core 8G PCIE */
#define	PCI_PRODUCT_INTEL_CFL_S_GT1_1	0x3e90		/* UHD Graphics 610 */
#define	PCI_PRODUCT_INTEL_CFL_S_GT2_1	0x3e91		/* UHD Graphics 630 */
#define	PCI_PRODUCT_INTEL_CFL_S_GT2_2	0x3e92		/* UHD Graphics 630 */
#define	PCI_PRODUCT_INTEL_CFL_S_GT1_2	0x3e93		/* UHD Graphics 610 */
#define	PCI_PRODUCT_INTEL_CFL_H_GT2_2	0x3e94		/* UHD Graphics P630 */
#define	PCI_PRODUCT_INTEL_CFL_S_GT2_3	0x3e96		/* UHD Graphics P630 */
#define	PCI_PRODUCT_INTEL_CFL_S_GT2_4	0x3e98		/* UHD Graphics 630 */
#define	PCI_PRODUCT_INTEL_CFL_S_GT1_3	0x3e99		/* UHD Graphics 610 */
#define	PCI_PRODUCT_INTEL_CFL_S_GT2_5	0x3e9a		/* UHD Graphics P630 */
#define	PCI_PRODUCT_INTEL_CFL_H_GT2_1	0x3e9b		/* UHD Graphics 630 */
#define	PCI_PRODUCT_INTEL_CFL_H_GT1	0x3e9c		/* UHD Graphics 610 */
#define	PCI_PRODUCT_INTEL_WHL_U_GT2_1	0x3ea0		/* UHD Graphics 620 */
#define	PCI_PRODUCT_INTEL_WHL_U_GT1_1	0x3ea1		/* UHD Graphics 610 */
#define	PCI_PRODUCT_INTEL_WHL_U_GT3	0x3ea2		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_WHL_U_GT2_2	0x3ea3		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_WHL_U_GT1_2	0x3ea4		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CFL_U_GT3_1	0x3ea5		/* Iris Plus 655 */
#define	PCI_PRODUCT_INTEL_CFL_U_GT3_2	0x3ea6		/* Iris Plus 645 */
#define	PCI_PRODUCT_INTEL_CFL_U_GT3_3	0x3ea7		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CFL_U_GT3_4	0x3ea8		/* Iris Plus 655 */
#define	PCI_PRODUCT_INTEL_CFL_U_GT2_2	0x3ea9		/* UHD Graphics 620 */
#define	PCI_PRODUCT_INTEL_CORE8G_S_D_HB_6C	0x3ec2		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_H_HB_6C	0x3ec4		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_S_W_HB_6C	0x3ec6		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_S_S_HB_6C	0x3eca		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_U_HB_2C	0x3ecc		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_CORE8G_U_HB_4C	0x3ed0		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_5400_HB	0x4000		/* 5400 Host */
#define	PCI_PRODUCT_INTEL_5400A_HB	0x4001		/* 5400A Host */
#define	PCI_PRODUCT_INTEL_5400B_HB	0x4003		/* 5400B Host */
#define	PCI_PRODUCT_INTEL_5400_PCIE_1	0x4021		/* 5400 PCIE */
#define	PCI_PRODUCT_INTEL_5400_PCIE_2	0x4022		/* 5400 PCIE */
#define	PCI_PRODUCT_INTEL_5400_PCIE_3	0x4023		/* 5400 PCIE */
#define	PCI_PRODUCT_INTEL_5400_PCIE_4	0x4024		/* 5400 PCIE */
#define	PCI_PRODUCT_INTEL_5400_PCIE_5	0x4025		/* 5400 PCIE */
#define	PCI_PRODUCT_INTEL_5400_PCIE_6	0x4026		/* 5400 PCIE */
#define	PCI_PRODUCT_INTEL_5400_PCIE_7	0x4027		/* 5400 PCIE */
#define	PCI_PRODUCT_INTEL_5400_PCIE_8	0x4028		/* 5400 PCIE */
#define	PCI_PRODUCT_INTEL_5400_PCIE_9	0x4029		/* 5400 PCIE */
#define	PCI_PRODUCT_INTEL_IOAT_SNB	0x402f		/* I/OAT SNB */
#define	PCI_PRODUCT_INTEL_5400_FSBINT	0x4030		/* 5400 FSB/Boot/Interrupt */
#define	PCI_PRODUCT_INTEL_5400_CE	0x4031		/* 5400 Coherency Engine */
#define	PCI_PRODUCT_INTEL_5400_IOAPIC	0x4032		/* 5400 IOAPIC */
#define	PCI_PRODUCT_INTEL_5400_RAS_0	0x4035		/* 5400 RAS */
#define	PCI_PRODUCT_INTEL_5400_RAS_1	0x4036		/* 5400 RAS */
#define	PCI_PRODUCT_INTEL_GMA600_0	0x4100		/* GMA 600 */
#define	PCI_PRODUCT_INTEL_GMA600_1	0x4101		/* GMA 600 */
#define	PCI_PRODUCT_INTEL_GMA600_2	0x4102		/* GMA 600 */
#define	PCI_PRODUCT_INTEL_GMA600_3	0x4103		/* GMA 600 */
#define	PCI_PRODUCT_INTEL_GMA600_4	0x4104		/* GMA 600 */
#define	PCI_PRODUCT_INTEL_GMA600_5	0x4105		/* GMA 600 */
#define	PCI_PRODUCT_INTEL_GMA600_6	0x4106		/* GMA 600 */
#define	PCI_PRODUCT_INTEL_GMA600_7	0x4107		/* GMA 600 */
#define	PCI_PRODUCT_INTEL_GMA600_8	0x4108		/* GMA 600 */
#define	PCI_PRODUCT_INTEL_E600_HB	0x4114		/* E600 Host */
#define	PCI_PRODUCT_INTEL_P5800X	0x4140		/* P5800X */
#define	PCI_PRODUCT_INTEL_PRO_WL_2200BG	0x4220		/* PRO/Wireless 2200BG */
#define	PCI_PRODUCT_INTEL_PRO_WL_2225BG	0x4221		/* PRO/Wireless 2225BG */
#define	PCI_PRODUCT_INTEL_PRO_WL_3945ABG_1	0x4222		/* PRO/Wireless 3945ABG */
#define	PCI_PRODUCT_INTEL_PRO_WL_2915ABG_1	0x4223		/* PRO/Wireless 2915ABG */
#define	PCI_PRODUCT_INTEL_PRO_WL_2915ABG_2	0x4224		/* PRO/Wireless 2915ABG */
#define	PCI_PRODUCT_INTEL_PRO_WL_3945ABG_2	0x4227		/* PRO/Wireless 3945ABG */
#define	PCI_PRODUCT_INTEL_WL_4965_1	0x4229		/* Wireless WiFi Link 4965 */
#define	PCI_PRODUCT_INTEL_WL_6300_1	0x422b		/* Centrino Ultimate-N 6300 */
#define	PCI_PRODUCT_INTEL_WL_6200_1	0x422c		/* Centrino Advanced-N 6200 */
#define	PCI_PRODUCT_INTEL_WL_4965_2	0x4230		/* Wireless WiFi Link 4965 */
#define	PCI_PRODUCT_INTEL_WL_5100_1	0x4232		/* WiFi Link 5100 */
#define	PCI_PRODUCT_INTEL_WL_5300_1	0x4235		/* WiFi Link 5300 */
#define	PCI_PRODUCT_INTEL_WL_5300_2	0x4236		/* WiFi Link 5300 */
#define	PCI_PRODUCT_INTEL_WL_5100_2	0x4237		/* WiFi Link 5100 */
#define	PCI_PRODUCT_INTEL_WL_6300_2	0x4238		/* Centrino Ultimate-N 6300 */
#define	PCI_PRODUCT_INTEL_WL_6200_2	0x4239		/* Centrino Advanced-N 6200 */
#define	PCI_PRODUCT_INTEL_WL_5350_1	0x423a		/* WiFi Link 5350 */
#define	PCI_PRODUCT_INTEL_WL_5350_2	0x423b		/* WiFi Link 5350 */
#define	PCI_PRODUCT_INTEL_WL_5150_1	0x423c		/* WiFi Link 5150 */
#define	PCI_PRODUCT_INTEL_WL_5150_2	0x423d		/* WiFi Link 5150 */
#define	PCI_PRODUCT_INTEL_Q570_ESPI	0x4384		/* Q570 eSPI */
#define	PCI_PRODUCT_INTEL_Z590_ESPI	0x4385		/* Z590 eSPI */
#define	PCI_PRODUCT_INTEL_H570_ESPI	0x4386		/* H570 eSPI */
#define	PCI_PRODUCT_INTEL_B560_ESPI	0x4387		/* B560 eSPI */
#define	PCI_PRODUCT_INTEL_H510_ESPI	0x4388		/* H510 eSPI */
#define	PCI_PRODUCT_INTEL_WM590_ESPI	0x4389		/* WM590 eSPI */
#define	PCI_PRODUCT_INTEL_QM580_ESPI	0x438a		/* QM580 eSPI */
#define	PCI_PRODUCT_INTEL_HM570_ESPI	0x438b		/* HM570 eSPI */
#define	PCI_PRODUCT_INTEL_W580_ESPI	0x438f		/* W580 eSPI */
#define	PCI_PRODUCT_INTEL_500SERIES_P2SB	0x43a0		/* 500 Series P2SB */
#define	PCI_PRODUCT_INTEL_500SERIES_PMC	0x43a1		/* 500 Series PMC */
#define	PCI_PRODUCT_INTEL_500SERIES_SMB	0x43a3		/* 500 Series SMBus */
#define	PCI_PRODUCT_INTEL_500SERIES_SPI	0x43a4		/* 500 Series SPI */
#define	PCI_PRODUCT_INTEL_500SERIES_TH	0x43a6		/* 500 Series TH */
#define	PCI_PRODUCT_INTEL_500SERIES_UART_2	0x43a7		/* 500 Series UART */
#define	PCI_PRODUCT_INTEL_500SERIES_UART_0	0x43a8		/* 500 Series UART */
#define	PCI_PRODUCT_INTEL_500SERIES_UART_1	0x43a9		/* 500 Series UART */
#define	PCI_PRODUCT_INTEL_500SERIES_GSPI_0	0x43aa		/* 500 Series GSPI */
#define	PCI_PRODUCT_INTEL_500SERIES_GSPI_1	0x43ab		/* 500 Series GSPI */
#define	PCI_PRODUCT_INTEL_500SERIES_I2C_4	0x43ad		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_I2C_5	0x43ae		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_9	0x43b0		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_10	0x43b1		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_11	0x43b2		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_12	0x43b3		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_13	0x43b4		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_14	0x43b5		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_15	0x43b6		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_16	0x43b7		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_1	0x43b8		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_2	0x43b9		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_3	0x43ba		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_4	0x43bb		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_5	0x43bc		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_6	0x43bd		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_7	0x43be		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_8	0x43bf		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_17	0x43c0		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_18	0x43c1		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_19	0x43c2		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_20	0x43c3		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_21	0x43c4		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_22	0x43c5		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_23	0x43c6		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_PCIE_24	0x43c7		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_HDA	0x43c8		/* 500 Series HD Audio */
#define	PCI_PRODUCT_INTEL_500SERIES_THC_0	0x43d0		/* 500 Series THC */
#define	PCI_PRODUCT_INTEL_500SERIES_THC_1	0x43d1		/* 500 Series THC */
#define	PCI_PRODUCT_INTEL_500SERIES_AHCI_1	0x43d2		/* 500 Series AHCI */
#define	PCI_PRODUCT_INTEL_500SERIES_AHCI_2	0x43d3		/* 500 Series AHCI */
#define	PCI_PRODUCT_INTEL_500SERIES_RAID_4	0x43d4		/* 500 Series RAID */
#define	PCI_PRODUCT_INTEL_500SERIES_RAID_5	0x43d5		/* 500 Series RAID */
#define	PCI_PRODUCT_INTEL_500SERIES_RAID_6	0x43d6		/* 500 Series RAID */
#define	PCI_PRODUCT_INTEL_500SERIES_RAID_7	0x43d7		/* 500 Series RAID */
#define	PCI_PRODUCT_INTEL_500SERIES_I2C_6	0x43d8		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_UART_3	0x43da		/* 500 Series UART */
#define	PCI_PRODUCT_INTEL_500SERIES_HECI_1	0x43e0		/* 500 Series HECI */
#define	PCI_PRODUCT_INTEL_500SERIES_HECI_2	0x43e1		/* 500 Series HECI */
#define	PCI_PRODUCT_INTEL_500SERIES_IDER	0x43e2		/* 500 Series IDE-R */
#define	PCI_PRODUCT_INTEL_500SERIES_KT	0x43e3		/* 500 Series KT */
#define	PCI_PRODUCT_INTEL_500SERIES_HECI_3	0x43e4		/* 500 Series HECI */
#define	PCI_PRODUCT_INTEL_500SERIES_HECI_4	0x43e5		/* 500 Series HECI */
#define	PCI_PRODUCT_INTEL_500SERIES_I2C_0	0x43e8		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_I2C_1	0x43e9		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_I2C_2	0x43ea		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_I2C_3	0x43eb		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_XHCI	0x43ed		/* 500 Series xHCI */
#define	PCI_PRODUCT_INTEL_500SERIES_XDCI	0x43ee		/* 500 Series xDCI */
#define	PCI_PRODUCT_INTEL_500SERIES_SRAM	0x43ef		/* 500 Series Shared SRAM */
#define	PCI_PRODUCT_INTEL_WL_22500_6	0x43f0		/* Wi-Fi 6 AX201 */
#define	PCI_PRODUCT_INTEL_500SERIES_GSPI_2	0x43fb		/* 500 Series GSPI */
#define	PCI_PRODUCT_INTEL_500SERIES_ISH	0x43fc		/* 500 Series ISH */
#define	PCI_PRODUCT_INTEL_500SERIES_GSPI_3	0x43fd		/* 500 Series GSPI */
#define	PCI_PRODUCT_INTEL_TURBO_MEMORY	0x444e		/* Turbo Memory */
#define	PCI_PRODUCT_INTEL_EHL_DPTF	0x4503		/* Elkhart Lake DPTF */
#define	PCI_PRODUCT_INTEL_EHL_GNA	0x4511		/* Elkhart Lake GNA */
#define	PCI_PRODUCT_INTEL_EHL_HB_1	0x4512		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_2	0x4514		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_3	0x4516		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_4	0x4518		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_5	0x451e		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_6	0x4522		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_7	0x4526		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_8	0x4528		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_GCD_TH	0x4529		/* Elkhart Lake TH */
#define	PCI_PRODUCT_INTEL_EHL_HB_9	0x452a		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_10	0x452c		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_11	0x452e		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_12	0x4532		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_13	0x4538		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_HB_14	0x453a		/* Elkhart Lake Host */
#define	PCI_PRODUCT_INTEL_EHL_GT_1	0x4541		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_EHL_GT_2	0x4551		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_EHL_GT_3	0x4555		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_EHL_GT_4	0x4557		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_EHL_GT_6	0x4570		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_EHL_GT_7	0x4571		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_ADL_PU15_HB_1	0x4601		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_U9_HB_1	0x4602		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_U15_HB_1	0x4609		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_U9_HB_2	0x460a		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_S_PCIE_1	0x460d		/* Core 12G PCIE */
#define	PCI_PRODUCT_INTEL_ADL_XDCI	0x460e		/* Core 12G xDCI */
#define	PCI_PRODUCT_INTEL_ADL_S_HB_6	0x4610		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_N_HB_1	0x4617		/* ADL-N Host */
#define	PCI_PRODUCT_INTEL_ADL_U15_HB_2	0x4619		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_U9_HB_3	0x461a		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_N_HB_2	0x461b		/* N200 Host */
#define	PCI_PRODUCT_INTEL_ADL_N_HB_3	0x461c		/* N100 Host */
#define	PCI_PRODUCT_INTEL_ADL_S_DTT	0x461d		/* Core 12G DTT */
#define	PCI_PRODUCT_INTEL_ADL_XHCI	0x461e		/* Core 12G xHCI */
#define	PCI_PRODUCT_INTEL_ADL_TBT_PCIE3	0x461f		/* Core 12G PCIE */
#define	PCI_PRODUCT_INTEL_ADL_HP_HB_2	0x4621		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_HX_HB_3	0x4623		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_1	0x4626		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_2	0x4628		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_H_HB_2	0x4629		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_3	0x462a		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_HX_HB_4	0x462b		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_S_PCIE_2	0x462d		/* Core 12G PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_IPU	0x462e		/* ADL-N IPU */
#define	PCI_PRODUCT_INTEL_ADL_TBT_PCIE2	0x462f		/* Core 12G PCIE */
#define	PCI_PRODUCT_INTEL_ADL_S_HB_5	0x4630		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_HX_HB_1	0x4637		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_HX_HB_2	0x463b		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_PCIE_1	0x463d		/* Core 12G PCIE */
#define	PCI_PRODUCT_INTEL_ADL_TBT_DMA0	0x463e		/* Core 12G TBT */
#define	PCI_PRODUCT_INTEL_ADL_TBT_PCIE1	0x463f		/* Core 12G PCIE */
#define	PCI_PRODUCT_INTEL_RPL_S_HB_1	0x4640		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_ADL_HP_HB_1	0x4641		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_RPL_HX_HB_1	0x4647		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_ADL_S_HB_3	0x4648		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_H_HB_1	0x4649		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_S_PCIE_3	0x464d		/* Core 12G PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_XHCI_2	0x464e		/* ADL-N xHCI */
#define	PCI_PRODUCT_INTEL_ADL_S_GNA	0x464f		/* Core 12G GNA */
#define	PCI_PRODUCT_INTEL_ADL_S_HB_4	0x4650		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_IPU	0x465d		/* Core 12G IPU */
#define	PCI_PRODUCT_INTEL_ADL_N_XDCI_2	0x465e		/* ADL-N xDCI */
#define	PCI_PRODUCT_INTEL_ADL_S_HB_1	0x4660		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_S_HB_2	0x4668		/* Core 12G Host */
#define	PCI_PRODUCT_INTEL_ADL_TBT_DMA1	0x466d		/* Core 12G TBT */
#define	PCI_PRODUCT_INTEL_ADL_TBT_PCIE0	0x466e		/* Core 12G PCIE */
#define	PCI_PRODUCT_INTEL_ADL_S_TH	0x466f		/* Core 12G TH */
#define	PCI_PRODUCT_INTEL_ADL_S_CL	0x467d		/* Core 12G CL */
#define	PCI_PRODUCT_INTEL_ADL_N_GNA	0x467e		/* ADL-N GNA */
#define	PCI_PRODUCT_INTEL_ADL_S_VMD	0x467f		/* Core 12G VMD */
#define	PCI_PRODUCT_INTEL_ADL_S_GT1_1	0x4680		/* UHD Graphics 770 */
#define	PCI_PRODUCT_INTEL_ADL_S_GT1_2	0x4682		/* UHD Graphics 730 */
#define	PCI_PRODUCT_INTEL_ADL_S_GT1_3	0x4688		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_ADL_S_GT1_4	0x468a		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_ADL_S_GT0_1	0x468b		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_ADL_S_GT1_5	0x4690		/* UHD Graphics 770 */
#define	PCI_PRODUCT_INTEL_ADL_S_GT1_6	0x4692		/* UHD Graphics 730 */
#define	PCI_PRODUCT_INTEL_ADL_S_GT1_7	0x4693		/* UHD Graphics 710 */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_4	0x46a0		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_5	0x46a1		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_6	0x46a2		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_7	0x46a3		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_8	0x46a6		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_9	0x46a8		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_10	0x46aa		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_11	0x46b0		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_12	0x46b1		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_13	0x46b2		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_14	0x46b3		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_15	0x46c0		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_16	0x46c1		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_17	0x46c2		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_P_GT2_18	0x46c3		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_N_GT_1	0x46d0		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_N_GT_2	0x46d1		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_N_GT_3	0x46d2		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_N_GT_4	0x46d3		/* Graphics */
#define	PCI_PRODUCT_INTEL_ADL_N_GT_5	0x46d4		/* Graphics */
#define	PCI_PRODUCT_INTEL_DG1_1	0x4905		/* Iris Xe MAX */
#define	PCI_PRODUCT_INTEL_DG1_2	0x4906		/* Graphics */
#define	PCI_PRODUCT_INTEL_DG1_3	0x4907		/* SG-18M */
#define	PCI_PRODUCT_INTEL_DG1_4	0x4908		/* Graphics */
#define	PCI_PRODUCT_INTEL_DG1_5	0x4909		/* Graphics */
#define	PCI_PRODUCT_INTEL_EHL_ESPI	0x4b00		/* Elkhart Lake eSPI */
#define	PCI_PRODUCT_INTEL_EHL_P2SB	0x4b20		/* Elkhart Lake P2SB */
#define	PCI_PRODUCT_INTEL_EHL_PMC	0x4b21		/* Elkhart Lake PMC */
#define	PCI_PRODUCT_INTEL_EHL_SMB	0x4b23		/* Elkhart Lake SMBus */
#define	PCI_PRODUCT_INTEL_EHL_SPI	0x4b24		/* Elkhart Lake SPI */
#define	PCI_PRODUCT_INTEL_EHL_TH	0x4b26		/* Elkhart Lake TH */
#define	PCI_PRODUCT_INTEL_EHL_SIO_UART_0	0x4b28		/* Elkhart Lake UART */
#define	PCI_PRODUCT_INTEL_EHL_SIO_UART_1	0x4b29		/* Elkhart Lake UART */
#define	PCI_PRODUCT_INTEL_EHL_SIO_SPI_0	0x4b2a		/* Elkhart Lake SPI */
#define	PCI_PRODUCT_INTEL_EHL_SIO_SPI_1	0x4b2b		/* Elkhart Lake SPI */
#define	PCI_PRODUCT_INTEL_EHL_IEH	0x4b2f		/* Elkhart Lake IEH */
#define	PCI_PRODUCT_INTEL_EHL_SGMII	0x4b32		/* Elkhart Lake Ethernet */
#define	PCI_PRODUCT_INTEL_EHL_SIO_SPI_2	0x4b37		/* Elkhart Lake SPI */
#define	PCI_PRODUCT_INTEL_EHL_PCIE_0	0x4b38		/* Elkhart Lake PCIE */
#define	PCI_PRODUCT_INTEL_EHL_PCIE_1	0x4b39		/* Elkhart Lake PCIE */
#define	PCI_PRODUCT_INTEL_EHL_PCIE_2	0x4b3a		/* Elkhart Lake PCIE */
#define	PCI_PRODUCT_INTEL_EHL_PCIE_3	0x4b3b		/* Elkhart Lake PCIE */
#define	PCI_PRODUCT_INTEL_EHL_PCIE_4	0x4b3c		/* Elkhart Lake PCIE */
#define	PCI_PRODUCT_INTEL_EHL_PCIE_5	0x4b3d		/* Elkhart Lake PCIE */
#define	PCI_PRODUCT_INTEL_EHL_PCIE_6	0x4b3e		/* Elkhart Lake PCIE */
#define	PCI_PRODUCT_INTEL_EHL_SIO_I2C_6	0x4b44		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_SIO_I2C_7	0x4b45		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_EMMC	0x4b47		/* Elkhart Lake eMMC */
#define	PCI_PRODUCT_INTEL_EHL_SDMMC	0x4b48		/* Elkhart Lake SD/MMC */
#define	PCI_PRODUCT_INTEL_EHL_SI	0x4b4a		/* Elkhart Lake SI */
#define	PCI_PRODUCT_INTEL_EHL_SIO_I2C_4	0x4b4b		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_SIO_I2C_5	0x4b4c		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_SIO_UART_2	0x4b4d		/* Elkhart Lake UART */
#define	PCI_PRODUCT_INTEL_EHL_HDA	0x4b58		/* Elkhart Lake HD Audio */
#define	PCI_PRODUCT_INTEL_EHL_AHCI	0x4b63		/* Elkhart Lake AHCI */
#define	PCI_PRODUCT_INTEL_EHL_HPET	0x4b68		/* Elkhart Lake HPET */
#define	PCI_PRODUCT_INTEL_EHL_IOAPIC	0x4b69		/* Elkhart Lake IOAPIC */
#define	PCI_PRODUCT_INTEL_EHL_CSE_PTT_DMA	0x4b6b		/* Elkhart Lake PTT DMA */
#define	PCI_PRODUCT_INTEL_EHL_CSE_UMA	0x4b6c		/* Elkhart Lake UMA */
#define	PCI_PRODUCT_INTEL_EHL_CSE_HECI_0	0x4b70		/* Elkhart Lake HECI */
#define	PCI_PRODUCT_INTEL_EHL_CSE_HECI_1	0x4b71		/* Elkhart Lake HECI */
#define	PCI_PRODUCT_INTEL_EHL_CSE_HECI_2	0x4b74		/* Elkhart Lake HECI */
#define	PCI_PRODUCT_INTEL_EHL_CSE_HECI_3	0x4b75		/* Elkhart Lake HECI */
#define	PCI_PRODUCT_INTEL_EHL_SIO_I2C_0	0x4b78		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_SIO_I2C_1	0x4b79		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_SIO_I2C_2	0x4b7a		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_SIO_I2C_3	0x4b7b		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_XHCI	0x4b7d		/* Elkhart Lake xHCI */
#define	PCI_PRODUCT_INTEL_EHL_XDCI	0x4b7e		/* Elkhart Lake xDCI */
#define	PCI_PRODUCT_INTEL_EHL_SRAM	0x4b7f		/* Elkhart Lake SRAM */
#define	PCI_PRODUCT_INTEL_EHL_PSE_QEP_1	0x4b81		/* Elkhart Lake QEP */
#define	PCI_PRODUCT_INTEL_EHL_PSE_QEP_2	0x4b82		/* Elkhart Lake QEP */
#define	PCI_PRODUCT_INTEL_EHL_PSE_QEP_3	0x4b83		/* Elkhart Lake QEP */
#define	PCI_PRODUCT_INTEL_EHL_PSE_SPI_0	0x4b84		/* Elkhart Lake SPI */
#define	PCI_PRODUCT_INTEL_EHL_PSE_SPI_1	0x4b85		/* Elkhart Lake SPI */
#define	PCI_PRODUCT_INTEL_EHL_PSE_SPI_2	0x4b86		/* Elkhart Lake SPI */
#define	PCI_PRODUCT_INTEL_EHL_PSE_SPI_3	0x4b87		/* Elkhart Lake SPI */
#define	PCI_PRODUCT_INTEL_EHL_PSE_GPIO_0	0x4b88		/* Elkhart Lake GPIO */
#define	PCI_PRODUCT_INTEL_EHL_PSE_GPIO_1	0x4b89		/* Elkhart Lake GPIO */
#define	PCI_PRODUCT_INTEL_EHL_PSE_UART_0	0x4b96		/* Elkhart Lake UART */
#define	PCI_PRODUCT_INTEL_EHL_PSE_UART_1	0x4b97		/* Elkhart Lake UART */
#define	PCI_PRODUCT_INTEL_EHL_PSE_UART_2	0x4b98		/* Elkhart Lake UART */
#define	PCI_PRODUCT_INTEL_EHL_PSE_UART_3	0x4b99		/* Elkhart Lake UART */
#define	PCI_PRODUCT_INTEL_EHL_PSE_UART_4	0x4b9a		/* Elkhart Lake UART */
#define	PCI_PRODUCT_INTEL_EHL_PSE_UART_5	0x4b9b		/* Elkhart Lake UART */
#define	PCI_PRODUCT_INTEL_EHL_PSE_I2S_0	0x4b9c		/* Elkhart Lake I2S */
#define	PCI_PRODUCT_INTEL_EHL_PSE_I2S_1	0x4b9d		/* Elkhart Lake I2S */
#define	PCI_PRODUCT_INTEL_EHL_PSE0_RGMII_1G	0x4ba0		/* Elkhart Lake Ethernet */
#define	PCI_PRODUCT_INTEL_EHL_PSE0_SGMII_1G	0x4ba1		/* Elkhart Lake Ethernet */
#define	PCI_PRODUCT_INTEL_EHL_PSE0_SGMII_2G	0x4ba2		/* Elkhart Lake Ethernet */
#define	PCI_PRODUCT_INTEL_EHL_PSE1_RGMII_1G	0x4bb0		/* Elkhart Lake Ethernet */
#define	PCI_PRODUCT_INTEL_EHL_PSE1_SGMII_1G	0x4bb1		/* Elkhart Lake Ethernet */
#define	PCI_PRODUCT_INTEL_EHL_PSE1_SGMII_2G	0x4bb2		/* Elkhart Lake Ethernet */
#define	PCI_PRODUCT_INTEL_EHL_PSE_LH2OSE	0x4bb3		/* Elkhart Lake LH2OSE */
#define	PCI_PRODUCT_INTEL_EHL_PSE_DMA_0	0x4bb4		/* Elkhart Lake DMA */
#define	PCI_PRODUCT_INTEL_EHL_PSE_DMA_1	0x4bb5		/* Elkhart Lake DMA */
#define	PCI_PRODUCT_INTEL_EHL_PSE_DMA_2	0x4bb6		/* Elkhart Lake DMA */
#define	PCI_PRODUCT_INTEL_EHL_PSE_PWM	0x4bb7		/* Elkhart Lake PWM */
#define	PCI_PRODUCT_INTEL_EHL_PSE_I2C_0	0x4bb9		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_PSE_I2C_1	0x4bba		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_PSE_I2C_2	0x4bbb		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_PSE_I2C_3	0x4bbc		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_PSE_I2C_4	0x4bbd		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_PSE_I2C_5	0x4bbe		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_PSE_I2C_6	0x4bbf		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_PSE_I2C_7	0x4bc0		/* Elkhart Lake I2C */
#define	PCI_PRODUCT_INTEL_EHL_PSE_CAN_0	0x4bc1		/* Elkhart Lake CAN */
#define	PCI_PRODUCT_INTEL_EHL_PSE_CAN_1	0x4bc2		/* Elkhart Lake CAN */
#define	PCI_PRODUCT_INTEL_EHL_PSE_QEP_0	0x4bc3		/* Elkhart Lake QEP */
#define	PCI_PRODUCT_INTEL_RKL_GT_1	0x4c80		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_RKL_GT_2	0x4c8a		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_RKL_GT_3	0x4c8b		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_RKL_GT_4	0x4c8c		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_RKL_GT_5	0x4c90		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_RKL_GT_6	0x4c9a		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_JSL_ESPI	0x4d87		/* Jasper Lake eSPI */
#define	PCI_PRODUCT_INTEL_JSL_P2SB	0x4da0		/* Jasper Lake P2SB */
#define	PCI_PRODUCT_INTEL_JSL_PMC	0x4da1		/* Jasper Lake PMC */
#define	PCI_PRODUCT_INTEL_JSL_SMB	0x4da3		/* Jasper Lake SMBus */
#define	PCI_PRODUCT_INTEL_JSL_SPI	0x4da4		/* Jasper Lake SPI */
#define	PCI_PRODUCT_INTEL_JSL_ITH	0x4da6		/* Jasper Lake ITH */
#define	PCI_PRODUCT_INTEL_JSL_UART_0	0x4da8		/* Jasper Lake UART */
#define	PCI_PRODUCT_INTEL_JSL_UART_1	0x4da9		/* Jasper Lake UART */
#define	PCI_PRODUCT_INTEL_JSL_LPSS_SPI_0	0x4daa		/* Jasper Lake SPI */
#define	PCI_PRODUCT_INTEL_JSL_LPSS_SPI_1	0x4dab		/* Jasper Lake SPI */
#define	PCI_PRODUCT_INTEL_JSL_PCIE_1	0x4db8		/* Jasper Lake PCIE */
#define	PCI_PRODUCT_INTEL_JSL_PCIE_2	0x4db9		/* Jasper Lake PCIE */
#define	PCI_PRODUCT_INTEL_JSL_PCIE_3	0x4dba		/* Jasper Lake PCIE */
#define	PCI_PRODUCT_INTEL_JSL_PCIE_4	0x4dbb		/* Jasper Lake PCIE */
#define	PCI_PRODUCT_INTEL_JSL_PCIE_5	0x4dbc		/* Jasper Lake PCIE */
#define	PCI_PRODUCT_INTEL_JSL_PCIE_6	0x4dbd		/* Jasper Lake PCIE */
#define	PCI_PRODUCT_INTEL_JSL_PCIE_7	0x4dbe		/* Jasper Lake PCIE */
#define	PCI_PRODUCT_INTEL_JSL_PCIE_8	0x4dbf		/* Jasper Lake PCIE */
#define	PCI_PRODUCT_INTEL_JSL_EMMC	0x4dc4		/* Jasper Lake eMMC */
#define	PCI_PRODUCT_INTEL_JSL_I2C_4	0x4dc5		/* Jasper Lake I2C */
#define	PCI_PRODUCT_INTEL_JSL_I2C_5	0x4dc6		/* Jasper Lake I2C */
#define	PCI_PRODUCT_INTEL_JSL_LPSS_UART_2	0x4dc7		/* Jasper Lake UART */
#define	PCI_PRODUCT_INTEL_JSL_HDA	0x4dc8		/* Jasper Lake HD Audio */
#define	PCI_PRODUCT_INTEL_JSL_AHCI_1	0x4dd2		/* Jasper Lake AHCI */
#define	PCI_PRODUCT_INTEL_JSL_AHCI_2	0x4dd3		/* Jasper Lake AHCI */
#define	PCI_PRODUCT_INTEL_JSL_RAID_1	0x4dd6		/* Jasper Lake RAID */
#define	PCI_PRODUCT_INTEL_JSL_RAID_2	0x4dd7		/* Jasper Lake RAID */
#define	PCI_PRODUCT_INTEL_JSL_HECI_1	0x4de0		/* Jasper Lake HECI */
#define	PCI_PRODUCT_INTEL_JSL_HECI_2	0x4de1		/* Jasper Lake HECI */
#define	PCI_PRODUCT_INTEL_JSL_HECI_3	0x4de4		/* Jasper Lake HECI */
#define	PCI_PRODUCT_INTEL_JSL_I2C_0	0x4de8		/* Jasper Lake I2C */
#define	PCI_PRODUCT_INTEL_JSL_I2C_1	0x4de9		/* Jasper Lake I2C */
#define	PCI_PRODUCT_INTEL_JSL_I2C_2	0x4dea		/* Jasper Lake I2C */
#define	PCI_PRODUCT_INTEL_JSL_I2C_3	0x4deb		/* Jasper Lake I2C */
#define	PCI_PRODUCT_INTEL_JSL_XHCI	0x4ded		/* Jasper Lake xHCI */
#define	PCI_PRODUCT_INTEL_JSL_XDCI	0x4dee		/* Jasper Lake xDCI */
#define	PCI_PRODUCT_INTEL_JSL_SRAM	0x4def		/* Jasper Lake Shared SRAM */
#define	PCI_PRODUCT_INTEL_WL_22500_8	0x4df0		/* Wi-Fi 6 AX201 */
#define	PCI_PRODUCT_INTEL_JSL_SDXC	0x4df8		/* Jasper Lake SDXC */
#define	PCI_PRODUCT_INTEL_JSL_LPSS_SPI_2	0x4dfb		/* Jasper Lake SPI */
#define	PCI_PRODUCT_INTEL_JSL_DPTF	0x4e03		/* Jasper Lake DPTF */
#define	PCI_PRODUCT_INTEL_JSL_GNA	0x4e11		/* Jasper Lake GNA */
#define	PCI_PRODUCT_INTEL_JSL_HB_1	0x4e12		/* Jasper Lake Host */
#define	PCI_PRODUCT_INTEL_JSL_HB_2	0x4e14		/* Jasper Lake Host */
#define	PCI_PRODUCT_INTEL_JSL_IPU	0x4e19		/* Jasper Lake IPU */
#define	PCI_PRODUCT_INTEL_JSL_HB_3	0x4e22		/* Jasper Lake Host */
#define	PCI_PRODUCT_INTEL_JSL_HB_4	0x4e24		/* Jasper Lake Host */
#define	PCI_PRODUCT_INTEL_JSL_HB_5	0x4e26		/* Jasper Lake Host */
#define	PCI_PRODUCT_INTEL_JSL_HB_6	0x4e28		/* Jasper Lake Host */
#define	PCI_PRODUCT_INTEL_JSL_TH	0x4e29		/* Jasper Lake TH */
#define	PCI_PRODUCT_INTEL_JSL_GT_1	0x4e51		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_JSL_GT_2	0x4e55		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_JSL_GT_3	0x4e57		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_JSL_GT_4	0x4e61		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_JSL_GT_5	0x4e71		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_EP80579_HB	0x5020		/* EP80579 Host */
#define	PCI_PRODUCT_INTEL_EP80579_MEM	0x5021		/* EP80579 Memory */
#define	PCI_PRODUCT_INTEL_EP80579_EDMA	0x5023		/* EP80579 EDMA */
#define	PCI_PRODUCT_INTEL_EP80579_PCIE_1	0x5024		/* EP80579 PCIE */
#define	PCI_PRODUCT_INTEL_EP80579_PCIE_2	0x5025		/* EP80579 PCIE */
#define	PCI_PRODUCT_INTEL_EP80579_SATA	0x5028		/* EP80579 SATA */
#define	PCI_PRODUCT_INTEL_EP80579_AHCI	0x5029		/* EP80579 AHCI */
#define	PCI_PRODUCT_INTEL_EP80579_ASU	0x502c		/* EP80579 ASU */
#define	PCI_PRODUCT_INTEL_EP80579_RESERVED1	0x5030		/* EP80579 Reserved */
#define	PCI_PRODUCT_INTEL_EP80579_LPC	0x5031		/* EP80579 LPC */
#define	PCI_PRODUCT_INTEL_EP80579_SMBUS	0x5032		/* EP80579 SMBus */
#define	PCI_PRODUCT_INTEL_EP80579_UHCI	0x5033		/* EP80579 USB */
#define	PCI_PRODUCT_INTEL_EP80579_EHCI	0x5035		/* EP80579 USB */
#define	PCI_PRODUCT_INTEL_EP80579_PPB	0x5037		/* EP80579 */
#define	PCI_PRODUCT_INTEL_EP80579_CAN_1	0x5039		/* EP80579 CANbus */
#define	PCI_PRODUCT_INTEL_EP80579_CAN_2	0x503a		/* EP80579 CANbus */
#define	PCI_PRODUCT_INTEL_EP80579_SERIAL	0x503b		/* EP80579 Serial */
#define	PCI_PRODUCT_INTEL_EP80579_1588	0x503c		/* EP80579 1588 */
#define	PCI_PRODUCT_INTEL_EP80579_LEB	0x503d		/* EP80579 LEB */
#define	PCI_PRODUCT_INTEL_EP80579_GCU	0x503e		/* EP80579 GCU */
#define	PCI_PRODUCT_INTEL_EP80579_RESERVED2	0x503f		/* EP80579 Reserved */
#define	PCI_PRODUCT_INTEL_EP80579_LAN_1	0x5040		/* EP80579 LAN */
#define	PCI_PRODUCT_INTEL_EP80579_LAN_4	0x5041		/* EP80579 LAN */
#define	PCI_PRODUCT_INTEL_EP80579_LAN_2	0x5044		/* EP80579 LAN */
#define	PCI_PRODUCT_INTEL_EP80579_LAN_5	0x5045		/* EP80579 LAN */
#define	PCI_PRODUCT_INTEL_EP80579_LAN_3	0x5048		/* EP80579 LAN */
#define	PCI_PRODUCT_INTEL_EP80579_LAN_6	0x5049		/* EP80579 LAN */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_ESPI	0x5182		/* 600 Series eSPI */
#define	PCI_PRODUCT_INTEL_700SERIES_LP_ESPI	0x519d		/* 700 Series eSPI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_P2SB	0x51a0		/* 600 Series P2SB */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PMC	0x51a1		/* 600 Series PMC */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_SMB	0x51a3		/* 600 Series SMBus */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_SPI	0x51a4		/* 600 Series SPI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_TH	0x51a6		/* 600 Series TH */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_UART_0	0x51a8		/* 600 Series UART */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_UART_1	0x51a9		/* 600 Series UART */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_GSPI_0	0x51aa		/* 600 Series GSPI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_GSPI_1	0x51ab		/* 600 Series GSPI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_9	0x51b0		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_10	0x51b1		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_11	0x51b2		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_12	0x51b3		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_1	0x51b8		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_2	0x51b9		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_3	0x51ba		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_4	0x51bb		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_5	0x51bc		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_6	0x51bd		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_7	0x51be		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_PCIE_8	0x51bf		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_I2C_0	0x51c5		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_I2C_1	0x51c6		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_UART_2	0x51c7		/* 600 Series UART */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_HDA	0x51c8		/* 600 Series HD Audio */
#define	PCI_PRODUCT_INTEL_700SERIES_LP_HDA	0x51ca		/* 700 Series HD Audio */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_THC_0	0x51d0		/* 600 Series THC */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_THC_1	0x51d1		/* 600 Series THC */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_AHCI	0x51d3		/* 600 Series AHCI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_RAID	0x51d7		/* 600 Series RAID */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_I2C_2	0x51d8		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_I2C_3	0x51d9		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_UART_3	0x51da		/* 600 Series UART */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_HECI	0x51e0		/* 600 Series HECI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_HECI_2	0x51e1		/* 600 Series HECI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_IDER	0x51e2		/* 600 Series IDE-R */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_KT	0x51e3		/* 600 Series KT */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_HECI_3	0x51e4		/* 600 Series HECI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_HECI_4	0x51e5		/* 600 Series HECI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_I2C_4	0x51e8		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_I2C_5	0x51e9		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_I2C_6	0x51ea		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_I2C_7	0x51eb		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_XHCI	0x51ed		/* 600 Series xHCI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_XDCI	0x51ee		/* 600 Series xDCI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_SRAM	0x51ef		/* 600 Series SRAM */
#define	PCI_PRODUCT_INTEL_WL_22500_11	0x51f0		/* Wi-Fi 6 AX211 */
#define	PCI_PRODUCT_INTEL_WL_22500_17	0x51f1		/* Wi-Fi 6 AX211 */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_GSPI_2	0x51fb		/* 600 Series GSPI */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_ISH	0x51fc		/* 600 Series ISH */
#define	PCI_PRODUCT_INTEL_600SERIES_LP_UFS	0x51ff		/* 600 Series UFS */
#define	PCI_PRODUCT_INTEL_80960RD	0x5200		/* i960 RD */
#define	PCI_PRODUCT_INTEL_PRO_100_SERVER	0x5201		/* PRO 100 Server */
#define	PCI_PRODUCT_INTEL_ADL_N_ESPI	0x5481		/* ADL-N eSPI */
#define	PCI_PRODUCT_INTEL_ADL_N_P2SB	0x54a0		/* ADL-N P2SB */
#define	PCI_PRODUCT_INTEL_ADL_N_PMC	0x54a1		/* ADL-N PMC */
#define	PCI_PRODUCT_INTEL_ADL_N_SMB	0x54a3		/* ADL-N SMBus */
#define	PCI_PRODUCT_INTEL_ADL_N_SPI	0x54a4		/* ADL-N SPI */
#define	PCI_PRODUCT_INTEL_ADL_N_TH	0x54a6		/* ADL-N TH */
#define	PCI_PRODUCT_INTEL_ADL_N_UART_0	0x54a8		/* ADL-N UART */
#define	PCI_PRODUCT_INTEL_ADL_N_UART_1	0x54a9		/* ADL-N UART */
#define	PCI_PRODUCT_INTEL_ADL_N_GSPI_0	0x54aa		/* ADL-N GSPI */
#define	PCI_PRODUCT_INTEL_ADL_N_GSPI_1	0x54ab		/* ADL-N GSPI */
#define	PCI_PRODUCT_INTEL_ADL_N_PCIE_9	0x54b0		/* ADL-N PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_PCIE_10	0x54b1		/* ADL-N PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_PCIE_11	0x54b2		/* ADL-N PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_PCIE_12	0x54b3		/* ADL-N PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_PCIE_1	0x54b8		/* ADL-N PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_PCIE_2	0x54b9		/* ADL-N PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_PCIE_3	0x54ba		/* ADL-N PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_PCIE_4	0x54bb		/* ADL-N PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_PCIE_7	0x54be		/* ADL-N PCIE */
#define	PCI_PRODUCT_INTEL_ADL_N_EMMC	0x54c4		/* ADL-N eMMC */
#define	PCI_PRODUCT_INTEL_ADL_N_I2C_4	0x54c5		/* ADL-N I2C */
#define	PCI_PRODUCT_INTEL_ADL_N_I2C_5	0x54c6		/* ADL-N I2C */
#define	PCI_PRODUCT_INTEL_ADL_N_UART_2	0x54c7		/* ADL-N UART */
#define	PCI_PRODUCT_INTEL_ADL_N_HDA	0x54c8		/* ADL-N HD Audio */
#define	PCI_PRODUCT_INTEL_ADL_N_THC_0	0x54d0		/* ADL-N THC */
#define	PCI_PRODUCT_INTEL_ADL_N_THC_1	0x54d1		/* ADL-N THC */
#define	PCI_PRODUCT_INTEL_ADL_N_AHCI	0x54d3		/* ADL-N AHCI */
#define	PCI_PRODUCT_INTEL_ADL_N_I2C_6	0x54d8		/* ADL-N I2C */
#define	PCI_PRODUCT_INTEL_ADL_N_I2C_7	0x54d9		/* ADL-N I2C */
#define	PCI_PRODUCT_INTEL_ADL_N_UART_3	0x54da		/* ADL-N UART */
#define	PCI_PRODUCT_INTEL_ADL_N_HECI_1	0x54e0		/* ADL-N HECI */
#define	PCI_PRODUCT_INTEL_ADL_N_HECI_2	0x54e1		/* ADL-N HECI */
#define	PCI_PRODUCT_INTEL_ADL_N_HECI_3	0x54e4		/* ADL-N HECI */
#define	PCI_PRODUCT_INTEL_ADL_N_HECI_4	0x54e5		/* ADL-N HECI */
#define	PCI_PRODUCT_INTEL_ADL_N_I2C_0	0x54e8		/* ADL-N I2C */
#define	PCI_PRODUCT_INTEL_ADL_N_I2C_1	0x54e9		/* ADL-N I2C */
#define	PCI_PRODUCT_INTEL_ADL_N_I2C_2	0x54ea		/* ADL-N I2C */
#define	PCI_PRODUCT_INTEL_ADL_N_I2C_3	0x54eb		/* ADL-N I2C */
#define	PCI_PRODUCT_INTEL_ADL_N_XHCI	0x54ed		/* ADL-N xHCI */
#define	PCI_PRODUCT_INTEL_ADL_N_XDCI	0x54ee		/* ADL-N xDCI */
#define	PCI_PRODUCT_INTEL_ADL_N_SRAM	0x54ef		/* ADL-N SRAM */
#define	PCI_PRODUCT_INTEL_WL_22500_16	0x54f0		/* Wi-Fi 6 AX211 */
#define	PCI_PRODUCT_INTEL_ADL_N_GSPI_2	0x54fb		/* ADL-N GSPI */
#define	PCI_PRODUCT_INTEL_ADL_N_ISH	0x54fc		/* ADL-N ISH */
#define	PCI_PRODUCT_INTEL_ADL_N_UFS	0x54ff		/* ADL-N UFS */
#define	PCI_PRODUCT_INTEL_I225_LMVP	0x5502		/* I225-LMvP */
#define	PCI_PRODUCT_INTEL_I226_K	0x5504		/* I226-K */
#define	PCI_PRODUCT_INTEL_I219_LM18	0x550a		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V18	0x550b		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM19	0x550c		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V19	0x550d		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM20	0x550e		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V20	0x550f		/* I219-V */
#define	PCI_PRODUCT_INTEL_I219_LM21	0x5510		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V21	0x5511		/* I219-V */
#define	PCI_PRODUCT_INTEL_DG2_G10_1	0x5690		/* Arc A770M */
#define	PCI_PRODUCT_INTEL_DG2_G10_2	0x5691		/* Arc A730M */
#define	PCI_PRODUCT_INTEL_DG2_G10_3	0x5692		/* Arc A550M */
#define	PCI_PRODUCT_INTEL_DG2_G11_1	0x5693		/* Arc A370M */
#define	PCI_PRODUCT_INTEL_DG2_G11_2	0x5694		/* Arc A350M */
#define	PCI_PRODUCT_INTEL_DG2_G11_3	0x5695		/* Graphics */
#define	PCI_PRODUCT_INTEL_DG2_G12_1	0x5696		/* Arc A570M */
#define	PCI_PRODUCT_INTEL_DG2_G12_2	0x5697		/* Arc A530M */
#define	PCI_PRODUCT_INTEL_DG2_G10_4	0x56a0		/* Arc A770 */
#define	PCI_PRODUCT_INTEL_DG2_G10_5	0x56a1		/* Arc A750 */
#define	PCI_PRODUCT_INTEL_DG2_G10_6	0x56a2		/* Arc A580 */
#define	PCI_PRODUCT_INTEL_DG2_G12_3	0x56a3		/* Graphics */
#define	PCI_PRODUCT_INTEL_DG2_G12_4	0x56a4		/* Graphics */
#define	PCI_PRODUCT_INTEL_DG2_G11_4	0x56a5		/* Arc A380 */
#define	PCI_PRODUCT_INTEL_DG2_G11_5	0x56a6		/* Arc A310 */
#define	PCI_PRODUCT_INTEL_DG2_G11_6	0x56b0		/* Arc Pro A30M */
#define	PCI_PRODUCT_INTEL_DG2_G11_7	0x56b1		/* Arc Pro A40/A50 */
#define	PCI_PRODUCT_INTEL_DG2_G12_5	0x56b2		/* Arc Pro A60M */
#define	PCI_PRODUCT_INTEL_DG2_G12_6	0x56b3		/* Arc Pro A60 */
#define	PCI_PRODUCT_INTEL_DG2_G11_8	0x56ba		/* Arc A380E */
#define	PCI_PRODUCT_INTEL_DG2_G11_9	0x56bb		/* Arc A310E */
#define	PCI_PRODUCT_INTEL_DG2_G11_10	0x56bc		/* Arc A370E */
#define	PCI_PRODUCT_INTEL_DG2_G11_11	0x56bd		/* Arc A350E */
#define	PCI_PRODUCT_INTEL_DG2_G10_7	0x56be		/* Arc A750E */
#define	PCI_PRODUCT_INTEL_DG2_G10_8	0x56bf		/* Arc A580E */
#define	PCI_PRODUCT_INTEL_ATS_M150	0x56c0		/* Flex 170 */
#define	PCI_PRODUCT_INTEL_ATS_M75	0x56c1		/* Flex 140 */
#define	PCI_PRODUCT_INTEL_I219_LM24	0x57a0		/* I219-LM */
#define	PCI_PRODUCT_INTEL_I219_V24	0x57a1		/* I219-V */
#define	PCI_PRODUCT_INTEL_QEMU_NVME	0x5845		/* QEMU NVM Express Controller */
#define	PCI_PRODUCT_INTEL_KBL_D_GT1	0x5902		/* HD Graphics 610 */
#define	PCI_PRODUCT_INTEL_CORE7G_U_HB	0x5904		/* Core 7G Host */
#define	PCI_PRODUCT_INTEL_KBL_U_GT1	0x5906		/* HD Graphics 610 */
#define	PCI_PRODUCT_INTEL_KBL_H_GT1_1	0x5908		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_KBL_S_GT1	0x590a		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_KBL_H_GT1_2	0x590b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE7G_Y_HB	0x590c		/* Core 7G Host */
#define	PCI_PRODUCT_INTEL_KBL_Y_GT1	0x590e		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE7G_S_HB_2C	0x590f		/* Core 7G Host */
#define	PCI_PRODUCT_INTEL_XEONE3_1200V6_HB2	0x5910		/* Xeon E3-1200 v6/7 Host */
#define	PCI_PRODUCT_INTEL_CORE_GMM_2	0x5911		/* Core GMM */
#define	PCI_PRODUCT_INTEL_KBL_S_GT2	0x5912		/* HD Graphics 630 */
#define	PCI_PRODUCT_INTEL_KBL_U_GT15	0x5913		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_CORE8G_U_HB	0x5914		/* Core 8G Host */
#define	PCI_PRODUCT_INTEL_KBL_U_GT15_2	0x5915		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_KBL_U_GT2_1	0x5916		/* HD Graphics 620 */
#define	PCI_PRODUCT_INTEL_KBL_U_GT2	0x5917		/* UHD Graphics 620 */
#define	PCI_PRODUCT_INTEL_XEONE3_1200V6_HB	0x5918		/* Xeon E3-1200 v6/7 Host */
#define	PCI_PRODUCT_INTEL_KBL_SRV_GT2	0x591a		/* HD Graphics P630 */
#define	PCI_PRODUCT_INTEL_KBL_H_GT2	0x591b		/* HD Graphics 630 */
#define	PCI_PRODUCT_INTEL_AML_KBL_Y_GT2	0x591c		/* UHD Graphics 615 */
#define	PCI_PRODUCT_INTEL_KBL_U_GT2_2	0x591d		/* HD Graphics P630 */
#define	PCI_PRODUCT_INTEL_KBL_Y_GT2	0x591e		/* HD Graphics 615 */
#define	PCI_PRODUCT_INTEL_CORE7G_S_HB_4C	0x591f		/* Core 7G Host */
#define	PCI_PRODUCT_INTEL_KBL_U_GT2F	0x5921		/* HD Graphics 620 */
#define	PCI_PRODUCT_INTEL_KBL_U_GT3	0x5923		/* HD Graphics 635 */
#define	PCI_PRODUCT_INTEL_KBL_U_GT3_15W	0x5926		/* Iris Plus 640 */
#define	PCI_PRODUCT_INTEL_KBL_U_GT3_28W	0x5927		/* Iris Plus 650 */
#define	PCI_PRODUCT_INTEL_KBL_H_GT4	0x593b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_IGD_1	0x5a84		/* HD Graphics 505 */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_IGD_2	0x5a85		/* HD Graphics 500 */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_HDA	0x5a98		/* Apollo Lake HD Audio */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_TXE	0x5a9a		/* Apollo Lake TXE */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_XHCI	0x5aa8		/* Apollo Lake xHCI */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_XDCI	0x5aaa		/* Apollo Lake xDCI */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_1	0x5aac		/* Apollo Lake I2C */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_2	0x5aae		/* Apollo Lake I2C */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_3	0x5ab0		/* Apollo Lake I2C */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_4	0x5ab2		/* Apollo Lake I2C */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_5	0x5ab4		/* Apollo Lake I2C */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_6	0x5ab6		/* Apollo Lake I2C */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_7	0x5ab8		/* Apollo Lake I2C */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_I2C_8	0x5aba		/* Apollo Lake I2C */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_UART_1	0x5abc		/* Apollo Lake HSUART */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_UART_2	0x5abe		/* Apollo Lake HSUART */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_UART_3	0x5ac0		/* Apollo Lake HSUART */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_SPI_1	0x5ac2		/* Apollo Lake SPI */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_SPI_2	0x5ac4		/* Apollo Lake SPI */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_SPI_3	0x5ac6		/* Apollo Lake SPI */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_PWM	0x5ac8		/* Apollo Lake PWM */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_SDMMC	0x5aca		/* Apollo Lake SD/MMC */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_EMMC	0x5acc		/* Apollo Lake eMMC */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_SDIO	0x5ad0		/* Apollo Lake SDIO */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_SMB	0x5ad4		/* Apollo Lake SMBus */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_PCIE_4	0x5ad6		/* Apollo Lake PCIE */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_PCIE_5	0x5ad7		/* Apollo Lake PCIE */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_PCIE_1	0x5ad8		/* Apollo Lake PCIE */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_PCIE_2	0x5ad9		/* Apollo Lake PCIE */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_PCIE_3	0x5ada		/* Apollo Lake PCIE */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_PCIE_6	0x5adb		/* Apollo Lake PCIE */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_AHCI	0x5ae3		/* Apollo Lake AHCI */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_LPC	0x5ae8		/* Apollo Lake LPC */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_UART_4	0x5aee		/* Apollo Lake HSUART */
#define	PCI_PRODUCT_INTEL_APOLLOLAKE_HB	0x5af0		/* Apollo Lake Host */
#define	PCI_PRODUCT_INTEL_LNL_HB	0x6400		/* Core Ultra Host */
#define	PCI_PRODUCT_INTEL_LNL_DTT	0x641d		/* Core Ultra DTT */
#define	PCI_PRODUCT_INTEL_LNL_GT_1	0x6420		/* Graphics */
#define	PCI_PRODUCT_INTEL_LNL_NPU	0x643e		/* Core Ultra NPU */
#define	PCI_PRODUCT_INTEL_LNL_IPU	0x645d		/* Core Ultra IPU */
#define	PCI_PRODUCT_INTEL_LNL_CT	0x647d		/* Core Ultra CT */
#define	PCI_PRODUCT_INTEL_LNL_GT_2	0x64a0		/* Graphics */
#define	PCI_PRODUCT_INTEL_LNL_GT_3	0x64b0		/* Graphics */
#define	PCI_PRODUCT_INTEL_5100_HB	0x65c0		/* 5100 Host */
#define	PCI_PRODUCT_INTEL_5100_PCIE_2	0x65e2		/* 5100 PCIE */
#define	PCI_PRODUCT_INTEL_5100_PCIE_3	0x65e3		/* 5100 PCIE */
#define	PCI_PRODUCT_INTEL_5100_PCIE_4	0x65e4		/* 5100 PCIE */
#define	PCI_PRODUCT_INTEL_5100_PCIE_5	0x65e5		/* 5100 PCIE */
#define	PCI_PRODUCT_INTEL_5100_PCIE_6	0x65e6		/* 5100 PCIE */
#define	PCI_PRODUCT_INTEL_5100_PCIE_7	0x65e7		/* 5100 PCIE */
#define	PCI_PRODUCT_INTEL_5100_FSB	0x65f0		/* 5100 FSB */
#define	PCI_PRODUCT_INTEL_5100_RESERVED_1	0x65f1		/* 5100 Reserved */
#define	PCI_PRODUCT_INTEL_5100_RESERVED_2	0x65f3		/* 5100 Reserved */
#define	PCI_PRODUCT_INTEL_5100_DDR	0x65f5		/* 5100 DDR */
#define	PCI_PRODUCT_INTEL_5100_DDR2	0x65f6		/* 5100 DDR */
#define	PCI_PRODUCT_INTEL_5100_PCIE_23	0x65f7		/* 5100 PCIE */
#define	PCI_PRODUCT_INTEL_5100_PCIE_45	0x65f8		/* 5100 PCIE */
#define	PCI_PRODUCT_INTEL_5100_PCIE_67	0x65f9		/* 5100 PCIE */
#define	PCI_PRODUCT_INTEL_5100_PCIE_47	0x65fa		/* 5100 PCIE */
#define	PCI_PRODUCT_INTEL_IOAT_SCNB	0x65ff		/* I/OAT SCNB */
#define	PCI_PRODUCT_INTEL_XEOND_HB	0x6f00		/* Xeon-D Host */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_1	0x6f02		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_2	0x6f03		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_3	0x6f04		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_4	0x6f05		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_5	0x6f06		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_6	0x6f07		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_7	0x6f08		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_8	0x6f09		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_9	0x6f0a		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_10	0x6f0b		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_PCIE_11	0x6f1d		/* Xeon-D PCIE */
#define	PCI_PRODUCT_INTEL_XEOND_UBOX_0	0x6f1e		/* Xeon-D Ubox */
#define	PCI_PRODUCT_INTEL_XEOND_UBOX_1	0x6f1f		/* Xeon-D Ubox */
#define	PCI_PRODUCT_INTEL_E5V4_DMA_0	0x6f20		/* E5 v4 DMA */
#define	PCI_PRODUCT_INTEL_E5V4_DMA_1	0x6f21		/* E5 v4 DMA */
#define	PCI_PRODUCT_INTEL_E5V4_DMA_2	0x6f22		/* E5 v4 DMA */
#define	PCI_PRODUCT_INTEL_E5V4_DMA_3	0x6f23		/* E5 v4 DMA */
#define	PCI_PRODUCT_INTEL_E5V4_DMA_4	0x6f24		/* E5 v4 DMA */
#define	PCI_PRODUCT_INTEL_E5V4_DMA_5	0x6f25		/* E5 v4 DMA */
#define	PCI_PRODUCT_INTEL_E5V4_DMA_6	0x6f26		/* E5 v4 DMA */
#define	PCI_PRODUCT_INTEL_E5V4_DMA_7	0x6f27		/* E5 v4 DMA */
#define	PCI_PRODUCT_INTEL_XEOND_ADDRMAP	0x6f28		/* Xeon-D Address Map */
#define	PCI_PRODUCT_INTEL_XEOND_HOTPLUG	0x6f29		/* Xeon-D Hot Plug */
#define	PCI_PRODUCT_INTEL_XEOND_RAS	0x6f2a		/* Xeon-D RAS */
#define	PCI_PRODUCT_INTEL_XEOND_IOAPIC	0x6f2b		/* Xeon-D I/O APIC */
#define	PCI_PRODUCT_INTEL_XEOND_IOAPIC_2	0x6f2c		/* Xeon-D I/O APIC */
#define	PCI_PRODUCT_INTEL_XEOND_HA_0	0x6f30		/* Xeon-D Home Agent */
#define	PCI_PRODUCT_INTEL_E5V4_R2PCIE	0x6f34		/* E5 v4 R2PCIe Agent */
#define	PCI_PRODUCT_INTEL_XEOND_QPI_R3_0	0x6f36		/* Xeon-D QPI Link */
#define	PCI_PRODUCT_INTEL_XEOND_QPI_R3_1	0x6f37		/* Xeon-D QPI Link */
#define	PCI_PRODUCT_INTEL_XEOND_QD_1	0x6f50		/* Xeon-D QuickData */
#define	PCI_PRODUCT_INTEL_XEOND_QD_2	0x6f51		/* Xeon-D QuickData */
#define	PCI_PRODUCT_INTEL_XEOND_QD_3	0x6f52		/* Xeon-D QuickData */
#define	PCI_PRODUCT_INTEL_XEOND_QD_4	0x6f53		/* Xeon-D QuickData */
#define	PCI_PRODUCT_INTEL_E5V4_RAS	0x6f68		/* E5 v4 RAS */
#define	PCI_PRODUCT_INTEL_E5V4_DDRIO_1	0x6f6e		/* E5 v4 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V4_DDRIO_2	0x6f6f		/* E5 v4 DDRIO */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_0	0x6f71		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_QPI_R3_2	0x6f76		/* Xeon-D QPI Debug */
#define	PCI_PRODUCT_INTEL_XEOND_UBOX_2	0x6f7d		/* Xeon-D Ubox */
#define	PCI_PRODUCT_INTEL_XEOND_QPI_R3_3	0x6f81		/* Xeon-D QPI Link */
#define	PCI_PRODUCT_INTEL_XEOND_PCU_0	0x6f88		/* Xeon-D PCU */
#define	PCI_PRODUCT_INTEL_XEOND_PCU_1	0x6f8a		/* Xeon-D PCU */
#define	PCI_PRODUCT_INTEL_XEOND_PCU_2	0x6f98		/* Xeon-D PCU */
#define	PCI_PRODUCT_INTEL_XEOND_PCU_3	0x6f99		/* Xeon-D PCU */
#define	PCI_PRODUCT_INTEL_XEOND_PCU_4	0x6f9a		/* Xeon-D PCU */
#define	PCI_PRODUCT_INTEL_XEOND_PCU_5	0x6f9c		/* Xeon-D PCU */
#define	PCI_PRODUCT_INTEL_XEOND_HA_1	0x6fa0		/* Xeon-D Home Agent */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_1	0x6fa8		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_2	0x6faa		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_3	0x6fab		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_4	0x6fac		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_5	0x6fad		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_6	0x6fae		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_7	0x6faf		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_8	0x6fb0		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_9	0x6fb1		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_10	0x6fb2		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_11	0x6fb3		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_12	0x6fb4		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_13	0x6fb5		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_14	0x6fb6		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_15	0x6fb7		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_E5V4_DDRIO_3	0x6fb8		/* E5 v4 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V4_DDRIO_4	0x6fb9		/* E5 v4 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V4_DDRIO_5	0x6fba		/* E5 v4 DDRIO */
#define	PCI_PRODUCT_INTEL_E5V4_DDRIO_6	0x6fbb		/* E5 v4 DDRIO */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_16	0x6fbc		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_17	0x6fbd		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_18	0x6fbe		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_MEM_19	0x6fbf		/* Xeon-D Memory */
#define	PCI_PRODUCT_INTEL_XEOND_PCU_6	0x6fc0		/* Xeon-D PCU */
#define	PCI_PRODUCT_INTEL_E5V4_THERMAL_1	0x6fd0		/* E5 v4 Thermal */
#define	PCI_PRODUCT_INTEL_XEOND_CACHE_0	0x6fe0		/* Xeon-D Cache */
#define	PCI_PRODUCT_INTEL_XEOND_CACHE_1	0x6fe1		/* Xeon-D Cache */
#define	PCI_PRODUCT_INTEL_XEOND_CACHE_2	0x6fe2		/* Xeon-D Cache */
#define	PCI_PRODUCT_INTEL_XEOND_CACHE_3	0x6fe3		/* Xeon-D Cache */
#define	PCI_PRODUCT_INTEL_E5V4_CACHE_0	0x6fe4		/* E5 v4 Cache */
#define	PCI_PRODUCT_INTEL_E5V4_CACHE_1	0x6fe5		/* E5 v4 Cache */
#define	PCI_PRODUCT_INTEL_XEOND_CACHE_4	0x6ff8		/* Xeon-D Cache */
#define	PCI_PRODUCT_INTEL_E5V4_CACHE_2	0x6ff9		/* E5 v4 Cache */
#define	PCI_PRODUCT_INTEL_XEOND_CACHE_5	0x6ffc		/* Xeon-D Cache */
#define	PCI_PRODUCT_INTEL_XEOND_CACHE_6	0x6ffd		/* Xeon-D Cache */
#define	PCI_PRODUCT_INTEL_XEOND_CACHE_7	0x6ffe		/* Xeon-D Cache */
#define	PCI_PRODUCT_INTEL_82371SB_ISA	0x7000		/* 82371SB ISA */
#define	PCI_PRODUCT_INTEL_82371SB_IDE	0x7010		/* 82371SB IDE */
#define	PCI_PRODUCT_INTEL_82371USB	0x7020		/* 82371SB USB */
#define	PCI_PRODUCT_INTEL_82437VX	0x7030		/* 82437VX */
#define	PCI_PRODUCT_INTEL_82439TX	0x7100		/* 82439TX */
#define	PCI_PRODUCT_INTEL_82371AB_ISA	0x7110		/* 82371AB PIIX4 ISA */
#define	PCI_PRODUCT_INTEL_82371AB_IDE	0x7111		/* 82371AB IDE */
#define	PCI_PRODUCT_INTEL_82371AB_USB	0x7112		/* 82371AB USB */
#define	PCI_PRODUCT_INTEL_82371AB_PM	0x7113		/* 82371AB Power */
#define	PCI_PRODUCT_INTEL_82810_HB	0x7120		/* 82810 Host */
#define	PCI_PRODUCT_INTEL_82810_IGD	0x7121		/* 82810 Video */
#define	PCI_PRODUCT_INTEL_82810_DC100_HB	0x7122		/* 82810-DC100 Host */
#define	PCI_PRODUCT_INTEL_82810_DC100_IGD	0x7123		/* 82810-DC100 Video */
#define	PCI_PRODUCT_INTEL_82810E_HB	0x7124		/* 82810E Host */
#define	PCI_PRODUCT_INTEL_82810E_IGD	0x7125		/* 82810E Video */
#define	PCI_PRODUCT_INTEL_82443LX	0x7180		/* 82443LX AGP */
#define	PCI_PRODUCT_INTEL_82443LX_AGP	0x7181		/* 82443LX AGP */
#define	PCI_PRODUCT_INTEL_82443BX	0x7190		/* 82443BX AGP */
#define	PCI_PRODUCT_INTEL_82443BX_AGP	0x7191		/* 82443BX AGP */
#define	PCI_PRODUCT_INTEL_82443BX_NOAGP	0x7192		/* 82443BX */
#define	PCI_PRODUCT_INTEL_82440MX_HB	0x7194		/* 82440MX Host */
#define	PCI_PRODUCT_INTEL_82440MX_ACA	0x7195		/* 82440MX AC97 */
#define	PCI_PRODUCT_INTEL_82440MX_ACM	0x7196		/* 82440MX Modem */
#define	PCI_PRODUCT_INTEL_82440MX_ISA	0x7198		/* 82440MX ISA */
#define	PCI_PRODUCT_INTEL_82440MX_IDE	0x7199		/* 82440MX IDE */
#define	PCI_PRODUCT_INTEL_82440MX_USB	0x719a		/* 82440MX USB */
#define	PCI_PRODUCT_INTEL_82440MX_PM	0x719b		/* 82440MX Power */
#define	PCI_PRODUCT_INTEL_82440BX	0x71a0		/* 82440BX AGP */
#define	PCI_PRODUCT_INTEL_82440BX_AGP	0x71a1		/* 82440BX AGP */
#define	PCI_PRODUCT_INTEL_82443GX	0x71a2		/* 82443GX */
#define	PCI_PRODUCT_INTEL_82372FB_IDE	0x7601		/* 82372FB IDE */
#define	PCI_PRODUCT_INTEL_ARL_H_ESPI	0x7702		/* Core Ultra eSPI */
#define	PCI_PRODUCT_INTEL_ARL_U_ESPI	0x7703		/* Core Ultra eSPI */
#define	PCI_PRODUCT_INTEL_ARL_U_P2SB_SOC	0x7720		/* Core Ultra P2SB */
#define	PCI_PRODUCT_INTEL_ARL_U_PMC_SOC	0x7721		/* Core Ultra PMC */
#define	PCI_PRODUCT_INTEL_ARL_U_SMB	0x7722		/* Core Ultra SMBus */
#define	PCI_PRODUCT_INTEL_ARL_U_SPI	0x7723		/* Core Ultra SPI */
#define	PCI_PRODUCT_INTEL_ARL_U_TH	0x7724		/* Core Ultra TH */
#define	PCI_PRODUCT_INTEL_ARL_U_UART_0	0x7725		/* Core Ultra UART */
#define	PCI_PRODUCT_INTEL_ARL_U_UART_1	0x7726		/* Core Ultra UART */
#define	PCI_PRODUCT_INTEL_ARL_U_GSPI_0	0x7727		/* Core Ultra GSPI */
#define	PCI_PRODUCT_INTEL_ARL_U_HDA	0x7728		/* Core Ultra HD Audio */
#define	PCI_PRODUCT_INTEL_ARL_U_GSPI_1	0x7730		/* Core Ultra GSPI */
#define	PCI_PRODUCT_INTEL_ARL_U_PCIE_1	0x7738		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_ARL_U_PCIE_2	0x7739		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_ARL_U_PCIE_3	0x773a		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_ARL_U_PCIE_4	0x773b		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_ARL_U_PCIE_5	0x773c		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_ARL_U_PCIE_6	0x773d		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_ARL_U_PCIE_7	0x773e		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_ARL_U_PCIE_8	0x773f		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_ARL_U_ISH	0x7745		/* Core Ultra ISH */
#define	PCI_PRODUCT_INTEL_ARL_U_GSPI_2	0x7746		/* Core Ultra GSPI */
#define	PCI_PRODUCT_INTEL_ARL_U_THC_0_1	0x7748		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_ARL_U_THC_0_2	0x7749		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_ARL_U_THC_1_1	0x774a		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_ARL_U_THC_1_2	0x774b		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_ARL_U_GNA	0x774c		/* Core Ultra GNA */
#define	PCI_PRODUCT_INTEL_ARL_U_PCIE_9	0x774d		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_ARL_U_I2C_4	0x7750		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_ARL_U_I2C_5	0x7751		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_ARL_U_UART_2	0x7752		/* Core Ultra UART */
#define	PCI_PRODUCT_INTEL_ARL_U_HECI_1	0x7758		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_ARL_U_HECI_2	0x7759		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_ARL_U_HECI_3	0x775a		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_ARL_U_AHCI	0x7763		/* Core Ultra AHCI */
#define	PCI_PRODUCT_INTEL_ARL_U_RAID	0x7767		/* Core Ultra RAID */
#define	PCI_PRODUCT_INTEL_ARL_U_HECI_4	0x7770		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_ARL_U_HECI_5	0x7771		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_ARL_U_IDER	0x7772		/* Core Ultra IDE-R */
#define	PCI_PRODUCT_INTEL_ARL_U_KT	0x7773		/* Core Ultra KT */
#define	PCI_PRODUCT_INTEL_ARL_U_HECI_6	0x7774		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_ARL_U_HECI_7	0x7775		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_ARL_U_I2C_0	0x7778		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_ARL_U_I2C_1	0x7779		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_ARL_U_I2C_2	0x777a		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_ARL_U_I2C_3	0x777b		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_ARL_U_I3C	0x777c		/* Core Ultra I3C */
#define	PCI_PRODUCT_INTEL_ARL_U_XHCI	0x777d		/* Core Ultra xHCI */
#define	PCI_PRODUCT_INTEL_ARL_U_XDCI	0x777e		/* Core Ultra xDCI */
#define	PCI_PRODUCT_INTEL_ARL_U_SRAM	0x777f		/* Core Ultra SRAM */
#define	PCI_PRODUCT_INTEL_82740	0x7800		/* 82740 AGP */
#define	PCI_PRODUCT_INTEL_Z790_ESPI	0x7a04		/* Z790 eSPI */
#define	PCI_PRODUCT_INTEL_H770_ESPI	0x7a05		/* H770 eSPI */
#define	PCI_PRODUCT_INTEL_B760_ESPI	0x7a06		/* B760 eSPI */
#define	PCI_PRODUCT_INTEL_C266_ESPI	0x7a13		/* C266 eSPI */
#define	PCI_PRODUCT_INTEL_C262_ESPI	0x7a14		/* C262 eSPI */
#define	PCI_PRODUCT_INTEL_700SERIES_P2SB	0x7a20		/* 700 Series P2SB */
#define	PCI_PRODUCT_INTEL_700SERIES_PMC	0x7a21		/* 700 Series PMC */
#define	PCI_PRODUCT_INTEL_700SERIES_SMB	0x7a23		/* 700 Series SMBus */
#define	PCI_PRODUCT_INTEL_700SERIES_SPI	0x7a24		/* 700 Series SPI */
#define	PCI_PRODUCT_INTEL_700SERIES_TH	0x7a26		/* 700 Series TH */
#define	PCI_PRODUCT_INTEL_700SERIES_SRAM	0x7a27		/* 700 Series Shared SRAM */
#define	PCI_PRODUCT_INTEL_700SERIES_UART_0	0x7a28		/* 700 Series UART */
#define	PCI_PRODUCT_INTEL_700SERIES_UART_1	0x7a29		/* 700 Series UART */
#define	PCI_PRODUCT_INTEL_700SERIES_GSPI_0	0x7a2a		/* 700 Series GSPI */
#define	PCI_PRODUCT_INTEL_700SERIES_GSPI_1	0x7a2b		/* 700 Series GSPI */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_9	0x7a30		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_10	0x7a31		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_11	0x7a32		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_12	0x7a33		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_13	0x7a34		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_14	0x7a35		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_15	0x7a36		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_16	0x7a37		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_1	0x7a38		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_2	0x7a39		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_3	0x7a3a		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_4	0x7a3b		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_5	0x7a3c		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_6	0x7a3d		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_7	0x7a3e		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_8	0x7a3f		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_17	0x7a40		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_18	0x7a41		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_19	0x7a42		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_20	0x7a43		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_21	0x7a44		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_22	0x7a45		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_23	0x7a46		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_24	0x7a47		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_25	0x7a48		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_26	0x7a49		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_27	0x7a4a		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_PCIE_28	0x7a4b		/* 700 Series PCIE */
#define	PCI_PRODUCT_INTEL_700SERIES_I2C_0	0x7a4c		/* 700 Series I2C */
#define	PCI_PRODUCT_INTEL_700SERIES_I2C_1	0x7a4d		/* 700 Series I2C */
#define	PCI_PRODUCT_INTEL_700SERIES_I2C_2	0x7a4e		/* 700 Series I2C */
#define	PCI_PRODUCT_INTEL_700SERIES_I2C_3	0x7a4f		/* 700 Series I2C */
#define	PCI_PRODUCT_INTEL_700SERIES_HDA	0x7a50		/* 700 Series HD Audio */
#define	PCI_PRODUCT_INTEL_700SERIES_UART_3	0x7a5c		/* 700 Series UART */
#define	PCI_PRODUCT_INTEL_700SERIES_XHCI	0x7a60		/* 700 Series xHCI */
#define	PCI_PRODUCT_INTEL_700SERIES_XDCI	0x7a61		/* 700 Series xDCI */
#define	PCI_PRODUCT_INTEL_700SERIES_AHCI	0x7a62		/* 700 Series AHCI */
#define	PCI_PRODUCT_INTEL_700SERIES_HECI_1	0x7a68		/* 700 Series HECI */
#define	PCI_PRODUCT_INTEL_700SERIES_HECI_2	0x7a69		/* 700 Series HECI */
#define	PCI_PRODUCT_INTEL_700SERIES_IDER	0x7a6a		/* 700 Series IDE-R */
#define	PCI_PRODUCT_INTEL_700SERIES_KT	0x7a6b		/* 700 Series KT */
#define	PCI_PRODUCT_INTEL_700SERIES_HECI_3	0x7a6c		/* 700 Series HECI */
#define	PCI_PRODUCT_INTEL_700SERIES_HECI_4	0x7a6d		/* 700 Series HECI */
#define	PCI_PRODUCT_INTEL_WL_22500_12	0x7a70		/* Wi-Fi 6 AX211 */
#define	PCI_PRODUCT_INTEL_700SERIES_ISH	0x7a78		/* 700 Series ISH */
#define	PCI_PRODUCT_INTEL_700SERIES_GSPI_3	0x7a79		/* 700 Series GSPI */
#define	PCI_PRODUCT_INTEL_700SERIES_GSPI_2	0x7a7b		/* 700 Series GSPI */
#define	PCI_PRODUCT_INTEL_700SERIES_I2C_4	0x7a7c		/* 700 Series I2C */
#define	PCI_PRODUCT_INTEL_700SERIES_I2C_5	0x7a7d		/* 700 Series I2C */
#define	PCI_PRODUCT_INTEL_700SERIES_UART_2	0x7a7e		/* 700 Series UART */
#define	PCI_PRODUCT_INTEL_Z690_ESPI	0x7a84		/* Z690 eSPI */
#define	PCI_PRODUCT_INTEL_600SERIES_P2SB	0x7aa0		/* 600 Series P2SB */
#define	PCI_PRODUCT_INTEL_600SERIES_PMC	0x7aa1		/* 600 Series PMC */
#define	PCI_PRODUCT_INTEL_600SERIES_SMB	0x7aa3		/* 600 Series SMBus */
#define	PCI_PRODUCT_INTEL_600SERIES_SPI	0x7aa4		/* 600 Series SPI */
#define	PCI_PRODUCT_INTEL_600SERIES_TH	0x7aa6		/* 600 Series TH */
#define	PCI_PRODUCT_INTEL_600SERIES_SRAM	0x7aa7		/* 600 Series Shared SRAM */
#define	PCI_PRODUCT_INTEL_600SERIES_UART_0	0x7aa8		/* 600 Series UART */
#define	PCI_PRODUCT_INTEL_600SERIES_UART_1	0x7aa9		/* 600 Series UART */
#define	PCI_PRODUCT_INTEL_600SERIES_GSPI_0	0x7aaa		/* 600 Series GSPI */
#define	PCI_PRODUCT_INTEL_600SERIES_GSPI_1	0x7aab		/* 600 Series GSPI */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_9	0x7ab0		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_10	0x7ab1		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_11	0x7ab2		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_12	0x7ab3		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_13	0x7ab4		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_14	0x7ab5		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_15	0x7ab6		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_16	0x7ab7		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_1	0x7ab8		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_2	0x7ab9		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_3	0x7aba		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_4	0x7abb		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_5	0x7abc		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_6	0x7abd		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_7	0x7abe		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_8	0x7abf		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_17	0x7ac0		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_18	0x7ac1		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_19	0x7ac2		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_20	0x7ac3		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_21	0x7ac4		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_22	0x7ac5		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_23	0x7ac6		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_24	0x7ac7		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_25	0x7ac8		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_26	0x7ac9		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_27	0x7aca		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_PCIE_28	0x7acb		/* 600 Series PCIE */
#define	PCI_PRODUCT_INTEL_600SERIES_I2C_0	0x7acc		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_I2C_1	0x7acd		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_I2C_2	0x7ace		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_I2C_3	0x7acf		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_HDA	0x7ad0		/* 600 Series HD Audio */
#define	PCI_PRODUCT_INTEL_600SERIES_UART_3	0x7adc		/* 600 Series UART */
#define	PCI_PRODUCT_INTEL_600SERIES_XHCI	0x7ae0		/* 600 Series xHCI */
#define	PCI_PRODUCT_INTEL_600SERIES_XDCI	0x7ae1		/* 600 Series xDCI */
#define	PCI_PRODUCT_INTEL_600SERIES_AHCI	0x7ae2		/* 600 Series AHCI */
#define	PCI_PRODUCT_INTEL_600SERIES_HECI_1	0x7ae8		/* 600 Series HECI */
#define	PCI_PRODUCT_INTEL_600SERIES_HECI_2	0x7ae9		/* 600 Series HECI */
#define	PCI_PRODUCT_INTEL_600SERIES_IDER	0x7aea		/* 600 Series IDE-R */
#define	PCI_PRODUCT_INTEL_600SERIES_KT	0x7aeb		/* 600 Series KT */
#define	PCI_PRODUCT_INTEL_600SERIES_HECI_3	0x7aec		/* 600 Series HECI */
#define	PCI_PRODUCT_INTEL_600SERIES_HECI_4	0x7aed		/* 600 Series HECI */
#define	PCI_PRODUCT_INTEL_WL_22500_13	0x7af0		/* Wi-Fi 6 AX211 */
#define	PCI_PRODUCT_INTEL_600SERIES_ISH	0x7af8		/* 600 Series ISH */
#define	PCI_PRODUCT_INTEL_600SERIES_GSPI_3	0x7af9		/* 600 Series GSPI */
#define	PCI_PRODUCT_INTEL_600SERIES_GSPI_2	0x7afb		/* 600 Series GSPI */
#define	PCI_PRODUCT_INTEL_600SERIES_I2C_4	0x7afc		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_I2C_5	0x7afd		/* 600 Series I2C */
#define	PCI_PRODUCT_INTEL_600SERIES_UART_2	0x7afe		/* 600 Series UART */
#define	PCI_PRODUCT_INTEL_MTL_U4_HB	0x7d00		/* Core Ultra Host */
#define	PCI_PRODUCT_INTEL_MTL_H_HB_2	0x7d01		/* Core Ultra Host */
#define	PCI_PRODUCT_INTEL_MTL_U_HB_2	0x7d02		/* Core Ultra Host */
#define	PCI_PRODUCT_INTEL_MTL_DTT	0x7d03		/* Core Ultra DTT */
#define	PCI_PRODUCT_INTEL_ARL_H_HB	0x7d06		/* Core Ultra Host */
#define	PCI_PRODUCT_INTEL_MTL_VMD	0x7d0b		/* Core Ultra VMD */
#define	PCI_PRODUCT_INTEL_MTL_PMT	0x7d0d		/* Core Ultra PMT */
#define	PCI_PRODUCT_INTEL_MTL_H_HB_1	0x7d14		/* Core Ultra Host */
#define	PCI_PRODUCT_INTEL_MTL_U_HB_1	0x7d16		/* Core Ultra Host */
#define	PCI_PRODUCT_INTEL_MTL_IPU	0x7d19		/* Core Ultra IPU */
#define	PCI_PRODUCT_INTEL_MTL_NPU	0x7d1d		/* Core Ultra NPU */
#define	PCI_PRODUCT_INTEL_ARL_U_HB	0x7d30		/* Core Ultra Host */
#define	PCI_PRODUCT_INTEL_MTL_U4_GT_1	0x7d40		/* Graphics */
#define	PCI_PRODUCT_INTEL_ARL_U_GT_1	0x7d41		/* Graphics */
#define	PCI_PRODUCT_INTEL_MTL_U_GT_1	0x7d45		/* Graphics */
#define	PCI_PRODUCT_INTEL_ARL_H_GT_1	0x7d51		/* Graphics */
#define	PCI_PRODUCT_INTEL_MTL_H_GT_1	0x7d55		/* Arc Graphics */
#define	PCI_PRODUCT_INTEL_MTL_U_GT_2	0x7d60		/* Graphics */
#define	PCI_PRODUCT_INTEL_ARL_S_GT_1	0x7d67		/* Graphics */
#define	PCI_PRODUCT_INTEL_ARL_H_GT_2	0x7dd1		/* Graphics */
#define	PCI_PRODUCT_INTEL_MTL_H_GT_2	0x7dd5		/* Graphics */
#define	PCI_PRODUCT_INTEL_MTL_H_ESPI	0x7e02		/* Core Ultra eSPI */
#define	PCI_PRODUCT_INTEL_MTL_U_ESPI	0x7e03		/* Core Ultra eSPI */
#define	PCI_PRODUCT_INTEL_MTL_U4_ESPI	0x7e07		/* Core Ultra eSPI */
#define	PCI_PRODUCT_INTEL_MTL_P2SB_SOC	0x7e20		/* Core Ultra P2SB */
#define	PCI_PRODUCT_INTEL_MTL_PMC_SOC	0x7e21		/* Core Ultra PMC */
#define	PCI_PRODUCT_INTEL_MTL_SMB	0x7e22		/* Core Ultra SMBus */
#define	PCI_PRODUCT_INTEL_MTL_SPI	0x7e23		/* Core Ultra SPI */
#define	PCI_PRODUCT_INTEL_MTL_TH	0x7e24		/* Core Ultra TH */
#define	PCI_PRODUCT_INTEL_MTL_UART_0	0x7e25		/* Core Ultra UART */
#define	PCI_PRODUCT_INTEL_MTL_UART_1	0x7e26		/* Core Ultra UART */
#define	PCI_PRODUCT_INTEL_MTL_GSPI_0	0x7e27		/* Core Ultra GSPI */
#define	PCI_PRODUCT_INTEL_MTL_HDA	0x7e28		/* Core Ultra HD Audio */
#define	PCI_PRODUCT_INTEL_MTL_GSPI_1	0x7e30		/* Core Ultra GSPI */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_1	0x7e38		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_2	0x7e39		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_3	0x7e3a		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_4	0x7e3b		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_5	0x7e3c		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_6	0x7e3d		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_7	0x7e3e		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_8	0x7e3f		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_WL_22500_14	0x7e40		/* Wi-Fi 6 AX210 */
#define	PCI_PRODUCT_INTEL_MTL_ISH	0x7e45		/* Core Ultra ISH */
#define	PCI_PRODUCT_INTEL_MTL_GSPI_2	0x7e46		/* Core Ultra GSPI */
#define	PCI_PRODUCT_INTEL_MTL_THC_0_1	0x7e48		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_MTL_THC_0_2	0x7e49		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_MTL_THC_1_1	0x7e4a		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_MTL_THC_1_2	0x7e4b		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_MTL_GNA	0x7e4c		/* Core Ultra GNA */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_9	0x7e4d		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_I2C_4	0x7e50		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_MTL_I2C_5	0x7e51		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_MTL_UART_2	0x7e52		/* Core Ultra UART */
#define	PCI_PRODUCT_INTEL_MTL_HECI_5	0x7e58		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_MTL_HECI_6	0x7e59		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_MTL_HECI_7	0x7e5a		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_MTL_AHCI	0x7e63		/* Core Ultra AHCI */
#define	PCI_PRODUCT_INTEL_MTL_RAID_1	0x7e67		/* Core Ultra RAID */
#define	PCI_PRODUCT_INTEL_MTL_HECI_1	0x7e70		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_MTL_HECI_2	0x7e71		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_MTL_IDER	0x7e72		/* Core Ultra IDE-R */
#define	PCI_PRODUCT_INTEL_MTL_KT	0x7e73		/* Core Ultra KT */
#define	PCI_PRODUCT_INTEL_MTL_HECI_3	0x7e74		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_MTL_HECI_4	0x7e75		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_MTL_I2C_0	0x7e78		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_MTL_I2C_1	0x7e79		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_MTL_I2C_2	0x7e7a		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_MTL_I2C_3	0x7e7b		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_MTL_I3C	0x7e7c		/* Core Ultra I3C */
#define	PCI_PRODUCT_INTEL_MTL_XHCI_2	0x7e7d		/* Core Ultra xHCI */
#define	PCI_PRODUCT_INTEL_MTL_XDCI_2	0x7e7e		/* Core Ultra xDCI */
#define	PCI_PRODUCT_INTEL_MTL_SRAM	0x7e7f		/* Core Ultra SRAM */
#define	PCI_PRODUCT_INTEL_MTL_U4_XHCI	0x7eb0		/* Core Ultra xHCI */
#define	PCI_PRODUCT_INTEL_MTL_U4_XDCI	0x7eb1		/* Core Ultra xDCI */
#define	PCI_PRODUCT_INTEL_MTL_U4_TBT_DMA0	0x7eb2		/* Core Ultra TBT */
#define	PCI_PRODUCT_INTEL_MTL_U4_PCIE_16	0x7eb4		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_U4_PCIE_17	0x7eb5		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_U4_P2SB_IOE	0x7eb8		/* Core Ultra P2SB */
#define	PCI_PRODUCT_INTEL_MTL_U4_IEH_IOE	0x7eb9		/* Core Ultra IEH */
#define	PCI_PRODUCT_INTEL_MTL_U4_PMC_IOE	0x7ebe		/* Core Ultra PMC */
#define	PCI_PRODUCT_INTEL_MTL_U4_SRAM_IOE	0x7ebf		/* Core Ultra SRAM */
#define	PCI_PRODUCT_INTEL_MTL_XHCI_1	0x7ec0		/* Core Ultra xHCI */
#define	PCI_PRODUCT_INTEL_MTL_XDCI_1	0x7ec1		/* Core Ultra xDCI */
#define	PCI_PRODUCT_INTEL_MTL_TBT_DMA0	0x7ec2		/* Core Ultra TBT */
#define	PCI_PRODUCT_INTEL_MTL_TBT_DMA1	0x7ec3		/* Core Ultra TBT */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_16	0x7ec4		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_17	0x7ec5		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_18	0x7ec6		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_19	0x7ec7		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_P2SB_IOE	0x7ec8		/* Core Ultra P2SB */
#define	PCI_PRODUCT_INTEL_MTL_IEH_IOE	0x7ec9		/* Core Ultra IEH */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_10	0x7eca		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PCIE_11	0x7ecb		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_H_PCIE_12	0x7ecc		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_MTL_PMC_IOE	0x7ece		/* Core Ultra PMC */
#define	PCI_PRODUCT_INTEL_MTL_SRAM_IOE	0x7ecf		/* Core Ultra SRAM */
#define	PCI_PRODUCT_INTEL_WL_22500_15	0x7f70		/* Wi-Fi 6 AX211 */
#define	PCI_PRODUCT_INTEL_US15W_HB	0x8100		/* US15W Host */
#define	PCI_PRODUCT_INTEL_US15L_HB	0x8101		/* US15L/UL11L Host */
#define	PCI_PRODUCT_INTEL_US15W_IGD	0x8108		/* US15W Video */
#define	PCI_PRODUCT_INTEL_US15L_IGD	0x8109		/* US15L/UL11L Video */
#define	PCI_PRODUCT_INTEL_SCH_PCIE_1	0x8110		/* SCH PCIE */
#define	PCI_PRODUCT_INTEL_SCH_PCIE_2	0x8112		/* SCH PCIE */
#define	PCI_PRODUCT_INTEL_SCH_UHCI_1	0x8114		/* SCH USB */
#define	PCI_PRODUCT_INTEL_SCH_UHCI_2	0x8115		/* SCH USB */
#define	PCI_PRODUCT_INTEL_SCH_UHCI_3	0x8116		/* SCH USB */
#define	PCI_PRODUCT_INTEL_SCH_EHCI	0x8117		/* SCH USB */
#define	PCI_PRODUCT_INTEL_SCH_USBCL	0x8118		/* SCH USB Client */
#define	PCI_PRODUCT_INTEL_SCH_LPC	0x8119		/* SCH LPC */
#define	PCI_PRODUCT_INTEL_SCH_IDE	0x811a		/* SCH IDE */
#define	PCI_PRODUCT_INTEL_SCH_HDA	0x811b		/* SCH HD Audio */
#define	PCI_PRODUCT_INTEL_SCH_SDMMC_1	0x811c		/* SCH SD/MMC */
#define	PCI_PRODUCT_INTEL_SCH_SDMMC_2	0x811d		/* SCH SD/MMC */
#define	PCI_PRODUCT_INTEL_SCH_SDMMC_3	0x811e		/* SCH SD/MMC */
#define	PCI_PRODUCT_INTEL_E600_PCIE_3	0x8180		/* E600 PCIE */
#define	PCI_PRODUCT_INTEL_E600_PCIE_4	0x8181		/* E600 PCIE */
#define	PCI_PRODUCT_INTEL_E600_IGD	0x8182		/* E600 Video */
#define	PCI_PRODUCT_INTEL_E600_CFG	0x8183		/* E600 Config */
#define	PCI_PRODUCT_INTEL_E600_PCIE_1	0x8184		/* E600 PCIE */
#define	PCI_PRODUCT_INTEL_E600_PCIE_2	0x8185		/* E600 PCIE */
#define	PCI_PRODUCT_INTEL_E600_LPC	0x8186		/* E600 LPC */
#define	PCI_PRODUCT_INTEL_PCI450_PB	0x84c4		/* 82450KX/GX */
#define	PCI_PRODUCT_INTEL_PCI450_MC	0x84c5		/* 82450KX/GX Memory */
#define	PCI_PRODUCT_INTEL_82451NX	0x84ca		/* 82451NX Mem & IO */
#define	PCI_PRODUCT_INTEL_82454NX	0x84cb		/* 82454NX PXB */
#define	PCI_PRODUCT_INTEL_AML_KBL_Y_GT2_2	0x87c0		/* UHD Graphics 617 */
#define	PCI_PRODUCT_INTEL_AML_CFL_Y_GT2	0x87ca		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_EG20T_PCIE	0x8800		/* EG20T PCIE */
#define	PCI_PRODUCT_INTEL_EG20T_PH	0x8801		/* EG20T Packet Hub */
#define	PCI_PRODUCT_INTEL_EG20T_GBE	0x8802		/* EG20T Ethernet */
#define	PCI_PRODUCT_INTEL_EG20T_GPIO	0x8803		/* EG20T GPIO */
#define	PCI_PRODUCT_INTEL_EG20T_OHCI_1	0x8804		/* EG20T USB */
#define	PCI_PRODUCT_INTEL_EG20T_OHCI_2	0x8805		/* EG20T USB */
#define	PCI_PRODUCT_INTEL_EG20T_OHCI_3	0x8806		/* EG20T USB */
#define	PCI_PRODUCT_INTEL_EG20T_EHCI	0x8807		/* EG20T USB */
#define	PCI_PRODUCT_INTEL_EG20T_USBCL	0x8808		/* EG20T USB Client */
#define	PCI_PRODUCT_INTEL_EG20T_SDIO_1	0x8809		/* EG20T SDIO */
#define	PCI_PRODUCT_INTEL_EG20T_SDIO_2	0x880a		/* EG20T SDIO */
#define	PCI_PRODUCT_INTEL_EG20T_AHCI	0x880b		/* EG20T AHCI */
#define	PCI_PRODUCT_INTEL_EG20T_OHCI_4	0x880c		/* EG20T USB */
#define	PCI_PRODUCT_INTEL_EG20T_OHCI_5	0x880d		/* EG20T USB */
#define	PCI_PRODUCT_INTEL_EG20T_OHCI_6	0x880e		/* EG20T USB */
#define	PCI_PRODUCT_INTEL_EG20T_OHCI_7	0x880f		/* EG20T USB */
#define	PCI_PRODUCT_INTEL_EG20T_DMA_1	0x8810		/* EG20T DMA */
#define	PCI_PRODUCT_INTEL_EG20T_SERIAL_1	0x8811		/* EG20T Serial */
#define	PCI_PRODUCT_INTEL_EG20T_SERIAL_2	0x8812		/* EG20T Serial */
#define	PCI_PRODUCT_INTEL_EG20T_SERIAL_3	0x8813		/* EG20T Serial */
#define	PCI_PRODUCT_INTEL_EG20T_SERIAL_4	0x8814		/* EG20T Serial */
#define	PCI_PRODUCT_INTEL_EG20T_DMA_2	0x8815		/* EG20T DMA */
#define	PCI_PRODUCT_INTEL_EG20T_SPI	0x8816		/* EG20T SPI */
#define	PCI_PRODUCT_INTEL_EG20T_I2C	0x8817		/* EG20T I2C */
#define	PCI_PRODUCT_INTEL_EG20T_CAN	0x8818		/* EG20T CAN */
#define	PCI_PRODUCT_INTEL_EG20T_1588	0x8819		/* EG20T 1588 */
#define	PCI_PRODUCT_INTEL_82802AC	0x89ac		/* 82802AC Firmware Hub 8Mbit */
#define	PCI_PRODUCT_INTEL_82802AB	0x89ad		/* 82802AB Firmware Hub 4Mbit */
#define	PCI_PRODUCT_INTEL_ICL_GT2_1	0x8a50		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_ICL_GT2_2	0x8a51		/* Iris Plus */
#define	PCI_PRODUCT_INTEL_ICL_GT2_3	0x8a52		/* Iris Plus */
#define	PCI_PRODUCT_INTEL_ICL_GT2_4	0x8a53		/* Iris Plus */
#define	PCI_PRODUCT_INTEL_ICL_GT15_1	0x8a54		/* Iris Plus */
#define	PCI_PRODUCT_INTEL_ICL_GT1_1	0x8a56		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_ICL_GT15_2	0x8a57		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_ICL_GT1_2	0x8a58		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_ICL_GT15_3	0x8a59		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_ICL_GT15_4	0x8a5a		/* Iris Plus */
#define	PCI_PRODUCT_INTEL_ICL_GT1_3	0x8a5b		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_ICL_GT15_5	0x8a5c		/* Iris Plus */
#define	PCI_PRODUCT_INTEL_ICL_GT1_4	0x8a5d		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_ICL_GT1_5	0x8a70		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_ICL_GT_05_1	0x8a71		/* HD Graphics */
#define	PCI_PRODUCT_INTEL_8SERIES_SATA_1	0x8c00		/* 8 Series SATA */
#define	PCI_PRODUCT_INTEL_8SERIES_SATA_2	0x8c01		/* 8 Series SATA */
#define	PCI_PRODUCT_INTEL_8SERIES_AHCI_1	0x8c02		/* 8 Series AHCI */
#define	PCI_PRODUCT_INTEL_8SERIES_AHCI_2	0x8c03		/* 8 Series AHCI */
#define	PCI_PRODUCT_INTEL_8SERIES_RAID_1	0x8c04		/* 8 Series RAID */
#define	PCI_PRODUCT_INTEL_8SERIES_RAID_2	0x8c05		/* 8 Series RAID */
#define	PCI_PRODUCT_INTEL_8SERIES_RAID_3	0x8c06		/* 8 Series RAID */
#define	PCI_PRODUCT_INTEL_8SERIES_RAID_4	0x8c07		/* 8 Series RAID */
#define	PCI_PRODUCT_INTEL_8SERIES_SATA_3	0x8c08		/* 8 Series SATA */
#define	PCI_PRODUCT_INTEL_8SERIES_SATA_4	0x8c09		/* 8 Series SATA */
#define	PCI_PRODUCT_INTEL_8SERIES_RAID_5	0x8c0e		/* 8 Series RAID */
#define	PCI_PRODUCT_INTEL_8SERIES_RAID_6	0x8c0f		/* 8 Series RAID */
#define	PCI_PRODUCT_INTEL_8SERIES_PCIE_1	0x8c10		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_PCIE_2	0x8c12		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_PCIE_3	0x8c14		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_PCIE_4	0x8c16		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_PCIE_5	0x8c18		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_PCIE_6	0x8c1a		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_PCIE_7	0x8c1c		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_PCIE_8	0x8c1e		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_HDA	0x8c20		/* 8 Series HD Audio */
#define	PCI_PRODUCT_INTEL_8SERIES_SMB	0x8c22		/* 8 Series SMBus */
#define	PCI_PRODUCT_INTEL_8SERIES_THERM	0x8c24		/* 8 Series Thermal */
#define	PCI_PRODUCT_INTEL_8SERIES_EHCI_1	0x8c26		/* 8 Series USB */
#define	PCI_PRODUCT_INTEL_8SERIES_EHCI_2	0x8c2d		/* 8 Series USB */
#define	PCI_PRODUCT_INTEL_8SERIES_XHCI	0x8c31		/* 8 Series xHCI */
#define	PCI_PRODUCT_INTEL_8SERIES_MEI_1	0x8c3a		/* 8 Series MEI */
#define	PCI_PRODUCT_INTEL_8SERIES_MEI_2	0x8c3b		/* 8 Series MEI */
#define	PCI_PRODUCT_INTEL_8SERIES_IDER	0x8c3c		/* 8 Series IDER */
#define	PCI_PRODUCT_INTEL_8SERIES_KT	0x8c3d		/* 8 Series KT */
#define	PCI_PRODUCT_INTEL_Z87_LPC	0x8c44		/* Z87 LPC */
#define	PCI_PRODUCT_INTEL_Z85_LPC	0x8c46		/* Z85 LPC */
#define	PCI_PRODUCT_INTEL_HM86_LPC	0x8c49		/* HM86 LPC */
#define	PCI_PRODUCT_INTEL_H87_LPC	0x8c4a		/* H87 LPC */
#define	PCI_PRODUCT_INTEL_HM87_LPC	0x8c4b		/* HM87 LPC */
#define	PCI_PRODUCT_INTEL_Q85_LPC	0x8c4c		/* Q85 LPC */
#define	PCI_PRODUCT_INTEL_Q87_LPC	0x8c4e		/* Q87 LPC */
#define	PCI_PRODUCT_INTEL_QM87_LPC	0x8c4f		/* QM87 LPC */
#define	PCI_PRODUCT_INTEL_B85_LPC	0x8c50		/* B85 LPC */
#define	PCI_PRODUCT_INTEL_C222_LPC	0x8c52		/* C222 LPC */
#define	PCI_PRODUCT_INTEL_C224_LPC	0x8c54		/* C224 LPC */
#define	PCI_PRODUCT_INTEL_C226_LPC	0x8c56		/* C226 LPC */
#define	PCI_PRODUCT_INTEL_H81_LPC	0x8c5c		/* H81 LPC */
#define	PCI_PRODUCT_INTEL_9SERIES_SATA_1	0x8c80		/* 9 Series SATA */
#define	PCI_PRODUCT_INTEL_9SERIES_AHCI	0x8c82		/* 9 Series AHCI */
#define	PCI_PRODUCT_INTEL_9SERIES_RAID_1	0x8c84		/* 9 Series RAID */
#define	PCI_PRODUCT_INTEL_9SERIES_RAID_2	0x8c86		/* 9 Series RAID */
#define	PCI_PRODUCT_INTEL_9SERIES_SATA_2	0x8c88		/* 9 Series SATA */
#define	PCI_PRODUCT_INTEL_9SERIES_RAID_3	0x8c8e		/* 9 Series RAID */
#define	PCI_PRODUCT_INTEL_9SERIES_PCIE_1	0x8c90		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_PCIE_2	0x8c92		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_PCIE_3	0x8c94		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_PCIE_4	0x8c96		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_PCIE_5	0x8c98		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_PCIE_6	0x8c9a		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_PCIE_7	0x8c9c		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_PCIE_8	0x8c9e		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_HDA	0x8ca0		/* 9 Series HD Audio */
#define	PCI_PRODUCT_INTEL_9SERIES_SMB	0x8ca2		/* 9 Series SMBus */
#define	PCI_PRODUCT_INTEL_9SERIES_EHCI_1	0x8ca6		/* 9 Series USB */
#define	PCI_PRODUCT_INTEL_9SERIES_EHCI_2	0x8cad		/* 9 Series USB */
#define	PCI_PRODUCT_INTEL_9SERIES_XHCI	0x8cb1		/* 9 Series xHCI */
#define	PCI_PRODUCT_INTEL_9SERIES_MEI_1	0x8cba		/* 9 Series MEI */
#define	PCI_PRODUCT_INTEL_9SERIES_MEI_2	0x8cbb		/* 9 Series MEI */
#define	PCI_PRODUCT_INTEL_9SERIES_IDER	0x8cbc		/* 9 Series IDER */
#define	PCI_PRODUCT_INTEL_9SERIES_KT	0x8cbd		/* 9 Series KT */
#define	PCI_PRODUCT_INTEL_Z97_LPC	0x8cc4		/* Z97 LPC */
#define	PCI_PRODUCT_INTEL_H97_LPC	0x8cc6		/* H97 LPC */
#define	PCI_PRODUCT_INTEL_C610_SATA_1	0x8d00		/* C610 SATA */
#define	PCI_PRODUCT_INTEL_C610_AHCI_1	0x8d02		/* C610 AHCI */
#define	PCI_PRODUCT_INTEL_C610_RAID_1	0x8d06		/* C610 RAID */
#define	PCI_PRODUCT_INTEL_C610_SATA_2	0x8d08		/* C610 SATA */
#define	PCI_PRODUCT_INTEL_C610_PCIE_1	0x8d10		/* C610 PCIE */
#define	PCI_PRODUCT_INTEL_C610_PCIE_2	0x8d12		/* C610 PCIE */
#define	PCI_PRODUCT_INTEL_C610_PCIE_3	0x8d14		/* C610 PCIE */
#define	PCI_PRODUCT_INTEL_C610_PCIE_4	0x8d16		/* C610 PCIE */
#define	PCI_PRODUCT_INTEL_C610_PCIE_5	0x8d18		/* C610 PCIE */
#define	PCI_PRODUCT_INTEL_C610_PCIE_6	0x8d1a		/* C610 PCIE */
#define	PCI_PRODUCT_INTEL_C610_PCIE_7	0x8d1c		/* C610 PCIE */
#define	PCI_PRODUCT_INTEL_C610_PCIE_8	0x8d1e		/* C610 PCIE */
#define	PCI_PRODUCT_INTEL_C610_HDA_1	0x8d20		/* C610 HD Audio */
#define	PCI_PRODUCT_INTEL_C610_HDA_2	0x8d21		/* C610 HD Audio */
#define	PCI_PRODUCT_INTEL_C610_SMB	0x8d22		/* C610 SMBus */
#define	PCI_PRODUCT_INTEL_C610_THERM	0x8d24		/* C610 Thermal */
#define	PCI_PRODUCT_INTEL_C610_EHCI_1	0x8d26		/* C610 USB */
#define	PCI_PRODUCT_INTEL_C610_EHCI_2	0x8d2d		/* C610 USB */
#define	PCI_PRODUCT_INTEL_C610_XHCI	0x8d31		/* C610 xHCI */
#define	PCI_PRODUCT_INTEL_C610_MEI_1	0x8d3a		/* C610 MEI */
#define	PCI_PRODUCT_INTEL_C610_MEI_2	0x8d3b		/* C610 MEI */
#define	PCI_PRODUCT_INTEL_C610_LPC	0x8d44		/* C610 LPC */
#define	PCI_PRODUCT_INTEL_X99_LPC	0x8d47		/* X99 LPC */
#define	PCI_PRODUCT_INTEL_C610_SATA_3	0x8d60		/* C610 SATA */
#define	PCI_PRODUCT_INTEL_C610_AHCI_2	0x8d62		/* C610 AHCI */
#define	PCI_PRODUCT_INTEL_C610_RAID_2	0x8d66		/* C610 RAID */
#define	PCI_PRODUCT_INTEL_C610_MS_SPSR	0x8d7c		/* C610 MS SPSR */
#define	PCI_PRODUCT_INTEL_C610_MS_SMB_1	0x8d7d		/* C610 MS SMBus */
#define	PCI_PRODUCT_INTEL_C610_MS_SMB_2	0x8d7e		/* C610 MS SMBus */
#define	PCI_PRODUCT_INTEL_C610_MS_SMB_3	0x8d7f		/* C610 MS SMBus */
#define	PCI_PRODUCT_INTEL_I2OPCIB	0x9620		/* I2O RAID */
#define	PCI_PRODUCT_INTEL_RCU21	0x9621		/* RCU21 I2O RAID */
#define	PCI_PRODUCT_INTEL_RCUXX	0x9622		/* RCUxx I2O RAID */
#define	PCI_PRODUCT_INTEL_RCU31	0x9641		/* RCU31 I2O RAID */
#define	PCI_PRODUCT_INTEL_RCU31L	0x96a1		/* RCU31L I2O RAID */
#define	PCI_PRODUCT_INTEL_TGL_H_PCIE_0	0x9a01		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_UP4_2C_HB	0x9a02		/* Core 11G Host */
#define	PCI_PRODUCT_INTEL_TGL_DTT	0x9a03		/* Core 11G DTT */
#define	PCI_PRODUCT_INTEL_TGL_UP3_2C_HB	0x9a04		/* Core 11G Host */
#define	PCI_PRODUCT_INTEL_TGL_H_PCIE_1	0x9a05		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_H_PCIE_2	0x9a07		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_PCIE_1	0x9a09		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_VMD	0x9a0b		/* Core 11G VMD */
#define	PCI_PRODUCT_INTEL_TGL_SRAM	0x9a0d		/* Core 11G SRAM */
#define	PCI_PRODUCT_INTEL_TGL_H_PCIE_3	0x9a0f		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_GNA	0x9a11		/* Core 11G GNA */
#define	PCI_PRODUCT_INTEL_TGL_UP4_4C_HB	0x9a12		/* Core 11G Host */
#define	PCI_PRODUCT_INTEL_TGL_XHCI	0x9a13		/* Core 11G xHCI */
#define	PCI_PRODUCT_INTEL_TGL_UP3_4C_HB	0x9a14		/* Core 11G Host */
#define	PCI_PRODUCT_INTEL_TGL_XDCI	0x9a15		/* Core 11G xDCI */
#define	PCI_PRODUCT_INTEL_TGL_H_XHCI	0x9a17		/* Core 11G xHCI */
#define	PCI_PRODUCT_INTEL_TGL_IPU	0x9a19		/* Core 11G IPU */
#define	PCI_PRODUCT_INTEL_TGL_UP3R_4C_HB	0x9a1a		/* Core 11G Host */
#define	PCI_PRODUCT_INTEL_TGL_TBT_DMA0	0x9a1b		/* Core 11G TBT */
#define	PCI_PRODUCT_INTEL_TGL_TBT_DMA1	0x9a1d		/* Core 11G TBT */
#define	PCI_PRODUCT_INTEL_TGL_H_TBT_DMA0	0x9a1f		/* Core 11G TBT */
#define	PCI_PRODUCT_INTEL_TGL_H_TBT_DMA1	0x9a21		/* Core 11G TBT */
#define	PCI_PRODUCT_INTEL_TGL_PCIE_2	0x9a23		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_PCIE_3	0x9a25		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_H_6C_HB	0x9a26		/* Core 11G Host */
#define	PCI_PRODUCT_INTEL_TGL_PCIE_4	0x9a27		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_PCIE_5	0x9a29		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_H_TBT_PCIE0	0x9a2b		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_H_TBT_PCIE1	0x9a2d		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_H_TBT_PCIE2	0x9a2f		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_H_TBT_PCIE3	0x9a31		/* Core 11G PCIE */
#define	PCI_PRODUCT_INTEL_TGL_NPK	0x9a33		/* Core 11G NPK */
#define	PCI_PRODUCT_INTEL_TGL_H_8C_HB	0x9a36		/* Core 11G Host */
#define	PCI_PRODUCT_INTEL_TGL_H_IPU	0x9a39		/* Core 11G IPU */
#define	PCI_PRODUCT_INTEL_TGL_GT2_1	0x9a40		/* Xe Graphics */
#define	PCI_PRODUCT_INTEL_TGL_GT2_2	0x9a49		/* Xe Graphics */
#define	PCI_PRODUCT_INTEL_TGL_GT2_3	0x9a59		/* Graphics */
#define	PCI_PRODUCT_INTEL_TGL_GT1_1	0x9a60		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_TGL_GT1_2	0x9a68		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_TGL_GT1_3	0x9a70		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_TGL_GT2_4	0x9a78		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_TGL_GT2_5	0x9ac0		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_TGL_GT2_6	0x9ac9		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_TGL_GT2_7	0x9ad9		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_TGL_GT2_8	0x9af8		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_U_GT1_1	0x9b21		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_S_HB_1	0x9b33		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_U_GT2_1	0x9b41		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_S_HB_2	0x9b43		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_H_HB_1	0x9b44		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_U_HB_1	0x9b51		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_S_HB_3	0x9b53		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_H_HB_2	0x9b54		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_U_HB_2	0x9b61		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_S_HB_4	0x9b63		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_H_HB_3	0x9b64		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_U_HB_3	0x9b71		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_S_HB_5	0x9b73		/* Core 10G Host */
#define	PCI_PRODUCT_INTEL_CML_GT1_4	0x9ba2		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_GT1_3	0x9ba4		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_GT1_1	0x9ba5		/* UHD Graphics 610 */
#define	PCI_PRODUCT_INTEL_CML_GT1_2	0x9ba8		/* UHD Graphics 610 */
#define	PCI_PRODUCT_INTEL_CML_U_GT1_2	0x9baa		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_U_GT1_3	0x9bac		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_GT2_4	0x9bc2		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_GT2_3	0x9bc4		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_GT2_1	0x9bc5		/* UHD Graphics 630 */
#define	PCI_PRODUCT_INTEL_CML_GT2_5	0x9bc6		/* UHD Graphics P630 */
#define	PCI_PRODUCT_INTEL_CML_GT2_2	0x9bc8		/* UHD Graphics 630 */
#define	PCI_PRODUCT_INTEL_CML_U_GT2_2	0x9bca		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_U_GT2_3	0x9bcc		/* UHD Graphics */
#define	PCI_PRODUCT_INTEL_CML_GT2_6	0x9be6		/* UHD Graphics P630 */
#define	PCI_PRODUCT_INTEL_CML_GT2_7	0x9bf6		/* UHD Graphics P630 */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_SATA_1	0x9c00		/* 8 Series SATA */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_SATA_2	0x9c01		/* 8 Series SATA */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_AHCI	0x9c03		/* 8 Series AHCI */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_RAID_1	0x9c05		/* 8 Series RAID */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_RAID_2	0x9c07		/* 8 Series RAID */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_SATA_3	0x9c08		/* 8 Series SATA */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_SATA_4	0x9c09		/* 8 Series SATA */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_PCIE_1	0x9c10		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_PCIE_2	0x9c12		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_PCIE_3	0x9c14		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_PCIE_4	0x9c16		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_PCIE_5	0x9c18		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_PCIE_6	0x9c1a		/* 8 Series PCIE */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_HDA	0x9c20		/* 8 Series HD Audio */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_SMB	0x9c22		/* 8 Series SMBus */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_THERM	0x9c24		/* 8 Series Thermal */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_EHCI	0x9c26		/* 8 Series USB */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_XHCI	0x9c31		/* 8 Series xHCI */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_MEI_1	0x9c3a		/* 8 Series MEI */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_MEI_2	0x9c3b		/* 8 Series MEI */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_IDER	0x9c3c		/* 8 Series IDER */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_KT	0x9c3d		/* 8 Series KT */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_LPC_1	0x9c41		/* 8 Series LPC */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_LPC_2	0x9c43		/* 8 Series LPC */
#define	PCI_PRODUCT_INTEL_8SERIES_LP_LPC_3	0x9c45		/* 8 Series LPC */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_AHCI	0x9c83		/* 9 Series AHCI */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_RAID_1	0x9c85		/* 9 Series RAID */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_RAID_2	0x9c87		/* 9 Series RAID */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_RAID_3	0x9c8f		/* 9 Series RAID */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_PCIE_1	0x9c90		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_PCIE_2	0x9c92		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_PCIE_3	0x9c94		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_PCIE_4	0x9c96		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_PCIE_5	0x9c98		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_PCIE_6	0x9c9a		/* 9 Series PCIE */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_HDA	0x9ca0		/* 9 Series HD Audio */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_SMB	0x9ca2		/* 9 Series SMBus */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_THERM	0x9ca4		/* 9 Series Thermal */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_EHCI	0x9ca6		/* 9 Series USB */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_XHCI	0x9cb1		/* 9 Series xHCI */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_MEI_1	0x9cba		/* 9 Series MEI */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_MEI_2	0x9cbb		/* 9 Series MEI */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_IDER	0x9cbc		/* 9 Series IDER */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_KT	0x9cbd		/* 9 Series KT */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_LPC_1	0x9cc3		/* 9 Series LPC */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_LPC_2	0x9cc5		/* 9 Series LPC */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_LPC_3	0x9cc7		/* 9 Series LPC */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_LPC_4	0x9cc9		/* 9 Series LPC */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_DMA	0x9ce0		/* 9 Series DMA */
#define	PCI_PRODUCT_INTEL_9SERIES_LP_SPI	0x9ce6		/* 9 Series SPI */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_AHCI	0x9d03		/* 100 Series AHCI */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_RAID_1	0x9d05		/* 100 Series RAID */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_RAID_2	0x9d07		/* 100 Series RAID */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_1	0x9d10		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_2	0x9d11		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_3	0x9d12		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_4	0x9d13		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_5	0x9d14		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_6	0x9d15		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_7	0x9d16		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_8	0x9d17		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_9	0x9d18		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_10	0x9d19		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_11	0x9d1a		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PCIE_12	0x9d1b		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_PMC	0x9d21		/* 100 Series PMC */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_SMB	0x9d23		/* 100 Series SMBus */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_SPI_1	0x9d24		/* 100 Series SPI */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_UART_1	0x9d27		/* 100 Series UART */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_UART_2	0x9d28		/* 100 Series UART */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_SPI_2	0x9d29		/* 100 Series SPI */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_SPI_3	0x9d2a		/* 100 Series SPI */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_EMMC	0x9d2b		/* 100 Series eMMC */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_SDXC	0x9d2d		/* 100 Series SDXC */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_XHCI	0x9d2f		/* 100 Series xHCI */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_THERM	0x9d31		/* 100 Series Thermal */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_ISH	0x9d35		/* 100 Series ISH */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_MEI_1	0x9d3a		/* 100 Series MEI */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_MEI_2	0x9d3b		/* 100 Series MEI */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_IDER	0x9d3c		/* 100 Series IDER */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_KT	0x9d3d		/* 100 Series KT */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_MEI_3	0x9d3e		/* 100 Series MEI */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_LPC_1	0x9d43		/* 100 Series LPC */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_LPC_2	0x9d46		/* 100 Series LPC */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_LPC_3	0x9d48		/* 100 Series LPC */
#define	PCI_PRODUCT_INTEL_200SERIES_Y_LPC_1	0x9d4b		/* 200 Series LPC */
#define	PCI_PRODUCT_INTEL_200SERIES_U_LPC_1	0x9d4e		/* 200 Series LPC */
#define	PCI_PRODUCT_INTEL_200SERIES_U_LPC_2	0x9d50		/* 200 Series LPC */
#define	PCI_PRODUCT_INTEL_200SERIES_U_LPC_3	0x9d53		/* 200 Series LPC */
#define	PCI_PRODUCT_INTEL_200SERIES_Y_LPC_2	0x9d56		/* 200 Series LPC */
#define	PCI_PRODUCT_INTEL_200SERIES_U_LPC_4	0x9d58		/* 200 Series LPC */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_I2C_1	0x9d60		/* 100 Series I2C */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_I2C_2	0x9d61		/* 100 Series I2C */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_I2C_3	0x9d62		/* 100 Series I2C */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_I2C_4	0x9d63		/* 100 Series I2C */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_I2C_5	0x9d64		/* 100 Series I2C */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_I2C_6	0x9d65		/* 100 Series I2C */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_UART_3	0x9d66		/* 100 Series UART */
#define	PCI_PRODUCT_INTEL_100SERIES_LP_HDA	0x9d70		/* 100 Series HD Audio */
#define	PCI_PRODUCT_INTEL_200SERIES_U_HDA	0x9d71		/* 200 Series HD Audio */
#define	PCI_PRODUCT_INTEL_300SERIES_U_LPC	0x9d84		/* 300 Series LPC */
#define	PCI_PRODUCT_INTEL_300SERIES_U_P2SB	0x9da0		/* 300 Series P2SB */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PMC	0x9da1		/* 300 Series PMC */
#define	PCI_PRODUCT_INTEL_300SERIES_U_SMB	0x9da3		/* 300 Series SMBus */
#define	PCI_PRODUCT_INTEL_300SERIES_U_SPI_1	0x9da4		/* 300 Series SPI */
#define	PCI_PRODUCT_INTEL_300SERIES_U_TH	0x9da6		/* 300 Series TH */
#define	PCI_PRODUCT_INTEL_300SERIES_U_UART_1	0x9da8		/* 300 Series UART */
#define	PCI_PRODUCT_INTEL_300SERIES_U_UART_2	0x9da9		/* 300 Series UART */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_1	0x9db0		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_2	0x9db1		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_3	0x9db2		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_4	0x9db3		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_5	0x9db4		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_6	0x9db5		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_7	0x9db6		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_8	0x9db7		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_9	0x9db8		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_10	0x9db9		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_11	0x9dba		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_12	0x9dbb		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_13	0x9dbc		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_14	0x9dbd		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_15	0x9dbe		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_PCIE_16	0x9dbf		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_U_EMMC	0x9dc4		/* 300 Series eMMC */
#define	PCI_PRODUCT_INTEL_300SERIES_U_I2C_1	0x9dc5		/* 300 Series I2C */
#define	PCI_PRODUCT_INTEL_300SERIES_U_I2C_2	0x9dc6		/* 300 Series I2C */
#define	PCI_PRODUCT_INTEL_300SERIES_U_UART_3	0x9dc7		/* 300 Series UART */
#define	PCI_PRODUCT_INTEL_300SERIES_U_HDA	0x9dc8		/* 300 Series HD Audio */
#define	PCI_PRODUCT_INTEL_300SERIES_U_AHCI	0x9dd3		/* 300 Series AHCI */
#define	PCI_PRODUCT_INTEL_300SERIES_U_RAID_1	0x9dd5		/* 300 Series RAID */
#define	PCI_PRODUCT_INTEL_300SERIES_U_RAID_2	0x9dd7		/* 300 Series RAID */
#define	PCI_PRODUCT_INTEL_300SERIES_U_MEI_1	0x9de0		/* 300 Series MEI */
#define	PCI_PRODUCT_INTEL_300SERIES_U_MEI_2	0x9de1		/* 300 Series MEI */
#define	PCI_PRODUCT_INTEL_300SERIES_U_IDER	0x9de2		/* 300 Series IDER */
#define	PCI_PRODUCT_INTEL_300SERIES_U_KT	0x9de3		/* 300 Series KT */
#define	PCI_PRODUCT_INTEL_300SERIES_U_MEI_3	0x9de4		/* 300 Series MEI */
#define	PCI_PRODUCT_INTEL_300SERIES_U_MEI_4	0x9de5		/* 300 Series MEI */
#define	PCI_PRODUCT_INTEL_300SERIES_U_I2C_3	0x9de8		/* 300 Series I2C */
#define	PCI_PRODUCT_INTEL_300SERIES_U_I2C_4	0x9de9		/* 300 Series I2C */
#define	PCI_PRODUCT_INTEL_300SERIES_U_I2C_5	0x9dea		/* 300 Series I2C */
#define	PCI_PRODUCT_INTEL_300SERIES_U_I2C_6	0x9deb		/* 300 Series I2C */
#define	PCI_PRODUCT_INTEL_300SERIES_U_XHCI	0x9ded		/* 300 Series xHCI */
#define	PCI_PRODUCT_INTEL_300SERIES_U_SSRAM	0x9def		/* 300 Series Shared SRAM */
#define	PCI_PRODUCT_INTEL_WL_9560_1	0x9df0		/* AC 9560 */
#define	PCI_PRODUCT_INTEL_300SERIES_U_SDXC	0x9df5		/* 300 Series SDXC */
#define	PCI_PRODUCT_INTEL_300SERIES_U_THERM	0x9df9		/* 300 Series Thermal */
#define	PCI_PRODUCT_INTEL_300SERIES_U_SPI_2	0x9dfb		/* 300 Series SPI */
#define	PCI_PRODUCT_INTEL_300SERIES_U_ISH	0x9dfc		/* 300 Series ISH */
#define	PCI_PRODUCT_INTEL_PINEVIEW_DMI	0xa000		/* Pineview DMI */
#define	PCI_PRODUCT_INTEL_PINEVIEW_IGC_1	0xa001		/* Pineview Video */
#define	PCI_PRODUCT_INTEL_PINEVIEW_IGC_2	0xa002		/* Pineview Video */
#define	PCI_PRODUCT_INTEL_PINEVIEW_M_DMI	0xa010		/* Pineview DMI */
#define	PCI_PRODUCT_INTEL_PINEVIEW_M_IGC_1	0xa011		/* Pineview Video */
#define	PCI_PRODUCT_INTEL_PINEVIEW_M_IGC_2	0xa012		/* Pineview Video */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_ESPI_UP3	0xa082		/* 500 Series eSPI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_ESPI_UP4	0xa087		/* 500 Series eSPI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_P2SB	0xa0a0		/* 500 Series P2SB */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PMC	0xa0a1		/* 500 Series PMC */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_SMB	0xa0a3		/* 500 Series SMBus */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_SPI_FLASH	0xa0a4		/* 500 Series SPI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_TH	0xa0a6		/* 500 Series TH */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_UART_1	0xa0a8		/* 500 Series UART */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_UART_2	0xa0a9		/* 500 Series UART */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_GSPI_1	0xa0aa		/* 500 Series GSPI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_GSPI_2	0xa0ab		/* 500 Series GSPI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_9	0xa0b0		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_10	0xa0b1		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_11	0xa0b2		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_12	0xa0b3		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_1	0xa0b8		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_2	0xa0b9		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_3	0xa0ba		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_4	0xa0bb		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_5	0xa0bc		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_6	0xa0bd		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_7	0xa0be		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_PCIE_8	0xa0bf		/* 500 Series PCIE */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_I2C_1	0xa0c5		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_I2C_2	0xa0c6		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_UART_3	0xa0c7		/* 500 Series UART */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_HDA	0xa0c8		/* 500 Series HD Audio */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_THC_1	0xa0d0		/* 500 Series THC */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_THC_2	0xa0d1		/* 500 Series THC */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_AHCI	0xa0d3		/* 500 Series AHCI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_RAID	0xa0d7		/* 500 Series RAID */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_UART_4	0xa0da		/* 500 Series UART */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_HECI_1	0xa0e0		/* 500 Series HECI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_HECI_2	0xa0e1		/* 500 Series HECI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_IDER	0xa0e2		/* 500 Series IDE-R */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_KT	0xa0e3		/* 500 Series KT */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_HECI_3	0xa0e4		/* 500 Series HECI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_HECI_4	0xa0e5		/* 500 Series HECI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_I2C_3	0xa0e8		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_I2C_4	0xa0e9		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_I2C_5	0xa0ea		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_I2C_6	0xa0eb		/* 500 Series I2C */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_XHCI	0xa0ed		/* 500 Series xHCI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_XDCI	0xa0ee		/* 500 Series xDCI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_SRAM	0xa0ef		/* 500 Series Shared SRAM */
#define	PCI_PRODUCT_INTEL_WL_22500_3	0xa0f0		/* Wi-Fi 6 AX201 */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_GSPI_3	0xa0fb		/* 500 Series GSPI */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_ISH	0xa0fc		/* 500 Series ISH */
#define	PCI_PRODUCT_INTEL_500SERIES_LP_GSPI_4	0xa0fd		/* 500 Series GSPI */
#define	PCI_PRODUCT_INTEL_100SERIES_AHCI_1	0xa102		/* 100 Series AHCI */
#define	PCI_PRODUCT_INTEL_100SERIES_AHCI_2	0xa103		/* 100 Series AHCI */
#define	PCI_PRODUCT_INTEL_100SERIES_RAID_1	0xa105		/* 100 Series RAID */
#define	PCI_PRODUCT_INTEL_100SERIES_RAID_2	0xa106		/* 100 Series RAID */
#define	PCI_PRODUCT_INTEL_100SERIES_RAID_3	0xa107		/* 100 Series RAID */
#define	PCI_PRODUCT_INTEL_100SERIES_RAID_4	0xa10f		/* 100 Series RAID */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_1	0xa110		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_2	0xa111		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_3	0xa112		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_4	0xa113		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_5	0xa114		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_6	0xa115		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_7	0xa116		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_8	0xa117		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_9	0xa118		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_10	0xa119		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_11	0xa11a		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_12	0xa11b		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_13	0xa11c		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_14	0xa11d		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_15	0xa11e		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_16	0xa11f		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PMC	0xa121		/* 100 Series PMC */
#define	PCI_PRODUCT_INTEL_100SERIES_SMB	0xa123		/* 100 Series SMBus */
#define	PCI_PRODUCT_INTEL_100SERIES_XHCI	0xa12f		/* 100 Series xHCI */
#define	PCI_PRODUCT_INTEL_100SERIES_THERM	0xa131		/* 100 Series Thermal */
#define	PCI_PRODUCT_INTEL_100SERIES_ISH	0xa135		/* 100 Series ISH */
#define	PCI_PRODUCT_INTEL_100SERIES_MEI_1	0xa13a		/* 100 Series MEI */
#define	PCI_PRODUCT_INTEL_100SERIES_MEI_2	0xa13b		/* 100 Series MEI */
#define	PCI_PRODUCT_INTEL_100SERIES_IDER	0xa13c		/* 100 Series IDER */
#define	PCI_PRODUCT_INTEL_100SERIES_KT	0xa13d		/* 100 Series KT */
#define	PCI_PRODUCT_INTEL_H110_LPC	0xa143		/* H110 LPC */
#define	PCI_PRODUCT_INTEL_H170_LPC	0xa144		/* H170 LPC */
#define	PCI_PRODUCT_INTEL_Z170_LPC	0xa145		/* Z170 LPC */
#define	PCI_PRODUCT_INTEL_Q170_LPC	0xa146		/* Q170 LPC */
#define	PCI_PRODUCT_INTEL_Q150_LPC	0xa147		/* Q150 LPC */
#define	PCI_PRODUCT_INTEL_B150_LPC	0xa148		/* B150 LPC */
#define	PCI_PRODUCT_INTEL_C236_LPC	0xa149		/* C236 LPC */
#define	PCI_PRODUCT_INTEL_C232_LPC	0xa14a		/* C232 LPC */
#define	PCI_PRODUCT_INTEL_CQM170_LPC	0xa14d		/* CQM170 LPC */
#define	PCI_PRODUCT_INTEL_HM170_LPC	0xa14e		/* HM170 LPC */
#define	PCI_PRODUCT_INTEL_CM236_LPC	0xa150		/* CM236 LPC */
#define	PCI_PRODUCT_INTEL_HM175_LPC	0xa152		/* HM175 LPC */
#define	PCI_PRODUCT_INTEL_CM238_LPC	0xa154		/* CM238 LPC */
#define	PCI_PRODUCT_INTEL_100SERIES_I2C0	0xa160		/* 100 Series I2C */
#define	PCI_PRODUCT_INTEL_100SERIES_I2C1	0xa161		/* 100 Series I2C */
#define	PCI_PRODUCT_INTEL_100SERIES_I2C2	0xa162		/* 100 Series I2C */
#define	PCI_PRODUCT_INTEL_100SERIES_I2C3	0xa163		/* 100 Series I2C */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_17	0xa167		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_18	0xa168		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_19	0xa169		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_PCIE_20	0xa16a		/* 100 Series PCIE */
#define	PCI_PRODUCT_INTEL_100SERIES_HDA	0xa170		/* 100 Series HD Audio */
#define	PCI_PRODUCT_INTEL_100SERIES_H_HDA	0xa171		/* 100 Series HD Audio */
#define	PCI_PRODUCT_INTEL_C620_AHCI_1	0xa182		/* C620 AHCI */
#define	PCI_PRODUCT_INTEL_C620_PCIE_1	0xa190		/* C620 PCIE */
#define	PCI_PRODUCT_INTEL_C620_PCIE_2	0xa194		/* C620 PCIE */
#define	PCI_PRODUCT_INTEL_C620_PM	0xa1a1		/* C620 Power */
#define	PCI_PRODUCT_INTEL_C620_SMB	0xa1a3		/* C620 SMBus */
#define	PCI_PRODUCT_INTEL_C620_SPI	0xa1a4		/* C620 SPI */
#define	PCI_PRODUCT_INTEL_C620_XHCI	0xa1af		/* C620 xHCI */
#define	PCI_PRODUCT_INTEL_C620_THERM	0xa1b1		/* C620 Thermal */
#define	PCI_PRODUCT_INTEL_C620_MEI_1	0xa1ba		/* C620 MEI */
#define	PCI_PRODUCT_INTEL_C620_MEI_2	0xa1bb		/* C620 MEI */
#define	PCI_PRODUCT_INTEL_C620_MEI_3	0xa1be		/* C620 MEI */
#define	PCI_PRODUCT_INTEL_C621_LPC	0xa1c1		/* C621 LPC */
#define	PCI_PRODUCT_INTEL_C620_AHCI_2	0xa1d2		/* C620 AHCI */
#define	PCI_PRODUCT_INTEL_C620_MROM0	0xa1ec		/* C620 MROM */
#define	PCI_PRODUCT_INTEL_C620_HDA_1	0xa1f0		/* C620 HD Audio */
#define	PCI_PRODUCT_INTEL_C620_HDA_2	0xa270		/* C620 HD Audio */
#define	PCI_PRODUCT_INTEL_200SERIES_AHCI_1	0xa282		/* 200 Series AHCI */
#define	PCI_PRODUCT_INTEL_200SERIES_RAID_1	0xa286		/* 200 Series RAID */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_1	0xa290		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_2	0xa291		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_3	0xa292		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_4	0xa293		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_5	0xa294		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_6	0xa295		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_7	0xa296		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_8	0xa297		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_9	0xa298		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_10	0xa299		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_11	0xa29a		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_12	0xa29b		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_13	0xa29c		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_14	0xa29d		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_15	0xa29e		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_16	0xa29f		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_P2SB	0xa2a0		/* 200 Series P2SB */
#define	PCI_PRODUCT_INTEL_200SERIES_PMC	0xa2a1		/* 200 Series PMC */
#define	PCI_PRODUCT_INTEL_200SERIES_SMB	0xa2a3		/* 200 Series SMBus */
#define	PCI_PRODUCT_INTEL_200SERIES_SPI	0xa2a4		/* 200 Series SPI */
#define	PCI_PRODUCT_INTEL_200SERIES_TH	0xa2a6		/* 200 Series TH */
#define	PCI_PRODUCT_INTEL_200SERIES_UART_1	0xa2a7		/* 200 Series UART */
#define	PCI_PRODUCT_INTEL_200SERIES_UART_2	0xa2a8		/* 200 Series UART */
#define	PCI_PRODUCT_INTEL_200SERIES_GSPI_1	0xa2a9		/* 200 Series GSPI */
#define	PCI_PRODUCT_INTEL_200SERIES_GSPI_2	0xa2aa		/* 200 Series GSPI */
#define	PCI_PRODUCT_INTEL_200SERIES_XHCI	0xa2af		/* 200 Series xHCI */
#define	PCI_PRODUCT_INTEL_200SERIES_THERM	0xa2b1		/* 200 Series Thermal */
#define	PCI_PRODUCT_INTEL_200SERIES_ISH	0xa2b5		/* 200 Series ISH */
#define	PCI_PRODUCT_INTEL_200SERIES_MEI_1	0xa2ba		/* 200 Series MEI */
#define	PCI_PRODUCT_INTEL_200SERIES_MEI_2	0xa2bb		/* 200 Series MEI */
#define	PCI_PRODUCT_INTEL_200SERIES_IDER	0xa2bc		/* 200 Series IDER */
#define	PCI_PRODUCT_INTEL_200SERIES_KT	0xa2bd		/* 200 Series KT */
#define	PCI_PRODUCT_INTEL_200SERIES_MEI_3	0xa2be		/* 200 Series MEI */
#define	PCI_PRODUCT_INTEL_H270_LPC	0xa2c4		/* H270 LPC */
#define	PCI_PRODUCT_INTEL_Z270_LPC	0xa2c5		/* Z270 LPC */
#define	PCI_PRODUCT_INTEL_Q270_LPC	0xa2c6		/* Q270 LPC */
#define	PCI_PRODUCT_INTEL_Q250_LPC	0xa2c7		/* Q250 LPC */
#define	PCI_PRODUCT_INTEL_B250_LPC	0xa2c8		/* B250 LPC */
#define	PCI_PRODUCT_INTEL_Z370_LPC	0xa2c9		/* Z370 LPC */
#define	PCI_PRODUCT_INTEL_X299_LPC	0xa2d2		/* X299 LPC */
#define	PCI_PRODUCT_INTEL_C422_LPC	0xa2d3		/* C422 LPC */
#define	PCI_PRODUCT_INTEL_200SERIES_I2C_1	0xa2e0		/* 200 Series I2C */
#define	PCI_PRODUCT_INTEL_200SERIES_I2C_2	0xa2e1		/* 200 Series I2C */
#define	PCI_PRODUCT_INTEL_200SERIES_I2C_3	0xa2e2		/* 200 Series I2C */
#define	PCI_PRODUCT_INTEL_200SERIES_I2C_4	0xa2e3		/* 200 Series I2C */
#define	PCI_PRODUCT_INTEL_200SERIES_UART_3	0xa2e6		/* 200 Series UART */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_17	0xa2e7		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_18	0xa2e8		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_19	0xa2e9		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_20	0xa2ea		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_21	0xa2eb		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_22	0xa2ec		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_23	0xa2ed		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_PCIE_24	0xa2ee		/* 200 Series PCIE */
#define	PCI_PRODUCT_INTEL_200SERIES_HDA	0xa2f0		/* 200 Series HD Audio */
#define	PCI_PRODUCT_INTEL_H310_LPC	0xa303		/* H310 LPC */
#define	PCI_PRODUCT_INTEL_H370_LPC	0xa304		/* H370 LPC */
#define	PCI_PRODUCT_INTEL_Z390_LPC	0xa305		/* Z390 LPC */
#define	PCI_PRODUCT_INTEL_Q370_LPC	0xa306		/* Q370 LPC */
#define	PCI_PRODUCT_INTEL_B360_LPC	0xa308		/* B360 LPC */
#define	PCI_PRODUCT_INTEL_C246_LPC	0xa309		/* C246 LPC */
#define	PCI_PRODUCT_INTEL_C242_LPC	0xa30a		/* C242 LPC */
#define	PCI_PRODUCT_INTEL_QM370_LPC	0xa30c		/* QM370 LPC */
#define	PCI_PRODUCT_INTEL_HM370_LPC	0xa30d		/* HM370 LPC */
#define	PCI_PRODUCT_INTEL_CM246_LPC	0xa30e		/* CM246 LPC */
#define	PCI_PRODUCT_INTEL_300SERIES_P2SB	0xa320		/* 300 Series P2SB */
#define	PCI_PRODUCT_INTEL_300SERIES_PMC	0xa321		/* 300 Series PMC */
#define	PCI_PRODUCT_INTEL_300SERIES_SMB	0xa323		/* 300 Series SMBus */
#define	PCI_PRODUCT_INTEL_300SERIES_SPI	0xa324		/* 300 Series SPI */
#define	PCI_PRODUCT_INTEL_300SERIES_TH	0xa326		/* 300 Series TH */
#define	PCI_PRODUCT_INTEL_300SERIES_UART_1	0xa328		/* 300 Series UART */
#define	PCI_PRODUCT_INTEL_300SERIES_UART_2	0xa329		/* 300 Series UART */
#define	PCI_PRODUCT_INTEL_300SERIES_GSPI_1	0xa32a		/* 300 Series GSPI */
#define	PCI_PRODUCT_INTEL_300SERIES_GSPI_2	0xa32b		/* 300 Series GSPI */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_21	0xa32c		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_22	0xa32d		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_23	0xa32e		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_24	0xa32f		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_9	0xa330		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_10	0xa331		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_11	0xa332		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_12	0xa333		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_13	0xa334		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_14	0xa335		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_15	0xa336		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_16	0xa337		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_1	0xa338		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_2	0xa339		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_3	0xa33a		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_4	0xa33b		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_5	0xa33c		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_6	0xa33d		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_7	0xa33e		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_8	0xa33f		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_17	0xa340		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_18	0xa341		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_19	0xa342		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_PCIE_20	0xa343		/* 300 Series PCIE */
#define	PCI_PRODUCT_INTEL_300SERIES_UART_3	0xa347		/* 300 Series UART */
#define	PCI_PRODUCT_INTEL_300SERIES_CAVS	0xa348		/* 300 Series cAVS */
#define	PCI_PRODUCT_INTEL_300SERIES_AHCI_1	0xa352		/* 300 Series AHCI */
#define	PCI_PRODUCT_INTEL_300SERIES_AHCI_2	0xa353		/* 300 Series AHCI */
#define	PCI_PRODUCT_INTEL_300SERIES_RAID_1	0xa355		/* 300 Series RAID */
#define	PCI_PRODUCT_INTEL_300SERIES_RAID_2	0xa356		/* 300 Series RAID */
#define	PCI_PRODUCT_INTEL_300SERIES_RAID_3	0xa357		/* 300 Series RAID */
#define	PCI_PRODUCT_INTEL_300SERIES_HECI_1	0xa360		/* 300 Series HECI */
#define	PCI_PRODUCT_INTEL_300SERIES_HECI_2	0xa361		/* 300 Series HECI */
#define	PCI_PRODUCT_INTEL_300SERIES_IDER	0xa362		/* 300 Series IDER */
#define	PCI_PRODUCT_INTEL_300SERIES_KT	0xa363		/* 300 Series KT */
#define	PCI_PRODUCT_INTEL_300SERIES_HECI_3	0xa364		/* 300 Series HECI */
#define	PCI_PRODUCT_INTEL_300SERIES_HECI_4	0xa365		/* 300 Series HECI */
#define	PCI_PRODUCT_INTEL_300SERIES_I2C_1	0xa368		/* 300 Series I2C */
#define	PCI_PRODUCT_INTEL_300SERIES_I2C_2	0xa369		/* 300 Series I2C */
#define	PCI_PRODUCT_INTEL_300SERIES_I2C_3	0xa36a		/* 300 Series I2C */
#define	PCI_PRODUCT_INTEL_300SERIES_I2C_4	0xa36b		/* 300 Series I2C */
#define	PCI_PRODUCT_INTEL_300SERIES_XHCI	0xa36d		/* 300 Series xHCI */
#define	PCI_PRODUCT_INTEL_300SERIES_XDCI	0xa36e		/* 300 Series xDCI */
#define	PCI_PRODUCT_INTEL_300SERIES_SSRAM	0xa36f		/* 300 Series Shared SRAM */
#define	PCI_PRODUCT_INTEL_WL_9560_2	0xa370		/* AC 9560 */
#define	PCI_PRODUCT_INTEL_300SERIES_THERM	0xa379		/* 300 Series Thermal */
#define	PCI_PRODUCT_INTEL_300SERIES_GSPI_3	0xa37b		/* 300 Series GSPI */
#define	PCI_PRODUCT_INTEL_300SERIES_ISH	0xa37c		/* 300 Series ISH */
#define	PCI_PRODUCT_INTEL_400SERIES_V_AHCI	0xa382		/* 400 Series AHCI */
#define	PCI_PRODUCT_INTEL_400SERIES_V_RAID_1	0xa384		/* 400 Series RAID */
#define	PCI_PRODUCT_INTEL_400SERIES_V_RAID_2	0xa386		/* 400 Series RAID */
#define	PCI_PRODUCT_INTEL_400SERIES_V_OPTANE	0xa38e		/* 400 Series Optane */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_1	0xa390		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_2	0xa391		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_3	0xa392		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_4	0xa393		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_5	0xa394		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_6	0xa395		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_7	0xa396		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_8	0xa397		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_9	0xa398		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_10	0xa399		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_11	0xa39a		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_12	0xa39b		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_13	0xa39c		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_14	0xa39d		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_15	0xa39e		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_16	0xa39f		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_P2SB	0xa3a0		/* 400 Series P2SB */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PMC	0xa3a1		/* 400 Series PMC */
#define	PCI_PRODUCT_INTEL_400SERIES_V_SMB	0xa3a3		/* 400 Series SMBus */
#define	PCI_PRODUCT_INTEL_400SERIES_V_SPI_FLASH	0xa3a4		/* 400 Series SPI */
#define	PCI_PRODUCT_INTEL_400SERIES_V_TH	0xa3a6		/* 400 Series TH */
#define	PCI_PRODUCT_INTEL_400SERIES_V_UART_1	0xa3a7		/* 400 Series UART */
#define	PCI_PRODUCT_INTEL_400SERIES_V_UART_2	0xa3a8		/* 400 Series UART */
#define	PCI_PRODUCT_INTEL_400SERIES_V_SPI_1	0xa3a9		/* 400 Series SPI */
#define	PCI_PRODUCT_INTEL_400SERIES_V_SPI_2	0xa3aa		/* 400 Series SPI */
#define	PCI_PRODUCT_INTEL_400SERIES_V_XHCI	0xa3af		/* 400 Series xHCI */
#define	PCI_PRODUCT_INTEL_400SERIES_V_XDCI	0xa3b0		/* 400 Series xDCI */
#define	PCI_PRODUCT_INTEL_400SERIES_V_THERM	0xa3b1		/* 400 Series Thermal */
#define	PCI_PRODUCT_INTEL_400SERIES_V_ISH	0xa3b5		/* 400 Series ISH */
#define	PCI_PRODUCT_INTEL_400SERIES_V_HECI_1	0xa3ba		/* 400 Series HECI */
#define	PCI_PRODUCT_INTEL_400SERIES_V_HECI_2	0xa3bb		/* 400 Series HECI */
#define	PCI_PRODUCT_INTEL_400SERIES_V_IDER	0xa3bc		/* 400 Series IDE-R */
#define	PCI_PRODUCT_INTEL_400SERIES_V_KT	0xa3bd		/* 400 Series KT */
#define	PCI_PRODUCT_INTEL_400SERIES_V_HECI_3	0xa3be		/* 400 Series HECI */
#define	PCI_PRODUCT_INTEL_B460_LPC	0xa3c8		/* B460 LPC */
#define	PCI_PRODUCT_INTEL_H410_LPC	0xa3da		/* H410 LPC */
#define	PCI_PRODUCT_INTEL_400SERIES_V_I2C_1	0xa3e0		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_V_I2C_2	0xa3e1		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_V_I2C_3	0xa3e2		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_V_I2C_4	0xa3e3		/* 400 Series I2C */
#define	PCI_PRODUCT_INTEL_400SERIES_V_UART_3	0xa3e6		/* 400 Series UART */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_17	0xa3e7		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_18	0xa3e8		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_19	0xa3e9		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_20	0xa3ea		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_21	0xa3eb		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_22	0xa3ec		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_400SERIES_V_PCIE_23	0xa3ed		/* 400 Series PCIE */
#define	PCI_PRODUCT_INTEL_RPL_S_HB_2	0xa700		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_HX_HB_2	0xa702		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_S_HB_3	0xa703		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_S_HB_4	0xa704		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_S_HB_5	0xa705		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_H_HB_1	0xa706		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_H_HB_2	0xa707		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_U_HB_1	0xa708		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_PX_HB_1	0xa709		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_PX_HB_2	0xa70a		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_PCIE_1	0xa70d		/* Core 13G PCIE */
#define	PCI_PRODUCT_INTEL_RPL_E_HB_1	0xa711		/* Xeon E-2400 Host */
#define	PCI_PRODUCT_INTEL_RPL_E_HB_2	0xa712		/* Xeon E-2400 Host */
#define	PCI_PRODUCT_INTEL_RPL_E_HB_3	0xa713		/* Xeon E-2400 Host */
#define	PCI_PRODUCT_INTEL_RPL_H_HB_3	0xa716		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_HX_HB_3	0xa719		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_U_HB_2	0xa71b		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_U_HB_3	0xa71c		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_DTT	0xa71d		/* Core 13G DTT */
#define	PCI_PRODUCT_INTEL_RPL_XHCI	0xa71e		/* Core 13G xHCI */
#define	PCI_PRODUCT_INTEL_RPL_TBT_PCIE3	0xa71f		/* Core 13G PCIE */
#define	PCI_PRODUCT_INTEL_RPL_P_GT_1	0xa720		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_U_GT_1	0xa721		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_HX_HB_4	0xa728		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_HX_HB_5	0xa729		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_HX_HB_6	0xa72a		/* Core 13G Host */
#define	PCI_PRODUCT_INTEL_RPL_PCIE_2	0xa72d		/* Core 13G PCIE */
#define	PCI_PRODUCT_INTEL_RPL_TBT_PCIE2	0xa72f		/* Core 13G PCIE */
#define	PCI_PRODUCT_INTEL_RPL_TBT_DMA0	0xa73e		/* Core 13G TBT */
#define	PCI_PRODUCT_INTEL_RPL_TBT_PCIE1	0xa73f		/* Core 13G PCIE */
#define	PCI_PRODUCT_INTEL_RPL_S_HB_6	0xa740		/* Core 14G Host */
#define	PCI_PRODUCT_INTEL_RPL_PCIE_3	0xa74d		/* Core 13G PCIE */
#define	PCI_PRODUCT_INTEL_RPL_GNA	0xa74f		/* Core 13G GNA */
#define	PCI_PRODUCT_INTEL_RPL_IPU	0xa75d		/* Core 13G IPU */
#define	PCI_PRODUCT_INTEL_RPL_TBT_DMA1	0xa76d		/* Core 13G TBT */
#define	PCI_PRODUCT_INTEL_RPL_TBT_PCIE0	0xa76e		/* Core 13G PCIE */
#define	PCI_PRODUCT_INTEL_RPL_TH	0xa76f		/* Core 13G TH */
#define	PCI_PRODUCT_INTEL_RPL_CL	0xa77d		/* Core 13G CL */
#define	PCI_PRODUCT_INTEL_RPL_VMD	0xa77f		/* Core 13G VMD */
#define	PCI_PRODUCT_INTEL_RPL_S_GT_1	0xa780		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_S_GT_2	0xa781		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_S_GT_3	0xa782		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_S_GT_4	0xa783		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_S_GT_5	0xa788		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_S_GT_6	0xa789		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_S_GT_7	0xa78a		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_S_GT_8	0xa78b		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_P_GT_2	0xa7a0		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_U_GT_2	0xa7a1		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_P_GT_3	0xa7a8		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_U_GT_3	0xa7a9		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_P_GT_4	0xa7aa		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_P_GT_5	0xa7ab		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_U_GT_4	0xa7ac		/* Graphics */
#define	PCI_PRODUCT_INTEL_RPL_U_GT_5	0xa7ad		/* Graphics */
#define	PCI_PRODUCT_INTEL_LNL_ESPI	0xa807		/* Core Ultra eSPI */
#define	PCI_PRODUCT_INTEL_LNL_P2SB_1	0xa820		/* Core Ultra P2SB */
#define	PCI_PRODUCT_INTEL_LNL_PMC	0xa821		/* Core Ultra PMC */
#define	PCI_PRODUCT_INTEL_LNL_SMB	0xa822		/* Core Ultra SMBus */
#define	PCI_PRODUCT_INTEL_LNL_SPI	0xa823		/* Core Ultra SPI */
#define	PCI_PRODUCT_INTEL_LNL_TH	0xa824		/* Core Ultra TH */
#define	PCI_PRODUCT_INTEL_LNL_UART_0	0xa825		/* Core Ultra UART */
#define	PCI_PRODUCT_INTEL_LNL_UART_1	0xa826		/* Core Ultra UART */
#define	PCI_PRODUCT_INTEL_LNL_GSPI_0	0xa827		/* Core Ultra GSPI */
#define	PCI_PRODUCT_INTEL_LNL_HDA	0xa828		/* Core Ultra HD Audio */
#define	PCI_PRODUCT_INTEL_LNL_GSPI_1	0xa830		/* Core Ultra GSPI */
#define	PCI_PRODUCT_INTEL_LNL_TC_XHCI	0xa831		/* Core Ultra xHCI */
#define	PCI_PRODUCT_INTEL_LNL_TBT_DMA0	0xa833		/* Core Ultra TBT */
#define	PCI_PRODUCT_INTEL_LNL_TBT_DMA1	0xa834		/* Core Ultra TBT */
#define	PCI_PRODUCT_INTEL_LNL_PCIE_1	0xa838		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_LNL_PCIE_2	0xa839		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_LNL_PCIE_3	0xa83a		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_LNL_PCIE_4	0xa83b		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_LNL_PCIE_5	0xa83c		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_LNL_PCIE_6	0xa83d		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_LNL_ISH	0xa845		/* Core Ultra ISH */
#define	PCI_PRODUCT_INTEL_LNL_GSPI_2	0xa846		/* Core Ultra GSPI */
#define	PCI_PRODUCT_INTEL_LNL_THC_0_1	0xa848		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_LNL_THC_0_2	0xa849		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_LNL_THC_1_1	0xa84a		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_LNL_THC_1_2	0xa84b		/* Core Ultra THC */
#define	PCI_PRODUCT_INTEL_LNL_P2SB_2	0xa84c		/* Core Ultra P2SB */
#define	PCI_PRODUCT_INTEL_LNL_TC_PCIE_21	0xa84e		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_LNL_TC_PCIE_22	0xa84f		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_LNL_I2C_4	0xa850		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_LNL_I2C_5	0xa851		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_LNL_UART_2	0xa852		/* Core Ultra UART */
#define	PCI_PRODUCT_INTEL_LNL_HECI_4	0xa85d		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_LNL_HECI_5	0xa85e		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_LNL_HECI_6	0xa85f		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_LNL_TC_PCIE_23	0xa860		/* Core Ultra PCIE */
#define	PCI_PRODUCT_INTEL_LNL_HECI_1	0xa862		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_LNL_HECI_2	0xa863		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_LNL_HECI_3	0xa864		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_LNL_CSE_HECI_1	0xa870		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_LNL_CSE_HECI_2	0xa871		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_LNL_IDER	0xa872		/* Core Ultra IDE-R */
#define	PCI_PRODUCT_INTEL_LNL_KT	0xa873		/* Core Ultra KT */
#define	PCI_PRODUCT_INTEL_LNL_CSE_HECI_3	0xa874		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_LNL_CSE_HECI_4	0xa875		/* Core Ultra HECI */
#define	PCI_PRODUCT_INTEL_LNL_I3C_2	0xa877		/* Core Ultra I3C */
#define	PCI_PRODUCT_INTEL_LNL_I2C_0	0xa878		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_LNL_I2C_1	0xa879		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_LNL_I2C_2	0xa87a		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_LNL_I2C_3	0xa87b		/* Core Ultra I2C */
#define	PCI_PRODUCT_INTEL_LNL_I3C_1	0xa87c		/* Core Ultra I3C */
#define	PCI_PRODUCT_INTEL_LNL_XHCI	0xa87d		/* Core Ultra xHCI */
#define	PCI_PRODUCT_INTEL_LNL_SRAM	0xa87f		/* Core Ultra SRAM */
#define	PCI_PRODUCT_INTEL_21152	0xb152		/* S21152BB */
#define	PCI_PRODUCT_INTEL_21154	0xb154		/* 21154AE/BE */
#define	PCI_PRODUCT_INTEL_ARL_S_GT_2	0xb640		/* Graphics */
#define	PCI_PRODUCT_INTEL_CORE_DMI_0	0xd130		/* Core DMI */
#define	PCI_PRODUCT_INTEL_CORE_DMI_1	0xd131		/* Core DMI */
#define	PCI_PRODUCT_INTEL_CORE_DMI_2	0xd132		/* Core DMI */
#define	PCI_PRODUCT_INTEL_CORE_PCIE_2	0xd138		/* Core PCIE */
#define	PCI_PRODUCT_INTEL_CORE_PCIE_3	0xd139		/* Core PCIE */
#define	PCI_PRODUCT_INTEL_CORE_PCIE_4	0xd13a		/* Core PCIE */
#define	PCI_PRODUCT_INTEL_CORE_PCIE_5	0xd13b		/* Core PCIE */
#define	PCI_PRODUCT_INTEL_CORE_QPI_L	0xd150		/* Core QPI Link */
#define	PCI_PRODUCT_INTEL_CORE_QPI_R	0xd151		/* Core QPI Routing */
#define	PCI_PRODUCT_INTEL_CORE_DMI_3	0xd152		/* Core DMI */
#define	PCI_PRODUCT_INTEL_CORE_DMI_4	0xd153		/* Core DMI */
#define	PCI_PRODUCT_INTEL_CORE_DMI_5	0xd154		/* Core DMI */
#define	PCI_PRODUCT_INTEL_CORE_MANAGEMENT	0xd155		/* Core Management */
#define	PCI_PRODUCT_INTEL_CORE_SCRATCH	0xd156		/* Core Scratch */
#define	PCI_PRODUCT_INTEL_CORE_CONTROL	0xd157		/* Core Control */
#define	PCI_PRODUCT_INTEL_CORE_MISC	0xd158		/* Core Misc */
#define	PCI_PRODUCT_INTEL_500SERIES_HDA_2	0xf0c8		/* 500 Series HD Audio */
#define	PCI_PRODUCT_INTEL_NVME_2	0xf1a5		/* NVMe */
#define	PCI_PRODUCT_INTEL_NVME_3	0xf1a6		/* NVMe */
#define	PCI_PRODUCT_INTEL_NVME_4	0xf1a8		/* NVMe */

/* Intergraph products */
#define	PCI_PRODUCT_INTERGRAPH_4D50T	0x00e4		/* Powerstorm 4D50T */
#define	PCI_PRODUCT_INTERGRAPH_INTENSE3D	0x00eb		/* Intense 3D */
#define	PCI_PRODUCT_INTERGRAPH_EXPERT3D	0x07a0		/* Expert3D */

/* Interphase products */
#define	PCI_PRODUCT_INTERPHASE_5526	0x0004		/* 5526 FibreChannel */

/* Intersil products */
#define	PCI_PRODUCT_INTERSIL_ISL3872	0x3872		/* PRISM3 */
#define	PCI_PRODUCT_INTERSIL_MINI_PCI_WLAN	0x3873		/* PRISM2.5 */
#define	PCI_PRODUCT_INTERSIL_ISL3877	0x3877		/* Prism Indigo */
#define	PCI_PRODUCT_INTERSIL_ISL3886	0x3886		/* Prism Javelin/Xbow */
#define	PCI_PRODUCT_INTERSIL_ISL3890	0x3890		/* Prism GT/Duette */

/* Invertex */
#define	PCI_PRODUCT_INVERTEX_AEON	0x0005		/* AEON */

/* IO Data Device Inc products */
#define	PCI_PRODUCT_IODATA_GV_BCTV3	0x4020		/* GV-BCTV3 */

/* ITExpress */
#define	PCI_PRODUCT_ITEXPRESS_IT8211F	0x8211		/* IT8211F */
#define	PCI_PRODUCT_ITEXPRESS_IT8212F	0x8212		/* IT8212F */
#define	PCI_PRODUCT_ITEXPRESS_IT8213F	0x8213		/* IT8213F */
#define	PCI_PRODUCT_ITEXPRESS_IT8330G	0x8330		/* IT8330G */
#define	PCI_PRODUCT_ITEXPRESS_IT8888F_ISA	0x8888		/* IT8888F ISA */
#define	PCI_PRODUCT_ITEXPRESS_IT8892E	0x8892		/* IT8892E PCIE-PCI */
#define	PCI_PRODUCT_ITEXPRESS_IT8893E	0x8893		/* IT8893E PCIE-PCI */

/* ITT products */
#define	PCI_PRODUCT_ITT_AGX016	0x0001		/* AGX016 */
#define	PCI_PRODUCT_ITT_ITT3204	0x0002		/* ITT3204 MPEG Decoder */

/* JMicron */
#define	PCI_PRODUCT_JMICRON_JMC250	0x0250		/* JMC250 */
#define	PCI_PRODUCT_JMICRON_JMC260	0x0260		/* JMC260 */
#define	PCI_PRODUCT_JMICRON_JMB58X	0x0585		/* JMB58x AHCI */
#define	PCI_PRODUCT_JMICRON_JMB360	0x2360		/* JMB360 SATA */
#define	PCI_PRODUCT_JMICRON_JMB361	0x2361		/* JMB361 IDE/SATA */
#define	PCI_PRODUCT_JMICRON_JMB362	0x2362		/* JMB362 SATA */
#define	PCI_PRODUCT_JMICRON_JMB363	0x2363		/* JMB363 IDE/SATA */
#define	PCI_PRODUCT_JMICRON_JMB365	0x2365		/* JMB365 IDE/SATA */
#define	PCI_PRODUCT_JMICRON_JMB366	0x2366		/* JMB366 IDE/SATA */
#define	PCI_PRODUCT_JMICRON_JMB368	0x2368		/* JMB368 IDE */
#define	PCI_PRODUCT_JMICRON_FIREWIRE	0x2380		/* FireWire */
#define	PCI_PRODUCT_JMICRON_SD	0x2381		/* SD Host Controller */
#define	PCI_PRODUCT_JMICRON_SDMMC	0x2382		/* SD/MMC */
#define	PCI_PRODUCT_JMICRON_MS	0x2383		/* Memory Stick */
#define	PCI_PRODUCT_JMICRON_XD	0x2384		/* xD */
#define	PCI_PRODUCT_JMICRON_SD_3	0x2386		/* SD Host Controller */
#define	PCI_PRODUCT_JMICRON_SDMMC_3	0x2387		/* SD/MMC */
#define	PCI_PRODUCT_JMICRON_MS_3	0x2388		/* Memory Stick */
#define	PCI_PRODUCT_JMICRON_XD_3	0x2389		/* xD */
#define	PCI_PRODUCT_JMICRON_SD_2	0x2391		/* SD Host Controller */
#define	PCI_PRODUCT_JMICRON_SDMMC_2	0x2392		/* SD/MMC */
#define	PCI_PRODUCT_JMICRON_MS_2	0x2393		/* Memory Stick */
#define	PCI_PRODUCT_JMICRON_XD_2	0x2394		/* xD */

/* Kingston */
#define	PCI_PRODUCT_KINGSTON_A2000	0x2263		/* A2000 */
#define	PCI_PRODUCT_KINGSTON_KC3000	0x5013		/* KC3000 */
#define	PCI_PRODUCT_KINGSTON_SNV2S	0x5017		/* SNV2S */
#define	PCI_PRODUCT_KINGSTON_NV2	0x5019		/* NV2 */

/* Kioxia */
#define	PCI_PRODUCT_KIOXIA_BG4	0x0001		/* BG4 */

/* KTI */
#define	PCI_PRODUCT_KTI_KTIE	0x3000		/* KTI */

/* Lanergy */
#define	PCI_PRODUCT_LANERGY_APPIAN_PCI_LITE	0x0001		/* Appian Lite */

/* Lava */
#define	PCI_PRODUCT_LAVA_TWOSP_2S	0x0100		/* Dual Serial */
#define	PCI_PRODUCT_LAVA_QUATTRO_AB	0x0101		/* Dual Serial */
#define	PCI_PRODUCT_LAVA_QUATTRO_CD	0x0102		/* Dual Serial */
#define	PCI_PRODUCT_LAVA_IOFLEX_2S_0	0x0110		/* Serial */
#define	PCI_PRODUCT_LAVA_IOFLEX_2S_1	0x0111		/* Serial */
#define	PCI_PRODUCT_LAVA_QUATTRO_AB2	0x0120		/* Dual Serial */
#define	PCI_PRODUCT_LAVA_QUATTRO_CD2	0x0121		/* Dual Serial */
#define	PCI_PRODUCT_LAVA_OCTOPUS550_0	0x0180		/* Quad Serial */
#define	PCI_PRODUCT_LAVA_OCTOPUS550_1	0x0181		/* Quad Serial */
#define	PCI_PRODUCT_LAVA_LAVAPORT_2	0x0200		/* Serial */
#define	PCI_PRODUCT_LAVA_LAVAPORT_0	0x0201		/* Serial */
#define	PCI_PRODUCT_LAVA_LAVAPORT_1	0x0202		/* Serial */
#define	PCI_PRODUCT_LAVA_650	0x0600		/* Serial */
#define	PCI_PRODUCT_LAVA_TWOSP_1P	0x8000		/* Parallel */
#define	PCI_PRODUCT_LAVA_PARALLEL2	0x8001		/* Dual Parallel */
#define	PCI_PRODUCT_LAVA_PARALLEL2A	0x8002		/* Dual Parallel */
#define	PCI_PRODUCT_LAVA_PARALLELB	0x8003		/* Dual Parallel */

/* LeadTek Research */
#define	PCI_PRODUCT_LEADTEK_S3_805	0x0000		/* S3 805 */
#define	PCI_PRODUCT_LEADTEK_WINFAST	0x6606		/* Leadtek WinFast TV 2000 */
#define	PCI_PRODUCT_LEADTEK_WINFAST_XP	0x6609		/* Leadtek WinFast TV 2000 XP */

/* Lenovo products */
#define	PCI_PRODUCT_LENOVO_NVME	0x0003		/* NVMe */
#define	PCI_PRODUCT_LENOVO_NVME_2	0x0006		/* NVMe */

/* Level 1 (Intel) */
#define	PCI_PRODUCT_LEVEL1_LXT1001	0x0001		/* LXT1001 */

/* Linksys products */
#define	PCI_PRODUCT_LINKSYS_EG1032	0x1032		/* EG1032 */
#define	PCI_PRODUCT_LINKSYS_EG1064	0x1064		/* EG1064 */
#define	PCI_PRODUCT_LINKSYS_PCMPC200	0xab08		/* PCMPC200 */
#define	PCI_PRODUCT_LINKSYS_PCM200	0xab09		/* PCM200 */

/* Lite-On Communications */
#define	PCI_PRODUCT_LITEON_PNIC	0x0002		/* PNIC */
#define	PCI_PRODUCT_LITEON_PNICII	0xc115		/* PNIC-II */
#define	PCI_PRODUCT_LITEON2_CB1	0x5100		/* CB1 NVMe */

/* LAN Media Corporation */
#define	PCI_PRODUCT_LMC_HSSI	0x0003		/* HSSI */
#define	PCI_PRODUCT_LMC_DS3	0x0004		/* DS3 */
#define	PCI_PRODUCT_LMC_SSI	0x0005		/* SSI */
#define	PCI_PRODUCT_LMC_DS1	0x0006		/* DS1 */
#define	PCI_PRODUCT_LMC_HSSIC	0x0007		/* HSSIc */

/* Longsys products */
#define	PCI_PRODUCT_LONGSYS_FORESEE_XP1000	0x5216		/* FORESEE XP1000 */

/* Lucent products */
#define	PCI_PRODUCT_LUCENT_LTMODEM	0x0440		/* K56flex DSVD LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0441	0x0441		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0442	0x0442		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0443	0x0443		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0444	0x0444		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0445	0x0445		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0446	0x0446		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0447	0x0447		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0448	0x0448		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0449	0x0449		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_044A	0x044a		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_044B	0x044b		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_044C	0x044c		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_044D	0x044d		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_044E	0x044e		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0450	0x0450		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0451	0x0451		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0452	0x0452		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0453	0x0453		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0454	0x0454		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0455	0x0455		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0456	0x0456		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0457	0x0457		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0458	0x0458		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_0459	0x0459		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_045A	0x045a		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_LTMODEM_045C	0x045c		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_VENUSMODEM	0x0480		/* Venus Modem */
#define	PCI_PRODUCT_LUCENT_LTMODEM_048C	0x048c		/* LTMODEM */
#define	PCI_PRODUCT_LUCENT_USBHC	0x5801		/* USB */
#define	PCI_PRODUCT_LUCENT_USBHC2	0x5802		/* USB 2-port */
#define	PCI_PRODUCT_LUCENT_USBQBUS	0x5803		/* USB QuadraBus */
#define	PCI_PRODUCT_LUCENT_FW322	0x5811		/* FW322 1394 */
#define	PCI_PRODUCT_LUCENT_FW643	0x5901		/* FW643 1394 */
#define	PCI_PRODUCT_LUCENT_ET1310_GBE	0xed00		/* ET1310 */
#define	PCI_PRODUCT_LUCENT_ET1310_FE	0xed01		/* ET1310 */

/* LuxSonor */
#define	PCI_PRODUCT_LUXSONOR_LS242	0x0020		/* LS242 DVD Decoder */

/* Macronix */
#define	PCI_PRODUCT_MACRONIX_MX98713	0x0512		/* PMAC 98713 */
#define	PCI_PRODUCT_MACRONIX_MX98715	0x0531		/* PMAC 98715 */
#define	PCI_PRODUCT_MACRONIX_MX98727	0x0532		/* PMAC 98727 */
#define	PCI_PRODUCT_MACRONIX_MX86250	0x8625		/* MX86250 */

/* Madge Networks products */
#define	PCI_PRODUCT_MADGE_SMARTRN	0x0001		/* Smart 16/4 PCI Ringnode */
#define	PCI_PRODUCT_MADGE_SMARTRN2	0x0002		/* Smart 16/4 PCI Ringnode Mk2 */
#define	PCI_PRODUCT_MADGE_SMARTRN3	0x0003		/* Smart 16/4 PCI Ringnode Mk3 */
#define	PCI_PRODUCT_MADGE_SMARTRN1	0x0004		/* Smart 16/4 PCI Ringnode Mk1 */
#define	PCI_PRODUCT_MADGE_164CB	0x0006		/* 16/4 Cardbus */
#define	PCI_PRODUCT_MADGE_PRESTO	0x0007		/* Presto PCI */
#define	PCI_PRODUCT_MADGE_SMARTHSRN100	0x0009		/* Smart 100/16/4 PCI-HS Ringnode */
#define	PCI_PRODUCT_MADGE_SMARTRN100	0x000a		/* Smart 100/16/4 PCI Ringnode */
#define	PCI_PRODUCT_MADGE_164CB2	0x000b		/* 16/4 CardBus Mk2 */
#define	PCI_PRODUCT_MADGE_COLLAGE25	0x1000		/* Collage 25 ATM */
#define	PCI_PRODUCT_MADGE_COLLAGE155	0x1001		/* Collage 155 ATM */

/* Martin-Marietta */
#define	PCI_PRODUCT_MARTINMARIETTA_I740	0x00d1		/* i740 PCI */

/* Marvell products */
#define	PCI_PRODUCT_MARVELL_ARMADA_3700	0x0100		/* ARMADA 3700 PCIE */
#define	PCI_PRODUCT_MARVELL_ARMADA_CP110_RC	0x0110		/* ARMADA 7K/8K Root Complex */
#define	PCI_PRODUCT_MARVELL_88W8300_1	0x1fa6		/* Libertas 88W8300 */
#define	PCI_PRODUCT_MARVELL_88W8310	0x1fa7		/* Libertas 88W8310 */
#define	PCI_PRODUCT_MARVELL_88W8335_1	0x1faa		/* Libertas 88W8335 */
#define	PCI_PRODUCT_MARVELL_88W8335_2	0x1fab		/* Libertas 88W8335 */
#define	PCI_PRODUCT_MARVELL_88W8300_2	0x2a01		/* Libertas 88W8300 */
#define	PCI_PRODUCT_MARVELL_88W8897	0x2b38		/* 88W8897 802.11ac */
#define	PCI_PRODUCT_MARVELL_YUKON	0x4320		/* Yukon 88E8001/8003/8010 */
#define	PCI_PRODUCT_MARVELL_YUKON_8021CU	0x4340		/* Yukon 88E8021CU */
#define	PCI_PRODUCT_MARVELL_YUKON_8022CU	0x4341		/* Yukon 88E8022CU */
#define	PCI_PRODUCT_MARVELL_YUKON_8061CU	0x4342		/* Yukon 88E8061CU */
#define	PCI_PRODUCT_MARVELL_YUKON_8062CU	0x4343		/* Yukon 88E8062CU */
#define	PCI_PRODUCT_MARVELL_YUKON_8021X	0x4344		/* Yukon 88E8021X */
#define	PCI_PRODUCT_MARVELL_YUKON_8022X	0x4345		/* Yukon 88E8022X */
#define	PCI_PRODUCT_MARVELL_YUKON_8061X	0x4346		/* Yukon 88E8061X */
#define	PCI_PRODUCT_MARVELL_YUKON_8062X	0x4347		/* Yukon 88E8062X */
#define	PCI_PRODUCT_MARVELL_YUKON_8035	0x4350		/* Yukon 88E8035 */
#define	PCI_PRODUCT_MARVELL_YUKON_8036	0x4351		/* Yukon 88E8036 */
#define	PCI_PRODUCT_MARVELL_YUKON_8038	0x4352		/* Yukon 88E8038 */
#define	PCI_PRODUCT_MARVELL_YUKON_8039	0x4353		/* Yukon 88E8039 */
#define	PCI_PRODUCT_MARVELL_YUKON_8040	0x4354		/* Yukon 88E8040 */
#define	PCI_PRODUCT_MARVELL_YUKON_8040T	0x4355		/* Yukon 88E8040T */
#define	PCI_PRODUCT_MARVELL_YUKON_C033	0x4356		/* Yukon 88EC033 */
#define	PCI_PRODUCT_MARVELL_YUKON_8042	0x4357		/* Yukon 88E8042 */
#define	PCI_PRODUCT_MARVELL_YUKON_8048	0x435a		/* Yukon 88E8048 */
#define	PCI_PRODUCT_MARVELL_YUKON_8052	0x4360		/* Yukon 88E8052 */
#define	PCI_PRODUCT_MARVELL_YUKON_8050	0x4361		/* Yukon 88E8050 */
#define	PCI_PRODUCT_MARVELL_YUKON_8053	0x4362		/* Yukon 88E8053 */
#define	PCI_PRODUCT_MARVELL_YUKON_8055	0x4363		/* Yukon 88E8055 */
#define	PCI_PRODUCT_MARVELL_YUKON_8056	0x4364		/* Yukon 88E8056 */
#define	PCI_PRODUCT_MARVELL_YUKON_8070	0x4365		/* Yukon 88E8070 */
#define	PCI_PRODUCT_MARVELL_YUKON_C036	0x4366		/* Yukon 88EC036 */
#define	PCI_PRODUCT_MARVELL_YUKON_C032	0x4367		/* Yukon 88EC032 */
#define	PCI_PRODUCT_MARVELL_YUKON_C034	0x4368		/* Yukon 88EC034 */
#define	PCI_PRODUCT_MARVELL_YUKON_C042	0x4369		/* Yukon 88EC042 */
#define	PCI_PRODUCT_MARVELL_YUKON_8058	0x436a		/* Yukon 88E8058 */
#define	PCI_PRODUCT_MARVELL_YUKON_8071	0x436b		/* Yukon 88E8071 */
#define	PCI_PRODUCT_MARVELL_YUKON_8072	0x436c		/* Yukon 88E8072 */
#define	PCI_PRODUCT_MARVELL_YUKON_8055_2	0x436d		/* Yukon 88E8055 */
#define	PCI_PRODUCT_MARVELL_YUKON_8075	0x4370		/* Yukon 88E8075 */
#define	PCI_PRODUCT_MARVELL_YUKON_8057	0x4380		/* Yukon 88E8057 */
#define	PCI_PRODUCT_MARVELL_YUKON_8059	0x4381		/* Yukon 88E8059 */
#define	PCI_PRODUCT_MARVELL_YUKON_8079	0x4382		/* Yukon 88E8079 */
#define	PCI_PRODUCT_MARVELL_YUKON_BELKIN	0x5005		/* Yukon (Belkin F5D5005) */
#define	PCI_PRODUCT_MARVELL_88SX5040	0x5040		/* 88SX5040 SATA */
#define	PCI_PRODUCT_MARVELL_88SX5041	0x5041		/* 88SX5041 SATA */
#define	PCI_PRODUCT_MARVELL_88SX5080	0x5080		/* 88SX5080 SATA */
#define	PCI_PRODUCT_MARVELL_88SX5081	0x5081		/* 88SX5081 SATA */
#define	PCI_PRODUCT_MARVELL_88SX6040	0x6040		/* 88SX6040 SATA */
#define	PCI_PRODUCT_MARVELL_88SX6041	0x6041		/* 88SX6041 SATA */
#define	PCI_PRODUCT_MARVELL_88SX6042	0x6042		/* 88SX6042 SATA */
#define	PCI_PRODUCT_MARVELL_88SX6080	0x6080		/* 88SX6080 SATA */
#define	PCI_PRODUCT_MARVELL_88SX6081	0x6081		/* 88SX6081 SATA */
#define	PCI_PRODUCT_MARVELL_88SE6101	0x6101		/* 88SE6101 IDE */
#define	PCI_PRODUCT_MARVELL_88SE6111	0x6111		/* 88SE6111 SATA */
#define	PCI_PRODUCT_MARVELL_88SE6120	0x6120		/* 88SE6120 SATA */
#define	PCI_PRODUCT_MARVELL_88SE6121	0x6121		/* 88SE6121 SATA */
#define	PCI_PRODUCT_MARVELL_88SE6122	0x6122		/* 88SE6122 SATA */
#define	PCI_PRODUCT_MARVELL_88SE6140	0x6140		/* 88SE6140 SATA */
#define	PCI_PRODUCT_MARVELL_88SE6141	0x6141		/* 88SE6141 SATA */
#define	PCI_PRODUCT_MARVELL_88SE6145	0x6145		/* 88SE6145 SATA */
#define	PCI_PRODUCT_MARVELL_88SX7042	0x7042		/* 88SX7042 SATA */
#define	PCI_PRODUCT_MARVELL2_88SE9120	0x9120		/* 88SE9120 SATA */
#define	PCI_PRODUCT_MARVELL2_88SE9123	0x9123		/* 88SE9123 SATA */
#define	PCI_PRODUCT_MARVELL2_88SE9125	0x9125		/* 88SE9125 SATA */
#define	PCI_PRODUCT_MARVELL2_88SE9128	0x9128		/* 88SE9128 AHCI */
#define	PCI_PRODUCT_MARVELL2_88SE9172	0x9172		/* 88SE9172 SATA */
#define	PCI_PRODUCT_MARVELL2_88SE9215	0x9215		/* 88SE9215 AHCI */
#define	PCI_PRODUCT_MARVELL2_88SE9230	0x9230		/* 88SE9230 AHCI */
#define	PCI_PRODUCT_MARVELL2_88SE9235	0x9235		/* 88SE9235 AHCI */

/* Matrox products */
#define	PCI_PRODUCT_MATROX_ATLAS	0x0518		/* MGA PX2085 */
#define	PCI_PRODUCT_MATROX_MILLENIUM	0x0519		/* MGA Millenium 2064W */
#define	PCI_PRODUCT_MATROX_MYSTIQUE_220	0x051a		/* MGA 1064SG 220MHz */
#define	PCI_PRODUCT_MATROX_MILLENNIUM_II	0x051b		/* MGA Millennium II 2164W */
#define	PCI_PRODUCT_MATROX_MILLENNIUM_IIAGP	0x051f		/* MGA Millennium II 2164WA-B AGP */
#define	PCI_PRODUCT_MATROX_MILL_II_G200_PCI	0x0520		/* MGA G200 PCI */
#define	PCI_PRODUCT_MATROX_MILL_II_G200_AGP	0x0521		/* MGA G200 AGP */
#define	PCI_PRODUCT_MATROX_G200E_SE	0x0522		/* MGA G200e */
#define	PCI_PRODUCT_MATROX_G200E_SE_B	0x0524		/* MGA G200e */
#define	PCI_PRODUCT_MATROX_MILL_II_G400_AGP	0x0525		/* MGA G400/G450 AGP */
#define	PCI_PRODUCT_MATROX_G200EV	0x0530		/* MGA G200eV */
#define	PCI_PRODUCT_MATROX_G200EW	0x0532		/* MGA G200eW */
#define	PCI_PRODUCT_MATROX_G200EH	0x0533		/* MGA G200eH */
#define	PCI_PRODUCT_MATROX_G200ER	0x0534		/* MGA G200eR */
#define	PCI_PRODUCT_MATROX_IMPRESSION	0x0d10		/* MGA Impression */
#define	PCI_PRODUCT_MATROX_PRODUCTIVA_PCI	0x1000		/* MGA G100 PCI */
#define	PCI_PRODUCT_MATROX_PRODUCTIVA_AGP	0x1001		/* MGA G100 AGP */
#define	PCI_PRODUCT_MATROX_MYSTIQUE	0x102b		/* MGA 1064SG */
#define	PCI_PRODUCT_MATROX_G400_TH	0x2179		/* MGA G400 Twin Head */
#define	PCI_PRODUCT_MATROX_MILL_II_G550_AGP	0x2527		/* MGA G550 AGP */
#define	PCI_PRODUCT_MATROX_MILL_P650_PCIE	0x2538		/* MGA P650 PCIe */
#define	PCI_PRODUCT_MATROX_MILL_G200_SD	0xff00		/* MGA Millennium G200 SD */
#define	PCI_PRODUCT_MATROX_PROD_G100_SD	0xff01		/* MGA Produktiva G100 SD */
#define	PCI_PRODUCT_MATROX_MYST_G200_SD	0xff02		/* MGA Mystique G200 SD */
#define	PCI_PRODUCT_MATROX_MILL_G200_SG	0xff03		/* MGA Millennium G200 SG */
#define	PCI_PRODUCT_MATROX_MARV_G200_SD	0xff04		/* MGA Marvel G200 SD */

/* MediaTek products */
#define	PCI_PRODUCT_MEDIATEK_MT7921K	0x0608		/* MT7921K */
#define	PCI_PRODUCT_MEDIATEK_MT7922	0x0616		/* MT7922 */
#define	PCI_PRODUCT_MEDIATEK_MT7921	0x7961		/* MT7921 */

/* Meinberg Funkuhren */
#define	PCI_PRODUCT_MEINBERG_PCI32	0x0101		/* PCI32 */
#define	PCI_PRODUCT_MEINBERG_PCI509	0x0102		/* PCI509 */
#define	PCI_PRODUCT_MEINBERG_PCI510	0x0103		/* PCI510 */
#define	PCI_PRODUCT_MEINBERG_PCI511	0x0104		/* PCI511 */
#define	PCI_PRODUCT_MEINBERG_PEX511	0x0105		/* PEX511 */
#define	PCI_PRODUCT_MEINBERG_PZF180PEX	0x0106		/* PZF180PEX */
#define	PCI_PRODUCT_MEINBERG_GPS170PCI	0x0204		/* GPS170PCI */

/* Mellanox */
#define	PCI_PRODUCT_MELLANOX_MT27700	0x1013		/* ConnectX-4 */
#define	PCI_PRODUCT_MELLANOX_MT27700VF	0x1014		/* ConnectX-4 VF */
#define	PCI_PRODUCT_MELLANOX_MT27710	0x1015		/* ConnectX-4 Lx */
#define	PCI_PRODUCT_MELLANOX_MT27710VF	0x1016		/* ConnectX-4 Lx VF */
#define	PCI_PRODUCT_MELLANOX_MT27800	0x1017		/* ConnectX-5 */
#define	PCI_PRODUCT_MELLANOX_MT27800VF	0x1018		/* ConnectX-5 VF */
#define	PCI_PRODUCT_MELLANOX_MT28800	0x1019		/* ConnectX-5 Ex */
#define	PCI_PRODUCT_MELLANOX_MT28800VF	0x101a		/* ConnectX-5 Ex VF */
#define	PCI_PRODUCT_MELLANOX_MT28908	0x101b		/* ConnectX-6 */
#define	PCI_PRODUCT_MELLANOX_MT28908VF	0x101c		/* ConnectX-6 VF */
#define	PCI_PRODUCT_MELLANOX_MT2892	0x101d		/* ConnectX-6 Dx */
#define	PCI_PRODUCT_MELLANOX_MT2894	0x101f		/* ConnectX-6 Lx */
#define	PCI_PRODUCT_MELLANOX_CONNECTX_EN	0x6368		/* ConnectX EN */

/* Mentor */
#define	PCI_PRODUCT_MENTOR_PCI0660	0x0660		/* PCI */
#define	PCI_PRODUCT_MENTOR_PCI0661	0x0661		/* PCI-PCI */

/* Micrel products */
#define	PCI_PRODUCT_MICREL_KSZ8841	0x8841		/* KSZ8841 */
#define	PCI_PRODUCT_MICREL_KSZ8842	0x8842		/* KSZ8842 */

/* Micron Technology products */
#define	PCI_PRODUCT_MICRON_MTFDKBA512QFM	0x5413		/* NVMe */

/* Microsoft products */
#define	PCI_PRODUCT_MICROSOFT_MN120	0x0001		/* MN-120 */
#define	PCI_PRODUCT_MICROSOFT_MN130	0x0002		/* MN-130 */
#define	PCI_PRODUCT_MICROSOFT_VGA	0x5353		/* VGA */

/* Miro Computer Products AG */
#define	PCI_PRODUCT_MIRO_DC20	0x5601		/* MiroVIDEO DC20 */
#define	PCI_PRODUCT_MIRO_2IVDC	0x5607		/* 2IVDC-PCX1 */
#define	PCI_PRODUCT_MIRO_MEDIA3D	0x5631		/* Media 3D */
#define	PCI_PRODUCT_MIRO_DC10	0x6057		/* MiroVIDEO DC10/DC20 */

/* Mitsubishi Electronics */
#define	PCI_PRODUCT_MITSUBISHIELEC_4D30T	0x0301		/* Powerstorm 4D30T */
#define	PCI_PRODUCT_MITSUBISHIELEC_GUI	0x0304		/* GUI Accel */

/* MosChip products */
#define	PCI_PRODUCT_MOSCHIP_MCS9865	0x6873		/* Serial MCS9865 */

/* Motorola products */
#define	PCI_PRODUCT_MOT_MPC105	0x0001		/* MPC105 PCI */
#define	PCI_PRODUCT_MOT_MPC106	0x0002		/* MPC106 PCI */
#define	PCI_PRODUCT_MOT_RAVEN	0x4801		/* Raven PCI */
#define	PCI_PRODUCT_MOT_SM56	0x5600		/* SM56 */

/* Moxa */
#define	PCI_PRODUCT_MOXA_C104H	0x1040		/* C104H */
#define	PCI_PRODUCT_MOXA_CP104UL	0x1041		/* CP-104UL */
#define	PCI_PRODUCT_MOXA_CP104JU	0x1042		/* CP-104JU */
#define	PCI_PRODUCT_MOXA_CP104EL	0x1043		/* CP-104EL */
#define	PCI_PRODUCT_MOXA_CP114	0x1141		/* CP-114 */
#define	PCI_PRODUCT_MOXA_C168H	0x1680		/* C168H */
#define	PCI_PRODUCT_MOXA_CP168U	0x1681		/* CP-168U */

/* Mesa Ridge Technologies (MAGMA) */
#define	PCI_PRODUCT_MRTMAGMA_DMA4	0x0011		/* DMA4 serial */

/* Micro Star International products */
#define	PCI_PRODUCT_MSI_RT3090	0x891a		/* RT3090 */

/* Mutech products */
#define	PCI_PRODUCT_MUTECH_MV1000	0x0001		/* MV1000 */

/* Mylex products */
#define	PCI_PRODUCT_MYLEX_960P_V2	0x0001		/* DAC960P V2 RAID */
#define	PCI_PRODUCT_MYLEX_960P_V3	0x0002		/* DAC960P V3 RAID */
#define	PCI_PRODUCT_MYLEX_960P_V4	0x0010		/* DAC960P V4 RAID */
#define	PCI_PRODUCT_MYLEX_960P_V5	0x0020		/* DAC960P V5 RAID */
#define	PCI_PRODUCT_MYLEX_ACCELERAID	0x0050		/* AcceleRAID */
#define	PCI_PRODUCT_MYLEX_EXTREMERAID	0xba56		/* eXtremeRAID */

/* Myricom */
#define	PCI_PRODUCT_MYRICOM_Z8E	0x0008		/* Z8E */
#define	PCI_PRODUCT_MYRICOM_Z8E_9	0x0009		/* Z8E */
#define	PCI_PRODUCT_MYRICOM_LANAI_92	0x8043		/* Myrinet LANai 9.2 */

/* Myson Century products */
#define	PCI_PRODUCT_MYSON_MTD800	0x0800		/* MTD800 */
#define	PCI_PRODUCT_MYSON_MTD803	0x0803		/* MTD803 */
#define	PCI_PRODUCT_MYSON_MTD891	0x0891		/* MTD891 */

/* National Instruments */
#define	PCI_PRODUCT_NATINST_PCIGPIB	0xc801		/* PCI-GPIB */

/* National Datacomm Corp products */
#define	PCI_PRODUCT_NDC_NCP130	0x0130		/* NCP130 */
#define	PCI_PRODUCT_NDC_NCP130A2	0x0131		/* NCP130A2 */

/* NEC */
#define	PCI_PRODUCT_NEC_USB	0x0035		/* USB */
#define	PCI_PRODUCT_NEC_POWERVR2	0x0046		/* PowerVR PCX2 */
#define	PCI_PRODUCT_NEC_MARTH	0x0074		/* I/O */
#define	PCI_PRODUCT_NEC_PKUG	0x007d		/* I/O */
#define	PCI_PRODUCT_NEC_USB2	0x00e0		/* USB */
#define	PCI_PRODUCT_NEC_UPD72874	0x00f2		/* Firewire */
#define	PCI_PRODUCT_NEC_UPD720400	0x0125		/* PCIE-PCIX */
#define	PCI_PRODUCT_NEC_UPD720200	0x0194		/* xHCI */
#define	PCI_PRODUCT_NEC_VERSAPRONXVA26D	0x803c		/* Versa Va26D Maestro */
#define	PCI_PRODUCT_NEC_VERSAMAESTRO	0x8058		/* Versa Maestro */

/* NeoMagic */
#define	PCI_PRODUCT_NEOMAGIC_NM2070	0x0001		/* Magicgraph NM2070 */
#define	PCI_PRODUCT_NEOMAGIC_128V	0x0002		/* Magicgraph 128V */
#define	PCI_PRODUCT_NEOMAGIC_128ZV	0x0003		/* Magicgraph 128ZV */
#define	PCI_PRODUCT_NEOMAGIC_NM2160	0x0004		/* Magicgraph NM2160 */
#define	PCI_PRODUCT_NEOMAGIC_NM2200	0x0005		/* Magicgraph NM2200 */
#define	PCI_PRODUCT_NEOMAGIC_NM2360	0x0006		/* Magicgraph NM2360 */
#define	PCI_PRODUCT_NEOMAGIC_NM256XLP	0x0016		/* MagicMedia 256XL+ */
#define	PCI_PRODUCT_NEOMAGIC_NM2230	0x0025		/* MagicMedia 256AV+ */
#define	PCI_PRODUCT_NEOMAGIC_NM256AV	0x8005		/* MagicMedia 256AV */
#define	PCI_PRODUCT_NEOMAGIC_NM256ZX	0x8006		/* MagicMedia 256ZX */

/* NetChip Technology products */
#define	PCI_PRODUCT_NETCHIP_NET2282	0x2282		/* NET2282 USB */

/* Neterion products */
#define	PCI_PRODUCT_NETERION_XFRAME	0x5831		/* Xframe */
#define	PCI_PRODUCT_NETERION_XFRAME_2	0x5832		/* Xframe II */

/* Netgear products */
#define	PCI_PRODUCT_NETGEAR_MA301	0x4100		/* MA301 */
#define	PCI_PRODUCT_NETGEAR_GA620	0x620a		/* GA620 */
#define	PCI_PRODUCT_NETGEAR_GA620T	0x630a		/* GA620T */

/* NetMos */
#define	PCI_PRODUCT_NETMOS_NM9805	0x9805		/* Nm9805 */
#define	PCI_PRODUCT_NETMOS_NM9820	0x9820		/* Nm9820 */
#define	PCI_PRODUCT_NETMOS_NM9835	0x9835		/* Nm9835 */
#define	PCI_PRODUCT_NETMOS_NM9845	0x9845		/* Nm9845 */
#define	PCI_PRODUCT_NETMOS_NM9865	0x9865		/* Nm9865 */
#define	PCI_PRODUCT_NETMOS_NM9900	0x9900		/* Nm9900 */
#define	PCI_PRODUCT_NETMOS_NM9901	0x9901		/* Nm9901 */
#define	PCI_PRODUCT_NETMOS_NM9912	0x9912		/* Nm9912 */
#define	PCI_PRODUCT_NETMOS_NM9922	0x9922		/* Nm9922 */

/* Netoctave */
#define	PCI_PRODUCT_NETOCTAVE_NSP2K	0x0100		/* NSP2K */

/* Network Security Technologies */
#define	PCI_PRODUCT_NETSEC_7751	0x7751		/* 7751 */

/* NetVin */
#define	PCI_PRODUCT_NETVIN_NV5000	0x5000		/* NetVin 5000 */

/* NetXen Inc products */
#define	PCI_PRODUCT_NETXEN_NXB_10GXXR	0x0001		/* NXB-10GXxR */
#define	PCI_PRODUCT_NETXEN_NXB_10GCX4	0x0002		/* NXB-10GCX4 */
#define	PCI_PRODUCT_NETXEN_NXB_4GCU	0x0003		/* NXB-4GCU */
#define	PCI_PRODUCT_NETXEN_NXB_IMEZ	0x0004		/* IMEZ 10GbE */
#define	PCI_PRODUCT_NETXEN_NXB_HMEZ	0x0005		/* HMEZ 10GbE */
#define	PCI_PRODUCT_NETXEN_NXB_IMEZ_2	0x0024		/* IMEZ 10GbE Mgmt */
#define	PCI_PRODUCT_NETXEN_NXB_HMEZ_2	0x0025		/* HMEZ 10GbE Mgmt */
#define	PCI_PRODUCT_NETXEN_NX3031	0x0100		/* NX3031 */

/* Newbridge / Tundra / IDT products */
#define	PCI_PRODUCT_NEWBRIDGE_CA91CX42	0x0000		/* Universe VME */
#define	PCI_PRODUCT_NEWBRIDGE_TSI381	0x8111		/* Tsi381 PCIE-PCI */
#define	PCI_PRODUCT_NEWBRIDGE_PEB383	0x8113		/* PEB383 PCIE-PCI */

/* NexGen products */
#define	PCI_PRODUCT_NEXGEN_NX82C501	0x4e78		/* NX82C501 PCI */

/* NKK products */
#define	PCI_PRODUCT_NKK_NDR4600	0xa001		/* NDR4600 PCI */

/* Nortel Networks products */
#define	PCI_PRODUCT_NORTEL_BS21	0x1211		/* BS21 */
#define	PCI_PRODUCT_NORTEL_211818A	0x8030		/* E-mobility */

/* National Semiconductor products */
#define	PCI_PRODUCT_NS_DP83810	0x0001		/* DP83810 */
#define	PCI_PRODUCT_NS_PC87415	0x0002		/* PC87415 IDE */
#define	PCI_PRODUCT_NS_PC87560	0x000e		/* 87560 Legacy I/O */
#define	PCI_PRODUCT_NS_USB	0x0012		/* USB */
#define	PCI_PRODUCT_NS_DP83815	0x0020		/* DP83815 */
#define	PCI_PRODUCT_NS_DP83820	0x0022		/* DP83820 */
#define	PCI_PRODUCT_NS_CS5535_HB	0x0028		/* CS5535 Host */
#define	PCI_PRODUCT_NS_CS5535_ISA	0x002b		/* CS5535 ISA */
#define	PCI_PRODUCT_NS_CS5535_IDE	0x002d		/* CS5535 IDE */
#define	PCI_PRODUCT_NS_CS5535_AUDIO	0x002e		/* CS5535 AUDIO */
#define	PCI_PRODUCT_NS_CS5535_USB	0x002f		/* CS5535 USB */
#define	PCI_PRODUCT_NS_CS5535_VIDEO	0x0030		/* CS5535 VIDEO */
#define	PCI_PRODUCT_NS_SATURN	0x0035		/* Saturn */
#define	PCI_PRODUCT_NS_SCX200_ISA	0x0500		/* SCx200 ISA */
#define	PCI_PRODUCT_NS_SCX200_SMI	0x0501		/* SCx200 SMI */
#define	PCI_PRODUCT_NS_SCX200_IDE	0x0502		/* SCx200 IDE */
#define	PCI_PRODUCT_NS_SCX200_AUDIO	0x0503		/* SCx200 AUDIO */
#define	PCI_PRODUCT_NS_SCX200_VIDEO	0x0504		/* SCx200 VIDEO */
#define	PCI_PRODUCT_NS_SCX200_XBUS	0x0505		/* SCx200 X-BUS */
#define	PCI_PRODUCT_NS_SC1100_ISA	0x0510		/* SC1100 ISA */
#define	PCI_PRODUCT_NS_SC1100_SMI	0x0511		/* SC1100 SMI */
#define	PCI_PRODUCT_NS_SC1100_XBUS	0x0515		/* SC1100 X-Bus */
#define	PCI_PRODUCT_NS_NS87410	0xd001		/* NS87410 */

/* Number Nine products */
#define	PCI_PRODUCT_NUMBER9_I128	0x2309		/* Imagine-128 */
#define	PCI_PRODUCT_NUMBER9_I128_2	0x2339		/* Imagine-128 II */
#define	PCI_PRODUCT_NUMBER9_I128_T2R	0x493d		/* Imagine-128 T2R */
#define	PCI_PRODUCT_NUMBER9_I128_T2R4	0x5348		/* Imagine-128 T2R4 */

/* NVIDIA products */
#define	PCI_PRODUCT_NVIDIA_NV1	0x0008		/* NV1 */
#define	PCI_PRODUCT_NVIDIA_DAC64	0x0009		/* DAC64 */
#define	PCI_PRODUCT_NVIDIA_RIVA_TNT	0x0020		/* Riva TNT */
#define	PCI_PRODUCT_NVIDIA_RIVA_TNT2	0x0028		/* Riva TNT2 */
#define	PCI_PRODUCT_NVIDIA_RIVA_TNT2_ULTRA	0x0029		/* Riva TNT2 Ultra */
#define	PCI_PRODUCT_NVIDIA_VANTA1	0x002c		/* Vanta */
#define	PCI_PRODUCT_NVIDIA_VANTA2	0x002d		/* Vanta */
#define	PCI_PRODUCT_NVIDIA_MCP04_ISA	0x0030		/* MCP04 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP04_SMB	0x0034		/* MCP04 SMBus */
#define	PCI_PRODUCT_NVIDIA_MCP04_IDE	0x0035		/* MCP04 IDE */
#define	PCI_PRODUCT_NVIDIA_MCP04_SATA	0x0036		/* MCP04 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP04_LAN1	0x0037		/* MCP04 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP04_LAN2	0x0038		/* MCP04 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP04_AC97	0x003a		/* MCP04 AC97 */
#define	PCI_PRODUCT_NVIDIA_MCP04_OHCI	0x003b		/* MCP04 USB */
#define	PCI_PRODUCT_NVIDIA_MCP04_EHCI	0x003c		/* MCP04 USB */
#define	PCI_PRODUCT_NVIDIA_MCP04_PPB	0x003d		/* MCP04 */
#define	PCI_PRODUCT_NVIDIA_MCP04_SATA2	0x003e		/* MCP04 SATA */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_ISA1	0x0050		/* nForce4 ISA */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_ISA2	0x0051		/* nForce4 ISA */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_SMB	0x0052		/* nForce4 SMBus */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_ATA133	0x0053		/* nForce4 IDE */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_SATA1	0x0054		/* nForce4 SATA */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_SATA2	0x0055		/* nForce4 SATA */
#define	PCI_PRODUCT_NVIDIA_CK804_LAN1	0x0056		/* CK804 LAN */
#define	PCI_PRODUCT_NVIDIA_CK804_LAN2	0x0057		/* CK804 LAN */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_AC	0x0059		/* nForce4 AC97 */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_OHCI	0x005a		/* nForce4 USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_EHCI	0x005b		/* nForce4 USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_PPB	0x005c		/* nForce4 */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_PPB2	0x005d		/* nForce4 PCIE */
#define	PCI_PRODUCT_NVIDIA_NFORCE4_MEM	0x005e		/* nForce4 DDR */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_ISA	0x0060		/* nForce2 ISA */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_SMB	0x0064		/* nForce2 SMBus */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_IDE	0x0065		/* nForce2 IDE */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_LAN	0x0066		/* nForce2 LAN */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_OHCI	0x0067		/* nForce2 USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_EHCI	0x0068		/* nForce2 USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_ACA	0x006a		/* nForce2 AC97 */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_APU	0x006b		/* nForce2 Audio */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_PPB	0x006c		/* nForce2 */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_PPB2	0x006d		/* nForce2 */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_FW	0x006e		/* nForce2 FireWire */
#define	PCI_PRODUCT_NVIDIA_MCP04_PPB2	0x007e		/* MCP04 PCIE */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_400_ISA	0x0080		/* nForce2 400 ISA */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_400_SMB	0x0084		/* nForce2 400 SMBus */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_400_IDE	0x0085		/* nForce2 400 IDE */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_LAN2	0x0086		/* nForce3 LAN */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_400_OHCI	0x0087		/* nForce2 400 USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_400_EHCI	0x0088		/* nForce2 400 USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_400_ACA	0x008a		/* nForce2 400 AC97 */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_400_PPB	0x008b		/* nForce2 400 */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_LAN3	0x008c		/* nForce3 LAN */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_400_SATA	0x008e		/* nForce2 400 SATA */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7800GTX	0x0091		/* GeForce 7800 GTX */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7800GT	0x0092		/* GeForce 7800 GT */
#define	PCI_PRODUCT_NVIDIA_ITNT2	0x00a0		/* Aladdin TNT2 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6800GO	0x00c8		/* GeForce Go 6800 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6800GO_U	0x00c9		/* GeForce Go 6800 Ultra */
#define	PCI_PRODUCT_NVIDIA_QUADROFXGO1400	0x00cc		/* Quadro FX Go1400 */
#define	PCI_PRODUCT_NVIDIA_QUADROFX1400	0x00ce		/* Quadro FX 1400 */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_ISA	0x00d0		/* nForce3 ISA */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_PCHB	0x00d1		/* nForce3 Host */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_PPB2	0x00d2		/* nForce3 */
#define	PCI_PRODUCT_NVIDIA_CK804_MEM	0x00d3		/* CK804 */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_SMB	0x00d4		/* nForce3 SMBus */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_IDE	0x00d5		/* nForce3 IDE */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_LAN1	0x00d6		/* nForce3 LAN */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_OHCI	0x00d7		/* nForce3 USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_EHCI	0x00d8		/* nForce3 USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_MODEM	0x00d9		/* nForce3 Modem */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_ACA	0x00da		/* nForce3 AC97 */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_PPB	0x00dd		/* nForce3 */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_LAN4	0x00df		/* nForce3 LAN */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_ISA	0x00e0		/* nForce3 250 ISA */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_PCHB	0x00e1		/* nForce3 250 Host */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_AGP	0x00e2		/* nForce3 250 AGP */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_SATA	0x00e3		/* nForce3 250 SATA */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_SMB	0x00e4		/* nForce3 250 SMBus */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_IDE	0x00e5		/* nForce3 250 IDE */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_LAN5	0x00e6		/* nForce3 LAN */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_OHCI	0x00e7		/* nForce3 250 USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_EHCI	0x00e8		/* nForce3 250 USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_ACA	0x00ea		/* nForce3 250 AC97 */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_PPB	0x00ed		/* nForce3 250 */
#define	PCI_PRODUCT_NVIDIA_NFORCE3_250_SATA2	0x00ee		/* nForce3 250 SATA */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6600GTAGP	0x00f1		/* GeForce 6600 GT AGP */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6600_3	0x00f2		/* GeForce 6600 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7800GS	0x00f5		/* GeForce 7800 GS */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6800GT	0x00f9		/* GeForce 6800 GT */
#define	PCI_PRODUCT_NVIDIA_GEFORCE5300PCX	0x00fc		/* GeForce 5300 PCX */
#define	PCI_PRODUCT_NVIDIA_QUADROFX330	0x00fd		/* Quadro FX 330 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE256	0x0100		/* GeForce256 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE256_DDR	0x0101		/* GeForce256 DDR */
#define	PCI_PRODUCT_NVIDIA_QUADRO	0x0103		/* Quadro */
#define	PCI_PRODUCT_NVIDIA_GEFORCE2MX	0x0110		/* GeForce2 MX */
#define	PCI_PRODUCT_NVIDIA_GEFORCE2MX_100	0x0111		/* GeForce2 MX 100 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE2GO	0x0112		/* GeForce2 Go */
#define	PCI_PRODUCT_NVIDIA_QUADRO2_MXR	0x0113		/* Quadro2 MXR */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6600GT	0x0140		/* GeForce 6600 GT */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6600	0x0141		/* GeForce 6600 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6600_2	0x0142		/* GeForce 6600 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6600GO	0x0144		/* GeForce 6600 Go */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6600GO_2	0x0146		/* GeForce 6600 Go */
#define	PCI_PRODUCT_NVIDIA_GEFORCE2GTS	0x0150		/* GeForce2 GTS */
#define	PCI_PRODUCT_NVIDIA_GEFORCE2TI	0x0151		/* GeForce2 Ti */
#define	PCI_PRODUCT_NVIDIA_GEFORCE2ULTRA	0x0152		/* GeForce2 Ultra */
#define	PCI_PRODUCT_NVIDIA_QUADRO2PRO	0x0153		/* Quadro2 Pro */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6200	0x0161		/* GeForce 6200 */
#define	PCI_PRODUCT_NVIDIA_QUADRONVS285	0x0165		/* Quadro NVS 285 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGO6200	0x0167		/* GeForce Go 6200 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4MX460	0x0170		/* GeForce4 MX 460 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4MX440	0x0171		/* GeForce4 MX 440 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4MX420	0x0172		/* GeForce4 MX 420 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4440GO	0x0174		/* GeForce4 440 Go */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4420GO	0x0175		/* GeForce4 420 Go */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4420GOM32	0x0176		/* GeForce4 420 Go 32M */
#define	PCI_PRODUCT_NVIDIA_QUADRO4500XGL	0x0178		/* Quadro4 500XGL */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4440GOM64	0x0179		/* GeForce4 440 Go 64M */
#define	PCI_PRODUCT_NVIDIA_QUADRO4200	0x017a		/* Quadro4 200/400NVS */
#define	PCI_PRODUCT_NVIDIA_QUADRO4550XGL	0x017b		/* Quadro4 550XGL */
#define	PCI_PRODUCT_NVIDIA_QUADRO4500GOGL	0x017c		/* Quadro4 GoGL */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4MX440AGP8	0x0181		/* GeForce4 MX 440 AGP */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4MX440SEAGP8	0x0182		/* GeForce4 MX 440SE AGP */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4MX420AGP8	0x0183		/* GeForce 4 MX 420 AGP */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4MX4000	0x0185		/* GeForce4 MX 4000 */
#define	PCI_PRODUCT_NVIDIA_QUADRO4_580XGL	0x0188		/* Quadro4 580 XGL */
#define	PCI_PRODUCT_NVIDIA_QUADRO4NVS	0x018a		/* Quadro4 NVS */
#define	PCI_PRODUCT_NVIDIA_QUADRO4_380XGL	0x018b		/* Quadro4 380 XGL */
#define	PCI_PRODUCT_NVIDIA_GEFORCE8800GTX	0x0191		/* GeForce 8800 GTX */
#define	PCI_PRODUCT_NVIDIA_GEFORCE8800GTS	0x0193		/* GeForce 8800 GTS */
#define	PCI_PRODUCT_NVIDIA_GEFORCE2_11	0x01a0		/* GeForce2 Crush11 */
#define	PCI_PRODUCT_NVIDIA_NFORCE_PCHB	0x01a4		/* nForce Host */
#define	PCI_PRODUCT_NVIDIA_NFORCE_DDR2	0x01aa		/* nForce 220 DDR */
#define	PCI_PRODUCT_NVIDIA_NFORCE_DDR	0x01ab		/* nForce 420 DDR */
#define	PCI_PRODUCT_NVIDIA_NFORCE_MEM	0x01ac		/* nForce 220/420 */
#define	PCI_PRODUCT_NVIDIA_NFORCE_MEM1	0x01ad		/* nForce 220/420 */
#define	PCI_PRODUCT_NVIDIA_NFORCE_APU	0x01b0		/* nForce APU */
#define	PCI_PRODUCT_NVIDIA_NFORCE_ACA	0x01b1		/* nForce AC97 */
#define	PCI_PRODUCT_NVIDIA_NFORCE_ISA	0x01b2		/* nForce ISA */
#define	PCI_PRODUCT_NVIDIA_NFORCE_SMB	0x01b4		/* nForce SMBus */
#define	PCI_PRODUCT_NVIDIA_NFORCE_AGP	0x01b7		/* nForce AGP */
#define	PCI_PRODUCT_NVIDIA_NFORCE_PPB	0x01b8		/* nForce */
#define	PCI_PRODUCT_NVIDIA_NFORCE_IDE	0x01bc		/* nForce IDE */
#define	PCI_PRODUCT_NVIDIA_NFORCE_OHCI	0x01c2		/* nForce USB */
#define	PCI_PRODUCT_NVIDIA_NFORCE_LAN	0x01c3		/* nForce LAN */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7300LE	0x01d1		/* GeForce 7300 LE */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7200GS	0x01d3		/* GeForce 7200 GS */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7300GO	0x01d7		/* GeForce 7300 Go */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7400GO	0x01d8		/* GeForce 7400 Go */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7300GS	0x01df		/* GeForce 7300 GS */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_PCHB	0x01e0		/* nForce2 PCI */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_AGP	0x01e8		/* nForce2 AGP */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_MEM0	0x01ea		/* nForce2 */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_MEM1	0x01eb		/* nForce2 */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_MEM2	0x01ec		/* nForce2 */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_MEM3	0x01ed		/* nForce2 */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_MEM4	0x01ee		/* nForce2 */
#define	PCI_PRODUCT_NVIDIA_NFORCE2_MEM5	0x01ef		/* nForce2 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4MXNFORCE	0x01f0		/* GeForce4 MX nForce GPU */
#define	PCI_PRODUCT_NVIDIA_GEFORCE3	0x0200		/* GeForce3 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE3TI200	0x0201		/* GeForce3 Ti 200 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE3TI500	0x0202		/* GeForce3 Ti 500 */
#define	PCI_PRODUCT_NVIDIA_QUADRO_DCC	0x0203		/* Quadro DCC */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6200_2	0x0221		/* GeForce 6200 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6150	0x0240		/* GeForce 6150 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6150LE	0x0241		/* GeForce 6150 LE */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6100	0x0242		/* GeForce 6100 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGO6150	0x0244		/* GeForce Go 6150 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGO6100	0x0247		/* GeForce Go 6100 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4TI4600	0x0250		/* GeForce4 Ti 4600 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4TI4400	0x0251		/* GeForce4 Ti 4400 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4TI4200	0x0253		/* GeForce4 Ti 4200 */
#define	PCI_PRODUCT_NVIDIA_QUADRO4900XGL	0x0258		/* Quadro4 900 XGL */
#define	PCI_PRODUCT_NVIDIA_QUADRO4750XGL	0x0259		/* Quadro4 750 XGL */
#define	PCI_PRODUCT_NVIDIA_QUADRO4700XGL	0x025b		/* Quadro4 700 XGL */
#define	PCI_PRODUCT_NVIDIA_MCP51_ISA1	0x0260		/* MCP51 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP51_ISA2	0x0261		/* MCP51 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP51_ISA3	0x0262		/* MCP51 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP51_ISA4	0x0263		/* MCP51 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP51_SMB	0x0264		/* MCP51 SMBus */
#define	PCI_PRODUCT_NVIDIA_MCP51_IDE	0x0265		/* MCP51 IDE */
#define	PCI_PRODUCT_NVIDIA_MCP51_SATA	0x0266		/* MCP51 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP51_SATA2	0x0267		/* MCP51 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP51_LAN1	0x0268		/* MCP51 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP51_LAN2	0x0269		/* MCP51 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP51_ACA	0x026b		/* MCP51 AC97 */
#define	PCI_PRODUCT_NVIDIA_MCP51_HDA	0x026c		/* MCP51 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP51_OHCI	0x026d		/* MCP51 USB */
#define	PCI_PRODUCT_NVIDIA_MCP51_EHCI	0x026e		/* MCP51 USB */
#define	PCI_PRODUCT_NVIDIA_MCP51_PPB	0x026f		/* MCP51 */
#define	PCI_PRODUCT_NVIDIA_MCP51_HB	0x0270		/* MCP51 Host */
#define	PCI_PRODUCT_NVIDIA_MCP51_PMU	0x0271		/* MCP51 PMU */
#define	PCI_PRODUCT_NVIDIA_MCP51_MEM	0x0272		/* MCP51 Memory */
#define	PCI_PRODUCT_NVIDIA_C51_MEM_2	0x027e		/* C51 Memory */
#define	PCI_PRODUCT_NVIDIA_C51_MEM_3	0x027f		/* C51 Memory */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4TI4800	0x0280		/* GeForce4 Ti 4800 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4TI4200_2	0x0281		/* GeForce4 Ti 4200 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE4TI4200GO	0x0286		/* GeForce4 Ti 4200 Go */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7900GT	0x0291		/* GeForce 7900 GT/GTO */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7950GTX	0x0297		/* GeForce Go 7950 GTX */
#define	PCI_PRODUCT_NVIDIA_QUADROFX3500	0x029d		/* Quadro FX 3500 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7600GT_2	0x02e0		/* GeForce 7600 GT */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7600GS_2	0x02e1		/* GeForce 7600 GS */
#define	PCI_PRODUCT_NVIDIA_C51_HB_1	0x02f0		/* C51 Host */
#define	PCI_PRODUCT_NVIDIA_C51_HB_2	0x02f1		/* C51 Host */
#define	PCI_PRODUCT_NVIDIA_C51_HB_3	0x02f2		/* C51 Host */
#define	PCI_PRODUCT_NVIDIA_C51_HB_4	0x02f3		/* C51 Host */
#define	PCI_PRODUCT_NVIDIA_C51_HB_5	0x02f4		/* C51 Host */
#define	PCI_PRODUCT_NVIDIA_C51_HB_6	0x02f5		/* C51 Host */
#define	PCI_PRODUCT_NVIDIA_C51_HB_7	0x02f6		/* C51 Host */
#define	PCI_PRODUCT_NVIDIA_C51_HB_8	0x02f7		/* C51 Host */
#define	PCI_PRODUCT_NVIDIA_C51_MEM_5	0x02f8		/* C51 Memory */
#define	PCI_PRODUCT_NVIDIA_C51_MEM_4	0x02f9		/* C51 Memory */
#define	PCI_PRODUCT_NVIDIA_C51_MEM_0	0x02fa		/* C51 Memory */
#define	PCI_PRODUCT_NVIDIA_C51_PCIE_0	0x02fb		/* C51 PCIE */
#define	PCI_PRODUCT_NVIDIA_C51_PCIE_1	0x02fc		/* C51 PCIE */
#define	PCI_PRODUCT_NVIDIA_C51_PCIE_2	0x02fd		/* C51 PCIE */
#define	PCI_PRODUCT_NVIDIA_C51_MEM_1	0x02fe		/* C51 Memory */
#define	PCI_PRODUCT_NVIDIA_C51_MEM_6	0x02ff		/* C51 Memory */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5800_U	0x0301		/* GeForce FX 5800 Ultra */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5800	0x0302		/* GeForce FX 5800 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5600_U	0x0311		/* GeForce FX 5600 Ultra */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5600	0x0312		/* GeForce FX 5600 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFXGO5600	0x031a		/* GeForce FX Go 5600 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFXGO5650	0x031b		/* GeForce FX Go 5650 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5200_U	0x0321		/* GeForce FX 5200 Ultra */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5200	0x0322		/* GeForce FX 5200 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFXGO5200	0x0324		/* GeForce FX Go 5200 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5500	0x0326		/* GeForce FX 5500 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5100	0x0327		/* GeForce FX 5100 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFXGO5200_3	0x0328		/* GeForce FX Go 5200 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFXGO5200_2	0x0329		/* GeForce FX Go 5200 */
#define	PCI_PRODUCT_NVIDIA_QUADROFX500	0x032b		/* Quadro FX 500/600 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFXGO5300	0x032c		/* GeForce FX Go 5300 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFXGO5100	0x032d		/* GeForce FX Go 5100 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5900_U	0x0330		/* GeForce FX 5900 Ultra */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5900	0x0331		/* GeForce FX 5900 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5950_U	0x0333		/* GeForce FX 5950 Ultra */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFX5700LE	0x0343		/* GeForce FX 5700LE */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFXGO5700_2	0x0347		/* GeForce FX Go 5700 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEFXGO5700	0x0348		/* GeForce FX Go 5700 */
#define	PCI_PRODUCT_NVIDIA_MCP55_ISA1	0x0360		/* MCP55 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP55_ISA2	0x0361		/* MCP55 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP55_ISA3	0x0362		/* MCP55 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP55_ISA4	0x0363		/* MCP55 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP55_ISA5	0x0364		/* MCP55 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP55_ISA6	0x0365		/* MCP55 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP55_ISA7	0x0366		/* MCP55 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP55_ISA8	0x0367		/* MCP55 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP55_SMB	0x0368		/* MCP55 SMBus */
#define	PCI_PRODUCT_NVIDIA_MCP55_MEM1	0x0369		/* MCP55 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP55_MEM2	0x036a		/* MCP55 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP55_OHCI	0x036c		/* MCP55 USB */
#define	PCI_PRODUCT_NVIDIA_MCP55_EHCI	0x036d		/* MCP55 USB */
#define	PCI_PRODUCT_NVIDIA_MCP55_IDE	0x036e		/* MCP55 IDE */
#define	PCI_PRODUCT_NVIDIA_MCP55_PPB_6	0x0370		/* MCP55 */
#define	PCI_PRODUCT_NVIDIA_MCP55_HDA	0x0371		/* MCP55 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP55_LAN1	0x0372		/* MCP55 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP55_LAN2	0x0373		/* MCP55 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP55_PPB_1	0x0374		/* MCP55 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP55_PPB_2	0x0375		/* MCP55 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP55_PPB_3	0x0376		/* MCP55 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP55_PPB_4	0x0377		/* MCP55 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP55_PPB_5	0x0378		/* MCP55 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP55_MEM3	0x037a		/* MCP55 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP55_SATA	0x037e		/* MCP55 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP55_SATA2	0x037f		/* MCP55 SATA */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7600GT	0x0391		/* GeForce 7600 GT */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7600GS	0x0392		/* GeForce 7600 GS */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7300GT	0x0393		/* GeForce 7300 GT */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7900GO	0x0398		/* GeForce 7600 Go */
#define	PCI_PRODUCT_NVIDIA_C55_HB_1	0x03a0		/* C55 Host */
#define	PCI_PRODUCT_NVIDIA_C55_HB_2	0x03a1		/* C55 Host */
#define	PCI_PRODUCT_NVIDIA_C55_HB_3	0x03a2		/* C55 Host */
#define	PCI_PRODUCT_NVIDIA_C55_HB_4	0x03a3		/* C55 Host */
#define	PCI_PRODUCT_NVIDIA_C55_HB_5	0x03a4		/* C55 Host */
#define	PCI_PRODUCT_NVIDIA_C55_HB_6	0x03a5		/* C55 Host */
#define	PCI_PRODUCT_NVIDIA_C55_HB_7	0x03a6		/* C55 Host */
#define	PCI_PRODUCT_NVIDIA_C55_HB_8	0x03a7		/* C55 Host */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_1	0x03a8		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_2	0x03a9		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_3	0x03aa		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_4	0x03ab		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_5	0x03ac		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_6	0x03ad		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_7	0x03ae		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_8	0x03af		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_9	0x03b0		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_10	0x03b1		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_11	0x03b2		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_12	0x03b3		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_13	0x03b4		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_14	0x03b5		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_15	0x03b6		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_PCIE_0	0x03b7		/* C55 PCIE */
#define	PCI_PRODUCT_NVIDIA_C55_PCIE_1	0x03b8		/* C55 PCIE */
#define	PCI_PRODUCT_NVIDIA_C55_PCIE_2	0x03b9		/* C55 PCIE */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_16	0x03ba		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_C55_PCIE_3	0x03bb		/* C55 PCIE */
#define	PCI_PRODUCT_NVIDIA_C55_MEM_17	0x03bc		/* C55 Memory */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6100_430	0x03d0		/* GeForce 6100 nForce 430 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE6100_405	0x03d1		/* GeForce 6100 nForce 405 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7025_630A	0x03d6		/* GeForce 7025 nForce 630a */
#define	PCI_PRODUCT_NVIDIA_MCP61_ISA	0x03e0		/* MCP61 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP61_ISA_2	0x03e1		/* MCP61 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP61_MEM1	0x03e2		/* MCP61 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP61_HDA_1	0x03e4		/* MCP61 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP61_LAN1	0x03e5		/* MCP61 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP61_LAN2	0x03e6		/* MCP61 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP61_SATA	0x03e7		/* MCP61 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP61_PPB_1	0x03e8		/* MCP61 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP61_PPB_2	0x03e9		/* MCP61 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP61_MEM2	0x03ea		/* MCP61 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP61_SMB	0x03eb		/* MCP61 SMBus */
#define	PCI_PRODUCT_NVIDIA_MCP61_IDE	0x03ec		/* MCP61 IDE */
#define	PCI_PRODUCT_NVIDIA_MCP61_LAN3	0x03ee		/* MCP61 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP61_LAN4	0x03ef		/* MCP61 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP61_HDA_2	0x03f0		/* MCP61 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP61_OHCI	0x03f1		/* MCP61 USB */
#define	PCI_PRODUCT_NVIDIA_MCP61_EHCI	0x03f2		/* MCP61 USB */
#define	PCI_PRODUCT_NVIDIA_MCP61_PPB_3	0x03f3		/* MCP61 */
#define	PCI_PRODUCT_NVIDIA_MCP61_SMU	0x03f4		/* MCP61 SMU */
#define	PCI_PRODUCT_NVIDIA_MCP61_MEM3	0x03f5		/* MCP61 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP61_SATA2	0x03f6		/* MCP61 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP61_SATA3	0x03f7		/* MCP61 SATA */
#define	PCI_PRODUCT_NVIDIA_GEFORCE8600_GT	0x0402		/* GeForce 8600 GT */
#define	PCI_PRODUCT_NVIDIA_GEFORCE8600M_GT	0x0407		/* GeForce 8600M GT */
#define	PCI_PRODUCT_NVIDIA_QUADROFX570M	0x040c		/* Quadro FX 570M */
#define	PCI_PRODUCT_NVIDIA_GEFORCE8500_GT	0x0421		/* GeForce 8500 GT */
#define	PCI_PRODUCT_NVIDIA_GEFORCE8400_GS_0	0x0422		/* GeForce 8400 GS */
#define	PCI_PRODUCT_NVIDIA_GEFORCE8400M_GS	0x0427		/* GeForce 8400M GS */
#define	PCI_PRODUCT_NVIDIA_GEFORCE8400M_G	0x0428		/* GeForce 8400M G */
#define	PCI_PRODUCT_NVIDIA_MCP65_ISA1	0x0440		/* MCP65 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP65_ISA2	0x0441		/* MCP65 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP65_MEM1	0x0444		/* MCP65 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP65_MEM2	0x0445		/* MCP65 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP65_SMB	0x0446		/* MCP65 SMBus */
#define	PCI_PRODUCT_NVIDIA_MCP65_IDE	0x0448		/* MCP65 IDE */
#define	PCI_PRODUCT_NVIDIA_MCP65_PPB_1	0x0449		/* MCP65 PCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_HDA_1	0x044a		/* MCP65 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP65_HDA_2	0x044b		/* MCP65 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_1	0x044c		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_2	0x044d		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_3	0x044e		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_AHCI_4	0x044f		/* MCP65 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP65_LAN1	0x0450		/* MCP65 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP65_LAN2	0x0451		/* MCP65 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP65_LAN3	0x0452		/* MCP65 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP65_LAN4	0x0453		/* MCP65 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP65_USB_1	0x0454		/* MCP65 USB */
#define	PCI_PRODUCT_NVIDIA_MCP65_USB_2	0x0455		/* MCP65 USB */
#define	PCI_PRODUCT_NVIDIA_MCP65_USB_3	0x0456		/* MCP65 USB */
#define	PCI_PRODUCT_NVIDIA_MCP65_USB_4	0x0457		/* MCP65 USB */
#define	PCI_PRODUCT_NVIDIA_MCP65_PPB_2	0x0458		/* MCP65 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP65_PPB_3	0x0459		/* MCP65 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP65_PPB_4	0x045a		/* MCP65 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP65_PPB_5	0x045b		/* MCP65 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP65_SATA_1	0x045c		/* MCP65 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP65_SATA_2	0x045d		/* MCP65 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP65_SATA_3	0x045e		/* MCP65 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP65_SATA_4	0x045f		/* MCP65 SATA */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7000M	0x0533		/* GeForce 7000M */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7050_PV	0x053b		/* GeForce 7050 PV */
#define	PCI_PRODUCT_NVIDIA_MCP67_MEM1	0x0541		/* MCP67 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP67_SMB	0x0542		/* MCP67 SMBus */
#define	PCI_PRODUCT_NVIDIA_MCP67_COPROC	0x0543		/* MCP67 Co-processor */
#define	PCI_PRODUCT_NVIDIA_MCP67_MEM2	0x0547		/* MCP67 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP67_ISA	0x0548		/* MCP67 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP67_LAN1	0x054c		/* MCP67 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP67_LAN2	0x054d		/* MCP67 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP67_LAN3	0x054e		/* MCP67 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP67_LAN4	0x054f		/* MCP67 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP67_SATA_1	0x0550		/* MCP67 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP67_SATA_2	0x0551		/* MCP67 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP67_SATA_3	0x0552		/* MCP67 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP67_SATA_4	0x0553		/* MCP67 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP67_AHCI_1	0x0554		/* MCP67 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP67_AHCI_2	0x0555		/* MCP67 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP67_AHCI_3	0x0556		/* MCP67 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP67_AHCI_4	0x0557		/* MCP67 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP67_RAID_1	0x0558		/* MCP67 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP67_RAID_2	0x0559		/* MCP67 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP67_RAID_3	0x055a		/* MCP67 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP67_RAID_4	0x055b		/* MCP67 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP67_HDA_1	0x055c		/* MCP67 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP67_HDA_2	0x055d		/* MCP67 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP67_OHCI	0x055e		/* MCP67 USB */
#define	PCI_PRODUCT_NVIDIA_MCP67_EHCI	0x055f		/* MCP67 USB */
#define	PCI_PRODUCT_NVIDIA_MCP67_IDE	0x0560		/* MCP67 IDE */
#define	PCI_PRODUCT_NVIDIA_MCP67_PPB_1	0x0561		/* MCP67 PCI */
#define	PCI_PRODUCT_NVIDIA_MCP67_PPB_2	0x0562		/* MCP67 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP67_PPB_3	0x0563		/* MCP67 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP77_MEM1	0x0568		/* MCP77 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP77_PPB_1	0x0569		/* MCP77 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP73_EHCI	0x056a		/* MCP73 USB */
#define	PCI_PRODUCT_NVIDIA_MCP73_IDE	0x056c		/* MCP73 IDE */
#define	PCI_PRODUCT_NVIDIA_MCP73_PPB_1	0x056d		/* MCP73 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP73_PPB_2	0x056e		/* MCP73 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP73_PPB_3	0x056f		/* MCP73 PCIE */
#define	PCI_PRODUCT_NVIDIA_NFORCE_200	0x05b1		/* nForce 200 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGTX285	0x05e3		/* GeForce GTX 285 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_9800_GTX	0x0605		/* GeForce 9800 GTX */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_8800_GT	0x0611		/* GeForce 8800 GT */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_9800_GT	0x0614		/* GeForce 9800 GT */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_9600_GT	0x0622		/* GeForce 9600 GT */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_9300_GE_1	0x06e0		/* GeForce 9300 GE */
#define	PCI_PRODUCT_NVIDIA_GEFORCE8400_GS_1	0x06e4		/* GeForce 8400 GS */
#define	PCI_PRODUCT_NVIDIA_GEFORCE9300M_GS	0x06e9		/* GeForce 9300M GS */
#define	PCI_PRODUCT_NVIDIA_QUADRONVS150	0x06ea		/* Quadro NVS 150m */
#define	PCI_PRODUCT_NVIDIA_QUADRONVS160	0x06eb		/* Quadro NVS 160m */
#define	PCI_PRODUCT_NVIDIA_MCP77_MEM2	0x0751		/* MCP77 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP77_SMB	0x0752		/* MCP77 SMBus */
#define	PCI_PRODUCT_NVIDIA_MCP77_COPROC	0x0753		/* MCP77 Co-processor */
#define	PCI_PRODUCT_NVIDIA_MCP77_MEM3	0x0754		/* MCP77 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP77_IDE	0x0759		/* MCP77 IDE */
#define	PCI_PRODUCT_NVIDIA_MCP77_PPB_2	0x075a		/* MCP77 PCI */
#define	PCI_PRODUCT_NVIDIA_MCP77_PPB_3	0x075b		/* MCP77 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP77_ISA1	0x075c		/* MCP77 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP77_ISA2	0x075d		/* MCP77 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP77_ISA3	0x075e		/* MCP77 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP77_LAN1	0x0760		/* MCP77 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP77_LAN2	0x0761		/* MCP77 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP77_LAN3	0x0762		/* MCP77 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP77_LAN4	0x0763		/* MCP77 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP77_HDA_1	0x0774		/* MCP77 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP77_HDA_2	0x0775		/* MCP77 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP77_HDA_3	0x0776		/* MCP77 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP77_HDA_4	0x0777		/* MCP77 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP77_PPB_4	0x0778		/* MCP77 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP77_PPB_5	0x0779		/* MCP77 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP77_PPB_6	0x077a		/* MCP77 PCI */
#define	PCI_PRODUCT_NVIDIA_MCP77_OHCI_1	0x077b		/* MCP77 USB */
#define	PCI_PRODUCT_NVIDIA_MCP77_EHCI_1	0x077c		/* MCP77 USB */
#define	PCI_PRODUCT_NVIDIA_MCP77_OHCI_2	0x077d		/* MCP77 USB */
#define	PCI_PRODUCT_NVIDIA_MCP77_EHCI_2	0x077e		/* MCP77 USB */
#define	PCI_PRODUCT_NVIDIA_MCP73_HB_1	0x07c0		/* MCP73 Host */
#define	PCI_PRODUCT_NVIDIA_MCP73_HB_2	0x07c1		/* MCP73 Host */
#define	PCI_PRODUCT_NVIDIA_MCP73_HB_3	0x07c2		/* MCP73 Host */
#define	PCI_PRODUCT_NVIDIA_MCP73_HB_4	0x07c3		/* MCP73 Host */
#define	PCI_PRODUCT_NVIDIA_MCP73_HB_5	0x07c5		/* MCP73 Host */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM11	0x07c8		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM1	0x07cb		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM2	0x07cd		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM3	0x07ce		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM4	0x07cf		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM5	0x07d0		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM6	0x07d1		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM7	0x07d2		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM8	0x07d3		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM9	0x07d6		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_ISA	0x07d7		/* MCP73 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP73_SMB	0x07d8		/* MCP73 SMBus */
#define	PCI_PRODUCT_NVIDIA_MCP73_MEM10	0x07d9		/* MCP73 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP73_LAN1	0x07dc		/* MCP73 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP73_LAN2	0x07dd		/* MCP73 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP73_LAN3	0x07de		/* MCP73 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP73_LAN4	0x07df		/* MCP73 LAN */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7100	0x07e1		/* GeForce 7100 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE7050	0x07e3		/* GeForce 7050 */
#define	PCI_PRODUCT_NVIDIA_MCP73_SATA_1	0x07f0		/* MCP73 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP73_SATA_2	0x07f1		/* MCP73 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP73_SATA_3	0x07f2		/* MCP73 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP73_SATA_4	0x07f3		/* MCP73 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP73_AHCI_1	0x07f4		/* MCP73 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP73_AHCI_2	0x07f5		/* MCP73 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP73_AHCI_3	0x07f6		/* MCP73 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP73_AHCI_4	0x07f7		/* MCP73 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP73_RAID_1	0x07f8		/* MCP73 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP73_RAID_2	0x07f9		/* MCP73 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP73_RAID_3	0x07fa		/* MCP73 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP73_RAID_4	0x07fb		/* MCP73 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP73_HDA_1	0x07fc		/* MCP73 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP73_HDA_2	0x07fd		/* MCP73 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP73_OHCI	0x07fe		/* MCP73 USB */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_8200_G	0x0845		/* GeForce 8200m G */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_9100	0x0847		/* GeForce 9100 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_8200	0x0849		/* GeForce 8200 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_9300_GE_2	0x084b		/* GeForce 9300 GE */
#define	PCI_PRODUCT_NVIDIA_NFORCE_780A_SLI	0x084c		/* nForce 780a SLI */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_9400	0x0861		/* GeForce 9400 */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_9400_2	0x0863		/* GeForce 9400m */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_9300	0x086c		/* GeForce 9300 */
#define	PCI_PRODUCT_NVIDIA_ION_VGA	0x087d		/* ION VGA */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_320M_1	0x08a0		/* GeForce 320M */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_320M_2	0x08a4		/* GeForce 320M */
#define	PCI_PRODUCT_NVIDIA_GEFORCE210	0x0a65		/* GeForce 210 */
#define	PCI_PRODUCT_NVIDIA_MCP79_HB_1	0x0a80		/* MCP79 Host */
#define	PCI_PRODUCT_NVIDIA_MCP79_HB_2	0x0a81		/* MCP79 Host */
#define	PCI_PRODUCT_NVIDIA_MCP79_HB_3	0x0a82		/* MCP79 Host */
#define	PCI_PRODUCT_NVIDIA_MCP79_HB_4	0x0a83		/* MCP79 Host */
#define	PCI_PRODUCT_NVIDIA_MCP79_HB_5	0x0a84		/* MCP79 Host */
#define	PCI_PRODUCT_NVIDIA_MCP79_HB_6	0x0a85		/* MCP79 Host */
#define	PCI_PRODUCT_NVIDIA_MCP79_HB_7	0x0a86		/* MCP79 Host */
#define	PCI_PRODUCT_NVIDIA_MCP79_HB_8	0x0a87		/* MCP79 Host */
#define	PCI_PRODUCT_NVIDIA_MCP79_MEM1	0x0a88		/* MCP79 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP79_MEM2	0x0a89		/* MCP79 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP7A_PPB_1	0x0aa0		/* MCP79 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP79_SMB	0x0aa2		/* MCP79 SMBus */
#define	PCI_PRODUCT_NVIDIA_MCP79_COPROC	0x0aa3		/* MCP79 Co-processor */
#define	PCI_PRODUCT_NVIDIA_MCP79_MEM3	0x0aa4		/* MCP79 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP7A_OHCI_1	0x0aa5		/* MCP79 USB */
#define	PCI_PRODUCT_NVIDIA_MCP7A_EHCI_1	0x0aa6		/* MCP79 USB */
#define	PCI_PRODUCT_NVIDIA_MCP79_OHCI_2	0x0aa7		/* MCP79 USB */
#define	PCI_PRODUCT_NVIDIA_MCP79_OHCI_3	0x0aa8		/* MCP79 USB */
#define	PCI_PRODUCT_NVIDIA_MCP79_EHCI_2	0x0aa9		/* MCP79 USB */
#define	PCI_PRODUCT_NVIDIA_MCP79_EHCI_3	0x0aaa		/* MCP79 USB */
#define	PCI_PRODUCT_NVIDIA_MCP79_PPB_2	0x0aab		/* MCP79 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP79_ISA1	0x0aac		/* MCP79 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP79_ISA2	0x0aad		/* MCP79 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP79_ISA3	0x0aae		/* MCP79 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP79_ISA4	0x0aaf		/* MCP79 ISA */
#define	PCI_PRODUCT_NVIDIA_MCP79_LAN1	0x0ab0		/* MCP79 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP79_LAN2	0x0ab1		/* MCP79 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP79_LAN3	0x0ab2		/* MCP79 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP79_LAN4	0x0ab3		/* MCP79 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP79_SATA_1	0x0ab4		/* MCP79 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP79_SATA_2	0x0ab5		/* MCP79 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP79_SATA_3	0x0ab6		/* MCP79 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP79_SATA_4	0x0ab7		/* MCP79 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP79_AHCI_1	0x0ab8		/* MCP79 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP79_AHCI_2	0x0ab9		/* MCP79 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP79_AHCI_3	0x0aba		/* MCP79 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP79_AHCI_4	0x0abb		/* MCP79 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP79_RAID_1	0x0abc		/* MCP79 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP79_RAID_2	0x0abd		/* MCP79 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP79_RAID_3	0x0abe		/* MCP79 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP79_RAID_4	0x0abf		/* MCP79 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP79_HDA_1	0x0ac0		/* MCP79 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP79_HDA_2	0x0ac1		/* MCP79 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP79_HDA_3	0x0ac2		/* MCP79 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP79_HDA_4	0x0ac3		/* MCP79 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP79_PPB_3	0x0ac4		/* MCP79 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP79_PPB_4	0x0ac5		/* MCP79 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP79_PPB_5	0x0ac6		/* MCP79 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP79_PPB_6	0x0ac7		/* MCP79 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP79_PPB_7	0x0ac8		/* MCP79 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP77_SATA_1	0x0ad0		/* MCP77 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP77_SATA_2	0x0ad1		/* MCP77 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP77_SATA_3	0x0ad2		/* MCP77 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP77_SATA_4	0x0ad3		/* MCP77 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP77_AHCI_1	0x0ad4		/* MCP77 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP77_AHCI_2	0x0ad5		/* MCP77 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP77_AHCI_3	0x0ad6		/* MCP77 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP77_AHCI_4	0x0ad7		/* MCP77 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP77_RAID_1	0x0ad8		/* MCP77 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP77_RAID_2	0x0ad9		/* MCP77 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP77_RAID_3	0x0ada		/* MCP77 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP77_RAID_4	0x0adb		/* MCP77 RAID */
#define	PCI_PRODUCT_NVIDIA_GF108_HDA	0x0bea		/* GF108 HD Audio */
#define	PCI_PRODUCT_NVIDIA_GF116_HDA	0x0bee		/* GF116 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP89_HB	0x0d60		/* MCP89 Host */
#define	PCI_PRODUCT_NVIDIA_MCP89_MEM_1	0x0d68		/* MCP89 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP89_MEM_2	0x0d69		/* MCP89 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP89_PPB_1	0x0d76		/* MCP89 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP89_SMB	0x0d79		/* MCP89 SMBus */
#define	PCI_PRODUCT_NVIDIA_MCP89_COPROC	0x0d7a		/* MCP89 Co-processor */
#define	PCI_PRODUCT_NVIDIA_MCP89_MEM_4	0x0d7b		/* MCP89 Memory */
#define	PCI_PRODUCT_NVIDIA_MCP89_LAN	0x0d7d		/* MCP89 LAN */
#define	PCI_PRODUCT_NVIDIA_MCP89_LPC	0x0d80		/* MCP89 LPC */
#define	PCI_PRODUCT_NVIDIA_MCP89_SATA_1	0x0d84		/* MCP89 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP89_SATA_2	0x0d85		/* MCP89 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP89_SATA_3	0x0d86		/* MCP89 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP89_SATA_4	0x0d87		/* MCP89 SATA */
#define	PCI_PRODUCT_NVIDIA_MCP89_AHCI_1	0x0d88		/* MCP89 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP89_AHCI_2	0x0d89		/* MCP89 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP89_AHCI_3	0x0d8a		/* MCP89 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP89_AHCI_4	0x0d8b		/* MCP89 AHCI */
#define	PCI_PRODUCT_NVIDIA_MCP89_RAID_1	0x0d8c		/* MCP89 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP89_RAID_2	0x0d8d		/* MCP89 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP89_RAID_3	0x0d8e		/* MCP89 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP89_RAID_4	0x0d8f		/* MCP89 RAID */
#define	PCI_PRODUCT_NVIDIA_MCP89_HDA_1	0x0d94		/* MCP89 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP89_HDA_2	0x0d95		/* MCP89 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP89_HDA_3	0x0d96		/* MCP89 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP89_HDA_4	0x0d97		/* MCP89 HD Audio */
#define	PCI_PRODUCT_NVIDIA_MCP89_PPB_2	0x0d9a		/* MCP89 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP89_PPB_3	0x0d9b		/* MCP89 PCIE */
#define	PCI_PRODUCT_NVIDIA_MCP89_OHCI	0x0d9c		/* MCP89 USB */
#define	PCI_PRODUCT_NVIDIA_MCP89_EHCI	0x0d9d		/* MCP89 USB */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGTX460M	0x0dd1		/* GeForce GTX 460M */
#define	PCI_PRODUCT_NVIDIA_GEFORCE_425M	0x0df0		/* GeForce 425M */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGTX550TI	0x1244		/* GeForce GTX 550 Ti */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGTS450_1	0x1245		/* GeForce GTS 450 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT550M	0x1246		/* GeForce GT 550M */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT635M	0x1247		/* GeForce GT 635M */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT555M_1	0x1248		/* GeForce GT 555M */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGTS450_2	0x1249		/* GeForce GTS 450 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT640_1	0x124b		/* GeForce GT 640 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT555M_2	0x124d		/* GeForce GT 555M */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGTX560M	0x1251		/* GeForce GTX 560M */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT635	0x1280		/* GeForce GT 635 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT710	0x1281		/* GeForce GT 710 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT640_2	0x1282		/* GeForce GT 640 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT630	0x1284		/* GeForce GT 630 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT730M_1	0x1290		/* GeForce GT 730M */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT735M	0x1291		/* GeForce GT 735M */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT740M	0x1292		/* GeForce GT 740M */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT730M_2	0x1293		/* GeForce GT 730M */
#define	PCI_PRODUCT_NVIDIA_GEFORCE825M	0x1296		/* GeForce 825M */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGT720M	0x1298		/* GeForce GT 720M */
#define	PCI_PRODUCT_NVIDIA_GEFORCE940MX	0x134d		/* GeForce 940MX */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGTX750TI	0x1380		/* GeForce GTX 750 Ti */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGTX750	0x1381		/* GeForce GTX 750 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGTX745	0x1382		/* GeForce GTX 745 */
#define	PCI_PRODUCT_NVIDIA_QUADROM1200	0x13b6		/* Quadro M1200 */
#define	PCI_PRODUCT_NVIDIA_GEFORCEGTX1050M	0x1c8d		/* Geforce GTX 1050M */

/* O2 Micro */
#define	PCI_PRODUCT_O2MICRO_FIREWIRE	0x00f7		/* Firewire */
#define	PCI_PRODUCT_O2MICRO_OZ6729	0x6729		/* OZ6729 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ6730	0x673a		/* OZ6730 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ6922	0x6825		/* OZ6922 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ6832	0x6832		/* OZ6832 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ6836	0x6836		/* OZ6836/OZ6860 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ6872	0x6872		/* OZ68[17]2 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ6933	0x6933		/* OZ6933 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ6972	0x6972		/* OZ69[17]2 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ7110	0x7110		/* OZ711Mx Misc */
#define	PCI_PRODUCT_O2MICRO_OZ7113	0x7113		/* OZ711EC1 SmartCardBus */
#define	PCI_PRODUCT_O2MICRO_OZ7114	0x7114		/* OZ711M1 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ7120	0x7120		/* OZ711MP1 SDHC */
#define	PCI_PRODUCT_O2MICRO_OZ7130	0x7130		/* OZ711MP1 XDHC */
#define	PCI_PRODUCT_O2MICRO_OZ7134	0x7134		/* OZ711MP1 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ7135	0x7135		/* OZ711EZ1 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ7136	0x7136		/* OZ711SP1 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ7223	0x7223		/* OZ711E0 CardBus */
#define	PCI_PRODUCT_O2MICRO_OZ8621	0x8621		/* 0Z8621 SD/MMC */

/* Oak Technologies products */
#define	PCI_PRODUCT_OAKTECH_OTI1007	0x0107		/* OTI107 */

/* Olicom */
#define	PCI_PRODUCT_OLICOM_OC2325	0x0012		/* OC2325 */
#define	PCI_PRODUCT_OLICOM_OC2183	0x0013		/* OC2183 */
#define	PCI_PRODUCT_OLICOM_OC2326	0x0014		/* OC2326 */

/* Omega Micro products */
#define	PCI_PRODUCT_OMEGA_82C092G	0x1221		/* 82C092G */

/* OpenBSD VMM products */
#define	PCI_PRODUCT_OPENBSD_PCHB	0x0666		/* VMM Host */
#define	PCI_PRODUCT_OPENBSD_CONTROL	0x0777		/* VMM Control */

/* Opti products */
#define	PCI_PRODUCT_OPTI_82C557	0xc557		/* 82C557 Host */
#define	PCI_PRODUCT_OPTI_82C558	0xc558		/* 82C558 ISA */
#define	PCI_PRODUCT_OPTI_82C568	0xc568		/* 82C568 IDE */
#define	PCI_PRODUCT_OPTI_82C621	0xc621		/* 82C621 IDE */
#define	PCI_PRODUCT_OPTI_82C700	0xc700		/* 82C700 */
#define	PCI_PRODUCT_OPTI_82C701	0xc701		/* 82C701 */
#define	PCI_PRODUCT_OPTI_82C822	0xc822		/* 82C822 */
#define	PCI_PRODUCT_OPTI_82C861	0xc861		/* 82C861 */
#define	PCI_PRODUCT_OPTI_82D568	0xd568		/* 82D568 IDE */

/* Option products */
#define	PCI_PRODUCT_OPTION_F32	0x000c		/* 3G+ UMTS HSDPA */

/* Oxford/ VScom */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI010L	0x8001		/* 010L */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI100L	0x8010		/* 100L */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI110L	0x8011		/* 110L */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI200L	0x8020		/* 200L */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI210L	0x8021		/* 210L */
#define	PCI_PRODUCT_MOLEX_VSCOM_PCI400L	0x8040		/* 400L */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI800L	0x8080		/* 800L */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCIX10H	0xa000		/* x10H */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI100H	0xa001		/* 100H */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI800H_0	0xa003		/* 400H/800H */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI800H_1	0xa004		/* 800H */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI200H	0xa005		/* 200H */
#define	PCI_PRODUCT_OXFORD_VSCOM_PCI200HV2	0xe020		/* 200HV2 */
#define	PCI_PRODUCT_OXFORD2_VSCOM_PCI011H	0x8403		/* 011H */
#define	PCI_PRODUCT_OXFORD2_OX16PCI954	0x9501		/* OX16PCI954 */
#define	PCI_PRODUCT_OXFORD2_OX16PCI954K	0x9504		/* OX16PCI954K */
#define	PCI_PRODUCT_OXFORD2_EXSYS_EX41092	0x950a		/* Exsys EX-41092 */
#define	PCI_PRODUCT_OXFORD2_OXCB950	0x950b		/* OXCB950 */
#define	PCI_PRODUCT_OXFORD2_OXMPCI954	0x950c		/* OXmPCI954 */
#define	PCI_PRODUCT_OXFORD2_OXMPCI954D	0x9510		/* OXmPCI954 Disabled */
#define	PCI_PRODUCT_OXFORD2_EXSYS_EX41098	0x9511		/* Exsys EX-41098 */
#define	PCI_PRODUCT_OXFORD2_OX16PCI954P	0x9513		/* OX16PCI954 Parallel */
#define	PCI_PRODUCT_OXFORD2_OX16PCI952	0x9521		/* OX16PCI952 */
#define	PCI_PRODUCT_OXFORD2_OX16PCI952P	0x9523		/* OX16PCI952 Parallel */
#define	PCI_PRODUCT_OXFORD2_OXPCIE952	0xc110		/* OXPCIE952 Parallel */
#define	PCI_PRODUCT_OXFORD2_OXPCIE952S	0xc120		/* OXPCIE952 Serial */

/* Parallels products */
#define	PCI_PRODUCT_PARALLELS_TOOLS	0x1112		/* Tools */
#define	PCI_PRODUCT_PARALLELS_VIDEO	0x1121		/* Video */
#define	PCI_PRODUCT_PARALLELS2_VMCI	0x4000		/* VMCI */
#define	PCI_PRODUCT_PARALLELS2_VIDEO	0x4005		/* Video */

/* PC Tech products */
#define	PCI_PRODUCT_PCTECH_RZ1000	0x1000		/* RZ1000 */

/* PCTEL */
#define	PCI_PRODUCT_PCTEL_MICROMODEM56	0x7879		/* HSP MicroModem 56 */
#define	PCI_PRODUCT_PCTEL_MICROMODEM56_1	0x7892		/* HSP MicroModem 56 */

/* Pacific Data products */
#define	PCI_PRODUCT_PDC_QSTOR_SATA	0x2068		/* QStor SATA */

/* Packet Engines products */
#define	PCI_PRODUCT_PE_GNIC2	0x0911		/* PMC/GNIC2 */

/* Pericom products */
#define	PCI_PRODUCT_PERICOM_PI7C21P100	0x01a7		/* PI7C21P100 PCIX-PCIX */
#define	PCI_PRODUCT_PERICOM_PI7C9X2G404EL	0x2404		/* PI7C9X2G404EL PCIE */
#define	PCI_PRODUCT_PERICOM_PI7C9X2G608GP	0x2608		/* PI7C9X2G608GP PCIE */
#define	PCI_PRODUCT_PERICOM_PPB_1	0x8140		/* PCI-PCI */
#define	PCI_PRODUCT_PERICOM_PPB_2	0x8150		/* PCI-PCI */
#define	PCI_PRODUCT_PERICOM_PI7C9X111SL	0xe111		/* PI7C9X111SL PCI */
#define	PCI_PRODUCT_PERICOM_PI7C9X130	0xe130		/* PI7C9X130 PCIE-PCIX */

/* Perle */
#define	PCI_PRODUCT_PERLE_SPEED8_LE	0xb008		/* Speed8 LE */

/* Philips products */
#define	PCI_PRODUCT_PHILIPS_OHCI	0x1561		/* ISP156x USB */
#define	PCI_PRODUCT_PHILIPS_EHCI	0x1562		/* ISP156x USB */
#define	PCI_PRODUCT_PHILIPS_SAA7130	0x7130		/* SAA7130 TV */
#define	PCI_PRODUCT_PHILIPS_SAA7133	0x7133		/* SAA7133 TV */
#define	PCI_PRODUCT_PHILIPS_SAA7134	0x7134		/* SAA7134 TV */
#define	PCI_PRODUCT_PHILIPS_SAA7135	0x7135		/* SAA7135 TV */
#define	PCI_PRODUCT_PHILIPS_SAA7231	0x7231		/* SAA7231 TV */

/* Phison products */
#define	PCI_PRODUCT_PHISON_PS5000	0x5000		/* PS5000 */
#define	PCI_PRODUCT_PHISON_PS5021	0x5021		/* PS5021 */

/* Picopower */
#define	PCI_PRODUCT_PICOPOWER_PT80C826	0x0000		/* PT80C826 */
#define	PCI_PRODUCT_PICOPOWER_PT86C521	0x0001		/* PT86C521 */
#define	PCI_PRODUCT_PICOPOWER_PT86C523	0x0002		/* PT86C523 */
#define	PCI_PRODUCT_PICOPOWER_PC87550	0x0005		/* PC87550 */
#define	PCI_PRODUCT_PICOPOWER_PT86C523_2	0x8002		/* PT86C523_2 */

/* Pijnenburg */
#define	PCI_PRODUCT_PIJNENBURG_PCC_ISES	0x0001		/* PCC-ISES */
#define	PCI_PRODUCT_PIJNENBURG_PCWD_PCI	0x5030		/* PCI PC WD */

/* Planex products */
#define	PCI_PRODUCT_PLANEX_FNW_3603_TX	0xab06		/* FNW-3603-TX */
#define	PCI_PRODUCT_PLANEX_FNW_3800_TX	0xab07		/* FNW-3800-TX */

/* Platform */
#define	PCI_PRODUCT_PLATFORM_ES1849	0x0100		/* ES1849 */

/* PLDA products */
#define	PCI_PRODUCT_PLDA_XR_AXI	0x1111		/* XpressRICH-AXI */

/* PLX products */
#define	PCI_PRODUCT_PLX_1076	0x1076		/* I/O 1076 */
#define	PCI_PRODUCT_PLX_1077	0x1077		/* I/O 1077 */
#define	PCI_PRODUCT_PLX_PCI_6520	0x6520		/* PCI 6520 */
#define	PCI_PRODUCT_PLX_PEX_8111	0x8111		/* PEX 8111 */
#define	PCI_PRODUCT_PLX_PEX_8112	0x8112		/* PEX 8112 */
#define	PCI_PRODUCT_PLX_PEX_8114	0x8114		/* PEX 8114 */
#define	PCI_PRODUCT_PLX_PEX_8517	0x8517		/* PEX 8517 */
#define	PCI_PRODUCT_PLX_PEX_8518	0x8518		/* PEX 8518 */
#define	PCI_PRODUCT_PLX_PEX_8524	0x8524		/* PEX 8524 */
#define	PCI_PRODUCT_PLX_PEX_8525	0x8525		/* PEX 8525 */
#define	PCI_PRODUCT_PLX_PEX_8532	0x8532		/* PEX 8532 */
#define	PCI_PRODUCT_PLX_PEX_8533	0x8533		/* PEX 8533 */
#define	PCI_PRODUCT_PLX_PEX_8547	0x8547		/* PEX 8547 */
#define	PCI_PRODUCT_PLX_PEX_8548	0x8548		/* PEX 8548 */
#define	PCI_PRODUCT_PLX_PEX_8603	0x8603		/* PEX 8603 */
#define	PCI_PRODUCT_PLX_PEX_8605	0x8605		/* PEX 8605 */
#define	PCI_PRODUCT_PLX_PEX_8608	0x8608		/* PEX 8608 */
#define	PCI_PRODUCT_PLX_PEX_8612	0x8612		/* PEX 8612 */
#define	PCI_PRODUCT_PLX_PEX_8613	0x8613		/* PEX 8613 */
#define	PCI_PRODUCT_PLX_PEX_8614	0x8614		/* PEX 8614 */
#define	PCI_PRODUCT_PLX_PEX_8616	0x8616		/* PEX 8616 */
#define	PCI_PRODUCT_PLX_PEX_8624	0x8624		/* PEX 8624 */
#define	PCI_PRODUCT_PLX_PEX_8632	0x8632		/* PEX 8632 */
#define	PCI_PRODUCT_PLX_PEX_8648	0x8648		/* PEX 8648 */
#define	PCI_PRODUCT_PLX_PEX_8717	0x8717		/* PEX 8717 */
#define	PCI_PRODUCT_PLX_PEX_8718	0x8718		/* PEX 8718 */
#define	PCI_PRODUCT_PLX_PEX_8724	0x8724		/* PEX 8724 */
#define	PCI_PRODUCT_PLX_PEX_8732	0x8732		/* PEX 8732 */
#define	PCI_PRODUCT_PLX_PEX_8733	0x8733		/* PEX 8733 */
#define	PCI_PRODUCT_PLX_PEX_8734	0x8734		/* PEX 8734 */
#define	PCI_PRODUCT_PLX_PEX_8780	0x8780		/* PEX 8780 */
#define	PCI_PRODUCT_PLX_9016	0x9016		/* I/O 9016 */
#define	PCI_PRODUCT_PLX_9050	0x9050		/* I/O 9050 */
#define	PCI_PRODUCT_PLX_9080	0x9080		/* I/O 9080 */
#define	PCI_PRODUCT_PLX_PEX_9733	0x9733		/* PEX 9733 */
#define	PCI_PRODUCT_PLX_CRONYX_OMEGA	0xc001		/* Cronyx Omega */

/* Promise products */
#define	PCI_PRODUCT_PROMISE_PDC20265	0x0d30		/* PDC20265 */
#define	PCI_PRODUCT_PROMISE_PDC20263	0x0d38		/* PDC20263 */
#define	PCI_PRODUCT_PROMISE_PDC20275	0x1275		/* PDC20275 */
#define	PCI_PRODUCT_PROMISE_PDC20318	0x3318		/* PDC20318 */
#define	PCI_PRODUCT_PROMISE_PDC20319	0x3319		/* PDC20319 */
#define	PCI_PRODUCT_PROMISE_PDC20371	0x3371		/* PDC20371 */
#define	PCI_PRODUCT_PROMISE_PDC20379	0x3372		/* PDC20379 */
#define	PCI_PRODUCT_PROMISE_PDC20378	0x3373		/* PDC20378 */
#define	PCI_PRODUCT_PROMISE_PDC20375	0x3375		/* PDC20375 */
#define	PCI_PRODUCT_PROMISE_PDC20376	0x3376		/* PDC20376 */
#define	PCI_PRODUCT_PROMISE_PDC20377	0x3377		/* PDC20377 */
#define	PCI_PRODUCT_PROMISE_PDC40719	0x3515		/* PDC40719 */
#define	PCI_PRODUCT_PROMISE_PDC40519	0x3519		/* PDC40519 */
#define	PCI_PRODUCT_PROMISE_PDC20771	0x3570		/* PDC20771 */
#define	PCI_PRODUCT_PROMISE_PDC20571	0x3571		/* PDC20571 */
#define	PCI_PRODUCT_PROMISE_PDC20579	0x3574		/* PDC20579 */
#define	PCI_PRODUCT_PROMISE_PDC40779	0x3577		/* PDC40779 */
#define	PCI_PRODUCT_PROMISE_PDC40718	0x3d17		/* PDC40718 */
#define	PCI_PRODUCT_PROMISE_PDC40518	0x3d18		/* PDC40518 */
#define	PCI_PRODUCT_PROMISE_PDC20775	0x3d73		/* PDC20775 */
#define	PCI_PRODUCT_PROMISE_PDC20575	0x3d75		/* PDC20575 */
#define	PCI_PRODUCT_PROMISE_PDC42819	0x3f20		/* PDC42819 */
#define	PCI_PRODUCT_PROMISE_PDC20267	0x4d30		/* PDC20267 */
#define	PCI_PRODUCT_PROMISE_PDC20246	0x4d33		/* PDC20246 */
#define	PCI_PRODUCT_PROMISE_PDC20262	0x4d38		/* PDC20262 */
#define	PCI_PRODUCT_PROMISE_PDC20268	0x4d68		/* PDC20268 */
#define	PCI_PRODUCT_PROMISE_PDC20269	0x4d69		/* PDC20269 */
#define	PCI_PRODUCT_PROMISE_PDC20276	0x5275		/* PDC20276 */
#define	PCI_PRODUCT_PROMISE_DC5030	0x5300		/* DC5030 */
#define	PCI_PRODUCT_PROMISE_PDC20268R	0x6268		/* PDC20268R */
#define	PCI_PRODUCT_PROMISE_PDC20271	0x6269		/* PDC20271 */
#define	PCI_PRODUCT_PROMISE_PDC20617	0x6617		/* PDC20617 */
#define	PCI_PRODUCT_PROMISE_PDC20620	0x6620		/* PDC20620 */
#define	PCI_PRODUCT_PROMISE_PDC20621	0x6621		/* PDC20621 */
#define	PCI_PRODUCT_PROMISE_PDC20618	0x6626		/* PDC20618 */
#define	PCI_PRODUCT_PROMISE_PDC20619	0x6629		/* PDC20619 */
#define	PCI_PRODUCT_PROMISE_PDC20277	0x7275		/* PDC20277 */

/* QLogic products */
#define	PCI_PRODUCT_QLOGIC_ISP10160	0x1016		/* ISP10160 */
#define	PCI_PRODUCT_QLOGIC_ISP1020	0x1020		/* ISP1020 */
#define	PCI_PRODUCT_QLOGIC_ISP1022	0x1022		/* ISP1022 */
#define	PCI_PRODUCT_QLOGIC_ISP1080	0x1080		/* ISP1080 */
#define	PCI_PRODUCT_QLOGIC_ISP12160	0x1216		/* ISP12160 */
#define	PCI_PRODUCT_QLOGIC_ISP1240	0x1240		/* ISP1240 */
#define	PCI_PRODUCT_QLOGIC_ISP1280	0x1280		/* ISP1280 */
#define	PCI_PRODUCT_QLOGIC_ISP2100	0x2100		/* ISP2100 */
#define	PCI_PRODUCT_QLOGIC_ISP2200	0x2200		/* ISP2200 */
#define	PCI_PRODUCT_QLOGIC_ISP2300	0x2300		/* ISP2300 */
#define	PCI_PRODUCT_QLOGIC_ISP2312	0x2312		/* ISP2312 */
#define	PCI_PRODUCT_QLOGIC_ISP2322	0x2322		/* ISP2322 */
#define	PCI_PRODUCT_QLOGIC_ISP2422	0x2422		/* ISP2422 */
#define	PCI_PRODUCT_QLOGIC_ISP2432	0x2432		/* ISP2432 */
#define	PCI_PRODUCT_QLOGIC_ISP2512	0x2512		/* ISP2512 */
#define	PCI_PRODUCT_QLOGIC_ISP2522	0x2522		/* ISP2522 */
#define	PCI_PRODUCT_QLOGIC_ISP2532	0x2532		/* ISP2532 */
#define	PCI_PRODUCT_QLOGIC_ISP4010_TOE	0x3010		/* ISP4010 iSCSI TOE */
#define	PCI_PRODUCT_QLOGIC_ISP4022_TOE	0x3022		/* ISP4022 iSCSI TOE */
#define	PCI_PRODUCT_QLOGIC_ISP4032_TOE	0x3032		/* ISP4032 iSCSI TOE */
#define	PCI_PRODUCT_QLOGIC_ISP4010_HBA	0x4010		/* ISP4010 iSCSI HBA */
#define	PCI_PRODUCT_QLOGIC_ISP4022_HBA	0x4022		/* ISP4022 iSCSI HBA */
#define	PCI_PRODUCT_QLOGIC_ISP4032_HBA	0x4032		/* ISP4032 iSCSI HBA */
#define	PCI_PRODUCT_QLOGIC_ISP5422	0x5422		/* ISP5422 */
#define	PCI_PRODUCT_QLOGIC_ISP5432	0x5432		/* ISP5432 */
#define	PCI_PRODUCT_QLOGIC_ISP6312	0x6312		/* ISP6312 */
#define	PCI_PRODUCT_QLOGIC_ISP6322	0x6322		/* ISP6322 */
#define	PCI_PRODUCT_QLOGIC_ISP8432	0x8432		/* ISP8432 */

/* Qualcomm products */
#define	PCI_PRODUCT_QUALCOMM_SC8280XP_PCIE	0x010e		/* SC8280XP PCIe */
#define	PCI_PRODUCT_QUALCOMM_X1E80100_PCIE	0x0111		/* X1E80100 PCIe */
#define	PCI_PRODUCT_QUALCOMM_QCNFA765	0x1103		/* QCNFA765 */
#define	PCI_PRODUCT_QUALCOMM_WCN7850	0x1107		/* WCN7850 */

/* Quancom products */
#define	PCI_PRODUCT_QUANCOM_PWDOG1	0x0010		/* PWDOG1 */

/* Quantum Designs products */
#define	PCI_PRODUCT_QUANTUMDESIGNS_8500	0x0001		/* 8500 */
#define	PCI_PRODUCT_QUANTUMDESIGNS_8580	0x0002		/* 8580 */

/* Quectel products */
#define	PCI_PRODUCT_QUECTEL_EM120R_GL	0x1001		/* EM120R-GL */

/* Qumranet products */
#define	PCI_PRODUCT_QUMRANET_VIO_NET	0x1000		/* Virtio Network */
#define	PCI_PRODUCT_QUMRANET_VIO_BLOCK	0x1001		/* Virtio Storage */
#define	PCI_PRODUCT_QUMRANET_VIO_MEM	0x1002		/* Virtio Memory Balloon */
#define	PCI_PRODUCT_QUMRANET_VIO_CONS	0x1003		/* Virtio Console */
#define	PCI_PRODUCT_QUMRANET_VIO_SCSI	0x1004		/* Virtio SCSI */
#define	PCI_PRODUCT_QUMRANET_VIO_RNG	0x1005		/* Virtio RNG */
#define	PCI_PRODUCT_QUMRANET_VIO1_NET	0x1041		/* Virtio 1.x Network */
#define	PCI_PRODUCT_QUMRANET_VIO1_BLOCK	0x1042		/* Virtio 1.x Storage */
#define	PCI_PRODUCT_QUMRANET_VIO1_CONS	0x1043		/* Virtio 1.x Console */
#define	PCI_PRODUCT_QUMRANET_VIO1_RNG	0x1044		/* Virtio 1.x RNG */
#define	PCI_PRODUCT_QUMRANET_VIO1_MEM	0x1045		/* Virtio 1.x Memory Balloon */
#define	PCI_PRODUCT_QUMRANET_VIO1_SCSI	0x1048		/* Virtio 1.x SCSI */
#define	PCI_PRODUCT_QUMRANET_VIO1_GPU	0x1050		/* Virtio 1.x GPU */
#define	PCI_PRODUCT_QUMRANET_VIO1_INPUT	0x1052		/* Virtio 1.x Input */

/* Ralink Technology Corporation */
#define	PCI_PRODUCT_RALINK_RT2460A	0x0101		/* RT2460A */
#define	PCI_PRODUCT_RALINK_RT2560	0x0201		/* RT2560 */
#define	PCI_PRODUCT_RALINK_RT2561S	0x0301		/* RT2561S */
#define	PCI_PRODUCT_RALINK_RT2561	0x0302		/* RT2561 */
#define	PCI_PRODUCT_RALINK_RT2661	0x0401		/* RT2661 */
#define	PCI_PRODUCT_RALINK_RT2860	0x0601		/* RT2860 */
#define	PCI_PRODUCT_RALINK_RT2890	0x0681		/* RT2890 */
#define	PCI_PRODUCT_RALINK_RT2760	0x0701		/* RT2760 */
#define	PCI_PRODUCT_RALINK_RT2790	0x0781		/* RT2790 */
#define	PCI_PRODUCT_RALINK_RT3060	0x3060		/* RT3060 */
#define	PCI_PRODUCT_RALINK_RT3062	0x3062		/* RT3062 */
#define	PCI_PRODUCT_RALINK_RT3090	0x3090		/* RT3090 */
#define	PCI_PRODUCT_RALINK_RT3091	0x3091		/* RT3091 */
#define	PCI_PRODUCT_RALINK_RT3092	0x3092		/* RT3092 */
#define	PCI_PRODUCT_RALINK_RT3290	0x3290		/* RT3290 */
#define	PCI_PRODUCT_RALINK_RT3298	0x3298		/* Bluetooth */
#define	PCI_PRODUCT_RALINK_RT3562	0x3562		/* RT3562 */
#define	PCI_PRODUCT_RALINK_RT3592	0x3592		/* RT3592 */
#define	PCI_PRODUCT_RALINK_RT3593	0x3593		/* RT3593 */
#define	PCI_PRODUCT_RALINK_RT5360	0x5360		/* RT5360 */
#define	PCI_PRODUCT_RALINK_RT5390	0x5390		/* RT5390 */
#define	PCI_PRODUCT_RALINK_RT5392	0x5392		/* RT5392 */
#define	PCI_PRODUCT_RALINK_RT5390_1	0x539a		/* RT5390 */
#define	PCI_PRODUCT_RALINK_RT5390_2	0x539b		/* RT5390 */
#define	PCI_PRODUCT_RALINK_RT5390_3	0x539f		/* RT5390 */

/* Ross -> Pequr -> ServerWorks -> Broadcom ServerWorks products */
#define	PCI_PRODUCT_RCC_CMIC_LE	0x0000		/* CMIC-LE */
#define	PCI_PRODUCT_RCC_CNB20_LE	0x0005		/* CNB20-LE Host */
#define	PCI_PRODUCT_RCC_CNB20HE_1	0x0006		/* CNB20HE Host */
#define	PCI_PRODUCT_RCC_CNB20_LE_2	0x0007		/* CNB20-LE Host */
#define	PCI_PRODUCT_RCC_CNB20HE_2	0x0008		/* CNB20HE Host */
#define	PCI_PRODUCT_RCC_CNB20LE	0x0009		/* CNB20LE Host */
#define	PCI_PRODUCT_RCC_CIOB30	0x0010		/* CIOB30 */
#define	PCI_PRODUCT_RCC_CMIC_HE	0x0011		/* CMIC-HE */
#define	PCI_PRODUCT_RCC_CMIC_WS_GC_LE	0x0012		/* CMIC-WS Host */
#define	PCI_PRODUCT_RCC_CNB20_HE	0x0013		/* CNB20-HE Host */
#define	PCI_PRODUCT_RCC_CMIC_LE_GC_LE	0x0014		/* CNB20-HE Host */
#define	PCI_PRODUCT_RCC_CMIC_GC_1	0x0015		/* CMIC-GC Host */
#define	PCI_PRODUCT_RCC_CMIC_GC_2	0x0016		/* CMIC-GC Host */
#define	PCI_PRODUCT_RCC_GCNB_LE	0x0017		/* GCNB-LE Host */
#define	PCI_PRODUCT_RCC_HT_1000_PCI	0x0036		/* HT-1000 PCI */
#define	PCI_PRODUCT_RCC_CIOB_X2	0x0101		/* CIOB-X2 PCIX */
#define	PCI_PRODUCT_RCC_PCIE_PCIX	0x0103		/* PCIE-PCIX */
#define	PCI_PRODUCT_RCC_HT_1000_PCIX	0x0104		/* HT-1000 PCIX */
#define	PCI_PRODUCT_RCC_CIOB_E	0x0110		/* CIOB-E */
#define	PCI_PRODUCT_RCC_HT_2000_PCIX	0x0130		/* HT-2000 PCIX */
#define	PCI_PRODUCT_RCC_HT_2000_PCIE	0x0132		/* HT-2000 PCIE */
#define	PCI_PRODUCT_RCC_HT_2100_PCIE_1	0x0140		/* HT-2100 PCIE */
#define	PCI_PRODUCT_RCC_HT_2100_PCIE_2	0x0141		/* HT-2100 PCIE */
#define	PCI_PRODUCT_RCC_HT_2100_PCIE_3	0x0142		/* HT-2100 PCIE */
#define	PCI_PRODUCT_RCC_HT_2100_PCIE_5	0x0144		/* HT-2100 PCIE */
#define	PCI_PRODUCT_RCC_OSB4	0x0200		/* OSB4 */
#define	PCI_PRODUCT_RCC_CSB5	0x0201		/* CSB5 */
#define	PCI_PRODUCT_RCC_CSB6	0x0203		/* CSB6 */
#define	PCI_PRODUCT_RCC_HT_1000	0x0205		/* HT-1000 */
#define	PCI_PRODUCT_RCC_IDE	0x0210		/* IDE */
#define	PCI_PRODUCT_RCC_OSB4_IDE	0x0211		/* OSB4 IDE */
#define	PCI_PRODUCT_RCC_CSB5_IDE	0x0212		/* CSB5 IDE */
#define	PCI_PRODUCT_RCC_CSB6_RAID_IDE	0x0213		/* CSB6 RAID/IDE */
#define	PCI_PRODUCT_RCC_HT_1000_IDE	0x0214		/* HT-1000 IDE */
#define	PCI_PRODUCT_RCC_CSB6_IDE	0x0217		/* CSB6 IDE */
#define	PCI_PRODUCT_RCC_USB	0x0220		/* OSB4/CSB5 USB */
#define	PCI_PRODUCT_RCC_CSB6_USB	0x0221		/* CSB6 USB */
#define	PCI_PRODUCT_RCC_HT_1000_USB	0x0223		/* HT-1000 USB */
#define	PCI_PRODUCT_RCC_CSB5_LPC_1	0x0225		/* CSB5 LPC */
#define	PCI_PRODUCT_RCC_GCLE_2	0x0227		/* GCLE-2 Host */
#define	PCI_PRODUCT_RCC_CSB5_LPC_2	0x0230		/* CSB5 LPC */
#define	PCI_PRODUCT_RCC_HT_1000_LPC	0x0234		/* HT-1000 LPC */
#define	PCI_PRODUCT_RCC_K2_SATA	0x0240		/* K2 SATA */
#define	PCI_PRODUCT_RCC_FRODO4_SATA	0x0241		/* Frodo4 SATA */
#define	PCI_PRODUCT_RCC_FRODO8_SATA	0x0242		/* Frodo8 SATA */
#define	PCI_PRODUCT_RCC_HT_1000_SATA_1	0x024a		/* HT-1000 SATA */
#define	PCI_PRODUCT_RCC_HT_1000_SATA_2	0x024b		/* HT-1000 SATA */
#define	PCI_PRODUCT_RCC_HT_1100	0x0408		/* HT-1100 */
#define	PCI_PRODUCT_RCC_HT_1100_SATA_1	0x0410		/* HT-1100 SATA */
#define	PCI_PRODUCT_RCC_HT_1100_SATA_2	0x0411		/* HT-1100 SATA */

/* RDC products */
#define	PCI_PRODUCT_RDC_R1010_IDE	0x1010		/* R1010 IDE */
#define	PCI_PRODUCT_RDC_R1011_IDE	0x1011		/* R1011 IDE */
#define	PCI_PRODUCT_RDC_R1012_IDE	0x1012		/* R1012 IDE */
#define	PCI_PRODUCT_RDC_R1031_PCIE	0x1031		/* R1031 PCIe */
#define	PCI_PRODUCT_RDC_R1060_USBD	0x1060		/* R1060 USB */
#define	PCI_PRODUCT_RDC_R1070_CAN	0x1070		/* R1070 CAN */
#define	PCI_PRODUCT_RDC_R1331_MC	0x1331		/* R1331 MC */
#define	PCI_PRODUCT_RDC_R1710_SPI	0x1710		/* R1710 SPI */
#define	PCI_PRODUCT_RDC_M2010_VGA	0x2010		/* M2010 VGA */
#define	PCI_PRODUCT_RDC_M2015_VGA	0x2015		/* M2015 VGA */
#define	PCI_PRODUCT_RDC_R3010_HDA	0x3010		/* R3010 HDA */
#define	PCI_PRODUCT_RDC_R6011_SB	0x6011		/* R6011 SB */
#define	PCI_PRODUCT_RDC_R6021_HB	0x6021		/* R6021 Host */
#define	PCI_PRODUCT_RDC_R6023_HB	0x6023		/* R6023 Host */
#define	PCI_PRODUCT_RDC_R6025_HB	0x6025		/* R6025 Host */
#define	PCI_PRODUCT_RDC_R6031_ISA	0x6031		/* R6031 ISA */
#define	PCI_PRODUCT_RDC_R6035_ISA	0x6035		/* R6035 ISA */
#define	PCI_PRODUCT_RDC_R6036_ISA	0x6036		/* R6036 ISA */
#define	PCI_PRODUCT_RDC_R6040_ETHER	0x6040		/* R6040 Ethernet */
#define	PCI_PRODUCT_RDC_R6060_OHCI	0x6060		/* R6060 USB */
#define	PCI_PRODUCT_RDC_R6061_EHCI	0x6061		/* R6061 USB2 */

/* Realtek products */
#define	PCI_PRODUCT_REALTEK_E2500V2	0x2502		/* E2500 */
#define	PCI_PRODUCT_REALTEK_E2600	0x2600		/* E2600 */
#define	PCI_PRODUCT_REALTEK_E3000	0x3000		/* Killer E3000 */
#define	PCI_PRODUCT_REALTEK_RTS5208	0x5208		/* RTS5208 Card Reader */
#define	PCI_PRODUCT_REALTEK_RTS5209	0x5209		/* RTS5209 Card Reader */
#define	PCI_PRODUCT_REALTEK_RTS5227	0x5227		/* RTS5227 Card Reader */
#define	PCI_PRODUCT_REALTEK_RTS5229	0x5229		/* RTS5229 Card Reader */
#define	PCI_PRODUCT_REALTEK_RTS522A	0x522a		/* RTS522A Card Reader */
#define	PCI_PRODUCT_REALTEK_RTS5249	0x5249		/* RTS5249 Card Reader */
#define	PCI_PRODUCT_REALTEK_RTS525A	0x525a		/* RTS525A Card Reader */
#define	PCI_PRODUCT_REALTEK_RTS5260	0x5260		/* RTS5260 Card Reader */
#define	PCI_PRODUCT_REALTEK_RTL8402	0x5286		/* RTL8402 Card Reader */
#define	PCI_PRODUCT_REALTEK_RTL8411B	0x5287		/* RTL8411B Card Reader */
#define	PCI_PRODUCT_REALTEK_RTL8411	0x5289		/* RTL8411 Card Reader */
#define	PCI_PRODUCT_REALTEK_RT8029	0x8029		/* 8029 */
#define	PCI_PRODUCT_REALTEK_RT8139D	0x8039		/* 8139D */
#define	PCI_PRODUCT_REALTEK_RTL8125	0x8125		/* RTL8125 */
#define	PCI_PRODUCT_REALTEK_RTL8126	0x8126		/* RTL8126 */
#define	PCI_PRODUCT_REALTEK_RTL8127	0x8127		/* RTL8127 */
#define	PCI_PRODUCT_REALTEK_RT8129	0x8129		/* 8129 */
#define	PCI_PRODUCT_REALTEK_RT8101E	0x8136		/* 8101E */
#define	PCI_PRODUCT_REALTEK_RT8138	0x8138		/* 8138 */
#define	PCI_PRODUCT_REALTEK_RT8139	0x8139		/* 8139 */
#define	PCI_PRODUCT_REALTEK_RT8168_2	0x8161		/* 8168 */
#define	PCI_PRODUCT_REALTEK_RT8169SC	0x8167		/* 8169SC */
#define	PCI_PRODUCT_REALTEK_RT8168	0x8168		/* 8168 */
#define	PCI_PRODUCT_REALTEK_RT8169	0x8169		/* 8169 */
#define	PCI_PRODUCT_REALTEK_REALMANAGE_SERIAL_1	0x816a		/* RealManage Serial */
#define	PCI_PRODUCT_REALTEK_REALMANAGE_SERIAL_2	0x816b		/* RealManage Serial */
#define	PCI_PRODUCT_REALTEK_REALMANAGE_IPMI	0x816c		/* RealManage IPMI */
#define	PCI_PRODUCT_REALTEK_REALMANAGE_EHCI	0x816d		/* RealManage USB */
#define	PCI_PRODUCT_REALTEK_REALMANAGE_BMC	0x816e		/* RealManage BMC */
#define	PCI_PRODUCT_REALTEK_RTL8192SE	0x8172		/* 8192SE */
#define	PCI_PRODUCT_REALTEK_RTL8188CE	0x8176		/* 8188CE */
#define	PCI_PRODUCT_REALTEK_RTL8192CE	0x8178		/* RTL8192CE */
#define	PCI_PRODUCT_REALTEK_RTL8188EE	0x8179		/* 8188EE */
#define	PCI_PRODUCT_REALTEK_RT8180	0x8180		/* 8180 */
#define	PCI_PRODUCT_REALTEK_RT8185	0x8185		/* 8185 */
#define	PCI_PRODUCT_REALTEK_RTL8192EE	0x818b		/* RTL8192EE */
#define	PCI_PRODUCT_REALTEK_RTL8190P	0x8190		/* RTL8190P */
#define	PCI_PRODUCT_REALTEK_RTL8192E	0x8192		/* RTL8192E */
#define	PCI_PRODUCT_REALTEK_RTL8187SE	0x8199		/* 8187SE */
#define	PCI_PRODUCT_REALTEK_RTL8723AE	0x8723		/* 8723AE */
#define	PCI_PRODUCT_REALTEK_RTL8821AE	0x8821		/* 8821AE */
#define	PCI_PRODUCT_REALTEK_RTL8852AE	0x8852		/* 8852AE */
#define	PCI_PRODUCT_REALTEK_RTL8852AE_VT	0xa85a		/* 8852AE-VT */
#define	PCI_PRODUCT_REALTEK_RTL8723BE	0xb723		/* 8723BE */
#define	PCI_PRODUCT_REALTEK_RTL8822BE	0xb822		/* 8822BE */
#define	PCI_PRODUCT_REALTEK_RTL8852BE	0xb852		/* 8852BE */
#define	PCI_PRODUCT_REALTEK_RTL8852BE_2	0xb85b		/* 8852BE */
#define	PCI_PRODUCT_REALTEK_RTL8821CE	0xc821		/* 8821CE */
#define	PCI_PRODUCT_REALTEK_RTL8822CE	0xc822		/* 8822CE */
#define	PCI_PRODUCT_REALTEK_RTL8852CE	0xc852		/* 8852CE */

/* Red Hat products */
#define	PCI_PRODUCT_REDHAT_PPB	0x0001		/* Qemu PCI-PCI */
#define	PCI_PRODUCT_REDHAT_SERIAL	0x0002		/* Qemu Serial */
#define	PCI_PRODUCT_REDHAT_SERIAL2	0x0003		/* Qemu Serial 2x */
#define	PCI_PRODUCT_REDHAT_SERIAL4	0x0004		/* Qemu Serial 4x */
#define	PCI_PRODUCT_REDHAT_SDMMC	0x0007		/* SD/MMC */
#define	PCI_PRODUCT_REDHAT_HB	0x0008		/* Host */
#define	PCI_PRODUCT_REDHAT_PCIE	0x000c		/* PCIE */
#define	PCI_PRODUCT_REDHAT_XHCI	0x000d		/* xHCI */
#define	PCI_PRODUCT_REDHAT_PCI	0x000e		/* PCI */
#define	PCI_PRODUCT_REDHAT_NVME	0x0010		/* NVMe */
#define	PCI_PRODUCT_REDHAT_QXL	0x0100		/* QXL Video */

/* Rendition products */
#define	PCI_PRODUCT_RENDITION_V1000	0x0001		/* Verite 1000 */
#define	PCI_PRODUCT_RENDITION_V2X00	0x2000		/* Verite V2x00 */

/* Renesas products */
#define	PCI_PRODUCT_RENESAS_SH7757_PPB	0x0012		/* SH7757 PCIE-PCI */
#define	PCI_PRODUCT_RENESAS_SH7757_SW	0x0013		/* SH7757 PCIE Switch */
#define	PCI_PRODUCT_RENESAS_UPD720201_XHCI	0x0014		/* uPD720201 xHCI */
#define	PCI_PRODUCT_RENESAS_UPD720202_XHCI	0x0015		/* uPD720202 xHCI */
#define	PCI_PRODUCT_RENESAS_SH7758_PPB	0x001a		/* SH7758 PCIE-PCI */
#define	PCI_PRODUCT_RENESAS_SH7758_SW	0x001d		/* SH7758 PCIE Switch */

/* Rhino Equipment products */
#define	PCI_PRODUCT_RHINO_R1T1	0x0105		/* T1/E1/J1 */
#define	PCI_PRODUCT_RHINO_R4T1	0x0305		/* Quad T1/E1/J1 */
#define	PCI_PRODUCT_RHINO_R2T1	0x0605		/* Dual T1/E1/J1 */

/* RICOH products */
#define	PCI_PRODUCT_RICOH_RF5C465	0x0465		/* 5C465 CardBus */
#define	PCI_PRODUCT_RICOH_RF5C466	0x0466		/* 5C466 CardBus */
#define	PCI_PRODUCT_RICOH_RF5C475	0x0475		/* 5C475 CardBus */
#define	PCI_PRODUCT_RICOH_RF5C476	0x0476		/* 5C476 CardBus */
#define	PCI_PRODUCT_RICOH_RF5C477	0x0477		/* 5C477 CardBus */
#define	PCI_PRODUCT_RICOH_RF5C478	0x0478		/* 5C478 CardBus */
#define	PCI_PRODUCT_RICOH_R5C521	0x0521		/* 5C521 Firewire */
#define	PCI_PRODUCT_RICOH_R5C551	0x0551		/* 5C551 Firewire */
#define	PCI_PRODUCT_RICOH_RL5C552	0x0552		/* 5C552 Firewire */
#define	PCI_PRODUCT_RICOH_R5C592	0x0592		/* 5C592 Memory Stick */
#define	PCI_PRODUCT_RICOH_R5C822	0x0822		/* 5C822 SD/MMC */
#define	PCI_PRODUCT_RICOH_R5C832	0x0832		/* 5C832 Firewire */
#define	PCI_PRODUCT_RICOH_R5C843	0x0843		/* 5C843 MMC */
#define	PCI_PRODUCT_RICOH_R5C852	0x0852		/* 5C852 xD */
#define	PCI_PRODUCT_RICOH_R5U230	0xe230		/* 5U230 Memory Stick */
#define	PCI_PRODUCT_RICOH_R5U822	0xe822		/* 5U822 SD/MMC */
#define	PCI_PRODUCT_RICOH_R5U823	0xe823		/* 5U823 SD/MMC */
#define	PCI_PRODUCT_RICOH_R5U832	0xe832		/* 5U832 Firewire */
#define	PCI_PRODUCT_RICOH_R5U852	0xe852		/* 5U852 SD/MMC */

/* Rockchip products */
#define	PCI_PRODUCT_ROCKCHIP_RK3399	0x0100		/* RK3399 */
#define	PCI_PRODUCT_ROCKCHIP_RK3566	0x3566		/* RK3566 */
#define	PCI_PRODUCT_ROCKCHIP_RK3588	0x3588		/* RK3588 */

/* Rockwell products */
#define	PCI_PRODUCT_ROCKWELL_RS56SP_PCI11P1	0x2005		/* RS56/SP-PCI11P1 Modem */

/* Raspberry Pi products */
#define	PCI_PRODUCT_RPI_RP1	0x0001		/* RP1 */

/* S3 products */
#define	PCI_PRODUCT_S3_VIRGE	0x5631		/* ViRGE */
#define	PCI_PRODUCT_S3_TRIO32	0x8810		/* Trio32 */
#define	PCI_PRODUCT_S3_TRIO64	0x8811		/* Trio32/64 */
#define	PCI_PRODUCT_S3_AURORA64P	0x8812		/* Aurora64V+ */
#define	PCI_PRODUCT_S3_TRIO64UVP	0x8814		/* Trio64UV+ */
#define	PCI_PRODUCT_S3_VIRGE_VX	0x883d		/* ViRGE VX */
#define	PCI_PRODUCT_S3_868	0x8880		/* 868 */
#define	PCI_PRODUCT_S3_928	0x88b0		/* 86C928 */
#define	PCI_PRODUCT_S3_864_0	0x88c0		/* 86C864-0 */
#define	PCI_PRODUCT_S3_864_1	0x88c1		/* 86C864-1 */
#define	PCI_PRODUCT_S3_864_2	0x88c2		/* 86C864-2 */
#define	PCI_PRODUCT_S3_864_3	0x88c3		/* 86C864-3 */
#define	PCI_PRODUCT_S3_964_0	0x88d0		/* 86C964-0 */
#define	PCI_PRODUCT_S3_964_1	0x88d1		/* 86C964-1 */
#define	PCI_PRODUCT_S3_964_2	0x88d2		/* 86C964-2 */
#define	PCI_PRODUCT_S3_964_3	0x88d3		/* 86C964-3 */
#define	PCI_PRODUCT_S3_968_0	0x88f0		/* 86C968-0 */
#define	PCI_PRODUCT_S3_968_1	0x88f1		/* 86C968-1 */
#define	PCI_PRODUCT_S3_968_2	0x88f2		/* 86C968-2 */
#define	PCI_PRODUCT_S3_968_3	0x88f3		/* 86C968-3 */
#define	PCI_PRODUCT_S3_TRIO64V2_DX	0x8901		/* Trio64V2/DX */
#define	PCI_PRODUCT_S3_PLATO	0x8902		/* Plato */
#define	PCI_PRODUCT_S3_TRIO3D_AGP	0x8904		/* Trio3D AGP */
#define	PCI_PRODUCT_S3_VIRGE_DX_GX	0x8a01		/* ViRGE DX/GX */
#define	PCI_PRODUCT_S3_VIRGE_GX2	0x8a10		/* ViRGE GX2 */
#define	PCI_PRODUCT_S3_TRIO3_DX2	0x8a13		/* Trio3 DX2 */
#define	PCI_PRODUCT_S3_SAVAGE3D	0x8a20		/* Savage 3D */
#define	PCI_PRODUCT_S3_SAVAGE3D_M	0x8a21		/* Savage 3DM */
#define	PCI_PRODUCT_S3_SAVAGE4	0x8a22		/* Savage 4 */
#define	PCI_PRODUCT_S3_SAVAGE4_2	0x8a23		/* Savage 4 */
#define	PCI_PRODUCT_S3_PROSAVAGE_PM133	0x8a25		/* ProSavage PM133 */
#define	PCI_PRODUCT_S3_PROSAVAGE_KM133	0x8a26		/* ProSavage KM133 */
#define	PCI_PRODUCT_S3_VIRGE_MX	0x8c01		/* ViRGE MX */
#define	PCI_PRODUCT_S3_VIRGE_MXP	0x8c03		/* ViRGE MXP */
#define	PCI_PRODUCT_S3_SAVAGE_MXMV	0x8c10		/* Savage/MX-MV */
#define	PCI_PRODUCT_S3_SAVAGE_MX	0x8c11		/* Savage/MX */
#define	PCI_PRODUCT_S3_SAVAGE_IXMV	0x8c12		/* Savage/IX-MV */
#define	PCI_PRODUCT_S3_SAVAGE_IX	0x8c13		/* Savage/IX */
#define	PCI_PRODUCT_S3_SUPERSAVAGE_MX128	0x8c22		/* SuperSavage MX/128 */
#define	PCI_PRODUCT_S3_SUPERSAVAGE_MX64	0x8c24		/* SuperSavage MX/64 */
#define	PCI_PRODUCT_S3_SUPERSAVAGE_MX64C	0x8c26		/* SuperSavage MX/64C */
#define	PCI_PRODUCT_S3_SUPERSAVAGE_IX128SDR	0x8c2a		/* SuperSavage IX/128 SDR */
#define	PCI_PRODUCT_S3_SUPERSAVAGE_IX128DDR	0x8c2b		/* SuperSavage IX/128 DDR */
#define	PCI_PRODUCT_S3_SUPERSAVAGE_IX64SDR	0x8c2c		/* SuperSavage IX/64 SDR */
#define	PCI_PRODUCT_S3_SUPERSAVAGE_IX64DDR	0x8c2d		/* SuperSavage IX/64 DDR */
#define	PCI_PRODUCT_S3_SUPERSAVAGE_IXCSDR	0x8c2e		/* SuperSavage IX/C SDR */
#define	PCI_PRODUCT_S3_SUPERSAVAGE_IXCDDR	0x8c2f		/* SuperSavage IX/C DDR */
#define	PCI_PRODUCT_S3_TWISTER	0x8d01		/* Twister */
#define	PCI_PRODUCT_S3_TWISTER_K	0x8d02		/* Twister-K */
#define	PCI_PRODUCT_S3_PROSAVAGE_DDR	0x8d03		/* ProSavage DDR */
#define	PCI_PRODUCT_S3_PROSAVAGE_DDR_K	0x8d04		/* ProSavage DDR-K */
#define	PCI_PRODUCT_S3_SONICVIBES	0xca00		/* SonicVibes */

/* SafeNet products */
#define	PCI_PRODUCT_SAFENET_SAFEXCEL	0x1141		/* SafeXcel */

/* Samsung products */
#define	PCI_PRODUCT_SAMSUNG_SWL2210P	0xa000		/* MagicLAN SWL-2210P */
#define	PCI_PRODUCT_SAMSUNG2_S4LN053X01	0x1600		/* S4LN053X01 */
#define	PCI_PRODUCT_SAMSUNG2_XP941	0xa800		/* XP941 */
#define	PCI_PRODUCT_SAMSUNG2_SM951_AHCI	0xa801		/* SM951 AHCI */
#define	PCI_PRODUCT_SAMSUNG2_SM951_NVME	0xa802		/* SM951/PM951 */
#define	PCI_PRODUCT_SAMSUNG2_SM961_NVME	0xa804		/* SM961/PM961 */
#define	PCI_PRODUCT_SAMSUNG2_SM981_NVME	0xa808		/* SM981/PM981 */
#define	PCI_PRODUCT_SAMSUNG2_PM991_NVME	0xa809		/* PM991 */
#define	PCI_PRODUCT_SAMSUNG2_PM9A1_NVME	0xa80a		/* PM9A1 */
#define	PCI_PRODUCT_SAMSUNG2_PM9B1_NVME	0xa80b		/* PM9B1 */
#define	PCI_PRODUCT_SAMSUNG2_PM9C1_NVME	0xa80c		/* PM9C1 */
#define	PCI_PRODUCT_SAMSUNG2_PM9C1A_NVME	0xa80d		/* PM9C1a */
#define	PCI_PRODUCT_SAMSUNG2_NVME_171X	0xa820		/* NVMe */
#define	PCI_PRODUCT_SAMSUNG2_NVME_172X	0xa821		/* NVMe */
#define	PCI_PRODUCT_SAMSUNG2_NVME_172X_A_B	0xa822		/* NVMe */

/* SanDisk (Western Digital) */
#define	PCI_PRODUCT_SANDISK_WDSXXXG1X0C	0x5001		/* WD Black */
#define	PCI_PRODUCT_SANDISK_WDSXXXG2X0C	0x5002		/* WD Black */
#define	PCI_PRODUCT_SANDISK_PCSN520_1	0x5003		/* PC SN520 */
#define	PCI_PRODUCT_SANDISK_PCSN520_2	0x5004		/* PC SN520 */
#define	PCI_PRODUCT_SANDISK_PCSN520_3	0x5005		/* PC SN520 */
#define	PCI_PRODUCT_SANDISK_WDSXXXG3X0C	0x5006		/* WD Black */
#define	PCI_PRODUCT_SANDISK_PCSN530	0x5008		/* PC SN530 */
#define	PCI_PRODUCT_SANDISK_NVME_1	0x5009		/* NVMe */
#define	PCI_PRODUCT_SANDISK_SN850	0x5011		/* SN850 */
#define	PCI_PRODUCT_SANDISK_NVME_2	0x5014		/* NVMe */
#define	PCI_PRODUCT_SANDISK_PCSN740_1	0x5015		/* PC SN740 */
#define	PCI_PRODUCT_SANDISK_PCSN740_2	0x5016		/* PC SN740 */
#define	PCI_PRODUCT_SANDISK_NVME_3	0x5017		/* NVMe */
#define	PCI_PRODUCT_SANDISK_SN750	0x501a		/* SN750 */
#define	PCI_PRODUCT_SANDISK_SN850X	0x5030		/* SN850X */
#define	PCI_PRODUCT_SANDISK_SN580	0x5041		/* SN580 */

/* Sangoma products */
#define	PCI_PRODUCT_SANGOMA_A10X	0x0300		/* A10x */

/* Schneider & Koch (SysKonnect) */
#define	PCI_PRODUCT_SCHNEIDERKOCH_FDDI	0x4000		/* FDDI */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK98XX	0x4300		/* SK-98xx */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK98XX2	0x4320		/* SK-98xx v2.0 */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK9D21	0x4400		/* SK-9D21 */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK9SXX	0x9000		/* SK-9Sxx */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK9821	0x9821		/* SK-9821 */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK9843	0x9843		/* SK-9843 */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK9EXX	0x9e00		/* SK-9Exx */
#define	PCI_PRODUCT_SCHNEIDERKOCH_SK9E21M	0x9e01		/* SK-9E21M */

/* ServerEngines products */
#define	PCI_PRODUCT_SERVERENGINES_BE2	0x0211		/* BladeEngine2 10GbE */
#define	PCI_PRODUCT_SERVERENGINES_BE3	0x0221		/* BladeEngine3 10GbE */
#define	PCI_PRODUCT_SERVERENGINES_OCBE2	0x0700		/* BladeEngine2 10GbE */
#define	PCI_PRODUCT_SERVERENGINES_OCBE3	0x0710		/* BladeEngine3 10GbE */
#define	PCI_PRODUCT_SERVERENGINES_IRMC	0x0800		/* iRMC */

/* SGI products */
#define	PCI_PRODUCT_SGI_IOC3	0x0003		/* IOC3 */
#define	PCI_PRODUCT_SGI_RAD1	0x0005		/* Rad1 */
#define	PCI_PRODUCT_SGI_TIGON	0x0009		/* Tigon */
#define	PCI_PRODUCT_SGI_IOC4	0x100a		/* IOC4 */
#define	PCI_PRODUCT_SGI_IS1	0x100c		/* ImageSync 1 */

/* SGS Thomson products */
#define	PCI_PRODUCT_SGSTHOMSON_2000	0x0008		/* STG 2000X */
#define	PCI_PRODUCT_SGSTHOMSON_1764	0x0009		/* STG 1764 */
#define	PCI_PRODUCT_SGSTHOMSON_KYROII	0x0010		/* Kyro-II */
#define	PCI_PRODUCT_SGSTHOMSON_1764X	0x1746		/* STG 1764X */

/* SiFive products */
#define	PCI_PRODUCT_SIFIVE_PCIE	0x0000		/* PCIe */

/* Sigma Designs */
#define	PCI_PRODUCT_SIGMA_64GX	0x6401		/* 64GX */
#define	PCI_PRODUCT_SIGMA_DVDMAGICPRO	0x8300		/* DVDmagic-PRO */

/* SIIG products */
#define	PCI_PRODUCT_SIIG_1000	0x1000		/* I/O */
#define	PCI_PRODUCT_SIIG_1001	0x1001		/* I/O */
#define	PCI_PRODUCT_SIIG_1002	0x1002		/* I/O */
#define	PCI_PRODUCT_SIIG_1010	0x1010		/* I/O */
#define	PCI_PRODUCT_SIIG_1011	0x1011		/* I/O */
#define	PCI_PRODUCT_SIIG_1012	0x1012		/* I/O */
#define	PCI_PRODUCT_SIIG_1020	0x1020		/* I/O */
#define	PCI_PRODUCT_SIIG_1021	0x1021		/* I/O */
#define	PCI_PRODUCT_SIIG_1030	0x1030		/* I/O */
#define	PCI_PRODUCT_SIIG_1031	0x1031		/* I/O */
#define	PCI_PRODUCT_SIIG_1032	0x1032		/* I/O */
#define	PCI_PRODUCT_SIIG_1034	0x1034		/* I/O */
#define	PCI_PRODUCT_SIIG_1035	0x1035		/* I/O */
#define	PCI_PRODUCT_SIIG_1036	0x1036		/* I/O */
#define	PCI_PRODUCT_SIIG_1050	0x1050		/* I/O */
#define	PCI_PRODUCT_SIIG_1051	0x1051		/* I/O */
#define	PCI_PRODUCT_SIIG_1052	0x1052		/* I/O */
#define	PCI_PRODUCT_SIIG_2000	0x2000		/* I/O */
#define	PCI_PRODUCT_SIIG_2001	0x2001		/* I/O */
#define	PCI_PRODUCT_SIIG_2002	0x2002		/* I/O */
#define	PCI_PRODUCT_SIIG_2010	0x2010		/* I/O */
#define	PCI_PRODUCT_SIIG_2011	0x2011		/* I/O */
#define	PCI_PRODUCT_SIIG_2012	0x2012		/* I/O */
#define	PCI_PRODUCT_SIIG_2020	0x2020		/* I/O */
#define	PCI_PRODUCT_SIIG_2021	0x2021		/* I/O */
#define	PCI_PRODUCT_SIIG_2030	0x2030		/* I/O */
#define	PCI_PRODUCT_SIIG_2031	0x2031		/* I/O */
#define	PCI_PRODUCT_SIIG_2032	0x2032		/* I/O */
#define	PCI_PRODUCT_SIIG_2040	0x2040		/* I/O */
#define	PCI_PRODUCT_SIIG_2041	0x2041		/* I/O */
#define	PCI_PRODUCT_SIIG_2042	0x2042		/* I/O */
#define	PCI_PRODUCT_SIIG_2050	0x2050		/* I/O */
#define	PCI_PRODUCT_SIIG_2051	0x2051		/* I/O */
#define	PCI_PRODUCT_SIIG_2052	0x2052		/* I/O */
#define	PCI_PRODUCT_SIIG_2060	0x2060		/* I/O */
#define	PCI_PRODUCT_SIIG_2061	0x2061		/* I/O */
#define	PCI_PRODUCT_SIIG_2062	0x2062		/* I/O */
#define	PCI_PRODUCT_SIIG_2081	0x2081		/* I/O */
#define	PCI_PRODUCT_SIIG_2082	0x2082		/* I/O */

/* Silan products */
#define	PCI_PRODUCT_SILAN_SC92301	0x2301		/* SC92301 */
#define	PCI_PRODUCT_SILAN_8139D	0x8139		/* 8139D */

/* Silicon Integrated System products */
#define	PCI_PRODUCT_SIS_86C201	0x0001		/* 86C201 AGP */
#define	PCI_PRODUCT_SIS_86C202	0x0002		/* 86C202 AGP */
#define	PCI_PRODUCT_SIS_648FX	0x0003		/* 648FX AGP */
#define	PCI_PRODUCT_SIS_PPB_1	0x0004		/* PCI-PCI */
#define	PCI_PRODUCT_SIS_86C205_1	0x0005		/* 86C205 */
#define	PCI_PRODUCT_SIS_85C503	0x0008		/* 85C503 System */
#define	PCI_PRODUCT_SIS_5595	0x0009		/* 5595 System */
#define	PCI_PRODUCT_SIS_PPB_2	0x000a		/* PCI-PCI */
#define	PCI_PRODUCT_SIS_85C503_ISA	0x0018		/* 85C503 ISA */
#define	PCI_PRODUCT_SIS_180	0x0180		/* 180 SATA */
#define	PCI_PRODUCT_SIS_181	0x0181		/* 181 SATA */
#define	PCI_PRODUCT_SIS_182	0x0182		/* 182 SATA */
#define	PCI_PRODUCT_SIS_183	0x0183		/* 183 SATA */
#define	PCI_PRODUCT_SIS_190	0x0190		/* 190 */
#define	PCI_PRODUCT_SIS_191	0x0191		/* 191 */
#define	PCI_PRODUCT_SIS_5597_VGA	0x0200		/* 5597/5598 VGA */
#define	PCI_PRODUCT_SIS_6215	0x0204		/* 6215 */
#define	PCI_PRODUCT_SIS_86C205_2	0x0205		/* 86C205 */
#define	PCI_PRODUCT_SIS_300	0x0300		/* 300/305/630 VGA */
#define	PCI_PRODUCT_SIS_315PRO_VGA	0x0325		/* 315 Pro VGA */
#define	PCI_PRODUCT_SIS_85C501	0x0406		/* 85C501 */
#define	PCI_PRODUCT_SIS_85C496	0x0496		/* 85C496 */
#define	PCI_PRODUCT_SIS_530	0x0530		/* 530 PCI */
#define	PCI_PRODUCT_SIS_540	0x0540		/* 540 PCI */
#define	PCI_PRODUCT_SIS_550	0x0550		/* 550 PCI */
#define	PCI_PRODUCT_SIS_85C596	0x0596		/* 85C596 */
#define	PCI_PRODUCT_SIS_85C601	0x0601		/* 85C601 EIDE */
#define	PCI_PRODUCT_SIS_620	0x0620		/* 620 PCI */
#define	PCI_PRODUCT_SIS_630	0x0630		/* 630 PCI */
#define	PCI_PRODUCT_SIS_633	0x0633		/* 633 PCI */
#define	PCI_PRODUCT_SIS_635	0x0635		/* 635 PCI */
#define	PCI_PRODUCT_SIS_640	0x0640		/* 640 PCI */
#define	PCI_PRODUCT_SIS_645	0x0645		/* 645 PCI */
#define	PCI_PRODUCT_SIS_646	0x0646		/* 646 PCI */
#define	PCI_PRODUCT_SIS_648	0x0648		/* 648 PCI */
#define	PCI_PRODUCT_SIS_649	0x0649		/* 649 PCI */
#define	PCI_PRODUCT_SIS_650	0x0650		/* 650 PCI */
#define	PCI_PRODUCT_SIS_651	0x0651		/* 651 PCI */
#define	PCI_PRODUCT_SIS_652	0x0652		/* 652 PCI */
#define	PCI_PRODUCT_SIS_655	0x0655		/* 655 PCI */
#define	PCI_PRODUCT_SIS_656	0x0656		/* 656 PCI */
#define	PCI_PRODUCT_SIS_658	0x0658		/* 658 PCI */
#define	PCI_PRODUCT_SIS_661	0x0661		/* 661 PCI */
#define	PCI_PRODUCT_SIS_662	0x0662		/* 662 PCI */
#define	PCI_PRODUCT_SIS_671	0x0671		/* 671 PCI */
#define	PCI_PRODUCT_SIS_730	0x0730		/* 730 PCI */
#define	PCI_PRODUCT_SIS_733	0x0733		/* 733 PCI */
#define	PCI_PRODUCT_SIS_735	0x0735		/* 735 PCI */
#define	PCI_PRODUCT_SIS_740	0x0740		/* 740 PCI */
#define	PCI_PRODUCT_SIS_741	0x0741		/* 741 PCI */
#define	PCI_PRODUCT_SIS_745	0x0745		/* 745 PCI */
#define	PCI_PRODUCT_SIS_746	0x0746		/* 746 PCI */
#define	PCI_PRODUCT_SIS_748	0x0748		/* 748 PCI */
#define	PCI_PRODUCT_SIS_750	0x0750		/* 750 PCI */
#define	PCI_PRODUCT_SIS_751	0x0751		/* 751 PCI */
#define	PCI_PRODUCT_SIS_752	0x0752		/* 752 PCI */
#define	PCI_PRODUCT_SIS_755	0x0755		/* 755 PCI */
#define	PCI_PRODUCT_SIS_756	0x0756		/* 756 PCI */
#define	PCI_PRODUCT_SIS_760	0x0760		/* 760 PCI */
#define	PCI_PRODUCT_SIS_761	0x0761		/* 761 PCI */
#define	PCI_PRODUCT_SIS_900	0x0900		/* 900 */
#define	PCI_PRODUCT_SIS_961	0x0961		/* 961 ISA */
#define	PCI_PRODUCT_SIS_962	0x0962		/* 962 ISA */
#define	PCI_PRODUCT_SIS_963	0x0963		/* 963 ISA */
#define	PCI_PRODUCT_SIS_964	0x0964		/* 964 ISA */
#define	PCI_PRODUCT_SIS_965	0x0965		/* 965 ISA */
#define	PCI_PRODUCT_SIS_966	0x0966		/* 966 ISA */
#define	PCI_PRODUCT_SIS_968	0x0968		/* 968 ISA */
#define	PCI_PRODUCT_SIS_1182	0x1182		/* 1182 SATA */
#define	PCI_PRODUCT_SIS_1183	0x1183		/* 1183 SATA */
#define	PCI_PRODUCT_SIS_1184	0x1184		/* 1184 RAID */
#define	PCI_PRODUCT_SIS_1185	0x1185		/* 1185 AHCI */
#define	PCI_PRODUCT_SIS_5300	0x5300		/* 540 VGA */
#define	PCI_PRODUCT_SIS_5315	0x5315		/* 530 VGA */
#define	PCI_PRODUCT_SIS_5511	0x5511		/* 5511 */
#define	PCI_PRODUCT_SIS_5512	0x5512		/* 5512 */
#define	PCI_PRODUCT_SIS_5513	0x5513		/* 5513 EIDE */
#define	PCI_PRODUCT_SIS_5518	0x5518		/* 5518 EIDE */
#define	PCI_PRODUCT_SIS_5571	0x5571		/* 5571 PCI */
#define	PCI_PRODUCT_SIS_5581	0x5581		/* 5581 */
#define	PCI_PRODUCT_SIS_5582	0x5582		/* 5582 */
#define	PCI_PRODUCT_SIS_5591	0x5591		/* 5591 PCI */
#define	PCI_PRODUCT_SIS_5596	0x5596		/* 5596 */
#define	PCI_PRODUCT_SIS_5597_HB	0x5597		/* 5597/5598 Host */
#define	PCI_PRODUCT_SIS_6204	0x6204		/* 6204 */
#define	PCI_PRODUCT_SIS_6205	0x6205		/* 6205 */
#define	PCI_PRODUCT_SIS_6300	0x6300		/* 6300 */
#define	PCI_PRODUCT_SIS_530_VGA	0x6306		/* 530 VGA */
#define	PCI_PRODUCT_SIS_650_VGA	0x6325		/* 650 VGA */
#define	PCI_PRODUCT_SIS_6326	0x6326		/* 6326 VGA */
#define	PCI_PRODUCT_SIS_6330	0x6330		/* 6330 VGA */
#define	PCI_PRODUCT_SIS_5597_USB	0x7001		/* 5597/5598 USB */
#define	PCI_PRODUCT_SIS_7002	0x7002		/* 7002 USB */
#define	PCI_PRODUCT_SIS_7007	0x7007		/* 7007 FireWire */
#define	PCI_PRODUCT_SIS_7012_ACA	0x7012		/* 7012 AC97 */
#define	PCI_PRODUCT_SIS_7013	0x7013		/* 7013 Modem */
#define	PCI_PRODUCT_SIS_7016	0x7016		/* 7016 */
#define	PCI_PRODUCT_SIS_7018	0x7018		/* 7018 Audio */
#define	PCI_PRODUCT_SIS_7019	0x7019		/* 7019 Audio */
#define	PCI_PRODUCT_SIS_7300	0x7300		/* 7300 VGA */
#define	PCI_PRODUCT_SIS_966_HDA	0x7502		/* 966 HD Audio */

/* SK hynix products */
#define	PCI_PRODUCT_SKHYNIX_BC501	0x1327		/* BC501 */
#define	PCI_PRODUCT_SKHYNIX_PC601	0x1627		/* PC601 */
#define	PCI_PRODUCT_SKHYNIX_SHGP31	0x174a		/* Gold P31 */
#define	PCI_PRODUCT_SKHYNIX_SHPP41	0x1959		/* Platinum P41 */
#define	PCI_PRODUCT_SKHYNIX_BC901	0x1d59		/* BC901 */
#define	PCI_PRODUCT_SKHYNIX_PE8000	0x2839		/* PE8000 */

/* SMC products */
#define	PCI_PRODUCT_SMC_83C170	0x0005		/* 83C170 */
#define	PCI_PRODUCT_SMC_83C175	0x0006		/* 83C175 */
#define	PCI_PRODUCT_SMC_37C665	0x1000		/* FDC 37C665 */
#define	PCI_PRODUCT_SMC_37C922	0x1001		/* FDC 37C922 */

/* Silicon Motion products */
#define	PCI_PRODUCT_SMI_SM501	0x0501		/* Voyager GX */
#define	PCI_PRODUCT_SMI_SM710	0x0710		/* LynxEM */
#define	PCI_PRODUCT_SMI_SM712	0x0712		/* LynxEM+ */
#define	PCI_PRODUCT_SMI_SM720	0x0720		/* Lynx3DM */
#define	PCI_PRODUCT_SMI_SM810	0x0810		/* LynxE */
#define	PCI_PRODUCT_SMI_SM811	0x0811		/* LynxE+ */
#define	PCI_PRODUCT_SMI_SM820	0x0820		/* Lynx3D */
#define	PCI_PRODUCT_SMI_SM910	0x0910		/* 910 */
#define	PCI_PRODUCT_SMI_SM2260	0x2260		/* SM2260 */

/* SMSC products */
#define	PCI_PRODUCT_SMSC_VICTORY66_IDE_1	0x9130		/* Victory66 IDE */
#define	PCI_PRODUCT_SMSC_VICTORY66_ISA	0x9460		/* Victory66 ISA */
#define	PCI_PRODUCT_SMSC_VICTORY66_IDE_2	0x9461		/* Victory66 IDE */
#define	PCI_PRODUCT_SMSC_VICTORY66_USB	0x9462		/* Victory66 USB */
#define	PCI_PRODUCT_SMSC_VICTORY66_PM	0x9463		/* Victory66 Power */

/* SNI products */
#define	PCI_PRODUCT_SNI_PIRAHNA	0x0002		/* Pirahna 2-port */
#define	PCI_PRODUCT_SNI_TCPMSE	0x0005		/* Tulip, power, switch extender */
#define	PCI_PRODUCT_SNI_FPGAIBUS	0x4942		/* FPGA I-Bus Tracer for MBD */
#define	PCI_PRODUCT_SNI_SZB6120	0x6120		/* SZB6120 */

/* Solarflare products */
#define	PCI_PRODUCT_SOLARFLARE_FALCON_P	0x0703		/* Falcon P */
#define	PCI_PRODUCT_SOLARFLARE_FALCON_S	0x6703		/* Falcon S */
#define	PCI_PRODUCT_SOLARFLARE_EF1002	0xc101		/* EF1002 */

/* Sony products */
#define	PCI_PRODUCT_SONY_CXD1947A	0x8009		/* CXD1947A FireWire */
#define	PCI_PRODUCT_SONY_CXD3222	0x8039		/* CXD3222 FireWire */
#define	PCI_PRODUCT_SONY_MEMSTICK_SLOT	0x808a		/* Memory Stick Slot */
#define	PCI_PRODUCT_SONY_RS780	0x9602		/* RS780 */

/* Solid State Storage Technology Corporation products */
#define	PCI_PRODUCT_SSSTC_CL1	0x9100		/* CL1 */

/* Stallion Technologies products */
#define	PCI_PRODUCT_STALLION_EASYIO	0x0003		/* EasyIO */

/* STB products */
#define	PCI_PRODUCT_STB2_RIVA128	0x0018		/* Velocity128 */

/* Sun */
#define	PCI_PRODUCT_SUN_EBUS	0x1000		/* PCIO EBus2 */
#define	PCI_PRODUCT_SUN_HME	0x1001		/* HME */
#define	PCI_PRODUCT_SUN_RIO_EBUS	0x1100		/* RIO EBus */
#define	PCI_PRODUCT_SUN_ERINETWORK	0x1101		/* ERI */
#define	PCI_PRODUCT_SUN_FIREWIRE	0x1102		/* FireWire */
#define	PCI_PRODUCT_SUN_USB	0x1103		/* USB */
#define	PCI_PRODUCT_SUN_GEMNETWORK	0x2bad		/* GEM */
#define	PCI_PRODUCT_SUN_SIMBA	0x5000		/* Simba */
#define	PCI_PRODUCT_SUN_5821	0x5454		/* Crypto 5821 */
#define	PCI_PRODUCT_SUN_SCA1K	0x5455		/* Crypto 1K */
#define	PCI_PRODUCT_SUN_SCA6K	0x5ca0		/* Crypto 6K */
#define	PCI_PRODUCT_SUN_PSYCHO	0x8000		/* Psycho PCI */
#define	PCI_PRODUCT_SUN_SPARC_T3_PCIE	0x8186		/* SPARC-T3/T4 PCIE */
#define	PCI_PRODUCT_SUN_SPARC_M7_PCIE	0x818e		/* SPARC-M7 PCIE */
#define	PCI_PRODUCT_SUN_SPARC_T5_PCIE	0x8196		/* SPARC-T5/M5/M6 PCIE */
#define	PCI_PRODUCT_SUN_MS_2EP	0x9000		/* microSPARC IIep PCI */
#define	PCI_PRODUCT_SUN_US_2I	0xa000		/* UltraSPARC IIi PCI */
#define	PCI_PRODUCT_SUN_US_2E	0xa001		/* UltraSPARC IIe PCI */
#define	PCI_PRODUCT_SUN_CASSINI	0xabba		/* Cassini */
#define	PCI_PRODUCT_SUN_NEPTUNE	0xabcd		/* Neptune */
#define	PCI_PRODUCT_SUN_SBBC	0xc416		/* SBBC */
#define	PCI_PRODUCT_SUN_SDIO	0xfa04		/* SDIO */
#define	PCI_PRODUCT_SUN_SDIO_PCIE	0xfa05		/* SDIO PCIE */

/* Sundance products */
#define	PCI_PRODUCT_SUNDANCE_ST201_1	0x0200		/* ST201 */
#define	PCI_PRODUCT_SUNDANCE_ST201_2	0x0201		/* ST201 */
#define	PCI_PRODUCT_SUNDANCE_TC9021	0x1021		/* TC9021 */
#define	PCI_PRODUCT_SUNDANCE_ST1023	0x1023		/* ST1023 */
#define	PCI_PRODUCT_SUNDANCE_ST2021	0x2021		/* ST2021 */
#define	PCI_PRODUCT_SUNDANCE_TC9021_ALT	0x9021		/* TC9021 */

/* Sunix */
#define	PCI_PRODUCT_SUNIX_40XX	0x7168		/* 40XX */
#define	PCI_PRODUCT_SUNIX_4018A	0x7268		/* 4018A */
#define	PCI_PRODUCT_SUNIX2_50XX	0x1999		/* 50XX */

/* Surecom products */
#define	PCI_PRODUCT_SURECOM_NE34	0x0e34		/* NE-34 */

/* Syba */
#define	PCI_PRODUCT_SYBA_4S2P	0x0781		/* 4S2P */
#define	PCI_PRODUCT_SYBA_4S	0x0786		/* 4S */

/* NCR/Symbios Logic/LSI products */
#define	PCI_PRODUCT_SYMBIOS_810	0x0001		/* 53c810 */
#define	PCI_PRODUCT_SYMBIOS_820	0x0002		/* 53c820 */
#define	PCI_PRODUCT_SYMBIOS_825	0x0003		/* 53c825 */
#define	PCI_PRODUCT_SYMBIOS_815	0x0004		/* 53c815 */
#define	PCI_PRODUCT_SYMBIOS_810AP	0x0005		/* 53c810AP */
#define	PCI_PRODUCT_SYMBIOS_860	0x0006		/* 53c860 */
#define	PCI_PRODUCT_SYMBIOS_1510D	0x000a		/* 53c1510D */
#define	PCI_PRODUCT_SYMBIOS_896	0x000b		/* 53c896 */
#define	PCI_PRODUCT_SYMBIOS_895	0x000c		/* 53c895 */
#define	PCI_PRODUCT_SYMBIOS_885	0x000d		/* 53c885 */
#define	PCI_PRODUCT_SYMBIOS_875	0x000f		/* 53c875 */
#define	PCI_PRODUCT_SYMBIOS_1510	0x0010		/* 53c1510 */
#define	PCI_PRODUCT_SYMBIOS_895A	0x0012		/* 53c895A */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_3516	0x0014		/* MegaRAID SAS3516 */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_3416	0x0015		/* MegaRAID SAS3416 */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_3508	0x0016		/* MegaRAID SAS3508 */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_3408	0x0017		/* MegaRAID SAS3408 */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_3504	0x001b		/* MegaRAID SAS3504 */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_3404	0x001c		/* MegaRAID SAS3404 */
#define	PCI_PRODUCT_SYMBIOS_1010	0x0020		/* 53c1010-33 */
#define	PCI_PRODUCT_SYMBIOS_1010_2	0x0021		/* 53c1010-66 */
#define	PCI_PRODUCT_SYMBIOS_1030	0x0030		/* 53c1030 */
#define	PCI_PRODUCT_SYMBIOS_1030ZC	0x0031		/* 53c1030ZC */
#define	PCI_PRODUCT_SYMBIOS_1030_1035	0x0032		/* 53c1035 */
#define	PCI_PRODUCT_SYMBIOS_1030ZC_1035	0x0033		/* 53c1035 */
#define	PCI_PRODUCT_SYMBIOS_1035	0x0040		/* 53c1035 */
#define	PCI_PRODUCT_SYMBIOS_1035ZC	0x0041		/* 53c1035ZC */
#define	PCI_PRODUCT_SYMBIOS_SAS1064	0x0050		/* SAS1064 */
#define	PCI_PRODUCT_SYMBIOS_SAS1068	0x0054		/* SAS1068 */
#define	PCI_PRODUCT_SYMBIOS_SAS1068_2	0x0055		/* SAS1068 */
#define	PCI_PRODUCT_SYMBIOS_SAS1064E	0x0056		/* SAS1064E */
#define	PCI_PRODUCT_SYMBIOS_SAS1064E_2	0x0057		/* SAS1064E */
#define	PCI_PRODUCT_SYMBIOS_SAS1068E	0x0058		/* SAS1068E */
#define	PCI_PRODUCT_SYMBIOS_SAS1068E_2	0x0059		/* SAS1068E */
#define	PCI_PRODUCT_SYMBIOS_SAS1066E	0x005a		/* SAS1066E */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_2208	0x005b		/* MegaRAID SAS2208 */
#define	PCI_PRODUCT_SYMBIOS_SAS1064A	0x005c		/* SAS1064A */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_3108	0x005d		/* MegaRAID SAS3108 */
#define	PCI_PRODUCT_SYMBIOS_SAS1066	0x005e		/* SAS1066 */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_3008	0x005f		/* MegaRAID SAS3008 */
#define	PCI_PRODUCT_SYMBIOS_SAS1078	0x0060		/* SAS1078 */
#define	PCI_PRODUCT_SYMBIOS_SAS1078_PCIE	0x0062		/* SAS1078 */
#define	PCI_PRODUCT_SYMBIOS_SAS2116_1	0x0064		/* SAS2116 */
#define	PCI_PRODUCT_SYMBIOS_SAS2116_2	0x0065		/* SAS2116 */
#define	PCI_PRODUCT_SYMBIOS_SAS2308_3	0x006e		/* SAS2308 */
#define	PCI_PRODUCT_SYMBIOS_SAS2004	0x0070		/* SAS2004 */
#define	PCI_PRODUCT_SYMBIOS_SAS2008	0x0072		/* SAS2008 */
#define	PCI_PRODUCT_SYMBIOS_SAS2008_1	0x0073		/* MegaRAID SAS2008 */
#define	PCI_PRODUCT_SYMBIOS_SAS2108_3	0x0074		/* SAS2108 */
#define	PCI_PRODUCT_SYMBIOS_SAS2108_4	0x0076		/* SAS2108 */
#define	PCI_PRODUCT_SYMBIOS_SAS2108_5	0x0077		/* SAS2108 */
#define	PCI_PRODUCT_SYMBIOS_SAS2108_1	0x0078		/* MegaRAID SAS2108 CRYPTO GEN2 */
#define	PCI_PRODUCT_SYMBIOS_SAS2108_2	0x0079		/* MegaRAID SAS2108 GEN2 */
#define	PCI_PRODUCT_SYMBIOS_SAS1078DE	0x007c		/* SAS1078DE */
#define	PCI_PRODUCT_SYMBIOS_SSS6200	0x007e		/* SSS6200 */
#define	PCI_PRODUCT_SYMBIOS_SAS2208_1	0x0080		/* SAS2208 */
#define	PCI_PRODUCT_SYMBIOS_SAS2208_2	0x0081		/* SAS2208 */
#define	PCI_PRODUCT_SYMBIOS_SAS2208_3	0x0082		/* SAS2208 */
#define	PCI_PRODUCT_SYMBIOS_SAS2208_4	0x0083		/* SAS2208 */
#define	PCI_PRODUCT_SYMBIOS_SAS2208_5	0x0084		/* SAS2208 */
#define	PCI_PRODUCT_SYMBIOS_SAS2208_6	0x0085		/* SAS2208 */
#define	PCI_PRODUCT_SYMBIOS_SAS2308_1	0x0086		/* SAS2308 */
#define	PCI_PRODUCT_SYMBIOS_SAS2308_2	0x0087		/* SAS2308 */
#define	PCI_PRODUCT_SYMBIOS_875J	0x008f		/* 53c875J */
#define	PCI_PRODUCT_SYMBIOS_SAS3108_1	0x0090		/* SAS3108 */
#define	PCI_PRODUCT_SYMBIOS_SAS3108_2	0x0091		/* SAS3108 */
#define	PCI_PRODUCT_SYMBIOS_SAS3108_3	0x0094		/* SAS3108 */
#define	PCI_PRODUCT_SYMBIOS_SAS3108_4	0x0095		/* SAS3108 */
#define	PCI_PRODUCT_SYMBIOS_SAS3004	0x0096		/* SAS3004 */
#define	PCI_PRODUCT_SYMBIOS_SAS3008	0x0097		/* SAS3008 */
#define	PCI_PRODUCT_SYMBIOS_SAS3516	0x00aa		/* SAS3516 */
#define	PCI_PRODUCT_SYMBIOS_SAS3516_1	0x00ab		/* SAS3516 */
#define	PCI_PRODUCT_SYMBIOS_SAS3416	0x00ac		/* SAS3416 */
#define	PCI_PRODUCT_SYMBIOS_SAS3508	0x00ad		/* SAS3508 */
#define	PCI_PRODUCT_SYMBIOS_SAS3508_1	0x00ae		/* SAS3508 */
#define	PCI_PRODUCT_SYMBIOS_SAS3408	0x00af		/* SAS3408 */
#define	PCI_PRODUCT_SYMBIOS_SAS39XX	0x00e1		/* SAS39XX */
#define	PCI_PRODUCT_SYMBIOS_SAS39XX_1	0x00e2		/* SAS39XX */
#define	PCI_PRODUCT_SYMBIOS_SAS38XX	0x00e5		/* SAS38XX */
#define	PCI_PRODUCT_SYMBIOS_SAS38XX_1	0x00e6		/* SAS38XX */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_320	0x0407		/* MegaRAID 320 */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_3202E	0x0408		/* MegaRAID 320-2E */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_SATA	0x0409		/* MegaRAID SATA 4x/8x */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_SAS	0x0411		/* MegaRAID SAS 1064R */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_VERDE_ZCR	0x0413		/* MegaRAID Verde ZCR */
#define	PCI_PRODUCT_SYMBIOS_FC909	0x0620		/* FC909 */
#define	PCI_PRODUCT_SYMBIOS_FC909A	0x0621		/* FC909A */
#define	PCI_PRODUCT_SYMBIOS_FC929	0x0622		/* FC929 */
#define	PCI_PRODUCT_SYMBIOS_FC929_1	0x0623		/* FC929 */
#define	PCI_PRODUCT_SYMBIOS_FC919	0x0624		/* FC919 */
#define	PCI_PRODUCT_SYMBIOS_FC919_1	0x0625		/* FC919 */
#define	PCI_PRODUCT_SYMBIOS_FC929X	0x0626		/* FC929X */
#define	PCI_PRODUCT_SYMBIOS_FC919X	0x0628		/* FC919X */
#define	PCI_PRODUCT_SYMBIOS_FC949X	0x0640		/* FC949X */
#define	PCI_PRODUCT_SYMBIOS_FC939X	0x0642		/* FC939X */
#define	PCI_PRODUCT_SYMBIOS_FC949E	0x0646		/* FC949E */
#define	PCI_PRODUCT_SYMBIOS_YELLOWFIN_1	0x0701		/* Yellowfin */
#define	PCI_PRODUCT_SYMBIOS_YELLOWFIN_2	0x0702		/* Yellowfin */
#define	PCI_PRODUCT_SYMBIOS_61C102	0x0901		/* 61C102 */
#define	PCI_PRODUCT_SYMBIOS_63C815	0x1000		/* 63C815 */
#define	PCI_PRODUCT_SYMBIOS_1030R	0x1030		/* 53c1030R */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_39XX	0x10e1		/* MegaRAID SAS39XX */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_39XX_2	0x10e2		/* MegaRAID SAS39XX */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_38XX	0x10e5		/* MegaRAID SAS38XX */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID_38XX_2	0x10e6		/* MegaRAID SAS38XX */
#define	PCI_PRODUCT_SYMBIOS_MEGARAID	0x1960		/* MegaRAID */

/* Packet Engines products */
#define	PCI_PRODUCT_SYMBIOS_PE_GNIC	0x0702		/* Packet Engines G-NIC */

/* Symbol */
#define	PCI_PRODUCT_SYMBOL_LA41X3	0x0001		/* Spectrum24 LA41X3 */

/* Symphony Labs products */
#define	PCI_PRODUCT_SYMPHONY_82C101	0x0001		/* 82C101 */
#define	PCI_PRODUCT_SYMPHONY_82C103	0x0103		/* 82C103 */
#define	PCI_PRODUCT_SYMPHONY_82C105	0x0105		/* 82C105 */
#define	PCI_PRODUCT_SYMPHONY_82C565	0x0565		/* 82C565 ISA */
#define	PCI_PRODUCT_SYMPHONY2_82C101	0x0001		/* 82C101 */

/* Synopsys products */
#define	PCI_PRODUCT_SYNOPSYS_DW_PCIE	0xabcd		/* DesignWare PCIE */

/* Tamarack Microelectronics */
#define	PCI_PRODUCT_TAMARACK_TC9021	0x1021		/* TC9021 */
#define	PCI_PRODUCT_TAMARACK_TC9021_ALT	0x9021		/* TC9021 */

/* Techsan Electronics */
#define	PCI_PRODUCT_TECHSAN_B2C2_SKY2PC	0x2104		/* B2C2 Sky2PC */
#define	PCI_PRODUCT_TECHSAN_B2C2_SKY2PC_2	0x2200		/* B2C2 Sky2PC */

/* Tehuti Networks Ltd */
#define	PCI_PRODUCT_TEHUTI_TN3009	0x3009		/* TN3009 */
#define	PCI_PRODUCT_TEHUTI_TN3010	0x3010		/* TN3010 */
#define	PCI_PRODUCT_TEHUTI_TN3014	0x3014		/* TN3014 */

/* Tekram Technology products (1st ID)*/
#define	PCI_PRODUCT_TEKRAM_DC290	0xdc29		/* DC-290(M) */

/* Tekram Technology products(2) */
#define	PCI_PRODUCT_TEKRAM2_DC3X5U	0x0391		/* DC-3x5U */
#define	PCI_PRODUCT_TEKRAM2_DC690C	0x690c		/* DC-690C */

/* TerraTec Electronic Gmbh */
#define	PCI_PRODUCT_TERRATEC_TVALUE_PLUS	0x1127		/* Terratec TV+ */
#define	PCI_PRODUCT_TERRATEC_TVALUE	0x1134		/* Terratec TValue */
#define	PCI_PRODUCT_TERRATEC_TVALUER	0x1135		/* Terratec TValue Radio */

/* Texas Instruments products */
#define	PCI_PRODUCT_TI_TLAN	0x0500		/* TLAN */
#define	PCI_PRODUCT_TI_PERMEDIA	0x3d04		/* 3DLabs Permedia */
#define	PCI_PRODUCT_TI_PERMEDIA2	0x3d07		/* 3DLabs Permedia 2 */
#define	PCI_PRODUCT_TI_TSB12LV21	0x8000		/* TSB12LV21 FireWire */
#define	PCI_PRODUCT_TI_TSB12LV22	0x8009		/* TSB12LV22 FireWire */
#define	PCI_PRODUCT_TI_PCI4450_FW	0x8011		/* PCI4450 FireWire */
#define	PCI_PRODUCT_TI_PCI4410_FW	0x8017		/* PCI4410 FireWire */
#define	PCI_PRODUCT_TI_TSB12LV23	0x8019		/* TSB12LV23 FireWire */
#define	PCI_PRODUCT_TI_TSB12LV26	0x8020		/* TSB12LV26 FireWire */
#define	PCI_PRODUCT_TI_TSB43AA22	0x8021		/* TSB43AA22 FireWire */
#define	PCI_PRODUCT_TI_TSB43AB22	0x8023		/* TSB43AB22 FireWire */
#define	PCI_PRODUCT_TI_TSB43AB23	0x8024		/* TSB43AB23 FireWire */
#define	PCI_PRODUCT_TI_TSB82AA2	0x8025		/* TSB82AA2 FireWire */
#define	PCI_PRODUCT_TI_TSB43AB21	0x8026		/* TSB43AB21 FireWire */
#define	PCI_PRODUCT_TI_PCI4451_FW	0x8027		/* PCI4451 FireWire */
#define	PCI_PRODUCT_TI_PCI4510_FW	0x8029		/* PCI4510 FireWire */
#define	PCI_PRODUCT_TI_PCI4520_FW	0x802a		/* PCI4520 FireWire */
#define	PCI_PRODUCT_TI_PCI7410_FW	0x802b		/* PCI7(4-6)10 FireWire */
#define	PCI_PRODUCT_TI_PCI7420_FW	0x802e		/* PCI7x20 FireWire */
#define	PCI_PRODUCT_TI_PCI7XX1	0x8031		/* PCI7XX1 CardBus */
#define	PCI_PRODUCT_TI_PCI7XX1_FW	0x8032		/* PCI7XX1 FireWire */
#define	PCI_PRODUCT_TI_PCI7XX1_FLASH	0x8033		/* PCI7XX1 Flash */
#define	PCI_PRODUCT_TI_PCI7XX1_SD	0x8034		/* PCI7XX1 SD */
#define	PCI_PRODUCT_TI_PCI7XX1_SM	0x8035		/* PCI7XX1 Smart Card */
#define	PCI_PRODUCT_TI_PCI6515	0x8036		/* PCI6515 CardBus */
#define	PCI_PRODUCT_TI_PCI6515SC	0x8038		/* PCI6515 CardBus */
#define	PCI_PRODUCT_TI_PCIXX12	0x8039		/* PCIXX12 CardBus */
#define	PCI_PRODUCT_TI_PCIXX12_FW	0x803a		/* PCIXX12 FireWire */
#define	PCI_PRODUCT_TI_PCIXX12_MCR	0x803b		/* PCIXX12 Multimedia Card Reader */
#define	PCI_PRODUCT_TI_PCIXX12_SD	0x803c		/* PCIXX12 SD */
#define	PCI_PRODUCT_TI_PCIXX12_SM	0x803d		/* PCIXX12 Smart Card */
#define	PCI_PRODUCT_TI_PCI1620_MISC	0x8201		/* PCI1620 Misc */
#define	PCI_PRODUCT_TI_XIO2000A	0x8231		/* XIO2000A PCIE-PCI */
#define	PCI_PRODUCT_TI_XIO3130U	0x8232		/* XIO3130 PCIE-PCIE upstream */
#define	PCI_PRODUCT_TI_XIO3130D	0x8233		/* XIO3130 PCIE-PCIE downstream */
#define	PCI_PRODUCT_TI_XIO2221	0x823e		/* XIO2221 PCIE-PCI */
#define	PCI_PRODUCT_TI_XIO2221_FW	0x823f		/* XIO2221 FireWire */
#define	PCI_PRODUCT_TI_XIO2001	0x8240		/* XIO2001 PCIE-PCI */
#define	PCI_PRODUCT_TI_XHCI	0x8241		/* xHCI */
#define	PCI_PRODUCT_TI_ACX100A	0x8400		/* ACX100A */
#define	PCI_PRODUCT_TI_ACX100B	0x8401		/* ACX100B */
#define	PCI_PRODUCT_TI_ACX111	0x9066		/* ACX111 */
#define	PCI_PRODUCT_TI_PCI1130	0xac12		/* PCI1130 CardBus */
#define	PCI_PRODUCT_TI_PCI1031	0xac13		/* PCI1031 PCMCIA */
#define	PCI_PRODUCT_TI_PCI1131	0xac15		/* PCI1131 CardBus */
#define	PCI_PRODUCT_TI_PCI1250	0xac16		/* PCI1250 CardBus */
#define	PCI_PRODUCT_TI_PCI1220	0xac17		/* PCI1220 CardBus */
#define	PCI_PRODUCT_TI_PCI1221	0xac19		/* PCI1221 CardBus */
#define	PCI_PRODUCT_TI_PCI1210	0xac1a		/* PCI1210 CardBus */
#define	PCI_PRODUCT_TI_PCI1450	0xac1b		/* PCI1450 CardBus */
#define	PCI_PRODUCT_TI_PCI1225	0xac1c		/* PCI1225 CardBus */
#define	PCI_PRODUCT_TI_PCI1251	0xac1d		/* PCI1251 CardBus */
#define	PCI_PRODUCT_TI_PCI1211	0xac1e		/* PCI1211 CardBus */
#define	PCI_PRODUCT_TI_PCI1251B	0xac1f		/* PCI1251B CardBus */
#define	PCI_PRODUCT_TI_PCI2030	0xac20		/* PCI2030 */
#define	PCI_PRODUCT_TI_PCI2031	0xac21		/* PCI2031 */
#define	PCI_PRODUCT_TI_PCI2032	0xac22		/* PCI2032 */
#define	PCI_PRODUCT_TI_PCI2250	0xac23		/* PCI2250 */
#define	PCI_PRODUCT_TI_PCI2050	0xac28		/* PCI2050 */
#define	PCI_PRODUCT_TI_PCI4450_CB	0xac40		/* PCI4450 CardBus */
#define	PCI_PRODUCT_TI_PCI4410_CB	0xac41		/* PCI4410 CardBus */
#define	PCI_PRODUCT_TI_PCI4451_CB	0xac42		/* PCI4451 CardBus */
#define	PCI_PRODUCT_TI_PCI4510_CB	0xac44		/* PCI4510 CardBus */
#define	PCI_PRODUCT_TI_PCI4520_CB	0xac46		/* PCI4520 CardBus */
#define	PCI_PRODUCT_TI_PCI7510_CB	0xac47		/* PCI7510 CardBus */
#define	PCI_PRODUCT_TI_PCI7610_CB	0xac48		/* PCI7610 CardBus */
#define	PCI_PRODUCT_TI_PCI7410_CB	0xac49		/* PCI7410 CardBus */
#define	PCI_PRODUCT_TI_PCI7610SM	0xac4a		/* PCI7610 CardBus */
#define	PCI_PRODUCT_TI_PCI7410SD	0xac4b		/* PCI7[46]10 CardBus */
#define	PCI_PRODUCT_TI_PCI7410MS	0xac4c		/* PCI7[46]10 CardBus */
#define	PCI_PRODUCT_TI_PCI1410	0xac50		/* PCI1410 CardBus */
#define	PCI_PRODUCT_TI_PCI1420	0xac51		/* PCI1420 CardBus */
#define	PCI_PRODUCT_TI_PCI1451	0xac52		/* PCI1451 CardBus */
#define	PCI_PRODUCT_TI_PCI1421	0xac53		/* PCI1421 CardBus */
#define	PCI_PRODUCT_TI_PCI1620	0xac54		/* PCI1620 CardBus */
#define	PCI_PRODUCT_TI_PCI1520	0xac55		/* PCI1520 CardBus */
#define	PCI_PRODUCT_TI_PCI1510	0xac56		/* PCI1510 CardBus */
#define	PCI_PRODUCT_TI_PCI1530	0xac57		/* PCI1530 CardBus */
#define	PCI_PRODUCT_TI_PCI1515	0xac58		/* PCI1515 CardBus */
#define	PCI_PRODUCT_TI_PCI2040	0xac60		/* PCI2040 DSP */
#define	PCI_PRODUCT_TI_PCI7420	0xac8e		/* PCI7420 CardBus */

/* TigerJet Network products */
#define	PCI_PRODUCT_TIGERJET_TIGER320	0x0001		/* PCI */

/* Topic */
#define	PCI_PRODUCT_TOPIC_5634PCV	0x0000		/* 5634PCV SurfRider */

/* Toshiba products */
#define	PCI_PRODUCT_TOSHIBA_R4X00	0x0009		/* R4x00 */
#define	PCI_PRODUCT_TOSHIBA_TC35856F	0x0020		/* TC35856F ATM */
#define	PCI_PRODUCT_TOSHIBA_R4X00_PCI	0x102f		/* R4x00 PCI */

/* Toshiba(2) products */
#define	PCI_PRODUCT_TOSHIBA2_BG3_NVME	0x0113		/* NVMe */
#define	PCI_PRODUCT_TOSHIBA2_NVME	0x0115		/* NVMe */
#define	PCI_PRODUCT_TOSHIBA2_THB	0x0601		/* PCI */
#define	PCI_PRODUCT_TOSHIBA2_ISA	0x0602		/* ISA */
#define	PCI_PRODUCT_TOSHIBA2_TOPIC95	0x0603		/* ToPIC95 CardBus-PCI */
#define	PCI_PRODUCT_TOSHIBA2_TOPIC95B	0x060a		/* ToPIC95B CardBus */
#define	PCI_PRODUCT_TOSHIBA2_TOPIC97	0x060f		/* ToPIC97 CardBus */
#define	PCI_PRODUCT_TOSHIBA2_TOPIC100	0x0617		/* ToPIC100 CardBus */
#define	PCI_PRODUCT_TOSHIBA2_TFIRO	0x0701		/* Infrared */
#define	PCI_PRODUCT_TOSHIBA2_SDCARD	0x0805		/* SD */

/* Transmeta products */
#define	PCI_PRODUCT_TRANSMETA_TM8000_HT	0x0060		/* TM8000 HyperTransport */
#define	PCI_PRODUCT_TRANSMETA_TM8000_AGP	0x0061		/* TM8000 AGP */
#define	PCI_PRODUCT_TRANSMETA_NB	0x0295		/* Northbridge */
#define	PCI_PRODUCT_TRANSMETA_LONGRUN_NB	0x0395		/* LongRun Northbridge */
#define	PCI_PRODUCT_TRANSMETA_SDRAM	0x0396		/* SDRAM */
#define	PCI_PRODUCT_TRANSMETA_BIOS	0x0397		/* BIOS */

/* Trident products */
#define	PCI_PRODUCT_TRIDENT_4DWAVE_DX	0x2000		/* 4DWAVE DX */
#define	PCI_PRODUCT_TRIDENT_4DWAVE_NX	0x2001		/* 4DWAVE NX */
#define	PCI_PRODUCT_TRIDENT_CYBERBLADEI7	0x8400		/* CyberBlade i7 */
#define	PCI_PRODUCT_TRIDENT_CYBERBLADEI7AGP	0x8420		/* CyberBlade i7 AGP */
#define	PCI_PRODUCT_TRIDENT_CYBERBLADEI1	0x8500		/* CyberBlade i1 */
#define	PCI_PRODUCT_TRIDENT_CYBERBLADEI1AGP	0x8520		/* CyberBlade i1 AGP */
#define	PCI_PRODUCT_TRIDENT_CYBERBLADEAI1	0x8600		/* CyberBlade Ai1 */
#define	PCI_PRODUCT_TRIDENT_CYBERBLADEAI1AGP	0x8620		/* CyberBlade Ai1 AGP */
#define	PCI_PRODUCT_TRIDENT_CYBERBLADEXPAI1	0x8820		/* CyberBlade XP/Ai1 */
#define	PCI_PRODUCT_TRIDENT_TGUI_9320	0x9320		/* TGUI 9320 */
#define	PCI_PRODUCT_TRIDENT_TGUI_9350	0x9350		/* TGUI 9350 */
#define	PCI_PRODUCT_TRIDENT_TGUI_9360	0x9360		/* TGUI 9360 */
#define	PCI_PRODUCT_TRIDENT_TGUI_9388	0x9388		/* TGUI 9388 */
#define	PCI_PRODUCT_TRIDENT_CYBER_9397	0x9397		/* CYBER 9397 */
#define	PCI_PRODUCT_TRIDENT_CYBER_9397DVD	0x939a		/* CYBER 9397DVD */
#define	PCI_PRODUCT_TRIDENT_TGUI_9420	0x9420		/* TGUI 9420 */
#define	PCI_PRODUCT_TRIDENT_TGUI_9440	0x9440		/* TGUI 9440 */
#define	PCI_PRODUCT_TRIDENT_CYBER_9525	0x9525		/* CYBER 9525 */
#define	PCI_PRODUCT_TRIDENT_TGUI_9660	0x9660		/* TGUI 9660 */
#define	PCI_PRODUCT_TRIDENT_TGUI_9680	0x9680		/* TGUI 9680 */
#define	PCI_PRODUCT_TRIDENT_TGUI_9682	0x9682		/* TGUI 9682 */
#define	PCI_PRODUCT_TRIDENT_3DIMAGE_9750	0x9750		/* 3DImage 9750 */
#define	PCI_PRODUCT_TRIDENT_3DIMAGE_9850	0x9850		/* 3DImage 9850 */
#define	PCI_PRODUCT_TRIDENT_BLADE_3D	0x9880		/* Blade 3D */
#define	PCI_PRODUCT_TRIDENT_BLADE_XP	0x9910		/* CyberBlade XP */
#define	PCI_PRODUCT_TRIDENT_BLADE_XP2	0x9960		/* CyberBlade XP2 */

/* Triones/HighPoint Technologies products */
#define	PCI_PRODUCT_TRIONES_HPT343	0x0003		/* HPT343/345 IDE */
#define	PCI_PRODUCT_TRIONES_HPT366	0x0004		/* HPT36x/37x IDE */
#define	PCI_PRODUCT_TRIONES_HPT372A	0x0005		/* HPT372A IDE */
#define	PCI_PRODUCT_TRIONES_HPT302	0x0006		/* HPT302 IDE */
#define	PCI_PRODUCT_TRIONES_HPT371	0x0007		/* HPT371 IDE */
#define	PCI_PRODUCT_TRIONES_HPT374	0x0008		/* HPT374 IDE */

/* TriTech Microelectronics products*/
#define	PCI_PRODUCT_TRITECH_TR25202	0xfc02		/* Pyramid3D TR25202 */

/* Tseng Labs products */
#define	PCI_PRODUCT_TSENG_ET4000_W32P_A	0x3202		/* ET4000w32pA */
#define	PCI_PRODUCT_TSENG_ET4000_W32P_B	0x3205		/* ET4000w32pB */
#define	PCI_PRODUCT_TSENG_ET4000_W32P_C	0x3206		/* ET4000w32pC */
#define	PCI_PRODUCT_TSENG_ET4000_W32P_D	0x3207		/* ET4000w32pD */
#define	PCI_PRODUCT_TSENG_ET6000	0x3208		/* ET6000/ET6100 */
#define	PCI_PRODUCT_TSENG_ET6300	0x4702		/* ET6300 */

/* TTTech */
#define	PCI_PRODUCT_TTTECH_MC322	0x000a		/* MC322 */

/* Turtle Beach products */
#define	PCI_PRODUCT_TURTLEBEACH_SANTA_CRUZ	0x3357		/* Santa Cruz */

/* Tvia products */
#define	PCI_PRODUCT_TVIA_IGA1680	0x1680		/* IGA-1680 */
#define	PCI_PRODUCT_TVIA_IGA1682	0x1682		/* IGA-1682 */
#define	PCI_PRODUCT_TVIA_IGA1683	0x1683		/* IGA-1683 */
#define	PCI_PRODUCT_TVIA_CP2000	0x2000		/* CyberPro 2000 */
#define	PCI_PRODUCT_TVIA_CP2000A	0x2010		/* CyberPro 2010 */
#define	PCI_PRODUCT_TVIA_CP5000	0x5000		/* CyberPro 5000 */
#define	PCI_PRODUCT_TVIA_CP5050	0x5050		/* CyberPro 5050 */
#define	PCI_PRODUCT_TVIA_CP5202	0x5202		/* CyberPro 5202 */
#define	PCI_PRODUCT_TVIA_CP5252	0x5252		/* CyberPro 5252 */

/* TXIC */
#define	PCI_PRODUCT_TXIC_TX382B	0x3273		/* TX382B */

/* ULSI Systems products */
#define	PCI_PRODUCT_ULSI_US201	0x0201		/* US201 */

/* UMC products */
#define	PCI_PRODUCT_UMC_UM82C881	0x0001		/* UM82C881 486 */
#define	PCI_PRODUCT_UMC_UM82C886	0x0002		/* UM82C886 ISA */
#define	PCI_PRODUCT_UMC_UM8673F	0x0101		/* UM8673F EIDE */
#define	PCI_PRODUCT_UMC_UM8881	0x0881		/* UM8881 HB4 486 PCI */
#define	PCI_PRODUCT_UMC_UM82C891	0x0891		/* UM82C891 */
#define	PCI_PRODUCT_UMC_UM886A	0x1001		/* UM886A */
#define	PCI_PRODUCT_UMC_UM8886BF	0x673a		/* UM8886BF */
#define	PCI_PRODUCT_UMC_UM8710	0x8710		/* UM8710 */
#define	PCI_PRODUCT_UMC_UM8886	0x886a		/* UM8886 */
#define	PCI_PRODUCT_UMC_UM8881F	0x8881		/* UM8881F Host */
#define	PCI_PRODUCT_UMC_UM8886F	0x8886		/* UM8886F ISA */
#define	PCI_PRODUCT_UMC_UM8886A	0x888a		/* UM8886A */
#define	PCI_PRODUCT_UMC_UM8891A	0x8891		/* UM8891A */
#define	PCI_PRODUCT_UMC_UM9017F	0x9017		/* UM9017F */
#define	PCI_PRODUCT_UMC_UM8886E_OR_WHAT	0xe886		/* ISA */
#define	PCI_PRODUCT_UMC_UM8886N	0xe88a		/* UM8886N */
#define	PCI_PRODUCT_UMC_UM8891N	0xe891		/* UM8891N */

/* Shenzhen Unionmemory Information System products */
#define	PCI_PRODUCT_UMIS_NVME	0x2263		/* NVMe */
#define	PCI_PRODUCT_UMIS_AM620	0x6202		/* AM620 */
#define	PCI_PRODUCT_UMIS_AM630	0x6303		/* AM630 */

/* US Robotics */
#define	PCI_PRODUCT_USR_3CP5610	0x1008		/* 3CP5610 */
#define	PCI_PRODUCT_USR2_USR997902	0x0116		/* USR997902 */
#define	PCI_PRODUCT_USR2_WL11000P	0x3685		/* WL11000P */

/* V3 Semiconductor products */
#define	PCI_PRODUCT_V3_V961PBC	0x0002		/* V961PBC i960 PCI */
#define	PCI_PRODUCT_V3_V292PBC	0x0292		/* V292PBC AMD290x0 PCI */
#define	PCI_PRODUCT_V3_V960PBC	0x0960		/* V960PBC i960 PCI */
#define	PCI_PRODUCT_V3_V96DPC	0xc960		/* V96DPC i960 PCI */

/* VIA Technologies products */
#define	PCI_PRODUCT_VIATECH_K8M800_0	0x0204		/* K8M800 Host */
#define	PCI_PRODUCT_VIATECH_K8T890_0	0x0238		/* K8T890 Host */
#define	PCI_PRODUCT_VIATECH_PT880_0	0x0258		/* PT880 Host */
#define	PCI_PRODUCT_VIATECH_PM800_AGP	0x0259		/* PM800 AGP */
#define	PCI_PRODUCT_VIATECH_KT880_AGP	0x0269		/* KT880 AGP */
#define	PCI_PRODUCT_VIATECH_K8HTB_0	0x0282		/* K8HTB Host */
#define	PCI_PRODUCT_VIATECH_VT8363	0x0305		/* VT8363 Host */
#define	PCI_PRODUCT_VIATECH_PT894	0x0308		/* PT894 Host */
#define	PCI_PRODUCT_VIATECH_CN700	0x0314		/* CN700 Host */
#define	PCI_PRODUCT_VIATECH_CX700	0x0324		/* CX700 Host */
#define	PCI_PRODUCT_VIATECH_P4M890	0x0327		/* P4M890 Host */
#define	PCI_PRODUCT_VIATECH_K8M890_0	0x0336		/* K8M890 Host */
#define	PCI_PRODUCT_VIATECH_VT3351_HB	0x0351		/* VT3351 Host */
#define	PCI_PRODUCT_VIATECH_VX800_0	0x0353		/* VX800 Host */
#define	PCI_PRODUCT_VIATECH_P4M900	0x0364		/* P4M900 Host */
#define	PCI_PRODUCT_VIATECH_VT8371_HB	0x0391		/* VT8371 Host */
#define	PCI_PRODUCT_VIATECH_VX900_HB	0x0410		/* VX900 Host */
#define	PCI_PRODUCT_VIATECH_VT6415	0x0415		/* VT6415 IDE */
#define	PCI_PRODUCT_VIATECH_VT8501	0x0501		/* VT8501 */
#define	PCI_PRODUCT_VIATECH_VT82C505	0x0505		/* VT82C505 */
#define	PCI_PRODUCT_VIATECH_VT82C561	0x0561		/* VT82C561 */
#define	PCI_PRODUCT_VIATECH_VT82C571	0x0571		/* VT82C571 IDE */
#define	PCI_PRODUCT_VIATECH_VT82C576	0x0576		/* VT82C576 3V */
#define	PCI_PRODUCT_VIATECH_VX700_IDE	0x0581		/* VX700 IDE */
#define	PCI_PRODUCT_VIATECH_VT82C585	0x0585		/* VT82C585 ISA */
#define	PCI_PRODUCT_VIATECH_VT82C586_ISA	0x0586		/* VT82C586 ISA */
#define	PCI_PRODUCT_VIATECH_VT8237A_SATA	0x0591		/* VT8237A SATA */
#define	PCI_PRODUCT_VIATECH_VT82C595	0x0595		/* VT82C595 PCI */
#define	PCI_PRODUCT_VIATECH_VT82C596A	0x0596		/* VT82C596A ISA */
#define	PCI_PRODUCT_VIATECH_VT82C597PCI	0x0597		/* VT82C597 PCI */
#define	PCI_PRODUCT_VIATECH_VT82C598PCI	0x0598		/* VT82C598 PCI */
#define	PCI_PRODUCT_VIATECH_VT8601	0x0601		/* VT8601 PCI */
#define	PCI_PRODUCT_VIATECH_VT8605	0x0605		/* VT8605 PCI */
#define	PCI_PRODUCT_VIATECH_VT82C686A_ISA	0x0686		/* VT82C686 ISA */
#define	PCI_PRODUCT_VIATECH_VT82C691	0x0691		/* VT82C691 PCI */
#define	PCI_PRODUCT_VIATECH_VT82C693	0x0693		/* VT82C693 PCI */
#define	PCI_PRODUCT_VIATECH_VT86C926	0x0926		/* VT86C926 Amazon */
#define	PCI_PRODUCT_VIATECH_VT82C570M	0x1000		/* VT82C570M PCI */
#define	PCI_PRODUCT_VIATECH_VT82C570MV	0x1006		/* VT82C570M ISA */
#define	PCI_PRODUCT_VIATECH_CHROME9HC3	0x1122		/* Chrome9 HC3 IGP */
#define	PCI_PRODUCT_VIATECH_K8M800_1	0x1204		/* K8M800 Host */
#define	PCI_PRODUCT_VIATECH_K8T890_1	0x1238		/* K8T890 Host */
#define	PCI_PRODUCT_VIATECH_PT880_1	0x1258		/* PT880 Host */
#define	PCI_PRODUCT_VIATECH_PM800_ERRS	0x1259		/* PM800 Errors */
#define	PCI_PRODUCT_VIATECH_KT880_1	0x1269		/* KT880 Host */
#define	PCI_PRODUCT_VIATECH_K8HTB_1	0x1282		/* K8HTB Host */
#define	PCI_PRODUCT_VIATECH_PT894_2	0x1308		/* PT894 Host */
#define	PCI_PRODUCT_VIATECH_CN700_2	0x1314		/* CN700 Host */
#define	PCI_PRODUCT_VIATECH_CX700_1	0x1324		/* CX700 Host */
#define	PCI_PRODUCT_VIATECH_P4M890_1	0x1327		/* P4M890 Host */
#define	PCI_PRODUCT_VIATECH_K8M890_1	0x1336		/* K8M890 Host */
#define	PCI_PRODUCT_VIATECH_VT3351_2	0x1351		/* VT3351 Host */
#define	PCI_PRODUCT_VIATECH_VX800_1	0x1353		/* VX800 Host */
#define	PCI_PRODUCT_VIATECH_P4M900_1	0x1364		/* P4M900 Host */
#define	PCI_PRODUCT_VIATECH_VX900_ERR	0x1410		/* VX900 Error Reporting */
#define	PCI_PRODUCT_VIATECH_VT82C416	0x1571		/* VT82C416 IDE */
#define	PCI_PRODUCT_VIATECH_VT82C1595	0x1595		/* VT82C1595 PCI */
#define	PCI_PRODUCT_VIATECH_K8M800_2	0x2204		/* K8M800 Host */
#define	PCI_PRODUCT_VIATECH_K8T890_2	0x2238		/* K8T890 Host */
#define	PCI_PRODUCT_VIATECH_PT880_2	0x2258		/* PT880 Host */
#define	PCI_PRODUCT_VIATECH_PM800	0x2259		/* PM800 Host */
#define	PCI_PRODUCT_VIATECH_KT880_2	0x2269		/* KT880 Host */
#define	PCI_PRODUCT_VIATECH_K8HTB_2	0x2282		/* K8HTB Host */
#define	PCI_PRODUCT_VIATECH_PT894_3	0x2308		/* PT894 Host */
#define	PCI_PRODUCT_VIATECH_CN700_3	0x2314		/* CN700 Host */
#define	PCI_PRODUCT_VIATECH_CX700_2	0x2324		/* CX700 Host */
#define	PCI_PRODUCT_VIATECH_P4M890_2	0x2327		/* P4M890 Host */
#define	PCI_PRODUCT_VIATECH_K8M890_2	0x2336		/* K8M890 Host */
#define	PCI_PRODUCT_VIATECH_VT3351_3	0x2351		/* VT3351 Host */
#define	PCI_PRODUCT_VIATECH_VX800_2	0x2353		/* VX800 Host */
#define	PCI_PRODUCT_VIATECH_P4M900_2	0x2364		/* P4M900 Host */
#define	PCI_PRODUCT_VIATECH_VX900_0	0x2410		/* VX900 Host */
#define	PCI_PRODUCT_VIATECH_VT8251_PCI	0x287a		/* VT8251 PCI */
#define	PCI_PRODUCT_VIATECH_VT8251_PCIE_0	0x287b		/* VT8251 PCIE */
#define	PCI_PRODUCT_VIATECH_VT8251_PCIE_1	0x287c		/* VT8251 PCIE */
#define	PCI_PRODUCT_VIATECH_VT8251_PCIE_2	0x287d		/* VT8251 PCIE */
#define	PCI_PRODUCT_VIATECH_VT8251_VLINK	0x287e		/* VT8251 VLINK */
#define	PCI_PRODUCT_VIATECH_VT83C572	0x3038		/* VT83C572 USB */
#define	PCI_PRODUCT_VIATECH_VT82C586_PWR	0x3040		/* VT82C586 Power */
#define	PCI_PRODUCT_VIATECH_RHINE	0x3043		/* Rhine/RhineII */
#define	PCI_PRODUCT_VIATECH_VT6306	0x3044		/* VT6306 FireWire */
#define	PCI_PRODUCT_VIATECH_VT82C596	0x3050		/* VT82C596 Power */
#define	PCI_PRODUCT_VIATECH_VT82C596B_PM	0x3051		/* VT82C596B PM */
#define	PCI_PRODUCT_VIATECH_VT6105M	0x3053		/* VT6105M RhineIII */
#define	PCI_PRODUCT_VIATECH_VT82C686A_SMB	0x3057		/* VT82C686 SMBus */
#define	PCI_PRODUCT_VIATECH_VT82C686A_AC97	0x3058		/* VT82C686 AC97 */
#define	PCI_PRODUCT_VIATECH_VT8233_AC97	0x3059		/* VT8233 AC97 */
#define	PCI_PRODUCT_VIATECH_RHINEII_2	0x3065		/* RhineII-2 */
#define	PCI_PRODUCT_VIATECH_VT82C686A_ACM	0x3068		/* VT82C686 Modem */
#define	PCI_PRODUCT_VIATECH_VT8233_ISA	0x3074		/* VT8233 ISA */
#define	PCI_PRODUCT_VIATECH_VT8633	0x3091		/* VT8633 PCI */
#define	PCI_PRODUCT_VIATECH_VT8366	0x3099		/* VT8366 PCI */
#define	PCI_PRODUCT_VIATECH_VT8653_PCI	0x3101		/* VT8653 PCI */
#define	PCI_PRODUCT_VIATECH_VT6202	0x3104		/* VT6202 USB */
#define	PCI_PRODUCT_VIATECH_VT6105	0x3106		/* VT6105 RhineIII */
#define	PCI_PRODUCT_VIATECH_UNICHROME	0x3108		/* S3 Unichrome PRO IGP */
#define	PCI_PRODUCT_VIATECH_VT8361_PCI	0x3112		/* VT8361 PCI */
#define	PCI_PRODUCT_VIATECH_VT8101_PPB	0x3113		/* VT8101 VPX-64 */
#define	PCI_PRODUCT_VIATECH_VT8375	0x3116		/* VT8375 PCI */
#define	PCI_PRODUCT_VIATECH_PM800_UNICHROME	0x3118		/* PM800 Unichrome S3 */
#define	PCI_PRODUCT_VIATECH_VT612X	0x3119		/* VT612x */
#define	PCI_PRODUCT_VIATECH_CLE266	0x3122		/* CLE266 */
#define	PCI_PRODUCT_VIATECH_VT8623	0x3123		/* VT8623 PCI */
#define	PCI_PRODUCT_VIATECH_VT8233A_ISA	0x3147		/* VT8233A ISA */
#define	PCI_PRODUCT_VIATECH_VT8751	0x3148		/* VT8751 PCI */
#define	PCI_PRODUCT_VIATECH_VT6420_SATA	0x3149		/* VT6420 SATA */
#define	PCI_PRODUCT_VIATECH_UNICHROME2_1	0x3157		/* S3 UniChrome Pro II IGP */
#define	PCI_PRODUCT_VIATECH_VT6410	0x3164		/* VT6410 IDE */
#define	PCI_PRODUCT_VIATECH_P4X400	0x3168		/* P4X400 Host */
#define	PCI_PRODUCT_VIATECH_VT8235_ISA	0x3177		/* VT8235 ISA */
#define	PCI_PRODUCT_VIATECH_P4N333	0x3178		/* P4N333 Host */
#define	PCI_PRODUCT_VIATECH_K8HTB	0x3188		/* K8HTB Host */
#define	PCI_PRODUCT_VIATECH_VT8377	0x3189		/* VT8377 PCI */
#define	PCI_PRODUCT_VIATECH_K8M800	0x3204		/* K8M800 Host */
#define	PCI_PRODUCT_VIATECH_VT8378	0x3205		/* VT8378 PCI */
#define	PCI_PRODUCT_VIATECH_PT890	0x3208		/* PT890 Host */
#define	PCI_PRODUCT_VIATECH_K8T800M	0x3218		/* K8T800M Host */
#define	PCI_PRODUCT_VIATECH_VT8237_ISA	0x3227		/* VT8237 ISA */
#define	PCI_PRODUCT_VIATECH_DELTACHROME	0x3230		/* DeltaChrome Video */
#define	PCI_PRODUCT_VIATECH_K8T890_3	0x3238		/* K8T890 Host */
#define	PCI_PRODUCT_VIATECH_VT6421_SATA	0x3249		/* VT6421 SATA */
#define	PCI_PRODUCT_VIATECH_CX700_PPB_1	0x324a		/* CX700 */
#define	PCI_PRODUCT_VIATECH_CX700_3	0x324b		/* CX700 Host */
#define	PCI_PRODUCT_VIATECH_VX700_1	0x324e		/* VX700 Host */
#define	PCI_PRODUCT_VIATECH_VT6655	0x3253		/* VT6655 */
#define	PCI_PRODUCT_VIATECH_PT880_3	0x3258		/* PT880 Host */
#define	PCI_PRODUCT_VIATECH_PM800_DRAM	0x3259		/* PM800 DRAM */
#define	PCI_PRODUCT_VIATECH_KT880_3	0x3269		/* KT880 Host */
#define	PCI_PRODUCT_VIATECH_K8HTB_3	0x3282		/* K8HTB Host */
#define	PCI_PRODUCT_VIATECH_VT8251_ISA	0x3287		/* VT8251 ISA */
#define	PCI_PRODUCT_VIATECH_HDA_0	0x3288		/* HD Audio */
#define	PCI_PRODUCT_VIATECH_CX700_4	0x3324		/* CX700 Host */
#define	PCI_PRODUCT_VIATECH_P4M890_3	0x3327		/* P4M890 Host */
#define	PCI_PRODUCT_VIATECH_K8M890_3	0x3336		/* K8M890 Host */
#define	PCI_PRODUCT_VIATECH_VT8237A_ISA	0x3337		/* VT8237A ISA */
#define	PCI_PRODUCT_VIATECH_UNICHROME_3	0x3343		/* S3 Unichrome PRO IGP */
#define	PCI_PRODUCT_VIATECH_UNICHROME_2	0x3344		/* S3 Unichrome PRO IGP */
#define	PCI_PRODUCT_VIATECH_VT8251_SATA	0x3349		/* VT8251 SATA */
#define	PCI_PRODUCT_VIATECH_VT3351_4	0x3351		/* VT3351 Host */
#define	PCI_PRODUCT_VIATECH_VX800_DRAM	0x3353		/* VX800 DRAM */
#define	PCI_PRODUCT_VIATECH_P4M900_3	0x3364		/* P4M900 Host */
#define	PCI_PRODUCT_VIATECH_CHROME9_HC	0x3371		/* Chrome9 HC IGP */
#define	PCI_PRODUCT_VIATECH_VT8237S_ISA	0x3372		/* VT8237S ISA */
#define	PCI_PRODUCT_VIATECH_VT8237A_PPB_1	0x337a		/* VT8237A */
#define	PCI_PRODUCT_VIATECH_VT8237A_PPB_2	0x337b		/* VT8237A */
#define	PCI_PRODUCT_VIATECH_VX900_DRAM	0x3410		/* VX900 DRAM */
#define	PCI_PRODUCT_VIATECH_VL80X_XHCI	0x3432		/* VL80x xHCI */
#define	PCI_PRODUCT_VIATECH_VL805_XHCI	0x3483		/* VL805 xHCI */
#define	PCI_PRODUCT_VIATECH_K8M800_4	0x4204		/* K8M800 Host */
#define	PCI_PRODUCT_VIATECH_K8T890_4	0x4238		/* K8T890 Host */
#define	PCI_PRODUCT_VIATECH_PT880_4	0x4258		/* PT880 Host */
#define	PCI_PRODUCT_VIATECH_PM800_PMC	0x4259		/* PM800 PMC */
#define	PCI_PRODUCT_VIATECH_KT880_4	0x4269		/* KT880 Host */
#define	PCI_PRODUCT_VIATECH_K8HTB_4	0x4282		/* K8HTB Host */
#define	PCI_PRODUCT_VIATECH_PT894_4	0x4308		/* PT894 Host */
#define	PCI_PRODUCT_VIATECH_CN700_4	0x4314		/* CN700 Host */
#define	PCI_PRODUCT_VIATECH_CX700_5	0x4324		/* CX700 Host */
#define	PCI_PRODUCT_VIATECH_P4M890_4	0x4327		/* P4M890 Host */
#define	PCI_PRODUCT_VIATECH_K8M890_4	0x4336		/* K8M890 Host */
#define	PCI_PRODUCT_VIATECH_VT3351_5	0x4351		/* VT3351 Host */
#define	PCI_PRODUCT_VIATECH_VX800_4	0x4353		/* VX800 Host */
#define	PCI_PRODUCT_VIATECH_P4M900_4	0x4364		/* P4M900 Host */
#define	PCI_PRODUCT_VIATECH_VX900_1	0x4410		/* VX900 Host */
#define	PCI_PRODUCT_VIATECH_K8T890_IOAPIC	0x5238		/* K8T890 IOAPIC */
#define	PCI_PRODUCT_VIATECH_PT894_IOAPIC	0x5308		/* PT894 IOAPIC */
#define	PCI_PRODUCT_VIATECH_CX700_IDE	0x5324		/* CX700 IDE */
#define	PCI_PRODUCT_VIATECH_P4M890_IOAPIC	0x5327		/* P4M890 IOAPIC */
#define	PCI_PRODUCT_VIATECH_K8M890_IOAPIC	0x5336		/* K8M890 IOAPIC */
#define	PCI_PRODUCT_VIATECH_VT8237A_SATA_2	0x5337		/* VT8237A SATA */
#define	PCI_PRODUCT_VIATECH_VT3351_IOAPIC	0x5351		/* VT3351 IOAPIC */
#define	PCI_PRODUCT_VIATECH_VX800_IOAPIC	0x5353		/* VX800 IOAPIC */
#define	PCI_PRODUCT_VIATECH_P4M900_IOAPIC	0x5364		/* P4M900 IOAPIC */
#define	PCI_PRODUCT_VIATECH_VT8237S_SATA	0x5372		/* VT8237S SATA */
#define	PCI_PRODUCT_VIATECH_VX900_APIC	0x5410		/* VX900 APIC */
#define	PCI_PRODUCT_VIATECH_RHINEII	0x6100		/* RhineII */
#define	PCI_PRODUCT_VIATECH_VT3351_6	0x6238		/* VT3351 Host */
#define	PCI_PRODUCT_VIATECH_VT8251_AHCI	0x6287		/* VT8251 AHCI */
#define	PCI_PRODUCT_VIATECH_K8M890_6	0x6290		/* K8M890 Host */
#define	PCI_PRODUCT_VIATECH_P4M890_6	0x6327		/* P4M890 Security */
#define	PCI_PRODUCT_VIATECH_VX800_6	0x6353		/* VX800 Host */
#define	PCI_PRODUCT_VIATECH_P4M900_6	0x6364		/* P4M900 Security */
#define	PCI_PRODUCT_VIATECH_VX900_SCRATCH	0x6410		/* VX900 Scratch */
#define	PCI_PRODUCT_VIATECH_CHROME9_HD	0x7122		/* Chrome9 HD */
#define	PCI_PRODUCT_VIATECH_K8M800_7	0x7204		/* K8M800 Host */
#define	PCI_PRODUCT_VIATECH_VT8378_VGA	0x7205		/* VT8378 VGA */
#define	PCI_PRODUCT_VIATECH_K8T890_7	0x7238		/* K8T890 Host */
#define	PCI_PRODUCT_VIATECH_PT880_7	0x7258		/* PT880 Host */
#define	PCI_PRODUCT_VIATECH_PM800_PCI	0x7259		/* PM800 PCI */
#define	PCI_PRODUCT_VIATECH_KT880_7	0x7269		/* KT880 Host */
#define	PCI_PRODUCT_VIATECH_K8HTB_7	0x7282		/* K8HTB Host */
#define	PCI_PRODUCT_VIATECH_PT894_5	0x7308		/* PT894 Host */
#define	PCI_PRODUCT_VIATECH_CN700_7	0x7314		/* CN700 Host */
#define	PCI_PRODUCT_VIATECH_CX700_7	0x7324		/* CX700 Host */
#define	PCI_PRODUCT_VIATECH_P4M890_7	0x7327		/* P4M890 Host */
#define	PCI_PRODUCT_VIATECH_K8M890_7	0x7336		/* K8M890 Host */
#define	PCI_PRODUCT_VIATECH_VT3351_7	0x7351		/* VT3351 Host */
#define	PCI_PRODUCT_VIATECH_VX800_7	0x7353		/* VX800 Host */
#define	PCI_PRODUCT_VIATECH_P4M900_7	0x7364		/* P4M900 Host */
#define	PCI_PRODUCT_VIATECH_VX900_2	0x7410		/* VX900 Host */
#define	PCI_PRODUCT_VIATECH_VT8231_ISA	0x8231		/* VT8231 ISA */
#define	PCI_PRODUCT_VIATECH_VT8231_PWR	0x8235		/* VT8231 PMG */
#define	PCI_PRODUCT_VIATECH_VT8363_AGP	0x8305		/* VT8363 AGP */
#define	PCI_PRODUCT_VIATECH_CX700_ISA	0x8324		/* CX700 ISA */
#define	PCI_PRODUCT_VIATECH_VX800_ISA	0x8353		/* VX800 ISA */
#define	PCI_PRODUCT_VIATECH_VT8371_PPB	0x8391		/* VT8371 */
#define	PCI_PRODUCT_VIATECH_VX855_ISA	0x8409		/* VX855 ISA */
#define	PCI_PRODUCT_VIATECH_VX900_ISA	0x8410		/* VX900 ISA */
#define	PCI_PRODUCT_VIATECH_VT8501_AGP	0x8501		/* VT8501 AGP */
#define	PCI_PRODUCT_VIATECH_VT82C597AGP	0x8597		/* VT82C597 AGP */
#define	PCI_PRODUCT_VIATECH_VT82C598AGP	0x8598		/* VT82C598 AGP */
#define	PCI_PRODUCT_VIATECH_VT82C601	0x8601		/* VT82C601 AGP */
#define	PCI_PRODUCT_VIATECH_VT8605_AGP	0x8605		/* VT8605 AGP */
#define	PCI_PRODUCT_VIATECH_VX900_IDE	0x9001		/* VX900 IDE */
#define	PCI_PRODUCT_VIATECH_HDA_1	0x9170		/* HD Audio */
#define	PCI_PRODUCT_VIATECH_VX800_SDMMC	0x9530		/* VX800 SD/MMC */
#define	PCI_PRODUCT_VIATECH_VX800_SDIO	0x95d0		/* VX800 SDIO */
#define	PCI_PRODUCT_VIATECH_K8T890_PPB_A	0xa238		/* K8T890 */
#define	PCI_PRODUCT_VIATECH_P4M890_PPB_1	0xa327		/* P4M890 */
#define	PCI_PRODUCT_VIATECH_VX800_A	0xa353		/* VX800 Host */
#define	PCI_PRODUCT_VIATECH_P4M900_PPB_1	0xa364		/* P4M900 */
#define	PCI_PRODUCT_VIATECH_VX900_PCIE_0	0xa410		/* VX900 PCIE */
#define	PCI_PRODUCT_VIATECH_VT8633_AGP	0xb091		/* VT8633 AGP */
#define	PCI_PRODUCT_VIATECH_VT8366_AGP	0xb099		/* VT8366 AGP */
#define	PCI_PRODUCT_VIATECH_VT8361_AGP	0xb112		/* VT8361 AGP */
#define	PCI_PRODUCT_VIATECH_VT8101_IOAPIC	0xb113		/* VT8101 VPX-64 IOAPIC */
#define	PCI_PRODUCT_VIATECH_VT8363_PCI	0xb115		/* VT8363 */
#define	PCI_PRODUCT_VIATECH_VT8235_AGP	0xb168		/* VT8235 AGP */
#define	PCI_PRODUCT_VIATECH_K8HTB_AGP	0xb188		/* K8HTB AGP */
#define	PCI_PRODUCT_VIATECH_VT8377_AGP	0xb198		/* VT8377 AGP */
#define	PCI_PRODUCT_VIATECH_VX800_PPB	0xb353		/* VX800 */
#define	PCI_PRODUCT_VIATECH_VX900_PCIE_1	0xb410		/* VX900 PCIE */
#define	PCI_PRODUCT_VIATECH_K8T890_PPB_B	0xb999		/* K8T890 */
#define	PCI_PRODUCT_VIATECH_K8T890_PPB_C	0xc238		/* K8T890 */
#define	PCI_PRODUCT_VIATECH_P4M890_PPB_2	0xc327		/* P4M890 */
#define	PCI_PRODUCT_VIATECH_VX800_PCIE_0	0xc353		/* VX800 PCIE */
#define	PCI_PRODUCT_VIATECH_P4M900_PPB_2	0xc364		/* P4M900 */
#define	PCI_PRODUCT_VIATECH_VX855_IDE	0xc409		/* VX855 IDE */
#define	PCI_PRODUCT_VIATECH_VX900_PCIE_2	0xc410		/* VX900 PCIE */
#define	PCI_PRODUCT_VIATECH_K8T890_PPB_D	0xd238		/* K8T890 */
#define	PCI_PRODUCT_VIATECH_VX900_PCIE_3	0xd410		/* VX900 PCIE */
#define	PCI_PRODUCT_VIATECH_K8T890_PPB_E	0xe238		/* K8T890 */
#define	PCI_PRODUCT_VIATECH_VX800_PCIE_1	0xe353		/* VX800 PCIE */
#define	PCI_PRODUCT_VIATECH_VX900_PCIE_4	0xe410		/* VX900 PCIE */
#define	PCI_PRODUCT_VIATECH_K8T890_PPB_F	0xf238		/* K8T890 */
#define	PCI_PRODUCT_VIATECH_VX800_PCIE_2	0xf353		/* VX800 PCIE */

/* Vitesse Semiconductor products */
#define	PCI_PRODUCT_VITESSE_VSC_7174	0x7174		/* VSC-7174 SATA */

/* VLSI products */
#define	PCI_PRODUCT_VLSI_82C592	0x0005		/* 82C592 CPU */
#define	PCI_PRODUCT_VLSI_82C593	0x0006		/* 82C593 ISA */
#define	PCI_PRODUCT_VLSI_82C594	0x0007		/* 82C594 Wildcat */
#define	PCI_PRODUCT_VLSI_82C596597	0x0008		/* 82C596/597 Wildcat ISA */
#define	PCI_PRODUCT_VLSI_82C541	0x000c		/* 82C541 */
#define	PCI_PRODUCT_VLSI_82C543	0x000d		/* 82C543 */
#define	PCI_PRODUCT_VLSI_82C532	0x0101		/* 82C532 */
#define	PCI_PRODUCT_VLSI_82C534	0x0102		/* 82C534 */
#define	PCI_PRODUCT_VLSI_82C535	0x0104		/* 82C535 */
#define	PCI_PRODUCT_VLSI_82C147	0x0105		/* 82C147 */
#define	PCI_PRODUCT_VLSI_82C975	0x0200		/* 82C975 */
#define	PCI_PRODUCT_VLSI_82C925	0x0280		/* 82C925 */

/* VMware */
#define	PCI_PRODUCT_VMWARE_SVGA2	0x0405		/* SVGA II */
#define	PCI_PRODUCT_VMWARE_SVGA	0x0710		/* SVGA */
#define	PCI_PRODUCT_VMWARE_NET	0x0720		/* VMXNET */
#define	PCI_PRODUCT_VMWARE_VMCI	0x0740		/* VMCI */
#define	PCI_PRODUCT_VMWARE_EHCI	0x0770		/* EHCI */
#define	PCI_PRODUCT_VMWARE_UHCI	0x0774		/* UHCI */
#define	PCI_PRODUCT_VMWARE_XHCI	0x0778		/* xHCI */
#define	PCI_PRODUCT_VMWARE_XHCI_2	0x0779		/* xHCI */
#define	PCI_PRODUCT_VMWARE_PCI	0x0790		/* PCI */
#define	PCI_PRODUCT_VMWARE_PCIE	0x07a0		/* PCIE */
#define	PCI_PRODUCT_VMWARE_NET_3	0x07b0		/* VMXNET3 */
#define	PCI_PRODUCT_VMWARE_PVSCSI	0x07c0		/* PVSCSI */
#define	PCI_PRODUCT_VMWARE_AHCI	0x07e0		/* AHCI */
#define	PCI_PRODUCT_VMWARE_NVME	0x07f0		/* NVMe */
#define	PCI_PRODUCT_VMWARE_VMI	0x0801		/* VMI */
#define	PCI_PRODUCT_VMWARE_HDA	0x1977		/* HD Audio */

/* Vortex Computer Systems products */
#define	PCI_PRODUCT_VORTEX_GDT_60X0	0x0000		/* GDT6000/6020/6050 */
#define	PCI_PRODUCT_VORTEX_GDT_6000B	0x0001		/* GDT6000B/6010 */
#define	PCI_PRODUCT_VORTEX_GDT_6X10	0x0002		/* GDT6110/6510 */
#define	PCI_PRODUCT_VORTEX_GDT_6X20	0x0003		/* GDT6120/6520 */
#define	PCI_PRODUCT_VORTEX_GDT_6530	0x0004		/* GDT6530 */
#define	PCI_PRODUCT_VORTEX_GDT_6550	0x0005		/* GDT6550 */
#define	PCI_PRODUCT_VORTEX_GDT_6X17	0x0006		/* GDT6x17 */
#define	PCI_PRODUCT_VORTEX_GDT_6X27	0x0007		/* GDT6x27 */
#define	PCI_PRODUCT_VORTEX_GDT_6537	0x0008		/* GDT6537 */
#define	PCI_PRODUCT_VORTEX_GDT_6557	0x0009		/* GDT6557 */
#define	PCI_PRODUCT_VORTEX_GDT_6X15	0x000a		/* GDT6x15 */
#define	PCI_PRODUCT_VORTEX_GDT_6X25	0x000b		/* GDT6x25 */
#define	PCI_PRODUCT_VORTEX_GDT_6535	0x000c		/* GDT6535 */
#define	PCI_PRODUCT_VORTEX_GDT_6555	0x000d		/* GDT6555 */
#define	PCI_PRODUCT_VORTEX_GDT_6X17RP	0x0100		/* GDT6x17RP */
#define	PCI_PRODUCT_VORTEX_GDT_6X27RP	0x0101		/* GDT6x27RP */
#define	PCI_PRODUCT_VORTEX_GDT_6537RP	0x0102		/* GDT6537RP */
#define	PCI_PRODUCT_VORTEX_GDT_6557RP	0x0103		/* GDT6557RP */
#define	PCI_PRODUCT_VORTEX_GDT_6X11RP	0x0104		/* GDT6x11RP */
#define	PCI_PRODUCT_VORTEX_GDT_6X21RP	0x0105		/* GDT6x21RP */
#define	PCI_PRODUCT_VORTEX_GDT_6X17RD	0x0110		/* GDT6x17RP1 */
#define	PCI_PRODUCT_VORTEX_GDT_6X27RD	0x0111		/* GDT6x27RP1 */
#define	PCI_PRODUCT_VORTEX_GDT_6537RD	0x0112		/* GDT6537RP1 */
#define	PCI_PRODUCT_VORTEX_GDT_6557RD	0x0113		/* GDT6557RP1 */
#define	PCI_PRODUCT_VORTEX_GDT_6X11RD	0x0114		/* GDT6x11RP1 */
#define	PCI_PRODUCT_VORTEX_GDT_6X21RD	0x0115		/* GDT6x21RP1 */
#define	PCI_PRODUCT_VORTEX_GDT_6X18RD	0x0118		/* GDT6x18RD */
#define	PCI_PRODUCT_VORTEX_GDT_6X28RD	0x0119		/* GDT6x28RD */
#define	PCI_PRODUCT_VORTEX_GDT_6X38RD	0x011a		/* GDT6x38RD */
#define	PCI_PRODUCT_VORTEX_GDT_6X58RD	0x011b		/* GDT6x58RD */
#define	PCI_PRODUCT_VORTEX_GDT_6X17RP2	0x0120		/* GDT6x17RP2 */
#define	PCI_PRODUCT_VORTEX_GDT_6X27RP2	0x0121		/* GDT6x27RP2 */
#define	PCI_PRODUCT_VORTEX_GDT_6537RP2	0x0122		/* GDT6537RP2 */
#define	PCI_PRODUCT_VORTEX_GDT_6557RP2	0x0123		/* GDT6557RP2 */
#define	PCI_PRODUCT_VORTEX_GDT_6X11RP2	0x0124		/* GDT6x11RP2 */
#define	PCI_PRODUCT_VORTEX_GDT_6X21RP2	0x0125		/* GDT6x21RP2 */
#define	PCI_PRODUCT_VORTEX_GDT_6X13RS	0x0136		/* GDT6513RS */
#define	PCI_PRODUCT_VORTEX_GDT_6X23RS	0x0137		/* GDT6523RS */
#define	PCI_PRODUCT_VORTEX_GDT_6518RS	0x0138		/* GDT6518RS */
#define	PCI_PRODUCT_VORTEX_GDT_6X28RS	0x0139		/* GDT6x28RS */
#define	PCI_PRODUCT_VORTEX_GDT_6X38RS	0x013a		/* GDT6x38RS */
#define	PCI_PRODUCT_VORTEX_GDT_6X58RS	0x013b		/* GDT6x58RS */
#define	PCI_PRODUCT_VORTEX_GDT_6X33RS	0x013c		/* GDT6x33RS */
#define	PCI_PRODUCT_VORTEX_GDT_6X43RS	0x013d		/* GDT6x43RS */
#define	PCI_PRODUCT_VORTEX_GDT_6X53RS	0x013e		/* GDT6x53RS */
#define	PCI_PRODUCT_VORTEX_GDT_6X63RS	0x013f		/* GDT6x63RS */
#define	PCI_PRODUCT_VORTEX_GDT_7X13RN	0x0166		/* GDT7x13RN */
#define	PCI_PRODUCT_VORTEX_GDT_7X23RN	0x0167		/* GDT7x23RN */
#define	PCI_PRODUCT_VORTEX_GDT_7X18RN	0x0168		/* GDT7x18RN */
#define	PCI_PRODUCT_VORTEX_GDT_7X28RN	0x0169		/* GDT7x28RN */
#define	PCI_PRODUCT_VORTEX_GDT_7X38RN	0x016a		/* GDT7x38RN */
#define	PCI_PRODUCT_VORTEX_GDT_7X58RN	0x016b		/* GDT7x58RN */
#define	PCI_PRODUCT_VORTEX_GDT_7X43RN	0x016d		/* GDT7x43RN */
#define	PCI_PRODUCT_VORTEX_GDT_7X53RN	0x016e		/* GDT7x53RN */
#define	PCI_PRODUCT_VORTEX_GDT_7X63RN	0x016f		/* GDT7x63RN */
#define	PCI_PRODUCT_VORTEX_GDT_4X13RZ	0x01d6		/* GDT4x13RZ */
#define	PCI_PRODUCT_VORTEX_GDT_4X23RZ	0x01d7		/* GDT4x23RZ */
#define	PCI_PRODUCT_VORTEX_GDT_8X13RZ	0x01f6		/* GDT8x13RZ */
#define	PCI_PRODUCT_VORTEX_GDT_8X23RZ	0x01f7		/* GDT8x23RZ */
#define	PCI_PRODUCT_VORTEX_GDT_8X33RZ	0x01fc		/* GDT8x33RZ */
#define	PCI_PRODUCT_VORTEX_GDT_8X43RZ	0x01fd		/* GDT8x43RZ */
#define	PCI_PRODUCT_VORTEX_GDT_8X53RZ	0x01fe		/* GDT8x53RZ */
#define	PCI_PRODUCT_VORTEX_GDT_8X63RZ	0x01ff		/* GDT8x63RZ */
#define	PCI_PRODUCT_VORTEX_GDT_6X19RD	0x0210		/* GDT6x19RD */
#define	PCI_PRODUCT_VORTEX_GDT_6X29RD	0x0211		/* GDT6x29RD */
#define	PCI_PRODUCT_VORTEX_GDT_7X19RN	0x0260		/* GDT7x19RN */
#define	PCI_PRODUCT_VORTEX_GDT_7X29RN	0x0261		/* GDT7x29RN */
#define	PCI_PRODUCT_VORTEX_GDT_8X22RZ	0x02f6		/* GDT8x22RZ */
#define	PCI_PRODUCT_VORTEX_GDT_ICP	0x0300		/* ICP */
#define	PCI_PRODUCT_VORTEX_GDT_ICP2	0x0301		/* ICP */

/* Beijing WangXun Technology products */
#define	PCI_PRODUCT_WANGXUN_WX1860A2	0x0101		/* WX1860A2 */
#define	PCI_PRODUCT_WANGXUN_WX1860AL1	0x010b		/* WX1860AL1 */

/* Nanjing QinHeng Electronics products */
#define	PCI_PRODUCT_WCH_CH352	0x3253		/* CH352 */
#define	PCI_PRODUCT_WCH2_CH351	0x2273		/* CH351 */
#define	PCI_PRODUCT_WCH2_CH382_2	0x3250		/* CH382 */
#define	PCI_PRODUCT_WCH2_CH382_1	0x3253		/* CH382 */

/* Western Digital products */
#define	PCI_PRODUCT_WD_WD33C193A	0x0193		/* WD33C193A */
#define	PCI_PRODUCT_WD_WD33C196A	0x0196		/* WD33C196A */
#define	PCI_PRODUCT_WD_WD33C197A	0x0197		/* WD33C197A */
#define	PCI_PRODUCT_WD_WD7193	0x3193		/* WD7193 */
#define	PCI_PRODUCT_WD_WD7197	0x3197		/* WD7197 */
#define	PCI_PRODUCT_WD_WD33C296A	0x3296		/* WD33C296A */
#define	PCI_PRODUCT_WD_WD34C296	0x4296		/* WD34C296 */
#define	PCI_PRODUCT_WD_WD9710	0x9710		/* WD9610 */
#define	PCI_PRODUCT_WD_90C	0xc24a		/* 90C */

/* Weitek products */
#define	PCI_PRODUCT_WEITEK_P9000	0x9001		/* P9000 */
#define	PCI_PRODUCT_WEITEK_P9100	0x9100		/* P9100 */

/* Winbond Electronics products */
#define	PCI_PRODUCT_WINBOND_W83769F	0x0001		/* W83769F */
#define	PCI_PRODUCT_WINBOND_W83C553F_1	0x0105		/* W83C553F */
#define	PCI_PRODUCT_WINBOND_W83C553F_0	0x0565		/* W83C553F ISA */
#define	PCI_PRODUCT_WINBOND_W89C840F	0x0840		/* W89C840F */
#define	PCI_PRODUCT_WINBOND_W89C940F	0x0940		/* Ethernet */
#define	PCI_PRODUCT_WINBOND_W89C940F_1	0x5a5a		/* W89C940F */
#define	PCI_PRODUCT_WINBOND_W6692	0x6692		/* W6692 ISDN */

/* Winbond Electronics products (PCI products set 2) */
#define	PCI_PRODUCT_WINBOND2_W89C940	0x1980		/* Ethernet */

/* Workbit products */
#define	PCI_PRODUCT_WORKBIT_CF32A_1	0xf021		/* CF32A */
#define	PCI_PRODUCT_WORKBIT_CF32A_2	0xf024		/* CF32A */

/* XenSource products */
#define	PCI_PRODUCT_XENSOURCE_PLATFORMDEV	0x0001		/* Platform Device */

/* XGI Technology products */
#define	PCI_PRODUCT_XGI_VOLARI_Z7	0x0020		/* Volari Z7 */
#define	PCI_PRODUCT_XGI_VOLARI_Z9	0x0021		/* Volari Z9s/Z9m */
#define	PCI_PRODUCT_XGI_VOLARI_V3XT	0x0040		/* Volari V3XT */

/* Xircom products */
#define	PCI_PRODUCT_XIRCOM_X3201_3	0x0002		/* X3201-3 */
#define	PCI_PRODUCT_XIRCOM_X3201_3_21143	0x0003		/* X3201-3 */
#define	PCI_PRODUCT_XIRCOM_CARDBUS_ETH_1	0x0005		/* Ethernet */
#define	PCI_PRODUCT_XIRCOM_CARDBUS_ETH_2	0x0007		/* Ethernet */
#define	PCI_PRODUCT_XIRCOM_CARDBUS_ETH_3	0x000b		/* Ethernet */
#define	PCI_PRODUCT_XIRCOM_MPCI_MODEM_V90	0x000c		/* Modem */
#define	PCI_PRODUCT_XIRCOM_CARDBUS_ETH_4	0x000f		/* Ethernet */
#define	PCI_PRODUCT_XIRCOM_MPCI_MODEM_K56	0x00d4		/* Modem */
#define	PCI_PRODUCT_XIRCOM_MODEM_56K	0x0101		/* Modem */
#define	PCI_PRODUCT_XIRCOM_MODEM56	0x0103		/* Modem */
#define	PCI_PRODUCT_XIRCOM_CBEM56G	0x0105		/* Modem */

/* Yamaha products */
#define	PCI_PRODUCT_YAMAHA_YMF724	0x0004		/* 724 */
#define	PCI_PRODUCT_YAMAHA_YMF734	0x0005		/* 734 */
#define	PCI_PRODUCT_YAMAHA_YMF738_TEG	0x0006		/* 738 */
#define	PCI_PRODUCT_YAMAHA_YMF737	0x0008		/* 737 */
#define	PCI_PRODUCT_YAMAHA_YMF740	0x000a		/* 740 */
#define	PCI_PRODUCT_YAMAHA_YMF740C	0x000c		/* 740C */
#define	PCI_PRODUCT_YAMAHA_YMF724F	0x000d		/* 724F */
#define	PCI_PRODUCT_YAMAHA_YMF744	0x0010		/* 744 */
#define	PCI_PRODUCT_YAMAHA_YMF754	0x0012		/* 754 */
#define	PCI_PRODUCT_YAMAHA_YMF738	0x0020		/* 738 */

/* Yangtze Memory products */
#define	PCI_PRODUCT_YMTC_PC005	0x1001		/* PC005 */

/* Zeinet products */
#define	PCI_PRODUCT_ZEINET_1221	0x0001		/* 1221 */

/* Zhaoxin products */
#define	PCI_PRODUCT_ZHAOXIN_STORX_AHCI	0x9083		/* StorX AHCI */

/* Ziatech products */
#define	PCI_PRODUCT_ZIATECH_ZT8905	0x8905		/* PCI-ST32 */

/* Zoltrix products */
#define	PCI_PRODUCT_ZOLTRIX_GENIE_TV_FM	0x400d		/* Genie TV/FM */

/* Zoran products */
#define	PCI_PRODUCT_ZORAN_ZR36057	0x6057		/* TV */
#define	PCI_PRODUCT_ZORAN_ZR36120	0x6120		/* DVD */

/* ZyDAS Technology products */
#define	PCI_PRODUCT_ZYDAS_ZD1201	0x2100		/* ZD1201 */
#define	PCI_PRODUCT_ZYDAS_ZD1202	0x2102		/* ZD1202 */
#define	PCI_PRODUCT_ZYDAS_ZD1205	0x2105		/* ZD1205 */
