/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 * Copyright (C) 2025 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __SDCA_UMP_H__
#define __SDCA_UMP_H__

struct regmap;
struct sdca_control;
struct sdca_entity;
struct sdca_function_data;
struct snd_soc_component;
struct delayed_work;

int sdca_ump_get_owner_host(struct device *dev,
			    struct regmap *function_regmap,
			    struct sdca_function_data *function,
			    struct sdca_entity *entity,
			    struct sdca_control *control);
int sdca_ump_set_owner_device(struct device *dev,
			      struct regmap *function_regmap,
			      struct sdca_function_data *function,
			      struct sdca_entity *entity,
			      struct sdca_control *control);
int sdca_ump_read_message(struct device *dev,
			  struct regmap *device_regmap,
			  struct regmap *function_regmap,
			  struct sdca_function_data *function,
			  struct sdca_entity *entity,
			  unsigned int offset_sel, unsigned int length_sel,
			  void **msg);
int sdca_ump_write_message(struct device *dev,
			   struct regmap *device_regmap,
			   struct regmap *function_regmap,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   unsigned int offset_sel, unsigned int msg_offset,
			   unsigned int length_sel,
			   void *msg, int msg_len);

void sdca_ump_cancel_timeout(struct delayed_work *work);
void sdca_ump_schedule_timeout(struct delayed_work *work,
			       unsigned int timeout_us);

#endif // __SDCA_UMP_H__
