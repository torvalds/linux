/*	$OpenBSD: nmadef.h,v 1.4 2003/06/02 21:38:39 maja Exp $ */

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
 *
 *	$OpenBSD: nmadef.h,v 1.4 2003/06/02 21:38:39 maja Exp $
 *
 */

#ifndef _NMADEF_H_
#define _NMADEF_H_

#define   NMA_C_SOFD_DP    0	/* DP11-DA */
#define   NMA_C_SOFD_UNA   1	/* DEUNA */
#define   NMA_C_SOFD_DU    2	/* DU11-DA */
#define   NMA_C_SOFD_CNA   3	/* DECNA */
#define   NMA_C_SOFD_DL    4	/* DL11-C, -E, or -WA */
#define   NMA_C_SOFD_QNA   5	/* DEQNA */
#define   NMA_C_SOFD_DQ    6	/* DQ11-DA */
#define   NMA_C_SOFD_CI    7	/* Computer Interconnect */
#define   NMA_C_SOFD_DA    8	/* DA11-B or -AL */
#define   NMA_C_SOFD_PCL   9	/* PCL11-B */
#define   NMA_C_SOFD_DUP   10	/* DUP11-DA */
#define   NMA_C_SOFD_LUA   11	/* DELUA */
#define   NMA_C_SOFD_DMC   12	/* DMC11-DA/AR, -FA/AR, -MA/AL or -MD/AL */
#define   NMA_C_SOFD_LNA   13	/* MicroServer Lance */
#define   NMA_C_SOFD_DN    14	/* DN11-BA or -AA */
#define   NMA_C_SOFD_DLV   16	/* DLV11-E, -F, -J, MXV11-A or -B */
#define   NMA_C_SOFD_LCS   17	/* DECServer 100 */
#define   NMA_C_SOFD_DMP   18	/* DMP11 */
#define   NMA_C_SOFD_AMB   19	/* AMBER */
#define   NMA_C_SOFD_DTE   20	/* DTE20 */
#define   NMA_C_SOFD_DBT   21	/* DEBET */
#define   NMA_C_SOFD_DV    22	/* DV11-AA/BA */
#define   NMA_C_SOFD_BNA   23	/* DEBNA */
#define   NMA_C_SOFD_BNT   23	/* DEBNT */
#define   NMA_C_SOFD_DZ    24	/* DZ11-A, -B, -C, -D */
#define   NMA_C_SOFD_LPC   25	/* PCXX */
#define   NMA_C_SOFD_DSV   26	/* DSV11 */
#define   NMA_C_SOFD_CEC   27	/* 3-COM/IBM-PC */
#define   NMA_C_SOFD_KDP   28	/* KMC11/DUP11-DA */
#define   NMA_C_SOFD_IEC   29	/* Interlan/IBM-PC */
#define   NMA_C_SOFD_KDZ   30	/* KMC11/DZ11-A, -B, -C, or -D */
#define   NMA_C_SOFD_UEC   31	/* Univation/RAINBOW-100 */
#define   NMA_C_SOFD_KL8   32	/* KL8-J */
#define   NMA_C_SOFD_DS2   33	/* DECServer 200 */
#define   NMA_C_SOFD_DMV   34	/* DMV11 */
#define   NMA_C_SOFD_DS5   35	/* DECServer 500 */
#define   NMA_C_SOFD_DPV   36	/* DPV11 */
#define   NMA_C_SOFD_LQA   37	/* DELQA */
#define   NMA_C_SOFD_DMF   38	/* DMF32 */
#define   NMA_C_SOFD_SVA   39	/* DESVA */
#define   NMA_C_SOFD_DMR   40	/* DMR11-AA, -AB, -AC, or -AE */
#define   NMA_C_SOFD_MUX   41	/* MUXserver */
#define   NMA_C_SOFD_KMY   42	/* KMS11-PX */
#define   NMA_C_SOFD_DEP   43	/* DEPCA PCSG/IBM-PC */
#define   NMA_C_SOFD_KMX   44	/* KMS11-BD/BE */
#define   NMA_C_SOFD_LTM   45	/* LTM Ethernet monitor */
#define   NMA_C_SOFD_DMB   46	/* DMB-32 */
#define   NMA_C_SOFD_DES   47	/* DESNC */
#define   NMA_C_SOFD_KCP   48	/* KCP */
#define   NMA_C_SOFD_MX3   49	/* MUXServer 300 */
#define   NMA_C_SOFD_SYN   50	/* MicroServer */
#define   NMA_C_SOFD_MEB   51	/* DEMEB */
#define   NMA_C_SOFD_DSB   52	/* DSB32 */
#define   NMA_C_SOFD_BAM   53	/* DEBAM LANBridge-200 */
#define   NMA_C_SOFD_DST   54	/* DST-32 TEAMmate */
#define   NMA_C_SOFD_FAT   55	/* DEFAT */
#define   NMA_C_SOFD_RSM   56	/* DERSM - Remote Segment Monitor */
#define   NMA_C_SOFD_RES   57	/* DERES - Remote Environmental Sensor */
#define   NMA_C_SOFD_3C2   58	/* 3COM Etherlink II (3C503) */
#define   NMA_C_SOFD_3CM   59	/* 3COM Etherlink/MC (3C523) */
#define   NMA_C_SOFD_DS3   60	/* DECServer 300 */
#define   NMA_C_SOFD_MF2   61	/* Mayfair-2 */
#define   NMA_C_SOFD_MMR   62	/* DEMMR */
#define   NMA_C_SOFD_VIT   63	/* Vitalink TransLAN III/IV (NP3A) Bridge  */
#define   NMA_C_SOFD_VT5   64	/* Vitalink TransLAN 350 (NPC25) Bridge  */
#define   NMA_C_SOFD_BNI   65	/* DEBNI */
#define   NMA_C_SOFD_MNA   66	/* DEMNA */
#define   NMA_C_SOFD_PMX   67	/* PMAX (KN01) */
#define   NMA_C_SOFD_NI5   68	/* Interlan NI5210-8 */
#define   NMA_C_SOFD_NI9   69	/* Interlan NI9210 */
#define   NMA_C_SOFD_KMK   70	/* KMS11-K */
#define   NMA_C_SOFD_3CP   71	/* 3COM Etherlink Plus (3C505)  */
#define   NMA_C_SOFD_DP2   72	/* DPNserver-200 */
#define   NMA_C_SOFD_ISA   73	/* SGEC */
#define   NMA_C_SOFD_DIV   74	/* DIV-32 DEC WAN controller-100 */
#define   NMA_C_SOFD_QTA   75	/* DEQTA */
#define   NMA_C_SOFD_B15   76	/* LANbridge-150 */
#define   NMA_C_SOFD_WD8   77	/* WD8003 Family */
#define   NMA_C_SOFD_ILA   78	/* BICC ISOLAN 4110-2 */
#define   NMA_C_SOFD_ILM   79	/* BICC ISOLAN 4110-3 */
#define   NMA_C_SOFD_APR   80	/* Apricot Xen-S and Qi */
#define   NMA_C_SOFD_ASN   81	/* AST EtherNode */
#define   NMA_C_SOFD_ASE   82	/* AST Ethernet */
#define   NMA_C_SOFD_TRW   83	/* TRW HC-2001 */
#define   NMA_C_SOFD_EDX   84	/* Ethernet-XT/AT */
#define   NMA_C_SOFD_EDA   85	/* Ethernet-AT */
#define   NMA_C_SOFD_DR2   86	/* DECrouter-250 */
#define   NMA_C_SOFD_SCC   87	/* DECrouter-250 DUSCC */
#define   NMA_C_SOFD_DCA   88	/* DCA Series 300 */
#define   NMA_C_SOFD_TIA   89	/* LANcard/E */
#define   NMA_C_SOFD_FBN   90	/* DEFEB DECbridge-500 */
#define   NMA_C_SOFD_FEB   91	/* DEFEB DECbridge-500 FDDI */
#define   NMA_C_SOFD_FCN   92	/* DEFCN DECconcentrator-500 */
#define   NMA_C_SOFD_MFA   93	/* DEMFA */
#define   NMA_C_SOFD_MXE   94	/* MIPS workstation family */
#define   NMA_C_SOFD_CED   95	/* Cabletron Ethernet Desktop */
#define   NMA_C_SOFD_C20   96	/* 3Com CS/200 */
#define   NMA_C_SOFD_CS1   97	/* 3Com CS/1 */
#define   NMA_C_SOFD_C2M   98	/* 3Com CS/210, CS/2000, CS/2100 */
#define   NMA_C_SOFD_ACA   99	/* ACA/32000 system */
#define   NMA_C_SOFD_GSM   100	/* Gandalf StarMaster */
#define   NMA_C_SOFD_DSF   101	/* DSF32 */
#define   NMA_C_SOFD_CS5   102	/* 3Com CS/50 */
#define   NMA_C_SOFD_XIR   103	/* XIRCOM PE10B2 */
#define   NMA_C_SOFD_KFE   104	/* KFE52 */
#define   NMA_C_SOFD_RT3   105	/* rtVAX-300 */
#define   NMA_C_SOFD_SPI   106	/* Spiderport M250 */
#define   NMA_C_SOFD_FOR   107	/* LAT gateway */
#define   NMA_C_SOFD_MER   108	/* Meridian */
#define   NMA_C_SOFD_PER   109	/* Persoft */
#define   NMA_C_SOFD_STR   110	/* AT&T StarLan-10 */
#define   NMA_C_SOFD_MPS   111	/* MIPSfair */
#define   NMA_C_SOFD_L20   112	/* LPS20 print server */
#define   NMA_C_SOFD_VT2   113	/* Vitalink TransLAN 320 Bridge */
#define   NMA_C_SOFD_DWT   114	/* VT-1000 */
#define   NMA_C_SOFD_WGB   115	/* DEWGB */
#define   NMA_C_SOFD_ZEN   116	/* Zenith Z-LAN4000, Z-LAN */
#define   NMA_C_SOFD_TSS   117	/* Thursby Software Systems */
#define   NMA_C_SOFD_MNE   118	/* 3MIN (KN02-BA) */
#define   NMA_C_SOFD_FZA   119	/* DEFZA */
#define   NMA_C_SOFD_90L   120	/* DS90L */
#define   NMA_C_SOFD_CIS   121	/* Cisco Systems */
#define   NMA_C_SOFD_STC   122	/* STRTC */
#define   NMA_C_SOFD_UBE   123	/* Ungermann-Bass PC2030, PC3030 */
#define   NMA_C_SOFD_DW2   124	/* DECwindows terminal II */
#define   NMA_C_SOFD_FUE   125	/* Fujitsu Etherstar MB86950 */
#define   NMA_C_SOFD_M38   126	/* MUXServer 380 */
#define   NMA_C_SOFD_NTI   127	/* NTI Group PC Ethernet Card */
#define   NMA_C_SOFD_RAD   130	/* RADLINX LAN Gateway */
#define   NMA_C_SOFD_INF   131	/* Infotron Commix */
#define   NMA_C_SOFD_XMX   132	/* Xyplex MAXserver */
#define   NMA_C_SOFD_NDI   133	/* NDIS data link driver for MS/DOS systems */
#define   NMA_C_SOFD_ND2   134	/* NDIS data link driver for OS/2 systems */
#define   NMA_C_SOFD_TRN   135	/* DEC LANcontroller 520 */
#define   NMA_C_SOFD_DEV   136	/* Develcon Electronics Ltd. LAT gateway */
#define   NMA_C_SOFD_ACE   137	/* Acer 5220, 5270 adapter */
#define   NMA_C_SOFD_PNT   138	/* ProNet-4/18 #1390 */
#define   NMA_C_SOFD_ISE   139	/* Network Integration Server 600 */
#define   NMA_C_SOFD_IST   140	/* Network Integration Server 600 T1 */
#define   NMA_C_SOFD_ISH   141	/* Network Integration Server 64 kb HDLC */
#define   NMA_C_SOFD_ISF   142	/* Network Integration Server 600 FDDI */
#define   NMA_C_SOFD_DSW   149	/* DSW-21 */
#define   NMA_C_SOFD_DW4   150	/* DSW-41/42 */
#define   NMA_C_SOFD_TRA   175	/* DETRA-AA */

#endif /* _NMADEF_H_ */
