/*
 * ALSA SoC CX20709 Channel codec driver
 *
 * Copyright:   (C) 2009/2010 Conexant Systems
 *
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This code is to download the firmware to CX2070x device. 
 *      
 *************************************************************************
 *  Modified Date:  01/24/11
 *  File Version:   1.0.0.1
 *************************************************************************
 */
#if defined(_MSC_VER) 
// microsoft windows environment.
#define  __BYTE_ORDER       __LITTLE_ENDIAN
#define  __LITTLE_ENDIAN    1234
#include <stdlib.h>   // For _MAX_PATH definition
#include <stdio.h>
#include <string.h>
#define msleep(_x_) 
int printk(const char *s, ...);
#define KERN_ERR "<3>"
#elif defined(__KERNEL__)  
// linux kernel environment.
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#else
//
// linux user mode environment.
//
#include <stdlib.h>   // For _MAX_PATH definition
#include <stdio.h>
#endif
#include "cxpump.h"

#if defined( __BIG_ENDIAN) && !defined(__BYTE_ORDER)
#define __BYTE_ORDER __BIG_ENDIAN
#elif defined( __LITTLE_ENDIAN ) && !defined(__BYTE_ORDER)
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif 


#ifndef __BYTE_ORDER
#error __BYTE_ORDER is undefined.
#endif 

#define ENABLE_I2C_BURST_MODE

#if defined(_MSC_VER) 
#pragma warning(disable:4127 4706 4101) // conditional experssion is constant
typedef enum I2C_STATE{I2C_OK,I2C_ERR,I2C_RETRY} ;
void ShowProgress(int curPos,bool bForceRedraw, I2C_STATE eState, const int MaxPos);
void InitShowProgress(const int MaxPos);
#elif defined(__KERNEL__)  
//linux kernel mode
#define ShowProgress(curPos,bForceRedraw,eState, axPos)
#define InitShowProgress(MaxPos)
#else 
//linux user mode
#define InitShowProgress(MaxPos)
#define ShowProgress(curPos,bForceRedraw,eState, axPos)
#endif


#ifndef NULL
#define NULL 0
#endif //#ifndef NULL

#define S_DESC          "Cnxt Channel Firmware"  /*Specify the string that will show on head of rom file*/
#define S_ROM_FILE_NAME "cx2070x.fw"            /*Specify the file name of rom file*/
#define CHIP_ADDR        0x14                    /*Specify the i2c chip address*/
#define MEMORY_UPDATE_TIMEOUT  300
#define MAX_ROM_SIZE (1024*1024)
//#define DBG_ERROR  "ERROR  : "
#define DBG_ERROR  KERN_ERR
#define LOG( _msg_ )  printk  _msg_ 
//#define LOG( _msg_ )  ;

typedef struct CX_CODEC_ROM_DATA
{
#ifdef USE_TYPE_DEFINE
    unsigned long      Type;
#endif //#ifdef USE_TYPE_DEFINE
    unsigned long      Length;
    unsigned long      Address;
    unsigned char      data[1];
}CX_CODEC_ROM_DATA;

#define ROM_DATA_TYPE_S37           0xAA55CC01 // S37 format.
#define ROM_DATA_TYPE_CNXT          0xAA55CC04 // Conexant SPI format.
#define ROM_DATA_SEPARATED_LINE     0x23232323 //()()()()

typedef struct CX_CODEC_ROM{
    char                        sDesc[24]; 
    char                        cOpenBracket;
    char                        sVersion[5];
    char                        cCloseBracket;
    char                        cEOF;
    unsigned long               FileSize;
    unsigned long               LoaderAddr;
    unsigned long               LoaderLen;
    unsigned long               CtlAddr;
    unsigned long               CtlLen;
    unsigned long               SpxAddr;
    unsigned long               SpxLen;
    struct CX_CODEC_ROM_DATA    Data[1];
}CX_CODEC_ROM;

typedef struct CX_CODEC_APPENDED_DATA
{
    unsigned char      Address[2];      // The address of data.
    unsigned char      padding;      // The actual firmware data.
    unsigned char      data;      // The actual firmware data.
}CX_CODEC_APPENDED_DATA;

typedef struct CX_CODEC_ROM_APPENDED{
    unsigned long               TuningAddr;
    unsigned long               TuningLen;
    CX_CODEC_APPENDED_DATA      data[1]; // following by Jira id and time.
}CX_CODEC_ROM_APPENDED;

typedef struct CX_CODEC_ROM_APPENDED_INFO{
    char                        sJIRAID[16];
    char                        sTime[16];
}CX_CODEC_ROM_APPENDED_INFO;


// To convert two digital ASCII into one BYTE.
unsigned char ASCII_2_BYTE( char ah, char al) ;

#define BUF_SIZE 0x1000
#define BIBF(_x_) if(!(_x_)) break;
#define BIF(_x_) if((ErrNo=(_x_)) !=0) break;


#ifndef BIBF
#define BIBF( _x_ ) if(!(_x_)) break;
#endif 

enum { 
    MEM_TYPE_RAM     = 1 /* CTL*/, 
    MEM_TYPE_SPX     = 2,
    MEM_TYPE_EEPROM  = 3,
    MEM_TYPE_CPX     =0x04,
    MEM_TYPE_EEPROM_RESET  = 0x8003, //reset after writing.
}; 


fun_I2cWriteThenRead g_I2cWriteThenReadPtr      = NULL;
fun_I2cWrite         g_I2cWritePtr              = NULL;
unsigned char *      g_AllocatedBuffer          = NULL;
unsigned char *      g_Buffer                   = NULL;
unsigned long        g_cbMaxWriteBufSize        = 0;
void *               g_pContextI2cWrite         = NULL;
void *               g_pContextI2cWriteThenRead = NULL;


/*
* The SetupI2cWriteCallback sets the I2cWrite callback function.
* 
* PARAMETERS
*  
*    pCallbackContext [in] - A pointer to a caller-defined structure of data items
*                            to be passed as the context parameter of the callback
*                            routine each time it is called. 
*
*    I2cWritePtr      [in] - A pointer to a i2cwirte callback routine, which is to 
*                            write I2C data. The callback routine must conform to 
*                            the following prototype:
* 
*                        int (*fun_I2cWrite)(  
*                                void * pCallbackContext,
*                                unsigned char ChipAddr,
*                                unsigned long cbBuf, 
*                                unsigned char* pBuf
*                             );
*
*                        The callback routine parameters are as follows:
*
*                        pCallbackContext [in] - A pointer to a caller-supplied 
*                                                context area as specified in the
*                                                CallbackContext parameter of 
*                                                SetupI2cWriteCallback. 
*                        ChipAddr         [in] - The i2c chip address.
*                        cbBuf            [in] - The size of the input buffer, in bytes.
*                        pBuf             [in] - A pointer to the input buffer that contains 
*                                                the data required to perform the operation.
*
*
*    cbMaxWriteBuf    [in] - Specify the maximux transfer size for a I2c continue 
*                            writing with 'STOP'. This is limited in I2C bus Master
*                            device. The size can not less then 3 since Channel 
*                            requires 2 address bytes plus a data byte.
*                              
*
*
* RETURN
*  
*    None
*
*/
void SetupI2cWriteCallback( void * pCallbackContext,
    fun_I2cWrite         I2cWritePtr,
    unsigned long        cbMaxWriteBufSize)
{
    g_pContextI2cWrite  = pCallbackContext;
    g_I2cWritePtr       = I2cWritePtr;
    g_cbMaxWriteBufSize = cbMaxWriteBufSize;
}

/*
* The SetupI2cWriteThenReadCallback sets the SetupI2cWriteThenRead callback function.
* 
* PARAMETERS
*  
*    pCallbackContext    [in] - A pointer to a caller-defined structure of data items
*                               to be passed as the context parameter of the callback
*                               routine each time it is called. 
*
*    I2cWriteThenReadPtr [in] - A pointer to a i2cwirte callback routine, which is to 
*                               write I2C data. The callback routine must conform to 
*                               the following prototype:
*
*                        int (*fun_I2cWriteThenRead)(  
*                                void * pCallbackContext,
*                                unsigned char ChipAddr,
*                                unsigned long cbBuf, 
*                                unsigned char* pBuf
*                             );
*
*                        The callback routine parameters are as follows:
*
*                         pCallbackContext [in] - A pointer to a caller-supplied 
*                                                 context area as specified in the
*                                                 CallbackContext parameter of 
*                                                 SetupI2cWriteCallback. 
*                         ChipAddr         [in] - The i2c chip address.
*                         cbBuf            [in] - The size of the input buffer, in bytes.
*                         pBuf             [in] - A pointer to the input buffer that contains 
*                                                 the data required to perform the operation.
*
* RETURN
*      None
* 
*/
void SetupI2cWriteThenReadCallback( void * pCallbackContext,
    fun_I2cWriteThenRead I2cWriteThenReadPtr)
{
    g_pContextI2cWriteThenRead  = pCallbackContext;
    g_I2cWriteThenReadPtr       = I2cWriteThenReadPtr;
}

void SetupMemoryBuffer(void * pAllocedMemoryBuffer)
{
    g_AllocatedBuffer = (unsigned char*)pAllocedMemoryBuffer;
    g_Buffer = g_AllocatedBuffer +2;
}

/*
* Convert a 4-byte number from a ByteOrder into another ByteOrder.
*/
unsigned long ByteOrderSwapULONG(unsigned long i)
{
    return((i&0xff)<<24)+((i&0xff00)<<8)+((i&0xff0000)>>8)+((i>>24)&0xff);
}

/*
* Convert a 2-byte number from a ByteOrder into another ByteOrder.
*/
unsigned short ByteOrderSwapWORD(unsigned short i)
{
    return ((i>>8)&0xff)+((i << 8)&0xff00);
}

/*
* Convert a 4-byte number from generic byte order into Big Endia
*/
unsigned long ToBigEndiaULONG(unsigned long i)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return ByteOrderSwapULONG(i);
#else
    return i;
#endif
}


/*
* Convert a 2-byte number from generic byte order into Big Endia
*/
unsigned short ToBigEndiaWORD(unsigned short i)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return ByteOrderSwapWORD(i);
#else
    return i;
#endif
}

/*
* Convert a 4-byte number from Big Endia into generic byte order.
*/
unsigned long FromBigEndiaULONG(unsigned long i)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return ByteOrderSwapULONG(i);
#else
    return i;
#endif
}


/*
* Convert a 2-byte number from Big Endia into generic byte order.
*/
unsigned short FromBigEndiaWORD(unsigned short i)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
    return ByteOrderSwapWORD(i);
#else
    return i;
#endif
}


/*
* To convert two digital ASCII into one BYTE.
*/
unsigned char ASCII_2_BYTE( char ah, char al) 
{
    unsigned char ret = '\0';
    int i =2;

    for(;i>0;i--)
    {
        if( 'a' <= ah && 'f' >= ah)
        {
            ret += ah - 'a'+10;
        }
        else if( 'A' <= ah && 'F' >= ah)
        {
            ret += ah -'A'+10;
        }

        else if( '0' <= ah && '9' >= ah)
        {
            ret += ah - '0';
        }
        else
        {
            LOG((DBG_ERROR "Invalid txt data.\n"));

            // ErrNo = ERRNO_INVALID_DATA;
            break;
        }
        ah =al;
        if(i==2)
            ret = (unsigned short)ret << 4;
    }
    return ret;
}

/*
* Read a byte from the specified  register address.
* 
* PARAMETERS
*  
*    RegAddr             [in] - Specifies the register address.
*
* RETURN
*  
*    Returns the byte that is read from the specified register address.
*
*/
unsigned char ReadReg(unsigned short RegAddr)
{

    unsigned char RegData;

    if(!g_I2cWriteThenReadPtr)
    {
        LOG((DBG_ERROR "i2C function is not set.\n"));
        return 0;
    }


    RegAddr = ToBigEndiaWORD(RegAddr);

    g_I2cWriteThenReadPtr(g_pContextI2cWriteThenRead,CHIP_ADDR,
        2,(unsigned char*) &RegAddr,1,&RegData);

    return RegData;
}


/*
* Write a byte from the specified register address.
* 
* PARAMETERS
*  
*    RegAddr             [in] - Specifies the register address.
*
* RETURN
*  
*    Returns the byte that is read from the specified register address.
*
* REMARK
* 
*    The g_I2cWriteThenReadPtr must be set before calling this function.
*/
int WriteReg(unsigned short RegAddr, unsigned char RegData)
{
    unsigned char WrBuf[3];

    if(!g_I2cWritePtr)
    {
        LOG((DBG_ERROR "i2C function is not set.\n"));
        return -ERRNO_I2CFUN_NOT_SET;
    }

    *((unsigned short*) WrBuf) = ToBigEndiaWORD(RegAddr);
    WrBuf[2] = RegData;

    g_I2cWritePtr(g_pContextI2cWrite,CHIP_ADDR,sizeof(WrBuf),WrBuf);

    return ERRNO_NOERR;
}

/*
*  Writes a number of bytes from a buffer to Channel via I2C bus.
*  
* PARAMETERS
*  
*    NumOfBytes         [in] - Specifies the number of bytes to be written
*                              to the memory address.
*    pData              [in] - Pointer to a buffer from an array of I2C data
*                              are to be written.
*  
* RETURN
*  
*    If the operation completes successfully, the return value is ERRNO_NOERR.
*    Otherwise, return ERRON_* error code. 
*/
int ChannelI2cBulkWrite( unsigned long NumOfBytes, unsigned char *pData)
{
    int ErrNo           = ERRNO_NOERR;
    unsigned short CurAddr;

    //unsigned char  *pDataEnd            = pData + NumOfBytes;
    unsigned char  *pCurData            = pData;
    unsigned short *pCurAddrByte        = NULL;
    unsigned long  BytesToProcess       = 0;
    unsigned short backup               = 0;
    const unsigned long cbAddressBytes  = 2;
    const unsigned long cbMaxDataLen    = g_cbMaxWriteBufSize-cbAddressBytes;


    if(!g_I2cWritePtr )
    {
        LOG((DBG_ERROR "i2C function is not set.\n"));
        return -ERRNO_I2CFUN_NOT_SET;
    }

    //assert(NumOfBytes < 3);
    CurAddr = FromBigEndiaWORD( *((unsigned short*)pData));

    //skip first 2 bytes data (address).
    NumOfBytes -= cbAddressBytes;
    pCurData   += cbAddressBytes;

    for(;NumOfBytes;)
    {
        BytesToProcess = NumOfBytes > cbMaxDataLen? cbMaxDataLen : NumOfBytes;
        NumOfBytes-= BytesToProcess;
        // save the pervious 2 bytes for later use.
        pCurAddrByte = (unsigned short*) (pCurData -cbAddressBytes);
        backup       = *pCurAddrByte;
        *pCurAddrByte=  ToBigEndiaWORD(CurAddr);
        BIBF(g_I2cWritePtr(g_pContextI2cWrite,CHIP_ADDR, BytesToProcess + cbAddressBytes,(unsigned char*)pCurAddrByte));
        //restore the data 
        *pCurAddrByte = backup;

        pCurData += BytesToProcess;
        CurAddr  += (unsigned short)BytesToProcess;
    }
    return ErrNo;
}

/*
*  Writes a number of bytes from a buffer to the specified memory address.
*
* PARAMETERS
*
*    dwAddr             [in] - Specifies the memory address.
*    NumOfBytes         [in] - Specifies the number of bytes to be written
*                              to the memory address.
*    pData              [in] - Pointer to a buffer from an struct of 
*                              CX_CODEC_ROM_DATA is to be written.
*    MemType            [in] - Specifies the requested memory type, the value must be from 
*                              the following table.
*
*                              MEM_TYPE_RAM     = 1
*                              MEM_TYPE_SPX     = 2
*                              MEM_TYPE_EEPROM  = 3
*
* RETURN
*  
*    If the operation completes successfully, the return value is ERRNO_NOERR.
*    Otherwise, return ERRON_* error code. 
*/
int CxWriteMemory(unsigned long dwAddr, unsigned long NumOfBytes, unsigned char * pData, int MemType )
{
    int ErrNo           = ERRNO_NOERR;
    unsigned char      Address[4];
    unsigned char      WrData[8];
    unsigned char      offset = 0;
    const unsigned long MAX_BUF_LEN = 0x100;
    unsigned char      cr = 0;
    int                 bNeedToContinue = 0;
    int                 i=0;


    const unsigned long cbAddressBytes  = 2;

    unsigned short *    pAddressByte;
    unsigned char *pEndData  = pData + NumOfBytes;
    unsigned short      RegMemMapAddr = ToBigEndiaWORD(0x300);
    unsigned long       BytesToProcess = 0;

    while(NumOfBytes)
    {

        BytesToProcess = NumOfBytes <= MAX_BUF_LEN ? NumOfBytes : MAX_BUF_LEN;
        NumOfBytes -= BytesToProcess;
        pEndData  = pData + BytesToProcess;

        *((unsigned long*)&Address) = ToBigEndiaULONG(dwAddr);
        //        dwAddr += offset;
        offset = 0;

        if( !bNeedToContinue )
        {
#ifdef ENABLE_I2C_BURST_MODE
            //
            //  Update the memory target address and buffer length.
            //
            WrData[0] = 0x02;    //update target address Low 0x02FC 
            WrData[1] = 0xFC;
            WrData[2] = Address[3];
            WrData[3] = Address[2];
            WrData[4] = Address[1];
            WrData[5] = (unsigned char)BytesToProcess -1 ;  // X bytes - 1
            BIBF(g_I2cWritePtr(g_pContextI2cWrite,CHIP_ADDR, 6 , WrData));
#else
            //
            //  Update the memory target address and buffer length.
            //
            WrData[0] = 0x02;    //update target address Low 0x02FC 
            WrData[1] = 0xFC;
            WrData[2] = Address[3];
            BIBF(g_I2cWritePtr(g_pContextI2cWrite,CHIP_ADDR, 3 , WrData));

            WrData[0] = 0x02;    //update target address Middle 0x02FD
            WrData[1] = 0xFD;
            WrData[2] = Address[2];
            BIBF(g_I2cWritePtr(g_pContextI2cWrite,CHIP_ADDR, 3 , WrData));

            WrData[0] = 0x02;    //update target address High 0x02FE
            WrData[1] = 0xFE;
            WrData[2] = Address[1];
            BIBF(g_I2cWritePtr(g_pContextI2cWrite,CHIP_ADDR, 3 , WrData));

            WrData[0] = 0x02;    //update Buffer Length.  0x02FF
            WrData[1] = 0xFF;
            WrData[2] = (unsigned char)BytesToProcess -1 ;  // X bytes - 1
            BIBF(g_I2cWritePtr(g_pContextI2cWrite,CHIP_ADDR, 3 , WrData));
#endif
        }

        //
        //  Update buffer.
        //
#ifdef ENABLE_I2C_BURST_MODE
        pAddressByte = (unsigned short*) (pData - cbAddressBytes);
        memcpy(g_Buffer, pAddressByte, BytesToProcess+cbAddressBytes);
        *((unsigned short*)g_Buffer) = RegMemMapAddr;
        ChannelI2cBulkWrite(BytesToProcess+cbAddressBytes, (unsigned char*)g_Buffer);
        pData = pEndData;
#else
        for(offset=0;pData != pEndData;offset++,pData++)
        {
            WrData[0] = 0x03;   //update Buffer [0x0300 - 0x03ff]
            WrData[1] = (unsigned char) offset;
            WrData[2] = *pData;
            BIBF(g_I2cWritePtr(g_pContextI2cWrite,CHIP_ADDR, 3 , WrData));
        }
#endif 

        //
        // Commit the changes and start to transfer buffer to memory.
        //
        if( MemType == MEM_TYPE_RAM)
        {
            cr = 0x81;
        }
        else if( MemType == MEM_TYPE_EEPROM)
        {
            cr = 0x83;
        }
        else if( MemType == MEM_TYPE_SPX)
        {
            cr = 0x85;
            if( bNeedToContinue )
            {
                cr |= 0x08;
            }
        }

        WrData[0] = 0x04;   // UpdateCtl [0x400]
        WrData[1] = 0x00;
        WrData[2] = cr;   // start to transfer  
        BIBF(g_I2cWritePtr(g_pContextI2cWrite,CHIP_ADDR, 3 , WrData));

        for(i = 0;i<MEMORY_UPDATE_TIMEOUT;i++)
        {

            // loop until the writing is done.
            WrData[0] = ReadReg(0x0400);
            if(!( WrData[0] & 0x80 ))
            {
                //done
                break;
            }
            else
            {
                //pending
                if(MemType== MEM_TYPE_EEPROM)
                {   
                    //it needs more time for updating eeprom.
                    msleep(5); // need more waiting  
                }
                else
                {
                    udelay(1);
                }
                continue;
            }
        }

        if( i == MEMORY_UPDATE_TIMEOUT)
        {
            //writing failed.
            LOG( (DBG_ERROR "memory update timeout.\n"));
            ErrNo = -ERRNO_UPDATE_MEMORY_FAILED;

            break;
        }
        if ( i >= 1) 
        {
           printk( KERN_ERR "write pending loop =%d\n", i);
        }	

        bNeedToContinue = 1; 
    }while(0);

    return ErrNo ;
}

#define WAIT_UNTIL_DEVICE_READY(_x_,_msg_) \
for (timeout=0;timeout<dev_ready_time_out;timeout++) \
{                                                    \
    Ready = ReadReg(0x1000);                         \
    if (Ready _x_) break;                       \
    msleep(10);                                      \
};                                                   \
if( timeout == dev_ready_time_out)                   \
{                                                    \
    printk(KERN_ERR _msg_); \
    ErrNo = -ERRNO_DEVICE_OUT_OF_CONTROL;             \
    break;                                           \
}            

unsigned int CxGetFirmwarePatchVersion(void)
{
    unsigned int FwPatchVersion = 0;
    int ErrNo;


    if( NULL == g_I2cWriteThenReadPtr||
        NULL == g_I2cWritePtr)
    {
        ErrNo = -ERRNO_I2CFUN_NOT_SET;
        LOG( (DBG_ERROR "i2C function is not set.\n"));
        return 0;
    }

    FwPatchVersion = ReadReg(0x1584);
    FwPatchVersion <<= 8;
    FwPatchVersion |= ReadReg(0x1585);
    FwPatchVersion <<= 8;
    FwPatchVersion |= ReadReg(0x1586);

    return FwPatchVersion;
}

unsigned int CxGetFirmwareVersion(void)
{
    unsigned int FwVersion = 0;
    int ErrNo;


    if( NULL == g_I2cWriteThenReadPtr||
        NULL == g_I2cWritePtr)
    {
        ErrNo = -ERRNO_I2CFUN_NOT_SET;
        LOG( (DBG_ERROR "i2C function is not set.\n"));
        return 0;
    }

    FwVersion = ReadReg(0x1002);
    FwVersion <<= 8;
    FwVersion |= ReadReg(0x1001);
    FwVersion <<= 8;
    FwVersion |= ReadReg(0x1006);

    return FwVersion;
}

// return number, 0= failed. 1  = successful. 
int DownloadFW(const unsigned char * const pRomBin)
{
    int ErrNo = ERRNO_NOERR;
    struct CX_CODEC_ROM      *pRom  = (struct CX_CODEC_ROM  *)pRomBin;
    struct CX_CODEC_ROM_DATA *pRomData;
    struct CX_CODEC_ROM_DATA *pRomDataEnd;
    unsigned char            *pData;
    unsigned char            *pDataEnd;
    unsigned long            CurAddr = 0;
    unsigned long            cbDataLen = 0;
    unsigned char            Ready;
    unsigned long            curProgress = 0;
    unsigned long            TotalLen    = 0;
    unsigned long            i = 0;
    const unsigned long      dev_ready_time_out = 100;
    int                      bIsRomVersion  = 0;           
    const char               CHAN_PATH[]="CNXT CHANNEL PATCH";    
    unsigned long            timeout;
    unsigned long            fwVer;
    unsigned long            fwPatchVer;

    do{
        if(pRom == NULL ||g_Buffer == NULL)
        {
            ErrNo = -ERRNO_INVALID_PARAMETER;
            LOG( (DBG_ERROR "Invalid parameter.\n"));
            break;
        }

        if( NULL == g_I2cWriteThenReadPtr||
            NULL == g_I2cWritePtr)
        {
            ErrNo = -ERRNO_I2CFUN_NOT_SET;
            LOG( (DBG_ERROR "i2C function is not set.\n"));
            break;
        }
		
        //check if codec is ROM version
        if (0 == memcmp(CHAN_PATH,pRom->sDesc,sizeof(CHAN_PATH)-1)) {
			printk(KERN_INFO "[CNXT] sDesc = %s", pRom->sDesc);
			bIsRomVersion = 1;
        }

        if (bIsRomVersion) {
            WAIT_UNTIL_DEVICE_READY(== 0X01,"cx2070x: Timed out waiting for codecto be ready!\n");
        } else {
            //Check if there is a FIRMWARE present. the Channel should get
            // a clear reset signal before we download firmware to it.
            if( (ReadReg(0x009) & 0x04) == 0) {
                LOG((DBG_ERROR "cx2070x: did not get a clear reset..!"));
                ErrNo = -ERRNO_DEVICE_NOT_RESET;
                break;
            }
		}

        TotalLen = FromBigEndiaULONG(pRom->LoaderLen) + FromBigEndiaULONG(pRom->CtlLen) + FromBigEndiaULONG(pRom->SpxLen);
       // InitShowProgress(TotalLen);

        //Download the loader.
        pRomData    = (struct CX_CODEC_ROM_DATA *) ( (char*)pRom + FromBigEndiaULONG(pRom->LoaderAddr));
        pRomDataEnd = (struct CX_CODEC_ROM_DATA *) ((char*)pRomData +FromBigEndiaULONG(pRom->LoaderLen));

        for( ;pRomData!=pRomDataEnd;)
        {
#ifdef ENABLE_I2C_BURST_MODE
            pData   = &pRomData->data[0];
            pDataEnd= pData + FromBigEndiaULONG(pRomData->Length) - sizeof(unsigned long); 
            memcpy(g_Buffer, pData-2, FromBigEndiaULONG(pRomData->Length) - sizeof(unsigned short));
            BIF(ChannelI2cBulkWrite( FromBigEndiaULONG(pRomData->Length) - sizeof(unsigned short), g_Buffer));
            curProgress +=  FromBigEndiaULONG(pRomData->Length) ;
            ShowProgress(curProgress,false, I2C_OK,TotalLen);

            pRomData = (struct CX_CODEC_ROM_DATA *)pDataEnd;

#else
            CurAddr = FromBigEndiaULONG(pRomData->Address);
            pData   = &pRomData->data[0];
            pDataEnd= pData + FromBigEndiaULONG(pRomData->Length) - sizeof(unsigned long); 
            for( ;pData!=pDataEnd;pData++)
            {
                *((unsigned short*)writeBuf) = ToBigEndiaWORD((unsigned short)CurAddr);
                writeBuf[2]= *pData;
                g_I2cWritePtr(g_pContextI2cWrite,CHIP_ADDR,3, writeBuf);
                CurAddr++;
            }
            pRomData = (struct CX_CODEC_ROM_DATA *)pData;
#endif 

        }

        //* check if the device is ready.
        if (bIsRomVersion) {
            WAIT_UNTIL_DEVICE_READY(== 0X01,"cx2070x: Timed out waiting for cx2070x to be ready after loader downloaded!\n");
        } else { 
            WAIT_UNTIL_DEVICE_READY(!= 0xFF,"cx2070x: Timed out waiting for cx2070x to be ready after loader downloaded!\n");
		}

        //Download the CTL
        pRomData    = (struct CX_CODEC_ROM_DATA *) ( (char*)pRom + FromBigEndiaULONG(pRom->CtlAddr ));
        pRomDataEnd = (struct CX_CODEC_ROM_DATA *) ((char*)pRomData +FromBigEndiaULONG(pRom->CtlLen));

        for( ;pRomData!=pRomDataEnd;)
        {
            CurAddr = FromBigEndiaULONG(pRomData->Address);
            pData       = &pRomData->data[0];
            cbDataLen   = FromBigEndiaULONG(pRomData->Length) ;
            BIF(CxWriteMemory(CurAddr,cbDataLen -sizeof(unsigned long)/*subtracts the address bytes*/ , pData, MEM_TYPE_RAM ));
            // The next RoMData position = current romData position + cbDataLen + sizeof( data len bytes)
            pRomData  =   (struct CX_CODEC_ROM_DATA *)((char*) pRomData + cbDataLen + sizeof(unsigned long));  

            curProgress +=  cbDataLen ;
            ShowProgress(curProgress,false, I2C_OK,TotalLen);
        }

        pRomData    = (struct CX_CODEC_ROM_DATA *) ( (char*)pRom + FromBigEndiaULONG(pRom->SpxAddr ));
        pRomDataEnd = (struct CX_CODEC_ROM_DATA *) ((char*)pRomData +FromBigEndiaULONG(pRom->SpxLen));

        for( ;pRomData!=pRomDataEnd;)
        {
            CurAddr = FromBigEndiaULONG(pRomData->Address);
            pData       = &pRomData->data[0];
            cbDataLen   = FromBigEndiaULONG(pRomData->Length) ;
            BIF(CxWriteMemory(CurAddr,cbDataLen -sizeof(unsigned long)/*subtracts the address bytes*/ , pData, MEM_TYPE_SPX ));
            // The next RoMData position = current romData position + cbDataLen + sizeof( data len bytes)
            pRomData  =   (struct CX_CODEC_ROM_DATA *)((char*) pRomData + cbDataLen + sizeof(unsigned long));  

            curProgress +=  cbDataLen ;
            ShowProgress(curProgress,false, I2C_OK,TotalLen);
        }

        if(ErrNo != 0) break;

        ShowProgress(TotalLen,false, I2C_OK,TotalLen);

        //
        // Reset
        //
        if(bIsRomVersion)
        {
            WriteReg(0x1000,0x00);
         //   msleep(400); //delay 400 ms
        }
        else
        {
            WriteReg(0x400,0x40);
            msleep(400); //delay 400 ms
        }
       
       WAIT_UNTIL_DEVICE_READY(== 0x01,"cx2070x: Timed out waiting for cx2070x to be ready after firmware downloaded!\n");

        //check if XPS code is working or not.

        WriteReg(0x117d,0x01);
        for (timeout=0;timeout<dev_ready_time_out;timeout++) 
        {                                                    
            Ready = ReadReg(0x117d);                         
            if (Ready == 0x00) break;                            
            msleep(1);                                      
        };                                                   
        if( timeout == dev_ready_time_out)                   
        {                                                    
            LOG((DBG_ERROR "cx2070x: DSP lockup! download firmware failed!")); 
            ErrNo = -ERRNO_DEVICE_DSP_LOCKUP;          
            break;                                           
        } 

        fwVer = CxGetFirmwareVersion();
        if(bIsRomVersion)
        {
            fwPatchVer = CxGetFirmwarePatchVersion();
            printk(KERN_INFO "cx2070x: firmware download successfully! FW: %u,%u,%u, FW Patch: %u,%u,%u\n",
                (unsigned char)(fwVer>>16),  
                (unsigned char)(fwVer>>8),  
                (unsigned char)fwVer,
                (unsigned char)(fwPatchVer>>16),  
                (unsigned char)(fwPatchVer>>8),  
                (unsigned char)fwPatchVer);
        }
        else
        {
             printk(KERN_INFO "cx2070x: firmware download successfully! FW: %u,%u,%u\n",
                (unsigned char)(fwVer>>16),  
                (unsigned char)(fwVer>>8),  
                (unsigned char)fwVer);
        }

    }while(0);

    return ErrNo;
}

int ApplyDSPChanges(const unsigned char *const pRom)
{
    int ErrNo = ERRNO_NOERR;
    struct CX_CODEC_ROM* pNewRom ;
    struct CX_CODEC_ROM_APPENDED *pRomAppended;
    struct CX_CODEC_ROM_APPENDED_INFO *pInfo;
    struct CX_CODEC_APPENDED_DATA    *pData;
    struct CX_CODEC_APPENDED_DATA    *pDataEnd;
    unsigned short                    wRegAddr;
    unsigned char                     NewC;

#define DESC_LEN   (16)
    char szJira[DESC_LEN+1];
    char szDate[DESC_LEN+1];

    pNewRom = (struct CX_CODEC_ROM*) pRom;

    // check if firmware contains DSP tuning data.
    if( (FromBigEndiaULONG(pNewRom->SpxLen) + FromBigEndiaULONG(pNewRom->SpxAddr)) != FromBigEndiaULONG(pNewRom->FileSize) ) 
    {
      // has DSP Tuning data.

        pRomAppended = (struct CX_CODEC_ROM_APPENDED*)((char*)pNewRom + FromBigEndiaULONG(pNewRom->SpxAddr) + FromBigEndiaULONG(pNewRom->SpxLen));
        pInfo = (struct CX_CODEC_ROM_APPENDED_INFO*) ((char*)pNewRom + FromBigEndiaULONG(pRomAppended ->TuningAddr) + 
            FromBigEndiaULONG(pRomAppended ->TuningLen));

        strncpy(szJira,pInfo->sJIRAID,DESC_LEN);
        strncpy(szDate,pInfo->sTime,DESC_LEN);
        szJira[DESC_LEN]=0;
        szDate[DESC_LEN-1]=0; //remove the last lettle $.
        printk(KERN_INFO "Applying the DSP tuning changes..Jira: %s Date: %s\n"
							,szJira,szDate);

        pData     = pRomAppended->data; 
        pDataEnd  = (struct CX_CODEC_APPENDED_DATA*)((char*)pData + FromBigEndiaULONG(pRomAppended->TuningLen));
        for(;pData != pDataEnd; pData++)
        {
            wRegAddr = pData->Address[0];
            wRegAddr <<=8;
            wRegAddr |= pData->Address[1];
            WriteReg(wRegAddr,pData->data);
            //printk(KERN_INFO "0X%04x=0x%02x\n",wRegAddr,pData->data);
        }
        // re-set NewC.
        NewC = ReadReg(0x117d); 
        WriteReg(0x117d,NewC|1);
    }
    
    return ErrNo;
}

