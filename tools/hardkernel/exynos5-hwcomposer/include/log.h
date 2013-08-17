#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>

#if __cplusplus
extern "C" {
#endif

#if 1
#define ALOGD(format, ...) \
	do { \
		fprintf(stderr, "%s,%d -- " format "\n", \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); \
	} while(0)

#define ALOGV	ALOGD
#define ALOGI	ALOGD
#define ALOGE	ALOGD
#define ALOGW	ALOGD
#define ALOGF	ALOGD
#else
#define ALOGD
#define ALOGV
#define ALOGI
#define ALOGE
#define ALOGW
#endif

#if __cplusplus
};
#endif

#endif
