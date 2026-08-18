/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Device tables which are exported to userspace via
 * scripts/mod/file2alias.c.  You must keep that file in sync with this
 * header.
 */

#ifndef LINUX_MOD_DEVICETABLE_H
#define LINUX_MOD_DEVICETABLE_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#include "device-id/acpi.h"
#include "device-id/amba.h"
#include "device-id/ap.h"
#include "device-id/apr.h"
#include "device-id/auxiliary.h"
#include "device-id/bcma.h"
#include "device-id/ccw.h"
#include "device-id/cdx.h"
#include "device-id/coreboot.h"
#include "device-id/css.h"
#include "device-id/dfl.h"
#include "device-id/dmi.h"
#include "device-id/eisa.h"
#include "device-id/fsl_mc.h"
#include "device-id/hda.h"
#include "device-id/hid.h"
#include "device-id/hv_vmbus.h"
#include "device-id/i2c.h"
#include "device-id/i3c.h"
#include "device-id/ieee1394.h"
#include "device-id/input.h"
#include "device-id/ipack.h"
#include "device-id/isapnp.h"
#include "device-id/ishtp.h"
#include "device-id/mcb.h"
#include "device-id/mdio.h"
#include "device-id/mei_cl.h"
#include "device-id/mhi.h"
#include "device-id/mips_cdmm.h"
#include "device-id/of.h"
#include "device-id/parisc.h"
#include "device-id/pci.h"
#include "device-id/pcmcia.h"
#include "device-id/platform.h"
#include "device-id/pnp.h"
#include "device-id/rio.h"
#include "device-id/rpmsg.h"
#include "device-id/sdio.h"
#include "device-id/sdw.h"
#include "device-id/serio.h"
#include "device-id/slim.h"
#include "device-id/spi.h"
#include "device-id/spmi.h"
#include "device-id/ssam.h"
#include "device-id/ssb.h"
#include "device-id/tb.h"
#include "device-id/tee_client.h"
#include "device-id/typec.h"
#include "device-id/ulpi.h"
#include "device-id/usb.h"
#include "device-id/vchiq.h"
#include "device-id/vio.h"
#include "device-id/virtio.h"
#include "device-id/wmi.h"
#include "device-id/x86_cpu.h"
#include "device-id/zorro.h"

/*
 * Generic table type for matching CPU features.
 * @feature:	the bit number of the feature (0 - 65535)
 */

struct cpu_feature {
	__u16	feature;
};

#endif /* LINUX_MOD_DEVICETABLE_H */
