#ifndef _MPC_H_
#define _MPC_H_

#include <linux/types.h>
#include <linux/atm.h>
#include <linux/atmmpc.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include "mpoa_caches.h"

/* kernel -> mpc-daemon */
int msg_to_mpoad(struct k_message *msg, struct mpoa_client *mpc);

struct mpoa_client {
	struct mpoa_client *next;
	struct net_device *dev;      /* lec in question                     */
	int dev_num;                 /* e.g. 2 for lec2                     */
	int (*old_hard_start_xmit)(struct sk_buff *skb, struct net_device *dev);
	struct atm_vcc *mpoad_vcc;   /* control channel to mpoad            */
	uint8_t mps_ctrl_addr[ATM_ESA_LEN];  /* MPS control ATM address     */
	uint8_t our_ctrl_addr[ATM_ESA_LEN];  /* MPC's control ATM address   */

	rwlock_t ingress_lock;
	struct in_cache_ops *in_ops; /* ingress cache operations            */
	in_cache_entry *in_cache;    /* the ingress cache of this MPC       */

	rwlock_t egress_lock;
	struct eg_cache_ops *eg_ops; /* egress cache operations             */
	eg_cache_entry *eg_cache;    /* the egress  cache of this MPC       */

	uint8_t *mps_macs;           /* array of MPS MAC addresses, >=1     */
	int number_of_mps_macs;      /* number of the above MAC addresses   */
	struct mpc_parameters parameters;  /* parameters for this client    */
};


struct atm_mpoa_qos {
	struct atm_mpoa_qos *next;
	__be32 ipaddr;
	struct atm_qos qos;
};


/* MPOA QoS operations */
struct atm_mpoa_qos *atm_mpoa_add_qos(__be32 dst_ip, struct atm_qos *qos);
struct atm_mpoa_qos *atm_mpoa_search_qos(__be32 dst_ip);
int atm_mpoa_delete_qos(struct atm_mpoa_qos *qos);

/* Display QoS entries. This is for the procfs */
struct seq_file;
void atm_mpoa_disp_qos(struct seq_file *m);

#ifdef CONFIG_PROC_FS
int mpc_proc_init(void);
void mpc_proc_clean(void);
#else
#define mpc_proc_init() (0)
#define mpc_proc_clean() do { } while(0)
#endif

#endif /* _MPC_H_ */
