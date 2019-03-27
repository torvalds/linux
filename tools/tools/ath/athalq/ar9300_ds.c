/*
 * Copyright (c) 2012 Adrian Chadd <adrian@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/alq.h>
#include <sys/endian.h>

#include <dev/ath/if_ath_alq.h>
#include <ar9300/ar9300desc.h>

#include "ar9300_ds.h"

/* XXX should break this out into if_athvar.h */

#define	MS(_v, _f)	( ((_v) & (_f)) >> _f##_S )
#define	MF(_v, _f) ( !! ((_v) & (_f)))

static uint32_t last_ts = 0;

void
ath_alq_print_edma_tx_fifo_push(struct if_ath_alq_payload *a)
{
	struct if_ath_alq_tx_fifo_push p;

	memcpy(&p, &a->payload, sizeof(p));
	printf("[%u.%06u] [%llu] TXPUSH txq=%d, nframes=%d, fifodepth=%d, frmcount=%d\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    be32toh(p.txq),
	    be32toh(p.nframes),
	    be32toh(p.fifo_depth),
	    be32toh(p.frame_cnt));
}

static void
ar9300_decode_txstatus(struct if_ath_alq_payload *a)
{
	struct ar9300_txs txs;

	/* XXX assumes txs is smaller than PAYLOAD_LEN! */
	memcpy(&txs, &a->payload, sizeof(struct ar9300_txs));

	printf("[%u.%06u] [%llu] TXSTATUS TxTimestamp=%u (%u), DescId=0x%04x, QCU=%d\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    txs.status4,
	    txs.status4 - last_ts,
	    (unsigned int) MS(txs.status1, AR_tx_desc_id),
	    (unsigned int) MS(txs.ds_info, AR_tx_qcu_num));
	printf("    DescId=0x%08x\n", txs.status1);

	last_ts = txs.status4;

	printf("    DescLen=%d, TxQcuNum=%d, CtrlStat=%d, DescId=0x%04x\n",
	    txs.ds_info & 0xff,
	    MS(txs.ds_info, AR_tx_qcu_num),
	    MS(txs.ds_info, AR_ctrl_stat),
	    MS(txs.ds_info, AR_desc_id));

	printf("    TxTimestamp: %u\n", txs.status4);

	printf("    TxDone=%d, SeqNo=%d, TxOpExceed=%d, TXBFStatus=%d\n",
	    MF(txs.status8, AR_tx_done),
	    MS(txs.status8, AR_seq_num),
	    MF(txs.status8, AR_tx_op_exceeded),
	    MS(txs.status8, AR_TXBFStatus));

	printf("    TXBfMismatch=%d, BFStreamMiss=%d, FinalTxIdx=%d\n",
	    MF(txs.status8, AR_tx_bf_bw_mismatch),
	    MF(txs.status8, AR_tx_bf_stream_miss),
	    MS(txs.status8, AR_final_tx_idx));

	printf("    TxBfDestMiss=%d, TxBfExpired=%d, PwrMgmt=%d, Tid=%d,"
	    " FastTsBit=%d\n",
	    MF(txs.status8, AR_tx_bf_dest_miss),
	    MF(txs.status8, AR_tx_bf_expired),
	    MF(txs.status8, AR_power_mgmt),
	    MS(txs.status8, AR_tx_tid),
	    MF(txs.status8, AR_tx_fast_ts));

	printf("    Frmok=%d, xretries=%d, fifounderrun=%d, filt=%d\n",
	    MF(txs.status3, AR_frm_xmit_ok),
	    MF(txs.status3, AR_excessive_retries),
	    MF(txs.status3, AR_fifounderrun),
	    MF(txs.status3, AR_filtered));
	printf("    DelimUnderrun=%d, DataUnderun=%d, DescCfgErr=%d,"
	    " TxTimerExceeded=%d\n",
	    MF(txs.status3, AR_tx_delim_underrun),
	    MF(txs.status3, AR_tx_data_underrun),
	    MF(txs.status3, AR_desc_cfg_err),
	    MF(txs.status3, AR_tx_timer_expired));

	printf("    RTScnt=%d, FailCnt=%d, VRetryCnt=%d\n",
	    MS(txs.status3, AR_rts_fail_cnt),
	    MS(txs.status3, AR_data_fail_cnt),
	    MS(txs.status3, AR_virt_retry_cnt));



	printf("    RX RSSI 0 [%d %d %d]\n",
	    MS(txs.status2, AR_tx_rssi_ant00),
	    MS(txs.status2, AR_tx_rssi_ant01),
	    MS(txs.status2, AR_tx_rssi_ant02));

	printf("    RX RSSI 1 [%d %d %d] Comb=%d\n",
	    MS(txs.status7, AR_tx_rssi_ant10),
	    MS(txs.status7, AR_tx_rssi_ant11),
	    MS(txs.status7, AR_tx_rssi_ant12),
	    MS(txs.status7, AR_tx_rssi_combined));

	printf("    BA Valid=%d\n",
	    MF(txs.status2, AR_tx_ba_status));

	printf("    BALow=0x%08x\n", txs.status5);
	printf("    BAHigh=0x%08x\n", txs.status6);

	printf("\n ------ \n");
}

/*
 * Note - these are rounded up to 128 bytes; but we
 * only use 96 bytes from it.
 */
static void
ar9300_decode_txdesc(struct if_ath_alq_payload *a)
{
	struct ar9300_txc txc;

	/* XXX assumes txs is smaller than PAYLOAD_LEN! */
	memcpy(&txc, &a->payload, 96);

	printf("[%u.%06u] [%llu] TXD DescId=0x%04x\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    (unsigned int) MS(txc.ds_ctl10, AR_tx_desc_id));

	printf("  DescLen=%d, TxQcuNum=%d, CtrlStat=%d, DescId=0x%04x\n",
	    txc.ds_info & 0xff,
	    MS(txc.ds_info, AR_tx_qcu_num),
	    MS(txc.ds_info, AR_ctrl_stat),
	    MS(txc.ds_info, AR_desc_id));

	/* link */
	printf("    Link 0x%08x\n", txc.ds_link);

	/* data0 */
	printf("    Data0 0x%08x Len %d\n",
	    txc.ds_data0,
	    MS(txc.ds_ctl3, AR_buf_len));

	/* data1 */
	printf("    Data1 0x%08x Len %d\n",
	    txc.ds_data1,
	    MS(txc.ds_ctl5, AR_buf_len));

	/* data2 */
	printf("    Data2 0x%08x Len %d\n",
	    txc.ds_data2,
	    MS(txc.ds_ctl7, AR_buf_len));

	/* data3 */
	printf("    Data3 0x%08x Len %d\n",
	    txc.ds_data3,
	    MS(txc.ds_ctl9, AR_buf_len));
	

	/* ctl10 */
	printf("    Desc ID=0x%04x, Chksum=0x%04x (ctl10=0x%08x)\n",
	    MS(txc.ds_ctl10, AR_tx_desc_id),
	    txc.ds_ctl10 & AR_tx_ptr_chk_sum,
	    txc.ds_ctl10);

	/* ctl11 */
	printf("    Frame Len=%d, VMF=%d, LowRxChain=%d, TxClrRetry=%d\n",
	     txc.ds_ctl11 & AR_frame_len,
	    MF(txc.ds_ctl11, AR_virt_more_frag),
	    MF(txc.ds_ctl11, AR_low_rx_chain),
	    MF(txc.ds_ctl11, AR_tx_clear_retry));
	printf("    TX power 0 = %d, RtsEna=%d, Veol=%d, ClrDstMask=%d\n",
	    MS(txc.ds_ctl11, AR_xmit_power0),
	    MF(txc.ds_ctl11, AR_rts_enable),
	    MF(txc.ds_ctl11, AR_veol),
	    MF(txc.ds_ctl11, AR_clr_dest_mask));
	printf("    TxIntrReq=%d, DestIdxValid=%d, CtsEnable=%d\n",
	    MF(txc.ds_ctl11, AR_tx_intr_req),
	    MF(txc.ds_ctl11, AR_dest_idx_valid),
	    MF(txc.ds_ctl11, AR_cts_enable));

	/* ctl12 */
	printf("    Paprd Chain Mask=0x%x, TxMore=%d, DestIdx=%d,"
	    " FrType=0x%x\n",
	    MS(txc.ds_ctl12, AR_paprd_chain_mask),
	    MF(txc.ds_ctl12, AR_tx_more),
	    MS(txc.ds_ctl12, AR_dest_idx),
	    MS(txc.ds_ctl12, AR_frame_type));
	printf("    NoAck=%d, InsertTs=%d, CorruptFcs=%d, ExtOnly=%d,"
	    " ExtAndCtl=%d\n",
	    MF(txc.ds_ctl12, AR_no_ack),
	    MF(txc.ds_ctl12, AR_insert_ts),
	    MF(txc.ds_ctl12, AR_corrupt_fcs),
	    MF(txc.ds_ctl12, AR_ext_only),
	    MF(txc.ds_ctl12, AR_ext_and_ctl));
	printf("    IsAggr=%d, MoreRifs=%d, LocMode=%d\n",
	    MF(txc.ds_ctl12, AR_is_aggr),
	    MF(txc.ds_ctl12, AR_more_rifs),
	    MF(txc.ds_ctl12, AR_loc_mode));


	/* ctl13 */
	printf("    DurUpEna=%d, Burstdur=0x%04x\n",
	    MF(txc.ds_ctl13, AR_dur_update_ena),
	    MS(txc.ds_ctl13, AR_burst_dur));
	printf("    Try0=%d, Try1=%d, Try2=%d, Try3=%d\n",
	    MS(txc.ds_ctl13, AR_xmit_data_tries0),
	    MS(txc.ds_ctl13, AR_xmit_data_tries1),
	    MS(txc.ds_ctl13, AR_xmit_data_tries2),
	    MS(txc.ds_ctl13, AR_xmit_data_tries3));

	/* ctl14 */
	printf("    rate0=0x%02x, rate1=0x%02x, rate2=0x%02x, rate3=0x%02x\n",
	    MS(txc.ds_ctl14, AR_xmit_rate0),
	    MS(txc.ds_ctl14, AR_xmit_rate1),
	    MS(txc.ds_ctl14, AR_xmit_rate2),
	    MS(txc.ds_ctl14, AR_xmit_rate3));

	/* ctl15 */
	printf("    try 0: PktDur=%d, RTS/CTS ena=%d\n",
	    MS(txc.ds_ctl15, AR_packet_dur0),
	    MF(txc.ds_ctl15, AR_rts_cts_qual0));
	printf("    try 1: PktDur=%d, RTS/CTS ena=%d\n",
	    MS(txc.ds_ctl15, AR_packet_dur1),
	    MF(txc.ds_ctl15, AR_rts_cts_qual1));

	/* ctl16 */
	printf("    try 2: PktDur=%d, RTS/CTS ena=%d\n",
	    MS(txc.ds_ctl16, AR_packet_dur2),
	    MF(txc.ds_ctl16, AR_rts_cts_qual2));
	printf("    try 3: PktDur=%d, RTS/CTS ena=%d\n",
	    MS(txc.ds_ctl16, AR_packet_dur3),
	    MF(txc.ds_ctl16, AR_rts_cts_qual3));

	/* ctl17 */
	printf("    AggrLen=%d, PadDelim=%d, EncrType=%d, TxDcApStaSel=%d\n",
	    MS(txc.ds_ctl17, AR_aggr_len),
	    MS(txc.ds_ctl17, AR_pad_delim),
	    MS(txc.ds_ctl17, AR_encr_type),
	    MF(txc.ds_ctl17, AR_tx_dc_ap_sta_sel));
	printf("    Calib=%d LDPC=%d\n",
	    MF(txc.ds_ctl17, AR_calibrating),
	    MF(txc.ds_ctl17, AR_ldpc));

	/* ctl18 */
	printf("    try 0: chainMask=0x%x, GI=%d, 2040=%d, STBC=%d\n",
	    MS(txc.ds_ctl18, AR_chain_sel0),
	    MF(txc.ds_ctl18, AR_gi0),
	    MF(txc.ds_ctl18, AR_2040_0),
	    MF(txc.ds_ctl18, AR_stbc0));
	printf("    try 1: chainMask=0x%x, GI=%d, 2040=%d, STBC=%d\n",
	    MS(txc.ds_ctl18, AR_chain_sel1),
	    MF(txc.ds_ctl18, AR_gi1),
	    MF(txc.ds_ctl18, AR_2040_1),
	    MF(txc.ds_ctl18, AR_stbc1));
	printf("    try 2: chainMask=0x%x, GI=%d, 2040=%d, STBC=%d\n",
	    MS(txc.ds_ctl18, AR_chain_sel2),
	    MF(txc.ds_ctl18, AR_gi2),
	    MF(txc.ds_ctl18, AR_2040_2),
	    MF(txc.ds_ctl18, AR_stbc2));
	printf("    try 3: chainMask=0x%x, GI=%d, 2040=%d, STBC=%d\n",
	    MS(txc.ds_ctl18, AR_chain_sel3),
	    MF(txc.ds_ctl18, AR_gi3),
	    MF(txc.ds_ctl18, AR_2040_3),
	    MF(txc.ds_ctl18, AR_stbc3));

	/* ctl19 */
	printf("    NotSounding=%d\n",
	    MF(txc.ds_ctl19, AR_not_sounding));

	printf("    try 0: ant=0x%08x, antsel=%d, ness=%d\n",
	    txc.ds_ctl19 &  AR_tx_ant0,
	    MF(txc.ds_ctl19, AR_tx_ant_sel0),
	    MS(txc.ds_ctl19, AR_ness));

	/* ctl20 */
	printf("    try 1: TxPower=%d, ant=0x%08x, antsel=%d, ness=%d\n",
	    MS(txc.ds_ctl20, AR_xmit_power1),
	    txc.ds_ctl20 &  AR_tx_ant1,
	    MF(txc.ds_ctl20, AR_tx_ant_sel1),
	    MS(txc.ds_ctl20, AR_ness1));

	/* ctl21 */
	printf("    try 2: TxPower=%d, ant=0x%08x, antsel=%d, ness=%d\n",
	    MS(txc.ds_ctl21, AR_xmit_power2),
	    txc.ds_ctl21 &  AR_tx_ant2,
	    MF(txc.ds_ctl21, AR_tx_ant_sel2),
	    MS(txc.ds_ctl21, AR_ness2));

	/* ctl22 */
	printf("    try 3: TxPower=%d, ant=0x%08x, antsel=%d, ness=%d\n",
	    MS(txc.ds_ctl22, AR_xmit_power3),
	    txc.ds_ctl22 &  AR_tx_ant3,
	    MF(txc.ds_ctl22, AR_tx_ant_sel3),
	    MS(txc.ds_ctl22, AR_ness3));

	printf("\n ------ \n");
}

static void
ar9300_decode_rxstatus(struct if_ath_alq_payload *a)
{
	struct ar9300_rxs rxs;

	/* XXX assumes rxs is smaller than PAYLOAD_LEN! */
	memcpy(&rxs, &a->payload, sizeof(struct ar9300_rxs));

	printf("[%u.%06u] [%llu] RXSTATUS RxTimestamp: %u (%d)\n",
	    (unsigned int) be32toh(a->hdr.tstamp_sec),
	    (unsigned int) be32toh(a->hdr.tstamp_usec),
	    (unsigned long long) be64toh(a->hdr.threadid),
	    rxs.status3,
	    rxs.status3 - last_ts);

	/* status1 */
	/* .. and status5 */
	printf("    RSSI %d/%d/%d / %d/%d/%d; combined: %d; rate=0x%02x\n",
	    MS(rxs.status1, AR_rx_rssi_ant00),
	    MS(rxs.status1, AR_rx_rssi_ant01),
	    MS(rxs.status1, AR_rx_rssi_ant02),
	    MS(rxs.status5, AR_rx_rssi_ant10),
	    MS(rxs.status5, AR_rx_rssi_ant11),
	    MS(rxs.status5, AR_rx_rssi_ant12),
	    MS(rxs.status5, AR_rx_rssi_combined),
	    MS(rxs.status1, AR_rx_rate));

	/* status2 */
	printf("    Len: %d; more=%d, delim=%d, upload=%d\n",
	    MS(rxs.status2, AR_data_len),
	    MF(rxs.status2, AR_rx_more),
	    MS(rxs.status2, AR_num_delim),
	    MS(rxs.status2, AR_hw_upload_data));

	/* status3 */
	printf("    RX timestamp: %u\n", rxs.status3);
	last_ts = rxs.status3;

	/* status4 */
	printf("    GI: %d, 2040: %d, parallel40: %d, stbc=%d\n",
	    MF(rxs.status4, AR_gi),
	    MF(rxs.status4, AR_2040),
	    MF(rxs.status4, AR_parallel40),
	    MF(rxs.status4, AR_rx_stbc));
	printf("    Not sounding: %d, ness: %d, upload_valid: %d\n",
	    MF(rxs.status4, AR_rx_not_sounding),
	    MS(rxs.status4, AR_rx_ness),
	    MS(rxs.status4, AR_hw_upload_data_valid));
	printf("    RX antenna: 0x%08x\n",
	    MS(rxs.status4, AR_rx_antenna));

	/* EVM */
	/* status6 - 9 */
	printf("    EVM: 0x%08x; 0x%08x; 0x%08x; 0x%08x\n",
	    rxs.status6,
	    rxs.status7,
	    rxs.status8,
	    rxs.status9);

	/* status10 - ? */

	/* status11 */
	printf("    RX done: %d, RX frame ok: %d, CRC error: %d\n",
	    MF(rxs.status11, AR_rx_done),
	    MF(rxs.status11, AR_rx_frame_ok),
	    MF(rxs.status11, AR_crc_err));
	printf("    Decrypt CRC err: %d, PHY err: %d, MIC err: %d\n",
	    MF(rxs.status11, AR_decrypt_crc_err),
	    MF(rxs.status11, AR_phyerr),
	    MF(rxs.status11, AR_michael_err));
	printf("    Pre delim CRC err: %d, uAPSD Trig: %d\n",
	    MF(rxs.status11, AR_pre_delim_crc_err),
	    MF(rxs.status11, AR_apsd_trig));
	printf("    RXKeyIdxValid: %d, KeyIdx: %d, PHY error: %d\n",
	    MF(rxs.status11, AR_rx_key_idx_valid),
	    MS(rxs.status11, AR_key_idx),
	    MS(rxs.status11, AR_phy_err_code));
	printf("    RX more Aggr: %d, RX aggr %d, post delim CRC err: %d\n",
	    MF(rxs.status11, AR_rx_more_aggr),
	    MF(rxs.status11, AR_rx_aggr),
	    MF(rxs.status11, AR_post_delim_crc_err));
	printf("    hw upload data type: %d; position bit: %d\n",
	    MS(rxs.status11, AR_hw_upload_data_type),
	    MF(rxs.status11, AR_position_bit));
	printf("    Hi RX chain: %d, RxFirstAggr: %d, DecryptBusy: %d, KeyMiss: %d\n",
	    MF(rxs.status11, AR_hi_rx_chain),
	    MF(rxs.status11, AR_rx_first_aggr),
	    MF(rxs.status11, AR_decrypt_busy_err),
	    MF(rxs.status11, AR_key_miss));
}

void
ar9300_alq_payload(struct if_ath_alq_payload *a)
{

		switch (be16toh(a->hdr.op)) {
			case ATH_ALQ_EDMA_TXSTATUS:	/* TXSTATUS */
				ar9300_decode_txstatus(a);
				break;
			case ATH_ALQ_EDMA_RXSTATUS:	/* RXSTATUS */
				ar9300_decode_rxstatus(a);
				break;
			case ATH_ALQ_EDMA_TXDESC:	/* TXDESC */
				ar9300_decode_txdesc(a);
				break;
			default:
				printf("[%d.%06d] [%lld] op: %d; len %d\n",
				    be32toh(a->hdr.tstamp_sec),
				    be32toh(a->hdr.tstamp_usec),
				    be64toh(a->hdr.threadid),
				    be16toh(a->hdr.op), be16toh(a->hdr.len));
		}
}
