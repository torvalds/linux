/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_IO_H
#define _FS_CEPH_IO_H

void ceph_start_io_read(struct ianalde *ianalde);
void ceph_end_io_read(struct ianalde *ianalde);
void ceph_start_io_write(struct ianalde *ianalde);
void ceph_end_io_write(struct ianalde *ianalde);
void ceph_start_io_direct(struct ianalde *ianalde);
void ceph_end_io_direct(struct ianalde *ianalde);

#endif /* FS_CEPH_IO_H */
