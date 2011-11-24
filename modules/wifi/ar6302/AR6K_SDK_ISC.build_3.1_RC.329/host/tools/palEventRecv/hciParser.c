#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdarg.h>

#include <a_config.h>
#include <a_osapi.h>
#include <athdefs.h>
#include <a_types.h>
#include "athdrv_linux.h"
#include "a_hci.h"
#include "hciParser.h"

int countLines(FILE *fpt)
{
    int lines =0;
    while(!feof(fpt)){
        if(fgetc(fpt)== '\n'){
            lines++;
        }
    }
    return lines;
}
int readline(FILE *fpt,char *buff)
{
    int inc=0,fpos = 0;
    char c;
    do{
        c = *(buff + inc)= fgetc(fpt);
        //hciLog("%c",*(char *)(readline +inc));
        inc++;
    }while((c != '\n'));

    fpos = ftell(fpt);
    return inc;
}
char xtod(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='A' && c<='F') return c-'A'+10;
    if (c>='a' && c<='f') return c-'a'+10;
    return c=0;        // not Hex digit
}

int strTochar(unsigned char *str,unsigned char *cBuf,int bufSize)
{
    int i = 0;
    int strSize = 0;
    unsigned char hexVal = 0;
    for(i = 0 ; i < bufSize; i++)
    {
        if(str[i] == 'x')
        {
            hexVal = xtod(str[++i]);
            hexVal = hexVal << 4;
            hexVal = hexVal | xtod(str[++i]);
            cBuf[strSize] = hexVal;
            strSize++;
        }
    }
    return strSize;
}

char *errorCodes[] = {
    "Success",
    "Unknown HCI Command",
    "Unknown Connection Identifier",
    "Hardware Failure",
    "Page Timeout",
    "Authentication Failure",
    "PIN or Key Missing",
    "Memory Capacity Exceeded",
    "Connection Timeout",
    "Connection Limit Exceeded",
    "Synchronous Connection Limit To A Device Exceeded",
    "ACL Connection Already Exists",
    "Command Disallowed",
    "Connection Rejected due to Limited Resources",
    "Connection Rejected Due To Security Reasons ",
    "Connection Rejected due to Unacceptable BD_ADDR", //0x0F
    "Connection Accept Timeout Exceeded",
    "Unsupported Feature or Parameter Value",
    "Invalid HCI Command Parameters",
    "Remote User Terminated Connection",
    "Remote Device Terminated Connection due to Low Resources",
    "Remote Device Terminated Connection due to Power Off",
    "Connection Terminated By Local Host",
    "Repeated Attempts",
    "Pairing Not Allowed",
    "Unknown LMP PDU",
    "Unsupported Remote Feature / Unsupported LMP Feature",
    "SCO Offset Rejected",
    "SCO Interval Rejected",
    "SCO Air Mode Rejected",
    "Invalid LMP Parameters",
    "Unspecified Error",                                //0x1F
    "Unsupported LMP Parameter Value",
    "Role Change Not Allowed",
    "LMP Response Timeout",
    "LMP Error Transaction Collision",
    "LMP PDU Not Allowed",
    "Encryption Mode Not Acceptable",
    "Link Key Can Not be Changed",
    "Requested QoS Not Supported",
    "Instant Passed",
    "Pairing With Unit Key Not Supported",
    "Different Transaction Collision",
    "Reserved",
    "QoS Unacceptable Parameter",
    "QoS Rejected",
    "Channel Classification Not Supported",
    "Insufficient Security",                            //0x2f
    "Parameter Out Of Mandatory Range",
    "Reserved",
    "Role Switch Pending",
    "Reserved",
    "Reserved Slot Violation ",
    "Role Switch Failed",
    "Extended Inquiry Response Too Large",
    "Secure Simple Pairing Not Supported By Host",
    "Host Busy - Pairing",
    "Connection Rejected due to No Suitable Channel Found",
};


char *serviceType[] =
{
    "No Traffic",
    "Best effort (Default)",
    "Guaranteed",
    "Reserved"
};

FILE *opFile;
int eventLogLevel = 0;
int eventLogFile  = 0;

void hciLog(char *format,...)
{
    char     buffer[2000]; /* Output Buffer */
    int      len;
    va_list  args;

    va_start(args, format);
    len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if(eventLogFile == 1)
    {
        fwrite(buffer, sizeof(char), strlen(buffer), opFile);
        fflush(opFile);
    }
    if(eventLogLevel == 1)
    {
        printf("%s",buffer);
    }
}


void printBuffer(A_UINT8 *buf,A_UINT16 length)
{
    int i = 0;
    for(i = 0; i < length; i++)
    {
        hciLog("0x%02x ",buf[i]);
        if(!((i+1) % 16))
        {
            hciLog("\n");
        }
    }
    hciLog("\n");
}



void cmdParser(A_UINT8 *cmd,A_UINT16 len)
{
    HCI_CMD_PHY_LINK            *cHciPhyLink;
    HCI_CMD_DISCONNECT_PHY_LINK *cHciDiscntPhyLink;
    HCI_CMD_CREATE_LOGICAL_LINK *cHciCreateLogLink;
    HCI_CMD_DISCONNECT_LOGICAL_LINK *cHciDiscntLogLink;
    HCI_CMD_LOGICAL_LINK_CANCEL *cHciLogLinkCancel;
    HCI_CMD_FLOW_SPEC_MODIFY    *cHciFlowSpecModify;
    HCI_CMD_SET_EVT_MASK        *cHciSetEventMask;
//    HCI_CMD_FLUSH               *cHciFlush;
    HCI_CMD_READ_LINK_SUPERVISION_TIMEOUT   *cHciLinkReadSupervisTimeout;
    HCI_CMD_WRITE_LINK_SUPERVISION_TIMEOUT  *cHciLinkWriteSupervisTimeout;
    HCI_CMD_WRITE_LOCATION_DATA *cHciWriteLocationData;
    HCI_CMD_WRITE_FLOW_CONTROL  *cHciWriteFlowControl;
    HCI_CMD_SHORT_RANGE_MODE    *cHciShortRangeMode;
    HCI_CMD_READ_LINK_QUAL      *cHciReadLinkQuality;
    HCI_CMD_READ_LOCAL_AMP_ASSOC    *cHciReadLclAmpAssoc;
    HCI_CMD_WRITE_REM_AMP_ASSOC     *cHciWriteRemAmpAssoc;
    HCI_CMD_PKT *hciCmd = (HCI_CMD_PKT *)cmd;
    A_UINT8     *ampAssocFrag;
    A_UINT16    ampLen = 0;
    A_UINT32    length = 0;
    A_UINT32    lenRead= 0;
    A_UINT16    temp16 = 0;
    A_UINT32    temp32 = 0;

    A_UINT16    connAcpTimeout = 0;

    hciLog("________________________________________________________________________\n");
    hciLog("RAW PACKET\n");
    printBuffer(cmd,len);

    hciLog("HCI Command Parser\n");
    hciLog("OGF                             -%#x\n",HCI_CMD_GET_OGF(hciCmd->opcode));
    hciLog("OCF                             -%#x\n",HCI_CMD_GET_OCF(hciCmd->opcode));
    switch(HCI_CMD_GET_OGF(hciCmd->opcode))
    {
        case OGF_LINK_CONTROL:
            {
                switch(HCI_CMD_GET_OCF(hciCmd->opcode))
                {
                    /*Link Control Commands */
                    case OCF_HCI_Create_Physical_Link:
                        cHciPhyLink     = (HCI_CMD_PHY_LINK *)hciCmd;
                        hciLog("OCF_HCI_Create_Physical_Link\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciPhyLink->opcode);
                        hciLog("Param Length                    -%#x \n",cHciPhyLink->param_length);
                        hciLog("Phy Link Handle                 -%#x \n",cHciPhyLink->phy_link_hdl);
                        hciLog("Link Hey Length                 -%#x \n",cHciPhyLink->link_key_len);
                        if(cHciPhyLink->link_key_type == 0x03)
                        {
                            hciLog("Link Hey Type                   -Debug combination keys\n");
                        }
                        else if(cHciPhyLink->link_key_type == 0x04)
                        {
                            hciLog("Link Hey Type                   -Unauthenticated combination key\n");
                        }
                        else if(cHciPhyLink->link_key_type == 0x05)
                        {
                            hciLog("Link Hey Type                   -Authenticated combination key\n");
                        }
                        else
                        {
                            hciLog("Link Hey Type                   -Unknown(%#x) \n",cHciPhyLink->link_key_type);
                        }
                        hciLog("Link Key:\n");
                        printBuffer(cHciPhyLink->link_key,LINK_KEY_LEN);
                        hciLog("\n------------------------------------\n");
                        break;
                    case OCF_HCI_Accept_Physical_Link_Req:
                        cHciPhyLink     = (HCI_CMD_PHY_LINK *)hciCmd;
                        hciLog("OCF_HCI_Accept_Physical_Link_Req\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciPhyLink->opcode);
                        hciLog("Param Length                    -%#x \n",cHciPhyLink->param_length);
                        hciLog("Phy Link Handle                 -%#x \n",cHciPhyLink->phy_link_hdl);
                        hciLog("Link Hey Length                 -%#x \n",cHciPhyLink->link_key_len);
                        if(cHciPhyLink->link_key_type == 0x03)
                        {
                            hciLog("Link Hey Type                   -Debug combination keys\n");
                        }
                        else if(cHciPhyLink->link_key_type == 0x04)
                        {
                            hciLog("Link Hey Type                   -Unauthenticated combination key\n");
                        }
                        else if(cHciPhyLink->link_key_type == 0x05)
                        {
                            hciLog("Link Hey Type                   -Authenticated combination key\n");
                        }
                        else
                        {
                            hciLog("Link Hey Type                   -Unknown(%#x) \n",cHciPhyLink->link_key_type);
                        }
                        hciLog("Link Key:\n");
                        printBuffer(cHciPhyLink->link_key,LINK_KEY_LEN);
                        hciLog("\n------------------------------------\n");
                        break;
                    case OCF_HCI_Disconnect_Physical_Link:
                        cHciDiscntPhyLink   = (HCI_CMD_DISCONNECT_PHY_LINK *)hciCmd;
                        hciLog("OCF_HCI_Disconnect_Physical_Link\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciDiscntPhyLink->opcode);
                        hciLog("Param Length                    -%#x \n",cHciDiscntPhyLink->param_length);
                        hciLog("Phy Link Handle                 -%#x \n",cHciDiscntPhyLink->phy_link_hdl);
#if 0
                        if(cHciDiscntPhyLink->reason <= 0x39)
                            hciLog("Reason                          -%s   \n",errorCodes[cHciDiscntPhyLink->reason]);
                        else
                            hciLog("Reason                          -Unknown Error code\n");
#endif
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Create_Logical_Link:
                        cHciCreateLogLink   = (HCI_CMD_CREATE_LOGICAL_LINK *)hciCmd;
                        hciLog("OCF_HCI_Create_Logical_Link\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciCreateLogLink->opcode);
                        hciLog("Param Length                    -%#x \n",cHciCreateLogLink->param_length);
                        hciLog("Logical Link Handle             -%#x \n",cHciCreateLogLink->phy_link_hdl);
                        hciLog("Tx Flow Spec\n");
                        hciLog("ID                              -%#x \n",cHciCreateLogLink->tx_flow_spec.id);
                        hciLog("Service Type                    -%#x \n",cHciCreateLogLink->tx_flow_spec.service_type);
                        hciLog("Max SDU Size                    -%#x Bytes\n",cHciCreateLogLink->tx_flow_spec.max_sdu);
                        hciLog("SDU Inter-arrival Time          -%#x microSeconds\n",cHciCreateLogLink->tx_flow_spec.sdu_inter_arrival_time);
                        hciLog("Access Latency                  -%#x microSeconds\n",cHciCreateLogLink->tx_flow_spec.access_latency);
                        hciLog("Flush Timeout                   -%#x microSeconds\n",cHciCreateLogLink->tx_flow_spec.flush_timeout);
                        hciLog("Rx Flow Spec\n");
                        hciLog("ID                              -%#x \n",cHciCreateLogLink->rx_flow_spec.id);
                        hciLog("Service Type                    -%#x \n",cHciCreateLogLink->rx_flow_spec.service_type);
                        hciLog("Max SDU Size                    -%#x Bytes\n",cHciCreateLogLink->rx_flow_spec.max_sdu);
                        hciLog("SDU Inter-arrival Time          -%#x microSeconds\n",cHciCreateLogLink->rx_flow_spec.sdu_inter_arrival_time);
                        hciLog("Access Latency                  -%#x microSeconds\n",cHciCreateLogLink->rx_flow_spec.access_latency);
                        hciLog("Flush Timeout                   -%#x microSeconds\n",cHciCreateLogLink->rx_flow_spec.flush_timeout);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Accept_Logical_Link:
                        cHciCreateLogLink   = (HCI_CMD_CREATE_LOGICAL_LINK *)hciCmd;
                        hciLog("OCF_HCI_Accept_Logical_Link\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciCreateLogLink->opcode);
                        hciLog("Param Length                    -%#x \n",cHciCreateLogLink->param_length);
                        hciLog("Phy Link Handle                 -%#x \n",cHciCreateLogLink->phy_link_hdl);
                        hciLog("Tx Flow Spec\n");
                        hciLog("ID                              -%#x \n",cHciCreateLogLink->tx_flow_spec.id);
                        hciLog("Service Type                    -%#x \n",cHciCreateLogLink->tx_flow_spec.service_type);
                        hciLog("Max SDU Size                    -%#x Bytes\n",cHciCreateLogLink->tx_flow_spec.max_sdu);
                        hciLog("SDU Inter-arrival Time          -%#x microSeconds\n",cHciCreateLogLink->tx_flow_spec.sdu_inter_arrival_time);
                        hciLog("Access Latency                  -%#x microSeconds\n",cHciCreateLogLink->tx_flow_spec.access_latency);
                        hciLog("Flush Timeout                   -%#x microSeconds\n",cHciCreateLogLink->tx_flow_spec.flush_timeout);
                        hciLog("Rx Flow Spec\n");
                        hciLog("ID                              -%#x \n",cHciCreateLogLink->rx_flow_spec.id);
                        hciLog("Service Type                    -%#x \n",cHciCreateLogLink->rx_flow_spec.service_type);
                        hciLog("Max SDU Size                    -%#x Bytes\n",cHciCreateLogLink->rx_flow_spec.max_sdu);
                        hciLog("SDU Inter-arrival Time          -%#x microSeconds\n",cHciCreateLogLink->rx_flow_spec.sdu_inter_arrival_time);
                        hciLog("Access Latency                  -%#x microSeconds\n",cHciCreateLogLink->rx_flow_spec.access_latency);
                        hciLog("Flush Timeout                   -%#x microSeconds\n",cHciCreateLogLink->rx_flow_spec.flush_timeout);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Disconnect_Logical_Link:
                        cHciDiscntLogLink   = (HCI_CMD_DISCONNECT_LOGICAL_LINK *)hciCmd;
                        hciLog("OCF_HCI_Disconnect_Logical_Link\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciDiscntLogLink->opcode);
                        hciLog("Param Length                    -%#x \n",cHciDiscntLogLink->param_length);
                        hciLog("Logical Link hdl                -%#x \n",cHciDiscntLogLink->logical_link_hdl);
#if 0
                        if(cHciDiscntLogLink->reason <= 0x39)
                            hciLog("Reason                          -%s   \n",errorCodes[cHciDiscntLogLink->reason]);
                        else
                            hciLog("Reason                          -Unknown Error code\n");
#endif
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Logical_Link_Cancel:
                        cHciLogLinkCancel   = (HCI_CMD_LOGICAL_LINK_CANCEL *)hciCmd;
                        hciLog("OCF_HCI_Logical_Link_Cancel\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciLogLinkCancel->opcode);
                        hciLog("Param Length                    -%#x \n",cHciLogLinkCancel->param_length);
                        hciLog("Phy Link Handle                 -%#x \n",cHciLogLinkCancel->phy_link_hdl);
                        hciLog("Tx Flow Spec ID                 -%#x \n",cHciLogLinkCancel->tx_flow_spec_id);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Flow_Spec_Modify:
                        cHciFlowSpecModify  = (HCI_CMD_FLOW_SPEC_MODIFY *)hciCmd;
                        hciLog("OCF_HCI_Flow_Spec_Modify\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciFlowSpecModify->opcode);
                        hciLog("Param Length                    -%#x \n",cHciFlowSpecModify->param_length);
                        hciLog("Handle                          -%#x \n",cHciFlowSpecModify->hdl);
                        hciLog("Tx Flow Spec\n");
                        hciLog("ID                              -%#x \n",cHciFlowSpecModify->tx_flow_spec.id);
                        hciLog("Service Type                    -%#x \n",cHciFlowSpecModify->tx_flow_spec.service_type);
                        hciLog("Max SDU Size                    -%#x Bytes\n",cHciFlowSpecModify->tx_flow_spec.max_sdu);
                        hciLog("SDU Inter-arrival Time          -%#x microSeconds\n",cHciFlowSpecModify->tx_flow_spec.sdu_inter_arrival_time);
                        hciLog("Access Latency                  -%#x microSeconds\n",cHciFlowSpecModify->tx_flow_spec.access_latency);
                        hciLog("Flush Timeout                   -%#x microSeconds\n",cHciFlowSpecModify->tx_flow_spec.flush_timeout);
                        hciLog("Rx Flow Spec\n");
                        hciLog("ID                              -%#x \n",cHciFlowSpecModify->rx_flow_spec.id);
                        hciLog("Service Type                    -%#x \n",cHciFlowSpecModify->rx_flow_spec.service_type);
                        hciLog("Max SDU Size                    -%#x Bytes\n",cHciFlowSpecModify->rx_flow_spec.max_sdu);
                        hciLog("SDU Inter-arrival Time          -%#x microSeconds\n",cHciFlowSpecModify->rx_flow_spec.sdu_inter_arrival_time);
                        hciLog("Access Latency                  -%#x microSeconds\n",cHciFlowSpecModify->rx_flow_spec.access_latency);
                        hciLog("Flush Timeout                   -%#x microSeconds\n",cHciFlowSpecModify->rx_flow_spec.flush_timeout);
                        hciLog("------------------------------------\n");
                        break;
                    default:
                        hciLog("Unknown Link Control Command\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                }
            }
            break;
        case OGF_LINK_POLICY:
            {
                switch(HCI_CMD_GET_OCF(hciCmd->opcode))
                {
                    /*===== Link Policy Commands Opcode====================*/
                    case OCF_HCI_Set_Event_Mask:
                        cHciSetEventMask    = (HCI_CMD_SET_EVT_MASK *)hciCmd;
                        hciLog("OCF_HCI_Set_Event_Mask\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciSetEventMask->opcode);
                        hciLog("Param Length                    -%#x \n",cHciSetEventMask->param_length);
                        hciLog("Mask                            -0x%08x%08x \n",(unsigned int)(cHciSetEventMask->mask >> 32),(unsigned int)(cHciSetEventMask->mask));
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Reset:
                        hciLog("OCF_HCI_Reset\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                        break;
#if 0
                    case OCF_HCI_Flush:
                        cHciFlush           = (HCI_CMD_FLUSH *)cHciFlush;
                        hciLog("OCF_HCI_Flush\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciFlush->opcode);
                        hciLog("Param Length                    -%#x \n",cHciFlush->param_length);
                        hciLog("Handle                          -%#x \n",cHciFlush->hdl);
                        hciLog("------------------------------------\n");
                        break;
#endif
                    case OCF_HCI_Read_Conn_Accept_Timeout:
                        hciLog("OCF_HCI_Read_Conn_Accept_Timeout\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Write_Conn_Accept_Timeout:
                        hciLog("OCF_HCI_Write_Conn_Accept_Timeout\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        memcpy(&connAcpTimeout,hciCmd->params,sizeof(A_UINT16));
                        hciLog("Conn_Accept_Timeout             -%#x \n",connAcpTimeout);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Read_Logical_Link_Accept_Timeout:
                        hciLog("OCF_HCI_Write_Conn_Accept_Timeout\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Write_Logical_Link_Accept_Timeout:
                        hciLog("OCF_HCI_Write_Logical_Link_Accept_Timeout\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        memcpy(&connAcpTimeout,hciCmd->params,sizeof(A_UINT16));
                        hciLog("Conn_Accept_Timeout             -%#x \n",connAcpTimeout);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Read_Link_Supervision_Timeout:
                        cHciLinkReadSupervisTimeout = (HCI_CMD_READ_LINK_SUPERVISION_TIMEOUT *)hciCmd;
                        hciLog("OCF_HCI_Read_Link_Supervision_Timeout\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciLinkReadSupervisTimeout->opcode);
                        hciLog("Param Length                    -%#x \n",cHciLinkReadSupervisTimeout->param_length);
                        hciLog("Handle                          -%#x \n",cHciLinkReadSupervisTimeout->hdl);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Write_Link_Supervision_Timeout:
                        cHciLinkWriteSupervisTimeout = (HCI_CMD_WRITE_LINK_SUPERVISION_TIMEOUT *)hciCmd;
                        hciLog("OCF_HCI_Write_Link_Supervision_Timeout\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciLinkWriteSupervisTimeout->opcode);
                        hciLog("Param Length                    -%#x \n",cHciLinkWriteSupervisTimeout->param_length);
                        hciLog("TimeOut                         -%d \n",cHciLinkWriteSupervisTimeout->timeout);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Set_Event_Mask_Page_2:
                        cHciSetEventMask    = (HCI_CMD_SET_EVT_MASK *)hciCmd;
                        hciLog("OCF_HCI_Set_Event_Mask_Page_2\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciSetEventMask->opcode);
                        hciLog("Param Length                    -%#x \n",cHciSetEventMask->param_length);
                        hciLog("Mask                            -0x%08x%08x \n",(unsigned int)(cHciSetEventMask->mask >> 32),(unsigned int)(cHciSetEventMask->mask));
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Read_Location_Data:
                        hciLog("OCF_HCI_Read_Location_Data\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Write_Location_Data: //reg domain options & reg options ??
                        hciLog("OCF_HCI_Write_Location_Data\n");
                        cHciWriteLocationData   = (HCI_CMD_WRITE_LOCATION_DATA *)hciCmd;
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciWriteLocationData->opcode);
                        hciLog("Param Length                    -%#x \n",cHciWriteLocationData->param_length);
                        hciLog("Reg Domain Aware                -%#x \n",cHciWriteLocationData->cfg.reg_domain_aware);
                        hciLog("Reg Domain                      -0x%02x 0x%02x 0x%02x \n",cHciWriteLocationData->cfg.reg_domain[0],cHciWriteLocationData->cfg.reg_domain[1],cHciWriteLocationData->cfg.reg_domain[2]);
                        hciLog("Reg Domain Options              -%#x \n",cHciWriteLocationData->cfg.reg_options);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Read_Flow_Control_Mode:
                        hciLog("OCF_HCI_Read_Flow_Control_Mode\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Write_Flow_Control_Mode:
                        cHciWriteFlowControl    = (HCI_CMD_WRITE_FLOW_CONTROL *)hciCmd;
                        hciLog("OCF_HCI_Write_Flow_Control_Mode\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciWriteFlowControl->opcode);
                        hciLog("Param Length                    -%#x \n",cHciWriteFlowControl->param_length);
                        hciLog("Mode                            -%#x \n",cHciWriteFlowControl->mode);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Write_BE_Flush_Timeout:
                        hciLog("OCF_HCI_Write_BE_Flush_Timeout\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        memcpy(&temp16,&hciCmd->params[0],sizeof(A_UINT16));
                        hciLog("Logical Link Handle             -%#x \n",temp16);
                        memcpy(&temp32,(&hciCmd->params[0] + sizeof(A_UINT16)),sizeof(A_UINT32));
                        hciLog("Best Effort Flush Timeout       -%#x \n",temp32);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Read_BE_Flush_Timeout:
                        hciLog("OCF_HCI_Read_BE_Flush_Timeout\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        memcpy(&temp16,&hciCmd->params[0],sizeof(A_UINT16));
                        hciLog("Logical Link Handle             -%#x \n",temp16);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Short_Range_Mode:
                        cHciShortRangeMode  = (HCI_CMD_SHORT_RANGE_MODE *)hciCmd;
                        hciLog("OCF_HCI_Read_BE_Flush_Timeout\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciShortRangeMode->opcode);
                        hciLog("Param Length                    -%#x \n",cHciShortRangeMode->param_length);
                        hciLog("Phy Link Handle                 -%#x \n",cHciShortRangeMode->phy_link_hdl);
                        hciLog("Mode                            -%#x \n",cHciShortRangeMode->mode);
                        hciLog("------------------------------------\n");
                        break;
                    default:
                        hciLog("Unknown Link Policy\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                }
            }
            break;
        case OGF_INFO_PARAMS:
            {
                /*======== Info Commands Opcode========================*/
                switch(HCI_CMD_GET_OCF(hciCmd->opcode))
                {
                    case OCF_HCI_Read_Local_Ver_Info:
                        hciLog("OCF_HCI_Read_Local_Ver_Info\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Read_Local_Supported_Cmds:
                        hciLog("OCF_HCI_Read_Local_Supported_Cmds\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Read_Data_Block_Size:
                        hciLog("OCF_HCI_Read_Data_Block_Size\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                        break;
                    default:
                        hciLog("Unknown Info Command\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                        break;
                }
            }
            break;
        case OGF_STATUS:
            {
                switch(HCI_CMD_GET_OCF(hciCmd->opcode))
                {
                    /*======== Status Commands Opcode======================*/
                    case OCF_HCI_Read_Link_Quality:
                        cHciReadLinkQuality = (HCI_CMD_READ_LINK_QUAL *)hciCmd;
                        hciLog("OCF_HCI_Read_Link_Quality\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciReadLinkQuality->opcode);
                        hciLog("Param Length                    -%#x \n",cHciReadLinkQuality->param_length);
                        hciLog("Handle                          -%#x \n",cHciReadLinkQuality->hdl);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Read_RSSI:
                        hciLog("OCF_HCI_Read_RSSI\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        memcpy(&temp16,&hciCmd->params[0],sizeof(A_UINT16));
                        hciLog("Link Handle                     -%#x \n",temp16);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Read_Local_AMP_Info:
                        hciLog("OCF_HCI_Read_Local_AMP_Info\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Read_Local_AMP_ASSOC:
                        cHciReadLclAmpAssoc = (HCI_CMD_READ_LOCAL_AMP_ASSOC *)hciCmd;
                        hciLog("OCF_HCI_Read_Local_AMP_ASSOC\n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciReadLclAmpAssoc->opcode);
                        hciLog("Param Length                    -%#x \n",cHciReadLclAmpAssoc->param_length);
                        hciLog("Phy Link Handle                 -%#x \n",cHciReadLclAmpAssoc->phy_link_hdl);
                        hciLog("Len so far                      -%#x \n",cHciReadLclAmpAssoc->len_so_far);
                        hciLog("Max Remote Amp Assoc Length     -%#x \n",cHciReadLclAmpAssoc->max_rem_amp_assoc_len);
                        hciLog("------------------------------------\n");
                        break;
                    case OCF_HCI_Write_Remote_AMP_ASSOC:
                        cHciWriteRemAmpAssoc = (HCI_CMD_WRITE_REM_AMP_ASSOC *)hciCmd;
                        hciLog("OCF_HCI_Write_Remote_AMP_ASSOC\n");
                        hciLog("\n------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",cHciWriteRemAmpAssoc->opcode);
                        hciLog("Param Length                    -%#x \n",cHciWriteRemAmpAssoc->param_length);
                        hciLog("Phy Link Handle                 -%#x \n",cHciWriteRemAmpAssoc->phy_link_hdl);
                        hciLog("Len So Far                      -%#x \n",cHciWriteRemAmpAssoc->len_so_far);
                        hciLog("Amp Assoc Remaining Length      -%#x \n",cHciWriteRemAmpAssoc->amp_assoc_remaining_len);
                        length          = cHciWriteRemAmpAssoc->amp_assoc_remaining_len;
                        lenRead         = 0;
                        while(length > 0)
                        {
                            if(length > 248)
                            {
                                hciLog("Length is greater than 248. Invalid\n");
                                break;
                            }
                            ampAssocFrag    = cHciWriteRemAmpAssoc->amp_assoc_frag  + lenRead;
                            switch(ampAssocFrag[0])
                            {
                                case 0x01:
                                    hciLog("MAC Address\n");
                                    hciLog("_____________________\n");
                                    hciLog("Type Id                         -%#x\n",ampAssocFrag[0]);
                                    memcpy(&ampLen,&ampAssocFrag[1],sizeof(A_UINT16));
                                    hciLog("Length                          -%#x\n",ampLen);
                                    hciLog("Mac Address                     -%02x:%02x:%02x:%02x:%02x:%02x\n",ampAssocFrag[3],ampAssocFrag[4],ampAssocFrag[5],ampAssocFrag[6],ampAssocFrag[7],ampAssocFrag[8]);
                                    break;
                                case 0x02:
                                    hciLog("Prefered Channel List\n");
                                    hciLog("_____________________\n");
                                    hciLog("Type Id                         -%#x\n",ampAssocFrag[0]);
                                    memcpy(&ampLen,&ampAssocFrag[1],sizeof(A_UINT16));
                                    hciLog("Length                          -%#x\n",ampLen);
                                    hciLog("Channel List                    -");
                                    printBuffer(&ampAssocFrag[3],ampLen);
                                    hciLog("\n");
                                    break;
                                case 0x03:
                                    hciLog("Connected Channel\n");
                                    hciLog("_____________________\n");
                                    hciLog("Type Id                         -%#x\n",ampAssocFrag[0]);
                                    memcpy(&ampLen,&ampAssocFrag[1],sizeof(A_UINT16));
                                    hciLog("Length                          -%#x\n",ampLen);
                                    hciLog("Connected Channel list          -");
                                    printBuffer(&ampAssocFrag[3],ampLen);
                                    hciLog("\n");
                                    break;
                                case 0x04:
                                    hciLog("802.11 PAL Capabilities list\n");
                                    hciLog("_____________________\n");
                                    hciLog("Type Id                         -%#x\n",ampAssocFrag[0]);
                                    memcpy(&ampLen,&ampAssocFrag[1],sizeof(A_UINT16));
                                    hciLog("Length                          -%#x\n",ampLen);
                                    hciLog("802.11 PAL Capabilities         -");
                                    printBuffer(&ampAssocFrag[3],ampLen);
                                    hciLog("\n");
                                    break;
                                case 0x05:
                                    hciLog("802.11 PAL version\n");
                                    hciLog("_____________________\n");
                                    hciLog("Type Id                         -%#x\n",ampAssocFrag[0]);
                                    memcpy(&ampLen,&ampAssocFrag[1],sizeof(A_UINT16));
                                    hciLog("Length                          -%#x\n",ampLen);
                                    hciLog("802.11 PAL Version              -");
                                    printBuffer(&ampAssocFrag[3],ampLen);
                                    hciLog("\n");
                                    break;
                                default:
                                    hciLog("Unknown data\n");
                                    hciLog("_____________________\n");
                                    hciLog("Type Id                         -%#x\n",ampAssocFrag[0]);
                                    memcpy(&ampLen,&ampAssocFrag[1],sizeof(A_UINT16));
                                    hciLog("Length                          -%#x\n",ampLen);
                                    printBuffer(&ampAssocFrag[3],ampLen);
                                    hciLog("\n");

                            }
                            lenRead += sizeof(A_UINT8) + sizeof(A_UINT16) + ampLen;
                            length  = cHciWriteRemAmpAssoc->amp_assoc_remaining_len - lenRead;
                        }
                        hciLog("------------------------------------\n");

                        break;
                    default:
                        hciLog("Unknown Status Command \n");
                        hciLog("------------------------------------\n");
                        hciLog("Opcode                          -%#x \n",hciCmd->opcode);
                        hciLog("Param Length                    -%#x \n",hciCmd->param_length);
                        hciLog("------------------------------------\n");
                }
            }
            break;
        default:
            hciLog("Unknown Command\n");
            hciLog("------------------------------------\n");
            hciLog("Opcode                          -%#x \n",hciCmd->opcode);
            hciLog("Param Length                    -%#x \n",hciCmd->param_length);
            hciLog("------------------------------------\n");
    }
    hciLog("________________________________________________________________________\n");
}
void palData(A_UINT8 *data,A_UINT32 len)
{
    hciLog("ACL Data Received \n");
    hciLog("RAW PACKET\n");
    hciLog("____________________________________________________________________________________\n");

    printBuffer(data,len);
    hciLog("____________________________________________________________________________________\n");

}


void cmdCompleteReturnParams(A_UINT16 opcode,A_UINT8 *params)
{
    A_UINT8     temp8     =0;
    A_UINT16    temp16    =0;
    A_UINT32    temp32    =0;

    hciLog("Parameters Returned - \n");
    hciLog("______________________\n");

    hciLog("OGF                             -%#x\n",HCI_CMD_GET_OGF(opcode));
    hciLog("OCF                             -%#x\n",HCI_CMD_GET_OCF(opcode));

    if(params[0] <= 0x39)
        hciLog("Status                          -%s\n",errorCodes[params[0]]);
    else
        hciLog("Status                          -Unknown Error code(%#x)\n",params[0]);
    switch(HCI_CMD_GET_OGF(opcode))
    {
        case OGF_LINK_CONTROL:
            {
                hciLog("OGF Link Control\n");
                switch(HCI_CMD_GET_OCF(opcode))
                {
                    /*Link Control Commands */
                    case OCF_HCI_Logical_Link_Cancel:
                        hciLog("Phy Link Handle                 -%#x\n",params[1]);
                        hciLog("Tx Spec Flow ID                 -%#x\n",params[2]);
                        break;
                    default:
                        hciLog("Unknown Link Control Command. Returned Params (%#x)\n",HCI_CMD_GET_OCF(opcode));

                }
            }
            break;
        case OGF_LINK_POLICY:
            {
                hciLog("OGF Link Policy\n");
                switch(HCI_CMD_GET_OCF(opcode))
                {
                    /*===== Link Policy Commands Opcode====================*/
                    case OCF_HCI_Set_Event_Mask:
                    case OCF_HCI_Reset:
                    case OCF_HCI_Write_Conn_Accept_Timeout:
                    case OCF_HCI_Write_Logical_Link_Accept_Timeout:
                    case OCF_HCI_Read_Location_Data:   //Check for param length
                    case OCF_HCI_Write_Location_Data:
                    case OCF_HCI_Set_Event_Mask_Page_2:
                    case OCF_HCI_Write_Flow_Control_Mode:
                    case OCF_HCI_Write_BE_Flush_Timeout:
                        break;
                    case OCF_HCI_Read_Conn_Accept_Timeout:
                        memcpy(&temp16,&params[1],sizeof(A_UINT16));
                        hciLog("Connection Accept Timeout       -%#x\n",temp16);
                        break;
                    case OCF_HCI_Read_Logical_Link_Accept_Timeout:
                        memcpy(&temp16,&params[1],sizeof(A_UINT16));
                        hciLog("Logical Link accept Timeout     -%#x\n",temp16);
                        break;
                    case OCF_HCI_Read_Link_Supervision_Timeout:
                        memcpy(&temp16,&params[1],sizeof(A_UINT16));
                        hciLog("Handle                          -%#x\n",temp16);
                        memcpy(&temp16,&params[3],sizeof(A_UINT16));
                        hciLog("Link Supervision Timeout        -%#x\n",temp16);
                        break;
                    case OCF_HCI_Write_Link_Supervision_Timeout:
                        memcpy(&temp16,&params[1],sizeof(A_UINT16));
                        hciLog("Handle                          -%#x\n",temp16);
                        break;
                    case OCF_HCI_Read_Flow_Control_Mode:
                        hciLog("Flow Control Mode               -%#x\n",params[1]);
                        break;
                    case OCF_HCI_Read_BE_Flush_Timeout:
                        memcpy(&temp32,&params[1],sizeof(A_UINT32));
                        hciLog("Best Effort Flush Timeout       -%#x\n",temp32);
                        break;

                    default:
                        hciLog("Unknown Link Policy Command. Returned Params (%#x)\n",HCI_CMD_GET_OCF(opcode));
                }

            }
            break;
        case OGF_INFO_PARAMS:
            {
                hciLog("OGF Info Params\n");
                /*======== Info Commands Opcode========================*/
                switch(HCI_CMD_GET_OCF(opcode))
                {
                    case OCF_HCI_Read_Local_Ver_Info:
                        hciLog("HCI Version                     -%#x\n",params[1]);
                        memcpy(&temp16,&params[2],sizeof(A_UINT16));
                        hciLog("HCI Revision                    -%#x\n",temp16);
                        hciLog("LMP/PAL_Version                 -%#x\n",params[4]);
                        memcpy(&temp16,&params[5],sizeof(A_UINT16));
                        hciLog("Manufacturer Name               -%#x\n",temp16);
                        memcpy(&temp16,&params[7],sizeof(A_UINT16));
                        hciLog("LMP/PAL_Subversion              -%#x\n",temp16);
                        break;
                    case OCF_HCI_Read_Local_Supported_Cmds:
                        hciLog("Supported Commands\n");
                        temp8 = params[1];
                        if(temp8 && 0x01)
                            hciLog("    - Inquiry");
                        if(temp8 && 0x02)
                            hciLog("    - Inquiry Cancel");
                        if(temp8 && 0x04)
                            hciLog("    - Periodic Inquiry Mode");
                        if(temp8 && 0x08)
                            hciLog("    - Exit Periodic Inquiry Mode");
                        if(temp8 && 0x10)
                            hciLog("    - Create Connection");
                        if(temp8 && 0x20)
                            hciLog("    - Disconnect");
                        if(temp8 && 0x40)
                            hciLog("    - Add SCO Connection");
                        if(temp8 && 0x80)
                            hciLog("    - Cancel Create Connection");
                        //Read the second Byte
                        temp8 = params[2];
                        if(temp8 && 0x01)
                            hciLog("    - Accept Connection Request");
                        if(temp8 && 0x02)
                            hciLog("    - Reject Connection Request");
                        if(temp8 && 0x04)
                            hciLog("    - Link Key Request Reply");
                        if(temp8 && 0x08)
                            hciLog("    - Link Key Request Negative Reply");
                        if(temp8 && 0x10)
                            hciLog("    - PIN Code Request Reply");
                        if(temp8 && 0x20)
                            hciLog("    - PIN Code Request Negative Reply");
                        if(temp8 && 0x40)
                            hciLog("    - Change Connection Packet Type");
                        if(temp8 && 0x80)
                            hciLog("    - Authentication Request");
                        break;
                    case OCF_HCI_Read_Data_Block_Size:
                        memcpy(&temp16,&params[1],sizeof(A_UINT16));
                        hciLog("Max ACL Data Packet Length      -%#x\n",temp16);
                        memcpy(&temp16,&params[3],sizeof(A_UINT16));
                        hciLog("Data_Block_Length               -%#x\n",temp16);
                        memcpy(&temp16,&params[5],sizeof(A_UINT16));
                        hciLog("Total_Num_Data_Blocks           -%#x\n",temp16);
                        break;
                    default:
                        hciLog("Unknown Info Command. Returned Params (%#x)\n",HCI_CMD_GET_OCF(opcode));
                }
            }
            break;
        case OGF_STATUS:
            {
                hciLog("OGF Status:\n");
                switch(HCI_CMD_GET_OCF(opcode))
                {
                    /*======== Status Commands Opcode======================*/
                    case OCF_HCI_Read_Link_Quality:
                        memcpy(&temp16,&params[1],sizeof(A_UINT16));
                        hciLog("Handle                          -%#x\n",temp16);
                        hciLog("Link Quality                    -%#x\n",params[3]);
                        break;
                    case OCF_HCI_Read_RSSI:
                        memcpy(&temp16,&params[1],sizeof(A_UINT16));
                        hciLog("Handle                          -%#x\n",temp16);
                        hciLog("RSSI                            -%#x\n",params[3]);
                        break;
                    case OCF_HCI_Read_Local_AMP_Info:
                        hciLog("AMP Status                      -%#x\n",params[1]);
                        memcpy(&temp32,&params[2],sizeof(A_UINT32));
                        hciLog("Total Bandwidth                 -%#x\n",temp32);
                        memcpy(&temp32,&params[6],sizeof(A_UINT32));
                        hciLog("Max Guaranteed Bandwidth        -%#x\n",temp32);
                        memcpy(&temp32,&params[10],sizeof(A_UINT32));
                        hciLog("Min Latency                     -%#x\n",temp32);
                        memcpy(&temp32,&params[14],sizeof(A_UINT32));
                        hciLog("Max PDU Size                    -%#x\n",temp32);
                        hciLog("Controller Type                 -%#x\n",params[18]);
                        memcpy(&temp16,&params[19],sizeof(A_UINT16));
                        hciLog("PAL Capabilities                -%#x\n",temp16);
                        memcpy(&temp16,&params[21],sizeof(A_UINT16));
                        hciLog("Max AMP ASSOC Length            -%#x\n",temp16);
                        memcpy(&temp32,&params[23],sizeof(A_UINT32));
                        hciLog("Max_Flush_Timeout               -%#x\n",temp32);
                        memcpy(&temp32,&params[27],sizeof(A_UINT32));
                        hciLog("BE_Flush_Timeout                -%#x\n",temp32);

                        break;
                    case OCF_HCI_Read_Local_AMP_ASSOC:
                        hciLog("Physical Link Handle            -%#x\n",params[1]);
                        memcpy(&temp16,&params[2],sizeof(A_UINT32));
                        hciLog("AMP ASSOC Remaining Length      -%#x\n",temp16);
                        hciLog("AMP_ASSOC_fragment : \n");
                        printBuffer(&params[4],temp16);
                        break;
                    case OCF_HCI_Write_Remote_AMP_ASSOC:
                        hciLog("Physical Link Handle            -%#x\n",params[1]);
                        break;
                    default:
                        hciLog("Unknown Status Command. Returned Params (%#x)\n",HCI_CMD_GET_OCF(opcode));
                }
            }
            break;

        default:
            hciLog("Unknown OGF Command (%#x)\n",HCI_CMD_GET_OGF(opcode));
    }
}


void palEvents( HCI_EVENT_PKT *hciEvent,int len)
{
    HCI_EVENT_CMD_COMPLETE  *hciCmdComplete;
    HCI_EVENT_CMD_STATUS    *hciCmdStatus;
    HCI_EVENT_HW_ERR        *hciHwError;
    HCI_EVENT_FLUSH_OCCRD   *hciFlush;
    HCI_EVENT_LOOPBACK_CMD  *hciLoopBack;
    HCI_EVENT_DATA_BUF_OVERFLOW     *hciBufOvrFlow;
    HCI_EVENT_PHY_LINK_COMPLETE     *hciPhyLinkComplete;
    HCI_EVENT_CHAN_SELECT   *hciChannelSelect;
    HCI_EVENT_DISCONNECT_PHY_LINK_COMPLETE  *hciDisconnectPhyLink;
    HCI_EVENT_LOGICAL_LINK_COMPLETE_EVENT   *hciLogicalLinkCmpl;
    HCI_EVENT_DISCONNECT_LOGICAL_LINK_EVENT *hciLogicalDiscconnect;
    HCI_EVENT_FLOW_SPEC_MODIFY              *hciFlowSpecModify;
    HCI_EVENT_NUM_COMPL_DATA_BLKS   *hciNumCmplDataBlks;
    HCI_EVENT_SRM_COMPL     *hciSRMComplete;
    HCI_EVENT_PHY_LINK_LOSS_EARLY_WARNING   *hciEarlyLinkLoss;
    HCI_EVENT_PHY_LINK_RECOVERY *hciPhyLinkRecovery;
    HCI_EVENT_AMP_STATUS_CHANGE *hciAmpStatusChange;
    A_INT32     i;
    A_UINT8     *buf;
    A_UINT16    temp16;
    A_UINT8     *rBuffer;
    rBuffer = (A_UINT8 *)hciEvent;
    hciLog("PAL Event \n");
    hciLog("____________________________________________________________________________________\n");
    hciLog("RAW PACKET\n");
    printBuffer(rBuffer,len);

    switch(hciEvent->event_code)
    {
        case PAL_COMMAND_COMPLETE_EVENT:
            hciCmdComplete = (HCI_EVENT_CMD_COMPLETE *)hciEvent;
            hciLog("PAL_COMMAND_COMPLETE_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciCmdComplete->event_code);
            hciLog("Param Length                    -%#x \n",hciCmdComplete->param_len);
            hciLog("Number of HCI Command Packets   -%#x \n",hciCmdComplete->num_hci_cmd_pkts);
            hciLog("OpCode                          -%#x \n",hciCmdComplete->opcode);
            hciLog("Parameters\n");
            printBuffer(hciCmdComplete->params,hciCmdComplete->param_len-3);
            cmdCompleteReturnParams(hciCmdComplete->opcode,hciCmdComplete->params);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_COMMAND_STATUS_EVENT:
            hciCmdStatus = (HCI_EVENT_CMD_STATUS *)hciEvent;
            hciLog("PAL_COMMAND_STATUS_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x  \n",hciCmdStatus->event_code);
            hciLog("Param Length                    -%#x  \n",hciCmdStatus->param_len);
            if(hciCmdStatus->status <= 0x39)
                hciLog("Status                          -%s   \n",errorCodes[hciCmdStatus->status]);
            else
                hciLog("Status                          -Unknown Error code\n");

            hciLog("Number of HCI Command Packets   -%#x \n",hciCmdStatus->num_hci_cmd_pkts);
            hciLog("OpCode                          -%#x \n",hciCmdStatus->opcode);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_HARDWARE_ERROR_EVENT:
            hciHwError = (HCI_EVENT_HW_ERR *)hciEvent;
            hciLog("PAL_HARDWARE_ERROR_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x  \n",hciHwError->event_code);
            hciLog("Param Length                    -%#x  \n",hciHwError->param_len);
            hciLog("Hardware Error Code             -%#x  \n",hciHwError->hw_err_code);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_FLUSH_OCCURRED_EVENT:
            hciFlush = (HCI_EVENT_FLUSH_OCCRD *)hciEvent;
            hciLog("PAL_FLUSH_OCCURRED_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciFlush->event_code);
            hciLog("Param Length                    -%#x \n",hciFlush->param_len);
            hciLog("Handle                          -%#x \n",hciFlush->handle);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_LOOPBACK_EVENT:
            hciLoopBack = (HCI_EVENT_LOOPBACK_CMD *)hciEvent;
            hciLog("PAL_LOOPBACK_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x  \n",hciLoopBack->event_code);
            hciLog("Param Length                    -%#x  \n",hciLoopBack->param_len);
            hciLog("Params \n");
            printBuffer(hciLoopBack->params,hciLoopBack->param_len);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_BUFFER_OVERFLOW_EVENT:
            hciBufOvrFlow = (HCI_EVENT_DATA_BUF_OVERFLOW *)hciEvent;
            hciLog("PAL_BUFFER_OVERFLOW_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x  \n",hciBufOvrFlow->event_code);
            hciLog("Param Length                    -%#x  \n",hciBufOvrFlow->param_len);
            hciLog("Link Type                       -%#x  \n",hciBufOvrFlow->link_type);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_QOS_VIOLATION_EVENT:
            hciFlush = (HCI_EVENT_FLUSH_OCCRD *)hciEvent;
            hciLog("PAL_QOS_VIOLATION_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x  \n",hciFlush->event_code);
            hciLog("Param Length                    -%#x  \n",hciFlush->param_len);
            hciLog("Handle                          -%#x \n",hciFlush->handle);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_PHYSICAL_LINK_COMPL_EVENT:
            hciPhyLinkComplete = (HCI_EVENT_PHY_LINK_COMPLETE *)hciEvent;
            hciLog("PAL_PHYSICAL_LINK_COMPL_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x  \n",hciPhyLinkComplete->event_code);
            hciLog("Param Length                    -%#x  \n",hciPhyLinkComplete->param_len);
            if(hciPhyLinkComplete->status <= 0x39)
                hciLog("Status                          -%s   \n",errorCodes[hciPhyLinkComplete->status]);
            else
                hciLog("Status                          -Unknown Error code\n");
            hciLog("Phy Link Handle                 -%#x\n",hciPhyLinkComplete->phy_link_hdl);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_CHANNEL_SELECT_EVENT:
            hciChannelSelect = (HCI_EVENT_CHAN_SELECT *)hciEvent;
            hciLog("PAL_CHANNEL_SELECT_EVENT \n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x  \n",hciChannelSelect->event_code);
            hciLog("Param Length                    -%#x  \n",hciChannelSelect->param_len);
            hciLog("PHY Link Handle                 -%#x \n",hciChannelSelect->phy_link_hdl);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_DISCONNECT_PHYSICAL_LINK_EVENT:
            hciDisconnectPhyLink = (HCI_EVENT_DISCONNECT_PHY_LINK_COMPLETE *)hciEvent;
            hciLog("PAL_DISCONNECT_PHYSICAL_LINK_EVENT \n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x  \n",hciDisconnectPhyLink->event_code);
            hciLog("Param Length                    -%#x  \n",hciDisconnectPhyLink->param_len);
            if(hciDisconnectPhyLink->status <= 0x39)
                hciLog("Status                          -%s   \n",errorCodes[hciDisconnectPhyLink->status]);
            else
                hciLog("Status                          -Unknown Error code\n");
            hciLog("Phy Link hdl                    -%#x \n",hciDisconnectPhyLink->phy_link_hdl);
            if(hciDisconnectPhyLink->reason <= 0x39)
                hciLog("Reason                          -%s   \n",errorCodes[hciDisconnectPhyLink->reason]);
            else
                hciLog("Reason                          -Unknown Error code\n");
            hciLog("\n------------------------------------\n");
            break;
        case PAL_PHY_LINK_EARLY_LOSS_WARNING_EVENT:
            hciEarlyLinkLoss = (HCI_EVENT_PHY_LINK_LOSS_EARLY_WARNING *)hciEvent;
            hciLog("PAL_PHY_LINK_EARLY_LOSS_WARNING_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciEarlyLinkLoss->event_code);
            hciLog("Param Length                    -%#x \n",hciEarlyLinkLoss->param_len);
            hciLog("Phy Link Handle                 -%#x \n",hciEarlyLinkLoss->phy_hdl);
        if(hciEarlyLinkLoss->reason == 0x00)
        {
        hciLog("Reason              -Unknown\n");
        }
        else if(hciEarlyLinkLoss->reason == 0x01)
        {
        hciLog("Reason              -Range Related\n");
        }
        else if(hciEarlyLinkLoss->reason == 0x02)
        {
        hciLog("Reason              -Bandwidth related\n");
        }
        else if(hciEarlyLinkLoss->reason == 0x03)
        {
        hciLog("Reason              -Resolving conflict\n");
        }
        else if(hciEarlyLinkLoss->reason == 0x04)
        {
        hciLog("Reason              -Interference\n");
        }
        else
        {
        hciLog("Reason              -Reserved(%#x)\n",hciEarlyLinkLoss->reason);
        }



            hciLog("\n------------------------------------\n");
            break;
        case PAL_PHY_LINK_RECOVERY_EVENT:
            hciPhyLinkRecovery = (HCI_EVENT_PHY_LINK_RECOVERY *)hciEvent;
            hciLog("PAL_PHY_LINK_RECOVERY_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciPhyLinkRecovery->event_code);
            hciLog("Param Length                    -%#x \n",hciPhyLinkRecovery->param_len);
            hciLog("Phy Link Handle         -%#x \n",hciPhyLinkRecovery->phy_hdl);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_LOGICAL_LINK_COMPL_EVENT:
            hciLogicalLinkCmpl = (HCI_EVENT_LOGICAL_LINK_COMPLETE_EVENT *)hciEvent;
            hciLog("PAL_LOGICAL_LINK_COMPL_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciLogicalLinkCmpl->event_code);
            hciLog("Param Length                    -%#x \n",hciLogicalLinkCmpl->param_len);
            if(hciLogicalLinkCmpl->status <= 0x39)
                hciLog("Status                          -%s   \n",errorCodes[hciLogicalLinkCmpl->status]);
            else
                hciLog("Status                          -Unknown Error code\n");
            hciLog("Logical Link hdl                -%#x \n",hciLogicalLinkCmpl->logical_link_hdl);
            hciLog("Phy hdl                         -%#x \n",hciLogicalLinkCmpl->phy_hdl);
            hciLog("Tx Flow Spec ID                 -%#x \n",hciLogicalLinkCmpl->tx_flow_id);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_DISCONNECT_LOGICAL_LINK_COMPL_EVENT:
            hciLogicalDiscconnect = (HCI_EVENT_DISCONNECT_LOGICAL_LINK_EVENT *)hciEvent;
            hciLog("PAL_DISCONNECT_LOGICAL_LINK_COMPL_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciLogicalDiscconnect->event_code);
            hciLog("Param Length                    -%#x \n",hciLogicalDiscconnect->param_len);
            if(hciLogicalDiscconnect->status <= 0x39)
                hciLog("Status                          -%s   \n",errorCodes[hciLogicalDiscconnect->status]);
            else
                hciLog("Status                          -Unknown Error code\n");
            hciLog("Logical Link hdl                -%#x \n",hciLogicalDiscconnect->logical_link_hdl);
            hciLog("Reason                          -%#x \n",hciLogicalDiscconnect->reason);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_FLOW_SPEC_MODIFY_COMPL_EVENT:
            hciFlowSpecModify = (HCI_EVENT_FLOW_SPEC_MODIFY  *)hciEvent;
            hciLog("PAL_FLOW_SPEC_MODIFY_COMPL_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciFlowSpecModify->event_code);
            hciLog("Param Length                    -%#x \n",hciFlowSpecModify->param_len);
            if(hciFlowSpecModify->status <= 0x39)
                hciLog("Status                          -%s   \n",errorCodes[hciFlowSpecModify->status]);
            else
                hciLog("Status                          -Unknown Error code\n");
            hciLog("Handle                          -%#x \n",hciFlowSpecModify->handle);
#if 0
            if(hciFlowSpecModify->reason <= 0x39)
                hciLog("Reason                          -%s   \n",errorCodes[hciFlowSpecModify->reason]);
            else
                hciLog("Reason                          -Unknown Error code\n");
#endif
            hciLog("\n------------------------------------\n");
            break;
        case PAL_NUM_COMPL_DATA_BLOCK_EVENT:
            hciNumCmplDataBlks = (HCI_EVENT_NUM_COMPL_DATA_BLKS *)hciEvent;
            hciLog("PAL_NUM_COMPL_DATA_BLOCK_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciNumCmplDataBlks->event_code);
            hciLog("Param Length                    -%#x \n",hciNumCmplDataBlks->param_len);
            hciLog("Number of Data Blks             -%#x \n",hciNumCmplDataBlks->num_data_blks);
            hciLog("Number of Handles               -%#x \n",hciNumCmplDataBlks->num_handles);
            hciLog("Params \n");
            buf = hciNumCmplDataBlks->params;
            for(i = 0; i < hciNumCmplDataBlks->num_handles; i++)
            {
                memcpy(&temp16,&buf[i * sizeof(A_UINT16)],sizeof(A_UINT16));
                hciLog("Handle[%d]          -%#x \n",i,temp16);
            }
            buf = buf + (hciNumCmplDataBlks->num_handles * sizeof(A_UINT16));
            for(i=0;i < hciNumCmplDataBlks->num_handles; i++)
            {
                memcpy(&temp16,&buf[i * sizeof(A_UINT16)],sizeof(A_UINT16));
                hciLog("Num Of Completed Packets[%d]    -%#x \n",i,temp16);
            }
            buf = buf + (hciNumCmplDataBlks->num_handles * sizeof(A_UINT16));
            for(i=0;i < hciNumCmplDataBlks->num_handles; i++)
            {
                memcpy(&temp16,&buf[i * sizeof(A_UINT16)],sizeof(A_UINT16));
                hciLog("Num Of Completed Blocks [%d]    -%#x \n",i,temp16);
            }
            printBuffer(hciNumCmplDataBlks->params,hciNumCmplDataBlks->param_len);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_SHORT_RANGE_MODE_CHANGE_COMPL_EVENT:
            hciSRMComplete = (HCI_EVENT_SRM_COMPL *)hciEvent;
            hciLog("PAL_SHORT_RANGE_MODE_CHANGE_COMPL_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciSRMComplete->event_code);
            hciLog("Param Length                    -%#x \n",hciSRMComplete->param_len);
            if(hciSRMComplete->status <= 0x39)
                hciLog("Status                          -%s   \n",errorCodes[hciSRMComplete->status]);
            else
                hciLog("Status                          - Unknown Error code\n");
            hciLog("Handle                          -%#x \n",hciSRMComplete->phy_link);
            hciLog("State                           -%#x \n",hciSRMComplete->state);
            hciLog("\n------------------------------------\n");
            break;
        case PAL_AMP_STATUS_CHANGE_EVENT:
            hciAmpStatusChange = (HCI_EVENT_AMP_STATUS_CHANGE *)hciEvent;
            hciLog("PAL_AMP_STATUS_CHANGE_EVENT\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciAmpStatusChange->event_code);
            hciLog("Param Length                    -%#x \n",hciAmpStatusChange->param_len);
        if(hciAmpStatusChange->status == 0x00)
        {
            hciLog("Status              - AMP_Status_Change has occurred\n");
        }
        else
        {
        if(hciAmpStatusChange->status <= 0x39)
                hciLog("Status                          -%s   \n",errorCodes[hciAmpStatusChange->status]);
        else
                hciLog("Status                          - Unknown Error code\n");
        }
        hciLog("AMP Status              -%#x \n",hciAmpStatusChange->amp_status);
            hciLog("\n------------------------------------\n");
            break;

        default:
            hciLog("Unknown Event\n");
            hciLog("\n------------------------------------\n");
            hciLog("Event Code                      -%#x \n",hciEvent->event_code);
            hciLog("Param Length                    -%#x \n",hciEvent->param_len);
            printBuffer(hciEvent->params,hciEvent->param_len);
            hciLog("\n------------------------------------\n");
    }
    hciLog("____________________________________________________________________________________\n");

}

#if 0

void parseHciCmdsnEvents(FILE *fp,int lineCount)
{
    A_INT32 bufSize = 0;
    A_INT32 cnt     = 0;
    A_INT32 len     = 0;
    A_UINT8 buffer  [500];
    A_UINT8 cBuf    [500];
    A_UINT32 i      = 0;
    A_UINT8 pLength = 0;
    HCI_CMD_PKT     *hciCmd;
    HCI_EVENT_PKT   *hciEvent;
    A_UINT32 tmp    = 0;
    A_INT32 cmdCnt      = 0;
    A_INT32 eventCnt    = 0;



    fseek(fp,0,SEEK_SET);
    while(cnt < lineCount)
    {
        memset(buffer,0,500);
        memset(cBuf,0,500);
        bufSize = readline(fp,buffer);
        if(strncmp(pal_log[0],buffer,strlen(pal_log[0])) == 0)
        {
            hciLog("Command %d\n",++cmdCnt);
            for(i = 0; i < EMPTY_LINES ;i++)
            {
                bufSize = readline(fp,buffer);
                cnt++;
            }
            len     = strTochar(buffer,cBuf,bufSize);
            pLength = cBuf[CMD_PARAMLENGTH_OFFSET];
            while(pLength > (len - CMD_HEADER_SIZE))
            {
                tmp     = bufSize;
                bufSize += readline(fp,(buffer + bufSize));
                len     += strTochar(buffer + tmp,cBuf + len,bufSize);
                cnt++;
            }
            hciLog("\n______________________________________________________________________________\n");
            hciLog("RAW BYTES\n");
            printBuffer(cBuf,len);
            cmdParser((HCI_CMD_PKT *)cBuf);
            hciLog("\n______________________________________________________________________________\n");
            cnt++;

        }
        else if(strncmp(pal_log[1],buffer,strlen(pal_log[1])) == 0)
        {
            hciLog("Event %d\n",++eventCnt);
            for(i = 0;i < EMPTY_LINES; i++)
            {
                bufSize = readline(fp,buffer);
                cnt++;
            }
            len = strTochar(buffer,cBuf,bufSize);
            pLength = cBuf[EVENT_PARAMLENGTH_OFFSET];
            while(pLength > (len - EVENT_HEADER_SIZE))
            {
                tmp     = bufSize;
                bufSize += readline(fp,(buffer + bufSize));
                len     += strTochar(buffer + tmp,cBuf + len,bufSize);
                cnt++;
            }
            //Complete params as Buffer
            hciLog("\n______________________________________________________________________________\n");
            hciLog("RAW BYTES\n");
            printBuffer(cBuf,len);
            palEvents((HCI_EVENT_PKT *)cBuf);
            hciLog("\n______________________________________________________________________________\n");
            cnt++;
        }
        else
        {
            //hciLog("Unknown\n");
            //for(i = 0;i < bufSize; i++)
            //{
            //    hciLog("%c",buffer[i]);
            //}
            cnt++;
        }
        hciLog("\n");
    }
}
A_INT32 main(int argc,char *argv[])
{
    FILE    *hciParser = NULL;
    A_INT32     lineCount;
    opFile = fopen(OUTPUT_FILE_NAME,"w+");
    hciLog("############################\n");
    hciLog("PAL Command and Event Parser\n");
    hciLog("############################\n\n");

    if(argc > 1)
    {
        hciParser   = fopen(argv[1],"r");
    }
    else
    {
        hciParser   = fopen(DEFAULT_INPUT_FILE,"r");
    }
    if(hciParser != NULL)
    {
        lineCount   = countLines(hciParser);
        //hciLog("Number of Lines %d\n",lineCount);
        //hciLog("Input File Found\n");
        parseHciCmdsnEvents(hciParser,lineCount);
        fclose(hciParser);
    }
    else
    {
        hciLog("Can't find Input file\n");
    }
    return 0;
}
#endif
