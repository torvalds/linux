/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (C) 2019 Linaro Ltd. */

#ifndef __QCOM_Q6V5_IPA_NOTIFY_H__
#define __QCOM_Q6V5_IPA_NOTIFY_H__

#if IS_ENABLED(CONFIG_QCOM_Q6V5_IPA_NOTIFY)

#include <linux/remoteproc.h>

enum qcom_rproc_event {
	MODEM_STARTING	= 0,	/* Modem is about to be started */
	MODEM_RUNNING	= 1,	/* Startup complete; modem is operational */
	MODEM_STOPPING	= 2,	/* Modem is about to shut down */
	MODEM_CRASHED	= 3,	/* Modem has crashed (implies stopping) */
	MODEM_OFFLINE	= 4,	/* Modem is now offline */
	MODEM_REMOVING	= 5,	/* Modem is about to be removed */
};

typedef void (*qcom_ipa_notify_t)(void *data, enum qcom_rproc_event event);

struct qcom_rproc_ipa_notify {
	struct rproc_subdev subdev;

	qcom_ipa_notify_t notify;
	void *data;
};

/**
 * qcom_add_ipa_notify_subdev() - Register IPA notification subdevice
 * @rproc:	rproc handle
 * @ipa_notify:	IPA notification subdevice handle
 *
 * Register the @ipa_notify subdevice with the @rproc so modem events
 * can be sent to IPA when they occur.
 *
 * This is defined in "qcom_q6v5_ipa_notify.c".
 */
void qcom_add_ipa_notify_subdev(struct rproc *rproc,
		struct qcom_rproc_ipa_notify *ipa_notify);

/**
 * qcom_remove_ipa_notify_subdev() - Remove IPA SSR subdevice
 * @rproc:	rproc handle
 * @ipa_notify:	IPA notification subdevice handle
 *
 * This is defined in "qcom_q6v5_ipa_notify.c".
 */
void qcom_remove_ipa_notify_subdev(struct rproc *rproc,
		struct qcom_rproc_ipa_notify *ipa_notify);

/**
 * qcom_register_ipa_notify() - Register IPA notification function
 * @rproc:	Remote processor handle
 * @notify:	Non-null IPA notification callback function pointer
 * @data:	Data supplied to IPA notification callback function
 *
 * @Return: 0 if successful, or a negative error code otherwise
 *
 * This is defined in "qcom_q6v5_mss.c".
 */
int qcom_register_ipa_notify(struct rproc *rproc, qcom_ipa_notify_t notify,
			     void *data);
/**
 * qcom_deregister_ipa_notify() - Deregister IPA notification function
 * @rproc:	Remote processor handle
 *
 * This is defined in "qcom_q6v5_mss.c".
 */
void qcom_deregister_ipa_notify(struct rproc *rproc);

#else /* !IS_ENABLED(CONFIG_QCOM_Q6V5_IPA_NOTIFY) */

struct qcom_rproc_ipa_notify { /* empty */ };

#define qcom_add_ipa_notify_subdev(rproc, ipa_notify)		/* no-op */
#define qcom_remove_ipa_notify_subdev(rproc, ipa_notify)	/* no-op */

#endif /* !IS_ENABLED(CONFIG_QCOM_Q6V5_IPA_NOTIFY) */

#endif /* !__QCOM_Q6V5_IPA_NOTIFY_H__ */
