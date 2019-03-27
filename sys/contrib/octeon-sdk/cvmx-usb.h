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
 * "cvmx-usb.h" defines a set of low level USB functions to help
 * developers create Octeon USB drivers for various operating
 * systems. These functions provide a generic API to the Octeon
 * USB blocks, hiding the internal hardware specific
 * operations.
 *
 * At a high level the device driver needs to:
 *
 * -# Call cvmx_usb_get_num_ports() to get the number of
 *  supported ports.
 * -# Call cvmx_usb_initialize() for each Octeon USB port.
 * -# Enable the port using cvmx_usb_enable().
 * -# Either periodically, or in an interrupt handler, call
 *  cvmx_usb_poll() to service USB events.
 * -# Manage pipes using cvmx_usb_open_pipe() and
 *  cvmx_usb_close_pipe().
 * -# Manage transfers using cvmx_usb_submit_*() and
 *  cvmx_usb_cancel*().
 * -# Shutdown USB on unload using cvmx_usb_shutdown().
 *
 * To monitor USB status changes, the device driver must use
 * cvmx_usb_register_callback() to register for events that it
 * is interested in. Below are a few hints on successfully
 * implementing a driver on top of this API.
 *
 * <h2>Initialization</h2>
 *
 * When a driver is first loaded, it is normally not necessary
 * to bring up the USB port completely. Most operating systems
 * expect to initialize and enable the port in two independent
 * steps. Normally an operating system will probe hardware,
 * initialize anything found, and then enable the hardware.
 *
 * In the probe phase you should:
 * -# Use cvmx_usb_get_num_ports() to determine the number of
 *  USB port to be supported.
 * -# Allocate space for a cvmx_usb_state_t structure for each
 *  port.
 * -# Tell the operating system about each port
 *
 * In the initialization phase you should:
 * -# Use cvmx_usb_initialize() on each port.
 * -# Do not call cvmx_usb_enable(). This leaves the USB port in
 *  the disabled state until the operating system is ready.
 *
 * Finally, in the enable phase you should:
 * -# Call cvmx_usb_enable() on the appropriate port.
 * -# Note that some operating system use a RESET instead of an
 *  enable call. To implement RESET, you should call
 *  cvmx_usb_disable() followed by cvmx_usb_enable().
 *
 * <h2>Locking</h2>
 *
 * All of the functions in the cvmx-usb API assume exclusive
 * access to the USB hardware and internal data structures. This
 * means that the driver must provide locking as necessary.
 *
 * In the single CPU state it is normally enough to disable
 * interrupts before every call to cvmx_usb*() and enable them
 * again after the call is complete. Keep in mind that it is
 * very common for the callback handlers to make additional
 * calls into cvmx-usb, so the disable/enable must be protected
 * against recursion. As an example, the Linux kernel
 * local_irq_save() and local_irq_restore() are perfect for this
 * in the non SMP case.
 *
 * In the SMP case, locking is more complicated. For SMP you not
 * only need to disable interrupts on the local core, but also
 * take a lock to make sure that another core cannot call
 * cvmx-usb.
 *
 * <h2>Port callback</h2>
 *
 * The port callback prototype needs to look as follows:
 *
 * void port_callback(cvmx_usb_state_t *usb,
 *                    cvmx_usb_callback_t reason,
 *                    cvmx_usb_complete_t status,
 *                    int pipe_handle,
 *                    int submit_handle,
 *                    int bytes_transferred,
 *                    void *user_data);
 * - @b usb is the cvmx_usb_state_t for the port.
 * - @b reason will always be
 *   CVMX_USB_CALLBACK_PORT_CHANGED.
 * - @b status will always be CVMX_USB_COMPLETE_SUCCESS.
 * - @b pipe_handle will always be -1.
 * - @b submit_handle will always be -1.
 * - @b bytes_transferred will always be 0.
 * - @b user_data is the void pointer originally passed along
 *   with the callback. Use this for any state information you
 *   need.
 *
 * The port callback will be called whenever the user plugs /
 * unplugs a device from the port. It will not be called when a
 * device is plugged / unplugged from a hub connected to the
 * root port. Normally all the callback needs to do is tell the
 * operating system to poll the root hub for status. Under
 * Linux, this is performed by calling usb_hcd_poll_rh_status().
 * In the Linux driver we use @b user_data. to pass around the
 * Linux "hcd" structure. Once the port callback completes,
 * Linux automatically calls octeon_usb_hub_status_data() which
 * uses cvmx_usb_get_status() to determine the root port status.
 *
 * <h2>Complete callback</h2>
 *
 * The completion callback prototype needs to look as follows:
 *
 * void complete_callback(cvmx_usb_state_t *usb,
 *                        cvmx_usb_callback_t reason,
 *                        cvmx_usb_complete_t status,
 *                        int pipe_handle,
 *                        int submit_handle,
 *                        int bytes_transferred,
 *                        void *user_data);
 * - @b usb is the cvmx_usb_state_t for the port.
 * - @b reason will always be
 *   CVMX_USB_CALLBACK_TRANSFER_COMPLETE.
 * - @b status will be one of the cvmx_usb_complete_t
 *   enumerations.
 * - @b pipe_handle is the handle to the pipe the transaction
 *   was originally submitted on.
 * - @b submit_handle is the handle returned by the original
 *   cvmx_usb_submit_* call.
 * - @b bytes_transferred is the number of bytes successfully
 *   transferred in the transaction. This will be zero on most
 *   error conditions.
 * - @b user_data is the void pointer originally passed along
 *   with the callback. Use this for any state information you
 *   need. For example, the Linux "urb" is stored in here in the
 *   Linux driver.
 *
 * In general your callback handler should use @b status and @b
 * bytes_transferred to tell the operating system the how the
 * transaction completed. Normally the pipe is not changed in
 * this callback.
 *
 * <h2>Canceling transactions</h2>
 *
 * When a transaction is cancelled using cvmx_usb_cancel*(), the
 * actual length of time until the complete callback is called
 * can vary greatly. It may be called before cvmx_usb_cancel*()
 * returns, or it may be called a number of usb frames in the
 * future once the hardware frees the transaction. In either of
 * these cases, the complete handler will receive
 * CVMX_USB_COMPLETE_CANCEL.
 *
 * <h2>Handling pipes</h2>
 *
 * USB "pipes" is a software construct created by this API to
 * enable the ordering of usb transactions to a device endpoint.
 * Octeon's underlying hardware doesn't have any concept
 * equivalent to "pipes". The hardware instead has eight
 * channels that can be used simultaneously to have up to eight
 * transaction in process at the same time. In order to maintain
 * ordering in a pipe, the transactions for a pipe will only be
 * active in one hardware channel at a time. From an API user's
 * perspective, this doesn't matter but it can be helpful to
 * keep this in mind when you are probing hardware while
 * debugging.
 *
 * Also keep in mind that usb transactions contain state
 * information about the previous transaction to the same
 * endpoint. Each transaction has a PID toggle that changes 0/1
 * between each sub packet. This is maintained in the pipe data
 * structures. For this reason, you generally cannot create and
 * destroy a pipe for every transaction. A sequence of
 * transaction to the same endpoint must use the same pipe.
 *
 * <h2>Root Hub</h2>
 *
 * Some operating systems view the usb root port as a normal usb
 * hub. These systems attempt to control the root hub with
 * messages similar to the usb 2.0 spec for hub control and
 * status. For these systems it may be necessary to write
 * function to decode standard usb control messages into
 * equivalent cvmx-usb API calls. As an example, the following
 * code is used under Linux for some of the basic hub control
 * messages.
 *
 * @code
 * static int octeon_usb_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue, u16 wIndex, char *buf, u16 wLength)
 * {
 *     cvmx_usb_state_t *usb = (cvmx_usb_state_t *)hcd->hcd_priv;
 *     cvmx_usb_port_status_t usb_port_status;
 *     int port_status;
 *     struct usb_hub_descriptor *desc;
 *     unsigned long flags;
 *
 *     switch (typeReq)
 *     {
 *         case ClearHubFeature:
 *             DEBUG_ROOT_HUB("OcteonUSB: ClearHubFeature\n");
 *             switch (wValue)
 *             {
 *                 case C_HUB_LOCAL_POWER:
 *                 case C_HUB_OVER_CURRENT:
 *                     // Nothing required here
 *                     break;
 *                 default:
 *                     return -EINVAL;
 *             }
 *             break;
 *         case ClearPortFeature:
 *             DEBUG_ROOT_HUB("OcteonUSB: ClearPortFeature");
 *             if (wIndex != 1)
 *             {
 *                 DEBUG_ROOT_HUB(" INVALID\n");
 *                 return -EINVAL;
 *             }
 *
 *             switch (wValue)
 *             {
 *                 case USB_PORT_FEAT_ENABLE:
 *                     DEBUG_ROOT_HUB(" ENABLE");
 *                     local_irq_save(flags);
 *                     cvmx_usb_disable(usb);
 *                     local_irq_restore(flags);
 *                     break;
 *                 case USB_PORT_FEAT_SUSPEND:
 *                     DEBUG_ROOT_HUB(" SUSPEND");
 *                     // Not supported on Octeon
 *                     break;
 *                 case USB_PORT_FEAT_POWER:
 *                     DEBUG_ROOT_HUB(" POWER");
 *                     // Not supported on Octeon
 *                     break;
 *                 case USB_PORT_FEAT_INDICATOR:
 *                     DEBUG_ROOT_HUB(" INDICATOR");
 *                     // Port inidicator not supported
 *                     break;
 *                 case USB_PORT_FEAT_C_CONNECTION:
 *                     DEBUG_ROOT_HUB(" C_CONNECTION");
 *                     // Clears drivers internal connect status change flag
 *                     cvmx_usb_set_status(usb, cvmx_usb_get_status(usb));
 *                     break;
 *                 case USB_PORT_FEAT_C_RESET:
 *                     DEBUG_ROOT_HUB(" C_RESET");
 *                     // Clears the driver's internal Port Reset Change flag
 *                     cvmx_usb_set_status(usb, cvmx_usb_get_status(usb));
 *                     break;
 *                 case USB_PORT_FEAT_C_ENABLE:
 *                     DEBUG_ROOT_HUB(" C_ENABLE");
 *                     // Clears the driver's internal Port Enable/Disable Change flag
 *                     cvmx_usb_set_status(usb, cvmx_usb_get_status(usb));
 *                     break;
 *                 case USB_PORT_FEAT_C_SUSPEND:
 *                     DEBUG_ROOT_HUB(" C_SUSPEND");
 *                     // Clears the driver's internal Port Suspend Change flag,
 *                         which is set when resume signaling on the host port is
 *                         complete
 *                     break;
 *                 case USB_PORT_FEAT_C_OVER_CURRENT:
 *                     DEBUG_ROOT_HUB(" C_OVER_CURRENT");
 *                     // Clears the driver's overcurrent Change flag
 *                     cvmx_usb_set_status(usb, cvmx_usb_get_status(usb));
 *                     break;
 *                 default:
 *                     DEBUG_ROOT_HUB(" UNKNOWN\n");
 *                     return -EINVAL;
 *             }
 *             DEBUG_ROOT_HUB("\n");
 *             break;
 *         case GetHubDescriptor:
 *             DEBUG_ROOT_HUB("OcteonUSB: GetHubDescriptor\n");
 *             desc = (struct usb_hub_descriptor *)buf;
 *             desc->bDescLength = 9;
 *             desc->bDescriptorType = 0x29;
 *             desc->bNbrPorts = 1;
 *             desc->wHubCharacteristics = 0x08;
 *             desc->bPwrOn2PwrGood = 1;
 *             desc->bHubContrCurrent = 0;
 *             desc->bitmap[0] = 0;
 *             desc->bitmap[1] = 0xff;
 *             break;
 *         case GetHubStatus:
 *             DEBUG_ROOT_HUB("OcteonUSB: GetHubStatus\n");
 *             *(__le32 *)buf = 0;
 *             break;
 *         case GetPortStatus:
 *             DEBUG_ROOT_HUB("OcteonUSB: GetPortStatus");
 *             if (wIndex != 1)
 *             {
 *                 DEBUG_ROOT_HUB(" INVALID\n");
 *                 return -EINVAL;
 *             }
 *
 *             usb_port_status = cvmx_usb_get_status(usb);
 *             port_status = 0;
 *
 *             if (usb_port_status.connect_change)
 *             {
 *                 port_status |= (1 << USB_PORT_FEAT_C_CONNECTION);
 *                 DEBUG_ROOT_HUB(" C_CONNECTION");
 *             }
 *
 *             if (usb_port_status.port_enabled)
 *             {
 *                 port_status |= (1 << USB_PORT_FEAT_C_ENABLE);
 *                 DEBUG_ROOT_HUB(" C_ENABLE");
 *             }
 *
 *             if (usb_port_status.connected)
 *             {
 *                 port_status |= (1 << USB_PORT_FEAT_CONNECTION);
 *                 DEBUG_ROOT_HUB(" CONNECTION");
 *             }
 *
 *             if (usb_port_status.port_enabled)
 *             {
 *                 port_status |= (1 << USB_PORT_FEAT_ENABLE);
 *                 DEBUG_ROOT_HUB(" ENABLE");
 *             }
 *
 *             if (usb_port_status.port_over_current)
 *             {
 *                 port_status |= (1 << USB_PORT_FEAT_OVER_CURRENT);
 *                 DEBUG_ROOT_HUB(" OVER_CURRENT");
 *             }
 *
 *             if (usb_port_status.port_powered)
 *             {
 *                 port_status |= (1 << USB_PORT_FEAT_POWER);
 *                 DEBUG_ROOT_HUB(" POWER");
 *             }
 *
 *             if (usb_port_status.port_speed == CVMX_USB_SPEED_HIGH)
 *             {
 *                 port_status |= (1 << USB_PORT_FEAT_HIGHSPEED);
 *                 DEBUG_ROOT_HUB(" HIGHSPEED");
 *             }
 *             else if (usb_port_status.port_speed == CVMX_USB_SPEED_LOW)
 *             {
 *                 port_status |= (1 << USB_PORT_FEAT_LOWSPEED);
 *                 DEBUG_ROOT_HUB(" LOWSPEED");
 *             }
 *
 *             *((__le32 *)buf) = cpu_to_le32(port_status);
 *             DEBUG_ROOT_HUB("\n");
 *             break;
 *         case SetHubFeature:
 *             DEBUG_ROOT_HUB("OcteonUSB: SetHubFeature\n");
 *             // No HUB features supported
 *             break;
 *         case SetPortFeature:
 *             DEBUG_ROOT_HUB("OcteonUSB: SetPortFeature");
 *             if (wIndex != 1)
 *             {
 *                 DEBUG_ROOT_HUB(" INVALID\n");
 *                 return -EINVAL;
 *             }
 *
 *             switch (wValue)
 *             {
 *                 case USB_PORT_FEAT_SUSPEND:
 *                     DEBUG_ROOT_HUB(" SUSPEND\n");
 *                     return -EINVAL;
 *                 case USB_PORT_FEAT_POWER:
 *                     DEBUG_ROOT_HUB(" POWER\n");
 *                     return -EINVAL;
 *                 case USB_PORT_FEAT_RESET:
 *                     DEBUG_ROOT_HUB(" RESET\n");
 *                     local_irq_save(flags);
 *                     cvmx_usb_disable(usb);
 *                     if (cvmx_usb_enable(usb))
 *                         DEBUG_ERROR("Failed to enable the port\n");
 *                     local_irq_restore(flags);
 *                     return 0;
 *                 case USB_PORT_FEAT_INDICATOR:
 *                     DEBUG_ROOT_HUB(" INDICATOR\n");
 *                     // Not supported
 *                     break;
 *                 default:
 *                     DEBUG_ROOT_HUB(" UNKNOWN\n");
 *                     return -EINVAL;
 *             }
 *             break;
 *         default:
 *             DEBUG_ROOT_HUB("OcteonUSB: Unknown root hub request\n");
 *             return -EINVAL;
 *     }
 *     return 0;
 * }
 * @endcode
 *
 * <h2>Interrupts</h2>
 *
 * If you plan on using usb interrupts, cvmx_usb_poll() must be
 * called on every usb interrupt. It will read the usb state,
 * call any needed callbacks, and schedule transactions as
 * needed. Your device driver needs only to hookup an interrupt
 * handler and call cvmx_usb_poll(). Octeon's usb port 0 causes
 * CIU bit CIU_INT*_SUM0[USB] to be set (bit 56). For port 1,
 * CIU bit CIU_INT_SUM1[USB1] is set (bit 17). How these bits
 * are turned into interrupt numbers is operating system
 * specific. For Linux, there are the convenient defines
 * OCTEON_IRQ_USB0 and OCTEON_IRQ_USB1 for the IRQ numbers.
 *
 * If you aren't using interrupts, simple call cvmx_usb_poll()
 * in your main processing loop.
 *
 * <hr>$Revision: 32636 $<hr>
 */

#ifndef __CVMX_USB_H__
#define __CVMX_USB_H__

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Enumerations representing the status of function calls.
 */
typedef enum
{
    CVMX_USB_SUCCESS = 0,           /**< There were no errors */
    CVMX_USB_INVALID_PARAM = -1,    /**< A parameter to the function was invalid */
    CVMX_USB_NO_MEMORY = -2,        /**< Insufficient resources were available for the request */
    CVMX_USB_BUSY = -3,             /**< The resource is busy and cannot service the request */
    CVMX_USB_TIMEOUT = -4,          /**< Waiting for an action timed out */
    CVMX_USB_INCORRECT_MODE = -5,   /**< The function call doesn't work in the current USB
                                         mode. This happens when host only functions are
                                         called in device mode or vice versa */
} cvmx_usb_status_t;

/**
 * Enumerations representing the possible USB device speeds
 */
typedef enum
{
    CVMX_USB_SPEED_HIGH = 0,        /**< Device is operation at 480Mbps */
    CVMX_USB_SPEED_FULL = 1,        /**< Device is operation at 12Mbps */
    CVMX_USB_SPEED_LOW = 2,         /**< Device is operation at 1.5Mbps */
} cvmx_usb_speed_t;

/**
 * Enumeration representing the possible USB transfer types.
 */
typedef enum
{
    CVMX_USB_TRANSFER_CONTROL = 0,      /**< USB transfer type control for hub and status transfers */
    CVMX_USB_TRANSFER_ISOCHRONOUS = 1,  /**< USB transfer type isochronous for low priority periodic transfers */
    CVMX_USB_TRANSFER_BULK = 2,         /**< USB transfer type bulk for large low priority transfers */
    CVMX_USB_TRANSFER_INTERRUPT = 3,    /**< USB transfer type interrupt for high priority periodic transfers */
} cvmx_usb_transfer_t;

/**
 * Enumeration of the transfer directions
 */
typedef enum
{
    CVMX_USB_DIRECTION_OUT,         /**< Data is transferring from Octeon to the device/host */
    CVMX_USB_DIRECTION_IN,          /**< Data is transferring from the device/host to Octeon */
} cvmx_usb_direction_t;

/**
 * Enumeration of all possible status codes passed to callback
 * functions.
 */
typedef enum
{
    CVMX_USB_COMPLETE_SUCCESS,      /**< The transaction / operation finished without any errors */
    CVMX_USB_COMPLETE_SHORT,        /**< FIXME: This is currently not implemented */
    CVMX_USB_COMPLETE_CANCEL,       /**< The transaction was canceled while in flight by a user call to cvmx_usb_cancel* */
    CVMX_USB_COMPLETE_ERROR,        /**< The transaction aborted with an unexpected error status */
    CVMX_USB_COMPLETE_STALL,        /**< The transaction received a USB STALL response from the device */
    CVMX_USB_COMPLETE_XACTERR,      /**< The transaction failed with an error from the device even after a number of retries */
    CVMX_USB_COMPLETE_DATATGLERR,   /**< The transaction failed with a data toggle error even after a number of retries */
    CVMX_USB_COMPLETE_BABBLEERR,    /**< The transaction failed with a babble error */
    CVMX_USB_COMPLETE_FRAMEERR,     /**< The transaction failed with a frame error even after a number of retries */
} cvmx_usb_complete_t;

/**
 * Structure returned containing the USB port status information.
 */
typedef struct
{
    uint32_t reserved           : 25;
    uint32_t port_enabled       : 1; /**< 1 = Usb port is enabled, 0 = disabled */
    uint32_t port_over_current  : 1; /**< 1 = Over current detected, 0 = Over current not detected. Octeon doesn't support over current detection */
    uint32_t port_powered       : 1; /**< 1 = Port power is being supplied to the device, 0 = power is off. Octeon doesn't support turning port power off */
    cvmx_usb_speed_t port_speed : 2; /**< Current port speed */
    uint32_t connected          : 1; /**< 1 = A device is connected to the port, 0 = No device is connected */
    uint32_t connect_change     : 1; /**< 1 = Device connected state changed since the last set status call */
} cvmx_usb_port_status_t;

/**
 * This is the structure of a Control packet header
 */
typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t request_type   : 8;  /**< Bit 7 tells the direction: 1=IN, 0=OUT */
        uint64_t request        : 8;  /**< The standard usb request to make */
        uint64_t value          : 16; /**< Value parameter for the request in little endian format */
        uint64_t index          : 16; /**< Index for the request in little endian format */
        uint64_t length         : 16; /**< Length of the data associated with this request in little endian format */
    } s;
} cvmx_usb_control_header_t;

/**
 * Descriptor for Isochronous packets
 */
typedef struct
{
    int offset;                     /**< This is the offset in bytes into the main buffer where this data is stored */
    int length;                     /**< This is the length in bytes of the data */
    cvmx_usb_complete_t status;     /**< This is the status of this individual packet transfer */
} cvmx_usb_iso_packet_t;

/**
 * Possible callback reasons for the USB API.
 */
typedef enum
{
    CVMX_USB_CALLBACK_TRANSFER_COMPLETE,
                                    /**< A callback of this type is called when a submitted transfer
                                        completes. The completion callback will be called even if the
                                        transfer fails or is canceled. The status parameter will
                                        contain details of why he callback was called. */
    CVMX_USB_CALLBACK_PORT_CHANGED, /**< The status of the port changed. For example, someone may have
                                        plugged a device in. The status parameter contains
                                        CVMX_USB_COMPLETE_SUCCESS. Use cvmx_usb_get_status() to get
                                        the new port status. */
    __CVMX_USB_CALLBACK_END         /**< Do not use. Used internally for array bounds */
} cvmx_usb_callback_t;

/**
 * USB state internal data. The contents of this structure
 * may change in future SDKs. No data in it should be referenced
 * by user's of this API.
 */
typedef struct
{
    char data[65536];
} cvmx_usb_state_t;

/**
 * USB callback functions are always of the following type.
 * The parameters are as follows:
 *      - state = USB device state populated by
 *        cvmx_usb_initialize().
 *      - reason = The cvmx_usb_callback_t used to register
 *        the callback.
 *      - status = The cvmx_usb_complete_t representing the
 *        status code of a transaction.
 *      - pipe_handle = The Pipe that caused this callback, or
 *        -1 if this callback wasn't associated with a pipe.
 *      - submit_handle = Transfer submit handle causing this
 *        callback, or -1 if this callback wasn't associated
 *        with a transfer.
 *      - Actual number of bytes transfer.
 *      - user_data = The user pointer supplied to the
 *        function cvmx_usb_submit() or
 *        cvmx_usb_register_callback() */
typedef void (*cvmx_usb_callback_func_t)(cvmx_usb_state_t *state,
                                         cvmx_usb_callback_t reason,
                                         cvmx_usb_complete_t status,
                                         int pipe_handle, int submit_handle,
                                         int bytes_transferred, void *user_data);

/**
 * Flags to pass the initialization function.
 */
typedef enum
{
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_XI = 1<<0,       /**< The USB port uses a 12MHz crystal as clock source
                                                            at USB_XO and USB_XI. */
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_XO_GND = 1<<1,      /**< The USB port uses 12/24/48MHz 2.5V board clock
                                                            source at USB_XO. USB_XI should be tied to GND.*/
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_AUTO = 0,           /**< Automatically determine clock type based on function
                                                             in cvmx-helper-board.c. */
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_MHZ_MASK  = 3<<3,       /**< Mask for clock speed field */
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_12MHZ = 1<<3,       /**< Speed of reference clock or crystal */
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_24MHZ = 2<<3,       /**< Speed of reference clock */
    CVMX_USB_INITIALIZE_FLAGS_CLOCK_48MHZ = 3<<3,       /**< Speed of reference clock */
    /* Bits 3-4 used to encode the clock frequency */
    CVMX_USB_INITIALIZE_FLAGS_NO_DMA = 1<<5,            /**< Disable DMA and used polled IO for data transfer use for the USB  */
    CVMX_USB_INITIALIZE_FLAGS_DEBUG_TRANSFERS = 1<<16,  /**< Enable extra console output for debugging USB transfers */
    CVMX_USB_INITIALIZE_FLAGS_DEBUG_CALLBACKS = 1<<17,  /**< Enable extra console output for debugging USB callbacks */
    CVMX_USB_INITIALIZE_FLAGS_DEBUG_INFO = 1<<18,       /**< Enable extra console output for USB informational data */
    CVMX_USB_INITIALIZE_FLAGS_DEBUG_CALLS = 1<<19,      /**< Enable extra console output for every function call */
    CVMX_USB_INITIALIZE_FLAGS_DEBUG_CSRS = 1<<20,       /**< Enable extra console output for every CSR access */
    CVMX_USB_INITIALIZE_FLAGS_DEBUG_ALL = ((CVMX_USB_INITIALIZE_FLAGS_DEBUG_CSRS<<1)-1) - (CVMX_USB_INITIALIZE_FLAGS_DEBUG_TRANSFERS-1),
} cvmx_usb_initialize_flags_t;

/**
 * Flags for passing when a pipe is created. Currently no flags
 * need to be passed.
 */
typedef enum
{
    CVMX_USB_PIPE_FLAGS_DEBUG_TRANSFERS = 1<<15,/**< Used to display CVMX_USB_INITIALIZE_FLAGS_DEBUG_TRANSFERS for a specific pipe only */
    __CVMX_USB_PIPE_FLAGS_OPEN = 1<<16,         /**< Used internally to determine if a pipe is open. Do not use */
    __CVMX_USB_PIPE_FLAGS_SCHEDULED = 1<<17,    /**< Used internally to determine if a pipe is actively using hardware. Do not use */
    __CVMX_USB_PIPE_FLAGS_NEED_PING = 1<<18,    /**< Used internally to determine if a high speed pipe is in the ping state. Do not use */
} cvmx_usb_pipe_flags_t;

/**
 * Return the number of USB ports supported by this Octeon
 * chip. If the chip doesn't support USB, or is not supported
 * by this API, a zero will be returned. Most Octeon chips
 * support one usb port, but some support two ports.
 * cvmx_usb_initialize() must be called on independent
 * cvmx_usb_state_t structures.
 *
 * @return Number of port, zero if usb isn't supported
 */
extern int cvmx_usb_get_num_ports(void);

/**
 * Initialize a USB port for use. This must be called before any
 * other access to the Octeon USB port is made. The port starts
 * off in the disabled state.
 *
 * @param state  Pointer to an empty cvmx_usb_state_t structure
 *               that will be populated by the initialize call.
 *               This structure is then passed to all other USB
 *               functions.
 * @param usb_port_number
 *               Which Octeon USB port to initialize.
 * @param flags  Flags to control hardware initialization. See
 *               cvmx_usb_initialize_flags_t for the flag
 *               definitions. Some flags are mandatory.
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
extern cvmx_usb_status_t cvmx_usb_initialize(cvmx_usb_state_t *state,
                                             int usb_port_number,
                                             cvmx_usb_initialize_flags_t flags);

/**
 * Shutdown a USB port after a call to cvmx_usb_initialize().
 * The port should be disabled with all pipes closed when this
 * function is called.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
extern cvmx_usb_status_t cvmx_usb_shutdown(cvmx_usb_state_t *state);

/**
 * Enable a USB port. After this call succeeds, the USB port is
 * online and servicing requests.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
extern cvmx_usb_status_t cvmx_usb_enable(cvmx_usb_state_t *state);

/**
 * Disable a USB port. After this call the USB port will not
 * generate data transfers and will not generate events.
 * Transactions in process will fail and call their
 * associated callbacks.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
extern cvmx_usb_status_t cvmx_usb_disable(cvmx_usb_state_t *state);

/**
 * Get the current state of the USB port. Use this call to
 * determine if the usb port has anything connected, is enabled,
 * or has some sort of error condition. The return value of this
 * call has "changed" bits to signal of the value of some fields
 * have changed between calls. These "changed" fields are based
 * on the last call to cvmx_usb_set_status(). In order to clear
 * them, you must update the status through cvmx_usb_set_status().
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return Port status information
 */
extern cvmx_usb_port_status_t cvmx_usb_get_status(cvmx_usb_state_t *state);

/**
 * Set the current state of the USB port. The status is used as
 * a reference for the "changed" bits returned by
 * cvmx_usb_get_status(). Other than serving as a reference, the
 * status passed to this function is not used. No fields can be
 * changed through this call.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 * @param port_status
 *               Port status to set, most like returned by cvmx_usb_get_status()
 */
extern void cvmx_usb_set_status(cvmx_usb_state_t *state, cvmx_usb_port_status_t port_status);

/**
 * Open a virtual pipe between the host and a USB device. A pipe
 * must be opened before data can be transferred between a device
 * and Octeon.
 *
 * @param state      USB device state populated by
 *                   cvmx_usb_initialize().
 * @param flags      Optional pipe flags defined in
 *                   cvmx_usb_pipe_flags_t.
 * @param device_addr
 *                   USB device address to open the pipe to
 *                   (0-127).
 * @param endpoint_num
 *                   USB endpoint number to open the pipe to
 *                   (0-15).
 * @param device_speed
 *                   The speed of the device the pipe is going
 *                   to. This must match the device's speed,
 *                   which may be different than the port speed.
 * @param max_packet The maximum packet length the device can
 *                   transmit/receive (low speed=0-8, full
 *                   speed=0-1023, high speed=0-1024). This value
 *                   comes from the standard endpoint descriptor
 *                   field wMaxPacketSize bits <10:0>.
 * @param transfer_type
 *                   The type of transfer this pipe is for.
 * @param transfer_dir
 *                   The direction the pipe is in. This is not
 *                   used for control pipes.
 * @param interval   For ISOCHRONOUS and INTERRUPT transfers,
 *                   this is how often the transfer is scheduled
 *                   for. All other transfers should specify
 *                   zero. The units are in frames (8000/sec at
 *                   high speed, 1000/sec for full speed).
 * @param multi_count
 *                   For high speed devices, this is the maximum
 *                   allowed number of packet per microframe.
 *                   Specify zero for non high speed devices. This
 *                   value comes from the standard endpoint descriptor
 *                   field wMaxPacketSize bits <12:11>.
 * @param hub_device_addr
 *                   Hub device address this device is connected
 *                   to. Devices connected directly to Octeon
 *                   use zero. This is only used when the device
 *                   is full/low speed behind a high speed hub.
 *                   The address will be of the high speed hub,
 *                   not and full speed hubs after it.
 * @param hub_port   Which port on the hub the device is
 *                   connected. Use zero for devices connected
 *                   directly to Octeon. Like hub_device_addr,
 *                   this is only used for full/low speed
 *                   devices behind a high speed hub.
 *
 * @return A non negative value is a pipe handle. Negative
 *         values are failure codes from cvmx_usb_status_t.
 */
extern int cvmx_usb_open_pipe(cvmx_usb_state_t *state,
                              cvmx_usb_pipe_flags_t flags,
                              int device_addr, int endpoint_num,
                              cvmx_usb_speed_t device_speed, int max_packet,
                              cvmx_usb_transfer_t transfer_type,
                              cvmx_usb_direction_t transfer_dir, int interval,
                              int multi_count, int hub_device_addr,
                              int hub_port);

/**
 * Call to submit a USB Bulk transfer to a pipe.
 *
 * @param state     USB device state populated by
 *                  cvmx_usb_initialize().
 * @param pipe_handle
 *                  Handle to the pipe for the transfer.
 * @param buffer    Physical address of the data buffer in
 *                  memory. Note that this is NOT A POINTER, but
 *                  the full 64bit physical address of the
 *                  buffer. This may be zero if buffer_length is
 *                  zero.
 * @param buffer_length
 *                  Length of buffer in bytes.
 * @param callback  Function to call when this transaction
 *                  completes. If the return value of this
 *                  function isn't an error, then this function
 *                  is guaranteed to be called when the
 *                  transaction completes. If this parameter is
 *                  NULL, then the generic callback registered
 *                  through cvmx_usb_register_callback is
 *                  called. If both are NULL, then there is no
 *                  way to know when a transaction completes.
 * @param user_data User supplied data returned when the
 *                  callback is called. This is only used if
 *                  callback in not NULL.
 *
 * @return A submitted transaction handle or negative on
 *         failure. Negative values are failure codes from
 *         cvmx_usb_status_t.
 */
extern int cvmx_usb_submit_bulk(cvmx_usb_state_t *state, int pipe_handle,
                                uint64_t buffer, int buffer_length,
                                cvmx_usb_callback_func_t callback,
                                void *user_data);

/**
 * Call to submit a USB Interrupt transfer to a pipe.
 *
 * @param state     USB device state populated by
 *                  cvmx_usb_initialize().
 * @param pipe_handle
 *                  Handle to the pipe for the transfer.
 * @param buffer    Physical address of the data buffer in
 *                  memory. Note that this is NOT A POINTER, but
 *                  the full 64bit physical address of the
 *                  buffer. This may be zero if buffer_length is
 *                  zero.
 * @param buffer_length
 *                  Length of buffer in bytes.
 * @param callback  Function to call when this transaction
 *                  completes. If the return value of this
 *                  function isn't an error, then this function
 *                  is guaranteed to be called when the
 *                  transaction completes. If this parameter is
 *                  NULL, then the generic callback registered
 *                  through cvmx_usb_register_callback is
 *                  called. If both are NULL, then there is no
 *                  way to know when a transaction completes.
 * @param user_data User supplied data returned when the
 *                  callback is called. This is only used if
 *                  callback in not NULL.
 *
 * @return A submitted transaction handle or negative on
 *         failure. Negative values are failure codes from
 *         cvmx_usb_status_t.
 */
extern int cvmx_usb_submit_interrupt(cvmx_usb_state_t *state, int pipe_handle,
                                     uint64_t buffer, int buffer_length,
                                     cvmx_usb_callback_func_t callback,
                                     void *user_data);

/**
 * Call to submit a USB Control transfer to a pipe.
 *
 * @param state     USB device state populated by
 *                  cvmx_usb_initialize().
 * @param pipe_handle
 *                  Handle to the pipe for the transfer.
 * @param control_header
 *                  USB 8 byte control header physical address.
 *                  Note that this is NOT A POINTER, but the
 *                  full 64bit physical address of the buffer.
 * @param buffer    Physical address of the data buffer in
 *                  memory. Note that this is NOT A POINTER, but
 *                  the full 64bit physical address of the
 *                  buffer. This may be zero if buffer_length is
 *                  zero.
 * @param buffer_length
 *                  Length of buffer in bytes.
 * @param callback  Function to call when this transaction
 *                  completes. If the return value of this
 *                  function isn't an error, then this function
 *                  is guaranteed to be called when the
 *                  transaction completes. If this parameter is
 *                  NULL, then the generic callback registered
 *                  through cvmx_usb_register_callback is
 *                  called. If both are NULL, then there is no
 *                  way to know when a transaction completes.
 * @param user_data User supplied data returned when the
 *                  callback is called. This is only used if
 *                  callback in not NULL.
 *
 * @return A submitted transaction handle or negative on
 *         failure. Negative values are failure codes from
 *         cvmx_usb_status_t.
 */
extern int cvmx_usb_submit_control(cvmx_usb_state_t *state, int pipe_handle,
                                   uint64_t control_header,
                                   uint64_t buffer, int buffer_length,
                                   cvmx_usb_callback_func_t callback,
                                   void *user_data);

/**
 * Flags to pass the cvmx_usb_submit_isochronous() function.
 */
typedef enum
{
    CVMX_USB_ISOCHRONOUS_FLAGS_ALLOW_SHORT = 1<<0,  /**< Do not return an error if a transfer is less than the maximum packet size of the device */
    CVMX_USB_ISOCHRONOUS_FLAGS_ASAP = 1<<1,         /**< Schedule the transaction as soon as possible */
} cvmx_usb_isochronous_flags_t;

/**
 * Call to submit a USB Isochronous transfer to a pipe.
 *
 * @param state     USB device state populated by
 *                  cvmx_usb_initialize().
 * @param pipe_handle
 *                  Handle to the pipe for the transfer.
 * @param start_frame
 *                  Number of frames into the future to schedule
 *                  this transaction.
 * @param flags     Flags to control the transfer. See
 *                  cvmx_usb_isochronous_flags_t for the flag
 *                  definitions.
 * @param number_packets
 *                  Number of sequential packets to transfer.
 *                  "packets" is a pointer to an array of this
 *                  many packet structures.
 * @param packets   Description of each transfer packet as
 *                  defined by cvmx_usb_iso_packet_t. The array
 *                  pointed to here must stay valid until the
 *                  complete callback is called.
 * @param buffer    Physical address of the data buffer in
 *                  memory. Note that this is NOT A POINTER, but
 *                  the full 64bit physical address of the
 *                  buffer. This may be zero if buffer_length is
 *                  zero.
 * @param buffer_length
 *                  Length of buffer in bytes.
 * @param callback  Function to call when this transaction
 *                  completes. If the return value of this
 *                  function isn't an error, then this function
 *                  is guaranteed to be called when the
 *                  transaction completes. If this parameter is
 *                  NULL, then the generic callback registered
 *                  through cvmx_usb_register_callback is
 *                  called. If both are NULL, then there is no
 *                  way to know when a transaction completes.
 * @param user_data User supplied data returned when the
 *                  callback is called. This is only used if
 *                  callback in not NULL.
 *
 * @return A submitted transaction handle or negative on
 *         failure. Negative values are failure codes from
 *         cvmx_usb_status_t.
 */
extern int cvmx_usb_submit_isochronous(cvmx_usb_state_t *state, int pipe_handle,
                                       int start_frame, int flags,
                                       int number_packets,
                                       cvmx_usb_iso_packet_t packets[],
                                       uint64_t buffer, int buffer_length,
                                       cvmx_usb_callback_func_t callback,
                                       void *user_data);

/**
 * Cancel one outstanding request in a pipe. Canceling a request
 * can fail if the transaction has already completed before cancel
 * is called. Even after a successful cancel call, it may take
 * a frame or two for the cvmx_usb_poll() function to call the
 * associated callback.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 * @param pipe_handle
 *               Pipe handle to cancel requests in.
 * @param submit_handle
 *               Handle to transaction to cancel, returned by the submit function.
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
extern cvmx_usb_status_t cvmx_usb_cancel(cvmx_usb_state_t *state,
                                         int pipe_handle, int submit_handle);


/**
 * Cancel all outstanding requests in a pipe. Logically all this
 * does is call cvmx_usb_cancel() in a loop.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 * @param pipe_handle
 *               Pipe handle to cancel requests in.
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
extern cvmx_usb_status_t cvmx_usb_cancel_all(cvmx_usb_state_t *state,
                                             int pipe_handle);

/**
 * Close a pipe created with cvmx_usb_open_pipe().
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 * @param pipe_handle
 *               Pipe handle to close.
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t. CVMX_USB_BUSY is returned if the
 *         pipe has outstanding transfers.
 */
extern cvmx_usb_status_t cvmx_usb_close_pipe(cvmx_usb_state_t *state,
                                             int pipe_handle);

/**
 * Register a function to be called when various USB events occur.
 *
 * @param state     USB device state populated by
 *                  cvmx_usb_initialize().
 * @param reason    Which event to register for.
 * @param callback  Function to call when the event occurs.
 * @param user_data User data parameter to the function.
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
extern cvmx_usb_status_t cvmx_usb_register_callback(cvmx_usb_state_t *state,
                                                    cvmx_usb_callback_t reason,
                                                    cvmx_usb_callback_func_t callback,
                                                    void *user_data);

/**
 * Get the current USB protocol level frame number. The frame
 * number is always in the range of 0-0x7ff.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return USB frame number
 */
extern int cvmx_usb_get_frame_number(cvmx_usb_state_t *state);

/**
 * Poll the USB block for status and call all needed callback
 * handlers. This function is meant to be called in the interrupt
 * handler for the USB controller. It can also be called
 * periodically in a loop for non-interrupt based operation.
 *
 * @param state  USB device state populated by
 *               cvmx_usb_initialize().
 *
 * @return CVMX_USB_SUCCESS or a negative error code defined in
 *         cvmx_usb_status_t.
 */
extern cvmx_usb_status_t cvmx_usb_poll(cvmx_usb_state_t *state);

/*
 * The FreeBSD host driver uses these functions to manipulate the toggle to deal
 * more easily with endpoint management.
 */
extern void cvmx_usb_set_toggle(cvmx_usb_state_t *state, int endpoint_num, int toggle);
extern int cvmx_usb_get_toggle(cvmx_usb_state_t *state, int endpoint_num);

#ifdef	__cplusplus
}
#endif

#endif  /* __CVMX_USB_H__ */
