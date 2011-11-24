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
#include "dfs_target_api.h"

void dfs_radar_task (unsigned long arg)
{
    struct ath_dfs_host *dfs = (struct ath_dfs_host *)arg;
    A_INT16 chan_index; 
    A_UINT8 bangradar;

    //printk("\n%s\n",__func__);

    if(dfs_process_radarevent_host(dfs, &chan_index, &bangradar)){
        if(!bangradar){
            DFS_DPRINTK(dfs, ATH_DEBUG_DFS, "%s: Radar detected on channel idx %d\n",
                    __func__, chan_index);
        }

        /* TODO: The radar detected event is sent from host in timer
         * context which could potentially cause issues with sleepable
         * WMI. Change this to process context later. */
        DFS_RADAR_DETECTED(dfs->dev_hdl, chan_index, bangradar);
    } 

}

#endif /* ATH_SUPPORT_DFS */


