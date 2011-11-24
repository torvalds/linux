#include "wifi_engine_internal.h"

#if (DE_CCX == CFG_INCLUDED)

static void wei_ccx_connected(wi_msg_param_t param, void* priv)
{
   if(wifiEngineState.ccxState.SendAdjReq==1)
   {
      WiFiEngine_net_t *net;
      DE_TRACE_STATIC(TR_SM_HIGH_RES, "SendAdjacentAPreport\n");
      net = wei_netlist_get_current_net();

      WiFiEngine_SendAdjacentAPreport();
      if(wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len == net->bss_p->bss_description_p->ie.ssid.hdr.len)
      {
         if(DE_MEMCMP(&wifiEngineState.ccxState.LastAssociatedNetInfo.ssid, net->bss_p->bss_description_p->ie.ssid.ssid, wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len)==0)
         {
            wifiEngineState.ccxState.metrics.roaming_count++;
            wifiEngineState.ccxState.metrics.roaming_delay = DriverEnvironment_GetTimestamp_msec()-wifiEngineState.ccxState.LastAssociatedNetInfo.Disassociation_Timestamp;
         }
      }
      wei_copy_bssid(&wifiEngineState.ccxState.LastAssociatedNetInfo.MacAddress, &net->bssId_AP);
      wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len = net->bss_p->bss_description_p->ie.ssid.hdr.len;
      DE_MEMCPY(&wifiEngineState.ccxState.LastAssociatedNetInfo.ssid, net->bss_p->bss_description_p->ie.ssid.ssid, wifiEngineState.ccxState.LastAssociatedNetInfo.ssid_len);
      wifiEngineState.ccxState.LastAssociatedNetInfo.ChannelNumber = net->bss_p->bss_description_p->ie.ds_parameter_set.channel;
      wifiEngineState.ccxState.SendAdjReq = 0;
   }
}

static void wei_ccx_connect_handler_release(void *priv)
{
   wifiEngineState.ccxState.connect_handler = NULL;
}

void wei_ccx_add_trans(ccx_metrics_t * metrics_p, uint32_t transid)
{
    trans_ts_t * trans;

    if(wifiEngineState.ccxState.metrics.collect_metrics_enabled)
    {
        trans = (trans_ts_t*)DriverEnvironment_Malloc(sizeof(trans_ts_t));

        trans->start_ts = DriverEnvironment_GetTimestamp_msec();
        trans->transid  = transid;
        trans->next = metrics_p->trans_ts_list;

        metrics_p->trans_ts_list = trans;

        DE_TRACE_STATIC4(TR_SM_HIGH_RES, "NIKS: new transaction 0x%x (id=%u) added at top of list, ts=%u\n", (unsigned int)trans, transid, trans->start_ts);
    }
/*
    if(metrics_p->trans_ts_list == NULL)
    {
        metrics_p->trans_ts_list = trans;
    }
    else
    {
        (wei_net_p2p_find_last_peer(p2p_p))->next = trans;
    }
*/
}

#if 0
trans_ts_t * wei_ccx_find_trans(trans_ts_t * head, uint32_t transid)
{
    trans_ts_t * cur_trans = head;

    while(cur_trans)
    {
        if(transid == cur_trans->transid)
        {
            break;
        }
        cur_trans = cur_trans->next;
    }
    return cur_trans;
}

trans_ts_t * wei_ccx_find_last_trans(trans_ts_t * head)
{
    trans_ts_t * cur_trans = head;

    while(cur_trans->next)
    {
        cur_trans = cur_trans->next;
    }
    return cur_trans;
}
#endif

/* wei_net_p2p_remove_peer()
 *
 * params:
 *     p2p_p (in): the network where the p2p peers are attached
 *     mac (in): address of the peer to be removed
 *     addr_type (in): type of passed address, p2p device address or p2p interface address
 * returns:
 *     a pointer to the found (and removed from the linked list) p2p device struct,
 *       so that its memory can be freed
 */
static trans_ts_t * wei_ccx_remove_trans_from_list(ccx_metrics_t * metrics_p, uint32_t transid)
{
    trans_ts_t * cur_trans  = metrics_p->trans_ts_list;
    trans_ts_t * prev_trans = NULL;

    while(cur_trans)
    {
        if(transid == cur_trans->transid)
        {
            if(!prev_trans)   /*found device at beginning of list*/
            {
                metrics_p->trans_ts_list = cur_trans->next;   /*reset list head to the next element*/
            }
            else
            {
                prev_trans->next = cur_trans->next;
            }
            break;
        }
        else
        {
            prev_trans = cur_trans;
            cur_trans  = cur_trans->next;
        }
    }
    return cur_trans;
}

void wei_ccx_metrics_on_cfm(uint32_t transid)
{
    uint16_t     delay;
    int          index;

    trans_ts_t * trans = wei_ccx_remove_trans_from_list(&wifiEngineState.ccxState.metrics, transid);

    if(trans)
    {
        delay = (uint16_t) (DriverEnvironment_GetTimestamp_msec() - trans->start_ts);

        DE_TRACE_STATIC4(TR_SM_HIGH_RES, "NIKS: metrics: found transaction 0x%x: delay=%u, ts=%u\n", (unsigned int)trans, delay, DriverEnvironment_GetTimestamp_msec());

        wifiEngineState.ccxState.metrics.driver_pkt_delay_total += delay;
        wifiEngineState.ccxState.metrics.driver_pkt_cnt++;

        if(delay <= 10)
        {
            index = 0;
        }
        else if(delay > 10 && delay <= 20)
        {
            index = 1;
        }
        else if(delay > 20 && delay <= 40)
        {
            index = 2;
        }
        else
        {
            index = 3;
        }

        wifiEngineState.ccxState.metrics.uplink_packet_queue_delay_histogram[index]++;

        DriverEnvironment_Free(trans);

        /*DE_TRACE_STATIC4(TR_SM_HIGH_RES, "NIKS: metrics: total delay=%u, total pkt count=%u, histogram index=%u\n",
                         wifiEngineState.ccxState.metrics.driver_pkt_delay_total,
                         wifiEngineState.ccxState.metrics.driver_pkt_cnt,
                         index);*/
    }
}

void wei_ccx_free_trans(uint32_t transid)
{
    trans_ts_t * trans = wei_ccx_remove_trans_from_list(&wifiEngineState.ccxState.metrics, transid);

    if(trans)
    {
        DE_TRACE_STATIC3(TR_SM_HIGH_RES, "NIKS: freeing transaction 0x%x for transid=%u (no metrics collected)\n", (unsigned int)trans, transid);
        DriverEnvironment_Free(trans);
    }
}


void ccx_handle_wakeup(void)
{
    if(wifiEngineState.ccxState.resend_packet_mask)
    {
        wifiEngineState.dataPathState = DATA_PATH_OPENED;

        if(wifiEngineState.ccxState.resend_packet_mask & CCX_RDIO_MEASURE_PENDING)
        {
            DE_TRACE_STATIC(TR_ALWAYS, "NIKS:   found radio measurement flag active, retrying...\n");
            radio_measurement_cb(NULL, 0);
        }
        if(wifiEngineState.ccxState.resend_packet_mask & CCX_AP_REPORT_PENDING)
        {
            DE_TRACE_STATIC(TR_ALWAYS, "NIKS:   found adjacent AP report flag active, retrying...\n");
            WiFiEngine_SendAdjacentAPreport();
        }
        if(wifiEngineState.ccxState.resend_packet_mask & CCX_TS_METRICS_PENDING)
        {
            DE_TRACE_STATIC(TR_ALWAYS, "NIKS:   found traffic stream metrics flag active, retrying...\n");
            WiFiEngine_SendTrafficStreamMetricsReport();
        }
    }
}

void WiFiEngine_SetRequiredAdmissionCapacity(uint16_t rac)
{
    /*now a wei global, maybe in the future a per-network, per-stream var*/
    wifiEngineState.ccxState.rac = rac;
}

#endif

void
wei_ccx_plug(void)
{
#if (DE_CCX == CFG_INCLUDED)
   int i;

   wifiEngineState.ccxState.connect_handler = we_ind_register(
      WE_IND_80211_CONNECTED,
      "ccx-connected",
      wei_ccx_connected,
      wei_ccx_connect_handler_release,
      RELEASE_IND_ON_UNPLUG,
      NULL);

   i = DriverEnvironment_GetNewTimer(&wifiEngineState.ccxState.ccx_radio_measurement_timer_id, TRUE);
   DE_ASSERT(i == DRIVERENVIRONMENT_SUCCESS);

   i = DriverEnvironment_GetNewTimer(&wifiEngineState.ccxState.ccx_traffic_stream_metrics_id, TRUE);
   DE_ASSERT(i == DRIVERENVIRONMENT_SUCCESS);

   for(i=0;i<7;i++)
   {
       wifiEngineState.ccxState.addts_state[i].active = ADDTS_NOT_TRIED;
   }

   WiFiEngine_roam_enable(TRUE);

   /*FIXME: this should come from a higher layer, for testing only*/
   WiFiEngine_SetRequiredAdmissionCapacity(0x300);
#endif
}

void
wei_ccx_unplug(void)
{
#if (DE_CCX == CFG_INCLUDED)
   DriverEnvironment_FreeTimer(wifiEngineState.ccxState.ccx_radio_measurement_timer_id);
   DriverEnvironment_FreeTimer(wifiEngineState.ccxState.ccx_traffic_stream_metrics_id);
#endif
}
