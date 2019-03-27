/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 * 
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 * 
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 * 
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 * 
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 * 
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 *
 * $FreeBSD$
 */

/*
 * Copyright (c) 1991, Sun Microsystems Inc.
 */

/*
 * This file contains the interfaces that are visible in the SunOS 5.x
 * implementation of NIS Plus.
 */

#ifndef	_RPCSVC_NISLIB_H
#define	_RPCSVC_NISLIB_H

/* From: #pragma ident	"@(#)nislib.h	1.16	94/05/03 SMI" */

#ifdef __cplusplus
extern "C" {
#endif

struct signature {
	int signature_len;
	char *signature_val;
};

extern void nis_freeresult(nis_result *);
extern nis_result * nis_lookup(nis_name, u_long);
extern nis_result * nis_list(nis_name, u_long,
	int (*)(nis_name, nis_object *, void *), void *);
extern nis_result * nis_add(nis_name, nis_object *);
extern nis_result * nis_remove(nis_name, nis_object *);
extern nis_result * nis_modify(nis_name, nis_object *);

extern nis_result * nis_add_entry(nis_name, nis_object *, u_long);
extern nis_result * nis_remove_entry(nis_name, nis_object *, u_long);
extern nis_result * nis_modify_entry(nis_name, nis_object *, u_long);
extern nis_result * nis_first_entry(nis_name);
extern nis_result * nis_next_entry(nis_name, netobj *);

extern nis_error nis_mkdir(nis_name, nis_server *);
extern nis_error nis_rmdir(nis_name, nis_server *);
extern name_pos nis_dir_cmp(nis_name, nis_name);

extern nis_name * nis_getnames(nis_name);
extern void nis_freenames(nis_name *);
extern nis_name nis_domain_of(nis_name);
extern nis_name nis_leaf_of(nis_name);
extern nis_name nis_leaf_of_r(const nis_name, char *, size_t);
extern nis_name nis_name_of(nis_name);
extern nis_name nis_local_group(void);
extern nis_name nis_local_directory(void);
extern nis_name nis_local_principal(void);
extern nis_name nis_local_host(void);

extern void nis_destroy_object(nis_object *);
extern nis_object * nis_clone_object(nis_object *, nis_object *);
extern void nis_print_object(nis_object *);

extern char * nis_sperrno(nis_error);
extern void nis_perror(nis_error, char *);
extern char * nis_sperror(nis_error, char *);
extern void nis_lerror(nis_error, char *);

extern void nis_print_group_entry(nis_name);
extern bool_t nis_ismember(nis_name, nis_name);
extern nis_error nis_creategroup(nis_name, u_long);
extern nis_error nis_destroygroup(nis_name);
extern nis_error nis_addmember(nis_name, nis_name);
extern nis_error nis_removemember(nis_name, nis_name);
extern nis_error nis_verifygroup(nis_name);

extern void nis_freeservlist(nis_server **);
extern nis_server ** nis_getservlist(nis_name);
extern nis_error nis_stats(nis_server *, nis_tag *, int, nis_tag **);
extern nis_error nis_servstate(nis_server *, nis_tag *, int, nis_tag **);
extern void nis_freetags(nis_tag *, int);

extern nis_result * nis_checkpoint(nis_name);
extern void nis_ping(nis_name, u_long, nis_object *);

/*
 * XXX: PLEASE NOTE THAT THE FOLLOWING FUNCTIONS ARE INTERNAL
 * TO NIS+ AND SHOULD NOT BE USED BY ANY APPLICATION PROGRAM.
 * THEIR SEMANTICS AND/OR SIGNATURE CAN CHANGE WITHOUT NOTICE.
 * SO, PLEASE DO NOT USE THEM.  YOU ARE WARNED!!!!
 */

extern char ** __break_name(nis_name, int *);
extern int __name_distance(char **, char **);
extern nis_result * nis_make_error(nis_error, u_long, u_long, u_long, u_long);
extern nis_attr * __cvt2attr(int *, char **);
extern void nis_free_request(ib_request *);
extern nis_error nis_get_request(nis_name, nis_object *, netobj*, ib_request*);
extern nis_object * nis_read_obj(char *);
extern int nis_write_obj(char *, nis_object *);
extern int nis_in_table(nis_name, NIS_HASH_TABLE *, int *);
extern int nis_insert_item(NIS_HASH_ITEM *, NIS_HASH_TABLE *);
extern NIS_HASH_ITEM * nis_find_item(nis_name, NIS_HASH_TABLE *);
extern NIS_HASH_ITEM * nis_remove_item(nis_name, NIS_HASH_TABLE *);
extern void nis_insert_name(nis_name, NIS_HASH_TABLE *);
extern void nis_remove_name(nis_name, NIS_HASH_TABLE *);
extern CLIENT * nis_make_rpchandle(nis_server *, int, u_long, u_long, u_long,
								int, int);
extern void * nis_get_static_storage(struct nis_sdata *, u_long, u_long);
extern char * nis_data(char *);
extern void nis_print_rights(u_long);
extern void nis_print_directory(directory_obj *);
extern void nis_print_group(group_obj *);
extern void nis_print_table(table_obj *);
extern void nis_print_link(link_obj *);
extern void nis_print_entry(entry_obj *);
extern nis_object * nis_get_object(char *, char *, char *, u_long, u_long,
								    zotypes);
extern nis_server * __nis_init_callback(CLIENT *,
    int (*)(nis_name, nis_object *, void *), void *);
extern int nis_getdtblsize(void);
extern int __nis_run_callback(netobj *, u_long, struct timeval *, CLIENT *);

extern log_result *nis_dumplog(nis_server *, nis_name, u_long);
extern log_result *nis_dump(nis_server *, nis_name,
    int (*)(nis_name, nis_object *, void *));

extern bool_t __do_ismember(nis_name, nis_name,
    nis_result *(*)(nis_name, u_long));
extern nis_name __nis_map_group(nis_name);
extern nis_name __nis_map_group_r(nis_name, char*, size_t);

extern nis_error __nis_CacheBind(char *, directory_obj *);
extern nis_error __nis_CacheSearch(char *, directory_obj *);
extern bool_t __nis_CacheRemoveEntry(directory_obj *);
extern void __nis_CacheRestart(void);
extern void __nis_CachePrint(void);
extern void __nis_CacheDumpStatistics(void);
extern bool_t writeColdStartFile(directory_obj *);

extern CLIENT * __get_ti_clnt(char *, CLIENT *, char **, pid_t *);
extern int __strcmp_case_insens(char *, char *);
extern int __strncmp_case_insens(char *, char *);

extern fd_result * nis_finddirectory(directory_obj *, nis_name);
extern int __start_clock(int);
extern u_long __stop_clock(int);

/*
 * This particular function is part of the FreeBSD NIS+ implementation
 * only. Ideally it should be somewhere else, but it is used by both
 * rpc.nisd and nis_cachemgr, and there aren't that many headers common
 * to both programs.
 */

extern struct signature *__nis_calculate_encrypted_cksum(unsigned char *, unsigned int, char *, int);

#define	NUL '\0'

#ifdef	__cplusplus
}
#endif

#endif	/* _RPCSVC_NISLIB_H */
