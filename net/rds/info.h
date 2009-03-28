#ifndef _RDS_INFO_H
#define _RDS_INFO_H

struct rds_info_lengths {
	unsigned int	nr;
	unsigned int	each;
};

struct rds_info_iterator;

/*
 * These functions must fill in the fields of @lens to reflect the size
 * of the available info source.  If the snapshot fits in @len then it
 * should be copied using @iter.  The caller will deduce if it was copied
 * or not by comparing the lengths.
 */
typedef void (*rds_info_func)(struct socket *sock, unsigned int len,
			      struct rds_info_iterator *iter,
			      struct rds_info_lengths *lens);

void rds_info_register_func(int optname, rds_info_func func);
void rds_info_deregister_func(int optname, rds_info_func func);
int rds_info_getsockopt(struct socket *sock, int optname, char __user *optval,
			int __user *optlen);
void rds_info_copy(struct rds_info_iterator *iter, void *data,
		   unsigned long bytes);
void rds_info_iter_unmap(struct rds_info_iterator *iter);


#endif
