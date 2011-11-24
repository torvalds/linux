#include "driverenv.h"
#include "registry.h"

#undef E
#ifdef __GNUC__ /* really C99 */
#define E(X) .X =
#else
#define E(X)
#endif

rRegistry registry;

const rRegistry DefaultRegistry = {
   E(version) 0xb0863e6d,
   E(general) {
      E(macAddress) { { 0xff,0xff,0xff,0xff,0xff,0xff } },
   },
   E(network) {
      E(basic) {
         E(enableCoredump) 1,
         E(restartTargetAfterCoredump) 1,
         E(timeout) 100,
         E(connectionPolicy) {
            E(defaultBssType) Infrastructure_BSS,
            E(joinTimeout) 0,
            E(probeDelay) 1,
            E(probesPerChannel) 2,
            E(passiveScanTimeouts) {
               E(minChannelTime) 20,
               E(maxChannelTime) 160,
            },
            E(activeScanTimeouts) {
               E(minChannelTime) 20,
               E(maxChannelTime) 40,
            },
            E(connectedScanTimeouts) {
               E(minChannelTime) 6,
               E(maxChannelTime) 10,
            },
            E(connectedScanPeriod) 5000,
            E(max_connectedScanPeriod) 5000,
#ifdef USE_NEW_AGE
            E(scanResultLifetime) 6,
#else
            E(scanResultLifetime) 2000,
#endif
            E(disconnectedScanInterval) 5000,
            E(max_disconnectedScanInterval) 16000,
            E(periodicScanRepetition) 2,
         },
         E(supportedRateSet) { 12, { 0x02,0x04,0x0b,0x0c,0x12,0x16,0x18,0x24,0x30,0x48,0x60,0x6c } },  
         E(ht_capabilities) 0 
           | M80211_HT_CAPABILITY_HT_GREENFIELD
           | M80211_HT_CAPABILITY_RX_STBC_ONE_STREAM
           | M80211_HT_CAPABILITY_SHORT_GI_20MHZ 
//         | M80211_HT_CAPABILITY_LSIG_TXOP_PROTECT_SUPPORT
         ,
         E(ht_rates) 0xff, /* MCS rate mask */
         E(multiDomainCapabilityEnabled) FALSE,
         E(multiDomainCapabilityEnforced) FALSE,
         E(regionalChannels) { 
            13, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 }, 0
         },
         E(maxPower) 17,
         E(desiredSSID) { { M80211_IE_ID_NOT_USED, 0 }, { 0 } },
         E(desiredBSSID) { { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
         E(txRatePowerControl) 3, /* level 1 and 2 */
         E(enableWMM) STA_WMM_Enabled,
         E(enableWMMPs) FALSE,
         E(wmmPsPeriod) 0,
         E(QoSInfoElements) {
            E(ac_vo) TRUE,
            E(ac_vi) TRUE,
            E(ac_bk) TRUE,
            E(ac_be) TRUE,
         },
         E(qosMaxServicePeriodLength) 0,
         E(linkSupervision) {
            E(enable) TRUE,
            E(beaconFailCount) 80,
            E(beaconWarningCount) 5,
            E(beaconTimeout) 0,
            E(TxFailureCount) 40,
            E(roundtripCount) 0,
            E(roundtripSilent) 0xffffffff,
         },
         E(activeScanMode) TRUE,
         E(defaultScanJobDisposition) FALSE,
         E(scanNotificationPolicy) SCAN_NOTIFICATION_FLAG_JOB_COMPLETE
         | SCAN_NOTIFICATION_FLAG_BG_PERIOD_COMPLETE,
         E(scanPolicy) {
            E(ssid) { { M80211_IE_ID_NOT_USED, 0 }, { 0 } },
            E(bssid) { { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
            E(bssType) Infrastructure_BSS,
         },
         E(DHCPBroadcastFilter) 0
            | UDP_BROADCAST_FILTER_FLAG_BOOTP
            | UDP_BROADCAST_FILTER_FLAG_SSDP
            ,
         E(enableBTCoex) FALSE,
         E(activityTimeout) 0,
         E(cmdTimeout) 5000,
      },
      E(ibssBeacon) {
         E(dtim_period) 0,
         E(beacon_period) 100,
         E(tx_channel) { { M80211_IE_ID_DS_PAR_SET, 1 }, 1 },
         E(atim_set) { { M80211_IE_ID_IBSS_PAR_SET, 2 }, 0 },
         E(supportedRateSet) { 12, { 0x82,0x84,0x8b,0x8c,0x12,0x96,0x18,0x24,0x30,0x48,0x60,0x6c } },
      },
   },
   E(powerManagement) {
      E(mode) PowerSave_Enabled_Activated_From_Start,
      E(enablePsPoll) FALSE,
      E(receiveAll_DTIM) TRUE,
      E(listenInterval) 5,
      E(psTrafficTimeout) 160,
      E(psTxTrafficTimeout) 5,
      E(psDelayStartOfPs) 3000,
   },
   E(hostDriver) {
      E(delayAfterReset) 0,
      E(automaticFWLoad) TRUE,
      E(enable20MHzSdioClock) FALSE,
      E(txPktWinSize) 3,
      E(hmgAutoMode) TRUE,
   }
};
