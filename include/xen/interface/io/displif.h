/******************************************************************************
 * displif.h
 *
 * Unified display device I/O interface for Xen guest OSes.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (C) 2016-2017 EPAM Systems Inc.
 *
 * Authors: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 *          Oleksandr Grytsov <oleksandr_grytsov@epam.com>
 */

#ifndef __XEN_PUBLIC_IO_DISPLIF_H__
#define __XEN_PUBLIC_IO_DISPLIF_H__

#include "ring.h"
#include "../grant_table.h"

/*
 ******************************************************************************
 *                           Protocol version
 ******************************************************************************
 */
#define XENDISPL_PROTOCOL_VERSION	"1"

/*
 ******************************************************************************
 *                  Main features provided by the protocol
 ******************************************************************************
 * This protocol aims to provide a unified protocol which fits more
 * sophisticated use-cases than a framebuffer device can handle. At the
 * moment basic functionality is supported with the intention to be extended:
 *  o multiple dynamically allocated/destroyed framebuffers
 *  o buffers of arbitrary sizes
 *  o buffer allocation at either back or front end
 *  o better configuration options including multiple display support
 *
 * Note: existing fbif can be used together with displif running at the
 * same time, e.g. on Linux one provides framebuffer and another DRM/KMS
 *
 * Note: display resolution (XenStore's "resolution" property) defines
 * visible area of the virtual display. At the same time resolution of
 * the display and frame buffers may differ: buffers can be smaller, equal
 * or bigger than the visible area. This is to enable use-cases, where backend
 * may do some post-processing of the display and frame buffers supplied,
 * e.g. those buffers can be just a part of the final composition.
 *
 ******************************************************************************
 *                        Direction of improvements
 ******************************************************************************
 * Future extensions to the existing protocol may include:
 *  o display/connector cloning
 *  o allocation of objects other than display buffers
 *  o plane/overlay support
 *  o scaling support
 *  o rotation support
 *
 ******************************************************************************
 *                  Feature and Parameter Negotiation
 ******************************************************************************
 *
 * Front->back notifications: when enqueuing a new request, sending a
 * notification can be made conditional on xendispl_req (i.e., the generic
 * hold-off mechanism provided by the ring macros). Backends must set
 * xendispl_req appropriately (e.g., using RING_FINAL_CHECK_FOR_REQUESTS()).
 *
 * Back->front notifications: when enqueuing a new response, sending a
 * notification can be made conditional on xendispl_resp (i.e., the generic
 * hold-off mechanism provided by the ring macros). Frontends must set
 * xendispl_resp appropriately (e.g., using RING_FINAL_CHECK_FOR_RESPONSES()).
 *
 * The two halves of a para-virtual display driver utilize nodes within
 * XenStore to communicate capabilities and to negotiate operating parameters.
 * This section enumerates these nodes which reside in the respective front and
 * backend portions of XenStore, following the XenBus convention.
 *
 * All data in XenStore is stored as strings. Nodes specifying numeric
 * values are encoded in decimal. Integer value ranges listed below are
 * expressed as fixed sized integer types capable of storing the conversion
 * of a properly formated node string, without loss of information.
 *
 ******************************************************************************
 *                        Example configuration
 ******************************************************************************
 *
 * Note: depending on the use-case backend can expose more display connectors
 * than the underlying HW physically has by employing SW graphics compositors
 *
 * This is an example of backend and frontend configuration:
 *
 *--------------------------------- Backend -----------------------------------
 *
 * /local/domain/0/backend/vdispl/1/0/frontend-id = "1"
 * /local/domain/0/backend/vdispl/1/0/frontend = "/local/domain/1/device/vdispl/0"
 * /local/domain/0/backend/vdispl/1/0/state = "4"
 * /local/domain/0/backend/vdispl/1/0/versions = "1,2"
 *
 *--------------------------------- Frontend ----------------------------------
 *
 * /local/domain/1/device/vdispl/0/backend-id = "0"
 * /local/domain/1/device/vdispl/0/backend = "/local/domain/0/backend/vdispl/1/0"
 * /local/domain/1/device/vdispl/0/state = "4"
 * /local/domain/1/device/vdispl/0/version = "1"
 * /local/domain/1/device/vdispl/0/be-alloc = "1"
 *
 *-------------------------- Connector 0 configuration ------------------------
 *
 * /local/domain/1/device/vdispl/0/0/resolution = "1920x1080"
 * /local/domain/1/device/vdispl/0/0/req-ring-ref = "2832"
 * /local/domain/1/device/vdispl/0/0/req-event-channel = "15"
 * /local/domain/1/device/vdispl/0/0/evt-ring-ref = "387"
 * /local/domain/1/device/vdispl/0/0/evt-event-channel = "16"
 *
 *-------------------------- Connector 1 configuration ------------------------
 *
 * /local/domain/1/device/vdispl/0/1/resolution = "800x600"
 * /local/domain/1/device/vdispl/0/1/req-ring-ref = "2833"
 * /local/domain/1/device/vdispl/0/1/req-event-channel = "17"
 * /local/domain/1/device/vdispl/0/1/evt-ring-ref = "388"
 * /local/domain/1/device/vdispl/0/1/evt-event-channel = "18"
 *
 ******************************************************************************
 *                            Backend XenBus Nodes
 ******************************************************************************
 *
 *----------------------------- Protocol version ------------------------------
 *
 * versions
 *      Values:         <string>
 *
 *      List of XENDISPL_LIST_SEPARATOR separated protocol versions supported
 *      by the backend. For example "1,2,3".
 *
 ******************************************************************************
 *                            Frontend XenBus Nodes
 ******************************************************************************
 *
 *-------------------------------- Addressing ---------------------------------
 *
 * dom-id
 *      Values:         <uint16_t>
 *
 *      Domain identifier.
 *
 * dev-id
 *      Values:         <uint16_t>
 *
 *      Device identifier.
 *
 * conn-idx
 *      Values:         <uint8_t>
 *
 *      Zero based contigous index of the connector.
 *      /local/domain/<dom-id>/device/vdispl/<dev-id>/<conn-idx>/...
 *
 *----------------------------- Protocol version ------------------------------
 *
 * version
 *      Values:         <string>
 *
 *      Protocol version, chosen among the ones supported by the backend.
 *
 *------------------------- Backend buffer allocation -------------------------
 *
 * be-alloc
 *      Values:         "0", "1"
 *
 *      If value is set to "1", then backend can be a buffer provider/allocator
 *      for this domain during XENDISPL_OP_DBUF_CREATE operation (see below
 *      for negotiation).
 *      If value is not "1" or omitted frontend must allocate buffers itself.
 *
 *----------------------------- Connector settings ----------------------------
 *
 * unique-id
 *      Values:         <string>
 *
 *      After device instance initialization each connector is assigned a
 *      unique ID, so it can be identified by the backend by this ID.
 *      This can be UUID or such.
 *
 * resolution
 *      Values:         <width, uint32_t>x<height, uint32_t>
 *
 *      Width and height of the connector in pixels separated by
 *      XENDISPL_RESOLUTION_SEPARATOR. This defines visible area of the
 *      display.
 *
 *------------------ Connector Request Transport Parameters -------------------
 *
 * This communication path is used to deliver requests from frontend to backend
 * and get the corresponding responses from backend to frontend,
 * set up per connector.
 *
 * req-event-channel
 *      Values:         <uint32_t>
 *
 *      The identifier of the Xen connector's control event channel
 *      used to signal activity in the ring buffer.
 *
 * req-ring-ref
 *      Values:         <uint32_t>
 *
 *      The Xen grant reference granting permission for the backend to map
 *      a sole page of connector's control ring buffer.
 *
 *------------------- Connector Event Transport Parameters --------------------
 *
 * This communication path is used to deliver asynchronous events from backend
 * to frontend, set up per connector.
 *
 * evt-event-channel
 *      Values:         <uint32_t>
 *
 *      The identifier of the Xen connector's event channel
 *      used to signal activity in the ring buffer.
 *
 * evt-ring-ref
 *      Values:         <uint32_t>
 *
 *      The Xen grant reference granting permission for the backend to map
 *      a sole page of connector's event ring buffer.
 */

/*
 ******************************************************************************
 *                               STATE DIAGRAMS
 ******************************************************************************
 *
 * Tool stack creates front and back state nodes with initial state
 * XenbusStateInitialising.
 * Tool stack creates and sets up frontend display configuration
 * nodes per domain.
 *
 *-------------------------------- Normal flow --------------------------------
 *
 * Front                                Back
 * =================================    =====================================
 * XenbusStateInitialising              XenbusStateInitialising
 *                                       o Query backend device identification
 *                                         data.
 *                                       o Open and validate backend device.
 *                                                |
 *                                                |
 *                                                V
 *                                      XenbusStateInitWait
 *
 * o Query frontend configuration
 * o Allocate and initialize
 *   event channels per configured
 *   connector.
 * o Publish transport parameters
 *   that will be in effect during
 *   this connection.
 *              |
 *              |
 *              V
 * XenbusStateInitialised
 *
 *                                       o Query frontend transport parameters.
 *                                       o Connect to the event channels.
 *                                                |
 *                                                |
 *                                                V
 *                                      XenbusStateConnected
 *
 *  o Create and initialize OS
 *    virtual display connectors
 *    as per configuration.
 *              |
 *              |
 *              V
 * XenbusStateConnected
 *
 *                                      XenbusStateUnknown
 *                                      XenbusStateClosed
 *                                      XenbusStateClosing
 * o Remove virtual display device
 * o Remove event channels
 *              |
 *              |
 *              V
 * XenbusStateClosed
 *
 *------------------------------- Recovery flow -------------------------------
 *
 * In case of frontend unrecoverable errors backend handles that as
 * if frontend goes into the XenbusStateClosed state.
 *
 * In case of backend unrecoverable errors frontend tries removing
 * the virtualized device. If this is possible at the moment of error,
 * then frontend goes into the XenbusStateInitialising state and is ready for
 * new connection with backend. If the virtualized device is still in use and
 * cannot be removed, then frontend goes into the XenbusStateReconfiguring state
 * until either the virtualized device is removed or backend initiates a new
 * connection. On the virtualized device removal frontend goes into the
 * XenbusStateInitialising state.
 *
 * Note on XenbusStateReconfiguring state of the frontend: if backend has
 * unrecoverable errors then frontend cannot send requests to the backend
 * and thus cannot provide functionality of the virtualized device anymore.
 * After backend is back to normal the virtualized device may still hold some
 * state: configuration in use, allocated buffers, client application state etc.
 * In most cases, this will require frontend to implement complex recovery
 * reconnect logic. Instead, by going into XenbusStateReconfiguring state,
 * frontend will make sure no new clients of the virtualized device are
 * accepted, allow existing client(s) to exit gracefully by signaling error
 * state etc.
 * Once all the clients are gone frontend can reinitialize the virtualized
 * device and get into XenbusStateInitialising state again signaling the
 * backend that a new connection can be made.
 *
 * There are multiple conditions possible under which frontend will go from
 * XenbusStateReconfiguring into XenbusStateInitialising, some of them are OS
 * specific. For example:
 * 1. The underlying OS framework may provide callbacks to signal that the last
 *    client of the virtualized device has gone and the device can be removed
 * 2. Frontend can schedule a deferred work (timer/tasklet/workqueue)
 *    to periodically check if this is the right time to re-try removal of
 *    the virtualized device.
 * 3. By any other means.
 *
 ******************************************************************************
 *                             REQUEST CODES
 ******************************************************************************
 * Request codes [0; 15] are reserved and must not be used
 */

#define XENDISPL_OP_DBUF_CREATE		0x10
#define XENDISPL_OP_DBUF_DESTROY	0x11
#define XENDISPL_OP_FB_ATTACH		0x12
#define XENDISPL_OP_FB_DETACH		0x13
#define XENDISPL_OP_SET_CONFIG		0x14
#define XENDISPL_OP_PG_FLIP		0x15

/*
 ******************************************************************************
 *                                 EVENT CODES
 ******************************************************************************
 */
#define XENDISPL_EVT_PG_FLIP		0x00

/*
 ******************************************************************************
 *               XENSTORE FIELD AND PATH NAME STRINGS, HELPERS
 ******************************************************************************
 */
#define XENDISPL_DRIVER_NAME		"vdispl"

#define XENDISPL_LIST_SEPARATOR		","
#define XENDISPL_RESOLUTION_SEPARATOR	"x"

#define XENDISPL_FIELD_BE_VERSIONS	"versions"
#define XENDISPL_FIELD_FE_VERSION	"version"
#define XENDISPL_FIELD_REQ_RING_REF	"req-ring-ref"
#define XENDISPL_FIELD_REQ_CHANNEL	"req-event-channel"
#define XENDISPL_FIELD_EVT_RING_REF	"evt-ring-ref"
#define XENDISPL_FIELD_EVT_CHANNEL	"evt-event-channel"
#define XENDISPL_FIELD_RESOLUTION	"resolution"
#define XENDISPL_FIELD_BE_ALLOC		"be-alloc"
#define XENDISPL_FIELD_UNIQUE_ID	"unique-id"

/*
 ******************************************************************************
 *                          STATUS RETURN CODES
 ******************************************************************************
 *
 * Status return code is zero on success and -XEN_EXX on failure.
 *
 ******************************************************************************
 *                              Assumptions
 ******************************************************************************
 * o usage of grant reference 0 as invalid grant reference:
 *   grant reference 0 is valid, but never exposed to a PV driver,
 *   because of the fact it is already in use/reserved by the PV console.
 * o all references in this document to page sizes must be treated
 *   as pages of size XEN_PAGE_SIZE unless otherwise noted.
 *
 ******************************************************************************
 *       Description of the protocol between frontend and backend driver
 ******************************************************************************
 *
 * The two halves of a Para-virtual display driver communicate with
 * each other using shared pages and event channels.
 * Shared page contains a ring with request/response packets.
 *
 * All reserved fields in the structures below must be 0.
 * Display buffers's cookie of value 0 is treated as invalid.
 * Framebuffer's cookie of value 0 is treated as invalid.
 *
 * For all request/response/event packets that use cookies:
 *   dbuf_cookie - uint64_t, unique to guest domain value used by the backend
 *     to map remote display buffer to its local one
 *   fb_cookie - uint64_t, unique to guest domain value used by the backend
 *     to map remote framebuffer to its local one
 *
 *---------------------------------- Requests ---------------------------------
 *
 * All requests/responses, which are not connector specific, must be sent over
 * control ring of the connector which has the index value of 0:
 *   /local/domain/<dom-id>/device/vdispl/<dev-id>/0/req-ring-ref
 *
 * All request packets have the same length (64 octets)
 * All request packets have common header:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                |    operation   |   reserved     | 4
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 8
 * +----------------+----------------+----------------+----------------+
 *   id - uint16_t, private guest value, echoed in response
 *   operation - uint8_t, operation code, XENDISPL_OP_???
 *
 * Request dbuf creation - request creation of a display buffer.
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                |_OP_DBUF_CREATE |   reserved     | 4
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 8
 * +----------------+----------------+----------------+----------------+
 * |                       dbuf_cookie low 32-bit                      | 12
 * +----------------+----------------+----------------+----------------+
 * |                       dbuf_cookie high 32-bit                     | 16
 * +----------------+----------------+----------------+----------------+
 * |                               width                               | 20
 * +----------------+----------------+----------------+----------------+
 * |                               height                              | 24
 * +----------------+----------------+----------------+----------------+
 * |                                bpp                                | 28
 * +----------------+----------------+----------------+----------------+
 * |                             buffer_sz                             | 32
 * +----------------+----------------+----------------+----------------+
 * |                               flags                               | 36
 * +----------------+----------------+----------------+----------------+
 * |                           gref_directory                          | 40
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 44
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 64
 * +----------------+----------------+----------------+----------------+
 *
 * Must be sent over control ring of the connector which has the index
 * value of 0:
 *   /local/domain/<dom-id>/device/vdispl/<dev-id>/0/req-ring-ref
 * All unused bits in flags field must be set to 0.
 *
 * An attempt to create multiple display buffers with the same dbuf_cookie is
 * an error. dbuf_cookie can be re-used after destroying the corresponding
 * display buffer.
 *
 * Width and height of the display buffers can be smaller, equal or bigger
 * than the connector's resolution. Depth/pixel format of the individual
 * buffers can differ as well.
 *
 * width - uint32_t, width in pixels
 * height - uint32_t, height in pixels
 * bpp - uint32_t, bits per pixel
 * buffer_sz - uint32_t, buffer size to be allocated, octets
 * flags - uint32_t, flags of the operation
 *   o XENDISPL_DBUF_FLG_REQ_ALLOC - if set, then backend is requested
 *     to allocate the buffer with the parameters provided in this request.
 *     Page directory is handled as follows:
 *       Frontend on request:
 *         o allocates pages for the directory (gref_directory,
 *           gref_dir_next_page(s)
 *         o grants permissions for the pages of the directory to the backend
 *         o sets gref_dir_next_page fields
 *       Backend on response:
 *         o grants permissions for the pages of the buffer allocated to
 *           the frontend
 *         o fills in page directory with grant references
 *           (gref[] in struct xendispl_page_directory)
 * gref_directory - grant_ref_t, a reference to the first shared page
 *   describing shared buffer references. At least one page exists. If shared
 *   buffer size (buffer_sz) exceeds what can be addressed by this single page,
 *   then reference to the next page must be supplied (see gref_dir_next_page
 *   below)
 */

#define XENDISPL_DBUF_FLG_REQ_ALLOC	(1 << 0)

struct xendispl_dbuf_create_req {
	uint64_t dbuf_cookie;
	uint32_t width;
	uint32_t height;
	uint32_t bpp;
	uint32_t buffer_sz;
	uint32_t flags;
	grant_ref_t gref_directory;
};

/*
 * Shared page for XENDISPL_OP_DBUF_CREATE buffer descriptor (gref_directory in
 * the request) employs a list of pages, describing all pages of the shared
 * data buffer:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |                        gref_dir_next_page                         | 4
 * +----------------+----------------+----------------+----------------+
 * |                              gref[0]                              | 8
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                              gref[i]                              | i*4+8
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             gref[N - 1]                           | N*4+8
 * +----------------+----------------+----------------+----------------+
 *
 * gref_dir_next_page - grant_ref_t, reference to the next page describing
 *   page directory. Must be 0 if there are no more pages in the list.
 * gref[i] - grant_ref_t, reference to a shared page of the buffer
 *   allocated at XENDISPL_OP_DBUF_CREATE
 *
 * Number of grant_ref_t entries in the whole page directory is not
 * passed, but instead can be calculated as:
 *   num_grefs_total = (XENDISPL_OP_DBUF_CREATE.buffer_sz + XEN_PAGE_SIZE - 1) /
 *       XEN_PAGE_SIZE
 */

struct xendispl_page_directory {
	grant_ref_t gref_dir_next_page;
	grant_ref_t gref[1]; /* Variable length */
};

/*
 * Request dbuf destruction - destroy a previously allocated display buffer:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                |_OP_DBUF_DESTROY|   reserved     | 4
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 8
 * +----------------+----------------+----------------+----------------+
 * |                       dbuf_cookie low 32-bit                      | 12
 * +----------------+----------------+----------------+----------------+
 * |                       dbuf_cookie high 32-bit                     | 16
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 20
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 64
 * +----------------+----------------+----------------+----------------+
 *
 * Must be sent over control ring of the connector which has the index
 * value of 0:
 *   /local/domain/<dom-id>/device/vdispl/<dev-id>/0/req-ring-ref
 */

struct xendispl_dbuf_destroy_req {
	uint64_t dbuf_cookie;
};

/*
 * Request framebuffer attachment - request attachment of a framebuffer to
 * previously created display buffer.
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                | _OP_FB_ATTACH  |   reserved     | 4
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 8
 * +----------------+----------------+----------------+----------------+
 * |                       dbuf_cookie low 32-bit                      | 12
 * +----------------+----------------+----------------+----------------+
 * |                       dbuf_cookie high 32-bit                     | 16
 * +----------------+----------------+----------------+----------------+
 * |                        fb_cookie low 32-bit                       | 20
 * +----------------+----------------+----------------+----------------+
 * |                        fb_cookie high 32-bit                      | 24
 * +----------------+----------------+----------------+----------------+
 * |                               width                               | 28
 * +----------------+----------------+----------------+----------------+
 * |                               height                              | 32
 * +----------------+----------------+----------------+----------------+
 * |                            pixel_format                           | 36
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 40
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 64
 * +----------------+----------------+----------------+----------------+
 *
 * Must be sent over control ring of the connector which has the index
 * value of 0:
 *   /local/domain/<dom-id>/device/vdispl/<dev-id>/0/req-ring-ref
 * Width and height can be smaller, equal or bigger than the connector's
 * resolution.
 *
 * An attempt to create multiple frame buffers with the same fb_cookie is
 * an error. fb_cookie can be re-used after destroying the corresponding
 * frame buffer.
 *
 * width - uint32_t, width in pixels
 * height - uint32_t, height in pixels
 * pixel_format - uint32_t, pixel format of the framebuffer, FOURCC code
 */

struct xendispl_fb_attach_req {
	uint64_t dbuf_cookie;
	uint64_t fb_cookie;
	uint32_t width;
	uint32_t height;
	uint32_t pixel_format;
};

/*
 * Request framebuffer detach - detach a previously
 * attached framebuffer from the display buffer in request:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                |  _OP_FB_DETACH |   reserved     | 4
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 8
 * +----------------+----------------+----------------+----------------+
 * |                        fb_cookie low 32-bit                       | 12
 * +----------------+----------------+----------------+----------------+
 * |                        fb_cookie high 32-bit                      | 16
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 20
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 64
 * +----------------+----------------+----------------+----------------+
 *
 * Must be sent over control ring of the connector which has the index
 * value of 0:
 *   /local/domain/<dom-id>/device/vdispl/<dev-id>/0/req-ring-ref
 */

struct xendispl_fb_detach_req {
	uint64_t fb_cookie;
};

/*
 * Request configuration set/reset - request to set or reset
 * the configuration/mode of the display:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                | _OP_SET_CONFIG |   reserved     | 4
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 8
 * +----------------+----------------+----------------+----------------+
 * |                        fb_cookie low 32-bit                       | 12
 * +----------------+----------------+----------------+----------------+
 * |                        fb_cookie high 32-bit                      | 16
 * +----------------+----------------+----------------+----------------+
 * |                                 x                                 | 20
 * +----------------+----------------+----------------+----------------+
 * |                                 y                                 | 24
 * +----------------+----------------+----------------+----------------+
 * |                               width                               | 28
 * +----------------+----------------+----------------+----------------+
 * |                               height                              | 32
 * +----------------+----------------+----------------+----------------+
 * |                                bpp                                | 40
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 44
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 64
 * +----------------+----------------+----------------+----------------+
 *
 * Pass all zeros to reset, otherwise command is treated as
 * configuration set.
 * Framebuffer's cookie defines which framebuffer/dbuf must be
 * displayed while enabling display (applying configuration).
 * x, y, width and height are bound by the connector's resolution and must not
 * exceed it.
 *
 * x - uint32_t, starting position in pixels by X axis
 * y - uint32_t, starting position in pixels by Y axis
 * width - uint32_t, width in pixels
 * height - uint32_t, height in pixels
 * bpp - uint32_t, bits per pixel
 */

struct xendispl_set_config_req {
	uint64_t fb_cookie;
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t bpp;
};

/*
 * Request page flip - request to flip a page identified by the framebuffer
 * cookie:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                | _OP_PG_FLIP    |   reserved     | 4
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 8
 * +----------------+----------------+----------------+----------------+
 * |                        fb_cookie low 32-bit                       | 12
 * +----------------+----------------+----------------+----------------+
 * |                        fb_cookie high 32-bit                      | 16
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 20
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 64
 * +----------------+----------------+----------------+----------------+
 */

struct xendispl_page_flip_req {
	uint64_t fb_cookie;
};

/*
 *---------------------------------- Responses --------------------------------
 *
 * All response packets have the same length (64 octets)
 *
 * All response packets have common header:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                |            reserved             | 4
 * +----------------+----------------+----------------+----------------+
 * |                              status                               | 8
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 12
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 64
 * +----------------+----------------+----------------+----------------+
 *
 * id - uint16_t, private guest value, echoed from request
 * status - int32_t, response status, zero on success and -XEN_EXX on failure
 *
 *----------------------------------- Events ----------------------------------
 *
 * Events are sent via a shared page allocated by the front and propagated by
 *   evt-event-channel/evt-ring-ref XenStore entries
 * All event packets have the same length (64 octets)
 * All event packets have common header:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                |      type      |   reserved     | 4
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 8
 * +----------------+----------------+----------------+----------------+
 *
 * id - uint16_t, event id, may be used by front
 * type - uint8_t, type of the event
 *
 *
 * Page flip complete event - event from back to front on page flip completed:
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |               id                |   _EVT_PG_FLIP |   reserved     | 4
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 8
 * +----------------+----------------+----------------+----------------+
 * |                        fb_cookie low 32-bit                       | 12
 * +----------------+----------------+----------------+----------------+
 * |                        fb_cookie high 32-bit                      | 16
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 20
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 64
 * +----------------+----------------+----------------+----------------+
 */

struct xendispl_pg_flip_evt {
	uint64_t fb_cookie;
};

struct xendispl_req {
	uint16_t id;
	uint8_t operation;
	uint8_t reserved[5];
	union {
		struct xendispl_dbuf_create_req dbuf_create;
		struct xendispl_dbuf_destroy_req dbuf_destroy;
		struct xendispl_fb_attach_req fb_attach;
		struct xendispl_fb_detach_req fb_detach;
		struct xendispl_set_config_req set_config;
		struct xendispl_page_flip_req pg_flip;
		uint8_t reserved[56];
	} op;
};

struct xendispl_resp {
	uint16_t id;
	uint8_t operation;
	uint8_t reserved;
	int32_t status;
	uint8_t reserved1[56];
};

struct xendispl_evt {
	uint16_t id;
	uint8_t type;
	uint8_t reserved[5];
	union {
		struct xendispl_pg_flip_evt pg_flip;
		uint8_t reserved[56];
	} op;
};

DEFINE_RING_TYPES(xen_displif, struct xendispl_req, struct xendispl_resp);

/*
 ******************************************************************************
 *                        Back to front events delivery
 ******************************************************************************
 * In order to deliver asynchronous events from back to front a shared page is
 * allocated by front and its granted reference propagated to back via
 * XenStore entries (evt-ring-ref/evt-event-channel).
 * This page has a common header used by both front and back to synchronize
 * access and control event's ring buffer, while back being a producer of the
 * events and front being a consumer. The rest of the page after the header
 * is used for event packets.
 *
 * Upon reception of an event(s) front may confirm its reception
 * for either each event, group of events or none.
 */

struct xendispl_event_page {
	uint32_t in_cons;
	uint32_t in_prod;
	uint8_t reserved[56];
};

#define XENDISPL_EVENT_PAGE_SIZE XEN_PAGE_SIZE
#define XENDISPL_IN_RING_OFFS (sizeof(struct xendispl_event_page))
#define XENDISPL_IN_RING_SIZE (XENDISPL_EVENT_PAGE_SIZE - XENDISPL_IN_RING_OFFS)
#define XENDISPL_IN_RING_LEN (XENDISPL_IN_RING_SIZE / sizeof(struct xendispl_evt))
#define XENDISPL_IN_RING(page) \
	((struct xendispl_evt *)((char *)(page) + XENDISPL_IN_RING_OFFS))
#define XENDISPL_IN_RING_REF(page, idx) \
	(XENDISPL_IN_RING((page))[(idx) % XENDISPL_IN_RING_LEN])

#endif /* __XEN_PUBLIC_IO_DISPLIF_H__ */
