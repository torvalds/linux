/*
 * Copyright (c) 2018 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "fido.h"

static int
get_key_len(uint8_t tag, uint8_t *key, size_t *key_len)
{
	*key = tag & 0xfc;
	if ((*key & 0xf0) == 0xf0) {
		fido_log_debug("%s: *key=0x%02x", __func__, *key);
		return (-1);
	}

	*key_len = tag & 0x3;
	if (*key_len == 3) {
		*key_len = 4;
	}

	return (0);
}

static int
get_key_val(const void *body, size_t key_len, uint32_t *val)
{
	const uint8_t *ptr = body;

	switch (key_len) {
	case 0:
		*val = 0;
		break;
	case 1:
		*val = ptr[0];
		break;
	case 2:
		*val = (uint32_t)((ptr[1] << 8) | ptr[0]);
		break;
	default:
		fido_log_debug("%s: key_len=%zu", __func__, key_len);
		return (-1);
	}

	return (0);
}

int
fido_hid_get_usage(const uint8_t *report_ptr, size_t report_len,
    uint32_t *usage_page)
{
	const uint8_t	*ptr = report_ptr;
	size_t		 len = report_len;

	while (len > 0) {
		const uint8_t tag = ptr[0];
		ptr++;
		len--;

		uint8_t  key;
		size_t   key_len;
		uint32_t key_val;

		if (get_key_len(tag, &key, &key_len) < 0 || key_len > len ||
		    get_key_val(ptr, key_len, &key_val) < 0) {
			return (-1);
		}

		if (key == 0x4) {
			*usage_page = key_val;
		}

		ptr += key_len;
		len -= key_len;
	}

	return (0);
}

int
fido_hid_get_report_len(const uint8_t *report_ptr, size_t report_len,
    size_t *report_in_len, size_t *report_out_len)
{
	const uint8_t	*ptr = report_ptr;
	size_t		 len = report_len;
	uint32_t	 report_size = 0;

	while (len > 0) {
		const uint8_t tag = ptr[0];
		ptr++;
		len--;

		uint8_t  key;
		size_t   key_len;
		uint32_t key_val;

		if (get_key_len(tag, &key, &key_len) < 0 || key_len > len ||
		    get_key_val(ptr, key_len, &key_val) < 0) {
			return (-1);
		}

		if (key == 0x94) {
			report_size = key_val;
		} else if (key == 0x80) {
			*report_in_len = (size_t)report_size;
		} else if (key == 0x90) {
			*report_out_len = (size_t)report_size;
		}

		ptr += key_len;
		len -= key_len;
	}

	return (0);
}

fido_dev_info_t *
fido_dev_info_new(size_t n)
{
	return (calloc(n, sizeof(fido_dev_info_t)));
}

static void
fido_dev_info_reset(fido_dev_info_t *di)
{
	free(di->path);
	free(di->manufacturer);
	free(di->product);
	memset(di, 0, sizeof(*di));
}

void
fido_dev_info_free(fido_dev_info_t **devlist_p, size_t n)
{
	fido_dev_info_t *devlist;

	if (devlist_p == NULL || (devlist = *devlist_p) == NULL)
		return;

	for (size_t i = 0; i < n; i++)
		fido_dev_info_reset(&devlist[i]);

	free(devlist);

	*devlist_p = NULL;
}

const fido_dev_info_t *
fido_dev_info_ptr(const fido_dev_info_t *devlist, size_t i)
{
	return (&devlist[i]);
}

int
fido_dev_info_set(fido_dev_info_t *devlist, size_t i,
    const char *path, const char *manufacturer, const char *product,
    const fido_dev_io_t *io, const fido_dev_transport_t *transport)
{
	char *path_copy = NULL, *manu_copy = NULL, *prod_copy = NULL;
	int r;

	if (path == NULL || manufacturer == NULL || product == NULL ||
	    io == NULL) {
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto out;
	}

	if ((path_copy = strdup(path)) == NULL ||
	    (manu_copy = strdup(manufacturer)) == NULL ||
	    (prod_copy = strdup(product)) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto out;
	}

	fido_dev_info_reset(&devlist[i]);
	devlist[i].path = path_copy;
	devlist[i].manufacturer = manu_copy;
	devlist[i].product = prod_copy;
	devlist[i].io = *io;
	if (transport)
		devlist[i].transport = *transport;
	r = FIDO_OK;
out:
	if (r != FIDO_OK) {
		free(prod_copy);
		free(manu_copy);
		free(path_copy);
	}
	return (r);
}

const char *
fido_dev_info_path(const fido_dev_info_t *di)
{
	return (di->path);
}

int16_t
fido_dev_info_vendor(const fido_dev_info_t *di)
{
	return (di->vendor_id);
}

int16_t
fido_dev_info_product(const fido_dev_info_t *di)
{
	return (di->product_id);
}

const char *
fido_dev_info_manufacturer_string(const fido_dev_info_t *di)
{
	return (di->manufacturer);
}

const char *
fido_dev_info_product_string(const fido_dev_info_t *di)
{
	return (di->product);
}
