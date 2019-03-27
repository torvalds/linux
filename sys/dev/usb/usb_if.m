#-
# Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer,
#    without modification, immediately at the beginning of the file.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#

# USB interface description
#

#include <sys/bus.h>

INTERFACE usb;

# The device received a control request
#
# The value pointed to by "pstate" can be updated to
# "USB_HR_COMPLETE_OK" to indicate that the control
# read transfer is complete, in case of short USB
# control transfers.
#
# Return values:
# 0: Success
# ENOTTY: Transaction stalled
# Else: Use builtin request handler
#
METHOD int handle_request {
	device_t dev;
	const void *req; /* pointer to the device request */
	void **pptr; /* data pointer */
	uint16_t *plen; /* maximum transfer length */
	uint16_t offset; /* data offset */
	uint8_t *pstate; /* set if transfer is complete, see USB_HR_XXX */
};

# Take controller from BIOS
#
# Return values:
# 0: Success
# Else: Failure
#
METHOD int take_controller {
	device_t dev;
};
