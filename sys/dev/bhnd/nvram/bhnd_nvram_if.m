#-
# Copyright (c) 2016 Landon Fuller <landon@landonf.org>
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
# USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$

#include <sys/types.h>
#include <sys/bus.h>

#include <dev/bhnd/bhnd.h>

INTERFACE bhnd_nvram;

#
# bhnd(4) NVRAM device interface.
#
# Provides a shared interface to HND NVRAM, OTP, and SPROM devices that provide
# access to a common set of hardware/device configuration variables.
#

/**
 * Read an NVRAM variable.
 *
 * @param		dev	The NVRAM device.
 * @param		name	The NVRAM variable name.
 * @param[out]		buf	On success, the requested value will be written
 *				to this buffer. This argment may be NULL if
 *				the value is not desired.
 * @param[in,out]	len	The maximum capacity of @p buf. On success,
 *				will be set to the actual size of the requested
 *				value.
 * @param		type	The data type to be written to @p buf.
 *
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENOMEM	If @p buf is non-NULL and a buffer of @p len is too
 *			small to hold the requested value.
 * @retval ENODEV	If no supported NVRAM hardware is accessible via this
 *			device.
 * @retval EOPNOTSUPP	If any coercion to @p type is unsupported.
 * @retval EFTYPE	If the @p name's data type cannot be coerced to @p type.
 * @retval ERANGE	If value coercion would overflow @p type.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
METHOD int getvar {
	device_t	 dev;
	const char	*name;
	void		*buf;
	size_t		*len;
	bhnd_nvram_type	 type;
};

/**
 * Set an NVRAM variable's value.
 *
 * No changes will be written to non-volatile storage until explicitly
 * committed.
 *
 * @param	dev	The NVRAM device.
 * @param	name	The NVRAM variable name.
 * @param	value	The new value.
 * @param	len	The size of @p value.
 * @param	type	The data type of @p value.
 *
 * @retval 0		success
 * @retval ENOENT	The specified variable name is not recognized.
 * @retval ENODEV	If no supported NVRAM hardware is accessible via this
 *			device.
 * @retval EOPNOTSUPP	If any coercion to @p type is unsupported.
 * @retval EFTYPE	If the @p name's data type cannot be coerced to @p type.
 * @retval ERANGE	If value coercion from  @p type would overflow.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
METHOD int setvar {
	device_t	 dev;
	const char	*name;
	const void	*value;
	size_t		 len;
	bhnd_nvram_type	 type;
};
