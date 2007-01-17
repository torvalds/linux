#ifndef _LINUX_I2C_ALGO_PCA_H
#define _LINUX_I2C_ALGO_PCA_H

struct i2c_algo_pca_data {
	int  (*get_own)			(struct i2c_algo_pca_data *adap); /* Obtain own address */
	int  (*get_clock)		(struct i2c_algo_pca_data *adap);
	void (*write_byte)		(struct i2c_algo_pca_data *adap, int reg, int val);
	int  (*read_byte)		(struct i2c_algo_pca_data *adap, int reg);
	int  (*wait_for_interrupt)	(struct i2c_algo_pca_data *adap);
};

int i2c_pca_add_bus(struct i2c_adapter *);

#endif /* _LINUX_I2C_ALGO_PCA_H */
