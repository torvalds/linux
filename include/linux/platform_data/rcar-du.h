/*
 * rcar_du.h  --  R-Car Display Unit DRM driver
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_DU_H__
#define __RCAR_DU_H__

#include <drm/drm_mode.h>

enum rcar_du_encoder_type {
	RCAR_DU_ENCODER_UNUSED = 0,
	RCAR_DU_ENCODER_VGA,
	RCAR_DU_ENCODER_LVDS,
};

struct rcar_du_panel_data {
	unsigned int width_mm;		/* Panel width in mm */
	unsigned int height_mm;		/* Panel height in mm */
	struct drm_mode_modeinfo mode;
};

struct rcar_du_connector_lvds_data {
	struct rcar_du_panel_data panel;
};

struct rcar_du_connector_vga_data {
	/* TODO: Add DDC information for EDID retrieval */
};

/*
 * struct rcar_du_encoder_data - Encoder platform data
 * @type: the encoder type (RCAR_DU_ENCODER_*)
 * @output: the DU output the connector is connected to
 * @connector.lvds: platform data for LVDS connectors
 * @connector.vga: platform data for VGA connectors
 */
struct rcar_du_encoder_data {
	enum rcar_du_encoder_type type;
	unsigned int output;

	union {
		struct rcar_du_connector_lvds_data lvds;
		struct rcar_du_connector_vga_data vga;
	} connector;
};

struct rcar_du_platform_data {
	struct rcar_du_encoder_data *encoders;
	unsigned int num_encoders;
};

#endif /* __RCAR_DU_H__ */
