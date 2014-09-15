#ifndef __PLAT_BOARD_H
#define __PLAT_BOARD_H

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>

struct rksdmmc_iomux {
    char    *name;  //set the MACRO of gpio
    int     fgpio;
    int     fmux;
};

struct rksdmmc_gpio {
    int     io;                             //set the address of gpio
    char    name[64];   //
    int     enable;  // disable = !enable   //set the default value,i.e,GPIO_HIGH or GPIO_LOW
    struct rksdmmc_iomux  iomux;
};

struct rksdmmc_pmu {
    bool power_ctrl_by_pmu;
    char pmu_regulator[20];
    int  enable;
};

struct rksdmmc_gpio_wifi_moudle {
    int sdio_vol;    //sdio reference voltage
    bool vref_ctrl_enble;
    bool wifi_power_remain;
    struct rksdmmc_pmu    mregulator;
    struct rksdmmc_pmu    ioregulator;
    struct rksdmmc_gpio   power_n;  //PMU_EN  
    struct rksdmmc_gpio   reset_n;  //SYSRET_B, DAIRST 
    struct rksdmmc_gpio   vddio;
    struct rksdmmc_gpio   bgf_int_b;
    struct rksdmmc_gpio   wifi_int_b;
    struct rksdmmc_gpio   gps_sync;
    struct rksdmmc_gpio   ANTSEL2;  //pin5--ANTSEL2  
    struct rksdmmc_gpio   ANTSEL3;  //pin6--ANTSEL3 
    struct rksdmmc_gpio   GPS_LAN;  //pin33--GPS_LAN
};

enum {
    WIFI_BCMWIFI,
    WIFI_RTKWIFI,
    WIFI_ESP8089,
    TYPE_MAX,
};

int rfkill_get_wifi_power_state(int *power, int *vref_ctrl_enable);
void *rockchip_mem_prealloc(int section, unsigned long size);
int rockchip_wifi_ref_voltage(int on);
int rockchip_wifi_power(int on);
int rockchip_wifi_set_carddetect(int val);
int rockchip_wifi_get_oob_irq(void);
int rockchip_wifi_reset(int on);
int rockchip_wifi_mac_addr(unsigned char *buf);
void *rockchip_wifi_country_code(char *ccode);
#endif
