/*
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/* Rock-chips rfkill driver for wifi
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/rfkill.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/rfkill-wlan.h>
#include <linux/rfkill-bt.h>
#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/suspend.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/rockchip/iomap.h>
#include <dt-bindings/gpio/gpio.h>
#include <linux/skbuff.h>
#include <linux/rockchip/cpu.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif

#if 0
#define DBG(x...)   printk(KERN_INFO "[WLAN_RFKILL]: "x)
#else
#define DBG(x...)
#endif

#define LOG(x...)   printk(KERN_INFO "[WLAN_RFKILL]: "x)

struct rfkill_wlan_data {
	struct rksdmmc_gpio_wifi_moudle *pdata;
    struct wake_lock            wlan_irq_wl;
};

static struct rfkill_wlan_data *g_rfkill = NULL;

static const char wlan_name[] = 
#if defined (CONFIG_BCM4330)
    #if defined (CONFIG_BT_MODULE_NH660)
        "nh660"
    #else
        "bcm4330"
    #endif
#elif defined (CONFIG_RK903)
    #if defined(CONFIG_RKWIFI_26M)
        "rk903_26M"
    #else
        "rk903"
    #endif
#elif defined(CONFIG_BCM4329)
        "bcm4329"
#elif defined(CONFIG_MV8787)
        "mv8787"
#elif defined(CONFIG_AP6210)
    #if defined(CONFIG_RKWIFI_26M)
        "ap6210"
    #else
        "ap6210_24M"
    #endif
#elif defined(CONFIG_AP6330)
		"ap6330"
#elif defined(CONFIG_AP6476)
		"ap6476"
#elif defined(CONFIG_AP6493)
		"ap6493"
#else
        "wlan_default"
#endif
;

/***********************************************************
 * 
 * Broadcom Wifi Static Memory
 * 
 **********************************************************/
#ifdef CONFIG_RKWIFI
#define BCM_STATIC_MEMORY_SUPPORT 1
#else
#define BCM_STATIC_MEMORY_SUPPORT 0
#endif
//===========================
#if BCM_STATIC_MEMORY_SUPPORT
#define PREALLOC_WLAN_SEC_NUM           4
#define PREALLOC_WLAN_BUF_NUM           160
#define PREALLOC_WLAN_SECTION_HEADER    0
#define WLAN_SKB_BUF_NUM        16

#define WLAN_SECTION_SIZE_0     (12 * 1024)
#define WLAN_SECTION_SIZE_1     (12 * 1024)
#define WLAN_SECTION_SIZE_2     (32 * 1024)
#define WLAN_SECTION_SIZE_3     (136* 1024)
#define WLAN_SECTION_SIZE_4     (4  * 1024)
#define WLAN_SECTION_SIZE_5     (64 * 1024)
#define WLAN_SECTION_SIZE_6     (4  * 1024)
#define WLAN_SECTION_SIZE_7     (4  * 1024)

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM+1];

struct wifi_mem_prealloc {
    void *mem_ptr;
    unsigned long size;
};

static struct wifi_mem_prealloc wifi_mem_array[8] = {
    {NULL, (WLAN_SECTION_SIZE_0)},
    {NULL, (WLAN_SECTION_SIZE_1)},
    {NULL, (WLAN_SECTION_SIZE_2)},
    {NULL, (WLAN_SECTION_SIZE_3)},
    {NULL, (WLAN_SECTION_SIZE_4)},
    {NULL, (WLAN_SECTION_SIZE_5)},
    {NULL, (WLAN_SECTION_SIZE_6)},
    {NULL, (WLAN_SECTION_SIZE_7)}
};

static int rockchip_init_wifi_mem(void)
{
    int i;
    int j;

    for (i = 0 ; i < WLAN_SKB_BUF_NUM ; i++) {
        wlan_static_skb[i] = dev_alloc_skb(
               ((i < (WLAN_SKB_BUF_NUM / 2)) ? (PAGE_SIZE*1) : (PAGE_SIZE*2)));

        if (!wlan_static_skb[i])
            goto err_skb_alloc;
    }

    wlan_static_skb[i] = dev_alloc_skb((PAGE_SIZE*4));
    if (!wlan_static_skb[i])
        goto err_skb_alloc;

    for (i = 0 ; i <= 7; i++) {
        wifi_mem_array[i].mem_ptr =
               kmalloc(wifi_mem_array[i].size, GFP_KERNEL);

        if (!wifi_mem_array[i].mem_ptr)
            goto err_mem_alloc;
    }
    return 0;

err_mem_alloc:
    pr_err("Failed to mem_alloc for WLAN\n");
    for (j = 0 ; j < i ; j++)
        kfree(wifi_mem_array[j].mem_ptr);
    i = WLAN_SKB_BUF_NUM;
err_skb_alloc:
    pr_err("Failed to skb_alloc for WLAN\n");
    for (j = 0 ; j < i ; j++)
        dev_kfree_skb(wlan_static_skb[j]);
    dev_kfree_skb(wlan_static_skb[j]);

    return -ENOMEM;
}

void *rockchip_mem_prealloc(int section, unsigned long size)
{
    //printk("rockchip_mem_prealloc: section = %d, size = %d\n", section, size);
    if (section == PREALLOC_WLAN_SEC_NUM)
        return wlan_static_skb;

    if ((section < 0) || (section > 7))
        return NULL;

    if (wifi_mem_array[section].size < size)
        return NULL;

    return wifi_mem_array[section].mem_ptr;
}
#else
void *rockchip_mem_prealloc(int section, unsigned long size) { return NULL;}
#endif
EXPORT_SYMBOL(rockchip_mem_prealloc);

/**************************************************************************
 *
 * get wifi power state Func
 *
 *************************************************************************/
static int wifi_power_state = 0;
int rfkill_get_wifi_power_state(int *power, int *vref_ctrl_enable)
{
    struct rfkill_wlan_data *mrfkill = g_rfkill;

    if (mrfkill == NULL) {
        LOG("%s: rfkill-wlan driver has not Successful initialized\n", __func__);
        return -1;
    }

    if (mrfkill->pdata->vref_ctrl_enble)
        *vref_ctrl_enable = 1;
    *power = wifi_power_state;

    return 0;
}

/**************************************************************************
 *
 * wifi reference voltage control Func
 *
 *************************************************************************/
int rockchip_wifi_ref_voltage(int on)
{
    struct rfkill_wlan_data *mrfkill = g_rfkill;
    struct rksdmmc_gpio *vddio;
    struct regulator *ldo = NULL;
    int power = 0;
    bool toggle = false;

    LOG("%s: %d\n", __func__, on);

    if (mrfkill == NULL) {
        LOG("%s: rfkill-wlan driver has not Successful initialized\n", __func__);
        return -1;
    }

    if (!mrfkill->pdata->vref_ctrl_enble) {
        LOG("%s: wifi io reference voltage control is disabled.\n", __func__);
        return 0;
    }

    if (!rfkill_get_bt_power_state(&power, &toggle)) {
        if (power == 1) {
            LOG("%s: wifi shouldn't control io reference voltage, BT is running!\n", __func__);
            return 0;
        }
    }

    if (mrfkill->pdata->ioregulator.power_ctrl_by_pmu) {
        int ret = -1;
        char *ldostr;
        int level = mrfkill->pdata->ioregulator.enable;
		int voltage = 1000 * mrfkill->pdata->sdio_vol;

        ldostr = mrfkill->pdata->ioregulator.pmu_regulator;
        if (ldostr == NULL) {
            LOG("%s: wifi io reference voltage set to be controled by pmic, but which one?\n", __func__);
            return -1;
        }
        ldo = regulator_get(NULL, ldostr);
        if (ldo == NULL || IS_ERR(ldo)) {
            LOG("\n\n\n%s get ldo error,please mod this\n\n\n", __func__);
            return -1;
        } else {
            if (on == level) {
            	if(cpu_is_rk3036())
            	{
					/*regulator_set_voltage(ldo, voltage, voltage);
					LOG("%s: %s enabled, level = %d\n", __func__, ldostr, voltage);
					ret = regulator_enable(ldo);
					LOG("wifi turn on io reference voltage.\n");*/
            	}else{
					regulator_set_voltage(ldo, voltage, voltage);
					LOG("%s: %s enabled, level = %d\n", __func__, ldostr, voltage);
					ret = regulator_enable(ldo);
					LOG("wifi turn on io reference voltage.\n");
            	}
            } else {
                LOG("%s: %s disabled\n", __func__, ldostr);
                while (regulator_is_enabled(ldo) > 0) {
                    ret = regulator_disable(ldo);
                }
                LOG("wifi shut off io reference voltage.\n");
            }
            regulator_put(ldo);
            msleep(100);
        }
    } else {
        vddio = &mrfkill->pdata->power_n;

        if (on){
            if (gpio_is_valid(vddio->io)) {
                gpio_set_value(vddio->io, vddio->enable);
                msleep(100);
            }

            LOG("wifi turn on io reference voltage.\n");
        }else{
            if (gpio_is_valid(vddio->io)) {
                gpio_set_value(vddio->io, !(vddio->enable));
                msleep(100);
            }

            LOG("wifi shut off io reference voltage.\n");
        }
    }

	return 0;
}

/**************************************************************************
 *
 * Wifi Power Control Func
 * 0 -> power off
 * 1 -> power on
 *
 *************************************************************************/
int rockchip_wifi_power(int on)
{
	struct rfkill_wlan_data *mrfkill = g_rfkill;
    struct rksdmmc_gpio *poweron, *reset;
    struct regulator *ldo = NULL;
    int power = 0;
    bool toggle = false;

    LOG("%s: %d\n", __func__, on);

    if (mrfkill == NULL) {
        LOG("%s: rfkill-wlan driver has not Successful initialized\n", __func__);
        return -1;
    }

    if (!rfkill_get_bt_power_state(&power, &toggle)) {
        if (toggle == true && power == 1) {
            LOG("%s: wifi shouldn't control the power, it was enabled by BT!\n", __func__);
            return 0;
        }
    }
    
    if (on)
        rockchip_wifi_ref_voltage(1);

    if (mrfkill->pdata->mregulator.power_ctrl_by_pmu) {
        int ret = -1;
        char *ldostr;
        int level = mrfkill->pdata->mregulator.enable;

        ldostr = mrfkill->pdata->mregulator.pmu_regulator;
        if (ldostr == NULL) {
            LOG("%s: wifi power set to be controled by pmic, but which one?\n", __func__);
            return -1;
        }
        ldo = regulator_get(NULL, ldostr);
        if (ldo == NULL || IS_ERR(ldo)) {
            LOG("\n\n\n%s get ldo error,please mod this\n\n\n", __func__);
            return -1;
        } else {
			if (on == level) {
				regulator_set_voltage(ldo, 3000000, 3000000);
			    LOG("%s: %s enabled\n", __func__, ldostr);
				ret = regulator_enable(ldo);
                wifi_power_state = 1;
			    LOG("wifi turn on power.\n");
            } else {
				LOG("%s: %s disabled\n", __func__, ldostr);
                while (regulator_is_enabled(ldo) > 0) {
				    ret = regulator_disable(ldo);
                }
                wifi_power_state = 0;
			    LOG("wifi shut off power.\n");
			}
			regulator_put(ldo);
			msleep(100);
		}
    } else {
		poweron = &mrfkill->pdata->power_n;
		reset = &mrfkill->pdata->reset_n;

		if (on){
			if (gpio_is_valid(poweron->io)) {
				gpio_set_value(poweron->io, poweron->enable);
				msleep(100);
			}

			if (gpio_is_valid(reset->io)) {
				gpio_set_value(reset->io, reset->enable);
				msleep(100);
			}

            wifi_power_state = 1;
			LOG("wifi turn on power. %d\n", poweron->io);
		}else{
			if (gpio_is_valid(poweron->io)) {
				gpio_set_value(poweron->io, !(poweron->enable));
				msleep(100);
			}

			if (gpio_is_valid(reset->io)) {
				gpio_set_value(reset->io, !(reset->enable));
			}

            wifi_power_state = 0;
			LOG("wifi shut off power.\n");
		}
    }

    if (!on)
        rockchip_wifi_ref_voltage(0);

    return 0;
}
EXPORT_SYMBOL(rockchip_wifi_power);

/**************************************************************************
 *
 * Wifi Sdio Detect Func
 *
 *************************************************************************/
#include <linux/mmc/host.h>
extern int mmc_host_rescan(struct mmc_host *host, int val);
int rockchip_wifi_set_carddetect(int val)
{
    return mmc_host_rescan(NULL, val);//NULL => SDIO host
}
EXPORT_SYMBOL(rockchip_wifi_set_carddetect);

/**************************************************************************
 *
 * Wifi Get Interrupt irq Func
 *
 *************************************************************************/
int rockchip_wifi_get_oob_irq(void)
{
    struct rfkill_wlan_data *mrfkill = g_rfkill;
    struct rksdmmc_gpio *wifi_int_irq;

    LOG("%s: Enter\n", __func__);

    if (mrfkill == NULL) {
        LOG("%s: rfkill-wlan driver has not Successful initialized\n", __func__);
        return -1;
    }
    
    wifi_int_irq = &mrfkill->pdata->wifi_int_b;
    if (gpio_is_valid(wifi_int_irq->io)) {
        return gpio_to_irq(wifi_int_irq->io);
        //return wifi_int_irq->io;
    } else {
        LOG("%s: wifi OOB pin isn't defined.\n", __func__);
    }
    
    return -1;
}
EXPORT_SYMBOL(rockchip_wifi_get_oob_irq);

/**************************************************************************
 *
 * Wifi Reset Func
 *
 *************************************************************************/
int rockchip_wifi_reset(int on)
{
    return 0;
}
EXPORT_SYMBOL(rockchip_wifi_reset);

/**************************************************************************
 *
 * Wifi MAC custom Func
 *
 *************************************************************************/
#include <linux/etherdevice.h>
u8 wifi_custom_mac_addr[6] = {0,0,0,0,0,0};
extern char GetSNSectorInfo(char * pbuf);
int rockchip_wifi_mac_addr(unsigned char *buf)
{
    return -1;
/*
    char mac_buf[20] = {0};
    LOG("%s: enter.\n", __func__);

    // from vflash
    if(is_zero_ether_addr(wifi_custom_mac_addr)) {
        int i;
        char *tempBuf = kmalloc(512, GFP_KERNEL);
        if(tempBuf) {
            GetSNSectorInfo(tempBuf);
            for (i = 506; i <= 511; i++)
                wifi_custom_mac_addr[i-506] = tempBuf[i];
            kfree(tempBuf);
        } else {
            return -1;
        }
    }

    sprintf(mac_buf,"%02x:%02x:%02x:%02x:%02x:%02x",wifi_custom_mac_addr[0],wifi_custom_mac_addr[1],
    wifi_custom_mac_addr[2],wifi_custom_mac_addr[3],wifi_custom_mac_addr[4],wifi_custom_mac_addr[5]);
    LOG("falsh wifi_custom_mac_addr=[%s]\n", mac_buf);

    if (is_valid_ether_addr(wifi_custom_mac_addr)) {
        if (2 == (wifi_custom_mac_addr[0] & 0x0F)) {
            LOG("This mac address come into conflict with the address of direct, ignored...\n");
            return -1;
        }
    } else {
        LOG("This mac address is not valid, ignored...\n");
        return -1;
    }

#if defined(CONFIG_RKWIFI)
    memcpy(buf, wifi_custom_mac_addr, 6);
#else
    memcpy(buf, mac_buf, strlen(mac_buf));//realtek's wifi use this branch
#endif
    return 0;
*/
}
EXPORT_SYMBOL(rockchip_wifi_mac_addr);

/**************************************************************************
 *
 * wifi get country code func
 *
 *************************************************************************/
struct cntry_locales_custom {
    char iso_abbrev[4];  /* ISO 3166-1 country abbreviation */
    char custom_locale[4];   /* Custom firmware locale */
    int custom_locale_rev;        /* Custom local revisin default -1 */
};

static struct cntry_locales_custom country_cloc;

void *rockchip_wifi_country_code(char *ccode)
{
    struct cntry_locales_custom *mcloc;

    LOG("%s: set country code [%s]\n", __func__, ccode);
    mcloc = &country_cloc;
    memcpy(mcloc->custom_locale, ccode, 4);
    mcloc->custom_locale_rev = 0;

    return mcloc;
}
EXPORT_SYMBOL(rockchip_wifi_country_code);
/**************************************************************************/

static int rockchip_wifi_voltage_select(void)
{
    struct rfkill_wlan_data *mrfkill = g_rfkill;
    int voltage = 0;

    if (mrfkill == NULL) {
        LOG("%s: rfkill-wlan driver has not Successful initialized\n", __func__);
        return -1;
    }
    voltage = mrfkill->pdata->sdio_vol;
    if (voltage > 2700 && voltage < 3500) {
        writel_relaxed(0x00100000, RK_GRF_VIRT+0x380); //3.3
        LOG("%s: wifi & sdio reference voltage: 3.3V\n", __func__);
    } else if (voltage  > 1500 && voltage < 1950) {
        writel_relaxed(0x00100010, RK_GRF_VIRT+0x380); //1.8
        LOG("%s: wifi & sdio reference voltage: 1.8V\n", __func__);
    } else {
        LOG("%s: unsupport wifi & sdio reference voltage!\n", __func__);
        return -1;
    }

    return 0;
}

static int rfkill_rk_setup_gpio(struct rksdmmc_gpio *gpio, const char* prefix, const char* name)
{
    if (gpio_is_valid(gpio->io)) {
        int ret=0;
        sprintf(gpio->name, "%s_%s", prefix, name);
        ret = gpio_request(gpio->io, gpio->name);
        if (ret) {
            LOG("Failed to get %s gpio.\n", gpio->name);
            return -1;
        }
    }

    return 0;
}

#ifdef CONFIG_OF
static int wlan_platdata_parse_dt(struct device *dev,
                  struct rksdmmc_gpio_wifi_moudle *data)
{
    struct device_node *node = dev->of_node;
    const char *strings;
    u32 value;
    int gpio,ret;
    enum of_gpio_flags flags;

    if (!node)
        return -ENODEV;

    memset(data, 0, sizeof(*data));

	if(cpu_is_rk3036()){
		/* ret = of_property_read_u32(node, "sdio_vref", &value);
		if (ret < 0) {
			LOG("%s: Can't get sdio vref.", __func__);
			return -1;
		}
		data->sdio_vol = value;*/
	}else{
		ret = of_property_read_u32(node, "sdio_vref", &value);
		if (ret < 0) {
			LOG("%s: Can't get sdio vref.", __func__);
			return -1;
		}
		data->sdio_vol = value;
	}

    if (of_find_property(node, "vref_ctrl_enable", NULL)) {
        LOG("%s: enable wifi io reference voltage control.\n", __func__);
        data->vref_ctrl_enble = true;
        if (of_find_property(node, "vref_ctrl_gpio", NULL)) {
            gpio = of_get_named_gpio_flags(node, "vref_ctrl_gpio", 0, &flags);
            if (gpio_is_valid(gpio)){
                data->vddio.io = gpio;
                data->vddio.enable = (flags == GPIO_ACTIVE_HIGH)? 1:0;
                data->ioregulator.power_ctrl_by_pmu = false;
                LOG("%s: get property: vref_ctrl_gpio = %d, flags = %d.\n", __func__, gpio, flags);
            } else {
                data->vddio.io = -1;
                data->vref_ctrl_enble = false;
                LOG("%s: vref_ctrl_gpio defined invalid, disable wifi io reference voltage control.\n", __func__);
            }
        } else {
            data->ioregulator.power_ctrl_by_pmu = true;
            ret = of_property_read_string(node, "vref_pmu_regulator", &strings);
            if (ret) {
                LOG("%s: Can not read property: vref_pmu_regulator.\n", __func__);
                data->vref_ctrl_enble = false;
                data->ioregulator.power_ctrl_by_pmu = false;
            } else {
                LOG("%s: wifi io reference voltage controled by pmu(%s).\n", __func__, strings);
                sprintf(data->ioregulator.pmu_regulator, "%s", strings);
            }
            ret = of_property_read_u32(node, "vref_pmu_enable_level", &value);
            if (ret) {
                LOG("%s: Can not read property: vref_pmu_enable_level.\n", __func__);
                data->vref_ctrl_enble = false;
                data->ioregulator.power_ctrl_by_pmu = false;
            } else {
                LOG("%s: wifi io reference voltage controled by pmu(level = %s).\n", __func__, (value == 1)?"HIGH":"LOW");
                data->ioregulator.enable = value;
            }
        }
    } else {
        data->vref_ctrl_enble = false;
        LOG("%s: disable wifi io reference voltage control.\n", __func__);
    }

    if (of_find_property(node, "power_ctrl_by_pmu", NULL)) {
        data->mregulator.power_ctrl_by_pmu = true;
        ret = of_property_read_string(node, "power_pmu_regulator", &strings);
        if (ret) {
            LOG("%s: Can not read property: power_pmu_regulator.\n", __func__);
            data->mregulator.power_ctrl_by_pmu = false;
        } else {
            LOG("%s: wifi power controled by pmu(%s).\n", __func__, strings);
            sprintf(data->mregulator.pmu_regulator, "%s", strings);
        }
        ret = of_property_read_u32(node, "power_pmu_enable_level", &value);
        if (ret) {
            LOG("%s: Can not read property: power_pmu_enable_level.\n", __func__);
            data->mregulator.power_ctrl_by_pmu = false;
        } else {
            LOG("%s: wifi power controled by pmu(level = %s).\n", __func__, (value == 1)?"HIGH":"LOW");
            data->mregulator.enable = value;
        }
	} else {
		data->mregulator.power_ctrl_by_pmu = false;
		LOG("%s: wifi power controled by gpio.\n", __func__);
        gpio = of_get_named_gpio_flags(node, "WIFI,poweren_gpio", 0, &flags);
        if (gpio_is_valid(gpio)){
			data->power_n.io = gpio;
			data->power_n.enable = (flags == GPIO_ACTIVE_HIGH)? 1:0;
			LOG("%s: get property: WIFI,poweren_gpio = %d, flags = %d.\n", __func__, gpio, flags);
        } else data->power_n.io = -1;
        gpio = of_get_named_gpio_flags(node, "WIFI,reset_gpio", 0, &flags);
        if (gpio_is_valid(gpio)){
			data->reset_n.io = gpio;
			data->reset_n.enable = (flags == GPIO_ACTIVE_HIGH)? 1:0;
			LOG("%s: get property: WIFI,reset_gpio = %d, flags = %d.\n", __func__, gpio, flags);
        } else data->reset_n.io = -1;
        gpio = of_get_named_gpio_flags(node, "WIFI,host_wake_irq", 0, &flags);
        if (gpio_is_valid(gpio)){
			data->wifi_int_b.io = gpio;
			data->wifi_int_b.enable = flags;
			LOG("%s: get property: WIFI,host_wake_irq = %d, flags = %d.\n", __func__, gpio, flags);
        } else data->wifi_int_b.io = -1;
	}

    return 0;
}
#endif //CONFIG_OF

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>

static void wlan_early_suspend(struct early_suspend *h)
{
    LOG("%s :enter\n", __func__);

    return;
}

static void wlan_late_resume(struct early_suspend *h)
{
    LOG("%s :enter\n", __func__);

    return;
}

struct early_suspend wlan_early_suspend {
    .level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20;
    .suspend = wlan_early_suspend;
    .resume = wlan_late_resume; 
}
#endif

static int rfkill_wlan_probe(struct platform_device *pdev)
{
	struct rfkill_wlan_data *rfkill;
	struct rksdmmc_gpio_wifi_moudle *pdata = pdev->dev.platform_data;
	int ret = -1;

    LOG("Enter %s\n", __func__);

	if (!pdata) {
#ifdef CONFIG_OF
        pdata = kzalloc(sizeof(struct rksdmmc_gpio_wifi_moudle), GFP_KERNEL);
        if (!pdata)
            return -ENOMEM;

        ret = wlan_platdata_parse_dt(&pdev->dev, pdata);
        if (ret < 0) {
#endif
		    LOG("%s: No platform data specified\n", __func__);
            return ret;
#ifdef CONFIG_OF
        }
#endif
	}

	rfkill = kzalloc(sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
        goto rfkill_alloc_fail;

	rfkill->pdata = pdata;
    g_rfkill = rfkill;

    LOG("%s: init gpio\n", __func__);

    if (!pdata->mregulator.power_ctrl_by_pmu) {
        ret = rfkill_rk_setup_gpio(&pdata->power_n, wlan_name, "wlan_poweren");
        if (ret) goto fail_alloc;

        ret = rfkill_rk_setup_gpio(&pdata->reset_n, wlan_name, "wlan_reset");
        if (ret) goto fail_alloc;
    }

    wake_lock_init(&(rfkill->wlan_irq_wl), WAKE_LOCK_SUSPEND, "rfkill_wlan_wake");

    // Turn off wifi power as default
    if (gpio_is_valid(pdata->power_n.io))
    {
        gpio_direction_output(pdata->power_n.io, !pdata->power_n.enable);
    }

    rockchip_wifi_voltage_select();

#if BCM_STATIC_MEMORY_SUPPORT
    rockchip_init_wifi_mem();
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND)
    register_early_suspend(wlan_early_suspend);
#endif

    LOG("Exit %s\n", __func__);

	return 0;

fail_alloc:
	kfree(rfkill);
rfkill_alloc_fail:
    kfree(pdata);

    g_rfkill = NULL;

	return ret;
}

static int rfkill_wlan_remove(struct platform_device *pdev)
{
	struct rfkill_wlan_data *rfkill = platform_get_drvdata(pdev);

    LOG("Enter %s\n", __func__);

    wake_lock_destroy(&rfkill->wlan_irq_wl);
    
    if (gpio_is_valid(rfkill->pdata->power_n.io))
        gpio_free(rfkill->pdata->power_n.io);
    
    if (gpio_is_valid(rfkill->pdata->reset_n.io))
        gpio_free(rfkill->pdata->reset_n.io);
    
//    if (gpio_is_valid(rfkill->pdata->vddio.io))
//        gpio_free(rfkill->pdata->vddio.io);
//
//    if (gpio_is_valid(rfkill->pdata->bgf_int_b.io))
//        gpio_free(rfkill->pdata->bgf_int_b.io);
//    
//    if (gpio_is_valid(rfkill->pdata->gps_sync.io))
//        gpio_free(rfkill->pdata->gps_sync.io);
//    
//    if (gpio_is_valid(rfkill->pdata->ANTSEL2.io))
//        gpio_free(rfkill->pdata->ANTSEL2.io);
//
//    if (gpio_is_valid(rfkill->pdata->ANTSEL3.io))
//        gpio_free(rfkill->pdata->ANTSEL3.io);
//    
//    if (gpio_is_valid(rfkill->pdata->GPS_LAN.io))
//        gpio_free(rfkill->pdata->GPS_LAN.io);

    kfree(rfkill);
    g_rfkill = NULL;

	return 0;
}

static int rfkill_wlan_suspend(struct platform_device *pdev, pm_message_t state)
{
    LOG("Enter %s\n", __func__);
    return 0;
}

static int rfkill_wlan_resume(struct platform_device *pdev)
{
    LOG("Enter %s\n", __func__);
    return 0;
}

#ifdef CONFIG_OF
static struct of_device_id wlan_platdata_of_match[] = {
    { .compatible = "wlan-platdata" },
    { }
};
MODULE_DEVICE_TABLE(of, wlan_platdata_of_match);
#endif //CONFIG_OF

static struct platform_driver rfkill_wlan_driver = {
	.probe = rfkill_wlan_probe,
	.remove = rfkill_wlan_remove,
    .suspend = rfkill_wlan_suspend,
    .resume = rfkill_wlan_resume,
	.driver = {
		.name = "wlan-platdata",
		.owner = THIS_MODULE,
        .of_match_table = of_match_ptr(wlan_platdata_of_match),
	},
};

static int __init rfkill_wlan_init(void)
{
    LOG("Enter %s\n", __func__);
	return platform_driver_register(&rfkill_wlan_driver);
}

static void __exit rfkill_wlan_exit(void)
{
    LOG("Enter %s\n", __func__);
	platform_driver_unregister(&rfkill_wlan_driver);
}

module_init(rfkill_wlan_init);
module_exit(rfkill_wlan_exit);

MODULE_DESCRIPTION("rock-chips rfkill for wifi v0.1");
MODULE_AUTHOR("gwl@rock-chips.com");
MODULE_LICENSE("GPL");
