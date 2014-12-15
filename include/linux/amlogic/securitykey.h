#ifndef __SECURITYKEY__
#define __SECURITYKEY__

#include <linux/cdev.h>
typedef struct
{
    struct cdev cdev;
    unsigned int flags;
} securitykey_dev_t;


#define AML_KEYS_INSTALL_ID     _IO('f', 0x10)
#define AML_KEYS_INSTALL        _IO('f', 0x20)
#if 0
typedef struct {///exported structure
    char name[16];
    uint16_t size;
    uint16_t type;
    char key[1024];
}aml_install_key_t;
#endif
#define AML_KEYS_SET_VERSION    _IO('f', 0x30)
typedef struct aml_keybox_provider_s aml_keybox_provider_t;
struct aml_keybox_provider_s{
	char * name;
	int32_t flag;
	int32_t (* read)(aml_keybox_provider_t * provider,uint8_t * buf,int bytes,int flags);
	int32_t (* write)(aml_keybox_provider_t * provider,uint8_t * buf,int bytes);
	void * priv;
};

#ifdef CONFIG_SECURITYKEY
/* function: get_aml_key_kernel
 * key_name: key name
 * data: key data
 * ascii_flag:  0: ascii after merge bytes     1: asicii that don't merge bytes
 * return : >=0 ok,  <0 error
 * */
extern int get_aml_key_kernel(const char* key_name, unsigned char* data, int ascii_flag);
/* function: extenal_api_key_set_version
 * devvesion: nand3, emmc3,auto3
 * return : >= 0 ok, <0 error
 * */
extern int extenal_api_key_set_version(char *devvesion);
/* function :extenal_api_key_write
 * keyname: key data
 * dsize: key data size
 * hexascii_flag: ==0 merge byte2, !=0 no merge bytes
 * */
extern int extenal_api_key_write(char *keyname,char *keydata,int dsize,int hexascii_flag);
/* function :extenal_api_key_read
 * keyname: key data
 * hexascii_flag: ==0 merge byte2, !=0 no merge bytes
 * */
extern int extenal_api_key_read(char *keyname,char *keydata,int dsize,int hexascii_flag);
extern int32_t aml_keybox_provider_register(aml_keybox_provider_t * provider);
extern aml_keybox_provider_t * aml_keybox_provider_get(char * name);

/*
 * function name: extenal_api_key_query
 * keyname: key name
 * keystate:  0: key not exist, 1: key burned , other : reserve
 * return : <0: fail, >=0 ok
 * */
extern int extenal_api_key_query(char *keyname,unsigned int *keystate);

#else
static inline int get_aml_key_kernel(const char* key_name, unsigned char* data, int ascii_flag)
{
	return -EINVAL;
}
static inline int extenal_api_key_set_version(char *devvesion)
{
	return -EINVAL;
}
static inline int extenal_api_key_write(char *keyname,char *keydata,int dsize,int hexascii_flag)
{
	return -EINVAL;
}
static inline int extenal_api_key_read(char *keyname,char *keydata,int dsize,int hexascii_flag)
{
	return -EINVAL;
}
static inline int32_t aml_keybox_provider_register(aml_keybox_provider_t * provider)
{
	return -EINVAL;
}
static inline aml_keybox_provider_t * aml_keybox_provider_get(char * name)
{
	return NULL;
}
static inline int extenal_api_key_query(char *keyname,unsigned int *keystate)
{
	return -EINVAL;
}
#endif

#endif
