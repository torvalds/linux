#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>

#include <nanoutil.h>
#include "wifi_engine.h"
#include "driverenv.h"
#include "registry.h"
#include "nrx_stream.h"

#if !defined(CONFIG_PREEMPT) && !defined(CONFIG_PREEMPT_VOLUNTARY)
#define RWLOCK spinlock_t
#define RWLOCK_INIT(X) spin_lock_init(X)
#define RWLOCK_RLOCK(X) spin_lock(X)
#define RWLOCK_WLOCK(X) spin_lock(X)
#define RWLOCK_RUNLOCK(X) spin_unlock(X)
#define RWLOCK_WUNLOCK(X) spin_unlock(X)
#define RWLOCK_RTRYLOCK(X) spin_trylock(X)
#define RWLOCK_WTRYLOCK(X) spin_trylock(X)
#else
#define RWLOCK struct semaphore 
#ifdef init_MUTEX
#define RWLOCK_INIT(X) init_MUTEX(X)
#else
#define RWLOCK_INIT(X) sema_init(X, 1) 
#endif
#define RWLOCK_RLOCK(X) down(X)
#define RWLOCK_WLOCK(X) down(X)
#define RWLOCK_RUNLOCK(X) up(X) 
#define RWLOCK_WUNLOCK(X) up(X)
#define RWLOCK_RTRYLOCK(X) !down_trylock(X)
#define RWLOCK_WTRYLOCK(X) !down_trylock(X) 
#endif

RWLOCK mac_lock;
extern rRegistry registry;

#define MAC_PREFIX	"\x84\x5D\xD7"	//Netcom MAC address prefix

static inline void make_filename(char *filename, size_t len, const char *name)
{
   const char *sep = "";
   if(*nrx_macpath == '\0' || nrx_macpath[strlen(nrx_macpath) - 1] != '/')
      sep = "/";
   snprintf(filename, len, "%s%s%s", nrx_macpath, sep, name);
}

static int mac_read_file(void *buf, const char *filename)
{
	struct nrx_stream *fd;
	int status;
	loff_t off, size;
	ssize_t n;

	if((status = nrx_stream_open_file(filename, O_RDONLY, 0600, &fd)) < 0)
		return status;
	size = nrx_stream_lseek(fd, 0, 2);
	if(size < 0) {
		KDEBUG(ERROR, "EXIT %d", (int)size);
		nrx_stream_close(fd);
		return size;
	}
	off = nrx_stream_lseek(fd, 0, 0);
	if(off < 0) {
		KDEBUG(ERROR, "EXIT %d", (int)off);
		nrx_stream_close(fd);
		return off;
	}

	n = nrx_stream_read(fd, buf, 6);
	nrx_stream_close(fd);
	return n;
}


static int mac_write_file(void *buf, ssize_t len, const char *filename)
{
	struct nrx_stream *fd;
	int status;
	loff_t off;
	ssize_t n;

	if((status = nrx_stream_open_file(filename, O_CREAT|O_RDWR, 0600, &fd)) < 0) {
		printk("Create file %s failed, please be sure directory %s is exist!\n", filename, nrx_macpath);
		return status;
	}

	off = nrx_stream_lseek(fd, 0, 0);
	if(off < 0) {
		KDEBUG(ERROR, "EXIT %d", (int)off);
		nrx_stream_close(fd);
		return off;
	}

	n = nrx_stream_write(fd, buf, len);
	nrx_stream_flush(fd);
	nrx_stream_close(fd);
	return n;
}


static inline int
macaddr_valid(uint8_t *data, size_t data_len)
{
   if(data == NULL || data_len != 6)
      return FALSE;
   
   if(data[0] & 0x1)
      return FALSE;

   if((data[0] | data[1] | data[2] | data[3] | data[4] | data[5]) == 0)
      return FALSE;

#if 1
   /* these addresses are common on cards, and we don't want to use
    * them */
   if(memcmp(data, "\x10\x20\x30\x40\x50", 5) == 0 &&
      (data[5] == 0x60 || data[5] == 0xbb))
      return FALSE;
#endif

   return TRUE;
}

#ifdef WINNER_SN2MAC
int sn_to_mac(char* mac)
{
	char mac_buf[16];
	const char sn[] = "\xAA\xAA\xAA";	// Instead of your chip ID
	
	memcpy(mac_buf, MAC_PREFIX, 3);
	memcpy(mac_buf+3, sn, 3);
	memcpy(mac, mac_buf, 6);
	
	return TRUE;
}
#endif

int get_mac_addr(char* mac)
{
	int status;
	char filename[128];
	char filebuf[128];
	RWLOCK_INIT(&mac_lock);
	
	RWLOCK_WLOCK(&mac_lock);
	memset(filebuf, 0, 128);
	//printk("%s : enter\n", __func__);
	// 1. if "mac.bin" exist ?
	make_filename(filename, sizeof(filename), "mac.bin");
	status = mac_read_file(filebuf, filename);
	if(status < 0) {
		//printk("Not find file: %s!\n", filename);
	}
	else {
		//printk("Find file: %s \n", filename);
		memcpy(&(registry.general.macAddress), filebuf, 6);
		goto _exit;
	}
	#if 0
	// 2. if mib file mac available ?
	if(macaddr_valid(mac, 6)) {
		printk("Find mac in mib file or EEPROM\n");
		memcpy(filebuf, mac, 6);
		goto _exit;
	}
	#endif

#ifdef WINNER_SN2MAC
	// 2. if mac.sn exist ?
	make_filename(filename, sizeof(filename), "mac.sn");
	status = mac_read_file(filebuf, filename);
	if(status < 0) {
		if(sn_to_mac(filebuf)==TRUE)
		{
			printk("sn_to_mac : %02X:%02X:%02X:%02X:%02X:%02X\n", filebuf[0], filebuf[1], filebuf[2], filebuf[3], filebuf[4], filebuf[5]);
			mac_write_file(filebuf, 6, filename);
			goto _exit;
		}
	}
	else {
		printk("Find file %s\n", filename);
		goto _exit;
	}
#endif

	// 3. if mac.random exist ?
	make_filename(filename, sizeof(filename), "mac.random");
	status = mac_read_file(filebuf, filename);
	if(status < 0) {
		//printk("Not find file: %s!\n", filename);
	}
	else {
		printk("Find file %s\n", filename);
		goto _exit;
	}
	// 4. mac.random not exist, generate it
	printk("[nano] found no mac, generate random one!\n");
	WiFiEngine_RandomMAC(0, filebuf, 6);
	memcpy(filebuf, MAC_PREFIX, strlen(MAC_PREFIX));

	mac_write_file(filebuf, 6, filename);
	
_exit:
	RWLOCK_WUNLOCK(&mac_lock);
	memcpy(mac, filebuf, 6);
	printk("[nano] mac: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return 0;
}
