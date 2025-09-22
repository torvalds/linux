/*	$OpenBSD: nma.c,v 1.7 2009/10/27 23:59:52 deraadt Exp $ */

/*
 * Copyright (c) 1995 Mats O Jansson.  All rights reserved.
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

#include <stddef.h>
#include "common/nmadef.h"

struct commDev {
	int		val;
	char		*sname;
	char		*name;
};

struct commDev nmaCommDev[] = {
	{ NMA_C_SOFD_DP , "DP ", "DP11-DA" },
	{ NMA_C_SOFD_UNA, "UNA", "DEUNA" },
	{ NMA_C_SOFD_DU , "DU ", "DU11-DA" },
	{ NMA_C_SOFD_CNA, "CNA", "DECNA" },
	{ NMA_C_SOFD_DL , "DL ", "DL11-C, -E, or -WA" },
	{ NMA_C_SOFD_QNA, "QNA", "DEQNA" },
	{ NMA_C_SOFD_DQ , "DQ ", "DQ11-DA" },
	{ NMA_C_SOFD_CI , "CI ", "Computer Interconnect" },
	{ NMA_C_SOFD_DA , "DA ", "DA11-B or -AL" },
	{ NMA_C_SOFD_PCL, "PCL", "PCL11-B" },
	{ NMA_C_SOFD_DUP, "DUP", "DUP11-DA" },
	{ NMA_C_SOFD_LUA, "LUA", "DELUA" },
	{ NMA_C_SOFD_DMC, "DMC", "DMC11-DA/AR, -FA/AR, -MA/AL or -MD/AL" },
	{ NMA_C_SOFD_LNA, "LNA", "MicroServer Lance" },
	{ NMA_C_SOFD_DN , "DN ", "DN11-BA or -AA" },
	{ NMA_C_SOFD_DLV, "DLV", "DLV11-E, -F, -J, MXV11-A or -B" },
	{ NMA_C_SOFD_LCS, "LCS", "DECServer 100" },
	{ NMA_C_SOFD_DMP, "DMP", "DMP11" },
	{ NMA_C_SOFD_AMB, "AMB", "AMBER" },
	{ NMA_C_SOFD_DTE, "DTE", "DTE20" },
	{ NMA_C_SOFD_DBT, "DBT", "DEBET" },
	{ NMA_C_SOFD_DV , "DV ", "DV11-AA/BA" },
	{ NMA_C_SOFD_BNA, "BNA", "DEBNA" },
	{ NMA_C_SOFD_BNT, "BNT", "DEBNT" },
	{ NMA_C_SOFD_DZ , "DZ ", "DZ11-A, -B, -C, -D" },
	{ NMA_C_SOFD_LPC, "LPC", "PCXX" },
	{ NMA_C_SOFD_DSV, "DSV", "DSV11" },
	{ NMA_C_SOFD_CEC, "CEC", "3-COM/IBM-PC" },
	{ NMA_C_SOFD_KDP, "KDP", "KMC11/DUP11-DA" },
	{ NMA_C_SOFD_IEC, "IEC", "Interlan/IBM-PC" },
	{ NMA_C_SOFD_KDZ, "KDZ", "KMC11/DZ11-A, -B, -C, or -D" },
	{ NMA_C_SOFD_UEC, "UEC", "Univation/RAINBOW-100" },
	{ NMA_C_SOFD_KL8, "KL8", "KL8-J" },
	{ NMA_C_SOFD_DS2, "DS2", "DECServer 200" },
	{ NMA_C_SOFD_DMV, "DMV", "DMV11" },
	{ NMA_C_SOFD_DS5, "DS5", "DECServer 500" },
	{ NMA_C_SOFD_DPV, "DPV", "DPV11" },
	{ NMA_C_SOFD_LQA, "LQA", "DELQA" },
	{ NMA_C_SOFD_DMF, "DMF", "DMF32" },
	{ NMA_C_SOFD_SVA, "SVA", "DESVA" },
	{ NMA_C_SOFD_DMR, "DMR", "DMR11-AA, -AB, -AC, or -AE" },
	{ NMA_C_SOFD_MUX, "MUX", "MUXserver" },
	{ NMA_C_SOFD_KMY, "KMY", "KMS11-PX" },
	{ NMA_C_SOFD_DEP, "DEP", "DEPCA PCSG/IBM-PC" },
	{ NMA_C_SOFD_KMX, "KMX", "KMS11-BD/BE" },
	{ NMA_C_SOFD_LTM, "LTM", "LTM Ethernet monitor" },
	{ NMA_C_SOFD_DMB, "DMB", "DMB-32" },
	{ NMA_C_SOFD_DES, "DES", "DESNC" },
	{ NMA_C_SOFD_KCP, "KCP", "KCP" },
	{ NMA_C_SOFD_MX3, "MX3", "MUXServer 300" },
	{ NMA_C_SOFD_SYN, "SYN", "MicroServer" },
	{ NMA_C_SOFD_MEB, "MEB", "DEMEB" },
	{ NMA_C_SOFD_DSB, "DSB", "DSB32" },
	{ NMA_C_SOFD_BAM, "BAM", "DEBAM LANBridge-200" },
	{ NMA_C_SOFD_DST, "DST", "DST-32 TEAMmate" },
	{ NMA_C_SOFD_FAT, "FAT", "DEFAT" },
	{ NMA_C_SOFD_RSM, "RSM", "DERSM - Remote Segment Monitor" },
	{ NMA_C_SOFD_RES, "RES", "DERES - Remote Environmental Sensor" },
	{ NMA_C_SOFD_3C2, "3C2", "3COM Etherlink II (3C503)" },
	{ NMA_C_SOFD_3CM, "3CM", "3COM Etherlink/MC (3C523)" },
	{ NMA_C_SOFD_DS3, "DS3", "DECServer 300" },
	{ NMA_C_SOFD_MF2, "MF2", "Mayfair-2" },
	{ NMA_C_SOFD_MMR, "MMR", "DEMMR" },
	{ NMA_C_SOFD_VIT, "VIT", "Vitalink TransLAN III/IV (NP3A) Bridge " },
	{ NMA_C_SOFD_VT5, "VT5", "Vitalink TransLAN 350 (NPC25) Bridge " },
	{ NMA_C_SOFD_BNI, "BNI", "DEBNI" },
	{ NMA_C_SOFD_MNA, "MNA", "DEMNA" },
	{ NMA_C_SOFD_PMX, "PMX", "PMAX (KN01)" },
	{ NMA_C_SOFD_NI5, "NI5", "Interlan NI5210-8" },
	{ NMA_C_SOFD_NI9, "NI9", "Interlan NI9210" },
	{ NMA_C_SOFD_KMK, "KMK", "KMS11-K" },
	{ NMA_C_SOFD_3CP, "3CP", "3COM Etherlink Plus (3C505) " },
	{ NMA_C_SOFD_DP2, "DP2", "DPNserver-200" },
	{ NMA_C_SOFD_ISA, "ISA", "SGEC" },
	{ NMA_C_SOFD_DIV, "DIV", "DIV-32 DEC WAN controller-100" },
	{ NMA_C_SOFD_QTA, "QTA", "DEQTA" },
	{ NMA_C_SOFD_B15, "B15", "LANbridge-150" },
	{ NMA_C_SOFD_WD8, "WD8", "WD8003 Family" },
	{ NMA_C_SOFD_ILA, "ILA", "BICC ISOLAN 4110-2" },
	{ NMA_C_SOFD_ILM, "ILM", "BICC ISOLAN 4110-3" },
	{ NMA_C_SOFD_APR, "APR", "Apricot Xen-S and Qi" },
	{ NMA_C_SOFD_ASN, "ASN", "AST EtherNode" },
	{ NMA_C_SOFD_ASE, "ASE", "AST Ethernet" },
	{ NMA_C_SOFD_TRW, "TRW", "TRW HC-2001" },
	{ NMA_C_SOFD_EDX, "EDX", "Ethernet-XT/AT" },
	{ NMA_C_SOFD_EDA, "EDA", "Ethernet-AT" },
	{ NMA_C_SOFD_DR2, "DR2", "DECrouter-250" },
	{ NMA_C_SOFD_SCC, "SCC", "DECrouter-250 DUSCC" },
	{ NMA_C_SOFD_DCA, "DCA", "DCA Series 300" },
	{ NMA_C_SOFD_TIA, "TIA", "LANcard/E" },
	{ NMA_C_SOFD_FBN, "FBN", "DEFEB DECbridge-500" },
	{ NMA_C_SOFD_FEB, "FEB", "DEFEB DECbridge-500 FDDI" },
	{ NMA_C_SOFD_FCN, "FCN", "DEFCN DECconcentrator-500" },
	{ NMA_C_SOFD_MFA, "MFA", "DEMFA" },
	{ NMA_C_SOFD_MXE, "MXE", "MIPS workstation family" },
	{ NMA_C_SOFD_CED, "CED", "Cabletron Ethernet Desktop" },
	{ NMA_C_SOFD_C20, "C20", "3Com CS/200" },
	{ NMA_C_SOFD_CS1, "CS1", "3Com CS/1" },
	{ NMA_C_SOFD_C2M, "C2M", "3Com CS/210, CS/2000, CS/2100" },
	{ NMA_C_SOFD_ACA, "ACA", "ACA/32000 system" },
	{ NMA_C_SOFD_GSM, "GSM", "Gandalf StarMaster" },
	{ NMA_C_SOFD_DSF, "DSF", "DSF32" },
	{ NMA_C_SOFD_CS5, "CS5", "3Com CS/50" },
	{ NMA_C_SOFD_XIR, "XIR", "XIRCOM PE10B2" },
	{ NMA_C_SOFD_KFE, "KFE", "KFE52" },
	{ NMA_C_SOFD_RT3, "RT3", "rtVAX-300" },
	{ NMA_C_SOFD_SPI, "SPI", "Spiderport M250" },
	{ NMA_C_SOFD_FOR, "FOR", "LAT gateway" },
	{ NMA_C_SOFD_MER, "MER", "Meridian" },
	{ NMA_C_SOFD_PER, "PER", "Persoft" },
	{ NMA_C_SOFD_STR, "STR", "AT&T StarLan-10" },
	{ NMA_C_SOFD_MPS, "MPS", "MIPSfair" },
	{ NMA_C_SOFD_L20, "L20", "LPS20 print server" },
	{ NMA_C_SOFD_VT2, "VT2", "Vitalink TransLAN 320 Bridge" },
	{ NMA_C_SOFD_DWT, "DWT", "VT-1000" },
	{ NMA_C_SOFD_WGB, "WGB", "DEWGB" },
	{ NMA_C_SOFD_ZEN, "ZEN", "Zenith Z-LAN4000, Z-LAN" },
	{ NMA_C_SOFD_TSS, "TSS", "Thursby Software Systems" },
	{ NMA_C_SOFD_MNE, "MNE", "3MIN (KN02-BA)" },
	{ NMA_C_SOFD_FZA, "FZA", "DEFZA" },
	{ NMA_C_SOFD_90L, "90L", "DS90L" },
	{ NMA_C_SOFD_CIS, "CIS", "Cisco Systems" },
	{ NMA_C_SOFD_STC, "STC", "STRTC" },
	{ NMA_C_SOFD_UBE, "UBE", "Ungermann-Bass PC2030, PC3030" },
	{ NMA_C_SOFD_DW2, "DW2", "DECwindows terminal II" },
	{ NMA_C_SOFD_FUE, "FUE", "Fujitsu Etherstar MB86950" },
	{ NMA_C_SOFD_M38, "M38", "MUXServer 380" },
	{ NMA_C_SOFD_NTI, "NTI", "NTI Group PC Ethernet Card" },
	{ NMA_C_SOFD_RAD, "RAD", "RADLINX LAN Gateway" },
	{ NMA_C_SOFD_INF, "INF", "Infotron Commix" },
	{ NMA_C_SOFD_XMX, "XMX", "Xyplex MAXserver" },
	{ NMA_C_SOFD_NDI, "NDI", "NDIS data link driver for MS/DOS systems" },
	{ NMA_C_SOFD_ND2, "ND2", "NDIS data link driver for OS/2 systems" },
	{ NMA_C_SOFD_TRN, "TRN", "DEC LANcontroller 520" },
	{ NMA_C_SOFD_DEV, "DEV", "Develcon Electronics Ltd. LAT gateway" },
	{ NMA_C_SOFD_ACE, "ACE", "Acer 5220, 5270 adapter" },
	{ NMA_C_SOFD_PNT, "PNT", "ProNet-4/18 #1390" },
	{ NMA_C_SOFD_ISE, "ISE", "Network Integration Server 600" },
	{ NMA_C_SOFD_IST, "IST", "Network Integration Server 600 T1" },
	{ NMA_C_SOFD_ISH, "ISH", "Network Integration Server 64 kb HDLC" },
	{ NMA_C_SOFD_ISF, "ISF", "Network Integration Server 600 FDDI" },
	{ NMA_C_SOFD_DSW, "DSW", "DSW-21" },
	{ NMA_C_SOFD_DW4, "DW4", "DSW-41/42" },
	{ NMA_C_SOFD_TRA, "TRA", "DETRA-AA" },
	{ 0, 0, 0 },
};

char *
nmaGetShort(int devno)
{
	struct commDev *current;

	current = nmaCommDev;

	while (current->sname != NULL) {
		if (current->val == devno)
			break;
		current++;
	}

	return (current->sname);
}

char *
nmaGetDevice(int devno)
{
	struct commDev *current;

	current = nmaCommDev;

	while (current->name != NULL) {
		if (current->val == devno)
			break;
		current++;
	}

	return (current->name);
}
