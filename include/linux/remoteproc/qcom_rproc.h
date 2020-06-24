#ifndef __QCOM_RPROC_H__
#define __QCOM_RPROC_H__

struct notifier_block;

#if IS_ENABLED(CONFIG_QCOM_RPROC_COMMON)

struct qcom_ssr_notify_data {
	const char *name;
	bool crashed;
};

void *qcom_register_ssr_notifier(const char *name, struct notifier_block *nb);
int qcom_unregister_ssr_notifier(void *notify, struct notifier_block *nb);

#else

static inline void *qcom_register_ssr_notifier(const char *name,
					       struct notifier_block *nb)
{
	return NULL;
}

static inline int qcom_unregister_ssr_notifier(void *notify,
					       struct notifier_block *nb)
{
	return 0;
}

#endif

#endif
