/*
 * soc-apci.c - support for ACPI enumeration.
 *
 * Copyright (c) 2013-15, Intel Corporation.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <sound/soc-acpi.h>

struct snd_soc_acpi_mach *
snd_soc_acpi_find_machine(struct snd_soc_acpi_mach *machines)
{
	struct snd_soc_acpi_mach *mach;

	for (mach = machines; mach->id[0]; mach++) {
		if (acpi_dev_present(mach->id, NULL, -1)) {
			if (mach->machine_quirk)
				mach = mach->machine_quirk(mach);
			return mach;
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_acpi_find_machine);

static acpi_status snd_soc_acpi_find_package(acpi_handle handle, u32 level,
					     void *context, void **ret)
{
	struct acpi_device *adev;
	acpi_status status = AE_OK;
	struct snd_soc_acpi_package_context *pkg_ctx = context;

	pkg_ctx->data_valid = false;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;

	if (adev->status.present && adev->status.functional) {
		struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
		union acpi_object  *myobj = NULL;

		status = acpi_evaluate_object_typed(handle, pkg_ctx->name,
						NULL, &buffer,
						ACPI_TYPE_PACKAGE);
		if (ACPI_FAILURE(status))
			return AE_OK;

		myobj = buffer.pointer;
		if (!myobj || myobj->package.count != pkg_ctx->length) {
			kfree(buffer.pointer);
			return AE_OK;
		}

		status = acpi_extract_package(myobj,
					pkg_ctx->format, pkg_ctx->state);
		if (ACPI_FAILURE(status)) {
			kfree(buffer.pointer);
			return AE_OK;
		}

		kfree(buffer.pointer);
		pkg_ctx->data_valid = true;
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

bool snd_soc_acpi_find_package_from_hid(const u8 hid[ACPI_ID_LEN],
				struct snd_soc_acpi_package_context *ctx)
{
	acpi_status status;

	status = acpi_get_devices(hid, snd_soc_acpi_find_package, ctx, NULL);

	if (ACPI_FAILURE(status) || !ctx->data_valid)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(snd_soc_acpi_find_package_from_hid);

struct snd_soc_acpi_mach *snd_soc_acpi_codec_list(void *arg)
{
	struct snd_soc_acpi_mach *mach = arg;
	struct snd_soc_acpi_codecs *codec_list =
		(struct snd_soc_acpi_codecs *) mach->quirk_data;
	int i;

	if (mach->quirk_data == NULL)
		return mach;

	for (i = 0; i < codec_list->num_codecs; i++) {
		if (!acpi_dev_present(codec_list->codecs[i], NULL, -1))
			return NULL;
	}

	return mach;
}
EXPORT_SYMBOL_GPL(snd_soc_acpi_codec_list);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ALSA SoC ACPI module");
