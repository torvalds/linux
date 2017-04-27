#ifndef _MEDUSA_MODEL_H
#define _MEDUSA_MODEL_H

#include <linux/medusa/l3/config.h>

#pragma GCC optimize ("O0")

/* this header file defines the VS model */

/* objects and subjects are two different kinds of kobjects:
 * `subject' is the initiator of some operation, `object' is
 * the target of operation.
 */

#if CONFIG_MEDUSA_VS <= 32
typedef u_int32_t vs_t;
#else
typedef struct { u_int32_t vspack[((CONFIG_MEDUSA_VS+31)/32)]; } vs_t;
#endif
typedef u_int32_t act_t;
typedef struct { /* this is at each subject */
	u_int32_t data[4];
} s_cinfo_t;
typedef struct { /* this is at each object */
	u_int64_t data[1];
} o_cinfo_t;
typedef void* cinfo_t; /* this is at kclass; must be able to hold pointer */

struct medusa_object_s {
	vs_t vs;	/* virt. spaces of this object */
	act_t act;	/* actions on this object, which are reported to L4 */
			/* this may slightly correspond with defined access
			   types ;> */
	o_cinfo_t cinfo;/* l4 hint */
	int magic;	/* whether this piece of crap is valid */
};

struct medusa_subject_s {
	vs_t vsr;	/* which vs I can read from */
	vs_t vsw;	/* which vs I can write to */
	vs_t vss;	/* which vs I can see */
	act_t act;	/* which actions of me are monitored. this may slig.. */
	s_cinfo_t cinfo;/* l4 hint */
	/* subject does not have a magic. As it executes operations on its
	 * own right, it surely gets the vs* spaces set by auth. server some
	 * time.
	 */
};

#define _VS(X)	((X)->vs)
#define _VSR(X)	((X)->vsr)
#define _VSW(X)	((X)->vsw)
#define _VSS(X)	((X)->vss)

#if CONFIG_MEDUSA_VS <= 32
#define VS_ISSUBSET(X,Y)	((X) & ~((Y)) == 0)
#define VS_ISSUPERSET(X,Y)	VS_ISSUBSET((Y),(X))
#define VS_INTERSECT(X,Y)	(((X) & (Y)) != 0)
#else
static inline int VS_ISSUBSET(vs_t X, vs_t Y)
{
	int i;
	for (i=0; i<(CONFIG_MEDUSA_VS+31)/32; i++)
		if (X.vspack[i] & ~Y.vspack[i])
			return 0;
	return 1;
}
#define VS_ISSUPERSET(X,Y)	VS_ISSUBSET((Y),(X))
static inline int VS_INTERSECT(vs_t X, vs_t Y)
{
	int i;
	for (i=0; i<(CONFIG_MEDUSA_VS+31)/32; i++)
		if (X.vspack[i] & Y.vspack[i])
			return 1;
	return 0;
}
#endif

/* infrastructure for l1, l2 */

#define VS(X) _VS(&((X)->med_object))
#define VSR(X) _VSR(&((X)->med_subject))
#define VSW(X) _VSW(&((X)->med_subject))
#define VSS(X) _VSS(&((X)->med_subject))

/* this is an object data - add to system structures and kobjects, which act as
   objects of some operation */
#define MEDUSA_OBJECT_VARS \
	struct medusa_object_s med_object
#define COPY_MEDUSA_OBJECT_VARS(to,from) \
	do { \
		(to)->med_object = (from)->med_object; \
	} while (0)
#if CONFIG_MEDUSA_VS <= 32
#define INIT_MEDUSA_OBJECT_VARS(ptr) \
	do { /* don't touch, unless you REALLY know what you are doing. */ \
		(ptr)->med_object.vs = 0xffffffff;			\
		(ptr)->med_object.act = 0xffffffff;			\
		(ptr)->med_object.cinfo.data[0] = 0;			\
		(ptr)->med_object.magic = 0;				\
	} while (0)
#define UNMONITOR_MEDUSA_OBJECT_VARS(ptr) \
	do { /* don't touch, unless you REALLY know what you are doing. */ \
		(ptr)->med_object.vs = 0xffffffff;			\
		(ptr)->med_object.act = 0;				\
	} while (0)
#else
#define INIT_MEDUSA_OBJECT_VARS(ptr) \
	do { /* don't touch, unless you REALLY know what you are doing. */ \
		int i;							\
		for (i=0; i<(CONFIG_MEDUSA_VS+31)/32; i++)		\
			(ptr)->med_object.vs.vspack[i] = 0xffffffff;	\
		(ptr)->med_object.act = 0xffffffff;			\
		(ptr)->med_object.cinfo.data[0] = 0;			\
		(ptr)->med_object.magic = 0;				\
	} while (0)
#define UNMONITOR_MEDUSA_OBJECT_VARS(ptr) \
	do { /* don't touch, unless you REALLY know what you are doing. */ \
		int i;							\
		for (i=0; i<(CONFIG_MEDUSA_VS+31)/32; i++)		\
			(ptr)->med_object.vs.vspack[i] = 0xffffffff;	\
		(ptr)->med_object.act = 0;				\
	} while (0)
#endif

/* this is an subject data - add to system structures and kobjects, which act as
   subjects of some operation */
#define MEDUSA_SUBJECT_VARS \
	struct medusa_subject_s med_subject
#define COPY_MEDUSA_SUBJECT_VARS(to,from) \
	do { \
		(to)->med_subject = (from)->med_subject; \
	} while (0)
#if CONFIG_MEDUSA_VS <= 32
#define INIT_MEDUSA_SUBJECT_VARS(ptr) \
	do { /* don't touch, unless you REALLY know what you are doing. */ \
		(ptr)->med_subject.vss = 0xffffffff;			\
		(ptr)->med_subject.vsr = 0xffffffff;			\
		(ptr)->med_subject.vsw = 0xffffffff;			\
		(ptr)->med_subject.act = 0xffffffff;			\
		(ptr)->med_subject.cinfo.data[0] = 0;			\
		(ptr)->med_subject.cinfo.data[1] = 0;			\
		(ptr)->med_subject.cinfo.data[2] = 0;			\
		(ptr)->med_subject.cinfo.data[3] = 0;			\
	} while (0)
#define UNMONITOR_MEDUSA_SUBJECT_VARS(ptr) \
	do { /* don't touch, unless you REALLY know what you are doing. */ \
		(ptr)->med_subject.vss = 0xffffffff;			\
		(ptr)->med_subject.vsr = 0xffffffff;			\
		(ptr)->med_subject.vsw = 0xffffffff;			\
		(ptr)->med_subject.act = 0;				\
	} while (0)
#else
#define INIT_MEDUSA_SUBJECT_VARS(ptr) \
	do { /* don't touch, unless you REALLY know what you are doing. */ \
		int i;							\
		for (i=0; i<(CONFIG_MEDUSA_VS+31)/32; i++)		\
			(ptr)->med_subject.vss.vspack[i] =		\
			(ptr)->med_subject.vsr.vspack[i] =		\
			(ptr)->med_subject.vsw.vspack[i] =		\
				 0xffffffff;				\
		(ptr)->med_subject.act = 0xffffffff;			\
		(ptr)->med_subject.cinfo.data[0] = 0;			\
		(ptr)->med_subject.cinfo.data[1] = 0;			\
		(ptr)->med_subject.cinfo.data[2] = 0;			\
		(ptr)->med_subject.cinfo.data[3] = 0;			\
	} while (0)
#define UNMONITOR_MEDUSA_SUBJECT_VARS(ptr) \
	do { /* don't touch, unless you REALLY know what you are doing. */ \
		int i;							\
		for (i=0; i<(CONFIG_MEDUSA_VS+31)/32; i++)		\
			(ptr)->med_subject.vss.vspack[i] =		\
			(ptr)->med_subject.vsr.vspack[i] =		\
			(ptr)->med_subject.vsw.vspack[i] =		\
				 0xffffffff;				\
		(ptr)->med_subject.act = 0;				\
	} while (0)
#endif

/* read the comment in l3/registry.c on this magic number */
extern int medusa_authserver_magic;
#define MED_MAGIC_VALID(pointer) \
	((pointer)->med_object.magic == medusa_authserver_magic)
#define MED_MAGIC_VALIDATE(pointer) \
	do { \
		(pointer)->med_object.magic = medusa_authserver_magic; \
	} while (0)
#define MED_MAGIC_INVALIDATE(pointer) \
	do { \
		(pointer)->med_object.magic = 0; \
	} while (0)

#endif /* _MEDUSA_MODEL_H */

