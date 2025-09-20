/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _IIO_UTILS_H_
#define _IIO_UTILS_H_

/* IIO - useful set of util functionality
 *
 * Copyright (c) 2008 Jonathan Cameron
 */

#include <stdint.h>

/* Made up value to limit allocation sizes */
#define IIO_MAX_NAME_LENGTH 64

#define FORMAT_SCAN_ELEMENTS_DIR "%s/buffer%d"
#define FORMAT_EVENTS_DIR "%s/events"
#define FORMAT_TYPE_FILE "%s_type"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

extern const char *iio_dir;

/**
 * struct iio_channel_info - information about a given channel
 * @name: channel name
 * @generic_name: general name for channel type
 * @scale: scale factor to be applied for conversion to si units
 * @offset: offset to be applied for conversion to si units
 * @index: the channel index in the buffer output
 * @bytes: number of bytes occupied in buffer output
 * @bits_used: number of valid bits of data
 * @shift: amount of bits to shift right data before applying bit mask
 * @mask: a bit mask for the raw output
 * @be: flag if data is big endian
 * @is_signed: is the raw value stored signed
 * @location: data offset for this channel inside the buffer (in bytes)
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

static inline int iioutils_check_suffix(const char *str, const char *suffix)
{
	return strlen(str) >= strlen(suffix) &&
		strncmp(str+strlen(str)-strlen(suffix),
			suffix, strlen(suffix)) == 0;
}

int iioutils_break_up_name(const char *full_name, char **generic_name);
int iioutils_get_param_float(float *output, const char *param_name,
			     const char *device_dir, const char *name,
			     const char *generic_name);
void bsort_channel_array_by_index(struct iio_channel_info *ci_array, int cnt);
int build_channel_array(const char *device_dir, int buffer_idx,
			struct iio_channel_info **ci_array, int *counter);
int find_type_by_name(const char *name, const char *type);
int write_sysfs_int(const char *filename, const char *basedir, int val);
int write_sysfs_int_and_verify(const char *filename, const char *basedir,
			       int val);
int write_sysfs_string_and_verify(const char *filename, const char *basedir,
				  const char *val);
int write_sysfs_string(const char *filename, const char *basedir,
		       const char *val);
int read_sysfs_posint(const char *filename, const char *basedir);
int read_sysfs_float(const char *filename, const char *basedir, float *val);
int read_sysfs_string(const char *filename, const char *basedir, char *str);

#endif /* _IIO_UTILS_H_ */
