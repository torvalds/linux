/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CONNECTION_H_
#define CONNECTION_H_

#include "interface/vchi/vchi_cfg_internal.h"
#include "interface/vchi/vchi_common.h"
#include "interface/vchi/message_drivers/message.h"

/******************************************************************************
 Global defs
 *****************************************************************************/

// Opaque handle for a connection / service pair
typedef struct opaque_vchi_connection_connected_service_handle_t *VCHI_CONNECTION_SERVICE_HANDLE_T;

// opaque handle to the connection state information
typedef struct opaque_vchi_connection_info_t VCHI_CONNECTION_STATE_T;

typedef struct vchi_connection_t VCHI_CONNECTION_T;


/******************************************************************************
 API
 *****************************************************************************/

// Routine to init a connection with a particular low level driver
typedef VCHI_CONNECTION_STATE_T * (*VCHI_CONNECTION_INIT_T)( struct vchi_connection_t * connection,
                                                             const VCHI_MESSAGE_DRIVER_T * driver );

// Routine to control CRC enabling at a connection level
typedef int32_t (*VCHI_CONNECTION_CRC_CONTROL_T)( VCHI_CONNECTION_STATE_T *state_handle,
                                                  VCHI_CRC_CONTROL_T control );

// Routine to create a service
typedef int32_t (*VCHI_CONNECTION_SERVICE_CONNECT_T)( VCHI_CONNECTION_STATE_T *state_handle,
                                                      int32_t service_id,
                                                      uint32_t rx_fifo_size,
                                                      uint32_t tx_fifo_size,
                                                      int server,
                                                      VCHI_CALLBACK_T callback,
                                                      void *callback_param,
                                                      int32_t want_crc,
                                                      int32_t want_unaligned_bulk_rx,
                                                      int32_t want_unaligned_bulk_tx,
                                                      VCHI_CONNECTION_SERVICE_HANDLE_T *service_handle );

// Routine to close a service
typedef int32_t (*VCHI_CONNECTION_SERVICE_DISCONNECT_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle );

// Routine to queue a message
typedef int32_t (*VCHI_CONNECTION_SERVICE_QUEUE_MESSAGE_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle,
                                                            const void *data,
                                                            uint32_t data_size,
                                                            VCHI_FLAGS_T flags,
                                                            void *msg_handle );

// scatter-gather (vector) message queueing
typedef int32_t (*VCHI_CONNECTION_SERVICE_QUEUE_MESSAGEV_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle,
                                                             VCHI_MSG_VECTOR_T *vector,
                                                             uint32_t count,
                                                             VCHI_FLAGS_T flags,
                                                             void *msg_handle );

// Routine to dequeue a message
typedef int32_t (*VCHI_CONNECTION_SERVICE_DEQUEUE_MESSAGE_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle,
                                                              void *data,
                                                              uint32_t max_data_size_to_read,
                                                              uint32_t *actual_msg_size,
                                                              VCHI_FLAGS_T flags );

// Routine to peek at a message
typedef int32_t (*VCHI_CONNECTION_SERVICE_PEEK_MESSAGE_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle,
                                                           void **data,
                                                           uint32_t *msg_size,
                                                           VCHI_FLAGS_T flags );

// Routine to hold a message
typedef int32_t (*VCHI_CONNECTION_SERVICE_HOLD_MESSAGE_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle,
                                                           void **data,
                                                           uint32_t *msg_size,
                                                           VCHI_FLAGS_T flags,
                                                           void **message_handle );

// Routine to initialise a received message iterator
typedef int32_t (*VCHI_CONNECTION_SERVICE_LOOKAHEAD_MESSAGE_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle,
                                                                VCHI_MSG_ITER_T *iter,
                                                                VCHI_FLAGS_T flags );

// Routine to release a held message
typedef int32_t (*VCHI_CONNECTION_HELD_MSG_RELEASE_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle,
                                                       void *message_handle );

// Routine to get info on a held message
typedef int32_t (*VCHI_CONNECTION_HELD_MSG_INFO_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle,
                                                    void *message_handle,
                                                    void **data,
                                                    int32_t *msg_size,
                                                    uint32_t *tx_timestamp,
                                                    uint32_t *rx_timestamp );

// Routine to check whether the iterator has a next message
typedef int32_t (*VCHI_CONNECTION_MSG_ITER_HAS_NEXT_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service,
                                                       const VCHI_MSG_ITER_T *iter );

// Routine to advance the iterator
typedef int32_t (*VCHI_CONNECTION_MSG_ITER_NEXT_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service,
                                                    VCHI_MSG_ITER_T *iter,
                                                    void **data,
                                                    uint32_t *msg_size );

// Routine to remove the last message returned by the iterator
typedef int32_t (*VCHI_CONNECTION_MSG_ITER_REMOVE_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service,
                                                      VCHI_MSG_ITER_T *iter );

// Routine to hold the last message returned by the iterator
typedef int32_t (*VCHI_CONNECTION_MSG_ITER_HOLD_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service,
                                                    VCHI_MSG_ITER_T *iter,
                                                    void **msg_handle );

// Routine to transmit bulk data
typedef int32_t (*VCHI_CONNECTION_BULK_QUEUE_TRANSMIT_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle,
                                                          const void *data_src,
                                                          uint32_t data_size,
                                                          VCHI_FLAGS_T flags,
                                                          void *bulk_handle );

// Routine to receive data
typedef int32_t (*VCHI_CONNECTION_BULK_QUEUE_RECEIVE_T)( VCHI_CONNECTION_SERVICE_HANDLE_T service_handle,
                                                         void *data_dst,
                                                         uint32_t data_size,
                                                         VCHI_FLAGS_T flags,
                                                         void *bulk_handle );

// Routine to report if a server is available
typedef int32_t (*VCHI_CONNECTION_SERVER_PRESENT)( VCHI_CONNECTION_STATE_T *state, int32_t service_id, int32_t peer_flags );

// Routine to report the number of RX slots available
typedef int (*VCHI_CONNECTION_RX_SLOTS_AVAILABLE)( const VCHI_CONNECTION_STATE_T *state );

// Routine to report the RX slot size
typedef uint32_t (*VCHI_CONNECTION_RX_SLOT_SIZE)( const VCHI_CONNECTION_STATE_T *state );

// Callback to indicate that the other side has added a buffer to the rx bulk DMA FIFO
typedef void (*VCHI_CONNECTION_RX_BULK_BUFFER_ADDED)(VCHI_CONNECTION_STATE_T *state,
                                                     int32_t service,
                                                     uint32_t length,
                                                     MESSAGE_TX_CHANNEL_T channel,
                                                     uint32_t channel_params,
                                                     uint32_t data_length,
                                                     uint32_t data_offset);

// Callback to inform a service that a Xon or Xoff message has been received
typedef void (*VCHI_CONNECTION_FLOW_CONTROL)(VCHI_CONNECTION_STATE_T *state, int32_t service_id, int32_t xoff);

// Callback to inform a service that a server available reply message has been received
typedef void (*VCHI_CONNECTION_SERVER_AVAILABLE_REPLY)(VCHI_CONNECTION_STATE_T *state, int32_t service_id, uint32_t flags);

// Callback to indicate that bulk auxiliary messages have arrived
typedef void (*VCHI_CONNECTION_BULK_AUX_RECEIVED)(VCHI_CONNECTION_STATE_T *state);

// Callback to indicate that bulk auxiliary messages have arrived
typedef void (*VCHI_CONNECTION_BULK_AUX_TRANSMITTED)(VCHI_CONNECTION_STATE_T *state, void *handle);

// Callback with all the connection info you require
typedef void (*VCHI_CONNECTION_INFO)(VCHI_CONNECTION_STATE_T *state, uint32_t protocol_version, uint32_t slot_size, uint32_t num_slots, uint32_t min_bulk_size);

// Callback to inform of a disconnect
typedef void (*VCHI_CONNECTION_DISCONNECT)(VCHI_CONNECTION_STATE_T *state, uint32_t flags);

// Callback to inform of a power control request
typedef void (*VCHI_CONNECTION_POWER_CONTROL)(VCHI_CONNECTION_STATE_T *state, MESSAGE_TX_CHANNEL_T channel, int32_t enable);

// allocate memory suitably aligned for this connection
typedef void * (*VCHI_BUFFER_ALLOCATE)(VCHI_CONNECTION_SERVICE_HANDLE_T service_handle, uint32_t * length);

// free memory allocated by buffer_allocate
typedef void   (*VCHI_BUFFER_FREE)(VCHI_CONNECTION_SERVICE_HANDLE_T service_handle, void * address);


/******************************************************************************
 System driver struct
 *****************************************************************************/

struct opaque_vchi_connection_api_t
{
   // Routine to init the connection
   VCHI_CONNECTION_INIT_T                      init;

   // Connection-level CRC control
   VCHI_CONNECTION_CRC_CONTROL_T               crc_control;

   // Routine to connect to or create service
   VCHI_CONNECTION_SERVICE_CONNECT_T           service_connect;

   // Routine to disconnect from a service
   VCHI_CONNECTION_SERVICE_DISCONNECT_T        service_disconnect;

   // Routine to queue a message
   VCHI_CONNECTION_SERVICE_QUEUE_MESSAGE_T     service_queue_msg;

   // scatter-gather (vector) message queue
   VCHI_CONNECTION_SERVICE_QUEUE_MESSAGEV_T    service_queue_msgv;

   // Routine to dequeue a message
   VCHI_CONNECTION_SERVICE_DEQUEUE_MESSAGE_T   service_dequeue_msg;

   // Routine to peek at a message
   VCHI_CONNECTION_SERVICE_PEEK_MESSAGE_T      service_peek_msg;

   // Routine to hold a message
   VCHI_CONNECTION_SERVICE_HOLD_MESSAGE_T      service_hold_msg;

   // Routine to initialise a received message iterator
   VCHI_CONNECTION_SERVICE_LOOKAHEAD_MESSAGE_T service_look_ahead_msg;

   // Routine to release a message
   VCHI_CONNECTION_HELD_MSG_RELEASE_T          held_msg_release;

   // Routine to get information on a held message
   VCHI_CONNECTION_HELD_MSG_INFO_T             held_msg_info;

   // Routine to check for next message on iterator
   VCHI_CONNECTION_MSG_ITER_HAS_NEXT_T         msg_iter_has_next;

   // Routine to get next message on iterator
   VCHI_CONNECTION_MSG_ITER_NEXT_T             msg_iter_next;

   // Routine to remove the last message returned by iterator
   VCHI_CONNECTION_MSG_ITER_REMOVE_T           msg_iter_remove;

   // Routine to hold the last message returned by iterator
   VCHI_CONNECTION_MSG_ITER_HOLD_T             msg_iter_hold;

   // Routine to transmit bulk data
   VCHI_CONNECTION_BULK_QUEUE_TRANSMIT_T       bulk_queue_transmit;

   // Routine to receive data
   VCHI_CONNECTION_BULK_QUEUE_RECEIVE_T        bulk_queue_receive;

   // Routine to report the available servers
   VCHI_CONNECTION_SERVER_PRESENT              server_present;

   // Routine to report the number of RX slots available
   VCHI_CONNECTION_RX_SLOTS_AVAILABLE          connection_rx_slots_available;

   // Routine to report the RX slot size
   VCHI_CONNECTION_RX_SLOT_SIZE                connection_rx_slot_size;

   // Callback to indicate that the other side has added a buffer to the rx bulk DMA FIFO
   VCHI_CONNECTION_RX_BULK_BUFFER_ADDED        rx_bulk_buffer_added;

   // Callback to inform a service that a Xon or Xoff message has been received
   VCHI_CONNECTION_FLOW_CONTROL                flow_control;

   // Callback to inform a service that a server available reply message has been received
   VCHI_CONNECTION_SERVER_AVAILABLE_REPLY      server_available_reply;

   // Callback to indicate that bulk auxiliary messages have arrived
   VCHI_CONNECTION_BULK_AUX_RECEIVED           bulk_aux_received;

   // Callback to indicate that a bulk auxiliary message has been transmitted
   VCHI_CONNECTION_BULK_AUX_TRANSMITTED        bulk_aux_transmitted;

   // Callback to provide information about the connection
   VCHI_CONNECTION_INFO                        connection_info;

   // Callback to notify that peer has requested disconnect
   VCHI_CONNECTION_DISCONNECT                  disconnect;

   // Callback to notify that peer has requested power change
   VCHI_CONNECTION_POWER_CONTROL               power_control;

   // allocate memory suitably aligned for this connection
   VCHI_BUFFER_ALLOCATE                        buffer_allocate;

   // free memory allocated by buffer_allocate
   VCHI_BUFFER_FREE                            buffer_free;

};

struct vchi_connection_t {
   const VCHI_CONNECTION_API_T *api;
   VCHI_CONNECTION_STATE_T     *state;
#ifdef VCHI_COARSE_LOCKING
   struct semaphore             sem;
#endif
};


#endif /* CONNECTION_H_ */

/****************************** End of file **********************************/
