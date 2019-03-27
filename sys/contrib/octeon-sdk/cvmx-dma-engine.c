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
 * Interface to the PCI / PCIe DMA engines. These are only avialable
 * on chips with PCI / PCIe.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/octeon-model.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-cmd-queue.h>
#include <asm/octeon/cvmx-dma-engine.h>
#include <asm/octeon/octeon-feature.h>
#include <asm/octeon/cvmx-npi-defs.h>
#include <asm/octeon/cvmx-npei-defs.h>
#include <asm/octeon/cvmx-dpi-defs.h>
#include <asm/octeon/cvmx-pexp-defs.h>
#include <asm/octeon/cvmx-helper-cfg.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#endif
#include "cvmx.h"
#include "cvmx-cmd-queue.h"
#include "cvmx-dma-engine.h"
#include "cvmx-helper-cfg.h"
#endif

#ifdef CVMX_ENABLE_PKO_FUNCTIONS

/**
 * Return the number of DMA engimes supported by this chip
 *
 * @return Number of DMA engines
 */
int cvmx_dma_engine_get_num(void)
{
    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        if (OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X))
            return 4;
        else
            return 5;
    }
    else if (octeon_has_feature(OCTEON_FEATURE_PCIE))
        return 8;
    else
        return 2;
}

/**
 * Initialize the DMA engines for use
 *
 * @return Zero on success, negative on failure
 */
int cvmx_dma_engine_initialize(void)
{
    int engine;

    for (engine=0; engine < cvmx_dma_engine_get_num(); engine++)
    {
        cvmx_cmd_queue_result_t result;
        result = cvmx_cmd_queue_initialize(CVMX_CMD_QUEUE_DMA(engine),
                                           0, CVMX_FPA_OUTPUT_BUFFER_POOL,
                                           CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE);
        if (result != CVMX_CMD_QUEUE_SUCCESS)
            return -1;
        if (octeon_has_feature(OCTEON_FEATURE_NPEI))
        {
            cvmx_npei_dmax_ibuff_saddr_t dmax_ibuff_saddr;
            dmax_ibuff_saddr.u64 = 0;
            dmax_ibuff_saddr.s.saddr = cvmx_ptr_to_phys(cvmx_cmd_queue_buffer(CVMX_CMD_QUEUE_DMA(engine))) >> 7;
            cvmx_write_csr(CVMX_PEXP_NPEI_DMAX_IBUFF_SADDR(engine), dmax_ibuff_saddr.u64);
        }
        else if (octeon_has_feature(OCTEON_FEATURE_PCIE))
        {
            cvmx_dpi_dmax_ibuff_saddr_t dpi_dmax_ibuff_saddr;
            dpi_dmax_ibuff_saddr.u64 = 0;
            dpi_dmax_ibuff_saddr.s.csize = CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE/8;
            dpi_dmax_ibuff_saddr.s.saddr = cvmx_ptr_to_phys(cvmx_cmd_queue_buffer(CVMX_CMD_QUEUE_DMA(engine))) >> 7;
            cvmx_write_csr(CVMX_DPI_DMAX_IBUFF_SADDR(engine), dpi_dmax_ibuff_saddr.u64);
        }
        else
        {
            uint64_t address = cvmx_ptr_to_phys(cvmx_cmd_queue_buffer(CVMX_CMD_QUEUE_DMA(engine)));
            if (engine)
                cvmx_write_csr(CVMX_NPI_HIGHP_IBUFF_SADDR, address);
            else
                cvmx_write_csr(CVMX_NPI_LOWP_IBUFF_SADDR, address);
        }
    }

    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        cvmx_npei_dma_control_t dma_control;
        dma_control.u64 = 0;
        if (cvmx_dma_engine_get_num() >= 5)
            dma_control.s.dma4_enb = 1;
        dma_control.s.dma3_enb = 1;
        dma_control.s.dma2_enb = 1;
        dma_control.s.dma1_enb = 1;
        dma_control.s.dma0_enb = 1;
        dma_control.s.o_mode = 1; /* Pull NS and RO from this register, not the pointers */
        //dma_control.s.dwb_denb = 1;
        //dma_control.s.dwb_ichk = CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE/128;
        dma_control.s.fpa_que = CVMX_FPA_OUTPUT_BUFFER_POOL;
        dma_control.s.csize = CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE/8;
        cvmx_write_csr(CVMX_PEXP_NPEI_DMA_CONTROL, dma_control.u64);
        /* As a workaround for errata PCIE-811 we only allow a single
            outstanding DMA read over PCIe at a time. This limits performance,
            but works in all cases. If you need higher performance, remove
            this code and implement the more complicated workaround documented
            in the errata. This only affects CN56XX pass 2.0 chips */
        if (OCTEON_IS_MODEL(OCTEON_CN56XX_PASS2_0))
        {
            cvmx_npei_dma_pcie_req_num_t pcie_req_num;
            pcie_req_num.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DMA_PCIE_REQ_NUM);
            pcie_req_num.s.dma_cnt = 1;
            cvmx_write_csr(CVMX_PEXP_NPEI_DMA_PCIE_REQ_NUM, pcie_req_num.u64);
        }
    }
    else if (octeon_has_feature(OCTEON_FEATURE_PCIE))
    {
        cvmx_dpi_engx_buf_t dpi_engx_buf;
        cvmx_dpi_dma_engx_en_t dpi_dma_engx_en;
        cvmx_dpi_dma_control_t dma_control;
        cvmx_dpi_ctl_t dpi_ctl;

        /* Give engine 0-4 1KB, and 5 3KB. This gives the packet engines better
            performance. Total must not exceed 8KB */
        dpi_engx_buf.u64 = 0;
        dpi_engx_buf.s.blks = 2;
        cvmx_write_csr(CVMX_DPI_ENGX_BUF(0), dpi_engx_buf.u64);
        cvmx_write_csr(CVMX_DPI_ENGX_BUF(1), dpi_engx_buf.u64);
        cvmx_write_csr(CVMX_DPI_ENGX_BUF(2), dpi_engx_buf.u64);
        cvmx_write_csr(CVMX_DPI_ENGX_BUF(3), dpi_engx_buf.u64);
        cvmx_write_csr(CVMX_DPI_ENGX_BUF(4), dpi_engx_buf.u64);
        dpi_engx_buf.s.blks = 6;
        cvmx_write_csr(CVMX_DPI_ENGX_BUF(5), dpi_engx_buf.u64);

        dma_control.u64 = cvmx_read_csr(CVMX_DPI_DMA_CONTROL);
        dma_control.s.pkt_hp = 1;
        dma_control.s.pkt_en = 1;
        dma_control.s.dma_enb = 0x1f;
        dma_control.s.dwb_denb = cvmx_helper_cfg_opt_get(CVMX_HELPER_CFG_OPT_USE_DWB);
        dma_control.s.dwb_ichk = CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE/128;
        dma_control.s.fpa_que = CVMX_FPA_OUTPUT_BUFFER_POOL;
        dma_control.s.o_mode = 1;
        cvmx_write_csr(CVMX_DPI_DMA_CONTROL, dma_control.u64);
        /* When dma_control[pkt_en] = 1, engine 5 is used for packets and is not
           available for DMA. */
        dpi_dma_engx_en.u64 = cvmx_read_csr(CVMX_DPI_DMA_ENGX_EN(5));
        dpi_dma_engx_en.s.qen = 0;
        cvmx_write_csr(CVMX_DPI_DMA_ENGX_EN(5), dpi_dma_engx_en.u64);
        dpi_ctl.u64 = cvmx_read_csr(CVMX_DPI_CTL);
        dpi_ctl.s.en = 1;
        cvmx_write_csr(CVMX_DPI_CTL, dpi_ctl.u64);
    }
    else
    {
        cvmx_npi_dma_control_t dma_control;
        dma_control.u64 = 0;
        //dma_control.s.dwb_denb = 1;
        //dma_control.s.dwb_ichk = CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE/128;
        dma_control.s.o_add1 = 1;
        dma_control.s.fpa_que = CVMX_FPA_OUTPUT_BUFFER_POOL;
        dma_control.s.hp_enb = 1;
        dma_control.s.lp_enb = 1;
        dma_control.s.csize = CVMX_FPA_OUTPUT_BUFFER_POOL_SIZE/8;
        cvmx_write_csr(CVMX_NPI_DMA_CONTROL, dma_control.u64);
    }

    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_dma_engine_initialize);
#endif

/**
 * Shutdown all DMA engines. The engines must be idle when this
 * function is called.
 *
 * @return Zero on success, negative on failure
 */
int cvmx_dma_engine_shutdown(void)
{
    int engine;

    for (engine=0; engine < cvmx_dma_engine_get_num(); engine++)
    {
        if (cvmx_cmd_queue_length(CVMX_CMD_QUEUE_DMA(engine)))
        {
            cvmx_dprintf("ERROR: cvmx_dma_engine_shutdown: Engine not idle.\n");
            return -1;
        }
    }

    if (octeon_has_feature(OCTEON_FEATURE_NPEI))
    {
        cvmx_npei_dma_control_t dma_control;
        dma_control.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DMA_CONTROL);
        if (cvmx_dma_engine_get_num() >= 5)
            dma_control.s.dma4_enb = 0;
        dma_control.s.dma3_enb = 0;
        dma_control.s.dma2_enb = 0;
        dma_control.s.dma1_enb = 0;
        dma_control.s.dma0_enb = 0;
        cvmx_write_csr(CVMX_PEXP_NPEI_DMA_CONTROL, dma_control.u64);
        /* Make sure the disable completes */
        cvmx_read_csr(CVMX_PEXP_NPEI_DMA_CONTROL);
    }
    else if (octeon_has_feature(OCTEON_FEATURE_PCIE))
    {
        cvmx_dpi_dma_control_t dma_control;
        dma_control.u64 = cvmx_read_csr(CVMX_DPI_DMA_CONTROL);
        dma_control.s.dma_enb = 0;
        cvmx_write_csr(CVMX_DPI_DMA_CONTROL, dma_control.u64);
        /* Make sure the disable completes */
        cvmx_read_csr(CVMX_DPI_DMA_CONTROL);
    }
    else
    {
        cvmx_npi_dma_control_t dma_control;
        dma_control.u64 = cvmx_read_csr(CVMX_NPI_DMA_CONTROL);
        dma_control.s.hp_enb = 0;
        dma_control.s.lp_enb = 0;
        cvmx_write_csr(CVMX_NPI_DMA_CONTROL, dma_control.u64);
        /* Make sure the disable completes */
        cvmx_read_csr(CVMX_NPI_DMA_CONTROL);
    }

    for (engine=0; engine < cvmx_dma_engine_get_num(); engine++)
    {
        cvmx_cmd_queue_shutdown(CVMX_CMD_QUEUE_DMA(engine));
        if (octeon_has_feature(OCTEON_FEATURE_NPEI))
            cvmx_write_csr(CVMX_PEXP_NPEI_DMAX_IBUFF_SADDR(engine), 0);
        else if (octeon_has_feature(OCTEON_FEATURE_PCIE))
            cvmx_write_csr(CVMX_DPI_DMAX_IBUFF_SADDR(engine), 0);
        else
        {
            if (engine)
                cvmx_write_csr(CVMX_NPI_HIGHP_IBUFF_SADDR, 0);
            else
                cvmx_write_csr(CVMX_NPI_LOWP_IBUFF_SADDR, 0);
        }
    }

    return 0;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_dma_engine_shutdown);
#endif

/**
 * Submit a series of DMA command to the DMA engines.
 *
 * @param engine  Engine to submit to (0 to cvmx_dma_engine_get_num()-1)
 * @param header  Command header
 * @param num_buffers
 *                The number of data pointers
 * @param buffers Command data pointers
 *
 * @return Zero on success, negative on failure
 */
int cvmx_dma_engine_submit(int engine, cvmx_dma_engine_header_t header, int num_buffers, cvmx_dma_engine_buffer_t buffers[])
{
    cvmx_cmd_queue_result_t result;
    int cmd_count = 1;
    uint64_t cmds[num_buffers + 1];

    if (OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X))
    {
        /* Check for Errata PCIe-604 */
        if ((header.s.nfst > 11) || (header.s.nlst > 11) || (header.s.nfst + header.s.nlst > 15))
        {
            cvmx_dprintf("DMA engine submit too large\n");
            return -1;
        }
    }

    cmds[0] = header.u64;
    while (num_buffers--)
    {
        cmds[cmd_count++] = buffers->u64;
        buffers++;
    }

    /* Due to errata PCIE-13315, it is necessary to have the queue lock while we
        ring the doorbell for the DMA engines. This prevents doorbells from
        possibly arriving out of order with respect to the command queue
        entries */
    __cvmx_cmd_queue_lock(CVMX_CMD_QUEUE_DMA(engine), __cvmx_cmd_queue_get_state(CVMX_CMD_QUEUE_DMA(engine)));
    result = cvmx_cmd_queue_write(CVMX_CMD_QUEUE_DMA(engine), 0, cmd_count, cmds);
    /* This SYNCWS is needed since the command queue didn't do locking, which
        normally implies the SYNCWS. This one makes sure the command queue
        updates make it to L2 before we ring the doorbell */
    CVMX_SYNCWS;
    /* A syncw isn't needed here since the command queue did one as part of the queue unlock */
    if (cvmx_likely(result == CVMX_CMD_QUEUE_SUCCESS))
    {
        if (octeon_has_feature(OCTEON_FEATURE_NPEI))
        {
            /* DMA doorbells are 32bit writes in little endian space. This means we need to xor the address with 4 */
            cvmx_write64_uint32(CVMX_PEXP_NPEI_DMAX_DBELL(engine)^4, cmd_count);
        }
        else if (octeon_has_feature(OCTEON_FEATURE_PCIE))
            cvmx_write_csr(CVMX_DPI_DMAX_DBELL(engine), cmd_count);
        else
        {
            if (engine)
                cvmx_write_csr(CVMX_NPI_HIGHP_DBELL, cmd_count);
            else
                cvmx_write_csr(CVMX_NPI_LOWP_DBELL, cmd_count);
        }
    }
    /* Here is the unlock for the above errata workaround */
    __cvmx_cmd_queue_unlock(__cvmx_cmd_queue_get_state(CVMX_CMD_QUEUE_DMA(engine)));
    return result;
}


/**
 * @INTERNAL
 * Function used by cvmx_dma_engine_transfer() to build the
 * internal address list.
 *
 * @param buffers Location to store the list
 * @param address Address to build list for
 * @param size    Length of the memory pointed to by address
 *
 * @return Number of internal pointer chunks created
 */
static inline int __cvmx_dma_engine_build_internal_pointers(cvmx_dma_engine_buffer_t *buffers, uint64_t address, int size)
{
    int segments = 0;
    while (size)
    {
        /* Each internal chunk can contain a maximum of 8191 bytes */
        int chunk = size;
        if (chunk > 8191)
            chunk = 8191;
        buffers[segments].u64 = 0;
        buffers[segments].internal.size = chunk;
        buffers[segments].internal.addr = address;
        address += chunk;
        size -= chunk;
        segments++;
    }
    return segments;
}


/**
 * @INTERNAL
 * Function used by cvmx_dma_engine_transfer() to build the PCI / PCIe address
 * list.
 * @param buffers Location to store the list
 * @param address Address to build list for
 * @param size    Length of the memory pointed to by address
 *
 * @return Number of PCI / PCIe address chunks created. The number of words used
 *         will be segments + (segments-1)/4 + 1.
 */
static inline int __cvmx_dma_engine_build_external_pointers(cvmx_dma_engine_buffer_t *buffers, uint64_t address, int size)
{
    const int MAX_SIZE = 65535;
    int segments = 0;
    while (size)
    {
        /* Each block of 4 PCI / PCIe pointers uses one dword for lengths followed by
            up to 4 addresses. This then repeats if more data is needed */
        buffers[0].u64 = 0;
        if (size <= MAX_SIZE)
        {
            /* Only one more segment needed */
            buffers[0].pcie_length.len0 = size;
            buffers[1].u64 = address;
            segments++;
            break;
        }
        else if (size <= MAX_SIZE * 2)
        {
            /* Two more segments needed */
            buffers[0].pcie_length.len0 = MAX_SIZE;
            buffers[0].pcie_length.len1 = size - MAX_SIZE;
            buffers[1].u64 = address;
            address += MAX_SIZE;
            buffers[2].u64 = address;
            segments+=2;
            break;
        }
        else if (size <= MAX_SIZE * 3)
        {
            /* Three more segments needed */
            buffers[0].pcie_length.len0 = MAX_SIZE;
            buffers[0].pcie_length.len1 = MAX_SIZE;
            buffers[0].pcie_length.len2 = size - MAX_SIZE * 2;
            buffers[1].u64 = address;
            address += MAX_SIZE;
            buffers[2].u64 = address;
            address += MAX_SIZE;
            buffers[3].u64 = address;
            segments+=3;
            break;
        }
        else if (size <= MAX_SIZE * 4)
        {
            /* Four more segments needed */
            buffers[0].pcie_length.len0 = MAX_SIZE;
            buffers[0].pcie_length.len1 = MAX_SIZE;
            buffers[0].pcie_length.len2 = MAX_SIZE;
            buffers[0].pcie_length.len3 = size - MAX_SIZE * 3;
            buffers[1].u64 = address;
            address += MAX_SIZE;
            buffers[2].u64 = address;
            address += MAX_SIZE;
            buffers[3].u64 = address;
            address += MAX_SIZE;
            buffers[4].u64 = address;
            segments+=4;
            break;
        }
        else
        {
            /* Five or more segments are needed */
            buffers[0].pcie_length.len0 = MAX_SIZE;
            buffers[0].pcie_length.len1 = MAX_SIZE;
            buffers[0].pcie_length.len2 = MAX_SIZE;
            buffers[0].pcie_length.len3 = MAX_SIZE;
            buffers[1].u64 = address;
            address += MAX_SIZE;
            buffers[2].u64 = address;
            address += MAX_SIZE;
            buffers[3].u64 = address;
            address += MAX_SIZE;
            buffers[4].u64 = address;
            address += MAX_SIZE;
            size -= MAX_SIZE*4;
            buffers += 5;
            segments+=4;
        }
    }
    return segments;
}


/**
 * Build the first and last pointers based on a DMA engine header
 * and submit them to the engine. The purpose of this function is
 * to simplify the building of DMA engine commands by automatically
 * converting a simple address and size into the apropriate internal
 * or PCI / PCIe address list. This function does not support gather lists,
 * so you will need to build your own lists in that case.
 *
 * @param engine Engine to submit to (0 to cvmx_dma_engine_get_num()-1)
 * @param header DMA Command header. Note that the nfst and nlst fields do not
 *               need to be filled in. All other fields must be set properly.
 * @param first_address
 *               Address to use for the first pointers. In the case of INTERNAL,
 *               INBOUND, and OUTBOUND this is an Octeon memory address. In the
 *               case of EXTERNAL, this is the source PCI / PCIe address.
 * @param last_address
 *               Address to use for the last pointers. In the case of EXTERNAL,
 *               INBOUND, and OUTBOUND this is a PCI / PCIe address. In the
 *               case of INTERNAL, this is the Octeon memory destination address.
 * @param size   Size of the transfer to perform.
 *
 * @return Zero on success, negative on failure
 */
int cvmx_dma_engine_transfer(int engine, cvmx_dma_engine_header_t header,
                             uint64_t first_address, uint64_t last_address,
                             int size)
{
    cvmx_dma_engine_buffer_t buffers[32];
    int words = 0;

    switch (header.s.type)
    {
        case CVMX_DMA_ENGINE_TRANSFER_INTERNAL:
            header.s.nfst = __cvmx_dma_engine_build_internal_pointers(buffers, first_address, size);
            words += header.s.nfst;
            header.s.nlst = __cvmx_dma_engine_build_internal_pointers(buffers + words, last_address, size);
            words += header.s.nlst;
            break;
        case CVMX_DMA_ENGINE_TRANSFER_INBOUND:
        case CVMX_DMA_ENGINE_TRANSFER_OUTBOUND:
            header.s.nfst = __cvmx_dma_engine_build_internal_pointers(buffers, first_address, size);
            words += header.s.nfst;
            header.s.nlst = __cvmx_dma_engine_build_external_pointers(buffers + words, last_address, size);
            words +=  header.s.nlst + ((header.s.nlst-1) >> 2) + 1;
            break;
        case CVMX_DMA_ENGINE_TRANSFER_EXTERNAL:
            header.s.nfst = __cvmx_dma_engine_build_external_pointers(buffers, first_address, size);
            words +=  header.s.nfst + ((header.s.nfst-1) >> 2) + 1;
            header.s.nlst = __cvmx_dma_engine_build_external_pointers(buffers + words, last_address, size);
            words +=  header.s.nlst + ((header.s.nlst-1) >> 2) + 1;
            break;
    }
    return cvmx_dma_engine_submit(engine, header, words, buffers);
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_dma_engine_transfer);
#endif
#endif
