// dk_cmds.h - contains definitions for dk commands 

// Copyright (c) 2000 Atheros Communications, Inc., All Rights Reserved 

#ident	"ACI $Id: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/dk_cmds.h#3 $, $Header: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/dk_cmds.h#3 $"

// modification history
// --------------------
// 00jan02 	sharmat		created (copied from windows client)

// DESCRIPTION
// -----------
// Contains the definitions for all of the commands sent between dk_master and dk_client.

#ifndef	 __INCdk_cmdsh
#define	 __INCdk_cmdsh

#include "wlantype.h"

#include  "athreg.h"
//#if defined(ECOS)
//#else // THIN_CLIENT_BUILD
#include "mdata.h"
#include "manlib.h"
//#endif // THIN_CLIENT_BUILD

#include  "event.h"
#ifndef _MLD
#include "event.h"
#else
#ifdef JUNGO
#include "mld.h"
#endif
#if defined(VXWORKS) || defined(SOC_AP)
#include "hw.h"
#else
#include "common_hw.h"
#endif
#endif // _MLD

// Error codes for commands sent back to dk_perl 
#define CMD_OK 0x0
#define COMMS_ERR_BAD_LENGTH 0x1
#define BAD_LENGTH_MSG "Length of command received from dk_perl does not match expected length for CmdID\n"
#define COMMS_ERR_DEV_INIT_FAIL 0x2
#define DEV_INIT_FAIL_MSG "There was a failure in dk_c, somewhere in deviceInit() function\n"
#define	COMMS_ERR_INDEX_TOO_LARGE 0x3
#define INDEX_TOO_LARGE_MSG "Not able to create any more devInfo structures in dk_c. Too many F2's in system?\n"
#define COMMS_ERR_BAD_OFFSET 0x4
#define BAD_OFFSET_MSG "An invalid offset was given for a register access\n"
#define COMMS_ERR_REGS_NOT_MAPPED 0x5
#define REGS_NOT_MAPPED_MSG "F2 registers have not been mapped in dk_c\n"
#define COMMS_ERR_NO_F2_TO_SELECT 0x6
#define NO_F2_TO_SELECT_MSG "Error in dk_c, incorrect F2 number specified in select_hw cmd\n"
#define COMMS_ERR_BAD_CFG_SIZE 0x7
#define BAD_CFG_SIZE_MSG "Illegal PCI config access size sent to dk_c, must be 8, 16 or 32\n"
#define COMMS_ERR_ALLOC_FAIL 0x8
#define ALLOC_FAIL_MSG "Unable to allocate memory in dk_c\n"
#define COMMS_ERR_BAD_MEM_SIZE 0x9
#define BAD_MEM_SIZE_MSG "Illegal memory access size sent to dk_c, must be 8, 16 or 32\n"
#define COMMS_ERR_WRITE_BLOCK_FAIL 0xa
#define WRITE_BLOCK_FAIL_MSG "Failed in call to hwMemWriteBlock()\n"
#define COMMS_ERR_READ_BLOCK_FAIL 0xb
#define READ_BLOCK_FAIL_MSG "Failed in call to hwMemReadBlock()\n"
#define COMMS_ERR_REMAP_FAIL 0xc
#define REMAP_FAIL_MSG "Failed in the call to hwRemapHardware()\n"
#define COMMS_ERR_EVENT_CREATE_FAIL 0xd
#define EVENT_CREATE_FAIL_MSG "Failed in call to hwCreateEvent()\n"
#define COMMS_ERR_NO_EVENTS 0xe
#define NO_EVENTS_MSG "Triggered Q has not events to get\n"
#define COMMS_BAD_STAT_ID 0xf
#define BAD_STAT_ID_MSG "Illegal statID, use get_all_stats cmds\n"
#define COMMS_ERR_EVENT_ENABLE_FEATURE_FAIL 0x10
#define COMMS_ERR_EVENT_DISABLE_FEATURE_FAIL 0x11
#define COMMS_ERR_MDK_ERROR 0x12
#define MDK_ERROR_MSG "MDK Error: Check the mdkerrno"
#define COMMS_ERROR_FTPDOWNLOAD_FAIL 0x20
// Command status 32 bits 
// Lower 16 bits gives the Error Number.
// Upper 16 bits contains some more information for that Error.
#define COMMS_ERR_MASK 0xffff
#define COMMS_ERR_SHIFT 0
#define COMMS_ERR_INFO_MASK 0xffff0000
#define COMMS_ERR_INFO_SHIFT 16
#define MAX_GENERIC_CMD_LEN		1024

#define MAX_NUM_FIELDS			100
#define MAX_PCDACS				64
#define MAX_BUFFER_SIZE			2048
#define NUM_BYTES_PER_EEPROM_LOC  2


// defined in otpstream_api.h on target, not available on host
#define OTPSTREAM_MAXSZ_APP 256
enum otpstream_op_app {
        OTPSTREAM_READ_APP  = 1,
        OTPSTREAM_WRITE_APP = 2,
};


#ifdef __cplusplus
extern "C" {
#endif //__cplusplus 

struct phsChannelToggle{
	  A_UINT32 devIndex;
      A_INT32 ch_int_cal;
      A_INT32 ch_frac_cal;
      A_INT32 ch1_int;
	  A_INT32 ch1_frac;
	  A_INT32 ch2_int;
	  A_INT32 ch2_frac;
	  A_INT32 delay_us;
};


// command ID's for the commands 
enum COMMAND_IDS {
	INIT_F2_CMD_ID,                         //0
	SELECT_HW_CMD_ID,						//1
	MEM_WRITE_CMD_ID,						//2
	MEM_READ_CMD_ID,						//3	
	REG_READ_CMD_ID,						//4
	REG_WRITE_CMD_ID,						//5
	CFG_READ_CMD_ID,						//6
    	CFG_WRITE_CMD_ID,					//7
    	MEM_ALLOC_CMD_ID,					//8
    	MEM_FREE_CMD_ID,					//9
    	MEM_WRITE_BLOCK_CMD_ID,				//10
    	MEM_READ_BLOCK_CMD_ID,				//11
    	REMAP_HW_CMD_ID,					//12
    	CREATE_EVENT_CMD_ID,				//13
    	ANY_EVENTS_CMD_ID,					//14
    	GET_EVENT_CMD_ID,					//15
    	DISCONNECT_PIPE_CMD_ID,				//16
    	CLOSE_PIPE_CMD_ID,					//17
    	ISR_FEATURE_ENABLE_CMD_ID,			//18
    	ISR_GET_RX_STATS_CMD_ID,			//19
    	ISR_GET_TX_STATS_CMD_ID,			//20
    	ISR_SINGLE_RX_STAT_CMD_ID,			//21
    	ISR_SINGLE_TX_STAT_CMD_ID,			//22	
    	WAIT_ON_EVENT_CMD_ID,				//23
	WAIT_ON_TIME_CMD_ID,					//24
	ISR_FEATURE_DISABLE_CMD_ID,				//25
	M_EEPROM_READ_CMD_ID,					//26
	M_EEPROM_WRITE_CMD_ID,					//27
	M_EEPROM_READ_BLOCK_CMD_ID,				//28
	M_EEPROM_WRITE_BLOCK_CMD_ID,			//29
	M_RESET_DEVICE_CMD_ID,					//30
	M_GENERIC_CMD_ID,						//31
    	M_CHECK_REGS_CMD_ID,				//31
    	M_CHANGE_CHANNEL_CMD_ID,			//33
    	M_CHECK_PROM_CMD_ID,				//34
    	M_REREAD_PROM_CMD_ID,				//35
    	M_TX_DATA_SETUP_CMD_ID,				//36
    	M_TX_DATA_BEGIN_CMD_ID,				//37
	M_TX_DATA_START_CMD_ID,					//38
   	M_TX_DATA_COMPLETE_CMD_ID,				//39
    	M_RX_DATA_SETUP_CMD_ID,				//40
    	M_RX_DATA_BEGIN_CMD_ID,				//41
	M_RX_DATA_START_CMD_ID,					//42
   	M_RX_DATA_COMPLETE_CMD_ID,				//43
	M_RX_STATS_SNAPSHOT_CMD_ID,				//44
    	M_TXRX_DATA_BEGIN_CMD_ID,			//45
	M_CLEANUP_TXRX_MEMORY_CMD_ID,			//46
    	M_TX_GET_STATS_CMD_ID,				//47
    	M_RX_GET_STATS_CMD_ID,				//48
    	M_RX_GET_DATA_CMD_ID,				//49
    	M_TX_CONT_BEGIN_CMD_ID,				//50
	M_TX_CONT_FRAME_BEGIN_CMD_ID,			//51
    	M_TX_CONT_END_CMD_ID,				//52
    	M_SET_ANTENNA_CMD_ID,				//53
    	M_SET_POWER_SCALE_CMD_ID,			//54
    	M_SET_TRANSMIT_POWER_CMD_ID,		//55
    	M_SET_SINGLE_TRANSMIT_POWER_CMD_ID,	//56
    	M_DEV_SLEEP_CMD_ID,					//57
    	M_CLOSE_DEVICE_CMD_ID,				//58
	M_CHANGE_FIELD_CMD_ID,					//59
	M_ENABLE_WEP_CMD_ID,					//60
	M_ENABLE_PA_PRE_DIST_CMD_ID,			//61
	M_DUMP_REGS_CMD_ID,						//62
	M_DUMP_PCI_WRITES_CMD_ID,				//63
	M_TEST_LIB_CMD_ID,						//64
	M_DISPLAY_FIELD_VALUES_CMD_ID,			//65
	M_GET_FIELD_VALUE_CMD_ID,				//66
	M_READ_FIELD_CMD_ID,					//67
	M_WRITE_FIELD_CMD_ID,					//68
	M_SET_RESET_PARAMS_CMD_ID,				//69
	M_CHANGE_MULTIPLE_FIELDS_ALL_MODES_CMD_ID, //70	
	M_CHANGE_MULTIPLE_FIELDS_CMD_ID,		//71
	M_GET_FIELD_FOR_MODE_CMD_ID,			//72
	LOAD_AND_RUN_CODE_CMD_ID,				//73
	M_FORCE_SINGLE_PCDAC_TABLE_CMD_ID,		//74
	M_FORCE_PCDAC_TABLE_CMD_ID,				//75
	M_FORCE_POWER_TX_MAX_CMD_ID,			//76
	M_GET_EEPROM_STRUCT_CMD_ID,				//77
	M_GET_DEVICE_INFO_CMD_ID,				//78
	M_WRITE_PROD_DATA_CMD_ID,				//79
	M_WRITE_NEW_PROD_DATA_CMD_ID,			//80
	M_SPECIFY_SUBSYSTEM_CMD_ID,				//81
	M_FORCE_SINGLE_POWER_TX_MAX_CMD_ID,		//82
	M_SET_QUEUE_CMD_ID,						//83
	M_MAP_QUEUE_CMD_ID,						//84
	M_CLEAR_KEY_CACHE_CMD_ID,				//85
	RUN_SCREENING_TEST_CMD_ID,				//86
	DIAG_CMD,								//87
	ENABLE_HW_CAL_CMD,						//88
   	TRAM_WRITE_BLOCK_CMD_ID,				//89
   	TRAM_READ_BLOCK_CMD_ID,					//90
	M_GET_MAX_POWER_CMD_ID,					//91
	M_GET_PCDAC_FOR_POWER_CMD_ID,			//92
	M_FALSE_DETECT_BACKOFF_VALS_CMD_ID,		//93
	M_GET_MAC_ADDR_CMD_ID,					//94
	M_MASK_TRIGGER_SWEEP_CMD_ID,			//95
	M_MASK_DETECT_SIGNAL_CMD_ID,			//96
	M_MASK_SET_DEV_NUMS_CMD_ID,				//97
	M_MASK_FORCE_MIN_CCAPWR_CMD_ID,			//98
	M_MASK_CONFIG_CAPTURE_CMD_ID,			//99
	AP_REG_READ_CMD_ID,						//100
	AP_REG_WRITE_CMD_ID,					//101
	M_SET_LIB_CONFIG_CMD_ID,				//102
	M_GET_XPDGAIN_CMD_ID,					//103
	M_SELECT_DEV_NUM_CMD_ID,				//104
	M_GET_POWER_INDEX_CMD_ID,				//105
	M_GET_ART_ANI_LEVEL_CMD_ID,				//106
	M_SET_ART_ANI_LEVEL_CMD_ID,				//107
	M_FTP_DOWNLOAD_FILE_CMD_ID,				//108
	M_GET_CTL_INFO_CMD_ID,					//109
	M_GET_MAX_LIN_PWR_CMD_ID,				//110
	M_SET_CHAIN_CMD_ID,                     //111
	M_GET_PHASE_CAL_CMD_ID,                 //112
   	M_TX_DATA_SETUP_NO_END_PACKET_CMD_ID,   //113
	M_HW_RESET_CMD_ID,						//114
	M_PLL_PROGRAM_CMD_ID,					//115
	M_PCI_WRITE_CMD_ID,						//116
	M_CAL_CHECK_CMD_ID,						//117
	M_FILL_TX_STATS_CMD_ID,					//118
	M_CREATE_DESC_CMD_ID,					//119
	LOAD_AND_PROGRAM_CODE_CMD_ID,			//120
	M_FORCE_SINGLE_PCDAC_TABLE_GRIFFIN_CMD_ID, 	//121
	PHS_CHANNEL_TOGGLE_CMD_ID, 				//122
	STOP_PHS_CHANNEL_TOGGLE_CMD_ID, 		//123
	GENERIC_FN_CALL_CMD_ID, 				//124
	SEND_FRAME_CMD_ID,					    //125
	RECV_FRAME_CMD_ID,					    //126
	M_EEPROM_WRITE_BYTEBASED_BLOCK_CMD_ID,	//127
    GET_ENDIAN_MODE_CMD_ID,                 //128
	M_TX_DATA_AGG_SETUP_CMD_ID,             //129
	M_RX_DATA_AGG_SETUP_CMD_ID,             //130
	IQ_CAL_CMD_ID,						    //131
    RTC_REG_WRITE_CMD_ID,                   //132
    RTC_REG_READ_CMD_ID,                    //133
    M_CHANGE_ADDAC_FIELD_CMD_ID,            //134
    M_SAVE_XPA_BIAS_FREQ_CMD_ID,            //135
    M_PA_OFFSET_CAL_CMD_ID,                 //136

    // tbd: in case more cmds added at main branch
    // migrated from AR6002
    M_EEPROM_READ_LOCS_CMD_ID = 160,        //160
    GET_REF_CLK_SPEED_CMD_ID,               //161
    SLEEP_CMD_ID,                           //162

    ART_IMAGE_DOWNLOAD_ID,                  //163
    ART_TEST_UITRON,                        //164
    ART_TEST_CLIENT,                        //165

    ART_CLIENT_REWIND_ID,                   //166
    ART_THROUGHPUT_TEST_CMD_ID,             //167
    ART_THROUGHPUT_TEST_CMD_ID_1,           //168

    ART_BMI_WRITE_SOC_REGISTER_ID,          //169
    ART_BMI_WRITE_SOC_MEMORY_ID,            //170
    ART_BMI_OP_DONE_ID,                     //171
    ART_BMI_READ_SOC_REGISTER_ID,           //172
    ART_BMI_READ_SOC_TARGET_ID,             //173

    // added for Venus
    OTP_WRITE_CMD_ID,                       //174
    OTP_READ_CMD_ID,                        //175
    OTP_RESET_CMD_ID,                       //176
    EFUSE_READ_CMD_ID,                      //177
    M_WRITE_CARD_LABEL_TO_FILE,             //178

    // Bluetooth commands
    BTART_SEND_CMD_ID,                      //179
    BTART_RECEIVE_CMD_ID,                   //180

    BT_WRITE_CMD_ID,                        //181
    
    EFUSE_WRITE_CMD_ID,                      //182    
};

// number of bytes that can send in a block read or write command.
// limit is impossed by the communications buffers.
#define	MAX_BLOCK_BYTES 3900
#define MAX_FIELD_SIZE 256
#define	MAX_BLOCK_DWORDS (MAX_BLOCK_BYTES / 4)
#define	MAX_BLOCK_SWORDS (MAX_BLOCK_BYTES / 2)
#define MAX_NUM_FIELDS			100

// structures for the commands 
typedef struct cmdReply {
	A_UINT32 replyCmdLen;
  	A_UINT32 replyCmdId; // command ID of command to which this is a reply 
  	A_UINT32 status; // status of the command 
  	A_UCHAR cmdBytes[4000]; // bytes of the command reply 
} CMD_REPLY;


typedef struct {
// Always make sure these start bytes are DWORD aligned
	A_UINT16 cmdLen;  
  	A_UINT8 cmdID;
    A_INT8 devNum;

  	union {
    		struct memWriteCmd {
      			A_UINT32 writeAddr;
      			A_UINT32 writeValue;
      			A_UINT32 writeSize;
    		} MEM_WRITE_CMD;

    		struct memReadCmd {
      			A_UINT32 readAddr;
       			A_UINT32 readSize;
    		} MEM_READ_CMD;

    		struct initF2Cmd {
      			A_UINT32 whichF2; // which F2 in system to initialize 
    		} INIT_F2_CMD;

    		struct regReadCmd {
      			A_UINT32 readAddr; // address to read from 
    		} REG_READ_CMD;

    		struct regWriteCmd {
      			A_UINT32 writeAddr; // address to write to 
      			A_UINT32 regValue; // value to write 
    		} REG_WRITE_CMD;
               struct rtcRegReadCmd {
                               A_UINT32 readAddr; // address to read from 
               } RTC_REG_READ_CMD;

               struct rtcRegWriteCmd {
                               A_UINT32 writeAddr; // address to write to 
                               A_UINT32 regValue; // value to write 
               } RTC_REG_WRITE_CMD;

    		struct selectHwCmd {
      			A_UINT32 whichF2; // which F2 in system to initialize 
    		} SELECT_HW_CMD;

    		struct cfgReadCmd {
      			A_UINT32 cfgReadAddr; // address to read from 
      			A_UINT32 readSize;  // size to read 32, 16 or 8 
    		} CFG_READ_CMD;

    		struct cfgWriteCmd {
      			A_UINT32 cfgWriteAddr; // address to write to 
      			A_UINT32 cfgValue; // value to write 
      			A_UINT32 writeSize; // size to write, 32, 16 or 8 
    		} CFG_WRITE_CMD;

    		struct memReadBlockCmd {
      			A_UINT32 physAddr; // phys address to read from 
      			A_UINT32 length; // how many bytes to read
    		} MEM_READ_BLOCK_CMD;

    		struct memWriteBlockCmd {
      			A_UINT32 physAddr; // phys address to allocate at 
      			A_UINT32 length; // how many bytes to write 
      			A_UINT8 bytes[MAX_BLOCK_BYTES]; // bytes to be written 
    		} MEM_WRITE_BLOCK_CMD;

    		struct remapHwCmd {
      			A_UINT32 mapAddress; // new address to map to 
    		} REMAP_HW_CMD;

    		struct createEventCmd {
      			A_UINT32 type;
      			A_UINT32 persistent;
      			A_UINT32 param1;
      			A_UINT32 param2;
      			A_UINT32 param3;
      			EVT_HANDLE eventHandle;
    		} CREATE_EVENT_CMD;

    		// devNum command struct is for all funcs that only take
    		// only a devNum as an argument
    		struct eepromReadCmd {
      			A_UINT32 offset;
    		} EEPROM_READ_CMD;

    		struct eepromWriteCmd {
      			A_UINT32 offset;
      			A_UINT32 value;
    		} EEPROM_WRITE_CMD;

            struct hwResetCmd {
                    A_UINT8 resetMask;
            } __ATTRIB_PACK HW_RESET_CMD;

            struct pllProgramCmd {
                    A_UINT8 turbo;
                    A_UINT8 mode;
            } PLL_PROGRAM_CMD;
			
			struct pciWriteCmd {
					A_UINT32 length;
					PCI_VALUES pPciValues[MAX_PCI_ENTRIES_PER_CMD];
			} PCI_WRITE_CMD;
	
			struct calCheckCmd {
				A_UINT32 enableCal;
				A_UINT32 timeout;
			} CAL_CHECK_CMD;

    		struct fillTxStatsCmd {
      			A_UINT32 descAddress;
      			A_UINT32 numDesc;
      			A_UINT32 dataBodyLen;
      			A_UINT32 txTime;
			} FILL_TX_STATS_CMD;

    		struct createDescriptors {
      			A_UINT32 descBaseAddress;
      			A_UINT32 descInfo;
      			A_UINT32 bufAddrIncrement;
      			A_UINT32 descOp;
				A_UINT32 descWords[MAX_DESC_WORDS];
			} CREATE_DESC_CMD;

		struct eepromReadBlockCmd {
      			A_UINT32 startOffset;
      			A_UINT32 length; // how many eeprom words to read 
    		} EEPROM_READ_BLOCK_CMD;

    		struct eepromWriteBlockCmd {
      			A_UINT32 startOffset;
      			A_UINT32 length; // how many eeprom words to read 
      			A_UINT32 eepromValue[MAX_BLOCK_DWORDS]; // words to be written 
    		} EEPROM_WRITE_BLOCK_CMD;

                struct eepromWriteByteBasedBlockCmd {
                        A_UINT32 startOffset;
                        A_UINT32 length; // how many eeprom bytes to read
                        A_UINT8 eepromValue[MAX_BLOCK_BYTES]; // bytes to be written
                } EEPROM_WRITE_BYTEBASED_BLOCK_CMD;

    		struct apRegReadCmd {
      			A_UINT32 readAddr; // address to read from 
       			A_UINT32 readSize;
    		} AP_REG_READ_CMD;

    		struct apRegWriteCmd {
      			A_UINT32 writeAddr; // address to write to 
      			A_UINT32 regValue; // value to write 
      			A_UINT32 writeSize;
    		} AP_REG_WRITE_CMD;

			struct writeProdDataCmd	{
				A_UCHAR wlan0Mac[6];
				A_UCHAR wlan1Mac[6];
				A_UCHAR enet0Mac[6];
				A_UCHAR enet1Mac[6];
			} WRITE_PROD_DATA_CMD;

			struct eepromReadLocsCmd {
      			A_UINT32 startOffset;
      			A_UINT32 length; // how many eeprom locs to read 
                        A_UINT8 useFlash;
    		} EEPROM_READ_LOCS_CMD;

	    	struct sleepFnCmd {
				A_UINT32 id;
			} SLEEP_CMD;

			struct LoadAndProgramCodeCmd {
				A_UINT32 loadFlag;
				A_UINT32 csAddr;
				A_UINT32 pPhyAddr;
				A_UINT32 length;
 				A_UCHAR  pBuffer[MAX_BUFFER_SIZE];
			} LOAD_AND_PROGRAM_CODE_CMD;

	    	struct phsChannelToggle PHS_CHANNEL_TOGGLE_CMD;

	    	struct stopPhsChannelToggleCmd {
				A_UINT8 dummy;
			} STOP_PHS_CHANNEL_TOGGLE_CMD;

	    	struct genericFnCallCmd {
				ST_GENERIC_FN_CALL stGFnCallCmd;
			} GENERIC_FN_CALL_CMD;

	    	struct sendFrameCmd {
      			A_UINT32 tx_desc_ptr;
      			A_UINT32 tx_buf_ptr;
      			A_UINT32 rx_desc_ptr;
      			A_UINT32 rx_buf_ptr;
      			A_UINT32 rate_code;
 				A_UINT8  pBuffer[MAX_LB_FRAME_LEN];
			} SEND_FRAME_CMD;

	    	struct recvFrameCmd {
      			A_UINT32 tx_desc_ptr;
      			A_UINT32 tx_buf_ptr;
      			A_UINT32 rx_desc_ptr;
      			A_UINT32 rx_buf_ptr;
      			A_UINT32 rate_code;
			} RECV_FRAME_CMD;

    		struct resetDeviceCmd {
      			A_UCHAR  mac[6];
      			A_UCHAR  bss[6];
      			A_UINT32 freq;
      			A_UINT32 turbo;
      			A_UINT8  wlanMode;      			
    		} RESET_DEVICE_CMD;


#if defined(THIN_CLIENT_BUILD)
#else // ECOS
			struct writeNewProdDataCmd	{
				A_UINT32  devNum;
				A_INT32   argList[16];
				A_UINT32  numArgs;
			} WRITE_NEW_PROD_DATA_CMD;

    		struct memAllocCmd {
      			A_UINT32 allocSize; // num bytes to allocate 
      			A_UINT32 physAddr;  // phys address to allocate at 
    		} MEM_ALLOC_CMD;

    	 //	struct memFreeCmd {
         //	} MEM_FREE_CMD;

    		struct waitEventCmd {
      			A_UINT32 eventID;
      			A_UINT32 simulationTime;
      			A_UINT32 returnValue;
    		} WAIT_EVENT_CMD;

    		struct waitTimeStruct {
      			A_UINT32 timeToWait;
    		} WAIT_ON_TIME_CMD;

            struct maskForceMinCCAPWR {
                    A_UINT32 maxccapwr;
            } MASK_FORCE_MIN_CCAPWR_CMD;

    		struct isrFeatureEnableCmd {
      			A_UINT32 featureID;
      			A_UINT32 param1;
      			A_UINT32 param2;
    		} ISR_FEATURE_ENABLE_CMD;

    		struct isrFeatureDisableCmd {
      			A_UINT32 featureID;
    		} ISR_FEATURE_DISABLE_CMD;

    		struct isrGetStatsCmd {
      			A_UINT32 clearOnRead;
    		} ISR_GET_STATS_CMD;

    		struct isrSingleStatCmd {
      			A_UINT32 statID;
      			A_UINT32 clearOnRead;
    		} ISR_SINGLE_STAT_CMD;

    		// setOne provides a devNum and a single parameter
    		struct setOneCmd {
      			A_UINT32 param;
    		} SET_ONE_CMD;

			struct genericCmd
			{
				A_INT32  intVar1;
				A_INT32  intVar2;
				A_INT32  intVar3;
				A_CHAR	 stringVar[MAX_GENERIC_CMD_LEN];
			} GENERIC_CMD;

    		struct txDataSetupCmd {
      			A_UINT32 rateMask;
      			A_UCHAR  dest[6];
      			A_UINT32 numDescPerRate;
      			A_UINT32 dataBodyLength;
      			A_UINT32 retries;
      			A_UINT32 antenna;
      			A_UINT32 broadcast;
      			A_UINT32 dataPatternLength;
      			A_UCHAR  dataPattern[MAX_BLOCK_BYTES]; // bytes to be written 
    		} TX_DATA_SETUP_CMD;

     		struct txDataSetupNoEndPacketCmd {
      			A_UINT32 devNum;
      			A_UINT32 rateMask;
      			A_UCHAR  dest[6];
      			A_UINT32 numDescPerRate;
      			A_UINT32 dataBodyLength;
      			A_UINT32 retries;
      			A_UINT32 antenna;
      			A_UINT32 broadcast;
      			A_UINT32 dataPatternLength;
      			A_UCHAR  dataPattern[MAX_BLOCK_BYTES]; // bytes to be written 
    		} TX_DATA_SETUP_NO_END_PACKET_CMD;

    		struct txDataBeginCmd {
      			A_UINT32 timeout;
      			A_UINT32 remoteStats;
    		} TX_DATA_BEGIN_CMD;

    		struct rxDataSetupCmd {
      			A_UINT32 numDesc;
      			A_UINT32 dataBodyLength;
      			A_UINT32 enablePPM;
    		} RX_DATA_SETUP_CMD;

    		struct rxDataAggSetupCmd {
      			A_UINT32 numDesc;
      			A_UINT32 dataBodyLength;
      			A_UINT32 enablePPM;
				A_UINT32 aggSize;
    		} RX_DATA_AGG_SETUP_CMD;

			// also used by the txrxDataBegin()
    		struct rxDataBeginCmd {
      			A_UINT32 waitTime;
      			A_UINT32 timeout;
      			A_UINT32 remoteStats;
      			A_UINT32 enableCompare;
      			A_UINT32 dataPatternLength;
      			A_UINT8  dataPattern[MAX_BLOCK_BYTES]; // bytes to be written 
    		} RX_DATA_BEGIN_CMD;

    		struct getStatsCmd {
      			A_UINT32 rateInMb;
      			A_UINT32 remote;
    		} GET_STATS_CMD;

    		struct rxGetDataCmd {
      			A_UINT32 bufferNum;
      			A_UINT32 sizeBuffer;
    		} RX_GET_DATA_CMD;

    		struct txContBeginCmd {
      			A_UINT32 type;
      			A_UINT32 typeOption1;
      			A_UINT32 typeOption2;
      			A_UINT32 antenna;
    		} TX_CONT_BEGIN_CMD;

			struct txContFrameBeginCmd
			{
			  A_UINT32 length;
			  A_UINT32 ifswait;
			  A_UINT32 typeOption1;
			  A_UINT32 typeOption2;
			  A_UINT32 antenna;
			  A_BOOL   performStabilizePower;
			  A_UINT32 numDescriptors;
     	      A_UCHAR  dest[6];
			}  TX_CONT_FRAME_BEGIN_CMD;

    		struct setTransmitPowerCmd {
      			A_UCHAR  txPowerArray[17];
    		} SET_TRANSMIT_POWER_CMD;


    		struct changeFieldCmd {
      			A_CHAR fieldName[MAX_FIELD_SIZE];
      			A_UINT32  newValue;
    		} CHANGE_FIELD_CMD;

		struct enableWepCmd {
			A_UCHAR key;
		} ENABLE_WEP_CMD;

		struct enablePAPreDistCmd {
			A_UINT16 rate;
			A_UINT32 power;
		} ENABLE_PA_PRE_DIST_CMD;

		struct displayFieldValueCmd {
      			A_CHAR fieldName[MAX_FIELD_SIZE];
    		} DISPLAY_FIELD_VALUES_CMD;
	
		struct getFieldValueCmd {
      			A_CHAR fieldName[MAX_FIELD_SIZE];
	  		A_UINT32 turbo;
    		} GET_FIELD_VALUE_CMD;

		struct readFieldcmd {
      			A_CHAR fieldName[MAX_FIELD_SIZE];
      			A_UINT32  printValue;
    		} READ_FIELD_CMD;

		struct writeFieldCmd {
      			A_CHAR fieldName[MAX_FIELD_SIZE];
      			A_UINT32  newValue;
    		} WRITE_FIELD_CMD;

    		struct setResetParamsCmd {
			A_CHAR fileName[MAX_BUFFER_SIZE];
			A_UINT32 eePromLoad;
			A_UINT32 forceCfgLoad;
			A_UINT32 mode;		
            A_UINT16   use_init;   } SET_RESET_PARAMS_CMD;

		struct getFieldForModeCmd {
			 A_CHAR   fieldName[MAX_FIELD_SIZE];
			 A_UINT32  mode;	
			 A_UINT32  turbo;	
		} GET_FIELD_FOR_MODE_CMD;  

		struct changeMultiFieldsAllModesCmd {
			A_UINT32		  devNum;
			A_UINT32		  numFields;
			PARSE_MODE_INFO FieldsToChange[MAX_NUM_FIELDS];
		} CHANGE_MULTI_FIELDS_ALL_MODES_CMD;

		struct changeMultiFieldsCmd {
			A_UINT32		  devNum;
			A_UINT32		  numFields;			
			PARSE_FIELD_INFO FieldsToChange[MAX_NUM_FIELDS];
		} CHANGE_MULTI_FIELDS_CMD;

		struct changeAddacFieldCmd {
			A_UINT32		  devNum;
			PARSE_FIELD_INFO FieldToChange;
		} CHANGE_ADDAC_FIELD_CMD;

		struct saveXpaBiasLvlFreqCmd {
			A_UINT32		  devNum;
			PARSE_FIELD_INFO  FieldToChange;
            A_UINT16           biasLevel;
        } SAVE_XPA_BIAS_FREQ_CMD;

        struct forceSinglePcdacTableCmd {
			A_UINT16 pcdac;
		} FORCE_SINGLE_PCDAC_TABLE_CMD;

		struct forcePcdacTableCmd {
			A_UINT16 pcdac[MAX_PCDACS];
		} FORCE_PCDAC_TABLE_CMD;

        struct processPAOffsetCalCmd {
			A_UINT32		  devNum;
        } PA_OFFSET_CAL_CMD_ID;

		struct forcePowerTxMaxCmd {
			A_UINT32 length;
			A_UINT16 ratesPower[MAX_BLOCK_SWORDS]; 
		} FORCE_POWER_TX_MAX_CMD;

		struct getEepromStructCmd {
			A_UINT32 padding;
			A_UINT16 eepStructFlag;
		} GET_EEPROM_STRUCT_CMD;


		struct mapQueueCmd {
			A_UINT32 qcuNumber;
			A_UINT32 dcuNumber;
		} MAP_QUEUE_CMD;

		struct runScreeningTestCmd {	
			A_UINT32 testId;
		} RUN_SCREENING_TEST_CMD;

		struct diagCmd {
      			A_UINT32 diagNum;
      			A_UINT32 cmdSize;
      			A_UINT8  data[MAX_BLOCK_BYTES];  // bytes to be written 
    		}  DIAG_CMD;

		struct getMaxPowerCmd {
			A_UINT32 freq;
			A_UINT32 rate;
		} GET_MAX_POWER_CMD;

		struct getPcdacForPower {
			A_UINT32 freq;
			A_INT32 power;
		} GET_PCDAC_FOR_POWER_CMD;

		struct falseDetectBackoffValsCmd {
			A_UINT32 backoffValues[3];
		} FALSE_DETECT_BACKOFF_VALS_CMD;

		struct getMacAddrCmd {
			A_UINT16 wmac;
			A_UINT16 instNo;
		} GET_MAC_ADDR_CMD;

		struct maskConfigCaptureCmd {
			A_UINT32 dut_dev;
			A_UCHAR RX_ID[6]; 
			A_UCHAR BSS_ID[6]; 
			A_UINT32 channel;
			A_UINT32 turbo;
			A_UINT32 gain[5]; 
			A_UINT32 mode;
		} MASK_CONFIG_CAPTURE_CMD;

		struct maskDetectSignalCmd {
			A_UINT32 desc_cnt;
			A_UINT32 adc_des_size;
			A_UINT32 mode;
			A_UINT32 ret_length;
		} MASK_DETECT_SIGNAL_CMD;

		struct maskTriggerSweepCmd {
			A_UINT32 channel;
			A_UINT32 mode;
			A_UINT32 averages;
			A_UINT32 path_loss;
			A_BOOL   return_spectrum; 			
			A_UINT32 ret_length;
		} MASK_TRIGGER_SWEEP_CMD;

			struct setLibConfigCmd {
				LIB_PARAMS libParams;
			} SET_LIB_CONFIG_CMD;
			
			struct setChainCmd {
				A_UINT32 devNum;
				A_UINT32 curChainSel;
				A_UINT32 curPhaseDelta;
			} SET_CHAIN_CMD;

			struct getPhaseCalCmd{
				A_UINT32 devNum;
				A_UINT32 curChan;
			} GET_PHASE_CAL_CMD;

			struct getXpdgainCmd {
				A_INT32  power;
			} GET_XPDGAIN_CMD;

			struct getPowerIndex {
				A_INT32  power;
			} GET_POWER_INDEX_CMD;

			struct getArtAniLevel {
				A_UINT32 artAniType;
			} GET_ART_ANI_LEVEL_CMD;

			struct setArtAniLevel {
				A_UINT32 artAniType;
				A_UINT32 artAniLevel;
			} SET_ART_ANI_LEVEL_CMD;

			struct ftpDownloadFile {
				A_CHAR hostname[MAX_BUFFER_SIZE];
				A_CHAR user[MAX_BUFFER_SIZE];
				A_CHAR passwd[MAX_BUFFER_SIZE];
				A_CHAR remotefile[MAX_BUFFER_SIZE];
				A_CHAR localfile[MAX_BUFFER_SIZE];
				
			} FTP_DOWNLOAD_FILE_CMD;

			struct getMaxLinPwr {
				A_UINT32 devNum;			
			} GET_MAX_LIN_PWR_CMD;

			struct LoadAndRunCodeCmd {
				A_UINT32 loadFlag;
				A_UINT32 pPhyAddr;
				A_UINT32 length;
 				A_UCHAR  pBuffer[256];
			} LOAD_AND_RUN_CODE_CMD;

			struct forceSinglePcdacTableGriffinCmd {
			    A_UINT16 pcdac;
				A_UINT16 offset;
			} FORCE_SINGLE_PCDAC_TABLE_GRIFFIN_CMD;

            struct getEndianMode {
                A_UINT32 devNum;
            } GET_ENDIAN_MODE_CMD;

	   		struct txDataAggSetupCmd {
      			A_UINT32 rateMask;
      			A_UCHAR  dest[6];
      			A_UINT32 numDescPerRate;
      			A_UINT32 dataBodyLength;
      			A_UINT32 retries;
      			A_UINT32 antenna;
      			A_UINT32 broadcast;
				A_UINT32 aggSize;
      			A_UINT32 dataPatternLength;
      			A_UCHAR  dataPattern[MAX_BLOCK_BYTES]; // bytes to be written 
    		} TX_DATA_AGG_SETUP_CMD;

            struct iqCal {
				A_UINT32 devNum;			
            } IQ_CAL_CMD;


#endif // ECOS
            struct __artImageDownloadCmd{
                A_UINT8  ifEnd; // =1 means end; =0 no end
                A_UINT16 block_length;
                A_UINT8 bytes[2048];
                //A_UINT8 bytes[30*1024];
            }ART_IMAGE_DOWNLOAD_CMD;

            struct __artThroughputTestCmd{
                A_UINT32 length;
                A_UINT8 bytes[2*1024];
            }ART_THROUGHPUT_TEST_CMD;

            struct _ART_BMI_WRITE_SOC_REGISTER{
                A_UINT32 address;
                A_UINT32 value;
            }ART_BMI_WRITE_SOC_REGISTER_CMD;

            struct _ART_BMI_WRITE_SOC_MEMORY{
                A_UINT32 baseaddress;
                A_UINT32 blockLength;
                A_UINT8 byte[2048];
            }ART_BMI_WRITE_SOC_MEMORY_CMD; 

            struct otpWriteCmd {
                A_UINT32 length; 
                A_UINT8 bytes[OTPSTREAM_MAXSZ_APP]; 
            } OTP_WRITE_CMD;
        
            struct otpReadCmd {
                A_UINT32 length; 
            } OTP_READ_CMD;

            struct otpResetCmd {
                enum otpstream_op_app resetCmd; 
            } OTP_RESET_CMD;

            struct efuseReadCmd {
                A_UINT32 startPos; 
                A_UINT32 length; 
            } EFUSE_READ_CMD;

            struct BtArtGenericCmd {
                A_UINT32 length; 
                A_UINT8 bytes[MAX_GENERIC_CMD_LEN]; 
            } BTART_GENERIC_CMD;
			
            struct BtWriteCmd {
                A_UINT32 length; 
                A_UINT8 bytes[MAX_GENERIC_CMD_LEN]; 
            } BT_WRITE_CMD;
            
            struct efuseWriteCmd {
                A_UINT32 startPos; 
                A_UINT32 length; 
                A_UINT8 bytes[16]; 
            } EFUSE_WRITE_CMD;            
	} __ATTRIB_PACK CMD_U;
} PIPE_CMD;

extern A_STATUS waitForGenericCmd
(
 void *pSock,
 A_UCHAR   *pStringVar,
 A_UINT32  *pIntVar1,
 A_UINT32  *pIntVar2,
 A_UINT32  *pIntVar3
);

#ifdef __cplusplus
}
#endif //__cplusplus 


#endif //__INCdk_cmdsh
