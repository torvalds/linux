#ifndef SENSOR_COMMON_H
#define SENSOR_COMMON_H

#include <linux/i2c.h>

int sensor_setup_i2c_dev(struct i2c_board_info *i2c_info, int *i2c_bus_nr, int *gpio);

typedef struct _pdata
{
    struct i2c_client *client;
    int acc_negate_x;
    int acc_negate_y;
    int acc_negate_z;
    int acc_swap_xy;

    int mag_negate_x;
    int mag_negate_y;
    int mag_negate_z;
    int mag_swap_xy;

    int gyr_negate_x;
    int gyr_negate_y;
    int gyr_negate_z;
    int gyr_swap_xy;
}sensor_pdata_t;

void aml_sensor_report_acc(struct i2c_client *client, struct input_dev *dev, int x, int y, int z);
void aml_sensor_report_mag(struct i2c_client *client, struct input_dev *dev, int x, int y, int z);
void aml_sensor_report_gyr(struct i2c_client *client, struct input_dev *dev, int x, int y, int z);

#endif
