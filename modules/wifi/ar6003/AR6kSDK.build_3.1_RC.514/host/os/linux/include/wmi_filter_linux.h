//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
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
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _WMI_FILTER_LINUX_H_
#define  _WMI_FILTER_LINUX_H_

/*
 * sioctl_filter - Standard ioctl
 * pioctl_filter - Priv ioctl
 * xioctl_filter - eXtended ioctl
 *
 * ---- Possible values for the WMI filter ---------------
 * (0) - Block this cmd always (or) not implemented
 * (INFRA_NETWORK) - Allow this cmd only in STA mode
 * (ADHOC_NETWORK) - Allow this cmd only in IBSS mode
 * (AP_NETWORK) -    Allow this cmd only in AP mode
 * (INFRA_NETWORK | ADHOC_NETWORK) - Block this cmd in AP mode
 * (ADHOC_NETWORK | AP_NETWORK) -    Block this cmd in STA mode
 * (INFRA_NETWORK | AP_NETWORK) -    Block this cmd in IBSS mode
 * (INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK)- allow only when mode is set
 * (0xFF) - Allow this cmd always irrespective of mode
 */

A_UINT8 sioctl_filter[] = {
(AP_NETWORK),                                   /* SIOCSIWCOMMIT   0x8B00   */
(0xFF),                                         /* SIOCGIWNAME     0x8B01   */
(0),                                            /* SIOCSIWNWID     0x8B02   */
(0),                                            /* SIOCGIWNWID     0x8B03   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWFREQ     0x8B04   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWFREQ     0x8B05   */
(0xFF),                                         /* SIOCSIWMODE     0x8B06   */
(0xFF),                                         /* SIOCGIWMODE     0x8B07   */
(0),                                            /* SIOCSIWSENS     0x8B08   */
(0),                                            /* SIOCGIWSENS     0x8B09   */
(0),                                            /* SIOCSIWRANGE    0x8B0A   */
(0xFF),                                         /* SIOCGIWRANGE    0x8B0B   */
(0),                                            /* SIOCSIWPRIV     0x8B0C   */
(0),                                            /* SIOCGIWPRIV     0x8B0D   */
(0),                                            /* SIOCSIWSTATS    0x8B0E   */
(0),                                            /* SIOCGIWSTATS    0x8B0F   */
(0),                                            /* SIOCSIWSPY      0x8B10   */
(0),                                            /* SIOCGIWSPY      0x8B11   */
(0),                                            /* SIOCSIWTHRSPY   0x8B12   */
(0),                                            /* SIOCGIWTHRSPY   0x8B13   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWAP       0x8B14   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWAP       0x8B15   */
#if (WIRELESS_EXT >= 18)
(INFRA_NETWORK | ADHOC_NETWORK),                /* SIOCSIWMLME     0X8B16   */
#else
(0),                                            /* Dummy           0        */
#endif /* WIRELESS_EXT */
(0),                                            /* SIOCGIWAPLIST   0x8B17   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* SIOCSIWSCAN     0x8B18   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* SIOCGIWSCAN     0x8B19   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWESSID    0x8B1A   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWESSID    0x8B1B   */
(0),                                            /* SIOCSIWNICKN    0x8B1C   */
(0),                                            /* SIOCGIWNICKN    0x8B1D   */
(0),                                            /* Dummy           0        */
(0),                                            /* Dummy           0        */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWRATE     0x8B20   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWRATE     0x8B21   */
(0),                                            /* SIOCSIWRTS      0x8B22   */
(0),                                            /* SIOCGIWRTS      0x8B23   */
(0),                                            /* SIOCSIWFRAG     0x8B24   */
(0),                                            /* SIOCGIWFRAG     0x8B25   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWTXPOW    0x8B26   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWTXPOW    0x8B27   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* SIOCSIWRETRY    0x8B28   */
(INFRA_NETWORK | ADHOC_NETWORK),                /* SIOCGIWRETRY    0x8B29   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWENCODE   0x8B2A   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWENCODE   0x8B2B   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCSIWPOWER    0x8B2C   */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* SIOCGIWPOWER    0x8B2D   */
};



A_UINT8 pioctl_filter[] = {
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* IEEE80211_IOCTL_SETPARAM             (SIOCIWFIRSTPRIV+0)     */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* IEEE80211_IOCTL_SETKEY               (SIOCIWFIRSTPRIV+1)     */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* IEEE80211_IOCTL_DELKEY               (SIOCIWFIRSTPRIV+2)     */
(AP_NETWORK),                                   /* IEEE80211_IOCTL_SETMLME              (SIOCIWFIRSTPRIV+3)     */
(INFRA_NETWORK),                                /* IEEE80211_IOCTL_ADDPMKID             (SIOCIWFIRSTPRIV+4)     */
(0),                                            /* IEEE80211_IOCTL_SETOPTIE             (SIOCIWFIRSTPRIV+5)     */
(0),                                            /*                                      (SIOCIWFIRSTPRIV+6)     */
(0),                                            /*                                      (SIOCIWFIRSTPRIV+7)     */
(0),                                            /*                                      (SIOCIWFIRSTPRIV+8)     */
(0),                                            /*                                      (SIOCIWFIRSTPRIV+9)     */
(0),                                            /* IEEE80211_IOCTL_LASTONE              (SIOCIWFIRSTPRIV+10)    */
(0xFF),                                         /* AR6000_IOCTL_WMI_GETREV              (SIOCIWFIRSTPRIV+11)    */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_IOCTL_WMI_SETPWR              (SIOCIWFIRSTPRIV+12)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SETSCAN             (SIOCIWFIRSTPRIV+13)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SETLISTENINT        (SIOCIWFIRSTPRIV+14)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SETBSSFILTER        (SIOCIWFIRSTPRIV+15)    */
(INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_IOCTL_WMI_SET_CHANNELPARAMS   (SIOCIWFIRSTPRIV+16)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_PROBEDSSID      (SIOCIWFIRSTPRIV+17)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_PMPARAMS        (SIOCIWFIRSTPRIV+18)    */
(INFRA_NETWORK),                                /* AR6000_IOCTL_WMI_SET_BADAP           (SIOCIWFIRSTPRIV+19)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_GET_QOS_QUEUE       (SIOCIWFIRSTPRIV+20)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_CREATE_QOS          (SIOCIWFIRSTPRIV+21)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_DELETE_QOS          (SIOCIWFIRSTPRIV+22)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_SNRTHRESHOLD    (SIOCIWFIRSTPRIV+23)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_ERROR_REPORT_BITMASK (SIOCIWFIRSTPRIV+24)*/
(0xFF),                                         /* AR6000_IOCTL_WMI_GET_TARGET_STATS    (SIOCIWFIRSTPRIV+25)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_ASSOC_INFO      (SIOCIWFIRSTPRIV+26)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_ACCESS_PARAMS   (SIOCIWFIRSTPRIV+27)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_BMISS_TIME      (SIOCIWFIRSTPRIV+28)    */
(INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_IOCTL_WMI_SET_DISC_TIMEOUT    (SIOCIWFIRSTPRIV+29)    */
(ADHOC_NETWORK),                                /* AR6000_IOCTL_WMI_SET_IBSS_PM_CAPS    (SIOCIWFIRSTPRIV+30)    */
};

/* Submode for the sake of filtering XIOCTLs are broadly set to 2 types.
 * P2P Submode & Non-P2P submode. IOCLT cmds can be marked to be valid only in 
 * P2P Submode or Non-P2P submode or both. The bits- b5,b6,b7 are used to encode
 * this information in the IOCTL filters. The LSBits b0-b4 are used to encode
 * the mode information.
 */
#define XIOCTL_FILTER_P2P_SUBMODE 0x20
#define XIOCTL_FILTER_NONP2P_SUBMODE 0x40
#define XIOCTL_FILTER_SUBMODE_DONTCARE \
             (XIOCTL_FILTER_P2P_SUBMODE | XIOCTL_FILTER_NONP2P_SUBMODE)

A_UINT8 xioctl_filter[] = {
(0xFF),                                         /* Dummy                                           0    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_DONE                          1    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_READ_MEMORY                   2    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_WRITE_MEMORY                  3    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_EXECUTE                       4    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_SET_APP_START                 5    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_READ_SOC_REGISTER             6    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_WRITE_SOC_REGISTER            7    */
(0xFF),                                         /* AR6000_XIOCTL_BMI_TEST                          8    */
(0xFF),                                         /* AR6000_XIOCTL_UNUSED9                           9    */
(0xFF),                                         /* AR6000_XIOCTL_UNUSED10                          10   */
(0xFF),                                         /* AR6000_XIOCTL_UNUSED11                          11   */
(0xFF),                                         /* AR6000_XIOCTL_FORCE_TARGET_RESET                12   */
(0xFF),                                         /* AR6000_XIOCTL_HTC_RAW_OPEN                      13   */
(0xFF),                                         /* AR6000_XIOCTL_HTC_RAW_CLOSE                     14   */
(0xFF),                                         /* AR6000_XIOCTL_HTC_RAW_READ                      15   */
(0xFF),                                         /* AR6000_XIOCTL_HTC_RAW_WRITE                     16   */
(0xFF),                                         /* AR6000_XIOCTL_CHECK_TARGET_READY                17   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_OUTPUT_SET                   18   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_INPUT_GET                    19   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_REGISTER_SET                 20   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_REGISTER_GET                 21   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_INTR_ACK                     22   */
(0xFF),                                         /* AR6000_XIOCTL_GPIO_INTR_WAIT                    23   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_SET_ADHOC_BSSID                   24   */
(0x00),                                                                        /*AR6000_XIOCTL_UNUSED                             25   */
(0x00),                                                                        /*AR6000_XIOCTL_UNUSED                             26   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | ADHOC_NETWORK | AP_NETWORK),                   /* AR6000_XIOCTL_SET_BEACON_INTVAL                 27   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* IEEE80211_IOCTL_SETAUTHALG                      28   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_SET_VOICE_PKT_SIZE                29   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_SET_MAX_SP                        30   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_ROAM_TBL                  31   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_ROAM_CTRL                 32   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTRL_WMI_SET_POWERSAVE_TIMERS         33   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTRL_WMI_GET_POWER_MODE               34   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTRL_WMI_SET_WLAN_STATE               35   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_ROAM_DATA                 36   */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SETRETRYLIMITS                37   */
(0xFF),          /* AR6000_XIOCTL_TCMD_CONT_TX                      38   */
(0xFF),          /* AR6000_XIOCTL_TCMD_CONT_RX                      39   */
(0xFF),                                         /* AR6000_XIOCTL_TCMD_PM                           40   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_STARTSCAN                     41   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SETFIXRATES                   42   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_GETFIXRATES                   43   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_RSSITHRESHOLD             44   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_CLR_RSSISNR                   45   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_LQTHRESHOLD               46   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_RTS                       47   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_LPREAMBLE                 48   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_AUTHMODE                  49   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_REASSOCMODE               50   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_WMM                       51   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_HB_CHALLENGE_RESP_PARAMS  52   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_HB_CHALLENGE_RESP         53   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_GET_RD                        54   */
(0xFF),                                         /* AR6000_XIOCTL_DIAG_READ                         55   */
(0xFF),                                         /* AR6000_XIOCTL_DIAG_WRITE                        56   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_TXOP                      57   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK),                                /* AR6000_XIOCTL_USER_SETKEYS                      58   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK),                                /* AR6000_XIOCTL_WMI_SET_KEEPALIVE                 59   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK),                                /* AR6000_XIOCTL_WMI_GET_KEEPALIVE                 60   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_ROMPATCH_INSTALL              61   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_ROMPATCH_UNINSTALL            62   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_ROMPATCH_ACTIVATE             63   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_ROMPATCH_DEACTIVATE           64   */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SET_APPIE                     65   */
(XIOCTL_FILTER_NONP2P_SUBMODE | 0x1F),                                         /* AR6000_XIOCTL_WMI_SET_MGMT_FRM_RX_FILTER        66   */
(0xFF),                                         /* AR6000_XIOCTL_DBGLOG_CFG_MODULE                 67   */
(0xFF),                                         /* AR6000_XIOCTL_DBGLOG_GET_DEBUG_LOGS             68   */
(0xFF),                                         /* Dummy                                           69   */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SET_WSC_STATUS                70   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BT_STATUS                 71   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_BT_PARAMS                 72   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_HOST_SLEEP_MODE           73   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_WOW_MODE                  74   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_GET_WOW_LIST                  75   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_ADD_WOW_PATTERN               76   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_DEL_WOW_PATTERN               77   */
(0xFF),                                         /* AR6000_XIOCTL_TARGET_INFO                       78   */
(0xFF),                                         /* AR6000_XIOCTL_DUMP_HTC_CREDIT_STATE             79   */
(0xFF),                                         /* AR6000_XIOCTL_TRAFFIC_ACTIVITY_CHANGE           80   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_CONNECT_CTRL_FLAGS        81   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_AKMP_PARAMS               82   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_GET_PMKID_LIST                83   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_PMKID_LIST                84   */
(0xFF),                                         /* Dummy                                           85   */
(0xFF),                                         /* Dummy                                           86   */
(0xFF),                                         /* Dummy                                           87   */
(0xFF),                                         /* Dummy                                           88   */
(0xFF),                                         /* Dummy                                           89   */
(0xFF),                                         /* AR6000_XIOCTL_UNUSED90                          90   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_LZ_STREAM_START               91   */
(0xFF),                                         /* AR6000_XIOCTL_BMI_LZ_DATA                       92   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_PROF_CFG                          93   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_PROF_ADDR_SET                     94   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_PROF_START                        95   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_PROF_STOP                         96   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_PROF_COUNT_GET                    97   */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_ABORT_SCAN                    98   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_STA_LIST                   99   */
(XIOCTL_FILTER_NONP2P_SUBMODE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_HIDDEN_SSID                    100  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_NUM_STA                    101  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_ACL_MAC                    102  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_ACL_LIST                   103  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_COMMIT_CONFIG                  104  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* IEEE80211_IOCTL_GETWPAIE                        105  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_CONN_INACT_TIME                106  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_PROT_SCAN_TIME                 107  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_WMI_SET_COUNTRY                   108  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_DTIM                       109  */
(0xFF),                                         /* AR6000_XIOCTL_WMI_TARGET_EVENT_REPORT           110  */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),    /* AR6000_XIOCTL_SET_IP                            111  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_ACL_POLICY                 112  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_CTRL_BSS_COMM                  113  */
(0xFF),                                         /* AR6000_XIOCTL_DUMP_MODULE_DEBUG_INFO            114  */
(0xFF),                                         /* AR6000_XIOCTL_MODULE_DEBUG_SET_MASK             115  */
(0xFF),                                         /* AR6000_XIOCTL_MODULE_DEBUG_GET_MASK             116  */
(0xFF),                                         /* AR6000_XIOCTL_DUMP_RCV_AGGR_STATS               117  */
(0xFF),                                         /* AR6000_XIOCTL_SET_HT_CAP                        118  */
(0xFF),                                         /* AR6000_XIOCTL_SET_HT_OP                         119  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_STAT                       120  */
(XIOCTL_FILTER_NONP2P_SUBMODE | 0x1F),                                         /* AR6000_XIOCTL_SET_TX_SELECT_RATES               121  */
(0xFF),                                         /* AR6000_XIOCTL_SETUP_AGGR                        122  */
(0xFF),                                         /* AR6000_XIOCTL_ALLOW_AGGR                        123  */
(XIOCTL_FILTER_NONP2P_SUBMODE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_HIDDEN_SSID                124  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_COUNTRY                    125  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_WMODE                      126  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_DTIM                       127  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK | ADHOC_NETWORK),                   /* AR6000_XIOCTL_AP_GET_BINTVL                     128  */
(0xFF),                                         /* AR6000_XIOCTL_AP_GET_RTS                        129  */
(0xFF),                                         /* AR6000_XIOCTL_DELE_AGGR                         130  */
(0xFF),                                         /* AR6000_XIOCTL_FETCH_TARGET_REGS                 131  */
(XIOCTL_FILTER_NONP2P_SUBMODE | 0x1F),                                         /* AR6000_XIOCTL_HCI_CMD                           132  */
(XIOCTL_FILTER_NONP2P_SUBMODE | 0x1F),                                         /* AR6000_XIOCTL_ACL_DATA                          133  */
(XIOCTL_FILTER_NONP2P_SUBMODE | 0x1F),                                         /* AR6000_XIOCTL_WLAN_CONN_PRECEDENCE              134  */
(XIOCTL_FILTER_NONP2P_SUBMODE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_11BG_RATESET               135  */
(0xFF),
(0xFF),
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_BTCOEX_FE_ANT             138  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_BTCOEX_COLOCATED_BT_DEV   139  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG  140  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_BTCOEX_SCO_CONFIG         141  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_BTCOEX_A2DP_CONFIG        142  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_BTCOEX_ACLCOEX_CONFIG     143  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_BTCOEX_DEBUG              144  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_SET_BT_OPERATING_STATUS       145  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_GET_BTCOEX_CONFIG             146  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_GET_BTCOEX_GET_STATS          147  */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SET_QOS_SUPP                  148  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_SET_DFS                        149  */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* P2P CMDS BEGIN  AR6000_XIOCTL_WMI_P2P_DISCOVER    150 */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_STOP_FIND */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_CANCEL */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_LISTEN */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_GO_NEG */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_AUTH_GO_NEG */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_REJECT */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_CONFIG */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_WPS_CONFIG */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_FINDNODE */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_GRP_INIT              160 */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_GRP_FORMATION_DONE */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_INVITE */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_PROV_DISC */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_SET */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_PEER */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_FLUSH */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_GET_GO_PARAMS */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_AUTH_INVITE */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_GET_IF_ADDR */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_GET_DEV_ADDR   170 */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOCTL_WMI_P2P_SDPD_TX_CMD */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK),   /* AR6000_XIOTCL_WMI_P2P_SD_CANCEL_REQUEST */
(0xFF),                                         /* AR6000_XIOCTL_SET_BT_HW_POWER_STATE             173  */
(0xFF),                                         /* AR6000_XIOCTL_GET_BT_HW_POWER_STATE             174  */
(0xFF),                                         /* AR6000_XIOCTL_GET_WLAN_SLEEP_STATE              175  */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SET_TX_SGI_PARAM              176  */
(0xFF),                                         /* R6000_XIOCTL_WMI_ENABLE_WAC_PARAM               177 */
(0xFF),                                         /* AR6000_XIOCTL_WAC_SCAN_REPLY                    178 */
(0xFF),                                         /* AR6000_XIOCTL_WMI_WAC_CTRL_REQ                  179 */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_WPA_OFFLOAD_STATE         180  */
(XIOCTL_FILTER_NONP2P_SUBMODE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_PASSPHRASE                181  */
(0xFF),                                         /* AR6000_XIOCTL_BMI_NVRAM_PROCESS                 182  */
(0xFF),                                         /* AR6000_XIOCTL_WMI_SET_DIVERSITY_PARAM           183  */
(0xFF),                                         /* AR6000_XIOCTL_WMI_FORCE_ASSERT                  184 */
(0xFF),                                         /* AR6000_XIOCTL_WMI_ENABLE_PKTLOG                 185 */
(0xFF),                                         /* AR6000_XIOCTL_WMI_DISABLE_PKTLOG                186 */
(0xFF),                                         /* AR6000_XIOCTL_WMI_GET_PKTLOG                    187 */
(XIOCTL_FILTER_NONP2P_SUBMODE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_ACS_DISABLE_HI_CHANNELS        188  */
(0xFF),                                         /* AR6000_XIOCTL_TCMD_CMDS                         189  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK),                /* AR6000_XIOCTL_WMI_SET_EXCESS_TX_RETRY_THRES     190  */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                   /* AR6000_XIOCTL_AP_GET_NUM_STA                    191  */
(XIOCTL_FILTER_NONP2P_SUBMODE | 0x1F),                                         /* AR6000_XIOCTL_SUSPEND_DRIVER                    192   */
(XIOCTL_FILTER_NONP2P_SUBMODE | 0x1F),                                         /* AR6000_XIOCTL_RESUME_DRIVER                     193   */
(XIOCTL_FILTER_SUBMODE_DONTCARE | INFRA_NETWORK | ADHOC_NETWORK | AP_NETWORK),  /* AR6000_XIOCTL_GET_SUBMODE                     194 */
(XIOCTL_FILTER_SUBMODE_DONTCARE | AP_NETWORK),                                  /* AR6000_XIOCTL_WMI_AP_APSD                     195 */
(0xFF),                                         /* AR6000_XIOCTL_TCMD_SETREG                       196  */
(0xFF),                                         /* AR6000_XIOCTL_GET_HT_CAP         197  */
(XIOCTL_FILTER_P2P_SUBMODE | INFRA_NETWORK | AP_NETWORK), /* AR6000_XIOCTL_WMI_GET_P2P_IE  198 */
};

#endif /*_WMI_FILTER_LINUX_H_*/
