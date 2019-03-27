#-
# Copyright (c) 1998-2004 Doug Rabson
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#include <sys/bus.h>

/**
 * @defgroup DEVICE device - KObj methods for all device drivers
 * @brief A basic set of methods required for all device drivers.
 *
 * The device interface is used to match devices to drivers during
 * autoconfiguration and provides methods to allow drivers to handle
 * system-wide events such as suspend, resume or shutdown.
 * @{
 */
INTERFACE device;

# Needed for timestamping device probe/attach calls
HEADER {
	#include <sys/tslog.h>
}

#
# Default implementations of some methods.
#
CODE {
	static int null_shutdown(device_t dev)
	{
	    return 0;
	}

	static int null_suspend(device_t dev)
	{
	    return 0;
	}

	static int null_resume(device_t dev)
	{
	    return 0;
	}

	static int null_quiesce(device_t dev)
	{
	    return 0;
	}

	static void * null_register(device_t dev)
	{
		return NULL;
	}
};
	
/**
 * @brief Probe to see if a device matches a driver.
 *
 * Users should not call this method directly. Normally, this
 * is called via device_probe_and_attach() to select a driver
 * calling the DEVICE_PROBE() of all candidate drivers and attach
 * the winning driver (if any) to the device.
 *
 * This function is used to match devices to device drivers.
 * Typically, the driver will examine the device to see if
 * it is suitable for this driver. This might include checking
 * the values of various device instance variables or reading
 * hardware registers.
 *  
 * In some cases, there may be more than one driver available
 * which can be used for a device (for instance there might
 * be a generic driver which works for a set of many types of
 * device and a more specific driver which works for a subset
 * of devices). Because of this, a driver should not assume
 * that it will be the driver that attaches to the device even
 * if it returns a success status from DEVICE_PROBE(). In particular,
 * a driver must free any resources which it allocated during
 * the probe before returning. The return value of DEVICE_PROBE()
 * is used to elect which driver is used - the driver which returns
 * the largest non-error value wins the election and attaches to
 * the device. Common non-error values are described in the
 * DEVICE_PROBE(9) manual page.
 *
 * If a driver matches the hardware, it should set the device
 * description string using device_set_desc() or
 * device_set_desc_copy(). This string is used to generate an
 * informative message when DEVICE_ATTACH() is called.
 * 
 * As a special case, if a driver returns zero, the driver election
 * is cut short and that driver will attach to the device
 * immediately. This should rarely be used.
 *
 * For example, a probe method for a PCI device driver might look
 * like this:
 *
 * @code
 * int
 * foo_probe(device_t dev)
 * {
 *         if (pci_get_vendor(dev) == FOOVENDOR &&
 *             pci_get_device(dev) == FOODEVICE) {
 *                 device_set_desc(dev, "Foo device");
 *                 return (BUS_PROBE_DEFAULT);
 *         }
 *         return (ENXIO);
 * }
 * @endcode
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_probe, foo_probe)
 * @endcode
 *
 * @param dev		the device to probe
 *
 * @retval 0		if this is the only possible driver for this
 *			device
 * @retval negative	if the driver can match this device - the
 *			least negative value is used to select the
 *			driver
 * @retval ENXIO	if the driver does not match the device
 * @retval positive	if some kind of error was detected during
 *			the probe, a regular unix error code should
 *			be returned to indicate the type of error
 * @see DEVICE_ATTACH(), pci_get_vendor(), pci_get_device()
 */
PROLOG {
	TSENTER2(device_get_name(dev));
}
EPILOG {
	TSEXIT2(device_get_name(dev));
}
METHOD int probe {
	device_t dev;
};

/**
 * @brief Allow a device driver to detect devices not otherwise enumerated.
 *
 * The DEVICE_IDENTIFY() method is used by some drivers (e.g. the ISA
 * bus driver) to help populate the bus device with a useful set of
 * child devices, normally by calling the BUS_ADD_CHILD() method of
 * the parent device. For instance, the ISA bus driver uses several
 * special drivers, including the isahint driver and the pnp driver to
 * create child devices based on configuration hints and PnP bus
 * probes respectively.
 *
 * Many bus drivers which support true plug-and-play do not need to
 * use this method at all since child devices can be discovered
 * automatically without help from child drivers.
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_identify, foo_identify)
 * @endcode
 *
 * @param driver	the driver whose identify method is being called
 * @param parent	the parent device to use when adding new children
 */
STATICMETHOD void identify {
	driver_t *driver;
	device_t parent;
};

/**
 * @brief Attach a device to a device driver
 *
 * Normally only called via device_probe_and_attach(), this is called
 * when a driver has succeeded in probing against a device.
 * This method should initialise the hardware and allocate other
 * system resources (e.g. devfs entries) as required.
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_attach, foo_attach)
 * @endcode
 *
 * @param dev		the device to probe
 *
 * @retval 0		success
 * @retval non-zero	if some kind of error was detected during
 *			the attach, a regular unix error code should
 *			be returned to indicate the type of error
 * @see DEVICE_PROBE()
 */
PROLOG {
	TSENTER2(device_get_name(dev));
}
EPILOG {
	TSEXIT2(device_get_name(dev));
}
METHOD int attach {
	device_t dev;
};

/**
 * @brief Detach a driver from a device.
 *
 * This can be called if the user is replacing the
 * driver software or if a device is about to be physically removed
 * from the system (e.g. for removable hardware such as USB or PCCARD).
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_detach, foo_detach)
 * @endcode
 *
 * @param dev		the device to detach
 *
 * @retval 0		success
 * @retval non-zero	the detach could not be performed, e.g. if the
 *			driver does not support detaching.
 *
 * @see DEVICE_ATTACH()
 */
METHOD int detach {
	device_t dev;
};

/**
 * @brief Called during system shutdown.
 *
 * This method allows drivers to detect when the system is being shut down.
 * Some drivers need to use this to place their hardware in a consistent
 * state before rebooting the computer.
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_shutdown, foo_shutdown)
 * @endcode
 */
METHOD int shutdown {
	device_t dev;
} DEFAULT null_shutdown;

/**
 * @brief This is called by the power-management subsystem when a
 * suspend has been requested by the user or by some automatic
 * mechanism.
 *
 * This gives drivers a chance to veto the suspend or save their
 * configuration before power is removed.
 *
 * To include this method in a device driver, use a line like this in
 * the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_suspend, foo_suspend)
 * @endcode
 *
 * @param dev		the device being suspended
 *
 * @retval 0		success
 * @retval non-zero	an error occurred while attempting to prepare the
 *                      device for suspension
 *
 * @see DEVICE_RESUME()
 */
METHOD int suspend {
	device_t dev;
} DEFAULT null_suspend;

/**
 * @brief This is called when the system resumes after a suspend.
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_resume, foo_resume)
 * @endcode
 *
 * @param dev		the device being resumed
 *
 * @retval 0		success
 * @retval non-zero	an error occurred while attempting to restore the
 *                      device from suspension
 *
 * @see DEVICE_SUSPEND()
 */
METHOD int resume {
	device_t dev;
} DEFAULT null_resume;

/**
 * @brief This is called when the driver is asked to quiesce itself.
 *
 * The driver should arrange for the orderly shutdown of this device.
 * All further access to the device should be curtailed.  Soon there
 * will be a request to detach, but there won't necessarily be one.
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_quiesce, foo_quiesce)
 * @endcode
 *
 * @param dev		the device being quiesced
 *
 * @retval 0		success
 * @retval non-zero	an error occurred while attempting to quiesce the
 *                      device
 *
 * @see DEVICE_DETACH()
 */
METHOD int quiesce {
	device_t dev;
} DEFAULT null_quiesce;

/**
 * @brief This is called when the driver is asked to register handlers.
 *
 *
 * To include this method in a device driver, use a line like this
 * in the driver's method list:
 *
 * @code
 * 	KOBJMETHOD(device_register, foo_register)
 * @endcode
 *
 * @param dev		the device for which handlers are being registered
 *
 * @retval NULL     method not implemented
 * @retval non-NULL	a pointer to implementation specific static driver state
 *
 */
METHOD void * register {
	device_t dev;
} DEFAULT null_register;
