#ifndef IOU_CORE_H
#define IOU_CORE_H

#include <linux/errno.h>
#include "io_uring_types.h"

static inline void io_req_set_res(struct io_kiocb *req, s32 res, u32 cflags)
{
	req->cqe.res = res;
	req->cqe.flags = cflags;
}

#endif
