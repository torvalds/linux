#ifndef _IIO_UTILS_H_
#define _IIO_UTILS_H_

/* IIO - useful set of util functionality
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <stdint.h>

/* Made up value to limit allocation sizes */
#define IIO_MAX_NAME_LENGTH 30

#define FORMAT_SCAN_ELEMENTS_DIR "%s/scan_elements"
#define FORMAT_TYPE_FILE "%s_type"

extern const char *iio_dir;

/**
 * struct iio_channel_info - information about a given channel
 * @name: channel name
 * @generic_name: general name for channel type
 * @scale: scale factor to be applied for conversion to si units
 * @offset: offset to be applied for conversion to si units
 * @index: the channel index in the buffer output
 * @bytes: number of bytes occupied in buffer output
 * @mask: a bit mask for the raw output
 * @is_signed: is the raw value stored signed
 * @enabled: is this channel enabled
 **/
struct iio_channel_info {
	char *name;
	char *generic_name;
	float scale;
	float offset;
	unsigned index;
	unsigned bytes;
	unsigned bits_used;
	unsigned shift;
	uint64_t mask;
	unsigned be;
	unsigned is_signed;
	unsigned location;
};

int iioutils_break_up_name(const char *full_name, char **generic_name);
int iioutils_get_type(unsigned *is_signed, unsigned *bytes,
					  unsigned *bits_used, unsigned *shift,
					  uint64_t *mask, unsigned *be,
					  const char *device_dir, const char *name,
					  const char *generic_name);
int iioutils_get_param_float(float *output, const char *param_name,
							 const char *device_dir, const char *name,
							 const char *generic_name);
void bsort_channel_array_by_index(struct iio_channel_info **ci_array, int cnt);
int build_channel_array(const char *device_dir,
						struct iio_channel_info **ci_array, int *counter);
int find_type_by_name(const char *name, const char *type);
int write_sysfs_int(char *filename, char *basedir, int val);
int write_sysfs_int_and_verify(char *filename, char *basedir, int val);
int write_sysfs_string_and_verify(char *filename, char *basedir, char *val);
int write_sysfs_string(char *filename, char *basedir, char *val);
int read_sysfs_posint(char *filename, char *basedir);
int read_sysfs_float(char *filename, char *basedir, float *val);
int read_sysfs_string(const char *filename, const char *basedir, char *str);

#endif /* _IIO_UTILS_H_ */
