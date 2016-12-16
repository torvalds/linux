/*
 * cec-edid - HDMI Consumer Electronics Control & EDID helpers
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _MEDIA_CEC_EDID_H
#define _MEDIA_CEC_EDID_H

#include <linux/types.h>

#define CEC_PHYS_ADDR_INVALID		0xffff
#define cec_phys_addr_exp(pa) \
	((pa) >> 12), ((pa) >> 8) & 0xf, ((pa) >> 4) & 0xf, (pa) & 0xf

/**
 * cec_get_edid_phys_addr() - find and return the physical address
 *
 * @edid:	pointer to the EDID data
 * @size:	size in bytes of the EDID data
 * @offset:	If not %NULL then the location of the physical address
 *		bytes in the EDID will be returned here. This is set to 0
 *		if there is no physical address found.
 *
 * Return: the physical address or CEC_PHYS_ADDR_INVALID if there is none.
 */
u16 cec_get_edid_phys_addr(const u8 *edid, unsigned int size,
			   unsigned int *offset);

/**
 * cec_set_edid_phys_addr() - find and set the physical address
 *
 * @edid:	pointer to the EDID data
 * @size:	size in bytes of the EDID data
 * @phys_addr:	the new physical address
 *
 * This function finds the location of the physical address in the EDID
 * and fills in the given physical address and updates the checksum
 * at the end of the EDID block. It does nothing if the EDID doesn't
 * contain a physical address.
 */
void cec_set_edid_phys_addr(u8 *edid, unsigned int size, u16 phys_addr);

/**
 * cec_phys_addr_for_input() - calculate the PA for an input
 *
 * @phys_addr:	the physical address of the parent
 * @input:	the number of the input port, must be between 1 and 15
 *
 * This function calculates a new physical address based on the input
 * port number. For example:
 *
 * PA = 0.0.0.0 and input = 2 becomes 2.0.0.0
 *
 * PA = 3.0.0.0 and input = 1 becomes 3.1.0.0
 *
 * PA = 3.2.1.0 and input = 5 becomes 3.2.1.5
 *
 * PA = 3.2.1.3 and input = 5 becomes f.f.f.f since it maxed out the depth.
 *
 * Return: the new physical address or CEC_PHYS_ADDR_INVALID.
 */
u16 cec_phys_addr_for_input(u16 phys_addr, u8 input);

/**
 * cec_phys_addr_validate() - validate a physical address from an EDID
 *
 * @phys_addr:	the physical address to validate
 * @parent:	if not %NULL, then this is filled with the parents PA.
 * @port:	if not %NULL, then this is filled with the input port.
 *
 * This validates a physical address as read from an EDID. If the
 * PA is invalid (such as 1.0.1.0 since '0' is only allowed at the end),
 * then it will return -EINVAL.
 *
 * The parent PA is passed into %parent and the input port is passed into
 * %port. For example:
 *
 * PA = 0.0.0.0: has parent 0.0.0.0 and input port 0.
 *
 * PA = 1.0.0.0: has parent 0.0.0.0 and input port 1.
 *
 * PA = 3.2.0.0: has parent 3.0.0.0 and input port 2.
 *
 * PA = f.f.f.f: has parent f.f.f.f and input port 0.
 *
 * Return: 0 if the PA is valid, -EINVAL if not.
 */
int cec_phys_addr_validate(u16 phys_addr, u16 *parent, u16 *port);

#endif /* _MEDIA_CEC_EDID_H */
