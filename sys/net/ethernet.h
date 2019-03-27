/*
 * Fundamental constants relating to ethernet.
 *
 * $FreeBSD$
 *
 */

#ifndef _NET_ETHERNET_H_
#define _NET_ETHERNET_H_

/*
 * Some basic Ethernet constants.
 */
#define	ETHER_ADDR_LEN		6	/* length of an Ethernet address */
#define	ETHER_TYPE_LEN		2	/* length of the Ethernet type field */
#define	ETHER_CRC_LEN		4	/* length of the Ethernet CRC */
#define	ETHER_HDR_LEN		(ETHER_ADDR_LEN*2+ETHER_TYPE_LEN)
#define	ETHER_MIN_LEN		64	/* minimum frame len, including CRC */
#define	ETHER_MAX_LEN		1518	/* maximum frame len, including CRC */
#define	ETHER_MAX_LEN_JUMBO	9018	/* max jumbo frame len, including CRC */

#define	ETHER_VLAN_ENCAP_LEN	4	/* len of 802.1Q VLAN encapsulation */
/*
 * Mbuf adjust factor to force 32-bit alignment of IP header.
 * Drivers should do m_adj(m, ETHER_ALIGN) when setting up a
 * receive so the upper layers get the IP header properly aligned
 * past the 14-byte Ethernet header.
 */
#define	ETHER_ALIGN		2	/* driver adjust for IP hdr alignment */

/*
 * Compute the maximum frame size based on ethertype (i.e. possible
 * encapsulation) and whether or not an FCS is present.
 */
#define	ETHER_MAX_FRAME(ifp, etype, hasfcs)				\
	((ifp)->if_mtu + ETHER_HDR_LEN +				\
	 ((hasfcs) ? ETHER_CRC_LEN : 0) +				\
	 (((etype) == ETHERTYPE_VLAN) ? ETHER_VLAN_ENCAP_LEN : 0))

/*
 * Ethernet-specific mbuf flags.
 */
#define	M_HASFCS	M_PROTO5	/* FCS included at end of frame */

/*
 * Ethernet CRC32 polynomials (big- and little-endian verions).
 */
#define	ETHER_CRC_POLY_LE	0xedb88320
#define	ETHER_CRC_POLY_BE	0x04c11db6

/*
 * A macro to validate a length with
 */
#define	ETHER_IS_VALID_LEN(foo)	\
	((foo) >= ETHER_MIN_LEN && (foo) <= ETHER_MAX_LEN)

/*
 * Structure of a 10Mb/s Ethernet header.
 */
struct ether_header {
	u_char	ether_dhost[ETHER_ADDR_LEN];
	u_char	ether_shost[ETHER_ADDR_LEN];
	u_short	ether_type;
} __packed;

/*
 * Structure of a 48-bit Ethernet address.
 */
struct ether_addr {
	u_char octet[ETHER_ADDR_LEN];
} __packed;

#define	ETHER_IS_MULTICAST(addr) (*(addr) & 0x01) /* is address mcast/bcast? */
#define	ETHER_IS_BROADCAST(addr) \
	(((addr)[0] & (addr)[1] & (addr)[2] & \
	  (addr)[3] & (addr)[4] & (addr)[5]) == 0xff)

/*
 * 802.1q Virtual LAN header.
 */
struct ether_vlan_header {
	uint8_t evl_dhost[ETHER_ADDR_LEN];
	uint8_t evl_shost[ETHER_ADDR_LEN];
	uint16_t evl_encap_proto;
	uint16_t evl_tag;
	uint16_t evl_proto;
} __packed;

#define	EVL_VLID_MASK		0x0FFF
#define	EVL_PRI_MASK		0xE000
#define	EVL_VLANOFTAG(tag)	((tag) & EVL_VLID_MASK)
#define	EVL_PRIOFTAG(tag)	(((tag) >> 13) & 7)
#define	EVL_CFIOFTAG(tag)	(((tag) >> 12) & 1)
#define	EVL_MAKETAG(vlid, pri, cfi)					\
	((((((pri) & 7) << 1) | ((cfi) & 1)) << 12) | ((vlid) & EVL_VLID_MASK))

/*
 *  NOTE: 0x0000-0x05DC (0..1500) are generally IEEE 802.3 length fields.
 *  However, there are some conflicts.
 */

#define	ETHERTYPE_8023		0x0004	/* IEEE 802.3 packet */
		   /* 0x0101 .. 0x1FF	   Experimental */
#define	ETHERTYPE_PUP		0x0200	/* Xerox PUP protocol - see 0A00 */
#define	ETHERTYPE_PUPAT		0x0200	/* PUP Address Translation - see 0A01 */
#define	ETHERTYPE_SPRITE	0x0500	/* ??? */
			     /* 0x0400	   Nixdorf */
#define	ETHERTYPE_NS		0x0600	/* XNS */
#define	ETHERTYPE_NSAT		0x0601	/* XNS Address Translation (3Mb only) */
#define	ETHERTYPE_DLOG1 	0x0660	/* DLOG (?) */
#define	ETHERTYPE_DLOG2 	0x0661	/* DLOG (?) */
#define	ETHERTYPE_IP		0x0800	/* IP protocol */
#define	ETHERTYPE_X75		0x0801	/* X.75 Internet */
#define	ETHERTYPE_NBS		0x0802	/* NBS Internet */
#define	ETHERTYPE_ECMA		0x0803	/* ECMA Internet */
#define	ETHERTYPE_CHAOS 	0x0804	/* CHAOSnet */
#define	ETHERTYPE_X25		0x0805	/* X.25 Level 3 */
#define	ETHERTYPE_ARP		0x0806	/* Address resolution protocol */
#define	ETHERTYPE_NSCOMPAT	0x0807	/* XNS Compatibility */
#define	ETHERTYPE_FRARP 	0x0808	/* Frame Relay ARP (RFC1701) */
			     /* 0x081C	   Symbolics Private */
		    /* 0x0888 - 0x088A	   Xyplex */
#define	ETHERTYPE_UBDEBUG	0x0900	/* Ungermann-Bass network debugger */
#define	ETHERTYPE_IEEEPUP	0x0A00	/* Xerox IEEE802.3 PUP */
#define	ETHERTYPE_IEEEPUPAT	0x0A01	/* Xerox IEEE802.3 PUP Address Translation */
#define	ETHERTYPE_VINES 	0x0BAD	/* Banyan VINES */
#define	ETHERTYPE_VINESLOOP	0x0BAE	/* Banyan VINES Loopback */
#define	ETHERTYPE_VINESECHO	0x0BAF	/* Banyan VINES Echo */

/*		       0x1000 - 0x100F	   Berkeley Trailer */
/*
 * The ETHERTYPE_NTRAILER packet types starting at ETHERTYPE_TRAIL have
 * (type-ETHERTYPE_TRAIL)*512 bytes of data followed
 * by an ETHER type (as given above) and then the (variable-length) header.
 */
#define	ETHERTYPE_TRAIL		0x1000	/* Trailer packet */
#define	ETHERTYPE_NTRAILER	16

#define	ETHERTYPE_DCA		0x1234	/* DCA - Multicast */
#define	ETHERTYPE_VALID 	0x1600	/* VALID system protocol */
#define	ETHERTYPE_DOGFIGHT	0x1989	/* Artificial Horizons ("Aviator" dogfight simulator [on Sun]) */
#define	ETHERTYPE_RCL		0x1995	/* Datapoint Corporation (RCL lan protocol) */

					/* The following 3C0x types
					   are unregistered: */
#define	ETHERTYPE_NBPVCD	0x3C00	/* 3Com NBP virtual circuit datagram (like XNS SPP) not registered */
#define	ETHERTYPE_NBPSCD	0x3C01	/* 3Com NBP System control datagram not registered */
#define	ETHERTYPE_NBPCREQ	0x3C02	/* 3Com NBP Connect request (virtual cct) not registered */
#define	ETHERTYPE_NBPCRSP	0x3C03	/* 3Com NBP Connect response not registered */
#define	ETHERTYPE_NBPCC		0x3C04	/* 3Com NBP Connect complete not registered */
#define	ETHERTYPE_NBPCLREQ	0x3C05	/* 3Com NBP Close request (virtual cct) not registered */
#define	ETHERTYPE_NBPCLRSP	0x3C06	/* 3Com NBP Close response not registered */
#define	ETHERTYPE_NBPDG		0x3C07	/* 3Com NBP Datagram (like XNS IDP) not registered */
#define	ETHERTYPE_NBPDGB	0x3C08	/* 3Com NBP Datagram broadcast not registered */
#define	ETHERTYPE_NBPCLAIM	0x3C09	/* 3Com NBP Claim NetBIOS name not registered */
#define	ETHERTYPE_NBPDLTE	0x3C0A	/* 3Com NBP Delete NetBIOS name not registered */
#define	ETHERTYPE_NBPRAS	0x3C0B	/* 3Com NBP Remote adaptor status request not registered */
#define	ETHERTYPE_NBPRAR	0x3C0C	/* 3Com NBP Remote adaptor response not registered */
#define	ETHERTYPE_NBPRST	0x3C0D	/* 3Com NBP Reset not registered */

#define	ETHERTYPE_PCS		0x4242	/* PCS Basic Block Protocol */
#define	ETHERTYPE_IMLBLDIAG	0x424C	/* Information Modes Little Big LAN diagnostic */
#define	ETHERTYPE_DIDDLE	0x4321	/* THD - Diddle */
#define	ETHERTYPE_IMLBL		0x4C42	/* Information Modes Little Big LAN */
#define	ETHERTYPE_SIMNET	0x5208	/* BBN Simnet Private */
#define	ETHERTYPE_DECEXPER	0x6000	/* DEC Unassigned, experimental */
#define	ETHERTYPE_MOPDL		0x6001	/* DEC MOP dump/load */
#define	ETHERTYPE_MOPRC		0x6002	/* DEC MOP remote console */
#define	ETHERTYPE_DECnet	0x6003	/* DEC DECNET Phase IV route */
#define	ETHERTYPE_DN		ETHERTYPE_DECnet	/* libpcap, tcpdump */
#define	ETHERTYPE_LAT		0x6004	/* DEC LAT */
#define	ETHERTYPE_DECDIAG	0x6005	/* DEC diagnostic protocol (at interface initialization?) */
#define	ETHERTYPE_DECCUST	0x6006	/* DEC customer protocol */
#define	ETHERTYPE_SCA		0x6007	/* DEC LAVC, SCA */
#define	ETHERTYPE_AMBER		0x6008	/* DEC AMBER */
#define	ETHERTYPE_DECMUMPS	0x6009	/* DEC MUMPS */
		    /* 0x6010 - 0x6014	   3Com Corporation */
#define	ETHERTYPE_TRANSETHER	0x6558	/* Trans Ether Bridging (RFC1701)*/
#define	ETHERTYPE_RAWFR		0x6559	/* Raw Frame Relay (RFC1701) */
#define	ETHERTYPE_UBDL		0x7000	/* Ungermann-Bass download */
#define	ETHERTYPE_UBNIU		0x7001	/* Ungermann-Bass NIUs */
#define	ETHERTYPE_UBDIAGLOOP	0x7002	/* Ungermann-Bass diagnostic/loopback */
#define	ETHERTYPE_UBNMC		0x7003	/* Ungermann-Bass ??? (NMC to/from UB Bridge) */
#define	ETHERTYPE_UBBST		0x7005	/* Ungermann-Bass Bridge Spanning Tree */
#define	ETHERTYPE_OS9		0x7007	/* OS/9 Microware */
#define	ETHERTYPE_OS9NET	0x7009	/* OS/9 Net? */
		    /* 0x7020 - 0x7029	   LRT (England) (now Sintrom) */
#define	ETHERTYPE_RACAL		0x7030	/* Racal-Interlan */
#define	ETHERTYPE_PRIMENTS	0x7031	/* Prime NTS (Network Terminal Service) */
#define	ETHERTYPE_CABLETRON	0x7034	/* Cabletron */
#define	ETHERTYPE_CRONUSVLN	0x8003	/* Cronus VLN */
#define	ETHERTYPE_CRONUS	0x8004	/* Cronus Direct */
#define	ETHERTYPE_HP		0x8005	/* HP Probe */
#define	ETHERTYPE_NESTAR	0x8006	/* Nestar */
#define	ETHERTYPE_ATTSTANFORD	0x8008	/* AT&T/Stanford (local use) */
#define	ETHERTYPE_EXCELAN	0x8010	/* Excelan */
#define	ETHERTYPE_SG_DIAG	0x8013	/* SGI diagnostic type */
#define	ETHERTYPE_SG_NETGAMES	0x8014	/* SGI network games */
#define	ETHERTYPE_SG_RESV	0x8015	/* SGI reserved type */
#define	ETHERTYPE_SG_BOUNCE	0x8016	/* SGI bounce server */
#define	ETHERTYPE_APOLLODOMAIN	0x8019	/* Apollo DOMAIN */
#define	ETHERTYPE_TYMSHARE	0x802E	/* Tymeshare */
#define	ETHERTYPE_TIGAN		0x802F	/* Tigan, Inc. */
#define	ETHERTYPE_REVARP	0x8035	/* Reverse addr resolution protocol */
#define	ETHERTYPE_AEONIC	0x8036	/* Aeonic Systems */
#define	ETHERTYPE_IPXNEW	0x8037	/* IPX (Novell Netware?) */
#define	ETHERTYPE_LANBRIDGE	0x8038	/* DEC LANBridge */
#define	ETHERTYPE_DSMD	0x8039	/* DEC DSM/DDP */
#define	ETHERTYPE_ARGONAUT	0x803A	/* DEC Argonaut Console */
#define	ETHERTYPE_VAXELN	0x803B	/* DEC VAXELN */
#define	ETHERTYPE_DECDNS	0x803C	/* DEC DNS Naming Service */
#define	ETHERTYPE_ENCRYPT	0x803D	/* DEC Ethernet Encryption */
#define	ETHERTYPE_DECDTS	0x803E	/* DEC Distributed Time Service */
#define	ETHERTYPE_DECLTM	0x803F	/* DEC LAN Traffic Monitor */
#define	ETHERTYPE_DECNETBIOS	0x8040	/* DEC PATHWORKS DECnet NETBIOS Emulation */
#define	ETHERTYPE_DECLAST	0x8041	/* DEC Local Area System Transport */
			     /* 0x8042	   DEC Unassigned */
#define	ETHERTYPE_PLANNING	0x8044	/* Planning Research Corp. */
		    /* 0x8046 - 0x8047	   AT&T */
#define	ETHERTYPE_DECAM		0x8048	/* DEC Availability Manager for Distributed Systems DECamds (but someone at DEC says not) */
#define	ETHERTYPE_EXPERDATA	0x8049	/* ExperData */
#define	ETHERTYPE_VEXP		0x805B	/* Stanford V Kernel exp. */
#define	ETHERTYPE_VPROD		0x805C	/* Stanford V Kernel prod. */
#define	ETHERTYPE_ES		0x805D	/* Evans & Sutherland */
#define	ETHERTYPE_LITTLE	0x8060	/* Little Machines */
#define	ETHERTYPE_COUNTERPOINT	0x8062	/* Counterpoint Computers */
		    /* 0x8065 - 0x8066	   Univ. of Mass @ Amherst */
#define	ETHERTYPE_VEECO		0x8067	/* Veeco Integrated Auto. */
#define	ETHERTYPE_GENDYN	0x8068	/* General Dynamics */
#define	ETHERTYPE_ATT		0x8069	/* AT&T */
#define	ETHERTYPE_AUTOPHON	0x806A	/* Autophon */
#define	ETHERTYPE_COMDESIGN	0x806C	/* ComDesign */
#define	ETHERTYPE_COMPUGRAPHIC	0x806D	/* Compugraphic Corporation */
		    /* 0x806E - 0x8077	   Landmark Graphics Corp. */
#define	ETHERTYPE_MATRA		0x807A	/* Matra */
#define	ETHERTYPE_DDE		0x807B	/* Dansk Data Elektronik */
#define	ETHERTYPE_MERIT		0x807C	/* Merit Internodal (or Univ of Michigan?) */
		    /* 0x807D - 0x807F	   Vitalink Communications */
#define	ETHERTYPE_VLTLMAN	0x8080	/* Vitalink TransLAN III Management */
		    /* 0x8081 - 0x8083	   Counterpoint Computers */
		    /* 0x8088 - 0x808A	   Xyplex */
#define	ETHERTYPE_ATALK		0x809B	/* AppleTalk */
#define	ETHERTYPE_AT		ETHERTYPE_ATALK		/* old NetBSD */
#define	ETHERTYPE_APPLETALK	ETHERTYPE_ATALK		/* HP-UX */
		    /* 0x809C - 0x809E	   Datability */
#define	ETHERTYPE_SPIDER	0x809F	/* Spider Systems Ltd. */
			     /* 0x80A3	   Nixdorf */
		    /* 0x80A4 - 0x80B3	   Siemens Gammasonics Inc. */
		    /* 0x80C0 - 0x80C3	   DCA (Digital Comm. Assoc.) Data Exchange Cluster */
		    /* 0x80C4 - 0x80C5	   Banyan Systems */
#define	ETHERTYPE_PACER		0x80C6	/* Pacer Software */
#define	ETHERTYPE_APPLITEK	0x80C7	/* Applitek Corporation */
		    /* 0x80C8 - 0x80CC	   Intergraph Corporation */
		    /* 0x80CD - 0x80CE	   Harris Corporation */
		    /* 0x80CF - 0x80D2	   Taylor Instrument */
		    /* 0x80D3 - 0x80D4	   Rosemount Corporation */
#define	ETHERTYPE_SNA		0x80D5	/* IBM SNA Services over Ethernet */
#define	ETHERTYPE_VARIAN	0x80DD	/* Varian Associates */
		    /* 0x80DE - 0x80DF	   TRFS (Integrated Solutions Transparent Remote File System) */
		    /* 0x80E0 - 0x80E3	   Allen-Bradley */
		    /* 0x80E4 - 0x80F0	   Datability */
#define	ETHERTYPE_RETIX		0x80F2	/* Retix */
#define	ETHERTYPE_AARP		0x80F3	/* AppleTalk AARP */
		    /* 0x80F4 - 0x80F5	   Kinetics */
#define	ETHERTYPE_APOLLO	0x80F7	/* Apollo Computer */
#define ETHERTYPE_VLAN		0x8100	/* IEEE 802.1Q VLAN tagging (XXX conflicts) */
		    /* 0x80FF - 0x8101	   Wellfleet Communications (XXX conflicts) */
#define	ETHERTYPE_BOFL		0x8102	/* Wellfleet; BOFL (Breath OF Life) pkts [every 5-10 secs.] */
#define	ETHERTYPE_WELLFLEET	0x8103	/* Wellfleet Communications */
		    /* 0x8107 - 0x8109	   Symbolics Private */
#define	ETHERTYPE_TALARIS	0x812B	/* Talaris */
#define	ETHERTYPE_WATERLOO	0x8130	/* Waterloo Microsystems Inc. (XXX which?) */
#define	ETHERTYPE_HAYES		0x8130	/* Hayes Microcomputers (XXX which?) */
#define	ETHERTYPE_VGLAB		0x8131	/* VG Laboratory Systems */
		    /* 0x8132 - 0x8137	   Bridge Communications */
#define	ETHERTYPE_IPX		0x8137	/* Novell (old) NetWare IPX (ECONFIG E option) */
#define	ETHERTYPE_NOVELL	0x8138	/* Novell, Inc. */
		    /* 0x8139 - 0x813D	   KTI */
#define	ETHERTYPE_MUMPS		0x813F	/* M/MUMPS data sharing */
#define	ETHERTYPE_AMOEBA	0x8145	/* Vrije Universiteit (NL) Amoeba 4 RPC (obsolete) */
#define	ETHERTYPE_FLIP		0x8146	/* Vrije Universiteit (NL) FLIP (Fast Local Internet Protocol) */
#define	ETHERTYPE_VURESERVED	0x8147	/* Vrije Universiteit (NL) [reserved] */
#define	ETHERTYPE_LOGICRAFT	0x8148	/* Logicraft */
#define	ETHERTYPE_NCD		0x8149	/* Network Computing Devices */
#define	ETHERTYPE_ALPHA		0x814A	/* Alpha Micro */
#define	ETHERTYPE_SNMP		0x814C	/* SNMP over Ethernet (see RFC1089) */
		    /* 0x814D - 0x814E	   BIIN */
#define	ETHERTYPE_TEC	0x814F	/* Technically Elite Concepts */
#define	ETHERTYPE_RATIONAL	0x8150	/* Rational Corp */
		    /* 0x8151 - 0x8153	   Qualcomm */
		    /* 0x815C - 0x815E	   Computer Protocol Pty Ltd */
		    /* 0x8164 - 0x8166	   Charles River Data Systems */
#define	ETHERTYPE_XTP		0x817D	/* Protocol Engines XTP */
#define	ETHERTYPE_SGITW		0x817E	/* SGI/Time Warner prop. */
#define	ETHERTYPE_HIPPI_FP	0x8180	/* HIPPI-FP encapsulation */
#define	ETHERTYPE_STP		0x8181	/* Scheduled Transfer STP, HIPPI-ST */
		    /* 0x8182 - 0x8183	   Reserved for HIPPI-6400 */
		    /* 0x8184 - 0x818C	   SGI prop. */
#define	ETHERTYPE_MOTOROLA	0x818D	/* Motorola */
#define	ETHERTYPE_NETBEUI	0x8191	/* PowerLAN NetBIOS/NetBEUI (PC) */
		    /* 0x819A - 0x81A3	   RAD Network Devices */
		    /* 0x81B7 - 0x81B9	   Xyplex */
		    /* 0x81CC - 0x81D5	   Apricot Computers */
		    /* 0x81D6 - 0x81DD	   Artisoft Lantastic */
		    /* 0x81E6 - 0x81EF	   Polygon */
		    /* 0x81F0 - 0x81F2	   Comsat Labs */
		    /* 0x81F3 - 0x81F5	   SAIC */
		    /* 0x81F6 - 0x81F8	   VG Analytical */
		    /* 0x8203 - 0x8205	   QNX Software Systems Ltd. */
		    /* 0x8221 - 0x8222	   Ascom Banking Systems */
		    /* 0x823E - 0x8240	   Advanced Encryption Systems */
		    /* 0x8263 - 0x826A	   Charles River Data Systems */
		    /* 0x827F - 0x8282	   Athena Programming */
		    /* 0x829A - 0x829B	   Inst Ind Info Tech */
		    /* 0x829C - 0x82AB	   Taurus Controls */
		    /* 0x82AC - 0x8693	   Walker Richer & Quinn */
#define	ETHERTYPE_ACCTON	0x8390	/* Accton Technologies (unregistered) */
#define	ETHERTYPE_TALARISMC	0x852B	/* Talaris multicast */
#define	ETHERTYPE_KALPANA	0x8582	/* Kalpana */
		    /* 0x8694 - 0x869D	   Idea Courier */
		    /* 0x869E - 0x86A1	   Computer Network Tech */
		    /* 0x86A3 - 0x86AC	   Gateway Communications */
#define	ETHERTYPE_SECTRA	0x86DB	/* SECTRA */
#define	ETHERTYPE_IPV6		0x86DD	/* IP protocol version 6 */
#define	ETHERTYPE_DELTACON	0x86DE	/* Delta Controls */
#define	ETHERTYPE_ATOMIC	0x86DF	/* ATOMIC */
		    /* 0x86E0 - 0x86EF	   Landis & Gyr Powers */
		    /* 0x8700 - 0x8710	   Motorola */
#define	ETHERTYPE_RDP		0x8739	/* Control Technology Inc. RDP Without IP */
#define	ETHERTYPE_MICP		0x873A	/* Control Technology Inc. Mcast Industrial Ctrl Proto. */
		    /* 0x873B - 0x873C	   Control Technology Inc. Proprietary */
#define	ETHERTYPE_TCPCOMP	0x876B	/* TCP/IP Compression (RFC1701) */
#define	ETHERTYPE_IPAS		0x876C	/* IP Autonomous Systems (RFC1701) */
#define	ETHERTYPE_SECUREDATA	0x876D	/* Secure Data (RFC1701) */
#define	ETHERTYPE_FLOWCONTROL	0x8808	/* 802.3x flow control packet */
#define	ETHERTYPE_SLOW		0x8809	/* 802.3ad link aggregation (LACP) */
#define	ETHERTYPE_PPP		0x880B	/* PPP (obsolete by PPPoE) */
#define	ETHERTYPE_HITACHI	0x8820	/* Hitachi Cable (Optoelectronic Systems Laboratory) */
#define ETHERTYPE_TEST		0x8822  /* Network Conformance Testing */
#define	ETHERTYPE_MPLS		0x8847	/* MPLS Unicast */
#define	ETHERTYPE_MPLS_MCAST	0x8848	/* MPLS Multicast */
#define	ETHERTYPE_AXIS		0x8856	/* Axis Communications AB proprietary bootstrap/config */
#define	ETHERTYPE_PPPOEDISC	0x8863	/* PPP Over Ethernet Discovery Stage */
#define	ETHERTYPE_PPPOE		0x8864	/* PPP Over Ethernet Session Stage */
#define	ETHERTYPE_LANPROBE	0x8888	/* HP LanProbe test? */
#define	ETHERTYPE_PAE		0x888e	/* EAPOL PAE/802.1x */
#define	ETHERTYPE_QINQ		0x88A8	/* 802.1ad VLAN stacking */
#define	ETHERTYPE_LOOPBACK	0x9000	/* Loopback: used to test interfaces */
#define	ETHERTYPE_LBACK		ETHERTYPE_LOOPBACK	/* DEC MOP loopback */
#define	ETHERTYPE_XNSSM		0x9001	/* 3Com (Formerly Bridge Communications), XNS Systems Management */
#define	ETHERTYPE_TCPSM		0x9002	/* 3Com (Formerly Bridge Communications), TCP/IP Systems Management */
#define	ETHERTYPE_BCLOOP	0x9003	/* 3Com (Formerly Bridge Communications), loopback detection */
#define	ETHERTYPE_DEBNI		0xAAAA	/* DECNET? Used by VAX 6220 DEBNI */
#define	ETHERTYPE_SONIX		0xFAF5	/* Sonix Arpeggio */
#define	ETHERTYPE_VITAL		0xFF00	/* BBN VITAL-LanBridge cache wakeups */
		    /* 0xFF00 - 0xFFOF	   ISC Bunker Ramo */

#define	ETHERTYPE_MAX		0xFFFF	/* Maximum valid ethernet type, reserved */

/*
 * The ETHERTYPE_NTRAILER packet types starting at ETHERTYPE_TRAIL have
 * (type-ETHERTYPE_TRAIL)*512 bytes of data followed
 * by an ETHER type (as given above) and then the (variable-length) header.
 */
#define	ETHERTYPE_TRAIL		0x1000		/* Trailer packet */
#define	ETHERTYPE_NTRAILER	16

#define	ETHERMTU	(ETHER_MAX_LEN-ETHER_HDR_LEN-ETHER_CRC_LEN)
#define	ETHERMIN	(ETHER_MIN_LEN-ETHER_HDR_LEN-ETHER_CRC_LEN)
#define	ETHERMTU_JUMBO	(ETHER_MAX_LEN_JUMBO - ETHER_HDR_LEN - ETHER_CRC_LEN)
/*
 * The ETHER_BPF_MTAP macro should be used by drivers which support hardware
 * offload for VLAN tag processing.  It will check the mbuf to see if it has
 * M_VLANTAG set, and if it does, will pass the packet along to
 * ether_vlan_mtap.  This function will re-insert VLAN tags for the duration
 * of the tap, so they show up properly for network analyzers.
 */
#define ETHER_BPF_MTAP(_ifp, _m) do {					\
	if (bpf_peers_present((_ifp)->if_bpf)) {			\
		M_ASSERTVALID(_m);					\
		if (((_m)->m_flags & M_VLANTAG) != 0)			\
			ether_vlan_mtap((_ifp)->if_bpf, (_m), NULL, 0);	\
		else							\
			bpf_mtap((_ifp)->if_bpf, (_m));			\
	}								\
} while (0)

/*
 * Names for 802.1q priorities ("802.1p").  Notice that in this scheme,
 * (0 < 1), allowing default 0-tagged traffic to take priority over background
 * tagged traffic.
 */
#define	IEEE8021Q_PCP_BK	1	/* Background (lowest) */
#define	IEEE8021Q_PCP_BE	0	/* Best effort (default) */
#define	IEEE8021Q_PCP_EE	2	/* Excellent effort */
#define	IEEE8021Q_PCP_CA	3	/* Critical applications */
#define	IEEE8021Q_PCP_VI	4	/* Video, < 100ms latency */
#define	IEEE8021Q_PCP_VO	5	/* Video, < 10ms latency */
#define	IEEE8021Q_PCP_IC	6	/* Internetwork control */
#define	IEEE8021Q_PCP_NC	7	/* Network control (highest) */

#ifdef _KERNEL

struct ifnet;
struct mbuf;
struct route;
struct sockaddr;
struct bpf_if;

extern	uint32_t ether_crc32_le(const uint8_t *, size_t);
extern	uint32_t ether_crc32_be(const uint8_t *, size_t);
extern	void ether_demux(struct ifnet *, struct mbuf *);
extern	void ether_ifattach(struct ifnet *, const u_int8_t *);
extern	void ether_ifdetach(struct ifnet *);
extern	int  ether_ioctl(struct ifnet *, u_long, caddr_t);
extern	int  ether_output(struct ifnet *, struct mbuf *,
	    const struct sockaddr *, struct route *);
extern	int  ether_output_frame(struct ifnet *, struct mbuf *);
extern	char *ether_sprintf(const u_int8_t *);
void	ether_vlan_mtap(struct bpf_if *, struct mbuf *,
	    void *, u_int);
struct mbuf  *ether_vlanencap(struct mbuf *, uint16_t);
bool	ether_8021q_frame(struct mbuf **mp, struct ifnet *ife, struct ifnet *p,
	    uint16_t vid, uint8_t pcp);
void	ether_fakeaddr(struct ether_addr *hwaddr);

#ifdef _SYS_EVENTHANDLER_H_
/* new ethernet interface attached event */
typedef void (*ether_ifattach_event_handler_t)(void *, struct ifnet *);
EVENTHANDLER_DECLARE(ether_ifattach_event, ether_ifattach_event_handler_t);
#endif

#else /* _KERNEL */

#include <sys/cdefs.h>

/*
 * Ethernet address conversion/parsing routines.
 */
__BEGIN_DECLS
struct	ether_addr *ether_aton(const char *);
struct	ether_addr *ether_aton_r(const char *, struct ether_addr *);
int	ether_hostton(const char *, struct ether_addr *);
int	ether_line(const char *, struct ether_addr *, char *);
char 	*ether_ntoa(const struct ether_addr *);
char 	*ether_ntoa_r(const struct ether_addr *, char *);
int	ether_ntohost(char *, const struct ether_addr *);
__END_DECLS

#endif /* !_KERNEL */

#endif /* !_NET_ETHERNET_H_ */
