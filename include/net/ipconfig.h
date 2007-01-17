/*
 *  $Id: ipconfig.h,v 1.4 2001/04/30 04:51:46 davem Exp $
 *
 *  Copyright (C) 1997 Martin Mares
 *
 *  Automatic IP Layer Configuration
 */

/* The following are initdata: */

extern int ic_proto_enabled;	/* Protocols enabled (see IC_xxx) */
extern int ic_set_manually;	/* IPconfig parameters set manually */

extern __be32 ic_myaddr;		/* My IP address */
extern __be32 ic_gateway;		/* Gateway IP address */

extern __be32 ic_servaddr;		/* Boot server IP address */

extern __be32 root_server_addr;	/* Address of NFS server */
extern u8 root_server_path[];	/* Path to mount as root */


/* bits in ic_proto_{enabled,used} */
#define IC_PROTO	0xFF	/* Protocols mask: */
#define IC_BOOTP	0x01	/*   BOOTP (or DHCP, see below) */
#define IC_RARP		0x02	/*   RARP */
#define IC_USE_DHCP    0x100	/* If on, use DHCP instead of BOOTP */
