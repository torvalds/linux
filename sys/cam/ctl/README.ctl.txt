/* $FreeBSD$ */

CTL - CAM Target Layer Description

Revision 1.4 (December 29th, 2011)
Ken Merry <ken@FreeBSD.org>

Table of Contents:
=================

Introduction
Features
Configuring and Running CTL
Revision 1.N Changes
To Do List
Code Roadmap
Userland Commands

Introduction:
============

CTL is a disk, processor and cdrom device emulation subsystem originally
written for Copan Systems under Linux starting in 2003.  It has been
shipping in Copan (now SGI) products since 2005.

It was ported to FreeBSD in 2008, and thanks to an agreement between SGI
(who acquired Copan's assets in 2010) and Spectra Logic in 2010, CTL is
available under a BSD-style license.  The intent behind the agreement was
that Spectra would work to get CTL into the FreeBSD tree.

Features:
========

 - Disk, processor and cdrom device emulation.
 - Tagged queueing
 - SCSI task attribute support (ordered, head of queue, simple tags)
 - SCSI implicit command ordering support.  (e.g. if a read follows a mode
   select, the read will be blocked until the mode select completes.)
 - Full task management support (abort, LUN reset, target reset, etc.)
 - Support for multiple ports
 - Support for multiple simultaneous initiators
 - Support for multiple simultaneous backing stores
 - Support for VMWare VAAI: COMPARE AND WRITE, XCOPY, WRITE SAME and
   UNMAP commands
 - Support for Microsoft ODX: POPULATE TOKEN/WRITE USING TOKEN, WRITE SAME
   and UNMAP commands
 - Persistent reservation support
 - Mode sense/select support
 - Error injection support
 - High Availability clustering support with ALUA
 - All I/O handled in-kernel, no userland context switch overhead.

Configuring and Running CTL:
===========================

 - Add 'device ctl' to your kernel configuration file or load the module.

 - If you're running with a 8Gb or 4Gb Qlogic FC board, add
   'options ISP_TARGET_MODE' to your kernel config file. 'device ispfw' or
   loading the ispfw module is also recommended.

 - Rebuild and install a new kernel.

 - Reboot with the new kernel.

 - To add a LUN with the RAM disk backend:

	ctladm create -b ramdisk -s 10485760000000000000
	ctladm port -o on

 - You should now see the CTL disk LUN through camcontrol devlist:

scbus6 on ctl2cam0 bus 0:
<FREEBSD CTLDISK 0001>             at scbus6 target 1 lun 0 (da24,pass32)
<>                                 at scbus6 target -1 lun -1 ()

   This is visible through the CTL CAM SIM.  This allows using CTL without
   any physical hardware.  You should be able to issue any normal SCSI
   commands to the device via the pass(4)/da(4) devices.

   If any target-capable HBAs are in the system (e.g. isp(4)), and have
   target mode enabled, you should now also be able to see the CTL LUNs via
   that target interface.

   Note that all CTL LUNs are presented to all frontends.  There is no
   LUN masking, or separate, per-port configuration.

 - Note that the ramdisk backend is a "fake" ramdisk.  That is, it is
   backed by a small amount of RAM that is used for all I/O requests.  This
   is useful for performance testing, but not for any data integrity tests.

 - To add a LUN with the block/file backend:

	truncate -s +1T myfile
	ctladm create -b block -o file=myfile
	ctladm port -o on

 - You can also see a list of LUNs and their backends like this:

# ctladm devlist
LUN Backend       Size (Blocks)   BS Serial Number    Device ID       
  0 block            2147483648  512 MYSERIAL   0     MYDEVID   0     
  1 block            2147483648  512 MYSERIAL   1     MYDEVID   1     
  2 block            2147483648  512 MYSERIAL   2     MYDEVID   2     
  3 block            2147483648  512 MYSERIAL   3     MYDEVID   3     
  4 block            2147483648  512 MYSERIAL   4     MYDEVID   4     
  5 block            2147483648  512 MYSERIAL   5     MYDEVID   5     
  6 block            2147483648  512 MYSERIAL   6     MYDEVID   6     
  7 block            2147483648  512 MYSERIAL   7     MYDEVID   7     
  8 block            2147483648  512 MYSERIAL   8     MYDEVID   8     
  9 block            2147483648  512 MYSERIAL   9     MYDEVID   9     
 10 block            2147483648  512 MYSERIAL  10     MYDEVID  10     
 11 block            2147483648  512 MYSERIAL  11     MYDEVID  11    

 - You can see the LUN type and backing store for block/file backend LUNs
   like this:

# ctladm devlist -v
LUN Backend       Size (Blocks)   BS Serial Number    Device ID       
  0 block            2147483648  512 MYSERIAL   0     MYDEVID   0     
      lun_type=0
      num_threads=14
      file=testdisk0
  1 block            2147483648  512 MYSERIAL   1     MYDEVID   1     
      lun_type=0
      num_threads=14
      file=testdisk1
  2 block            2147483648  512 MYSERIAL   2     MYDEVID   2     
      lun_type=0
      num_threads=14
      file=testdisk2
  3 block            2147483648  512 MYSERIAL   3     MYDEVID   3     
      lun_type=0
      num_threads=14
      file=testdisk3
  4 block            2147483648  512 MYSERIAL   4     MYDEVID   4     
      lun_type=0
      num_threads=14
      file=testdisk4
  5 block            2147483648  512 MYSERIAL   5     MYDEVID   5     
      lun_type=0
      num_threads=14
      file=testdisk5
  6 block            2147483648  512 MYSERIAL   6     MYDEVID   6     
      lun_type=0
      num_threads=14
      file=testdisk6
  7 block            2147483648  512 MYSERIAL   7     MYDEVID   7     
      lun_type=0
      num_threads=14
      file=testdisk7
  8 block            2147483648  512 MYSERIAL   8     MYDEVID   8     
      lun_type=0
      num_threads=14
      file=testdisk8
  9 block            2147483648  512 MYSERIAL   9     MYDEVID   9     
      lun_type=0
      num_threads=14
      file=testdisk9
 10 ramdisk                   0    0 MYSERIAL   0     MYDEVID   0     
      lun_type=3
 11 ramdisk     204800000000000  512 MYSERIAL   1     MYDEVID   1     
      lun_type=0


Revision 1.4 Changes
====================
 - Added in the second HA mode (where CTL does the data transfers instead
   of having data transfers done below CTL), and abstracted out the Copan
   HA API.

 - Fixed the phantom device problem in the CTL CAM SIM and improved the
   CAM SIM to automatically trigger a rescan when the port is enabled and
   disabled.
 
 - Made the number of threads in the block backend configurable via sysctl,
   loader tunable and the ctladm command line.  (You can now specify
   -o num_threads=4 when creating a LUN with ctladm create.)

 - Fixed some LUN selection issues in ctlstat(8) and allowed for selection
   of LUN numbers up to 1023.

 - General cleanup.

 - This version intended for public release.

Revision 1.3 Changes
====================
 - Added descriptor sense support to CTL.  It can be enabled through the
   control mode page (10), but is disabled by default.

 - Improved error injection support.  The number of errors that can be
   injected with 'ctladm inject' has been increased, and any arbitrary
   sense data may now be injected as well.

 - The port infrastructure has been revamped.  Individual ports and types
   of ports may now be enabled and disabled from the command line.  ctladm
   now has the ability to set the WWNN and WWPN for each port.

 - The block backend can now send multiple I/Os to backing files.  Multiple
   writes are only allowed for ZFS, but multiple readers are allowed for
   any filesystem.

 - The block and ramdisk backends now support setting the LUN blocksize.
   There are some restrictions when the backing device is a block device,
   but otherwise the blocksize may be set to anything.

Revision 1.2 Changes
====================

 - CTL initialization process has been revamped.  Instead of using an
   ad-hoc method, it is now sequenced through SYSINIT() calls.

 - A block/file backend has been added.  This allows using arbitrary files
   or block devices as a backing store.

 - The userland LUN configuration interface has been completely rewritten.
   Configuration is now done out of band.

 - The ctladm(8) command line interface has been revamped, and is now
   similar to camcontrol(8).

To Do List:
==========

 - Use devstat(9) for CTL's statistics collection.  CTL uses a home-grown
   statistics collection system that is similar to devstat(9).  ctlstat
   should be retired in favor of iostat, etc., once aggregation modes are
   available in iostat to match the behavior of ctlstat -t and dump modes
   are available to match the behavior of ctlstat -d/ctlstat -J.

 - ZFS ARC backend for CTL.  Since ZFS copies all I/O into the ARC
   (Adaptive Replacement Cache), running the block/file backend on top of a
   ZFS-backed zdev or file will involve an extra set of copies.  The
   optimal solution for backing targets served by CTL with ZFS would be to
   allocate buffers out of the ARC directly, and DMA to/from them directly.
   That would eliminate an extra data buffer allocation and copy.

 - Switch CTL over to using CAM CCBs instead of its own union ctl_io.  This
   will likely require a significant amount of work, but will eliminate
   another data structure in the stack, more memory allocations, etc.  This
   will also require changes to the CAM CCB structure to support CTL.

Code Roadmap:
============

CTL has the concept of pluggable frontend ports and backends.  All
frontends and backends can be active at the same time.  You can have a
ramdisk-backed LUN present along side a file backed LUN.

ctl.c:
-----

This is the core of CTL, where all of the command handlers and a lot of
other things live.  Yes, it is large.  It started off small and grew to its
current size over time.  Perhaps it can be split into more files at some
point.

Here is a roadmap of some of the primary functions in ctl.c.  Starting here
and following the various leaf functions will show the command flow.

ctl_queue() 		This is where commands from the frontend ports come
			in.

ctl_queue_sense()	This is only used for non-packetized SCSI.  i.e.
			parallel SCSI prior to U320 and perhaps U160.

ctl_work_thread() 	This is the primary work thread, and everything gets
			executed from there.

ctl_scsiio_precheck() 	This where all of the initial checks are done, and I/O
			is either queued for execution or blocked.

ctl_scsiio() 		This is where the command handler is actually
			executed.  (See ctl_cmd_table.c for the mapping of
			SCSI opcode to command handler function.)

ctl_done()		This is the routine called (or ctl_done_lock()) to
			initiate the command completion process.

ctl_process_done()	This is where command completion actually happens.

ctl.h:
-----

Basic function declarations and data structures.

ctl_backend.c,
ctl_backend.h:
-------------

These files define the basic CTL backend API.  The comments in the header
explain the API.

ctl_backend_block.c
-------------------

The block and file backend.  This allows for using a disk or a file as the
backing store for a LUN.  Multiple threads are started to do I/O to the
backing device, primarily because the VFS API requires that to get any
concurrency.

ctl_backend_ramdisk.c:
---------------------

A "fake" ramdisk backend.  It only allocates a small amount of memory to
act as a source and sink for reads and writes from an initiator.  Therefore
it cannot be used for any real data, but it can be used to test for
throughput.  It can also be used to test initiators' support for extremely
large LUNs.

ctl_cmd_table.c:
---------------

This is a table with all 256 possible SCSI opcodes, and command handler
functions defined for supported opcodes.  It is included in ctl.c.

ctl_debug.h:
-----------

Simplistic debugging support.

ctl_error.c,
ctl_error.h:
-----------

CTL-specific wrappers around the CAM sense building functions.

ctl_frontend.c,
ctl_frontend.h:
--------------

These files define the basic CTL frontend port API.  The comments in the
header explain the API.

ctl_frontend_cam_sim.c:
----------------------

This is a CTL frontend port that is also a CAM SIM.  The idea is that this
frontend allows for using CTL without any target-capable hardware.  So any
LUNs you create in CTL are visible via this port.

ctl_ha.c:
ctl_ha.h:
--------

This is a High Availability API and TCP-based interlink implementation.

ctl_io.h:
--------

This defines most of the core CTL I/O structures.  union ctl_io is
conceptually very similar to CAM's union ccb.  

ctl_ioctl.h:
-----------

This defines all ioctls available through the CTL character device, and
the data structures needed for those ioctls.

ctl_private.h:
-------------

Private data structres (e.g. CTL softc) and function prototypes.  This also
includes the SCSI vendor and product names used by CTL.

ctl_scsi_all.c
ctl_scsi_all.h:
--------------

CTL wrappers around CAM sense printing functions.

ctl_ser_table.c:
---------------

Command serialization table.  This defines what happens when one type of
command is followed by another type of command.  e.g., what do you do when
you have a mode select followed by a write?  You block the write until the
mode select is complete.  That is defined in this table.

ctl_util.c
ctl_util.h:
----------

CTL utility functions, primarily designed to be used from userland.  See
ctladm for the primary consumer of these functions.  These include CDB
building functions.

scsi_ctl.c:
----------

CAM target peripheral driver and CTL frontend port.  This is the path into
CTL for commands from target-capable hardware/SIMs.

Userland Commands:
=================

ctladm(8) fills a role similar to camcontrol(8).  It allow configuring LUNs,
issuing commands, injecting errors and various other control functions.

ctlstat(8) fills a role similar to iostat(8).  It reports I/O statistics
for CTL.
