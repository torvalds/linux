/*
 * fbif.h -- Xen virtual frame buffer device
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

#ifndef __XEN_PUBLIC_IO_FBIF_H__
#define __XEN_PUBLIC_IO_FBIF_H__

/* Out events (frontend -> backend) */

/*
 * Out events may be sent only when requested by backend, and receipt
 * of an unknown out event is an error.
 */

/* Event type 1 currently not used */
/*
 * Framebuffer update notification event
 * Capable frontend sets feature-update in xenstore.
 * Backend requests it by setting request-update in xenstore.
 */
#define XENFB_TYPE_UPDATE 2

struct xenfb_update
{
    uint8_t type;    /* XENFB_TYPE_UPDATE */
    int32_t x;      /* source x */
    int32_t y;      /* source y */
    int32_t width;  /* rect width */
    int32_t height; /* rect height */
};

/*
 * Framebuffer resize notification event
 * Capable backend sets feature-resize in xenstore.
 */
#define XENFB_TYPE_RESIZE 3

struct xenfb_resize
{
    uint8_t type;    /* XENFB_TYPE_RESIZE */
    int32_t width;   /* width in pixels */
    int32_t height;  /* height in pixels */
    int32_t stride;  /* stride in bytes */
    int32_t depth;   /* depth in bits */
    int32_t offset;  /* offset of the framebuffer in bytes */
};

#define XENFB_OUT_EVENT_SIZE 40

union xenfb_out_event
{
    uint8_t type;
    struct xenfb_update update;
    struct xenfb_resize resize;
    char pad[XENFB_OUT_EVENT_SIZE];
};

/* In events (backend -> frontend) */

/*
 * Frontends should ignore unknown in events.
 */

/*
 * Framebuffer refresh period advice
 * Backend sends it to advise the frontend their preferred period of
 * refresh.  Frontends that keep the framebuffer constantly up-to-date
 * just ignore it.  Frontends that use the advice should immediately
 * refresh the framebuffer (and send an update notification event if
 * those have been requested), then use the update frequency to guide
 * their periodical refreshs.
 */
#define XENFB_TYPE_REFRESH_PERIOD 1
#define XENFB_NO_REFRESH 0

struct xenfb_refresh_period
{
    uint8_t type;    /* XENFB_TYPE_UPDATE_PERIOD */
    uint32_t period; /* period of refresh, in ms,
                      * XENFB_NO_REFRESH if no refresh is needed */
};

#define XENFB_IN_EVENT_SIZE 40

union xenfb_in_event
{
    uint8_t type;
    struct xenfb_refresh_period refresh_period;
    char pad[XENFB_IN_EVENT_SIZE];
};

/* shared page */

#define XENFB_IN_RING_SIZE 1024
#define XENFB_IN_RING_LEN (XENFB_IN_RING_SIZE / XENFB_IN_EVENT_SIZE)
#define XENFB_IN_RING_OFFS 1024
#define XENFB_IN_RING(page) \
    ((union xenfb_in_event *)((char *)(page) + XENFB_IN_RING_OFFS))
#define XENFB_IN_RING_REF(page, idx) \
    (XENFB_IN_RING((page))[(idx) % XENFB_IN_RING_LEN])

#define XENFB_OUT_RING_SIZE 2048
#define XENFB_OUT_RING_LEN (XENFB_OUT_RING_SIZE / XENFB_OUT_EVENT_SIZE)
#define XENFB_OUT_RING_OFFS (XENFB_IN_RING_OFFS + XENFB_IN_RING_SIZE)
#define XENFB_OUT_RING(page) \
    ((union xenfb_out_event *)((char *)(page) + XENFB_OUT_RING_OFFS))
#define XENFB_OUT_RING_REF(page, idx) \
    (XENFB_OUT_RING((page))[(idx) % XENFB_OUT_RING_LEN])

struct xenfb_page
{
    uint32_t in_cons, in_prod;
    uint32_t out_cons, out_prod;

    int32_t width;          /* the width of the framebuffer (in pixels) */
    int32_t height;         /* the height of the framebuffer (in pixels) */
    uint32_t line_length;   /* the length of a row of pixels (in bytes) */
    uint32_t mem_length;    /* the length of the framebuffer (in bytes) */
    uint8_t depth;          /* the depth of a pixel (in bits) */

    /*
     * Framebuffer page directory
     *
     * Each directory page holds PAGE_SIZE / sizeof(*pd)
     * framebuffer pages, and can thus map up to PAGE_SIZE *
     * PAGE_SIZE / sizeof(*pd) bytes.  With PAGE_SIZE == 4096 and
     * sizeof(unsigned long) == 4/8, that's 4 Megs 32 bit and 2 Megs
     * 64 bit.  256 directories give enough room for a 512 Meg
     * framebuffer with a max resolution of 12,800x10,240.  Should
     * be enough for a while with room leftover for expansion.
     */
    unsigned long pd[256];
};

/*
 * Wart: xenkbd needs to know default resolution.  Put it here until a
 * better solution is found, but don't leak it to the backend.
 */
#ifdef __KERNEL__
#define XENFB_WIDTH 800
#define XENFB_HEIGHT 600
#define XENFB_DEPTH 32
#endif

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
