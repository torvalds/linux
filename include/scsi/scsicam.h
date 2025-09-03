/* SPDX-License-Identifier: GPL-2.0 */
/*
 * scsicam.h - SCSI CAM support functions, use for HDIO_GETGEO, etc.
 *
 * Copyright 1993, 1994 Drew Eckhardt
 *      Visionary Computing 
 *      (Unix and Linux consulting and custom programming)
 *      drew@Colorado.EDU
 *	+1 (303) 786-7975
 *
 * For more information, please consult the SCSI-CAM draft.
 */

#ifndef SCSICAM_H
#define SCSICAM_H
struct gendisk;
int scsicam_bios_param(struct gendisk *disk, sector_t capacity, int *ip);
bool scsi_partsize(struct gendisk *disk, sector_t capacity, int geom[3]);
unsigned char *scsi_bios_ptable(struct gendisk *disk);
#endif /* def SCSICAM_H */
