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
 * Support library for the ILK
 *
 * <hr>$Revision: 49448 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-sysinfo.h>
#include <asm/octeon/cvmx-pko.h>
#include <asm/octeon/cvmx-ilk.h>
#include <asm/octeon/cvmx-ilk-defs.h>
#include <asm/octeon/cvmx-helper-util.h>
#include <asm/octeon/cvmx-helper-ilk.h>
#else
#include "cvmx.h"
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "cvmx-config.h"
#endif
#include "cvmx-sysinfo.h"
#include "cvmx-pko.h"
#include "cvmx-ilk.h"
#include "cvmx-helper-util.h"
#include "cvmx-helper-ilk.h"
#endif

#ifdef CVMX_ENABLE_HELPER_FUNCTIONS

/*
 * global configurations. to disable the 2nd ILK, set
 * cvmx_ilk_lane_mask[CVMX_NUM_ILK_INTF] = {0xff, 0x0} and
 * cvmx_ilk_chans[CVMX_NUM_ILK_INTF] = {8, 0}
 */
unsigned char cvmx_ilk_lane_mask[CVMX_NUM_ILK_INTF] = {0xf, 0xf0};
//#define SINGLE_PORT_SIM_ILK
#ifdef SINGLE_PORT_SIM_ILK
unsigned char cvmx_ilk_chans[CVMX_NUM_ILK_INTF] = {1, 1};
unsigned char cvmx_ilk_chan_map[CVMX_NUM_ILK_INTF][CVMX_MAX_ILK_CHANS] =
{{0},
 {0}};
#else /* sample case */
unsigned char cvmx_ilk_chans[CVMX_NUM_ILK_INTF] = {8, 8};
unsigned char cvmx_ilk_chan_map[CVMX_NUM_ILK_INTF][CVMX_MAX_ILK_CHANS] =
{{0, 1, 2, 3, 4, 5, 6, 7},
 {0, 1, 2, 3, 4, 5, 6, 7}};
#endif

/* Default callbacks, can be overridden
 *  using cvmx_ilk_get_callbacks/cvmx_ilk_set_callbacks
 */
static cvmx_ilk_callbacks_t cvmx_ilk_callbacks = {
  .calendar_setup_rx   = cvmx_ilk_cal_setup_rx,
};

static cvmx_ilk_intf_t cvmx_ilk_intf_cfg[CVMX_NUM_ILK_INTF];

/**
 * Get current ILK initialization callbacks
 *
 * @param callbacks  Pointer to the callbacks structure.to fill
 *
 * @return Pointer to cvmx_ilk_callbacks_t structure.
 */
void cvmx_ilk_get_callbacks(cvmx_ilk_callbacks_t * callbacks)
{
    memcpy(callbacks, &cvmx_ilk_callbacks, sizeof(cvmx_ilk_callbacks));
}

/**
 * Set new ILK initialization callbacks
 *
 * @param new_callbacks  Pointer to an updated callbacks structure.
 */
void cvmx_ilk_set_callbacks(cvmx_ilk_callbacks_t * new_callbacks)
{
    memcpy(&cvmx_ilk_callbacks, new_callbacks, sizeof(cvmx_ilk_callbacks));
}

/**
 * Initialize and start the ILK interface.
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param lane_mask the lane group for this interface
 *
 * @return Zero on success, negative of failure.
 */
int cvmx_ilk_start_interface (int interface, unsigned char lane_mask)
{
    int res = -1;
    int other_intf, this_qlm, other_qlm;
    unsigned char uni_mask;
    cvmx_mio_qlmx_cfg_t mio_qlmx_cfg, other_mio_qlmx_cfg;
    cvmx_ilk_txx_cfg0_t ilk_txx_cfg0;
    cvmx_ilk_rxx_cfg0_t ilk_rxx_cfg0;
    cvmx_ilk_ser_cfg_t ilk_ser_cfg;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    if (lane_mask == 0)
        return res;

    /* check conflicts between 2 ilk interfaces. 1 lane can be assigned to 1
     * interface only */
    other_intf = !interface;
    this_qlm = interface + CVMX_ILK_QLM_BASE;
    other_qlm = other_intf + CVMX_ILK_QLM_BASE;
    if (cvmx_ilk_intf_cfg[other_intf].lane_en_mask & lane_mask)
    {
        cvmx_dprintf ("ILK%d: %s: lane assignment conflict\n", interface,
                      __FUNCTION__);
        return res;
    }

    /* check the legality of the lane mask. interface 0 can have 8 lanes,
     * while interface 1 can have 4 lanes at most */
    uni_mask = lane_mask >> (interface * 4);
    if ((uni_mask != 0x1 && uni_mask != 0x3 && uni_mask != 0xf &&
         uni_mask != 0xff) || (interface == 1 && lane_mask > 0xf0))
    {
#if CVMX_ENABLE_DEBUG_PRINTS
        cvmx_dprintf ("ILK%d: %s: incorrect lane mask: 0x%x \n", interface,
                      __FUNCTION__, uni_mask);
#endif
        return res;
    }

    /* check the availability of qlms. qlm_cfg = 001 means the chip is fused
     * to give this qlm to ilk */
    mio_qlmx_cfg.u64 = cvmx_read_csr (CVMX_MIO_QLMX_CFG(this_qlm));
    other_mio_qlmx_cfg.u64 = cvmx_read_csr (CVMX_MIO_QLMX_CFG(other_qlm));
    if (mio_qlmx_cfg.s.qlm_cfg != 1 ||
        (uni_mask == 0xff && other_mio_qlmx_cfg.s.qlm_cfg != 1))
    {
#if CVMX_ENABLE_DEBUG_PRINTS
        cvmx_dprintf ("ILK%d: %s: qlm unavailable\n", interface, __FUNCTION__);
#endif
        return res;
    }

    /* power up the serdes */
    ilk_ser_cfg.u64 = cvmx_read_csr (CVMX_ILK_SER_CFG);
    if (ilk_ser_cfg.s.ser_pwrup == 0)
    {
        ilk_ser_cfg.s.ser_rxpol_auto = 1;
        ilk_ser_cfg.s.ser_rxpol = 0;
        ilk_ser_cfg.s.ser_txpol = 0;
        ilk_ser_cfg.s.ser_reset_n = 0xff;
        ilk_ser_cfg.s.ser_haul = 0;
    }
    ilk_ser_cfg.s.ser_pwrup |= ((interface ==0) && (lane_mask > 0xf)) ?
                               0x3 : (1 << interface);
    cvmx_write_csr (CVMX_ILK_SER_CFG, ilk_ser_cfg.u64);

    /* configure the lane enable of the interface */
    ilk_txx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
    ilk_rxx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG0(interface));
    ilk_txx_cfg0.s.lane_ena = ilk_rxx_cfg0.s.lane_ena = lane_mask;
    cvmx_write_csr (CVMX_ILK_TXX_CFG0(interface), ilk_txx_cfg0.u64);
    cvmx_write_csr (CVMX_ILK_RXX_CFG0(interface), ilk_rxx_cfg0.u64);

    /* write to local cache. for lane speed, if interface 0 has 8 lanes,
     * assume both qlms have the same speed */
    cvmx_ilk_intf_cfg[interface].intf_en = 1;
    cvmx_ilk_intf_cfg[interface].lane_en_mask = lane_mask;
    res = 0;

    return res;
}

/**
 * set pipe group base and length for the interface
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param pipe_base the base of the pipe group
 * @param pipe_len  the length of the pipe group
 * 
 * @return Zero on success, negative of failure.
 */
int cvmx_ilk_set_pipe (int interface, int pipe_base, unsigned int pipe_len) 
{
    int res = -1;
    cvmx_ilk_txx_pipe_t ilk_txx_pipe;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    /* base should be between 0 and 127. base + length should be <127 */
    if (!(pipe_base >= 0 && pipe_base <= 127) || (pipe_base + pipe_len > 127))
    {
#if CVMX_ENABLE_DEBUG_PRINTS
        cvmx_dprintf ("ILK%d: %s: pipe base/length out of bounds\n", interface,
                      __FUNCTION__);
#endif
        return res;
    }

    /* set them in ilk tx section */
    ilk_txx_pipe.u64 = cvmx_read_csr (CVMX_ILK_TXX_PIPE(interface));
    ilk_txx_pipe.s.base = pipe_base;
    ilk_txx_pipe.s.nump = pipe_len;
    cvmx_write_csr (CVMX_ILK_TXX_PIPE(interface), ilk_txx_pipe.u64);
    res = 0;

    return res;
}

/**
 * set logical channels for tx
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param pch     pointer to an array of pipe-channel pair
 * @param num_chs the number of entries in the pipe-channel array
 *
 * @return Zero on success, negative of failure.
 */
int cvmx_ilk_tx_set_channel (int interface, cvmx_ilk_pipe_chan_t *pch,
                             unsigned int num_chs)
{
    int res = -1;
    cvmx_ilk_txx_idx_pmap_t ilk_txx_idx_pmap;
    unsigned int i;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    if (pch == NULL || num_chs > CVMX_MAX_ILK_PIPES)
        return res;

    /* write the pair to ilk tx */
    for (i = 0; i < num_chs; i++)
    {
        ilk_txx_idx_pmap.u64 = 0;
        ilk_txx_idx_pmap.s.index = pch->pipe;
        cvmx_write_csr(CVMX_ILK_TXX_IDX_PMAP(interface), ilk_txx_idx_pmap.u64);
        cvmx_write_csr(CVMX_ILK_TXX_MEM_PMAP(interface), pch->chan);
        pch++;
    }
    res = 0;

    return res;
}

/**
 * set pkind for rx
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param chpknd    pointer to an array of channel-pkind pair
 * @param num_pknd the number of entries in the channel-pkind array
 *
 * @return Zero on success, negative of failure.
 */
int cvmx_ilk_rx_set_pknd (int interface, cvmx_ilk_chan_pknd_t *chpknd,
                          unsigned int num_pknd)
{
    int res = -1;
    cvmx_ilk_rxf_idx_pmap_t ilk_rxf_idx_pmap;
    unsigned int i;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    if (chpknd == NULL || num_pknd > CVMX_MAX_ILK_PKNDS)
        return res;

    /* write the pair to ilk rx. note the channels for different interfaces
     * are given in *chpknd and interface is not used as a param */
    for (i = 0; i < num_pknd; i++)
    {
        ilk_rxf_idx_pmap.u64 = 0;
        ilk_rxf_idx_pmap.s.index = interface * 256 + chpknd->chan;
        cvmx_write_csr (CVMX_ILK_RXF_IDX_PMAP, ilk_rxf_idx_pmap.u64);
        cvmx_write_csr (CVMX_ILK_RXF_MEM_PMAP, chpknd->pknd);
        chpknd++;
    }
    res = 0;

    return res;
}

/**
 * configure calendar for rx
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param cal_depth the number of calendar entries
 * @param pent      pointer to calendar entries
 *
 * @return Zero on success, negative of failure.
 */
static int cvmx_ilk_rx_cal_conf (int interface, int cal_depth, 
                          cvmx_ilk_cal_entry_t *pent)
{
    int res = -1, num_grp, num_rest, i, j;
    cvmx_ilk_rxx_cfg0_t ilk_rxx_cfg0;
    cvmx_ilk_rxx_idx_cal_t ilk_rxx_idx_cal;
    cvmx_ilk_rxx_mem_cal0_t ilk_rxx_mem_cal0;
    cvmx_ilk_rxx_mem_cal1_t ilk_rxx_mem_cal1;
    unsigned long int tmp;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    if (cal_depth < CVMX_ILK_RX_MIN_CAL || cal_depth > CVMX_ILK_MAX_CAL
        || pent == NULL)
        return res;

    /* mandatory link-level fc as workarounds for ILK-15397 and ILK-15479 */
    /* TODO: test effectiveness */
#if 0
    if (OCTEON_IS_MODEL(OCTEON_CN68XX_PASS1_0) && pent->ent_ctrl == PIPE_BPID)
        for (i = 0; i < cal_depth; i++)
            pent->ent_ctrl = LINK;
#endif

    /* set the depth */
    ilk_rxx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG0(interface));
    ilk_rxx_cfg0.s.cal_depth = cal_depth;
    cvmx_write_csr (CVMX_ILK_RXX_CFG0(interface), ilk_rxx_cfg0.u64); 

    /* set the calendar index */
    num_grp = cal_depth / CVMX_ILK_CAL_GRP_SZ;
    num_rest = cal_depth % CVMX_ILK_CAL_GRP_SZ;
    ilk_rxx_idx_cal.u64 = 0;
    ilk_rxx_idx_cal.s.inc = 1;
    cvmx_write_csr (CVMX_ILK_RXX_IDX_CAL(interface), ilk_rxx_idx_cal.u64); 

    /* set the calendar entries. each group has both cal0 and cal1 registers */
    for (i = 0; i < num_grp; i++)
    {
        ilk_rxx_mem_cal0.u64 = 0;
        for (j = 0; j < CVMX_ILK_CAL_GRP_SZ/2; j++)
        {
            tmp = 0;
            tmp = pent->pipe_bpid & ~(~tmp << CVMX_ILK_PIPE_BPID_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) * j;
            ilk_rxx_mem_cal0.u64 |= tmp;

            tmp = 0;
            tmp = pent->ent_ctrl & ~(~tmp << CVMX_ILK_ENT_CTRL_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) * j +
                    CVMX_ILK_PIPE_BPID_SZ;
            ilk_rxx_mem_cal0.u64 |= tmp;
            pent++;
        }
        cvmx_write_csr(CVMX_ILK_RXX_MEM_CAL0(interface), ilk_rxx_mem_cal0.u64);

        ilk_rxx_mem_cal1.u64 = 0;
        for (j = 0; j < CVMX_ILK_CAL_GRP_SZ/2; j++)
        {
            tmp = 0;
            tmp = pent->pipe_bpid & ~(~tmp << CVMX_ILK_PIPE_BPID_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) * j;
            ilk_rxx_mem_cal1.u64 |= tmp;

            tmp = 0;
            tmp = pent->ent_ctrl & ~(~tmp << CVMX_ILK_ENT_CTRL_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) * j +
                    CVMX_ILK_PIPE_BPID_SZ;
            ilk_rxx_mem_cal1.u64 |= tmp;
            pent++;
        }
        cvmx_write_csr(CVMX_ILK_RXX_MEM_CAL1(interface), ilk_rxx_mem_cal1.u64);
    }

    /* set the calendar entries, the fraction of a group. but both cal0 and
     * cal1 must be written */
    ilk_rxx_mem_cal0.u64 = 0;
    ilk_rxx_mem_cal1.u64 = 0;
    for (i = 0; i < num_rest; i++)
    {
        if (i < CVMX_ILK_CAL_GRP_SZ/2)
        {
            tmp = 0;
            tmp = pent->pipe_bpid & ~(~tmp << CVMX_ILK_PIPE_BPID_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) * i;
            ilk_rxx_mem_cal0.u64 |= tmp;

            tmp = 0;
            tmp = pent->ent_ctrl & ~(~tmp << CVMX_ILK_ENT_CTRL_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) * i +
                    CVMX_ILK_PIPE_BPID_SZ;
            ilk_rxx_mem_cal0.u64 |= tmp;
            pent++;
        }

        if (i >= CVMX_ILK_CAL_GRP_SZ/2)
        {
            tmp = 0;
            tmp = pent->pipe_bpid & ~(~tmp << CVMX_ILK_PIPE_BPID_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) *
                    (i - CVMX_ILK_CAL_GRP_SZ/2);
            ilk_rxx_mem_cal1.u64 |= tmp;

            tmp = 0;
            tmp = pent->ent_ctrl & ~(~tmp << CVMX_ILK_ENT_CTRL_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) *
                    (i - CVMX_ILK_CAL_GRP_SZ/2) + CVMX_ILK_PIPE_BPID_SZ;
            ilk_rxx_mem_cal1.u64 |= tmp;
            pent++;
        }
    }
    cvmx_write_csr(CVMX_ILK_RXX_MEM_CAL0(interface), ilk_rxx_mem_cal0.u64);
    cvmx_write_csr(CVMX_ILK_RXX_MEM_CAL1(interface), ilk_rxx_mem_cal1.u64);
    cvmx_read_csr (CVMX_ILK_RXX_MEM_CAL1(interface));

    return 0;
}

/**
 * set high water mark for rx
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param hi_wm     high water mark for this interface
 *
 * @return Zero on success, negative of failure.
 */
static int cvmx_ilk_rx_set_hwm (int interface, int hi_wm)
{
    int res = -1;
    cvmx_ilk_rxx_cfg1_t ilk_rxx_cfg1;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    if (hi_wm <= 0)
        return res;

    /* set the hwm */
    ilk_rxx_cfg1.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG1(interface));
    ilk_rxx_cfg1.s.rx_fifo_hwm = hi_wm;
    cvmx_write_csr (CVMX_ILK_RXX_CFG1(interface), ilk_rxx_cfg1.u64); 
    res = 0;

    return res;
}

/**
 * enable calendar for rx
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param cal_ena   enable or disable calendar
 *
 * @return Zero on success, negative of failure.
 */
static int cvmx_ilk_rx_cal_ena (int interface, unsigned char cal_ena)
{
    int res = -1;
    cvmx_ilk_rxx_cfg0_t ilk_rxx_cfg0;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    /* set the enable */
    ilk_rxx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG0(interface));
    ilk_rxx_cfg0.s.cal_ena = cal_ena;
    cvmx_write_csr (CVMX_ILK_RXX_CFG0(interface), ilk_rxx_cfg0.u64); 
    cvmx_read_csr (CVMX_ILK_RXX_CFG0(interface));
    res = 0;

    return res;
}

/**
 * set up calendar for rx
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param cal_depth the number of calendar entries
 * @param pent      pointer to calendar entries
 * @param hi_wm     high water mark for this interface
 * @param cal_ena   enable or disable calendar
 *
 * @return Zero on success, negative of failure.
 */
int cvmx_ilk_cal_setup_rx (int interface, int cal_depth,
                           cvmx_ilk_cal_entry_t *pent, int hi_wm,
                           unsigned char cal_ena)
{
    int res = -1;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    res = cvmx_ilk_rx_cal_conf (interface, cal_depth, pent);
    if (res < 0)
        return res;

    res = cvmx_ilk_rx_set_hwm (interface, hi_wm);
    if (res < 0)
        return res;

    res = cvmx_ilk_rx_cal_ena (interface, cal_ena);
    return res;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_ilk_cal_setup_rx);
#endif

/**
 * configure calendar for tx
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param cal_depth the number of calendar entries
 * @param pent      pointer to calendar entries
 *
 * @return Zero on success, negative of failure.
 */
static int cvmx_ilk_tx_cal_conf (int interface, int cal_depth, 
                          cvmx_ilk_cal_entry_t *pent)
{
    int res = -1, num_grp, num_rest, i, j;
    cvmx_ilk_txx_cfg0_t ilk_txx_cfg0;
    cvmx_ilk_txx_idx_cal_t ilk_txx_idx_cal;
    cvmx_ilk_txx_mem_cal0_t ilk_txx_mem_cal0;
    cvmx_ilk_txx_mem_cal1_t ilk_txx_mem_cal1;
    unsigned long int tmp;
    cvmx_ilk_cal_entry_t *ent_tmp;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    if (cal_depth < CVMX_ILK_TX_MIN_CAL || cal_depth > CVMX_ILK_MAX_CAL
        || pent == NULL)
        return res;

    /* mandatory link-level fc as workarounds for ILK-15397 and ILK-15479 */
    /* TODO: test effectiveness */
#if 0
    if (OCTEON_IS_MODEL(OCTEON_CN68XX_PASS1_0) && pent->ent_ctrl == PIPE_BPID)
        for (i = 0; i < cal_depth; i++)
            pent->ent_ctrl = LINK;
#endif

    /* tx calendar depth must be a multiple of 8 */
    num_grp = (cal_depth - 1) / CVMX_ILK_CAL_GRP_SZ + 1;
    num_rest = cal_depth % CVMX_ILK_CAL_GRP_SZ;
    if (num_rest != 0)
    {
        ent_tmp = pent + cal_depth;
        for (i = num_rest; i < 8; i++, ent_tmp++)
        {
            ent_tmp->pipe_bpid = 0;
            ent_tmp->ent_ctrl = XOFF;
        }
    }
    cal_depth = num_grp * 8;

    /* set the depth */
    ilk_txx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
    ilk_txx_cfg0.s.cal_depth = cal_depth;
    cvmx_write_csr (CVMX_ILK_TXX_CFG0(interface), ilk_txx_cfg0.u64); 

    /* set the calendar index */
    ilk_txx_idx_cal.u64 = 0;
    ilk_txx_idx_cal.s.inc = 1;
    cvmx_write_csr (CVMX_ILK_TXX_IDX_CAL(interface), ilk_txx_idx_cal.u64); 

    /* set the calendar entries. each group has both cal0 and cal1 registers */
    for (i = 0; i < num_grp; i++)
    {
        ilk_txx_mem_cal0.u64 = 0;
        for (j = 0; j < CVMX_ILK_CAL_GRP_SZ/2; j++)
        {
            tmp = 0;
            tmp = pent->pipe_bpid & ~(~tmp << CVMX_ILK_PIPE_BPID_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) * j;
            ilk_txx_mem_cal0.u64 |= tmp;

            tmp = 0;
            tmp = pent->ent_ctrl & ~(~tmp << CVMX_ILK_ENT_CTRL_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) * j +
                    CVMX_ILK_PIPE_BPID_SZ;
            ilk_txx_mem_cal0.u64 |= tmp;
            pent++;
        }
        cvmx_write_csr(CVMX_ILK_TXX_MEM_CAL0(interface), ilk_txx_mem_cal0.u64);

        ilk_txx_mem_cal1.u64 = 0;
        for (j = 0; j < CVMX_ILK_CAL_GRP_SZ/2; j++)
        {
            tmp = 0;
            tmp = pent->pipe_bpid & ~(~tmp << CVMX_ILK_PIPE_BPID_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) * j;
            ilk_txx_mem_cal1.u64 |= tmp;

            tmp = 0;
            tmp = pent->ent_ctrl & ~(~tmp << CVMX_ILK_ENT_CTRL_SZ);
            tmp <<= (CVMX_ILK_PIPE_BPID_SZ + CVMX_ILK_ENT_CTRL_SZ) * j +
                    CVMX_ILK_PIPE_BPID_SZ;
            ilk_txx_mem_cal1.u64 |= tmp;
            pent++;
        }
        cvmx_write_csr(CVMX_ILK_TXX_MEM_CAL1(interface), ilk_txx_mem_cal1.u64);
    }
    cvmx_read_csr (CVMX_ILK_TXX_MEM_CAL1(interface));

    return 0;
}

#ifdef CVMX_ILK_BP_CONF_ENA
/**
 * configure backpressure for tx
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param cal_depth the number of calendar entries
 * @param pent      pointer to calendar entries
 *
 * @return Zero on success, negative of failure.
 */
static int cvmx_ilk_bp_conf (int interface, int cal_depth, cvmx_ilk_cal_entry_t *pent)
{
    int res = -1, i;
    cvmx_ipd_ctl_status_t ipd_ctl_status;
    cvmx_ilk_cal_entry_t *tmp;
    unsigned char bpid;
    cvmx_ipd_bpidx_mbuf_th_t ipd_bpidx_mbuf_th;

    /* enable bp for the interface */
    ipd_ctl_status.u64 = cvmx_read_csr (CVMX_IPD_CTL_STATUS);
    ipd_ctl_status.s.pbp_en = 1;
    cvmx_write_csr (CVMX_IPD_CTL_STATUS, ipd_ctl_status.u64);

    /* enable bp for each id */
    for (i = 0, tmp = pent; i < cal_depth; i++, tmp++)
    {
        bpid = tmp->pipe_bpid;
        ipd_bpidx_mbuf_th.u64 =
            cvmx_read_csr (CVMX_IPD_BPIDX_MBUF_TH(bpid));
        ipd_bpidx_mbuf_th.s.page_cnt = 1; /* 256 buffers */
        ipd_bpidx_mbuf_th.s.bp_enb = 1;
        cvmx_write_csr (CVMX_IPD_BPIDX_MBUF_TH(bpid), ipd_bpidx_mbuf_th.u64);
    }         
    res = 0;

    return res;
}
#endif

/**
 * enable calendar for tx
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param cal_ena   enable or disable calendar
 *
 * @return Zero on success, negative of failure.
 */
static int cvmx_ilk_tx_cal_ena (int interface, unsigned char cal_ena)
{
    int res = -1;
    cvmx_ilk_txx_cfg0_t ilk_txx_cfg0;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    /* set the enable */
    ilk_txx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
    ilk_txx_cfg0.s.cal_ena = cal_ena;
    cvmx_write_csr (CVMX_ILK_TXX_CFG0(interface), ilk_txx_cfg0.u64); 
    cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
    res = 0;

    return res;
}

/**
 * set up calendar for tx
 *
 * @param interface The identifier of the packet interface to configure and
 *                  use as a ILK interface. cn68xx has 2 interfaces: ilk0 and
 *                  ilk1.
 *
 * @param cal_depth the number of calendar entries
 * @param pent      pointer to calendar entries
 * @param cal_ena   enable or disable calendar
 *
 * @return Zero on success, negative of failure.
 */
int cvmx_ilk_cal_setup_tx (int interface, int cal_depth,
                           cvmx_ilk_cal_entry_t *pent, unsigned char cal_ena)
{
    int res = -1;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    res = cvmx_ilk_tx_cal_conf (interface, cal_depth, pent);
    if (res < 0)
        return res;

#ifdef CVMX_ILK_BP_CONF_ENA
    res = cvmx_ilk_bp_conf (interface, cal_depth, pent);
    if (res < 0)
        return res;
#endif

    res = cvmx_ilk_tx_cal_ena (interface, cal_ena);
    return res;
}
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(cvmx_ilk_cal_setup_tx);
#endif

#ifdef CVMX_ILK_STATS_ENA
static void cvmx_ilk_reg_dump_rx (int interface)
{
    int i;
    cvmx_ilk_rxx_cfg0_t ilk_rxx_cfg0;
    cvmx_ilk_rxx_cfg1_t ilk_rxx_cfg1;
    cvmx_ilk_rxx_int_t ilk_rxx_int;
    cvmx_ilk_rxx_jabber_t ilk_rxx_jabber;
    cvmx_ilk_rx_lnex_cfg_t ilk_rx_lnex_cfg;
    cvmx_ilk_rx_lnex_int_t ilk_rx_lnex_int;
    cvmx_ilk_gbl_cfg_t ilk_gbl_cfg;
    cvmx_ilk_ser_cfg_t ilk_ser_cfg;
    cvmx_ilk_rxf_idx_pmap_t ilk_rxf_idx_pmap;
    cvmx_ilk_rxf_mem_pmap_t ilk_rxf_mem_pmap;
    cvmx_ilk_rxx_idx_cal_t ilk_rxx_idx_cal;
    cvmx_ilk_rxx_mem_cal0_t ilk_rxx_mem_cal0;
    cvmx_ilk_rxx_mem_cal1_t ilk_rxx_mem_cal1;

    ilk_rxx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG0(interface));
    cvmx_dprintf ("ilk rxx cfg0: 0x%16lx\n", ilk_rxx_cfg0.u64);
    
    ilk_rxx_cfg1.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG1(interface));
    cvmx_dprintf ("ilk rxx cfg1: 0x%16lx\n", ilk_rxx_cfg1.u64);
    
    ilk_rxx_int.u64 = cvmx_read_csr (CVMX_ILK_RXX_INT(interface));
    cvmx_dprintf ("ilk rxx int: 0x%16lx\n", ilk_rxx_int.u64);
    cvmx_write_csr (CVMX_ILK_RXX_INT(interface), ilk_rxx_int.u64);

    ilk_rxx_jabber.u64 = cvmx_read_csr (CVMX_ILK_RXX_JABBER(interface));
    cvmx_dprintf ("ilk rxx jabber: 0x%16lx\n", ilk_rxx_jabber.u64);

#define LNE_NUM_DBG 4
    for (i = 0; i < LNE_NUM_DBG; i++)
    {
        ilk_rx_lnex_cfg.u64 = cvmx_read_csr (CVMX_ILK_RX_LNEX_CFG(i));
        cvmx_dprintf ("ilk rx lnex cfg lane: %d  0x%16lx\n", i,
                      ilk_rx_lnex_cfg.u64);
    }

    for (i = 0; i < LNE_NUM_DBG; i++)
    {
        ilk_rx_lnex_int.u64 = cvmx_read_csr (CVMX_ILK_RX_LNEX_INT(i));
        cvmx_dprintf ("ilk rx lnex int lane: %d  0x%16lx\n", i,
                      ilk_rx_lnex_int.u64);
        cvmx_write_csr (CVMX_ILK_RX_LNEX_INT(i), ilk_rx_lnex_int.u64);
    }

    ilk_gbl_cfg.u64 = cvmx_read_csr (CVMX_ILK_GBL_CFG);
    cvmx_dprintf ("ilk gbl cfg: 0x%16lx\n", ilk_gbl_cfg.u64);

    ilk_ser_cfg.u64 = cvmx_read_csr (CVMX_ILK_SER_CFG);
    cvmx_dprintf ("ilk ser cfg: 0x%16lx\n", ilk_ser_cfg.u64);

#define CHAN_NUM_DBG 8
    ilk_rxf_idx_pmap.u64 = 0;
    ilk_rxf_idx_pmap.s.index = interface * 256;
    ilk_rxf_idx_pmap.s.inc = 1;
    cvmx_write_csr (CVMX_ILK_RXF_IDX_PMAP, ilk_rxf_idx_pmap.u64);
    for (i = 0; i < CHAN_NUM_DBG; i++)
    {
        ilk_rxf_mem_pmap.u64 = cvmx_read_csr (CVMX_ILK_RXF_MEM_PMAP);
        cvmx_dprintf ("ilk rxf mem pmap chan: %3d  0x%16lx\n", i,
                      ilk_rxf_mem_pmap.u64);
    }

#define CAL_NUM_DBG 2
    ilk_rxx_idx_cal.u64 = 0;
    ilk_rxx_idx_cal.s.inc = 1;
    cvmx_write_csr (CVMX_ILK_RXX_IDX_CAL(interface), ilk_rxx_idx_cal.u64); 
    for (i = 0; i < CAL_NUM_DBG; i++)
    {
        ilk_rxx_idx_cal.u64 = cvmx_read_csr(CVMX_ILK_RXX_IDX_CAL(interface));
        cvmx_dprintf ("ilk rxx idx cal: 0x%16lx\n", ilk_rxx_idx_cal.u64);

        ilk_rxx_mem_cal0.u64 = cvmx_read_csr(CVMX_ILK_RXX_MEM_CAL0(interface));
        cvmx_dprintf ("ilk rxx mem cal0: 0x%16lx\n", ilk_rxx_mem_cal0.u64);
        ilk_rxx_mem_cal1.u64 = cvmx_read_csr(CVMX_ILK_RXX_MEM_CAL1(interface));
        cvmx_dprintf ("ilk rxx mem cal1: 0x%16lx\n", ilk_rxx_mem_cal1.u64);
    }
}

static void cvmx_ilk_reg_dump_tx (int interface)
{
    int i;
    cvmx_ilk_txx_cfg0_t ilk_txx_cfg0;
    cvmx_ilk_txx_cfg1_t ilk_txx_cfg1;
    cvmx_ilk_txx_idx_pmap_t ilk_txx_idx_pmap;
    cvmx_ilk_txx_mem_pmap_t ilk_txx_mem_pmap;
    cvmx_ilk_txx_int_t ilk_txx_int;
    cvmx_ilk_txx_pipe_t ilk_txx_pipe;
    cvmx_ilk_txx_idx_cal_t ilk_txx_idx_cal;
    cvmx_ilk_txx_mem_cal0_t ilk_txx_mem_cal0;
    cvmx_ilk_txx_mem_cal1_t ilk_txx_mem_cal1;

    ilk_txx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
    cvmx_dprintf ("ilk txx cfg0: 0x%16lx\n", ilk_txx_cfg0.u64);
    
    ilk_txx_cfg1.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG1(interface));
    cvmx_dprintf ("ilk txx cfg1: 0x%16lx\n", ilk_txx_cfg1.u64);

    ilk_txx_pipe.u64 = cvmx_read_csr (CVMX_ILK_TXX_PIPE(interface));
    cvmx_dprintf ("ilk txx pipe: 0x%16lx\n", ilk_txx_pipe.u64);

    ilk_txx_idx_pmap.u64 = 0;
    ilk_txx_idx_pmap.s.index = ilk_txx_pipe.s.base;
    ilk_txx_idx_pmap.s.inc = 1;
    cvmx_write_csr (CVMX_ILK_TXX_IDX_PMAP(interface), ilk_txx_idx_pmap.u64);
    for (i = 0; i < CHAN_NUM_DBG; i++)
    {
        ilk_txx_mem_pmap.u64 = cvmx_read_csr (CVMX_ILK_TXX_MEM_PMAP(interface));
        cvmx_dprintf ("ilk txx mem pmap pipe: %3d  0x%16lx\n",
                      ilk_txx_pipe.s.base + i, ilk_txx_mem_pmap.u64);
    }

    ilk_txx_int.u64 = cvmx_read_csr (CVMX_ILK_TXX_INT(interface));
    cvmx_dprintf ("ilk txx int: 0x%16lx\n", ilk_txx_int.u64);

    ilk_txx_idx_cal.u64 = 0;
    ilk_txx_idx_cal.s.inc = 1;
    cvmx_write_csr (CVMX_ILK_TXX_IDX_CAL(interface), ilk_txx_idx_cal.u64); 
    for (i = 0; i < CAL_NUM_DBG; i++)
    {
        ilk_txx_idx_cal.u64 = cvmx_read_csr(CVMX_ILK_TXX_IDX_CAL(interface));
        cvmx_dprintf ("ilk txx idx cal: 0x%16lx\n", ilk_txx_idx_cal.u64);

        ilk_txx_mem_cal0.u64 = cvmx_read_csr(CVMX_ILK_TXX_MEM_CAL0(interface));
        cvmx_dprintf ("ilk txx mem cal0: 0x%16lx\n", ilk_txx_mem_cal0.u64);
        ilk_txx_mem_cal1.u64 = cvmx_read_csr(CVMX_ILK_TXX_MEM_CAL1(interface));
        cvmx_dprintf ("ilk txx mem cal1: 0x%16lx\n", ilk_txx_mem_cal1.u64);
    }
}
#endif

/**
 * show run time status
 *
 * @param interface The identifier of the packet interface to enable. cn68xx
 *                  has 2 interfaces: ilk0 and ilk1.
 *
 * @return nothing
 */
#ifdef CVMX_ILK_RUNTIME_DBG
void cvmx_ilk_runtime_status (int interface)
{
    cvmx_ilk_txx_cfg1_t ilk_txx_cfg1;
    cvmx_ilk_txx_flow_ctl0_t ilk_txx_flow_ctl0; 
    cvmx_ilk_rxx_cfg1_t ilk_rxx_cfg1;
    cvmx_ilk_rxx_int_t ilk_rxx_int;
    cvmx_ilk_rxx_flow_ctl0_t ilk_rxx_flow_ctl0; 
    cvmx_ilk_rxx_flow_ctl1_t ilk_rxx_flow_ctl1; 
    cvmx_ilk_gbl_int_t ilk_gbl_int;

    cvmx_dprintf ("\nilk run-time status: interface: %d\n", interface);

    ilk_txx_cfg1.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG1(interface));
    cvmx_dprintf ("\nilk txx cfg1: 0x%16lx\n", ilk_txx_cfg1.u64);
    if (ilk_txx_cfg1.s.rx_link_fc)
        cvmx_dprintf ("link flow control received\n");
    if (ilk_txx_cfg1.s.tx_link_fc)
        cvmx_dprintf ("link flow control sent\n");

    ilk_txx_flow_ctl0.u64 = cvmx_read_csr (CVMX_ILK_TXX_FLOW_CTL0(interface));
    cvmx_dprintf ("\nilk txx flow ctl0: 0x%16lx\n", ilk_txx_flow_ctl0.u64);
        
    ilk_rxx_cfg1.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG1(interface));
    cvmx_dprintf ("\nilk rxx cfg1: 0x%16lx\n", ilk_rxx_cfg1.u64);
    cvmx_dprintf ("rx fifo count: %d\n", ilk_rxx_cfg1.s.rx_fifo_cnt);

    ilk_rxx_int.u64 = cvmx_read_csr (CVMX_ILK_RXX_INT(interface));
    cvmx_dprintf ("\nilk rxx int: 0x%16lx\n", ilk_rxx_int.u64);
    if (ilk_rxx_int.s.pkt_drop_rxf)
        cvmx_dprintf ("rx fifo packet drop\n");
    if (ilk_rxx_int.u64)
        cvmx_write_csr (CVMX_ILK_RXX_INT(interface), ilk_rxx_int.u64);
        
    ilk_rxx_flow_ctl0.u64 = cvmx_read_csr (CVMX_ILK_RXX_FLOW_CTL0(interface));
    cvmx_dprintf ("\nilk rxx flow ctl0: 0x%16lx\n", ilk_rxx_flow_ctl0.u64);

    ilk_rxx_flow_ctl1.u64 = cvmx_read_csr (CVMX_ILK_RXX_FLOW_CTL1(interface));
    cvmx_dprintf ("\nilk rxx flow ctl1: 0x%16lx\n", ilk_rxx_flow_ctl1.u64);

    ilk_gbl_int.u64 = cvmx_read_csr (CVMX_ILK_GBL_INT);
    cvmx_dprintf ("\nilk gbl int: 0x%16lx\n", ilk_gbl_int.u64);
    if (ilk_gbl_int.s.rxf_push_full)
        cvmx_dprintf ("rx fifo overflow\n");
    if (ilk_gbl_int.u64)
        cvmx_write_csr (CVMX_ILK_GBL_INT, ilk_gbl_int.u64);
}
#endif

/**
 * enable interface
 *
 * @param interface The identifier of the packet interface to enable. cn68xx
 *                  has 2 interfaces: ilk0 and ilk1.
 *
 * @return Zero on success, negative of failure.
 */
//#define CVMX_ILK_STATS_ENA 1
int cvmx_ilk_enable (int interface)
{
    int res = -1;
    int retry_count = 0;
    cvmx_helper_link_info_t result;
    cvmx_ilk_txx_cfg1_t ilk_txx_cfg1;
    cvmx_ilk_rxx_cfg1_t ilk_rxx_cfg1;
#ifdef CVMX_ILK_STATS_ENA
    cvmx_ilk_rxx_cfg0_t ilk_rxx_cfg0;
    cvmx_ilk_txx_cfg0_t ilk_txx_cfg0;
#endif

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    result.u64 = 0;
    
#ifdef CVMX_ILK_STATS_ENA
    cvmx_dprintf ("\n");
    cvmx_dprintf ("<<<< ILK%d: Before enabling ilk\n", interface);
    cvmx_ilk_reg_dump_rx (interface);
    cvmx_ilk_reg_dump_tx (interface);
#endif

    /* RX packet will be enabled only if link is up */

    /* TX side */
    ilk_txx_cfg1.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG1(interface));
    ilk_txx_cfg1.s.pkt_ena = 1;
    ilk_txx_cfg1.s.rx_link_fc_ign = 1; /* cannot use link fc workaround */
    cvmx_write_csr (CVMX_ILK_TXX_CFG1(interface), ilk_txx_cfg1.u64); 
    cvmx_read_csr (CVMX_ILK_TXX_CFG1(interface));

#ifdef CVMX_ILK_STATS_ENA
    /* RX side stats */
    ilk_rxx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG0(interface));
    ilk_rxx_cfg0.s.lnk_stats_ena = 1;
    cvmx_write_csr (CVMX_ILK_RXX_CFG0(interface), ilk_rxx_cfg0.u64); 

    /* TX side stats */
    ilk_txx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
    ilk_txx_cfg0.s.lnk_stats_ena = 1;
    cvmx_write_csr (CVMX_ILK_TXX_CFG0(interface), ilk_txx_cfg0.u64); 
#endif

retry:
    retry_count++;
    if (retry_count > 10)
       goto out;

    /* Make sure the link is up, so that packets can be sent. */
    result = __cvmx_helper_ilk_link_get(cvmx_helper_get_ipd_port(interface + CVMX_ILK_GBL_BASE, 0));

    /* Small delay before another retry. */
    cvmx_wait_usec(100);

    ilk_rxx_cfg1.u64 = cvmx_read_csr(CVMX_ILK_RXX_CFG1(interface));
    if (ilk_rxx_cfg1.s.pkt_ena == 0)
       goto retry; 

out:
        
#ifdef CVMX_ILK_STATS_ENA
    cvmx_dprintf (">>>> ILK%d: After ILK is enabled\n", interface);
    cvmx_ilk_reg_dump_rx (interface);
    cvmx_ilk_reg_dump_tx (interface);
#endif

    if (result.s.link_up)
        return 0;

    return -1;
}

/**
 * Disable interface
 *
 * @param interface The identifier of the packet interface to disable. cn68xx
 *                  has 2 interfaces: ilk0 and ilk1.
 *
 * @return Zero on success, negative of failure.
 */
int cvmx_ilk_disable (int interface)
{
    int res = -1;
    cvmx_ilk_txx_cfg1_t ilk_txx_cfg1;
    cvmx_ilk_rxx_cfg1_t ilk_rxx_cfg1;
#ifdef CVMX_ILK_STATS_ENA
    cvmx_ilk_rxx_cfg0_t ilk_rxx_cfg0;
    cvmx_ilk_txx_cfg0_t ilk_txx_cfg0;
#endif

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    /* TX side */
    ilk_txx_cfg1.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG1(interface));
    ilk_txx_cfg1.s.pkt_ena = 0;
    cvmx_write_csr (CVMX_ILK_TXX_CFG1(interface), ilk_txx_cfg1.u64); 

    /* RX side */
    ilk_rxx_cfg1.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG1(interface));
    ilk_rxx_cfg1.s.pkt_ena = 0;
    cvmx_write_csr (CVMX_ILK_RXX_CFG1(interface), ilk_rxx_cfg1.u64); 

#ifdef CVMX_ILK_STATS_ENA
    /* RX side stats */
    ilk_rxx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG0(interface));
    ilk_rxx_cfg0.s.lnk_stats_ena = 0;
    cvmx_write_csr (CVMX_ILK_RXX_CFG0(interface), ilk_rxx_cfg0.u64); 

    /* RX side stats */
    ilk_txx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
    ilk_txx_cfg0.s.lnk_stats_ena = 0;
    cvmx_write_csr (CVMX_ILK_TXX_CFG0(interface), ilk_txx_cfg0.u64); 
#endif

    return 0;
}

/**
 * Provide interface enable status
 *
 * @param interface The identifier of the packet interface to disable. cn68xx
 *                  has 2 interfaces: ilk0 and ilk1.
 *
 * @return Zero, not enabled; One, enabled.
 */
int cvmx_ilk_get_intf_ena (int interface)
{
    return cvmx_ilk_intf_cfg[interface].intf_en;
}

/**
 * bit counter
 *
 * @param uc the byte to be counted
 *
 * @return number of bits set
 */
unsigned char cvmx_ilk_bit_count (unsigned char uc)
{
    unsigned char count;

    for (count = 0; uc > 0; uc &= uc-1)
        count++;

    return count;
}

/**
 * Provide interface lane mask
 *
 * @param interface The identifier of the packet interface to disable. cn68xx
 *                  has 2 interfaces: ilk0 and ilk1.
 *
 * @return lane mask
 */
unsigned char cvmx_ilk_get_intf_ln_msk (int interface)
{
    return cvmx_ilk_intf_cfg[interface].lane_en_mask;
}

/**
 * Provide channel info
 *
 * @param interface The identifier of the packet interface to disable. cn68xx
 *                  has 2 interfaces: ilk0 and ilk1.
 * @param chans    A pointer to a channel array
 * @param num_chan A pointer to the number of channels
 *
 * @return Zero on success, negative of failure.
 */
int cvmx_ilk_get_chan_info (int interface, unsigned char **chans,
                            unsigned char *num_chan)
{
    *chans = cvmx_ilk_chan_map[interface];
    *num_chan = cvmx_ilk_chans[interface];

    return 0;
}

/**
 * Show channel statistics
 *
 * @param interface The identifier of the packet interface to disable. cn68xx
 *                  has 2 interfaces: ilk0 and ilk1.
 * @param pstats A pointer to cvmx_ilk_stats_ctrl_t that specifies which
 *               logical channels to access
 *
 * @return nothing
 */
void cvmx_ilk_show_stats (int interface, cvmx_ilk_stats_ctrl_t *pstats)
{
    unsigned int i;
    cvmx_ilk_rxx_idx_stat0_t ilk_rxx_idx_stat0;
    cvmx_ilk_rxx_idx_stat1_t ilk_rxx_idx_stat1;
    cvmx_ilk_rxx_mem_stat0_t ilk_rxx_mem_stat0;
    cvmx_ilk_rxx_mem_stat1_t ilk_rxx_mem_stat1;

    cvmx_ilk_txx_idx_stat0_t ilk_txx_idx_stat0;
    cvmx_ilk_txx_idx_stat1_t ilk_txx_idx_stat1;
    cvmx_ilk_txx_mem_stat0_t ilk_txx_mem_stat0;
    cvmx_ilk_txx_mem_stat1_t ilk_txx_mem_stat1;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return;

    if (interface >= CVMX_NUM_ILK_INTF)
        return;

    if (pstats == NULL)
        return;

    /* discrete channels */
    if (pstats->chan_list != NULL)
    {
        for (i = 0; i < pstats->num_chans; i++)
        {

            /* get the number of rx packets */
            ilk_rxx_idx_stat0.u64 = 0;
            ilk_rxx_idx_stat0.s.index = *pstats->chan_list;
            ilk_rxx_idx_stat0.s.clr = pstats->clr_on_rd;
            cvmx_write_csr (CVMX_ILK_RXX_IDX_STAT0(interface),
                            ilk_rxx_idx_stat0.u64);
            ilk_rxx_mem_stat0.u64 = cvmx_read_csr
                                    (CVMX_ILK_RXX_MEM_STAT0(interface));

            /* get the number of rx bytes */
            ilk_rxx_idx_stat1.u64 = 0;
            ilk_rxx_idx_stat1.s.index = *pstats->chan_list;
            ilk_rxx_idx_stat1.s.clr = pstats->clr_on_rd;
            cvmx_write_csr (CVMX_ILK_RXX_IDX_STAT1(interface),
                            ilk_rxx_idx_stat1.u64);
            ilk_rxx_mem_stat1.u64 = cvmx_read_csr
                                    (CVMX_ILK_RXX_MEM_STAT1(interface));

            cvmx_dprintf ("ILK%d Channel%d Rx: %d packets %d bytes\n", interface,
                    *pstats->chan_list, ilk_rxx_mem_stat0.s.rx_pkt,
                    (unsigned int) ilk_rxx_mem_stat1.s.rx_bytes);

            /* get the number of tx packets */
            ilk_txx_idx_stat0.u64 = 0;
            ilk_txx_idx_stat0.s.index = *pstats->chan_list;
            ilk_txx_idx_stat0.s.clr = pstats->clr_on_rd;
            cvmx_write_csr (CVMX_ILK_TXX_IDX_STAT0(interface),
                            ilk_txx_idx_stat0.u64);
            ilk_txx_mem_stat0.u64 = cvmx_read_csr
                                    (CVMX_ILK_TXX_MEM_STAT0(interface));

            /* get the number of tx bytes */
            ilk_txx_idx_stat1.u64 = 0;
            ilk_txx_idx_stat1.s.index = *pstats->chan_list;
            ilk_txx_idx_stat1.s.clr = pstats->clr_on_rd;
            cvmx_write_csr (CVMX_ILK_TXX_IDX_STAT1(interface),
                            ilk_txx_idx_stat1.u64);
            ilk_txx_mem_stat1.u64 = cvmx_read_csr
                                    (CVMX_ILK_TXX_MEM_STAT1(interface));

            cvmx_dprintf ("ILK%d Channel%d Tx: %d packets %d bytes\n", interface,
                    *pstats->chan_list, ilk_txx_mem_stat0.s.tx_pkt,
                    (unsigned int) ilk_txx_mem_stat1.s.tx_bytes);

            pstats++;
        }
        return;
    }

    /* continuous channels */
    ilk_rxx_idx_stat0.u64 = 0;
    ilk_rxx_idx_stat0.s.index = pstats->chan_start;
    ilk_rxx_idx_stat0.s.inc = pstats->chan_step;
    ilk_rxx_idx_stat0.s.clr = pstats->clr_on_rd;
    cvmx_write_csr (CVMX_ILK_RXX_IDX_STAT0(interface), ilk_rxx_idx_stat0.u64);

    ilk_rxx_idx_stat1.u64 = 0;
    ilk_rxx_idx_stat1.s.index = pstats->chan_start;
    ilk_rxx_idx_stat1.s.inc = pstats->chan_step;
    ilk_rxx_idx_stat1.s.clr = pstats->clr_on_rd;
    cvmx_write_csr (CVMX_ILK_RXX_IDX_STAT1(interface), ilk_rxx_idx_stat1.u64);

    ilk_txx_idx_stat0.u64 = 0;
    ilk_txx_idx_stat0.s.index = pstats->chan_start;
    ilk_txx_idx_stat0.s.inc = pstats->chan_step;
    ilk_txx_idx_stat0.s.clr = pstats->clr_on_rd;
    cvmx_write_csr (CVMX_ILK_TXX_IDX_STAT0(interface), ilk_txx_idx_stat0.u64);

    ilk_txx_idx_stat1.u64 = 0;
    ilk_txx_idx_stat1.s.index = pstats->chan_start;
    ilk_txx_idx_stat1.s.inc = pstats->chan_step;
    ilk_txx_idx_stat1.s.clr = pstats->clr_on_rd;
    cvmx_write_csr (CVMX_ILK_TXX_IDX_STAT1(interface), ilk_txx_idx_stat1.u64);

    for (i = pstats->chan_start; i <= pstats->chan_end; i += pstats->chan_step)
    {
        ilk_rxx_mem_stat0.u64 = cvmx_read_csr
                                (CVMX_ILK_RXX_MEM_STAT0(interface));
        ilk_rxx_mem_stat1.u64 = cvmx_read_csr
                                (CVMX_ILK_RXX_MEM_STAT1(interface));
        cvmx_dprintf ("ILK%d Channel%d Rx: %d packets %d bytes\n", interface, i,
                ilk_rxx_mem_stat0.s.rx_pkt,
                (unsigned int) ilk_rxx_mem_stat1.s.rx_bytes);

        ilk_txx_mem_stat0.u64 = cvmx_read_csr
                                (CVMX_ILK_TXX_MEM_STAT0(interface));
        ilk_txx_mem_stat1.u64 = cvmx_read_csr
                                (CVMX_ILK_TXX_MEM_STAT1(interface));
        cvmx_dprintf ("ILK%d Channel%d Tx: %d packets %d bytes\n", interface, i,
                ilk_rxx_mem_stat0.s.rx_pkt,
                (unsigned int) ilk_rxx_mem_stat1.s.rx_bytes);
    }

    return;
}

/**
 * enable or disable loopbacks
 *
 * @param interface The identifier of the packet interface to disable. cn68xx
 *                  has 2 interfaces: ilk0 and ilk1.
 * @param enable    Enable or disable loopback
 * @param mode      Internal or external loopback
 *
 * @return Zero on success, negative of failure.
 */
int cvmx_ilk_lpbk (int interface, cvmx_ilk_lpbk_ena_t enable,
                   cvmx_ilk_lpbk_mode_t mode)
{
    int res = -1;
    cvmx_ilk_txx_cfg0_t ilk_txx_cfg0;
    cvmx_ilk_rxx_cfg0_t ilk_rxx_cfg0;

    if (!(OCTEON_IS_MODEL(OCTEON_CN68XX)))
        return res;

    if (interface >= CVMX_NUM_ILK_INTF)
        return res;

    /* internal loopback. only 1 type of loopback can be on at 1 time */
    if (mode == CVMX_ILK_LPBK_INT)
    {
        if (enable == CVMX_ILK_LPBK_ENA)
        {
            ilk_txx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
            ilk_txx_cfg0.s.ext_lpbk = CVMX_ILK_LPBK_DISA;
            ilk_txx_cfg0.s.ext_lpbk_fc = CVMX_ILK_LPBK_DISA;
            cvmx_write_csr (CVMX_ILK_TXX_CFG0(interface), ilk_txx_cfg0.u64);

            ilk_rxx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG0(interface));
            ilk_rxx_cfg0.s.ext_lpbk = CVMX_ILK_LPBK_DISA;
            ilk_rxx_cfg0.s.ext_lpbk_fc = CVMX_ILK_LPBK_DISA;
            cvmx_write_csr (CVMX_ILK_RXX_CFG0(interface), ilk_rxx_cfg0.u64);
        }

        ilk_txx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
        ilk_txx_cfg0.s.int_lpbk = enable;
        cvmx_write_csr (CVMX_ILK_TXX_CFG0(interface), ilk_txx_cfg0.u64);

        res = 0;
        return res;
    }

    /* external loopback. only 1 type of loopback can be on at 1 time */
    if (enable == CVMX_ILK_LPBK_ENA)
    {
        ilk_txx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
        ilk_txx_cfg0.s.int_lpbk = CVMX_ILK_LPBK_DISA;
        cvmx_write_csr (CVMX_ILK_TXX_CFG0(interface), ilk_txx_cfg0.u64);
    }

    ilk_txx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_TXX_CFG0(interface));
    ilk_txx_cfg0.s.ext_lpbk = enable;
    ilk_txx_cfg0.s.ext_lpbk_fc = enable;
    cvmx_write_csr (CVMX_ILK_TXX_CFG0(interface), ilk_txx_cfg0.u64);

    ilk_rxx_cfg0.u64 = cvmx_read_csr (CVMX_ILK_RXX_CFG0(interface));
    ilk_rxx_cfg0.s.ext_lpbk = enable;
    ilk_rxx_cfg0.s.ext_lpbk_fc = enable;
    cvmx_write_csr (CVMX_ILK_RXX_CFG0(interface), ilk_rxx_cfg0.u64);

    res = 0;
    return res;
}

#endif /* CVMX_ENABLE_HELPER_FUNCTIONS */
