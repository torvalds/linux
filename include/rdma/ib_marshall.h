/*
 * Copyright (c) 2005-2006 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if !defined(IB_USER_MARSHALL_H)
#define IB_USER_MARSHALL_H

#include <rdma/ib_verbs.h>
#include <rdma/ib_sa.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_user_sa.h>

void ib_copy_qp_attr_to_user(struct ib_uverbs_qp_attr *dst,
			     struct ib_qp_attr *src);

void ib_copy_ah_attr_to_user(struct ib_uverbs_ah_attr *dst,
			     struct ib_ah_attr *src);

void ib_copy_path_rec_to_user(struct ib_user_path_rec *dst,
			      struct ib_sa_path_rec *src);

void ib_copy_path_rec_from_user(struct ib_sa_path_rec *dst,
				struct ib_user_path_rec *src);

#endif /* IB_USER_MARSHALL_H */
