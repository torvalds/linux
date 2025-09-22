/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */

#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <drm/drm_device.h>

#include "atom.h"
#include "radeon.h"
#include "radeon_reg.h"

#if defined(__amd64__) || defined(__i386__)
#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#endif

#if defined (__loongson__)
#include <machine/autoconf.h>
#endif

/*
 * BIOS.
 */

/* If you boot an IGP board with a discrete card as the primary,
 * the IGP rom is not accessible via the rom bar as the IGP rom is
 * part of the system bios.  On boot, the system bios puts a
 * copy of the igp rom at the start of vram if a discrete card is
 * present.
 */
#ifdef __linux__
static bool igp_read_bios_from_vram(struct radeon_device *rdev)
{
	uint8_t __iomem *bios;
	resource_size_t vram_base;
	resource_size_t size = 256 * 1024; /* ??? */

	if (!(rdev->flags & RADEON_IS_IGP))
		if (!radeon_card_posted(rdev))
			return false;

	rdev->bios = NULL;
	vram_base = pci_resource_start(rdev->pdev, 0);
	bios = ioremap(vram_base, size);
	if (!bios) {
		return false;
	}

	if (size == 0 || bios[0] != 0x55 || bios[1] != 0xaa) {
		iounmap(bios);
		return false;
	}
	rdev->bios = kmalloc(size, GFP_KERNEL);
	if (rdev->bios == NULL) {
		iounmap(bios);
		return false;
	}
	memcpy_fromio(rdev->bios, bios, size);
	iounmap(bios);
	return true;
}
#else
static bool igp_read_bios_from_vram(struct radeon_device *rdev)
{
	bus_size_t size = 256 * 1024; /* ??? */
	bus_space_handle_t bsh;
	bus_space_tag_t bst = rdev->memt;
	
	if (!(rdev->flags & RADEON_IS_IGP))
		if (!radeon_card_posted(rdev))
			return false;

	rdev->bios = NULL;

	if (bus_space_map(bst, rdev->fb_aper_offset, size, 0, &bsh) != 0)
		return false;

	rdev->bios = kmalloc(size, GFP_KERNEL);
	bus_space_read_region_1(rdev->memt, bsh, 0, rdev->bios, size);
	bus_space_unmap(bst, bsh, size);

	if (size == 0 || rdev->bios[0] != 0x55 || rdev->bios[1] != 0xaa) {
		kfree(rdev->bios);
		rdev->bios = NULL;
		return false;
	}

	return true;
}
#endif

#ifdef __linux__
static bool radeon_read_bios(struct radeon_device *rdev)
{
	uint8_t __iomem *bios, val1, val2;
	size_t size;

	rdev->bios = NULL;
	/* XXX: some cards may return 0 for rom size? ddx has a workaround */
	bios = pci_map_rom(rdev->pdev, &size);
	if (!bios) {
		return false;
	}

	val1 = readb(&bios[0]);
	val2 = readb(&bios[1]);

	if (size == 0 || val1 != 0x55 || val2 != 0xaa) {
		pci_unmap_rom(rdev->pdev, bios);
		return false;
	}
	rdev->bios = kzalloc(size, GFP_KERNEL);
	if (rdev->bios == NULL) {
		pci_unmap_rom(rdev->pdev, bios);
		return false;
	}
	memcpy_fromio(rdev->bios, bios, size);
	pci_unmap_rom(rdev->pdev, bios);
	return true;
}
#else
static bool radeon_read_bios(struct radeon_device *rdev)
{
	bus_size_t size;
	pcireg_t address, mask;
	bus_space_handle_t romh;
	int rc;

	rdev->bios = NULL;
	/* XXX: some cards may return 0 for rom size? ddx has a workaround */

	address = pci_conf_read(rdev->pc, rdev->pa_tag, PCI_ROM_REG);
	pci_conf_write(rdev->pc, rdev->pa_tag, PCI_ROM_REG, ~PCI_ROM_ENABLE);
	mask = pci_conf_read(rdev->pc, rdev->pa_tag, PCI_ROM_REG);
	address |= PCI_ROM_ENABLE;
	pci_conf_write(rdev->pc, rdev->pa_tag, PCI_ROM_REG, address);

	size = PCI_ROM_SIZE(mask);
	if (size == 0)
		return false;
	rc = bus_space_map(rdev->memt, PCI_ROM_ADDR(address), size, 0, &romh);
	if (rc != 0) {
		printf(": can't map PCI ROM (%d)\n", rc);
		return false;
	}
	
	rdev->bios = kmalloc(size, GFP_KERNEL);
	bus_space_read_region_1(rdev->memt, romh, 0, rdev->bios, size);
	bus_space_unmap(rdev->memt, romh, size);

	if (size == 0 || rdev->bios[0] != 0x55 || rdev->bios[1] != 0xaa) {
		kfree(rdev->bios);
		rdev->bios = NULL;
		return false;
	}
	return true;
}

#endif

#ifdef __linux__
static bool radeon_read_platform_bios(struct radeon_device *rdev)
{
	phys_addr_t rom = rdev->pdev->rom;
	size_t romlen = rdev->pdev->romlen;
	void __iomem *bios;

	rdev->bios = NULL;

	if (!rom || romlen == 0)
		return false;

	rdev->bios = kzalloc(romlen, GFP_KERNEL);
	if (!rdev->bios)
		return false;

	bios = ioremap(rom, romlen);
	if (!bios)
		goto free_bios;

	memcpy_fromio(rdev->bios, bios, romlen);
	iounmap(bios);

	if (rdev->bios[0] != 0x55 || rdev->bios[1] != 0xaa)
		goto free_bios;

	return true;
free_bios:
	kfree(rdev->bios);
	return false;
}
#else
static bool radeon_read_platform_bios(struct radeon_device *rdev)
{
#if defined(__amd64__) || defined(__i386__) || defined(__loongson__)
	uint8_t __iomem *bios;
	bus_size_t size = 256 * 1024; /* ??? */
	uint8_t *found = NULL;
	int i;
	
	if (!(rdev->flags & RADEON_IS_IGP))
		if (!radeon_card_posted(rdev))
			return false;

	rdev->bios = NULL;

#if defined(__loongson__)
	if (loongson_videobios == NULL)
		return false;
	bios = loongson_videobios;
#else
	bios = (u8 *)ISA_HOLE_VADDR(0xc0000);
#endif

	for (i = 0; i + 2 < size; i++) {
		if (bios[i] == 0x55 && bios[i + 1] == 0xaa) {
			found = bios + i;
			break;
		}
			
	}
	if (found == NULL) {
		DRM_ERROR("bios size zero or checksum mismatch\n");
		return false;
	}

	rdev->bios = kmalloc(size, GFP_KERNEL);
	if (rdev->bios == NULL)
		return false;

	memcpy(rdev->bios, found, size);

	return true;
#endif
	return false;
}
#endif

#ifdef CONFIG_ACPI
/* ATRM is used to get the BIOS on the discrete cards in
 * dual-gpu systems.
 */
/* retrieve the ROM in 4k blocks */
#define ATRM_BIOS_PAGE 4096
/**
 * radeon_atrm_call - fetch a chunk of the vbios
 *
 * @atrm_handle: acpi ATRM handle
 * @bios: vbios image pointer
 * @offset: offset of vbios image data to fetch
 * @len: length of vbios image data to fetch
 *
 * Executes ATRM to fetch a chunk of the discrete
 * vbios image on PX systems (all asics).
 * Returns the length of the buffer fetched.
 */
static int radeon_atrm_call(acpi_handle atrm_handle, uint8_t *bios,
			    int offset, int len)
{
	acpi_status status;
	union acpi_object atrm_arg_elements[2], *obj;
	struct acpi_object_list atrm_arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL};

	atrm_arg.count = 2;
	atrm_arg.pointer = &atrm_arg_elements[0];

	atrm_arg_elements[0].type = ACPI_TYPE_INTEGER;
	atrm_arg_elements[0].integer.value = offset;

	atrm_arg_elements[1].type = ACPI_TYPE_INTEGER;
	atrm_arg_elements[1].integer.value = len;

	status = acpi_evaluate_object(atrm_handle, NULL, &atrm_arg, &buffer);
	if (ACPI_FAILURE(status)) {
		printk("failed to evaluate ATRM got %s\n", acpi_format_exception(status));
		return -ENODEV;
	}

	obj = (union acpi_object *)buffer.pointer;
	memcpy(bios+offset, obj->buffer.pointer, obj->buffer.length);
	len = obj->buffer.length;
	kfree(buffer.pointer);
	return len;
}

static bool radeon_atrm_get_bios(struct radeon_device *rdev)
{
	int ret;
	int size = 256 * 1024;
	int i;
	struct pci_dev *pdev = NULL;
	acpi_handle dhandle, atrm_handle;
	acpi_status status;
	bool found = false;

	/* ATRM is for the discrete card only */
	if (rdev->flags & RADEON_IS_IGP)
		return false;

#ifdef notyet
	while ((pdev = pci_get_base_class(PCI_BASE_CLASS_DISPLAY, pdev))) {
		if ((pdev->class != PCI_CLASS_DISPLAY_VGA << 8) &&
		    (pdev->class != PCI_CLASS_DISPLAY_OTHER << 8))
			continue;

		dhandle = ACPI_HANDLE(&pdev->dev);
		if (!dhandle)
			continue;

		status = acpi_get_handle(dhandle, "ATRM", &atrm_handle);
		if (ACPI_SUCCESS(status)) {
			found = true;
			break;
		}
	}
#else
	{
		pdev = rdev->pdev;
		dhandle = ACPI_HANDLE(&pdev->dev);

		if (dhandle) {
			status = acpi_get_handle(dhandle, "ATRM", &atrm_handle);
			if (ACPI_SUCCESS(status)) {
				found = true;
			}
		}
	}
#endif

	if (!found)
		return false;
	pci_dev_put(pdev);

	rdev->bios = kmalloc(size, GFP_KERNEL);
	if (!rdev->bios) {
		DRM_ERROR("Unable to allocate bios\n");
		return false;
	}

	for (i = 0; i < size / ATRM_BIOS_PAGE; i++) {
		ret = radeon_atrm_call(atrm_handle,
				       rdev->bios,
				       (i * ATRM_BIOS_PAGE),
				       ATRM_BIOS_PAGE);
		if (ret < ATRM_BIOS_PAGE)
			break;
	}

	if (i == 0 || rdev->bios[0] != 0x55 || rdev->bios[1] != 0xaa) {
		kfree(rdev->bios);
		return false;
	}
	return true;
}
#else
static inline bool radeon_atrm_get_bios(struct radeon_device *rdev)
{
	return false;
}
#endif

static bool ni_read_disabled_bios(struct radeon_device *rdev)
{
	u32 bus_cntl;
	u32 d1vga_control;
	u32 d2vga_control;
	u32 vga_render_control;
	u32 rom_cntl;
	bool r;

	bus_cntl = RREG32(R600_BUS_CNTL);
	d1vga_control = RREG32(AVIVO_D1VGA_CONTROL);
	d2vga_control = RREG32(AVIVO_D2VGA_CONTROL);
	vga_render_control = RREG32(AVIVO_VGA_RENDER_CONTROL);
	rom_cntl = RREG32(R600_ROM_CNTL);

	/* enable the rom */
	WREG32(R600_BUS_CNTL, (bus_cntl & ~R600_BIOS_ROM_DIS));
	if (!ASIC_IS_NODCE(rdev)) {
		/* Disable VGA mode */
		WREG32(AVIVO_D1VGA_CONTROL,
		       (d1vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
					  AVIVO_DVGA_CONTROL_TIMING_SELECT)));
		WREG32(AVIVO_D2VGA_CONTROL,
		       (d2vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
					  AVIVO_DVGA_CONTROL_TIMING_SELECT)));
		WREG32(AVIVO_VGA_RENDER_CONTROL,
		       (vga_render_control & ~AVIVO_VGA_VSTATUS_CNTL_MASK));
	}
	WREG32(R600_ROM_CNTL, rom_cntl | R600_SCK_OVERWRITE);

	r = radeon_read_bios(rdev);

	/* restore regs */
	WREG32(R600_BUS_CNTL, bus_cntl);
	if (!ASIC_IS_NODCE(rdev)) {
		WREG32(AVIVO_D1VGA_CONTROL, d1vga_control);
		WREG32(AVIVO_D2VGA_CONTROL, d2vga_control);
		WREG32(AVIVO_VGA_RENDER_CONTROL, vga_render_control);
	}
	WREG32(R600_ROM_CNTL, rom_cntl);
	return r;
}

static bool r700_read_disabled_bios(struct radeon_device *rdev)
{
	uint32_t viph_control;
	uint32_t bus_cntl;
	uint32_t d1vga_control;
	uint32_t d2vga_control;
	uint32_t vga_render_control;
	uint32_t rom_cntl;
	uint32_t cg_spll_func_cntl = 0;
	uint32_t cg_spll_status;
	bool r;

	viph_control = RREG32(RADEON_VIPH_CONTROL);
	bus_cntl = RREG32(R600_BUS_CNTL);
	d1vga_control = RREG32(AVIVO_D1VGA_CONTROL);
	d2vga_control = RREG32(AVIVO_D2VGA_CONTROL);
	vga_render_control = RREG32(AVIVO_VGA_RENDER_CONTROL);
	rom_cntl = RREG32(R600_ROM_CNTL);

	/* disable VIP */
	WREG32(RADEON_VIPH_CONTROL, (viph_control & ~RADEON_VIPH_EN));
	/* enable the rom */
	WREG32(R600_BUS_CNTL, (bus_cntl & ~R600_BIOS_ROM_DIS));
	/* Disable VGA mode */
	WREG32(AVIVO_D1VGA_CONTROL,
	       (d1vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
		AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	WREG32(AVIVO_D2VGA_CONTROL,
	       (d2vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
		AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	WREG32(AVIVO_VGA_RENDER_CONTROL,
	       (vga_render_control & ~AVIVO_VGA_VSTATUS_CNTL_MASK));

	if (rdev->family == CHIP_RV730) {
		cg_spll_func_cntl = RREG32(R600_CG_SPLL_FUNC_CNTL);

		/* enable bypass mode */
		WREG32(R600_CG_SPLL_FUNC_CNTL, (cg_spll_func_cntl |
						R600_SPLL_BYPASS_EN));

		/* wait for SPLL_CHG_STATUS to change to 1 */
		cg_spll_status = 0;
		while (!(cg_spll_status & R600_SPLL_CHG_STATUS))
			cg_spll_status = RREG32(R600_CG_SPLL_STATUS);

		WREG32(R600_ROM_CNTL, (rom_cntl & ~R600_SCK_OVERWRITE));
	} else
		WREG32(R600_ROM_CNTL, (rom_cntl | R600_SCK_OVERWRITE));

	r = radeon_read_bios(rdev);

	/* restore regs */
	if (rdev->family == CHIP_RV730) {
		WREG32(R600_CG_SPLL_FUNC_CNTL, cg_spll_func_cntl);

		/* wait for SPLL_CHG_STATUS to change to 1 */
		cg_spll_status = 0;
		while (!(cg_spll_status & R600_SPLL_CHG_STATUS))
			cg_spll_status = RREG32(R600_CG_SPLL_STATUS);
	}
	WREG32(RADEON_VIPH_CONTROL, viph_control);
	WREG32(R600_BUS_CNTL, bus_cntl);
	WREG32(AVIVO_D1VGA_CONTROL, d1vga_control);
	WREG32(AVIVO_D2VGA_CONTROL, d2vga_control);
	WREG32(AVIVO_VGA_RENDER_CONTROL, vga_render_control);
	WREG32(R600_ROM_CNTL, rom_cntl);
	return r;
}

static bool r600_read_disabled_bios(struct radeon_device *rdev)
{
	uint32_t viph_control;
	uint32_t bus_cntl;
	uint32_t d1vga_control;
	uint32_t d2vga_control;
	uint32_t vga_render_control;
	uint32_t rom_cntl;
	uint32_t general_pwrmgt;
	uint32_t low_vid_lower_gpio_cntl;
	uint32_t medium_vid_lower_gpio_cntl;
	uint32_t high_vid_lower_gpio_cntl;
	uint32_t ctxsw_vid_lower_gpio_cntl;
	uint32_t lower_gpio_enable;
	bool r;
	
	/*
	 * Some machines with RV610 running amd64 pass initial checks but later
	 * fail atombios specific checks.  Return early here so the bios will be
	 * read from 0xc0000 in radeon_read_platform_bios() instead.
	 * RV610 0x1002:0x94C3 0x1028:0x0402 0x00
	 * RV610 0x1002:0x94C1 0x1028:0x0D02 0x00
	 */
	if (rdev->family == CHIP_RV610)
		return false;

	viph_control = RREG32(RADEON_VIPH_CONTROL);
	bus_cntl = RREG32(R600_BUS_CNTL);
	d1vga_control = RREG32(AVIVO_D1VGA_CONTROL);
	d2vga_control = RREG32(AVIVO_D2VGA_CONTROL);
	vga_render_control = RREG32(AVIVO_VGA_RENDER_CONTROL);
	rom_cntl = RREG32(R600_ROM_CNTL);
	general_pwrmgt = RREG32(R600_GENERAL_PWRMGT);
	low_vid_lower_gpio_cntl = RREG32(R600_LOW_VID_LOWER_GPIO_CNTL);
	medium_vid_lower_gpio_cntl = RREG32(R600_MEDIUM_VID_LOWER_GPIO_CNTL);
	high_vid_lower_gpio_cntl = RREG32(R600_HIGH_VID_LOWER_GPIO_CNTL);
	ctxsw_vid_lower_gpio_cntl = RREG32(R600_CTXSW_VID_LOWER_GPIO_CNTL);
	lower_gpio_enable = RREG32(R600_LOWER_GPIO_ENABLE);

	/* disable VIP */
	WREG32(RADEON_VIPH_CONTROL, (viph_control & ~RADEON_VIPH_EN));
	/* enable the rom */
	WREG32(R600_BUS_CNTL, (bus_cntl & ~R600_BIOS_ROM_DIS));
	/* Disable VGA mode */
	WREG32(AVIVO_D1VGA_CONTROL,
	       (d1vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
		AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	WREG32(AVIVO_D2VGA_CONTROL,
	       (d2vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
		AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	WREG32(AVIVO_VGA_RENDER_CONTROL,
	       (vga_render_control & ~AVIVO_VGA_VSTATUS_CNTL_MASK));

	WREG32(R600_ROM_CNTL,
	       ((rom_cntl & ~R600_SCK_PRESCALE_CRYSTAL_CLK_MASK) |
		(1 << R600_SCK_PRESCALE_CRYSTAL_CLK_SHIFT) |
		R600_SCK_OVERWRITE));

	WREG32(R600_GENERAL_PWRMGT, (general_pwrmgt & ~R600_OPEN_DRAIN_PADS));
	WREG32(R600_LOW_VID_LOWER_GPIO_CNTL,
	       (low_vid_lower_gpio_cntl & ~0x400));
	WREG32(R600_MEDIUM_VID_LOWER_GPIO_CNTL,
	       (medium_vid_lower_gpio_cntl & ~0x400));
	WREG32(R600_HIGH_VID_LOWER_GPIO_CNTL,
	       (high_vid_lower_gpio_cntl & ~0x400));
	WREG32(R600_CTXSW_VID_LOWER_GPIO_CNTL,
	       (ctxsw_vid_lower_gpio_cntl & ~0x400));
	WREG32(R600_LOWER_GPIO_ENABLE, (lower_gpio_enable | 0x400));

	r = radeon_read_bios(rdev);

	/* restore regs */
	WREG32(RADEON_VIPH_CONTROL, viph_control);
	WREG32(R600_BUS_CNTL, bus_cntl);
	WREG32(AVIVO_D1VGA_CONTROL, d1vga_control);
	WREG32(AVIVO_D2VGA_CONTROL, d2vga_control);
	WREG32(AVIVO_VGA_RENDER_CONTROL, vga_render_control);
	WREG32(R600_ROM_CNTL, rom_cntl);
	WREG32(R600_GENERAL_PWRMGT, general_pwrmgt);
	WREG32(R600_LOW_VID_LOWER_GPIO_CNTL, low_vid_lower_gpio_cntl);
	WREG32(R600_MEDIUM_VID_LOWER_GPIO_CNTL, medium_vid_lower_gpio_cntl);
	WREG32(R600_HIGH_VID_LOWER_GPIO_CNTL, high_vid_lower_gpio_cntl);
	WREG32(R600_CTXSW_VID_LOWER_GPIO_CNTL, ctxsw_vid_lower_gpio_cntl);
	WREG32(R600_LOWER_GPIO_ENABLE, lower_gpio_enable);
	return r;
}

static bool avivo_read_disabled_bios(struct radeon_device *rdev)
{
	uint32_t seprom_cntl1;
	uint32_t viph_control;
	uint32_t bus_cntl;
	uint32_t d1vga_control;
	uint32_t d2vga_control;
	uint32_t vga_render_control;
	uint32_t gpiopad_a;
	uint32_t gpiopad_en;
	uint32_t gpiopad_mask;
	bool r;

	seprom_cntl1 = RREG32(RADEON_SEPROM_CNTL1);
	viph_control = RREG32(RADEON_VIPH_CONTROL);
	bus_cntl = RREG32(RV370_BUS_CNTL);
	d1vga_control = RREG32(AVIVO_D1VGA_CONTROL);
	d2vga_control = RREG32(AVIVO_D2VGA_CONTROL);
	vga_render_control = RREG32(AVIVO_VGA_RENDER_CONTROL);
	gpiopad_a = RREG32(RADEON_GPIOPAD_A);
	gpiopad_en = RREG32(RADEON_GPIOPAD_EN);
	gpiopad_mask = RREG32(RADEON_GPIOPAD_MASK);

	WREG32(RADEON_SEPROM_CNTL1,
	       ((seprom_cntl1 & ~RADEON_SCK_PRESCALE_MASK) |
		(0xc << RADEON_SCK_PRESCALE_SHIFT)));
	WREG32(RADEON_GPIOPAD_A, 0);
	WREG32(RADEON_GPIOPAD_EN, 0);
	WREG32(RADEON_GPIOPAD_MASK, 0);

	/* disable VIP */
	WREG32(RADEON_VIPH_CONTROL, (viph_control & ~RADEON_VIPH_EN));

	/* enable the rom */
	WREG32(RV370_BUS_CNTL, (bus_cntl & ~RV370_BUS_BIOS_DIS_ROM));

	/* Disable VGA mode */
	WREG32(AVIVO_D1VGA_CONTROL,
	       (d1vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
		AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	WREG32(AVIVO_D2VGA_CONTROL,
	       (d2vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
		AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	WREG32(AVIVO_VGA_RENDER_CONTROL,
	       (vga_render_control & ~AVIVO_VGA_VSTATUS_CNTL_MASK));

	r = radeon_read_bios(rdev);

	/* restore regs */
	WREG32(RADEON_SEPROM_CNTL1, seprom_cntl1);
	WREG32(RADEON_VIPH_CONTROL, viph_control);
	WREG32(RV370_BUS_CNTL, bus_cntl);
	WREG32(AVIVO_D1VGA_CONTROL, d1vga_control);
	WREG32(AVIVO_D2VGA_CONTROL, d2vga_control);
	WREG32(AVIVO_VGA_RENDER_CONTROL, vga_render_control);
	WREG32(RADEON_GPIOPAD_A, gpiopad_a);
	WREG32(RADEON_GPIOPAD_EN, gpiopad_en);
	WREG32(RADEON_GPIOPAD_MASK, gpiopad_mask);
	return r;
}

static bool legacy_read_disabled_bios(struct radeon_device *rdev)
{
	uint32_t seprom_cntl1;
	uint32_t viph_control;
	uint32_t bus_cntl;
	uint32_t crtc_gen_cntl;
	uint32_t crtc2_gen_cntl;
	uint32_t crtc_ext_cntl;
	uint32_t fp2_gen_cntl;
	bool r;

	seprom_cntl1 = RREG32(RADEON_SEPROM_CNTL1);
	viph_control = RREG32(RADEON_VIPH_CONTROL);
	if (rdev->flags & RADEON_IS_PCIE)
		bus_cntl = RREG32(RV370_BUS_CNTL);
	else
		bus_cntl = RREG32(RADEON_BUS_CNTL);
	crtc_gen_cntl = RREG32(RADEON_CRTC_GEN_CNTL);
	crtc2_gen_cntl = 0;
	crtc_ext_cntl = RREG32(RADEON_CRTC_EXT_CNTL);
	fp2_gen_cntl = 0;

	if (rdev->pdev->device == PCI_DEVICE_ID_ATI_RADEON_QY) {
		fp2_gen_cntl = RREG32(RADEON_FP2_GEN_CNTL);
	}

	if (!(rdev->flags & RADEON_SINGLE_CRTC)) {
		crtc2_gen_cntl = RREG32(RADEON_CRTC2_GEN_CNTL);
	}

	WREG32(RADEON_SEPROM_CNTL1,
	       ((seprom_cntl1 & ~RADEON_SCK_PRESCALE_MASK) |
		(0xc << RADEON_SCK_PRESCALE_SHIFT)));

	/* disable VIP */
	WREG32(RADEON_VIPH_CONTROL, (viph_control & ~RADEON_VIPH_EN));

	/* enable the rom */
	if (rdev->flags & RADEON_IS_PCIE)
		WREG32(RV370_BUS_CNTL, (bus_cntl & ~RV370_BUS_BIOS_DIS_ROM));
	else
		WREG32(RADEON_BUS_CNTL, (bus_cntl & ~RADEON_BUS_BIOS_DIS_ROM));

	/* Turn off mem requests and CRTC for both controllers */
	WREG32(RADEON_CRTC_GEN_CNTL,
	       ((crtc_gen_cntl & ~RADEON_CRTC_EN) |
		(RADEON_CRTC_DISP_REQ_EN_B |
		 RADEON_CRTC_EXT_DISP_EN)));
	if (!(rdev->flags & RADEON_SINGLE_CRTC)) {
		WREG32(RADEON_CRTC2_GEN_CNTL,
		       ((crtc2_gen_cntl & ~RADEON_CRTC2_EN) |
			RADEON_CRTC2_DISP_REQ_EN_B));
	}
	/* Turn off CRTC */
	WREG32(RADEON_CRTC_EXT_CNTL,
	       ((crtc_ext_cntl & ~RADEON_CRTC_CRT_ON) |
		(RADEON_CRTC_SYNC_TRISTAT |
		 RADEON_CRTC_DISPLAY_DIS)));

	if (rdev->pdev->device == PCI_DEVICE_ID_ATI_RADEON_QY) {
		WREG32(RADEON_FP2_GEN_CNTL, (fp2_gen_cntl & ~RADEON_FP2_ON));
	}

	r = radeon_read_bios(rdev);

	/* restore regs */
	WREG32(RADEON_SEPROM_CNTL1, seprom_cntl1);
	WREG32(RADEON_VIPH_CONTROL, viph_control);
	if (rdev->flags & RADEON_IS_PCIE)
		WREG32(RV370_BUS_CNTL, bus_cntl);
	else
		WREG32(RADEON_BUS_CNTL, bus_cntl);
	WREG32(RADEON_CRTC_GEN_CNTL, crtc_gen_cntl);
	if (!(rdev->flags & RADEON_SINGLE_CRTC)) {
		WREG32(RADEON_CRTC2_GEN_CNTL, crtc2_gen_cntl);
	}
	WREG32(RADEON_CRTC_EXT_CNTL, crtc_ext_cntl);
	if (rdev->pdev->device == PCI_DEVICE_ID_ATI_RADEON_QY) {
		WREG32(RADEON_FP2_GEN_CNTL, fp2_gen_cntl);
	}
	return r;
}

static bool radeon_read_disabled_bios(struct radeon_device *rdev)
{
	if (rdev->flags & RADEON_IS_IGP)
		return igp_read_bios_from_vram(rdev);
	else if (rdev->family >= CHIP_BARTS)
		return ni_read_disabled_bios(rdev);
	else if (rdev->family >= CHIP_RV770)
		return r700_read_disabled_bios(rdev);
	else if (rdev->family >= CHIP_R600)
		return r600_read_disabled_bios(rdev);
	else if (rdev->family >= CHIP_RS600)
		return avivo_read_disabled_bios(rdev);
	else
		return legacy_read_disabled_bios(rdev);
}

#ifdef CONFIG_ACPI
static bool radeon_acpi_vfct_bios(struct radeon_device *rdev)
{
	struct acpi_table_header *hdr;
	acpi_size tbl_size;
	UEFI_ACPI_VFCT *vfct;
	unsigned offset;
	bool r = false;

	if (!ACPI_SUCCESS(acpi_get_table("VFCT", 1, &hdr)))
		return false;
	tbl_size = hdr->length;
	if (tbl_size < sizeof(UEFI_ACPI_VFCT)) {
		DRM_ERROR("ACPI VFCT table present but broken (too short #1)\n");
		goto out;
	}

	vfct = (UEFI_ACPI_VFCT *)hdr;
	offset = vfct->VBIOSImageOffset;

	while (offset < tbl_size) {
		GOP_VBIOS_CONTENT *vbios = (GOP_VBIOS_CONTENT *)((char *)hdr + offset);
		VFCT_IMAGE_HEADER *vhdr = &vbios->VbiosHeader;

		offset += sizeof(VFCT_IMAGE_HEADER);
		if (offset > tbl_size) {
			DRM_ERROR("ACPI VFCT image header truncated\n");
			goto out;
		}

		offset += vhdr->ImageLength;
		if (offset > tbl_size) {
			DRM_ERROR("ACPI VFCT image truncated\n");
			goto out;
		}

		if (vhdr->ImageLength &&
		    vhdr->PCIBus == rdev->pdev->bus->number &&
		    vhdr->PCIDevice == PCI_SLOT(rdev->pdev->devfn) &&
		    vhdr->PCIFunction == PCI_FUNC(rdev->pdev->devfn) &&
		    vhdr->VendorID == rdev->pdev->vendor &&
		    vhdr->DeviceID == rdev->pdev->device) {
			rdev->bios = kmemdup(&vbios->VbiosContent,
					     vhdr->ImageLength,
					     GFP_KERNEL);
			if (rdev->bios)
				r = true;

			goto out;
		}
	}

	DRM_ERROR("ACPI VFCT table present but broken (too short #2)\n");

out:
	acpi_put_table(hdr);
	return r;
}
#else
static inline bool radeon_acpi_vfct_bios(struct radeon_device *rdev)
{
	return false;
}
#endif

bool radeon_get_bios(struct radeon_device *rdev)
{
	bool r;
	uint16_t tmp;

	r = radeon_atrm_get_bios(rdev);
	if (!r)
		r = radeon_acpi_vfct_bios(rdev);
	if (!r)
		r = igp_read_bios_from_vram(rdev);
	if (!r)
		r = radeon_read_bios(rdev);
	if (!r)
		r = radeon_read_disabled_bios(rdev);
	if (!r)
		r = radeon_read_platform_bios(rdev);
	if (!r || rdev->bios == NULL) {
		DRM_ERROR("Unable to locate a BIOS ROM\n");
		rdev->bios = NULL;
		return false;
	}
	if (rdev->bios[0] != 0x55 || rdev->bios[1] != 0xaa) {
		printk("BIOS signature incorrect %x %x\n", rdev->bios[0], rdev->bios[1]);
		goto free_bios;
	}

	tmp = RBIOS16(0x18);
	if (RBIOS8(tmp + 0x14) != 0x0) {
		DRM_INFO("Not an x86 BIOS ROM, not using.\n");
		goto free_bios;
	}

	rdev->bios_header_start = RBIOS16(0x48);
	if (!rdev->bios_header_start) {
		goto free_bios;
	}
	tmp = rdev->bios_header_start + 4;
	if (!memcmp(rdev->bios + tmp, "ATOM", 4) ||
	    !memcmp(rdev->bios + tmp, "MOTA", 4)) {
		rdev->is_atom_bios = true;
	} else {
		rdev->is_atom_bios = false;
	}

	DRM_DEBUG("%sBIOS detected\n", rdev->is_atom_bios ? "ATOM" : "COM");
	return true;
free_bios:
	kfree(rdev->bios);
	rdev->bios = NULL;
	return false;
}
