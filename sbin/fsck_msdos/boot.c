/*	$OpenBSD: boot.c,v 1.24 2016/10/10 00:34:50 bluhm Exp $	*/
/*	$NetBSD: boot.c,v 1.5 1997/10/17 11:19:23 ws Exp $	*/

/*
 * Copyright (C) 1995, 1997 Wolfgang Solfrank
 * Copyright (c) 1995 Martin Husemann
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>	/* DEV_BSIZE powerof2 */
#include <sys/disklabel.h>

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>

#include "ext.h"

int
readboot(int dosfs, struct bootblock *boot)
{
	u_char *block = NULL;
	u_char *fsinfo = NULL;
	u_char *backup = NULL;
	int ret = FSOK, secsize = lab.d_secsize, fsinfosz;
	off_t o;
	ssize_t n;

	if (secsize < DOSBOOTBLOCKSIZE) {
		xperror("sector size < DOSBOOTBLOCKSIZE");
		goto fail;
	}
	if (DOSBOOTBLOCKSIZE != DEV_BSIZE) {
		xperror("DOSBOOTBLOCKSIZE != DEV_BSIZE");
		goto fail;
	}

	block = malloc(secsize);
	if (block == NULL) {
		xperror("could not malloc boot block");
		goto fail;
	}

	if ((o = lseek(dosfs, 0, SEEK_SET)) == -1) {
		xperror("could not seek boot block");
		goto fail;
	}

	n = read(dosfs, block, secsize);
	if (n == -1 || n != secsize) {
		xperror("could not read boot block");
		goto fail;
	}

	if (block[510] != 0x55 || block[511] != 0xaa) {
		pfatal("Invalid signature in boot block: %02x%02x\n",
		    block[511], block[510]);
	}

	memset(boot, 0, sizeof *boot);
	boot->ValidFat = -1;

	/* decode bios parameter block */
	boot->BytesPerSec = block[11] + (block[12] << 8);
	if (boot->BytesPerSec == 0 || boot->BytesPerSec != secsize) {
		pfatal("Invalid sector size: %u\n", boot->BytesPerSec);
		goto fail;
	}
	boot->SecPerClust = block[13];
	if (boot->SecPerClust == 0 || !powerof2(boot->SecPerClust)) {
		pfatal("Invalid cluster size: %u\n", boot->SecPerClust);
		goto fail;
	}
	boot->ResSectors = block[14] + (block[15] << 8);
	boot->FATs = block[16];
	if (boot->FATs == 0) {
		pfatal("Invalid number of FATs: %u\n", boot->FATs);
		goto fail;
	}
	boot->RootDirEnts = block[17] + (block[18] << 8);
	boot->Sectors = block[19] + (block[20] << 8);
	boot->Media = block[21];
	boot->FATsmall = block[22] + (block[23] << 8);
	boot->SecPerTrack = block[24] + (block[25] << 8);
	boot->Heads = block[26] + (block[27] << 8);
	boot->HiddenSecs = block[28] + (block[29] << 8) + (block[30] << 16) + (block[31] << 24);
	boot->HugeSectors = block[32] + (block[33] << 8) + (block[34] << 16) + (block[35] << 24);

	boot->FATsecs = boot->FATsmall;

	if (!boot->RootDirEnts) {
		boot->flags |= FAT32;
		boot->FATsecs = block[36] + (block[37] << 8)
				+ (block[38] << 16) + (block[39] << 24);
		if (block[40] & 0x80)
			boot->ValidFat = block[40] & 0x0f;

		/* check version number: */
		if (block[42] || block[43]) {
			/* Correct?				XXX */
			pfatal("Unknown filesystem version: %x.%x\n",
			       block[43], block[42]);
			goto fail;
		}
		boot->RootCl = block[44] + (block[45] << 8)
			       + (block[46] << 16) + (block[47] << 24);
		boot->FSInfo = block[48] + (block[49] << 8);
		boot->Backup = block[50] + (block[51] << 8);

		o = lseek(dosfs, boot->FSInfo * secsize, SEEK_SET);
		if (o == -1 || o != boot->FSInfo * secsize) {
			xperror("could not seek fsinfo block");
			goto fail;
		}

		if ((2 * DOSBOOTBLOCKSIZE) < secsize)
			fsinfosz = secsize;
		else
			fsinfosz = 2 * secsize;
		fsinfo = malloc(fsinfosz);
		if (fsinfo == NULL) {
			xperror("could not malloc fsinfo");
			goto fail;
		}
		n = read(dosfs, fsinfo, fsinfosz);
		if (n == -1 || n != fsinfosz) {
			xperror("could not read fsinfo block");
			goto fail;
		}

		if (memcmp(fsinfo, "RRaA", 4)
		    || memcmp(fsinfo + 0x1e4, "rrAa", 4)
		    || fsinfo[0x1fc]
		    || fsinfo[0x1fd]
		    || fsinfo[0x1fe] != 0x55
		    || fsinfo[0x1ff] != 0xaa
		    || fsinfo[0x3fc]
		    || fsinfo[0x3fd]
		    || fsinfo[0x3fe] != 0x55
		    || fsinfo[0x3ff] != 0xaa) {
			pwarn("Invalid signature in fsinfo block\n");
			if (ask(0, "fix")) {
				memcpy(fsinfo, "RRaA", 4);
				memcpy(fsinfo + 0x1e4, "rrAa", 4);
				fsinfo[0x1fc] = fsinfo[0x1fd] = 0;
				fsinfo[0x1fe] = 0x55;
				fsinfo[0x1ff] = 0xaa;
				fsinfo[0x3fc] = fsinfo[0x3fd] = 0;
				fsinfo[0x3fe] = 0x55;
				fsinfo[0x3ff] = 0xaa;

				o = lseek(dosfs, boot->FSInfo * secsize,
				    SEEK_SET);
				if (o == -1 || o != boot->FSInfo * secsize) {
					xperror("Unable to seek FSInfo");
					goto fail;
				}
				n = write(dosfs, fsinfo, fsinfosz);
				if (n == -1 || n != fsinfosz) {
					xperror("Unable to write FSInfo");
					goto fail;
				}
				ret = FSBOOTMOD;
			} else
				boot->FSInfo = 0;
		}
		if (boot->FSInfo) {
			boot->FSFree = fsinfo[0x1e8] + (fsinfo[0x1e9] << 8)
				       + (fsinfo[0x1ea] << 16)
				       + (fsinfo[0x1eb] << 24);
			boot->FSNext = fsinfo[0x1ec] + (fsinfo[0x1ed] << 8)
				       + (fsinfo[0x1ee] << 16)
				       + (fsinfo[0x1ef] << 24);
		}

		o = lseek(dosfs, boot->Backup * secsize, SEEK_SET);
		if (o == -1 || o != boot->Backup * secsize) {
			xperror("could not seek backup bootblock");
			goto fail;
		}
		backup = malloc(2 * secsize); /* In case we check fsinfo. */
		if (backup == NULL) {
			xperror("could not malloc backup boot block");
			goto fail;
		}
		n = read(dosfs, backup, secsize);
		if (n == -1 || n != secsize) {
			xperror("could not read backup bootblock");
			goto fail;
		}

		/*
		 * Check that the backup boot block matches the primary one.
		 * We don't check every byte, since some vendor utilities
		 * seem to overwrite the boot code when they feel like it,
		 * without changing the backup block.  Specifically, we check
		 * the two-byte signature at the end, the BIOS parameter
		 * block (which starts after the 3-byte JMP and the 8-byte
		 * OEM name/version) and the filesystem information that
		 * follows the BPB (bsBPB[53] and bsExt[26] for FAT32, so we
		 * check 79 bytes).
		 */
		if (backup[510] != 0x55 || backup[511] != 0xaa) {
			pfatal("Invalid signature in backup boot block: %02x%02x\n", backup[511], backup[510]);
		}
		if (memcmp(block + 11, backup + 11, 79)) {
			pfatal("backup doesn't compare to primary bootblock\n");
			goto fail;
		}
		/* Check backup FSInfo?					XXX */
	}

	if (boot->FATsecs == 0) {
		pfatal("Invalid number of FAT sectors: %u\n", boot->FATsecs);
		goto fail;
	}

	boot->ClusterOffset = (boot->RootDirEnts * 32 + secsize - 1)
	    / secsize
	    + boot->ResSectors
	    + boot->FATs * boot->FATsecs
	    - CLUST_FIRST * boot->SecPerClust;

	if (boot->Sectors) {
		boot->HugeSectors = 0;
		boot->NumSectors = boot->Sectors;
	} else
		boot->NumSectors = boot->HugeSectors;

	if (boot->ClusterOffset > boot->NumSectors) {
		pfatal("Cluster offset too large (%u clusters)\n",
		    boot->ClusterOffset);
		goto fail;
	}
	boot->NumClusters = (boot->NumSectors - boot->ClusterOffset) / boot->SecPerClust;

	if (boot->flags&FAT32)
		boot->ClustMask = CLUST32_MASK;
	else if (boot->NumClusters < (CLUST_RSRVD&CLUST12_MASK))
		boot->ClustMask = CLUST12_MASK;
	else if (boot->NumClusters < (CLUST_RSRVD&CLUST16_MASK))
		boot->ClustMask = CLUST16_MASK;
	else {
		pfatal("Filesystem too big (%u clusters) for non-FAT32 partition\n",
		       boot->NumClusters);
		goto fail;
	}

	switch (boot->ClustMask) {
	case CLUST32_MASK:
		boot->NumFatEntries = (boot->FATsecs * secsize) / 4;
		break;
	case CLUST16_MASK:
		boot->NumFatEntries = (boot->FATsecs * secsize) / 2;
		break;
	default:
		boot->NumFatEntries = (boot->FATsecs * secsize * 2) / 3;
		break;
	}

	if (boot->NumFatEntries < boot->NumClusters) {
		pfatal("FAT size too small, %u entries won't fit into %u sectors\n",
		       boot->NumClusters, boot->FATsecs);
		goto fail;
	}
	boot->ClusterSize = boot->SecPerClust * secsize;

	boot->NumFiles = 1;
	boot->NumFree = 0;

	free(backup);
	free(block);
	free(fsinfo);
	return ret;
fail:
	free(backup);
	free(block);
	free(fsinfo);
	return FSFATAL;
}

int
writefsinfo(int dosfs, struct bootblock *boot)
{
	u_char *fsinfo = NULL;
	int secsize = lab.d_secsize, fsinfosz;
	off_t o;
	ssize_t n;

	if ((2 * DOSBOOTBLOCKSIZE) < secsize)
		fsinfosz = secsize;
	else
		fsinfosz = 2 * secsize;

	fsinfo = malloc(fsinfosz);
	if (fsinfo == NULL) {
		xperror("could not malloc fsinfo block");
		goto fail;
	}

	o = lseek(dosfs, boot->FSInfo * secsize, SEEK_SET);
	if (o == -1 || o != boot->FSInfo * secsize) {
		xperror("could not seek fsinfo block");
		goto fail;
	}

	n = read(dosfs, fsinfo, fsinfosz);
	if (n == -1 || n != fsinfosz) {
		xperror("could not read fsinfo block");
		goto fail;
	}

	fsinfo[0x1e8] = (u_char)boot->FSFree;
	fsinfo[0x1e9] = (u_char)(boot->FSFree >> 8);
	fsinfo[0x1ea] = (u_char)(boot->FSFree >> 16);
	fsinfo[0x1eb] = (u_char)(boot->FSFree >> 24);
	fsinfo[0x1ec] = (u_char)boot->FSNext;
	fsinfo[0x1ed] = (u_char)(boot->FSNext >> 8);
	fsinfo[0x1ee] = (u_char)(boot->FSNext >> 16);
	fsinfo[0x1ef] = (u_char)(boot->FSNext >> 24);

	o = lseek(dosfs, o, SEEK_SET);
	if (o == -1 || o != boot->FSInfo * boot->BytesPerSec) {
		xperror("Unable to seek FSInfo");
		goto fail;
	}
	n = write(dosfs, fsinfo, fsinfosz);
	if (n == -1 || n != fsinfosz) {
		xperror("Unable to write FSInfo");
		goto fail;
	}

	free(fsinfo);

	/*
	 * Technically, we should return FSBOOTMOD here.
	 *
	 * However, since Win95 OSR2 (the first M$ OS that has
	 * support for FAT32) doesn't maintain the FSINFO block
	 * correctly, it has to be fixed pretty often.
	 *
	 * Therefore, we handle the FSINFO block only informally,
	 * fixing it if necessary, but otherwise ignoring the
	 * fact that it was incorrect.
	 */
	return 0;
fail:
	free(fsinfo);
	return FSFATAL;
}
