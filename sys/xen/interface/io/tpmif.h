/******************************************************************************
 * tpmif.h
 *
 * TPM I/O interface for Xen guest OSes.
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
 * Copyright (c) 2005, IBM Corporation
 *
 * Author: Stefan Berger, stefanb@us.ibm.com
 * Grant table support: Mahadevan Gomathisankaran
 *
 * This code has been derived from tools/libxc/xen/io/netif.h
 *
 * Copyright (c) 2003-2004, Keir Fraser
 */

#ifndef __XEN_PUBLIC_IO_TPMIF_H__
#define __XEN_PUBLIC_IO_TPMIF_H__

#include "../grant_table.h"

struct tpmif_tx_request {
    unsigned long addr;   /* Machine address of packet.   */
    grant_ref_t ref;      /* grant table access reference */
    uint16_t unused;
    uint16_t size;        /* Packet size in bytes.        */
};
typedef struct tpmif_tx_request tpmif_tx_request_t;

/*
 * The TPMIF_TX_RING_SIZE defines the number of pages the
 * front-end and backend can exchange (= size of array).
 */
typedef uint32_t TPMIF_RING_IDX;

#define TPMIF_TX_RING_SIZE 1

/* This structure must fit in a memory page. */

struct tpmif_ring {
    struct tpmif_tx_request req;
};
typedef struct tpmif_ring tpmif_ring_t;

struct tpmif_tx_interface {
    struct tpmif_ring ring[TPMIF_TX_RING_SIZE];
};
typedef struct tpmif_tx_interface tpmif_tx_interface_t;

/******************************************************************************
 * TPM I/O interface for Xen guest OSes, v2
 *
 * Author: Daniel De Graaf <dgdegra@tycho.nsa.gov>
 *
 * This protocol emulates the request/response behavior of a TPM using a Xen
 * shared memory interface. All interaction with the TPM is at the direction
 * of the frontend, since a TPM (hardware or virtual) is a passive device -
 * the backend only processes commands as requested by the frontend.
 *
 * The frontend sends a request to the TPM by populating the shared page with
 * the request packet, changing the state to TPMIF_STATE_SUBMIT, and sending
 * and event channel notification. When the backend is finished, it will set
 * the state to TPMIF_STATE_FINISH and send an event channel notification.
 *
 * In order to allow long-running commands to be canceled, the frontend can
 * at any time change the state to TPMIF_STATE_CANCEL and send a notification.
 * The TPM can either finish the command (changing state to TPMIF_STATE_FINISH)
 * or can cancel the command and change the state to TPMIF_STATE_IDLE. The TPM
 * can also change the state to TPMIF_STATE_IDLE instead of TPMIF_STATE_FINISH
 * if another reason for cancellation is required - for example, a physical
 * TPM may cancel a command if the interface is seized by another locality.
 *
 * The TPM command format is defined by the TCG, and is available at
 * http://www.trustedcomputinggroup.org/resources/tpm_main_specification
 */

enum tpmif_state {
    TPMIF_STATE_IDLE,        /* no contents / vTPM idle / cancel complete */
    TPMIF_STATE_SUBMIT,      /* request ready / vTPM working */
    TPMIF_STATE_FINISH,      /* response ready / vTPM idle */
    TPMIF_STATE_CANCEL,      /* cancel requested / vTPM working */
};
/* Note: The backend should only change state to IDLE or FINISH, while the
 * frontend should only change to SUBMIT or CANCEL. Status changes do not need
 * to use atomic operations.
 */


/* The shared page for vTPM request/response packets looks like:
 *
 *  Offset               Contents
 *  =================================================
 *  0                    struct tpmif_shared_page
 *  16                   [optional] List of grant IDs
 *  16+4*nr_extra_pages  TPM packet data
 *
 * If the TPM packet data extends beyond the end of a single page, the grant IDs
 * defined in extra_pages are used as if they were mapped immediately following
 * the primary shared page. The grants are allocated by the frontend and mapped
 * by the backend. Before sending a request spanning multiple pages, the
 * frontend should verify that the TPM supports such large requests by querying
 * the TPM_CAP_PROP_INPUT_BUFFER property from the TPM.
 */
struct tpmif_shared_page {
    uint32_t length;         /* request/response length in bytes */

    uint8_t state;           /* enum tpmif_state */
    uint8_t locality;        /* for the current request */
    uint8_t pad;             /* should be zero */

    uint8_t nr_extra_pages;  /* extra pages for long packets; may be zero */
    uint32_t extra_pages[0]; /* grant IDs; length is actually nr_extra_pages */
};
typedef struct tpmif_shared_page tpmif_shared_page_t;

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
