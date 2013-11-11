#ifndef _LINUX_SOM_H
#define _LINUX_SOM_H

/* File format definition for SOM executables / shared libraries */

/* we need struct timespec */
#include <linux/time.h>

#define SOM_PAGESIZE 4096

/* this is the SOM header */
struct som_hdr {
	short		system_id;		/* magic number - system */
	short		a_magic;		/* magic number - file type */
	unsigned int	version_id;		/* versiod ID: YYMMDDHH */
	struct timespec	file_time;		/* system clock */
	unsigned int	entry_space;		/* space for entry point */
	unsigned int	entry_subspace;		/* subspace for entry point */
	unsigned int	entry_offset;		/* offset of entry point */
	unsigned int	aux_header_location;	/* auxiliary header location */
	unsigned int	aux_header_size;	/* auxiliary header size */
	unsigned int	som_length;		/* length of entire SOM */
	unsigned int	presumed_dp;		/* compiler's DP value */
	unsigned int	space_location;		/* space dictionary location */
	unsigned int	space_total;		/* number of space entries */
	unsigned int	subspace_location;	/* subspace entries location */
	unsigned int	subspace_total;		/* number of subspace entries */
	unsigned int	loader_fixup_location;	/* MPE/iX loader fixup */
	unsigned int	loader_fixup_total;	/* number of fixup records */
	unsigned int	space_strings_location;	/* (sub)space names */
	unsigned int	space_strings_size;	/* size of strings area */
	unsigned int	init_array_location;	/* reserved */
	unsigned int	init_array_total;	/* reserved */
	unsigned int	compiler_location;	/* module dictionary */
	unsigned int	compiler_total;		/* number of modules */
	unsigned int	symbol_location;	/* symbol dictionary */
	unsigned int	symbol_total;		/* number of symbols */
	unsigned int	fixup_request_location;	/* fixup requests */
	unsigned int	fixup_request_total;	/* number of fixup requests */
	unsigned int	symbol_strings_location;/* module & symbol names area */
	unsigned int	symbol_strings_size;	/* size of strings area */
	unsigned int	unloadable_sp_location;	/* unloadable spaces location */
	unsigned int	unloadable_sp_size;	/* size of data */
	unsigned int	checksum;
};

/* values for system_id */

#define SOM_SID_PARISC_1_0	0x020b
#define SOM_SID_PARISC_1_1	0x0210
#define SOM_SID_PARISC_2_0	0x0214

/* values for a_magic */

#define SOM_LIB_EXEC		0x0104
#define SOM_RELOCATABLE		0x0106
#define SOM_EXEC_NONSHARE	0x0107
#define SOM_EXEC_SHARE		0x0108
#define SOM_EXEC_DEMAND		0x010B
#define SOM_LIB_DYN		0x010D
#define SOM_LIB_SHARE		0x010E
#define SOM_LIB_RELOC		0x0619

/* values for version_id.  Decimal not hex, yes.  Grr. */

#define SOM_ID_OLD		85082112
#define SOM_ID_NEW		87102412

struct aux_id {
	unsigned int	mandatory :1;	/* the linker must understand this */
	unsigned int	copy	  :1;	/* Must be copied by the linker */
	unsigned int	append	  :1;	/* Must be merged by the linker */
	unsigned int	ignore	  :1;	/* Discard section if unknown */
	unsigned int	reserved  :12;
	unsigned int	type	  :16;	/* Header type */
	unsigned int	length;		/* length of _following_ data */
};

/* The Exec Auxiliary Header.  Called The HP-UX Header within HP apparently. */
struct som_exec_auxhdr {
	struct aux_id	som_auxhdr;
	int		exec_tsize;	/* Text size in bytes */
	int		exec_tmem;	/* Address to load text at */
	int		exec_tfile;	/* Location of text in file */
	int		exec_dsize;	/* Data size in bytes */
	int		exec_dmem;	/* Address to load data at */
	int		exec_dfile;	/* Location of data in file */
	int		exec_bsize;	/* Uninitialised data (bss) */
	int		exec_entry;	/* Address to start executing */
	int		exec_flags;	/* loader flags */
	int		exec_bfill;	/* initialisation value for bss */
};

/* Oh, the things people do to avoid casts.  Shame it'll break with gcc's
 * new aliasing rules really.
 */
union name_pt {
	char *		n_name;
	unsigned int	n_strx;
};

/* The Space Dictionary */
struct space_dictionary_record {
	union name_pt	name;			/* index to subspace name */
	unsigned int	is_loadable	:1;	/* loadable */
	unsigned int	is_defined	:1;	/* defined within file */
	unsigned int	is_private	:1;	/* not sharable */
	unsigned int	has_intermediate_code :1; /* contains intermediate code */
	unsigned int	is_tspecific	:1;	/* thread specific */
	unsigned int	reserved	:11;	/* for future expansion */
	unsigned int	sort_key	:8;	/* for linker */
	unsigned int	reserved2	:8;	/* for future expansion */

	int		space_number;		/* index */
	int		subspace_index;		/* index into subspace dict */
	unsigned int	subspace_quantity;	/* number of subspaces */
	int		loader_fix_index;	/* for loader */
	unsigned int	loader_fix_quantity;	/* for loader */
	int		init_pointer_index;	/* data pointer array index */
	unsigned int	init_pointer_quantity;	/* number of data pointers */
};

/* The Subspace Dictionary */
struct subspace_dictionary_record {
	int		space_index;
	unsigned int	access_control_bits :7;
	unsigned int	memory_resident	:1;
	unsigned int	dup_common	:1;
	unsigned int	is_common	:1;
	unsigned int	quadrant	:2;
	unsigned int	initially_frozen :1;
	unsigned int	is_first	:1;
	unsigned int	code_only	:1;
	unsigned int	sort_key	:8;
	unsigned int	replicate_init	:1;
	unsigned int	continuation	:1;
	unsigned int	is_tspecific	:1;
	unsigned int	is_comdat	:1;
	unsigned int	reserved	:4;

	int		file_loc_init_value;
	unsigned int	initialization_length;
	unsigned int	subspace_start;
	unsigned int	subspace_length;

	unsigned int	reserved2	:5;
	unsigned int	alignment	:27;

	union name_pt	name;
	int		fixup_request_index;
	unsigned int	fixup_request_quantity;
};

#endif /* _LINUX_SOM_H */
