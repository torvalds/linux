// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "rcom.h"
#include "util.h"

#define DLM_ERRNO_EDEADLK		35
#define DLM_ERRNO_EBADR			53
#define DLM_ERRNO_EBADSLT		57
#define DLM_ERRNO_EPROTO		71
#define DLM_ERRNO_EOPNOTSUPP		95
#define DLM_ERRNO_ETIMEDOUT	       110
#define DLM_ERRNO_EINPROGRESS	       115

/* higher errno values are inconsistent across architectures, so select
   one set of values for on the wire */

int to_dlm_errno(int err)
{
	switch (err) {
	case -EDEADLK:
		return -DLM_ERRNO_EDEADLK;
	case -EBADR:
		return -DLM_ERRNO_EBADR;
	case -EBADSLT:
		return -DLM_ERRNO_EBADSLT;
	case -EPROTO:
		return -DLM_ERRNO_EPROTO;
	case -EOPNOTSUPP:
		return -DLM_ERRNO_EOPNOTSUPP;
	case -ETIMEDOUT:
		return -DLM_ERRNO_ETIMEDOUT;
	case -EINPROGRESS:
		return -DLM_ERRNO_EINPROGRESS;
	}
	return err;
}

int from_dlm_errno(int err)
{
	switch (err) {
	case -DLM_ERRNO_EDEADLK:
		return -EDEADLK;
	case -DLM_ERRNO_EBADR:
		return -EBADR;
	case -DLM_ERRNO_EBADSLT:
		return -EBADSLT;
	case -DLM_ERRNO_EPROTO:
		return -EPROTO;
	case -DLM_ERRNO_EOPNOTSUPP:
		return -EOPNOTSUPP;
	case -DLM_ERRNO_ETIMEDOUT:
		return -ETIMEDOUT;
	case -DLM_ERRNO_EINPROGRESS:
		return -EINPROGRESS;
	}
	return err;
}
