#ifndef __RK1000_H__
#define __RK1000_H__

#define I2C_ADDR_CTRL 0x40
#define I2C_ADDR_TVE 0x42

#define RK1000_I2C_RATE	(100*1000)

struct ioctrl {
	int gpio;
	int active;
};

int rk1000_i2c_send(const u8 addr, const u8 reg, const u8 value);
int rk1000_i2c_recv(const u8 addr, const u8 reg, const char *buf);

#endif
