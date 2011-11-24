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


struct dfs_pulse ar6000_etsi_radars[] = {
        /* TYPE 1 */
        {15, 2,   750,  750, 0,  24, 7,  0,  2, 21,  0, -10, 0},
        /* TYPE 2 */
        {10,  5,   200, 200,   0,  24, 5,  1,  9, 21,  0, 3, 1},
        {10,  5,   300, 300,  0,  24, 8,  1,  9, 21,  0, 3, 2},
        {10,  5,   500, 500,  0,  24, 8,  1,  9, 21,  1, 3, 3},
        {10,  5,   800, 800,  0,  24, 8,  1,  9, 21,  1, 3, 4},
        {10,  5,  1001, 1001,  0,  30, 8,  1,  9, 21,  0, 3, 5},
        /* TYPE 3 */
        {15, 16,  200,   200, 0,  24, 6, 10, 19, 22, 0, 3, 6},
        {15, 16,  300,   300, 0,  24, 6, 10, 19, 22, 0, 3, 7},
        {15, 16,  503,   503, 0,  24, 7, 10, 19, 22, 0, 3, 8},
        {15, 16,  809,   809, 0,  24, 7, 10, 19, 22, 0, 3, 9},
        {15, 16, 1014,   1014, 0,  30, 7, 10, 19, 22, 0, 3, 10},
        /* TYPE 4 */
        {15, 5,  1200,   1200, 0,  24, 7,  1,  9, 21,  0, 3, 11},
        {15, 5,  1500,   1500, 0,  30, 7,  1,  9, 21,  0, 3, 12},
        {15, 5,  1600,   1600, 0,  24, 7,  1,  9, 21,  0, 3, 13},
        {15, 16,  1200,  1200, 0,  30, 7, 10,  19, 22, 0, 3, 14},
        {15, 16,  1500,  1500, 0,  24, 7, 10,  19, 22, 0, 3, 15},
        {15, 16,  1600,  1600, 0,  24, 7, 10,  19, 22, 0, 3, 16},
        /* TYPE 5 */
        {25, 5,  2305,   2305, 0,  24, 12,  1,  9, 21,  0, 3, 17},
        {25, 5,  3009,   3009, 0,  24, 12,  1,  9, 21,  0, 3, 18},
        {25, 5,  3500,   3500, 0,  24, 12,  1,  9, 21,  0, 3, 19},
        {25, 5,  4000,   4000, 0,  24, 12,  1,  9, 21,  0, 3, 20},
        {25, 16, 2300,   2300, 0,  24, 12, 10, 20, 22,  0, 3, 21},
        {25, 16, 3000,   3000, 0,  24, 12, 10, 20, 22,  0, 3, 22},
        {25, 16, 3500,   3500, 0,  24, 12, 10, 20, 22,  0, 3, 23},
        {25, 16, 3850,   3850, 0,  24, 12, 10, 20, 22,  0, 3, 24},
        /* TYPE 6 */
        {20, 25, 2000,   2000, 0,  24, 10, 20, 26, 22,  0, 3, 25},
        {20, 25, 3000,   3000, 0,  24, 10, 20, 26, 22,  0, 3, 26},
        {20, 25, 4000,   4000, 0,  24, 10, 20, 26, 22,  0, 3, 27},
        {20, 37, 2000,   2000, 0,  24, 10, 30, 36, 22,  0, 3, 28},
        {20, 37, 3000,   3000, 0,  24, 10, 30, 36, 22,  0, 3, 29},
        {20, 37, 4000,   4000, 0,  24, 10, 30, 36, 22,  0, 3, 30},
        
        /* TYPE staggered pulse */
        {20, 2, 300,    400, 2,  30, 10, 0, 2, 22,  0, 3, 31}, //0.8-2us, 2-3 bursts,300-400 PRF, 10 pulses each 
        {30, 2, 400,   1200, 2,  30, 15, 0, 2, 22,  0, 3, 32}, //0.8-2us, 2-3 bursts, 400-1200 PRF, 15 pulses each

        /* constant PRF based */ 
        {10, 5,   200,  1000, 0,  24, 6,  0,  8, 21,  0, -10, 33}, /*0.8-5us , 200-1000 PRF, 10 pulses */
        {15, 15,   200,  1600, 0,  24, 7,  0,  18, 21,  0, -10, 34}, /*0.8-15us , 200-1600 PRF, 15 pulses */
        {25, 15,   2300,  4000, 0,  24, 12,  0,  18, 21,  0, -10, 35}, /* 0.8-15 us, 2300-4000 PRF, 25 pulses*/
        {20, 30,   2000,  4000, 0,  24, 10,  19,  33, 21,  0, -10, 36}, /* 20-30us, 2000-4000 PRF, 20 pulses*/

};

/* The following are for FCC Bin 1-4 pulses */
struct dfs_pulse ar6000_fcc_radars[] = {
        /* following two filters are specific to Japan/MKK4 */
        {18,   1,  720,  720, 1,  6,   6,  0,  1, 18,  0, 3, 17}, // 1389 +/- 6 us
        {18,   4,  250,  250, 1,  10,  5,  1,  6, 18,  0, 3, 18}, // 4000 +/- 6 us
        {18,   5,  260,  260, 1,  10,  6,  1,  6, 18,  0, 3, 19}, // 3846 +/- 7 us
        /* following filters are common to both FCC and JAPAN */
        {18,  1,  325,   1930, 0,  6,  7,  0,  1, 18,  0, 3,  0}, // 1428 +/- 7 us
        {9,   1, 3003,   3003, 1,  7,  5,  0,  1, 18,  0, 0,  1}, // 333 +/- 7 us
       
        {23,  5, 6250,   6250, 0, 15, 11,  0,  7, 22,  0, 3,  2}, // 160 +/- 15 us
        {23,  5, 5263,   5263, 0, 18, 11,  0,  7, 22,  0, 3,  3}, // 190 +/- 15 us
        {23,  5, 4545,   4545, 0, 18, 11,  0,  7, 22,  0, 3,  4}, // 220 +/- 15 us
        
        {18, 10, 4444,   4444, 0, 35,  6,  7, 13, 22,  0, 3,  5}, // 225 +/- 30 us
        {18, 10, 3636,   3636, 0, 25,  6,  7, 13, 22,  0, 3,  6}, // 275 +/- 25 us
        {18, 10, 3076,   3076, 0, 25,  8,  7, 13, 22,  0, 3,  7}, // 325 +/- 25 us
        {18, 10, 2666,   2666, 0, 25,  8,  7, 13, 22,  0, 3,  8}, // 375 +/- 25 us
        {18, 10, 2352,   2352, 0, 25,  8,  7, 13, 22,  0, 3,  9}, // 425 +/- 25 us
        {18, 10, 2105,   2105, 0, 30,  8,  7, 13, 22,  0, 3, 10}, // 475 +/- 30 us
       
        {14, 15, 4444,   4444, 0, 35,  5, 13, 21, 22,  0, 3, 11}, // 225 +/- 30 us
        {14, 15, 3636,   3636, 0, 25,  5, 13, 24, 22,  0, 3, 12}, // 275 +/- 25 us
        {14, 15, 3076,   3076, 0, 25,  7, 13, 23, 22,  0, 3, 13}, // 325 +/- 25 us
        {14, 15, 2666,   2666, 0, 25,  7, 13, 23, 22,  0, 3, 14}, // 375 +/- 25 us
        {14, 15, 2352,   2352, 0, 25,  7, 13, 21, 22,  0, 3, 15}, // 425 +/- 25 us
        {12, 15, 2105,   2105, 0, 30,  7, 13, 21, 22,  0, 3, 16}, // 475 +/- 30 us
};

struct dfs_bin5pulse ar6000_bin5pulses[] = {
        {4, 28, 105, 12, 22, 5},
};

/*
 * Clear all delay lines for all filter types
 */
void dfs_reset_alldelaylines(struct ath_dfs_host *dfs)
{
    struct dfs_filtertype *ft = NULL;
    struct dfs_filter *rf;
    struct dfs_delayline *dl;
    struct dfs_pulseline *pl;
    int i,j;

    if (dfs == NULL) {
        A_PRINTF("%s: sc_dfs is NULL\n", __func__);
        return;
    }

    pl = dfs->pulses;
    /* reset the pulse log */
    pl->pl_firstelem = pl->pl_numelems = 0;
    pl->pl_lastelem = DFS_MAX_PULSE_BUFFER_MASK;

    for (i=0; i<DFS_MAX_RADAR_TYPES; i++) {
        if (dfs->dfs_radarf[i] != NULL) {
            ft = dfs->dfs_radarf[i];
            for (j=0; j<ft->ft_numfilters; j++) {
                rf = &(ft->ft_filters[j]);
                dl = &(rf->rf_dl);
                if(dl != NULL) {
                    OS_MEMZERO(dl, sizeof(struct dfs_delayline));
                    dl->dl_lastelem = (0xFFFFFFFF) & DFS_MAX_DL_MASK;
                }
            }
        }
    }
    for (i=0; i<dfs->dfs_rinfo.rn_numbin5radars; i++) {
        OS_MEMZERO(&(dfs->dfs_b5radars[i].br_elems[0]), sizeof(struct dfs_bin5elem)*DFS_MAX_B5_SIZE);
        dfs->dfs_b5radars[i].br_firstelem = 0;
        dfs->dfs_b5radars[i].br_numelems = 0;
        dfs->dfs_b5radars[i].br_lastelem = (0xFFFFFFFF)&DFS_MAX_B5_MASK;
    }
}
/*
 * Clear only a single delay line
 */

void dfs_reset_delayline(struct dfs_delayline *dl)
{
	OS_MEMZERO(&(dl->dl_elems[0]), sizeof(dl->dl_elems));
	dl->dl_lastelem = (0xFFFFFFFF)&DFS_MAX_DL_MASK;
}

void dfs_reset_filter_delaylines(struct dfs_filtertype *dft)
{
        int i;
        struct dfs_filter *df;
        for (i=0; i< DFS_MAX_NUM_RADAR_FILTERS; i++) {
                df = &dft->ft_filters[i];
                dfs_reset_delayline(&(df->rf_dl));
        }
}

void
dfs_reset_radarq(struct ath_dfs_host *dfs)
{
	struct dfs_event *event;
    if (dfs == NULL) {
        A_PRINTF("%s: sc_dfs is NULL\n", __func__);
        return;
    }
	ATH_DFSQ_LOCK(dfs);
	ATH_DFSEVENTQ_LOCK(dfs);
	while (!STAILQ_EMPTY(&(dfs->dfs_radarq))) {
		event = STAILQ_FIRST(&(dfs->dfs_radarq));
		STAILQ_REMOVE_HEAD(&(dfs->dfs_radarq), re_list);
		OS_MEMZERO(event, sizeof(struct dfs_event));
		STAILQ_INSERT_TAIL(&(dfs->dfs_eventq), event, re_list);
	}
	ATH_DFSEVENTQ_UNLOCK(dfs);
	ATH_DFSQ_UNLOCK(dfs);
}

struct dfs_pulse *dfs_get_radars(u_int32_t dfsdomain, u_int32_t *numradars, struct dfs_bin5pulse **bin5pulses, u_int32_t *numb5radars)
{
#define N(a)    (sizeof(a)/sizeof(a[0]))
    struct dfs_pulse *dfs_radars = NULL;
    switch (dfsdomain) {
        case DFS_FCC_DOMAIN:
            dfs_radars = &ar6000_fcc_radars[3];
            *numradars= N(ar6000_fcc_radars)-3;
            *bin5pulses = &ar6000_bin5pulses[0];
            *numb5radars = N(ar6000_bin5pulses);
            break;
        case DFS_ETSI_DOMAIN:
            dfs_radars = &ar6000_etsi_radars[0];
            *numradars = N(ar6000_etsi_radars);
            *bin5pulses = &ar6000_bin5pulses[0];
            *numb5radars = N(ar6000_bin5pulses);
            break;
        case DFS_MKK4_DOMAIN:
            dfs_radars = &ar6000_fcc_radars[0];
            *numradars = N(ar6000_fcc_radars);
            *bin5pulses = &ar6000_bin5pulses[0];
            *numb5radars = N(ar6000_bin5pulses);
            break;
        default:
            return NULL;
    }

    return dfs_radars;

#undef N

}

int dfs_init_radar_filters_host( struct ath_dfs_host *dfs, struct ath_dfs_info *dfs_info)
{ 
    u_int32_t T, Tmax;
    int numpulses,p,n, i;
    struct dfs_filtertype *ft = NULL;
    struct dfs_filter *rf=NULL;
    struct dfs_pulse *dfs_radars;
    struct dfs_bin5pulse *b5pulses;
    int32_t min_rssithresh=DFS_MAX_RSSI_VALUE;
    u_int32_t max_pulsedur=0;
    u_int32_t numb5radars;
    u_int32_t numradars;

    dfs_radars = dfs_get_radars(dfs_info->dfs_domain, &numradars, &b5pulses, &numb5radars);
    /* If DFS not enabled return immediately.*/
    if (!dfs_radars) {
        return 0;
    }

    dfs->dfs_rinfo.rn_numradars = 0;

    /* Clear filter type table */
    for (n=0; n<256; n++) {
        for (i=0;i<DFS_MAX_RADAR_OVERLAP; i++)
            (dfs->dfs_radartable[n])[i] = -1;
    }

    /* Now, initialize the radar filters */
    for (p=0; p < numradars; p++) {
        ft = NULL;
        for (n=0; n<dfs->dfs_rinfo.rn_numradars; n++) {
            if ((dfs_radars[p].rp_pulsedur == dfs->dfs_radarf[n]->ft_filterdur) &&
                    (dfs_radars[p].rp_numpulses == dfs->dfs_radarf[n]->ft_numpulses) &&
                    (dfs_radars[p].rp_mindur == dfs->dfs_radarf[n]->ft_mindur) &&
                    (dfs_radars[p].rp_maxdur == dfs->dfs_radarf[n]->ft_maxdur)) {
                ft = dfs->dfs_radarf[n];
                break;
            }
        }
        if (ft == NULL) {
            /* No filter of the appropriate dur was found */
            if ((dfs->dfs_rinfo.rn_numradars+1) >DFS_MAX_RADAR_TYPES) {
                DFS_DPRINTK(dfs, ATH_DEBUG_DFS, "%s: Too many filter types\n",
                        __func__);
                goto bad4;
            }

            ft = dfs->dfs_radarf[dfs->dfs_rinfo.rn_numradars];
            ft->ft_numfilters = 0;
            ft->ft_numpulses = dfs_radars[p].rp_numpulses;
            ft->ft_patterntype = dfs_radars[p].rp_patterntype;
            ft->ft_mindur = dfs_radars[p].rp_mindur;
            ft->ft_maxdur = dfs_radars[p].rp_maxdur;
            ft->ft_filterdur = dfs_radars[p].rp_pulsedur;
            ft->ft_rssithresh = dfs_radars[p].rp_rssithresh;
            ft->ft_rssimargin = dfs_radars[p].rp_rssimargin;
            ft->ft_minpri = 1000000;

            if (ft->ft_rssithresh < min_rssithresh)
                min_rssithresh = ft->ft_rssithresh;
            if (ft->ft_maxdur > max_pulsedur)
                max_pulsedur = ft->ft_maxdur;
            for (i=ft->ft_mindur; i<=ft->ft_maxdur; i++) {
                u_int32_t stop=0,tableindex=0;
                while ((tableindex < DFS_MAX_RADAR_OVERLAP) && (!stop)) {
                    if ((dfs->dfs_radartable[i])[tableindex] == -1)
                        stop = 1;
                    else
                        tableindex++;
                }
                if (stop) {
                    (dfs->dfs_radartable[i])[tableindex] =
                        (int8_t) (dfs->dfs_rinfo.rn_numradars);
                } else {
                    DFS_DPRINTK(dfs, ATH_DEBUG_DFS,
                            "%s: Too many overlapping radar filters\n",
                            __func__);
                    goto bad4;
                }
            }
            dfs->dfs_rinfo.rn_numradars++;
        }
        rf = &(ft->ft_filters[ft->ft_numfilters++]);
        dfs_reset_delayline(&rf->rf_dl);
        numpulses = dfs_radars[p].rp_numpulses;

        rf->rf_numpulses = numpulses;
        rf->rf_patterntype = dfs_radars[p].rp_patterntype;
        rf->rf_pulseid = dfs_radars[p].rp_pulseid;
        rf->rf_mindur = dfs_radars[p].rp_mindur;
        rf->rf_maxdur = dfs_radars[p].rp_maxdur;
        rf->rf_numpulses = dfs_radars[p].rp_numpulses;

        T = (100000000/dfs_radars[p].rp_max_pulsefreq) -
            100*(dfs_radars[p].rp_meanoffset);
        rf->rf_minpri =
            dfs_round((int32_t)T - (100*(dfs_radars[p].rp_pulsevar)));
        Tmax = (100000000/dfs_radars[p].rp_pulsefreq) -
            100*(dfs_radars[p].rp_meanoffset);
        rf->rf_maxpri =
            dfs_round((int32_t)Tmax + (100*(dfs_radars[p].rp_pulsevar)));

        if( rf->rf_minpri < ft->ft_minpri )
            ft->ft_minpri = rf->rf_minpri;

        rf->rf_threshold = dfs_radars[p].rp_threshold;
        rf->rf_filterlen = rf->rf_maxpri * rf->rf_numpulses;

        DFS_DPRINTK(dfs, ATH_DEBUG_DFS2, "minprf = %d maxprf = %d pulsevar = %d thresh=%d\n",
                dfs_radars[p].rp_pulsefreq, dfs_radars[p].rp_max_pulsefreq, dfs_radars[p].rp_pulsevar, rf->rf_threshold);
        DFS_DPRINTK(dfs, ATH_DEBUG_DFS2,
                "minpri = %d maxpri = %d filterlen = %d filterID = %d\n",
                rf->rf_minpri, rf->rf_maxpri, rf->rf_filterlen, rf->rf_pulseid);
    }

#ifdef DFS_DEBUG
    dfs_print_filters(dfs);
#endif

    dfs->dfs_rinfo.rn_numbin5radars  = numb5radars;
    if ( dfs->dfs_b5radars == NULL ) {
        dfs->dfs_b5radars = (struct dfs_bin5radars *)DFS_MALLOC(dfs->os_hdl, numb5radars * sizeof(struct dfs_bin5radars));
        if (dfs->dfs_b5radars == NULL) {
            DFS_DPRINTK(dfs, ATH_DEBUG_DFS, 
                    "%s: cannot allocate memory for bin5 radars\n",
                    __func__);
            goto bad4;
        }
    }
    for (n=0; n<numb5radars; n++) {
        dfs->dfs_b5radars[n].br_pulse = b5pulses[n];
        dfs->dfs_b5radars[n].br_pulse.b5_timewindow *= 1000000;
        if (dfs->dfs_b5radars[n].br_pulse.b5_rssithresh < min_rssithresh)
            min_rssithresh = dfs->dfs_b5radars[n].br_pulse.b5_rssithresh;
        if (dfs->dfs_b5radars[n].br_pulse.b5_maxdur > max_pulsedur )
            max_pulsedur = dfs->dfs_b5radars[n].br_pulse.b5_maxdur;
    }
    dfs_reset_alldelaylines(dfs);
    dfs_reset_radarq(dfs);

    DFS_SET_MINRSSITHRESH(dfs->dev_hdl, min_rssithresh);
    DFS_SET_MAXPULSEDUR(dfs->dev_hdl, dfs_round((int32_t)((max_pulsedur*100/80)*100)));

    
    return 0;

bad4:
    for (n=0; n<ft->ft_numfilters; n++) {
        rf = &(ft->ft_filters[n]);
    }
    return 1;
}
 


#endif /* ATH_SUPPORT_DFS */
