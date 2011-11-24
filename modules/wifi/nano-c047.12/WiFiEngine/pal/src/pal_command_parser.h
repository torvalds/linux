
/******************************************************************************
 *  Copyright (c) 2010 - Sweden Connectivity AB.
 *  ALL RIGHTS RESERVED
 *  
 *  pal_command_parser.h
 *  Parses incomming HCI messages
 *	Take necessary WiFi action and send back the response
 *
 *****************************************************************************/

#ifndef HCI_30_CMD_H_
#define HCI_30_CMD_H_

#include "driverenv.h"

#define DEBUG_COMMAND_PARSER
//#define WE_PAL

//Golden Range
#define GOLDEN_UPPER_LIMIT	-40
#define GOLDEN_LOWER_LIMIT	-60

//Set event filter
#define CLEAR_ALL_FILTERS 		0x00
#define INQUIRY_RESULTS			0x01
#define CONENCTION_SETUP		0x02


//Read AMP info
#define AMP_CONTROLLER_POWERED_DOWN 	0x00
#define AMP_CONTROLLER_BT 		0x01
#define AMP_CONTROLLER_NO_BT		0x02
#define AMP_CONTROLLER_LOW_BT		0x03
#define AMP_CONTROLLER_MEDIUM_BT	0x04
#define AMP_CONTROLLER_HIGH_BT		0x05
#define AMP_CONTROLLER_FULL_BT		0x06

#define AMP_TOTAL_BANDWIDTH		0x00000000 //
#define AMP_GUARANTEED_BANDWITH		0xffffffff //For best effort
#define AMP_MIN_LATENCY			0xffffffff 
#define AMP_MAX_PDU_SIZE		0xffffffff 
#define AMP_CONTROLLER_TYPE		0x01 

#define AMP_MAX_HCI_PACKET_LENGTH	254

#define HCI_SUCCESS 									0x00
#define CONNECTION_TIMEOUT								0x08
#define CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES 	0x0d
#define HCI_CONNECTION_ACCEPT_TIMEOUT_EXCEEDED	 		0x10
#define HCI_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE 		0x11
#define HCI_INVALID_HCI_COMMAND_PARAMETERS 				0x12
#define COMMAND_DISALLOWED 								0x13
#define CONNECTION_TERMINATED_BY_LOCAL_HOST 			0x16
#define HCI_UNSPECIFIED_ERROR 							0x1F

#define NO_TRAFFIC		0x00
#define BEST_EFFORT		0x01
#define GUARANTEED		0x02

#define TOTAL_NUMBER_OF_DATA_BLOCKS 3
//extern de_callback_t;


// SEF Set event filter type
#define CLEAR_ALL_FILTERS 		0x00
#define INQUIRY_RESULTS			0x01
#define CONNECTION_SETUP		0x02

// SEF event condition_type for CONNECTION_SETUP filter ype
#define ALLOW_CONNECTIONS_FROM_ALL 0x00 //Allow Connections from all devices.
#define ALLOW_SPECIFIC_COD 0x01			//Allow Connections from a device with a specific Class of Device.
#define ALLOW_SPECIFIC_BDADDR 0x02		//Allow Connections from a device with a specific BD_ADDR.

//SEF Condition for ALLOW_CONNECTIONS_FROM_ALL condititon_type
#define NO_AUTO_ACCEPT 0x01				//Do NOT Auto accept the connection. (Auto accept is off)
#define AUTO_ACCEPT_ROLE_SWITCH 0x02	//Do Auto accept the connection with role switch disabled. (Auto accept is on).
#define NO_AUTO_ACCEPT_ROLE_SWITCH 0x03 //Do Auto accept the connection with role switch enabled. (Auto accept is on).

//SEF Condition for ALLOW_SPECIFIC_COD condititon_type
#define DEFAULT 0x000000				//Default, Return All Devices.
#define COD_INTEREST 0x111111			//Class of Device of Interest.


//typedef int (*de_callback_t)(void *data, size_t data_len);

/////////////////////////////////////////////////////////////////////////////////////////
#define HCI_Read_Flow_Control_Mode_Cmd_def 0x660c  //(OCF - 0x0066, OGF - 0x0003)
int HCI_Read_Flow_Control_Mode_Cmd_fcn(void* param);

typedef struct
{ 
char evt_hdr;
  char evt_code;
  char length;
  char num_HCI_pkts;
  short opcode;
  char status;
  char flow_control_mode;
}HCI_Read_Flow_Control_Mode_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Write_Flow_Control_Mode_Cmd_def  0x670c// 8(OCF - 0x0067, OGF - 0x0003)
int HCI_Write_Flow_Control_Mode_Cmd_fcn(void* param);

typedef struct
{ char flow_control_mode;
	
}HCI_Write_Flow_Control_Mode_Cmd_struct;

typedef struct
{ 	char evt_hdr;
	char evt_code;
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;
}HCI_Write_Flow_Control_Mode_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Buffer_Size_Cmd_def  0x0510// (OCF - 0x0005, OGF - 0x0004)
int HCI_Read_Buffer_Size_Cmd_fcn(void* param);

typedef struct
{ 	char evt_hdr;
	char evt_code;	
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;
	short HC_ACL_Data_Packet_Length;
	char HC_Synchronous_Data_Packet_Length;
	short HC_Total_Num_ACL_Data_Packets;
	short HC_Total_Num_Synchronous_Data_Packets; 
}HCI_Read_Buffer_Size_evt;

/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Data_Block_Size_Cmd_def 0x0a10  // (OCF - 0x000A, OGF - 0x0004)
int HCI_Read_Data_Block_Size_Cmd_fcn(void* param);

typedef struct
{ 	char evt_hdr;
	char evt_code;
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;
	short Max_ACL_Data_Packet_Length;
	short Data_Block_Length;  
	short Total_Data_Blocks;  
}HCI_Read_Data_Block_Size_evt;
/////////////////////////////////////////////////////////////////////////////////////////


int Number_Of_Complete_Data_Bocks_Evt_fcn(int noOfCompletedPackets);


typedef struct
{ 	
	char evt_hdr;
	char evt_code; //0x48
	char length;
	short Total_Num_Data_Blocks;
	char Number_Of_Handles;
	short Handle;
	short Num_Of_Completed_Packets;
	short Num_Of_Completed_Blocks;
}HCI_Total_Number_Of_Complete_Data_Bocks;

/////////////////////////////////////////////////////////////////////////////////////////////////
int Buffer_OverFlow_Evt_fcn(void);


typedef struct
{ 	
	char evt_hdr;
	char evt_code; //0x1A
	char length;
	char linkType;
}HCI_Buffer_OverFlow_Evt;



///////////////////////////////////////////////////////////////////////////////////////

#define Read_Local_Version_information_Cmd_def  0x0110// (OCF - 0x0001, OGF - 0x0004) 48
int HCI_Read_Local_Version_information_Cmd_fcn(void* param);

typedef struct
{ 	char evt_hdr;
	char evt_code;
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;
	char HCI_Version;
	short HCI_Revision;
	char LMP_Version;
	short Manufacturer_Name;
	short LMP_Subversion;
}HCI_Read_Local_Version_information_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Local_Supported_Commands_Cmd_def 0x0210 // (OCF - 0x0002, OGF - 0x0004)
int HCI_Read_Local_Supported_Commands_Cmd_fcn(void* param);


typedef struct
{  	char evt_hdr;
	char evt_code;
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;
	char Supported_Commands[64];
}HCI_Read_Local_Supported_Commands_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Local_Supported_Features_Cmd_def 0x0310 // (OCF - 0x0003, OGF - 0x0004)
int HCI_Read_Local_Supported_Features_Cmd_fcn(void* param);

typedef struct
{ 	char evt_hdr;
	char evt_code;
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;
	char LMP_features[8];
}HCI_Read_Local_Supported_Features_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Reset_Failed_Contact_Counter_Cmd_def 0x0214  // (OCF - 0x0001, OGF - 0x0005)
int HCI_Reset_Failed_Contact_Counter_Cmd_fcn(void* param);

typedef struct
{ short handle;
	
}HCI_Reset_Failed_Contact_Counter_Cmd_struct;

typedef struct
{ 	char evt_hdr;
	char evt_code;
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;
	short handle;
}HCI_Reset_Failed_Contact_Counter_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Set_Event_Mask_Cmd_def  0x010c// (OCF - 0x0001, OGF - 0x0003)
int HCI_Set_Event_Mask_Cmd_fcn(void* param);

typedef struct
{ char Event_Mask[8];
	
}HCI_Set_Event_Mask_Cmd_struct;

typedef struct
{	char evt_hdr;
	char evt_code;
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;
}HCI_Set_Event_Mask_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Host_Number_Of_Completed_Packets_Cmd_def 0x350c // (OCF - 0x0035, OGF - 0x0003)
int HCI_Host_Number_Of_Completed_Packets_Cmd_fcn(void* param);

typedef struct
{ char Number_Of_Handles;
  short Connection_Handle[7];
  short Host_Num_Of_Completed_Packets[7];
}HCI_Host_Number_Of_Completed_Packets_Cmd_struct;

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;
}HCI_Host_Number_Of_Completed_Packets_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Set_Event_Mask_Page_2_Cmd_def 0x630c // (OCF - 0x0063, OGF - 0x0003)
int HCI_Set_Event_Mask_Page_2_Cmd_fcn(void* param);

typedef struct
{ char Event_Mask_Page_2[8];
}HCI_Set_Event_Mask_Page_2_Cmd_struct;

typedef struct
{ 	char evt_hdr;
	char evt_code;
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;
}HCI_Set_Event_Mask_Page_2_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Enable_Device_Under_Test_Cmd_def  0x0318// (OCF - 0x0003, OGF - 0x0006)
int HCI_Enable_Device_Under_Test_Cmd_fcn(void* param);

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;
}HCI_Enable_Device_Under_Test_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Reset_Cmd_def  0x030C// (OCF - 0x0001, OGF - 0x0003)
int HCI_Reset_Cmd_fcn(void* param);

typedef struct
{  char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;
}HCI_Reset_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Location_Data_Cmd_def 0x640c //  (OCF - 0x064, OGF - 0x0003)
int HCI_Read_Location_Data_Cmd_fcn(void* param);

typedef struct
{  char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;
  char Location_Domain_Aware;
  short Location_Domain;
  char Location_Domain_Options;
  char Location_Options;
}HCI_Read_Location_Data_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Write_Location_Data_Cmd_def 0x650c //    (OCF - 0x065, OGF - 0x0003)
int HCI_Write_Location_Data_Cmd_fcn(void* param);

typedef struct
{ char Location_Domain_Aware;
short Location_Domain;
char Location_Domain_Options;
char Location_Options; 	
}HCI_Write_Location_Data_Cmd_struct;

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;
}HCI_Write_Location_Data_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Connection_Accept_Timeout_Cmd_def 0x150c // (OCF - 0x015, OGF - 0x0003)
int HCI_Read_Connection_Accept_Timeout_Cmd_fcn(void* param);


typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;
  short Conn_Accept_Timeout;
}HCI_Read_Connection_Accept_Timeout_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Write_Connection_Accept_Timeout_Cmd_def  0x160c// (OCF - 0x016, OGF - 0x0003)
int HCI_Write_Connection_Accept_Timeout_Cmd_fcn(void* param);

typedef struct
{	short Conn_Accept_Timeout;	
}HCI_Write_Connection_Accept_Timeout_Cmd_struct;

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;
}HCI_Write_Connection_Accept_Timeout_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Failed_Contact_Counter_Command_Cmd_def  0x0114 //  (OCF - 0x005f, OGF - 0x0003)
int HCI_Read_Failed_Contact_Counter_Command_Cmd_fcn(void* param);

typedef struct
{ 
  short handle;	
}HCI_Read_Failed_Contact_Counter_Command_Cmd_struct;

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;
  short handle;
  short Failed_Contact_Counter;
}HCI_Read_Failed_Contact_Counter_Command_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Link_Supervision_Timeout_Cmd_def  0x360c  //  (OCF - 0x0036, OGF - 0x0003)
int HCI_Read_Link_Supervision_Timeout_Cmd_fcn(void* param);

typedef struct
{ 
  short handle;
}HCI_Read_Link_Supervision_Timeout_Cmd_struct;

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;
  short handle;
  short Link_Supervission_Timeout;
}HCI_Read_Link_Supervision_Timeout_evt;
/////////////////////////////////////////////////////////////////////////////////////////
#define HCI_Write_Link_Supervision_Timeout_Cmd_def  0x370c //   (OCF - 0x0037, OGF - 0x0003)
int HCI_Write_Link_Supervision_Timeout_Cmd_fcn(void* param);

typedef struct
{ short handle;
  short Link_Supervission_Timeout;
	
}HCI_Write_Link_Supervision_Timeout_Cmd_struct;

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;
  short handle;
}HCI_Write_Link_Supervision_Timeout_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Short_Range_Mode_Cmd_def  0x6b0c//  (OCF - 0x006B, OGF - 0x0003)
int HCI_Short_Range_Mode_Cmd_fcn(void* param);

typedef struct
{ char Physical_Link_Handle;
  char 	Short_Range_Mode;
}HCI_Short_Range_Mode_Cmd_struct;

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;  
}HCI_Short_Range_Mode_evt;

typedef struct
{
	char evt_hdr;
	char evt_code; //0x4c
	char length;
	char status;
	char Physical_Link_Handle;
	char Short_Range_State;
}HCI_Short_Range_Mode_Change_Complete_Evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Set_Event_Filter_Cmd_def 0x050c //   (OCF - 0x0005, OGF - 0x0003)
int HCI_Set_Event_Filter_Cmd_fcn(void* param);

typedef struct
{ char Filter_Type;
  char Filter_Condition_Type;
  char Condition;	
}HCI_Set_Event_Filter_Cmd_struct;

typedef struct
{ HCI_Set_Event_Filter_Cmd_struct Set_Event_Filter_Cmd_struct;
  char Class_of_Device[3];
  char Condition;	
}HCI_Set_Event_Filter_Connection_type;


typedef struct
{char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;   
}HCI_Set_Event_Filter_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Local_AMP_Info_Cmd_def  0x0914 //   (OCF - 0x0009, OGF - 0x0005)
int HCI_Read_Local_AMP_Info_Cmd_fcn(void* param);

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;  
  char AMP_Status;
  int Total_Bandwidth;
  int Max_Guaranteed_Bandwidth;
  int Min_Latency;
  int Max_PDU_Size;
  char Controller_Type;
  short PAL_Capabilities;
  short Max_AMP_ASSOC_Length;
  int Max_Flush_Timeout;
  int Best_Effort_Flush_Timeout;  
}HCI_Read_Local_AMP_Info_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_AMP_Test_End_Cmd_def  0x0818//  (OCF - 0x0008, OGF - 0x0006)
int HCI_AMP_Test_End_Cmd_fcn(void* param);

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status;    
}HCI_AMP_Test_End_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_AMP_Test_Cmd_def 0x0918 //  (OCF - 0x0009, OGF - 0x0006)
int HCI_AMP_Test_Cmd_fcn(void* param);

typedef struct
{ char Controller_type;
	
}HCI_AMP_Test_Cmd_struct;

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status; 
}HCI_AMP_Test_Data_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Enable_AMP_Receiver_Report_Cmd_def  0x0718 //   (OCF - 0x0009, OGF - 0x0006)
int HCI_Enable_AMP_Receiver_Report_Cmd_fcn(void* param);


typedef struct
{ char enable;
 char interval;

}HCI_Enable_AMP_Receiver_Report_Cmd_struct;

typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status; 
}HCI_Enable_AMP_Receiver_Report_evt;
/////////////////////////////////////////////////////////////////////////////////////////

int HCI_AMP_Start_Test_Evt_fcn(void* param);


typedef struct
{ char evt_hdr;
char evt_code;
char length;
char num_HCI_pkts;
short opcode;
char status; 
}HCI_AMP_Start_Test_Event;


int HCI_AMP_Test_Command_fcn(void* param);




///////////////////////////////////////////////////////////////////////////////////////////
int HCI_AMP_Receiver_Report_Evt_fcn(char reason);

typedef struct
{ 
	char evt_hdr;
	char evt_code;
	char length;
	char controller_type;
	char reason;
	int event_type;
	char Number_Of_Frames;
	char Number_Of_Error_Frames;
	int Number_Of_Bits;
	int Number_Of_Error_Bits; 
}HCI_AMP_Receiver_Report_evt;


//////////////////////////////////////////////////////



#define HCI_Accept_Logical_Link_Cmd_def  0x3904 //  (OCF - 0x0039, OGF - 0x0001)
int HCI_Accept_Logical_Link_Cmd_fcn(void* param);

typedef struct
{ char Logical_Link_Handle;
  char Tx_Flow_Spec[16];
  char Rx_Flow_Spec[16];	
}HCI_Accept_Logical_Link_Cmd_struct;

typedef struct
{  char evt_hdr;
char evt_code; //ox0f
char length;
char status; 
char num_HCI_pkts;
short opcode;
}HCI_Accept_Logical_Link_status_evt;

typedef struct
{  char evt_hdr;
char evt_code; //0x45
char length;
char status; 
short logical_link_handle;
char phy_link_handle;
char TX_flow_spec_ID;
}HCI_Accept_Logical_Link_complete_evt;

/////////////////////////////////////////////////////////////////////////////////////////


#define HCI_Create_Logical_Link_Cmd_def  0x3804//  (OCF - 0x0038, OGF - 0x0001)
int HCI_Create_Logical_Link_Cmd_fcn(void* param);

typedef struct
{ char Physical_Link_Handle;
char Tx_Flow_Spec[16];
char Rx_Flow_Spec[16];
	
}HCI_Create_Logical_Link_Cmd_struct;

typedef struct
{  char evt_hdr;
char evt_code; //ox0f
char length;
char status; 
char num_HCI_pkts;
short opcode;
}HCI_Create_Logical_Link_status_evt;

typedef struct
{  char evt_hdr;
char evt_code; //0x45
char length;
char status; 
short logical_link_handle;
char phy_link_handle;
char TX_flow_spec_ID;
}HCI_Create_Logical_Link_complete_evt;
/////////////////////////////////////////////////////////////////////////////////////////
int HCI_Disconnect_Physical_Link_Evt(void* param);
#define HCI_Disconnect_Logical_Link_Cmd_def 0x3a04 //  (OCF - 0x003A, OGF - 0x0001)
int HCI_Disconnect_Logical_Link_Cmd_fcn(void* param);

typedef struct
{ short Logical_Link_Handle;
}HCI_Disconnect_Logical_Link_Cmd_struct;

typedef struct
{  char evt_hdr;
char evt_code; //ox0f
char length;
char status; 
char num_HCI_pkts;
short opcode;
}HCI_Disconnect_Logical_Link_status_evt;

typedef struct
{ 
char evt_hdr;
char evt_code; //0x46
char length;
char status; 
short logical_link_handle;
char reason;
}HCI_Disconnect_Logical_Link_complete_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Logical_Link_Cancel_Cmd_def 0x3b04 // (OCF - 0x003B, OGF - 0x0001)
int HCI_Logical_Link_Cancel_Cmd_fcn(void* param);

typedef struct
{ char Physical_Link_Handle;
  char Tx_Flow_Spec_ID;
	
}HCI_Logical_Link_Cancel_Cmd_struct;

typedef struct
{  	char evt_hdr;
	char evt_code;	
	char length;
	char num_HCI_pkts;
	short opcode;
	char status; 
	char Physical_Link_Handle;
	char Tx_Flow_Spec_ID;
}HCI_Logical_Link_Cancel_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_logical_Link_Accept_Timeout_Cmd_def 0x610c  //  (OCF - 0x0061, OGF - 0x0003)
int HCI_Read_logical_Link_Accept_Timeout_Cmd_fcn(void* param);


typedef struct
{  	char evt_hdr;
	char evt_code;	
	char length;
	char num_HCI_pkts;
	short opcode;
	char status; 
	short Logical_Link_Accept_Timeout;
}HCI_Read_logical_Link_Accept_Timeout_evt;

/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Write_logical_Link_Accept_Timeout_Cmd_def  0x620c //  (OCF - 0x0062, OGF - 0x0003)
int HCI_Write_logical_Link_Accept_Timeout_Cmd_fcn(void* param);

typedef struct
{ 	short Logical_Link_Accept_Timeout;	
}HCI_Write_logical_Link_Accept_Timeout_struct;

typedef struct
{ 	char evt_hdr;
	char evt_code;	
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;  
}HCI_Write_logical_Link_Accept_Timeout_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Best_Effort_Flush_Timeout_Cmd_def 0x690c // (OCF - 0x0069, OGF - 0x0003)
int HCI_Read_Best_Effort_Flush_Timeout_Cmd_fcn(void* param);

typedef struct
{ short Logical_Link_Handle;
}HCI_Read_Best_Effort_Flush_Timeout_Cmd_struct;

typedef struct
{ 	char evt_hdr;
	char evt_code;	
	char length;
	char num_HCI_pkts;
	short opcode;
	char status; 
	int Best_Effort_Flush_Timeout;  
}HCI_Read_Best_Effort_Flush_Timeout_evt;
/////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////////


int Flush_Occured_Event(void);

typedef struct
{ 	char evt_hdr;
	char evt_code; //0x11
	char length;
	char Logical_Link_Handle;
}HCI_Flush_Occured_Event;

/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Write_Best_Effort_Flush_Timeout_Cmd_def  0x6a0c //  (OCF - 0x006A, OGF - 0x0003)
int HCI_Write_Best_Effort_Flush_Timeout_Cmd_fcn(void* param);

typedef struct
{ 	short Logical_Link_Handle;
  	int Best_Effort_Flush_Timeout;
}HCI_Write_Best_Effort_Flush_Timeout_Cmd_struct;

typedef struct
{ 	char evt_hdr;
	char evt_code;	
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;  
}HCI_Write_Best_Effort_Flush_Timeout_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Enhanced_Flush_Cmd_def  0x5f0c// Enhanced_Flush (OCF - 0x005f, OGF - 0x0003)
int HCI_Enhanced_Flush_Cmd_fcn(void* param);

typedef struct
{ 	short Handle;
  	char Paket_Type;
}HCI_Enhanced_Flush_Cmd_struct;

typedef struct
{  	char evt_hdr;
	char evt_code; //ox0f
	char length;
	char status; 
	char num_HCI_pkts;
	short opcode;
}HCI_Enhanced_Flush_status_evt;

typedef struct
{ 	char evt_hdr;
	char evt_code; //0x39
	char length;
	short handle;	
}HCI_Enhanced_Flush_complete_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_RSSI_Cmd_def 0x0514 //  (OCF - 0x0005, OGF - 0x0005)
int HCI_Read_RSSI_Cmd_fcn(void* param);

typedef struct
{ 	short handle;	
}HCI_Read_RSSI_Cmd_struct;

typedef struct
{ 	char evt_hdr;
	char evt_code;	
	char length;
	char num_HCI_pkts;
	short opcode;
	char status; 
	short handle;
	char RSSI;  
}HCI_Read_RSSI_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Link_Quality_Cmd_def 0x0314 //  (OCF - 0x0003, OGF - 0x0005)
int HCI_Read_Link_Quality_Cmd_fcn(void* param);

typedef struct
{   short handle;
}HCI_Read_Link_Quality_Cmd_struct;

typedef struct
{ 	char evt_hdr;
	char evt_code;	
	char length;
	char num_HCI_pkts;
	short opcode;
	char status; 
	short handle;
	char Link_Quality;  
}HCI_Read_Link_Quality_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Create_Physical_Link_Cmd_def  0x3504//   (OCF - 0x0035, OGF - 0x0001)
int HCI_Create_Physical_Link_Cmd_fcn(void* param);

typedef struct
{ char Physical_Link_Handle;
  char Dedicated_AMP_Key_Length;
  char Dedicated_AMP_Key_Type; 
  char Dedicated_AMP_Key[16];
}HCI_Create_Physical_Link_struct;

typedef struct
{  	char evt_hdr;
	char evt_code; //ox0f
	char length;
	char status; 
	char num_HCI_pkts;
	short opcode;
}HCI_Create_Physical_Link_status_evt;

typedef struct
{ 	char evt_hdr;
	char evt_code; //ox41
	char length;
	char Physical_Link_Handle;
}HCI_Channel_Selected_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Accept_Physical_Link_Cmd_def  0x3604//  (OCF - 0x0036, OGF - 0x0001)
int HCI_Accept_Physical_Link_Cmd_fcn(void* param);
int Physical_Link_Complete_Event(char reason);

typedef struct
{ 	char Physical_Link_Handle;
	char Dedicated_AMP_Key_Length;
	char Dedicated_AMP_Key_Type; 
	char Dedicated_AMP_Key[16];
}HCI_Accept_Physical_Link_Cmd_struct;

typedef struct
{  	char evt_hdr;
	char evt_code; //ox0f
	char length;
	char status; 
	char num_HCI_pkts;
	short opcode;
}HCI_Accept_Physical_Link_status_evt;

typedef struct
{	char evt_hdr;
	char evt_code; //ox40
	char length;
	char status;
	char Physical_Link_Handle;  
}HCI_Physical_Link_complete_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Disconnect_Physical_Link_Cmd_def  0x3704 //  (OCF - 0x0037, OGF - 0x0001)
int HCI_Disconnect_Physical_Link_Cmd_fcn(void* param);

typedef struct
{ char Physical_Link_Handle;
  char reason;
}HCI_Disconnect_Physical_Link_Cmd_struct;

typedef struct
{  	char evt_hdr;
	char evt_code; //ox0f
	char length;
	char status; 
	char num_HCI_pkts;
	short opcode;
}HCI_Disconnect_Physical_Link_status_evt;

typedef struct
{	char evt_hdr;
	char evt_code; //ox42
	char length;
	char status;
	char Physical_Link_Handle;
	char reason;
}HCI_Disconnection_Physical_Link_complete_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Read_Local_AMP_ASSOC_Cmd_def  0x0a14 //  (OCF - 0x000A, OGF - 0x0005)
int HCI_Read_Local_AMP_ASSOC_Cmd_fcn(void* param);

typedef struct
{ char Physical_Link_Handle;
  short Length_So_Far;
  short Max_Remote_AMP_ASSOC_Length;
}HCI_Read_Local_AMP_ASSOC_Cmd_struct;

typedef struct
{ 	char evt_hdr;
	char evt_code;	
	char length;
	char num_HCI_pkts;
	short opcode;
	char status;  
	char Physical_Link_Handle;
	short AMP_ASSOC_Remaining_Length;
	char AMP_ASSOC_fragment[248];  
}HCI_Read_Local_AMP_ASSOC_evt;
/////////////////////////////////////////////////////////////////////////////////////////

#define HCI_Write_Remote_AMP_ASSOC_Cmd_def  0x0b14//  (OCF - 0x000B, OGF - 0x0005)
int HCI_Write_Remote_AMP_ASSOC_Cmd_fcn(void* param);

typedef struct
{ 	char Physical_Link_Handle;
  	short Length_So_Far;
  	short AMP_ASSOC_Remaining_Length;
  	char AMP_ASSOC_fragment[248];
}HCI_Write_Remote_AMP_ASSOC_Cmd_struct;

typedef struct
{	char evt_hdr;
	char evt_code;	
	char length;
	char num_HCI_pkts;
	short opcode;
	char status; 
	char Physical_Link_Handle;  
}HCI_Write_Remote_AMP_ASSOC_evt;
/////////////////////////////////////////////////////////////////////////////////////////
#define HCI_Flow_Spec_Modify_Cmd_def 0x3c04  //  
int HCI_Flow_Spec_Modify_Cmd_fcn(void* param);

typedef struct
{ 	short handle;
  	char Tx_flow_spec[16];
  	char Rx_flow_spec[16];  
}HCI_Flow_Spec_Modify_Cmd_struct;

typedef struct
{  	char evt_hdr;
	char evt_code; //ox0f
	char length;
	char status; 
	char num_HCI_pkts;
	short opcode;
}HCI_Flow_Spec_Modify_status_evt;

typedef struct
{	char evt_hdr;
	char evt_code; //ox47
	char length;
	char status;
	char handle;	
}HCI_Flow_Spec_Modify_complete_evt;
/////////////////////////////////////////////////////////////////////////////////////////
int Check_Event_Mask(uint16_t netID, char *bufPt, int length);
int Init_Command_Parser(void);

int HCI_Send_Data(void* readBuf);
int number_of_completed_data_blocks(void);

#endif /*HCI_30_CMD_H_*/

