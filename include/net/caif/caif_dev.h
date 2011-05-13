/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/ sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef CAIF_DEV_H_
#define CAIF_DEV_H_

#include <net/caif/caif_layer.h>
#include <net/caif/cfcnfg.h>
#include <linux/caif/caif_socket.h>
#include <linux/if.h>
#include <linux/net.h>

/**
 * struct caif_param - CAIF parameters.
 * @size:	Length of data
 * @data:	Binary Data Blob
 */
struct caif_param {
	u16  size;
	u8   data[256];
};

/**
 * struct caif_connect_request - Request data for CAIF channel setup.
 * @protocol:		Type of CAIF protocol to use (at, datagram etc)
 * @sockaddr:		Socket address to connect.
 * @priority:		Priority of the connection.
 * @link_selector:	Link selector (high bandwidth or low latency)
 * @ifindex:		kernel index of the interface.
 * @param:		Connect Request parameters (CAIF_SO_REQ_PARAM).
 *
 * This struct is used when connecting a CAIF channel.
 * It contains all CAIF channel configuration options.
 */
struct caif_connect_request {
	enum caif_protocol_type protocol;
	struct sockaddr_caif sockaddr;
	enum caif_channel_priority priority;
	enum caif_link_selector link_selector;
	int ifindex;
	struct caif_param param;
};

/**
 * caif_connect_client - Connect a client to CAIF Core Stack.
 * @config:		Channel setup parameters, specifying what address
 *			to connect on the Modem.
 * @client_layer:	User implementation of client layer. This layer
 *			MUST have receive and control callback functions
 *			implemented.
 * @ifindex:		Link layer interface index used for this connection.
 * @headroom:		Head room needed by CAIF protocol.
 * @tailroom:		Tail room needed by CAIF protocol.
 *
 * This function connects a CAIF channel. The Client must implement
 * the struct cflayer. This layer represents the Client layer and holds
 * receive functions and control callback functions. Control callback
 * function will receive information about connect/disconnect responses,
 * flow control etc (see enum caif_control).
 * E.g. CAIF Socket will call this function for each socket it connects
 * and have one client_layer instance for each socket.
 */
int caif_connect_client(struct net *net,
			struct caif_connect_request *conn_req,
			struct cflayer *client_layer, int *ifindex,
			int *headroom, int *tailroom);

/**
 * caif_disconnect_client - Disconnects a client from the CAIF stack.
 *
 * @client_layer: Client layer to be disconnected.
 */
int caif_disconnect_client(struct net *net, struct cflayer *client_layer);


/**
 * caif_client_register_refcnt - register ref-count functions provided by client.
 *
 * @adapt_layer: Client layer using CAIF Stack.
 * @hold:	Function provided by client layer increasing ref-count
 * @put:	Function provided by client layer decreasing ref-count
 *
 * Client of the CAIF Stack must register functions for reference counting.
 * These functions are called by the CAIF Stack for every upstream packet,
 * and must therefore be implemented efficiently.
 *
 * Client should call caif_free_client when reference count degrease to zero.
 */

void caif_client_register_refcnt(struct cflayer *adapt_layer,
					void (*hold)(struct cflayer *lyr),
					void (*put)(struct cflayer *lyr));
/**
 * caif_free_client - Free memory used to manage the client in the CAIF Stack.
 *
 * @client_layer: Client layer to be removed.
 *
 * This function must be called from client layer in order to free memory.
 * Caller must guarantee that no packets are in flight upstream when calling
 * this function.
 */
void caif_free_client(struct cflayer *adap_layer);

#endif /* CAIF_DEV_H_ */
