/* SPDX-License-Identifier: GPL-2.0 */
/**
 * \file Codec Firmware Declarations
 */

#ifndef CFW_FIRMWARE_H_
#define CFW_FIRMWARE_H_

/** \defgroup bt Basic Types */
/* @{ */
#ifndef AIC3XXX_CFW_HOST_BLD
#include <asm-generic/int-ll64.h>
#else
typedef unsigned char u8;
typedef unsigned short int u16;
typedef unsigned long  int u32;
#endif
typedef signed char i8;
typedef signed short int i16;
typedef signed long  int i32;

#define CFW_FW_MAGIC 0xC0D1F1ED
/* @} */


/** \defgroup pd Arbitrary Limitations */
/* @{ */
#ifndef CFW_MAX_ID
#    define CFW_MAX_ID          (64)    ///<Max length of string identifies
#endif
#ifndef CFW_MAX_DESC
#   define  CFW_MAX_DESC        (512)   ///<Max length of description
#endif
#ifndef CFW_MAX_NOVLY
#    define CFW_MAX_NOVLY       (4)     ///<Max number of overlays per PFW
#endif

#ifndef CFW_MAX_NCFG
#    define CFW_MAX_NCFG        (16)    ///<Max number of configurations per PFW
#endif

#ifndef CFW_MAX_TRANSITIONS
#    define CFW_MAX_TRANSITIONS (32)    ///<max number of pre-defined transition
#endif

#ifndef CFW_MAX_NPFW
#    define CFW_MAX_NPFW        (16)    ///<Max number fo process flows
#endif

#ifndef CFW_MAX_MODES
#    define CFW_MAX_MODES       (32)    ///<Max number of modes
#endif

#ifndef CFW_MAX_ASI
#    define CFW_MAX_ASI         (4)     ///<Max number ASIs in a single device
#endif

/* @} */



/** \defgroup st Enums, Flags, Macros and Supporting Types */
/* @{ */

/**
 * Sample rate bitmask
 *
 */ 
enum cfw_fs {
    CFW_FS_8KHZ     = 0x0001u,
    CFW_FS_11KHZ    = 0x0002u,
    CFW_FS_16KHZ    = 0x0004u,
    CFW_FS_22KHZ    = 0x0008u,
    CFW_FS_24KHZ    = 0x0010u,
    CFW_FS_32KHZ    = 0x0020u,
    CFW_FS_44KHZ    = 0x0040u,
    CFW_FS_48KHZ    = 0x0080u,
    CFW_FS_88KHZ    = 0x0100u,
    CFW_FS_96KHZ    = 0x0200u,
    CFW_FS_176KHZ   = 0x0400u,
    CFW_FS_192KHZ   = 0x0800u,
    CFW_FS_ANY      = 0x8000u,
    CFW_FS_ALL      = 0x0FFFu,
};

/**
 * Sample rate index
 *
 */
enum cfw_fsi {
    CFW_FSI_8KHZ,
    CFW_FSI_11KHZ,
    CFW_FSI_16KHZ,
    CFW_FSI_22KHZ,
    CFW_FSI_24KHZ,
    CFW_FSI_32KHZ,
    CFW_FSI_44KHZ,
    CFW_FSI_48KHZ,
    CFW_FSI_88KHZ,
    CFW_FSI_96KHZ,
    CFW_FSI_176KHZ,
    CFW_FSI_192KHZ,
    CFW_FSI_ANY = 15,
};


/**
 * Device Family Identifier
 *
 */
typedef enum __attribute__ ((__packed__)) cfw_dfamily {
    CFW_DFM_TYPE_A,
    CFW_DFM_TYPE_B,
    CFW_DFM_TYPE_C
} cfw_dfamily;

/**
 * Device Identifier
 *
 */
typedef enum __attribute__ ((__packed__)) cfw_device {
    CFW_DEV_DAC3120,
    CFW_DEV_DAC3100,

    CFW_DEV_AIC3120,
    CFW_DEV_AIC3100,
    CFW_DEV_AIC3110,
    CFW_DEV_AIC3111,

    CFW_DEV_AIC36,

    CFW_DEV_AIC3206,
    CFW_DEV_AIC3204,
    CFW_DEV_AIC3254,
    CFW_DEV_AIC3256,
    CFW_DEV_AIC3253,
    
    CFW_DEV_AIC3212,
    CFW_DEV_AIC3262,
    CFW_DEV_AIC3017,
    CFW_DEV_AIC3008,

} cfw_device;

/**
 * Transition Sequence Identifier
 *
 */
typedef enum cfw_transition_t {
    CFW_TRN_INIT,
    CFW_TRN_RESUME,
    CFW_TRN_NEUTRAL,
    CFW_TRN_SUSPEND,
    CFW_TRN_EXIT,
    CFW_TRN_N
} cfw_transition_t;
static const char * const cfw_transition_id[] = {
    [CFW_TRN_INIT] "INIT",
    [CFW_TRN_RESUME] "RESUME",
    [CFW_TRN_NEUTRAL] "NEUTRAL",
    [CFW_TRN_SUSPEND] "SUSPEND",
    [CFW_TRN_EXIT] "EXIT",
};

/* @} */

/** \defgroup ds Data Structures */
/* @{ */


/**
* CFW Meta Command
* These commands do not appear in the register
* set of the device.
* Mainly delay, wait and set_bits.
*/
typedef enum __attribute__ ((__packed__)) cfw_meta_cmd {
    CFW_META_DELAY = 0x80,
    CFW_META_UPDTBITS,
    CFW_META_WAITBITS,
    CFW_META_LOCK,
} cfw_meta_cmd;

/**
* CFW Delay
* Used for the meta command delay
* Has one parameter of delay time in ms
*/
typedef struct cfw_meta_delay {
    u16 delay;
    cfw_meta_cmd mcmd;
    u8 	unused1;
} cfw_meta_delay;

/**
* CFW set_bits or wait
* Both these meta commands have same arguments
* mcmd will be used to specify which command it is
* has parameters of book, page, offset and mask
*/
typedef struct cfw_meta_bitop {
    u16 unused1;
    cfw_meta_cmd mcmd;
    u8   mask;
} cfw_meta_bitop;

/**
* CFW meta register
* Contains the data structures for the meta commands
*/
typedef union cfw_meta_register {
    struct {
        u16 unused1;
        cfw_meta_cmd mcmd;
        u8 unused2;
    };
    cfw_meta_delay delay;
    cfw_meta_bitop bitop;
} cfw_meta_register;


/**
 * CFW Register
 *
 * A single reg write
 *
 */
typedef union cfw_register {
    struct {
        u8 book;
        u8 page;
        u8 offset;
        u8 data;
    };
    u32 bpod;
    cfw_meta_register meta;
} cfw_register;

/**
 * CFW Burst
 *
 * A single I2C/SPI burst write sequence
 *
 */
typedef struct cfw_burst {
    u32 length;
    union {
        cfw_register reg;
        struct {
            u8 bpo[3];
            u8 data[1];
        };
    };
} cfw_burst;



/**
 * CFW Command
 *
 * Can be a either a
 *      -# single register write, 
 *      -# a burst write, or
 *      -# meta-command 
 *
 */
typedef union cfw_cmd {
    cfw_register reg;
    cfw_burst    *burst;
}  cfw_cmd;


/**
 * CFW Block Type
 *
 * Block identifier
 *
 */
typedef enum __attribute__ ((__packed__)) cfw_block_t {
    CFW_BLOCK_SYSTEM_PRE,
    CFW_BLOCK_A_INST,
    CFW_BLOCK_A_A_COEF,
    CFW_BLOCK_A_B_COEF,
    CFW_BLOCK_A_F_COEF,
    CFW_BLOCK_D_INST,
    CFW_BLOCK_D_A1_COEF,
    CFW_BLOCK_D_B1_COEF,
    CFW_BLOCK_D_A2_COEF,
    CFW_BLOCK_D_B2_COEF,
    CFW_BLOCK_D_F_COEF,
    CFW_BLOCK_SYSTEM_POST,
    CFW_BLOCK_N,
    CFW_BLOCK_BURSTS = 0x80
} cfw_block_t;
#define CFW_BLOCK_BURSTS(x) ((x)&CFW_BLOCK_BURSTS)
#define CFW_BLOCK_D_A_COEF CFW_BLOCK_D_A1_COEF
#define CFW_BLOCK_D_B_COEF CFW_BLOCK_D_B1_COEF

/**
 * CFW Block
 *
 * A block of logically grouped sequences/commands/meta-commands
 *
 */
typedef struct cfw_block  {
    cfw_block_t type;
    int ncmds;
    cfw_cmd cmd[1];
} cfw_block;


/**
 * CFW Image
 *
 * A downloadable image
 */
typedef struct cfw_image {
    char name[CFW_MAX_ID];  ///< Name of the pfw/overlay/configuration
    char desc[CFW_MAX_DESC];    ///< User string
    cfw_block *block[CFW_BLOCK_N];
} cfw_image;


/**
 * Sysclk source
 *
 */
typedef enum __attribute__ ((__packed__)) cfw_sclk_source {
    CFW_SYSCLK_MCLK1,
    CFW_SYSCLK_MCLK2,
    CFW_SYSCLK_BCLK1,
    CFW_SYSCLK_GPIO1,
    CFW_SYSCLK_PLL_CLK,
    CFW_SYSCLK_BCLK2,
    CFW_SYSCLK_GPI1,
    CFW_SYSCLK_HF_REF_CLK,
    CFW_SYSCLK_HF_OSC_CLK,
    CFW_SYSCLK_GPIO2,
    CFW_SYSCLK_GPI2,
} cfw_sclk_source;

    
/**
 * Process flow
 *
 * Complete description of a process flow
 */
typedef struct cfw_pfw {
    char name[CFW_MAX_ID];  ///< Name of the process flow
    char desc[CFW_MAX_DESC];    ///< User string
    u32 version;
    u16 supported_fs;       ///< Sampling  rates at which this process flow may run (bit mask; see \ref cfw_fs)
    u8  prb_a;
    u8  prb_d;
    int novly;              ///< Number of overlays (1 or more)
    int ncfg;              ///< Number of configurations (0 or more)
    cfw_block *pll;
    cfw_image *base;           ///< Base sequence
    cfw_image *ovly_cfg[CFW_MAX_NOVLY][CFW_MAX_NCFG]; ///< Overlay and cfg
                                                ///< patches (if any)
} cfw_pfw;


/**
 * Process transition
 *
 * Sequence for specific state transisitions within the driver
 *
 */
typedef struct cfw_transition {
    u32 id;
    char name[CFW_MAX_ID];  ///< Name of the transition    
    char desc[CFW_MAX_DESC];    ///< User string
    cfw_block *block;
} cfw_transition;

/**
 * Device audio mode
 *
 * Structure linking various operating modes to process flows,
 * configurations and sequences
 *
 */
typedef struct cfw_mode {
    u32 id;
    char name[CFW_MAX_ID];
    char desc[CFW_MAX_DESC];    ///< User string
    u32 flags;
    u8  pfw;
    u8  ovly;
    u8  cfg;
    u32 supported_cfgs;
    cfw_block *entry;
    cfw_block *exit;
} cfw_mode;

/**
 * CFW Project 
 *
 * Top level structure describing the CFW project
 */
typedef struct cfw_project {
    u32 magic;
    u32 bmagic;
    u32 size;
    u32 cksum;
    u32 version;
    u32 tstamp;
    char name[CFW_MAX_ID];      ///< Project name
    char desc[CFW_MAX_DESC];    ///< User string
    cfw_dfamily dfamily;
    cfw_device  device;
    u32  flags;
    cfw_sclk_source clksrc;     ///< Clock source
    u32  clkfreq;               ///< Clock frequency
    cfw_transition *transition[CFW_MAX_TRANSITIONS];
    u16  npfw;                  ///< Number of process flows
    u16  nmode;                  ///< Number of operating modes
    cfw_pfw  *pfw[CFW_MAX_NPFW]; ///< Indices to PFW locations
    cfw_mode *mode[CFW_MAX_MODES];
} cfw_project;


/* @} */

#endif /* CFW_FIRMWARE_H_ */
