#ifndef __mib_defs_h__
#define __mib_defs_h__

 /* SMT Constants */
#define MIB_dot11StationID                         "1.1.1.1"
#define MIB_dot11MediumOccupancyLimit              "1.1.1.2"
#define MIB_dot11CFPollable                        "1.1.1.3"
#define MIB_dot11CFPeriod                          "1.1.1.4"
#define MIB_dot11CPFMaxDuration                    "1.1.1.5"
#define MIB_dot11AuthenticationResponseTimeout     "1.1.1.6"
#define MIB_dot11PrivacyOptionImplemented          "1.1.1.7"
#define MIB_dot11PowerManagementMode               "1.1.1.8"
#define MIB_dot11DesiredSSID                       "1.1.1.9"
#define MIB_dot11DesiredBSSType                   "1.1.1.10"
#define MIB_dot11OperationalRatesSet              "1.1.1.11"
#define MIB_dot11BeaconPeriod                     "1.1.1.12"
#define MIB_dot11DTIMPeriod                       "1.1.1.13"
#define MIB_dot11AssociationResponsTimeout        "1.1.1.14"
#define MIB_dot11DisassociateReason               "1.1.1.15"
#define MIB_dot11DisassociateStation              "1.1.1.16"
#define MIB_dot11DeauthenticateReason             "1.1.1.17"
#define MIB_dot11DeauthenticateStation            "1.1.1.18"
#define MIB_dot11AuthenticateFailStatus           "1.1.1.19"
#define MIB_dot11AuthenticateFailStation          "1.1.1.20"
#define MIB_dot11MultiDomainCapabilityImplemented "1.1.1.21"
#define MIB_dot11MultiDomainCapabilityEnabled     "1.1.1.22"
#define MIB_dot11CountryString                    "1.1.1.23"
/* TODO: define all SMT constants */

/* char idx 6 (the 0) is the key index. */
#define MIB_dot11WEPDefaultKeyValue                "1.3.1.0.2"
#define MIB_dot11WEPDefaultKeyValue_idxidx         6

#define MIB_dot11PrivacyInvoked                    "1.5.1.1"
#define MIB_dot11DefaultKeyID                      "1.5.1.2"
#define MIB_dot11ExcludeUnencrypted                "1.5.1.4"
#define MIB_dot11WEPICVErrorCount                  "1.5.1.5"
#define MIB_dot11RSNAEnable                        "1.5.1.7"

/* MAC Constants */
#define MIB_dot11MACAddress              "2.1.1.1" 
#define MIB_dot11RTSThreshold            "2.1.1.2"
#define MIB_dot11ShortRetryLimit         "2.1.1.3"
#define MIB_dot11LongRetryLimit          "2.1.1.4"
#define MIB_dot11FragmentationThreshold  "2.1.1.5"
#define MIB_dot11MaxTransmitMSDULifetime "2.1.1.6"
#define MIB_dot11MaxReceiveLifetime      "2.1.1.7"
#define MIB_dot11ManufacturerID          "2.1.1.8"
#define MIB_dot11ProductID               "2.1.1.9"

#define MIB_dot11CountersTable			"2.2.1"
#define MIB_dot11TransmittedFragmentCount       "2.2.1.1"
#define MIB_dot11MulticastTransmittedFrameCount "2.2.1.2"
#define MIB_dot11FailedCount                    "2.2.1.3"
#define MIB_dot11RetryCount                     "2.2.1.4"
#define MIB_dot11MultipleRetryCount             "2.2.1.5"
#define MIB_dot11FrameDuplicateCount            "2.2.1.6"
#define MIB_dot11RTSSuccessCount                "2.2.1.7"
#define MIB_dot11RTSFailureCount                "2.2.1.8"
#define MIB_dot11AckFailureCount                "2.2.1.9"
#define MIB_dot11ReceivedFragmentCount          "2.2.1.10"
#define MIB_dot11MulticastReceviedFrameCount    "2.2.1.11"
#define MIB_dot11FCSErrorCount                  "2.2.1.12"
#define MIB_dot11TransmittedFrameCount          "2.2.1.13"
#define MIB_dot11WEPUndecryptableCount          "2.2.1.14"

#define MIB_dot11Address            "2.3.1.2"

#define MIB_dot11manufacturerProductVersion     "3.1.2.1.4"

 /* SMT Local Constants */
#define MIB_dot11agcOpMode           "5.1"
#define MIB_dot11LinkMonitoring      "5.2.1"
#define MIB_dot11adaptiveTxRateLvl1  "5.2.2"
#define MIB_dot11rssi                "5.2.3"
#define MIB_dot11adaptiveTxRateMask  "5.2.5"
#define MIB_dot11rssiDataFrame       "5.2.6"
#define MIB_dot11snrBeacon           "5.2.7"  /* [dB] */
#define MIB_dot11snrData             "5.2.8"  /* [dB] */
#define MIB_dot11beaconLossRate      "5.2.9"
#define MIB_dot11packetErrorRate     "5.2.10"
#define MIB_dot11fixedRate           "5.2.11"
#define MIB_dot11noiseFloor          "5.2.15"

#define MIB_dot11InterferenceMode    "5.2.15.1" 
#define MIB_dot11interfererenceErrorRate        "5.2.15.3"

#define MIB_dot11LinkMonitoringBeaconCount      "5.2.19.1"
#define MIB_dot11LinkMonitoringBeaconTimeout    "5.2.19.2"
#define MIB_dot11LinkMonitoringTxFailureCount   "5.2.19.3"
#define MIB_dot11LinkMonitoringRoundtripCount   "5.2.19.4"
#define MIB_dot11LinkMonitoringRoundtripSilent  "5.2.19.5"
#define MIB_dot11LinkMonitoringBeaconCountWarning    "5.2.19.6"
#define MIB_dot11LinkMonitoringTxFailureCountWarning "5.2.19.8"

#define MIB_dot11WEPICVCorrectCount             "5.2.21"

#define MIB_dot11delaySpreadThresholdRatio   "5.2.22.1"
#define MIB_dot11delaySpreadMinPacketLimit   "5.2.22.2"
#define MIB_dot11delaySpreadThresholdSnr5db  "5.2.22.3"
#define MIB_dot11delaySpreadThresholdSnr10db "5.2.22.4"
#define MIB_dot11delaySpreadThresholdSnr20db "5.2.22.5"
#define MIB_dot11LinksupervisionTimeout      "5.2.26"


#define MIB_dot11PSTrafficTimeout    "5.3.1"
#define MIB_dot11PSWMMPeriod         "5.3.2"
#define MIB_dot11PSPollPeriod        "5.3.3"
#define MIB_dot11ListenInterval      "5.3.4"
#define MIB_dot11ReceiveAllDTIM      "5.3.5"
#define MIB_dot11UsePSPoll           "5.3.6"
#define MIB_dot11powerIndex          "5.4"
#define MIB_dot11xcoCrystalFreqIndex "5.7.1"
#define MIB_dot11enableExternalLFC   "5.7.2"
#define MIB_dot11backgroundScanEnabled          "5.11.1"
#define MIB_dot11backgroundScanPeriod           "5.11.2"
#define MIB_dot11backgroundScanProbeDelay       "5.11.3"
#define MIB_dot11backgroundScanMinChannelTime   "5.11.4"
#define MIB_dot11backgroundScanMaxChannelTime   "5.11.5"
#define MIB_dot11backgroundScanChannelList      "5.11.6"
#define MIB_dot11backgroundScanUseAlternateSSID "5.11.7"
#define MIB_dot11backgroundScanAlternateSSID    "5.11.8"
#define MIB_dot11btCoexEnabled                  "5.16.1"
#define MIB_dot11btCoexConfig                   "5.16.2"
#define MIB_dot11maxTxWindow                    "5.17" /* uint8_t with window size in packets */
#define MIB_dot11diversityAntennaMask           "5.18.1"
#define MIB_dot11diversityRssiThreshold         "5.18.2"
#define MIB_dot11listenEveryBeacon              "5.19"
#define MIB_dot11trafficFilterList              "5.22.1"
#define MIB_dot11trafficFilterDiscard           "5.22.2"
#define MIB_dot11trafficFilterCounter           "5.22.3"
#define MIB_dot11DHCPBroadcastFilter            "5.22.4"
#define MIB_dot11firmwareCapabilites            "5.23"
#define MIB_dot11ARPFilterMode                  "5.24.1"
#define MIB_dot11myIPAddress                    "5.24.2"
#define MIB_dot11gpioActiveMode                 "5.26.1"
#define MIB_dot11gpioPSMode                     "5.26.2"
#define MIB_dot11pinShutdownMode                "5.26.3"
#define MIB_dot11dtimBeaconSkipping             "5.27"
#define MIB_dot11compatibilityMask              "5.30"

#define MIB_dot11Aggregation                    "5.25.1"
#define MIB_dot11AggregationFilter              "5.25.2"

#define MIB_dot11SupportedRateTable             "5.35"

#endif /* __mib_defs_h__ */
