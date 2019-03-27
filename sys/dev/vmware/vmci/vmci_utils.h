/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* Some common utilities used by the VMCI kernel module. */

#ifndef _VMCI_UTILS_H_
#define _VMCI_UTILS_H_

/*
 *------------------------------------------------------------------------------
 *
 * vmci_hash_id --
 *
 *     Hash function used by the Simple Datagram API. Hashes only a VMCI ID (not
 *     the full VMCI handle). Based on the djb2 hash function by Dan Bernstein.
 *
 * Result:
 *     Returns guest call size.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static inline int
vmci_hash_id(vmci_id id, unsigned size)
{
	unsigned i;
	int hash = 5381;

	for (i = 0; i < sizeof(id); i++)
		hash = ((hash << 5) + hash) + (uint8_t)(id >> (i * 8));

	return (hash & (size - 1));
}

#endif /* !_VMCI_UTILS_H_ */
