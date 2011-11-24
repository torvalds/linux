/** @defgroup we_multi_net Implements support for multiple concurrent network connections
 *
 * approx size build with debugging
 * xxkB total
 *
 **/

#include "driverenv.h"
#include "wifi_engine.h"
#include "wifi_engine_internal.h"
#include "mac_api.h"

net_rx_cb_t data_path_rx_cb = NULL;
net_rx_cb_t data_path_cfm_cb = NULL;

/*!
 * @brief Reset everything that has to do with AMP
 * 
 * @param net_id        Unique identifier for the network defined by driver.
 * @param reset_target ***TBD*** 
 *
 * @return WIFI_ENGINE_SUCCESS if successful.
 */
int WiFiEngine_Transport_Reset(mac_api_net_id_t net_id, bool_t reset_target)
{
   DE_TRACE_INT2(TR_AMP, "net_id: %d, reset: %d\n", net_id, reset_target);
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Discards any AMP data buffer in driver and firmware 
 * 
 * For each buffer added to the tx pipe by WiFiEngine_AMP_Transport_Send
 * a HIC_AMP_DATA_CFM will be generated that can be handled in the hmg_pal 
 * statemachine. The status of the data confirm message AMP_DATA_FLUSHED.
 *
 * The PAL layer will use the transaction id's to handle the data buffers
 * to filter out, sort, discard or whatever the data buffers.
 *
 * @param net_id        Unique identifier for the network defined by driver.
 *
 * @return WIFI_ENGINE_SUCCESS if successful.
 */
int WiFiEngine_Transport_Flush(mac_api_net_id_t net_id)
{
   DE_TRACE_INT(TR_AMP, "net_id: %d\n", net_id);
   return WIFI_ENGINE_SUCCESS;
}



/*!
 * @brief Request transmission of AMP data over wifi
 *
 * The buffer shall be kept by the PAL layer until a cfm message with the same 
 * trans_id has been returned.
 *
 * @param net_id        Unique identifier for the network defined by driver.
 * @param m802_1_data   Data frame in 802.1 format, including LLC and SNAP headers
 * <dst mac><src mac><type_len><llc snap><data>
 * If <type_len>  is below 0x600 it will be interpreted as a length filed.
 * @param data_len      Size in bytes of the buffer to send.
 * @param vlanid_prio   VLAN ID and 802.1p priority value
 *                      Ignored for association without QoS
 *                      using following format:
 * <PRE>
 *       1
 * 5|432109876543|210
 * -+------------+---
 * 0|   VLANID   |PRI
 * </PRE>
 * @param trans_id_p    Output buffer for the data transaction ID. 
 *
 * @return WIFI_ENGINE_SUCCESS if successful.
 */
int WiFiEngine_Transport_Send(mac_api_net_id_t net_id, void* m802_1_data, int data_len, uint16_t vlanid_prio, uint32_t *trans_id_p)
{
   size_t   dhsize = WiFiEngine_GetDataHeaderSize();
   int      len = dhsize + data_len;
   int      pad_len = 0;
   int      status;
   char*    pkt;
	
   DE_TRACE_INT2(TR_AMP, "net_id: %d, vlanid_prio: %d\n", net_id, vlanid_prio);

	if(wifiEngineState.config.pdu_size_alignment != 0 &&
	   (len % wifiEngineState.config.pdu_size_alignment) != 0) {
		pad_len = wifiEngineState.config.pdu_size_alignment - len % wifiEngineState.config.pdu_size_alignment;
	}

	pkt = (char*)DriverEnvironment_TX_Alloc(len + pad_len);
	DE_MEMCPY(pkt + dhsize, m802_1_data, data_len);

	status = WiFiEngine_ProcessSendPacket(pkt + dhsize, 14, 
					                          len - dhsize, pkt, &dhsize, 0, NULL);
	if(status == WIFI_ENGINE_SUCCESS) 
   {
		pkt[5] = pad_len;
		*(uint16_t*)pkt += pad_len;
		DriverEnvironment_HIC_Send(pkt, len + pad_len);
		return WIFI_ENGINE_SUCCESS;
	}
	DriverEnvironment_TX_Free(pkt);

   return WIFI_ENGINE_FAILURE;
}


/*!
 * @brief Registers a callback function for receiving data from a specific network
 *
 * @param net_id        Unique identifier for the network defined by driver.
 * @param rx_handler    Callback function that will be called when data is received.
 * @param cfm_handler   Callback function that will be called when a data packet has been sent.
 *
 * @return WIFI_ENGINE_SUCCESS if successful.
*/
int WiFiEngine_Transport_RegisterDataPath(mac_api_net_id_t net_id, net_rx_cb_t rx_handler, net_rx_cb_t cfm_handler)
{
   DE_TRACE_INT(TR_AMP, "net_id: %d\n", net_id);

   data_path_rx_cb  = rx_handler;
   data_path_cfm_cb = cfm_handler;
   return WIFI_ENGINE_SUCCESS;
}
   

/*!
 * @brief Set the Pairwise Master Key for a WPA2 connection
 *
 * The key will be sent to the supplicant (internal or external).
 * !UL 111113 (probably the method needs to be abstracted by a driver env call)!
 * !UL 111113 (check with costas/johan if any more key info is needed in call)!
 *
 * @param net_id   Unique identifier for the network defined by driver.
 * @param pmk     Pairwise Master Key data.
 * @param size      Size in bytes of the pmk (32 bytes).
 *
 * @return WIFI_ENGINE_SUCCESS if successful.
 */  
int WiFiEngine_Supplicant_Set_PSK(mac_api_net_id_t net_id, char* pmk, int size)
{
   DE_TRACE_INT(TR_AMP, "net_id: %d\n", net_id);

   DBG_PRINTBUF("PSK: ", (unsigned char *)pmk, size);

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Set the SSID that will be used when starting a network
 *
 * !UL 111113 (add parameters in defaultreg)!
 *
 * The SSID will be used when for identifying a network. It may be either 
 * a BSS when acting as AP, a BSS when acting as STA, or a peer to peer network 
 * (e.g when setting up an AMP link)    
 *
 * @param net_id   Unique identifier for the network defined by driver.
 * @param ssid    Input buffer containg SSID string.
 * @param size    size of the input buffer. If this is 0
 *                then the desired SSID will be unset. Max size is 32 bytes.
 *
 * @return WIFI_ENGINE_SUCCESS on success. 
 * WIFI_ENGINE_FAILURE_INVALID_LENGTH if the input string was too long.
 */
int WiFiEngine_NetSetSSID(mac_api_net_id_t net_id, char *ssid, int size)
{
   DE_TRACE_INT2(TR_AMP, "net_id: %d, ssid length: %d\n", net_id, size);
   DE_TRACE_STRING(TR_AMP, "ssid: %s\n", ssid);

   WiFiEngine_SetSSID(ssid, size);

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Start to send beacons.
 *
 * @param net_id   Unique identifier for the network defined by driver.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_NetStartBeacon(mac_api_net_id_t net_id)
{
   DE_TRACE_INT(TR_AMP, "net_id: %d\n", net_id);

   return WiFiEngine_sac_start();
}


/*!
 * @brief Stop to send beacons.
 * 
 * @param net_id   Unique identifier for the network defined by driver.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_NetStopBeacon(mac_api_net_id_t net_id)
{
   DE_TRACE_INT(TR_AMP, "net_id: %d\n", net_id);

   return WiFiEngine_sac_stop();
}


/*!
 * @brief Connect and maintain connection with BSS.
 * 
 * @param net_id   Unique identifier for the network defined by driver.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_NetConnect(mac_api_net_id_t net_id)
{
   DE_TRACE_INT(TR_AMP, "net_id: %d\n", net_id);

   WiFiEngine_NetStartBeacon(net_id);

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Disconnect and stop maintaining connection with BSS.
 * 
 * @param net_id   Unique identifier for the network defined by driver.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_NetDisconnect(mac_api_net_id_t net_id)
{
   DE_TRACE_INT(TR_AMP, "net_id: %d\n", net_id);

   WiFiEngine_NetStopBeacon(net_id);

   return WIFI_ENGINE_SUCCESS;
}



