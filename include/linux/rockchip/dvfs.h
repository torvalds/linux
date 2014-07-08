/* arch/arm/mach-rk30/rk30_dvfs.h
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef _RK30_DVFS_H_
#define _RK30_DVFS_H_

#include <linux/device.h>
#include <linux/clk-provider.h>

struct dvfs_node;
typedef int (*dvfs_set_rate_callback)(struct dvfs_node *clk_dvfs_node, unsigned long rate);
typedef int (*clk_set_rate_callback)(struct clk *clk, unsigned long rate);

/**
 * struct vd_node:	To Store All Voltage Domains' info
 * @name:		Voltage Domain's Name
 * @regulator_name:	Voltage domain's regulator name
 * @cur_volt:		Voltage Domain's Current Voltage
 * @regulator:		Voltage Domain's regulator point
 * @node:		Point of he Voltage Domain List Node
 * @pd_list:		Head of Power Domain List Belongs to This Voltage Domain
 * @req_volt_list:	The list of clocks requests
 * @dvfs_mutex:		Lock
 * @vd_dvfs_target:	Callback function	
 */
 #define VD_VOL_LIST_CNT (200)
 #define VD_LIST_RELATION_L 0
 #define VD_LIST_RELATION_H 1

struct vd_node {
	const char		*name;
	const char		*regulator_name;
	int			volt_time_flag;// =0 ,is no initing checking ,>0 ,support,<0 not support
	int			mode_flag;// =0 ,is no initing checking ,>0 ,support,<0 not support;
	int			cur_volt;
	int			volt_set_flag;
	int			suspend_volt;
	struct regulator	*regulator;
	struct list_head	node;
	struct list_head	pd_list;
	struct mutex		mutex;
	dvfs_set_rate_callback	vd_dvfs_target;
	unsigned int 		n_voltages;
	int volt_list[VD_VOL_LIST_CNT];
	unsigned int		regu_mode;
};

/**
 * struct pd_node:	To Store All Power Domains' info
 * @name:		Power Domain's Name
 * @cur_volt:		Power Domain's Current Voltage
 * @pd_status:		Power Domain's status
 * @vd:			Voltage Domain the power domain belongs to
 * @pd_clk:		Look power domain as a clock
 * @node:		List node to Voltage Domain
 * @clk_list:		Head of Power Domain's Clocks List
 */
struct pd_node {
	const char		*name;
	int			cur_volt;
	unsigned char		pd_status;
	struct vd_node		*vd;
	struct list_head	node;
	struct list_head	clk_list;
	unsigned int		regu_mode;
};

/**
 * struct dvfs_node:	To Store All dvfs clocks' info
 * @name:		Dvfs clock's Name
 * @set_freq:		Dvfs clock's Current Frequency
 * @set_volt:		Dvfs clock's Current Voltage
 * @enable_dvfs:	Sign if DVFS clock enable
 * @clk:		System clk's point
 * @pd:			Power Domains dvfs clock belongs to
 * @vd:			Voltage Domains dvfs clock belongs to
 * @dvfs_nb:		Notify list
 * @dvfs_table:		Frequency and voltage table for dvfs
 * @clk_dvfs_target:	Callback function
 */
struct dvfs_node {
	struct device		dev;		//for opp
	const char		*name;
	int			set_freq;	//KHZ
	int			set_volt;	//MV
	int			enable_count;
	int			freq_limit_en;	//sign if use limit frequency
	unsigned int		min_rate;	//limit min frequency
	unsigned int		max_rate;	//limit max frequency
	unsigned long		last_set_rate;
	unsigned int		temp_channel;
	unsigned long		temp_limit_rate;
	struct clk 		*clk;
	struct pd_node		*pd;
	struct vd_node		*vd;
	struct list_head	node;
	struct notifier_block	*dvfs_nb;
	struct cpufreq_frequency_table	*dvfs_table;
	struct cpufreq_frequency_table	*per_temp_limit_table;
	struct cpufreq_frequency_table  *nor_temp_limit_table;
	clk_set_rate_callback 	clk_dvfs_target;
	struct cpufreq_frequency_table  *regu_mode_table;
	int			regu_mode_en;
	unsigned int		regu_mode;
};



#define DVFS_MHZ (1000*1000)
#define DVFS_KHZ (1000)

#define DVFS_V (1000*1000)
#define DVFS_MV (1000)
#if 0
#define DVFS_DBG(fmt, args...) printk(KERN_INFO "DVFS DBG:\t"fmt, ##args)
#else
#define DVFS_DBG(fmt, args...) {while(0);}
#endif

#define DVFS_ERR(fmt, args...) printk(KERN_ERR "DVFS ERR:\t"fmt, ##args)
#define DVFS_LOG(fmt, args...) printk(KERN_DEBUG "DVFS LOG:\t"fmt, ##args)
#define DVFS_WARNING(fmt, args...) printk(KERN_WARNING "DVFS WARNING:\t"fmt, ##args)

#define DVFS_SET_VOLT_FAILURE 	1
#define DVFS_SET_VOLT_SUCCESS	0

#define dvfs_regulator_get(dev,id) regulator_get((dev),(id))
#define dvfs_regulator_put(regu) regulator_put((regu))
#define dvfs_regulator_set_voltage(regu,min_uV,max_uV) regulator_set_voltage((regu),(min_uV),(max_uV))
#define dvfs_regulator_get_voltage(regu) regulator_get_voltage((regu))
#define dvfs_regulator_set_voltage_time(regu, old_uV, new_uV) regulator_set_voltage_time((regu), (old_uV), (new_uV))
#define dvfs_regulator_set_mode(regu, mode) regulator_set_mode((regu), (mode))
#define dvfs_regulator_get_mode(regu) regulator_get_mode((regu))
#define dvfs_regulator_list_voltage(regu,selector) regulator_list_voltage((regu),(selector))
#define dvfs_regulator_count_voltages(regu) regulator_count_voltages((regu))

#define clk_dvfs_node_get(a,b) clk_get((a),(b))
#define clk_dvfs_node_get_rate_kz(a) (clk_get_rate((a))/1000)
#define clk_dvfs_node_set_rate(a,b) clk_set_rate((a),(b))

typedef void (*avs_init_fn)(void);
typedef u8 (*avs_get_val_fn)(void);
struct avs_ctr_st {
	avs_init_fn		avs_init;
	avs_get_val_fn		avs_get_val;
};

#ifdef CONFIG_DVFS
struct dvfs_node *clk_get_dvfs_node(char *clk_name);
void clk_put_dvfs_node(struct dvfs_node *clk_dvfs_node);
unsigned long dvfs_clk_get_rate(struct dvfs_node *clk_dvfs_node);
unsigned long dvfs_clk_get_last_set_rate(struct dvfs_node *clk_dvfs_node);
unsigned long dvfs_clk_round_rate(struct dvfs_node *clk_dvfs_node, unsigned long rate);
int dvfs_clk_set_rate(struct dvfs_node *clk_dvfs_node, unsigned long rate);
int dvfs_clk_enable(struct dvfs_node *clk_dvfs_node);
void dvfs_clk_disable(struct dvfs_node *clk_dvfs_node);
int dvfs_clk_prepare_enable(struct dvfs_node *clk_dvfs_node);
void dvfs_clk_disable_unprepare(struct dvfs_node *clk_dvfs_node);
int dvfs_set_freq_volt_table(struct dvfs_node *clk_dvfs_node, struct cpufreq_frequency_table *table);
int dvfs_clk_register_set_rate_callback(struct dvfs_node *clk_dvfs_node, clk_set_rate_callback clk_dvfs_target);
int dvfs_clk_enable_limit(struct dvfs_node *clk_dvfs_node, unsigned int min_rate, unsigned max_rate);
int dvfs_clk_get_limit(struct dvfs_node *clk_dvfs_node, unsigned int *min_rate, unsigned int *max_rate) ;
int dvfs_clk_disable_limit(struct dvfs_node *clk_dvfs_node);
int clk_disable_dvfs(struct dvfs_node *clk_dvfs_node);
int clk_enable_dvfs(struct dvfs_node *clk_dvfs_node);
void dvfs_disable_temp_limit(void);
struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct dvfs_node *clk_dvfs_node);
int rk_regist_vd(struct vd_node *vd);
int rk_regist_pd(struct pd_node *pd);
int rk_regist_clk(struct dvfs_node *clk_dvfs_node);
struct regulator *dvfs_get_regulator(char *regulator_name);
int of_dvfs_init(void);

#else

static inline struct dvfs_node *clk_get_dvfs_node(char *clk_name){ return NULL; };
static inline void clk_put_dvfs_node(struct dvfs_node *clk_dvfs_node){ return; };
static inline unsigned long dvfs_clk_get_rate(struct dvfs_node *clk_dvfs_node){ return 0; };
static inline unsigned long dvfs_clk_get_last_set_rate(struct dvfs_node *clk_dvfs_node){ return 0; };
static inline unsigned long dvfs_clk_round_rate(struct dvfs_node *clk_dvfs_node, unsigned long rate) { return 0; };
static inline int dvfs_clk_set_rate(struct dvfs_node *clk_dvfs_node, unsigned long rate){ return 0; };
static inline int dvfs_clk_enable(struct dvfs_node *clk_dvfs_node){ return 0; };
static inline void dvfs_clk_disable(struct dvfs_node *clk_dvfs_node){ };
static inline int dvfs_clk_prepare_enable(struct dvfs_node *clk_dvfs_node){ return 0; };
static inline void dvfs_clk_disable_unprepare(struct dvfs_node *clk_dvfs_node){ };
static inline int dvfs_set_freq_volt_table(struct dvfs_node *clk_dvfs_node, struct cpufreq_frequency_table *table){ return 0; };
static inline int dvfs_clk_register_set_rate_callback(struct dvfs_node *clk_dvfs_node, clk_set_rate_callback clk_dvfs_target){ return 0; };
static inline int dvfs_clk_enable_limit(struct dvfs_node *clk_dvfs_node, unsigned int min_rate, unsigned max_rate){ return 0; };
static inline int dvfs_clk_get_limit(struct dvfs_node *clk_dvfs_node, unsigned int *min_rate, unsigned int *max_rate) { return 0; };
static inline int dvfs_clk_disable_limit(struct dvfs_node *clk_dvfs_node){ return 0; };
static inline int clk_disable_dvfs(struct dvfs_node *clk_dvfs_node){ return 0; };
static inline int clk_enable_dvfs(struct dvfs_node *clk_dvfs_node){ return 0; };
static inline void dvfs_disable_temp_limit(void) {};
static inline struct cpufreq_frequency_table *dvfs_get_freq_volt_table(struct dvfs_node *clk_dvfs_node){ return NULL; };
static inline int rk_regist_vd(struct vd_node *vd){ return 0; };
static inline int rk_regist_pd(struct pd_node *pd){ return 0; };
static inline int rk_regist_clk(struct dvfs_node *clk_dvfs_node){ return 0; };
static inline struct regulator *dvfs_get_regulator(char *regulator_name){ return NULL; };
static inline int of_dvfs_init(void){ return 0; };
#endif

#endif
