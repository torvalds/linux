/* SPDX-License-Identifier: GPL-2.0 */
/* platform data for the OXU210HP HCD */

struct oxu210hp_platform_data {
	unsigned int bus16:1;
	unsigned int use_hcd_otg:1;
	unsigned int use_hcd_sph:1;
};
