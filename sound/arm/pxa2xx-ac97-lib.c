// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on sound/arm/pxa2xx-ac97.c and sound/soc/pxa/pxa2xx-ac97.c
 * which contain:
 *
 * Author:	Nicolas Pitre
 * Created:	Dec 02, 2004
 * Copyright:	MontaVista Software Inc.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/soc/pxa/cpu.h>

#include <sound/pxa2xx-lib.h>

#include <linux/platform_data/asoc-pxa.h>

#include "pxa2xx-ac97-regs.h"

static DEFINE_MUTEX(car_mutex);
static DECLARE_WAIT_QUEUE_HEAD(gsr_wq);
static volatile long gsr_bits;
static struct clk *ac97_clk;
static struct clk *ac97conf_clk;
static int reset_gpio;
static void __iomem *ac97_reg_base;

extern void pxa27x_configure_ac97reset(int reset_gpio, bool to_gpio);

/*
 * Beware PXA27x bugs:
 *
 *   o Slot 12 read from modem space will hang controller.
 *   o CDONE, SDONE interrupt fails after any slot 12 IO.
 *
 * We therefore have an hybrid approach for waiting on SDONE (interrupt or
 * 1 jiffy timeout if interrupt never comes).
 */

int pxa2xx_ac97_read(int slot, unsigned short reg)
{
	int val = -ENODEV;
	u32 __iomem *reg_addr;

	if (slot > 0)
		return -ENODEV;

	mutex_lock(&car_mutex);

	/* set up primary or secondary codec space */
	if (cpu_is_pxa25x() && reg == AC97_GPIO_STATUS)
		reg_addr = ac97_reg_base +
			   (slot ? SMC_REG_BASE : PMC_REG_BASE);
	else
		reg_addr = ac97_reg_base +
			   (slot ? SAC_REG_BASE : PAC_REG_BASE);
	reg_addr += (reg >> 1);

	/* start read access across the ac97 link */
	writel(GSR_CDONE | GSR_SDONE, ac97_reg_base + GSR);
	gsr_bits = 0;
	val = (readl(reg_addr) & 0xffff);
	if (reg == AC97_GPIO_STATUS)
		goto out;
	if (wait_event_timeout(gsr_wq, (readl(ac97_reg_base + GSR) | gsr_bits) & GSR_SDONE, 1) <= 0 &&
	    !((readl(ac97_reg_base + GSR) | gsr_bits) & GSR_SDONE)) {
		printk(KERN_ERR "%s: read error (ac97_reg=%d GSR=%#lx)\n",
				__func__, reg, readl(ac97_reg_base + GSR) | gsr_bits);
		val = -ETIMEDOUT;
		goto out;
	}

	/* valid data now */
	writel(GSR_CDONE | GSR_SDONE, ac97_reg_base + GSR);
	gsr_bits = 0;
	val = (readl(reg_addr) & 0xffff);
	/* but we've just started another cycle... */
	wait_event_timeout(gsr_wq, (readl(ac97_reg_base + GSR) | gsr_bits) & GSR_SDONE, 1);

out:	mutex_unlock(&car_mutex);
	return val;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_read);

int pxa2xx_ac97_write(int slot, unsigned short reg, unsigned short val)
{
	u32 __iomem *reg_addr;
	int ret = 0;

	mutex_lock(&car_mutex);

	/* set up primary or secondary codec space */
	if (cpu_is_pxa25x() && reg == AC97_GPIO_STATUS)
		reg_addr = ac97_reg_base +
			   (slot ? SMC_REG_BASE : PMC_REG_BASE);
	else
		reg_addr = ac97_reg_base +
			   (slot ? SAC_REG_BASE : PAC_REG_BASE);
	reg_addr += (reg >> 1);

	writel(GSR_CDONE | GSR_SDONE, ac97_reg_base + GSR);
	gsr_bits = 0;
	writel(val, reg_addr);
	if (wait_event_timeout(gsr_wq, (readl(ac97_reg_base + GSR) | gsr_bits) & GSR_CDONE, 1) <= 0 &&
	    !((readl(ac97_reg_base + GSR) | gsr_bits) & GSR_CDONE)) {
		printk(KERN_ERR "%s: write error (ac97_reg=%d GSR=%#lx)\n",
				__func__, reg, readl(ac97_reg_base + GSR) | gsr_bits);
		ret = -EIO;
	}

	mutex_unlock(&car_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_write);

#ifdef CONFIG_PXA25x
static inline void pxa_ac97_warm_pxa25x(void)
{
	gsr_bits = 0;

	writel(readl(ac97_reg_base + GCR) | (GCR_WARM_RST), ac97_reg_base + GCR);
}

static inline void pxa_ac97_cold_pxa25x(void)
{
	writel(readl(ac97_reg_base + GCR) & ( GCR_COLD_RST), ac97_reg_base + GCR);  /* clear everything but nCRST */
	writel(readl(ac97_reg_base + GCR) & (~GCR_COLD_RST), ac97_reg_base + GCR);  /* then assert nCRST */

	gsr_bits = 0;

	writel(GCR_COLD_RST, ac97_reg_base + GCR);
}
#endif

#ifdef CONFIG_PXA27x
static inline void pxa_ac97_warm_pxa27x(void)
{
	gsr_bits = 0;

	/* warm reset broken on Bulverde, so manually keep AC97 reset high */
	pxa27x_configure_ac97reset(reset_gpio, true);
	udelay(10);
	writel(readl(ac97_reg_base + GCR) | (GCR_WARM_RST), ac97_reg_base + GCR);
	pxa27x_configure_ac97reset(reset_gpio, false);
	udelay(500);
}

static inline void pxa_ac97_cold_pxa27x(void)
{
	writel(readl(ac97_reg_base + GCR) & ( GCR_COLD_RST), ac97_reg_base + GCR);  /* clear everything but nCRST */
	writel(readl(ac97_reg_base + GCR) & (~GCR_COLD_RST), ac97_reg_base + GCR);  /* then assert nCRST */

	gsr_bits = 0;

	/* PXA27x Developers Manual section 13.5.2.2.1 */
	clk_prepare_enable(ac97conf_clk);
	udelay(5);
	clk_disable_unprepare(ac97conf_clk);
	writel(GCR_COLD_RST | GCR_WARM_RST, ac97_reg_base + GCR);
}
#endif

#ifdef CONFIG_PXA3xx
static inline void pxa_ac97_warm_pxa3xx(void)
{
	gsr_bits = 0;

	/* Can't use interrupts */
	writel(readl(ac97_reg_base + GCR) | (GCR_WARM_RST), ac97_reg_base + GCR);
}

static inline void pxa_ac97_cold_pxa3xx(void)
{
	/* Hold CLKBPB for 100us */
	writel(0, ac97_reg_base + GCR);
	writel(GCR_CLKBPB, ac97_reg_base + GCR);
	udelay(100);
	writel(0, ac97_reg_base + GCR);

	writel(readl(ac97_reg_base + GCR) & ( GCR_COLD_RST), ac97_reg_base + GCR);  /* clear everything but nCRST */
	writel(readl(ac97_reg_base + GCR) & (~GCR_COLD_RST), ac97_reg_base + GCR);  /* then assert nCRST */

	gsr_bits = 0;

	/* Can't use interrupts on PXA3xx */
	writel(readl(ac97_reg_base + GCR) & (~(GCR_PRIRDY_IEN|GCR_SECRDY_IEN)), ac97_reg_base + GCR);

	writel(GCR_WARM_RST | GCR_COLD_RST, ac97_reg_base + GCR);
}
#endif

bool pxa2xx_ac97_try_warm_reset(void)
{
	unsigned long gsr;
	unsigned int timeout = 100;

#ifdef CONFIG_PXA25x
	if (cpu_is_pxa25x())
		pxa_ac97_warm_pxa25x();
	else
#endif
#ifdef CONFIG_PXA27x
	if (cpu_is_pxa27x())
		pxa_ac97_warm_pxa27x();
	else
#endif
#ifdef CONFIG_PXA3xx
	if (cpu_is_pxa3xx())
		pxa_ac97_warm_pxa3xx();
	else
#endif
		snd_BUG();

	while (!((readl(ac97_reg_base + GSR) | gsr_bits) & (GSR_PCR | GSR_SCR)) && timeout--)
		mdelay(1);

	gsr = readl(ac97_reg_base + GSR) | gsr_bits;
	if (!(gsr & (GSR_PCR | GSR_SCR))) {
		printk(KERN_INFO "%s: warm reset timeout (GSR=%#lx)\n",
				 __func__, gsr);

		return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_try_warm_reset);

bool pxa2xx_ac97_try_cold_reset(void)
{
	unsigned long gsr;
	unsigned int timeout = 1000;

#ifdef CONFIG_PXA25x
	if (cpu_is_pxa25x())
		pxa_ac97_cold_pxa25x();
	else
#endif
#ifdef CONFIG_PXA27x
	if (cpu_is_pxa27x())
		pxa_ac97_cold_pxa27x();
	else
#endif
#ifdef CONFIG_PXA3xx
	if (cpu_is_pxa3xx())
		pxa_ac97_cold_pxa3xx();
	else
#endif
		snd_BUG();

	while (!((readl(ac97_reg_base + GSR) | gsr_bits) & (GSR_PCR | GSR_SCR)) && timeout--)
		mdelay(1);

	gsr = readl(ac97_reg_base + GSR) | gsr_bits;
	if (!(gsr & (GSR_PCR | GSR_SCR))) {
		printk(KERN_INFO "%s: cold reset timeout (GSR=%#lx)\n",
				 __func__, gsr);

		return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_try_cold_reset);


void pxa2xx_ac97_finish_reset(void)
{
	u32 gcr = readl(ac97_reg_base + GCR);
	gcr &= ~(GCR_PRIRDY_IEN|GCR_SECRDY_IEN);
	gcr |= GCR_SDONE_IE|GCR_CDONE_IE;
	writel(gcr, ac97_reg_base + GCR);
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_finish_reset);

static irqreturn_t pxa2xx_ac97_irq(int irq, void *dev_id)
{
	long status;

	status = readl(ac97_reg_base + GSR);
	if (status) {
		writel(status, ac97_reg_base + GSR);
		gsr_bits |= status;
		wake_up(&gsr_wq);

		/* Although we don't use those we still need to clear them
		   since they tend to spuriously trigger when MMC is used
		   (hardware bug? go figure)... */
		if (cpu_is_pxa27x()) {
			writel(MISR_EOC, ac97_reg_base + MISR);
			writel(PISR_EOC, ac97_reg_base + PISR);
			writel(MCSR_EOC, ac97_reg_base + MCSR);
		}

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

#ifdef CONFIG_PM
int pxa2xx_ac97_hw_suspend(void)
{
	writel(readl(ac97_reg_base + GCR) | (GCR_ACLINK_OFF), ac97_reg_base + GCR);
	clk_disable_unprepare(ac97_clk);
	return 0;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_hw_suspend);

int pxa2xx_ac97_hw_resume(void)
{
	clk_prepare_enable(ac97_clk);
	return 0;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_hw_resume);
#endif

int pxa2xx_ac97_hw_probe(struct platform_device *dev)
{
	int ret;
	int irq;
	pxa2xx_audio_ops_t *pdata = dev->dev.platform_data;

	ac97_reg_base = devm_platform_ioremap_resource(dev, 0);
	if (IS_ERR(ac97_reg_base)) {
		dev_err(&dev->dev, "Missing MMIO resource\n");
		return PTR_ERR(ac97_reg_base);
	}

	if (pdata) {
		switch (pdata->reset_gpio) {
		case 95:
		case 113:
			reset_gpio = pdata->reset_gpio;
			break;
		case 0:
			reset_gpio = 113;
			break;
		case -1:
			break;
		default:
			dev_err(&dev->dev, "Invalid reset GPIO %d\n",
				pdata->reset_gpio);
		}
	} else if (!pdata && dev->dev.of_node) {
		pdata = devm_kzalloc(&dev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		pdata->reset_gpio = of_get_named_gpio(dev->dev.of_node,
						      "reset-gpios", 0);
		if (pdata->reset_gpio == -ENOENT)
			pdata->reset_gpio = -1;
		else if (pdata->reset_gpio < 0)
			return pdata->reset_gpio;
		reset_gpio = pdata->reset_gpio;
	} else {
		if (cpu_is_pxa27x())
			reset_gpio = 113;
	}

	if (cpu_is_pxa27x()) {
		/*
		 * This gpio is needed for a work-around to a bug in the ac97
		 * controller during warm reset.  The direction and level is set
		 * here so that it is an output driven high when switching from
		 * AC97_nRESET alt function to generic gpio.
		 */
		ret = gpio_request_one(reset_gpio, GPIOF_OUT_INIT_HIGH,
				       "pxa27x ac97 reset");
		if (ret < 0) {
			pr_err("%s: gpio_request_one() failed: %d\n",
			       __func__, ret);
			goto err_conf;
		}
		pxa27x_configure_ac97reset(reset_gpio, false);

		ac97conf_clk = clk_get(&dev->dev, "AC97CONFCLK");
		if (IS_ERR(ac97conf_clk)) {
			ret = PTR_ERR(ac97conf_clk);
			ac97conf_clk = NULL;
			goto err_conf;
		}
	}

	ac97_clk = clk_get(&dev->dev, "AC97CLK");
	if (IS_ERR(ac97_clk)) {
		ret = PTR_ERR(ac97_clk);
		ac97_clk = NULL;
		goto err_clk;
	}

	ret = clk_prepare_enable(ac97_clk);
	if (ret)
		goto err_clk2;

	irq = platform_get_irq(dev, 0);
	if (!irq)
		goto err_irq;

	ret = request_irq(irq, pxa2xx_ac97_irq, 0, "AC97", NULL);
	if (ret < 0)
		goto err_irq;

	return 0;

err_irq:
	writel(readl(ac97_reg_base + GCR) | (GCR_ACLINK_OFF), ac97_reg_base + GCR);
err_clk2:
	clk_put(ac97_clk);
	ac97_clk = NULL;
err_clk:
	if (ac97conf_clk) {
		clk_put(ac97conf_clk);
		ac97conf_clk = NULL;
	}
err_conf:
	return ret;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_hw_probe);

void pxa2xx_ac97_hw_remove(struct platform_device *dev)
{
	if (cpu_is_pxa27x())
		gpio_free(reset_gpio);
	writel(readl(ac97_reg_base + GCR) | (GCR_ACLINK_OFF), ac97_reg_base + GCR);
	free_irq(platform_get_irq(dev, 0), NULL);
	if (ac97conf_clk) {
		clk_put(ac97conf_clk);
		ac97conf_clk = NULL;
	}
	clk_disable_unprepare(ac97_clk);
	clk_put(ac97_clk);
	ac97_clk = NULL;
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_hw_remove);

u32 pxa2xx_ac97_read_modr(void)
{
	if (!ac97_reg_base)
		return 0;

	return readl(ac97_reg_base + MODR);
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_read_modr);

u32 pxa2xx_ac97_read_misr(void)
{
	if (!ac97_reg_base)
		return 0;

	return readl(ac97_reg_base + MISR);
}
EXPORT_SYMBOL_GPL(pxa2xx_ac97_read_misr);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("Intel/Marvell PXA sound library");
MODULE_LICENSE("GPL");

