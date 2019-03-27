/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2015 LSI Corp.
 * Copyright (c) 2013-2015 Avago Technologies
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * Avago Technologies (LSI) MPT-Fusion Host Adapter FreeBSD
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* TODO Move headers to mpsvar */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>
#include <sys/eventhandler.h>
#include <sys/uio.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <dev/mps/mpi/mpi2_type.h>
#include <dev/mps/mpi/mpi2.h>
#include <dev/mps/mpi/mpi2_ioc.h>
#include <dev/mps/mpi/mpi2_sas.h>
#include <dev/mps/mpi/mpi2_cnfg.h>
#include <dev/mps/mpi/mpi2_init.h>
#include <dev/mps/mpi/mpi2_tool.h>
#include <dev/mps/mps_ioctl.h>
#include <dev/mps/mpsvar.h>
#include <dev/mps/mps_mapping.h>

/**
 * _mapping_clear_map_entry - Clear a particular mapping entry.
 * @map_entry: map table entry
 *
 * Returns nothing.
 */
static inline void
_mapping_clear_map_entry(struct dev_mapping_table *map_entry)
{
	map_entry->physical_id = 0;
	map_entry->device_info = 0;
	map_entry->phy_bits = 0;
	map_entry->dpm_entry_num = MPS_DPM_BAD_IDX;
	map_entry->dev_handle = 0;
	map_entry->id = -1;
	map_entry->missing_count = 0;
	map_entry->init_complete = 0;
	map_entry->TLR_bits = (u8)MPI2_SCSIIO_CONTROL_NO_TLR;
}

/**
 * _mapping_clear_enc_entry - Clear a particular enclosure table entry.
 * @enc_entry: enclosure table entry
 *
 * Returns nothing.
 */
static inline void
_mapping_clear_enc_entry(struct enc_mapping_table *enc_entry)
{
	enc_entry->enclosure_id = 0;
	enc_entry->start_index = MPS_MAPTABLE_BAD_IDX;
	enc_entry->phy_bits = 0;
	enc_entry->dpm_entry_num = MPS_DPM_BAD_IDX;
	enc_entry->enc_handle = 0;
	enc_entry->num_slots = 0;
	enc_entry->start_slot = 0;
	enc_entry->missing_count = 0;
	enc_entry->removal_flag = 0;
	enc_entry->skip_search = 0;
	enc_entry->init_complete = 0;
}

/**
 * _mapping_commit_enc_entry - write a particular enc entry in DPM page0.
 * @sc: per adapter object
 * @enc_entry: enclosure table entry
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_mapping_commit_enc_entry(struct mps_softc *sc,
    struct enc_mapping_table *et_entry)
{
	Mpi2DriverMap0Entry_t *dpm_entry;
	struct dev_mapping_table *mt_entry;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2DriverMappingPage0_t config_page;

	if (!sc->is_dpm_enable)
		return 0;

	memset(&config_page, 0, sizeof(Mpi2DriverMappingPage0_t));
	memcpy(&config_page.Header, (u8 *) sc->dpm_pg0,
	    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
	dpm_entry = (Mpi2DriverMap0Entry_t *)((u8 *)sc->dpm_pg0 +
	    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
	dpm_entry += et_entry->dpm_entry_num;
	dpm_entry->PhysicalIdentifier.Low =
	    ( 0xFFFFFFFF & et_entry->enclosure_id);
	dpm_entry->PhysicalIdentifier.High =
	    ( et_entry->enclosure_id >> 32);
	mt_entry = &sc->mapping_table[et_entry->start_index];
	dpm_entry->DeviceIndex = htole16(mt_entry->id);
	dpm_entry->MappingInformation = et_entry->num_slots;
	dpm_entry->MappingInformation <<= MPI2_DRVMAP0_MAPINFO_SLOT_SHIFT;
	dpm_entry->MappingInformation |= et_entry->missing_count;
	dpm_entry->MappingInformation = htole16(dpm_entry->MappingInformation);
	dpm_entry->PhysicalBitsMapping = htole32(et_entry->phy_bits);
	dpm_entry->Reserved1 = 0;

	mps_dprint(sc, MPS_MAPPING, "%s: Writing DPM entry %d for enclosure.\n",
	    __func__, et_entry->dpm_entry_num);
	memcpy(&config_page.Entry, (u8 *)dpm_entry,
	    sizeof(Mpi2DriverMap0Entry_t));
	if (mps_config_set_dpm_pg0(sc, &mpi_reply, &config_page,
	    et_entry->dpm_entry_num)) {
		mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: Write of DPM "
		    "entry %d for enclosure failed.\n", __func__,
		    et_entry->dpm_entry_num);
		dpm_entry->MappingInformation = le16toh(dpm_entry->
		    MappingInformation);
		dpm_entry->DeviceIndex = le16toh(dpm_entry->DeviceIndex);
		dpm_entry->PhysicalBitsMapping =
		    le32toh(dpm_entry->PhysicalBitsMapping);
		return -1;
	}
	dpm_entry->MappingInformation = le16toh(dpm_entry->
	    MappingInformation);
	dpm_entry->DeviceIndex = le16toh(dpm_entry->DeviceIndex);
	dpm_entry->PhysicalBitsMapping =
	    le32toh(dpm_entry->PhysicalBitsMapping);
	return 0;
}

/**
 * _mapping_commit_map_entry - write a particular map table entry in DPM page0.
 * @sc: per adapter object
 * @mt_entry: mapping table entry
 *
 * Returns 0 for success, non-zero for failure.
 */

static int
_mapping_commit_map_entry(struct mps_softc *sc,
    struct dev_mapping_table *mt_entry)
{
	Mpi2DriverMap0Entry_t *dpm_entry;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2DriverMappingPage0_t config_page;

	if (!sc->is_dpm_enable)
		return 0;

	/*
	 * It's possible that this Map Entry points to a BAD DPM index. This
	 * can happen if the Map Entry is a for a missing device and the DPM
	 * entry that was being used by this device is now being used by some
	 * new device. So, check for a BAD DPM index and just return if so.
	 */
	if (mt_entry->dpm_entry_num == MPS_DPM_BAD_IDX) {
		mps_dprint(sc, MPS_MAPPING, "%s: DPM entry location for target "
		    "%d is invalid. DPM will not be written.\n", __func__,
		    mt_entry->id);
		return 0;
	}

	memset(&config_page, 0, sizeof(Mpi2DriverMappingPage0_t));
	memcpy(&config_page.Header, (u8 *)sc->dpm_pg0,
	    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
	dpm_entry = (Mpi2DriverMap0Entry_t *)((u8 *) sc->dpm_pg0 +
	    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
	dpm_entry = dpm_entry + mt_entry->dpm_entry_num;
	dpm_entry->PhysicalIdentifier.Low = (0xFFFFFFFF &
	    mt_entry->physical_id);
	dpm_entry->PhysicalIdentifier.High = (mt_entry->physical_id >> 32);
	dpm_entry->DeviceIndex = htole16(mt_entry->id);
	dpm_entry->MappingInformation = htole16(mt_entry->missing_count);
	dpm_entry->PhysicalBitsMapping = 0;
	dpm_entry->Reserved1 = 0;
	memcpy(&config_page.Entry, (u8 *)dpm_entry,
	    sizeof(Mpi2DriverMap0Entry_t));

	mps_dprint(sc, MPS_MAPPING, "%s: Writing DPM entry %d for target %d.\n",
	    __func__, mt_entry->dpm_entry_num, mt_entry->id);
	if (mps_config_set_dpm_pg0(sc, &mpi_reply, &config_page,
	    mt_entry->dpm_entry_num)) {
		mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: Write of DPM "
		    "entry %d for target %d failed.\n", __func__,
		    mt_entry->dpm_entry_num, mt_entry->id);
		dpm_entry->MappingInformation = le16toh(dpm_entry->
		    MappingInformation);
		dpm_entry->DeviceIndex = le16toh(dpm_entry->DeviceIndex);
		return -1;
	}

	dpm_entry->MappingInformation = le16toh(dpm_entry->MappingInformation);
	dpm_entry->DeviceIndex = le16toh(dpm_entry->DeviceIndex);
	return 0;
}

/**
 * _mapping_get_ir_maprange - get start and end index for IR map range.
 * @sc: per adapter object
 * @start_idx: place holder for start index
 * @end_idx: place holder for end index
 *
 * The IR volumes can be mapped either at start or end of the mapping table
 * this function gets the detail of where IR volume mapping starts and ends
 * in the device mapping table
 *
 * Returns nothing.
 */
static void
_mapping_get_ir_maprange(struct mps_softc *sc, u32 *start_idx, u32 *end_idx)
{
	u16 volume_mapping_flags;
	u16 ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);

	volume_mapping_flags = le16toh(sc->ioc_pg8.IRVolumeMappingFlags) &
	    MPI2_IOCPAGE8_IRFLAGS_MASK_VOLUME_MAPPING_MODE;
	if (volume_mapping_flags == MPI2_IOCPAGE8_IRFLAGS_LOW_VOLUME_MAPPING) {
		*start_idx = 0;
		if (ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_RESERVED_TARGETID_0)
			*start_idx = 1;
	} else
		*start_idx = sc->max_devices - sc->max_volumes;
	*end_idx = *start_idx + sc->max_volumes - 1;
}

/**
 * _mapping_get_enc_idx_from_id - get enclosure index from enclosure ID
 * @sc: per adapter object
 * @enc_id: enclosure logical identifier
 *
 * Returns the index of enclosure entry on success or bad index.
 */
static u8
_mapping_get_enc_idx_from_id(struct mps_softc *sc, u64 enc_id,
    u64 phy_bits)
{
	struct enc_mapping_table *et_entry;
	u8 enc_idx = 0;

	for (enc_idx = 0; enc_idx < sc->num_enc_table_entries; enc_idx++) {
		et_entry = &sc->enclosure_table[enc_idx];
		if ((et_entry->enclosure_id == le64toh(enc_id)) &&
		    (!et_entry->phy_bits || (et_entry->phy_bits &
		    le32toh(phy_bits))))
			return enc_idx;
	}
	return MPS_ENCTABLE_BAD_IDX;
}

/**
 * _mapping_get_enc_idx_from_handle - get enclosure index from handle
 * @sc: per adapter object
 * @enc_id: enclosure handle
 *
 * Returns the index of enclosure entry on success or bad index.
 */
static u8
_mapping_get_enc_idx_from_handle(struct mps_softc *sc, u16 handle)
{
	struct enc_mapping_table *et_entry;
	u8 enc_idx = 0;

	for (enc_idx = 0; enc_idx < sc->num_enc_table_entries; enc_idx++) {
		et_entry = &sc->enclosure_table[enc_idx];
		if (et_entry->missing_count)
			continue;
		if (et_entry->enc_handle == handle)
			return enc_idx;
	}
	return MPS_ENCTABLE_BAD_IDX;
}

/**
 * _mapping_get_high_missing_et_idx - get missing enclosure index
 * @sc: per adapter object
 *
 * Search through the enclosure table and identifies the enclosure entry
 * with high missing count and returns it's index
 *
 * Returns the index of enclosure entry on success or bad index.
 */
static u8
_mapping_get_high_missing_et_idx(struct mps_softc *sc)
{
	struct enc_mapping_table *et_entry;
	u8 high_missing_count = 0;
	u8 enc_idx, high_idx = MPS_ENCTABLE_BAD_IDX;

	for (enc_idx = 0; enc_idx < sc->num_enc_table_entries; enc_idx++) {
		et_entry = &sc->enclosure_table[enc_idx];
		if ((et_entry->missing_count > high_missing_count) &&
		    !et_entry->skip_search) {
			high_missing_count = et_entry->missing_count;
			high_idx = enc_idx;
		}
	}
	return high_idx;
}

/**
 * _mapping_get_high_missing_mt_idx - get missing map table index
 * @sc: per adapter object
 *
 * Search through the map table and identifies the device entry
 * with high missing count and returns it's index
 *
 * Returns the index of map table entry on success or bad index.
 */
static u32
_mapping_get_high_missing_mt_idx(struct mps_softc *sc)
{
	u32 map_idx, high_idx = MPS_MAPTABLE_BAD_IDX;
	u8 high_missing_count = 0;
	u32 start_idx, end_idx, start_idx_ir, end_idx_ir;
	struct dev_mapping_table *mt_entry;
	u16 ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);

	start_idx = 0;
	start_idx_ir = 0;
	end_idx_ir = 0;
	end_idx = sc->max_devices;
	if (ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_RESERVED_TARGETID_0)
		start_idx = 1;
	if (sc->ir_firmware) {
		_mapping_get_ir_maprange(sc, &start_idx_ir, &end_idx_ir);
		if (start_idx == start_idx_ir)
			start_idx = end_idx_ir + 1;
		else
			end_idx = start_idx_ir;
	}
	mt_entry = &sc->mapping_table[start_idx];
	for (map_idx = start_idx; map_idx < end_idx; map_idx++, mt_entry++) {
		if (mt_entry->missing_count > high_missing_count) {
			high_missing_count =  mt_entry->missing_count;
			high_idx = map_idx;
		}
	}
	return high_idx;
}

/**
 * _mapping_get_ir_mt_idx_from_wwid - get map table index from volume WWID
 * @sc: per adapter object
 * @wwid: world wide unique ID of the volume
 *
 * Returns the index of map table entry on success or bad index.
 */
static u32
_mapping_get_ir_mt_idx_from_wwid(struct mps_softc *sc, u64 wwid)
{
	u32 start_idx, end_idx, map_idx;
	struct dev_mapping_table *mt_entry;

	_mapping_get_ir_maprange(sc, &start_idx, &end_idx);
	mt_entry = &sc->mapping_table[start_idx];
	for (map_idx = start_idx; map_idx <= end_idx; map_idx++, mt_entry++)
		if (mt_entry->physical_id == wwid)
			return map_idx;

	return MPS_MAPTABLE_BAD_IDX;
}

/**
 * _mapping_get_mt_idx_from_id - get map table index from a device ID
 * @sc: per adapter object
 * @dev_id: device identifer (SAS Address)
 *
 * Returns the index of map table entry on success or bad index.
 */
static u32
_mapping_get_mt_idx_from_id(struct mps_softc *sc, u64 dev_id)
{
	u32 map_idx;
	struct dev_mapping_table *mt_entry;

	for (map_idx = 0; map_idx < sc->max_devices; map_idx++) {
		mt_entry = &sc->mapping_table[map_idx];
		if (mt_entry->physical_id == dev_id)
			return map_idx;
	}
	return MPS_MAPTABLE_BAD_IDX;
}

/**
 * _mapping_get_ir_mt_idx_from_handle - get map table index from volume handle
 * @sc: per adapter object
 * @wwid: volume device handle
 *
 * Returns the index of map table entry on success or bad index.
 */
static u32
_mapping_get_ir_mt_idx_from_handle(struct mps_softc *sc, u16 volHandle)
{
	u32 start_idx, end_idx, map_idx;
	struct dev_mapping_table *mt_entry;

	_mapping_get_ir_maprange(sc, &start_idx, &end_idx);
	mt_entry = &sc->mapping_table[start_idx];
	for (map_idx = start_idx; map_idx <= end_idx; map_idx++, mt_entry++)
		if (mt_entry->dev_handle == volHandle)
			return map_idx;

	return MPS_MAPTABLE_BAD_IDX;
}

/**
 * _mapping_get_mt_idx_from_handle - get map table index from handle
 * @sc: per adapter object
 * @dev_id: device handle
 *
 * Returns the index of map table entry on success or bad index.
 */
static u32
_mapping_get_mt_idx_from_handle(struct mps_softc *sc, u16 handle)
{
	u32 map_idx;
	struct dev_mapping_table *mt_entry;

	for (map_idx = 0; map_idx < sc->max_devices; map_idx++) {
		mt_entry = &sc->mapping_table[map_idx];
		if (mt_entry->dev_handle == handle)
			return map_idx;
	}
	return MPS_MAPTABLE_BAD_IDX;
}

/**
 * _mapping_get_free_ir_mt_idx - get first free index for a volume
 * @sc: per adapter object
 *
 * Search through mapping table for free index for a volume and if no free
 * index then looks for a volume with high mapping index
 *
 * Returns the index of map table entry on success or bad index.
 */
static u32
_mapping_get_free_ir_mt_idx(struct mps_softc *sc)
{
	u8 high_missing_count = 0;
	u32 start_idx, end_idx, map_idx;
	u32 high_idx = MPS_MAPTABLE_BAD_IDX;
	struct dev_mapping_table *mt_entry;

	/*
	 * The IN_USE flag should be clear if the entry is available to use.
	 * This flag is cleared on initialization and and when a volume is
	 * deleted. All other times this flag should be set. If, for some
	 * reason, a free entry cannot be found, look for the entry with the
	 * highest missing count just in case there is one.
	 */
	_mapping_get_ir_maprange(sc, &start_idx, &end_idx);

	mt_entry = &sc->mapping_table[start_idx];
	for (map_idx = start_idx; map_idx <= end_idx; map_idx++, mt_entry++) {
		if (!(mt_entry->device_info & MPS_MAP_IN_USE))
			return map_idx;

		if (mt_entry->missing_count > high_missing_count) {
			high_missing_count = mt_entry->missing_count;
			high_idx = map_idx;
		}
	}

	if (high_idx == MPS_MAPTABLE_BAD_IDX) {
		mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: Could not find a "
		    "free entry in the mapping table for a Volume. The mapping "
		    "table is probably corrupt.\n", __func__);
	}
	
	return high_idx;
}

/**
 * _mapping_get_free_mt_idx - get first free index for a device
 * @sc: per adapter object
 * @start_idx: offset in the table to start search
 *
 * Returns the index of map table entry on success or bad index.
 */
static u32
_mapping_get_free_mt_idx(struct mps_softc *sc, u32 start_idx)
{
	u32 map_idx, max_idx = sc->max_devices;
	struct dev_mapping_table *mt_entry = &sc->mapping_table[start_idx];
	u16 volume_mapping_flags;

	volume_mapping_flags = le16toh(sc->ioc_pg8.IRVolumeMappingFlags) &
	    MPI2_IOCPAGE8_IRFLAGS_MASK_VOLUME_MAPPING_MODE;
	if (sc->ir_firmware && (volume_mapping_flags ==
	    MPI2_IOCPAGE8_IRFLAGS_HIGH_VOLUME_MAPPING))
		max_idx -= sc->max_volumes;

	for (map_idx  = start_idx; map_idx < max_idx; map_idx++, mt_entry++)
		if (!(mt_entry->device_info & (MPS_MAP_IN_USE |
		    MPS_DEV_RESERVED)))
			return map_idx;

	return MPS_MAPTABLE_BAD_IDX;
}

/**
 * _mapping_get_dpm_idx_from_id - get DPM index from ID
 * @sc: per adapter object
 * @id: volume WWID or enclosure ID or device ID
 *
 * Returns the index of DPM entry on success or bad index.
 */
static u16
_mapping_get_dpm_idx_from_id(struct mps_softc *sc, u64 id, u32 phy_bits)
{
	u16 entry_num;
	uint64_t PhysicalIdentifier;
	Mpi2DriverMap0Entry_t *dpm_entry;

	dpm_entry = (Mpi2DriverMap0Entry_t *)((u8 *)sc->dpm_pg0 +
	    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
	PhysicalIdentifier = dpm_entry->PhysicalIdentifier.High;
	PhysicalIdentifier = (PhysicalIdentifier << 32) | 
	    dpm_entry->PhysicalIdentifier.Low;
	for (entry_num = 0; entry_num < sc->max_dpm_entries; entry_num++,
	    dpm_entry++)
		if ((id == PhysicalIdentifier) &&
		    (!phy_bits || !dpm_entry->PhysicalBitsMapping ||
		    (phy_bits & dpm_entry->PhysicalBitsMapping)))
			return entry_num;

	return MPS_DPM_BAD_IDX;
}


/**
 * _mapping_get_free_dpm_idx - get first available DPM index
 * @sc: per adapter object
 *
 * Returns the index of DPM entry on success or bad index.
 */
static u32
_mapping_get_free_dpm_idx(struct mps_softc *sc)
{
	u16 entry_num;
	Mpi2DriverMap0Entry_t *dpm_entry;
	u16 current_entry = MPS_DPM_BAD_IDX, missing_cnt, high_missing_cnt = 0;
	u64 physical_id;
	struct dev_mapping_table *mt_entry;
	u32 map_idx;

 	for (entry_num = 0; entry_num < sc->max_dpm_entries; entry_num++) {
		dpm_entry = (Mpi2DriverMap0Entry_t *) ((u8 *)sc->dpm_pg0 +
		    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
		dpm_entry += entry_num;
		missing_cnt = dpm_entry->MappingInformation &
		    MPI2_DRVMAP0_MAPINFO_MISSING_MASK;

		/*
		 * If entry is used and not missing, then this entry can't be
		 * used. Look at next one.
		 */
		if (sc->dpm_entry_used[entry_num] && !missing_cnt)
			continue;

		/*
		 * If this entry is not used at all, then the missing count
		 * doesn't matter. Just use this one. Otherwise, keep looking
		 * and make sure the entry with the highest missing count is
		 * used.
		 */
		if (!sc->dpm_entry_used[entry_num]) {
			current_entry = entry_num;
			break;
		}
		if ((current_entry == MPS_DPM_BAD_IDX) ||
		    (missing_cnt > high_missing_cnt)) {
			current_entry = entry_num;
			high_missing_cnt = missing_cnt;
		}
 	}

	/*
	 * If an entry has been found to use and it's already marked as used
	 * it means that some device was already using this entry but it's
	 * missing, and that means that the connection between the missing
	 * device's DPM entry and the mapping table needs to be cleared. To do
	 * this, use the Physical ID of the old device still in the DPM entry
	 * to find its mapping table entry, then mark its DPM entry as BAD.
	 */
	if ((current_entry != MPS_DPM_BAD_IDX) &&
	    sc->dpm_entry_used[current_entry]) {
		dpm_entry = (Mpi2DriverMap0Entry_t *) ((u8 *)sc->dpm_pg0 +
		    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
		dpm_entry += current_entry;
		physical_id = dpm_entry->PhysicalIdentifier.High;
		physical_id = (physical_id << 32) |
		    dpm_entry->PhysicalIdentifier.Low;
		map_idx = _mapping_get_mt_idx_from_id(sc, physical_id);
		if (map_idx != MPS_MAPTABLE_BAD_IDX) {
			mt_entry = &sc->mapping_table[map_idx];
			mt_entry->dpm_entry_num = MPS_DPM_BAD_IDX;
		}
	}
	return current_entry;
}

/**
 * _mapping_update_ir_missing_cnt - Updates missing count for a volume
 * @sc: per adapter object
 * @map_idx: map table index of the volume
 * @element: IR configuration change element
 * @wwid: IR volume ID.
 *
 * Updates the missing count in the map table and in the DPM entry for a volume
 *
 * Returns nothing.
 */
static void
_mapping_update_ir_missing_cnt(struct mps_softc *sc, u32 map_idx,
    Mpi2EventIrConfigElement_t *element, u64 wwid)
{
	struct dev_mapping_table *mt_entry;
	u8 missing_cnt, reason = element->ReasonCode, update_dpm = 1;
	u16 dpm_idx;
	Mpi2DriverMap0Entry_t *dpm_entry;

	/*
	 * Depending on the reason code, update the missing count. Always set
	 * the init_complete flag when here, so just do it first. That flag is
	 * used for volumes to make sure that the DPM entry has been updated.
	 * When a volume is deleted, clear the map entry's IN_USE flag so that
	 * the entry can be used again if another volume is created. Also clear
	 * its dev_handle entry so that other functions can't find this volume
	 * by the handle, since it's not defined any longer.
	 */
	mt_entry = &sc->mapping_table[map_idx];
	mt_entry->init_complete = 1;
	if ((reason == MPI2_EVENT_IR_CHANGE_RC_ADDED) ||
	    (reason == MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED)) {
		mt_entry->missing_count = 0;
	} else if (reason == MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED) {
		if (mt_entry->missing_count < MPS_MAX_MISSING_COUNT)
			mt_entry->missing_count++;

		mt_entry->device_info &= ~MPS_MAP_IN_USE;
		mt_entry->dev_handle = 0;
	}

	/*
	 * If persistent mapping is enabled, update the DPM with the new missing
	 * count for the volume. If the DPM index is bad, get a free one. If
	 * it's bad for a volume that's being deleted do nothing because that
	 * volume doesn't have a DPM entry. 
	 */
	if (!sc->is_dpm_enable)
		return;
	dpm_idx = mt_entry->dpm_entry_num;
	if (dpm_idx == MPS_DPM_BAD_IDX) {
		if (reason == MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED)
		{
			mps_dprint(sc, MPS_MAPPING, "%s: Volume being deleted "
			    "is not in DPM so DPM missing count will not be "
			    "updated.\n", __func__);
			return;
		}
	}
	if (dpm_idx == MPS_DPM_BAD_IDX)
		dpm_idx = _mapping_get_free_dpm_idx(sc);

	/*
	 * Got the DPM entry for the volume or found a free DPM entry if this is
	 * a new volume. Check if the current information is outdated.
	 */
	if (dpm_idx != MPS_DPM_BAD_IDX) {
		dpm_entry = (Mpi2DriverMap0Entry_t *)((u8 *)sc->dpm_pg0 +
		    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
		dpm_entry += dpm_idx;
		missing_cnt = dpm_entry->MappingInformation &
		    MPI2_DRVMAP0_MAPINFO_MISSING_MASK;
		if ((mt_entry->physical_id ==
		    le64toh(((u64)dpm_entry->PhysicalIdentifier.High << 32) |
		    (u64)dpm_entry->PhysicalIdentifier.Low)) && (missing_cnt ==
		    mt_entry->missing_count)) {
			mps_dprint(sc, MPS_MAPPING, "%s: DPM entry for volume "
			   "with target ID %d does not require an update.\n",
			    __func__, mt_entry->id);
			update_dpm = 0;
		}
	}

	/*
	 * Update the volume's persistent info if it's new or the ID or missing
	 * count has changed. If a good DPM index has not been found by now,
	 * there is no space left in the DPM table.
	 */
	if ((dpm_idx != MPS_DPM_BAD_IDX) && update_dpm) {
		mps_dprint(sc, MPS_MAPPING, "%s: Update DPM entry for volume "
		    "with target ID %d.\n", __func__, mt_entry->id);

		mt_entry->dpm_entry_num = dpm_idx;
		dpm_entry = (Mpi2DriverMap0Entry_t *)((u8 *)sc->dpm_pg0 +
		    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
		dpm_entry += dpm_idx;
		dpm_entry->PhysicalIdentifier.Low =
		    (0xFFFFFFFF & mt_entry->physical_id);
		dpm_entry->PhysicalIdentifier.High =
		    (mt_entry->physical_id >> 32);
		dpm_entry->DeviceIndex = map_idx;
		dpm_entry->MappingInformation = mt_entry->missing_count;
		dpm_entry->PhysicalBitsMapping = 0;
		dpm_entry->Reserved1 = 0;
		sc->dpm_flush_entry[dpm_idx] = 1;
		sc->dpm_entry_used[dpm_idx] = 1;
	} else if (dpm_idx == MPS_DPM_BAD_IDX) {
		mps_dprint(sc, MPS_INFO | MPS_MAPPING, "%s: No space to add an "
		    "entry in the DPM table for volume with target ID %d.\n",
		    __func__, mt_entry->id);
	}
}

/**
 * _mapping_add_to_removal_table - add DPM index to the removal table
 * @sc: per adapter object
 * @dpm_idx: Index of DPM entry to remove
 *
 * Adds a DPM entry number to the removal table.
 *
 * Returns nothing.
 */
static void
_mapping_add_to_removal_table(struct mps_softc *sc, u16 dpm_idx)
{
	struct map_removal_table *remove_entry;
	u32 i;

	/*
	 * This is only used to remove entries from the DPM in the controller.
	 * If DPM is not enabled, just return.
	 */
	if (!sc->is_dpm_enable)
		return;

	/*
	 * Find the first available removal_table entry and add the new entry
	 * there.
	 */
	remove_entry = sc->removal_table;

	for (i = 0; i < sc->max_devices; i++, remove_entry++) {
		if (remove_entry->dpm_entry_num != MPS_DPM_BAD_IDX)
			continue;
 
		mps_dprint(sc, MPS_MAPPING, "%s: Adding DPM entry %d to table "
		    "for removal.\n", __func__, dpm_idx);
		remove_entry->dpm_entry_num = dpm_idx;
		break;
	}

}

/**
 * _mapping_update_missing_count - Update missing count for a device
 * @sc: per adapter object
 * @topo_change: Topology change event entry
 *
 * Increment the missing count in the mapping table for a device that is not
 * responding. If Persitent Mapping is used, increment the DPM entry as well.
 * Currently, this function only increments the missing count if the device 
 * goes missing, so after initialization has completed. This means that the
 * missing count can only go from 0 to 1 here. The missing count is incremented
 * during initialization as well, so that's where a target's missing count can
 * go past 1.
 *
 * Returns nothing.
 */
static void
_mapping_update_missing_count(struct mps_softc *sc,
    struct _map_topology_change *topo_change)
{
	u16 ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);
	u8 entry;
	struct _map_phy_change *phy_change;
	u32 map_idx;
	struct dev_mapping_table *mt_entry;
	Mpi2DriverMap0Entry_t *dpm_entry;

	for (entry = 0; entry < topo_change->num_entries; entry++) {
		phy_change = &topo_change->phy_details[entry];
		if (!phy_change->dev_handle || (phy_change->reason !=
		    MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING))
			continue;
		map_idx = _mapping_get_mt_idx_from_handle(sc, phy_change->
		    dev_handle);
		phy_change->is_processed = 1;
		if (map_idx == MPS_MAPTABLE_BAD_IDX) {
			mps_dprint(sc, MPS_INFO | MPS_MAPPING, "%s: device is "
			    "already removed from mapping table\n", __func__);
			continue;
		}
		mt_entry = &sc->mapping_table[map_idx];
		if (mt_entry->missing_count < MPS_MAX_MISSING_COUNT)
			mt_entry->missing_count++;

		/*
		 * When using Enc/Slot mapping, when a device is removed, it's
		 * mapping table information should be cleared. Otherwise, the
		 * target ID will be incorrect if this same device is re-added
		 * to a different slot.
		 */
		if ((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
		    MPI2_IOCPAGE8_FLAGS_ENCLOSURE_SLOT_MAPPING) {
			_mapping_clear_map_entry(mt_entry);
		}

		/*
		 * When using device mapping, update the missing count in the
		 * DPM entry, but only if the missing count has changed.
		 */
		if (((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
		    MPI2_IOCPAGE8_FLAGS_DEVICE_PERSISTENCE_MAPPING) &&
		    sc->is_dpm_enable &&
		    mt_entry->dpm_entry_num != MPS_DPM_BAD_IDX) {
			dpm_entry =
			    (Mpi2DriverMap0Entry_t *) ((u8 *)sc->dpm_pg0 +
			    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
			dpm_entry += mt_entry->dpm_entry_num;
			if (dpm_entry->MappingInformation !=
			    mt_entry->missing_count) {
				dpm_entry->MappingInformation =
				    mt_entry->missing_count;
				sc->dpm_flush_entry[mt_entry->dpm_entry_num] =
				    1;
			}
		}
	}
}

/**
 * _mapping_find_enc_map_space -find map table entries for enclosure
 * @sc: per adapter object
 * @et_entry: enclosure entry
 *
 * Search through the mapping table defragment it and provide contiguous
 * space in map table for a particular enclosure entry
 *
 * Returns start index in map table or bad index.
 */
static u32
_mapping_find_enc_map_space(struct mps_softc *sc,
    struct enc_mapping_table *et_entry)
{
	u16 vol_mapping_flags;
	u32 skip_count, end_of_table, map_idx, enc_idx;
	u16 num_found;
	u32 start_idx = MPS_MAPTABLE_BAD_IDX;
	struct dev_mapping_table *mt_entry;
	struct enc_mapping_table *enc_entry;
	unsigned char done_flag = 0, found_space;
	u16 max_num_phy_ids = le16toh(sc->ioc_pg8.MaxNumPhysicalMappedIDs);

	skip_count = sc->num_rsvd_entries;
	num_found = 0;

	vol_mapping_flags = le16toh(sc->ioc_pg8.IRVolumeMappingFlags) &
	    MPI2_IOCPAGE8_IRFLAGS_MASK_VOLUME_MAPPING_MODE;

	/*
	 * The end of the mapping table depends on where volumes are kept, if
	 * IR is enabled.
	 */
	if (!sc->ir_firmware)
		end_of_table = sc->max_devices;
	else if (vol_mapping_flags == MPI2_IOCPAGE8_IRFLAGS_LOW_VOLUME_MAPPING)
		end_of_table = sc->max_devices;
	else
		end_of_table = sc->max_devices - sc->max_volumes;

	/*
	 * The skip_count is the number of entries that are reserved at the
	 * beginning of the mapping table. But, it does not include the number
	 * of Physical IDs that are reserved for direct attached devices. Look
	 * through the mapping table after these reserved entries to see if 
	 * the devices for this enclosure are already mapped. The PHY bit check
	 * is used to make sure that at least one PHY bit is common between the
	 * enclosure and the device that is already mapped.
	 */
	mps_dprint(sc, MPS_MAPPING, "%s: Looking for space in the mapping "
	    "table for added enclosure.\n", __func__);
	for (map_idx = (max_num_phy_ids + skip_count);
	    map_idx < end_of_table; map_idx++) {
		mt_entry = &sc->mapping_table[map_idx];
		if ((et_entry->enclosure_id == mt_entry->physical_id) &&
		    (!mt_entry->phy_bits || (mt_entry->phy_bits &
		    et_entry->phy_bits))) {
			num_found += 1;
			if (num_found == et_entry->num_slots) {
				start_idx = (map_idx - num_found) + 1;
				mps_dprint(sc, MPS_MAPPING, "%s: Found space "
				    "in the mapping for enclosure at map index "
				    "%d.\n", __func__, start_idx);
				return start_idx;
			}
		} else
			num_found = 0;
	}

	/*
	 * If the enclosure's devices are not mapped already, look for
	 * contiguous entries in the mapping table that are not reserved. If
	 * enough entries are found, return the starting index for that space.
	 */
	num_found = 0;
	for (map_idx = (max_num_phy_ids + skip_count);
	    map_idx < end_of_table; map_idx++) {
		mt_entry = &sc->mapping_table[map_idx];
		if (!(mt_entry->device_info & MPS_DEV_RESERVED)) {
			num_found += 1;
			if (num_found == et_entry->num_slots) {
				start_idx = (map_idx - num_found) + 1;
				mps_dprint(sc, MPS_MAPPING, "%s: Found space "
				    "in the mapping for enclosure at map index "
				    "%d.\n", __func__, start_idx);
				return start_idx;
			}
		} else
			num_found = 0;
	}

	/*
	 * If here, it means that not enough space in the mapping table was
	 * found to support this enclosure, so go through the enclosure table to
	 * see if any enclosure entries have a missing count. If so, get the
	 * enclosure with the highest missing count and check it to see if there
	 * is enough space for the new enclosure.
	 */
	while (!done_flag) {
		enc_idx = _mapping_get_high_missing_et_idx(sc);
		if (enc_idx == MPS_ENCTABLE_BAD_IDX) {
			mps_dprint(sc, MPS_MAPPING, "%s: Not enough space was "
			    "found in the mapping for the added enclosure.\n",
			    __func__);
			return MPS_MAPTABLE_BAD_IDX;
		}

		/*
		 * Found a missing enclosure. Set the skip_search flag so this
		 * enclosure is not checked again for a high missing count if
		 * the loop continues. This way, all missing enclosures can
		 * have their space added together to find enough space in the
		 * mapping table for the added enclosure. The space must be
		 * contiguous.
		 */
		mps_dprint(sc, MPS_MAPPING, "%s: Space from a missing "
		    "enclosure was found.\n", __func__);
		enc_entry = &sc->enclosure_table[enc_idx];
		enc_entry->skip_search = 1;

		/*
		 * Unmark all of the missing enclosure's device's reserved
		 * space. These will be remarked as reserved if this missing
		 * enclosure's space is not used.
		 */
		mps_dprint(sc, MPS_MAPPING, "%s: Clear the reserved flag for "
		    "all of the map entries for the enclosure.\n", __func__);
		mt_entry = &sc->mapping_table[enc_entry->start_index];
		for (map_idx = enc_entry->start_index; map_idx <
		    (enc_entry->start_index + enc_entry->num_slots); map_idx++,
		    mt_entry++)
			mt_entry->device_info &= ~MPS_DEV_RESERVED;

		/*
		 * Now that space has been unreserved, check again to see if
		 * enough space is available for the new enclosure.
		 */
		mps_dprint(sc, MPS_MAPPING, "%s: Check if new mapping space is "
		    "enough for the new enclosure.\n", __func__);
		found_space = 0;
		num_found = 0;
		for (map_idx = (max_num_phy_ids + skip_count);
		    map_idx < end_of_table; map_idx++) {
			mt_entry = &sc->mapping_table[map_idx];
			if (!(mt_entry->device_info & MPS_DEV_RESERVED)) {
				num_found += 1;
				if (num_found == et_entry->num_slots) {
					start_idx = (map_idx - num_found) + 1;
					found_space = 1;
					break;
				}
			} else
				num_found = 0;
		}
		if (!found_space)
			continue;

		/*
		 * If enough space was found, all of the missing enclosures that
		 * will be used for the new enclosure must be added to the
		 * removal table. Then all mappings for the enclosure's devices
		 * and for the enclosure itself need to be cleared. There may be
		 * more than one enclosure to add to the removal table and
		 * clear.
		 */
		mps_dprint(sc, MPS_MAPPING, "%s: Found space in the mapping "
		    "for enclosure at map index %d.\n", __func__, start_idx);
		for (map_idx = start_idx; map_idx < (start_idx + num_found);
		    map_idx++) {
			enc_entry = sc->enclosure_table;
			for (enc_idx = 0; enc_idx < sc->num_enc_table_entries;
			    enc_idx++, enc_entry++) {
				if (map_idx < enc_entry->start_index ||
				    map_idx > (enc_entry->start_index +
				    enc_entry->num_slots))
					continue;
				if (!enc_entry->removal_flag) {
					mps_dprint(sc, MPS_MAPPING, "%s: "
					    "Enclosure %d will be removed from "
					    "the mapping table.\n", __func__,
					    enc_idx);
					enc_entry->removal_flag = 1;
					_mapping_add_to_removal_table(sc,
					    enc_entry->dpm_entry_num);
				}
				mt_entry = &sc->mapping_table[map_idx];
				_mapping_clear_map_entry(mt_entry);
				if (map_idx == (enc_entry->start_index +
				    enc_entry->num_slots - 1))
					_mapping_clear_enc_entry(et_entry);
			}
		}

		/*
		 * During the search for space for this enclosure, some entries
		 * in the mapping table may have been unreserved. Go back and
		 * change all of these to reserved again. Only the enclosures
		 * with the removal_flag set should be left as unreserved. The
		 * skip_search flag needs to be cleared as well so that the
		 * enclosure's space will be looked at the next time space is
		 * needed.
		 */ 
		enc_entry = sc->enclosure_table;
		for (enc_idx = 0; enc_idx < sc->num_enc_table_entries;
		    enc_idx++, enc_entry++) {
			if (!enc_entry->removal_flag) {
				mps_dprint(sc, MPS_MAPPING, "%s: Reset the "
				    "reserved flag for all of the map entries "
				    "for enclosure %d.\n", __func__, enc_idx);
				mt_entry = &sc->mapping_table[enc_entry->
				    start_index];
				for (map_idx = enc_entry->start_index; map_idx <
				    (enc_entry->start_index +
				    enc_entry->num_slots); map_idx++,
				    mt_entry++)
					mt_entry->device_info |=
					    MPS_DEV_RESERVED;
				et_entry->skip_search = 0;
			}
		}
		done_flag = 1;
	}
	return start_idx;
}

/**
 * _mapping_get_dev_info -get information about newly added devices
 * @sc: per adapter object
 * @topo_change: Topology change event entry
 *
 * Search through the topology change event list and issues sas device pg0
 * requests for the newly added device and reserved entries in tables
 *
 * Returns nothing
 */
static void
_mapping_get_dev_info(struct mps_softc *sc,
    struct _map_topology_change *topo_change)
{
	u16 ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u8 entry, enc_idx, phy_idx;
	u32 map_idx, index, device_info;
	struct _map_phy_change *phy_change, *tmp_phy_change;
	uint64_t sas_address;
	struct enc_mapping_table *et_entry;
	struct dev_mapping_table *mt_entry;
	u8 add_code = MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED;
	int rc = 1;

	for (entry = 0; entry < topo_change->num_entries; entry++) {
		phy_change = &topo_change->phy_details[entry];
		if (phy_change->is_processed || !phy_change->dev_handle ||
		    phy_change->reason != MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED)
			continue;

		if (mps_config_get_sas_device_pg0(sc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    phy_change->dev_handle)) {
			phy_change->is_processed = 1;
			continue;
		}

		/*
		 * Always get SATA Identify information because this is used
		 * to determine if Start/Stop Unit should be sent to the drive
		 * when the system is shutdown.
		 */
		device_info = le32toh(sas_device_pg0.DeviceInfo);
		sas_address = le32toh(sas_device_pg0.SASAddress.High);
		sas_address = (sas_address << 32) |
		    le32toh(sas_device_pg0.SASAddress.Low);
		if ((device_info & MPI2_SAS_DEVICE_INFO_END_DEVICE) &&
		    (device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE)) {
			rc = mpssas_get_sas_address_for_sata_disk(sc,
			    &sas_address, phy_change->dev_handle, device_info,
			    &phy_change->is_SATA_SSD);
			if (rc) {
				mps_dprint(sc, MPS_ERROR, "%s: failed to get "
				    "disk type (SSD or HDD) and SAS Address "
				    "for SATA device with handle 0x%04x\n",
				    __func__, phy_change->dev_handle);
			}
		}

		phy_change->physical_id = sas_address;
		phy_change->slot = le16toh(sas_device_pg0.Slot);
		phy_change->device_info = device_info;

		/*
		 * When using Enc/Slot mapping, if this device is an enclosure
		 * make sure that all of its slots can fit into the mapping
		 * table.
		 */
		if ((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
		    MPI2_IOCPAGE8_FLAGS_ENCLOSURE_SLOT_MAPPING) {
			/*
			 * The enclosure should already be in the enclosure
			 * table due to the Enclosure Add event. If not, just
			 * continue, nothing can be done.
			 */
			enc_idx = _mapping_get_enc_idx_from_handle(sc,
			    topo_change->enc_handle);
			if (enc_idx == MPS_ENCTABLE_BAD_IDX) {
				phy_change->is_processed = 1;
				mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: "
				    "failed to add the device with handle "
				    "0x%04x because the enclosure is not in "
				    "the mapping table\n", __func__,
				    phy_change->dev_handle);
				continue;
			}
			if (!((phy_change->device_info &
			    MPI2_SAS_DEVICE_INFO_END_DEVICE) &&
			    (phy_change->device_info &
			    (MPI2_SAS_DEVICE_INFO_SSP_TARGET |
			    MPI2_SAS_DEVICE_INFO_STP_TARGET |
			    MPI2_SAS_DEVICE_INFO_SATA_DEVICE)))) {
				phy_change->is_processed = 1;
				continue;
			}
			et_entry = &sc->enclosure_table[enc_idx];

			/*
			 * If the enclosure already has a start_index, it's been
			 * mapped, so go to the next Topo change.
			 */
			if (et_entry->start_index != MPS_MAPTABLE_BAD_IDX)
				continue;

			/*
			 * If the Expander Handle is 0, the devices are direct
			 * attached. In that case, the start_index must be just 
			 * after the reserved entries. Otherwise, find space in
			 * the mapping table for the enclosure's devices.
			 */ 
			if (!topo_change->exp_handle) {
				map_idx	= sc->num_rsvd_entries;
				et_entry->start_index = map_idx;
			} else {
				map_idx = _mapping_find_enc_map_space(sc,
				    et_entry);
				et_entry->start_index = map_idx;

				/*
				 * If space cannot be found to hold all of the
				 * enclosure's devices in the mapping table,
				 * there's no need to continue checking the
				 * other devices in this event. Set all of the
				 * phy_details for this event (if the change is
				 * for an add) as already processed because none
				 * of these devices can be added to the mapping
				 * table.
				 */
				if (et_entry->start_index ==
				    MPS_MAPTABLE_BAD_IDX) {
					mps_dprint(sc, MPS_ERROR | MPS_MAPPING,
					    "%s: failed to add the enclosure "
					    "with ID 0x%016jx because there is "
					    "no free space available in the "
					    "mapping table for all of the "
					    "enclosure's devices.\n", __func__,
					    (uintmax_t)et_entry->enclosure_id);
					phy_change->is_processed = 1;
					for (phy_idx = 0; phy_idx <
					    topo_change->num_entries;
					    phy_idx++) {
						tmp_phy_change =
						    &topo_change->phy_details
						    [phy_idx];
						if (tmp_phy_change->reason ==
						    add_code)
							tmp_phy_change->
							    is_processed = 1;
					}
					break;
				}
			}

			/*
			 * Found space in the mapping table for this enclosure.
			 * Initialize each mapping table entry for the
			 * enclosure.
			 */
			mps_dprint(sc, MPS_MAPPING, "%s: Initialize %d map "
			    "entries for the enclosure, starting at map index "
			    " %d.\n", __func__, et_entry->num_slots, map_idx);
			mt_entry = &sc->mapping_table[map_idx];
			for (index = map_idx; index < (et_entry->num_slots
			    + map_idx); index++, mt_entry++) {
				mt_entry->device_info = MPS_DEV_RESERVED;
				mt_entry->physical_id = et_entry->enclosure_id;
				mt_entry->phy_bits = et_entry->phy_bits;
				mt_entry->missing_count = 0;
			}
		}
	}
}

/**
 * _mapping_set_mid_to_eid -set map table data from enclosure table
 * @sc: per adapter object
 * @et_entry: enclosure entry
 *
 * Returns nothing
 */
static inline void
_mapping_set_mid_to_eid(struct mps_softc *sc,
    struct enc_mapping_table *et_entry)
{
	struct dev_mapping_table *mt_entry;
	u16 slots = et_entry->num_slots, map_idx;
	u32 start_idx = et_entry->start_index;

	if (start_idx != MPS_MAPTABLE_BAD_IDX) {
		mt_entry = &sc->mapping_table[start_idx];
		for (map_idx = 0; map_idx < slots; map_idx++, mt_entry++)
			mt_entry->physical_id = et_entry->enclosure_id;
	}
}

/**
 * _mapping_clear_removed_entries - mark the entries to be cleared
 * @sc: per adapter object
 *
 * Search through the removal table and mark the entries which needs to be
 * flushed to DPM and also updates the map table and enclosure table by
 * clearing the corresponding entries.
 *
 * Returns nothing
 */
static void
_mapping_clear_removed_entries(struct mps_softc *sc)
{
	u32 remove_idx;
	struct map_removal_table *remove_entry;
	Mpi2DriverMap0Entry_t *dpm_entry;
	u8 done_flag = 0, num_entries, m, i;
	struct enc_mapping_table *et_entry, *from, *to;
	u16 ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);

	if (sc->is_dpm_enable) {
		remove_entry = sc->removal_table;
		for (remove_idx = 0; remove_idx < sc->max_devices;
		    remove_idx++, remove_entry++) {
			if (remove_entry->dpm_entry_num != MPS_DPM_BAD_IDX) {
				dpm_entry = (Mpi2DriverMap0Entry_t *)
				    ((u8 *) sc->dpm_pg0 +
				    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
				dpm_entry += remove_entry->dpm_entry_num;
				dpm_entry->PhysicalIdentifier.Low = 0;
				dpm_entry->PhysicalIdentifier.High = 0;
				dpm_entry->DeviceIndex = 0;
				dpm_entry->MappingInformation = 0;
				dpm_entry->PhysicalBitsMapping = 0;
				sc->dpm_flush_entry[remove_entry->
				    dpm_entry_num] = 1;
				sc->dpm_entry_used[remove_entry->dpm_entry_num]
				    = 0;
				remove_entry->dpm_entry_num = MPS_DPM_BAD_IDX;
			}
		}
	}

	/*
	 * When using Enc/Slot mapping, if a new enclosure was added and old
	 * enclosure space was needed, the enclosure table may now have gaps
	 * that need to be closed. All enclosure mappings need to be contiguous
	 * so that space can be reused correctly if available.
	 */
	if ((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
	    MPI2_IOCPAGE8_FLAGS_ENCLOSURE_SLOT_MAPPING) {
		num_entries = sc->num_enc_table_entries;
		while (!done_flag) {
			done_flag = 1;
			et_entry = sc->enclosure_table;
			for (i = 0; i < num_entries; i++, et_entry++) {
				if (!et_entry->enc_handle && et_entry->
				    init_complete) {
					done_flag = 0;
					if (i != (num_entries - 1)) {
						from = &sc->enclosure_table
						    [i+1];
						to = &sc->enclosure_table[i];
						for (m = i; m < (num_entries -
						    1); m++, from++, to++) {
							_mapping_set_mid_to_eid
							    (sc, to);
							*to = *from;
						}
						_mapping_clear_enc_entry(to);
						sc->num_enc_table_entries--;
						num_entries =
						    sc->num_enc_table_entries;
					} else {
						_mapping_clear_enc_entry
						    (et_entry);
						sc->num_enc_table_entries--;
						num_entries =
						    sc->num_enc_table_entries;
					}
				}
			}
		}
	}
}

/**
 * _mapping_add_new_device -Add the new device into mapping table
 * @sc: per adapter object
 * @topo_change: Topology change event entry
 *
 * Search through the topology change event list and update map table,
 * enclosure table and DPM pages for the newly added devices.
 *
 * Returns nothing
 */
static void
_mapping_add_new_device(struct mps_softc *sc,
    struct _map_topology_change *topo_change)
{
	u8 enc_idx, missing_cnt, is_removed = 0;
	u16 dpm_idx;
	u32 search_idx, map_idx;
	u32 entry;
	struct dev_mapping_table *mt_entry;
	struct enc_mapping_table *et_entry;
	struct _map_phy_change *phy_change;
	u16 ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);
	Mpi2DriverMap0Entry_t *dpm_entry;
	uint64_t temp64_var;
	u8 map_shift = MPI2_DRVMAP0_MAPINFO_SLOT_SHIFT;
	u8 hdr_sz = sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER);
	u16 max_num_phy_ids = le16toh(sc->ioc_pg8.MaxNumPhysicalMappedIDs);

	for (entry = 0; entry < topo_change->num_entries; entry++) {
		phy_change = &topo_change->phy_details[entry];
		if (phy_change->is_processed)
			continue;
		if (phy_change->reason != MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED ||
		    !phy_change->dev_handle) {
			phy_change->is_processed = 1;
			continue;
		}
		if ((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
		    MPI2_IOCPAGE8_FLAGS_ENCLOSURE_SLOT_MAPPING) {
			enc_idx = _mapping_get_enc_idx_from_handle
			    (sc, topo_change->enc_handle);
			if (enc_idx == MPS_ENCTABLE_BAD_IDX) {
				phy_change->is_processed = 1;
				mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: "
				    "failed to add the device with handle "
				    "0x%04x because the enclosure is not in "
				    "the mapping table\n", __func__,
				    phy_change->dev_handle);
				continue;
			}

			/*
			 * If the enclosure's start_index is BAD here, it means
			 * that there is no room in the mapping table to cover
			 * all of the devices that could be in the enclosure.
			 * There's no reason to process any of the devices for
			 * this enclosure since they can't be mapped.
			 */
			et_entry = &sc->enclosure_table[enc_idx];
			if (et_entry->start_index == MPS_MAPTABLE_BAD_IDX) {
				phy_change->is_processed = 1;
				mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: "
				    "failed to add the device with handle "
				    "0x%04x because there is no free space "
				    "available in the mapping table\n",
				    __func__, phy_change->dev_handle);
				continue;
			}

			/*
			 * Add this device to the mapping table at the correct
			 * offset where space was found to map the enclosure.
			 * Then setup the DPM entry information if being used.
			 */
			map_idx = et_entry->start_index + phy_change->slot -
			    et_entry->start_slot;
			mt_entry = &sc->mapping_table[map_idx];
			mt_entry->physical_id = phy_change->physical_id;
			mt_entry->id = map_idx;
			mt_entry->dev_handle = phy_change->dev_handle;
			mt_entry->missing_count = 0;
			mt_entry->dpm_entry_num = et_entry->dpm_entry_num;
			mt_entry->device_info = phy_change->device_info |
			    (MPS_DEV_RESERVED | MPS_MAP_IN_USE);
			if (sc->is_dpm_enable) {
				dpm_idx = et_entry->dpm_entry_num;
				if (dpm_idx == MPS_DPM_BAD_IDX)
					dpm_idx = _mapping_get_dpm_idx_from_id
					    (sc, et_entry->enclosure_id,
					     et_entry->phy_bits);
				if (dpm_idx == MPS_DPM_BAD_IDX) {
					dpm_idx = _mapping_get_free_dpm_idx(sc);
					if (dpm_idx != MPS_DPM_BAD_IDX) {
						dpm_entry =
						    (Mpi2DriverMap0Entry_t *)
						    ((u8 *) sc->dpm_pg0 +
						     hdr_sz);
						dpm_entry += dpm_idx;
						dpm_entry->
						    PhysicalIdentifier.Low =
						    (0xFFFFFFFF &
						    et_entry->enclosure_id);
						dpm_entry->
						    PhysicalIdentifier.High =
						    (et_entry->enclosure_id
						     >> 32);
						dpm_entry->DeviceIndex =
						    (U16)et_entry->start_index;
						dpm_entry->MappingInformation =
						    et_entry->num_slots;
						dpm_entry->MappingInformation
						    <<= map_shift;
						dpm_entry->PhysicalBitsMapping
						    = et_entry->phy_bits;
						et_entry->dpm_entry_num =
						    dpm_idx;
						sc->dpm_entry_used[dpm_idx] = 1;
						sc->dpm_flush_entry[dpm_idx] =
						    1;
						phy_change->is_processed = 1;
					} else {
						phy_change->is_processed = 1;
						mps_dprint(sc, MPS_ERROR |
						    MPS_MAPPING, "%s: failed "
						    "to add the device with "
						    "handle 0x%04x to "
						    "persistent table because "
						    "there is no free space "
						    "available\n", __func__,
						    phy_change->dev_handle);
					}
				} else {
					et_entry->dpm_entry_num = dpm_idx;
					mt_entry->dpm_entry_num = dpm_idx;
				}
			}
			et_entry->init_complete = 1;
		} else if ((ioc_pg8_flags &
		    MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
		    MPI2_IOCPAGE8_FLAGS_DEVICE_PERSISTENCE_MAPPING) {

			/*
			 * Get the mapping table index for this device. If it's
			 * not in the mapping table yet, find a free entry if
			 * one is available. If there are no free entries, look
			 * for the entry that has the highest missing count. If
			 * none of that works to find an entry in the mapping
			 * table, there is a problem. Log a message and just
			 * continue on.
			 */
			map_idx = _mapping_get_mt_idx_from_id
			    (sc, phy_change->physical_id);
			if (map_idx == MPS_MAPTABLE_BAD_IDX) {
				search_idx = sc->num_rsvd_entries;
				if (topo_change->exp_handle)
					search_idx += max_num_phy_ids;
				map_idx = _mapping_get_free_mt_idx(sc,
				    search_idx);
			}

			/*
			 * If an entry will be used that has a missing device,
			 * clear its entry from  the DPM in the controller.
			 */
			if (map_idx == MPS_MAPTABLE_BAD_IDX) {
				map_idx = _mapping_get_high_missing_mt_idx(sc);
				if (map_idx != MPS_MAPTABLE_BAD_IDX) {
					mt_entry = &sc->mapping_table[map_idx];
					_mapping_add_to_removal_table(sc,
					    mt_entry->dpm_entry_num);
					is_removed = 1;
					mt_entry->init_complete = 0;
				}
			}
			if (map_idx != MPS_MAPTABLE_BAD_IDX) {
				mt_entry = &sc->mapping_table[map_idx];
				mt_entry->physical_id = phy_change->physical_id;
				mt_entry->id = map_idx;
				mt_entry->dev_handle = phy_change->dev_handle;
				mt_entry->missing_count = 0;
				mt_entry->device_info = phy_change->device_info
				    | (MPS_DEV_RESERVED | MPS_MAP_IN_USE);
			} else {
				phy_change->is_processed = 1;
				mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: "
				    "failed to add the device with handle "
				    "0x%04x because there is no free space "
				    "available in the mapping table\n",
				    __func__, phy_change->dev_handle);
				continue;
			}
			if (sc->is_dpm_enable) {
				if (mt_entry->dpm_entry_num !=
				    MPS_DPM_BAD_IDX) {
					dpm_idx = mt_entry->dpm_entry_num;
					dpm_entry = (Mpi2DriverMap0Entry_t *)
					    ((u8 *)sc->dpm_pg0 + hdr_sz);
					dpm_entry += dpm_idx;
					missing_cnt = dpm_entry->
					    MappingInformation &
					    MPI2_DRVMAP0_MAPINFO_MISSING_MASK;
					temp64_var = dpm_entry->
					    PhysicalIdentifier.High;
					temp64_var = (temp64_var << 32) |
					   dpm_entry->PhysicalIdentifier.Low;

					/*
					 * If the Mapping Table's info is not
					 * the same as the DPM entry, clear the
					 * init_complete flag so that it's
					 * updated.
					 */
					if ((mt_entry->physical_id ==
					    temp64_var) && !missing_cnt)
						mt_entry->init_complete = 1;
					else
						mt_entry->init_complete = 0;
				} else {
					dpm_idx = _mapping_get_free_dpm_idx(sc);
					mt_entry->init_complete = 0;
				}
				if (dpm_idx != MPS_DPM_BAD_IDX &&
				    !mt_entry->init_complete) {
					mt_entry->dpm_entry_num = dpm_idx;
					dpm_entry = (Mpi2DriverMap0Entry_t *)
					    ((u8 *)sc->dpm_pg0 + hdr_sz);
					dpm_entry += dpm_idx;
					dpm_entry->PhysicalIdentifier.Low =
					    (0xFFFFFFFF &
					    mt_entry->physical_id);
					dpm_entry->PhysicalIdentifier.High =
					    (mt_entry->physical_id >> 32);
					dpm_entry->DeviceIndex = (U16) map_idx;
					dpm_entry->MappingInformation = 0;
					dpm_entry->PhysicalBitsMapping = 0;
					sc->dpm_entry_used[dpm_idx] = 1;
					sc->dpm_flush_entry[dpm_idx] = 1;
					phy_change->is_processed = 1;
				} else if (dpm_idx == MPS_DPM_BAD_IDX) {
					phy_change->is_processed = 1;
					mps_dprint(sc, MPS_ERROR | MPS_MAPPING,
					    "%s: failed to add the device with "
					    "handle 0x%04x to persistent table "
					    "because there is no free space "
					    "available\n", __func__,
					    phy_change->dev_handle);
				}
			}
			mt_entry->init_complete = 1;
		}

		phy_change->is_processed = 1;
	}
	if (is_removed)
		_mapping_clear_removed_entries(sc);
}

/**
 * _mapping_flush_dpm_pages -Flush the DPM pages to NVRAM
 * @sc: per adapter object
 *
 * Returns nothing
 */
static void
_mapping_flush_dpm_pages(struct mps_softc *sc)
{
	Mpi2DriverMap0Entry_t *dpm_entry;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2DriverMappingPage0_t config_page;
	u16 entry_num;

	for (entry_num = 0; entry_num < sc->max_dpm_entries; entry_num++) {
		if (!sc->dpm_flush_entry[entry_num])
			continue;
		memset(&config_page, 0, sizeof(Mpi2DriverMappingPage0_t));
		memcpy(&config_page.Header, (u8 *)sc->dpm_pg0,
		    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
		dpm_entry = (Mpi2DriverMap0Entry_t *) ((u8 *)sc->dpm_pg0 +
		    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
		dpm_entry += entry_num;
		dpm_entry->MappingInformation = htole16(dpm_entry->
		    MappingInformation);
		dpm_entry->DeviceIndex = htole16(dpm_entry->DeviceIndex);
		dpm_entry->PhysicalBitsMapping = htole32(dpm_entry->
		    PhysicalBitsMapping);
		memcpy(&config_page.Entry, (u8 *)dpm_entry,
		    sizeof(Mpi2DriverMap0Entry_t));
		/* TODO-How to handle failed writes? */
		mps_dprint(sc, MPS_MAPPING, "%s: Flushing DPM entry %d.\n",
		    __func__, entry_num);
		if (mps_config_set_dpm_pg0(sc, &mpi_reply, &config_page,
		    entry_num)) {
			mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: Flush of "
			    "DPM entry %d for device failed\n", __func__,
			    entry_num);
		} else
			sc->dpm_flush_entry[entry_num] = 0;
		dpm_entry->MappingInformation = le16toh(dpm_entry->
		    MappingInformation);
		dpm_entry->DeviceIndex = le16toh(dpm_entry->DeviceIndex);
		dpm_entry->PhysicalBitsMapping = le32toh(dpm_entry->
		    PhysicalBitsMapping);
	}
}

/**
 * _mapping_allocate_memory- allocates the memory required for mapping tables
 * @sc: per adapter object
 *
 * Allocates the memory for all the tables required for host mapping
 *
 * Return 0 on success or non-zero on failure.
 */
int
mps_mapping_allocate_memory(struct mps_softc *sc)
{
	uint32_t dpm_pg0_sz;

	sc->mapping_table = malloc((sizeof(struct dev_mapping_table) *
	    sc->max_devices), M_MPT2, M_ZERO|M_NOWAIT);
	if (!sc->mapping_table)
		goto free_resources;

	sc->removal_table = malloc((sizeof(struct map_removal_table) *
	    sc->max_devices), M_MPT2, M_ZERO|M_NOWAIT);
	if (!sc->removal_table)
		goto free_resources;

	sc->enclosure_table = malloc((sizeof(struct enc_mapping_table) *
	    sc->max_enclosures), M_MPT2, M_ZERO|M_NOWAIT);
	if (!sc->enclosure_table)
		goto free_resources;

	sc->dpm_entry_used = malloc((sizeof(u8) * sc->max_dpm_entries),
	    M_MPT2, M_ZERO|M_NOWAIT);
	if (!sc->dpm_entry_used)
		goto free_resources;

	sc->dpm_flush_entry = malloc((sizeof(u8) * sc->max_dpm_entries),
	    M_MPT2, M_ZERO|M_NOWAIT);
	if (!sc->dpm_flush_entry)
		goto free_resources;

	dpm_pg0_sz = sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER) +
	    (sc->max_dpm_entries * sizeof(MPI2_CONFIG_PAGE_DRIVER_MAP0_ENTRY));

	sc->dpm_pg0 = malloc(dpm_pg0_sz, M_MPT2, M_ZERO|M_NOWAIT);
	if (!sc->dpm_pg0) {
		printf("%s: memory alloc failed for dpm page; disabling dpm\n",
		    __func__);
		sc->is_dpm_enable = 0;
	}

	return 0;

free_resources:
	free(sc->mapping_table, M_MPT2);
	free(sc->removal_table, M_MPT2);
	free(sc->enclosure_table, M_MPT2);
	free(sc->dpm_entry_used, M_MPT2);
	free(sc->dpm_flush_entry, M_MPT2);
	free(sc->dpm_pg0, M_MPT2);
	printf("%s: device initialization failed due to failure in mapping "
	    "table memory allocation\n", __func__);
	return -1;
}

/**
 * mps_mapping_free_memory- frees the memory allocated for mapping tables
 * @sc: per adapter object
 *
 * Returns nothing.
 */
void
mps_mapping_free_memory(struct mps_softc *sc)
{
	free(sc->mapping_table, M_MPT2);
	free(sc->removal_table, M_MPT2);
	free(sc->enclosure_table, M_MPT2);
	free(sc->dpm_entry_used, M_MPT2);
	free(sc->dpm_flush_entry, M_MPT2);
	free(sc->dpm_pg0, M_MPT2);
}

static void
_mapping_process_dpm_pg0(struct mps_softc *sc)
{
	u8 missing_cnt, enc_idx;
	u16 slot_id, entry_num, num_slots;
	u32 map_idx, dev_idx, start_idx, end_idx;
	struct dev_mapping_table *mt_entry;
	Mpi2DriverMap0Entry_t *dpm_entry;
	u16 ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);
	u16 max_num_phy_ids = le16toh(sc->ioc_pg8.MaxNumPhysicalMappedIDs);
	struct enc_mapping_table *et_entry;
	u64 physical_id;
	u32 phy_bits = 0;

	/*
	 * start_idx and end_idx are only used for IR.
	 */
	if (sc->ir_firmware)
		_mapping_get_ir_maprange(sc, &start_idx, &end_idx);

	/*
	 * Look through all of the DPM entries that were read from the
	 * controller and copy them over to the driver's internal table if they
	 * have a non-zero ID. At this point, any ID with a value of 0 would be
	 * invalid, so don't copy it.
	 */
	mps_dprint(sc, MPS_MAPPING, "%s: Start copy of %d DPM entries into the "
	    "mapping table.\n", __func__, sc->max_dpm_entries);
	dpm_entry = (Mpi2DriverMap0Entry_t *) ((uint8_t *) sc->dpm_pg0 +
	    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
	for (entry_num = 0; entry_num < sc->max_dpm_entries; entry_num++, 
	    dpm_entry++) {
		physical_id = dpm_entry->PhysicalIdentifier.High;
		physical_id = (physical_id << 32) | 
		    dpm_entry->PhysicalIdentifier.Low;
		if (!physical_id) {
			sc->dpm_entry_used[entry_num] = 0;
			continue;
		}
		sc->dpm_entry_used[entry_num] = 1;
		dpm_entry->MappingInformation = le16toh(dpm_entry->
		    MappingInformation);
		missing_cnt = dpm_entry->MappingInformation &
		    MPI2_DRVMAP0_MAPINFO_MISSING_MASK;
		dev_idx = le16toh(dpm_entry->DeviceIndex);
		phy_bits = le32toh(dpm_entry->PhysicalBitsMapping);

		/*
		 * Volumes are at special locations in the mapping table so
		 * account for that. Volume mapping table entries do not depend
		 * on the type of mapping, so continue the loop after adding
		 * volumes to the mapping table.
		 */
		if (sc->ir_firmware && (dev_idx >= start_idx) &&
		    (dev_idx <= end_idx)) {
			mt_entry = &sc->mapping_table[dev_idx];
			mt_entry->physical_id =
			    dpm_entry->PhysicalIdentifier.High;
			mt_entry->physical_id = (mt_entry->physical_id << 32) |
			    dpm_entry->PhysicalIdentifier.Low;
			mt_entry->id = dev_idx;
			mt_entry->missing_count = missing_cnt;
			mt_entry->dpm_entry_num = entry_num;
			mt_entry->device_info = MPS_DEV_RESERVED;
			continue;
		}
		if ((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
		    MPI2_IOCPAGE8_FLAGS_ENCLOSURE_SLOT_MAPPING) {

			/*
			 * The dev_idx for an enclosure is the start index. If
			 * the start index is within the controller's default
			 * enclosure area, set the number of slots for this
			 * enclosure to the max allowed. Otherwise, it should be
			 * a normal enclosure and the number of slots is in the
			 * DPM entry's Mapping Information.
			 */
			if (dev_idx < (sc->num_rsvd_entries +
			    max_num_phy_ids)) {
				slot_id = 0;
				if (ioc_pg8_flags &
				    MPI2_IOCPAGE8_FLAGS_DA_START_SLOT_1)
					slot_id = 1;
				num_slots = max_num_phy_ids;
			} else {
				slot_id = 0;
				num_slots = dpm_entry->MappingInformation &
				    MPI2_DRVMAP0_MAPINFO_SLOT_MASK;
				num_slots >>= MPI2_DRVMAP0_MAPINFO_SLOT_SHIFT;
			}
			enc_idx = sc->num_enc_table_entries;
			if (enc_idx >= sc->max_enclosures) {
				mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: "
				    "Number of enclosure entries in DPM exceed "
				    "the max allowed of %d.\n", __func__,
				    sc->max_enclosures);
				break;
			}
			sc->num_enc_table_entries++;
			et_entry = &sc->enclosure_table[enc_idx];
			physical_id = dpm_entry->PhysicalIdentifier.High;
			et_entry->enclosure_id = (physical_id << 32) |
			    dpm_entry->PhysicalIdentifier.Low;
			et_entry->start_index = dev_idx;
			et_entry->dpm_entry_num = entry_num;
			et_entry->num_slots = num_slots;
			et_entry->start_slot = slot_id;
			et_entry->missing_count = missing_cnt;
			et_entry->phy_bits = phy_bits;

			/*
			 * Initialize all entries for this enclosure in the
			 * mapping table and mark them as reserved. The actual
			 * devices have not been processed yet but when they are
			 * they will use these entries. If an entry is found
			 * that already has a valid DPM index, the mapping table
			 * is corrupt. This can happen if the mapping type is
			 * changed without clearing all of the DPM entries in
			 * the controller.
			 */
			mt_entry = &sc->mapping_table[dev_idx];
			for (map_idx = dev_idx; map_idx < (dev_idx + num_slots);
			    map_idx++, mt_entry++) {
				if (mt_entry->dpm_entry_num !=
				    MPS_DPM_BAD_IDX) {
					mps_dprint(sc, MPS_ERROR | MPS_MAPPING,
					    "%s: Conflict in mapping table for "
					    " enclosure %d\n", __func__,
					    enc_idx);
					break;
				}
				physical_id =
				    dpm_entry->PhysicalIdentifier.High;
				mt_entry->physical_id = (physical_id << 32) |
				    dpm_entry->PhysicalIdentifier.Low;
				mt_entry->phy_bits = phy_bits;
				mt_entry->id = dev_idx;
				mt_entry->dpm_entry_num = entry_num;
				mt_entry->missing_count = missing_cnt;
				mt_entry->device_info = MPS_DEV_RESERVED;
			}
		} else if ((ioc_pg8_flags &
		    MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
		    MPI2_IOCPAGE8_FLAGS_DEVICE_PERSISTENCE_MAPPING) {

			/*
			 * Device mapping, so simply copy the DPM entries to the
			 * mapping table, but check for a corrupt mapping table
			 * (as described above in Enc/Slot mapping).
			 */
			map_idx = dev_idx;
			mt_entry = &sc->mapping_table[map_idx];
			if (mt_entry->dpm_entry_num != MPS_DPM_BAD_IDX) {
				mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: "
				    "Conflict in mapping table for device %d\n",
				    __func__, map_idx);
				break;
			}
			physical_id = dpm_entry->PhysicalIdentifier.High;
			mt_entry->physical_id = (physical_id << 32) |
			    dpm_entry->PhysicalIdentifier.Low;
			mt_entry->phy_bits = phy_bits;
			mt_entry->id = dev_idx;
			mt_entry->missing_count = missing_cnt;
			mt_entry->dpm_entry_num = entry_num;
			mt_entry->device_info = MPS_DEV_RESERVED;
		}
	} /*close the loop for DPM table */
}

/*
 * mps_mapping_check_devices - start of the day check for device availabilty
 * @sc: per adapter object
 *
 * Returns nothing.
 */
void
mps_mapping_check_devices(void *data)
{
	u32 i;
	struct dev_mapping_table *mt_entry;
	struct mps_softc *sc = (struct mps_softc *)data;
	u16 ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);
	struct enc_mapping_table *et_entry;
	u32 start_idx = 0, end_idx = 0;
	u8 stop_device_checks = 0;

	MPS_FUNCTRACE(sc);

	/*
	 * Clear this flag so that this function is never called again except
	 * within this function if the check needs to be done again. The
	 * purpose is to check for missing devices that are currently in the
	 * mapping table so do this only at driver init after discovery.
	 */
	sc->track_mapping_events = 0;

	/*
	 * callout synchronization
	 * This is used to prevent race conditions for the callout. 
	 */
	mps_dprint(sc, MPS_MAPPING, "%s: Start check for missing devices.\n",
	    __func__);
	mtx_assert(&sc->mps_mtx, MA_OWNED);
	if ((callout_pending(&sc->device_check_callout)) ||
	    (!callout_active(&sc->device_check_callout))) {
		mps_dprint(sc, MPS_MAPPING, "%s: Device Check Callout is "
		    "already pending or not active.\n", __func__);
		return;
	}
	callout_deactivate(&sc->device_check_callout);

	/*
	 * Use callout to check if any devices in the mapping table have been
	 * processed yet. If ALL devices are marked as not init_complete, no
	 * devices have been processed and mapped. Until devices are mapped
	 * there's no reason to mark them as missing. Continue resetting this
	 * callout until devices have been mapped.
	 */
	if ((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
	    MPI2_IOCPAGE8_FLAGS_ENCLOSURE_SLOT_MAPPING) {
		et_entry = sc->enclosure_table;
		for (i = 0; i < sc->num_enc_table_entries; i++, et_entry++) {
			if (et_entry->init_complete) {
				stop_device_checks = 1;
				break;
			}
		}
	} else if ((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
	    MPI2_IOCPAGE8_FLAGS_DEVICE_PERSISTENCE_MAPPING) {
		mt_entry = sc->mapping_table;
		for (i = 0; i < sc->max_devices; i++, mt_entry++) {
			if (mt_entry->init_complete) {
				stop_device_checks = 1;
				break;
			}
		}
	}

	/*
	 * Setup another callout check after a delay. Keep doing this until
	 * devices are mapped.
	 */
	if (!stop_device_checks) {
		mps_dprint(sc, MPS_MAPPING, "%s: No devices have been mapped. "
		    "Reset callout to check again after a %d second delay.\n",
		    __func__, MPS_MISSING_CHECK_DELAY);
		callout_reset(&sc->device_check_callout,
		    MPS_MISSING_CHECK_DELAY * hz, mps_mapping_check_devices,
		    sc);
		return;
	}
	mps_dprint(sc, MPS_MAPPING, "%s: Device check complete.\n", __func__);

	/*
	 * Depending on the mapping type, check if devices have been processed
	 * and update their missing counts if not processed.
	 */
	if ((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
	    MPI2_IOCPAGE8_FLAGS_ENCLOSURE_SLOT_MAPPING) {
		et_entry = sc->enclosure_table;
		for (i = 0; i < sc->num_enc_table_entries; i++, et_entry++) {
			if (!et_entry->init_complete) {
				if (et_entry->missing_count <
				    MPS_MAX_MISSING_COUNT) {
					mps_dprint(sc, MPS_MAPPING, "%s: "
					    "Enclosure %d is missing from the "
					    "topology. Update its missing "
					    "count.\n", __func__, i);
					et_entry->missing_count++;
					if (et_entry->dpm_entry_num !=
					    MPS_DPM_BAD_IDX) {
						_mapping_commit_enc_entry(sc,
						    et_entry);
					}
				}
				et_entry->init_complete = 1;
			}
		}
		if (!sc->ir_firmware)
			return;
		_mapping_get_ir_maprange(sc, &start_idx, &end_idx);
		mt_entry = &sc->mapping_table[start_idx];
	} else if ((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) ==
	    MPI2_IOCPAGE8_FLAGS_DEVICE_PERSISTENCE_MAPPING) {
		start_idx = 0;
		end_idx = sc->max_devices - 1;
		mt_entry = sc->mapping_table;
	}

	/*
	 * The start and end indices have been set above according to the
	 * mapping type. Go through these mappings and update any entries that
	 * do not have the init_complete flag set, which means they are missing.
	 */
	if (end_idx == 0)
		return;
	for (i = start_idx; i < (end_idx + 1); i++, mt_entry++) {
		if (mt_entry->device_info & MPS_DEV_RESERVED
		    && !mt_entry->physical_id)
			mt_entry->init_complete = 1;
		else if (mt_entry->device_info & MPS_DEV_RESERVED) {
			if (!mt_entry->init_complete) {
				mps_dprint(sc, MPS_MAPPING, "%s: Device in "
				    "mapping table at index %d is missing from "
				    "topology. Update its missing count.\n",
				    __func__, i);
				if (mt_entry->missing_count <
				    MPS_MAX_MISSING_COUNT) {
					mt_entry->missing_count++;
					if (mt_entry->dpm_entry_num !=
					    MPS_DPM_BAD_IDX) {
						_mapping_commit_map_entry(sc,
						    mt_entry);
					}
				}
				mt_entry->init_complete = 1;
			}
		}
	}
}

/**
 * mps_mapping_initialize - initialize mapping tables
 * @sc: per adapter object
 *
 * Read controller persitant mapping tables into internal data area.
 *
 * Return 0 for success or non-zero for failure.
 */
int
mps_mapping_initialize(struct mps_softc *sc)
{
	uint16_t volume_mapping_flags, dpm_pg0_sz;
	uint32_t i;
	Mpi2ConfigReply_t mpi_reply;
	int error;
	uint8_t retry_count;
	uint16_t ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);

	/* The additional 1 accounts for the virtual enclosure
	 * created for the controller
	 */
	sc->max_enclosures = sc->facts->MaxEnclosures + 1;
	sc->max_expanders = sc->facts->MaxSasExpanders;
	sc->max_volumes = sc->facts->MaxVolumes;
	sc->max_devices = sc->facts->MaxTargets + sc->max_volumes;
	sc->pending_map_events = 0;
	sc->num_enc_table_entries = 0;
	sc->num_rsvd_entries = 0;
	sc->max_dpm_entries = sc->ioc_pg8.MaxPersistentEntries;
	sc->is_dpm_enable = (sc->max_dpm_entries) ? 1 : 0;
	sc->track_mapping_events = 0;
	

	mps_dprint(sc, MPS_MAPPING, "%s: Mapping table has a max of %d entries "
	    "and DPM has a max of %d entries.\n", __func__, sc->max_devices,
	    sc->max_dpm_entries);
	if (ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_DISABLE_PERSISTENT_MAPPING)
		sc->is_dpm_enable = 0;

	if (ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_RESERVED_TARGETID_0)
		sc->num_rsvd_entries = 1;

	volume_mapping_flags = sc->ioc_pg8.IRVolumeMappingFlags &
	    MPI2_IOCPAGE8_IRFLAGS_MASK_VOLUME_MAPPING_MODE;
	if (sc->ir_firmware && (volume_mapping_flags ==
	    MPI2_IOCPAGE8_IRFLAGS_LOW_VOLUME_MAPPING))
		sc->num_rsvd_entries += sc->max_volumes;

	error = mps_mapping_allocate_memory(sc);
	if (error)
		return (error);

	for (i = 0; i < sc->max_devices; i++)
		_mapping_clear_map_entry(sc->mapping_table + i);

	for (i = 0; i < sc->max_enclosures; i++)
		_mapping_clear_enc_entry(sc->enclosure_table + i);

	for (i = 0; i < sc->max_devices; i++) {
		sc->removal_table[i].dev_handle = 0;
		sc->removal_table[i].dpm_entry_num = MPS_DPM_BAD_IDX;
	}

	memset(sc->dpm_entry_used, 0, sc->max_dpm_entries);
	memset(sc->dpm_flush_entry, 0, sc->max_dpm_entries);

	if (sc->is_dpm_enable) {
		dpm_pg0_sz = sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER) +
		    (sc->max_dpm_entries *
		     sizeof(MPI2_CONFIG_PAGE_DRIVER_MAP0_ENTRY));
		retry_count = 0;

retry_read_dpm:
		if (mps_config_get_dpm_pg0(sc, &mpi_reply, sc->dpm_pg0,
		    dpm_pg0_sz)) {
			mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: DPM page "
			    "read failed.\n", __func__);
			if (retry_count < 3) {
				retry_count++;
				goto retry_read_dpm;
			}
			sc->is_dpm_enable = 0;
		}
	}

	if (sc->is_dpm_enable)
		_mapping_process_dpm_pg0(sc);
	else {
		mps_dprint(sc, MPS_MAPPING, "%s: DPM processing is disabled. "
		    "Device mappings will not persist across reboots or "
		    "resets.\n", __func__);
	}

	sc->track_mapping_events = 1;
	return 0;
}

/**
 * mps_mapping_exit - clear mapping table and associated memory
 * @sc: per adapter object
 *
 * Returns nothing.
 */
void
mps_mapping_exit(struct mps_softc *sc)
{
	_mapping_flush_dpm_pages(sc);
	mps_mapping_free_memory(sc);
}

/**
 * mps_mapping_get_tid - return the target id for sas device and handle
 * @sc: per adapter object
 * @sas_address: sas address of the device
 * @handle: device handle
 *
 * Returns valid target ID on success or BAD_ID.
 */
unsigned int
mps_mapping_get_tid(struct mps_softc *sc, uint64_t sas_address, u16 handle)
{
	u32 map_idx;
	struct dev_mapping_table *mt_entry;

	for (map_idx = 0; map_idx < sc->max_devices; map_idx++) {
		mt_entry = &sc->mapping_table[map_idx];
		if (mt_entry->dev_handle == handle && mt_entry->physical_id ==
		    sas_address)
			return mt_entry->id;
	}

	return MPS_MAP_BAD_ID;
}

/**
 * mps_mapping_get_tid_from_handle - find a target id in mapping table using
 * only the dev handle.  This is just a wrapper function for the local function
 * _mapping_get_mt_idx_from_handle.
 * @sc: per adapter object
 * @handle: device handle
 *
 * Returns valid target ID on success or BAD_ID.
 */
unsigned int
mps_mapping_get_tid_from_handle(struct mps_softc *sc, u16 handle)
{
	return (_mapping_get_mt_idx_from_handle(sc, handle));
}

/**
 * mps_mapping_get_raid_tid - return the target id for raid device
 * @sc: per adapter object
 * @wwid: world wide identifier for raid volume
 * @volHandle: volume device handle
 *
 * Returns valid target ID on success or BAD_ID.
 */
unsigned int
mps_mapping_get_raid_tid(struct mps_softc *sc, u64 wwid, u16 volHandle)
{
	u32 start_idx, end_idx, map_idx;
	struct dev_mapping_table *mt_entry;

	_mapping_get_ir_maprange(sc, &start_idx, &end_idx);
	mt_entry = &sc->mapping_table[start_idx];
	for (map_idx  = start_idx; map_idx <= end_idx; map_idx++, mt_entry++) {
		if (mt_entry->dev_handle == volHandle &&
		    mt_entry->physical_id == wwid)
			return mt_entry->id;
	}

	return MPS_MAP_BAD_ID;
}

/**
 * mps_mapping_get_raid_tid_from_handle - find raid device in mapping table
 * using only the volume dev handle.  This is just a wrapper function for the
 * local function _mapping_get_ir_mt_idx_from_handle.
 * @sc: per adapter object
 * @volHandle: volume device handle
 *
 * Returns valid target ID on success or BAD_ID.
 */
unsigned int
mps_mapping_get_raid_tid_from_handle(struct mps_softc *sc, u16 volHandle)
{
	return (_mapping_get_ir_mt_idx_from_handle(sc, volHandle));
}

/**
 * mps_mapping_enclosure_dev_status_change_event - handle enclosure events
 * @sc: per adapter object
 * @event_data: event data payload
 *
 * Return nothing.
 */
void
mps_mapping_enclosure_dev_status_change_event(struct mps_softc *sc,
    Mpi2EventDataSasEnclDevStatusChange_t *event_data)
{
	u8 enc_idx, missing_count;
	struct enc_mapping_table *et_entry;
	Mpi2DriverMap0Entry_t *dpm_entry;
	u16 ioc_pg8_flags = le16toh(sc->ioc_pg8.Flags);
	u8 map_shift = MPI2_DRVMAP0_MAPINFO_SLOT_SHIFT;
	u8 update_phy_bits = 0;
	u32 saved_phy_bits;
	uint64_t temp64_var;

	if ((ioc_pg8_flags & MPI2_IOCPAGE8_FLAGS_MASK_MAPPING_MODE) !=
	    MPI2_IOCPAGE8_FLAGS_ENCLOSURE_SLOT_MAPPING)
		goto out;

	dpm_entry = (Mpi2DriverMap0Entry_t *)((u8 *)sc->dpm_pg0 +
	    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));

	if (event_data->ReasonCode == MPI2_EVENT_SAS_ENCL_RC_ADDED) {
		if (!event_data->NumSlots) {
			mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: Enclosure "
			    "with handle = 0x%x reported 0 slots.\n", __func__,
			    le16toh(event_data->EnclosureHandle));
			goto out;
		}
		temp64_var = event_data->EnclosureLogicalID.High;
		temp64_var = (temp64_var << 32) |
		    event_data->EnclosureLogicalID.Low;
		enc_idx = _mapping_get_enc_idx_from_id(sc, temp64_var,
		    event_data->PhyBits);

		/*
		 * If the Added enclosure is already in the Enclosure Table,
		 * make sure that all the the enclosure info is up to date. If
		 * the enclosure was missing and has just been added back, or if
		 * the enclosure's Phy Bits have changed, clear the missing
		 * count and update the Phy Bits in the mapping table and in the
		 * DPM, if it's being used.
		 */
		if (enc_idx != MPS_ENCTABLE_BAD_IDX) {
			et_entry = &sc->enclosure_table[enc_idx];
			if (et_entry->init_complete &&
			    !et_entry->missing_count) {
				mps_dprint(sc, MPS_MAPPING, "%s: Enclosure %d "
				    "is already present with handle = 0x%x\n",
				    __func__, enc_idx, et_entry->enc_handle);
				goto out;
			}
			et_entry->enc_handle = le16toh(event_data->
			    EnclosureHandle);
			et_entry->start_slot = le16toh(event_data->StartSlot);
			saved_phy_bits = et_entry->phy_bits;
			et_entry->phy_bits |= le32toh(event_data->PhyBits);
			if (saved_phy_bits != et_entry->phy_bits)
				update_phy_bits = 1;
			if (et_entry->missing_count || update_phy_bits) {
				et_entry->missing_count = 0;
				if (sc->is_dpm_enable &&
				    et_entry->dpm_entry_num !=
				    MPS_DPM_BAD_IDX) {
					dpm_entry += et_entry->dpm_entry_num;
					missing_count =
					    (u8)(dpm_entry->MappingInformation &
					    MPI2_DRVMAP0_MAPINFO_MISSING_MASK);
					if (missing_count || update_phy_bits) {
						dpm_entry->MappingInformation
						    = et_entry->num_slots;
						dpm_entry->MappingInformation
						    <<= map_shift;
						dpm_entry->PhysicalBitsMapping
						    = et_entry->phy_bits;
						sc->dpm_flush_entry[et_entry->
						    dpm_entry_num] = 1;
					}
				}
			}
		} else {
			/*
			 * This is a new enclosure that is being added.
			 * Initialize the Enclosure Table entry. It will be
			 * finalized when a device is added for the enclosure
			 * and the enclosure has enough space in the Mapping
			 * Table to map its devices.
			 */
			enc_idx = sc->num_enc_table_entries;
			if (enc_idx >= sc->max_enclosures) {
				mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: "
				    "Enclosure cannot be added to mapping "
				    "table because it's full.\n", __func__);
				goto out;
			}
			sc->num_enc_table_entries++;
			et_entry = &sc->enclosure_table[enc_idx];
			et_entry->enc_handle = le16toh(event_data->
			    EnclosureHandle);
			et_entry->enclosure_id = le64toh(event_data->
			    EnclosureLogicalID.High);
			et_entry->enclosure_id =
			    ((et_entry->enclosure_id << 32) |
			    le64toh(event_data->EnclosureLogicalID.Low));
			et_entry->start_index = MPS_MAPTABLE_BAD_IDX;
			et_entry->dpm_entry_num = MPS_DPM_BAD_IDX;
			et_entry->num_slots = le16toh(event_data->NumSlots);
			et_entry->start_slot = le16toh(event_data->StartSlot);
			et_entry->phy_bits = le32toh(event_data->PhyBits);
		}
		et_entry->init_complete = 1;
	} else if (event_data->ReasonCode ==
	    MPI2_EVENT_SAS_ENCL_RC_NOT_RESPONDING) {
		/*
		 * An enclosure was removed. Update its missing count and then
		 * update the DPM entry with the new missing count for the
		 * enclosure.
		 */
		enc_idx = _mapping_get_enc_idx_from_handle(sc,
		    le16toh(event_data->EnclosureHandle));
		if (enc_idx == MPS_ENCTABLE_BAD_IDX) {
			mps_dprint(sc, MPS_ERROR | MPS_MAPPING, "%s: Cannot "
			    "unmap enclosure %d because it has already been "
			    "deleted.\n", __func__, enc_idx);
			goto out;
		}
		et_entry = &sc->enclosure_table[enc_idx];
		if (et_entry->missing_count < MPS_MAX_MISSING_COUNT)
			et_entry->missing_count++;
		if (sc->is_dpm_enable &&
		    et_entry->dpm_entry_num != MPS_DPM_BAD_IDX) {
			dpm_entry += et_entry->dpm_entry_num;
			dpm_entry->MappingInformation = et_entry->num_slots;
			dpm_entry->MappingInformation <<= map_shift;
			dpm_entry->MappingInformation |=
			    et_entry->missing_count;
			sc->dpm_flush_entry[et_entry->dpm_entry_num] = 1;
		}
		et_entry->init_complete = 1;
	}

out:
	_mapping_flush_dpm_pages(sc);
	if (sc->pending_map_events)
		sc->pending_map_events--;
}

/**
 * mps_mapping_topology_change_event - handle topology change events
 * @sc: per adapter object
 * @event_data: event data payload
 *
 * Returns nothing.
 */
void
mps_mapping_topology_change_event(struct mps_softc *sc,
    Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	struct _map_topology_change topo_change;
	struct _map_phy_change *phy_change;
	Mpi2EventSasTopoPhyEntry_t *event_phy_change;
	u8 i, num_entries;

	topo_change.enc_handle = le16toh(event_data->EnclosureHandle);
	topo_change.exp_handle = le16toh(event_data->ExpanderDevHandle);
	num_entries = event_data->NumEntries;
	topo_change.num_entries = num_entries;
	topo_change.start_phy_num = event_data->StartPhyNum;
	topo_change.num_phys = event_data->NumPhys;
	topo_change.exp_status = event_data->ExpStatus;
	event_phy_change = event_data->PHY;
	topo_change.phy_details = NULL;

	if (!num_entries)
		goto out;
	phy_change = malloc(sizeof(struct _map_phy_change) * num_entries,
	    M_MPT2, M_NOWAIT|M_ZERO);
	topo_change.phy_details = phy_change;
	if (!phy_change)
		goto out;
	for (i = 0; i < num_entries; i++, event_phy_change++, phy_change++) {
		phy_change->dev_handle = le16toh(event_phy_change->
		    AttachedDevHandle);
		phy_change->reason = event_phy_change->PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
	}
	_mapping_update_missing_count(sc, &topo_change);
	_mapping_get_dev_info(sc, &topo_change);
	_mapping_clear_removed_entries(sc);
	_mapping_add_new_device(sc, &topo_change);

out:
	free(topo_change.phy_details, M_MPT2);
	_mapping_flush_dpm_pages(sc);
	if (sc->pending_map_events)
		sc->pending_map_events--;
}

/**
 * mps_mapping_ir_config_change_event - handle IR config change list events
 * @sc: per adapter object
 * @event_data: event data payload
 *
 * Returns nothing.
 */
void
mps_mapping_ir_config_change_event(struct mps_softc *sc,
    Mpi2EventDataIrConfigChangeList_t *event_data)
{
	Mpi2EventIrConfigElement_t *element;
	int i;
	u64 *wwid_table;
	u32 map_idx, flags;
	struct dev_mapping_table *mt_entry;
	u16 element_flags;

	wwid_table = malloc(sizeof(u64) * event_data->NumElements, M_MPT2,
	    M_NOWAIT | M_ZERO);
	if (!wwid_table)
		goto out;
	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
	flags = le32toh(event_data->Flags);

	/*
	 * For volume changes, get the WWID for the volume and put it in a
	 * table to be used in the processing of the IR change event.
	 */
	for (i = 0; i < event_data->NumElements; i++, element++) {
		element_flags = le16toh(element->ElementFlags);
		if ((element->ReasonCode != MPI2_EVENT_IR_CHANGE_RC_ADDED) &&
		    (element->ReasonCode != MPI2_EVENT_IR_CHANGE_RC_REMOVED) &&
		    (element->ReasonCode != MPI2_EVENT_IR_CHANGE_RC_NO_CHANGE)
		    && (element->ReasonCode !=
			MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED))
			continue;
		if ((element_flags &
		    MPI2_EVENT_IR_CHANGE_EFLAGS_ELEMENT_TYPE_MASK) ==
		    MPI2_EVENT_IR_CHANGE_EFLAGS_VOLUME_ELEMENT) {
			mps_config_get_volume_wwid(sc,
			    le16toh(element->VolDevHandle), &wwid_table[i]);
		}
	}

	/*
	 * Check the ReasonCode for each element in the IR event and Add/Remove
	 * Volumes or Physical Disks of Volumes to/from the mapping table. Use
	 * the WWIDs gotten above in wwid_table.
	 */
	if (flags == MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG)
		goto out;
	else {
		element = (Mpi2EventIrConfigElement_t *)&event_data->
		    ConfigElement[0];
		for (i = 0; i < event_data->NumElements; i++, element++) {
			if (element->ReasonCode ==
			    MPI2_EVENT_IR_CHANGE_RC_ADDED ||
			    element->ReasonCode ==
			    MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED) {
				map_idx = _mapping_get_ir_mt_idx_from_wwid
				    (sc, wwid_table[i]);
				if (map_idx != MPS_MAPTABLE_BAD_IDX) {
					/*
					 * The volume is already in the mapping
					 * table. Just update it's info.
					 */
					mt_entry = &sc->mapping_table[map_idx];
					mt_entry->id = map_idx;
					mt_entry->dev_handle = le16toh
					    (element->VolDevHandle);
					mt_entry->device_info =
					    MPS_DEV_RESERVED | MPS_MAP_IN_USE;
					_mapping_update_ir_missing_cnt(sc,
					    map_idx, element, wwid_table[i]);
					continue;
				}

				/*
				 * Volume is not in mapping table yet. Find a
				 * free entry in the mapping table at the
				 * volume mapping locations. If no entries are
				 * available, this is an error because it means
				 * there are more volumes than can be mapped
				 * and that should never happen for volumes.
				 */
				map_idx = _mapping_get_free_ir_mt_idx(sc);
				if (map_idx == MPS_MAPTABLE_BAD_IDX)
				{
					mps_dprint(sc, MPS_ERROR | MPS_MAPPING,
					    "%s: failed to add the volume with "
					    "handle 0x%04x because there is no "
					    "free space available in the "
					    "mapping table\n", __func__,
					    le16toh(element->VolDevHandle));
					continue;
				}
				mt_entry = &sc->mapping_table[map_idx];
				mt_entry->physical_id = wwid_table[i];
				mt_entry->id = map_idx;
				mt_entry->dev_handle = le16toh(element->
				    VolDevHandle);
				mt_entry->device_info = MPS_DEV_RESERVED |
				    MPS_MAP_IN_USE;
				_mapping_update_ir_missing_cnt(sc, map_idx,
				    element, wwid_table[i]);
			} else if (element->ReasonCode ==
			    MPI2_EVENT_IR_CHANGE_RC_REMOVED) {
				map_idx = _mapping_get_ir_mt_idx_from_wwid(sc,
				    wwid_table[i]);
				if (map_idx == MPS_MAPTABLE_BAD_IDX) {
					mps_dprint(sc, MPS_MAPPING,"%s: Failed "
					    "to remove a volume because it has "
					    "already been removed.\n",
					    __func__);
					continue;
				}
				_mapping_update_ir_missing_cnt(sc, map_idx,
				    element, wwid_table[i]);
			} else if (element->ReasonCode ==
			    MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED) {
				map_idx = _mapping_get_mt_idx_from_handle(sc,
				    le16toh(element->VolDevHandle));
				if (map_idx == MPS_MAPTABLE_BAD_IDX) {
					mps_dprint(sc, MPS_MAPPING,"%s: Failed "
					    "to remove volume with handle "
					    "0x%04x because it has already "
					    "been removed.\n", __func__,
					    le16toh(element->VolDevHandle));
					continue;
				}
				mt_entry = &sc->mapping_table[map_idx];
				_mapping_update_ir_missing_cnt(sc, map_idx,
				    element, mt_entry->physical_id);
			}
		}
	}

out:
	_mapping_flush_dpm_pages(sc);
	free(wwid_table, M_MPT2);
	if (sc->pending_map_events)
		sc->pending_map_events--;
}

int
mps_mapping_dump(SYSCTL_HANDLER_ARGS)
{
	struct mps_softc *sc;
	struct dev_mapping_table *mt_entry;
	struct sbuf sbuf;
	int i, error;

	sc = (struct mps_softc *)arg1;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);

	sbuf_printf(&sbuf, "\nindex physical_id       handle id\n");
	for (i = 0; i < sc->max_devices; i++) {
		mt_entry = &sc->mapping_table[i];
		if (mt_entry->physical_id == 0)
			continue;
		sbuf_printf(&sbuf, "%4d  %jx  %04x   %hd\n",
		    i, mt_entry->physical_id, mt_entry->dev_handle,
		    mt_entry->id);
	}
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}

int
mps_mapping_encl_dump(SYSCTL_HANDLER_ARGS)
{
	struct mps_softc *sc;
	struct enc_mapping_table *enc_entry;
	struct sbuf sbuf;
	int i, error;

	sc = (struct mps_softc *)arg1;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);

	sbuf_printf(&sbuf, "\nindex enclosure_id      handle map_index\n");
	for (i = 0; i < sc->max_enclosures; i++) {
		enc_entry = &sc->enclosure_table[i];
		if (enc_entry->enclosure_id == 0)
			continue;
		sbuf_printf(&sbuf, "%4d  %jx  %04x   %d\n",
		    i, enc_entry->enclosure_id, enc_entry->enc_handle,
		    enc_entry->start_index);
	}
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);
	return (error);
}
