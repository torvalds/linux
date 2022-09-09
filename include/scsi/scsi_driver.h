/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCSI_SCSI_DRIVER_H
#define _SCSI_SCSI_DRIVER_H

#include <linux/blk_types.h>
#include <linux/device.h>
#include <scsi/scsi_cmnd.h>

struct module;
struct request;

struct scsi_driver {
	struct device_driver	gendrv;

	void (*rescan)(struct device *);
	blk_status_t (*init_command)(struct scsi_cmnd *);
	void (*uninit_command)(struct scsi_cmnd *);
	int (*done)(struct scsi_cmnd *);
	int (*eh_action)(struct scsi_cmnd *, int);
	void (*eh_reset)(struct scsi_cmnd *);
};
#define to_scsi_driver(drv) \
	container_of((drv), struct scsi_driver, gendrv)

extern int scsi_register_driver(struct device_driver *);
#define scsi_unregister_driver(drv) \
	driver_unregister(drv);

extern int scsi_register_interface(struct class_interface *);
#define scsi_unregister_interface(intf) \
	class_interface_unregister(intf)

/* make sure not to use it with passthrough commands */
static inline struct scsi_driver *scsi_cmd_to_driver(struct scsi_cmnd *cmd)
{
	return to_scsi_driver(cmd->device->sdev_gendev.driver);
}

#endif /* _SCSI_SCSI_DRIVER_H */
