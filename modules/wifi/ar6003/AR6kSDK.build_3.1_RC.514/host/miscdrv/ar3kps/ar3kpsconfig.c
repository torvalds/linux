/*
 * Copyright (c) 2004-2010 Atheros Communications Inc.
 * All rights reserved.
 *
 * This file implements the Atheros PS and patch downloaded for HCI UART Transport driver.
 * This file can be used for HCI SDIO transport implementation for AR6002 with HCI_TRANSPORT_SDIO
 * defined.
 *
 *
 * ar3kcpsconfig.c
 *
 *
 *
 * The software source and binaries included in this development package are
 * licensed, not sold. You, or your company, received the package under one
 * or more license agreements. The rights granted to you are specifically
 * listed in these license agreement(s). All other rights remain with Atheros
 * Communications, Inc., its subsidiaries, or the respective owner including
 * those listed on the included copyright notices..  Distribution of any
 * portion of this package must be in strict compliance with the license
 * agreement(s) terms.
 *
 *
 *
 */



#include "ar3kpsconfig.h"
#ifndef HCI_TRANSPORT_SDIO
#include "hci_ath.h"
#include "hci_uart.h"
#endif /* #ifndef HCI_TRANSPORT_SDIO */

#define MAX_FW_PATH_LEN             50
#define MAX_BDADDR_FORMAT_LENGTH    30

/*
 *  Structure used to send HCI packet, hci packet length and device info 
 *  together as parameter to PSThread.
 */
typedef struct {

    PSCmdPacket *HciCmdList;
    A_UINT32  num_packets;
    AR3K_CONFIG_INFO *dev;
}HciCommandListParam;

A_STATUS SendHCICommandWaitCommandComplete(AR3K_CONFIG_INFO *pConfig,
                                           A_UINT8          *pHCICommand,
                                           int              CmdLength,
                                           A_UINT8          **ppEventBuffer,
                                           A_UINT8          **ppBufferToFree);

void LoadHeader(A_UCHAR *HCI_PS_Command,A_UCHAR opcode,int length,int index);
A_UINT32  Rom_Version;
A_UINT32  Build_Version;
extern A_BOOL BDADDR;
extern A_UINT32 Patch_Count;

A_STATUS getDeviceType(AR3K_CONFIG_INFO *pConfig, A_UINT32 * code);
A_STATUS ReadVersionInfo(AR3K_CONFIG_INFO *pConfig);
A_STATUS set_patch_ram(AR3K_CONFIG_INFO *pConfig,A_UCHAR *patch_loc,A_UINT8 len);
#ifndef HCI_TRANSPORT_SDIO

DECLARE_WAIT_QUEUE_HEAD(PsCompleteEvent);
DECLARE_WAIT_QUEUE_HEAD(HciEvent);
A_UCHAR *HciEventpacket;
rwlock_t syncLock;
wait_queue_t Eventwait;

int PSHciWritepacket(struct hci_dev*,A_UCHAR* Data, A_UINT32 len);
extern char *bdaddr;
#endif /* HCI_TRANSPORT_SDIO */

A_STATUS write_bdaddr(AR3K_CONFIG_INFO *pConfig,A_UCHAR *bdaddr,int type);

int PSSendOps(void *arg);

#ifdef BT_PS_DEBUG
void Hci_log(A_UCHAR * log_string,A_UCHAR *data,A_UINT32 len)
{
    int i;
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s : ",log_string));
    for (i = 0; i < len; i++) {
        printk("0x%02x ", data[i]);
    }
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("\n...................................\n"));
}
#else
#define Hci_log(string,data,len)
#endif /* BT_PS_DEBUG */




A_STATUS AthPSInitialize(AR3K_CONFIG_INFO *hdev)
{
    A_STATUS status = A_OK;
    if(hdev == NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Invalid Device handle received\n"));
        return A_ERROR;
    }

#ifndef HCI_TRANSPORT_SDIO
    DECLARE_WAITQUEUE(wait, current);
#endif /* HCI_TRANSPORT_SDIO */
    

#ifdef HCI_TRANSPORT_SDIO
    status = PSSendOps((void*)hdev);
#else
    if(InitPSState(hdev) == -1) {
        return A_ERROR;
    }
    allow_signal(SIGKILL);
    add_wait_queue(&PsCompleteEvent,&wait);
    set_current_state(TASK_INTERRUPTIBLE);
    if(!kernel_thread(PSSendOps,(void*)hdev,CLONE_FS|CLONE_FILES|CLONE_SIGHAND|SIGCHLD)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Kthread Failed\n"));
        remove_wait_queue(&PsCompleteEvent,&wait);
        return A_ERROR;
    }
    wait_event_interruptible(PsCompleteEvent,(PSTagMode == FALSE));
    set_current_state(TASK_RUNNING);
    remove_wait_queue(&PsCompleteEvent,&wait);

#endif /* HCI_TRANSPORT_SDIO */


    return status;
    
}

int PSSendOps(void *arg) 
{
    int i;
    int ps_index;
    int status = 0;
    PSCmdPacket *HciCmdList; /* List storing the commands */
    const struct firmware* firmware;
    A_UINT32 numCmds;
    A_UINT8 *event;
    A_UINT8 *bufferToFree;
    struct hci_dev *device;
    A_UCHAR *buffer;
    A_UINT32 len;
    A_UINT32 DevType;
    A_UCHAR *PsFileName;
    A_UCHAR *patchFileName;
    A_UCHAR patch_loc[40];
    A_UCHAR *path = NULL;
    A_UCHAR *config_path = NULL;
    A_UCHAR config_bdaddr[MAX_BDADDR_FORMAT_LENGTH];
    AR3K_CONFIG_INFO *hdev = (AR3K_CONFIG_INFO*)arg;
    struct device *firmwareDev = NULL;
    A_UINT8 cFlags = 0;
    A_UINT8 bit7 = 0;

    status = 0;
    HciCmdList = NULL;
#ifdef HCI_TRANSPORT_SDIO
    device = hdev->pBtStackHCIDev; 
    firmwareDev = device->parent;
#else 
    device = hdev;
    firmwareDev = &device->dev;
    AthEnableSyncCommandOp(TRUE);    
#endif /* HCI_TRANSPORT_SDIO */
    /* First verify if the controller is an FPGA or ASIC, so depending on the device type the PS file to be written will be different.
     */

    path =(A_UCHAR *)A_MALLOC(MAX_FW_PATH_LEN);
    if(path == NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Malloc failed to allocate %d bytes for path\n", MAX_FW_PATH_LEN));
        goto complete;
    }
    config_path = (A_UCHAR *) A_MALLOC(MAX_FW_PATH_LEN);
    if(config_path == NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Malloc failed to allocate %d bytes for config_path\n", MAX_FW_PATH_LEN));
        goto complete;
    }

    if(A_ERROR == getDeviceType(hdev,&DevType)) {
        status = 1;
        goto complete;
    }
    if(A_ERROR == ReadVersionInfo(hdev)) {
        status = 1;
        goto complete;
    }

    patchFileName = PATCH_FILE;
    snprintf(path, MAX_FW_PATH_LEN, "%s/%xcoex/",CONFIG_PATH,Rom_Version);
    if(DevType){
        if(DevType == 0xdeadc0de){
	        PsFileName =  PS_ASIC_FILE;
	    } else{
    		AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" FPGA Test Image : %x %x  \n",Rom_Version,Build_Version));
                if((Rom_Version == 0x99999999) && (Build_Version == 1)){
                        
    			AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("FPGA Test Image : Skipping Patch File load\n"));
    			patchFileName = NULL;
		}
	        PsFileName =  PS_FPGA_FILE;
	    }
    }
    else{
	    PsFileName =  PS_ASIC_FILE;
    }

    snprintf(config_path, MAX_FW_PATH_LEN, "%s%s",path,PsFileName);
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%x: FPGA/ASIC PS File Name %s\n", DevType,config_path));
    /* Read the PS file to a dynamically allocated buffer */
    if(A_REQUEST_FIRMWARE(&firmware,config_path,firmwareDev) < 0) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s: firmware file open error\n", __FUNCTION__ ));
        status = 1;
        goto complete;

    }
    if(NULL == firmware || firmware->size == 0) {
        status = 1;
        goto complete;
    }
    buffer = (A_UCHAR *)A_MALLOC(firmware->size);
    if(buffer != NULL) {
    /* Copy the read file to a local Dynamic buffer */
        memcpy(buffer,firmware->data,firmware->size);
        len = firmware->size;
        A_RELEASE_FIRMWARE(firmware);
        /* Parse the PS buffer to a global variable */
        status = AthDoParsePS(buffer,len);
        A_FREE(buffer);
    } else {
        A_RELEASE_FIRMWARE(firmware);
    }


    /* Read the patch file to a dynamically allocated buffer */
	if(patchFileName != NULL)
                snprintf(config_path,
                         MAX_FW_PATH_LEN, "%s%s",path,patchFileName);
	else {
        	status = 0;
	}
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Patch File Name %s\n", config_path));
    if((patchFileName == NULL) || (A_REQUEST_FIRMWARE(&firmware,config_path,firmwareDev) < 0)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s: firmware file open error\n", __FUNCTION__ ));
        /* 
         *  It is not necessary that Patch file be available, continue with PS Operations if.
         *  failed.
         */
        status = 0;

    } else {
        if(NULL == firmware || firmware->size == 0) {
            status = 0;
        } else {
            buffer = (A_UCHAR *)A_MALLOC(firmware->size);
            if(buffer != NULL) {
                /* Copy the read file to a local Dynamic buffer */
                memcpy(buffer,firmware->data,firmware->size);
                len = firmware->size;
                A_RELEASE_FIRMWARE(firmware);
                /* parse and store the Patch file contents to a global variables */
				patch_loc[0] = '\0';
                status = AthDoParsePatch(buffer,len, patch_loc);
		
                A_FREE(buffer);
            } else {
                A_RELEASE_FIRMWARE(firmware);
            }
        }
    }

    /* Create an HCI command list from the parsed PS and patch information */
    AthCreateCommandList(&HciCmdList,&numCmds);
#define CONFIG_PLATFORM 0x21
#define CONFIG_TLPM 0x23
#define PLATFORM_CONFIG_BIT 0x01
#define TLPM_CONFIG_BIT 0x02
#define IDLE_TIMEOUT_OFFSET 12
#define WAKEUP_TIMEOUT_OFFSET 8
#define IDLE_TIMEOUT_DEFAULT_VAL 1000
#define WAKEUP_TIMEOUT_DEFAULT_VAL 10

    hdev->IdleTimeout = IDLE_TIMEOUT_DEFAULT_VAL;
    hdev->WakeupTimeout = WAKEUP_TIMEOUT_DEFAULT_VAL;
    hdev->PwrMgmtEnabled = 0;

	ps_index = 2; /* CRC + PS Reset */
	if (Patch_Count)
		ps_index += Patch_Count + 1; /* Patches + Enable patch Cmd */

    for(i = ps_index; i <numCmds; i++) {

	AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Check PS ID %x\n", HciCmdList[i].Hcipacket[4]));
        /* search for Platform config and TLPM tags */
        if((HciCmdList[i].Hcipacket[4] == CONFIG_PLATFORM) &&
            (HciCmdList[i].Hcipacket[5] == 0)) {
            cFlags |= PLATFORM_CONFIG_BIT;
            bit7 = (HciCmdList[i].Hcipacket[7]) & (1<<7);
            if(bit7) {
                hdev->PwrMgmtEnabled = 1;
		AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("CONFIG PLATFORM present and Pwr Manage %x\n", hdev->PwrMgmtEnabled));
		}
        }
        else if((HciCmdList[i].Hcipacket[4] == CONFIG_TLPM) &&
                (HciCmdList[i].Hcipacket[5] == 0)) {
            cFlags |= TLPM_CONFIG_BIT;
            hdev->IdleTimeout = *((A_UINT32 *)&HciCmdList[i].Hcipacket[IDLE_TIMEOUT_OFFSET + 7]);
            hdev->WakeupTimeout = *((A_UINT16 *)&HciCmdList[i].Hcipacket[WAKEUP_TIMEOUT_OFFSET + 7]);
		AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("hdev->idletimeout %d hdev->WakeupTimeout %d",hdev->IdleTimeout, hdev->WakeupTimeout));
        }
    }


    /* Form the parameter for PSSendOps() API */
 

    /*
     * First Send the CRC packet, 
     * We have to continue with the PS operations only if the CRC packet has been replied with 
     * a Command complete event with status Error.
     */

    if(SendHCICommandWaitCommandComplete
    (hdev,
    HciCmdList[0].Hcipacket,
    HciCmdList[0].packetLen,
    &event,
    &bufferToFree) == A_OK) {
        if(ReadPSEvent(event) == A_OK) { /* Exit if the status is success */
            if(bufferToFree != NULL) {
                A_FREE(bufferToFree);
                }
#ifndef HCI_TRANSPORT_SDIO
			if(bdaddr && bdaddr[0] !='\0') {
				write_bdaddr(hdev,bdaddr,BDADDR_TYPE_STRING);
			}
#endif 
               status = 1;
               goto complete;
        }
        if(bufferToFree != NULL) {
               A_FREE(bufferToFree);
        }
    } else {
        status = 0;
        goto complete;
    }
	/* Set Patch location */ 
	if(patch_loc[0] != '\0') {
    	AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Setting Patch Location  %s\n", patch_loc));
		set_patch_ram(hdev,patch_loc,sizeof(patch_loc));
	}
    for(i = 1; i <numCmds; i++) {
    	Hci_log("PS/Patch Write -->",HciCmdList[i].Hcipacket,HciCmdList[i].packetLen);
        if(SendHCICommandWaitCommandComplete
        (hdev,
        HciCmdList[i].Hcipacket,
        HciCmdList[i].packetLen,
        &event,
        &bufferToFree) == A_OK) {
            if(ReadPSEvent(event) != A_OK) { /* Exit if the status is success */
                if(bufferToFree != NULL) {
                    A_FREE(bufferToFree);
                    }
                   status = 1;
                    goto complete;
            }
            if(bufferToFree != NULL) {
                   A_FREE(bufferToFree);
            }
        } else {
            status = 0;
            goto complete;
        }
    }
#ifdef HCI_TRANSPORT_SDIO
	if(BDADDR == FALSE)
		if(hdev->bdaddr[0] !=0x00 ||
		   hdev->bdaddr[1] !=0x00 ||
		   hdev->bdaddr[2] !=0x00 ||
		   hdev->bdaddr[3] !=0x00 ||
		   hdev->bdaddr[4] !=0x00 ||
		   hdev->bdaddr[5] !=0x00)
			write_bdaddr(hdev,hdev->bdaddr,BDADDR_TYPE_HEX);


	/* if Platform config is present and TLPM is not available
	 * write HCI command for TLPM with default timeout values */
	if(bit7 && !(cFlags & TLPM_CONFIG_BIT)) {
		A_UCHAR TLPMHciCmd[] = {0x0b, 0xfc, 0x1c, 0x01, 0x23, 0x00, 0x18, 
			0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x0a, 0x00, 0x0a, 0x00, 0xe8, 0x03,
			0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0xe8,
			0x03, 0x00, 0x00 };
		int CmdLen = sizeof(TLPMHciCmd);

		*((A_UINT32 *)&TLPMHciCmd[IDLE_TIMEOUT_OFFSET + 7]) = hdev->IdleTimeout;
		*((A_UINT16 *)&TLPMHciCmd[WAKEUP_TIMEOUT_OFFSET + 7]) = hdev->WakeupTimeout;

		if(SendHCICommandWaitCommandComplete
				(hdev,
				 TLPMHciCmd,
				 CmdLen,
				 &event,
				 &bufferToFree) == A_OK) {
			if(ReadPSEvent(event) != A_OK) { /* Exit if the status is success */
				if(bufferToFree != NULL) {
					A_FREE(bufferToFree);
				}
				status = 1;
				goto complete;
			}
			if(bufferToFree != NULL) {
				A_FREE(bufferToFree);
			}
		} else {
			status = 0;
			goto complete;
		}
	}

#ifndef HCI_TRANSPORT_SDIO

	if(bdaddr && bdaddr[0] != '\0') {
		write_bdaddr(hdev,bdaddr,BDADDR_TYPE_STRING);
	} else
#endif /* HCI_TRANSPORT_SDIO */
    /* Write BDADDR Read from OTP here */



#endif

	{
		 /* Read Contents of BDADDR file if user has not provided any option */
        snprintf(config_path,MAX_FW_PATH_LEN, "%s%s",path,BDADDR_FILE);
    	AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BDADDR File Name %s\n", config_path));
    	if(A_REQUEST_FIRMWARE(&firmware,config_path,firmwareDev) < 0) {
        	AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s: firmware file open error\n", __FUNCTION__ ));
        	goto complete;
    	}
    	if(NULL == firmware || firmware->size == 0) {
        	goto complete;
    	}
        len = (firmware->size > MAX_BDADDR_FORMAT_LENGTH)? MAX_BDADDR_FORMAT_LENGTH: firmware->size;
	memcpy(config_bdaddr, firmware->data,len);
	config_bdaddr[len] = '\0';
	write_bdaddr(hdev,config_bdaddr,BDADDR_TYPE_STRING);
       	A_RELEASE_FIRMWARE(firmware);
	}
complete:
#ifndef HCI_TRANSPORT_SDIO
    AthEnableSyncCommandOp(FALSE);    
    PSTagMode = FALSE;
    wake_up_interruptible(&PsCompleteEvent);
#endif /* HCI_TRANSPORT_SDIO */
    if(NULL != HciCmdList) {
        AthFreeCommandList(&HciCmdList,numCmds);
    }
    if(path) {
        A_FREE(path);
    }
    if(config_path) {
        A_FREE(config_path);
    }
    return status;
}
#ifndef HCI_TRANSPORT_SDIO
/*
 *  This API is used to send the HCI command to controller and return
 *  with a HCI Command Complete event.
 *  For HCI SDIO transport, this will be internally defined. 
 */
A_STATUS SendHCICommandWaitCommandComplete(AR3K_CONFIG_INFO *pConfig,
                                           A_UINT8          *pHCICommand,
                                           int              CmdLength,
                                           A_UINT8          **ppEventBuffer,
                                           A_UINT8          **ppBufferToFree)
{
    if(CmdLength == 0) {
        return A_ERROR;
    }
    Hci_log("COM Write -->",pHCICommand,CmdLength);
    PSAcked = FALSE;
    if(PSHciWritepacket(pConfig,pHCICommand,CmdLength) == 0) {
        /* If the controller is not available, return Error */
        return A_ERROR;
    }
    //add_timer(&psCmdTimer);
    wait_event_interruptible(HciEvent,(PSAcked == TRUE));
    if(NULL != HciEventpacket) {
        *ppEventBuffer = HciEventpacket;
        *ppBufferToFree = HciEventpacket;
    } else {
        /* Did not get an event from controller. return error */
        *ppBufferToFree = NULL;
        return A_ERROR;
    }

    return A_OK;
}
#endif /* HCI_TRANSPORT_SDIO */

A_STATUS ReadPSEvent(A_UCHAR* Data){
                                
    if(Data[4] == 0xFC && Data[5] == 0x00)
    {
         switch(Data[3]){
             case 0x0B:
                     return A_OK;
                 break;
                 case 0x0C:
                    /* Change Baudrate */
                        return A_OK;    
                 break;  
                 case 0x04:
                     return A_OK;
                 break;  
		case 0x1E:
			Rom_Version = Data[9];
			Rom_Version = ((Rom_Version << 8) |Data[8]);
			Rom_Version = ((Rom_Version << 8) |Data[7]);
			Rom_Version = ((Rom_Version << 8) |Data[6]);

			Build_Version = Data[13];
			Build_Version = ((Build_Version << 8) |Data[12]);
			Build_Version = ((Build_Version << 8) |Data[11]);
			Build_Version = ((Build_Version << 8) |Data[10]);
			return A_OK;
		break;

        
                }
    }                       
        
    return A_ERROR;           
}
int str2ba(unsigned char *str_bdaddr,unsigned char *bdaddr)
{
	unsigned char bdbyte[3];
	unsigned char *str_byte = str_bdaddr;
	int i,j;
	unsigned char colon_present = 0;

	if(NULL != strstr(str_bdaddr,":")) {
		colon_present = 1;
	}


	bdbyte[2] = '\0';

	for( i = 0,j = 5; i < 6; i++, j--) {
		bdbyte[0] = str_byte[0];
		bdbyte[1] = str_byte[1];
		bdaddr[j] = A_STRTOL(bdbyte,NULL,16);
		if(colon_present == 1) {
			str_byte+=3;
		} else {
			str_byte+=2;
		}
	}
	return 0; 
}
A_STATUS set_patch_ram(AR3K_CONFIG_INFO *pConfig,A_UCHAR *patch_loc,A_UINT8 len)
{
    A_UCHAR cmd[] = { 0x0B, 0xFC, 0x07, 0x0D, 0x00, 0x00, 0x00, /* Loc */0x00, 0x00, 0x00, 0x00 };
	A_UCHAR i,j;
	unsigned char loc_byte[3];
    A_UINT8 *event;
    A_UINT8 *bufferToFree = NULL;
    A_STATUS result = A_ERROR;
    A_UCHAR *loc_ptr = &cmd[7];

    if(!patch_loc)
		return result;

	loc_byte[2] = '\0';

	LoadHeader(cmd, 0x0D, 4, 0);
	for( i = 0,j = 3; i < 4; i++, j--) {
		loc_byte[0] = patch_loc[0];
		loc_byte[1] = patch_loc[1];
		loc_ptr[j] = A_STRTOL(loc_byte,NULL,16);
		patch_loc+=2;
	}
    Hci_log("Patch Ram Write -->",cmd, sizeof(cmd));
    if(A_OK == SendHCICommandWaitCommandComplete(pConfig,cmd,
	sizeof(cmd),
	&event,&bufferToFree)) {

	result = ReadPSEvent(event);

    }
    if(bufferToFree != NULL) {
        A_FREE(bufferToFree);
   }
    return result;

}
A_STATUS write_bdaddr(AR3K_CONFIG_INFO *pConfig,A_UCHAR *bdaddr,int type)
{
	A_UCHAR bdaddr_cmd[] = { 0x0B, 0xFC, 0x0A, 0x01, 0x01, 
							0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    A_UINT8 *event;
    A_UINT8 *bufferToFree = NULL;
    A_STATUS result = A_ERROR;
	int inc,outc;

	if (type == BDADDR_TYPE_STRING)
		str2ba(bdaddr,&bdaddr_cmd[7]);
	else {
		/* Bdaddr has to be sent as LAP first */
		for(inc = 5 ,outc = 7; inc >=0; inc--, outc++)
			bdaddr_cmd[outc] = bdaddr[inc];
	}

    Hci_log("BDADDR Write -->",bdaddr_cmd,sizeof(bdaddr_cmd));
    if(A_OK == SendHCICommandWaitCommandComplete(pConfig,bdaddr_cmd,
												sizeof(bdaddr_cmd),
												&event,&bufferToFree)) {

        if(event[4] == 0xFC && event[5] == 0x00){
               if(event[3] == 0x0B){
                result = A_OK;
            }
        }

    }
    if(bufferToFree != NULL) {
        A_FREE(bufferToFree);
   }
    return result;

}
A_STATUS ReadVersionInfo(AR3K_CONFIG_INFO *pConfig)
{
    A_UINT8   hciCommand[] =  {0x1E,0xfc,0x00};
    A_UINT8 *event;
    A_UINT8 *bufferToFree = NULL;
    A_STATUS result = A_ERROR;
    Hci_log("Read Version -->",hciCommand,sizeof(hciCommand));
    if(A_OK == SendHCICommandWaitCommandComplete(pConfig,hciCommand,sizeof(hciCommand),&event,&bufferToFree)) {
	result = ReadPSEvent(event);

    }
    if(bufferToFree != NULL) {
        A_FREE(bufferToFree);
   }
    return result;
}
A_STATUS getDeviceType(AR3K_CONFIG_INFO *pConfig, A_UINT32 * code)
{
    A_UINT8   hciCommand[] =  {0x05,0xfc,0x05,0x00,0x00,0x00,0x00,0x04};
    A_UINT8 *event;
    A_UINT8 *bufferToFree = NULL;
    A_UINT32 reg;
    A_STATUS result = A_ERROR;
    *code = 0;
    hciCommand[3] = (A_UINT8)(FPGA_REGISTER & 0xFF);
    hciCommand[4] = (A_UINT8)((FPGA_REGISTER >> 8) & 0xFF);
    hciCommand[5] = (A_UINT8)((FPGA_REGISTER >> 16) & 0xFF);
    hciCommand[6] = (A_UINT8)((FPGA_REGISTER >> 24) & 0xFF); 
    if(A_OK == SendHCICommandWaitCommandComplete(pConfig,hciCommand,sizeof(hciCommand),&event,&bufferToFree)) {

        if(event[4] == 0xFC && event[5] == 0x00){
               switch(event[3]){
                case 0x05:
                reg = event[9];
                reg = ((reg << 8) |event[8]);
                reg = ((reg << 8) |event[7]);
                reg = ((reg << 8) |event[6]);
                *code = reg;
                result = A_OK;

                break;
                case 0x06:
                    //Sleep(500);
                break;
            }
        }

    }
    if(bufferToFree != NULL) {
        A_FREE(bufferToFree);
   }
    return result;
}


