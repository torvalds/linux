/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TYPEC_ALTMODE_H
#define __USB_TYPEC_ALTMODE_H

#include <linux/mod_devicetable.h>
#include <linux/usb/typec.h>
#include <linux/device.h>

#define MODE_DISCOVERY_MAX	6

struct typec_altmode_ops;

/**
 * struct typec_altmode - USB Type-C alternate mode device
 * @dev: Driver model's view of this device
 * @svid: Standard or Vendor ID (SVID) of the alternate mode
 * @mode: Index of the Mode
 * @vdo: VDO returned by Discover Modes USB PD command
 * @active: Tells has the mode been entered or not
 * @desc: Optional human readable description of the mode
 * @ops: Operations vector from the driver
 */
struct typec_altmode {
	struct device			dev;
	u16				svid;
	int				mode;
	u32				vdo;
	unsigned int			active:1;

	char				*desc;
	const struct typec_altmode_ops	*ops;
};

#define to_typec_altmode(d) container_of(d, struct typec_altmode, dev)

static inline void typec_altmode_set_drvdata(struct typec_altmode *altmode,
					     void *data)
{
	dev_set_drvdata(&altmode->dev, data);
}

static inline void *typec_altmode_get_drvdata(struct typec_altmode *altmode)
{
	return dev_get_drvdata(&altmode->dev);
}

/**
 * struct typec_altmode_ops - Alternate mode specific operations vector
 * @enter: Operations to be executed with Enter Mode Command
 * @exit: Operations to be executed with Exit Mode Command
 * @attention: Callback for Attention Command
 * @vdm: Callback for SVID specific commands
 * @notify: Communication channel for platform and the alternate mode
 * @activate: User callback for Enter/Exit Mode
 */
struct typec_altmode_ops {
	int (*enter)(struct typec_altmode *altmode);
	int (*exit)(struct typec_altmode *altmode);
	void (*attention)(struct typec_altmode *altmode, u32 vdo);
	int (*vdm)(struct typec_altmode *altmode, const u32 hdr,
		   const u32 *vdo, int cnt);
	int (*notify)(struct typec_altmode *altmode, unsigned long conf,
		      void *data);
	int (*activate)(struct typec_altmode *altmode, int activate);
};

int typec_altmode_enter(struct typec_altmode *altmode);
int typec_altmode_exit(struct typec_altmode *altmode);
void typec_altmode_attention(struct typec_altmode *altmode, u32 vdo);
int typec_altmode_vdm(struct typec_altmode *altmode,
		      const u32 header, const u32 *vdo, int count);
int typec_altmode_notify(struct typec_altmode *altmode, unsigned long conf,
			 void *data);
const struct typec_altmode *
typec_altmode_get_partner(struct typec_altmode *altmode);

/*
 * These are the connector states (USB, Safe and Alt Mode) defined in USB Type-C
 * Specification. SVID specific connector states are expected to follow and
 * start from the value TYPEC_STATE_MODAL.
 */
enum {
	TYPEC_STATE_SAFE,	/* USB Safe State */
	TYPEC_STATE_USB,	/* USB Operation */
	TYPEC_STATE_MODAL,	/* Alternate Modes */
};

/*
 * For the muxes there is no difference between Accessory Modes and Alternate
 * Modes, so the Accessory Modes are supplied with specific modal state values
 * here. Unlike with Alternate Modes, where the mux will be linked with the
 * alternate mode device, the mux for Accessory Modes will be linked with the
 * port device instead.
 *
 * Port drivers can use TYPEC_MODE_AUDIO and TYPEC_MODE_DEBUG as the mode
 * value for typec_set_mode() when accessory modes are supported.
 */
enum {
	TYPEC_MODE_AUDIO = TYPEC_STATE_MODAL,	/* Audio Accessory */
	TYPEC_MODE_DEBUG,			/* Debug Accessory */
};

#define TYPEC_MODAL_STATE(_state_)	((_state_) + TYPEC_STATE_MODAL)

struct typec_altmode *typec_altmode_get_plug(struct typec_altmode *altmode,
					     enum typec_plug_index index);
void typec_altmode_put_plug(struct typec_altmode *plug);

struct typec_altmode *typec_match_altmode(struct typec_altmode **altmodes,
					  size_t n, u16 svid, u8 mode);

struct typec_altmode *
typec_altmode_register_notifier(struct device *dev, u16 svid, u8 mode,
				struct notifier_block *nb);

void typec_altmode_unregister_notifier(struct typec_altmode *adev,
				       struct notifier_block *nb);

/**
 * typec_altmode_get_orientation - Get cable plug orientation
 * altmode: Handle to the alternate mode
 */
static inline enum typec_orientation
typec_altmode_get_orientation(struct typec_altmode *altmode)
{
	return typec_get_orientation(typec_altmode2port(altmode));
}

/**
 * struct typec_altmode_driver - USB Type-C alternate mode device driver
 * @id_table: Null terminated array of SVIDs
 * @probe: Callback for device binding
 * @remove: Callback for device unbinding
 * @driver: Device driver model driver
 *
 * These drivers will be bind to the partner alternate mode devices. They will
 * handle all SVID specific communication.
 */
struct typec_altmode_driver {
	const struct typec_device_id *id_table;
	int (*probe)(struct typec_altmode *altmode);
	void (*remove)(struct typec_altmode *altmode);
	struct device_driver driver;
};

#define to_altmode_driver(d) container_of(d, struct typec_altmode_driver, \
					  driver)

#define typec_altmode_register_driver(drv) \
		__typec_altmode_register_driver(drv, THIS_MODULE)
int __typec_altmode_register_driver(struct typec_altmode_driver *drv,
				    struct module *module);
void typec_altmode_unregister_driver(struct typec_altmode_driver *drv);

#define module_typec_altmode_driver(__typec_altmode_driver) \
	module_driver(__typec_altmode_driver, typec_altmode_register_driver, \
		      typec_altmode_unregister_driver)

#endif /* __USB_TYPEC_ALTMODE_H */
