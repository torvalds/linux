/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __AWINIC_LOG_H__
#define __AWINIC_LOG_H__

/********************************************
 * print information control
 *******************************************/
#define aw_dev_err(dev, format, ...) \
	do { \
		pr_err("[Awinic][%s]%s: " format "\n", dev_name(dev), __func__, ##__VA_ARGS__); \
	} while (0)
#ifdef AW_INFO_LOG_ENABLE
#define aw_dev_info(dev, format, ...) \
	do { \
		pr_info("[Awinic][%s]%s: " format "\n", dev_name(dev), __func__, ##__VA_ARGS__); \
	} while (0)
#else
#define aw_dev_info(dev, format, ...) \
	do { \
		pr_debug("[Awinic][%s]%s: " format "\n", dev_name(dev), __func__, ##__VA_ARGS__); \
	} while (0)
#endif

#define aw_dev_dbg(dev, format, ...) \
	do { \
		pr_debug("[Awinic][%s]%s: " format "\n", dev_name(dev), __func__, ##__VA_ARGS__); \
	} while (0)

#define aw_pr_err(format, ...) \
	do { \
		pr_err("[Awinic]%s: " format "\n", __func__, ##__VA_ARGS__); \
	} while (0)
#ifdef AW_INFO_LOG_ENABLE
#define aw_pr_info(format, ...) \
	do { \
		pr_info("[Awinic]%s: " format "\n", __func__, ##__VA_ARGS__); \
	} while (0)

#else
#define aw_pr_info(format, ...) \
	do { \
		pr_debug("[Awinic]%s: " format "\n", __func__, ##__VA_ARGS__); \
	} while (0)
#endif

#define aw_pr_dbg(format, ...) \
	do { \
		pr_debug("[Awinic]%s: " format "\n", __func__, ##__VA_ARGS__); \
	} while (0)


#endif

