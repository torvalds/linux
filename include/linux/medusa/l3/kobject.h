#ifndef _MEDUSA_KOBJECT_H
#define _MEDUSA_KOBJECT_H

/*
 * Used by l2, l3, l4
 *
 * This file defines
 *	ATTRIBUTE TYPE (defined in l2, used by l4)	medusa_attribute_s
 *
 *	KCLASS (defined in l2, used by l3, l4)		medusa_kclass_s
 *	KOBJECT (defined in l2, used by l3, l4)		medusa_kobject_s
 *
 *	EVENT TYPE (defined in l2, used by l4)		medusa_evtype_s
 *		(and ACCESS TYPE)
 *	EVENT (defined in l2, used by l3, l4)		medusa_event_s
 *		(and ACCESS)
 */

/*
 * While you are not looking, this source is in Pascal.
 */

#include <linux/medusa/l3/arch.h>
#include <linux/medusa/l3/constants.h>
#include <linux/medusa/l3/model.h>
#include <linux/types.h>

#pragma GCC optimize ("Og")

struct medusa_attribute_s;
struct medusa_kclass_s;
struct medusa_kobject_s;
struct medusa_evtype_s;

/**/
/**/

/* describes one attribute in kclass (defined in l2 code, used by l4) */
struct medusa_attribute_s {
	char name[MEDUSA_ATTRNAME_MAX];	/* string: attribute name */
	unsigned int type;		/* data type (MED_xxx) */
	unsigned long offset;		/* offset of attribute in kobject */
	unsigned long length;		/* bytes consumed by data */
};

#define MED_ATTRSOF(structname) (structname##_attrs)

/* macros for l2 to simplify attribute definitions */
#define MED_ATTRS(structname) struct medusa_attribute_s (MED_ATTRSOF(structname))[] =

#define MED_ATTR(structname,structmember,attrname,type) { \
		(attrname), \
		(type), \
		/*(uintptr_t)(&(((struct structname *)0)->structmember)), */\
		(Mptr_t)(&(((struct structname *)0)->structmember)), \
		sizeof (((struct structname *)0)->structmember) \
	}
#define MED_ATTR_END {"", MED_END, 0, 0}

#define MED_ATTR_RO(sn,sm,an,ty) MED_ATTR(sn,sm,an,(ty)|MED_RO)
#define MED_ATTR_KEY(sn,sm,an,ty) MED_ATTR(sn,sm,an,(ty)|MED_KEY)
#define MED_ATTR_KEY_RO(sn,sm,an,ty) MED_ATTR(sn,sm,an,(ty)|MED_KEY|MED_RO)
#define __MED_ATTR_SUBJ(sn,mn) /* internal macro copying medusa/l3/model.h */ \
	MED_ATTR(sn,mn.vsr, "vsr", MED_BITMAP), \
	MED_ATTR(sn,mn.vsw, "vsw", MED_BITMAP), \
	MED_ATTR(sn,mn.vss, "vss", MED_BITMAP), \
	MED_ATTR(sn,mn.act, "med_sact", MED_BITMAP), \
	MED_ATTR(sn,mn.cinfo, "s_cinfo", MED_BITMAP)
#define MED_ATTR_SUBJECT(sn) __MED_ATTR_SUBJ(sn,med_subject)
#define MED_SUBATTR_SUBJECT(sn,mn) __MED_ATTR_SUBJ(sn,mn.med_subject)
#define __MED_ATTR_OBJ(sn,mn) /* internal macro copying medusa/l3/model.h */ \
	MED_ATTR(sn,mn.vs, "vs", MED_BITMAP), \
	MED_ATTR(sn,mn.act, "med_oact", MED_BITMAP), \
	MED_ATTR(sn,mn.cinfo, "o_cinfo", MED_BITMAP)
#define MED_ATTR_OBJECT(sn) __MED_ATTR_OBJ(sn,med_object)
#define MED_SUBATTR_OBJECT(sn,mn) __MED_ATTR_OBJ(sn,mn.med_object)

/**/
/**/

/* description of kclass (defined in l2 code, used by l3, l4) */
#define MED_KCLASSOF(structname) (structname##_kclass)
#define MED_DECLARE_KCLASSOF(structname) struct medusa_kclass_s (structname##_kclass)
struct medusa_kclass_s {
	/* l3-defined data, filled by register_kobject */
	struct medusa_kclass_s * next;	/* through all kclasses */
	int use_count;
	cinfo_t cinfo; /* l4 hint */
#ifdef CONFIG_MEDUSA_PROFILING
	unsigned long long l2_to_l4;
	unsigned long long l4_to_l2;
#endif

	/* l2-defined data */
	unsigned int kobject_size;		/* sizeof(kobject) */
	struct medusa_attribute_s * attr;	/* attributes */
	char name[MEDUSA_KCLASSNAME_MAX];	/* string: kclass name */
	void * reg;
	void * unreg;
	struct medusa_kobject_s * (* fetch)(struct medusa_kobject_s * key_obj); /* fetch the kobj. by key */
	medusa_answer_t (* update)(struct medusa_kobject_s * kobj); /* update the kobj. */
	void (* unmonitor)(struct medusa_kobject_s * kobj); /* disable all monitoring on kobj. optional; cannot sleep. */
};

#ifdef CONFIG_MEDUSA_PROFILING
#define MEDUSA_DEFAULT_KCLASS_HEADER \
	NULL, 0,	/* register_kclass */	\
	0,		/* cinfo */		\
	0, 0		/* stats */
#else
#define MEDUSA_DEFAULT_KCLASS_HEADER \
	NULL, 0,	/* register_kclass */	\
	0		/* cinfo */
#endif

/* macros for l2 to simplify kclass definition */
#define MED_KCLASS(structname) struct medusa_kclass_s (MED_KCLASSOF(structname)) =
#define MEDUSA_KCLASS_HEADER(structname) \
	MEDUSA_DEFAULT_KCLASS_HEADER, \
	sizeof(struct structname), \
	MED_ATTRSOF(structname)

/**/
/**/

/* this is the kobject header - use it at the beginning of l2 structures */
#define MEDUSA_KOBJECT_HEADER \
	struct medusa_kclass_s * kclass_id	/* kclass - type of kobject */
	
/* used by l3 and l4 to easily access the header of l2 structures */
struct medusa_kobject_s {
	MEDUSA_KOBJECT_HEADER;
	unsigned char data[0];
};

#define MED_EVTYPEOF(structname) (structname##_evtype)
struct medusa_evtype_s {
	/* l3-defined data */
	struct medusa_evtype_s * next;
	unsigned short bitnr;	/* which bit at subject or object triggers
				 * monitoring of this evtype. The value is
				 * OR'd with these flags: */
	/* if you change/swap them, check the usage anywhere (l3/registry.c) */
#define MEDUSA_EVTYPE_NOTTRIGGERED		0xffff
#define MEDUSA_EVTYPE_TRIGGEREDATSUBJECT	0x0000	/* for the beauty of L2 */
#define MEDUSA_EVTYPE_TRIGGEREDATOBJECT		0x8000

#define MEDUSA_EVTYPE_TRIGGEREDBYSUBJECTBIT	0x0000	/* for the beauty of L2 */
#define MEDUSA_EVTYPE_TRIGGEREDBYOBJECTTBIT	0x4000

#define MEDUSA_ACCTYPE_NOTTRIGGERED		MEDUSA_EVTYPE_NOTTRIGGERED
#define MEDUSA_ACCTYPE_TRIGGEREDATSUBJECT (MEDUSA_EVTYPE_TRIGGEREDATSUBJECT | \
				MEDUSA_EVTYPE_TRIGGEREDBYSUBJECTBIT)
#define MEDUSA_ACCTYPE_TRIGGEREDATOBJECT (MEDUSA_EVTYPE_TRIGGEREDATOBJECT | \
				MEDUSA_EVTYPE_TRIGGEREDBYOBJECTTBIT)

/* internal macro */
#define ___MEDUSA_EVENTOP(evname,kobjptr,OP,WHAT,WHERE) \
		OP(MED_EVTYPEOF(evname).WHAT, &((kobjptr)->med_ ## WHERE.act))

/* is the event monitored (at object) ? */
#define MEDUSA_MONITORED_EVENT_O(evname,kobjptr) \
	___MEDUSA_EVENTOP(evname,kobjptr,MED_TST_BIT,bitnr & 0x3fff, object)
/* is the event monitored (at subject) ? */
#define MEDUSA_MONITORED_EVENT_S(evname,kobjptr) \
	___MEDUSA_EVENTOP(evname,kobjptr,MED_TST_BIT,bitnr, subject)
/* set the event monitoring at object */
#define MEDUSA_MONITOR_EVENT_O(evname,kobjptr) \
	___MEDUSA_EVENTOP(evname,kobjptr,MED_SET_BIT,bitnr & 0x3fff, object)
/* set the event monitoring at subject */
#define MEDUSA_MONITOR_EVENT_S(evname,kobjptr) \
	___MEDUSA_EVENTOP(evname,kobjptr,MED_SET_BIT,bitnr, subject)
/* unset the event monitoring at object */
#define MEDUSA_UNMONITOR_EVENT_O(evname,kobjptr) \
	___MEDUSA_EVENTOP(evname,kobjptr,MED_CLR_BIT,bitnr & 0x3fff, object)
/* unset the event monitoring at subject */
#define MEDUSA_UNMONITOR_EVENT_S(evname,kobjptr) \
	___MEDUSA_EVENTOP(evname,kobjptr,MED_CLR_BIT,bitnr, subject)

#define MEDUSA_MONITORED_ACCESS_O(evname,kobjptr) \
		MEDUSA_MONITORED_EVENT_O(evname,kobjptr)
#define MEDUSA_MONITORED_ACCESS_S(evname,kobjptr) \
		MEDUSA_MONITORED_EVENT_S(evname,kobjptr)
#define MEDUSA_MONITOR_ACCESS_O(evname,kobjptr) \
		MEDUSA_MONITOR_EVENT_O(evname,kobjptr)
#define MEDUSA_MONITOR_ACCESS_S(evname,kobjptr) \
		MEDUSA_MONITOR_EVENT_S(evname,kobjptr)
#define MEDUSA_UNMONITOR_ACCESS_O(evname,kobjptr) \
		MEDUSA_UNMONITOR_EVENT_O(evname,kobjptr)
#define MEDUSA_UNMONITOR_ACCESS_S(evname,kobjptr) \
		MEDUSA_UNMONITOR_EVENT_S(evname,kobjptr)

	cinfo_t cinfo; /* l4 hint */
#ifdef CONFIG_MEDUSA_PROFILING
	unsigned long long l2_to_l4;
	unsigned long long l4_to_l2;
#endif

	/* l2-defined data */
	char name[MEDUSA_EVNAME_MAX];		/* string: event name */
	struct medusa_kclass_s * arg_kclass[2];	/* kclasses of arguments */
	char arg_name[2][MEDUSA_ATTRNAME_MAX];	/* names of arguments */
	unsigned int event_size;		/* sizeof(event) */
	struct medusa_attribute_s * attr;	/* attributes */
};

#ifdef CONFIG_MEDUSA_PROFILING
#define MEDUSA_DEFAULT_EVTYPE_HEADER \
	NULL,	/*                   register_evtype */ \
	0 /* bitnr */, \
	0 /* cinfo */, \
	0, 0
#else
#define MEDUSA_DEFAULT_EVTYPE_HEADER \
	NULL,	/*                   register_evtype */ \
	0 /* bitnr */, \
	0 /* cinfo */
#endif
#define MEDUSA_DEFAULT_ACCTYPE_HEADER MEDUSA_DEFAULT_EVTYPE_HEADER

#define MED_EVTYPE(structname,evtypename,s1name,arg1name,s2name,arg2name) \
	struct medusa_evtype_s (MED_EVTYPEOF(structname)) = { 		\
		MEDUSA_DEFAULT_ACCTYPE_HEADER,				\
		(evtypename),						\
		{ &MED_KCLASSOF(s1name), &MED_KCLASSOF(s2name) },	\
		{ (arg1name), (arg2name) },				\
		sizeof(struct structname),				\
		MED_ATTRSOF(structname)					\
	}
#define MED_ACCTYPE(structname,acctypename,s1name,arg1name,s2name,arg2name) \
		MED_EVTYPE(structname,acctypename,s1name,arg1name,s2name,arg2name)

/* this is the access header - use it at the beginning of l2 structures */
#define MEDUSA_ACCESS_HEADER \
	struct medusa_evtype_s * evtype_id
/* used by l3 and l4 to easily access the header of l2 structures */
struct medusa_event_s {
	MEDUSA_ACCESS_HEADER;
	unsigned char data[0];
};

#endif
