#ifndef REGISTRY_H
#define REGISTRY_H

#include "sysdef.h"           /* Must be first include */
#include "mac_api.h"
#include "wei_list.h"

/************************************************
** Macro definitions.
************************************************/
#define GET_REGISTRY_OBJECT(ObjectType, ObjectInstance) \
   (ObjectType*)Registry_GetProperty(ID_#ObjectInstance)  

/************************************************
** Definition of generic registry base types
************************************************/
typedef char*                       rDynStr;          /* Dynamic length string type */
typedef int                         rBool;            /* Boolean */

/************************************************
** Definition of instance specific registry types
************************************************/
typedef uint32_t                          rVersionId;             /**< 32 bit hash of registry content used as unique version id */
typedef uint8_t                           rBit8;                  /**< Redef of uint8_t */
typedef rDynStr                           rNetworkId;             /**< Textual network identification */
typedef int                               rTimeout;               /**< Timeout value (usec) */
typedef int                               rInfo;                  /**< QoS info */
typedef m80211_ie_ssid_t                  rSSID;                  /**< SSID string */
typedef m80211_mac_addr_t                 rBSSID;                 /**< BSSID MAC adress */
typedef channel_list_t                    rChannelList;           /**< Channel List */
typedef m80211_ie_supported_rates_t       rSupportedRates;        /**< Supported BSS rates. */
typedef m80211_ie_ext_supported_rates_t   rExtSupportedRates;     /**< Extended Supported BSS rates. */
typedef uint16_t                          rInterval;              /**< Recurring timeout value (usec). */

typedef m80211_ie_ibss_par_set_t          rATIMSet;               /**< IE IBSS parameter set. */
typedef m80211_ie_ds_par_set_t            rChannelSet;            /**< IE DS parameter set */

/************************************************
** Definition of registry specific enumerations
************************************************/
#define MAX_NETWORKS 32       /* Max number of networks */

/* This reflects the driver power save modes */
typedef enum                                          
{
   PowerSave_Disabled_Permanently ,           /**< Power save is permanently off */
   PowerSave_Enabled_Activated_From_Start,    /**< Power management is enabled */
   PowerSave_Enabled_Deactivated_From_Start   /**< Power management is enabled but deactivated when driver is started */
} rPowerSaveMode;

typedef enum                                          /**< BSS Type */
{
   Infrastructure_BSS = M80211_CAPABILITY_ESS,        /**< Infrastructre BSS */
   Independent_BSS    = M80211_CAPABILITY_IBSS        /**< Independent BSS */
} rBSS_Type;


typedef enum                  /* WMM Support */
{
   STA_WMM_Disabled,         /**< No WMM support */
   STA_WMM_Enabled,          /**< WMM Enabled */
   STA_WMM_Auto              /**< WMM Enabled with AP supporting WMM*/
} rSTA_WMMSupport;


/************************************************
** Definition of property objects.
************************************************/


typedef struct {
      unsigned int len;
      uint8_t rates[M80211_IE_MAX_LENGTH_SUPPORTED_RATES+M80211_IE_MAX_LENGTH_EXT_SUPPORTED_RATES];
} rRateList;

typedef struct                               /* Connection timeouts. */
{
   rTimeout             minChannelTime;      /**< Minimum channel time. */
   rTimeout             maxChannelTime;      /**< Maximun channel time. */
} rScanTimeouts;

typedef struct
{
      rSSID              ssid;
      rBSSID             bssid;
      rBSS_Type          bssType;
      
} rScanPolicy;

typedef struct                               /* Policy for how to connect to the network. */
{
   rInfo             ac_vo;                  /**< Access category voice. */
   rInfo             ac_vi;                  /**< Access category video. */
   rInfo             ac_bk;                  /**< Access category background. */
   rInfo             ac_be;                  /**< Access category best effort. */
} rQoSInfoElements;

typedef struct {
   rBool             enable;                 /**< Link Supervision (0 OFF, 1 ON) */
   unsigned int      beaconFailCount;        /**< Mimimum missed beacons before disconnect. */
   unsigned int      beaconWarningCount;     /**< Mimimum missed beacons before warning. */
   unsigned int      beaconTimeout;          /**< Mimimum time with no received beacons before disconnect. */
   unsigned int      TxFailureCount;         /**< Number of consecutive failed attempts to transmit a frame. */
   unsigned int      roundtripCount;         /**< Maximum transmitted (roundtrip) messages without any reply. */
   unsigned int      roundtripSilent;        /**< Number of silent intervals before roundtrip messages are injected. */
} rLinkSupervision;

typedef struct                                     /* Policy for how to connect to the network. */
{
   rBSS_Type        defaultBssType;                /**< BSS type (infrastructure or IBSS). */
   rTimeout         joinTimeout;                   /**< Timeout for join requests. */
   rTimeout         probeDelay;                    /**< Delay before a probe request is sent. */
   int              probesPerChannel;              /**< Number of probe requests sent on each channel */
   rScanTimeouts    passiveScanTimeouts;           /**< Setting of passive scan timeouts. */
   rScanTimeouts    activeScanTimeouts;            /**< Setting of active scan  timeouts. */
   rScanTimeouts    connectedScanTimeouts;         /**< Setting of scan timeouts in connected mode */
   rTimeout         connectedScanPeriod;           /**< Periodtime for background scan. */
   rTimeout         scanResultLifetime;            /**< Life time (in msec) of scan results. 0 disabled scan expiry */
   rTimeout         disconnectedScanInterval;      /**< Time between periodic scans. 0 disables periodic scan */
   rTimeout         max_disconnectedScanInterval;  /**< Max Time between periodic scans. */
   rTimeout         max_connectedScanPeriod;       /**< Max Time between background periodic scans.  */
   int              periodicScanRepetition;        /**< Number of repetitions for current scan period, default 1  */
} rConnectionPolicy;

typedef struct                               /* GeneralProperties. */ 
{
   rBSSID            macAddress;             /**< MAC address of the device. */
} rGeneralWiFiProperties;

typedef struct                                     /* Basic Properties. */
{
   int               enableCoredump;               /**< Get coredump from target if heartbeat message is not received by host  */
   int               restartTargetAfterCoredump;   /**< Reset and restart target if heartbeat message is not recevied by host */
   rTimeout          timeout;                      /**< Connection timeout for each step (scan, join, auth, assoc). */
   rConnectionPolicy connectionPolicy;             /**< Connection policy settings (priority between VoIP and cellular when both are present) */
   rRateList         supportedRateSet;             /**< Set of supported rates. */
   unsigned int      ht_capabilities;              /**< Supported HT capabilities */
   unsigned int      ht_rates;                     /**< Supported HT rates (MCS style) */
   rBool             multiDomainCapabilityEnabled; /**< Support regulatory domain */
   rBool             multiDomainCapabilityEnforced;/**< Enforce regulatory domain */
   rChannelList      regionalChannels;             /**< List of channels regulated for use  in this geographical area. */
   int               maxPower;                     /**< The user specified max power for the region */
   rSSID             desiredSSID;                  /**< The SSID set by the user */
   rBSSID            desiredBSSID;                 /**< The BSSID set by the user */
   int               txRatePowerControl;           /**< Level 1 adaptive Tx rate (0 Disabled, 1 Enabled) */
   rSTA_WMMSupport   enableWMM;                    /**< Join with WMM mode (0 Disabled, 1 Enabled, Auto) */
   rBool             enableWMMPs;                  /**< Join with WMM mode (0 Disabled, 1 Enabled, Auto) */
   int               wmmPsPeriod;                  /**< Period in us in which data transfer is triggered  */
   rQoSInfoElements  QoSInfoElements;              /**< Defines if legacy or WMM power save shall be used */
   int               qosMaxServicePeriodLength;    /**< QoS Max SP length */
   rLinkSupervision  linkSupervision;              /**< Defines link supervision parameters */
   int               activeScanMode;               /**< Scan mode (0 passive, 1 active) */
   rBool             defaultScanJobDisposition;    /**< Initial disposition (0 stopped, 1 started) of the default scan job */
   int               scanNotificationPolicy;       /**< Scan notification policy. 1 is FIRST HIT, 2 is JOB COMPLETE, 4 is ALL_JOBS_COMPLETE and 8 is COMPLETE WITH HIT. */
   rScanPolicy       scanPolicy;                   /**< Scan policy */
   int               DHCPBroadcastFilter;          /**< Name should be changed!!! UDP broadcast filter bitmask (1 DHCP, 2 SSDP) */
   rBool             enableBTCoex;                 /**< Enable bluetooth coexistance */
   rInterval         activityTimeout;              /**< Driver activity timeout. No traffic in this period -> driver inactive */
   rInterval         cmdTimeout;                   /**< Driver command timeout. Defines when the fw is considered hung. */
} rBasicWiFiProperties;

typedef struct
{
   rInterval         dtim_period;            /**< dtim in beacon */
   rInterval         beacon_period;          /**< beacon period in TU */
   rChannelSet       tx_channel;             /**< default tx channel for beacon */
   rATIMSet          atim_set;               /**< IE IBSS parameter set. */
   rRateList         supportedRateSet;       /**< Set of supported rates. */
} rIBSSBeaconProperties;

typedef struct                               /* Network properties. */
{
   rBasicWiFiProperties       basic;            /**< Basic Properties. */
   rIBSSBeaconProperties      ibssBeacon;       /**< Beacon properties for IBSS */
} rNetworkProperties;

typedef struct                               /**< Power management properties for headset and phone . */
{
   rPowerSaveMode    mode;                   /**< PM mode: active or power save. */
   rBool             enablePsPoll;           /**< Enable legacy ps poll mode */   
   rBool             receiveAll_DTIM;        /**< Receive all DTIM. */
   rInterval         listenInterval;         /**< Beacon listen interval. */
   rTimeout          psTrafficTimeout;       /**< Traffic timeout after which device should go into power save used by firmware*/
   rTimeout          psTxTrafficTimeout;     /**< Traffic timeout after which device should go into power save used by driver*/
   rTimeout          psDelayStartOfPs;       /**< Delay in ms that will postpone start of ps related to associate success */
} rPowerManagementProperties;

typedef struct                               /* Host Driver Properties. */
{
   rTimeout          delayAfterReset;        /**< Delay in ms after reset of HW. */
   rBool             automaticFWLoad;         /**< Enables boot using J-Tag when needed. */
   rBool             enable20MHzSdioClock;   /**< Enable 20MHz SDIO clock (default is 16MHz) */
   int               txPktWinSize;           /**< The maximum size of the TX packet window */
   rBool             hmgAutoMode;            /**< Enable hmg auto connect mode */
} rHostDriverProperties;

/* Definition of persistent content of the registry. */
typedef struct GENERATE_WRAPPER_FUNCTIONS(REGISTRY)
{
   rVersionId                    version;          /**< MUST BE FIRST! */
   rGeneralWiFiProperties        general;          /**< General WiFi properties. */
   rNetworkProperties            network;          /**< Network properties. */
   rPowerManagementProperties    powerManagement;  /**< Power management properties. */
   rHostDriverProperties         hostDriver;       /**< Host Driver Properties. */ 
} rRegistry;

extern const rRegistry DefaultRegistry;


/******************************************************************************
** Function Prototypes
******************************************************************************/

#endif /* REGISTRY_H */

/******************End of File***************************************************************************/
