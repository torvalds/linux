/*********************************************************************
 *                
 * Filename:      discovery.h
 * Version:       
 * Description:   
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Apr  6 16:53:53 1999
 * Modified at:   Tue Oct  5 10:05:10 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 2000-2002 Jean Tourrilhes <jt@hpl.hp.com>
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, see <http://www.gnu.org/licenses/>.
 *     
 ********************************************************************/

#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <asm/param.h>

#include <net/irda/irda.h>
#include <net/irda/irqueue.h>		/* irda_queue_t */
#include <net/irda/irlap_event.h>	/* LAP_REASON */

#define DISCOVERY_EXPIRE_TIMEOUT (2*sysctl_discovery_timeout*HZ)
#define DISCOVERY_DEFAULT_SLOTS  0

/*
 *  This type is used by the protocols that transmit 16 bits words in 
 *  little endian format. A little endian machine stores MSB of word in
 *  byte[1] and LSB in byte[0]. A big endian machine stores MSB in byte[0] 
 *  and LSB in byte[1].
 *
 * This structure is used in the code for things that are endian neutral
 * but that fit in a word so that we can manipulate them efficiently.
 * By endian neutral, I mean things that are really an array of bytes,
 * and always used as such, for example the hint bits. Jean II
 */
typedef union {
	__u16 word;
	__u8  byte[2];
} __u16_host_order;

/* Types of discovery */
typedef enum {
	DISCOVERY_LOG,		/* What's in our discovery log */
	DISCOVERY_ACTIVE,	/* Doing our own discovery on the medium */
	DISCOVERY_PASSIVE,	/* Peer doing discovery on the medium */
	EXPIRY_TIMEOUT,		/* Entry expired due to timeout */
} DISCOVERY_MODE;

#define NICKNAME_MAX_LEN 21

/* Basic discovery information about a peer */
typedef struct irda_device_info		discinfo_t;	/* linux/irda.h */

/*
 * The DISCOVERY structure is used for both discovery requests and responses
 */
typedef struct discovery_t {
	irda_queue_t	q;		/* Must be first! */

	discinfo_t	data;		/* Basic discovery information */
	int		name_len;	/* Length of nickname */

	LAP_REASON	condition;	/* More info about the discovery */
	int		gen_addr_bit;	/* Need to generate a new device
					 * address? */
	int		nslots;		/* Number of slots to use when
					 * discovering */
	unsigned long	timestamp;	/* Last time discovered */
	unsigned long	firststamp;	/* First time discovered */
} discovery_t;

void irlmp_add_discovery(hashbin_t *cachelog, discovery_t *discovery);
void irlmp_add_discovery_log(hashbin_t *cachelog, hashbin_t *log);
void irlmp_expire_discoveries(hashbin_t *log, __u32 saddr, int force);
struct irda_device_info *irlmp_copy_discoveries(hashbin_t *log, int *pn,
						__u16 mask, int old_entries);

#endif
