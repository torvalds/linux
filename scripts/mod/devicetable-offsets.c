// SPDX-License-Identifier: GPL-2.0
#include <linux/kbuild.h>
#include <linux/mod_devicetable.h>

#define DEVID(devid) DEFINE(SIZE_##devid, sizeof(struct devid))
#define DEVID_FIELD(devid, field) \
	DEFINE(OFF_##devid##_##field, offsetof(struct devid, field))

int main(void)
{
	DEVID(usb_device_id);
	DEVID_FIELD(usb_device_id, match_flags);
	DEVID_FIELD(usb_device_id, idVendor);
	DEVID_FIELD(usb_device_id, idProduct);
	DEVID_FIELD(usb_device_id, bcdDevice_lo);
	DEVID_FIELD(usb_device_id, bcdDevice_hi);
	DEVID_FIELD(usb_device_id, bDeviceClass);
	DEVID_FIELD(usb_device_id, bDeviceSubClass);
	DEVID_FIELD(usb_device_id, bDeviceProtocol);
	DEVID_FIELD(usb_device_id, bInterfaceClass);
	DEVID_FIELD(usb_device_id, bInterfaceSubClass);
	DEVID_FIELD(usb_device_id, bInterfaceProtocol);
	DEVID_FIELD(usb_device_id, bInterfaceNumber);

	DEVID(hid_device_id);
	DEVID_FIELD(hid_device_id, bus);
	DEVID_FIELD(hid_device_id, group);
	DEVID_FIELD(hid_device_id, vendor);
	DEVID_FIELD(hid_device_id, product);

	DEVID(ieee1394_device_id);
	DEVID_FIELD(ieee1394_device_id, match_flags);
	DEVID_FIELD(ieee1394_device_id, vendor_id);
	DEVID_FIELD(ieee1394_device_id, model_id);
	DEVID_FIELD(ieee1394_device_id, specifier_id);
	DEVID_FIELD(ieee1394_device_id, version);

	DEVID(pci_device_id);
	DEVID_FIELD(pci_device_id, vendor);
	DEVID_FIELD(pci_device_id, device);
	DEVID_FIELD(pci_device_id, subvendor);
	DEVID_FIELD(pci_device_id, subdevice);
	DEVID_FIELD(pci_device_id, class);
	DEVID_FIELD(pci_device_id, class_mask);

	DEVID(ccw_device_id);
	DEVID_FIELD(ccw_device_id, match_flags);
	DEVID_FIELD(ccw_device_id, cu_type);
	DEVID_FIELD(ccw_device_id, cu_model);
	DEVID_FIELD(ccw_device_id, dev_type);
	DEVID_FIELD(ccw_device_id, dev_model);

	DEVID(ap_device_id);
	DEVID_FIELD(ap_device_id, dev_type);

	DEVID(css_device_id);
	DEVID_FIELD(css_device_id, type);

	DEVID(serio_device_id);
	DEVID_FIELD(serio_device_id, type);
	DEVID_FIELD(serio_device_id, proto);
	DEVID_FIELD(serio_device_id, id);
	DEVID_FIELD(serio_device_id, extra);

	DEVID(acpi_device_id);
	DEVID_FIELD(acpi_device_id, id);
	DEVID_FIELD(acpi_device_id, cls);
	DEVID_FIELD(acpi_device_id, cls_msk);

	DEVID(pnp_device_id);
	DEVID_FIELD(pnp_device_id, id);

	DEVID(pnp_card_device_id);
	DEVID_FIELD(pnp_card_device_id, devs);

	DEVID(pcmcia_device_id);
	DEVID_FIELD(pcmcia_device_id, match_flags);
	DEVID_FIELD(pcmcia_device_id, manf_id);
	DEVID_FIELD(pcmcia_device_id, card_id);
	DEVID_FIELD(pcmcia_device_id, func_id);
	DEVID_FIELD(pcmcia_device_id, function);
	DEVID_FIELD(pcmcia_device_id, device_no);
	DEVID_FIELD(pcmcia_device_id, prod_id_hash);

	DEVID(of_device_id);
	DEVID_FIELD(of_device_id, name);
	DEVID_FIELD(of_device_id, type);
	DEVID_FIELD(of_device_id, compatible);

	DEVID(vio_device_id);
	DEVID_FIELD(vio_device_id, type);
	DEVID_FIELD(vio_device_id, compat);

	DEVID(input_device_id);
	DEVID_FIELD(input_device_id, flags);
	DEVID_FIELD(input_device_id, bustype);
	DEVID_FIELD(input_device_id, vendor);
	DEVID_FIELD(input_device_id, product);
	DEVID_FIELD(input_device_id, version);
	DEVID_FIELD(input_device_id, evbit);
	DEVID_FIELD(input_device_id, keybit);
	DEVID_FIELD(input_device_id, relbit);
	DEVID_FIELD(input_device_id, absbit);
	DEVID_FIELD(input_device_id, mscbit);
	DEVID_FIELD(input_device_id, ledbit);
	DEVID_FIELD(input_device_id, sndbit);
	DEVID_FIELD(input_device_id, ffbit);
	DEVID_FIELD(input_device_id, swbit);

	DEVID(eisa_device_id);
	DEVID_FIELD(eisa_device_id, sig);

	DEVID(parisc_device_id);
	DEVID_FIELD(parisc_device_id, hw_type);
	DEVID_FIELD(parisc_device_id, hversion);
	DEVID_FIELD(parisc_device_id, hversion_rev);
	DEVID_FIELD(parisc_device_id, sversion);

	DEVID(sdio_device_id);
	DEVID_FIELD(sdio_device_id, class);
	DEVID_FIELD(sdio_device_id, vendor);
	DEVID_FIELD(sdio_device_id, device);

	DEVID(ssb_device_id);
	DEVID_FIELD(ssb_device_id, vendor);
	DEVID_FIELD(ssb_device_id, coreid);
	DEVID_FIELD(ssb_device_id, revision);

	DEVID(bcma_device_id);
	DEVID_FIELD(bcma_device_id, manuf);
	DEVID_FIELD(bcma_device_id, id);
	DEVID_FIELD(bcma_device_id, rev);
	DEVID_FIELD(bcma_device_id, class);

	DEVID(virtio_device_id);
	DEVID_FIELD(virtio_device_id, device);
	DEVID_FIELD(virtio_device_id, vendor);

	DEVID(hv_vmbus_device_id);
	DEVID_FIELD(hv_vmbus_device_id, guid);

	DEVID(i2c_device_id);
	DEVID_FIELD(i2c_device_id, name);

	DEVID(spi_device_id);
	DEVID_FIELD(spi_device_id, name);

	DEVID(dmi_system_id);
	DEVID_FIELD(dmi_system_id, matches);

	DEVID(platform_device_id);
	DEVID_FIELD(platform_device_id, name);

	DEVID(mdio_device_id);
	DEVID_FIELD(mdio_device_id, phy_id);
	DEVID_FIELD(mdio_device_id, phy_id_mask);

	DEVID(zorro_device_id);
	DEVID_FIELD(zorro_device_id, id);

	DEVID(isapnp_device_id);
	DEVID_FIELD(isapnp_device_id, vendor);
	DEVID_FIELD(isapnp_device_id, function);

	DEVID(ipack_device_id);
	DEVID_FIELD(ipack_device_id, format);
	DEVID_FIELD(ipack_device_id, vendor);
	DEVID_FIELD(ipack_device_id, device);

	DEVID(amba_id);
	DEVID_FIELD(amba_id, id);
	DEVID_FIELD(amba_id, mask);

	DEVID(mips_cdmm_device_id);
	DEVID_FIELD(mips_cdmm_device_id, type);

	DEVID(x86_cpu_id);
	DEVID_FIELD(x86_cpu_id, feature);
	DEVID_FIELD(x86_cpu_id, family);
	DEVID_FIELD(x86_cpu_id, model);
	DEVID_FIELD(x86_cpu_id, vendor);

	DEVID(cpu_feature);
	DEVID_FIELD(cpu_feature, feature);

	DEVID(mei_cl_device_id);
	DEVID_FIELD(mei_cl_device_id, name);
	DEVID_FIELD(mei_cl_device_id, uuid);
	DEVID_FIELD(mei_cl_device_id, version);

	DEVID(rio_device_id);
	DEVID_FIELD(rio_device_id, did);
	DEVID_FIELD(rio_device_id, vid);
	DEVID_FIELD(rio_device_id, asm_did);
	DEVID_FIELD(rio_device_id, asm_vid);

	DEVID(ulpi_device_id);
	DEVID_FIELD(ulpi_device_id, vendor);
	DEVID_FIELD(ulpi_device_id, product);

	DEVID(hda_device_id);
	DEVID_FIELD(hda_device_id, vendor_id);
	DEVID_FIELD(hda_device_id, rev_id);
	DEVID_FIELD(hda_device_id, api_version);

	DEVID(fsl_mc_device_id);
	DEVID_FIELD(fsl_mc_device_id, vendor);
	DEVID_FIELD(fsl_mc_device_id, obj_type);

	return 0;
}
