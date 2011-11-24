/* $Id: we_data.c,v 1.154 2008/04/07 08:33:27 anob Exp $ */
/*****************************************************************************

Copyright (c) 2004 by Nanoradio AB

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
This module implements the WiFiEngine data path interface.

*****************************************************************************/
/** @defgroup we_data WiFiEngine data path interface
 *
 * @brief The WiFiEngine data path interface handles preparation of packets
 * for transmission and processing of received packets prior to
 * forwarding them to the consumer/IP stack above the driver.
 *
 * Packets to be transmitted are send to
 * WiFiEngine_ProcessSendPacket() which will allow or prohibit the
 * send to go forward depending on the flow control and power save
 * state of the device as well as the status of the 802.1X port. If
 * the transmission is allowed a internal data header is constructed
 * which must be prepended to the packet before transmission to the
 * hardware.
 *
 * Received packets are sent to WiFiEngine_ProcessReceivedPacket()
 * which will strip the data header from the packet and update the
 * flow control state of the driver. Packets may constitute management
 * messages that should not be forwarded upwards in the protocol stack
 * and in these cases WiFiEngine_ProcessSendPacket() will update the
 * driver state and "consumes" the message by indicating that it
 * should be freed.
 *
 *  @{
 */
#include "driverenv.h"
#include "ucos.h"
#include "m80211_stddefs.h"
#include "registry.h"
#include "packer.h"
#include "registryAccess.h"
#include "wifi_engine_internal.h"
#include "hmg_defs.h"
#include "mlme_proxy.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "wei_asscache.h"
#include "pkt_debug.h"
#include "state_trace.h"

#ifdef FILE_WE_DATA_C
#undef FILE_NUMBER
#define FILE_NUMBER FILE_WE_DATA_C
#endif //FILE_WE_DATA_C

int is_eapol_packet(unsigned char *p, int max)
{
   if(max < 14)
      return FALSE;

   /*
    * if 802.2 or 802.3 or SNAP
    * uint16_t len;
    * DE_MEMCPY(&len,p+12,2);
    * if(ntohw(len) < 0x0800)
    */
   if(p[12] < 0x08)
   {
      if(max < 14+1+1+1+3+2)
         return FALSE;

      // SNAP (802.3 with 802.2 and SNAP headers)
      return (p[14] == 0xaa &&
              p[15] == 0xaa &&
              p[16] == 0x03 &&
              p[17] == 0x00 &&
              p[18] == 0x00 &&
              p[19] == 0x00 &&
              p[20] == 0x88 && ((p[21] == 0x8e)
                                || (p[21] == 0xb4)
                                ));
   }
   // ethernet
   return (p[12] == 0x88 && p[13] == 0x8e)
     || (p[12] == 0x88 && p[13] == 0xb4)
     ;
}

#if (DE_CCX == CFG_INCLUDED)
static int send_addts_req(size_t dialog_token, size_t identifier, char* TSPECbody)
{
    char *pkt;
    hic_message_context_t   msg_ref;
    m80211_nrp_mlme_addts_req_t addts_req;
    uint32_t trans_id;
    int status;
   
    DE_TRACE_STATIC(TR_SM, "send_addts_req!\n");

    pkt = DriverEnvironment_TX_Alloc(74);
    if(dialog_token == 0)
    {
	dialog_token=1;
    }

    //TSPEC Element
    //TSPEC Header
    addts_req.action_code = 0x00; //ADDTS Request

    //WMM TSPEC Element
    addts_req.body.dialog_token = dialog_token;
    addts_req.body.status_code = 0x00; //Admission Accepted

    addts_req.body.wmm_tspec_ie.WMM_hdr.hdr.hdr.id = 0xDD; //WMM
    addts_req.body.wmm_tspec_ie.WMM_hdr.hdr.hdr.len = 0x3D;
    addts_req.body.wmm_tspec_ie.WMM_hdr.hdr.OUI_1 = 0x00;
    addts_req.body.wmm_tspec_ie.WMM_hdr.hdr.OUI_2 = 0x50;
    addts_req.body.wmm_tspec_ie.WMM_hdr.hdr.OUI_3 = 0xF2;
    addts_req.body.wmm_tspec_ie.WMM_hdr.hdr.OUI_type = 0x02;
    addts_req.body.wmm_tspec_ie.WMM_hdr.OUI_Subtype = WMM_IE_OUI_SUBTYPE_TSPEC;  //0x02
    addts_req.body.wmm_tspec_ie.WMM_Protocol_Version = 0x01;

    if(TSPECbody == NULL)
    {
	//TSPEC Body

        pkt[0] = 0xE0 | (identifier<<1);
        pkt[1] = 0x34; //0x1A
        pkt[2] = 0x00; //0x00
	    
        pkt[3] = 0xD0;
        pkt[4] = 0x80;
	    
        pkt[5] = 0xD0;
        pkt[6] = 0x00;

        pkt[7]  = 0x20;
        pkt[8]  = 0x4E;
        pkt[9]  = 0x00;
        pkt[10] = 0x00;

        pkt[11] = 0x20;
        pkt[12] = 0x4E;
        pkt[13] = 0x00;
        pkt[14] = 0x00;

        pkt[15] = 0x80;
        pkt[16] = 0x96;
        pkt[17] = 0x98;
        pkt[18] = 0x00;

        pkt[19] = 0xF6;
        pkt[20] = 0xFF;
        pkt[21] = 0xFF;
        pkt[22] = 0x9F;

        pkt[23] = 0x00;
        pkt[24] = 0x00;
        pkt[25] = 0x00;
        pkt[26] = 0x00;

        pkt[27] = 0x00;
        pkt[28] = 0x45;
        pkt[29] = 0x01;
        pkt[30] = 0x00;

        pkt[31] = 0x00;
        pkt[32] = 0x45;
        pkt[33] = 0x01;
        pkt[34] = 0x00;

        pkt[35] = 0x00;
        pkt[36] = 0x45;
        pkt[37] = 0x01;
        pkt[38] = 0x00;

        pkt[39] = 0x00;
        pkt[40] = 0x00;
        pkt[41] = 0x00;
        pkt[42] = 0x00;

        pkt[43] = 0x00;
        pkt[44] = 0x00;
        pkt[45] = 0x00;
        pkt[46] = 0x00;

        pkt[47] = 0x80;
        pkt[48] = 0x8d;
        pkt[49] = 0x5b;
        pkt[50] = 0x00;

        pkt[51] = 0x00;
        pkt[52] = 0x00;

        pkt[53] = 0x42;
        pkt[54] = 0x04;

        DE_MEMCPY(addts_req.body.wmm_tspec_ie.TSPEC_body, &pkt[0], 55);
        DE_MEMCPY(wifiEngineState.ccxState.addts_state[identifier].TSPEC_body, &pkt[0], 55);
    }
    else
    {
        DE_MEMCPY(addts_req.body.wmm_tspec_ie.TSPEC_body, TSPECbody, 55);
	DE_MEMCPY(wifiEngineState.ccxState.addts_state[identifier].TSPEC_body, TSPECbody, 55);
	
    }	     

    Mlme_CreateMessageContext(msg_ref);


    if (Mlme_CreateADDTSRequest(&msg_ref, &addts_req, &trans_id))
    {
       wei_send_cmd(&msg_ref);
       DE_TRACE_STATIC2(TR_SM,"Create addts_req succesfull, identifier %d\n", identifier);
       status = WIFI_ENGINE_SUCCESS;
    }
    else
    {
       DE_TRACE_STATIC(TR_SM,"Failed to create addts_req\n");
       status = WIFI_ENGINE_FAILURE;
    }

    DriverEnvironment_TX_Free(pkt);
    Mlme_ReleaseMessageContext(msg_ref);
 
    return 1;
}

int send_delts(size_t identifier)
{
    hic_message_context_t       msg_ref;
    m80211_nrp_mlme_delts_cfm_t *cfm;
    m80211_nrp_mlme_delts_req_t delts;
    uint32_t trans_id;
    int status;
   


    delts.action_code = 0x02; //DELTS
    delts.dialog_token = 0;   // Dialog token is 0 for DELTS
    delts.status_code = 0x00; //Admission Accepted

    delts.wmm_tspec_ie.WMM_hdr.hdr.hdr.id = 0xDD; //WMM
    delts.wmm_tspec_ie.WMM_hdr.hdr.hdr.len = 0x3D; //0x0A;
    delts.wmm_tspec_ie.WMM_hdr.hdr.OUI_1 = 0x00;
    delts.wmm_tspec_ie.WMM_hdr.hdr.OUI_2 = 0x50;
    delts.wmm_tspec_ie.WMM_hdr.hdr.OUI_3 = 0xF2;
    delts.wmm_tspec_ie.WMM_hdr.hdr.OUI_type = 0x02;
    delts.wmm_tspec_ie.WMM_hdr.OUI_Subtype = WMM_IE_OUI_SUBTYPE_TSPEC;  //0x02
    delts.wmm_tspec_ie.WMM_Protocol_Version = 0x01;

    delts.wmm_tspec_ie.TSPEC_body[0] = 0xE0 | (identifier<<1);
    delts.wmm_tspec_ie.TSPEC_body[1] = 0x34; //0x1A
    delts.wmm_tspec_ie.TSPEC_body[2] = 0x00; //0x00
/*
    delts.WMM_hdr.hdr.hdr.id = 0xDD; //WMM
    delts.WMM_hdr.hdr.hdr.len = 0x0A;
    delts.WMM_hdr.hdr.OUI_1 = 0x00;
    delts.WMM_hdr.hdr.OUI_2 = 0x50;
    delts.WMM_hdr.hdr.OUI_3 = 0xF2;
    delts.WMM_hdr.hdr.OUI_type = 0x02;
    delts.WMM_hdr.OUI_Subtype = WMM_IE_OUI_SUBTYPE_TSPEC;  //0x02
    delts.WMM_Protocol_Version = 0x01;

    //TS Info
    delts.ts_info[0] = 0xEC | (identifier<<1);
    delts.ts_info[1] = 0x34; //0x1A
    delts.ts_info[2] = 0x00; //0x00

    //Reason Code
    //Available values for delts
    // STA_LEAVING = 36
    // END_TS      = 37
    // UNKNOWN_TS  = 38
    // TIMEOUT     = 39
    delts.reason_code = 0x25; //End TS
*/
    wifiEngineState.ccxState.addts_state[identifier].admission_state = 0;
    wifiEngineState.ccxState.addts_state[identifier].active = ADDTS_NOT_TRIED;

    Mlme_CreateMessageContext(msg_ref);

    if (Mlme_CreateDELTSRequest(&msg_ref, &delts, &trans_id))
    {
       wei_send_cmd(&msg_ref);
       DE_TRACE_STATIC(TR_SM,"Create delts_req succesfull\n");
       status = WIFI_ENGINE_SUCCESS;
    }
    else
    {
       DE_TRACE_STATIC(TR_SM,"Failed to create delts_req\n");
       status = WIFI_ENGINE_FAILURE;
    }

     cfm = HIC_GET_RAW_FROM_CONTEXT(&msg_ref, m80211_nrp_mlme_delts_cfm_t);
     /*if (Mlme_HandleDeltsConfirm(cfm)){
        DE_TRACE_STATIC(TR_SM,"DELTS Confirmation successful!!!\n ");
   }*/
   Mlme_ReleaseMessageContext(msg_ref);
 
    return 1;
}

int   WiFiEngine_SendAddtsReq(uint8_t identifier)
{
    return send_addts_req(0, identifier, NULL);
} 

int   WiFiEngine_SendDelts(uint8_t identifier)
{
    return send_delts(identifier);
}

static int GetPacketSize(int *result_entries)
{
    int offset = 0;
    int entries, i;
	int entries_1; 
	int status; 
    
    WiFiEngine_net_t *net=NULL;
	WiFiEngine_net_t *netlist=NULL; 
    WiFiEngine_net_t *nlstart=NULL;
    
	status = WiFiEngine_GetBSSIDList(NULL, &entries);
	if (status != WIFI_ENGINE_SUCCESS){
	   KDEBUG(ERROR,"EXIT EIO");
	   return 0;
	}
	if (entries){
	    netlist = kmalloc(entries * sizeof(*netlist), GFP_KERNEL);
		if(netlist == NULL){
		   kfree(netlist);
		   KDEBUG(ERROR, "out of memory");
           return -1;
		}
		status = WiFiEngine_GetBSSIDList(netlist, &entries_1);   
		if(status != WIFI_ENGINE_SUCCESS){
		   kfree(netlist);
		   KDEBUG(ERROR, "EXIT EIO");
           return -1;
		}
	}

    if(entries>0)
    {
		nlstart = netlist;
		*result_entries = entries;
	   
		for(i=0;i<entries;i++)
	    {
			net = &netlist[i];
			offset+=36;
			
			if(net->bss_p->bss_description_p->ie.ssid.hdr.id != M80211_IE_ID_NOT_USED)
			{
				offset+=2+net->bss_p->bss_description_p->ie.ssid.hdr.len;
			}
			if(net->bss_p->bss_description_p->ie.supported_rate_set.hdr.id != M80211_IE_ID_NOT_USED)
			{
				offset+=2+net->bss_p->bss_description_p->ie.supported_rate_set.hdr.len;
			}
			if(net->bss_p->bss_description_p->ie.fh_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
			{
				offset+=2+net->bss_p->bss_description_p->ie.fh_parameter_set.hdr.len;
			}
			if(net->bss_p->bss_description_p->ie.ds_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
			{
				offset+=2+net->bss_p->bss_description_p->ie.ds_parameter_set.hdr.len;
			}	
			if(net->bss_p->bss_description_p->ie.cf_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
			{
				offset+=2+net->bss_p->bss_description_p->ie.cf_parameter_set.hdr.len;
			}
			if(net->bss_p->bss_description_p->ie.ibss_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
			{
				offset+=2+net->bss_p->bss_description_p->ie.ibss_parameter_set.hdr.len;
			}	
			if(net->bss_p->bss_description_p->ie.tim_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
			{
				offset+=2+net->bss_p->bss_description_p->ie.tim_parameter_set.hdr.len;
			}	
			if(net->bss_p->bss_description_p->ie.ccx_rm_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED)
			{
				offset+=2+net->bss_p->bss_description_p->ie.ccx_rm_parameter_set.hdr.hdr.len;
			}

	    }
	    kfree(netlist); 
    }
	else
	   *result_entries = 0; 
    
	return offset;
}

void wei_createFWStatsReq(bool_t init)
{
    hic_message_context_t msg_ref;
    uint32_t              trans_id;

    //DE_TRACE_STATIC2(TR_SM,"NIKS: f/w Stats_REQ with init=%d\n", init);

    Mlme_CreateMessageContext(msg_ref);

    Mlme_CreateFWStatsRequest(&msg_ref, init, &trans_id);

    wei_send_cmd(&msg_ref);

    Mlme_ReleaseMessageContext(msg_ref);
}
static int traffic_metrics_cb(void *data, size_t len)
{
    /* Request metrics from Firmware; f/w will send data with its CFM which will handle the rest */
    //DE_TRACE_STATIC(TR_SM,"NIKS: Request metrics for last interval from f/w\n");
    wei_createFWStatsReq(FALSE);

    return WIFI_ENGINE_SUCCESS;
}

int radio_measurement_cb(void *data, size_t len)
{
    int entries=0;
	int entries_1; 
    WiFiEngine_net_t *net=NULL;
	WiFiEngine_net_t *netlist=NULL;
    char *pkt;
    size_t pkt_size;
    char dst[6], iface_addr[6];
    int length = 0;
    size_t dhsize = WiFiEngine_GetDataHeaderSize();
    u16 proto=0;
    size_t nr_bytes_added, offset, element_size_offset, element_size;
    m80211_mac_addr_t bssid;
    int count=6;
    int i,j;
    u16 timestamp;
    int status;
    int stripped_packet_len;
	int len_swap;     
	int packetsize=0;
    static int sj_id = -1;	
    m80211_ie_ssid_t bcast_ssid;
    m80211_mac_addr_t bcast_bssid;
    rBasicWiFiProperties*   basic;
    rConnectionPolicy* c;
    rScanPolicy * s;

    DE_TRACE_STATIC(TR_SM, "radio_measurement_cb!\n");
    
    packetsize = GetPacketSize(&entries);
	DE_TRACE_STATIC2(TR_SM, "Number of scanned APs = %d\n", entries);
	if(!packetsize)
	   DE_TRACE_STATIC(TR_SM, "Packetsize is 0!\n");
	else
	   DE_TRACE_STATIC2(TR_SM, "Packetsize is = %d\n", packetsize);

	length = dhsize + 40 + packetsize ; 
    nr_bytes_added = WiFiEngine_GetPaddingLength(length);
	
    pkt = DriverEnvironment_TX_Alloc(length+nr_bytes_added); 
	if (pkt == NULL){
	   DE_TRACE_INT(TR_WARN, "Failed to allocate %u bytes buffer\n", length+nr_bytes_added);
	   return WIFI_ENGINE_FAILURE_RESOURCES;
	}
	
    WiFiEngine_GetBSSID(&bssid);
    DE_MEMCPY(dst, bssid.octet, 6);

    WiFiEngine_GetMACAddress(iface_addr, &count);

    DE_MEMCPY(pkt+dhsize, dst, 6);
    DE_MEMCPY(pkt+dhsize+6, iface_addr, 6);
    proto = ntohs(proto);
    DE_MEMCPY(pkt+dhsize+12, (u8*)&proto, 2);

    pkt[dhsize+14] = 0xaa;
    pkt[dhsize+15] = 0xaa;
    pkt[dhsize+16] = 0x03;

    pkt[dhsize+17] = 0x00;
    pkt[dhsize+18] = 0x40;
    pkt[dhsize+19] = 0x96;
    pkt[dhsize+20] = 0x00;
    pkt[dhsize+21] = 0x00;

    //bytes 22,23 get set near the end of the function
     pkt[dhsize+24] = 0x32;
    pkt[dhsize+25] = 0x81;

	DE_MEMCPY(pkt+dhsize+26, dst, 6);
    DE_MEMCPY(pkt+dhsize+32, iface_addr, 6);

    pkt[dhsize+38] = wifiEngineState.ccxState.dialog_token[0];
    pkt[dhsize+39] = wifiEngineState.ccxState.dialog_token[1];

    offset = dhsize+40;

    if (entries>0)
	{
		netlist=kmalloc(entries * sizeof(*netlist), GFP_KERNEL);
		if(netlist == NULL){
		   kfree(netlist);
		   KDEBUG(ERROR, "out of memory");
           return -1;
		}
		status = WiFiEngine_GetBSSIDList(netlist, &entries_1);   
		if(status != WIFI_ENGINE_SUCCESS){
		   kfree(netlist);
		   KDEBUG(ERROR, "EXIT EIO");
           return -1;
		}
		
	}
    for(i=0;i<entries;i++)
    {
		char isDSS=1;
		net = &netlist[i];
	
		pkt[offset] = 0x27;
		offset++;
		pkt[offset] = 0;
		offset++;
		
		element_size_offset = offset;
		element_size = 0;

		offset+=2;

		pkt[offset] = wifiEngineState.ccxState.measurement_token[0];
		offset++;
		element_size++;
		pkt[offset] = wifiEngineState.ccxState.measurement_token[1];
		offset++;
		element_size++;

		pkt[offset] = 0;
		offset++;
		element_size++;
		pkt[offset] = 0x03;
		offset++;
		element_size++;

		pkt[offset] = wifiEngineState.ccxState.channel;
		offset++;
		element_size++;

		pkt[offset] = 0x01;  //spare
		offset++;
		element_size++;

		DE_MEMCPY(&pkt[offset], &wifiEngineState.ccxState.duration, 2);
		offset+=2;
		element_size+=2;

		if(net->bss_p->bss_description_p->ie.supported_rate_set.hdr.id != M80211_IE_ID_NOT_USED)
		{
			for(j=0;j<net->bss_p->bss_description_p->ie.supported_rate_set.hdr.len;j++)
			{
				if((net->bss_p->bss_description_p->ie.supported_rate_set.rates[j] != 0x82)||(net->bss_p->bss_description_p->ie.supported_rate_set.rates[j] != 0x84)||(net->bss_p->bss_description_p->ie.supported_rate_set.rates[j] != 0x8B)||(net->bss_p->bss_description_p->ie.supported_rate_set.rates[j] != 0x96))
				isDSS = 0;
			}
		}
		if((isDSS==1)&&(net->bss_p->bss_description_p->ie.ext_supported_rate_set.hdr.id != M80211_IE_ID_NOT_USED))
		{
			for(j=0;j<net->bss_p->bss_description_p->ie.ext_supported_rate_set.hdr.len;j++)
			{
				if((net->bss_p->bss_description_p->ie.ext_supported_rate_set.rates[j] != 0x82)||(net->bss_p->bss_description_p->ie.ext_supported_rate_set.rates[j] != 0x84)||(net->bss_p->bss_description_p->ie.ext_supported_rate_set.rates[j] != 0x8B)||(net->bss_p->bss_description_p->ie.ext_supported_rate_set.rates[j] != 0x96))
				isDSS = 0;
			}
		}
			if(isDSS)
		{ 
			pkt[offset] = 0x02;  //dss
			offset++;
			element_size++;
		}
		else
		{ 
			pkt[offset] = 0x04;  //ofdm
			offset++;
			element_size++;
		}

		pkt[offset] = (char)net->bss_p->rssi_info;  //signal strength
		offset++;
		element_size++;	

		DE_MEMCPY(pkt+offset, net->bss_p->bssId.octet, 6);
		offset+=6;
		element_size+=6;

		timestamp = (u16)net->bss_p->local_timestamp;	
		DE_MEMCPY(pkt+offset, &timestamp, 4);
		offset+=4;
		element_size+=4;

		DE_MEMCPY(pkt+offset, &net->bss_p->bss_description_p->timestamp, 8);
		offset+=8;
		element_size+=8;

		DE_MEMCPY(pkt+offset, &net->bss_p->bss_description_p->beacon_period, 2);
		offset+=2;
		element_size+=2;

		DE_MEMCPY(pkt+offset, &net->bss_p->bss_description_p->capability_info, 2);
		offset+=2;
		element_size+=2;

		if(net->bss_p->bss_description_p->ie.ssid.hdr.id != M80211_IE_ID_NOT_USED)
		{
			DE_MEMCPY(pkt+offset, (char*)&net->bss_p->bss_description_p->ie.ssid, net->bss_p->bss_description_p->ie.ssid.hdr.len+2);
			offset+=2+net->bss_p->bss_description_p->ie.ssid.hdr.len;
			element_size+=2+net->bss_p->bss_description_p->ie.ssid.hdr.len;
		}
		if(net->bss_p->bss_description_p->ie.supported_rate_set.hdr.id != M80211_IE_ID_NOT_USED)
		{
			DE_MEMCPY(pkt+offset, (char*)&net->bss_p->bss_description_p->ie.supported_rate_set, net->bss_p->bss_description_p->ie.supported_rate_set.hdr.len+2);
			offset+=2+net->bss_p->bss_description_p->ie.supported_rate_set.hdr.len;
			element_size+=2+net->bss_p->bss_description_p->ie.supported_rate_set.hdr.len;
		}
		if(net->bss_p->bss_description_p->ie.fh_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
		{
			DE_MEMCPY(pkt+offset, (char*)&net->bss_p->bss_description_p->ie.fh_parameter_set, net->bss_p->bss_description_p->ie.fh_parameter_set.hdr.len+2);
			offset+=2+net->bss_p->bss_description_p->ie.fh_parameter_set.hdr.len;
			element_size+=2+net->bss_p->bss_description_p->ie.fh_parameter_set.hdr.len;
		}
		if(net->bss_p->bss_description_p->ie.ds_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
		{
			DE_MEMCPY(pkt+offset, (char*)&net->bss_p->bss_description_p->ie.ds_parameter_set, net->bss_p->bss_description_p->ie.ds_parameter_set.hdr.len+2);
			offset+=2+net->bss_p->bss_description_p->ie.ds_parameter_set.hdr.len;
			element_size+=2+net->bss_p->bss_description_p->ie.ds_parameter_set.hdr.len;
		}	
		if(net->bss_p->bss_description_p->ie.cf_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
		{
			DE_MEMCPY(pkt+offset, (char*)&net->bss_p->bss_description_p->ie.cf_parameter_set, net->bss_p->bss_description_p->ie.cf_parameter_set.hdr.len+2);
			offset+=2+net->bss_p->bss_description_p->ie.cf_parameter_set.hdr.len;
			element_size+=2+net->bss_p->bss_description_p->ie.cf_parameter_set.hdr.len;
		}
		if(net->bss_p->bss_description_p->ie.ibss_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
		{
			DE_MEMCPY(pkt+offset, (char*)&net->bss_p->bss_description_p->ie.ibss_parameter_set, net->bss_p->bss_description_p->ie.ibss_parameter_set.hdr.len+2);
			offset+=2+net->bss_p->bss_description_p->ie.ibss_parameter_set.hdr.len;
			element_size+=2+net->bss_p->bss_description_p->ie.ibss_parameter_set.hdr.len;
		}	
		if(net->bss_p->bss_description_p->ie.tim_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
		{
			DE_MEMCPY(pkt+offset, (char*)&net->bss_p->bss_description_p->ie.tim_parameter_set, net->bss_p->bss_description_p->ie.tim_parameter_set.hdr.len+2);
			offset+=2+net->bss_p->bss_description_p->ie.tim_parameter_set.hdr.len;
			element_size+=2+net->bss_p->bss_description_p->ie.tim_parameter_set.hdr.len;
		}	
		if(net->bss_p->bss_description_p->ie.ccx_rm_parameter_set.hdr.hdr.id != M80211_IE_ID_NOT_USED)
		{
			DE_MEMCPY(pkt+offset, (char*)&net->bss_p->bss_description_p->ie.ccx_rm_parameter_set, net->bss_p->bss_description_p->ie.ccx_rm_parameter_set.hdr.hdr.len+2);
			offset+=2+net->bss_p->bss_description_p->ie.ccx_rm_parameter_set.hdr.hdr.len;
			element_size+=2+net->bss_p->bss_description_p->ie.ccx_rm_parameter_set.hdr.hdr.len;
		}

		DE_MEMCPY(pkt+element_size_offset, &element_size, 2);
    }
	if (entries)
	   kfree(netlist);

    stripped_packet_len = offset - dhsize - 22;
	len_swap = (((stripped_packet_len << 8) & 0xFF00) | ((stripped_packet_len >> 8) & 0xff));
	DE_MEMCPY(pkt+dhsize+22,(char *)&len_swap, 2);

    len = offset;

    status = WiFiEngine_ProcessSendPacket(pkt+dhsize, 14, 
					  len - dhsize,
					  pkt, &dhsize, 0, NULL);

    pkt_size = HIC_MESSAGE_LENGTH_GET(pkt); 

    if(status == WIFI_ENGINE_SUCCESS)
    {
		//DriverEnvironment_HIC_Send(pkt, pkt_size);
		if (DriverEnvironment_HIC_Send(pkt, pkt_size)!= DRIVERENVIRONMENT_SUCCESS) {
			wifiEngineState.cmdReplyPending = 0;
		    return WIFI_ENGINE_FAILURE;
		}
        //wei_initialize_scan();
        basic = (rBasicWiFiProperties*) Registry_GetProperty(ID_basic);
        c = (rConnectionPolicy*) &basic->connectionPolicy;
        s = (rScanPolicy*) &basic->scanPolicy;

        DE_MEMSET(&bcast_ssid, 0, sizeof bcast_ssid); /* IE id for ssid is 0 */
        DE_MEMSET(&bcast_bssid, 0xFF, sizeof bcast_bssid);

        WiFiEngine_RemoveScanJob(0,NULL);
		if(wifiEngineState.ccxState.scan_mode==2)
			WiFiEngine_RemoveScanJob(1,NULL);
        WiFiEngine_AddScanJob((int *)&sj_id, bcast_ssid, bcast_bssid, registry.network.basic.activeScanMode,registry.network.basic.regionalChannels, ANY_MODE, 1, 0, 0, 1, NULL);
        //wei_reinitialize_scan();
        WiFiEngine_SetScanJobState(0, 1, NULL);

        //success, remove pending flag
        wifiEngineState.ccxState.resend_packet_mask &= (~CCX_RDIO_MEASURE_PENDING);

        return 1;
    }
	else
	{
		DE_TRACE_STATIC2(TR_SM_HIGH_RES, "WiFiEngine_ProcessSendPacket returned with error status=%d\n",status);
        DriverEnvironment_TX_Free(pkt);
        //insert pending flag
        wifiEngineState.ccxState.resend_packet_mask |= CCX_RDIO_MEASURE_PENDING;
        return WIFI_ENGINE_FAILURE;
    }
}


static char *LocateIE(char *packet, char *element, int len)
{
    char *p = packet;
    int l = len;

    /* Go through the IEs and return a pointer to the matching IE, if present. */
    while (p && l >= 2) {
        len = p[1] + 2;

        if (len > l) {
            return 0;
        }

        if (DE_MEMCMP(p, element, 6) == 0) 
        {
            return p; 
        }
 
        l -= len;
        p += len;
    }
    return 0;
	}
	
static int HandleAddtsResponsePacket(unsigned char *p, int max)
{
   size_t dialog_token=1;
   size_t identifier=0;
   char TSPEC_Element[] = {0xDD, 0x3D, 0x00, 0x50, 0xF2, 0x02};
   char TSM_Element[] = {0xDD, 0x08, 0x00, 0x40, 0x96, 0x07}; //Traffic Stream Metrics
   char *pTSM_Element = 0;

   //DE_TRACE_STATIC2(TR_SM,"NIKS: HandleAddtsResponsePacket called, size %d.\n", max);

  if(p[10] == 0x11 && //Category code = 17
     p[11] == 0x01 ) //Action Code = ADDTS Response)
  {
     dialog_token = p[12];
     //identifier = (p[24]|0xE)>> 1; //..DILE's code
     identifier = (p[22] & 0x1E)>> 1; //..according to sniffer
     if( p[13] == 0)       //Admission Accepted
	    {
		wifiEngineState.ccxState.addts_state[identifier].admission_state = 0;
		wifiEngineState.ccxState.addts_state[identifier].active = ADDTS_ACCEPTED;
        if(DE_MEMCMP(&p[14], TSPEC_Element, 6)== 0)
		{
            DE_MEMCPY(wifiEngineState.ccxState.addts_state[identifier].TSPEC_body, &p[14+8], 55);
            pTSM_Element = LocateIE(&p[14], TSM_Element, max - 14);

        }

        // Check if TSM element exists
        if (pTSM_Element != 0)
		{
			//TSM_RELATED
             wifiEngineState.ccxState.addts_state[identifier].tsm_state =  pTSM_Element[7];
             if( pTSM_Element[7] == 0x01) //Traffic Stream Metrics Enabled
		     {
				WiFiEngine_stats_t stats;
				
                wifiEngineState.ccxState.metrics.collect_metrics_enabled = TRUE;

                //DE_MEMCPY(&wifiEngineState.ccxState.addts_state[identifier].tsm_interval, &p[14+8+55+8], 2);
                wifiEngineState.ccxState.addts_state[identifier].tsm_interval = (uint16_t) ( (uint16_t)( pTSM_Element[8]&0xff) | (uint16_t) ( pTSM_Element[9]<<8));

                //DE_TRACE_STATIC(TR_SM,"NIKS: Initialize f/w metrics\n");
                wei_createFWStatsReq(TRUE); //initialize f/w metrics
				WiFiEngine_GetStatistics(&stats, 1);
                wifiEngineState.ccxState.metrics.lastFwPktLoss = (uint16_t)stats.dot11FailedCount;
                wifiEngineState.ccxState.metrics.lastFwPktCnt  = (uint16_t)stats.dot11TransmittedFrameCount;
				
				
                DriverEnvironment_RegisterTimerCallback(wifiEngineState.ccxState.addts_state[identifier].tsm_interval,
                                                        wifiEngineState.ccxState.ccx_traffic_stream_metrics_id,
                                                        traffic_metrics_cb,
                                                        TRUE);
		     }
		     else
             {
                wifiEngineState.ccxState.metrics.collect_metrics_enabled = FALSE;
				wifiEngineState.ccxState.addts_state[identifier].tsm_interval = 0;
                DriverEnvironment_CancelTimer(wifiEngineState.ccxState.ccx_traffic_stream_metrics_id);
		}
	    }
        }
     else if( (p[13] == 0x01) || (p[13] == 0xCB) )
        {
	    //Admission refused - let's retry with suggested parameters...
        if((p[14]==0xDD)&&(p[16]==0x00)&&(p[17]==0x50)&&(p[18]==0xF2)&&(p[19]==0x02)&&(p[20]==0x02)&&(p[21]==0x01))
	    { 		
		//save admission state and re-send TSPEC with suggested parameters
		if(wifiEngineState.ccxState.addts_state[identifier].dialog_token == dialog_token)
	   	{
                wifiEngineState.ccxState.addts_state[identifier].admission_state = p[13];
		    wifiEngineState.ccxState.addts_state[identifier].active = ADDTS_REFUSED_RETRY;
	        }

            send_addts_req(dialog_token, identifier, &p[22]);  //p[22]=TS_info
 	    }
	     
        }   
     else if((p[13] == 0xC9) || (p[13] == 0x03) || (p[13] == 0xCA))
        {
	    //Admission refused - do not retry with different parameters
        wifiEngineState.ccxState.addts_state[identifier].admission_state = p[13];
	    wifiEngineState.ccxState.addts_state[identifier].active = ADDTS_REFUSED_DO_NOT_RETRY_UNTIL_ROAMING;	     
        }   
   	return 1;
   }
   return 0;
}

int is_ccx_delts_packet(unsigned char *p, int max)
{
   size_t dialog_token, identifier;
   if(max < 80)
      return FALSE;

   if(p[12] == 0x00 && p[13] == 0x00 && //Protocol Type 
     p[14] == 0x11 && // Category Code = 17
     p[16] == 0x02)   // Action Code = DELTS    
   {
        dialog_token = p[17];
	identifier = (p[28]|0xE)>> 1;

        wifiEngineState.ccxState.addts_state[identifier].admission_state = 0;
    	wifiEngineState.ccxState.addts_state[identifier].active = ADDTS_NOT_TRIED;

   	return 1;
   }
   return 0;
}

int is_ccx_beacon_req_packet(unsigned char *p, int max, char* channel, int* duration, char* active)
{
   if(max < 42)
      return FALSE;

   if(p[20] == 0x00 && p[21] == 0x00 && //Protocol Type 
      p[22] == 0x00 && // IAPP ID
      p[24] == 0x32 && // IAPP Type
      p[25] == 0x01 && // IAPP SubType
      p[42] == 0x26 && // Element ID = Measurement Req
      p[49] == 0x03)
   {
	 int dur=0;
   	 *channel = p[50];
	

	 wifiEngineState.ccxState.scan_mode = *active = p[51];
	
	 DE_TRACE_STATIC2(TR_SM,"Scan mode =%d\n", wifiEngineState.ccxState.scan_mode);
	
	 DE_MEMCPY(&dur, &p[52], 2);

	 *duration = dur;
     wifiEngineState.ccxState.dialog_token[0] = p[38];
     wifiEngineState.ccxState.dialog_token[1] = p[39];
   	 wifiEngineState.ccxState.measurement_token[0] = p[46];
   	 wifiEngineState.ccxState.measurement_token[1] = p[47];
   
   	 return 1;   // Measurement Type = Beacon Req
    }

   return 0;
}

int HandleRadioMeasurementReq(char channel, int duration, char mode)
{    
    static int sj_id = -1;
    uint8_t new_channel[M80211_CHANNEL_LIST_MAX_LENGTH];
	int i;
    m80211_ie_ssid_t Ssid;
    m80211_mac_addr_t bcast_bssid;
    channel_list_t channelList;
	channel_list_t channelList2;
	char *p;
    p = new_channel;
 
	//Suspend the default scan job
	WiFiEngine_RemoveScanJob(0, NULL);
	if(mode==2)
	  WiFiEngine_RemoveScanJob(1, NULL);
    WiFiEngine_FlushScanList(); 

    DE_TRACE_STATIC(TR_SM, "HandleRadioMeasurementReq\n");

    wifiEngineState.ccxState.channel = channel;
    wifiEngineState.ccxState.duration = duration;

	//Create new channel list
	if(mode==2)
	{
		channelList2.no_channels = M80211_CHANNEL_LIST_MAX_LENGTH-1;
        for(i=0;i<M80211_CHANNEL_LIST_MAX_LENGTH;i++)
		{
			if(i+1 != channel) 
			{
				*p = i+1;
				p++;
			}
		}
		memcpy(channelList2.channelList,new_channel, channelList2.no_channels);
	}
    channelList.no_channels = 1;
    channelList.channelList[0] = channel;
	
    Ssid.hdr.len = 0;
    Ssid.hdr.id = M80211_IE_ID_NOT_USED;
    DE_MEMSET(&bcast_bssid, 0xFF, sizeof(bcast_bssid));

	//WiFiEngine_ConfigureScan(SHORT_PREAMBLE,108,2,15,5000,0,20,20,20,40,15000,6,10,5000,5000,1,NULL);
 
	//Passive scan on the serving channel and active scan on other available channels
	if(mode==2)
	{
	  WiFiEngine_ConfigureScan(SHORT_PREAMBLE,108,2,15,5000,0,100,60,40,40,15000,60,40,5000,5000,1,NULL);	
      WiFiEngine_AddScanJob((int*) &sj_id, Ssid, bcast_bssid, 0, channelList, 2, 1, 0, 0, 1, NULL); //passive scan
	  DE_TRACE_STATIC4(TR_SM, "Created scan Job with id=%d and mode=%d and channel %d \n",sj_id,mode,channel);
	  WiFiEngine_TriggerScanJob(sj_id,20);
      WiFiEngine_AddScanJob((int*) &sj_id, Ssid, bcast_bssid, 1, channelList2, 2, 1, 0, 0, 1, NULL); //active scan	  
	  DE_TRACE_STATIC4(TR_SM, "Created scan Job with id=%d and mode=%d and channel %d \n",sj_id,mode,channel);
	  WiFiEngine_TriggerScanJob(sj_id,20);
	  duration = duration * 3;
	}
	else
    {
	//Add scanjob using id 0
	 WiFiEngine_ConfigureScan(SHORT_PREAMBLE,108,2,15,5000,0,100,20,20,40,15000,60,40,5000,5000,1,NULL);
    WiFiEngine_AddScanJob((int*) &sj_id, Ssid, bcast_bssid, mode, channelList, 2, 1, 0, 0, 1, NULL);
	DE_TRACE_STATIC4(TR_SM, "Created scan Job with id=%d and mode=%d and channel %d \n",sj_id,mode,channel);
	WiFiEngine_TriggerScanJob(sj_id,20);
    }
	
	if( mode==0 )
	   duration = duration * 3;

	DriverEnvironment_RegisterTimerCallback(duration, wifiEngineState.ccxState.ccx_radio_measurement_timer_id, radio_measurement_cb, FALSE);  

    return 1;
}
#endif //CCX

static int connected = FALSE;
static int interface_enabled = FALSE;
static int is_wpa_4way_final(const void *frame, size_t frame_len);

static struct data_ctx_t {
   iobject_t* connected_h;
   iobject_t* disconnecting_h;
   iobject_t* disconnected_h;
   iobject_t* ps_interface_enabled_h;
   iobject_t* ps_interface_disabled_h;
} data_ctx;

static void connected_cb(wi_msg_param_t param, void* priv)
{
   DE_TRACE_STATIC(TR_SM, "Device connected\n");
   wifiEngineState.dataReqPending = 0;   
   connected = TRUE;
  
}

  
static void disconnecting_cb(wi_msg_param_t param, void* priv)
{
   DE_TRACE_STATIC(TR_SM, "Device disconnected\n");
   connected = FALSE;
   
}


static void ps_interface_enabled_cb(wi_msg_param_t param, void* priv)
{
   DE_TRACE_STATIC(TR_SM, "power save mechanism for hic interface enabled \n");
   interface_enabled = TRUE;
  
}

static void ps_interface_disabled_cb(wi_msg_param_t param, void* priv)
{
   DE_TRACE_STATIC(TR_SM, "power save mechanism for hic interface disabled\n");
   interface_enabled = FALSE;
   wifiEngineState.dataPathState = DATA_PATH_OPENED;    
}

static void data_path_free_handlers(void)
{
   we_ind_deregister_null(&data_ctx.connected_h);
   we_ind_deregister_null(&data_ctx.disconnecting_h);
   we_ind_deregister_null(&data_ctx.disconnected_h);
   we_ind_deregister_null(&data_ctx.ps_interface_enabled_h);
   we_ind_deregister_null(&data_ctx.ps_interface_disabled_h);
}

void wei_data_unplug(void)
{
   we_ind_deregister_null(&data_ctx.connected_h);
   we_ind_deregister_null(&data_ctx.disconnecting_h);
   we_ind_deregister_null(&data_ctx.disconnected_h);
   we_ind_deregister_null(&data_ctx.ps_interface_enabled_h);
   we_ind_deregister_null(&data_ctx.ps_interface_disabled_h);
}
void wei_data_plug(void)
{

   DE_TRACE_STATIC(TR_SM, "State Init\n");

   connected = FALSE;
   interface_enabled = FALSE;   
   data_ctx.connected_h = NULL;
   data_ctx.disconnecting_h = NULL;
   data_ctx.disconnected_h = NULL;
   data_ctx.ps_interface_enabled_h = NULL;
   data_ctx.ps_interface_disabled_h = NULL;   

  data_ctx.connected_h = we_ind_register(
            WE_IND_80211_CONNECTED,
            "WE_IND_80211_CONNECTED",
            connected_cb,
            NULL,
            0,
            NULL);
   DE_ASSERT(data_ctx.connected_h != NULL);

   data_ctx.disconnecting_h = we_ind_register(
            WE_IND_80211_DISCONNECTING,
            "WE_IND_80211_DISCONNECTING",
            disconnecting_cb,
            NULL,
            0,
            NULL);   
   DE_ASSERT(data_ctx.disconnecting_h != NULL);

   data_ctx.disconnected_h = we_ind_register(
            WE_IND_80211_IBSS_DISCONNECTED,
            "WE_IND_80211_IBSS_DISCONNECTED",
            disconnecting_cb,
            NULL,
            0,
            NULL); 
   DE_ASSERT(data_ctx.disconnected_h != NULL);

   data_ctx.ps_interface_enabled_h= we_ind_register(
            WE_IND_HIC_PS_INTERFACE_ENABLED,
            "WE_IND_HIC_PS_INTERFACE_ENABLED",
            ps_interface_enabled_cb,
            NULL,
            0,
            NULL); 
   DE_ASSERT(data_ctx.ps_interface_enabled_h != NULL);

   data_ctx.ps_interface_disabled_h = we_ind_register(
            WE_IND_HIC_PS_INTERFACE_DISABLED,
            "WE_IND_HIC_PS_INTERFACE_DISABLED",
            ps_interface_disabled_cb,
            NULL,
            0,
            NULL);
   DE_ASSERT(data_ctx.ps_interface_disabled_h != NULL);
}

static m80211_std_rate_encoding_t
convert_rate(m80211_std_rate_encoding_t rate)
{
   int status;

   if(rate & 0x80) {
      /* this is reported as a rate index in the lower 7 bits */
      uint32_t linkspeed;
      status = WiFiEngine_GetRateIndexLinkspeed(rate & 0x7f, &linkspeed);
      if(status != WIFI_ENGINE_SUCCESS) {
         DE_TRACE_INT2(TR_DATA, "GetRateIndexLinkspeed(%u) failed (%d)\n", 
                       rate, status);
         return 2; /* XXX what to do? */
      }
      if(linkspeed == 65000)
         return 127; /* can't be represented, use max representable
                      * rate for now */
      return linkspeed / 500; /* kbit/s -> 500kbit/s */
   } else {
      /* this is the reported as a bg rate code in 500 kbit/s units,
       * with HT rates as 0 */
#ifdef WORKAROUND_MISSING_DATA_HT_RATE_SUPPORT
      if(rate == 0)
         /* ht rates get reported as 0; silently treat this
          * as 63.5 Mbit/s (which is not a valid HT rate, but
          * is the max value supported by the BG rate code
          * system)
          */
         return 127; 
#endif
      return rate;
   }
}

/*!
 * @brief Process a raw received packet
 *
 * Takes a raw packet, strips the local transport
 * header and possibly unencrypts the packet.
 *
 * @param pkt Input buffer (raw packet)
 * @param pkt_len Length of the input buffer (in bytes)
 * @param stripped_pkt Pointer to the packet with the
 *  transport header stripped.
 * @param stripped_pkt_len Length of the stripped packet.
 * @param vlanid_prio VLAN ID and 802.1p priority value
 * using following format:
 * <PRE>
 *        1
 *  5|432109876543|210
 *  -+------------+---
 *  0|   VLANID   |PRI
 * </PRE>
 *
 * @param transid Output buffer for the transaction ID of the packet.
 * @return 
 * - WIFI_ENGINE_SUCCESS on success,
 * - WIFI_ENGINE_SUCCESS_ABSORBED if the packet was consumed by WiFiEngine (it was a command)
 * - WIFI_ENGINE_SUCCESS_DATA_CFM if the packet was a data confirm
 * - WIFI_ENGINE_FAILURE_NOT_ACCEPTED if the packet should be dropped (such as when
 *  the 802.1X port is closed and the frame is not EAPOL)
 * - WIFI_ENGINE_FAILURE if the header was malformed or if WiFiEngine is in a bad state.
 */
int   WiFiEngine_ProcessReceivedPacket(char *pkt, size_t pkt_len,
                                       char **stripped_pkt, 
                                       size_t *stripped_pkt_len,
                                       uint16_t *vlanid_prio,
                                       uint32_t *transid) 
{
   typedef struct
   {
      struct wrapper_alloc_buf hdr;
      mac_msg_t         buffer;
   } static_msg_t;
   
   static_msg_t          static_msg;
   hic_message_context_t msg_ref;
   int      status = WIFI_ENGINE_SUCCESS_ABSORBED;
   Blob_t   blob;
   int      use_static_buffer;
   int      skip_wrappers;

   char *hic_payload;
   size_t hic_size;
#if (DE_CCX == CFG_INCLUDED)
   char channel;
   int duration;
   char active;
#endif

   DE_TRACE_DO(TR_DATA, print_pkt_hdr(pkt, pkt_len));  

   if (TRACE_ENABLED(TR_DATA_DUMP))
   {
      DE_TRACE_DATA(TR_DATA_DUMP, "RX Data: ", pkt, pkt_len);      
   }
   else
   {    
      DE_TRACE3(TR_DATA_HIGH_RES, "RX pkt is at %p, len " TR_FSIZE_T "\n", 
                pkt, TR_ASIZE_T(pkt_len));
   }

#ifdef USE_IF_REINIT
   WEI_ACTIVITY();
#endif

   if(WiFiEngine_isCoredumpEnabled())
   {
      WiFiEngine_HandleCoreDumpPkt(pkt);
      return WIFI_ENGINE_SUCCESS_ABSORBED;
   }

   BAIL_IF_UNPLUGGED;

#define MIN_HIC_HEADER_SIZE 6
   if(pkt_len < MIN_HIC_HEADER_SIZE) {
      DE_TRACE_INT(TR_DATA, "short packet (" TR_FSIZE_T ")\n", 
                   TR_ASIZE_T(pkt_len));
      return WIFI_ENGINE_FAILURE;
   }

   /* We abuse the MessageContext in this function, it is never initialized
    * and never freed (since indata is freed elsewhere and the unpacked
    * message is a local buffer that is copied in wei_sm_queue_sig()).
    */
   /* Mlme_CreateMessageContext(msg_ref);  */
   
   msg_ref.raw = NULL;
   msg_ref.raw_size = sizeof(mac_msg_t);
   
     /* Catch HIC header and add type/id info to msg_ref */
   msg_ref.msg_type     = HIC_MESSAGE_TYPE(pkt) & ~MAC_API_MSG_DIRECTION_BIT;
   msg_ref.msg_id       = HIC_MESSAGE_ID(pkt);
   msg_ref.packed       = pkt;
   msg_ref.packed_size  = pkt_len;


   hic_payload = pkt + HIC_MESSAGE_HDR_SIZE(pkt);
   hic_size = pkt_len - HIC_MESSAGE_HDR_SIZE(pkt) - HIC_MESSAGE_PADDING_GET(pkt);
   if(hic_size >= pkt_len) {
      DE_TRACE_STATIC(TR_DATA, "malformed hic message\n");
      return WIFI_ENGINE_FAILURE;
   }

   /* Detect messages that do not need a dynamically allocated buffer. */
   use_static_buffer = TRUE;
   skip_wrappers     = TRUE;
   switch  (msg_ref.msg_type)
   {
      case HIC_MESSAGE_TYPE_DLM:
         skip_wrappers = FALSE;
         break;

      case HIC_MESSAGE_TYPE_MIB:
         skip_wrappers = FALSE;
         break;
         
      case HIC_MESSAGE_TYPE_DATA:
         switch (msg_ref.msg_id)
         {
            case HIC_MAC_DATA_CFM :
            {
               mac_api_transid_t          trans_id;
               m80211_std_rate_encoding_t rate_used;
               
               if(hic_size < MAC_DATA_CFM_MSG_SIZE) {
                  DE_TRACE_INT(TR_DATA, "runt data_cfm (" TR_FSIZE_T ")\n", 
                               TR_ASIZE_T(hic_size));
                  return WIFI_ENGINE_FAILURE;
               }
               trans_id    = HIC_DATA_CFM_GET_TRANS_ID(hic_payload);
               rate_used   = HIC_DATA_CFM_RATE_USED(hic_payload);

               rate_used = convert_rate(rate_used);
              
               DE_ASSERT(wifiEngineState.dataReqPending > 0 && 
			                wifiEngineState.dataReqPending <= wifiEngineState.txPktWindowMax);

               WIFI_LOCK();              
               wifiEngineState.dataReqPending--;
               {
                  unsigned int prio;
                  prio = HIC_DATA_CFM_RATE_PRIO(hic_payload);
                  prio &= 7;
                  DE_ASSERT(wifiEngineState.dataReqByPrio[prio] > 0);
                  wifiEngineState.dataReqByPrio[prio]--;
               }
             

               if ((wifiEngineState.dataReqPending == 0)&&(interface_enabled))
               {  
                  WIFI_UNLOCK();                   
                  wei_release_resource_hic(RESOURCE_USER_DATA_PATH);   
               }
               else
               {
                  WIFI_UNLOCK();  
               }
               
               DE_TRACE_INT2(TR_PS, "HIC_MAC_DATA_CFM received (trans id %d, data req pending %d)\n", 
                           			     trans_id, wifiEngineState.dataReqPending);
                             
#if (DE_CCX == CFG_INCLUDED)
               wei_ccx_metrics_on_cfm(trans_id);
#endif
               /* For Michael MIC countermeasures we need to deauthenticate
                * after sending a EAPOL frame. Detect the send completion
                * of that frame here. */
               if (WES_TEST_FLAG(WES_FLAG_ASSOC_BLOCKED))
               {
                  
                  DE_TRACE_STATIC(TR_ASSOC, "ASSOC_BLOCKED set\n");
                  DE_TRACE_INT2(TR_DATA, "DATA_CFM trans_id %d, save trans_id %d\n", 
                                          trans_id, wifiEngineState.last_eapol_trans_id);
                  log_transid(trans_id);
                  
                  /* Was the last EAPOL frame confirmed? */
                  if (wei_is_hmg_auto_mode() && 
                      wifiEngineState.last_eapol_trans_id <= trans_id)
                  {
                     DE_TRACE_STATIC(TR_AUTH, "Deauthenticating due to Michael countermeasures.\n");
                     WiFiEngine_Deauthenticate();
                  }
               }
               else
               {
                  wifiEngineState.current_tx_rate = rate_used;
                  log_transid(trans_id);
               }
               
               if (transid)
               {
                  *transid = trans_id;
               }

               wei_ratemon_notify_data_cfm(rate_used);
               /* Notify roaming agent that tx rate might have changed */
               wei_roam_notify_data_cfm(rate_used);
               
#if (DE_CCX == CFG_INCLUDED)
               //DE_TRACE_STATIC(TR_ALWAYS, "NIKS: ccx_handle_wakeup called from data_cfm!\n");
               ccx_handle_wakeup();
#endif
               /* Execute state machine to take care of any queued message. */
               wei_sm_execute();  

               return (WIFI_ENGINE_SUCCESS_DATA_CFM);
            }

            case HIC_MAC_DATA_IND:
            {
               mac_api_transid_t          trans_id;
               m80211_std_rate_encoding_t rate_used;

               wifiEngineState.ps_data_ind_received = TRUE;
#define MAC_DATA_IND_SIZE 8
               if(hic_size < MAC_DATA_IND_SIZE) {
                  DE_TRACE_INT(TR_DATA, "runt data_ind (" TR_FSIZE_T ")\n", 
                               TR_ASIZE_T(hic_size));
                  return WIFI_ENGINE_FAILURE;
               }
               trans_id  = HIC_DATA_IND_GET_TRANS_ID(hic_payload);
               rate_used = HIC_DATA_IND_RATE_USED(hic_payload);

               rate_used = convert_rate(rate_used);

#ifdef WIFI_SINK_DATA
               return (WIFI_ENGINE_SUCCESS_ABSORBED);
#endif /* WIFI_SINK_DATA */

               if (transid)
               {
                  *transid = trans_id;
               }
               
               *vlanid_prio      = HIC_DATA_REQ_GET_VLANID_PRIO(hic_payload);
               *stripped_pkt     = HIC_DATA_IND_PAYLOAD_BUF(hic_payload);
               *stripped_pkt_len = HIC_DATA_IND_PAYLOAD_SIZE(hic_size);
               
               status = WIFI_ENGINE_SUCCESS_DATA_IND;

               /* assume no SNAP headers */
               if (is_eapol_packet((unsigned char*)*stripped_pkt,
                                   *stripped_pkt_len)) 
               {
                  if(wifiEngineState.eapol_handler != NULL) {
                     /* XXX This is bogus, we need a handle to the
                     complete packet for this to work, not just the
                     header. Luckily, on platforms that matter this
                     is the same. */
                     if((*wifiEngineState.eapol_handler)(*stripped_pkt, 
                         *stripped_pkt_len))
                     status = WIFI_ENGINE_SUCCESS_ABSORBED;
                  }
               } 
#if (DE_CCX == CFG_INCLUDED)
				else if (is_ccx_beacon_req_packet((unsigned char*)*stripped_pkt,
                                   *stripped_pkt_len, &channel, &duration, &active))
				{
					HandleRadioMeasurementReq(channel, duration, active);		
				}
#endif
               else if (WiFiEngine_Is8021xPortClosed())
               {
                  DE_TRACE_STATIC(TR_DATA, "RX: 802.1x port closed. Discarding packet.\n");
                  status = WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
               }

               wifiEngineState.current_rx_rate = rate_used;
               wei_ratemon_notify_data_ind(rate_used);
               /* Notify roaming agent that tx rate might have changed */
               wei_roam_notify_data_ind();

               return (status);
            }

            default:
               DE_BUG_ON(1, "Bad data message received in ProcRxData()\n");
               return (WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED);
         }
         break;
         
      case HIC_MESSAGE_TYPE_CTRL:
         break;

      case HIC_MESSAGE_TYPE_ECHO:
         DriverEnvironment_indicate(WE_IND_MAC_ECHO_REPLY,
                                    hic_payload, hic_size);
         /* instead of unpacking the normal way, we bail out here */
         if (msg_ref.msg_id & MAC_API_PRIMITIVE_TYPE_CFM) {
            wifiEngineState.cmdReplyPending = 0;
            DE_TRACE_STATIC(TR_SM_HIGH_RES, "Setting cmdReplyPending to 0\n");
            wei_send_cmd_raw(NULL, 0);
         }
         return WIFI_ENGINE_SUCCESS_ABSORBED;
         
      case HIC_MESSAGE_TYPE_MGMT:
      default:
         use_static_buffer = FALSE;
         skip_wrappers     = FALSE;
         break;
   }

   if (use_static_buffer)
   {
      static_msg.hdr.next_in_chain = NULL;
      static_msg.hdr.this_buf_size = msg_ref.raw_size;
      msg_ref.raw = &static_msg.buffer;
   }
   else
   {
      /* Allocate a dynamic buffer for any kind of HIC message. */     
      msg_ref.raw = (mac_msg_t*)WrapperAllocStructure(NULL, msg_ref.raw_size);

      if(msg_ref.raw == NULL)
      {
         return WIFI_ENGINE_FAILURE_RESOURCES;
      }
   }

   if (skip_wrappers)
   {
      DE_ASSERT(hic_size <= msg_ref.raw_size);
      DE_MEMCPY(msg_ref.raw, hic_payload, hic_size);
   }
   else
   {
      INIT_BLOB(&blob, hic_payload, hic_size); 
      if (packer_Unpack(&msg_ref, &blob) == FALSE) /* Unpack the level 2 header */
      {
         DBG_PRINTBUF("Bad packet : ", (unsigned char *)pkt, pkt_len);
         goto Cleanup;
      }
   }

   switch (msg_ref.msg_type)
   {
      case HIC_MESSAGE_TYPE_MIB:
         wei_mib_schedule_cb(msg_ref.msg_id, (hic_interface_wrapper_t *)msg_ref.raw); 
         switch (msg_ref.msg_id)
         {
            case MLME_GET_CFM:
            case MLME_SET_CFM:
               wei_queue_mib_reply(msg_ref.msg_id, &msg_ref);
               break;
         }
         WiFiEngine_DispatchCallbacks();
         break;
            
      case HIC_MESSAGE_TYPE_MGMT:
         DE_TRACE_STATIC2(TR_SM,"Management packet received with msg_ref.msg_id=%d\n",msg_ref.msg_id);
         switch(msg_ref.msg_id)
         {
            case MLME_SET_KEY_CFM:
            case MLME_DELETE_KEY_CFM:
            case MLME_SET_PROTECTION_CFM:
               /* ignore */
               break;

            /* Scans are handled separately */
            case MLME_SCAN_IND :
            {
               wei_handle_scan_ind(&msg_ref);
            }
            break;
            case MLME_SCAN_CFM:
            {
               wei_handle_scan_cfm(&msg_ref);
            }
            break;
            case MLME_POWER_MGMT_CFM:
            case NRP_MLME_WMM_PS_PERIOD_START_CFM:
            {
               /* Let the driver act upon the new message. */
               wei_sm_queue_sig(wei_message2signal(msg_ref.msg_id, msg_ref.msg_type, MGMT_MESSAGE_RX), SYSDEF_OBJID_HOST_MANAGER_PS, wei_wrapper2param(msg_ref.raw), FALSE);
               wei_sm_execute();
            }
            break;

            case NRP_MLME_SETSCANPARAM_CFM:
            {
               wei_handle_set_scan_param_cfm(&msg_ref);
            }
            break;
            case NRP_MLME_ADD_SCANFILTER_CFM:
            {
               wei_handle_add_scan_filter_cfm(&msg_ref);
            }
            break;
            case NRP_MLME_REMOVE_SCANFILTER_CFM:
            {
               wei_handle_remove_scan_filter_cfm(&msg_ref);
            }
            break;
            case NRP_MLME_ADD_SCANJOB_CFM:
            {
               wei_handle_add_scan_job_cfm(&msg_ref);
            }
            break;
            case NRP_MLME_REMOVE_SCANJOB_CFM:
            {
               wei_handle_remove_scan_job_cfm(&msg_ref);
            }
            break;
            case NRP_MLME_SET_SCANJOBSTATE_CFM:
            {
               wei_handle_set_scan_job_state_cfm(&msg_ref);
            }
            break;
            case NRP_MLME_GET_SCANFILTER_CFM:
            {
               wei_handle_get_scan_filter_cfm(&msg_ref);
            }
            break;
            case NRP_MLME_SCANNOTIFICATION_IND:
            {
               wei_handle_scan_notification_ind(&msg_ref);
            }
            break;
            case NRP_MLME_SET_SCANCOUNTRYINFO_CFM:
            {
               wei_handle_set_scancountryinfo_cfm(&msg_ref);
            }
            break;

#if (DE_CCX == CFG_INCLUDED)
            case NRP_MLME_ADDTS_CFM:
            {
                Mlme_HandleADDTSConfirm(&msg_ref);
            }
            break;
            case NRP_MLME_ADDTS_IND:
            {
                HandleAddtsResponsePacket(pkt,pkt_len); //FIXME: this needs to receive &msg_ref as a parameter
            }
            break;
            case NRP_MLME_DELTS_CFM:
            {
                Mlme_HandleDeltsConfirm(&msg_ref);
            }
            break;
            case NRP_MLME_GET_FW_STATS_CFM:
            {
                Mlme_HandleFWStatsCfm(&msg_ref);
            }
            break;
#endif //DE_CCX
            default :
            {
               wei_sm_queue_sig(wei_message2signal(msg_ref.msg_id, msg_ref.msg_type, MGMT_MESSAGE_RX), SYSDEF_OBJID_HOST_MANAGER_TRAFFIC,wei_wrapper2param(msg_ref.raw) , FALSE);
               /* Let the driver act upon the new message. */
               wei_sm_execute();
            }
            break;
         }
         WiFiEngine_DispatchCallbacks();
         break;
         
      case HIC_MESSAGE_TYPE_CTRL:
         switch (msg_ref.msg_id)
         {
            case HIC_CTRL_SET_ALIGNMENT_CFM:
               /* ignore */
               break;
            
            case HIC_CTRL_INIT_COMPLETED_CFM:
               Mlme_HandleHICInitCompleteConfirm((hic_ctrl_init_completed_cfm_t *)msg_ref.raw);
               break;
            case HIC_CTRL_HEARTBEAT_IND:
            {
               wifiEngineState.hb_rx_ts = DriverEnvironment_GetTicks();
               /* To do: free pkt ? */
            }
            break;

            case HIC_CTRL_SCB_ERROR_IND:
            {
               WiFiEngine_StartCoredump();
            }
            break;
            case HIC_CTRL_HEARTBEAT_CFM:
               wifiEngineState.hb_rx_ts = DriverEnvironment_GetTicks();               
               break;
            
            case HIC_CTRL_WAKEUP_IND:
            {
               Mlme_HandleHICWakeupInd((hic_ctrl_wakeup_ind_t *)msg_ref.raw);
               if(WiFiEngine_IsReasonHost((hic_ctrl_wakeup_ind_t *)msg_ref.raw))
               {               
                  we_ind_send(WE_IND_PS_WAKEUP_IND,NULL);
               }
            }
            break;

            case HIC_CTRL_HL_SYNC_CFM:
            {
               hic_ctrl_hl_sync_cfm_t *cfm;

               cfm = (hic_ctrl_hl_sync_cfm_t*)msg_ref.raw;
               DriverEnvironment_indicate(WE_IND_MAC_TIME, cfm, 0);
               break;
            }
               
            default:
            {               
               wei_sm_queue_sig(wei_message2signal(msg_ref.msg_id, msg_ref.msg_type, MGMT_MESSAGE_RX), SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, wei_wrapper2param(msg_ref.raw), FALSE);
               /* Let the driver act upon the new message. */
               wei_sm_execute();
            }
            break;
         }
         WiFiEngine_DispatchCallbacks();
         break;
      case HIC_MESSAGE_TYPE_CONSOLE:
#define CONS_ENQUEUE(N)                                   \
      case HIC_MAC_##N:                                   \
         DE_TRACE_STATIC(TR_CONSOLE, "Got " #N "\n");     \
         wei_queue_console_reply(pkt, (size_t)pkt_len);   \
         break

         switch (msg_ref.msg_id)
         {
            CONS_ENQUEUE(CONSOLE_CFM);
            CONS_ENQUEUE(CONSOLE_IND);
            default:
               DE_BUG_ON(1, "Got unknown CONSOLE message\n");
               break;
         }
         WiFiEngine_DispatchCallbacks();
         break;
      /* We can treat these as console commands, as WiFiEngine
       * doesn't really look at them anyway. This means that it's
       * impossible to get a reply to a specific flash request,
       * instead you get the first available reply from either a
       * flash or console command, but in practice this doesn't
       * matter. */
      case HIC_MESSAGE_TYPE_FLASH_PRG:
         switch (msg_ref.msg_id) {
            CONS_ENQUEUE(START_PRG_CFM);
            CONS_ENQUEUE(WRITE_FLASH_CFM);
            CONS_ENQUEUE(END_PRG_CFM);
            CONS_ENQUEUE(START_READ_CFM);
            CONS_ENQUEUE(READ_FLASH_CFM);
            CONS_ENQUEUE(END_READ_CFM);
            default:
               DE_BUG_ON(1, "Got unknown FLASH_PRG message\n");
               break;
         }
         WiFiEngine_DispatchCallbacks();
#undef CONS_ENQUEUE
         break;
      case HIC_MESSAGE_TYPE_AGGREGATION:
         *stripped_pkt = hic_payload;
         *stripped_pkt_len = hic_size;
         status = WIFI_ENGINE_SUCCESS_AGGREGATED;

         DE_ASSERT(*stripped_pkt_len <= pkt_len);

         break;

      case HIC_MESSAGE_TYPE_DLM:
         wei_handle_dlm_pkt(&msg_ref);
         status = WIFI_ENGINE_SUCCESS_ABSORBED;
         break;

      default:
         DE_BUG_ON(1, "Unexpected message header type encountered in WiFiEngine_ProcessReceivedPacket()\n");
         status = WIFI_ENGINE_FAILURE;
   }

   /* DLM messages are sent unconditional(not using command queue) and should
      not trigg sending of any messages in command queue */
   if ((msg_ref.msg_id & MAC_API_PRIMITIVE_TYPE_CFM)
       && msg_ref.msg_type != HIC_MESSAGE_TYPE_DLM)
   {
      wifiEngineState.cmdReplyPending = 0;
      DE_TRACE_STATIC(TR_SM_HIGH_RES, "Setting cmdReplyPending to 0\n");
      wei_send_cmd_raw(NULL, 0);
   }

   /* Execute state machine to take care of any queued message. */
   wei_sm_execute();  
      
Cleanup:
   if ( (msg_ref.raw != NULL) && (msg_ref.raw != &static_msg.buffer) )
   {
      WrapperFreeStructure(msg_ref.raw);
   }

   return status;
}

/******************************************************************************/
/* functions to help debug and gather stat's from WiFiEngine_ProcessSendPacket*/
/******************************************************************************/

static struct {
   int not_connected;
   int coredump;
   int lock;
   int ps;
   int data_queue_full;
   int data_path;
} send_failure_status = {0,0,0,0,0,0};

int WiFiEngine_DebugStatsProcessSendPacket(int status)
{
   // this is for subcategories of WIFI_ENGINE_FAILURE_RESOURCES
   switch(status) { 
      case WIFI_ENGINE_FAILURE_NOT_CONNECTED:
         return send_failure_status.not_connected++;
      case WIFI_ENGINE_FAILURE_COREDUMP:
         return send_failure_status.coredump++;
      case WIFI_ENGINE_FAILURE_LOCK:
         return send_failure_status.lock++;
      case WIFI_ENGINE_FAILURE_PS:
         return send_failure_status.ps++;
      case WIFI_ENGINE_FAILURE_DATA_PATH:
         return send_failure_status.data_path++;
      case WIFI_ENGINE_FAILURE_DATA_QUEUE_FULL:
         return send_failure_status.data_queue_full++;
   }
   /* ignored */
   return 0;
}

void WiFiEngine_DebugMsgProcessSendPacket(int status)
{
   // this is for subcategories of WIFI_ENGINE_FAILURE_RESOURCES
   switch(status) { 
      case WIFI_ENGINE_FAILURE_NOT_CONNECTED:
         DE_TRACE_STATIC(TR_DATA, "FAILURE: SendPacket not connected\n");
         break;
      case WIFI_ENGINE_FAILURE_COREDUMP:
         //DE_TRACE_STATIC(TR_WEI, "Coredump is enabled queue data and wait for reset\n");
         DE_TRACE_STATIC(TR_DATA, "FAILURE: SendPacket coredump\n");
         break;
      case WIFI_ENGINE_FAILURE_LOCK:
         //DE_TRACE_STATIC(TR_SEVERE, "Failed to acquire wifiEngineState.send_lock\n");
         DE_TRACE_STATIC(TR_DATA, "FAILURE: SendPacket send_lock\n");
         break;
      case WIFI_ENGINE_FAILURE_PS:
         DE_TRACE_STATIC(TR_DATA, "FAILURE: SendPacket PS is prevening it\n");
         break;
      case WIFI_ENGINE_FAILURE_DATA_PATH:
         DE_TRACE_STATIC(TR_DATA, "FAILURE: SendPacket data path is closed\n");
         break;
      case WIFI_ENGINE_FAILURE_DATA_QUEUE_FULL:
         DE_TRACE_STATIC(TR_DATA, "FAILURE: SendPacket data path is maxed out\n");
         break;
   }
}
/******************************************************************************/

/*!
 * @brief Process a packet for sending
 * 
 * Takes a Ethernet II frame header and generates a message passing header for it.
 * The ethernet header is assumed to have the following layout :
 * <dst addr:6><src addr:6><type:2>...
 * The rest of the ethernet header buffer (if any) is ignored.
 * 
 * @param eth_hdr Input buffer (ethernet header)
 * @param eth_hdr_len Input buffer length (must be >= 14)
 * @param pkt_len Length of the complete data packet (in bytes)
 * @param hdr Pointer to the header buffer (must be
 * allocated by the caller)
 * @param hdr_len_p The length of the allocated hdr buffer
 * (that is the length allocated by the caller).
 * @param vlanid_prio VLAN ID and 802.1p priority value
 * using following format:
 * <PRE>
 *        1
 *  5|432109876543|210
 *  -+------------+---
 *  0|   VLANID   |PRI
 * </PRE>
 * Ignored for legacy association (no WMM)
 * @param trans_id_p Output buffer for the data transaction ID (can be NULL).
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS
 * - WIFI_ENGINE_FAILURE_INVALID_LENGTH if the input buffer size
 * was invalid (too small or too big).
 * - WIFI_ENGINE_FAILURE_RESOURCES we cannot send more data right now.
 * The packet should be retried later.
 * - WIFI_ENGINE_FAILURE_NOT_ACCEPTED if the network was not available.
 * - WIFI_ENGINE_FAILURE on general failure.
 */
int   WiFiEngine_ProcessSendPacket(char *eth_hdr, size_t eth_hdr_len, size_t pkt_len, char *hdr, size_t* hdr_len_p, uint16_t vlanid_prio, uint32_t *trans_id_p)
{
   uint16_t uapsd_enabled_for_ac;
   size_t   mac_data_hdr_size;
   uint32_t trans_id;
   char*    mac_data_hdr_p;                     
   int      pad_len;
                        
   DE_ASSERT(eth_hdr_len >= 14);

   DE_TRACE_INT(TR_DATA_HIGH_RES, "WiFiEngine_ProcessSendPacket(pkt_len: " TR_FSIZE_T "\n", 
                TR_ASIZE_T(pkt_len));
   BAIL_IF_UNPLUGGED;
   
#ifdef USE_IF_REINIT   
   WEI_ACTIVITY();
#endif

   /* Don't send until we are connected */
   if (!connected)
   {
      return WIFI_ENGINE_FAILURE_NOT_CONNECTED;
   }
   
   if(WiFiEngine_isCoredumpEnabled())
   {
      return WIFI_ENGINE_FAILURE_COREDUMP;
   }  

   WIFI_LOCK();
   if (DriverEnvironment_acquire_trylock(&wifiEngineState.send_lock) == LOCK_LOCKED)
   {
      WIFI_UNLOCK();
      return WIFI_ENGINE_FAILURE_LOCK;
   }
      WIFI_UNLOCK();
   if (wifiEngineState.dataReqPending >= wifiEngineState.txPktWindowMax)
   {
      DE_ASSERT(wifiEngineState.dataReqPending <= 
      wifiEngineState.txPktWindowMax);
      DriverEnvironment_release_trylock(&wifiEngineState.send_lock);
      return WIFI_ENGINE_FAILURE_DATA_QUEUE_FULL;
   }

   if(interface_enabled)
   {
      if(!wei_request_resource_hic(RESOURCE_USER_DATA_PATH))
      {
         WIFI_LOCK();
         if(!wei_request_resource_hic(RESOURCE_USER_DATA_PATH))
         {               
             DriverEnvironment_release_trylock(&wifiEngineState.send_lock);    
             //  TODO: rewrite wei_request_resource_hic(...) to return reason for denial so it can be passed on
             WIFI_UNLOCK();
             return WIFI_ENGINE_FAILURE_PS;
         }
         WIFI_UNLOCK();
      }
   }
   
   WIFI_LOCK();
   trans_id = wifiEngineState.pkt_cnt;
   wifiEngineState.pkt_cnt++;
   WIFI_UNLOCK();
   if (trans_id_p)
   {
#if (DE_CCX == CFG_INCLUDED)
       /*add transaction id and timestamp to list*/
       wei_ccx_add_trans(&wifiEngineState.ccxState.metrics, trans_id);
#endif
      *trans_id_p = trans_id;
   }

   
   /* Check 802.1X port */
   if (WiFiEngine_Is8021xPortClosed())
   {
      /* Filter non-EAPOL frames */
      if (is_eapol_packet((unsigned char*)eth_hdr,eth_hdr_len))
      {
         DE_TRACE_STATIC(TR_WPA, "TX: EAPOL frame detected and port is closed. OK to send.\n");
         if (WES_TEST_FLAG(WES_FLAG_ASSOC_BLOCKED))
         {
            DE_TRACE_STATIC(TR_WPA, "ASSOC_BLOCKED-flag set and EAPOL ready to send. Setup to deauth (countermeasures) \n");
            DE_TRACE_INT(TR_WPA, "EAPOL frame tagged with trans_id %d\n", trans_id);
            wifiEngineState.last_eapol_trans_id = trans_id;
         }
      }
      else
      {
         DriverEnvironment_release_trylock(&wifiEngineState.send_lock);    
         return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
      }
   }
   /* Calculate the header size including padding. */
   mac_data_hdr_size = WiFiEngine_GetDataHeaderSize();
   if (*hdr_len_p < mac_data_hdr_size)
   {
      DE_TRACE_INT2(TR_DATA, "Invalid length (hdr_len_p " TR_FSIZE_T ", data hdr size " TR_FSIZE_T "\n", 
                    TR_ASIZE_T(*hdr_len_p), TR_ASIZE_T(mac_data_hdr_size));
      DriverEnvironment_release_trylock(&wifiEngineState.send_lock);
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   } 
  
   WIFI_LOCK();    
   if(wifiEngineState.dataPathState == DATA_PATH_CLOSED)
   {
      WIFI_UNLOCK(); 
      wei_release_resource_hic(RESOURCE_USER_DATA_PATH);
      DriverEnvironment_release_trylock(&wifiEngineState.send_lock);      
      return WIFI_ENGINE_FAILURE_DATA_PATH;
   }

   wifiEngineState.dataReqPending++;  
   wifiEngineState.dataReqByPrio[vlanid_prio & 7]++;
   DE_ASSERT(wifiEngineState.dataReqPending <=
	     wifiEngineState.txPktWindowMax);
   if(wifiEngineState.dataReqPending == wifiEngineState.txPktWindowMax)
      DriverEnvironment_indicate(WE_IND_TX_QUEUE_FULL, NULL, 0);
   WIFI_UNLOCK();  

   pad_len = WiFiEngine_GetPaddingLength(mac_data_hdr_size + pkt_len);
   HIC_MESSAGE_LENGTH_SET(hdr, mac_data_hdr_size + pkt_len + pad_len);
   HIC_MESSAGE_TYPE(hdr)         = HIC_MESSAGE_TYPE_DATA;
   HIC_MESSAGE_ID(hdr)           = HIC_MAC_DATA_REQ;
   HIC_MESSAGE_HDR_SIZE(hdr)     = wifiEngineState.config.tx_hic_hdr_size;
   HIC_MESSAGE_PADDING_SET(hdr, pad_len);
   
   uapsd_enabled_for_ac = wei_qos_get_ac_value(vlanid_prio & 0x07);

   /* Log to get WMM AC info fron the vlanid-prio that NDIS provides. */
   DE_TRACE_INT4(TR_DATA_HIGH_RES, "VLAN tag %02X (prio %d), WMM enabled:%d, U-APSD enabled for AC: %d\n", vlanid_prio, (vlanid_prio & 0x7), WiFiEngine_isWMMAssociated(), uapsd_enabled_for_ac);

   /* Setup data req header. */
   mac_data_hdr_p = &((char*)hdr)[wifiEngineState.config.tx_hic_hdr_size];

   HIC_DATA_REQ_SET_TRANS_ID(mac_data_hdr_p, trans_id);
   HIC_DATA_REQ_SET_VLANID_PRIO(mac_data_hdr_p, vlanid_prio);

   if(is_wpa_4way_final(eth_hdr, eth_hdr_len))
   {
      HIC_DATA_REQ_SET_SVC(mac_data_hdr_p, 0x0004);
   } 
   else
   {
      HIC_DATA_REQ_SET_SVC(mac_data_hdr_p, 0x0000);      
   }

   *hdr_len_p = mac_data_hdr_size;

   DE_TRACE_INT2(TR_PS_DEBUG, "HIC_MAC_DATA_REQ sent (trans id %d, dataReqPending %d)\n", 
		 trans_id, wifiEngineState.dataReqPending);

   /* DBG_PRINTBUF("WiFiEngine_ProcessSendPacket() got packet ", (unsigned char *)pkt, pkt_len); */

   DriverEnvironment_release_trylock(&wifiEngineState.send_lock);
   /*Set timestamp (cmd time-out)*/
   WEI_CMD_TX();
   if(registry.network.basic.cmdTimeout)
   {
      WiFiEngine_CommandTimeoutStart();
   }
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Return the minimum size of a local transport header.
 *
 * @return 
 * - Returns the length of the header in bytes
 */
uint16_t wei_get_hic_hdr_min_size()
{
   int                           size;
   Blob_t                        blob;
   m80211_mlme_host_header_t     m80211_mlme_host_header;
   
   MEMSET(&m80211_mlme_host_header, 0, sizeof(m80211_mlme_host_header_t));
   
   /* Calculate the header size including padding. */
   INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
   HicWrapper_m80211_mlme_host_header_t(&m80211_mlme_host_header, &blob, ACTION_GET_SIZE);
   size = BLOB_CURRENT_SIZE(&blob);

   return size;
}

/*!
 * @brief Return the size of the mac data req header.
 *
 * @return 
 * - Returns the length of the header in bytes
 */
#if 0
uint16_t wei_get_data_req_hdr_size()
{
   static uint16_t result = 0;
   Blob_t blob;
   m80211_mac_data_req_header_t m80211_mac_data_req_header;

   if(result == 0) {
      /* Calculate the Data Request header size. */
      INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
      HicWrapper_m80211_mac_data_req_header_t(&m80211_mac_data_req_header, 
                                              &blob, 
                                              ACTION_GET_SIZE);
      result = BLOB_CURRENT_SIZE(&blob);
   }
   return result;
}
#endif

/*!
 * @brief Return the size of a local data header
 *
 * @return 
 * - Returns the length of the header in bytes
 */
uint16_t WiFiEngine_GetDataHeaderSize()
{
   return wei_get_data_req_hdr_size() + wifiEngineState.config.tx_hic_hdr_size;
}

/*!
 * @brief Return required padding for given size of packet.
 */
size_t 
WiFiEngine_GetPaddingLength(size_t data_packet_size)
{
   size_t nr_bytes_added;
   if (data_packet_size < wifiEngineState.config.min_pdu_size)
   {
      nr_bytes_added = wifiEngineState.config.min_pdu_size - data_packet_size;
   }
   else
   {
      nr_bytes_added = DE_SZ_ALIGN(data_packet_size, wifiEngineState.config.pdu_size_alignment) - data_packet_size;
   }     
   return nr_bytes_added;
}


/* @brief Detect final WPA 4-way handshake message.
 * 
 * We need to delay the final 4-way handshake message until we've had
 * time to set the keys, but we also can't set the keys before we send
 * the message (since then it would be encrypted). This is solved by
 * delaying the frame in the firmware until encryption is enabled (for
 * a maximum of 50ms). 
 *
 * @param [in] frame       points to ethernet frame
 * @param [in] frame_len   size of ethernet frame
 *
 * @retval TRUE if frame is the final 4-way handshake message
 * @retval FALSE if frame is not the final 4-way handshake message
 */
/* The reason this doesn't live in WiFiEngine (which would make it
 * work automagically on all platforms), is that ProcessSendPacket
 * doesn't conceptually take a complete frame, only the ethernet
 * header. */
static int
is_wpa_4way_final(const void *frame, size_t frame_len)
{
   int i;
   const unsigned char *pkt = (const unsigned char *)frame;

   /* Ethernet frame layout (WAPI):
    * [offset][size]
    * [ 0][ 6] dst address
    * [ 6][ 6] src address
    * [12][ 2] ether type       (88b4)
    * [14][ 2] protocol version (1)
    * [16][ 1] packet type      (1: WAI Protocol Message)
    * [17][ 1] packet subtype   (0x0c: multicast response)
    * [18][ 2] reserved
    * [20][ 2] packet length
    * more stuff follows...
    */
   if(pkt[12] == 0x88 && pkt[13] == 0xb4) { /* WAPI ethernet frame */   
     if(frame_len < 3+12+16+20) /* flags + addid + key + mic */
       return FALSE;
     if(pkt[16] != 1)
       return FALSE;
   } else {  
     /* Ethernet frame layout (WPA):
      * [offset][size]
      * [ 0][ 6] dst address
      * [ 6][ 6] src address
      * [12][ 2] ether type       (888e)
      * [14][ 1] protocol version (1)
      * [15][ 1] packet type      (3: EAPOL-Key)
      * [16][ 2] packet body length
      * [18][ 1] descriptor type  (2: RSN, 254: WPA)
      * [19][ 2] key information  (various flags)
      * [21][ 2] key length
      * [23][ 8] replay counter
      * [31][32] key nonce        (all zeros)
      * more stuff follows...
      */
     if(frame_len < 31 + 32)
       return FALSE;
     if(pkt[12] != 0x88 || pkt[13] != 0x8e) /* EAPOL ethernet frame */
       return FALSE;
     if(pkt[14] != 1) /* Version 1 */
       return FALSE;
     if(pkt[15] != 3) /* EAPOL-Key */
       return FALSE;
     if(pkt[18] != 2 && pkt[18] != 254)   /* descriptor type is RSN or WPA */
       return FALSE;
     if((pkt[19] & 0x1d) != 0x01 || /* MIC, !ERROR, !REQUEST, !ENCKEY */
	(pkt[20] & 0xc8) != 0x08)   /* PAIRWISE, !INSTALL, !ACK */
       return FALSE;
     for(i = 31; i < 31 + 32; i++) { /* check for zero nonce */
       if(pkt[i] != 0)
	 return FALSE;
     }
   }

   return TRUE;
}

void wei_data_init(void)
{
   DE_MEMSET(&data_ctx,0,sizeof(data_ctx));
}

void wei_data_shutdown(void)
{
   data_path_free_handlers();
}


/** @} */ /* End of we_data group */
