#ifndef __UAPI_IFE_H
#define __UAPI_IFE_H

#define IFE_METAHDRLEN 2

enum {
	IFE_META_SKBMARK = 1,
	IFE_META_HASHID,
	IFE_META_PRIO,
	IFE_META_QMAP,
	IFE_META_TCINDEX,
	__IFE_META_MAX
};

/*Can be overridden at runtime by module option*/
#define IFE_META_MAX (__IFE_META_MAX - 1)

#endif
