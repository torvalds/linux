#-
# Copyright (c) 2004 Nate Lawson
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
#include <sys/types.h>

#include <contrib/dev/acpica/include/acpi.h>

INTERFACE acpi;

#
# Callback function for each child handle traversed in acpi_scan_children().
#
# ACPI_HANDLE h:  current child device being considered
#
# device_t *dev:  pointer to the child's original device_t or NULL if there
#   was none.  The callback should store a new device in *dev if it has
#   created one.  The method implementation will automatically clean up the
#   previous device and properly associate the current ACPI_HANDLE with it.
#
# level:  current level being scanned
#
# void *arg:  argument passed in original call to acpi_scan_children()
#
# Returns:  AE_OK if the scan should continue, otherwise an error
#
HEADER {
	typedef ACPI_STATUS (*acpi_scan_cb_t)(ACPI_HANDLE h, device_t *dev,
	    int level, void *arg);

	struct acpi_bif;
	struct acpi_bst;
};

#
# Default implementation for acpi_id_probe().
#
CODE {
	static char *
	acpi_generic_id_probe(device_t bus, device_t dev, char **ids)
	{
		return (NULL);
	}
};

#
# Check a device for a match in a list of ID strings.  The strings can be
# EISA PNP IDs or ACPI _HID/_CID values.
#
# device_t bus:  parent bus for the device
#
# device_t dev:  device being considered
#
# char **ids:  array of ID strings to consider
#
# char **match:  Pointer to store ID string matched or NULL if no match
#                pass NULL if not needed.
#
# Returns: BUS_PROBE_DEFAULT if _HID match
#          BUS_PROBE_LOW_PRIORITY  if _CID match and not _HID match
#          ENXIO if no match.
#

METHOD int id_probe {
	device_t	bus;
	device_t	dev;
	char		**ids;
	char 		**match;
} DEFAULT acpi_generic_id_probe;

#
# Evaluate an ACPI method or object, given its path.
#
# device_t bus:  parent bus for the device
#
# device_t dev:  evaluate the object relative to this device's handle.
#   Specify NULL to begin the search at the ACPI root.
#
# ACPI_STRING pathname:  absolute or relative path to this object
#
# ACPI_OBJECT_LIST *parameters:  array of arguments to pass to the object.
#   Specify NULL if there are none.
#
# ACPI_BUFFER *ret:  the result (if any) of the evaluation
#   Specify NULL if there is none.
#
# Returns:  AE_OK or an error value
#
METHOD ACPI_STATUS evaluate_object {
	device_t	bus;
	device_t	dev;
	ACPI_STRING 	pathname;
	ACPI_OBJECT_LIST *parameters;
	ACPI_BUFFER	*ret;
};

#
# Get the highest power state (D0-D3) that is usable for a device when
# suspending/resuming.  If a bus calls this when suspending a device, it
# must also call it when resuming.
#
# device_t bus:  parent bus for the device
#
# device_t dev:  check this device's appropriate power state
#
# int *dstate:  if successful, contains the highest valid sleep state
#
# Returns:  0 on success or some other error value.
#
METHOD int pwr_for_sleep {
	device_t	bus;
	device_t	dev;
	int		*dstate;
};

#
# Rescan a subtree and optionally reattach devices to handles.  Users
# specify a callback that is called for each ACPI_HANDLE of type Device
# that is a child of "dev".
#
# device_t bus:  parent bus for the device
#
# device_t dev:  begin the scan starting with this device's handle.
#   Specify NULL to begin the scan at the ACPI root.
# 
# int max_depth:  number of levels to traverse (i.e., 1 means just the
#   immediate children.
#
# acpi_scan_cb_t user_fn:  called for each child handle
#
# void *arg:  argument to pass to the callback function
#
# Returns:  AE_OK or an error value, based on the callback return value
#
METHOD ACPI_STATUS scan_children {
	device_t	bus;
	device_t	dev;
	int		max_depth;
	acpi_scan_cb_t	user_fn;
	void		*arg;
};

#
# Query a given driver for its supported feature(s).  This should be
# called by the parent bus before the driver is probed.
#
# driver_t *driver:  child driver
#
# u_int *features:  returned bitmask of all supported features
#
STATICMETHOD int get_features {
	driver_t	*driver;
	u_int		*features;
};

#
# Read embedded controller (EC) address space
#
# device_t dev:  EC device
# u_int addr:  Address to read from in EC space
# UINT64 *val:  Location to store read value
# int width:  Size of area to read in bytes
#
METHOD int ec_read {
	device_t	dev;
	u_int		addr;
	UINT64		*val;
	int		width;
};

#
# Write embedded controller (EC) address space
#
# device_t dev:  EC device
# u_int addr:  Address to write to in EC space
# UINT64 val:  Value to write
# int width:  Size of value to write in bytes
#
METHOD int ec_write {
	device_t	dev;
	u_int		addr;
	UINT64		val;
	int		width;
};

#
# Get battery information (_BIF format)
#
# device_t dev:  Battery device
# struct acpi_bif *bif:  Pointer to storage for _BIF results
#
METHOD int batt_get_info {
	device_t	dev;
	struct acpi_bif	*bif;
};

#
# Get battery status (_BST format)
#
# device_t dev:  Battery device
# struct acpi_bst *bst:  Pointer to storage for _BST results
#
METHOD int batt_get_status {
	device_t	dev;
	struct acpi_bst	*bst;
};
