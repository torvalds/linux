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
#include "ar9300/ar9300phy.h"

#if ATH_SUPPORT_MCI

#define AH_MCI_REMOTE_RESET_INTERVAL_US     500
#define AH_MCI_DEBUG_PRINT_SCHED    0

static void ar9300_mci_print_msg(struct ath_hal *ah, HAL_BOOL send,u_int8_t hdr,
                                 int len, u_int32_t *pl)
{
#if 0
    char s[128];
    char *p = s;
    int i;
    u_int8_t *p_data = (u_int8_t *) pl;
    
    if (send) {
        p += snprintf(s, 60,
                      "(MCI) >>>>> Hdr: %02X, Len: %d, Payload:", hdr, len);
    }
    else {
        p += snprintf(s, 60,
                      "(MCI) <<<<< Hdr: %02X, Len: %d, Payload:", hdr, len);
    }
    for ( i=0; i<len; i++)
    {
        p += snprintf(p, 60, " %02x", *(p_data + i));
    }
    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s\n", s);
/*
    for ( i=0; i<(len + 3)/4; i++)
    {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI)   0x%08x\n", *(pl + i));
    }
*/
#endif
}

static
void ar9300_mci_osla_setup(struct ath_hal *ah, HAL_BOOL enable)
{
//    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t thresh;

    if (enable) {
        OS_REG_RMW_FIELD(ah, AR_MCI_SCHD_TABLE_2, AR_MCI_SCHD_TABLE_2_HW_BASED, 1);
        OS_REG_RMW_FIELD(ah, AR_MCI_SCHD_TABLE_2, AR_MCI_SCHD_TABLE_2_MEM_BASED, 1);

        if (!(ah->ah_config.ath_hal_mci_config &
            ATH_MCI_CONFIG_DISABLE_AGGR_THRESH))
        {

            if (AR_SREV_APHRODITE(ah))
                OS_REG_RMW_FIELD(ah, AR_MCI_MISC, AR_MCI_MISC_HW_FIX_EN, 1);

            thresh = MS(ah->ah_config.ath_hal_mci_config,
                        ATH_MCI_CONFIG_AGGR_THRESH);
            OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL,
                             AR_BTCOEX_CTRL_AGGR_THRESH, thresh);
            OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL,
                             AR_BTCOEX_CTRL_TIME_TO_NEXT_BT_THRESH_EN, 1);
            HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                "(MCI) SCHED aggr thresh: on, thresh=%d (%d.%d%%)\n",
                thresh, (thresh + 1)*125/10, (thresh + 1)*125%10);

        }
        else {
            OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL,
                             AR_BTCOEX_CTRL_TIME_TO_NEXT_BT_THRESH_EN, 0);
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) SCHED aggr thresh: off\n");
        }
        OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL,
                         AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN, 1);
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) SCHED one step look ahead: on\n");
    }
    else {
        OS_REG_CLR_BIT(ah, AR_BTCOEX_CTRL, 
            AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN);
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) SCHED one step look ahead: off\n");
    }
}

static void ar9300_mci_reset_req_wakeup(struct ath_hal *ah)
{
    /* to be tested in emulation */
    if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
        OS_REG_RMW_FIELD(ah, AR_MCI_COMMAND2,
            AR_MCI_COMMAND2_RESET_REQ_WAKEUP, 1);
        OS_DELAY(1);
        OS_REG_RMW_FIELD(ah, AR_MCI_COMMAND2,
            AR_MCI_COMMAND2_RESET_REQ_WAKEUP, 0);
    }
}

static int32_t ar9300_mci_wait_for_interrupt(struct ath_hal *ah,
                                             u_int32_t address, 
                                             u_int32_t bit_position,
                                             int32_t time_out)
{
    int data; //, loop;

    while (time_out) {
        data = OS_REG_READ(ah, address);

        if (data & bit_position) {
            OS_REG_WRITE(ah, address, bit_position);
            if (address == AR_MCI_INTERRUPT_RX_MSG_RAW) {
                if (bit_position & AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE) {
                    ar9300_mci_reset_req_wakeup(ah);
                }
                if (bit_position & (AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING |
                                    AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING))
                {
                    OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RAW,
                        AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE);
                }
                OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RAW, AR_MCI_INTERRUPT_RX_MSG);
            }
            break;
        }

        OS_DELAY(10);
        time_out -= 10;
        if (time_out < 0) {
            break;
        }
    }

    if (time_out <= 0) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
            "(MCI) %s: Wait for Reg0x%08x = 0x%08x timeout.\n",
            __func__, address, bit_position);
        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
            "(MCI) INT_RAW = 0x%08x, RX_MSG_RAW = 0x%08x\n",
            OS_REG_READ(ah, AR_MCI_INTERRUPT_RAW),
            OS_REG_READ(ah, AR_MCI_INTERRUPT_RX_MSG_RAW));
        time_out = 0;
    }
    return time_out;
}

void ar9300_mci_remote_reset(struct ath_hal *ah, HAL_BOOL wait_done)
{
    u_int32_t payload[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffff00};

    ar9300_mci_send_message(ah, MCI_REMOTE_RESET, 0, payload, 16, 
        wait_done, AH_FALSE);

    OS_DELAY(5);
}

void ar9300_mci_send_lna_transfer(struct ath_hal *ah, HAL_BOOL wait_done)
{
    u_int32_t payload = 0x00000000;

    ar9300_mci_send_message(ah, MCI_LNA_TRANS, 0, &payload, 1, 
        wait_done, AH_FALSE);
}

static void ar9300_mci_send_req_wake(struct ath_hal *ah, HAL_BOOL wait_done)
{
    ar9300_mci_send_message(ah, MCI_REQ_WAKE, 
        HAL_MCI_FLAG_DISABLE_TIMESTAMP, AH_NULL, 0, wait_done, AH_FALSE);

    OS_DELAY(5);
}

void ar9300_mci_send_sys_waking(struct ath_hal *ah, HAL_BOOL wait_done)
{
    ar9300_mci_send_message(ah, MCI_SYS_WAKING, 
        HAL_MCI_FLAG_DISABLE_TIMESTAMP, AH_NULL, 0, wait_done, AH_FALSE);
}

static void ar9300_mci_send_lna_take(struct ath_hal *ah, HAL_BOOL wait_done)
{
    u_int32_t payload = 0x70000000;

    /* LNA gain index is set to 7. */
    ar9300_mci_send_message(ah, MCI_LNA_TAKE, 
        HAL_MCI_FLAG_DISABLE_TIMESTAMP, &payload, 1, wait_done, AH_FALSE);
}

static void ar9300_mci_send_sys_sleeping(struct ath_hal *ah, HAL_BOOL wait_done)
{
    ar9300_mci_send_message(ah, MCI_SYS_SLEEPING, 
        HAL_MCI_FLAG_DISABLE_TIMESTAMP, AH_NULL, 0, wait_done, AH_FALSE);
}

static void
ar9300_mci_send_coex_version_query(struct ath_hal *ah, HAL_BOOL wait_done)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t payload[4] = {0, 0, 0, 0};

    if ((ahp->ah_mci_coex_bt_version_known == AH_FALSE) &&
        (ahp->ah_mci_bt_state != MCI_BT_SLEEP)) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) Send Coex version query.\n");
        MCI_GPM_SET_TYPE_OPCODE(payload,
            MCI_GPM_COEX_AGENT, MCI_GPM_COEX_VERSION_QUERY);
        ar9300_mci_send_message(ah, MCI_GPM, 0, payload, 16, wait_done, AH_TRUE);
    }
}

static void
ar9300_mci_send_coex_version_response(struct ath_hal *ah, HAL_BOOL wait_done)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t payload[4] = {0, 0, 0, 0};

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) Send Coex version response.\n");
    MCI_GPM_SET_TYPE_OPCODE(payload,
        MCI_GPM_COEX_AGENT, MCI_GPM_COEX_VERSION_RESPONSE);
    *(((u_int8_t *)payload) + MCI_GPM_COEX_B_MAJOR_VERSION) =
        ahp->ah_mci_coex_major_version_wlan;
    *(((u_int8_t *)payload) + MCI_GPM_COEX_B_MINOR_VERSION) =
        ahp->ah_mci_coex_minor_version_wlan;
    ar9300_mci_send_message(ah, MCI_GPM, 0, payload, 16, wait_done, AH_TRUE);
}

static void
ar9300_mci_send_coex_wlan_channels(struct ath_hal *ah, HAL_BOOL wait_done)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t *payload = &ahp->ah_mci_coex_wlan_channels[0];

    if ((ahp->ah_mci_coex_wlan_channels_update == AH_TRUE) &&
        (ahp->ah_mci_bt_state != MCI_BT_SLEEP))
    {
        MCI_GPM_SET_TYPE_OPCODE(payload,
            MCI_GPM_COEX_AGENT, MCI_GPM_COEX_WLAN_CHANNELS);
        ar9300_mci_send_message(ah, MCI_GPM, 0, payload, 16, wait_done, AH_TRUE);
        MCI_GPM_SET_TYPE_OPCODE(payload, 0xff, 0xff);
    }
}

static void ar9300_mci_send_coex_bt_status_query(struct ath_hal *ah,
                                    HAL_BOOL wait_done, u_int8_t query_type)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t pld[4] = {0, 0, 0, 0};
    HAL_BOOL query_btinfo = query_type &
            (MCI_GPM_COEX_QUERY_BT_ALL_INFO | MCI_GPM_COEX_QUERY_BT_TOPOLOGY);

    if (ahp->ah_mci_bt_state != MCI_BT_SLEEP) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
            "(MCI) Send Coex BT Status Query 0x%02X\n", query_type);
        MCI_GPM_SET_TYPE_OPCODE(pld,
            MCI_GPM_COEX_AGENT, MCI_GPM_COEX_STATUS_QUERY);
        *(((u_int8_t *)pld) + MCI_GPM_COEX_B_BT_BITMAP) = query_type;
        /*
         * If bt_status_query message is thought not sent successfully,
         * then ah_mci_need_flush_btinfo should be set again.
         */
        if (!ar9300_mci_send_message(ah, MCI_GPM, 0, pld, 16, wait_done, AH_TRUE))
        {
            if (query_btinfo) {
                ahp->ah_mci_need_flush_btinfo = AH_TRUE;
                HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                    "(MCI) send bt_status_query fail, set flush flag again\n");
            }
        }
        if (query_btinfo) {
            ahp->ah_mci_query_bt = AH_FALSE;
        }
    }
}

void ar9300_mci_send_coex_halt_bt_gpm(struct ath_hal *ah,
                                      HAL_BOOL halt, HAL_BOOL wait_done)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t payload[4] = {0, 0, 0, 0};

    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
        "(MCI) Send Coex %s BT GPM.\n", (halt == AH_TRUE)?"HALT":"UNHALT");

    MCI_GPM_SET_TYPE_OPCODE(payload,
        MCI_GPM_COEX_AGENT, MCI_GPM_COEX_HALT_BT_GPM);
    if (halt == AH_TRUE) {
        ahp->ah_mci_query_bt = AH_TRUE;
        /* Send next UNHALT no matter HALT sent or not */
        ahp->ah_mci_unhalt_bt_gpm = AH_TRUE;
        ahp->ah_mci_need_flush_btinfo = AH_TRUE;
        *(((u_int8_t *)payload) + MCI_GPM_COEX_B_HALT_STATE) =
            MCI_GPM_COEX_BT_GPM_HALT;
    }
    else {
        *(((u_int8_t *)payload) + MCI_GPM_COEX_B_HALT_STATE) =
            MCI_GPM_COEX_BT_GPM_UNHALT;
    }
    ar9300_mci_send_message(ah, MCI_GPM, 0, payload, 16, wait_done, AH_TRUE);
}

static HAL_BOOL ar9300_mci_send_coex_bt_flags(struct ath_hal *ah, HAL_BOOL wait_done,
                                          u_int8_t opcode, u_int32_t bt_flags)
{
//    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t pld[4] = {0, 0, 0, 0};

    MCI_GPM_SET_TYPE_OPCODE(pld,
        MCI_GPM_COEX_AGENT, MCI_GPM_COEX_BT_UPDATE_FLAGS);

    *(((u_int8_t *)pld) + MCI_GPM_COEX_B_BT_FLAGS_OP)  = opcode;
    *(((u_int8_t *)pld) + MCI_GPM_COEX_W_BT_FLAGS + 0) = bt_flags & 0xFF;
    *(((u_int8_t *)pld) + MCI_GPM_COEX_W_BT_FLAGS + 1) =
        (bt_flags >> 8) & 0xFF;
    *(((u_int8_t *)pld) + MCI_GPM_COEX_W_BT_FLAGS + 2) =
        (bt_flags >> 16) & 0xFF;
    *(((u_int8_t *)pld) + MCI_GPM_COEX_W_BT_FLAGS + 3) =
        (bt_flags >> 24) & 0xFF;

    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
        "(MCI) BT_MCI_FLAGS: Send Coex BT Update Flags %s 0x%08x\n",
            (opcode == MCI_GPM_COEX_BT_FLAGS_READ)?"READ":
            ((opcode == MCI_GPM_COEX_BT_FLAGS_SET)?"SET":"CLEAR"),
            bt_flags);

    return ar9300_mci_send_message(ah, MCI_GPM, 0, pld, 16, wait_done, AH_TRUE);
}

void ar9300_mci_2g5g_changed(struct ath_hal *ah, HAL_BOOL is_2g)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (ahp->ah_mci_coex_2g5g_update == AH_FALSE) {
        if (ahp->ah_mci_coex_is_2g == is_2g) {
            //HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) BT_MCI_FLAGS: not changed\n");
        } else {
            ahp->ah_mci_coex_2g5g_update = AH_TRUE;
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) BT_MCI_FLAGS: changed\n");
        }
    } else {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) BT_MCI_FLAGS: force send\n");
    }
    ahp->ah_mci_coex_is_2g = is_2g;
}

static void ar9300_mci_send_2g5g_status(struct ath_hal *ah, HAL_BOOL wait_done)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t new_flags, to_set, to_clear;

    if ((AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) &&
        (ahp->ah_mci_coex_2g5g_update == AH_TRUE) &&
        (ahp->ah_mci_bt_state != MCI_BT_SLEEP))
    {
        if (ahp->ah_mci_coex_is_2g) {
            new_flags = HAL_MCI_2G_FLAGS;
            to_clear = HAL_MCI_2G_FLAGS_CLEAR_MASK;
            to_set = HAL_MCI_2G_FLAGS_SET_MASK;
        } else {
            new_flags = HAL_MCI_5G_FLAGS;
            to_clear = HAL_MCI_5G_FLAGS_CLEAR_MASK;
            to_set = HAL_MCI_5G_FLAGS_SET_MASK;
        }
        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
            "(MCI) BT_MCI_FLAGS: %s (0x%08x) clr=0x%08x, set=0x%08x\n",
            ahp->ah_mci_coex_is_2g?"2G":"5G", new_flags, to_clear, to_set);
        if (to_clear) {
            ar9300_mci_send_coex_bt_flags(ah, wait_done,
                MCI_GPM_COEX_BT_FLAGS_CLEAR, to_clear);
        }
        if (to_set) {
            ar9300_mci_send_coex_bt_flags(ah, wait_done,
                MCI_GPM_COEX_BT_FLAGS_SET, to_set);
        }
    }
    if (AR_SREV_JUPITER_10(ah) && (ahp->ah_mci_bt_state != MCI_BT_SLEEP)) {
        ahp->ah_mci_coex_2g5g_update = AH_FALSE;
    }
}

void ar9300_mci_2g5g_switch(struct ath_hal *ah, HAL_BOOL wait_done)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    if (ahp->ah_mci_coex_2g5g_update)
    {
        if (ahp->ah_mci_coex_is_2g) {
            ar9300_mci_send_2g5g_status(ah, AH_TRUE);

            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) Send LNA trans\n");
            ar9300_mci_send_lna_transfer(ah, AH_TRUE);
            OS_DELAY(5);

            OS_REG_CLR_BIT(ah, AR_MCI_TX_CTRL,
                AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE);
            if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
                OS_REG_CLR_BIT(ah, AR_GLB_CONTROL,
                    AR_BTCOEX_CTRL_BT_OWN_SPDT_CTRL);
                if (!(ah->ah_config.ath_hal_mci_config &
                    ATH_MCI_CONFIG_DISABLE_OSLA))
                {
                    ar9300_mci_osla_setup(ah, AH_TRUE);
                }
            }
        } else {
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) Send LNA take\n");
            ar9300_mci_send_lna_take(ah, AH_TRUE);
            OS_DELAY(5);

            OS_REG_SET_BIT(ah, AR_MCI_TX_CTRL,
                AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE);
            if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
                OS_REG_SET_BIT(ah, AR_GLB_CONTROL,
                    AR_BTCOEX_CTRL_BT_OWN_SPDT_CTRL);
                ar9300_mci_osla_setup(ah, AH_FALSE);
            }

            ar9300_mci_send_2g5g_status(ah, AH_TRUE);
        }
    }

    /*
     * Update self gen chain mask. Also set basic set for
     * txbf.
     */
    if (AR_SREV_JUPITER(ah)) {
        if (ahp->ah_mci_coex_is_2g) {
            ahp->ah_reduced_self_gen_mask = AH_TRUE;
            OS_REG_WRITE(ah, AR_SELFGEN_MASK, 0x02);
            ar9300_txbf_set_basic_set(ah);
        }
        else {
            ahp->ah_reduced_self_gen_mask = AH_FALSE;
            ar9300_txbf_set_basic_set(ah);
        }
    }
}

void ar9300_mci_mute_bt(struct ath_hal *ah)
{

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: called\n", __func__);

    /* disable all MCI messages */ 
    OS_REG_WRITE(ah, AR_MCI_MSG_ATTRIBUTES_TABLE, 0xFFFF0000);
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS0, 0xFFFFFFFF);
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS1, 0xFFFFFFFF);
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS2, 0xFFFFFFFF);
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS3, 0xFFFFFFFF);
    OS_REG_SET_BIT(ah, AR_MCI_TX_CTRL, AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE);
    /* wait pending HW messages to flush out */
    OS_DELAY(10);

    /*
     * Send LNA_TAKE and SYS_SLEEPING when
     * 1. reset not after resuming from full sleep
     * 2. before reset MCI RX, to quiet BT and avoid MCI RX misalignment
     */
    if (MCI_ANT_ARCH_PA_LNA_SHARED(ah->ah_config.ath_hal_mci_config)) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) Send LNA take\n");
        ar9300_mci_send_lna_take(ah, AH_TRUE);
        OS_DELAY(5);
    }
    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) Send sys sleeping\n");
    ar9300_mci_send_sys_sleeping(ah, AH_TRUE);
}

static void ar9300_mci_observation_set_up(struct ath_hal *ah)
{
    /*
     * Set up the observation bus in order to monitor MCI bus
     * through GPIOs (0, 1, 2, and 3).
     */
    /*
    OS_REG_WRITE(ah, AR_GPIO_INTR_POL, 0x00420000);
    OS_REG_WRITE(ah, AR_GPIO_OE_OUT, 0x000000ff); // 4050
    OS_REG_WRITE(ah, AR_GPIO_OUTPUT_MUX1, 0x000bdab4); // 4068
    OS_REG_WRITE(ah, AR_OBS, 0x0000004b); // 4088
    OS_REG_WRITE(ah, AR_DIAG_SW, 0x080c0000);
    OS_REG_WRITE(ah, AR_MACMISC, 0x0001a000);
    OS_REG_WRITE(ah, AR_PHY_TEST, 0x00080000); // a360
    OS_REG_WRITE(ah, AR_PHY_TEST_CTL_STATUS, 0xe0000000); // a364
    */
    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: called; config=0x%08x\n",
        __func__, ah->ah_config.ath_hal_mci_config);

    if (ah->ah_config.ath_hal_mci_config &
        ATH_MCI_CONFIG_MCI_OBS_MCI)
    {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: CONFIG_MCI_OBS_MCI\n", __func__);
        ar9300_gpio_cfg_output(ah, 3, HAL_GPIO_OUTPUT_MUX_AS_MCI_WLAN_DATA);
        ar9300_gpio_cfg_output(ah, 2, HAL_GPIO_OUTPUT_MUX_AS_MCI_WLAN_CLK);
        ar9300_gpio_cfg_output(ah, 1, HAL_GPIO_OUTPUT_MUX_AS_MCI_BT_DATA);
        ar9300_gpio_cfg_output(ah, 0, HAL_GPIO_OUTPUT_MUX_AS_MCI_BT_CLK);
    }
    else if (ah->ah_config.ath_hal_mci_config & 
        ATH_MCI_CONFIG_MCI_OBS_TXRX)
    {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: CONFIG_MCI_OBS_TXRX\n", __func__);
        ar9300_gpio_cfg_output(ah, 3, HAL_GPIO_OUTPUT_MUX_AS_WL_IN_TX);
        ar9300_gpio_cfg_output(ah, 2, HAL_GPIO_OUTPUT_MUX_AS_WL_IN_RX);
        ar9300_gpio_cfg_output(ah, 1, HAL_GPIO_OUTPUT_MUX_AS_BT_IN_TX);
        ar9300_gpio_cfg_output(ah, 0, HAL_GPIO_OUTPUT_MUX_AS_BT_IN_RX);
        ar9300_gpio_cfg_output(ah, 5, HAL_GPIO_OUTPUT_MUX_AS_OUTPUT);
    }
    else if (ah->ah_config.ath_hal_mci_config & 
        ATH_MCI_CONFIG_MCI_OBS_BT)
    {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: CONFIG_MCI_OBS_BT\n", __func__);
        ar9300_gpio_cfg_output(ah, 3, HAL_GPIO_OUTPUT_MUX_AS_BT_IN_TX);
        ar9300_gpio_cfg_output(ah, 2, HAL_GPIO_OUTPUT_MUX_AS_BT_IN_RX);
        ar9300_gpio_cfg_output(ah, 1, HAL_GPIO_OUTPUT_MUX_AS_MCI_BT_DATA);
        ar9300_gpio_cfg_output(ah, 0, HAL_GPIO_OUTPUT_MUX_AS_MCI_BT_CLK);
    }
    else {
        return;
    }

    OS_REG_SET_BIT(ah,
        AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL), AR_GPIO_JTAG_DISABLE);

    if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
        OS_REG_RMW_FIELD(ah, AR_GLB_CONTROL, AR_GLB_DS_JTAG_DISABLE, 1);
        OS_REG_RMW_FIELD(ah, AR_GLB_CONTROL, AR_GLB_WLAN_UART_INTF_EN, 0);
        OS_REG_WRITE(ah, AR_GLB_GPIO_CONTROL, 
                     (OS_REG_READ(ah, AR_GLB_GPIO_CONTROL) | 
                      ATH_MCI_CONFIG_MCI_OBS_GPIO));
    }

    OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL2, AR_BTCOEX_CTRL2_GPIO_OBS_SEL, 0);
    OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL2, AR_BTCOEX_CTRL2_MAC_BB_OBS_SEL, 1);
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_OBS), 0x4b);
    OS_REG_RMW_FIELD(ah, AR_DIAG_SW, AR_DIAG_OBS_PT_SEL1, 0x03);
    OS_REG_RMW_FIELD(ah, AR_DIAG_SW, AR_DIAG_OBS_PT_SEL2, 0x01);    
    OS_REG_RMW_FIELD(ah, AR_MACMISC, AR_MACMISC_MISC_OBS_BUS_LSB, 0x02);
    OS_REG_RMW_FIELD(ah, AR_MACMISC, AR_MACMISC_MISC_OBS_BUS_MSB, 0x03);
    //OS_REG_RMW_FIELD(ah, AR_PHY_TEST, AR_PHY_TEST_BBB_OBS_SEL, 0x01);
    OS_REG_RMW_FIELD(ah, AR_PHY_TEST_CTL_STATUS, 
        AR_PHY_TEST_CTL_DEBUGPORT_SEL, 0x07);
}

static void ar9300_mci_process_gpm_extra(struct ath_hal *ah,
                    u_int8_t gpm_type, u_int8_t gpm_opcode, u_int32_t *p_gpm)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int8_t *p_data = (u_int8_t *) p_gpm;

    switch (gpm_type)
    {
        case MCI_GPM_COEX_AGENT:
            switch (gpm_opcode)
            {
                case MCI_GPM_COEX_VERSION_QUERY:
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) Recv GPM COEX Version Query.\n");
                    ar9300_mci_send_coex_version_response(ah, AH_TRUE);
                    break;

                case MCI_GPM_COEX_VERSION_RESPONSE:
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) Recv GPM COEX Version Response.\n");
                    ahp->ah_mci_coex_major_version_bt =
                        *(p_data + MCI_GPM_COEX_B_MAJOR_VERSION);
                    ahp->ah_mci_coex_minor_version_bt =
                        *(p_data + MCI_GPM_COEX_B_MINOR_VERSION);
                    ahp->ah_mci_coex_bt_version_known = AH_TRUE;
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) BT Coex version: %d.%d\n",
                        ahp->ah_mci_coex_major_version_bt,
                        ahp->ah_mci_coex_minor_version_bt);
                    break;

                case MCI_GPM_COEX_STATUS_QUERY:
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) Recv GPM COEX Status Query = 0x%02X.\n",
                        *(p_data + MCI_GPM_COEX_B_WLAN_BITMAP));
                    //if ((*(p_data + MCI_GPM_COEX_B_WLAN_BITMAP)) &
                    //    MCI_GPM_COEX_QUERY_WLAN_ALL_INFO)
                    {
                        ahp->ah_mci_coex_wlan_channels_update = AH_TRUE;
                        ar9300_mci_send_coex_wlan_channels(ah, AH_TRUE);
                    }
                    break;

                case MCI_GPM_COEX_BT_PROFILE_INFO:
                    ahp->ah_mci_query_bt = AH_TRUE;
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) Recv GPM COEX BT_Profile_Info (drop&query)\n");
                    break;

                case MCI_GPM_COEX_BT_STATUS_UPDATE:
                    ahp->ah_mci_query_bt = AH_TRUE;
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) Recv GPM COEX BT_Status_Update "
                        "SEQ=%d (drop&query)\n",
                        *(p_gpm + 3));
                    break;

                default:
                    break;
            }
        default:
            break;
    }
}

u_int32_t ar9300_mci_wait_for_gpm(struct ath_hal *ah, u_int8_t gpm_type, 
                                  u_int8_t gpm_opcode, int32_t time_out)
{
    u_int32_t *p_gpm = NULL, mismatch = 0, more_data = HAL_MCI_GPM_NOMORE;
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL b_is_bt_cal_done = (gpm_type == MCI_GPM_BT_CAL_DONE);
    u_int32_t offset;
    u_int8_t recv_type = 0, recv_opcode = 0;

    if (time_out == 0) {
        more_data = HAL_MCI_GPM_MORE;
    }

    while (time_out > 0)
    {
        if (p_gpm != NULL) {
            MCI_GPM_RECYCLE(p_gpm);
            p_gpm = NULL;
        }

        if (more_data != HAL_MCI_GPM_MORE) {
            time_out = ar9300_mci_wait_for_interrupt(ah, 
                AR_MCI_INTERRUPT_RX_MSG_RAW, 
                AR_MCI_INTERRUPT_RX_MSG_GPM,
                time_out);
        }

        if (time_out) {
            offset = ar9300_mci_state(ah,
                HAL_MCI_STATE_NEXT_GPM_OFFSET, &more_data);

            if (offset == HAL_MCI_GPM_INVALID) {
                continue;
            }
            p_gpm = (u_int32_t *) (ahp->ah_mci_gpm_buf + offset);
            ar9300_mci_print_msg(ah, AH_FALSE, MCI_GPM, 16, p_gpm);

            recv_type = MCI_GPM_TYPE(p_gpm);
            recv_opcode = MCI_GPM_OPCODE(p_gpm);

            if (MCI_GPM_IS_CAL_TYPE(recv_type)) {
                if (recv_type == gpm_type) {
                    if ((gpm_type == MCI_GPM_BT_CAL_DONE) && !b_is_bt_cal_done)
                    {
                        gpm_type = MCI_GPM_BT_CAL_GRANT;
                        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                            "(MCI) Rcv BT_CAL_DONE. Now Wait BT_CAL_GRANT\n");
                        continue;
                    }
                    if (gpm_type == MCI_GPM_BT_CAL_GRANT) {
                        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                            "(MCI) BT_CAL_GRANT seq=%d, req_count=%d\n",
                            *(p_gpm + 2), *(p_gpm + 3));
                    }
                    break;
                }
            }
            else {
                if ((recv_type == gpm_type) && (recv_opcode == gpm_opcode)) {
                    break;
                }
            }
            
            /* not expected message */
            
            /*
             * Check if it's cal_grant
             *
             * When we're waiting for cal_grant in reset routine, it's
             * possible that BT sends out cal_request at the same time.
             * Since BT's calibration doesn't happen that often, we'll
             * let BT completes calibration then we continue to wait 
             * for cal_grant from BT.
             * Orginal: Wait BT_CAL_GRANT.
             * New: Receive BT_CAL_REQ -> send WLAN_CAL_GRANT -> wait
             * BT_CAL_DONE -> Wait BT_CAL_GRANT.
             */
            if ((gpm_type == MCI_GPM_BT_CAL_GRANT) &&
                (recv_type == MCI_GPM_BT_CAL_REQ))
            {
                u_int32_t payload[4] = {0, 0, 0, 0};

                gpm_type = MCI_GPM_BT_CAL_DONE;
                HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                    "(MCI) Rcv BT_CAL_REQ. Send WLAN_CAL_GRANT.\n");

                MCI_GPM_SET_CAL_TYPE(payload, MCI_GPM_WLAN_CAL_GRANT);
                ar9300_mci_send_message(ah, MCI_GPM, 0, payload, 16, 
                    AH_FALSE, AH_FALSE);

                HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                    "(MCI) Now wait for BT_CAL_DONE.\n");
                continue;
            }
            else {
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                    "(MCI) GPM subtype not match 0x%x\n", *(p_gpm + 1));
                mismatch++;
                ar9300_mci_process_gpm_extra(ah, recv_type, recv_opcode, p_gpm);
            }
        }
    }
    if (p_gpm != NULL) {
        MCI_GPM_RECYCLE(p_gpm);
        p_gpm = NULL;
    }

    if (time_out <= 0) {
        time_out = 0;
        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
            "(MCI) GPM receiving timeout, mismatch = %d\n", mismatch);
    } else {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
            "(MCI) Receive GPM type=0x%x, code=0x%x\n", gpm_type, gpm_opcode);
    }

    while (more_data == HAL_MCI_GPM_MORE) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) discard remaining GPM\n");
        offset = ar9300_mci_state(ah,
            HAL_MCI_STATE_NEXT_GPM_OFFSET, &more_data);

        if (offset == HAL_MCI_GPM_INVALID) {
            break;
        }
        p_gpm = (u_int32_t *) (ahp->ah_mci_gpm_buf + offset);
        ar9300_mci_print_msg(ah, AH_FALSE, MCI_GPM, 16, p_gpm);
        recv_type = MCI_GPM_TYPE(p_gpm);
        recv_opcode = MCI_GPM_OPCODE(p_gpm);
        if (!MCI_GPM_IS_CAL_TYPE(recv_type)) {
            ar9300_mci_process_gpm_extra(ah, recv_type, recv_opcode, p_gpm);
        }
        MCI_GPM_RECYCLE(p_gpm);
    }

    return time_out;
}

static void ar9300_mci_prep_interface(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t saved_mci_int_en;
    u_int32_t mci_timeout = 150;

    ahp->ah_mci_bt_state = MCI_BT_SLEEP;

    saved_mci_int_en = OS_REG_READ(ah, AR_MCI_INTERRUPT_EN);
    OS_REG_WRITE(ah, AR_MCI_INTERRUPT_EN, 0);

    OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_RAW,
        OS_REG_READ(ah, AR_MCI_INTERRUPT_RX_MSG_RAW));
    OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RAW,
        OS_REG_READ(ah, AR_MCI_INTERRUPT_RAW));

    /* Remote Reset */
    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) %s: Reset sequence start\n", __func__);
    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) send REMOTE_RESET\n");
    ar9300_mci_remote_reset(ah, AH_TRUE);

    /*
     * This delay is required for the reset delay worst case value 255 in 
     * MCI_COMMAND2 register 
     */
    if (AR_SREV_JUPITER_10(ah)) {
        OS_DELAY(252);
    }

    /* Send REQ_WAKE to BT */
    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) %s: Send REQ_WAKE to remote(BT)\n",
        __func__);

    ar9300_mci_send_req_wake(ah, AH_TRUE);

    if (ar9300_mci_wait_for_interrupt(ah, AR_MCI_INTERRUPT_RX_MSG_RAW, 
        AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING, 500))
    {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
            "(MCI) %s: Saw SYS_WAKING from remote(BT)\n", __func__);
        ahp->ah_mci_bt_state = MCI_BT_AWAKE;

        if (AR_SREV_JUPITER_10(ah)) {
            OS_DELAY(10);
        }
        /*
         * We don't need to send more remote_reset at this moment.
         *
         * If BT receive first remote_reset, then BT HW will be cleaned up and
         * will be able to receive req_wake and BT HW will respond sys_waking.
         * In this case, WLAN will receive BT's HW sys_waking.
         *
         * Otherwise, if BT SW missed initial remote_reset, that remote_reset
         * will still clean up BT MCI RX, and the req_wake will wake BT up,
         * and BT SW will respond this req_wake with a remote_reset and
         * sys_waking. In this case, WLAN will receive BT's SW sys_waking.
         *
         * In either case, BT's RX is cleaned up. So we don't need to reply
         * BT's remote_reset now, if any.
         *
         * Similarly, if in any case, WLAN can receive BT's sys_waking, that
         * means WLAN's RX is also fine.
         */

        /* Send SYS_WAKING to BT */
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
            "(MCI) %s: Send SW SYS_WAKING to remot(BT)\n", __func__);
        ar9300_mci_send_sys_waking(ah, AH_TRUE);

        OS_DELAY(10);

        /*
         * Set BT priority interrupt value to be 0xff to
         * avoid having too many BT PRIORITY interrupts.
         */

        OS_REG_WRITE(ah, AR_MCI_BT_PRI0, 0xFFFFFFFF);
        OS_REG_WRITE(ah, AR_MCI_BT_PRI1, 0xFFFFFFFF);
        OS_REG_WRITE(ah, AR_MCI_BT_PRI2, 0xFFFFFFFF);
        OS_REG_WRITE(ah, AR_MCI_BT_PRI3, 0xFFFFFFFF);
        OS_REG_WRITE(ah, AR_MCI_BT_PRI, 0X000000FF);

        /*
         * A contention reset will be received after send out sys_waking.
         * Also BT priority interrupt bits will be set. Clear those bits
         * before the next step.
         */
        OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_RAW, 
            AR_MCI_INTERRUPT_RX_MSG_CONT_RST);
        OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RAW, AR_MCI_INTERRUPT_BT_PRI);

        if (AR_SREV_JUPITER_10(ah) ||
           (ahp->ah_mci_coex_is_2g &&
            MCI_ANT_ARCH_PA_LNA_SHARED(ah->ah_config.ath_hal_mci_config))) {
            /* Send LNA_TRANS */
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) %s: Send LNA_TRANS to BT\n", 
                __func__);
            ar9300_mci_send_lna_transfer(ah, AH_TRUE);
    
            OS_DELAY(5);
        }

        if (AR_SREV_JUPITER_10(ah) ||
            (ahp->ah_mci_coex_is_2g && !ahp->ah_mci_coex_2g5g_update &&
            MCI_ANT_ARCH_PA_LNA_SHARED(ah->ah_config.ath_hal_mci_config))) {
            if (ar9300_mci_wait_for_interrupt(ah, AR_MCI_INTERRUPT_RX_MSG_RAW, 
                AR_MCI_INTERRUPT_RX_MSG_LNA_INFO, mci_timeout)) {
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                    "(MCI) %s: WLAN has control over the LNA & BT obeys it\n", 
                    __func__);
            } else {
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                    "(MCI) %s: BT did not respond to LNA_TRANS!\n", __func__);
                //ahp->ah_mci_bt_state = MCI_BT_SLEEP;
            }
        }

        if (AR_SREV_JUPITER_10(ah)) {
            /* Send another remote_reset to deassert BT clk_req. */
            HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                "(MCI) %s: Another remote_reset to deassert clk_req.\n", 
                __func__);
            ar9300_mci_remote_reset(ah, AH_TRUE);
            OS_DELAY(252);
        }
    }

    /* Clear the extra redundant SYS_WAKING from BT */
    if ((ahp->ah_mci_bt_state == MCI_BT_AWAKE) &&
        (OS_REG_READ_FIELD(ah, AR_MCI_INTERRUPT_RX_MSG_RAW,
            AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING)) &&
        (OS_REG_READ_FIELD(ah, AR_MCI_INTERRUPT_RX_MSG_RAW,
            AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING) == 0))
    {
        OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_RAW,
            AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING);
        OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RAW,
            AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE);
    }

    OS_REG_WRITE(ah, AR_MCI_INTERRUPT_EN, saved_mci_int_en);
}

void ar9300_mci_setup(struct ath_hal *ah, u_int32_t gpm_addr, 
                      void *gpm_buf, u_int16_t len,
                      u_int32_t sched_addr)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    void *sched_buf = (void *)((char *) gpm_buf + (sched_addr - gpm_addr));

    ahp->ah_mci_gpm_addr = gpm_addr;
    ahp->ah_mci_gpm_buf = gpm_buf;
    ahp->ah_mci_gpm_len = len;
    ahp->ah_mci_sched_addr = sched_addr;
    ahp->ah_mci_sched_buf = sched_buf;

    ar9300_mci_reset(ah, AH_TRUE, AH_TRUE, AH_TRUE);
}

void ar9300_mci_disable_interrupt(struct ath_hal *ah)
{
    OS_REG_WRITE(ah, AR_MCI_INTERRUPT_EN, 0);
    OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_EN, 0);
}

void ar9300_mci_enable_interrupt(struct ath_hal *ah)
{
    OS_REG_WRITE(ah, AR_MCI_INTERRUPT_EN, AR_MCI_INTERRUPT_DEFAULT);
    OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_EN, 
        AR_MCI_INTERRUPT_RX_MSG_DEFAULT);
}

static void ar9300_mci_set_btcoex_ctrl_9565_1ANT(struct ath_hal *ah)
{
    uint32_t regval;

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: called\n", __func__);
    regval = SM(1, AR_BTCOEX_CTRL_JUPITER_MODE) |
      SM(1, AR_BTCOEX_CTRL_WBTIMER_EN) |
      SM(1, AR_BTCOEX_CTRL_PA_SHARED) |
      SM(1, AR_BTCOEX_CTRL_LNA_SHARED) |
      SM(1, AR_BTCOEX_CTRL_NUM_ANTENNAS) |
      SM(1, AR_BTCOEX_CTRL_RX_CHAIN_MASK) |
      SM(0, AR_BTCOEX_CTRL_1_CHAIN_ACK) |
      SM(0, AR_BTCOEX_CTRL_1_CHAIN_BCN) |
      SM(0, AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN);

    OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL2,
      AR_BTCOEX_CTRL2_TX_CHAIN_MASK, 0x1);
    OS_REG_WRITE(ah, AR_BTCOEX_CTRL, regval);
}

static void ar9300_mci_set_btcoex_ctrl_9565_2ANT(struct ath_hal *ah)
{
    uint32_t regval;

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: called\n", __func__);
    regval = SM(1, AR_BTCOEX_CTRL_JUPITER_MODE) |
      SM(1, AR_BTCOEX_CTRL_WBTIMER_EN) |
      SM(0, AR_BTCOEX_CTRL_PA_SHARED) |
      SM(0, AR_BTCOEX_CTRL_LNA_SHARED) |
      SM(2, AR_BTCOEX_CTRL_NUM_ANTENNAS) |
      SM(1, AR_BTCOEX_CTRL_RX_CHAIN_MASK) |
      SM(0, AR_BTCOEX_CTRL_1_CHAIN_ACK) |
      SM(0, AR_BTCOEX_CTRL_1_CHAIN_BCN) |
      SM(0, AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN);

    OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL2,
      AR_BTCOEX_CTRL2_TX_CHAIN_MASK, 0x0);
    OS_REG_WRITE(ah, AR_BTCOEX_CTRL, regval);
}

static void ar9300_mci_set_btcoex_ctrl_9462(struct ath_hal *ah)
{
    uint32_t regval;

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: called\n", __func__);
    regval = SM(1, AR_BTCOEX_CTRL_JUPITER_MODE) |
      SM(1, AR_BTCOEX_CTRL_WBTIMER_EN) |
      SM(1, AR_BTCOEX_CTRL_PA_SHARED) |
      SM(1, AR_BTCOEX_CTRL_LNA_SHARED) |
      SM(2, AR_BTCOEX_CTRL_NUM_ANTENNAS) |
      SM(3, AR_BTCOEX_CTRL_RX_CHAIN_MASK) |
      SM(0, AR_BTCOEX_CTRL_1_CHAIN_ACK) |
      SM(0, AR_BTCOEX_CTRL_1_CHAIN_BCN) |
      SM(0, AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN);

    if (AR_SREV_JUPITER_10(ah)) {
        regval |= SM(1, AR_BTCOEX_CTRL_SPDT_ENABLE_10);
    }

    OS_REG_WRITE(ah, AR_BTCOEX_CTRL, regval);
}

void ar9300_mci_reset(struct ath_hal *ah, HAL_BOOL en_int, HAL_BOOL is_2g,
                      HAL_BOOL is_full_sleep)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
//    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    u_int32_t regval;

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) %s: full_sleep = %d, is_2g = %d\n",
        __func__, is_full_sleep, is_2g);

    if (!ahp->ah_mci_gpm_addr && !ahp->ah_mci_sched_addr) {
        /* GPM buffer and scheduling message buffer are not allocated */
        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
            "(MCI) GPM and SCHEDULE buffers not allocated\n");
        return;
    }

    if (OS_REG_READ(ah, AR_BTCOEX_CTRL) == 0xdeadbeef) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
            "(MCI) %s: ### It's deadbeef, quit mcireset()\n", __func__);
        return;
    }

    /* Program MCI DMA related registers */
    OS_REG_WRITE(ah, AR_MCI_GPM_0, ahp->ah_mci_gpm_addr);
    OS_REG_WRITE(ah, AR_MCI_GPM_1, ahp->ah_mci_gpm_len);
    OS_REG_WRITE(ah, AR_MCI_SCHD_TABLE_0, ahp->ah_mci_sched_addr);

    /*
     * To avoid MCI state machine be affected by incoming remote MCI messages,
     * MCI mode will be enabled later, right before reset the MCI TX and RX.
     */
    if (AR_SREV_APHRODITE(ah)) {
        uint8_t ant = MS(ah->ah_config.ath_hal_mci_config,
          ATH_MCI_CONFIG_ANT_ARCH);
        if (ant == ATH_MCI_ANT_ARCH_1_ANT_PA_LNA_SHARED)
            ar9300_mci_set_btcoex_ctrl_9565_1ANT(ah);
        else
            ar9300_mci_set_btcoex_ctrl_9565_2ANT(ah);
    } else {
            ar9300_mci_set_btcoex_ctrl_9462(ah);
    }


    if (is_2g && (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) &&
         !(ah->ah_config.ath_hal_mci_config &
           ATH_MCI_CONFIG_DISABLE_OSLA))
    {
        ar9300_mci_osla_setup(ah, AH_TRUE);
    }
    else {
        ar9300_mci_osla_setup(ah, AH_FALSE);
    }

    if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
        OS_REG_SET_BIT(ah, AR_GLB_CONTROL, AR_BTCOEX_CTRL_SPDT_ENABLE);

        OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL3,
                         AR_BTCOEX_CTRL3_CONT_INFO_TIMEOUT, 20);
    }

    OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL2, AR_BTCOEX_CTRL2_RX_DEWEIGHT, 0);

    OS_REG_RMW_FIELD(ah, AR_PCU_MISC, AR_PCU_BT_ANT_PREVENT_RX, 0);

    /* Set the time out to 3.125ms (5 BT slots) */
    OS_REG_RMW_FIELD(ah, AR_BTCOEX_WL_LNA, AR_BTCOEX_WL_LNA_TIMEOUT, 0x3D090);

    if (ah->ah_config.ath_hal_mci_config & ATH_MCI_CONFIG_CONCUR_TX) {
        u_int8_t i;
        u_int32_t const *pmax_tx_pwr;

        if ((ah->ah_config.ath_hal_mci_config & 
             ATH_MCI_CONFIG_CONCUR_TX) == ATH_MCI_CONCUR_TX_SHARED_CHN)
        {
            ahp->ah_mci_concur_tx_en = (ahp->ah_bt_coex_flag & 
                HAL_BT_COEX_FLAG_MCI_MAX_TX_PWR) ? AH_TRUE : AH_FALSE;

            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) concur_tx_en = %d\n", 
                     ahp->ah_mci_concur_tx_en);
            /*
             * We're not relying on HW to reduce WLAN tx power.
             * Set the max tx power table to 0x7f for all.
             */
#if 0
            if (AH_PRIVATE(ah)->ah_curchan) {
                chan_flags = AH_PRIVATE(ah)->ah_curchan->channel_flags;
            }
            if (chan_flags == CHANNEL_G_HT20) {
                pmax_tx_pwr = &mci_concur_tx_max_pwr[2][0];
            }
            else if (chan_flags == CHANNEL_G) {
                pmax_tx_pwr = &mci_concur_tx_max_pwr[1][0];
            }
            else if ((chan_flags == CHANNEL_G_HT40PLUS) || 
                     (chan_flags == CHANNEL_G_HT40MINUS))
            {
                pmax_tx_pwr = &mci_concur_tx_max_pwr[3][0];
            }
            else {
                pmax_tx_pwr = &mci_concur_tx_max_pwr[0][0];
            }

            if (ahp->ah_mci_concur_tx_en) {
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                        "(MCI) chan flags = 0x%x, max_tx_pwr = %d dBm\n", 
                        chan_flags, 
                        (MS(pmax_tx_pwr[2],
                         ATH_MCI_CONCUR_TX_LOWEST_PWR_MASK) >> 1));
            }
#else
            pmax_tx_pwr = &mci_concur_tx_max_pwr[0][0];
#endif
        }
        else if ((ah->ah_config.ath_hal_mci_config &
                  ATH_MCI_CONFIG_CONCUR_TX) == ATH_MCI_CONCUR_TX_UNSHARED_CHN)
        {
            pmax_tx_pwr = &mci_concur_tx_max_pwr[0][0];
            ahp->ah_mci_concur_tx_en = AH_TRUE;
        }
        else {
            pmax_tx_pwr = &mci_concur_tx_max_pwr[0][0];
            ahp->ah_mci_concur_tx_en = AH_TRUE;
        }

    	/* Default is using rate based TPC. */
        OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL2, 
                         AR_BTCOEX_CTRL2_DESC_BASED_TXPWR_ENABLE, 0);
        OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL2,
                         AR_BTCOEX_CTRL2_TXPWR_THRESH, 0x7f);
        OS_REG_RMW_FIELD(ah, AR_BTCOEX_CTRL, 
                         AR_BTCOEX_CTRL_REDUCE_TXPWR, 0);
        for (i = 0; i < 8; i++) {
            OS_REG_WRITE(ah, AR_BTCOEX_MAX_TXPWR(i), pmax_tx_pwr[i]);
        }
    }

    regval = MS(ah->ah_config.ath_hal_mci_config,
                ATH_MCI_CONFIG_CLK_DIV);
    OS_REG_RMW_FIELD(ah, AR_MCI_TX_CTRL, AR_MCI_TX_CTRL_CLK_DIV, regval);

    OS_REG_SET_BIT(ah, AR_BTCOEX_CTRL, AR_BTCOEX_CTRL_MCI_MODE_EN);

    /* Resetting the Rx and Tx paths of MCI */
    regval = OS_REG_READ(ah, AR_MCI_COMMAND2);
    regval |= SM(1, AR_MCI_COMMAND2_RESET_TX);
    OS_REG_WRITE(ah, AR_MCI_COMMAND2, regval);
    OS_DELAY(1);
    regval &= ~SM(1, AR_MCI_COMMAND2_RESET_TX);
    OS_REG_WRITE(ah, AR_MCI_COMMAND2, regval);

    if (is_full_sleep) {
        ar9300_mci_mute_bt(ah);
        OS_DELAY(100);
    }

    regval |= SM(1, AR_MCI_COMMAND2_RESET_RX);
    OS_REG_WRITE(ah, AR_MCI_COMMAND2, regval);
    OS_DELAY(1);
    regval &= ~SM(1, AR_MCI_COMMAND2_RESET_RX);
    OS_REG_WRITE(ah, AR_MCI_COMMAND2, regval);

    ar9300_mci_state(ah, HAL_MCI_STATE_INIT_GPM_OFFSET, NULL);
    OS_REG_WRITE(ah, AR_MCI_MSG_ATTRIBUTES_TABLE,
             (SM(0xe801, AR_MCI_MSG_ATTRIBUTES_TABLE_INVALID_HDR) |
              SM(0x0000, AR_MCI_MSG_ATTRIBUTES_TABLE_CHECKSUM)));
    if (MCI_ANT_ARCH_PA_LNA_SHARED(ah->ah_config.ath_hal_mci_config)) {
        OS_REG_CLR_BIT(ah, AR_MCI_TX_CTRL, AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE);
    } else {
        OS_REG_SET_BIT(ah, AR_MCI_TX_CTRL, AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE);
    }

    if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
        ar9300_mci_observation_set_up(ah);
    }
    
    ahp->ah_mci_ready = AH_TRUE;
    ar9300_mci_prep_interface(ah);

    if (en_int) {
        ar9300_mci_enable_interrupt(ah);
    }

#if ATH_SUPPORT_AIC
    if (ahp->ah_aic_enabled) {
        ar9300_aic_start_normal(ah);
    }
#endif
}

static void ar9300_mci_queue_unsent_gpm(struct ath_hal *ah, u_int8_t header,
                                        u_int32_t *payload, HAL_BOOL queue)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int8_t type, opcode;

    if (queue == AH_TRUE) {
        if (payload != NULL) {
            HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                "(MCI) ERROR: Send fail: %02x: %02x %02x %02x\n",
                header,
                *(((u_int8_t *)payload) + 4),
                *(((u_int8_t *)payload) + 5),
                *(((u_int8_t *)payload) + 6));
        } else {
            HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                "(MCI) ERROR: Send fail: %02x\n", header);
        }
    }
    /* check if the message is to be queued */
    if (header == MCI_GPM) {
        type = MCI_GPM_TYPE(payload);
        opcode = MCI_GPM_OPCODE(payload);

        if (type == MCI_GPM_COEX_AGENT) {
            switch (opcode)
            {
                case MCI_GPM_COEX_BT_UPDATE_FLAGS:
                    if (AR_SREV_JUPITER_10(ah)) {
                        break;
                    }
                    if (*(((u_int8_t *)payload) + MCI_GPM_COEX_B_BT_FLAGS_OP) ==
                        MCI_GPM_COEX_BT_FLAGS_READ)
                    {
                        break;
                    }
                    ahp->ah_mci_coex_2g5g_update = queue;
                    if (queue == AH_TRUE) {
                        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                            "(MCI) BT_MCI_FLAGS: 2G5G status <queued> %s.\n",
                            ahp->ah_mci_coex_is_2g?"2G":"5G");
                    }
                    else {
                        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                            "(MCI) BT_MCI_FLAGS: 2G5G status <sent> %s.\n",
                            ahp->ah_mci_coex_is_2g?"2G":"5G");
                    }
                    break;

                case MCI_GPM_COEX_WLAN_CHANNELS:
                    ahp->ah_mci_coex_wlan_channels_update = queue;
                    if (queue == AH_TRUE) {
                        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                            "(MCI) WLAN channel map <queued>.\n");
                    }
                    else {
                        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                            "(MCI) WLAN channel map <sent>.\n");
                    }
                    break;

                case MCI_GPM_COEX_HALT_BT_GPM:
                    if (*(((u_int8_t *)payload) + MCI_GPM_COEX_B_HALT_STATE) ==
                        MCI_GPM_COEX_BT_GPM_UNHALT)
                    {
                        ahp->ah_mci_unhalt_bt_gpm = queue;
                        if (queue == AH_TRUE) {
                            HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                                "(MCI) UNHALT BT GPM <queued>.\n");
                        }
                        else {
                            ahp->ah_mci_halted_bt_gpm = AH_FALSE;
                            HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                                "(MCI) UNHALT BT GPM <sent>.\n");
                        }
                    }
                    if (*(((u_int8_t *)payload) + MCI_GPM_COEX_B_HALT_STATE) ==
                        MCI_GPM_COEX_BT_GPM_HALT)
                    {
                        ahp->ah_mci_halted_bt_gpm = !queue;
                        if (queue == AH_TRUE) {
                            HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                                "(MCI) HALT BT GPM <not sent>.\n");
                        }
                        else {
                            HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                                "(MCI) HALT BT GPM <sent>.\n");
                        }
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

HAL_BOOL ar9300_mci_send_message(struct ath_hal *ah, u_int8_t header,
                              u_int32_t flag, u_int32_t *payload, 
                              u_int8_t len, HAL_BOOL wait_done, HAL_BOOL check_bt)
{
    int i;
    struct ath_hal_9300 *ahp = AH9300(ah);
    HAL_BOOL msg_sent = AH_FALSE;
    u_int32_t regval;
    u_int32_t saved_mci_int_en = OS_REG_READ(ah, AR_MCI_INTERRUPT_EN);

    regval = OS_REG_READ(ah, AR_BTCOEX_CTRL);
    if ((regval == 0xdeadbeef) || !(regval & AR_BTCOEX_CTRL_MCI_MODE_EN)) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
            "(MCI) %s: Not send 0x%x. MCI is not enabled. full_sleep = %d\n", 
            __func__, header, ahp->ah_chip_full_sleep);
        ar9300_mci_queue_unsent_gpm(ah, header, payload, AH_TRUE);
        return AH_FALSE;
    }
    else if (check_bt && (ahp->ah_mci_bt_state == MCI_BT_SLEEP)) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
            "(MCI) %s: Don't send message(0x%x). BT is in sleep state\n", 
            __func__, header);
        ar9300_mci_queue_unsent_gpm(ah, header, payload, AH_TRUE);
        return AH_FALSE;
    }

    if (wait_done) {
        OS_REG_WRITE(ah, AR_MCI_INTERRUPT_EN, 0);
    }

    /* Need to clear SW_MSG_DONE raw bit before wait */
    OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RAW,
        AR_MCI_INTERRUPT_SW_MSG_DONE | AR_MCI_INTERRUPT_MSG_FAIL_MASK);

    if (payload != AH_NULL) {
        for (i = 0; (i*4) < len; i++) {
            OS_REG_WRITE(ah, (AR_MCI_TX_PAYLOAD0 + i*4), *(payload + i));
        }
    }
    ar9300_mci_print_msg(ah, AH_TRUE, header, len, payload);

    OS_REG_WRITE(ah, AR_MCI_COMMAND0,
                (SM((flag & HAL_MCI_FLAG_DISABLE_TIMESTAMP), 
                 AR_MCI_COMMAND0_DISABLE_TIMESTAMP) |
                 SM(len, AR_MCI_COMMAND0_LEN) |
                 SM(header, AR_MCI_COMMAND0_HEADER)));

    if (wait_done &&
        ar9300_mci_wait_for_interrupt(ah, AR_MCI_INTERRUPT_RAW, 
                                    AR_MCI_INTERRUPT_SW_MSG_DONE, 500) == 0)
    {
        ar9300_mci_queue_unsent_gpm(ah, header, payload, AH_TRUE);
    }
    else {
        ar9300_mci_queue_unsent_gpm(ah, header, payload, AH_FALSE);
        msg_sent = AH_TRUE;
    }
    
    if (wait_done) {
        OS_REG_WRITE(ah, AR_MCI_INTERRUPT_EN, saved_mci_int_en);
    }

    return msg_sent;
}

u_int32_t ar9300_mci_get_interrupt(struct ath_hal *ah, u_int32_t *mci_int, 
                                   u_int32_t *mci_int_rx_msg)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    *mci_int = ahp->ah_mci_int_raw;
    *mci_int_rx_msg = ahp->ah_mci_int_rx_msg;

    /* Clean int bits after the values are read. */
    ahp->ah_mci_int_raw = 0;
    ahp->ah_mci_int_rx_msg = 0;

    return 0;
}

u_int32_t ar9300_mci_check_int(struct ath_hal *ah, u_int32_t ints)
{
    u_int32_t reg;

    reg = OS_REG_READ(ah, AR_MCI_INTERRUPT_RX_MSG_RAW);
    return ((reg & ints) == ints);
}

void ar9300_mci_sync_bt_state(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t cur_bt_state;

    cur_bt_state = ar9300_mci_state(ah, HAL_MCI_STATE_REMOTE_SLEEP, NULL);
    if (ahp->ah_mci_bt_state != cur_bt_state) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
            "(MCI) %s: BT state mismatches. old: %d, new: %d\n",
            __func__, ahp->ah_mci_bt_state, cur_bt_state);
        ahp->ah_mci_bt_state = cur_bt_state;
    }
    if (ahp->ah_mci_bt_state != MCI_BT_SLEEP) {
#if MCI_QUERY_BT_VERSION_VERBOSE
        ar9300_mci_send_coex_version_query(ah, AH_TRUE);
#endif
        ar9300_mci_send_coex_wlan_channels(ah, AH_TRUE);
        if (ahp->ah_mci_unhalt_bt_gpm == AH_TRUE) {
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) %s: UNHALT BT GPM\n", __func__);
            ar9300_mci_send_coex_halt_bt_gpm(ah, AH_FALSE, AH_TRUE);
        }
    }
}

static HAL_BOOL ar9300_mci_is_gpm_valid(struct ath_hal *ah, u_int32_t msg_index)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t *payload;
    u_int32_t recv_type, offset = msg_index << 4;

    if (msg_index == HAL_MCI_GPM_INVALID) {
        return AH_FALSE;
    }

    payload = (u_int32_t *) (ahp->ah_mci_gpm_buf + offset);
    recv_type = MCI_GPM_TYPE(payload);

    if (recv_type == MCI_GPM_RSVD_PATTERN) {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) Skip RSVD GPM\n");
        return AH_FALSE;
    }

    return AH_TRUE;
}

u_int32_t
ar9300_mci_state(struct ath_hal *ah, u_int32_t state_type, u_int32_t *p_data)
{
    u_int32_t   value = 0, more_gpm = 0, gpm_ptr;
    struct ath_hal_9300 *ahp = AH9300(ah);

    switch (state_type) {
        case HAL_MCI_STATE_ENABLE:
            if (AH_PRIVATE(ah)->ah_caps.halMciSupport && ahp->ah_mci_ready) {
                value = OS_REG_READ(ah, AR_BTCOEX_CTRL);
                if ((value == 0xdeadbeef) || (value == 0xffffffff)) {
                        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                            "(MCI) BTCOEX_CTRL = 0xdeadbeef\n");
                    value = 0;
                }
            }
            value &= AR_BTCOEX_CTRL_MCI_MODE_EN;
            break;

        case HAL_MCI_STATE_INIT_GPM_OFFSET:
            value = MS(OS_REG_READ(ah, AR_MCI_GPM_1), AR_MCI_GPM_WRITE_PTR);
            HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                    "(MCI) %s: GPM initial WRITE_PTR=%d.\n", __func__, value);
            ahp->ah_mci_gpm_idx = value;
            break;

        case HAL_MCI_STATE_NEXT_GPM_OFFSET:
        case HAL_MCI_STATE_LAST_GPM_OFFSET:
            /*
             * This could be useful to avoid new GPM message interrupt which
             * may lead to spurious interrupt after power sleep, or multiple
             * entry of ath_coex_mci_intr().
             * Adding empty GPM check by returning HAL_MCI_GPM_INVALID can
             * alleviate this effect, but clearing GPM RX interrupt bit is
             * safe, because whether this is called from HAL or LMAC, there
             * must be an interrupt bit set/triggered initially.
             */
            OS_REG_WRITE(ah, AR_MCI_INTERRUPT_RX_MSG_RAW,
                         AR_MCI_INTERRUPT_RX_MSG_GPM);

            gpm_ptr = MS(OS_REG_READ(ah, AR_MCI_GPM_1), AR_MCI_GPM_WRITE_PTR);
            value = gpm_ptr;

            if (value == 0) {
                value = ahp->ah_mci_gpm_len - 1;
            }
            else if (value >= ahp->ah_mci_gpm_len) {
                if (value != 0xFFFF) {
                    value = 0;
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) %s: GPM offset out of range.\n", __func__);
                }
            }
            else {
                value--;
            }

            if (value == 0xFFFF) {
                value = HAL_MCI_GPM_INVALID;
                more_gpm = HAL_MCI_GPM_NOMORE;
                HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) %s: GPM ptr invalid "
                        "@ptr=%d, @offset=%d, more=NOMORE.\n",
                        __func__, gpm_ptr, value);
            }
            else if (state_type == HAL_MCI_STATE_NEXT_GPM_OFFSET) {
                if (gpm_ptr == ahp->ah_mci_gpm_idx) {
                    value = HAL_MCI_GPM_INVALID;
                    more_gpm = HAL_MCI_GPM_NOMORE;
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                            "(MCI) %s: GPM message not available "
                            "@ptr=%d, @offset=%d, more=NOMORE.\n",
                            __func__, gpm_ptr, value);
                }
                else {
                    while (1) {
                        u_int32_t temp_index;

                        /* skip reserved GPM if any */
                        if (value != ahp->ah_mci_gpm_idx) {
                            more_gpm = HAL_MCI_GPM_MORE;
                        }
                        else {
                            more_gpm = HAL_MCI_GPM_NOMORE;
                        }
                        temp_index = ahp->ah_mci_gpm_idx;
                        ahp->ah_mci_gpm_idx++;
                        if (ahp->ah_mci_gpm_idx >= ahp->ah_mci_gpm_len) {
                            ahp->ah_mci_gpm_idx = 0;
                        }
                        HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                                "(MCI) %s: GPM message got "
                                "@ptr=%d, @offset=%d, more=%s.\n",
                                __func__, gpm_ptr, temp_index,
                                (more_gpm == HAL_MCI_GPM_MORE)?"MORE":"NOMORE");
                        if (ar9300_mci_is_gpm_valid(ah, temp_index)) {
                            value = temp_index;
                            break;
                        }
                        if (more_gpm == HAL_MCI_GPM_NOMORE) {
                            value = HAL_MCI_GPM_INVALID;
                            break;
                        }
                    }
                }
                if (p_data != NULL) {
                    *p_data = more_gpm;
                }
            }
            if (value != HAL_MCI_GPM_INVALID) {
                value <<= 4;
            }
            break;

    case HAL_MCI_STATE_LAST_SCHD_MSG_OFFSET:
        value = MS(OS_REG_READ(ah, AR_MCI_RX_STATUS), 
            AR_MCI_RX_LAST_SCHD_MSG_INDEX);

#if AH_MCI_DEBUG_PRINT_SCHED
        {
            u_int32_t index = value;
            u_int32_t prev_index, sched_idx;
            u_int32_t *pld;
            u_int8_t  *pld8;
            u_int32_t wbtimer = OS_REG_READ(ah, AR_BTCOEX_WBTIMER);
            u_int32_t schd_ctl = OS_REG_READ(ah, AR_MCI_HW_SCHD_TBL_CTL);

            if (index > 0) {
                prev_index = index - 1;
            } else {
                prev_index = index;
            }

            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) SCHED\n");
            HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                "(MCI) SCHED SCHD_TBL_CTRL=0x%08x, WBTIMER=0x%08x (%d)\n",
                schd_ctl, wbtimer, wbtimer);
            for (sched_idx = prev_index; sched_idx <= index; sched_idx++) {
                pld = (u_int32_t *) (ahp->ah_mci_sched_buf + (sched_idx << 4));
                pld8 = (u_int8_t *) pld;

                ar9300_mci_print_msg(ah, AH_FALSE, MCI_SCHD_INFO, 16, pld);
                HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                    "(MCI) SCHED    idx=%d, T1=0x%08x (%d), T2=0x%08x (%d)\n",
                    sched_idx,
                    pld[0], pld[0], pld[1], pld[1]);
                HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                    "(MCI) SCHED    addr=%d %s pwr=%d prio=%d %s link=%d\n",
                    pld8[11] >> 4,
                    (pld8[11] & 0x08)?"TX":"RX",
                    (int8_t) (((pld8[11] & 0x07) << 5) | (pld8[10] >> 3)),
                    (((pld8[10] & 0x07) << 5) | (pld8[9] >> 3)),
                    (pld8[9] & 0x04)?"LE":"BR/EDR",
                    (((pld8[9] & 0x03) << 2) | (pld8[8] >> 6)));
            }
        }
#endif /* AH_MCI_DEBUG_PRINT_SCHED */

        /* Make it in bytes */
        value <<= 4;
        break;

    case HAL_MCI_STATE_REMOTE_SLEEP:
        value = MS(OS_REG_READ(ah, AR_MCI_RX_STATUS), 
            AR_MCI_RX_REMOTE_SLEEP) ? MCI_BT_SLEEP : MCI_BT_AWAKE;
        break;

        case HAL_MCI_STATE_CONT_RSSI_POWER:
            value = MS(ahp->ah_mci_cont_status,
                AR_MCI_CONT_RSSI_POWER);
            break;

        case HAL_MCI_STATE_CONT_PRIORITY:
            value = MS(ahp->ah_mci_cont_status,
                AR_MCI_CONT_RRIORITY);
            break;

        case HAL_MCI_STATE_CONT_TXRX:
            value = MS(ahp->ah_mci_cont_status,
                AR_MCI_CONT_TXRX);
            break;

        case HAL_MCI_STATE_BT:
            value = ahp->ah_mci_bt_state;
            break;

        case HAL_MCI_STATE_SET_BT_SLEEP:
            ahp->ah_mci_bt_state = MCI_BT_SLEEP;
            break;

        case HAL_MCI_STATE_SET_BT_AWAKE:
            ahp->ah_mci_bt_state = MCI_BT_AWAKE;
            ar9300_mci_send_coex_version_query(ah, AH_TRUE);
            ar9300_mci_send_coex_wlan_channels(ah, AH_TRUE);
            if (ahp->ah_mci_unhalt_bt_gpm == AH_TRUE) {
                HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                    "(MCI) %s: UNHALT BT GPM\n", __func__);
                ar9300_mci_send_coex_halt_bt_gpm(ah, AH_FALSE, AH_TRUE);
            }
            ar9300_mci_2g5g_switch(ah, AH_TRUE);
            break;

        case HAL_MCI_STATE_SET_BT_CAL_START:
            ahp->ah_mci_bt_state = MCI_BT_CAL_START;
            break;

        case HAL_MCI_STATE_SET_BT_CAL:
            ahp->ah_mci_bt_state = MCI_BT_CAL;
            break;

        case HAL_MCI_STATE_RESET_REQ_WAKE:
            ar9300_mci_reset_req_wakeup(ah);
            ahp->ah_mci_coex_2g5g_update = AH_TRUE;

            if ((AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) && 
                (ah->ah_config.ath_hal_mci_config & 
                 ATH_MCI_CONFIG_MCI_OBS_MASK))
            {
                /* Check if we still have control of the GPIOs */
                if ((OS_REG_READ(ah, AR_GLB_GPIO_CONTROL) & 
                     ATH_MCI_CONFIG_MCI_OBS_GPIO) != 
                     ATH_MCI_CONFIG_MCI_OBS_GPIO)
                 {
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                        "(MCI) Reconfigure observation\n");
                    ar9300_mci_observation_set_up(ah);
                 }
            }

            break;
            
        case HAL_MCI_STATE_SEND_WLAN_COEX_VERSION:
            ar9300_mci_send_coex_version_response(ah, AH_TRUE);
            break;

        case HAL_MCI_STATE_SET_BT_COEX_VERSION:
            if (p_data == NULL) {
                HALDEBUG(ah, HAL_DEBUG_BT_COEX,
                    "(MCI) Error: Set BT Coex version with NULL data !!!\n");
            }
            else {
                ahp->ah_mci_coex_major_version_bt = (*p_data >> 8) & 0xff;
                ahp->ah_mci_coex_minor_version_bt = (*p_data) & 0xff;
                ahp->ah_mci_coex_bt_version_known = AH_TRUE;
                HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) BT version set: %d.%d\n",
                        ahp->ah_mci_coex_major_version_bt,
                        ahp->ah_mci_coex_minor_version_bt);
            }
            break;

        case HAL_MCI_STATE_SEND_WLAN_CHANNELS:
            if (p_data != NULL)
            {
                if (((ahp->ah_mci_coex_wlan_channels[1] & 0xffff0000) ==
                    (*(p_data + 1) & 0xffff0000)) &&
                    (ahp->ah_mci_coex_wlan_channels[2] == *(p_data + 2)) &&
                    (ahp->ah_mci_coex_wlan_channels[3] == *(p_data + 3)))
                {
                    break;
                }
                ahp->ah_mci_coex_wlan_channels[0] = *p_data++;
                ahp->ah_mci_coex_wlan_channels[1] = *p_data++;
                ahp->ah_mci_coex_wlan_channels[2] = *p_data++;
                ahp->ah_mci_coex_wlan_channels[3] = *p_data++;
            }
            ahp->ah_mci_coex_wlan_channels_update = AH_TRUE;
            ar9300_mci_send_coex_wlan_channels(ah, AH_TRUE);
            break;

        case HAL_MCI_STATE_SEND_VERSION_QUERY:
            ar9300_mci_send_coex_version_query(ah, AH_TRUE);
            break;

        case HAL_MCI_STATE_SEND_STATUS_QUERY:
            if (AR_SREV_JUPITER_10(ah)) {
                ar9300_mci_send_coex_bt_status_query(ah, AH_TRUE,
                        MCI_GPM_COEX_QUERY_BT_ALL_INFO);
            } else {
                ar9300_mci_send_coex_bt_status_query(ah, AH_TRUE,
                        MCI_GPM_COEX_QUERY_BT_TOPOLOGY);
            }
            break;

        case HAL_MCI_STATE_NEED_FLUSH_BT_INFO:
            /*
             * ah_mci_unhalt_bt_gpm means whether it's needed to send
             * UNHALT message. It's set whenever there's a request to send HALT
             * message. ah_mci_halted_bt_gpm means whether HALT message is sent
             * out successfully.
             *
             * Checking (ah_mci_unhalt_bt_gpm == AH_FALSE) instead of checking
             * (ahp->ah_mci_halted_bt_gpm == AH_FALSE) will make sure currently is
             * in UNHALT-ed mode and BT can respond to status query.
             */
            if ((ahp->ah_mci_unhalt_bt_gpm == AH_FALSE) &&
                (ahp->ah_mci_need_flush_btinfo == AH_TRUE))
            {
                value = 1;
            }
            else {
                value = 0;
            }
            if (p_data != NULL) {
                ahp->ah_mci_need_flush_btinfo = (*p_data != 0)? AH_TRUE : AH_FALSE;
            }
            break;

        case HAL_MCI_STATE_SET_CONCUR_TX_PRI:
            if (p_data) {
                ahp->ah_mci_stomp_none_tx_pri = *p_data & 0xff;
                ahp->ah_mci_stomp_low_tx_pri = (*p_data >> 8) & 0xff;
                ahp->ah_mci_stomp_all_tx_pri = (*p_data >> 16) & 0xff;
            }
            break;

        case HAL_MCI_STATE_RECOVER_RX:
            HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) hal RECOVER_RX\n");
            ar9300_mci_prep_interface(ah);
            ahp->ah_mci_query_bt = AH_TRUE;
            ahp->ah_mci_need_flush_btinfo = AH_TRUE;
            ar9300_mci_send_coex_wlan_channels(ah, AH_TRUE);
            ar9300_mci_2g5g_switch(ah, AH_TRUE);
            break;
            
        case HAL_MCI_STATE_DEBUG:
            if (p_data != NULL) {
                if (*p_data == HAL_MCI_STATE_DEBUG_REQ_BT_DEBUG) {
                    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) QUERY_BT_DEBUG\n");
                    ar9300_mci_send_coex_bt_status_query(ah, AH_TRUE,
                        MCI_GPM_COEX_QUERY_BT_DEBUG);
                    OS_DELAY(10);
                    if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
                        ar9300_mci_send_coex_bt_flags(ah, AH_TRUE,
                            MCI_GPM_COEX_BT_FLAGS_READ, 0);
                    }
                }
            }
            break;

        case HAL_MCI_STATE_NEED_FTP_STOMP:
            value = (ah->ah_config.ath_hal_mci_config &
                     ATH_MCI_CONFIG_DISABLE_FTP_STOMP) ? 0 : 1;
            break;

        case HAL_MCI_STATE_NEED_TUNING:
            value = (ah->ah_config.ath_hal_mci_config &
                     ATH_MCI_CONFIG_DISABLE_TUNING) ? 0 : 1;
            break;

        case HAL_MCI_STATE_SHARED_CHAIN_CONCUR_TX:
            value = ((ah->ah_config.ath_hal_mci_config &
                     ATH_MCI_CONFIG_CONCUR_TX) == 
                     ATH_MCI_CONCUR_TX_SHARED_CHN)? 1 : 0;
            break;

        default:
            break;
    }
    return value;
}

void ar9300_mci_detach(struct ath_hal *ah)
{
    /* Turn off MCI and Jupiter mode. */
    OS_REG_WRITE(ah, AR_BTCOEX_CTRL, 0x00);
    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) ar9300_mci_detach\n");
    ar9300_mci_disable_interrupt(ah);
}

/*
 * Low priority BT: 0 - 59(0x3b)
 * High priority BT: 60 - 125(0x7d)
 * Critical BT: 126 - 255

    BTCOEX_WL_WEIGHTS0_VALUE0 ; // wl_idle                       
    BTCOEX_WL_WEIGHTS0_VALUE1 ; // sw_ctrl[3] - all_stomp        
    BTCOEX_WL_WEIGHTS0_VALUE2 ; // sw_ctrl[2] - all_not_stomp    
    BTCOEX_WL_WEIGHTS0_VALUE3 ; // sw_ctrl[1] - pa_pre_distortion
    BTCOEX_WL_WEIGHTS1_VALUE0 ; // sw_ctrl[0] - general purpose  
    BTCOEX_WL_WEIGHTS1_VALUE1 ; // tm_wl_wait_beacon             
    BTCOEX_WL_WEIGHTS1_VALUE2 ; // ts_state_wait_ack_cts         
    BTCOEX_WL_WEIGHTS1_VALUE3 ; // self_gen                      
    BTCOEX_WL_WEIGHTS2_VALUE0 ; // idle                          
    BTCOEX_WL_WEIGHTS2_VALUE1 ; // rx                            
    BTCOEX_WL_WEIGHTS2_VALUE2 ; // tx                            
    BTCOEX_WL_WEIGHTS2_VALUE3 ; // rx + tx                       
    BTCOEX_WL_WEIGHTS3_VALUE0 ; // tx                            
    BTCOEX_WL_WEIGHTS3_VALUE1 ; // rx                            
    BTCOEX_WL_WEIGHTS3_VALUE2 ; // tx                            
    BTCOEX_WL_WEIGHTS3_VALUE3 ; // rx + tx                       

    Stomp all:
    ah_bt_coex_wlan_weight[0] = 0x00007d00
    ah_bt_coex_wlan_weight[1] = 0x7d7d7d00
    ah_bt_coex_wlan_weight[2] = 0x7d7d7d00
    ah_bt_coex_wlan_weight[3] = 0x7d7d7d7d
    Stomp low:
    ah_bt_coex_wlan_weight[0] = 0x00007d00
    ah_bt_coex_wlan_weight[1] = 0x7d3b3b00
    ah_bt_coex_wlan_weight[2] = 0x3b3b3b00
    ah_bt_coex_wlan_weight[3] = 0x3b3b3b3b
    Stomp none:
    ah_bt_coex_wlan_weight[0] = 0x00007d00
    ah_bt_coex_wlan_weight[1] = 0x7d000000
    ah_bt_coex_wlan_weight[2] = 0x00000000
    ah_bt_coex_wlan_weight[3] = 0x00000000
*/

void ar9300_mci_bt_coex_set_weights(struct ath_hal *ah, u_int32_t stomp_type)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
//    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    u_int32_t tx_priority = 0;

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "%s: stomp_type=%d\n", __func__, stomp_type);

    switch (stomp_type) {
    case HAL_BT_COEX_STOMP_ALL:
        ahp->ah_bt_coex_wlan_weight[0] = JUPITER_STOMP_ALL_WLAN_WGHT0;
        ahp->ah_bt_coex_wlan_weight[1] = JUPITER_STOMP_ALL_WLAN_WGHT1;
        ahp->ah_bt_coex_wlan_weight[2] = JUPITER_STOMP_ALL_WLAN_WGHT2;
        ahp->ah_bt_coex_wlan_weight[3] = JUPITER_STOMP_ALL_WLAN_WGHT3;
        if (ahp->ah_mci_concur_tx_en && ahp->ah_mci_stomp_all_tx_pri) {
            tx_priority = ahp->ah_mci_stomp_all_tx_pri;
        }
        break;
    case HAL_BT_COEX_STOMP_LOW:
        if (ahp->ah_bt_coex_flag & HAL_BT_COEX_FLAG_MCI_FTP_STOMP_RX) {
            ahp->ah_bt_coex_wlan_weight[0] = JUPITER_STOMP_LOW_FTP_WLAN_WGHT0;
            ahp->ah_bt_coex_wlan_weight[1] = JUPITER_STOMP_LOW_FTP_WLAN_WGHT1;
            ahp->ah_bt_coex_wlan_weight[2] = JUPITER_STOMP_LOW_FTP_WLAN_WGHT2;
            ahp->ah_bt_coex_wlan_weight[3] = JUPITER_STOMP_LOW_FTP_WLAN_WGHT3;
        }
        else {
            ahp->ah_bt_coex_wlan_weight[0] = JUPITER_STOMP_LOW_WLAN_WGHT0;
            ahp->ah_bt_coex_wlan_weight[1] = JUPITER_STOMP_LOW_WLAN_WGHT1;
            ahp->ah_bt_coex_wlan_weight[2] = JUPITER_STOMP_LOW_WLAN_WGHT2;
            ahp->ah_bt_coex_wlan_weight[3] = JUPITER_STOMP_LOW_WLAN_WGHT3;
        }
        if (ahp->ah_mci_concur_tx_en && ahp->ah_mci_stomp_low_tx_pri) {
            tx_priority = ahp->ah_mci_stomp_low_tx_pri;
        }
        if (ah->ah_config.ath_hal_mci_config & 
            ATH_MCI_CONFIG_MCI_OBS_TXRX)
        {
            ar9300_gpio_set(ah, 5, 1);
        }
        break;
    case HAL_BT_COEX_STOMP_ALL_FORCE:
        ahp->ah_bt_coex_wlan_weight[0] = JUPITER_STOMP_ALL_FORCE_WLAN_WGHT0;
        ahp->ah_bt_coex_wlan_weight[1] = JUPITER_STOMP_ALL_FORCE_WLAN_WGHT1;
        ahp->ah_bt_coex_wlan_weight[2] = JUPITER_STOMP_ALL_FORCE_WLAN_WGHT2;
        ahp->ah_bt_coex_wlan_weight[3] = JUPITER_STOMP_ALL_FORCE_WLAN_WGHT3;
        break;
    case HAL_BT_COEX_STOMP_LOW_FORCE:
        ahp->ah_bt_coex_wlan_weight[0] = JUPITER_STOMP_LOW_FORCE_WLAN_WGHT0;
        ahp->ah_bt_coex_wlan_weight[1] = JUPITER_STOMP_LOW_FORCE_WLAN_WGHT1;
        ahp->ah_bt_coex_wlan_weight[2] = JUPITER_STOMP_LOW_FORCE_WLAN_WGHT2;
        ahp->ah_bt_coex_wlan_weight[3] = JUPITER_STOMP_LOW_FORCE_WLAN_WGHT3;
        if (ahp->ah_mci_concur_tx_en && ahp->ah_mci_stomp_low_tx_pri) {
            tx_priority = ahp->ah_mci_stomp_low_tx_pri;
        }
        break;
    case HAL_BT_COEX_STOMP_NONE:
    case HAL_BT_COEX_NO_STOMP:
        ahp->ah_bt_coex_wlan_weight[0] = JUPITER_STOMP_NONE_WLAN_WGHT0;
        ahp->ah_bt_coex_wlan_weight[1] = JUPITER_STOMP_NONE_WLAN_WGHT1;
        ahp->ah_bt_coex_wlan_weight[2] = JUPITER_STOMP_NONE_WLAN_WGHT2;
        ahp->ah_bt_coex_wlan_weight[3] = JUPITER_STOMP_NONE_WLAN_WGHT3;
        if (ahp->ah_mci_concur_tx_en && ahp->ah_mci_stomp_none_tx_pri) {
            tx_priority = ahp->ah_mci_stomp_none_tx_pri;
        }
        if (ah->ah_config.ath_hal_mci_config & 
            ATH_MCI_CONFIG_MCI_OBS_TXRX)
        {
            ar9300_gpio_set(ah, 5, 0);
        }
        break;
    case HAL_BT_COEX_STOMP_AUDIO:
        ahp->ah_bt_coex_wlan_weight[0] = 0xffffff01;
        ahp->ah_bt_coex_wlan_weight[1] = 0xffffffff;
        ahp->ah_bt_coex_wlan_weight[2] = 0xffffff01;
        ahp->ah_bt_coex_wlan_weight[3] = 0xffffffff;
        break;
    default:
        /* There is a forceWeight from registry */
        ahp->ah_bt_coex_wlan_weight[0] = stomp_type;
        ahp->ah_bt_coex_wlan_weight[1] = stomp_type;
        break;
    }

    if (ahp->ah_mci_concur_tx_en && tx_priority) {
        ahp->ah_bt_coex_wlan_weight[1] &= ~MCI_CONCUR_TX_WLAN_WGHT1_MASK;
        ahp->ah_bt_coex_wlan_weight[1] |= 
            SM(tx_priority, MCI_CONCUR_TX_WLAN_WGHT1_MASK);
        ahp->ah_bt_coex_wlan_weight[2] &= ~MCI_CONCUR_TX_WLAN_WGHT2_MASK;
        ahp->ah_bt_coex_wlan_weight[2] |= 
            SM(tx_priority, MCI_CONCUR_TX_WLAN_WGHT2_MASK);
        ahp->ah_bt_coex_wlan_weight[3] &= ~MCI_CONCUR_TX_WLAN_WGHT3_MASK;
        ahp->ah_bt_coex_wlan_weight[3] |= 
            SM(tx_priority, MCI_CONCUR_TX_WLAN_WGHT3_MASK);
        ahp->ah_bt_coex_wlan_weight[3] &= ~MCI_CONCUR_TX_WLAN_WGHT3_MASK2;
        ahp->ah_bt_coex_wlan_weight[3] |= 
            SM(tx_priority, MCI_CONCUR_TX_WLAN_WGHT3_MASK2);
    }
//    if (ah->ah_config.ath_hal_mci_config & 
//        ATH_MCI_CONFIG_MCI_WEIGHT_DBG)
//    {
        HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
                "(MCI) Set weights: 0x%08x 0x%08x 0x%08x 0x%08x\n",
                ahp->ah_bt_coex_wlan_weight[0],
                ahp->ah_bt_coex_wlan_weight[1],
                ahp->ah_bt_coex_wlan_weight[2],
                ahp->ah_bt_coex_wlan_weight[3]);
//    }
}

void ar9300_mci_bt_coex_disable(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
        "(MCI) %s: Set weight to stomp none.\n", __func__);

    ar9300_mci_bt_coex_set_weights(ah, HAL_BT_COEX_STOMP_NONE);

    /* 
     * In Jupiter, when coex is disabled, we just set weight
     * table to be in favor of WLAN.
     */
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS0, ahp->ah_bt_coex_wlan_weight[0]);
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS1, ahp->ah_bt_coex_wlan_weight[1]);
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS2, ahp->ah_bt_coex_wlan_weight[2]);
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS3, ahp->ah_bt_coex_wlan_weight[3]);

    ahp->ah_bt_coex_enabled = AH_FALSE;
}

int ar9300_mci_bt_coex_enable(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, "(MCI) %s: called\n", __func__);

    HALDEBUG(ah, HAL_DEBUG_BT_COEX, 
        "(MCI) Write weights: 0x%08x 0x%08x 0x%08x 0x%08x\n",
       ahp->ah_bt_coex_wlan_weight[0],
       ahp->ah_bt_coex_wlan_weight[1],
       ahp->ah_bt_coex_wlan_weight[2],
       ahp->ah_bt_coex_wlan_weight[3]);


    /* Mainly change the WLAN weight table */
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS0, ahp->ah_bt_coex_wlan_weight[0]);
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS1, ahp->ah_bt_coex_wlan_weight[1]);
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS2, ahp->ah_bt_coex_wlan_weight[2]);
    OS_REG_WRITE(ah, AR_BTCOEX_WL_WEIGHTS3, ahp->ah_bt_coex_wlan_weight[3]);

    /* Send ACK even when BT has higher priority. */
    OS_REG_RMW_FIELD(ah, AR_QUIET1, AR_QUIET1_QUIET_ACK_CTS_ENABLE, 1);

    if (ahp->ah_bt_coex_flag & HAL_BT_COEX_FLAG_LOW_ACK_PWR) {
        OS_REG_WRITE(ah, AR_TPC, HAL_BT_COEX_LOW_ACK_POWER);
    }
    else {
        OS_REG_WRITE(ah, AR_TPC, HAL_BT_COEX_HIGH_ACK_POWER);
    }

    ahp->ah_bt_coex_enabled = AH_TRUE;

    return 0;
}

#endif /* ATH_SUPPORT_MCI */
