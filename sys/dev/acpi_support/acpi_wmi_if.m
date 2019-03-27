#-
# Copyright (c) 2009 Michael Gmelin
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

INTERFACE acpi_wmi;

#
# Default implementation for acpi_wmi_generic_provides_guid_string().
#
CODE {
	static int
	acpi_wmi_generic_provides_guid_string(device_t dev, const char* guid_string)
	{
		return 0;
	}
};


#
# Check if given GUID exists in WMI
# Returns number of instances (max_instace+1) or 0 if guid doesn't exist
#
# device_t dev:	Device to probe
# const char* guid_string: String form of the GUID
#
METHOD int provides_guid_string {
	device_t	dev;
	const char*	guid_string;
} DEFAULT acpi_wmi_generic_provides_guid_string;

#
# Evaluate a WMI method call
#
# device_t dev:  Device to use
# const char* guid_string:  String form of the GUID
# UINT8 instance: instance id
# UINT32 method_id: method to call
# const ACPI_BUFFER* in: input data
# ACPI_BUFFER* out: output buffer
#
METHOD ACPI_STATUS evaluate_call {
	device_t	dev;
	const char	*guid_string;
	UINT8		instance;
	UINT32		method_id;
	const ACPI_BUFFER *in;
	ACPI_BUFFER	*out;
};

#
# Get content of a WMI block
#
# device_t dev:  Device to use
# const char* guid_string:  String form of the GUID
# UINT8 instance: instance id
# ACPI_BUFFER* out: output buffer
#
METHOD ACPI_STATUS get_block {
	device_t	dev;
	const char	*guid_string;
	UINT8		instance;
	ACPI_BUFFER	*out;
};
#
# Write to a WMI data block
#
# device_t dev:  Device to use
# const char* guid_string:  String form of the GUID
# UINT8 instance: instance id
# const ACPI_BUFFER* in: input data
#
METHOD ACPI_STATUS set_block {
	device_t	dev;
	const char	*guid_string;
	UINT8		instance;
	const ACPI_BUFFER *in;
};

#
# Install wmi event handler
#
# device_t dev:  Device to use
# const char* guid_string:  String form of the GUID
# ACPI_NOTIFY_HANDLER handler: Handler
# void* data: Payload
#
METHOD ACPI_STATUS install_event_handler {
	device_t	dev;
	const char	*guid_string;
	ACPI_NOTIFY_HANDLER handler;
	void		*data;
};

#
# Remove wmi event handler
#
# device_t dev:  Device to use
# const char* guid_string:  String form of the GUID
#
METHOD ACPI_STATUS remove_event_handler {
	device_t	dev;
	const char	*guid_string;
};


#
# Get event data associated to an event
#
# device_t dev:  Device to use
# UINT32 event_id: event id
# ACPI_BUFFER* out: output buffer
#
METHOD ACPI_STATUS get_event_data {
	device_t	dev;
	UINT32		event_id;
	ACPI_BUFFER	*out;
};
