#ifndef __QCOM_RPM_H__
#define __QCOM_RPM_H__

#include <linux/types.h>

struct qcom_rpm;

#define QCOM_RPM_ACTIVE_STATE	0
#define QCOM_RPM_SLEEP_STATE	1

int qcom_rpm_write(struct qcom_rpm *rpm, int state, int resource, u32 *buf, size_t count);

#endif
