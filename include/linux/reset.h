#ifndef _LINUX_RESET_H_
#define _LINUX_RESET_H_

struct device;
struct device_node;
struct reset_control;

#ifdef CONFIG_RESET_CONTROLLER

int reset_control_reset(struct reset_control *rstc);
int reset_control_assert(struct reset_control *rstc);
int reset_control_deassert(struct reset_control *rstc);
int reset_control_status(struct reset_control *rstc);

struct reset_control *reset_control_get(struct device *dev, const char *id);
void reset_control_put(struct reset_control *rstc);
struct reset_control *devm_reset_control_get(struct device *dev, const char *id);

int __must_check device_reset(struct device *dev);

static inline int device_reset_optional(struct device *dev)
{
	return device_reset(dev);
}

static inline struct reset_control *reset_control_get_optional(
					struct device *dev, const char *id)
{
	return reset_control_get(dev, id);
}

static inline struct reset_control *devm_reset_control_get_optional(
					struct device *dev, const char *id)
{
	return devm_reset_control_get(dev, id);
}

struct reset_control *of_reset_control_get(struct device_node *node,
					   const char *id);

struct reset_control *of_reset_control_get_by_index(
					struct device_node *node, int index);

#else

static inline int reset_control_reset(struct reset_control *rstc)
{
	WARN_ON(1);
	return 0;
}

static inline int reset_control_assert(struct reset_control *rstc)
{
	WARN_ON(1);
	return 0;
}

static inline int reset_control_deassert(struct reset_control *rstc)
{
	WARN_ON(1);
	return 0;
}

static inline int reset_control_status(struct reset_control *rstc)
{
	WARN_ON(1);
	return 0;
}

static inline void reset_control_put(struct reset_control *rstc)
{
	WARN_ON(1);
}

static inline int device_reset_optional(struct device *dev)
{
	return -ENOTSUPP;
}

static inline struct reset_control *__must_check reset_control_get(
					struct device *dev, const char *id)
{
	WARN_ON(1);
	return ERR_PTR(-EINVAL);
}

static inline struct reset_control *__must_check devm_reset_control_get(
					struct device *dev, const char *id)
{
	WARN_ON(1);
	return ERR_PTR(-EINVAL);
}

static inline struct reset_control *reset_control_get_optional(
					struct device *dev, const char *id)
{
	return ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *devm_reset_control_get_optional(
					struct device *dev, const char *id)
{
	return ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *of_reset_control_get(
				struct device_node *node, const char *id)
{
	return ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *of_reset_control_get_by_index(
				struct device_node *node, int index)
{
	return ERR_PTR(-ENOTSUPP);
}

#endif /* CONFIG_RESET_CONTROLLER */

#endif
