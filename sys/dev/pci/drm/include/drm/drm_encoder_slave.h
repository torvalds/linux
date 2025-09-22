/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __DRM_ENCODER_SLAVE_H__
#define __DRM_ENCODER_SLAVE_H__

#include <linux/i2c.h>

#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>

/**
 * struct drm_encoder_slave_funcs - Entry points exposed by a slave encoder driver
 *
 * Most of its members are analogous to the function pointers in
 * &drm_encoder_helper_funcs and they can optionally be used to
 * initialize the latter. Connector-like methods (e.g. @get_modes and
 * @set_property) will typically be wrapped around and only be called
 * if the encoder is the currently selected one for the connector.
 */
struct drm_encoder_slave_funcs {
	/**
	 * @set_config: Initialize any encoder-specific modesetting parameters.
	 * The meaning of the @params parameter is implementation dependent. It
	 * will usually be a structure with DVO port data format settings or
	 * timings. It's not required for the new parameters to take effect
	 * until the next mode is set.
	 */
	void (*set_config)(struct drm_encoder *encoder,
			   void *params);

	/**
	 * @destroy: Analogous to &drm_encoder_funcs @destroy callback.
	 */
	void (*destroy)(struct drm_encoder *encoder);

	/**
	 * @dpms: Analogous to &drm_encoder_helper_funcs @dpms callback. Wrapped
	 * by drm_i2c_encoder_dpms().
	 */
	void (*dpms)(struct drm_encoder *encoder, int mode);

	/**
	 * @save: Save state. Wrapped by drm_i2c_encoder_save().
	 */
	void (*save)(struct drm_encoder *encoder);

	/**
	 * @restore: Restore state. Wrapped by drm_i2c_encoder_restore().
	 */
	void (*restore)(struct drm_encoder *encoder);

	/**
	 * @mode_fixup: Analogous to &drm_encoder_helper_funcs @mode_fixup
	 * callback. Wrapped by drm_i2c_encoder_mode_fixup().
	 */
	bool (*mode_fixup)(struct drm_encoder *encoder,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);

	/**
	 * @mode_valid: Analogous to &drm_encoder_helper_funcs @mode_valid.
	 */
	int (*mode_valid)(struct drm_encoder *encoder,
			  struct drm_display_mode *mode);
	/**
	 * @mode_set: Analogous to &drm_encoder_helper_funcs @mode_set
	 * callback. Wrapped by drm_i2c_encoder_mode_set().
	 */
	void (*mode_set)(struct drm_encoder *encoder,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode);

	/**
	 * @detect: Analogous to &drm_encoder_helper_funcs @detect
	 * callback. Wrapped by drm_i2c_encoder_detect().
	 */
	enum drm_connector_status (*detect)(struct drm_encoder *encoder,
					    struct drm_connector *connector);
	/**
	 * @get_modes: Get modes.
	 */
	int (*get_modes)(struct drm_encoder *encoder,
			 struct drm_connector *connector);
	/**
	 * @create_resources: Create resources.
	 */
	int (*create_resources)(struct drm_encoder *encoder,
				 struct drm_connector *connector);
	/**
	 * @set_property: Set property.
	 */
	int (*set_property)(struct drm_encoder *encoder,
			    struct drm_connector *connector,
			    struct drm_property *property,
			    uint64_t val);
};

/**
 * struct drm_encoder_slave - Slave encoder struct
 *
 * A &drm_encoder_slave has two sets of callbacks, @slave_funcs and the
 * ones in @base. The former are never actually called by the common
 * CRTC code, it's just a convenience for splitting the encoder
 * functions in an upper, GPU-specific layer and a (hopefully)
 * GPU-agnostic lower layer: It's the GPU driver responsibility to
 * call the slave methods when appropriate.
 *
 * drm_i2c_encoder_init() provides a way to get an implementation of
 * this.
 */
struct drm_encoder_slave {
	/**
	 * @base: DRM encoder object.
	 */
	struct drm_encoder base;

	/**
	 * @slave_funcs: Slave encoder callbacks.
	 */
	const struct drm_encoder_slave_funcs *slave_funcs;

	/**
	 * @slave_priv: Slave encoder private data.
	 */
	void *slave_priv;

	/**
	 * @bus_priv: Bus specific data.
	 */
	void *bus_priv;
};
#define to_encoder_slave(x) container_of((x), struct drm_encoder_slave, base)

#ifdef notyet
int drm_i2c_encoder_init(struct drm_device *dev,
			 struct drm_encoder_slave *encoder,
			 struct i2c_adapter *adap,
			 const struct i2c_board_info *info);
#endif

/**
 * struct drm_i2c_encoder_driver
 *
 * Describes a device driver for an encoder connected to the GPU through an I2C
 * bus.
 */
struct drm_i2c_encoder_driver {
#ifdef notyet
	/**
	 * @i2c_driver: I2C device driver description.
	 */
	struct i2c_driver i2c_driver;

	/**
	 * @encoder_init: Callback to allocate any per-encoder data structures
	 * and to initialize the @slave_funcs and (optionally) @slave_priv
	 * members of @encoder.
	 */
	int (*encoder_init)(struct i2c_client *client,
			    struct drm_device *dev,
			    struct drm_encoder_slave *encoder);
#endif

};
#define to_drm_i2c_encoder_driver(x) container_of((x),			\
						  struct drm_i2c_encoder_driver, \
						  i2c_driver)

/**
 * drm_i2c_encoder_get_client - Get the I2C client corresponding to an encoder
 * @encoder: The encoder
 */
static inline struct i2c_client *drm_i2c_encoder_get_client(struct drm_encoder *encoder)
{
	return (struct i2c_client *)to_encoder_slave(encoder)->bus_priv;
}

/**
 * drm_i2c_encoder_register - Register an I2C encoder driver
 * @owner:	Module containing the driver.
 * @driver:	Driver to be registered.
 */
static inline int drm_i2c_encoder_register(struct module *owner,
					   struct drm_i2c_encoder_driver *driver)
{
	STUB();
	return -ENOSYS;
#ifdef notyet
	return i2c_register_driver(owner, &driver->i2c_driver);
#endif
}

/**
 * drm_i2c_encoder_unregister - Unregister an I2C encoder driver
 * @driver:	Driver to be unregistered.
 */
static inline void drm_i2c_encoder_unregister(struct drm_i2c_encoder_driver *driver)
{
	STUB();
#ifdef notyet
	i2c_del_driver(&driver->i2c_driver);
#endif
}

void drm_i2c_encoder_destroy(struct drm_encoder *encoder);


/*
 * Wrapper fxns which can be plugged in to drm_encoder_helper_funcs:
 */

void drm_i2c_encoder_dpms(struct drm_encoder *encoder, int mode);
bool drm_i2c_encoder_mode_fixup(struct drm_encoder *encoder,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode);
void drm_i2c_encoder_prepare(struct drm_encoder *encoder);
void drm_i2c_encoder_commit(struct drm_encoder *encoder);
void drm_i2c_encoder_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode);
enum drm_connector_status drm_i2c_encoder_detect(struct drm_encoder *encoder,
	    struct drm_connector *connector);
void drm_i2c_encoder_save(struct drm_encoder *encoder);
void drm_i2c_encoder_restore(struct drm_encoder *encoder);


#endif
