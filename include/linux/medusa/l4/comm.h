#ifndef _MEDUSA_COMM_H
#define _MEDUSA_COMM_H

#include <linux/medusa/l3/arch_types.h>

/*
 * the following constants and structures cover the standard
 * communication protocol.
 *
 * this means: DON'T TOUCH! Not only you will break the comm
 * protocol, but also the code which relies on particular
 * facts about them.
 */

typedef uint64_t MCPptr_t; // medusa common protocol pointer type this is here because we wanna have one protocol for all architectures JK March 2015
typedef uint64_t Mptr_t; // medusa pointer if you want to run effectivly medusa you should use something like coid* :) for debuggin purposes you should use mcptr_t :) JK March 2015

/* version of this communication protocol */
#define MEDUSA_COMM_VERSION	1

#define MEDUSA_COMM_GREETING 0x66007e5a

#define MEDUSA_COMM_ATTRNAME_MAX	(32-5)
#define MEDUSA_COMM_KCLASSNAME_MAX	(32-2)
#define MEDUSA_COMM_EVNAME_MAX		(32-2)

/* comm protocol commands. 'k' stands for kernel, 'c' for constable. */

#define MEDUSA_COMM_AUTHREQUEST		0x01	/* k->c */
#define MEDUSA_COMM_AUTHANSWER		0x81	/* c->k */

#define MEDUSA_COMM_KCLASSDEF		0x02	/* k->c */
#define MEDUSA_COMM_KCLASSUNDEF		0x03	/* k->c */
#define MEDUSA_COMM_EVTYPEDEF		0x04	/* k->c */
#define MEDUSA_COMM_EVTYPEUNDEF		0x05	/* k->c */

#define MEDUSA_COMM_FETCH_REQUEST	0x88	/* c->k */
#define MEDUSA_COMM_FETCH_ANSWER	0x08	/* k->c */
#define MEDUSA_COMM_FETCH_ERROR		0x09	/* k->c */

#define MEDUSA_COMM_UPDATE_REQUEST	0x8a	/* c->k */
#define MEDUSA_COMM_UPDATE_ANSWER	0x0a	/* k->c */

#pragma pack(push,1)
struct medusa_comm_attribute_s {
	u_int16_t offset;			/* offset of attribute in object */
	u_int16_t length;			/* bytes consumed by data */
	u_int8_t type;				/* data type (MED_COMM_TYPE_xxx) */
	char name[MEDUSA_COMM_ATTRNAME_MAX];	/* string: attribute name */
};
#pragma pack(pop)

#define	MED_COMM_TYPE_END		0x00	/* end of attribute list */
#define	MED_COMM_TYPE_UNSIGNED		0x01	/* unsigned integer attr */
#define	MED_COMM_TYPE_SIGNED		0x02	/* signed integer attr */
#define	MED_COMM_TYPE_STRING		0x03	/* string attr */
#define	MED_COMM_TYPE_BITMAP		0x04	/* bitmap attr */

#define	MED_COMM_TYPE_READ_ONLY		0x80	/* this attribute is read-only */
#define	MED_COMM_TYPE_PRIMARY_KEY	0x40	/* this attribute is used to lookup object */

#pragma pack(push,1)
struct medusa_comm_kclass_s {
	MCPptr_t kclassid;	/* unique identifier of this kclass */
	u_int16_t	size;		/* size of object */
	char		name[MEDUSA_COMM_KCLASSNAME_MAX];
};
#pragma pack(pop)

#pragma pack(push,1)
struct medusa_comm_evtype_s {
	MCPptr_t evid;
	u_int16_t	size;
	u_int16_t	actbit;	/* which bit of 'act' controls this evtype:
				 * 0x8000 + bitnr: bitnr at subject,
				 * 0x0000 + bitnr: bitnr at object,
				 * 0xffff: there is no way to trigger this ev.
				 */
	MCPptr_t ev_kclass[2];
	char		name[MEDUSA_COMM_EVNAME_MAX];
	char		ev_name[2][MEDUSA_COMM_ATTRNAME_MAX];
};
#pragma pack(pop)

#endif
