/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __AMD_PCIE_HELPERS_H__
#define __AMD_PCIE_HELPERS_H__

#include "amd_pcie.h"

static inline bool is_pcie_gen3_supported(uint32_t pcie_link_speed_cap)
{
	if (pcie_link_speed_cap & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)
		return true;

	return false;
}

static inline bool is_pcie_gen2_supported(uint32_t pcie_link_speed_cap)
{
	if (pcie_link_speed_cap & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2)
		return true;

	return false;
}

/* Get the new PCIE speed given the ASIC PCIE Cap and the NewState's requested PCIE speed*/
static inline uint16_t get_pcie_gen_support(uint32_t pcie_link_speed_cap,
					    uint16_t ns_pcie_gen)
{
	uint32_t asic_pcie_link_speed_cap = (pcie_link_speed_cap &
		CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_MASK);
	uint32_t sys_pcie_link_speed_cap  = (pcie_link_speed_cap &
		CAIL_PCIE_LINK_SPEED_SUPPORT_MASK);

	switch (asic_pcie_link_speed_cap) {
	case CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN1:
		return PP_PCIEGen1;

	case CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN2:
		return PP_PCIEGen2;

	case CAIL_ASIC_PCIE_LINK_SPEED_SUPPORT_GEN3:
		return PP_PCIEGen3;

	default:
		if (is_pcie_gen3_supported(sys_pcie_link_speed_cap) &&
			(ns_pcie_gen == PP_PCIEGen3)) {
			return PP_PCIEGen3;
		} else if (is_pcie_gen2_supported(sys_pcie_link_speed_cap) &&
			((ns_pcie_gen == PP_PCIEGen3) || (ns_pcie_gen == PP_PCIEGen2))) {
			return PP_PCIEGen2;
		}
	}

	return PP_PCIEGen1;
}

static inline uint16_t get_pcie_lane_support(uint32_t pcie_lane_width_cap,
					     uint16_t ns_pcie_lanes)
{
	int i, j;
	uint16_t new_pcie_lanes = ns_pcie_lanes;
	uint16_t pcie_lanes[7] = {1, 2, 4, 8, 12, 16, 32};

	switch (pcie_lane_width_cap) {
	case 0:
		pr_err("No valid PCIE lane width reported\n");
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X1:
		new_pcie_lanes = 1;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X2:
		new_pcie_lanes = 2;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X4:
		new_pcie_lanes = 4;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X8:
		new_pcie_lanes = 8;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X12:
		new_pcie_lanes = 12;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X16:
		new_pcie_lanes = 16;
		break;
	case CAIL_PCIE_LINK_WIDTH_SUPPORT_X32:
		new_pcie_lanes = 32;
		break;
	default:
		for (i = 0; i < 7; i++) {
			if (ns_pcie_lanes == pcie_lanes[i]) {
				if (pcie_lane_width_cap & (0x10000 << i)) {
					break;
				} else {
					for (j = i - 1; j >= 0; j--) {
						if (pcie_lane_width_cap & (0x10000 << j)) {
							new_pcie_lanes = pcie_lanes[j];
							break;
						}
					}

					if (j < 0) {
						for (j = i + 1; j < 7; j++) {
							if (pcie_lane_width_cap & (0x10000 << j)) {
								new_pcie_lanes = pcie_lanes[j];
								break;
							}
						}
						if (j > 7)
							pr_err("Cannot find a valid PCIE lane width!\n");
					}
				}
				break;
			}
		}
		break;
	}

	return new_pcie_lanes;
}

#endif
