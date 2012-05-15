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
void
dfs_queue_phyerr(struct ath_dfs_host *dfs, struct dfs_event_info *ev_info);


void
dfs_process_phyerr_host(struct ath_dfs_host *dfs, WMI_DFS_PHYERR_EVENT *ev)
{
    int i;
    for(i=0; i<ev->num_events; i++)
        dfs_queue_phyerr(dfs, &ev->ev_info[i]);
}

void
dfs_queue_phyerr(struct ath_dfs_host *dfs, struct dfs_event_info *ev_info)
{
    struct dfs_event *event;
    int empty;

    ATH_DFSEVENTQ_LOCK(dfs);
    empty = STAILQ_EMPTY(&(dfs->dfs_eventq));
    ATH_DFSEVENTQ_UNLOCK(dfs);
    if (empty) {
        return;
    }

    /* XXX: Lot of common code. Optimize */

    if((ev_info->flags & EVENT_TYPE_MASK) == AR_EVENT){
       ATH_DFSEVENTQ_LOCK(dfs);
        event = STAILQ_FIRST(&(dfs->dfs_eventq));
        if (event == NULL) {
            ATH_DFSEVENTQ_UNLOCK(dfs);
            DFS_DPRINTK(dfs, ATH_DEBUG_DFS, "%s: no more events space left\n",
                    __func__);
            return;
        }
        STAILQ_REMOVE_HEAD(&(dfs->dfs_eventq), re_list);
        ATH_DFSEVENTQ_UNLOCK(dfs);
        event->re_rssi = ev_info->rssi;
        event->re_dur = ev_info->dur;
        event->re_full_ts = ev_info->full_ts;
        event->re_ts = ev_info->ts;
        event->re_chanindex = ev_info->chanindex;
        event->re_chanindextype = ev_info->flags & CH_TYPE_MASK;
        ATH_ARQ_LOCK(dfs);
        STAILQ_INSERT_TAIL(&(dfs->dfs_arq), event, re_list);
        ATH_ARQ_UNLOCK(dfs);
    }
    else { /* DFS event */
        ATH_DFSEVENTQ_LOCK(dfs);
        event = STAILQ_FIRST(&(dfs->dfs_eventq));
        if (event == NULL) {
            ATH_DFSEVENTQ_UNLOCK(dfs);
            DFS_DPRINTK(dfs, ATH_DEBUG_DFS, "%s: no more events space left\n",
                    __func__);
            return;
        }
        STAILQ_REMOVE_HEAD(&(dfs->dfs_eventq), re_list);
        ATH_DFSEVENTQ_UNLOCK(dfs);
        event->re_rssi = ev_info->rssi;
        event->re_dur = ev_info->dur;
        event->re_full_ts = ev_info->full_ts;
        event->re_ts = ev_info->ts;
        event->re_chanindex = ev_info->chanindex;
        event->re_chanindextype = ev_info->flags & CH_TYPE_MASK;
        event->re_ext_chan_busy = ev_info->ext_chan_busy;

        ATH_DFSQ_LOCK(dfs);
        STAILQ_INSERT_TAIL(&(dfs->dfs_radarq), event, re_list);
        ATH_DFSQ_UNLOCK(dfs);
    }

    /* TODO: The radar detected event is sent from host in timer
     * context which could potentially cause issues with sleepable
     * WMI. Change this to process context later. */

    A_TIMEOUT_MS(&dfs->dfs_radar_task_timer, 0, 0);

}

#endif /* ATH_SUPPORT_DFS */
