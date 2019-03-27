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

#ifndef CONFIG_H
#define CONFIG_H

#define	LINUX_PERBI_SUPPORT
#define	HIALEAH_ENCRYPTION
#define	HOTPLUG_SUPPORT
#define	AG_CPU_LITTLE_ENDIAN
#define	INITIATOR_DRIVER
#define	AGTIAPI_SA
#define	AGTIAPI_EVENT_LOG
// #define	AGTIAPI_DPC
#define	AGTIAPI_SA
#define	PMC_SPC
#define	SPC_MSIX_INTR
#define	SPC_INT_ENABLE
#define	AGTIAPI_LOCAL_LOCK
#define	AGTIAPI_LOCAL_RESET
#define	MU_I2O_DISABLE
#define	HOST_SAST_ENABLE
#define	HOST_PM2_ENABLE
#define	TD_DISCOVER
#define	SA_ENABLE_HDA_FUNCTIONS
// #define	SA_FW_TEST_INTERRUPT_REASSERT
// #define	SALLSDK_DEBUG
// #define	AGTIAPI_DEBUG
// #define	TD_DEBUG_ENABLE
// #define	AGTIAPI_IO_DEBUG
// #define	AGTIAPI_FLOW_DEBUG
// #define	AGTIAPI_INIT_DEBUG
#define	PMC_PM8001_BAR64KB
// #define	DM_DEBUG
#define	FDS_DM
// #define	SM_DEBUG
#define	FDS_SM
#define	SATA_ENABLE
#define	CHAR_DEVICE
#define	TD_4GB_WORKAROUND

#endif  /* CONFIG_H */
