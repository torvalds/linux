/****************************************************************************************
*****************************************************************************************
***                                                                                   ***
***                                 Copyright (c) 2011                                ***
***                                                                                   ***
***                                Conexant Systems, Inc.                             ***
***                                                                                   ***
***                                 All Rights Reserved                               ***
***                                                                                   ***
***                                    CONFIDENTIAL                                   ***
***                                                                                   ***
***               NO DISSEMINATION OR USE WITHOUT PRIOR WRITTEN PERMISSION            ***
***                                                                                   ***
*****************************************************************************************
**
**  File Name:
**      pump.c
**
**  Abstract:
**      This code is to download the firmware to CX20709 device via I2C bus. 
**      
**
**  Product Name:
**      Conexant Channel CX20709
**
**  Remark:
**      
**
** 
********************************************************************************
**  Revision History
**      Date        Description                                 Author
**      01/21/11    Created.                                    Simon Ho
**      01/24/11    Speed up the firmware download by sending   Simon Ho
**                  I2C data continually without  addressing    
********************************************************************************
*****************************************************************************************/
#ifdef __cplusplus
extern "C"{
#endif 


typedef int (*fun_I2cWriteThenRead)(  void * pCallbackContext,
                                      unsigned char ChipAddr, 
                                      unsigned long cbBuf,
                                      unsigned char* pBuf,
                                      unsigned long cbReadBuf, 
                                      unsigned char*pReadBuf);

typedef int (*fun_I2cWrite)(  void * pCallbackContext,
                              unsigned char ChipAddr,
                              unsigned long cbBuf, 
                              unsigned char* pBuf);

/*
 * Set the I2cWrite callback function.
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
 *    cbMaxWriteBufSize [in] - Specify the maximux transfer size for a I2c continue 
 *                            writing with 'STOP'. This is limited in I2C bus Master
 *                            device. The size can not less then 3 since Channel 
 *                            requires 2 address bytes plus a data byte.
 *                              
 *
 *
 * RETURN
 *      None
 *
 */
void SetupI2cWriteCallback( void * pCallbackContext,
                            fun_I2cWrite         I2cWritePtr,
                            unsigned long        cbMaxWriteBufSize);


/*
 * Set the SetupI2cWriteThenRead callback function.
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
 *  
 *    If the operation completes successfully, the return value is ERRNO_NOERR.
 *    Otherwise, return ERRON_* error code. 
 *
 */
void SetupI2cWriteThenReadCallback( void * pCallbackContext,
                fun_I2cWriteThenRead I2cWriteThenReadPtr);


void SetupMemoryBuffer(void * pAllocedMemoryBuffer);


/*
 * Download Firmware to Channel.
 * 
 * PARAMETERS
 *  
 *    pRomData            [in] - A pointer fo the input buffer that contains rom data.
 *
 * RETURN
 *  
 *    If the operation completes successfully, the return value is ERRNO_NOERR.
 *    Otherwise, return ERRON_* error code. 
 * 
 * REMARKS
 *  
 *    You need to set up both I2cWrite and I2cWriteThenRead callback function by calling 
 *    SetupI2cWriteCallback and SetupI2cWriteThenReadCallback before you call this function.
 */
int DownloadFW(const unsigned char *const pRomData);

/*
 * Apply the extra DSP changes from FW file.
 * 
 * PARAMETERS
 *  
 *    pRomData            [in] - A pointer fo the input buffer that contains rom data.
 *
 * RETURN
 *  
 *    If the operation completes successfully, the return value is ERRNO_NOERR.
 *    Otherwise, return ERRON_* error code. 
 * 
 * REMARKS
 *  
 *    You need to set up both I2C/SPI Write and I2C/SPI WriteThenRead callback function 
 *    by calling SetupI2cSpiWriteCallback and SetupI2cSpiWriteThenReadCallback before you call 
 *    this function.
 */
int ApplyDSPChanges(const unsigned char *const pRom);

#ifdef __cplusplus
}
#endif 


/*Error codes*/
#define ERRNO_NOERR                 0
#define ERRNO_SRC_FILE_NOT_EXIST    101
#define ERRNO_WRITE_FILE_FAILED     102
#define ERRNO_INVALID_DATA          103
#define ERRNO_CHECKSUM_FAILED       104
#define ERRNO_FAILED                105
#define ERRNO_INVALID_PARAMETER     106
#define ERRNO_NOMEM                 107
#define ERRNO_I2CFUN_NOT_SET        108
#define ERRNO_UPDATE_MEMORY_FAILED  109
#define ERRNO_DEVICE_NOT_RESET      110
#define ERRNO_DEVICE_OUT_OF_CONTROL 111
#define ERRNO_DEVICE_DSP_LOCKUP     112


