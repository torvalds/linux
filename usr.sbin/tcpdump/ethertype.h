/*	$OpenBSD: ethertype.h,v 1.14 2008/12/05 01:25:24 sthen Exp $	*/

/*
 * Copyright (c) 1993, 1994, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @(#) $Id: ethertype.h,v 1.14 2008/12/05 01:25:24 sthen Exp $ (LBL)
 */

/*
 * Ethernet types.
 *
 * We wrap the declarations with #ifdef, so that if a file includes
 * <netinet/if_ether.h>, which may declare some of these, we don't
 * get a bunch of complaints from the C compiler about redefinitions
 * of these values.
 *
 * We declare all of them here so that no file has to include
 * <netinet/if_ether.h> if all it needs are ETHERTYPE_ values.
 */

#ifndef ETHERTYPE_LEN
#define ETHERTYPE_LEN		2
#endif

#ifndef ETHERTYPE_GRE_ISO
#define ETHERTYPE_GRE_ISO	0x00FE	/* not really an ethertype only used in GRE */
#endif
#ifndef ETHERTYPE_PUP
#define ETHERTYPE_PUP		0x0200	/* PUP protocol */
#endif
#ifndef ETHERTYPE_IP
#define ETHERTYPE_IP		0x0800	/* IP protocol */
#endif
#ifndef ETHERTYPE_ARP
#define ETHERTYPE_ARP		0x0806	/* Addr. resolution protocol */
#endif
#ifndef ETHERTYPE_REVARP
#define ETHERTYPE_REVARP	0x8035	/* reverse Addr. resolution protocol */
#endif
#ifndef ETHERTYPE_NS
#define ETHERTYPE_NS		0x0600
#endif
#ifndef ETHERTYPE_SPRITE
#define ETHERTYPE_SPRITE	0x0500
#endif
#ifndef ETHERTYPE_TRAIL
#define ETHERTYPE_TRAIL		0x1000
#endif
#ifndef ETHERTYPE_MOPDL
#define ETHERTYPE_MOPDL		0x6001
#endif
#ifndef ETHERTYPE_MOPRC
#define ETHERTYPE_MOPRC		0x6002
#endif
#ifndef ETHERTYPE_DN
#define ETHERTYPE_DN		0x6003
#endif
#ifndef ETHERTYPE_LAT
#define ETHERTYPE_LAT		0x6004
#endif
#ifndef ETHERTYPE_SCA
#define ETHERTYPE_SCA		0x6007
#endif
#ifndef ETHERTYPE_LANBRIDGE
#define ETHERTYPE_LANBRIDGE	0x8038
#endif
#ifndef ETHERTYPE_DECDNS
#define ETHERTYPE_DECDNS	0x803c
#endif
#ifndef ETHERTYPE_DECDTS
#define ETHERTYPE_DECDTS	0x803e
#endif
#ifndef ETHERTYPE_VEXP
#define ETHERTYPE_VEXP		0x805b
#endif
#ifndef ETHERTYPE_VPROD
#define ETHERTYPE_VPROD		0x805c
#endif
#ifndef ETHERTYPE_ATALK
#define ETHERTYPE_ATALK		0x809b
#endif
#ifndef ETHERTYPE_AARP
#define ETHERTYPE_AARP		0x80f3
#endif
#ifndef ETHERTYPE_8021Q
#define ETHERTYPE_8021Q		0x8100
#endif
#ifndef ETHERTYPE_QINQ
#define ETHERTYPE_QINQ		0x88a8
#endif
#ifndef ETHERTYPE_IPX
#define ETHERTYPE_IPX		0x8137
#endif
#ifndef ETHERTYPE_IPV6
#define ETHERTYPE_IPV6		0x86dd
#endif
#ifndef ETHERTYPE_PPP
#define ETHERTYPE_PPP		0x880b
#endif
#ifndef ETHERTYPE_MPLS
#define ETHERTYPE_MPLS		0x8847
#endif
#ifndef ETHERTYPE_MPLS_MULTI
#define ETHERTYPE_MPLS_MULTI	0x8848
#endif
#ifndef ETHERTYPE_PPPOED
#define ETHERTYPE_PPPOED	0x8863
#endif
#ifndef ETHERTYPE_PPPOES
#define ETHERTYPE_PPPOES	0x8864
#endif
#ifndef ETHERTYPE_JUMBO
#define ETHERTYPE_JUMBO		0x8870
#endif
#ifndef ETHERTYPE_PAE
#define ETHERTYPE_PAE	  	0x888e
#endif
#ifndef ETHERTYPE_LLDP
#define ETHERTYPE_LLDP		0x88cc
#endif
#ifndef ETHERTYPE_LOOPBACK
#define ETHERTYPE_LOOPBACK	0x9000
#endif
#ifndef ETHERTYPE_VMAN
#define ETHERTYPE_VMAN		0x9100	/* Extreme VMAN Protocol */ 
#endif
#ifndef ETHERTYPE_ISO
#define ETHERTYPE_ISO		0xfefe	/* nonstandard - used in Cisco HDLC encapsulation */
#endif
