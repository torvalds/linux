#ifndef __LINUX_KEY_INPUT_H
#define __LINUX_KEY_INPUT_H

#define PRINT_DBG
//debug
#ifdef PRINT_DBG
    #define print_dbg(format, arg...)   do { printk(KERN_INFO "[DEBUG]: FILE:%s:%d, FUNC:%s--- "format"\n",\
                                                     __FILE__,__LINE__,__func__,## arg);} \
                                         while (0)
#else
    #define print_dbg(format, arg...)   do {} while (0)
#endif

struct key_input_platform_data{
    int scan_period;
    int fuzz_time;
    int *key_code_list;
    int key_num;
    int (*scan_func)(void *data);
    int (*init_func)(void);
    int config;
};


#endif