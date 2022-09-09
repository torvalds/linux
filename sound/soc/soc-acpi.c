// SPDX-License-Identifier: GPL-2.0
//
// soc-apci.c - support for ACPI enumeration.
//
// Copyright (c) 2013-15, Intel Corporation.

#include <linux/export.h>
#include <linux/module.h>
#include <sound/soc-acpi.h>

static bool snd_soc_acpi_id_present(struct snd_soc_acpi_mach *machine)
{
	const struct snd_soc_acpi_codecs *comp_ids = machine->comp_ids;
	int i;

	if (machine->id[0]) {
		if (acpi_dev_present(machine->id, NULL, -1))
			return true;
	}

	if (comp_ids) {
		for (i = 0; i < comp_ids->num_codecs; i++) {
			if (acpi_dev_present(comp_ids->codecs[i], NULL, -1)) {
				strscpy(machine->id, comp_ids->codecs[i], ACPI_ID_LEN);
				return true;
			}
		}
	}

	return false;
}

struct snd_soc_acpi_mach *
snd_soc_acpi_find_machine(struct snd_soc_acpi_mach *machines)
{
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_acpi_mach *mach_alt;

	for (mach = machines; mach->id[0] || mach->comp_ids; mach++) {
		if (snd_soc_acpi_id_present(mach)) {
			if (mach->machine_quirk) {
				mach_alt = mach->machine_quirk(mach);
				if (!mach_alt)
					continue; /* not full match, ignore */
				mach = mach_alt;
			}

			return mach;
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_soc_acpi_find_machine);

static acpi_status snd_soc_acpi_find_package(acpi_handle handle, u32 level,
					     void *context, void **ret)
{
	struct acpi_device *adev = acpi_fetch_acpi_dev(handle);
	acpi_status status;
	struct snd_soc_acpi_package_context *pkg_ctx = context;

	pkg_ctx->data_valid = false;

	if (adev && adev->status.present && adev->status.functional) {
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
