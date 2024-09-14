// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2012 Freescale Semiconductor, Inc.
// Copyright 2012 Linaro Ltd.
// Copyright 2009 Pengutronix, Sascha Hauer <s.hauer@pengutronix.de>
//
// Initial development of this code was funded by
// Phytec Messtechnik GmbH, https://www.phytec.de

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "imx-audmux.h"

#define DRIVER_NAME "imx-audmux"

static struct clk *audmux_clk;
static void __iomem *audmux_base;
static u32 *regcache;
static u32 reg_max;

#define IMX_AUDMUX_V2_PTCR(x)		((x) * 8)
#define IMX_AUDMUX_V2_PDCR(x)		((x) * 8 + 4)

#ifdef CONFIG_DEBUG_FS
static struct dentry *audmux_debugfs_root;

/* There is an annoying discontinuity in the SSI numbering with regard
 * to the Linux number of the devices */
static const char *audmux_port_string(int port)
{
	switch (port) {
	case MX31_AUDMUX_PORT1_SSI0:
		return "imx-ssi.0";
	case MX31_AUDMUX_PORT2_SSI1:
		return "imx-ssi.1";
	case MX31_AUDMUX_PORT3_SSI_PINS_3:
		return "SSI3";
	case MX31_AUDMUX_PORT4_SSI_PINS_4:
		return "SSI4";
	case MX31_AUDMUX_PORT5_SSI_PINS_5:
		return "SSI5";
	case MX31_AUDMUX_PORT6_SSI_PINS_6:
		return "SSI6";
	default:
		return "UNKNOWN";
	}
}

static ssize_t audmux_read_file(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	ssize_t ret;
	char *buf;
	uintptr_t port = (uintptr_t)file->private_data;
	u32 pdcr, ptcr;

	ret = clk_prepare_enable(audmux_clk);
	if (ret)
		return ret;

	ptcr = readl(audmux_base + IMX_AUDMUX_V2_PTCR(port));
	pdcr = readl(audmux_base + IMX_AUDMUX_V2_PDCR(port));

	clk_disable_unprepare(audmux_clk);

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = sysfs_emit(buf, "PDCR: %08x\nPTCR: %08x\n", pdcr, ptcr);

	if (ptcr & IMX_AUDMUX_V2_PTCR_TFSDIR)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
				"TxFS output from %s, ",
				audmux_port_string((ptcr >> 27) & 0x7));
	else
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
				"TxFS input, ");

	if (ptcr & IMX_AUDMUX_V2_PTCR_TCLKDIR)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
				"TxClk output from %s",
				audmux_port_string((ptcr >> 22) & 0x7));
	else
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
				"TxClk input");

	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");

	if (ptcr & IMX_AUDMUX_V2_PTCR_SYN) {
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
				"Port is symmetric");
	} else {
		if (ptcr & IMX_AUDMUX_V2_PTCR_RFSDIR)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					"RxFS output from %s, ",
					audmux_port_string((ptcr >> 17) & 0x7));
		else
			ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					"RxFS input, ");

		if (ptcr & IMX_AUDMUX_V2_PTCR_RCLKDIR)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					"RxClk output from %s",
					audmux_port_string((ptcr >> 12) & 0x7));
		else
			ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					"RxClk input");
	}

	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"\nData received from %s\n",
			audmux_port_string((pdcr >> 13) & 0x7));

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return ret;
}

static const struct file_operations audmux_debugfs_fops = {
	.open = simple_open,
	.read = audmux_read_file,
	.llseek = default_llseek,
};

static void audmux_debugfs_init(void)
{
	uintptr_t i;
	char buf[20];

	audmux_debugfs_root = debugfs_create_dir("audmux", NULL);

	for (i = 0; i < MX31_AUDMUX_PORT7_SSI_PINS_7 + 1; i++) {
		snprintf(buf, sizeof(buf), "ssi%lu", i);
		debugfs_create_file(buf, 0444, audmux_debugfs_root,
				    (void *)i, &audmux_debugfs_fops);
	}
}

static void audmux_debugfs_remove(void)
{
	debugfs_remove_recursive(audmux_debugfs_root);
}
#else
static inline void audmux_debugfs_init(void)
{
}

static inline void audmux_debugfs_remove(void)
{
}
#endif

static enum imx_audmux_type {
	IMX21_AUDMUX,
	IMX31_AUDMUX,
} audmux_type;

static const struct of_device_id imx_audmux_dt_ids[] = {
	{ .compatible = "fsl,imx21-audmux", .data = (void *)IMX21_AUDMUX, },
	{ .compatible = "fsl,imx31-audmux", .data = (void *)IMX31_AUDMUX, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_audmux_dt_ids);

static const uint8_t port_mapping[] = {
	0x0, 0x4, 0x8, 0x10, 0x14, 0x1c,
};

int imx_audmux_v1_configure_port(unsigned int port, unsigned int pcr)
{
	if (audmux_type != IMX21_AUDMUX)
		return -EINVAL;

	if (!audmux_base)
		return -ENOSYS;

	if (port >= ARRAY_SIZE(port_mapping))
		return -EINVAL;

	writel(pcr, audmux_base + port_mapping[port]);

	return 0;
}
EXPORT_SYMBOL_GPL(imx_audmux_v1_configure_port);

int imx_audmux_v2_configure_port(unsigned int port, unsigned int ptcr,
		unsigned int pdcr)
{
	int ret;

	if (audmux_type != IMX31_AUDMUX)
		return -EINVAL;

	if (!audmux_base)
		return -ENOSYS;

	ret = clk_prepare_enable(audmux_clk);
	if (ret)
		return ret;

	writel(ptcr, audmux_base + IMX_AUDMUX_V2_PTCR(port));
	writel(pdcr, audmux_base + IMX_AUDMUX_V2_PDCR(port));

	clk_disable_unprepare(audmux_clk);

	return 0;
}
EXPORT_SYMBOL_GPL(imx_audmux_v2_configure_port);

static int imx_audmux_parse_dt_defaults(struct platform_device *pdev,
		struct device_node *of_node)
{
	struct device_node *child;

	for_each_available_child_of_node(of_node, child) {
		unsigned int port;
		unsigned int ptcr = 0;
		unsigned int pdcr = 0;
		unsigned int pcr = 0;
		unsigned int val;
		int ret;
		int i = 0;

		ret = of_property_read_u32(child, "fsl,audmux-port", &port);
		if (ret) {
			dev_warn(&pdev->dev, "Failed to get fsl,audmux-port of child node \"%pOF\"\n",
					child);
			continue;
		}
		if (!of_property_read_bool(child, "fsl,port-config")) {
			dev_warn(&pdev->dev, "child node \"%pOF\" does not have property fsl,port-config\n",
					child);
			continue;
		}

		for (i = 0; (ret = of_property_read_u32_index(child,
					"fsl,port-config", i, &val)) == 0;
				++i) {
			if (audmux_type == IMX31_AUDMUX) {
				if (i % 2)
					pdcr |= val;
				else
					ptcr |= val;
			} else {
				pcr |= val;
			}
		}

		if (ret != -EOVERFLOW) {
			dev_err(&pdev->dev, "Failed to read u32 at index %d of child %pOF\n",
					i, child);
			continue;
		}

		if (audmux_type == IMX31_AUDMUX) {
			if (i % 2) {
				dev_err(&pdev->dev, "One pdcr value is missing in child node %pOF\n",
						child);
				continue;
			}
			imx_audmux_v2_configure_port(port, ptcr, pdcr);
		} else {
			imx_audmux_v1_configure_port(port, pcr);
		}
	}

	return 0;
}

static int imx_audmux_probe(struct platform_device *pdev)
{
	audmux_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(audmux_base))
		return PTR_ERR(audmux_base);

	audmux_clk = devm_clk_get(&pdev->dev, "audmux");
	if (IS_ERR(audmux_clk)) {
		dev_dbg(&pdev->dev, "cannot get clock: %ld\n",
				PTR_ERR(audmux_clk));
		audmux_clk = NULL;
	}

	audmux_type = (uintptr_t)of_device_get_match_data(&pdev->dev);

	switch (audmux_type) {
	case IMX31_AUDMUX:
		audmux_debugfs_init();
		reg_max = 14;
		break;
	case IMX21_AUDMUX:
		reg_max = 6;
		break;
	default:
		dev_err(&pdev->dev, "unsupported version!\n");
		return -EINVAL;
	}

	regcache = devm_kzalloc(&pdev->dev, sizeof(u32) * reg_max, GFP_KERNEL);
	if (!regcache)
		return -ENOMEM;

	imx_audmux_parse_dt_defaults(pdev, pdev->dev.of_node);

	return 0;
}

static void imx_audmux_remove(struct platform_device *pdev)
{
	if (audmux_type == IMX31_AUDMUX)
		audmux_debugfs_remove();
}

static int imx_audmux_suspend(struct device *dev)
{
	int i;

	clk_prepare_enable(audmux_clk);

	for (i = 0; i < reg_max; i++)
		regcache[i] = readl(audmux_base + i * 4);

	clk_disable_unprepare(audmux_clk);

	return 0;
}

static int imx_audmux_resume(struct device *dev)
{
	int i;

	clk_prepare_enable(audmux_clk);

	for (i = 0; i < reg_max; i++)
		writel(regcache[i], audmux_base + i * 4);

	clk_disable_unprepare(audmux_clk);

	return 0;
}

static const struct dev_pm_ops imx_audmux_pm = {
	SYSTEM_SLEEP_PM_OPS(imx_audmux_suspend, imx_audmux_resume)
};

static struct platform_driver imx_audmux_driver = {
	.probe		= imx_audmux_probe,
	.remove		= imx_audmux_remove,
	.driver	= {
		.name	= DRIVER_NAME,
		.pm = pm_sleep_ptr(&imx_audmux_pm),
		.of_match_table = imx_audmux_dt_ids,
	}
};

static int __init imx_audmux_init(void)
{
	return platform_driver_register(&imx_audmux_driver);
}
subsys_initcall(imx_audmux_init);

static void __exit imx_audmux_exit(void)
{
	platform_driver_unregister(&imx_audmux_driver);
}
module_exit(imx_audmux_exit);

MODULE_DESCRIPTION("Freescale i.MX AUDMUX driver");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
