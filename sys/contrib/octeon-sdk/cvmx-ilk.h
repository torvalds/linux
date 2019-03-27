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
 * This file contains defines for the ILK interface

 * <hr>$Revision: 49448 $<hr>
 *
 *
 */
#ifndef __CVMX_ILK_H__
#define __CVMX_ILK_H__

#ifdef	__cplusplus
extern "C" {
#endif

/* CSR typedefs have been moved to cvmx-ilk-defs.h */

#define CVMX_ILK_GBL_BASE  5
#define CVMX_ILK_QLM_BASE  1

typedef struct
{
    int intf_en      : 8;
    int lane_en_mask : 8;
    int lane_speed   : 16;
    /* add more here */
} cvmx_ilk_intf_t;

#define CVMX_NUM_ILK_INTF  2
#define CVMX_MAX_ILK_LANES 8
extern unsigned char cvmx_ilk_lane_mask[CVMX_NUM_ILK_INTF];

typedef struct
{
    unsigned int pipe;
    unsigned int chan;
} cvmx_ilk_pipe_chan_t;

#define CVMX_ILK_PIPE_BASE 72
#define CVMX_MAX_ILK_PIPES 45
#define CVMX_MAX_ILK_CHANS 8
extern unsigned char cvmx_ilk_chans[CVMX_NUM_ILK_INTF];
extern unsigned char cvmx_ilk_chan_map[CVMX_NUM_ILK_INTF][CVMX_MAX_ILK_CHANS];

typedef struct
{
    unsigned int chan;
    unsigned int pknd;
} cvmx_ilk_chan_pknd_t;

#define CVMX_ILK_PKND_BASE 20
#define CVMX_MAX_ILK_PKNDS 8 /* must be <45 */

typedef struct
{
    unsigned int *chan_list; /* for discrete channels. or, must be null */
    unsigned int num_chans;

    unsigned int chan_start; /* for continuous channels */
    unsigned int chan_end;
    unsigned int chan_step;

    unsigned int clr_on_rd;
} cvmx_ilk_stats_ctrl_t;

#define CVMX_ILK_MAX_CAL      288
#define CVMX_ILK_TX_MIN_CAL   1
#define CVMX_ILK_RX_MIN_CAL   1
#define CVMX_ILK_CAL_GRP_SZ   8
#define CVMX_ILK_PIPE_BPID_SZ 7
#define CVMX_ILK_ENT_CTRL_SZ  2
#define CVMX_ILK_RX_FIFO_WM   0x200

typedef enum
{
    PIPE_BPID = 0,
    LINK, 
    XOFF,
    XON
} cvmx_ilk_cal_ent_ctrl_t;

typedef struct
{
    unsigned char pipe_bpid;
    cvmx_ilk_cal_ent_ctrl_t ent_ctrl;
} cvmx_ilk_cal_entry_t;

/** Callbacks structure to customize ILK initialization sequence */
typedef struct
{
    /** Called to setup rx calendar */
    int (*calendar_setup_rx) (int interface, int cal_depth,
                              cvmx_ilk_cal_entry_t *pent, int hi_wm,
                              unsigned char cal_ena);

    /** add more here */
} cvmx_ilk_callbacks_t;

typedef enum
{
    CVMX_ILK_LPBK_DISA = 0,
    CVMX_ILK_LPBK_ENA
} cvmx_ilk_lpbk_ena_t;

typedef enum
{
    CVMX_ILK_LPBK_INT = 0,
    CVMX_ILK_LPBK_EXT
} cvmx_ilk_lpbk_mode_t;

extern void cvmx_ilk_get_callbacks(cvmx_ilk_callbacks_t * callbacks);
extern void cvmx_ilk_set_callbacks(cvmx_ilk_callbacks_t * new_callbacks);

extern int cvmx_ilk_start_interface (int interface, unsigned char num_lanes);
extern int cvmx_ilk_set_pipe (int interface, int pipe_base,
                              unsigned int pipe_len);
extern int cvmx_ilk_tx_set_channel (int interface, cvmx_ilk_pipe_chan_t *pch,
                                    unsigned int num_chs);
extern int cvmx_ilk_rx_set_pknd (int interface, cvmx_ilk_chan_pknd_t *chpknd,
                                 unsigned int num_pknd);
extern int cvmx_ilk_calendar_setup_cb (int interface, int num_ports);
extern int cvmx_ilk_calendar_sync_cb (int interface, int timeout);
extern int cvmx_ilk_enable (int interface);
extern int cvmx_ilk_disable (int interface);
extern int cvmx_ilk_get_intf_ena (int interface);
extern unsigned char cvmx_ilk_bit_count (unsigned char uc);
extern unsigned char cvmx_ilk_get_intf_ln_msk (int interface);
extern int cvmx_ilk_get_chan_info (int interface, unsigned char **chans,
                                   unsigned char *num_chan);
extern void cvmx_ilk_show_stats (int interface, cvmx_ilk_stats_ctrl_t *pstats);
extern int cvmx_ilk_cal_setup_rx (int interface, int cal_depth,
                                  cvmx_ilk_cal_entry_t *pent, int hi_wm,
                                  unsigned char cal_ena);
extern int cvmx_ilk_cal_setup_tx (int interface, int cal_depth,
                                  cvmx_ilk_cal_entry_t *pent,
                                  unsigned char cal_ena);
extern int cvmx_ilk_lpbk (int interface, cvmx_ilk_lpbk_ena_t enable,
                          cvmx_ilk_lpbk_mode_t mode);
#ifdef	__cplusplus
}
#endif

#endif  /* __CVMX_ILK_H__ */
