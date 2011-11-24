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

int dfs_bin_fixedpattern_check(struct ath_dfs_host *dfs, struct dfs_filter *rf, u_int32_t dur, int ext_chan_flag, u_int32_t ext_chan_busy)
{
    struct dfs_pulseline *pl = dfs->pulses;
    int i, n, refpri, primargin, numpulses=0;
    u_int64_t start_ts, end_ts, event_ts, prev_event_ts, next_event_ts, window_start, window_end;
    u_int32_t index, next_index, deltadur;

    /* For fixed pattern types, rf->rf_patterntype=1*/
    primargin = dfs_get_pri_margin(ext_chan_flag, (rf->rf_patterntype==1),dfs->dfs_rinfo.rn_lastfull_ts, ext_chan_busy);

    refpri = (rf->rf_minpri + rf->rf_maxpri)/2;
    index = pl->pl_lastelem;
    end_ts = pl->pl_elems[index].p_time;
    start_ts = end_ts - (refpri*rf->rf_numpulses);

    DFS_DPRINTK(dfs, ATH_DEBUG_DFS3, "lastelem ts=%llu start_ts=%llu, end_ts=%llu\n", (unsigned long long)pl->pl_elems[index].p_time, (unsigned long long)start_ts, (unsigned long long)end_ts);
    /* find the index of first element in our window of interest */
    for(i=0;i<pl->pl_numelems;i++) {
        index = (index-1) & DFS_MAX_PULSE_BUFFER_MASK;
        if(pl->pl_elems[index].p_time >= start_ts )
            continue;
        else {
            index = (index) & DFS_MAX_PULSE_BUFFER_MASK;
            break;
        }
    }
    for (n=0;n<=rf->rf_numpulses; n++) {
        window_start = (start_ts + (refpri*n))-(primargin+n);
        window_end = window_start + 2*(primargin+n);
        DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
                "window_start %u window_end %u \n",
                (u_int32_t)window_start, (u_int32_t)window_end);
        for(i=0;i<pl->pl_numelems;i++) {
            prev_event_ts = pl->pl_elems[index].p_time;
            index = (index+1) & DFS_MAX_PULSE_BUFFER_MASK;
            event_ts = pl->pl_elems[index].p_time;
            next_index = (index+1) & DFS_MAX_PULSE_BUFFER_MASK;
            next_event_ts = pl->pl_elems[next_index].p_time;
            DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
                    "ts %u \n", (u_int32_t)event_ts);
            if( (event_ts <= window_end) && (event_ts >= window_start)){
                deltadur = DFS_DIFF(pl->pl_elems[index].p_dur, dur);
                if( (pl->pl_elems[index].p_dur == 1) ||
                        ((dur != 1) && (deltadur <= 2))) {
                    numpulses++;
                    DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
                            "numpulses %u \n", numpulses);
                    break;
                }
            }
            else if( event_ts > window_end) {
                index = (index-1) & DFS_MAX_PULSE_BUFFER_MASK;
                break;
            }
            else if( event_ts == prev_event_ts) {
                if( ((next_event_ts - event_ts) > refpri) ||
                        ((next_event_ts - event_ts) == 0)) {
                    deltadur = DFS_DIFF(pl->pl_elems[index].p_dur, dur);
                    if( (pl->pl_elems[index].p_dur == 1) ||
                            ((pl->pl_elems[index].p_dur != 1) && (deltadur <= 2))) {
                        numpulses++;
                        DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
                                "zero PRI: numpulses %u \n", numpulses);
                        break;
                    }
                }
            }
        }
    }
    if (numpulses >= dfs_get_filter_threshold(rf, ext_chan_flag, dfs->dfs_rinfo.rn_lastfull_ts, ext_chan_busy)) {
        DFS_DPRINTK(dfs, ATH_DEBUG_DFS1, "%s FOUND filterID=%u numpulses=%d unadj thresh=%d\n", __func__, rf->rf_pulseid, numpulses, rf->rf_threshold);
        return 1;
    }
    else 
        return 0;
}

void
dfs_add_pulse(struct ath_dfs_host *dfs, struct dfs_filter *rf, struct dfs_event *re,
	      u_int32_t deltaT)
{
    u_int32_t index,n, window, pri;
    struct dfs_delayline *dl;

    dl = &rf->rf_dl;
    /* Circular buffer of size 2^n */
    index = (dl->dl_lastelem + 1) & DFS_MAX_DL_MASK;
    //if ((dl->dl_numelems+1) == DFS_MAX_DL_SIZE)
    if ((dl->dl_numelems) == DFS_MAX_DL_SIZE)
        dl->dl_firstelem = (dl->dl_firstelem + 1) & DFS_MAX_DL_MASK;
    else
        dl->dl_numelems++;
    dl->dl_lastelem = index;
    dl->dl_elems[index].de_time = deltaT;
    window = deltaT;
    dl->dl_elems[index].de_dur = re->re_dur;
    dl->dl_elems[index].de_rssi = re->re_rssi;

    for (n=0;n<dl->dl_numelems-1; n++) {
        index = (index-1) & DFS_MAX_DL_MASK;
        pri = dl->dl_elems[index].de_time;
        window += pri;
        if (window > rf->rf_filterlen) {
            dl->dl_firstelem = (index+1) & DFS_MAX_DL_MASK;
            dl->dl_numelems = n+1;
        }
    }
    DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
            "dl firstElem = %d  lastElem = %d\n",dl->dl_firstelem,
            dl->dl_lastelem);
}


int dfs_bin_check(struct ath_dfs_host *dfs, struct dfs_filter *rf,
                             u_int32_t deltaT, u_int32_t width, int ext_chan_flag, u_int32_t ext_chan_busy)
{
    u_int32_t refpri, refdur, searchpri, deltapri, averagerefpri;
    u_int32_t n, i, primargin, durmargin, highscore, highscoreindex;
    int score[DFS_MAX_DL_SIZE], delayindex, dindex, found=0;
    struct dfs_delayline *dl;
    u_int32_t scoreindex, lowpriindex= 0, lowpri = 0xffff;
    int numpulses=0;

    dl = &rf->rf_dl;
    if( dl->dl_numelems < (rf->rf_threshold-1)) {
        return 0; 
    }
    if( deltaT > rf->rf_filterlen)
        return 0;

    primargin = dfs_get_pri_margin(ext_chan_flag, (rf->rf_patterntype==1), 
            dfs->dfs_rinfo.rn_lastfull_ts, ext_chan_busy);


    if(rf->rf_maxdur < 10) {
        durmargin = 4;
    }
    else {
        durmargin = 6;
    }

    if( rf->rf_patterntype == 1 ){
        found = dfs_bin_fixedpattern_check(dfs, rf, width, ext_chan_flag, ext_chan_busy);
        if(found) {
            dl->dl_numelems = 0;
        }
        return found;
    }

    OS_MEMZERO(score, sizeof(int)*DFS_MAX_DL_SIZE);
    /* find out the lowest pri */
    for (n=0;n<dl->dl_numelems; n++) {
        delayindex = (dl->dl_firstelem + n) & DFS_MAX_DL_MASK;
        refpri = dl->dl_elems[delayindex].de_time;
        if( refpri == 0)
            continue;
        else if(refpri < lowpri) {
            lowpri = dl->dl_elems[delayindex].de_time;
            lowpriindex = n;
        }
    }
    /* find out the each delay element's pri score */
    for (n=0;n<dl->dl_numelems; n++) {
        delayindex = (dl->dl_firstelem + n) & DFS_MAX_DL_MASK;
        refpri = dl->dl_elems[delayindex].de_time;
        if( refpri == 0)
            continue;
        for (i=0;i<dl->dl_numelems; i++) {
            dindex = (dl->dl_firstelem + i) & DFS_MAX_DL_MASK;
            searchpri = dl->dl_elems[dindex].de_time;
            deltapri = DFS_DIFF(searchpri, refpri);
            if( deltapri < primargin)
                score[n]++;
        }
        if( score[n] > rf->rf_threshold) {
            /* we got the most possible candidate,
             * no need to continue further */
            break;
        }
    }
    /* find out the high scorer */
    highscore = 0;
    highscoreindex = 0;
    for (n=0;n<dl->dl_numelems; n++) {
        if( score[n] > highscore) {
            highscore = score[n];
            highscoreindex = n;
        }
        else if( score[n] == highscore ) {
            /*more than one pri has highscore take the least pri */
            delayindex = (dl->dl_firstelem + highscoreindex) & DFS_MAX_DL_MASK;
            dindex = (dl->dl_firstelem + n) & DFS_MAX_DL_MASK;
            if( dl->dl_elems[dindex].de_time <=
                    dl->dl_elems[delayindex].de_time ) {
                highscoreindex = n;
            }
        }
    }
    /* find the average pri of pulses around the pri of highscore or
     * the pulses around the lowest pri */
    if( highscore < 3) {
        scoreindex = lowpriindex;
    }
    else {
        scoreindex = highscoreindex;
    }
    /* We got the possible pri, save its parameters as reference */
    delayindex = (dl->dl_firstelem + scoreindex) & DFS_MAX_DL_MASK;
    refdur = dl->dl_elems[delayindex].de_dur;
    refpri = dl->dl_elems[delayindex].de_time;
    averagerefpri = 0;

    numpulses = dfs_bin_pri_check(dfs, rf, dl, score[scoreindex], refpri, refdur, ext_chan_flag, ext_chan_busy);
    if (numpulses >= dfs_get_filter_threshold(rf, ext_chan_flag, dfs->dfs_rinfo.rn_lastfull_ts, ext_chan_busy)) {
        found = 1;
        DFS_DPRINTK(dfs, ATH_DEBUG_DFS1, "ext_flag=%d MATCH filter=%u numpulses=%u thresh=%u refpri=%d primargin=%d\n", ext_chan_flag, rf->rf_pulseid, numpulses,rf->rf_threshold, refpri, primargin);     
        dfs_print_delayline(dfs, &rf->rf_dl);
        dfs_print_filter(dfs, rf);
    }
    return found;
}

int dfs_bin_pri_check(struct ath_dfs_host *dfs, struct dfs_filter *rf,
                             struct dfs_delayline *dl, u_int32_t score,
                             u_int32_t refpri, u_int32_t refdur, int ext_chan_flag, u_int32_t ext_chan_busy)
{
    u_int32_t searchpri, searchdur, searchrssi, deltapri, deltadur, averagerefpri=0;
    int primultiples[6];
    int delayindex, dindex;
    u_int32_t i, j, primargin, durmargin, highscore=score, highscoreindex=0;
    int numpulses=1;  //first pulse in the burst is most likely being filtered out based on maxfilterlen

    //Use the adjusted PRI margin to reduce false alarms
    /* For non fixed pattern types, rf->rf_patterntype=0*/
    primargin = dfs_get_pri_margin(ext_chan_flag, (rf->rf_patterntype==1),
            dfs->dfs_rinfo.rn_lastfull_ts, ext_chan_busy);


    if(rf->rf_maxdur < 10) {
        durmargin = 4;
    }
    else {
        durmargin = 6;
    }
    if( score > 1) {
        for (i=0;i<dl->dl_numelems; i++) {
            dindex = (dl->dl_firstelem + i) & DFS_MAX_DL_MASK;
            searchpri = dl->dl_elems[dindex].de_time;
            deltapri = DFS_DIFF(searchpri, refpri);
            if( deltapri < primargin)
                averagerefpri += searchpri;
        }
        refpri = (averagerefpri/score);  //average
    }
    /* Note: Following primultiple calculation should be done once per filter
     * during initialization stage (dfs_attach) and stored in its array
     * atleast for fixed frequency types like FCC Bin1 to save some CPU cycles.
     * multiplication, devide operators in the following code are left as it is
     * for readability hoping the complier will use left/right shifts wherever possible
     */
    if( refpri > rf->rf_maxpri) {
        primultiples[0] = (refpri - refdur)/2;
        primultiples[1] = refpri;
        primultiples[2] = refpri + primultiples[0];
        primultiples[3] = (refpri - refdur)*2;
        primultiples[4] = primultiples[3] + primultiples[0];
        primultiples[5] = primultiples[3] + refpri;
    }
    else {
        primultiples[0] = refpri;
        primultiples[1] = refpri + primultiples[0];
        primultiples[2] = refpri + primultiples[1];
        primultiples[3] = refpri + primultiples[2];
        primultiples[4] = refpri + primultiples[3];
        primultiples[5] = refpri + primultiples[4];
    }
    DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
            "pri0 = %d pri1 = %d pri2 = %d pri3 = %d pri4 = %d pri5 = %d\n",
            primultiples[0], primultiples[1], primultiples[2],
            primultiples[3], primultiples[4], primultiples[5]);
    DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
            "refpri = %d high score = %d index = %d numpulses = %d\n",
            refpri, highscore, highscoreindex, numpulses);
    /* Count the other delay elements that have  pri and dur with in the
     * acceptable range from the reference one */
    for (i=0; i<dl->dl_numelems; i++) {
        delayindex = (dl->dl_firstelem + i) & DFS_MAX_DL_MASK;
        searchpri = dl->dl_elems[delayindex].de_time;
        if( searchpri == 0) {
            /* This events PRI is zero, take it as a
             * valid pulse but decrement next event's PRI by refpri
             */
            dindex = (delayindex+1)& DFS_MAX_DL_MASK;
            dl->dl_elems[dindex].de_time -=  refpri;
            searchpri = refpri;
        }
        searchdur = dl->dl_elems[delayindex].de_dur;
        searchrssi = dl->dl_elems[delayindex].de_rssi;
        deltadur = DFS_DIFF(searchdur, refdur);
        for(j=0; j<6; j++) {
            deltapri = DFS_DIFF(searchpri, primultiples[j]);
            /* May need to revisit this as this increases the primargin by 5*/
            /* if( deltapri < (primargin+j)) {  */
            if( deltapri < (primargin)) {
                if( deltadur < durmargin) {
                    if( (refdur < 8) || ((refdur >=8)&&
                                (searchrssi < 250))) {

                        numpulses++;
                        DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
                                "rf->minpri=%d rf->maxpri=%d searchpri = %d index = %d numpulses = %d deltapri=%d j=%d\n",
                                rf->rf_minpri, rf->rf_maxpri, searchpri, i, numpulses, deltapri, j);
                    }
                }
                break;
            }
        }
    }
    return numpulses;
}
#endif /* ATH_SUPPORT_DFS */
