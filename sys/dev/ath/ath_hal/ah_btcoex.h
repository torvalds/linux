/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
 * All Rights Reserved.
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
 *
 * $FreeBSD$
 */
#ifndef	__ATH_HAL_BTCOEX_H__
#define	__ATH_HAL_BTCOEX_H__

/*
 * General BT coexistence definitions.
 */
typedef enum {
	HAL_BT_MODULE_CSR_BC4	= 0,	/* CSR BlueCore v4 */
	HAL_BT_MODULE_JANUS	= 1,	/* Kite + Valkyrie combo */
	HAL_BT_MODULE_HELIUS	= 2,	/* Kiwi + Valkyrie combo */
	HAL_MAX_BT_MODULES
} HAL_BT_MODULE;

typedef struct {
	HAL_BT_MODULE	bt_module;
	u_int8_t	bt_coex_config;
	u_int8_t	bt_gpio_bt_active;
	u_int8_t	bt_gpio_bt_priority;
	u_int8_t	bt_gpio_wlan_active;
	u_int8_t	bt_active_polarity;
	HAL_BOOL	bt_single_ant;
	u_int8_t	bt_isolation;
} HAL_BT_COEX_INFO;

typedef enum {
	HAL_BT_COEX_MODE_LEGACY		= 0,	/* legacy rx_clear mode */
	HAL_BT_COEX_MODE_UNSLOTTED	= 1,	/* untimed/unslotted mode */
	HAL_BT_COEX_MODE_SLOTTED	= 2,	/* slotted mode */
	HAL_BT_COEX_MODE_DISALBED	= 3,	/* coexistence disabled */
} HAL_BT_COEX_MODE;

typedef enum {
	HAL_BT_COEX_CFG_NONE,		/* No bt coex enabled */
	HAL_BT_COEX_CFG_2WIRE_2CH,	/* 2-wire with 2 chains */
	HAL_BT_COEX_CFG_2WIRE_CH1,	/* 2-wire with ch1 */
	HAL_BT_COEX_CFG_2WIRE_CH0,	/* 2-wire with ch0 */
	HAL_BT_COEX_CFG_3WIRE,		/* 3-wire */
	HAL_BT_COEX_CFG_MCI		/* MCI */
} HAL_BT_COEX_CFG;

typedef enum {
	HAL_BT_COEX_SET_ACK_PWR		= 0,	/* Change ACK power setting */
	HAL_BT_COEX_LOWER_TX_PWR,		/* Change transmit power */
	HAL_BT_COEX_ANTENNA_DIVERSITY,	/* Enable RX diversity for Kite */
	HAL_BT_COEX_MCI_MAX_TX_PWR,	/* Set max tx power for concurrent tx */
	HAL_BT_COEX_MCI_FTP_STOMP_RX,	/* Use a different weight for stomp low */
} HAL_BT_COEX_SET_PARAMETER;

/*
 * MCI specific coexistence definitions.
 */

#define	HAL_BT_COEX_FLAG_LOW_ACK_PWR	0x00000001
#define	HAL_BT_COEX_FLAG_LOWER_TX_PWR	0x00000002
/* Check Rx Diversity is allowed */
#define	HAL_BT_COEX_FLAG_ANT_DIV_ALLOW	0x00000004
/* Check Diversity is on or off */
#define	HAL_BT_COEX_FLAG_ANT_DIV_ENABLE	0x00000008

#define	HAL_BT_COEX_ANTDIV_CONTROL1_ENABLE	0x0b
/* main: LNA1, alt: LNA2 */
#define	HAL_BT_COEX_ANTDIV_CONTROL2_ENABLE	0x09
#define	HAL_BT_COEX_ANTDIV_CONTROL1_FIXED_A	0x04
#define	HAL_BT_COEX_ANTDIV_CONTROL2_FIXED_A	0x09
#define	HAL_BT_COEX_ANTDIV_CONTROL1_FIXED_B	0x02
#define	HAL_BT_COEX_ANTDIV_CONTROL2_FIXED_B	0x06

#define	HAL_BT_COEX_ISOLATION_FOR_NO_COEX	30

#define	HAL_BT_COEX_ANT_DIV_SWITCH_COM	0x66666666

#define	HAL_BT_COEX_HELIUS_CHAINMASK	0x02

#define	HAL_BT_COEX_LOW_ACK_POWER	0x0
#define	HAL_BT_COEX_HIGH_ACK_POWER	0x3f3f3f

typedef enum {
	HAL_BT_COEX_NO_STOMP = 0,
	HAL_BT_COEX_STOMP_ALL,
	HAL_BT_COEX_STOMP_LOW,
	HAL_BT_COEX_STOMP_NONE,
	HAL_BT_COEX_STOMP_ALL_FORCE,
	HAL_BT_COEX_STOMP_LOW_FORCE,
	HAL_BT_COEX_STOMP_AUDIO,
} HAL_BT_COEX_STOMP_TYPE;

typedef struct {
	/* extend rx_clear after tx/rx to protect the burst (in usec). */
	u_int8_t	bt_time_extend;

	/*
	 * extend rx_clear as long as txsm is
	 * transmitting or waiting for ack.
	 */
	HAL_BOOL	bt_txstate_extend;

	/*
	 * extend rx_clear so that when tx_frame
	 * is asserted, rx_clear will drop.
	 */
	HAL_BOOL	bt_txframe_extend;

	/*
	 * coexistence mode
	 */
	HAL_BT_COEX_MODE	bt_mode;

	/*
	 * treat BT high priority traffic as
	 * a quiet collision
	 */
	HAL_BOOL	bt_quiet_collision;

	/*
	 * invert rx_clear as WLAN_ACTIVE
	 */
	HAL_BOOL	bt_rxclear_polarity;

	/*
	 * slotted mode only. indicate the time in usec
	 * from the rising edge of BT_ACTIVE to the time
	 * BT_PRIORITY can be sampled to indicate priority.
	 */
	u_int8_t	bt_priority_time;

	/*
	 * slotted mode only. indicate the time in usec
	 * from the rising edge of BT_ACTIVE to the time
	 * BT_PRIORITY can be sampled to indicate tx/rx and
	 * BT_FREQ is sampled.
	 */
	u_int8_t	bt_first_slot_time;

	/*
	 * slotted mode only. rx_clear and bt_ant decision
	 * will be held the entire time that BT_ACTIVE is asserted,
	 * otherwise the decision is made before every slot boundary.
	 */
	HAL_BOOL	bt_hold_rxclear;
} HAL_BT_COEX_CONFIG;

#define HAL_BT_COEX_FLAG_LOW_ACK_PWR        0x00000001
#define HAL_BT_COEX_FLAG_LOWER_TX_PWR       0x00000002
#define HAL_BT_COEX_FLAG_ANT_DIV_ALLOW      0x00000004    /* Check Rx Diversity is allowed */
#define HAL_BT_COEX_FLAG_ANT_DIV_ENABLE     0x00000008    /* Check Diversity is on or off */
#define HAL_BT_COEX_FLAG_MCI_MAX_TX_PWR     0x00000010
#define HAL_BT_COEX_FLAG_MCI_FTP_STOMP_RX   0x00000020

#define HAL_MCI_FLAG_DISABLE_TIMESTAMP      0x00000001      /* Disable time stamp */

typedef enum mci_message_header {
    MCI_LNA_CTRL     = 0x10,        /* len = 0 */
    MCI_CONT_NACK    = 0x20,        /* len = 0 */
    MCI_CONT_INFO    = 0x30,        /* len = 4 */
    MCI_CONT_RST     = 0x40,        /* len = 0 */
    MCI_SCHD_INFO    = 0x50,        /* len = 16 */
    MCI_CPU_INT      = 0x60,        /* len = 4 */
    MCI_SYS_WAKING   = 0x70,        /* len = 0 */
    MCI_GPM          = 0x80,        /* len = 16 */
    MCI_LNA_INFO     = 0x90,        /* len = 1 */
    MCI_LNA_STATE    = 0x94,
    MCI_LNA_TAKE     = 0x98,
    MCI_LNA_TRANS    = 0x9c,
    MCI_SYS_SLEEPING = 0xa0,        /* len = 0 */
    MCI_REQ_WAKE     = 0xc0,        /* len = 0 */
    MCI_DEBUG_16     = 0xfe,        /* len = 2 */
    MCI_REMOTE_RESET = 0xff         /* len = 16 */
} MCI_MESSAGE_HEADER;

/* Default remote BT device MCI COEX version */
#define MCI_GPM_COEX_MAJOR_VERSION_DEFAULT  3
#define MCI_GPM_COEX_MINOR_VERSION_DEFAULT  0
/* Local WLAN MCI COEX version */
#define MCI_GPM_COEX_MAJOR_VERSION_WLAN     3
#define MCI_GPM_COEX_MINOR_VERSION_WLAN     0

typedef enum mci_gpm_subtype {
    MCI_GPM_BT_CAL_REQ      = 0,
    MCI_GPM_BT_CAL_GRANT    = 1,
    MCI_GPM_BT_CAL_DONE     = 2,
    MCI_GPM_WLAN_CAL_REQ    = 3,
    MCI_GPM_WLAN_CAL_GRANT  = 4,
    MCI_GPM_WLAN_CAL_DONE   = 5,
    MCI_GPM_COEX_AGENT      = 0x0C,
    MCI_GPM_RSVD_PATTERN    = 0xFE,
    MCI_GPM_RSVD_PATTERN32  = 0xFEFEFEFE,
    MCI_GPM_BT_DEBUG        = 0xFF
} MCI_GPM_SUBTYPE_T;

typedef enum mci_gpm_coex_opcode {
    MCI_GPM_COEX_VERSION_QUERY      = 0,
    MCI_GPM_COEX_VERSION_RESPONSE   = 1,
    MCI_GPM_COEX_STATUS_QUERY       = 2,
    MCI_GPM_COEX_HALT_BT_GPM        = 3,
    MCI_GPM_COEX_WLAN_CHANNELS      = 4,
    MCI_GPM_COEX_BT_PROFILE_INFO    = 5,
    MCI_GPM_COEX_BT_STATUS_UPDATE   = 6,
    MCI_GPM_COEX_BT_UPDATE_FLAGS    = 7
} MCI_GPM_COEX_OPCODE_T;

typedef enum mci_gpm_coex_query_type {
    /* WLAN information */
    MCI_GPM_COEX_QUERY_WLAN_ALL_INFO    = 0x01,
    /* BT information */
    MCI_GPM_COEX_QUERY_BT_ALL_INFO      = 0x01,
    MCI_GPM_COEX_QUERY_BT_TOPOLOGY      = 0x02,
    MCI_GPM_COEX_QUERY_BT_DEBUG         = 0x04
} MCI_GPM_COEX_QUERY_TYPE_T;

typedef enum mci_gpm_coex_halt_bt_gpm {
    MCI_GPM_COEX_BT_GPM_UNHALT      = 0,
    MCI_GPM_COEX_BT_GPM_HALT        = 1
} MCI_GPM_COEX_HALT_BT_GPM_T;

typedef enum mci_gpm_coex_profile_type {
    MCI_GPM_COEX_PROFILE_UNKNOWN    = 0,
    MCI_GPM_COEX_PROFILE_RFCOMM     = 1,
    MCI_GPM_COEX_PROFILE_A2DP       = 2,
    MCI_GPM_COEX_PROFILE_HID        = 3,
    MCI_GPM_COEX_PROFILE_BNEP       = 4,
    MCI_GPM_COEX_PROFILE_VOICE      = 5,
    MCI_GPM_COEX_PROFILE_MAX
} MCI_GPM_COEX_PROFILE_TYPE_T;

typedef enum mci_gpm_coex_profile_state {
    MCI_GPM_COEX_PROFILE_STATE_END      = 0,
    MCI_GPM_COEX_PROFILE_STATE_START    = 1
} MCI_GPM_COEX_PROFILE_STATE_T;

typedef enum mci_gpm_coex_profile_role {
    MCI_GPM_COEX_PROFILE_SLAVE      = 0,
    MCI_GPM_COEX_PROFILE_MASTER     = 1
} MCI_GPM_COEX_PROFILE_ROLE_T;

typedef enum mci_gpm_coex_bt_status_type {
    MCI_GPM_COEX_BT_NONLINK_STATUS  = 0,
    MCI_GPM_COEX_BT_LINK_STATUS     = 1
} MCI_GPM_COEX_BT_STATUS_TYPE_T;

typedef enum mci_gpm_coex_bt_status_state {
    MCI_GPM_COEX_BT_NORMAL_STATUS   = 0,
    MCI_GPM_COEX_BT_CRITICAL_STATUS = 1
} MCI_GPM_COEX_BT_STATUS_STATE_T;

#define MCI_GPM_INVALID_PROFILE_HANDLE  0xff

typedef enum mci_gpm_coex_bt_updata_flags_op {
    MCI_GPM_COEX_BT_FLAGS_READ          = 0x00,
    MCI_GPM_COEX_BT_FLAGS_SET           = 0x01,
    MCI_GPM_COEX_BT_FLAGS_CLEAR         = 0x02
} MCI_GPM_COEX_BT_FLAGS_OP_T;

/* MCI GPM/Coex opcode/type definitions */
enum {
    MCI_GPM_COEX_W_GPM_PAYLOAD      = 1,
    MCI_GPM_COEX_B_GPM_TYPE         = 4,
    MCI_GPM_COEX_B_GPM_OPCODE       = 5,
    /* MCI_GPM_WLAN_CAL_REQ, MCI_GPM_WLAN_CAL_DONE */
    MCI_GPM_WLAN_CAL_W_SEQUENCE     = 2,
    /* MCI_GPM_COEX_VERSION_QUERY */
    /* MCI_GPM_COEX_VERSION_RESPONSE */
    MCI_GPM_COEX_B_MAJOR_VERSION    = 6,
    MCI_GPM_COEX_B_MINOR_VERSION    = 7,
    /* MCI_GPM_COEX_STATUS_QUERY */
    MCI_GPM_COEX_B_BT_BITMAP        = 6,
    MCI_GPM_COEX_B_WLAN_BITMAP      = 7,
    /* MCI_GPM_COEX_HALT_BT_GPM */
    MCI_GPM_COEX_B_HALT_STATE       = 6,
    /* MCI_GPM_COEX_WLAN_CHANNELS */
    MCI_GPM_COEX_B_CHANNEL_MAP      = 6,
    /* MCI_GPM_COEX_BT_PROFILE_INFO */
    MCI_GPM_COEX_B_PROFILE_TYPE     = 6,
    MCI_GPM_COEX_B_PROFILE_LINKID   = 7,
    MCI_GPM_COEX_B_PROFILE_STATE    = 8,
    MCI_GPM_COEX_B_PROFILE_ROLE     = 9,
    MCI_GPM_COEX_B_PROFILE_RATE     = 10,
    MCI_GPM_COEX_B_PROFILE_VOTYPE   = 11,
    MCI_GPM_COEX_H_PROFILE_T        = 12,
    MCI_GPM_COEX_B_PROFILE_W        = 14,
    MCI_GPM_COEX_B_PROFILE_A        = 15,
    /* MCI_GPM_COEX_BT_STATUS_UPDATE */
    MCI_GPM_COEX_B_STATUS_TYPE      = 6,
    MCI_GPM_COEX_B_STATUS_LINKID    = 7,
    MCI_GPM_COEX_B_STATUS_STATE     = 8,
    /* MCI_GPM_COEX_BT_UPDATE_FLAGS */
    MCI_GPM_COEX_B_BT_FLAGS_OP      = 10,
    MCI_GPM_COEX_W_BT_FLAGS         = 6
};

#define MCI_GPM_RECYCLE(_p_gpm) \
    {                           \
        *(((u_int32_t *)(_p_gpm)) + MCI_GPM_COEX_W_GPM_PAYLOAD) = MCI_GPM_RSVD_PATTERN32; \
    }
#define MCI_GPM_TYPE(_p_gpm)    \
    (*(((u_int8_t *)(_p_gpm)) + MCI_GPM_COEX_B_GPM_TYPE) & 0xff)
#define MCI_GPM_OPCODE(_p_gpm)  \
    (*(((u_int8_t *)(_p_gpm)) + MCI_GPM_COEX_B_GPM_OPCODE) & 0xff)

#define MCI_GPM_SET_CAL_TYPE(_p_gpm, _cal_type)             \
    {                                                       \
        *(((u_int8_t *)(_p_gpm)) + MCI_GPM_COEX_B_GPM_TYPE) = (_cal_type) & 0xff; \
    }
#define MCI_GPM_SET_TYPE_OPCODE(_p_gpm, _type, _opcode)     \
    {                                                       \
        *(((u_int8_t *)(_p_gpm)) + MCI_GPM_COEX_B_GPM_TYPE) = (_type) & 0xff;     \
        *(((u_int8_t *)(_p_gpm)) + MCI_GPM_COEX_B_GPM_OPCODE) = (_opcode) & 0xff;   \
    }
#define MCI_GPM_IS_CAL_TYPE(_type) ((_type) <= MCI_GPM_WLAN_CAL_DONE)

#define MCI_NUM_BT_CHANNELS     79

#define MCI_GPM_SET_CHANNEL_BIT(_p_gpm, _bt_chan)                   \
    {                                                               \
        if (_bt_chan < MCI_NUM_BT_CHANNELS) {                       \
            *(((u_int8_t *)(_p_gpm)) + MCI_GPM_COEX_B_CHANNEL_MAP + \
                (_bt_chan / 8)) |= 1 << (_bt_chan & 7);             \
        }                                                           \
    }

#define MCI_GPM_CLR_CHANNEL_BIT(_p_gpm, _bt_chan)                   \
    {                                                               \
        if (_bt_chan < MCI_NUM_BT_CHANNELS) {                       \
            *(((u_int8_t *)(_p_gpm)) + MCI_GPM_COEX_B_CHANNEL_MAP + \
                (_bt_chan / 8)) &= ~(1 << (_bt_chan & 7));          \
        }                                                           \
    }

#define HAL_MCI_INTERRUPT_SW_MSG_DONE            0x00000001
#define HAL_MCI_INTERRUPT_CPU_INT_MSG            0x00000002
#define HAL_MCI_INTERRUPT_RX_CHKSUM_FAIL         0x00000004
#define HAL_MCI_INTERRUPT_RX_INVALID_HDR         0x00000008
#define HAL_MCI_INTERRUPT_RX_HW_MSG_FAIL         0x00000010
#define HAL_MCI_INTERRUPT_RX_SW_MSG_FAIL         0x00000020
#define HAL_MCI_INTERRUPT_TX_HW_MSG_FAIL         0x00000080
#define HAL_MCI_INTERRUPT_TX_SW_MSG_FAIL         0x00000100
#define HAL_MCI_INTERRUPT_RX_MSG                 0x00000200
#define HAL_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE    0x00000400
#define HAL_MCI_INTERRUPT_CONT_INFO_TIMEOUT      0x80000000
#define HAL_MCI_INTERRUPT_MSG_FAIL_MASK ( HAL_MCI_INTERRUPT_RX_HW_MSG_FAIL | \
                                          HAL_MCI_INTERRUPT_RX_SW_MSG_FAIL | \
                                          HAL_MCI_INTERRUPT_TX_HW_MSG_FAIL | \
                                          HAL_MCI_INTERRUPT_TX_SW_MSG_FAIL )

#define HAL_MCI_INTERRUPT_RX_MSG_REMOTE_RESET    0x00000001
#define HAL_MCI_INTERRUPT_RX_MSG_LNA_CONTROL     0x00000002
#define HAL_MCI_INTERRUPT_RX_MSG_CONT_NACK       0x00000004
#define HAL_MCI_INTERRUPT_RX_MSG_CONT_INFO       0x00000008
#define HAL_MCI_INTERRUPT_RX_MSG_CONT_RST        0x00000010
#define HAL_MCI_INTERRUPT_RX_MSG_SCHD_INFO       0x00000020
#define HAL_MCI_INTERRUPT_RX_MSG_CPU_INT         0x00000040
#define HAL_MCI_INTERRUPT_RX_MSG_GPM             0x00000100
#define HAL_MCI_INTERRUPT_RX_MSG_LNA_INFO        0x00000200
#define HAL_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING    0x00000400
#define HAL_MCI_INTERRUPT_RX_MSG_SYS_WAKING      0x00000800
#define HAL_MCI_INTERRUPT_RX_MSG_REQ_WAKE        0x00001000
#define HAL_MCI_INTERRUPT_RX_MSG_MONITOR         (HAL_MCI_INTERRUPT_RX_MSG_LNA_CONTROL | \
                                                  HAL_MCI_INTERRUPT_RX_MSG_LNA_INFO    | \
                                                  HAL_MCI_INTERRUPT_RX_MSG_CONT_NACK   | \
                                                  HAL_MCI_INTERRUPT_RX_MSG_CONT_INFO   | \
                                                  HAL_MCI_INTERRUPT_RX_MSG_CONT_RST)

typedef enum mci_bt_state {
    MCI_BT_SLEEP,
    MCI_BT_AWAKE,
    MCI_BT_CAL_START,
    MCI_BT_CAL
} MCI_BT_STATE_T;

/* Type of state query */
typedef enum mci_state_type {
    HAL_MCI_STATE_ENABLE,
    HAL_MCI_STATE_INIT_GPM_OFFSET,
    HAL_MCI_STATE_NEXT_GPM_OFFSET,
    HAL_MCI_STATE_LAST_GPM_OFFSET,
    HAL_MCI_STATE_BT,
    HAL_MCI_STATE_SET_BT_SLEEP,
    HAL_MCI_STATE_SET_BT_AWAKE,
    HAL_MCI_STATE_SET_BT_CAL_START,
    HAL_MCI_STATE_SET_BT_CAL,
    HAL_MCI_STATE_LAST_SCHD_MSG_OFFSET,
    HAL_MCI_STATE_REMOTE_SLEEP,
    HAL_MCI_STATE_CONT_RSSI_POWER,
    HAL_MCI_STATE_CONT_PRIORITY,
    HAL_MCI_STATE_CONT_TXRX,
    HAL_MCI_STATE_RESET_REQ_WAKE,
    HAL_MCI_STATE_SEND_WLAN_COEX_VERSION,
    HAL_MCI_STATE_SET_BT_COEX_VERSION,
    HAL_MCI_STATE_SEND_WLAN_CHANNELS,
    HAL_MCI_STATE_SEND_VERSION_QUERY,
    HAL_MCI_STATE_SEND_STATUS_QUERY,
    HAL_MCI_STATE_NEED_FLUSH_BT_INFO,
    HAL_MCI_STATE_SET_CONCUR_TX_PRI,
    HAL_MCI_STATE_RECOVER_RX,
    HAL_MCI_STATE_NEED_FTP_STOMP,
    HAL_MCI_STATE_NEED_TUNING,
    HAL_MCI_STATE_SHARED_CHAIN_CONCUR_TX,
    HAL_MCI_STATE_DEBUG,
    HAL_MCI_STATE_MAX
} HAL_MCI_STATE_TYPE;

#define HAL_MCI_STATE_DEBUG_REQ_BT_DEBUG    1

#define HAL_MCI_BT_MCI_FLAGS_UPDATE_CORR          0x00000002
#define HAL_MCI_BT_MCI_FLAGS_UPDATE_HDR           0x00000004
#define HAL_MCI_BT_MCI_FLAGS_UPDATE_PLD           0x00000008
#define HAL_MCI_BT_MCI_FLAGS_LNA_CTRL             0x00000010
#define HAL_MCI_BT_MCI_FLAGS_DEBUG                0x00000020
#define HAL_MCI_BT_MCI_FLAGS_SCHED_MSG            0x00000040
#define HAL_MCI_BT_MCI_FLAGS_CONT_MSG             0x00000080
#define HAL_MCI_BT_MCI_FLAGS_COEX_GPM             0x00000100
#define HAL_MCI_BT_MCI_FLAGS_CPU_INT_MSG          0x00000200
#define HAL_MCI_BT_MCI_FLAGS_MCI_MODE             0x00000400
#define HAL_MCI_BT_MCI_FLAGS_EGRET_MODE           0x00000800
#define HAL_MCI_BT_MCI_FLAGS_JUPITER_MODE         0x00001000
#define HAL_MCI_BT_MCI_FLAGS_OTHER                0x00010000

#define HAL_MCI_DEFAULT_BT_MCI_FLAGS        0x00011dde
/*
    HAL_MCI_BT_MCI_FLAGS_UPDATE_CORR  = 1
    HAL_MCI_BT_MCI_FLAGS_UPDATE_HDR   = 1
    HAL_MCI_BT_MCI_FLAGS_UPDATE_PLD   = 1
    HAL_MCI_BT_MCI_FLAGS_LNA_CTRL     = 1
    HAL_MCI_BT_MCI_FLAGS_DEBUG        = 0
    HAL_MCI_BT_MCI_FLAGS_SCHED_MSG    = 1
    HAL_MCI_BT_MCI_FLAGS_CONT_MSG     = 1
    HAL_MCI_BT_MCI_FLAGS_COEX_GPM     = 1
    HAL_MCI_BT_MCI_FLAGS_CPU_INT_MSG  = 0
    HAL_MCI_BT_MCI_FLAGS_MCI_MODE     = 1
    HAL_MCI_BT_MCI_FLAGS_EGRET_MODE   = 1
    HAL_MCI_BT_MCI_FLAGS_JUPITER_MODE = 1
    HAL_MCI_BT_MCI_FLAGS_OTHER        = 1
*/

#define HAL_MCI_TOGGLE_BT_MCI_FLAGS \
    (   HAL_MCI_BT_MCI_FLAGS_UPDATE_CORR    |   \
        HAL_MCI_BT_MCI_FLAGS_UPDATE_HDR     |   \
        HAL_MCI_BT_MCI_FLAGS_UPDATE_PLD     |   \
        HAL_MCI_BT_MCI_FLAGS_MCI_MODE   )

#define HAL_MCI_2G_FLAGS_CLEAR_MASK         0x00000000
#define HAL_MCI_2G_FLAGS_SET_MASK           HAL_MCI_TOGGLE_BT_MCI_FLAGS
#define HAL_MCI_2G_FLAGS                    HAL_MCI_DEFAULT_BT_MCI_FLAGS

#define HAL_MCI_5G_FLAGS_CLEAR_MASK         HAL_MCI_TOGGLE_BT_MCI_FLAGS
#define HAL_MCI_5G_FLAGS_SET_MASK           0x00000000
#define HAL_MCI_5G_FLAGS                    (HAL_MCI_DEFAULT_BT_MCI_FLAGS & \
                                            ~HAL_MCI_TOGGLE_BT_MCI_FLAGS)
    
#define HAL_MCI_GPM_NOMORE  0
#define HAL_MCI_GPM_MORE    1
#define HAL_MCI_GPM_INVALID 0xffffffff    

#define ATH_AIC_MAX_BT_CHANNEL          79

/*
 * Default value for Jupiter   is 0x00002201
 * Default value for Aphrodite is 0x00002282
 */
#define ATH_MCI_CONFIG_CONCUR_TX            0x00000003
#define ATH_MCI_CONFIG_MCI_OBS_MCI          0x00000004
#define ATH_MCI_CONFIG_MCI_OBS_TXRX         0x00000008
#define ATH_MCI_CONFIG_MCI_OBS_BT           0x00000010
#define ATH_MCI_CONFIG_DISABLE_MCI_CAL      0x00000020
#define ATH_MCI_CONFIG_DISABLE_OSLA         0x00000040
#define ATH_MCI_CONFIG_DISABLE_FTP_STOMP    0x00000080
#define ATH_MCI_CONFIG_AGGR_THRESH          0x00000700
#define ATH_MCI_CONFIG_AGGR_THRESH_S        8
#define ATH_MCI_CONFIG_DISABLE_AGGR_THRESH  0x00000800
#define ATH_MCI_CONFIG_CLK_DIV              0x00003000
#define ATH_MCI_CONFIG_CLK_DIV_S            12
#define ATH_MCI_CONFIG_DISABLE_TUNING       0x00004000
#define ATH_MCI_CONFIG_DISABLE_AIC          0x00008000
#define ATH_MCI_CONFIG_AIC_CAL_NUM_CHAN     0x007f0000
#define ATH_MCI_CONFIG_AIC_CAL_NUM_CHAN_S   16
#define ATH_MCI_CONFIG_NO_QUIET_ACK         0x00800000
#define ATH_MCI_CONFIG_NO_QUIET_ACK_S       23
#define ATH_MCI_CONFIG_ANT_ARCH             0x07000000
#define ATH_MCI_CONFIG_ANT_ARCH_S           24
#define ATH_MCI_CONFIG_FORCE_QUIET_ACK      0x08000000
#define ATH_MCI_CONFIG_FORCE_QUIET_ACK_S    27
#define ATH_MCI_CONFIG_FORCE_2CHAIN_ACK     0x10000000
#define ATH_MCI_CONFIG_MCI_STAT_DBG         0x20000000
#define ATH_MCI_CONFIG_MCI_WEIGHT_DBG       0x40000000
#define ATH_MCI_CONFIG_DISABLE_MCI          0x80000000

#define ATH_MCI_CONFIG_MCI_OBS_MASK     ( ATH_MCI_CONFIG_MCI_OBS_MCI | \
                                          ATH_MCI_CONFIG_MCI_OBS_TXRX | \
                                          ATH_MCI_CONFIG_MCI_OBS_BT )
#define ATH_MCI_CONFIG_MCI_OBS_GPIO     0x0000002F

#define ATH_MCI_ANT_ARCH_1_ANT_PA_LNA_NON_SHARED 0x00
#define ATH_MCI_ANT_ARCH_1_ANT_PA_LNA_SHARED     0x01
#define ATH_MCI_ANT_ARCH_2_ANT_PA_LNA_NON_SHARED 0x02
#define ATH_MCI_ANT_ARCH_2_ANT_PA_LNA_SHARED     0x03
#define ATH_MCI_ANT_ARCH_3_ANT                   0x04

#define	MCI_ANT_ARCH_PA_LNA_SHARED(c)		\
	    ((MS(c, ATH_MCI_CONFIG_ANT_ARCH) == ATH_MCI_ANT_ARCH_1_ANT_PA_LNA_SHARED) || \
	    (MS(c, ATH_MCI_CONFIG_ANT_ARCH) == ATH_MCI_ANT_ARCH_2_ANT_PA_LNA_SHARED))

#define ATH_MCI_CONCUR_TX_SHARED_CHN    0x01
#define ATH_MCI_CONCUR_TX_UNSHARED_CHN  0x02
#define ATH_MCI_CONCUR_TX_DEBUG         0x03

#endif
