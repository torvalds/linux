/*
 *  ZPIOS is a heavily modified version of the original PIOS test code.
 *  It is designed to have the test code running in the Linux kernel
 *  against ZFS while still being flexibly controlled from user space.
 *
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  LLNL-CODE-403049
 *
 *  Original PIOS Test Code
 *  Copyright (C) 2004 Cluster File Systems, Inc.
 *  Written by Peter Braam <braam@clusterfs.com>
 *             Atul Vidwansa <atul@clusterfs.com>
 *             Milind Dumbare <milind@clusterfs.com>
 *
 *  This file is part of ZFS on Linux.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  ZPIOS is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  ZPIOS is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with ZPIOS.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Copyright (c) 2015, Intel Corporation.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/dsl_destroy.h>
#include <linux/miscdevice.h>
#include "zpios-internal.h"


static char *zpios_tag = "zpios_tag";

static int
zpios_upcall(char *path, char *phase, run_args_t *run_args, int rc)
{
	/*
	 * This is stack heavy but it should be OK since we are only
	 * making the upcall between tests when the stack is shallow.
	 */
	char id[16], chunk_size[16], region_size[16], thread_count[16];
	char region_count[16], offset[16], region_noise[16], chunk_noise[16];
	char thread_delay[16], flags[16], result[8];
	char *argv[16], *envp[4];

	if ((path == NULL) || (strlen(path) == 0))
		return (-ENOENT);

	snprintf(id, 15, "%d", run_args->id);
	snprintf(chunk_size, 15, "%lu", (long unsigned)run_args->chunk_size);
	snprintf(region_size, 15, "%lu", (long unsigned) run_args->region_size);
	snprintf(thread_count, 15, "%u", run_args->thread_count);
	snprintf(region_count, 15, "%u", run_args->region_count);
	snprintf(offset, 15, "%lu", (long unsigned)run_args->offset);
	snprintf(region_noise, 15, "%u", run_args->region_noise);
	snprintf(chunk_noise, 15, "%u", run_args->chunk_noise);
	snprintf(thread_delay, 15, "%u", run_args->thread_delay);
	snprintf(flags, 15, "0x%x", run_args->flags);
	snprintf(result, 7, "%d", rc);

	/* Passing 15 args to registered pre/post upcall */
	argv[0] = path;
	argv[1] = phase;
	argv[2] = strlen(run_args->log) ? run_args->log : "<none>";
	argv[3] = id;
	argv[4] = run_args->pool;
	argv[5] = chunk_size;
	argv[6] = region_size;
	argv[7] = thread_count;
	argv[8] = region_count;
	argv[9] = offset;
	argv[10] = region_noise;
	argv[11] = chunk_noise;
	argv[12] = thread_delay;
	argv[13] = flags;
	argv[14] = result;
	argv[15] = NULL;

	/* Passing environment for user space upcall */
	envp[0] = "HOME=/";
	envp[1] = "TERM=linux";
	envp[2] = "PATH=/sbin:/usr/sbin:/bin:/usr/bin";
	envp[3] = NULL;

	return (call_usermodehelper(path, argv, envp, UMH_WAIT_PROC));
}

static int
zpios_print(struct file *file, const char *format, ...)
{
	zpios_info_t *info = (zpios_info_t *)file->private_data;
	va_list adx;
	int rc;

	ASSERT(info);
	ASSERT(info->info_buffer);

	va_start(adx, format);
	spin_lock(&info->info_lock);

	/* Don't allow the kernel to start a write in the red zone */
	if ((int)(info->info_head - info->info_buffer) >
	    (info->info_size - ZPIOS_INFO_BUFFER_REDZONE)) {
		rc = -EOVERFLOW;
	} else {
		rc = vsprintf(info->info_head, format, adx);
		if (rc >= 0)
			info->info_head += rc;
	}

	spin_unlock(&info->info_lock);
	va_end(adx);

	return (rc);
}

static uint64_t
zpios_dmu_object_create(run_args_t *run_args, objset_t *os)
{
	struct dmu_tx *tx;
	uint64_t obj = 0ULL;
	uint64_t blksize = run_args->block_size;
	int rc;

	if (blksize < SPA_MINBLOCKSIZE ||
	    blksize > spa_maxblocksize(dmu_objset_spa(os)) ||
	    !ISP2(blksize)) {
		zpios_print(run_args->file,
		    "invalid block size for pool: %d\n", (int)blksize);
		return (obj);
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, OBJ_SIZE);
	rc = dmu_tx_assign(tx, TXG_WAIT);
	if (rc) {
		zpios_print(run_args->file,
		    "dmu_tx_assign() failed: %d\n", rc);
		dmu_tx_abort(tx);
		return (obj);
	}

	obj = dmu_object_alloc(os, DMU_OT_UINT64_OTHER, 0, DMU_OT_NONE, 0, tx);
	rc = dmu_object_set_blocksize(os, obj, blksize, 0, tx);
	if (rc) {
		zpios_print(run_args->file,
		    "dmu_object_set_blocksize to %d failed: %d\n",
		    (int)blksize, rc);
		dmu_tx_abort(tx);
		return (obj);
	}

	dmu_tx_commit(tx);

	return (obj);
}

static int
zpios_dmu_object_free(run_args_t *run_args, objset_t *os, uint64_t obj)
{
	struct dmu_tx *tx;
	int rc;

	tx = dmu_tx_create(os);
	dmu_tx_hold_free(tx, obj, 0, DMU_OBJECT_END);
	rc = dmu_tx_assign(tx, TXG_WAIT);
	if (rc) {
		zpios_print(run_args->file,
		    "dmu_tx_assign() failed: %d\n", rc);
		dmu_tx_abort(tx);
		return (rc);
	}

	rc = dmu_object_free(os, obj, tx);
	if (rc) {
		zpios_print(run_args->file,
		    "dmu_object_free() failed: %d\n", rc);
		dmu_tx_abort(tx);
		return (rc);
	}

	dmu_tx_commit(tx);

	return (0);
}

static int
zpios_dmu_setup(run_args_t *run_args)
{
	zpios_time_t *t = &(run_args->stats.cr_time);
	objset_t *os;
	char name[32];
	uint64_t obj = 0ULL;
	int i, rc = 0, rc2;

	(void) zpios_upcall(run_args->pre, PHASE_PRE_CREATE, run_args, 0);
	t->start = zpios_timespec_now();

	(void) snprintf(name, 32, "%s/id_%d", run_args->pool, run_args->id);
	rc = dmu_objset_create(name, DMU_OST_OTHER, 0, NULL, NULL);
	if (rc) {
		zpios_print(run_args->file, "Error dmu_objset_create(%s, ...) "
		    "failed: %d\n", name, rc);
		goto out;
	}

	rc = dmu_objset_own(name, DMU_OST_OTHER, 0, zpios_tag, &os);
	if (rc) {
		zpios_print(run_args->file, "Error dmu_objset_own(%s, ...) "
		    "failed: %d\n", name, rc);
		goto out_destroy;
	}

	if (!(run_args->flags & DMU_FPP)) {
		obj = zpios_dmu_object_create(run_args, os);
		if (obj == 0) {
			rc = -EBADF;
			zpios_print(run_args->file, "Error zpios_dmu_"
			    "object_create() failed, %d\n", rc);
			goto out_destroy;
		}
	}

	for (i = 0; i < run_args->region_count; i++) {
		zpios_region_t *region;

		region = &run_args->regions[i];
		mutex_init(&region->lock, NULL, MUTEX_DEFAULT, NULL);

		if (run_args->flags & DMU_FPP) {
			/* File per process */
			region->obj.os  = os;
			region->obj.obj = zpios_dmu_object_create(run_args, os);
			ASSERT(region->obj.obj > 0); /* XXX - Handle this */
			region->wr_offset   = run_args->offset;
			region->rd_offset   = run_args->offset;
			region->init_offset = run_args->offset;
			region->max_offset  = run_args->offset +
			    run_args->region_size;
		} else {
			/* Single shared file */
			region->obj.os  = os;
			region->obj.obj = obj;
			region->wr_offset   = run_args->offset * i;
			region->rd_offset   = run_args->offset * i;
			region->init_offset = run_args->offset * i;
			region->max_offset  = run_args->offset *
			    i + run_args->region_size;
		}
	}

	run_args->os = os;
out_destroy:
	if (rc) {
		rc2 = dsl_destroy_head(name);
		if (rc2)
			zpios_print(run_args->file, "Error dsl_destroy_head"
			    "(%s, ...) failed: %d\n", name, rc2);
	}
out:
	t->stop  = zpios_timespec_now();
	t->delta = zpios_timespec_sub(t->stop, t->start);
	(void) zpios_upcall(run_args->post, PHASE_POST_CREATE, run_args, rc);

	return (rc);
}

static int
zpios_setup_run(run_args_t **run_args, zpios_cmd_t *kcmd, struct file *file)
{
	run_args_t *ra;
	int rc, size;

	size = sizeof (*ra) + kcmd->cmd_region_count * sizeof (zpios_region_t);

	ra = vmem_zalloc(size, KM_SLEEP);

	*run_args = ra;
	snprintf(ra->pool, sizeof (ra->pool), "%s", kcmd->cmd_pool);
	snprintf(ra->pre, sizeof (ra->pre), "%s", kcmd->cmd_pre);
	snprintf(ra->post, sizeof (ra->post), "%s", kcmd->cmd_post);
	snprintf(ra->log, sizeof (ra->log), "%s", kcmd->cmd_log);

	ra->id			= kcmd->cmd_id;
	ra->chunk_size		= kcmd->cmd_chunk_size;
	ra->thread_count	= kcmd->cmd_thread_count;
	ra->region_count	= kcmd->cmd_region_count;
	ra->region_size		= kcmd->cmd_region_size;
	ra->offset		= kcmd->cmd_offset;
	ra->region_noise	= kcmd->cmd_region_noise;
	ra->chunk_noise		= kcmd->cmd_chunk_noise;
	ra->thread_delay	= kcmd->cmd_thread_delay;
	ra->flags		= kcmd->cmd_flags;
	ra->block_size		= kcmd->cmd_block_size;
	ra->stats.wr_data	= 0;
	ra->stats.wr_chunks	= 0;
	ra->stats.rd_data	= 0;
	ra->stats.rd_chunks	= 0;
	ra->region_next		= 0;
	ra->file		= file;
	mutex_init(&ra->lock_work, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&ra->lock_ctl, NULL, MUTEX_DEFAULT, NULL);

	(void) zpios_upcall(ra->pre, PHASE_PRE_RUN, ra, 0);

	rc = zpios_dmu_setup(ra);
	if (rc) {
		mutex_destroy(&ra->lock_ctl);
		mutex_destroy(&ra->lock_work);
		vmem_free(ra, size);
		*run_args = NULL;
	}

	return (rc);
}

static int
zpios_get_work_item(run_args_t *run_args, dmu_obj_t *obj, __u64 *offset,
    __u32 *chunk_size, zpios_region_t **region, __u32 flags)
{
	int i, j, count = 0;
	unsigned int random_int;

	get_random_bytes(&random_int, sizeof (unsigned int));

	mutex_enter(&run_args->lock_work);
	i = run_args->region_next;

	/*
	 * XXX: I don't much care for this chunk selection mechansim
	 * there's the potential to burn a lot of time here doing nothing
	 * useful while holding the global lock.  This could give some
	 * misleading performance results.  I'll fix it latter.
	 */
	while (count < run_args->region_count) {
		__u64 *rw_offset;
		zpios_time_t *rw_time;

		j = i % run_args->region_count;
		*region = &(run_args->regions[j]);

		if (flags & DMU_WRITE) {
			rw_offset = &((*region)->wr_offset);
			rw_time = &((*region)->stats.wr_time);
		} else {
			rw_offset = &((*region)->rd_offset);
			rw_time = &((*region)->stats.rd_time);
		}

		/* test if region is fully written */
		if (*rw_offset + *chunk_size > (*region)->max_offset) {
			i++;
			count++;

			if (unlikely(rw_time->stop.ts_sec == 0) &&
			    unlikely(rw_time->stop.ts_nsec == 0))
				rw_time->stop = zpios_timespec_now();

			continue;
		}

		*offset = *rw_offset;
		*obj = (*region)->obj;
		*rw_offset += *chunk_size;

		/* update ctl structure */
		if (run_args->region_noise) {
			get_random_bytes(&random_int, sizeof (unsigned int));
			run_args->region_next +=
			    random_int % run_args->region_noise;
		} else {
			run_args->region_next++;
		}

		mutex_exit(&run_args->lock_work);
		return (1);
	}

	/* nothing left to do */
	mutex_exit(&run_args->lock_work);

	return (0);
}

static void
zpios_remove_objset(run_args_t *run_args)
{
	zpios_time_t *t = &(run_args->stats.rm_time);
	zpios_region_t *region;
	char name[32];
	int rc = 0, i;

	(void) zpios_upcall(run_args->pre, PHASE_PRE_REMOVE, run_args, 0);
	t->start = zpios_timespec_now();

	(void) snprintf(name, 32, "%s/id_%d", run_args->pool, run_args->id);

	if (run_args->flags & DMU_REMOVE) {
		if (run_args->flags & DMU_FPP) {
			for (i = 0; i < run_args->region_count; i++) {
				region = &run_args->regions[i];
				rc = zpios_dmu_object_free(run_args,
				    region->obj.os, region->obj.obj);
				if (rc)
					zpios_print(run_args->file,
					    "Error removing object %d, %d\n",
					    (int)region->obj.obj, rc);
			}
		} else {
			region = &run_args->regions[0];
			rc = zpios_dmu_object_free(run_args,
			    region->obj.os, region->obj.obj);
			if (rc)
				zpios_print(run_args->file,
				    "Error removing object %d, %d\n",
				    (int)region->obj.obj, rc);
		}
	}

	dmu_objset_disown(run_args->os, zpios_tag);

	if (run_args->flags & DMU_REMOVE) {
		rc = dsl_destroy_head(name);
		if (rc)
			zpios_print(run_args->file, "Error dsl_destroy_head"
			    "(%s, ...) failed: %d\n", name, rc);
	}

	t->stop  = zpios_timespec_now();
	t->delta = zpios_timespec_sub(t->stop, t->start);
	(void) zpios_upcall(run_args->post, PHASE_POST_REMOVE, run_args, rc);
}

static void
zpios_cleanup_run(run_args_t *run_args)
{
	int i, size = 0;

	if (run_args == NULL)
		return;

	if (run_args->threads != NULL) {
		for (i = 0; i < run_args->thread_count; i++) {
			if (run_args->threads[i]) {
				mutex_destroy(&run_args->threads[i]->lock);
				kmem_free(run_args->threads[i],
				    sizeof (thread_data_t));
			}
		}

		kmem_free(run_args->threads,
		    sizeof (thread_data_t *) * run_args->thread_count);
	}

	for (i = 0; i < run_args->region_count; i++)
		mutex_destroy(&run_args->regions[i].lock);

	mutex_destroy(&run_args->lock_work);
	mutex_destroy(&run_args->lock_ctl);
	size = run_args->region_count * sizeof (zpios_region_t);

	vmem_free(run_args, sizeof (*run_args) + size);
}

static int
zpios_dmu_write(run_args_t *run_args, objset_t *os, uint64_t object,
    uint64_t offset, uint64_t size, const void *buf)
{
	struct dmu_tx *tx;
	int rc, how = TXG_WAIT;
//	int flags = 0;

	if (run_args->flags & DMU_WRITE_NOWAIT)
		how = TXG_NOWAIT;

	while (1) {
		tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, object, offset, size);
		rc = dmu_tx_assign(tx, how);

		if (rc) {
			if (rc == ERESTART && how == TXG_NOWAIT) {
				dmu_tx_wait(tx);
				dmu_tx_abort(tx);
				continue;
			}
			zpios_print(run_args->file,
			    "Error in dmu_tx_assign(), %d", rc);
			dmu_tx_abort(tx);
			return (rc);
		}
		break;
	}

//	if (run_args->flags & DMU_WRITE_ZC)
//		flags |= DMU_WRITE_ZEROCOPY;

	dmu_write(os, object, offset, size, buf, tx);
	dmu_tx_commit(tx);

	return (0);
}

static int
zpios_dmu_read(run_args_t *run_args, objset_t *os, uint64_t object,
    uint64_t offset, uint64_t size, void *buf)
{
	int flags = 0;

//	if (run_args->flags & DMU_READ_ZC)
//		flags |= DMU_READ_ZEROCOPY;

	if (run_args->flags & DMU_READ_NOPF)
		flags |= DMU_READ_NO_PREFETCH;

	return (dmu_read(os, object, offset, size, buf, flags));
}

static int
zpios_thread_main(void *data)
{
	thread_data_t *thr = (thread_data_t *)data;
	run_args_t *run_args = thr->run_args;
	zpios_time_t t;
	dmu_obj_t obj;
	__u64 offset;
	__u32 chunk_size;
	zpios_region_t *region;
	char *buf;
	unsigned int random_int;
	int chunk_noise = run_args->chunk_noise;
	int chunk_noise_tmp = 0;
	int thread_delay = run_args->thread_delay;
	int thread_delay_tmp = 0;
	int i, rc = 0;

	if (chunk_noise) {
		get_random_bytes(&random_int, sizeof (unsigned int));
		chunk_noise_tmp = (random_int % (chunk_noise * 2))-chunk_noise;
	}

	/*
	 * It's OK to vmem_alloc() this memory because it will be copied
	 * in to the slab and pointers to the slab copy will be setup in
	 * the bio when the IO is submitted.  This of course is not ideal
	 * since we want a zero-copy IO path if possible.  It would be nice
	 * to have direct access to those slab entries.
	 */
	chunk_size = run_args->chunk_size + chunk_noise_tmp;
	buf = (char *)vmem_alloc(chunk_size, KM_SLEEP);
	ASSERT(buf);

	/* Trivial data verification pattern for now. */
	if (run_args->flags & DMU_VERIFY)
		memset(buf, 'z', chunk_size);

	/* Write phase */
	mutex_enter(&thr->lock);
	thr->stats.wr_time.start = zpios_timespec_now();
	mutex_exit(&thr->lock);

	while (zpios_get_work_item(run_args, &obj, &offset,
	    &chunk_size, &region, DMU_WRITE)) {
		if (thread_delay) {
			get_random_bytes(&random_int, sizeof (unsigned int));
			thread_delay_tmp = random_int % thread_delay;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(thread_delay_tmp); /* In jiffies */
		}

		t.start = zpios_timespec_now();
		rc = zpios_dmu_write(run_args, obj.os, obj.obj,
		    offset, chunk_size, buf);
		t.stop  = zpios_timespec_now();
		t.delta = zpios_timespec_sub(t.stop, t.start);

		if (rc) {
			zpios_print(run_args->file, "IO error while doing "
			    "dmu_write(): %d\n", rc);
			break;
		}

		mutex_enter(&thr->lock);
		thr->stats.wr_data += chunk_size;
		thr->stats.wr_chunks++;
		thr->stats.wr_time.delta = zpios_timespec_add(
		    thr->stats.wr_time.delta, t.delta);
		mutex_exit(&thr->lock);

		mutex_enter(&region->lock);
		region->stats.wr_data += chunk_size;
		region->stats.wr_chunks++;
		region->stats.wr_time.delta = zpios_timespec_add(
		    region->stats.wr_time.delta, t.delta);

		/* First time region was accessed */
		if (region->init_offset == offset)
			region->stats.wr_time.start = t.start;

		mutex_exit(&region->lock);
	}

	mutex_enter(&run_args->lock_ctl);
	run_args->threads_done++;
	mutex_exit(&run_args->lock_ctl);

	mutex_enter(&thr->lock);
	thr->rc = rc;
	thr->stats.wr_time.stop = zpios_timespec_now();
	mutex_exit(&thr->lock);
	wake_up(&run_args->waitq);

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();

	/* Check if we should exit */
	mutex_enter(&thr->lock);
	rc = thr->rc;
	mutex_exit(&thr->lock);
	if (rc)
		goto out;

	/* Read phase */
	mutex_enter(&thr->lock);
	thr->stats.rd_time.start = zpios_timespec_now();
	mutex_exit(&thr->lock);

	while (zpios_get_work_item(run_args, &obj, &offset,
	    &chunk_size, &region, DMU_READ)) {
		if (thread_delay) {
			get_random_bytes(&random_int, sizeof (unsigned int));
			thread_delay_tmp = random_int % thread_delay;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(thread_delay_tmp); /* In jiffies */
		}

		if (run_args->flags & DMU_VERIFY)
			memset(buf, 0, chunk_size);

		t.start = zpios_timespec_now();
		rc = zpios_dmu_read(run_args, obj.os, obj.obj,
		    offset, chunk_size, buf);
		t.stop  = zpios_timespec_now();
		t.delta = zpios_timespec_sub(t.stop, t.start);

		if (rc) {
			zpios_print(run_args->file, "IO error while doing "
			    "dmu_read(): %d\n", rc);
			break;
		}

		/* Trivial data verification, expensive! */
		if (run_args->flags & DMU_VERIFY) {
			for (i = 0; i < chunk_size; i++) {
				if (buf[i] != 'z') {
					zpios_print(run_args->file,
					    "IO verify error: %d/%d/%d\n",
					    (int)obj.obj, (int)offset,
					    (int)chunk_size);
					break;
				}
			}
		}

		mutex_enter(&thr->lock);
		thr->stats.rd_data += chunk_size;
		thr->stats.rd_chunks++;
		thr->stats.rd_time.delta = zpios_timespec_add(
		    thr->stats.rd_time.delta, t.delta);
		mutex_exit(&thr->lock);

		mutex_enter(&region->lock);
		region->stats.rd_data += chunk_size;
		region->stats.rd_chunks++;
		region->stats.rd_time.delta = zpios_timespec_add(
		    region->stats.rd_time.delta, t.delta);

		/* First time region was accessed */
		if (region->init_offset == offset)
			region->stats.rd_time.start = t.start;

		mutex_exit(&region->lock);
	}

	mutex_enter(&run_args->lock_ctl);
	run_args->threads_done++;
	mutex_exit(&run_args->lock_ctl);

	mutex_enter(&thr->lock);
	thr->rc = rc;
	thr->stats.rd_time.stop = zpios_timespec_now();
	mutex_exit(&thr->lock);
	wake_up(&run_args->waitq);

out:
	vmem_free(buf, chunk_size);
	do_exit(0);

	return (rc); /* Unreachable, due to do_exit() */
}

static int
zpios_thread_done(run_args_t *run_args)
{
	ASSERT(run_args->threads_done <= run_args->thread_count);
	return (run_args->threads_done == run_args->thread_count);
}

static int
zpios_threads_run(run_args_t *run_args)
{
	struct task_struct *tsk, **tsks;
	thread_data_t *thr = NULL;
	zpios_time_t *tt = &(run_args->stats.total_time);
	zpios_time_t *tw = &(run_args->stats.wr_time);
	zpios_time_t *tr = &(run_args->stats.rd_time);
	int i, rc = 0, tc = run_args->thread_count;

	tsks = kmem_zalloc(sizeof (struct task_struct *) * tc, KM_SLEEP);

	run_args->threads = kmem_zalloc(sizeof (thread_data_t *)*tc, KM_SLEEP);

	init_waitqueue_head(&run_args->waitq);
	run_args->threads_done = 0;

	/* Create all the needed threads which will sleep until awoken */
	for (i = 0; i < tc; i++) {
		thr = kmem_zalloc(sizeof (thread_data_t), KM_SLEEP);

		thr->thread_no = i;
		thr->run_args = run_args;
		thr->rc = 0;
		mutex_init(&thr->lock, NULL, MUTEX_DEFAULT, NULL);
		run_args->threads[i] = thr;

		tsk = kthread_create(zpios_thread_main, (void *)thr,
		    "%s/%d", "zpios_io", i);
		if (IS_ERR(tsk)) {
			rc = -EINVAL;
			goto taskerr;
		}

		tsks[i] = tsk;
	}

	tt->start = zpios_timespec_now();

	/* Wake up all threads for write phase */
	(void) zpios_upcall(run_args->pre, PHASE_PRE_WRITE, run_args, 0);
	for (i = 0; i < tc; i++)
		wake_up_process(tsks[i]);

	/* Wait for write phase to complete */
	tw->start = zpios_timespec_now();
	wait_event(run_args->waitq, zpios_thread_done(run_args));
	tw->stop = zpios_timespec_now();
	(void) zpios_upcall(run_args->post, PHASE_POST_WRITE, run_args, rc);

	for (i = 0; i < tc; i++) {
		thr = run_args->threads[i];

		mutex_enter(&thr->lock);

		if (!rc && thr->rc)
			rc = thr->rc;

		run_args->stats.wr_data += thr->stats.wr_data;
		run_args->stats.wr_chunks += thr->stats.wr_chunks;
		mutex_exit(&thr->lock);
	}

	if (rc) {
		/* Wake up all threads and tell them to exit */
		for (i = 0; i < tc; i++) {
			mutex_enter(&thr->lock);
			thr->rc = rc;
			mutex_exit(&thr->lock);

			wake_up_process(tsks[i]);
		}
		goto out;
	}

	mutex_enter(&run_args->lock_ctl);
	ASSERT(run_args->threads_done == run_args->thread_count);
	run_args->threads_done = 0;
	mutex_exit(&run_args->lock_ctl);

	/* Wake up all threads for read phase */
	(void) zpios_upcall(run_args->pre, PHASE_PRE_READ, run_args, 0);
	for (i = 0; i < tc; i++)
		wake_up_process(tsks[i]);

	/* Wait for read phase to complete */
	tr->start = zpios_timespec_now();
	wait_event(run_args->waitq, zpios_thread_done(run_args));
	tr->stop = zpios_timespec_now();
	(void) zpios_upcall(run_args->post, PHASE_POST_READ, run_args, rc);

	for (i = 0; i < tc; i++) {
		thr = run_args->threads[i];

		mutex_enter(&thr->lock);

		if (!rc && thr->rc)
			rc = thr->rc;

		run_args->stats.rd_data += thr->stats.rd_data;
		run_args->stats.rd_chunks += thr->stats.rd_chunks;
		mutex_exit(&thr->lock);
	}
out:
	tt->stop  = zpios_timespec_now();
	tt->delta = zpios_timespec_sub(tt->stop, tt->start);
	tw->delta = zpios_timespec_sub(tw->stop, tw->start);
	tr->delta = zpios_timespec_sub(tr->stop, tr->start);

cleanup:
	kmem_free(tsks, sizeof (struct task_struct *) * tc);
	return (rc);

taskerr:
	/* Destroy all threads that were created successfully */
	for (i = 0; i < tc; i++)
		if (tsks[i] != NULL)
			(void) kthread_stop(tsks[i]);

	goto cleanup;
}

static int
zpios_do_one_run(struct file *file, zpios_cmd_t *kcmd,
    int data_size, void *data)
{
	run_args_t *run_args = { 0 };
	zpios_stats_t *stats = (zpios_stats_t *)data;
	int i, n, m, size, rc;

	if ((!kcmd->cmd_chunk_size) || (!kcmd->cmd_region_size) ||
	    (!kcmd->cmd_thread_count) || (!kcmd->cmd_region_count)) {
		zpios_print(file, "Invalid chunk_size, region_size, "
		    "thread_count, or region_count, %d\n", -EINVAL);
		return (-EINVAL);
	}

	if (!(kcmd->cmd_flags & DMU_WRITE) ||
	    !(kcmd->cmd_flags & DMU_READ)) {
		zpios_print(file, "Invalid flags, minimally DMU_WRITE "
		    "and DMU_READ must be set, %d\n", -EINVAL);
		return (-EINVAL);
	}

	if ((kcmd->cmd_flags & (DMU_WRITE_ZC | DMU_READ_ZC)) &&
	    (kcmd->cmd_flags & DMU_VERIFY)) {
		zpios_print(file, "Invalid flags, DMU_*_ZC incompatible "
		    "with DMU_VERIFY, used for performance analysis "
		    "only, %d\n", -EINVAL);
		return (-EINVAL);
	}

	/*
	 * Opaque data on return contains structs of the following form:
	 *
	 * zpios_stat_t stats[];
	 * stats[0]     = run_args->stats;
	 * stats[1-N]   = threads[N]->stats;
	 * stats[N+1-M] = regions[M]->stats;
	 *
	 * Where N is the number of threads, and M is the number of regions.
	 */
	size = (sizeof (zpios_stats_t) +
	    (kcmd->cmd_thread_count * sizeof (zpios_stats_t)) +
	    (kcmd->cmd_region_count * sizeof (zpios_stats_t)));
	if (data_size < size) {
		zpios_print(file, "Invalid size, command data buffer "
		    "size too small, (%d < %d)\n", data_size, size);
		return (-ENOSPC);
	}

	rc = zpios_setup_run(&run_args, kcmd, file);
	if (rc)
		return (rc);

	rc = zpios_threads_run(run_args);
	zpios_remove_objset(run_args);
	if (rc)
		goto cleanup;

	if (stats) {
		n = 1;
		m = 1 + kcmd->cmd_thread_count;
		stats[0] = run_args->stats;

		for (i = 0; i < kcmd->cmd_thread_count; i++)
			stats[n+i] = run_args->threads[i]->stats;

		for (i = 0; i < kcmd->cmd_region_count; i++)
			stats[m+i] = run_args->regions[i].stats;
	}

cleanup:
	zpios_cleanup_run(run_args);

	(void) zpios_upcall(kcmd->cmd_post, PHASE_POST_RUN, run_args, 0);

	return (rc);
}

static int
zpios_open(struct inode *inode, struct file *file)
{
	zpios_info_t *info;

	info = (zpios_info_t *)kmem_alloc(sizeof (*info), KM_SLEEP);

	spin_lock_init(&info->info_lock);
	info->info_size = ZPIOS_INFO_BUFFER_SIZE;
	info->info_buffer =
	    (char *)vmem_alloc(ZPIOS_INFO_BUFFER_SIZE, KM_SLEEP);

	info->info_head = info->info_buffer;
	file->private_data = (void *)info;

	return (0);
}

static int
zpios_release(struct inode *inode, struct file *file)
{
	zpios_info_t *info = (zpios_info_t *)file->private_data;

	ASSERT(info);
	ASSERT(info->info_buffer);

	vmem_free(info->info_buffer, ZPIOS_INFO_BUFFER_SIZE);
	kmem_free(info, sizeof (*info));

	return (0);
}

static int
zpios_buffer_clear(struct file *file, zpios_cfg_t *kcfg, unsigned long arg)
{
	zpios_info_t *info = (zpios_info_t *)file->private_data;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);
	memset(info->info_buffer, 0, info->info_size);
	info->info_head = info->info_buffer;
	spin_unlock(&info->info_lock);

	return (0);
}

static int
zpios_buffer_size(struct file *file, zpios_cfg_t *kcfg, unsigned long arg)
{
	zpios_info_t *info = (zpios_info_t *)file->private_data;
	char *buf;
	int min, size, rc = 0;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);
	if (kcfg->cfg_arg1 > 0) {

		size = kcfg->cfg_arg1;
		buf = (char *)vmem_alloc(size, KM_SLEEP);

		/* Zero fill and truncate contents when coping buffer */
		min = ((size < info->info_size) ? size : info->info_size);
		memset(buf, 0, size);
		memcpy(buf, info->info_buffer, min);
		vmem_free(info->info_buffer, info->info_size);
		info->info_size = size;
		info->info_buffer = buf;
		info->info_head = info->info_buffer;
	}

	kcfg->cfg_rc1 = info->info_size;

	if (copy_to_user((struct zpios_cfg_t __user *)arg,
	    kcfg, sizeof (*kcfg)))
		rc = -EFAULT;

	spin_unlock(&info->info_lock);

	return (rc);
}

static int
zpios_ioctl_cfg(struct file *file, unsigned long arg)
{
	zpios_cfg_t kcfg;
	int rc = 0;

	if (copy_from_user(&kcfg, (zpios_cfg_t *)arg, sizeof (kcfg)))
		return (-EFAULT);

	if (kcfg.cfg_magic != ZPIOS_CFG_MAGIC) {
		zpios_print(file, "Bad config magic 0x%x != 0x%x\n",
		    kcfg.cfg_magic, ZPIOS_CFG_MAGIC);
		return (-EINVAL);
	}

	switch (kcfg.cfg_cmd) {
		case ZPIOS_CFG_BUFFER_CLEAR:
			/*
			 * cfg_arg1 - Unused
			 * cfg_rc1  - Unused
			 */
			rc = zpios_buffer_clear(file, &kcfg, arg);
			break;
		case ZPIOS_CFG_BUFFER_SIZE:
			/*
			 * cfg_arg1 - 0 - query size; >0 resize
			 * cfg_rc1  - Set to current buffer size
			 */
			rc = zpios_buffer_size(file, &kcfg, arg);
			break;
		default:
			zpios_print(file, "Bad config command %d\n",
			    kcfg.cfg_cmd);
			rc = -EINVAL;
			break;
	}

	return (rc);
}

static int
zpios_ioctl_cmd(struct file *file, unsigned long arg)
{
	zpios_cmd_t *kcmd;
	void *data = NULL;
	int rc = -EINVAL;

	kcmd = kmem_alloc(sizeof (zpios_cmd_t), KM_SLEEP);

	rc = copy_from_user(kcmd, (zpios_cfg_t *)arg, sizeof (zpios_cmd_t));
	if (rc) {
		zpios_print(file, "Unable to copy command structure "
		    "from user to kernel memory, %d\n", rc);
		goto out_cmd;
	}

	if (kcmd->cmd_magic != ZPIOS_CMD_MAGIC) {
		zpios_print(file, "Bad command magic 0x%x != 0x%x\n",
		    kcmd->cmd_magic, ZPIOS_CFG_MAGIC);
		rc = (-EINVAL);
		goto out_cmd;
	}

	/* Allocate memory for any opaque data the caller needed to pass on */
	if (kcmd->cmd_data_size > 0) {
		data = (void *)vmem_alloc(kcmd->cmd_data_size, KM_SLEEP);

		rc = copy_from_user(data, (void *)(arg + offsetof(zpios_cmd_t,
		    cmd_data_str)), kcmd->cmd_data_size);
		if (rc) {
			zpios_print(file, "Unable to copy data buffer "
			    "from user to kernel memory, %d\n", rc);
			goto out_data;
		}
	}

	rc = zpios_do_one_run(file, kcmd, kcmd->cmd_data_size, data);

	if (data != NULL) {
		/* If the test failed do not print out the stats */
		if (rc)
			goto out_data;

		rc = copy_to_user((void *)(arg + offsetof(zpios_cmd_t,
		    cmd_data_str)), data, kcmd->cmd_data_size);
		if (rc) {
			zpios_print(file, "Unable to copy data buffer "
			    "from kernel to user memory, %d\n", rc);
			rc = -EFAULT;
		}

out_data:
		vmem_free(data, kcmd->cmd_data_size);
	}
out_cmd:
	kmem_free(kcmd, sizeof (zpios_cmd_t));

	return (rc);
}

static long
zpios_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int rc = 0;

	/* Ignore tty ioctls */
	if ((cmd & 0xffffff00) == ((int)'T') << 8)
		return (-ENOTTY);

	switch (cmd) {
		case ZPIOS_CFG:
			rc = zpios_ioctl_cfg(file, arg);
			break;
		case ZPIOS_CMD:
			rc = zpios_ioctl_cmd(file, arg);
			break;
		default:
			zpios_print(file, "Bad ioctl command %d\n", cmd);
			rc = -EINVAL;
			break;
	}

	return (rc);
}

#ifdef CONFIG_COMPAT
/* Compatibility handler for ioctls from 32-bit ELF binaries */
static long
zpios_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return (zpios_unlocked_ioctl(file, cmd, arg));
}
#endif /* CONFIG_COMPAT */

/*
 * I'm not sure why you would want to write in to this buffer from
 * user space since its principle use is to pass test status info
 * back to the user space, but I don't see any reason to prevent it.
 */
static ssize_t
zpios_write(struct file *file, const char __user *buf,
    size_t count, loff_t *ppos)
{
	zpios_info_t *info = (zpios_info_t *)file->private_data;
	int rc = 0;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);

	/* Write beyond EOF */
	if (*ppos >= info->info_size) {
		rc = -EFBIG;
		goto out;
	}

	/* Resize count if beyond EOF */
	if (*ppos + count > info->info_size)
		count = info->info_size - *ppos;

	if (copy_from_user(info->info_buffer, buf, count)) {
		rc = -EFAULT;
		goto out;
	}

	*ppos += count;
	rc = count;
out:
	spin_unlock(&info->info_lock);
	return (rc);
}

static ssize_t
zpios_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	zpios_info_t *info = (zpios_info_t *)file->private_data;
	int rc = 0;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);

	/* Read beyond EOF */
	if (*ppos >= info->info_size)
		goto out;

	/* Resize count if beyond EOF */
	if (*ppos + count > info->info_size)
		count = info->info_size - *ppos;

	if (copy_to_user(buf, info->info_buffer + *ppos, count)) {
		rc = -EFAULT;
		goto out;
	}

	*ppos += count;
	rc = count;
out:
	spin_unlock(&info->info_lock);
	return (rc);
}

static loff_t zpios_seek(struct file *file, loff_t offset, int origin)
{
	zpios_info_t *info = (zpios_info_t *)file->private_data;
	int rc = -EINVAL;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);

	switch (origin) {
	case 0: /* SEEK_SET - No-op just do it */
		break;
	case 1: /* SEEK_CUR - Seek from current */
		offset = file->f_pos + offset;
		break;
	case 2: /* SEEK_END - Seek from end */
		offset = info->info_size + offset;
		break;
	}

	if (offset >= 0) {
		file->f_pos = offset;
		file->f_version = 0;
		rc = offset;
	}

	spin_unlock(&info->info_lock);

	return (rc);
}

static struct file_operations zpios_fops = {
	.owner		= THIS_MODULE,
	.open		= zpios_open,
	.release	= zpios_release,
	.unlocked_ioctl	= zpios_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= zpios_compat_ioctl,
#endif
	.read		= zpios_read,
	.write		= zpios_write,
	.llseek		= zpios_seek,
};

static struct miscdevice zpios_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= ZPIOS_NAME,
	.fops		= &zpios_fops,
};

#ifdef DEBUG
#define	ZFS_DEBUG_STR   " (DEBUG mode)"
#else
#define	ZFS_DEBUG_STR   ""
#endif

static int __init
zpios_init(void)
{
	int error;

	error = misc_register(&zpios_misc);
	if (error) {
		printk(KERN_INFO "ZPIOS: misc_register() failed %d\n", error);
	} else {
		printk(KERN_INFO "ZPIOS: Loaded module v%s-%s%s\n",
		    ZFS_META_VERSION, ZFS_META_RELEASE, ZFS_DEBUG_STR);
	}

	return (error);
}

static void __exit
zpios_fini(void)
{
	misc_deregister(&zpios_misc);

	printk(KERN_INFO "ZPIOS: Unloaded module v%s-%s%s\n",
	    ZFS_META_VERSION, ZFS_META_RELEASE, ZFS_DEBUG_STR);
}

module_init(zpios_init);
module_exit(zpios_fini);

MODULE_AUTHOR("LLNL / Sun");
MODULE_DESCRIPTION("Kernel PIOS implementation");
MODULE_LICENSE("GPL");
MODULE_VERSION(ZFS_META_VERSION "-" ZFS_META_RELEASE);
