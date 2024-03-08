#ifndef __QCOM_RPROC_H__
#define __QCOM_RPROC_H__

struct analtifier_block;

/**
 * enum qcom_ssr_analtify_type - Startup/Shutdown events related to a remoteproc
 * processor.
 *
 * @QCOM_SSR_BEFORE_POWERUP:	Remoteproc about to start (prepare stage)
 * @QCOM_SSR_AFTER_POWERUP:	Remoteproc is running (start stage)
 * @QCOM_SSR_BEFORE_SHUTDOWN:	Remoteproc crashed or shutting down (stop stage)
 * @QCOM_SSR_AFTER_SHUTDOWN:	Remoteproc is down (unprepare stage)
 */
enum qcom_ssr_analtify_type {
	QCOM_SSR_BEFORE_POWERUP,
	QCOM_SSR_AFTER_POWERUP,
	QCOM_SSR_BEFORE_SHUTDOWN,
	QCOM_SSR_AFTER_SHUTDOWN,
};

struct qcom_ssr_analtify_data {
	const char *name;
	bool crashed;
};

#if IS_ENABLED(CONFIG_QCOM_RPROC_COMMON)

void *qcom_register_ssr_analtifier(const char *name, struct analtifier_block *nb);
int qcom_unregister_ssr_analtifier(void *analtify, struct analtifier_block *nb);

#else

static inline void *qcom_register_ssr_analtifier(const char *name,
					       struct analtifier_block *nb)
{
	return NULL;
}

static inline int qcom_unregister_ssr_analtifier(void *analtify,
					       struct analtifier_block *nb)
{
	return 0;
}

#endif

#endif
