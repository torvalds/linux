/* SPDX-License-Identifier: LGPL-2.1+ */
/* Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org> */
#ifndef __THERMAL_TOOLS_LOG_H
#define __THERMAL_TOOLS_LOG_H

#include <syslog.h>

#ifndef __maybe_unused
#define __maybe_unused		__attribute__((__unused__))
#endif

#define TO_SYSLOG 0x1
#define TO_STDOUT 0x2
#define TO_STDERR 0x4

extern void logit(int level, const char *format, ...);

#define DEBUG(fmt, ...)		logit(LOG_DEBUG, "%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define INFO(fmt, ...)		logit(LOG_INFO, fmt, ##__VA_ARGS__)
#define NOTICE(fmt, ...)	logit(LOG_NOTICE, fmt, ##__VA_ARGS__)
#define WARN(fmt, ...)		logit(LOG_WARNING, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...)		logit(LOG_ERR, fmt, ##__VA_ARGS__)
#define CRITICAL(fmt, ...)	logit(LOG_CRIT, fmt, ##__VA_ARGS__)
#define ALERT(fmt, ...)		logit(LOG_ALERT, fmt, ##__VA_ARGS__)
#define EMERG(fmt, ...)		logit(LOG_EMERG, fmt, ##__VA_ARGS__)

int log_init(int level, const char *ident, int options);
int log_str2level(const char *lvl);
void log_exit(void);

#endif
