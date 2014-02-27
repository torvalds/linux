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
#include <linux/rfkill-rk.h>
#include <linux/rfkill-wlan.h>
#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/suspend.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
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
#define BCM_STATIC_MEMORY_SUPPORT 0
//===========================
#if BCM_STATIC_MEMORY_SUPPORT
#define PREALLOC_WLAN_SEC_NUM           4
#define PREALLOC_WLAN_BUF_NUM           160
#define PREALLOC_WLAN_SECTION_HEADER    24
#define WLAN_SKB_BUF_NUM        16

#define WLAN_SECTION_SIZE_0     (PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_1     (PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_2     (PREALLOC_WLAN_BUF_NUM * 512)
#define WLAN_SECTION_SIZE_3     (PREALLOC_WLAN_BUF_NUM * 1024)
#define WLAN_SECTION_SIZE_5     (PREALLOC_WLAN_BUF_NUM * 512)

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

struct wifi_mem_prealloc {
    void *mem_ptr;
    unsigned long size;
};

static struct wifi_mem_prealloc wifi_mem_array[5] = {
    {NULL, (WLAN_SECTION_SIZE_0 + PREALLOC_WLAN_SECTION_HEADER)},
    {NULL, (WLAN_SECTION_SIZE_1 + PREALLOC_WLAN_SECTION_HEADER)},
    {NULL, (WLAN_SECTION_SIZE_2 + PREALLOC_WLAN_SECTION_HEADER)},
    {NULL, (WLAN_SECTION_SIZE_3 + PREALLOC_WLAN_SECTION_HEADER)},
    {NULL, (WLAN_SECTION_SIZE_5 + PREALLOC_WLAN_SECTION_HEADER)}
};

static int __init rk29sdk_init_wifi_mem(void)
{
    int i;
    int j;

    for (i = 0 ; i < WLAN_SKB_BUF_NUM ; i++) {
        wlan_static_skb[i] = dev_alloc_skb(
               ((i < (WLAN_SKB_BUF_NUM / 2)) ? 4096 : 8192));

        if (!wlan_static_skb[i])
            goto err_skb_alloc;
    }

    for (i = 0 ; i < 5; i++) {
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

    return -ENOMEM;
}

void *rk29sdk_mem_prealloc(int section, unsigned long size)
{
    if (section == PREALLOC_WLAN_SEC_NUM)
        return wlan_static_skb;

    if ((section < 0) || (section > 5))
        return NULL;

    if (section == 5)
        return wifi_mem_array[4].mem_ptr;

    if (wifi_mem_array[section].size < size)
        return NULL;

    return wifi_mem_array[section].mem_ptr;
}
#else
void *rk29sdk_mem_prealloc(int section, unsigned long size) { return NULL;}
#endif
EXPORT_SYMBOL(rk29sdk_mem_prealloc);

/**************************************************************************
 *
 * Wifi Power Control Func
 * 0 -> power off
 * 1 -> power on
 *
 *************************************************************************/
int rk29sdk_wifi_power(int on)
{
	struct rfkill_wlan_data *mrfkill = g_rfkill;
    struct rksdmmc_gpio *poweron, *reset;
    struct regulator *ldo = NULL;

    LOG("%s: %d\n", __func__, on);

    if (mrfkill == NULL) {
        LOG("%s: rfkill-wlan driver has not Successful initialized\n", __func__);
        return -1;
    }

    if (mrfkill->pdata->mregulator.power_ctrl_by_pmu) {
        char *ldostr;
        int ret = -1;
        int level = mrfkill->pdata->mregulator.enable;

        ldostr = mrfkill->pdata->mregulator.pmu_regulator;
        if (ldostr == NULL) {
            LOG("%s: wifi power set to be controled by pmic, but which one?\n", __func__);
            return -1;
        }
        ldo = regulator_get(NULL, ldostr);
        if (ldo == NULL) {
            LOG("\n\n\n%s get ldo error,please mod this\n\n\n", __func__);
        } else {
			if (on == level) {
				regulator_set_voltage(ldo, 3000000, 3000000);
			    LOG("%s: %s enabled\n", __func__, ldostr);
				ret = regulator_enable(ldo);
				if(ret != 0){
				    LOG("%s: faild to enable %s\n", __func__, ldostr);
				}
			    LOG("wifi turn on power.\n");
            } else {
			    LOG("wifi shut off power.\n");
				LOG("%s: %s disabled\n", __func__, ldostr);
				ret = regulator_disable(ldo);
				if(ret != 0){
					LOG("%s: faild to disable %s\n", __func__, ldostr);
				}
			}
			regulator_put(ldo);
			mdelay(100);
		}
    } else {
		poweron = &mrfkill->pdata->power_n;
		reset = &mrfkill->pdata->reset_n;

		if (on){
			if (gpio_is_valid(poweron->io)) {
				gpio_set_value(poweron->io, !(poweron->enable));
			}
			mdelay(100);

			if (gpio_is_valid(reset->io)) {
				gpio_set_value(reset->io, !(reset->enable));
			}
			mdelay(100);
			LOG("wifi turn on power. %d\n", poweron->io);
		}else{
			if (gpio_is_valid(poweron->io)) {
				gpio_set_value(poweron->io, poweron->enable);
			}

			mdelay(100);
			if (gpio_is_valid(reset->io)) {
				gpio_set_value(reset->io, reset->enable);
			}

			LOG("wifi shut off power.\n");
		}
    }

    return 0;
}
EXPORT_SYMBOL(rk29sdk_wifi_power);

/**************************************************************************
 *
 * Wifi Reset Func
 *
 *************************************************************************/
int rk29sdk_wifi_reset(int on)
{
    return 0;
}
EXPORT_SYMBOL(rk29sdk_wifi_reset);

/**************************************************************************
 *
 * Wifi MAC custom Func
 *
 *************************************************************************/
#include <linux/etherdevice.h>
u8 wifi_custom_mac_addr[6] = {0,0,0,0,0,0};
extern char GetSNSectorInfo(char * pbuf);
int rk29sdk_wifi_mac_addr(unsigned char *buf)
{
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
}
EXPORT_SYMBOL(rk29sdk_wifi_mac_addr);

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

void *rk29sdk_wifi_country_code(char *ccode)
{
    struct cntry_locales_custom *mcloc;

    LOG("%s: set country code [%s]\n", __func__, ccode);
    mcloc = &country_cloc;
    memcpy(mcloc->custom_locale, ccode, 4);
    mcloc->custom_locale_rev = 0;

    return mcloc;
}
/**************************************************************************/

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
    struct device_node *childnode, *grandchildnode;
    const char *pin_funcs, *strings;
    u32 value;
    int gpio,ret;
    enum of_gpio_flags flags;

    if (!node)
        return -ENODEV;

    memset(data, 0, sizeof(*data));

    ret = of_property_read_u32(node, "sdio_vref", &value);
    if (ret < 0)
        LOG("%s: Can't get sdio vref.", __func__);

    data->sdio_vol = value;

    childnode = of_get_child_by_name(node, "wlan_ctrl_pmic");
	if (!childnode) {
		LOG("%s: Can not get child => wlan_ctrl_pmic.\n", __func__);
	} else {
		ret = of_property_read_string(childnode, "power_ctrl_by_pmu", &strings);
		if (ret) {
			LOG("%s: Can not read property: power_ctrl_by_pmu.\n", __func__);
			data->mregulator.power_ctrl_by_pmu = false;
		}
		if (0 == strcmp(strings, "true")) {
			data->mregulator.power_ctrl_by_pmu = true;
			ret = of_property_read_string(childnode, "pmu_regulator", &strings);
			if (ret) {
				LOG("%s: Can not read property: pmu_regulator.\n", __func__);
				data->mregulator.power_ctrl_by_pmu = false;
			} else {
				LOG("%s: wifi power controled by pmu(%s).\n", __func__, strings);
                sprintf(data->mregulator.pmu_regulator, "%s", strings);
			}
			ret = of_property_read_u32(childnode, "pmu_enable_level", &value);
			if (ret) {
				LOG("%s: Can not read property: pmu_enable_level.\n", __func__);
				data->mregulator.power_ctrl_by_pmu = false;
			} else {
				LOG("%s: wifi power controled by pmu(level = %s).\n", __func__, (value == 1)?"HIGH":"LOW");
                data->mregulator.enable = value;
			}
		} else {
			data->mregulator.power_ctrl_by_pmu = false;
			LOG("%s: wifi power controled by gpio.\n", __func__);
		}
    }

    childnode = of_get_child_by_name(node, "wlan_ctrl_gpios");
	if (!childnode) {
		LOG("%s: Can not get child => wlan_ctrl_gpios.\n", __func__);
		return -EINVAL;
	}

    for_each_child_of_node(childnode, grandchildnode) {
        ret = of_property_read_string(grandchildnode, "pin-func", &pin_funcs);
		if (ret) {
			LOG("%s: Can not read property: pin-func.\n", __func__);
            continue;
		}
        gpio = of_get_named_gpio_flags(grandchildnode, "gpios", 0, &flags);
        if (!gpio_is_valid(gpio)){
            LOG("%s: Can not read property: %s->gpios.\n", __func__, pin_funcs);
            continue;
        }

        if (0 == strcmp(pin_funcs, "wlan_poweren")) {
            data->power_n.io = gpio;
            data->power_n.enable = flags;
		    LOG("pin_funcs = %s, gpio = %d, flags = %d", pin_funcs, gpio, flags);
        } else if (0 == strcmp(pin_funcs, "wlan_reset")) {
            data->reset_n.io = gpio;
            data->reset_n.enable = flags;
		    LOG("pin_funcs = %s, gpio = %d, flags = %d", pin_funcs, gpio, flags);
        } else if (0 == strcmp(pin_funcs, "wlan_wake_host_irq")) {
            data->wifi_int_b.io = gpio;
            data->wifi_int_b.enable = flags;
		    LOG("pin_funcs = %s, gpio = %d, flags = %d", pin_funcs, gpio, flags);
        }

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

    ret = rfkill_rk_setup_gpio(&pdata->power_n, wlan_name, "wlan_poweren");
    if (ret) goto fail_alloc;

    ret = rfkill_rk_setup_gpio(&pdata->reset_n, wlan_name, "wlan_reset");
    if (ret) goto fail_alloc;
/*
	ret = rfkill_rk_setup_gpio(&pdata->vddio, IOMUX_FGPIO, wlan_name, "wlan-vddio");
	if (ret) goto fail_alloc;

	ret = rfkill_rk_setup_gpio(&pdata->bgf_int_b, IOMUX_FGPIO, wlan_name, "bgf_int_b");
	if (ret) goto fail_alloc;

    ret = rfkill_rk_setup_gpio(&pdata->gps_sync, IOMUX_FGPIO, wlan_name, "gps_sync");
    if (ret) goto fail_alloc;

	ret = rfkill_rk_setup_gpio(&pdata->ANTSEL2, IOMUX_FGPIO, wlan_name, "ANTSEL2");
	if (ret) goto fail_alloc;

	ret = rfkill_rk_setup_gpio(&pdata->ANTSEL3, IOMUX_FGPIO, wlan_name, "ANTSEL3");
	if (ret) goto fail_alloc;

	ret = rfkill_rk_setup_gpio(&pdata->GPS_LAN, IOMUX_FGPIO, wlan_name, "GPS_LAN");
	if (ret) goto fail_alloc;
*/
    wake_lock_init(&(rfkill->wlan_irq_wl), WAKE_LOCK_SUSPEND, "rfkill_wlan_wake");

    // Turn off wifi power as default
    if (gpio_is_valid(pdata->power_n.io))
    {
        gpio_direction_output(pdata->power_n.io, pdata->power_n.enable);
    }

#if BCM_STATIC_MEMORY_SUPPORT
    rk29sdk_init_wifi_mem();
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
static struct of_device_id pwm_backlight_of_match[] = {
    { .compatible = "wlan-platdata" },
    { }
};
MODULE_DEVICE_TABLE(of, pwm_backlight_of_match);
#endif //CONFIG_OF

static struct platform_driver rfkill_wlan_driver = {
	.probe = rfkill_wlan_probe,
	.remove = rfkill_wlan_remove,
    .suspend = rfkill_wlan_suspend,
    .resume = rfkill_wlan_resume,
	.driver = {
		.name = "wlan-platdata",
		.owner = THIS_MODULE,
        .of_match_table = of_match_ptr(pwm_backlight_of_match),
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
