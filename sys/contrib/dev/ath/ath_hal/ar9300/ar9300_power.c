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
#include "ah_internal.h"

#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"

#if ATH_WOW_OFFLOAD
void ar9300_wowoffload_prep(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    ahp->ah_mcast_filter_l32_set = 0;
    ahp->ah_mcast_filter_u32_set = 0;
}

void ar9300_wowoffload_post(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t val;

    if (ahp->ah_mcast_filter_l32_set != 0) {
        val = OS_REG_READ(ah, AR_MCAST_FIL0);
        val &= ~ahp->ah_mcast_filter_l32_set;
        OS_REG_WRITE(ah, AR_MCAST_FIL0, val);
    }
    if (ahp->ah_mcast_filter_u32_set != 0) {
        val = OS_REG_READ(ah, AR_MCAST_FIL1);
        val &= ~ahp->ah_mcast_filter_u32_set;
        OS_REG_WRITE(ah, AR_MCAST_FIL1, val);
    }

    ahp->ah_mcast_filter_l32_set = 0;
    ahp->ah_mcast_filter_u32_set = 0;
}

static void ar9300_wowoffload_add_mcast_filter(struct ath_hal *ah, u_int8_t *mc_addr)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t reg, val;
    u_int8_t  pos, high32;

    memcpy((u_int8_t *) &val, &mc_addr[0], 3);
    pos = (val >> 18) ^ (val >> 12) ^ (val >> 6) ^ val;
    memcpy((u_int8_t *) &val, &mc_addr[3], 3);
    pos ^= (val >> 18) ^ (val >> 12) ^ (val >> 6) ^ val;
    high32 = pos & 0x20;
    reg = high32 ? AR_MCAST_FIL1 : AR_MCAST_FIL0;
    pos &= 0x1F;

    val = OS_REG_READ(ah, reg);
    if ((val & (1 << pos)) == 0) {
        val |= (1 << pos);
        if (high32) {
            ahp->ah_mcast_filter_u32_set |= (1 << pos);
        } else {
            ahp->ah_mcast_filter_l32_set |= (1 << pos);
        }
        OS_REG_WRITE(ah, reg, val);
    }
}

/*
 * DeviceID SWAR - EV91928
 *
 * During SW WOW, 0x4004[13] is set to allow BT eCPU to access WLAN MAC
 * registers. Setting 00x4004[13] will prevent eeprom state machine to
 * load customizable PCIE configuration registers, which lead to the PCIE
 * device id stay as default 0xABCD. The SWAR to have BT eCPU to write
 * to PCIE registers as soon as it detects PCIE reset is deasserted.
 */
void ar9300_wowoffload_download_devid_swar(struct ath_hal *ah)
{
    u_int32_t addr = AR_WOW_OFFLOAD_WLAN_REGSET_NUM;

    OS_REG_WRITE(ah, addr, 8);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x5000);
    addr += 4;
    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) pcie_000 = %08x\n",
             AH_PRIVATE(ah)->ah_config.ath_hal_pcie_000);
    OS_REG_WRITE(ah, addr, AH_PRIVATE(ah)->ah_config.ath_hal_pcie_000);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x5008);
    addr += 4;
    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) pcie_008 = %08x\n",
             AH_PRIVATE(ah)->ah_config.ath_hal_pcie_008);
    OS_REG_WRITE(ah, addr, AH_PRIVATE(ah)->ah_config.ath_hal_pcie_008);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x502c);
    addr += 4;
    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) pcie_02c = %08x\n",
             AH_PRIVATE(ah)->ah_config.ath_hal_pcie_02c);
    OS_REG_WRITE(ah, addr, AH_PRIVATE(ah)->ah_config.ath_hal_pcie_02c);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x18c00);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x18212ede);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x18c04);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x008001d8);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x18c08);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x0003580c);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x570c);
    addr += 4;
    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) pcie_70c = %08x\n",
             AH_PRIVATE(ah)->ah_config.ath_hal_pcie_70c);
    OS_REG_WRITE(ah, addr, AH_PRIVATE(ah)->ah_config.ath_hal_pcie_70c);
    addr += 4;
    OS_REG_WRITE(ah, addr, 0x5040);
    addr += 4;
    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) pcie_040 = %08x\n",
             AH_PRIVATE(ah)->ah_config.ath_hal_pcie_040);
    OS_REG_WRITE(ah, addr, AH_PRIVATE(ah)->ah_config.ath_hal_pcie_040);
    addr += 4;
/*
    A_SOC_REG_WRITE(0x45000, 0x0034168c);
    A_SOC_REG_WRITE(0x45008, 0x02800001);
    A_SOC_REG_WRITE(0x4502c, 0x3117168c);
    A_SOC_REG_WRITE(0x58c00, 0x18212ede);
    A_SOC_REG_WRITE(0x58c04, 0x000801d8);
    A_SOC_REG_WRITE(0x58c08, 0x0003580c);
    A_SOC_REG_WRITE(0x4570c, 0x275f3f01);
    A_SOC_REG_WRITE(0x45040, 0xffc25001);
*/
}

/* Retrieve updated information from MAC PCU buffer.
 * Embedded CPU would have written the value before exiting WoW
 * */
void ar9300_wowoffload_retrieve_data(struct ath_hal *ah, void *buf, u_int32_t param)
{
    u_int32_t rc_lower, rc_upper;

    if (param == WOW_PARAM_REPLAY_CNTR) {
        rc_lower = OS_REG_READ(ah, AR_WOW_TXBUF(0));
        rc_upper = OS_REG_READ(ah, AR_WOW_TXBUF(1));
        *(u_int64_t *)buf = rc_lower + (rc_upper << 32);
    }
    else if (param == WOW_PARAM_KEY_TSC) {
        rc_lower = OS_REG_READ(ah, AR_WOW_TXBUF(2));
        rc_upper = OS_REG_READ(ah, AR_WOW_TXBUF(3));
        *(u_int64_t *)buf = rc_lower + (rc_upper << 32);
    }
    else if (param == WOW_PARAM_TX_SEQNUM) {
        *(u_int32_t *)buf = OS_REG_READ(ah, AR_WOW_TXBUF(4));
    }

}

/* Download GTK rekey related information to the embedded CPU */
u_int32_t ar9300_wowoffload_download_rekey_data(struct ath_hal *ah, u_int32_t *data, u_int32_t bytes)
{
    int i;
    int mbox_status = OS_REG_READ(ah, AR_MBOX_CTRL_STATUS);
    u_int32_t gtk_data_start;

    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) %s, bytes=%d\n", __func__, bytes);
    if (AR_SREV_JUPITER(ah) &&
        (bytes > (AR_WOW_OFFLOAD_GTK_DATA_WORDS_JUPITER * 4)))
    {
        bytes = AR_WOW_OFFLOAD_GTK_DATA_WORDS_JUPITER * 4;
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) bytes truncated to %d\n", bytes);
    }
    /* Check if mailbox is busy */
    if (mbox_status != 0) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "%s: Mailbox register busy! Reg = 0x%x", __func__, mbox_status);
        return 1;
    }

    /* Clear status */
    OS_REG_WRITE(ah, AR_EMB_CPU_WOW_STATUS, 0x0);
    OS_REG_WRITE(ah, AR_WLAN_WOW_ENABLE, 0);
    OS_REG_WRITE(ah, AR_WLAN_WOW_STATUS, 0xFFFFFFFF);

    if (AR_SREV_JUPITER(ah)) {
        gtk_data_start = AR_WOW_OFFLOAD_GTK_DATA_START_JUPITER;
    } else {
        gtk_data_start = AR_WOW_OFFLOAD_GTK_DATA_START;
    }
    for (i = 0;i < bytes/4; i++) {
        OS_REG_WRITE(ah, gtk_data_start + i * 4, data[i]);
    }

    return 0;
}

void ar9300_wowoffload_download_acer_magic( struct ath_hal *ah,
                                            HAL_BOOL      valid,
                                            u_int8_t* datap,
                                            u_int32_t bytes)
{
    u_int32_t *p32 = (u_int32_t *) datap;
    u_int32_t l = 0, u = 0;

    if (valid) {
        l = *p32;
        p32++;
        u = *(u_int16_t *) p32;
    }

    OS_REG_WRITE(ah, AR_WOW_OFFLOAD_ACER_MAGIC_START, l);
    OS_REG_WRITE(ah, AR_WOW_OFFLOAD_ACER_MAGIC_START + 4, u);

    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
        "%s: Aer Magic: %02x-%02x-%02x-%02x-%02x-%02x\n", __func__,
        datap[0], datap[1], datap[2], datap[3], datap[4], datap[5]);
}

void ar9300_wowoffload_download_acer_swka(  struct ath_hal *ah,
                                            u_int32_t  id,
                                            HAL_BOOL       valid,
                                            u_int32_t  period,
                                            u_int32_t  size,
                                            u_int32_t* datap)
{
    u_int32_t ka_period[2] = {
        AR_WOW_OFFLOAD_ACER_KA0_PERIOD_MS,
        AR_WOW_OFFLOAD_ACER_KA1_PERIOD_MS
    };
    u_int32_t ka_size[2] = {
        AR_WOW_OFFLOAD_ACER_KA0_SIZE,
        AR_WOW_OFFLOAD_ACER_KA1_SIZE
    };
    u_int32_t ka_data[2] = {
        AR_WOW_OFFLOAD_ACER_KA0_DATA,
        AR_WOW_OFFLOAD_ACER_KA1_DATA
    };
    u_int32_t n_data = AR_WOW_OFFLOAD_ACER_KA0_DATA_WORDS;
    int i;

    if (id >= 2) {
        return;
    }

    if (valid) {
        OS_REG_WRITE(ah, ka_period[id], period);
        OS_REG_WRITE(ah, ka_size[id], size);
    } else {
        OS_REG_WRITE(ah, ka_period[id], 0);
        OS_REG_WRITE(ah, ka_size[id], 0);
    }
    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "%s: id=%d, period=%d ms, size=%d bytes\n",
            __func__, id, period, size);

    if (size < (n_data * 4)) {
        n_data = (size + 3) / 4;
    }
    for (i=0; i<n_data * 4; i+=4) {
        OS_REG_WRITE(ah, ka_data[id] + i, *datap);
        /*HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) %08x\n", *datap);*/
        datap++;
    }
}

void ar9300_wowoffload_download_arp_info(struct ath_hal *ah, u_int32_t id, u_int32_t *data)
{
    u_int32_t addr;
    struct hal_wow_offload_arp_info *p_info = (struct hal_wow_offload_arp_info *) data;

    if (id == 0) {
        addr = AR_WOW_OFFLOAD_ARP0_VALID;
    } else if (id == 1) {
        addr = AR_WOW_OFFLOAD_ARP1_VALID;
    } else {
        return;
    }

    if (p_info->valid) {
        OS_REG_WRITE(ah, addr, 0x1);
        addr += 4;
        OS_REG_WRITE(ah, addr, p_info->RemoteIPv4Address.u32);
        addr += 4;
        OS_REG_WRITE(ah, addr, p_info->HostIPv4Address.u32);
        addr += 4;
        OS_REG_WRITE(ah, addr, p_info->MacAddress.u32[0]);
        addr += 4;
        OS_REG_WRITE(ah, addr, p_info->MacAddress.u32[1]);
    } else {
        OS_REG_WRITE(ah, addr, 0x0);
    }
}

#define WOW_WRITE_NS_IPV6_ADDRESS(_ah, _buf_addr, _p_ipv6_addr) \
    {                                                           \
        u_int32_t   offset = (_buf_addr);                       \
        u_int32_t  *p_ipv6_addr = (u_int32_t *) (_p_ipv6_addr); \
        int i;                                                  \
        for (i = 0; i < 4; i++) {                               \
            OS_REG_WRITE((_ah), offset, *p_ipv6_addr);          \
            offset += 4;                                        \
            p_ipv6_addr ++;                                     \
        }                                                       \
    }

void ar9300_wowoffload_download_ns_info(struct ath_hal *ah, u_int32_t id, u_int32_t *data)
{
    u_int32_t addr;
    struct hal_wow_offload_ns_info *p_info = (struct hal_wow_offload_ns_info *) data;
    u_int8_t mc_addr[6];

    if (id == 0) {
        addr = AR_WOW_OFFLOAD_NS0_VALID;
    } else if (id == 1) {
        addr = AR_WOW_OFFLOAD_NS1_VALID;
    } else {
        return;
    }

    if (p_info->valid) {
        OS_REG_WRITE(ah, addr, 0x1);
        addr += 4;
        WOW_WRITE_NS_IPV6_ADDRESS(ah, addr, &p_info->RemoteIPv6Address.u32[0]);
        addr += 4 * 4;
        WOW_WRITE_NS_IPV6_ADDRESS(ah, addr, &p_info->SolicitedNodeIPv6Address.u32[0]);
        addr += 4 * 4;
        OS_REG_WRITE(ah, addr, p_info->MacAddress.u32[0]);
        addr += 4;
        OS_REG_WRITE(ah, addr, p_info->MacAddress.u32[1]);
        addr += 4;
        WOW_WRITE_NS_IPV6_ADDRESS(ah, addr, &p_info->TargetIPv6Addresses[0].u32[0]);
        addr += 4 * 4;
        WOW_WRITE_NS_IPV6_ADDRESS(ah, addr, &p_info->TargetIPv6Addresses[1].u32[0]);

        mc_addr[0] = 0x33;
        mc_addr[1] = 0x33;
        mc_addr[2] = 0xFF;
        mc_addr[3] = p_info->SolicitedNodeIPv6Address.u8[13];
        mc_addr[4] = p_info->SolicitedNodeIPv6Address.u8[14];
        mc_addr[5] = p_info->SolicitedNodeIPv6Address.u8[15];
        ar9300_wowoffload_add_mcast_filter(ah, mc_addr);
    } else {
        OS_REG_WRITE(ah, addr, 0x0);
    }
}

/* Download transmit parameters for GTK response frame during WoW
 * offload */
u_int32_t ar9300_wow_offload_download_hal_params(struct ath_hal *ah)
{
    u_int32_t tpc = 0x3f; /* Transmit Power Control */
    u_int32_t tx_tries_series = 7;  
    u_int32_t tx_rate_series, transmit_rate; 
    u_int32_t gtk_txdesc_param_start;

    if (AH_PRIVATE(ah)->ah_curchan->channel_flags & CHANNEL_CCK) {
        transmit_rate = 0x1B;    /* CCK_1M */
    } else {
        transmit_rate = 0xB;     /* OFDM_6M */
    }

    /* Use single rate for now. Change later as need be */
    tx_rate_series  = transmit_rate;
    tx_tries_series = 7;

    if (AR_SREV_JUPITER(ah)) {
        gtk_txdesc_param_start = AR_WOW_OFFLOAD_GTK_TXDESC_PARAM_START_JUPITER;
    } else {
        gtk_txdesc_param_start = AR_WOW_OFFLOAD_GTK_TXDESC_PARAM_START;
    }
#define AR_WOW_OFFLOAD_GTK_TXDESC_PARAM(x) (gtk_txdesc_param_start + ((x) * 4))

    /* Do not change the data order unless firmware code on embedded
     * CPU is changed correspondingly */
    OS_REG_WRITE(ah, AR_WOW_OFFLOAD_GTK_TXDESC_PARAM(0), tx_rate_series);
    OS_REG_WRITE(ah, AR_WOW_OFFLOAD_GTK_TXDESC_PARAM(1), tx_tries_series);
    OS_REG_WRITE(ah, AR_WOW_OFFLOAD_GTK_TXDESC_PARAM(2), AH9300(ah)->ah_tx_chainmask);
    OS_REG_WRITE(ah, AR_WOW_OFFLOAD_GTK_TXDESC_PARAM(3), tpc);

    return 0;
}

/* Indicate to the embedded CPU that host is ready to enter WoW mode.
 * Embedded CPU will copy relevant information from the MAC PCU buffer
 */
u_int32_t ar9300_wow_offload_handshake(struct ath_hal *ah, u_int32_t pattern_enable)
{
    int val;
    int mbox_status = OS_REG_READ(ah, AR_MBOX_CTRL_STATUS);
#if ATH_WOW_OFFLOAD
    u_int32_t bt_handshake_timeout_us = HAL_WOW_CTRL_WAIT_BT_TO(ah) * 100000;

#define AH_DEFAULT_BT_WAIT_TIMEOUT  3000000; /* 3 sec */
    if (bt_handshake_timeout_us == 0) {
        bt_handshake_timeout_us = AH_DEFAULT_BT_WAIT_TIMEOUT;
    }
    HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) TIMEOUT: %d us\n", bt_handshake_timeout_us);
#endif /* ATH_WOW_OFFLOAD */

    if (mbox_status & AR_MBOX_WOW_REQ) {
        /* WOW mode request handshake is already in progress. 
         * Do nothing */
        return 0;
    }

    /* Clear status */
    OS_REG_WRITE(ah, AR_MBOX_CTRL_STATUS, 0);
    OS_REG_WRITE(ah, AR_EMB_CPU_WOW_STATUS, 0x0);
    OS_REG_WRITE(ah, AR_WLAN_WOW_ENABLE, 0);
    OS_REG_WRITE(ah, AR_WLAN_WOW_STATUS, 0xFFFFFFFF);

    OS_REG_WRITE(ah, AR_RIMT, 0);
    OS_REG_WRITE(ah, AR_TIMT, 0); 

    val = 0;
    if (pattern_enable & AH_WOW_USER_PATTERN_EN) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - User pattern\n");
        val |= AR_EMB_CPU_WOW_ENABLE_PATTERN_MATCH;
    }
    else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - User pattern\n");
    }
    if ((pattern_enable & AH_WOW_MAGIC_PATTERN_EN)
#if ATH_WOW_OFFLOAD
        || (pattern_enable & AH_WOW_ACER_MAGIC_EN)
#endif
        )
    {
        val |= AR_EMB_CPU_WOW_ENABLE_MAGIC_PATTERN;
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - Magic pattern\n");
    }
    else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - Magic pattern\n");
    }
    if ((pattern_enable & AH_WOW_LINK_CHANGE)
#if ATH_WOW_OFFLOAD
        || HAL_WOW_CTRL(ah, HAL_WOW_OFFLOAD_KAFAIL_ENABLE)
#endif
        )
    {
        val |= AR_EMB_CPU_WOW_ENABLE_KEEP_ALIVE_FAIL;
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - Kepp alive fail\n");
    }
    else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - Kepp alive fail\n");
    }
    if (pattern_enable & AH_WOW_BEACON_MISS) {
        val |= AR_EMB_CPU_WOW_ENABLE_BEACON_MISS;
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - Becon Miss\n");
    }
    else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - Becon Miss\n");
    }

    OS_REG_WRITE(ah, AR_EMB_CPU_WOW_ENABLE, val);

    OS_REG_CLR_BIT(ah, AR_MBOX_CTRL_STATUS, AR_MBOX_WOW_CONF);
    OS_REG_SET_BIT(ah, AR_MBOX_CTRL_STATUS, AR_MBOX_WOW_REQ);
    OS_REG_SET_BIT(ah, AR_MBOX_CTRL_STATUS, AR_MBOX_INT_EMB_CPU);

    if (!ath_hal_waitfor(ah, AR_MBOX_CTRL_STATUS, AR_MBOX_WOW_CONF, AR_MBOX_WOW_CONF, bt_handshake_timeout_us)) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "%s: WoW offload handshake failed", __func__);
        return 0;
    }
    else {
        OS_REG_CLR_BIT(ah, AR_MBOX_CTRL_STATUS, AR_MBOX_WOW_CONF);
        HALDEBUG(ah, HAL_DEBUG_POWER_MGMT, "%s: WoW offload handshake successful",__func__);
    } 
    return 1;
}
#endif /* ATH_WOW_OFFLOAD */

/*
 * Notify Power Mgt is enabled in self-generated frames.
 * If requested, force chip awake.
 *
 * Returns A_OK if chip is awake or successfully forced awake.
 *
 * WARNING WARNING WARNING
 * There is a problem with the chip where sometimes it will not wake up.
 */
HAL_BOOL
ar9300_set_power_mode_awake(struct ath_hal *ah, int set_chip)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
#define POWER_UP_TIME   10000
    u_int32_t val;
    int i;

    /* Set Bits 14 and 17 of AR_WA before powering on the chip. */
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_WA), ahp->ah_wa_reg_val);
    OS_DELAY(10); /* delay to allow the write to take effect. */

    if (set_chip) {
        /* Do a Power-On-Reset if MAC is shutdown */
        if ((OS_REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_SHUTDOWN)) {
            if (ar9300_set_reset_reg(ah, HAL_RESET_POWER_ON) != AH_TRUE) {
                HALASSERT(0);
                return AH_FALSE;
            }
        }

        OS_REG_SET_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);

        OS_DELAY(50);

        for (i = POWER_UP_TIME / 50; i > 0; i--) {
            val = OS_REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M;
            if (val == AR_RTC_STATUS_ON) {
                break;
            }
            OS_DELAY(50);
            OS_REG_SET_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);
        }
        if (i == 0) {
            HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "%s: Failed to wakeup in %uus\n",
                     __func__, POWER_UP_TIME / 20);
            return AH_FALSE;
        }

    }

    OS_REG_CLR_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
    return AH_TRUE;
#undef POWER_UP_TIME
}

/*
 * Notify Power Mgt is disabled in self-generated frames.
 * If requested, force chip to sleep.
 */
static void
ar9300_set_power_mode_sleep(struct ath_hal *ah, int set_chip)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    OS_REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
    if (set_chip ) {
        if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
            OS_REG_WRITE(ah, AR_TIMER_MODE,
                    OS_REG_READ(ah, AR_TIMER_MODE) & 0xFFFFFF00);
            OS_REG_WRITE(ah, AR_GEN_TIMERS2_MODE,
                    OS_REG_READ(ah, AR_GEN_TIMERS2_MODE) & 0xFFFFFF00);
            OS_REG_WRITE(ah, AR_SLP32_INC,
                    OS_REG_READ(ah, AR_SLP32_INC) & 0xFFF00000);
            OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_EN, 0);
            OS_DELAY(100);
        }
        /* Clear the RTC force wake bit to allow the mac to go to sleep */
        OS_REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);

        if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
            /*
             * In Jupiter, after enter sleep mode, hardware will send
             * a SYS_SLEEPING message through MCI interface. Add a
             * few us delay to make sure the message can reach BT side.
             */
            OS_DELAY(100);
        }

        if (!AR_SREV_JUPITER_10(ah)) {
            /* Shutdown chip. Active low */
            OS_REG_CLR_BIT(ah, AR_RTC_RESET, AR_RTC_RESET_EN);
            /* Settle time */
            OS_DELAY(2);
        }
    }

#if ATH_WOW_OFFLOAD
    if (!AR_SREV_JUPITER(ah) || !HAL_WOW_CTRL(ah, HAL_WOW_OFFLOAD_SET_4004_BIT14))
#endif /* ATH_WOW_OFFLOAD */
    {
        /* Clear Bit 14 of AR_WA after putting chip into Full Sleep mode. */
        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_WA),
               ahp->ah_wa_reg_val & ~AR_WA_D3_TO_L1_DISABLE);
    }
}

/*
 * Notify Power Management is enabled in self-generating
 * frames. If request, set power mode of chip to
 * auto/normal.  Duration in units of 128us (1/8 TU).
 */
static void
ar9300_set_power_mode_network_sleep(struct ath_hal *ah, int set_chip)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    
    OS_REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
    if (set_chip) {
        HAL_CAPABILITIES *p_cap = &AH_PRIVATE(ah)->ah_caps;

        if (! p_cap->halAutoSleepSupport) {
            /* Set wake_on_interrupt bit; clear force_wake bit */
            OS_REG_WRITE(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_ON_INT);
        }
        else {
            /*
             * When chip goes into network sleep, it could be waken up by
             * MCI_INT interrupt caused by BT's HW messages (LNA_xxx, CONT_xxx)
             * which chould be in a very fast rate (~100us). This will cause
             * chip to leave and re-enter network sleep mode frequently, which
             * in consequence will have WLAN MCI HW to generate lots of
             * SYS_WAKING and SYS_SLEEPING messages which will make BT CPU
             * to busy to process.
             */
            if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
                OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_EN,
                        OS_REG_READ(ah, AR_MCI_INTERRUPT_RX_MSG_EN) &
                                    ~AR_MCI_INTERRUPT_RX_HW_MSG_MASK);
            }

            /* Clear the RTC force wake bit to allow the mac to go to sleep */
            OS_REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);

            if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
                /*
                 * In Jupiter, after enter sleep mode, hardware will send
                 * a SYS_SLEEPING message through MCI interface. Add a
                 * few us delay to make sure the message can reach BT side.
                 */
                OS_DELAY(30);
            }
        }
    }

#if ATH_WOW_OFFLOAD
    if (!AR_SREV_JUPITER(ah) || !HAL_WOW_CTRL(ah, HAL_WOW_OFFLOAD_SET_4004_BIT14))
#endif /* ATH_WOW_OFFLOAD */
    {
        /* Clear Bit 14 of AR_WA after putting chip into Sleep mode. */
        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_WA),
               ahp->ah_wa_reg_val & ~AR_WA_D3_TO_L1_DISABLE);
    }
}

/*
 * Set power mgt to the requested mode, and conditionally set
 * the chip as well
 */
HAL_BOOL
ar9300_set_power_mode(struct ath_hal *ah, HAL_POWER_MODE mode, int set_chip)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
#if defined(AH_DEBUG) || defined(AH_PRINT_FILTER)
    static const char* modes[] = {
        "AWAKE",
        "FULL-SLEEP",
        "NETWORK SLEEP",
        "UNDEFINED"
    };
#endif
    int status = AH_TRUE;

    HALDEBUG(ah, HAL_DEBUG_POWER_MGMT, "%s: %s -> %s (%s)\n", __func__,
        modes[ar9300_get_power_mode(ah)], modes[mode],
        set_chip ? "set chip " : "");
    OS_MARK(ah, AH_MARK_CHIP_POWER, mode);
    
    switch (mode) {
    case HAL_PM_AWAKE:
        if (set_chip)
            ah->ah_powerMode = mode;
        status = ar9300_set_power_mode_awake(ah, set_chip);
#if ATH_SUPPORT_MCI
        if (AH_PRIVATE(ah)->ah_caps.halMciSupport) {
            OS_REG_WRITE(ah, AR_RTC_KEEP_AWAKE, 0x2);
        }
#endif
        ahp->ah_chip_full_sleep = AH_FALSE;
        break;
    case HAL_PM_FULL_SLEEP:
#if ATH_SUPPORT_MCI
        if (AH_PRIVATE(ah)->ah_caps.halMciSupport) {
            if (ar9300_get_power_mode(ah) == HAL_PM_AWAKE) {
                if ((ar9300_mci_state(ah, HAL_MCI_STATE_ENABLE, NULL) != 0) &&
                    (ahp->ah_mci_bt_state != MCI_BT_SLEEP) &&
                    !ahp->ah_mci_halted_bt_gpm)
                {
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) %s: HALT BT GPM (full_sleep)\n", __func__);
                    ar9300_mci_send_coex_halt_bt_gpm(ah, AH_TRUE, AH_TRUE);
                }
            }
            ahp->ah_mci_ready = AH_FALSE;
        }
#endif
#if ATH_SUPPORT_MCI
        if (AH_PRIVATE(ah)->ah_caps.halMciSupport) {
            OS_REG_WRITE(ah, AR_RTC_KEEP_AWAKE, 0x2);
        }
#endif
        ar9300_set_power_mode_sleep(ah, set_chip);
        if (set_chip) {
            ahp->ah_chip_full_sleep = AH_TRUE;
            ah->ah_powerMode = mode;
        }
        break;
    case HAL_PM_NETWORK_SLEEP:
#if ATH_SUPPORT_MCI
        if (AH_PRIVATE(ah)->ah_caps.halMciSupport) {
            OS_REG_WRITE(ah, AR_RTC_KEEP_AWAKE, 0x2);
        }
#endif
        ar9300_set_power_mode_network_sleep(ah, set_chip);
        if (set_chip) {
            ah->ah_powerMode = mode;
        }
        break;
    default:
        HALDEBUG(ah, HAL_DEBUG_POWER_MGMT,
            "%s: unknown power mode %u\n", __func__, mode);
        OS_MARK(ah, AH_MARK_CHIP_POWER_DONE, -1);
        return AH_FALSE;
    }
    OS_MARK(ah, AH_MARK_CHIP_POWER_DONE, status);
    return status;
}

/*
 * Return the current sleep mode of the chip
 */
HAL_POWER_MODE
ar9300_get_power_mode(struct ath_hal *ah)
{
    int mode = OS_REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M;

    switch (mode) {
    case AR_RTC_STATUS_ON:
    case AR_RTC_STATUS_WAKEUP:
        return HAL_PM_AWAKE;
        break;
    case AR_RTC_STATUS_SLEEP:
        return HAL_PM_NETWORK_SLEEP;
        break;
    case AR_RTC_STATUS_SHUTDOWN:
        return HAL_PM_FULL_SLEEP;
        break;
    default:
        HALDEBUG(ah, HAL_DEBUG_POWER_MGMT,
            "%s: unknown power mode 0x%x\n", __func__, mode);
        return HAL_PM_UNDEFINED;
    }
}

/*
 * Set SM power save mode
 */
void
ar9300_set_sm_power_mode(struct ath_hal *ah, HAL_SMPS_MODE mode)
{
    int regval;
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (ar9300_get_capability(ah, HAL_CAP_DYNAMIC_SMPS, 0, AH_NULL) != HAL_OK) {
        return;
    }

    /* Program low & high power chainmask settings and enable MAC control */
    regval = SM(AR_PCU_SMPS_LPWR_CHNMSK_VAL, AR_PCU_SMPS_LPWR_CHNMSK) |
             SM(ahp->ah_rx_chainmask, AR_PCU_SMPS_HPWR_CHNMSK) |
             AR_PCU_SMPS_MAC_CHAINMASK;

    /* Program registers according to required SM power mode.*/
    switch (mode) {
    case HAL_SMPS_SW_CTRL_LOW_PWR:
        OS_REG_WRITE(ah, AR_PCU_SMPS, regval);
        break;
    case HAL_SMPS_SW_CTRL_HIGH_PWR:
        OS_REG_WRITE(ah, AR_PCU_SMPS, regval | AR_PCU_SMPS_SW_CTRL_HPWR);
        break;
    case HAL_SMPS_HW_CTRL:
        OS_REG_WRITE(ah, AR_PCU_SMPS, regval | AR_PCU_SMPS_HW_CTRL_EN);
        break;
    case HAL_SMPS_DEFAULT:
        OS_REG_WRITE(ah, AR_PCU_SMPS, 0);
        break;
    default:
        break;
    }
    ahp->ah_sm_power_mode = mode;
}

#if ATH_WOW
#if NOT_NEEDED_FOR_OSPREY /* not compiled for darwin */
/*
 * This routine is called to configure the SerDes register for the
 * Merlin 2.0 and above chip during WOW sleep.
 */
static void
ar9280_config_ser_des__wow_sleep(struct ath_hal *ah)
{
    int i;
    struct ath_hal_9300 *ahp = AH9300(ah);

    /*
     * For WOW sleep, we reprogram the SerDes so that the PLL and CHK REQ
     * are both enabled. This uses more power but the Maverick team reported
     * that otherwise, WOW sleep is unstable and chip may disappears.
     */
    for (i = 0; i < ahp->ah_ini_pcie_serdes_wow.ia_rows; i++) {
        OS_REG_WRITE(ah,
            INI_RA(&ahp->ah_ini_pcie_serdes_wow, i, 0),
            INI_RA(&ahp->ah_ini_pcie_serdes_wow, i, 1));
    }
    OS_DELAY(1000);
}
#endif /* if NOT_NEEDED_FOR_OSPREY */
static HAL_BOOL
ar9300_wow_create_keep_alive_pattern(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t  frame_len = 28;
    u_int32_t  tpc = 0x3f;
    u_int32_t  transmit_rate;
    u_int32_t  frame_type = 0x2;    /* Frame Type -> Data; */
    u_int32_t  sub_type = 0x4;      /* Subtype -> Null Data */
    u_int32_t  to_ds = 1;
    u_int32_t  duration_id = 0x3d;
    u_int8_t   *sta_mac_addr, *ap_mac_addr;
    u_int8_t   *addr1, *addr2, *addr3;
    u_int32_t  ctl[13] = { 0, };
#define NUM_KA_DATA_WORDS 6
    u_int32_t  data_word[NUM_KA_DATA_WORDS];
    u_int32_t  i;
    u_int32_t wow_ka_dataword0;

    sta_mac_addr = (u_int8_t *)ahp->ah_macaddr;
    ap_mac_addr = (u_int8_t *)ahp->ah_bssid;
    addr2 = sta_mac_addr;
    addr1 = addr3 = ap_mac_addr;

    if (AH_PRIVATE(ah)->ah_curchan->channel_flags & CHANNEL_CCK) {
        transmit_rate = 0x1B;    /* CCK_1M */
    } else {
        transmit_rate = 0xB;     /* OFDM_6M */
    }

    /* Set the Transmit Buffer. */
    ctl[0] = (frame_len | (tpc << 16));
    ctl[1] = 0;
    ctl[2] = (0x7 << 16);  /* tx_tries0 */
    ctl[3] = transmit_rate;
    ctl[4] = 0;
    ctl[7] = ahp->ah_tx_chainmask << 2;

    for (i = 0; i < 13; i++) {
        OS_REG_WRITE(ah, (AR_WOW_KA_DESC_WORD2 + i * 4), ctl[i]);
    }

    data_word[0] =
        (frame_type  <<  2) |
        (sub_type    <<  4) |
        (to_ds       <<  8) |
        (duration_id << 16);
    data_word[1] = (((u_int32_t)addr1[3] << 24) | ((u_int32_t)addr1[2] << 16) |
                  ((u_int32_t)addr1[1]) << 8 | ((u_int32_t)addr1[0]));
    data_word[2] = (((u_int32_t)addr2[1] << 24) | ((u_int32_t)addr2[0] << 16) |
                  ((u_int32_t)addr1[5]) << 8 | ((u_int32_t)addr1[4]));
    data_word[3] = (((u_int32_t)addr2[5] << 24) | ((u_int32_t)addr2[4] << 16) |
                  ((u_int32_t)addr2[3]) << 8 | ((u_int32_t)addr2[2]));
    data_word[4] = (((u_int32_t)addr3[3] << 24) | ((u_int32_t)addr3[2] << 16) |
                  ((u_int32_t)addr3[1]) << 8 | (u_int32_t)addr3[0]);
    data_word[5] = (((u_int32_t)addr3[5]) << 8 | ((u_int32_t)addr3[4]));

    if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
        /* Jupiter 2.0 has an extra descriptor word (Time based
         * discard) compared to other chips */
        OS_REG_WRITE(ah, (AR_WOW_KA_DESC_WORD2 + 12 * 4), 0);
        wow_ka_dataword0 = AR_WOW_TXBUF(13);
    }
    else {
        wow_ka_dataword0 = AR_WOW_TXBUF(12);
    }

    for (i = 0; i < NUM_KA_DATA_WORDS; i++) {
        OS_REG_WRITE(ah, (wow_ka_dataword0 + i * 4), data_word[i]);
    }

    return AH_TRUE;
}

/* TBD: Should querying hal for hardware capability */
#define MAX_PATTERN_SIZE      256
#define MAX_PATTERN_MASK_SIZE  32
#define MAX_NUM_USER_PATTERN    6 /* Deducting the disassoc/deauth packets */

void
ar9300_wow_apply_pattern(
    struct ath_hal *ah,
    u_int8_t *p_ath_pattern,
    u_int8_t *p_ath_mask,
    int32_t pattern_count,
    u_int32_t ath_pattern_len)
{
    int i;
    u_int32_t    reg_pat[] = {
                  AR_WOW_TB_PATTERN0,
                  AR_WOW_TB_PATTERN1,
                  AR_WOW_TB_PATTERN2,
                  AR_WOW_TB_PATTERN3,
                  AR_WOW_TB_PATTERN4,
                  AR_WOW_TB_PATTERN5,
                  AR_WOW_TB_PATTERN6,
                  AR_WOW_TB_PATTERN7
                 };
    u_int32_t    reg_mask[] = {
                  AR_WOW_TB_MASK0,
                  AR_WOW_TB_MASK1,
                  AR_WOW_TB_MASK2,
                  AR_WOW_TB_MASK3,
                  AR_WOW_TB_MASK4,
                  AR_WOW_TB_MASK5,
                  AR_WOW_TB_MASK6,
                  AR_WOW_TB_MASK7
                 };
    u_int32_t   pattern_val;
    u_int32_t   mask_val;
    u_int32_t   val;
    u_int8_t    mask_bit = 0x1;
    u_int8_t    pattern;

    /* TBD: should check count by querying the hardware capability */
    if (pattern_count >= MAX_NUM_USER_PATTERN) {
        return;
    }

    pattern = (u_int8_t)OS_REG_READ(ah, AR_WOW_PATTERN_REG);
    pattern = pattern | (mask_bit << pattern_count);
    OS_REG_WRITE(ah, AR_WOW_PATTERN_REG, pattern);

    /* Set the registers for pattern */
    for (i = 0; i < MAX_PATTERN_SIZE; i += 4) {
        pattern_val = (((u_int32_t)p_ath_pattern[i + 0]) |
                       ((u_int32_t)p_ath_pattern[i + 1] << 8) |
                       ((u_int32_t)p_ath_pattern[i + 2] << 16) |
                       ((u_int32_t)p_ath_pattern[i + 3] << 24));
        OS_REG_WRITE(ah, (reg_pat[pattern_count] + i), pattern_val);
    }

    /* Set the registers for mask */
    for (i = 0; i < MAX_PATTERN_MASK_SIZE; i += 4) {
        mask_val = (((u_int32_t)p_ath_mask[i + 0]) |
                    ((u_int32_t)p_ath_mask[i + 1] << 8) |
                    ((u_int32_t)p_ath_mask[i + 2] << 16) |
                    ((u_int32_t)p_ath_mask[i + 3] << 24));
        OS_REG_WRITE(ah, (reg_mask[pattern_count] + i), mask_val);
    }

    /* XXX */
    /* Set the pattern length to be matched */
    if (pattern_count < 4) {
        /* Pattern 0-3 uses AR_WOW_LENGTH1_REG register */
        val = OS_REG_READ(ah, AR_WOW_LENGTH1_REG);
        val = ((val & (~AR_WOW_LENGTH1_MASK(pattern_count))) |
               ((ath_pattern_len & AR_WOW_LENGTH_MAX) <<
                AR_WOW_LENGTH1_SHIFT(pattern_count)));
        OS_REG_WRITE(ah, AR_WOW_LENGTH1_REG, val);
    } else {
        /* Pattern 4-7 uses AR_WOW_LENGTH2_REG register */
        val = OS_REG_READ(ah, AR_WOW_LENGTH2_REG);
        val = ((val & (~AR_WOW_LENGTH2_MASK(pattern_count))) |
               ((ath_pattern_len & AR_WOW_LENGTH_MAX) <<
                AR_WOW_LENGTH2_SHIFT(pattern_count)));
        OS_REG_WRITE(ah, AR_WOW_LENGTH2_REG, val);
    }

    AH_PRIVATE(ah)->ah_wow_event_mask |=
        (1 << (pattern_count + AR_WOW_PATTERN_FOUND_SHIFT));

    return;
}

HAL_BOOL
ar9300_set_power_mode_wow_sleep(struct ath_hal *ah)
{
    OS_REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);

    OS_REG_WRITE(ah, AR_CR, AR_CR_RXD);    /* Set receive disable bit */
    if (!ath_hal_waitfor(ah, AR_CR, AR_CR_RXE, 0, AH_WAIT_TIMEOUT)) {
        HALDEBUG(ah, HAL_DEBUG_POWER_MGMT, "%s: dma failed to stop in 10ms\n"
                 "AR_CR=0x%08x\nAR_DIAG_SW=0x%08x\n", __func__,
                 OS_REG_READ(ah, AR_CR), OS_REG_READ(ah, AR_DIAG_SW));
        return AH_FALSE;
    } else {
#if 0
        OS_REG_WRITE(ah, AR_RXDP, 0x0);
#endif

        HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
            "%s: TODO How to disable RXDP!!\n", __func__);

#if ATH_SUPPORT_MCI
        if (AH_PRIVATE(ah)->ah_caps.halMciSupport) {
            OS_REG_WRITE(ah, AR_RTC_KEEP_AWAKE, 0x2);
        }
#endif
        OS_REG_WRITE(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_ON_INT);

        return AH_TRUE;
    }
}


HAL_BOOL
ar9300_wow_enable(
    struct ath_hal *ah,
    u_int32_t pattern_enable,
    u_int32_t timeout_in_seconds,
    int clearbssid,
    HAL_BOOL offloadEnable)
{
    uint32_t init_val, val, rval = 0;
    const int ka_delay = 4; /* Delay of 4 millisec between two keep_alive's */
    uint32_t wow_event_mask;
#if ATH_WOW_OFFLOAD
    uint32_t wow_feature_enable =
            //AR_WOW_OFFLOAD_ENA_GTK            |
            //AR_WOW_OFFLOAD_ENA_ARP_OFFLOAD    |
            //AR_WOW_OFFLOAD_ENA_NS_OFFLOAD     |
            //AR_WOW_OFFLOAD_ENA_ACER_MAGIC     |
            //AR_WOW_OFFLOAD_ENA_STD_MAGIC      |
            //AR_WOW_OFFLOAD_ENA_4WAY_WAKE      |
            //AR_WOW_OFFLOAD_ENA_SWKA           |
            //AR_WOW_OFFLOAD_ENA_BT_SLEEP       |
            AR_WOW_OFFLOAD_ENA_SW_NULL;
#endif

    /*
     * ah_wow_event_mask is a mask to the AR_WOW_PATTERN_REG register to
     * indicate which WOW events that we have enabled. The WOW Events are
     * from the pattern_enable in this function and pattern_count of
     * ar9300_wow_apply_pattern()
     */
    wow_event_mask = AH_PRIVATE(ah)->ah_wow_event_mask;

    HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
        "%s: offload: %d, pattern: %08x, event_mask: %08x\n",
        __func__, offloadEnable, pattern_enable, wow_event_mask);

    /*
     * Untie Power-On-Reset from the PCI-E Reset. When we are in WOW sleep,
     * we do not want the Reset from the PCI-E to disturb our hw state.
     */
    if (AH_PRIVATE(ah)->ah_is_pci_express == AH_TRUE) {
        
        u_int32_t wa_reg_val;
        /*
         * We need to untie the internal POR (power-on-reset) to the external
         * PCI-E reset. We also need to tie the PCI-E Phy reset to the PCI-E
         * reset.
         */
        HAL_DEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
            "%s: Untie POR and PCIE reset\n", __func__);
        wa_reg_val = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_WA));
        wa_reg_val = wa_reg_val & ~(AR_WA_UNTIE_RESET_EN);
        wa_reg_val = wa_reg_val | AR_WA_RESET_EN | AR_WA_POR_SHORT;
        /*
         * This bit is to bypass the EEPROM/OTP state machine, (by clearing its
         * busy state while PCIE_rst is asserted), to allow BT embedded CPU
         * be able to access WLAN registers. Otherwise the eCPU access will be
         * stalled as eeprom_sm is held in busy state.
         *
         * EV91928 is that when this bit is set, after host wakeup and PCIE_rst
         * deasserted, PCIE configuration registers will be reset and DeviceID
         * SubsystemID etc. registers will be different from values before
         * entering sleep. This will cause Windows to detect a device removal.
         *
         * For HW WOW, this bit should keep as cleared.
         */
        if (offloadEnable) {
            HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
                "%s: Set AR_WA.13 COLD_RESET_OVERRIDE\n", __func__);
            wa_reg_val = wa_reg_val | AR_WA_COLD_RESET_OVERRIDE;

#if ATH_WOW_OFFLOAD
            if (AR_SREV_JUPITER(ah)) {
                wa_reg_val = wa_reg_val | AR_WA_D3_TO_L1_DISABLE;
            }
#endif
        }
        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_WA), wa_reg_val);
    }

    /*
     * Set the power states appropriately and enable pme.
     */
    val = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL));
    val |=
        AR_PMCTRL_HOST_PME_EN     | 
        AR_PMCTRL_PWR_PM_CTRL_ENA |
        AR_PMCTRL_AUX_PWR_DET;

    /*
     * Set and clear WOW_PME_CLEAR registers for the chip to generate next
     * wow signal.
     */
    val |= AR_PMCTRL_WOW_PME_CLR;
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL), val);
    val &= ~AR_PMCTRL_WOW_PME_CLR;
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL), val);

    /*
     * Setup for for:
     *     - beacon misses
     *     - magic pattern
     *     - keep alive timeout
     *     - pattern matching
     */

    /*
     * Program some default values for keep-alives, beacon misses, etc.
     */
    init_val = OS_REG_READ(ah, AR_WOW_PATTERN_REG);
    val = AR_WOW_BACK_OFF_SHIFT(AR_WOW_PAT_BACKOFF) | init_val;
    OS_REG_WRITE(ah, AR_WOW_PATTERN_REG, val);
    rval = OS_REG_READ(ah, AR_WOW_PATTERN_REG);

    val =
        AR_WOW_AIFS_CNT(AR_WOW_CNT_AIFS_CNT) |
        AR_WOW_SLOT_CNT(AR_WOW_CNT_SLOT_CNT) |
        AR_WOW_KEEP_ALIVE_CNT(AR_WOW_CNT_KA_CNT);
    OS_REG_WRITE(ah, AR_WOW_COUNT_REG, val);
    rval = OS_REG_READ(ah, AR_WOW_COUNT_REG);

    if (pattern_enable & AH_WOW_BEACON_MISS) {
        val = AR_WOW_BEACON_TIMO;
    } else {
        /* We are not using the beacon miss. Program a large value. */
        val = AR_WOW_BEACON_TIMO_MAX;
    }
    OS_REG_WRITE(ah, AR_WOW_BCN_TIMO_REG, val);
    rval = OS_REG_READ(ah, AR_WOW_BCN_TIMO_REG);

    /*
     * Keep Alive Timo in ms.
     */
    if (pattern_enable == 0) {
        val =  AR_WOW_KEEP_ALIVE_NEVER;
    } else {
        val =  AH_PRIVATE(ah)->ah_config.ath_hal_keep_alive_timeout * 32;
    }
    OS_REG_WRITE(ah, AR_WOW_KEEP_ALIVE_TIMO_REG, val);
    rval = OS_REG_READ(ah, AR_WOW_KEEP_ALIVE_TIMO_REG);

    /*
     * Keep Alive delay in us.
     */
    val = ka_delay * 1000;
    OS_REG_WRITE(ah, AR_WOW_KEEP_ALIVE_DELAY_REG, val);
    rval = OS_REG_READ(ah, AR_WOW_KEEP_ALIVE_DELAY_REG);

    /*
     * Create keep_alive Pattern to respond to beacons.
     */
    ar9300_wow_create_keep_alive_pattern(ah);

    /*
     * Configure Mac Wow Registers.
     */

    val = OS_REG_READ(ah, AR_WOW_KEEP_ALIVE_REG);    

    /*
     * Send keep alive timeouts anyway.
     */
    val &= ~AR_WOW_KEEP_ALIVE_AUTO_DIS;

    if (pattern_enable & AH_WOW_LINK_CHANGE) {
        val &= ~ AR_WOW_KEEP_ALIVE_FAIL_DIS;
        wow_event_mask |= AR_WOW_KEEP_ALIVE_FAIL;
    } else {
        val |=  AR_WOW_KEEP_ALIVE_FAIL_DIS;
    }
#if ATH_WOW_OFFLOAD
    if (offloadEnable) {
        /* Don't enable KA frames yet. BT CPU is not 
         * yet ready. */
    }
    else 
#endif /* ATH_WOW_OFFLOAD */
    {
        OS_REG_WRITE(ah, AR_WOW_KEEP_ALIVE_REG, val);
        val = OS_REG_READ(ah, AR_WOW_KEEP_ALIVE_REG);
    }


    /*
     * We are relying on a bmiss failure. Ensure we have enough
     * threshold to prevent AH_FALSE positives.
     */
    OS_REG_RMW_FIELD(ah, AR_RSSI_THR, AR_RSSI_THR_BM_THR,
        AR_WOW_BMISSTHRESHOLD);

    val = OS_REG_READ(ah, AR_WOW_BCN_EN_REG);
    if (pattern_enable & AH_WOW_BEACON_MISS) {
        val |= AR_WOW_BEACON_FAIL_EN;
        wow_event_mask |= AR_WOW_BEACON_FAIL;
    } else {
        val &= ~AR_WOW_BEACON_FAIL_EN;
    }
    OS_REG_WRITE(ah, AR_WOW_BCN_EN_REG, val);
    val = OS_REG_READ(ah, AR_WOW_BCN_EN_REG);

    /*
     * Enable the magic packet registers.
     */
    val = OS_REG_READ(ah, AR_WOW_PATTERN_REG);
    if ((pattern_enable & AH_WOW_MAGIC_PATTERN_EN)
#if ATH_WOW_OFFLOAD
        || (pattern_enable & AH_WOW_ACER_MAGIC_EN)
#endif
        )
    {
        val |= AR_WOW_MAGIC_EN;
        wow_event_mask |= AR_WOW_MAGIC_PAT_FOUND;
    } else {
        val &= ~AR_WOW_MAGIC_EN;
    }
    val |= AR_WOW_MAC_INTR_EN;
    OS_REG_WRITE(ah, AR_WOW_PATTERN_REG, val);
    val = OS_REG_READ(ah, AR_WOW_PATTERN_REG);

#if ATH_WOW_OFFLOAD
    if (HAL_WOW_CTRL(ah, HAL_WOW_OFFLOAD_FORCE_BT_SLEEP)) {
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_BT_SLEEP;
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - BT SLEEP\n");
    } else {
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_BT_SLEEP;
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - BT SLEEP\n");
    }
        
    if (HAL_WOW_CTRL(ah, HAL_WOW_OFFLOAD_SW_NULL_DISABLE)) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - SW NULL\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_SW_NULL;
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - SW NULL\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_SW_NULL;
    }

    if (HAL_WOW_CTRL(ah, HAL_WOW_OFFLOAD_DEVID_SWAR_DISABLE)) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - DevID SWAR\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_DEVID_SWAR;
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - DevID SWAR\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_DEVID_SWAR;
    }

    if (pattern_enable & AH_WOW_ACER_KEEP_ALIVE_EN) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - Acer SWKA\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_SWKA;
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - Acer SWKA\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_SWKA;
    }

    if (pattern_enable & AH_WOW_ACER_MAGIC_EN) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - Standard Magic\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_STD_MAGIC;
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - Acer Magic\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_ACER_MAGIC;
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - Standard Magic\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_STD_MAGIC;
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - Acer Magic\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_ACER_MAGIC;
    }

    if ((pattern_enable & AH_WOW_4WAY_HANDSHAKE_EN) ||
        HAL_WOW_CTRL(ah, HAL_WOW_OFFLOAD_FORCE_4WAY_HS_WAKE)) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - 4Way Handshake\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_4WAY_WAKE;
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - 4Way Handshake\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_4WAY_WAKE;
    }

    if((pattern_enable & AH_WOW_AP_ASSOCIATION_LOST_EN) ||
        HAL_WOW_CTRL(ah, HAL_WOW_OFFLOAD_FORCE_AP_LOSS_WAKE))
    {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - AP loss wake\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_AP_LOSS_WAKE;
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - AP loss wake\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_AP_LOSS_WAKE;
    }

    if((pattern_enable & AH_WOW_GTK_HANDSHAKE_ERROR_EN) ||
        HAL_WOW_CTRL(ah, HAL_WOW_OFFLOAD_FORCE_GTK_ERR_WAKE))
    {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - GTK error wake\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_GTK_ERROR_WAKE;
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - GTK error wake\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_GTK_ERROR_WAKE;
    }

    if (pattern_enable & AH_WOW_GTK_OFFLOAD_EN) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - GTK offload\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_GTK;
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - GTK offload\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_GTK;
    }

    if (pattern_enable & AH_WOW_ARP_OFFLOAD_EN) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - ARP offload\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_ARP_OFFLOAD;
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - ARP offload\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_ARP_OFFLOAD;
    }

    if (pattern_enable & AH_WOW_NS_OFFLOAD_EN) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) ENA - NS offload\n");
        wow_feature_enable |= AR_WOW_OFFLOAD_ENA_NS_OFFLOAD;
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) DIS - NS offload\n");
        wow_feature_enable &= ~AR_WOW_OFFLOAD_ENA_NS_OFFLOAD;
    }

#endif /* ATH_WOW_OFFLOAD */

    /* For Kite and later version of the chips
     * enable wow pattern match for packets less than
     * 256 bytes for all patterns.
     */
    /* XXX */
    OS_REG_WRITE(
        ah, AR_WOW_PATTERN_MATCH_LT_256B_REG, AR_WOW_PATTERN_SUPPORTED);

    /*
     * Set the power states appropriately and enable PME.
     */
    val = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL));
    val |=
        AR_PMCTRL_PWR_STATE_D1D3 |
        AR_PMCTRL_HOST_PME_EN    |
        AR_PMCTRL_PWR_PM_CTRL_ENA;
    val &= ~AR_PCIE_PM_CTRL_ENA;
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL), val);

    /* Wake on Timer Interrupt. Test mode only. Used in Manufacturing line. */
    if (timeout_in_seconds) {
        /* convert Timeout to u_secs */
        OS_REG_WRITE(ah, AR_NEXT_NDP_TIMER,
            OS_REG_READ(ah, AR_TSF_L32) + timeout_in_seconds * 1000000 );
        /* timer_period = 30 seconds always */
        OS_REG_WRITE(ah, AR_NDP_PERIOD, 30 * 1000000);
        OS_REG_WRITE(ah, AR_TIMER_MODE, OS_REG_READ(ah, AR_TIMER_MODE) | 0x80);
        OS_REG_WRITE(ah, AR_IMR_S5, OS_REG_READ(ah, AR_IMR_S5) | 0x80);
        OS_REG_WRITE(ah, AR_IMR, OS_REG_READ(ah, AR_IMR) | AR_IMR_GENTMR);
        if (clearbssid) {
            OS_REG_WRITE(ah, AR_BSS_ID0, 0);
            OS_REG_WRITE(ah, AR_BSS_ID1, 0);
        }
    }

    /* Enable Seq# generation when asleep. */
    OS_REG_WRITE(ah, AR_STA_ID1, 
                     OS_REG_READ(ah, AR_STA_ID1) & ~AR_STA_ID1_PRESERVE_SEQNUM);

    AH_PRIVATE(ah)->ah_wow_event_mask = wow_event_mask;

#if ATH_WOW_OFFLOAD
    if (offloadEnable) {
        /* Force MAC awake before entering SW WoW mode */
        OS_REG_SET_BIT(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);
#if ATH_SUPPORT_MCI
        if (AH_PRIVATE(ah)->ah_caps.halMciSupport) {
            OS_REG_WRITE(ah, AR_RTC_KEEP_AWAKE, 0x2);
        }
#endif

        OS_REG_WRITE(ah, AR_WOW_OFFLOAD_COMMAND_JUPITER, wow_feature_enable);
        OS_REG_WRITE(ah, AR_WOW_OFFLOAD_STATUS_JUPITER, 0x0);
        if (wow_feature_enable & AR_WOW_OFFLOAD_ENA_SW_NULL) {
            OS_REG_WRITE(ah, AR_WOW_SW_NULL_PARAMETER,
                ((1000) |
                (4 << AR_WOW_SW_NULL_SHORT_PERIOD_MASK_S)));
        }

        if (wow_feature_enable & AR_WOW_OFFLOAD_ENA_DEVID_SWAR) {
            ar9300_wowoffload_download_devid_swar(ah);
        }

        ar9300_wow_offload_download_hal_params(ah);
        ar9300_wow_offload_handshake(ah, pattern_enable);
        AH9300(ah)->ah_chip_full_sleep = AH_FALSE;

        //OS_REG_SET_BIT(ah, AR_SW_WOW_CONTROL, AR_HW_WOW_DISABLE);
    }
    else 
#endif /* ATH_WOW_OFFLOAD */
    {
#if ATH_SUPPORT_MCI
        if (AH_PRIVATE(ah)->ah_caps.halMciSupport) {
            OS_REG_WRITE(ah, AR_RTC_KEEP_AWAKE, 0x2);
        }
#endif
        ar9300_set_power_mode_wow_sleep(ah);    
        AH9300(ah)->ah_chip_full_sleep = AH_TRUE;
    }

    return (AH_TRUE);
}

u_int32_t
//ar9300_wow_wake_up(struct ath_hal *ah, u_int8_t  *chipPatternBytes)
ar9300_wow_wake_up(struct ath_hal *ah, HAL_BOOL offloadEnabled)
{
    uint32_t wow_status = 0;
    uint32_t val = 0, rval;

    OS_REG_CLR_BIT(ah, AR_SW_WOW_CONTROL, AR_HW_WOW_DISABLE);
    OS_REG_CLR_BIT(ah, AR_SW_WOW_CONTROL, AR_SW_WOW_ENABLE);

#if ATH_WOW_OFFLOAD
    /* If WoW was offloaded to embedded CPU, use the global 
     * shared register to know the wakeup reason */
    if (offloadEnabled) {
        val = OS_REG_READ(ah, AR_EMB_CPU_WOW_STATUS);
        if (val) {
            if (val & AR_EMB_CPU_WOW_STATUS_MAGIC_PATTERN) {
                HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) SW MAGIC_PATTERN\n");
                wow_status |= AH_WOW_MAGIC_PATTERN_EN;
            }
            if (val & AR_EMB_CPU_WOW_STATUS_PATTERN_MATCH) {
                HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) SW USER_PATTERN\n");
                wow_status |= AH_WOW_USER_PATTERN_EN;
            }
            if (val & AR_EMB_CPU_WOW_STATUS_KEEP_ALIVE_FAIL) {
                HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) SW KEEP_ALIVE_FAIL\n");
                wow_status |= AH_WOW_LINK_CHANGE;
            }
            if (val & AR_EMB_CPU_WOW_STATUS_BEACON_MISS) {
                HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) SW BEACON_FAIL\n");
                wow_status |= AH_WOW_BEACON_MISS;
            }
        }

        /* Clear status and mask registers */
        OS_REG_WRITE(ah, AR_EMB_CPU_WOW_STATUS, 0x0);
        OS_REG_WRITE(ah, AR_EMB_CPU_WOW_ENABLE, 0);
        OS_REG_WRITE(ah, AR_MBOX_CTRL_STATUS, 0);

    }
    else 
#endif /* ATH_WOW_OFFLOAD */
    {
        /*
         * Read the WOW Status register to know the wakeup reason.
         */
        rval = OS_REG_READ(ah, AR_WOW_PATTERN_REG);
        val = AR_WOW_STATUS(rval);

        /*
         * Mask only the WOW events that we have enabled.
         * Sometimes we have spurious WOW events from the AR_WOW_PATTERN_REG
         * register. This mask will clean it up.
         */
        val &= AH_PRIVATE(ah)->ah_wow_event_mask;

        if (val) {
            if (val & AR_WOW_MAGIC_PAT_FOUND) {
                HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) HW MAGIC_PATTERN\n");
                wow_status |= AH_WOW_MAGIC_PATTERN_EN;
            }
            if (AR_WOW_PATTERN_FOUND(val)) {
                //int  i, offset; 
                //offset = OS_REG_READ(ah, AR_WOW_RXBUF_START_ADDR);
                //// Read matched pattern for wake packet detection indication.            
                //for( i = 0; i< MAX_PATTERN_SIZE/4; i+=4)
                //{
                //    // RX FIFO is only 8K wrapping.
                //    if(offset >= 8 * 1024 / 4) offset = 0;
                //    *(u_int32_t*)(chipPatternBytes + i) = OS_REG_READ( ah,offset );
                //    offset++;
                //}
                wow_status |= AH_WOW_USER_PATTERN_EN;
                HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) HW USER_PATTERN\n");
            }
            if (val & AR_WOW_KEEP_ALIVE_FAIL) {
                HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) HW KEEP_ALIVE_FAIL\n");
                wow_status |= AH_WOW_LINK_CHANGE;
            }
            if (val & AR_WOW_BEACON_FAIL) {
                HALDEBUG(ah, HAL_DEBUG_UNMASKABLE, "(WOW) HW BEACON_FAIL\n");
                wow_status |= AH_WOW_BEACON_MISS;
            }
        }
    }

    /*
     * Set and clear WOW_PME_CLEAR registers for the chip to generate next
     * wow signal.
     * Disable D3 before accessing other registers ?
     */
    val = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL));
    /* Check the bit value 0x01000000 (7-10)? */
    val &= ~AR_PMCTRL_PWR_STATE_D1D3;
    val |= AR_PMCTRL_WOW_PME_CLR;
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL), val);

    /*
     * Clear all events.
     */
    OS_REG_WRITE(ah, AR_WOW_PATTERN_REG,
        AR_WOW_CLEAR_EVENTS(OS_REG_READ(ah, AR_WOW_PATTERN_REG)));

    //HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
    //    "%s: Skip PCIE WA programming\n", __func__);
#if 0
    /*
     * Tie reset register.
     * FIXME: Per David Quan not tieing it back might have some repurcussions.
     */
    /* XXX */
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_WA), OS_REG_READ(ah, AR_WA) |
            AR_WA_UNTIE_RESET_EN | AR_WA_POR_SHORT | AR_WA_RESET_EN);
#endif

    /* Restore the Beacon Threshold to init value */
    OS_REG_RMW_FIELD(ah, AR_RSSI_THR, AR_RSSI_THR_BM_THR, INIT_RSSI_THR);

    /*
     * Restore the way the PCI-E Reset, Power-On-Reset, external PCIE_POR_SHORT
     * pins are tied to its original value. Previously just before WOW sleep,
     * we untie the PCI-E Reset to our Chip's Power On Reset so that
     * any PCI-E reset from the bus will not reset our chip.
     */
    HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE, "%s: restore AR_WA\n", __func__);
    if (AH_PRIVATE(ah)->ah_is_pci_express == AH_TRUE) {
        ar9300_config_pci_power_save(ah, 0, 0);
    }

    AH_PRIVATE(ah)->ah_wow_event_mask = 0;
    HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
        "(WOW) wow_status=%08x\n", wow_status);

    return (wow_status);
}

void
ar9300_wow_set_gpio_reset_low(struct ath_hal *ah)
{
    uint32_t val;

    val = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_OE_OUT));
    val |= (1 << (2 * 2));
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_GPIO_OE_OUT), val);
    val = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_GPIO_OE_OUT));
    /* val = OS_REG_READ(ah,AR_GPIO_IN_OUT ); */
}
#endif /* ATH_WOW */
