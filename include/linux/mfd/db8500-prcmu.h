/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Kumar Sanghvi <kumar.sanghvi@stericsson.com>
 *
 * PRCMU f/w APIs
 */
#ifndef __MFD_DB8500_PRCMU_H
#define __MFD_DB8500_PRCMU_H

#include <linux/interrupt.h>
#include <linux/bitops.h>

/*
 * Registers
 */
#define DB8500_PRCM_GPIOCR 0x138
#define DB8500_PRCM_GPIOCR_DBG_UARTMOD_CMD0	BIT(0)
#define DB8500_PRCM_GPIOCR_DBG_STM_APE_CMD	BIT(9)
#define DB8500_PRCM_GPIOCR_DBG_STM_MOD_CMD1	BIT(11)
#define DB8500_PRCM_GPIOCR_SPI2_SELECT		BIT(23)

#define DB8500_PRCM_LINE_VALUE 0x170
#define DB8500_PRCM_LINE_VALUE_HSI_CAWAKE0	BIT(3)

#define DB8500_PRCM_DSI_SW_RESET 0x324
#define DB8500_PRCM_DSI_SW_RESET_DSI0_SW_RESETN BIT(0)
#define DB8500_PRCM_DSI_SW_RESET_DSI1_SW_RESETN BIT(1)
#define DB8500_PRCM_DSI_SW_RESET_DSI2_SW_RESETN BIT(2)

/* This portion previously known as <mach/prcmu-fw-defs_v1.h> */

/**
 * enum state - ON/OFF state definition
 * @OFF: State is ON
 * @ON: State is OFF
 *
 */
enum state {
	OFF = 0x0,
	ON  = 0x1,
};

/**
 * enum ret_state - general purpose On/Off/Retention states
 *
 */
enum ret_state {
	OFFST = 0,
	ONST  = 1,
	RETST = 2
};

/**
 * enum clk_arm - ARM Cortex A9 clock schemes
 * @A9_OFF:
 * @A9_BOOT:
 * @A9_OPPT1:
 * @A9_OPPT2:
 * @A9_EXTCLK:
 */
enum clk_arm {
	A9_OFF,
	A9_BOOT,
	A9_OPPT1,
	A9_OPPT2,
	A9_EXTCLK
};

/**
 * enum clk_gen - GEN#0/GEN#1 clock schemes
 * @GEN_OFF:
 * @GEN_BOOT:
 * @GEN_OPPT1:
 */
enum clk_gen {
	GEN_OFF,
	GEN_BOOT,
	GEN_OPPT1,
};

/* some information between arm and xp70 */

/**
 * enum romcode_write - Romcode message written by A9 AND read by XP70
 * @RDY_2_DS: Value set when ApDeepSleep state can be executed by XP70
 * @RDY_2_XP70_RST: Value set when 0x0F has been successfully polled by the
 *                 romcode. The xp70 will go into self-reset
 */
enum romcode_write {
	RDY_2_DS = 0x09,
	RDY_2_XP70_RST = 0x10
};

/**
 * enum romcode_read - Romcode message written by XP70 and read by A9
 * @INIT: Init value when romcode field is not used
 * @FS_2_DS: Value set when power state is going from ApExecute to
 *          ApDeepSleep
 * @END_DS: Value set when ApDeepSleep power state is reached coming from
 *         ApExecute state
 * @DS_TO_FS: Value set when power state is going from ApDeepSleep to
 *           ApExecute
 * @END_FS: Value set when ApExecute power state is reached coming from
 *         ApDeepSleep state
 * @SWR: Value set when power state is going to ApReset
 * @END_SWR: Value set when the xp70 finished executing ApReset actions and
 *          waits for romcode acknowledgment to go to self-reset
 */
enum romcode_read {
	INIT = 0x00,
	FS_2_DS = 0x0A,
	END_DS = 0x0B,
	DS_TO_FS = 0x0C,
	END_FS = 0x0D,
	SWR = 0x0E,
	END_SWR = 0x0F
};

/**
 * enum ap_pwrst - current power states defined in PRCMU firmware
 * @NO_PWRST: Current power state init
 * @AP_BOOT: Current power state is apBoot
 * @AP_EXECUTE: Current power state is apExecute
 * @AP_DEEP_SLEEP: Current power state is apDeepSleep
 * @AP_SLEEP: Current power state is apSleep
 * @AP_IDLE: Current power state is apIdle
 * @AP_RESET: Current power state is apReset
 */
enum ap_pwrst {
	NO_PWRST = 0x00,
	AP_BOOT = 0x01,
	AP_EXECUTE = 0x02,
	AP_DEEP_SLEEP = 0x03,
	AP_SLEEP = 0x04,
	AP_IDLE = 0x05,
	AP_RESET = 0x06
};

/**
 * enum ap_pwrst_trans - Transition states defined in PRCMU firmware
 * @NO_TRANSITION: No power state transition
 * @APEXECUTE_TO_APSLEEP: Power state transition from ApExecute to ApSleep
 * @APIDLE_TO_APSLEEP: Power state transition from ApIdle to ApSleep
 * @APBOOT_TO_APEXECUTE: Power state transition from ApBoot to ApExecute
 * @APEXECUTE_TO_APDEEPSLEEP: Power state transition from ApExecute to
 *                          ApDeepSleep
 * @APEXECUTE_TO_APIDLE: Power state transition from ApExecute to ApIdle
 */
enum ap_pwrst_trans {
	PRCMU_AP_NO_CHANGE		= 0x00,
	APEXECUTE_TO_APSLEEP		= 0x01,
	APIDLE_TO_APSLEEP		= 0x02, /* To be removed */
	PRCMU_AP_SLEEP			= 0x01,
	APBOOT_TO_APEXECUTE		= 0x03,
	APEXECUTE_TO_APDEEPSLEEP	= 0x04, /* To be removed */
	PRCMU_AP_DEEP_SLEEP		= 0x04,
	APEXECUTE_TO_APIDLE		= 0x05, /* To be removed */
	PRCMU_AP_IDLE			= 0x05,
	PRCMU_AP_DEEP_IDLE		= 0x07,
};

/**
 * enum hw_acc_state - State definition for hardware accelerator
 * @HW_NO_CHANGE: The hardware accelerator state must remain unchanged
 * @HW_OFF: The hardware accelerator must be switched off
 * @HW_OFF_RAMRET: The hardware accelerator must be switched off with its
 *               internal RAM in retention
 * @HW_ON: The hwa hardware accelerator hwa must be switched on
 *
 * NOTE! Deprecated, to be removed when all users switched over to use the
 * regulator API.
 */
enum hw_acc_state {
	HW_NO_CHANGE = 0x00,
	HW_OFF = 0x01,
	HW_OFF_RAMRET = 0x02,
	HW_ON = 0x04
};

/**
 * enum  mbox_2_arm_stat - Status messages definition for mbox_arm
 * @BOOT_TO_EXECUTEOK: The apBoot to apExecute state transition has been
 *                    completed
 * @DEEPSLEEPOK: The apExecute to apDeepSleep state transition has been
 *              completed
 * @SLEEPOK: The apExecute to apSleep state transition has been completed
 * @IDLEOK: The apExecute to apIdle state transition has been completed
 * @SOFTRESETOK: The A9 watchdog/ SoftReset state has been completed
 * @SOFTRESETGO : The A9 watchdog/SoftReset state is on going
 * @BOOT_TO_EXECUTE: The apBoot to apExecute state transition is on going
 * @EXECUTE_TO_DEEPSLEEP: The apExecute to apDeepSleep state transition is on
 *                       going
 * @DEEPSLEEP_TO_EXECUTE: The apDeepSleep to apExecute state transition is on
 *                       going
 * @DEEPSLEEP_TO_EXECUTEOK: The apDeepSleep to apExecute state transition has
 *                         been completed
 * @EXECUTE_TO_SLEEP: The apExecute to apSleep state transition is on going
 * @SLEEP_TO_EXECUTE: The apSleep to apExecute state transition is on going
 * @SLEEP_TO_EXECUTEOK: The apSleep to apExecute state transition has been
 *                     completed
 * @EXECUTE_TO_IDLE: The apExecute to apIdle state transition is on going
 * @IDLE_TO_EXECUTE: The apIdle to apExecute state transition is on going
 * @IDLE_TO_EXECUTEOK: The apIdle to apExecute state transition has been
 *                    completed
 * @INIT_STATUS: Status init
 */
enum ap_pwrsttr_status {
	BOOT_TO_EXECUTEOK = 0xFF,
	DEEPSLEEPOK = 0xFE,
	SLEEPOK = 0xFD,
	IDLEOK = 0xFC,
	SOFTRESETOK = 0xFB,
	SOFTRESETGO = 0xFA,
	BOOT_TO_EXECUTE = 0xF9,
	EXECUTE_TO_DEEPSLEEP = 0xF8,
	DEEPSLEEP_TO_EXECUTE = 0xF7,
	DEEPSLEEP_TO_EXECUTEOK = 0xF6,
	EXECUTE_TO_SLEEP = 0xF5,
	SLEEP_TO_EXECUTE = 0xF4,
	SLEEP_TO_EXECUTEOK = 0xF3,
	EXECUTE_TO_IDLE = 0xF2,
	IDLE_TO_EXECUTE = 0xF1,
	IDLE_TO_EXECUTEOK = 0xF0,
	RDYTODS_RETURNTOEXE    = 0xEF,
	NORDYTODS_RETURNTOEXE  = 0xEE,
	EXETOSLEEP_RETURNTOEXE = 0xED,
	EXETOIDLE_RETURNTOEXE  = 0xEC,
	INIT_STATUS = 0xEB,

	/*error messages */
	INITERROR                     = 0x00,
	PLLARMLOCKP_ER                = 0x01,
	PLLDDRLOCKP_ER                = 0x02,
	PLLSOCLOCKP_ER                = 0x03,
	PLLSOCK1LOCKP_ER              = 0x04,
	ARMWFI_ER                     = 0x05,
	SYSCLKOK_ER                   = 0x06,
	I2C_NACK_DATA_ER              = 0x07,
	BOOT_ER                       = 0x08,
	I2C_STATUS_ALWAYS_1           = 0x0A,
	I2C_NACK_REG_ADDR_ER          = 0x0B,
	I2C_NACK_DATA0123_ER          = 0x1B,
	I2C_NACK_ADDR_ER              = 0x1F,
	CURAPPWRSTISNOT_BOOT          = 0x20,
	CURAPPWRSTISNOT_EXECUTE       = 0x21,
	CURAPPWRSTISNOT_SLEEPMODE     = 0x22,
	CURAPPWRSTISNOT_CORRECTFORIT10 = 0x23,
	FIFO4500WUISNOT_WUPEVENT      = 0x24,
	PLL32KLOCKP_ER                = 0x29,
	DDRDEEPSLEEPOK_ER             = 0x2A,
	ROMCODEREADY_ER               = 0x50,
	WUPBEFOREDS                   = 0x51,
	DDRCONFIG_ER                  = 0x52,
	WUPBEFORESLEEP                = 0x53,
	WUPBEFOREIDLE                 = 0x54
};  /* earlier called as  mbox_2_arm_stat */

/**
 * enum dvfs_stat - DVFS status messages definition
 * @DVFS_GO: A state transition DVFS is on going
 * @DVFS_ARM100OPPOK: The state transition DVFS has been completed for 100OPP
 * @DVFS_ARM50OPPOK: The state transition DVFS has been completed for 50OPP
 * @DVFS_ARMEXTCLKOK: The state transition DVFS has been completed for EXTCLK
 * @DVFS_NOCHGTCLKOK: The state transition DVFS has been completed for
 *                   NOCHGCLK
 * @DVFS_INITSTATUS: Value init
 */
enum dvfs_stat {
	DVFS_GO = 0xFF,
	DVFS_ARM100OPPOK = 0xFE,
	DVFS_ARM50OPPOK = 0xFD,
	DVFS_ARMEXTCLKOK = 0xFC,
	DVFS_NOCHGTCLKOK = 0xFB,
	DVFS_INITSTATUS = 0x00
};

/**
 * enum sva_mmdsp_stat - SVA MMDSP status messages
 * @SVA_MMDSP_GO: SVAMMDSP interrupt has happened
 * @SVA_MMDSP_INIT: Status init
 */
enum sva_mmdsp_stat {
	SVA_MMDSP_GO = 0xFF,
	SVA_MMDSP_INIT = 0x00
};

/**
 * enum sia_mmdsp_stat - SIA MMDSP status messages
 * @SIA_MMDSP_GO: SIAMMDSP interrupt has happened
 * @SIA_MMDSP_INIT: Status init
 */
enum sia_mmdsp_stat {
	SIA_MMDSP_GO = 0xFF,
	SIA_MMDSP_INIT = 0x00
};

/**
 * enum  mbox_to_arm_err - Error messages definition
 * @INIT_ERR: Init value
 * @PLLARMLOCKP_ERR: PLLARM has not been correctly locked in given time
 * @PLLDDRLOCKP_ERR: PLLDDR has not been correctly locked in the given time
 * @PLLSOC0LOCKP_ERR: PLLSOC0 has not been correctly locked in the given time
 * @PLLSOC1LOCKP_ERR: PLLSOC1 has not been correctly locked in the given time
 * @ARMWFI_ERR: The ARM WFI has not been correctly executed in the given time
 * @SYSCLKOK_ERR: The SYSCLK is not available in the given time
 * @BOOT_ERR: Romcode has not validated the XP70 self reset in the given time
 * @ROMCODESAVECONTEXT: The Romcode didn.t correctly save it secure context
 * @VARMHIGHSPEEDVALTO_ERR: The ARM high speed supply value transfered
 *          through I2C has not been correctly executed in the given time
 * @VARMHIGHSPEEDACCESS_ERR: The command value of VarmHighSpeedVal transfered
 *             through I2C has not been correctly executed in the given time
 * @VARMLOWSPEEDVALTO_ERR:The ARM low speed supply value transfered through
 *                     I2C has not been correctly executed in the given time
 * @VARMLOWSPEEDACCESS_ERR: The command value of VarmLowSpeedVal transfered
 *             through I2C has not been correctly executed in the given time
 * @VARMRETENTIONVALTO_ERR: The ARM retention supply value transfered through
 *                     I2C has not been correctly executed in the given time
 * @VARMRETENTIONACCESS_ERR: The command value of VarmRetentionVal transfered
 *             through I2C has not been correctly executed in the given time
 * @VAPEHIGHSPEEDVALTO_ERR: The APE highspeed supply value transfered through
 *                     I2C has not been correctly executed in the given time
 * @VSAFEHPVALTO_ERR: The SAFE high power supply value transfered through I2C
 *                         has not been correctly executed in the given time
 * @VMODSEL1VALTO_ERR: The MODEM sel1 supply value transfered through I2C has
 *                             not been correctly executed in the given time
 * @VMODSEL2VALTO_ERR: The MODEM sel2 supply value transfered through I2C has
 *                             not been correctly executed in the given time
 * @VARMOFFACCESS_ERR: The command value of Varm ON/OFF transfered through
 *                     I2C has not been correctly executed in the given time
 * @VAPEOFFACCESS_ERR: The command value of Vape ON/OFF transfered through
 *                     I2C has not been correctly executed in the given time
 * @VARMRETACCES_ERR: The command value of Varm retention ON/OFF transfered
 *             through I2C has not been correctly executed in the given time
 * @CURAPPWRSTISNOTBOOT:Generated when Arm want to do power state transition
 *             ApBoot to ApExecute but the power current state is not Apboot
 * @CURAPPWRSTISNOTEXECUTE: Generated when Arm want to do power state
 *              transition from ApExecute to others power state but the
 *              power current state is not ApExecute
 * @CURAPPWRSTISNOTSLEEPMODE: Generated when wake up events are transmitted
 *             but the power current state is not ApDeepSleep/ApSleep/ApIdle
 * @CURAPPWRSTISNOTCORRECTDBG:  Generated when wake up events are transmitted
 *              but the power current state is not correct
 * @ARMREGU1VALTO_ERR:The ArmRegu1 value transferred through I2C has not
 *                    been correctly executed in the given time
 * @ARMREGU2VALTO_ERR: The ArmRegu2 value transferred through I2C has not
 *                    been correctly executed in the given time
 * @VAPEREGUVALTO_ERR: The VApeRegu value transfered through I2C has not
 *                    been correctly executed in the given time
 * @VSMPS3REGUVALTO_ERR: The VSmps3Regu value transfered through I2C has not
 *                      been correctly executed in the given time
 * @VMODREGUVALTO_ERR: The VModemRegu value transfered through I2C has not
 *                    been correctly executed in the given time
 */
enum mbox_to_arm_err {
	INIT_ERR = 0x00,
	PLLARMLOCKP_ERR = 0x01,
	PLLDDRLOCKP_ERR = 0x02,
	PLLSOC0LOCKP_ERR = 0x03,
	PLLSOC1LOCKP_ERR = 0x04,
	ARMWFI_ERR = 0x05,
	SYSCLKOK_ERR = 0x06,
	BOOT_ERR = 0x07,
	ROMCODESAVECONTEXT = 0x08,
	VARMHIGHSPEEDVALTO_ERR = 0x10,
	VARMHIGHSPEEDACCESS_ERR = 0x11,
	VARMLOWSPEEDVALTO_ERR = 0x12,
	VARMLOWSPEEDACCESS_ERR = 0x13,
	VARMRETENTIONVALTO_ERR = 0x14,
	VARMRETENTIONACCESS_ERR = 0x15,
	VAPEHIGHSPEEDVALTO_ERR = 0x16,
	VSAFEHPVALTO_ERR = 0x17,
	VMODSEL1VALTO_ERR = 0x18,
	VMODSEL2VALTO_ERR = 0x19,
	VARMOFFACCESS_ERR = 0x1A,
	VAPEOFFACCESS_ERR = 0x1B,
	VARMRETACCES_ERR = 0x1C,
	CURAPPWRSTISNOTBOOT = 0x20,
	CURAPPWRSTISNOTEXECUTE = 0x21,
	CURAPPWRSTISNOTSLEEPMODE = 0x22,
	CURAPPWRSTISNOTCORRECTDBG = 0x23,
	ARMREGU1VALTO_ERR = 0x24,
	ARMREGU2VALTO_ERR = 0x25,
	VAPEREGUVALTO_ERR = 0x26,
	VSMPS3REGUVALTO_ERR = 0x27,
	VMODREGUVALTO_ERR = 0x28
};

enum hw_acc {
	SVAMMDSP = 0,
	SVAPIPE = 1,
	SIAMMDSP = 2,
	SIAPIPE = 3,
	SGA = 4,
	B2R2MCDE = 5,
	ESRAM12 = 6,
	ESRAM34 = 7,
};

enum cs_pwrmgt {
	PWRDNCS0  = 0,
	WKUPCS0   = 1,
	PWRDNCS1  = 2,
	WKUPCS1   = 3
};

/* Defs related to autonomous power management */

/**
 * enum sia_sva_pwr_policy - Power policy
 * @NO_CHGT:	No change
 * @DSPOFF_HWPOFF:
 * @DSPOFFRAMRET_HWPOFF:
 * @DSPCLKOFF_HWPOFF:
 * @DSPCLKOFF_HWPCLKOFF:
 *
 */
enum sia_sva_pwr_policy {
	NO_CHGT			= 0x0,
	DSPOFF_HWPOFF		= 0x1,
	DSPOFFRAMRET_HWPOFF	= 0x2,
	DSPCLKOFF_HWPOFF	= 0x3,
	DSPCLKOFF_HWPCLKOFF	= 0x4,
};

/**
 * enum auto_enable - Auto Power enable
 * @AUTO_OFF:
 * @AUTO_ON:
 *
 */
enum auto_enable {
	AUTO_OFF	= 0x0,
	AUTO_ON		= 0x1,
};

/* End of file previously known as prcmu-fw-defs_v1.h */

/**
 * enum prcmu_power_status - results from set_power_state
 * @PRCMU_SLEEP_OK: Sleep went ok
 * @PRCMU_DEEP_SLEEP_OK: DeepSleep went ok
 * @PRCMU_IDLE_OK: Idle went ok
 * @PRCMU_DEEPIDLE_OK: DeepIdle went ok
 * @PRCMU_PRCMU2ARMPENDINGIT_ER: Pending interrupt detected
 * @PRCMU_ARMPENDINGIT_ER: Pending interrupt detected
 *
 */
enum prcmu_power_status {
	PRCMU_SLEEP_OK			= 0xf3,
	PRCMU_DEEP_SLEEP_OK		= 0xf6,
	PRCMU_IDLE_OK			= 0xf0,
	PRCMU_DEEPIDLE_OK		= 0xe3,
	PRCMU_PRCMU2ARMPENDINGIT_ER	= 0x91,
	PRCMU_ARMPENDINGIT_ER		= 0x93,
};

/*
 * Definitions for autonomous power management configuration.
 */

#define PRCMU_AUTO_PM_OFF 0
#define PRCMU_AUTO_PM_ON 1

#define PRCMU_AUTO_PM_POWER_ON_HSEM BIT(0)
#define PRCMU_AUTO_PM_POWER_ON_ABB_FIFO_IT BIT(1)

enum prcmu_auto_pm_policy {
	PRCMU_AUTO_PM_POLICY_NO_CHANGE,
	PRCMU_AUTO_PM_POLICY_DSP_OFF_HWP_OFF,
	PRCMU_AUTO_PM_POLICY_DSP_OFF_RAMRET_HWP_OFF,
	PRCMU_AUTO_PM_POLICY_DSP_CLK_OFF_HWP_OFF,
	PRCMU_AUTO_PM_POLICY_DSP_CLK_OFF_HWP_CLK_OFF,
};

/**
 * struct prcmu_auto_pm_config - Autonomous power management configuration.
 * @sia_auto_pm_enable: SIA autonomous pm enable. (PRCMU_AUTO_PM_{OFF,ON})
 * @sia_power_on:       SIA power ON enable. (PRCMU_AUTO_PM_POWER_ON_* bitmask)
 * @sia_policy:         SIA power policy. (enum prcmu_auto_pm_policy)
 * @sva_auto_pm_enable: SVA autonomous pm enable. (PRCMU_AUTO_PM_{OFF,ON})
 * @sva_power_on:       SVA power ON enable. (PRCMU_AUTO_PM_POWER_ON_* bitmask)
 * @sva_policy:         SVA power policy. (enum prcmu_auto_pm_policy)
 */
struct prcmu_auto_pm_config {
	u8 sia_auto_pm_enable;
	u8 sia_power_on;
	u8 sia_policy;
	u8 sva_auto_pm_enable;
	u8 sva_power_on;
	u8 sva_policy;
};

#define PRCMU_FW_PROJECT_U8500		2
#define PRCMU_FW_PROJECT_U9500		4
#define PRCMU_FW_PROJECT_U8500_C2	7
#define PRCMU_FW_PROJECT_U9500_C2	11
#define PRCMU_FW_PROJECT_U8520		13
#define PRCMU_FW_PROJECT_U8420		14

struct prcmu_fw_version {
	u8 project;
	u8 api_version;
	u8 func_version;
	u8 errata;
};

#ifdef CONFIG_MFD_DB8500_PRCMU

void db8500_prcmu_early_init(void);
int prcmu_set_rc_a2p(enum romcode_write);
enum romcode_read prcmu_get_rc_p2a(void);
enum ap_pwrst prcmu_get_xp70_current_state(void);
bool prcmu_has_arm_maxopp(void);
struct prcmu_fw_version *prcmu_get_fw_version(void);
int prcmu_release_usb_wakeup_state(void);
void prcmu_configure_auto_pm(struct prcmu_auto_pm_config *sleep,
	struct prcmu_auto_pm_config *idle);
bool prcmu_is_auto_pm_enabled(void);

int prcmu_config_clkout(u8 clkout, u8 source, u8 div);
int prcmu_set_clock_divider(u8 clock, u8 divider);
int db8500_prcmu_config_hotdog(u8 threshold);
int db8500_prcmu_config_hotmon(u8 low, u8 high);
int db8500_prcmu_start_temp_sense(u16 cycles32k);
int db8500_prcmu_stop_temp_sense(void);
int prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size);
int prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size);

int prcmu_ac_wake_req(void);
void prcmu_ac_sleep_req(void);
void db8500_prcmu_modem_reset(void);

int db8500_prcmu_config_a9wdog(u8 num, bool sleep_auto_off);
int db8500_prcmu_enable_a9wdog(u8 id);
int db8500_prcmu_disable_a9wdog(u8 id);
int db8500_prcmu_kick_a9wdog(u8 id);
int db8500_prcmu_load_a9wdog(u8 id, u32 val);

void db8500_prcmu_system_reset(u16 reset_code);
int db8500_prcmu_set_power_state(u8 state, bool keep_ulp_clk, bool keep_ap_pll);
u8 db8500_prcmu_get_power_state_result(void);
int db8500_prcmu_gic_decouple(void);
int db8500_prcmu_gic_recouple(void);
int db8500_prcmu_copy_gic_settings(void);
bool db8500_prcmu_gic_pending_irq(void);
bool db8500_prcmu_pending_irq(void);
bool db8500_prcmu_is_cpu_in_wfi(int cpu);
void db8500_prcmu_enable_wakeups(u32 wakeups);
int db8500_prcmu_set_epod(u16 epod_id, u8 epod_state);
int db8500_prcmu_request_clock(u8 clock, bool enable);
int db8500_prcmu_set_display_clocks(void);
int db8500_prcmu_disable_dsipll(void);
int db8500_prcmu_enable_dsipll(void);
void db8500_prcmu_config_abb_event_readout(u32 abb_events);
void db8500_prcmu_get_abb_event_buffer(void __iomem **buf);
int db8500_prcmu_config_esram0_deep_sleep(u8 state);
u16 db8500_prcmu_get_reset_code(void);
bool db8500_prcmu_is_ac_wake_requested(void);
int db8500_prcmu_set_arm_opp(u8 opp);
int db8500_prcmu_get_arm_opp(void);
int db8500_prcmu_set_ape_opp(u8 opp);
int db8500_prcmu_get_ape_opp(void);
int db8500_prcmu_request_ape_opp_100_voltage(bool enable);
int db8500_prcmu_set_ddr_opp(u8 opp);
int db8500_prcmu_get_ddr_opp(void);

u32 db8500_prcmu_read(unsigned int reg);
void db8500_prcmu_write(unsigned int reg, u32 value);
void db8500_prcmu_write_masked(unsigned int reg, u32 mask, u32 value);

#else /* !CONFIG_MFD_DB8500_PRCMU */

static inline void db8500_prcmu_early_init(void) {}

static inline int prcmu_set_rc_a2p(enum romcode_write code)
{
	return 0;
}

static inline enum romcode_read prcmu_get_rc_p2a(void)
{
	return INIT;
}

static inline enum ap_pwrst prcmu_get_xp70_current_state(void)
{
	return AP_EXECUTE;
}

static inline bool prcmu_has_arm_maxopp(void)
{
	return false;
}

static inline struct prcmu_fw_version *prcmu_get_fw_version(void)
{
	return NULL;
}

static inline int db8500_prcmu_set_ape_opp(u8 opp)
{
	return 0;
}

static inline int db8500_prcmu_get_ape_opp(void)
{
	return APE_100_OPP;
}

static inline int db8500_prcmu_request_ape_opp_100_voltage(bool enable)
{
	return 0;
}

static inline int prcmu_release_usb_wakeup_state(void)
{
	return 0;
}

static inline int db8500_prcmu_set_ddr_opp(u8 opp)
{
	return 0;
}

static inline int db8500_prcmu_get_ddr_opp(void)
{
	return DDR_100_OPP;
}

static inline void prcmu_configure_auto_pm(struct prcmu_auto_pm_config *sleep,
	struct prcmu_auto_pm_config *idle)
{
}

static inline bool prcmu_is_auto_pm_enabled(void)
{
	return false;
}

static inline int prcmu_config_clkout(u8 clkout, u8 source, u8 div)
{
	return 0;
}

static inline int prcmu_set_clock_divider(u8 clock, u8 divider)
{
	return 0;
}

static inline int db8500_prcmu_config_hotdog(u8 threshold)
{
	return 0;
}

static inline int db8500_prcmu_config_hotmon(u8 low, u8 high)
{
	return 0;
}

static inline int db8500_prcmu_start_temp_sense(u16 cycles32k)
{
	return 0;
}

static inline int db8500_prcmu_stop_temp_sense(void)
{
	return 0;
}

static inline int prcmu_abb_read(u8 slave, u8 reg, u8 *value, u8 size)
{
	return -ENOSYS;
}

static inline int prcmu_abb_write(u8 slave, u8 reg, u8 *value, u8 size)
{
	return -ENOSYS;
}

static inline int prcmu_ac_wake_req(void)
{
	return 0;
}

static inline void prcmu_ac_sleep_req(void) {}

static inline void db8500_prcmu_modem_reset(void) {}

static inline void db8500_prcmu_system_reset(u16 reset_code) {}

static inline int db8500_prcmu_set_power_state(u8 state, bool keep_ulp_clk,
	bool keep_ap_pll)
{
	return 0;
}

static inline u8 db8500_prcmu_get_power_state_result(void)
{
	return 0;
}

static inline void db8500_prcmu_enable_wakeups(u32 wakeups) {}

static inline int db8500_prcmu_set_epod(u16 epod_id, u8 epod_state)
{
	return 0;
}

static inline int db8500_prcmu_request_clock(u8 clock, bool enable)
{
	return 0;
}

static inline int db8500_prcmu_set_display_clocks(void)
{
	return 0;
}

static inline int db8500_prcmu_disable_dsipll(void)
{
	return 0;
}

static inline int db8500_prcmu_enable_dsipll(void)
{
	return 0;
}

static inline int db8500_prcmu_config_esram0_deep_sleep(u8 state)
{
	return 0;
}

static inline void db8500_prcmu_config_abb_event_readout(u32 abb_events) {}

static inline void db8500_prcmu_get_abb_event_buffer(void __iomem **buf) {}

static inline u16 db8500_prcmu_get_reset_code(void)
{
	return 0;
}

static inline int db8500_prcmu_config_a9wdog(u8 num, bool sleep_auto_off)
{
	return 0;
}

static inline int db8500_prcmu_enable_a9wdog(u8 id)
{
	return 0;
}

static inline int db8500_prcmu_disable_a9wdog(u8 id)
{
	return 0;
}

static inline int db8500_prcmu_kick_a9wdog(u8 id)
{
	return 0;
}

static inline int db8500_prcmu_load_a9wdog(u8 id, u32 val)
{
	return 0;
}

static inline bool db8500_prcmu_is_ac_wake_requested(void)
{
	return 0;
}

static inline int db8500_prcmu_set_arm_opp(u8 opp)
{
	return 0;
}

static inline int db8500_prcmu_get_arm_opp(void)
{
	return 0;
}

static inline u32 db8500_prcmu_read(unsigned int reg)
{
	return 0;
}

static inline void db8500_prcmu_write(unsigned int reg, u32 value) {}

static inline void db8500_prcmu_write_masked(unsigned int reg, u32 mask,
	u32 value) {}

#endif /* !CONFIG_MFD_DB8500_PRCMU */

#endif /* __MFD_DB8500_PRCMU_H */
