#ifndef _LINUX_I2C_ALGO_PXA_H
#define _LINUX_I2C_ALGO_PXA_H

struct i2c_eeprom_emu_watcher {
	void (*write)(void *, unsigned int addr, unsigned char newval);
};

struct i2c_eeprom_emu_watch {
	struct list_head node;
	unsigned int start;
	unsigned int end;
	struct i2c_eeprom_emu_watcher *ops;
	void *data;
};

#define I2C_EEPROM_EMU_SIZE (256)

struct i2c_eeprom_emu {
	unsigned int size;
	unsigned int ptr;
	unsigned int seen_start;
	struct list_head watch;

	unsigned char bytes[I2C_EEPROM_EMU_SIZE];
};

typedef enum i2c_slave_event_e {
	I2C_SLAVE_EVENT_START_READ,
	I2C_SLAVE_EVENT_START_WRITE,
	I2C_SLAVE_EVENT_STOP
} i2c_slave_event_t;

struct i2c_slave_client {
	void *data;
	void (*event)(void *ptr, i2c_slave_event_t event);
	int  (*read) (void *ptr);
	void (*write)(void *ptr, unsigned int val);
};

extern int i2c_eeprom_emu_addwatcher(struct i2c_eeprom_emu *, void *data,
				     unsigned int addr, unsigned int size,
				     struct i2c_eeprom_emu_watcher *);

extern void i2c_eeprom_emu_delwatcher(struct i2c_eeprom_emu *, void *data, struct i2c_eeprom_emu_watcher *watcher);

extern struct i2c_eeprom_emu *i2c_pxa_get_eeprom(void);

#endif /* _LINUX_I2C_ALGO_PXA_H */
