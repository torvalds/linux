/*
 * include/linux/power/aw_pm.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : pm.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-27 14:08
* Descript: power manager
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __AW_PM_H__
#define __AW_PM_H__


/**max device number of pmu*/
#define PMU_MAX_DEVS        2
/**start address for function run in sram*/
#define SRAM_FUNC_START     SW_VA_SRAM_BASE

/**
*@name PMU command
*@{
*/
#define AW_PMU_SET          0x10
#define AW_PMU_VALID        0x20
/**
*@}
*/

/*
* define event source for wakeup system when suspended
*/
#define SUSPEND_WAKEUP_SRC_EXINT    (1<<0)  /* external interrupt, pmu event for ex.    */
#define SUSPEND_WAKEUP_SRC_USB      (1<<1)  /* usb connection event */
#define SUSPEND_WAKEUP_SRC_KEY      (1<<2)  /* key event    */
#define SUSPEND_WAKEUP_SRC_IR       (1<<3)  /* ir event */
#define SUSPEND_WAKEUP_SRC_ALARM    (1<<4)  /* alarm event  */
#define SUSPEND_WAKEUP_SRC_TIMEOFF  (1<<5)  /* set time to power off event  */
#define SUSPEND_WAKEUP_SRC_PIO      (1<<6)  /* gpio event  */



/**
*@brief struct of pmu device arg
*/
struct aw_pmu_arg{
    unsigned int  twi_port;     /**<twi port for pmu chip   */
    unsigned char dev_addr;     /**<address of pmu device   */
};

#ifdef CONFIG_ARCH_SUN7I
typedef struct _boot_dram_para_t
{
	unsigned int	dram_baseaddr;
	unsigned int	dram_clk;
	unsigned int	dram_type;
	unsigned int	dram_rank_num;
	unsigned int	dram_chip_density;
	unsigned int	dram_io_width;
	unsigned int	dram_bus_width;
	unsigned int	dram_cas;
	unsigned int	dram_zq;
	unsigned int	dram_odt_en;
	unsigned int 	dram_size;
	unsigned int	dram_tpr0;
	unsigned int	dram_tpr1;
	unsigned int	dram_tpr2;
	unsigned int	dram_tpr3;
	unsigned int	dram_tpr4;
	unsigned int	dram_tpr5;
	unsigned int 	dram_emr1;
	unsigned int	dram_emr2;
	unsigned int	dram_emr3;
}standy_dram_para_t;
#endif

/**
*@brief struct of standby
*/
struct aw_standby_para{
#ifdef CONFIG_ARCH_SUN7I
	unsigned int event_enable;   /**<event type for system wakeup        */
#endif
    unsigned int event;     /**<event type for system wakeup    */
#ifdef CONFIG_ARCH_SUN7I
	unsigned int axp_src;        /**<axp event type for system wakeup    */
	unsigned int axp_enable;     /**<axp event type for system wakeup    */
#endif
    signed int   time_off;  /**<time to power off from now, based on second */
};


/**
*@brief struct of power management info
*/
struct aw_pm_info{
    struct aw_standby_para  standby_para;   /* standby parameter            */
    struct aw_pmu_arg       pmu_arg;        /**<args used by main function  */
#ifdef CONFIG_ARCH_SUN7I
	standy_dram_para_t	dram_para;
#endif
};


#endif /* __AW_PM_H__ */

