/** @defgroup we_pal Implements support for BT 3.0 AMP
 *
 * approx size build with debugging
 * xxkB total
 *
 **/

#include "driverenv.h"
#include "we_ind.h"
#include "wifi_engine_internal.h"
#include "hmg_defs.h"
#include "pal_command_parser.h"


static int wei_pal_default_HCI_response_fn(mac_api_net_id_t net_id, char* data_buf, int size);
static int wei_pal_data_receive(mac_api_net_id_t net_id, char* data_buf, int size);
static int wei_pal_data_confirm(mac_api_net_id_t net_id, char* data_buf, int size);

net_rx_cb_t HCI_response_cb = wei_pal_default_HCI_response_fn;

int wei_pal_init(void)
{
   DE_TRACE_STATIC(TR_AMP, "Register data path\n");
	//Init_Command_Parser();
   return WiFiEngine_Transport_RegisterDataPath(NETWORK_ID_AMP, wei_pal_data_receive, wei_pal_data_confirm);
}

/* Called by PAL to send a data frame to an amp network (NETWORK_ID_AMP)
  *
  * data_buf points at an ethernet header.
  *
  * The ethernet header is assumed to have the following layout :
  * <dst addr:6><src addr:6><type:2>...
  */
int wei_pal_send_data(char* data_buf, int size,  uint32_t trans_id)
{
   int      result;
  // uint32_t trans_id;

   DE_TRACE_STATIC(TR_AMP, "Do send:\n");
   DBG_PRINTBUF("PAL data: ", (unsigned char *)data_buf, size);

   result = WiFiEngine_Transport_Send(NETWORK_ID_AMP, data_buf, size, 0 /* WMM Prio */, &trans_id);

   DE_TRACE_INT2(TR_AMP, "result %d, trans id: %d\n", result, trans_id);
   return  result;
}

int WiFiEngine_PAL_RegisterHCIInterface (net_rx_cb_t HCI_response_fn)
{
   DE_TRACE_STATIC(TR_AMP, "Setup callback\n");

   HCI_response_cb = HCI_response_fn;

	
   return TRUE;
}

int WiFiEngine_PAL_HCIRequest(char* data_buf, int size)
{

	DE_TRACE_STATIC(TR_AMP, "HCI Request:\n");
   	DBG_PRINTBUF("HCI data: ", (unsigned char *)data_buf, size);


   /* Parse HCI commands from BT stack and generate signals to hmg_pal*/
  	wei_sm_queue_sig(INTSIG_EVAL_HCI,
                                         SYSDEF_OBJID_HOST_MANAGER_PAL,
                                         (wei_sm_queue_param_s*) data_buf,
                                         FALSE);
	
	wei_sm_execute();
   return TRUE;
}

/*!
 * @brief Called by driver when it has received a data frame from amp network (NETWORK_ID_AMP)
 *
 * This function is executed in rx process context. any access to static variables must be protected.
 *
 * @param data_buf points at an ethernet header.
 *
 * The ethernet header is assumed to have the following layout :
 * <dst addr:6><src addr:6><type:2>...
 *
 * @return WIFI_ENGINE_SUCCESS_ABSORBED
 */
static int wei_pal_data_receive(mac_api_net_id_t net_id, char* data_buf, int data_len)
{
	char *HCI_Packet;
#ifdef WE_PAL
		int i;
#endif		
	
	HCI_Packet =(char*)DriverEnvironment_Malloc(256+14);
	DE_TRACE_STATIC(TR_AMP, "Data ind\n");
#ifdef WE_PAL
	DE_TRACE_INT(TR_AMP, "data_len = %02x\n",data_len);
	for (i=0; i<=data_len; i++)
   {
		DE_TRACE_INT(TR_AMP, "rec data = %02x\n",data_buf[i]);
   }
	DE_TRACE_INT(TR_AMP, "HCI legnth = %02x\n",data_buf[17]);
#endif		
	DE_TRACE_INT(TR_AMP, "net_id: %d\n", net_id);
	DBG_PRINTBUF("PAL data: ", (unsigned char *)data_buf, data_len);
	
	//Problem with Length filed, related to !byte alignment?
	//Expected 6+6+2 (MAC+MAC+Prio)
	//HCI length used instead, 
	memcpy(HCI_Packet, (data_buf+14), (5+data_buf[17]+(data_buf[18]*0x100)));
	HCI_response_cb(net_id, HCI_Packet, (5+data_buf[17]+(data_buf[18]*0x100)));
   DriverEnvironment_Free(HCI_Packet);
	return WIFI_ENGINE_SUCCESS_ABSORBED;
}


/*!
 * @brief Called by driver when it has received a data frame from amp network (NETWORK_ID_AMP)
 *
 * This function is executed in rx process context. any access to static variables must be protected.
 *
 * @param data_buf    Points at an ethernet header.
 * @param data_len    Size of complete packet
 *
 * The ethernet header is assumed to have the following layout :
 * <dst addr:6><src addr:6><type:2>...
 *
 * @return WIFI_ENGINE_SUCCESS_ABSORBED
 */
static int wei_pal_data_confirm(mac_api_net_id_t net_id, char* data_buf, int data_len)
{
#ifdef WIFI_DEBUG_ON
   uint32_t trans_id = HIC_DATA_CFM_GET_TRANS_ID(data_buf);
#endif

   DE_TRACE_INT2(TR_AMP, "net_id: %d, trans id: %d\n", net_id, trans_id);

	number_of_completed_data_blocks();
   return WIFI_ENGINE_SUCCESS_ABSORBED;
}

static int wei_pal_default_HCI_response_fn(mac_api_net_id_t net_id, char* data_buf, int size)
{
   DE_BUG_ON(1 , "No HCI response function has been registered");
   return TRUE;
}

