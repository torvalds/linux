/* sound/soc/samsung/audss.c
 *
 * ALSA SoC Audio Layer - Samsung Audio Subsystem driver
 *
 * Copyright (c) 2010 Samsung Electronics Co. Ltd.
 *	Lakkyung Jung <lakkyung.jung@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <plat/audio.h>
#include <plat/clock.h>
#include <mach/map.h>
#include <mach/regs-audss.h>

#include "audss.h"

static struct audss_runtime_data {
	struct clk *mout_audss;
	struct clk *dout_srp;
	struct clk *srp_clk;
	struct clk *bus_clk;
	struct clk *i2s_clk;

	char	*rclksrc;
	u32	clk_src_rate;
	u32	suspend_audss_clksrc;
	u32	suspend_audss_clkdiv;
	u32	suspend_audss_clkgate;

	bool	clk_enabled;
	bool	reg_saved;
} audss;

static char *rclksrc[] = {
	[0] = "busclk",
	[1] = "i2sclk",
};

/* Lock for cross i/f checks */
static DEFINE_SPINLOCK(lock);

static int audss_clk_div_init(struct clk *src_clk)
{
	u32 clk_div = readl(S5P_CLKDIV_AUDSS);
	u32 src_clk_rate = 0;
	u32 bus_div = 0;
	u32 i2s_div = 0;
	u32 ret = -1;

	src_clk_rate = clk_get_rate(src_clk);
	if (!src_clk_rate) {
		pr_err("%s: Can't get current clk_rate %d\n",
			__func__, src_clk_rate);
		return ret;
	}

	pr_debug("%s: SRC Clock Rate[%d]\n", __func__, src_clk_rate);
	switch (src_clk_rate) {
	case 192000000:
	case 180633605:
	case 180000000:
		bus_div = 4;
		i2s_div = !strcmp(audss.rclksrc, "i2sclk") ? 4 : 16;
		break;

	case 96000000:
		bus_div = 2;
		i2s_div = !strcmp(audss.rclksrc, "i2sclk") ? 2 : 16;
		break;

	default:
		pr_err("%s: Not supported src clk rate\n", __func__);
	}

	clk_div &= ~(S5P_AUDSS_CLKDIV_RP_MASK
		| S5P_AUDSS_CLKDIV_BUSCLK_MASK
		| S5P_AUDSS_CLKDIV_I2SCLK_MASK);

	if (bus_div)
		clk_div |= (bus_div - 1) << S5P_AUDSS_CLKDIV_BUSCLK_SHIFT;

	if (i2s_div)
		clk_div |= (i2s_div - 1) << S5P_AUDSS_CLKDIV_I2SCLK_SHIFT;

	writel(clk_div, S5P_CLKDIV_AUDSS);

	pr_debug("%s: BUSCLK[%ld], I2SCLK[%ld]\n", __func__,
						clk_get_rate(audss.bus_clk),
						clk_get_rate(audss.i2s_clk));
	pr_debug("%s: CLKDIV[0x%x]\n", __func__, readl(S5P_CLKDIV_AUDSS));

	return 0;
}

void audss_reg_save(void)
{
	if (audss.reg_saved)
		return;

	audss.suspend_audss_clksrc = readl(S5P_CLKSRC_AUDSS);
	audss.suspend_audss_clkdiv = readl(S5P_CLKDIV_AUDSS);
	audss.suspend_audss_clkgate = readl(S5P_CLKGATE_AUDSS);
	audss.reg_saved = true;

	pr_debug("%s: Successfully saved audss reg\n", __func__);
	pr_debug("%s: SRC[0x%x], DIV[0x%x], GATE[0x%x]\n", __func__,
					audss.suspend_audss_clksrc,
					audss.suspend_audss_clkdiv,
					audss.suspend_audss_clkgate);
}

void audss_reg_restore(void)
{
	if (!audss.reg_saved)
		return;

	writel(audss.suspend_audss_clksrc, S5P_CLKSRC_AUDSS);
	writel(audss.suspend_audss_clkdiv, S5P_CLKDIV_AUDSS);
	writel(audss.suspend_audss_clkgate, S5P_CLKGATE_AUDSS);
	audss.reg_saved = false;

	pr_debug("%s: Successfully restored audss reg\n", __func__);
	pr_debug("%s: SRC[0x%x], DIV[0x%x], GATE[0x%x]\n", __func__,
					audss.suspend_audss_clksrc,
					audss.suspend_audss_clkdiv,
					audss.suspend_audss_clkgate);
}

void audss_clk_enable(bool enable)
{
	unsigned long flags;

	pr_debug("%s: state %d\n", __func__, enable);
	spin_lock_irqsave(&lock, flags);

	if (enable) {
		if (audss.clk_enabled) {
			pr_debug("%s: Already enabled audss clk %d\n",
					__func__, audss.clk_enabled);
			goto exit_func;
		}

		audss_reg_restore();
		clk_enable(audss.srp_clk);
		clk_enable(audss.bus_clk);
		if (!strcmp(audss.rclksrc, "i2sclk"))
			clk_enable(audss.i2s_clk);

		audss.clk_enabled = true;
	} else {
		if (!audss.clk_enabled) {
			pr_debug("%s: Already disabled audss clk %d\n",
					__func__, audss.clk_enabled);
			goto exit_func;
		}

		clk_disable(audss.bus_clk);
		clk_disable(audss.srp_clk);
		if (!strcmp(audss.rclksrc, "i2sclk"))
			clk_disable(audss.i2s_clk);

		audss.clk_enabled = false;
		audss_reg_save();
	}

	pr_debug("%s: SRC[0x%x], DIV[0x%x], GATE[0x%x]\n", __func__,
						readl(S5P_CLKSRC_AUDSS),
						readl(S5P_CLKDIV_AUDSS),
						readl(S5P_CLKGATE_AUDSS));
exit_func:
	spin_unlock_irqrestore(&lock, flags);

	return;
}

void audss_suspend(void)
{
	if (!audss.reg_saved)
		audss_reg_save();
}

void audss_resume(void)
{
	if (audss.reg_saved)
		audss_reg_restore();
}

static __devinit int audss_init(void)
{
	int ret = 0;

	audss.mout_audss = clk_get(NULL, "mout_audss");
	if (IS_ERR(audss.mout_audss)) {
		pr_err("%s: failed to get mout audss\n", __func__);
		ret = PTR_ERR(audss.mout_audss);
		return ret;
	}

	audss.dout_srp = clk_get(NULL, "dout_srp");
	if (IS_ERR(audss.dout_srp)) {
		pr_err("%s: failed to get dout_srp\n", __func__);
		ret = PTR_ERR(audss.dout_srp);
		goto err1;
	}

	audss.srp_clk = clk_get(NULL, "srpclk");
	if (IS_ERR(audss.srp_clk)) {
		pr_err("%s:failed to get srp_clk\n", __func__);
		ret = PTR_ERR(audss.srp_clk);
		goto err2;
	}

	audss.bus_clk = clk_get(NULL, "busclk");
	if (IS_ERR(audss.bus_clk)) {
		pr_err("%s: failed to get bus clk\n", __func__);
		ret = PTR_ERR(audss.bus_clk);
		goto err3;
	}

	audss.i2s_clk = clk_get(NULL, "i2sclk");
	if (IS_ERR(audss.i2s_clk)) {
		pr_err("%s: failed to get i2s clk\n", __func__);
		ret = PTR_ERR(audss.i2s_clk);
		goto err4;
	}

	audss.rclksrc = rclksrc[BUSCLK];
	pr_info("%s: RCLK SRC[%s]\n", __func__, audss.rclksrc);

	audss.reg_saved = false;
	audss.clk_enabled = false;

	ret = audss_clk_div_init(audss.mout_audss);
	if (ret < 0) {
		pr_err("%s: failed to init clk div\n", __func__);
		goto err5;
	}

	audss_reg_save();

	return ret;
err5:
	clk_put(audss.i2s_clk);
err4:
	clk_put(audss.bus_clk);
err3:
	clk_put(audss.srp_clk);
err2:
	clk_put(audss.dout_srp);
err1:
	clk_put(audss.mout_audss);

	return ret;
}

static __devexit int audss_deinit(void)
{
	clk_put(audss.i2s_clk);
	clk_put(audss.bus_clk);
	clk_put(audss.srp_clk);
	clk_put(audss.dout_srp);
	clk_put(audss.mout_audss);

	audss.rclksrc = NULL;

	return 0;
}

static char banner[] __initdata = "Samsung Audio Subsystem Driver, (c) 2011 Samsung Electronics";

static int __init samsung_audss_init(void)
{
	int ret = 0;

	pr_info("%s\n", banner);

	ret = audss_init();
	if (ret < 0)
		pr_err("%s:failed to init audss clock\n", __func__);

	return ret;

}
module_init(samsung_audss_init);

static void __exit samsung_audss_exit(void)
{
	audss_deinit();
}
module_exit(samsung_audss_exit);

/* Module information */
MODULE_AUTHOR("Lakkyung Jung, <lakkyung.jung@samsung.com>");
MODULE_DESCRIPTION("Samsung Audio subsystem Interface");
MODULE_ALIAS("platform:samsung-audss");
MODULE_LICENSE("GPL");
