/*
 * $FreeBSD$
 */
extern void setmodulus(char *modx);

extern keystatus pk_setkey( uid_t, keybuf );
extern keystatus pk_encrypt( uid_t, char *, netobj *, des_block * );
extern keystatus pk_decrypt( uid_t, char *, netobj *, des_block * );
extern keystatus pk_netput( uid_t, key_netstarg * );
extern keystatus pk_netget( uid_t, key_netstarg * );
extern keystatus pk_get_conv_key( uid_t, keybuf, cryptkeyres * );
extern void pk_nodefaultkeys( void );

extern void crypt_prog_1( struct svc_req *, register SVCXPRT * );
extern void load_des( int, char * );

extern int (*_my_crypt)( char *, int, struct desparams * );
