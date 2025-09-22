/* $OpenBSD: acpidev.h,v 1.45 2024/08/06 17:38:56 kettenis Exp $ */
/*
 * Copyright (c) 2005 Marco Peereboom <marco@openbsd.org>
 * Copyright (c) 2005 Thorsten Lockert <tholo@sigmasoft.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __DEV_ACPI_ACPIDEV_H__
#define __DEV_ACPI_ACPIDEV_H__

#include <sys/sensors.h>
#include <sys/rwlock.h>
#include <dev/acpi/acpireg.h>
#include <dev/acpi/smbus.h>

#define DEVNAME(s)  ((s)->sc_dev.dv_xname)

#define ACPIDEV_NOPOLL		0x0000
#define ACPIDEV_POLL		0x0001
#define ACPIDEV_WAKEUP		0x0002

/*
 * _BIF (Battery InFormation)
 * Arguments: none
 * Results  : package _BIF (Battery InFormation)
 * Package {
 * 	// ASCIIZ is ASCII character string terminated with a 0x00.
 * 	Power Unit			//DWORD
 * 	Design Capacity			//DWORD
 * 	Last Full Charge Capacity	//DWORD
 * 	Battery Technology		//DWORD
 * 	Design Voltage			//DWORD
 * 	Design Capacity of Warning	//DWORD
 * 	Design Capacity of Low		//DWORD
 * 	Battery Capacity Granularity 1	//DWORD
 * 	Battery Capacity Granularity 2	//DWORD
 * 	Model Number			//ASCIIZ
 * 	Serial Number			//ASCIIZ
 * 	Battery Type			//ASCIIZ
 * 	OEM Information			//ASCIIZ
 * }
 *
 * _BIX (Battery Information Extended)
 * Arguments: none
 * Results  : package _BIX (Battery Information Extended)
 * Package {
 * 	// ASCIIZ is ASCII character string terminated with a 0x00.
 *	Revision			//Integer
 * 	Power Unit			//DWORD
 * 	Design Capacity			//DWORD
 * 	Last Full Charge Capacity	//DWORD
 * 	Battery Technology		//DWORD
 * 	Design Voltage			//DWORD
 * 	Design Capacity of Warning	//DWORD
 * 	Design Capacity of Low		//DWORD
 * 	Cycle Count			//DWORD
 * 	Measurement Accuracy		//DWORD
 * 	Max Sampling Time		//DWORD
 * 	Min Sampling Time		//DWORD
 * 	Max Averaging Interval		//DWORD
 * 	Min Averaging Interval		//DWORD
 * 	Battery Capacity Granularity 1	//DWORD
 * 	Battery Capacity Granularity 2	//DWORD
 * 	Model Number			//ASCIIZ
 * 	Serial Number			//ASCIIZ
 * 	Battery Type			//ASCIIZ
 * 	OEM Information			//ASCIIZ
 * }
 */
struct acpibat_bix {
	uint8_t		bix_revision;
	uint32_t	bix_power_unit;
#define BIX_POWER_MW		0x00
#define BIX_POWER_MA		0x01
	uint32_t	bix_capacity;
#define BIX_UNKNOWN		0xffffffff
	uint32_t	bix_last_capacity;
	uint32_t	bix_technology;
#define BIX_TECH_PRIMARY	0x00
#define BIX_TECH_SECONDARY	0x01
	uint32_t	bix_voltage;
	uint32_t	bix_warning;
	uint32_t	bix_low;
	uint32_t	bix_cycle_count;
	uint32_t	bix_accuracy;
	uint32_t	bix_max_sample;
	uint32_t	bix_min_sample;
	uint32_t	bix_max_avg;
	uint32_t	bix_min_avg;
	uint32_t	bix_cap_granu1;
	uint32_t	bix_cap_granu2;
	char		bix_model[20];
	char		bix_serial[20];
	char		bix_type[20];
	char		bix_oem[20];
};

/*
 * _OSC Definition for Control Method Battery
 * Arguments: none
 * Results  : DWORD flags
 */
#define CMB_OSC_UUID		"f18fc78b-0f15-4978-b793-53f833a1d35b"
#define   CMB_OSC_GRANULARITY	0x01
#define   CMB_OSC_WAKE_ON_LOW	0x02

/*
 * _BST (Battery STatus)
 * Arguments: none
 * Results  : package _BST (Battery STatus)
 * Package {
 * 	Battery State			//DWORD
 * 	Battery Present Rate		//DWORD
 * 	Battery Remaining Capacity	//DWORD
 * 	Battery Present Voltage		//DWORD
 * }
 *
 * Per the spec section 10.2.2.3
 * Remaining Battery Percentage[%] = (Battery Remaining Capacity [=0 ~ 100] /
 *     Last Full Charged Capacity[=100]) * 100
 *
 * Remaining Battery Life [h] = Battery Remaining Capacity [mAh/mWh] /
 *     Battery Present Rate [=0xFFFFFFFF] = unknown
 */
struct acpibat_bst {
	uint32_t	bst_state;
#define BST_DISCHARGE		0x01
#define BST_CHARGE		0x02
#define BST_CRITICAL		0x04
	uint32_t	bst_rate;
#define BST_UNKNOWN		0xffffffff
	uint32_t	bst_capacity;
	uint32_t	bst_voltage;
};

/*
 * _BTP (Battery Trip Point)
 * Arguments: DWORD level
 * Results  : none
 */
#define BTP_CLEAR_TRIP_POINT	0x00

/*
 * _BTM (Battery TiMe)
 * Arguments: DWORD rate of discharge
 * Results  : DWORD time in seconds or error/unknown
 */
#define BTM_CURRENT_RATE	0x00

#define BTM_RATE_TOO_LARGE	0x00
#define BTM_CRITICAL		0x00
#define BTM_UNKNOWN		0xffffffff

/*
 * _BMD (Battery Maintenance Data)
 * Arguments: none
 * Results  : package _BMD (Battery Maintenance Data)
 * Package {
 * 	Status Flags		//DWORD
 * 	Capability Flags	//DWORD
 * 	Recalibrate Count	//DWORD
 * 	Quick Recalibrate Time	//DWORD
 * 	Slow Recalibrate Time	//DWORD
 * }
 */
struct acpibat_bmd {
	uint32_t	bmd_status;
#define BMD_AML_CALIBRATE_CYCLE	0x01
#define BMD_CHARGING_DISABLED	0x02
#define BMD_DISCHARGE_WHILE_AC	0x04
#define BMD_RECALIBRATE_BAT	0x08
#define BMD_GOTO_STANDBY_SPEED	0x10
	uint32_t	bmd_capability;
#define BMD_CB_AML_CALIBRATION	0x01
#define BMD_CB_DISABLE_CHARGER	0x02
#define BMD_CB_DISCH_WHILE_AC	0x04
#define BMD_CB_AFFECT_ALL_BATT	0x08
#define BMD_CB_FULL_CHRG_FIRST	0x10
	uint32_t	bmd_recalibrate_count;
#define BMD_ONLY_CALIB_IF_ST3	0x00	/* only recal when status bit 3 set */
	uint32_t	bmd_quick_recalibrate_time;
#define BMD_UNKNOWN		0xffffffff
	uint32_t	bmd_slow_recalibrate_time;
};

/*
 * _BMC (Battery Maintenance Control)
 * Arguments: DWORD flags
 * Results  : none
 */
#define BMC_AML_CALIBRATE	0x01
#define BMC_DISABLE_CHARGING	0x02
#define BMC_ALLOW_AC_DISCHARGE	0x04

/* AC device */
/*
 * _PSR (Power Source)
 * Arguments: none
 * Results  : DWORD status
 */
#define PSR_OFFLINE		0x00
#define PSR_ONLINE		0x01

/*
 * _PCL (Power Consumer List)
 * Arguments: none
 * Results  : LIST of Power Class pointers
 */

/* hpet device */
#define	HPET_REG_SIZE		1024

#define	HPET_CAPABILITIES	0x000
#define	HPET_CONFIGURATION	0x010
#define	HPET_INTERRUPT_STATUS	0x020
#define	HPET_MAIN_COUNTER	0x0F0
#define	HPET_TIMER0_CONFIG	0x100
#define	HPET_TIMER0_COMPARE	0x108
#define	HPET_TIMER0_INTERRUPT	0x110
#define	HPET_TIMER1_CONFIG	((0x20 * 1) + HPET_TIMER0_CONFIG)
#define	HPET_TIMER1_COMPARE	((0x20 * 1) + HPET_TIMER0_COMPARE)
#define	HPET_TIMER1_INTERRUPT	((0x20 * 1) + HPET_TIMER0_INTERRUPT)
#define	HPET_TIMER2_CONFIG	((0x20 * 2) + HPET_TIMER0_CONFIG)
#define	HPET_TIMER2_COMPARE	((0x20 * 2) + HPET_TIMER0_COMPARE)
#define	HPET_TIMER2_INTERRUPT	((0x20 * 2) + HPET_TIMER0_INTERRUPT)

/* Max period is 10^8 fs (100 ns) == 0x5F5E100 as per the HPET SDM */
#define HPET_MAX_PERIOD		0x5F5E100

#define STA_PRESENT   (1L << 0)
#define STA_ENABLED   (1L << 1)
#define STA_SHOW_UI   (1L << 2)
#define STA_DEV_OK    (1L << 3)
#define STA_BATTERY   (1L << 4)

/*
 * _PSS (Performance Supported States)
 * Arguments: none
 * Results  : package _PSS (Performance Supported States)
 * Package {
 *	CoreFreq		//DWORD
 *	Power			//DWORD
 *	TransitionLatency	//DWORD
 *	BusMasterLatency	//DWORD
 *	Control			//DWORD
 * 	Status			//DWORD
 * }
 */
struct acpicpu_pss {
	uint32_t	pss_core_freq;
	uint32_t	pss_power;
	uint32_t	pss_trans_latency;
	uint32_t	pss_bus_latency;
	uint32_t	pss_ctrl;
	uint32_t	pss_status;
};

int acpicpu_fetch_pss(struct acpicpu_pss **);
void acpicpu_set_notify(void (*)(struct acpicpu_pss *, int));
/*
 * XXX this is returned in a buffer and is not a "natural" type.
 *
 * GRD (Generic Register Descriptor )
 *
 */
struct acpi_grd {
	uint8_t		grd_descriptor;
	uint16_t	grd_length;
	struct acpi_gas	grd_gas;
} __packed;

/*
 * _PCT (Performance Control )
 * Arguments: none
 * Results  : package _PCT (Performance Control)
 * Package {
 *	Perf_Ctrl_register	//Register
 *	Perf_Status_register	//Register
 * }
 */
struct acpicpu_pct {
	struct acpi_grd	pct_ctrl;
	struct acpi_grd	pct_status;
};

/* softc for fake apm devices */
struct acpiac_softc {
	struct device		sc_dev;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	int			sc_ac_stat;

	struct ksensor		sc_sens[1];
	struct ksensordev	sc_sensdev;
};

struct acpibat_softc {
	struct device		sc_dev;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	struct acpibat_bix	sc_bix;
	int			sc_use_bif;
	struct acpibat_bst	sc_bst;
	volatile int		sc_bat_present;

	struct ksensor		sc_sens[10];
	struct ksensordev	sc_sensdev;
};

TAILQ_HEAD(aml_nodelisth, aml_nodelist);

struct acpidock_softc {
	struct device		sc_dev;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	struct aml_nodelisth	sc_deps_h;
	struct aml_nodelist	*sc_deps;

	struct ksensor		sc_sens;
	struct ksensordev	sc_sensdev;

	int			sc_docked;
	int			sc_sta;

#define ACPIDOCK_STATUS_UNKNOWN		-1
#define ACPIDOCK_STATUS_UNDOCKED	0
#define ACPIDOCK_STATUS_DOCKED		1
};

#define ACPIDOCK_EVENT_INSERT	0
#define ACPIDOCK_EVENT_DEVCHECK 1
#define	ACPIDOCK_EVENT_EJECT	3

#define ACPIEC_MAX_EVENTS	256

struct acpiec_event {
	struct aml_node *event;
};

struct acpiec_softc {
	struct device		sc_dev;

	int			sc_ecbusy;

	/* command/status register */
	bus_size_t		sc_ec_sc;
	bus_space_tag_t		sc_cmd_bt;
	bus_space_handle_t	sc_cmd_bh;

	/* data register */
	bus_size_t		sc_ec_data;
	bus_space_tag_t		sc_data_bt;
	bus_space_handle_t	sc_data_bh;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;
	uint32_t		sc_gpe;
	struct acpiec_event	sc_events[ACPIEC_MAX_EVENTS];
	int			sc_gotsci;
	int			sc_glk;
	int			sc_cantburst;
};

void		acpibtn_disable_psw(void);
void		acpibtn_enable_psw(void);
int		acpibtn_numopenlids(void);

struct acpisbs_battery {
	uint16_t mode;			/* bit flags */
	int	 units;
#define	ACPISBS_UNITS_MW		0
#define	ACPISBS_UNITS_MA		1
	uint16_t at_rate;		/* mAh or mWh */
	uint16_t temperature;		/* 0.1 degK */
	uint16_t voltage;		/* mV */
	uint16_t current;		/* mA */
	uint16_t avg_current;		/* mA */
	uint16_t rel_charge;		/* percent of last_capacity */
	uint16_t abs_charge;		/* percent of design_capacity */
	uint16_t capacity;		/* mAh */
	uint16_t full_capacity;		/* mAh, when fully charged */
	uint16_t run_time;		/* minutes */
	uint16_t avg_empty_time;	/* minutes */
	uint16_t avg_full_time;		/* minutes until full */
	uint16_t charge_current;	/* mA */
	uint16_t charge_voltage;	/* mV */
	uint16_t status;		/* bit flags */
	uint16_t cycle_count;		/* cycles */
	uint16_t design_capacity;	/* mAh */
	uint16_t design_voltage;	/* mV */
	uint16_t spec;			/* formatted */
	uint16_t manufacture_date;	/* formatted */
	uint16_t serial;		/* number */

#define	ACPISBS_VALUE_UNKNOWN		65535

	char	 manufacturer[SMBUS_DATA_SIZE];
	char	 device_name[SMBUS_DATA_SIZE];
	char	 device_chemistry[SMBUS_DATA_SIZE];
	char	 oem_data[SMBUS_DATA_SIZE];
};

struct acpisbs_softc {
	struct device		sc_dev;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;
	struct acpiec_softc     *sc_ec;
	uint8_t			sc_ec_base;

	struct acpisbs_battery	sc_battery;
	int			sc_batteries_present;

	struct ksensor		*sc_sensors;
	struct ksensordev	sc_sensordev;
	struct sensor_task	*sc_sensor_task;
	struct timeval		sc_lastpoll;
};

#endif /* __DEV_ACPI_ACPIDEV_H__ */
