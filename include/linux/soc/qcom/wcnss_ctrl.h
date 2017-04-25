#ifndef __WCNSS_CTRL_H__
#define __WCNSS_CTRL_H__

#include <linux/soc/qcom/smd.h>

#if IS_ENABLED(CONFIG_QCOM_WCNSS_CTRL)

struct qcom_smd_channel *qcom_wcnss_open_channel(void *wcnss, const char *name, qcom_smd_cb_t cb);

#else

static inline struct qcom_smd_channel*
qcom_wcnss_open_channel(void *wcnss, const char *name, qcom_smd_cb_t cb)
{
	WARN_ON(1);
	return ERR_PTR(-ENXIO);
}

#endif

#endif
