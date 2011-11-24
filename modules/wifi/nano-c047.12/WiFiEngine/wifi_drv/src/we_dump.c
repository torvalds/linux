/* $Id: we_dump.c,v 1.110 2008-04-09 07:15:38 anob Exp $ */
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
This module implements the WiFiEngine target crash recovery interfaces

*****************************************************************************/
/** @defgroup we_dump WiFiEngine target crash recovery interface
 *
 * @brief The WiFiEngine crash recovery interface configures the device
 * crash recovery functionality.
 *
 *  @{
 */
#include "driverenv.h"
#include "ucos.h"
#include "m80211_stddefs.h"
#include "packer.h"
#include "registryAccess.h"
#include "wifi_engine_internal.h"
#include "hmg_traffic.h"
#include "mlme_proxy.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "pkt_debug.h"
#include "state_trace.h"
#include "we_dump.h"

#define START_ADDRESS_PHYSICAL_TX_BM_DESC_NRX701 0x73020
#define START_ADDRESS_PHYSICAL_TX_BM_DESC_NRX600 0xfffc3000
#define OFFSET_TO_POINTER_BM_DESC 0x4
#define OFFSET_TO_POINTER 16
#define START_TRANSMISSION 1

#define SAFE_ADDRESS_NRX600 0x60000
#define SAFE_ADDRESS_NRX701 0x40000 /* XXX wrong */

#define COREDUMP_BLOCK_SIZE 222U

#define BM_DESCRIPTOR_TOT_SIZE 16 /* sizeof(bm_descr_t) */
#define BM_DESCRIPTOR_SIZE_OFFSET (2*sizeof(uint32_t))
#define REPEATING 1
#define SINGLE_SHOT 0


#ifdef FILE_WE_DUMP_C
#undef FILE_NUMBER
#define FILE_NUMBER FILE_WE_DUMP_C
#endif //FILE_WE_DUMP_C

typedef enum
{
   W4_COMMIT_SUICIDE_REQUESTED,
   W4_SCB_ERROR_CFM,
   W4_MEMORY_DUMP
} core_dump_state_t;

#define dump_region_start(__r)  ((__r)->address)
#define dump_region_size(__r)   ((__r)->size)
#define dump_region_end(__r)    (dump_region_start((__r)) +     \
                                 dump_region_size((__r)))

typedef struct
{
   uint32_t  address;
   uint32_t  size;
} dump_address_table_t;

static const dump_address_table_t dump_address_table_nrx701[]= {
   /* NRX701 core registers */   
   { 0x00000, 0x20000},  /* RAM */
   { 0x40000, 0x08000},  /* RAM */
   { 0x00070000, 4 },    /* reset */
   { 0x00070004, 0x0c }, /* hpi */
   { 0x00070010, 0x1c }, /* gpio */   
   { 0x0007002c, 4 },    /* iomux adc */
   { 0x00070030, 0x28 }, /* dcu */
   { 0x00070058, 4 },    /* unit clocks */   
   { 0x00070060, 0x0c }, /* rfif */  
   { 0x0007006c, 8 },    /* dcu2 */     
   { 0x00070080, 8 },    /* adc dac */ 
   { 0x00070088, 0x10 }, /* hfc 2 */ 
   { 0x000700f4, 8 },    /* clock gate */      
   { 0x00070100, 0x0c }, /* tsf timer */    
   { 0x00070200, 4 },    /* icu context */ 
   { 0x00070204, 0x18 }, /* clock divider */ 
   { 0x00070300, 0x10 }, /* watchdog */    
   { 0x00071000, 0x60 }, /* icu */   
   { 0x00072008, 4 },    /* rand */
   { 0x00072030, 8 },    /* spi */   
   { 0x00072040, 0x10 }, /* soft irq */  
   { 0x00073000, 0x80 }, /* bm reg */
   { 0x00074000, 0x14 }, /* bb events */  
   { 0x00075000, 0x38 }, /* backoff */  
   { 0x00076000, 0x50 }, /* tsf */     
   { 0x00078000, 0x0c }, /* bbtx */  
   { 0x0007800C, 0x24 }, /* bbrx */    
   { 0x00078030, 0x38 }, /* bbtx2*/  
   { 0x00078080, 0x10 }, /* bbrx2 */ 
   { 0x000780a0, 0x28 }, /* bb measure */
   { 0x0007a000, 0x84 }, /* bma */ 
   { 0, 0 }              /* end */  
};

static const dump_address_table_t dump_address_table_nrx600[]= {
   /* NRX600 core registers */   
   { 0x00040000, 0x00010000 }, /* RAM */
   { 0x00050000, 0x00006000 }, /* RAM */
   { 0xfffc0000, 0x000000a0 }, /* .hw_dcu_reg    */  
   { 0xfffc0200, 0x00000010 }, /* .hw_tsf_tmr_cntr_reg  */  
   { 0xfffc0300, 0x00000030 }, /* .hw_gpio_reg   */  
   { 0xfffc0400, 0x00000010 }, /* .hw_wdg_ctrl_reg  */  
   { 0xfffc0500, 0x00000018 }, /* .hw_hpi_hw_reg  */  
   { 0xfffc0e00, 0x00000010 }, /* .hw_mac_clk_div_reg  */  
   { 0xfffc0f00, 0x00000010 }, /* .hw_mac_misc_reg  */  
   { 0xfffc1000, 0x00000034 }, /* .hw_icu_hw_reg  */  
   { 0xfffc2000, 0x0000004c }, /* .hw_tsf_tmr_reg  */  
   { 0xfffc3000, 0x00000060 }, /* .hw_bm_reg     */  
   { 0xfffc4000, 0x00000010 }, /* .hw_fmu_reg    */  
   { 0xfffc4100, 0x00000010 }, /* .hw_hw_build_info_reg   */  
   { 0xfffc4200, 0x00000004 }, /* .hw_rand_reg   */  
   { 0xfffc4300, 0x0000000c }, /* .hw_spi_reg    */  
   { 0xfffc5000, 0x000000c0 }, /* .hw_backoff_reg  */  
   { 0xfffc6000, 0x00000080 }, /* .hw_seq_reg    */  
   { 0xfffc6100, 0x0000001c }, /* .hw_hw_events_reg  */  
   { 0xfffc7000, 0x0000008c }, /* .hw_bbrx_ctrl_reg  */  
   { 0xfffc7100, 0x000000d0 }, /* .hw_rf_control_reg  */  
   { 0xfffc7200, 0x00000050 }, /* .hw_bb_misc_reg  */  
   { 0xfffc7300, 0x00000020 }, /* .hw_bbtx_reg   */  
   { 0xfffc7320, 0x0000001c }, /* .hw_bbtx_predist_reg  */  
   { 0xfffc7400, 0x0000003c }, /* .hw_bbrx_status_reg  */  
   { 0xfffc7500, 0x00000014 }, /* .hw_reset_reg_bb  */  
   { 0xfffc7600, 0x00000010 }, /* .hw_bbrx_ctrl_reg2  */  
   { 0xfffc7700, 0x0000003c }, /* .hw_adc_dac_reg  */  
   { 0xfffc7800, 0x0000001c }, /* .hw_bb_debug_channel_reg  */  
   { 0xfffc8000, 0x00000100 }, /* .hw_mmu_reg  */  
   { 0xfffca000, 0x0000004c }, /* .hw_coexist_reg  */  
   { 0, 0 }              /* end */  
};

static const dump_address_table_t *dump_address_table;

#define TX_DESC_SIZE 32
#define MAC_DATA_HDR_SIZE 6

static int timeout_during_coredump(void);
static void createDataReq(unsigned char **msg, const void *payload, size_t payload_size, uint32_t address, size_t *size);
static void createDataReq_u32(unsigned char **msg, uint32_t payload, uint32_t address, size_t *size);
static void createTxDescriptor(unsigned char *tx_desc);
static uint8_t handleSCBErrorCfm(char *pkt);
static void getNextBlock(uint32_t currentAddress);
static void sendScbErrorRequest(void);
static void sendCommitSuicideRequest(void);
static void sendExternalTriggedCommitSuicideRequest(void);
static int start_target_core_dump(void);

static int cmd_timeout_detect_cb(void *data, size_t len);
static int old_trace_mask;

static core_dump_state_t core_dump_state;
static uint32_t currentAddress;
static uint8_t m_objId;
static uint8_t m_errCode;
static uint32_t StartDescriptorAddress;
static uint32_t StartTxBmDesc;
static uint32_t signalHostAttentionAddress;
static const dump_address_table_t *currentRegion;
static bool_t is_recovering_from_crash = FALSE;

/* must not be modified in this file */
static void* m_ext_ctx = NULL;


static size_t
dump_total_size(const dump_address_table_t *table)
{
   size_t total = 0;
   unsigned int nreg = 0;

   while(table->size != 0) {
      total += table->size + sizeof(*table);
      nreg++;
      table++;
   }
   DE_TRACE_INT2(TR_ALWAYS, "COREDUMP_TOTAL_SIZE: " TR_FSIZE_T " bytes, "
                 "%u regions\n", 
                 TR_ASIZE_T(total), nreg);
   return total;
}

/*!
 * @brief Handles packet received when core dump is enabled.
 *
 * @param pkt Received packet.
 *
 * @return 
 * - Always returns WIFI_ENGINE_SUCCESS
 */
void WiFiEngine_HandleCoreDumpPkt(char* pkt)
{
   size_t size;
   unsigned char *msg =  NULL;
   
   if (!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) 
      return;
   
   switch(core_dump_state)
   {
      case W4_SCB_ERROR_CFM:
      {
         unsigned char tx_desc[TX_DESC_SIZE];
         hic_message_context_t msg_ref;
         uint8_t  messageId;
         uint8_t  messageType;
         Blob_t blob;
         Mlme_CreateMessageContext(msg_ref);

         printk("[nano] W4_SCB_ERROR_CFM parsed\n");
         INIT_BLOB(&blob, pkt, 1500); /* XXX */
         /* Remove HIC header and add type/id info to msg_ref */
         packer_HIC_Unpack(&msg_ref, &blob); 
         messageId = msg_ref.msg_id;
         messageType = msg_ref.msg_type;
         msg_ref.packed = NULL;
         Mlme_ReleaseMessageContext(msg_ref);

         if((messageType == HIC_MESSAGE_TYPE_CTRL) && (messageId == HIC_CTRL_SCB_ERROR_CFM))
            {
               /* Handle confirm message */         
               if(handleSCBErrorCfm(pkt) == 0) 
               {

                  /* SCB_ERROR_CFM has error code 0, this means that firmware.
                   * is executing "normally" (no scb error has occured).
                   * However a command timeout has occured so it would be nice
                   * to force firmware in scb error and try to get a core dump.
                   */
                  DE_TRACE_STATIC(TR_ALWAYS,"Cancel timer\n");
                  DriverEnvironment_CancelTimer(wifiEngineState.cmd_timer_id);
                  DE_TRACE_STATIC(TR_ALWAYS,"Suicide requested\n");  
                  sendCommitSuicideRequest();
                  wifiEngineState.core_dump_state = WEI_CORE_DUMP_DISABLED;
                  return;
               }


            if(m_ext_ctx) 
            {
               /* Coredump already started */
               DriverEnvironment_Core_Dump_Abort(
                                          registry.network.basic.enableCoredump, 
                                          registry.network.basic.restartTargetAfterCoredump,
                                          m_objId,
                                          m_errCode, 
                                          &m_ext_ctx);
            } 
            else 
            {
               DriverEnvironment_indicate(WE_IND_CORE_DUMP_START, NULL, 0);
            }
            DriverEnvironment_Core_Dump_Started(
                                          registry.network.basic.enableCoredump, 
                                          registry.network.basic.restartTargetAfterCoredump,
                                          m_objId,
                                          m_errCode, 
                                          dump_total_size(dump_address_table),
                                          dump_total_size(dump_address_table),
                                          &m_ext_ctx);

            /* Turn off traces */
            //trace_mask &= ~(TR_CMD | TR_NOISE | TR_PS | TR_HIGH_RES | TR_DATA );
            trace_mask = 0;

            DriverEnvironment_Enable_Boot();

            /* This is to prevent command timeout logic to re-start the timer */
            WES_SET_FLAG(WES_FLAG_CMD_TIMEOUT_RUNNING);

            /* Initiate physical bm-descriptor to point to tx-descriptors */
            createDataReq_u32(&msg, StartDescriptorAddress, 
                              StartTxBmDesc + OFFSET_TO_POINTER_BM_DESC, &size);
            /* Clear any pending flags and flush queue*/
            wifiEngineState.cmdReplyPending = 0;
            wei_clear_cmd_queue();
            
            wei_send_cmd_raw((char *)msg, size);

            /* Initiate targets tx-descriptor to be used */
            createTxDescriptor(tx_desc);
            createDataReq(&msg, tx_desc, sizeof(tx_desc),
                          StartDescriptorAddress, &size);
            /* Clear any pending flags and flush queue*/
            wifiEngineState.cmdReplyPending = 0;
            wei_clear_cmd_queue();
            wei_send_cmd_raw((char *)msg, size);

            core_dump_state = W4_MEMORY_DUMP;
            /*
             * if m_ext_ctx==NULL we are not interested in the coredump and can restart the target directly
             * Not sure of how to do this in a safe way so will go ahead with the coredump anyway for now and fix this later 
             */
            if(m_ext_ctx==NULL) { 
               trace_mask = old_trace_mask; 
               DriverEnvironment_Core_Dump_Complete(
                                                FALSE,
                                                registry.network.basic.restartTargetAfterCoredump,
                                                m_objId,
                                                m_errCode, 
                                                &m_ext_ctx);
               DriverEnvironment_indicate(WE_IND_CORE_DUMP_COMPLETE, NULL, 0);
               DriverEnvironment_CancelTimer(wifiEngineState.cmd_timer_id);
               WES_CLEAR_FLAG(WES_FLAG_CMD_TIMEOUT_RUNNING);
               return;
            }

            currentRegion = dump_address_table;
            currentAddress = dump_region_start(currentRegion);

            getNextBlock(currentAddress);
         }
         else
         {
            DE_TRACE_INT2(TR_ALWAYS, "unexpected message %u.%u\n",
                          messageType,
                          messageId); 
         }
      }
      break;

      case W4_MEMORY_DUMP:
      {
         uint32_t len;
         /* Store recevied packet in a file */
         
         len = HIC_MESSAGE_LENGTH_GET(pkt);
         /* Move beyond size */ 
         pkt += 2;
         len -= 2;
         
         /* Write dumpfile header (for nrx600 coredumps) */
         if (dump_address_table == dump_address_table_nrx600
             && m_ext_ctx
             && currentAddress == dump_region_start(currentRegion)) {
            DriverEnvironment_Core_Dump_Write(m_ext_ctx, 
                                              (char *)currentRegion, 
                                              sizeof(*currentRegion));
         }
         
         len = DE_MIN(len, dump_region_end(currentRegion) - currentAddress);
         /* Copy to buffer */
         if (m_ext_ctx) {
            DriverEnvironment_Core_Dump_Write(m_ext_ctx, pkt, len);
         }

         currentAddress += len;

         if(currentAddress >= dump_region_end(currentRegion)) {
            /* end of region, skip forward */
            currentRegion++;
            currentAddress = dump_region_start(currentRegion);
            if(dump_region_size(currentRegion) == 0) {
               /* Complete - Start store data on a file and restart driver */
               trace_mask = old_trace_mask;
               DE_TRACE_STATIC(TR_ALWAYS,"Coredump complete - cancel timer\n");
               DriverEnvironment_CancelTimer(wifiEngineState.cmd_timer_id);
               WES_CLEAR_FLAG(WES_FLAG_CMD_TIMEOUT_RUNNING);
               DriverEnvironment_Core_Dump_Complete(registry.network.basic.enableCoredump && m_ext_ctx != NULL, /* see driverenv.h */
                                                    registry.network.basic.restartTargetAfterCoredump,
                                                    m_objId,
                                                    m_errCode, 
                                                    &m_ext_ctx);
               DriverEnvironment_indicate(WE_IND_CORE_DUMP_COMPLETE, NULL, 0);
               return;
            }
         }
         getNextBlock(currentAddress);
      }
      break;

      default:
         /* Discard */
         printk("[nano] Unknown coredump message received\n");
         break;
   }
}

/*!
 * @brief Start coredump
 *
 * @return 
 * - Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_StartCoredump(void)
{
   rBasicWiFiProperties *basic;

   basic =(rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic != NULL);
   
   start_target_core_dump();
      
   return WIFI_ENGINE_SUCCESS;   
}


/*!
 * @brief Force a coredump to be generated
 *
 * @return 
 * - Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_RequestCoredump(void)
{
   WiFiEngine_RequestSuicide();

/* Suicide will cause x_mac to send an scb_error_ind
   that will start a coredump */
#if 0
   start_target_core_dump();
#endif

   return WIFI_ENGINE_SUCCESS;   
}

/*!
 * @brief Force a target crash generated
 *
 * @return 
 * - Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_RequestSuicide(void)
{
   BAIL_IF_UNPLUGGED;
   DE_TRACE_STATIC(TR_ALWAYS,"Suicide requested\n");  
   sendExternalTriggedCommitSuicideRequest();
   return WIFI_ENGINE_SUCCESS;   
}


/* @brief Start command timeout detection timer.
 */
void WiFiEngine__CommandTimeoutStart(void)
{
   int status;
   DE_TRACE_STATIC(TR_NOISE, "starting command timeout timer\n");
   status = DriverEnvironment_RegisterTimerCallback(registry.network.basic.cmdTimeout / 10,
                                                    wifiEngineState.cmd_timer_id, 
                                                    cmd_timeout_detect_cb,
                                                    1);
   if(status != DRIVERENVIRONMENT_SUCCESS) {
      DE_TRACE_STATIC(TR_NOISE, "No Cmd timeout callback registered, DE was busy\n");
   }
   WES_SET_FLAG(WES_FLAG_CMD_TIMEOUT_RUNNING);
}

/* @brief Stop command timeout detection timer.
 */
void WiFiEngine__CommandTimeoutStop(void)
{
   DE_TRACE_STATIC(TR_NOISE, "stopping command timeout timer\n");
   DriverEnvironment_CancelTimer(wifiEngineState.cmd_timer_id);
   WES_CLEAR_FLAG(WES_FLAG_CMD_TIMEOUT_RUNNING);
}

static int start_target_core_dump(void)
{
   WIFI_LOCK();
   if(WiFiEngine_isCoredumpEnabled()) {
      DE_TRACE_STATIC(TR_ALWAYS,"Coredump already started\n");
      WIFI_UNLOCK();
      return WIFI_ENGINE_FAILURE;
   }
   wifiEngineState.core_dump_state = WEI_CORE_DUMP_ENABLED;
   WIFI_UNLOCK();

   /* Release any pending scan complete before coredump
      starts */
   DriverEnvironment_indicate(WE_IND_SCAN_COMPLETE, NULL, 0);

   is_recovering_from_crash = TRUE;
   if(registry.network.basic.enableCoredump)
   {
      old_trace_mask = trace_mask;      
      sendScbErrorRequest();
   } 
   else 
   {
      if(m_ext_ctx) 
      {
          /* Release allocated memory */
          DriverEnvironment_Core_Dump_Abort(
                                    registry.network.basic.enableCoredump, 
                                    registry.network.basic.restartTargetAfterCoredump,
                                    m_objId,
                                    m_errCode, 
                                    &m_ext_ctx);
      } 
      else 
      {
         DriverEnvironment_indicate(WE_IND_CORE_DUMP_START, NULL, 0);
      }
      /* Should call Core_Dump_Started before calling ..._Complete() but exceptions can be made if coredump=FALSE */
      DriverEnvironment_Core_Dump_Complete(FALSE,
                                       registry.network.basic.restartTargetAfterCoredump,
                                       0,
                                       0, 
                                       &m_ext_ctx);
      DriverEnvironment_indicate(WE_IND_CORE_DUMP_COMPLETE, NULL, 0);
   }
   return WIFI_ENGINE_SUCCESS;
}

#ifndef UNREF
#define UNREF(x) (x = x)
#endif


/* This limits the number of times we run the command timeout callback
 * when we're idle. A value of 3 means that we will disable the timer
 * after the third invocation with an unchanged transaction id
 * counter. This means that we will disable the timer sometime after
 * cmdTimeout / 10 * cmd_timeout_max_idle_count seconds. Special value
 * of 0xffffffff means never disable timer.
 */
unsigned int cmd_timeout_max_idle_count = 4;

static int cmd_timeout_detect_cb(void *data, size_t len)
{   
   static uint32_t last_pkt_count;
   static uint32_t idle_count;

   UNREF(data);
   UNREF(len);

   if(!wifiEngineState.forceRestart)
   {
      if(!wifiEngineState.cmdReplyPending && 
         !wifiEngineState.dataReqPending) { 

         if(cmd_timeout_max_idle_count == 0xffffffff)
            return WIFI_ENGINE_SUCCESS;

         if(last_pkt_count == wifiEngineState.pkt_cnt) {
            idle_count++;
            DE_TRACE_INT(TR_CMD_DEBUG, "idle_count = %d\n", idle_count);
            if(idle_count >= cmd_timeout_max_idle_count) {
               WiFiEngine_CommandTimeoutStop();
            }
         } else {
            last_pkt_count = wifiEngineState.pkt_cnt;
            idle_count = 0;
         }
         return WIFI_ENGINE_SUCCESS;
      }
       
      if( DriverEnvironment_GetTicks() - wifiEngineState.cmd_tx_ts <
          DriverEnvironment_msec_to_ticks(registry.network.basic.cmdTimeout) )
         return WIFI_ENGINE_SUCCESS;
   }

   if(wifiEngineState.core_dump_state != WEI_CORE_DUMP_ENABLED)
   {
      DE_TRACE_INT2(TR_ALWAYS, "Core dump started by timeout, "
                    "last command %02x.%02x\n",
                    wifiEngineState.last_sent_msg_type, 
                    wifiEngineState.last_sent_msg_id);
      printk("[nano] ----> Core dump started by timeout, "
             "last command %02x.%02x\n",
             wifiEngineState.last_sent_msg_type, 
             wifiEngineState.last_sent_msg_id);
      DE_TRACE_INT4(TR_WARN,"Coredump timers: %u - %u = %u < %u\n", 
            (unsigned int)DriverEnvironment_GetTicks(),
            (unsigned int)wifiEngineState.cmd_tx_ts,
            (unsigned int)(DriverEnvironment_GetTicks() - wifiEngineState.cmd_tx_ts),
            (unsigned int)DriverEnvironment_msec_to_ticks(registry.network.basic.cmdTimeout));

      start_target_core_dump();
      return WIFI_ENGINE_SUCCESS;
   }
   else
   {
      timeout_during_coredump();
      return WIFI_ENGINE_SUCCESS;
   }

         
}


static int timeout_during_coredump(void)
{
   /* Target is not responding */
   if(core_dump_state == W4_SCB_ERROR_CFM)
   {
      DE_TRACE_STATIC(TR_ALWAYS,"Target not responding to scb error request\n");
      DE_TRACE_STATIC(TR_ALWAYS,"Try with commit suicide request\n");
      core_dump_state = W4_COMMIT_SUICIDE_REQUESTED;
      wifiEngineState.core_dump_state = WEI_CORE_DUMP_DISABLED;      
      sendCommitSuicideRequest(); 
   }
   else if(core_dump_state == W4_COMMIT_SUICIDE_REQUESTED)
   {
      char *str = "Fw not responding to scb error request - no coredump created";
      
      /* Target is not responding */
      DE_TRACE_STATIC(TR_ALWAYS,"Target not responding to commit suicide req\n");
      DE_TRACE_STATIC(TR_ALWAYS,"No coredump created - restart target\n");
      trace_mask = old_trace_mask;
      /* Not possible to load software */
      /* Prepare for coredump file*/
      DriverEnvironment_indicate(WE_IND_CORE_DUMP_START, NULL, 0);        
      DriverEnvironment_Core_Dump_Started(
                                    registry.network.basic.enableCoredump, 
                                    registry.network.basic.restartTargetAfterCoredump,
                                    0,
                                    0, 
                                    DE_STRLEN(str),
                                    DE_STRLEN(str),
                                    &m_ext_ctx); 
      if(m_ext_ctx) DriverEnvironment_Core_Dump_Write(m_ext_ctx, str, DE_STRLEN(str));

      /* Should call Core_Dump_Started before calling ..._Complete() but exceptions can be made if coredump=FALSE */
      DriverEnvironment_Core_Dump_Complete(FALSE,
                                       registry.network.basic.restartTargetAfterCoredump,
                                       0,
                                       0, 
                                       &m_ext_ctx);

       DriverEnvironment_indicate(WE_IND_CORE_DUMP_COMPLETE, NULL, 0);            
  }
  else
  {
      char *str = "Fw not responding during dump sequence - no coredump created";   
      /* Target is not responding */
      DE_TRACE_STATIC(TR_ALWAYS,"Target not responding during dump sequence\n");
      DE_TRACE_STATIC(TR_ALWAYS,"No coredump created - restart target\n");
      trace_mask = old_trace_mask;
      DriverEnvironment_indicate(WE_IND_CORE_DUMP_START, NULL, 0);        
      DriverEnvironment_Core_Dump_Started(
                                    registry.network.basic.enableCoredump, 
                                    registry.network.basic.restartTargetAfterCoredump,
                                    0,
                                    0, 
                                    DE_STRLEN(str),
                                    DE_STRLEN(str),
                                    &m_ext_ctx); 
      if(m_ext_ctx) DriverEnvironment_Core_Dump_Write(m_ext_ctx, str, DE_STRLEN(str));

      /* Should call Core_Dump_Started before calling ..._Complete() but exceptions can be made if coredump=FALSE */
      DriverEnvironment_Core_Dump_Complete(FALSE,
                                       registry.network.basic.restartTargetAfterCoredump,
                                       0,
                                       0, 
                                       &m_ext_ctx);
       DriverEnvironment_indicate(WE_IND_CORE_DUMP_COMPLETE, NULL, 0);       
  }
     

   return 0;
}

static void sendCommitSuicideRequest(void)
{
   hic_message_context_t  msg_ref;
   char *cmd; 
   int size;   
   
   /* Send commit suicide request */
   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateCommitSuicideReq(&msg_ref))
   {
      /* Clear any pending flags and flush queue*/
      wifiEngineState.cmdReplyPending = 0;
      wei_clear_cmd_queue();
      if (! packer_HIC_Pack(&msg_ref))
      {
         return;
      }       
      cmd = msg_ref.packed;
      size = msg_ref.packed_size;
      msg_ref.packed = NULL; 
      
      if (wei_send_cmd_raw(cmd, size)!= WIFI_ENGINE_SUCCESS)
      {
         DE_TRACE_STATIC(TR_WARN, "Failed to send HIC_CTRL_COMMIT_SUICIDE_REQ\n");
      }        
   }
   else
   {
      DE_TRACE_STATIC(TR_WARN, "Failed to create suicide request\n");
   }
   
   Mlme_ReleaseMessageContext(msg_ref);
    
}

static void sendExternalTriggedCommitSuicideRequest(void)
{
   hic_message_context_t  msg_ref;
   
   /* Send commit suicide request */
   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateCommitSuicideReq(&msg_ref))
   {
      /* Clear any pending flags and flush queue*/
      wifiEngineState.cmdReplyPending = 0;
      wei_clear_cmd_queue();
      /* wei_send_cmd will automatically wake up target if in sleep */
      if (wei_send_cmd(&msg_ref) != WIFI_ENGINE_SUCCESS)
      {
         DE_TRACE_STATIC(TR_WARN, "Failed to send HIC_CTRL_COMMIT_SUICIDE_REQ\n");
      }        
   }
   else
   {
      DE_TRACE_STATIC(TR_WARN, "Failed to create suicide request\n");
   }
   Mlme_ReleaseMessageContext(msg_ref);
    
}


static void sendScbErrorRequest(void)
{
   hic_message_context_t  msg_ref;
   char *dst_str;
   char *cmd;
   int size;
   
   DE_TRACE_STATIC(TR_ALWAYS,"sendScbErrorRequest()\n");

   core_dump_state = W4_SCB_ERROR_CFM;

   /* Send scb error request */
   Mlme_CreateMessageContext(msg_ref);
   dst_str = (char*)DriverEnvironment_Nonpaged_Malloc(sizeof(SCB_ERROR_KEY_STRING)); 
   if (Mlme_CreateScbErrorReq(&msg_ref, dst_str))
   {
      /* Clear any pending flags and flush queue*/
      wifiEngineState.cmdReplyPending = 0;
      wei_clear_cmd_queue();
      if (! packer_HIC_Pack(&msg_ref))
      {
         return ;
      }      
      cmd = msg_ref.packed;
      size = msg_ref.packed_size;
      msg_ref.packed = NULL;      
      
      if (wei_send_cmd_raw(cmd, size) != WIFI_ENGINE_SUCCESS)
      {
         DE_TRACE_STATIC(TR_WARN, "Failed to send HIC_CTRL_SCB_ERROR_REQ\n");
      }   
   }
   DriverEnvironment_Nonpaged_Free(dst_str);
   Mlme_ReleaseMessageContext(msg_ref);
}


static void getNextBlock(uint32_t currentAddress)
{
   unsigned char *msg =  NULL;
   size_t size;


   createDataReq_u32(&msg, currentAddress, StartDescriptorAddress + OFFSET_TO_POINTER, &size);
   /* Clear any pending flags and flush queue*/
   wifiEngineState.cmdReplyPending = 0;
   wei_clear_cmd_queue();
   wei_send_cmd_raw((char *)msg, size);

   /* Start transmission of next 256 bytes */
   createDataReq_u32(&msg, START_TRANSMISSION, StartTxBmDesc , &size);
   /* Clear any pending flags and flush queue*/
   wifiEngineState.cmdReplyPending = 0;
   wei_clear_cmd_queue();
   wei_send_cmd_raw((char *)msg, size);

   /* Request signal host attention */
   createDataReq_u32(&msg, START_TRANSMISSION, signalHostAttentionAddress, &size);
   /* Clear any pending flags and flush queue*/
   wifiEngineState.cmdReplyPending = 0;
   wei_clear_cmd_queue();
   wei_send_cmd_raw((char *)msg, size);
}

/* write integer to buffer in LE byteorder, 
   return pointer to following byte */
static unsigned char* stuff_int(unsigned char *ptr, uint32_t value, size_t len)
{
   while(len--) {
      *ptr++ = (unsigned char) value & 0xff;
      value >>= 8;
   }
   return ptr;
}

/* write a tx descriptor to buffer in LE byteorder */
static unsigned char*
stuff_tx_desc(unsigned char *p,
              uint32_t buf_ptr,
              uint32_t ctrl,
              uint16_t size,
              uint32_t next)
{
   p = stuff_int(p, buf_ptr, 4);
   p = stuff_int(p, ctrl, 4);
   p = stuff_int(p, size, 2);
   p = stuff_int(p, 0, 2);
   p = stuff_int(p, next, 4);
   return p;
}

static void
createDataReq_u32(unsigned char **msg, 
                  uint32_t payload, 
                  uint32_t address, 
                  size_t *size)
{
   unsigned char buf[4];

   stuff_int(buf, payload, sizeof(buf));
   createDataReq(msg, buf, sizeof(buf), address, size);
}

static void
createDataReq(unsigned char **msg, 
              const void *payload, 
              size_t payload_size, 
              uint32_t address, 
              size_t *size)
{
   unsigned char *p;
   size_t totSize;
   size_t padding = 0;

   totSize = MAC_DATA_HDR_SIZE + payload_size;
   if(totSize < wifiEngineState.config.min_pdu_size)
      padding = wifiEngineState.config.min_pdu_size - totSize;
   if(padding > 0 && padding < 6)
      padding = 6;
#undef A
#define A(N, S) ((N) & ((S) - 1))
   if(A(totSize + padding, wifiEngineState.config.pdu_size_alignment) != 0) {
      padding += wifiEngineState.config.pdu_size_alignment 
         - A(totSize + padding, wifiEngineState.config.pdu_size_alignment);
   }
   if(padding > 0 && padding < 6)
      padding = 6;
   if(A(totSize + padding, wifiEngineState.config.pdu_size_alignment) != 0) {
      padding += wifiEngineState.config.pdu_size_alignment 
         - A(totSize + padding, wifiEngineState.config.pdu_size_alignment);
   }

   totSize += padding;
   *msg = p = (unsigned char *)DriverEnvironment_TX_Alloc(totSize);
   p = stuff_int(p, payload_size, 2);
   p = stuff_int(p, address, 4);
   DE_MEMCPY(p, payload, payload_size);
   p += payload_size;

   DE_ASSERT(padding == 0 || padding >= 6);
   if(padding > 0) {
      /* padding is at least 6 */
      p = stuff_int(p, padding - 6, 2);
      if(dump_address_table == dump_address_table_nrx600) 
         p = stuff_int(p, SAFE_ADDRESS_NRX600, 4);
      else
         p = stuff_int(p, SAFE_ADDRESS_NRX701, 4);
      DE_MEMSET(p, 0, padding - 6);
   }

   *size = totSize;
}

static void createTxDescriptor(unsigned char *tx_desc)
{
   tx_desc = stuff_tx_desc(tx_desc,
                           StartDescriptorAddress+BM_DESCRIPTOR_TOT_SIZE + BM_DESCRIPTOR_SIZE_OFFSET,
                           0,
                           2,
                           StartDescriptorAddress+BM_DESCRIPTOR_TOT_SIZE);
   
   tx_desc = stuff_tx_desc(tx_desc,
                           0,
                           0,
                           COREDUMP_BLOCK_SIZE,
                           0);
}


static uint8_t handleSCBErrorCfm(char *pkt)
{
   hic_message_context_t   msg_ref;
   hic_ctrl_scb_error_cfm_t cfm;
   Blob_t blob;

   Mlme_CreateMessageContext(msg_ref);
   INIT_BLOB(&blob, pkt, 1500); /* XXX */
   packer_HIC_Unpack(&msg_ref, &blob);
   msg_ref.raw = &cfm;
   msg_ref.raw_size = sizeof cfm;
   packer_Unpack(&msg_ref, &blob); /* Unpack the level 2 header */
   msg_ref.raw = NULL;

   m_objId = cfm.objId;
   m_errCode = cfm.errCode;
   DE_TRACE_INT(TR_ALWAYS,"Object id: %d\n", cfm.objId);     
   DE_TRACE_INT(TR_ALWAYS,"errCode: %d\n",cfm.errCode);  

   StartDescriptorAddress = cfm.txDescriptorAddress;
   signalHostAttentionAddress = cfm.signalHostAttentionAddress;

   if (signalHostAttentionAddress < 0x40000) {
      /* NRX701 */
      StartTxBmDesc = START_ADDRESS_PHYSICAL_TX_BM_DESC_NRX701;
      dump_address_table = dump_address_table_nrx701;
      DE_TRACE_STATIC(TR_ALWAYS, "chipType: NRX701\n");  
   } else {
      /* NRX600 */
      StartTxBmDesc = START_ADDRESS_PHYSICAL_TX_BM_DESC_NRX600;
      dump_address_table = dump_address_table_nrx600;
      DE_TRACE_STATIC(TR_ALWAYS, "chipType: NRX600\n");  
   }

   return cfm.errCode;
}

void wei_dump_notify_connect_failed(void)
{
   // TODO: FIXME signal coredump start/complete and make the connection manager figure out what to do!
   DE_ASSERT(FALSE);
   is_recovering_from_crash = FALSE;
}

void wei_dump_notify_connect_success(void)
{
   is_recovering_from_crash = FALSE;
}



/** @} */ /* End of we_dump group */
