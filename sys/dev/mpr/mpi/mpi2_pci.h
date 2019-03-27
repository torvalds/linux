/*-
 *  Copyright 2000-2020 Broadcom Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Broadcom Inc. (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

/*
 *  Copyright 2000-2020 Broadcom Inc. All rights reserved.
 *
 *
 *           Name:  mpi2_pci.h
 *          Title:  MPI PCIe Attached Devices structures and definitions.
 *  Creation Date:  October 9, 2012
 *
 *  mpi2_pci.h Version:  02.00.03
 *
 *  NOTE: Names (typedefs, defines, etc.) beginning with an MPI25 or Mpi25
 *        prefix are for use only on MPI v2.5 products, and must not be used
 *        with MPI v2.0 products. Unless otherwise noted, names beginning with
 *        MPI2 or Mpi2 are for use with both MPI v2.0 and MPI v2.5 products.
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  03-16-15  02.00.00  Initial version.
 *  02-17-16  02.00.01  Removed AHCI support.
 *                      Removed SOP support.
 *  07-01-16  02.00.02  Added MPI26_NVME_FLAGS_FORCE_ADMIN_ERR_RESP to
 *                      NVME Encapsulated Request.
 *  07-22-18  02.00.03  Updted flags field for NVME Encapsulated req
 *  --------------------------------------------------------------------------
 */

#ifndef MPI2_PCI_H
#define MPI2_PCI_H


/*
 * Values for the PCIe DeviceInfo field used in PCIe Device Status Change Event
 * data and PCIe Configuration pages.
 */
#define MPI26_PCIE_DEVINFO_DIRECT_ATTACH        (0x00000010)

#define MPI26_PCIE_DEVINFO_MASK_DEVICE_TYPE     (0x0000000F)
#define MPI26_PCIE_DEVINFO_NO_DEVICE            (0x00000000)
#define MPI26_PCIE_DEVINFO_PCI_SWITCH           (0x00000001)
#define MPI26_PCIE_DEVINFO_NVME                 (0x00000003)


/****************************************************************************
*  NVMe Encapsulated message
****************************************************************************/

/* NVME Encapsulated Request Message */
typedef struct _MPI26_NVME_ENCAPSULATED_REQUEST
{
    U16                     DevHandle;                      /* 0x00 */
    U8                      ChainOffset;                    /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     EncapsulatedCommandLength;      /* 0x04 */
    U8                      Reserved1;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved2;                      /* 0x0A */
    U32                     Reserved3;                      /* 0x0C */
    U64                     ErrorResponseBaseAddress;       /* 0x10 */
    U16                     ErrorResponseAllocationLength;  /* 0x18 */
    U16                     Flags;                          /* 0x1A */
    U32                     DataLength;                     /* 0x1C */
    U8                      NVMe_Command[4];                /* 0x20 */ /* variable length */

} MPI26_NVME_ENCAPSULATED_REQUEST, MPI2_POINTER PTR_MPI26_NVME_ENCAPSULATED_REQUEST,
  Mpi26NVMeEncapsulatedRequest_t, MPI2_POINTER pMpi26NVMeEncapsulatedRequest_t;

/* defines for the Flags field */
#define MPI26_NVME_FLAGS_FORCE_ADMIN_ERR_RESP       (0x0020)
/* Submission Queue Type*/
#define MPI26_NVME_FLAGS_SUBMISSIONQ_MASK           (0x0010)
#define MPI26_NVME_FLAGS_SUBMISSIONQ_IO             (0x0000)
#define MPI26_NVME_FLAGS_SUBMISSIONQ_ADMIN          (0x0010)
/* Error Response Address Space */
#define MPI26_NVME_FLAGS_MASK_ERROR_RSP_ADDR        (0x000C)
#define MPI26_NVME_FLAGS_MASK_ERROR_RSP_ADDR_MASK   (0x000C)
#define MPI26_NVME_FLAGS_SYSTEM_RSP_ADDR            (0x0000)
#define MPI26_NVME_FLAGS_IOCCTL_RSP_ADDR            (0x0008)
/* Data Direction*/
#define MPI26_NVME_FLAGS_DATADIRECTION_MASK         (0x0003)
#define MPI26_NVME_FLAGS_NODATATRANSFER             (0x0000)
#define MPI26_NVME_FLAGS_WRITE                      (0x0001)
#define MPI26_NVME_FLAGS_READ                       (0x0002)
#define MPI26_NVME_FLAGS_BIDIRECTIONAL              (0x0003)


/* NVMe Encapuslated Reply Message */
typedef struct _MPI26_NVME_ENCAPSULATED_ERROR_REPLY
{
    U16                     DevHandle;                      /* 0x00 */
    U8                      MsgLength;                      /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     EncapsulatedCommandLength;      /* 0x04 */
    U8                      Reserved1;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved2;                      /* 0x0A */
    U16                     Reserved3;                      /* 0x0C */
    U16                     IOCStatus;                      /* 0x0E */
    U32                     IOCLogInfo;                     /* 0x10 */
    U16                     ErrorResponseCount;             /* 0x14 */
    U16                     Reserved4;                      /* 0x16 */
} MPI26_NVME_ENCAPSULATED_ERROR_REPLY,
  MPI2_POINTER PTR_MPI26_NVME_ENCAPSULATED_ERROR_REPLY,
  Mpi26NVMeEncapsulatedErrorReply_t,
  MPI2_POINTER pMpi26NVMeEncapsulatedErrorReply_t;


#endif


