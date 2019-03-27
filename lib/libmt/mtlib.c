/*-
 * Copyright (c) 2013, 2014, 2015 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Ken Merry           (Spectra Logic Corporation)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/queue.h>
#include <sys/sbuf.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <bsdxml.h>
#include <mtlib.h>

/*
 * Called at the start of each XML element, and includes the list of
 * attributes for the element.
 */
void
mt_start_element(void *user_data, const char *name, const char **attr)
{
	int i;
	struct mt_status_data *mtinfo;
	struct mt_status_entry *entry;

	mtinfo = (struct mt_status_data *)user_data;

	if (mtinfo->error != 0)
		return;

	mtinfo->level++;
	if ((u_int)mtinfo->level >= (sizeof(mtinfo->cur_sb) /
            sizeof(mtinfo->cur_sb[0]))) {
		mtinfo->error = 1;
                snprintf(mtinfo->error_str, sizeof(mtinfo->error_str), 
		    "%s: too many nesting levels, %zd max", __func__,
		    sizeof(mtinfo->cur_sb) / sizeof(mtinfo->cur_sb[0]));
		return;
	}

        mtinfo->cur_sb[mtinfo->level] = sbuf_new_auto();
        if (mtinfo->cur_sb[mtinfo->level] == NULL) {
		mtinfo->error = 1;
                snprintf(mtinfo->error_str, sizeof(mtinfo->error_str),
		    "%s: Unable to allocate sbuf", __func__);
		return;
	}

	entry = malloc(sizeof(*entry));
	if (entry == NULL) {
		mtinfo->error = 1;
		snprintf(mtinfo->error_str, sizeof(mtinfo->error_str),
		    "%s: unable to allocate %zd bytes", __func__,
		    sizeof(*entry));
		return;
	}
	bzero(entry, sizeof(*entry)); 
	STAILQ_INIT(&entry->nv_list);
	STAILQ_INIT(&entry->child_entries);
	entry->entry_name = strdup(name);
	mtinfo->cur_entry[mtinfo->level] = entry;
	if (mtinfo->cur_entry[mtinfo->level - 1] == NULL) {
		STAILQ_INSERT_TAIL(&mtinfo->entries, entry, links);
	} else {
		STAILQ_INSERT_TAIL(
		    &mtinfo->cur_entry[mtinfo->level - 1]->child_entries,
		    entry, links);
		entry->parent = mtinfo->cur_entry[mtinfo->level - 1];
	}
	for (i = 0; attr[i] != NULL; i+=2) {
		struct mt_status_nv *nv;
		int need_nv;

		need_nv = 0;

		if (strcmp(attr[i], "size") == 0) {
			entry->size = strtoull(attr[i+1], NULL, 0);
		} else if (strcmp(attr[i], "type") == 0) {
			if (strcmp(attr[i+1], "int") == 0) {
				entry->var_type = MT_TYPE_INT;
			} else if (strcmp(attr[i+1], "uint") == 0) {
				entry->var_type = MT_TYPE_UINT;
			} else if (strcmp(attr[i+1], "str") == 0) {
				entry->var_type = MT_TYPE_STRING;
			} else if (strcmp(attr[i+1], "node") == 0) {
				entry->var_type = MT_TYPE_NODE;
			} else {
				need_nv = 1;
			}
		} else if (strcmp(attr[i], "fmt") == 0) {
			entry->fmt = strdup(attr[i+1]);
		} else if (strcmp(attr[i], "desc") == 0) {
			entry->desc = strdup(attr[i+1]);
		} else {
			need_nv = 1;
		}
		if (need_nv != 0) {
			nv = malloc(sizeof(*nv));
			if (nv == NULL) {
				mtinfo->error = 1;
				snprintf(mtinfo->error_str,
				    sizeof(mtinfo->error_str),
				    "%s: error allocating %zd bytes",
				    __func__, sizeof(*nv));
			}
			bzero(nv, sizeof(*nv));
			nv->name = strdup(attr[i]);
			nv->value = strdup(attr[i+1]);
			STAILQ_INSERT_TAIL(&entry->nv_list, nv, links);
		}
	}
}

/*
 * Called on XML element close.
 */
void
mt_end_element(void *user_data, const char *name)
{
	struct mt_status_data *mtinfo;
	char *str;

	mtinfo = (struct mt_status_data *)user_data;

	if (mtinfo->error != 0)
		return;

	if (mtinfo->cur_sb[mtinfo->level] == NULL) {
		mtinfo->error = 1;
		snprintf(mtinfo->error_str, sizeof(mtinfo->error_str),
		    "%s: no valid sbuf at level %d (name %s)", __func__,
		    mtinfo->level, name);
		return;
	}
	sbuf_finish(mtinfo->cur_sb[mtinfo->level]);
	str = strdup(sbuf_data(mtinfo->cur_sb[mtinfo->level]));
	if (str == NULL) {
		mtinfo->error = 1;
		snprintf(mtinfo->error_str, sizeof(mtinfo->error_str),
		    "%s can't allocate %zd bytes for string", __func__,
		    sbuf_len(mtinfo->cur_sb[mtinfo->level]));
		return;
	}

	if (strlen(str) == 0) {
		free(str);
		str = NULL;
	}
	if (str != NULL) {
		struct mt_status_entry *entry;

		entry = mtinfo->cur_entry[mtinfo->level];
		switch(entry->var_type) {
		case MT_TYPE_INT:
			entry->value_signed = strtoll(str, NULL, 0);
			break;
		case MT_TYPE_UINT:
			entry->value_unsigned = strtoull(str, NULL, 0);
			break;
		default:
			break;
		}
	}

	mtinfo->cur_entry[mtinfo->level]->value = str;

	sbuf_delete(mtinfo->cur_sb[mtinfo->level]);
	mtinfo->cur_sb[mtinfo->level] = NULL;
	mtinfo->cur_entry[mtinfo->level] = NULL;
	mtinfo->level--;
}

/*
 * Called to handle character strings in the current element.
 */
void
mt_char_handler(void *user_data, const XML_Char *str, int len)
{
	struct mt_status_data *mtinfo;

	mtinfo = (struct mt_status_data *)user_data;
	if (mtinfo->error != 0)
		return;

	sbuf_bcat(mtinfo->cur_sb[mtinfo->level], str, len);
}

void
mt_status_tree_sbuf(struct sbuf *sb, struct mt_status_entry *entry, int indent,
    void (*sbuf_func)(struct sbuf *sb, struct mt_status_entry *entry,
    void *arg), void *arg)
{
	struct mt_status_nv *nv;
	struct mt_status_entry *entry2;

	if (sbuf_func != NULL) {
		sbuf_func(sb, entry, arg);
	} else {
		sbuf_printf(sb, "%*sname: %s, value: %s, fmt: %s, size: %zd, "
		    "type: %d, desc: %s\n", indent, "", entry->entry_name,
		    entry->value, entry->fmt, entry->size, entry->var_type,
		    entry->desc);
		STAILQ_FOREACH(nv, &entry->nv_list, links) {
			sbuf_printf(sb, "%*snv: name: %s, value: %s\n",
			    indent + 1, "", nv->name, nv->value);
		}
	}

	STAILQ_FOREACH(entry2, &entry->child_entries, links)
		mt_status_tree_sbuf(sb, entry2, indent + 2, sbuf_func, arg);
}

void
mt_status_tree_print(struct mt_status_entry *entry, int indent,
    void (*print_func)(struct mt_status_entry *entry, void *arg), void *arg)
{

	if (print_func != NULL) {
		struct mt_status_entry *entry2;

		print_func(entry, arg);
		STAILQ_FOREACH(entry2, &entry->child_entries, links)
			mt_status_tree_print(entry2, indent + 2, print_func,
			    arg);
	} else {
		struct sbuf *sb;

		sb = sbuf_new_auto();
		if (sb == NULL)
			return;
		mt_status_tree_sbuf(sb, entry, indent, NULL, NULL);
		sbuf_finish(sb);

		printf("%s", sbuf_data(sb));
		sbuf_delete(sb);
	}
}

/*
 * Given a parameter name in the form "foo" or "foo.bar.baz", traverse the
 * tree looking for the parameter (the first case) or series of parameters
 * (second case).
 */
struct mt_status_entry *
mt_entry_find(struct mt_status_entry *entry, char *name)
{
	struct mt_status_entry *entry2;
	char *tmpname = NULL, *tmpname2 = NULL, *tmpstr = NULL;

	tmpname = strdup(name);
	if (tmpname == NULL)
		goto bailout;

	/* Save a pointer so we can free this later */
	tmpname2 = tmpname;

	tmpstr = strsep(&tmpname, ".");
	
	/*
	 * Is this the entry we're looking for?  Or do we have further
	 * child entries that we need to grab?
	 */
	if (strcmp(entry->entry_name, tmpstr) == 0) {
	 	if (tmpname == NULL) {
			/*
			 * There are no further child entries to find.  We
			 * have a complete match.
			 */
			free(tmpname2);
			return (entry);
		} else {
			/*
			 * There are more child entries that we need to find.
			 * Fall through to the recursive search off of this
			 * entry, below.  Use tmpname, which will contain
			 * everything after the first period.
			 */
			name = tmpname;
		}
	} 

	/*
	 * Recursively look for further entries.
	 */
	STAILQ_FOREACH(entry2, &entry->child_entries, links) {
		struct mt_status_entry *entry3;

		entry3 = mt_entry_find(entry2, name);
		if (entry3 != NULL) {
			free(tmpname2);
			return (entry3);
		}
	}

bailout:
	free(tmpname2);

	return (NULL);
}

struct mt_status_entry *
mt_status_entry_find(struct mt_status_data *status_data, char *name)
{
	struct mt_status_entry *entry, *entry2;

	STAILQ_FOREACH(entry, &status_data->entries, links) {
		entry2 = mt_entry_find(entry, name);
		if (entry2 != NULL)
			return (entry2);
	}

	return (NULL);
}

void
mt_status_entry_free(struct mt_status_entry *entry)
{
	struct mt_status_entry *entry2, *entry3;
	struct mt_status_nv *nv, *nv2;

	STAILQ_FOREACH_SAFE(entry2, &entry->child_entries, links, entry3) {
		STAILQ_REMOVE(&entry->child_entries, entry2, mt_status_entry,
		    links);
		mt_status_entry_free(entry2);
	}

	free(entry->entry_name);
	free(entry->value);
	free(entry->fmt);
	free(entry->desc);

	STAILQ_FOREACH_SAFE(nv, &entry->nv_list, links, nv2) {
		STAILQ_REMOVE(&entry->nv_list, nv, mt_status_nv, links);
		free(nv->name);
		free(nv->value);
		free(nv);
	}
	free(entry);
}

void
mt_status_free(struct mt_status_data *status_data)
{
	struct mt_status_entry *entry, *entry2;

	STAILQ_FOREACH_SAFE(entry, &status_data->entries, links, entry2) {
		STAILQ_REMOVE(&status_data->entries, entry, mt_status_entry,
		    links);
		mt_status_entry_free(entry);
	}
}

void
mt_entry_sbuf(struct sbuf *sb, struct mt_status_entry *entry, char *fmt)
{
	switch(entry->var_type) {
	case MT_TYPE_INT:
		if (fmt != NULL)
			sbuf_printf(sb, fmt, (intmax_t)entry->value_signed);
		else
			sbuf_printf(sb, "%jd",
				    (intmax_t)entry->value_signed);
		break;
	case MT_TYPE_UINT:
		if (fmt != NULL)
			sbuf_printf(sb, fmt, (uintmax_t)entry->value_unsigned);
		else
			sbuf_printf(sb, "%ju",
				    (uintmax_t)entry->value_unsigned);
		break;
	default:
		if (fmt != NULL)
			sbuf_printf(sb, fmt, entry->value);
		else
			sbuf_printf(sb, "%s", entry->value);
		break;
	}
}

void
mt_param_parent_print(struct mt_status_entry *entry,
    struct mt_print_params *print_params)
{
	if (entry->parent != NULL)
		mt_param_parent_print(entry->parent, print_params);

	if (((print_params->flags & MT_PF_INCLUDE_ROOT) == 0)
	 && (strcmp(entry->entry_name, print_params->root_name) == 0))
		return;

	printf("%s.", entry->entry_name);
}

void
mt_param_parent_sbuf(struct sbuf *sb, struct mt_status_entry *entry,
    struct mt_print_params *print_params)
{
	if (entry->parent != NULL)
		mt_param_parent_sbuf(sb, entry->parent, print_params);

	if (((print_params->flags & MT_PF_INCLUDE_ROOT) == 0)
	 && (strcmp(entry->entry_name, print_params->root_name) == 0))
		return;

	sbuf_printf(sb, "%s.", entry->entry_name);
}

void
mt_param_entry_sbuf(struct sbuf *sb, struct mt_status_entry *entry, void *arg)
{
	struct mt_print_params *print_params;

	print_params = (struct mt_print_params *)arg;

	/*
	 * We don't want to print nodes.
	 */
	if (entry->var_type == MT_TYPE_NODE)
		return;

	if ((print_params->flags & MT_PF_FULL_PATH)
	 && (entry->parent != NULL))
		mt_param_parent_sbuf(sb, entry->parent, print_params);

	sbuf_printf(sb, "%s: %s", entry->entry_name, entry->value);
	if ((print_params->flags & MT_PF_VERBOSE)
	 && (entry->desc != NULL)
	 && (strlen(entry->desc) > 0))
		sbuf_printf(sb, " (%s)", entry->desc);
	sbuf_printf(sb, "\n");

}

void
mt_param_entry_print(struct mt_status_entry *entry, void *arg)
{
	struct mt_print_params *print_params;

	print_params = (struct mt_print_params *)arg;

	/*
	 * We don't want to print nodes.
	 */
	if (entry->var_type == MT_TYPE_NODE)
		return;

	if ((print_params->flags & MT_PF_FULL_PATH)
	 && (entry->parent != NULL))
		mt_param_parent_print(entry->parent, print_params);

	printf("%s: %s", entry->entry_name, entry->value);
	if ((print_params->flags & MT_PF_VERBOSE)
	 && (entry->desc != NULL)
	 && (strlen(entry->desc) > 0))
		printf(" (%s)", entry->desc);
	printf("\n");
}

int
mt_protect_print(struct mt_status_data *status_data, int verbose)
{
	struct mt_status_entry *entry;
	const char *prot_name = MT_PROTECTION_NAME;
	struct mt_print_params print_params;

	snprintf(print_params.root_name, sizeof(print_params.root_name),
	    MT_PARAM_ROOT_NAME);
	print_params.flags = MT_PF_FULL_PATH;
	if (verbose != 0)
		print_params.flags |= MT_PF_VERBOSE;

	entry = mt_status_entry_find(status_data, __DECONST(char *,prot_name));
	if (entry == NULL)
		return (1);
	mt_status_tree_print(entry, 0, mt_param_entry_print, &print_params);

	return (0);
}

int
mt_param_list(struct mt_status_data *status_data, char *param_name, int quiet)
{
	struct mt_status_entry *entry;
	struct mt_print_params print_params;
	char root_name[20];

	snprintf(root_name, sizeof(root_name), "mtparamget");
	strlcpy(print_params.root_name, root_name,
	    sizeof(print_params.root_name));

	print_params.flags = MT_PF_FULL_PATH;
	if (quiet == 0)
		print_params.flags |= MT_PF_VERBOSE;

	if (param_name != NULL) {
		entry = mt_status_entry_find(status_data, param_name);
		if (entry == NULL)
			return (1);

		mt_param_entry_print(entry, &print_params);

		return (0);
	} else {
		entry = mt_status_entry_find(status_data, root_name);

		STAILQ_FOREACH(entry, &status_data->entries, links)
			mt_status_tree_print(entry, 0, mt_param_entry_print,
			    &print_params);
	}

	return (0);
}

static struct densities {
	int dens;
	int bpmm;
	int bpi;
	const char *name;
} dens[] = {
	/*
	 * Taken from T10 Project 997D 
	 * SCSI-3 Stream Device Commands (SSC)
	 * Revision 11, 4-Nov-97
	 *
	 * LTO 1-6 definitions obtained from the eighth edition of the
	 * IBM TotalStorage LTO Ultrium Tape Drive SCSI Reference
	 * (July 2007) and the second edition of the IBM System Storage LTO
	 * Tape Drive SCSI Reference (February 13, 2013).
	 *
	 * IBM 3592 definitions obtained from second edition of the IBM
	 * System Storage Tape Drive 3592 SCSI Reference (May 25, 2012).
	 *
	 * DAT-72 and DAT-160 bpi values taken from "HP StorageWorks DAT160
	 * tape drive white paper", dated June 2007.
	 *
	 * DAT-160 / SDLT220 density code (0x48) conflict information
	 * found here:
	 *
	 * http://h20564.www2.hp.com/hpsc/doc/public/display?docId=emr_na-c01065117&sp4ts.oid=429311
 	 * (Document ID c01065117)
	 */
	/*Num.  bpmm    bpi     Reference     */
	{ 0x1,	32,	800,	"X3.22-1983" },
	{ 0x2,	63,	1600,	"X3.39-1986" },
	{ 0x3,	246,	6250,	"X3.54-1986" },
	{ 0x5,	315,	8000,	"X3.136-1986" },
	{ 0x6,	126,	3200,	"X3.157-1987" },
	{ 0x7,	252,	6400,	"X3.116-1986" },
	{ 0x8,	315,	8000,	"X3.158-1987" },
	{ 0x9,	491,	37871,	"X3.180" },
	{ 0xA,	262,	6667,	"X3B5/86-199" },
	{ 0xB,	63,	1600,	"X3.56-1986" },
	{ 0xC,	500,	12690,	"HI-TC1" },
	{ 0xD,	999,	25380,	"HI-TC2" },
	{ 0xF,	394,	10000,	"QIC-120" },
	{ 0x10,	394,	10000,	"QIC-150" },
	{ 0x11,	630,	16000,	"QIC-320" },
	{ 0x12,	2034,	51667,	"QIC-1350" },
	{ 0x13,	2400,	61000,	"X3B5/88-185A" },
	{ 0x14,	1703,	43245,	"X3.202-1991" },
	{ 0x15,	1789,	45434,	"ECMA TC17" },
	{ 0x16,	394,	10000,	"X3.193-1990" },
	{ 0x17,	1673,	42500,	"X3B5/91-174" },
	{ 0x18,	1673,	42500,	"X3B5/92-50" },
	{ 0x19, 2460,   62500,  "DLTapeIII" },
	{ 0x1A, 3214,   81633,  "DLTapeIV(20GB)" },
	{ 0x1B, 3383,   85937,  "DLTapeIV(35GB)" },
	{ 0x1C, 1654,	42000,	"QIC-385M" },
	{ 0x1D,	1512,	38400,	"QIC-410M" },
	{ 0x1E, 1385,	36000,	"QIC-1000C" },
	{ 0x1F,	2666,	67733,	"QIC-2100C" },
	{ 0x20, 2666,	67733,	"QIC-6GB(M)" },
	{ 0x21,	2666,	67733,	"QIC-20GB(C)" },
	{ 0x22,	1600,	40640,	"QIC-2GB(C)" },
	{ 0x23, 2666,	67733,	"QIC-875M" },
	{ 0x24,	2400,	61000,	"DDS-2" },
	{ 0x25,	3816,	97000,	"DDS-3" },
	{ 0x26,	3816,	97000,	"DDS-4" },
	{ 0x27,	3056,	77611,	"Mammoth" },
	{ 0x28,	1491,	37871,	"X3.224" },
	{ 0x40, 4880,   123952, "LTO-1" },
	{ 0x41, 3868,   98250,  "DLTapeIV(40GB)" },
	{ 0x42, 7398,   187909, "LTO-2" },
	{ 0x44, 9638,   244805, "LTO-3" }, 
	{ 0x46, 12725,  323215, "LTO-4" }, 
	{ 0x47, 6417,   163000, "DAT-72" },
	/*
	 * XXX KDM note that 0x48 is also the density code for DAT-160.
	 * For some reason they used overlapping density codes.
	 */
#if 0
	{ 0x48, 6870,   174500, "DAT-160" },
#endif
	{ 0x48, 5236,   133000, "SDLTapeI(110)" },
	{ 0x49, 7598,   193000, "SDLTapeI(160)" },
	{ 0x4a,     0,       0, "T10000A" },
	{ 0x4b,     0,       0, "T10000B" },
	{ 0x4c,     0,       0, "T10000C" },
	{ 0x4d,     0,       0, "T10000D" },
	{ 0x51, 11800,  299720, "3592A1 (unencrypted)" },
	{ 0x52, 11800,  299720, "3592A2 (unencrypted)" },
	{ 0x53, 13452,  341681, "3592A3 (unencrypted)" },
	{ 0x54, 19686,  500024, "3592A4 (unencrypted)" },
	{ 0x55, 20670,  525018, "3592A5 (unencrypted)" },
	{ 0x56, 20670,  525018, "3592B5 (unencrypted)" },
	{ 0x57, 21850,  554990, "3592A6 (unencrypted)" },
	{ 0x58, 15142,  384607, "LTO-5" },
	{ 0x5A, 15142,  384607, "LTO-6" },
	{ 0x5C, 19107,  485318, "LTO-7" },
	{ 0x5D, 19107,  485318, "LTO-M8" },
	{ 0x5E, 20669,  524993, "LTO-8" },
	{ 0x71, 11800,  299720, "3592A1 (encrypted)" },
	{ 0x72, 11800,  299720, "3592A2 (encrypted)" },
	{ 0x73, 13452,  341681, "3592A3 (encrypted)" },
	{ 0x74, 19686,  500024, "3592A4 (encrypted)" },
	{ 0x75, 20670,  525018, "3592A5 (encrypted)" },
	{ 0x76, 20670,  525018, "3592B5 (encrypted)" },
	{ 0x77, 21850,  554990, "3592A6 (encrypted)" },
	{ 0x8c,  1789,   45434, "EXB-8500c" },
	{ 0x90,  1703,   43245, "EXB-8200c" },
	{ 0, 0, 0, NULL }
};

const char *
mt_density_name(int density_num)
{
	struct densities *sd;

	/* densities 0 and 0x7f are handled as special cases */
	if (density_num == 0)
		return ("default");
	if (density_num == 0x7f)
		return ("same");

	for (sd = dens; sd->dens != 0; sd++)
		if (sd->dens == density_num)
			break;
	if (sd->dens == 0)
		return ("UNKNOWN");
	return (sd->name);
}

/*
 * Given a specific density number, return either the bits per inch or bits
 * per millimeter for the given density.
 */
int
mt_density_bp(int density_num, int bpi)
{
	struct densities *sd;

	for (sd = dens; sd->dens; sd++)
		if (sd->dens == density_num)
			break;
	if (sd->dens == 0)
		return (0);
	if (bpi)
		return (sd->bpi);
	else
		return (sd->bpmm);
}

int
mt_density_num(const char *density_name)
{
	struct densities *sd;
	size_t l = strlen(density_name);

	for (sd = dens; sd->dens; sd++)
		if (strncasecmp(sd->name, density_name, l) == 0)
			break;
	return (sd->dens);
}

/*
 * Get the current status XML string.
 * Returns 0 on success, -1 on failure (with errno set, and *xml_str == NULL).
 */
int
mt_get_xml_str(int mtfd, unsigned long cmd, char **xml_str)
{
	size_t alloc_len = 32768;
	struct mtextget extget;
	int error;

	*xml_str = NULL;

	for (;;) {
		bzero(&extget, sizeof(extget));
		*xml_str = malloc(alloc_len);
		if (*xml_str == NULL)
			return (-1);
		extget.status_xml = *xml_str;
		extget.alloc_len = alloc_len;

		error = ioctl(mtfd, cmd, (caddr_t)&extget);
		if (error == 0 && extget.status == MT_EXT_GET_OK)
			break;

		free(*xml_str);
		*xml_str = NULL;

		if (error != 0 || extget.status != MT_EXT_GET_NEED_MORE_SPACE)
			return (-1);

		/* The driver needs more space, so double and try again. */
		alloc_len *= 2;
	}
	return (0);
}

/*
 * Populate a struct mt_status_data from the XML string via mt_get_xml_str().
 *
 * Returns XML_STATUS_OK on success.
 * If XML_STATUS_ERROR is returned, errno may be set to indicate the reason.
 * The caller must check status_data->error.
 */
int
mt_get_status(char *xml_str, struct mt_status_data *status_data)
{
	XML_Parser parser;
	int retval;

	bzero(status_data, sizeof(*status_data));
	STAILQ_INIT(&status_data->entries);

	parser = XML_ParserCreate(NULL);
	if (parser == NULL) {
		errno = ENOMEM;
		return (XML_STATUS_ERROR);
	}

	XML_SetUserData(parser, status_data);
	XML_SetElementHandler(parser, mt_start_element, mt_end_element);
	XML_SetCharacterDataHandler(parser, mt_char_handler);

	retval = XML_Parse(parser, xml_str, strlen(xml_str), 1);
	XML_ParserFree(parser);
	return (retval);
}
