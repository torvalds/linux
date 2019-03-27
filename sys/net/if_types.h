/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)if_types.h	8.3 (Berkeley) 4/28/95
 * $FreeBSD$
 * $NetBSD: if_types.h,v 1.16 2000/04/19 06:30:53 itojun Exp $
 */

#ifndef _NET_IF_TYPES_H_
#define _NET_IF_TYPES_H_

/*
 * Interface types for benefit of parsing media address headers.
 * This list is derived from the SNMP list of ifTypes, originally
 * documented in RFC1573, now maintained as:
 *
 * 	http://www.iana.org/assignments/smi-numbers
 */

typedef enum {
	IFT_OTHER	= 0x1,		/* none of the following */
	IFT_1822	= 0x2,		/* old-style arpanet imp */
	IFT_HDH1822	= 0x3,		/* HDH arpanet imp */
	IFT_X25DDN	= 0x4,		/* x25 to imp */
	IFT_X25		= 0x5,		/* PDN X25 interface (RFC877) */
	IFT_ETHER	= 0x6,		/* Ethernet CSMA/CD */
	IFT_ISO88023	= 0x7,		/* CMSA/CD */
	IFT_ISO88024	= 0x8,		/* Token Bus */
	IFT_ISO88025	= 0x9,		/* Token Ring */
	IFT_ISO88026	= 0xa,		/* MAN */
	IFT_STARLAN	= 0xb,
	IFT_P10		= 0xc,		/* Proteon 10MBit ring */
	IFT_P80		= 0xd,		/* Proteon 80MBit ring */
	IFT_HY		= 0xe,		/* Hyperchannel */
	IFT_FDDI	= 0xf,
	IFT_LAPB	= 0x10,
	IFT_SDLC	= 0x11,
	IFT_T1		= 0x12,
	IFT_CEPT	= 0x13,		/* E1 - european T1 */
	IFT_ISDNBASIC	= 0x14,
	IFT_ISDNPRIMARY	= 0x15,
	IFT_PTPSERIAL	= 0x16,		/* Proprietary PTP serial */
	IFT_PPP		= 0x17,		/* RFC 1331 */
	IFT_LOOP	= 0x18,		/* loopback */
	IFT_EON		= 0x19,		/* ISO over IP */
	IFT_XETHER	= 0x1a,		/* obsolete 3MB experimental ethernet */
	IFT_NSIP	= 0x1b,		/* XNS over IP */
	IFT_SLIP	= 0x1c,		/* IP over generic TTY */
	IFT_ULTRA	= 0x1d,		/* Ultra Technologies */
	IFT_DS3		= 0x1e,		/* Generic T3 */
	IFT_SIP		= 0x1f,		/* SMDS */
	IFT_FRELAY	= 0x20,		/* Frame Relay DTE only */
	IFT_RS232	= 0x21,
	IFT_PARA	= 0x22,		/* parallel-port */
	IFT_ARCNET	= 0x23,
	IFT_ARCNETPLUS	= 0x24,
	IFT_ATM		= 0x25,		/* ATM cells */
	IFT_MIOX25	= 0x26,
	IFT_SONET	= 0x27,		/* SONET or SDH */
	IFT_X25PLE	= 0x28,
	IFT_ISO88022LLC	= 0x29,
	IFT_LOCALTALK	= 0x2a,
	IFT_SMDSDXI	= 0x2b,
	IFT_FRELAYDCE	= 0x2c,		/* Frame Relay DCE */
	IFT_V35		= 0x2d,
	IFT_HSSI	= 0x2e,
	IFT_HIPPI	= 0x2f,
	IFT_MODEM	= 0x30,		/* Generic Modem */
	IFT_AAL5	= 0x31,		/* AAL5 over ATM */
	IFT_SONETPATH	= 0x32,
	IFT_SONETVT	= 0x33,
	IFT_SMDSICIP	= 0x34,		/* SMDS InterCarrier Interface */
	IFT_PROPVIRTUAL	= 0x35,		/* Proprietary Virtual/internal */
	IFT_PROPMUX	= 0x36,		/* Proprietary Multiplexing */
	IFT_IEEE80212	= 0x37,		/* 100BaseVG */
	IFT_FIBRECHANNEL = 0x38,	/* Fibre Channel */
	IFT_HIPPIINTERFACE = 0x39,	/* HIPPI interfaces	 */
	IFT_FRAMERELAYINTERCONNECT = 0x3a, /* Obsolete, use 0x20 either 0x2c */
	IFT_AFLANE8023	= 0x3b,		/* ATM Emulated LAN for 802.3 */
	IFT_AFLANE8025	= 0x3c,		/* ATM Emulated LAN for 802.5 */
	IFT_CCTEMUL	= 0x3d,		/* ATM Emulated circuit		  */
	IFT_FASTETHER	= 0x3e,		/* Fast Ethernet (100BaseT) */
	IFT_ISDN	= 0x3f,		/* ISDN and X.25	    */
	IFT_V11		= 0x40,		/* CCITT V.11/X.21		*/
	IFT_V36		= 0x41,		/* CCITT V.36			*/
	IFT_G703AT64K	= 0x42,		/* CCITT G703 at 64Kbps */
	IFT_G703AT2MB	= 0x43,		/* Obsolete see DS1-MIB */
	IFT_QLLC	= 0x44,		/* SNA QLLC			*/
	IFT_FASTETHERFX	= 0x45,		/* Fast Ethernet (100BaseFX)	*/
	IFT_CHANNEL	= 0x46,		/* channel			*/
	IFT_IEEE80211	= 0x47,		/* radio spread spectrum (unused) */
	IFT_IBM370PARCHAN = 0x48,	/* IBM System 360/370 OEMI Channel */
	IFT_ESCON	= 0x49,		/* IBM Enterprise Systems Connection */
	IFT_DLSW	= 0x4a,		/* Data Link Switching */
	IFT_ISDNS	= 0x4b,		/* ISDN S/T interface */
	IFT_ISDNU	= 0x4c,		/* ISDN U interface */
	IFT_LAPD	= 0x4d,		/* Link Access Protocol D */
	IFT_IPSWITCH	= 0x4e,		/* IP Switching Objects */
	IFT_RSRB	= 0x4f,		/* Remote Source Route Bridging */
	IFT_ATMLOGICAL	= 0x50,		/* ATM Logical Port */
	IFT_DS0		= 0x51,		/* Digital Signal Level 0 */
	IFT_DS0BUNDLE	= 0x52,		/* group of ds0s on the same ds1 */
	IFT_BSC		= 0x53,		/* Bisynchronous Protocol */
	IFT_ASYNC	= 0x54,		/* Asynchronous Protocol */
	IFT_CNR		= 0x55,		/* Combat Net Radio */
	IFT_ISO88025DTR	= 0x56,		/* ISO 802.5r DTR */
	IFT_EPLRS	= 0x57,		/* Ext Pos Loc Report Sys */
	IFT_ARAP	= 0x58,		/* Appletalk Remote Access Protocol */
	IFT_PROPCNLS	= 0x59,		/* Proprietary Connectionless Protocol*/
	IFT_HOSTPAD	= 0x5a,		/* CCITT-ITU X.29 PAD Protocol */
	IFT_TERMPAD	= 0x5b,		/* CCITT-ITU X.3 PAD Facility */
	IFT_FRAMERELAYMPI = 0x5c,	/* Multiproto Interconnect over FR */
	IFT_X213	= 0x5d,		/* CCITT-ITU X213 */
	IFT_ADSL	= 0x5e,		/* Asymmetric Digital Subscriber Loop */
	IFT_RADSL	= 0x5f,		/* Rate-Adapt. Digital Subscriber Loop*/
	IFT_SDSL	= 0x60,		/* Symmetric Digital Subscriber Loop */
	IFT_VDSL	= 0x61,		/* Very H-Speed Digital Subscrib. Loop*/
	IFT_ISO88025CRFPINT = 0x62,	/* ISO 802.5 CRFP */
	IFT_MYRINET	= 0x63,		/* Myricom Myrinet */
	IFT_VOICEEM	= 0x64,		/* voice recEive and transMit */
	IFT_VOICEFXO	= 0x65,		/* voice Foreign Exchange Office */
	IFT_VOICEFXS	= 0x66,		/* voice Foreign Exchange Station */
	IFT_VOICEENCAP	= 0x67,		/* voice encapsulation */
	IFT_VOICEOVERIP	= 0x68,		/* voice over IP encapsulation */
	IFT_ATMDXI	= 0x69,		/* ATM DXI */
	IFT_ATMFUNI	= 0x6a,		/* ATM FUNI */
	IFT_ATMIMA	= 0x6b,		/* ATM IMA		      */
	IFT_PPPMULTILINKBUNDLE = 0x6c,	/* PPP Multilink Bundle */
	IFT_IPOVERCDLC	= 0x6d,		/* IBM ipOverCdlc */
	IFT_IPOVERCLAW	= 0x6e,		/* IBM Common Link Access to Workstn */
	IFT_STACKTOSTACK = 0x6f,	/* IBM stackToStack */
	IFT_VIRTUALIPADDRESS = 0x70,	/* IBM VIPA */
	IFT_MPC		= 0x71,		/* IBM multi-protocol channel support */
	IFT_IPOVERATM	= 0x72,		/* IBM ipOverAtm */
	IFT_ISO88025FIBER = 0x73,	/* ISO 802.5j Fiber Token Ring */
	IFT_TDLC	= 0x74,		/* IBM twinaxial data link control */
	IFT_GIGABITETHERNET = 0x75,	/* Gigabit Ethernet */
	IFT_HDLC	= 0x76,		/* HDLC */
	IFT_LAPF	= 0x77,		/* LAP F */
	IFT_V37		= 0x78,		/* V.37 */
	IFT_X25MLP	= 0x79,		/* Multi-Link Protocol */
	IFT_X25HUNTGROUP = 0x7a,	/* X25 Hunt Group */
	IFT_TRANSPHDLC	= 0x7b,		/* Transp HDLC */
	IFT_INTERLEAVE	= 0x7c,		/* Interleave channel */
	IFT_FAST	= 0x7d,		/* Fast channel */
	IFT_IP		= 0x7e,		/* IP (for APPN HPR in IP networks) */
	IFT_DOCSCABLEMACLAYER = 0x7f,	/* CATV Mac Layer */
	IFT_DOCSCABLEDOWNSTREAM = 0x80,	/* CATV Downstream interface */
	IFT_DOCSCABLEUPSTREAM = 0x81,	/* CATV Upstream interface */
	IFT_A12MPPSWITCH = 0x82,	/* Avalon Parallel Processor */
	IFT_TUNNEL	= 0x83,		/* Encapsulation interface */
	IFT_COFFEE	= 0x84,		/* coffee pot */
	IFT_CES		= 0x85,		/* Circiut Emulation Service */
	IFT_ATMSUBINTERFACE = 0x86,	/* (x)  ATM Sub Interface */
	IFT_L2VLAN	= 0x87,		/* Layer 2 Virtual LAN using 802.1Q */
	IFT_L3IPVLAN	= 0x88,		/* Layer 3 Virtual LAN - IP Protocol */
	IFT_L3IPXVLAN	= 0x89,		/* Layer 3 Virtual LAN - IPX Prot. */
	IFT_DIGITALPOWERLINE = 0x8a,	/* IP over Power Lines */
	IFT_MEDIAMAILOVERIP = 0x8b,	/* (xxx)  Multimedia Mail over IP */
	IFT_DTM		= 0x8c,		/* Dynamic synchronous Transfer Mode */
	IFT_DCN		= 0x8d,		/* Data Communications Network */
	IFT_IPFORWARD	= 0x8e,		/* IP Forwarding Interface */
	IFT_MSDSL	= 0x8f,		/* Multi-rate Symmetric DSL */
	IFT_IEEE1394	= 0x90,		/* IEEE1394 High Performance SerialBus*/
	IFT_IFGSN	= 0x91,		/* HIPPI-6400 */
	IFT_DVBRCCMACLAYER = 0x92,	/* DVB-RCC MAC Layer */
	IFT_DVBRCCDOWNSTREAM = 0x93,	/* DVB-RCC Downstream Channel */
	IFT_DVBRCCUPSTREAM = 0x94,	/* DVB-RCC Upstream Channel */
	IFT_ATMVIRTUAL	= 0x95,		/* ATM Virtual Interface */
	IFT_MPLSTUNNEL	= 0x96,		/* MPLS Tunnel Virtual Interface */
	IFT_SRP		= 0x97,		/* Spatial Reuse Protocol */
	IFT_VOICEOVERATM = 0x98,	/* Voice over ATM */
	IFT_VOICEOVERFRAMERELAY	= 0x99,	/* Voice Over Frame Relay */
	IFT_IDSL	= 0x9a,		/* Digital Subscriber Loop over ISDN */
	IFT_COMPOSITELINK = 0x9b,	/* Avici Composite Link Interface */
	IFT_SS7SIGLINK	= 0x9c,		/* SS7 Signaling Link */
	IFT_PROPWIRELESSP2P = 0x9d,	/* Prop. P2P wireless interface */
	IFT_FRFORWARD	= 0x9e,		/* Frame forward Interface */
	IFT_RFC1483	= 0x9f,		/* Multiprotocol over ATM AAL5 */
	IFT_USB		= 0xa0,		/* USB Interface */
	IFT_IEEE8023ADLAG = 0xa1,	/* IEEE 802.3ad Link Aggregate*/
	IFT_BGPPOLICYACCOUNTING = 0xa2,	/* BGP Policy Accounting */
	IFT_FRF16MFRBUNDLE = 0xa3,	/* FRF.16 Multilik Frame Relay*/
	IFT_H323GATEKEEPER = 0xa4,	/* H323 Gatekeeper */
	IFT_H323PROXY	= 0xa5,		/* H323 Voice and Video Proxy */
	IFT_MPLS	= 0xa6,		/* MPLS */
	IFT_MFSIGLINK	= 0xa7,		/* Multi-frequency signaling link */
	IFT_HDSL2	= 0xa8,		/* High Bit-Rate DSL, 2nd gen. */
	IFT_SHDSL	= 0xa9,		/* Multirate HDSL2 */
	IFT_DS1FDL	= 0xaa,		/* Facility Data Link (4Kbps) on a DS1*/
	IFT_POS		= 0xab,		/* Packet over SONET/SDH Interface */
	IFT_DVBASILN	= 0xac,		/* DVB-ASI Input */
	IFT_DVBASIOUT	= 0xad,		/* DVB-ASI Output */
	IFT_PLC		= 0xae,		/* Power Line Communications */
	IFT_NFAS	= 0xaf,		/* Non-Facility Associated Signaling */
	IFT_TR008	= 0xb0,		/* TROO8 */
	IFT_GR303RDT	= 0xb1,		/* Remote Digital Terminal */
	IFT_GR303IDT	= 0xb2,		/* Integrated Digital Terminal */
	IFT_ISUP	= 0xb3,		/* ISUP */
	IFT_PROPDOCSWIRELESSMACLAYER = 0xb4,	/* prop/Wireless MAC Layer */
	IFT_PROPDOCSWIRELESSDOWNSTREAM = 0xb5,	/* prop/Wireless Downstream */
	IFT_PROPDOCSWIRELESSUPSTREAM = 0xb6,	/* prop/Wireless Upstream */
	IFT_HIPERLAN2	= 0xb7,		/* HIPERLAN Type 2 Radio Interface */
	IFT_PROPBWAP2MP	= 0xb8,		/* PropBroadbandWirelessAccess P2MP*/
	IFT_SONETOVERHEADCHANNEL = 0xb9, /* SONET Overhead Channel */
	IFT_DIGITALWRAPPEROVERHEADCHANNEL = 0xba, /* Digital Wrapper Overhead */
	IFT_AAL2	= 0xbb,		/* ATM adaptation layer 2 */
	IFT_RADIOMAC	= 0xbc,		/* MAC layer over radio links */
	IFT_ATMRADIO	= 0xbd,		/* ATM over radio links */
	IFT_IMT		= 0xbe,		/* Inter-Machine Trunks */
	IFT_MVL		= 0xbf,		/* Multiple Virtual Lines DSL */
	IFT_REACHDSL	= 0xc0,		/* Long Reach DSL */
	IFT_FRDLCIENDPT	= 0xc1,		/* Frame Relay DLCI End Point */
	IFT_ATMVCIENDPT	= 0xc2,		/* ATM VCI End Point */
	IFT_OPTICALCHANNEL = 0xc3,	/* Optical Channel */
	IFT_OPTICALTRANSPORT = 0xc4,	/* Optical Transport */
	IFT_INFINIBAND	= 0xc7,		/* Infiniband */
	IFT_BRIDGE	= 0xd1,		/* Transparent bridge interface */
	IFT_STF		= 0xd7,		/* 6to4 interface */

	/*
	 * Not based on IANA assignments.  Conflicting with IANA assignments.
	 * We should make them negative probably.
	 * This requires changes to struct if_data.
	 */
	IFT_GIF		= 0xf0,		/* Generic tunnel interface */
	IFT_PVC		= 0xf1,		/* Unused */
	IFT_ENC		= 0xf4,		/* Encapsulating interface */
	IFT_PFLOG	= 0xf6,		/* PF packet filter logging */
	IFT_PFSYNC	= 0xf7,		/* PF packet filter synchronization */
} ifType;

/*
 * Some (broken) software uses #ifdef IFT_TYPE to check whether
 * an operating systems supports certain interface type.  Lack of
 * ifdef leads to a piece of functionality compiled out.
 */
#ifndef BURN_BRIDGES
#define	IFT_BRIDGE	IFT_BRIDGE
#define	IFT_PPP		IFT_PPP
#define	IFT_PROPVIRTUAL	IFT_PROPVIRTUAL
#define	IFT_L2VLAN	IFT_L2VLAN
#define	IFT_L3IPVLAN	IFT_L3IPVLAN
#define	IFT_IEEE1394	IFT_IEEE1394
#define	IFT_INFINIBAND	IFT_INFINIBAND
#endif

#endif /* !_NET_IF_TYPES_H_ */
