/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 */

#ifndef CAIF_LAYER_H_
#define CAIF_LAYER_H_

#include <linux/list.h>

struct cflayer;
struct cfpkt;
struct cfpktq;
struct caif_payload_info;
struct caif_packet_funcs;

#define CAIF_LAYER_NAME_SZ 16

/**
 * caif_assert() - Assert function for CAIF.
 * @assert: expression to evaluate.
 *
 * This function will print a error message and a do WARN_ON if the
 * assertion failes. Normally this will do a stack up at the current location.
 */
#define caif_assert(assert)					\
do {								\
	if (!(assert)) {					\
		pr_err("caif:Assert detected:'%s'\n", #assert); \
		WARN_ON(!(assert));				\
	}							\
} while (0)

/**
 * enum caif_ctrlcmd - CAIF Stack Control Signaling sent in layer.ctrlcmd().
 *
 * @CAIF_CTRLCMD_FLOW_OFF_IND:		Flow Control is OFF, transmit function
 *					should stop sending data
 *
 * @CAIF_CTRLCMD_FLOW_ON_IND:		Flow Control is ON, transmit function
 *					can start sending data
 *
 * @CAIF_CTRLCMD_REMOTE_SHUTDOWN_IND:	Remote end modem has decided to close
 *					down channel
 *
 * @CAIF_CTRLCMD_INIT_RSP:		Called initially when the layer below
 *					has finished initialization
 *
 * @CAIF_CTRLCMD_DEINIT_RSP:		Called when de-initialization is
 *					complete
 *
 * @CAIF_CTRLCMD_INIT_FAIL_RSP:		Called if initialization fails
 *
 * @_CAIF_CTRLCMD_PHYIF_FLOW_OFF_IND:	CAIF Link layer temporarily cannot
 *					send more packets.
 * @_CAIF_CTRLCMD_PHYIF_FLOW_ON_IND:	Called if CAIF Link layer is able
 *					to send packets again.
 * @_CAIF_CTRLCMD_PHYIF_DOWN_IND:	Called if CAIF Link layer is going
 *					down.
 *
 * These commands are sent upwards in the CAIF stack to the CAIF Client.
 * They are used for signaling originating from the modem or CAIF Link Layer.
 * These are either responses (*_RSP) or events (*_IND).
 */
enum caif_ctrlcmd {
	CAIF_CTRLCMD_FLOW_OFF_IND,
	CAIF_CTRLCMD_FLOW_ON_IND,
	CAIF_CTRLCMD_REMOTE_SHUTDOWN_IND,
	CAIF_CTRLCMD_INIT_RSP,
	CAIF_CTRLCMD_DEINIT_RSP,
	CAIF_CTRLCMD_INIT_FAIL_RSP,
	_CAIF_CTRLCMD_PHYIF_FLOW_OFF_IND,
	_CAIF_CTRLCMD_PHYIF_FLOW_ON_IND,
	_CAIF_CTRLCMD_PHYIF_DOWN_IND,
};

/**
 * enum caif_modemcmd -	 Modem Control Signaling, sent from CAIF Client
 *			 to the CAIF Link Layer or modem.
 *
 * @CAIF_MODEMCMD_FLOW_ON_REQ:		Flow Control is ON, transmit function
 *					can start sending data.
 *
 * @CAIF_MODEMCMD_FLOW_OFF_REQ:		Flow Control is OFF, transmit function
 *					should stop sending data.
 *
 * @_CAIF_MODEMCMD_PHYIF_USEFULL:	Notify physical layer that it is in use
 *
 * @_CAIF_MODEMCMD_PHYIF_USELESS:	Notify physical layer that it is
 *					no longer in use.
 *
 * These are requests sent 'downwards' in the stack.
 * Flow ON, OFF can be indicated to the modem.
 */
enum caif_modemcmd {
	CAIF_MODEMCMD_FLOW_ON_REQ = 0,
	CAIF_MODEMCMD_FLOW_OFF_REQ = 1,
	_CAIF_MODEMCMD_PHYIF_USEFULL = 3,
	_CAIF_MODEMCMD_PHYIF_USELESS = 4
};

/**
 * enum caif_direction - CAIF Packet Direction.
 * Indicate if a packet is to be sent out or to be received in.
 * @CAIF_DIR_IN:		Incoming packet received.
 * @CAIF_DIR_OUT:		Outgoing packet to be transmitted.
 */
enum caif_direction {
	CAIF_DIR_IN = 0,
	CAIF_DIR_OUT = 1
};

/**
 * struct cflayer - CAIF Stack layer.
 * Defines the framework for the CAIF Core Stack.
 * @up:		Pointer up to the layer above.
 * @dn:		Pointer down to the layer below.
 * @node:	List node used when layer participate in a list.
 * @receive:	Packet receive function.
 * @transmit:	Packet transmit funciton.
 * @ctrlcmd:	Used for control signalling upwards in the stack.
 * @modemcmd:	Used for control signaling downwards in the stack.
 * @id:		The identity of this layer
 * @name:	Name of the layer.
 *
 *  This structure defines the layered structure in CAIF.
 *
 *  It defines CAIF layering structure, used by all CAIF Layers and the
 *  layers interfacing CAIF.
 *
 *  In order to integrate with CAIF an adaptation layer on top of the CAIF stack
 *  and PHY layer below the CAIF stack
 *  must be implemented. These layer must follow the design principles below.
 *
 *  Principles for layering of protocol layers:
 *    - All layers must use this structure. If embedding it, then place this
 *	structure first in the layer specific structure.
 *
 *    - Each layer should not depend on any others layer's private data.
 *
 *    - In order to send data upwards do
 *	layer->up->receive(layer->up, packet);
 *
 *    - In order to send data downwards do
 *	layer->dn->transmit(layer->dn, info, packet);
 */
struct cflayer {
	struct cflayer *up;
	struct cflayer *dn;
	struct list_head node;

	/*
	 *  receive() - Receive Function (non-blocking).
	 *  Contract: Each layer must implement a receive function passing the
	 *  CAIF packets upwards in the stack.
	 *	Packet handling rules:
	 *	      - The CAIF packet (cfpkt) ownership is passed to the
	 *		called receive function. This means that the the
	 *		packet cannot be accessed after passing it to the
	 *		above layer using up->receive().
	 *
	 *	      - If parsing of the packet fails, the packet must be
	 *		destroyed and negative error code returned
	 *		from the function.
	 *		EXCEPTION: If the framing layer (cffrml) returns
	 *			-EILSEQ, the packet is not freed.
	 *
	 *	      - If parsing succeeds (and above layers return OK) then
	 *		      the function must return a value >= 0.
	 *
	 *  Returns result < 0 indicates an error, 0 or positive value
	 *	     indicates success.
	 *
	 *  @layr: Pointer to the current layer the receive function is
	 *		implemented for (this pointer).
	 *  @cfpkt: Pointer to CaifPacket to be handled.
	 */
	int (*receive)(struct cflayer *layr, struct cfpkt *cfpkt);

	/*
	 *  transmit() - Transmit Function (non-blocking).
	 *  Contract: Each layer must implement a transmit function passing the
	 *	CAIF packet downwards in the stack.
	 *	Packet handling rules:
	 *	      - The CAIF packet (cfpkt) ownership is passed to the
	 *		transmit function. This means that the the packet
	 *		cannot be accessed after passing it to the below
	 *		layer using dn->transmit().
	 *
	 *	      - Upon error the packet ownership is still passed on,
	 *		so the packet shall be freed where error is detected.
	 *		Callers of the transmit function shall not free packets,
	 *		but errors shall be returned.
	 *
	 *	      - Return value less than zero means error, zero or
	 *		greater than zero means OK.
	 *
	 *  Returns result < 0 indicates an error, 0 or positive value
	 *		indicates success.
	 *
	 *  @layr:	Pointer to the current layer the receive function
	 *		isimplemented for (this pointer).
	 *  @cfpkt:	 Pointer to CaifPacket to be handled.
	 */
	int (*transmit) (struct cflayer *layr, struct cfpkt *cfpkt);

	/*
	 *  cttrlcmd() - Control Function upwards in CAIF Stack  (non-blocking).
	 *  Used for signaling responses (CAIF_CTRLCMD_*_RSP)
	 *  and asynchronous events from the modem  (CAIF_CTRLCMD_*_IND)
	 *
	 *  @layr:	Pointer to the current layer the receive function
	 *		is implemented for (this pointer).
	 *  @ctrl:	Control Command.
	 */
	void (*ctrlcmd) (struct cflayer *layr, enum caif_ctrlcmd ctrl,
			 int phyid);

	/*
	 *  modemctrl() - Control Function used for controlling the modem.
	 *  Used to signal down-wards in the CAIF stack.
	 *  Returns 0 on success, < 0 upon failure.
	 *
	 *  @layr:	Pointer to the current layer the receive function
	 *		is implemented for (this pointer).
	 *  @ctrl:  Control Command.
	 */
	int (*modemcmd) (struct cflayer *layr, enum caif_modemcmd ctrl);

	unsigned int id;
	char name[CAIF_LAYER_NAME_SZ];
};

/**
 * layer_set_up() - Set the up pointer for a specified layer.
 *  @layr: Layer where up pointer shall be set.
 *  @above: Layer above.
 */
#define layer_set_up(layr, above) ((layr)->up = (struct cflayer *)(above))

/**
 *  layer_set_dn() - Set the down pointer for a specified layer.
 *  @layr:  Layer where down pointer shall be set.
 *  @below: Layer below.
 */
#define layer_set_dn(layr, below) ((layr)->dn = (struct cflayer *)(below))

/**
 * struct dev_info - Physical Device info information about physical layer.
 * @dev:	Pointer to native physical device.
 * @id:		Physical ID of the physical connection used by the
 *		logical CAIF connection. Used by service layers to
 *		identify their physical id to Caif MUX (CFMUXL)so
 *		that the MUX can add the correct physical ID to the
 *		packet.
 */
struct dev_info {
	void *dev;
	unsigned int id;
};

/**
 * struct caif_payload_info - Payload information embedded in packet (sk_buff).
 *
 * @dev_info:	Information about the receiving device.
 *
 * @hdr_len:	Header length, used to align pay load on 32bit boundary.
 *
 * @channel_id: Channel ID of the logical CAIF connection.
 *		Used by mux to insert channel id into the caif packet.
 */
struct caif_payload_info {
	struct dev_info *dev_info;
	unsigned short hdr_len;
	unsigned short channel_id;
};

#endif	/* CAIF_LAYER_H_ */
