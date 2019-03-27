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
 * Fixes and workaround for Octeon chip errata. This file
 * contains functions called by cvmx-helper to workaround known
 * chip errata. For the most part, code doesn't need to call
 * these functions directly.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-jtag.h>
#include <asm/octeon/cvmx-pko.h>
#include <asm/octeon/cvmx-asxx-defs.h>
#include <asm/octeon/cvmx-gmxx-defs.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#endif

#include "cvmx.h"

#include "cvmx-fpa.h"
#include "cvmx-pip.h"
#include "cvmx-pko.h"
#include "cvmx-ipd.h"
#include "cvmx-gmx.h"
#include "cvmx-spi.h"
#include "cvmx-pow.h"
#include "cvmx-sysinfo.h"
#include "cvmx-helper.h"
#include "cvmx-helper-jtag.h"
#endif


#ifdef CVMX_ENABLE_PKO_FUNCTIONS


/**
 * @INTERNAL
 * Function to adjust internal IPD pointer alignments
 *
 * @return 0 on success
 *         !0 on failure
 */
int __cvmx_helper_errata_fix_ipd_ptr_alignment(void)
{
#define FIX_IPD_FIRST_BUFF_PAYLOAD_BYTES     (CVMX_FPA_PACKET_POOL_SIZE-8-CVMX_HELPER_FIRST_MBUFF_SKIP)
#define FIX_IPD_NON_FIRST_BUFF_PAYLOAD_BYTES (CVMX_FPA_PACKET_POOL_SIZE-8-CVMX_HELPER_NOT_FIRST_MBUFF_SKIP)
#define FIX_IPD_OUTPORT 0
#define INTERFACE(port) (port >> 4) /* Ports 0-15 are interface 0, 16-31 are interface 1 */
#define INDEX(port) (port & 0xf)
    uint64_t *p64;
    cvmx_pko_command_word0_t    pko_command;
    cvmx_buf_ptr_t              g_buffer, pkt_buffer;
    cvmx_wqe_t *work;
    int size, num_segs = 0, wqe_pcnt, pkt_pcnt;
    cvmx_gmxx_prtx_cfg_t gmx_cfg;
    int retry_cnt;
    int retry_loop_cnt;
    int i;
    cvmx_helper_link_info_t link_info;

    /* Save values for restore at end */
    uint64_t prtx_cfg = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)));
    uint64_t tx_ptr_en = cvmx_read_csr(CVMX_ASXX_TX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)));
    uint64_t rx_ptr_en = cvmx_read_csr(CVMX_ASXX_RX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)));
    uint64_t rxx_jabber = cvmx_read_csr(CVMX_GMXX_RXX_JABBER(INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)));
    uint64_t frame_max = cvmx_read_csr(CVMX_GMXX_RXX_FRM_MAX(INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)));

    /* Configure port to gig FDX as required for loopback mode */
    cvmx_helper_rgmii_internal_loopback(FIX_IPD_OUTPORT);

    /* Disable reception on all ports so if traffic is present it will not interfere. */
    cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)), 0);

    cvmx_wait(100000000ull);

    for (retry_loop_cnt = 0;retry_loop_cnt < 10;retry_loop_cnt++)
    {
        retry_cnt = 100000;
        wqe_pcnt = cvmx_read_csr(CVMX_IPD_PTR_COUNT);
        pkt_pcnt = (wqe_pcnt >> 7) & 0x7f;
        wqe_pcnt &= 0x7f;

        num_segs = (2 + pkt_pcnt - wqe_pcnt) & 3;

        if (num_segs == 0)
            goto fix_ipd_exit;

        num_segs += 1;

        size = FIX_IPD_FIRST_BUFF_PAYLOAD_BYTES + ((num_segs-1)*FIX_IPD_NON_FIRST_BUFF_PAYLOAD_BYTES) -
            (FIX_IPD_NON_FIRST_BUFF_PAYLOAD_BYTES / 2);

        cvmx_write_csr(CVMX_ASXX_PRT_LOOP(INTERFACE(FIX_IPD_OUTPORT)), 1 << INDEX(FIX_IPD_OUTPORT));
        CVMX_SYNC;

        g_buffer.u64 = 0;
        g_buffer.s.addr = cvmx_ptr_to_phys(cvmx_fpa_alloc(CVMX_FPA_WQE_POOL));
        if (g_buffer.s.addr == 0) {
            cvmx_dprintf("WARNING: FIX_IPD_PTR_ALIGNMENT buffer allocation failure.\n");
            goto fix_ipd_exit;
        }

        g_buffer.s.pool = CVMX_FPA_WQE_POOL;
        g_buffer.s.size = num_segs;

        pkt_buffer.u64 = 0;
        pkt_buffer.s.addr = cvmx_ptr_to_phys(cvmx_fpa_alloc(CVMX_FPA_PACKET_POOL));
        if (pkt_buffer.s.addr == 0) {
            cvmx_dprintf("WARNING: FIX_IPD_PTR_ALIGNMENT buffer allocation failure.\n");
            goto fix_ipd_exit;
        }
        pkt_buffer.s.i = 1;
        pkt_buffer.s.pool = CVMX_FPA_PACKET_POOL;
        pkt_buffer.s.size = FIX_IPD_FIRST_BUFF_PAYLOAD_BYTES;

        p64 = (uint64_t*) cvmx_phys_to_ptr(pkt_buffer.s.addr);
        p64[0] = 0xffffffffffff0000ull;
        p64[1] = 0x08004510ull;
        p64[2] = ((uint64_t)(size-14) << 48) | 0x5ae740004000ull;
        p64[3] = 0x3a5fc0a81073c0a8ull;

        for (i=0;i<num_segs;i++)
        {
            if (i>0)
                pkt_buffer.s.size = FIX_IPD_NON_FIRST_BUFF_PAYLOAD_BYTES;

            if (i==(num_segs-1))
                pkt_buffer.s.i = 0;

            *(uint64_t*)cvmx_phys_to_ptr(g_buffer.s.addr + 8*i) = pkt_buffer.u64;
        }

        /* Build the PKO command */
        pko_command.u64 = 0;
        pko_command.s.segs = num_segs;
        pko_command.s.total_bytes = size;
        pko_command.s.dontfree = 0;
        pko_command.s.gather = 1;

        gmx_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)));
        gmx_cfg.s.en = 1;
        cvmx_write_csr(CVMX_GMXX_PRTX_CFG(INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)), gmx_cfg.u64);
        cvmx_write_csr(CVMX_ASXX_TX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)), 1 << INDEX(FIX_IPD_OUTPORT));
        cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)), 1 << INDEX(FIX_IPD_OUTPORT));

        cvmx_write_csr(CVMX_GMXX_RXX_JABBER(INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)), 65392-14-4);
        cvmx_write_csr(CVMX_GMXX_RXX_FRM_MAX(INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)), 65392-14-4);

        cvmx_pko_send_packet_prepare(FIX_IPD_OUTPORT, cvmx_pko_get_base_queue(FIX_IPD_OUTPORT), CVMX_PKO_LOCK_CMD_QUEUE);
        cvmx_pko_send_packet_finish(FIX_IPD_OUTPORT, cvmx_pko_get_base_queue(FIX_IPD_OUTPORT), pko_command, g_buffer, CVMX_PKO_LOCK_CMD_QUEUE);

        CVMX_SYNC;

        do {
            work = cvmx_pow_work_request_sync(CVMX_POW_WAIT);
            retry_cnt--;
        } while ((work == NULL) && (retry_cnt > 0));

        if (!retry_cnt)
            cvmx_dprintf("WARNING: FIX_IPD_PTR_ALIGNMENT get_work() timeout occurred.\n");


        /* Free packet */
        if (work)
            cvmx_helper_free_packet_data(work);
    }

fix_ipd_exit:

    /* Return CSR configs to saved values */
    cvmx_write_csr(CVMX_GMXX_PRTX_CFG(INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)), prtx_cfg);
    cvmx_write_csr(CVMX_ASXX_TX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)), tx_ptr_en);
    cvmx_write_csr(CVMX_ASXX_RX_PRT_EN(INTERFACE(FIX_IPD_OUTPORT)), rx_ptr_en);
    cvmx_write_csr(CVMX_GMXX_RXX_JABBER(INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)), rxx_jabber);
    cvmx_write_csr(CVMX_GMXX_RXX_FRM_MAX(INDEX(FIX_IPD_OUTPORT), INTERFACE(FIX_IPD_OUTPORT)), frame_max);
    cvmx_write_csr(CVMX_ASXX_PRT_LOOP(INTERFACE(FIX_IPD_OUTPORT)), 0);
    link_info.u64 = 0;  /* Set link to down so autonegotiation will set it up again */
    cvmx_helper_link_set(FIX_IPD_OUTPORT, link_info);

    /* Bring the link back up as autonegotiation is not done in user applications. */
    cvmx_helper_link_autoconf(FIX_IPD_OUTPORT);

    CVMX_SYNC;
    if (num_segs)
        cvmx_dprintf("WARNING: FIX_IPD_PTR_ALIGNMENT failed.\n");

    return(!!num_segs);

}


/**
 * This function needs to be called on all Octeon chips with
 * errata PKI-100.
 *
 * The Size field is 8 too large in WQE and next pointers
 *
 *  The Size field generated by IPD is 8 larger than it should
 *  be. The Size field is <55:40> of both:
 *      - WORD3 in the work queue entry, and
 *      - the next buffer pointer (which precedes the packet data
 *        in each buffer).
 *
 * @param work   Work queue entry to fix
 * @return Zero on success. Negative on failure
 */
int cvmx_helper_fix_ipd_packet_chain(cvmx_wqe_t *work)
{
    uint64_t number_buffers = work->word2.s.bufs;

    /* We only need to do this if the work has buffers */
    if (number_buffers)
    {
        cvmx_buf_ptr_t buffer_ptr = work->packet_ptr;
        /* Check for errata PKI-100 */
        if ( (buffer_ptr.s.pool == 0) && (((uint64_t)buffer_ptr.s.size +
                 ((uint64_t)buffer_ptr.s.back << 7) + ((uint64_t)buffer_ptr.s.addr & 0x7F))
                 != (CVMX_FPA_PACKET_POOL_SIZE+8))) {
            /* fix is not needed */
            return 0;
        }
        /* Decrement the work packet pointer */
        buffer_ptr.s.size -= 8;
        work->packet_ptr = buffer_ptr;

        /* Now loop through decrementing the size for each additional buffer */
        while (--number_buffers)
        {
            /* Chain pointers are 8 bytes before the data */
            cvmx_buf_ptr_t *ptr = (cvmx_buf_ptr_t*)cvmx_phys_to_ptr(buffer_ptr.s.addr - 8);
            buffer_ptr = *ptr;
            buffer_ptr.s.size -= 8;
            *ptr = buffer_ptr;
        }
    }
    /* Make sure that these write go out before other operations such as FPA frees */
    CVMX_SYNCWS;
    return 0;
}

#endif /* CVMX_ENABLE_PKO_FUNCTIONS */


/**
 * Due to errata G-720, the 2nd order CDR circuit on CN52XX pass
 * 1 doesn't work properly. The following code disables 2nd order
 * CDR for the specified QLM.
 *
 * @param qlm    QLM to disable 2nd order CDR for.
 */
void __cvmx_helper_errata_qlm_disable_2nd_order_cdr(int qlm)
{
    int lane;
    /* Apply the workaround only once. */
    cvmx_ciu_qlm_jtgd_t qlm_jtgd;
    qlm_jtgd.u64 = cvmx_read_csr(CVMX_CIU_QLM_JTGD);
    if (qlm_jtgd.s.select != 0)
        return;

    cvmx_helper_qlm_jtag_init();
    /* We need to load all four lanes of the QLM, a total of 1072 bits */
    for (lane=0; lane<4; lane++)
    {
        /* Each lane has 268 bits. We need to set cfg_cdr_incx<67:64>=3 and
            cfg_cdr_secord<77>=1. All other bits are zero. Bits go in LSB
            first, so start off with the zeros for bits <63:0> */
        cvmx_helper_qlm_jtag_shift_zeros(qlm, 63 - 0 + 1);
        /* cfg_cdr_incx<67:64>=3 */
        cvmx_helper_qlm_jtag_shift(qlm, 67 - 64 + 1, 3);
        /* Zeros for bits <76:68> */
        cvmx_helper_qlm_jtag_shift_zeros(qlm, 76 - 68 + 1);
        /* cfg_cdr_secord<77>=1 */
        cvmx_helper_qlm_jtag_shift(qlm, 77 - 77 + 1, 1);
        /* Zeros for bits <267:78> */
        cvmx_helper_qlm_jtag_shift_zeros(qlm, 267 - 78 + 1);
    }
    cvmx_helper_qlm_jtag_update(qlm);
}
