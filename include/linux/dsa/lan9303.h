/* Included by drivers/net/dsa/lan9303.h and net/dsa/tag_lan9303.c */
#include <linux/if_ether.h>

struct lan9303;

struct lan9303_phy_ops {
	/* PHY 1 and 2 access*/
	int	(*phy_read)(struct lan9303 *chip, int port, int regnum);
	int	(*phy_write)(struct lan9303 *chip, int port,
			     int regnum, u16 val);
};

#define LAN9303_NUM_ALR_RECORDS 512
struct lan9303_alr_cache_entry {
	u8  mac_addr[ETH_ALEN];
	u8  port_map;         /* Bitmap of ports. Zero if unused entry */
	u8  stp_override;     /* non zero if set LAN9303_ALR_DAT1_AGE_OVERRID */
};

struct lan9303 {
	struct device *dev;
	struct regmap *regmap;
	struct regmap_irq_chip_data *irq_data;
	struct gpio_desc *reset_gpio;
	u32 reset_duration; /* in [ms] */
	int phy_addr_base;
	struct dsa_switch *ds;
	struct mutex indirect_mutex; /* protect indexed register access */
	struct mutex alr_mutex; /* protect ALR access */
	const struct lan9303_phy_ops *ops;
	bool is_bridged; /* true if port 1 and 2 are bridged */

	/* remember LAN9303_SWE_PORT_STATE while not bridged */
	u32 swe_port_state;
	/* LAN9303 do not offer reading specific ALR entry. Cache all
	 * static entries in a flat table
	 **/
	struct lan9303_alr_cache_entry alr_cache[LAN9303_NUM_ALR_RECORDS];
};
