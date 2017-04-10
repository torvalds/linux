/*
 * kbdif.h -- Xen virtual keyboard/mouse
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
 * Copyright (C) 2005 Anthony Liguori <aliguori@us.ibm.com>
 * Copyright (C) 2006 Red Hat, Inc., Markus Armbruster <armbru@redhat.com>
 */

#ifndef __XEN_PUBLIC_IO_KBDIF_H__
#define __XEN_PUBLIC_IO_KBDIF_H__

/*
 *****************************************************************************
 *                     Feature and Parameter Negotiation
 *****************************************************************************
 *
 * The two halves of a para-virtual driver utilize nodes within
 * XenStore to communicate capabilities and to negotiate operating parameters.
 * This section enumerates these nodes which reside in the respective front and
 * backend portions of XenStore, following XenBus convention.
 *
 * All data in XenStore is stored as strings.  Nodes specifying numeric
 * values are encoded in decimal. Integer value ranges listed below are
 * expressed as fixed sized integer types capable of storing the conversion
 * of a properly formated node string, without loss of information.
 *
 *****************************************************************************
 *                            Backend XenBus Nodes
 *****************************************************************************
 *
 *---------------------------- Features supported ----------------------------
 *
 * Capable backend advertises supported features by publishing
 * corresponding entries in XenStore and puts 1 as the value of the entry.
 * If a feature is not supported then 0 must be set or feature entry omitted.
 *
 * feature-abs-pointer
 *      Values:         <uint>
 *
 *      Backends, which support reporting of absolute coordinates for pointer
 *      device should set this to 1.
 *
 *------------------------- Pointer Device Parameters ------------------------
 *
 * width
 *      Values:         <uint>
 *
 *      Maximum X coordinate (width) to be used by the frontend
 *      while reporting input events, pixels, [0; UINT32_MAX].
 *
 * height
 *      Values:         <uint>
 *
 *      Maximum Y coordinate (height) to be used by the frontend
 *      while reporting input events, pixels, [0; UINT32_MAX].
 *
 *****************************************************************************
 *                            Frontend XenBus Nodes
 *****************************************************************************
 *
 *------------------------------ Feature request -----------------------------
 *
 * Capable frontend requests features from backend via setting corresponding
 * entries to 1 in XenStore. Requests for features not advertised as supported
 * by the backend have no effect.
 *
 * request-abs-pointer
 *      Values:         <uint>
 *
 *      Request backend to report absolute pointer coordinates
 *      (XENKBD_TYPE_POS) instead of relative ones (XENKBD_TYPE_MOTION).
 *
 *----------------------- Request Transport Parameters -----------------------
 *
 * event-channel
 *      Values:         <uint>
 *
 *      The identifier of the Xen event channel used to signal activity
 *      in the ring buffer.
 *
 * page-gref
 *      Values:         <uint>
 *
 *      The Xen grant reference granting permission for the backend to map
 *      a sole page in a single page sized event ring buffer.
 *
 * page-ref
 *      Values:         <uint>
 *
 *      OBSOLETE, not recommended for use.
 *      PFN of the shared page.
 */

/*
 * EVENT CODES.
 */

#define XENKBD_TYPE_MOTION		1
#define XENKBD_TYPE_RESERVED		2
#define XENKBD_TYPE_KEY			3
#define XENKBD_TYPE_POS			4

/*
 * CONSTANTS, XENSTORE FIELD AND PATH NAME STRINGS, HELPERS.
 */

#define XENKBD_DRIVER_NAME		"vkbd"

#define XENKBD_FIELD_FEAT_ABS_POINTER	"feature-abs-pointer"
#define XENKBD_FIELD_REQ_ABS_POINTER	"request-abs-pointer"
#define XENKBD_FIELD_RING_GREF		"page-gref"
#define XENKBD_FIELD_EVT_CHANNEL	"event-channel"
#define XENKBD_FIELD_WIDTH		"width"
#define XENKBD_FIELD_HEIGHT		"height"

/* OBSOLETE, not recommended for use */
#define XENKBD_FIELD_RING_REF		"page-ref"

/*
 *****************************************************************************
 * Description of the protocol between frontend and backend driver.
 *****************************************************************************
 *
 * The two halves of a Para-virtual driver communicate with
 * each other using a shared page and an event channel.
 * Shared page contains a ring with event structures.
 *
 * All reserved fields in the structures below must be 0.
 *
 *****************************************************************************
 *                           Backend to frontend events
 *****************************************************************************
 *
 * Frontends should ignore unknown in events.
 * All event packets have the same length (40 octets)
 * All event packets have common header:
 *
 *          0         octet
 * +-----------------+
 * |       type      |
 * +-----------------+
 * type - uint8_t, event code, XENKBD_TYPE_???
 *
 *
 * Pointer relative movement event
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |  _TYPE_MOTION  |                     reserved                     | 4
 * +----------------+----------------+----------------+----------------+
 * |                               rel_x                               | 8
 * +----------------+----------------+----------------+----------------+
 * |                               rel_y                               | 12
 * +----------------+----------------+----------------+----------------+
 * |                               rel_z                               | 16
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 20
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 40
 * +----------------+----------------+----------------+----------------+
 *
 * rel_x - int32_t, relative X motion
 * rel_y - int32_t, relative Y motion
 * rel_z - int32_t, relative Z motion (wheel)
 */

struct xenkbd_motion {
	uint8_t type;
	int32_t rel_x;
	int32_t rel_y;
	int32_t rel_z;
};

/*
 * Key event (includes pointer buttons)
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |  _TYPE_KEY     |     pressed    |            reserved             | 4
 * +----------------+----------------+----------------+----------------+
 * |                              keycode                              | 8
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 12
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 40
 * +----------------+----------------+----------------+----------------+
 *
 * pressed - uint8_t, 1 if pressed; 0 otherwise
 * keycode - uint32_t, KEY_* from linux/input.h
 */

struct xenkbd_key {
	uint8_t type;
	uint8_t pressed;
	uint32_t keycode;
};

/*
 * Pointer absolute position event
 *         0                1                 2               3        octet
 * +----------------+----------------+----------------+----------------+
 * |  _TYPE_POS     |                     reserved                     | 4
 * +----------------+----------------+----------------+----------------+
 * |                               abs_x                               | 8
 * +----------------+----------------+----------------+----------------+
 * |                               abs_y                               | 12
 * +----------------+----------------+----------------+----------------+
 * |                               rel_z                               | 16
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 20
 * +----------------+----------------+----------------+----------------+
 * |/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/|
 * +----------------+----------------+----------------+----------------+
 * |                             reserved                              | 40
 * +----------------+----------------+----------------+----------------+
 *
 * abs_x - int32_t, absolute X position (in FB pixels)
 * abs_y - int32_t, absolute Y position (in FB pixels)
 * rel_z - int32_t, relative Z motion (wheel)
 */

struct xenkbd_position {
	uint8_t type;
	int32_t abs_x;
	int32_t abs_y;
	int32_t rel_z;
};

#define XENKBD_IN_EVENT_SIZE 40

union xenkbd_in_event {
	uint8_t type;
	struct xenkbd_motion motion;
	struct xenkbd_key key;
	struct xenkbd_position pos;
	char pad[XENKBD_IN_EVENT_SIZE];
};

/*
 *****************************************************************************
 *                            Frontend to backend events
 *****************************************************************************
 *
 * Out events may be sent only when requested by backend, and receipt
 * of an unknown out event is an error.
 * No out events currently defined.

 * All event packets have the same length (40 octets)
 * All event packets have common header:
 *          0         octet
 * +-----------------+
 * |       type      |
 * +-----------------+
 * type - uint8_t, event code
 */

#define XENKBD_OUT_EVENT_SIZE 40

union xenkbd_out_event {
	uint8_t type;
	char pad[XENKBD_OUT_EVENT_SIZE];
};

/*
 *****************************************************************************
 *                            Shared page
 *****************************************************************************
 */

#define XENKBD_IN_RING_SIZE 2048
#define XENKBD_IN_RING_LEN (XENKBD_IN_RING_SIZE / XENKBD_IN_EVENT_SIZE)
#define XENKBD_IN_RING_OFFS 1024
#define XENKBD_IN_RING(page) \
	((union xenkbd_in_event *)((char *)(page) + XENKBD_IN_RING_OFFS))
#define XENKBD_IN_RING_REF(page, idx) \
	(XENKBD_IN_RING((page))[(idx) % XENKBD_IN_RING_LEN])

#define XENKBD_OUT_RING_SIZE 1024
#define XENKBD_OUT_RING_LEN (XENKBD_OUT_RING_SIZE / XENKBD_OUT_EVENT_SIZE)
#define XENKBD_OUT_RING_OFFS (XENKBD_IN_RING_OFFS + XENKBD_IN_RING_SIZE)
#define XENKBD_OUT_RING(page) \
	((union xenkbd_out_event *)((char *)(page) + XENKBD_OUT_RING_OFFS))
#define XENKBD_OUT_RING_REF(page, idx) \
	(XENKBD_OUT_RING((page))[(idx) % XENKBD_OUT_RING_LEN])

struct xenkbd_page {
	uint32_t in_cons, in_prod;
	uint32_t out_cons, out_prod;
};

#endif /* __XEN_PUBLIC_IO_KBDIF_H__ */
