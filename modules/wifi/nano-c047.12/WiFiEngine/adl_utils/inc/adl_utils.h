/* $Id:  Exp $ */
/*****************************************************************************

Copyright (c) 2009 by Nanoradio AB

This software is copyrighted by and is the sole property of Nanoradio AB.
All rights, title, ownership, or other interests in the
software remain the property of Nanoradio AB.  This software may
only be used in accordance with the corresponding license agreement.  Any
unauthorized use, duplication, transmission, distribution, or disclosure of
this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without
notice.

Nanoradio AB
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                            http://www.nanoradio.se
SWEDEN

Module Description :
==================
ADL-tools contains functions for simplify the use of WiFi-Engine.

If you modify any code the following rules apply:
-WiFi-Engine must never depend on any ADL-tool
-All code should be platform independent (do not include ndis or any other 
 platform specific module).
-This is not a place for dumping functions that will be used only by one driver.


*****************************************************************************/


#ifndef __ADL_TOOLS_H
#define __ADL_TOOLS_H

#include "wifi_engine.h"
#include "transport.h"

typedef enum _wlan_security_type{
   WLAN_SECURITY_OPEN,
   WLAN_SECURITY_WEP40,
   WLAN_SECURITY_WEP128,
   WLAN_SECURITY_WPA_PSK,
   WLAN_SECURITY_WPA2_PSK,
   WLAN_SECURITY_OTHER = 127
}wlan_security_type;


typedef enum _connect_status{
   WLAN_CONNECTED,
   WLAN_CONNECT_TIMEOUT,
   WLAN_CONNECT_FAILED,
   WLAN_CONNECT_DISCONNECTED,
   WLAN_DISCONNECTED = WLAN_CONNECT_DISCONNECTED,
   WLAN_CONNECTING
}connect_status;


typedef enum _driver_mode{
   WLAN_MODE_NORMAL,          //x_mac / normal
   WLAN_MODE_RFTEST           //x_test / RF-test
}driver_mode;


/************************ used by adl_rf_test.c *******************************/
typedef struct _OemWlanTmConfigInfoType
{
   uint32_t       Channel;
   uint32_t       DataRate;
   uint32_t       Preamble;
   uint32_t       PacketLength;
   uint32_t       NumOfFrame;
   uint32_t       TxPower;
   uint32_t       iGoodFrame;
   uint32_t       iErrorFrame;
   uint32_t       iTotalFrame;
   uint32_t       iInterPacketDelay;
   unsigned char  MacAddr[6];
} WlanTestConfigInfoType;

typedef struct hwt_req_s {
   int no;
   WlanTestConfigInfoType attr_param;
   void (*result_cb)(int status, struct hwt_req_s *req);
   void *priv;
} hwt_req_t;

#define HWT_START_TEST_MODE               0x90
#define HWT_STOP_TEST_MODE                0x91

#define HWT_TX_REQ_END                    0x86

#define HWT_TX_REQ_CW_START               0x87
#define HWT_TX_REQ_PKT_START              0x88
#define HWT_TX_REQ_CONTINUOUS_START       0x89

#define HWT_RX_REQ_END                    0x8A
#define HWT_RX_REQ_START                  0x8B

#define HWT_RESET_COUNTERS_REQ            HWT_RX_REQ_START
#define HWT_GET_RX_COUNTERS_REQ           HWT_RX_REQ_END




typedef void (*nrwifi_auto_connect_cb)(connect_status status);
typedef void (*nrwifi_scan_done_cb)(WiFiEngine_net_t* netlist, int num_of_nets);
typedef void (*nrwifi_startup_cb)(int status, driver_mode mode);
typedef void (*nrwifi_startup_extra_cb)(driver_mode mode);
typedef void (*nrwifi_rf_statiscs_cb)(WlanTestConfigInfoType *stats);




typedef struct _callback_fuctions {
   nrwifi_startup_cb          startup_done_cb;
   from_transport_receive_t   data_from_target_cb;
   transport_free_send_buffer free_tx_data_cb;

   //this function will be called before configure device
   //this is where you sett mac-adress
   nrwifi_startup_extra_cb    before_config_cb;
}callback_fuctions;


int nrwifi_auto_connect(const char* ssid,
                        wlan_security_type security_type,
                        int timeout,
                        const char* security_key,
                        nrwifi_auto_connect_cb callback_fn);


int            nrwifi_auto_disconnect(void);
connect_status nrwifi_get_connection_status(void);

void           nrwifi_scan_all(nrwifi_scan_done_cb callback_fn);
void           nrwifi_scan_one(nrwifi_scan_done_cb callback_fn, const char* ssid);
void           nrwifi_clear_scanlist(void);

int            nrwifi_startup(driver_mode mode, callback_fuctions* callback_fn);
int            nrwifi_shutdown(void);
void           nrwifi_rf_test_start(void);
void           nrwifi_start_rx_test(int channel);
void           nrwifi_get_rx_stats(nrwifi_rf_statiscs_cb cb_fn, int rate);
void           nrwifi_start_pkt_tx( int PacketLength,
                                    int NumOfFrame,
                                    int InterPacketDelay,
                                    int rate);





#endif /* __ADL_TOOLS_H */
