/*
 * Generic EDAC defs
 *
 * Author: Dave Jiang <djiang@mvista.com>
 *
 * 2006-2008 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */
#ifndef _LINUX_EDAC_H_
#define _LINUX_EDAC_H_

#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/numa.h>

#define EDAC_DEVICE_NAME_LEN	31

struct device;

#define EDAC_OPSTATE_INVAL	-1
#define EDAC_OPSTATE_POLL	0
#define EDAC_OPSTATE_NMI	1
#define EDAC_OPSTATE_INT	2

extern int edac_op_state;

struct bus_type *edac_get_sysfs_subsys(void);
int edac_get_report_status(void);
void edac_set_report_status(int new);

enum {
	EDAC_REPORTING_ENABLED,
	EDAC_REPORTING_DISABLED,
	EDAC_REPORTING_FORCE
};

static inline void opstate_init(void)
{
	switch (edac_op_state) {
	case EDAC_OPSTATE_POLL:
	case EDAC_OPSTATE_NMI:
		break;
	default:
		edac_op_state = EDAC_OPSTATE_POLL;
	}
	return;
}

/* Max length of a DIMM label*/
#define EDAC_MC_LABEL_LEN	31

/* Maximum size of the location string */
#define LOCATION_SIZE 256

/* Defines the maximum number of labels that can be reported */
#define EDAC_MAX_LABELS		8

/* String used to join two or more labels */
#define OTHER_LABEL " or "

/**
 * enum dev_type - describe the type of memory DRAM chips used at the stick
 * @DEV_UNKNOWN:	Can't be determined, or MC doesn't support detect it
 * @DEV_X1:		1 bit for data
 * @DEV_X2:		2 bits for data
 * @DEV_X4:		4 bits for data
 * @DEV_X8:		8 bits for data
 * @DEV_X16:		16 bits for data
 * @DEV_X32:		32 bits for data
 * @DEV_X64:		64 bits for data
 *
 * Typical values are x4 and x8.
 */
enum dev_type {
	DEV_UNKNOWN = 0,
	DEV_X1,
	DEV_X2,
	DEV_X4,
	DEV_X8,
	DEV_X16,
	DEV_X32,		/* Do these parts exist? */
	DEV_X64			/* Do these parts exist? */
};

#define DEV_FLAG_UNKNOWN	BIT(DEV_UNKNOWN)
#define DEV_FLAG_X1		BIT(DEV_X1)
#define DEV_FLAG_X2		BIT(DEV_X2)
#define DEV_FLAG_X4		BIT(DEV_X4)
#define DEV_FLAG_X8		BIT(DEV_X8)
#define DEV_FLAG_X16		BIT(DEV_X16)
#define DEV_FLAG_X32		BIT(DEV_X32)
#define DEV_FLAG_X64		BIT(DEV_X64)

/**
 * enum hw_event_mc_err_type - type of the detected error
 *
 * @HW_EVENT_ERR_CORRECTED:	Corrected Error - Indicates that an ECC
 *				corrected error was detected
 * @HW_EVENT_ERR_UNCORRECTED:	Uncorrected Error - Indicates an error that
 *				can't be corrected by ECC, but it is not
 *				fatal (maybe it is on an unused memory area,
 *				or the memory controller could recover from
 *				it for example, by re-trying the operation).
 * @HW_EVENT_ERR_DEFERRED:	Deferred Error - Indicates an uncorrectable
 *				error whose handling is not urgent. This could
 *				be due to hardware data poisoning where the
 *				system can continue operation until the poisoned
 *				data is consumed. Preemptive measures may also
 *				be taken, e.g. offlining pages, etc.
 * @HW_EVENT_ERR_FATAL:		Fatal Error - Uncorrected error that could not
 *				be recovered.
 * @HW_EVENT_ERR_INFO:		Informational - The CPER spec defines a forth
 *				type of error: informational logs.
 */
enum hw_event_mc_err_type {
	HW_EVENT_ERR_CORRECTED,
	HW_EVENT_ERR_UNCORRECTED,
	HW_EVENT_ERR_DEFERRED,
	HW_EVENT_ERR_FATAL,
	HW_EVENT_ERR_INFO,
};

static inline char *mc_event_error_type(const unsigned int err_type)
{
	switch (err_type) {
	case HW_EVENT_ERR_CORRECTED:
		return "Corrected";
	case HW_EVENT_ERR_UNCORRECTED:
		return "Uncorrected";
	case HW_EVENT_ERR_DEFERRED:
		return "Deferred";
	case HW_EVENT_ERR_FATAL:
		return "Fatal";
	default:
	case HW_EVENT_ERR_INFO:
		return "Info";
	}
}

/**
 * enum mem_type - memory types. For a more detailed reference, please see
 *			http://en.wikipedia.org/wiki/DRAM
 *
 * @MEM_EMPTY:		Empty csrow
 * @MEM_RESERVED:	Reserved csrow type
 * @MEM_UNKNOWN:	Unknown csrow type
 * @MEM_FPM:		FPM - Fast Page Mode, used on systems up to 1995.
 * @MEM_EDO:		EDO - Extended data out, used on systems up to 1998.
 * @MEM_BEDO:		BEDO - Burst Extended data out, an EDO variant.
 * @MEM_SDR:		SDR - Single data rate SDRAM
 *			http://en.wikipedia.org/wiki/Synchronous_dynamic_random-access_memory
 *			They use 3 pins for chip select: Pins 0 and 2 are
 *			for rank 0; pins 1 and 3 are for rank 1, if the memory
 *			is dual-rank.
 * @MEM_RDR:		Registered SDR SDRAM
 * @MEM_DDR:		Double data rate SDRAM
 *			http://en.wikipedia.org/wiki/DDR_SDRAM
 * @MEM_RDDR:		Registered Double data rate SDRAM
 *			This is a variant of the DDR memories.
 *			A registered memory has a buffer inside it, hiding
 *			part of the memory details to the memory controller.
 * @MEM_RMBS:		Rambus DRAM, used on a few Pentium III/IV controllers.
 * @MEM_DDR2:		DDR2 RAM, as described at JEDEC JESD79-2F.
 *			Those memories are labeled as "PC2-" instead of "PC" to
 *			differentiate from DDR.
 * @MEM_FB_DDR2:	Fully-Buffered DDR2, as described at JEDEC Std No. 205
 *			and JESD206.
 *			Those memories are accessed per DIMM slot, and not by
 *			a chip select signal.
 * @MEM_RDDR2:		Registered DDR2 RAM
 *			This is a variant of the DDR2 memories.
 * @MEM_XDR:		Rambus XDR
 *			It is an evolution of the original RAMBUS memories,
 *			created to compete with DDR2. Weren't used on any
 *			x86 arch, but cell_edac PPC memory controller uses it.
 * @MEM_DDR3:		DDR3 RAM
 * @MEM_RDDR3:		Registered DDR3 RAM
 *			This is a variant of the DDR3 memories.
 * @MEM_LRDDR3:		Load-Reduced DDR3 memory.
 * @MEM_DDR4:		Unbuffered DDR4 RAM
 * @MEM_RDDR4:		Registered DDR4 RAM
 *			This is a variant of the DDR4 memories.
 * @MEM_LRDDR4:		Load-Reduced DDR4 memory.
 * @MEM_NVDIMM:		Non-volatile RAM
 */
enum mem_type {
	MEM_EMPTY = 0,
	MEM_RESERVED,
	MEM_UNKNOWN,
	MEM_FPM,
	MEM_EDO,
	MEM_BEDO,
	MEM_SDR,
	MEM_RDR,
	MEM_DDR,
	MEM_RDDR,
	MEM_RMBS,
	MEM_DDR2,
	MEM_FB_DDR2,
	MEM_RDDR2,
	MEM_XDR,
	MEM_DDR3,
	MEM_RDDR3,
	MEM_LRDDR3,
	MEM_DDR4,
	MEM_RDDR4,
	MEM_LRDDR4,
	MEM_NVDIMM,
};

#define MEM_FLAG_EMPTY		BIT(MEM_EMPTY)
#define MEM_FLAG_RESERVED	BIT(MEM_RESERVED)
#define MEM_FLAG_UNKNOWN	BIT(MEM_UNKNOWN)
#define MEM_FLAG_FPM		BIT(MEM_FPM)
#define MEM_FLAG_EDO		BIT(MEM_EDO)
#define MEM_FLAG_BEDO		BIT(MEM_BEDO)
#define MEM_FLAG_SDR		BIT(MEM_SDR)
#define MEM_FLAG_RDR		BIT(MEM_RDR)
#define MEM_FLAG_DDR		BIT(MEM_DDR)
#define MEM_FLAG_RDDR		BIT(MEM_RDDR)
#define MEM_FLAG_RMBS		BIT(MEM_RMBS)
#define MEM_FLAG_DDR2           BIT(MEM_DDR2)
#define MEM_FLAG_FB_DDR2        BIT(MEM_FB_DDR2)
#define MEM_FLAG_RDDR2          BIT(MEM_RDDR2)
#define MEM_FLAG_XDR            BIT(MEM_XDR)
#define MEM_FLAG_DDR3           BIT(MEM_DDR3)
#define MEM_FLAG_RDDR3          BIT(MEM_RDDR3)
#define MEM_FLAG_DDR4           BIT(MEM_DDR4)
#define MEM_FLAG_RDDR4          BIT(MEM_RDDR4)
#define MEM_FLAG_LRDDR4         BIT(MEM_LRDDR4)
#define MEM_FLAG_NVDIMM         BIT(MEM_NVDIMM)

/**
 * enum edac-type - Error Detection and Correction capabilities and mode
 * @EDAC_UNKNOWN:	Unknown if ECC is available
 * @EDAC_NONE:		Doesn't support ECC
 * @EDAC_RESERVED:	Reserved ECC type
 * @EDAC_PARITY:	Detects parity errors
 * @EDAC_EC:		Error Checking - no correction
 * @EDAC_SECDED:	Single bit error correction, Double detection
 * @EDAC_S2ECD2ED:	Chipkill x2 devices - do these exist?
 * @EDAC_S4ECD4ED:	Chipkill x4 devices
 * @EDAC_S8ECD8ED:	Chipkill x8 devices
 * @EDAC_S16ECD16ED:	Chipkill x16 devices
 */
enum edac_type {
	EDAC_UNKNOWN =	0,
	EDAC_NONE,
	EDAC_RESERVED,
	EDAC_PARITY,
	EDAC_EC,
	EDAC_SECDED,
	EDAC_S2ECD2ED,
	EDAC_S4ECD4ED,
	EDAC_S8ECD8ED,
	EDAC_S16ECD16ED,
};

#define EDAC_FLAG_UNKNOWN	BIT(EDAC_UNKNOWN)
#define EDAC_FLAG_NONE		BIT(EDAC_NONE)
#define EDAC_FLAG_PARITY	BIT(EDAC_PARITY)
#define EDAC_FLAG_EC		BIT(EDAC_EC)
#define EDAC_FLAG_SECDED	BIT(EDAC_SECDED)
#define EDAC_FLAG_S2ECD2ED	BIT(EDAC_S2ECD2ED)
#define EDAC_FLAG_S4ECD4ED	BIT(EDAC_S4ECD4ED)
#define EDAC_FLAG_S8ECD8ED	BIT(EDAC_S8ECD8ED)
#define EDAC_FLAG_S16ECD16ED	BIT(EDAC_S16ECD16ED)

/**
 * enum scrub_type - scrubbing capabilities
 * @SCRUB_UNKNOWN:		Unknown if scrubber is available
 * @SCRUB_NONE:			No scrubber
 * @SCRUB_SW_PROG:		SW progressive (sequential) scrubbing
 * @SCRUB_SW_SRC:		Software scrub only errors
 * @SCRUB_SW_PROG_SRC:		Progressive software scrub from an error
 * @SCRUB_SW_TUNABLE:		Software scrub frequency is tunable
 * @SCRUB_HW_PROG:		HW progressive (sequential) scrubbing
 * @SCRUB_HW_SRC:		Hardware scrub only errors
 * @SCRUB_HW_PROG_SRC:		Progressive hardware scrub from an error
 * @SCRUB_HW_TUNABLE:		Hardware scrub frequency is tunable
 */
enum scrub_type {
	SCRUB_UNKNOWN =	0,
	SCRUB_NONE,
	SCRUB_SW_PROG,
	SCRUB_SW_SRC,
	SCRUB_SW_PROG_SRC,
	SCRUB_SW_TUNABLE,
	SCRUB_HW_PROG,
	SCRUB_HW_SRC,
	SCRUB_HW_PROG_SRC,
	SCRUB_HW_TUNABLE
};

#define SCRUB_FLAG_SW_PROG	BIT(SCRUB_SW_PROG)
#define SCRUB_FLAG_SW_SRC	BIT(SCRUB_SW_SRC)
#define SCRUB_FLAG_SW_PROG_SRC	BIT(SCRUB_SW_PROG_SRC)
#define SCRUB_FLAG_SW_TUN	BIT(SCRUB_SW_SCRUB_TUNABLE)
#define SCRUB_FLAG_HW_PROG	BIT(SCRUB_HW_PROG)
#define SCRUB_FLAG_HW_SRC	BIT(SCRUB_HW_SRC)
#define SCRUB_FLAG_HW_PROG_SRC	BIT(SCRUB_HW_PROG_SRC)
#define SCRUB_FLAG_HW_TUN	BIT(SCRUB_HW_TUNABLE)

/* FIXME - should have notify capabilities: NMI, LOG, PROC, etc */

/* EDAC internal operation states */
#define	OP_ALLOC		0x100
#define OP_RUNNING_POLL		0x201
#define OP_RUNNING_INTERRUPT	0x202
#define OP_RUNNING_POLL_INTR	0x203
#define OP_OFFLINE		0x300

/**
 * enum edac_mc_layer - memory controller hierarchy layer
 *
 * @EDAC_MC_LAYER_BRANCH:	memory layer is named "branch"
 * @EDAC_MC_LAYER_CHANNEL:	memory layer is named "channel"
 * @EDAC_MC_LAYER_SLOT:		memory layer is named "slot"
 * @EDAC_MC_LAYER_CHIP_SELECT:	memory layer is named "chip select"
 * @EDAC_MC_LAYER_ALL_MEM:	memory layout is unknown. All memory is mapped
 *				as a single memory area. This is used when
 *				retrieving errors from a firmware driven driver.
 *
 * This enum is used by the drivers to tell edac_mc_sysfs what name should
 * be used when describing a memory stick location.
 */
enum edac_mc_layer_type {
	EDAC_MC_LAYER_BRANCH,
	EDAC_MC_LAYER_CHANNEL,
	EDAC_MC_LAYER_SLOT,
	EDAC_MC_LAYER_CHIP_SELECT,
	EDAC_MC_LAYER_ALL_MEM,
};

/**
 * struct edac_mc_layer - describes the memory controller hierarchy
 * @type:		layer type
 * @size:		number of components per layer. For example,
 *			if the channel layer has two channels, size = 2
 * @is_virt_csrow:	This layer is part of the "csrow" when old API
 *			compatibility mode is enabled. Otherwise, it is
 *			a channel
 */
struct edac_mc_layer {
	enum edac_mc_layer_type	type;
	unsigned		size;
	bool			is_virt_csrow;
};

/*
 * Maximum number of layers used by the memory controller to uniquely
 * identify a single memory stick.
 * NOTE: Changing this constant requires not only to change the constant
 * below, but also to change the existing code at the core, as there are
 * some code there that are optimized for 3 layers.
 */
#define EDAC_MAX_LAYERS		3

/**
 * EDAC_DIMM_OFF - Macro responsible to get a pointer offset inside a pointer
 *		   array for the element given by [layer0,layer1,layer2]
 *		   position
 *
 * @layers:	a struct edac_mc_layer array, describing how many elements
 *		were allocated for each layer
 * @nlayers:	Number of layers at the @layers array
 * @layer0:	layer0 position
 * @layer1:	layer1 position. Unused if n_layers < 2
 * @layer2:	layer2 position. Unused if n_layers < 3
 *
 * For 1 layer, this macro returns "var[layer0] - var";
 *
 * For 2 layers, this macro is similar to allocate a bi-dimensional array
 * and to return "var[layer0][layer1] - var";
 *
 * For 3 layers, this macro is similar to allocate a tri-dimensional array
 * and to return "var[layer0][layer1][layer2] - var".
 *
 * A loop could be used here to make it more generic, but, as we only have
 * 3 layers, this is a little faster.
 *
 * By design, layers can never be 0 or more than 3. If that ever happens,
 * a NULL is returned, causing an OOPS during the memory allocation routine,
 * with would point to the developer that he's doing something wrong.
 */
#define EDAC_DIMM_OFF(layers, nlayers, layer0, layer1, layer2) ({		\
	int __i;							\
	if ((nlayers) == 1)						\
		__i = layer0;						\
	else if ((nlayers) == 2)					\
		__i = (layer1) + ((layers[1]).size * (layer0));		\
	else if ((nlayers) == 3)					\
		__i = (layer2) + ((layers[2]).size * ((layer1) +	\
			    ((layers[1]).size * (layer0))));		\
	else								\
		__i = -EINVAL;						\
	__i;								\
})

/**
 * EDAC_DIMM_PTR - Macro responsible to get a pointer inside a pointer array
 *		   for the element given by [layer0,layer1,layer2] position
 *
 * @layers:	a struct edac_mc_layer array, describing how many elements
 *		were allocated for each layer
 * @var:	name of the var where we want to get the pointer
 *		(like mci->dimms)
 * @nlayers:	Number of layers at the @layers array
 * @layer0:	layer0 position
 * @layer1:	layer1 position. Unused if n_layers < 2
 * @layer2:	layer2 position. Unused if n_layers < 3
 *
 * For 1 layer, this macro returns "var[layer0]";
 *
 * For 2 layers, this macro is similar to allocate a bi-dimensional array
 * and to return "var[layer0][layer1]";
 *
 * For 3 layers, this macro is similar to allocate a tri-dimensional array
 * and to return "var[layer0][layer1][layer2]";
 */
#define EDAC_DIMM_PTR(layers, var, nlayers, layer0, layer1, layer2) ({	\
	typeof(*var) __p;						\
	int ___i = EDAC_DIMM_OFF(layers, nlayers, layer0, layer1, layer2);	\
	if (___i < 0)							\
		__p = NULL;						\
	else								\
		__p = (var)[___i];					\
	__p;								\
})

struct dimm_info {
	struct device dev;

	char label[EDAC_MC_LABEL_LEN + 1];	/* DIMM label on motherboard */

	/* Memory location data */
	unsigned int location[EDAC_MAX_LAYERS];

	struct mem_ctl_info *mci;	/* the parent */

	u32 grain;		/* granularity of reported error in bytes */
	enum dev_type dtype;	/* memory device type */
	enum mem_type mtype;	/* memory dimm type */
	enum edac_type edac_mode;	/* EDAC mode for this dimm */

	u32 nr_pages;			/* number of pages on this dimm */

	unsigned int csrow, cschannel;	/* Points to the old API data */

	u16 smbios_handle;              /* Handle for SMBIOS type 17 */
};

/**
 * struct rank_info - contains the information for one DIMM rank
 *
 * @chan_idx:	channel number where the rank is (typically, 0 or 1)
 * @ce_count:	number of correctable errors for this rank
 * @csrow:	A pointer to the chip select row structure (the parent
 *		structure). The location of the rank is given by
 *		the (csrow->csrow_idx, chan_idx) vector.
 * @dimm:	A pointer to the DIMM structure, where the DIMM label
 *		information is stored.
 *
 * FIXME: Currently, the EDAC core model will assume one DIMM per rank.
 *	  This is a bad assumption, but it makes this patch easier. Later
 *	  patches in this series will fix this issue.
 */
struct rank_info {
	int chan_idx;
	struct csrow_info *csrow;
	struct dimm_info *dimm;

	u32 ce_count;		/* Correctable Errors for this csrow */
};

struct csrow_info {
	struct device dev;

	/* Used only by edac_mc_find_csrow_by_page() */
	unsigned long first_page;	/* first page number in csrow */
	unsigned long last_page;	/* last page number in csrow */
	unsigned long page_mask;	/* used for interleaving -
					 * 0UL for non intlv */

	int csrow_idx;			/* the chip-select row */

	u32 ue_count;		/* Uncorrectable Errors for this csrow */
	u32 ce_count;		/* Correctable Errors for this csrow */

	struct mem_ctl_info *mci;	/* the parent */

	/* channel information for this csrow */
	u32 nr_channels;
	struct rank_info **channels;
};

/*
 * struct errcount_attribute - used to store the several error counts
 */
struct errcount_attribute_data {
	int n_layers;
	int pos[EDAC_MAX_LAYERS];
	int layer0, layer1, layer2;
};

/**
 * struct edac_raw_error_desc - Raw error report structure
 * @grain:			minimum granularity for an error report, in bytes
 * @error_count:		number of errors of the same type
 * @top_layer:			top layer of the error (layer[0])
 * @mid_layer:			middle layer of the error (layer[1])
 * @low_layer:			low layer of the error (layer[2])
 * @page_frame_number:		page where the error happened
 * @offset_in_page:		page offset
 * @syndrome:			syndrome of the error (or 0 if unknown or if
 * 				the syndrome is not applicable)
 * @msg:			error message
 * @location:			location of the error
 * @label:			label of the affected DIMM(s)
 * @other_detail:		other driver-specific detail about the error
 * @enable_per_layer_report:	if false, the error affects all layers
 *				(typically, a memory controller error)
 */
struct edac_raw_error_desc {
	/*
	 * NOTE: everything before grain won't be cleaned by
	 * edac_raw_error_desc_clean()
	 */
	char location[LOCATION_SIZE];
	char label[(EDAC_MC_LABEL_LEN + 1 + sizeof(OTHER_LABEL)) * EDAC_MAX_LABELS];
	long grain;

	/* the vars below and grain will be cleaned on every new error report */
	u16 error_count;
	int top_layer;
	int mid_layer;
	int low_layer;
	unsigned long page_frame_number;
	unsigned long offset_in_page;
	unsigned long syndrome;
	const char *msg;
	const char *other_detail;
	bool enable_per_layer_report;
};

/* MEMORY controller information structure
 */
struct mem_ctl_info {
	struct device			dev;
	struct bus_type			*bus;

	struct list_head link;	/* for global list of mem_ctl_info structs */

	struct module *owner;	/* Module owner of this control struct */

	unsigned long mtype_cap;	/* memory types supported by mc */
	unsigned long edac_ctl_cap;	/* Mem controller EDAC capabilities */
	unsigned long edac_cap;	/* configuration capabilities - this is
				 * closely related to edac_ctl_cap.  The
				 * difference is that the controller may be
				 * capable of s4ecd4ed which would be listed
				 * in edac_ctl_cap, but if channels aren't
				 * capable of s4ecd4ed then the edac_cap would
				 * not have that capability.
				 */
	unsigned long scrub_cap;	/* chipset scrub capabilities */
	enum scrub_type scrub_mode;	/* current scrub mode */

	/* Translates sdram memory scrub rate given in bytes/sec to the
	   internal representation and configures whatever else needs
	   to be configured.
	 */
	int (*set_sdram_scrub_rate) (struct mem_ctl_info * mci, u32 bw);

	/* Get the current sdram memory scrub rate from the internal
	   representation and converts it to the closest matching
	   bandwidth in bytes/sec.
	 */
	int (*get_sdram_scrub_rate) (struct mem_ctl_info * mci);


	/* pointer to edac checking routine */
	void (*edac_check) (struct mem_ctl_info * mci);

	/*
	 * Remaps memory pages: controller pages to physical pages.
	 * For most MC's, this will be NULL.
	 */
	/* FIXME - why not send the phys page to begin with? */
	unsigned long (*ctl_page_to_phys) (struct mem_ctl_info * mci,
					   unsigned long page);
	int mc_idx;
	struct csrow_info **csrows;
	unsigned int nr_csrows, num_cschannel;

	/*
	 * Memory Controller hierarchy
	 *
	 * There are basically two types of memory controller: the ones that
	 * sees memory sticks ("dimms"), and the ones that sees memory ranks.
	 * All old memory controllers enumerate memories per rank, but most
	 * of the recent drivers enumerate memories per DIMM, instead.
	 * When the memory controller is per rank, csbased is true.
	 */
	unsigned int n_layers;
	struct edac_mc_layer *layers;
	bool csbased;

	/*
	 * DIMM info. Will eventually remove the entire csrows_info some day
	 */
	unsigned int tot_dimms;
	struct dimm_info **dimms;

	/*
	 * FIXME - what about controllers on other busses? - IDs must be
	 * unique.  dev pointer should be sufficiently unique, but
	 * BUS:SLOT.FUNC numbers may not be unique.
	 */
	struct device *pdev;
	const char *mod_name;
	const char *ctl_name;
	const char *dev_name;
	void *pvt_info;
	unsigned long start_time;	/* mci load start time (in jiffies) */

	/*
	 * drivers shouldn't access those fields directly, as the core
	 * already handles that.
	 */
	u32 ce_noinfo_count, ue_noinfo_count;
	u32 ue_mc, ce_mc;
	u32 *ce_per_layer[EDAC_MAX_LAYERS], *ue_per_layer[EDAC_MAX_LAYERS];

	struct completion complete;

	/* Additional top controller level attributes, but specified
	 * by the low level driver.
	 *
	 * Set by the low level driver to provide attributes at the
	 * controller level.
	 * An array of structures, NULL terminated
	 *
	 * If attributes are desired, then set to array of attributes
	 * If no attributes are desired, leave NULL
	 */
	const struct mcidev_sysfs_attribute *mc_driver_sysfs_attributes;

	/* work struct for this MC */
	struct delayed_work work;

	/*
	 * Used to report an error - by being at the global struct
	 * makes the memory allocated by the EDAC core
	 */
	struct edac_raw_error_desc error_desc;

	/* the internal state of this controller instance */
	int op_state;

	struct dentry *debugfs;
	u8 fake_inject_layer[EDAC_MAX_LAYERS];
	bool fake_inject_ue;
	u16 fake_inject_count;
};
#endif
