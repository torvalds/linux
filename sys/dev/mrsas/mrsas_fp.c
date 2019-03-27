/*
 * Copyright (c) 2015, AVAGO Tech. All rights reserved. Author: Marian Choy
 * Copyright (c) 2014, LSI Corp. All rights reserved. Author: Marian Choy
 * Support: freebsdraid@avagotech.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer. 2. Redistributions
 * in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution. 3. Neither the name of the
 * <ORGANIZATION> nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing
 * official policies,either expressed or implied, of the FreeBSD Project.
 *
 * Send feedback to: <megaraidfbsd@avagotech.com> Mail to: AVAGO TECHNOLOGIES, 1621
 * Barber Lane, Milpitas, CA 95035 ATTN: MegaRaid FreeBSD
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/mrsas/mrsas.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>


/*
 * Function prototypes
 */
u_int8_t MR_ValidateMapInfo(struct mrsas_softc *sc);
u_int8_t 
mrsas_get_best_arm_pd(struct mrsas_softc *sc,
    PLD_LOAD_BALANCE_INFO lbInfo, struct IO_REQUEST_INFO *io_info);
u_int8_t
MR_BuildRaidContext(struct mrsas_softc *sc,
    struct IO_REQUEST_INFO *io_info,
    RAID_CONTEXT * pRAID_Context, MR_DRV_RAID_MAP_ALL * map);
u_int8_t
MR_GetPhyParams(struct mrsas_softc *sc, u_int32_t ld,
    u_int64_t stripRow, u_int16_t stripRef, struct IO_REQUEST_INFO *io_info,
    RAID_CONTEXT * pRAID_Context,
    MR_DRV_RAID_MAP_ALL * map);
u_int8_t MR_TargetIdToLdGet(u_int32_t ldTgtId, MR_DRV_RAID_MAP_ALL *map);
u_int32_t MR_LdBlockSizeGet(u_int32_t ldTgtId, MR_DRV_RAID_MAP_ALL * map);
u_int16_t MR_GetLDTgtId(u_int32_t ld, MR_DRV_RAID_MAP_ALL * map);
u_int16_t 
mrsas_get_updated_dev_handle(struct mrsas_softc *sc,
    PLD_LOAD_BALANCE_INFO lbInfo, struct IO_REQUEST_INFO *io_info);
u_int32_t mega_mod64(u_int64_t dividend, u_int32_t divisor);
u_int32_t
MR_GetSpanBlock(u_int32_t ld, u_int64_t row, u_int64_t *span_blk,
    MR_DRV_RAID_MAP_ALL * map, int *div_error);
u_int64_t mega_div64_32(u_int64_t dividend, u_int32_t divisor);
void 
mrsas_update_load_balance_params(struct mrsas_softc *sc,
    MR_DRV_RAID_MAP_ALL * map, PLD_LOAD_BALANCE_INFO lbInfo);
void
mrsas_set_pd_lba(MRSAS_RAID_SCSI_IO_REQUEST * io_request,
    u_int8_t cdb_len, struct IO_REQUEST_INFO *io_info, union ccb *ccb,
    MR_DRV_RAID_MAP_ALL * local_map_ptr, u_int32_t ref_tag,
    u_int32_t ld_block_size);
static u_int16_t
MR_LdSpanArrayGet(u_int32_t ld, u_int32_t span,
    MR_DRV_RAID_MAP_ALL * map);
static u_int16_t MR_PdDevHandleGet(u_int32_t pd, MR_DRV_RAID_MAP_ALL * map);
static u_int16_t
MR_ArPdGet(u_int32_t ar, u_int32_t arm,
    MR_DRV_RAID_MAP_ALL * map);
static MR_LD_SPAN *
MR_LdSpanPtrGet(u_int32_t ld, u_int32_t span,
    MR_DRV_RAID_MAP_ALL * map);
static u_int8_t
MR_LdDataArmGet(u_int32_t ld, u_int32_t armIdx,
    MR_DRV_RAID_MAP_ALL * map);
static MR_SPAN_BLOCK_INFO *
MR_LdSpanInfoGet(u_int32_t ld,
    MR_DRV_RAID_MAP_ALL * map);
MR_LD_RAID *MR_LdRaidGet(u_int32_t ld, MR_DRV_RAID_MAP_ALL * map);
static int MR_PopulateDrvRaidMap(struct mrsas_softc *sc);


/*
 * Spanset related function prototypes Added for PRL11 configuration (Uneven
 * span support)
 */
void	mr_update_span_set(MR_DRV_RAID_MAP_ALL * map, PLD_SPAN_INFO ldSpanInfo);
static u_int8_t
mr_spanset_get_phy_params(struct mrsas_softc *sc, u_int32_t ld,
    u_int64_t stripRow, u_int16_t stripRef, struct IO_REQUEST_INFO *io_info,
    RAID_CONTEXT * pRAID_Context, MR_DRV_RAID_MAP_ALL * map);
static u_int64_t
get_row_from_strip(struct mrsas_softc *sc, u_int32_t ld,
    u_int64_t strip, MR_DRV_RAID_MAP_ALL * map);
static u_int32_t
mr_spanset_get_span_block(struct mrsas_softc *sc,
    u_int32_t ld, u_int64_t row, u_int64_t *span_blk,
    MR_DRV_RAID_MAP_ALL * map, int *div_error);
static u_int8_t
get_arm(struct mrsas_softc *sc, u_int32_t ld, u_int8_t span,
    u_int64_t stripe, MR_DRV_RAID_MAP_ALL * map);


/*
 * Spanset related defines Added for PRL11 configuration(Uneven span support)
 */
#define	SPAN_ROW_SIZE(map, ld, index_) MR_LdSpanPtrGet(ld, index_, map)->spanRowSize
#define	SPAN_ROW_DATA_SIZE(map_, ld, index_)	\
	MR_LdSpanPtrGet(ld, index_, map)->spanRowDataSize
#define	SPAN_INVALID	0xff
#define	SPAN_DEBUG		0

/*
 * Related Defines
 */

typedef u_int64_t REGION_KEY;
typedef u_int32_t REGION_LEN;

#define	MR_LD_STATE_OPTIMAL		3
#define	FALSE					0
#define	TRUE					1

#define	LB_PENDING_CMDS_DEFAULT 4


/*
 * Related Macros
 */

#define	ABS_DIFF(a,b)   ( ((a) > (b)) ? ((a) - (b)) : ((b) - (a)) )

#define	swap32(x) \
  ((unsigned int)( \
    (((unsigned int)(x) & (unsigned int)0x000000ffUL) << 24) | \
    (((unsigned int)(x) & (unsigned int)0x0000ff00UL) <<  8) | \
    (((unsigned int)(x) & (unsigned int)0x00ff0000UL) >>  8) | \
    (((unsigned int)(x) & (unsigned int)0xff000000UL) >> 24) ))


/*
 * In-line functions for mod and divide of 64-bit dividend and 32-bit
 * divisor. Assumes a check for a divisor of zero is not possible.
 *
 * @param dividend:	Dividend
 * @param divisor:	Divisor
 * @return			remainder
 */

#define	mega_mod64(dividend, divisor) ({ \
int remainder; \
remainder = ((u_int64_t) (dividend)) % (u_int32_t) (divisor); \
remainder;})

#define	mega_div64_32(dividend, divisor) ({ \
int quotient; \
quotient = ((u_int64_t) (dividend)) / (u_int32_t) (divisor); \
quotient;})


/*
 * Various RAID map access functions.  These functions access the various
 * parts of the RAID map and returns the appropriate parameters.
 */

MR_LD_RAID *
MR_LdRaidGet(u_int32_t ld, MR_DRV_RAID_MAP_ALL * map)
{
	return (&map->raidMap.ldSpanMap[ld].ldRaid);
}

u_int16_t
MR_GetLDTgtId(u_int32_t ld, MR_DRV_RAID_MAP_ALL * map)
{
	return (map->raidMap.ldSpanMap[ld].ldRaid.targetId);
}

static u_int16_t
MR_LdSpanArrayGet(u_int32_t ld, u_int32_t span, MR_DRV_RAID_MAP_ALL * map)
{
	return map->raidMap.ldSpanMap[ld].spanBlock[span].span.arrayRef;
}

static u_int8_t
MR_LdDataArmGet(u_int32_t ld, u_int32_t armIdx, MR_DRV_RAID_MAP_ALL * map)
{
	return map->raidMap.ldSpanMap[ld].dataArmMap[armIdx];
}

static u_int16_t
MR_PdDevHandleGet(u_int32_t pd, MR_DRV_RAID_MAP_ALL * map)
{
	return map->raidMap.devHndlInfo[pd].curDevHdl;
}

static u_int8_t MR_PdInterfaceTypeGet(u_int32_t pd, MR_DRV_RAID_MAP_ALL *map)
{
    return map->raidMap.devHndlInfo[pd].interfaceType;
}


static u_int16_t
MR_ArPdGet(u_int32_t ar, u_int32_t arm, MR_DRV_RAID_MAP_ALL * map)
{
	return map->raidMap.arMapInfo[ar].pd[arm];
}

static MR_LD_SPAN *
MR_LdSpanPtrGet(u_int32_t ld, u_int32_t span, MR_DRV_RAID_MAP_ALL * map)
{
	return &map->raidMap.ldSpanMap[ld].spanBlock[span].span;
}

static MR_SPAN_BLOCK_INFO *
MR_LdSpanInfoGet(u_int32_t ld, MR_DRV_RAID_MAP_ALL * map)
{
	return &map->raidMap.ldSpanMap[ld].spanBlock[0];
}

u_int8_t
MR_TargetIdToLdGet(u_int32_t ldTgtId, MR_DRV_RAID_MAP_ALL * map)
{
	return map->raidMap.ldTgtIdToLd[ldTgtId];
}

u_int32_t
MR_LdBlockSizeGet(u_int32_t ldTgtId, MR_DRV_RAID_MAP_ALL * map)
{
	MR_LD_RAID *raid;
	u_int32_t ld, ldBlockSize = MRSAS_SCSIBLOCKSIZE;

	ld = MR_TargetIdToLdGet(ldTgtId, map);

	/*
	 * Check if logical drive was removed.
	 */
	if (ld >= MAX_LOGICAL_DRIVES)
		return ldBlockSize;

	raid = MR_LdRaidGet(ld, map);
	ldBlockSize = raid->logicalBlockLength;
	if (!ldBlockSize)
		ldBlockSize = MRSAS_SCSIBLOCKSIZE;

	return ldBlockSize;
}

/*
 * This function will Populate Driver Map using Dynamic firmware raid map
 */
static int
MR_PopulateDrvRaidMapVentura(struct mrsas_softc *sc)
{
	unsigned int i, j;
	u_int16_t ld_count;

	MR_FW_RAID_MAP_DYNAMIC *fw_map_dyn;
	MR_RAID_MAP_DESC_TABLE *desc_table;
	MR_DRV_RAID_MAP_ALL *drv_map = sc->ld_drv_map[(sc->map_id & 1)];
	MR_DRV_RAID_MAP *pDrvRaidMap = &drv_map->raidMap;
	void *raid_map_data = NULL;

	fw_map_dyn = (MR_FW_RAID_MAP_DYNAMIC *) sc->raidmap_mem[(sc->map_id & 1)];

	if (fw_map_dyn == NULL) {
		device_printf(sc->mrsas_dev,
		    "from %s %d map0  %p map1 %p map size %d \n", __func__, __LINE__,
		    sc->raidmap_mem[0], sc->raidmap_mem[1], sc->maxRaidMapSize);
		return 1;
	}
#if VD_EXT_DEBUG
	device_printf(sc->mrsas_dev,
	    " raidMapSize 0x%x, descTableOffset 0x%x, "
	    " descTableSize 0x%x, descTableNumElements 0x%x \n",
	    fw_map_dyn->raidMapSize, fw_map_dyn->descTableOffset,
	    fw_map_dyn->descTableSize, fw_map_dyn->descTableNumElements);
#endif
	desc_table = (MR_RAID_MAP_DESC_TABLE *) ((char *)fw_map_dyn +
	    fw_map_dyn->descTableOffset);
	if (desc_table != fw_map_dyn->raidMapDescTable) {
		device_printf(sc->mrsas_dev,
		    "offsets of desc table are not matching returning "
		    " FW raid map has been changed: desc %p original %p\n",
		    desc_table, fw_map_dyn->raidMapDescTable);
	}
	memset(drv_map, 0, sc->drv_map_sz);
	ld_count = fw_map_dyn->ldCount;
	pDrvRaidMap->ldCount = ld_count;
	pDrvRaidMap->fpPdIoTimeoutSec = fw_map_dyn->fpPdIoTimeoutSec;
	pDrvRaidMap->totalSize = sizeof(MR_DRV_RAID_MAP_ALL);
	/* point to actual data starting point */
	raid_map_data = (char *)fw_map_dyn +
	    fw_map_dyn->descTableOffset + fw_map_dyn->descTableSize;

	for (i = 0; i < fw_map_dyn->descTableNumElements; ++i) {
		if (!desc_table) {
			device_printf(sc->mrsas_dev,
			    "desc table is null, coming out %p \n", desc_table);
			return 1;
		}
#if VD_EXT_DEBUG
		device_printf(sc->mrsas_dev, "raid_map_data %p \n", raid_map_data);
		device_printf(sc->mrsas_dev,
		    "desc table %p \n", desc_table);
		device_printf(sc->mrsas_dev,
		    "raidmap type %d, raidmapOffset 0x%x, "
		    " raid map number of elements 0%x, raidmapsize 0x%x\n",
		    desc_table->raidMapDescType, desc_table->raidMapDescOffset,
		    desc_table->raidMapDescElements, desc_table->raidMapDescBufferSize);
#endif
		switch (desc_table->raidMapDescType) {
		case RAID_MAP_DESC_TYPE_DEVHDL_INFO:
			fw_map_dyn->RaidMapDescPtrs.ptrStruct.devHndlInfo = (MR_DEV_HANDLE_INFO *)
			    ((char *)raid_map_data + desc_table->raidMapDescOffset);
#if VD_EXT_DEBUG
			device_printf(sc->mrsas_dev,
			    "devHndlInfo address %p\n", fw_map_dyn->RaidMapDescPtrs.ptrStruct.devHndlInfo);
#endif
			memcpy(pDrvRaidMap->devHndlInfo, fw_map_dyn->RaidMapDescPtrs.ptrStruct.devHndlInfo,
			    sizeof(MR_DEV_HANDLE_INFO) * desc_table->raidMapDescElements);
			break;
		case RAID_MAP_DESC_TYPE_TGTID_INFO:
			fw_map_dyn->RaidMapDescPtrs.ptrStruct.ldTgtIdToLd = (u_int16_t *)
			    ((char *)raid_map_data + desc_table->raidMapDescOffset);
#if VD_EXT_DEBUG
			device_printf(sc->mrsas_dev,
			    "ldTgtIdToLd  address %p\n", fw_map_dyn->RaidMapDescPtrs.ptrStruct.ldTgtIdToLd);
#endif
			for (j = 0; j < desc_table->raidMapDescElements; j++) {
				pDrvRaidMap->ldTgtIdToLd[j] = fw_map_dyn->RaidMapDescPtrs.ptrStruct.ldTgtIdToLd[j];
#if VD_EXT_DEBUG
				device_printf(sc->mrsas_dev,
				    " %d drv ldTgtIdToLd %d\n",	j, pDrvRaidMap->ldTgtIdToLd[j]);
#endif
			}
			break;
		case RAID_MAP_DESC_TYPE_ARRAY_INFO:
			fw_map_dyn->RaidMapDescPtrs.ptrStruct.arMapInfo = (MR_ARRAY_INFO *) ((char *)raid_map_data +
			    desc_table->raidMapDescOffset);
#if VD_EXT_DEBUG
			device_printf(sc->mrsas_dev,
			    "arMapInfo  address %p\n", fw_map_dyn->RaidMapDescPtrs.ptrStruct.arMapInfo);
#endif
			memcpy(pDrvRaidMap->arMapInfo, fw_map_dyn->RaidMapDescPtrs.ptrStruct.arMapInfo,
			    sizeof(MR_ARRAY_INFO) * desc_table->raidMapDescElements);
			break;
		case RAID_MAP_DESC_TYPE_SPAN_INFO:
			fw_map_dyn->RaidMapDescPtrs.ptrStruct.ldSpanMap = (MR_LD_SPAN_MAP *) ((char *)raid_map_data +
			    desc_table->raidMapDescOffset);
			memcpy(pDrvRaidMap->ldSpanMap, fw_map_dyn->RaidMapDescPtrs.ptrStruct.ldSpanMap,
			    sizeof(MR_LD_SPAN_MAP) * desc_table->raidMapDescElements);
#if VD_EXT_DEBUG
			device_printf(sc->mrsas_dev,
			    "ldSpanMap  address %p\n", fw_map_dyn->RaidMapDescPtrs.ptrStruct.ldSpanMap);
			device_printf(sc->mrsas_dev,
			    "MR_LD_SPAN_MAP size 0x%lx\n", sizeof(MR_LD_SPAN_MAP));
			for (j = 0; j < ld_count; j++) {
				printf("mrsas(%d) : fw_map_dyn->ldSpanMap[%d].ldRaid.targetId 0x%x "
				    "fw_map_dyn->ldSpanMap[%d].ldRaid.seqNum 0x%x size 0x%x\n",
				    j, j, fw_map_dyn->RaidMapDescPtrs.ptrStruct.ldSpanMap[j].ldRaid.targetId, j,
				    fw_map_dyn->RaidMapDescPtrs.ptrStruct.ldSpanMap[j].ldRaid.seqNum,
				    (u_int32_t)fw_map_dyn->RaidMapDescPtrs.ptrStruct.ldSpanMap[j].ldRaid.rowSize);
				printf("mrsas(%d) : pDrvRaidMap->ldSpanMap[%d].ldRaid.targetId 0x%x "
				    "pDrvRaidMap->ldSpanMap[%d].ldRaid.seqNum 0x%x size 0x%x\n",
				    j, j, pDrvRaidMap->ldSpanMap[j].ldRaid.targetId, j,
				    pDrvRaidMap->ldSpanMap[j].ldRaid.seqNum,
				    (u_int32_t)pDrvRaidMap->ldSpanMap[j].ldRaid.rowSize);
				printf("mrsas : drv raid map all %p raid map %p LD RAID MAP %p/%p\n",
				    drv_map, pDrvRaidMap, &fw_map_dyn->RaidMapDescPtrs.ptrStruct.ldSpanMap[j].ldRaid,
				    &pDrvRaidMap->ldSpanMap[j].ldRaid);
			}
#endif
			break;
		default:
			device_printf(sc->mrsas_dev,
			    "wrong number of desctableElements %d\n",
			    fw_map_dyn->descTableNumElements);
		}
		++desc_table;
	}
	return 0;
}

/*
 * This function will Populate Driver Map using firmware raid map
 */
static int
MR_PopulateDrvRaidMap(struct mrsas_softc *sc)
{
	MR_FW_RAID_MAP_ALL *fw_map_old = NULL;
	MR_FW_RAID_MAP_EXT *fw_map_ext;
	MR_FW_RAID_MAP *pFwRaidMap = NULL;
	unsigned int i;
	u_int16_t ld_count;

	MR_DRV_RAID_MAP_ALL *drv_map = sc->ld_drv_map[(sc->map_id & 1)];
	MR_DRV_RAID_MAP *pDrvRaidMap = &drv_map->raidMap;

	if (sc->maxRaidMapSize) {
		return MR_PopulateDrvRaidMapVentura(sc);
	} else if (sc->max256vdSupport) {
		fw_map_ext = (MR_FW_RAID_MAP_EXT *) sc->raidmap_mem[(sc->map_id & 1)];
		ld_count = (u_int16_t)(fw_map_ext->ldCount);
		if (ld_count > MAX_LOGICAL_DRIVES_EXT) {
			device_printf(sc->mrsas_dev,
			    "mrsas: LD count exposed in RAID map in not valid\n");
			return 1;
		}
#if VD_EXT_DEBUG
		for (i = 0; i < ld_count; i++) {
			printf("mrsas : Index 0x%x Target Id 0x%x Seq Num 0x%x Size 0/%lx\n",
			    i, fw_map_ext->ldSpanMap[i].ldRaid.targetId,
			    fw_map_ext->ldSpanMap[i].ldRaid.seqNum,
			    fw_map_ext->ldSpanMap[i].ldRaid.size);
		}
#endif
		memset(drv_map, 0, sc->drv_map_sz);
		pDrvRaidMap->ldCount = ld_count;
		pDrvRaidMap->fpPdIoTimeoutSec = fw_map_ext->fpPdIoTimeoutSec;
		for (i = 0; i < (MAX_LOGICAL_DRIVES_EXT); i++) {
			pDrvRaidMap->ldTgtIdToLd[i] = (u_int16_t)fw_map_ext->ldTgtIdToLd[i];
		}
		memcpy(pDrvRaidMap->ldSpanMap, fw_map_ext->ldSpanMap, sizeof(MR_LD_SPAN_MAP) * ld_count);
#if VD_EXT_DEBUG
		for (i = 0; i < ld_count; i++) {
			printf("mrsas(%d) : fw_map_ext->ldSpanMap[%d].ldRaid.targetId 0x%x "
			    "fw_map_ext->ldSpanMap[%d].ldRaid.seqNum 0x%x size 0x%x\n",
			    i, i, fw_map_ext->ldSpanMap[i].ldRaid.targetId, i,
			    fw_map_ext->ldSpanMap[i].ldRaid.seqNum,
			    (u_int32_t)fw_map_ext->ldSpanMap[i].ldRaid.rowSize);
			printf("mrsas(%d) : pDrvRaidMap->ldSpanMap[%d].ldRaid.targetId 0x%x"
			    "pDrvRaidMap->ldSpanMap[%d].ldRaid.seqNum 0x%x size 0x%x\n", i, i,
			    pDrvRaidMap->ldSpanMap[i].ldRaid.targetId, i,
			    pDrvRaidMap->ldSpanMap[i].ldRaid.seqNum,
			    (u_int32_t)pDrvRaidMap->ldSpanMap[i].ldRaid.rowSize);
			printf("mrsas : drv raid map all %p raid map %p LD RAID MAP %p/%p\n",
			    drv_map, pDrvRaidMap, &fw_map_ext->ldSpanMap[i].ldRaid,
			    &pDrvRaidMap->ldSpanMap[i].ldRaid);
		}
#endif
		memcpy(pDrvRaidMap->arMapInfo, fw_map_ext->arMapInfo,
		    sizeof(MR_ARRAY_INFO) * MAX_API_ARRAYS_EXT);
		memcpy(pDrvRaidMap->devHndlInfo, fw_map_ext->devHndlInfo,
		    sizeof(MR_DEV_HANDLE_INFO) * MAX_RAIDMAP_PHYSICAL_DEVICES);

		pDrvRaidMap->totalSize = sizeof(MR_FW_RAID_MAP_EXT);
	} else {
		fw_map_old = (MR_FW_RAID_MAP_ALL *) sc->raidmap_mem[(sc->map_id & 1)];
		pFwRaidMap = &fw_map_old->raidMap;

#if VD_EXT_DEBUG
		for (i = 0; i < pFwRaidMap->ldCount; i++) {
			device_printf(sc->mrsas_dev,
			    "Index 0x%x Target Id 0x%x Seq Num 0x%x Size 0/%lx\n", i,
			    fw_map_old->raidMap.ldSpanMap[i].ldRaid.targetId,
			    fw_map_old->raidMap.ldSpanMap[i].ldRaid.seqNum,
			    fw_map_old->raidMap.ldSpanMap[i].ldRaid.size);
		}
#endif

		memset(drv_map, 0, sc->drv_map_sz);
		pDrvRaidMap->totalSize = pFwRaidMap->totalSize;
		pDrvRaidMap->ldCount = pFwRaidMap->ldCount;
		pDrvRaidMap->fpPdIoTimeoutSec =
		    pFwRaidMap->fpPdIoTimeoutSec;

		for (i = 0; i < MAX_RAIDMAP_LOGICAL_DRIVES + MAX_RAIDMAP_VIEWS; i++) {
			pDrvRaidMap->ldTgtIdToLd[i] =
			    (u_int8_t)pFwRaidMap->ldTgtIdToLd[i];
		}

		for (i = 0; i < pDrvRaidMap->ldCount; i++) {
			pDrvRaidMap->ldSpanMap[i] =
			    pFwRaidMap->ldSpanMap[i];

#if VD_EXT_DEBUG
			device_printf(sc->mrsas_dev, "pFwRaidMap->ldSpanMap[%d].ldRaid.targetId 0x%x "
			    "pFwRaidMap->ldSpanMap[%d].ldRaid.seqNum 0x%x size 0x%x\n",
			    i, i, pFwRaidMap->ldSpanMap[i].ldRaid.targetId,
			    pFwRaidMap->ldSpanMap[i].ldRaid.seqNum,
			    (u_int32_t)pFwRaidMap->ldSpanMap[i].ldRaid.rowSize);
			device_printf(sc->mrsas_dev, "pDrvRaidMap->ldSpanMap[%d].ldRaid.targetId 0x%x"
			    "pDrvRaidMap->ldSpanMap[%d].ldRaid.seqNum 0x%x size 0x%x\n", i, i,
			    pDrvRaidMap->ldSpanMap[i].ldRaid.targetId,
			    pDrvRaidMap->ldSpanMap[i].ldRaid.seqNum,
			    (u_int32_t)pDrvRaidMap->ldSpanMap[i].ldRaid.rowSize);
			device_printf(sc->mrsas_dev, "drv raid map all %p raid map %p LD RAID MAP %p/%p\n",
			    drv_map, pDrvRaidMap,
			    &pFwRaidMap->ldSpanMap[i].ldRaid, &pDrvRaidMap->ldSpanMap[i].ldRaid);
#endif
		}

		memcpy(pDrvRaidMap->arMapInfo, pFwRaidMap->arMapInfo,
		    sizeof(MR_ARRAY_INFO) * MAX_RAIDMAP_ARRAYS);
		memcpy(pDrvRaidMap->devHndlInfo, pFwRaidMap->devHndlInfo,
		    sizeof(MR_DEV_HANDLE_INFO) *
		    MAX_RAIDMAP_PHYSICAL_DEVICES);
	}
	return 0;
}

/*
 * MR_ValidateMapInfo:	Validate RAID map
 * input:				Adapter instance soft state
 *
 * This function checks and validates the loaded RAID map. It returns 0 if
 * successful, and 1 otherwise.
 */
u_int8_t
MR_ValidateMapInfo(struct mrsas_softc *sc)
{
	if (!sc) {
		return 1;
	}
	if (MR_PopulateDrvRaidMap(sc))
		return 0;

	MR_DRV_RAID_MAP_ALL *drv_map = sc->ld_drv_map[(sc->map_id & 1)];
	MR_DRV_RAID_MAP *pDrvRaidMap = &drv_map->raidMap;

	u_int32_t expected_map_size;

	drv_map = sc->ld_drv_map[(sc->map_id & 1)];
	pDrvRaidMap = &drv_map->raidMap;
	PLD_SPAN_INFO ldSpanInfo = (PLD_SPAN_INFO) & sc->log_to_span;

	if (sc->maxRaidMapSize)
		expected_map_size = sizeof(MR_DRV_RAID_MAP_ALL);
	else if (sc->max256vdSupport)
		expected_map_size = sizeof(MR_FW_RAID_MAP_EXT);
	else
		expected_map_size =
		    (sizeof(MR_FW_RAID_MAP) - sizeof(MR_LD_SPAN_MAP)) +
		    (sizeof(MR_LD_SPAN_MAP) * pDrvRaidMap->ldCount);

	if (pDrvRaidMap->totalSize != expected_map_size) {
		device_printf(sc->mrsas_dev, "map size %x not matching ld count\n", expected_map_size);
		device_printf(sc->mrsas_dev, "span map= %x\n", (unsigned int)sizeof(MR_LD_SPAN_MAP));
		device_printf(sc->mrsas_dev, "pDrvRaidMap->totalSize=%x\n", pDrvRaidMap->totalSize);
		return 1;
	}
	if (sc->UnevenSpanSupport) {
		mr_update_span_set(drv_map, ldSpanInfo);
	}
	mrsas_update_load_balance_params(sc, drv_map, sc->load_balance_info);

	return 0;
}

/*
 *
 * Function to print info about span set created in driver from FW raid map
 *
 * Inputs:		map
 * ldSpanInfo:	ld map span info per HBA instance
 *
 *
 */
#if SPAN_DEBUG
static int
getSpanInfo(MR_DRV_RAID_MAP_ALL * map, PLD_SPAN_INFO ldSpanInfo)
{

	u_int8_t span;
	u_int32_t element;
	MR_LD_RAID *raid;
	LD_SPAN_SET *span_set;
	MR_QUAD_ELEMENT *quad;
	int ldCount;
	u_int16_t ld;

	for (ldCount = 0; ldCount < MAX_LOGICAL_DRIVES; ldCount++) {
		ld = MR_TargetIdToLdGet(ldCount, map);
		if (ld >= MAX_LOGICAL_DRIVES) {
			continue;
		}
		raid = MR_LdRaidGet(ld, map);
		printf("LD %x: span_depth=%x\n", ld, raid->spanDepth);
		for (span = 0; span < raid->spanDepth; span++)
			printf("Span=%x, number of quads=%x\n", span,
			    map->raidMap.ldSpanMap[ld].spanBlock[span].
			    block_span_info.noElements);
		for (element = 0; element < MAX_QUAD_DEPTH; element++) {
			span_set = &(ldSpanInfo[ld].span_set[element]);
			if (span_set->span_row_data_width == 0)
				break;

			printf("Span Set %x: width=%x, diff=%x\n", element,
			    (unsigned int)span_set->span_row_data_width,
			    (unsigned int)span_set->diff);
			printf("logical LBA start=0x%08lx, end=0x%08lx\n",
			    (long unsigned int)span_set->log_start_lba,
			    (long unsigned int)span_set->log_end_lba);
			printf("span row start=0x%08lx, end=0x%08lx\n",
			    (long unsigned int)span_set->span_row_start,
			    (long unsigned int)span_set->span_row_end);
			printf("data row start=0x%08lx, end=0x%08lx\n",
			    (long unsigned int)span_set->data_row_start,
			    (long unsigned int)span_set->data_row_end);
			printf("data strip start=0x%08lx, end=0x%08lx\n",
			    (long unsigned int)span_set->data_strip_start,
			    (long unsigned int)span_set->data_strip_end);

			for (span = 0; span < raid->spanDepth; span++) {
				if (map->raidMap.ldSpanMap[ld].spanBlock[span].
				    block_span_info.noElements >= element + 1) {
					quad = &map->raidMap.ldSpanMap[ld].
					    spanBlock[span].block_span_info.
					    quad[element];
					printf("Span=%x, Quad=%x, diff=%x\n", span,
					    element, quad->diff);
					printf("offset_in_span=0x%08lx\n",
					    (long unsigned int)quad->offsetInSpan);
					printf("logical start=0x%08lx, end=0x%08lx\n",
					    (long unsigned int)quad->logStart,
					    (long unsigned int)quad->logEnd);
				}
			}
		}
	}
	return 0;
}

#endif
/*
 *
 * This routine calculates the Span block for given row using spanset.
 *
 * Inputs :	HBA instance
 * ld:		Logical drive number
 * row:		Row number
 * map:		LD map
 *
 * Outputs :	span	- Span number block
 * 						- Absolute Block number in the physical disk
 * 				div_error    - Devide error code.
 */

u_int32_t
mr_spanset_get_span_block(struct mrsas_softc *sc, u_int32_t ld, u_int64_t row,
    u_int64_t *span_blk, MR_DRV_RAID_MAP_ALL * map, int *div_error)
{
	MR_LD_RAID *raid = MR_LdRaidGet(ld, map);
	LD_SPAN_SET *span_set;
	MR_QUAD_ELEMENT *quad;
	u_int32_t span, info;
	PLD_SPAN_INFO ldSpanInfo = sc->log_to_span;

	for (info = 0; info < MAX_QUAD_DEPTH; info++) {
		span_set = &(ldSpanInfo[ld].span_set[info]);

		if (span_set->span_row_data_width == 0)
			break;
		if (row > span_set->data_row_end)
			continue;

		for (span = 0; span < raid->spanDepth; span++)
			if (map->raidMap.ldSpanMap[ld].spanBlock[span].
			    block_span_info.noElements >= info + 1) {
				quad = &map->raidMap.ldSpanMap[ld].
				    spanBlock[span].
				    block_span_info.quad[info];
				if (quad->diff == 0) {
					*div_error = 1;
					return span;
				}
				if (quad->logStart <= row &&
				    row <= quad->logEnd &&
				    (mega_mod64(row - quad->logStart,
				    quad->diff)) == 0) {
					if (span_blk != NULL) {
						u_int64_t blk;

						blk = mega_div64_32
						    ((row - quad->logStart),
						    quad->diff);
						blk = (blk + quad->offsetInSpan)
						    << raid->stripeShift;
						*span_blk = blk;
					}
					return span;
				}
			}
	}
	return SPAN_INVALID;
}

/*
 *
 * This routine calculates the row for given strip using spanset.
 *
 * Inputs :	HBA instance
 * ld:		Logical drive number
 * Strip:	Strip
 * map:		LD map
 *
 * Outputs :	row - row associated with strip
 */

static u_int64_t
get_row_from_strip(struct mrsas_softc *sc,
    u_int32_t ld, u_int64_t strip, MR_DRV_RAID_MAP_ALL * map)
{
	MR_LD_RAID *raid = MR_LdRaidGet(ld, map);
	LD_SPAN_SET *span_set;
	PLD_SPAN_INFO ldSpanInfo = sc->log_to_span;
	u_int32_t info, strip_offset, span, span_offset;
	u_int64_t span_set_Strip, span_set_Row;

	for (info = 0; info < MAX_QUAD_DEPTH; info++) {
		span_set = &(ldSpanInfo[ld].span_set[info]);

		if (span_set->span_row_data_width == 0)
			break;
		if (strip > span_set->data_strip_end)
			continue;

		span_set_Strip = strip - span_set->data_strip_start;
		strip_offset = mega_mod64(span_set_Strip,
		    span_set->span_row_data_width);
		span_set_Row = mega_div64_32(span_set_Strip,
		    span_set->span_row_data_width) * span_set->diff;
		for (span = 0, span_offset = 0; span < raid->spanDepth; span++)
			if (map->raidMap.ldSpanMap[ld].spanBlock[span].
			    block_span_info.noElements >= info + 1) {
				if (strip_offset >=
				    span_set->strip_offset[span])
					span_offset++;
				else
					break;
			}
		mrsas_dprint(sc, MRSAS_PRL11, "AVAGO Debug : Strip 0x%llx, span_set_Strip 0x%llx, span_set_Row 0x%llx "
		    "data width 0x%llx span offset 0x%llx\n", (unsigned long long)strip,
		    (unsigned long long)span_set_Strip,
		    (unsigned long long)span_set_Row,
		    (unsigned long long)span_set->span_row_data_width, (unsigned long long)span_offset);
		mrsas_dprint(sc, MRSAS_PRL11, "AVAGO Debug : For strip 0x%llx row is 0x%llx\n", (unsigned long long)strip,
		    (unsigned long long)span_set->data_row_start +
		    (unsigned long long)span_set_Row + (span_offset - 1));
		return (span_set->data_row_start + span_set_Row + (span_offset - 1));
	}
	return -1LLU;
}


/*
 *
 * This routine calculates the Start Strip for given row using spanset.
 *
 * Inputs:	HBA instance
 * ld:		Logical drive number
 * row:		Row number
 * map:		LD map
 *
 * Outputs :	Strip - Start strip associated with row
 */

static u_int64_t
get_strip_from_row(struct mrsas_softc *sc,
    u_int32_t ld, u_int64_t row, MR_DRV_RAID_MAP_ALL * map)
{
	MR_LD_RAID *raid = MR_LdRaidGet(ld, map);
	LD_SPAN_SET *span_set;
	MR_QUAD_ELEMENT *quad;
	PLD_SPAN_INFO ldSpanInfo = sc->log_to_span;
	u_int32_t span, info;
	u_int64_t strip;

	for (info = 0; info < MAX_QUAD_DEPTH; info++) {
		span_set = &(ldSpanInfo[ld].span_set[info]);

		if (span_set->span_row_data_width == 0)
			break;
		if (row > span_set->data_row_end)
			continue;

		for (span = 0; span < raid->spanDepth; span++)
			if (map->raidMap.ldSpanMap[ld].spanBlock[span].
			    block_span_info.noElements >= info + 1) {
				quad = &map->raidMap.ldSpanMap[ld].
				    spanBlock[span].block_span_info.quad[info];
				if (quad->logStart <= row &&
				    row <= quad->logEnd &&
				    mega_mod64((row - quad->logStart),
				    quad->diff) == 0) {
					strip = mega_div64_32
					    (((row - span_set->data_row_start)
					    - quad->logStart),
					    quad->diff);
					strip *= span_set->span_row_data_width;
					strip += span_set->data_strip_start;
					strip += span_set->strip_offset[span];
					return strip;
				}
			}
	}
	mrsas_dprint(sc, MRSAS_PRL11, "AVAGO Debug - get_strip_from_row: returns invalid "
	    "strip for ld=%x, row=%lx\n", ld, (long unsigned int)row);
	return -1;
}

/*
 * *****************************************************************************
 *
 *
 * This routine calculates the Physical Arm for given strip using spanset.
 *
 * Inputs :	HBA instance
 * 			Logical drive number
 * 			Strip
 * 			LD map
 *
 * Outputs :	Phys Arm - Phys Arm associated with strip
 */

static u_int32_t
get_arm_from_strip(struct mrsas_softc *sc,
    u_int32_t ld, u_int64_t strip, MR_DRV_RAID_MAP_ALL * map)
{
	MR_LD_RAID *raid = MR_LdRaidGet(ld, map);
	LD_SPAN_SET *span_set;
	PLD_SPAN_INFO ldSpanInfo = sc->log_to_span;
	u_int32_t info, strip_offset, span, span_offset;

	for (info = 0; info < MAX_QUAD_DEPTH; info++) {
		span_set = &(ldSpanInfo[ld].span_set[info]);

		if (span_set->span_row_data_width == 0)
			break;
		if (strip > span_set->data_strip_end)
			continue;

		strip_offset = (u_int32_t)mega_mod64
		    ((strip - span_set->data_strip_start),
		    span_set->span_row_data_width);

		for (span = 0, span_offset = 0; span < raid->spanDepth; span++)
			if (map->raidMap.ldSpanMap[ld].spanBlock[span].
			    block_span_info.noElements >= info + 1) {
				if (strip_offset >= span_set->strip_offset[span])
					span_offset = span_set->strip_offset[span];
				else
					break;
			}
		mrsas_dprint(sc, MRSAS_PRL11, "AVAGO PRL11: get_arm_from_strip: "
		    "for ld=0x%x strip=0x%lx arm is  0x%x\n", ld,
		    (long unsigned int)strip, (strip_offset - span_offset));
		return (strip_offset - span_offset);
	}

	mrsas_dprint(sc, MRSAS_PRL11, "AVAGO Debug: - get_arm_from_strip: returns invalid arm"
	    " for ld=%x strip=%lx\n", ld, (long unsigned int)strip);

	return -1;
}


/* This Function will return Phys arm */
u_int8_t
get_arm(struct mrsas_softc *sc, u_int32_t ld, u_int8_t span, u_int64_t stripe,
    MR_DRV_RAID_MAP_ALL * map)
{
	MR_LD_RAID *raid = MR_LdRaidGet(ld, map);

	/* Need to check correct default value */
	u_int32_t arm = 0;

	switch (raid->level) {
	case 0:
	case 5:
	case 6:
		arm = mega_mod64(stripe, SPAN_ROW_SIZE(map, ld, span));
		break;
	case 1:
		/* start with logical arm */
		arm = get_arm_from_strip(sc, ld, stripe, map);
		arm *= 2;
		break;
	}

	return arm;
}

/*
 *
 * This routine calculates the arm, span and block for the specified stripe and
 * reference in stripe using spanset
 *
 * Inputs :
 * sc - HBA instance
 * ld - Logical drive number
 * stripRow: Stripe number
 * stripRef: Reference in stripe
 *
 * Outputs :	span - Span number block - Absolute Block
 * number in the physical disk
 */
static u_int8_t
mr_spanset_get_phy_params(struct mrsas_softc *sc, u_int32_t ld, u_int64_t stripRow,
    u_int16_t stripRef, struct IO_REQUEST_INFO *io_info,
    RAID_CONTEXT * pRAID_Context, MR_DRV_RAID_MAP_ALL * map)
{
	MR_LD_RAID *raid = MR_LdRaidGet(ld, map);
	u_int32_t pd, arRef, r1_alt_pd;
	u_int8_t physArm, span;
	u_int64_t row;
	u_int8_t retval = TRUE;
	u_int64_t *pdBlock = &io_info->pdBlock;
	u_int16_t *pDevHandle = &io_info->devHandle;
	u_int8_t  *pPdInterface = &io_info->pdInterface;

	u_int32_t logArm, rowMod, armQ, arm;

	/* Get row and span from io_info for Uneven Span IO. */
	row = io_info->start_row;
	span = io_info->start_span;


	if (raid->level == 6) {
		logArm = get_arm_from_strip(sc, ld, stripRow, map);
		rowMod = mega_mod64(row, SPAN_ROW_SIZE(map, ld, span));
		armQ = SPAN_ROW_SIZE(map, ld, span) - 1 - rowMod;
		arm = armQ + 1 + logArm;
		if (arm >= SPAN_ROW_SIZE(map, ld, span))
			arm -= SPAN_ROW_SIZE(map, ld, span);
		physArm = (u_int8_t)arm;
	} else
		/* Calculate the arm */
		physArm = get_arm(sc, ld, span, stripRow, map);


	arRef = MR_LdSpanArrayGet(ld, span, map);
	pd = MR_ArPdGet(arRef, physArm, map);

	if (pd != MR_PD_INVALID) {
		*pDevHandle = MR_PdDevHandleGet(pd, map);
		*pPdInterface = MR_PdInterfaceTypeGet(pd, map);
		/* get second pd also for raid 1/10 fast path writes */
		if ((raid->level == 1) && !io_info->isRead) {
			r1_alt_pd = MR_ArPdGet(arRef, physArm + 1, map);
			if (r1_alt_pd != MR_PD_INVALID)
				io_info->r1_alt_dev_handle = MR_PdDevHandleGet(r1_alt_pd, map);
		}
	} else {
		*pDevHandle = MR_DEVHANDLE_INVALID;
		if ((raid->level >= 5) && ((sc->device_id == MRSAS_TBOLT) ||
			(sc->mrsas_gen3_ctrl &&
			raid->regTypeReqOnRead != REGION_TYPE_UNUSED)))
			pRAID_Context->regLockFlags = REGION_TYPE_EXCLUSIVE;
		else if (raid->level == 1) {
			pd = MR_ArPdGet(arRef, physArm + 1, map);
			if (pd != MR_PD_INVALID) {
				*pDevHandle = MR_PdDevHandleGet(pd, map);
				*pPdInterface = MR_PdInterfaceTypeGet(pd, map);
			}
		}
	}

	*pdBlock += stripRef + MR_LdSpanPtrGet(ld, span, map)->startBlk;
	if (sc->is_ventura || sc->is_aero) {
		((RAID_CONTEXT_G35 *) pRAID_Context)->spanArm =
		    (span << RAID_CTX_SPANARM_SPAN_SHIFT) | physArm;
		io_info->span_arm = (span << RAID_CTX_SPANARM_SPAN_SHIFT) | physArm;
	} else {
		pRAID_Context->spanArm = (span << RAID_CTX_SPANARM_SPAN_SHIFT) | physArm;
		io_info->span_arm = pRAID_Context->spanArm;
	}
	return retval;
}

/*
 * MR_BuildRaidContext:	Set up Fast path RAID context
 *
 * This function will initiate command processing.  The start/end row and strip
 * information is calculated then the lock is acquired. This function will
 * return 0 if region lock was acquired OR return num strips.
 */
u_int8_t
MR_BuildRaidContext(struct mrsas_softc *sc, struct IO_REQUEST_INFO *io_info,
    RAID_CONTEXT * pRAID_Context, MR_DRV_RAID_MAP_ALL * map)
{
	MR_LD_RAID *raid;
	u_int32_t ld, stripSize, stripe_mask;
	u_int64_t endLba, endStrip, endRow, start_row, start_strip;
	REGION_KEY regStart;
	REGION_LEN regSize;
	u_int8_t num_strips, numRows;
	u_int16_t ref_in_start_stripe, ref_in_end_stripe;
	u_int64_t ldStartBlock;
	u_int32_t numBlocks, ldTgtId;
	u_int8_t isRead, stripIdx;
	u_int8_t retval = 0;
	u_int8_t startlba_span = SPAN_INVALID;
	u_int64_t *pdBlock = &io_info->pdBlock;
	int error_code = 0;

	ldStartBlock = io_info->ldStartBlock;
	numBlocks = io_info->numBlocks;
	ldTgtId = io_info->ldTgtId;
	isRead = io_info->isRead;

	io_info->IoforUnevenSpan = 0;
	io_info->start_span = SPAN_INVALID;

	ld = MR_TargetIdToLdGet(ldTgtId, map);
	raid = MR_LdRaidGet(ld, map);

	/* check read ahead bit */
	io_info->raCapable = raid->capability.raCapable;

	if (raid->rowDataSize == 0) {
		if (MR_LdSpanPtrGet(ld, 0, map)->spanRowDataSize == 0)
			return FALSE;
		else if (sc->UnevenSpanSupport) {
			io_info->IoforUnevenSpan = 1;
		} else {
			mrsas_dprint(sc, MRSAS_PRL11, "AVAGO Debug: raid->rowDataSize is 0, but has SPAN[0] rowDataSize = 0x%0x,"
			    " but there is _NO_ UnevenSpanSupport\n",
			    MR_LdSpanPtrGet(ld, 0, map)->spanRowDataSize);
			return FALSE;
		}
	}
	stripSize = 1 << raid->stripeShift;
	stripe_mask = stripSize - 1;
	/*
	 * calculate starting row and stripe, and number of strips and rows
	 */
	start_strip = ldStartBlock >> raid->stripeShift;
	ref_in_start_stripe = (u_int16_t)(ldStartBlock & stripe_mask);
	endLba = ldStartBlock + numBlocks - 1;
	ref_in_end_stripe = (u_int16_t)(endLba & stripe_mask);
	endStrip = endLba >> raid->stripeShift;
	num_strips = (u_int8_t)(endStrip - start_strip + 1);	/* End strip */
	if (io_info->IoforUnevenSpan) {
		start_row = get_row_from_strip(sc, ld, start_strip, map);
		endRow = get_row_from_strip(sc, ld, endStrip, map);
		if (raid->spanDepth == 1) {
			startlba_span = 0;
			*pdBlock = start_row << raid->stripeShift;
		} else {
			startlba_span = (u_int8_t)mr_spanset_get_span_block(sc, ld, start_row,
			    pdBlock, map, &error_code);
			if (error_code == 1) {
				mrsas_dprint(sc, MRSAS_PRL11, "AVAGO Debug: return from %s %d. Send IO w/o region lock.\n",
				    __func__, __LINE__);
				return FALSE;
			}
		}
		if (startlba_span == SPAN_INVALID) {
			mrsas_dprint(sc, MRSAS_PRL11, "AVAGO Debug: return from %s %d for row 0x%llx,"
			    "start strip %llx endSrip %llx\n", __func__,
			    __LINE__, (unsigned long long)start_row,
			    (unsigned long long)start_strip,
			    (unsigned long long)endStrip);
			return FALSE;
		}
		io_info->start_span = startlba_span;
		io_info->start_row = start_row;
		mrsas_dprint(sc, MRSAS_PRL11, "AVAGO Debug: Check Span number from %s %d for row 0x%llx, "
		    " start strip 0x%llx endSrip 0x%llx span 0x%x\n",
		    __func__, __LINE__, (unsigned long long)start_row,
		    (unsigned long long)start_strip,
		    (unsigned long long)endStrip, startlba_span);
		mrsas_dprint(sc, MRSAS_PRL11, "AVAGO Debug : 1. start_row 0x%llx endRow 0x%llx Start span 0x%x\n",
		    (unsigned long long)start_row, (unsigned long long)endRow, startlba_span);
	} else {
		start_row = mega_div64_32(start_strip, raid->rowDataSize);
		endRow = mega_div64_32(endStrip, raid->rowDataSize);
	}

	numRows = (u_int8_t)(endRow - start_row + 1);	/* get the row count */

	/*
	 * Calculate region info.  (Assume region at start of first row, and
	 * assume this IO needs the full row - will adjust if not true.)
	 */
	regStart = start_row << raid->stripeShift;
	regSize = stripSize;

	/* Check if we can send this I/O via FastPath */
	if (raid->capability.fpCapable) {
		if (isRead)
			io_info->fpOkForIo = (raid->capability.fpReadCapable &&
			    ((num_strips == 1) ||
			    raid->capability.fpReadAcrossStripe));
		else
			io_info->fpOkForIo = (raid->capability.fpWriteCapable &&
			    ((num_strips == 1) ||
			    raid->capability.fpWriteAcrossStripe));
	} else
		io_info->fpOkForIo = FALSE;

	if (numRows == 1) {
		if (num_strips == 1) {
			regStart += ref_in_start_stripe;
			regSize = numBlocks;
		}
	} else if (io_info->IoforUnevenSpan == 0) {
		/*
		 * For Even span region lock optimization. If the start strip
		 * is the last in the start row
		 */
		if (start_strip == (start_row + 1) * raid->rowDataSize - 1) {
			regStart += ref_in_start_stripe;
			/*
			 * initialize count to sectors from startRef to end
			 * of strip
			 */
			regSize = stripSize - ref_in_start_stripe;
		}
		/* add complete rows in the middle of the transfer */
		if (numRows > 2)
			regSize += (numRows - 2) << raid->stripeShift;

		/* if IO ends within first strip of last row */
		if (endStrip == endRow * raid->rowDataSize)
			regSize += ref_in_end_stripe + 1;
		else
			regSize += stripSize;
	} else {
		if (start_strip == (get_strip_from_row(sc, ld, start_row, map) +
		    SPAN_ROW_DATA_SIZE(map, ld, startlba_span) - 1)) {
			regStart += ref_in_start_stripe;
			/*
			 * initialize count to sectors from startRef to end
			 * of strip
			 */
			regSize = stripSize - ref_in_start_stripe;
		}
		/* add complete rows in the middle of the transfer */
		if (numRows > 2)
			regSize += (numRows - 2) << raid->stripeShift;

		/* if IO ends within first strip of last row */
		if (endStrip == get_strip_from_row(sc, ld, endRow, map))
			regSize += ref_in_end_stripe + 1;
		else
			regSize += stripSize;
	}
	pRAID_Context->timeoutValue = map->raidMap.fpPdIoTimeoutSec;
	if (sc->mrsas_gen3_ctrl)
		pRAID_Context->regLockFlags = (isRead) ? raid->regTypeReqOnRead : raid->regTypeReqOnWrite;
	else if (sc->device_id == MRSAS_TBOLT)
		pRAID_Context->regLockFlags = (isRead) ? REGION_TYPE_SHARED_READ : raid->regTypeReqOnWrite;
	pRAID_Context->VirtualDiskTgtId = raid->targetId;
	pRAID_Context->regLockRowLBA = regStart;
	pRAID_Context->regLockLength = regSize;
	pRAID_Context->configSeqNum = raid->seqNum;

	/*
	 * Get Phy Params only if FP capable, or else leave it to MR firmware
	 * to do the calculation.
	 */
	if (io_info->fpOkForIo) {
		retval = io_info->IoforUnevenSpan ?
		    mr_spanset_get_phy_params(sc, ld, start_strip,
		    ref_in_start_stripe, io_info, pRAID_Context, map) :
		    MR_GetPhyParams(sc, ld, start_strip,
		    ref_in_start_stripe, io_info, pRAID_Context, map);
		/* If IO on an invalid Pd, then FP is not possible */
		if (io_info->devHandle == MR_DEVHANDLE_INVALID)
			io_info->fpOkForIo = FALSE;
		/*
		 * if FP possible, set the SLUD bit in regLockFlags for
		 * ventura
		 */
		else if ((sc->is_ventura || sc->is_aero) && !isRead &&
			    (raid->writeMode == MR_RL_WRITE_BACK_MODE) && (raid->level <= 1) &&
		    raid->capability.fpCacheBypassCapable) {
			((RAID_CONTEXT_G35 *) pRAID_Context)->routingFlags.bits.sld = 1;
		}

		return retval;
	} else if (isRead) {
		for (stripIdx = 0; stripIdx < num_strips; stripIdx++) {
			retval = io_info->IoforUnevenSpan ?
			    mr_spanset_get_phy_params(sc, ld, start_strip + stripIdx,
			    ref_in_start_stripe, io_info, pRAID_Context, map) :
			    MR_GetPhyParams(sc, ld, start_strip + stripIdx,
			    ref_in_start_stripe, io_info, pRAID_Context, map);
			if (!retval)
				return TRUE;
		}
	}
#if SPAN_DEBUG
	/* Just for testing what arm we get for strip. */
	get_arm_from_strip(sc, ld, start_strip, map);
#endif
	return TRUE;
}

/*
 *
 * This routine pepare spanset info from Valid Raid map and store it into local
 * copy of ldSpanInfo per instance data structure.
 *
 * Inputs :	LD map
 * 			ldSpanInfo per HBA instance
 *
 */
void
mr_update_span_set(MR_DRV_RAID_MAP_ALL * map, PLD_SPAN_INFO ldSpanInfo)
{
	u_int8_t span, count;
	u_int32_t element, span_row_width;
	u_int64_t span_row;
	MR_LD_RAID *raid;
	LD_SPAN_SET *span_set, *span_set_prev;
	MR_QUAD_ELEMENT *quad;
	int ldCount;
	u_int16_t ld;

	for (ldCount = 0; ldCount < MAX_LOGICAL_DRIVES; ldCount++) {
		ld = MR_TargetIdToLdGet(ldCount, map);
		if (ld >= MAX_LOGICAL_DRIVES)
			continue;
		raid = MR_LdRaidGet(ld, map);
		for (element = 0; element < MAX_QUAD_DEPTH; element++) {
			for (span = 0; span < raid->spanDepth; span++) {
				if (map->raidMap.ldSpanMap[ld].spanBlock[span].
				    block_span_info.noElements < element + 1)
					continue;
				/* TO-DO */
				span_set = &(ldSpanInfo[ld].span_set[element]);
				quad = &map->raidMap.ldSpanMap[ld].
				    spanBlock[span].block_span_info.quad[element];

				span_set->diff = quad->diff;

				for (count = 0, span_row_width = 0;
				    count < raid->spanDepth; count++) {
					if (map->raidMap.ldSpanMap[ld].spanBlock[count].
					    block_span_info.noElements >= element + 1) {
						span_set->strip_offset[count] = span_row_width;
						span_row_width +=
						    MR_LdSpanPtrGet(ld, count, map)->spanRowDataSize;
#if SPAN_DEBUG
						printf("AVAGO Debug span %x rowDataSize %x\n", count,
						    MR_LdSpanPtrGet(ld, count, map)->spanRowDataSize);
#endif
					}
				}

				span_set->span_row_data_width = span_row_width;
				span_row = mega_div64_32(((quad->logEnd -
				    quad->logStart) + quad->diff), quad->diff);

				if (element == 0) {
					span_set->log_start_lba = 0;
					span_set->log_end_lba =
					    ((span_row << raid->stripeShift) * span_row_width) - 1;

					span_set->span_row_start = 0;
					span_set->span_row_end = span_row - 1;

					span_set->data_strip_start = 0;
					span_set->data_strip_end = (span_row * span_row_width) - 1;

					span_set->data_row_start = 0;
					span_set->data_row_end = (span_row * quad->diff) - 1;
				} else {
					span_set_prev = &(ldSpanInfo[ld].span_set[element - 1]);
					span_set->log_start_lba = span_set_prev->log_end_lba + 1;
					span_set->log_end_lba = span_set->log_start_lba +
					    ((span_row << raid->stripeShift) * span_row_width) - 1;

					span_set->span_row_start = span_set_prev->span_row_end + 1;
					span_set->span_row_end =
					    span_set->span_row_start + span_row - 1;

					span_set->data_strip_start =
					    span_set_prev->data_strip_end + 1;
					span_set->data_strip_end = span_set->data_strip_start +
					    (span_row * span_row_width) - 1;

					span_set->data_row_start = span_set_prev->data_row_end + 1;
					span_set->data_row_end = span_set->data_row_start +
					    (span_row * quad->diff) - 1;
				}
				break;
			}
			if (span == raid->spanDepth)
				break;	/* no quads remain */
		}
	}
#if SPAN_DEBUG
	getSpanInfo(map, ldSpanInfo);	/* to get span set info */
#endif
}

/*
 * mrsas_update_load_balance_params:	Update load balance parmas
 * Inputs:
 * sc - driver softc instance
 * drv_map - driver RAID map
 * lbInfo - Load balance info
 *
 * This function updates the load balance parameters for the LD config of a two
 * drive optimal RAID-1.
 */
void
mrsas_update_load_balance_params(struct mrsas_softc *sc,
    MR_DRV_RAID_MAP_ALL * drv_map, PLD_LOAD_BALANCE_INFO lbInfo)
{
	int ldCount;
	u_int16_t ld;
	MR_LD_RAID *raid;

	if (sc->lb_pending_cmds > 128 || sc->lb_pending_cmds < 1)
		sc->lb_pending_cmds = LB_PENDING_CMDS_DEFAULT;

	for (ldCount = 0; ldCount < MAX_LOGICAL_DRIVES_EXT; ldCount++) {
		ld = MR_TargetIdToLdGet(ldCount, drv_map);
		if (ld >= MAX_LOGICAL_DRIVES_EXT) {
			lbInfo[ldCount].loadBalanceFlag = 0;
			continue;
		}
		raid = MR_LdRaidGet(ld, drv_map);
		if ((raid->level != 1) ||
		    (raid->ldState != MR_LD_STATE_OPTIMAL)) {
			lbInfo[ldCount].loadBalanceFlag = 0;
			continue;
		}
		lbInfo[ldCount].loadBalanceFlag = 1;
	}
}


/*
 * mrsas_set_pd_lba:	Sets PD LBA
 * input:				io_request pointer
 * 						CDB length
 * 						io_info pointer
 * 						Pointer to CCB
 * 						Local RAID map pointer
 * 						Start block of IO Block Size
 *
 * Used to set the PD logical block address in CDB for FP IOs.
 */
void
mrsas_set_pd_lba(MRSAS_RAID_SCSI_IO_REQUEST * io_request, u_int8_t cdb_len,
    struct IO_REQUEST_INFO *io_info, union ccb *ccb,
    MR_DRV_RAID_MAP_ALL * local_map_ptr, u_int32_t ref_tag,
    u_int32_t ld_block_size)
{
	MR_LD_RAID *raid;
	u_int32_t ld;
	u_int64_t start_blk = io_info->pdBlock;
	u_int8_t *cdb = io_request->CDB.CDB32;
	u_int32_t num_blocks = io_info->numBlocks;
	u_int8_t opcode = 0, flagvals = 0, groupnum = 0, control = 0;
	struct ccb_hdr *ccb_h = &(ccb->ccb_h);

	/* Check if T10 PI (DIF) is enabled for this LD */
	ld = MR_TargetIdToLdGet(io_info->ldTgtId, local_map_ptr);
	raid = MR_LdRaidGet(ld, local_map_ptr);
	if (raid->capability.ldPiMode == MR_PROT_INFO_TYPE_CONTROLLER) {
		memset(cdb, 0, sizeof(io_request->CDB.CDB32));
		cdb[0] = MRSAS_SCSI_VARIABLE_LENGTH_CMD;
		cdb[7] = MRSAS_SCSI_ADDL_CDB_LEN;

		if (ccb_h->flags == CAM_DIR_OUT)
			cdb[9] = MRSAS_SCSI_SERVICE_ACTION_READ32;
		else
			cdb[9] = MRSAS_SCSI_SERVICE_ACTION_WRITE32;
		cdb[10] = MRSAS_RD_WR_PROTECT_CHECK_ALL;

		/* LBA */
		cdb[12] = (u_int8_t)((start_blk >> 56) & 0xff);
		cdb[13] = (u_int8_t)((start_blk >> 48) & 0xff);
		cdb[14] = (u_int8_t)((start_blk >> 40) & 0xff);
		cdb[15] = (u_int8_t)((start_blk >> 32) & 0xff);
		cdb[16] = (u_int8_t)((start_blk >> 24) & 0xff);
		cdb[17] = (u_int8_t)((start_blk >> 16) & 0xff);
		cdb[18] = (u_int8_t)((start_blk >> 8) & 0xff);
		cdb[19] = (u_int8_t)(start_blk & 0xff);

		/* Logical block reference tag */
		io_request->CDB.EEDP32.PrimaryReferenceTag = swap32(ref_tag);
		io_request->CDB.EEDP32.PrimaryApplicationTagMask = 0xffff;
		io_request->IoFlags = 32;	/* Specify 32-byte cdb */

		/* Transfer length */
		cdb[28] = (u_int8_t)((num_blocks >> 24) & 0xff);
		cdb[29] = (u_int8_t)((num_blocks >> 16) & 0xff);
		cdb[30] = (u_int8_t)((num_blocks >> 8) & 0xff);
		cdb[31] = (u_int8_t)(num_blocks & 0xff);

		/* set SCSI IO EEDP Flags */
		if (ccb_h->flags == CAM_DIR_OUT) {
			io_request->EEDPFlags =
			    MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
			    MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG |
			    MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP |
			    MPI2_SCSIIO_EEDPFLAGS_CHECK_APPTAG |
			    MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;
		} else {
			io_request->EEDPFlags =
			    MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
			    MPI2_SCSIIO_EEDPFLAGS_INSERT_OP;
		}
		io_request->Control |= (0x4 << 26);
		io_request->EEDPBlockSize = ld_block_size;
	} else {
		/* Some drives don't support 16/12 byte CDB's, convert to 10 */
		if (((cdb_len == 12) || (cdb_len == 16)) &&
		    (start_blk <= 0xffffffff)) {
			if (cdb_len == 16) {
				opcode = cdb[0] == READ_16 ? READ_10 : WRITE_10;
				flagvals = cdb[1];
				groupnum = cdb[14];
				control = cdb[15];
			} else {
				opcode = cdb[0] == READ_12 ? READ_10 : WRITE_10;
				flagvals = cdb[1];
				groupnum = cdb[10];
				control = cdb[11];
			}

			memset(cdb, 0, sizeof(io_request->CDB.CDB32));

			cdb[0] = opcode;
			cdb[1] = flagvals;
			cdb[6] = groupnum;
			cdb[9] = control;

			/* Transfer length */
			cdb[8] = (u_int8_t)(num_blocks & 0xff);
			cdb[7] = (u_int8_t)((num_blocks >> 8) & 0xff);

			io_request->IoFlags = 10;	/* Specify 10-byte cdb */
			cdb_len = 10;
		} else if ((cdb_len < 16) && (start_blk > 0xffffffff)) {
			/* Convert to 16 byte CDB for large LBA's */
			switch (cdb_len) {
			case 6:
				opcode = cdb[0] == READ_6 ? READ_16 : WRITE_16;
				control = cdb[5];
				break;
			case 10:
				opcode = cdb[0] == READ_10 ? READ_16 : WRITE_16;
				flagvals = cdb[1];
				groupnum = cdb[6];
				control = cdb[9];
				break;
			case 12:
				opcode = cdb[0] == READ_12 ? READ_16 : WRITE_16;
				flagvals = cdb[1];
				groupnum = cdb[10];
				control = cdb[11];
				break;
			}

			memset(cdb, 0, sizeof(io_request->CDB.CDB32));

			cdb[0] = opcode;
			cdb[1] = flagvals;
			cdb[14] = groupnum;
			cdb[15] = control;

			/* Transfer length */
			cdb[13] = (u_int8_t)(num_blocks & 0xff);
			cdb[12] = (u_int8_t)((num_blocks >> 8) & 0xff);
			cdb[11] = (u_int8_t)((num_blocks >> 16) & 0xff);
			cdb[10] = (u_int8_t)((num_blocks >> 24) & 0xff);

			io_request->IoFlags = 16;	/* Specify 16-byte cdb */
			cdb_len = 16;
		} else if ((cdb_len == 6) && (start_blk > 0x1fffff)) {
			/* convert to 10 byte CDB */
			opcode = cdb[0] == READ_6 ? READ_10 : WRITE_10;
			control = cdb[5];

			memset(cdb, 0, sizeof(io_request->CDB.CDB32));
			cdb[0] = opcode;
			cdb[9] = control;

			/* Set transfer length */
			cdb[8] = (u_int8_t)(num_blocks & 0xff);
			cdb[7] = (u_int8_t)((num_blocks >> 8) & 0xff);

			/* Specify 10-byte cdb */
			cdb_len = 10;
		}
		/* Fall through normal case, just load LBA here */
		u_int8_t val = cdb[1] & 0xE0;

		switch (cdb_len) {
		case 6:
			cdb[3] = (u_int8_t)(start_blk & 0xff);
			cdb[2] = (u_int8_t)((start_blk >> 8) & 0xff);
			cdb[1] = val | ((u_int8_t)(start_blk >> 16) & 0x1f);
			break;
		case 10:
			cdb[5] = (u_int8_t)(start_blk & 0xff);
			cdb[4] = (u_int8_t)((start_blk >> 8) & 0xff);
			cdb[3] = (u_int8_t)((start_blk >> 16) & 0xff);
			cdb[2] = (u_int8_t)((start_blk >> 24) & 0xff);
			break;
		case 16:
			cdb[9] = (u_int8_t)(start_blk & 0xff);
			cdb[8] = (u_int8_t)((start_blk >> 8) & 0xff);
			cdb[7] = (u_int8_t)((start_blk >> 16) & 0xff);
			cdb[6] = (u_int8_t)((start_blk >> 24) & 0xff);
			cdb[5] = (u_int8_t)((start_blk >> 32) & 0xff);
			cdb[4] = (u_int8_t)((start_blk >> 40) & 0xff);
			cdb[3] = (u_int8_t)((start_blk >> 48) & 0xff);
			cdb[2] = (u_int8_t)((start_blk >> 56) & 0xff);
			break;
		}
	}
}

/*
 * mrsas_get_best_arm_pd:	Determine the best spindle arm
 * Inputs:
 *    sc - HBA instance
 *    lbInfo - Load balance info
 *    io_info - IO request info
 *
 * This function determines and returns the best arm by looking at the
 * parameters of the last PD access.
 */
u_int8_t 
mrsas_get_best_arm_pd(struct mrsas_softc *sc,
    PLD_LOAD_BALANCE_INFO lbInfo, struct IO_REQUEST_INFO *io_info)
{
	MR_LD_RAID *raid;
	MR_DRV_RAID_MAP_ALL *drv_map;
	u_int16_t pd1_devHandle;
	u_int16_t pend0, pend1, ld;
	u_int64_t diff0, diff1;
	u_int8_t bestArm, pd0, pd1, span, arm;
	u_int32_t arRef, span_row_size;

	u_int64_t block = io_info->ldStartBlock;
	u_int32_t count = io_info->numBlocks;

	span = ((io_info->span_arm & RAID_CTX_SPANARM_SPAN_MASK)
	    >> RAID_CTX_SPANARM_SPAN_SHIFT);
	arm = (io_info->span_arm & RAID_CTX_SPANARM_ARM_MASK);

	drv_map = sc->ld_drv_map[(sc->map_id & 1)];
	ld = MR_TargetIdToLdGet(io_info->ldTgtId, drv_map);
	raid = MR_LdRaidGet(ld, drv_map);
	span_row_size = sc->UnevenSpanSupport ?
	    SPAN_ROW_SIZE(drv_map, ld, span) : raid->rowSize;

	arRef = MR_LdSpanArrayGet(ld, span, drv_map);
	pd0 = MR_ArPdGet(arRef, arm, drv_map);
	pd1 = MR_ArPdGet(arRef, (arm + 1) >= span_row_size ?
	    (arm + 1 - span_row_size) : arm + 1, drv_map);

	/* Get PD1 Dev Handle */
	pd1_devHandle = MR_PdDevHandleGet(pd1, drv_map);
	if (pd1_devHandle == MR_DEVHANDLE_INVALID) {
		bestArm = arm;
	} else {
		/* get the pending cmds for the data and mirror arms */
		pend0 = mrsas_atomic_read(&lbInfo->scsi_pending_cmds[pd0]);
		pend1 = mrsas_atomic_read(&lbInfo->scsi_pending_cmds[pd1]);

		/* Determine the disk whose head is nearer to the req. block */
		diff0 = ABS_DIFF(block, lbInfo->last_accessed_block[pd0]);
		diff1 = ABS_DIFF(block, lbInfo->last_accessed_block[pd1]);
		bestArm = (diff0 <= diff1 ? arm : arm ^ 1);

		if ((bestArm == arm && pend0 > pend1 + sc->lb_pending_cmds) ||
		    (bestArm != arm && pend1 > pend0 + sc->lb_pending_cmds))
			bestArm ^= 1;

		/* Update the last accessed block on the correct pd */
		io_info->span_arm = (span << RAID_CTX_SPANARM_SPAN_SHIFT) | bestArm;
		io_info->pd_after_lb = (bestArm == arm) ? pd0 : pd1;
	}

	lbInfo->last_accessed_block[bestArm == arm ? pd0 : pd1] = block + count - 1;
#if SPAN_DEBUG
	if (arm != bestArm)
		printf("AVAGO Debug R1 Load balance occur - span 0x%x arm 0x%x bestArm 0x%x "
		    "io_info->span_arm 0x%x\n",
		    span, arm, bestArm, io_info->span_arm);
#endif

	return io_info->pd_after_lb;
}

/*
 * mrsas_get_updated_dev_handle:	Get the update dev handle
 * Inputs:
 *	sc - Adapter instance soft state
 *	lbInfo - Load balance info
 *	io_info - io_info pointer
 *
 * This function determines and returns the updated dev handle.
 */
u_int16_t 
mrsas_get_updated_dev_handle(struct mrsas_softc *sc,
    PLD_LOAD_BALANCE_INFO lbInfo, struct IO_REQUEST_INFO *io_info)
{
	u_int8_t arm_pd;
	u_int16_t devHandle;
	MR_DRV_RAID_MAP_ALL *drv_map;

	drv_map = sc->ld_drv_map[(sc->map_id & 1)];

	/* get best new arm */
	arm_pd = mrsas_get_best_arm_pd(sc, lbInfo, io_info);
	devHandle = MR_PdDevHandleGet(arm_pd, drv_map);
	io_info->pdInterface = MR_PdInterfaceTypeGet(arm_pd, drv_map);
	mrsas_atomic_inc(&lbInfo->scsi_pending_cmds[arm_pd]);

	return devHandle;
}

/*
 * MR_GetPhyParams:	Calculates arm, span, and block
 * Inputs:			Adapter soft state
 * 					Logical drive number (LD)
 * 					Stripe number(stripRow)
 * 					Reference in stripe (stripRef)
 *
 * Outputs:			Absolute Block number in the physical disk
 *
 * This routine calculates the arm, span and block for the specified stripe and
 * reference in stripe.
 */
u_int8_t
MR_GetPhyParams(struct mrsas_softc *sc, u_int32_t ld,
    u_int64_t stripRow,
    u_int16_t stripRef, struct IO_REQUEST_INFO *io_info,
    RAID_CONTEXT * pRAID_Context, MR_DRV_RAID_MAP_ALL * map)
{
	MR_LD_RAID *raid = MR_LdRaidGet(ld, map);
	u_int32_t pd, arRef, r1_alt_pd;
	u_int8_t physArm, span;
	u_int64_t row;
	u_int8_t retval = TRUE;
	int error_code = 0;
	u_int64_t *pdBlock = &io_info->pdBlock;
	u_int16_t *pDevHandle = &io_info->devHandle;
	u_int8_t  *pPdInterface = &io_info->pdInterface;
	u_int32_t rowMod, armQ, arm, logArm;

	row = mega_div64_32(stripRow, raid->rowDataSize);

	if (raid->level == 6) {
		/* logical arm within row */
		logArm = mega_mod64(stripRow, raid->rowDataSize);
		if (raid->rowSize == 0)
			return FALSE;
		rowMod = mega_mod64(row, raid->rowSize);	/* get logical row mod */
		armQ = raid->rowSize - 1 - rowMod;	/* index of Q drive */
		arm = armQ + 1 + logArm;/* data always logically follows Q */
		if (arm >= raid->rowSize)	/* handle wrap condition */
			arm -= raid->rowSize;
		physArm = (u_int8_t)arm;
	} else {
		if (raid->modFactor == 0)
			return FALSE;
		physArm = MR_LdDataArmGet(ld, mega_mod64(stripRow, raid->modFactor), map);
	}

	if (raid->spanDepth == 1) {
		span = 0;
		*pdBlock = row << raid->stripeShift;
	} else {
		span = (u_int8_t)MR_GetSpanBlock(ld, row, pdBlock, map, &error_code);
		if (error_code == 1)
			return FALSE;
	}

	/* Get the array on which this span is present */
	arRef = MR_LdSpanArrayGet(ld, span, map);

	pd = MR_ArPdGet(arRef, physArm, map);	/* Get the Pd. */

	if (pd != MR_PD_INVALID) {
		/* Get dev handle from Pd */
		*pDevHandle = MR_PdDevHandleGet(pd, map);
		*pPdInterface = MR_PdInterfaceTypeGet(pd, map);
		/* get second pd also for raid 1/10 fast path writes */
		if ((raid->level == 1) && !io_info->isRead) {
			r1_alt_pd = MR_ArPdGet(arRef, physArm + 1, map);
			if (r1_alt_pd != MR_PD_INVALID)
				io_info->r1_alt_dev_handle = MR_PdDevHandleGet(r1_alt_pd, map);
		}
	} else {
		*pDevHandle = MR_DEVHANDLE_INVALID;	/* set dev handle as invalid. */
		if ((raid->level >= 5) && ((sc->device_id == MRSAS_TBOLT) ||
			(sc->mrsas_gen3_ctrl &&
			raid->regTypeReqOnRead != REGION_TYPE_UNUSED)))
			pRAID_Context->regLockFlags = REGION_TYPE_EXCLUSIVE;
		else if (raid->level == 1) {
			/* Get Alternate Pd. */
			pd = MR_ArPdGet(arRef, physArm + 1, map);
			if (pd != MR_PD_INVALID) {
				/* Get dev handle from Pd. */
				*pDevHandle = MR_PdDevHandleGet(pd, map);
				*pPdInterface = MR_PdInterfaceTypeGet(pd, map);
			}
		}
	}

	*pdBlock += stripRef + MR_LdSpanPtrGet(ld, span, map)->startBlk;
	if (sc->is_ventura || sc->is_aero) {
		((RAID_CONTEXT_G35 *) pRAID_Context)->spanArm =
		    (span << RAID_CTX_SPANARM_SPAN_SHIFT) | physArm;
		io_info->span_arm = (span << RAID_CTX_SPANARM_SPAN_SHIFT) | physArm;
	} else {
		pRAID_Context->spanArm = (span << RAID_CTX_SPANARM_SPAN_SHIFT) | physArm;
		io_info->span_arm = pRAID_Context->spanArm;
	}
	return retval;
}

/*
 * MR_GetSpanBlock:	Calculates span block
 * Inputs:			LD
 * 					row PD
 * 					span block
 * 					RAID map pointer
 *
 * Outputs:			Span number Error code
 *
 * This routine calculates the span from the span block info.
 */
u_int32_t
MR_GetSpanBlock(u_int32_t ld, u_int64_t row, u_int64_t *span_blk,
    MR_DRV_RAID_MAP_ALL * map, int *div_error)
{
	MR_SPAN_BLOCK_INFO *pSpanBlock = MR_LdSpanInfoGet(ld, map);
	MR_QUAD_ELEMENT *quad;
	MR_LD_RAID *raid = MR_LdRaidGet(ld, map);
	u_int32_t span, j;
	u_int64_t blk, debugBlk;

	for (span = 0; span < raid->spanDepth; span++, pSpanBlock++) {
		for (j = 0; j < pSpanBlock->block_span_info.noElements; j++) {
			quad = &pSpanBlock->block_span_info.quad[j];
			if (quad->diff == 0) {
				*div_error = 1;
				return span;
			}
			if (quad->logStart <= row && row <= quad->logEnd &&
			    (mega_mod64(row - quad->logStart, quad->diff)) == 0) {
				if (span_blk != NULL) {
					blk = mega_div64_32((row - quad->logStart), quad->diff);
					debugBlk = blk;
					blk = (blk + quad->offsetInSpan) << raid->stripeShift;
					*span_blk = blk;
				}
				return span;
			}
		}
	}
	return span;
}
