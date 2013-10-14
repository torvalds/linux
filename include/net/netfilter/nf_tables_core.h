#ifndef _NET_NF_TABLES_CORE_H
#define _NET_NF_TABLES_CORE_H

extern int nf_tables_core_module_init(void);
extern void nf_tables_core_module_exit(void);

extern int nft_immediate_module_init(void);
extern void nft_immediate_module_exit(void);

extern int nft_cmp_module_init(void);
extern void nft_cmp_module_exit(void);

extern int nft_lookup_module_init(void);
extern void nft_lookup_module_exit(void);

extern int nft_bitwise_module_init(void);
extern void nft_bitwise_module_exit(void);

extern int nft_byteorder_module_init(void);
extern void nft_byteorder_module_exit(void);

extern int nft_payload_module_init(void);
extern void nft_payload_module_exit(void);

#endif /* _NET_NF_TABLES_CORE_H */
