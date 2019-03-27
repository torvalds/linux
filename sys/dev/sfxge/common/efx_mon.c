/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2016 Solarflare Communications Inc.
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

#if EFSYS_OPT_MON_MCDI
#include "mcdi_mon.h"
#endif

#if EFSYS_OPT_NAMES

static const char * const __efx_mon_name[] = {
	"",
	"sfx90x0",
	"sfx91x0",
	"sfx92x0"
};

		const char *
efx_mon_name(
	__in	efx_nic_t *enp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);

	EFSYS_ASSERT(encp->enc_mon_type != EFX_MON_INVALID);
	EFSYS_ASSERT3U(encp->enc_mon_type, <, EFX_MON_NTYPES);
	return (__efx_mon_name[encp->enc_mon_type]);
}

#endif	/* EFSYS_OPT_NAMES */

#if EFSYS_OPT_MON_MCDI
static const efx_mon_ops_t	__efx_mon_mcdi_ops = {
#if EFSYS_OPT_MON_STATS
	mcdi_mon_stats_update,		/* emo_stats_update */
	mcdi_mon_limits_update,		/* emo_limits_update */
#endif	/* EFSYS_OPT_MON_STATS */
};
#endif


	__checkReturn	efx_rc_t
efx_mon_init(
	__in		efx_nic_t *enp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_mon_t *emp = &(enp->en_mon);
	const efx_mon_ops_t *emop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);

	if (enp->en_mod_flags & EFX_MOD_MON) {
		rc = EINVAL;
		goto fail1;
	}

	enp->en_mod_flags |= EFX_MOD_MON;

	emp->em_type = encp->enc_mon_type;

	EFSYS_ASSERT(encp->enc_mon_type != EFX_MON_INVALID);
	switch (emp->em_type) {
#if EFSYS_OPT_MON_MCDI
	case EFX_MON_SFC90X0:
	case EFX_MON_SFC91X0:
	case EFX_MON_SFC92X0:
		emop = &__efx_mon_mcdi_ops;
		break;
#endif
	default:
		rc = ENOTSUP;
		goto fail2;
	}

	emp->em_emop = emop;
	return (0);

fail2:
	EFSYS_PROBE(fail2);

	emp->em_type = EFX_MON_INVALID;

	enp->en_mod_flags &= ~EFX_MOD_MON;

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#if EFSYS_OPT_MON_STATS

#if EFSYS_OPT_NAMES

/* START MKCONFIG GENERATED MonitorStatNamesBlock 277c17eda1a6d1a4 */
static const char * const __mon_stat_name[] = {
	"controller_temp",
	"phy_common_temp",
	"controller_cooling",
	"phy0_temp",
	"phy0_cooling",
	"phy1_temp",
	"phy1_cooling",
	"in_1v0",
	"in_1v2",
	"in_1v8",
	"in_2v5",
	"in_3v3",
	"in_12v0",
	"in_1v2a",
	"in_vref",
	"out_vaoe",
	"aoe_temp",
	"psu_aoe_temp",
	"psu_temp",
	"fan_0",
	"fan_1",
	"fan_2",
	"fan_3",
	"fan_4",
	"in_vaoe",
	"out_iaoe",
	"in_iaoe",
	"nic_power",
	"in_0v9",
	"in_i0v9",
	"in_i1v2",
	"in_0v9_adc",
	"controller_2_temp",
	"vreg_internal_temp",
	"vreg_0v9_temp",
	"vreg_1v2_temp",
	"controller_vptat",
	"controller_internal_temp",
	"controller_vptat_extadc",
	"controller_internal_temp_extadc",
	"ambient_temp",
	"airflow",
	"vdd08d_vss08d_csr",
	"vdd08d_vss08d_csr_extadc",
	"hotpoint_temp",
	"phy_power_port0",
	"phy_power_port1",
	"mum_vcc",
	"in_0v9_a",
	"in_i0v9_a",
	"vreg_0v9_a_temp",
	"in_0v9_b",
	"in_i0v9_b",
	"vreg_0v9_b_temp",
	"ccom_avreg_1v2_supply",
	"ccom_avreg_1v2_supply_extadc",
	"ccom_avreg_1v8_supply",
	"ccom_avreg_1v8_supply_extadc",
	"controller_master_vptat",
	"controller_master_internal_temp",
	"controller_master_vptat_extadc",
	"controller_master_internal_temp_extadc",
	"controller_slave_vptat",
	"controller_slave_internal_temp",
	"controller_slave_vptat_extadc",
	"controller_slave_internal_temp_extadc",
	"sodimm_vout",
	"sodimm_0_temp",
	"sodimm_1_temp",
	"phy0_vcc",
	"phy1_vcc",
	"controller_tdiode_temp",
	"board_front_temp",
	"board_back_temp",
	"in_i1v8",
	"in_i2v5",
	"in_i3v3",
	"in_i12v0",
	"in_1v3",
	"in_i1v3",
};

/* END MKCONFIG GENERATED MonitorStatNamesBlock */

					const char *
efx_mon_stat_name(
	__in				efx_nic_t *enp,
	__in				efx_mon_stat_t id)
{
	_NOTE(ARGUNUSED(enp))
	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);

	EFSYS_ASSERT3U(id, <, EFX_MON_NSTATS);
	return (__mon_stat_name[id]);
}

typedef struct _stat_description_t {
	efx_mon_stat_t	stat;
	const char	*desc;
} stat_description_t;

/* START MKCONFIG GENERATED MonitorStatDescriptionsBlock f072138f16d2e1f8 */
static const char *__mon_stat_description[] = {
	MC_CMD_SENSOR_CONTROLLER_TEMP_ENUM_STR,
	MC_CMD_SENSOR_PHY_COMMON_TEMP_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_COOLING_ENUM_STR,
	MC_CMD_SENSOR_PHY0_TEMP_ENUM_STR,
	MC_CMD_SENSOR_PHY0_COOLING_ENUM_STR,
	MC_CMD_SENSOR_PHY1_TEMP_ENUM_STR,
	MC_CMD_SENSOR_PHY1_COOLING_ENUM_STR,
	MC_CMD_SENSOR_IN_1V0_ENUM_STR,
	MC_CMD_SENSOR_IN_1V2_ENUM_STR,
	MC_CMD_SENSOR_IN_1V8_ENUM_STR,
	MC_CMD_SENSOR_IN_2V5_ENUM_STR,
	MC_CMD_SENSOR_IN_3V3_ENUM_STR,
	MC_CMD_SENSOR_IN_12V0_ENUM_STR,
	MC_CMD_SENSOR_IN_1V2A_ENUM_STR,
	MC_CMD_SENSOR_IN_VREF_ENUM_STR,
	MC_CMD_SENSOR_OUT_VAOE_ENUM_STR,
	MC_CMD_SENSOR_AOE_TEMP_ENUM_STR,
	MC_CMD_SENSOR_PSU_AOE_TEMP_ENUM_STR,
	MC_CMD_SENSOR_PSU_TEMP_ENUM_STR,
	MC_CMD_SENSOR_FAN_0_ENUM_STR,
	MC_CMD_SENSOR_FAN_1_ENUM_STR,
	MC_CMD_SENSOR_FAN_2_ENUM_STR,
	MC_CMD_SENSOR_FAN_3_ENUM_STR,
	MC_CMD_SENSOR_FAN_4_ENUM_STR,
	MC_CMD_SENSOR_IN_VAOE_ENUM_STR,
	MC_CMD_SENSOR_OUT_IAOE_ENUM_STR,
	MC_CMD_SENSOR_IN_IAOE_ENUM_STR,
	MC_CMD_SENSOR_NIC_POWER_ENUM_STR,
	MC_CMD_SENSOR_IN_0V9_ENUM_STR,
	MC_CMD_SENSOR_IN_I0V9_ENUM_STR,
	MC_CMD_SENSOR_IN_I1V2_ENUM_STR,
	MC_CMD_SENSOR_IN_0V9_ADC_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_2_TEMP_ENUM_STR,
	MC_CMD_SENSOR_VREG_INTERNAL_TEMP_ENUM_STR,
	MC_CMD_SENSOR_VREG_0V9_TEMP_ENUM_STR,
	MC_CMD_SENSOR_VREG_1V2_TEMP_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_VPTAT_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_INTERNAL_TEMP_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_VPTAT_EXTADC_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_INTERNAL_TEMP_EXTADC_ENUM_STR,
	MC_CMD_SENSOR_AMBIENT_TEMP_ENUM_STR,
	MC_CMD_SENSOR_AIRFLOW_ENUM_STR,
	MC_CMD_SENSOR_VDD08D_VSS08D_CSR_ENUM_STR,
	MC_CMD_SENSOR_VDD08D_VSS08D_CSR_EXTADC_ENUM_STR,
	MC_CMD_SENSOR_HOTPOINT_TEMP_ENUM_STR,
	MC_CMD_SENSOR_PHY_POWER_PORT0_ENUM_STR,
	MC_CMD_SENSOR_PHY_POWER_PORT1_ENUM_STR,
	MC_CMD_SENSOR_MUM_VCC_ENUM_STR,
	MC_CMD_SENSOR_IN_0V9_A_ENUM_STR,
	MC_CMD_SENSOR_IN_I0V9_A_ENUM_STR,
	MC_CMD_SENSOR_VREG_0V9_A_TEMP_ENUM_STR,
	MC_CMD_SENSOR_IN_0V9_B_ENUM_STR,
	MC_CMD_SENSOR_IN_I0V9_B_ENUM_STR,
	MC_CMD_SENSOR_VREG_0V9_B_TEMP_ENUM_STR,
	MC_CMD_SENSOR_CCOM_AVREG_1V2_SUPPLY_ENUM_STR,
	MC_CMD_SENSOR_CCOM_AVREG_1V2_SUPPLY_EXTADC_ENUM_STR,
	MC_CMD_SENSOR_CCOM_AVREG_1V8_SUPPLY_ENUM_STR,
	MC_CMD_SENSOR_CCOM_AVREG_1V8_SUPPLY_EXTADC_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_MASTER_VPTAT_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_MASTER_INTERNAL_TEMP_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_MASTER_VPTAT_EXTADC_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_MASTER_INTERNAL_TEMP_EXTADC_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_SLAVE_VPTAT_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_SLAVE_INTERNAL_TEMP_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_SLAVE_VPTAT_EXTADC_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_SLAVE_INTERNAL_TEMP_EXTADC_ENUM_STR,
	MC_CMD_SENSOR_SODIMM_VOUT_ENUM_STR,
	MC_CMD_SENSOR_SODIMM_0_TEMP_ENUM_STR,
	MC_CMD_SENSOR_SODIMM_1_TEMP_ENUM_STR,
	MC_CMD_SENSOR_PHY0_VCC_ENUM_STR,
	MC_CMD_SENSOR_PHY1_VCC_ENUM_STR,
	MC_CMD_SENSOR_CONTROLLER_TDIODE_TEMP_ENUM_STR,
	MC_CMD_SENSOR_BOARD_FRONT_TEMP_ENUM_STR,
	MC_CMD_SENSOR_BOARD_BACK_TEMP_ENUM_STR,
	MC_CMD_SENSOR_IN_I1V8_ENUM_STR,
	MC_CMD_SENSOR_IN_I2V5_ENUM_STR,
	MC_CMD_SENSOR_IN_I3V3_ENUM_STR,
	MC_CMD_SENSOR_IN_I12V0_ENUM_STR,
	MC_CMD_SENSOR_IN_1V3_ENUM_STR,
	MC_CMD_SENSOR_IN_I1V3_ENUM_STR,
};

/* END MKCONFIG GENERATED MonitorStatDescriptionsBlock */

					const char *
efx_mon_stat_description(
	__in				efx_nic_t *enp,
	__in				efx_mon_stat_t id)
{
	_NOTE(ARGUNUSED(enp))
	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);

	EFSYS_ASSERT3U(id, <, EFX_MON_NSTATS);
	return (__mon_stat_description[id]);
}

#endif	/* EFSYS_OPT_NAMES */

/* START MKCONFIG GENERATED MonitorMcdiMappingBlock 173eee0a5599996a */
	__checkReturn			boolean_t
efx_mon_mcdi_to_efx_stat(
	__in				int mcdi_index,
	__out				efx_mon_stat_t *statp)
{

	if ((mcdi_index % (MC_CMD_SENSOR_PAGE0_NEXT + 1)) ==
	    MC_CMD_SENSOR_PAGE0_NEXT) {
		*statp = EFX_MON_NSTATS;
		return (B_FALSE);
	}

	switch (mcdi_index) {
	case MC_CMD_SENSOR_IN_I0V9:
		*statp = EFX_MON_STAT_IN_I0V9;
		break;
	case MC_CMD_SENSOR_CONTROLLER_SLAVE_VPTAT_EXTADC:
		*statp = EFX_MON_STAT_CONTROLLER_SLAVE_VPTAT_EXTADC;
		break;
	case MC_CMD_SENSOR_CONTROLLER_SLAVE_VPTAT:
		*statp = EFX_MON_STAT_CONTROLLER_SLAVE_VPTAT;
		break;
	case MC_CMD_SENSOR_PSU_TEMP:
		*statp = EFX_MON_STAT_PSU_TEMP;
		break;
	case MC_CMD_SENSOR_FAN_2:
		*statp = EFX_MON_STAT_FAN_2;
		break;
	case MC_CMD_SENSOR_CONTROLLER_INTERNAL_TEMP_EXTADC:
		*statp = EFX_MON_STAT_CONTROLLER_INTERNAL_TEMP_EXTADC;
		break;
	case MC_CMD_SENSOR_BOARD_BACK_TEMP:
		*statp = EFX_MON_STAT_BOARD_BACK_TEMP;
		break;
	case MC_CMD_SENSOR_IN_1V3:
		*statp = EFX_MON_STAT_IN_1V3;
		break;
	case MC_CMD_SENSOR_CONTROLLER_TDIODE_TEMP:
		*statp = EFX_MON_STAT_CONTROLLER_TDIODE_TEMP;
		break;
	case MC_CMD_SENSOR_IN_2V5:
		*statp = EFX_MON_STAT_IN_2V5;
		break;
	case MC_CMD_SENSOR_PHY_COMMON_TEMP:
		*statp = EFX_MON_STAT_PHY_COMMON_TEMP;
		break;
	case MC_CMD_SENSOR_PHY1_TEMP:
		*statp = EFX_MON_STAT_PHY1_TEMP;
		break;
	case MC_CMD_SENSOR_VREG_INTERNAL_TEMP:
		*statp = EFX_MON_STAT_VREG_INTERNAL_TEMP;
		break;
	case MC_CMD_SENSOR_IN_1V0:
		*statp = EFX_MON_STAT_IN_1V0;
		break;
	case MC_CMD_SENSOR_FAN_1:
		*statp = EFX_MON_STAT_FAN_1;
		break;
	case MC_CMD_SENSOR_IN_1V2:
		*statp = EFX_MON_STAT_IN_1V2;
		break;
	case MC_CMD_SENSOR_FAN_3:
		*statp = EFX_MON_STAT_FAN_3;
		break;
	case MC_CMD_SENSOR_IN_1V2A:
		*statp = EFX_MON_STAT_IN_1V2A;
		break;
	case MC_CMD_SENSOR_SODIMM_0_TEMP:
		*statp = EFX_MON_STAT_SODIMM_0_TEMP;
		break;
	case MC_CMD_SENSOR_IN_1V8:
		*statp = EFX_MON_STAT_IN_1V8;
		break;
	case MC_CMD_SENSOR_IN_VREF:
		*statp = EFX_MON_STAT_IN_VREF;
		break;
	case MC_CMD_SENSOR_SODIMM_VOUT:
		*statp = EFX_MON_STAT_SODIMM_VOUT;
		break;
	case MC_CMD_SENSOR_CCOM_AVREG_1V2_SUPPLY:
		*statp = EFX_MON_STAT_CCOM_AVREG_1V2_SUPPLY;
		break;
	case MC_CMD_SENSOR_IN_I1V2:
		*statp = EFX_MON_STAT_IN_I1V2;
		break;
	case MC_CMD_SENSOR_IN_I1V3:
		*statp = EFX_MON_STAT_IN_I1V3;
		break;
	case MC_CMD_SENSOR_AIRFLOW:
		*statp = EFX_MON_STAT_AIRFLOW;
		break;
	case MC_CMD_SENSOR_HOTPOINT_TEMP:
		*statp = EFX_MON_STAT_HOTPOINT_TEMP;
		break;
	case MC_CMD_SENSOR_VDD08D_VSS08D_CSR:
		*statp = EFX_MON_STAT_VDD08D_VSS08D_CSR;
		break;
	case MC_CMD_SENSOR_AOE_TEMP:
		*statp = EFX_MON_STAT_AOE_TEMP;
		break;
	case MC_CMD_SENSOR_IN_I1V8:
		*statp = EFX_MON_STAT_IN_I1V8;
		break;
	case MC_CMD_SENSOR_IN_I2V5:
		*statp = EFX_MON_STAT_IN_I2V5;
		break;
	case MC_CMD_SENSOR_PHY1_COOLING:
		*statp = EFX_MON_STAT_PHY1_COOLING;
		break;
	case MC_CMD_SENSOR_CCOM_AVREG_1V8_SUPPLY_EXTADC:
		*statp = EFX_MON_STAT_CCOM_AVREG_1V8_SUPPLY_EXTADC;
		break;
	case MC_CMD_SENSOR_IN_0V9_ADC:
		*statp = EFX_MON_STAT_IN_0V9_ADC;
		break;
	case MC_CMD_SENSOR_VREG_0V9_A_TEMP:
		*statp = EFX_MON_STAT_VREG_0V9_A_TEMP;
		break;
	case MC_CMD_SENSOR_CONTROLLER_MASTER_VPTAT:
		*statp = EFX_MON_STAT_CONTROLLER_MASTER_VPTAT;
		break;
	case MC_CMD_SENSOR_PHY0_VCC:
		*statp = EFX_MON_STAT_PHY0_VCC;
		break;
	case MC_CMD_SENSOR_PHY0_COOLING:
		*statp = EFX_MON_STAT_PHY0_COOLING;
		break;
	case MC_CMD_SENSOR_PSU_AOE_TEMP:
		*statp = EFX_MON_STAT_PSU_AOE_TEMP;
		break;
	case MC_CMD_SENSOR_VREG_0V9_TEMP:
		*statp = EFX_MON_STAT_VREG_0V9_TEMP;
		break;
	case MC_CMD_SENSOR_IN_I0V9_A:
		*statp = EFX_MON_STAT_IN_I0V9_A;
		break;
	case MC_CMD_SENSOR_IN_I3V3:
		*statp = EFX_MON_STAT_IN_I3V3;
		break;
	case MC_CMD_SENSOR_BOARD_FRONT_TEMP:
		*statp = EFX_MON_STAT_BOARD_FRONT_TEMP;
		break;
	case MC_CMD_SENSOR_OUT_VAOE:
		*statp = EFX_MON_STAT_OUT_VAOE;
		break;
	case MC_CMD_SENSOR_VDD08D_VSS08D_CSR_EXTADC:
		*statp = EFX_MON_STAT_VDD08D_VSS08D_CSR_EXTADC;
		break;
	case MC_CMD_SENSOR_IN_I12V0:
		*statp = EFX_MON_STAT_IN_I12V0;
		break;
	case MC_CMD_SENSOR_PHY_POWER_PORT1:
		*statp = EFX_MON_STAT_PHY_POWER_PORT1;
		break;
	case MC_CMD_SENSOR_PHY_POWER_PORT0:
		*statp = EFX_MON_STAT_PHY_POWER_PORT0;
		break;
	case MC_CMD_SENSOR_CONTROLLER_SLAVE_INTERNAL_TEMP_EXTADC:
		*statp = EFX_MON_STAT_CONTROLLER_SLAVE_INTERNAL_TEMP_EXTADC;
		break;
	case MC_CMD_SENSOR_CONTROLLER_MASTER_INTERNAL_TEMP:
		*statp = EFX_MON_STAT_CONTROLLER_MASTER_INTERNAL_TEMP;
		break;
	case MC_CMD_SENSOR_CONTROLLER_TEMP:
		*statp = EFX_MON_STAT_CONTROLLER_TEMP;
		break;
	case MC_CMD_SENSOR_IN_IAOE:
		*statp = EFX_MON_STAT_IN_IAOE;
		break;
	case MC_CMD_SENSOR_IN_VAOE:
		*statp = EFX_MON_STAT_IN_VAOE;
		break;
	case MC_CMD_SENSOR_CONTROLLER_MASTER_VPTAT_EXTADC:
		*statp = EFX_MON_STAT_CONTROLLER_MASTER_VPTAT_EXTADC;
		break;
	case MC_CMD_SENSOR_CCOM_AVREG_1V8_SUPPLY:
		*statp = EFX_MON_STAT_CCOM_AVREG_1V8_SUPPLY;
		break;
	case MC_CMD_SENSOR_PHY1_VCC:
		*statp = EFX_MON_STAT_PHY1_VCC;
		break;
	case MC_CMD_SENSOR_CONTROLLER_COOLING:
		*statp = EFX_MON_STAT_CONTROLLER_COOLING;
		break;
	case MC_CMD_SENSOR_AMBIENT_TEMP:
		*statp = EFX_MON_STAT_AMBIENT_TEMP;
		break;
	case MC_CMD_SENSOR_IN_3V3:
		*statp = EFX_MON_STAT_IN_3V3;
		break;
	case MC_CMD_SENSOR_PHY0_TEMP:
		*statp = EFX_MON_STAT_PHY0_TEMP;
		break;
	case MC_CMD_SENSOR_SODIMM_1_TEMP:
		*statp = EFX_MON_STAT_SODIMM_1_TEMP;
		break;
	case MC_CMD_SENSOR_MUM_VCC:
		*statp = EFX_MON_STAT_MUM_VCC;
		break;
	case MC_CMD_SENSOR_VREG_0V9_B_TEMP:
		*statp = EFX_MON_STAT_VREG_0V9_B_TEMP;
		break;
	case MC_CMD_SENSOR_CONTROLLER_SLAVE_INTERNAL_TEMP:
		*statp = EFX_MON_STAT_CONTROLLER_SLAVE_INTERNAL_TEMP;
		break;
	case MC_CMD_SENSOR_FAN_4:
		*statp = EFX_MON_STAT_FAN_4;
		break;
	case MC_CMD_SENSOR_CONTROLLER_2_TEMP:
		*statp = EFX_MON_STAT_CONTROLLER_2_TEMP;
		break;
	case MC_CMD_SENSOR_CCOM_AVREG_1V2_SUPPLY_EXTADC:
		*statp = EFX_MON_STAT_CCOM_AVREG_1V2_SUPPLY_EXTADC;
		break;
	case MC_CMD_SENSOR_IN_0V9_A:
		*statp = EFX_MON_STAT_IN_0V9_A;
		break;
	case MC_CMD_SENSOR_CONTROLLER_VPTAT_EXTADC:
		*statp = EFX_MON_STAT_CONTROLLER_VPTAT_EXTADC;
		break;
	case MC_CMD_SENSOR_IN_0V9:
		*statp = EFX_MON_STAT_IN_0V9;
		break;
	case MC_CMD_SENSOR_IN_I0V9_B:
		*statp = EFX_MON_STAT_IN_I0V9_B;
		break;
	case MC_CMD_SENSOR_NIC_POWER:
		*statp = EFX_MON_STAT_NIC_POWER;
		break;
	case MC_CMD_SENSOR_IN_12V0:
		*statp = EFX_MON_STAT_IN_12V0;
		break;
	case MC_CMD_SENSOR_OUT_IAOE:
		*statp = EFX_MON_STAT_OUT_IAOE;
		break;
	case MC_CMD_SENSOR_CONTROLLER_VPTAT:
		*statp = EFX_MON_STAT_CONTROLLER_VPTAT;
		break;
	case MC_CMD_SENSOR_CONTROLLER_MASTER_INTERNAL_TEMP_EXTADC:
		*statp = EFX_MON_STAT_CONTROLLER_MASTER_INTERNAL_TEMP_EXTADC;
		break;
	case MC_CMD_SENSOR_CONTROLLER_INTERNAL_TEMP:
		*statp = EFX_MON_STAT_CONTROLLER_INTERNAL_TEMP;
		break;
	case MC_CMD_SENSOR_FAN_0:
		*statp = EFX_MON_STAT_FAN_0;
		break;
	case MC_CMD_SENSOR_VREG_1V2_TEMP:
		*statp = EFX_MON_STAT_VREG_1V2_TEMP;
		break;
	case MC_CMD_SENSOR_IN_0V9_B:
		*statp = EFX_MON_STAT_IN_0V9_B;
		break;
	default:
		*statp = EFX_MON_NSTATS;
		break;
	};

	if (*statp == EFX_MON_NSTATS)
		goto fail1;

	return (B_TRUE);

fail1:
	EFSYS_PROBE1(fail1, boolean_t, B_TRUE);
	return (B_FALSE);
};

/* END MKCONFIG GENERATED MonitorMcdiMappingBlock */

/* START MKCONFIG GENERATED MonitorStatisticUnitsBlock 2d447c656cc2d01d */
	__checkReturn			boolean_t
efx_mon_get_stat_unit(
	__in				efx_mon_stat_t stat,
	__out				efx_mon_stat_unit_t *unitp)
{
	switch (stat) {
	case EFX_MON_STAT_IN_1V0:
	case EFX_MON_STAT_IN_1V2:
	case EFX_MON_STAT_IN_1V8:
	case EFX_MON_STAT_IN_2V5:
	case EFX_MON_STAT_IN_3V3:
	case EFX_MON_STAT_IN_12V0:
	case EFX_MON_STAT_IN_1V2A:
	case EFX_MON_STAT_IN_VREF:
	case EFX_MON_STAT_OUT_VAOE:
	case EFX_MON_STAT_IN_VAOE:
	case EFX_MON_STAT_IN_0V9:
	case EFX_MON_STAT_IN_0V9_ADC:
	case EFX_MON_STAT_CONTROLLER_VPTAT_EXTADC:
	case EFX_MON_STAT_VDD08D_VSS08D_CSR:
	case EFX_MON_STAT_VDD08D_VSS08D_CSR_EXTADC:
	case EFX_MON_STAT_MUM_VCC:
	case EFX_MON_STAT_IN_0V9_A:
	case EFX_MON_STAT_IN_0V9_B:
	case EFX_MON_STAT_CCOM_AVREG_1V2_SUPPLY:
	case EFX_MON_STAT_CCOM_AVREG_1V2_SUPPLY_EXTADC:
	case EFX_MON_STAT_CCOM_AVREG_1V8_SUPPLY:
	case EFX_MON_STAT_CCOM_AVREG_1V8_SUPPLY_EXTADC:
	case EFX_MON_STAT_CONTROLLER_MASTER_VPTAT:
	case EFX_MON_STAT_CONTROLLER_MASTER_VPTAT_EXTADC:
	case EFX_MON_STAT_CONTROLLER_SLAVE_VPTAT:
	case EFX_MON_STAT_CONTROLLER_SLAVE_VPTAT_EXTADC:
	case EFX_MON_STAT_SODIMM_VOUT:
	case EFX_MON_STAT_PHY0_VCC:
	case EFX_MON_STAT_PHY1_VCC:
	case EFX_MON_STAT_IN_1V3:
		*unitp = EFX_MON_STAT_UNIT_VOLTAGE_MV;
		break;
	case EFX_MON_STAT_CONTROLLER_TEMP:
	case EFX_MON_STAT_PHY_COMMON_TEMP:
	case EFX_MON_STAT_PHY0_TEMP:
	case EFX_MON_STAT_PHY1_TEMP:
	case EFX_MON_STAT_AOE_TEMP:
	case EFX_MON_STAT_PSU_AOE_TEMP:
	case EFX_MON_STAT_PSU_TEMP:
	case EFX_MON_STAT_CONTROLLER_2_TEMP:
	case EFX_MON_STAT_VREG_INTERNAL_TEMP:
	case EFX_MON_STAT_VREG_0V9_TEMP:
	case EFX_MON_STAT_VREG_1V2_TEMP:
	case EFX_MON_STAT_CONTROLLER_VPTAT:
	case EFX_MON_STAT_CONTROLLER_INTERNAL_TEMP:
	case EFX_MON_STAT_CONTROLLER_INTERNAL_TEMP_EXTADC:
	case EFX_MON_STAT_AMBIENT_TEMP:
	case EFX_MON_STAT_HOTPOINT_TEMP:
	case EFX_MON_STAT_VREG_0V9_A_TEMP:
	case EFX_MON_STAT_VREG_0V9_B_TEMP:
	case EFX_MON_STAT_CONTROLLER_MASTER_INTERNAL_TEMP:
	case EFX_MON_STAT_CONTROLLER_MASTER_INTERNAL_TEMP_EXTADC:
	case EFX_MON_STAT_CONTROLLER_SLAVE_INTERNAL_TEMP:
	case EFX_MON_STAT_CONTROLLER_SLAVE_INTERNAL_TEMP_EXTADC:
	case EFX_MON_STAT_SODIMM_0_TEMP:
	case EFX_MON_STAT_SODIMM_1_TEMP:
	case EFX_MON_STAT_CONTROLLER_TDIODE_TEMP:
	case EFX_MON_STAT_BOARD_FRONT_TEMP:
	case EFX_MON_STAT_BOARD_BACK_TEMP:
		*unitp = EFX_MON_STAT_UNIT_TEMP_C;
		break;
	case EFX_MON_STAT_CONTROLLER_COOLING:
	case EFX_MON_STAT_PHY0_COOLING:
	case EFX_MON_STAT_PHY1_COOLING:
	case EFX_MON_STAT_AIRFLOW:
	case EFX_MON_STAT_PHY_POWER_PORT0:
	case EFX_MON_STAT_PHY_POWER_PORT1:
		*unitp = EFX_MON_STAT_UNIT_BOOL;
		break;
	case EFX_MON_STAT_NIC_POWER:
		*unitp = EFX_MON_STAT_UNIT_POWER_W;
		break;
	case EFX_MON_STAT_OUT_IAOE:
	case EFX_MON_STAT_IN_IAOE:
	case EFX_MON_STAT_IN_I0V9:
	case EFX_MON_STAT_IN_I1V2:
	case EFX_MON_STAT_IN_I0V9_A:
	case EFX_MON_STAT_IN_I0V9_B:
	case EFX_MON_STAT_IN_I1V8:
	case EFX_MON_STAT_IN_I2V5:
	case EFX_MON_STAT_IN_I3V3:
	case EFX_MON_STAT_IN_I12V0:
	case EFX_MON_STAT_IN_I1V3:
		*unitp = EFX_MON_STAT_UNIT_CURRENT_MA;
		break;
	case EFX_MON_STAT_FAN_0:
	case EFX_MON_STAT_FAN_1:
	case EFX_MON_STAT_FAN_2:
	case EFX_MON_STAT_FAN_3:
	case EFX_MON_STAT_FAN_4:
		*unitp = EFX_MON_STAT_UNIT_RPM;
		break;
	default:
		*unitp = EFX_MON_STAT_UNIT_UNKNOWN;
		break;
	};

	if (*unitp == EFX_MON_STAT_UNIT_UNKNOWN)
		goto fail1;

	return (B_TRUE);

fail1:
	EFSYS_PROBE1(fail1, boolean_t, B_TRUE);
	return (B_FALSE);
};

/* END MKCONFIG GENERATED MonitorStatisticUnitsBlock */

/* START MKCONFIG GENERATED MonitorStatisticPortsBlock 1719b751d842534f */
	__checkReturn			boolean_t
efx_mon_get_stat_portmap(
	__in				efx_mon_stat_t stat,
	__out				efx_mon_stat_portmask_t *maskp)
{

	switch (stat) {
	case EFX_MON_STAT_PHY1_TEMP:
	case EFX_MON_STAT_PHY1_COOLING:
	case EFX_MON_STAT_PHY_POWER_PORT1:
		*maskp = EFX_MON_STAT_PORTMAP_PORT1;
		break;
	case EFX_MON_STAT_CONTROLLER_TEMP:
	case EFX_MON_STAT_PHY_COMMON_TEMP:
	case EFX_MON_STAT_CONTROLLER_COOLING:
	case EFX_MON_STAT_IN_1V0:
	case EFX_MON_STAT_IN_1V2:
	case EFX_MON_STAT_IN_1V8:
	case EFX_MON_STAT_IN_2V5:
	case EFX_MON_STAT_IN_3V3:
	case EFX_MON_STAT_IN_12V0:
	case EFX_MON_STAT_IN_1V2A:
	case EFX_MON_STAT_IN_VREF:
	case EFX_MON_STAT_OUT_VAOE:
	case EFX_MON_STAT_AOE_TEMP:
	case EFX_MON_STAT_PSU_AOE_TEMP:
	case EFX_MON_STAT_PSU_TEMP:
	case EFX_MON_STAT_FAN_0:
	case EFX_MON_STAT_FAN_1:
	case EFX_MON_STAT_FAN_2:
	case EFX_MON_STAT_FAN_3:
	case EFX_MON_STAT_FAN_4:
	case EFX_MON_STAT_IN_VAOE:
	case EFX_MON_STAT_OUT_IAOE:
	case EFX_MON_STAT_IN_IAOE:
	case EFX_MON_STAT_NIC_POWER:
	case EFX_MON_STAT_IN_0V9:
	case EFX_MON_STAT_IN_I0V9:
	case EFX_MON_STAT_IN_I1V2:
	case EFX_MON_STAT_IN_0V9_ADC:
	case EFX_MON_STAT_CONTROLLER_2_TEMP:
	case EFX_MON_STAT_VREG_INTERNAL_TEMP:
	case EFX_MON_STAT_VREG_0V9_TEMP:
	case EFX_MON_STAT_VREG_1V2_TEMP:
	case EFX_MON_STAT_CONTROLLER_VPTAT:
	case EFX_MON_STAT_CONTROLLER_INTERNAL_TEMP:
	case EFX_MON_STAT_CONTROLLER_VPTAT_EXTADC:
	case EFX_MON_STAT_CONTROLLER_INTERNAL_TEMP_EXTADC:
	case EFX_MON_STAT_AMBIENT_TEMP:
	case EFX_MON_STAT_AIRFLOW:
	case EFX_MON_STAT_VDD08D_VSS08D_CSR:
	case EFX_MON_STAT_VDD08D_VSS08D_CSR_EXTADC:
	case EFX_MON_STAT_HOTPOINT_TEMP:
	case EFX_MON_STAT_MUM_VCC:
	case EFX_MON_STAT_IN_0V9_A:
	case EFX_MON_STAT_IN_I0V9_A:
	case EFX_MON_STAT_VREG_0V9_A_TEMP:
	case EFX_MON_STAT_IN_0V9_B:
	case EFX_MON_STAT_IN_I0V9_B:
	case EFX_MON_STAT_VREG_0V9_B_TEMP:
	case EFX_MON_STAT_CCOM_AVREG_1V2_SUPPLY:
	case EFX_MON_STAT_CCOM_AVREG_1V2_SUPPLY_EXTADC:
	case EFX_MON_STAT_CCOM_AVREG_1V8_SUPPLY:
	case EFX_MON_STAT_CCOM_AVREG_1V8_SUPPLY_EXTADC:
	case EFX_MON_STAT_CONTROLLER_MASTER_VPTAT:
	case EFX_MON_STAT_CONTROLLER_MASTER_INTERNAL_TEMP:
	case EFX_MON_STAT_CONTROLLER_MASTER_VPTAT_EXTADC:
	case EFX_MON_STAT_CONTROLLER_MASTER_INTERNAL_TEMP_EXTADC:
	case EFX_MON_STAT_CONTROLLER_SLAVE_VPTAT:
	case EFX_MON_STAT_CONTROLLER_SLAVE_INTERNAL_TEMP:
	case EFX_MON_STAT_CONTROLLER_SLAVE_VPTAT_EXTADC:
	case EFX_MON_STAT_CONTROLLER_SLAVE_INTERNAL_TEMP_EXTADC:
	case EFX_MON_STAT_SODIMM_VOUT:
	case EFX_MON_STAT_SODIMM_0_TEMP:
	case EFX_MON_STAT_SODIMM_1_TEMP:
	case EFX_MON_STAT_PHY0_VCC:
	case EFX_MON_STAT_PHY1_VCC:
	case EFX_MON_STAT_CONTROLLER_TDIODE_TEMP:
	case EFX_MON_STAT_BOARD_FRONT_TEMP:
	case EFX_MON_STAT_BOARD_BACK_TEMP:
	case EFX_MON_STAT_IN_I1V8:
	case EFX_MON_STAT_IN_I2V5:
	case EFX_MON_STAT_IN_I3V3:
	case EFX_MON_STAT_IN_I12V0:
	case EFX_MON_STAT_IN_1V3:
	case EFX_MON_STAT_IN_I1V3:
		*maskp = EFX_MON_STAT_PORTMAP_ALL;
		break;
	case EFX_MON_STAT_PHY0_TEMP:
	case EFX_MON_STAT_PHY0_COOLING:
	case EFX_MON_STAT_PHY_POWER_PORT0:
		*maskp = EFX_MON_STAT_PORTMAP_PORT0;
		break;
	default:
		*maskp = EFX_MON_STAT_PORTMAP_UNKNOWN;
		break;
	};

	if (*maskp == EFX_MON_STAT_PORTMAP_UNKNOWN)
		goto fail1;

	return (B_TRUE);

fail1:
	EFSYS_PROBE1(fail1, boolean_t, B_TRUE);
	return (B_FALSE);
};

/* END MKCONFIG GENERATED MonitorStatisticPortsBlock */

	__checkReturn			efx_rc_t
efx_mon_stats_update(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__inout_ecount(EFX_MON_NSTATS)	efx_mon_stat_value_t *values)
{
	efx_mon_t *emp = &(enp->en_mon);
	const efx_mon_ops_t *emop = emp->em_emop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_MON);

	return (emop->emo_stats_update(enp, esmp, values));
}

	__checkReturn			efx_rc_t
efx_mon_limits_update(
	__in				efx_nic_t *enp,
	__inout_ecount(EFX_MON_NSTATS)	efx_mon_stat_limits_t *values)
{
	efx_mon_t *emp = &(enp->en_mon);
	const efx_mon_ops_t *emop = emp->em_emop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_MON);

	return (emop->emo_limits_update(enp, values));
}

#endif	/* EFSYS_OPT_MON_STATS */

		void
efx_mon_fini(
	__in	efx_nic_t *enp)
{
	efx_mon_t *emp = &(enp->en_mon);

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_MON);

	emp->em_emop = NULL;

	emp->em_type = EFX_MON_INVALID;

	enp->en_mod_flags &= ~EFX_MOD_MON;
}
