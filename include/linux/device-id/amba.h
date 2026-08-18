/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_AMBA_H
#define LINUX_DEVICE_ID_AMBA_H

/**
 * struct amba_id - identifies a device on an AMBA bus
 * @id: The significant bits if the hardware device ID
 * @mask: Bitmask specifying which bits of the id field are significant when
 *	matching.  A driver binds to a device when ((hardware device ID) & mask)
 *	== id.
 * @data: Private data used by the driver.
 */
struct amba_id {
	unsigned int		id;
	unsigned int		mask;
	void			*data;
};

#endif /* ifndef LINUX_DEVICE_ID_AMBA_H */
