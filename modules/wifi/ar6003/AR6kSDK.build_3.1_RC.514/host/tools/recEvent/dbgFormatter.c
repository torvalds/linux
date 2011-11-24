//
// Copyright (c) 2006 Atheros Communications Inc.
// All rights reserved.
// 
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
// 
//

/* This tool parses the recevent logs stored in the binary format 
   by the wince athsrc */
#define WAPI_ENABLE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <a_config.h>
#include <a_osapi.h>
#include <a_types.h>
#include <athdefs.h>
#include <ieee80211.h>
#include <wmi.h>
#include <athdrv_linux.h>
#include <dbglog_api.h>
#include <dbglog_id.h>

typedef int (*DbgCmdFormatter)(char *output, size_t len, const A_UINT8 *);
typedef int (*DbgLogFormatter)(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer);

struct dbglog_desc {
    A_INT32 oid;
    const char *desc;
    DbgLogFormatter formatter;
};

struct module_desc {
    int module;
    const char *name;
    struct dbglog_desc *descs;
    size_t len;
    int bsearch;
};

struct wmi_id_desc {
    A_INT32 oid;
    const char *desc;
    DbgCmdFormatter formatter;
    size_t cmdSize;
};

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#define CASE_STR_CONST(_s, _n, _val) case _n: (_val) = #_s; break
#define CASE_STR(_x, _val) case _x: (_val) = #_x; break
#define CASE_DEF_STR(_val) default: (_val) = "Unknown"; break
#define BIT_STR(_b, _l, _r, _s, _c, _v) if (((_v) & (_c)) == (_c)) (_r) += snprintf((_b)+(_r), (_l)-(_r), _s)

#define DBG_DESC(_x) { _x, #_x, NULL }
#define DBG_DESC_FMT(_x) { _x, #_x, _x ## _fmt }

#define WM_ID_DESC(_x) { _x, #_x, NULL, 0 }
#define WM_ID_DESC_FMT(_x, s) { _x, #_x, _x ## _fmt, s }

#define MODULE_DESC(_x, _d) { DBGLOG_MODULEID_ ## _x, #_x, _d, ARRAY_SIZE(_d),0}

extern A_CHAR dbglog_id_tag[DBGLOG_MODULEID_NUM_MAX][DBGLOG_DBGID_NUM_MAX][DBGLOG_DBGID_DEFINITION_LEN_MAX];
static int check_ids;

static const char *pmmode2text(A_INT32 mode)
{
    const char *pm;
    switch (mode) {
    case 1: pm = "sleep"; break;
    case 2: pm = "awake"; break;
    case 3: pm = "fake_sleep"; break;
    default: pm = "unknown"; break;
    }
    return pm;
}

#if 0
static const char *module2text(A_INT32 moduleid)
{
    const char *msg;
    switch (moduleid) {
    CASE_STR_CONST(INF, DBGLOG_MODULEID_INF, msg);
    CASE_STR_CONST(WMI, DBGLOG_MODULEID_WMI, msg);
    CASE_STR_CONST(MISC, DBGLOG_MODULEID_MISC, msg);
    CASE_STR_CONST(PM, DBGLOG_MODULEID_PM, msg);
    CASE_STR_CONST(MGMTBUF, DBGLOG_MODULEID_TXRX_MGMTBUF, msg);
    CASE_STR_CONST(TXBUF, DBGLOG_MODULEID_TXRX_TXBUF, msg);
    CASE_STR_CONST(RXBUF, DBGLOG_MODULEID_TXRX_RXBUF, msg);
    CASE_STR_CONST(WOW, DBGLOG_MODULEID_WOW, msg);
    CASE_STR_CONST(WHAL, DBGLOG_MODULEID_WHAL, msg);
    CASE_STR_CONST(DC, DBGLOG_MODULEID_DC, msg);
    CASE_STR_CONST(CO, DBGLOG_MODULEID_CO, msg);
    CASE_STR_CONST(RO, DBGLOG_MODULEID_RO, msg);
    CASE_STR_CONST(CM, DBGLOG_MODULEID_CM, msg);
    CASE_STR_CONST(MGMT, DBGLOG_MODULEID_MGMT, msg);
    CASE_STR_CONST(TMR, DBGLOG_MODULEID_TMR, msg);
    CASE_STR_CONST(BTCOEX, DBGLOG_MODULEID_BTCOEX, msg);
    CASE_DEF_STR(msg);
    }
    return msg;
}
#endif

static const char *pmmodule2text(A_INT32 moduleid)
{
    const char *msg;
    switch (moduleid) {
    CASE_STR_CONST(CSERV, 1, msg);
    CASE_STR_CONST(MLME   , 2, msg);
    CASE_STR_CONST(TXRX   , 3, msg);
    CASE_STR_CONST(PM     , 4, msg);
    CASE_STR_CONST(BT_COEX, 5, msg);
    CASE_STR_CONST(CO     , 6, msg);
    CASE_STR_CONST(DC     , 7, msg);
    CASE_STR_CONST(RO     , 8, msg);
    CASE_STR_CONST(CM     , 9, msg);
    CASE_STR_CONST(RRM    , 10, msg);
    CASE_STR_CONST(AP     , 11, msg);
    CASE_STR_CONST(KEYMGMT, 12, msg);
    CASE_STR_CONST(CCX    , 13, msg);
    CASE_STR_CONST(MISC   , 14, msg);
    CASE_STR_CONST(DFS    , 15, msg);
    CASE_STR_CONST(TIMER  , 16, msg);
    CASE_DEF_STR(msg);
    }
    return msg;
}

static const char *status2text(A_STATUS status)
{
    const char *msg;
    switch (status) {
    CASE_STR(A_ERROR, msg);
    CASE_STR(A_OK, msg);                   /* success */
                                /* Following values start at 1 */
    CASE_STR(A_DEVICE_NOT_FOUND, msg);         /* not able to find PCI device */
    CASE_STR(A_NO_MEMORY, msg);                /* not able to allocate memory, msg); not available */
    CASE_STR(A_MEMORY_NOT_AVAIL, msg);         /* memory region is not free for mapping */
    CASE_STR(A_NO_FREE_DESC, msg);             /* no free descriptors available */
    CASE_STR(A_BAD_ADDRESS, msg);              /* address does not match descriptor */
    CASE_STR(A_WIN_DRIVER_ERROR, msg);         /* used in NT_HW version, msg); if problem at init */
    CASE_STR(A_REGS_NOT_MAPPED, msg);          /* registers not correctly mapped */
    CASE_STR(A_EPERM, msg);                    /* Not superuser */
    CASE_STR(A_EACCES, msg);                   /* Access denied */
    CASE_STR(A_ENOENT, msg);                   /* No such entry, msg); search failed, msg); etc. */
    CASE_STR(A_EEXIST, msg);                   /* The object already exists (can't create) */
    CASE_STR(A_EFAULT, msg);                   /* Bad address fault */
    CASE_STR(A_EBUSY, msg);                    /* Object is busy */
    CASE_STR(A_EINVAL, msg);                   /* Invalid parameter */
    CASE_STR(A_EMSGSIZE, msg);                 /* Inappropriate message buffer length */
    CASE_STR(A_ECANCELED, msg);                /* Operation canceled */
    CASE_STR(A_ENOTSUP, msg);                  /* Operation not supported */
    CASE_STR(A_ECOMM, msg);                    /* Communication error on send */
    CASE_STR(A_EPROTO, msg);                   /* Protocol error */
    CASE_STR(A_ENODEV, msg);                   /* No such device */
    CASE_STR(A_EDEVNOTUP, msg);                /* device is not UP */
    CASE_STR(A_NO_RESOURCE, msg);              /* No resources for requested operation */
    CASE_STR(A_HARDWARE, msg);                 /* Hardware failure */
    CASE_STR(A_PENDING, msg);                  /* Asynchronous routine; will send up results la
ter (typically in callback) */
    CASE_STR(A_EBADCHANNEL, msg);              /* The channel cannot be used */
    CASE_STR(A_DECRYPT_ERROR, msg);            /* Decryption error */
    CASE_STR(A_PHY_ERROR, msg);                /* RX PHY error */
    CASE_STR(A_CONSUMED, msg);                    /* Object was consumed */
    CASE_DEF_STR(msg);
    }
    return msg;
}

static const char *btStatus2text(A_INT32 status)
{
    const char *btState;
    switch (status) {
    CASE_STR_CONST(BT_UNDEF, 1, btState);
    CASE_STR_CONST(BT_ON, 2, btState);
    CASE_STR_CONST(BT_OFF, 3, btState); 
    CASE_STR_CONST(BT_IGNORE, 4, btState);    
    CASE_DEF_STR(btState);
    }
    return btState;
}

static int cipher2text(char *buf, size_t len, A_UINT8 cipher)
{
    int ret = 0;
    BIT_STR(buf, len, ret, "none ", NONE_CRYPT, cipher);
    BIT_STR(buf, len, ret, "wep ", WEP_CRYPT, cipher);
    BIT_STR(buf, len, ret, "tkip ", TKIP_CRYPT, cipher);
    BIT_STR(buf, len, ret, "aes ", AES_CRYPT, cipher);
    buf[(ret>0) ? --ret : ret] = 0;
    return ret;
}

static const char* enable2text(A_INT32 enable)
{
    return enable ? "ENABLE" : "DISABLE";
}

static const char* txrxstatus2text(A_INT32 status)
{
    const char *txstatus;
    switch (status) {
    CASE_STR_CONST(TXRX_ERROR, -1, txstatus);
    CASE_STR_CONST(TXRX_OK                 ,  0, txstatus);
    CASE_STR_CONST(TXRX_FAILED             ,  1, txstatus);
    CASE_STR_CONST(TXRX_Q_IS_EMPTY         ,  2, txstatus);
    CASE_STR_CONST(TXRX_Q_NOT_DRAINED      ,  3, txstatus);
    CASE_STR_CONST(TXRX_Q_DRAINED_ALL      ,  4, txstatus);
    CASE_STR_CONST(TXRX_Q_DRAIN_PENDING    ,  5, txstatus);
    CASE_STR_CONST(TXRX_IS_MGMT_PKT        ,  6, txstatus);
    CASE_STR_CONST(TXRX_IS_DATA_PKT        ,  7, txstatus);
    CASE_STR_CONST(TXRX_SAVED_AS_FRAG      ,  8, txstatus);
    CASE_STR_CONST(TXRX_Q_IS_PAUSED        ,  9, txstatus);
    CASE_STR_CONST(TXRX_RX_SEND_TO_HOST    , 10, txstatus);
    CASE_STR_CONST(TXRX_ERROR_PKT_DRAINED  , 11, txstatus);
    CASE_STR_CONST(TXRX_EOL_EXPIRED        , 12, txstatus);
    CASE_STR_CONST(TXRX_PS_FILTERED        , 13,  txstatus);
    CASE_STR_CONST(TXRX_RX_SEND_TO_TX      , 14,  txstatus);
    CASE_DEF_STR(txstatus);
    }
    return txstatus;
}

static int rxfilter2text(char *buf, size_t blen, A_INT32 rxfilter)
{
    int ret = 0;  
    BIT_STR(buf, blen, ret, "UCAST "    , 0x00000001, rxfilter);
    BIT_STR(buf, blen, ret, "MCAST "    , 0x00000002, rxfilter);
    BIT_STR(buf, blen, ret, "BCAST "   , 0x00000004, rxfilter);
    BIT_STR(buf, blen, ret, "CONTROL "  , 0x00000008, rxfilter);
    BIT_STR(buf, blen, ret, "BEACON "  , 0x00000010, rxfilter);
    BIT_STR(buf, blen, ret, "PROM "    , 0x00000020, rxfilter);
    BIT_STR(buf, blen, ret, "PROBEREQ ", 0x00000080, rxfilter);
    BIT_STR(buf, blen, ret, "MYBEACON ", 0x00000200, rxfilter);
    BIT_STR(buf, blen, ret, "COMP_BAR ", 0x00000400, rxfilter);
    BIT_STR(buf, blen, ret, "COMP_BA " , 0x00000800, rxfilter);
    BIT_STR(buf, blen, ret, "UNCOMP_BA_BAR " , 0x00001000, rxfilter);
    BIT_STR(buf, blen, ret, "PS_POLL " , 0x00004000, rxfilter);
    BIT_STR(buf, blen, ret, "MCAST_BCAST_ALL " , 0x00008000, rxfilter);
    BIT_STR(buf, blen, ret, "FROM_TO_DS "      , 0x00020000, rxfilter);
    buf[(ret>0) ? --ret : ret] = 0;
    return ret;
}

static int btState2text(char *buf, size_t blen, A_INT32 btState)
{
    int bret = 0;
    BIT_STR(buf, blen, bret, "SCO ", (1<<1), btState);
    BIT_STR(buf, blen, bret, "A2DP ", (1<<2), btState);
    BIT_STR(buf, blen, bret, "SCAN ", (1<<3), btState);
    BIT_STR(buf, blen, bret, "ESCO ", (1<<4), btState);
    buf[(bret>0) ? --bret : bret] = 0;
    return bret;
}

static int btcoexFlags2text(char *buf, size_t blen, A_INT32 btCoexFlags)
{
    int bret = 0;
    BIT_STR(buf, blen, bret, "SCO_ON ", 0x0001, btCoexFlags);
    BIT_STR(buf, blen, bret, "A2DP_ON ", 0x0002, btCoexFlags);
    BIT_STR(buf, blen, bret, "ACL_ON ", 0x0004, btCoexFlags);
    BIT_STR(buf, blen, bret, "INQUIRY_ON ", 0x0008, btCoexFlags);
    BIT_STR(buf, blen, bret, "VOIP ", 0x0010, btCoexFlags);
    BIT_STR(buf, blen, bret, "RXDATA_PENDING ", 0x0020, btCoexFlags);
    BIT_STR(buf, blen, bret, "SLEEP_PENDING ", 0x0040, btCoexFlags);
    BIT_STR(buf, blen, bret, "FAKESLEEP_ON ", 0x0080, btCoexFlags);
    BIT_STR(buf, blen, bret, "ADD_BA_PENDING ", 0x0100, btCoexFlags);
    BIT_STR(buf, blen, bret, "WAKEUP_TIM_INTR ", 0x0200, btCoexFlags);
    BIT_STR(buf, blen, bret, "OPT_MODE ", 0x0400, btCoexFlags);
    BIT_STR(buf, blen, bret, "FIRST_BMISS ", 0x0800, btCoexFlags);
    BIT_STR(buf, blen, bret, "BEACON_PROTECION_ON ", 0x1000, btCoexFlags);
    BIT_STR(buf, blen, bret, "WAIT_FOR_NULL_COMP ", 0x4000, btCoexFlags);
    BIT_STR(buf, blen, bret, "PROTECT_POST_INQUIRY ", 0x8000, btCoexFlags);
    BIT_STR(buf, blen, bret, "DUAL_ANT_ACTIVE ", 0x10000, btCoexFlags);
    BIT_STR(buf, blen, bret, "DUAL_ANT_WAKEUP_TIM ", 0x20000, btCoexFlags);
    BIT_STR(buf, blen, bret, "STOMP_FOR_DTIM_RECV ", 0x40000, btCoexFlags);
    BIT_STR(buf, blen, bret, "ENABLE_COEX_ON_BCN_RECV ", 0x80000, btCoexFlags);
    buf[(bret>0) ? --bret : bret] = 0;
    return bret;
}

static int WMI_SET_MCAST_FILTER_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    WMI_SET_MCAST_FILTER_CMD *cmd = (WMI_SET_MCAST_FILTER_CMD*)cmdbuf;
    return snprintf(output, len, "%02X:%02X:%02X:%02X:%02X:%02X",
                    cmd->multicast_mac[0], cmd->multicast_mac[1], cmd->multicast_mac[2], 
                    cmd->multicast_mac[3], cmd->multicast_mac[4], cmd->multicast_mac[5]);
}

static int WMI_DEL_MCAST_FILTER_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    return WMI_SET_MCAST_FILTER_CMDID_fmt(output, len, cmdbuf);
}

static int WMI_MCAST_FILTER_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    WMI_MCAST_FILTER_CMD *cmd = (WMI_MCAST_FILTER_CMD*)cmdbuf;
    return snprintf(output, len, "%s", enable2text(cmd->enable));
}

static int WMI_START_SCAN_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    int i;
    int ret = 0;
    WMI_START_SCAN_CMD *cmd = (WMI_START_SCAN_CMD*)cmdbuf;
    const char *scanType = (cmd->scanType == WMI_SHORT_SCAN) ? "short" : "long";
    if (cmd->forceFgScan) {
        ret += snprintf(output+ret, len-ret, "forceFg ");
    }
    ret += snprintf(output+ret, len-ret, "hmdwell %u ", cmd->homeDwellTime);
    ret += snprintf(output+ret, len-ret, "fscanint %u ", cmd->forceScanInterval);
    ret += snprintf(output+ret, len-ret, "%s ", scanType);
    for (i=0; i<cmd->numChannels; ++i) {
        ret += snprintf(output+ret, len-ret, "%d ", cmd->channelList[i]);
    }
    return ret;
}

static int WMI_CONNECT_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    WMI_CONNECT_CMD *cmd = (WMI_CONNECT_CMD*)cmdbuf;
    char ssid[33];
    char pairwise[128], group[128];
    const char *dot11Auth, *authMode;
    memcpy(ssid, cmd->ssid, 32);
    ssid[cmd->ssidLength] = 0;
    cipher2text(pairwise, sizeof(pairwise), cmd->pairwiseCryptoType);
    cipher2text(group, sizeof(group), cmd->groupCryptoType);

    switch (cmd->dot11AuthMode) {
    CASE_STR(OPEN_AUTH, dot11Auth);
    CASE_STR(SHARED_AUTH, dot11Auth);
    CASE_STR(LEAP_AUTH, dot11Auth);
    CASE_DEF_STR(dot11Auth);
    }

    switch (cmd->authMode) {
    CASE_STR(WMI_NONE_AUTH, authMode);
    CASE_STR(WMI_WPA_AUTH, authMode);
    CASE_STR(WMI_WPA2_AUTH, authMode);
    CASE_STR(WMI_WPA_PSK_AUTH, authMode);
    CASE_STR(WMI_WPA2_PSK_AUTH, authMode);
    CASE_STR(WMI_WPA_AUTH_CCKM, authMode);
    CASE_STR(WMI_WPA2_AUTH_CCKM, authMode);
    CASE_DEF_STR(authMode);
    }
    return snprintf(output, len, "'%s' ch %d %s %s uni:%s grp:%s %02X:%02X:%02X:%02X:%02X:%02X ctrl 0x%x",
        ssid, cmd->channel, 
        dot11Auth, authMode, pairwise, group,
        cmd->bssid[0], cmd->bssid[1], cmd->bssid[2], 
        cmd->bssid[3], cmd->bssid[4], cmd->bssid[5], 
        cmd->ctrl_flags);
}

static int WMI_SET_BTCOEX_FE_ANT_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    WMI_SET_BTCOEX_FE_ANT_CMD *cmd = (WMI_SET_BTCOEX_FE_ANT_CMD*)cmdbuf;
    const char *fe;
    switch (cmd->btcoexFeAntType) {
    CASE_STR(WMI_BTCOEX_NOT_ENABLED, fe);
    CASE_STR(WMI_BTCOEX_FE_ANT_SINGLE, fe);
    CASE_STR(WMI_BTCOEX_FE_ANT_DUAL, fe);
    CASE_STR(WMI_BTCOEX_FE_ANT_DUAL_HIGH_ISO, fe);
    CASE_DEF_STR(fe);
    }
    return snprintf(output, len, "%s", fe);
}

static int WMI_SET_BTCOEX_SCO_CONFIG_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    WMI_SET_BTCOEX_SCO_CONFIG_CMD *cmd = (WMI_SET_BTCOEX_SCO_CONFIG_CMD*)cmdbuf;
    char scoFlags[512];
    int blen = sizeof(scoFlags);
    int bret = 0;
    BIT_STR(scoFlags, blen, bret, "OPT ", (1<<0), cmd->scoConfig.scoFlags);
    BIT_STR(scoFlags, blen, bret, "EDR ", (1<<1), cmd->scoConfig.scoFlags);   
    BIT_STR(scoFlags, blen, bret, "MASTER ", (1<<2), cmd->scoConfig.scoFlags);
    BIT_STR(scoFlags, blen, bret, "FRM ", (1<<3), cmd->scoConfig.scoFlags);
    scoFlags[(bret>0) ? --bret : bret] = 0;

    return snprintf(output, len, "%d/%d slots [%s] ps %u-%u-%u opt %u-%u-%u-%u-%u-%u scan %u/%u", 
                    cmd->scoConfig.scoSlots, cmd->scoConfig.scoIdleSlots, scoFlags,

                    cmd->scoPspollConfig.scoCyclesForceTrigger,
                    cmd->scoPspollConfig.scoDataResponseTimeout,
                    cmd->scoPspollConfig.scoPsPollLatencyFraction,

                    cmd->scoOptModeConfig.scoStompCntIn100ms,
	                cmd->scoOptModeConfig.scoContStompMax,
	                cmd->scoOptModeConfig.scoMinlowRateMbps,
	                cmd->scoOptModeConfig.scoLowRateCnt,
	                cmd->scoOptModeConfig.scoHighPktRatio,
	                cmd->scoOptModeConfig.scoMaxAggrSize,

                    cmd->scoWlanScanConfig.scanInterval,
                    cmd->scoWlanScanConfig.maxScanStompCnt);
}

static int WMI_SET_BTCOEX_A2DP_CONFIG_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    return 0;
}

static int WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    return 0;
}

static int WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD *cmd = (WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD*)cmdbuf;
    return snprintf(output, len, "pspoll every %u inquiry duration %u",
                    cmd->btInquiryDataFetchFrequency,
                    cmd->protectBmissDurPostBtInquiry);
}

static int WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD *cmd = (WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD*)cmdbuf;
    const char *profile;
    const char *op = (cmd->btOperatingStatus==1) ? "START" : "STOP";
    switch (cmd->btProfileType) {
    CASE_STR_CONST(SCO, 1, profile);
    CASE_STR_CONST(A2DP, 2, profile);
    CASE_STR_CONST(Inquiry, 3, profile);
    CASE_STR_CONST(ACL, 4, profile);
    CASE_DEF_STR(profile);
    }
    return snprintf(output, len, "%s %s", profile, op);
}

static int WMI_SET_LISTEN_INT_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    WMI_LISTEN_INT_CMD *cmd = (WMI_LISTEN_INT_CMD*)cmdbuf;
    return snprintf(output, len, "interval %d numBeacons %d", cmd->listenInterval, cmd->numBeacons);
}

static int WMI_SET_BSS_FILTER_CMDID_fmt(char *output, size_t len, const A_UINT8 *cmdbuf)
{
    WMI_BSS_FILTER_CMD *cmd = (WMI_BSS_FILTER_CMD*)cmdbuf;
    const char *filter;
    switch (cmd->bssFilter) {
    CASE_STR(NONE_BSS_FILTER, filter);
    CASE_STR(ALL_BSS_FILTER, filter);
    CASE_STR(PROFILE_FILTER, filter);
    CASE_STR(ALL_BUT_PROFILE_FILTER, filter);
    CASE_STR(CURRENT_BSS_FILTER, filter);
    CASE_STR(ALL_BUT_BSS_FILTER, filter);
    CASE_STR(PROBED_SSID_FILTER, filter);
    CASE_STR(LAST_BSS_FILTER, filter);
    CASE_DEF_STR(filter);
    }
    return snprintf(output, len, "[%s]", filter);
}

static int CO_CHANGE_CHANNEL_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT32 freq = buffer[1];
    A_INT32 duration = buffer[2];
    return snprintf(output, len, "freq %d duration %d", freq, duration);
}

static int CO_CHANGE_STATE_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT32 stateLog = (numargs==1) ? buffer[1] : buffer[2];
    int op = stateLog & 0xff;
    const char *opstr = op ? "set" : "clr";
    int opState = (stateLog >> 8) & 0xff;
    int oldState = stateLog >> 16;
    int newState = (op) ? (oldState | opState) : (oldState & ~(opState));
    char state[256];
    size_t blen = sizeof(state);
    int ret = 0;
    BIT_STR(state, blen, ret, "DRAIN_IN_PROGRESS ", 0x01, newState);
    BIT_STR(state, blen, ret, "FAKE_SLEEP_ENABLE_IN_PROGRESS ", 0x02, newState);
    BIT_STR(state, blen, ret, "FAKE_SLEEP_ENABLED ", 0x04, newState);
    BIT_STR(state, blen, ret, "TXQ_PAUSED ", 0x08, newState);
    BIT_STR(state, blen, ret, "CHANNEL_CHANGE_IN_PROGRESS ", 0x10, newState);
    state[(ret>0) ? --ret : ret] = 0;

    ret = 0;
    if (numargs==2) {
        ret = snprintf(output+ret, len-ret, "dev %d ", buffer[1]);
    }
    ret += snprintf(output+ret, len-ret, "0x%x %s 0x%x->0x%x [%s]",
                    oldState, opstr, opState, newState, state);
    return ret;
}

static int TXRX_MGMTBUF_WLAN_RESET_ON_ERROR_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    if (numargs==1) {
        char buf[512];
        A_INT32 rxfilter = buffer[1];
        rxfilter2text(buf, sizeof(buf),rxfilter);
        return snprintf(output, len, "rxfilter:[%s]", buf);
    } else if (numargs==2) {
        return snprintf(output, len, "rstCnt %d caller %p", 
                        buffer[1], (void*)buffer[2]);
    } else {
        return 0;
    }
}
static int TXRX_MGMTBUF_WAIT_FOR_TXQ_DRAIN_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *mid;
    switch (buffer[2]) {
    CASE_STR_CONST(CO, 0x001, mid);
    CASE_STR_CONST(PM, 0x002, mid);
    CASE_STR_CONST(BTCOEX, 0x004, mid);
    CASE_DEF_STR(mid);
    }
    return snprintf(output, len, "wait drain %d ms %s ", buffer[1], mid);
}

static int TXRX_MGMTBUF_REAPED_BUF_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *txstatus = txrxstatus2text(buffer[2]);
    return snprintf(output, len, "mgt %p %s", (void*)buffer[1], txstatus);
}

static int TXRX_MGMTBUF_DRAINQ_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT32 hwq = buffer[1];
    A_INT32 depth = buffer[2];
    return snprintf(output, len, "hwq 0x%x depth 0x%x", hwq, depth);
}

static int DC_RECEIVED_ANY_BEACON_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    if (numargs==1) {
        A_INT32 addr3 = buffer[1];
        return snprintf(output, len, "addr3 ??:??:%02X:%02X:%02X:%02X",
                 (addr3 & 0xff), ((addr3>>8)&0xff), ((addr3>>16)&0xff), ((addr3>>24)&0xff));
    } else if (numargs==2) {
        A_UINT64 tsf;
        A_UINT8 ie_tstamp[8];
        ie_tstamp[4] = buffer[1] & 0xff;
        ie_tstamp[5] = (buffer[1]>>8) & 0xff;
        ie_tstamp[6] = (buffer[1]>>16) & 0xff;
        ie_tstamp[7] = (buffer[1]>>24) & 0xff;
        ie_tstamp[0] = buffer[2] & 0xff;
        ie_tstamp[1] = (buffer[2]>>8) & 0xff;
        ie_tstamp[2] = (buffer[2]>>16) & 0xff;
        ie_tstamp[3] = (buffer[2]>>24) & 0xff;
        A_MEMCPY((A_UINT8 *)&tsf, ie_tstamp, sizeof(ie_tstamp));
        return snprintf(output, len, "ie_tsf %llu", tsf);
    }
    return 0;
}

static int DC_SET_POWER_MODE_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *pm = pmmode2text(buffer[1]);
    return snprintf(output, len, "%s caller %p", pm, (void*)buffer[2]);
}

static int DC_SSID_PROBE_CB_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *txstatus = txrxstatus2text(buffer[1]);
    return snprintf(output, len, "%s", txstatus);
}

static int DC_SEARCH_OPPORTUNITY_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *opt;
    switch (buffer[1]) {
    CASE_STR_CONST(SCAN_IN_PROGRESS, 1, opt);
    CASE_STR_CONST(NOT_SCAN_IN_PROGRESS,  0, opt);
    CASE_DEF_STR(opt);
    }
    return snprintf(output, len, "%s", opt);
}

static int DC_SEND_NEXT_SSID_PROBE_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *flags;
    switch (buffer[2]) {
    CASE_STR(DISABLE_SSID_FLAG, flags);
    CASE_STR(SPECIFIC_SSID_FLAG, flags);
    CASE_STR(ANY_SSID_FLAG, flags);
    CASE_DEF_STR(flags);
    }  
    return snprintf(output, len, "idx %d %s", buffer[1], flags);
}

static int DC_SCAN_CHAN_FINISH_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_UINT16 freq = buffer[1] & 0xffff;
    A_UINT16 status = (buffer[1] >> 16) & 0xffff;
    A_INT32 rxfilter = buffer[2];   
    char rxfilterMsg[1024];
    rxfilter2text(rxfilterMsg, sizeof(rxfilterMsg), rxfilter);
    return snprintf(output, len, "freq %d status %s(%d), %s", 
                    freq, status2text(status), status, rxfilterMsg);
}

static int DC_SCAN_CHAN_START_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT32 rxfilter = buffer[2];   
    char rxfilterMsg[1024];
    A_UINT16 freq;
    A_UINT16 attrib;
    const char *probed;
    rxfilter2text(rxfilterMsg, sizeof(rxfilterMsg), rxfilter);
    freq = buffer[1] & 0xffff;
    attrib = (buffer[1] >> 16) & 0xffff;
    probed = ((attrib & (0x0100|0x10))==(0x0100|0x10)) && !(attrib &  0x0800) ? "allow" : "not allow";

    return snprintf(output, len, "freq %d attrib %d probed %s %s", 
                    freq, attrib, probed, rxfilterMsg);
}

static int DC_START_SEARCH_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    if (numargs==1) {
        return snprintf(output, len, "devid %d", buffer[1]);
    } else {
        A_INT32 stype = buffer[1];
        int ret = 0;  
        char buf[1024];
        size_t blen = sizeof(buf);
        if (stype == 0) {
            BIT_STR(buf, blen, ret, "RESET "                     , 0, stype);
        }
        BIT_STR(buf, blen, ret, "ALL "                      , (0x01|0x02|0x04|0x08), stype);
        if (((0x01|0x02|0x04|0x08)& stype)!=(0x01|0x02|0x04|0x08)) {
            BIT_STR(buf, blen, ret, "POPULAR "                   , (0x02 | 0x04 | 0x08), stype);
            if (((0x02|0x04|0x08)& stype)!=(0x02|0x04|0x08)) {
                BIT_STR(buf, blen, ret, "SSIDS "                     , (0x04 | 0x08), stype);
                if (((0x04|0x08)& stype)!=(0x04|0x08)) {
                    BIT_STR(buf, blen, ret, "PROF_MASK "                 , (0x08), stype);
                }
            }
        }

        BIT_STR(buf, blen, ret, "MULTI_CHANNEL "             , 0x000100, stype);
        BIT_STR(buf, blen, ret, "DETERMINISTIC "             , 0x000200, stype);
        BIT_STR(buf, blen, ret, "PROFILE_MATCH_TERMINATED "  , 0x000400, stype);
        BIT_STR(buf, blen, ret, "HOME_CHANNEL_SKIP "         , 0x000800, stype);
        BIT_STR(buf, blen, ret, "CHANNEL_LIST_CONTINUE "     , 0x001000, stype);
        BIT_STR(buf, blen, ret, "CURRENT_SSID_SKIP "         , 0x002000, stype);
        BIT_STR(buf, blen, ret, "ACTIVE_PROBE_DISABLE "      , 0x004000, stype);
        BIT_STR(buf, blen, ret, "CHANNEL_HINT_ONLY "         , 0x008000, stype);
        BIT_STR(buf, blen, ret, "ACTIVE_CHANNELS_ONLY "      , 0x010000, stype);
        BIT_STR(buf, blen, ret, "UNUSED1 "                   , 0x020000, stype);
        BIT_STR(buf, blen, ret, "PERIODIC "                  , 0x040000, stype);
        BIT_STR(buf, blen, ret, "FIXED_DURATION "            , 0x080000, stype);
        BIT_STR(buf, blen, ret, "AP_ASSISTED "               , 0x100000, stype);
        buf[(ret>0) ? --ret : ret] = 0;
        return snprintf(output, len, "%s cb 0x%x", buf, buffer[2]);
    }
}

static int PM_CHAN_OP_REQ_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *start = (buffer[1]==1) ? "start" : "stop";
    A_INT32 chanOpReq = buffer[2];
    return snprintf(output, len, "%s chan OpReq %d", start, chanOpReq);
}

static int PM_SET_ALL_BEACON_POLICY_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *policyMsg;
    A_UINT16 policy = buffer[1];
    A_UINT32 cnt = buffer[2];
    switch (policy) {
    CASE_STR_CONST(disallow, 1, policyMsg);
    CASE_STR_CONST(allow, 2, policyMsg);
    CASE_DEF_STR(policyMsg);
    }
    return snprintf(output, len, "%s beacons filterCnt %d", policyMsg, cnt);
}

static int PM_SET_MY_BEACON_POLICY_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *policyMsg;
    A_UINT16 policy = buffer[1] & 0xff;
    A_UINT32 bMiss = (buffer[1] >> 8);
    A_UINT32 myBeaconCnt = buffer[2];
    switch (policy) {
    CASE_STR_CONST(disallow, 1, policyMsg);
    CASE_STR_CONST(allow, 2, policyMsg);
    CASE_DEF_STR(policyMsg);
    }
    return snprintf(output, len, "bmiss %d %s during sleep filterCnt %d", bMiss, policyMsg, myBeaconCnt);
}


static int PM_SET_STATE_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT16 pmStateWakeupCount = (buffer[1] >> 16) & 0xffff;
    A_INT16 pmState = buffer[1] & 0xffff;
    A_INT16 pmAwakeCount = (buffer[2] >> 16) & 0xffff;
    A_INT8 pmSleepCount = (buffer[2] >> 8) & 0xff;
    A_INT8 pmOldState = buffer[2] & 0xff;
    return snprintf(output, len, "StateWakeupCnt %d AwakeCnt %d, SleepCnt %d, %s to %s",
                    pmStateWakeupCount, pmAwakeCount, pmSleepCount, 
                    pmmode2text(pmOldState), pmmode2text(pmState));
}

static int PM_SET_POWERMODE_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_UINT16 wcnt = buffer[1] >> 16;
    A_UINT16 fakeSleep = buffer[1] & 0xffff;
    A_UINT16 oldPowerMode = buffer[2] >> 16;
    A_UINT8 powerMode = (buffer[2] >> 8) & 0xff;
    A_UINT8 moduleId = buffer[2] & 0xff;
    return snprintf(output, len, "wakeCnt %d fakeSleep %d %s(%d)=>%s(%d) %s(%d)",
         wcnt, fakeSleep, pmmode2text(oldPowerMode), oldPowerMode, pmmode2text(powerMode), powerMode, pmmodule2text(moduleId), moduleId);
}

static int PM_FAKE_SLEEP_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT16 enable = (buffer[1] >> 16) & 0xffff;
    const char *state = enable2text(enable);
    A_INT8 pmFakeSleepCount = (buffer[1] >> 8) & 0xff;
    A_INT8 fakeSleepEnable = (buffer[1] & 0xff);
    A_INT16 forceAwake = (buffer[2] >> 16) & 0xffff;
    A_INT8 dontWaitForDrain = (buffer[2] >> 8) & 0xff;
    A_INT8 module = buffer[2] & 0xff;
    return snprintf(output, len, "%s cnt %d hasCnt %d forceAwake %d dontWaitDrain %d %s(%d)",
            state, pmFakeSleepCount, fakeSleepEnable, forceAwake, dontWaitForDrain, pmmodule2text(module), module);
}

static int BTCOEX_DBG_pmwakeupcnt_flags(char *output, size_t len, A_INT32 pmWakeupCnt, A_INT32 btCoexFlags)
{
    int ret = 0;
    ret += snprintf(output+ret, len-ret, "coexPmWakeupCnt %d", pmWakeupCnt);
    if (btCoexFlags!=-1) {
        char buf[512];
        btcoexFlags2text(buf, sizeof(buf), btCoexFlags);
        ret += snprintf(output+ret, len-ret, " coex [%s]", buf);
    }
    return ret;
}

static int BTCOEX_ACL_COEX_STATUS_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT32 btStatus = (buffer[1] >> 16) & 0xffff;
    A_INT32 redoAggr = buffer[1] & 0xffff;
    return snprintf(output, len, "%s redoAggr %d", btStatus2text(btStatus), redoAggr);
}

static int BTCOEX_DBG_PM_SLEEP_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT32 pmWakeupCnt = buffer[1];
    A_INT32 btCoexFlags = buffer[2];
    return BTCOEX_DBG_pmwakeupcnt_flags(output,len,pmWakeupCnt,btCoexFlags); 
}

static int BTCOEX_PSPOLL_QUEUED_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT16 isEolEnabled = (buffer[1] >> 16) & 0xffff;
    A_INT8 bSendAtLowestRate = (buffer[1] >>8) & 0xff;
    A_INT8 isPmSleep = (buffer[1]) & 0xff;
    return snprintf(output, len, "Eol:%s LowestRate:%s PmSleep:%s", 
                    enable2text(isEolEnabled), enable2text(bSendAtLowestRate),
                    enable2text(isPmSleep));
}

static int BTCOEX_PSPOLL_COMPLETE_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    return snprintf(output, len, "%s", txrxstatus2text(buffer[1]));
}

static int BTCOEX_DBG_PM_AWAKE_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    return BTCOEX_DBG_PM_SLEEP_fmt(output,len,numargs,buffer);
}

static int BTCOEX_DBG_GO_TO_SLEEP_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    return BTCOEX_DBG_pmwakeupcnt_flags(output,len,buffer[1],-1);
}

static int BTCOEX_WAKEUP_ON_DATA_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    return BTCOEX_DBG_pmwakeupcnt_flags(output,len,buffer[2], buffer[1]);
}

static int BTCOEX_TIM_NOTIFICATION_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    int ret = 0;
    A_INT32 btCoexFlags = buffer[1];
    A_INT32 btStatus = (buffer[2]>>16) & 0xffff;
    A_INT32 pmWakeupCnt = (buffer[2] & 0xffff);
    ret = BTCOEX_DBG_pmwakeupcnt_flags(output,len, pmWakeupCnt, btCoexFlags);
    ret += snprintf(output+ret, len-ret, " %s", btStatus2text(btStatus));
    return ret;
}

static int BTCOEX_DBG_ALLOW_SCAN_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    char buf[512];
    A_INT32 retStatus = buffer[2];
    A_INT32 btState = buffer[1];
    btState2text(buf, sizeof(buf), btState);
    return snprintf(output, len, "state: [%s] allow:%d", buf, retStatus);
}

static int BTCOEX_DBG_SCAN_REQUEST_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    char buf[512];
    A_INT32 scanReqEnabled = buffer[2];
    A_INT32 btState = buffer[1];
    btState2text(buf, sizeof(buf), btState);
    return snprintf(output, len, "state: [%s] scanReqEnabled:%d", buf, scanReqEnabled);
}

static int BTCOEX_DBG_SET_WLAN_STATE_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT32 btCoexFlags = buffer[2];
    char buf[512];
    const char *wlanState;
    switch (buffer[1]) {
    CASE_STR_CONST(IDLE     , 1, wlanState);
    CASE_STR_CONST(CONNECTED , 2, wlanState);
    CASE_STR_CONST(SCAN_START  , 3, wlanState);
    CASE_STR_CONST(CONNECT_START ,4, wlanState);
    CASE_STR_CONST(SCAN_END    , 5, wlanState);
    CASE_STR_CONST(APMODE_STA_CONNECTED , 6, wlanState);
    CASE_STR_CONST(APMODE_IDLE , 7, wlanState);
    CASE_STR_CONST(APMODE_SWITCH , 8, wlanState);
    CASE_STR_CONST(APMODE_TEARDOWN , 9, wlanState);
    CASE_DEF_STR(wlanState);
    }
    btcoexFlags2text(buf, sizeof(buf), btCoexFlags);
    return snprintf(output, len, "wlan %s coex [%s]", wlanState, buf);
}

static int BTCOEX_DBG_BT_INQUIRY_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *btState = btStatus2text(buffer[1]);
    A_INT32 btCoexFlags = buffer[2];
    char buf[512];
    btcoexFlags2text(buf, sizeof(buf), btCoexFlags);
    return snprintf(output, len, "%s coex [%s]", btState, buf);
}

static int BTCOEX_SET_WEIGHTS_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *weights;
    switch (buffer[1]) {
    CASE_STR_CONST(ALL_BT_TRAFFIC,1,weights);
    CASE_STR_CONST(ONLY_HIGH_PRI_BT_TRAFFIC,2,weights);
    CASE_STR_CONST(STOMP_ALL_BT_TRAFFIC,3,weights);
    CASE_STR_CONST(ONLY_A2DP_TRAFFIC,4,weights);
    CASE_STR_CONST(ONLY_HIGH_PRIO_AND_A2DP,5,weights);
    CASE_STR_CONST(A2DP_STOMPED,6,weights);
    CASE_STR_CONST(ALL_BT_TRAFFIC_WTX,7,weights);
    CASE_STR_CONST(ALL_BT_TRAFFIC_WTX_HIGHISO_TXRX,8,weights);
    CASE_STR_CONST(HIGH_PRI_TRAFFIC_WTX,9,weights);
    CASE_STR_CONST(HIGH_PRI_TRAFFIC_WTX_HIGHISO_TXRX,0xa,weights);
    CASE_STR_CONST(MCI_TEST,0xb,weights);
    CASE_DEF_STR(weights);
    }

    return snprintf(output, len, "%s val 0x%x", weights, buffer[2]);    
}

static int BTCOEX_PM_FAKE_SLEEP_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *enable = enable2text(buffer[1]);
    A_STATUS status = buffer[2];
    return snprintf(output, len, "%s -> %s", enable, status2text(status));
}

static const struct wmi_id_desc evt_desc[] = {
    WM_ID_DESC(WMI_READY_EVENTID), 
    WM_ID_DESC(WMI_CONNECT_EVENTID),
    WM_ID_DESC(WMI_DISCONNECT_EVENTID),
    WM_ID_DESC(WMI_BSSINFO_EVENTID),
    WM_ID_DESC(WMI_CMDERROR_EVENTID),
    WM_ID_DESC(WMI_REGDOMAIN_EVENTID),
    WM_ID_DESC(WMI_PSTREAM_TIMEOUT_EVENTID),
    WM_ID_DESC(WMI_NEIGHBOR_REPORT_EVENTID),
    WM_ID_DESC(WMI_TKIP_MICERR_EVENTID),
    WM_ID_DESC(WMI_SCAN_COMPLETE_EVENTID),           /* 0x100a */
    WM_ID_DESC(WMI_REPORT_STATISTICS_EVENTID),
    WM_ID_DESC(WMI_RSSI_THRESHOLD_EVENTID),
    WM_ID_DESC(WMI_ERROR_REPORT_EVENTID),
    WM_ID_DESC(WMI_OPT_RX_FRAME_EVENTID),
    WM_ID_DESC(WMI_REPORT_ROAM_TBL_EVENTID),
    WM_ID_DESC(WMI_EXTENSION_EVENTID),
    WM_ID_DESC(WMI_CAC_EVENTID),
    WM_ID_DESC(WMI_SNR_THRESHOLD_EVENTID),
    WM_ID_DESC(WMI_LQ_THRESHOLD_EVENTID),
    WM_ID_DESC(WMI_TX_RETRY_ERR_EVENTID),            /* 0x1014 */
    WM_ID_DESC(WMI_REPORT_ROAM_DATA_EVENTID),
    WM_ID_DESC(WMI_TEST_EVENTID),
    WM_ID_DESC(WMI_APLIST_EVENTID),
    WM_ID_DESC(WMI_GET_WOW_LIST_EVENTID),
    WM_ID_DESC(WMI_GET_PMKID_LIST_EVENTID),
    WM_ID_DESC(WMI_CHANNEL_CHANGE_EVENTID),
    WM_ID_DESC(WMI_PEER_NODE_EVENTID),
    WM_ID_DESC(WMI_PSPOLL_EVENTID),
    WM_ID_DESC(WMI_DTIMEXPIRY_EVENTID),
    WM_ID_DESC(WMI_WLAN_VERSION_EVENTID),
    WM_ID_DESC(WMI_SET_PARAMS_REPLY_EVENTID),
    WM_ID_DESC(WMI_ADDBA_REQ_EVENTID),              /*0x1020 */
    WM_ID_DESC(WMI_ADDBA_RESP_EVENTID),
    WM_ID_DESC(WMI_DELBA_REQ_EVENTID),
    WM_ID_DESC(WMI_TX_COMPLETE_EVENTID),
    WM_ID_DESC(WMI_HCI_EVENT_EVENTID),
    WM_ID_DESC(WMI_ACL_DATA_EVENTID),
    WM_ID_DESC(WMI_REPORT_SLEEP_STATE_EVENTID),

    WM_ID_DESC(WMI_WAPI_REKEY_EVENTID),

    WM_ID_DESC(WMI_REPORT_BTCOEX_STATS_EVENTID),
    WM_ID_DESC(WMI_REPORT_BTCOEX_CONFIG_EVENTID),
    WM_ID_DESC(WMI_GET_PMK_EVENTID),

    /* DFS Events */
    WM_ID_DESC(WMI_DFS_HOST_ATTACH_EVENTID),
    WM_ID_DESC(WMI_DFS_HOST_INIT_EVENTID),
    WM_ID_DESC(WMI_DFS_RESET_DELAYLINES_EVENTID),
    WM_ID_DESC(WMI_DFS_RESET_RADARQ_EVENTID),
    WM_ID_DESC(WMI_DFS_RESET_AR_EVENTID),
    WM_ID_DESC(WMI_DFS_RESET_ARQ_EVENTID),          /*0x1030*/
    WM_ID_DESC(WMI_DFS_SET_DUR_MULTIPLIER_EVENTID),
    WM_ID_DESC(WMI_DFS_SET_BANGRADAR_EVENTID),
    WM_ID_DESC(WMI_DFS_SET_DEBUGLEVEL_EVENTID),
    WM_ID_DESC(WMI_DFS_PHYERR_EVENTID),
    /* CCX Evants */
    WM_ID_DESC(WMI_CCX_RM_STATUS_EVENTID),

    /* P2P Events */
    WM_ID_DESC(WMI_P2P_GO_NEG_RESULT_EVENTID),

    WM_ID_DESC(WMI_WAC_SCAN_DONE_EVENTID),
    WM_ID_DESC(WMI_WAC_REPORT_BSS_EVENTID),
    WM_ID_DESC(WMI_WAC_START_WPS_EVENTID),
    WM_ID_DESC(WMI_WAC_CTRL_REQ_REPLY_EVENTID),
        
    /*RFKILL Events*/
    WM_ID_DESC(WMI_RFKILL_STATE_CHANGE_EVENTID),
    WM_ID_DESC(WMI_RFKILL_GET_MODE_CMD_EVENTID),

    /* More P2P Events */
    WM_ID_DESC(WMI_P2P_GO_NEG_REQ_EVENTID),
    WM_ID_DESC(WMI_P2P_INVITE_REQ_EVENTID),
    WM_ID_DESC(WMI_P2P_INVITE_RCVD_RESULT_EVENTID),
    WM_ID_DESC(WMI_P2P_INVITE_SENT_RESULT_EVENTID), /*1040*/
    WM_ID_DESC(WMI_P2P_PROV_DISC_RESP_EVENTID),
    WM_ID_DESC(WMI_P2P_PROV_DISC_REQ_EVENTID),
    WM_ID_DESC(WMI_P2P_START_SDPD_EVENTID),
    WM_ID_DESC(WMI_P2P_SDPD_RX_EVENTID),
};

static const struct wmi_id_desc cmds_desc[] = {
    WM_ID_DESC_FMT(WMI_CONNECT_CMDID, sizeof(WMI_CONNECT_CMD)),
    WM_ID_DESC(WMI_RECONNECT_CMDID),
    WM_ID_DESC(WMI_DISCONNECT_CMDID),
    WM_ID_DESC(WMI_SYNCHRONIZE_CMDID),
    WM_ID_DESC(WMI_CREATE_PSTREAM_CMDID),
    WM_ID_DESC(WMI_DELETE_PSTREAM_CMDID),
    WM_ID_DESC_FMT(WMI_START_SCAN_CMDID, sizeof(WMI_START_SCAN_CMD)),
    WM_ID_DESC(WMI_SET_SCAN_PARAMS_CMDID),
    WM_ID_DESC_FMT(WMI_SET_BSS_FILTER_CMDID, sizeof(WMI_BSS_FILTER_CMD)),
    WM_ID_DESC(WMI_SET_PROBED_SSID_CMDID),               /* 10 */
    WM_ID_DESC_FMT(WMI_SET_LISTEN_INT_CMDID, sizeof(WMI_LISTEN_INT_CMD)),
    WM_ID_DESC(WMI_SET_BMISS_TIME_CMDID),
    WM_ID_DESC(WMI_SET_DISC_TIMEOUT_CMDID),
    WM_ID_DESC(WMI_GET_CHANNEL_LIST_CMDID),
    WM_ID_DESC(WMI_SET_BEACON_INT_CMDID),
    WM_ID_DESC(WMI_GET_STATISTICS_CMDID),
    WM_ID_DESC(WMI_SET_CHANNEL_PARAMS_CMDID),
    WM_ID_DESC(WMI_SET_POWER_MODE_CMDID),
    WM_ID_DESC(WMI_SET_IBSS_PM_CAPS_CMDID),
    WM_ID_DESC(WMI_SET_POWER_PARAMS_CMDID),              /* 20 */
    WM_ID_DESC(WMI_SET_POWERSAVE_TIMERS_POLICY_CMDID),
    WM_ID_DESC(WMI_ADD_CIPHER_KEY_CMDID),
    WM_ID_DESC(WMI_DELETE_CIPHER_KEY_CMDID),
    WM_ID_DESC(WMI_ADD_KRK_CMDID),
    WM_ID_DESC(WMI_DELETE_KRK_CMDID),
    WM_ID_DESC(WMI_SET_PMKID_CMDID),
    WM_ID_DESC(WMI_SET_TX_PWR_CMDID),
    WM_ID_DESC(WMI_GET_TX_PWR_CMDID),
    WM_ID_DESC(WMI_SET_ASSOC_INFO_CMDID),
    WM_ID_DESC(WMI_ADD_BAD_AP_CMDID),                    /* 30 */
    WM_ID_DESC(WMI_DELETE_BAD_AP_CMDID),
    WM_ID_DESC(WMI_SET_TKIP_COUNTERMEASURES_CMDID),
    WM_ID_DESC(WMI_RSSI_THRESHOLD_PARAMS_CMDID),
    WM_ID_DESC(WMI_TARGET_ERROR_REPORT_BITMASK_CMDID),
    WM_ID_DESC(WMI_SET_ACCESS_PARAMS_CMDID),
    WM_ID_DESC(WMI_SET_RETRY_LIMITS_CMDID),
    WM_ID_DESC(WMI_RESERVED1),
    WM_ID_DESC(WMI_RESERVED2),
    WM_ID_DESC(WMI_SET_VOICE_PKT_SIZE_CMDID),
    WM_ID_DESC(WMI_SET_MAX_SP_LEN_CMDID),                /* 40 */
    WM_ID_DESC(WMI_SET_ROAM_CTRL_CMDID),
    WM_ID_DESC(WMI_GET_ROAM_TBL_CMDID),
    WM_ID_DESC(WMI_GET_ROAM_DATA_CMDID),
    WM_ID_DESC(WMI_ENABLE_RM_CMDID),
    WM_ID_DESC(WMI_SET_MAX_OFFHOME_DURATION_CMDID),
    WM_ID_DESC(WMI_EXTENSION_CMDID),                        /* Non-wireless extensions */
    WM_ID_DESC(WMI_SNR_THRESHOLD_PARAMS_CMDID),
    WM_ID_DESC(WMI_LQ_THRESHOLD_PARAMS_CMDID),
    WM_ID_DESC(WMI_SET_LPREAMBLE_CMDID),
    WM_ID_DESC(WMI_SET_RTS_CMDID),                       /* 50 */
    WM_ID_DESC(WMI_CLR_RSSI_SNR_CMDID),
    WM_ID_DESC(WMI_SET_FIXRATES_CMDID),
    WM_ID_DESC(WMI_GET_FIXRATES_CMDID),
    WM_ID_DESC(WMI_SET_AUTH_MODE_CMDID),
    WM_ID_DESC(WMI_SET_REASSOC_MODE_CMDID),
    WM_ID_DESC(WMI_SET_WMM_CMDID),
    WM_ID_DESC(WMI_SET_WMM_TXOP_CMDID),
    WM_ID_DESC(WMI_TEST_CMDID),
    /* COEX AR6002 only*/
    WM_ID_DESC(WMI_SET_BT_STATUS_CMDID),                
    WM_ID_DESC(WMI_SET_BT_PARAMS_CMDID),                /* 60 */

    WM_ID_DESC(WMI_SET_KEEPALIVE_CMDID),
    WM_ID_DESC(WMI_GET_KEEPALIVE_CMDID),
    WM_ID_DESC(WMI_SET_APPIE_CMDID),
    WM_ID_DESC(WMI_GET_APPIE_CMDID),
    WM_ID_DESC(WMI_SET_WSC_STATUS_CMDID),

    /* Wake on Wireless */
    WM_ID_DESC(WMI_SET_HOST_SLEEP_MODE_CMDID),
    WM_ID_DESC(WMI_SET_WOW_MODE_CMDID),
    WM_ID_DESC(WMI_GET_WOW_LIST_CMDID),
    WM_ID_DESC(WMI_ADD_WOW_PATTERN_CMDID),
    WM_ID_DESC(WMI_DEL_WOW_PATTERN_CMDID),               /* 70 */

    WM_ID_DESC(WMI_SET_FRAMERATES_CMDID),
    WM_ID_DESC(WMI_SET_AP_PS_CMDID),
    WM_ID_DESC(WMI_SET_QOS_SUPP_CMDID),
};

static const struct wmi_id_desc cmdxs_desc[] = {
    WM_ID_DESC(WMI_SET_BITRATE_CMDID),
    WM_ID_DESC(WMI_GET_BITRATE_CMDID),
    WM_ID_DESC(WMI_SET_WHALPARAM_CMDID),


    /*Should add the new command to the tail for compatible with
     * etna.
     */
    WM_ID_DESC(WMI_SET_MAC_ADDRESS_CMDID),
    WM_ID_DESC(WMI_SET_AKMP_PARAMS_CMDID),
    WM_ID_DESC(WMI_SET_PMKID_LIST_CMDID),
    WM_ID_DESC(WMI_GET_PMKID_LIST_CMDID),
    WM_ID_DESC(WMI_ABORT_SCAN_CMDID),
    WM_ID_DESC(WMI_SET_TARGET_EVENT_REPORT_CMDID),

    /* Unused */
    WM_ID_DESC(WMI_UNUSED1),
    WM_ID_DESC(WMI_UNUSED2),

    /*
     * AP mode commands
     */
    WM_ID_DESC(WMI_AP_HIDDEN_SSID_CMDID),
    WM_ID_DESC(WMI_AP_SET_NUM_STA_CMDID),
    WM_ID_DESC(WMI_AP_ACL_POLICY_CMDID),
    WM_ID_DESC(WMI_AP_ACL_MAC_LIST_CMDID),
    WM_ID_DESC(WMI_AP_CONFIG_COMMIT_CMDID),
    WM_ID_DESC(WMI_AP_SET_MLME_CMDID),
    WM_ID_DESC(WMI_AP_SET_PVB_CMDID),
    WM_ID_DESC(WMI_AP_CONN_INACT_CMDID),
    WM_ID_DESC(WMI_AP_PROT_SCAN_TIME_CMDID),
    WM_ID_DESC(WMI_AP_SET_COUNTRY_CMDID),
    WM_ID_DESC(WMI_AP_SET_DTIM_CMDID),
    WM_ID_DESC(WMI_AP_MODE_STAT_CMDID),

    WM_ID_DESC(WMI_SET_IP_CMDID),
    WM_ID_DESC(WMI_SET_PARAMS_CMDID),
    WM_ID_DESC_FMT(WMI_SET_MCAST_FILTER_CMDID, sizeof(WMI_SET_MCAST_FILTER_CMD)),
    WM_ID_DESC_FMT(WMI_DEL_MCAST_FILTER_CMDID, sizeof(WMI_SET_MCAST_FILTER_CMD)),

    WM_ID_DESC(WMI_ALLOW_AGGR_CMDID),
    WM_ID_DESC(WMI_ADDBA_REQ_CMDID),
    WM_ID_DESC(WMI_DELBA_REQ_CMDID),
    WM_ID_DESC(WMI_SET_HT_CAP_CMDID),
    WM_ID_DESC(WMI_SET_HT_OP_CMDID),
    WM_ID_DESC(WMI_SET_TX_SELECT_RATES_CMDID),
    WM_ID_DESC(WMI_SET_TX_SGI_PARAM_CMDID),
    WM_ID_DESC(WMI_SET_RATE_POLICY_CMDID),

    WM_ID_DESC(WMI_HCI_CMD_CMDID),
    WM_ID_DESC(WMI_RX_FRAME_FORMAT_CMDID),
    WM_ID_DESC(WMI_SET_THIN_MODE_CMDID),
    WM_ID_DESC(WMI_SET_BT_WLAN_CONN_PRECEDENCE_CMDID),

    WM_ID_DESC(WMI_AP_SET_11BG_RATESET_CMDID),
    WM_ID_DESC(WMI_SET_PMK_CMDID),
    WM_ID_DESC_FMT(WMI_MCAST_FILTER_CMDID, sizeof(WMI_MCAST_FILTER_CMD)),
	/* COEX CMDID AR6003*/
	WM_ID_DESC_FMT(WMI_SET_BTCOEX_FE_ANT_CMDID, sizeof(WMI_SET_BTCOEX_FE_ANT_CMD)),
	WM_ID_DESC(WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMDID),
	WM_ID_DESC_FMT(WMI_SET_BTCOEX_SCO_CONFIG_CMDID, sizeof(WMI_SET_BTCOEX_SCO_CONFIG_CMD)),
	WM_ID_DESC_FMT(WMI_SET_BTCOEX_A2DP_CONFIG_CMDID, sizeof(WMI_SET_BTCOEX_A2DP_CONFIG_CMD)),
	WM_ID_DESC_FMT(WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMDID, sizeof(WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD)),
	WM_ID_DESC_FMT(WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMDID, sizeof(WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD)),
	WM_ID_DESC(WMI_SET_BTCOEX_DEBUG_CMDID),
	WM_ID_DESC_FMT(WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMDID, sizeof(WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD)),
	WM_ID_DESC(WMI_GET_BTCOEX_STATS_CMDID),
	WM_ID_DESC(WMI_GET_BTCOEX_CONFIG_CMDID),

    WM_ID_DESC(WMI_SET_DFS_ENABLE_CMDID),   /* F034 */
    WM_ID_DESC(WMI_SET_DFS_MINRSSITHRESH_CMDID),
    WM_ID_DESC(WMI_SET_DFS_MAXPULSEDUR_CMDID),
    WM_ID_DESC(WMI_DFS_RADAR_DETECTED_CMDID),

    /* P2P CMDS */
    WM_ID_DESC(WMI_P2P_SET_CONFIG_CMDID),    /* F038 */
    WM_ID_DESC(WMI_WPS_SET_CONFIG_CMDID),
    WM_ID_DESC(WMI_SET_REQ_DEV_ATTR_CMDID),
    WM_ID_DESC(WMI_P2P_FIND_CMDID),
    WM_ID_DESC(WMI_P2P_STOP_FIND_CMDID),
    WM_ID_DESC(WMI_P2P_GO_NEG_START_CMDID),
    WM_ID_DESC(WMI_P2P_LISTEN_CMDID),

    WM_ID_DESC(WMI_CONFIG_TX_MAC_RULES_CMDID),
    WM_ID_DESC(WMI_SET_PROMISCUOUS_MODE_CMDID),/* F040 */
    WM_ID_DESC(WMI_RX_FRAME_FILTER_CMDID),
    WM_ID_DESC(WMI_SET_CHANNEL_CMDID),

    /* WAC commands */
    WM_ID_DESC(WMI_ENABLE_WAC_CMDID),
    WM_ID_DESC(WMI_WAC_SCAN_REPLY_CMDID),
    WM_ID_DESC(WMI_WAC_CTRL_REQ_CMDID),
    WM_ID_DESC(WMI_SET_DIV_PARAMS_CMDID),

    WM_ID_DESC(WMI_GET_PMK_CMDID),
    WM_ID_DESC(WMI_SET_PASSPHRASE_CMDID),
    WM_ID_DESC(WMI_SEND_ASSOC_RES_CMDID),
    WM_ID_DESC(WMI_SET_ASSOC_REQ_RELAY_CMDID),
    WM_ID_DESC(WMI_GET_RFKILL_MODE_CMDID),
    WM_ID_DESC(WMI_SET_RFKILL_MODE_CMDID),

    /* ACS command, consists of sub-commands */
    WM_ID_DESC(WMI_ACS_CTRL_CMDID),
    
    /* Ultra low power store / recall commands */
    WM_ID_DESC(WMI_STORERECALL_CONFIGURE_CMDID),
    WM_ID_DESC(WMI_STORERECALL_RECALL_CMDID),
    WM_ID_DESC(WMI_STORERECALL_HOST_READY_CMDID),
    WM_ID_DESC(WMI_FORCE_TARGET_ASSERT_CMDID),
    WM_ID_DESC(WMI_SET_EXCESS_TX_RETRY_THRES_CMDID),

    WM_ID_DESC(WMI_P2P_GO_NEG_REQ_RSP_CMDID),  /* F053 */
    WM_ID_DESC(WMI_P2P_GRP_INIT_CMDID),
    WM_ID_DESC(WMI_P2P_GRP_FORMATION_DONE_CMDID),
    WM_ID_DESC(WMI_P2P_INVITE_CMDID),
    WM_ID_DESC(WMI_P2P_INVITE_REQ_RSP_CMDID),
    WM_ID_DESC(WMI_P2P_PROV_DISC_REQ_CMDID),
    WM_ID_DESC(WMI_P2P_SET_CMDID),

    WM_ID_DESC(WMI_AP_SET_APSD_CMDID),         /* F05A */
    WM_ID_DESC(WMI_AP_APSD_BUFFERED_TRAFFIC_CMDID),

    WM_ID_DESC(WMI_P2P_SDPD_TX_CMDID), /* F05C */
    WM_ID_DESC(WMI_P2P_STOP_SDPD_CMDID),
    WM_ID_DESC(WMI_P2P_CANCEL_CMDID),
};

static int dbg_wmi_cmd_params_pos, dbg_wmi_cmd_params_cmdid;
static A_UINT8 *dbg_wmi_cmd_params_buf;

static int WMI_EVENT_SEND_XTND_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    const char *txt;
    A_INT32 evt = buffer[1];
    switch (evt) {
    CASE_STR(WMIX_DSETOPENREQ_EVENTID, txt);
    CASE_STR(WMIX_DSETCLOSE_EVENTID, txt);
    CASE_STR(WMIX_DSETDATAREQ_EVENTID, txt);
    CASE_STR(WMIX_GPIO_INTR_EVENTID, txt);
    CASE_STR(WMIX_GPIO_DATA_EVENTID, txt);
    CASE_STR(WMIX_GPIO_ACK_EVENTID, txt);
    CASE_STR(WMIX_HB_CHALLENGE_RESP_EVENTID, txt);
    CASE_STR(WMIX_DBGLOG_EVENTID, txt);
    CASE_STR(WMIX_PROF_COUNT_EVENTID, txt);
    CASE_DEF_STR(txt);
    }
    return snprintf(output, len, "%s", txt);
}

static int WMI_EVENT_SEND_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT32 idx = buffer[1];
    A_INT32 sidx;
    if (idx>=(A_INT32)WMI_READY_EVENTID && 
            idx<(A_INT32)(WMI_READY_EVENTID+ARRAY_SIZE(evt_desc))) {
        sidx = idx - WMI_READY_EVENTID;
        return snprintf(output, len, "%s", evt_desc[sidx].desc);
    } else if (idx>=(A_INT32)WMI_SET_BITRATE_CMDID && 
            idx<(A_INT32)(WMI_SET_BITRATE_CMDID+ARRAY_SIZE(cmdxs_desc))) {
        sidx = idx - WMI_SET_BITRATE_CMDID;
        return snprintf(output, len, "%s", cmdxs_desc[sidx].desc);
    }
    return 0;
}


static int WMI_CMD_RX_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT32 idx = buffer[1];
    A_INT32 length = buffer[2];
    A_INT32 sidx;
    if (idx>=(A_INT32)WMI_CONNECT_CMDID && 
            idx<(A_INT32)(WMI_CONNECT_CMDID+ARRAY_SIZE(cmds_desc))) {
        sidx = idx-WMI_CONNECT_CMDID;
        return snprintf(output, len, "%s, len %d", 
                       cmds_desc[sidx].desc, length);
    } else if (idx>=(A_INT32)WMI_SET_BITRATE_CMDID && 
            idx<(A_INT32)(WMI_SET_BITRATE_CMDID+ARRAY_SIZE(cmdxs_desc))) {
        sidx = idx - WMI_SET_BITRATE_CMDID;
        return snprintf(output, len, "%s, len %d", 
                       cmdxs_desc[sidx].desc, length);
    }
    return 0;
}

static int WMI_CMD_PARAMS_DUMP_START_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    A_INT32 paramslen = buffer[2];
    paramslen += (sizeof(A_INT32) * 2); /* adding pad */
    dbg_wmi_cmd_params_pos = 0;
    dbg_wmi_cmd_params_cmdid = buffer[1];
    dbg_wmi_cmd_params_buf = (paramslen>0) ? malloc(paramslen) : NULL;
    return 0;
}

static int WMI_CMD_PARAMS_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    if (dbg_wmi_cmd_params_buf == NULL) {
        /* partial debug log where there is no START. Skip it*/
        return 0;
    }   
    memcpy(&dbg_wmi_cmd_params_buf[dbg_wmi_cmd_params_pos], &buffer[1], sizeof(A_INT32));
    memcpy(&dbg_wmi_cmd_params_buf[dbg_wmi_cmd_params_pos+4], &buffer[2], sizeof(A_INT32));
    dbg_wmi_cmd_params_pos += (sizeof(A_INT32) * 2);
    return 0;
}

static int WMI_CMD_PARAMS_DUMP_END_fmt(char *output, size_t len, A_UINT32 numargs, A_INT32 *buffer)
{
    int ret = 0;
    A_INT32 idx = dbg_wmi_cmd_params_cmdid;
    DbgCmdFormatter cmdFormatter = NULL;
    size_t cmdSize;
    if (dbg_wmi_cmd_params_buf == NULL) {
        /* partial debug log where there is no START. Skip it*/
        return 0;
    }   
    if (idx>=(A_INT32)WMI_CONNECT_CMDID && 
            idx<(A_INT32)(WMI_CONNECT_CMDID+ARRAY_SIZE(cmds_desc)) ) {
        cmdFormatter = cmds_desc[idx-WMI_CONNECT_CMDID].formatter;
        cmdSize = cmds_desc[idx-WMI_CONNECT_CMDID].cmdSize;
    } else if (idx>=(A_INT32)WMI_SET_BITRATE_CMDID && 
            idx<(A_INT32)(WMI_SET_BITRATE_CMDID+ARRAY_SIZE(cmdxs_desc))) {
        cmdFormatter = cmdxs_desc[idx-WMI_SET_BITRATE_CMDID].formatter;
        cmdSize = cmdxs_desc[idx-WMI_SET_BITRATE_CMDID].cmdSize;
    }
    if (cmdFormatter) {
        ret += snprintf(output+ret, len-ret, " ");
        if (dbg_wmi_cmd_params_pos>=cmdSize) {
            ret += cmdFormatter(output+ret, len-ret, dbg_wmi_cmd_params_buf);
        } else {
            ret += snprintf(output+ret, len-ret, "malformed cmd. size too small %d < %d",
                    dbg_wmi_cmd_params_pos, cmdSize);
        }
    }
    dbg_wmi_cmd_params_pos = 0;
    dbg_wmi_cmd_params_cmdid = 0;
    free(dbg_wmi_cmd_params_buf);
    dbg_wmi_cmd_params_buf = NULL;
    return ret;
}

static struct dbglog_desc wmi_desc[] = {
    DBG_DESC(0),
    DBG_DESC(WMI_CMD_RX_XTND_PKT_TOO_SHORT),
    DBG_DESC(WMI_EXTENDED_CMD_NOT_HANDLED),
    DBG_DESC(WMI_CMD_RX_PKT_TOO_SHORT),
    DBG_DESC(WMI_CALLING_WMI_EXTENSION_FN),
    DBG_DESC(WMI_CMD_NOT_HANDLED),
    DBG_DESC(WMI_IN_SYNC),
    DBG_DESC(WMI_TARGET_WMI_SYNC_CMD),
    DBG_DESC(WMI_SET_SNR_THRESHOLD_PARAMS),
    DBG_DESC(WMI_SET_RSSI_THRESHOLD_PARAMS),
    DBG_DESC(WMI_SET_LQ_TRESHOLD_PARAMS),
    DBG_DESC(WMI_TARGET_CREATE_PSTREAM_CMD),
    DBG_DESC(WMI_WI_DTM_INUSE),
    DBG_DESC(WMI_TARGET_DELETE_PSTREAM_CMD),
    DBG_DESC(WMI_TARGET_IMPLICIT_DELETE_PSTREAM_CMD),
    DBG_DESC(WMI_TARGET_GET_BIT_RATE_CMD),
    DBG_DESC(WMI_GET_RATE_MASK_CMD_FIX_RATE_MASK_IS),
    DBG_DESC(WMI_TARGET_GET_AVAILABLE_CHANNELS_CMD),
    DBG_DESC(WMI_TARGET_GET_TX_PWR_CMD),
    DBG_DESC(WMI_FREE_EVBUF_WMIBUF),
    DBG_DESC(WMI_FREE_EVBUF_DATABUF),
    DBG_DESC(WMI_FREE_EVBUF_BADFLAG),
    DBG_DESC(WMI_HTC_RX_ERROR_DATA_PACKET),
    DBG_DESC(WMI_HTC_RX_SYNC_PAUSING_FOR_MBOX),
    DBG_DESC(WMI_INCORRECT_WMI_DATA_HDR_DROPPING_PKT),
    DBG_DESC(WMI_SENDING_READY_EVENT),
    DBG_DESC(WMI_SETPOWER_MDOE_TO_MAXPERF),
    DBG_DESC(WMI_SETPOWER_MDOE_TO_REC),
    DBG_DESC(WMI_BSSINFO_EVENT_FROM),
    DBG_DESC(WMI_TARGET_GET_STATS_CMD),
    DBG_DESC(WMI_SENDING_SCAN_COMPLETE_EVENT),
    DBG_DESC(WMI_SENDING_RSSI_INDB_THRESHOLD_EVENT),
    DBG_DESC(WMI_SENDING_RSSI_INDBM_THRESHOLD_EVENT),
    DBG_DESC(WMI_SENDING_LINK_QUALITY_THRESHOLD_EVENT),
    DBG_DESC(WMI_SENDING_ERROR_REPORT_EVENT),
    DBG_DESC(WMI_SENDING_CAC_EVENT),
    DBG_DESC(WMI_TARGET_GET_ROAM_TABLE_CMD),
    DBG_DESC(WMI_TARGET_GET_ROAM_DATA_CMD),
    DBG_DESC(WMI_SENDING_GPIO_INTR_EVENT),
    DBG_DESC(WMI_SENDING_GPIO_ACK_EVENT),
    DBG_DESC(WMI_SENDING_GPIO_DATA_EVENT),
    DBG_DESC_FMT(WMI_CMD_RX),
    DBG_DESC(WMI_CMD_RX_XTND),
    DBG_DESC_FMT(WMI_EVENT_SEND),
    DBG_DESC_FMT(WMI_EVENT_SEND_XTND),
    DBG_DESC_FMT(WMI_CMD_PARAMS_DUMP_START),
    DBG_DESC_FMT(WMI_CMD_PARAMS_DUMP_END),
    DBG_DESC_FMT(WMI_CMD_PARAMS),
};

static struct dbglog_desc co_desc[] = {
    DBG_DESC(0),
    DBG_DESC(CO_INIT),
    DBG_DESC(CO_ACQUIRE_LOCK),
    DBG_DESC(CO_START_OP1),
    DBG_DESC(CO_START_OP2),
    DBG_DESC(CO_DRAIN_TX_COMPLETE_CB),
    DBG_DESC(CO_CHANGE_CHANNEL_CB),
    DBG_DESC(CO_RETURN_TO_HOME_CHANNEL),
    DBG_DESC(CO_FINISH_OP_TIMEOUT),
    DBG_DESC(CO_OP_END),
    DBG_DESC(CO_CANCEL_OP),
    DBG_DESC_FMT(CO_CHANGE_CHANNEL),
    DBG_DESC(CO_RELEASE_LOCK),
    DBG_DESC_FMT(CO_CHANGE_STATE),
};

static struct dbglog_desc mgmtbuf_desc[] = {
    DBG_DESC(0),
    DBG_DESC(TXRX_MGMTBUF_ALLOCATE_BUF),
    DBG_DESC(TXRX_MGMTBUF_ALLOCATE_SM_BUF),
    DBG_DESC(TXRX_MGMTBUF_ALLOCATE_RMBUF),
    DBG_DESC(TXRX_MGMTBUF_GET_BUF),
    DBG_DESC(TXRX_MGMTBUF_GET_SM_BUF),
    DBG_DESC(TXRX_MGMTBUF_QUEUE_BUF_TO_TXQ),
    DBG_DESC_FMT(TXRX_MGMTBUF_REAPED_BUF),
    DBG_DESC(TXRX_MGMTBUF_REAPED_SM_BUF),
    DBG_DESC_FMT(TXRX_MGMTBUF_WAIT_FOR_TXQ_DRAIN),
    DBG_DESC(TXRX_MGMTBUF_WAIT_FOR_TXQ_SFQ_DRAIN),
    DBG_DESC(TXRX_MGMTBUF_ENQUEUE_INTO_DATA_SFQ),
    DBG_DESC(TXRX_MGMTBUF_DEQUEUE_FROM_DATA_SFQ),
    DBG_DESC(TXRX_MGMTBUF_PAUSE_DATA_TXQ),
    DBG_DESC(TXRX_MGMTBUF_RESUME_DATA_TXQ),
    DBG_DESC(TXRX_MGMTBUF_WAIT_FORTXQ_DRAIN_TIMEOUT),
    DBG_DESC_FMT(TXRX_MGMTBUF_DRAINQ),
    DBG_DESC(TXRX_MGMTBUF_INDICATE_Q_DRAINED),
    DBG_DESC(TXRX_MGMTBUF_ENQUEUE_INTO_HW_SFQ),
    DBG_DESC(TXRX_MGMTBUF_DEQUEUE_FROM_HW_SFQ),
    DBG_DESC(TXRX_MGMTBUF_PAUSE_HW_TXQ),
    DBG_DESC(TXRX_MGMTBUF_RESUME_HW_TXQ),
    DBG_DESC(TXRX_MGMTBUF_TEAR_DOWN_BA),
    DBG_DESC(TXRX_MGMTBUF_PROCESS_ADDBA_REQ),
    DBG_DESC(TXRX_MGMTBUF_PROCESS_DELBA),
    DBG_DESC(TXRX_MGMTBUF_PERFORM_BA),
    DBG_DESC_FMT(TXRX_MGMTBUF_WLAN_RESET_ON_ERROR),
};

static struct dbglog_desc dc_desc[] = {
    DBG_DESC(0),
    DBG_DESC_FMT(DC_SCAN_CHAN_START),
    DBG_DESC_FMT(DC_SCAN_CHAN_FINISH),
    DBG_DESC(DC_BEACON_RECEIVE7),
    DBG_DESC_FMT(DC_SSID_PROBE_CB),
    DBG_DESC_FMT(DC_SEND_NEXT_SSID_PROBE),
    DBG_DESC_FMT(DC_START_SEARCH),
    DBG_DESC(DC_CANCEL_SEARCH_CB),
    DBG_DESC(DC_STOP_SEARCH),
    DBG_DESC(DC_END_SEARCH),
    DBG_DESC(DC_MIN_CHDWELL_TIMEOUT),
    DBG_DESC(DC_START_SEARCH_CANCELED),
    DBG_DESC_FMT(DC_SET_POWER_MODE),
    DBG_DESC(DC_INIT),
    DBG_DESC_FMT(DC_SEARCH_OPPORTUNITY),
    DBG_DESC_FMT(DC_RECEIVED_ANY_BEACON),
    DBG_DESC(DC_RECEIVED_MY_BEACON),
    DBG_DESC(DC_PROFILE_IS_ADHOC_BUT_BSS_IS_INFRA),
    DBG_DESC(DC_PS_ENABLED_BUT_ATHEROS_IE_ABSENT),
    DBG_DESC(DC_BSS_ADHOC_CHANNEL_NOT_ALLOWED),
    DBG_DESC(DC_SET_BEACON_UPDATE),
    DBG_DESC(DC_BEACON_UPDATE_COMPLETE),
    DBG_DESC(DC_END_SEARCH_BEACON_UPDATE_COMP_CB),
    DBG_DESC(DC_BSSINFO_EVENT_DROPPED),
    DBG_DESC(DC_IEEEPS_ENABLED_BUT_ATIM_ABSENT),
};

static struct dbglog_desc btcoex_desc[] = {
    DBG_DESC(0),
    DBG_DESC(BTCOEX_STATUS_CMD),
    DBG_DESC(BTCOEX_PARAMS_CMD),
    DBG_DESC(BTCOEX_ANT_CONFIG),
    DBG_DESC(BTCOEX_COLOCATED_BT_DEVICE),
    DBG_DESC(BTCOEX_CLOSE_RANGE_SCO_ON),
    DBG_DESC(BTCOEX_CLOSE_RANGE_SCO_OFF),
    DBG_DESC(BTCOEX_CLOSE_RANGE_A2DP_ON),
    DBG_DESC(BTCOEX_CLOSE_RANGE_A2DP_OFF),
    DBG_DESC(BTCOEX_A2DP_PROTECT_ON),
    DBG_DESC(BTCOEX_A2DP_PROTECT_OFF),
    DBG_DESC(BTCOEX_SCO_PROTECT_ON),
    DBG_DESC(BTCOEX_SCO_PROTECT_OFF),
    DBG_DESC(BTCOEX_CLOSE_RANGE_DETECTOR_START),
    DBG_DESC(BTCOEX_CLOSE_RANGE_DETECTOR_STOP),
    DBG_DESC(BTCOEX_CLOSE_RANGE_TOGGLE),
    DBG_DESC(BTCOEX_CLOSE_RANGE_TOGGLE_RSSI_LRCNT),
    DBG_DESC(BTCOEX_CLOSE_RANGE_RSSI_THRESH),
    DBG_DESC(BTCOEX_CLOSE_RANGE_LOW_RATE_THRESH),
    DBG_DESC(BTCOEX_PTA_PRI_INTR_HANDLER),
    DBG_DESC_FMT(BTCOEX_PSPOLL_QUEUED),
    DBG_DESC_FMT(BTCOEX_PSPOLL_COMPLETE),
    DBG_DESC_FMT(BTCOEX_DBG_PM_AWAKE),
    DBG_DESC_FMT(BTCOEX_DBG_PM_SLEEP),
    DBG_DESC(BTCOEX_DBG_SCO_COEX_ON),
    DBG_DESC(BTCOEX_SCO_DATARECEIVE),
    DBG_DESC(BTCOEX_INTR_INIT),
    DBG_DESC(BTCOEX_PTA_PRI_DIFF),
    DBG_DESC_FMT(BTCOEX_TIM_NOTIFICATION),
    DBG_DESC(BTCOEX_SCO_WAKEUP_ON_DATA),
    DBG_DESC(BTCOEX_SCO_SLEEP),
    DBG_DESC_FMT(BTCOEX_SET_WEIGHTS),
    DBG_DESC(BTCOEX_SCO_DATARECEIVE_LATENCY_VAL),
    DBG_DESC(BTCOEX_SCO_MEASURE_TIME_DIFF),
    DBG_DESC(BTCOEX_SET_EOL_VAL),
    DBG_DESC(BTCOEX_OPT_DETECT_HANDLER),
    DBG_DESC(BTCOEX_SCO_TOGGLE_STATE),
    DBG_DESC(BTCOEX_SCO_STOMP),
    DBG_DESC(BTCOEX_NULL_COMP_CALLBACK),
    DBG_DESC(BTCOEX_RX_INCOMING),
    DBG_DESC(BTCOEX_RX_INCOMING_CTL),
    DBG_DESC(BTCOEX_RX_INCOMING_MGMT),
    DBG_DESC(BTCOEX_RX_INCOMING_DATA),
    DBG_DESC(BTCOEX_RTS_RECEPTION),
    DBG_DESC(BTCOEX_FRAME_PRI_LOW_RATE_THRES),
    DBG_DESC_FMT(BTCOEX_PM_FAKE_SLEEP),
    DBG_DESC_FMT(BTCOEX_ACL_COEX_STATUS),
    DBG_DESC(BTCOEX_ACL_COEX_DETECTION),
    DBG_DESC(BTCOEX_A2DP_COEX_STATUS),
    DBG_DESC(BTCOEX_SCO_STATUS),
    DBG_DESC_FMT(BTCOEX_WAKEUP_ON_DATA),
    DBG_DESC(BTCOEX_DATARECEIVE),
    DBG_DESC(0),
    DBG_DESC(BTCOEX_GET_MAX_AGGR_SIZE),
    DBG_DESC(BTCOEX_MAX_AGGR_AVAIL_TIME),
    DBG_DESC(BTCOEX_DBG_WBTIMER_INTR),
    DBG_DESC(0),
    DBG_DESC(BTCOEX_DBG_SCO_SYNC),
    DBG_DESC(0),
    DBG_DESC(BTCOEX_UPLINK_QUEUED_RATE),
    DBG_DESC(BTCOEX_DBG_UPLINK_ENABLE_EOL),
    DBG_DESC(BTCOEX_UPLINK_FRAME_DURATION),
    DBG_DESC(BTCOEX_UPLINK_SET_EOL),
    DBG_DESC(BTCOEX_DBG_EOL_EXPIRED),
    DBG_DESC(BTCOEX_DBG_DATA_COMPLETE),
    DBG_DESC(BTCOEX_UPLINK_QUEUED_TIMESTAMP),
    DBG_DESC(BTCOEX_DBG_DATA_COMPLETE_TIME),
    DBG_DESC(BTCOEX_DBG_A2DP_ROLE_IS_SLAVE),
    DBG_DESC(BTCOEX_DBG_A2DP_ROLE_IS_MASTER),
    DBG_DESC(BTCOEX_DBG_UPLINK_SEQ_NUM),
    DBG_DESC(BTCOEX_UPLINK_AGGR_SEQ),
    DBG_DESC(BTCOEX_DBG_TX_COMP_SEQ_NO),
    DBG_DESC(BTCOEX_DBG_MAX_AGGR_PAUSE_STATE),
    DBG_DESC(BTCOEX_DBG_ACL_TRAFFIC),
    DBG_DESC(BTCOEX_CURR_AGGR_PROP),
    DBG_DESC(BTCOEX_DBG_SCO_GET_PER_TIME_DIFF),
    DBG_DESC(BTCOEX_PSPOLL_PROCESS),
    DBG_DESC(BTCOEX_RETURN_FROM_MAC),
    DBG_DESC(BTCOEX_FREED_REQUEUED_CNT),
    DBG_DESC(BTCOEX_DBG_TOGGLE_LOW_RATES),
    DBG_DESC(BTCOEX_MAC_GOES_TO_SLEEP),
    DBG_DESC(BTCOEX_DBG_A2DP_NO_SYNC),
    DBG_DESC(BTCOEX_RETURN_FROM_MAC_HOLD_Q_INFO),
    DBG_DESC(BTCOEX_RETURN_FROM_MAC_AC),
    DBG_DESC(BTCOEX_DBG_DTIM_RECV),
    DBG_DESC(0),
    DBG_DESC(BTCOEX_IS_PRE_UPDATE),
    DBG_DESC(BTCOEX_ENQUEUED_BIT_MAP),
    DBG_DESC(BTCOEX_TX_COMPLETE_FIRST_DESC_STATS),
    DBG_DESC(BTCOEX_UPLINK_DESC),
    DBG_DESC(BTCOEX_SCO_GET_PER_FIRST_FRM_TIMESTAMP),
    DBG_DESC(0), DBG_DESC(0), DBG_DESC(0),
    DBG_DESC(BTCOEX_DBG_RECV_ACK),
    DBG_DESC(BTCOEX_DBG_ADDBA_INDICATION),
    DBG_DESC(BTCOEX_TX_COMPLETE_EOL_FAILED),
    DBG_DESC(BTCOEX_DBG_A2DP_USAGE_COMPLETE),
    DBG_DESC(BTCOEX_DBG_A2DP_STOMP_FOR_BCN_HANDLER),
    DBG_DESC(BTCOEX_DBG_A2DP_SYNC_INTR),
    DBG_DESC(BTCOEX_DBG_A2DP_STOMP_FOR_BCN_RECEPTION),
    DBG_DESC(BTCOEX_FORM_AGGR_CURR_AGGR),
    DBG_DESC(BTCOEX_DBG_TOGGLE_A2DP_BURST_CNT),
    DBG_DESC(BTCOEX_DBG_BT_TRAFFIC),
    DBG_DESC(BTCOEX_DBG_STOMP_BT_TRAFFIC),
    DBG_DESC(BTCOEX_RECV_NULL),
    DBG_DESC(BTCOEX_DBG_A2DP_MASTER_BT_END),
    DBG_DESC(BTCOEX_DBG_A2DP_BT_START),
    DBG_DESC(BTCOEX_DBG_A2DP_SLAVE_BT_END),
    DBG_DESC(BTCOEX_DBG_A2DP_STOMP_BT),
    DBG_DESC_FMT(BTCOEX_DBG_GO_TO_SLEEP),
    DBG_DESC(BTCOEX_DBG_A2DP_PKT),
    DBG_DESC(BTCOEX_DBG_A2DP_PSPOLL_DATA_RECV),
    DBG_DESC(BTCOEX_DBG_A2DP_NULL),
    DBG_DESC(BTCOEX_DBG_UPLINK_DATA),
    DBG_DESC(BTCOEX_DBG_A2DP_STOMP_LOW_PRIO_NULL),
    DBG_DESC(BTCOEX_DBG_ADD_BA_RESP_TIMEOUT),
    DBG_DESC(BTCOEX_DBG_TXQ_STATE),
    DBG_DESC_FMT(BTCOEX_DBG_ALLOW_SCAN),
    DBG_DESC_FMT(BTCOEX_DBG_SCAN_REQUEST),
    DBG_DESC(0), DBG_DESC(0), DBG_DESC(0), DBG_DESC(0), DBG_DESC(0), DBG_DESC(0), DBG_DESC(0),
    DBG_DESC(BTCOEX_A2DP_SLEEP),
    DBG_DESC(BTCOEX_DBG_DATA_ACTIV_TIMEOUT),
    DBG_DESC(BTCOEX_DBG_SWITCH_TO_PSPOLL_ON_MODE),
    DBG_DESC(BTCOEX_DBG_SWITCH_TO_PSPOLL_OFF_MODE),
    DBG_DESC(BTCOEX_DATARECEIVE_AGGR),
    DBG_DESC(BTCOEX_DBG_DATA_RECV_SLEEPING_PENDING),
    DBG_DESC(BTCOEX_DBG_DATARESP_TIMEOUT),
    DBG_DESC(BTCOEX_BDG_BMISS),
    DBG_DESC(BTCOEX_DBG_DATA_RECV_WAKEUP_TIM),
    DBG_DESC(BTCOEX_DBG_SECOND_BMISS),
    DBG_DESC(0), 
    DBG_DESC_FMT(BTCOEX_DBG_SET_WLAN_STATE),
    DBG_DESC(BTCOEX_BDG_FIRST_BMISS),
    DBG_DESC(BTCOEX_DBG_A2DP_CHAN_OP),
    DBG_DESC(BTCOEX_DBG_A2DP_INTR),
    DBG_DESC_FMT(BTCOEX_DBG_BT_INQUIRY),
    DBG_DESC(BTCOEX_DBG_BT_INQUIRY_DATA_FETCH),
    DBG_DESC(BTCOEX_DBG_POST_INQUIRY_FINISH),
    DBG_DESC(BTCOEX_DBG_SCO_OPT_MODE_TIMER_HANDLER),
    DBG_DESC(BTCOEX_DBG_NULL_FRAME_SLEEP),
    DBG_DESC(BTCOEX_DBG_NULL_FRAME_AWAKE),
    DBG_DESC(0), DBG_DESC(0), DBG_DESC(0), DBG_DESC(0), 
    DBG_DESC(BTCOEX_DBG_SET_AGGR_SIZE),
    DBG_DESC(BTCOEX_DBG_TEAR_BA_TIMEOUT),
    DBG_DESC(BTCOEX_DBG_MGMT_FRAME_SEQ_NO),
    DBG_DESC(BTCOEX_DBG_SCO_STOMP_HIGH_PRI),
    DBG_DESC(BTCOEX_DBG_COLOCATED_BT_DEV),
    DBG_DESC(BTCOEX_DBG_FE_ANT_TYPE),
    DBG_DESC(BTCOEX_DBG_BT_INQUIRY_CMD),
    DBG_DESC(BTCOEX_DBG_SCO_CONFIG),
    DBG_DESC(BTCOEX_DBG_SCO_PSPOLL_CONFIG),
    DBG_DESC(BTCOEX_DBG_SCO_OPTMODE_CONFIG),
    DBG_DESC(BTCOEX_DBG_A2DP_CONFIG),
    DBG_DESC(BTCOEX_DBG_A2DP_PSPOLL_CONFIG),
    DBG_DESC(BTCOEX_DBG_A2DP_OPTMODE_CONFIG),
    DBG_DESC(BTCOEX_DBG_ACLCOEX_CONFIG),
    DBG_DESC(BTCOEX_DBG_ACLCOEX_PSPOLL_CONFIG),
    DBG_DESC(BTCOEX_DBG_ACLCOEX_OPTMODE_CONFIG),
    DBG_DESC(BTCOEX_DBG_DEBUG_CMD),
    DBG_DESC(BTCOEX_DBG_SET_BT_OPERATING_STATUS),
    DBG_DESC(BTCOEX_DBG_GET_CONFIG),
    DBG_DESC(BTCOEX_DBG_GET_STATS),
    DBG_DESC(BTCOEX_DBG_BT_OPERATING_STATUS),
    DBG_DESC(BTCOEX_DBG_PERFORM_RECONNECT),
    DBG_DESC(0), 
    DBG_DESC(BTCOEX_DBG_ACL_WLAN_MED),
    DBG_DESC(BTCOEX_DBG_ACL_BT_MED),
    DBG_DESC(BTCOEX_DBG_WLAN_CONNECT),
    DBG_DESC(BTCOEX_DBG_A2DP_DUAL_START),
    DBG_DESC(BTCOEX_DBG_PMAWAKE_NOTIFY),
    DBG_DESC(BTCOEX_DBG_BEACON_SCAN_ENABLE),
    DBG_DESC(BTCOEX_DBG_BEACON_SCAN_DISABLE),
    DBG_DESC(BTCOEX_DBG_RX_NOTIFY),
    DBG_DESC(BTCOEX_SCO_GET_PER_SECOND_FRM_TIMESTAMP),
    DBG_DESC(BTCOEX_DBG_TXQ_DETAILS),
    DBG_DESC(BTCOEX_DBG_SCO_STOMP_LOW_PRI),
    DBG_DESC(BTCOEX_DBG_A2DP_FORCE_SCAN),
    DBG_DESC(BTCOEX_DBG_DTIM_STOMP_COMP),
    DBG_DESC(BTCOEX_ACL_PRESENCE_TIMER),
    DBG_DESC(BTCOEX_DBG_QUEUE_SELF_CTS),
    DBG_DESC(BTCOEX_DBG_SELF_CTS_COMP),
    DBG_DESC(BTCOEX_DBG_APMODE_WAIT_FOR_CTS_COMP_FAILED),
    DBG_DESC(BTCOEX_DBG_APMODE_A2DP_MED_TO_BT),
    DBG_DESC(BTCOEX_DBG_APMODE_SET_BTSTATE),
    DBG_DESC(BTCOEX_DBG_APMODE_A2DP_STATUS),
    DBG_DESC(BTCOEX_DBG_APMODE_SCO_CTS_HANDLER),
    DBG_DESC(BTCOEX_DBG_APMODE_SCO_STATUS),
    DBG_DESC(BTCOEX_DBG_APMODE_TXQ_DRAINED),
    DBG_DESC(BTCOEX_DBG_APMODE_SCO_ARM_TIMER),
    DBG_DESC(BTCOEX_DBG_APMODE_SWITCH_MED_TO_WLAN),
    DBG_DESC(BTCOEX_APMODE_BCN_TX_HANDLER),
    DBG_DESC(BTCOEX_APMODE_BCN_TX),
    DBG_DESC(BTCOEX_APMODE_SCO_RTS_HANDLER),
};

static struct dbglog_desc pm_desc[] = {
    DBG_DESC(0),
    DBG_DESC(PM_INIT), 
    DBG_DESC(PM_ENABLE),
    DBG_DESC_FMT(PM_SET_STATE),
    DBG_DESC_FMT(PM_SET_POWERMODE),
    DBG_DESC(PM_CONN_NOTIFY),
    DBG_DESC(PM_REF_COUNT_NEGATIVE),
    DBG_DESC(PM_INFRA_STA_APSD_ENABLE),
    DBG_DESC(PM_INFRA_STA_UPDATE_APSD_STATE),
    DBG_DESC_FMT(PM_CHAN_OP_REQ),
    DBG_DESC_FMT(PM_SET_MY_BEACON_POLICY),
    DBG_DESC_FMT(PM_SET_ALL_BEACON_POLICY),
    DBG_DESC(PM_INFRA_STA_SET_PM_PARAMS1),
    DBG_DESC(PM_INFRA_STA_SET_PM_PARAMS2),
    DBG_DESC(PM_ADHOC_SET_PM_CAPS_FAIL),
    DBG_DESC(PM_ADHOC_UNKNOWN_IBSS_ATTRIB_ID),
    DBG_DESC(PM_ADHOC_SET_PM_PARAMS),
    DBG_DESC(0),
    DBG_DESC(PM_ADHOC_STATE1),
    DBG_DESC(PM_ADHOC_STATE2),
    DBG_DESC(PM_ADHOC_CONN_MAP),
    DBG_DESC_FMT(PM_FAKE_SLEEP),
    DBG_DESC(PM_AP_STATE1),
    DBG_DESC(PM_AP_SET_PM_PARAMS),
    DBG_DESC(PM_P2P_STATE1),
};

static struct dbglog_desc dummy_desc[] = {
    DBG_DESC(0),
};

static struct module_desc modules[] = {
    MODULE_DESC(INF, dummy_desc),
    MODULE_DESC(WMI, wmi_desc),
    MODULE_DESC(MISC, dummy_desc),
    MODULE_DESC(PM, pm_desc),
    MODULE_DESC(TXRX_MGMTBUF, mgmtbuf_desc),
    MODULE_DESC(TXRX_TXBUF, dummy_desc),
    MODULE_DESC(TXRX_RXBUF, dummy_desc),
    MODULE_DESC(WOW, dummy_desc),
    MODULE_DESC(WHAL, dummy_desc),
    MODULE_DESC(DC, dc_desc),
    MODULE_DESC(CO, co_desc),
    MODULE_DESC(RO, dummy_desc),
    MODULE_DESC(CM, dummy_desc),
    MODULE_DESC(MGMT, dummy_desc),
    MODULE_DESC(TMR, dummy_desc),
    MODULE_DESC(BTCOEX, btcoex_desc),
};

static int compOid(const void *a, const void *b)
{
    return *(A_INT32*)a  - *(A_INT32*)b;
}

static void do_check_ids()
{
    size_t m, d, td;
    for (m=0; dbglog_id_tag[m][1][0]!='\0'; ++m) {
        size_t mlen;
        struct dbglog_desc *dlog;
        if (m >= ARRAY_SIZE(modules)) {
            printf("module does not matched\n");
            break;
        }
        dlog = modules[m].descs;
        mlen = modules[m].len;
        d=td=1;
        while (dlog && (d<mlen||dbglog_id_tag[m][td][0]!='\0')) {       
            if (d>=mlen) {
                if (dlog != dummy_desc) {
                    printf("m %s dbgid %s(%d) is larger than max internal table %d host/firmware mismatch?\n", 
                            modules[m].name, dbglog_id_tag[m][td], td, mlen);
                }                
                break;
            }

            if (dbglog_id_tag[m][td][0]=='\0' && dlog[d].oid!=0) {
                for (;td < DBGLOG_DBGID_NUM_MAX && dbglog_id_tag[m][td][0]=='\0'; ++td);
            }
            if (strcmp(dbglog_id_tag[m][td], dlog[d].desc)!=0 && dlog[d].oid!=0) {
                printf("debug id does not matched '%s' <> '%s'\n", dbglog_id_tag[m][td], dlog[d].desc);
                break;
            } else {
                if (td!=d && !modules[m].bsearch) {                    
                    printf("Module %s debugid %s(%d) mismatched. using binary search for debugid",
                           modules[m].name, dbglog_id_tag[m][td], td); 
                    modules[m].bsearch = 1;
                }
            }
            ++d; ++td;
        }
        if (modules[m].bsearch && dlog) {
            qsort(dlog, mlen, sizeof(dlog[0]), compOid);
        }
    }
    check_ids = 1;
}

static DbgLogFormatter getFormatter(A_INT32 moduleid, A_INT32 debugid)
{
    if (!check_ids) {
        do_check_ids();
    }

    if ( moduleid < ARRAY_SIZE(modules)) {
        const struct module_desc *m = &modules[moduleid];
        if (m->descs ) {
            if (m->bsearch){
                struct dbglog_desc *d;
                d = (struct dbglog_desc*)bsearch(&debugid, m->descs, m->len, sizeof(m->descs[0]), compOid);
                return d ? d->formatter : NULL;
            } else if (debugid>0 && debugid<m->len) {
                return m->descs[debugid].formatter;   
            }
        }
    }
    return NULL;
}

int dbg_formater(int lv, char *output, size_t len, A_UINT32 ts, A_INT32 *logbuf)
{
    int ret = 0;
    A_UINT32 debugid = DBGLOG_GET_DBGID(logbuf[0]);
    A_UINT32 moduleid = DBGLOG_GET_MODULEID(logbuf[0]);
    A_UINT32 numargs = DBGLOG_GET_NUMARGS(logbuf[0]);
    A_UINT32 timestamp = DBGLOG_GET_TIMESTAMP(logbuf[0]);
    DbgLogFormatter dbgFormatter = NULL;

    if (numargs>2) {
        return ret;
    }

    if (lv > 0) {
        dbgFormatter = getFormatter(moduleid, debugid);
    }

    if (ts>0) {
        if (lv == 0) {
            ret += snprintf(output+ret, len-ret, "%8d: ", ts);
        } else {
            ret += strftime(output+ret, len-ret, "%m-%d %H:%M:%S ", gmtime((time_t*)&ts));
        }
    }
    ret += snprintf(output+ret, len-ret, "%s (%d)", dbglog_id_tag[moduleid][debugid],
                    timestamp);
    if (lv>1 || lv == 0 || !dbgFormatter) {
        if (numargs>0) {
            ret += snprintf(output+ret, len-ret, ": 0x%x", logbuf[1]);
        }
        if (numargs>1) {
            ret += snprintf(output+ret, len-ret, ", 0x%x", logbuf[2]);
        }
        if (dbgFormatter && numargs>0 && lv > 0) {
            ret += snprintf(output+ret, len-ret, ", ");
        }
    } else {
        ret += snprintf(output+ret, len-ret, ": ");
    }

    if (dbgFormatter && lv > 0) {
        int pos = ret;
        int addlen;
        addlen = dbgFormatter(output+ret, len-ret, numargs, logbuf);
        if (addlen>0) {
            ret += addlen;
        } else if (lv<=1) {
            /* skip this message */
            (void)pos;
            return 0;
        }
    }
    ret += snprintf(output+ret, len-ret, "\n");
    return ret;
}

