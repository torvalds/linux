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


 /* Contains descriptor definitions for Osprey */


#ifndef _ATH_AR9300_DESC_H_
#define _ATH_AR9300_DESC_H_

#ifdef	_KERNEL
#include "ar9300_freebsd_inc.h"
#endif

/* Osprey Status Descriptor. */
struct ar9300_txs {
    u_int32_t   ds_info;
    u_int32_t   status1;
    u_int32_t   status2;
    u_int32_t   status3;
    u_int32_t   status4;
    u_int32_t   status5;
    u_int32_t   status6;
    u_int32_t   status7;
    u_int32_t   status8;
};

struct ar9300_rxs {
    u_int32_t   ds_info;
    u_int32_t   status1;
    u_int32_t   status2;
    u_int32_t   status3;
    u_int32_t   status4;
    u_int32_t   status5;
    u_int32_t   status6;
    u_int32_t   status7;
    u_int32_t   status8;
    u_int32_t   status9;
    u_int32_t   status10;
    u_int32_t   status11;
};

/* Transmit Control Descriptor */
struct ar9300_txc {
    u_int32_t   ds_info;   /* descriptor information */
    u_int32_t   ds_link;   /* link pointer */
    u_int32_t   ds_data0;  /* data pointer to 1st buffer */
    u_int32_t   ds_ctl3;   /* DMA control 3  */
    u_int32_t   ds_data1;  /* data pointer to 2nd buffer */
    u_int32_t   ds_ctl5;   /* DMA control 5  */
    u_int32_t   ds_data2;  /* data pointer to 3rd buffer */
    u_int32_t   ds_ctl7;   /* DMA control 7  */
    u_int32_t   ds_data3;  /* data pointer to 4th buffer */
    u_int32_t   ds_ctl9;   /* DMA control 9  */
    u_int32_t   ds_ctl10;  /* DMA control 10 */
    u_int32_t   ds_ctl11;  /* DMA control 11 */
    u_int32_t   ds_ctl12;  /* DMA control 12 */
    u_int32_t   ds_ctl13;  /* DMA control 13 */
    u_int32_t   ds_ctl14;  /* DMA control 14 */
    u_int32_t   ds_ctl15;  /* DMA control 15 */
    u_int32_t   ds_ctl16;  /* DMA control 16 */
    u_int32_t   ds_ctl17;  /* DMA control 17 */
    u_int32_t   ds_ctl18;  /* DMA control 18 */
    u_int32_t   ds_ctl19;  /* DMA control 19 */
    u_int32_t   ds_ctl20;  /* DMA control 20 */
    u_int32_t   ds_ctl21;  /* DMA control 21 */
    u_int32_t   ds_ctl22;  /* DMA control 22 */
    u_int32_t   ds_ctl23;  /* DMA control 23 */
    u_int32_t   ds_pad[8]; /* pad to cache line (128 bytes/32 dwords) */
};


#define AR9300RXS(_rxs)        ((struct ar9300_rxs *)(_rxs))
#define AR9300TXS(_txs)        ((struct ar9300_txs *)(_txs))
#define AR9300TXC(_ds)         ((struct ar9300_txc *)(_ds))

#define AR9300TXC_CONST(_ds)   ((const struct ar9300_txc *)(_ds))


/* ds_info */
#define AR_desc_len          0x000000ff
#define AR_rx_priority       0x00000100
#define AR_tx_qcu_num         0x00000f00
#define AR_tx_qcu_num_S       8
#define AR_ctrl_stat         0x00004000
#define AR_ctrl_stat_S       14
#define AR_tx_rx_desc         0x00008000
#define AR_tx_rx_desc_S       15
#define AR_desc_id           0xffff0000
#define AR_desc_id_S         16

/***********
 * TX Desc *
 ***********/

/* ds_ctl3 */
/* ds_ctl5 */
/* ds_ctl7 */
/* ds_ctl9 */
#define AR_buf_len           0x0fff0000
#define AR_buf_len_S         16

/* ds_ctl10 */
#define AR_tx_desc_id         0xffff0000
#define AR_tx_desc_id_S       16
#define AR_tx_ptr_chk_sum      0x0000ffff

/* ds_ctl11 */
#define AR_frame_len         0x00000fff
#define AR_virt_more_frag     0x00001000
#define AR_tx_ctl_rsvd00      0x00002000
#define AR_low_rx_chain       0x00004000
#define AR_tx_clear_retry     0x00008000
#define AR_xmit_power0       0x003f0000
#define AR_xmit_power0_S     16
#define AR_rts_enable        0x00400000
#define AR_veol             0x00800000
#define AR_clr_dest_mask      0x01000000
#define AR_tx_bf0            0x02000000
#define AR_tx_bf1            0x04000000
#define AR_tx_bf2            0x08000000
#define AR_tx_bf3            0x10000000
#define	AR_TxBfSteered		0x1e000000			/* for tx_bf*/ 
#define AR_tx_intr_req        0x20000000
#define AR_dest_idx_valid     0x40000000
#define AR_cts_enable        0x80000000

/* ds_ctl12 */
#define AR_tx_ctl_rsvd02      0x000001ff
#define AR_paprd_chain_mask   0x00000e00
#define AR_paprd_chain_mask_S 9
#define AR_tx_more           0x00001000
#define AR_dest_idx          0x000fe000
#define AR_dest_idx_S        13
#define AR_frame_type        0x00f00000
#define AR_frame_type_S      20
#define AR_no_ack            0x01000000
#define AR_insert_ts         0x02000000
#define AR_corrupt_fcs       0x04000000
#define AR_ext_only          0x08000000
#define AR_ext_and_ctl        0x10000000
#define AR_more_aggr         0x20000000
#define AR_is_aggr           0x40000000
#define AR_more_rifs         0x80000000
#define AR_loc_mode          0x00000100 /* Positioning bit in TX desc */

/* ds_ctl13 */
#define AR_burst_dur         0x00007fff
#define AR_burst_dur_S       0
#define AR_dur_update_ena     0x00008000
#define AR_xmit_data_tries0   0x000f0000
#define AR_xmit_data_tries0_S 16
#define AR_xmit_data_tries1   0x00f00000
#define AR_xmit_data_tries1_S 20
#define AR_xmit_data_tries2   0x0f000000
#define AR_xmit_data_tries2_S 24
#define AR_xmit_data_tries3   0xf0000000
#define AR_xmit_data_tries3_S 28

/* ds_ctl14 */
#define AR_xmit_rate0        0x000000ff
#define AR_xmit_rate0_S      0
#define AR_xmit_rate1        0x0000ff00
#define AR_xmit_rate1_S      8
#define AR_xmit_rate2        0x00ff0000
#define AR_xmit_rate2_S      16
#define AR_xmit_rate3        0xff000000
#define AR_xmit_rate3_S      24

/* ds_ctl15 */
#define AR_packet_dur0       0x00007fff
#define AR_packet_dur0_S     0
#define AR_rts_cts_qual0      0x00008000
#define AR_packet_dur1       0x7fff0000
#define AR_packet_dur1_S     16
#define AR_rts_cts_qual1      0x80000000

/* ds_ctl16 */
#define AR_packet_dur2       0x00007fff
#define AR_packet_dur2_S     0
#define AR_rts_cts_qual2      0x00008000
#define AR_packet_dur3       0x7fff0000
#define AR_packet_dur3_S     16
#define AR_rts_cts_qual3      0x80000000

/* ds_ctl17 */
#define AR_aggr_len          0x0000ffff
#define AR_aggr_len_S        0
#define AR_tx_ctl_rsvd60      0x00030000
#define AR_pad_delim         0x03fc0000
#define AR_pad_delim_S       18
#define AR_encr_type         0x1c000000
#define AR_encr_type_S       26
#define AR_tx_dc_ap_sta_sel     0x40000000
#define AR_tx_ctl_rsvd61      0xc0000000
#define AR_calibrating      0x40000000
#define AR_ldpc             0x80000000

/* ds_ctl18 */
#define AR_2040_0           0x00000001
#define AR_gi0              0x00000002
#define AR_chain_sel0        0x0000001c
#define AR_chain_sel0_S      2
#define AR_2040_1           0x00000020
#define AR_gi1              0x00000040
#define AR_chain_sel1        0x00000380
#define AR_chain_sel1_S      7
#define AR_2040_2           0x00000400
#define AR_gi2              0x00000800
#define AR_chain_sel2        0x00007000
#define AR_chain_sel2_S      12
#define AR_2040_3           0x00008000
#define AR_gi3              0x00010000
#define AR_chain_sel3        0x000e0000
#define AR_chain_sel3_S      17
#define AR_rts_cts_rate       0x0ff00000
#define AR_rts_cts_rate_S     20
#define AR_stbc0            0x10000000
#define AR_stbc1            0x20000000
#define AR_stbc2            0x40000000
#define AR_stbc3            0x80000000

/* ds_ctl19 */
#define AR_tx_ant0           0x00ffffff
#define AR_tx_ant_sel0        0x80000000
#define	AR_RTS_HTC_TRQ      0x10000000	/* bit 28 for rts_htc_TRQ*/ /*for tx_bf*/
#define AR_not_sounding     0x20000000 
#define AR_ness				0xc0000000 
#define AR_ness_S			30

/* ds_ctl20 */
#define AR_tx_ant1           0x00ffffff
#define AR_xmit_power1       0x3f000000
#define AR_xmit_power1_S     24
#define AR_tx_ant_sel1        0x80000000
#define AR_ness1			0xc0000000 
#define AR_ness1_S			30

/* ds_ctl21 */
#define AR_tx_ant2           0x00ffffff
#define AR_xmit_power2       0x3f000000
#define AR_xmit_power2_S     24
#define AR_tx_ant_sel2        0x80000000
#define AR_ness2			0xc0000000 
#define AR_ness2_S			30

/* ds_ctl22 */
#define AR_tx_ant3           0x00ffffff
#define AR_xmit_power3       0x3f000000
#define AR_xmit_power3_S     24
#define AR_tx_ant_sel3        0x80000000
#define AR_ness3			0xc0000000 
#define AR_ness3_S			30

/*************
 * TX Status *
 *************/

/* ds_status1 */
#define AR_tx_status_rsvd     0x0000ffff

/* ds_status2 */
#define AR_tx_rssi_ant00      0x000000ff
#define AR_tx_rssi_ant00_S    0
#define AR_tx_rssi_ant01      0x0000ff00
#define AR_tx_rssi_ant01_S    8
#define AR_tx_rssi_ant02      0x00ff0000
#define AR_tx_rssi_ant02_S    16
#define AR_tx_status_rsvd00   0x3f000000
#define AR_tx_ba_status       0x40000000
#define AR_tx_status_rsvd01   0x80000000

/* ds_status3 */
#define AR_frm_xmit_ok        0x00000001
#define AR_excessive_retries 0x00000002
#define AR_fifounderrun     0x00000004
#define AR_filtered         0x00000008
#define AR_rts_fail_cnt       0x000000f0
#define AR_rts_fail_cnt_S     4
#define AR_data_fail_cnt      0x00000f00
#define AR_data_fail_cnt_S    8
#define AR_virt_retry_cnt     0x0000f000
#define AR_virt_retry_cnt_S   12
#define AR_tx_delim_underrun  0x00010000
#define AR_tx_data_underrun   0x00020000
#define AR_desc_cfg_err       0x00040000
#define AR_tx_timer_expired   0x00080000
#define AR_tx_status_rsvd10   0xfff00000

/* ds_status7 */
#define AR_tx_rssi_ant10      0x000000ff
#define AR_tx_rssi_ant10_S    0
#define AR_tx_rssi_ant11      0x0000ff00
#define AR_tx_rssi_ant11_S    8
#define AR_tx_rssi_ant12      0x00ff0000
#define AR_tx_rssi_ant12_S    16
#define AR_tx_rssi_combined   0xff000000
#define AR_tx_rssi_combined_S 24

/* ds_status8 */
#define AR_tx_done           0x00000001
#define AR_seq_num           0x00001ffe
#define AR_seq_num_S         1
#define AR_tx_status_rsvd80   0x0001e000
#define AR_tx_op_exceeded     0x00020000
#define AR_tx_status_rsvd81   0x001c0000
#define	AR_TXBFStatus		0x001c0000
#define	AR_TXBFStatus_S		18
#define AR_tx_bf_bw_mismatch 0x00040000
#define AR_tx_bf_stream_miss 0x00080000
#define AR_final_tx_idx       0x00600000
#define AR_final_tx_idx_S     21
#define AR_tx_bf_dest_miss   0x00800000
#define AR_tx_bf_expired     0x01000000
#define AR_power_mgmt        0x02000000
#define AR_tx_status_rsvd83   0x0c000000
#define AR_tx_tid            0xf0000000
#define AR_tx_tid_S          28
#define AR_tx_fast_ts        0x08000000 /* 27th bit for locationing */


/*************
 * Rx Status *
 *************/

/* ds_status1 */
#define AR_rx_rssi_ant00      0x000000ff
#define AR_rx_rssi_ant00_S    0
#define AR_rx_rssi_ant01      0x0000ff00
#define AR_rx_rssi_ant01_S    8
#define AR_rx_rssi_ant02      0x00ff0000
#define AR_rx_rssi_ant02_S    16
#define AR_rx_rate           0xff000000
#define AR_rx_rate_S         24

/* ds_status2 */
#define AR_data_len          0x00000fff
#define AR_data_len_S        0
#define AR_rx_more           0x00001000
#define AR_num_delim         0x003fc000
#define AR_num_delim_S       14
#define AR_hw_upload_data     0x00400000
#define AR_hw_upload_data_S   22
#define AR_rx_status_rsvd10   0xff800000


/* ds_status4 */
#define AR_gi               0x00000001
#define AR_2040             0x00000002
#define AR_parallel40       0x00000004
#define AR_parallel40_S     2
#define AR_rx_stbc           0x00000008
#define AR_rx_not_sounding    0x00000010
#define AR_rx_ness           0x00000060
#define AR_rx_ness_S         5
#define AR_hw_upload_data_valid    0x00000080
#define AR_hw_upload_data_valid_S  7    
#define AR_rx_antenna	    0xffffff00
#define AR_rx_antenna_S	    8

/* ds_status5 */
#define AR_rx_rssi_ant10            0x000000ff
#define AR_rx_rssi_ant10_S          0
#define AR_rx_rssi_ant11            0x0000ff00
#define AR_rx_rssi_ant11_S          8
#define AR_rx_rssi_ant12            0x00ff0000
#define AR_rx_rssi_ant12_S          16
#define AR_rx_rssi_combined         0xff000000
#define AR_rx_rssi_combined_S       24

/* ds_status6 */
#define AR_rx_evm0           status6

/* ds_status7 */
#define AR_rx_evm1           status7

/* ds_status8 */
#define AR_rx_evm2           status8

/* ds_status9 */
#define AR_rx_evm3           status9

/* ds_status11 */
#define AR_rx_done           0x00000001
#define AR_rx_frame_ok        0x00000002
#define AR_crc_err           0x00000004
#define AR_decrypt_crc_err    0x00000008
#define AR_phyerr           0x00000010
#define AR_michael_err       0x00000020
#define AR_pre_delim_crc_err   0x00000040
#define AR_apsd_trig         0x00000080
#define AR_rx_key_idx_valid    0x00000100
#define AR_key_idx           0x0000fe00
#define AR_key_idx_S         9
#define AR_phy_err_code       0x0000ff00
#define AR_phy_err_code_S     8
#define AR_rx_more_aggr       0x00010000
#define AR_rx_aggr           0x00020000
#define AR_post_delim_crc_err  0x00040000
#define AR_rx_status_rsvd71   0x01f80000
#define AR_hw_upload_data_type 0x06000000
#define AR_hw_upload_data_type_S   25
#define AR_position_bit      0x08000000 /* positioning bit */
#define AR_hi_rx_chain        0x10000000
#define AR_rx_first_aggr      0x20000000
#define AR_decrypt_busy_err   0x40000000
#define AR_key_miss          0x80000000

#define TXCTL_OFFSET(ah)      11
#define TXCTL_NUMWORDS(ah)    12
#define TXSTATUS_OFFSET(ah)   2
#define TXSTATUS_NUMWORDS(ah) 7

#define RXCTL_OFFSET(ah)      0
#define RXCTL_NUMWORDS(ah)    0
#define RXSTATUS_OFFSET(ah)   1
#define RXSTATUS_NUMWORDS(ah) 11


#define TXC_INFO(_qcu, _desclen) (ATHEROS_VENDOR_ID << AR_desc_id_S) \
                        | (1 << AR_tx_rx_desc_S) \
                        | (1 << AR_ctrl_stat_S) \
                        | (_qcu << AR_tx_qcu_num_S) \
                        | (_desclen)

#define VALID_KEY_TYPES \
        ((1 << HAL_KEY_TYPE_CLEAR) | (1 << HAL_KEY_TYPE_WEP)|\
         (1 << HAL_KEY_TYPE_AES)   | (1 << HAL_KEY_TYPE_TKIP))
#define is_valid_key_type(_t)      ((1 << (_t)) & VALID_KEY_TYPES)

#define set_11n_tries(_series, _index) \
        (SM((_series)[_index].Tries, AR_xmit_data_tries##_index))

#define set_11n_rate(_series, _index) \
        (SM((_series)[_index].Rate, AR_xmit_rate##_index))

#define set_11n_pkt_dur_rts_cts(_series, _index) \
        (SM((_series)[_index].PktDuration, AR_packet_dur##_index) |\
         ((_series)[_index].RateFlags & HAL_RATESERIES_RTS_CTS   ?\
         AR_rts_cts_qual##_index : 0))
        
#define not_two_stream_rate(_rate) (((_rate) >0x8f) || ((_rate)<0x88))

#define set_11n_tx_bf_ldpc( _series) \
        ((( not_two_stream_rate((_series)[0].Rate) && (not_two_stream_rate((_series)[1].Rate)|| \
        (!(_series)[1].Tries)) && (not_two_stream_rate((_series)[2].Rate)||(!(_series)[2].Tries)) \
         && (not_two_stream_rate((_series)[3].Rate)||(!(_series)[3].Tries)))) \
        ? AR_ldpc : 0)

#define set_11n_rate_flags(_series, _index) \
        ((_series)[_index].RateFlags & HAL_RATESERIES_2040 ? AR_2040_##_index : 0) \
        |((_series)[_index].RateFlags & HAL_RATESERIES_HALFGI ? AR_gi##_index : 0) \
        |((_series)[_index].RateFlags & HAL_RATESERIES_STBC ? AR_stbc##_index : 0) \
        |SM((_series)[_index].ChSel, AR_chain_sel##_index)

#define set_11n_tx_power(_index, _txpower) \
        SM(_txpower, AR_xmit_power##_index)

#define IS_3CHAIN_TX(_ah) (AH9300(_ah)->ah_tx_chainmask == 7)
/*
 * Descriptor Access Functions
 */
/* XXX valid Tx rates will change for 3 stream support */
#define VALID_PKT_TYPES \
        ((1<<HAL_PKT_TYPE_NORMAL)|(1<<HAL_PKT_TYPE_ATIM)|\
         (1<<HAL_PKT_TYPE_PSPOLL)|(1<<HAL_PKT_TYPE_PROBE_RESP)|\
         (1<<HAL_PKT_TYPE_BEACON))
#define is_valid_pkt_type(_t)      ((1<<(_t)) & VALID_PKT_TYPES)
#define VALID_TX_RATES \
        ((1<<0x0b)|(1<<0x0f)|(1<<0x0a)|(1<<0x0e)|(1<<0x09)|(1<<0x0d)|\
         (1<<0x08)|(1<<0x0c)|(1<<0x1b)|(1<<0x1a)|(1<<0x1e)|(1<<0x19)|\
         (1<<0x1d)|(1<<0x18)|(1<<0x1c))
#define is_valid_tx_rate(_r)       ((1<<(_r)) & VALID_TX_RATES)


#ifdef	_KERNEL
        /* TX common functions */

extern  HAL_BOOL ar9300_update_tx_trig_level(struct ath_hal *,
        HAL_BOOL IncTrigLevel);
extern  u_int16_t ar9300_get_tx_trig_level(struct ath_hal *);
extern  HAL_BOOL ar9300_set_tx_queue_props(struct ath_hal *ah, int q,
        const HAL_TXQ_INFO *q_info);
extern  HAL_BOOL ar9300_get_tx_queue_props(struct ath_hal *ah, int q,
        HAL_TXQ_INFO *q_info);
extern  int ar9300_setup_tx_queue(struct ath_hal *ah, HAL_TX_QUEUE type,
        const HAL_TXQ_INFO *q_info);
extern  HAL_BOOL ar9300_release_tx_queue(struct ath_hal *ah, u_int q);
extern  HAL_BOOL ar9300_reset_tx_queue(struct ath_hal *ah, u_int q);
extern  u_int32_t ar9300_get_tx_dp(struct ath_hal *ah, u_int q);
extern  HAL_BOOL ar9300_set_tx_dp(struct ath_hal *ah, u_int q, u_int32_t txdp);
extern  HAL_BOOL ar9300_start_tx_dma(struct ath_hal *ah, u_int q);
extern  u_int32_t ar9300_num_tx_pending(struct ath_hal *ah, u_int q);
extern  HAL_BOOL ar9300_stop_tx_dma(struct ath_hal *ah, u_int q, u_int timeout);
extern HAL_BOOL ar9300_stop_tx_dma_indv_que(struct ath_hal *ah, u_int q, u_int timeout);
extern  HAL_BOOL ar9300_abort_tx_dma(struct ath_hal *ah);
extern  void ar9300_get_tx_intr_queue(struct ath_hal *ah, u_int32_t *);

extern  void ar9300_tx_req_intr_desc(struct ath_hal *ah, void *ds);
extern  HAL_BOOL ar9300_fill_tx_desc(struct ath_hal *ah, void *ds, HAL_DMA_ADDR *buf_addr,
        u_int32_t *seg_len, u_int desc_id, u_int qcu, HAL_KEY_TYPE key_type, HAL_BOOL first_seg,
        HAL_BOOL last_seg, const void *ds0);
extern  void ar9300_set_desc_link(struct ath_hal *, void *ds, u_int32_t link);
extern  void ar9300_get_desc_link_ptr(struct ath_hal *, void *ds, u_int32_t **link);
extern  void ar9300_clear_tx_desc_status(struct ath_hal *ah, void *ds);
#ifdef ATH_SWRETRY
extern void ar9300_clear_dest_mask(struct ath_hal *ah, void *ds);
#endif
extern  HAL_STATUS ar9300_proc_tx_desc(struct ath_hal *ah, void *);
extern  void ar9300_get_raw_tx_desc(struct ath_hal *ah, u_int32_t *);
extern  void ar9300_get_tx_rate_code(struct ath_hal *ah, void *, struct ath_tx_status *);
extern  u_int32_t ar9300_calc_tx_airtime(struct ath_hal *ah, void *, struct ath_tx_status *, 
        HAL_BOOL comp_wastedt, u_int8_t nbad, u_int8_t nframes);
extern  void ar9300_setup_tx_status_ring(struct ath_hal *ah, void *, u_int32_t , u_int16_t);
extern void ar9300_set_paprd_tx_desc(struct ath_hal *ah, void *ds, int chain_num);
HAL_STATUS ar9300_is_tx_done(struct ath_hal *ah);
extern void ar9300_set_11n_tx_desc(struct ath_hal *ah, void *ds,
       u_int pkt_len, HAL_PKT_TYPE type, u_int tx_power,
       u_int key_ix, HAL_KEY_TYPE key_type, u_int flags);
extern void ar9300_set_rx_chainmask(struct ath_hal *ah, int rxchainmask);
extern void ar9300_update_loc_ctl_reg(struct ath_hal *ah, int pos_bit);

/* for tx_bf*/       
#define ar9300_set_11n_txbf_cal(ah, ds, cal_pos, code_rate, cec, opt)
/* for tx_bf*/
         
extern void ar9300_set_11n_rate_scenario(struct ath_hal *ah, void *ds,
        void *lastds, u_int dur_update_en, u_int rts_cts_rate, u_int rts_cts_duration, HAL_11N_RATE_SERIES series[],
       u_int nseries, u_int flags, u_int32_t smartAntenna);
extern void ar9300_set_11n_aggr_first(struct ath_hal *ah, struct ath_desc *ds,
       u_int aggr_len, u_int num_delims);
extern void ar9300_set_11n_aggr_middle(struct ath_hal *ah, struct ath_desc *ds,
       u_int num_delims);
extern void ar9300_set_11n_aggr_last(struct ath_hal *ah, struct ath_desc *ds);
extern void ar9300_clr_11n_aggr(struct ath_hal *ah, struct ath_desc *ds);
extern void ar9300_set_11n_burst_duration(struct ath_hal *ah,
       struct ath_desc *ds, u_int burst_duration);
extern void ar9300_set_11n_rifs_burst_middle(struct ath_hal *ah, void *ds);
extern void ar9300_set_11n_rifs_burst_last(struct ath_hal *ah, void *ds);
extern void ar9300_clr_11n_rifs_burst(struct ath_hal *ah, void *ds);
extern void ar9300_set_11n_aggr_rifs_burst(struct ath_hal *ah, void *ds);
extern void ar9300_set_11n_virtual_more_frag(struct ath_hal *ah,
       struct ath_desc *ds, u_int vmf);
#ifdef AH_PRIVATE_DIAG
extern void ar9300__cont_tx_mode(struct ath_hal *ah, void *ds, int mode);
#endif

	/* RX common functions */

extern  u_int32_t ar9300_get_rx_dp(struct ath_hal *ath, HAL_RX_QUEUE qtype);
extern  void ar9300_set_rx_dp(struct ath_hal *ah, u_int32_t rxdp, HAL_RX_QUEUE qtype);
extern  void ar9300_enable_receive(struct ath_hal *ah);
extern  HAL_BOOL ar9300_stop_dma_receive(struct ath_hal *ah, u_int timeout);
extern  void ar9300_start_pcu_receive(struct ath_hal *ah, HAL_BOOL is_scanning);
extern  void ar9300_stop_pcu_receive(struct ath_hal *ah);
extern  void ar9300_set_multicast_filter(struct ath_hal *ah,
        u_int32_t filter0, u_int32_t filter1);
extern  u_int32_t ar9300_get_rx_filter(struct ath_hal *ah);
extern  void ar9300_set_rx_filter(struct ath_hal *ah, u_int32_t bits);
extern  HAL_BOOL ar9300_set_rx_sel_evm(struct ath_hal *ah, HAL_BOOL, HAL_BOOL);
extern	HAL_BOOL ar9300_set_rx_abort(struct ath_hal *ah, HAL_BOOL);

extern  HAL_STATUS ar9300_proc_rx_desc(struct ath_hal *ah,
        struct ath_desc *, u_int32_t, struct ath_desc *, u_int64_t, struct ath_rx_status *);
extern  HAL_STATUS ar9300_get_rx_key_idx(struct ath_hal *ah,
        struct ath_desc *, u_int8_t *, u_int8_t *);
extern  HAL_STATUS ar9300_proc_rx_desc_fast(struct ath_hal *ah, struct ath_desc *,
        u_int32_t, struct ath_desc *, struct ath_rx_status *, void *);

extern  void ar9300_promisc_mode(struct ath_hal *ah, HAL_BOOL enable);
extern  void ar9300_read_pktlog_reg(struct ath_hal *ah, u_int32_t *, u_int32_t *, u_int32_t *, u_int32_t *);
extern  void ar9300_write_pktlog_reg(struct ath_hal *ah, HAL_BOOL , u_int32_t , u_int32_t , u_int32_t , u_int32_t );

#endif

#endif /* _ATH_AR9300_DESC_H_ */
