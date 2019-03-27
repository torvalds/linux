/*-
 * Copyright (c) 2006, Andrea Bittau <a.bittau@cs.ucl.ac.uk>
 * All rights reserved.
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211.h>
#include <net/ethernet.h>
#include <net80211/ieee80211_radiotap.h>
#include <sys/endian.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <curses.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>

//int hopfreq = 3*1000; // ms
int hopfreq = 500; // ms
int sig_reset = 1*1000; // ms


int ioctl_s = -1;
int bpf_s = -1;

struct chan_info {
	int locked;
	int chan;
	struct ieee80211req ireq;
	struct timeval last_hop;
} chaninfo;


#define CRYPT_NONE		0
#define CRYPT_WEP		1
#define CRYPT_WPA1		2
#define CRYPT_WPA		3
#define CRYPT_WPA1_TKIP		4
#define CRYPT_WPA1_TKIP_PSK	5
#define CRYPT_WPA1_CCMP		6
#define CRYPT_WPA1_CCMP_PSK	7
#define CRYPT_80211i		8
#define CRYPT_80211i_TKIP	9
#define CRYPT_80211i_TKIP_PSK	10

struct node_info {
	unsigned char mac[6];
	int signal;
	int noise;
	int max;
	unsigned char ssid[256];
	int chan;
	int wep;
	int pos;
	int ap;

	struct timeval seen;

	struct node_info* prev;
	struct node_info* next;
} *nodes = 0;

void clean_crap() {
	struct node_info* next;

	if (ioctl_s != -1)
		close(ioctl_s);
	if (bpf_s != -1)
		close(bpf_s);

	while (nodes) {
		next = nodes->next;
		free(nodes);
		nodes = next;
	}
}

char* mac2str(unsigned char* mac) {
        static char ret[6*3];

        sprintf(ret, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        return ret;
}

char* wep2str(int w) {
	char* wep = 0;
	static char res[14];

	switch (w) {
	case CRYPT_NONE:
		wep = "";
		break;

	case CRYPT_WEP:
		wep = "WEP";
		break;
		
	case CRYPT_WPA1:
		wep = "WPA1";
		break;
	
	case CRYPT_WPA:
		wep = "WPA?";
		break;

	case CRYPT_WPA1_TKIP:
		wep = "WPA1-TKIP";
		break;
	
	case CRYPT_WPA1_TKIP_PSK:
		wep = "WPA1-TKIP-PSK";
		break;

	case CRYPT_WPA1_CCMP:
		wep = "WPA1-CCMP";
		break;

	case CRYPT_WPA1_CCMP_PSK:
		wep = "WPA1-CCMP-PSK";
		break;

	case CRYPT_80211i:
		wep = "i";
		break;

	case CRYPT_80211i_TKIP:
		wep = "11i-TKIP";
		break;
	
	case CRYPT_80211i_TKIP_PSK:
		wep = "11i-TKIP-PSK";
		break;

	default:
		wep = "FIXME!";
		break;
	}

	memset(res, ' ', sizeof(res));
	assert(strlen(wep) < sizeof(res));
	memcpy(res, wep, strlen(wep));
	res[sizeof(res)-1] = 0;
	return res;
}

char* ssid2str(struct node_info* node) {
	static char res[24];

	memset(res, ' ', sizeof(res));
	res[0] = '[';
	strcpy(&res[sizeof(res)-2], "]");

	if (node->ap) {
		int left = sizeof(res) - 3;

		if (strlen(node->ssid) < left)
			left = strlen(node->ssid);
		memcpy(&res[1], node->ssid, left);
	}	
	else {
		memcpy(&res[1], "<client>", 8);
	}
	return res;
}

void save_state() {
	FILE* f;
	struct node_info* node = nodes;

	f = fopen("stumbler.log", "w");
	if (!f) {
		perror("fopen()");
		exit(1);
	}	

	while (node) {
		struct tm* t;
		char tim[16];

		t = localtime( (time_t*) &node->seen.tv_sec);
		if (!t) {
			perror("localtime()");
			exit(1);
		}
		tim[0] = 0;
		strftime(tim, sizeof(tim), "%H:%M:%S", t);
	
		fprintf(f, "%s %s %s %2d %s 0x%.2x\n", tim,
			mac2str(node->mac), wep2str(node->wep),
			node->chan, ssid2str(node), node->max);

		node = node->next;	
	}

	fclose(f);
}

void cleanup(int x) {
	endwin();
	clean_crap();
	exit(0);
}

void die(int p, char* msg) {
	endwin();
	if (p)
		perror(msg);
	else
		printf("%s\n", msg);
	clean_crap();	
	exit(1);
}

void display_chan() {
	int x, y;
	char tmp[3];

	x = COLS - 2;
	y = LINES - 1;

	snprintf(tmp, sizeof(tmp), "%.2d", chaninfo.chan);
	mvaddstr(y, x, tmp);
	refresh();
}

void set_chan(int c) {
        chaninfo.ireq.i_val = c;

        if (ioctl(ioctl_s, SIOCS80211, &chaninfo.ireq) == -1)
                die(1, "ioctl(SIOCS80211) [chan]");
        
	chaninfo.chan = c;

	if (gettimeofday(&chaninfo.last_hop, NULL) == -1)
		die(1, "gettimeofday()");

	display_chan();
}

void setup_if(char *dev) {
        struct ifreq ifr;
        unsigned int flags;

        // set chan
        memset(&chaninfo.ireq, 0, sizeof(chaninfo.ireq));
        strcpy(chaninfo.ireq.i_name, dev);
        chaninfo.ireq.i_type = IEEE80211_IOC_CHANNEL;

        set_chan(1);

        // set iface up and promisc
        memset(&ifr, 0, sizeof(ifr));
        strcpy(ifr.ifr_name, dev);
        if (ioctl(ioctl_s, SIOCGIFFLAGS, &ifr) == -1)
                die(1, "ioctl(SIOCGIFFLAGS)");
        
        flags = (ifr.ifr_flags & 0xffff) | (ifr.ifr_flagshigh << 16);
        flags |= IFF_UP | IFF_PPROMISC;
        
        memset(&ifr, 0, sizeof(ifr));
        strcpy(ifr.ifr_name, dev);
        ifr.ifr_flags = flags & 0xffff;
        ifr.ifr_flagshigh = flags >> 16;
        if (ioctl(ioctl_s, SIOCSIFFLAGS, &ifr) == -1)
                die(1, "ioctl(SIOCSIFFLAGS)");
}

void open_bpf(char *dev, int dlt) {
        int i;
        char buf[64];
        int fd = -1;
        struct ifreq ifr;

        for(i = 0;i < 16; i++) {
                sprintf(buf, "/dev/bpf%d", i);

                fd = open(buf, O_RDWR);
                if(fd < 0) {
                        if(errno != EBUSY)
				die(1,"can't open /dev/bpf");
                        continue;
                }
                else
                        break;
        }

        if(fd < 0)
                die(1, "can't open /dev/bpf");

        strncpy(ifr.ifr_name, dev, sizeof(ifr.ifr_name)-1);
        ifr.ifr_name[sizeof(ifr.ifr_name)-1] = 0;

        if(ioctl(fd, BIOCSETIF, &ifr) < 0)
                die(1, "ioctl(BIOCSETIF)");

        if (ioctl(fd, BIOCSDLT, &dlt) < 0)
                die(1, "ioctl(BIOCSDLT)");

        i = 1;
        if(ioctl(fd, BIOCIMMEDIATE, &i) < 0)
                die(1, "ioctl(BIOCIMMEDIATE)");

	bpf_s = fd;
}

void user_input() {
	static char chan[3];
	static int pos = 0;
	int c;

	c = getch();

	switch (c) {
		case 'w':
			save_state();
			break;

		case 'q':
			cleanup(0);
			break;

		case 'c':
			chaninfo.locked = !chaninfo.locked;
			break;

		case ERR:
			die(0, "getch()");
			break;

		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			chan[pos++] = c;
			if (pos == 2) {
				int ch = atoi(chan);
				if (ch <= 11 && ch >= 1) {
					set_chan(atoi(chan));
					chaninfo.locked = 1;
				}	
				pos = 0;
			}	
			break;

		default:
			pos = 0;
			break;
	}		
}

void display_node(struct node_info* node) {
	int x = 0;
	int y = 0;
	int i;
	char chan[3];
	char* ssid = 0;
	int sig, max, left, noise;
	char* wep = 0;

	y = node->pos;
	if (y == -1) // offscreen
		return;

	assert(y < LINES);

	// MAC
	mvaddstr(y, x, mac2str(node->mac));
	x += 6*3;

	// WEP
	wep = wep2str(node->wep);
	assert(wep);
	mvaddstr(y, x, wep);
	x += strlen(wep);
	x++;	

	// CHAN
	sprintf(chan, "%.2d", node->chan);
	mvaddstr(y, x, chan);
	x += 3;

	// ssid
	ssid = ssid2str(node);
	assert(ssid);
	mvaddstr(y, x, ssid);
	x += strlen(ssid);
	x++;

	left = COLS - x - 1;

	sig = (int)  ( ((double)node->signal)*left/100.0 );
	noise=(int)  ( ((double)node->noise)*left/100.0 );
	max = (int)  ( ((double)node->max)*left/100.0 );

	// SIGNAL BAR
	for (i = 0; i < noise; i++)
		mvaddch(y, x++, 'N');

	for (; i < sig; i++)
		mvaddch(y,x++, 'X');

	for (; i < max; i++)
		mvaddch(y,x++, ' ');
	mvaddch(y,x++, '|');

	for (; x < COLS-1; x++)
		mvaddch(y, x, ' ');

	assert (x <= COLS);
}

void update_node(struct node_info* data) {
	struct node_info* node;
	int sort = 0;

	assert(data->signal <= 100);

	node = nodes;

	// first time [virgin]
	if (!node) {
		node = (struct node_info*) malloc(sizeof(struct node_info));
		if (!node)
			die(1, "malloc()");

		memset(node, 0, sizeof(*node));
		memcpy(node->mac, data->mac, 6);
		nodes = node;
	}

	while (node) {
		// found it
		if (memcmp(node->mac, data->mac, 6) == 0)
			break;

		// end of chain
		if (!node->next) {
			node->next = (struct node_info*) 
				      malloc(sizeof(struct node_info));
			if (!node->next)
				die(1, "malloc()");
			
			memset(node->next, 0, sizeof(*node->next));
			memcpy(node->next->mac, data->mac, 6);
			node->next->prev = node;
			node->next->pos = node->pos+1;

			node = node->next;
			if (node->pos == LINES)
				sort = 1;
			break;
		}

		node = node->next;
	}
	assert(node);

	// too many nodes for screen
	if (sort) {
		struct node_info* ni = nodes;

		while (ni) {
			if (ni->pos != -1)
				ni->pos--;

			display_node(ni);	
			ni = ni->next;	
		}
	}
	
	node->signal = data->signal;
	if (data->signal > node->max)
		node->max = data->signal;
	
	if (gettimeofday(&node->seen, NULL) == -1)
		die(1, "gettimeofday()");

	if (data->ssid[0] != 0)
		strcpy(node->ssid, data->ssid);
	if (data->chan != -1)
		node->chan = data->chan;
	if (data->wep != -1) {
		// XXX LAME --- won't detect if AP changes WEP mode in
		// beacons...
		if (node->wep != CRYPT_WEP && 
		    node->wep != CRYPT_NONE &&
		    data->wep == CRYPT_WEP) {
		}
		else
			node->wep = data->wep;
	}	
	if (data->ap != -1)	
		node->ap = data->ap;

	display_node(node);
	refresh();
}

void get_beacon_info(unsigned char* data, int rd, 
		     struct node_info* node) {

	int blen = 8 + 2 + 2;
	
	strcpy(node->ssid, "<hidden>");
	node->chan = 0;
	node->wep = CRYPT_NONE;

	assert(rd >= blen);

	if (IEEE80211_BEACON_CAPABILITY(data) & IEEE80211_CAPINFO_PRIVACY)
		node->wep = CRYPT_WEP;

	data += blen;
	rd -= blen;

	while (rd > 2) {
                int eid, elen;

                eid = *data;
                data++;
                elen = *data;
                data++;
                rd -= 2;

		// short!
                if (rd < elen) {
                        return;
                }

                // ssid
                if (eid == 0) {
			if (elen == 1 && data[0] == 0) {
			// hidden
			}
			else {
                        	memcpy(node->ssid, data, elen);
                        	node->ssid[elen] = 0;
			}	
                }
                // chan
                else if(eid == 3) {
			// weird chan!
                        if( elen != 1) 
				goto next;

                        node->chan = *data;
                }
		// WPA 
		else if (eid == 221 && node->wep == CRYPT_WEP) {
			struct ieee80211_ie_wpa* wpa;

			wpa = (struct ieee80211_ie_wpa*) data;
			if (elen < 6)
				goto next;
			
			if (!memcmp(wpa->wpa_oui, "\x00\x50\xf2", 3)) {
			//	node->wep = CRYPT_WPA;
			}	
			else
				goto next;

			if (wpa->wpa_type == WPA_OUI_TYPE &&
			    le16toh(wpa->wpa_version) == WPA_VERSION) {
			    	int cipher, auth;
				unsigned char* ptr;
				
				node->wep = CRYPT_WPA1;
				
				if (elen < 12)
					goto next;

				cipher = ((unsigned char*) wpa->wpa_mcipher)[3];

				ptr = (unsigned char*)wpa + 12 + 
				      4 * le16toh(wpa->wpa_uciphercnt);
				
				if (elen < (ptr - data + 6))
					goto next;

				if ( *((unsigned short*) ptr) == 0)
					goto next;

				ptr += 2 + 3;
				auth = *ptr;

				if (cipher == WPA_CSE_TKIP) {
					node->wep = CRYPT_WPA1_TKIP;
					
					if (auth == WPA_ASE_8021X_PSK)
						node->wep = CRYPT_WPA1_TKIP_PSK;
				}

				if (cipher == WPA_CSE_CCMP) {
					node->wep = CRYPT_WPA1_CCMP;
					
					if (auth == WPA_ASE_8021X_PSK)
						node->wep = CRYPT_WPA1_CCMP_PSK;
				}
			}
		}
		else if (eid == 48 && node->wep == CRYPT_WEP) {
			unsigned char* ptr;

			// XXX no bounds checking
			ptr = data;

			if (ptr[0] == 1 && ptr[1] == 0) {
				unsigned short* count;
				int cipher = 0;

				ptr += 2;
				node->wep = CRYPT_80211i;

				if (!memcmp(ptr, "\x00\x0f\xac\x02", 4)) {
					node->wep = CRYPT_80211i_TKIP;
					cipher = 1;
				}

				ptr += 4;
				count = (unsigned short*) ptr;
				ptr +=2 + *count*4;

				count = (unsigned short*) ptr;
				if (*count) {
					ptr += 2;

					if (!memcmp(ptr,"\x00\x0f\xac\x02", 4)) {
						if (cipher)
							node->wep = CRYPT_80211i_TKIP_PSK;
					}
				}
			}
		}

next:
                data += elen;
                rd -= elen;
	}
}

int get_packet_info(struct ieee80211_frame* wh, 
		     unsigned char* body, int bodylen,
		     struct node_info* node) {
	
	int type, stype;

	node->chan = chaninfo.chan;
	node->wep = -1;
	node->ssid[0] = 0;
	node->ap = -1;
	
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	
	if (type == IEEE80211_FC0_TYPE_CTL)
		return 0;
#if 0
	if (wh->i_addr2[0] != 0) {
		mvprintw(30,30,"%s %x",mac2str(wh->i_addr2), wh->i_fc[0]);
	}	
#endif

	stype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	
	if (type == IEEE80211_FC0_TYPE_MGT &&
	    stype == IEEE80211_FC0_SUBTYPE_BEACON) {
		get_beacon_info(body, bodylen, node);
		node->ap = 1;
	}	

	else if (type == IEEE80211_FC0_TYPE_DATA &&
	    stype == IEEE80211_FC0_SUBTYPE_DATA) {
	
		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
			unsigned char* iv;
			
			node->wep = CRYPT_WEP;

			iv = body;
			iv += 3;

			// extended IV?
			if (*iv & (1 << 1)) {
#if 0				
				node->wep = CRYPT_WPA;
				mvprintw(20,20, "shei");
				exit(1);
#endif				
			}
		}	
	
		if (wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS)
			node->ap = 1;
		else
			node->ap = 0;
	}    
	
	memcpy(node->mac, wh->i_addr2, 6);
	return 1;	
}		     

void radiotap(unsigned char* data, int rd) {
	struct ieee80211_radiotap_header* rth;
	struct ieee80211_frame* wh;
	char* body;
	struct node_info node;
	int8_t signal_dbm, noise_dbm;
	uint8_t signal_db, noise_db;
	int dbm = 0;
	int signal = 0;
	int i;

	rd -= 4; // 802.11 CRC

	// radiotap
	rth = (struct ieee80211_radiotap_header*) data;

	// 802.11
	wh = (struct ieee80211_frame*)
	     ((char*)rth + rth->it_len);
        rd -= rth->it_len;

	assert (rd >= 0);

	// body
	body = (char*) wh + sizeof(*wh);
	rd -= sizeof(*wh);

	if (!get_packet_info(wh, body, rd, &node))
		return;

	// signal and noise
	body = (char*) rth + sizeof(*rth);
	signal_dbm = noise_dbm = signal_db = noise_db = 0;

	for (i = IEEE80211_RADIOTAP_TSFT; i <= IEEE80211_RADIOTAP_EXT; i++) {
		if (!(rth->it_present & (1 << i)))
			continue;
		
		switch (i) {
		case IEEE80211_RADIOTAP_TSFT:
			body += sizeof(uint64_t);
			break;
		
		case IEEE80211_RADIOTAP_FLAGS:
		case IEEE80211_RADIOTAP_RATE:
			body += sizeof(uint8_t);
			break;
		
		case IEEE80211_RADIOTAP_CHANNEL:
			body += sizeof(uint16_t)*2;
			break;
		
		case IEEE80211_RADIOTAP_FHSS:
			body += sizeof(uint16_t);
			break;
		
		case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
			signal_dbm = *body;
			body++;
			dbm = 1;
			break;
		
		case IEEE80211_RADIOTAP_DBM_ANTNOISE:
			noise_dbm = *body;
			body++;
			break;
		
		case IEEE80211_RADIOTAP_DB_ANTSIGNAL:
			signal_db = *((unsigned char*)body);
			body++;
			break;

		case IEEE80211_RADIOTAP_DB_ANTNOISE:
			noise_db = *((unsigned char*)body);
			body++;
			break;
		
		case IEEE80211_RADIOTAP_EXT:
			abort();
			break;
		}
	}
	if (dbm) {
		signal = signal_dbm - noise_dbm;
	}
	else {
		signal = signal_db - noise_db;
	}
	if (signal < 0)
		signal = 0;

	node.signal = signal;
#if 0
	if (node.signal > 100 || node.signal < 0) {
		mvprintw(25,25, "sig=%d", node.signal);
	}	
#else		
	assert (node.signal <= 100 && node.signal >= 0);
#endif

	update_node(&node);
}

void bpf_input() {
	static unsigned char buf[4096];
	int rd;
	struct bpf_hdr* bpfh;
	unsigned char* data;

	rd = read(bpf_s, buf, sizeof(buf));
	if (rd == -1)
		die(1,"read()");
	
	bpfh = (struct bpf_hdr*) buf;
	rd -= bpfh->bh_hdrlen;

	if (rd != bpfh->bh_caplen) {
		assert( rd > bpfh->bh_caplen);
		rd = bpfh->bh_caplen;
	}

	data = (unsigned char*) bpfh + bpfh->bh_hdrlen;
	radiotap(data, rd);
}

unsigned long elapsed_ms(struct timeval* now, struct timeval* prev) {
	unsigned long elapsed = 0;

	if (now->tv_sec > prev->tv_sec)
		elapsed = 1000*1000 - prev->tv_usec +
			  now->tv_usec;
	else {
		assert(now->tv_sec == prev->tv_sec);
		elapsed = now->tv_usec - prev->tv_usec;
	}	
	elapsed /= 1000; //ms

	elapsed += (now->tv_sec - prev->tv_sec)*1000;
	return elapsed;
}

void chanhop(struct timeval* tv) {
	unsigned long elapsed = 0;

	if (gettimeofday(tv, NULL) == -1)
		die(1, "gettimeofday()");


	elapsed = elapsed_ms(tv, &chaninfo.last_hop);

	// need to chan hop
	if (elapsed >= hopfreq) {
		int c;

		c = chaninfo.chan + 1;

		if (c > 11)
			c = 1;

		set_chan(c);

		elapsed = hopfreq;
	} 
	// how much can we sleep?
	else {
		elapsed = hopfreq - elapsed;
	}

	// ok calculate sleeping time...
	tv->tv_sec = elapsed/1000;
	tv->tv_usec = (elapsed - tv->tv_sec*1000)*1000;
}

void check_seen(struct timeval* tv) {
	unsigned long elapsed  = 0;
	struct timeval now;
	int need_refresh = 0;
	unsigned long min_wait = 0;
	unsigned long will_wait;

	will_wait = tv->tv_sec*1000+tv->tv_usec/1000;
	min_wait = will_wait;

	struct node_info* node = nodes;

	if (gettimeofday(&now, NULL) == -1)
		die(1, "gettimeofday()");

	while(node) {
		if (node->signal) {
			elapsed = elapsed_ms(&now, &node->seen);

			// node is dead...
			if (elapsed >= sig_reset) {
				node->signal = 0;
				display_node(node);
				need_refresh = 1;
			}

			// need to check soon possibly...
			else {
				unsigned long left;

				left = sig_reset - elapsed;
				if (left < min_wait)
					left = min_wait;
			}
		}	
		node = node->next;
	}

	if (need_refresh)
		refresh();

	// need to sleep for less...
	if (min_wait < will_wait) {
		tv->tv_sec = min_wait/1000;
		tv->tv_usec = (min_wait - tv->tv_sec*1000)*1000;
	}
}

void own(char* ifname) {
	int rd;
	fd_set fds;
	struct timeval tv;
	int dlt = DLT_IEEE802_11_RADIO;

	hopfreq = 1000;

	setup_if(ifname);
	open_bpf(ifname, dlt);

	while(1) {
		// XXX innefficient all of this...
		if (!chaninfo.locked)
			chanhop(&tv);
		else {
			tv.tv_sec = 1;
			tv.tv_usec = 0;
		}	

		// especially this...
		check_seen(&tv);	

		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(bpf_s, &fds);

		rd = select(bpf_s+1, &fds,NULL , NULL, &tv);
		if (rd == -1)
			die(1, "select()");
		if (FD_ISSET(0, &fds))
			user_input();
		if (FD_ISSET(bpf_s, &fds))
			bpf_input();
	}
}

void init_globals() {
	ioctl_s = socket(PF_INET, SOCK_DGRAM, 0);
	if (ioctl_s == -1) {
		perror("socket()");
		exit(1);
	}

	chaninfo.locked = 0;
	chaninfo.chan = 0;
}

int main(int argc, char *argv[]) {


	if (argc < 2) {
		printf("Usage: %s <iface>\n", argv[0]);
		exit(1);
	}

	init_globals();

	initscr(); cbreak(); noecho();
	
	nonl();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);

	curs_set(0);

	clear();
	refresh();

	signal(SIGINT, cleanup);
	signal(SIGTERM, cleanup);
	
	own(argv[1]);

	cleanup(0);
	exit(0);
}
