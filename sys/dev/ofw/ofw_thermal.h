/*	$OpenBSD: ofw_thermal.h,v 1.3 2024/06/27 09:37:07 kettenis Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DEV_OFW_THERMAL_H_
#define _DEV_OFW_THERMAL_H_

struct thermal_sensor {
	int	ts_node;
	void	*ts_cookie;

	int32_t	(*ts_get_temperature)(void *, uint32_t *);
	int	(*ts_set_limit)(void *, uint32_t *, uint32_t);

	LIST_ENTRY(thermal_sensor) ts_list;
	uint32_t ts_phandle;
	uint32_t ts_cells;
};

#define THERMAL_SENSOR_MAX	0xffffffffU

struct cooling_device {
	int	cd_node;
	void	*cd_cookie;

	uint32_t (*cd_get_level)(void *, uint32_t *);
	void	(*cd_set_level)(void *, uint32_t *, uint32_t);

	LIST_ENTRY(cooling_device) cd_list;
	uint32_t cd_phandle;
	uint32_t cd_cells;
};

#define THERMAL_NO_LIMIT	0xffffffffU

void	thermal_sensor_register(struct thermal_sensor *);
void	thermal_sensor_update(struct thermal_sensor *, uint32_t *);
void	cooling_device_register(struct cooling_device *);

void	thermal_init(void);

#endif /* _DEV_OFW_THERMAL_H_ */
