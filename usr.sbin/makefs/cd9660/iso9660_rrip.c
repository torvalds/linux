/*	$NetBSD: iso9660_rrip.c,v 1.14 2014/05/30 13:14:47 martin Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2005 Daniel Watt, Walter Deignan, Ryan Gabrys, Alan
 * Perez-Rathke and Ram Vedam.  All rights reserved.
 *
 * This code was written by Daniel Watt, Walter Deignan, Ryan Gabrys,
 * Alan Perez-Rathke and Ram Vedam.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE,DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */
/* This will hold all the function definitions
 * defined in iso9660_rrip.h
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/queue.h>
#include <sys/types.h>
#include <stdio.h>

#include "makefs.h"
#include "cd9660.h"
#include "iso9660_rrip.h"
#include <util.h>

static void cd9660_rrip_initialize_inode(cd9660node *);
static int cd9660_susp_handle_continuation(iso9660_disk *, cd9660node *);
static int cd9660_susp_handle_continuation_common(iso9660_disk *, cd9660node *,
    int);

int
cd9660_susp_initialize(iso9660_disk *diskStructure, cd9660node *node,
    cd9660node *parent, cd9660node *grandparent)
{
	cd9660node *cn;
	int r;

	/* Make sure the node is not NULL. If it is, there are major problems */
	assert(node != NULL);

	if (!(node->type & CD9660_TYPE_DOT) &&
	    !(node->type & CD9660_TYPE_DOTDOT))
		TAILQ_INIT(&(node->head));
	if (node->dot_record != 0)
		TAILQ_INIT(&(node->dot_record->head));
	if (node->dot_dot_record != 0)
		TAILQ_INIT(&(node->dot_dot_record->head));

	 /* SUSP specific entries here */
	if ((r = cd9660_susp_initialize_node(diskStructure, node)) < 0)
		return r;

	/* currently called cd9660node_rrip_init_links */
	r = cd9660_rrip_initialize_node(diskStructure, node, parent, grandparent);
	if (r < 0)
		return r;

	/*
	 * See if we need a CE record, and set all of the
	 * associated counters.
	 *
	 * This should be called after all extensions. After
	 * this is called, no new records should be added.
	 */
	if ((r = cd9660_susp_handle_continuation(diskStructure, node)) < 0)
		return r;

	/* Recurse on children. */
	TAILQ_FOREACH(cn, &node->cn_children, cn_next_child) {
		if ((r = cd9660_susp_initialize(diskStructure, cn, node, parent)) < 0)
			return 0;
	}
	return 1;
}

int
cd9660_susp_finalize(iso9660_disk *diskStructure, cd9660node *node)
{
	cd9660node *temp;
	int r;

	assert(node != NULL);

	if (node == diskStructure->rootNode)
		diskStructure->susp_continuation_area_current_free = 0;

	if ((r = cd9660_susp_finalize_node(diskStructure, node)) < 0)
		return r;
	if ((r = cd9660_rrip_finalize_node(node)) < 0)
		return r;

	TAILQ_FOREACH(temp, &node->cn_children, cn_next_child) {
		if ((r = cd9660_susp_finalize(diskStructure, temp)) < 0)
			return r;
	}
	return 1;
}

/*
 * If we really wanted to speed things up, we could have some sort of
 * lookup table on the SUSP entry type that calls a functor. Or, we could
 * combine the functions. These functions are kept separate to allow
 * easier addition of other extensions.

 * For the sake of simplicity and clarity, we won't be doing that for now.
 */

/*
 * SUSP needs to update the following types:
 * CE (continuation area)
 */
int
cd9660_susp_finalize_node(iso9660_disk *diskStructure, cd9660node *node)
{
	struct ISO_SUSP_ATTRIBUTES *t;

	/* Handle CE counters */
	if (node->susp_entry_ce_length > 0) {
		node->susp_entry_ce_start =
		    diskStructure->susp_continuation_area_current_free;
		diskStructure->susp_continuation_area_current_free +=
		    node->susp_entry_ce_length;
	}

	TAILQ_FOREACH(t, &node->head, rr_ll) {
		if (t->susp_type != SUSP_TYPE_SUSP ||
		    t->entry_type != SUSP_ENTRY_SUSP_CE)
			continue;
		cd9660_bothendian_dword(
			diskStructure->
			  susp_continuation_area_start_sector,
			t->attr.su_entry.CE.ca_sector);

		cd9660_bothendian_dword(
			diskStructure->
			  susp_continuation_area_start_sector,
			t->attr.su_entry.CE.ca_sector);
		cd9660_bothendian_dword(node->susp_entry_ce_start,
			t->attr.su_entry.CE.offset);
		cd9660_bothendian_dword(node->susp_entry_ce_length,
			t->attr.su_entry.CE.length);
	}
	return 0;
}

int
cd9660_rrip_finalize_node(cd9660node *node)
{
	struct ISO_SUSP_ATTRIBUTES *t;

	TAILQ_FOREACH(t, &node->head, rr_ll) {
		if (t->susp_type != SUSP_TYPE_RRIP)
			continue;
		switch (t->entry_type) {
		case SUSP_ENTRY_RRIP_CL:
			/* Look at rr_relocated*/
			if (node->rr_relocated == NULL)
				return -1;
			cd9660_bothendian_dword(
				node->rr_relocated->fileDataSector,
				(unsigned char *)
				    t->attr.rr_entry.CL.dir_loc);
			break;
		case SUSP_ENTRY_RRIP_PL:
			/* Look at rr_real_parent */
			if (node->parent == NULL ||
			    node->parent->rr_real_parent == NULL)
				return -1;
			cd9660_bothendian_dword(
				node->parent->rr_real_parent->fileDataSector,
				(unsigned char *)
				    t->attr.rr_entry.PL.dir_loc);
			break;
		}
	}
	return 0;
}

static int
cd9660_susp_handle_continuation_common(iso9660_disk *diskStructure,
    cd9660node *node, int space)
{
	int ca_used, susp_used, susp_used_pre_ce, working;
	struct ISO_SUSP_ATTRIBUTES *temp, *pre_ce, *last, *CE, *ST;

	pre_ce = last = NULL;
	working = 254 - space;
	if (node->su_tail_size > 0)
		/* Allow 4 bytes for "ST" record. */
		working -= node->su_tail_size + 4;
	/* printf("There are %i bytes to work with\n",working); */

	susp_used_pre_ce = susp_used = 0;
	ca_used = 0;
	TAILQ_FOREACH(temp, &node->head, rr_ll) {
		if (working < 0)
			break;
		/*
		 * printf("SUSP Entry found, length is %i\n",
		 * CD9660_SUSP_ENTRY_SIZE(temp));
		 */
		working -= CD9660_SUSP_ENTRY_SIZE(temp);
		if (working >= 0) {
			last = temp;
			susp_used += CD9660_SUSP_ENTRY_SIZE(temp);
		}
		if (working >= 28) {
			/*
			 * Remember the last entry after which we
			 * could insert a "CE" entry.
			 */
			pre_ce = last;
			susp_used_pre_ce = susp_used;
		}
	}

	/* A CE entry is needed */
	if (working <= 0) {
		CE = cd9660node_susp_create_node(SUSP_TYPE_SUSP,
			SUSP_ENTRY_SUSP_CE, "CE", SUSP_LOC_ENTRY);
		cd9660_susp_ce(CE, node);
		/* This will automatically insert at the appropriate location */
		if (pre_ce != NULL)
			TAILQ_INSERT_AFTER(&node->head, pre_ce, CE, rr_ll);
		else
			TAILQ_INSERT_HEAD(&node->head, CE, rr_ll);
		last = CE;
		susp_used = susp_used_pre_ce + 28;
		/* Count how much CA data is necessary */
		for (temp = TAILQ_NEXT(last, rr_ll); temp != NULL;
		     temp = TAILQ_NEXT(temp, rr_ll)) {
			ca_used += CD9660_SUSP_ENTRY_SIZE(temp);
		}
	}

	/* An ST entry is needed */
	if (node->su_tail_size > 0) {
		ST = cd9660node_susp_create_node(SUSP_TYPE_SUSP,
		    SUSP_ENTRY_SUSP_ST, "ST", SUSP_LOC_ENTRY);
		cd9660_susp_st(ST, node);
		if (last != NULL)
			TAILQ_INSERT_AFTER(&node->head, last, ST, rr_ll);
		else
			TAILQ_INSERT_HEAD(&node->head, ST, rr_ll);
		last = ST;
		susp_used += 4;
	}
	if (last != NULL)
		last->last_in_suf = 1;

	node->susp_entry_size = susp_used;
	node->susp_entry_ce_length = ca_used;

	diskStructure->susp_continuation_area_size += ca_used;
	return 1;
}

/* See if a continuation entry is needed for each of the different types */
static int
cd9660_susp_handle_continuation(iso9660_disk *diskStructure, cd9660node *node)
{
	assert (node != NULL);

	/* Entry */
	if (cd9660_susp_handle_continuation_common(diskStructure,
		node,(int)(node->isoDirRecord->length[0])) < 0)
		return 0;

	return 1;
}

int
cd9660_susp_initialize_node(iso9660_disk *diskStructure, cd9660node *node)
{
	struct ISO_SUSP_ATTRIBUTES *temp;

	/*
	 * Requirements/notes:
	 * CE: is added for us where needed
	 * ST: not sure if it is even required, but if so, should be
	 *     handled by the CE code
	 * PD: isn't needed (though might be added for testing)
	 * SP: is stored ONLY on the . record of the root directory
	 * ES: not sure
	 */

	/* Check for root directory, add SP and ER if needed. */
	if (node->type & CD9660_TYPE_DOT) {
		if (node->parent == diskStructure->rootNode) {
			temp = cd9660node_susp_create_node(SUSP_TYPE_SUSP,
				SUSP_ENTRY_SUSP_SP, "SP", SUSP_LOC_DOT);
			cd9660_susp_sp(temp, node);

			/* Should be first entry. */
			TAILQ_INSERT_HEAD(&node->head, temp, rr_ll);
		}
	}
	return 1;
}

static void
cd9660_rrip_initialize_inode(cd9660node *node)
{
	struct ISO_SUSP_ATTRIBUTES *attr;

	/*
	 * Inode dependent values - this may change,
	 * but for now virtual files and directories do
	 * not have an inode structure
	 */

	if ((node->node != NULL) && (node->node->inode != NULL)) {
		/* PX - POSIX attributes */
		attr = cd9660node_susp_create_node(SUSP_TYPE_RRIP,
			SUSP_ENTRY_RRIP_PX, "PX", SUSP_LOC_ENTRY);
		cd9660node_rrip_px(attr, node->node);

		TAILQ_INSERT_TAIL(&node->head, attr, rr_ll);

		/* TF - timestamp */
		attr = cd9660node_susp_create_node(SUSP_TYPE_RRIP,
			SUSP_ENTRY_RRIP_TF, "TF", SUSP_LOC_ENTRY);
		cd9660node_rrip_tf(attr, node->node);
		TAILQ_INSERT_TAIL(&node->head, attr, rr_ll);

		/* SL - Symbolic link */
		/* ?????????? Dan - why is this here? */
		if (TAILQ_EMPTY(&node->cn_children) &&
		    node->node->inode != NULL &&
		    S_ISLNK(node->node->inode->st.st_mode))
			cd9660_createSL(node);

		/* PN - device number */
		if (node->node->inode != NULL &&
		    ((S_ISCHR(node->node->inode->st.st_mode) ||
		     S_ISBLK(node->node->inode->st.st_mode)))) {
			attr =
			    cd9660node_susp_create_node(SUSP_TYPE_RRIP,
				SUSP_ENTRY_RRIP_PN, "PN",
				SUSP_LOC_ENTRY);
			cd9660node_rrip_pn(attr, node->node);
			TAILQ_INSERT_TAIL(&node->head, attr, rr_ll);
		}
	}
}

int
cd9660_rrip_initialize_node(iso9660_disk *diskStructure, cd9660node *node,
    cd9660node *parent, cd9660node *grandparent)
{
	struct ISO_SUSP_ATTRIBUTES *current = NULL;

	assert(node != NULL);

	if (node->type & CD9660_TYPE_DOT) {
		/*
		 * Handle ER - should be the only entry to appear on
		 * a "." record
		 */
		if (node->parent == diskStructure->rootNode) {
			cd9660_susp_ER(node, 1, SUSP_RRIP_ER_EXT_ID,
				SUSP_RRIP_ER_EXT_DES, SUSP_RRIP_ER_EXT_SRC);
		}
		if (parent != NULL && parent->node != NULL &&
		    parent->node->inode != NULL) {
			/* PX - POSIX attributes */
			current = cd9660node_susp_create_node(SUSP_TYPE_RRIP,
				SUSP_ENTRY_RRIP_PX, "PX", SUSP_LOC_ENTRY);
			cd9660node_rrip_px(current, parent->node);
			TAILQ_INSERT_TAIL(&node->head, current, rr_ll);
		}
	} else if (node->type & CD9660_TYPE_DOTDOT) {
		if (grandparent != NULL && grandparent->node != NULL &&
		    grandparent->node->inode != NULL) {
			/* PX - POSIX attributes */
			current = cd9660node_susp_create_node(SUSP_TYPE_RRIP,
				SUSP_ENTRY_RRIP_PX, "PX", SUSP_LOC_ENTRY);
			cd9660node_rrip_px(current, grandparent->node);
			TAILQ_INSERT_TAIL(&node->head, current, rr_ll);
		}
		/* Handle PL */
		if (parent != NULL && parent->rr_real_parent != NULL) {
			current = cd9660node_susp_create_node(SUSP_TYPE_RRIP,
			    SUSP_ENTRY_RRIP_PL, "PL", SUSP_LOC_DOTDOT);
			cd9660_rrip_PL(current,node);
			TAILQ_INSERT_TAIL(&node->head, current, rr_ll);
		}
	} else {
		cd9660_rrip_initialize_inode(node);

		/*
		 * Not every node needs a NM set - only if the name is
		 * actually different. IE: If a file is TEST -> TEST,
		 * no NM. test -> TEST, need a NM
		 *
		 * The rr_moved_dir needs to be assigned a NM record as well.
		 */
		if (node == diskStructure->rr_moved_dir) {
			cd9660_rrip_add_NM(node, RRIP_DEFAULT_MOVE_DIR_NAME);
		}
		else if ((node->node != NULL) &&
			((strlen(node->node->name) !=
			    (uint8_t)node->isoDirRecord->name_len[0]) ||
			(memcmp(node->node->name,node->isoDirRecord->name,
				(uint8_t)node->isoDirRecord->name_len[0]) != 0))) {
			cd9660_rrip_NM(node);
		}



		/* Rock ridge directory relocation code here. */

		/* First handle the CL for the placeholder file. */
		if (node->rr_relocated != NULL) {
			current = cd9660node_susp_create_node(SUSP_TYPE_RRIP,
				SUSP_ENTRY_RRIP_CL, "CL", SUSP_LOC_ENTRY);
			cd9660_rrip_CL(current, node);
			TAILQ_INSERT_TAIL(&node->head, current, rr_ll);
		}

		/* Handle RE*/
		if (node->rr_real_parent != NULL) {
			current = cd9660node_susp_create_node(SUSP_TYPE_RRIP,
				SUSP_ENTRY_RRIP_RE, "RE", SUSP_LOC_ENTRY);
			cd9660_rrip_RE(current,node);
			TAILQ_INSERT_TAIL(&node->head, current, rr_ll);
		}
	}
	return 1;
}

struct ISO_SUSP_ATTRIBUTES*
cd9660node_susp_create_node(int susp_type, int entry_type, const char *type_id,
			    int write_loc)
{
	struct ISO_SUSP_ATTRIBUTES* temp;

	temp = emalloc(sizeof(*temp));
	temp->susp_type = susp_type;
	temp->entry_type = entry_type;
	temp->last_in_suf = 0;
	/* Phase this out */
	temp->type_of[0] = type_id[0];
	temp->type_of[1] = type_id[1];
	temp->write_location = write_loc;

	/*
	 * Since the first four bytes is common, lets go ahead and
	 * set the type identifier, since we are passing that to this
	 * function anyhow.
	 */
	temp->attr.su_entry.SP.h.type[0] = type_id[0];
	temp->attr.su_entry.SP.h.type[1] = type_id[1];
	return temp;
}

int
cd9660_rrip_PL(struct ISO_SUSP_ATTRIBUTES* p, cd9660node *node __unused)
{
	p->attr.rr_entry.PL.h.length[0] = 12;
	p->attr.rr_entry.PL.h.version[0] = 1;
	return 1;
}

int
cd9660_rrip_CL(struct ISO_SUSP_ATTRIBUTES *p, cd9660node *node __unused)
{
	p->attr.rr_entry.CL.h.length[0] = 12;
	p->attr.rr_entry.CL.h.version[0] = 1;
	return 1;
}

int
cd9660_rrip_RE(struct ISO_SUSP_ATTRIBUTES *p, cd9660node *node __unused)
{
	p->attr.rr_entry.RE.h.length[0] = 4;
	p->attr.rr_entry.RE.h.version[0] = 1;
	return 1;
}

void
cd9660_createSL(cd9660node *node)
{
	struct ISO_SUSP_ATTRIBUTES* current;
	int path_count, dir_count, done, i, j, dir_copied;
	char temp_cr[255];
	char temp_sl[255]; /* used in copying continuation entry*/
	char* sl_ptr;

	sl_ptr = node->node->symlink;

	done = 0;
	path_count = 0;
	dir_count = 0;
	dir_copied = 0;
	current = cd9660node_susp_create_node(SUSP_TYPE_RRIP,
	    SUSP_ENTRY_RRIP_SL, "SL", SUSP_LOC_ENTRY);

	current->attr.rr_entry.SL.h.version[0] = 1;
	current->attr.rr_entry.SL.flags[0] = SL_FLAGS_NONE;

	if (*sl_ptr == '/') {
		temp_cr[0] = SL_FLAGS_ROOT;
		temp_cr[1] = 0;
		memcpy(current->attr.rr_entry.SL.component + path_count,
		    temp_cr, 2);
		path_count += 2;
		sl_ptr++;
	}

	for (i = 0; i < (dir_count + 2); i++)
		temp_cr[i] = '\0';

	while (!done) {
		while ((*sl_ptr != '/') && (*sl_ptr != '\0')) {
			dir_copied = 1;
			if (*sl_ptr == '.') {
				if ((*(sl_ptr + 1) == '/') || (*(sl_ptr + 1)
				     == '\0')) {
					temp_cr[0] = SL_FLAGS_CURRENT;
					sl_ptr++;
				} else if(*(sl_ptr + 1) == '.') {
					if ((*(sl_ptr + 2) == '/') ||
					    (*(sl_ptr + 2) == '\0')) {
						temp_cr[0] = SL_FLAGS_PARENT;
						sl_ptr += 2;
					}
				} else {
					temp_cr[dir_count+2] = *sl_ptr;
					sl_ptr++;
					dir_count++;
				}
			} else {
				temp_cr[dir_count + 2] = *sl_ptr;
				sl_ptr++;
				dir_count++;
			}
		}

		if ((path_count + dir_count) >= 249) {
			current->attr.rr_entry.SL.flags[0] |= SL_FLAGS_CONTINUE;

			j = 0;

			if (path_count <= 249) {
				while(j != (249 - path_count)) {
					temp_sl[j] = temp_cr[j];
					j++;
				}
				temp_sl[0] = SL_FLAGS_CONTINUE;
				temp_sl[1] = j - 2;
				memcpy(
				    current->attr.rr_entry.SL.component +
					path_count,
				    temp_sl, j);
			}

			path_count += j;
			current->attr.rr_entry.SL.h.length[0] = path_count + 5;
			TAILQ_INSERT_TAIL(&node->head, current, rr_ll);
			current= cd9660node_susp_create_node(SUSP_TYPE_RRIP,
			       SUSP_ENTRY_RRIP_SL, "SL", SUSP_LOC_ENTRY);
			current->attr.rr_entry.SL.h.version[0] = 1;
			current->attr.rr_entry.SL.flags[0] = SL_FLAGS_NONE;

			path_count = 0;

			if (dir_count > 2) {
				while (j != dir_count + 2) {
					current->attr.rr_entry.SL.component[
					    path_count + 2] = temp_cr[j];
					j++;
					path_count++;
				}
				current->attr.rr_entry.SL.component[1]
				    = path_count;
				path_count+= 2;
			} else {
				while(j != dir_count) {
					current->attr.rr_entry.SL.component[
					    path_count+2] = temp_cr[j];
					j++;
					path_count++;
				}
			}
		} else {
			if (dir_copied == 1) {
				temp_cr[1] = dir_count;
				memcpy(current->attr.rr_entry.SL.component +
					path_count,
				    temp_cr, dir_count + 2);
				path_count += dir_count + 2;
			}
		}

		if (*sl_ptr == '\0') {
			done = 1;
			current->attr.rr_entry.SL.h.length[0] = path_count + 5;
			TAILQ_INSERT_TAIL(&node->head, current, rr_ll);
		} else {
			sl_ptr++;
			dir_count = 0;
			dir_copied = 0;
			for(i = 0; i < 255; i++) {
				temp_cr[i] = '\0';
			}
		}
	}
}

int
cd9660node_rrip_px(struct ISO_SUSP_ATTRIBUTES *v, fsnode *pxinfo)
{
	v->attr.rr_entry.PX.h.length[0] = 44;
	v->attr.rr_entry.PX.h.version[0] = 1;
	cd9660_bothendian_dword(pxinfo->inode->st.st_mode,
	    v->attr.rr_entry.PX.mode);
	cd9660_bothendian_dword(pxinfo->inode->st.st_nlink,
	    v->attr.rr_entry.PX.links);
	cd9660_bothendian_dword(pxinfo->inode->st.st_uid,
	    v->attr.rr_entry.PX.uid);
	cd9660_bothendian_dword(pxinfo->inode->st.st_gid,
	    v->attr.rr_entry.PX.gid);
	cd9660_bothendian_dword(pxinfo->inode->st.st_ino,
	    v->attr.rr_entry.PX.serial);

	return 1;
}

int
cd9660node_rrip_pn(struct ISO_SUSP_ATTRIBUTES *pn_field, fsnode *fnode)
{
	pn_field->attr.rr_entry.PN.h.length[0] = 20;
	pn_field->attr.rr_entry.PN.h.version[0] = 1;

	if (sizeof (fnode->inode->st.st_rdev) > 4)
		cd9660_bothendian_dword(
		    (uint64_t)fnode->inode->st.st_rdev >> 32,
		    pn_field->attr.rr_entry.PN.high);
	else
		cd9660_bothendian_dword(0, pn_field->attr.rr_entry.PN.high);

	cd9660_bothendian_dword(fnode->inode->st.st_rdev & 0xffffffff,
		pn_field->attr.rr_entry.PN.low);
	return 1;
}

#if 0
int
cd9660node_rrip_nm(struct ISO_SUSP_ATTRIBUTES *p, cd9660node *file_node)
{
	int nm_length = strlen(file_node->isoDirRecord->name) + 5;
        p->attr.rr_entry.NM.h.type[0] = 'N';
	p->attr.rr_entry.NM.h.type[1] = 'M';
	sprintf(p->attr.rr_entry.NM.altname, "%s", file_node->isoDirRecord->name);
	p->attr.rr_entry.NM.h.length[0] = (unsigned char)nm_length;
	p->attr.rr_entry.NM.h.version[0] = (unsigned char)1;
	p->attr.rr_entry.NM.flags[0] = (unsigned char) NM_PARENT;
	return 1;
}
#endif

int
cd9660node_rrip_tf(struct ISO_SUSP_ATTRIBUTES *p, fsnode *_node)
{
	p->attr.rr_entry.TF.flags[0] = TF_MODIFY | TF_ACCESS | TF_ATTRIBUTES;
	p->attr.rr_entry.TF.h.length[0] = 5;
	p->attr.rr_entry.TF.h.version[0] = 1;

	/*
	 * Need to add creation time, backup time,
	 * expiration time, and effective time.
	 */

	cd9660_time_915(p->attr.rr_entry.TF.timestamp,
		_node->inode->st.st_atime);
	p->attr.rr_entry.TF.h.length[0] += 7;

	cd9660_time_915(p->attr.rr_entry.TF.timestamp + 7,
		_node->inode->st.st_mtime);
	p->attr.rr_entry.TF.h.length[0] += 7;

	cd9660_time_915(p->attr.rr_entry.TF.timestamp + 14,
		_node->inode->st.st_ctime);
	p->attr.rr_entry.TF.h.length[0] += 7;
	return 1;
}

int
cd9660_susp_sp(struct ISO_SUSP_ATTRIBUTES *p, cd9660node *spinfo __unused)
{
	p->attr.su_entry.SP.h.length[0] = 7;
	p->attr.su_entry.SP.h.version[0] = 1;
	p->attr.su_entry.SP.check[0] = 0xBE;
	p->attr.su_entry.SP.check[1] = 0xEF;
	p->attr.su_entry.SP.len_skp[0] = 0;
	return 1;
}

int
cd9660_susp_st(struct ISO_SUSP_ATTRIBUTES *p, cd9660node *stinfo __unused)
{
	p->attr.su_entry.ST.h.type[0] = 'S';
	p->attr.su_entry.ST.h.type[1] = 'T';
	p->attr.su_entry.ST.h.length[0] = 4;
	p->attr.su_entry.ST.h.version[0] = 1;
	return 1;
}

int
cd9660_susp_ce(struct ISO_SUSP_ATTRIBUTES *p, cd9660node *spinfo __unused)
{
	p->attr.su_entry.CE.h.length[0] = 28;
	p->attr.su_entry.CE.h.version[0] = 1;
	/* Other attributes dont matter right now, will be updated later */
	return 1;
}

int
cd9660_susp_pd(struct ISO_SUSP_ATTRIBUTES *p __unused, int length __unused)
{
	return 1;
}

void
cd9660_rrip_add_NM(cd9660node *node, const char *name)
{
	int working,len;
	const char *p;
	struct ISO_SUSP_ATTRIBUTES *r;

	/*
	 * Each NM record has 254 byes to work with. This means that
	 * the name data itself only has 249 bytes to work with. So, a
	 * name with 251 characters would require two nm records.
	 */
	p = name;
	working = 1;
	while (working) {
		r = cd9660node_susp_create_node(SUSP_TYPE_RRIP,
		    SUSP_ENTRY_RRIP_NM, "NM", SUSP_LOC_ENTRY);
		r->attr.rr_entry.NM.h.version[0] = 1;
		r->attr.rr_entry.NM.flags[0] = RRIP_NM_FLAGS_NONE;
		len = strlen(p);

		if (len > 249) {
			len = 249;
			r->attr.rr_entry.NM.flags[0] = RRIP_NM_FLAGS_CONTINUE;
		} else {
			working = 0;
		}
		memcpy(r->attr.rr_entry.NM.altname, p, len);
		r->attr.rr_entry.NM.h.length[0] = 5 + len;

		TAILQ_INSERT_TAIL(&node->head, r, rr_ll);

		p += len;
	}
}

void
cd9660_rrip_NM(cd9660node *node)
{
	cd9660_rrip_add_NM(node, node->node->name);
}

struct ISO_SUSP_ATTRIBUTES*
cd9660_susp_ER(cd9660node *node,
	       u_char ext_version, const char* ext_id, const char* ext_des,
	       const char* ext_src)
{
	int l;
	struct ISO_SUSP_ATTRIBUTES *r;

	r = cd9660node_susp_create_node(SUSP_TYPE_SUSP,
			SUSP_ENTRY_SUSP_ER, "ER", SUSP_LOC_DOT);

	/* Fixed data is 8 bytes */
	r->attr.su_entry.ER.h.length[0] = 8;
	r->attr.su_entry.ER.h.version[0] = 1;

	r->attr.su_entry.ER.len_id[0] = (u_char)strlen(ext_id);
	r->attr.su_entry.ER.len_des[0] = (u_char)strlen(ext_des);
	r->attr.su_entry.ER.len_src[0] = (u_char)strlen(ext_src);

	l = r->attr.su_entry.ER.len_id[0] +
		r->attr.su_entry.ER.len_src[0] +
		r->attr.su_entry.ER.len_des[0];

	/* Everything must fit. */
	assert(l + r->attr.su_entry.ER.h.length[0] <= 254);

	r->attr.su_entry.ER.h.length[0] += (u_char)l;


	r->attr.su_entry.ER.ext_ver[0] = ext_version;
	memcpy(r->attr.su_entry.ER.ext_data, ext_id,
		(int)r->attr.su_entry.ER.len_id[0]);
	l = (int) r->attr.su_entry.ER.len_id[0];
	memcpy(r->attr.su_entry.ER.ext_data + l,ext_des,
		(int)r->attr.su_entry.ER.len_des[0]);

	l += (int)r->attr.su_entry.ER.len_des[0];
	memcpy(r->attr.su_entry.ER.ext_data + l,ext_src,
		(int)r->attr.su_entry.ER.len_src[0]);

	TAILQ_INSERT_TAIL(&node->head, r, rr_ll);
	return r;
}

struct ISO_SUSP_ATTRIBUTES*
cd9660_susp_ES(struct ISO_SUSP_ATTRIBUTES *last __unused, cd9660node *node __unused)
{
	return NULL;
}
