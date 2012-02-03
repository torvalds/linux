/*
* Customer code to add GPIO control during WLAN start/stop
* Copyright (C) 1999-2011, Broadcom Corporation
* 
*         Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2 (the "GPL"),
* available at http://www.broadcom.com/licenses/GPLv2.php, with the
* following added to such license:
* 
*      As a special exception, the copyright holders of this software give you
* permission to link this software with independent modules, and to copy and
* distribute the resulting executable under terms of your choice, provided that
* you also meet, for each linked independent module, the terms and conditions of
* the license of that module.  An independent module is a module which is not
* derived from this software.  The special exception does not apply to any
* modifications of the software.
* 
*      Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior written consent.
*
* $Id: dhd_custom_gpio.c,v 1.2.42.1 2010/10/19 00:41:09 Exp $
*/

/*
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/gfp.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>
#include <linux/mmc/core.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/host.h>
#include <linux/irq.h>
#include <asm/ioctl.h>

#include <linux/freezer.h>
#include <linux/completion.h> 
#include <linux/sched.h>
*/
//===================
#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <bcmutils.h>

#include <dngl_stats.h>
#include <dhd.h>

#include <wlioctl.h>
#include <wl_iw.h>

#define WL_ERROR(x) printf x
#define WL_TRACE(x)


#if 1
extern void sunximmc_rescan_card(unsigned id, unsigned insert);
extern int mmc_pm_get_mod_type(void);
extern int mmc_pm_gpio_ctrl(char* name, int level);
extern int mmc_pm_get_io_val(char* name);
#endif

#ifdef CUSTOMER_HW
extern  void bcm_wlan_power_off(int);
extern  void bcm_wlan_power_on(int);
#endif /* CUSTOMER_HW */
#if  defined(CONFIG_MACH_MAHIMAHI)
int wifi_set_carddetect(int on);
int wifi_set_power(int on, unsigned long msec);
int wifi_get_irq_number(unsigned long *irq_flags_ptr);
int wifi_get_mac_addr(unsigned char *buf);
#endif

#if defined(OOB_INTR_ONLY)

#if defined(BCMLXSDMMC)
extern int sdioh_mmc_irq(int irq);
#endif /* (BCMLXSDMMC)  */

#ifdef CUSTOMER_HW3
#include <mach/gpio.h>
#endif

/* Customer specific Host GPIO defintion  */
static int dhd_oob_gpio_num = -1;

module_param(dhd_oob_gpio_num, int, 0644);
MODULE_PARM_DESC(dhd_oob_gpio_num, "DHD oob gpio number");

/* that function will returns :
    1) return :  Host gpio interrupt number per customer platform
    2) irq_flags_ptr : Type of Host interrupt as Level or Edge

    NOTE :
    Customer should check his platform definitions
    and hist Host Interrupt  spec
    to figure out the proper setting for his platform.
    BRCM provides just reference settings as example.

*/
int dhd_customer_oob_irq_map(unsigned long *irq_flags_ptr)
{
	int  host_oob_irq = 0;

#ifdef CONFIG_MACH_MAHIMAHI
	host_oob_irq = wifi_get_irq_number(irq_flags_ptr);

#else /* for NOT  CONFIG_MACH_MAHIMAHI */
#if defined(CUSTOM_OOB_GPIO_NUM)
	if (dhd_oob_gpio_num < 0) {
		dhd_oob_gpio_num = CUSTOM_OOB_GPIO_NUM;
	}
#endif

	if (dhd_oob_gpio_num < 0) {
		WL_ERROR(("%s: ERROR customer specific Host GPIO is NOT defined \n",
			__FUNCTION__));
		return (dhd_oob_gpio_num);
	}

	WL_ERROR(("%s: customer specific Host GPIO number is (%d)\n",
	         __FUNCTION__, dhd_oob_gpio_num));

#if defined CUSTOMER_HW
	host_oob_irq = MSM_GPIO_TO_INT(dhd_oob_gpio_num);
#elif defined CUSTOMER_HW3
	gpio_request(dhd_oob_gpio_num, "oob irq");
	host_oob_irq = gpio_to_irq(dhd_oob_gpio_num);
	gpio_direction_input(dhd_oob_gpio_num);
#endif /* CUSTOMER_HW */
#endif /* CONFIG_MACH_MAHIMAHI */

	return (host_oob_irq);
}
#endif /* defined(OOB_INTR_ONLY) */

/* Customer function to control hw specific wlan gpios */
void
dhd_customer_gpio_wlan_ctrl(int onoff)
{

	switch (onoff) {
		case WLAN_RESET_OFF:
		{
			WL_TRACE(("%s: call customer specific GPIO to insert WLAN RESET\n", __FUNCTION__));
			mmc_pm_gpio_ctrl("bcm40181_shdn", 0);
			mmc_pm_gpio_ctrl("bcm40181_vcc_en", 0);
			mmc_pm_gpio_ctrl("bcm40181_vdd_en", 0);
			printk("[bcm40181]: bcm40181_shdn=>0 !!\n");

#ifdef CUSTOMER_HW
			bcm_wlan_power_off(2);
#endif /* CUSTOMER_HW */

#ifdef CONFIG_MACH_MAHIMAHI
			wifi_set_power(0, 0);
#endif
			WL_ERROR(("=========== WLAN placed in RESET ========\n"));
		}
		break;

		case WLAN_RESET_ON:
		{
			WL_TRACE(("%s: callc customer specific GPIO to remove WLAN RESET\n", __FUNCTION__));
			mmc_pm_gpio_ctrl("bcm40181_vcc_en", 1);
			udelay(100);
			mmc_pm_gpio_ctrl("bcm40181_shdn", 1);
			udelay(50);
			mmc_pm_gpio_ctrl("bcm40181_vdd_en", 1);
			printk("[bcm40181]: bcm40181_shdn=>1 !!\n");

#ifdef CUSTOMER_HW
			bcm_wlan_power_on(2);
#endif /* CUSTOMER_HW */

#ifdef CONFIG_MACH_MAHIMAHI
			wifi_set_power(1, 0);
#endif
			WL_ERROR(("=========== WLAN going back to live  ========\n"));
		}
		break;

		case WLAN_POWER_OFF:
		{
			WL_TRACE(("%s: call customer specific GPIO to turn off WL_REG_ON\n", __FUNCTION__));
			mmc_pm_gpio_ctrl("bcm40181_shdn", 0);
			mmc_pm_gpio_ctrl("bcm40181_vcc_en", 0);
			mmc_pm_gpio_ctrl("bcm40181_vdd_en", 0);
			sunximmc_rescan_card(3, 0);

#ifdef CUSTOMER_HW
			bcm_wlan_power_off(1);
#endif /* CUSTOMER_HW */
		}
		break;

		case WLAN_POWER_ON:
		{
			WL_TRACE(("%s: call customer specific GPIO to turn on WL_REG_ON\n", __FUNCTION__));
			mmc_pm_gpio_ctrl("bcm40181_vcc_en", 1);
			udelay(100);
			mmc_pm_gpio_ctrl("bcm40181_shdn", 1);
			udelay(50);
			mmc_pm_gpio_ctrl("bcm40181_vdd_en", 1);			

#ifdef CUSTOMER_HW
			bcm_wlan_power_on(1);
#endif /* CUSTOMER_HW */
			/* Lets customer power to get stable */
			OSL_DELAY(200);
            sunximmc_rescan_card(3, 1);
		}
		break;
	}
}

#ifdef GET_CUSTOM_MAC_ENABLE
/* Function to get custom MAC address */
int
dhd_custom_get_mac_address(unsigned char *buf)
{
	int ret = 0;

	WL_TRACE(("%s Enter\n", __FUNCTION__));
	if (!buf)
		return -EINVAL;

	/* Customer access to MAC address stored outside of DHD driver */
#if defined(CONFIG_MACH_MAHIMAHI) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	/* Lin - this will call wifi_control_data->get_mac_addr (dhd_linux.c) */
	ret = wifi_get_mac_addr(buf);
#endif

#ifdef EXAMPLE_GET_MAC
	/* EXAMPLE code */
	{
		struct ether_addr ea_example = {{0x00, 0x11, 0x22, 0x33, 0x44, 0xFF}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
	}
#endif /* EXAMPLE_GET_MAC */

	return ret;
}
#endif /* GET_CUSTOM_MAC_ENABLE */
