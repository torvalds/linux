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

/* In events (backend -> frontend) */

/*
 * Frontends should ignore unknown in events.
 */

/* Pointer movement event */
#define XENKBD_TYPE_MOTION  1
/* Event type 2 currently not used */
/* Key event (includes pointer buttons) */
#define XENKBD_TYPE_KEY     3
/*
 * Pointer position event
 * Capable backend sets feature-abs-pointer in xenstore.
 * Frontend requests ot instead of XENKBD_TYPE_MOTION by setting
 * request-abs-update in xenstore.
 */
#define XENKBD_TYPE_POS     4

struct xenkbd_motion {
	uint8_t type;		/* XENKBD_TYPE_MOTION */
	int32_t rel_x;		/* relative X motion */
	int32_t rel_y;		/* relative Y motion */
	int32_t rel_z;		/* relative Z motion (wheel) */
};

struct xenkbd_key {
	uint8_t type;		/* XENKBD_TYPE_KEY */
	uint8_t pressed;	/* 1 if pressed; 0 otherwise */
	uint32_t keycode;	/* KEY_* from linux/input.h */
};

struct xenkbd_position {
	uint8_t type;		/* XENKBD_TYPE_POS */
	int32_t abs_x;		/* absolute X position (in FB pixels) */
	int32_t abs_y;		/* absolute Y position (in FB pixels) */
	int32_t rel_z;		/* relative Z motion (wheel) */
};

#define XENKBD_IN_EVENT_SIZE 40

union xenkbd_in_event {
	uint8_t type;
	struct xenkbd_motion motion;
	struct xenkbd_key key;
	struct xenkbd_position pos;
	char pad[XENKBD_IN_EVENT_SIZE];
};

/* Out events (frontend -> backend) */

/*
 * Out events may be sent only when requested by backend, and receipt
 * of an unknown out event is an error.
 * No out events currently defined.
 */

#define XENKBD_OUT_EVENT_SIZE 40

union xenkbd_out_event {
	uint8_t type;
	char pad[XENKBD_OUT_EVENT_SIZE];
};

/* shared page */

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

#endif
