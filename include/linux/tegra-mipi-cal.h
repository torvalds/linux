/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __TEGRA_MIPI_CAL_H_
#define __TEGRA_MIPI_CAL_H_

struct tegra_mipi_device {
	const struct tegra_mipi_ops *ops;
	struct platform_device *pdev;
	unsigned long pads;
};

/**
 * Operations for Tegra MIPI calibration device
 */
struct tegra_mipi_ops {
	/**
	 * @enable:
	 *
	 * Enable MIPI calibration device
	 */
	int (*enable)(struct tegra_mipi_device *device);

	/**
	 * @disable:
	 *
	 * Disable MIPI calibration device
	 */
	int (*disable)(struct tegra_mipi_device *device);

	/**
	 * @start_calibration:
	 *
	 * Start MIPI calibration
	 */
	int (*start_calibration)(struct tegra_mipi_device *device);

	/**
	 * @finish_calibration:
	 *
	 * Finish MIPI calibration
	 */
	int (*finish_calibration)(struct tegra_mipi_device *device);
};

int devm_tegra_mipi_add_provider(struct device *device, struct device_node *np,
				 const struct tegra_mipi_ops *ops);

struct tegra_mipi_device *tegra_mipi_request(struct device *device,
					     struct device_node *np);
void tegra_mipi_free(struct tegra_mipi_device *device);

int tegra_mipi_enable(struct tegra_mipi_device *device);
int tegra_mipi_disable(struct tegra_mipi_device *device);
int tegra_mipi_start_calibration(struct tegra_mipi_device *device);
int tegra_mipi_finish_calibration(struct tegra_mipi_device *device);

#endif /* __TEGRA_MIPI_CAL_H_ */
