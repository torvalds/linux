
#ifdef CONFIG_SCHED_TUNE

#ifdef CONFIG_CGROUP_SCHEDTUNE

int schedtune_cpu_boost(int cpu);

void schedtune_enqueue_task(struct task_struct *p, int cpu);
void schedtune_dequeue_task(struct task_struct *p, int cpu);

#else /* CONFIG_CGROUP_SCHEDTUNE */

#define schedtune_enqueue_task(task, cpu) do { } while (0)
#define schedtune_dequeue_task(task, cpu) do { } while (0)

#endif /* CONFIG_CGROUP_SCHEDTUNE */

#else /* CONFIG_SCHED_TUNE */

#define schedtune_enqueue_task(task, cpu) do { } while (0)
#define schedtune_dequeue_task(task, cpu) do { } while (0)

#endif /* CONFIG_SCHED_TUNE */
