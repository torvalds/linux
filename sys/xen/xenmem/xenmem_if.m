#-
# Copyright (c) 2015 Roger Pau Monné <royger@FreeBSD.org>
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

INTERFACE xenmem;

#
# Default implementations of some methods.
#
CODE {
        static struct resource *
        xenmem_generic_alloc(device_t dev, device_t child, int *res_id,
            size_t size)
        {
                device_t parent;

                parent = device_get_parent(dev);
                if (parent == NULL)
                        return (NULL);
                return (XENMEM_ALLOC(parent, child, res_id, size));
        }

        static int
        xenmem_generic_free(device_t dev, device_t child, int res_id,
            struct resource *res)
        {
                device_t parent;

                parent = device_get_parent(dev);
                if (parent == NULL)
                        return (ENXIO);
                return (XENMEM_FREE(parent, child, res_id, res));
        }
};

/**
 * @brief Request for unused physical memory regions.
 *
 * @param _dev          the device whose child was being probed.
 * @param _child        the child device which failed to probe.
 * @param _res_id       a pointer to the resource identifier.
 * @param _size         size of the required memory region.
 *
 * @returns             the resource which was allocated or @c NULL if no
 *                      resource could be allocated.
 */
METHOD struct resource * alloc {
	device_t                _dev;
	device_t                _child;
	int                    *_res_id;
	size_t                  _size;
} DEFAULT xenmem_generic_alloc;

/**
 * @brief Free physical memory regions.
 *
 * @param _dev          the device whose child was being probed.
 * @param _child        the child device which failed to probe.
 * @param _res_id       the resource identifier.
 * @param _res          the resource.
 *
 * @returns             0 on success, otherwise an error code.
 */
METHOD int free {
	device_t                _dev;
	device_t                _child;
	int                     _res_id;
	struct resource        *_res;
} DEFAULT xenmem_generic_free;
