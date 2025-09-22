/*	$OpenBSD: ofw_regulator.h,v 1.9 2024/06/14 20:00:32 kettenis Exp $	*/
/*
 * Copyright (c) 2016 Mark Kettenis
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

#ifndef _DEV_OFW_REGULATOR_H_
#define _DEV_OFW_REGULATOR_H_

struct regulator_device {
	int	rd_node;
	void	*rd_cookie;
	uint32_t (*rd_get_voltage)(void *);
	int	(*rd_set_voltage)(void *, uint32_t);
	uint32_t (*rd_get_current)(void *);
	int	(*rd_set_current)(void *, uint32_t);
	int	(*rd_enable)(void *, int);

	uint32_t rd_volt_min, rd_volt_max;
	uint32_t rd_amp_min, rd_amp_max;
	uint32_t rd_ramp_delay;

	uint32_t rd_coupled;
	uint32_t rd_max_spread;

	LIST_ENTRY(regulator_device) rd_list;
	uint32_t rd_phandle;
};

void	regulator_register(struct regulator_device *);

int	regulator_enable(uint32_t);
int	regulator_disable(uint32_t);
uint32_t regulator_get_voltage(uint32_t);
int	regulator_set_voltage(uint32_t, uint32_t);
uint32_t regulator_get_current(uint32_t);
int	regulator_set_current(uint32_t, uint32_t);

struct regulator_notifier {
	uint32_t rn_phandle;
	void	*rn_cookie;
	void	(*rn_notify)(void *, uint32_t);

	LIST_ENTRY(regulator_notifier) rn_list;
};

void	regulator_notify(struct regulator_notifier *);

#endif /* _DEV_OFW_REGULATOR_H_ */
