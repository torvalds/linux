/* $FreeBSD$ */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <sysexits.h>
#include <sys/uio.h>
#include <sys/namei.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/vnode.h>
/*----------------------------------*/
static u_int crc16_table[16] = { 
    0x0000, 0xCC01, 0xD801, 0x1400,
    0xF001, 0x3C00, 0x2800, 0xE401,
    0xA001, 0x6C00, 0x7800, 0xB401,
    0x5000, 0x9C01, 0x8801, 0x4400 
};

/* XXX Taken from sys/kern/vfs_cache.c */
struct	namecache {
	LIST_ENTRY(namecache) nc_hash;
	LIST_ENTRY(namecache) nc_src;
	TAILQ_ENTRY(namecache) nc_dst;
	struct	vnode *nc_dvp;
	struct	vnode *nc_vp;
	u_char	nc_flag;
	u_char	nc_nlen;
	char	nc_name[0];
};

static u_short
wlpsacrc(u_char *buf, u_int len)
{
    u_short     crc = 0;
    int         i, r1;
    
    for (i = 0; i < len; i++, buf++) {
        /* lower 4 bits */
        r1 = crc16_table[crc & 0xF];
        crc = (crc >> 4) & 0x0FFF;
        crc = crc ^ r1 ^ crc16_table[*buf & 0xF];
        
        /* upper 4 bits */
        r1 = crc16_table[crc & 0xF];
        crc = (crc >> 4) & 0x0FFF;
        crc = crc ^ r1 ^ crc16_table[(*buf >> 4) & 0xF];
    }
    return(crc);
}

/*----------------------------------*/
struct nlist nl[] = {
	{ "_nchash", 0},
	{ "_nchashtbl", 0},
	{ 0, 0 },
};

int histo[2047];
int histn[2047];
int *newbucket;

int
main(int argc, char **argv)
{
	int nchash, i, j, k, kn;
	int nb, p1, p2;
	u_long p;
	LIST_HEAD(nchashhead, namecache) *nchashtbl;
	struct namecache *nc;
	struct vnode vn;

	kvm_t *kvm = kvm_open(NULL, NULL, NULL, O_RDONLY, argv[0]);
	if (kvm == NULL)
		return(EX_OSERR);

	printf("kvm: %p\n", kvm);
	printf("kvm_nlist: %d\n", kvm_nlist(kvm, nl));
	kvm_read(kvm, nl[0].n_value, &nchash, sizeof nchash);
	nchash++;
	nchashtbl = malloc(nchash * sizeof *nchashtbl);
	nc = malloc(sizeof *nc + NAME_MAX);
	newbucket = malloc(nchash * sizeof (int));
	memset(newbucket, 0, nchash * sizeof (int));
	kvm_read(kvm, nl[1].n_value, &p, sizeof p);
	kvm_read(kvm, p, nchashtbl, nchash * sizeof *nchashtbl);
	for (i=0; i < nchash; i++) {
#if 0
		printf("%d\n", i);
#endif
		nb=0;
		p = (u_long)LIST_FIRST(nchashtbl+i);
		while (p) {
			nb++;
			kvm_read(kvm, p, nc, sizeof *nc + NAME_MAX);
			kvm_read(kvm, (u_long)nc->nc_dvp, &vn, sizeof vn);
			nc->nc_name[nc->nc_nlen] = '\0';
			for (j=k=kn=0;nc->nc_name[j];j++) {
				k+= nc->nc_name[j];
				kn <<= 1;
				kn+= nc->nc_name[j];
			}
			/*
			kn = k;
			*/
			kn = wlpsacrc(nc->nc_name,nc->nc_nlen);

			/* kn += (u_long)vn.v_data >> 8;  */
			/* kn += (u_long)nc->nc_dvp >> 7;    */
			kn += vn.v_id; 
			kn &= (nchash - 1);
			newbucket[kn]++;
#if 1
			printf("%4d  dvp %08x  hash %08x  vp %08x  id %08x  name <%s>\n",
				i,nc->nc_dvp, k, nc->nc_vp, vn.v_id, nc->nc_name);
#endif
			p = (u_long)LIST_NEXT(nc, nc_hash);
		}
		histo[nb]++;
	}
	for (i=0; i < nchash; i++) {
		histn[newbucket[i]]++;
	}
	p1=p2 = 0;
	for (i=0;i<30;i++) {
		p1 += histo[i] * i;
		p2 += histn[i] * i;
		if (histo[i] || histn[i])
			printf("H%02d %4d %4d / %4d %4d\n",i,histo[i], p1 , histn[i], p2);
	}
		
	return (0);
}

