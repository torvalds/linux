/* $Id: globals.c,v 1.3 2007/09/27 08:42:16 maso Exp $ */
/*****************************************************************************

Copyright (c) 2006 by Nanoradio AB

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
Contains platform independent code for simplify rf tests.
This is for FCC/ETSI-testing or production testing.
Not for lab-tuning.

*****************************************************************************/

#include "driverenv.h"
#include "adl_utils.h"
#include "mlme_proxy.h"
#include "packer.h"

/******************************************************************************/
/*                              DEFINES                                       */
/******************************************************************************/
#define TP_ProxyStatusCode_DriverError 1
#define TP_ProxyStatusCode_OK 0

/******************************************************************************/
/*                              STATIC VARIABLES                              */
/******************************************************************************/
static iobject_t *ind_handle = NULL;

static hwt_req_t current_command;

static struct {
   uint32_t qpsk_index[M80211_XMIT_RATE_NUM_RATES + 1];
   uint32_t ofdm_index[M80211_XMIT_RATE_NUM_RATES + 1];
   uint32_t packet_size;
   uint32_t pattern;
   uint32_t rx_filter_mode;

   uint32_t pwr;
   uint32_t rf_channel;
   uint32_t data_rate;
   uint32_t packet_count;
   uint32_t packet_interval;
   uint8_t bssid[M80211_ADDRESS_SIZE];
} settings;

typedef struct _rxstat {
   long ucast_to_self;
   long ucast_to_other;
   long mcast_to_self;
   long mcast_to_other;
   long ack_to_self;
   long rts_to_self;
   long cts_to_self;
   long cfend_to_self;
   long bad_crc;
   long bad_crc_ucast;
   long bad_crc_mcast;

   /* the following are not per rate */
   long ofdm_chest_mag;
   long ofdm_snr;
   long ofdm_frame_counter;

   long qpsk_snr;
   long qpsk_symbol_diff;
   long qpsk_frame_counter;
   long qpsk_freq_error;

   long ofdm_freq_error;

   long ofdm_dc_off_re;
   long ofdm_dc_off_im;
   long ofdm_dc_off_count;

   long qpsk_dc_off_re;
   long qpsk_dc_off_im;
   long qpsk_dc_off_count;
}rxstat_t;

rxstat_t rxstat;

/******************************************************************************/
/*                              STATIC DECLARATIONS                           */
/******************************************************************************/
static void handle_console_reply(wi_msg_param_t param, void* priv);
static void x_cmd_set_channel(int ch);
static void x_cmd_rxstat_get(void);
static void parse_console_reply(char *string);
static void command_result(int status, void *param);
//static char *strtok_r (char *s, const char *delim, char **save_ptr);
static m80211_xmit_rate_t hwt_convert_bitrate_into_xmit_bitrate_code(uint32_t bitrate);
static int hwt_console_request(hwt_req_t *req);
static void hwt_req_result_cb(int status, hwt_req_t *req);
static int hwt_request(hwt_req_t *req);


/******************************************************************************/
/*                              PUBLIC FUNCTIONS                              */
/******************************************************************************/

/*!
 * @brief Prepare target/host for RF-tests.
 * 
 * prepare for wi-fi tests.
 * You should download x-test-p before calling this function.
 *
 *
 * @return none
 */
void nrwifi_rf_test_start(void)
{   
   ind_handle =  we_ind_register(WE_IND_CONSOLE_REPLY, 
                                 "WE_IND_CONSOLE_REPLY",
                                 handle_console_reply, 
                                 NULL,
                                 FALSE,
                                 NULL);

   DE_MEMSET(&rxstat,0,sizeof(rxstat));
   DE_MEMSET(&settings,0,sizeof(settings));

   //DE_MEMCPY(&settings.bssid[0],&bssid[0],6);
   settings.rf_channel = 1;
   settings.data_rate = 2;
   settings.packet_size = 512;
   settings.packet_count = 10;
   settings.packet_interval = 10;
   settings.pattern = 1;
   settings.rx_filter_mode = 0;

   x_cmd_set_channel(settings.rf_channel);
}

/*!
 * @brief Reset statistics and start listening for data.
 * 
 * @param channel RF-channel
 *
 *
 * @return none
 */
void nrwifi_start_rx_test(int channel)
{
   hwt_req_t req;

   //CON_print("nrwifi_start_rx_test(%u)\n", channel);

   DE_MEMSET(&req, 0, sizeof(hwt_req_t));
   
   
   req.no = HWT_RX_REQ_START;
   req.attr_param.Channel = channel;
   settings.rf_channel = channel;
   req.result_cb = hwt_req_result_cb;
   hwt_request(&req);
}

/*!
 * @brief gets the statistics for the rx-test
 *
 * fetches the statistics and deliver it by callig a function
 * the statistics is for one rate only
 * the statistics will not be cleared
 *
 * @param cb_fn   pointer to the function that will be called when the data have 
 *                been fetched fetched from target.
 * @param rate    rate in unit bits/sec (11000000 = 11Mbps)
 *
 * @return none
 */
void  nrwifi_get_rx_stats(nrwifi_rf_statiscs_cb cb_fn, int rate)
{
   hwt_req_t req;

   //CON_print("nrwifi_get_rx_stats(%X)\n", cb_fn);

   DE_MEMSET(&req, 0, sizeof(hwt_req_t));

   settings.data_rate = hwt_convert_bitrate_into_xmit_bitrate_code(rate);
   
   
   req.no = HWT_RX_REQ_END;
   req.attr_param.DataRate = rate;
   req.result_cb = hwt_req_result_cb;
   req.priv = (void*) cb_fn;
   hwt_request(&req);
}

/*!
 * @brief transmits packets
 *
 * Transmitts a number of packets.
 *
 * @param PacketLength     Lenth of packet counted in bytes
 * @param NumOfFrame       number of packets to send
 * @param InterPacketDelay delay between each packet (in us)
 * @param rate             rate in unit "bits/sec" (11000000 = 11Mbps)
 *
 * @return none
 */
void  nrwifi_start_pkt_tx( int PacketLength,
                           int NumOfFrame,
                           int InterPacketDelay,
                           int rate)
{
   hwt_req_t req;

   DE_MEMSET(&req, 0, sizeof(hwt_req_t));
  
   req.no = HWT_TX_REQ_PKT_START;
   req.attr_param.PacketLength = PacketLength;
   req.attr_param.NumOfFrame = NumOfFrame;
   req.attr_param.iInterPacketDelay = InterPacketDelay;
   req.attr_param.DataRate = rate;
   req.result_cb = hwt_req_result_cb;
   req.attr_param.Channel = settings.rf_channel;
   hwt_request(&req);
}


/*!
 * @brief sets the RF-channel
 *
 * @param channel     channel (1-14)
 *
 * @return none
 */
void  nrwifi_set_rf_ch( int channel)
{
   x_cmd_set_channel(channel);
}





/******************************************************************************/
/*                              STATIC FUNCTIONS                              */
/******************************************************************************/


static int hwt_request(hwt_req_t *req)
{
   current_command = *req;

   switch(req->no)
   {
      /*case HWT_START_TEST_MODE:
         hwt_startup();
         return TRUE;

      case HWT_STOP_TEST_MODE:
         hwt_shutdown();
         return TRUE;*/

      /* multiple console commands */
      case HWT_RX_REQ_END:
         x_cmd_rxstat_get();
         return TRUE;

      default:
         break;
   }
   
   /* sanity */
   if(req->attr_param.Channel > 14)        req->attr_param.Channel = 14;
   if(req->attr_param.Channel < 1)         req->attr_param.Channel = 1;

   if(req->attr_param.DataRate)
      settings.data_rate = hwt_convert_bitrate_into_xmit_bitrate_code(req->attr_param.DataRate);
   
   if(req->attr_param.PacketLength > 1536)      req->attr_param.PacketLength = 1536;
 
   if(hwt_console_request(req))
      return TRUE;

   DE_TRACE_INT(TR_SEVERE, "UNKNOWN SIGNAL NO: %u", req->no);
   return FALSE;
}

static char *nrstrtok_r (char *s, const char *delim, char **save_ptr)
{
  char *token;

  if (s == NULL)
    s = *save_ptr;

  // Scan leading delimiters. 
  s += strspn (s, delim);
  if (*s == '\0')
    {
      *save_ptr = s;
      return NULL;
    }

  // Find the end of the token.
  token = s;
  s = strpbrk (token, delim);
  if (s == NULL)
    // This token finishes the string.
    *save_ptr = strchr (token, '\0');// *save_ptr = memchr (token, '\0', strlen(token));
  else
    {
      // Terminate the token and make *SAVE_PTR point past it. 
      *s = '\0';
      *save_ptr = s + 1;
    }
  
  return token;
}

static void x_cmd_set_channel(int ch)
{
   char cmd[64];

   settings.rf_channel = ch;

   DE_SNPRINTF(cmd, sizeof(cmd), "set_rf_channel=%u\n", ch);
   DE_TRACE_STRING(TR_CONSOLE, "%s\n", cmd);
   WiFiEngine_SendConsoleRequest(cmd, NULL);
}

static void x_cmd_rxstat_get(void)
{
   char cmd[64];

   //CON_print("x_cmd_rxstat_get()\n");

   DE_MEMSET(&rxstat, 0, sizeof(rxstat));

   DE_SNPRINTF(cmd, sizeof(cmd), "rxstat=%d,0\n", settings.data_rate);
   WiFiEngine_SendConsoleRequest(cmd, NULL);
}

//Will be called each time a console reply is received
static void handle_console_reply(wi_msg_param_t param, void* priv)
{
   hic_message_context_t msg_ref;
   Blob_t blob;

   char buf[1024];
   union {
      hic_mac_console_cfm_t cfm;
      hic_mac_console_ind_t ind;
      char buf[1024];
   } reply;
   size_t buflen = sizeof(buf);
   int status;

   status = WiFiEngine_GetConsoleReply(buf, &buflen);
   if(status != WIFI_ENGINE_SUCCESS)
   {
      //CON_print("WiFiEngine_GetConsoleReply() failed\n");
      return;
   }

   Mlme_CreateMessageContext(msg_ref); 
   
   msg_ref.raw = &reply;
   msg_ref.raw_size = sizeof(reply);
   msg_ref.packed = buf;
   msg_ref.packed_size = buflen;
 
   INIT_BLOB(&blob, msg_ref.packed, msg_ref.packed_size);

   packer_HIC_Unpack(&msg_ref, &blob);

   packer_Unpack(&msg_ref, &blob);

   if(msg_ref.msg_id == HIC_MAC_CONSOLE_CFM)
   {
     //CON_print("HIC_MAC_CONSOLE_CFM: trans_id = %u, result = %u\n", 
     //            reply.cfm.trans_id,
     //            reply.cfm.result);*/
   }
   else if(msg_ref.msg_id == HIC_MAC_CONSOLE_IND)
   {
      //CON_print("console repy:\n%s\n", &reply.ind.string);
      parse_console_reply(&reply.ind.string);
   }
   return;
   
}


static void parse_console_reply(char *string)
{
   char *token;
   char *end;
   char *last = NULL;

#define PARSE_INT(N)                            \
   token = nrstrtok_r(NULL, "\r\n :,", &last);    \
   if(token == NULL)                          \
      return;                                   \
   N = DE_STRTOL(token, &end, 0);               \
   if(*end != '\0')                            \
      return;

   token = nrstrtok_r(string, "\r\n :,", &last);
   if(token == NULL)
   {
      //CON_print("token is NULL\n");
      return;
   }

   //CON_print("token is %s\n",token);

   if(DE_STRCMP(token, "ERROR") == 0) {
      wei_clear_cmd_queue();
      command_result(TP_ProxyStatusCode_DriverError, NULL);
   }
   if(DE_STRCMP(token, "RFCHAN") == 0) {
      // do nothing, other command will follow 
   }
   if(DE_STRCMP(token, "TXGEN_START") == 0) {
      command_result(TP_ProxyStatusCode_OK, NULL);
   }
   if(DE_STRCMP(token, "TXGEN_COMPLETE") == 0) {
      // don't need to do anything here 
   }
   if(DE_STRCMP(token, "TXGEN_STOP") == 0) {
      command_result(TP_ProxyStatusCode_OK, NULL);
   }
   if(DE_STRCMP(token, "DEC_PWR_INDEX") == 0) {
      // do nothing, other command will follow
   }
   if(DE_STRCMP(token, "SET_BSSID") == 0) {
      // do nothing, rxstat_clr will follow
   }
   if(DE_STRCMP(token, "RXSTAT_CLR") == 0) {
      // do nothing, txstat_clr will follow
   }
   if(DE_STRCMP(token, "TXSTAT_CLR") == 0) {
      command_result(TP_ProxyStatusCode_OK, NULL);
   }
   if(DE_STRCMP(token, "RXSTAT") == 0) {
      long rate, rxcount;
      rxstat_t rr;

      PARSE_INT(rate);
      PARSE_INT(rxcount);

      // if this is a short rxstat, we will bail out at this point
      // per-rate statistics follow
      PARSE_INT(rr.ucast_to_self);
      PARSE_INT(rr.ucast_to_other);
      PARSE_INT(rr.mcast_to_self);
      PARSE_INT(rr.mcast_to_other);
      PARSE_INT(rr.ack_to_self);
      PARSE_INT(rr.rts_to_self);
      PARSE_INT(rr.cts_to_self);
      PARSE_INT(rr.cfend_to_self);
      PARSE_INT(rr.bad_crc);
      PARSE_INT(rr.bad_crc_ucast);
      PARSE_INT(rr.bad_crc_mcast);
                
      // non-per-rate statistics follow 
      PARSE_INT(rr.ofdm_chest_mag);
      PARSE_INT(rr.ofdm_snr);
      PARSE_INT(rr.ofdm_frame_counter);

      PARSE_INT(rr.qpsk_snr);
      PARSE_INT(rr.qpsk_symbol_diff);
      PARSE_INT(rr.qpsk_frame_counter);
      PARSE_INT(rr.qpsk_freq_error);

      PARSE_INT(rr.ofdm_freq_error);

      PARSE_INT(rr.ofdm_dc_off_re);
      PARSE_INT(rr.ofdm_dc_off_im);
      PARSE_INT(rr.ofdm_dc_off_count);

      PARSE_INT(rr.qpsk_dc_off_re);
      PARSE_INT(rr.qpsk_dc_off_im);
      PARSE_INT(rr.qpsk_dc_off_count);

#define X(Y) rxstat.Y += rr.Y
      X(ucast_to_self);
      X(ucast_to_other);
      X(mcast_to_self);
      X(mcast_to_other);
      X(ack_to_self);
      X(rts_to_self);
      X(cts_to_self);
      X(cfend_to_self);
      X(bad_crc);
      X(bad_crc_ucast);
      X(bad_crc_mcast);

#undef X
#define X(Y) rxstat.Y = rr.Y
      X(ofdm_chest_mag);
      X(ofdm_snr);
      X(ofdm_frame_counter);

      X(qpsk_snr);
      X(qpsk_symbol_diff);
      X(qpsk_frame_counter);
      X(qpsk_freq_error);

      X(ofdm_freq_error);

      X(ofdm_dc_off_re);
      X(ofdm_dc_off_im);
      X(ofdm_dc_off_count);

      X(qpsk_dc_off_re);
      X(qpsk_dc_off_im);
      X(qpsk_dc_off_count);
#undef X
#define ZOO(Z) DE_TRACE_INT2(TR_HWT, #Z " = %u (%u)\n", (unsigned int)rxstat.Z, (unsigned int)rr.Z)
      ZOO(ucast_to_self);
      ZOO(ucast_to_other);
      ZOO(ucast_to_self);
      ZOO(ucast_to_other);
      ZOO(bad_crc);
      ZOO(bad_crc_ucast);
      ZOO(bad_crc_mcast);

      command_result(TP_ProxyStatusCode_OK, &rxstat);

   }
   if(DE_STRCMP(token, "TXSTAT") == 0) {
      long rate, txcount;

      PARSE_INT(rate);
      PARSE_INT(txcount);
   }
}


static void command_result(int status, void *param)
{

   hwt_req_t *req;

   /* TODO: dequeue */
   req = &current_command;

   //CON_print("command_result()\n");

   if(!req)
      return;

   if(!req->result_cb) 
      return;

   if(req->no != HWT_RX_REQ_END)
      return;

   /* HWT_RX_REQ_END */

   req->attr_param.iErrorFrame = rxstat.bad_crc 
      + rxstat.bad_crc_ucast 
      + rxstat.bad_crc_mcast;

   req->attr_param.iGoodFrame = 
      rxstat.ucast_to_self 
      + rxstat.ucast_to_other 
      + rxstat.mcast_to_self
      + rxstat.mcast_to_other
      + rxstat.ack_to_self
      + rxstat.rts_to_self
      + rxstat.cts_to_self
      + rxstat.cfend_to_self;

   req->attr_param.iTotalFrame = req->attr_param.iErrorFrame + req->attr_param.iGoodFrame;

   //CON_print("HWT_RX_REQ_END...\n");

   req->result_cb(status,req);

}

/*! 
 * @param   'bitrate'   unit bits/s
 */
static m80211_xmit_rate_t hwt_convert_bitrate_into_xmit_bitrate_code(uint32_t bitrate)
{
   m80211_xmit_rate_t rate;
   
   switch(bitrate)
   {
      case 1000000:
         rate = M80211_XMIT_RATE_1MBIT;
         break;
      case 2000000:
         rate = M80211_XMIT_RATE_2MBIT;
         break;
      case 5500000:
         rate = M80211_XMIT_RATE_5_5MBIT;
         break;
      case 6000000:
         rate = M80211_XMIT_RATE_6MBIT;
         break;
      case 9000000:
         rate = M80211_XMIT_RATE_9MBIT;
         break;
      case 11000000:
         rate = M80211_XMIT_RATE_11MBIT;
         break;
      case 12000000:
         rate = M80211_XMIT_RATE_12MBIT;
         break;
      case 18000000:
         rate = M80211_XMIT_RATE_18MBIT;
         break;
      case 22000000:
         rate = M80211_XMIT_RATE_22MBIT;
         break;
      case 24000000:
         rate = M80211_XMIT_RATE_24MBIT;
         break;
      case 33000000:
         rate = M80211_XMIT_RATE_33MBIT;
         break;
      case 36000000:
         rate = M80211_XMIT_RATE_36MBIT;
         break;
      case 48000000:
         rate = M80211_XMIT_RATE_48MBIT;
         break;
      case 54000000:
      default:
         rate = M80211_XMIT_RATE_54MBIT;
         break;
   }
   return rate;
}

static int hwt_console_request(hwt_req_t *req)
{
   char cmd[64];

   switch(req->no)
   {
      case HWT_TX_REQ_END:
         wei_clear_cmd_queue();
         DE_SNPRINTF(cmd, sizeof(cmd), "txgen_stop\n");
         break;
#if 0
      case HWT_TX_REQ_CONTINUOUS_START:
         if(req->attr_param.Channel != settings.rf_channel)
         {
            settings.rf_channel = req->attr_param.Channel;
            x_cmd_set_channel(settings.rf_channel);
         }

         x_cmd_set_tx_power(req->attr_param.TxPower);
         
         DE_SNPRINTF(cmd, sizeof(cmd),
                     "txgen_start=%u,%u,%u,%u,%x,%u,%u\n",
                     settings.data_rate, /* 0-13,14=CW */
                     req->attr_param.PacketLength,
                     0, /* inf */ 
                     (req->attr_param.iInterPacketDelay+500)/1000, /* µs -> ms */
                     0x100, /* PRBS9 */
                     0, /* autoprint */
                     0); /* framenum gen */
         break;

      case HWT_TX_REQ_CW_START:
         if(req->attr_param.Channel != settings.rf_channel)
         {
            settings.rf_channel = req->attr_param.Channel;
            x_cmd_set_channel(settings.rf_channel);
         }

         x_cmd_set_tx_power(req->attr_param.TxPower);
         
         DE_SNPRINTF(cmd, sizeof(cmd),
                     "txgen_start=%u,%u,%u,%u,%x,%u,%u\n",
                     14, /* 14=CW */
                     0,
                     0,  /* inf */
                     0,
                     0,
                     0, /* autoprint */
                     0); /* framenum gen */
         break;
#endif
      case HWT_TX_REQ_PKT_START: 
         if(req->attr_param.Channel != settings.rf_channel)
         {
            settings.rf_channel = req->attr_param.Channel;
            x_cmd_set_channel(settings.rf_channel);
         }

         //x_cmd_set_tx_power(req->attr_param.TxPower);
         
         DE_SNPRINTF(cmd, sizeof(cmd), 
                     "txgen_start=%u,%u,%u,%u,%x,%u,%u\n", 
                     settings.data_rate, /* 0-13,14=CW */
                     req->attr_param.PacketLength,
                     req->attr_param.NumOfFrame,
                     (req->attr_param.iInterPacketDelay+500)/1000, /* µs -> ms */
                     0x100, /* PRBS9 */
                     0, /* autoprint */
                     0); /* framenum gen */

         break;

      case HWT_RX_REQ_START:
         if(req->attr_param.Channel != settings.rf_channel)
         {
            settings.rf_channel = req->attr_param.Channel;
            x_cmd_set_channel(settings.rf_channel);
         }

         DE_SNPRINTF(cmd, sizeof(cmd), "rxstat_clr\n");
         break;

      default:
         return FALSE;
   }

   WiFiEngine_SendConsoleRequest(cmd, NULL);
   return TRUE;
}

static void hwt_req_result_cb(int status, hwt_req_t *req)
{
   //CON_print("hwt_req_result_cb(%u, %X)\n",status,req);

   if(req)
   {
      switch(req->no)
      {
      case HWT_RX_REQ_END:
         if(req->priv)
         {
            ((nrwifi_rf_statiscs_cb)req->priv)(&req->attr_param);
         }
         break;
      }
   }
}
