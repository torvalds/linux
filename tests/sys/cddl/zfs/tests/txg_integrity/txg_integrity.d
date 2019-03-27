#!/usr/sbin/dtrace -s

/* This D script attempts to verify whether a single dbuf is being held by
 * multiple transaction groups at the time it is fixed up.  The purpose is
 * to verify whether the txg_integrity test exercises the kernel code paths
 * that we want it to.
 * XXX this is a work in progress.  It is not yet usable
 *
 * $FreeBSD$
 */

/* zfs:kernel:dbuf_fix_old_data:entry
{
	printf("entry");
}*/

dtrace:::BEGIN
{
   dmu_write_uio_dnode_size = 0;
   dmu_write_uio_dnode_loffset = 0;
   dmu_write_uio_dnode_numbufs = 0;
   dbuf_dirty_size = 0;
   dbuf_dirty_offset = 0;
   uio_stats2_refcount = 0,
   uio_stats2_dirtycnt = 0,
   dbuf_dirty_entry_refcount = 0;
   dbuf_dirty_entry_dirtycnt = 0;
   dbuf_dirty_refcount = 0;
   dbuf_dirty_dirtycnt = 0;
   ge2TrackerName[0] = "";
   ge2TrackerCount[0] = 0;
}

zfs:kernel:dbuf_fix_old_data:db_get_spa
{
	/*printf("db_get_spa");*/
	/* stack(); */
	printf("uio_stats: size=%d\tloffset=%x\tnumbufs=%d\n\trefcount(holds)=%d\tdirtycnt=%d\n", 
      dmu_write_uio_dnode_size ,
      dmu_write_uio_dnode_loffset,
      dmu_write_uio_dnode_numbufs ,
      uio_stats2_refcount,
      uio_stats2_dirtycnt);
   printf("dbuf_dirty stats: size=%d\toffset=%x\n\tentry: refcount(holds)=%d\tdirtycnt=%d\n\texit: refcount(holds)=%d\tdirtycnt=%d\n", 
         dbuf_dirty_size,
         dbuf_dirty_offset,
         dbuf_dirty_entry_refcount,
         dbuf_dirty_entry_dirtycnt,
         dbuf_dirty_refcount,
         dbuf_dirty_dirtycnt);
   printf("DR_TXG=%d\tDB address = %x\n", args[0], (long long)args[1]);
   printf("ge2Tracker entry: name=%s\tcount=%d", ge2TrackerName[(long long)args[1]], ge2TrackerCount[(long long)args[1]]);
   exit(0);
}

zfs:kernel:dbuf_dirty:entry
{
   dbuf_dirty_size = args[0];
   dbuf_dirty_offset = args[1];
   dbuf_dirty_entry_refcount = args[2];
   dbuf_dirty_entry_dirtycnt = args[3];
}
      

zfs2:kernel:dmu_write_uio_dnode:uio_stats
{
   dmu_write_uio_dnode_size = args[0];
   dmu_write_uio_dnode_loffset = args[1];
   dmu_write_uio_dnode_numbufs = args[2];
   printf("uio_stats: size=%d\tloffset=%x\tnumbufs=%d", args[0], args[1], args[2]);
}

zfs2:kernel:dmu_write_uio_dnode:uio_stats_two
{
   uio_stats2_refcount = args[0];
   uio_stats2_dirtycnt = args[1];
   printf("uio_stats2: refcount=%d\tdirtycnt=%d\tdb=%x", args[0], args[1], (long long)args[2]);
}

zfs2:kernel:dmu_write_uio_dnode:uio_stats_three
{
   printf("uio_stats3: io_txg=%d", args[0]);
}

zfs:kernel:dbuf_dirty:no_db_nofill
{
   dbuf_dirty_refcount = args[0];
   dbuf_dirty_dirtycnt = args[1];
}

/* refcount:kernel::ge2
{
   stack();
} */

zfs:kernel:dbuf_hold_impl:ge2
{
   ge2TrackerName[(long long)args[0]] = "dbuf_hold_impl";
   ge2TrackerCount[(long long)args[0]] = args[1];
   printf(">= 2: db=%x\trc=%d", (long long)args[0], args[1]);
   /* stack(); */
   /* ustack(10); */
}

/* zfs:kernel:dbuf_add_ref:ge2
{
   ge2TrackerName[(long long)args[0]] = "dbuf_add_ref";
   ge2TrackerCount[(long long)args[0]] = args[1];
} */

/* zfs2:kernel:dmu_bonus_hold:ge2
{
   ge2TrackerName[(long long)args[0]] = "dmu_bonus_hold";
   ge2TrackerCount[(long long)args[0]] = args[1];
}*/


