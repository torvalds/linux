#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/bootmem.h>

#include "check.h"
#include "mtdpart.h"

/* error message prefix */
#define ERRP "mtd: "

/* debug macro */
#if 0
#define dbg(x) do { printk("DEBUG-CMDLINE-PART: "); printk x; } while(0)
#else
#define dbg(x)
#endif

#define SECTOR_1G	0x200000	// 0x200000 * 512 = 1G
#define FROM_OFFSET	0x2000		// 4MB

/* special size referring to all the remaining space in a partition */
#define SIZE_REMAINING UINT_MAX
#define OFFSET_CONTINUOUS UINT_MAX

struct mtd_partition{
	char *name;
	sector_t from;
	sector_t size;
};
struct cmdline_mtd_partition {
	struct cmdline_mtd_partition *next;
	char *mtd_id;
	int num_parts;
	struct mtd_partition *parts;
};

/* mtdpart_setup() parses into here */
static struct cmdline_mtd_partition *partitions;

/* the command line passed to mtdpart_setupd() */
static char *cmdline;
static int cmdline_parsed = 0;

/*
 * Parse one partition definition for an MTD. Since there can be many
 * comma separated partition definitions, this function calls itself
 * recursively until no more partition definitions are found. Nice side
 * effect: the memory to keep the mtd_partition structs and the names
 * is allocated upon the last definition being found. At that point the
 * syntax has been verified ok.
 */
static struct mtd_partition * newpart(char *s,
                                      char **retptr,
                                      int *num_parts,
                                      int this_part,
                                      unsigned char **extra_mem_ptr,
                                      int extra_mem_size)
{
	struct mtd_partition *parts;
	sector_t size;
	sector_t from = OFFSET_CONTINUOUS;
	char *name;
	int name_len;
	unsigned char *extra_mem;
	char delim;

	/* fetch the partition size */
	if (*s == '-')
	{	/* assign all remaining space to this partition */
		size = SIZE_REMAINING;
		s++;
	}
	else
	{
		size = memparse(s, &s);
		if (size < PAGE_SIZE)
		{
			printk(KERN_ERR ERRP "partition size too small (%llx)\n", size);
			return NULL;
		}
	}

	/* fetch partition name */
	delim = 0;
        /* check for from */
        if (*s == '@')
	{
                s++;
                from = memparse(s, &s);
        }
        /* now look for name */
	if (*s == '(')
	{
		delim = ')';
	}

	if (delim)
	{
		char *p;

	    	name = ++s;
		p = strchr(name, delim);
		if (!p)
		{
			printk(KERN_ERR ERRP "no closing %c found in partition name\n", delim);
			return NULL;
		}
		name_len = p - name;
		s = p + 1;
	}
	else
	{
	    	name = NULL;
		name_len = 13; /* Partition_000 */
	}

	/* record name length for memory allocation later */
	extra_mem_size += name_len + 1;

	/* test if more partitions are following */
	if (*s == ',')
	{
		if (size == SIZE_REMAINING)
		{
			printk(KERN_ERR ERRP "no partitions allowed after a fill-up partition\n");
			return NULL;
		}
		/* more partitions follow, parse them */
		parts = newpart(s + 1, &s, num_parts, this_part + 1,
				&extra_mem, extra_mem_size);
		if (!parts)
			return NULL;
	}
	else
	{	/* this is the last partition: allocate space for all */
		int alloc_size;

		*num_parts = this_part + 1;
		alloc_size = *num_parts * sizeof(struct mtd_partition) +
			     extra_mem_size;
		parts = kzalloc(alloc_size, GFP_KERNEL);
		if (!parts)
		{
			printk(KERN_ERR ERRP "out of memory\n");
			return NULL;
		}
		extra_mem = (unsigned char *)(parts + *num_parts);
	}
	/* enter this partition (from will be calculated later if it is zero at this point) */
	parts[this_part].size = size;
	parts[this_part].from = from;
	if (name)
	{
		strlcpy(extra_mem, name, name_len + 1);
	}
	else
	{
		sprintf(extra_mem, "Partition_%03d", this_part);
	}
	parts[this_part].name = extra_mem;
	extra_mem += name_len + 1;

	dbg(("partition %d: name <%s>, from %llx, size %llx\n",
	     this_part,
	     parts[this_part].name,
	     parts[this_part].from,
	     parts[this_part].size));

	/* return (updated) pointer to extra_mem memory */
	if (extra_mem_ptr)
	  *extra_mem_ptr = extra_mem;

	/* return (updated) pointer command line string */
	*retptr = s;

	/* return partition table */
	return parts;
}

/*
 * Parse the command line.
 */
static int mtdpart_setup_real(char *s)
{
	cmdline_parsed = 1;

	for( ; s != NULL; )
	{
		struct cmdline_mtd_partition *this_mtd;
		struct mtd_partition *parts;
	    	int mtd_id_len;
		int num_parts;
		char *p, *mtd_id;

	    	mtd_id = s;
		/* fetch <mtd-id> */
		if (!(p = strchr(s, ':')))
		{
			dbg(( "no mtd-id\n"));
			return 0;
		}
		mtd_id_len = p - mtd_id;

		dbg(("parsing <%s>\n", p+1));

		/*
		 * parse one mtd. have it reserve memory for the
		 * struct cmdline_mtd_partition and the mtd-id string.
		 */
		parts = newpart(p + 1,		/* cmdline */
				&s,		/* out: updated cmdline ptr */
				&num_parts,	/* out: number of parts */
				0,		/* first partition */
				(unsigned char**)&this_mtd, /* out: extra mem */
				mtd_id_len + 1 + sizeof(*this_mtd) +
				sizeof(void*)-1 /*alignment*/);
		if(!parts)
		{
			/*
			 * An error occurred. We're either:
			 * a) out of memory, or
			 * b) in the middle of the partition spec
			 * Either way, this mtd is hosed and we're
			 * unlikely to succeed in parsing any more
			 */
			 return 0;
		 }

		/* align this_mtd */
		this_mtd = (struct cmdline_mtd_partition *)
			ALIGN((unsigned long)this_mtd, sizeof(void*));
		/* enter results */
		this_mtd->parts = parts;
		this_mtd->num_parts = num_parts;
		this_mtd->mtd_id = (char*)(this_mtd + 1);
		strlcpy(this_mtd->mtd_id, mtd_id, mtd_id_len + 1);

		/* link into chain */
		this_mtd->next = partitions;
		partitions = this_mtd;

		dbg(("mtdid=<%s> num_parts=<%d>\n",
		     this_mtd->mtd_id, this_mtd->num_parts));

		/* EOS - we're done */
		if (*s == 0)
			break;
#if 0
		/* does another spec follow? */
		if (*s != ';')
		{
			printk(KERN_ERR ERRP "bad character after partition (%c)\n", *s);
			return 0;
		}
#endif
		s++;
	}
	return 1;
}

/*
 * Main function to be called from the MTD mapping driver/device to
 * obtain the partitioning information. At this point the command line
 * arguments will actually be parsed and turned to struct mtd_partition
 * information. It returns partitions for the requested mtd device, or
 * the first one in the chain if a NULL mtd_id is passed in.
 */
static int parse_cmdline_partitions(sector_t n,
                             	    struct mtd_partition **pparts,
                             	    unsigned long origin)
{
	unsigned long from;
	int i;
	struct cmdline_mtd_partition *part;
	const char *mtd_id = "rk29xxnand";

	/* parse command line */
	if (!cmdline_parsed)
		mtdpart_setup_real(cmdline);

	for(part = partitions; part; part = part->next)
	{
		if ((!mtd_id) || (!strcmp(part->mtd_id, mtd_id)))
		{
			for(i = 0, from = 0; i < part->num_parts; i++)
			{
				if (part->parts[i].from == OFFSET_CONTINUOUS)
				  part->parts[i].from = from;
				else
				  from = part->parts[i].from;
				if (part->parts[i].size == SIZE_REMAINING)
				  part->parts[i].size = n - from - FROM_OFFSET;
				if (from + part->parts[i].size > n)
				{
					printk(KERN_WARNING ERRP
					       "%s: partitioning exceeds flash size, truncating\n",
					       part->mtd_id);
					part->parts[i].size = n - from;
					part->num_parts = i;
				}
				from += part->parts[i].size;
			}
			*pparts = kmemdup(part->parts,
					sizeof(*part->parts) * part->num_parts,
					GFP_KERNEL);
			if (!*pparts)
				return -ENOMEM;
			return part->num_parts;
		}
	}
	return 0;
}

static void rk_emmc_fix(void)
{
	const char mode_emmc[] = " androidboot.mode=emmc";
	const char charger_emmc[] = " androidboot.charger.emmc=1";
	char *new_command_line;
	size_t saved_command_line_len = strlen(saved_command_line);

	if (strstr(saved_command_line, "androidboot.mode=charger")) {
		new_command_line = kzalloc(saved_command_line_len + strlen(charger_emmc) + 1, GFP_KERNEL);
		sprintf(new_command_line, "%s%s", saved_command_line, charger_emmc);
	} else {
		new_command_line = kzalloc(saved_command_line_len + strlen(mode_emmc) + 1, GFP_KERNEL);
		sprintf(new_command_line, "%s%s", saved_command_line, mode_emmc);
	}
	saved_command_line = new_command_line;
}

int mtdpart_partition(struct parsed_partitions *state)
{
	int num_parts = 0, i;
	sector_t n = get_capacity(state->bdev->bd_disk);
	struct mtd_partition *parts = NULL;

	if(n < SECTOR_1G)
		return 0;

	if (!(state->bdev->bd_disk->flags & 2))
		return 0;

	cmdline = strstr(saved_command_line, "mtdparts=") + 9;
	
	num_parts = parse_cmdline_partitions(n, &parts, 0);
	if(num_parts < 0)
		return num_parts;

	for(i = 0; i < num_parts; i++){
		put_partition(state, i+1, parts[i].from + FROM_OFFSET, parts[i].size);
		strcpy(state->parts[i+1].info.volname, parts[i].name);
		printk(KERN_INFO "%10s: 0x%09llx -- 0x%09llx (%llu MB)\n", 
				parts[i].name,
				parts[i].from * 512,
				(parts[i].from + parts[i].size) * 512,
				parts[i].size / 2048);
	}

	rk_emmc_fix();

	return 1;
}


