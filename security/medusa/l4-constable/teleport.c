/*
 * L4 authentication server for Medusa DS9
 * Copyright (C) 2002 Milan Pikula <www@terminus.sk>
 *
 * data to userspace go through this programmable teleport ;)
 */

#include <linux/medusa/l3/arch.h>
#include <linux/medusa/l4/comm.h>
#include <linux/medusa/l3/constants.h>
#include <linux/medusa/l3/kobject.h>

#if MED_RO != MED_COMM_TYPE_READ_ONLY
#error "L3 and L4 constants don't match. We don't convert them. Go well, go hell."
#endif

#define DEBUG	/* define this to get extra debugging output */

#include "teleport.h"

#undef PARANOIA_CHECKS	/* define this to enable extra checking */

/* much of this code is platform independent */
/* assumption: we're completely serialized (!!!) */

static ssize_t (*_to_user)(void *, size_t) = NULL;
static inline ssize_t place_to_user(teleport_t * teleport,
		size_t * userlimit);

/****/

void teleport_reset(teleport_t * teleport, teleport_insn_t * addr,
		ssize_t (*to_user)(void *, size_t))
{
	_to_user = to_user;
	teleport->ip = addr-1;
	teleport->cycle = tpc_FETCH;
}

ssize_t teleport_cycle(teleport_t * teleport, size_t userlimit)
{
	ssize_t retval = 0;
	ssize_t tmp;

	while (userlimit || teleport->cycle == tpc_FETCH) {
		tmp = 0;
		switch(teleport->cycle) {
		case tpc_HALT:
			return retval;
		case tpc_FETCH:
			teleport->cycle = tpc_EXECUTE;
			switch ((++teleport->ip)->opcode) {
			case tp_PUT16:
				teleport->data_to_user =
					(unsigned char *)&teleport->ip->args.put16.what;
				teleport->remaining = 2;
				break;
			case tp_PUT32:
				teleport->data_to_user =
					(unsigned char *)&teleport->ip->args.put32.what;
				teleport->remaining = 4;
				break;
			case tp_PUTPtr:
				teleport->data_to_user =
					(unsigned char *)&teleport->ip->args.putPtr.what;
					//teleport->remaining = sizeof(void*);
					teleport->remaining = 8; // Has to be 8 if we want to  have one protocol across all platforms ;) JK March 2015
				break;
			case tp_CUTNPASTE:
				teleport->data_to_user =
					teleport->ip->args.cutnpaste.from;
				teleport->remaining =
					teleport->ip->args.cutnpaste.count;
				break;
			case tp_PUTATTRS:
				teleport->u.putattrs.current_attr = -1;
				teleport->remaining = 0;
				break;
			case tp_PUTKCLASS:
				teleport->u.putkclass.cl.kclassid =
					(MCPptr_t)teleport->ip->args.putkclass.kclassdef; // possiblity for encryption .. JK note march 2015
				teleport->u.putkclass.cl.size =
					teleport->ip->args.putkclass.kclassdef->kobject_size;
				memcpy(teleport->u.putkclass.cl.name,
					teleport->ip->args.putkclass.kclassdef->name,
					MEDUSA_COMM_KCLASSNAME_MAX < MEDUSA_KCLASSNAME_MAX ? MEDUSA_COMM_KCLASSNAME_MAX : MEDUSA_KCLASSNAME_MAX);
				teleport->data_to_user =
					(unsigned char *)&teleport->u.putkclass.cl;
				teleport->remaining =
					sizeof(struct medusa_comm_kclass_s);
#ifdef DEBUG
				med_pr_debug("-> class %s [%p]\n", teleport->u.putkclass.cl.name,
						(void*)teleport->u.putkclass.cl.kclassid);
#endif
				break;
			case tp_PUTEVTYPE:
				teleport->u.putevtype.ev.evid =
					(MCPptr_t)teleport->ip->args.putevtype.evtypedef; // possibility for encryption ... JK note March 2015
				teleport->u.putevtype.ev.size =
					teleport->ip->args.putevtype.evtypedef->event_size;
				teleport->u.putevtype.ev.actbit =
					teleport->ip->args.putevtype.evtypedef->bitnr;
				memcpy(teleport->u.putevtype.ev.name,
					teleport->ip->args.putevtype.evtypedef->name,
					MEDUSA_COMM_EVNAME_MAX < MEDUSA_EVNAME_MAX ? MEDUSA_COMM_EVNAME_MAX : MEDUSA_EVNAME_MAX);
				teleport->u.putevtype.ev.ev_kclass[0] =
					(MCPptr_t)teleport->ip->args.putevtype.evtypedef->arg_kclass[0]; // possibility for encryption ... JK note March 2015
				teleport->u.putevtype.ev.ev_kclass[1] =
					(MCPptr_t)teleport->ip->args.putevtype.evtypedef->arg_kclass[1]; // possibility for encryption ... JK note March 2015

#ifdef DEBUG
				med_pr_debug("-> evtype %s [%p] with [%p] and [%p]\n", teleport->u.putevtype.ev.name,
					(void*)teleport->u.putevtype.ev.evid,
					(void*)teleport->u.putevtype.ev.ev_kclass[0],
					(void*)teleport->u.putevtype.ev.ev_kclass[1]
				);
#endif

				memcpy(teleport->u.putevtype.ev.ev_name[0],
					teleport->ip->args.putevtype.evtypedef->arg_name[0],
					MEDUSA_COMM_ATTRNAME_MAX < MEDUSA_ATTRNAME_MAX ? MEDUSA_COMM_ATTRNAME_MAX : MEDUSA_ATTRNAME_MAX);
				memcpy(teleport->u.putevtype.ev.ev_name[1],
					teleport->ip->args.putevtype.evtypedef->arg_name[1],
					MEDUSA_COMM_ATTRNAME_MAX < MEDUSA_ATTRNAME_MAX ? MEDUSA_COMM_ATTRNAME_MAX : MEDUSA_ATTRNAME_MAX);
				teleport->data_to_user =
					(unsigned char *)&teleport->u.putevtype.ev;
				teleport->remaining =
					sizeof(struct medusa_comm_evtype_s);
				break;
			case tp_HALT:
				teleport->cycle = tpc_HALT;
				break;
			}
			if (teleport->cycle == tpc_HALT)
				break;
			/* fallthrough */
		case tpc_EXECUTE:
			switch (teleport->ip->opcode) {
			case tp_PUT16:
			case tp_PUT32:
			case tp_PUTPtr:
			case tp_CUTNPASTE:
			case tp_PUTKCLASS:
			case tp_PUTEVTYPE:
				tmp = place_to_user(teleport, &userlimit);
				if (!teleport->remaining)
					teleport->cycle = tpc_FETCH;
				break;
			case tp_PUTATTRS:
				if (teleport->remaining) {
					tmp = place_to_user(teleport, &userlimit);
					if (teleport->remaining)
						break;
				}
				if (teleport->u.putattrs.current_attr >= 0 &&
					teleport->ip->args.putattrs.attrlist[
					teleport->u.putattrs.current_attr
					].type == MED_END) {
						teleport->cycle = tpc_FETCH;
						break;
					}
				teleport->u.putattrs.current_attr++;
				teleport->u.putattrs.attr.offset = teleport->ip->args.putattrs.attrlist[teleport->u.putattrs.current_attr].offset;
				teleport->u.putattrs.attr.length = teleport->ip->args.putattrs.attrlist[teleport->u.putattrs.current_attr].length;
				teleport->u.putattrs.attr.type = teleport->ip->args.putattrs.attrlist[teleport->u.putattrs.current_attr].type;
				memcpy(teleport->u.putattrs.attr.name, teleport->ip->args.putattrs.attrlist[teleport->u.putattrs.current_attr].name, MEDUSA_COMM_ATTRNAME_MAX < MEDUSA_ATTRNAME_MAX ? MEDUSA_COMM_ATTRNAME_MAX : MEDUSA_ATTRNAME_MAX);
				teleport->data_to_user =
					(unsigned char *)&teleport->u.putattrs.attr;
				teleport->remaining =
					sizeof(struct medusa_comm_attribute_s);
				break;
			case tp_HALT:
				teleport->cycle = tpc_HALT;
				break;
			default:
				med_pr_warn("Unknown instruction (0x%2x) at %p\n", teleport->ip->opcode, teleport->ip);
				teleport->cycle = tpc_HALT;
			}
		}
		if (tmp > 0)
			retval += tmp;
		else if (tmp < 0)
			return tmp; /* abnormal condition, data lost */
	}
	return retval;
}

static inline ssize_t place_to_user(teleport_t * teleport,
		size_t * userlimit)
{
	ssize_t len = *userlimit < teleport->remaining ?
		*userlimit : teleport->remaining;

	if (!len)
		return 0;
#ifdef PARANOIA_CHECKS
	if (!_to_user) {
		med_pr_warn("teleport wrongly initialized!\n");
		return 0;
	}
#endif
	len = _to_user(teleport->data_to_user, len);
	if (len < 0)
		return len;
	teleport->data_to_user += len; teleport->remaining -= len;
	*userlimit -= len;
	return len;
}

