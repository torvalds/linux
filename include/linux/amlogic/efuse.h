#ifndef __EFUSE_H
#define __EFUSE_H

#include <linux/ioctl.h>
#include <mach/efuse.h>
#include <linux/types.h>
#define EFUSE_ENCRYPT_DISABLE   _IO('f', 0x10)
#define EFUSE_ENCRYPT_ENABLE    _IO('f', 0x20)
#define EFUSE_ENCRYPT_RESET     _IO('f', 0x30)
#define EFUSE_INFO_GET				_IO('f', 0x40)

#ifdef __DEBUG
#define __D(fmt, args...) fprintf(stderr, "debug: " fmt, ## args)
#else
#define __D(fmt, args...)
#endif

#ifdef __ERROR
#define __E(fmt, args...) fprintf(stderr, "error: " fmt, ## args)
#else
#define __E(fmt, args...)
#endif



#define BCH_T           1
#define BCH_M           8
#define BCH_P_BYTES     30

#define DOUBLE_WORD_BYTES        4
#define EFUSE_IS_OPEN           (0x01)

#define EFUSE_NONE_ID			0
#define EFUSE_VERSION_ID		1
#define EFUSE_LICENCE_ID		2
#define EFUSE_MAC_ID				3
#define EFUSE_MAC_WIFI_ID	4
#define EFUSE_MAC_BT_ID		5
#define EFUSE_HDCP_ID			6
#define EFUSE_USID_ID				7
#define EFUSE_RSA_KEY_ID		8
#define EFUSE_CUSTOMER_ID		9
#define EFUSE_MACHINEID_ID		10
#define EFUSE_NANDEXTCMD_ID		11

int efuse_bch_enc(const char *ibuf, int isize, char *obuf, int reverse);
int efuse_bch_dec(const char *ibuf, int isize, char *obuf, int reverse);

struct efuse_platform_data {
	loff_t pos;
	size_t count;
	bool (*data_verify)(unsigned char *usid);
};

typedef struct efuseinfo_item{
	char title[40];	
	unsigned id;
	loff_t offset;    // write offset
	unsigned enc_len;
	unsigned data_len;			
	int bch_en;
	int bch_reverse;
} efuseinfo_item_t;


typedef struct efuseinfo{
	struct efuseinfo_item *efuseinfo_version;
	int size;
	int version;	
}efuseinfo_t;

typedef int (*pfn) (unsigned param, efuseinfo_item_t *info); 

#include <linux/cdev.h>

typedef struct 
{
    struct cdev cdev;
    unsigned int flags;
} efuse_dev_t;

#define EFUSE_READ_ONLY     


#include <linux/ioctl.h>
#ifdef CONFIG_EFUSE
extern int32_t __v3_read_hash(uint32_t id,char * buf);
extern int32_t __v3_write_hash(uint32_t id,char * buf);
#else
static int32_t __v3_read_hash(uint32_t id,char * buf)
{
	return -EINVAL;
}
static int32_t __v3_write_hash(uint32_t id,char * buf)
{
	return -EINVAL;
}
#endif

#ifdef CONFIG_EFUSE
/* function: efuse_read_intlItem
 * intl_item: item name, name is:[temperature]
 * buf:  output para
 * size: buf size
 * */
extern int efuse_read_intlItem(char *intl_item,char *buf,int size);
#else
int efuse_read_intlItem(char *intl_item,char *buf,int size)
{
	return -EINVAL;
}
#endif

#endif
