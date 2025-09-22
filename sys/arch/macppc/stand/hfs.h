/*	$OpenBSD: hfs.h,v 1.2 2015/03/14 20:52:41 miod Exp $	*/
/*	$NetBSD: hfs.h,v 1.1 2000/11/14 11:25:35 tsubai Exp $	*/

int hfs_open(char *, struct open_file *);
int hfs_close(struct open_file *);
int hfs_read(struct open_file *, void *, size_t, size_t *);
int hfs_write(struct open_file *, void *, size_t, size_t *);
off_t hfs_seek(struct open_file *, off_t, int);
int hfs_stat(struct open_file *, struct stat *);
int hfs_readdir(struct open_file *, char *);
