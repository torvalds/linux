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
**  $Revision: 113920 $
**  $Author: mcleanda $
**  $Date: 2012-05-08 11:30:44 -0700 (Tue, 08 May 2012) $
**  $Id: lxencrypt.c 113920 2012-05-08 18:30:44Z mcleanda $
**
*******************************************************************************/

#include <dev/pms/RefTisa/tisa/sassata/common/tdioctl.h>
#include <dev/pms/RefTisa/tisa/api/titypes.h>

#include <dev/pms/freebsd/driver/common/lxencrypt.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <vm/uma.h>


#ifdef ENCRYPT_ENHANCE
static atomic_t ioerr_queue_count;
/******************************************************************************
careful_write():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
static int
careful_write(char *buf, int offset, int max, const char *fmt, ...)
{
    static char s[PAGE_SIZE]; /* Assumes serialization */
    va_list args;
    int i;

    if(offset > max)
        return 0;
    s[PAGE_SIZE - 1] = '\0';

    va_start(args, fmt);    
    i = vsnprintf(s, PAGE_SIZE - 1, fmt, args);
    if((offset + i) > max) 
        return 0;
    memcpy(buf + offset, s, i); 
    va_end(args);

    return i;
}

/******************************************************************************
set_dek_table_entry():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
static inline int
set_dek_table_entry(struct device *dev, const char *buf, size_t len, dek_table_e table)
{
    int index;
    struct Scsi_Host *shost = class_to_shost(dev);
    struct agtiapi_softc *pCard = (struct agtiapi_softc *) shost->hostdata;

    /* Check permissions */
    if(!capable(CAP_SYS_ADMIN))
        return -EACCES;

    if(!pCard->encrypt)
        return -EINVAL;

    if(table != DEK_TABLE_0 && table != DEK_TABLE_1)
        return -EINVAL;

    sscanf(buf, "%d", &index);
    if(index >= 0 && index < DEK_MAX_TABLE_ITEMS) { 
        pCard->dek_index[table] = index;
        return strlen(buf);
    }
    return -EINVAL;
}

/******************************************************************************
set_dek_table_entry0():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
ssize_t
set_dek_table_entry0(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
    return set_dek_table_entry(dev, buf, len, DEK_TABLE_0);
}

/******************************************************************************
set_dek_table_entry1():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
ssize_t
set_dek_table_entry1(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
    return set_dek_table_entry(dev, buf, len, DEK_TABLE_1);
}


/******************************************************************************
show_dek_table_entry():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
static inline int
show_dek_table_entry(struct device *dev, char *buf, unsigned int table)
{
    int i = 0, j;
    unsigned char *p;
    struct Scsi_Host *sh = class_to_shost(dev);
    ag_card_t *pCard = (ag_card_t *) sh->hostdata;
    ag_card_info_t *pCardInfo = pCard->pCardInfo;
    ag_resource_info_t *pRscInfo = &pCardInfo->tiRscInfo;
    tiEncryptDekBlob_t *pDekTable = NULL;

    if(!pCard->encrypt)
        return -EINVAL;

    if(table == DEK_TABLE_0)
        pDekTable = pRscInfo->tiLoLevelResource.loLevelMem.mem[DEK_MEM_INDEX_1].virtPtr;
    else if(table == DEK_TABLE_1)
        pDekTable = pRscInfo->tiLoLevelResource.loLevelMem.mem[DEK_MEM_INDEX_2].virtPtr;
    if(pDekTable == NULL)
        return -EINVAL;

    if(pCard->dek_index[table] >= 0 || pCard->dek_index[table] < DEK_MAX_TABLE_ITEMS) {
        i += careful_write(buf, i, PAGE_SIZE, "%4d: ", pCard->dek_index[table]);
        p = (unsigned char *) &pDekTable[pCard->dek_index[table]];
        for(j = 0; j < sizeof(tiEncryptDekBlob_t); j++) {
            i += careful_write(buf, i, PAGE_SIZE, "%02x", p[j]);
        }
        i += careful_write(buf, i, PAGE_SIZE, "\n");
    } else {
        i += careful_write(buf, i, PAGE_SIZE, "Bad DEK index %d; range: 0 - %d\n", pCard->dek_index[table], DEK_MAX_TABLE_ITEMS);
    }

    /* BUG if we return more than a single page of data */
    //BUG_ON(i > PAGE_SIZE);
    if (i > PAGE_SIZE)
        i = PAGE_SIZE;

    return i;
}

/******************************************************************************
show_dek_table_entry0():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
ssize_t
show_dek_table_entry0(struct device *dev, struct device_attribute *attr, char *buf)
{
    return show_dek_table_entry(dev, buf, DEK_TABLE_0);
}

/******************************************************************************
show_dek_table_entry1():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
ssize_t
show_dek_table_entry1(struct device *dev, struct device_attribute *attr, char *buf)
{
    return show_dek_table_entry(dev, buf, DEK_TABLE_1);
}

/******************************************************************************
show_kek_table():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
ssize_t
show_kek_table(struct device *dev, struct device_attribute *attr, char *buf)
{
    int i = 0, j, kek_index;
    unsigned char *p;
    struct Scsi_Host *sh = class_to_shost(dev);
    ag_card_t *pCard = (ag_card_t *) sh->hostdata;

    if(!pCard->encrypt)
        return -EINVAL;

    for(kek_index = 0; kek_index < KEK_TABLE_MAX_ENTRY; kek_index++) {
        i += careful_write(buf, i, PAGE_SIZE, " %4d: %08x ", kek_index, pCard->kek_table[kek_index].wrapperIndex); 
        p = (unsigned char *) &pCard->kek_table[kek_index].kekBlob;
        for(j = 0; j < sizeof(tiEncryptKekBlob_t); j++) {
            i += careful_write(buf, i, PAGE_SIZE, "%02x", p[j]);
        }
        i += careful_write(buf, i, PAGE_SIZE, "\n");
    }
    i += careful_write(buf, i, PAGE_SIZE, "\n");

    /* BUG if we return more than a single page of data */
    //BUG_ON(i > PAGE_SIZE);
    if (i > PAGE_SIZE)
        i = PAGE_SIZE;

    return i;
}

/******************************************************************************
show_dek_kek_map():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
static inline int
show_dek_kek_map(struct device *dev, char *buf, unsigned int table)
{
    int i = 0, dek_index;
    struct Scsi_Host *sh = class_to_shost(dev);
    ag_card_t *pCard = (ag_card_t *) sh->hostdata;

    if(!pCard->encrypt)
        return -EINVAL;

    if(table != DEK_TABLE_0 && table != DEK_TABLE_1)
        return -EINVAL;

    i += careful_write(buf, i, PAGE_SIZE, "Table %d\n", table);
    i += careful_write(buf, i, PAGE_SIZE, "=======\n");
    for(dek_index = 0; dek_index < DEK_MAX_TABLE_ITEMS; dek_index++) { 
        i += careful_write(buf, i, PAGE_SIZE, " %4d: %08x\n", dek_index, pCard->dek_kek_map[table][dek_index].kekIndex);
    }
    i += sprintf(buf + i, "\n");

    /* BUG if we return more than a single page of data */
    //BUG_ON(i > PAGE_SIZE);
    if (i > PAGE_SIZE)
        i = PAGE_SIZE;
    
    return i;
}

/******************************************************************************
show_dek_kek_map0():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
ssize_t

show_dek_kek_map0(struct device *dev, struct device_attribute *attr, char *buf)
{
    return show_dek_kek_map(dev, buf, 0);
}

/******************************************************************************
show_dek_kek_map1():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
ssize_t
show_dek_kek_map1(struct device *dev, struct device_attribute *attr, char *buf)
{
    return show_dek_kek_map(dev, buf, 1);
}

/******************************************************************************
show_target_dek_map():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
ssize_t
show_target_dek_map(struct device *dev, struct device_attribute *attr, char *buf)
{
    int i = 0;
    unsigned int chan, device, lun = 0;
    ag_encrypt_map_t *p;
    struct list_head *lh;
    struct Scsi_Host *sh = class_to_shost(dev);
    ag_card_t *pCard = (ag_card_t *) sh->hostdata;

    if(!pCard->encrypt)
        return -EINVAL;

    for(chan = 0; chan <= AGTIAPI_MAX_CHANNEL_NUM; chan++) {
        for(device = 0; device < pCard->devDiscover; device++) {
#ifdef REPORT_ALL_LUNS
            for(lun = 0; lun < AGTIAPI_MAX_LUN; lun++) {
#endif
                lh = MAP_TABLE_ENTRY(pCard, chan, device, lun);
                if(lh) {
                    list_for_each_entry(p, lh, list) {
                        if(p->dekIndex != DEK_INDEX_INVALID)
                            i += careful_write(buf, i, PAGE_SIZE, " %u:%u:%u: %x %8x %8x %16lx %16lx %08x:%08x %1x\n", chan, device, lun, p->dekTable, p->dekIndex, p->kekIndex, p->lbaMin, p->lbaMax, p->keyTag[1], p->keyTag[0], p->keyTagCheck);
                    }
                }
#ifdef REPORT_ALL_LUNS
            }
#endif
        }
    }

    if (i > PAGE_SIZE)
        i = PAGE_SIZE;

    return i;
}


/******************************************************************************
agtiapi_AddDek():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
static int
agtiapi_AddDek(ag_card_t *pCard, bit32 dek_table, bit32 dek_index, bit32 blob_format, bit32 entry_sz, tiEncryptDekBlob_t *dek_blob, U32_64 *addr) 
{
    ag_resource_info_t *pRscInfo = &pCard->pCardInfo->tiRscInfo;
    tiEncryptDekBlob_t *pDekTable;
    char *p;

    if (dek_index >= DEK_MAX_TABLE_ITEMS) {
        printf("%s: Bad dek index 0x%x (MAX: 0x%x).\n", __FUNCTION__, dek_index, DEK_MAX_TABLE_ITEMS);
        return -E_DEK_INDEX;
    }

    switch(dek_table) {
        case DEK_TABLE_0:
            pDekTable = pRscInfo->tiLoLevelResource.loLevelMem.mem[DEK_MEM_INDEX_1].virtPtr;
            break;
        case DEK_TABLE_1:
            pDekTable = pRscInfo->tiLoLevelResource.loLevelMem.mem[DEK_MEM_INDEX_2].virtPtr;
            break;
        default:
            printf("%s: Unknown dek table %d\n", __FUNCTION__, dek_table);
            return -E_DEK_TABLE;
    }

    #ifdef __VMKLNX__
        *addr = (U32_64) __pa(&pDekTable[0]);
    #else
        *addr = (U32_64) virt_to_phys(&pDekTable[0]);
    #endif

    p = (char *) &pDekTable[0] + (dek_index * pCard->dek_size);

    printf("%s: Base: %p, Index: %08x, Virt: %p Size: %d\n", __FUNCTION__, pDekTable, dek_index, &pDekTable[dek_index], pCard->dek_size);
    memcpy(p, dek_blob, pCard->dek_size);
    wmb();

    /* Flush entry */
    ostiCacheFlush(&pCard->tiRoot, NULL, p, pCard->dek_size);

    return 0;
}

/******************************************************************************
agtiapi_MapDekKek():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
static int
agtiapi_MapDekKek(ag_card_t *pCard, bit32 dek_table, bit32 dek_index, bit32 kek_index)
{
    if (dek_index >= DEK_MAX_TABLE_ITEMS) {
        printf("%s: Bad dek index 0x%x (MAX: 0x%x).\n", __FUNCTION__, dek_index, DEK_MAX_TABLE_ITEMS);
        return -E_DEK_INDEX;
    }

    if (dek_table >= DEK_MAX_TABLES) {
        printf("%s: Bad dek table.\n", __FUNCTION__);
        return -E_DEK_TABLE;
    }

    if (kek_index >= KEK_TABLE_MAX_ENTRY) {
        printf("%s: Bad kek index.\n", __FUNCTION__);
        return -E_KEK_INDEX;
    }
   
    pCard->dek_kek_map[dek_table][dek_index].kekIndex = kek_index; 
    return 0;
}

/******************************************************************************
agtiapi_AddKek():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
static int
agtiapi_AddKek(ag_card_t *pCard, bit32 kek_index, bit32 wrapper_kek_index, tiEncryptKekBlob_t *kek_blob)
{
    if (kek_index >= KEK_TABLE_MAX_ENTRY) {
        printf("%s: Bad kek index.\n", __FUNCTION__);
        return -E_KEK_INDEX;
    }
    if (wrapper_kek_index >= KEK_TABLE_MAX_ENTRY) {
        printf("%s: Bad kek wrapper index.\n", __FUNCTION__);
        return -E_KEK_INDEX;
    }
    pCard->kek_table[kek_index].wrapperIndex = wrapper_kek_index;
    memcpy(&pCard->kek_table[kek_index].kekBlob, kek_blob, sizeof(tiEncryptKekBlob_t));
    return 0;
}

/******************************************************************************
agtiapi_MapDek():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
static int
agtiapi_MapDek(ag_card_t *pCard, EncryptDeviceDekMap_t *dek_map)
{
    int found = 0;
    bit32 chan, device, lun;
    bit32 dek_table, dek_index, kek_index;
    unsigned long long lba_min, lba_max;
    ag_encrypt_map_t *p, *n;
    struct list_head *lh;

    chan = dek_map->channel;
    device = dek_map->device;
    lun = dek_map->lun;

    lba_min = dek_map->dekMapEntry[0].startLBA;
    lba_max = dek_map->dekMapEntry[0].endLBA;

    dek_table = dek_map->dekMapEntry[0].dek.dekTable;
    dek_index = dek_map->dekMapEntry[0].dek.dekIndex;

    /* Sanity check channel, device, lun */
    if (chan > AGTIAPI_MAX_CHANNEL_NUM) {
        printf("%s: Bad channel %d.\n", __FUNCTION__, chan);
        return -E_CHANNEL_INDEX;
    }
    if (device >= pCard->devDiscover) {
        printf("%s: Bad device %d.\n", __FUNCTION__, device);
        return -E_DEVICE_INDEX;
    }
    if (lun >= AGTIAPI_MAX_LUN) {
        printf("%s: Bad lun %d.\n", __FUNCTION__, lun);
        return -E_LUN_INDEX;
    }

    /* Sanity check dek index */
    if (dek_index >= DEK_MAX_TABLE_ITEMS) {
        printf("%s: Bad dek index 0x%x (MAX: 0x%x).\n", __FUNCTION__, dek_index, DEK_MAX_TABLE_ITEMS);
        return -E_DEK_INDEX;
    }

    /* Sanity check dek table */
    if (dek_table >= DEK_MAX_TABLES) {
        printf("%s: Bad dek table %d.\n", __FUNCTION__, dek_table);
        return -E_DEK_TABLE;
    }

    /* Check that lba min and lba max are sane */
    if (lba_min >= lba_max) {
        printf("%s: Bad lba min and lba max: %llx %llx.\n", __FUNCTION__, lba_min, lba_max);
        return -E_LBA_RANGE;
    } 

    /* dek_table and dek_index are valid, look up kek */
    kek_index = pCard->dek_kek_map[dek_table][dek_index].kekIndex;

    lh = MAP_TABLE_ENTRY(pCard, chan, device, lun);

    if (dek_map->dekMapEntry[0].flags & ENCRYPT_DEK_MAP_ENTRY_CLEAR) { 
        /* Delete the entry */
        found = 0;
        list_for_each_entry_safe(p, n, lh, list) {
            if (p->lbaMin   == lba_min   &&
                p->lbaMax   == lba_max   &&
                p->dekTable == dek_table &&
                p->dekIndex == dek_index &&
                p->kekIndex == kek_index) {
                /* Entry found, unlink and reclaim it */
                found = 1;
                list_del(&p->list);
                mempool_free(p, pCard->map_mempool);
            }
        }
        if (!found) {
            printf("%s: Entry %x %x %x %llx %llx not found.\n", __FUNCTION__, dek_table, dek_index, kek_index, lba_min, lba_max);
            return -E_NOT_FOUND;
        }
    } else if (dek_map->dekMapEntry[0].flags & ENCRYPT_DEK_MAP_ENTRY_VALID) {
        /* Add the entry */

          p = (ag_encrypt_map_t *)uma_zalloc(pCard->map_cache, M_WAITOK); //Encryption
        if (!p) {
            printf("%s: Unable to allocate from memory pool.\n", __FUNCTION__);
            return -E_MEMPOOL_ALLOC;
        }

        /* Populate it */
        p->lbaMin = lba_min;
        p->lbaMax = lba_max;
        p->dekTable = dek_table;
        p->dekIndex = dek_index;
        p->kekIndex = kek_index;
        p->keyTagCheck = dek_map->keytag_check;
        memcpy(&p->keyTag, &dek_map->keytag, sizeof(p->keyTag));

        /* Test to see if this new mapping overlaps an existing mapping */
        list_for_each_entry(n, lh, list) {
            /* 
             * Check if the start lba falls in existing range ||
             * Check if the end lba falls in existing range   ||
             * Check if the start lba of the existing range falls in the new range
             */
            if (((p->lbaMin >= n->lbaMin) && (p->lbaMin <= n->lbaMax)) ||
                ((p->lbaMax >= n->lbaMin) && (p->lbaMax <= n->lbaMax)) ||
                ((n->lbaMin >= p->lbaMin) && (n->lbaMin <= p->lbaMax))) {
                printf("%s: WARNING: New entry lba range overlap: %llx - %llx vs %llx - %llx.\n", __FUNCTION__, p->lbaMin, p->lbaMax, n->lbaMin, n->lbaMax);
            }
        }

        /* Link it in to list at the head so it takes precedence */
        list_add(&p->list, lh);

        /* TODO: Decide if/how to refcount each dek/kek index used by the mapping */

    } else {
        printf("%s: Bad flags %08x\n", __FUNCTION__, dek_map->dekMapEntry[0].flags);
        return -E_FLAGS;
    } 

    return 0;
}
#endif
#ifdef HIALEAH_ENCRYPTION
/******************************************************************************
agtiapi_SetupEncryption():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
int
agtiapi_SetupEncryption(struct agtiapi_softc *pCard)
{
  tiRoot_t *tiRoot = (tiRoot_t *) &pCard->tiRoot;
  bit32 status = tiSuccess;
  printf("agtiapi_SetupEncryption: HIALEAH_ENCRYPTION\n");
  if (pCard->encrypt == agTRUE)
  {
    status = tiCOMEncryptGetInfo(tiRoot);
    printf("agtiapi_SetupEncryption: HIALEAH_ENCRYPTION tiCOMEncryptGetInfo Status 0x%x\n",status);

    if(status == 1 )
    {
        status = tiCOMEncryptHilSet(tiRoot );
        if (status) {
            pCard->encrypt = agFALSE;
            printf("agtiapi_SetupEncryption: HIALEAH_ENCRYPTION not set\n");
        }
    }
  }
    return 0;
}
#ifdef ENCRYPT_ENHANCE
/******************************************************************************
agtiapi_SetupEncryptionPools():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
int
agtiapi_SetupEncryptionPools(struct agtiapi_softc *pCard)
{
    /* Configure encryption memory pool */
    memset(pCard->map_cache_name, 0, sizeof(pCard->map_cache_name));
    snprintf(pCard->map_cache_name, sizeof(pCard->map_cache_name) - 1, "map_cache_%d", pCard->cardNo);

//zone allocation
     pCard->map_cache = uma_zcreate(pCard->map_cache_name, sizeof(ag_encrypt_map_t),NULL, NULL, NULL, NULL, 0, 0);
    if(!pCard->map_cache) {
        /*
         * This error may be due to an existing cache in the kernel
         * from an earlier kmem_cache that wasn't properly freed
         */
        printf("Unable to create uma_zcreate cache for encryption map mempool.\n");
        return -EFAULT;
    }
    uma_zone_set_max(pCard->map_cache, ENCRYPTION_MAP_MEMPOOL_SIZE); 
   

    /* Configure encryption IO error pool */
    INIT_LIST_HEAD(&pCard->ioerr_queue);
/*#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)) // ####
    pCard->ioerr_queue_lock = SPIN_LOCK_UNLOCKED;
#else */
    pCard->ioerr_queue_lock = AG_SPIN_UNLOCK(pCard->ioerr_queue_lock); 
//#endif


    memset(pCard->ioerr_cache_name, 0, sizeof(pCard->ioerr_cache_name));
    snprintf(pCard->ioerr_cache_name, sizeof(pCard->ioerr_cache_name) - 1, "ioerr_cache_%d", pCard->cardNo);

    pCard->ioerr_cache = uma_zcreate(pCard->ioerr_cache_name, sizeof(ag_encrypt_ioerr_t), NULL, NULL, NULL, NULL, 0, 0);
    if(!pCard->ioerr_cache) {
        /*
         * This error may be due to an existing cache in the kernel
         * from an earlier kmem_cache that wasn't properly freed
         */
        printf("Unable to create kmem cache for encryption IO error mempool.\n");
        return -EFAULT;
    } 
    uma_zone_set_max(pCard->ioerr_cache,  ENCRYPTION_IO_ERR_MEMPOOL_SIZE);

    /* Set cipher mode to something invalid */
    pCard->cipher_mode = CIPHER_MODE_INVALID;

    return 0;
}
#endif
/******************************************************************************
agtiapi_CleanupEncryption():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
void
agtiapi_CleanupEncryption(struct agtiapi_softc *pCard)
{
#ifdef ENCRYPT_ENHANCE
   if(pCard->encrypt_map) {
        int chan, device, lun;
        struct list_head *lh;
        ag_encrypt_map_t *p, *n;

        for (chan = 0; chan < (AGTIAPI_MAX_CHANNEL_NUM + 1); chan++) {
            for (device = 0; device < pCard->devDiscover; device++) {
                for (lun = 0; lun < AGTIAPI_MAX_LUN; lun++) {
                    lh = MAP_TABLE_ENTRY(pCard, chan, device, lun);
                    list_for_each_entry_safe(p, n, lh, list) {
        //                mempool_free(p, pCard->map_mempool);
                    }
                }
            }
        }
        vfree(pCard->encrypt_map);
        pCard->encrypt_map = NULL;
    }
#endif
}

#ifdef ENCRYPT_ENHANCE
/******************************************************************************
agtiapi_CleanupEncryptionPools():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
void
agtiapi_CleanupEncryptionPools(struct agtiapi_softc *pCard)
{
    ag_encrypt_ioerr_t *ioerr, *tmp;
    atomic_set(&ioerr_queue_count);

    /* 
     * TODO: check "outstanding_encrypted_io_count" for non-zero 
     *       and free all mempool items prior to destroying pool
     */

    /* Clean up memory pools */
    if (pCard->map_mempool) {
        mempool_destroy(pCard->map_mempool);
        printf("Encryption Map mempool released.\n");
        pCard->map_mempool = NULL;
    }

    /* Clean up kmem cache */
    if (pCard->map_cache) {
        kmem_cache_destroy(pCard->map_cache);
        printf("Kernel memory cache %s released.\n", pCard->map_cache_name);
        pCard->map_cache = NULL;
    }

    /* Clean up memory pools */
    list_for_each_entry_safe(ioerr, tmp, &pCard->ioerr_queue, list) {
        list_del_init(&ioerr->list);
        mempool_free(ioerr, pCard->ioerr_mempool);
        atomic_dec(&ioerr_queue_count);
    }

    if (pCard->ioerr_mempool) {
        mempool_destroy(pCard->ioerr_mempool);
        printf("Encryption IO Error mempool released.\n");
        pCard->ioerr_mempool = NULL;
    }

    /* Clean up kmem cache */
    if (pCard->ioerr_cache) {
        kmem_cache_destroy(pCard->ioerr_cache);
        printf("Kernel memory cache %s released.\n", pCard->ioerr_cache_name);
        pCard->ioerr_cache = NULL;
    }
}

/******************************************************************************
agtiapi_EncryptionIoctl():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
int
agtiapi_EncryptionIoctl(struct agtiapi_softc *pCard, IoctlEncrypt_t *pIoctlPayload) 
{
    int rv, rc = 0, skip_wait = 0;
    tiRoot_t *tiRoot = (tiRoot_t *) &pCard->tiRoot;
    IoctlTISAEncrypt_t *ioctl_data = &pIoctlPayload->body;
    pIoctlPayload->hdr.Status = IOCTL_ERR_STATUS_INVALID_CODE;
    pCard->ioctl_data = (void *) ioctl_data;
    init_completion(&pCard->ioctl_completion);

    /* Check that the system is quiesced */
    if (atomic_read(&outstanding_encrypted_io_count) != 0) 
        printf("%s: WARNING: Attempting encryption management update with outstanding encrypted IOs!\n", __FUNCTION__);

printf("%s: Minor %d\n", __FUNCTION__, pIoctlPayload->hdr.MinorFunction);
    switch(pIoctlPayload->hdr.MinorFunction) {
        case IOCTL_MN_ENCRYPTION_GET_INFO:
            {
                //IoctlEncryptGetInfo_t *get_info = (IoctlEncryptGetInfo_t *) &ioctl_data->request;
                rc = tiCOMEncryptGetInfo(tiRoot);
            }
            break;
        case IOCTL_MN_ENCRYPTION_SET_MODE:
            {
                u32 reg_val = 0, new_cipher_mode = 0;
                IoctlEncryptSetMode_t *set_mode = (IoctlEncryptSetMode_t *) &ioctl_data->request;

                printf("%s: input %08x\n", __FUNCTION__, set_mode->securityCipherMode);

                /* Set security mode */
                if(TI_ENCRYPT_SEC_MODE_FACT_INIT)
                    if(set_mode->securityCipherMode & TI_ENCRYPT_SEC_MODE_FACT_INIT) {
                        reg_val |= TI_ENCRYPT_SEC_MODE_FACT_INIT;
                        pCard->dek_size = DEK_SIZE_PLAIN;
                    }
                if(set_mode->securityCipherMode & TI_ENCRYPT_SEC_MODE_A) {
                    reg_val |= TI_ENCRYPT_SEC_MODE_A;
                    pCard->dek_size = DEK_SIZE_ENCRYPT;
                } else if(set_mode->securityCipherMode & TI_ENCRYPT_SEC_MODE_B) {
                    reg_val |= TI_ENCRYPT_SEC_MODE_B;
                    pCard->dek_size = DEK_SIZE_ENCRYPT;
                }

                /* Set cipher mode */
                if(set_mode->securityCipherMode & TI_ENCRYPT_ATTRIB_CIPHER_XTS) {
                    reg_val |= TI_ENCRYPT_ATTRIB_CIPHER_XTS;
                    new_cipher_mode = TI_ENCRYPT_MODE_XTS_AES;
                }

                printf("%s: Setting security cipher mode to: 0x%08x\n", __FUNCTION__, reg_val);
                pCard->cipher_mode = new_cipher_mode;

                rc = tiCOMEncryptSetMode(tiRoot, reg_val);
            } 
            break;
        case IOCTL_MN_ENCRYPTION_KEK_ADD:
            {
                tiEncryptKekBlob_t kek_blob;
                IoctlEncryptKekAdd_t *kek_add = (IoctlEncryptKekAdd_t *) &ioctl_data->request;
                printf("%s: Add kek at index 0x%x wrapper 0x%x format 0x%x\n", __FUNCTION__, kek_add->kekIndex, kek_add->wrapperKekIndex, kek_add->blobFormat);
                
                /* Copy kek_blob from user pointer to local buffer */
                if(access_ok(VERIFY_READ, kek_add->EncryptKekBlob, sizeof(kek_blob))) {
                    printf("%s: Starting copy from user %p to kernel %p\n", __FUNCTION__, kek_add->EncryptKekBlob, &kek_blob);
                    if((rv = copy_from_user(&kek_blob, kek_add->EncryptKekBlob, sizeof(kek_blob))) != 0) {
                        printf("%s: Copy error, %d left\n", __FUNCTION__, rv);
                        return IOCTL_CALL_FAIL;
                    }
                    rc = tiCOMEncryptKekAdd(tiRoot, kek_add->kekIndex, kek_add->wrapperKekIndex, kek_add->blobFormat, &kek_blob);
                    
                    /* Add kek to local kek table (in case of chip reset) */
                    if(rc == tiSuccess) {
                        if(agtiapi_AddKek(pCard, kek_add->kekIndex, kek_add->wrapperKekIndex, &kek_blob) < 0) {
                            return IOCTL_CALL_FAIL;
                        }
                    }
                } else { 
                    return IOCTL_CALL_FAIL;
                }
            }
            break;
        case IOCTL_MN_ENCRYPTION_DEK_ADD:
            {
                tiEncryptDekBlob_t dek_blob; /* Copied in */
                IoctlEncryptDekAdd_t *dek_add = (IoctlEncryptDekAdd_t *) &ioctl_data->request;
                bit32 kek_index = dek_add->kekIndex;
                bit32 dek_index = dek_add->dekIndex;
                bit32 dek_table = dek_add->dekTable;
                bit32 blob_format = dek_add->dekBlobFormat;
                bit32 entry_sz = dek_add->dekTableKeyEntrySize;
                U32_64 addr = 0;
                bit32 addr_table[2];
                memset(addr_table, 0, sizeof(addr_table));

                printf("%s: Add dek at index 0x%x, table %x, kek index %x, blob format %x, entry size %x\n", __FUNCTION__, dek_index, dek_table, kek_index, blob_format, entry_sz);
                
                /* Copy dek_blob from user pointer to local buffer */
                if(access_ok(VERIFY_READ, dek_add->dekBlob, sizeof(dek_blob))) {
                    printf("%s: Starting copy from user %p to kernel %p\n", __FUNCTION__, dek_add->dekBlob, &dek_blob);
                    if((rv = copy_from_user(&dek_blob, dek_add->dekBlob, sizeof(dek_blob))) != 0) {
                        printf("%s: Copy error, %d left\n", __FUNCTION__, rv);
                        return IOCTL_CALL_FAIL;
                    }
                    
                    /* Add DEK to local table */
                    if (agtiapi_AddDek(pCard, dek_table, dek_index, blob_format, entry_sz, &dek_blob, &addr) < 0) {
                        return IOCTL_CALL_FAIL;
                    }
                    memcpy(addr_table, &addr, sizeof(addr));

                    /* Add DEK-KEK association in local table */
                    if (agtiapi_MapDekKek(pCard, dek_table, dek_index, kek_index) < 0) {
                        return IOCTL_CALL_FAIL;
                    }
                
                    /* Push DEK to chip */    
                    rc = tiCOMEncryptDekAdd(tiRoot, kek_index, dek_table, addr_table[1], addr_table[0], dek_index, 1, blob_format, entry_sz);
                } else {
                    return IOCTL_CALL_FAIL;
                }
            }
            break;
        case IOCTL_MN_ENCRYPTION_DEK_INVALID:
            {
                IoctlEncryptDekInvalidate_t *dek_to_invalidate = (IoctlEncryptDekInvalidate_t *) &ioctl_data->request;
                printf("%s: Invalidating dek at index 0x%x, table %x\n", __FUNCTION__, dek_to_invalidate->dek.dekIndex, dek_to_invalidate->dek.dekTable);

                rc = tiCOMEncryptDekInvalidate(tiRoot, dek_to_invalidate->dek.dekTable, dek_to_invalidate->dek.dekIndex);
                /* TODO: What to do in local tables? Mark it? */
            }
            break;
        case IOCTL_MN_ENCRYPTION_KEK_NVRAM:
            {
                rc = tiError;
            }
            break;
        case IOCTL_MN_ENCRYPTION_DEK_ASSIGN:
            {
                IoctlEncryptDekMapTable_t *p_dek_map = (IoctlEncryptDekMapTable_t *) &ioctl_data->request;
                
                /* Fill in host */
                p_dek_map->dekMap[0].host = (bit32) pCard->pHost->host_no;

                printf("%s: Host %u: Mapping %u:%u:%u (%llx to %llx) to dek at index 0x%x, table %x, keytag %08x:%08x\n", __FUNCTION__, p_dek_map->dekMap[0].host, p_dek_map->dekMap[0].channel, p_dek_map->dekMap[0].device, p_dek_map->dekMap[0].lun, p_dek_map->dekMap[0].dekMapEntry[0].startLBA, p_dek_map->dekMap[0].dekMapEntry[0].endLBA, p_dek_map->dekMap[0].dekMapEntry[0].dek.dekIndex, p_dek_map->dekMap[0].dekMapEntry[0].dek.dekTable, p_dek_map->dekMap[0].keytag[1], p_dek_map->dekMap[0].keytag[0]);

                /* Create a mapping in local tables */
                if (agtiapi_MapDek(pCard, &p_dek_map->dekMap[0]) < 0) {
                    pIoctlPayload->hdr.Status = IOCTL_ERR_STATUS_INVALID_CODE;
                    return IOCTL_CALL_FAIL;
                }

                rc = tiSuccess;
                skip_wait = 1;
                ioctl_data->encryptFunction = encryptSetDekMap;
                ioctl_data->status = tiSuccess;
                ioctl_data->subEvent = 0;
            } 
            break;
        case IOCTL_MN_ENCRYPTION_ERROR_QUERY:
            {
                unsigned long flags, i, query_flag;
                ag_encrypt_ioerr_t *ioerr, *tmp; 
                IoctlEncryptErrorQuery_t *perr = (IoctlEncryptErrorQuery_t *) &ioctl_data->request;

                printf("%s: query flag %x\n", __FUNCTION__, perr->query_flag);
                query_flag = perr->query_flag;

                /* initialize */
                memset(perr, 0, sizeof(IoctlEncryptErrorQuery_t));

error_query_restart:
                /* Take spinlock */
              //  spin_lock_irqsave(&pCard->ioerr_queue_lock, flags);
                AG_SPIN_LOCK_IRQ(&pCard->ioerr_queue_lock, flags);  
                
                /* Walk list */
                i = 0;
                list_for_each_entry_safe(ioerr, tmp, &pCard->ioerr_queue, list) {
                    if (i >= 32) 
                        break;
                    
                    perr->valid_mask |= (1 << i);
                    memcpy(&perr->error[i], &ioerr->ioerr, sizeof(IoctlEncryptIOError_t));
                    list_del_init(&ioerr->list);
                    mempool_free(ioerr, pCard->ioerr_mempool);
                    i++;
                    atomic_dec(&ioerr_queue_count);
                }

                /* Release spinlock */
             //   spin_unlock_irqrestore(&pCard->ioerr_queue_lock, flags);
                AG_SPIN_UNLOCK_IRQ(&pCard->ioerr_queue_lock, flags); //for test

                if (!perr->valid_mask) {
                    /* No encryption IO error events, check flags to see if blocking wait OK */
                    if (query_flag == ERROR_QUERY_FLAG_BLOCK) {
                        if (wait_event_interruptible(ioerr_waitq, (atomic_read(&ioerr_queue_count)))) {
                            /* Awoken by signal */
                            return IOCTL_CALL_FAIL;
                        } else {
                            /* Awoken by IO error */
                            goto error_query_restart;
                        }
                    }
                }
                rc = tiSuccess;
                skip_wait = 1;
                ioctl_data->encryptFunction = encryptErrorQuery;
                ioctl_data->status = tiSuccess;
                ioctl_data->subEvent = 0;
            }
            break;
        default:
            printf("%s: Unrecognized Minor Function %d\n", __FUNCTION__, pIoctlPayload->hdr.MinorFunction);
            pIoctlPayload->hdr.Status = IOCTL_ERR_STATUS_INVALID_CODE;
            return IOCTL_CALL_FAIL;
            break;
    }

    /* Demux rc */
    switch(rc) {
        case tiSuccess:
            if(!skip_wait)
                wait_for_completion(&pCard->ioctl_completion);
                /* Maybe: wait_for_completion_timeout() */
            pIoctlPayload->hdr.Status = ioctl_data->status;
            break;
        case tiNotSupported:
            pIoctlPayload->hdr.Status = IOCTL_ERR_STATUS_NOT_SUPPORTED;
            break;
        default:
            printf("%s: Status: %d\n", __FUNCTION__, rc);
            pIoctlPayload->hdr.Status = IOCTL_ERR_STATUS_INVALID_CODE;
            break;
    }

    printf("%s: Encryption ioctl %d successful.\n", __FUNCTION__, pIoctlPayload->hdr.MinorFunction);
    return IOCTL_CALL_SUCCESS;
}
#endif
/******************************************************************************
agtiapi_SetupEncryptedIO():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
int
agtiapi_SetupEncryptedIO(struct agtiapi_softc *pCard, ccb_t *pccb, unsigned long long block)
{

    pCard->cipher_mode = TI_ENCRYPT_ATTRIB_CIPHER_XTS;
    /* Check that cipher mode is set properly */
    if (pCard->cipher_mode == CIPHER_MODE_INVALID) {
        printf("%s: Cipher mode not yet set.\n", __FUNCTION__);
        return -E_BAD_CIPHER_MODE;
    }

    memset(&(pccb->tiSuperScsiRequest.Encrypt), 0, sizeof(pccb->tiSuperScsiRequest.Encrypt));
    pccb->tiSuperScsiRequest.Encrypt.keyTagCheck = FALSE;
    pccb->tiSuperScsiRequest.Encrypt.encryptMode = pCard->cipher_mode;
    pccb->tiSuperScsiRequest.Encrypt.tweakVal_W0 = block; 
    if(pccb->tiSuperScsiRequest.scsiCmnd.cdb[0] == READ_16 ||
	pccb->tiSuperScsiRequest.scsiCmnd.cdb[0] == WRITE_16)
    {
        pccb->tiSuperScsiRequest.Encrypt.tweakVal_W0 = ((pccb->tiSuperScsiRequest.scsiCmnd.cdb[6] << 24 ) |
							   (pccb->tiSuperScsiRequest.scsiCmnd.cdb[7] << 16 ) |
							   (pccb->tiSuperScsiRequest.scsiCmnd.cdb[8] <<	8 ) |
							   (pccb->tiSuperScsiRequest.scsiCmnd.cdb[9]));
    pccb->tiSuperScsiRequest.Encrypt.tweakVal_W1 = ((pccb->tiSuperScsiRequest.scsiCmnd.cdb[2] << 24 ) |
							   (pccb->tiSuperScsiRequest.scsiCmnd.cdb[3] << 16 ) |
							   (pccb->tiSuperScsiRequest.scsiCmnd.cdb[4] <<	8 ) |
							   (pccb->tiSuperScsiRequest.scsiCmnd.cdb[5]));
    }
    /* Mark IO as valid encrypted IO */
    pccb->flags |= ENCRYPTED_IO;
    pccb->tiSuperScsiRequest.flags = TI_SCSI_INITIATOR_ENCRYPT;

    /* Bump refcount (atomic) */
    atomic_inc(&outstanding_encrypted_io_count);
    return 0;
}

/******************************************************************************
agtiapi_CleanupEncryptedIO():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
void
agtiapi_CleanupEncryptedIO(struct agtiapi_softc *pCard, ccb_t *pccb)
{
    if ((pccb->flags & ENCRYPTED_IO)) {
        /* Decrement refcount */
        atomic_dec(&outstanding_encrypted_io_count);
    }
    pccb->tiSuperScsiRequest.flags &= ~TI_SCSI_INITIATOR_ENCRYPT;
    pccb->flags &= ~ENCRYPTED_IO;
}
#ifdef ENCRYPT_ENHANCE
/******************************************************************************
agtiapi_HandleEncryptedIOFailure():

Purpose:
Parameters:
Return:
Note:
******************************************************************************/
void
agtiapi_HandleEncryptedIOFailure(ag_device_t *pDev, ccb_t *pccb) 
{
    unsigned long flags, qdepth;
    struct scsi_cmnd *cmd;
    ag_encrypt_ioerr_t *perr;
    ag_card_t *pCard;

    cmd = pccb->cmd;
    if (!cmd) {
        printf("%s: Malformed pccb %p.\n", __FUNCTION__, pccb);
        return;
    }

    pCard = pDev->pCard;

    /* Sanity check */
    if (!(pccb->flags & ENCRYPTED_IO)) {
        printf("%s: Skipping IO %lx: Not Encrypted.\n", __FUNCTION__, cmd->serial_number);
        return;
    }

    /* Check queue depth against max */
    qdepth = atomic_read(&ioerr_queue_count);
    if (qdepth >= IOERR_QUEUE_DEPTH_MAX) {
        printf("%s: Not queueing IO error due to queue full: %lu entries.\n", __FUNCTION__, qdepth);
        return;
    }

    /* Get a container for the ag_encrypt_ioerr_t item from the mempool */
//    perr = mempool_alloc(pCard->ioerr_mempool, GFP_ATOMIC);     
          p = (ag_encrypt_map_t *)uma_zalloc(pCard->map_cache, M_WAITOK); //Encryption
    if (!perr) {
        printf("%s: Mempool allocation failure.\n", __FUNCTION__);
        return;
    }

    /* Populate ag_encrypt_ioerr_t container */
    perr->ioerr.error_id = cmd->serial_number;
    perr->ioerr.timestamp = cmd->jiffies_at_alloc;
    perr->ioerr.host = (unsigned int) cmd->device->host->host_no;
    perr->ioerr.channel = cmd->device->channel;
    perr->ioerr.device = cmd->device->id;
    perr->ioerr.lun = cmd->device->lun;
    perr->ioerr.scsi_cmd = (unsigned int) cmd->cmnd[0];
    perr->ioerr.dek_index = pccb->tiSuperScsiRequest.Encrypt.dekInfo.dekIndex;
    perr->ioerr.dek_table = pccb->tiSuperScsiRequest.Encrypt.dekInfo.dekTable;
    perr->ioerr.kek_index = pccb->tiSuperScsiRequest.Encrypt.kekIndex;
    perr->ioerr.keytag_check = pccb->tiSuperScsiRequest.Encrypt.keyTagCheck;
    perr->ioerr.encrypt_mode = pccb->tiSuperScsiRequest.Encrypt.encryptMode;
    perr->ioerr.keytag[0] = pccb->tiSuperScsiRequest.Encrypt.keyTag_W0;
    perr->ioerr.keytag[1] = pccb->tiSuperScsiRequest.Encrypt.keyTag_W1;
 
    switch(pccb->scsiStatus) {
        case tiDetailDekKeyCacheMiss:
        case tiDetailDekIVMismatch:
            perr->ioerr.error_type = pccb->scsiStatus;
            break;
        default:
            printf("%s: Unrecognized encrypted IO completion error status: %d\n", __FUNCTION__, pccb->scsiStatus);
            perr->ioerr.error_type = 0xffffffff;
            break;
    }

    /* Link IO err into queue */
    AG_SPIN_LOCK_IRQ(&pCard->ioerr_queue_lock, flags);
    list_add_tail(&perr->list, &pCard->ioerr_queue);
    AG_SPIN_UNLOCK_IRQ(&pCard->ioerr_queue_lock, flags);   
 
    /* Notify any wait queue waiters that an IO error has occurred */
    atomic_inc(&ioerr_queue_count);
    wake_up_interruptible(&ioerr_waitq);    

}
#endif
#endif
