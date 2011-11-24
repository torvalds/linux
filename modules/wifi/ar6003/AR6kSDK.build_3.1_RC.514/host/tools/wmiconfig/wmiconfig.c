/*
 * Copyright (c) 2004-2009 Atheros Communications Inc.
 * All rights reserved.
 * 
 * 
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
 *
 *
 */
#define SUPPORT_11N
#include <sys/types.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/if.h>
#include <linux/wireless.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <a_config.h>
#include <a_osapi.h>
#include <athdefs.h>
#include <a_types.h>
#include <wmi.h>
#include <ar6kap_common.h>
#include "athdrv_linux.h"
#include "ieee80211.h"
#include "ieee80211_ioctl.h"
#include "wmiconfig.h"
#include "targaddrs.h"
#include "pkt_log.h"

#if WIRELESS_EXT >= 18
/* only allow WPA config commands if this is compiled against distribution
 * with a newer version of the wireless extensions */
#define WPA_SUPPORT
#endif

#define TO_LOWER(c) ((((c) >= 'A') && ((c) <= 'Z')) ? ((c)+ 0x20) : (c))

#define ISDIGIT(c)  ( (((c) >= '0') && ((c) <= '9')) ? (1) : (0) )

static int send_connect_cmd();

static int send_scan_probedssid_cmd();

static int _from_hex(char c);
static A_INT8 getPhyMode(char *pArg);

static A_UINT16 wmic_ieee2freq(int chan);
static A_STATUS wmic_ether_aton(const char *orig, A_UINT8 *eth);
void            printTargetStats(TARGET_STATS *pStats);
A_STATUS
wmic_validate_roam_ctrl(WMI_SET_ROAM_CTRL_CMD *pRoamCtrl, A_UINT8 numArgs,
                        char **argv);
A_STATUS
wmic_validate_appie(struct ieee80211req_getset_appiebuf *appIEInfo, char **argv);

A_STATUS
wmic_validate_mgmtfilter(A_UINT32 *pMgmtFilter, char **argv);
void convert_hexstring_bytearray(char *hexStr, A_UINT8 *byteArray,
                                    A_UINT8 numBytes);
static int is_mac_null(A_UINT8 *mac);
void print_wild_mac(unsigned char *mac, char wildcard);
A_STATUS wmic_ether_aton_wild(const char *orig, A_UINT8 *eth, A_UINT8 *wild);
void printBtcoexConfig(WMI_BTCOEX_CONFIG_EVENT * pConfig);
void printBtcoexStats(WMI_BTCOEX_STATS_EVENT * pStats);
#ifdef ATH_INCLUDE_PAL
void *phy_attach(char *if_name);
int pal_send_hci_cmd(void *dev, char *buf, short sz);
int pal_send_acl_data_pkt(void *dev, char *buf, short sz);
void cmdParser(A_UINT8 *cmd,A_UINT16 len);
void palData(A_UINT8 *data,A_UINT32 len);
extern int eventLogLevel; //To enable/disable Debug messages. defined in pal parser library
const char palcommands[]=
"--sendpalcmd <filename> <on/off> ; on - enable debug; off - disable debug \n\
--sendpaldata <filename> <on/off>; on - enable debug; off - disable debug \n";
#endif
const char *progname;
const char commands[] =
"commands:\n\
--version\n\
--power <mode> where <mode> is rec or maxperf\n\
--getpower is used to get the power mode(rec or maxperf)\n\
--pmparams --it=<msec> --np=<number of PS POLL> --dp=<DTIM policy: ignore/normal/stick> --twp=<Tx wakeup policy: wakeup/sleep> --nt=<number of tx to wakeup>\n\
--psparams --psPollTimer=<psPollTimeout> --triggerTimer=<triggerTimeout> --apsdTimPolicy=<ignore/adhere> --simulatedAPSDTimPolicy=<ignore/adhere>\n\
--forceAssert \n\
--ibsspmcaps --ps=<disable/atheros/ieee> --aw=<ATIM Windows in millisecond> --to=<TIMEOUT in millisecond> --ttl=<Time to live in number of beacon periods>\n\
--appsparams --pstype=<disable/atheros> --psit=<millisecond> --psperiod=<in microsecond> --sleepperiod=<in psperiods>\n\
--scan --fgstart=<sec> --fgend=<sec> --bg=<sec> --minact=<msec> maxact=<msec> --pas=<msec> --sr=<short scan ratio> --maxact2pas=<msec> --scanctrlflags <connScan> <scanConnected> <activeScan> <roamScan> <reportBSSINFO> <EnableAutoScan> --maxactscan_ssid=<Max no of active scan per probed ssid>\n\
  where: \n\\n\
  <connScan>           is 0 to not scan when Connect and Reconnect command, \n\
                          1 to scan when Connect and Reconnect command, \n\
  <scanConnected>      is 0 to skip the ssid it is already connected to, \n\
                          1 to scan the ssid it is already connected to, \n\
  <activeScan>         is 0 to disable active scan, \n\
                          1 to enable active scan, \n\
  <roamScan>           is 0 to disable roam scan when beacom miss and low rssi.(It's only valible when connScan is 0.\n\
                          1 to enable roam scan.\n\
  <reportBSSINFO>      is 0 to disable specified BSSINFO reporting rule.\n\
                          1 to enable specified BSSINFO reporting rule.\n\
 <EnableAutoScan>      is 0 to disable autonomous scan. No scan after a disconnect event\n\
                          1 Enable autonomous scan.\n\
--listen=<#of TUs, can  range from 15 to 5000>\n\
--listenbeacons=<#of beacons, can  range from 1 to 50>\n\
--setbmisstime <#of TUs, can range from 1000 to 5000>\n\
--setbmissbeacons <#of beacons, can range from 5 to 50>\n\
--filter=<filter> --ieMask 0x<mask> where <filter> is none, all, profile, not_profile, bss, not_bss, or ssid and <mask> is a combination of the following\n\
{\n\
    BSS_ELEMID_CHANSWITCH = 0x01 \n\
    BSS_ELEMID_ATHEROS = 0x02\n\
}\n\
--wmode <mode> <list> sc <scan> where \n\
        <mode> is a, g, b,ag, gonly (use mode alone in AP mode) \n\
        <list> is a list of channels (frequencies in mhz or ieee channel numbers)\n\
        <scan> is 0 to disable scan after setting channel list.\n\
                  1 to enable scan after setting channel list.\n\
--getwmode \n\
--ssid=<ssid> [--num=<index>] where <ssid> is the wireless network string and <index> is 0 or 1 (set to 0 if not specified). Set ssid to 'off' to clear the entry\n\
--badAP=<macaddr> [--num=<index>] where macaddr is macaddr of AP to be avoided in xx:xx:xx:xx:xx:xx format, and num is index from 0-1.\n\
--clrAP [--num=<index>] is used to clear a badAP entry.  num is index from 0-1\n\
--createqos <user priority> <direction> <traffic class> <trafficType> <voice PS capability> \n\
    <min service interval> <max service interval> <inactivity interval> <suspension interval> \n\
    <service start time> <tsid> <nominal MSDU> <max MSDU> <min data rate> <mean data rate> \n\
    <peak data rate> <max burst size> <delay bound> <min phy rate> <sba> <medium time>where:\n\
        <user priority>         802.1D user priority range : 0-7        \n\
        <direction>             is 0 for Tx(uplink) traffic,            \n\
                                   1 for Rx(downlink) traffic,          \n\
                                   2 for bi-directional traffic;        \n\
        <traffic class>         is 0 for BE,                            \n\
                                   1 for BK,                            \n\
                                   2 for VI,                            \n\
                                   3 for VO;                            \n\
        <trafficType>           1-periodic, 0-aperiodic                 \n\
        <voice PS capability>   specifies whether the voice power save mechanism \n\
                                (APSD if AP supports it or legacy/simulated APSD \n\
                                    [using PS-Poll] ) should be used             \n\
                                = 0 to disable voice power save for this traffic class,\n\
                                = 1 to enable APSD voice power save for this traffic class,\n\
                                = 2 to enable voice power save for ALL traffic classes,\n\
        <min service interval>  in milliseconds                     \n\
        <max service interval>  in milliseconds                     \n\
        <inactivity interval>   in milliseconds;=0 means infinite inactivity interval\n\
        <suspension interval>   in milliseconds \n\
        <service start time>    service start time \n\
        <tsid>                  TSID range: 0-15                    \n\
        <nominal MSDU>          nominal MAC SDU size                \n\
        <max MSDU>              maximum MAC SDU size                \n\
        <min data rate>         min data rate in bps                \n\
        <mean data rate>        mean data rate in bps               \n\
        <peak data rate>        peak data rate in bps               \n\
        <max burst size>        max burst size in bps               \n\
        <delay bound>           delay bound                         \n\
        <min phy rate>          min phy rate in bps                 \n\
        <sba>                   surplus bandwidth allowance         \n\
        <medium time>           medium time in TU of 32-us periods per sec    \n\
--deleteqos <trafficClass> <tsid> where:\n\
  <traffic class>         is 0 for BE,                            \n\
                             1 for BK,                            \n\
                             2 for VI,                            \n\
                             3 for VO;                            \n\
  <tsid> is the TspecID, use --qosqueue option to get the active tsids\n\
--qosqueue <traffic class>, where:\n\
  <traffic class>         is 0 for BE,                            \n\
                             1 for BK,                            \n\
                             2 for VI,                            \n\
                             3 for VO;                            \n\
--getTargetStats --clearStats\n\
   tx_unicast_rate, rx_unicast_rate values will be 0Kbps when no tx/rx \n\
   unicast data frame is received.\n\
--setErrorReportingBitmask\n\
--acparams --acval <0-3> --txop <limit> --cwmin <0-15> --cwmax <0-15> --aifsn<0-15>\n\
--disc=<timeout> to set the disconnect timeout in seconds\n\
--mode <mode> set the optional mode, where mode is special or off \n\
--sendframe <frmType> <dstaddr> <bssid> <optIEDatalen> <optIEData> where:\n\
  <frmType>   is 1 for probe request frame,\n\
                 2 for probe response frame,\n\
                 3 for CPPP start,\n\
                 4 for CPPP stop, \n\
  <dstaddr>   is the destination mac address, in xx:xx:xx:xx:xx:xx format, \n\
  <bssid>     is the bssid, in xx:xx:xx:xx:xx:xx format, \n\
  <optIEDatalen> optional IE data length,\n\
  <optIEData> is the pointer to optional IE data arrary \n\
--adhocbssid <macaddr> where macaddr is the BSSID for IBSS to be created in xx:xx:xx:xx:xx:xx format\n\
--beaconintvl   <beacon_interval in milliseonds> \n\
--getbeaconintvl \n\
--setretrylimits  <frameType> <trafficClass> <maxRetries> <enableNotify>\n\
  <frameType>      is 0 for management frame, \n\
                      1 for control frame,\n\
                      2,for data frame;\n\
  <trafficClass>   is 0 for BE, 1 for BK, 2 for VI, 3 for VO, only applies to data frame type\n\
  <maxRetries>     is # in [2 - 13];      \n\
  <enableNotify>   is \"on\" to enable the notification of max retries exceed \n\
                      \"off\" to disable the notification of max retries excedd \n\
--rssiThreshold <weight> <pollTimer> <above_threshold_tag_1> <above_threshold_val_1> ... \n\
                <above_threshold_tag_6> <above_threshold_val_6> \n\
                <below_threshold_tag_1> <below_threshold_val_1> ... \n\
                <below_threshold_tag_6> <below_threshold_val_6> \n\
  <weight>        share with snrThreshold\n\
  <threshold_x>   will be converted to negatvie value automatically, \n\
                   i.e. input 90, actually -90 will be set into HW\n\
  <pollTimer>     is timer to poll rssi value(factor of LI), set to 0 will disable all thresholds\n\
                 \n\
--snrThreshold <weight> <upper_threshold_1> ... <upper_threshold_4> \n\
               <lower_threshold_1> ... <lower_threshold_4> <pollTimer>\n\
  <weight>        share with rssiThreshold\n\
  <threshold_x>  is positive value, in ascending order\n\
  <pollTimer>     is timer to poll snr value(factor of LI), set to 0 will disable all thresholds\n\
                 \n\
--cleanRssiSnr \n\
--lqThreshold <enable> <upper_threshold_1>  ... <upper_threshold_4>\n\
              <lower_threshold_1> ... <lower_threshold_4>\n\
   <enable>       is 0 for disable,\n\
                     1 for enable lqThreshold\n\
   <threshold_x>  is in ascending order            \n\
--setlongpreamble <enable>\n\
    <enable>      is 0 for diable,\n\
                     1 for enable.\n\
--setRTS  <pkt length threshold>\n\
--getRTS \n\
--startscan   --homeDwellTime=<msec> --forceScanInt<ms> --forceScanFlags <scan type> <forcefgscan> <isLegacyCisco> --scanlist <list> where:\n\
  <homeDwellTime>     Maximum duration in the home channel(milliseconds),\n\
  <forceScanInt>      Time interval between scans (milliseconds),\n\
    <scan type>     is 0 for long scan,\n\
                     1 for short scan,\n\
  <forcefgscan>   is 0 for disable force fgscan,\n\
                     1 for enable force fgscan,\n\
  <isLegacyCisco> is 0 for disable legacy Cisco AP compatible,\n\
                     1 for enable legacy Cisco AP compatible,\n\
  <list> is a list of channels (frequencies in mhz or ieee channel numbers)\n\
--setfixrates <rate index> where: \n\
  <rate index> is {0 1M},{1 2M},{2 5.5M},{3 11M},{4 6M},{5 9M},{6 12M},{7 18M},{8 24M},{9 36M},{10 48M},{11 54M},\n\
  if want to config more rare index, can use blank to space out, such as: --setfixrates 0 1 2 \n\
--getfixrates : Get the fix rate index from target\n\
--setauthmode <mode> where:\n\
  <mode>        is 0 to do authentication when reconnect, \n\
                   1 to not do authentication when reconnect.(not clean key). \n\
--setreassocmode <mode> where:\n\
  <mode>        is 0 do send disassoc when reassociation, \n\
                   1 do not send disassoc when reassociation. \n\
--setVoicePktSize  is maximum size of voice packet \n\
--setMaxSPLength   is the maximum service period in packets, as applicable in APSD \n\
                   0 - deliver all packets \n\
                   1 - deliver up to 2 packets \n\
                   2 - deliver up to 4 packets \n\
                   3 - deliver up to 6 packets \n\
--setAssocIe <IE String>\n\
--roam <roamctrl> <info>\n\
       where <roamctrl> is   1  force a roam to specified bssid\n\
                             2  set the roam mode \n\
                             3  set the host bias of the specified BSSID\n\
                             4 set the lowrssi scan parameters \n\
      where <info> is BSSID<aa:bb:cc:dd:ee:ff> for roamctrl of 1\n\
                      DEFAULT ,BSSBIAS or LOCK for roamctrl of 2\n\
                      BSSID<aa:bb:cc:dd:ee:ff> <bias> for  roamctrl of 3\n\
                             where <bias> is  a value between -256 and 255\n\
                      <scan period> <scan threshold> <roam threshold> \n\
                      <roam rssi floor> for roamctrl of 4\n\
--getroamtable\n\
--getroamdata\n\
--wlan <enable/disable/query>\n\
--bt <on/off/query>\n\
--setBTstatus <streamType> <status>\n\
      where <streamType> is    1 - Bluetooth SCO stream\n\
                               2 - Bluetooth A2DP stream\n\
                               3 - Bluetooth Inquiry/low priority stream\n\
                               4 - Bluetooth E-SCO stream\n\
      \n\
      where <status> is        1 - stream started\n\
                               2 - stream stopped\n\
                               3 - stream resumed\n\
                               4 - stream suspended\n\
--setBTparams <paramType> <params>\n\
      where <paramType> is     1 - Bluetooth SCO stream parameters\n\
                               2 - Bluetooth A2DP stream parameters \n\
                               3 - Front end antenna configuration \n\
                               4 - Co-located Bluetooth configuration\n\
                               5 - Bluetooth ACL coex (non-a2dp) parameters\n\
                               6 - 11a is using a separate antenna\n\
      \n\
      where <params> for Bluetooth SCO are:\n\
              <numScoCyclesForceTrigger> - number of Sco cyles, to force a trigger\n\
               <dataResponseTimeout> - timeout for receiving downlink packet per PS-poll\n\
               <stompScoRules> - Applicable for dual/splitter front end\n\
                           1, Never stomp BT to receive downlink pkt\n\
                           2, Always stomp BT to receive downlink pkt\n\
                           3, Stomp BT only during low rssi conditions\n\
               <stompDutyCyleVal> If Sco is stomped while waiting for downlink pkt, number sco cyles to not queue ps-poll-(Applicable only for switch FE)\n\
              <psPollLatencyFraction> Fraction of idle SCO idle time.\n\
                           1, if more than 3/4 idle duration is left, retrieve downlink pkt\n\
                           2, if more than 1/2 idle duration is left, retrieve downlink pkt\n\
                           3, if more 1/4 idle duration is left, retrieve dwnlink pkt\n\
               <SCO slots> - number of Tx+Rx SCO slots : 2 for single-slot SCO, 6 for 3-slot SCO\n\
          <Idle SCO slots> - number of idle slots between two SCO Tx+Rx instances\n\
      \n\
      where <params> for A2DP configuration are\n\
      <a2dpWlanUsageLimit> Max duration wlan can use the medium ,whenever firmware detects medium for wlan (in msecs) \n\
     <a2dpBurstCntMin> Mininum number of bluetooth data frames to replenish wlan usage time\n\
     <a2dpDataRespTimeout> Time to wait for downlink data, after queuing pspoll\n\
      where <params> for front end antenna configuration are\n\
      1 - Dual antenna configuration (BT and wlan have seperate antenna) \n\
      2 - Single antenna splitter configuration \n\
      3 - Single antenna switch  configuration \n\
      \n\
      where <params> for co-located Bluetooth configuration are\n\
      0 - Qualcomm BTS402x (default)\n\
      1 - CSR Bluetooth\n\
      2 - Atheros Bluetooth\n\
      \n\
      where <params> for Bluetooth ACL coex(bt ftp or bt OPP or other data based ACL profile (non a2dp)  parameter are \n\
      <aclWlanMediumUsageTime> Usage time for Wlan.(default 30 msecs)\n\
      <aclBtMediumUsageTime> Usage time for bluetooth (default 30 msecs)\n\
      <aclDataRespTimeout> - timeout for receiving downlink packet per PS-poll\n\
--setbtcoexfeant <antType> \n\
       <antType> - Front end antenna type\n\
       1 - Single antenna\n\
       2 - Dual antenna\n\
       3 - Dual antenna high isolation\n\
       4 - bypass mode\n\
       5 - combine mode\n\
--setbtcoexcolocatedbt <btdevType >\n\
        <btdevType> Co-located bluetooth device\n\
        1 - Qualcomm BTS402X \n\
        2 - CSR BC06 bluetooth \n\
        3 - Atheros 3001 bluetooth\n\
        4 - ST-ericssion CG2900 \n\
        5 - Atheros 3002/MCI \n\
--setbtcoexscoconfig <noscoSlots> <noidleslots> <scoflags> <linkid> <scoCyclesForceTrigger> <scoDataResponseTimeout> <scoStompDutyCyleVal> <scoStompDutyCyleMaxVal> <scoPsPollLatencyFraction> <scoStompCntIn100ms> <scoContStompMax> <scoMinlowRateMbps> <scoLowRateCnt> <scoHighPktRatio> <scoMaxAggrSize>\n\
--setbtcoexa2dpconfig <a2dpFlags> <linkid> <a2dpWlanMaxDur> <a2dpMinBurstCnt> <a2dpDataRespTimeout> <a2dpMinlowRateMbps> <a2dpLowRateCnt> <a2dpHighPktRatio> <a2dpMaxAggrSize> <a2dpPktStompCnt>\n\
--setbtcoexaclcoexconfig <aclWlanMediumDur> <aclBtMediumDur> <aclDetectTimeout> <aclPktCntLowerLimit> <aclIterForEnDis> <aclPktCntUpperLimit> <aclCoexFlags> <linkId> <aclDataRespTimeout> <aclCoexMinlowRateMbps> <aclCoexLowRateCnt> <aclCoexHighPktRatio> <aclCoexMaxAggrSize> <aclPktStompCnt>  \n\
--setbtcoexbtinquirypageconfig <btInquiryDataFetchFrequency> <protectBmissDurPostBtInquiry> <btInquiryPageFlag>\n\
--setbtcoexbtoperatingstatus <btprofiletype> <btoperatingstatus> <btlinkid>\n\
        <btprofiletype> - Bluetooth profile\n\
        1 - Bluetooth SCO profile \n\
        2 - Bluetooth A2DP profile \n\
        3 - Bluetooth Inquiry Page profile \n\
        4 - Bluetooth ACL (non-a2dp) profile \n\
        \n\
        <btoperatingstatus>  profile operating status \n\
        1 - start \n\
        2 - stop \n\
        \n\
        <btlinkid> bluetooth link id -Applicable only for STE Bluetooth\n\
        \n\
--setbtcoexdebug <params1> <params2> <params3> <params4> <params5> \n\
--getbtcoexconfig <btprofile> <linkid>\n\
        <btprofile> - bluetooth profile \n\
        1 - Bluetooth SCO profile \n\
        2 - Bluetooth A2DP profile \n\
        3 - Bluetooth Inquiry Page profile \n\
        4 - Bluetooth ACL (non-a2dp) profile \n\
        \n\
    <btlinkid> bluetooth link id -Applicable only for STE Bluetooth\n\
    \n\
\n\
--getbtcoexstats\n\
--detecterror --frequency=<sec> --threshold=<count> where:\n\
  <frequency>   is the periodicity of the challenge messages in seconds, \n\
  <threshold>   is the number of challenge misses after which the error detection module in the driver will report an error, \n\
--getheartbeat --cookie=<cookie>\n\
  <cookie>  is used to identify the response corresponding to a challenge sent\n\
--usersetkeys --initrsc=<on/off>\n\
  initrsc=on(off> initialises(doesnot initialise) the RSC in the firmware\n\
--getRD\n\
--setcountry <countryCode> (Use --countrycodes for list of codes)\n\
--countrycodes (Lists all the valid country codes)\n\
--getcountry \n\
--disableregulatory\n\
--txopbursting <burstEnable>\n\
        where <burstEnable> is  0 disallow TxOp bursting\n\
                                1 allow TxOp bursting\n\
--diagread\n\
--diagwrite\n\
--setkeepalive <keepalive interval>\n\
  <keepalive interval> is the time within which if there is no transmission/reception activity, the station sends a null packet to AP.\n\
--getkeepalive\n\
--setappie <frame> <IE>\n\
         where frame is one of beacon, probe, respon, assoc\n\
               IE is a hex string starting with dd\n\
               if IE is 0 then no IE is sent in the management frame\n\
--setmgmtfilter <op> <frametype>\n\
                op is one of set, clear\n\
                frametype is one of beacon proberesp\n\
--setdbglogconfig --mmask=<mask> --rep=<0/1> --tsr=<tsr codes> --size=<num>\n\
         where <mask> is a 16 bit wide mask to selectively enable logging for different modules. Example: 0xFFFD enables logging for all modules except WMI. The mask is derived from the module ids defined in etna/include/dbglog.h header file.\n\
               <rep> is whether the target should generate log events to the host whenever the log buffer is full.\n\
               <tsr> resolution of the debug timestamp (less than 16)\n\
                     0: 31.25 us\n\
                     1: 62.50 us\n\
                     2: 125.0 us\n\
                     3: 250.0 us\n\
                     4: 500.0 us\n\
                     5: 1.0 ms and so on.\n\
               <size> size of the report in number of debug logs.\n\
--getdbglogs\n\
--sethostmode <mode>\n\
  where <mode> is awake\n\
                  asleep\n\
--setwowmode <mode> --wowfilter <filter> --hostreqdelay <hostreqdelay> \n\
  where <mode> is enable \n\
                  disable\n\
  where <filter> is ssid -to enable ssid filtering when asleep\n\
                    none \n\
--getwowlist <listid> \n\
--addwowpattern <list-id> <pattern-size> <pattern-offset> <pattern> <pattern-mask \n\
--delwowpattern <list-id> <pattern-id>\n\
--dumpchipmem \n\
--dumpchipmem_venus \n\
--setconnectctrl <ctrl flags bitmask> \n\
  where <flags> could take the following values:\n\
      0x0001(CONNECT_ASSOC_POLICY_USER): Assoc frames are sent using the policy specified by the flag below.\n\
      0x0002(CONNECT_SEND_REASSOC): Send Reassoc frame while connecting otherwise send assoc frames.\n\
      0x0004(CONNECT_IGNORE_WPAx_GROUP_CIPHER): Ignore WPAx group cipher for WPA/WPA2.\n\
      0x0008(CONNECT_PROFILE_MATCH_DONE): Ignore any profile check.\n\
      0x0010(CONNECT_IGNORE_AAC_BEACON): Ignore the admission control beacon.\n\
      0x0020(CONNECT_CSA_FOLLOW_BSS):Set to Follow BSS to the new channel and connect \n\
                                 and reset to disconnect from BSS and change channel \n\
--dumpcreditstates \n\
      Triggers the HTC layer to dump credit state information to the debugger \n\
--setakmp --multipmkid=<on/off>\n\
  multipmkid=on(off> enables(doesnot enable) Multi PMKID Caching in firmware\n\
--setpmkidlist --numpmkid=<n> --pmkid=<pmkid_1> ... --pmkid=<pmkid_n>\n\
   where n is the number of pmkids (max 8)\n\
   and pmkid_i is the ith pmkid (16 bytes in hex format)\n\
--setbsspmkid --bssid=<aabbccddeeff> --bsspmkid=<pmkid>\n\
   bssid is 6 bytes in hex format\n\
   bsspmkid is 16 bytes in hex format\n\
--getpmkidlist \n\
--abortscan \n\
--settgtevt <event value>\n\
      where <event value> is  0 send WMI_DISCONNECT_EVENT with disconnectReason = BSS_DISCONNECTED\n\
                                after re-connection with AP\n\
                              1 NOT send WMI_DISCONNECT_EVENT with disconnectReason = BSS_DISCONNECTED\n\
                                after re-connection with AP\n\
--getsta \n\
--hiddenssid <value> \n\
    where value 1-Enable, 0-Disable. \n\
--gethiddenssid \n\
--numsta <num> \n\
--gnumsta <num> \n\
--getnumsta \n\
--getgnumsta \n\
--conninact <period> \n\
    where period is time in min (default 5min). \n\
    0 will disable STA inactivity check. \n\
--protectionscan <period> <dwell> \n\
    where period is in min (default 5min). 0 will disable. \n\
    dwell is in ms. (default 200ms). \n\
--addacl <mac> \n\
    where mac is of the format xx:xx:xx:xx:xx:xx \n\
--delacl <index> \n\
    use --getacl to get index \n\
--getacl \n\
--aclpolicy <policy> <retain list> \n\
    where <policy> \n\
        0 - Disable ACL \n\
        1 - Allow MAC \n\
        2 - Deny MAC \n\
          <retain list> \n\
        0 - Clear the current ACL list \n\
        1 - Retain the current ACL list \n\
--removesta <action> <reason> <mac> \n\
    where <action>  \n\
            2 - Disassoc    \n\
            3 - Deauth      \n\
          <reason> protocol reason code, use 1 when in doubt \n\
          <mac> mac addr of a connected STA            \n\
--dtim <period> \n\
--getdtim \n\
--intrabss <ctrl> \n\
    where <ctrl> 0 - Disable, 1 - Enable (default) \n\
--interbss <ctrl> \n\
    where <ctrl> 0 - Disable, 1 - Enable (default) \n\
--apgetstats \n\
--apclearstats \n\
--acsdisablehichannels <0/1> \n\
    where 0 - ACS will choose 1, 6 or 11 \n\
          1 - ACS will choose 1, 6 or 11 \n\
--commit \n\
--ip arg, arg may be \n\
   none  - resets ip \n\
   x.x.x.x - ip addr is dotted form\n\
--set_ht_cap <band> <enable> <supported channel width set> <short GI 20MHz> <short GI 40MHz> <40MHz intolerant> <max AMPDU len exponent> \n\
    where <band> : 'g' for 2.4 GHZ or 'a' for 5GHZ \n\
    <enable> : 0 to disable 11n in band, 1 to enable 11n in band \n\
    <supported channel width set> : 0 if only 20MHz operation supported \n\
                                          1 if 20MHz and 40MHz operation supported \n\
    <short GI 20MHz> : 0 if not supported, 1 if supported \n\
    <short GI 40MHz> : 0 if not supported, 1 if supported \n\
    <40MHz intolerant> : 1 if prohibit a receing AP from operating as a 20/40 MHz BSS \n\
                         0 otherwise \n\
    <max AMPDU len exponent> : valid values from 0 to 3 \n\
--dump_recv_aggr_stats \n\
--setup_aggr <tid> <aid> \n\
        where <aid> = aid of a connected STA. Ignored in STA mode \n\
--allow_aggr <tx_tid_mask>  <rx_tid_mask> \n\
--dele_aggr <tid>  <direction> <aid> \n\
        where <direction> =1 uplink; \n\
                          =0 dnlink  \n\
              <aid> = aid of a connected STA. Ignored in STA mode \n\
--set_ht_op <STA channel width> : 0 if only allow 20MHz channel width\n\
                                  1 if allow any channel width in the supported channel width set \n\
--wlan_conn_prec <prec_val> \n\
        where 0: WLAN connection will have precedence;\n\
              1: PAL connection will have precedence;\n\
--settxselrates <11A ratemask> <11G ratemask> <11B ratemask> <11GOnly ratemask> <11A_HT20 ratemask> <11G_HT20 ratemask> \
<11A_HT40 ratemask> <11G_HT40 ratemask> where all rate masks are hex integers. \n\
--aprateset <val> \n\
        where 1: RateSet#1 - 1,2,5.5,11 basic rates (default)\n\
              2: RateSet#2 - 1,2,5.5,11,6,12,24 basic rates\n\
--connect <ssid> \n\
--connect <ssid> --wpa <ver> <ucipher> <mcipher> <psk> \n\
--connect <ssid> --wep <mode> <def_keyix> <key1> <key2*> <key3*> <key4*> \n\
        where   <ssid>     : SSID of network \n\
                <ver>      : 1 - WPA, 2 - RSN   \n\
                <ucipher>  : TKIP or CCMP for unicast \n\
                <mcipher>  : TKIP or CCMP for multicast \n\
                <psk>      : passphrase for WPA    \n\
                <mode>     : open or shared    \n\
                <def_keyix>: Default TX wep key index  [1-4] \n\
                <key>      : wep key   \n\
                *          : optional parameters  \n\
--set_tx_sgi --masksgi <mask> --persgi <value> \n\
         where <mask> is a 32 bit hexadecimal value (eg: 0x0808000 or 08080000) to select desired MCS and ht20/ht40 SGI implementation (refer spec for bit location of each MCS)\n\
             For disabling SGI enter 0x00000000 or 0.\n\
             If mask is not entered, the default mask is 0x08080000 which enables MCS 7 to use SGI for both ht20 and ht40, when set_tx_sgi command is issued \n\
         where  <value> is the acceptable loss percentage for Packet Error Rate (PER) for SGI (default : 10)\n\
--set_dfs <enable> \n\
        where  <enable> : 1 to allow DFS \n\
                        : 0 to disable DFS \n\
--setdivparam <idleTime> <RSSIThresh> <Enable> <Threshold Rate> \n\
        where <idleTime> : time in ms where the idle timer would start checking the other antenna (default: 10000) \n\
                  <RSSIThresh> : in db, where the firmware will check the other antenna if the RSSI drops below this delta (default: 10) \n\
                  <Enable> : 1 to enable diversity \n\
                  <Threshold Rate> : in Mbps, where the firmware will block the idleTimer if the throughput is above this rate (default 48) \n\
--scanprobedssid <ssid> where <ssid> is the wireless network string to scan. Broadcast probe will not be sent .  Set ssid to 'off' or 'any' to send broadcast probe requests \n\
--ap_apsd <value> \n\
    where value 1-Enable, 0-Disable \n\
--get_ht_cap <band> \n\
    where <band> : 'g' for 2.4 GHZ or 'a' for 5GHZ \n\
";

A_UINT32 mercuryAdd[5][2] = {
            {0x20000, 0x200fc},
            {0x28000, 0x28800},
            {0x20800, 0x20a40},
            {0x21000, 0x212f0},
            {0x500000, 0x500000+184*1024},
        };

typedef struct chip_internal_t{
    A_UINT32 addr_st;
    A_UINT32 addr_end;
    A_UINT8 *info;
}CHIP_INTERNAL;

CHIP_INTERNAL venus_internal[8] = {
            {0x20000, 0x200fc, (A_UINT8 *)"General DMA and recv related registers"},
            {0x28000, 0x28900, (A_UINT8 *)"MAC PCU register & keycache"},
            {0x20800, 0x20a40, (A_UINT8 *)"QCU"},
            {0x21000, 0x212f0, (A_UINT8 *)"DCU"},
            {0x4000,  0x42e4,  (A_UINT8 *)"RTC"},
            {0x540000, 0x540000+256*1024, (A_UINT8 *)"RAM"},
            {0x29800, 0x2B210, (A_UINT8 *)"BB"},
            {0x1C000, 0x1C748, (A_UINT8 *)"Analog"},
        };



static void
usage(void)
{
    fprintf(stderr, "usage:\n%s [-i device] commands\n", progname);
    fprintf(stderr, "%s\n", commands);
#ifdef ATH_INCLUDE_PAL
    fprintf(stderr, "%s\n", palcommands);
#endif
    fprintf(stderr, "The options can be given in any order\n");
    exit(-1);
}

/* List of Country codes */
char    *my_ctr[] = {
    "DB", "NA", "AL", "DZ", "AR", "AM", "AU", "AT", "AZ", "BH", "BY", "BE", "BZ", "BO", "BR", "BN",
    "BG", "CA", "CL", "CN", "CO", "CR", "HR", "CY", "CZ", "DK", "DO", "EC", "EG", "SV", "EE", "FI",
    "FR", "GE", "DE", "GR", "GT", "HN", "HK", "HU", "IS", "IN", "ID", "IR", "IE", "IL", "IT", "JP",
    "JO", "KZ", "KP", "KR", "K2", "KW", "LV", "LB", "LI", "LT", "LU",
    "MO", "MK", "MY", "MX", "MC", "MA", "NL", "NZ", "NO", "OM", "PK", "PA", "PE", "PH", "PL", "PT",
    "PR", "QA", "RO", "RU", "SA", "SG", "SK", "SI", "ZA", "ES", "SE", "CH", "SY", "TW", "TH", "TT",
    "TN", "TR", "UA", "AE", "GB", "US", "UY", "UZ", "VE", "VN", "YE", "ZW"
    };

const char *targ_reg_name[] = {
"zero", "pc", "lbeg", "lend", "lcount", "sar", "litbaddr", "ps",
"brtarg", "epc1", "epc2", "epc3", "epc4", "windowbase", "windowstart", "zero",
"zero", "zero", "zero", "zero", "zero", "zero", "zero", "zero",
"zero", "zero", "zero", "zero", "zero", "zero", "zero", "zero",
"ar0", "ar1", "ar2", "ar3", "ar4", "ar5", "ar6", "ar7",
"ar8", "ar9", "ar10", "ar11", "ar12", "ar13", "ar14", "ar15",
"ar16", "ar17", "ar18", "ar19", "ar20", "ar21", "ar22", "ar23",
"ar24", "ar25", "ar26", "ar27", "ar28", "ar29", "ar30", "ar31",
};

#ifdef ATH_INCLUDE_PAL

A_INT32
get_input_choice(char *fname,A_UINT8 *pdu, A_UINT16 *sz)
{
    int fhdl;
    A_INT32 ret = -1;
    fhdl = open(fname, O_RDONLY);
    if(fhdl != -1)
    {
        *sz  = read(fhdl, pdu,MAX_BUFFER_SIZE);
        close(fhdl);
        ret = 0;
    }
    return ret;
}

#endif
int
main (int argc, char **argv)
{
    int c, s;
    char ifname[IFNAMSIZ];
    unsigned int cmd = 0;
    progname = argv[0];
    struct ifreq ifr;
    struct iwreq iwr;
    char *buf = malloc(sizeof(PACKET_LOG));
    int clearstat = 0;

    WMI_LISTEN_INT_CMD *listenCmd         = (WMI_LISTEN_INT_CMD*)buf;
    WMI_BMISS_TIME_CMD *bmissCmd         = (WMI_BMISS_TIME_CMD*)buf;

    WMI_POWER_MODE_CMD *pwrCmd         = (WMI_POWER_MODE_CMD *)buf;
    WMI_SET_MCAST_FILTER_CMD *sMcastFilterCmd       = (WMI_SET_MCAST_FILTER_CMD *)(buf + 4);
    WMI_MCAST_FILTER_CMD *mcastFilterCmd       = (WMI_MCAST_FILTER_CMD *)(buf + 4);
    WMI_IBSS_PM_CAPS_CMD *adhocPmCmd   = (WMI_IBSS_PM_CAPS_CMD *)buf;
    WMI_AP_PS_CMD *apPsCmd             = (WMI_AP_PS_CMD *)(buf + 4);
    WMI_SCAN_PARAMS_CMD *sParamCmd     = (WMI_SCAN_PARAMS_CMD *)buf;
    WMI_BSS_FILTER_CMD *filterCmd      = (WMI_BSS_FILTER_CMD *)buf;
    WMI_CHANNEL_PARAMS_CMD *chParamCmd = (WMI_CHANNEL_PARAMS_CMD *)buf;
    WMI_PROBED_SSID_CMD *ssidCmd       = (WMI_PROBED_SSID_CMD *)buf;
    WMI_POWER_PARAMS_CMD *pmParamCmd   = (WMI_POWER_PARAMS_CMD *)buf;
    WMI_ADD_BAD_AP_CMD *badApCmd       = (WMI_ADD_BAD_AP_CMD *)buf;
    WMI_CREATE_PSTREAM_CMD *crePStreamCmd = (WMI_CREATE_PSTREAM_CMD *)buf;
    WMI_DELETE_PSTREAM_CMD *delPStreamCmd = (WMI_DELETE_PSTREAM_CMD *)buf;
    USER_RSSI_PARAMS *rssiThresholdParam = (USER_RSSI_PARAMS *)(buf + 4);
    WMI_SNR_THRESHOLD_PARAMS_CMD *snrThresholdParam = (WMI_SNR_THRESHOLD_PARAMS_CMD *)buf;
    WMI_LQ_THRESHOLD_PARAMS_CMD *lqThresholdParam = (WMI_LQ_THRESHOLD_PARAMS_CMD *)(buf + 4);
    WMI_TARGET_ERROR_REPORT_BITMASK *pBitMask =
                                    (WMI_TARGET_ERROR_REPORT_BITMASK *)buf;
    TARGET_STATS_CMD tgtStatsCmd;
    WMI_SET_ASSOC_INFO_CMD *ieInfo = (WMI_SET_ASSOC_INFO_CMD *)buf;
    WMI_SET_ACCESS_PARAMS_CMD *acParamsCmd = (WMI_SET_ACCESS_PARAMS_CMD *)buf;
    WMI_DISC_TIMEOUT_CMD *discCmd = (WMI_DISC_TIMEOUT_CMD *)buf;
    WMI_SET_ADHOC_BSSID_CMD *adhocBssidCmd = (WMI_SET_ADHOC_BSSID_CMD *)(buf + 4);
    WMI_BEACON_INT_CMD *bconIntvl     = (WMI_BEACON_INT_CMD *)(buf + 4);
    WMI_SET_RETRY_LIMITS_CMD *setRetryCmd  = (WMI_SET_RETRY_LIMITS_CMD *)(buf + 4);
    WMI_START_SCAN_CMD *startScanCmd  = (WMI_START_SCAN_CMD *)(buf + 4);
    WMI_FIX_RATES_CMD *setFixRatesCmd  = (WMI_FIX_RATES_CMD *)(buf + 4);
    WMI_FIX_RATES_CMD *getFixRatesCmd  = (WMI_FIX_RATES_CMD *)(buf + 4);
    WMI_SET_AUTH_MODE_CMD *setAuthMode = (WMI_SET_AUTH_MODE_CMD *)(buf + 4);
    WMI_SET_REASSOC_MODE_CMD *setReassocMode = (WMI_SET_REASSOC_MODE_CMD *)(buf + 4);
    WMI_SET_LPREAMBLE_CMD *setLpreambleCmd = (WMI_SET_LPREAMBLE_CMD *)(buf + 4);
    WMI_SET_RTS_CMD *setRtsCmd = (WMI_SET_RTS_CMD *)(buf + 4);
    struct ar6000_queuereq *getQosQueueCmd = (struct ar6000_queuereq *)buf;
    WMI_SET_VOICE_PKT_SIZE_CMD *pSizeThresh = (WMI_SET_VOICE_PKT_SIZE_CMD *)(buf + sizeof(int));
    WMI_SET_MAX_SP_LEN_CMD *pMaxSP          = (WMI_SET_MAX_SP_LEN_CMD *)(buf + sizeof(int));
    WMI_SET_ROAM_CTRL_CMD *pRoamCtrl        = (WMI_SET_ROAM_CTRL_CMD *)(buf +
                                                sizeof(int));
    WMI_POWERSAVE_TIMERS_POLICY_CMD *pPowerSave    = (WMI_POWERSAVE_TIMERS_POLICY_CMD *)(buf + sizeof(int));
    WMI_POWER_MODE_CMD *getPowerMode = (WMI_POWER_MODE_CMD *)buf;
    WMI_SET_BT_STATUS_CMD *pBtStatCmd = (WMI_SET_BT_STATUS_CMD *) (buf + sizeof(int));
    WMI_SET_BT_PARAMS_CMD *pBtParmCmd = (WMI_SET_BT_PARAMS_CMD *) (buf + sizeof(int));
    WMI_SET_BTCOEX_FE_ANT_CMD *pBtcoexFeAntCmd = (WMI_SET_BTCOEX_FE_ANT_CMD *) (buf + sizeof(int));
    WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD *pBtcoexCoLocatedBtCmd = (WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD *) (buf + sizeof(int));
    WMI_SET_BTCOEX_SCO_CONFIG_CMD *pBtcoexScoConfigCmd = (WMI_SET_BTCOEX_SCO_CONFIG_CMD *) (buf + sizeof(int));
    WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD *pBtcoexbtinquiryPageConfigCmd = (WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD *) (buf + sizeof(int));
    WMI_BTCOEX_CONFIG_EVENT btcoexConfigEvent;
    WMI_BTCOEX_STATS_EVENT btcoexStatsEvent;
    AR6000_BTCOEX_CONFIG *pBtcoexConfig = (AR6000_BTCOEX_CONFIG *) (buf + sizeof(int));
    AR6000_BTCOEX_STATS *pBtcoexStatsEvent = (AR6000_BTCOEX_STATS *) (buf + sizeof(int));
    WMI_SET_BTCOEX_A2DP_CONFIG_CMD *pBtcoexA2dpConfigCmd = (WMI_SET_BTCOEX_A2DP_CONFIG_CMD *) (buf + sizeof(int));
    WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD *pBtcoexAclCoexConfigCmd = (WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD *) (buf + sizeof(int));
//    WMI_SET_BTCOEX_DEBUG_CMD *pBtcoexDebugCmd = (WMI_SET_BTCOEX_DEBUG_CMD *) (buf + sizeof(int));
    WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD *pBtcoexBtOperatingStatusCmd = (WMI_SET_BTCOEX_BT_OPERATING_STATUS_CMD *) (buf + sizeof(int));
    WMI_SET_WMM_CMD *setWmmCmd = (WMI_SET_WMM_CMD *)(buf + 4);
    WMI_SET_QOS_SUPP_CMD *qosSupp = (WMI_SET_QOS_SUPP_CMD *)(buf +4);
    WMI_SET_HB_CHALLENGE_RESP_PARAMS_CMD *hbparam = (WMI_SET_HB_CHALLENGE_RESP_PARAMS_CMD *)(buf + 4);
    A_UINT32 *cookie = (A_UINT32 *)(buf + 4);
    A_UINT32 *diagaddr = (A_UINT32 *)(buf + 4);
    A_UINT32 *diagdata = (A_UINT32 *)(buf + 8);
    WMI_SET_WMM_TXOP_CMD *pTxOp = (WMI_SET_WMM_TXOP_CMD *)(buf + sizeof(int));
    WMI_AP_SET_COUNTRY_CMD *pCountry = (WMI_AP_SET_COUNTRY_CMD *)(buf + sizeof(int));
    A_UINT32 *rd = (A_UINT32 *)(buf + 4);
    WMI_SET_KEEPALIVE_CMD *setKeepAlive = (WMI_SET_KEEPALIVE_CMD *)(buf + 4);
    WMI_GET_KEEPALIVE_CMD *getKeepAlive = (WMI_GET_KEEPALIVE_CMD *)(buf + 4);
    //WMI_SET_APPIE_CMD     *appIEInfo     = (WMI_SET_APPIE_CMD *)(buf + 4);
    struct ieee80211req_getset_appiebuf     *appIEInfo     = (struct ieee80211req_getset_appiebuf *)(buf + 4);
    A_UINT32              *pMgmtFilter  = (A_UINT32 *)(buf + 4);
    DBGLOG_MODULE_CONFIG *dbglogCfg = (DBGLOG_MODULE_CONFIG *)(buf + 4);

    WMI_SET_HOST_SLEEP_MODE_CMD *hostSleepModeCmd = (WMI_SET_HOST_SLEEP_MODE_CMD*)(buf + sizeof(int));
    WMI_SET_WOW_MODE_CMD *wowModeCmd = (WMI_SET_WOW_MODE_CMD*)(buf + sizeof(int));
    WMI_ADD_WOW_PATTERN_CMD *addWowCmd = (WMI_ADD_WOW_PATTERN_CMD*)(buf + sizeof(int));
    WMI_DEL_WOW_PATTERN_CMD *delWowCmd = (WMI_DEL_WOW_PATTERN_CMD*)(buf + sizeof(int));
    WMI_GET_WOW_LIST_CMD *getWowListCmd = (WMI_GET_WOW_LIST_CMD*)(buf + sizeof(int));
    A_UINT32 *connectCtrlFlags = (A_UINT32 *)(buf + 4);
    AR6000_USER_SETKEYS_INFO *user_setkeys_info =
                                (AR6000_USER_SETKEYS_INFO *)(buf + sizeof(int));
    WMI_SET_AKMP_PARAMS_CMD *akmpCtrlCmd =
                                (WMI_SET_AKMP_PARAMS_CMD *)(buf + sizeof(int));
    WMI_SET_TARGET_EVENT_REPORT_CMD *evtCfgCmd = (WMI_SET_TARGET_EVENT_REPORT_CMD *) (buf + sizeof(int));
    WMI_SET_DFS_CMD *setDfsCmd = (WMI_SET_DFS_CMD *)(buf + 4);
    pmkidUserInfo_t         pmkidUserInfo;
    A_UINT8                 bssid[ATH_MAC_LEN];
    struct ieee80211req_addpmkid *pi_cmd = (struct ieee80211req_addpmkid *)buf;

    int i, index = 0, channel, chindex, cnt;
    A_INT8 phyMode;
    A_INT16 threshold[26];  /* user can set rssi tags */
    A_UINT16 *clist;
    A_UCHAR *ssid;
    char *ethIf;

    WMI_AP_HIDDEN_SSID_CMD *pHidden = (WMI_AP_HIDDEN_SSID_CMD *)(buf + 4);
    ap_get_sta_t *pGetSta = (ap_get_sta_t *)(buf + 4);
    WMI_AP_ACL_MAC_CMD *pACL = (WMI_AP_ACL_MAC_CMD *)(buf + 4);
    WMI_AP_NUM_STA_CMD *pNumSta = (WMI_AP_NUM_STA_CMD *)(buf + 4);
    WMI_AP_ACL *pGetAcl = (WMI_AP_ACL *)(buf + 4);
    WMI_AP_CONN_INACT_CMD *pInact = (WMI_AP_CONN_INACT_CMD *)(buf + 4);
    WMI_AP_PROT_SCAN_TIME_CMD *pProt = (WMI_AP_PROT_SCAN_TIME_CMD *)(buf + 4);
    struct ieee80211req_mlme *pMlme = (struct ieee80211req_mlme *)buf;
    WMI_AP_SET_DTIM_CMD *pDtim = (WMI_AP_SET_DTIM_CMD *)(buf + 4);
    WMI_SET_IP_CMD *pIP = (WMI_SET_IP_CMD*)(buf + 4);
    WMI_AP_ACL_POLICY_CMD *pACLpolicy = (WMI_AP_ACL_POLICY_CMD *)(buf + 4);
    WMI_SET_HT_CAP_CMD *pHtCap = (WMI_SET_HT_CAP_CMD *)(buf + 4);
    WMI_SET_HT_OP_CMD *pHtOp = (WMI_SET_HT_OP_CMD *)(buf + 4);
    A_UINT8 *intra = (A_UINT8 *)(buf + 4);
    WMI_AP_MODE_STAT *apStat = (WMI_AP_MODE_STAT *)(buf + 4);
    WMI_SET_TX_SELECT_RATES_CMD *pTxSelectRate = (WMI_SET_TX_SELECT_RATES_CMD *)(buf + 4);
    WMI_ADDBA_REQ_CMD *pAddbaReq = (WMI_ADDBA_REQ_CMD *)(buf + 4);
    WMI_ALLOW_AGGR_CMD *pAllowAggr = (WMI_ALLOW_AGGR_CMD *)(buf + 4);
    WMI_DELBA_REQ_CMD *pDeleteAggr = (WMI_DELBA_REQ_CMD *)(buf + 4);
    WMI_SET_BT_WLAN_CONN_PRECEDENCE *prec = (WMI_SET_BT_WLAN_CONN_PRECEDENCE *) (buf + 4);
    WMI_AP_SET_11BG_RATESET_CMD *pAPrs = (WMI_AP_SET_11BG_RATESET_CMD *) (buf + 4);
    WMI_SET_TX_SGI_PARAM_CMD *set_txsgiparam = (WMI_SET_TX_SGI_PARAM_CMD *) (buf + 4);
    WMI_DIV_PARAMS_CMD *pDiversity = (WMI_DIV_PARAMS_CMD *)(buf + 4);
    WMI_AP_SET_APSD_CMD *pApApsd = (WMI_AP_SET_APSD_CMD *)(buf + 4);
    PACKET_LOG *log;
    profile_t   cp;
    CHIP_INTERNAL *p;
    A_UINT8 *pWpaOffloadState = (A_UINT8 *) (buf + 4);
    A_UINT32 *pExcessTxRetryThres = (A_UINT32 *)(buf + 4);

    if (argc == 1) {
        usage();
    }

    memset(buf, 0, sizeof(buf));
    memset(ifname, '\0', IFNAMSIZ);
    if ((ethIf = getenv("NETIF")) == NULL) {
        ethIf = "eth1";
    }
    strcpy(ifname, ethIf);
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        err(1, "socket");
    }

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"it", 1, NULL, 'a'},
            {"bg", 1, NULL, 'b'},
            {"np", 1, NULL, 'c'},
            {"dp", 1, NULL, 'd'},
            {"fgend", 1, NULL, 'e'},
            {"filter", 1, NULL, 'f'},
            {"fgstart", 1, NULL, 'g'},
            {"maxact", 1, NULL, 'h'},
            {"interface", 1, NULL, 'i'},
            {"createqos", 1, NULL, 'j'},
            {"deleteqos", 1, NULL, 'k'},
            {"listen", 1, NULL, 'l'},
            {"listenbeacons", 1, NULL, 'N'},
            {"pmparams", 0, NULL, 'm'},
            {"num", 1, NULL, 'n'},
            {"qosqueue", 1, NULL, 'o'},
            {"power", 1, NULL, 'p'},
            {"pas", 1, NULL, 'q'},
            {"scan", 0, NULL, 's'},
            {"sr", 1, NULL, 'r'},
            {"ssid", 1, NULL, 't'},
            {"rssiThreshold", 1, NULL, 'u'},
            {"snrThreshold", 1, NULL, WMI_SET_SNR_THRESHOLDS},
            {"cleanRssiSnr", 0, NULL, WMI_CLR_RSSISNR},
            {"lqThreshold", 1, NULL, WMI_SET_LQ_THRESHOLDS},
            {"version", 0, NULL, 'v'},
            {"wmode", 1, NULL, 'w'},
            {"badAP", 1, NULL, 'x'},
            {"clrAP", 0, NULL, 'y'},
            {"minact", 1, NULL, 'z'},
            {"getTargetStats", 0, NULL, WMI_GET_TARGET_STATS},
            {"setErrorReportingBitmask", 1, NULL,
                        WMI_SET_TARGET_ERROR_REPORTING_BITMASK},
            {"acparams", 0, NULL, WMI_SET_AC_PARAMS},
            {"acval", 1, NULL, WMI_SET_AC_VAL},
            {"txop", 1, NULL, 'A'},
            {"cwmin", 1, NULL, 'B'},
            {"cwmax", 1, NULL, 'C'},
            {"aifsn", 1, NULL, 'D'},
            {"ps", 1, NULL, 'E'},
            {"aw", 1, NULL, 'F'},
            {"adhocbssid", 1, NULL, 'G'},
            {"mode", 1, NULL, 'H'},
            {"sendframe", 1, NULL, 'I'},
            {"wlan", 1, NULL, 'J'},
            {"to", 1, NULL, 'K'},
            {"ttl", 1, NULL, 'L'},
            {"scanctrlflags", 1, NULL, 'O'},
            {"homeDwellTime", 1, NULL, 'P'},
            {"forceScanInt", 1, NULL, 'Q'},
            {"forceScanFlags",1, NULL, 'R'},
            {"threshold", 1, NULL, 'S'},
            {"frequency", 1, NULL, 'T'},
            {"cookie", 1, NULL, 'U'},
            {"mmask", 1, NULL, 'V'},
            {"rep", 1, NULL, 'W'},
            {"tsr", 1, NULL, 'X'},
            {"size", 1, NULL, 'Y'},
            {"bssid",1, NULL, WMI_BSSID},
            {"initrsc", 1, NULL, USER_SETKEYS_INITRSC},
            {"multipmkid", 1, NULL, WMI_AKMP_MULTI_PMKID},
            {"numpmkid", 1, NULL, WMI_NUM_PMKID},
            {"pmkid", 1, NULL, WMI_PMKID_ENTRY},
            {"clearStats", 0, NULL, 'Z'},
            {"maxact2pas", 1, NULL, WMI_SCAN_DFSCH_ACT_TIME},
            {"maxactscan_ssid", 1, NULL, WMI_SCAN_MAXACT_PER_SSID},
            {"ibsspmcaps", 0, NULL, WMI_SET_IBSS_PM_CAPS},
            {"appsparams", 0, NULL, WMI_SET_AP_PS},
            {"setAssocIe", 1, NULL, WMI_SET_ASSOC_IE},
            {"setbmisstime", 1, NULL, WMI_SET_BMISS_TIME},
            {"setbmissbeacons", 1, NULL, 'M'},
            {"disc", 1, NULL, WMI_SET_DISC_TIMEOUT},
            {"beaconintvl", 1, NULL, WMI_SET_BEACON_INT},
            {"setVoicePktSize", 1, NULL, WMI_SET_VOICE_PKT_SIZE},
            {"setMaxSPLength", 1, NULL, WMI_SET_MAX_SP},
            {"getroamtable", 0, NULL, WMI_GET_ROAM_TBL},
            {"roam", 1, NULL, WMI_SET_ROAM_CTRL},
            {"psparams", 0, NULL, WMI_SET_POWERSAVE_TIMERS},
            {"psPollTimer", 1, NULL, WMI_SET_POWERSAVE_TIMERS_PSPOLLTIMEOUT},
            {"triggerTimer", 1, NULL, WMI_SET_POWERSAVE_TIMERS_TRIGGERTIMEOUT},
            {"getpower", 0, NULL, WMI_GET_POWER_MODE},
            {"getroamdata", 0, NULL, WMI_GET_ROAM_DATA},
            {"setBTstatus", 1, NULL, WMI_SET_BT_STATUS},
            {"setBTparams", 1, NULL, WMI_SET_BT_PARAMS},
            {"setbtcoexfeant", 1, NULL, WMI_SET_BTCOEX_FE_ANT},
            {"setbtcoexcolocatedbt", 1, NULL, WMI_SET_BTCOEX_COLOCATED_BT_DEV},
            {"setbtcoexscoconfig", 1, NULL, WMI_SET_BTCOEX_SCO_CONFIG},
            {"setbtcoexa2dpconfig", 1, NULL, WMI_SET_BTCOEX_A2DP_CONFIG},
            {"setbtcoexaclcoexconfig", 1, NULL, WMI_SET_BTCOEX_ACLCOEX_CONFIG},
            {"setbtcoexbtinquirypageconfig", 1, NULL, WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG},
            {"setbtcoexbtoperatingstatus", 1, NULL, WMI_SET_BTCOEX_BT_OPERATING_STATUS},
            {"getbtcoexconfig",1,NULL, WMI_GET_BTCOEX_CONFIG},
            {"getbtcoexstats", 0, NULL, WMI_GET_BTCOEX_STATS},
            {"setretrylimits", 1, NULL, WMI_SET_RETRYLIMITS},
            {"startscan", 0, NULL, WMI_START_SCAN},
            {"setfixrates", 1, NULL, WMI_SET_FIX_RATES},
            {"getfixrates", 0, NULL, WMI_GET_FIX_RATES},
            {"setauthmode", 1, NULL, WMI_SET_AUTH_MODE},
            {"setreassocmode", 1, NULL, WMI_SET_REASSOC_MODE},
            {"setlongpreamble", 1, NULL, WMI_SET_LPREAMBLE},
            {"setRTS", 1, NULL, WMI_SET_RTS},
            {"setwmm", 1, NULL, WMI_SET_WMM},
            {"setqos", 1, NULL, WMI_SET_QOS_SUPP},
            {"apsdTimPolicy", 1, NULL, WMI_APSD_TIM_POLICY},
            {"simulatedAPSDTimPolicy", 1, NULL, WMI_SIMULATED_APSD_TIM_POLICY},
            {"detecterror", 0, NULL, WMI_SET_ERROR_DETECTION},
            {"getheartbeat", 0, NULL, WMI_GET_HB_CHALLENGE_RESP},
#ifdef USER_KEYS
            {"usersetkeys", 0, NULL, USER_SETKEYS},
#endif
            {"getRD", 0, NULL, WMI_GET_RD},
            {"setcountry", 1, NULL, WMI_AP_SET_COUNTRY},
            {"countrycodes", 0, NULL, WMI_AP_GET_COUNTRY_LIST},
            {"disableregulatory", 0, NULL, WMI_AP_DISABLE_REGULATORY},
            {"txopbursting", 1, NULL, WMI_SET_TXOP},
            {"diagaddr", 1, NULL, DIAG_ADDR},
            {"diagdata", 1, NULL, DIAG_DATA},
            {"diagread", 0, NULL, DIAG_READ},
            {"diagwrite", 0, NULL, DIAG_WRITE},
            {"setkeepalive", 1, NULL, WMI_SET_KEEPALIVE},
            {"getkeepalive", 0, NULL, WMI_GET_KEEPALIVE},
            {"setappie", 1, NULL, WMI_SET_APPIE},
            {"setmgmtfilter", 1, NULL, WMI_SET_MGMT_FRM_RX_FILTER},
            {"setdbglogconfig", 0, NULL, WMI_DBGLOG_CFG_MODULE},
            {"getdbglogs", 0, NULL, WMI_DBGLOG_GET_DEBUG_LOGS},
            {"sethostmode", 1, NULL, WMI_SET_HOST_SLEEP_MODE},
            {"setwowmode", 1, NULL, WMI_SET_WOW_MODE},
            {"wowfilter",1,NULL,WMI_SET_WOW_FILTER},
            {"hostreqdelay",1,NULL,WMI_SET_WOW_HOST_REQ_DELAY},
            {"getwowlist", 1, NULL, WMI_GET_WOW_LIST},
            {"addwowpattern", 1, NULL, WMI_ADD_WOW_PATTERN},
            {"delwowpattern", 1, NULL, WMI_DEL_WOW_PATTERN},
            {"dumpchipmem", 0, NULL, DIAG_DUMP_CHIP_MEM},
            {"dumpchipmem_venus", 0, NULL, DIAG_DUMP_CHIP_MEM_VENUS},
            {"setconnectctrl", 1, NULL, WMI_SET_CONNECT_CTRL_FLAGS},
            {"dumpcreditstates",0, NULL, DUMP_HTC_CREDITS},
            {"setakmp", 0, NULL, WMI_SET_AKMP_INFO},
            {"setpmkidlist", 0, NULL, WMI_SET_PMKID_LIST},
            {"getpmkidlist", 0, NULL, WMI_GET_PMKID_LIST},
            {"ieMask", 1, NULL, WMI_SET_IEMASK},
            {"scanlist", 1, NULL, WMI_SCAN_CHANNEL_LIST},
            {"setbsspmkid", 0, NULL, WMI_SET_BSS_PMKID_INFO},
            {"bsspmkid", 1, NULL, WMI_BSS_PMKID_ENTRY},
            {"abortscan", 0, NULL, WMI_ABORT_SCAN},
            {"settgtevt", 1, NULL, WMI_TARGET_EVENT_REPORT},
            {"getsta", 0, NULL, WMI_AP_GET_STA_LIST},       /* AP mode */
            {"hiddenssid", 0, NULL, WMI_AP_HIDDEN_SSID},    /* AP mode */
            {"numsta", 0, NULL, WMI_AP_SET_NUM_STA},        /* AP mode */
            {"aclpolicy", 0, NULL, WMI_AP_ACL_POLICY},      /* AP mode */
            {"addacl", 0, NULL, WMI_AP_ACL_MAC_LIST1},      /* AP mode */
            {"delacl", 0, NULL, WMI_AP_ACL_MAC_LIST2},      /* AP mode */
            {"getacl", 0, NULL, WMI_AP_GET_ACL_LIST},       /* AP mode */
            {"commit", 0, NULL, WMI_AP_COMMIT_CONFIG},      /* AP mode */
            {"conninact", 0, NULL, WMI_AP_INACT_TIME},      /* AP mode */
            {"protectionscan", 0, NULL, WMI_AP_PROT_TIME},  /* AP mode */
            {"removesta", 0, NULL, WMI_AP_SET_MLME},        /* AP mode */
            {"dtim", 0, NULL, WMI_AP_SET_DTIM},             /* AP mode */
            {"intrabss", 0, NULL, WMI_AP_INTRA_BSS},        /* AP mode */
            {"interbss", 0, NULL, WMI_AP_INTER_BSS},        /* AP mode */            
            {"ip", 1, NULL, WMI_GET_IP},
            {"setMcastFilter", 1, NULL, WMI_SET_MCAST_FILTER},
            {"delMcastFilter", 1, NULL, WMI_DEL_MCAST_FILTER},
            {"mcastFilter", 1, NULL, WMI_MCAST_FILTER},
            {"dump_recv_aggr_stats",0, NULL, WMI_DUMP_RCV_AGGR_STATS},
            {"setup_aggr", 2, NULL, WMI_SETUP_AGGR},
            {"allow_aggr", 2, NULL, WMI_CFG_ALLOW_AGGR},
            {"dele_aggr", 2, NULL, WMI_CFG_DELE_AGGR},
            {"set_ht_cap",1, NULL, WMI_SET_HT_CAP},
            {"set_ht_op",1, NULL, WMI_SET_HT_OP},
            {"apgetstats", 0, NULL, WMI_AP_GET_STAT},       /* AP mode */
            {"apclearstats", 0, NULL, WMI_AP_CLR_STAT},     /* AP mode */
            {"settxselrates", 1, NULL, WMI_SET_TX_SELECT_RATES},
            {"gethiddenssid", 0, NULL, WMI_AP_GET_HIDDEN_SSID},   /* AP mode */
            {"getcountry", 0, NULL, WMI_AP_GET_COUNTRY},    /* AP mode */
            {"getwmode", 0, NULL, WMI_AP_GET_WMODE},
            {"getdtim", 0, NULL, WMI_AP_GET_DTIM},          /* AP mode */
            {"getbeaconintvl", 0, NULL, WMI_AP_GET_BINTVL}, /* AP mode */
            {"getRTS", 0, NULL, WMI_GET_RTS},
            {"targregs", 0, NULL, DIAG_FETCH_TARGET_REGS},
#ifdef ATH_INCLUDE_PAL
            {"sendpalcmd", 2, NULL, WMI_SEND_PAL_CMD},
            {"sendpaldata", 2, NULL, WMI_SEND_PAL_DATA},
            {"wlan_conn_prec", 1, NULL, WMI_SET_WLAN_CONN_PRECDNCE},
#endif
            {"aprateset", 0, NULL, WMI_SET_AP_RATESET},
            {"twp", 1, NULL, WMI_SET_TX_WAKEUP_POLICY},
            {"nt", 1, NULL, WMI_SET_TX_NUM_FRAMES_TO_WAKEUP},
            {"pstype", 1, NULL, WMI_SET_AP_PS_PSTYPE},
            {"psit", 1, NULL, WMI_SET_AP_PS_IDLE_TIME},
            {"psperiod", 1, NULL, WMI_SET_AP_PS_PS_PERIOD},
            {"sleepperiod", 1, NULL, WMI_SET_AP_PS_SLEEP_PERIOD},
            {"connect", 1, NULL, WMI_SEND_CONNECT_CMD},
            {"wpa", 1, NULL, WMI_SEND_CONNECT_CMD1},
            {"wep", 1, NULL, WMI_SEND_CONNECT_CMD2},
            {"set_dfs", 1, NULL, WMI_AP_SET_DFS},
            {"bt",1,NULL,BT_HW_POWER_STATE},
            {"set_tx_sgi", 0, NULL, WMI_SET_TX_SGI_PARAM},
            {"masksgi", 1, NULL, WMI_SGI_MASK},
            {"persgi", 1, NULL, WMI_PER_SGI},
            {"wac", 1, NULL, WMI_WAC_ENABLE},
            {"setwpaoffload", 1, NULL, WMI_SET_WPA_OFFLOAD_STATE},
            {"acsdisablehichannels", 0, NULL, WMI_AP_ACS_DISABLE_HI_CHANNELS},
            {"setdivparam", 1, NULL, WMI_SET_DIVERSITY_PARAM},
            {"setexcesstxretrythres", 1, NULL, WMI_SET_EXCESS_TX_RETRY_THRES},
            {"forceAssert", 0, NULL, WMI_FORCE_ASSERT},            
            {"gnumsta", 0, NULL, WMI_AP_SET_GNUM_STA},        /* AP mode */
            {"getgnumsta", 0, NULL, WMI_AP_GET_GNUM_STA},     /* AP mode */
            {"getnumsta", 0, NULL, WMI_AP_GET_NUM_STA},       /* AP mode */            
            {"suspend", 0, NULL, WMI_SUSPEND_DRIVER},                
            {"resume", 0, NULL, WMI_RESUME_DRIVER},                 
            {"scanprobedssid", 1, NULL, WMI_SCAN_PROBED_SSID},  
            {"ap_apsd", 0, NULL, WMI_AP_SET_APSD},    /* AP mode */
            {"get_ht_cap", 0, NULL, WMI_GET_HT_CAP},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "rsvda:b:c:e:h:f:g:h:i:l:p:q:r:w:n:t:u:x:y:z:A:B:C:D:E:F:G:H:I:J:K:L:M:N:O:P:Q:R:S:T:U:V:W:X:Y:Z:", long_options, &option_index);
        if (c == -1)
        break;
        switch (c) {
        case 'a':
            pmParamCmd->idle_period = atoi(optarg);
            break;
        case 'b':
            if (!strcasecmp(optarg,"default")) {
                sParamCmd->bg_period = 0;
            } else {
                sParamCmd->bg_period = atoi(optarg);
                /* Setting the background scan to 0 or 65535 has the same effect
                 - it disables background scanning */
                if(!sParamCmd->bg_period)
                    sParamCmd->bg_period = 65535;
            }
            break;
        case 'c':
            pmParamCmd->pspoll_number = atoi(optarg);
            break;
        case 'd':
            if (!strcmp(optarg, "ignore")) {
                pmParamCmd->dtim_policy = IGNORE_DTIM;
            } else if (!strcmp(optarg, "normal")) {
                pmParamCmd->dtim_policy = NORMAL_DTIM;
            } else if (!strcmp(optarg, "stick")) {
                pmParamCmd->dtim_policy = STICK_DTIM;
            } else {
                cmd = 0;
            }
            break;
        case 'f':
            cmd = WMI_SET_BSS_FILTER;
            if (!strcmp(optarg, "none")) {
                filterCmd->bssFilter = NONE_BSS_FILTER;
            } else if (!strcmp(optarg, "all")) {
                filterCmd->bssFilter = ALL_BSS_FILTER;
            } else if (!strcmp(optarg, "profile")) {
                filterCmd->bssFilter = PROFILE_FILTER;
            } else if (!strcmp(optarg, "not_profile")) {
                filterCmd->bssFilter = ALL_BUT_PROFILE_FILTER;
            } else if (!strcmp(optarg, "bss")) {
                filterCmd->bssFilter = CURRENT_BSS_FILTER;
            } else if (!strcmp(optarg, "not_bss")) {
                filterCmd->bssFilter = ALL_BUT_BSS_FILTER;
            } else if (!strcmp(optarg, "ssid")) {
                filterCmd->bssFilter = PROBED_SSID_FILTER;
            } else {
                cmd = 0;
            }
            break;
        case 'e':
            sParamCmd->fg_end_period = atoi(optarg);
            break;
        case 'g':
            sParamCmd->fg_start_period = atoi(optarg);
            break;
        case 'h':
            sParamCmd->maxact_chdwell_time = atoi(optarg);
            break;
        case 'q':
            sParamCmd->pas_chdwell_time = atoi(optarg);
            break;
        case 'j':
            cmd = WMI_CREATE_QOS;
            crePStreamCmd->userPriority = atoi(optarg);
            break;
        case 'k':
            cmd = WMI_DELETE_QOS;
            delPStreamCmd->trafficClass = atoi(optarg);
            break;
        case 'l':
            cmd = WMI_SET_LISTEN_INTERVAL;
            listenCmd->listenInterval = atoi(optarg);
            if ((listenCmd->listenInterval < MIN_LISTEN_INTERVAL) ||
                (listenCmd->listenInterval > MAX_LISTEN_INTERVAL))
            {
                printf("Listen Interval out of range\n");
                cmd = 0;
            }
            break;
        case 'N':
            cmd =  WMI_SET_LISTEN_INTERVAL;
            listenCmd->numBeacons = atoi(optarg);
            if ((listenCmd->numBeacons < MIN_LISTEN_BEACONS) ||
                (listenCmd->numBeacons > MAX_LISTEN_BEACONS))
            {
                printf("Listen beacons out of range\n");
                cmd = 0;
            }
            break;
        case 'm':
            cmd = WMI_SET_PM_PARAMS;
            break;
        case 'n':
            index = atoi(optarg);
            break;
        case 'v':
            cmd = WMI_GET_VERSION;
            break;
        case 'o':
            cmd = WMI_GET_QOS_QUEUE;
            getQosQueueCmd->trafficClass = atoi(optarg);
            break;
        case 'p':
            cmd = WMI_SET_POWER_MODE;
            if (!strcmp(optarg, "rec")) {
                pwrCmd->powerMode = REC_POWER;
            } else if (!strcmp(optarg, "maxperf")) {
                pwrCmd->powerMode = MAX_PERF_POWER;
            } else {
                cmd = 0;
            }
            break;
        case WMI_FORCE_ASSERT:
            cmd = WMI_FORCE_ASSERT;
            break;
        case WMI_SET_MCAST_FILTER:
            cmd = WMI_SET_MCAST_FILTER;
            sMcastFilterCmd->multicast_mac[0] = (unsigned int) atoi(argv[2]);
            sMcastFilterCmd->multicast_mac[1] = (unsigned int) atoi(argv[3]);
            sMcastFilterCmd->multicast_mac[2] = (unsigned int) atoi(argv[4]);
            sMcastFilterCmd->multicast_mac[3] = (unsigned int) atoi(argv[5]);
            sMcastFilterCmd->multicast_mac[4] = (unsigned int) atoi(argv[6]);
            sMcastFilterCmd->multicast_mac[5] = (unsigned int) atoi(argv[7]);

            printf("sMcastFilterCmd->multicast_mac[0]  %d\n",sMcastFilterCmd->multicast_mac[0] );
            printf("sMcastFilterCmd->multicast_mac[1]  %d\n",sMcastFilterCmd->multicast_mac[1] );
            printf("sMcastFilterCmd->multicast_mac[2]  %d\n",sMcastFilterCmd->multicast_mac[2] );
            printf("sMcastFilterCmd->multicast_mac[3]  %d\n",sMcastFilterCmd->multicast_mac[3] );
            printf("sMcastFilterCmd->multicast_mac[4]  %d\n",sMcastFilterCmd->multicast_mac[4] );
            printf("sMcastFilterCmd->multicast_mac[5]  %d\n",sMcastFilterCmd->multicast_mac[5] );
            break;
        case WMI_DEL_MCAST_FILTER:
            cmd = WMI_DEL_MCAST_FILTER;
            sMcastFilterCmd->multicast_mac[0] = (unsigned int) atoi(argv[2]);
            sMcastFilterCmd->multicast_mac[1] = (unsigned int) atoi(argv[3]);
            sMcastFilterCmd->multicast_mac[2] = (unsigned int) atoi(argv[4]);
            sMcastFilterCmd->multicast_mac[3] = (unsigned int) atoi(argv[5]);
            sMcastFilterCmd->multicast_mac[4] = (unsigned int) atoi(argv[6]);
            sMcastFilterCmd->multicast_mac[5] = (unsigned int) atoi(argv[7]);

            printf("sMcastFilterCmd->multicast_mac[0]  %d\n",sMcastFilterCmd->multicast_mac[0] );
            printf("sMcastFilterCmd->multicast_mac[1]  %d\n",sMcastFilterCmd->multicast_mac[1] );
            printf("sMcastFilterCmd->multicast_mac[2]  %d\n",sMcastFilterCmd->multicast_mac[2] );
            printf("sMcastFilterCmd->multicast_mac[3]  %d\n",sMcastFilterCmd->multicast_mac[3] );
            printf("sMcastFilterCmd->multicast_mac[4]  %d\n",sMcastFilterCmd->multicast_mac[4] );
            printf("sMcastFilterCmd->multicast_mac[5]  %d\n",sMcastFilterCmd->multicast_mac[5] );
            break;
        case WMI_MCAST_FILTER:
            cmd = WMI_MCAST_FILTER;

            mcastFilterCmd->enable = (unsigned int) atoi(argv[2]);
            printf("Multicast Filter State: %s\n", mcastFilterCmd->enable ? "enable" : "disable");
            break;
        case 'r':
            sParamCmd->shortScanRatio = atoi(optarg);
            break;
        case 's':
            cmd = WMI_SET_SCAN_PARAMS;
            sParamCmd->scanCtrlFlags = DEFAULT_SCAN_CTRL_FLAGS;
            sParamCmd->shortScanRatio = WMI_SHORTSCANRATIO_DEFAULT;
            sParamCmd->max_dfsch_act_time = 0 ;
            break;
        case WMI_SCAN_DFSCH_ACT_TIME:
            sParamCmd->max_dfsch_act_time = atoi(optarg);
            break;
        case WMI_SCAN_MAXACT_PER_SSID:
            sParamCmd->maxact_scan_per_ssid = atoi(optarg);
            break;
        case 't':
            cmd = WMI_SET_SSID;
            ssid = (A_UCHAR *)optarg;
            break;
        case 'u':
            cmd = WMI_SET_RSSI_THRESHOLDS;
            memset(threshold, 0, sizeof(threshold));
            for (index = optind; index <= argc; index++)
                threshold[index-optind] = atoi(argv[index-1]);

            rssiThresholdParam->weight                  = threshold[0];
            rssiThresholdParam->pollTime                = threshold[1];
            rssiThresholdParam->tholds[0].tag           = threshold[2];
            rssiThresholdParam->tholds[0].rssi          = 0 - threshold[3];
            rssiThresholdParam->tholds[1].tag           = threshold[4];
            rssiThresholdParam->tholds[1].rssi          = 0 - threshold[5];
            rssiThresholdParam->tholds[2].tag           = threshold[6];
            rssiThresholdParam->tholds[2].rssi          = 0 - threshold[7];
            rssiThresholdParam->tholds[3].tag           = threshold[8];
            rssiThresholdParam->tholds[3].rssi          = 0 - threshold[9];
            rssiThresholdParam->tholds[4].tag           = threshold[10];
            rssiThresholdParam->tholds[4].rssi          = 0 - threshold[11];
            rssiThresholdParam->tholds[5].tag           = threshold[12];
            rssiThresholdParam->tholds[5].rssi          = 0 - threshold[13];
            rssiThresholdParam->tholds[6].tag           = threshold[14];
            rssiThresholdParam->tholds[6].rssi          = 0 - threshold[15];
            rssiThresholdParam->tholds[7].tag           = threshold[16];
            rssiThresholdParam->tholds[7].rssi          = 0 - threshold[17];
            rssiThresholdParam->tholds[8].tag           = threshold[18];
            rssiThresholdParam->tholds[8].rssi          = 0 - threshold[19];
            rssiThresholdParam->tholds[9].tag           = threshold[20];
            rssiThresholdParam->tholds[9].rssi          = 0 - threshold[21];
            rssiThresholdParam->tholds[10].tag           = threshold[22];
            rssiThresholdParam->tholds[10].rssi          = 0 - threshold[23];
            rssiThresholdParam->tholds[11].tag           = threshold[24];
            rssiThresholdParam->tholds[11].rssi          = 0 - threshold[25];

            break;
        case WMI_SET_SNR_THRESHOLDS:
            cmd = WMI_SET_SNR_THRESHOLDS;
            memset(threshold, 0, sizeof(threshold));
            for (index = optind; index <= argc; index++)
                threshold[index-optind] = atoi(argv[index-1]);

            snrThresholdParam->weight                  = threshold[0];
            snrThresholdParam->thresholdAbove1_Val     = threshold[1];
            snrThresholdParam->thresholdAbove2_Val     = threshold[2];
            snrThresholdParam->thresholdAbove3_Val     = threshold[3];
            snrThresholdParam->thresholdAbove4_Val     = threshold[4];
            snrThresholdParam->thresholdBelow1_Val    = threshold[5];
            snrThresholdParam->thresholdBelow2_Val    = threshold[6];
            snrThresholdParam->thresholdBelow3_Val    = threshold[7];
            snrThresholdParam->thresholdBelow4_Val    = threshold[8];
            snrThresholdParam->pollTime                = threshold[9];
            break;
        case WMI_CLR_RSSISNR:
            cmd = WMI_CLR_RSSISNR;
            break;
        case WMI_SET_LQ_THRESHOLDS:
            cmd = WMI_SET_LQ_THRESHOLDS;
            memset(threshold, 0, sizeof(threshold));
            for (index = optind; index <= argc; index++)
                threshold[index-optind] = atoi(argv[index-1]);

            lqThresholdParam->enable                       = threshold[0];
            lqThresholdParam->thresholdAbove1_Val          = threshold[1];
            lqThresholdParam->thresholdAbove2_Val          = threshold[2];
            lqThresholdParam->thresholdAbove3_Val          = threshold[3];
            lqThresholdParam->thresholdAbove4_Val          = threshold[4];
            lqThresholdParam->thresholdBelow1_Val          = threshold[5];
            lqThresholdParam->thresholdBelow2_Val          = threshold[6];
            lqThresholdParam->thresholdBelow3_Val          = threshold[7];
            lqThresholdParam->thresholdBelow4_Val          = threshold[8];

            break;
        case 'i':
            memset(ifname, '\0', 8);
            strcpy(ifname, optarg);
            break;
        case 'w':
            cmd = WMI_SET_CHANNEL;
            chParamCmd->numChannels = 0;
            chParamCmd->scanParam = 0;
            break;
        case 'x':
            if (wmic_ether_aton(optarg, badApCmd->bssid) != A_OK) {
                printf("bad mac address\n");
                break;
            }
            cmd = WMI_SET_BADAP;
            break;
        case 'y':
            /*
             * we are clearing a bad AP. We pass a null mac address
             */
            cmd = WMI_DELETE_BADAP;
            break;
        case 'z':
            sParamCmd->minact_chdwell_time = atoi(optarg);
            break;
        case WMI_GET_TARGET_STATS:
            cmd = WMI_GET_TARGET_STATS;
            break;
        case WMI_SET_TARGET_ERROR_REPORTING_BITMASK:
            cmd = WMI_SET_TARGET_ERROR_REPORTING_BITMASK;
            pBitMask->bitmask = atoi(optarg);
            printf("Setting the bitmask = 0x%x\n", pBitMask->bitmask);
            break;
        case WMI_SET_ASSOC_IE:
            cmd = WMI_SET_ASSOC_INFO_CMDID;
            ieInfo->ieType = 1;
            if (strlen(optarg) > WMI_MAX_ASSOC_INFO_LEN) {
                printf("IE Size cannot be greater than %d\n",
                        WMI_MAX_ASSOC_INFO_LEN);
                cmd = 0;
            } else {
                ieInfo->bufferSize = strlen(optarg) + 2;
                memcpy(&ieInfo->assocInfo[2], optarg,
                       ieInfo->bufferSize - 2);
               ieInfo->assocInfo[0] = 0xdd;
               ieInfo->assocInfo[1] = ieInfo->bufferSize - 2;
            }
            break;
        case WMI_SET_BMISS_TIME:
            cmd = WMI_SET_BMISS_TIME;
            bmissCmd->bmissTime = atoi(optarg);
            if ((bmissCmd->bmissTime < MIN_BMISS_TIME) ||
                (bmissCmd->bmissTime > MAX_BMISS_TIME))
            {
                printf("BMISS time out of range\n");
                cmd = 0;
            } 
            break;
        case 'M':
            cmd = WMI_SET_BMISS_TIME;
            bmissCmd->numBeacons =  atoi(optarg);
            if ((bmissCmd->numBeacons < MIN_BMISS_BEACONS) ||
                (bmissCmd->numBeacons > MAX_BMISS_BEACONS))
            {
                printf("BMISS beacons out of range\n");
                cmd = 0;
            }
            break;

        case WMI_SET_AC_PARAMS:
            cmd = WMI_SET_AC_PARAMS;
            break;
        case WMI_SET_AC_VAL:
            acParamsCmd->ac = atoi(optarg);
            break;
        case 'A':
            acParamsCmd->txop = atoi(optarg);
            break;
        case 'B':
            acParamsCmd->eCWmin = atoi(optarg);
            break;
        case 'C':
            acParamsCmd->eCWmax = atoi(optarg);
            break;
        case 'D':
            acParamsCmd->aifsn = atoi(optarg);
            break;
        case 'E':
            if (!strcmp(optarg, "disable")) {
                adhocPmCmd->power_saving = ADHOC_PS_DISABLE;
            } else if (!strcmp(optarg, "atheros")) {
                adhocPmCmd->power_saving = ADHOC_PS_ATH;
            } else if (!strcmp(optarg, "ieee")) {
                adhocPmCmd->power_saving = ADHOC_PS_IEEE;
            } else {
                cmd = 0;
            }

            break;
        case 'F':
            adhocPmCmd->atim_windows = atoi(optarg);
            break;
        case 'G':
            if (wmic_ether_aton(optarg, adhocBssidCmd->bssid) != A_OK) {
                printf("bad mac address\n");
                break;
            }
            printf("adhoc bssid address, %x\n", adhocBssidCmd->bssid[0]);
            cmd = WMI_SET_ADHOC_BSSID;
            break;

       case 'J':
            cmd = WMI_SET_WLAN_STATE;
            if (!strcmp(optarg, "enable")) {
                ((int *)buf)[1] = WLAN_ENABLED;
            } else if (!strcmp(optarg, "disable")) {
                ((int *)buf)[1] = WLAN_DISABLED;
            } else if (!strcmp(optarg, "query")) {
                cmd = WMI_GET_WLAN_STATE;
            } else {
                usage();
            }
            break;
        case 'K':
            adhocPmCmd->timeout_value = atoi(optarg);
            break;
       case 'O':
            index = optind;
            index--;
            if((index + 6) > argc) {  /*6 is the number of  flags
                                       scanctrlflags takes  */
                printf("Incorrect number of scanctrlflags\n");
                cmd = 0;
                break;
           }
            sParamCmd->scanCtrlFlags = 0;
            if (atoi(argv[index]) == 1)
                sParamCmd->scanCtrlFlags |= CONNECT_SCAN_CTRL_FLAGS;
            index++;
            if (atoi(argv[index]) == 1)
                sParamCmd->scanCtrlFlags |= SCAN_CONNECTED_CTRL_FLAGS;
            index++;
            if (atoi(argv[index]) == 1)
                sParamCmd->scanCtrlFlags |= ACTIVE_SCAN_CTRL_FLAGS;
            index++;
            if (atoi(argv[index]) == 1)
                sParamCmd->scanCtrlFlags |= ROAM_SCAN_CTRL_FLAGS;
            index++;
            if (atoi(argv[index]) == 1)
                sParamCmd->scanCtrlFlags |= REPORT_BSSINFO_CTRL_FLAGS;
            index++;
            if(atoi(argv[index]) == 1)
               sParamCmd->scanCtrlFlags |= ENABLE_AUTO_CTRL_FLAGS;
            index++;
            if (argc - index) {
                if(atoi(argv[index]) == 1)
                   sParamCmd->scanCtrlFlags |= ENABLE_SCAN_ABORT_EVENT;
                index++;
            }
            if(!sParamCmd->scanCtrlFlags) {
               sParamCmd->scanCtrlFlags = 255; /* all flags have being disabled by the user */
            }
            break;
       case 'L':
            adhocPmCmd->ttl = atoi(optarg);
            break;
       case  'P':
            startScanCmd->homeDwellTime =atoi(optarg);
            break;
       case 'Q':
            startScanCmd->forceScanInterval =atoi(optarg);
            break;
       case 'R':
            index = optind;
            index--;
            if((index + 3) > argc) {
                printf("Incorrect number of forceScanCtrlFlags\n");
                cmd = 0;
                break;
            }
            startScanCmd->scanType = atoi(argv[index]);
            index++;
            startScanCmd->forceFgScan = atoi(argv[index]);
            index++;
            startScanCmd->isLegacy = atoi(argv[index]);
            index++;
            break;
       case WMI_SCAN_CHANNEL_LIST:
            chindex = 0;
            index = optind - 1;
            clist = startScanCmd->channelList;

            while (argv[index] != NULL) {
                channel = atoi(argv[index]);
                if (channel < 255) {
                    /*
                     * assume channel is a ieee channel #
                     */
                clist[chindex] = wmic_ieee2freq(channel);
                } else {
                    clist[chindex] = channel;
                }
                chindex++;
                index++;
            }
            startScanCmd->numChannels = chindex;
            break;
       case WMI_SET_IBSS_PM_CAPS:
            cmd = WMI_SET_IBSS_PM_CAPS;
            break;
       case WMI_SET_AP_PS:
            cmd = WMI_SET_AP_PS;
            break;
        case WMI_SET_DISC_TIMEOUT:
            cmd = WMI_SET_DISC_TIMEOUT;
            discCmd->disconnectTimeout = atoi(optarg);
            break;
        case WMI_SET_BEACON_INT:
            cmd = WMI_SET_BEACON_INT;
            bconIntvl->beaconInterval = atoi(optarg);
            break;
        case WMI_SET_VOICE_PKT_SIZE:
            cmd = WMI_SET_VOICE_PKT_SIZE;
            pSizeThresh->voicePktSize = atoi(optarg);
            break;
        case WMI_SET_MAX_SP:
            cmd = WMI_SET_MAX_SP;
            pMaxSP->maxSPLen = atoi(optarg);
            break;
        case WMI_GET_ROAM_TBL:
            cmd = WMI_GET_ROAM_TBL;
            break;
        case WMI_SET_ROAM_CTRL:
            pRoamCtrl->roamCtrlType = atoi(optarg);
            if (A_OK != wmic_validate_roam_ctrl(pRoamCtrl, argc-optind, argv)) {
                break;
            }
            cmd = WMI_SET_ROAM_CTRL;
            break;
        case WMI_SET_POWERSAVE_TIMERS:
            cmd = WMI_SET_POWERSAVE_TIMERS;
            break;
        case WMI_SET_POWERSAVE_TIMERS_PSPOLLTIMEOUT:
            pPowerSave->psPollTimeout = atoi(optarg);
            break;
        case WMI_SET_POWERSAVE_TIMERS_TRIGGERTIMEOUT:
            pPowerSave->triggerTimeout = atoi(optarg);
            break;
        case WMI_GET_POWER_MODE:
            cmd = WMI_GET_POWER_MODE;
            break;
        case WMI_GET_ROAM_DATA:
            cmd = WMI_GET_ROAM_DATA;
            break;
        case WMI_SET_BT_STATUS:
            cmd = WMI_SET_BT_STATUS;
            pBtStatCmd->streamType = atoi(optarg);
            pBtStatCmd->status = atoi(argv[optind]);
            if (pBtStatCmd->streamType >= BT_STREAM_MAX ||
                pBtStatCmd->status >= BT_STATUS_MAX)
            {
                fprintf(stderr, "Invalid parameters.\n");
                exit(0);
            }
            break;
        case WMI_SET_BT_PARAMS:
            cmd = WMI_SET_BT_PARAMS;
            pBtParmCmd->paramType = atoi(optarg);
            if (pBtParmCmd->paramType >= BT_PARAM_MAX)
            {
                fprintf(stderr, "Invalid parameters.\n");
                exit(0);
            }
            if (BT_PARAM_SCO == pBtParmCmd->paramType) {
                pBtParmCmd->info.scoParams.numScoCyclesForceTrigger =
                                    strtoul(argv[optind], NULL, 0);
                pBtParmCmd->info.scoParams.dataResponseTimeout =
                    strtoul(argv[optind+1], NULL, 0);
                pBtParmCmd->info.scoParams.stompScoRules =
                    strtoul(argv[optind+2], NULL, 0);
                pBtParmCmd->info.scoParams.scoOptFlags =
                    strtoul(argv[optind+3], NULL, 0);
                pBtParmCmd->info.scoParams.stompDutyCyleVal =
                    strtoul(argv[optind+4], NULL, 0);
                pBtParmCmd->info.scoParams.stompDutyCyleMaxVal =
                    strtoul(argv[optind+5], NULL, 0);
                pBtParmCmd->info.scoParams. psPollLatencyFraction =
                    strtoul(argv[optind+6], NULL, 0);
                pBtParmCmd->info.scoParams.noSCOSlots =
                    strtoul(argv[optind+7], NULL, 0);
                pBtParmCmd->info.scoParams.noIdleSlots =
                    strtoul(argv[optind+8], NULL, 0);
                pBtParmCmd->info.scoParams.scoOptOffRssi =
                    strtoul(argv[optind+9], NULL, 0);
                pBtParmCmd->info.scoParams.scoOptOnRssi =
                    strtoul(argv[optind+10], NULL, 0);
                pBtParmCmd->info.scoParams.scoOptRtsCount =
                    strtoul(argv[optind+11], NULL, 0);
            }else if (BT_PARAM_A2DP == pBtParmCmd->paramType) {
                pBtParmCmd->info.a2dpParams.a2dpWlanUsageLimit =
                    strtoul(argv[optind], NULL, 0);
                pBtParmCmd->info.a2dpParams.a2dpBurstCntMin =
                    strtoul(argv[optind+1 ], NULL, 0);
                pBtParmCmd->info.a2dpParams.a2dpDataRespTimeout =
                    strtoul(argv[optind+2 ], NULL, 0);
                pBtParmCmd->info.a2dpParams.a2dpOptFlags =
                    strtoul(argv[optind+3 ], NULL, 0);
                pBtParmCmd->info.a2dpParams.isCoLocatedBtRoleMaster =
                    strtoul(argv[optind+4 ], NULL, 0);
                pBtParmCmd->info.a2dpParams.a2dpOptOffRssi =
                    strtoul(argv[optind+5], NULL, 0);
                pBtParmCmd->info.a2dpParams.a2dpOptOnRssi =
                    strtoul(argv[optind+6], NULL, 0);
                pBtParmCmd->info.a2dpParams.a2dpOptRtsCount =
                    strtoul(argv[optind+7 ], NULL, 0);

            }else if (BT_PARAM_ANTENNA_CONFIG == pBtParmCmd->paramType) {
                pBtParmCmd->info.antType = strtoul(argv[optind], NULL, 0);
            } else if (BT_PARAM_COLOCATED_BT_DEVICE == pBtParmCmd->paramType) {
                pBtParmCmd->info.coLocatedBtDev =
                                         strtoul(argv[optind], NULL, 0);
            }else if(BT_PARAM_ACLCOEX == pBtParmCmd->paramType) {
                pBtParmCmd->info.aclCoexParams.aclWlanMediumUsageTime =
                                            strtoul(argv[optind], NULL, 0);
                pBtParmCmd->info.aclCoexParams.aclBtMediumUsageTime =
                                            strtoul(argv[optind+1], NULL, 0);
                pBtParmCmd->info.aclCoexParams.aclDataRespTimeout =
                                            strtoul(argv[optind+2], NULL, 0);
                pBtParmCmd->info.aclCoexParams.aclDetectTimeout =
                                            strtoul(argv[optind+3], NULL, 0);
                pBtParmCmd->info.aclCoexParams.aclmaxPktCnt =
                                            strtoul(argv[optind + 4], NULL, 0);
            } else if (BT_PARAM_11A_SEPARATE_ANT == pBtParmCmd->paramType) {
                printf("BT_PARAM_11A_SEPARATE_ANT \n");
            }
            else
            {
                fprintf(stderr, "Invalid parameters.\n");
                exit(0);
            }
            break;
        case WMI_SET_BTCOEX_FE_ANT:
            cmd = WMI_SET_BTCOEX_FE_ANT;
            pBtcoexFeAntCmd->btcoexFeAntType = atoi(optarg);
            if (pBtcoexFeAntCmd->btcoexFeAntType >= WMI_BTCOEX_FE_ANT_TYPE_MAX) {
                printf("Invalid configuration [1-Single Antenna,  2- dual antenna low isolation, 3 - dual antenna high isolation\n");
                printf("4 - bypass mode, 5 - combine mode]\n");
                exit(-1);
            }
            break;
    case WMI_SET_BTCOEX_COLOCATED_BT_DEV:
        cmd = WMI_SET_BTCOEX_COLOCATED_BT_DEV;
        pBtcoexCoLocatedBtCmd->btcoexCoLocatedBTdev = atoi(optarg);
            if (pBtcoexCoLocatedBtCmd->btcoexCoLocatedBTdev > 4) {
                printf("Invalid configuration %d\n",
                        pBtcoexCoLocatedBtCmd->btcoexCoLocatedBTdev);
                exit(-1);
            }
        printf("btcoex colocated antType = %d\n",
                   pBtcoexCoLocatedBtCmd->btcoexCoLocatedBTdev);
        break;
    case WMI_SET_BTCOEX_SCO_CONFIG:
        cmd = WMI_SET_BTCOEX_SCO_CONFIG;
        index = optind - 1;
        if((index + 17) > argc) {
            printf("Incorrect number of sco Config\n");
            exit(-1);
        }
        pBtcoexScoConfigCmd->scoConfig.scoSlots =  atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoConfig.scoIdleSlots = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoConfig.scoFlags = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoConfig.linkId = atoi(argv[index++]);

        pBtcoexScoConfigCmd->scoPspollConfig.scoCyclesForceTrigger = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoPspollConfig.scoDataResponseTimeout = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoPspollConfig.scoStompDutyCyleVal = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoPspollConfig.scoStompDutyCyleMaxVal = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoPspollConfig.scoPsPollLatencyFraction = atoi(argv[index++]);


        pBtcoexScoConfigCmd->scoOptModeConfig.scoStompCntIn100ms = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoOptModeConfig.scoContStompMax = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoOptModeConfig.scoMinlowRateMbps = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoOptModeConfig.scoLowRateCnt = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoOptModeConfig.scoHighPktRatio = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoOptModeConfig.scoMaxAggrSize = atoi(argv[index++]);

        pBtcoexScoConfigCmd->scoWlanScanConfig.scanInterval = atoi(argv[index++]);
        pBtcoexScoConfigCmd->scoWlanScanConfig.maxScanStompCnt = atoi(argv[index++]);
        break;
    case WMI_SET_BTCOEX_A2DP_CONFIG:
        cmd = WMI_SET_BTCOEX_A2DP_CONFIG;
        index = optind - 1;
        if((index + 10) > argc ) {
            printf("Incorrect number of A2DP Config\n");
            exit(-1);
        }
        pBtcoexA2dpConfigCmd->a2dpConfig.a2dpFlags =  atoi(argv[index++]);
        pBtcoexA2dpConfigCmd->a2dpConfig.linkId = atoi(argv[index++]);
        pBtcoexA2dpConfigCmd->a2dppspollConfig.a2dpWlanMaxDur = atoi(argv[index++]);
        pBtcoexA2dpConfigCmd->a2dppspollConfig.a2dpMinBurstCnt = atoi(argv[index++]);
        pBtcoexA2dpConfigCmd->a2dppspollConfig.a2dpDataRespTimeout = atoi(argv[index++]);

        pBtcoexA2dpConfigCmd->a2dpOptConfig.a2dpMinlowRateMbps = atoi(argv[index++]);
        pBtcoexA2dpConfigCmd->a2dpOptConfig.a2dpLowRateCnt = atoi(argv[index++]);
        pBtcoexA2dpConfigCmd->a2dpOptConfig.a2dpHighPktRatio = atoi(argv[index++]);
        pBtcoexA2dpConfigCmd->a2dpOptConfig.a2dpMaxAggrSize = atoi(argv[index++]);
        pBtcoexA2dpConfigCmd->a2dpOptConfig.a2dpPktStompCnt = atoi(argv[index++]);

        printf("a2dp Config, flags=%x\n", pBtcoexA2dpConfigCmd->a2dpConfig.a2dpFlags);
        break;
    case WMI_SET_BTCOEX_ACLCOEX_CONFIG:
        cmd = WMI_SET_BTCOEX_ACLCOEX_CONFIG;
        index = optind - 1;
        if((index + 14) > argc ) {
            printf("Incorrect number of ACL COEX Config\n");
            exit(-1);
        }
        pBtcoexAclCoexConfigCmd->aclCoexConfig.aclWlanMediumDur     =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexConfig.aclBtMediumDur       =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexConfig.aclDetectTimeout     =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexConfig.aclPktCntLowerLimit  =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexConfig.aclIterForEnDis      =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexConfig.aclPktCntUpperLimit  =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexConfig.aclCoexFlags         =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexConfig.linkId               =  atoi(argv[index++]);

        pBtcoexAclCoexConfigCmd->aclCoexPspollConfig.aclDataRespTimeout =  atoi(argv[index++]);

        pBtcoexAclCoexConfigCmd->aclCoexOptConfig.aclCoexMinlowRateMbps  =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexOptConfig.aclCoexLowRateCnt  =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexOptConfig.aclCoexHighPktRatio  =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexOptConfig.aclCoexMaxAggrSize  =  atoi(argv[index++]);
        pBtcoexAclCoexConfigCmd->aclCoexOptConfig.aclPktStompCnt  =  atoi(argv[index++]);
        break;

    case WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG:
        cmd = WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG;
        index = optind - 1;
        if((index + 3) > argc) {
            printf("Incorrect number of inquiry_page Config\n");
            exit(-1);
        }
        pBtcoexbtinquiryPageConfigCmd->btInquiryDataFetchFrequency = atoi(argv[index++]);
        pBtcoexbtinquiryPageConfigCmd->protectBmissDurPostBtInquiry = atoi(argv[index++]);
        pBtcoexbtinquiryPageConfigCmd->btInquiryPageFlag = atoi(argv[index++]);
        break;

    case WMI_SET_BTCOEX_BT_OPERATING_STATUS:
        cmd = WMI_SET_BTCOEX_BT_OPERATING_STATUS;
        index = optind - 1;
        if((index + 3) > argc) {
            printf("Incorrect number of operating status cmdn");
            exit(-1);
        }

        pBtcoexBtOperatingStatusCmd->btProfileType =atoi(argv[index++]);
        pBtcoexBtOperatingStatusCmd->btOperatingStatus =atoi(argv[index++]);
        pBtcoexBtOperatingStatusCmd->btLinkId =atoi(argv[index++]);
        break;

    case WMI_GET_BTCOEX_CONFIG:
        cmd = WMI_GET_BTCOEX_CONFIG;
        index = optind - 1;
        if((index + 2) > argc) {
            printf("Incorrect number of get Config\n");
            exit(-1);
        }
        pBtcoexConfig->configCmd.btProfileType = atoi(argv[index++]);
        pBtcoexConfig->configCmd.linkId = atoi(argv[index++]);
        break;
    case WMI_GET_BTCOEX_STATS:
        cmd = WMI_GET_BTCOEX_STATS;
        break;
        case WMI_SET_RETRYLIMITS:
            index = optind - 1;
            setRetryCmd->frameType = atoi(argv[index++]);
            if (setRetryCmd->frameType > 2) {
                printf("Invalid frame type! [0 - 2]\n");
                exit(-1);
            }
            setRetryCmd->trafficClass = atoi(argv[index++]);
            if (setRetryCmd->trafficClass > 3) {
                printf("Invalid traffic class! [0 - 3]\n");
                exit(-1);
            }
            setRetryCmd->maxRetries = atoi(argv[index++]);
            if (setRetryCmd->maxRetries > WMI_MAX_RETRIES) {
                printf("Invalid max retries! [0 - 13] \n");
                exit(-1);
            }
            if (!strcmp(argv[index], "on")) {
                setRetryCmd->enableNotify = 1;
            } else if (!strcmp(argv[index], "off")) {
                setRetryCmd->enableNotify = 0;
            } else {
                usage();
            }
            cmd = WMI_SET_RETRYLIMITS;
            break;
        case WMI_START_SCAN:
            cmd = WMI_START_SCAN;
            startScanCmd->homeDwellTime = 0;
            startScanCmd->forceScanInterval = 0;
            startScanCmd->numChannels = 0;
            break;
        case WMI_SET_FIX_RATES:
            cmd = WMI_SET_FIX_RATES;
            break;
        case WMI_GET_FIX_RATES:
            cmd = WMI_GET_FIX_RATES;
            break;
        case WMI_SET_AUTH_MODE:
            cmd = WMI_SET_AUTH_MODE;
            break;
        case WMI_SET_REASSOC_MODE:
            cmd = WMI_SET_REASSOC_MODE;
            break;
        case WMI_SET_LPREAMBLE:
            cmd = WMI_SET_LPREAMBLE;
            setLpreambleCmd->status = atoi(optarg);
            break;
        case WMI_SET_RTS:
            cmd = WMI_SET_RTS;
            setRtsCmd->threshold = atoi(optarg);
            break;
        case WMI_SET_WMM:
            cmd = WMI_SET_WMM;
            setWmmCmd->status = atoi(optarg);
            break;
        case WMI_SET_QOS_SUPP:
            cmd = WMI_SET_QOS_SUPP;
            qosSupp->status = atoi(optarg);
            break;
        case WMI_APSD_TIM_POLICY:
            if (!strcmp(optarg, "ignore")) {
                pPowerSave->apsdTimPolicy = IGNORE_TIM_ALL_QUEUES_APSD;
            } else {
                pPowerSave->apsdTimPolicy = PROCESS_TIM_ALL_QUEUES_APSD;
            }
            break;
        case WMI_SIMULATED_APSD_TIM_POLICY:
            if (!strcmp(optarg, "ignore")) {
                pPowerSave->simulatedAPSDTimPolicy = IGNORE_TIM_SIMULATED_APSD;
            } else {
                pPowerSave->simulatedAPSDTimPolicy = PROCESS_TIM_SIMULATED_APSD;
            }
            break;
        case WMI_SET_ERROR_DETECTION:
            cmd = WMI_SET_ERROR_DETECTION;
            break;
        case 'S':
            hbparam->threshold = atoi(optarg);
            break;
        case 'T':
            hbparam->frequency = atoi(optarg);
            break;
        case WMI_GET_HB_CHALLENGE_RESP:
            cmd = WMI_GET_HB_CHALLENGE_RESP;
            break;
        case 'U':
            *cookie = strtoul(optarg, (char **)NULL, 0);
            break;
        case USER_SETKEYS:
            if (argc == 5) {
                user_setkeys_info->keyOpCtrl = 0;
                cmd = USER_SETKEYS;
            } else {
              printf("Invalid arg %s:Usage --usersetkeys --initrsc=<on/off>",
                     optarg);
            }
            break;
        case WMI_GET_RD:
            cmd = WMI_GET_RD;
            break;
        case WMI_SET_TXOP:
            cmd = WMI_SET_TXOP;
            pTxOp->txopEnable = atoi(optarg);
            break;
        case DIAG_ADDR:
            *diagaddr = strtoul(optarg, (char **)NULL, 0);
            printf("addr: 0x%x\n", *diagaddr);
            break;
        case DIAG_DATA:
            *diagdata = strtoul(optarg, (char **)NULL, 0);
            printf("data: 0x%x\n", *diagdata);
            break;
        case DIAG_READ:
            cmd = DIAG_READ;
            break;
        case DIAG_WRITE:
            cmd = DIAG_WRITE;
            break;
        case WMI_SET_KEEPALIVE:
             cmd = WMI_SET_KEEPALIVE;
             break;
        case WMI_GET_KEEPALIVE:
             cmd = WMI_GET_KEEPALIVE;
             break;
        case WMI_SET_APPIE:
             cmd = WMI_SET_APPIE;

            if (argc - optind != 1) {
                printf("Usage is --setappie <beacon/probe/respon/assoc> <IE>\n");
                cmd = 0;
                break;
            }

            if (A_OK != wmic_validate_appie(appIEInfo, argv)) {
                cmd = 0;
                break;
            }
            break;
        case WMI_SET_MGMT_FRM_RX_FILTER:
             cmd = WMI_SET_MGMT_FRM_RX_FILTER;
            if (argc - optind != 1) {
                printf("Usage is --setmgmtfilter <set/clear> <frmtype> \n");
                cmd = 0;
                break;
            }

            if (A_OK != wmic_validate_mgmtfilter(pMgmtFilter, argv)) {
                cmd = 0;
                break;
            }
            break;
        case 'V':
            dbglogCfg->mmask = strtoul(optarg, (char **)NULL, 0);
            dbglogCfg->valid |= DBGLOG_MODULE_LOG_ENABLE_MASK;
            break;
        case 'W':
            dbglogCfg->rep = strtoul(optarg, (char **)NULL, 0);
            dbglogCfg->valid |= DBGLOG_REPORTING_ENABLED_MASK;
            break;
        case 'X':
            dbglogCfg->tsr = strtoul(optarg, (char **)NULL, 0);
            dbglogCfg->valid |= DBGLOG_TIMESTAMP_RESOLUTION_MASK;
            break;
        case 'Y':
            dbglogCfg->size = strtoul(optarg, (char **)NULL, 0);
            dbglogCfg->valid |= DBGLOG_REPORT_SIZE_MASK;
            break;
        case 'Z':
            clearstat = 1;
            break;
        case WMI_BSSID:
             convert_hexstring_bytearray(optarg, bssid, sizeof(bssid));
            break;
        case USER_SETKEYS_INITRSC:
            if (strcmp(optarg, "on") == 0) {
                user_setkeys_info->keyOpCtrl &=
                                    ~AR6000_USER_SETKEYS_RSC_UNCHANGED;
            } else if (strcmp(optarg, "off") == 0) {
                user_setkeys_info->keyOpCtrl |=
                                    AR6000_USER_SETKEYS_RSC_UNCHANGED;
            } else {
              printf("Invalid arg %s:Usage --usersetkeys --initrsc=<on/off>",
                     optarg);
            }
            break;
        case WMI_DBGLOG_CFG_MODULE:
            dbglogCfg->valid = 0;
            cmd = WMI_DBGLOG_CFG_MODULE;
            break;
        case WMI_DBGLOG_GET_DEBUG_LOGS:
            cmd = WMI_DBGLOG_GET_DEBUG_LOGS;
            break;
        case WMI_SET_HOST_SLEEP_MODE:
            cmd = WMI_SET_HOST_SLEEP_MODE;
            if (!strcmp(optarg, "asleep")) {
                hostSleepModeCmd->asleep = TRUE;
                hostSleepModeCmd->awake = FALSE;
            } else if (!strcmp(optarg, "awake")) {
                hostSleepModeCmd->asleep = FALSE;
                hostSleepModeCmd->awake = TRUE;
            }
            break;
        case WMI_GET_IP:
            cmd = WMI_GET_IP;
            if (strcmp(optarg, "none") == 0) {
               break;
            }
            pIP->ips[0] = inet_addr(optarg);
            if (!pIP->ips[0] || ((pIP->ips[0] & 0xf0) >= 0xe0)) {
                printf("Invalid IP\n");
                cmd = 0;
                break;
            }
            if (optind < argc) {
                pIP->ips[1] = inet_addr(argv[optind]);
                if (!pIP->ips[1] || ((pIP->ips[1] & 0xf0) >= 0xe0)) {
                    printf("Invalid IP\n");
                    cmd =0;
                    break;
                }
            }
            break;
        case WMI_SET_WOW_MODE:
            cmd = WMI_SET_WOW_MODE;
            if (!strcmp(optarg, "enable")) {
                wowModeCmd->enable_wow = TRUE;
            } else if (!strcmp(optarg, "disable")) {
                wowModeCmd->enable_wow = FALSE;
            }
            break;
        case WMI_SET_WOW_FILTER:
            if (!strcmp(optarg, "none")) {
               wowModeCmd->filter = 0;
            } else if (!strcmp(optarg, "ssid")) {
                wowModeCmd->filter |= (1<<WOW_FILTER_SSID);
            }
            break;
        case WMI_SET_WOW_HOST_REQ_DELAY:
             wowModeCmd->hostReqDelay=atoi(optarg);
             break;
        case WMI_ADD_WOW_PATTERN:
            cmd = WMI_ADD_WOW_PATTERN;
            index = (optind - 1);
            A_UINT8* filter_mask = NULL;
            A_UINT8 temp1[64]={0};
            A_UINT8 temp2[64]={0};

            if((index + 4) > argc) {
                printf("Incorrect number of add wow pattern parameters\n");
                cmd = 0;
                break;
            }
            memset((char*)addWowCmd, 0, sizeof(WMI_ADD_WOW_PATTERN_CMD));
            i = addWowCmd->filter_list_id = 0;
            addWowCmd->filter_list_id = atoi(argv[index++]);
            addWowCmd->filter_size = atoi(argv[index++]);
            addWowCmd->filter_offset = atoi(argv[index++]);
            printf("optind=%d, size=%d offset=%d id=%d\n", optind,
                    addWowCmd->filter_size, addWowCmd->filter_offset,
                    addWowCmd->filter_list_id);
            convert_hexstring_bytearray(argv[index], temp1,addWowCmd->filter_size );
            memcpy(&addWowCmd->filter[0], temp1, addWowCmd->filter_size);
            index++;
            filter_mask = (A_UINT8*)(addWowCmd->filter + addWowCmd->filter_size);
            convert_hexstring_bytearray(argv[index], temp2,addWowCmd->filter_size );
            memcpy(filter_mask, temp2, addWowCmd->filter_size);

            for (i=0; i< addWowCmd->filter_size; i++) {

                printf ("mask[%d]=%x pattern[%d]=%x temp=%x\n", i, filter_mask[i], i, addWowCmd->filter[i], temp1[i]);
            }
            break;
        case WMI_DEL_WOW_PATTERN:
            cmd = WMI_DEL_WOW_PATTERN;
            index = (optind - 1);
            if ((index  + 1) > argc) {
                printf("Incorrect number of del wow pattern parameters\n");
                cmd = 0;
                break;
            }
            delWowCmd->filter_list_id = 0;
            index++;
            delWowCmd->filter_id = atoi(argv[index]);
            break;
        case WMI_GET_WOW_LIST:
            cmd = WMI_GET_WOW_LIST;
            index = (optind - 1);
            if ((index  + 1) > argc) {
                printf("Incorrect number of get wow list parameters\n");
                cmd = 0;
                break;
            }
            getWowListCmd->filter_list_id = atoi(argv[index]);
            printf("Get wow filters in list %d\n", getWowListCmd->filter_list_id);
            break;
        case DIAG_DUMP_CHIP_MEM:
            cmd = DIAG_DUMP_CHIP_MEM;
            break;
        case DIAG_DUMP_CHIP_MEM_VENUS:
            cmd = DIAG_DUMP_CHIP_MEM_VENUS;
            break;
        case WMI_SET_CONNECT_CTRL_FLAGS:
            cmd = WMI_SET_CONNECT_CTRL_FLAGS;
            break;
        case DUMP_HTC_CREDITS:
            cmd = DUMP_HTC_CREDITS;
            break;
        case WMI_AKMP_MULTI_PMKID:
            if (strcmp(optarg, "on") == 0) {
                akmpCtrlCmd->akmpInfo |= WMI_AKMP_MULTI_PMKID_EN;
            } else if (strcmp(optarg, "off") == 0) {
                akmpCtrlCmd->akmpInfo &= ~WMI_AKMP_MULTI_PMKID_EN;
            } else {
              printf("Invalid arg %s:Usage --setakmctrl --multipmkid=<on/off>",
                     optarg);
            }
            break;
        case WMI_SET_AKMP_INFO:
            cmd = WMI_SET_AKMP_INFO;
            break;
        case WMI_NUM_PMKID:
            if ((pmkidUserInfo.numPMKIDUser = atoi(optarg))
                                                > WMI_MAX_PMKID_CACHE)
            {
                printf("Number of PMKIDs %d is out of range [1-%d]\n",
                       pmkidUserInfo.numPMKIDUser,
                       WMI_MAX_PMKID_CACHE);
                pmkidUserInfo.numPMKIDUser = 0;
            }
            break;
        case WMI_PMKID_ENTRY:
            if (pmkidUserInfo.pmkidInfo->numPMKID <
                    pmkidUserInfo.numPMKIDUser)
            {
                A_UINT8 nextEntry = pmkidUserInfo.pmkidInfo->numPMKID;

                convert_hexstring_bytearray(optarg,
                                            pmkidUserInfo.pmkidInfo->
                                            pmkidList[nextEntry].pmkid,
                                            WMI_PMKID_LEN);
                pmkidUserInfo.pmkidInfo->numPMKID++;
            }
            break;
        case WMI_SET_PMKID_LIST:
            cmd = WMI_SET_PMKID_LIST;
            pmkidUserInfo.pmkidInfo =
                                 (WMI_SET_PMKID_LIST_CMD *)(buf + sizeof(int));
            pmkidUserInfo.pmkidInfo->numPMKID = 0;
            pmkidUserInfo.numPMKIDUser = 0;
            break;
        case WMI_GET_PMKID_LIST:
            cmd = WMI_GET_PMKID_LIST;
            break;
        case WMI_SET_IEMASK:
            filterCmd->ieMask = strtoul(argv[optind-1], NULL, 0);
            break;
        case WMI_SET_BSS_PMKID_INFO:
            cmd = WMI_SET_BSS_PMKID_INFO;
            memset(bssid, 0, sizeof(bssid));
            pi_cmd->pi_enable = FALSE;
            break;
        case WMI_BSS_PMKID_ENTRY:
            convert_hexstring_bytearray(optarg, pi_cmd->pi_pmkid,
                                        sizeof(pi_cmd->pi_pmkid));
            memcpy(pi_cmd->pi_bssid, bssid, sizeof(bssid));
            pi_cmd->pi_enable = TRUE;
            break;
        case WMI_ABORT_SCAN:
             cmd = WMI_ABORT_SCAN;
             break;
        case WMI_TARGET_EVENT_REPORT:
             cmd = WMI_TARGET_EVENT_REPORT;
             evtCfgCmd->evtConfig = atoi(optarg);
             break;
        /* AP mode commands */
        case WMI_AP_GET_STA_LIST:
            cmd = WMI_AP_GET_STA_LIST;
            break;
        case WMI_AP_HIDDEN_SSID:
            cmd = WMI_AP_HIDDEN_SSID;
            pHidden->hidden_ssid = atoi(argv[optind]);
            break;
        case WMI_AP_SET_NUM_STA:
            cmd = WMI_AP_SET_NUM_STA;
            pNumSta->num_sta = atoi(argv[optind]);
            break;
        case WMI_AP_SET_GNUM_STA:
            cmd = WMI_AP_SET_GNUM_STA;
            pNumSta->num_sta = atoi(argv[optind]);
            pNumSta->num_sta |= 0x80;
            break;
        case WMI_AP_GET_NUM_STA:
            cmd = WMI_AP_GET_NUM_STA;
            pNumSta->num_sta = 0;
            break;
        case WMI_AP_GET_GNUM_STA:
            cmd = WMI_AP_GET_GNUM_STA;
            pNumSta->num_sta = 0x80;
            break;
        case WMI_AP_SET_DFS:
            cmd = WMI_AP_SET_DFS;
            setDfsCmd->enable = atoi(optarg);
            break;
        case WMI_AP_ACL_POLICY:
        {
            A_UINT8 policy, retain;
            cmd = WMI_AP_ACL_POLICY;
            index = optind;
            policy = atoi(argv[index++]);
            retain = atoi(argv[index++]);
            pACLpolicy->policy = policy |
                (retain?AP_ACL_RETAIN_LIST_MASK:0);
            break;
        }
        case WMI_AP_ACL_MAC_LIST1:
            cmd = WMI_AP_ACL_MAC_LIST1;
            pACL->action = ADD_MAC_ADDR;
            if(wmic_ether_aton_wild(argv[optind], pACL->mac, &pACL->wildcard) != A_OK) {
                printf("bad mac address\n");
                exit (0);
            }
            break;
        case WMI_AP_ACL_MAC_LIST2:
            cmd = WMI_AP_ACL_MAC_LIST2;
            pACL->action = DEL_MAC_ADDR;
            if( (strlen(argv[optind]) == 2) && ISDIGIT(argv[optind][0]) && ISDIGIT(argv[optind][1]) ) {
                pACL->index = atoi(argv[optind]);
            } else if( (strlen(argv[optind]) == 1) && ISDIGIT(argv[optind][0]) ) {
                pACL->index = atoi(argv[optind]);
            } else {
                printf("bad ACL index\n");
                exit(0);
            }
            break;
        case WMI_AP_GET_ACL_LIST:
            cmd = WMI_AP_GET_ACL_LIST;
            break;
        case WMI_AP_COMMIT_CONFIG:
            cmd = WMI_AP_COMMIT_CONFIG;
            break;
        case WMI_AP_INACT_TIME:
            cmd = WMI_AP_INACT_TIME;
            pInact->period = atoi(argv[optind]);
            break;
        case WMI_AP_PROT_TIME:
            cmd = WMI_AP_PROT_TIME;
            index = optind;
            pProt->period_min = atoi(argv[index++]);
            pProt->dwell_ms = atoi(argv[index++]);
            break;
        case WMI_AP_SET_MLME:
            cmd = WMI_AP_SET_MLME;
            index = optind;
            if((index + 3) > argc) {
                printf("Incorrect number of arguments\n");
                exit(0);
            }
            pMlme->im_op = atoi(argv[index++]);
            pMlme->im_reason = atoi(argv[index++]);
            if(wmic_ether_aton(argv[index++], pMlme->im_macaddr) != A_OK) {
                printf("bad mac address\n");
                exit (0);
            }
            break;
        case WMI_AP_SET_DTIM:
            cmd = WMI_AP_SET_DTIM;
            pDtim->dtim = atoi(argv[optind]);
            break;
        case WMI_AP_SET_COUNTRY:
            cmd = WMI_AP_SET_COUNTRY;
            A_BOOL match=FALSE;

            for(i = 0; i < sizeof(my_ctr)/sizeof(my_ctr[0]); i++) {
                if(!strcasecmp(optarg, my_ctr[i])) {
                    match = 1;
                    break;
                }
            }

            if (!match) {
                cmd = 0;
            } else {
                memcpy(pCountry->countryCode,my_ctr[i], 2);
                *(pCountry->countryCode + 2)=0x20;
            }

            break;
        case WMI_AP_GET_COUNTRY_LIST:
            cmd = WMI_AP_GET_COUNTRY_LIST;
            break;
        case WMI_AP_DISABLE_REGULATORY:
            cmd = WMI_AP_DISABLE_REGULATORY;
            break;
        case WMI_AP_INTRA_BSS:
            cmd = WMI_AP_INTRA_BSS;
            *intra = atoi(argv[optind]);
            *intra &= 0xF;
            break;
        case WMI_AP_INTER_BSS:
            cmd = WMI_AP_INTER_BSS;
            *intra = atoi(argv[optind]);
            *intra |= 0x80;
            break;
        case WMI_DUMP_RCV_AGGR_STATS:
            cmd = WMI_DUMP_RCV_AGGR_STATS;
            break;
        case WMI_SUSPEND_DRIVER:
            cmd = WMI_SUSPEND_DRIVER;
            break;
        case WMI_RESUME_DRIVER:
            cmd = WMI_RESUME_DRIVER;
            break;				
        case WMI_SETUP_AGGR:
        {
            A_UINT8 aid;
            cmd = WMI_SETUP_AGGR;
            if(argc-optind < 2) {
                printf("--setup_aggr <tid> <aid>\n");
                return 0;
            }
            pAddbaReq->tid = strtoul(argv[optind++], NULL, 0);
            if(argv[optind]) aid = strtoul(argv[optind], NULL, 0);
            pAddbaReq->tid = (pAddbaReq->tid & 0xF) | (aid << 4);
            break;
        }
        case WMI_CFG_ALLOW_AGGR:
            cmd = WMI_CFG_ALLOW_AGGR;
            pAllowAggr->tx_allow_aggr = strtoul(argv[argc-2], NULL, 0);
            pAllowAggr->rx_allow_aggr = strtoul(argv[argc-1], NULL, 0);
            break;
        case WMI_CFG_DELE_AGGR:
        {
            A_UINT8 aid;
            cmd = WMI_CFG_DELE_AGGR;
            if(argc-optind < 2) {
                printf("--dele_aggr <tid> <direction> <aid>\n");
                return 0;
            }
            pDeleteAggr->tid = strtoul(argv[optind++], NULL, 0);
            pDeleteAggr->is_sender_initiator = strtoul(argv[optind++], NULL, 0);
            if(argv[optind]) aid = strtoul(argv[optind], NULL, 0);
            pDeleteAggr->tid = (pDeleteAggr->tid & 0xF) | (aid << 4);
            break;
        }
        case WMI_SET_HT_CAP:
            cmd = WMI_SET_HT_CAP;
            break;
        case WMI_SET_HT_OP:
            cmd = WMI_SET_HT_OP;
            break;
        case WMI_AP_GET_STAT:
            cmd = WMI_AP_GET_STAT;
            break;
        case WMI_AP_CLR_STAT:
            cmd = WMI_AP_CLR_STAT;
            break;
        case WMI_SET_TX_SELECT_RATES:
            cmd = WMI_SET_TX_SELECT_RATES;
            break;
        case WMI_AP_GET_HIDDEN_SSID:
            cmd = WMI_AP_GET_HIDDEN_SSID;
            break;
        case WMI_AP_GET_COUNTRY:
            cmd = WMI_AP_GET_COUNTRY;
            break;
        case WMI_AP_GET_WMODE:
            cmd = WMI_AP_GET_WMODE;
            break;
        case WMI_AP_GET_DTIM:
            cmd = WMI_AP_GET_DTIM;
            break;
        case WMI_AP_GET_BINTVL:
            cmd = WMI_AP_GET_BINTVL;
            break;
        case WMI_GET_RTS:
            cmd = WMI_GET_RTS;
            break;
        case DIAG_FETCH_TARGET_REGS:
            cmd = DIAG_FETCH_TARGET_REGS;
            break;
#ifdef ATH_INCLUDE_PAL
        case WMI_SEND_PAL_CMD:
            cmd = WMI_SEND_PAL_CMD;
            break;
        case WMI_SEND_PAL_DATA:
            cmd = WMI_SEND_PAL_DATA;
            break;
#endif
        case WMI_SET_WLAN_CONN_PRECDNCE:
            cmd = WMI_SET_WLAN_CONN_PRECDNCE;
            prec->precedence = atoi(argv[argc-1]);
            break;
        case WMI_SET_AP_RATESET:
            cmd = WMI_SET_AP_RATESET;
            pAPrs->rateset = atoi(argv[optind]);
            break;
        case WMI_SET_TX_WAKEUP_POLICY:
            if (!strcmp(optarg, "sleep")) {
                pmParamCmd->tx_wakeup_policy = TX_DONT_WAKEUP_UPON_SLEEP;
            } else if (!strcmp(optarg, "wakeup")) {
                pmParamCmd->tx_wakeup_policy = TX_WAKEUP_UPON_SLEEP;
            } else {
                cmd = 0;
            }
            break;
        case WMI_SET_TX_NUM_FRAMES_TO_WAKEUP:
            pmParamCmd->num_tx_to_wakeup = atoi(optarg);
            break;
        case WMI_SET_AP_PS_PSTYPE:
            if (!strcmp(optarg, "disable")) {
                apPsCmd->psType = AP_PS_DISABLE;
            } else if (!strcmp(optarg, "atheros")) {
                apPsCmd->psType = AP_PS_ATH;
            } else {
                cmd = 0;
            }
            break;
        case WMI_SET_AP_PS_IDLE_TIME:
            apPsCmd->idle_time = atoi(optarg);
            break;
        case WMI_SET_AP_PS_PS_PERIOD:
            apPsCmd->ps_period = atoi(optarg);
            break;
        case WMI_SET_AP_PS_SLEEP_PERIOD:
            apPsCmd->sleep_period = atoi(optarg);
            break;
        case WMI_SEND_CONNECT_CMD:
            cmd = WMI_SEND_CONNECT_CMD;
            memset(&cp,0,sizeof(cp));
            if(strlen(optarg) > 32) {
                printf("Error: Wrong SSID\n");
            } else {
                cp.ssid_len = strlen(optarg);
                memcpy(cp.ssid, optarg, cp.ssid_len);
            }
            break;
        case WMI_SEND_CONNECT_CMD1:
#ifdef WPA_SUPPORT
        {
            unsigned long val;
            index = optind-1;

            if(argc-index != 4) {
                printf("Error: wpa needs 4 args but only %d given\n", argc-index);
                break;
            }

            val = strtol(argv[index++], NULL, 0);
            if(val == 1) {
                cp.wpa = IW_AUTH_WPA_VERSION_WPA;
            } else if(val == 2) {
                cp.wpa = IW_AUTH_WPA_VERSION_WPA2;
            } else {
                cp.wpa = 0;
                printf("Error: Wrong WPA version\n");
            }

            if (!strcasecmp(argv[index], "tkip")) {
                cp.ucipher = IW_AUTH_CIPHER_TKIP;
            } else if (!strcasecmp(argv[index], "ccmp")) {
                cp.ucipher = IW_AUTH_CIPHER_CCMP;
            } else {
                cp.ucipher = IW_AUTH_CIPHER_NONE;
                printf("Error: Wrong unicast cipher\n");
            }
            index++;

            if (!strcasecmp(argv[index], "tkip")) {
                cp.mcipher = IW_AUTH_CIPHER_TKIP;
            } else if (!strcasecmp(argv[index], "ccmp")) {
                cp.mcipher = IW_AUTH_CIPHER_CCMP;
            } else {
                cp.mcipher = IW_AUTH_CIPHER_NONE;
                printf("Error: Wrong multicast cipher\n");
            }
            index++;

            val = strlen(argv[index]);
            if(val >= 8 && val <= 63) {
                memcpy(cp.psk, argv[index], val);
                cp.psk_type = KEYTYPE_PHRASE;
            } else if (val == 64) {
                memcpy(cp.psk, argv[index], val);
                cp.psk_type = KEYTYPE_PSK;
            } else {
                printf("Error: Wrong PSK\n");
            }
            break;
        }
#else
            printf("Command not supported in wireless ext version: %d \n",WIRELESS_EXT);
            return -1;
#endif
       case WMI_SEND_CONNECT_CMD2:
            printf("Error: WEP not yet implemented\n");
            return 0;

       case WMI_SET_WPA_OFFLOAD_STATE:
       {
           index = optind-1;

           if(argc-index != 1) {
               printf("Error: setwpaoffload needs 1 arg but only %d given\n", argc-index);
               break;
           }

           *pWpaOffloadState = strtol(argv[index++], NULL, 0);

           cmd = WMI_SET_WPA_OFFLOAD_STATE;

           break;
       }
       case WMI_SET_EXCESS_TX_RETRY_THRES:
       {
           index = optind-1;

           if(argc-index != 1) {
               printf("Error: setexcesstxretrythres needs 1 arg but only %d given\n", argc-index);
               break;
           }

           *pExcessTxRetryThres = strtol(argv[index++], NULL, 0);

           cmd = WMI_SET_EXCESS_TX_RETRY_THRES;

           break;
       }
       case BT_HW_POWER_STATE:
            if (!strcmp(optarg, "on")) {
                cmd = SET_BT_HW_POWER_STATE;
                ((int *)buf)[0] = AR6000_XIOCTL_SET_BT_HW_POWER_STATE;
                ((int *)buf)[1] = 1;
            } else if (!strcmp(optarg, "off")) {
                cmd = SET_BT_HW_POWER_STATE;
                ((int *)buf)[0] = AR6000_XIOCTL_SET_BT_HW_POWER_STATE;
                ((int *)buf)[1] = 0;
            } else if (!strcmp(optarg, "query")) {
                cmd = GET_BT_HW_POWER_STATE;
                ((int *)buf)[0] = AR6000_XIOCTL_GET_BT_HW_POWER_STATE;
            } else {
                usage();
            }
            break;


       case WMI_SET_TX_SGI_PARAM:
             cmd = WMI_SET_TX_SGI_PARAM;
             set_txsgiparam->sgiMask[0] = DEFAULT_SGI_MASK_L32;
             set_txsgiparam->sgiMask[1] = DEFAULT_SGI_MASK_U32;
             set_txsgiparam->sgiPERThreshold = DEFAULT_SGI_PER;
             break;

       case WMI_SGI_MASK:
            {
                unsigned long long val;

                val = strtoll(optarg, NULL, 16);
                set_txsgiparam->sgiMask[0] = (unsigned long) val;
                set_txsgiparam->sgiMask[1] = (unsigned long) (val>>32);
            }
            break;
       

       case WMI_PER_SGI:
            {set_txsgiparam->sgiPERThreshold = atoi(optarg);
             break;
            }

       case WMI_WAC_ENABLE:
            cmd = WMI_WAC_ENABLE;
            break;

       case WMI_AP_ACS_DISABLE_HI_CHANNELS:
            cmd = WMI_AP_ACS_DISABLE_HI_CHANNELS;
            ((int *)buf)[1] = atoi(argv[optind]);
            break;

       case WMI_SET_DIVERSITY_PARAM:
            cmd = WMI_SET_DIVERSITY_PARAM;
            index = optind-1;
            
            pDiversity->divIdleTime = atoi(argv[index++]);
            pDiversity->antRssiThresh = atoi(argv[index++]);
            pDiversity->divEnable = atoi(argv[index++]);
            pDiversity->active_treshold_rate = atoi(argv[index++]);
            break;
       
       case WMI_SCAN_PROBED_SSID:
            cmd = WMI_SCAN_PROBED_SSID;
            ssid = (A_UCHAR *)optarg;
            break;

        case WMI_AP_SET_APSD:
            cmd = WMI_AP_SET_APSD;
            pApApsd->enable = atoi(argv[optind]);
            break;

        case WMI_GET_HT_CAP:
            cmd = WMI_GET_HT_CAP;
            break;

       default:
            usage();
            break;
        }
    }

    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, ifname, sizeof(iwr.ifr_name));

    switch (cmd) {
    case WMI_SET_BSS_FILTER:
        ifr.ifr_data = (void *)filterCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SETBSSFILTER, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_POWER_MODE:
        ifr.ifr_data = (void *)pwrCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SETPWR, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_PM_PARAMS:
        ifr.ifr_data = (void *)pmParamCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SET_PMPARAMS, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_IBSS_PM_CAPS:
        ifr.ifr_data = (void *)adhocPmCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SET_IBSS_PM_CAPS, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_AP_PS:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_AP_PS;
        ifr.ifr_data = (void *)buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_ERROR_DETECTION:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_HB_CHALLENGE_RESP_PARAMS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_HB_CHALLENGE_RESP:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_GET_HB_CHALLENGE_RESP;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
#ifdef USER_KEYS
    case USER_SETKEYS:
        {
            ((int *)buf)[0] = AR6000_XIOCTL_USER_SETKEYS;
            ifr.ifr_data = buf;

            if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
            {
                err(1, "%s", ifr.ifr_name);
            }

            break;
        }
#endif /* USER_KEYS */
    case WMI_SET_SCAN_PARAMS:
        if (sParamCmd->maxact_chdwell_time) {
           if (sParamCmd->maxact_chdwell_time < 5) {
              printf("Max active channel dwell time should be between 5-65535 msec\n");
              break;
           }
           if (sParamCmd->minact_chdwell_time &&
              (sParamCmd->maxact_chdwell_time < sParamCmd->minact_chdwell_time)) {
               printf("Max active channel dwell time should be greater than minimum\n");
               break;
           }
        }
        ifr.ifr_data = (void *)sParamCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SETSCAN, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_VERSION:
        {
        struct ar6000_version *revinfo;

        revinfo = malloc(sizeof(*revinfo));
        ifr.ifr_data = (void *)revinfo;
        if (ioctl(s, AR6000_IOCTL_WMI_GETREV, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        printf("Driver Rev = 0x%x(%u.%u.%u.%u), ROM Rev = 0x%x(%u.%u.%u.%u), "
               "Firmware Rev = 0x%x(%u.%u.%u.%u)\n",
              revinfo->host_ver,
              ((revinfo->host_ver)&0xf0000000)>>28,
              ((revinfo->host_ver)&0x0f000000)>>24,
              ((revinfo->host_ver)&0x00ff0000)>>16,
              ((revinfo->host_ver)&0x0000ffff),
              revinfo->target_ver,
              ((revinfo->target_ver)&0xf0000000)>>28,
              ((revinfo->target_ver)&0x0f000000)>>24,
              ((revinfo->target_ver)&0x00ff0000)>>16,
              ((revinfo->target_ver)&0x0000ffff),
              revinfo->wlan_ver,
              ((revinfo->wlan_ver)&0xf0000000)>>28,
              ((revinfo->wlan_ver)&0x0f000000)>>24,
              ((revinfo->wlan_ver)&0x00ff0000)>>16,
              ((revinfo->wlan_ver)&0x0000ffff)
              );
        }
        break;
    case WMI_SET_LISTEN_INTERVAL:
        ifr.ifr_data = (void *)listenCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SETLISTENINT, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_BMISS_TIME:
        ifr.ifr_data = (void *)bmissCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SET_BMISS_TIME, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_RSSI_THRESHOLDS:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_RSSITHRESHOLD;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_SNR_THRESHOLDS:
        ifr.ifr_data = (void *)snrThresholdParam;
        if (ioctl(s, AR6000_IOCTL_WMI_SET_SNRTHRESHOLD, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_CLR_RSSISNR:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_CLR_RSSISNR;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_LQ_THRESHOLDS:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_LQTHRESHOLD;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_CHANNEL:
        index = optind - 1;

        phyMode = getPhyMode(argv[index]);

        if(phyMode == -1)
        {
            printf("Incorrect Phy mode \n");
            break;
        }
        else
        {
            chParamCmd->phyMode = (WMI_PHY_MODE) phyMode;
            //printf("Phy mode %d \n",phyMode);
        }

        clist = chParamCmd->channelList;
        chindex = 0;
        index++;

        for (; index < argc; index++) {
            if (strcmp(argv[index],"sc") == 0) {
                chParamCmd->scanParam = atoi(argv[++index]);
                break;
            } else {
                channel = atoi(argv[index]);
                if (!channel) {
                    break;
                }
                if (channel < 255) {
                    /*
                     * assume channel is a ieee channel #
                     */
                clist[chindex] = wmic_ieee2freq(channel);
                } else {
                    clist[chindex] = channel;
                }
                chindex++;
            }
        }

        chParamCmd->numChannels = chindex;
        ifr.ifr_data = (void *)chParamCmd;

        if (ioctl(s, AR6000_IOCTL_WMI_SET_CHANNELPARAMS, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_SSID:
        if (index > MAX_PROBED_SSID_INDEX) {
            printf("num option for ssid command too large\n");
            err(1, "%s", ifr.ifr_name);
            break;
        }
        if (strlen((char *)ssid) > sizeof (ssidCmd->ssid)) {
            printf("ssid name too large\n");
            err(1, "%s", ifr.ifr_name);
            break;
        }
        ssidCmd->entryIndex = index;
        if (strcmp((char *)ssid, "off") == 0) {
            ssidCmd->ssidLength = 0;
            ssidCmd->flag = DISABLE_SSID_FLAG;
        } else if (strcmp((char *)ssid, "any") == 0) {
            ssidCmd->ssidLength = 0;
            ssidCmd->flag = ANY_SSID_FLAG;
        } else {
            ssidCmd->flag = SPECIFIC_SSID_FLAG;
            ssidCmd->ssidLength = strlen((char *)ssid);
            strcpy((char *)(ssidCmd->ssid), (char *)ssid);
        }
        ifr.ifr_data = (void *)ssidCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SET_PROBEDSSID, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_BADAP:
    case WMI_DELETE_BADAP:
        if (index > WMI_MAX_BAD_AP_INDEX) {
            printf("bad index\n");
            break;
        }
        badApCmd->badApIndex = index;
        ifr.ifr_data = (void *)badApCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SET_BADAP, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_CREATE_QOS:
        index = optind;
        crePStreamCmd->trafficDirection = atoi(argv[index]);
        index++;
        crePStreamCmd->trafficClass = atoi(argv[index]);
        index++;
        crePStreamCmd->trafficType = atoi(argv[index]);
        index++;
        crePStreamCmd->voicePSCapability = atoi(argv[index]);
        index++;
        crePStreamCmd->minServiceInt = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->maxServiceInt = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->inactivityInt = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->suspensionInt = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->serviceStartTime = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->tsid = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->nominalMSDU =  strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->maxMSDU =  strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->minDataRate = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->meanDataRate = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->peakDataRate = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->maxBurstSize = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->delayBound = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->minPhyRate = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->sba = strtoll(argv[index], (char **)NULL, 0);
        index++;
        crePStreamCmd->mediumTime = strtoll(argv[index],(char **)NULL, 0);
        crePStreamCmd->nominalPHY = 0;
        index++;

        if (argc == (index + 1)){
            crePStreamCmd->nominalPHY = strtoll(argv[index],(char **)NULL, 0);
        }

        if (crePStreamCmd->trafficClass > 3) {
            printf("bad traffic class (%d)\n", crePStreamCmd->trafficClass);
            printf("Traffic class should be 1(BK), 2(VI) or 3(VO)\n");
            break;
        } else if (crePStreamCmd->trafficDirection > BIDIR_TRAFFIC) {
            printf("bad traffic direction (%d)\n", crePStreamCmd->trafficDirection);
            printf("Traffic class should be 0(uplink), 1(dnlink) or 2(bi-dir)\n");
            break;
        }

        ifr.ifr_data = (void *)crePStreamCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_CREATE_QOS, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_TARGET_STATS:
        if (clearstat == 1) {
           tgtStatsCmd.clearStats = 1;
        } else {
           tgtStatsCmd.clearStats = 0;
        }

        ifr.ifr_data = (void *)&tgtStatsCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_GET_TARGET_STATS, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        printTargetStats(&(tgtStatsCmd.targetStats));
        break;
    case WMI_SET_TARGET_ERROR_REPORTING_BITMASK:
        ifr.ifr_data = (void *)pBitMask;
        if (ioctl(s, AR6000_IOCTL_WMI_SET_ERROR_REPORT_BITMASK, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_DELETE_QOS:
        index = optind;
        delPStreamCmd->tsid = atoi(argv[index]);
        ifr.ifr_data = (void *)delPStreamCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_DELETE_QOS, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_QOS_QUEUE:
        {
        int i;
        printf("getQosQueue: %d\n", getQosQueueCmd->trafficClass);

        if (getQosQueueCmd->trafficClass > 3) {
            printf("bad traffic class (%d)\n", getQosQueueCmd->trafficClass);
            printf("Traffic class should be 0(BE), 1(BK), 2(VI) or 3(VO)\n");
            break;
        }
        ifr.ifr_data = (void *)getQosQueueCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_GET_QOS_QUEUE, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }

        printf("Active TSIDs \n");
        for (i=0; i <=15; i++) {
            if ((getQosQueueCmd->activeTsids & (1<<i))) {
                printf("tsID: %d\n",i);
            }
        }
        }
        break;
    case WMI_SET_ASSOC_INFO_CMDID:
        ifr.ifr_data = (void *)ieInfo;
        if (ioctl(s, AR6000_IOCTL_WMI_SET_ASSOC_INFO, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_AC_PARAMS:
        if ((acParamsCmd->eCWmin > WMI_MAX_CW_ACPARAM) ||
            (acParamsCmd->eCWmax > WMI_MAX_CW_ACPARAM) ||
            (acParamsCmd->aifsn > WMI_MAX_AIFSN_ACPARAM) ||
            (acParamsCmd->ac >= WMM_NUM_AC) )
        {
            printf("incorrect value for access parameters\n");
            usage();
            break;
        }

        ifr.ifr_data = (void *)acParamsCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SET_ACCESS_PARAMS, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_DISC_TIMEOUT:
        ifr.ifr_data = (void *)discCmd;
        if (ioctl(s, AR6000_IOCTL_WMI_SET_DISC_TIMEOUT, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
     case WMI_SET_ADHOC_BSSID:
        ((int *)buf)[0] = AR6000_XIOCTL_SET_ADHOC_BSSID;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            printf("fail to set adhoc bssid \n");
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_BEACON_INT:
        ((int *)buf)[0] = AR6000_XIOCTL_SET_BEACON_INTVAL;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_VOICE_PKT_SIZE:
        ((int *)buf)[0] = AR6000_XIOCTL_SET_VOICE_PKT_SIZE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_MAX_SP:
        ((int *)buf)[0] = AR6000_XIOCTL_SET_MAX_SP;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_ROAM_TBL:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_GET_ROAM_TBL;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_ROAM_CTRL:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_ROAM_CTRL;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_POWERSAVE_TIMERS:
        ((int *)buf)[0] = AR6000_XIOCTRL_WMI_SET_POWERSAVE_TIMERS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_POWER_MODE:
        ((int *)buf)[0] = AR6000_XIOCTRL_WMI_GET_POWER_MODE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        printf("Power mode is %s\n",
               (getPowerMode->powerMode == MAX_PERF_POWER) ? "maxperf" : "rec");
        break;
     case WMI_SET_WLAN_STATE:
        ((int *)buf)[0] = AR6000_XIOCTRL_WMI_SET_WLAN_STATE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_WLAN_STATE:
        ((int *)buf)[0] = AR6000_XIOCTL_GET_WLAN_SLEEP_STATE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }    
        printf("WLAN state is %s\n", (ifr.ifr_ifru.ifru_ivalue == WLAN_ENABLED) ? "enabled" : "disabled");
        break;
    case WMI_GET_ROAM_DATA:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_GET_ROAM_DATA;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_BT_STATUS:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_BT_STATUS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_BT_PARAMS:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_BT_PARAMS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_BTCOEX_FE_ANT:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_BTCOEX_FE_ANT;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_BTCOEX_COLOCATED_BT_DEV:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_BTCOEX_COLOCATED_BT_DEV;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_BTCOEX_SCO_CONFIG:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_BTCOEX_SCO_CONFIG;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
    break;
    case WMI_SET_BTCOEX_A2DP_CONFIG:
       ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_BTCOEX_A2DP_CONFIG;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
    break;
    case WMI_SET_BTCOEX_ACLCOEX_CONFIG:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_BTCOEX_ACLCOEX_CONFIG;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
    break;
    case WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
    break;
    case WMI_SET_BTCOEX_DEBUG:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_BTCOEX_DEBUG;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        printf("issue ioctl AR6000_XIOCTL_WMI_SET_BTCOEX_DEBUG\n");
    break;

    case WMI_SET_BTCOEX_BT_OPERATING_STATUS:
       ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_BT_OPERATING_STATUS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        printf("issue ioctl AR6000_XIOCTL_WMI_SET_BT_OPERATING_STATUS\n");
    break;

    case WMI_GET_BTCOEX_CONFIG:
       ((int *)buf)[0] = AR6000_XIOCTL_WMI_GET_BTCOEX_CONFIG;
        pBtcoexConfig->configEvent = (A_UINT32 *)&btcoexConfigEvent;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        printBtcoexConfig(&btcoexConfigEvent);

        break;

    case WMI_GET_BTCOEX_STATS:
       ((int *)buf)[0] = AR6000_XIOCTL_WMI_GET_BTCOEX_STATS;
        pBtcoexStatsEvent->statsEvent = (A_UINT32 *)&btcoexStatsEvent;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        printBtcoexStats(&btcoexStatsEvent);
        break;

    case WMI_SET_RETRYLIMITS:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SETRETRYLIMITS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_START_SCAN:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_STARTSCAN;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_FIX_RATES:
        {
        unsigned long long value;
        index = optind;
        index--;
        setFixRatesCmd->fixRateMask[0] = 0;
        setFixRatesCmd->fixRateMask[1] = 0;
        printf("argc = %d\n",argc);
        /* FIXME: need to first submit a request to the device to
         * learn what rates are supported before we can correctly
         * conclude the validity of the user input */
        for(; index<argc; index++)
        {
            if (atoi(argv[index])<0 || atoi(argv[index])>= 64)
            {
                printf("incorrect value for rates parameters\n");
                usage();
                break;
            }
            value |= (unsigned long long)1<<(atoi(argv[index]));
            printf("wmiconfig:fixRateMask=%lld\n", value);
        }
        setFixRatesCmd->fixRateMask[0] = (unsigned long) value;
        setFixRatesCmd->fixRateMask[1] = (unsigned long) (value>>32);

        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SETFIXRATES;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        }
        break;
    case WMI_GET_FIX_RATES:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_GETFIXRATES;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        } else {
            int i;
            unsigned long long mask;
            mask = getFixRatesCmd->fixRateMask[0];
            mask |= (unsigned long long)getFixRatesCmd->fixRateMask[1]<<32;
            printf("Fix rate set index:");
            for (i = 0; i<64; i++) {
                if (mask&((unsigned long long)1<<i)) {
                    printf("%d ",i);
                }
            }
            printf("\n");
        }
        break;
    case WMI_SET_AUTH_MODE:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_AUTHMODE;
        ifr.ifr_data = buf;
        index = optind;
        index--;
        setAuthMode->mode = atoi(argv[index]);
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_REASSOC_MODE:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_REASSOCMODE;
        ifr.ifr_data = buf;
        index = optind;
        index--;
        setReassocMode->mode = atoi(argv[index]);
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_LPREAMBLE:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_LPREAMBLE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_RTS:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_RTS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_WMM:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_WMM;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_QOS_SUPP:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_QOS_SUPP;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_TXOP:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_TXOP;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case DIAG_READ:
        ((int *)buf)[0] = AR6000_XIOCTL_DIAG_READ;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        printf("diagdata: 0x%x\n", *diagdata);
        break;
    case DIAG_WRITE:
        ((int *)buf)[0] = AR6000_XIOCTL_DIAG_WRITE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_RD:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_GET_RD;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        else
        {
            if(REGCODE_IS_CC_BITSET(*rd))
            {
                COUNTRY_CODE_MAP *reg;
                void *endTable = (void *)allCountries + sizeof(allCountries);				
                for (reg = &allCountries[0]; (void *)reg < endTable; reg++)
                {
                    if (reg->countryCode == REGCODE_GET_CODE(*rd))
                    {
                        break;
                    }
                }            
                printf("CountryCode (%s) = ",reg->isoName);
            }
            else if(REGCODE_IS_WWR_BITSET(*rd))
                printf("WWR Roaming code = ");
            else
                printf("Regulatory Domain = ");

            printf("0x%x\n", REGCODE_GET_CODE(*rd));
        }
        break;
    case WMI_SET_MCAST_FILTER:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_MCAST_FILTER;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_DEL_MCAST_FILTER:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_DEL_MCAST_FILTER;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_MCAST_FILTER:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_MCAST_FILTER;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_KEEPALIVE:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_KEEPALIVE;
        ifr.ifr_data = buf;
        index = optind;
        index--;
        setKeepAlive->keepaliveInterval = atoi(argv[index]);
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_KEEPALIVE:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_GET_KEEPALIVE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        printf("Keepalive interval is %d secs and AP is %s\n",
               getKeepAlive->keepaliveInterval, (getKeepAlive->configured ?
               "configured" : "not configured"));
        break;
    case WMI_SET_APPIE:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_APPIE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_MGMT_FRM_RX_FILTER:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_MGMT_FRM_RX_FILTER;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_DBGLOG_CFG_MODULE:
        ((int *)buf)[0] = AR6000_XIOCTL_DBGLOG_CFG_MODULE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_DBGLOG_GET_DEBUG_LOGS:
        ((int *)buf)[0] = AR6000_XIOCTL_DBGLOG_GET_DEBUG_LOGS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_IP:
        ((int *)buf)[0] = AR6000_XIOCTL_SET_IP;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
     case WMI_SET_HOST_SLEEP_MODE:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_HOST_SLEEP_MODE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_WOW_MODE:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_WOW_MODE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_ADD_WOW_PATTERN:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_ADD_WOW_PATTERN;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_DEL_WOW_PATTERN:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_DEL_WOW_PATTERN;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_WOW_LIST:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_GET_WOW_LIST;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case DIAG_DUMP_CHIP_MEM:
        for(i = 0; i < 5; i++) {
            printf("Address range = 0x%x, 0x%x\n",
                    mercuryAdd[i][0], mercuryAdd[i][1]);
            for(*diagaddr = mercuryAdd[i][0];
                *diagaddr < mercuryAdd[i][1];
                *diagaddr += 4) {
                ((int *)buf)[0] = AR6000_XIOCTL_DIAG_READ;
                ifr.ifr_data = buf;
                if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
                    err(1, "%s", ifr.ifr_name);
                }
                printf("0x%04x:0x%04x\n", *diagaddr, *diagdata);
            }
        }
        break;
    case DIAG_DUMP_CHIP_MEM_VENUS:
        printf("Venus:Chip state\n");
        for(i = 0; i < 8; i++) {
            p = &venus_internal[i];
            printf("%s\n", p->info);
            printf("Address range = 0x%x, 0x%x\n",
                    p->addr_st, p->addr_end);
            for(*diagaddr = p->addr_st;
                *diagaddr < p->addr_end;
                *diagaddr += 4) {
                ((int *)buf)[0] = AR6000_XIOCTL_DIAG_READ;
                ifr.ifr_data = buf;
                if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
                    err(1, "%s", ifr.ifr_name);
                }
                printf("0x%04x:0x%04x\n", *diagaddr, *diagdata);
            }
            printf("\n");
        }
        break;
    case WMI_SET_CONNECT_CTRL_FLAGS:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_CONNECT_CTRL_FLAGS;
        ifr.ifr_data = buf;
        index = optind - 1;
        *connectCtrlFlags = strtoul(argv[index], NULL, 0);
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case DUMP_HTC_CREDITS:
        ((int *)buf)[0] = AR6000_XIOCTL_DUMP_HTC_CREDIT_STATE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_AKMP_INFO:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_AKMP_PARAMS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_PMKID_LIST:
        if (pmkidUserInfo.pmkidInfo->numPMKID) {
            ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_PMKID_LIST;
            ifr.ifr_data = buf;
            if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
                err(1, "%s", ifr.ifr_name);
            }
        } else {
            printf("No PMKIDs entered\n");
        }
        break;
    case WMI_GET_PMKID_LIST:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_GET_PMKID_LIST;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_BSS_PMKID_INFO:
        iwr.u.data.pointer = buf;
        iwr.u.data.length = sizeof(*pi_cmd);
        if (ioctl(s, IEEE80211_IOCTL_ADDPMKID, &iwr) < 0) {
            printf("ADDPMKID IOCTL Error\n");
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_ABORT_SCAN:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_ABORT_SCAN;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_TARGET_EVENT_REPORT:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_TARGET_EVENT_REPORT;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_GET_STA_LIST:
    {
        A_UINT8 flag=0;
        char *str;

        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_STA_LIST;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        pGetSta = (ap_get_sta_t *)buf;
        printf("\n________________ STA List ___________________________________\n");
        printf(" MAC                  AID   Auth   Keymgmt  Cipher  Wmode  \n");
        printf("_____________________________________________________________\n");
        for(i=0;i<AP_MAX_NUM_STA;i++) {
            if(!is_mac_null(pGetSta->sta[i].mac)) {
                printf(" %02X:%02X:%02X:%02X:%02X:%02X    %-5d ",
                pGetSta->sta[i].mac[0], pGetSta->sta[i].mac[1],
                pGetSta->sta[i].mac[2], pGetSta->sta[i].mac[3],
                pGetSta->sta[i].mac[4], pGetSta->sta[i].mac[5],
                pGetSta->sta[i].aid);

                switch (pGetSta->sta[i].auth) {
                    case OPEN_AUTH:
                        str = "OPEN";
                        break;
                    case SHARED_AUTH:
                        str = "SHARED";
                        break;
                    default:
                        str = "Unknown";
                        break;
                }
                printf("%-7s ", str);
                
                switch (pGetSta->sta[i].keymgmt) {
                    case WMI_WPA_PSK_AUTH:
                        str = "WPA-PSK";
                        break;
                    case WMI_WPA2_PSK_AUTH:
                        str = "WPA2-PSK";
                        break;
                    default:
                        str = "NONE";
                        break;
                }
                printf("%-7s ", str);

                switch (pGetSta->sta[i].ucipher) {
                    case AES_CRYPT:
                        str = "AES";
                        break;
                    case TKIP_CRYPT:
                        str = "TKIP";
                        break;
                    case WEP_CRYPT:
                        str = "WEP";
                        break;
#ifdef WAPI_ENABLE
                    case WAPI_CRYPT:
                        str = "WAPI";
                        break;
#endif
                    default:
                        str = "NONE";
                        break;
                }
                printf("%-7s ", str);

                switch (pGetSta->sta[i].wmode & 0xf) {
                    case MODE_11A:
                        str = "11A";
                        break;
                    case MODE_11G:
                        str = "11G";
                        break;
                    case MODE_11B:
                        str = "11B";
                        break;
                    case MODE_11GONLY:
                        str = "11Gonly";
                        break;
                    case MODE_11NA_HT20:
                    case MODE_11NA_HT40:
                        if(pGetSta->sta[i].wmode & 0x80) {
                            str = "11Nonly";
                        } else {
                            str = "11NA";
                        }
                        break;
                    case MODE_11NG_HT20:
                    case MODE_11NG_HT40:                    
                        if(pGetSta->sta[i].wmode & 0x80) {
                            str = "11Nonly";
                        } else {
                            str = "11NG";
                        }
                        break;
                    case MODE_UNKNOWN:
                    default:
                        str = "Unknown";
                        break;
                }
                printf("%-5s\n", str);

                flag = 1;
            }
        }
        if(!flag)
        printf("                    Empty             \n");
        printf("_____________________________________________________________\n");
        break;
    }
    case WMI_AP_HIDDEN_SSID:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_HIDDEN_SSID;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_SET_NUM_STA:
    case WMI_AP_SET_GNUM_STA:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_SET_NUM_STA;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;

    case WMI_AP_GET_NUM_STA:
    case WMI_AP_GET_GNUM_STA:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_NUM_STA;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        pNumSta = (WMI_AP_NUM_STA_CMD *)buf;
        if(pNumSta->num_sta == 0xFF)
            printf("NUM_STA=NA\n");
        else
            printf("NUM_STA=%d\n", pNumSta->num_sta);       
        break;

    case WMI_AP_SET_DFS:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_SET_DFS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_ACL_POLICY:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_SET_ACL_POLICY;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_ACL_MAC_LIST1:
    case WMI_AP_ACL_MAC_LIST2:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_SET_ACL_MAC;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_GET_ACL_LIST:
    {
        A_UINT8 flag=0;

        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_ACL_LIST;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        pGetAcl = (WMI_AP_ACL *)buf;
        printf("____________________________\n");
        printf(" ACL Policy - %d\n", pGetAcl->policy & ~AP_ACL_RETAIN_LIST_MASK);
        printf("____________________________\n");
        printf(" Index       MAC\n");
        for(i=0;i<AP_ACL_SIZE;i++) {
            if(!is_mac_null(pGetAcl->acl_mac[i]) || pGetAcl->wildcard[i]) {
                printf("  %d ",i);
                print_wild_mac(pGetAcl->acl_mac[i], pGetAcl->wildcard[i]);
                flag = 1;
            }
        }
        if(!flag)
        printf("       Empty          \n");
        printf("____________________________\n");
        break;
    }
    case WMI_AP_COMMIT_CONFIG:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_COMMIT_CONFIG;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_INACT_TIME:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_CONN_INACT_TIME;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_PROT_TIME:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_PROT_SCAN_TIME;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_SET_MLME:
        iwr.u.data.pointer = buf;
        iwr.u.data.length = sizeof(struct ieee80211req_mlme);
        if (ioctl(s, IEEE80211_IOCTL_SETMLME, &iwr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_SET_DTIM:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_SET_DTIM;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_GET_COUNTRY_LIST:
        for(i = 0; i < sizeof(my_ctr)/sizeof(my_ctr[0]); i++) {
            printf("%s\t",my_ctr[i]);
            if ((i+1)%5 == 0) {
                printf("\n");
            }
        }
        printf("\n");
        break;
    case WMI_AP_SET_COUNTRY:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_SET_COUNTRY;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_DISABLE_REGULATORY:
        /* Set the country code to invalid */
        memcpy(pCountry->countryCode,WMI_DISABLE_REGULATORY_CODE, 2);

        ((int *)buf)[0] = AR6000_XIOCTL_AP_SET_COUNTRY;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_INTRA_BSS:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_CTRL_BSS_COMM;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_INTER_BSS:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_CTRL_BSS_COMM;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_DUMP_RCV_AGGR_STATS:
        ((int *)buf)[0] = AR6000_XIOCTL_DUMP_RCV_AGGR_STATS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        log = (PACKET_LOG *)buf;
        cnt = 0x1000;
        for(i = log->last_idx; cnt; cnt--) {
            printf("%d %d %d %d\n", i, log->info[i].cur, log->info[i].st, log->info[i].end);
            i = (i + 1) & 0xFFF;
        }
        break;
    case WMI_SETUP_AGGR:
        ((int *)buf)[0] = AR6000_XIOCTL_SETUP_AGGR;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_CFG_ALLOW_AGGR:
        ((int *)buf)[0] = AR6000_XIOCTL_ALLOW_AGGR;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_CFG_DELE_AGGR:
        ((int *)buf)[0] = AR6000_XIOCTL_DELE_AGGR;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_HT_CAP:
        index = optind - 1;

        if(argc-index != 7) {
            printf("incorrect number of params\n");
            break;
        }
        
        if(strlen(argv[index]) == strlen("g") && 
            strcmp(argv[index], "g") == 0) {
            pHtCap->band = 0;
        }else if(strlen(argv[index]) == strlen("a") && 
            strcmp(argv[index], "a") == 0) {
            pHtCap->band = 1;
        }else{
            printf("bad band param. Use 'a' OR 'g' NOT %s\n", argv[index]);
            break;
        }
        
        pHtCap->enable = atoi(argv[++index]);
        if(pHtCap->enable > 2) {
            printf("bad enable value. Use '0', '1' OR '2' NOT %s\n", argv[index]);
            break;
        }
        
        pHtCap->chan_width_40M_supported = (atoi(argv[++index]) == 1);
        pHtCap->short_GI_20MHz = (atoi(argv[++index]) == 1);
        pHtCap->short_GI_40MHz = (atoi(argv[++index]) == 1);
        pHtCap->intolerance_40MHz = (atoi(argv[++index]) == 1);
        pHtCap->max_ampdu_len_exp = atoi(argv[++index]);

        if (pHtCap->max_ampdu_len_exp > 3) {
            printf("bad ampdu len exp (%d)\n", pHtCap->max_ampdu_len_exp);
            printf("AMPDU len exponent should take value from 0 - 3\n");
            break;
        }

        ((int *)buf)[0] = AR6000_XIOCTL_SET_HT_CAP;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SET_HT_OP:
        index = optind - 1;
        pHtOp->sta_chan_width = (atoi(argv[index]) == 1);

        ((int *)buf)[0] = AR6000_XIOCTL_SET_HT_OP;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_AP_GET_STAT:
    {
        A_UINT8 flag=0;

        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_STAT;
        apStat->action = AP_GET_STATS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        apStat = (WMI_AP_MODE_STAT *)buf;
        printf("_______________________________________________________\n");
        printf(" AID       bytes      pkts       error      discard  \n");
        printf("_______________________________________________________\n");
        for(i=0;i<AP_MAX_NUM_STA;i++) {
            if(apStat->sta[i].aid) {
                flag = 1;
                printf(" %-2d    TX  %-10d %-10d %-10d %-10d\n",
                    apStat->sta[i].aid,
                    apStat->sta[i].tx_bytes,
                    apStat->sta[i].tx_pkts,
                    apStat->sta[i].tx_error,
                    apStat->sta[i].tx_discard);
                printf("       RX  %-10d %-10d %-10d %-10d\n\n",
                    apStat->sta[i].rx_bytes,
                    apStat->sta[i].rx_pkts,
                    apStat->sta[i].rx_error,
                    apStat->sta[i].rx_discard);
            }
        }
        if(!flag)
        printf("                   Empty          \n");
        printf("_______________________________________________________\n\n");
        break;
    }
    case WMI_AP_CLR_STAT:
    {
        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_STAT;
        apStat->action = AP_CLEAR_STATS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    }
    case WMI_SET_TX_SELECT_RATES:
    {
        unsigned long long val;
        unsigned char count = 0;
        index = optind;
        index--;
        //pTxSelectRate->rateMasks = 0;
        printf("argc = %d\n",argc);

        for(; index<argc; index++)
        {
            val = strtol(argv[index], NULL, 0);

            if (val == 0)
            {
                printf("incorrect value for rates parameters\n");
                usage();
                break;
            }

            if(count >= 8) {
                break;
            }

            pTxSelectRate->rateMasks[count++] = val;
        }

        if(count != 8) {
            printf("incorrect parameter count (%d)\n", count);
            usage();
            break;
        }

        ((int *)buf)[0] = AR6000_XIOCTL_SET_TX_SELECT_RATES;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    }
    case WMI_AP_GET_HIDDEN_SSID:
    {
        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_HIDDEN_SSID;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        pHidden = (WMI_AP_HIDDEN_SSID_CMD *)buf;
        printf("Hidden SSID: %d : %s\n", pHidden->hidden_ssid,
                pHidden->hidden_ssid?"Enabled":"Disabled");
        break;
    }
    case WMI_AP_GET_COUNTRY:
    {
        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_COUNTRY;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        pCountry = (WMI_AP_SET_COUNTRY_CMD *)buf;
        if(pCountry->countryCode[0] == 'F' &&
            pCountry->countryCode[1] == 'F')
        {
            printf("Country Code: Disabled\n");
        } else {
            printf("Country Code: %s\n", pCountry->countryCode);
        }
        break;
    }
    case WMI_AP_GET_WMODE:
    {
        char *wmode[] = {" ","A","G","AG","B","Gonly"};
        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_WMODE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        printf("Wireless Mode: %s\n", wmode[(int)buf[0]]);
        break;
    }
    case WMI_AP_GET_DTIM:
    {
        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_DTIM;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        pDtim = (WMI_AP_SET_DTIM_CMD *)buf;
        printf("DTIM Period: %d\n", pDtim->dtim);
        break;
    }
    case WMI_AP_GET_BINTVL:
    {
        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_BINTVL;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        bconIntvl = (WMI_BEACON_INT_CMD *)buf;
        printf("Beacon Interval: %d ms\n", bconIntvl->beaconInterval);
        break;
    }
    case WMI_GET_RTS:
    {
        ((int *)buf)[0] = AR6000_XIOCTL_AP_GET_RTS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        setRtsCmd = (WMI_SET_RTS_CMD *)buf;
        printf("RTS threshold: %d\n", setRtsCmd->threshold);
        break;
    }
    case DIAG_FETCH_TARGET_REGS:
    {
        int regnum;
        A_UINT32 *targregs;

        ((int *)buf)[0] = AR6000_XIOCTL_FETCH_TARGET_REGS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        targregs = ((A_UINT32 *)buf);
        for (regnum=0; regnum<AR6003_FETCH_TARG_REGS_COUNT; regnum++) {
            printf("%s=0x%x ", targ_reg_name[regnum], targregs[regnum]);
        }
        printf("\n");
        break;
    }
#ifdef ATH_INCLUDE_PAL
    case WMI_SEND_PAL_CMD:
    {
        A_UINT8 pdu[MAX_BUFFER_SIZE];
        A_UINT8 offset = 0;
        void *pdev;
        A_UINT16  sz,ret;
        A_BOOL isDebugOn= 0;
        pdev = phy_attach(ifname);

        memset(pdu, 0, sizeof(pdu));
        if(!strncmp(argv[1],"-i",2))
        {
            offset += 2;
        }
        if(get_input_choice(argv[2 + offset],pdu,&sz) != 0)
        {
            printf("Invalid command! Failed\n");
            break;
        }
        if(argc > 3 + offset)
        {
            if(!memcmp(argv[3 + offset],"on",2))
            {
                isDebugOn = 1;
            }
        }
        eventLogLevel = isDebugOn;
        cmdParser(pdu,sz);
        ret = pal_send_hci_cmd(pdev, (char *)pdu, (short)sz);
        if(0 != ret) {
            printf("CMD IGNORED\n");
        }
    }
    break;
    case WMI_SEND_PAL_DATA:
    {
        A_UINT8 pdu[MAX_BUFFER_SIZE];
        A_UINT8 offset = 0;
        void *pdev;
        A_UINT16  sz,ret;
        A_BOOL isDebugOn= 0;
        pdev = phy_attach(ifname);
        if (!strncmp(argv[1],"-i",2))
        {
            offset += 2;
        }
        if(get_input_choice(argv[2 + offset],pdu,&sz) != 0)
        {
            printf("Invalid command! Failed\n");
            break;
        }
        if(argc > 3 + offset)
        {
            if(!memcmp(argv[3 + offset],"on",2))
            {
                isDebugOn = 1;
            }
        }
        eventLogLevel = isDebugOn;
        palData(pdu,sz);
        ret = pal_send_acl_data_pkt(pdev, (char *)pdu, (short)sz);
        if(0 != ret) {
            printf("DATA IGNORED\n");
        }
    }
    break;
    case WMI_SET_WLAN_CONN_PRECDNCE:
        ((int *)buf)[0] = AR6000_XIOCTL_WLAN_CONN_PRECEDENCE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
    break;
#endif
    case WMI_SET_AP_RATESET:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_SET_11BG_RATESET;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_SEND_CONNECT_CMD:
        {
           send_connect_cmd(s, ifname, &cp);
        }
        break;
     case SET_BT_HW_POWER_STATE:
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
     case GET_BT_HW_POWER_STATE:
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        printf("BT HW power state is %s\n", (ifr.ifr_ifru.ifru_ivalue) ? "on" : "off");
        break;

     case WMI_SET_TX_SGI_PARAM:
     {
     
          ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_TX_SGI_PARAM;
          ((int *)buf)[1] = set_txsgiparam->sgiMask[0];
          ((int *)buf)[2] = set_txsgiparam->sgiMask[1];
          ((int *)buf)[3] = set_txsgiparam->sgiPERThreshold;
          ifr.ifr_data = buf;
          if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
          {
            err(1, "%s", ifr.ifr_name);
          }
          break;
     }

    case WMI_WAC_ENABLE:
    {
        WMI_WAC_ENABLE_CMD *cmd = (WMI_WAC_ENABLE_CMD *)(buf + 4);
        cmd->enable = atoi(argv[argc - 4]);
        cmd->period = atoi(argv[argc -3]);
        cmd->threshold = atoi(argv[argc - 2]);
        cmd->rssi = atoi(argv[argc - 1]);
        cmd->enable |= 0x80;

        ((int *)buf)[0] = AR6000_XIOCTL_WMI_ENABLE_WAC_PARAM;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    }

    case WMI_SET_WPA_OFFLOAD_STATE:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_WPA_OFFLOAD_STATE;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    
    case WMI_AP_ACS_DISABLE_HI_CHANNELS:
        ((int *)buf)[0] = AR6000_XIOCTL_AP_ACS_DISABLE_HI_CHANNELS;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;

    case WMI_SET_EXCESS_TX_RETRY_THRES:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_EXCESS_TX_RETRY_THRES;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;


    case WMI_SET_DIVERSITY_PARAM:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_DIVERSITY_PARAM;
        ifr.ifr_data = (void *)buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;

    case WMI_FORCE_ASSERT:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_FORCE_ASSERT;
        ifr.ifr_data = (void *)buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0)
        {
            err(1, "%s", ifr.ifr_name);
        }
        break;

    case WMI_SUSPEND_DRIVER:
        ((int *)buf)[0] = AR6000_XIOCTL_SUSPEND_DRIVER;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_RESUME_DRIVER:
        ((int *)buf)[0] = AR6000_XIOCTL_RESUME_DRIVER;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;	
   
     case WMI_SCAN_PROBED_SSID:
        send_scan_probedssid_cmd(s, ifname, ssid);
        break;

     case WMI_AP_SET_APSD:
        ((int *)buf)[0] = AR6000_XIOCTL_WMI_AP_SET_APSD;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        break;
    case WMI_GET_HT_CAP:
    {
        index = optind;

        if(argc-index != 1) {
            printf("incorrect number of params\n");
            break;
        }
        
        if(strlen(argv[index]) == strlen("g") && 
            strcmp(argv[index], "g") == 0) {
            pHtCap->band = 0;
        }else if(strlen(argv[index]) == strlen("a") && 
            strcmp(argv[index], "a") == 0) {
            pHtCap->band = 1;
        }else{
            printf("bad band param. Use 'a' OR 'g' NOT %s\n", argv[index]);
            break;
        }
    
        ((int *)buf)[0] = AR6000_XIOCTL_GET_HT_CAP;
        ifr.ifr_data = buf;
        if (ioctl(s, AR6000_IOCTL_EXTENDED, &ifr) < 0) {
            err(1, "%s", ifr.ifr_name);
        }
        pHtCap = (WMI_SET_HT_CAP_CMD *)buf;
        printf("HT mode         : [%d] %s\n", pHtCap->enable, (pHtCap->enable?((pHtCap->enable==2)?"11n only":"Enabled"):"Disabled"));
        printf("40M support     : [%d] %s\n", pHtCap->chan_width_40M_supported, (pHtCap->chan_width_40M_supported?"Enabled":"Disabled"));
        printf("SGI 20M         : [%d] %s\n", pHtCap->short_GI_20MHz, (pHtCap->short_GI_20MHz?"Enabled":"Disabled"));
        printf("SGI 40M         : [%d] %s\n", pHtCap->short_GI_40MHz, (pHtCap->short_GI_40MHz?"Enabled":"Disabled"));
        printf("Intolerance 40M : [%d] %s\n", pHtCap->intolerance_40MHz, (pHtCap->intolerance_40MHz?"Enabled":"Disabled"));
        printf("AMPDU Len       : [%d] %s\n", pHtCap->max_ampdu_len_exp, (pHtCap->max_ampdu_len_exp?"Exponent":"Default"));
        break;
    }
    default:
        usage();
    }

    exit (0);
}

/*
 * converts ieee channel number to frequency
 */
static A_UINT16
wmic_ieee2freq(int chan)
{
    if (chan == 14) {
        return 2484;
    }
    if (chan < 14) {    /* 0-13 */
        return (2407 + (chan*5));
    }
    if (chan < 27) {    /* 15-26 */
        return (2512 + ((chan-15)*20));
    }
    return (5000 + (chan*5));
}


#ifdef NOT_YET
// Validate a hex character
static A_BOOL
_is_hex(char c)
{
    return (((c >= '0') && (c <= '9')) ||
            ((c >= 'A') && (c <= 'F')) ||
            ((c >= 'a') && (c <= 'f')));
}

// Validate alpha
static A_BOOL
isalpha(int c)
{
    return (((c >= 'a') && (c <= 'z')) ||
            ((c >= 'A') && (c <= 'Z')));
}

// Validate alphanum
static A_BOOL
isalnum(int c)
{
    return (isalpha(c) || isdigit(c));
}

#endif

// Convert a single hex nibble
static int
_from_hex(char c)
{
    int ret = 0;

    if ((c >= '0') && (c <= '9')) {
        ret = (c - '0');
    } else if ((c >= 'a') && (c <= 'f')) {
        ret = (c - 'a' + 0x0a);
    } else if ((c >= 'A') && (c <= 'F')) {
        ret = (c - 'A' + 0x0A);
    }
    return ret;
}

// Validate digit
static A_BOOL
isdigit(int c)
{
    return ((c >= '0') && (c <= '9'));
}

void
convert_hexstring_bytearray(char *hexStr, A_UINT8 *byteArray, A_UINT8 numBytes)
{
    A_UINT8 i;

    for (i = 0; i < numBytes; i++) {
        byteArray[i] = 16*_from_hex(hexStr[2*i + 0]) + _from_hex(hexStr[2*i +1]);
    }
}

/*------------------------------------------------------------------*/
/*
 * Input an Ethernet address and convert to binary.
 */
static A_STATUS
wmic_ether_aton(const char *orig, A_UINT8 *eth)
{
  const char *bufp;
  int i;

  i = 0;
  for(bufp = orig; *bufp != '\0'; ++bufp) {
    unsigned int val;
    unsigned char c = *bufp++;
    if (isdigit(c)) val = c - '0';
    else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
    else break;

    val <<= 4;
    c = *bufp++;
    if (isdigit(c)) val |= c - '0';
    else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
    else break;

    eth[i] = (unsigned char) (val & 0377);
    if(++i == ATH_MAC_LEN) {
        /* That's it.  Any trailing junk? */
        if (*bufp != '\0') {
#ifdef DEBUG
            fprintf(stderr, "iw_ether_aton(%s): trailing junk!\n", orig);
            return(A_EINVAL);
#endif
        }
        return(A_OK);
    }
    if (*bufp != ':')
        break;
  }

  return(A_EINVAL);
}

A_STATUS
wmic_ether_aton_wild(const char *orig, A_UINT8 *eth, A_UINT8 *wild)
{
  const char *bufp;
  unsigned char val, c;
  int i=0;

  *wild = 0;
  for(bufp = orig; *bufp != '\0'; ++bufp) {
    c = *bufp++;
    if (isdigit(c)) val = c - '0';
    else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
    else if (c == '*')  { val = 0; *wild |= 1<<i; goto next; }
    else break;

    val <<= 4;
    c = *bufp++;
    if (isdigit(c)) val |= c - '0';
    else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
    else break;

next:
    eth[i] = (unsigned char) (val & 0xFF);
    if(++i == ATH_MAC_LEN) {
        /* That's it.  Any trailing junk? */
        if (*bufp != '\0') {
        }
        return(A_OK);
    }
    if (*bufp != ':')
        break;
  }

  return(A_EINVAL);
}

A_STATUS
wmic_validate_roam_ctrl(WMI_SET_ROAM_CTRL_CMD *pRoamCtrl, A_UINT8 numArgs,
                        char **argv)
{
    A_STATUS status = A_OK;
    WMI_BSS_BIAS *pBssBias;
    A_INT32 bias;
    A_UINT8 i = 0;

    switch (pRoamCtrl->roamCtrlType) {
        case WMI_FORCE_ROAM:
            if (numArgs != 1) {
                fprintf(stderr, "BSSID to roam not given\n");
                status = A_EINVAL;
            } else if (wmic_ether_aton(argv[optind], pRoamCtrl->info.bssid)
                                       != A_OK)
            {
                fprintf(stderr,"BSSID %s not in correct format\n",
                         argv[optind]);
                status = A_EINVAL;
            }
            break;
        case WMI_SET_ROAM_MODE:
            if (numArgs != 1) {
                fprintf(stderr, "roam mode(default, bssbias, lock) not "
                        " given\n");
                status = A_EINVAL;
            } else {
                if (strcasecmp(argv[optind], "default") == 0) {
                    pRoamCtrl->info.roamMode = WMI_DEFAULT_ROAM_MODE;
                } else if (strcasecmp(argv[optind], "bssbias") == 0) {
                    pRoamCtrl->info.roamMode = WMI_HOST_BIAS_ROAM_MODE;
                } else if (strcasecmp(argv[optind], "lock") == 0) {
                    pRoamCtrl->info.roamMode = WMI_LOCK_BSS_MODE;
                } else {
                    fprintf(stderr, "roam mode(default, bssbias, lock) not "
                        " given\n");
                    status = A_EINVAL;
                }
            }
            break;
        case WMI_SET_HOST_BIAS:
            if ((numArgs & 0x01) || (numArgs > 25) ) {
                fprintf(stderr, "roam bias too many entries or bss bias"
                        "not input for every BSSID\n");
                status = A_EINVAL;
            } else {
                pRoamCtrl->info.bssBiasInfo.numBss = numArgs >> 1;
                pBssBias = pRoamCtrl->info.bssBiasInfo.bssBias;
                while (i < pRoamCtrl->info.bssBiasInfo.numBss) {
                    if (wmic_ether_aton(argv[optind + 2 * i],
                                        pBssBias[i].bssid)
                                        != A_OK)
                    {
                        fprintf(stderr,"BSSID %s not in correct format\n",
                                argv[optind + 2 * i]);
                        status = A_EINVAL;
                        pRoamCtrl->info.bssBiasInfo.numBss = 0;
                        break;
                    }
                    bias  = atoi(argv[optind + 2 * i + 1]);
                    if ((bias < -256) || (bias > 255)) {
                        fprintf(stderr,"bias value %d is  not in range\n",
                                bias);
                        status = A_EINVAL;
                        pRoamCtrl->info.bssBiasInfo.numBss = 0;
                        break;
                    }
                    pBssBias[i].bias = bias;
                    i++;
                }
            }
            break;
        case WMI_SET_LOWRSSI_SCAN_PARAMS:
            if (numArgs != 4) {
                fprintf(stderr, "not enough arguments\n");
                status = A_EINVAL;
            } else {
                pRoamCtrl->info.lrScanParams.lowrssi_scan_period = atoi(argv[optind]);
                if (atoi(argv[optind+1]) >= atoi(argv[optind+2])) {
                    pRoamCtrl->info.lrScanParams.lowrssi_scan_threshold = atoi(argv[optind+1]);
                    pRoamCtrl->info.lrScanParams.lowrssi_roam_threshold = atoi(argv[optind+2]);
                } else {
                    fprintf(stderr, "Scan threshold should be greater than \
                            equal to roam threshold\n");
                    status = A_EINVAL;
                }
                pRoamCtrl->info.lrScanParams.roam_rssi_floor = atoi(argv[optind+3]);
            }

            break;
        default:
            status = A_EINVAL;
            fprintf(stderr,"roamctrl type %d out if range should be between"
                       " %d and %d\n", pRoamCtrl->roamCtrlType,
                        WMI_MIN_ROAM_CTRL_TYPE, WMI_MAX_ROAM_CTRL_TYPE);
            break;
    }
    return status;
}

A_STATUS
wmic_validate_appie(struct ieee80211req_getset_appiebuf *appIEInfo, char **argv)
{
    A_STATUS status = A_OK;
    A_UINT8 index = optind - 1;
    A_UINT16 ieLen;

    if ((strlen(argv[index]) == strlen("probe")) &&
        (strcmp(argv[index], "probe") == 0))
    {
        appIEInfo->app_frmtype = IEEE80211_APPIE_FRAME_PROBE_REQ;
    } else if ((strlen(argv[index]) == strlen("assoc")) &&
               (strcmp(argv[index], "assoc") == 0))
    {
        appIEInfo->app_frmtype = IEEE80211_APPIE_FRAME_ASSOC_REQ;
    } else if((strlen(argv[index]) == strlen("beacon")) &&
        (strcmp(argv[index], "beacon") == 0)) {
        appIEInfo->app_frmtype = IEEE80211_APPIE_FRAME_BEACON;
    } else if((strlen(argv[index]) == strlen("respon")) &&
        (strcmp(argv[index], "respon") == 0)) {
        appIEInfo->app_frmtype = IEEE80211_APPIE_FRAME_PROBE_RESP;
    } else {
        printf("specify one of beacon/probe/respon/assoc\n");
        return A_EINVAL;
    }
    index++;

    ieLen = strlen(argv[index]);
    if ((ieLen == 1) && argv[index][0] == '0') {
        appIEInfo->app_buflen = 0;
    } else if ((ieLen > 4)  && (ieLen <= 2*IEEE80211_APPIE_FRAME_MAX_LEN) &&
               _from_hex(argv[index][2])*16 +
               _from_hex(argv[index][3]) + 2  == ieLen/2)
    {
        if ((argv[index][0] != 'd') && (argv[index][1] != 'd')) {
            status = A_EINVAL;
        } else {
            convert_hexstring_bytearray(argv[index], appIEInfo->app_buf, ieLen/2);
            appIEInfo->app_buflen = ieLen/2;
        }
    } else {
        status = A_EINVAL;
        printf("Invalid IE format should be of format dd04aabbccdd\n");
    }

    return status;
}

A_STATUS
wmic_validate_mgmtfilter(A_UINT32 *pMgmtFilter, char **argv)
{
    A_UINT8 index = optind - 1;
    A_BOOL  setFilter = FALSE;
    A_UINT32    filterType;

    if ((strlen(argv[index]) == strlen("set")) &&
        (strcmp(argv[index], "set") == 0))
    {
        setFilter = TRUE;
    } else if ((strlen(argv[index]) == strlen("clear")) &&
        (strcmp(argv[index], "clear") == 0))
    {
        setFilter = FALSE;
    } else {
        printf("specify one of set/clear\n");
        return A_EINVAL;
    }
    index++;
    if ((strlen(argv[index]) == strlen("beacon")) &&
        (strcmp(argv[index], "beacon") == 0))
    {
        filterType = IEEE80211_FILTER_TYPE_BEACON;
    } else if ((strlen(argv[index]) == strlen("proberesp")) &&
        (strcmp(argv[index], "proberesp") == 0))
    {
        filterType = IEEE80211_FILTER_TYPE_PROBE_RESP;
    } else {
        printf("specify one of beacon/proberesp\n");
        return A_EINVAL;
    }
    *pMgmtFilter = 0;

    if (setFilter) {
        *pMgmtFilter |= filterType;
    } else {
        *pMgmtFilter &= ~filterType;
    }

    return A_OK;
}

void
printTargetStats(TARGET_STATS *pStats)
{
    printf("Target stats\n");
    printf("------------\n");
    printf("tx_packets = %llu\n"
           "tx_bytes = %llu\n"
           "tx_unicast_pkts  = %llu\n"
           "tx_unicast_bytes = %llu\n"
           "tx_multicast_pkts = %llu\n"
           "tx_multicast_bytes = %llu\n"
           "tx_broadcast_pkts = %llu\n"
           "tx_broadcast_bytes = %llu\n"
           "tx_rts_success_cnt = %llu\n"
           "tx_packet_per_ac[%d] = %llu\n"
           "tx_packet_per_ac[%d] = %llu\n"
           "tx_packet_per_ac[%d] = %llu\n"
           "tx_packet_per_ac[%d] = %llu\n"
           "tx_errors = %llu\n"
           "tx_failed_cnt = %llu\n"
           "tx_retry_cnt = %llu\n"
           "tx_mult_retry_cnt = %llu\n"
           "tx_rts_fail_cnt = %llu\n"
           "tx_unicast_rate = %d Kbps\n"
           "rx_packets = %llu\n"
           "rx_bytes = %llu\n"
           "rx_unicast_pkts = %llu\n"
           "rx_unicast_bytes = %llu\n"
           "rx_multicast_pkts = %llu\n"
           "rx_multicast_bytes = %llu\n"
           "rx_broadcast_pkts = %llu\n"
           "rx_broadcast_bytes = %llu\n"
           "rx_fragment_pkt = %llu\n"
           "rx_errors = %llu\n"
           "rx_crcerr = %llu\n"
           "rx_key_cache_miss = %llu\n"
           "rx_decrypt_err = %llu\n"
           "rx_duplicate_frames = %llu\n"
           "rx_unicast_rate = %d Kbps\n"
           "tkip_local_mic_failure = %llu\n"
           "tkip_counter_measures_invoked = %llu\n"
           "tkip_replays = %llu\n"
           "tkip_format_errors = %llu\n"
           "ccmp_format_errors = %llu\n"
           "ccmp_replays = %llu\n"
           "power_save_failure_cnt = %llu\n"
           "noise_floor_calibation = %d\n"
           "cs_bmiss_cnt = %llu\n"
           "cs_lowRssi_cnt = %llu\n"
           "cs_connect_cnt = %llu\n"
           "cs_disconnect_cnt = %llu\n"
           "cs_aveBeacon_snr= %d\n"
           "cs_aveBeacon_rssi = %d\n"
           "cs_lastRoam_msec = %d\n"
           "cs_rssi = %d\n"
           "cs_snr = %d\n"
           "lqVal = %d\n"
           "wow_num_pkts_dropped = %d\n"
           "wow_num_host_pkt_wakeups = %d\n"
           "wow_num_host_event_wakeups = %d\n"
           "wow_num_events_discarded = %d\n"
           "arp_received = %d\n"
           "arp_matched = %d\n"
           "arp_replied = %d\n",
           pStats->tx_packets,
           pStats->tx_bytes,
           pStats->tx_unicast_pkts,
           pStats->tx_unicast_bytes,
           pStats->tx_multicast_pkts,
           pStats->tx_multicast_bytes,
           pStats->tx_broadcast_pkts,
           pStats->tx_broadcast_bytes,
           pStats->tx_rts_success_cnt,
           0, pStats->tx_packet_per_ac[0],
           1, pStats->tx_packet_per_ac[1],
           2, pStats->tx_packet_per_ac[2],
           3, pStats->tx_packet_per_ac[3],
           pStats->tx_errors,
           pStats->tx_failed_cnt,
           pStats->tx_retry_cnt,
           pStats->tx_mult_retry_cnt,
           pStats->tx_rts_fail_cnt,
           pStats->tx_unicast_rate,
           pStats->rx_packets,
           pStats->rx_bytes,
           pStats->rx_unicast_pkts,
           pStats->rx_unicast_bytes,
           pStats->rx_multicast_pkts,
           pStats->rx_multicast_bytes,
           pStats->rx_broadcast_pkts,
           pStats->rx_broadcast_bytes,
           pStats->rx_fragment_pkt,
           pStats->rx_errors,
           pStats->rx_crcerr,
           pStats->rx_key_cache_miss,
           pStats->rx_decrypt_err,
           pStats->rx_duplicate_frames,
           pStats->rx_unicast_rate,
           pStats->tkip_local_mic_failure,
           pStats->tkip_counter_measures_invoked,
           pStats->tkip_replays,
           pStats->tkip_format_errors,
           pStats->ccmp_format_errors,
           pStats->ccmp_replays,
           pStats->power_save_failure_cnt,
           pStats->noise_floor_calibation,
           pStats->cs_bmiss_cnt,
           pStats->cs_lowRssi_cnt,
           pStats->cs_connect_cnt,
           pStats->cs_disconnect_cnt,
           pStats->cs_aveBeacon_snr,
           pStats->cs_aveBeacon_rssi,
           pStats->cs_lastRoam_msec,
           pStats->cs_rssi,
           pStats->cs_snr,
           pStats->lq_val,
           pStats->wow_num_pkts_dropped,
           pStats->wow_num_host_pkt_wakeups,
           pStats->wow_num_host_event_wakeups,
           pStats->wow_num_events_discarded,
           pStats->arp_received,
           pStats->arp_matched,
           pStats->arp_replied
);

}

void
printBtcoexConfig(WMI_BTCOEX_CONFIG_EVENT *pConfig)
{
    switch(pConfig->btProfileType) {
    case WMI_BTCOEX_BT_PROFILE_SCO:
        {
            WMI_SET_BTCOEX_SCO_CONFIG_CMD *scoConfigCmd = &pConfig->info.scoConfigCmd;
            printf("BTCOEX SCO CONFIG\n");
            printf("GENERIC SCO CONFIG\n");
            printf("scoSlots =%d\n"
                    "scoIdleSlots =%d\n"
                    "scoFlags = %d\n"
                    "linkId = %d\n",
                    scoConfigCmd->scoConfig.scoSlots,
                    scoConfigCmd->scoConfig.scoIdleSlots,
                    scoConfigCmd->scoConfig.scoFlags,
                    scoConfigCmd->scoConfig.linkId
                   );
            printf("PSPOLL SCO CONFIG \n");
            printf( "scoCyclesForceTrigger = %d\n"
                    "scoDataResponseTimeout = %d\n"
                    "scoStompDutyCyleVal = %d\n"
                    "scoStompDutyCyleMaxVal = %d\n"
                    "scoPsPollLatencyFraction = %d\n",
                    scoConfigCmd->scoPspollConfig.scoCyclesForceTrigger,
                    scoConfigCmd->scoPspollConfig.scoDataResponseTimeout,
                    scoConfigCmd->scoPspollConfig.scoStompDutyCyleVal,
                    scoConfigCmd->scoPspollConfig.scoStompDutyCyleMaxVal,
                    scoConfigCmd->scoPspollConfig.scoPsPollLatencyFraction
                    );
            printf("SCO optmode Config\n");
            printf( "scoStompCntIn100ms = %d\n"
                    "scoContStompMax = %d\n"
                    "scoMinlowRateMbps = %d\n"
                    "scoLowRateCnt = %d\n"
                    "scoHighPktRatio = %d\n"
                    "scoMaxAggrSize = %d\n",
                    scoConfigCmd->scoOptModeConfig.scoStompCntIn100ms,
                    scoConfigCmd->scoOptModeConfig.scoContStompMax,
                    scoConfigCmd->scoOptModeConfig.scoMinlowRateMbps,
                    scoConfigCmd->scoOptModeConfig.scoLowRateCnt,
                    scoConfigCmd->scoOptModeConfig.scoHighPktRatio,
                    scoConfigCmd->scoOptModeConfig.scoMaxAggrSize
                 );
            printf("SCO wlan scan config\n");
            printf("scanInterval = %d\n"
                   "maxScanStompCnt = %d\n",
                   scoConfigCmd->scoWlanScanConfig.scanInterval,
                   scoConfigCmd->scoWlanScanConfig.maxScanStompCnt
                  );

         }
         break;
    }
}

void
printBtcoexStats(WMI_BTCOEX_STATS_EVENT *pStats)
{
    printf("btcoex Stats\n");
}

static A_INT8 getPhyMode(char *pArg)
{
    typedef struct {
                        char * pOption;
                        WMI_PHY_MODE phyMode;
                    } PHY_MODE_MAP;

    //note : add options in lower case only
    PHY_MODE_MAP phyModeMap [] =
                    {
                     {"ag",WMI_11AG_MODE},
                     {"a",WMI_11A_MODE},
                     {"b",WMI_11B_MODE},
                     {"g",WMI_11G_MODE},
                     {"gonly",WMI_11GONLY_MODE},                     
                     {NULL}
                    };
    int i, j;
    const char *c;

   for (i = 0 ;phyModeMap[i].pOption != NULL ;i++)
   {
        c = phyModeMap[i].pOption ;

        for(j = 0; pArg[j] != '\0';j++)
        {
            if(c[j] != TO_LOWER(pArg[j]))
            {
                break;
            }

            if((c[j+1] == '\0') && (pArg[j+1] == '\0'))
            {
                return phyModeMap[i].phyMode;
            }
        }
   }
   return -1;
}

static int is_mac_null(A_UINT8 *mac)
{
    if(mac[0]==0 && mac[1]==0 && mac[2]==0 &&
       mac[3]==0 && mac[4]==0 && mac[5]==0) {
        return 1;
    }
    return 0;
}

void print_wild_mac(unsigned char *mac, char wildcard)
{
    int i;

    printf("    ");
    for(i=0;i<5;i++) {
        if(wildcard & (1<<i))   printf("*:");
        else                    printf("%02X:", mac[i]);
    }

    if(wildcard & (1<<i))   printf("*\n");
    else                    printf("%02X\n", mac[5]);
}


int wmiconfig_set_auth_param(int sock, char *ifname,
                   int idx, A_UINT32 value)
{
#ifdef WPA_SUPPORT
    struct iwreq iwr;
    int ret = 0;

    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, ifname, IFNAMSIZ);
    iwr.u.param.flags = idx & IW_AUTH_INDEX;
    iwr.u.param.value = value;

    if (ioctl(sock, SIOCSIWAUTH, &iwr) < 0) {
        printf("WPA: SIOCSIWAUTH(param %d value 0x%x) failed)",
                   idx, value);
        ret = -1;
    }

    return ret;
#else
    return -1;
#endif
}

int wmiconfig_set_pmk(int sock, char *ifname, A_UINT8 *pmk)
{
#ifdef WPA_SUPPORT
    struct iwreq iwr;
    int ret = 0;
    struct iw_encode_ext *ext;

    ext = malloc(sizeof(*ext) + WMI_PMK_LEN);
    if (ext == NULL)
        return -1;

    memset(ext, 0, sizeof(*ext) + WMI_PMK_LEN);
    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, ifname, IFNAMSIZ);
    iwr.u.encoding.pointer = (caddr_t) ext;
    iwr.u.encoding.length = sizeof(*ext) + WMI_PMK_LEN;

    memcpy(ext + 1, pmk, WMI_PMK_LEN);
    ext->key_len = WMI_PMK_LEN;
    ext->alg = IW_ENCODE_ALG_PMK;

    if (ioctl(sock, SIOCSIWENCODEEXT, &iwr) < 0) {
        ret = -1;
        perror("ioctl[SIOCSIWENCODEEXT]");
    }

    free(ext);

    return ret;
#else
    return -1;
#endif
}

int wmiconfig_set_passphrase(int sock, char *ifname, A_CHAR *passphrase,
                             A_UINT8 passphrase_len, A_CHAR *ssid, A_UINT8 ssid_len)
{
#ifdef WPA_SUPPORT
    struct ifreq ifr;
    int ret = 0;
    char *buf;
    WMI_SET_PASSPHRASE_CMD* cmd;

    buf = malloc(sizeof(WMI_SET_PASSPHRASE_CMD) + sizeof(int));
    if (buf == NULL)
        return -1;

    cmd = (WMI_SET_PASSPHRASE_CMD*)(buf + sizeof(int));

    memset(cmd, 0, sizeof(cmd));
    cmd->passphrase_len = passphrase_len;
    memcpy(cmd->passphrase, passphrase, passphrase_len);
    cmd->ssid_len = ssid_len;
    memcpy(cmd->ssid, ssid, ssid_len);

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

    ((int *)buf)[0] = AR6000_XIOCTL_WMI_SET_PASSPHRASE;
    ifr.ifr_data = buf;
    if (ioctl(sock, AR6000_IOCTL_EXTENDED, &ifr) < 0)
    {
        ret = -1;
        perror("ioctl[AR6000_IOCTL_EXTENDED]");
    }

    free(buf);

    return ret;
#else
    return -1;
#endif
}

#ifdef WPA_SUPPORT
static int hex2num(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
#endif

static int send_connect_cmd(int sock, char *ifname, profile_t *cp)
{
    struct iwreq iwr;
    int ret = 0;
    char buf[WMI_MAX_SSID_LEN+1];


    if(cp->wpa) {
#ifdef WPA_SUPPORT
        int a, b;

        ret = wmiconfig_set_auth_param(sock, ifname,
                       IW_AUTH_WPA_VERSION, cp->wpa);
        ret = wmiconfig_set_auth_param(sock, ifname,
                       IW_AUTH_CIPHER_PAIRWISE, cp->ucipher);
        ret = wmiconfig_set_auth_param(sock, ifname,
                       IW_AUTH_CIPHER_GROUP, cp->mcipher);
        ret = wmiconfig_set_auth_param(sock, ifname,
                       IW_AUTH_KEY_MGMT, IW_AUTH_KEY_MGMT_PSK);

        if(cp->psk_type == KEYTYPE_PHRASE) {
            ret = wmiconfig_set_passphrase(sock, ifname, cp->psk, 
                                           strlen(cp->psk), cp->ssid,
                                           cp->ssid_len);
        } else if(cp->psk_type == KEYTYPE_PSK) {
            for(ret=0;ret<32;ret++) {
                a = hex2num(cp->psk[ret*2]);
                b = hex2num(cp->psk[ret*2+1]);
                if(a<0 || b<0) printf("WRONG PSK\n");
                cp->pmk[ret] = ((a << 4) | b);
            }

            ret = wmiconfig_set_pmk(sock, ifname, cp->pmk);
        }
#else
        ret = -1;
#endif
    }

    memset(&iwr, 0, sizeof(iwr));
    strncpy(iwr.ifr_name, ifname, IFNAMSIZ);

    iwr.u.essid.flags = 1;
    memset(buf, 0, sizeof(buf));
    memcpy(buf, cp->ssid, cp->ssid_len);
    iwr.u.essid.pointer = (caddr_t) buf;
    iwr.u.essid.length = cp->ssid_len;

    if (ioctl(sock, SIOCSIWESSID, &iwr) < 0) {
        perror("ioctl[SIOCSIWESSID]");
        ret = -1;
    }

    return ret;
}

static int send_scan_probedssid_cmd(int sock, char *ifname, char *ssid)
{
    struct iwreq iwr;
    struct iw_scan_req scanreq;
    int ret = 0;
    memset(&iwr, 0, sizeof(iwr));
    memset(&scanreq, 0, sizeof(scanreq));
    strncpy(iwr.ifr_name, ifname, IFNAMSIZ);

    if (strlen(ssid) > WMI_MAX_SSID_LEN ) {
            printf("SSID name too large\n");
            return ret;
        }else if ((strcmp((char *)ssid, "off") == 0) || ((strcmp((char *)ssid, "any") == 0))) {
              iwr.u.essid.pointer = &scanreq;
              iwr.u.essid.length = sizeof(struct iw_scan_req); 
        } else {
              iwr.u.essid.flags = IW_SCAN_THIS_ESSID;
              iwr.u.essid.pointer = &scanreq;
              memcpy(scanreq.essid, ssid, strlen(ssid));
              iwr.u.essid.length = sizeof(struct iw_scan_req);
              scanreq.essid_len = strlen(ssid);
        }
    if (ioctl(sock, SIOCSIWSCAN, &iwr) < 0) {
        perror("ioctl[SIOCSIWSCAN]");
        ret = -1;
    }
    return ret;
}
