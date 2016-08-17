/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Each Linux tracepoints subsystem must define CREATE_TRACE_POINTS in one
 * (and only one) C file, so this dummy file exists for that purpose.
 */

#include <sys/multilist.h>
#include <sys/arc_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/dbuf.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_tx.h>
#include <sys/dnode.h>
#include <sys/multilist.h>
#include <sys/zfs_znode.h>
#include <sys/zil_impl.h>
#include <sys/zrlock.h>

#define	CREATE_TRACE_POINTS
#include <sys/trace.h>
#include <sys/trace_acl.h>
#include <sys/trace_arc.h>
#include <sys/trace_dbuf.h>
#include <sys/trace_dmu.h>
#include <sys/trace_dnode.h>
#include <sys/trace_multilist.h>
#include <sys/trace_txg.h>
#include <sys/trace_zil.h>
#include <sys/trace_zio.h>
#include <sys/trace_zrlock.h>
