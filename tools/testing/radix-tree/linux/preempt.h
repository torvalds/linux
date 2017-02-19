extern int preempt_count;

#define preempt_disable()	uatomic_inc(&preempt_count)
#define preempt_enable()	uatomic_dec(&preempt_count)
