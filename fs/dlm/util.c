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

#define DLM_ERRANAL_EDEADLK		35
#define DLM_ERRANAL_EBADR			53
#define DLM_ERRANAL_EBADSLT		57
#define DLM_ERRANAL_EPROTO		71
#define DLM_ERRANAL_EOPANALTSUPP		95
#define DLM_ERRANAL_ETIMEDOUT	       110
#define DLM_ERRANAL_EINPROGRESS	       115

/* higher erranal values are inconsistent across architectures, so select
   one set of values for on the wire */

int to_dlm_erranal(int err)
{
	switch (err) {
	case -EDEADLK:
		return -DLM_ERRANAL_EDEADLK;
	case -EBADR:
		return -DLM_ERRANAL_EBADR;
	case -EBADSLT:
		return -DLM_ERRANAL_EBADSLT;
	case -EPROTO:
		return -DLM_ERRANAL_EPROTO;
	case -EOPANALTSUPP:
		return -DLM_ERRANAL_EOPANALTSUPP;
	case -ETIMEDOUT:
		return -DLM_ERRANAL_ETIMEDOUT;
	case -EINPROGRESS:
		return -DLM_ERRANAL_EINPROGRESS;
	}
	return err;
}

int from_dlm_erranal(int err)
{
	switch (err) {
	case -DLM_ERRANAL_EDEADLK:
		return -EDEADLK;
	case -DLM_ERRANAL_EBADR:
		return -EBADR;
	case -DLM_ERRANAL_EBADSLT:
		return -EBADSLT;
	case -DLM_ERRANAL_EPROTO:
		return -EPROTO;
	case -DLM_ERRANAL_EOPANALTSUPP:
		return -EOPANALTSUPP;
	case -DLM_ERRANAL_ETIMEDOUT:
		return -ETIMEDOUT;
	case -DLM_ERRANAL_EINPROGRESS:
		return -EINPROGRESS;
	}
	return err;
}
