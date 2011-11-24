
/******************************************************************************
 *  Copyright (c) 2010 - Sweden Connectivity AB.
 *  ALL RIGHTS RESERVED
 *  
 *  command_parser.c
 *  Parses incomming HCI messages
 *   Take necessary WiFi action and send back the response
 *
 *****************************************************************************/
 
 
#include "driverenv.h"
#include "pal_command_parser.h"
#include "pal_init.h"
#include "wifi_engine_internal.h"



char readBuf[254];

typedef struct 
{
    const int *content;
    int bytesToWrite;
} WriteInfoStruct;

typedef struct 
{
    int length;
    int* buffer;
} ReadInfoStruct;



#define MAX_NUMBER_OF_PHYSICAL_LINKS       1
//zero initite in init, malloc oin init
//0  - Physical link handle
#define POSITION_PHYSICAL_LINK_HANDLE       0
//1  - Logical Link handle
#define LOGICAL_LINK_HANDLE             1
//3  - Local MAC Address
#define LOCAL_MAC_ADDRESS                (LOGICAL_LINK_HANDLE+2)
//9  - Remote MAC Address
#define REMOTE_MAC_ADDRESS                (LOCAL_MAC_ADDRESS+6)
//16 - AMP Key Length
#define AMP_KEY_LENGTH                  (REMOTE_MAC_ADDRESS+6)
//17 - AMP Key
#define AMP_KEY                        (AMP_KEY_LENGTH+1)
//33 - Channel List
#define CHANNEL_LIST                  (AMP_KEY+16)
//39 - PAL Version
#define PAL_VERSION                     (CHANNEL_LIST+1)
//40 - Link initiator
#define LINK_INDICATOR                  (PAL_VERSION+1)
//41 - Length so far
#define LENGTH_SO_FAR                  (LINK_INDICATOR+1)
//43 - Max Remote Amp Assoc
#define MAX_REMOTE_AMP_ASSOC            (LENGTH_SO_FAR+2)
//45 - Reset failed contact counter
#define RESET_FAILED_CONTACT_COUNTER      (MAX_REMOTE_AMP_ASSOC+2)
//45 - Link Supervision tmo
#define LINK_SUPERVISION_TMO            (RESET_FAILED_CONTACT_COUNTER+1)
//47 - Short range mode
#define SHORT_RANGE_MODE               (LINK_SUPERVISION_TMO+2)
//48 - logicaql link handle
#define POSITION_LOGICAL_LINK_HANDLE      (SHORT_RANGE_MODE+1)
//+2
#define POSITION_BEST_EFFORT_FLUSH_TIME_OUT   (POSITION_LOGICAL_LINK_HANDLE+2)
//+4
#define POSITION_LAST                  54

uint8_t physicalLinkHandle[MAX_NUMBER_OF_PHYSICAL_LINKS] [54];

static char eventMask[8];
static char eventMaskPage2[8];
char AMP_Assoc[180];

//one enough?
static driver_timer_id_t connectionTimer = 0xFFFFFFFF;
static driver_timer_id_t flushTimer = 0xFFFFFFFF;

mac_api_net_id_t netId=0x0001;

//static int            inFile;      
extern net_rx_cb_t HCI_response_cb;

char flow_control_mode_var= 0;

char testMode=0x00;

int ReadSupportedCommands(char* buf);
int ReadSupportedFeatures(char* buf);
char MAC_Adress[12];

char connectionAcceptTimeOutBBslots[2];
//short connectionAcceptTimeOutMsec=20000;//ms
short connectionAcceptTimeOutMsec=0xffff;
char creatingPhysicalLink;

char LinkSupervisionTmoBBslots[2];
int LinkSupervisionTmoMsec=20000;//ms

short logicalLinkAcceptTimeout;
short logicalLinkAcceptTimeoutMs;
char logicalLinkConnectionTimerRunning;

int bestEffortFlushTmoMicroSec;
int bestEffortFlushTmoMsec;
//int RegisterdFlushTimer;

int totalBandwidth;
int maxGuaranteedBandwidth;
int minLatency;
int maxPDUSize;
short palCapabilities;
short maxAmpAssocLength;
int maxFlushTmo      =   0xccddeeff;
//int bestEffortFlushTmo    =   0xffffffff;

short txMaxSDU;
int txSduInterArrivalTime;

short rxMaxSDU;
int rxSduInterArrivalTime;


uint8_t  Location_Domain_Aware      = 0x01;
uint16_t Location_Domain            = 0x5858;
uint8_t  Location_Domain_Options    = 0x00;
uint8_t  Location_Options           = 0x00;

int      maxRemoteAmpLength         = 0;
int      remainingRemoteAmpLength   = 0;
//int maxAmpAsscocLength = 250;
int      remainingAmpAsscocLength   = 0;

//MAC address 0x01, Preffered channel list 0x02, 802.11 PAL version 0x05
uint8_t  remoteAmpMAC[6];
uint8_t  remoteAmpChannelList[6];
uint8_t  remoteAmpVersion[1];

uint8_t  logicalLinkCancelSent;

//Data Block size
uint8_t  maxAclDataPacketLength     = 0xff;
uint8_t  dataBlockLength            = 0xff;
uint8_t  totalNumDataBlocks         = TOTAL_NUMBER_OF_DATA_BLOCKS;


//Datapackets with corresponding seq no.
uint8_t* dataPacket0;
int      transactionIdPacket0;
uint8_t* dataPacket1;
int      transactionIdPacket1;
uint8_t* dataPacket2;
int      transactionIdPacket2;

struct {
  uint8_t *dataptr;
  enum { NO_PACKET, WAIT_CONFIRM, DATA_FLUSHED, COMPLETED } status;
  uint32_t transid;
} dataBlockMAtrix[TOTAL_NUMBER_OF_DATA_BLOCKS];

int      lastDataPacket =0;
int      packetToFree=0;

de_callback_t Flush_Timer_Callback(void *data, size_t data_len);
de_callback_t Physical_Link_Timer_Callback(void *data, size_t data_len);
de_callback_t Logical_Link_Timer_Callback(void *data, size_t data_len);


//////////////////////////////////////////////////////////////////////////////////

int Init_Command_Parser(void)
{
   
   
   int i=0;
   int j=0;
   //int size = 6; //Size of MAC Address
   //int ResGetAddress;
   //int RegisterdTimer;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Init_Command_Parser\n");
#endif	
   //Set default event mask
   eventMask[7]=0x00;
   eventMask[6]=0x00;
   eventMask[5]=0x1F;
   eventMask[4]=0xff;
   eventMask[3]=0xff;
   eventMask[2]=0xff;
   eventMask[1]=0xff;
   eventMask[0]=0xff;
   
   //Set default event mask page 2
   eventMaskPage2[7]=0x00;
   eventMaskPage2[6]=0x00;
   eventMaskPage2[5]=0x00;
   eventMaskPage2[4]=0x00;
   eventMaskPage2[3]=0x00;
   eventMaskPage2[2]=0x00;
   eventMaskPage2[1]=0x3f;
   eventMaskPage2[0]=0xff;
   
   //Set default Connection tmo
   //WifiEngine_ConnectTimeout();
   
   logicalLinkCancelSent=FALSE;
   
   
   //Logical link accept timeout 5.06s
   //logicalLinkAcceptTimeout=0x1Fa0;
   logicalLinkAcceptTimeout=0xffff;
   
   //Physical link accept timeout 20s
   //connectionAcceptTimeOutMsec=20000;//ms
   connectionAcceptTimeOutMsec=0xffff;//ms
   creatingPhysicalLink=0x00;
   
   //test mode disabled
   testMode=0x00;
   
   //Set tottal bandwidth
   totalBandwidth         =   0x00005000; //20Mbit/s
   //Set max guaranteed bandwidth
   maxGuaranteedBandwidth   =   0x00005000; //20Mbit/s
   //Set Min Latency
   minLatency            =   0xfffffff;
   
   maxPDUSize            =   0x000000ff;
   palCapabilities         =   0x0001; //Service Type = Guaranteed
   maxAmpAssocLength      =   0x00ff;
   
   maxFlushTmo            =   0xffffffff; //no flushing at all
   //bestEffortFlushTmo       =   0xffffffff; //no flushing at all
   
   //Loop and 0 initiate 
   while (j<MAX_NUMBER_OF_PHYSICAL_LINKS)
   {
      while ( i<POSITION_LAST)
      {
         physicalLinkHandle[j][i]=0x00;
         i++;
      }
      j++;
   }
   //Initiate
   physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT]=0xff;
   physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT+1]=0xff;
   physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT+2]=0xff;
   physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT+3]=0xff;
      
      
   //Set Array for Local AMP Association
   //MAC Address
   AMP_Assoc[0]=0x01;
   AMP_Assoc[1]=0x00;
   AMP_Assoc[2]=0x06;
   
   //Move to Read local Amp Assoc
   //Seems to be to early to be done during init
   //ResGetAddress=WiFiEngine_GetMACAddress((char *)&AMP_Assoc[3], &size);
   //memcpy(&physicalLinkHandle[0] [LOCAL_MAC_ADDRESS], &AMP_Assoc[3],  6); 
      
   //Preffered channel list
   AMP_Assoc[9]=0x02;
   AMP_Assoc[10]=0x00;
   AMP_Assoc[11]=0x00;

   //802.11 PAL version
   AMP_Assoc[12]=0x05;
   AMP_Assoc[13]=0x00;
   AMP_Assoc[14]=0x01;
   AMP_Assoc[15]=0x01;
   
   
   //Data Block size
   maxAclDataPacketLength  = 0xff;
   dataBlockLength         = 0xff;
   totalNumDataBlocks      = TOTAL_NUMBER_OF_DATA_BLOCKS;
   
   
   //Allocate totalNumDataBlocks of memory for data packets
   dataPacket0 = (uint8_t*)DriverEnvironment_Malloc(dataBlockLength);
   transactionIdPacket1=0;
   dataPacket1 = (uint8_t*)DriverEnvironment_Malloc(dataBlockLength);
   transactionIdPacket2=1;
   dataPacket2 = (uint8_t*)DriverEnvironment_Malloc(dataBlockLength);
   transactionIdPacket2=2;
   
   //Matrix for holding completed structure for data packets   
   dataBlockMAtrix[0].dataptr = dataPacket0;
   dataBlockMAtrix[0].status = NO_PACKET;
   dataBlockMAtrix[0].transid = transactionIdPacket0;
   
   dataBlockMAtrix[1].dataptr = dataPacket1;
   dataBlockMAtrix[1].status = NO_PACKET;
   dataBlockMAtrix[1].transid = transactionIdPacket1;
   
   dataBlockMAtrix[2].dataptr = dataPacket2;
   dataBlockMAtrix[2].status = NO_PACKET;
   dataBlockMAtrix[2].transid = transactionIdPacket2;

   lastDataPacket =0;
   packetToFree=0;
   
   bestEffortFlushTmoMicroSec=0xffffffff;
   bestEffortFlushTmoMsec=0x418937;
   //Register timer for flush
   //Register callback for timer, not repeating
   
   if (DriverEnvironment_GetNewTimer(&flushTimer, FALSE)!=DRIVERENVIRONMENT_SUCCESS)
   {
#ifdef DEBUG_COMMAND_PARSER		
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Failed to allocate timer\n");
#endif		
   }
   else
   {
#ifdef DEBUG_COMMAND_PARSER
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer started\n");
#endif	
   }   
   
   
   //Set Link Supervision tmo to default value
   /*   if   (WiFiEngine_SetLinkSupervisionBeaconTimeout(linkSupervisionTmo)==WIFI_ENGINE_SUCCESS)
   {
#ifdef DEBUG_COMMAND_PARSER 		
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Set link supervision tmo OK\n");
#endif	
   }
   */
   return 0;
}

void eval_hci_cmd(char* HCI_Packet)
{     
   unsigned short ocfogf;
   
#ifdef DEBUG_COMMAND_PARSER	
   if (TRACE_ENABLED(TR_AMP_HIGH_RES))
   {
      int i=0;
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "EVAL HCI comm\n");
      
      for (i=0; i<=25; i++)
      {
         DE_TRACE_INT(TR_AMP_HIGH_RES, "HCI_PAcket = %02x\n", HCI_Packet[i]);
      }
   }
#endif	
   
   //HCI command
   if (HCI_Packet[0]==0x01)
   {
      //Extract OGF and OCF from HCI cmd
      ocfogf=(HCI_Packet[1]*256)+HCI_Packet[2];
      
      // Parse ocfogf
      if(ocfogf== HCI_Reset_Cmd_def)
      {
         HCI_Reset_Cmd_fcn(readBuf);
      }
      else if (ocfogf== HCI_Enable_Device_Under_Test_Cmd_def)
      { 
         HCI_Enable_Device_Under_Test_Cmd_fcn(HCI_Packet);   
      }   
      else if (ocfogf== HCI_Read_Buffer_Size_Cmd_def)
      {  
         HCI_Read_Buffer_Size_Cmd_fcn(HCI_Packet);      
      }   
      else if (ocfogf== Read_Local_Version_information_Cmd_def)
      {  
         HCI_Read_Local_Version_information_Cmd_fcn(HCI_Packet);       
      }
      else if (ocfogf== HCI_Read_Local_Supported_Features_Cmd_def)
      {  
         HCI_Read_Local_Supported_Features_Cmd_fcn(HCI_Packet);            
      }
      else if (ocfogf== HCI_Read_Local_Supported_Commands_Cmd_def)
      {   
         HCI_Read_Local_Supported_Commands_Cmd_fcn(HCI_Packet);
      }
      else if (ocfogf== HCI_Read_Failed_Contact_Counter_Command_Cmd_def)
      {   
         HCI_Read_Failed_Contact_Counter_Command_Cmd_fcn(HCI_Packet);            
      }   
      else if (ocfogf== HCI_Reset_Failed_Contact_Counter_Cmd_def)
      {   
         HCI_Reset_Failed_Contact_Counter_Cmd_fcn(HCI_Packet);
      }
      else if (ocfogf== HCI_Read_Connection_Accept_Timeout_Cmd_def)
      {   
         HCI_Read_Connection_Accept_Timeout_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Write_Connection_Accept_Timeout_Cmd_def)
      {   
         HCI_Write_Connection_Accept_Timeout_Cmd_fcn(HCI_Packet);            
      }
      else if (ocfogf== HCI_Read_Link_Supervision_Timeout_Cmd_def)
      {   
         HCI_Read_Link_Supervision_Timeout_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Write_Link_Supervision_Timeout_Cmd_def)
      {  
         HCI_Write_Link_Supervision_Timeout_Cmd_fcn(HCI_Packet);   
      }   
      else if (ocfogf== HCI_Read_logical_Link_Accept_Timeout_Cmd_def)
      {   
         HCI_Read_logical_Link_Accept_Timeout_Cmd_fcn(HCI_Packet);      
      }
      else if (ocfogf== HCI_Write_logical_Link_Accept_Timeout_Cmd_def)
      {      
         HCI_Write_logical_Link_Accept_Timeout_Cmd_fcn(HCI_Packet);         
      }
      else if (ocfogf== HCI_Read_Best_Effort_Flush_Timeout_Cmd_def)
      {   
         HCI_Read_Best_Effort_Flush_Timeout_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Write_Best_Effort_Flush_Timeout_Cmd_def)
      {   
         HCI_Write_Best_Effort_Flush_Timeout_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Read_Flow_Control_Mode_Cmd_def)
      {   
         HCI_Read_Flow_Control_Mode_Cmd_fcn(HCI_Packet);       
      }
      else if (ocfogf== HCI_Write_Flow_Control_Mode_Cmd_def)
      {     
         HCI_Write_Flow_Control_Mode_Cmd_fcn(HCI_Packet);            
      }
      else if (ocfogf== HCI_Read_Data_Block_Size_Cmd_def)
      {   
         HCI_Read_Data_Block_Size_Cmd_fcn(HCI_Packet);            
      }
      else if (ocfogf== HCI_Enhanced_Flush_Cmd_def)
      {   
         HCI_Enhanced_Flush_Cmd_fcn(HCI_Packet);         
      }
      else if (ocfogf== HCI_Flow_Spec_Modify_Cmd_def)
      {   
         HCI_Flow_Spec_Modify_Cmd_fcn(HCI_Packet);      
      }
      else if (ocfogf== HCI_Read_Local_AMP_Info_Cmd_def)
      {   
         HCI_Read_Local_AMP_Info_Cmd_fcn(HCI_Packet);
      }
      else if (ocfogf== HCI_AMP_Test_Cmd_def)
      {   
         HCI_AMP_Test_Cmd_fcn(HCI_Packet);      
      }
      else if (ocfogf== HCI_AMP_Test_End_Cmd_def)
      {   
         HCI_AMP_Test_End_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Read_Location_Data_Cmd_def)
      {   
         HCI_Read_Location_Data_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Write_Location_Data_Cmd_def)
      {   
         HCI_Write_Location_Data_Cmd_fcn(HCI_Packet);      
      }
      else if (ocfogf== HCI_Enable_AMP_Receiver_Report_Cmd_def)
      {   
         HCI_Enable_AMP_Receiver_Report_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Short_Range_Mode_Cmd_def)
      {   
         HCI_Short_Range_Mode_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Create_Physical_Link_Cmd_def)
      {   
         HCI_Create_Physical_Link_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Disconnect_Physical_Link_Cmd_def)
      {   
         HCI_Disconnect_Physical_Link_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Accept_Physical_Link_Cmd_def)
      {   
         HCI_Accept_Physical_Link_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Write_Remote_AMP_ASSOC_Cmd_def)
      {   
         HCI_Write_Remote_AMP_ASSOC_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Read_Local_AMP_ASSOC_Cmd_def)
      {   
         HCI_Read_Local_AMP_ASSOC_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Create_Logical_Link_Cmd_def)
      {   
         HCI_Create_Logical_Link_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Disconnect_Logical_Link_Cmd_def)
      {   
         HCI_Disconnect_Logical_Link_Cmd_fcn(HCI_Packet);         
      }
      else if (ocfogf== HCI_Accept_Logical_Link_Cmd_def)
      {   
         HCI_Accept_Logical_Link_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Logical_Link_Cancel_Cmd_def)
      {  
         HCI_Logical_Link_Cancel_Cmd_fcn(HCI_Packet);      
      }
      else if (ocfogf== HCI_Read_RSSI_Cmd_def)
      {   
         HCI_Read_RSSI_Cmd_fcn(HCI_Packet);      
      }
      else if (ocfogf== HCI_Set_Event_Mask_Cmd_def)
      {   
         HCI_Set_Event_Mask_Cmd_fcn(HCI_Packet);
      }
      else if (ocfogf== HCI_Host_Number_Of_Completed_Packets_Cmd_def)
      {     
         HCI_Host_Number_Of_Completed_Packets_Cmd_fcn(HCI_Packet);
      }
      else if (ocfogf== HCI_Set_Event_Mask_Page_2_Cmd_def)
      {  
         HCI_Set_Event_Mask_Page_2_Cmd_fcn(HCI_Packet);
      }
      else if (ocfogf== HCI_Set_Event_Filter_Cmd_def)
      {   
         HCI_Set_Event_Filter_Cmd_fcn(HCI_Packet);   
      }
      else if (ocfogf== HCI_Read_Link_Quality_Cmd_def)
      {   
         HCI_Read_Link_Quality_Cmd_fcn(HCI_Packet);            
      }
   }   
   
   //HCI data   
   else if (HCI_Packet[0]==0x02)
   {
      //Echo data
      //Read Data block size has to be issued before ending data
      HCI_Send_Data(HCI_Packet);
      
   }
   
   //Shall not be reached do a reset
   else 
   {
#ifdef DEBUG_COMMAND_PARSER
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Deafault state, should not be reached\n");
#endif
      HCI_Reset_Cmd_fcn(HCI_Packet);
   }
}   
//////////////////////////////////////////////////////////////////////
//////////////////////// FUNCTION DEFINITIONS ////////////////////////
//////////////////////////////////////////////////////////////////////
int HCI_Read_Flow_Control_Mode_Cmd_fcn(void* readBuf)
{   
   HCI_Read_Flow_Control_Mode_evt evt;
   char buf_uartP[8];
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Flow_Control_Mode_Cmd Received\n");
#endif	

   //Read the flow mode control configuration and prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=5;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c66; //OBS little endian
   evt.status=HCI_SUCCESS;
   evt.flow_control_mode=flow_control_mode_var;

   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[5]=evt.status;
   buf_uartP[7]=evt.flow_control_mode;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
  
   return 1;      
}

/////////////////////////////////////////////////////////////////////////
int HCI_Write_Flow_Control_Mode_Cmd_fcn(void* readBuf)
{      
   HCI_Write_Flow_Control_Mode_Cmd_struct *param;
   HCI_Write_Flow_Control_Mode_evt evt;
   char* temp;
   char buf_uartP[7];

   temp=(char*)readBuf;
   
   param=(HCI_Write_Flow_Control_Mode_Cmd_struct*)&temp[4];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Write_Flow_Control_Mode_Cmd Received\n");
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Flow Control Mode= %02x\n",param->flow_control_mode);
#endif

   //Update flow_control_mode_var variable
   //FLow Control Mode shall be set to 1, default for AMP controller
   if (param->flow_control_mode==0x01)
   {
      flow_control_mode_var=param->flow_control_mode;
      buf_uartP[6]=HCI_SUCCESS;
   }
   else
   {
      flow_control_mode_var=0x01;
      buf_uartP[6]=HCI_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
   }
   // prepare event
   evt.evt_hdr=4;
   evt.evt_code=0x0e;
   evt.length=4;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c67; //OBS little endian
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));

   return 1;      
}

/////////////////////////////////////////////////////////////////////////    
int HCI_Read_Buffer_Size_Cmd_fcn(void* param)
{   
   HCI_Read_Buffer_Size_evt evt;
   char buf_uartP[14];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Buffer_Size_Cmd Received\n");
#endif

   // prepare event
   evt.evt_hdr=4;
   evt.evt_code=0x0e;
   evt.length=0x08;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1005; //OBS little endian
   evt.status=0x000;
   evt.HC_ACL_Data_Packet_Length=maxAclDataPacketLength;
   evt.HC_Synchronous_Data_Packet_Length=0x00;
   evt.HC_Total_Num_ACL_Data_Packets=TOTAL_NUMBER_OF_DATA_BLOCKS;
   evt.HC_Total_Num_Synchronous_Data_Packets=0x0000; 

   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.HC_ACL_Data_Packet_Length;
   buf_uartP[8]=evt.HC_ACL_Data_Packet_Length/256;
   buf_uartP[9]=evt.HC_Synchronous_Data_Packet_Length;
   buf_uartP[10]=evt.HC_Total_Num_ACL_Data_Packets;
   buf_uartP[11]=evt.HC_Total_Num_ACL_Data_Packets/256;
   buf_uartP[12]=evt.HC_Total_Num_Synchronous_Data_Packets;
   buf_uartP[13]=evt.HC_Total_Num_Synchronous_Data_Packets/256;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif   
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
  
   return 1;      
}

/////////////////////////////////////////////////////////////////////////    
int HCI_Read_Data_Block_Size_Cmd_fcn(void* param)
{   
   HCI_Read_Data_Block_Size_evt evt;
   char buf_uartP[13];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Data_Block_Size_Cmd Received\n");
#endif
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x0a;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x100a; //OBS little endian
   evt.status=0x00;
   evt.Max_ACL_Data_Packet_Length=maxAclDataPacketLength;
   evt.Data_Block_Length=dataBlockLength;  
   evt.Total_Data_Blocks=TOTAL_NUMBER_OF_DATA_BLOCKS;  

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Max_ACL_Data_Packet_Length= %04x \n", maxAclDataPacketLength);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "dataBlockLength= %04x \n", dataBlockLength);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "TOTAL_NUMBER_OF_DATA_BLOCKS= %02x \n", TOTAL_NUMBER_OF_DATA_BLOCKS);
#endif
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.Max_ACL_Data_Packet_Length;
   buf_uartP[8]=evt.Max_ACL_Data_Packet_Length/256;
   buf_uartP[9]=evt.Data_Block_Length;
   buf_uartP[10]=evt.Data_Block_Length/256;
   buf_uartP[11]=evt.Total_Data_Blocks;
   buf_uartP[12]=evt.Total_Data_Blocks/256;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif  
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   return 1;      
}
/////////////////////////////////////////////////////////////////////////   
int Number_Of_Complete_Data_Bocks_Evt_fcn(int noOfCompeltedPackets)
{
   //this event is triggered by a data confirmation, can be done together with a timer

   HCI_Total_Number_Of_Complete_Data_Bocks evt;
   char buf_uartP[12];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Number_Of_Complete_Data_Bocks_Evt_fcn \n");
#endif

   //Prepare  event
   evt.evt_hdr=0x04;
   evt.evt_code=0x48;
   evt.length=0x0c;
   
   
   evt.Total_Num_Data_Blocks=totalNumDataBlocks;
   evt.Number_Of_Handles=0x01;
   //Check transmitetd packet for connection handle 
   evt.Handle=0x0001;
   //Number of complete packets and blocks are the same for this implementation
   //Number of completed datablocks are updated in ind from data callback
   evt.Num_Of_Completed_Packets=0x01;
   evt.Num_Of_Completed_Blocks=noOfCompeltedPackets;
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.Total_Num_Data_Blocks;
   buf_uartP[4]=evt.Total_Num_Data_Blocks>>8;
   buf_uartP[5]=evt.Number_Of_Handles;
   buf_uartP[6]=evt.Handle;
   buf_uartP[7]=evt.Handle>>8;
   buf_uartP[8]=evt.Num_Of_Completed_Packets;
   buf_uartP[9]=evt.Num_Of_Completed_Packets>>8;
   buf_uartP[10]=evt.Num_Of_Completed_Blocks;
   buf_uartP[11]=evt.Num_Of_Completed_Blocks>>8;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif   
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   
   return 1;      
}

////////////////////////////////////////////////////////////////////////// 
int HCI_Read_Local_Version_information_Cmd_fcn(void* readBuf)
{   
   HCI_Read_Local_Version_information_evt evt;
   char buf_uartP[15];
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Local_Version_information_Cmd Received\n");
#endif
   
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x0c;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1001; //OBS little endian
   evt.status=0x00;   
   evt.HCI_Version=0x01;
   evt.HCI_Revision=0x0000; //0x0100 for EDR controller
   evt.LMP_Version=0x01; //PAl version is 0x01
   evt.Manufacturer_Name=0xffff;//Unknown
   evt.LMP_Subversion=0x0001;  //First PAL version
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;   
   buf_uartP[7]=evt.HCI_Version;
   buf_uartP[8]=evt.HCI_Revision;
   buf_uartP[9]=evt.HCI_Revision>>8;
   buf_uartP[10]=evt.LMP_Version;
   buf_uartP[11]=evt.Manufacturer_Name;
   buf_uartP[12]=evt.Manufacturer_Name>>8;
   buf_uartP[13]=evt.LMP_Subversion;
   buf_uartP[14]=evt.LMP_Subversion>>8;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif   
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   
   return 1;      
}
/////////////////////////////////////////////////////////////////////////   
int HCI_Read_Local_Supported_Commands_Cmd_fcn(void* readBuf)
{   
   HCI_Read_Local_Supported_Commands_evt evt;
   char buf_uartP[71];
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Local_Supported_Commands_Cmd Received\n");
#endif	
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=68;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1002; //OBS little endian
   evt.status=0x00;
   
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;


   ReadSupportedCommands(&buf_uartP[0]);

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif  
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
  
   return 1;      
}
///////////////////////////////////////////////////////////////////////// 
int HCI_Read_Local_Supported_Features_Cmd_fcn(void* readBuf)
{   
   HCI_Read_Local_Supported_Features_evt evt;
   char buf_uartP[15];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Local_Supported_Features_Cmd Received\n");
#endif	
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1003; //OBS little endian
   evt.status=0x00;
   
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
    
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));   
 
   return 1;      
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Reset_Failed_Contact_Counter_Cmd_fcn(void* readBuf)
{   
   HCI_Reset_Failed_Contact_Counter_evt evt;
   HCI_Reset_Failed_Contact_Counter_Cmd_struct *param;
   char* temp;
     
   char buf_uartP[9];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Reset_Failed_Contact_Counter_Cmd Received\n");
#endif
    //Extract the handle parameter
   temp=(char*)readBuf;
   param=(HCI_Reset_Failed_Contact_Counter_Cmd_struct*)&temp[4];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT2(TR_AMP_HIGH_RES, "Connection handle= %02x %02x\n",temp[4],temp[5]);
#endif
   
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x06;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1402; //OBS little endian
   evt.status=0x00;
   evt.handle=param->handle;
   
   //45 position for contact counter
   //Needs to be i WiFi engive
   physicalLinkHandle[evt.handle] [RESET_FAILED_CONTACT_COUNTER]=0;

   //WiFi call to reset
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.handle;
   buf_uartP[8]=evt.handle/256;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif   

   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));   
 
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Set_Event_Mask_Cmd_fcn(void* readBuf)
{   
   HCI_Set_Event_Mask_evt evt;
   HCI_Set_Event_Mask_Cmd_struct *param;
   char *temp;
   char buf_uartP[7];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Set_Event_Mask_Cmd Received\n");
#endif 
      // Extract the Event mask parameter
   temp=(char*)readBuf;
   param=(HCI_Set_Event_Mask_Cmd_struct*)&temp[4];
   
   //Store Event mask
   memcpy(&eventMask[0], &temp[4], 8);

#ifdef DEBUG_COMMAND_PARSER	
   if(TRACE_ENABLED(TR_AMP_HIGH_RES))
   {
      int i;
      for (i=0; i<=7; i++)
      {
         DE_TRACE_INT(TR_AMP_HIGH_RES, "i= %02x \n",i);
         DE_TRACE_INT(TR_AMP_HIGH_RES, "EventMask= %02x \n",eventMask[i]);
      }
   }
#endif 	
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c01; //OBS little endian
   evt.status=0x00;

   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif 
   
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Host_Number_Of_Completed_Packets_Cmd_fcn(void* readBuf)
{      
   HCI_Host_Number_Of_Completed_Packets_Cmd_struct *param;
   char *temp;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Host_Number_Of_Completed_Packets_cmd Received\n");
#endif
   // Extract the  parameter and save them
   temp=(char*)readBuf;
   param=(HCI_Host_Number_Of_Completed_Packets_Cmd_struct*)&temp[4];
       
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Set_Event_Mask_Page_2_Cmd_fcn(void* readBuf)
{   
   HCI_Set_Event_Mask_Page_2_evt evt;
   HCI_Set_Event_Mask_Page_2_Cmd_struct *param;
   char *temp;
   char buf_uartP[7];
   
#ifdef DEBUG_COMMAND_PARSER	
   if(TRACE_ENABLED(TR_AMP_HIGH_RES))
   {
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Set_Event_Mask_Page_2_Cmd Received\n");
   }
#endif	

    // Extract the Event mask parameter
   temp=(char*)readBuf;
   param=(HCI_Set_Event_Mask_Page_2_Cmd_struct*)&temp[4];

   //Store Event mask
   memcpy(&eventMaskPage2[0], &temp[4], 8);
#ifdef DEBUG_COMMAND_PARSER	
   if(TRACE_ENABLED(TR_AMP_HIGH_RES))
   {
      int i;
      for (i=0; i<=8; i++)
      {
         DE_TRACE_INT(TR_AMP_HIGH_RES, "EventMask_P2= %02x \n",eventMaskPage2[i]);
      }
   }
#endif 	
   
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c63; //OBS little endian
   evt.status=0x00;

   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
    
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Enable_Device_Under_Test_Cmd_fcn(void* readBuf)
{   
   HCI_Enable_Device_Under_Test_evt evt;
   
   
   char buf_uartP[7];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Enable_Device_Under_Test_Cmd Received\n");
#endif	
   // Send the AMP test command here
   testMode=0x01;
    // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1803; //OBS little endian
   evt.status=0x00;

   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "\nSending Event");
#endif	
   
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Reset_Cmd_fcn(void* readBuf)
{   HCI_Reset_evt evt;
   
   
   char buf_uartP[7];
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Reset_Cmd Receiveed\n");
#endif	
   // Reset all WIFI links and states and variables here

   //Restore Transport
   WiFiEngine_Transport_Reset(netId, TRUE);
   
   //restore to default values
   Init_Command_Parser();
   
   //Restore Statemachine
   
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c03; //OBS little endian
   evt.status=0x00;
  
   //WiFiEngine_AMP_Transport_Reset   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP) );
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Read_Location_Data_Cmd_fcn(void* readBuf)
{   HCI_Read_Location_Data_evt evt;
      
   char buf_uartP[12];
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Location_Data_Cmd Received\n");
#endif	

   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x08;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c64; //OBS little endian
   evt.status=0x00;
   
   // Read all WIFI location data info  here
   evt.Location_Domain_Aware=Location_Domain_Aware;
   evt.Location_Domain=Location_Domain;
   evt.Location_Domain_Options=Location_Domain_Options;
   evt.Location_Options=Location_Options;

   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.Location_Domain_Aware;
   buf_uartP[8]=evt.Location_Domain;
   buf_uartP[9]=evt.Location_Domain>>8;
   buf_uartP[10]=evt.Location_Domain_Options;
   buf_uartP[11]=evt.Location_Options;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Write_Location_Data_Cmd_fcn(void* readBuf)
{   
   HCI_Write_Location_Data_evt evt;
   HCI_Write_Location_Data_Cmd_struct *param;
   char *temp;
   char buf_uartP[7];
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Write_Location_Data_Cmd Received\n");
#endif
          
   temp=(char*)readBuf;
   param=(HCI_Write_Location_Data_Cmd_struct*)&temp[4];
   
   
   //Save the WIFI domain parameters here
   //Any use of the parameters?
   Location_Domain_Aware    =   param->Location_Domain_Aware;
   Location_Domain         =   param->Location_Domain;
   Location_Domain_Options   =   param->Location_Domain_Options;
   Location_Options      =   param->Location_Options;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Location_Domain_Aware= %02x \n",Location_Domain_Aware);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Location_Domain= %02x \n",Location_Domain);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Location_Domain_Options= %02x \n",Location_Domain_Options);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Location_Options= %02x \n",Location_Options);
#endif	
      
    // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c65; //OBS little endian
   evt.status=0x00;

   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));   
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Read_Connection_Accept_Timeout_Cmd_fcn(void* readBuf)
{   
   HCI_Read_Connection_Accept_Timeout_evt evt;
      
   char buf_uartP[9];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Connection_Accept_Timeout_Cmd Received\n");
#endif    

    // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x06;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c15; //OBS little endian
   evt.status=0x00;
   // Read connection timeout here
   //evt.Conn_Accept_Timeout=0x0101;

   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=connectionAcceptTimeOutBBslots[1];
   buf_uartP[8]=connectionAcceptTimeOutBBslots[0];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));  
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Write_Connection_Accept_Timeout_Cmd_fcn(void* readBuf)
{   
   HCI_Write_Connection_Accept_Timeout_evt evt;
   HCI_Write_Connection_Accept_Timeout_Cmd_struct *param;
   char *temp;
   char buf_uartP[7];
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Write_Connection_Accept_Timeout_Cmd Received\n");
#endif	
    
   // Save the connection Timeout here
   temp=(char*)readBuf;
   param=(HCI_Write_Connection_Accept_Timeout_Cmd_struct*)&temp[4];
   // short_var=param->Conn_Accept_Timeout;
    // DE_TRACE_INT2(TR_AMP_HIGH_RES, "\nConnection Accept Timeout= %02x %02x",readBuf[4],readBuf[5]);
   
   //connection timeout in BB slots
   connectionAcceptTimeOutBBslots[0]=temp[5];
   connectionAcceptTimeOutBBslots[1]=temp[4];
   
   //connection timeout in ms
   connectionAcceptTimeOutMsec=((connectionAcceptTimeOutBBslots[1]*625/1000)+
      (connectionAcceptTimeOutBBslots[0]*0x0100*625/1000));
      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT2(TR_AMP_HIGH_RES, "Connection tmo BBSlots= %02x %02x\n", connectionAcceptTimeOutBBslots[0], 
                                                                         connectionAcceptTimeOutBBslots[1]);   
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Connection tmo Msec= %d\n",connectionAcceptTimeOutMsec);
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Set Connection tmo");
#endif	
   
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c16; //OBS little endian
   evt.status=0x00;

   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Read_Failed_Contact_Counter_Command_Cmd_fcn(void* readBuf)
{   
   HCI_Read_Failed_Contact_Counter_Command_evt evt;
   HCI_Read_Failed_Contact_Counter_Command_Cmd_struct *param;
   char *temp; 
   char buf_uartP[11];
 
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Failed_Contact_Counter_Command_Cmd Received\n");
#endif
   // Save the handler
   temp=(char*)readBuf;
   param=(HCI_Read_Failed_Contact_Counter_Command_Cmd_struct*)&temp[4];
    
   //WiFi Call to resd failed contact counter
   //evt.Failed_Contact_Counter=wifFi
    // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x08;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1401; //OBS little endian
   evt.status=0x00;
   evt.handle=param->handle;
   evt.Failed_Contact_Counter=0x0000;

   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.handle;
   buf_uartP[8]=evt.handle/256;
   buf_uartP[9]=evt.Failed_Contact_Counter;
   buf_uartP[10]=evt.Failed_Contact_Counter/256;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
    
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Read_Link_Supervision_Timeout_Cmd_fcn(void* readBuf)
{   
   HCI_Read_Link_Supervision_Timeout_evt evt;
   HCI_Read_Link_Supervision_Timeout_Cmd_struct *param;
   char *temp;
   char buf_uartP[11];
   int i=0;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Link_Supervision_Timeout_Cmd Received\n");
#endif

   // Save the handler
   temp=(char*)readBuf;
   param=(HCI_Read_Link_Supervision_Timeout_Cmd_struct*)&temp[4];
   
   //Get link supervision tmo for correct handle
   buf_uartP[10] = physicalLinkHandle[i][LINK_SUPERVISION_TMO];   
   buf_uartP[9] =   physicalLinkHandle[i][LINK_SUPERVISION_TMO+1];   

#ifdef DEBUG_COMMAND_PARSER	
   if(TRACE_ENABLED(TR_AMP_HIGH_RES))
   {
      int i=0;
      for (i=0; i<=50;i++)
      {
         DE_TRACE_INT(TR_AMP_HIGH_RES, "physicalLinkHandle = %02x\n",physicalLinkHandle[0][i]);
      }
   }
#endif	
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x08;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c36; //OBS little endian
   evt.status=0x00;
   evt.handle=param->handle;

   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.handle;
   buf_uartP[8]=evt.handle/256;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Write_Link_Supervision_Timeout_Cmd_fcn(void* readBuf)
{   
   HCI_Write_Link_Supervision_Timeout_evt evt;
   HCI_Write_Link_Supervision_Timeout_Cmd_struct *param;
   int linkSupervisionTmo;
   char buf_uartP[9];
   char *temp;
   int i=0;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Write_Link_Supervision_Timeout_Cmd Received\n");
#endif	
   // Save the handler and the timeout
   temp=(char*)readBuf;
   param=(HCI_Write_Link_Supervision_Timeout_Cmd_struct*)&temp[4];
     
   //Loop over link handles for several links
//   if (physicalLinkHandle[i][POSITION_PHYSICAL_LINK_HANDLE]==temp[4])
//   {
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Physical Link Handle OK\n");
#endif		
   //Store Supervision tmo   
   physicalLinkHandle[i][LINK_SUPERVISION_TMO]   = temp[7];
   physicalLinkHandle[i][LINK_SUPERVISION_TMO+1]   = temp[6];
//   }

   //Range for N: 0x0001 – 0xFFFF Time Range: 0.625ms - 40.9 sec
   //LinkSupervisionTmoBBslots[2];
   //In ms
   linkSupervisionTmo=(physicalLinkHandle[i][LINK_SUPERVISION_TMO]*0x100*625/1000+
                       physicalLinkHandle[i][LINK_SUPERVISION_TMO+1]*625/1000);
                  
   //Set Link Supervision tmo
   if (WiFiEngine_SetLinkSupervisionBeaconTimeout(linkSupervisionTmo)==WIFI_ENGINE_SUCCESS)
   {
#ifdef DEBUG_COMMAND_PARSER 		
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Set link supervision tmo OK\n");
#endif	
      evt.status=HCI_SUCCESS;
   }
   else
   {
#ifdef DEBUG_COMMAND_PARSER 		
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Set link supervision tmo Failed\n");
#endif	
      evt.status=HCI_UNSPECIFIED_ERROR;
   }

   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x06;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c37; //OBS little endian
   
   evt.handle=temp[5]*0x100+temp[4];

   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.handle;
   buf_uartP[8]=evt.handle/256;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif
    
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Short_Range_Mode_Cmd_fcn(void* readBuf)
{   
   //HCI_Short_Range_Mode_evt evt;
   HCI_Short_Range_Mode_Change_Complete_Evt evt;
   HCI_Short_Range_Mode_Cmd_struct *param;
   char buf_uartP[6];
   char *temp;
   int i=0;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Short_Range_Mode_Cmd_fcn Received\n");
#endif
   // Save the physical handler and the mode
   temp=(char*)readBuf;
   param=(HCI_Short_Range_Mode_Cmd_struct*)&temp[4];

   
   //link Handle not part of  WIFI_Engine_SetTxPowerLevel
   //Store value of shortrange mode
   physicalLinkHandle[i][SHORT_RANGE_MODE]=param->Short_Range_Mode;
   
   //Short range mode disabled 
   if (param->Short_Range_Mode==0x00)
   { 
#ifdef DEBUG_COMMAND_PARSER  
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Short raneg mode disabled\n");
#endif
      //Todo, is 0,0 correct for nominal output power
      WiFiEngine_SetTxPowerLevel(0,0);
      //Return success, no return to WiFi call
      evt.status=HCI_SUCCESS;
      evt.Short_Range_State=0x00;
   }
   //short range mode enabled
   else if (param->Short_Range_Mode==0x01)
   { 
#ifdef DEBUG_COMMAND_PARSER  
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Short raneg mode disabled\n");
#endif
      //Todo, update 17, 17 wth something else
      WiFiEngine_SetTxPowerLevel(17,17);
      //Return success, no return to WiFi call
      evt.status=HCI_SUCCESS;
      evt.Short_Range_State=0x01;
   }
   else
   {
      evt.Short_Range_State=0x00;
      evt.status=HCI_INVALID_HCI_COMMAND_PARAMETERS;
   }
   //Set 4db output power
   
    // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x4c;
   evt.length=0x03;
   evt.Physical_Link_Handle=0x01;


   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.status;
   buf_uartP[4]=evt.Physical_Link_Handle;
   buf_uartP[5]=evt.Short_Range_State;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Set_Event_Filter_Cmd_fcn(void* readBuf)
{   HCI_Set_Event_Filter_evt evt;
   HCI_Set_Event_Filter_Cmd_struct *param;
   char *temp;
   char buf_uartP[7];
 
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Set_Event_Filter_Cmd Received\n");
#endif	
    
   // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Set_Event_Filter_Cmd_struct*)&temp[4];

   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c05; //OBS little endian
   evt.status=HCI_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE; //command disallowed

//check the filter_type
   switch (param->Filter_Type)
   {
      case CLEAR_ALL_FILTERS:
             // prepare event (command complete
         evt.status=HCI_SUCCESS;
      break;
      case INQUIRY_RESULTS:
         break;
      case CONNECTION_SETUP:
         switch (param->Filter_Condition_Type)
         {   
            case ALLOW_CONNECTIONS_FROM_ALL: // Allow Connections from all devices.
            {
               evt.status=HCI_SUCCESS; //command succesful
               switch (param->Condition)
               {   
                  case NO_AUTO_ACCEPT:         //Do NOT Auto accept the connection. (Auto accept is off)
                     //Not used
                     //Auto_Accept_Flag=NO_AUTO_ACCEPT;
                     break;
                  case AUTO_ACCEPT_ROLE_SWITCH:   //Do Auto accept the connection with role switch disabled. (Auto accept is on).
                     //Auto_Accept_Flag=AUTO_ACCEPT_ROLE_SWITCH;
                     break;
                  case NO_AUTO_ACCEPT_ROLE_SWITCH: //Do Auto accept the connection with role switch enabled. (Auto accept is on).
                     //Auto_Accept_Flag=NO_AUTO_ACCEPT_ROLE_SWITCH;
                     break;
                  default:
                     // 0x04-0xFF Reserved for Future Use
                     break;            
               }
            }
            break;
            
            case ALLOW_SPECIFIC_COD: // Allow Connections from a device with a specific Class of Device.
            {
               switch (param->Condition)
               {   
                  //Not corect size
                  /*case COD_INTEREST: //Class of Device of Interest.
                  //accept this connection request
                     break;
                  default:
                     // denie all other connections
                  break;*/
               }
               break;
            }
            break;
            
            case ALLOW_SPECIFIC_BDADDR: //Allow Connections from a device with a specific BD_ADDR.
            break;
         default:
            // 0x03-0xFF Reserved for Future Use
               break;         
         }
         break;
      default:
         // 0x03-0xFF Reserved for Future Use
         break;
   }



      
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
       
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Read_Local_AMP_Info_Cmd_fcn(void* readBuf)
{   HCI_Read_Local_AMP_Info_evt evt;
   
   char buf_uartP[37];
 
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Local_AMP_Info_Cmd Received\n");
#endif
   
    //prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x22;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1409; //OBS little endian
   evt.status=0x00;
   evt.AMP_Status=AMP_CONTROLLER_BT;
   evt.Total_Bandwidth=totalBandwidth;
   evt.Max_Guaranteed_Bandwidth=maxGuaranteedBandwidth;
   evt.Min_Latency=minLatency;
   evt.Max_PDU_Size=maxPDUSize;
   evt.Controller_Type=AMP_CONTROLLER_TYPE;
   evt.PAL_Capabilities=palCapabilities;
   evt.Max_AMP_ASSOC_Length=maxAmpAssocLength;
   evt.Max_Flush_Timeout=maxFlushTmo;
   //evt.Best_Effort_Flush_Timeout=bestEffortFlushTmo; 
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.AMP_Status;
   buf_uartP[8]=evt.Total_Bandwidth;
   buf_uartP[9]=evt.Total_Bandwidth>>8;
   buf_uartP[10]=evt.Total_Bandwidth>>16;
   buf_uartP[11]=evt.Total_Bandwidth>>24;
   buf_uartP[12]=evt.Max_Guaranteed_Bandwidth;
   buf_uartP[13]=evt.Max_Guaranteed_Bandwidth>>8;
   buf_uartP[14]=evt.Max_Guaranteed_Bandwidth>>16;
   buf_uartP[15]=evt.Max_Guaranteed_Bandwidth>>24;
   buf_uartP[16]=evt.Min_Latency>>0;
   buf_uartP[17]=evt.Min_Latency>>8;
   buf_uartP[18]=evt.Min_Latency>>16;
   buf_uartP[19]=evt.Min_Latency>>24;
   buf_uartP[20]=evt.Max_PDU_Size;
   buf_uartP[21]=evt.Max_PDU_Size>>8;
   buf_uartP[22]=evt.Max_PDU_Size>>16;
   buf_uartP[23]=evt.Max_PDU_Size>>24;
   buf_uartP[24]=evt.Controller_Type;
   buf_uartP[25]=evt.PAL_Capabilities;
   buf_uartP[26]=evt.PAL_Capabilities>>8;
   buf_uartP[27]=evt.Max_AMP_ASSOC_Length;
   buf_uartP[28]=evt.Max_AMP_ASSOC_Length>>8;
   buf_uartP[29]=evt.Max_Flush_Timeout;
   buf_uartP[30]=evt.Max_Flush_Timeout>>8;
   buf_uartP[31]=evt.Max_Flush_Timeout>>16;
   buf_uartP[32]=evt.Max_Flush_Timeout>>24;
   buf_uartP[33]=physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT+3];
   buf_uartP[34]=physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT+2];
   buf_uartP[35]=physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT+1];
   buf_uartP[36]=physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif
       
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}

int HCI_AMP_Receiver_Report_Evt_fcn(char reason)
{   
   HCI_AMP_Receiver_Report_evt evt;
   
   evt.evt_hdr=0x04;
   evt.evt_code=0x4b;
   evt.length=0x04;
   evt.controller_type=0x01;
   evt.reason=reason;
   evt.event_type=0x00000000;
   evt.Number_Of_Frames=0x0000;
   evt.Number_Of_Error_Frames=0x0000;
   evt.Number_Of_Bits=0x00000000;
   evt.Number_Of_Error_Bits=0x00000000;
   
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_AMP_Test_End_Cmd_fcn(void* readBuf)
{   
   HCI_AMP_Test_End_evt evt;
   
   char buf_uartP[7];
   
          
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_AMP_Test_End_Cmd Received\n");
#endif	
   if (testMode==0x01) 
   {
      evt.status=HCI_SUCCESS;
   }
   else
   {
      evt.status=COMMAND_DISALLOWED;
   }

   HCI_AMP_Receiver_Report_Evt_fcn(0x01);
   
    // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1808; //OBS little endian //1809
   
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////
int HCI_AMP_Test_Cmd_fcn(void* readBuf)
{   HCI_AMP_Test_End_evt evt;
   HCI_AMP_Test_Cmd_struct *param;
   char *temp;
   char buf_uartP[7];

   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_AMP_Test_Cmd Received\n");
#endif	
   // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_AMP_Test_Cmd_struct*)&temp[4];
   // charr=param->Controller_type;
   // DE_TRACE_INT(TR_AMP_HIGH_RES, "\nController Type= %02x",readBuf[4]);
    
   if (testMode==0x01) 
   {
      evt.status=HCI_SUCCESS;
   }
   else
   {
      evt.status=COMMAND_DISALLOWED;
   }

   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1809; //OBS little endian //1808
   //evt.status=0x00;
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
    
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Enable_AMP_Receiver_Report_Cmd_fcn(void* readBuf)
{   HCI_Enable_AMP_Receiver_Report_evt evt;
   HCI_Enable_AMP_Receiver_Report_Cmd_struct *param;
   char buf_uartP[7];
   char* temp;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Enable_AMP_Receiver_Report_Cmd Received\n");
#endif
   if (testMode==0x01) 
   {
     // Save the parameters
      temp=(char*)readBuf;
      param=(HCI_Enable_AMP_Receiver_Report_Cmd_struct*)&temp[4];
      evt.status=HCI_SUCCESS;
   }
   else
   {
      evt.status=COMMAND_DISALLOWED;
   }   
    // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1807; //OBS little endian
   
   
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
    
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Accept_Logical_Link_Cmd_fcn(void* readBuf)
{   HCI_Accept_Logical_Link_status_evt evt;
   HCI_Accept_Logical_Link_complete_evt evt2;
   HCI_Accept_Logical_Link_Cmd_struct *param;
   char *temp;
   int i=0;
   char buf_uartP[7];
   char buf_uartC[8];
   char txFlow;
   int RegisterdTimer=3;
            
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Accept_Logical_Link_Cmd_fcn\n");
#endif	
   logicalLinkCancelSent=FALSE;
    // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Accept_Logical_Link_Cmd_struct*)&temp[4];
   

   while (physicalLinkHandle[i][POSITION_PHYSICAL_LINK_HANDLE]!=temp[4] && i<=(char)MAX_NUMBER_OF_PHYSICAL_LINKS)
   {
      i++;
   }
   if (i==MAX_NUMBER_OF_PHYSICAL_LINKS)
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "No physical link handle\n");
#endif	
      evt.status=COMMAND_DISALLOWED;
   }
   
//TX: Check for extended flow identifier 0x01 for Best Effort
   if (temp[20]==0x01)
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Identifier best effort\n");
#endif	
      evt.status=HCI_SUCCESS;
   }
   else
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Parameter not allowed\n");
#endif
      evt.status=HCI_INVALID_HCI_COMMAND_PARAMETERS;
   }
      
//Check service type
   if (temp[19]==BEST_EFFORT)
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Servie Type best effort\n");
#endif	
   }
   else if (temp[19]==GUARANTEED)
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Parameter not allowed\n");
#endif
      evt.status=HCI_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
   }   

//Check Maximun SDU   
   txMaxSDU=((temp[18]>>8)+(temp[17]));
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "tx Received Params = %02x\n",txMaxSDU); 
#endif
   txMaxSDU=((temp[18]<<8)+(temp[17]));
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "tx Received Params = %02x\n",txMaxSDU); 
#endif	

//SDU Inter arrival time
   txSduInterArrivalTime =((temp[16]>>24)+(temp[15]>>16)+(temp[14]>>8)+(temp[13]));
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "tx Received Params = %02x\n",txMaxSDU); 
#endif	
//Calculate flow spec   
   txFlow=   txMaxSDU*1000000/txSduInterArrivalTime;
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "tx Flow = %02x\n",txFlow); 
#endif	


//RX: Check for extended flow identifier 0x01 for Best Effort
   if (temp[36]==0x01)
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Identifier best effort\n");
#endif	
      evt.status=HCI_SUCCESS;
   }
   else
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Parameter not allowed\n");
#endif
      evt.status=HCI_INVALID_HCI_COMMAND_PARAMETERS;
   }
   
   
//Check service type
   if (temp[35]==BEST_EFFORT)
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Servie Type best effort\n");
#endif	
   }
   else if (temp[35]==GUARANTEED)
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Parameter not allowed\n");
#endif
      evt.status=HCI_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
   }   

//Check Maximun SDU   
    rxMaxSDU=((temp[34]>>8)+(temp[33]));
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Received Params = %02x\n",rxMaxSDU); 
#endif
   rxMaxSDU=((temp[34]<<8)+(temp[33]));
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Received Params = %02x\n",rxMaxSDU); 
#endif	

//SDU Inter arrival time
   rxSduInterArrivalTime = ((temp[32]>>24)+(temp[31]>>16)+(temp[30]>>8)+(temp[29]));

//Start new timer if logical link process shall continue
   if (evt.status==HCI_SUCCESS)
   {
   //Create new timer
      logicalLinkConnectionTimerRunning=TRUE;
      if (DriverEnvironment_GetNewTimer(&connectionTimer, FALSE)!=DRIVERENVIRONMENT_SUCCESS)
      {
#ifdef DEBUG_COMMAND_PARSER		
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Failed to allocate timer\n");
#endif		
      }
      else
      {
#ifdef DEBUG_COMMAND_PARSER		
          DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer started\n");
#endif	
         //Register callback for timer, not repeating
         RegisterdTimer=DriverEnvironment_RegisterTimerCallback(logicalLinkAcceptTimeout, connectionTimer, 
            (de_callback_t)Logical_Link_Timer_Callback, 0);
         switch(RegisterdTimer)
         {
            case (0):
            {
#ifdef DEBUG_COMMAND_PARSER				
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer busy\n");
#endif				
            }
            break;
            
            case (-1):
            {
#ifdef DEBUG_COMMAND_PARSER				
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer FIALED\n");
#endif				
            }
            break;

            case (1):
            {
#ifdef DEBUG_COMMAND_PARSER				
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer SUCCESS\n");
#endif				
            }
            break;
            
            default:
#ifdef DEBUG_COMMAND_PARSER				
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer UnExpected\n");
#endif				
            break;
         }      
      }
   }

   //Todo  WIFI inter communicatiuon if neccasary
   
    // prepare status event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0f;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0439; //OBS little endian
   
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.status;
   buf_uartP[4]=evt.num_HCI_pkts;
   buf_uartP[5]=evt.opcode;
   buf_uartP[6]=evt.opcode/256;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Status Event\n");
#endif	
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));

   // prepare accept logical link complete event
   evt2.evt_hdr=0x04;
   evt2.evt_code=0x45;
   evt2.length=0x05;
   evt2.status=0x00;
   evt2.logical_link_handle=0x0001;
   evt2.phy_link_handle=0x01;
   evt2.TX_flow_spec_ID=0x01;    
   
   //copy the event to a char array befor sending it
   buf_uartC[0]=evt2.evt_hdr;
   buf_uartC[1]=evt2.evt_code;
   buf_uartC[2]=evt2.length;
   buf_uartC[3]=evt2.status;
   buf_uartC[4]=evt2.logical_link_handle;
   buf_uartC[5]=evt2.logical_link_handle/256;
   buf_uartC[6]=evt2.phy_link_handle;
   buf_uartC[7]=evt2.TX_flow_spec_ID;

   //Check if logical li9nk cancel complete has been sent
   if (logicalLinkCancelSent==FALSE)
   {
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Status Event\n");
#endif	
      //If timer running stop and free timer, send complete event 
      if (logicalLinkConnectionTimerRunning==TRUE)
      {
#ifdef DEBUG_COMMAND_PARSER	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Logical link OK\n");
#endif	
         DriverEnvironment_CancelTimer(connectionTimer);
         Check_Event_Mask(netId, buf_uartC, sizeof(buf_uartC));
         logicalLinkConnectionTimerRunning=FALSE;
      }
      else
      {
#ifdef DEBUG_COMMAND_PARSER	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Logical link Tmo\n");
#endif	
         evt2.status=HCI_CONNECTION_ACCEPT_TIMEOUT_EXCEEDED;
         buf_uartC[3]=evt2.status;
         Check_Event_Mask(netId, buf_uartC, sizeof(buf_uartC));
      }
      //Free timer
      DriverEnvironment_FreeTimer(connectionTimer);
      logicalLinkConnectionTimerRunning=FALSE;
   }
   else
   {
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Logical link cancel has been sent\n");
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "No complete event for create physical link\n");
#endif	
   }
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Create_Logical_Link_Cmd_fcn(void* readBuf)
{   
   HCI_Accept_Logical_Link_status_evt evt;
   HCI_Create_Logical_Link_complete_evt evt2;
   HCI_Create_Logical_Link_Cmd_struct *param;
   char txFlow;
   int RegisterdTimer=3;
   char *temp;
   int i=0;
   char buf_uartP[7];
   char buf_uartC[8];
 
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Create_Logical_Link_Cmd Received\n");
#endif	
   logicalLinkCancelSent=FALSE;
    // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Create_Logical_Link_Cmd_struct*)&temp[4];

   // prepare status event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0f;
   evt.length=0x04;
   evt.status=0x00;
   evt.num_HCI_pkts=0x01; 
   evt.opcode=0x0438; //OBS little endian
   
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.status;
   buf_uartP[4]=evt.num_HCI_pkts;
   buf_uartP[5]=evt.opcode;
   buf_uartP[6]=evt.opcode/256;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Status Event\n");  
#endif	
    
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   
//Check that physical link handle exists
while (physicalLinkHandle[i][POSITION_PHYSICAL_LINK_HANDLE]!=temp[4] && i<=MAX_NUMBER_OF_PHYSICAL_LINKS)
   {
      i++;
   }
   if (i==MAX_NUMBER_OF_PHYSICAL_LINKS)
   {
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "No physical link handle\n");   
#endif	
      evt2.status=COMMAND_DISALLOWED;
   }
   else 
   
   {
      //function for generating logical link handles
      physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE]=0x00;
      physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE+1]=0x01;
   }   
//TX: Check for extended flow identifier 0x01 for Best Effort
   if (temp[20]==0x01)
   {
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Identifier best effort\n");   
#endif	
      evt2.status=HCI_SUCCESS;
   }
   else
   {  
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Parameter not allowed\n");
#endif
      evt2.status=HCI_INVALID_HCI_COMMAND_PARAMETERS;
   }
   
//Check service type
   if (temp[19]==BEST_EFFORT)
   {
#ifdef DEBUG_COMMAND_PARSER	
       DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Servie Type best effort\n");  
#endif	
   }
   else if (temp[19]==GUARANTEED)
   {
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Parameter not allowed\n");
#endif
      evt2.status=HCI_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
   }   

//Check Maximun SDU   
   txMaxSDU=((temp[18]>>8)+(temp[17]));
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "tx Received Params = %02x\n", txMaxSDU); 
#endif
   txMaxSDU=((temp[18]<<8)+(temp[17]));
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "tx Received Params = %02x\n", txMaxSDU); 
#endif	

//SDU Inter arrival time
   txSduInterArrivalTime =((temp[16]>>24)+(temp[15]>>16)+(temp[14]>>8)+(temp[13]));   
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "tx Received Params = %02x\n", txMaxSDU); 
#endif	
//Calculate flow spec   
   txFlow=   txMaxSDU*1000000/txSduInterArrivalTime;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "tx Flow = %02x\n", txFlow); 
#endif
//RX: Check for extended flow identifier 0x01 for Best Effort
   if (temp[36]==0x01)
   {
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Identifier best effort\n");
#endif	
      evt2.status=HCI_SUCCESS;
   }
   else
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Parameter not allowed\n");
#endif
      evt2.status=HCI_INVALID_HCI_COMMAND_PARAMETERS;
   }
   
   
//Check service type
   if (temp[35]==BEST_EFFORT)
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Servie Type best effort\n");
#endif	
   }
   else if (temp[35]==GUARANTEED)
   {
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Parameter not allowed\n");
#endif
      evt2.status=HCI_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE;
   }   

//Check Maximun SDU   
   rxMaxSDU=((temp[34]>>8)+(temp[33]));
   
   
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Received Params = %02x\n", rxMaxSDU); 
#endif
   rxMaxSDU=((temp[34]<<8)+(temp[33]));
   
#ifdef DEBUG_COMMAND_PARSER	
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Received Params = %02x\n", rxMaxSDU); 
#endif	

   //SDU Inter arrival time
   rxSduInterArrivalTime = ((temp[32]>>24)+(temp[31]>>16)+(temp[30]>>8)+(temp[29]));
   
   //Start new timer if logical link process shall continue
   if (evt.status==HCI_SUCCESS)
   {
   //Create new timer
      logicalLinkConnectionTimerRunning=TRUE;
      if    (DriverEnvironment_GetNewTimer(&connectionTimer, FALSE)!=DRIVERENVIRONMENT_SUCCESS)
      {
#ifdef DEBUG_COMMAND_PARSER		
          DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Failed to allocate timer\n");
#endif		
      }
      else
      {
#ifdef DEBUG_COMMAND_PARSER		
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer started\n");
#endif	
         //Register callback for timer, not repeating
         RegisterdTimer=DriverEnvironment_RegisterTimerCallback(logicalLinkAcceptTimeout, connectionTimer, 
            (de_callback_t)Logical_Link_Timer_Callback, 0);
         switch(RegisterdTimer)
         {
            case (0):
            {
#ifdef DEBUG_COMMAND_PARSER				
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer busy\n");
#endif				
            }
            break;
            
            case (-1):
            {
#ifdef DEBUG_COMMAND_PARSER				
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer FIALED\n");
#endif				
            }
            break;

            case (1):
            {
#ifdef DEBUG_COMMAND_PARSER				
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer SUCCESS\n");
#endif				
            }
            break;
            
            default:
#ifdef DEBUG_COMMAND_PARSER				
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer UnExpected\n");
#endif				
            break;
         }      
      }
   }
   else
   {
   //remove logical link handle
      physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE]=0x00;
      physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE+1]=0x00;
   }

   // prepare logical link complete event
   evt2.evt_hdr=0x04;
   evt2.evt_code=0x45;
   evt2.length=0x05;
   // evt2.status=0x00;
   evt2.logical_link_handle=0x0001;
   evt2.phy_link_handle=0x01;
   evt2.TX_flow_spec_ID=temp[20];// ID for Tx 0x01;    

   //copy the event to a char array befor sending it
   buf_uartC[0]=evt2.evt_hdr;
   buf_uartC[1]=evt2.evt_code;
   buf_uartC[2]=evt2.length;
   buf_uartC[3]=evt2.status;
   buf_uartC[4]=evt2.logical_link_handle;
   buf_uartC[5]=evt2.logical_link_handle/256;
   buf_uartC[6]=evt2.phy_link_handle;
   buf_uartC[7]=evt2.TX_flow_spec_ID;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Command Complete Event\n");
#endif	
       
   //Check that LogicalLink Cancel complete hasn not been sent
   if (logicalLinkCancelSent==FALSE)
   {
      if (logicalLinkConnectionTimerRunning==TRUE)
      {
#ifdef DEBUG_COMMAND_PARSER	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Logical link OK\n");
#endif	
         DriverEnvironment_CancelTimer(connectionTimer);
         Check_Event_Mask(netId, buf_uartC, sizeof(buf_uartC));
         logicalLinkConnectionTimerRunning=FALSE;
      }
      else
      {
#ifdef DEBUG_COMMAND_PARSER	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Logical link Tmo\n");
#endif	
         evt2.status=HCI_CONNECTION_ACCEPT_TIMEOUT_EXCEEDED;
         buf_uartC[3]=evt2.status;
         Check_Event_Mask(netId, buf_uartC, sizeof(buf_uartC));
      }
      //Free timer
      DriverEnvironment_FreeTimer(connectionTimer);
      logicalLinkConnectionTimerRunning=FALSE;
   }
   else
   {
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Logical link cancel has been sent\n");
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "No complete event for create physical link\n");
#endif	
   }
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Disconnect_Logical_Link_Cmd_fcn(void* readBuf)
{   HCI_Disconnect_Logical_Link_status_evt evt;
   HCI_Disconnect_Logical_Link_complete_evt evt2;
   HCI_Disconnect_Logical_Link_Cmd_struct *param;
   char *temp;
   char buf_uartP[7];
   char buf_uartC[7];
   int i=0;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Disconnect_Logical_Link_Cmd Received\n");
#endif	
   // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Disconnect_Logical_Link_Cmd_struct*)&temp[4];
   evt.status=HCI_SUCCESS;
   evt2.logical_link_handle=0x0000;
   
   //Check for logical logocal link handle
   while (i<=MAX_NUMBER_OF_PHYSICAL_LINKS)
   {
      if (physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE]==temp[4+1] && 
         physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE+1]==temp[4])
      {   
#ifdef DEBUG_COMMAND_PARSER	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Logical link handle found\n");
#endif		
         evt.status=HCI_SUCCESS;
         evt2.logical_link_handle=((physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE+1]<<8)+
         (physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE]));
      }
      
      i++;
   }
   if (evt.status==HCI_SUCCESS)
   {
   
   }   
   else
   {
      //Handle not found
      evt2.logical_link_handle=((temp[4+1]<<8)+temp[4]);
      evt.status=HCI_INVALID_HCI_COMMAND_PARAMETERS;
   }

    // prepare status event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0f;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x043a; //OBS little endian
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.status;
   buf_uartP[4]=evt.num_HCI_pkts;
   buf_uartP[5]=evt.opcode;
   buf_uartP[6]=evt.opcode/256;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Status Event\n");
#endif	
    
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
    
   // prepare disconenct logical link complete event
   evt2.evt_hdr=0x04;
   evt2.evt_code=0x46;
   evt2.length=0x04;
   evt2.status=0x00;
   evt2.reason=0x01;   
   
   //copy the event to a char array befor sending it
   buf_uartC[0]=evt2.evt_hdr;
   buf_uartC[1]=evt2.evt_code;
   buf_uartC[2]=evt2.length;
   buf_uartC[3]=evt2.status;
   buf_uartC[4]=evt2.logical_link_handle;
   buf_uartC[5]=evt2.logical_link_handle/256;
   buf_uartC[6]=evt2.reason;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Command Complete Event\n");
#endif	
   
   Check_Event_Mask(netId,  buf_uartC, sizeof(buf_uartC));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Logical_Link_Cancel_Cmd_fcn(void* readBuf)
{   
   HCI_Logical_Link_Cancel_evt evt;
   HCI_Logical_Link_Cancel_Cmd_struct *param;
   char logicalLinkHandle;
   char txFlowSpec;
   char *temp;
   char buf_uartP[9];
   int i=0;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Logical_Link_Cancel_Cmd Received\n");
#endif	
   
    // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Logical_Link_Cancel_Cmd_struct*)&temp[4];

   logicalLinkHandle=temp[4]+ temp[5]*0;
   txFlowSpec=temp[5];
   //Check if logical connection is ongoing   
   if (logicalLinkConnectionTimerRunning==TRUE)   
   {
      //Check if handle exists
      while (physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE]!=temp[4] && i<=MAX_NUMBER_OF_PHYSICAL_LINKS)
      {
         i++;
      }
      if (i==MAX_NUMBER_OF_PHYSICAL_LINKS)
      {
#ifdef DEBUG_COMMAND_PARSER	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "No physical link handle\n");
#endif	
         evt.status=COMMAND_DISALLOWED;
      }
      else 
      {   
         evt.status=HCI_SUCCESS;
#ifdef DEBUG_COMMAND_PARSER	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Physical Link handle found\n");
#endif
         DriverEnvironment_CancelTimer(connectionTimer);
      }
   }   
   else 
   {
      evt.status=COMMAND_DISALLOWED;
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "No logical link setup in ongoing\n");
#endif
   }
   
    // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x06;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x043b; //OBS little endian
   evt.Physical_Link_Handle=logicalLinkHandle;
   evt.Tx_Flow_Spec_ID=txFlowSpec;
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.Physical_Link_Handle;
   buf_uartP[8]=evt.Tx_Flow_Spec_ID;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
    
   logicalLinkCancelSent=TRUE;
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Read_logical_Link_Accept_Timeout_Cmd_fcn(void* param)
{   
   HCI_Read_logical_Link_Accept_Timeout_evt evt;
   char buf_uartP[9];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_logical_Link_Accept_Timeout_Cmd Received\n");
#endif	
   
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x06;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0C61; //OBS little endian
   evt.status=0x00;
   evt.Logical_Link_Accept_Timeout=0x0001;
      
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=logicalLinkAcceptTimeout;
   buf_uartP[8]=logicalLinkAcceptTimeout>>8;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Write_logical_Link_Accept_Timeout_Cmd_fcn(void* readBuf)
{   
   HCI_Write_logical_Link_Accept_Timeout_evt evt;
   HCI_Write_logical_Link_Accept_Timeout_struct *param;   
   char *temp;
   char buf_uartP[7];
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Write_logical_Link_Accept_Timeout_Cmd Received\n");
#endif
   
   // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Write_logical_Link_Accept_Timeout_struct*)&temp[4];
   
   logicalLinkAcceptTimeout=((temp[5]<<8)+(temp[4]));
   logicalLinkAcceptTimeoutMs=(((temp[5]<<8)+(temp[4]))*625/1000);
     
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "logicalLinkAcceptTimeout = %04x\n", logicalLinkAcceptTimeout);
#endif	

    // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0C62; //OBS little endian
   evt.status=HCI_SUCCESS;
      
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Read_Best_Effort_Flush_Timeout_Cmd_fcn(void* readBuf)
{   
   HCI_Read_Best_Effort_Flush_Timeout_evt evt;
   HCI_Read_Best_Effort_Flush_Timeout_Cmd_struct *param;
   char *temp;   
   char buf_uartP[11];
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Best_Effort_Flush_Timeout_Cmd Received\n");
#endif	
   // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Read_Best_Effort_Flush_Timeout_Cmd_struct*)&temp[4];
   
   //Check logical link handle

   //physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE]
    
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x08;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0C69; //OBS little endian
   evt.status=0x00;
   //evt.Best_Effort_Flush_Timeout=0x00000001;
      
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT+3];
   buf_uartP[8]=physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT+2];
   buf_uartP[9]=physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT+1];
   buf_uartP[10]=physicalLinkHandle[0][POSITION_BEST_EFFORT_FLUSH_TIME_OUT];

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
    
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Write_Best_Effort_Flush_Timeout_Cmd_fcn(void* readBuf)
{   
   HCI_Write_Best_Effort_Flush_Timeout_evt evt;
   HCI_Write_Best_Effort_Flush_Timeout_Cmd_struct *param;   
   char* temp;
   short linkHandle;
   char buf_uartP[7];
   int i=0;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Write_Best_Effort_Flush_Timeout_Cmd Received\n");
#endif	
   // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Write_Best_Effort_Flush_Timeout_Cmd_struct*)&temp[4];
   
   //check if valid handle
   linkHandle=param->Logical_Link_Handle;
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Handle= %04x x\n", linkHandle);
#endif	
   
   
   //Include  check for several links
#ifdef DEBUG_COMMAND_PARSER
   if (((physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE+1]<<8) + physicalLinkHandle[i][POSITION_LOGICAL_LINK_HANDLE])==linkHandle)
        DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Handle OK\n");
#endif	
   

   //check for default value for BE flush tmo
   bestEffortFlushTmoMicroSec=temp[6]+(temp[7]<<8)+(temp[8]<<16)+(temp[9]<<24);
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "bestEffortFlushTmo %08x \n", bestEffortFlushTmoMicroSec);
#endif	
   bestEffortFlushTmoMsec=bestEffortFlushTmoMicroSec/1000;
   
   //Maped?
   physicalLinkHandle[i] [POSITION_BEST_EFFORT_FLUSH_TIME_OUT]=temp[9];
   physicalLinkHandle[i] [POSITION_BEST_EFFORT_FLUSH_TIME_OUT+1]=temp[8];
   physicalLinkHandle[i] [POSITION_BEST_EFFORT_FLUSH_TIME_OUT+2]=temp[7];
   physicalLinkHandle[i] [POSITION_BEST_EFFORT_FLUSH_TIME_OUT+3]=temp[6];
   
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x04;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0C60; //OBS little endian
   evt.status=0x00;
         
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif
   
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Enhanced_Flush_Cmd_fcn(void* readBuf)
{   
   //Only currently supported packet type for AMP in 3.0 spec is automatic flushable
   //Meaning all packets will be flushed
   //
   HCI_Enhanced_Flush_status_evt evt;
   HCI_Enhanced_Flush_complete_evt evt2;
   HCI_Enhanced_Flush_Cmd_struct *param;
   char *temp;
   char buf_uartP[7];
   char buf_uartC[5];
   int i=0;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Enhanced_Flush_Cmd Received\n");
#endif
   // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Enhanced_Flush_Cmd_struct*)&temp[4];
   
    // prepare status event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0f;
   evt.length=0x04;
   evt.status=0x00;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0c5f; //OBS little endian
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.status;
   buf_uartP[4]=evt.num_HCI_pkts;
   buf_uartP[5]=evt.opcode;
   buf_uartP[6]=evt.opcode/256;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Status Event\n");
#endif	
   
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
    
   //For AMP all data shall be flushed
   WiFiEngine_Transport_Flush(netId);
   
   //Reset data handler
   while (i<TOTAL_NUMBER_OF_DATA_BLOCKS)
   {
      if (dataBlockMAtrix[i].status == WAIT_CONFIRM)
      {
         dataBlockMAtrix[i].status = DATA_FLUSHED;
#ifdef DEBUG_COMMAND_PARSER	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Data packet flushed\n");
#endif	
      }
      i++;
   }
   
   // prepare complete event
   evt2.evt_hdr=0x04;
   evt2.evt_code=0x39;
   evt2.length=0x02;
   evt2.handle=0x00001; 
   
   //copy the event to a char array befor sending it
   buf_uartC[0]=evt2.evt_hdr;
   buf_uartC[1]=evt2.evt_code;
   buf_uartC[2]=evt2.length;
   buf_uartC[3]=evt2.handle;
   buf_uartC[4]=evt2.handle/256;   
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Command Complete Event\n");
#endif	
     
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Read_RSSI_Cmd_fcn(void* readBuf)
{   HCI_Read_RSSI_evt evt;
   HCI_Read_RSSI_Cmd_struct *param;
   char *temp;   
   char buf_uartP[8];
   int32_t RSSI_level; //int for nano, char in Bt spec
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_RSSI_Cmd Received\n");
#endif
   
   // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Read_RSSI_Cmd_struct*)&temp[4];
   
   WiFiEngine_GetRSSI(&RSSI_level, TRUE);
    
    //RSSI_level: int for nano, signed8 in Bt spec
   if (RSSI_level<-128)
      RSSI_level=-128;
   if (RSSI_level>128)
      RSSI_level=128;    
      
   if (RSSI_level<GOLDEN_UPPER_LIMIT && RSSI_level>GOLDEN_LOWER_LIMIT)
   {
      evt.RSSI=0x00;
   }
   else if (RSSI_level>GOLDEN_UPPER_LIMIT)
   {
      evt.RSSI=(RSSI_level-GOLDEN_UPPER_LIMIT);
   }
   else 
   {
      evt.RSSI=(GOLDEN_UPPER_LIMIT-RSSI_level);
   }
   // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x07;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1405; //OBS little endian
   evt.status=0x00;
   evt.handle=0x0001;
   
         
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[6]=evt.handle;
   buf_uartP[6]=evt.handle/256;
   buf_uartP[7]=evt.RSSI;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
    
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////

int HCI_Read_Link_Quality_Cmd_fcn(void* readBuf)
{   
   HCI_Read_Link_Quality_evt evt;
   HCI_Read_Link_Quality_Cmd_struct *param;   
   char *temp;
   char buf_uartP[10];
      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Link_Quality_Cmd Received\n");
#endif	
   // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Read_Link_Quality_Cmd_struct*)&temp[4];
   // short=param->handle;
   // DE_TRACE_STATIC(TR_AMP_HIGH_RES, "\nLogical Link handle= %02x %02x",readBuf[4],readBuf[5]);
      
    // prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x07;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1403; //OBS little endian
   evt.status=0x00;
   evt.handle=0x0001;
   evt.Link_Quality=0x11;
         
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.handle;
   buf_uartP[8]=evt.handle/256;
   buf_uartP[9]=evt.Link_Quality;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}


/////////////////////////////////////////////////////////////////////////////////////////
int Physical_Link_Complete_Event(char reason)
{
   HCI_Physical_Link_complete_evt evt;
   char buf_uartP[5];
   
   //Physical link completed
   creatingPhysicalLink=0x00;
      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Physical_Link_Complete_Event Received\n");
#endif	
   //prepare accept logical link complete event
   evt.evt_hdr=0x04;
   evt.evt_code=0x40;
   evt.length=0x02;
   evt.status=0x01;
   evt.Physical_Link_Handle=0x01; 
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=reason;
   buf_uartP[4]=physicalLinkHandle[0][POSITION_PHYSICAL_LINK_HANDLE] ;
    
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif	
   
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Create_Physical_Link_Cmd_fcn(void* readBuf)
{   
   HCI_Create_Physical_Link_status_evt evt;
   
   HCI_Create_Physical_Link_struct *param;
   char *temp;
   int RegisterdTimer=3;
   char buf_uartP[7];
   int i=0;

   //Create physical link is ongoing
   creatingPhysicalLink=0x01;
      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Create_Physical_Link_Cmd Received\n");
#endif	
       
   //Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Create_Physical_Link_struct*)&temp[4];
   //Initator of physical linl
   physicalLinkHandle[i] [LINK_INDICATOR]=0x01;
   physicalLinkHandle[i][POSITION_PHYSICAL_LINK_HANDLE]=temp[4];
   
   //TODO set linkkey
   //WiFiEngine_Supplicant_Set_PSK(char* pmk, int size)
   
   //Create new timer
   if    (DriverEnvironment_GetNewTimer(&connectionTimer, FALSE)!=DRIVERENVIRONMENT_SUCCESS)
   {
#ifdef DEBUG_COMMAND_PARSER		
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Failed to allocate timer\n");
#endif		
   }
   else
   {
#ifdef DEBUG_COMMAND_PARSER		
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer started\n");
#endif		
      //Register callback for timer, not repeating
      RegisterdTimer=DriverEnvironment_RegisterTimerCallback(connectionAcceptTimeOutMsec, connectionTimer, 
         (de_callback_t)Physical_Link_Timer_Callback, 0);
      switch(RegisterdTimer)
      {
         case (0):
         {
#ifdef DEBUG_COMMAND_PARSER				
            DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer busy\n");
#endif				
         }
         break;
         
         case (-1):
         {
#ifdef DEBUG_COMMAND_PARSER				
            DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer FIALED\n");
#endif				
         }
         break;

         case (1):
         {
#ifdef DEBUG_COMMAND_PARSER				
            DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer SUCCESS\n");
#endif				
         }
         break;
         
         default:
#ifdef DEBUG_COMMAND_PARSER				
            DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer UnExpected\n");
#endif				
         break;
      }      
   }

   //prepare status event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0f;
   evt.length=0x04;
   evt.status=0x00;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0435; //OBS little endian
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.status;
   buf_uartP[4]=evt.num_HCI_pkts;
   buf_uartP[5]=evt.opcode;
   buf_uartP[6]=evt.opcode/256;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Status Event\n");
#endif
   //Status Event
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Accept_Physical_Link_Cmd_fcn(void* readBuf)
{   
   HCI_Accept_Physical_Link_status_evt evt;
   HCI_Accept_Physical_Link_Cmd_struct *param;
   char *temp;
   char buf_uartP[7];
   int i=0;
   int RegisterdTimer=3;   

   //Create physical link is ongoing
   creatingPhysicalLink=0x01;

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Accept_Physical_Link_Cmd Received\n");
#endif
   //Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Accept_Physical_Link_Cmd_struct*)&temp[4];
          
   //Initator of physical linl
   physicalLinkHandle[i] [LINK_INDICATOR]=0x00;
   physicalLinkHandle[i][POSITION_PHYSICAL_LINK_HANDLE]=temp[4];
   
   //TODO set linkkey
   //WiFiEngine_Supplicant_Set_PSK(char* pmk, int size)
   
   //Create new timer
   if    (DriverEnvironment_GetNewTimer(&connectionTimer, FALSE)!=DRIVERENVIRONMENT_SUCCESS)
   {
#ifdef DEBUG_COMMAND_PARSER		
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Failed to allocate timer\n");
#endif		
   }
   else
   {
#ifdef DEBUG_COMMAND_PARSER		
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer started\n");
#endif		
      //Register callback for timer, not repeating
      RegisterdTimer=DriverEnvironment_RegisterTimerCallback(connectionAcceptTimeOutMsec, connectionTimer, 
         (de_callback_t)Physical_Link_Timer_Callback, 0);
      switch(RegisterdTimer)
      {
         case (0):
         {
#ifdef DEBUG_COMMAND_PARSER				
            DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer busy\n");
#endif				
         }
         break;
         
         case (-1):
         {
#ifdef DEBUG_COMMAND_PARSER				
            DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer FIALED\n");
#endif				
         }
         break;

         case (1):
         {
#ifdef DEBUG_COMMAND_PARSER				
            DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer SUCCESS\n");
#endif				
         }
         break;
         
         default:
#ifdef DEBUG_COMMAND_PARSER				
            DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer UnExpected\n");
#endif				
            break;
      }      
   }
          
   //prepare status event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0f;
   evt.length=0x04;
   evt.status=0x00;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0436; //OBS little endian
   
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.status;
   buf_uartP[4]=evt.num_HCI_pkts;
   buf_uartP[5]=evt.opcode;
   buf_uartP[6]=evt.opcode/256;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Status Event\n");
#endif
   //Status Event
   Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   
   return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Disconnect_Physical_Link_Cmd_fcn(void* readBuf)
{   
   HCI_Disconnect_Physical_Link_status_evt evt;
   HCI_Disconnect_Physical_Link_Cmd_struct *param;
   
   char *temp;
   char buf_uartP[7];
  
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Disconnect_Physical_Link_Cmd Received\n");
#endif	
   // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Disconnect_Physical_Link_Cmd_struct*)&temp[4];
         
    // prepare status event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0f;
   evt.length=0x04;
   evt.status=0x00;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x0437; //OBS little endian
   
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.status;
   buf_uartP[4]=evt.num_HCI_pkts;
   buf_uartP[5]=evt.opcode;
   buf_uartP[6]=evt.opcode/256;
    
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "ending Status Event\n");
#endif	
   //Shall always succeed
   WiFiEngine_sac_stop();

   //Disconnect physical link

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "WiFiEngine_sac_stop\n");
#endif	

   return 1;
}
int HCI_Disconnect_Physical_Link_Evt(void* readBuf)
{
   HCI_Disconnection_Physical_Link_complete_evt evt2;
   char buf_uartP[6];
   HCI_Disconnect_Logical_Link_complete_evt evt1;
   char buf_uartC[7];
  
    
   //Check if logical link needs to be disconencted
   //Only one link
   if (physicalLinkHandle[0][POSITION_LOGICAL_LINK_HANDLE+1]==0x01 &&
      physicalLinkHandle[0][POSITION_LOGICAL_LINK_HANDLE]==0x00)
   {
       // prepare disconenct logical link complete event
      evt1.evt_hdr=0x04;
      evt1.evt_code=0x46;
      evt1.length=0x04;
      evt1.status=0x00;
      evt1.reason=0x01;   
      
      //copy the event to a char array befor sending it
      buf_uartC[0]=evt1.evt_hdr;
      buf_uartC[1]=evt1.evt_code;
      buf_uartC[2]=evt1.length;
      buf_uartC[3]=evt1.status;
      buf_uartC[4]=physicalLinkHandle[0][POSITION_LOGICAL_LINK_HANDLE+1];
      buf_uartC[5]=physicalLinkHandle[0][POSITION_LOGICAL_LINK_HANDLE];
      buf_uartC[6]=evt1.reason;
   
#ifdef DEBUG_COMMAND_PARSER
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Disconnect logical link Event\n");
#endif	
      
      Check_Event_Mask(netId,  buf_uartC, sizeof(buf_uartC));
   }
   else
   {
#ifdef DEBUG_COMMAND_PARSER
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "No logical link\n");
#endif
   }   
   //physicalLinkHandle[i][POSITION_PHYSICAL_LINK_HANDLE]
   // prepare disconnect logical link complete event
   evt2.evt_hdr=0x04;
   evt2.evt_code=0x42;
   evt2.length=0x03;
   evt2.status=0x01;
   evt2.Physical_Link_Handle=0x01; 
   evt2.reason=0x01;
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt2.evt_hdr;
   buf_uartP[1]=evt2.evt_code;
   buf_uartP[2]=evt2.length;
   buf_uartP[3]=evt2.status;
   buf_uartP[4]=evt2.Physical_Link_Handle;
   buf_uartP[5]=evt2.reason;

   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));   

   return 1;   
}

/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Read_Local_AMP_ASSOC_Cmd_fcn(void* readBuf)
{   
   HCI_Read_Local_AMP_ASSOC_evt evt;
   HCI_Read_Local_AMP_ASSOC_Cmd_struct *param;
   char *temp;   
   char buf_uartP[29];
   int i=0;
   int assocLength = 0;
   int ResGetAddress;
   int size=6;
     
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Read_Local_AMP_ASSOC_CMD Received\n");
#endif
   // Save the parameters
   //Max length 248
   temp=(char*)readBuf;
   param=(HCI_Read_Local_AMP_ASSOC_Cmd_struct*)&temp[4];

   //from Init
   ResGetAddress=WiFiEngine_GetMACAddress((char *)&AMP_Assoc[3], &size);
     
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "WIfI_GEtMacAddress\n");

   if (ResGetAddress==WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "WIFI_ENGINE_SUCCESS\n");
      for (i=0; i<=5; i++); 
      {
         DE_TRACE_INT(TR_AMP_HIGH_RES, "WIfI MacAddress %02x\n", AMP_Assoc[3+i] );
      }
   }
   else if (ResGetAddress==WIFI_ENGINE_FAILURE) {
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "WIFI_ENGINE_FAILURE\n");
   } else if (ResGetAddress==WIFI_ENGINE_FAILURE_DEFER ) {
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "WIFI_ENGINE_FAILURE_DEFER \n");
   } else {
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "UNKNOWN \n");
   }
#endif	
      
   while (physicalLinkHandle[i][POSITION_PHYSICAL_LINK_HANDLE]!=0x00 && i<=MAX_NUMBER_OF_PHYSICAL_LINKS)
   {
#ifdef DEBUG_COMMAND_PARSER 	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Check for handle \n");
#endif
      i++;
   }
   i=0; //one link
   if (i==MAX_NUMBER_OF_PHYSICAL_LINKS-1)
   {
      if(physicalLinkHandle[i][POSITION_PHYSICAL_LINK_HANDLE]!=0x00)
      {
#ifdef DEBUG_COMMAND_PARSER 	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES \n");
#endif
         evt.status=CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES;
      }
      else
      {
         evt.status=HCI_SUCCESS;
         physicalLinkHandle[0] [POSITION_PHYSICAL_LINK_HANDLE]=temp[4];
    
#ifdef DEBUG_COMMAND_PARSER 	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_SUCCESS \n");
#endif
      }
   }
   else //for several links
   {
    
#ifdef DEBUG_COMMAND_PARSER 	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Several links \n");
#endif      
      evt.status=HCI_SUCCESS;
      physicalLinkHandle[0] [POSITION_PHYSICAL_LINK_HANDLE]=temp[4];
   }
   
   //Length so far
   physicalLinkHandle[0] [LENGTH_SO_FAR]=temp[6];
   physicalLinkHandle[0] [LENGTH_SO_FAR+1]=temp[5];
   //Remaining length
   physicalLinkHandle[0] [MAX_REMOTE_AMP_ASSOC]=temp[8];
   physicalLinkHandle[0] [MAX_REMOTE_AMP_ASSOC+1]=temp[7];
   

   
   assocLength = 13; 
   if (assocLength<=maxAmpAssocLength)
      remainingAmpAsscocLength = 0;
   else
      remainingAmpAsscocLength = assocLength-maxAmpAssocLength;
   
       //prepare event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x06+16;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x140a; //OBS little endian
   
   evt.Physical_Link_Handle=physicalLinkHandle[i] [POSITION_PHYSICAL_LINK_HANDLE];
   evt.AMP_ASSOC_Remaining_Length=0x00;
         
         
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.num_HCI_pkts;
   buf_uartP[4]=evt.opcode;
   buf_uartP[5]=evt.opcode/256;
   buf_uartP[6]=evt.status;
   buf_uartP[7]=evt.Physical_Link_Handle;
   buf_uartP[8]=evt.AMP_ASSOC_Remaining_Length;
   buf_uartP[9]=evt.AMP_ASSOC_Remaining_Length/256;

   
   
   //send parameter in reversed byte order
   i=0;
   for (i=0; i<=15; i++)
   {   
      buf_uartP[10+i] =   AMP_Assoc[15-i];
   }
   //memcpy(&buf_uartP[10], &AMP_Assoc[0], 16);
   
    
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[10]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[11]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[12]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[13]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[14]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[15]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[16]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[17]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[18]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[19]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[21]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[22]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[23]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[24]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[25]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[26]);
   DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[27]);
   
   i=0;
   for (i=0; i<=24; i++); 
   {
      DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet to send %02x\n", buf_uartP[i]);
   }
#endif
      
     
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif
       
   Check_Event_Mask(netId, buf_uartP, 27);
    return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Write_Remote_AMP_ASSOC_Cmd_fcn(void* readBuf)
{   
   HCI_Write_Remote_AMP_ASSOC_evt evt;
   HCI_Channel_Selected_evt chSelEvt;
   //HCI_Write_Remote_AMP_ASSOC_Cmd_struct *param;      
   char *temp;
   char *temp2;
   int i=0;
   int paramLength=0;
   char buf_uartP[4];
   char buf_uartP2[8];
   short remainingAmpLength;
   
    
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Write_Remote_AMP_ASSOC_Cmd Received\n");
#endif
   // Save the parameters
   temp2=(char*)readBuf;
   
   remainingAmpLength=temp2[7]+temp2[8]*0x0100;
   

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "remainingAmpLength=  %02x\n", remainingAmpLength);
   //print packet
   for (i=0; i<=25; i++)
   {
      DE_TRACE_INT2(TR_AMP_HIGH_RES, "Received Params = %02x, %02x\n", temp2[i], i);
   }
#endif
   
   //one packet AMP_ASSOC supported
   //if (param->AMP_ASSOC_Remaining_Length<=(AMP_MAX_HCI_PACKET_LENGTH-8))
   //{

#ifdef DEBUG_COMMAND_PARSER
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Look for params\n");
#endif
      //Find MAC Address
      //temp=(char*)readBuf;
      //Start at end of packet, revered byte order
      temp=&temp2[9+remainingAmpLength-1];
      
      
      while (temp[0]!=0x01 /*&& paramLength <=(AMP_MAX_HCI_PACKET_LENGTH-8)*/)
      {

#ifdef DEBUG_COMMAND_PARSER
         DE_TRACE_INT(TR_AMP_HIGH_RES, "MAC , %02x\n", paramLength);
#endif
         //jump to next param
         paramLength=paramLength-temp[9+remainingAmpLength-1-1]*256-temp[9+remainingAmpLength-1-1-1]-2-1;

#ifdef DEBUG_COMMAND_PARSER
         DE_TRACE_INT(TR_AMP_HIGH_RES, "paramLength, %02x\n", paramLength);
#endif
         temp=temp-paramLength;
         //temp=temp+temp[1]*256+temp[2]+2+1;
      }
      if (temp[0]==0x01)
      {

#ifdef DEBUG_COMMAND_PARSER
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "MAC found\n");
#endif      
         temp=temp-8;
         //Copy Address, stored reversed
         //memcpy(&physicalLinkHandle[0] [REMOTE_MAC_ADDRESS], &temp[0],  6);
         
         for (i=0; i<=5; i++)
         {
            physicalLinkHandle[0] [REMOTE_MAC_ADDRESS+5-i]=temp[i];
         }
      }   
#ifdef DEBUG_COMMAND_PARSER		
   for (i=0; i<=5; i++)
   {
      DE_TRACE_INT(TR_AMP_HIGH_RES, "Remote MAC adress = %02x\n", physicalLinkHandle[0] [REMOTE_MAC_ADDRESS+i]);
   }
#endif   
      //Find Channel List
      //temp=&temp2[9];
      temp=&temp2[9+remainingAmpLength-1];
      paramLength=0;
      while (temp[0]!=0x02 && paramLength <=(AMP_MAX_HCI_PACKET_LENGTH-8))
      {

#ifdef DEBUG_COMMAND_PARSER
         DE_TRACE_INT(TR_AMP_HIGH_RES, "Channel List .. . = %02x\n", temp[0]);
         DE_TRACE_INT(TR_AMP_HIGH_RES, "paramLength , %02x\n", paramLength);
#endif
         paramLength=paramLength+temp2[9+remainingAmpLength-2-paramLength]*256+
         temp2[9+remainingAmpLength-3-paramLength]+2+1;

#ifdef DEBUG_COMMAND_PARSER
         DE_TRACE_INT(TR_AMP_HIGH_RES, "Paramlength2 , %02x\n", paramLength);
#endif         
         temp=temp-paramLength;
      }
      if (temp[0]==0x02)
      {

#ifdef DEBUG_COMMAND_PARSER
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Channel List found\n");
#endif
         //Copy Cahnnel List
         memcpy(&physicalLinkHandle[0] [CHANNEL_LIST], &temp[3],  temp[2]); 
      }

      //Find Pal version
      temp=&temp2[9+remainingAmpLength-1];
      paramLength=0;
      while (temp[0]!=0x05 && paramLength <=(AMP_MAX_HCI_PACKET_LENGTH-8))
      {

#ifdef DEBUG_COMMAND_PARSER
         DE_TRACE_INT(TR_AMP_HIGH_RES, "temp .. . = %02x\n", temp[0]);
         DE_TRACE_INT(TR_AMP_HIGH_RES, "paramLength , %02x\n", paramLength );
#endif
         paramLength=paramLength+temp2[9+remainingAmpLength-2-paramLength]*256+
         temp2[9+remainingAmpLength-3-paramLength]+2+1;

#ifdef DEBUG_COMMAND_PARSER
         DE_TRACE_INT(TR_AMP_HIGH_RES, "Paramlength2 , %02x\n", paramLength);
#endif         
         temp=temp-paramLength;
      }
      if (temp[0]==0x05)
      {

#ifdef DEBUG_COMMAND_PARSER
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Pal version found\n");
#endif
         //Copy PAl version
         memcpy(&physicalLinkHandle[0] [PAL_VERSION], &temp[3],  1); 
         
      }
//   }


 //prepare command complete event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0e;
   evt.length=0x05;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x1408; //OBS little endian
   evt.status=0x00;
   evt.Physical_Link_Handle=0x01;   
          
   //copy the event to a char array befor sending it
   buf_uartP2[0]=evt.evt_hdr;
   buf_uartP2[1]=evt.evt_code;
   buf_uartP2[2]=evt.length;
   buf_uartP2[3]=evt.num_HCI_pkts;
   buf_uartP2[4]=evt.opcode;
   buf_uartP2[5]=evt.opcode/256;
   buf_uartP2[6]=evt.status;
   buf_uartP2[7]=evt.Physical_Link_Handle;
   
   //Always send command complete
Check_Event_Mask(netId, buf_uartP2, sizeof(buf_uartP2));   

   //Signal MAC to start scan

#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "WiFiEngine_NetSetSSID\n");
#endif

   if (WiFiEngine_NetSetSSID(NETWORK_ID_AMP, "AMP", 3)==1)
   {


#ifdef DEBUG_COMMAND_PARSER
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "WIFI_ENGINE_SUCCESS\n");
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "WiFiEngine_NetStartBeacon\n");
#endif
      WiFiEngine_NetStartBeacon(NETWORK_ID_AMP);
   }   




   //Prepare Channel select event
   chSelEvt.evt_hdr=0x04;
   chSelEvt.evt_code=0x41;
   chSelEvt.length=0x01;
   chSelEvt.Physical_Link_Handle=0x01;
   //copy the event to a char array befor sending it
   buf_uartP[0]=chSelEvt.evt_hdr;
   buf_uartP[1]=chSelEvt.evt_code;
   buf_uartP[2]=chSelEvt.length;
   buf_uartP[3]=chSelEvt.Physical_Link_Handle;



#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Event\n");
#endif
   //Send ChannelSelect Event if initiator of physical link
   if (physicalLinkHandle[0] [LINK_INDICATOR]==0x01)
      Check_Event_Mask(netId, buf_uartP, sizeof(buf_uartP));
   
   
       return 1;
}
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Flow_Spec_Modify_Cmd_fcn(void* readBuf)
{   
   HCI_Flow_Spec_Modify_status_evt evt;
   HCI_Flow_Spec_Modify_complete_evt evt2;
   HCI_Flow_Spec_Modify_Cmd_struct *param;
   char *temp;
   char buf_uartP[7];
   char buf_uartC[6];

      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Flow_Spec_Modify_Cmd Received\n");
#endif
    // Save the parameters
   temp=(char*)readBuf;
   param=(HCI_Flow_Spec_Modify_Cmd_struct*)&temp[4];

          
    // prepare status event
   evt.evt_hdr=0x04;
   evt.evt_code=0x0f;
   evt.length=0x04;
   evt.status=0x00;
   evt.num_HCI_pkts=0x01;
   evt.opcode=0x043c; //OBS little endian
   
   
   //copy the event to a char array befor sending it
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.status;
   buf_uartP[4]=evt.num_HCI_pkts;
   buf_uartP[5]=evt.opcode;
   buf_uartP[6]=evt.opcode/256;

      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Status Event\n");
#endif   
    Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   
    // prepare accept logical link complete event
    evt2.evt_hdr=0x04;
    evt2.evt_code=0x47;
    evt2.length=0x03;
    evt2.status=0x01;
    evt2.handle=0x0001; 
      
   //copy the event to a char array befor sending it
   buf_uartC[0]=evt2.evt_hdr;
   buf_uartC[1]=evt2.evt_code;
   buf_uartC[2]=evt2.length;
   buf_uartC[3]=evt2.status;
   buf_uartC[4]=evt2.handle;
   buf_uartC[5]=evt2.handle/256;

   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Command Complete Event\n");
#endif   
    
   Check_Event_Mask(netId,  buf_uartC, sizeof(buf_uartC));
    return 1;
}

/////////////////////////////////////////////////////////////////////////////////////

int Buffer_OverFlow_Evt_fcn(void)
{
   HCI_Buffer_OverFlow_Evt evt;

   char buf_uartP[4];

   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Buffer_OverFlow_Evt_fcn\n");
#endif      
   evt.evt_hdr=0x04;
   evt.evt_code=0x1a; 
   evt.length=0x01;
   evt.linkType=0x01; //Always acl data for AMP controller
   
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.linkType;
   
   
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Command Complete Event\n");
#endif   
    
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
    return 1;
   
}

///////////////////////////////////////////////////////////////////////////////////////

int HCI_Send_Data(void* readBuf)
{
   short connectionHandle;
   short packetLength;
   char packetBoundaryFlag;
   char broadcastFlagH2C;
   char broadcastFlagC2H;
   char *temp;
   int i, j;
   int RegisteredTimer;
   j=0;
   i=0;
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "HCI_Send_Data\n");
#endif   
   
   //Start new flush timer, allocated in init
   //Register callback for timer, not repeating
   if (bestEffortFlushTmoMicroSec!=0xffffffff)
   {
      RegisteredTimer=DriverEnvironment_RegisterTimerCallback(bestEffortFlushTmoMsec, flushTimer, 
         (de_callback_t)Flush_Timer_Callback, 0);
#ifdef DEBUG_COMMAND_PARSER	
      if (RegisteredTimer==1) 
      {
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Timer SUCCESS\n");
      }      
      else
      {
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Flush data timer fialed\n");
      }
#endif
   }      
   
      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "lastDataPacket = %02x\n", lastDataPacket);
#endif   
   //check if if buffer after lastDataPacket is free
   if (lastDataPacket==(totalNumDataBlocks-1)) //last data packet befor using first buffer
      lastDataPacket=0;
   else 
      lastDataPacket=lastDataPacket+1;

         
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_INT(TR_AMP_HIGH_RES, "lastDataPacket = %02x\n", lastDataPacket);
#endif
   //check if next buffer is available, if not buffer overflow
   if    (dataBlockMAtrix[lastDataPacket].status != WAIT_CONFIRM)
   {
      dataBlockMAtrix[lastDataPacket].status = WAIT_CONFIRM;
      
#ifdef DEBUG_COMMAND_PARSER		
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Available buffer exists\n");
#endif
      // Save the parameters
      temp=(char*)readBuf;
      //Check Connection handle
      connectionHandle=(temp[1]+((temp[2]&0x03)<<8));
      
   
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_INT(TR_AMP_HIGH_RES, "Connection Handle = %04x\n", connectionHandle);
#endif   

      //Packet boundary flag
      //Shall be set to 0x11 for AMP controllers
      packetBoundaryFlag=(temp[2]&0x0c);

      //Broad cast flag Host to controller
      //Shall be set to 00 for AMP controllers
      broadcastFlagH2C=(temp[2]&0x30);
      
      //Broad cast flag controller to host
      broadcastFlagC2H=(temp[2]&0x30);
      
      //Check length 
      packetLength=(temp[3]+(temp[4]<<8));
         
   
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_INT(TR_AMP_HIGH_RES, "Packet Length = %02x\n", packetLength);
#endif   
      
      //Copy dato into data packet
      memcpy(dataBlockMAtrix[i].dataptr, &physicalLinkHandle[0] [REMOTE_MAC_ADDRESS],  6); 
      memcpy(dataBlockMAtrix[i].dataptr+6, &physicalLinkHandle[0] [LOCAL_MAC_ADDRESS],  6); 
      //12 and 13 used for prio
      memcpy(dataBlockMAtrix[i].dataptr+14, &temp[0], packetLength+5); 

      

      //Send Data expected length packetLength+3+14
      wei_pal_send_data(dataBlockMAtrix[i].dataptr, packetLength+5+14, transactionIdPacket1);
   }
   //buffer oveflow event
   else
   {
      
#ifdef DEBUG_COMMAND_PARSER		
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "No Available buffer exists\n");
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Buffer overflow\n");
      
#endif   
      Buffer_OverFlow_Evt_fcn();
   }
   return 0;
}

int Flush_Occured_Event(void)
{
   HCI_Flush_Occured_Event evt;
   char buf_uartP[5];
   
      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Flush_Occured_Event\n");
#endif   
   evt.evt_hdr=0x04;
   evt.evt_code=0x1a; 
   evt.length=0x02;
   evt.Logical_Link_Handle=0x0001; 
   
   
   buf_uartP[0]=evt.evt_hdr;
   buf_uartP[1]=evt.evt_code;
   buf_uartP[2]=evt.length;
   buf_uartP[3]=evt.Logical_Link_Handle;
   buf_uartP[4]=evt.Logical_Link_Handle/256;
   
   
   
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Sending Command Complete Event\n");
#endif   
    
   Check_Event_Mask(netId,  buf_uartP, sizeof(buf_uartP));
   return 1;
}
/****************************************************************************
** number_of_completed_data_blocks
**
** Called from hmg_PAL each time a data packet has been transmitted
** Update Matrix with completed data packets
*****************************************************************************/

int number_of_completed_data_blocks(void)
{
   int completedDataBlocks =1;
   //Number of completed data blocks can be sent oen by one or one packet 
   //confirming several data blocks, timer is required when sending confirmation
   //for several data blocks
   if (packetToFree==totalNumDataBlocks-1) //last data packet befor using first buffer
   {
      dataBlockMAtrix[packetToFree].status = COMPLETED;
      packetToFree=0;
   }
   else 
   {
      dataBlockMAtrix[packetToFree].status = COMPLETED;
      packetToFree++;
   }   
   Number_Of_Complete_Data_Bocks_Evt_fcn(completedDataBlocks);
   return 0;
}

de_callback_t Flush_Timer_Callback(void *data, size_t data_len)
{
   int i=0;
      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Flush Timer Expired\n");
#endif   
   //Generate flush event
      WiFiEngine_Transport_Flush(netId);
   
   //Reset data handler
   while (i<TOTAL_NUMBER_OF_DATA_BLOCKS)
   {
      if (dataBlockMAtrix[i].status == WAIT_CONFIRM)
      {
         dataBlockMAtrix[i].status = DATA_FLUSHED;
         
   
#ifdef DEBUG_COMMAND_PARSER	
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Data packet flushed\n");
#endif   
      }
      i++;
   }
   Flush_Occured_Event();
   return 0;
}

de_callback_t Physical_Link_Timer_Callback(void *data, size_t data_len)
{
      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Physical link Timer Expired\n");
#endif   
   if (creatingPhysicalLink==0x01)
      Physical_Link_Complete_Event(CONNECTION_TIMEOUT);
   return 0;
}

de_callback_t Logical_Link_Timer_Callback(void *data, size_t data_len)
{
#ifdef DEBUG_COMMAND_PARSER		
   logicalLinkConnectionTimerRunning=FALSE;
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Logocal link Timer Expired\n");
#endif   
   return 0;
}

int Check_Event_Mask(mac_api_net_id_t netID, char* bufPt, int length)
{
   int eventBit;
   int sendEvent=0;
   
   //Check Event mask
   eventBit=bufPt[1];
   if (eventBit <0x40)
   {
   
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_INT(TR_AMP_HIGH_RES, "eventBit = %02x\n", eventBit);
      DE_TRACE_INT(TR_AMP_HIGH_RES, "(eventMaskPage[eventBit/8]) = %02x\n", (eventMask[(eventBit/8)]));
      DE_TRACE_INT(TR_AMP_HIGH_RES, "eventBit/0x8) = %02x\n", (eventBit/8));
      DE_TRACE_INT(TR_AMP_HIGH_RES, "(eventbit proc 0xff) = %02x\n", (eventBit%0x8));
      DE_TRACE_INT(TR_AMP_HIGH_RES, "1>>(eventBit proc 8)) = %02x\n", (1<<(eventBit%0x8)));
#endif      
      if((eventMask[(eventBit/8)])&(1<<(eventBit%0x8)))
      {
         
#ifdef DEBUG_COMMAND_PARSER			
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "eventMask OK\n");
#endif         
         sendEvent =1;
      }
      else
         sendEvent =0;
   }
   else
   {      
      eventBit=bufPt[1]-0x3f;
      //Check event mask page 2
   
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_INT(TR_AMP_HIGH_RES, "eventMaskPage2[0] = %02x\n", eventMaskPage2[0]);
      DE_TRACE_INT(TR_AMP_HIGH_RES, "eventMaskPage2[1] = %02x\n", eventMaskPage2[1]);
      
      DE_TRACE_INT(TR_AMP_HIGH_RES, "eventBit = %02x\n", eventBit);
      DE_TRACE_INT(TR_AMP_HIGH_RES, "(eventMaskPage2[eventBit/8]) = %02x\n", (eventMaskPage2[(eventBit/8)]));
      DE_TRACE_INT(TR_AMP_HIGH_RES, "eventBit/0x8) = %02x\n", (eventBit/8));
      DE_TRACE_INT(TR_AMP_HIGH_RES, "(eventbit proc 0xff) = %02x\n", (eventBit%0x8));
      DE_TRACE_INT(TR_AMP_HIGH_RES, "1>>(eventBit proc 8)) = %02x\n", (1<<(eventBit%0x8)));
#endif      
      if((eventMaskPage2[(eventBit/8)])&(1<<(eventBit%0x8)))
      {
         
#ifdef DEBUG_COMMAND_PARSER			
         DE_TRACE_STATIC(TR_AMP_HIGH_RES, "eventMaskPage2 OK\n");
#endif         
         sendEvent =1;
      }
      else
         sendEvent =0;
   }   
   if (sendEvent ==1)
   {
   
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Event mask OK sent event\n");
#endif      
      HCI_response_cb(netID, bufPt, length);
   }
   else
   {
   
#ifdef DEBUG_COMMAND_PARSER	
      DE_TRACE_STATIC(TR_AMP_HIGH_RES, "Event not sent due to event mask\n");
#endif      
   }
   return 1;
}

int ReadSupportedCommands (char *buf)
{
   
   int i=7;
   //DE_TRACE_STATIC(TR_AMP_HIGH_RES, "\nReadSupportedCommands");
   //5
   char switchRole=0;
   char readLinkPolicySettings=0;
   char writeLinkPolicySettings=1;
   char readDefaultLinkolicySettings=1;
   char writeDefaultLinkPolicySettings=0;
   char flowSpecification=0;
   char setEventMssk=0;
   char reset=0;
   //6
   char setEventFilter=0;
   char Flush=0;
   char ReadPinType=1;
   char WritePinType=1;
   char createNewUnitKey=0;
   char readStoredLinkKey=0;
   char writeStoredLinkKey=0;
   char deleteStoredLinkKey=0;

   //7
   char WriteLocalName=0;
   char readLocalName=0;
   char readconnectionAcceptTmo=1;
   char writeconnectionAcceptTmo=1;
   char readPageTmo=0;
   char writePageTmo=0;
   char readScanEnable=0;
   char writeScanEnable=0;

   //10
   char ReadHoldModeActivity=0;
   char writeHoldModeActivity=0;
   char readTransmitPowerLevel=0;
   char readSynchronousFlowControlEnable=0;
   char writeSynchronousFlowControlEnable=0;
   char setHostControllertoHostFlowControl=1;
   char HostBuffersSize=0;
   char hostNumberOfCompletedPackets=1;

   //11
   char readLinkSupervisionTmo=1;
   char writeLinkSupervisionTmo=1;
   char readNumberOfSupportedIac=0;
   char readCurrentIacLap=0;
   char writeCurrentIacLap=0;
   char readPageScanModePeriod=1;
   char writePageScanModePeriod=0;
   char readPageScanMode=0;


   //14
   //Reserved
   //Reserved
   //Reserved
   char readLocalVerisonInformation=1;
   //Reserved
   char readLocalSupportedFeatures=1;
   char readLocalExtendedFeatures=0;
   char ReadBufferSize=1;


   //15
   char readCountryCode=0;
   char readBdAddress=0;
   char readFailedContactCounter=1;
   char resetFailedContactCounter=1;
   char getLinkQuality=1;
   char readRssi=1;
   char readAfhChannelMap=0;
   char ReadBdClock=0;


   //16
   char readLoopbackMode=0;
   char writeLoopbackMode=0;
   char enableDeviceUnderTestMode=1;
   char setupSynchronousConnection=1;
   char acceptSynchronousConnection=1;
   char rejectSynchronousConnection=1;
   //Reserved
   //Reserved


   //20
   //Reserved
   //Reserved
   char sendKeyPressNotification=0;
   char IoCapabiltyResponseNegativeReply=0;
   char readEncryptionKeySize=0;
   //Reserved
   //Reserved
   //Reserved

   //21
   char createPhysicalLink=1;
   char acceptPhysicalLink=1;
   char disconnectPhysicalLink=1;
   char createLogicallLink=1;
   char acceptLogicalLink=1;
   char disconnectLogicalLink=1;
   char logicalLinkCancel=1;
   char flowSpecMode=1;

   //22
   char readLogicalLinkAcceptTmo=1;
   char writeLogicalLinkAcceptTmo=1;
   char setEventMaskPage2=1;
   char readLocationData=1;
   char wrietLocatonData=1;
   char readlocalAmpInfo=1;
   char readLocalAmpAssoc=1;
   char writeRemoteAmpassoc=1;

   //23
   char readFlowControlMode=1;
   char writeFlowControlMode=1;
   char readDataBlockSize=1;
   //Reserved
   //Reserved
   char enableAmpReceiverReports=1;
   char ampTestEnd=1;
   char ampTestComman=1;

   //24
   char readEnhancedTransmitPowerLevel=1;
   //Reserved
   char readBestEffortFlushTmo=1;
   char writeBestEffortFlushTmo=1;
   char shortRangeMode=1;
   //Reserved
   //Reserved
   //Reserved
   
      
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "loop\n");
#endif
   for (i=7; i<=70; i++)
      { 
         buf[i]=0x00;
      }
   
   
   
   buf[7+7]=0xee;
   buf[70]=0xff;

   //5
   buf[70-5]=switchRole|readLinkPolicySettings<<1|writeLinkPolicySettings<<2
      |readDefaultLinkolicySettings<<3|writeDefaultLinkPolicySettings<<4|flowSpecification<<5
      |setEventMssk<<6|reset<<7;
   
   //6
   
   buf[70-6]=setEventFilter|Flush<<1|ReadPinType<<2
      |WritePinType<<3|createNewUnitKey<<4|readStoredLinkKey<<5
      |writeStoredLinkKey<<6|deleteStoredLinkKey<<7;

   //7
   buf[70-7]=WriteLocalName|readLocalName<<1|readconnectionAcceptTmo<<2
      |writeconnectionAcceptTmo<<3|readPageTmo<<4|writePageTmo<<5
      |readScanEnable<<6|writeScanEnable<<7;

   
   //10
   buf[70-10]=ReadHoldModeActivity|writeHoldModeActivity<<1|readTransmitPowerLevel<<2
      |readSynchronousFlowControlEnable<<3|writeSynchronousFlowControlEnable<<4
      |setHostControllertoHostFlowControl<<5|HostBuffersSize<<6
      |hostNumberOfCompletedPackets<<7;

   
   
   //11
   buf[70-11]=readLinkSupervisionTmo|writeLinkSupervisionTmo<<1|readNumberOfSupportedIac<<2
      |readCurrentIacLap<<3|writeCurrentIacLap<<4|readPageScanModePeriod<<5
      |writePageScanModePeriod<<6|readPageScanMode<<7;

   
   //14
   buf[70-14]=readLocalVerisonInformation<<3|readLocalSupportedFeatures<<5
      |readLocalExtendedFeatures<<6|ReadBufferSize<<7;

   //15
   buf[70-15]=readCountryCode|readBdAddress<<1|readFailedContactCounter<<2
      |resetFailedContactCounter<<3|getLinkQuality<<4|readRssi<<5
      |readAfhChannelMap<<6|ReadBdClock<<7;

   //16
   buf[70-16]=readLoopbackMode|writeLoopbackMode<<1|enableDeviceUnderTestMode<<2
      |setupSynchronousConnection<<3|acceptSynchronousConnection<<4|rejectSynchronousConnection<<5;
   
   
   //20
   buf[70-20]=sendKeyPressNotification<<2|IoCapabiltyResponseNegativeReply<<3
      |readEncryptionKeySize<<4;
   
   
   //21
   buf[70-21]=createPhysicalLink|acceptPhysicalLink<<1|disconnectPhysicalLink<<2
      |createLogicallLink<<3|acceptLogicalLink<<4|disconnectLogicalLink<<5
      |logicalLinkCancel<<6|flowSpecMode<<7;
   
   
   //22
   buf[70-22]=readLogicalLinkAcceptTmo|writeLogicalLinkAcceptTmo<<1|setEventMaskPage2<<2
      |readLocationData<<3|wrietLocatonData<<4|readlocalAmpInfo<<5
      |readLocalAmpAssoc<<6|writeRemoteAmpassoc<<7;
   
   //23
   buf[70-23]=readFlowControlMode|writeFlowControlMode<<1|readDataBlockSize<<2
      |enableAmpReceiverReports<<5|ampTestEnd<<6|ampTestComman<<7;
   
   //24 
   buf[70-24]=readEnhancedTransmitPowerLevel|readBestEffortFlushTmo<<2|writeBestEffortFlushTmo<<3
      |shortRangeMode<<4;
         
#ifdef DEBUG_COMMAND_PARSER
   DE_TRACE_STATIC(TR_AMP_HIGH_RES, "return\n");
#endif   
   return 1;
}

void ReadLocalSupportedFeatures(char *buf)
{
   //0x000019987E0602BF
   buf[7]=0x00;
   buf[8]=0x00;
   buf[9]=0x19;
   buf[10]=0x98;
   buf[11]=0x7E;
   buf[12]=0x06;
   buf[13]=0x02;
   buf[14]=0xBF;
}

