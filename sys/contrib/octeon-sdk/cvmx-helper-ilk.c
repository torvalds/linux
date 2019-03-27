/***********************license start***************
 * Copyright (c) 2010  Cavium Inc. (support@cavium.com). All rights
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
 * Functions for ILK initialization, configuration,
 * and monitoring.
 *
 * <hr>$Revision: 41586 $<hr>
 */

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>

#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-helper.h>
#include <asm/octeon/cvmx-helper-cfg.h>
#include <asm/octeon/cvmx-ilk.h>
#include <asm/octeon/cvmx-bootmem.h>
#include <asm/octeon/cvmx-pko.h>
#include <asm/octeon/cvmx-qlm.h>
#include <asm/octeon/cvmx-ilk-defs.h>
#else
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "executive-config.h"
#include "cvmx-config.h"
#endif
#include "cvmx.h"
#include "cvmx-helper.h"
#include "cvmx-helper-cfg.h"
#include "cvmx-ilk.h"
#include "cvmx-bootmem.h"
#include "cvmx-pko.h"
#include "cvmx-qlm.h"
#endif

#ifdef CVMX_ENABLE_PKO_FUNCTIONS

int __cvmx_helper_ilk_enumerate(int interface)
{
    interface -= CVMX_ILK_GBL_BASE;
    return cvmx_ilk_chans[interface];
}

/**
 * @INTERNAL
 * Probe a ILK interface and determine the number of ports
 * connected to it. The ILK interface should still be down
 * after this call.
 *
 * @param interface Interface to probe
 *
 * @return Number of ports on the interface. Zero to disable.
 */
int __cvmx_helper_ilk_probe(int interface)
{
    int i, j, res = -1;
    static int pipe_base = 0, pknd_base = 0;
    static cvmx_ilk_pipe_chan_t *pch = NULL, *tmp;
    static cvmx_ilk_chan_pknd_t *chpknd = NULL, *tmp1;
    static cvmx_ilk_cal_entry_t *calent = NULL, *tmp2;

    if (!OCTEON_IS_MODEL(OCTEON_CN68XX))
        return 0;

    interface -= CVMX_ILK_GBL_BASE;
    if (interface >= CVMX_NUM_ILK_INTF)
        return 0;

    /* the configuration should be done only once */
    if (cvmx_ilk_get_intf_ena (interface))
        return cvmx_ilk_chans[interface];

    /* configure lanes and enable the link */
    res = cvmx_ilk_start_interface (interface, cvmx_ilk_lane_mask[interface]);
    if (res < 0)
        return 0;

    /* set up the group of pipes available to ilk */
    if (pipe_base == 0)
        pipe_base = __cvmx_pko_get_pipe (interface + CVMX_ILK_GBL_BASE, 0);

    if (pipe_base == -1)
    {
        pipe_base = 0;
        return 0;
    }

    res = cvmx_ilk_set_pipe (interface, pipe_base, cvmx_ilk_chans[interface]);
    if (res < 0)
        return 0;

    /* set up pipe to channel mapping */
    i = pipe_base;
    if (pch == NULL)
    {
        pch = (cvmx_ilk_pipe_chan_t *)
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
        kmalloc(CVMX_MAX_ILK_CHANS * sizeof(cvmx_ilk_pipe_chan_t), GFP_KERNEL);
#else
        cvmx_bootmem_alloc (CVMX_MAX_ILK_CHANS * sizeof(cvmx_ilk_pipe_chan_t),
                            sizeof(cvmx_ilk_pipe_chan_t));
#endif
        if (pch == NULL)
            return 0;
    }

    memset (pch, 0, CVMX_MAX_ILK_CHANS * sizeof(cvmx_ilk_pipe_chan_t));
    tmp = pch;
    for (j = 0; j < cvmx_ilk_chans[interface]; j++)
    {
        tmp->pipe = i++;
        tmp->chan = cvmx_ilk_chan_map[interface][j];
        tmp++;
    }
    res = cvmx_ilk_tx_set_channel (interface, pch, cvmx_ilk_chans[interface]);
    if (res < 0)
    {
        res = 0;
        goto err_free_pch;
    }
    pipe_base += cvmx_ilk_chans[interface];

    /* set up channel to pkind mapping */
    if (pknd_base == 0)
        pknd_base = cvmx_helper_get_pknd (interface + CVMX_ILK_GBL_BASE, 0);

    i = pknd_base;
    if (chpknd == NULL)
    {
        chpknd = (cvmx_ilk_chan_pknd_t *)
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
        kmalloc(CVMX_MAX_ILK_PKNDS * sizeof(cvmx_ilk_chan_pknd_t), GFP_KERNEL);
#else
        cvmx_bootmem_alloc (CVMX_MAX_ILK_PKNDS * sizeof(cvmx_ilk_chan_pknd_t),
                            sizeof(cvmx_ilk_chan_pknd_t));
#endif
        if (chpknd == NULL)
        {
            pipe_base -= cvmx_ilk_chans[interface];
            res = 0;
            goto err_free_pch;
        }
    }

    memset (chpknd, 0, CVMX_MAX_ILK_PKNDS * sizeof(cvmx_ilk_chan_pknd_t));
    tmp1 = chpknd;
    for (j = 0; j < cvmx_ilk_chans[interface]; j++)
    {
        tmp1->chan = cvmx_ilk_chan_map[interface][j];
        tmp1->pknd = i++;
        tmp1++;
    }
    res = cvmx_ilk_rx_set_pknd (interface, chpknd, cvmx_ilk_chans[interface]);
    if (res < 0)
    {
        pipe_base -= cvmx_ilk_chans[interface];
        res = 0;
        goto err_free_chpknd;
    }
    pknd_base += cvmx_ilk_chans[interface];

    /* Set up tx calendar */
    if (calent == NULL)
    {
        calent = (cvmx_ilk_cal_entry_t *)
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
        kmalloc(CVMX_MAX_ILK_PIPES * sizeof(cvmx_ilk_cal_entry_t), GFP_KERNEL);
#else
        cvmx_bootmem_alloc (CVMX_MAX_ILK_PIPES * sizeof(cvmx_ilk_cal_entry_t),
                            sizeof(cvmx_ilk_cal_entry_t));
#endif
        if (calent == NULL)
        {
            pipe_base -= cvmx_ilk_chans[interface];
            pknd_base -= cvmx_ilk_chans[interface];
            res = 0;
            goto err_free_chpknd;
        }
    }

    memset (calent, 0, CVMX_MAX_ILK_PIPES * sizeof(cvmx_ilk_cal_entry_t));
    tmp1 = chpknd;
    tmp2 = calent;
    for (j = 0; j < cvmx_ilk_chans[interface]; j++)
    {
        tmp2->pipe_bpid = tmp1->pknd;
        tmp2->ent_ctrl = PIPE_BPID;
        tmp1++;
        tmp2++;
    }
    res = cvmx_ilk_cal_setup_tx (interface, cvmx_ilk_chans[interface],
                                 calent, 1);
    if (res < 0)
    {
        pipe_base -= cvmx_ilk_chans[interface];
        pknd_base -= cvmx_ilk_chans[interface];
        res = 0;
        goto err_free_calent;
    }

    /* set up rx calendar. allocated memory can be reused.
     * this is because max pkind is always less than max pipe */
    memset (calent, 0, CVMX_MAX_ILK_PIPES * sizeof(cvmx_ilk_cal_entry_t));
    tmp = pch;
    tmp2 = calent;
    for (j = 0; j < cvmx_ilk_chans[interface]; j++)
    {
        tmp2->pipe_bpid = tmp->pipe;
        tmp2->ent_ctrl = PIPE_BPID;
        tmp++;
        tmp2++;
    }
    res = cvmx_ilk_cal_setup_rx (interface, cvmx_ilk_chans[interface],
                                 calent, CVMX_ILK_RX_FIFO_WM, 1);
    if (res < 0)
    {
        pipe_base -= cvmx_ilk_chans[interface];
        pknd_base -= cvmx_ilk_chans[interface];
        res = 0;
        goto err_free_calent;
    }
    res = __cvmx_helper_ilk_enumerate(interface + CVMX_ILK_GBL_BASE);

    goto out;

err_free_calent:
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
    kfree (calent);
#else
    /* no free() for cvmx_bootmem_alloc() */
#endif

err_free_chpknd:
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
    kfree (chpknd);
#else
    /* no free() for cvmx_bootmem_alloc() */ 
#endif

err_free_pch:
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
    kfree (pch);
#else
    /* no free() for cvmx_bootmem_alloc() */ 
#endif
out:
    return res;
}

/**
 * @INTERNAL
 * Bringup and enable ILK interface. After this call packet
 * I/O should be fully functional. This is called with IPD
 * enabled but PKO disabled.
 *
 * @param interface Interface to bring up
 *
 * @return Zero on success, negative on failure
 */
int __cvmx_helper_ilk_enable(int interface)
{
    interface -= CVMX_ILK_GBL_BASE;
    return cvmx_ilk_enable(interface);
}

/**
 * @INTERNAL
 * Return the link state of an IPD/PKO port as returned by ILK link status.
 *
 * @param ipd_port IPD/PKO port to query
 *
 * @return Link state
 */
cvmx_helper_link_info_t __cvmx_helper_ilk_link_get(int ipd_port)
{
    cvmx_helper_link_info_t result;
    int interface = cvmx_helper_get_interface_num(ipd_port);
    int retry_count = 0;
    cvmx_ilk_rxx_cfg1_t ilk_rxx_cfg1;
    cvmx_ilk_rxx_int_t ilk_rxx_int;
    int lanes = 0;

    result.u64 = 0;
    interface -= CVMX_ILK_GBL_BASE;

retry:
    retry_count++;
    if (retry_count > 10)
        goto out;

    ilk_rxx_cfg1.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG1(interface));
    ilk_rxx_int.u64 = cvmx_read_csr (CVMX_ILK_RXX_INT(interface));

    /* Clear all RX status bits */
    if (ilk_rxx_int.u64)
        cvmx_write_csr(CVMX_ILK_RXX_INT(interface), ilk_rxx_int.u64);

    if (ilk_rxx_cfg1.s.rx_bdry_lock_ena == 0)
    {
        /* We need to start looking for work boundary lock */
        ilk_rxx_cfg1.s.rx_bdry_lock_ena = cvmx_ilk_get_intf_ln_msk(interface);
        ilk_rxx_cfg1.s.rx_align_ena = 0;
        cvmx_write_csr(CVMX_ILK_RXX_CFG1(interface), ilk_rxx_cfg1.u64);
        //cvmx_dprintf("ILK%d: Looking for word boundary lock\n", interface);
        goto retry;
    }

    if (ilk_rxx_cfg1.s.rx_align_ena == 0)
    {
        if (ilk_rxx_int.s.word_sync_done)
        {
            ilk_rxx_cfg1.s.rx_align_ena = 1;
            cvmx_write_csr(CVMX_ILK_RXX_CFG1(interface), ilk_rxx_cfg1.u64);
            //printf("ILK%d: Looking for lane alignment\n", interface);
            goto retry;
        }
        goto out;
    }

    if (ilk_rxx_int.s.lane_align_fail)
    {
        ilk_rxx_cfg1.s.rx_bdry_lock_ena = 0;
        ilk_rxx_cfg1.s.rx_align_ena = 0;
        cvmx_write_csr(CVMX_ILK_RXX_CFG1(interface), ilk_rxx_cfg1.u64);
        cvmx_dprintf("ILK%d: Lane alignment failed\n", interface);
        goto out;
    }

    if (ilk_rxx_int.s.lane_align_done)
    {
        //cvmx_dprintf("ILK%d: Lane alignment complete\n", interface);
    }

    lanes = cvmx_pop(ilk_rxx_cfg1.s.rx_bdry_lock_ena);

    result.s.link_up = 1;
    result.s.full_duplex = 1;
    result.s.speed = cvmx_qlm_get_gbaud_mhz(1+interface) * 64 / 67;
    result.s.speed *= lanes;

out:
    /* If the link is down we will force disable the RX path. If it up, we'll
        set it to match the TX state set by the if_enable call */
    if (result.s.link_up)
    {
        cvmx_ilk_txx_cfg1_t ilk_txx_cfg1;
        ilk_txx_cfg1.u64 = cvmx_read_csr(CVMX_ILK_TXX_CFG1(interface));
        ilk_rxx_cfg1.s.pkt_ena = ilk_txx_cfg1.s.pkt_ena;
        cvmx_write_csr(CVMX_ILK_RXX_CFG1(interface), ilk_rxx_cfg1.u64);
        //cvmx_dprintf("ILK%d: link up, %d Mbps, Full duplex mode, %d lanes\n", interface, result.s.speed, lanes);  
    }
    else
    {
        ilk_rxx_cfg1.s.pkt_ena = 0;
        cvmx_write_csr(CVMX_ILK_RXX_CFG1(interface), ilk_rxx_cfg1.u64);
        //cvmx_dprintf("ILK link down\n");
    }
    return result;
}

/**
 * @INTERNAL
 * Set the link state of an IPD/PKO port.
 *
 * @param ipd_port  IPD/PKO port to configure
 * @param link_info The new link state
 *
 * @return Zero on success, negative on failure
 */
int __cvmx_helper_ilk_link_set(int ipd_port, cvmx_helper_link_info_t link_info)
{
    /* nothing to do */

    return 0;
}

/**
 * Display ilk interface statistics.
 *
 */
void __cvmx_helper_ilk_show_stats (void)
{
    int i, j;
    unsigned char *pchans, num_chans;
    unsigned int chan_tmp[CVMX_MAX_ILK_CHANS];
    cvmx_ilk_stats_ctrl_t ilk_stats_ctrl;

    for (i = 0; i < CVMX_NUM_ILK_INTF; i++)
    {
        cvmx_ilk_get_chan_info (i, &pchans, &num_chans);

        memset (chan_tmp, 0, CVMX_MAX_ILK_CHANS * sizeof (int));
        for (j = 0; j < num_chans; j++)
            chan_tmp[j] = pchans[j];

        ilk_stats_ctrl.chan_list = chan_tmp;
        ilk_stats_ctrl.num_chans = num_chans;
        ilk_stats_ctrl.clr_on_rd = 0;
        cvmx_ilk_show_stats (i, &ilk_stats_ctrl);
    }
}

#endif /* CVMX_ENABLE_PKO_FUNCTIONS */
