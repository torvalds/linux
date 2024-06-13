/* SPDX-License-Identifier: MIT */

#ifndef __XEN_PUBLIC_IO_XEN_PVCALLS_H__
#define __XEN_PUBLIC_IO_XEN_PVCALLS_H__

#include <linux/net.h>
#include <xen/interface/io/ring.h>
#include <xen/interface/grant_table.h>

/* "1" means socket, connect, release, bind, listen, accept and poll */
#define XENBUS_FUNCTIONS_CALLS "1"

/*
 * See docs/misc/pvcalls.markdown in xen.git for the full specification:
 * https://xenbits.xen.org/docs/unstable/misc/pvcalls.html
 */
struct pvcalls_data_intf {
    RING_IDX in_cons, in_prod, in_error;

    uint8_t pad1[52];

    RING_IDX out_cons, out_prod, out_error;

    uint8_t pad2[52];

    RING_IDX ring_order;
    grant_ref_t ref[];
};
DEFINE_XEN_FLEX_RING(pvcalls);

#define PVCALLS_SOCKET         0
#define PVCALLS_CONNECT        1
#define PVCALLS_RELEASE        2
#define PVCALLS_BIND           3
#define PVCALLS_LISTEN         4
#define PVCALLS_ACCEPT         5
#define PVCALLS_POLL           6

struct xen_pvcalls_request {
    uint32_t req_id; /* private to guest, echoed in response */
    uint32_t cmd;    /* command to execute */
    union {
        struct xen_pvcalls_socket {
            uint64_t id;
            uint32_t domain;
            uint32_t type;
            uint32_t protocol;
        } socket;
        struct xen_pvcalls_connect {
            uint64_t id;
            uint8_t addr[28];
            uint32_t len;
            uint32_t flags;
            grant_ref_t ref;
            uint32_t evtchn;
        } connect;
        struct xen_pvcalls_release {
            uint64_t id;
            uint8_t reuse;
        } release;
        struct xen_pvcalls_bind {
            uint64_t id;
            uint8_t addr[28];
            uint32_t len;
        } bind;
        struct xen_pvcalls_listen {
            uint64_t id;
            uint32_t backlog;
        } listen;
        struct xen_pvcalls_accept {
            uint64_t id;
            uint64_t id_new;
            grant_ref_t ref;
            uint32_t evtchn;
        } accept;
        struct xen_pvcalls_poll {
            uint64_t id;
        } poll;
        /* dummy member to force sizeof(struct xen_pvcalls_request)
         * to match across archs */
        struct xen_pvcalls_dummy {
            uint8_t dummy[56];
        } dummy;
    } u;
};

struct xen_pvcalls_response {
    uint32_t req_id;
    uint32_t cmd;
    int32_t ret;
    uint32_t pad;
    union {
        struct _xen_pvcalls_socket {
            uint64_t id;
        } socket;
        struct _xen_pvcalls_connect {
            uint64_t id;
        } connect;
        struct _xen_pvcalls_release {
            uint64_t id;
        } release;
        struct _xen_pvcalls_bind {
            uint64_t id;
        } bind;
        struct _xen_pvcalls_listen {
            uint64_t id;
        } listen;
        struct _xen_pvcalls_accept {
            uint64_t id;
        } accept;
        struct _xen_pvcalls_poll {
            uint64_t id;
        } poll;
        struct _xen_pvcalls_dummy {
            uint8_t dummy[8];
        } dummy;
    } u;
};

DEFINE_RING_TYPES(xen_pvcalls, struct xen_pvcalls_request,
                  struct xen_pvcalls_response);

#endif
