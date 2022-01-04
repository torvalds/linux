/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __AW_LOG_H__
#define __AW_LOG_H__

#include <linux/kernel.h>


/********************************************
 *
 * print information control
 *
 *******************************************/
#define AW_LOGI(fmt, ...)\
	pr_info("[Awinic] %s:" fmt "\n", __func__, ##__VA_ARGS__)

#define AW_LOGD(fmt, ...)\
	pr_debug("[Awinic] %s:" fmt "\n", __func__, ##__VA_ARGS__)

#define AW_LOGE(fmt, ...)\
	pr_err("[Awinic] %s:" fmt "\n", __func__, ##__VA_ARGS__)


#define AW_DEV_LOGI(dev, fmt, ...)\
	pr_info("[Awinic] [%s]%s: " fmt "\n", dev_name(dev), __func__, ##__VA_ARGS__)

#define AW_DEV_LOGD(dev, fmt, ...)\
	pr_debug("[Awinic] [%s]%s: " fmt "\n", dev_name(dev), __func__, ##__VA_ARGS__)

#define AW_DEV_LOGE(dev, fmt, ...)\
	pr_err("[Awinic] [%s]%s: " fmt "\n", dev_name(dev), __func__, ##__VA_ARGS__)



#endif
