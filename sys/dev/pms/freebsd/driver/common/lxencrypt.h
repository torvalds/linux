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
*******************************************************************************/
/*******************************************************************************
**
** Version Control Information:
**
**  $Revision: 114125 $
**  $Author: lindfors $
**  $Date: 2012-01-06 17:12:27 -0800 (Fri, 06 Jan 2012) $
**  $Id: lxencrypt.h 112360 2012-01-07 01:12:27Z mcleanda $
**
*******************************************************************************/

//#ifndef __LXENCRYPT_H__
//#define __LXENCRYPT_H__


#include <dev/pms/RefTisa/tisa/api/tiapi.h>
#include <dev/pms/RefTisa/tisa/api/ostiapi.h>
#include <dev/pms/RefTisa/tisa/api/tidefs.h>
#include <dev/pms/RefTisa/tisa/api/tiglobal.h>
#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdioctl.h>
#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>
#include <sys/param.h>		//zone allocation
#include <sys/queue.h>		//zone allocation
#include <vm/uma.h>		//zone allocation


#ifndef __LXENCRYPT_H__
#define __LXENCRYPT_H__

#define IOERR_QUEUE_DEPTH_MAX 1024

enum {
    E_SUCCESS = 0,
    E_DEK_INDEX,
    E_DEK_TABLE,
    E_KEK_INDEX,
    E_CHANNEL_INDEX,
    E_DEVICE_INDEX,
    E_LUN_INDEX,
    E_LBA_RANGE,
    E_MEMPOOL_ALLOC,
    E_FLAGS,
    E_ENCRYPTION_DISABLED,
    E_BAD_CIPHER_MODE,
    E_NO_TARGET_MAP,
    E_NO_LBA_MAP,
    E_NOT_FOUND,
} encrypt_error_e;

typedef struct ag_encrypt_ioerr_s {
    struct list_head *list;
} ag_encrypt_ioerr_t;

struct agtiapi_softc *pCard;	
#ifdef ENCRYPT_ENHANCE


ssize_t set_dek_table_entry0(struct device *dev, struct device_attribute *attr, const char *buf, size_t len);
ssize_t show_dek_table_entry0(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t set_dek_table_entry1(struct device *dev, struct device_attribute *attr, const char *buf, size_t len);
ssize_t show_dek_table_entry1(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t show_kek_table(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t show_dek_kek_map0(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t show_dek_kek_map1(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t show_target_dek_map(struct device *dev, struct device_attribute *attr, char *buf);

#endif
int agtiapi_SetupEncryption(struct agtiapi_softc *pCard);
int agtiapi_SetupEncryptionPools(struct agtiapi_softc *pCard);
void agtiapi_CleanupEncryption(struct agtiapi_softc *pCard);
void agtiapi_CleanupEncryptionPools(struct agtiapi_softc *pCard);
int agtiapi_SetupEncryptedIO(struct agtiapi_softc *pCard, ccb_t *pccb, unsigned long long block);
void agtiapi_CleanupEncryptedIO(struct agtiapi_softc *pCard, ccb_t *pccb);
void agtiapi_HandleEncryptedIOFailure(ag_device_t *pDev, ccb_t *pccb);

#endif

