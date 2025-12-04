/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022, Intel Corporation. */

#ifndef _LIBIE_FWLOG_H_
#define _LIBIE_FWLOG_H_

#include <linux/net/intel/libie/adminq.h>

/* Only a single log level should be set and all log levels under the set value
 * are enabled, e.g. if log level is set to LIBIE_FW_LOG_LEVEL_VERBOSE, then all
 * other log levels are included (except LIBIE_FW_LOG_LEVEL_NONE)
 */
enum libie_fwlog_level {
	LIBIE_FWLOG_LEVEL_NONE = 0,
	LIBIE_FWLOG_LEVEL_ERROR = 1,
	LIBIE_FWLOG_LEVEL_WARNING = 2,
	LIBIE_FWLOG_LEVEL_NORMAL = 3,
	LIBIE_FWLOG_LEVEL_VERBOSE = 4,
	LIBIE_FWLOG_LEVEL_INVALID, /* all values >= this entry are invalid */
};

struct libie_fwlog_module_entry {
	/* module ID for the corresponding firmware logging event */
	u16 module_id;
	/* verbosity level for the module_id */
	u8 log_level;
};

struct libie_fwlog_cfg {
	/* list of modules for configuring log level */
	struct libie_fwlog_module_entry module_entries[LIBIE_AQC_FW_LOG_ID_MAX];
	/* options used to configure firmware logging */
	u16 options;
#define LIBIE_FWLOG_OPTION_ARQ_ENA		BIT(0)
#define LIBIE_FWLOG_OPTION_UART_ENA		BIT(1)
	/* set before calling libie_fwlog_init() so the PF registers for
	 * firmware logging on initialization
	 */
#define LIBIE_FWLOG_OPTION_REGISTER_ON_INIT	BIT(2)
	/* set in the libie_aq_fwlog_get() response if the PF is registered for
	 * FW logging events over ARQ
	 */
#define LIBIE_FWLOG_OPTION_IS_REGISTERED	BIT(3)

	/* minimum number of log events sent per Admin Receive Queue event */
	u16 log_resolution;
};

struct libie_fwlog_data {
	u16 data_size;
	u8 *data;
};

struct libie_fwlog_ring {
	struct libie_fwlog_data *rings;
	u16 index;
	u16 size;
	u16 head;
	u16 tail;
};

#define LIBIE_FWLOG_RING_SIZE_INDEX_DFLT 3
#define LIBIE_FWLOG_RING_SIZE_DFLT 256
#define LIBIE_FWLOG_RING_SIZE_MAX 512

struct libie_fwlog {
	struct libie_fwlog_cfg cfg;
	bool supported; /* does hardware support FW logging? */
	struct libie_fwlog_ring ring;
	struct dentry *debugfs;
	/* keep track of all the dentrys for FW log modules */
	struct dentry **debugfs_modules;
	struct_group_tagged(libie_fwlog_api, api,
		struct pci_dev *pdev;
		int (*send_cmd)(void *, struct libie_aq_desc *, void *, u16);
		void *priv;
		struct dentry *debugfs_root;
	);
};

#if IS_ENABLED(CONFIG_LIBIE_FWLOG)
int libie_fwlog_init(struct libie_fwlog *fwlog, struct libie_fwlog_api *api);
void libie_fwlog_deinit(struct libie_fwlog *fwlog);
void libie_fwlog_reregister(struct libie_fwlog *fwlog);
void libie_get_fwlog_data(struct libie_fwlog *fwlog, u8 *buf, u16 len);
#else
static inline int libie_fwlog_init(struct libie_fwlog *fwlog,
				   struct libie_fwlog_api *api)
{
	return -EOPNOTSUPP;
}
static inline void libie_fwlog_deinit(struct libie_fwlog *fwlog) { }
static inline void libie_fwlog_reregister(struct libie_fwlog *fwlog) { }
static inline void libie_get_fwlog_data(struct libie_fwlog *fwlog, u8 *buf,
					u16 len) { }
#endif /* CONFIG_LIBIE_FWLOG */
#endif /* _LIBIE_FWLOG_H_ */
