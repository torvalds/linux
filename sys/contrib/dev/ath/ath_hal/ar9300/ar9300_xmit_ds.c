/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "opt_ah.h"

#include "ah.h"
#include "ah_desc.h"
#include "ah_internal.h"

#include "ar9300/ar9300desc.h"
#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300phy.h"
#include "ah_devid.h"

#if AH_BYTE_ORDER == AH_BIG_ENDIAN
static void ar9300_swap_tx_desc(void *ds);
#endif

void
ar9300_tx_req_intr_desc(struct ath_hal *ah, void *ds)
{
    HALDEBUG(ah, HAL_DEBUG_INTERRUPT,
        "%s:Desc Interrupt not supported\n", __func__);
}

static inline u_int16_t
ar9300_calc_ptr_chk_sum(struct ar9300_txc *ads)
{
    u_int checksum;
    u_int16_t ptrchecksum;

    /* checksum = __bswap32(ads->ds_info) + ads->ds_link */
    checksum =    ads->ds_info + ads->ds_link
                + ads->ds_data0 + ads->ds_ctl3
                + ads->ds_data1 + ads->ds_ctl5
                + ads->ds_data2 + ads->ds_ctl7
                + ads->ds_data3 + ads->ds_ctl9;

    ptrchecksum = ((checksum & 0xffff) + (checksum >> 16)) & AR_tx_ptr_chk_sum;
    return ptrchecksum;
}

HAL_BOOL
ar9300_fill_tx_desc(
    struct ath_hal *ah,
    void *ds,
    HAL_DMA_ADDR *buf_addr,
    u_int32_t *seg_len,
    u_int desc_id,
    u_int qcu,
    HAL_KEY_TYPE key_type,
    HAL_BOOL first_seg,
    HAL_BOOL last_seg,
    const void *ds0)
{
    struct ar9300_txc *ads = AR9300TXC(ds);
    short desclen;

    /* Fill TXC info field */
    desclen = (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) ? 0x18 : 0x17;
    ads->ds_info = TXC_INFO(qcu, desclen);

    /* Set the buffer addresses */
    ads->ds_data0 = buf_addr[0];
    ads->ds_data1 = buf_addr[1];
    ads->ds_data2 = buf_addr[2];
    ads->ds_data3 = buf_addr[3];

    /* Set the buffer lengths */
    ads->ds_ctl3 = (seg_len[0] << AR_buf_len_S) & AR_buf_len;
    ads->ds_ctl5 = (seg_len[1] << AR_buf_len_S) & AR_buf_len;
    ads->ds_ctl7 = (seg_len[2] << AR_buf_len_S) & AR_buf_len;
    ads->ds_ctl9 = (seg_len[3] << AR_buf_len_S) & AR_buf_len;

    /* Fill in pointer checksum and descriptor id */
    ads->ds_ctl10 = (desc_id << AR_tx_desc_id_S) | ar9300_calc_ptr_chk_sum(ads);

    if (first_seg) {
        /*
         * First descriptor, don't clobber xmit control data
         * setup by ar9300_set_11n_tx_desc.
         *
         * Note: AR_encr_type is already setup in the first descriptor by
         *       set_11n_tx_desc().
         */
        ads->ds_ctl12 |= (last_seg ? 0 : AR_tx_more);
    } else if (last_seg) { /* !first_seg && last_seg */
        /*
         * Last descriptor in a multi-descriptor frame,
         * copy the multi-rate transmit parameters from
         * the first frame for processing on completion.
         */
        ads->ds_ctl11 = 0;
        ads->ds_ctl12 = 0;
#ifdef AH_NEED_DESC_SWAP
        ads->ds_ctl13 = __bswap32(AR9300TXC_CONST(ds0)->ds_ctl13);
        ads->ds_ctl14 = __bswap32(AR9300TXC_CONST(ds0)->ds_ctl14);
        ads->ds_ctl17 = __bswap32(SM(key_type, AR_encr_type));
#else
        ads->ds_ctl13 = AR9300TXC_CONST(ds0)->ds_ctl13;
        ads->ds_ctl14 = AR9300TXC_CONST(ds0)->ds_ctl14;
        ads->ds_ctl17 = SM(key_type, AR_encr_type);
#endif
    } else { /* !first_seg && !last_seg */
        /*
         * XXX Intermediate descriptor in a multi-descriptor frame.
         */
        ads->ds_ctl11 = 0;
        ads->ds_ctl12 = AR_tx_more;
        ads->ds_ctl13 = 0;
        ads->ds_ctl14 = 0;
        ads->ds_ctl17 = SM(key_type, AR_encr_type);
    }

    /* Only relevant for Jupiter/Aphrodite */
    ads->ds_ctl23 = 0;

    return AH_TRUE;
}

void
ar9300_set_desc_link(struct ath_hal *ah, void *ds, u_int32_t link)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    ads->ds_link = link;

    /* TODO - checksum is calculated twice for subframes
     * Once in filldesc and again when linked. Need to fix.
     */
    /* Fill in pointer checksum.  Preserve descriptor id */
    ads->ds_ctl10 &= ~AR_tx_ptr_chk_sum;
    ads->ds_ctl10 |= ar9300_calc_ptr_chk_sum(ads);
}

void
ar9300_get_desc_link_ptr(struct ath_hal *ah, void *ds, u_int32_t **link)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    *link = &ads->ds_link;
}

void
ar9300_clear_tx_desc_status(struct ath_hal *ah, void *ds)
{
    struct ar9300_txs *ads = AR9300TXS(ds);
    ads->status1 = ads->status2 = 0;
    ads->status3 = ads->status4 = 0;
    ads->status5 = ads->status6 = 0;
    ads->status7 = ads->status8 = 0;
}

#ifdef ATH_SWRETRY
void
ar9300_clear_dest_mask(struct ath_hal *ah, void *ds)
{
    struct ar9300_txc *ads = AR9300TXC(ds);
    ads->ds_ctl11 |= AR_clr_dest_mask;
}
#endif

#if AH_BYTE_ORDER == AH_BIG_ENDIAN
/* XXX what words need swapping */
/* Swap transmit descriptor */
static __inline void
ar9300_swap_tx_desc(void *dsp)
{
    struct ar9300_txs *ds = (struct ar9300_txs *)dsp;

    ds->ds_info = __bswap32(ds->ds_info);
    ds->status1 = __bswap32(ds->status1);
    ds->status2 = __bswap32(ds->status2);
    ds->status3 = __bswap32(ds->status3);
    ds->status4 = __bswap32(ds->status4);
    ds->status5 = __bswap32(ds->status5);
    ds->status6 = __bswap32(ds->status6);
    ds->status7 = __bswap32(ds->status7);
    ds->status8 = __bswap32(ds->status8);
}
#endif


/*
 * Extract the transmit rate code.
 */
void
ar9300_get_tx_rate_code(struct ath_hal *ah, void *ds, struct ath_tx_status *ts)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    switch (ts->ts_finaltsi) {
    case 0:
        ts->ts_rate = MS(ads->ds_ctl14, AR_xmit_rate0);
        break;
    case 1:
        ts->ts_rate = MS(ads->ds_ctl14, AR_xmit_rate1);
        break;
    case 2:
        ts->ts_rate = MS(ads->ds_ctl14, AR_xmit_rate2);
        break;
    case 3:
        ts->ts_rate = MS(ads->ds_ctl14, AR_xmit_rate3);
        break;
    }

    ar9300_set_selfgenrate_limit(ah, ts->ts_rate);
}

/*
 * Get TX Status descriptor contents.
 */
void
ar9300_get_raw_tx_desc(struct ath_hal *ah, u_int32_t *txstatus)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_txs *ads;

    ads = &ahp->ts_ring[ahp->ts_tail];

    OS_MEMCPY(txstatus, ads, sizeof(struct ar9300_txs));
}

/*
 * Processing of HW TX descriptor.
 */
HAL_STATUS
ar9300_proc_tx_desc(struct ath_hal *ah, void *txstatus)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_txs *ads;
    struct ath_tx_status *ts = (struct ath_tx_status *)txstatus;
    u_int32_t dsinfo;

    ads = &ahp->ts_ring[ahp->ts_tail];

    if ((ads->status8 & AR_tx_done) == 0) {
        return HAL_EINPROGRESS;
    }

    /*
     * Sanity check
     */

#if 0
    ath_hal_printf(ah,
        "CHH: tail=%d\n", ahp->ts_tail);
    ath_hal_printf(ah,
        "CHH: ds_info 0x%x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
        ads->ds_info,
        ads->status1,
        ads->status2,
        ads->status3,
        ads->status4,
        ads->status5,
        ads->status6,
        ads->status7,
        ads->status8);
#endif


    /* Increment the tail to point to the next status element. */
    ahp->ts_tail = (ahp->ts_tail + 1) & (ahp->ts_size-1);

    /*
    ** For big endian systems, ds_info is not swapped as the other
    ** registers are.  Ensure we use the bswap32 version (which is
    ** defined to "nothing" in little endian systems
    */

    dsinfo = ads->ds_info;

    if ((MS(dsinfo, AR_desc_id) != ATHEROS_VENDOR_ID) ||
        (MS(dsinfo, AR_tx_rx_desc) != 1))
    {
        HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE, "%s: Tx Descriptor error %x\n",
                 __func__, dsinfo);
        HALASSERT(0);
        /* Zero out the status for reuse */
        OS_MEMZERO(ads, sizeof(struct ar9300_txs));
        return HAL_EIO;
    }

    /* Update software copies of the HW status */
    ts->ts_queue_id = MS(dsinfo, AR_tx_qcu_num);
    ts->ts_desc_id = MS(ads->status1, AR_tx_desc_id);
    ts->ts_seqnum = MS(ads->status8, AR_seq_num);
    ts->ts_tstamp = ads->status4;
    ts->ts_status = 0;
    ts->ts_flags  = 0;

    if (ads->status3 & AR_excessive_retries) {
        ts->ts_status |= HAL_TXERR_XRETRY;
    }
    if (ads->status3 & AR_filtered) {
        ts->ts_status |= HAL_TXERR_FILT;
    }
    if (ads->status3 & AR_fifounderrun) {
        ts->ts_status |= HAL_TXERR_FIFO;
        ar9300_update_tx_trig_level(ah, AH_TRUE);
    }
    if (ads->status8 & AR_tx_op_exceeded) {
        ts->ts_status |= HAL_TXERR_XTXOP;
    }
    if (ads->status3 & AR_tx_timer_expired) {
        ts->ts_status |= HAL_TXERR_TIMER_EXPIRED;
    }
    if (ads->status3 & AR_desc_cfg_err) {
        ts->ts_flags |= HAL_TX_DESC_CFG_ERR;
    }
    if (ads->status3 & AR_tx_data_underrun) {
        ts->ts_flags |= HAL_TX_DATA_UNDERRUN;
        ar9300_update_tx_trig_level(ah, AH_TRUE);
    }
    if (ads->status3 & AR_tx_delim_underrun) {
        ts->ts_flags |= HAL_TX_DELIM_UNDERRUN;
        ar9300_update_tx_trig_level(ah, AH_TRUE);
    }
    if (ads->status2 & AR_tx_ba_status) {
        ts->ts_flags |= HAL_TX_BA;
        ts->ts_ba_low = ads->status5;
        ts->ts_ba_high = ads->status6;
    }
    if (ads->status8 & AR_tx_fast_ts) {
        ts->ts_flags |= HAL_TX_FAST_TS;
    }

    /*
     * Extract the transmit rate.
     */
    ts->ts_finaltsi = MS(ads->status8, AR_final_tx_idx);

    ts->ts_rssi = MS(ads->status7, AR_tx_rssi_combined);
    ts->ts_rssi_ctl[0] = MS(ads->status2, AR_tx_rssi_ant00);
    ts->ts_rssi_ctl[1] = MS(ads->status2, AR_tx_rssi_ant01);
    ts->ts_rssi_ctl[2] = MS(ads->status2, AR_tx_rssi_ant02);
    ts->ts_rssi_ext[0] = MS(ads->status7, AR_tx_rssi_ant10);
    ts->ts_rssi_ext[1] = MS(ads->status7, AR_tx_rssi_ant11);
    ts->ts_rssi_ext[2] = MS(ads->status7, AR_tx_rssi_ant12);
    ts->ts_shortretry = MS(ads->status3, AR_rts_fail_cnt);
    ts->ts_longretry = MS(ads->status3, AR_data_fail_cnt);
    ts->ts_virtcol = MS(ads->status3, AR_virt_retry_cnt);
    ts->ts_antenna = 0;

    /* extract TID from block ack */
    ts->ts_tid = MS(ads->status8, AR_tx_tid);

    /* Zero out the status for reuse */
    OS_MEMZERO(ads, sizeof(struct ar9300_txs));

    return HAL_OK;
}

/*
 * Calculate air time of a transmit packet
 * if comp_wastedt is 1, calculate air time only for failed subframes
 * this is required for VOW_DCS ( dynamic channel selection )
 */
u_int32_t
ar9300_calc_tx_airtime(struct ath_hal *ah, void *ds, struct ath_tx_status *ts, 
        HAL_BOOL comp_wastedt, u_int8_t nbad, u_int8_t nframes )
{
    struct ar9300_txc *ads = AR9300TXC(ds);
    int finalindex_tries;
    u_int32_t airtime, lastrate_dur;
    

    /*
     * Number of attempts made on the final index
     * Note: If no BA was recv, then the data_fail_cnt is the number of tries
     * made on the final index.  If BA was recv, then add 1 to account for the
     * successful attempt.
     */
    if ( !comp_wastedt ){
        finalindex_tries = ts->ts_longretry + (ts->ts_flags & HAL_TX_BA)? 1 : 0;
    } else {
        finalindex_tries = ts->ts_longretry ;
    }

    /*
     * Calculate time of transmit on air for packet including retries
     * at different rates.
     */
    switch (ts->ts_finaltsi) {
    case 0:
        lastrate_dur = MS(ads->ds_ctl15, AR_packet_dur0);
        airtime = (lastrate_dur * finalindex_tries);
        break;
    case 1:
        lastrate_dur = MS(ads->ds_ctl15, AR_packet_dur1);
        airtime = (lastrate_dur * finalindex_tries) +
            (MS(ads->ds_ctl13, AR_xmit_data_tries0) *
             MS(ads->ds_ctl15, AR_packet_dur0));
        break;
    case 2:
        lastrate_dur = MS(ads->ds_ctl16, AR_packet_dur2);
        airtime = (lastrate_dur * finalindex_tries) +
            (MS(ads->ds_ctl13, AR_xmit_data_tries1) *
             MS(ads->ds_ctl15, AR_packet_dur1)) +
            (MS(ads->ds_ctl13, AR_xmit_data_tries0) *
             MS(ads->ds_ctl15, AR_packet_dur0));
        break;
    case 3:
        lastrate_dur = MS(ads->ds_ctl16, AR_packet_dur3);
        airtime = (lastrate_dur * finalindex_tries) +
            (MS(ads->ds_ctl13, AR_xmit_data_tries2) *
             MS(ads->ds_ctl16, AR_packet_dur2)) +
            (MS(ads->ds_ctl13, AR_xmit_data_tries1) *
             MS(ads->ds_ctl15, AR_packet_dur1)) +
            (MS(ads->ds_ctl13, AR_xmit_data_tries0) *
             MS(ads->ds_ctl15, AR_packet_dur0));
        break;
    default:
        HALASSERT(0);
        return 0;
    }

    if ( comp_wastedt && (ts->ts_flags & HAL_TX_BA)){
        airtime += nbad?((lastrate_dur*nbad) / nframes):0;  
    }
    return airtime;

}

#ifdef AH_PRIVATE_DIAG
void
ar9300__cont_tx_mode(struct ath_hal *ah, void *ds, int mode)
{
#if 0
    static int qnum = 0;
    int i;
    unsigned int qbits, val, val1, val2;
    int prefetch;
    struct ar9300_txs *ads = AR9300TXS(ds);

    if (mode == 10) {
        return;
    }

    if (mode == 7) { /* print status from the cont tx desc */
        if (ads) {
            val1 = ads->ds_txstatus1;
            val2 = ads->ds_txstatus2;
            HALDEBUG(ah, HAL_DEBUG_TXDESC, "s0(%x) s1(%x)\n",
                                       (unsigned)val1, (unsigned)val2);
        }
        HALDEBUG(ah, HAL_DEBUG_TXDESC, "txe(%x) txd(%x)\n",
                                   OS_REG_READ(ah, AR_Q_TXE),
                                   OS_REG_READ(ah, AR_Q_TXD)
                );
        for (i = 0; i < HAL_NUM_TX_QUEUES; i++) {
            val = OS_REG_READ(ah, AR_QTXDP(i));
            val2 = OS_REG_READ(ah, AR_QSTS(i)) & AR_Q_STS_PEND_FR_CNT;
            HALDEBUG(ah, HAL_DEBUG_TXDESC, "[%d] %x %d\n", i, val, val2);
        }
        return;
    }
    if (mode == 8) {                      /* set TXE for qnum */
        OS_REG_WRITE(ah, AR_Q_TXE, 1 << qnum);
        return;
    }
    if (mode == 9) {
        prefetch = (int)ds;
        return;
    }

    if (mode >= 1) {                    /* initiate cont tx operation */
        /* Disable AGC to A2 */
        qnum = (int) ds;

        OS_REG_WRITE(ah, AR_PHY_TEST,
            (OS_REG_READ(ah, AR_PHY_TEST) | PHY_AGC_CLR) );

        OS_REG_WRITE(ah, 0x9864, OS_REG_READ(ah, 0x9864) | 0x7f000);
        OS_REG_WRITE(ah, 0x9924, OS_REG_READ(ah, 0x9924) | 0x7f00fe);
        OS_REG_WRITE(ah, AR_DIAG_SW,
            (OS_REG_READ(ah, AR_DIAG_SW) |
             (AR_DIAG_FORCE_RX_CLEAR + AR_DIAG_IGNORE_VIRT_CS)) );


        OS_REG_WRITE(ah, AR_CR, AR_CR_RXD);     /* set receive disable */

        if (mode == 3 || mode == 4) {
            int txcfg;

            if (mode == 3) {
                OS_REG_WRITE(ah, AR_DLCL_IFS(qnum), 0);
                OS_REG_WRITE(ah, AR_DRETRY_LIMIT(qnum), 0xffffffff);
                OS_REG_WRITE(ah, AR_D_GBL_IFS_SIFS, 100);
                OS_REG_WRITE(ah, AR_D_GBL_IFS_EIFS, 100);
                OS_REG_WRITE(ah, AR_TIME_OUT, 2);
                OS_REG_WRITE(ah, AR_D_GBL_IFS_SLOT, 100);
            }

            OS_REG_WRITE(ah, AR_DRETRY_LIMIT(qnum), 0xffffffff);
            /* enable prefetch on qnum */
            OS_REG_WRITE(ah, AR_D_FPCTL, 0x10 | qnum);
            txcfg = 5 | (6 << AR_FTRIG_S);
            OS_REG_WRITE(ah, AR_TXCFG, txcfg);

            OS_REG_WRITE(ah, AR_QMISC(qnum),        /* set QCU modes */
                         AR_Q_MISC_DCU_EARLY_TERM_REQ
                         + AR_Q_MISC_FSP_ASAP
                         + AR_Q_MISC_CBR_INCR_DIS1
                         + AR_Q_MISC_CBR_INCR_DIS0
                        );

            /* stop tx dma all all except qnum */
            qbits = 0x3ff;
            qbits &= ~(1 << qnum);
            for (i = 0; i < 10; i++) {
                if (i == qnum) {
                    continue;
                }
                OS_REG_WRITE(ah, AR_Q_TXD, 1 << i);
            }

            OS_REG_WRITE(ah, AR_Q_TXD, qbits);

            /* clear and freeze MIB counters */
            OS_REG_WRITE(ah, AR_MIBC, AR_MIBC_CMC);
            OS_REG_WRITE(ah, AR_MIBC, AR_MIBC_FMC);

            OS_REG_WRITE(ah, AR_DMISC(qnum),
                         (AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL <<
                          AR_D_MISC_ARB_LOCKOUT_CNTRL_S)
                         + (AR_D_MISC_ARB_LOCKOUT_IGNORE)
                         + (AR_D_MISC_POST_FR_BKOFF_DIS)
                         + (AR_D_MISC_VIR_COL_HANDLING_IGNORE <<
                            AR_D_MISC_VIR_COL_HANDLING_S));

            for (i = 0; i < HAL_NUM_TX_QUEUES + 2; i++) { /* disconnect QCUs */
                if (i == qnum) {
                    continue;
                }
                OS_REG_WRITE(ah, AR_DQCUMASK(i), 0);
            }
        }
    }
    if (mode == 0) {
        OS_REG_WRITE(ah, AR_PHY_TEST,
            (OS_REG_READ(ah, AR_PHY_TEST) & ~PHY_AGC_CLR));
        OS_REG_WRITE(ah, AR_DIAG_SW,
            (OS_REG_READ(ah, AR_DIAG_SW) &
             ~(AR_DIAG_FORCE_RX_CLEAR + AR_DIAG_IGNORE_VIRT_CS)));
    }
#endif
}
#endif

void
ar9300_set_paprd_tx_desc(struct ath_hal *ah, void *ds, int chain_num)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    ads->ds_ctl12 |= SM((1 << chain_num), AR_paprd_chain_mask);
}
HAL_STATUS
ar9300_is_tx_done(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ar9300_txs *ads;

    ads = &ahp->ts_ring[ahp->ts_tail];

    if (ads->status8 & AR_tx_done) {
        return HAL_OK;
    }
    return HAL_EINPROGRESS;
}

void
ar9300_set_11n_tx_desc(
    struct ath_hal *ah,
    void *ds,
    u_int pkt_len,
    HAL_PKT_TYPE type,
    u_int tx_power,
    u_int key_ix,
    HAL_KEY_TYPE key_type,
    u_int flags)
{
    struct ar9300_txc *ads = AR9300TXC(ds);
    struct ath_hal_9300 *ahp = AH9300(ah);

    HALASSERT(is_valid_pkt_type(type));
    HALASSERT(is_valid_key_type(key_type));

    tx_power += ahp->ah_tx_power_index_offset;
    if (tx_power > 63) {
        tx_power = 63;
    }
    ads->ds_ctl11 =
        (pkt_len & AR_frame_len)
      | (flags & HAL_TXDESC_VMF ? AR_virt_more_frag : 0)
      | SM(tx_power, AR_xmit_power0)
      | (flags & HAL_TXDESC_VEOL ? AR_veol : 0)
      | (flags & HAL_TXDESC_CLRDMASK ? AR_clr_dest_mask : 0)
      | (key_ix != HAL_TXKEYIX_INVALID ? AR_dest_idx_valid : 0)
      | (flags & HAL_TXDESC_LOWRXCHAIN ? AR_low_rx_chain : 0);

    ads->ds_ctl12 =
        (key_ix != HAL_TXKEYIX_INVALID ? SM(key_ix, AR_dest_idx) : 0)
      | SM(type, AR_frame_type)
      | (flags & HAL_TXDESC_NOACK ? AR_no_ack : 0)
      | (flags & HAL_TXDESC_HWTS ? AR_insert_ts : 0)
      | (flags & HAL_TXDESC_EXT_ONLY ? AR_ext_only : 0)
      | (flags & HAL_TXDESC_EXT_AND_CTL ? AR_ext_and_ctl : 0);

    ads->ds_ctl17 =
        SM(key_type, AR_encr_type) | (flags & HAL_TXDESC_LDPC ? AR_ldpc : 0);

    ads->ds_ctl18 = 0;
    ads->ds_ctl19 = AR_not_sounding; /* set not sounding for normal frame */

    /* ToA/ToD positioning */
    if (flags & HAL_TXDESC_POS) {
        ads->ds_ctl12 |= AR_loc_mode;
        ads->ds_ctl19 &= ~AR_not_sounding;
    }

    /*
     * Clear Ness1/2/3 (Number of Extension Spatial Streams) fields.
     * Ness0 is cleared in ctl19.  See EV66059 (BB panic).
     */
    ads->ds_ctl20 = 0;
    ads->ds_ctl21 = 0;
    ads->ds_ctl22 = 0;
}

void ar9300_set_rx_chainmask(struct ath_hal *ah, int rxchainmask)
{
    OS_REG_WRITE(ah, AR_PHY_RX_CHAINMASK, rxchainmask);
}

void ar9300_update_loc_ctl_reg(struct ath_hal *ah, int pos_bit)
{
    u_int32_t reg_val;
    reg_val = OS_REG_READ(ah, AR_LOC_CTL_REG);
    if (pos_bit) {
        if (!(reg_val & AR_LOC_CTL_REG_FS)) {
            /* set fast timestamp bit in the regiter */
            OS_REG_WRITE(ah, AR_LOC_CTL_REG, (reg_val | AR_LOC_CTL_REG_FS));
            OS_REG_WRITE(ah, AR_LOC_TIMER_REG, 0);
        }
    }
    else {
        OS_REG_WRITE(ah, AR_LOC_CTL_REG, (reg_val & ~AR_LOC_CTL_REG_FS));
    }
}

#if 0
#define HT_RC_2_MCS(_rc)        ((_rc) & 0x0f)
static const u_int8_t ba_duration_delta[] = {
    24,     /*  0: BPSK       */
    12,     /*  1: QPSK 1/2   */
    12,     /*  2: QPSK 3/4   */
     4,     /*  3: 16-QAM 1/2 */
     4,     /*  4: 16-QAM 3/4 */
     4,     /*  5: 64-QAM 2/3 */
     4,     /*  6: 64-QAM 3/4 */
     4,     /*  7: 64-QAM 5/6 */
    24,     /*  8: BPSK       */
    12,     /*  9: QPSK 1/2   */
    12,     /* 10: QPSK 3/4   */
     4,     /* 11: 16-QAM 1/2 */
     4,     /* 12: 16-QAM 3/4 */
     4,     /* 13: 64-QAM 2/3 */
     4,     /* 14: 64-QAM 3/4 */
     4,     /* 15: 64-QAM 5/6 */
};
#endif


static u_int8_t
ar9300_get_tx_mode(u_int rate_flags)
{

    /* Check whether STBC is enabled if TxBF is not enabled */
    if (rate_flags & HAL_RATESERIES_STBC){
        return AR9300_STBC_MODE;
    }
    return AR9300_DEF_MODE;
}
void
ar9300_set_11n_rate_scenario(
    struct ath_hal *ah,
    void *ds,
    void *lastds,
    u_int dur_update_en,
    u_int rts_cts_rate,
    u_int rts_cts_duration,
    HAL_11N_RATE_SERIES series[],
    u_int nseries,
    u_int flags, 
    u_int32_t smart_antenna)
{
    struct ath_hal_private *ap = AH_PRIVATE(ah);
    struct ar9300_txc *ads = AR9300TXC(ds);
    struct ar9300_txc *last_ads = AR9300TXC(lastds);
    u_int32_t ds_ctl11;
    u_int8_t ant, cal_pkt = 0;
    u_int mode, tx_mode = AR9300_DEF_MODE;

    HALASSERT(nseries == 4);
    (void)nseries;
    (void)rts_cts_duration;   /* use H/W to calculate RTSCTSDuration */

    ds_ctl11 = ads->ds_ctl11;
    /*
     * Rate control settings override
     */
    if (flags & (HAL_TXDESC_RTSENA | HAL_TXDESC_CTSENA)) {
        if (flags & HAL_TXDESC_RTSENA) {
            ds_ctl11 &= ~AR_cts_enable;
            ds_ctl11 |= AR_rts_enable;
        } else {
            ds_ctl11 &= ~AR_rts_enable;
            ds_ctl11 |= AR_cts_enable;
        }
    } else {
        ds_ctl11 = (ds_ctl11 & ~(AR_rts_enable | AR_cts_enable));
    }

    mode = ath_hal_get_curmode(ah, ap->ah_curchan);
    cal_pkt = (ads->ds_ctl12 & AR_paprd_chain_mask)?1:0;

    if (ah->ah_config.ath_hal_desc_tpc) {
        int16_t txpower;

        if (!cal_pkt) {
            /* Series 0 TxPower */
            tx_mode = ar9300_get_tx_mode(series[0].RateFlags);
            txpower = ar9300_get_rate_txpower(ah, mode, series[0].RateIndex,
                                       series[0].ChSel, tx_mode);
        } else {
            txpower = AH9300(ah)->paprd_training_power;
        }
        ds_ctl11 &= ~AR_xmit_power0;
        ds_ctl11 |=
            set_11n_tx_power(0, AH_MIN(txpower, series[0].tx_power_cap));
    }

    ads->ds_ctl11 = ds_ctl11;


    ads->ds_ctl13 = set_11n_tries(series, 0)
                             |  set_11n_tries(series, 1)
                             |  set_11n_tries(series, 2)
                             |  set_11n_tries(series, 3)
                             |  (dur_update_en ? AR_dur_update_ena : 0)
                             |  SM(0, AR_burst_dur);

    ads->ds_ctl14 = set_11n_rate(series, 0)
                             |  set_11n_rate(series, 1)
                             |  set_11n_rate(series, 2)
                             |  set_11n_rate(series, 3);

    ads->ds_ctl15 = set_11n_pkt_dur_rts_cts(series, 0)
                             |  set_11n_pkt_dur_rts_cts(series, 1);

    ads->ds_ctl16 = set_11n_pkt_dur_rts_cts(series, 2)
                             |  set_11n_pkt_dur_rts_cts(series, 3);

    ads->ds_ctl18 = set_11n_rate_flags(series, 0)
                             |  set_11n_rate_flags(series, 1)
                             |  set_11n_rate_flags(series, 2)
                             |  set_11n_rate_flags(series, 3)
                             | SM(rts_cts_rate, AR_rts_cts_rate);
    /* set not sounding for normal frame */
    ads->ds_ctl19 = AR_not_sounding;

    if (ah->ah_config.ath_hal_desc_tpc) {
        int16_t txpower;

        if (!cal_pkt) {
            /* Series 1 TxPower */
            tx_mode = ar9300_get_tx_mode(series[1].RateFlags);
            txpower = ar9300_get_rate_txpower(
                ah, mode, series[1].RateIndex, series[1].ChSel, tx_mode);
        } else {
            txpower = AH9300(ah)->paprd_training_power;
        }
        ads->ds_ctl20 |=
            set_11n_tx_power(1, AH_MIN(txpower, series[1].tx_power_cap));
               

        /* Series 2 TxPower */
        if (!cal_pkt) {
            tx_mode = ar9300_get_tx_mode(series[2].RateFlags);
            txpower = ar9300_get_rate_txpower(
                ah, mode, series[2].RateIndex, series[2].ChSel, tx_mode);
        } else {
            txpower = AH9300(ah)->paprd_training_power;
        }
        ads->ds_ctl21 |=
            set_11n_tx_power(2, AH_MIN(txpower, series[2].tx_power_cap));

        /* Series 3 TxPower */
        if (!cal_pkt) {
            tx_mode = ar9300_get_tx_mode(series[3].RateFlags);
            txpower = ar9300_get_rate_txpower(
                ah, mode, series[3].RateIndex, series[3].ChSel, tx_mode);
        } else {
            txpower = AH9300(ah)->paprd_training_power;
        }
        ads->ds_ctl22 |=
            set_11n_tx_power(3, AH_MIN(txpower, series[3].tx_power_cap));
    }

    if (smart_antenna != 0xffffffff)
    {
        /* TX DESC dword 19 to 23 are used for smart antenna configuaration
         * ctl19 for rate series 0 ... ctrl22 for series 3
         * bits[2:0] used to configure smart anntenna
         */
        ant = (smart_antenna&0x000000ff);
        ads->ds_ctl19 |= ant; /* rateseries 0 */

        ant = (smart_antenna&0x0000ff00) >> 8;
        ads->ds_ctl20 |= ant;  /* rateseries 1 */

        ant = (smart_antenna&0x00ff0000) >> 16;
        ads->ds_ctl21 |= ant;  /* rateseries 2 */

        ant = (smart_antenna&0xff000000) >> 24;
        ads->ds_ctl22 |= ant;  /* rateseries 3 */
    }

#ifdef AH_NEED_DESC_SWAP
    last_ads->ds_ctl13 = __bswap32(ads->ds_ctl13);
    last_ads->ds_ctl14 = __bswap32(ads->ds_ctl14);
#else
    last_ads->ds_ctl13 = ads->ds_ctl13;
    last_ads->ds_ctl14 = ads->ds_ctl14;
#endif
}

void
ar9300_set_11n_aggr_first(struct ath_hal *ah, struct ath_desc *ds,
  u_int aggr_len, u_int num_delims)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    ads->ds_ctl12 |= (AR_is_aggr | AR_more_aggr);

    ads->ds_ctl17 &= ~AR_aggr_len;
    ads->ds_ctl17 &= ~AR_pad_delim;
    /* XXX should use a stack variable! */
    ads->ds_ctl17 |= SM(aggr_len, AR_aggr_len);
    ads->ds_ctl17 |= SM(num_delims, AR_pad_delim);
}

void
ar9300_set_11n_aggr_middle(struct ath_hal *ah, struct ath_desc *ds,
  u_int num_delims)
{
    struct ar9300_txc *ads = AR9300TXC(ds);
    unsigned int ctl17;

    ads->ds_ctl12 |= (AR_is_aggr | AR_more_aggr);

    /*
     * We use a stack variable to manipulate ctl6 to reduce uncached
     * read modify, modfiy, write.
     */
    ctl17 = ads->ds_ctl17;
    ctl17 &= ~AR_pad_delim;
    ctl17 |= SM(num_delims, AR_pad_delim);
    ads->ds_ctl17 = ctl17;
}

void
ar9300_set_11n_aggr_last(struct ath_hal *ah, struct ath_desc *ds)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    ads->ds_ctl12 |= AR_is_aggr;
    ads->ds_ctl12 &= ~AR_more_aggr;
    ads->ds_ctl17 &= ~AR_pad_delim;
}

void
ar9300_clr_11n_aggr(struct ath_hal *ah, struct ath_desc *ds)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    ads->ds_ctl12 &= (~AR_is_aggr & ~AR_more_aggr);
}

void
ar9300_set_11n_burst_duration(struct ath_hal *ah, struct ath_desc *ds,
    u_int burst_duration)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    ads->ds_ctl13 &= ~AR_burst_dur;
    ads->ds_ctl13 |= SM(burst_duration, AR_burst_dur);
}

void
ar9300_set_11n_rifs_burst_middle(struct ath_hal *ah, void *ds)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    ads->ds_ctl12 |= AR_more_rifs | AR_no_ack;
}

void
ar9300_set_11n_rifs_burst_last(struct ath_hal *ah, void *ds)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    ads->ds_ctl12 &= (~AR_more_aggr & ~AR_more_rifs);
}

void
ar9300_clr_11n_rifs_burst(struct ath_hal *ah, void *ds)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    ads->ds_ctl12 &= (~AR_more_rifs & ~AR_no_ack);
}

void
ar9300_set_11n_aggr_rifs_burst(struct ath_hal *ah, void *ds)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    ads->ds_ctl12 |= AR_no_ack;
    ads->ds_ctl12 &= ~AR_more_rifs;
}

void
ar9300_set_11n_virtual_more_frag(struct ath_hal *ah, struct ath_desc *ds,
                                                  u_int vmf)
{
    struct ar9300_txc *ads = AR9300TXC(ds);

    if (vmf) {
        ads->ds_ctl11 |=  AR_virt_more_frag;
    } else {
        ads->ds_ctl11 &= ~AR_virt_more_frag;
    }
}

void
ar9300_get_desc_info(struct ath_hal *ah, HAL_DESC_INFO *desc_info)
{
    desc_info->txctl_numwords = TXCTL_NUMWORDS(ah);
    desc_info->txctl_offset = TXCTL_OFFSET(ah);
    desc_info->txstatus_numwords = TXSTATUS_NUMWORDS(ah);
    desc_info->txstatus_offset = TXSTATUS_OFFSET(ah);

    desc_info->rxctl_numwords = RXCTL_NUMWORDS(ah);
    desc_info->rxctl_offset = RXCTL_OFFSET(ah);
    desc_info->rxstatus_numwords = RXSTATUS_NUMWORDS(ah);
    desc_info->rxstatus_offset = RXSTATUS_OFFSET(ah);
}
