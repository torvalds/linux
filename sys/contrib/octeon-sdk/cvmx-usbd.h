#ifndef __CVMX_USBD_H__
#define __CVMX_USBD_H__
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
 * "cvmx-usbd.h" defines a set of low level USB functions to help developers
 * create Octeon USB devices for various operating systems. These functions
 * provide a generic API to the Octeon USB blocks, hiding the internal hardware
 * specific operations.
 *
 * <hr>$Revision: 32636 $<hr>
 */

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum
{
    CVMX_USBD_TRANSFER_CONTROL = 0,
    CVMX_USBD_TRANSFER_ISOCHRONOUS = 1,
    CVMX_USBD_TRANSFER_BULK = 2,
    CVMX_USBD_TRANSFER_INTERRUPT = 3,
} cvmx_usbd_transfer_t;

typedef enum
{
    CVMX_USBD_SPEED_HIGH = 0,
    CVMX_USBD_SPEED_FULL = 1,
    CVMX_USBD_SPEED_LOW = 2,
} cvmx_usbd_speed_t;

typedef enum
{
    CVMX_USBD_CALLBACK_SUSPEND,
    CVMX_USBD_CALLBACK_RESET,
    CVMX_USBD_CALLBACK_ENUM_COMPLETE,
    CVMX_USBD_CALLBACK_DEVICE_SETUP,
    CVMX_USBD_CALLBACK_IN_COMPLETE,
    CVMX_USBD_CALLBACK_OUT_COMPLETE,
    __CVMX_USBD_CALLBACK_END
} cvmx_usbd_callback_t;

typedef enum
{
    CVMX_USBD_INITIALIZE_FLAGS_CLOCK_XO_XI = 1<<0,       /**< The USB port uses a 12MHz crystal as clock source
                                                            at USB_XO and USB_XI. */
    CVMX_USBD_INITIALIZE_FLAGS_CLOCK_XO_GND = 1<<1,      /**< The USB port uses 12/24/48MHz 2.5V board clock
                                                            source at USB_XO. USB_XI should be tied to GND.*/
    CVMX_USBD_INITIALIZE_FLAGS_CLOCK_AUTO = 0,           /**< Automatically determine clock type based on function
                                                             in cvmx-helper-board.c. */
    CVMX_USBD_INITIALIZE_FLAGS_CLOCK_MHZ_MASK  = 3<<3,       /**< Mask for clock speed field */
    CVMX_USBD_INITIALIZE_FLAGS_CLOCK_12MHZ = 1<<3,       /**< Speed of reference clock or crystal */
    CVMX_USBD_INITIALIZE_FLAGS_CLOCK_24MHZ = 2<<3,       /**< Speed of reference clock */
    CVMX_USBD_INITIALIZE_FLAGS_CLOCK_48MHZ = 3<<3,       /**< Speed of reference clock */
    /* Bits 3-4 used to encode the clock frequency */
    CVMX_USBD_INITIALIZE_FLAGS_DEBUG = 1<<16
} cvmx_usbd_initialize_flags_t;

typedef void (*cvmx_usbd_callback_func_t)(cvmx_usbd_callback_t reason, int endpoint_num, int bytes_transferred, void *user_data);

typedef struct
{
    int init_flags;
    int index;
    cvmx_usbd_callback_func_t callback[__CVMX_USBD_CALLBACK_END];
    void *callback_data[__CVMX_USBD_CALLBACK_END];
    struct {
        int buffer_length;
    } endpoint[16];
} cvmx_usbd_state_t;

/**
 * Initialize a USB port for use. This must be called before any
 * other access to the Octeon USB port is made. The port starts
 * off in the disabled state.
 *
 * @param usb    Pointer to an empty cvmx_usbd_state_t structure
 *               that will be populated by the initialize call.
 *               This structure is then passed to all other USB
 *               functions.
 * @param usb_port_number
 *               Which Octeon USB port to initialize.
 * @param flags  Flags to control hardware initialization. See
 *               cvmx_usbd_initialize_flags_t for the flag
 *               definitions. Some flags are mandatory.
 *
 * @return Zero or a negative on error.
 */
int cvmx_usbd_initialize(cvmx_usbd_state_t *usb, int usb_port_number,
    cvmx_usbd_initialize_flags_t flags);

/**
 * Shutdown a USB port after a call to cvmx_usbd_initialize().
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 *
 * @return Zero or a negative on error.
 */
int cvmx_usbd_shutdown(cvmx_usbd_state_t *usb);

/**
 * Enable a USB port. After this call succeeds, the USB port is
 * online and servicing requests.
 *
 * @param usb  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return Zero or negative on error.
 */
int cvmx_usbd_enable(cvmx_usbd_state_t *usb);

/**
 * Disable a USB port. After this call the USB port will not
 * generate data transfers and will not generate events.
 *
 * @param usb    USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return Zero or negative on error.
 */
int cvmx_usbd_disable(cvmx_usbd_state_t *usb);

/**
 * Register a callback function to process USB events
 *
 * @param usb       USB device state populated by
 *                  cvmx_usbd_initialize().
 * @param reason    The reason this callback should be called
 * @param func      Function to call
 * @param user_data User supplied data for the callback
 *
 * @return Zero on succes, negative on failure
 */
int cvmx_usbd_register(cvmx_usbd_state_t *usb, cvmx_usbd_callback_t reason, cvmx_usbd_callback_func_t func, void *user_data);

/**
 * Poll the USB block for status and call all needed callback
 * handlers. This function is meant to be called in the interrupt
 * handler for the USB controller. It can also be called
 * periodically in a loop for non-interrupt based operation.
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 *
 * @return Zero or negative on error.
 */
int cvmx_usbd_poll(cvmx_usbd_state_t *usb);

/**
 * Get the current USB address
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 *
 * @return The USB address
 */
int cvmx_usbd_get_address(cvmx_usbd_state_t *usb);

/**
 * Set the current USB address
 *
 * @param usb     USB device state populated by
 *                cvmx_usbd_initialize().
 * @param address Address to set
 */
void cvmx_usbd_set_address(cvmx_usbd_state_t *usb, int address);

/**
 * Get the current USB speed
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 *
 * @return The USB speed
 */
cvmx_usbd_speed_t cvmx_usbd_get_speed(cvmx_usbd_state_t *usb);

/**
 * Set the current USB speed
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param speed  The requested speed
 */
void cvmx_usbd_set_speed(cvmx_usbd_state_t *usb, cvmx_usbd_speed_t speed);

/**
 * Enable an endpoint to respond to an OUT transaction
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param endpoint_num
 *               Endpoint number to enable
 * @param transfer_type
 *               Transfer type for the endpoint
 * @param max_packet_size
 *               Maximum packet size for the endpoint
 * @param buffer Buffer to receive the data
 * @param buffer_length
 *               Length of the buffer in bytes
 *
 * @return Zero on success, negative on failure
 */
int cvmx_usbd_out_endpoint_enable(cvmx_usbd_state_t *usb,
    int endpoint_num, cvmx_usbd_transfer_t transfer_type,
    int max_packet_size, uint64_t buffer, int buffer_length);

/**
 * Disable an OUT endpoint
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param endpoint_num
 *               Endpoint number to disable
 *
 * @return Zero on success, negative on failure
 */
int cvmx_usbd_out_endpoint_disable(cvmx_usbd_state_t *usb, int endpoint_num);

/**
 * Enable an endpoint to respond to an IN transaction
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param endpoint_num
 *               Endpoint number to enable
 * @param transfer_type
 *               Transfer type for the endpoint
 * @param max_packet_size
 *               Maximum packet size for the endpoint
 * @param buffer Buffer to send
 * @param buffer_length
 *               Length of the buffer in bytes
 *
 * @return Zero on success, negative on failure
 */
int cvmx_usbd_in_endpoint_enable(cvmx_usbd_state_t *usb,
    int endpoint_num, cvmx_usbd_transfer_t transfer_type,
    int max_packet_size, uint64_t buffer, int buffer_length);

/**
 * Disable an IN endpoint
 *
 * @param usb    USB device state populated by
 *               cvmx_usbd_initialize().
 * @param endpoint_num
 *               Endpoint number to disable
 *
 * @return Zero on success, negative on failure
 */
int cvmx_usbd_in_endpoint_disable(cvmx_usbd_state_t *usb, int endpoint_num);

#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_USBD_H__ */

