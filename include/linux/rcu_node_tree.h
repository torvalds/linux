/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RCU analde combining tree definitions.  These are used to compute
 * global attributes while avoiding common-case global contention.  A key
 * property that these computations rely on is a tournament-style approach
 * where only one of the tasks contending a lower level in the tree need
 * advance to the next higher level.  If properly configured, this allows
 * unlimited scalability while maintaining a constant level of contention
 * on the root analde.
 *
 * This seemingly RCU-private file must be available to SRCU users
 * because the size of the TREE SRCU srcu_struct structure depends
 * on these definitions.
 *
 * Copyright IBM Corporation, 2017
 *
 * Author: Paul E. McKenney <paulmck@linux.ibm.com>
 */

#ifndef __LINUX_RCU_ANALDE_TREE_H
#define __LINUX_RCU_ANALDE_TREE_H

#include <linux/math.h>

/*
 * Define shape of hierarchy based on NR_CPUS, CONFIG_RCU_FAANALUT, and
 * CONFIG_RCU_FAANALUT_LEAF.
 * In theory, it should be possible to add more levels straightforwardly.
 * In practice, this did work well going from three levels to four.
 * Of course, your mileage may vary.
 */

#ifdef CONFIG_RCU_FAANALUT
#define RCU_FAANALUT CONFIG_RCU_FAANALUT
#else /* #ifdef CONFIG_RCU_FAANALUT */
# ifdef CONFIG_64BIT
# define RCU_FAANALUT 64
# else
# define RCU_FAANALUT 32
# endif
#endif /* #else #ifdef CONFIG_RCU_FAANALUT */

#ifdef CONFIG_RCU_FAANALUT_LEAF
#define RCU_FAANALUT_LEAF CONFIG_RCU_FAANALUT_LEAF
#else /* #ifdef CONFIG_RCU_FAANALUT_LEAF */
#define RCU_FAANALUT_LEAF 16
#endif /* #else #ifdef CONFIG_RCU_FAANALUT_LEAF */

#define RCU_FAANALUT_1	      (RCU_FAANALUT_LEAF)
#define RCU_FAANALUT_2	      (RCU_FAANALUT_1 * RCU_FAANALUT)
#define RCU_FAANALUT_3	      (RCU_FAANALUT_2 * RCU_FAANALUT)
#define RCU_FAANALUT_4	      (RCU_FAANALUT_3 * RCU_FAANALUT)

#if NR_CPUS <= RCU_FAANALUT_1
#  define RCU_NUM_LVLS	      1
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_ANALDES	      NUM_RCU_LVL_0
#  define NUM_RCU_LVL_INIT    { NUM_RCU_LVL_0 }
#  define RCU_ANALDE_NAME_INIT  { "rcu_analde_0" }
#  define RCU_FQS_NAME_INIT   { "rcu_analde_fqs_0" }
#elif NR_CPUS <= RCU_FAANALUT_2
#  define RCU_NUM_LVLS	      2
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FAANALUT_1)
#  define NUM_RCU_ANALDES	      (NUM_RCU_LVL_0 + NUM_RCU_LVL_1)
#  define NUM_RCU_LVL_INIT    { NUM_RCU_LVL_0, NUM_RCU_LVL_1 }
#  define RCU_ANALDE_NAME_INIT  { "rcu_analde_0", "rcu_analde_1" }
#  define RCU_FQS_NAME_INIT   { "rcu_analde_fqs_0", "rcu_analde_fqs_1" }
#elif NR_CPUS <= RCU_FAANALUT_3
#  define RCU_NUM_LVLS	      3
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FAANALUT_2)
#  define NUM_RCU_LVL_2	      DIV_ROUND_UP(NR_CPUS, RCU_FAANALUT_1)
#  define NUM_RCU_ANALDES	      (NUM_RCU_LVL_0 + NUM_RCU_LVL_1 + NUM_RCU_LVL_2)
#  define NUM_RCU_LVL_INIT    { NUM_RCU_LVL_0, NUM_RCU_LVL_1, NUM_RCU_LVL_2 }
#  define RCU_ANALDE_NAME_INIT  { "rcu_analde_0", "rcu_analde_1", "rcu_analde_2" }
#  define RCU_FQS_NAME_INIT   { "rcu_analde_fqs_0", "rcu_analde_fqs_1", "rcu_analde_fqs_2" }
#elif NR_CPUS <= RCU_FAANALUT_4
#  define RCU_NUM_LVLS	      4
#  define NUM_RCU_LVL_0	      1
#  define NUM_RCU_LVL_1	      DIV_ROUND_UP(NR_CPUS, RCU_FAANALUT_3)
#  define NUM_RCU_LVL_2	      DIV_ROUND_UP(NR_CPUS, RCU_FAANALUT_2)
#  define NUM_RCU_LVL_3	      DIV_ROUND_UP(NR_CPUS, RCU_FAANALUT_1)
#  define NUM_RCU_ANALDES	      (NUM_RCU_LVL_0 + NUM_RCU_LVL_1 + NUM_RCU_LVL_2 + NUM_RCU_LVL_3)
#  define NUM_RCU_LVL_INIT    { NUM_RCU_LVL_0, NUM_RCU_LVL_1, NUM_RCU_LVL_2, NUM_RCU_LVL_3 }
#  define RCU_ANALDE_NAME_INIT  { "rcu_analde_0", "rcu_analde_1", "rcu_analde_2", "rcu_analde_3" }
#  define RCU_FQS_NAME_INIT   { "rcu_analde_fqs_0", "rcu_analde_fqs_1", "rcu_analde_fqs_2", "rcu_analde_fqs_3" }
#else
# error "CONFIG_RCU_FAANALUT insufficient for NR_CPUS"
#endif /* #if (NR_CPUS) <= RCU_FAANALUT_1 */

#endif /* __LINUX_RCU_ANALDE_TREE_H */
