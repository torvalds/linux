#ifndef __LINUX_CLASS_DUAL_ROLE_H__
#define __LINUX_CLASS_DUAL_ROLE_H__

#include <linux/workqueue.h>
#include <linux/errno.h>
#include <linux/types.h>

struct device;

enum dual_role_supported_modes {
	DUAL_ROLE_SUPPORTED_MODES_DFP_AND_UFP = 0,
	DUAL_ROLE_SUPPORTED_MODES_DFP,
	DUAL_ROLE_SUPPORTED_MODES_UFP,
/*The following should be the last element*/
	DUAL_ROLE_PROP_SUPPORTED_MODES_TOTAL,
};

enum {
	DUAL_ROLE_PROP_MODE_UFP = 0,
	DUAL_ROLE_PROP_MODE_DFP,
	DUAL_ROLE_PROP_MODE_NONE,
/*The following should be the last element*/
	DUAL_ROLE_PROP_MODE_TOTAL,
};

enum {
	DUAL_ROLE_PROP_PR_SRC = 0,
	DUAL_ROLE_PROP_PR_SNK,
	DUAL_ROLE_PROP_PR_NONE,
/*The following should be the last element*/
	DUAL_ROLE_PROP_PR_TOTAL,

};

enum {
	DUAL_ROLE_PROP_DR_HOST = 0,
	DUAL_ROLE_PROP_DR_DEVICE,
	DUAL_ROLE_PROP_DR_NONE,
/*The following should be the last element*/
	DUAL_ROLE_PROP_DR_TOTAL,
};

enum {
	DUAL_ROLE_PROP_VCONN_SUPPLY_NO = 0,
	DUAL_ROLE_PROP_VCONN_SUPPLY_YES,
/*The following should be the last element*/
	DUAL_ROLE_PROP_VCONN_SUPPLY_TOTAL,
};

enum dual_role_property {
	DUAL_ROLE_PROP_SUPPORTED_MODES = 0,
	DUAL_ROLE_PROP_MODE,
	DUAL_ROLE_PROP_PR,
	DUAL_ROLE_PROP_DR,
	DUAL_ROLE_PROP_VCONN_SUPPLY,
};

struct dual_role_phy_instance;

/* Description of typec port */
struct dual_role_phy_desc {
	/* /sys/class/dual_role_usb/<name>/ */
	const char *name;
	enum dual_role_supported_modes supported_modes;
	enum dual_role_property *properties;
	size_t num_properties;

	/* Callback for "cat /sys/class/dual_role_usb/<name>/<property>" */
	int (*get_property)(struct dual_role_phy_instance *dual_role,
			     enum dual_role_property prop,
			     unsigned int *val);
	/* Callback for "echo <value> >
	 *                      /sys/class/dual_role_usb/<name>/<property>" */
	int (*set_property)(struct dual_role_phy_instance *dual_role,
			     enum dual_role_property prop,
			     const unsigned int *val);
	/* Decides whether userspace can change a specific property */
	int (*property_is_writeable)(struct dual_role_phy_instance *dual_role,
				      enum dual_role_property prop);
};

struct dual_role_phy_instance {
	const struct dual_role_phy_desc *desc;

	/* Driver private data */
	void *drv_data;

	struct device dev;
	struct work_struct changed_work;
};

#if IS_ENABLED(CONFIG_DUAL_ROLE_USB_INTF)
extern void dual_role_instance_changed(struct dual_role_phy_instance
				       *dual_role);
extern struct dual_role_phy_instance *__must_check
devm_dual_role_instance_register(struct device *parent,
				 const struct dual_role_phy_desc *desc);
extern void devm_dual_role_instance_unregister(struct device *dev,
					       struct dual_role_phy_instance
					       *dual_role);
extern int dual_role_get_property(struct dual_role_phy_instance *dual_role,
				  enum dual_role_property prop,
				  unsigned int *val);
extern int dual_role_set_property(struct dual_role_phy_instance *dual_role,
				  enum dual_role_property prop,
				  const unsigned int *val);
extern int dual_role_property_is_writeable(struct dual_role_phy_instance
					   *dual_role,
					   enum dual_role_property prop);
extern void *dual_role_get_drvdata(struct dual_role_phy_instance *dual_role);
#else /* CONFIG_DUAL_ROLE_USB_INTF */
static inline void dual_role_instance_changed(struct dual_role_phy_instance
				       *dual_role){}
static inline struct dual_role_phy_instance *__must_check
devm_dual_role_instance_register(struct device *parent,
				 const struct dual_role_phy_desc *desc)
{
	return ERR_PTR(-ENOSYS);
}
static inline void devm_dual_role_instance_unregister(struct device *dev,
					       struct dual_role_phy_instance
					       *dual_role){}
static inline void *dual_role_get_drvdata(struct dual_role_phy_instance
		*dual_role)
{
	return ERR_PTR(-ENOSYS);
}
#endif /* CONFIG_DUAL_ROLE_USB_INTF */
#endif /* __LINUX_CLASS_DUAL_ROLE_H__ */
