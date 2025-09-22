/* Public domain. */

#ifndef _LINUX_FWNODE_H
#define _LINUX_FWNODE_H

struct fwnode_handle {
	struct fwnode_handle *secondary;
};

#endif
