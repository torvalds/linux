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
int caif_connect_client(struct caif_connect_request *conn_req,
			struct cflayer *client_layer, int *ifindex,
			int *headroom, int *tailroom);

/**
 * caif_disconnect_client - Disconnects a client from the CAIF stack.
 *
 * @client_layer: Client layer to be removed.
 */
int caif_disconnect_client(struct cflayer *client_layer);

/**
 * caif_release_client - Release adaptation layer reference to client.
 *
 * @client_layer: Client layer.
 *
 * Releases a client/adaptation layer use of the caif stack.
 * This function must be used after caif_disconnect_client to
 * decrease the reference count of the service layer.
 */
void caif_release_client(struct cflayer *client_layer);

/**
 * connect_req_to_link_param - Translate configuration parameters
 *				from socket format to internal format.
 * @cnfg:	Pointer to configuration handler
 * @con_req:	Configuration parameters supplied in function
 *		caif_connect_client
 * @channel_setup_param: Parameters supplied to the CAIF Core stack for
 *			 setting up channels.
 *
 */
int connect_req_to_link_param(struct cfcnfg *cnfg,
				struct caif_connect_request *con_req,
				struct cfctrl_link_param *channel_setup_param);

/**
 * get_caif_conf() - Get the configuration handler.
 */
struct cfcnfg *get_caif_conf(void);


#endif /* CAIF_DEV_H_ */
