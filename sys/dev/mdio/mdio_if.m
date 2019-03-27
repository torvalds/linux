# $FreeBSD$

#include <sys/bus.h>

INTERFACE mdio;

CODE {
	#include <dev/mdio/mdio.h>

	static int
	mdio_null_readextreg(device_t dev, int phy, int devad, int reg)
	{
		if (devad == MDIO_DEVADDR_NONE)
			return (MDIO_READREG(dev, phy, reg));
		return (~0U);
	}

	static int
	mdio_null_writeextreg(device_t dev, int phy, int devad, int reg,
	    int val)
	{
		if (devad == MDIO_DEVADDR_NONE)
			return (MDIO_WRITEREG(dev, phy, reg, val));

		return (EINVAL);
	}
}

/**
 * @brief Read register from device on MDIO bus.
 *
 * @param dev	MDIO bus device.
 * @param phy	PHY address.
 * @param reg	The PHY register offset.
 */
METHOD int readreg {
	device_t		dev;
	int			phy;
	int			reg;
};

/**
 * @brief Write register to device on MDIO bus.
 *
 * @param dev	MDIO bus device.
 * @param phy	PHY address.
 * @param reg	The PHY register offset.
 * @param val	The value to write at offset @p reg.
 */
METHOD int writereg {
	device_t		dev;
	int			phy;
	int			reg;
	int			val;
};


/**
 * @brief Read extended register from device on MDIO bus.
 *
 * @param dev	MDIO bus device.
 * @param phy	PHY address.
 * @param devad The MDIO IEEE 802.3 Clause 45 device address, or
 *		MDIO_DEVADDR_NONE to request Clause 22 register addressing.
 * @param reg	The PHY register offset.
 */
METHOD int readextreg {
	device_t		dev;
	int			phy;
	int			devad;
	int			reg;
} DEFAULT mdio_null_readextreg;


/**
 * @brief Write extended register to device on MDIO bus.
 *
 * @param dev	MDIO bus device.
 * @param phy	PHY address.
 * @param devad The MDIO IEEE 802.3 Clause 45 device address, or
 *		MDIO_DEVADDR_NONE to request Clause 22 register addressing.
 * @param reg	The PHY register offset.
 * @param val	The value to write at offset @p reg.
 */
METHOD int writeextreg {
	device_t		dev;
	int			phy;
	int			devad;
	int			reg;
	int			val;
} DEFAULT mdio_null_writeextreg;
