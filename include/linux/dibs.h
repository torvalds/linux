/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Direct Internal Buffer Sharing
 *
 *  Definitions for the DIBS module
 *
 *  Copyright IBM Corp. 2025
 */
#ifndef _DIBS_H
#define _DIBS_H

/* DIBS - Direct Internal Buffer Sharing - concept
 * -----------------------------------------------
 * In the case of multiple system sharing the same hardware, dibs fabrics can
 * provide dibs devices to these systems. The systems use dibs devices of the
 * same fabric to communicate via dmbs (Direct Memory Buffers). Each dmb has
 * exactly one owning local dibs device and one remote using dibs device, that
 * is authorized to write into this dmb. This access control is provided by the
 * dibs fabric.
 *
 * Because the access to the dmb is based on access to physical memory, it is
 * lossless and synchronous. The remote devices can directly access any offset
 * of the dmb.
 *
 * Dibs fabrics, dibs devices and dmbs are identified by tokens and ids.
 * Dibs fabric id is unique within the same hardware (with the exception of the
 * dibs loopback fabric), dmb token is unique within the same fabric, dibs
 * device gids are guaranteed to be unique within the same fabric and
 * statistically likely to be globally unique. The exchange of these tokens and
 * ids between the systems is not part of the dibs concept.
 *
 * The dibs layer provides an abstraction between dibs device drivers and dibs
 * clients.
 */

/* DIBS client
 * -----------
 */
#define MAX_DIBS_CLIENTS	8

struct dibs_client {
	/* client name for logging and debugging purposes */
	const char *name;
	/* client index - provided and used by dibs layer */
	u8 id;
};

/* Functions to be called by dibs clients:
 */
/**
 * dibs_register_client() - register a client with dibs layer
 * @client: this client
 *
 * Return: zero on success.
 */
int dibs_register_client(struct dibs_client *client);
/**
 * dibs_unregister_client() - unregister a client with dibs layer
 * @client: this client
 *
 * Return: zero on success.
 */
int dibs_unregister_client(struct dibs_client *client);

#endif	/* _DIBS_H */
