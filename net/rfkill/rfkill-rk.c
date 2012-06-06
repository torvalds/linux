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
 * BT 源码由原来的arch/arm/mach-rkxx/board-xxx-rfkill.c移至此处
 * GPIO的配置在 arch/arm/mach-rkxx/board-rkxx-sdk.c 的rfkill_rk_platdata结构体中
 *
 * 请根据实际情况配置好蓝牙的各个GPIO引脚，引脚配置说明如下:
    .xxxx_gpio   = {
      .io         = -1,      // GPIO值， -1 表示无此功能
      .enable     = -1,      // 使能, GPIO_HIGH - 高使能， GPIO_LOW - 低使能
      .iomux      = {
          .name    = NULL,   // IOMUX名称，NULL 表示它是单功能，不需要mux
          .fgpio   = -1,     // 配置为GPIO时，应设置的值
          .fmux   = -1,      // 配置为复用功能时，应设置的值
      },
    },
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/rfkill.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/gpio.h>
#include <mach/iomux.h>
#include <linux/delay.h>
#include <linux/rfkill-rk.h>
#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/suspend.h>

#if 0
#define DBG(x...)   printk(KERN_INFO "[BT_RFKILL]: "x)
#else
#define DBG(x...)
#endif

#define LOG(x...)   printk(KERN_INFO "[BT_RFKILL]: "x)

#define BT_WAKEUP_TIMEOUT           3000
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

#ifdef CONFIG_ARCH_RK29
    #define rk_mux_api_set(name,mode)      rk29_mux_api_set(name,mode)
#elif defined (CONFIG_ARCH_RK30)
    #define rk_mux_api_set(name,mode)      rk30_mux_api_set(name,mode)
#else
    #define rk_mux_api_set(name,mode)
#endif

// RK29+BCM4329, 其wifi与bt的power控制脚是接在一起的
// 在给bt下电时，需要先判断wifi状态
#if defined(CONFIG_ARCH_RK29) && defined(CONFIG_BCM4329)
#define WIFI_BT_POWER_TOGGLE    1
extern int rk29sdk_bt_power_state;
extern int rk29sdk_wifi_power_state;
#else
#define WIFI_BT_POWER_TOGGLE    0
#endif

struct rfkill_rk_data {
    // 指向board中定义的platform data
	struct rfkill_rk_platform_data	*pdata;

    // 指向rfkill设备，它由rfkill_alloc创建
	struct rfkill				*rfkill_dev;

    // 在IRQ中断函数中使用，确保在BT唤醒AP后，不会立即
    // 调用suspend进入睡眠
    struct wake_lock            bt_irq_wl;
    
    // 在唤醒BT后，通过该delay work延时一段时间后进入睡眠
    // 如果delay work还未执行就又来一次 BT wake，则会先cancel
    // 已有的 delay work，并重新计时。
    struct delayed_work         bt_sleep_delay_work;
};

struct rfkill_rk_data *g_rfkill = NULL;

static const char bt_name[] = 
#if defined(CONFIG_RKWIFI)
    #if defined(CONFIG_RKWIFI_26M)
        "rk903_26M"
    #else
        "rk903"
    #endif
#elif defined(CONFIG_BCM4329)
        "bcm4329"
#elif defined(CONFIG_MV8787)
        "mv8787"
#else
        "bt_default"
#endif
;

/*
 *  rfkill_rk_wake_host_irq - BT_WAKE_HOST 中断回调函数
 *      申请一个wakelock，以确保此次唤醒不会马上结束
 */
static irqreturn_t rfkill_rk_wake_host_irq(int irq, void *dev)
{
    struct rfkill_rk_data *rfkill = dev;
    LOG("BT_WAKE_HOST IRQ fired\n");
    
    DBG("BT IRQ wakeup, request %dms wakelock\n", BT_IRQ_WAKELOCK_TIMEOUT);

    wake_lock_timeout(&rfkill->bt_irq_wl, 
                    msecs_to_jiffies(BT_IRQ_WAKELOCK_TIMEOUT));
    
	return IRQ_HANDLED;
}

/*
 * rfkill_rk_setup_gpio - 设置GPIO
 *      gpio    - 要设置的GPIO
 *      mux     - iomux 什么功能
 *      prefix,name - 组成该IO的名称
 * 返回值
 *      返回值与 gpio_request 相同
 */
static int rfkill_rk_setup_gpio(struct rfkill_rk_gpio* gpio, int mux, const char* prefix, const char* name)
{
	if (gpio_is_valid(gpio->io)) {
        int ret=0;
        sprintf(gpio->name, "%s_%s", prefix, name);
		ret = gpio_request(gpio->io, gpio->name);
		if (ret) {
			LOG("Failed to get %s gpio.\n", gpio->name);
			return -1;
		}
        if (gpio->iomux.name)
        {
            if (mux==1)
                rk_mux_api_set(gpio->iomux.name, gpio->iomux.fgpio);
            else if (mux==2)
                rk_mux_api_set(gpio->iomux.name, gpio->iomux.fmux);
            else
                ;// do nothing
        }
	}

    return 0;
}

// 设置 BT_WAKE_HOST IRQ
static int rfkill_rk_setup_wake_irq(struct rfkill_rk_data* rfkill)
{
    int ret=0;
    struct rfkill_rk_irq* irq = &(rfkill->pdata->wake_host_irq);
    
    ret = rfkill_rk_setup_gpio(&irq->gpio, IOMUX_FGPIO, rfkill->pdata->name, "wake_host");
    if (ret) goto fail1;

    if (gpio_is_valid(irq->gpio.io))
    {
        ret = gpio_pull_updown(irq->gpio.io, irq->is_falling?GPIOPullUp:GPIOPullDown);
        if (ret) goto fail2;
        DBG("Request irq for bt wakeup host\n");
        irq->irq = gpio_to_irq(irq->gpio.io);
        sprintf(irq->name, "%s_irq", irq->gpio.name);
        ret = request_irq(irq->irq,
                    rfkill_rk_wake_host_irq,
                    irq->is_falling?IRQF_TRIGGER_FALLING:IRQF_TRIGGER_RISING,
                    irq->name,
                    rfkill);
        if (ret) goto fail2;
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
    gpio_direction_output(wake->io, sleep?!wake->enable:wake->enable);
}

static void rfkill_rk_delay_sleep_bt(struct work_struct *work)
{
    struct rfkill_rk_data *rfkill = NULL;
    DBG("Enter %s\n",__FUNCTION__);

    rfkill = container_of(work, struct rfkill_rk_data, bt_sleep_delay_work.work);

    rfkill_rk_sleep_bt_internal(rfkill, BT_SLEEP);
}

/*
 *  rfkill_rk_sleep_bt - Sleep or Wakeup BT
 *      1 在给BT上电时候，调用该函数唤醒BT
 *      2 在suspend驱动时候，调用该函数睡眠BT
 *      3 在HCI驱动中，当有命令发送给BT时，调用该函数唤醒BT
 *
 *  对蓝牙的指令发送是发生在蓝牙上电之后，因此1与3不会同时调用该函数
 *  蓝牙指令发送由用户层发起，2与3不会同时调用该函数
 */
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

    // 取消delay work，如果work处于pendding，则立即取消
    // 如果work处于running，则等待直到该work结束
    ret = cancel_delayed_work_sync(&rfkill->bt_sleep_delay_work);

    rfkill_rk_sleep_bt_internal(rfkill, sleep);

    if (sleep==BT_WAKEUP)
    {
        // 重新设置delay work
        schedule_delayed_work(&rfkill->bt_sleep_delay_work, 
                            msecs_to_jiffies(BT_WAKEUP_TIMEOUT));
    }
}
EXPORT_SYMBOL(rfkill_rk_sleep_bt);

static int rfkill_rk_set_power(void *data, bool blocked)
{
	struct rfkill_rk_data *rfkill = data;
    struct rfkill_rk_gpio *poweron = &rfkill->pdata->poweron_gpio;
    struct rfkill_rk_gpio *reset = &rfkill->pdata->reset_gpio;

    DBG("Enter %s\n", __func__);

    DBG("Set blocked:%d\n", blocked);

	if (false == blocked) { 
        rfkill_rk_sleep_bt(BT_WAKEUP); // ensure bt is wakeup
        
		if (gpio_is_valid(poweron->io))
        {
			gpio_direction_output(poweron->io, poweron->enable);
            msleep(20);
        }
		if (gpio_is_valid(reset->io))
        {
			gpio_direction_output(reset->io, reset->enable);
            msleep(20);
			gpio_direction_output(reset->io, !reset->enable);
            msleep(20);
        }

    	LOG("bt turn on power\n");
	} else {
#if WIFI_BT_POWER_TOGGLE
		if (!rk29sdk_wifi_power_state) {
#endif
            if (gpio_is_valid(poweron->io))
            {      
                gpio_direction_output(poweron->io, !poweron->enable);
                msleep(20);
            }

    		LOG("bt shut off power\n");
#if WIFI_BT_POWER_TOGGLE
		}else {
			LOG("bt shouldn't shut off power, wifi is using it!\n");
		}
#endif
		if (gpio_is_valid(reset->io))
        {      
			gpio_direction_output(reset->io, reset->enable);/* bt reset active*/
            msleep(20);
        }
	}

#if WIFI_BT_POWER_TOGGLE
	rk29sdk_bt_power_state = !blocked;
#endif
	return 0;
}

static int rfkill_rk_suspend(struct platform_device *pdev, pm_message_t state)
{
    struct rfkill_rk_data *rfkill = platform_get_drvdata(pdev);
    struct rfkill_rk_gpio* rts = &rfkill->pdata->rts_gpio;
    struct rfkill_rk_irq*  wake_host_irq = &rfkill->pdata->wake_host_irq;
    DBG("Enter %s\n",__FUNCTION__);

    //To prevent uart to receive bt data when suspended
    if (gpio_is_valid(rts->io))
    {
        DBG("Disable UART_RTS\n");
        if (rts->iomux.name)
        {
            rk_mux_api_set(rts->iomux.name, rts->iomux.fgpio);
        }
        gpio_direction_output(rts->io, !rts->enable);
    }

#ifdef CONFIG_BT_AUTOSLEEP
    // BT进入睡眠状态，不接收主控数据
    rfkill_rk_sleep_bt(BT_SLEEP);
#endif

    /* 至此，蓝牙不再送数据到UART，也不再接收主控的UART数据
     * 接着调用enable_irq使能 bt_wake_host irq，当远端设备有数据
     * 到来时，将通过该IRQ唤醒主控
     */

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

static int rfkill_rk_resume(struct platform_device *pdev)
{
    struct rfkill_rk_data *rfkill = platform_get_drvdata(pdev);
    struct rfkill_rk_irq*  wake_host_irq = NULL;
    struct rfkill_rk_gpio* rts = NULL;
    DBG("Enter %s\n",__FUNCTION__);

    wake_host_irq = &rfkill->pdata->wake_host_irq;
    rts = &rfkill->pdata->rts_gpio;

    if (gpio_is_valid(wake_host_irq->gpio.io))
    {
        // 禁用掉 BT_WAKE_HOST IRQ，确保在系统唤醒后不会因BT的操作
        // 而多次触发该中断
        DBG("** disable bt wakeup host\n");
        disable_irq(wake_host_irq->irq);
    }

    /* 使用UART_RTS，此时蓝牙如果有数据就会送到UART
     * 上层分析送来的数据并做出相应的动作，比如: 送来的是
     * 配对请求，则上层将会亮屏并弹出配对界面
     */
    if (gpio_is_valid(rts->io))
    {
        DBG("Enable UART_RTS\n");
        gpio_direction_output(rts->io, rts->enable);
        if (rts->iomux.name)
        {
            rk_mux_api_set(rts->iomux.name, rts->iomux.fmux);
        }
    }

    return 0;
}

static const struct rfkill_ops rfkill_rk_ops = {
    .set_block = rfkill_rk_set_power,
};

static int rfkill_rk_probe(struct platform_device *pdev)
{
	struct rfkill_rk_data *rfkill;
	struct rfkill_rk_platform_data *pdata = pdev->dev.platform_data;
	int ret = 0;

    DBG("Enter %s\n", __func__);

	if (!pdata) {
		LOG("%s: No platform data specified\n", __func__);
		return -EINVAL;
	}

    pdata->name = (char*)bt_name;

	rfkill = kzalloc(sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		return -ENOMEM;

	rfkill->pdata = pdata;
    g_rfkill = rfkill;

    // 申请GPIO以及IRQ
    DBG("init gpio\n");
    // 对于RK29 BCM4329，它的poweron io与wifi共用，在boad文件中已经request
    // 此处不用去申请
#if !WIFI_BT_POWER_TOGGLE
    ret = rfkill_rk_setup_gpio(&pdata->poweron_gpio, IOMUX_FGPIO, pdata->name, "poweron");
    if (ret) goto fail_alloc;
#endif

    ret = rfkill_rk_setup_gpio(&pdata->reset_gpio, IOMUX_FGPIO, pdata->name, "reset");
    if (ret) goto fail_poweron;

    ret = rfkill_rk_setup_gpio(&pdata->wake_gpio, IOMUX_FGPIO, pdata->name, "wake");
    if (ret) goto fail_reset;

    ret = rfkill_rk_setup_wake_irq(rfkill);
    if (ret) goto fail_wake;

    ret = rfkill_rk_setup_gpio(&(pdata->rts_gpio), IOMUX_FNORMAL, rfkill->pdata->name, "rts");
    if (ret) goto fail_wake_host_irq;

    // 创建并注册RFKILL设备
    DBG("setup rfkill\n");
	rfkill->rfkill_dev = rfkill_alloc(pdata->name, &pdev->dev, pdata->type,
				&rfkill_rk_ops, rfkill);
	if (!rfkill->rfkill_dev)
		goto fail_rts;

    // cmy: 设置rfkill初始状态为blocked，在注册时不会调用 set_blocked函数
    rfkill_set_states(rfkill->rfkill_dev, BT_BLOCKED, false);
	ret = rfkill_register(rfkill->rfkill_dev);
	if (ret < 0)
		goto fail_rfkill;

    wake_lock_init(&(rfkill->bt_irq_wl), WAKE_LOCK_SUSPEND, "rfkill_rk_irq_wl");
    INIT_DELAYED_WORK(&rfkill->bt_sleep_delay_work, rfkill_rk_delay_sleep_bt);

    // cmy: 设置蓝牙电源的状态为 blocked
    rfkill_rk_set_power(rfkill, BT_BLOCKED);

	platform_set_drvdata(pdev, rfkill);

    LOG("%s device registered.\n", pdata->name);

	return 0;

fail_rfkill:
	rfkill_destroy(rfkill->rfkill_dev);
fail_rts:
    if (gpio_is_valid(pdata->rts_gpio.io))
        gpio_free(pdata->rts_gpio.io);
fail_wake_host_irq:
    if (gpio_is_valid(pdata->wake_host_irq.gpio.io)){
        free_irq(pdata->wake_host_irq.irq, rfkill);
        gpio_free(pdata->wake_host_irq.gpio.io);
    }
fail_wake:
    if (gpio_is_valid(pdata->wake_gpio.io))
        gpio_free(pdata->wake_gpio.io);
fail_reset:
	if (gpio_is_valid(pdata->reset_gpio.io))
		gpio_free(pdata->reset_gpio.io);
fail_poweron:
#if !WIFI_BT_POWER_TOGGLE
    if (gpio_is_valid(pdata->poweron_gpio.io))
        gpio_free(pdata->poweron_gpio.io);
#endif
fail_alloc:
	kfree(rfkill);
    g_rfkill = NULL;

	return ret;
}

static int rfkill_rk_remove(struct platform_device *pdev)
{
	struct rfkill_rk_data *rfkill = platform_get_drvdata(pdev);

    LOG("Enter %s\n", __func__);

	rfkill_unregister(rfkill->rfkill_dev);
	rfkill_destroy(rfkill->rfkill_dev);

    wake_lock_destroy(&rfkill->bt_irq_wl);
    
    cancel_delayed_work_sync(&rfkill->bt_sleep_delay_work);

    // free gpio
    if (gpio_is_valid(rfkill->pdata->rts_gpio.io))
        gpio_free(rfkill->pdata->rts_gpio.io);
    
    if (gpio_is_valid(rfkill->pdata->wake_host_irq.gpio.io)){
        free_irq(rfkill->pdata->wake_host_irq.irq, rfkill);
        gpio_free(rfkill->pdata->wake_host_irq.gpio.io);
    }
    
    if (gpio_is_valid(rfkill->pdata->wake_gpio.io))
        gpio_free(rfkill->pdata->wake_gpio.io);
    
    if (gpio_is_valid(rfkill->pdata->reset_gpio.io))
        gpio_free(rfkill->pdata->reset_gpio.io);
    
    if (gpio_is_valid(rfkill->pdata->poweron_gpio.io))
        gpio_free(rfkill->pdata->poweron_gpio.io);

    kfree(rfkill);
    g_rfkill = NULL;

	return 0;
}

static struct platform_driver rfkill_rk_driver = {
	.probe = rfkill_rk_probe,
	.remove = __devexit_p(rfkill_rk_remove),
    .suspend = rfkill_rk_suspend,
    .resume = rfkill_rk_resume,
	.driver = {
        .name = "rfkill_rk",
        .owner = THIS_MODULE,
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

MODULE_DESCRIPTION("rock-chips rfkill for Bluetooth v0.2");
MODULE_AUTHOR("cmy@rock-chips.com, cz@rock-chips.com");
MODULE_LICENSE("GPL");

