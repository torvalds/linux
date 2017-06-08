#ifndef __QCOM_SMEM_STATE__
#define __QCOM_SMEM_STATE__

#include <linux/err.h>

struct device_node;
struct qcom_smem_state;

struct qcom_smem_state_ops {
	int (*update_bits)(void *, u32, u32);
};

#ifdef CONFIG_QCOM_SMEM_STATE

struct qcom_smem_state *qcom_smem_state_get(struct device *dev, const char *con_id, unsigned *bit);
void qcom_smem_state_put(struct qcom_smem_state *);

int qcom_smem_state_update_bits(struct qcom_smem_state *state, u32 mask, u32 value);

struct qcom_smem_state *qcom_smem_state_register(struct device_node *of_node, const struct qcom_smem_state_ops *ops, void *data);
void qcom_smem_state_unregister(struct qcom_smem_state *state);

#else

static inline struct qcom_smem_state *qcom_smem_state_get(struct device *dev,
	const char *con_id, unsigned *bit)
{
	return ERR_PTR(-EINVAL);
}

static inline void qcom_smem_state_put(struct qcom_smem_state *state)
{
}

static inline int qcom_smem_state_update_bits(struct qcom_smem_state *state,
	u32 mask, u32 value)
{
	return -EINVAL;
}

static inline struct qcom_smem_state *qcom_smem_state_register(struct device_node *of_node,
	const struct qcom_smem_state_ops *ops, void *data)
{
	return ERR_PTR(-EINVAL);
}

static inline void qcom_smem_state_unregister(struct qcom_smem_state *state)
{
}

#endif

#endif
