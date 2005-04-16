/*
 *	File with in-memory structures of old quota format
 */

#ifndef _LINUX_DQBLK_V1_H
#define _LINUX_DQBLK_V1_H

/* Id of quota format */
#define QFMT_VFS_OLD 1

/* Root squash turned on */
#define V1_DQF_RSQUASH 1

/* Special information about quotafile */
struct v1_mem_dqinfo {
};

#endif	/* _LINUX_DQBLK_V1_H */
