/*	$OpenBSD: ipx.h,v 1.7 2007/10/07 16:41:05 deraadt Exp $	*/

/*
 * IPX protocol formats 
 *
 * @(#) $Id: ipx.h,v 1.7 2007/10/07 16:41:05 deraadt Exp $
 */

/* well-known sockets */
#define	IPX_SKT_NCP		0x0451
#define	IPX_SKT_SAP		0x0452
#define	IPX_SKT_RIP		0x0453
#define	IPX_SKT_NETBIOS		0x0455
#define	IPX_SKT_DIAGNOSTICS	0x0456

/* IPX transport header */
struct ipxHdr {
    u_short	cksum;		/* Checksum */
    u_short	length;		/* Length, in bytes, including header */
    u_char	tCtl;		/* Transport Control (i.e. hop count) */
    u_char	pType;		/* Packet Type (i.e. level 2 protocol) */
    u_short	dstNet[2];	/* destination net */
    u_char	dstNode[6];	/* destination node */
    u_short	dstSkt;		/* destination socket */
    u_short	srcNet[2];	/* source net */
    u_char	srcNode[6];	/* source node */
    u_short	srcSkt;		/* source socket */
} ipx_hdr_t;

#define ipxSize	30

