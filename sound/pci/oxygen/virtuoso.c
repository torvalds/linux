/*
 * C-Media CMI8788 driver for Asus Xonar cards
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this driver; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include "xonar.h"

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("Asus Virtuoso driver");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{{Asus,AV66},{Asus,AV100},{Asus,AV200}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "card index");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "enable card");

static const struct pci_device_id xonar_ids[] = {
	{ OXYGEN_PCI_SUBID(0x1043, 0x8269) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x8275) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x82b7) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x8314) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x8327) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x834f) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x835c) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x835d) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x835e) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x838e) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x8428) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x8522) },
	{ OXYGEN_PCI_SUBID(0x1043, 0x85f4) },
	{ OXYGEN_PCI_SUBID_BROKEN_EEPROM },
	{ }
};
MODULE_DEVICE_TABLE(pci, xonar_ids);

static int get_xonar_model(struct oxygen *chip,
			   const struct pci_device_id *id)
{
	if (get_xonar_pcm179x_model(chip, id) >= 0)
		return 0;
	if (get_xonar_cs43xx_model(chip, id) >= 0)
		return 0;
	if (get_xonar_wm87x6_model(chip, id) >= 0)
		return 0;
	return -EINVAL;
}

static int xonar_probe(struct pci_dev *pci,
		       const struct pci_device_id *pci_id)
{
	static int dev;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		++dev;
		return -ENOENT;
	}
	err = oxygen_pci_probe(pci, index[dev], id[dev], THIS_MODULE,
			       xonar_ids, get_xonar_model);
	if (err >= 0)
		++dev;
	return err;
}

static struct pci_driver xonar_driver = {
	.name = KBUILD_MODNAME,
	.id_table = xonar_ids,
	.probe = xonar_probe,
	.remove = oxygen_pci_remove,
#ifdef CONFIG_PM_SLEEP
	.driver = {
		.pm = &oxygen_pci_pm,
	},
#endif
	.shutdown = oxygen_pci_shutdown,
};

module_pci_driver(xonar_driver);
