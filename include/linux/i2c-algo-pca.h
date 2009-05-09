#ifndef _LINUX_I2C_ALGO_PCA_H
#define _LINUX_I2C_ALGO_PCA_H

/* Chips known to the pca algo */
#define I2C_PCA_CHIP_9564	0x00
#define I2C_PCA_CHIP_9665	0x01

/* Internal period for PCA9665 oscilator */
#define I2C_PCA_OSC_PER		3 /* e10-8s */

/* Clock speeds for the bus for PCA9564*/
#define I2C_PCA_CON_330kHz	0x00
#define I2C_PCA_CON_288kHz	0x01
#define I2C_PCA_CON_217kHz	0x02
#define I2C_PCA_CON_146kHz	0x03
#define I2C_PCA_CON_88kHz	0x04
#define I2C_PCA_CON_59kHz	0x05
#define I2C_PCA_CON_44kHz	0x06
#define I2C_PCA_CON_36kHz	0x07

/* PCA9564 registers */
#define I2C_PCA_STA		0x00 /* STATUS  Read Only  */
#define I2C_PCA_TO		0x00 /* TIMEOUT Write Only */
#define I2C_PCA_DAT		0x01 /* DATA    Read/Write */
#define I2C_PCA_ADR		0x02 /* OWN ADR Read/Write */
#define I2C_PCA_CON		0x03 /* CONTROL Read/Write */

/* PCA9665 registers */
#define I2C_PCA_INDPTR          0x00 /* INDIRECT Pointer Write Only */
#define I2C_PCA_IND             0x02 /* INDIRECT Read/Write */

/* PCA9665 indirect registers */
#define I2C_PCA_ICOUNT          0x00 /* Byte Count for buffered mode */
#define I2C_PCA_IADR            0x01 /* OWN ADR */
#define I2C_PCA_ISCLL           0x02 /* SCL LOW period */
#define I2C_PCA_ISCLH           0x03 /* SCL HIGH period */
#define I2C_PCA_ITO             0x04 /* TIMEOUT */
#define I2C_PCA_IPRESET         0x05 /* Parallel bus reset */
#define I2C_PCA_IMODE           0x06 /* I2C Bus mode */

/* PCA9665 I2C bus mode */
#define I2C_PCA_MODE_STD        0x00 /* Standard mode */
#define I2C_PCA_MODE_FAST       0x01 /* Fast mode */
#define I2C_PCA_MODE_FASTP      0x02 /* Fast Plus mode */
#define I2C_PCA_MODE_TURBO      0x03 /* Turbo mode */


#define I2C_PCA_CON_AA		0x80 /* Assert Acknowledge */
#define I2C_PCA_CON_ENSIO	0x40 /* Enable */
#define I2C_PCA_CON_STA		0x20 /* Start */
#define I2C_PCA_CON_STO		0x10 /* Stop */
#define I2C_PCA_CON_SI		0x08 /* Serial Interrupt */
#define I2C_PCA_CON_CR		0x07 /* Clock Rate (MASK) */

struct i2c_algo_pca_data {
	void 				*data;	/* private low level data */
	void (*write_byte)		(void *data, int reg, int val);
	int  (*read_byte)		(void *data, int reg);
	int  (*wait_for_completion)	(void *data);
	void (*reset_chip)		(void *data);
	/* For PCA9564, use one of the predefined frequencies:
	 * 330000, 288000, 217000, 146000, 88000, 59000, 44000, 36000
	 * For PCA9665, use the frequency you want here. */
	unsigned int			i2c_clock;
};

int i2c_pca_add_bus(struct i2c_adapter *);
int i2c_pca_add_numbered_bus(struct i2c_adapter *);

#endif /* _LINUX_I2C_ALGO_PCA_H */
