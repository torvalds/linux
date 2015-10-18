#ifndef L3_INTERNALS_H
#define L3_INTERNALS_H

/* data structures, internal l3 use only. */
MED_DECLARE_LOCK_DATA(registry_lock);
extern struct medusa_kclass_s * kclasses;
extern struct medusa_acctype_s * acctypes;
extern struct medusa_authserver_s * authserver;

#endif
