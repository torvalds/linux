/* (C) 2002 Milan Pikula */
#include <linux/medusa/l3/registry.h>
#include "kobject_fuck.h"

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
