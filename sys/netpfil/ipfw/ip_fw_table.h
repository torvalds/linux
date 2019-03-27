/*-
 * Copyright (c) 2002-2009 Luigi Rizzo, Universita` di Pisa
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _IPFW2_TABLE_H
#define _IPFW2_TABLE_H

/*
 * Internal constants and data structures used by ipfw tables
 * not meant to be exported outside the kernel.
 */
#ifdef _KERNEL

struct table_algo;
struct tables_config {
	struct namedobj_instance	*namehash;
	struct namedobj_instance	*valhash;
	uint32_t			val_size;
	uint32_t			algo_count;
	struct table_algo 		*algo[256];
	struct table_algo		*def_algo[IPFW_TABLE_MAXTYPE + 1];
	TAILQ_HEAD(op_state_l,op_state)	state_list;
};
#define	CHAIN_TO_TCFG(chain)	((struct tables_config *)(chain)->tblcfg)

struct table_info {
	table_lookup_t	*lookup;	/* Lookup function */
	void		*state;		/* Lookup radix/other structure */
	void		*xstate;	/* eXtended state */
	u_long		data;		/* Hints for given func */
};

struct table_value;
struct tentry_info {
	void		*paddr;
	struct table_value	*pvalue;
	void		*ptv;		/* Temporary field to hold obj	*/		
	uint8_t		masklen;	/* mask length			*/
	uint8_t		subtype;
	uint16_t	flags;		/* record flags			*/
	uint32_t	value;		/* value index			*/
};
#define	TEI_FLAGS_UPDATE	0x0001	/* Add or update rec if exists	*/
#define	TEI_FLAGS_UPDATED	0x0002	/* Entry has been updated	*/
#define	TEI_FLAGS_COMPAT	0x0004	/* Called from old ABI		*/
#define	TEI_FLAGS_DONTADD	0x0008	/* Do not create new rec	*/
#define	TEI_FLAGS_ADDED		0x0010	/* Entry was added		*/
#define	TEI_FLAGS_DELETED	0x0020	/* Entry was deleted		*/
#define	TEI_FLAGS_LIMIT		0x0040	/* Limit was hit		*/
#define	TEI_FLAGS_ERROR		0x0080	/* Unknown request error	*/
#define	TEI_FLAGS_NOTFOUND	0x0100	/* Entry was not found		*/
#define	TEI_FLAGS_EXISTS	0x0200	/* Entry already exists		*/

typedef int (ta_init)(struct ip_fw_chain *ch, void **ta_state,
    struct table_info *ti, char *data, uint8_t tflags);
typedef void (ta_destroy)(void *ta_state, struct table_info *ti);
typedef int (ta_prepare_add)(struct ip_fw_chain *ch, struct tentry_info *tei,
    void *ta_buf);
typedef int (ta_prepare_del)(struct ip_fw_chain *ch, struct tentry_info *tei,
    void *ta_buf);
typedef int (ta_add)(void *ta_state, struct table_info *ti,
    struct tentry_info *tei, void *ta_buf, uint32_t *pnum);
typedef int (ta_del)(void *ta_state, struct table_info *ti,
    struct tentry_info *tei, void *ta_buf, uint32_t *pnum);
typedef void (ta_flush_entry)(struct ip_fw_chain *ch, struct tentry_info *tei,
    void *ta_buf);

typedef int (ta_need_modify)(void *ta_state, struct table_info *ti,
    uint32_t count, uint64_t *pflags);
typedef int (ta_prepare_mod)(void *ta_buf, uint64_t *pflags);
typedef int (ta_fill_mod)(void *ta_state, struct table_info *ti,
    void *ta_buf, uint64_t *pflags);
typedef void (ta_modify)(void *ta_state, struct table_info *ti,
    void *ta_buf, uint64_t pflags);
typedef void (ta_flush_mod)(void *ta_buf);

typedef void (ta_change_ti)(void *ta_state, struct table_info *ti);
typedef void (ta_print_config)(void *ta_state, struct table_info *ti, char *buf,
    size_t bufsize);

typedef int ta_foreach_f(void *node, void *arg);
typedef void ta_foreach(void *ta_state, struct table_info *ti, ta_foreach_f *f,
  void *arg);
typedef int ta_dump_tentry(void *ta_state, struct table_info *ti, void *e,
    ipfw_obj_tentry *tent);
typedef int ta_find_tentry(void *ta_state, struct table_info *ti,
    ipfw_obj_tentry *tent);
typedef void ta_dump_tinfo(void *ta_state, struct table_info *ti, 
    ipfw_ta_tinfo *tinfo);
typedef uint32_t ta_get_count(void *ta_state, struct table_info *ti);

struct table_algo {
	char		name[16];
	uint32_t	idx;
	uint32_t	type;
	uint32_t	refcnt;
	uint32_t	flags;
	uint32_t	vlimit;
	size_t		ta_buf_size;
	ta_init		*init;
	ta_destroy	*destroy;
	ta_prepare_add	*prepare_add;
	ta_prepare_del	*prepare_del;
	ta_add		*add;
	ta_del		*del;
	ta_flush_entry	*flush_entry;
	ta_find_tentry	*find_tentry;
	ta_need_modify	*need_modify;
	ta_prepare_mod	*prepare_mod;
	ta_fill_mod	*fill_mod;
	ta_modify	*modify;
	ta_flush_mod	*flush_mod;
	ta_change_ti	*change_ti;
	ta_foreach	*foreach;
	ta_dump_tentry	*dump_tentry;
	ta_print_config	*print_config;
	ta_dump_tinfo	*dump_tinfo;
	ta_get_count	*get_count;
};
#define	TA_FLAG_DEFAULT		0x01	/* Algo is default for given type */
#define	TA_FLAG_READONLY	0x02	/* Algo does not support modifications*/
#define	TA_FLAG_EXTCOUNTER	0x04	/* Algo has external counter available*/

int ipfw_add_table_algo(struct ip_fw_chain *ch, struct table_algo *ta,
    size_t size, int *idx);
void ipfw_del_table_algo(struct ip_fw_chain *ch, int idx);

void ipfw_table_algo_init(struct ip_fw_chain *chain);
void ipfw_table_algo_destroy(struct ip_fw_chain *chain);

MALLOC_DECLARE(M_IPFW_TBL);
/* Exported to support legacy opcodes */
int add_table_entry(struct ip_fw_chain *ch, struct tid_info *ti,
    struct tentry_info *tei, uint8_t flags, uint32_t count);
int del_table_entry(struct ip_fw_chain *ch, struct tid_info *ti,
    struct tentry_info *tei, uint8_t flags, uint32_t count);
int flush_table(struct ip_fw_chain *ch, struct tid_info *ti);
void ipfw_import_table_value_legacy(uint32_t value, struct table_value *v);
uint32_t ipfw_export_table_value_legacy(struct table_value *v);
int ipfw_get_table_size(struct ip_fw_chain *ch, ip_fw3_opheader *op3,
    struct sockopt_data *sd);

/* ipfw_table_value.c functions */
struct table_config;
struct tableop_state;
void ipfw_table_value_init(struct ip_fw_chain *ch, int first);
void ipfw_table_value_destroy(struct ip_fw_chain *ch, int last);
int ipfw_link_table_values(struct ip_fw_chain *ch, struct tableop_state *ts);
void ipfw_garbage_table_values(struct ip_fw_chain *ch, struct table_config *tc,
    struct tentry_info *tei, uint32_t count, int rollback);
void ipfw_import_table_value_v1(ipfw_table_value *iv);
void ipfw_export_table_value_v1(struct table_value *v, ipfw_table_value *iv);
void ipfw_unref_table_values(struct ip_fw_chain *ch, struct table_config *tc,
    struct table_algo *ta, void *astate, struct table_info *ti);
void rollback_table_values(struct tableop_state *ts);

int ipfw_rewrite_table_uidx(struct ip_fw_chain *chain,
    struct rule_check_info *ci);
int ipfw_mark_table_kidx(struct ip_fw_chain *chain, struct ip_fw *rule,
    uint32_t *bmask);
int ipfw_export_table_ntlv(struct ip_fw_chain *ch, uint16_t kidx,
    struct sockopt_data *sd);
void ipfw_unref_rule_tables(struct ip_fw_chain *chain, struct ip_fw *rule);
struct namedobj_instance *ipfw_get_table_objhash(struct ip_fw_chain *ch);

/* utility functions  */
int ipfw_move_tables_sets(struct ip_fw_chain *ch, ipfw_range_tlv *rt,
    uint32_t new_set);
void ipfw_swap_tables_sets(struct ip_fw_chain *ch, uint32_t old_set,
    uint32_t new_set, int mv);
int ipfw_foreach_table_tentry(struct ip_fw_chain *ch, uint16_t kidx,
    ta_foreach_f f, void *arg);

/* internal functions */
void tc_ref(struct table_config *tc);
void tc_unref(struct table_config *tc);

struct op_state;
typedef void (op_rollback_f)(void *object, struct op_state *state);
struct op_state {
	TAILQ_ENTRY(op_state)	next;	/* chain link */
	op_rollback_f		*func;
};

struct tableop_state {
	struct op_state	opstate;
	struct ip_fw_chain *ch;
	struct table_config *tc;
	struct table_algo *ta;
	struct tentry_info *tei;
	uint32_t count;
	uint32_t vmask;
	int vshared;
	int modified;
};

void add_toperation_state(struct ip_fw_chain *ch, struct tableop_state *ts);
void del_toperation_state(struct ip_fw_chain *ch, struct tableop_state *ts);
void rollback_toperation_state(struct ip_fw_chain *ch, void *object);

/* Legacy interfaces */
int ipfw_count_table(struct ip_fw_chain *ch, struct tid_info *ti,
    uint32_t *cnt);
int ipfw_count_xtable(struct ip_fw_chain *ch, struct tid_info *ti,
    uint32_t *cnt);
int ipfw_dump_table_legacy(struct ip_fw_chain *ch, struct tid_info *ti,
    ipfw_table *tbl);


#endif /* _KERNEL */
#endif /* _IPFW2_TABLE_H */
