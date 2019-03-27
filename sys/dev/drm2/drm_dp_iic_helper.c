/*
 * Copyright © 2009 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/kobj.h>
#include <sys/bus.h>
#include <dev/iicbus/iic.h>
#include "iicbus_if.h"
#include <dev/iicbus/iiconf.h>
#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_dp_helper.h>

static int
iic_dp_aux_transaction(device_t idev, int mode, uint8_t write_byte,
    uint8_t *read_byte)
{
	struct iic_dp_aux_data *aux_data;
	int ret;

	aux_data = device_get_softc(idev);
	ret = (*aux_data->aux_ch)(idev, mode, write_byte, read_byte);
	if (ret < 0)
		return (ret);
	return (0);
}

/*
 * I2C over AUX CH
 */

/*
 * Send the address. If the I2C link is running, this 'restarts'
 * the connection with the new address, this is used for doing
 * a write followed by a read (as needed for DDC)
 */
static int
iic_dp_aux_address(device_t idev, u16 address, bool reading)
{
	struct iic_dp_aux_data *aux_data;
	int mode, ret;

	aux_data = device_get_softc(idev);
	mode = MODE_I2C_START;
	if (reading)
		mode |= MODE_I2C_READ;
	else
		mode |= MODE_I2C_WRITE;
	aux_data->address = address;
	aux_data->running = true;
	ret = iic_dp_aux_transaction(idev, mode, 0, NULL);
	return (ret);
}

/*
 * Stop the I2C transaction. This closes out the link, sending
 * a bare address packet with the MOT bit turned off
 */
static void
iic_dp_aux_stop(device_t idev, bool reading)
{
	struct iic_dp_aux_data *aux_data;
	int mode;

	aux_data = device_get_softc(idev);
	mode = MODE_I2C_STOP;
	if (reading)
		mode |= MODE_I2C_READ;
	else
		mode |= MODE_I2C_WRITE;
	if (aux_data->running) {
		(void)iic_dp_aux_transaction(idev, mode, 0, NULL);
		aux_data->running = false;
	}
}

/*
 * Write a single byte to the current I2C address, the
 * the I2C link must be running or this returns -EIO
 */
static int
iic_dp_aux_put_byte(device_t idev, u8 byte)
{
	struct iic_dp_aux_data *aux_data;
	int ret;

	aux_data = device_get_softc(idev);

	if (!aux_data->running)
		return (-EIO);

	ret = iic_dp_aux_transaction(idev, MODE_I2C_WRITE, byte, NULL);
	return (ret);
}

/*
 * Read a single byte from the current I2C address, the
 * I2C link must be running or this returns -EIO
 */
static int
iic_dp_aux_get_byte(device_t idev, u8 *byte_ret)
{
	struct iic_dp_aux_data *aux_data;
	int ret;

	aux_data = device_get_softc(idev);

	if (!aux_data->running)
		return (-EIO);

	ret = iic_dp_aux_transaction(idev, MODE_I2C_READ, 0, byte_ret);
	return (ret);
}

static int
iic_dp_aux_xfer(device_t idev, struct iic_msg *msgs, uint32_t num)
{
	u8 *buf;
	int b, m, ret;
	u16 len;
	bool reading;

	ret = 0;
	reading = false;

	for (m = 0; m < num; m++) {
		len = msgs[m].len;
		buf = msgs[m].buf;
		reading = (msgs[m].flags & IIC_M_RD) != 0;
		ret = iic_dp_aux_address(idev, msgs[m].slave >> 1, reading);
		if (ret < 0)
			break;
		if (reading) {
			for (b = 0; b < len; b++) {
				ret = iic_dp_aux_get_byte(idev, &buf[b]);
				if (ret != 0)
					break;
			}
		} else {
			for (b = 0; b < len; b++) {
				ret = iic_dp_aux_put_byte(idev, buf[b]);
				if (ret < 0)
					break;
			}
		}
		if (ret != 0)
			break;
	}
	iic_dp_aux_stop(idev, reading);
	DRM_DEBUG_KMS("dp_aux_xfer return %d\n", ret);
	return (-ret);
}

static void
iic_dp_aux_reset_bus(device_t idev)
{

	(void)iic_dp_aux_address(idev, 0, false);
	(void)iic_dp_aux_stop(idev, false);
}

static int
iic_dp_aux_reset(device_t idev, u_char speed, u_char addr, u_char *oldaddr)
{

	iic_dp_aux_reset_bus(idev);
	return (0);
}

static int
iic_dp_aux_prepare_bus(device_t idev)
{

	/* adapter->retries = 3; */
	iic_dp_aux_reset_bus(idev);
	return (0);
}

static int
iic_dp_aux_probe(device_t idev)
{

	return (BUS_PROBE_DEFAULT);
}

static int
iic_dp_aux_attach(device_t idev)
{
	struct iic_dp_aux_data *aux_data;

	aux_data = device_get_softc(idev);
	aux_data->port = device_add_child(idev, "iicbus", -1);
	if (aux_data->port == NULL)
		return (ENXIO);
	device_quiet(aux_data->port);
	bus_generic_attach(idev);
	return (0);
}

int
iic_dp_aux_add_bus(device_t dev, const char *name,
    int (*ch)(device_t idev, int mode, uint8_t write_byte, uint8_t *read_byte),
    void *priv, device_t *bus, device_t *adapter)
{
	device_t ibus;
	struct iic_dp_aux_data *data;
	int idx, error;
	static int dp_bus_counter;

	mtx_lock(&Giant);

	idx = atomic_fetchadd_int(&dp_bus_counter, 1);
	ibus = device_add_child(dev, "drm_iic_dp_aux", idx);
	if (ibus == NULL) {
		mtx_unlock(&Giant);
		DRM_ERROR("drm_iic_dp_aux bus %d creation error\n", idx);
		return (-ENXIO);
	}
	device_quiet(ibus);
	error = device_probe_and_attach(ibus);
	if (error != 0) {
		device_delete_child(dev, ibus);
		mtx_unlock(&Giant);
		DRM_ERROR("drm_iic_dp_aux bus %d attach failed, %d\n",
		    idx, error);
		return (-error);
	}
	data = device_get_softc(ibus);
	data->running = false;
	data->address = 0;
	data->aux_ch = ch;
	data->priv = priv;
	error = iic_dp_aux_prepare_bus(ibus);
	if (error == 0) {
		*bus = ibus;
		*adapter = data->port;
	}
	mtx_unlock(&Giant);
	return (-error);
}

static device_method_t drm_iic_dp_aux_methods[] = {
	DEVMETHOD(device_probe,		iic_dp_aux_probe),
	DEVMETHOD(device_attach,	iic_dp_aux_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(iicbus_reset,		iic_dp_aux_reset),
	DEVMETHOD(iicbus_transfer,	iic_dp_aux_xfer),
	DEVMETHOD_END
};
static driver_t drm_iic_dp_aux_driver = {
	"drm_iic_dp_aux",
	drm_iic_dp_aux_methods,
	sizeof(struct iic_dp_aux_data)
};
static devclass_t drm_iic_dp_aux_devclass;
DRIVER_MODULE_ORDERED(drm_iic_dp_aux, drmn, drm_iic_dp_aux_driver,
    drm_iic_dp_aux_devclass, 0, 0, SI_ORDER_SECOND);
