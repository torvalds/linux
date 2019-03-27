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

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/bus.h>

/**
 * @defgroup BUS bus - KObj methods for drivers of devices with children
 * @brief A set of methods required device drivers that support
 * child devices.
 * @{
 */
INTERFACE bus;

#
# Default implementations of some methods.
#
CODE {
	static struct resource *
	null_alloc_resource(device_t dev, device_t child,
	    int type, int *rid, rman_res_t start, rman_res_t end,
	    rman_res_t count, u_int flags)
	{
	    return (0);
	}

	static int
	null_remap_intr(device_t bus, device_t dev, u_int irq)
	{

		if (dev != NULL)
			return (BUS_REMAP_INTR(dev, NULL, irq));
		return (ENXIO);
	}

	static device_t
	null_add_child(device_t bus, int order, const char *name,
	    int unit)
	{

		panic("bus_add_child is not implemented");
	}
};

/**
 * @brief Print a description of a child device
 *
 * This is called from system code which prints out a description of a
 * device. It should describe the attachment that the child has with
 * the parent. For instance the TurboLaser bus prints which node the
 * device is attached to. See bus_generic_print_child() for more 
 * information.
 *
 * @param _dev		the device whose child is being printed
 * @param _child	the child device to describe
 *
 * @returns		the number of characters output.
 */
METHOD int print_child {
	device_t _dev;
	device_t _child;
} DEFAULT bus_generic_print_child;

/**
 * @brief Print a notification about an unprobed child device.
 *
 * Called for each child device that did not succeed in probing for a
 * driver.
 *
 * @param _dev		the device whose child was being probed
 * @param _child	the child device which failed to probe
 */   
METHOD void probe_nomatch {
        device_t _dev;
        device_t _child;
};

/**
 * @brief Read the value of a bus-specific attribute of a device
 *
 * This method, along with BUS_WRITE_IVAR() manages a bus-specific set
 * of instance variables of a child device.  The intention is that
 * each different type of bus defines a set of appropriate instance
 * variables (such as ports and irqs for ISA bus etc.)
 *
 * This information could be given to the child device as a struct but
 * that makes it hard for a bus to add or remove variables without
 * forcing an edit and recompile for all drivers which may not be
 * possible for vendor supplied binary drivers.
 *
 * This method copies the value of an instance variable to the
 * location specified by @p *_result.
 * 
 * @param _dev		the device whose child was being examined
 * @param _child	the child device whose instance variable is
 *			being read
 * @param _index	the instance variable to read
 * @param _result	a location to receive the instance variable
 *			value
 * 
 * @retval 0		success
 * @retval ENOENT	no such instance variable is supported by @p
 *			_dev 
 */
METHOD int read_ivar {
	device_t _dev;
	device_t _child;
	int _index;
	uintptr_t *_result;
};

/**
 * @brief Write the value of a bus-specific attribute of a device
 * 
 * This method sets the value of an instance variable to @p _value.
 * 
 * @param _dev		the device whose child was being updated
 * @param _child	the child device whose instance variable is
 *			being written
 * @param _index	the instance variable to write
 * @param _value	the value to write to that instance variable
 * 
 * @retval 0		success
 * @retval ENOENT	no such instance variable is supported by @p
 *			_dev 
 * @retval EINVAL	the instance variable was recognised but
 *			contains a read-only value
 */
METHOD int write_ivar {
	device_t _dev;
	device_t _child;
	int _indx;
	uintptr_t _value;
};

/**
 * @brief Notify a bus that a child was deleted
 *
 * Called at the beginning of device_delete_child() to allow the parent
 * to teardown any bus-specific state for the child.
 * 
 * @param _dev		the device whose child is being deleted
 * @param _child	the child device which is being deleted
 */
METHOD void child_deleted {
	device_t _dev;
	device_t _child;
};

/**
 * @brief Notify a bus that a child was detached
 *
 * Called after the child's DEVICE_DETACH() method to allow the parent
 * to reclaim any resources allocated on behalf of the child.
 * 
 * @param _dev		the device whose child changed state
 * @param _child	the child device which changed state
 */
METHOD void child_detached {
	device_t _dev;
	device_t _child;
};

/**
 * @brief Notify a bus that a new driver was added
 * 
 * Called when a new driver is added to the devclass which owns this
 * bus. The generic implementation of this method attempts to probe and
 * attach any un-matched children of the bus.
 * 
 * @param _dev		the device whose devclass had a new driver
 *			added to it
 * @param _driver	the new driver which was added
 */
METHOD void driver_added {
	device_t _dev;
	driver_t *_driver;
} DEFAULT bus_generic_driver_added;

/**
 * @brief Create a new child device
 *
 * For buses which use use drivers supporting DEVICE_IDENTIFY() to
 * enumerate their devices, this method is used to create new
 * device instances. The new device will be added after the last
 * existing child with the same order. Implementations of bus_add_child
 * call device_add_child_ordered to add the child and often add
 * a suitable ivar to the device specific to that bus.
 * 
 * @param _dev		the bus device which will be the parent of the
 *			new child device
 * @param _order	a value which is used to partially sort the
 *			children of @p _dev - devices created using
 *			lower values of @p _order appear first in @p
 *			_dev's list of children
 * @param _name		devclass name for new device or @c NULL if not
 *			specified
 * @param _unit		unit number for new device or @c -1 if not
 *			specified
 */
METHOD device_t add_child {
	device_t _dev;
	u_int _order;
	const char *_name;
	int _unit;
} DEFAULT null_add_child;

/**
 * @brief Rescan the bus
 *
 * This method is called by a parent bridge or devctl to trigger a bus
 * rescan.  The rescan should delete devices no longer present and
 * enumerate devices that have newly arrived.
 *
 * @param _dev		the bus device
 */
METHOD int rescan {
	device_t _dev;
}

/**
 * @brief Allocate a system resource
 *
 * This method is called by child devices of a bus to allocate resources.
 * The types are defined in <machine/resource.h>; the meaning of the
 * resource-ID field varies from bus to bus (but @p *rid == 0 is always
 * valid if the resource type is). If a resource was allocated and the
 * caller did not use the RF_ACTIVE to specify that it should be
 * activated immediately, the caller is responsible for calling
 * BUS_ACTIVATE_RESOURCE() when it actually uses the resource.
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device which is requesting an allocation
 * @param _type		the type of resource to allocate
 * @param _rid		a pointer to the resource identifier
 * @param _start	hint at the start of the resource range - pass
 *			@c 0 for any start address
 * @param _end		hint at the end of the resource range - pass
 *			@c ~0 for any end address
 * @param _count	hint at the size of range required - pass @c 1
 *			for any size
 * @param _flags	any extra flags to control the resource
 *			allocation - see @c RF_XXX flags in
 *			<sys/rman.h> for details
 * 
 * @returns		the resource which was allocated or @c NULL if no
 *			resource could be allocated
 */
METHOD struct resource * alloc_resource {
	device_t	_dev;
	device_t	_child;
	int		_type;
	int	       *_rid;
	rman_res_t	_start;
	rman_res_t	_end;
	rman_res_t	_count;
	u_int		_flags;
} DEFAULT null_alloc_resource;

/**
 * @brief Activate a resource
 *
 * Activate a resource previously allocated with
 * BUS_ALLOC_RESOURCE().  This may enable decoding of this resource in a
 * device for instance.  It will also establish a mapping for the resource
 * unless RF_UNMAPPED was set when allocating the resource.
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _type		the type of resource
 * @param _rid		the resource identifier
 * @param _r		the resource to activate
 */
METHOD int activate_resource {
	device_t	_dev;
	device_t	_child;
	int		_type;
	int		_rid;
	struct resource *_r;
};


/**
 * @brief Map a resource
 *
 * Allocate a mapping for a range of an active resource.  The mapping
 * is described by a struct resource_map object.  This may for instance
 * map a memory region into the kernel's virtual address space.
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _type		the type of resource
 * @param _r		the resource to map
 * @param _args		optional attributes of the mapping
 * @param _map		the mapping
 */
METHOD int map_resource {
	device_t	_dev;
	device_t	_child;
	int		_type;
	struct resource *_r;
	struct resource_map_request *_args;
	struct resource_map *_map;
} DEFAULT bus_generic_map_resource;


/**
 * @brief Unmap a resource
 *
 * Release a mapping previously allocated with
 * BUS_MAP_RESOURCE(). This may for instance unmap a memory region
 * from the kernel's virtual address space.
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _type		the type of resource
 * @param _r		the resource
 * @param _map		the mapping to release
 */
METHOD int unmap_resource {
	device_t	_dev;
	device_t	_child;
	int		_type;
	struct resource *_r;
	struct resource_map *_map;
} DEFAULT bus_generic_unmap_resource;


/**
 * @brief Deactivate a resource
 *
 * Deactivate a resource previously allocated with
 * BUS_ALLOC_RESOURCE(). 
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _type		the type of resource
 * @param _rid		the resource identifier
 * @param _r		the resource to deactivate
 */
METHOD int deactivate_resource {
	device_t	_dev;
	device_t	_child;
	int		_type;
	int		_rid;
	struct resource *_r;
};

/**
 * @brief Adjust a resource
 *
 * Adjust the start and/or end of a resource allocated by
 * BUS_ALLOC_RESOURCE.  At least part of the new address range must overlap
 * with the existing address range.  If the successful, the resource's range
 * will be adjusted to [start, end] on return.
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _type		the type of resource
 * @param _res		the resource to adjust
 * @param _start	the new starting address of the resource range
 * @param _end		the new ending address of the resource range
 */
METHOD int adjust_resource {
	device_t	_dev;
	device_t	_child;
	int		_type;
	struct resource *_res;
	rman_res_t	_start;
	rman_res_t	_end;
};

/**
 * @brief Release a resource
 *
 * Free a resource allocated by the BUS_ALLOC_RESOURCE.  The @p _rid
 * value must be the same as the one returned by BUS_ALLOC_RESOURCE()
 * (which is not necessarily the same as the one the client passed).
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _type		the type of resource
 * @param _rid		the resource identifier
 * @param _r		the resource to release
 */
METHOD int release_resource {
	device_t	_dev;
	device_t	_child;
	int		_type;
	int		_rid;
	struct resource *_res;
};

/**
 * @brief Install an interrupt handler
 *
 * This method is used to associate an interrupt handler function with
 * an irq resource. When the interrupt triggers, the function @p _intr
 * will be called with the value of @p _arg as its single
 * argument. The value returned in @p *_cookiep is used to cancel the
 * interrupt handler - the caller should save this value to use in a
 * future call to BUS_TEARDOWN_INTR().
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _irq		the resource representing the interrupt
 * @param _flags	a set of bits from enum intr_type specifying
 *			the class of interrupt
 * @param _intr		the function to call when the interrupt
 *			triggers
 * @param _arg		a value to use as the single argument in calls
 *			to @p _intr
 * @param _cookiep	a pointer to a location to receive a cookie
 *			value that may be used to remove the interrupt
 *			handler
 */
METHOD int setup_intr {
	device_t	_dev;
	device_t	_child;
	struct resource *_irq;
	int		_flags;
	driver_filter_t	*_filter;
	driver_intr_t	*_intr;
	void		*_arg;
	void		**_cookiep;
};

/**
 * @brief Uninstall an interrupt handler
 *
 * This method is used to disassociate an interrupt handler function
 * with an irq resource. The value of @p _cookie must be the value
 * returned from a previous call to BUS_SETUP_INTR().
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _irq		the resource representing the interrupt
 * @param _cookie	the cookie value returned when the interrupt
 *			was originally registered
 */
METHOD int teardown_intr {
	device_t	_dev;
	device_t	_child;
	struct resource	*_irq;
	void		*_cookie;
};

/**
 * @brief Suspend an interrupt handler
 *
 * This method is used to mark a handler as suspended in the case
 * that the associated device is powered down and cannot be a source
 * for the, typically shared, interrupt.
 * The value of @p _irq must be the interrupt resource passed
 * to a previous call to BUS_SETUP_INTR().
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _irq		the resource representing the interrupt
 */
METHOD int suspend_intr {
	device_t	_dev;
	device_t	_child;
	struct resource *_irq;
} DEFAULT bus_generic_suspend_intr;

/**
 * @brief Resume an interrupt handler
 *
 * This method is used to clear suspended state of a handler when
 * the associated device is powered up and can be an interrupt source
 * again.
 * The value of @p _irq must be the interrupt resource passed
 * to a previous call to BUS_SETUP_INTR().
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _irq		the resource representing the interrupt
 */
METHOD int resume_intr {
	device_t	_dev;
	device_t	_child;
	struct resource *_irq;
} DEFAULT bus_generic_resume_intr;

/**
 * @brief Define a resource which can be allocated with
 * BUS_ALLOC_RESOURCE().
 *
 * This method is used by some buses (typically ISA) to allow a
 * driver to describe a resource range that it would like to
 * allocate. The resource defined by @p _type and @p _rid is defined
 * to start at @p _start and to include @p _count indices in its
 * range.
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which owns the resource
 * @param _type		the type of resource
 * @param _rid		the resource identifier
 * @param _start	the start of the resource range
 * @param _count	the size of the resource range
 */
METHOD int set_resource {
	device_t	_dev;
	device_t	_child;
	int		_type;
	int		_rid;
	rman_res_t	_start;
	rman_res_t	_count;
};

/**
 * @brief Describe a resource
 *
 * This method allows a driver to examine the range used for a given
 * resource without actually allocating it.
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which owns the resource
 * @param _type		the type of resource
 * @param _rid		the resource identifier
 * @param _start	the address of a location to receive the start
 *			index of the resource range
 * @param _count	the address of a location to receive the size
 *			of the resource range
 */
METHOD int get_resource {
	device_t	_dev;
	device_t	_child;
	int		_type;
	int		_rid;
	rman_res_t	*_startp;
	rman_res_t	*_countp;
};

/**
 * @brief Delete a resource.
 * 
 * Use this to delete a resource (possibly one previously added with
 * BUS_SET_RESOURCE()).
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which owns the resource
 * @param _type		the type of resource
 * @param _rid		the resource identifier
 */
METHOD void delete_resource {
	device_t	_dev;
	device_t	_child;
	int		_type;
	int		_rid;
};

/**
 * @brief Return a struct resource_list.
 *
 * Used by drivers which use bus_generic_rl_alloc_resource() etc. to
 * implement their resource handling. It should return the resource
 * list of the given child device.
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which owns the resource list
 */
METHOD struct resource_list * get_resource_list {
	device_t	_dev;
	device_t	_child;
} DEFAULT bus_generic_get_resource_list;

/**
 * @brief Is the hardware described by @p _child still attached to the
 * system?
 *
 * This method should return 0 if the device is not present.  It
 * should return -1 if it is present.  Any errors in determining
 * should be returned as a normal errno value.  Client drivers are to
 * assume that the device is present, even if there is an error
 * determining if it is there.  Buses are to try to avoid returning
 * errors, but newcard will return an error if the device fails to
 * implement this method.
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which is being examined
 */
METHOD int child_present {
	device_t	_dev;
	device_t	_child;
} DEFAULT bus_generic_child_present;

/**
 * @brief Returns the pnp info for this device.
 *
 * Return it as a string.  If the storage is insufficient for the
 * string, then return EOVERFLOW.
 *
 * The string must be formatted as a space-separated list of
 * name=value pairs.  Names may only contain alphanumeric characters,
 * underscores ('_') and hyphens ('-').  Values can contain any
 * non-whitespace characters.  Values containing whitespace can be
 * quoted with double quotes ('"').  Double quotes and backslashes in
 * quoted values can be escaped with backslashes ('\').
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which is being examined
 * @param _buf		the address of a buffer to receive the pnp
 *			string
 * @param _buflen	the size of the buffer pointed to by @p _buf
 */
METHOD int child_pnpinfo_str {
	device_t	_dev;
	device_t	_child;
	char		*_buf;
	size_t		_buflen;
};

/**
 * @brief Returns the location for this device.
 *
 * Return it as a string.  If the storage is insufficient for the
 * string, then return EOVERFLOW.
 *
 * The string must be formatted as a space-separated list of
 * name=value pairs.  Names may only contain alphanumeric characters,
 * underscores ('_') and hyphens ('-').  Values can contain any
 * non-whitespace characters.  Values containing whitespace can be
 * quoted with double quotes ('"').  Double quotes and backslashes in
 * quoted values can be escaped with backslashes ('\').
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device which is being examined
 * @param _buf		the address of a buffer to receive the location
 *			string
 * @param _buflen	the size of the buffer pointed to by @p _buf
 */
METHOD int child_location_str {
	device_t	_dev;
	device_t	_child;
	char		*_buf;
	size_t		_buflen;
};

/**
 * @brief Allow drivers to request that an interrupt be bound to a specific
 * CPU.
 * 
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _irq		the resource representing the interrupt
 * @param _cpu		the CPU to bind the interrupt to
 */
METHOD int bind_intr {
	device_t	_dev;
	device_t	_child;
	struct resource *_irq;
	int		_cpu;
} DEFAULT bus_generic_bind_intr;

/**
 * @brief Allow (bus) drivers to specify the trigger mode and polarity
 * of the specified interrupt.
 * 
 * @param _dev		the bus device
 * @param _irq		the interrupt number to modify
 * @param _trig		the trigger mode required
 * @param _pol		the interrupt polarity required
 */
METHOD int config_intr {
	device_t	_dev;
	int		_irq;
	enum intr_trigger _trig;
	enum intr_polarity _pol;
} DEFAULT bus_generic_config_intr;

/**
 * @brief Allow drivers to associate a description with an active
 * interrupt handler.
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device which allocated the resource
 * @param _irq		the resource representing the interrupt
 * @param _cookie	the cookie value returned when the interrupt
 *			was originally registered
 * @param _descr	the description to associate with the interrupt
 */
METHOD int describe_intr {
	device_t	_dev;
	device_t	_child;
	struct resource *_irq;
	void		*_cookie;
	const char	*_descr;
} DEFAULT bus_generic_describe_intr;

/**
 * @brief Notify a (bus) driver about a child that the hints mechanism
 * believes it has discovered.
 *
 * The bus is responsible for then adding the child in the right order
 * and discovering other things about the child.  The bus driver is
 * free to ignore this hint, to do special things, etc.  It is all up
 * to the bus driver to interpret.
 *
 * This method is only called in response to the parent bus asking for
 * hinted devices to be enumerated.
 *
 * @param _dev		the bus device
 * @param _dname	the name of the device w/o unit numbers
 * @param _dunit	the unit number of the device
 */
METHOD void hinted_child {
	device_t	_dev;
	const char	*_dname;
	int		_dunit;
};

/**
 * @brief Returns bus_dma_tag_t for use w/ devices on the bus.
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device to which the tag will belong
 */
METHOD bus_dma_tag_t get_dma_tag {
	device_t	_dev;
	device_t	_child;
} DEFAULT bus_generic_get_dma_tag;

/**
 * @brief Returns bus_space_tag_t for use w/ devices on the bus.
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device to which the tag will belong
 */
METHOD bus_space_tag_t get_bus_tag {
	device_t	_dev;
	device_t	_child;
} DEFAULT bus_generic_get_bus_tag;

/**
 * @brief Allow the bus to determine the unit number of a device.
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device whose unit is to be wired
 * @param _name		the name of the device's new devclass
 * @param _unitp	a pointer to the device's new unit value
 */
METHOD void hint_device_unit {
	device_t	_dev;
	device_t	_child;
	const char	*_name;
	int		*_unitp;
};

/**
 * @brief Notify a bus that the bus pass level has been changed
 *
 * @param _dev		the bus device
 */
METHOD void new_pass {
	device_t	_dev;
} DEFAULT bus_generic_new_pass;

/**
 * @brief Notify a bus that specified child's IRQ should be remapped.
 *
 * @param _dev		the bus device
 * @param _child	the child device
 * @param _irq		the irq number
 */
METHOD int remap_intr {
	device_t	_dev;
	device_t	_child;
	u_int		_irq;
} DEFAULT null_remap_intr;

/**
 * @brief Suspend a given child
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device to suspend
 */
METHOD int suspend_child {
	device_t	_dev;
	device_t	_child;
} DEFAULT bus_generic_suspend_child;

/**
 * @brief Resume a given child
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device to resume
 */
METHOD int resume_child {
	device_t	_dev;
	device_t	_child;
} DEFAULT bus_generic_resume_child;

/**
 * @brief Get the VM domain handle for the given bus and child.
 *
 * @param _dev		the bus device
 * @param _child	the child device
 * @param _domain	a pointer to the bus's domain handle identifier
 */
METHOD int get_domain {
	device_t	_dev;
	device_t	_child;
	int		*_domain;
} DEFAULT bus_generic_get_domain;

/**
 * @brief Request a set of CPUs
 *
 * @param _dev		the bus device
 * @param _child	the child device
 * @param _op		type of CPUs to request
 * @param _setsize	the size of the set passed in _cpuset
 * @param _cpuset	a pointer to a cpuset to receive the requested
 *			set of CPUs
 */
METHOD int get_cpus {
	device_t	_dev;
	device_t	_child;
	enum cpu_sets	_op;
	size_t		_setsize;
	cpuset_t	*_cpuset;
} DEFAULT bus_generic_get_cpus;
