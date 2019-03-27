/*-
 * Copyright (c) 2009-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "efx.h"
#include "efx_impl.h"
#include "mcdi_mon.h"

#if EFSYS_OPT_MON_MCDI

#if EFSYS_OPT_MON_STATS

/* Get port mask from one-based MCDI port number */
#define	MCDI_MON_PORT_MASK(_emip) (1U << ((_emip)->emi_port - 1))

#define	MCDI_STATIC_SENSOR_ASSERT(_field)				\
	EFX_STATIC_ASSERT(MC_CMD_SENSOR_STATE_ ## _field		\
			    == EFX_MON_STAT_STATE_ ## _field)

static						void
mcdi_mon_decode_stats(
	__in					efx_nic_t *enp,
	__in_bcount(sensor_mask_size)		uint32_t *sensor_mask,
	__in					size_t sensor_mask_size,
	__in_opt				efsys_mem_t *esmp,
	__out_bcount_opt(sensor_mask_size)	uint32_t *stat_maskp,
	__inout_ecount_opt(EFX_MON_NSTATS)	efx_mon_stat_value_t *stat)
{
	efx_mcdi_iface_t *emip = &(enp->en_mcdi.em_emip);
	efx_mon_stat_portmask_t port_mask;
	uint16_t sensor;
	size_t sensor_max;
	uint32_t stat_mask[(EFX_MON_NSTATS + 31) / 32];
	uint32_t idx = 0;
	uint32_t page = 0;

	/* Assert the MC_CMD_SENSOR and EFX_MON_STATE namespaces agree */
	MCDI_STATIC_SENSOR_ASSERT(OK);
	MCDI_STATIC_SENSOR_ASSERT(WARNING);
	MCDI_STATIC_SENSOR_ASSERT(FATAL);
	MCDI_STATIC_SENSOR_ASSERT(BROKEN);
	MCDI_STATIC_SENSOR_ASSERT(NO_READING);

	sensor_max = 8 * sensor_mask_size;

	EFSYS_ASSERT(emip->emi_port > 0); /* MCDI port number is one-based */
	port_mask = (efx_mon_stat_portmask_t)MCDI_MON_PORT_MASK(emip);

	memset(stat_mask, 0, sizeof (stat_mask));

	/*
	 * The MCDI sensor readings in the DMA buffer are a packed array of
	 * MC_CMD_SENSOR_VALUE_ENTRY structures, which only includes entries for
	 * supported sensors (bit set in sensor_mask). The sensor_mask and
	 * sensor readings do not include entries for the per-page NEXT_PAGE
	 * flag.
	 *
	 * sensor_mask may legitimately contain MCDI sensors that the driver
	 * does not understand.
	 */
	for (sensor = 0; sensor < sensor_max; ++sensor) {
		efx_mon_stat_t id;
		efx_mon_stat_portmask_t stat_portmask = 0;
		boolean_t decode_ok;
		efx_mon_stat_unit_t stat_unit;

		if ((sensor % (MC_CMD_SENSOR_PAGE0_NEXT + 1)) ==
		    MC_CMD_SENSOR_PAGE0_NEXT) {
			page++;
			continue;
			/* This sensor is one of the page boundary bits. */
		}

		if (~(sensor_mask[page]) & (1U << sensor))
			continue;
		/* This sensor not in DMA buffer */

		idx++;
		/*
		 * Valid stat in DMA buffer that we need to increment over, even
		 * if we couldn't look up the id
		 */

		decode_ok = efx_mon_mcdi_to_efx_stat(sensor, &id);
		decode_ok =
		    decode_ok && efx_mon_get_stat_portmap(id, &stat_portmask);

		if (!(decode_ok && (stat_portmask & port_mask)))
			continue;
		/* Either bad decode, or don't know what port stat is on */

		EFSYS_ASSERT(id < EFX_MON_NSTATS);

		/*
		 * stat_mask is a bitmask indexed by EFX_MON_* monitor statistic
		 * identifiers from efx_mon_stat_t (without NEXT_PAGE bits).
		 *
		 * If there is an entry in the MCDI sensor to monitor statistic
		 * map then the sensor reading is used for the value of the
		 * monitor statistic.
		 */
		stat_mask[id / EFX_MON_MASK_ELEMENT_SIZE] |=
		    (1U << (id % EFX_MON_MASK_ELEMENT_SIZE));

		if (stat != NULL && esmp != NULL && !EFSYS_MEM_IS_NULL(esmp)) {
			efx_dword_t dword;

			/* Get MCDI sensor reading from DMA buffer */
			EFSYS_MEM_READD(esmp, 4 * (idx - 1), &dword);

			/* Update EFX monitor stat from MCDI sensor reading */
			stat[id].emsv_value = (uint16_t)EFX_DWORD_FIELD(dword,
			    MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE);

			stat[id].emsv_state = (uint16_t)EFX_DWORD_FIELD(dword,
			    MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE);

			stat[id].emsv_unit =
			    efx_mon_get_stat_unit(id, &stat_unit) ?
			    stat_unit : EFX_MON_STAT_UNIT_UNKNOWN;
		}
	}

	if (stat_maskp != NULL) {
		memcpy(stat_maskp, stat_mask, sizeof (stat_mask));
	}
}

	__checkReturn			efx_rc_t
mcdi_mon_ev(
	__in				efx_nic_t *enp,
	__in				efx_qword_t *eqp,
	__out				efx_mon_stat_t *idp,
	__out				efx_mon_stat_value_t *valuep)
{
	efx_mcdi_iface_t *emip = &(enp->en_mcdi.em_emip);
	efx_mon_stat_portmask_t port_mask, sensor_port_mask;
	uint16_t sensor;
	uint16_t state;
	uint16_t value;
	efx_mon_stat_t id;
	efx_rc_t rc;

	EFSYS_ASSERT(emip->emi_port > 0); /* MCDI port number is one-based */
	port_mask = MCDI_MON_PORT_MASK(emip);

	sensor = (uint16_t)MCDI_EV_FIELD(eqp, SENSOREVT_MONITOR);
	state = (uint16_t)MCDI_EV_FIELD(eqp, SENSOREVT_STATE);
	value = (uint16_t)MCDI_EV_FIELD(eqp, SENSOREVT_VALUE);

	/* Hardware must support this MCDI sensor */
	EFSYS_ASSERT3U(sensor, <,
	    (8 * enp->en_nic_cfg.enc_mcdi_sensor_mask_size));
	EFSYS_ASSERT((sensor % (MC_CMD_SENSOR_PAGE0_NEXT + 1)) !=
	    MC_CMD_SENSOR_PAGE0_NEXT);
	EFSYS_ASSERT(enp->en_nic_cfg.enc_mcdi_sensor_maskp != NULL);
	EFSYS_ASSERT((enp->en_nic_cfg.enc_mcdi_sensor_maskp[
		    sensor / (MC_CMD_SENSOR_PAGE0_NEXT + 1)] &
		(1U << (sensor % (MC_CMD_SENSOR_PAGE0_NEXT + 1)))) != 0);

	/* And we need to understand it, to get port-map */
	if (!efx_mon_mcdi_to_efx_stat(sensor, &id)) {
		rc = ENOTSUP;
		goto fail1;
	}
	if (!(efx_mon_get_stat_portmap(id, &sensor_port_mask) &&
		(port_mask && sensor_port_mask))) {
		return (ENODEV);
	}
	EFSYS_ASSERT(id < EFX_MON_NSTATS);

	*idp = id;
	valuep->emsv_value = value;
	valuep->emsv_state = state;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


static	__checkReturn	efx_rc_t
efx_mcdi_read_sensors(
	__in		efx_nic_t *enp,
	__in		efsys_mem_t *esmp,
	__in		uint32_t size)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_READ_SENSORS_EXT_IN_LEN,
		MC_CMD_READ_SENSORS_EXT_OUT_LEN);
	uint32_t addr_lo, addr_hi;
	efx_rc_t rc;

	if (EFSYS_MEM_SIZE(esmp) < size) {
		rc = EINVAL;
		goto fail1;
	}

	req.emr_cmd = MC_CMD_READ_SENSORS;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_READ_SENSORS_EXT_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_READ_SENSORS_EXT_OUT_LEN;

	addr_lo = (uint32_t)(EFSYS_MEM_ADDR(esmp) & 0xffffffff);
	addr_hi = (uint32_t)(EFSYS_MEM_ADDR(esmp) >> 32);

	MCDI_IN_SET_DWORD(req, READ_SENSORS_EXT_IN_DMA_ADDR_LO, addr_lo);
	MCDI_IN_SET_DWORD(req, READ_SENSORS_EXT_IN_DMA_ADDR_HI, addr_hi);
	MCDI_IN_SET_DWORD(req, READ_SENSORS_EXT_IN_LENGTH, size);

	efx_mcdi_execute(enp, &req);

	return (req.emr_rc);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_sensor_info_npages(
	__in		efx_nic_t *enp,
	__out		uint32_t *npagesp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_SENSOR_INFO_EXT_IN_LEN,
		MC_CMD_SENSOR_INFO_OUT_LENMAX);
	int page;
	efx_rc_t rc;

	EFSYS_ASSERT(npagesp != NULL);

	page = 0;
	do {
		(void) memset(payload, 0, sizeof (payload));
		req.emr_cmd = MC_CMD_SENSOR_INFO;
		req.emr_in_buf = payload;
		req.emr_in_length = MC_CMD_SENSOR_INFO_EXT_IN_LEN;
		req.emr_out_buf = payload;
		req.emr_out_length = MC_CMD_SENSOR_INFO_OUT_LENMAX;

		MCDI_IN_SET_DWORD(req, SENSOR_INFO_EXT_IN_PAGE, page++);

		efx_mcdi_execute_quiet(enp, &req);

		if (req.emr_rc != 0) {
			rc = req.emr_rc;
			goto fail1;
		}
	} while (MCDI_OUT_DWORD(req, SENSOR_INFO_OUT_MASK) &
	    (1U << MC_CMD_SENSOR_PAGE0_NEXT));

	*npagesp = page;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn		efx_rc_t
efx_mcdi_sensor_info(
	__in			efx_nic_t *enp,
	__out_ecount(npages)	uint32_t *sensor_maskp,
	__in			size_t npages)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_SENSOR_INFO_EXT_IN_LEN,
		MC_CMD_SENSOR_INFO_OUT_LENMAX);
	uint32_t page;
	efx_rc_t rc;

	EFSYS_ASSERT(sensor_maskp != NULL);

	if (npages < 1) {
		rc = EINVAL;
		goto fail1;
	}

	for (page = 0; page < npages; page++) {
		uint32_t mask;

		(void) memset(payload, 0, sizeof (payload));
		req.emr_cmd = MC_CMD_SENSOR_INFO;
		req.emr_in_buf = payload;
		req.emr_in_length = MC_CMD_SENSOR_INFO_EXT_IN_LEN;
		req.emr_out_buf = payload;
		req.emr_out_length = MC_CMD_SENSOR_INFO_OUT_LENMAX;

		MCDI_IN_SET_DWORD(req, SENSOR_INFO_EXT_IN_PAGE, page);

		efx_mcdi_execute(enp, &req);

		if (req.emr_rc != 0) {
			rc = req.emr_rc;
			goto fail2;
		}

		mask = MCDI_OUT_DWORD(req, SENSOR_INFO_OUT_MASK);

		if ((page != (npages - 1)) &&
		    ((mask & (1U << MC_CMD_SENSOR_PAGE0_NEXT)) == 0)) {
			rc = EINVAL;
			goto fail3;
		}
		sensor_maskp[page] = mask;
	}

	if (sensor_maskp[npages - 1] & (1U << MC_CMD_SENSOR_PAGE0_NEXT)) {
		rc = EINVAL;
		goto fail4;
	}

	return (0);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn		efx_rc_t
efx_mcdi_sensor_info_page(
	__in			efx_nic_t *enp,
	__in			uint32_t page,
	__out			uint32_t *mask_part,
	__out_ecount((sizeof (*mask_part) * 8) - 1)
				efx_mon_stat_limits_t *limits)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_SENSOR_INFO_EXT_IN_LEN,
		MC_CMD_SENSOR_INFO_OUT_LENMAX);
	efx_rc_t rc;
	uint32_t mask_copy;
	efx_dword_t *maskp;
	efx_qword_t *limit_info;

	EFSYS_ASSERT(mask_part != NULL);
	EFSYS_ASSERT(limits != NULL);

	memset(limits, 0,
	    ((sizeof (*mask_part) * 8) - 1) * sizeof (efx_mon_stat_limits_t));

	req.emr_cmd = MC_CMD_SENSOR_INFO;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_SENSOR_INFO_EXT_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_SENSOR_INFO_OUT_LENMAX;

	MCDI_IN_SET_DWORD(req, SENSOR_INFO_EXT_IN_PAGE, page);

	efx_mcdi_execute(enp, &req);

	rc = req.emr_rc;

	if (rc != 0)
		goto fail1;

	EFSYS_ASSERT(sizeof (*limit_info) ==
	    MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_LEN);
	maskp = MCDI_OUT2(req, efx_dword_t, SENSOR_INFO_OUT_MASK);
	limit_info = (efx_qword_t *)(maskp + 1);

	*mask_part = maskp->ed_u32[0];
	mask_copy = *mask_part;

	/* Copy an entry for all but the highest bit set. */
	while (mask_copy) {

		if (mask_copy == (1U << MC_CMD_SENSOR_PAGE0_NEXT)) {
			/* Only next page bit set. */
			mask_copy = 0;
		} else {
			/* Clear lowest bit */
			mask_copy = mask_copy & ~(mask_copy ^ (mask_copy - 1));
			/* And copy out limit entry into buffer */
			limits->emlv_warning_min = EFX_QWORD_FIELD(*limit_info,
			    MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MIN1);

			limits->emlv_warning_max = EFX_QWORD_FIELD(*limit_info,
			    MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MAX1);

			limits->emlv_fatal_min = EFX_QWORD_FIELD(*limit_info,
			    MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MIN2);

			limits->emlv_fatal_max = EFX_QWORD_FIELD(*limit_info,
			    MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MAX2);

			limits++;
			limit_info++;
		}
	}

	return (rc);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn			efx_rc_t
mcdi_mon_stats_update(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__inout_ecount(EFX_MON_NSTATS)	efx_mon_stat_value_t *values)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	uint32_t size = encp->enc_mon_stat_dma_buf_size;
	efx_rc_t rc;

	if ((rc = efx_mcdi_read_sensors(enp, esmp, size)) != 0)
		goto fail1;

	EFSYS_DMA_SYNC_FOR_KERNEL(esmp, 0, size);

	mcdi_mon_decode_stats(enp,
	    encp->enc_mcdi_sensor_maskp,
	    encp->enc_mcdi_sensor_mask_size,
	    esmp, NULL, values);

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static		void
lowest_set_bit(
	__in	uint32_t input_mask,
	__out	uint32_t *lowest_bit_mask,
	__out	uint32_t *lowest_bit_num
)
{
	uint32_t x;
	uint32_t set_bit, bit_index;

	x = (input_mask ^ (input_mask - 1));
	set_bit = (x + 1) >> 1;
	if (!set_bit)
		set_bit = (1U << 31U);

	bit_index = 0;
	if (set_bit & 0xFFFF0000)
		bit_index += 16;
	if (set_bit & 0xFF00FF00)
		bit_index += 8;
	if (set_bit & 0xF0F0F0F0)
		bit_index += 4;
	if (set_bit & 0xCCCCCCCC)
		bit_index += 2;
	if (set_bit & 0xAAAAAAAA)
		bit_index += 1;

	*lowest_bit_mask = set_bit;
	*lowest_bit_num = bit_index;
}

	__checkReturn			efx_rc_t
mcdi_mon_limits_update(
	__in				efx_nic_t *enp,
	__inout_ecount(EFX_MON_NSTATS)	efx_mon_stat_limits_t *values)
{
	efx_rc_t rc;
	uint32_t page;
	uint32_t page_mask;
	uint32_t limit_index;
	efx_mon_stat_limits_t limits[sizeof (page_mask) * 8];
	efx_mon_stat_t stat;

	page = 0;
	page--;
	do {
		page++;

		rc = efx_mcdi_sensor_info_page(enp, page, &page_mask, limits);
		if (rc != 0)
			goto fail1;

		limit_index = 0;
		while (page_mask) {
			uint32_t set_bit;
			uint32_t page_index;
			uint32_t mcdi_index;

			if (page_mask == (1U << MC_CMD_SENSOR_PAGE0_NEXT))
				break;

			lowest_set_bit(page_mask, &set_bit, &page_index);
			page_mask = page_mask & ~set_bit;

			mcdi_index =
			    page_index + (sizeof (page_mask) * 8 * page);

			/*
			 * This can fail if MCDI reports newer stats than the
			 * drivers understand, or the bit is the next page bit.
			 *
			 * Driver needs to be tolerant of this.
			 */
			if (!efx_mon_mcdi_to_efx_stat(mcdi_index, &stat))
				continue;

			values[stat] = limits[limit_index];
			limit_index++;
		}

	} while (page_mask & (1U << MC_CMD_SENSOR_PAGE0_NEXT));

	return (rc);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
mcdi_mon_cfg_build(
	__in		efx_nic_t *enp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	uint32_t npages;
	efx_rc_t rc;

	switch (enp->en_family) {
#if EFSYS_OPT_SIENA
	case EFX_FAMILY_SIENA:
		encp->enc_mon_type = EFX_MON_SFC90X0;
		break;
#endif
#if EFSYS_OPT_HUNTINGTON
	case EFX_FAMILY_HUNTINGTON:
		encp->enc_mon_type = EFX_MON_SFC91X0;
		break;
#endif
#if EFSYS_OPT_MEDFORD
	case EFX_FAMILY_MEDFORD:
		encp->enc_mon_type = EFX_MON_SFC92X0;
		break;
#endif
#if EFSYS_OPT_MEDFORD2
	case EFX_FAMILY_MEDFORD2:
		encp->enc_mon_type = EFX_MON_SFC92X0;
		break;
#endif
	default:
		rc = EINVAL;
		goto fail1;
	}

	/* Get mc sensor mask size */
	npages = 0;
	if ((rc = efx_mcdi_sensor_info_npages(enp, &npages)) != 0)
		goto fail2;

	encp->enc_mon_stat_dma_buf_size	= npages * EFX_MON_STATS_PAGE_SIZE;
	encp->enc_mcdi_sensor_mask_size = npages * sizeof (uint32_t);

	/* Allocate mc sensor mask */
	EFSYS_KMEM_ALLOC(enp->en_esip,
	    encp->enc_mcdi_sensor_mask_size,
	    encp->enc_mcdi_sensor_maskp);

	if (encp->enc_mcdi_sensor_maskp == NULL) {
		rc = ENOMEM;
		goto fail3;
	}

	/* Read mc sensor mask */
	if ((rc = efx_mcdi_sensor_info(enp,
		    encp->enc_mcdi_sensor_maskp,
		    npages)) != 0)
		goto fail4;

	/* Build monitor statistics mask */
	mcdi_mon_decode_stats(enp,
	    encp->enc_mcdi_sensor_maskp,
	    encp->enc_mcdi_sensor_mask_size,
	    NULL, encp->enc_mon_stat_mask, NULL);

	return (0);

fail4:
	EFSYS_PROBE(fail4);
	EFSYS_KMEM_FREE(enp->en_esip,
	    encp->enc_mcdi_sensor_mask_size,
	    encp->enc_mcdi_sensor_maskp);

fail3:
	EFSYS_PROBE(fail3);

fail2:
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

			void
mcdi_mon_cfg_free(
	__in		efx_nic_t *enp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);

	if (encp->enc_mcdi_sensor_maskp != NULL) {
		EFSYS_KMEM_FREE(enp->en_esip,
		    encp->enc_mcdi_sensor_mask_size,
		    encp->enc_mcdi_sensor_maskp);
	}
}


#endif	/* EFSYS_OPT_MON_STATS */

#endif	/* EFSYS_OPT_MON_MCDI */
