// SPDX-License-Identifier: GPL-2.0-only
/***********************************************************************

    AudioScience HPI driver
    Functions for reading DSP code using hotplug firmware loader

    Copyright (C) 1997-2014  AudioScience Inc. <support@audioscience.com>


***********************************************************************/
#define SOURCEFILE_NAME "hpidspcd.c"
#include "hpidspcd.h"
#include "hpidebug.h"
#include "hpi_version.h"

struct dsp_code_private {
	/**  Firmware descriptor */
	const struct firmware *firmware;
	struct pci_dev *dev;
};

/*-------------------------------------------------------------------*/
short hpi_dsp_code_open(u32 adapter, void *os_data, struct dsp_code *dsp_code,
	u32 *os_error_code)
{
	const struct firmware *firmware;
	struct pci_dev *dev = os_data;
	struct code_header header;
	char fw_name[20];
	short err_ret = HPI_ERROR_DSP_FILE_NOT_FOUND;
	int err;

	sprintf(fw_name, "asihpi/dsp%04x.bin", adapter);

	err = request_firmware(&firmware, fw_name, &dev->dev);

	if (err || !firmware) {
		dev_err(&dev->dev, "%d, request_firmware failed for %s\n",
			err, fw_name);
		goto error1;
	}
	if (firmware->size < sizeof(header)) {
		dev_err(&dev->dev, "Header size too small %s\n", fw_name);
		goto error2;
	}
	memcpy(&header, firmware->data, sizeof(header));

	if ((header.type != 0x45444F43) ||	/* "CODE" */
		(header.adapter != adapter)
		|| (header.size != firmware->size)) {
		dev_err(&dev->dev,
			"Invalid firmware header size %d != file %zd\n",
			header.size, firmware->size);
		goto error2;
	}

	if (HPI_VER_MAJOR(header.version) != HPI_VER_MAJOR(HPI_VER)) {
		/* Major version change probably means Host-DSP protocol change */
		dev_err(&dev->dev,
			"Incompatible firmware version DSP image %X != Driver %X\n",
			header.version, HPI_VER);
		goto error2;
	}

	if (header.version != HPI_VER) {
		dev_warn(&dev->dev,
			"Firmware version mismatch: DSP image %X != Driver %X\n",
			header.version, HPI_VER);
	}

	HPI_DEBUG_LOG(DEBUG, "dsp code %s opened\n", fw_name);
	dsp_code->pvt = kmalloc(sizeof(*dsp_code->pvt), GFP_KERNEL);
	if (!dsp_code->pvt) {
		err_ret = HPI_ERROR_MEMORY_ALLOC;
		goto error2;
	}

	dsp_code->pvt->dev = dev;
	dsp_code->pvt->firmware = firmware;
	dsp_code->header = header;
	dsp_code->block_length = header.size / sizeof(u32);
	dsp_code->word_count = sizeof(header) / sizeof(u32);
	return 0;

error2:
	release_firmware(firmware);
error1:
	dsp_code->block_length = 0;
	return err_ret;
}

/*-------------------------------------------------------------------*/
void hpi_dsp_code_close(struct dsp_code *dsp_code)
{
	HPI_DEBUG_LOG(DEBUG, "dsp code closed\n");
	release_firmware(dsp_code->pvt->firmware);
	kfree(dsp_code->pvt);
}

/*-------------------------------------------------------------------*/
void hpi_dsp_code_rewind(struct dsp_code *dsp_code)
{
	/* Go back to start of  data, after header */
	dsp_code->word_count = sizeof(struct code_header) / sizeof(u32);
}

/*-------------------------------------------------------------------*/
short hpi_dsp_code_read_word(struct dsp_code *dsp_code, u32 *pword)
{
	if (dsp_code->word_count + 1 > dsp_code->block_length)
		return HPI_ERROR_DSP_FILE_FORMAT;

	*pword = ((u32 *)(dsp_code->pvt->firmware->data))[dsp_code->
		word_count];
	dsp_code->word_count++;
	return 0;
}

/*-------------------------------------------------------------------*/
short hpi_dsp_code_read_block(size_t words_requested,
	struct dsp_code *dsp_code, u32 **ppblock)
{
	if (dsp_code->word_count + words_requested > dsp_code->block_length)
		return HPI_ERROR_DSP_FILE_FORMAT;

	*ppblock =
		((u32 *)(dsp_code->pvt->firmware->data)) +
		dsp_code->word_count;
	dsp_code->word_count += words_requested;
	return 0;
}
