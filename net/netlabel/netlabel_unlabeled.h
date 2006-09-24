/*
 * NetLabel Unlabeled Support
 *
 * This file defines functions for dealing with unlabeled packets for the
 * NetLabel system.  The NetLabel system manages static and dynamic label
 * mappings for network protocols such as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul.moore@hp.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;  if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef _NETLABEL_UNLABELED_H
#define _NETLABEL_UNLABELED_H

#include <net/netlabel.h>

/*
 * The following NetLabel payloads are supported by the Unlabeled subsystem.
 *
 * o ACK:
 *   Sent by the kernel in response to an applications message, applications
 *   should never send this message.
 *
 *   +----------------------+-----------------------+
 *   | seq number (32 bits) | return code (32 bits) |
 *   +----------------------+-----------------------+
 *
 *     seq number:  the sequence number of the original message, taken from the
 *                  nlmsghdr structure
 *     return code: return value, based on errno values
 *
 * o ACCEPT
 *   This message is sent from an application to specify if the kernel should
 *   allow unlabled packets to pass if they do not match any of the static
 *   mappings defined in the unlabeled module.
 *
 *   +-----------------+
 *   | allow (32 bits) |
 *   +-----------------+
 *
 *     allow: if true (1) then allow the packets to pass, if false (0) then
 *            reject the packets
 *
 * o LIST
 *   This message can be sent either from an application or by the kernel in
 *   response to an application generated LIST message.  When sent by an
 *   application there is no payload.  The kernel should respond to a LIST
 *   message either with a LIST message on success or an ACK message on
 *   failure.
 *
 *   +-----------------------+
 *   | accept flag (32 bits) |
 *   +-----------------------+
 *
 *     accept flag: if true (1) then unlabeled packets are allowed to pass,
 *                  if false (0) then unlabeled packets are rejected
 *
 */

/* NetLabel Unlabeled commands */
enum {
	NLBL_UNLABEL_C_UNSPEC,
	NLBL_UNLABEL_C_ACK,
	NLBL_UNLABEL_C_ACCEPT,
	NLBL_UNLABEL_C_LIST,
	__NLBL_UNLABEL_C_MAX,
};
#define NLBL_UNLABEL_C_MAX (__NLBL_UNLABEL_C_MAX - 1)

/* NetLabel protocol functions */
int netlbl_unlabel_genl_init(void);

/* Process Unlabeled incoming network packets */
int netlbl_unlabel_getattr(struct netlbl_lsm_secattr *secattr);

/* Set the default configuration to allow Unlabeled packets */
int netlbl_unlabel_defconf(void);

#endif
