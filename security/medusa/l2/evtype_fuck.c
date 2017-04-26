/* (C) 2002 Milan Pikula */
#include <linux/medusa/l3/registry.h>
#include <linux/dcache.h>
#include <linux/fs_struct.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/mm.h>
#include "../../../fs/mount.h"

#include "kobject_fuck.h"
#include <linux/medusa/l1/file_handlers.h>

/* the getfuck event types (yes, there are more of them) are a bit special:
 * 1) they are called from the beginning of various access types to get the
 *    initial VS set,
 * 2) they gain some additional information, which enables L4 code to keep
 *    the file hierarchy, if it wants.
 * 3) due to creepy VFS design in Linux we sometimes do some magic.
 */

struct getfuck_event {
	MEDUSA_ACCESS_HEADER;
	char path[NAME_MAX+1];
};

MED_ATTRS(getfuck_event) {
	MED_ATTR_RO (getfuck_event, path, "path", MED_STRING),
	MED_ATTR_END
};
MED_EVTYPE(getfuck_event, "getfuck", fuck_kobject, "fuck",
		fuck_kobject, "parent");


int __init getfuck_evtype_init(void) {
	MED_REGISTER_EVTYPE(getfuck_event,
			MEDUSA_EVTYPE_TRIGGEREDBYOBJECTTBIT);
	return 0;
}
__initcall(getfuck_evtype_init);





