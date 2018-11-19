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
/* Rock-chips rfkill driver for bluetooth
 *
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/rfkill.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/rfkill-bt.h>
#include <linux/rfkill-wlan.h>
#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/suspend.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <dt-bindings/gpio/gpio.h>
#include <uapi/linux/rfkill.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif

#if 0
#define DBG(x...)   printk(KERN_INFO "[BT_RFKILL]: "x)
#else
#define DBG(x...)
#endif

#define LOG(x...)   printk(KERN_INFO "[BT_RFKILL]: "x)

#define BT_WAKEUP_TIMEOUT           10000
#define BT_IRQ_WAKELOCK_TIMEOUT     10*1000

#define BT_BLOCKED     true
#define BT_UNBLOCK     false
#define BT_SLEEP       true
#define BT_WAKEUP      false

enum {
    IOMUX_FNORMAL=0,
    IOMUX_FGPIO,
    IOMUX_FMUX,
};

struct rfkill_rk_data {
	struct rfkill_rk_platform_data	*pdata;
    struct platform_device      *pdev;
	struct rfkill				*rfkill_dev;
    struct wake_lock            bt_irq_wl;
    struct delayed_work         bt_sleep_delay_work;
};

static struct rfkill_rk_data *g_rfkill = NULL;

static const char bt_name[] = 
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
#elif defined(CONFIG_AP6441)
        "ap6441"
#elif defined(CONFIG_AP6335)
        "ap6335"
#elif defined(CONFIG_GB86302I)
        "gb86302i"
#else
        "bt_default"
#endif
;

static irqreturn_t rfkill_rk_wake_host_irq(int irq, void *dev)
{
    struct rfkill_rk_data *rfkill = dev;
    LOG("BT_WAKE_HOST IRQ fired\n");
    
    DBG("BT IRQ wakeup, request %dms wakelock\n", BT_IRQ_WAKELOCK_TIMEOUT);

    wake_lock_timeout(&rfkill->bt_irq_wl, 
                    msecs_to_jiffies(BT_IRQ_WAKELOCK_TIMEOUT));
    
	return IRQ_HANDLED;
}

static int rfkill_rk_setup_gpio(struct platform_device *pdev, struct rfkill_rk_gpio* gpio, 
	const char* prefix, const char* name)
{
	if (gpio_is_valid(gpio->io)) {
        int ret=0;
        sprintf(gpio->name, "%s_%s", prefix, name);
		ret = devm_gpio_request(&pdev->dev, gpio->io, gpio->name);
		if (ret) {
			LOG("Failed to get %s gpio.\n", gpio->name);
			return -1;
		}
	}

    return 0;
}

static int rfkill_rk_setup_wake_irq(struct rfkill_rk_data* rfkill)
{
    int ret=0;
    struct rfkill_rk_irq* irq = &(rfkill->pdata->wake_host_irq);
    
    ret = rfkill_rk_setup_gpio(rfkill->pdev, &irq->gpio, rfkill->pdata->name, "wake_host");
    if (ret) goto fail1;
    if (gpio_is_valid(irq->gpio.io))
    {
        //ret = gpio_pull_updown(irq->gpio.io, (irq->gpio.enable==GPIO_ACTIVE_LOW)?GPIOPullUp:GPIOPullDown);
        //if (ret) goto fail2;
        LOG("Request irq for bt wakeup host\n");
        irq->irq = gpio_to_irq(irq->gpio.io);
        sprintf(irq->name, "%s_irq", irq->gpio.name);
        ret = request_irq(irq->irq,
                    rfkill_rk_wake_host_irq,
                    (irq->gpio.enable==GPIO_ACTIVE_LOW)?IRQF_TRIGGER_FALLING:IRQF_TRIGGER_RISING,
                    irq->name,
                    rfkill);
        if (ret) goto fail2;
        LOG("** disable irq\n");
        disable_irq(irq->irq);
        ret = enable_irq_wake(irq->irq);
        if (ret) goto fail3;
    }

    return ret;

fail3:
    free_irq(irq->gpio.io, rfkill);
fail2:
    gpio_free(irq->gpio.io);
fail1:
    return ret;
}

static inline void rfkill_rk_sleep_bt_internal(struct rfkill_rk_data *rfkill, bool sleep)
{
    struct rfkill_rk_gpio *wake = &rfkill->pdata->wake_gpio;
    
    DBG("*** bt sleep: %d ***\n", sleep);
#ifndef CONFIG_BK3515A_COMBO
    gpio_direction_output(wake->io, sleep?!wake->enable:wake->enable);
#else
    if(!sleep)
    {
        DBG("HOST_UART0_TX pull down 10us\n");
        if (rfkill_rk_setup_gpio(rfkill->pdev, wake, rfkill->pdata->name, "wake") != 0) {
            return;
        }

        gpio_direction_output(wake->io, wake->enable);
        udelay(10);
        gpio_direction_output(wake->io, !wake->enable);

        gpio_free(wake->io);
    }
#endif
}

static void rfkill_rk_delay_sleep_bt(struct work_struct *work)
{
    struct rfkill_rk_data *rfkill = NULL;
    DBG("Enter %s\n",__FUNCTION__);

    rfkill = container_of(work, struct rfkill_rk_data, bt_sleep_delay_work.work);

    rfkill_rk_sleep_bt_internal(rfkill, BT_SLEEP);
}

void rfkill_rk_sleep_bt(bool sleep)
{
    struct rfkill_rk_data *rfkill = g_rfkill;
    struct rfkill_rk_gpio *wake;
    bool ret;
    DBG("Enter %s\n",__FUNCTION__);
    
    if (rfkill==NULL)
    {
        LOG("*** RFKILL is empty???\n");
        return;
    }

    wake = &rfkill->pdata->wake_gpio;
    if (!gpio_is_valid(wake->io))
    {
        DBG("*** Not support bt wakeup and sleep\n");
        return;
    }

    ret = cancel_delayed_work_sync(&rfkill->bt_sleep_delay_work);

    rfkill_rk_sleep_bt_internal(rfkill, sleep);

#ifdef CONFIG_BT_AUTOSLEEP
    if (sleep==BT_WAKEUP)
    {
        schedule_delayed_work(&rfkill->bt_sleep_delay_work, 
                            msecs_to_jiffies(BT_WAKEUP_TIMEOUT));
    }
#endif
}
EXPORT_SYMBOL(rfkill_rk_sleep_bt);

static int bt_power_state = 0;
int rfkill_get_bt_power_state(int *power, bool *toggle)
{
    struct rfkill_rk_data *mrfkill = g_rfkill;

    if (mrfkill == NULL) {
        LOG("%s: rfkill-bt driver has not Successful initialized\n", __func__);
        return -1;
    }

    *toggle = mrfkill->pdata->power_toggle;
    *power = bt_power_state;

    return 0;
}

static int rfkill_rk_set_power(void *data, bool blocked)
{
	struct rfkill_rk_data *rfkill = data;
	struct rfkill_rk_gpio *wake_host = &rfkill->pdata->wake_host_irq.gpio;
    struct rfkill_rk_gpio *poweron = &rfkill->pdata->poweron_gpio;
    struct rfkill_rk_gpio *reset = &rfkill->pdata->reset_gpio;
    struct rfkill_rk_gpio* rts = &rfkill->pdata->rts_gpio;
    struct pinctrl *pinctrl = rfkill->pdata->pinctrl;
    int power = 0, vref_ctrl_enable = 0;
    bool toggle = false;

    DBG("Enter %s\n", __func__);

    DBG("Set blocked:%d\n", blocked);

    toggle = rfkill->pdata->power_toggle;
    if (!rfkill_get_wifi_power_state(&power, &vref_ctrl_enable)) {
        if (true == toggle && 1 == power) {
            LOG("%s: bt shouldn't control the power, it was enabled by wifi!\n", __func__);
            return 0;
        }
    } else {
        LOG("%s: cannot get wifi power state!\n", __func__);
        return -1;
    }

	if (false == blocked) { 

        rfkill_rk_sleep_bt(BT_WAKEUP); // ensure bt is wakeup
	if (gpio_is_valid(wake_host->io)) {
		LOG("%s: set bt wake_host pin output high!\n", __func__);
		gpio_direction_output(wake_host->io, 1);
		msleep(20);
	}

	if (gpio_is_valid(poweron->io) && gpio_is_valid(wake_host->io)) {
		if (gpio_get_value(poweron->io) == !poweron->enable) {
			gpio_direction_output(poweron->io, !poweron->enable);
			msleep(20);
			gpio_direction_output(poweron->io, poweron->enable);
			msleep(20);
			gpio_direction_input(wake_host->io);
			LOG("%s: set bt wake_host pin input!\n", __func__);
		}
        }

	if (gpio_is_valid(reset->io)) {
		if (gpio_get_value(reset->io) == !reset->enable) {
			gpio_direction_output(reset->io, !reset->enable);
			msleep(20);
			gpio_direction_output(reset->io, reset->enable);
		}
        }

        if (pinctrl != NULL && gpio_is_valid(rts->io))
        {
            pinctrl_select_state(pinctrl, rts->gpio_state);
            LOG("ENABLE UART_RTS\n");
            gpio_direction_output(rts->io, rts->enable);
            msleep(100);
            LOG("DISABLE UART_RTS\n");
            gpio_direction_output(rts->io, !rts->enable);
            pinctrl_select_state(pinctrl, rts->default_state);
        }

        bt_power_state = 1;
    	LOG("bt turn on power\n");
	} else {
		if (gpio_is_valid(poweron->io)) {
			if (gpio_get_value(poweron->io) == poweron->enable) {
				gpio_direction_output(poweron->io,
						      !poweron->enable);
				msleep(20);
			}
		}

		bt_power_state = 0;
		LOG("bt shut off power\n");
		if (gpio_is_valid(reset->io)) {
			if (gpio_get_value(reset->io) == reset->enable) {
				gpio_direction_output(reset->io,
						      !reset->enable);
				msleep(20);
			}
		}
	}

	return 0;
}

static int rfkill_rk_pm_prepare(struct device *dev)
{
    struct rfkill_rk_data *rfkill = g_rfkill;
    struct rfkill_rk_gpio* rts;
    struct rfkill_rk_irq*  wake_host_irq;
    struct pinctrl *pinctrl = rfkill->pdata->pinctrl;

    DBG("Enter %s\n",__FUNCTION__);

    if (!rfkill)
        return 0;

    rts = &rfkill->pdata->rts_gpio;
    wake_host_irq = &rfkill->pdata->wake_host_irq;

    //To prevent uart to receive bt data when suspended
    if (pinctrl != NULL && gpio_is_valid(rts->io))
    {
        DBG("Disable UART_RTS\n");
        pinctrl_select_state(pinctrl, rts->gpio_state);
        gpio_direction_output(rts->io, !rts->enable);
    }

#ifdef CONFIG_BT_AUTOSLEEP
    rfkill_rk_sleep_bt(BT_SLEEP);
#endif

    // enable bt wakeup host
    if (gpio_is_valid(wake_host_irq->gpio.io))
    {
        DBG("enable irq for bt wakeup host\n");
        enable_irq(wake_host_irq->irq);
    }

#ifdef CONFIG_RFKILL_RESET
    rfkill_set_states(rfkill->rfkill_dev, BT_BLOCKED, false);
    rfkill_rk_set_power(rfkill, BT_BLOCKED);
#endif

    return 0;
}

static void rfkill_rk_pm_complete(struct device *dev)
{
    struct rfkill_rk_data *rfkill = g_rfkill;
    struct rfkill_rk_irq*  wake_host_irq;
    struct rfkill_rk_gpio* rts;
    struct pinctrl *pinctrl = rfkill->pdata->pinctrl;

    DBG("Enter %s\n",__FUNCTION__);

    if (!rfkill)
        return;

    wake_host_irq = &rfkill->pdata->wake_host_irq;
    rts = &rfkill->pdata->rts_gpio;

    if (gpio_is_valid(wake_host_irq->gpio.io))
    {
        LOG("** disable irq\n");
        disable_irq(wake_host_irq->irq);
    }

    if (pinctrl != NULL && gpio_is_valid(rts->io))
    {
        DBG("Enable UART_RTS\n");
        gpio_direction_output(rts->io, rts->enable);
        pinctrl_select_state(pinctrl, rts->default_state);
    }
}

static const struct rfkill_ops rfkill_rk_ops = {
    .set_block = rfkill_rk_set_power,
};

#define PROC_DIR	"bluetooth/sleep"

static struct proc_dir_entry *bluetooth_dir, *sleep_dir;

static ssize_t bluesleep_read_proc_lpm(struct file *file, char __user *buffer,
				       size_t count, loff_t *data)
{
    return sprintf(buffer, "unsupported to read\n");
}

static ssize_t bluesleep_write_proc_lpm(struct file *file,
					const char __user *buffer,
					size_t count, loff_t *data)
{
    return count;
}

static ssize_t bluesleep_read_proc_btwrite(struct file *file,
					   char __user *buffer,
					   size_t count, loff_t *data)
{
    return sprintf(buffer, "unsupported to read\n");
}

static ssize_t bluesleep_write_proc_btwrite(struct file *file,
					    const char __user *buffer,
					    size_t count, loff_t *data)
{
    char b;

    if (count < 1)
        return -EINVAL;

    if (copy_from_user(&b, buffer, 1))
        return -EFAULT;

    DBG("btwrite %c\n", b);
    /* HCI_DEV_WRITE */
    if (b != '0') {
        rfkill_rk_sleep_bt(BT_WAKEUP);
    }

    return count;
}

#ifdef CONFIG_OF
static int bluetooth_platdata_parse_dt(struct device *dev,
                  struct rfkill_rk_platform_data *data)
{
    struct device_node *node = dev->of_node;
    int gpio;
    enum of_gpio_flags flags;

    if (!node)
        return -ENODEV;

    memset(data, 0, sizeof(*data));

    if (of_find_property(node, "wifi-bt-power-toggle", NULL)) {
        data->power_toggle = true;
        LOG("%s: get property wifi-bt-power-toggle.\n", __func__);
    } else {
        data->power_toggle = false;
    }

    gpio = of_get_named_gpio_flags(node, "uart_rts_gpios", 0, &flags);
    if (gpio_is_valid(gpio)) {
        data->rts_gpio.io = gpio;
        data->rts_gpio.enable = (flags == GPIO_ACTIVE_HIGH)? 1:0;
        LOG("%s: get property: uart_rts_gpios = %d.\n", __func__, gpio);
        data->pinctrl = devm_pinctrl_get(dev);
        if (!IS_ERR(data->pinctrl)) {
            data->rts_gpio.default_state = pinctrl_lookup_state(data->pinctrl, "default");
            data->rts_gpio.gpio_state = pinctrl_lookup_state(data->pinctrl, "rts_gpio");
        } else {
            data->pinctrl = NULL;
            LOG("%s: dts does't define the uart rts iomux.\n", __func__);
            return -EINVAL;
        }
    } else {
        data->pinctrl = NULL;
        data->rts_gpio.io = -EINVAL;
        LOG("%s: uart_rts_gpios is no-in-use.\n", __func__);
    }

    gpio = of_get_named_gpio_flags(node, "BT,power_gpio", 0, &flags);
    if (gpio_is_valid(gpio)){
        data->poweron_gpio.io = gpio;
        data->poweron_gpio.enable = (flags == GPIO_ACTIVE_HIGH)? 1:0;
        LOG("%s: get property: BT,power_gpio = %d.\n", __func__, gpio);
    } else data->poweron_gpio.io = -1;
    gpio = of_get_named_gpio_flags(node, "BT,reset_gpio", 0, &flags);
    if (gpio_is_valid(gpio)){
        data->reset_gpio.io = gpio;
        data->reset_gpio.enable = (flags == GPIO_ACTIVE_HIGH)? 1:0;
        LOG("%s: get property: BT,reset_gpio = %d.\n", __func__, gpio);
    } else data->reset_gpio.io = -1;
    gpio = of_get_named_gpio_flags(node, "BT,wake_gpio", 0, &flags);
    if (gpio_is_valid(gpio)){
        data->wake_gpio.io = gpio;
        data->wake_gpio.enable = (flags == GPIO_ACTIVE_HIGH)? 1:0;
        LOG("%s: get property: BT,wake_gpio = %d.\n", __func__, gpio);
    } else data->wake_gpio.io = -1;
    gpio = of_get_named_gpio_flags(node, "BT,wake_host_irq", 0, &flags);
    if (gpio_is_valid(gpio)) {
        data->wake_host_irq.gpio.io = gpio;
        data->wake_host_irq.gpio.enable = flags;
        LOG("%s: get property: BT,wake_host_irq = %d.\n", __func__, gpio);
    } else data->wake_host_irq.gpio.io = -1;

	data->ext_clk = devm_clk_get(dev, "ext_clock");
	if (IS_ERR(data->ext_clk)) {
		LOG("%s: clk_get failed!!!.\n", __func__);
	} else {
		clk_prepare_enable(data->ext_clk);
	}
	return 0;
}
#endif //CONFIG_OF

static const struct file_operations bluesleep_lpm = {
    .owner = THIS_MODULE,
    .read = bluesleep_read_proc_lpm,
    .write = bluesleep_write_proc_lpm,
};

static const struct file_operations bluesleep_btwrite = {
    .owner = THIS_MODULE,
    .read = bluesleep_read_proc_btwrite,
    .write = bluesleep_write_proc_btwrite,
};

static int rfkill_rk_probe(struct platform_device *pdev)
{
	struct rfkill_rk_data *rfkill;
	struct rfkill_rk_platform_data *pdata = pdev->dev.platform_data;
	int ret = 0;
    struct proc_dir_entry *ent;

    DBG("Enter %s\n", __func__);

    if (!pdata) {
#ifdef CONFIG_OF
        pdata = devm_kzalloc(&pdev->dev, sizeof(struct rfkill_rk_platform_data), GFP_KERNEL);
        if (!pdata)
            return -ENOMEM;

        ret = bluetooth_platdata_parse_dt(&pdev->dev, pdata);
        if (ret < 0) {
#endif
            LOG("%s: No platform data specified\n", __func__);
            return ret;
#ifdef CONFIG_OF
        }
#endif
    }

    pdata->name = (char*)bt_name;
    pdata->type = RFKILL_TYPE_BLUETOOTH;

	rfkill = devm_kzalloc(&pdev->dev, sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		return -ENOMEM;

	rfkill->pdata = pdata;
    rfkill->pdev = pdev;
    g_rfkill = rfkill;

    bluetooth_dir = proc_mkdir("bluetooth", NULL);
    if (bluetooth_dir == NULL) {
        LOG("Unable to create /proc/bluetooth directory");
        return -ENOMEM;
    }

    sleep_dir = proc_mkdir("sleep", bluetooth_dir);
    if (sleep_dir == NULL) {
        LOG("Unable to create /proc/%s directory", PROC_DIR);
        return -ENOMEM;
    }

	/* read/write proc entries */
    ent = proc_create("lpm", 0, sleep_dir, &bluesleep_lpm);
    if (ent == NULL) {
        LOG("Unable to create /proc/%s/lpm entry", PROC_DIR);
        ret = -ENOMEM;
        goto fail_alloc;
    }

    /* read/write proc entries */
    ent = proc_create("btwrite", 0, sleep_dir, &bluesleep_btwrite);
    if (ent == NULL) {
        LOG("Unable to create /proc/%s/btwrite entry", PROC_DIR);
        ret = -ENOMEM;
        goto fail_alloc;
    }

    DBG("init gpio\n");

    ret = rfkill_rk_setup_gpio(pdev, &pdata->poweron_gpio, pdata->name, "poweron");
    if (ret) goto fail_gpio;

    ret = rfkill_rk_setup_gpio(pdev, &pdata->reset_gpio, pdata->name, "reset");
    if (ret) goto fail_gpio;

    ret = rfkill_rk_setup_gpio(pdev, &pdata->wake_gpio, pdata->name, "wake");
    if (ret) goto fail_gpio;

    ret = rfkill_rk_setup_gpio(pdev, &pdata->rts_gpio, rfkill->pdata->name, "rts"); 
    if (ret) goto fail_gpio;

    wake_lock_init(&(rfkill->bt_irq_wl), WAKE_LOCK_SUSPEND, "rfkill_rk_irq_wl");

    ret = rfkill_rk_setup_wake_irq(rfkill);
    if (ret) goto fail_gpio;

    DBG("setup rfkill\n");
	rfkill->rfkill_dev = rfkill_alloc(pdata->name, &pdev->dev, pdata->type,
				&rfkill_rk_ops, rfkill);
	if (!rfkill->rfkill_dev)
		goto fail_alloc;

    rfkill_set_states(rfkill->rfkill_dev, BT_BLOCKED, false);
	ret = rfkill_register(rfkill->rfkill_dev);
	if (ret < 0)
		goto fail_rfkill;

    INIT_DELAYED_WORK(&rfkill->bt_sleep_delay_work, rfkill_rk_delay_sleep_bt);

    //rfkill_rk_set_power(rfkill, BT_BLOCKED);
    // bt turn off power
    if (gpio_is_valid(pdata->poweron_gpio.io))
    {
        gpio_direction_output(pdata->poweron_gpio.io, !pdata->poweron_gpio.enable);
    }
    if (gpio_is_valid(pdata->reset_gpio.io))
    {
        gpio_direction_output(pdata->reset_gpio.io, !pdata->reset_gpio.enable);
    }

	platform_set_drvdata(pdev, rfkill);

    LOG("%s device registered.\n", pdata->name);

	return 0;

fail_rfkill:
	rfkill_destroy(rfkill->rfkill_dev);
fail_alloc:

	remove_proc_entry("btwrite", sleep_dir);
	remove_proc_entry("lpm", sleep_dir);
fail_gpio:

        g_rfkill = NULL;
	return ret;
}

static int rfkill_rk_remove(struct platform_device *pdev)
{
	struct rfkill_rk_data *rfkill = platform_get_drvdata(pdev);

    LOG("Enter %s\n", __func__);

	rfkill_unregister(rfkill->rfkill_dev);
	rfkill_destroy(rfkill->rfkill_dev);

    
    cancel_delayed_work_sync(&rfkill->bt_sleep_delay_work);

    // free gpio
    if (gpio_is_valid(rfkill->pdata->rts_gpio.io))
        gpio_free(rfkill->pdata->rts_gpio.io);
    
    if (gpio_is_valid(rfkill->pdata->wake_host_irq.gpio.io)){
        free_irq(rfkill->pdata->wake_host_irq.irq, rfkill);
#ifndef CONFIG_BK3515A_COMBO
        gpio_free(rfkill->pdata->wake_host_irq.gpio.io);
#endif
    }
    
#ifndef CONFIG_BK3515A_COMBO
    if (gpio_is_valid(rfkill->pdata->wake_gpio.io))
        gpio_free(rfkill->pdata->wake_gpio.io);
#endif
    
    if (gpio_is_valid(rfkill->pdata->reset_gpio.io))
        gpio_free(rfkill->pdata->reset_gpio.io);
    
    if (gpio_is_valid(rfkill->pdata->poweron_gpio.io))
		gpio_free(rfkill->pdata->poweron_gpio.io);
	clk_disable_unprepare(rfkill->pdata->ext_clk);
    g_rfkill = NULL;

	return 0;
}

static const struct dev_pm_ops rfkill_rk_pm_ops = {
	.prepare = rfkill_rk_pm_prepare,
	.complete = rfkill_rk_pm_complete,
};

#ifdef CONFIG_OF
static struct of_device_id bt_platdata_of_match[] = {
    { .compatible = "bluetooth-platdata" },
    { }
};
MODULE_DEVICE_TABLE(of, bt_platdata_of_match);
#endif //CONFIG_OF

static struct platform_driver rfkill_rk_driver = {
	.probe = rfkill_rk_probe,
	.remove = rfkill_rk_remove,
	.driver = {
		.name = "rfkill_bt",
		.owner = THIS_MODULE,
		.pm = &rfkill_rk_pm_ops,
        .of_match_table = of_match_ptr(bt_platdata_of_match),
	},
};

static int __init rfkill_rk_init(void)
{
    LOG("Enter %s\n", __func__);
	return platform_driver_register(&rfkill_rk_driver);
}

static void __exit rfkill_rk_exit(void)
{
    LOG("Enter %s\n", __func__);
	platform_driver_unregister(&rfkill_rk_driver);
}

module_init(rfkill_rk_init);
module_exit(rfkill_rk_exit);

MODULE_DESCRIPTION("rock-chips rfkill for Bluetooth v0.3");
MODULE_AUTHOR("cmy@rock-chips.com, gwl@rock-chips.com");
MODULE_LICENSE("GPL");

