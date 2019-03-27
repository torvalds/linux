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

#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300desc.h"


/*
 * Process an RX descriptor, and return the status to the caller.
 * Copy some hardware specific items into the software portion
 * of the descriptor.
 *
 * NB: the caller is responsible for validating the memory contents
 *     of the descriptor (e.g. flushing any cached copy).
 */
HAL_STATUS
ar9300_proc_rx_desc_fast(struct ath_hal *ah, struct ath_desc *ds,
    u_int32_t pa, struct ath_desc *nds, struct ath_rx_status *rxs,
    void *buf_addr)
{
    struct ar9300_rxs *rxsp = AR9300RXS(buf_addr);

    /*
    ath_hal_printf(ah,"CHH=RX: ds_info 0x%x  status1: 0x%x  status11: 0x%x\n",
                        rxsp->ds_info,rxsp->status1,rxsp->status11);
     */

    if ((rxsp->status11 & AR_rx_done) == 0) {
        return HAL_EINPROGRESS;
    }

    if (MS(rxsp->ds_info, AR_desc_id) != 0x168c) {
#if __PKT_SERIOUS_ERRORS__
       /*BUG: 63564-HT */
        HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE, "%s: Rx Descriptor error 0x%x\n",
                 __func__, rxsp->ds_info);
#endif
        return HAL_EINVAL;
    }

    if ((rxsp->ds_info & (AR_tx_rx_desc | AR_ctrl_stat)) != 0) {
#if __PKT_SERIOUS_ERRORS__
        HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
            "%s: Rx Descriptor wrong info 0x%x\n", __func__, rxsp->ds_info);
#endif
        return HAL_EINPROGRESS;
    }

    rxs->rs_status = 0;
    rxs->rs_flags =  0;
    rxs->rs_phyerr = 0;

    rxs->rs_datalen = rxsp->status2 & AR_data_len;
    rxs->rs_tstamp =  rxsp->status3;

    /* XXX what about key_cache_miss? */
    rxs->rs_rssi = MS(rxsp->status5, AR_rx_rssi_combined);
    rxs->rs_rssi_ctl[0] = MS(rxsp->status1, AR_rx_rssi_ant00);
    rxs->rs_rssi_ctl[1] = MS(rxsp->status1, AR_rx_rssi_ant01);
    rxs->rs_rssi_ctl[2] = MS(rxsp->status1, AR_rx_rssi_ant02);
    rxs->rs_rssi_ext[0] = MS(rxsp->status5, AR_rx_rssi_ant10);
    rxs->rs_rssi_ext[1] = MS(rxsp->status5, AR_rx_rssi_ant11);
    rxs->rs_rssi_ext[2] = MS(rxsp->status5, AR_rx_rssi_ant12);
    if (rxsp->status11 & AR_rx_key_idx_valid) {
        rxs->rs_keyix = MS(rxsp->status11, AR_key_idx);
    } else {
        rxs->rs_keyix = HAL_RXKEYIX_INVALID;
    }
    /* NB: caller expected to do rate table mapping */
    rxs->rs_rate = MS(rxsp->status1, AR_rx_rate);
    rxs->rs_more = (rxsp->status2 & AR_rx_more) ? 1 : 0;

    rxs->rs_isaggr = (rxsp->status11 & AR_rx_aggr) ? 1 : 0;
    rxs->rs_moreaggr = (rxsp->status11 & AR_rx_more_aggr) ? 1 : 0;
    rxs->rs_antenna = (MS(rxsp->status4, AR_rx_antenna) & 0x7);
    rxs->rs_flags = (rxsp->status11 & AR_apsd_trig) ? HAL_RX_IS_APSD : 0;
    rxs->rs_flags |= (rxsp->status4 & AR_gi) ? HAL_RX_GI : 0;
    rxs->rs_flags |= (rxsp->status4 & AR_2040) ? HAL_RX_2040 : 0;

    /* TX beamforming; CSI for locationing */
    rxs->rs_flags |= (rxsp->status2 & AR_hw_upload_data) ? HAL_RX_HW_UPLOAD_DATA : 0;
    rxs->rs_flags |= (rxsp->status4 & AR_rx_not_sounding) ? 0 : HAL_RX_HW_SOUNDING;
    rxs->rs_ness = MS(rxsp->status4, AR_rx_ness);
    rxs->rs_flags |= (rxsp->status4 & AR_hw_upload_data_valid) ? HAL_RX_UPLOAD_VALID : 0;
    rxs->rs_hw_upload_data_type = MS(rxsp->status11, AR_hw_upload_data_type);

    /* Copy EVM information */
    rxs->rs_evm0 = rxsp->status6;
    rxs->rs_evm1 = rxsp->status7;
    rxs->rs_evm2 = rxsp->status8;
    rxs->rs_evm3 = rxsp->status9;
    rxs->rs_evm4 = (rxsp->status10 & 0xffff);

    if (rxsp->status11 & AR_pre_delim_crc_err) {
        rxs->rs_flags |= HAL_RX_DELIM_CRC_PRE;
    }
    if (rxsp->status11 & AR_post_delim_crc_err) {
        rxs->rs_flags |= HAL_RX_DELIM_CRC_POST;
    }
    if (rxsp->status11 & AR_decrypt_busy_err) {
        rxs->rs_flags |= HAL_RX_DECRYPT_BUSY;
    }
    if (rxsp->status11 & AR_hi_rx_chain) {
        rxs->rs_flags |= HAL_RX_HI_RX_CHAIN;
    }
    if (rxsp->status11 & AR_key_miss) {
        rxs->rs_status |= HAL_RXERR_KEYMISS;
    }

    if ((rxsp->status11 & AR_rx_frame_ok) == 0) {
        /*
         * These four bits should not be set together.  The
         * 9300 spec states a Michael error can only occur if
         * decrypt_crc_err not set (and TKIP is used).  Experience
         * indicates however that you can also get Michael errors
         * when a CRC error is detected, but these are specious.
         * Consequently we filter them out here so we don't
         * confuse and/or complicate drivers.
         */

        if (rxsp->status11 & AR_crc_err) {
            rxs->rs_status |= HAL_RXERR_CRC;
            /*
             * ignore CRC flag for phy reports
             */
            if (rxsp->status11 & AR_phyerr) {
                u_int phyerr = MS(rxsp->status11, AR_phy_err_code);
                rxs->rs_status |= HAL_RXERR_PHY;
                rxs->rs_phyerr = phyerr;
            }
        } else if (rxsp->status11 & AR_phyerr) {
            u_int phyerr;

            /*
             * Packets with OFDM_RESTART on post delimiter are CRC OK and
             * usable and MAC ACKs them.
             * To avoid packet from being lost, we remove the PHY Err flag
             * so that lmac layer does not drop them.
             * (EV 70071)
             */
            phyerr = MS(rxsp->status11, AR_phy_err_code);
            if ((phyerr == HAL_PHYERR_OFDM_RESTART) && 
                    (rxsp->status11 & AR_post_delim_crc_err)) {
                rxs->rs_phyerr = 0;
            } else {
                rxs->rs_status |= HAL_RXERR_PHY;
                rxs->rs_phyerr = phyerr;
            }
        } else if (rxsp->status11 & AR_decrypt_crc_err) {
            rxs->rs_status |= HAL_RXERR_DECRYPT;
        } else if (rxsp->status11 & AR_michael_err) {
            rxs->rs_status |= HAL_RXERR_MIC;
        }
    } else {
        if (rxsp->status11 & AR_position_bit) {
#if 1
            rxs->rs_flags |= HAL_RX_LOC_INFO;
#else
            /*
             * If the locationing counter is enabled, Osprey always
             * seems to put AR_position_bit in each frame.
             * So, only do this if we also have a valid upload
             * and it's type "1" (which I'm guessing is CSI.)
             */
            if ((rxs->rs_flags & HAL_RX_UPLOAD_VALID) &&
                (rxs->rs_hw_upload_data_type == 1)) {
                    rxs->rs_flags |= HAL_RX_LOC_INFO;
            }
#endif
        }
    }
#if 0
    rxs->rs_channel = AH_PRIVATE(ah)->ah_curchan->channel;
#endif
    return HAL_OK;
}

HAL_STATUS
ar9300_proc_rx_desc(struct ath_hal *ah, struct ath_desc *ds,
    u_int32_t pa, struct ath_desc *nds, u_int64_t tsf, 
    struct ath_rx_status *rxs)
{
    return HAL_ENOTSUPP;
}

/*
 * rx path in ISR is different for ar9300 from ar5416, and
 * ath_rx_proc_descfast will not be called if edmasupport is true.
 * So this function ath_hal_get_rxkeyidx will not be 
 * called for ar9300.
 * This function in ar9300's HAL is just a stub one because we need 
 * to link something to the callback interface of the HAL module.
 */
HAL_STATUS
ar9300_get_rx_key_idx(struct ath_hal *ah, struct ath_desc *ds, u_int8_t *keyix,
    u_int8_t *status)
{
    *status = 0;
    *keyix = HAL_RXKEYIX_INVALID;    
    return HAL_ENOTSUPP;
}
