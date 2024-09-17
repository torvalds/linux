/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_HTE_H
#define __LINUX_HTE_H

#include <linux/errno.h>

struct hte_chip;
struct hte_device;
struct of_phandle_args;

/**
 * enum hte_edge - HTE line edge flags.
 *
 * @HTE_EDGE_NO_SETUP: No edge setup. In this case consumer will setup edges,
 * for example during request irq call.
 * @HTE_RISING_EDGE_TS: Rising edge.
 * @HTE_FALLING_EDGE_TS: Falling edge.
 *
 */
enum hte_edge {
	HTE_EDGE_NO_SETUP = 1U << 0,
	HTE_RISING_EDGE_TS = 1U << 1,
	HTE_FALLING_EDGE_TS = 1U << 2,
};

/**
 * enum hte_return - HTE subsystem return values used during callback.
 *
 * @HTE_CB_HANDLED: The consumer handled the data.
 * @HTE_RUN_SECOND_CB: The consumer needs further processing, in that case
 * HTE subsystem calls secondary callback provided by the consumer where it
 * is allowed to sleep.
 */
enum hte_return {
	HTE_CB_HANDLED,
	HTE_RUN_SECOND_CB,
};

/**
 * struct hte_ts_data - HTE timestamp data.
 *
 * @tsc: Timestamp value.
 * @seq: Sequence counter of the timestamps.
 * @raw_level: Level of the line at the timestamp if provider supports it,
 * -1 otherwise.
 */
struct hte_ts_data {
	u64 tsc;
	u64 seq;
	int raw_level;
};

/**
 * struct hte_clk_info - Clock source info that HTE provider uses to timestamp.
 *
 * @hz: Supported clock rate in HZ, for example 1KHz clock = 1000.
 * @type: Supported clock type.
 */
struct hte_clk_info {
	u64 hz;
	clockid_t type;
};

/**
 * typedef hte_ts_cb_t - HTE timestamp data processing primary callback.
 *
 * The callback is used to push timestamp data to the client and it is
 * not allowed to sleep.
 *
 * @ts: HW timestamp data.
 * @data: Client supplied data.
 */
typedef enum hte_return (*hte_ts_cb_t)(struct hte_ts_data *ts, void *data);

/**
 * typedef hte_ts_sec_cb_t - HTE timestamp data processing secondary callback.
 *
 * This is used when the client needs further processing where it is
 * allowed to sleep.
 *
 * @data: Client supplied data.
 *
 */
typedef enum hte_return (*hte_ts_sec_cb_t)(void *data);

/**
 * struct hte_line_attr - Line attributes.
 *
 * @line_id: The logical ID understood by the consumers and providers.
 * @line_data: Line data related to line_id.
 * @edge_flags: Edge setup flags.
 * @name: Descriptive name of the entity that is being monitored for the
 * hardware timestamping. If null, HTE core will construct the name.
 *
 */
struct hte_line_attr {
	u32 line_id;
	void *line_data;
	unsigned long edge_flags;
	const char *name;
};

/**
 * struct hte_ts_desc - HTE timestamp descriptor.
 *
 * This structure is a communication token between consumers to subsystem
 * and subsystem to providers.
 *
 * @attr: The line attributes.
 * @hte_data: Subsystem's private data, set by HTE subsystem.
 */
struct hte_ts_desc {
	struct hte_line_attr attr;
	void *hte_data;
};

/**
 * struct hte_ops - HTE operations set by providers.
 *
 * @request: Hook for requesting a HTE timestamp. Returns 0 on success,
 * non-zero for failures.
 * @release: Hook for releasing a HTE timestamp. Returns 0 on success,
 * non-zero for failures.
 * @enable: Hook to enable the specified timestamp. Returns 0 on success,
 * non-zero for failures.
 * @disable: Hook to disable specified timestamp. Returns 0 on success,
 * non-zero for failures.
 * @get_clk_src_info: Hook to get the clock information the provider uses
 * to timestamp. Returns 0 for success and negative error code for failure. On
 * success HTE subsystem fills up provided struct hte_clk_info.
 *
 * xlated_id parameter is used to communicate between HTE subsystem and the
 * providers and is translated by the provider.
 */
struct hte_ops {
	int (*request)(struct hte_chip *chip, struct hte_ts_desc *desc,
		       u32 xlated_id);
	int (*release)(struct hte_chip *chip, struct hte_ts_desc *desc,
		       u32 xlated_id);
	int (*enable)(struct hte_chip *chip, u32 xlated_id);
	int (*disable)(struct hte_chip *chip, u32 xlated_id);
	int (*get_clk_src_info)(struct hte_chip *chip,
				struct hte_clk_info *ci);
};

/**
 * struct hte_chip - Abstract HTE chip.
 *
 * @name: functional name of the HTE IP block.
 * @dev: device providing the HTE.
 * @ops: callbacks for this HTE.
 * @nlines: number of lines/signals supported by this chip.
 * @xlate_of: Callback which translates consumer supplied logical ids to
 * physical ids, return 0 for the success and negative for the failures.
 * It stores (between 0 to @nlines) in xlated_id parameter for the success.
 * @xlate_plat: Same as above but for the consumers with no DT node.
 * @match_from_linedata: Match HTE device using the line_data.
 * @of_hte_n_cells: Number of cells used to form the HTE specifier.
 * @gdev: HTE subsystem abstract device, internal to the HTE subsystem.
 * @data: chip specific private data.
 */
struct hte_chip {
	const char *name;
	struct device *dev;
	const struct hte_ops *ops;
	u32 nlines;
	int (*xlate_of)(struct hte_chip *gc,
			const struct of_phandle_args *args,
			struct hte_ts_desc *desc, u32 *xlated_id);
	int (*xlate_plat)(struct hte_chip *gc, struct hte_ts_desc *desc,
			 u32 *xlated_id);
	bool (*match_from_linedata)(const struct hte_chip *chip,
				    const struct hte_ts_desc *hdesc);
	u8 of_hte_n_cells;

	struct hte_device *gdev;
	void *data;
};

#if IS_ENABLED(CONFIG_HTE)
/* HTE APIs for the providers */
int devm_hte_register_chip(struct hte_chip *chip);
int hte_push_ts_ns(const struct hte_chip *chip, u32 xlated_id,
		   struct hte_ts_data *data);

/* HTE APIs for the consumers */
int hte_init_line_attr(struct hte_ts_desc *desc, u32 line_id,
		       unsigned long edge_flags, const char *name,
		       void *data);
int hte_ts_get(struct device *dev, struct hte_ts_desc *desc, int index);
int hte_ts_put(struct hte_ts_desc *desc);
int hte_request_ts_ns(struct hte_ts_desc *desc, hte_ts_cb_t cb,
		      hte_ts_sec_cb_t tcb, void *data);
int devm_hte_request_ts_ns(struct device *dev, struct hte_ts_desc *desc,
			   hte_ts_cb_t cb, hte_ts_sec_cb_t tcb, void *data);
int of_hte_req_count(struct device *dev);
int hte_enable_ts(struct hte_ts_desc *desc);
int hte_disable_ts(struct hte_ts_desc *desc);
int hte_get_clk_src_info(const struct hte_ts_desc *desc,
			 struct hte_clk_info *ci);

#else /* !CONFIG_HTE */
static inline int devm_hte_register_chip(struct hte_chip *chip)
{
	return -EOPNOTSUPP;
}

static inline int hte_push_ts_ns(const struct hte_chip *chip,
				 u32 xlated_id,
				 const struct hte_ts_data *data)
{
	return -EOPNOTSUPP;
}

static inline int hte_init_line_attr(struct hte_ts_desc *desc, u32 line_id,
				     unsigned long edge_flags,
				     const char *name, void *data)
{
	return -EOPNOTSUPP;
}

static inline int hte_ts_get(struct device *dev, struct hte_ts_desc *desc,
			     int index)
{
	return -EOPNOTSUPP;
}

static inline int hte_ts_put(struct hte_ts_desc *desc)
{
	return -EOPNOTSUPP;
}

static inline int hte_request_ts_ns(struct hte_ts_desc *desc, hte_ts_cb_t cb,
				    hte_ts_sec_cb_t tcb, void *data)
{
	return -EOPNOTSUPP;
}

static inline int devm_hte_request_ts_ns(struct device *dev,
					 struct hte_ts_desc *desc,
					 hte_ts_cb_t cb,
					 hte_ts_sec_cb_t tcb,
					 void *data)
{
	return -EOPNOTSUPP;
}

static inline int of_hte_req_count(struct device *dev)
{
	return -EOPNOTSUPP;
}

static inline int hte_enable_ts(struct hte_ts_desc *desc)
{
	return -EOPNOTSUPP;
}

static inline int hte_disable_ts(struct hte_ts_desc *desc)
{
	return -EOPNOTSUPP;
}

static inline int hte_get_clk_src_info(const struct hte_ts_desc *desc,
				       struct hte_clk_info *ci)
{
	return -EOPNOTSUPP;
}
#endif /* !CONFIG_HTE */

#endif
