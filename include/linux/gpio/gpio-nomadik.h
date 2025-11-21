/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GPIO_NOMADIK_H
#define __LINUX_GPIO_NOMADIK_H

struct fwnode_handle;

/* Package definitions */
#define PINCTRL_NMK_STN8815	0
#define PINCTRL_NMK_DB8500	1

#define GPIO_BLOCK_SHIFT 5
#define NMK_GPIO_PER_CHIP BIT(GPIO_BLOCK_SHIFT)
#define NMK_MAX_BANKS DIV_ROUND_UP(512, NMK_GPIO_PER_CHIP)

/* Register in the logic block */
#define NMK_GPIO_DAT	0x00
#define NMK_GPIO_DATS	0x04
#define NMK_GPIO_DATC	0x08
#define NMK_GPIO_PDIS	0x0c
#define NMK_GPIO_DIR	0x10
#define NMK_GPIO_DIRS	0x14
#define NMK_GPIO_DIRC	0x18
#define NMK_GPIO_SLPC	0x1c
#define NMK_GPIO_AFSLA	0x20
#define NMK_GPIO_AFSLB	0x24
#define NMK_GPIO_LOWEMI	0x28

#define NMK_GPIO_RIMSC	0x40
#define NMK_GPIO_FIMSC	0x44
#define NMK_GPIO_IS	0x48
#define NMK_GPIO_IC	0x4c
#define NMK_GPIO_RWIMSC	0x50
#define NMK_GPIO_FWIMSC	0x54
#define NMK_GPIO_WKS	0x58
/* These appear in DB8540 and later ASICs */
#define NMK_GPIO_EDGELEVEL 0x5C
#define NMK_GPIO_LEVEL	0x60

/* Pull up/down values */
enum nmk_gpio_pull {
	NMK_GPIO_PULL_NONE,
	NMK_GPIO_PULL_UP,
	NMK_GPIO_PULL_DOWN,
};

/* Sleep mode */
enum nmk_gpio_slpm {
	NMK_GPIO_SLPM_INPUT,
	NMK_GPIO_SLPM_WAKEUP_ENABLE = NMK_GPIO_SLPM_INPUT,
	NMK_GPIO_SLPM_NOCHANGE,
	NMK_GPIO_SLPM_WAKEUP_DISABLE = NMK_GPIO_SLPM_NOCHANGE,
};

struct nmk_gpio_chip {
	struct gpio_chip chip;
	void __iomem *addr;
	struct clk *clk;
	unsigned int bank;
	void (*set_ioforce)(bool enable);
	spinlock_t lock;
	bool sleepmode;
	bool is_mobileye_soc;
	/* Keep track of configured edges */
	u32 edge_rising;
	u32 edge_falling;
	u32 real_wake;
	u32 rwimsc;
	u32 fwimsc;
	u32 rimsc;
	u32 fimsc;
	u32 pull_up;
	u32 lowemi;
};

/* Alternate functions: function C is set in hw by setting both A and B */
#define NMK_GPIO_ALT_GPIO	0
#define NMK_GPIO_ALT_A	1
#define NMK_GPIO_ALT_B	2
#define NMK_GPIO_ALT_C	(NMK_GPIO_ALT_A | NMK_GPIO_ALT_B)

#define NMK_GPIO_ALT_CX_SHIFT 2
#define NMK_GPIO_ALT_C1	((1<<NMK_GPIO_ALT_CX_SHIFT) | NMK_GPIO_ALT_C)
#define NMK_GPIO_ALT_C2	((2<<NMK_GPIO_ALT_CX_SHIFT) | NMK_GPIO_ALT_C)
#define NMK_GPIO_ALT_C3	((3<<NMK_GPIO_ALT_CX_SHIFT) | NMK_GPIO_ALT_C)
#define NMK_GPIO_ALT_C4	((4<<NMK_GPIO_ALT_CX_SHIFT) | NMK_GPIO_ALT_C)

#define PRCM_GPIOCR_ALTCX(pin_num,\
	altc1_used, altc1_ri, altc1_cb,\
	altc2_used, altc2_ri, altc2_cb,\
	altc3_used, altc3_ri, altc3_cb,\
	altc4_used, altc4_ri, altc4_cb)\
{\
	.pin = pin_num,\
	.altcx[PRCM_IDX_GPIOCR_ALTC1] = {\
		.used = altc1_used,\
		.reg_index = altc1_ri,\
		.control_bit = altc1_cb\
	},\
	.altcx[PRCM_IDX_GPIOCR_ALTC2] = {\
		.used = altc2_used,\
		.reg_index = altc2_ri,\
		.control_bit = altc2_cb\
	},\
	.altcx[PRCM_IDX_GPIOCR_ALTC3] = {\
		.used = altc3_used,\
		.reg_index = altc3_ri,\
		.control_bit = altc3_cb\
	},\
	.altcx[PRCM_IDX_GPIOCR_ALTC4] = {\
		.used = altc4_used,\
		.reg_index = altc4_ri,\
		.control_bit = altc4_cb\
	},\
}

/**
 * enum prcm_gpiocr_reg_index
 * Used to reference an PRCM GPIOCR register address.
 */
enum prcm_gpiocr_reg_index {
	PRCM_IDX_GPIOCR1,
	PRCM_IDX_GPIOCR2,
	PRCM_IDX_GPIOCR3
};
/**
 * enum prcm_gpiocr_altcx_index
 * Used to reference an Other alternate-C function.
 */
enum prcm_gpiocr_altcx_index {
	PRCM_IDX_GPIOCR_ALTC1,
	PRCM_IDX_GPIOCR_ALTC2,
	PRCM_IDX_GPIOCR_ALTC3,
	PRCM_IDX_GPIOCR_ALTC4,
	PRCM_IDX_GPIOCR_ALTC_MAX,
};

/**
 * struct prcm_gpio_altcx - Other alternate-C function
 * @used: other alternate-C function availability
 * @reg_index: PRCM GPIOCR register index used to control the function
 * @control_bit: PRCM GPIOCR bit used to control the function
 */
struct prcm_gpiocr_altcx {
	bool used:1;
	u8 reg_index:2;
	u8 control_bit:5;
} __packed;

/**
 * struct prcm_gpio_altcx_pin_desc - Other alternate-C pin
 * @pin: The pin number
 * @altcx: array of other alternate-C[1-4] functions
 */
struct prcm_gpiocr_altcx_pin_desc {
	unsigned short pin;
	struct prcm_gpiocr_altcx altcx[PRCM_IDX_GPIOCR_ALTC_MAX];
};

/**
 * struct nmk_function - Nomadik pinctrl mux function
 * @name: The name of the function, exported to pinctrl core.
 * @groups: An array of pin groups that may select this function.
 * @ngroups: The number of entries in @groups.
 */
struct nmk_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
};

/**
 * struct nmk_pingroup - describes a Nomadik pin group
 * @grp: Generic data of the pin group (name and pins)
 * @altsetting: the altsetting to apply to all pins in this group to
 *	configure them to be used by a function
 */
struct nmk_pingroup {
	struct pingroup grp;
	int altsetting;
};

#define NMK_PIN_GROUP(a, b)							\
	{									\
		.grp = PINCTRL_PINGROUP(#a, a##_pins, ARRAY_SIZE(a##_pins)),	\
		.altsetting = b,						\
	}

/**
 * struct nmk_pinctrl_soc_data - Nomadik pin controller per-SoC configuration
 * @pins:	An array describing all pins the pin controller affects.
 *		All pins which are also GPIOs must be listed first within the
 *		array, and be numbered identically to the GPIO controller's
 *		numbering.
 * @npins:	The number of entries in @pins.
 * @functions:	The functions supported on this SoC.
 * @nfunction:	The number of entries in @functions.
 * @groups:	An array describing all pin groups the pin SoC supports.
 * @ngroups:	The number of entries in @groups.
 * @altcx_pins:	The pins that support Other alternate-C function on this SoC
 * @npins_altcx: The number of Other alternate-C pins
 * @prcm_gpiocr_registers: The array of PRCM GPIOCR registers on this SoC
 */
struct nmk_pinctrl_soc_data {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct nmk_function *functions;
	unsigned int nfunctions;
	const struct nmk_pingroup *groups;
	unsigned int ngroups;
	const struct prcm_gpiocr_altcx_pin_desc *altcx_pins;
	unsigned int npins_altcx;
	const u16 *prcm_gpiocr_registers;
};

#ifdef CONFIG_PINCTRL_STN8815

void nmk_pinctrl_stn8815_init(const struct nmk_pinctrl_soc_data **soc);

#else

static inline void
nmk_pinctrl_stn8815_init(const struct nmk_pinctrl_soc_data **soc)
{
}

#endif

#ifdef CONFIG_PINCTRL_DB8500

void nmk_pinctrl_db8500_init(const struct nmk_pinctrl_soc_data **soc);

#else

static inline void
nmk_pinctrl_db8500_init(const struct nmk_pinctrl_soc_data **soc)
{
}

#endif

#ifdef CONFIG_PINCTRL_DB8540

void nmk_pinctrl_db8540_init(const struct nmk_pinctrl_soc_data **soc);

#else

static inline void
nmk_pinctrl_db8540_init(const struct nmk_pinctrl_soc_data **soc)
{
}

#endif

struct platform_device;

#ifdef CONFIG_DEBUG_FS

/*
 * Symbols declared in gpio-nomadik used by pinctrl-nomadik. If pinctrl-nomadik
 * is enabled, then gpio-nomadik is enabled as well; the reverse if not always
 * true.
 */
void nmk_gpio_dbg_show_one(struct seq_file *s, struct pinctrl_dev *pctldev,
			   struct gpio_chip *chip, unsigned int offset);

#else

static inline void nmk_gpio_dbg_show_one(struct seq_file *s,
					 struct pinctrl_dev *pctldev,
					 struct gpio_chip *chip,
					 unsigned int offset)
{
}

#endif

void __nmk_gpio_make_output(struct nmk_gpio_chip *nmk_chip,
			    unsigned int offset, int val);
void __nmk_gpio_set_slpm(struct nmk_gpio_chip *nmk_chip, unsigned int offset,
			 enum nmk_gpio_slpm mode);
struct nmk_gpio_chip *nmk_gpio_populate_chip(struct fwnode_handle *fwnode,
					     struct platform_device *pdev);

/* Symbols declared in pinctrl-nomadik used by gpio-nomadik. */
#ifdef CONFIG_PINCTRL_NOMADIK
extern struct nmk_gpio_chip *nmk_gpio_chips[NMK_MAX_BANKS];
extern spinlock_t nmk_gpio_slpm_lock;
int __maybe_unused nmk_prcm_gpiocr_get_mode(struct pinctrl_dev *pctldev,
					    int gpio);
#endif

#endif /* __LINUX_GPIO_NOMADIK_H */
