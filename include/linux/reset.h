#ifndef _LINUX_RESET_H_
#define _LINUX_RESET_H_

struct device;
struct device_node;
struct reset_control;

#ifdef CONFIG_RESET_CONTROLLER

int reset_control_reset(struct reset_control *rstc);
int reset_control_assert(struct reset_control *rstc);
int reset_control_deassert(struct reset_control *rstc);

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

static inline void reset_control_put(struct reset_control *rstc)
{
	WARN_ON(1);
}

static inline int device_reset_optional(struct device *dev)
{
	return -ENOSYS;
}

static inline struct reset_control *reset_control_get_optional(
					struct device *dev, const char *id)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct reset_control *devm_reset_control_get_optional(
					struct device *dev, const char *id)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct reset_control *of_reset_control_get(
				struct device_node *node, const char *id)
{
	return ERR_PTR(-ENOSYS);
}

#endif /* CONFIG_RESET_CONTROLLER */

#endif
