
/*
 * RCU implementation internal declarations:
 */
extern struct rcu_state rcu_state;
DECLARE_PER_CPU(struct rcu_data, rcu_data);

extern struct rcu_state rcu_bh_state;
DECLARE_PER_CPU(struct rcu_data, rcu_bh_data);

