/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 */

#ifndef CFPKT_H_
#define CFPKT_H_
#include <net/caif/caif_layer.h>
#include <linux/types.h>
struct cfpkt;

/* Create a CAIF packet.
 * len: Length of packet to be created
 * @return New packet.
 */
struct cfpkt *cfpkt_create(u16 len);

/*
 * Destroy a CAIF Packet.
 * pkt Packet to be destroyed.
 */
void cfpkt_destroy(struct cfpkt *pkt);

/*
 * Extract header from packet.
 *
 * pkt Packet to extract header data from.
 * data Pointer to copy the header data into.
 * len Length of head data to copy.
 * @return zero on success and error code upon failure
 */
int cfpkt_extr_head(struct cfpkt *pkt, void *data, u16 len);

static inline u8 cfpkt_extr_head_u8(struct cfpkt *pkt)
{
	u8 tmp;

	cfpkt_extr_head(pkt, &tmp, 1);

	return tmp;
}

static inline u16 cfpkt_extr_head_u16(struct cfpkt *pkt)
{
	__le16 tmp;

	cfpkt_extr_head(pkt, &tmp, 2);

	return le16_to_cpu(tmp);
}

static inline u32 cfpkt_extr_head_u32(struct cfpkt *pkt)
{
	__le32 tmp;

	cfpkt_extr_head(pkt, &tmp, 4);

	return le32_to_cpu(tmp);
}

/*
 * Peek header from packet.
 * Reads data from packet without changing packet.
 *
 * pkt Packet to extract header data from.
 * data Pointer to copy the header data into.
 * len Length of head data to copy.
 * @return zero on success and error code upon failure
 */
int cfpkt_peek_head(struct cfpkt *pkt, void *data, u16 len);

/*
 * Extract header from trailer (end of packet).
 *
 * pkt Packet to extract header data from.
 * data Pointer to copy the trailer data into.
 * len Length of header data to copy.
 * @return zero on success and error code upon failure
 */
int cfpkt_extr_trail(struct cfpkt *pkt, void *data, u16 len);

/*
 * Add header to packet.
 *
 *
 * pkt Packet to add header data to.
 * data Pointer to data to copy into the header.
 * len Length of header data to copy.
 * @return zero on success and error code upon failure
 */
int cfpkt_add_head(struct cfpkt *pkt, const void *data, u16 len);

/*
 * Add trailer to packet.
 *
 *
 * pkt Packet to add trailer data to.
 * data Pointer to data to copy into the trailer.
 * len Length of trailer data to copy.
 * @return zero on success and error code upon failure
 */
int cfpkt_add_trail(struct cfpkt *pkt, const void *data, u16 len);

/*
 * Pad trailer on packet.
 * Moves data pointer in packet, no content copied.
 *
 * pkt Packet in which to pad trailer.
 * len Length of padding to add.
 * @return zero on success and error code upon failure
 */
int cfpkt_pad_trail(struct cfpkt *pkt, u16 len);

/*
 * Add a single byte to packet body (tail).
 *
 * pkt Packet in which to add byte.
 * data Byte to add.
 * @return zero on success and error code upon failure
 */
int cfpkt_addbdy(struct cfpkt *pkt, const u8 data);

/*
 * Add a data to packet body (tail).
 *
 * pkt Packet in which to add data.
 * data Pointer to data to copy into the packet body.
 * len Length of data to add.
 * @return zero on success and error code upon failure
 */
int cfpkt_add_body(struct cfpkt *pkt, const void *data, u16 len);

/*
 * Checks whether there are more data to process in packet.
 * pkt Packet to check.
 * @return true if more data are available in packet false otherwise
 */
bool cfpkt_more(struct cfpkt *pkt);

/*
 * Checks whether the packet is erroneous,
 * i.e. if it has been attempted to extract more data than available in packet
 * or writing more data than has been allocated in cfpkt_create().
 * pkt Packet to check.
 * @return true on error false otherwise
 */
bool cfpkt_erroneous(struct cfpkt *pkt);

/*
 * Get the packet length.
 * pkt Packet to get length from.
 * @return Number of bytes in packet.
 */
u16 cfpkt_getlen(struct cfpkt *pkt);

/*
 * Set the packet length, by adjusting the trailer pointer according to length.
 * pkt Packet to set length.
 * len Packet length.
 * @return Number of bytes in packet.
 */
int cfpkt_setlen(struct cfpkt *pkt, u16 len);

/*
 * cfpkt_append - Appends a packet's data to another packet.
 * dstpkt:    Packet to append data into, WILL BE FREED BY THIS FUNCTION
 * addpkt:    Packet to be appended and automatically released,
 *            WILL BE FREED BY THIS FUNCTION.
 * expectlen: Packet's expected total length. This should be considered
 *            as a hint.
 * NB: Input packets will be destroyed after appending and cannot be used
 * after calling this function.
 * @return    The new appended packet.
 */
struct cfpkt *cfpkt_append(struct cfpkt *dstpkt, struct cfpkt *addpkt,
		      u16 expectlen);

/*
 * cfpkt_split - Split a packet into two packets at the specified split point.
 * pkt: Packet to be split (will contain the first part of the data on exit)
 * pos: Position to split packet in two parts.
 * @return The new packet, containing the second part of the data.
 */
struct cfpkt *cfpkt_split(struct cfpkt *pkt, u16 pos);

/*
 * Iteration function, iterates the packet buffers from start to end.
 *
 * Checksum iteration function used to iterate buffers
 * (we may have packets consisting of a chain of buffers)
 * pkt:       Packet to calculate checksum for
 * iter_func: Function pointer to iteration function
 * chks:      Checksum calculated so far.
 * buf:       Pointer to the buffer to checksum
 * len:       Length of buf.
 * data:      Initial checksum value.
 * @return    Checksum of buffer.
 */

int cfpkt_iterate(struct cfpkt *pkt,
		u16 (*iter_func)(u16 chks, void *buf, u16 len),
		u16 data);

/* Map from a "native" packet (e.g. Linux Socket Buffer) to a CAIF packet.
 *  dir - Direction indicating whether this packet is to be sent or received.
 *  nativepkt  - The native packet to be transformed to a CAIF packet
 *  @return The mapped CAIF Packet CFPKT.
 */
struct cfpkt *cfpkt_fromnative(enum caif_direction dir, void *nativepkt);

/* Map from a CAIF packet to a "native" packet (e.g. Linux Socket Buffer).
 *  pkt  - The CAIF packet to be transformed into a "native" packet.
 *  @return The native packet transformed from a CAIF packet.
 */
void *cfpkt_tonative(struct cfpkt *pkt);

/*
 * Returns packet information for a packet.
 * pkt Packet to get info from;
 * @return Packet information
 */
struct caif_payload_info *cfpkt_info(struct cfpkt *pkt);

/** cfpkt_set_prio - set priority for a CAIF packet.
 *
 * @pkt: The CAIF packet to be adjusted.
 * @prio: one of TC_PRIO_ constants.
 */
void cfpkt_set_prio(struct cfpkt *pkt, int prio);

#endif				/* CFPKT_H_ */
