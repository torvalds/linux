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

const struct bus_type *edac_get_sysfs_subsys(void);

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
 * @MEM_LPDDR3:		Low-Power DDR3 memory.
 * @MEM_DDR4:		Unbuffered DDR4 RAM
 * @MEM_RDDR4:		Registered DDR4 RAM
 *			This is a variant of the DDR4 memories.
 * @MEM_LRDDR4:		Load-Reduced DDR4 memory.
 * @MEM_LPDDR4:		Low-Power DDR4 memory.
 * @MEM_DDR5:		Unbuffered DDR5 RAM
 * @MEM_RDDR5:		Registered DDR5 RAM
 * @MEM_LRDDR5:		Load-Reduced DDR5 memory.
 * @MEM_NVDIMM:		Non-volatile RAM
 * @MEM_WIO2:		Wide I/O 2.
 * @MEM_HBM2:		High bandwidth Memory Gen 2.
 * @MEM_HBM3:		High bandwidth Memory Gen 3.
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
	MEM_LPDDR3,
	MEM_DDR4,
	MEM_RDDR4,
	MEM_LRDDR4,
	MEM_LPDDR4,
	MEM_DDR5,
	MEM_RDDR5,
	MEM_LRDDR5,
	MEM_NVDIMM,
	MEM_WIO2,
	MEM_HBM2,
	MEM_HBM3,
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
#define MEM_FLAG_DDR2		BIT(MEM_DDR2)
#define MEM_FLAG_FB_DDR2	BIT(MEM_FB_DDR2)
#define MEM_FLAG_RDDR2		BIT(MEM_RDDR2)
#define MEM_FLAG_XDR		BIT(MEM_XDR)
#define MEM_FLAG_DDR3		BIT(MEM_DDR3)
#define MEM_FLAG_RDDR3		BIT(MEM_RDDR3)
#define MEM_FLAG_LPDDR3		BIT(MEM_LPDDR3)
#define MEM_FLAG_DDR4		BIT(MEM_DDR4)
#define MEM_FLAG_RDDR4		BIT(MEM_RDDR4)
#define MEM_FLAG_LRDDR4		BIT(MEM_LRDDR4)
#define MEM_FLAG_LPDDR4		BIT(MEM_LPDDR4)
#define MEM_FLAG_DDR5		BIT(MEM_DDR5)
#define MEM_FLAG_RDDR5		BIT(MEM_RDDR5)
#define MEM_FLAG_LRDDR5		BIT(MEM_LRDDR5)
#define MEM_FLAG_NVDIMM		BIT(MEM_NVDIMM)
#define MEM_FLAG_WIO2		BIT(MEM_WIO2)
#define MEM_FLAG_HBM2		BIT(MEM_HBM2)
#define MEM_FLAG_HBM3		BIT(MEM_HBM3)

/**
 * enum edac_type - Error Detection and Correction capabilities and mode
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
 * enum edac_mc_layer_type - memory controller hierarchy layer
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

struct dimm_info {
	struct device dev;

	char label[EDAC_MC_LABEL_LEN + 1];	/* DIMM label on motherboard */

	/* Memory location data */
	unsigned int location[EDAC_MAX_LAYERS];

	struct mem_ctl_info *mci;	/* the parent */
	unsigned int idx;		/* index within the parent dimm array */

	u32 grain;		/* granularity of reported error in bytes */
	enum dev_type dtype;	/* memory device type */
	enum mem_type mtype;	/* memory dimm type */
	enum edac_type edac_mode;	/* EDAC mode for this dimm */

	u32 nr_pages;			/* number of pages on this dimm */

	unsigned int csrow, cschannel;	/* Points to the old API data */

	u16 smbios_handle;              /* Handle for SMBIOS type 17 */

	u32 ce_count;
	u32 ue_count;
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
 * @type:			severity of the error (CE/UE/Fatal)
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
 */
struct edac_raw_error_desc {
	char location[LOCATION_SIZE];
	char label[(EDAC_MC_LABEL_LEN + 1 + sizeof(OTHER_LABEL)) * EDAC_MAX_LABELS];
	long grain;

	u16 error_count;
	enum hw_event_mc_err_type type;
	int top_layer;
	int mid_layer;
	int low_layer;
	unsigned long page_frame_number;
	unsigned long offset_in_page;
	unsigned long syndrome;
	const char *msg;
	const char *other_detail;
};

/* MEMORY controller information structure
 */
struct mem_ctl_info {
	struct device			dev;
	const struct bus_type		*bus;

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

#define mci_for_each_dimm(mci, dimm)				\
	for ((dimm) = (mci)->dimms[0];				\
	     (dimm);						\
	     (dimm) = (dimm)->idx + 1 < (mci)->tot_dimms	\
		     ? (mci)->dimms[(dimm)->idx + 1]		\
		     : NULL)

/**
 * edac_get_dimm - Get DIMM info from a memory controller given by
 *                 [layer0,layer1,layer2] position
 *
 * @mci:	MC descriptor struct mem_ctl_info
 * @layer0:	layer0 position
 * @layer1:	layer1 position. Unused if n_layers < 2
 * @layer2:	layer2 position. Unused if n_layers < 3
 *
 * For 1 layer, this function returns "dimms[layer0]";
 *
 * For 2 layers, this function is similar to allocating a two-dimensional
 * array and returning "dimms[layer0][layer1]";
 *
 * For 3 layers, this function is similar to allocating a tri-dimensional
 * array and returning "dimms[layer0][layer1][layer2]";
 */
static inline struct dimm_info *edac_get_dimm(struct mem_ctl_info *mci,
	int layer0, int layer1, int layer2)
{
	int index;

	if (layer0 < 0
	    || (mci->n_layers > 1 && layer1 < 0)
	    || (mci->n_layers > 2 && layer2 < 0))
		return NULL;

	index = layer0;

	if (mci->n_layers > 1)
		index = index * mci->layers[1].size + layer1;

	if (mci->n_layers > 2)
		index = index * mci->layers[2].size + layer2;

	if (index < 0 || index >= mci->tot_dimms)
		return NULL;

	if (WARN_ON_ONCE(mci->dimms[index]->idx != index))
		return NULL;

	return mci->dimms[index];
}

#define EDAC_FEAT_NAME_LEN	128

/* RAS feature type */
enum edac_dev_feat {
	RAS_FEAT_SCRUB,
	RAS_FEAT_ECS,
	RAS_FEAT_MEM_REPAIR,
	RAS_FEAT_MAX
};

/**
 * struct edac_scrub_ops - scrub device operations (all elements optional)
 * @read_addr: read base address of scrubbing range.
 * @read_size: read offset of scrubbing range.
 * @write_addr: set base address of the scrubbing range.
 * @write_size: set offset of the scrubbing range.
 * @get_enabled_bg: check if currently performing background scrub.
 * @set_enabled_bg: start or stop a bg-scrub.
 * @get_min_cycle: get minimum supported scrub cycle duration in seconds.
 * @get_max_cycle: get maximum supported scrub cycle duration in seconds.
 * @get_cycle_duration: get current scrub cycle duration in seconds.
 * @set_cycle_duration: set current scrub cycle duration in seconds.
 */
struct edac_scrub_ops {
	int (*read_addr)(struct device *dev, void *drv_data, u64 *base);
	int (*read_size)(struct device *dev, void *drv_data, u64 *size);
	int (*write_addr)(struct device *dev, void *drv_data, u64 base);
	int (*write_size)(struct device *dev, void *drv_data, u64 size);
	int (*get_enabled_bg)(struct device *dev, void *drv_data, bool *enable);
	int (*set_enabled_bg)(struct device *dev, void *drv_data, bool enable);
	int (*get_min_cycle)(struct device *dev, void *drv_data,  u32 *min);
	int (*get_max_cycle)(struct device *dev, void *drv_data,  u32 *max);
	int (*get_cycle_duration)(struct device *dev, void *drv_data, u32 *cycle);
	int (*set_cycle_duration)(struct device *dev, void *drv_data, u32 cycle);
};

#if IS_ENABLED(CONFIG_EDAC_SCRUB)
int edac_scrub_get_desc(struct device *scrub_dev,
			const struct attribute_group **attr_groups,
			u8 instance);
#else
static inline int edac_scrub_get_desc(struct device *scrub_dev,
				      const struct attribute_group **attr_groups,
				      u8 instance)
{ return -EOPNOTSUPP; }
#endif /* CONFIG_EDAC_SCRUB */

/**
 * struct edac_ecs_ops - ECS device operations (all elements optional)
 * @get_log_entry_type: read the log entry type value.
 * @set_log_entry_type: set the log entry type value.
 * @get_mode: read the mode value.
 * @set_mode: set the mode value.
 * @reset: reset the ECS counter.
 * @get_threshold: read the threshold count per gigabits of memory cells.
 * @set_threshold: set the threshold count per gigabits of memory cells.
 */
struct edac_ecs_ops {
	int (*get_log_entry_type)(struct device *dev, void *drv_data, int fru_id, u32 *val);
	int (*set_log_entry_type)(struct device *dev, void *drv_data, int fru_id, u32 val);
	int (*get_mode)(struct device *dev, void *drv_data, int fru_id, u32 *val);
	int (*set_mode)(struct device *dev, void *drv_data, int fru_id, u32 val);
	int (*reset)(struct device *dev, void *drv_data, int fru_id, u32 val);
	int (*get_threshold)(struct device *dev, void *drv_data, int fru_id, u32 *threshold);
	int (*set_threshold)(struct device *dev, void *drv_data, int fru_id, u32 threshold);
};

struct edac_ecs_ex_info {
	u16 num_media_frus;
};

#if IS_ENABLED(CONFIG_EDAC_ECS)
int edac_ecs_get_desc(struct device *ecs_dev,
		      const struct attribute_group **attr_groups,
		      u16 num_media_frus);
#else
static inline int edac_ecs_get_desc(struct device *ecs_dev,
				    const struct attribute_group **attr_groups,
				    u16 num_media_frus)
{ return -EOPNOTSUPP; }
#endif /* CONFIG_EDAC_ECS */

enum edac_mem_repair_type {
	EDAC_REPAIR_MAX
};

enum edac_mem_repair_cmd {
	EDAC_DO_MEM_REPAIR = 1,
};

/**
 * struct edac_mem_repair_ops - memory repair operations
 * (all elements are optional except do_repair, set_hpa/set_dpa)
 * @get_repair_type: get the memory repair type, listed in
 *			 enum edac_mem_repair_function.
 * @get_persist_mode: get the current persist mode.
 *		      false - Soft repair type (temporary repair).
 *		      true - Hard memory repair type (permanent repair).
 * @set_persist_mode: set the persist mode of the memory repair instance.
 * @get_repair_safe_when_in_use: get whether memory media is accessible and
 *				 data is retained during repair operation.
 * @get_hpa: get current host physical address (HPA) of memory to repair.
 * @set_hpa: set host physical address (HPA) of memory to repair.
 * @get_min_hpa: get the minimum supported host physical address (HPA).
 * @get_max_hpa: get the maximum supported host physical address (HPA).
 * @get_dpa: get current device physical address (DPA) of memory to repair.
 * @set_dpa: set device physical address (DPA) of memory to repair.
 *	     In some states of system configuration (e.g. before address decoders
 *	     have been configured), memory devices (e.g. CXL) may not have an active
 *	     mapping in the host physical address map. As such, the memory
 *	     to repair must be identified by a device specific physical addressing
 *	     scheme using a device physical address(DPA). The DPA and other control
 *	     attributes to use for the repair operations will be presented in related
 *	     error records.
 * @get_min_dpa: get the minimum supported device physical address (DPA).
 * @get_max_dpa: get the maximum supported device physical address (DPA).
 * @get_nibble_mask: get current nibble mask of memory to repair.
 * @set_nibble_mask: set nibble mask of memory to repair.
 * @get_bank_group: get current bank group of memory to repair.
 * @set_bank_group: set bank group of memory to repair.
 * @get_bank: get current bank of memory to repair.
 * @set_bank: set bank of memory to repair.
 * @get_rank: get current rank of memory to repair.
 * @set_rank: set rank of memory to repair.
 * @get_row: get current row of memory to repair.
 * @set_row: set row of memory to repair.
 * @get_column: get current column of memory to repair.
 * @set_column: set column of memory to repair.
 * @get_channel: get current channel of memory to repair.
 * @set_channel: set channel of memory to repair.
 * @get_sub_channel: get current subchannel of memory to repair.
 * @set_sub_channel: set subchannel of memory to repair.
 * @do_repair: Issue memory repair operation for the HPA/DPA and
 *	       other control attributes set for the memory to repair.
 *
 * All elements are optional except do_repair and at least one of set_hpa/set_dpa.
 */
struct edac_mem_repair_ops {
	int (*get_repair_type)(struct device *dev, void *drv_data, const char **type);
	int (*get_persist_mode)(struct device *dev, void *drv_data, bool *persist);
	int (*set_persist_mode)(struct device *dev, void *drv_data, bool persist);
	int (*get_repair_safe_when_in_use)(struct device *dev, void *drv_data, bool *safe);
	int (*get_hpa)(struct device *dev, void *drv_data, u64 *hpa);
	int (*set_hpa)(struct device *dev, void *drv_data, u64 hpa);
	int (*get_min_hpa)(struct device *dev, void *drv_data, u64 *hpa);
	int (*get_max_hpa)(struct device *dev, void *drv_data, u64 *hpa);
	int (*get_dpa)(struct device *dev, void *drv_data, u64 *dpa);
	int (*set_dpa)(struct device *dev, void *drv_data, u64 dpa);
	int (*get_min_dpa)(struct device *dev, void *drv_data, u64 *dpa);
	int (*get_max_dpa)(struct device *dev, void *drv_data, u64 *dpa);
	int (*get_nibble_mask)(struct device *dev, void *drv_data, u32 *val);
	int (*set_nibble_mask)(struct device *dev, void *drv_data, u32 val);
	int (*get_bank_group)(struct device *dev, void *drv_data, u32 *val);
	int (*set_bank_group)(struct device *dev, void *drv_data, u32 val);
	int (*get_bank)(struct device *dev, void *drv_data, u32 *val);
	int (*set_bank)(struct device *dev, void *drv_data, u32 val);
	int (*get_rank)(struct device *dev, void *drv_data, u32 *val);
	int (*set_rank)(struct device *dev, void *drv_data, u32 val);
	int (*get_row)(struct device *dev, void *drv_data, u32 *val);
	int (*set_row)(struct device *dev, void *drv_data, u32 val);
	int (*get_column)(struct device *dev, void *drv_data, u32 *val);
	int (*set_column)(struct device *dev, void *drv_data, u32 val);
	int (*get_channel)(struct device *dev, void *drv_data, u32 *val);
	int (*set_channel)(struct device *dev, void *drv_data, u32 val);
	int (*get_sub_channel)(struct device *dev, void *drv_data, u32 *val);
	int (*set_sub_channel)(struct device *dev, void *drv_data, u32 val);
	int (*do_repair)(struct device *dev, void *drv_data, u32 val);
};

#if IS_ENABLED(CONFIG_EDAC_MEM_REPAIR)
int edac_mem_repair_get_desc(struct device *dev,
			     const struct attribute_group **attr_groups,
			     u8 instance);
#else
static inline int edac_mem_repair_get_desc(struct device *dev,
					   const struct attribute_group **attr_groups,
					   u8 instance)
{ return -EOPNOTSUPP; }
#endif /* CONFIG_EDAC_MEM_REPAIR */

/* EDAC device feature information structure */
struct edac_dev_data {
	union {
		const struct edac_scrub_ops *scrub_ops;
		const struct edac_ecs_ops *ecs_ops;
		const struct edac_mem_repair_ops *mem_repair_ops;
	};
	u8 instance;
	void *private;
};

struct edac_dev_feat_ctx {
	struct device dev;
	void *private;
	struct edac_dev_data *scrub;
	struct edac_dev_data ecs;
	struct edac_dev_data *mem_repair;
};

struct edac_dev_feature {
	enum edac_dev_feat ft_type;
	u8 instance;
	union {
		const struct edac_scrub_ops *scrub_ops;
		const struct edac_ecs_ops *ecs_ops;
		const struct edac_mem_repair_ops *mem_repair_ops;
	};
	void *ctx;
	struct edac_ecs_ex_info ecs_info;
};

int edac_dev_register(struct device *parent, char *dev_name,
		      void *parent_pvt_data, int num_features,
		      const struct edac_dev_feature *ras_features);
#endif /* _LINUX_EDAC_H_ */
