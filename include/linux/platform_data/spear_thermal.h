/*
 * SPEAr thermal driver platform data.
 *
 * Copyright (C) 2011-2012 ST Microelectronics
 * Author: Vincenzo Frascino <vincenzo.frascino@st.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef SPEAR_THERMAL_H
#define SPEAR_THERMAL_H

/* SPEAr Thermal Sensor Platform Data */
struct spear_thermal_pdata {
	/* flags used to enable thermal sensor */
	unsigned int thermal_flags;
};

#endif /* SPEAR_THERMAL_H */
