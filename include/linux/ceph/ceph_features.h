#ifndef __CEPH_FEATURES
#define __CEPH_FEATURES

/*
 * feature bits
 */
#define CEPH_FEATURE_UID            (1<<0)
#define CEPH_FEATURE_NOSRCADDR      (1<<1)
#define CEPH_FEATURE_MONCLOCKCHECK  (1<<2)
#define CEPH_FEATURE_FLOCK          (1<<3)
#define CEPH_FEATURE_SUBSCRIBE2     (1<<4)
#define CEPH_FEATURE_MONNAMES       (1<<5)
#define CEPH_FEATURE_RECONNECT_SEQ  (1<<6)
#define CEPH_FEATURE_DIRLAYOUTHASH  (1<<7)
/* bits 8-17 defined by user-space; not supported yet here */
#define CEPH_FEATURE_CRUSH_TUNABLES (1<<18)

/*
 * Features supported.
 */
#define CEPH_FEATURES_SUPPORTED_DEFAULT  \
	(CEPH_FEATURE_NOSRCADDR |	 \
	 CEPH_FEATURE_CRUSH_TUNABLES)

#define CEPH_FEATURES_REQUIRED_DEFAULT   \
	(CEPH_FEATURE_NOSRCADDR)
#endif
