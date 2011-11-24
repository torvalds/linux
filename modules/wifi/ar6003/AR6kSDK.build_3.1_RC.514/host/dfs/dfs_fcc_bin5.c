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


int dfs_bin5_addpulse(struct ath_dfs_host *dfs, struct dfs_bin5radars *br,
		  struct dfs_event *re, u_int64_t thists)
{
	u_int32_t index,stop;
	u_int64_t tsDelta;

	/* Check if this pulse is a valid pulse in terms of repetition, 
	 * if not, return without adding it to the queue.
	 * PRI : Pulse Repitetion Interval
	 * BRI : Burst Repitetion Interval */ 
	if( br->br_numelems != 0){
		index = br->br_lastelem;
		tsDelta = thists - br->br_elems[index].be_ts;
		if( (tsDelta < DFS_BIN5_PRI_LOWER_LIMIT) ||
			( (tsDelta > DFS_BIN5_PRI_HIGHER_LIMIT) &&
			  (tsDelta < DFS_BIN5_BRI_LOWER_LIMIT))) {
			    return 0;
		}
	}
	/* Circular buffer of size 2^n */
	index = (br->br_lastelem +1) & DFS_MAX_B5_MASK;
	br->br_lastelem = index;
        if (br->br_numelems == DFS_MAX_B5_SIZE)
		br->br_firstelem = (br->br_firstelem+1)&DFS_MAX_B5_MASK;
	else
		br->br_numelems++;
	br->br_elems[index].be_ts = thists;
	br->br_elems[index].be_rssi = re->re_rssi;
	br->br_elems[index].be_dur = re->re_dur;
	stop = 0;
	index = br->br_firstelem;
	while ((!stop) && (br->br_numelems-1) > 0) {
		if ((thists - br->br_elems[index].be_ts) > 
		    ((u_int64_t) br->br_pulse.b5_timewindow)) {
			br->br_numelems--;
			br->br_firstelem = (br->br_firstelem +1) & DFS_MAX_B5_MASK;
			index = br->br_firstelem;
		} else
			stop = 1;
	}
	return 1;
}

/*
 * If the dfs structure is NULL (which should be illegal if everyting is working
 * properly, then signify that a bin5 radar was found
 */

int dfs_bin5_check(struct ath_dfs_host *dfs)
{
    struct dfs_bin5radars *br;
    int index[DFS_MAX_B5_SIZE];
    u_int32_t n, i, this, prev, rssi_diff, width_diff, bursts= 0;
    u_int32_t total_diff=0, average_diff, total_width=0, average_width, numevents=0;
    u_int64_t pri;

    if (dfs == NULL) {
        A_PRINTF("%s: sc_dfs is NULL\n", __func__);
        return 1;
    }
    for (n=0;n<dfs->dfs_rinfo.rn_numbin5radars; n++) {
        br = &(dfs->dfs_b5radars[n]);
        DFS_DPRINTK(dfs, ATH_DEBUG_DFS1,
                "Num elems = %d\n", br->br_numelems);
        prev = br->br_firstelem;
        for(i=0;i<br->br_numelems;i++){
            this = ((br->br_firstelem +i) & DFS_MAX_B5_MASK);
            /* Rule 1: 1000 <= PRI <= 2000 + some margin */
            if( br->br_elems[this].be_ts >= br->br_elems[prev].be_ts ) {
                pri = br->br_elems[this].be_ts - br->br_elems[prev].be_ts;
            }
            else {//roll over case
                //pri = (0xffffffffffffffff - br->br_elems[prev].be_ts) + br->br_elems[this].be_ts;
                pri = br->br_elems[this].be_ts;
            }
            DFS_DPRINTK(dfs, ATH_DEBUG_DFS2," pri=%llu this.ts=%llu prev.ts=%llu\n", pri, br->br_elems[this].be_ts, br->br_elems[prev].be_ts);
            if(( (pri >= DFS_BIN5_PRI_LOWER_LIMIT) && (pri <= DFS_BIN5_PRI_HIGHER_LIMIT))) {  //pri: pulse repitition interval in us
                /* Rule 2: pulse width of the pulses in the burst should be same (+/- margin) */
                if( br->br_elems[this].be_dur >= br->br_elems[prev].be_dur) {
                    width_diff = br->br_elems[this].be_dur - br->br_elems[prev].be_dur;
                }
                else {
                    width_diff = br->br_elems[prev].be_dur - br->br_elems[this].be_dur;
                }
                if( width_diff <= DFS_BIN5_WIDTH_MARGIN ) {
                    /* Rule 3: RSSI of the pulses in the burst should be same (+/- margin) */
                    if( br->br_elems[this].be_rssi >= br->br_elems[prev].be_rssi) {
                        rssi_diff = br->br_elems[this].be_rssi - br->br_elems[prev].be_rssi;
                    }
                    else {
                        rssi_diff = br->br_elems[prev].be_rssi - br->br_elems[this].be_rssi;
                    }
                    if( rssi_diff <= DFS_BIN5_RSSI_MARGIN ) {
                        bursts++;
                        /* Save the indexes of this pair for later width variance check */
                        if( numevents >= 2 ) {
                            /* make sure the event is not duplicated,
                             * possible in a 3 pulse burst */
                            if( index[numevents-1] != prev) {
                                index[numevents++] = prev;
                            }
                        }
                        else {
                            index[numevents++] = prev;                                                }
                        index[numevents++] = this;
                    } else {
                        DFS_DPRINTK(dfs,ATH_DEBUG_DFS2,"%s %d Bin5 rssi_diff=%d\n", __func__, __LINE__, rssi_diff);
                    }
                } else {
                    DFS_DPRINTK(dfs,ATH_DEBUG_DFS2,"%s %d Bin5 width_diff=%d\n", __func__, __LINE__, width_diff);
                }
            } else {
                DFS_DPRINTK(dfs,ATH_DEBUG_DFS2,"%s %d Bin5 PRI check fail pri=%llu\n", __func__, __LINE__,pri);
            }
            prev = this;
        }

        DFS_DPRINTK(dfs, ATH_DEBUG_DFS2, "bursts=%u numevents=%u\n", bursts, numevents);
        if ( bursts >= br->br_pulse.b5_threshold) {
            if( (br->br_elems[br->br_lastelem].be_ts - br->br_elems[br->br_firstelem].be_ts) < 3000000 ) {
                return 0;
            }
            else {
                for (i=0; i<numevents; i++){
                    total_width += br->br_elems[index[i]].be_dur;
                }
                average_width = total_width/numevents;
                for (i=0; i<numevents; i++){
                    total_diff += DFS_DIFF(br->br_elems[index[i]].be_dur, average_width);
                }
                average_diff = total_diff/numevents;
                if( average_diff > DFS_BIN5_WIDTH_MARGIN ) {
                    return 1;
                } else {

                    DFS_DPRINTK(dfs, ATH_DEBUG_DFS2, "bursts=%u numevents=%u total_width=%d average_width=%d total_diff=%d average_diff=%d\n", bursts, numevents, total_width, average_width, total_diff, average_diff);

                }

            }
        }
    }

    return 0;
}


u_int8_t dfs_retain_bin5_burst_pattern(struct ath_dfs_host *dfs, u_int32_t diff_ts, u_int8_t old_dur)
{

    // Pulses may get split into 2 during chirping, this print is only to show that it happened, we do not handle this condition if we cannot detect the chirping
    // SPLIT pulses will have a time stamp difference of < 50
    if (diff_ts < 50) {
	DFS_DPRINTK(dfs,ATH_DEBUG_DFS1,"%s SPLIT pulse diffTs=%u dur=%d (old_dur=%d)\n", __func__, diff_ts, dfs->dfs_rinfo.dfs_last_bin5_dur, old_dur);
    }
    // Check if this is the 2nd or 3rd pulse in the same burst, PRI will be between 1000 and 2000 us
    if(((diff_ts >= DFS_BIN5_PRI_LOWER_LIMIT) && (diff_ts <= DFS_BIN5_PRI_HIGHER_LIMIT))) {

        //This pulse belongs to the same burst as the pulse before, so return the same random duration for it
	DFS_DPRINTK(dfs,ATH_DEBUG_DFS1,"%s this pulse belongs to the same burst as before, give it same dur=%d (old_dur=%d)\n", __func__, dfs->dfs_rinfo.dfs_last_bin5_dur, old_dur);

        return (dfs->dfs_rinfo.dfs_last_bin5_dur);
    }
    // This pulse does not belong to this burst, return unchanged duration
    return old_dur;
}


#endif /* ATH_SUPPORT_DFS */
