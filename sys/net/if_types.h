/*	$OpenBSD: if_types.h,v 1.24 2022/01/02 22:36:04 jsg Exp $	*/
/*	$NetBSD: if_types.h,v 1.17 2000/10/26 06:51:31 onoe Exp $	*/

/*
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
 */

#ifndef _NET_IF_TYPES_H_
#define _NET_IF_TYPES_H_

/*
 * Interface types for benefit of parsing media address headers.
 * This list is derived from the SNMP list of ifTypes, originally
 * documented in RFC1573, now maintained as:
 *
 * https://www.iana.org/assignments/ianaiftype-mib
 */

#define	IFT_OTHER		   0x01 /* none of the following */
#define	IFT_1822		   0x02 /* old-style arpanet imp */
#define	IFT_HDH1822		   0x03 /* HDH arpanet imp */
#define	IFT_X25DDN		   0x04 /* x25 to imp */
#define	IFT_X25			   0x05 /* PDN X25 interface (RFC877) */
#define	IFT_ETHER		   0x06 /* Ethernet CSMA/CD */
#define	IFT_ISO88023		   0x07 /* CSMA/CD */
#define	IFT_ISO88024		   0x08 /* Token Bus */
#define	IFT_ISO88025		   0x09 /* Token Ring */
#define	IFT_ISO88026		   0x0a /* MAN */
#define	IFT_STARLAN		   0x0b
#define	IFT_P10			   0x0c /* Proteon 10MBit ring */
#define	IFT_P80			   0x0d /* Proteon 80MBit ring */
#define	IFT_HY			   0x0e /* Hyperchannel */
#define	IFT_FDDI		   0x0f
#define	IFT_LAPB		   0x10
#define	IFT_SDLC		   0x11
#define	IFT_T1			   0x12
#define	IFT_CEPT		   0x13 /* E1 - european T1 */
#define	IFT_ISDNBASIC		   0x14
#define	IFT_ISDNPRIMARY		   0x15
#define	IFT_PTPSERIAL		   0x16 /* Proprietary PTP serial */
#define	IFT_PPP			   0x17 /* RFC 1331 */
#define	IFT_LOOP		   0x18 /* loopback */
#define	IFT_EON			   0x19 /* ISO over IP */
#define	IFT_XETHER		   0x1a /* obsolete 3MB experimental ethernet */
#define	IFT_NSIP		   0x1b /* XNS over IP */
#define	IFT_SLIP		   0x1c /* IP over generic TTY */
#define	IFT_ULTRA		   0x1d /* Ultra Technologies */
#define	IFT_DS3			   0x1e /* Generic T3 */
#define	IFT_SIP			   0x1f /* SMDS */
#define	IFT_FRELAY		   0x20 /* Frame Relay DTE only */
#define	IFT_RS232		   0x21
#define	IFT_PARA		   0x22 /* parallel-port */
#define	IFT_ARCNET		   0x23
#define	IFT_ARCNETPLUS		   0x24
#define	IFT_ATM			   0x25 /* ATM cells */
#define	IFT_MIOX25		   0x26
#define	IFT_SONET		   0x27 /* SONET or SDH */
#define	IFT_X25PLE		   0x28
#define	IFT_ISO88022LLC		   0x29
#define	IFT_LOCALTALK		   0x2a
#define	IFT_SMDSDXI		   0x2b
#define	IFT_FRELAYDCE		   0x2c /* Frame Relay DCE */
#define	IFT_V35			   0x2d
#define	IFT_HSSI		   0x2e
#define	IFT_HIPPI		   0x2f
#define	IFT_MODEM		   0x30 /* Generic Modem */
#define	IFT_AAL5		   0x31 /* AAL5 over ATM */
#define	IFT_SONETPATH		   0x32
#define	IFT_SONETVT		   0x33
#define	IFT_SMDSICIP		   0x34 /* SMDS InterCarrier Interface */
#define	IFT_PROPVIRTUAL		   0x35 /* Proprietary Virtual/internal */
#define	IFT_PROPMUX		   0x36 /* Proprietary Multiplexing */
#define	IFT_IEEE80212		   0x37 /* 100BaseVG */
#define	IFT_FIBRECHANNEL	   0x38 /* Fibre Channel */
#define	IFT_HIPPIINTERFACE	   0x39 /* HIPPI interfaces	 */
#define	IFT_FRAMERELAYINTERCONNECT 0x3a /* Obsolete, use either 0x20 or 0x2c */
#define	IFT_AFLANE8023		   0x3b /* ATM Emulated LAN for 802.3 */
#define	IFT_AFLANE8025		   0x3c /* ATM Emulated LAN for 802.5 */
#define	IFT_CCTEMUL		   0x3d /* ATM Emulated circuit		  */
#define	IFT_FASTETHER		   0x3e /* Fast Ethernet (100BaseT) */
#define	IFT_ISDN		   0x3f /* ISDN and X.25	    */
#define	IFT_V11			   0x40 /* CCITT V.11/X.21		*/
#define	IFT_V36			   0x41 /* CCITT V.36			*/
#define	IFT_G703AT64K		   0x42 /* CCITT G703 at 64Kbps */
#define	IFT_G703AT2MB		   0x43 /* Obsolete see DS1-MIB */
#define	IFT_QLLC		   0x44 /* SNA QLLC			*/
#define	IFT_FASTETHERFX		   0x45 /* Fast Ethernet (100BaseFX)	*/
#define	IFT_CHANNEL		   0x46 /* channel			*/
#define	IFT_IEEE80211		   0x47 /* radio spread spectrum	*/
#define	IFT_IBM370PARCHAN	   0x48 /* IBM System 360/370 OEMI Channel */
#define	IFT_ESCON		   0x49 /* IBM Enterprise Systems Connection */
#define	IFT_DLSW		   0x4a /* Data Link Switching */
#define	IFT_ISDNS		   0x4b /* ISDN S/T interface */
#define	IFT_ISDNU		   0x4c /* ISDN U interface */
#define	IFT_LAPD		   0x4d /* Link Access Protocol D */
#define	IFT_IPSWITCH		   0x4e /* IP Switching Objects */
#define	IFT_RSRB		   0x4f /* Remote Source Route Bridging */
#define	IFT_ATMLOGICAL		   0x50 /* ATM Logical Port */
#define	IFT_DS0			   0x51 /* Digital Signal Level 0 */
#define	IFT_DS0BUNDLE		   0x52 /* group of ds0s on the same ds1 */
#define	IFT_BSC			   0x53 /* Bisynchronous Protocol */
#define	IFT_ASYNC		   0x54 /* Asynchronous Protocol */
#define	IFT_CNR			   0x55 /* Combat Net Radio */
#define	IFT_ISO88025DTR		   0x56 /* ISO 802.5r DTR */
#define	IFT_EPLRS		   0x57 /* Ext Pos Loc Report Sys */
#define	IFT_ARAP		   0x58 /* Appletalk Remote Access Protocol */
#define	IFT_PROPCNLS		   0x59 /* Proprietary Connectionless Protocol*/
#define	IFT_HOSTPAD		   0x5a /* CCITT-ITU X.29 PAD Protocol */
#define	IFT_TERMPAD		   0x5b /* CCITT-ITU X.3 PAD Facility */
#define	IFT_FRAMERELAYMPI	   0x5c /* Multiproto Interconnect over FR */
#define	IFT_X213		   0x5d /* CCITT-ITU X213 */
#define	IFT_ADSL		   0x5e /* Asymmetric Digital Subscriber Loop */
#define	IFT_RADSL		   0x5f /* Rate-Adapt. Digital Subscriber Loop*/
#define	IFT_SDSL		   0x60 /* Symmetric Digital Subscriber Loop */
#define	IFT_VDSL		   0x61 /* Very H-Speed Digital Subscrib. Loop*/
#define	IFT_ISO88025CRFPINT	   0x62 /* ISO 802.5 CRFP */
#define	IFT_MYRINET		   0x63 /* Myricom Myrinet */
#define	IFT_VOICEEM		   0x64 /* voice recEive and transMit */
#define	IFT_VOICEFXO		   0x65 /* voice Foreign Exchange Office */
#define	IFT_VOICEFXS		   0x66 /* voice Foreign Exchange Station */
#define	IFT_VOICEENCAP		   0x67 /* voice encapsulation */
#define	IFT_VOICEOVERIP		   0x68 /* voice over IP encapsulation */
#define	IFT_ATMDXI		   0x69 /* ATM DXI */
#define	IFT_ATMFUNI		   0x6a /* ATM FUNI */
#define	IFT_ATMIMA		   0x6b /* ATM IMA		      */
#define	IFT_PPPMULTILINKBUNDLE	   0x6c /* PPP Multilink Bundle */
#define	IFT_IPOVERCDLC		   0x6d /* IBM ipOverCdlc */
#define	IFT_IPOVERCLAW		   0x6e /* IBM Common Link Access to Workstn */
#define	IFT_STACKTOSTACK	   0x6f /* IBM stackToStack */
#define	IFT_VIRTUALIPADDRESS	   0x70 /* IBM VIPA */
#define	IFT_MPC			   0x71 /* IBM multi-protocol channel support */
#define	IFT_IPOVERATM		   0x72 /* IBM ipOverAtm */
#define	IFT_ISO88025FIBER	   0x73 /* ISO 802.5j Fiber Token Ring */
#define	IFT_TDLC		   0x74 /* IBM twinaxial data link control */
#define	IFT_GIGABITETHERNET	   0x75 /* Gigabit Ethernet */
#define	IFT_HDLC		   0x76 /* HDLC */
#define	IFT_LAPF		   0x77 /* LAP F */
#define	IFT_V37			   0x78 /* V.37 */
#define	IFT_X25MLP		   0x79 /* Multi-Link Protocol */
#define	IFT_X25HUNTGROUP	   0x7a /* X25 Hunt Group */
#define	IFT_TRANSPHDLC		   0x7b /* Transp HDLC */
#define	IFT_INTERLEAVE		   0x7c /* Interleave channel */
#define	IFT_FAST		   0x7d /* Fast channel */
#define	IFT_IP			   0x7e /* IP (for APPN HPR in IP networks) */
#define	IFT_DOCSCABLEMACLAYER	   0x7f /* CATV Mac Layer */
#define	IFT_DOCSCABLEDOWNSTREAM	   0x80 /* CATV Downstream interface */
#define	IFT_DOCSCABLEUPSTREAM	   0x81 /* CATV Upstream interface */
#define	IFT_A12MPPSWITCH	   0x82	/* Avalon Parallel Processor */
#define	IFT_TUNNEL		   0x83	/* Encapsulation interface */
#define	IFT_COFFEE		   0x84	/* coffee pot */
#define	IFT_CES			   0x85	/* Circiut Emulation Service */
#define	IFT_ATMSUBINTERFACE	   0x86	/* (x)  ATM Sub Interface */
#define	IFT_L2VLAN		   0x87	/* Layer 2 Virtual LAN using 802.1Q */
#define	IFT_L3IPVLAN		   0x88	/* Layer 3 Virtual LAN - IP Protocol */
#define	IFT_L3IPXVLAN		   0x89	/* Layer 3 Virtual LAN - IPX Prot. */
#define	IFT_DIGITALPOWERLINE	   0x8a	/* IP over Power Lines */
#define	IFT_MEDIAMAILOVERIP	   0x8b	/* (xxx)  Multimedia Mail over IP */
#define	IFT_DTM			   0x8c	/* Dynamic synchronous Transfer Mode */
#define	IFT_DCN			   0x8d	/* Data Communications Network */
#define	IFT_IPFORWARD		   0x8e	/* IP Forwarding Interface */
#define	IFT_MSDSL		   0x8f	/* Multi-rate Symmetric DSL */
#define	IFT_IEEE1394		   0x90	/* IEEE1394 High Performance SerialBus*/
#define	IFT_IFGSN		   0x91	/* HIPPI-6400 */
#define	IFT_DVBRCCMACLAYER	   0x92	/* DVB-RCC MAC Layer */
#define	IFT_DVBRCCDOWNSTREAM	   0x93	/* DVB-RCC Downstream Channel */
#define	IFT_DVBRCCUPSTREAM	   0x94	/* DVB-RCC Upstream Channel */
#define	IFT_ATMVIRTUAL		   0x95	/* ATM Virtual Interface */
#define	IFT_MPLSTUNNEL		   0x96	/* MPLS Tunnel Virtual Interface */
#define	IFT_SRP			   0x97	/* Spatial Reuse Protocol */
#define	IFT_VOICEOVERATM	   0x98	/* Voice over ATM */
#define	IFT_VOICEOVERFRAMERELAY	   0x99	/* Voice Over Frame Relay */
#define	IFT_IDSL		   0x9a	/* Digital Subscriber Loop over ISDN */
#define	IFT_COMPOSITELINK	   0x9b	/* Avici Composite Link Interface */
#define	IFT_SS7SIGLINK		   0x9c	/* SS7 Signaling Link */
#define	IFT_PROPWIRELESSP2P	   0x9d	/* Prop. P2P wireless interface */
#define	IFT_FRFORWARD		   0x9e	/* Frame forward Interface */
#define	IFT_RFC1483		   0x9f	/* Multiprotocol over ATM AAL5 */
#define	IFT_USB			   0xa0	/* USB Interface */
#define	IFT_IEEE8023ADLAG	   0xa1	/* IEEE 802.3ad Link Aggregate*/
#define	IFT_BGPPOLICYACCOUNTING	   0xa2	/* BGP Policy Accounting */
#define	IFT_FRF16MFRBUNDLE	   0xa3	/* FRF.16 Multilink Frame Relay*/
#define	IFT_H323GATEKEEPER	   0xa4	/* H323 Gatekeeper */
#define	IFT_H323PROXY		   0xa5	/* H323 Voice and Video Proxy */
#define	IFT_MPLS		   0xa6	/* MPLS */
#define	IFT_MFSIGLINK		   0xa7	/* Multi-frequency signaling link */
#define	IFT_HDSL2		   0xa8	/* High Bit-Rate DSL, 2nd gen. */
#define	IFT_SHDSL		   0xa9	/* Multirate HDSL2 */
#define	IFT_DS1FDL		   0xaa	/* Facility Data Link (4Kbps) on a DS1*/
#define	IFT_POS			   0xab	/* Packet over SONET/SDH Interface */
#define	IFT_DVBASILN		   0xac	/* DVB-ASI Input */
#define	IFT_DVBASIOUT		   0xad	/* DVB-ASI Output */
#define	IFT_PLC			   0xae	/* Power Line Communications */
#define	IFT_NFAS		   0xaf	/* Non-Facility Associated Signaling */
#define	IFT_TR008		   0xb0	/* TROO8 */
#define	IFT_GR303RDT		   0xb1	/* Remote Digital Terminal */
#define	IFT_GR303IDT		   0xb2	/* Integrated Digital Terminal */
#define	IFT_ISUP		   0xb3	/* ISUP */
#define	IFT_PROPDOCSWIRELESSMACLAYER	   0xb4	/* prop/Wireless MAC Layer */
#define	IFT_PROPDOCSWIRELESSDOWNSTREAM	   0xb5	/* prop/Wireless Downstream */
#define	IFT_PROPDOCSWIRELESSUPSTREAM	   0xb6	/* prop/Wireless Upstream */
#define	IFT_HIPERLAN2		   0xb7	/* HIPERLAN Type 2 Radio Interface */
#define	IFT_PROPBWAP2MP		   0xb8	/* PropBroadbandWirelessAccess P2MP*/
#define	IFT_SONETOVERHEADCHANNEL   0xb9	/* SONET Overhead Channel */
#define	IFT_DIGITALWRAPPEROVERHEADCHANNEL  0xba	/* Digital Wrapper Overhead */
#define	IFT_AAL2		   0xbb	/* ATM adaptation layer 2 */
#define	IFT_RADIOMAC		   0xbc	/* MAC layer over radio links */
#define	IFT_ATMRADIO		   0xbd	/* ATM over radio links */
#define	IFT_IMT			   0xbe /* Inter-Machine Trunks */
#define	IFT_MVL			   0xbf /* Multiple Virtual Lines DSL */
#define	IFT_REACHDSL		   0xc0 /* Long Reach DSL */
#define	IFT_FRDLCIENDPT		   0xc1 /* Frame Relay DLCI End Point */
#define	IFT_ATMVCIENDPT		   0xc2 /* ATM VCI End Point */
#define	IFT_OPTICALCHANNEL	   0xc3 /* Optical Channel */
#define	IFT_OPTICALTRANSPORT	   0xc4 /* Optical Transport */
#define	IFT_PROPATM		   0xc5 /* Proprietary ATM */
#define	IFT_VOICEOVERCABLE	   0xc6 /* Voice Over Cable Interface */
#define	IFT_INFINIBAND		   0xc7 /* Infiniband */
#define	IFT_TELINK		   0xc8 /* TE Link */
#define	IFT_Q2931		   0xc9 /* Q.2931 */
#define	IFT_VIRTUALTG		   0xca /* Virtual Trunk Group */
#define	IFT_SIPTG		   0xcb /* SIP Trunk Group */
#define	IFT_SIPSIG		   0xcc /* SIP Signaling */
#define	IFT_DOCSCABLEUPSTREAMCHANNEL 0xcd /* CATV Upstream Channel */
#define	IFT_ECONET		   0xce /* Acorn Econet */
#define	IFT_PON155		   0xcf /* FSAN 155Mb Symmetrical PON interface */
#define	IFT_PON622		   0xd0 /* FSAN 622Mb Symmetrical PON interface */
#define	IFT_BRIDGE		   0xd1 /* Transparent bridge interface */
#define	IFT_LINEGROUP		   0xd2 /* Interface common to multiple lines */
#define	IFT_VOICEEMFGD		   0xd3 /* voice E&M Feature Group D */
#define	IFT_VOICEFGDEANA	   0xd4 /* voice FGD Exchange Access North American */
#define	IFT_VOICEDID		   0xd5 /* voice Direct Inward Dialing */

/* private usage... how should we define these? */
#define	IFT_GIF		0xf0
#define	IFT_DUMMY	0xf1
#define	IFT_PVC		0xf2
#define	IFT_FAITH	0xf3
#define	IFT_ENC		0xf4		/* Encapsulation */
#define	IFT_PFLOG	0xf5		/* Packet filter logging */
#define	IFT_PFSYNC	0xf6		/* Packet filter state syncing */
#define	IFT_CARP	0xf7		/* Common Address Redundancy Protocol */
#define IFT_BLUETOOTH	0xf8		/* Bluetooth */
#define IFT_PFLOW	0xf9		/* pflow */
#define IFT_MBIM	0xfa		/* Mobile Broadband Interface Model */
#define IFT_WIREGUARD	0xfb		/* WireGuard tunnel */

#endif /* _NET_IF_TYPES_H_ */
