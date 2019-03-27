/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/







/**
 * @file
 *
 * Support functions for managing the MII management port
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_MGMT_PORT_H__
#define __CVMX_MGMT_PORT_H__

#include "cvmx-helper.h"
#include "cvmx-helper-board.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CVMX_MGMT_PORT_NUM_PORTS        2       /* Right now we only have one mgmt port */
#define CVMX_MGMT_PORT_NUM_TX_BUFFERS   16      /* Number of TX ring buffer entries and buffers */
#define CVMX_MGMT_PORT_NUM_RX_BUFFERS   128     /* Number of RX ring buffer entries and buffers */
#define CVMX_MGMT_PORT_TX_BUFFER_SIZE   12288   /* Size of each TX/RX buffer */
#define CVMX_MGMT_PORT_RX_BUFFER_SIZE   1536    /* Size of each TX/RX buffer */

typedef enum
{
    CVMX_MGMT_PORT_SUCCESS = 0,
    CVMX_MGMT_PORT_NO_MEMORY = -1,
    CVMX_MGMT_PORT_INVALID_PARAM = -2,
    CVMX_MGMT_PORT_INIT_ERROR = -3,
} cvmx_mgmt_port_result_t;


/* Enumeration of Net Device interface flags. */
typedef enum
{
    CVMX_IFF_PROMISC = 0x100, 		/* receive all packets           */
    CVMX_IFF_ALLMULTI = 0x200, 		/* receive all multicast packets */
} cvmx_mgmt_port_netdevice_flags_t;

/**
 * Called to initialize a management port for use. Multiple calls
 * to this function accross applications is safe.
 *
 * @param port   Port to initialize
 *
 * @return CVMX_MGMT_PORT_SUCCESS or an error code
 */
extern cvmx_mgmt_port_result_t cvmx_mgmt_port_initialize(int port);

/**
 * Shutdown a management port. This currently disables packet IO
 * but leaves all hardware and buffers. Another application can then
 * call initialize() without redoing the hardware setup.
 *
 * @param port   Management port
 *
 * @return CVMX_MGMT_PORT_SUCCESS or an error code
 */
extern cvmx_mgmt_port_result_t cvmx_mgmt_port_shutdown(int port);

/**
 * Enable packet IO on a management port
 *
 * @param port   Management port
 *
 * @return CVMX_MGMT_PORT_SUCCESS or an error code
 */
extern cvmx_mgmt_port_result_t cvmx_mgmt_port_enable(int port);

/**
 * Disable packet IO on a management port
 *
 * @param port   Management port
 *
 * @return CVMX_MGMT_PORT_SUCCESS or an error code
 */
extern cvmx_mgmt_port_result_t cvmx_mgmt_port_disable(int port);

/**
 * Send a packet out the management port. The packet is copied so
 * the input buffer isn't used after this call.
 *
 * @param port       Management port
 * @param packet_len Length of the packet to send. It does not include the final CRC
 * @param buffer     Packet data
 *
 * @return CVMX_MGMT_PORT_SUCCESS or an error code
 */
extern cvmx_mgmt_port_result_t cvmx_mgmt_port_send(int port, int packet_len, void *buffer);

#if defined(__FreeBSD__)
/**
 * Send a packet out the management port. The packet is copied so
 * the input mbuf isn't used after this call.
 *
 * @param port       Management port
 * @param m          Packet mbuf (with pkthdr)
 *
 * @return CVMX_MGMT_PORT_SUCCESS or an error code
 */
extern cvmx_mgmt_port_result_t cvmx_mgmt_port_sendm(int port, const struct mbuf *m);
#endif

/**
 * Receive a packet from the management port.
 *
 * @param port       Management port
 * @param buffer_len Size of the buffer to receive the packet into
 * @param buffer     Buffer to receive the packet into
 *
 * @return The size of the packet, or a negative erorr code on failure. Zero
 *         means that no packets were available.
 */
extern int cvmx_mgmt_port_receive(int port, int buffer_len, uint8_t *buffer);

/**
 * Set the MAC address for a management port
 *
 * @param port   Management port
 * @param mac    New MAC address. The lower 6 bytes are used.
 *
 * @return CVMX_MGMT_PORT_SUCCESS or an error code
 */
extern cvmx_mgmt_port_result_t cvmx_mgmt_port_set_mac(int port, uint64_t mac);

/**
 * Get the MAC address for a management port
 *
 * @param port   Management port
 *
 * @return MAC address
 */
extern uint64_t cvmx_mgmt_port_get_mac(int port);
#define CVMX_MGMT_PORT_GET_MAC_ERROR ((unsigned long long)-2LL)

/**
 * Set the multicast list.
 *
 * @param port   Management port
 * @param flags  Interface flags
 *
 * @return
 */
extern void cvmx_mgmt_port_set_multicast_list(int port, int flags);

/**
 * Set the maximum packet allowed in. Size is specified
 * including L2 but without FCS. A normal MTU would corespond
 * to 1514 assuming the standard 14 byte L2 header.
 *
 * @param port   Management port
 * @param size_without_fcs
 *               Size in bytes without FCS
 */
extern void cvmx_mgmt_port_set_max_packet_size(int port, int size_without_fcs);

/**
 * Return the link state of an RGMII/MII port as returned by
 * auto negotiation. The result of this function may not match
 * Octeon's link config if auto negotiation has changed since
 * the last call to __cvmx_mgmt_port_link_set().
 *
 * @param port     The RGMII/MII interface port to query
 *
 * @return Link state
 */
extern cvmx_helper_link_info_t cvmx_mgmt_port_link_get(int port);

/**
 * Configure RGMII/MII port for the specified link state. This
 * function does not influence auto negotiation at the PHY level.
 *
 * @param port      RGMII/MII interface port
 * @param link_info The new link state
 *
 * @return Zero on success, negative on failure
 */
extern int cvmx_mgmt_port_link_set(int port, cvmx_helper_link_info_t link_info);

/**
 * Return the number of management ports supported on this board.
 *
 * @return Number of ports
 */
extern int cvmx_mgmt_port_num_ports(void);

#ifdef __cplusplus
}
#endif

#endif /* __CVMX_MGMT_PORT_H__ */
