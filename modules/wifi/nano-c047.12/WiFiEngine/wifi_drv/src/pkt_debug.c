/* $Id: pkt_debug.c 17581 2011-01-19 14:49:23Z niks $ */
#include "driverenv.h"
#include "wifi_engine_internal.h"

/* #define PACKET_MAX 1500 */
#define PACKET_MAX 2500

#define IP_HDR_PROTO_OFFSET 10

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#if defined(WITH_PACKET_HISTORY) || defined(WIFI_DEBUG_ON)
static uint16_t nbo_uint16(char *p)
{
   return p[1] + (p[0]<<8);
}
#endif

#ifdef WIFI_DEBUG_ON

const char *const CONSOLE_tx_table[] = {
  "HIC_MAC_CONSOLE_REQ"
};

const char *const CONSOLE_rx_table[] = {
  "HIC_MAC_CONSOLE_CFM",
  "HIC_MAC_CONSOLE_IND"
};

const char *const DATA_tx_table[] = {
  "HIC_MAC_DATA_REQ",
  "HIC_MAC_DATA_RSP"
};

const char *const DATA_rx_table[] = {
  "HIC_MAC_DATA_IND",
  "HIC_MAC_DATA_CFM"
};

const char *const DLM_tx_table[] = {
  "HIC_DLM_LOAD_REQ",
  "HIC_DLM_LOAD_FAILED_IND"
};

const char *const DLM_rx_table[] = {
  "HIC_DLM_LOAD_CFM",
  "HIC_DLM_SWAP_IND"
};

const char *const ECHO_tx_table[] = {
  "ECHO_REQ",
  "ECHO_ADVANCED_REQ"
};

const char *const ECHO_rx_table[] = {
  "ECHO_CFM"
};

const char *const CTRL_tx_table[] = {
  "HIC_CTRL_INTERFACE_DOWN",
  "HIC_CTRL_HIC_VERSION_REQ",
  "HIC_CTRL_HEARTBEAT_REQ",
  "HIC_CTRL_SET_ALIGNMENT_REQ",
  "HIC_CTRL_SCB_ERROR_REQ",
  "HIC_CTRL_SLEEP_FOREVER_REQ",
  "HIC_CTRL_COMMIT_SUICIDE",
  "HIC_CTRL_INIT_COMPLETED_REQ",
  "HIC_CTRL_HL_SYNC_REQ"
};

const char *const CTRL_rx_table[] = {
  "HIC_CTRL_WAKEUP_IND",
  "HIC_CTRL_HIC_VERSION_CFM",
  "HIC_CTRL_HEARTBEAT_CFM",
  "HIC_CTRL_HEARTBEAT_IND",
  "HIC_CTRL_SET_ALIGNMENT_CFM",
  "HIC_CTRL_SCB_ERROR_CFM",
  "HIC_CTRL_SLEEP_FOREVER_CFM",
  "HIC_CTRL_SCB_ERROR_IND",
  "HIC_CTRL_INIT_COMPLETED_CFM",
  "HIC_CTRL_HL_SYNC_CFM"
};

const char *const MIB_tx_table[] = {
  "MLME_GET_REQ",
  "MLME_SET_REQ",
  "MLME_GET_NEXT_REQ",
  "MLME_MIB_SET_TRIGGER_REQ",
  "MLME_MIB_REMOVE_TRIGGER_REQ",
  "MLME_MIB_SET_GATINGTRIGGER_REQ",
  "MLME_GET_RAW_REQ",
  "MLME_SET_RAW_REQ"
};

const char *const MIB_rx_table[] = {
  "MLME_GET_CFM",
  "MLME_SET_CFM",
  "MLME_MIB_SET_TRIGGER_CFM",
  "MLME_MIB_REMOVE_TRIGGER_CFM",
  "MLME_MIB_TRIGGER_IND",
  "MLME_MIB_SET_GATINGTRIGGER_CFM"
};

const char *const MGMT_tx_table[] = {
  "MLME_RESET_REQ",
  "MLME_SCAN_REQ",
  "MLME_POWER_MGMT_REQ",
  "MLME_JOIN_REQ",
  "MLME_AUTHENTICATE_REQ",
  "MLME_DEAUTHENTICATE_REQ",
  "MLME_ASSOCIATE_REQ",
  "MLME_REASSOCIATE_REQ",
  "MLME_DISASSOCIATE_REQ",
  "MLME_START_REQ",
  "MLME_SET_KEY_REQ",
  "MLME_DELETE_KEY_REQ",
  "MLME_SET_PROTECTION_REQ",
  "NRP_MLME_BSS_LEAVE_REQ",
  "NRP_MLME_IBSS_LEAVE_REQ",
  "NRP_MLME_SETSCANPARAM_REQ",
  "NRP_MLME_ADD_SCANFILTER_REQ",
  "NRP_MLME_REMOVE_SCANFILTER_REQ",
  "NRP_MLME_ADD_SCANJOB_REQ",
  "NRP_MLME_REMOVE_SCANJOB_REQ",
  "NRP_MLME_GET_SCANFILTER_REQ",
  "NRP_MLME_SET_SCANJOBSTATE_REQ",
  "NRP_MLME_SET_SCANCOUNTRYINFO_REQ",
  "NRP_MLME_WMM_PS_PERIOD_START_REQ",
  "NRP_MLME_ADDTS_REQ"
};

const char *const MGMT_rx_table[] = {
  "MLME_RESET_CFM",
  "MLME_SCAN_CFM",
  "MLME_POWER_MGMT_CFM",
  "MLME_JOIN_CFM",
  "MLME_AUTHENTICATE_CFM",
  "MLME_AUTHENTICATE_IND",
  "MLME_DEAUTHENTICATE_CFM",
  "MLME_DEAUTHENTICATE_IND",
  "MLME_ASSOCIATE_CFM",
  "MLME_ASSOCIATE_IND",
  "MLME_REASSOCIATE_CFM",
  "MLME_REASSOCIATE_IND",
  "MLME_DISASSOCIATE_CFM",
  "MLME_DISASSOCIATE_IND",
  "MLME_START_CFM",
  "MLME_SET_KEY_CFM",
  "MLME_DELETE_KEY_CFM",
  "MLME_SET_PROTECTION_CFM",
  "MLME_MICHAEL_MIC_FAILURE_IND",
  "MLME_SCAN_IND",
  "NRP_MLME_BSS_LEAVE_CFM",
  "NRP_MLME_IBSS_LEAVE_CFM",
  "NRP_MLME_PEER_STATUS_IND",
  "NRP_MLME_SETSCANPARAM_CFM",
  "NRP_MLME_ADD_SCANFILTER_CFM",
  "NRP_MLME_REMOVE_SCANFILTER_CFM",
  "NRP_MLME_ADD_SCANJOB_CFM",
  "NRP_MLME_REMOVE_SCANJOB_CFM",
  "NRP_MLME_GET_SCANFILTER_CFM",
  "NRP_MLME_SET_SCANJOBSTATE_CFM",
  "NRP_MLME_SCANNOTIFICATION_IND",
  "NRP_MLME_SET_SCANCOUNTRYINFO_CFM",
  "NRP_MLME_WMM_PS_PERIOD_START_CFM",
  "NRP_MLME_ADDTS_CFM"
};

const char *const FLASH_PRG_tx_table[] = {
  "HIC_MAC_START_PRG_REQ",
  "HIC_MAC_WRITE_FLASH_REQ",
  "HIC_MAC_END_PRG_REQ",
  "HIC_MAC_START_READ_REQ",
  "HIC_MAC_READ_FLASH_REQ",
  "HIC_MAC_END_READ_REQ"
};

const char *const FLASH_PRG_rx_table[] = {
  "HIC_MAC_START_PRG_CFM",
  "HIC_MAC_WRITE_FLASH_CFM",
  "HIC_MAC_END_PRG_CFM",
  "HIC_MAC_START_READ_CFM",
  "HIC_MAC_READ_FLASH_CFM",
  "HIC_MAC_END_READ_CFM",
  "HIC_MAC_CFM"
};



void print_pkt_hdr(char *pkt, size_t len)
{
   char str[128];
   uint16_t l;
   uint8_t t, m;
   size_t c = 0;
   unsigned int is_tx;
   unsigned int hsize;

   const char *const*table;
   size_t table_size;

   if (len < 8) {
      DE_TRACE_INT(TR_DATA, "Short packet (" TR_FSIZE_T " bytes)\n", 
                   TR_ASIZE_T(len));
      return;
   }
   l = HIC_MESSAGE_LENGTH_GET(pkt);
   t = HIC_MESSAGE_TYPE(pkt); 
   m = HIC_MESSAGE_ID(pkt);
   hsize = HIC_MESSAGE_HDR_SIZE(pkt);

   if(t == HIC_MESSAGE_TYPE_DATA && !TRACE_ENABLED(TR_DATA_DUMP))
   {
      return;
   }

   c += DE_SNPRINTF(str + c, sizeof(str) - c, "%d,%x.%x", l, t, m);
   c = DE_MIN(sizeof(str), c);

   if(len >= hsize + 4) {
      unsigned int tid = HIC_GET_ULE32(pkt + hsize);
      c += DE_SNPRINTF(str + c, sizeof(str) - c, "[%x]", tid);
      c = DE_MIN(sizeof(str), c);
   }

   is_tx = !(t & MAC_API_MSG_DIRECTION_BIT);
   t &= ~MAC_API_MSG_DIRECTION_BIT;
   m &= ~MAC_API_PRIMITIVE_TYPE_RSP;
   if (l < 8 || l > PACKET_MAX) {
      DE_TRACE_STRING(TR_DATA, "bad size packet %s\n", str);
      return;
   }
   if (t == HIC_MESSAGE_TYPE_AGGREGATION) {
      DE_TRACE_STRING(TR_DATA, "aggregated packet %s\n", str);
      return;
   }

#define X(N)                                            \
      case HIC_MESSAGE_TYPE_##N:                        \
         if(is_tx) {                                    \
            table = N##_tx_table;                       \
            table_size = DE_ARRAY_SIZE(N##_tx_table);   \
         } else {                                       \
            table = N##_rx_table;                       \
            table_size = DE_ARRAY_SIZE(N##_rx_table);   \
         }                                              \
      break;

   switch(t) {
      X(DATA);
      X(MGMT);
      X(MIB);
      X(ECHO);
      X(CONSOLE);
      X(FLASH_PRG);
      X(CTRL);
      X(DLM);
      default:
         DE_TRACE_STRING(TR_DATA, "unknown message type %s\n", str);
         return;
   }
   if(m >= table_size) {
      c += DE_SNPRINTF(str + c, sizeof(str) - c, ": UNKNOWN%d", m);
      c = DE_MIN(sizeof(str), c);
   } else {
      c += DE_SNPRINTF(str + c, sizeof(str) - c, ": %s", table[m]);
      c = DE_MIN(sizeof(str), c);
   }
   if(t == HIC_MESSAGE_TYPE_DATA 
      && m == (HIC_MAC_DATA_CFM & ~MAC_API_PRIMITIVE_TYPE_RSP)
      && len >= hsize + 8) {
      c += DE_SNPRINTF(str + c, sizeof(str) - c, " s%ur%up%xd%u",
                       HIC_DATA_CFM_STATUS(pkt + hsize),
                       HIC_DATA_CFM_RATE_USED(pkt + hsize),
                       HIC_DATA_CFM_RATE_PRIO(pkt + hsize),
                       HIC_DATA_CFM_DISCARDED_RX(pkt + hsize));
      c = DE_MIN(sizeof(str), c);
   }
   DE_TRACE_STRING(TR_DATA, "pkt: %s\n", str);
}


/* Call with pointer to the IP header */
void print_ip_header(char *pkt, size_t len)
{
   uint16_t proto;
   uint16_t ver;
   char str[128];
   size_t c = 0;
        
   ver = nbo_uint16(pkt);
   if (ver != 4) {
      DE_TRACE_INT(TR_DATA, "IP header version field not 4 (%d)\n", ver);
      return;
   }
   proto = nbo_uint16(pkt + IP_HDR_PROTO_OFFSET);
   switch (proto) {
      case IP_PROTO_ICMP:
         c += DE_SNPRINTF(str + c, sizeof(str) - c, "ICMP ");
         c = DE_MIN(sizeof(str), c);
         break;
      case IP_PROTO_UDP:
         c += DE_SNPRINTF(str + c, sizeof(str) - c, "UDP ");
         c = DE_MIN(sizeof(str), c);
         break;
      case IP_PROTO_TCP:
         c += DE_SNPRINTF(str + c, sizeof(str) - c, "TCP ");
         c = DE_MIN(sizeof(str), c);
         break;
      default :
         c += DE_SNPRINTF(str + c, sizeof(str) - c, "unknown protocol ");
         c = DE_MIN(sizeof(str), c);
         break;
                
   }

   DE_TRACE_STRING(TR_DATA, "%s\n", str);
}

#endif /* WIFI_DEBUG_ON */

#define MAC_FRAME_TYPE_OFFSET 12

#ifdef WITH_PACKET_HISTORY

/* Call with pointer to the MAC header */
void print_mac_pkt(char *pkt, size_t len)
{
}

void print_rsn_ie(char* rsn_ie, size_t len)
{
}


struct pkt_entry {
      char buf[PKT_LOG_PKT_SIZE];
      size_t len;
      uint32_t seq;
      uint32_t read;
      uint32_t trans_id;
};

struct pkt_entry pkt_log[PKT_LOG_LENGTH];
uint32_t current_seq;
int pkt_log_idx;

void init_pkt_log(void)
{
   DE_MEMSET(pkt_log, 0, sizeof pkt_log);
   current_seq = 0;
   pkt_log_idx = 0;
}

void log_pkt(char *buf, size_t len, int read)
{
   int i;
   char *p;

   WIFI_LOCK();
   i = pkt_log_idx;
   pkt_log_idx++;
   pkt_log_idx = pkt_log_idx % PKT_LOG_LENGTH;
   current_seq++;
   WIFI_UNLOCK();
   if (len > PKT_LOG_PKT_SIZE)
   {
      len = PKT_LOG_PKT_SIZE;
   }
   DE_MEMCPY(pkt_log[i].buf, buf, len);
   pkt_log[i].len = len;
   pkt_log[i].seq = current_seq;
   pkt_log[i].read = read;
   pkt_log[i].trans_id = 0;
   if (buf[2] == 0 && buf[3] == 0) /* Data req */
   {
      p = (char *) &pkt_log[i].trans_id;
      DE_MEMCPY(p, buf + 8 + 16, 4);
   }
   if (buf[2] == 0 && buf[3] == 1) /* Data cfm */
   {
      p = (char *) &pkt_log[i].trans_id;
      DE_MEMCPY(p, buf + 8 + 4, 4);
   }

}

/* Return 1 on success. 0 if len was too small. */
int dump_log(char *buf, size_t len)
{
   int i;
   
   for (i = 0; i < PKT_LOG_LENGTH; i++)
   {
      if (len < sizeof pkt_log[i])
      {
         return 0;
      }
      DE_MEMCPY(buf, &pkt_log[i], sizeof pkt_log[i]);
      buf += sizeof pkt_log[i];
      len -= sizeof pkt_log[i];
   }

   return 1;
}

#endif /* WITH_PACKET_HISTORY */
