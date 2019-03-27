/*******************************************************************************
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* $FreeBSD$
*
********************************************************************************/
/*******************************************************************************/
/*! \file sa_err.h
 *  \brief The file defines the error code constants, defined by LL API
 *
 *
 */
/******************************************************************************/

#ifndef  __SA_ERR_H__
#define __SA_ERR_H__

/************************************************************************************
 *                                                                                  *
 *               Error Code Constants defined for LL Layer starts                   *
 *                                                                                  *
 ************************************************************************************/

/***********************************************************************************
 *                    SSP/SMP/SATA IO Completion Status values
 ***********************************************************************************/

#define OSSA_IO_SUCCESS                                       0x00   /**< IO completes successfully */
#define OSSA_IO_ABORTED                                       0x01   /**< IO aborted */
#define OSSA_IO_OVERFLOW                                      0x02   /**< IO overflowed (SSP) */
#define OSSA_IO_UNDERFLOW                                     0x03   /**< IO underflowed (SSP) */
#define OSSA_IO_FAILED                                        0x04   /**< IO failed */
#define OSSA_IO_ABORT_RESET                                   0x05   /**< IO abort because of reset */
#define OSSA_IO_NOT_VALID                                     0x06   /**< IO not valid */
#define OSSA_IO_NO_DEVICE                                     0x07   /**< IO is for non-existing device */
#define OSSA_IO_ILLEGAL_PARAMETER                             0x08   /**< IO is not supported (SSP) */
/* The following two error codes 0x09 and 0x0A are not using */
#define OSSA_IO_LINK_FAILURE                                  0x09   /**< IO failed because of link failure (SMP) */
#define OSSA_IO_PROG_ERROR                                    0x0A   /**< IO failed because of program error (SMP) */

#define OSSA_IO_DIF_IN_ERROR                                  0x0B   /**< IO failed inbound DIF error (SSP) */
#define OSSA_IO_DIF_OUT_ERROR                                 0x0C   /**< IO failed outbound DIF error (SSP) */
#define OSSA_IO_ERROR_HW_TIMEOUT                              0x0D   /**< SMP request/response failed due to HW timeout  (SMP) */
#define OSSA_IO_XFER_ERROR_BREAK                              0x0E   /**< IO aborted due to BREAK during connection */
#define OSSA_IO_XFER_ERROR_PHY_NOT_READY                      0x0F   /**< IO aborted due to PHY NOT READY during connection*/
#define OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED         0x10   /**< Open connection error */
#define OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION                 0x11   /**< Open connection error */
#define OSSA_IO_OPEN_CNX_ERROR_BREAK                          0x12   /**< Open connection error */
#define OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS                  0x13   /**< Open connection error */
#define OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION                0x14   /**< Open connection error */
#define OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED  0x15   /**< Open connection error */
#define OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY             0x16   /**< Open connection error */
#define OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION              0x17   /**< Open connection error */
/* This error code 0x18 is not used on SPCv */
#define OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR                  0x18   /**< Open connection error */
#define OSSA_IO_XFER_ERROR_NAK_RECEIVED                       0x19   /**< IO aborted due to transfer error with data NAK received*/
#define OSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT                    0x1A   /**< IO aborted due to transfer error with data ACK/NAK timeout*/
#define OSSA_IO_XFER_ERROR_PEER_ABORTED                       0x1B
#define OSSA_IO_XFER_ERROR_RX_FRAME                           0x1C
#define OSSA_IO_XFER_ERROR_DMA                                0x1D
#define OSSA_IO_XFER_ERROR_CREDIT_TIMEOUT                     0x1E   /**< IO aborted due to CREDIT TIMEOUT during data transfer*/
#define OSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT                  0x1F
#define OSSA_IO_XFER_ERROR_SATA                               0x20

/* This error code 0x22 is not used on SPCv */
#define OSSA_IO_XFER_ERROR_ABORTED_DUE_TO_SRST                0x22
#define OSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE                  0x21
#define OSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE                   0x23
#define OSSA_IO_XFER_OPEN_RETRY_TIMEOUT                       0x24   /**< IO OPEN_RETRY_TIMEOUT */
/* This error code 0x25 is not used on SPCv */
#define OSSA_IO_XFER_SMP_RESP_CONNECTION_ERROR                0x25
#define OSSA_IO_XFER_ERROR_UNEXPECTED_PHASE                   0x26
#define OSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN                   0x27
#define OSSA_IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED              0x28

#define OSSA_IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT          0x30
/* The following error code 0x31 and 0x32 are not using (obsolete) */
#define OSSA_IO_XFER_ERROR_CMD_ISSUE_BREAK_BEFORE_ACK_NAK     0x31
#define OSSA_IO_XFER_ERROR_CMD_ISSUE_PHY_DOWN_BEFORE_ACK_NAK  0x32

#define OSSA_IO_XFER_ERROR_OFFSET_MISMATCH                    0x34
#define OSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN                 0x35
#define OSSA_IO_XFER_CMD_FRAME_ISSUED                         0x36
#define OSSA_IO_ERROR_INTERNAL_SMP_RESOURCE                   0x37
#define OSSA_IO_PORT_IN_RESET                                 0x38
#define OSSA_IO_DS_NON_OPERATIONAL                            0x39
#define OSSA_IO_DS_IN_RECOVERY                                0x3A
#define OSSA_IO_TM_TAG_NOT_FOUND                              0x3B
#define OSSA_IO_XFER_PIO_SETUP_ERROR                          0x3C
#define OSSA_IO_SSP_EXT_IU_ZERO_LEN_ERROR                     0x3D
#define OSSA_IO_DS_IN_ERROR                                   0x3E
#define OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY               0x3F
#define OSSA_IO_ABORT_IN_PROGRESS                             0x40
#define OSSA_IO_ABORT_DELAYED                                 0x41
#define OSSA_IO_INVALID_LENGTH                                0x42
#define OSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY_ALT           0x43
#define OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED     0x44
#define OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO         0x45
#define OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST          0x46
#define OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE     0x47
#define OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED  0x48
#define OSSA_IO_DS_INVALID                                    0x49

#define OSSA_IO_XFER_READ_COMPL_ERR                           0x0050
/* WARNING: the value is not contiguous from here */
#define OSSA_IO_XFER_ERR_LAST_PIO_DATAIN_CRC_ERR              0x0052
#define OSSA_IO_XFER_ERROR_DMA_ACTIVATE_TIMEOUT               0x0053
#define OSSA_IO_XFR_ERROR_INTERNAL_CRC_ERROR                  0x0054
#define OSSA_MPI_IO_RQE_BUSY_FULL                             0x0055
#define OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN                     0x0056   /* This status is only for Hitach FW */
#define OSSA_IO_XFR_ERROR_INVALID_SSP_RSP_FRAME               0x0057
#define OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED                 0x0058

#define OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE                  0x1004

/*encrypt saSetOperator() response status */
#define OSSA_MPI_ENC_ERR_CONTROLLER_NOT_IDLE                  0x1005
#define OSSA_MPI_ENC_NVM_MEM_ACCESS_ERR                       0x100B

#ifdef SA_TESTBASE_EXTRA
/* TestBase */
#define OSSA_IO_HOST_BST_INVALID                              0x1005
#endif /*  SA_TESTBASE_EXTRA */


#define OSSA_MPI_ERR_OFFLOAD_RESOURCE_UNAVAILABLE             0x1012
#define OSSA_MPI_ERR_OFFLOAD_DIF_OR_ENC_NOT_ENABLED           0x1013
#define OSSA_MPI_ERR_ATAPI_DEVICE_BUSY                        0x1024

/* Specifies the status of the PHY_START command */
#define OSSA_MPI_IO_SUCCESS                 0x00000000  /* PhyStart operation completed successfully */
/* Specifies the status of the PHY_STOP command */
#define OSSA_MPI_ERR_DEVICES_ATTACHED               0x00001046  /* All the devices in a port need to be deregistered if the PHY_STOP is for the last phy. */
#define OSSA_MPI_ERR_INVALID_PHY_ID                 0x00001061  /* identifier specified in the PHY_START command is invalid i.e out of supported range for this product. */
#define OSSA_MPI_ERR_PHY_ALREADY_STARTED            0x00001063  /* An attempt to start a phy which is already started.  */
#define OSSA_MPI_ERR_PHY_NOT_STARTED                0x00001064  /* An attempt to stop a phy which is not started */
#define OSSA_MPI_ERR_PHY_SUBOP_NOT_SUPPORTED        0x00001065  /* An attempt to use a sub operation that is not supported */

#define OSSA_MPI_ERR_INVALID_ANALOG_TBL_IDX         0x00001067  /* The Analog Setup Table Index used in the PHY_START command in invalid. */
#define OSSA_MPI_ERR_PHY_PROFILE_PAGE_NOT_SUPPORTED 0x00001068 /* Unsupported profile page code specified in the GET_PHY_PROFILE Command */
#define OSSA_MPI_ERR_PHY_PROFILE_PAGE_NOT_FOUND     0x00001069 /* Unsupported profile page code specified in the GET_PHY_PROFILE Command */

#define OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS                  0x2040
/*
   An encryption IO request failed due to DEK Key Tag mismatch.
   The key tag supplied in the encryption IOMB does not match with the Key Tag in the referenced DEK Entry.
*/
#define OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH                0x2041
#define OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID                 0x2042
/*
    An encryption I/O request failed
    because the initial value (IV) in the unwrapped DEK blob didn't match the IV used to unwrap it.
*/
#define OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH                     0x2043
/* An encryption I/O request failed due to an internal RAM ECC or interface error while unwrapping the DEK. */
#define OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR             0x2044
/* An encryption I/O request failed due to an internal RAM ECC or interface error while unwrapping the DEK. */
#define OSSA_IO_XFR_ERROR_INTERNAL_RAM                        0x2045
/*
    An encryption I/O request failed
    because the DEK index specified in the I/O was outside the bounds of thetotal number of entries in the host DEK table.
*/
#define OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS             0x2046
#define OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE                   0x2047

#define OSSA_MPI_ENC_ERR_UNSUPPORTED_OPTION                   0x2080
#define OSSA_MPI_ENC_ERR_ID_TRANSFER_FAILURE                  0x2081

#define OSSA_MPI_ENC_OPERATOR_AUTH_FAILURE                    0x2090
#define OSSA_MPI_ENC_OPERATOR_OPERATOR_ALREADY_LOGGED_IN      0x2091
#define OSSA_MPI_ENC_OPERATOR_ILLEGAL_PARAMETER               0x2092

/* define DIF IO response error status code */
#define OSSA_IO_XFR_ERROR_DIF_MISMATCH                        0x3000
#define OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH        0x3001
#define OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH          0x3002
#define OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH                    0x3003
#define OSSA_IO_XFER_ERROR_DIF_INTERNAL_ERROR                 0x3004

#define OSSA_MPI_ERR_DIF_IS_NOT_ENABLED                                   /* Indicates that saPCIeDiagExecute() is
                                                                          *  called with DIF but DIF is not enabled.
                                                                          */
/* define operator management response status and error qualifier code */
#define OPR_MGMT_OP_NOT_SUPPORTED                             0x2060
#define OPR_MGMT_MPI_ENC_ERR_OPR_PARAM_ILLEGAL                0x2061
#define OPR_MGMT_MPI_ENC_ERR_OPR_ID_NOT_FOUND                 0x2062
#define OPR_MGMT_MPI_ENC_ERR_OPR_ROLE_NOT_MATCH               0x2063
#define OPR_MGMT_MPI_ENC_ERR_OPR_MAX_NUM_EXCEEDED             0x2064
#define OPR_MGMT_MPI_ENC_ERR_KEK_UNWRAP_FAIL                  0x2022
#define OPR_MGMT_MPI_ENC_ERR_NVRAM_OPERATION_FAILURE          0x2023

/* When Status is 0x2061 */
#define OPR_MGMT_ERR_QLFR_ILLEGAL_AUTHENTICATIONKEK_INDEX     0x1
#define OPR_MGMT_ERR_QLFR_ILLEGAL_OPERATOR                    0x2
#define OPR_MGMT_ERR_QLFR_ILLEGAL_KEK_FORMAT                  0x3
#define OPR_MGMT_ERR_QLFR_WRONG_ROLE                          0x4

/* When status is 0x2090 */
/* invalid certificate: the certificate can not be unwrapped successfully by existing operators's KEKs */
#define OPR_SET_ERR_QLFR_INVALID_CERT                         0x01
/* role mismatch: the role from the certificate doesn't match the one inside the controller. */
#define OPR_SET_ERR_QLFR_ROLE_MISMATCH                        0x02
/* ID mismatch: the ID string from the certificate doesn't match the one inside the controller. */
#define OPR_SET_ERR_QLFR_ID_MISMATCH                          0x03
/* When status is 0x2092 */
/* invalid OPRIDX */
#define OPR_SET_ERR_QLFR_INVALID_OPRIDX                       0x04
/* invalid access type */
#define OPR_SET_ERR_QLFR_INVALID_ACCESS_TYPE                  0x05

/* WARNING: This error code must always be the last number.
 *          If you add error code, modify this code also
 *          It is used as an index
 */

/* SAS Reconfiguration error */
#define OSSA_CONTROLLER_NOT_IDLE                    0x1
#define OSSA_INVALID_CONFIG_PARAM                   0x2


/************************************************************************************
 *                                                                                  *
 *               Constants defined for OS Layer ends                                *
 *                                                                                  *
 ************************************************************************************/

#endif  /*__SA_ERR_H__ */
