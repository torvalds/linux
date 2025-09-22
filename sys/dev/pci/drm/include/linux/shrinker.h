/* Public domain. */

#ifndef _LINUX_SHRINKER_H
#define _LINUX_SHRINKER_H

struct shrink_control {
	u_long	nr_to_scan;
	u_long	nr_scanned;
};

struct shrinker {
	u_long	(*count_objects)(struct shrinker *, struct shrink_control *);
	u_long	(*scan_objects)(struct shrinker *, struct shrink_control *);
	long	batch;
	int	seeks;
	void	*private_data;
	TAILQ_ENTRY(shrinker) next;
};

#define SHRINK_STOP	~0UL

#define DEFAULT_SEEKS	2

static inline void
synchronize_shrinkers(void)
{
}

struct shrinker *shrinker_alloc(u_int, const char *, ...);
void shrinker_free(struct shrinker *);

void shrinker_register(struct shrinker *);

#endif
