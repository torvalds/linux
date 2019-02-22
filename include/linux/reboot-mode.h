/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __REBOOT_MODE_H__
#define __REBOOT_MODE_H__

struct reboot_mode_driver {
	struct device *dev;
	struct list_head head;
	int (*write)(struct reboot_mode_driver *reboot, unsigned int magic);
	int (*read)(struct reboot_mode_driver *reboot);
	struct notifier_block reboot_notifier;
	struct notifier_block panic_notifier;
};

int reboot_mode_register(struct reboot_mode_driver *reboot);
int reboot_mode_unregister(struct reboot_mode_driver *reboot);
int devm_reboot_mode_register(struct device *dev,
			      struct reboot_mode_driver *reboot);
void devm_reboot_mode_unregister(struct device *dev,
				 struct reboot_mode_driver *reboot);

#endif
