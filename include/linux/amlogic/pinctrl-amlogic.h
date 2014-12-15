#include <linux/platform_device.h>
#include <mach/io.h>
#include <plat/io.h>
struct amlogic_reg_mask{
	unsigned int reg;
	unsigned int mask;
};
struct amlogic_pmx_mask {
	unsigned int reg;
	unsigned int setmask;
	unsigned int clrmask;
};

struct amlogic_pin_group {
	const char *name;
	unsigned int *pins;
	unsigned num_pins;
	struct amlogic_reg_mask *setmask;
	unsigned num_setmask;
	struct amlogic_reg_mask *clearmask;
	unsigned num_clearmask;
};


/**
 * struct amlogic_pmx_func - describes amlogic pinmux functions
 * @name: the name of this specific function
 * @groups: corresponding pin groups
 */
struct amlogic_pmx_func {
	const char *name;
	const char **groups;
	unsigned num_groups;
};

struct amlogic_pinctrl_soc_data {
	const struct pinctrl_pin_desc *pins;
	unsigned npins;
	struct amlogic_pmx_func *functions;
	unsigned nfunctions;
	struct amlogic_pin_group *groups;
	unsigned ngroups;
	int (*meson_set_pullup)(unsigned int,unsigned int);
	int (*pin_map_to_direction)(unsigned int ,unsigned int *,unsigned int *);
};


int  amlogic_pmx_probe(struct platform_device *pdev,struct amlogic_pinctrl_soc_data *soc_data);
int amlogic_pmx_remove(struct platform_device *pdev);

