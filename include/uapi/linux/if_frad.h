/*
 * DLCI/FRAD	Definitions for Frame Relay Access Devices.  DLCI devices are
 *		created for each DLCI associated with a FRAD.  The FRAD driver
 *		is not truly a network device, but the lower level device
 *		handler.  This allows other FRAD manufacturers to use the DLCI
 *		code, including its RFC1490 encapsulation alongside the current
 *		implementation for the Sangoma cards.
 *
 * Version:	@(#)if_ifrad.h	0.15	31 Mar 96
 *
 * Author:	Mike McLagan <mike.mclagan@linux.org>
 *
 * Changes:
 *		0.15	Mike McLagan	changed structure defs (packed)
 *					re-arranged flags
 *					added DLCI_RET vars
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#ifndef _UAPI_FRAD_H_
#define _UAPI_FRAD_H_

#include <linux/if.h>

/* Structures and constants associated with the DLCI device driver */

struct dlci_add
{
   char  devname[IFNAMSIZ];
   short dlci;
};

#define DLCI_GET_CONF	(SIOCDEVPRIVATE + 2)
#define DLCI_SET_CONF	(SIOCDEVPRIVATE + 3)

/* 
 * These are related to the Sangoma SDLA and should remain in order. 
 * Code within the SDLA module is based on the specifics of this 
 * structure.  Change at your own peril.
 */
struct dlci_conf {
   short flags;
   short CIR_fwd;
   short Bc_fwd;
   short Be_fwd;
   short CIR_bwd;
   short Bc_bwd;
   short Be_bwd; 

/* these are part of the status read */
   short Tc_fwd;
   short Tc_bwd;
   short Tf_max;
   short Tb_max;

/* add any new fields here above is a mirror of sdla_dlci_conf */
};

#define DLCI_GET_SLAVE	(SIOCDEVPRIVATE + 4)

/* configuration flags for DLCI */
#define DLCI_IGNORE_CIR_OUT	0x0001
#define DLCI_ACCOUNT_CIR_IN	0x0002
#define DLCI_BUFFER_IF		0x0008

#define DLCI_VALID_FLAGS	0x000B

/* defines for the actual Frame Relay hardware */
#define FRAD_GET_CONF	(SIOCDEVPRIVATE)
#define FRAD_SET_CONF	(SIOCDEVPRIVATE + 1)

#define FRAD_LAST_IOCTL	FRAD_SET_CONF

/*
 * Based on the setup for the Sangoma SDLA.  If changes are 
 * necessary to this structure, a routine will need to be 
 * added to that module to copy fields.
 */
struct frad_conf 
{
   short station;
   short flags;
   short kbaud;
   short clocking;
   short mtu;
   short T391;
   short T392;
   short N391;
   short N392;
   short N393;
   short CIR_fwd;
   short Bc_fwd;
   short Be_fwd;
   short CIR_bwd;
   short Bc_bwd;
   short Be_bwd;

/* Add new fields here, above is a mirror of the sdla_conf */

};

#define FRAD_STATION_CPE	0x0000
#define FRAD_STATION_NODE	0x0001

#define FRAD_TX_IGNORE_CIR	0x0001
#define FRAD_RX_ACCOUNT_CIR	0x0002
#define FRAD_DROP_ABORTED	0x0004
#define FRAD_BUFFERIF		0x0008
#define FRAD_STATS		0x0010
#define FRAD_MCI		0x0100
#define FRAD_AUTODLCI		0x8000
#define FRAD_VALID_FLAGS	0x811F

#define FRAD_CLOCK_INT		0x0001
#define FRAD_CLOCK_EXT		0x0000


#endif /* _UAPI_FRAD_H_ */
