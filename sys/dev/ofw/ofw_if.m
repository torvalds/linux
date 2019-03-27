#-
# Copyright (c) 2008 Nathan Whitehorn
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

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofwvar.h>

/**
 * @defgroup OFW ofw - KObj methods for Open Firmware RTAS implementations
 * @brief A set of methods to implement the Open Firmware client side interface.
 * @{
 */

INTERFACE ofw;

/**
 * @brief Initialize OFW client interface
 *
 * @param _cookie	A handle to the client interface, generally the OF
 *			callback routine.
 */
METHOD int init {
	ofw_t		_ofw;
	void		*_cookie;
};

/**
 * @brief Return next sibling of node.
 *
 * @param _node		Selected node
 */
METHOD phandle_t peer {
	ofw_t		_ofw;
	phandle_t	_node;
};

/**
 * @brief Return parent of node.
 *
 * @param _node		Selected node
 */
METHOD phandle_t parent {
	ofw_t		_ofw;
	phandle_t	_node;
};

/**
 * @brief Return first child of node.
 *
 * @param _node		Selected node
 */
METHOD phandle_t child {
	ofw_t		_ofw;
	phandle_t	_node;
};

/**
 * @brief Return package corresponding to instance.
 *
 * @param _handle	Selected instance
 */
METHOD phandle_t instance_to_package {
	ofw_t		_ofw;
	ihandle_t	_handle;
};

/**
 * @brief Return length of node property.
 *
 * @param _node		Selected node
 * @param _prop		Property name
 */
METHOD ssize_t getproplen {
	ofw_t		_ofw;
	phandle_t	_node;
	const char	*_prop;
};

/**
 * @brief Read node property.
 *
 * @param _node		Selected node
 * @param _prop		Property name
 * @param _buf		Pointer to buffer
 * @param _size		Size of buffer
 */
METHOD ssize_t getprop {
	ofw_t		_ofw;
	phandle_t	_node;
	const char	*_prop;
	void		*_buf;
	size_t		_size;
};

/**
 * @brief Get next property name.
 *
 * @param _node		Selected node
 * @param _prop		Current property name
 * @param _buf		Buffer for next property name
 * @param _size		Size of buffer
 */
METHOD int nextprop {
	ofw_t		_ofw;
	phandle_t	_node;
	const char	*_prop;
	char		*_buf;
	size_t		_size;
};

/**
 * @brief Set property.
 *
 * @param _node		Selected node
 * @param _prop		Property name
 * @param _buf		Value to set
 * @param _size		Size of buffer
 */
METHOD int setprop {
	ofw_t		_ofw;
	phandle_t	_node;
	const char	*_prop;
	const void	*_buf;
	size_t		_size;
};

/**
 * @brief Canonicalize path.
 *
 * @param _path		Path to canonicalize
 * @param _buf		Buffer for canonicalized path
 * @param _size		Size of buffer
 */
METHOD ssize_t canon {
	ofw_t		_ofw;
	const char	*_path;
	char		*_buf;
	size_t		_size;
};

/**
 * @brief Return phandle for named device.
 *
 * @param _path		Device path
 */
METHOD phandle_t finddevice {
	ofw_t		_ofw;
	const char	*_path;
};

/**
 * @brief Return path for node instance.
 *
 * @param _handle	Instance handle
 * @param _path		Buffer for path
 * @param _size		Size of buffer
 */
METHOD ssize_t instance_to_path {
	ofw_t		_ofw;
	ihandle_t	_handle;
	char		*_path;
	size_t		_size;
};

/**
 * @brief Return path for node.
 *
 * @param _node		Package node
 * @param _path		Buffer for path
 * @param _size		Size of buffer
 */
METHOD ssize_t package_to_path {
	ofw_t		_ofw;
	phandle_t	_node;
	char		*_path;
	size_t		_size;
};

# Methods for OF method calls (optional)

/**
 * @brief Test to see if a service exists.
 *
 * @param _name		name of the service
 */
METHOD int test {
	ofw_t		_ofw;
	const char	*_name;
};

/**
 * @brief Call method belonging to an instance handle.
 *
 * @param _instance	Instance handle
 * @param _method	Method name
 * @param _nargs	Number of arguments
 * @param _nreturns	Number of return values
 * @param _args_and_returns	Values for arguments, followed by returns
 */

METHOD int call_method {
	ofw_t		_ofw;
	ihandle_t	_instance;
	const char	*_method;
	int		_nargs;
	int		_nreturns;

	cell_t		*_args_and_returns;
};

/**
 * @brief Interpret a forth command.
 *
 * @param _cmd		Command
 * @param _nreturns	Number of return values
 * @param _returns	Values for returns
 */

METHOD int interpret {
	ofw_t		_ofw;
	const char	*_cmd;
	int		_nreturns;
	cell_t		*_returns;
};

# Device I/O Functions (optional)

/**
 * @brief Open node, returning instance handle.
 *
 * @param _path		Path to node
 */
METHOD ihandle_t open {
	ofw_t		_ofw;
	const char	*_path;
}

/**
 * @brief Close node instance.
 *
 * @param _instance	Instance to close
 */
METHOD void close {
	ofw_t		_ofw;
	ihandle_t	_instance;
}

/**
 * @brief Read from device.
 *
 * @param _instance	Device instance
 * @param _buf		Buffer to read to
 * @param _size		Size of buffer
 */
METHOD ssize_t read {
	ofw_t		_ofw;
	ihandle_t	_instance;
	void		*_buf;
	size_t		size;
}

/**
 * @brief Write to device.
 *
 * @param _instance	Device instance
 * @param _buf		Buffer to write from
 * @param _size		Size of buffer
 */
METHOD ssize_t write {
	ofw_t		_ofw;
	ihandle_t	_instance;
	const void	*_buf;
	size_t		size;
}

/**
 * @brief Seek device.
 *
 * @param _instance	Device instance
 * @param _off		Offset to which to seek
 */
METHOD int seek {
	ofw_t		_ofw;
	ihandle_t	_instance;
	uint64_t	_off;
}

# Open Firmware memory management

/**
 * @brief Claim virtual memory.
 *
 * @param _addr		Requested memory location (NULL for first available)
 * @param _size		Requested size in bytes
 * @param _align	Requested alignment
 */
METHOD caddr_t claim {
	ofw_t		_ofw;
	void		*_addr;
	size_t		_size;
	u_int		_align;
}

/**
 * @brief Release virtual memory.
 *
 * @param _addr		Memory location
 * @param _size		Size in bytes
 */
METHOD void release {
	ofw_t		_ofw;
	void		*_addr;
	size_t		_size;
};

# Commands for returning control to the firmware

/**
 * @brief Temporarily return control to firmware.
 */
METHOD void enter {
	ofw_t		_ofw;
};

/**
 * @brief Halt and return control to firmware.
 */
METHOD void exit {
	ofw_t		_ofw;
};
