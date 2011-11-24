/*
 * Copyright (c) 2002-2010, Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef ATH_SUPPORT_DFS
#include "dfs_host.h"

int adjust_pri_per_chan_busy(int ext_chan_busy, int pri_margin)
{
    int adjust_pri=0;

    if(ext_chan_busy > DFS_EXT_CHAN_LOADING_THRESH) {

       adjust_pri = (ext_chan_busy - DFS_EXT_CHAN_LOADING_THRESH) * (pri_margin);
       adjust_pri /= 100;

    }
    return adjust_pri;
}

int adjust_thresh_per_chan_busy(int ext_chan_busy, int thresh)
{
    int adjust_thresh=0;

    if(ext_chan_busy > DFS_EXT_CHAN_LOADING_THRESH) {

       adjust_thresh = (ext_chan_busy - DFS_EXT_CHAN_LOADING_THRESH) * thresh;
       adjust_thresh /= 100;

    }
    return adjust_thresh;
}
/* For the extension channel, if legacy traffic is present, we see a lot of false alarms, 
so make the PRI margin narrower depending on the busy % for the extension channel.*/

int dfs_get_pri_margin(int is_extchan_detect, int is_fixed_pattern, u_int64_t lastfull_ts, u_int32_t ext_chan_busy)
{

    int adjust_pri=0;
    int pri_margin;
    //    struct ath_dfs_target *dfs = sc->sc_dfs_tgt;

    if (is_fixed_pattern)
        pri_margin = DFS_DEFAULT_FIXEDPATTERN_PRI_MARGIN;
    else
        pri_margin = DFS_DEFAULT_PRI_MARGIN;


    /*XXX: Does cached value make sense here? */
#if 0
    if(ext_chan_busy) {
        dfs->dfs_rinfo.ext_chan_busy_ts = ath_hal_gettsf64(sc->sc_ah);
        dfs->dfs_rinfo.dfs_ext_chan_busy = ext_chan_busy;
    } else {
        // Check to see if the cached value of ext_chan_busy can be used
        if (dfs->dfs_rinfo.dfs_ext_chan_busy) {
            if (lastfull_ts < dfs->dfs_rinfo.ext_chan_busy_ts) {
                ext_chan_busy = dfs->dfs_rinfo.dfs_ext_chan_busy; 
                DFS_DPRINTK(dfs, ATH_DEBUG_DFS2," PRI Use cached copy of ext_chan_busy extchanbusy=%d \n", ext_chan_busy);
            }
        }
    }
#endif
    adjust_pri = adjust_pri_per_chan_busy(ext_chan_busy, pri_margin);

    pri_margin -= adjust_pri;
    return pri_margin;
}

/* For the extension channel, if legacy traffic is present, we see a lot of false alarms, 
so make the thresholds higher depending on the busy % for the extension channel.*/

int dfs_get_filter_threshold(struct dfs_filter *rf, int is_extchan_detect, u_int64_t lastfull_ts, u_int32_t ext_chan_busy)
{
    int thresh, adjust_thresh=0;
    //    struct ath_dfs_target *dfs = sc->sc_dfs_tgt;

    thresh = rf->rf_threshold;    

    /*XXX: Does cached value make sense here? */
#if 0
    if(ext_chan_busy) {
        dfs->dfs_rinfo.ext_chan_busy_ts = ath_hal_gettsf64(sc->sc_ah);
        dfs->dfs_rinfo.dfs_ext_chan_busy = ext_chan_busy;
    } else {
        // Check to see if the cached value of ext_chan_busy can be used
        if (dfs->dfs_rinfo.dfs_ext_chan_busy) {
            if (lastfull_ts < dfs->dfs_rinfo.ext_chan_busy_ts) {
                ext_chan_busy = dfs->dfs_rinfo.dfs_ext_chan_busy; 
                DFS_DPRINTK(dfs, ATH_DEBUG_DFS2," THRESH Use cached copy of ext_chan_busy extchanbusy=%d lastfull_ts=%llu ext_chan_busy_ts=%llu\n", ext_chan_busy ,lastfull_ts, dfs->dfs_rinfo.ext_chan_busy_ts);
            }
        }
    }
#endif
    adjust_thresh = adjust_thresh_per_chan_busy(ext_chan_busy, thresh);

    //DFS_DPRINTK(dfs, ATH_DEBUG_DFS2," filterID=%d extchanbusy=%d adjust_thresh=%d\n", rf->rf_pulseid, ext_chan_busy, adjust_thresh);

    thresh += adjust_thresh;
    return thresh;
}






#endif /* ATH_SUPPORT_DFS */
