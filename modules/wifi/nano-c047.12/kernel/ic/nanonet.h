/* $Id: nanonet.h 17582 2011-01-19 14:54:03Z niks $ */


/*!
 * @defgroup transport_layer Transport Layer API
 *
 * This file declares the transport layer API. It is the glue layer to provide
 * WiFiEngine functionality to the SDIO driver and also to implement the
 * generic parts of a Linux network driver.
 *
 * @{
 */

#ifndef _NANONET_H
#define _NANONET_H
//#define WIFI_ENGINE_CLEAN_API
#include "wifi_engine.h"
#include "transport.h"
#include "compat24.h"

int nano_eth_header(struct sk_buff*, struct net_device*, unsigned short,
		    void*, void*, unsigned);
int nrx_send_buf(struct sk_buff*);

/*!
 * @brief Raw tx/rx functions used in unplugged mode
 *
 * @return int
 *
 */

int nrx_raw_tx(struct net_device * dev, char * data, size_t len);
int nrx_raw_rx(struct net_device * dev, char * data, size_t *max_len);



extern int nano_scan_wait;
extern char *nrx_config;
extern char *nrx_macpath;

int nrx_trsp_ctrl(void *dev, uint32_t command, uint32_t mode);

int nrx_wxevent_ap(struct net_device *dev);
int nrx_wxevent_rssi_trigger(struct net_device *dev, int32_t);
int nrx_wxevent_scan_complete(struct net_device *dev);
int nrx_wxevent_pmkid_candidate(struct net_device*, void*, int32_t, uint16_t);
int nrx_wxevent_michael_mic_failure(struct net_device*, void*, int);
int nrx_wxevent_device_reset(struct net_device *dev);
int nrx_wxevent_connection_lost(struct net_device *dev, we_conn_lost_ind_t *data);
int nrx_wxevent_incompatible(struct net_device *dev, we_conn_incompatible_ind_t *data);
int
nrx_wxevent_scan(struct net_device *dev, 
                 m80211_nrp_mlme_scannotification_ind_t *ind);
int nrx_wxevent_mibtrig(void *data, 
                        size_t len);
int nrx_wxevent_txrate(void *data, size_t len);
int nrx_wxevent_rxrate(void *data, size_t len);
int nrx_wxevent_no_beacon(void);
int nrx_wxevent_txfail(void);


#ifdef ENABLE_WARN_UNUSED
#define WARN_UNUSED __attribute__ ((warn_unused_result))
#else
#define WARN_UNUSED
#endif

int nano_fw_download(struct net_device *dev);

int nrx_enter_shutdown(struct net_device *dev);
int nrx_exit_shutdown(struct net_device *dev);
int nrx_get_mib(struct net_device *dev, 
                const char *id, 
                void *data, 
                size_t *len);
int synchronous_ConfigureScan(struct net_device *dev,
                              preamble_t preamble,
                              uint8_t rate,
                              uint8_t probes_per_ch,
                              WiFiEngine_scan_notif_pol_t notif_pol,
                              uint32_t scan_period,
                              uint32_t probe_delay,
                              uint16_t pa_min_ch_time,
                              uint16_t pa_max_ch_time,
                              uint16_t ac_min_ch_time,
                              uint16_t ac_max_ch_time,
                              uint32_t as_scan_period,
                              uint16_t as_min_ch_time,
                              uint16_t as_max_ch_time,
                              uint32_t max_scan_period,
                              uint32_t max_as_scan_period,
                              uint8_t  period_repetition);
int synchronous_AddScanFilter(struct net_device *dev,
                              int32_t *sf_id,
                              WiFiEngine_bss_type_t bss_type,
                              int32_t rssi_thr,
                              uint32_t snr_thr,
                              uint16_t threshold_type);
int synchronous_RemoveScanFilter(struct net_device *dev,
                                 int32_t sf_id);
int synchronous_AddScanJob(struct net_device *dev,
                           int32_t *sj_id,
                           m80211_ie_ssid_t ssid,
                           m80211_mac_addr_t bssid,
                           uint8_t scan_type,
                           channel_list_t ch_list,
                           int flags,
                           uint8_t prio,
                           uint8_t ap_exclude,
                           int sf_id,
                           uint8_t run_every_nth_period);
int synchronous_RemoveScanJob(struct net_device *dev,
                              int32_t sj_id);
int synchronous_SetScanJobState(struct net_device *dev,
                                int32_t sj_id, 
                                uint8_t state);
uint32_t nrx_get_corecount(struct net_device*);

void nrx_set_corecount(struct net_device*, uint32_t);

int nrx_create_coredump(struct net_device*, uint8_t, uint8_t, char*, size_t);

int nrx_pcap_append(uint16_t flags, const void *pkt, size_t len);
int nrx_pcap_setup(void);
int nrx_pcap_cleanup(void);

int nrx_log_setup(void);
int nrx_log_cleanup(void);

int nrx_set_dscp_mapping(uint8_t *buf, size_t len);
int nrx_get_dscp_mapping(uint8_t *buf, size_t len);

void nrx_tx_queue_stop(struct net_device *dev);
void nrx_tx_queue_wake(struct net_device *dev);

#endif /* ! _NANONET_H */
