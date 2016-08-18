#ifndef __WCNSS_CTRL_H__
#define __WCNSS_CTRL_H__

#include <linux/soc/qcom/smd.h>

struct qcom_smd_channel *qcom_wcnss_open_channel(void *wcnss, const char *name, qcom_smd_cb_t cb);

#endif
