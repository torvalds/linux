/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Equalizer Load-balancer for serial network interfaces.
 *
 * (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License, incorporated herein by reference.
 * 
 * The author may be reached as simon@ncm.com, or C/O
 *    NCM
 *    Attn: Simon Janes
 *    6803 Whittier Ave
 *    McLean VA 22101
 *    Phone: 1-703-847-0040 ext 103
 */

#ifndef _UAPI_LINUX_IF_EQL_H
#define _UAPI_LINUX_IF_EQL_H

#define EQL_DEFAULT_SLAVE_PRIORITY 28800
#define EQL_DEFAULT_MAX_SLAVES     4
#define EQL_DEFAULT_MTU            576
#define EQL_DEFAULT_RESCHED_IVAL   HZ

#define EQL_ENSLAVE     (SIOCDEVPRIVATE)
#define EQL_EMANCIPATE  (SIOCDEVPRIVATE + 1)

#define EQL_GETSLAVECFG (SIOCDEVPRIVATE + 2)
#define EQL_SETSLAVECFG (SIOCDEVPRIVATE + 3)

#define EQL_GETMASTRCFG (SIOCDEVPRIVATE + 4)
#define EQL_SETMASTRCFG (SIOCDEVPRIVATE + 5)


typedef struct master_config {
	char	master_name[16];
	int	max_slaves;
	int	min_slaves;
} master_config_t;

typedef struct slave_config {
	char	slave_name[16];
	long	priority;
} slave_config_t;

typedef struct slaving_request {
	char	slave_name[16];
	long	priority;
} slaving_request_t;


#endif /* _UAPI_LINUX_IF_EQL_H */
