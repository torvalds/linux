/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fs.h>

/**
 *	mod_firmware_load - load sound driver firmware
 *	@fn: filename
 *	@fp: return for the buffer.
 *
 *	Load the firmware for a sound module (up to 128K) into a buffer.
 *	The buffer is returned in *fp. It is allocated with vmalloc so is
 *	virtually linear and not DMAable. The caller should free it with
 *	vfree when finished.
 *
 *	The length of the buffer is returned on a successful load, the
 *	value zero on a failure.
 *
 *	Caution: This API is not recommended. Firmware should be loaded via
 *	request_firmware.
 */
static inline int mod_firmware_load(const char *fn, char **fp)
{
	loff_t size;
	int err;

	err = kernel_read_file_from_path(fn, (void **)fp, &size,
					 131072, READING_FIRMWARE);
	if (err < 0)
		return 0;
	return size;
}
