/* Public domain. */

#ifndef _LINUX_OF_ADDRESS_H
#define _LINUX_OF_ADDRESS_H

struct device_node;
struct resource;

int of_address_to_resource(struct device_node *, int, struct resource *);

#endif
