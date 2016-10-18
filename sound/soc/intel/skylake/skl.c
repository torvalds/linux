/*
 *  skl.c - Implementation of ASoC Intel SKL HD Audio driver
 *
 *  Copyright (C) 2014-2015 Intel Corp
 *  Author: Jeeja KP <jeeja.kp@intel.com>
 *
 *  Derived mostly from Intel HDA driver with following copyrights:
 *  Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *                     PeiSen Hou <pshou@realtek.com.tw>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <sound/pcm.h>
#include "skl.h"

/*
 * initialize the PCI registers
 */
static void skl_update_pci_byte(struct pci_dev *pci, unsigned int reg,
			    unsigned char mask, unsigned char val)
{
	unsigned char data;

	pci_read_config_byte(pci, reg, &data);
	data &= ~mask;
	data |= (val & mask);
	pci_write_config_byte(pci, reg, data);
}

static void skl_init_pci(struct skl *skl)
{
	struct hdac_ext_bus *ebus = &skl->ebus;

	/*
	 * Clear bits 0-2 of PCI register TCSEL (at offset 0x44)
	 * TCSEL == Traffic Class Select Register, which sets PCI express QOS
	 * Ensuring these bits are 0 clears playback static on some HD Audio
	 * codecs.
	 * The PCI register TCSEL is defined in the Intel manuals.
	 */
	dev_dbg(ebus_to_hbus(ebus)->dev, "Clearing TCSEL\n");
	skl_update_pci_byte(skl->pci, AZX_PCIREG_TCSEL, 0x07, 0);
}

/* called from IRQ */
static void skl_stream_update(struct hdac_bus *bus, struct hdac_stream *hstr)
{
	snd_pcm_period_elapsed(hstr->substream);
}

static irqreturn_t skl_interrupt(int irq, void *dev_id)
{
	struct hdac_ext_bus *ebus = dev_id;
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	u32 status;

	if (!pm_runtime_active(bus->dev))
		return IRQ_NONE;

	spin_lock(&bus->reg_lock);

	status = snd_hdac_chip_readl(bus, INTSTS);
	if (status == 0 || status == 0xffffffff) {
		spin_unlock(&bus->reg_lock);
		return IRQ_NONE;
	}

	/* clear rirb int */
	status = snd_hdac_chip_readb(bus, RIRBSTS);
	if (status & RIRB_INT_MASK) {
		if (status & RIRB_INT_RESPONSE)
			snd_hdac_bus_update_rirb(bus);
		snd_hdac_chip_writeb(bus, RIRBSTS, RIRB_INT_MASK);
	}

	spin_unlock(&bus->reg_lock);

	return snd_hdac_chip_readl(bus, INTSTS) ? IRQ_WAKE_THREAD : IRQ_HANDLED;
}

static irqreturn_t skl_threaded_handler(int irq, void *dev_id)
{
	struct hdac_ext_bus *ebus = dev_id;
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	u32 status;

	status = snd_hdac_chip_readl(bus, INTSTS);

	snd_hdac_bus_handle_stream_irq(bus, status, skl_stream_update);

	return IRQ_HANDLED;
}

static int skl_acquire_irq(struct hdac_ext_bus *ebus, int do_disconnect)
{
	struct skl *skl = ebus_to_skl(ebus);
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	int ret;

	ret = request_threaded_irq(skl->pci->irq, skl_interrupt,
			skl_threaded_handler,
			IRQF_SHARED,
			KBUILD_MODNAME, ebus);
	if (ret) {
		dev_err(bus->dev,
			"unable to grab IRQ %d, disabling device\n",
			skl->pci->irq);
		return ret;
	}

	bus->irq = skl->pci->irq;
	pci_intx(skl->pci, 1);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * power management
 */
static int skl_suspend(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct hdac_ext_bus *ebus = pci_get_drvdata(pci);
	struct hdac_bus *bus = ebus_to_hbus(ebus);

	snd_hdac_bus_stop_chip(bus);
	snd_hdac_bus_enter_link_reset(bus);

	return 0;
}

static int skl_resume(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct hdac_ext_bus *ebus = pci_get_drvdata(pci);
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	struct skl *hda = ebus_to_skl(ebus);

	skl_init_pci(hda);

	snd_hdac_bus_init_chip(bus, 1);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int skl_runtime_suspend(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct hdac_ext_bus *ebus = pci_get_drvdata(pci);
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	struct skl *skl = ebus_to_skl(ebus);
	int ret;

	dev_dbg(bus->dev, "in %s\n", __func__);

	/* enable controller wake up event */
	snd_hdac_chip_updatew(bus, WAKEEN, 0, STATESTS_INT_MASK);

	snd_hdac_ext_bus_link_power_down_all(ebus);

	ret = skl_suspend_dsp(skl);
	if (ret < 0)
		return ret;

	snd_hdac_bus_stop_chip(bus);
	snd_hdac_bus_enter_link_reset(bus);

	return 0;
}

static int skl_runtime_resume(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct hdac_ext_bus *ebus = pci_get_drvdata(pci);
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	struct skl *skl = ebus_to_skl(ebus);
	int status;

	dev_dbg(bus->dev, "in %s\n", __func__);

	/* Read STATESTS before controller reset */
	status = snd_hdac_chip_readw(bus, STATESTS);

	skl_init_pci(skl);
	snd_hdac_bus_init_chip(bus, true);
	/* disable controller Wake Up event */
	snd_hdac_chip_updatew(bus, WAKEEN, STATESTS_INT_MASK, 0);

	return skl_resume_dsp(skl);
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops skl_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(skl_suspend, skl_resume)
	SET_RUNTIME_PM_OPS(skl_runtime_suspend, skl_runtime_resume, NULL)
};

/*
 * destructor
 */
static int skl_free(struct hdac_ext_bus *ebus)
{
	struct skl *skl  = ebus_to_skl(ebus);
	struct hdac_bus *bus = ebus_to_hbus(ebus);

	skl->init_failed = 1; /* to be sure */

	snd_hdac_ext_stop_streams(ebus);

	if (bus->irq >= 0)
		free_irq(bus->irq, (void *)bus);
	if (bus->remap_addr)
		iounmap(bus->remap_addr);

	snd_hdac_bus_free_stream_pages(bus);
	snd_hdac_stream_free_all(ebus);
	snd_hdac_link_free_all(ebus);
	pci_release_regions(skl->pci);
	pci_disable_device(skl->pci);

	snd_hdac_ext_bus_exit(ebus);

	return 0;
}

static int skl_dmic_device_register(struct skl *skl)
{
	struct hdac_bus *bus = ebus_to_hbus(&skl->ebus);
	struct platform_device *pdev;
	int ret;

	/* SKL has one dmic port, so allocate dmic device for this */
	pdev = platform_device_alloc("dmic-codec", -1);
	if (!pdev) {
		dev_err(bus->dev, "failed to allocate dmic device\n");
		return -ENOMEM;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		dev_err(bus->dev, "failed to add dmic device: %d\n", ret);
		platform_device_put(pdev);
		return ret;
	}
	skl->dmic_dev = pdev;

	return 0;
}

static void skl_dmic_device_unregister(struct skl *skl)
{
	if (skl->dmic_dev)
		platform_device_unregister(skl->dmic_dev);
}

/*
 * Probe the given codec address
 */
static int probe_codec(struct hdac_ext_bus *ebus, int addr)
{
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	unsigned int cmd = (addr << 28) | (AC_NODE_ROOT << 20) |
		(AC_VERB_PARAMETERS << 8) | AC_PAR_VENDOR_ID;
	unsigned int res;

	mutex_lock(&bus->cmd_mutex);
	snd_hdac_bus_send_cmd(bus, cmd);
	snd_hdac_bus_get_response(bus, addr, &res);
	mutex_unlock(&bus->cmd_mutex);
	if (res == -1)
		return -EIO;
	dev_dbg(bus->dev, "codec #%d probed OK\n", addr);

	return snd_hdac_ext_bus_device_init(ebus, addr);
}

/* Codec initialization */
static int skl_codec_create(struct hdac_ext_bus *ebus)
{
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	int c, max_slots;

	max_slots = HDA_MAX_CODECS;

	/* First try to probe all given codec slots */
	for (c = 0; c < max_slots; c++) {
		if ((bus->codec_mask & (1 << c))) {
			if (probe_codec(ebus, c) < 0) {
				/*
				 * Some BIOSen give you wrong codec addresses
				 * that don't exist
				 */
				dev_warn(bus->dev,
					 "Codec #%d probe error; disabling it...\n", c);
				bus->codec_mask &= ~(1 << c);
				/*
				 * More badly, accessing to a non-existing
				 * codec often screws up the controller bus,
				 * and disturbs the further communications.
				 * Thus if an error occurs during probing,
				 * better to reset the controller bus to get
				 * back to the sanity state.
				 */
				snd_hdac_bus_stop_chip(bus);
				snd_hdac_bus_init_chip(bus, true);
			}
		}
	}

	return 0;
}

static const struct hdac_bus_ops bus_core_ops = {
	.command = snd_hdac_bus_send_cmd,
	.get_response = snd_hdac_bus_get_response,
};

/*
 * constructor
 */
static int skl_create(struct pci_dev *pci,
		      const struct hdac_io_ops *io_ops,
		      struct skl **rskl)
{
	struct skl *skl;
	struct hdac_ext_bus *ebus;

	int err;

	*rskl = NULL;

	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	skl = devm_kzalloc(&pci->dev, sizeof(*skl), GFP_KERNEL);
	if (!skl) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	ebus = &skl->ebus;
	snd_hdac_ext_bus_init(ebus, &pci->dev, &bus_core_ops, io_ops);
	ebus->bus.use_posbuf = 1;
	skl->pci = pci;

	ebus->bus.bdl_pos_adj = 0;

	*rskl = skl;

	return 0;
}

static int skl_first_init(struct hdac_ext_bus *ebus)
{
	struct skl *skl = ebus_to_skl(ebus);
	struct hdac_bus *bus = ebus_to_hbus(ebus);
	struct pci_dev *pci = skl->pci;
	int err;
	unsigned short gcap;
	int cp_streams, pb_streams, start_idx;

	err = pci_request_regions(pci, "Skylake HD audio");
	if (err < 0)
		return err;

	bus->addr = pci_resource_start(pci, 0);
	bus->remap_addr = pci_ioremap_bar(pci, 0);
	if (bus->remap_addr == NULL) {
		dev_err(bus->dev, "ioremap error\n");
		return -ENXIO;
	}

	snd_hdac_ext_bus_parse_capabilities(ebus);

	if (skl_acquire_irq(ebus, 0) < 0)
		return -EBUSY;

	pci_set_master(pci);
	synchronize_irq(bus->irq);

	gcap = snd_hdac_chip_readw(bus, GCAP);
	dev_dbg(bus->dev, "chipset global capabilities = 0x%x\n", gcap);

	/* allow 64bit DMA address if supported by H/W */
	if (!dma_set_mask(bus->dev, DMA_BIT_MASK(64))) {
		dma_set_coherent_mask(bus->dev, DMA_BIT_MASK(64));
	} else {
		dma_set_mask(bus->dev, DMA_BIT_MASK(32));
		dma_set_coherent_mask(bus->dev, DMA_BIT_MASK(32));
	}

	/* read number of streams from GCAP register */
	cp_streams = (gcap >> 8) & 0x0f;
	pb_streams = (gcap >> 12) & 0x0f;

	if (!pb_streams && !cp_streams)
		return -EIO;

	ebus->num_streams = cp_streams + pb_streams;

	/* initialize streams */
	snd_hdac_ext_stream_init_all
		(ebus, 0, cp_streams, SNDRV_PCM_STREAM_CAPTURE);
	start_idx = cp_streams;
	snd_hdac_ext_stream_init_all
		(ebus, start_idx, pb_streams, SNDRV_PCM_STREAM_PLAYBACK);

	err = snd_hdac_bus_alloc_stream_pages(bus);
	if (err < 0)
		return err;

	/* initialize chip */
	skl_init_pci(skl);

	snd_hdac_bus_init_chip(bus, true);

	/* codec detection */
	if (!bus->codec_mask) {
		dev_err(bus->dev, "no codecs found!\n");
		return -ENODEV;
	}

	return 0;
}

static int skl_probe(struct pci_dev *pci,
		     const struct pci_device_id *pci_id)
{
	struct skl *skl;
	struct hdac_ext_bus *ebus = NULL;
	struct hdac_bus *bus = NULL;
	int err;

	/* we use ext core ops, so provide NULL for ops here */
	err = skl_create(pci, NULL, &skl);
	if (err < 0)
		return err;

	ebus = &skl->ebus;
	bus = ebus_to_hbus(ebus);

	err = skl_first_init(ebus);
	if (err < 0)
		goto out_free;

	skl->nhlt = skl_nhlt_init(bus->dev);

	if (skl->nhlt == NULL) {
		err = -ENODEV;
		goto out_free;
	}

	pci_set_drvdata(skl->pci, ebus);

	/* check if dsp is there */
	if (ebus->ppcap) {
		err = skl_init_dsp(skl);
		if (err < 0) {
			dev_dbg(bus->dev, "error failed to register dsp\n");
			goto out_free;
		}
	}
	if (ebus->mlcap)
		snd_hdac_ext_bus_get_ml_capabilities(ebus);

	/* create device for soc dmic */
	err = skl_dmic_device_register(skl);
	if (err < 0)
		goto out_dsp_free;

	/* register platform dai and controls */
	err = skl_platform_register(bus->dev);
	if (err < 0)
		goto out_dmic_free;

	/* create codec instances */
	err = skl_codec_create(ebus);
	if (err < 0)
		goto out_unregister;

	/*configure PM */
	pm_runtime_set_autosuspend_delay(bus->dev, SKL_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(bus->dev);
	pm_runtime_put_noidle(bus->dev);
	pm_runtime_allow(bus->dev);

	return 0;

out_unregister:
	skl_platform_unregister(bus->dev);
out_dmic_free:
	skl_dmic_device_unregister(skl);
out_dsp_free:
	skl_free_dsp(skl);
out_free:
	skl->init_failed = 1;
	skl_free(ebus);

	return err;
}

static void skl_remove(struct pci_dev *pci)
{
	struct hdac_ext_bus *ebus = pci_get_drvdata(pci);
	struct skl *skl = ebus_to_skl(ebus);

	if (skl->tplg)
		release_firmware(skl->tplg);

	if (pci_dev_run_wake(pci))
		pm_runtime_get_noresume(&pci->dev);
	pci_dev_put(pci);
	skl_platform_unregister(&pci->dev);
	skl_free_dsp(skl);
	skl_dmic_device_unregister(skl);
	skl_free(ebus);
	dev_set_drvdata(&pci->dev, NULL);
}

/* PCI IDs */
static const struct pci_device_id skl_ids[] = {
	/* Sunrise Point-LP */
	{ PCI_DEVICE(0x8086, 0x9d70), 0},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, skl_ids);

/* pci_driver definition */
static struct pci_driver skl_driver = {
	.name = KBUILD_MODNAME,
	.id_table = skl_ids,
	.probe = skl_probe,
	.remove = skl_remove,
	.driver = {
		.pm = &skl_pm,
	},
};
module_pci_driver(skl_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Skylake ASoC HDA driver");
