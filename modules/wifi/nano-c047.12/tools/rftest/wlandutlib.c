/* This file contains functions for performing some basic RF-tests
*/


#ifdef ANDROID
#define LOG_TAG "wlandutlib"
#include <utils/Log.h>
#endif 

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "nanoioctl.h"   //from kernel/ic
#include "wlandutlib.h"


/***********************************************************************************************************/
/*                                       Defines                                                           */
/***********************************************************************************************************/
#define SUCCESS            0
#define INTERFACE_NAME     "wlan0"
#define HIC_TYPE_CONSOLE 0x04
#define HIC_CONSOLE_IND  0x01
#define HIC_CONSOLE_CFM  0x80
#define M80211_XMIT_RATE_1MBIT      0
#define M80211_XMIT_RATE_2MBIT      1
#define M80211_XMIT_RATE_5_5MBIT    2
#define M80211_XMIT_RATE_6MBIT      3
#define M80211_XMIT_RATE_6_5MBIT    4
#define M80211_XMIT_RATE_9MBIT      5
#define M80211_XMIT_RATE_11MBIT     6
#define M80211_XMIT_RATE_12MBIT     7
#define M80211_XMIT_RATE_13MBIT     8
#define M80211_XMIT_RATE_18MBIT     9
#define M80211_XMIT_RATE_19_5MBIT  10
#define M80211_XMIT_RATE_22MBIT    11
#define M80211_XMIT_RATE_24MBIT    12
#define M80211_XMIT_RATE_26MBIT    13
#define M80211_XMIT_RATE_33MBIT    14
#define M80211_XMIT_RATE_36MBIT    15
#define M80211_XMIT_RATE_39MBIT    16
#define M80211_XMIT_RATE_48MBIT    17
#define M80211_XMIT_RATE_52MBIT    18
#define M80211_XMIT_RATE_54MBIT    19
#define M80211_XMIT_RATE_58_5MBIT  20
#define M80211_XMIT_RATE_65MBIT    21
#define LAST_RATE_XMIT_RATE        21

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/***********************************************************************************************************/
/*                                            macros                                                       */
/***********************************************************************************************************/
//#define ENTRY CON("%s():ENTRY\n", __func__)
//#define RETURNI(i) do{CON("%s():returns %i\n", __func__,i); return i;}while(0)
#define ENTRY
#define RETURNI(i) return i
#define CON printf
//#define CON LOGE

/***********************************************************************************************************/
/*                                            struct deffenitions                                          */
/***********************************************************************************************************/

typedef struct handle {
    int                           sd;
    struct ifreq                  ifr;
    struct nrx_ioc_console_string conreq;
} *con_handle_t;



struct hic_hdr {
    uint16_t len;
    uint8_t  type;
    uint8_t  id;
    uint8_t  hdrlen;
    uint8_t  reserved;
    uint8_t  pad;
};


/***********************************************************************************************************/
/*                                            static variables                                             */
/***********************************************************************************************************/
//used to display error messages in clear text
static char  no_error[] = "No Error";
static char  is_error[128];
static char* error_msg = no_error;
static con_handle_t connection = NULL;
static int current_channel = 1;
static int current_rate = M80211_XMIT_RATE_1MBIT;
static int is_OFDM = FALSE;              //current selected rate is a G-rate
static int current_burst_interval = 10; //unknown unit
static int current_packet_size = 1024;
static int rx_frames = 0;
static int bad_crc_frames = 0;
static char interface_name[32] = {0};
static int trace_cmd = 0;
static int short_preamble = 1;
static int output_power = -99; //wanted power in dBm. -99 means "use chip default"

/***********************************************************************************************************/
/*                                            static function prototypes                                   */
/***********************************************************************************************************/
con_handle_t console_open(const char *ifname);
void console_close(con_handle_t h);
int console_read(con_handle_t h, char *buf, size_t buflen);
int send_cmd(const char *cmd);



/***********************************************************************************************************/
/*                                            Public Functions                                             */
/***********************************************************************************************************/

int set_interface(char* name)
{
    ENTRY;
    if(strlen(name) > (sizeof(interface_name) -1))
    {
        sprintf(is_error, "Interface name to long");
        error_msg = is_error;
        RETURNI(-1);
    }
    strcpy(interface_name,name);
    error_msg = no_error;
    RETURNI(SUCCESS);
}

void enable_debug(void)
{
    trace_cmd = 1;
}

int OpenDUT()
{
    ENTRY;
    if(strlen(interface_name) == 0)
    {
        strcpy(interface_name,INTERFACE_NAME);
    }
    connection = console_open(interface_name);
    if(connection == NULL)
    {
        sprintf(is_error, "Failed to open \"%s\"",interface_name);
        error_msg = is_error;
        RETURNI(-1);
    }
  
    current_channel = 1;
  
    error_msg = no_error;
    RETURNI(SUCCESS);
}


int CloseDUT()
{
  ENTRY;
  console_close(connection);  
  error_msg = no_error;
  RETURNI(SUCCESS);
}


int SetChannel(int channel)
{
  char buffer[512];
  int ret;
  int i;
  ENTRY;
  
  error_msg = no_error;
  
  //Only IEEE 802.11b/g channels
  if(channel < 1 || channel > 14)
  {
    sprintf(is_error, "invalid ch %i",channel);
    error_msg = is_error;
    RETURNI(-1);
  }
  
  current_channel = channel;
  
  sprintf(buffer, "set_rf_channel=%i\n",current_channel);
  ret = send_cmd(buffer);
  RETURNI(ret);
}


int SetDataRate(DataRate rate)
{
    int was_OFDM = is_OFDM;
    ENTRY;
   
   
    error_msg = no_error;
    switch(rate)
    {
        case DATA_RATE_1M:
            current_rate=M80211_XMIT_RATE_1MBIT;     is_OFDM = FALSE; break;
        case DATA_RATE_2M:  
             current_rate=M80211_XMIT_RATE_2MBIT;    is_OFDM = FALSE; break;
        case DATA_RATE_5_5M:
             current_rate=M80211_XMIT_RATE_5_5MBIT;  is_OFDM = FALSE; break;
        case DATA_RATE_6M:  
             current_rate=M80211_XMIT_RATE_6MBIT;    is_OFDM = TRUE; break;
        case DATA_RATE_MCS0:
             current_rate=M80211_XMIT_RATE_6_5MBIT;  is_OFDM = TRUE; break;
        case DATA_RATE_9M:  
             current_rate=M80211_XMIT_RATE_9MBIT;    is_OFDM = TRUE; break;
        case DATA_RATE_11M: 
             current_rate=M80211_XMIT_RATE_11MBIT;   is_OFDM = FALSE; break;
        case DATA_RATE_12M: 
             current_rate=M80211_XMIT_RATE_12MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_MCS1:
             current_rate=M80211_XMIT_RATE_13MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_18M: 
             current_rate=M80211_XMIT_RATE_18MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_MCS2:
             current_rate=M80211_XMIT_RATE_19_5MBIT; is_OFDM = TRUE; break;
        case DATA_RATE_22M: 
             current_rate=M80211_XMIT_RATE_22MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_24M: 
             current_rate=M80211_XMIT_RATE_24MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_MCS3:
             current_rate=M80211_XMIT_RATE_26MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_33M: 
             current_rate=M80211_XMIT_RATE_33MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_36M: 
             current_rate=M80211_XMIT_RATE_36MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_MCS4:
             current_rate=M80211_XMIT_RATE_39MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_48M: 
             current_rate=M80211_XMIT_RATE_48MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_MCS5:
             current_rate=M80211_XMIT_RATE_52MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_54M: 
             current_rate=M80211_XMIT_RATE_54MBIT;   is_OFDM = TRUE; break;
        case DATA_RATE_MCS6:
             current_rate=M80211_XMIT_RATE_58_5MBIT; is_OFDM = TRUE; break;
        case DATA_RATE_MCS7:
             current_rate=M80211_XMIT_RATE_65MBIT;   is_OFDM = TRUE; break;
        default:
            sprintf(is_error, "invalid rate %i",rate);
            error_msg = is_error;
            RETURNI(-1);
    }
     
    if((was_OFDM != is_OFDM) && (output_power != -99))
    {
        TxGain(output_power);
    } 
     
    RETURNI(SUCCESS);
}

int SetLongPreamble(int enable)
{
    ENTRY;
    if(enable)
    {
        short_preamble = 0;
    }
    else
    {
        short_preamble = 1;
    }
    RETURNI(SUCCESS);
}


int SetShortGuardInterval(int enable)
{
  ENTRY;
  sprintf(is_error, "Function not implemented");
  error_msg = is_error;
  RETURNI(-1);
}

#define RFLOSS 0
#define MAX_OFDM_POWER 17
#define MAX_QPSK_POWER 21
int TxGain(int txpwr)
{
    char buffer[512];
    int ret;
    int index;
    
    ENTRY;
    
    error_msg = no_error;
    
    if((txpwr < 0) || (txpwr > 21))
    {
        sprintf(is_error, "TX-power outside range");
        error_msg = is_error;
        RETURNI(-1);
    }
    
    output_power = txpwr;
  
    txpwr += RFLOSS;
    
    if(is_OFDM)
    {
        index = MAX_OFDM_POWER - txpwr;
    }
    else
    {
        index = MAX_QPSK_POWER - txpwr; 
    }
    
    if(index < 0)
       index = 0;
  
    sprintf(buffer, "dec_power_index=%u\n",index);
    ret = send_cmd(buffer);
    RETURNI(ret);
}


int SetBurstInterval(int burstinterval)
{
    current_burst_interval = burstinterval;
    return SUCCESS;
}


int SetPayload(int size)
{
    current_packet_size = size;
    return SUCCESS;
}


int TxStartWithMod()
{
    char buffer[512];
    int ret;
    int i;
    ENTRY;
  
    error_msg = no_error;
  
    sprintf(buffer, "txgen_start=%u,%u,-1,%u,0,0,0,%u\n",
                    current_rate,
                    current_packet_size,
                    current_burst_interval,
                    short_preamble);
  ret = send_cmd(buffer);
  RETURNI(ret);
}


int TxStartWoMod()
{
  ENTRY;
  sprintf(is_error, "Function not implemented");
  error_msg = is_error;
  RETURNI(-1);
}


int TxStop()
{
    char buffer[512];
    int ret;
    int i;
    ENTRY;
  
    error_msg = no_error;
  
    sprintf(buffer, "txgen_stop\n");
    ret = send_cmd(buffer);
    RETURNI(ret);
}


int RxStart()
{
  char buffer[512];
  int ret;
  int i;
  ENTRY;
  
  error_msg = no_error;
  
  sprintf(buffer, "rxstat_clr\n");
  ret = send_cmd(buffer);
  rx_frames = 0;
  bad_crc_frames = 0;
  RETURNI(ret);
}


int RxStop()
{
    char buffer[512];
    int ret;
    int i;
    ENTRY;
  
    error_msg = no_error;
    
    for(i=0;i <= LAST_RATE_XMIT_RATE; i++)
    {  
        sprintf(buffer, "rxstat=%u,0\n",i);
        ret = send_cmd(buffer);
    }
    //send some nonsense to make sure last results are read 
    SetChannel(current_channel);
    
    RETURNI(ret);

    return SUCCESS;
}


int GetGoodFrame()
{
    return rx_frames;
}


int GetErrorFrame() {

    return bad_crc_frames;
}

int SetBand(int band)
{
    if(band == 1)
    {
       return 0;
    }
    sprintf(is_error, "5GHz band not supported"); 
    error_msg = is_error;   
    return -1;
}

int SetBandWidth(int width)
{
    if(width == 1)
    {
       return 0;
    }
    sprintf(is_error, "40Mhz bandwidth not supported"); 
    error_msg = is_error;   
    return -1;
}


const char *GetErrorString()
{
    char* last_error = error_msg;
    error_msg = no_error;
    return last_error;
}


/***********************************************************************************************************/
/*                                            static functions                                             */
/***********************************************************************************************************/

con_handle_t console_open(const char *ifname)
{
    con_handle_t h;

    if ((h = malloc(sizeof(*h))) != NULL) {
        memset(h, 0, sizeof(*h));
        h->sd = socket(AF_INET, SOCK_DGRAM, 0);
        if (h->sd < 0) {
            free(h);
            h = NULL;
        } else {
            strcpy(h->ifr.ifr_name, ifname);
            h->ifr.ifr_data = (char *)&h->conreq;
        }
    }

    return h;
}

void console_close(con_handle_t h)
{
    if(h == NULL)
        return;

    close(h->sd);
    free(h);
}

int console_write(con_handle_t h, const char *str)
{
    h->conreq.ioc.magic = NRXIOCMAGIC;
    h->conreq.ioc.cmd   = NRXIOWCONSOLEWRITE;
    h->conreq.str_size  = strlen(str);
    h->conreq.str       = (char*) str;

    return ioctl(h->sd, SIOCNRXIOCTL, &h->ifr);
}

int console_read(con_handle_t h, char *buf, size_t buflen)
{
    int ret = 0;

    for (;;)
    {
        struct hic_hdr *hic = (struct hic_hdr *)buf;

        h->conreq.ioc.magic = NRXIOCMAGIC;
        h->conreq.ioc.cmd   = NRXIOWRCONSOLEREAD;
        h->conreq.str_size  = buflen;
        h->conreq.str       = buf;

        ret = ioctl(h->sd, SIOCNRXIOCTL, &h->ifr);

        if (ret < 0) {
            /* Failed to process ioctl() */
            break;
        } else if (h->conreq.str_size == 0) {
            /* No message available */
            ret = 0;
            break;
        } else if ((hic->type & 0x7f) != 0x04) {
            printf("Invalid type (%#x)\n", hic->type);
            ret = -EPROTO;
            break;
        } else if (hic->id == HIC_CONSOLE_CFM) {
            /* CONSOLE_CFM - Read again */
        } else if (hic->id != HIC_CONSOLE_IND) {
            printf("Invalid id (%#x)\n", hic->id);
            ret = -EPROTO;
            break;
        } else {
            size_t slen = hic->len - hic->hdrlen - hic->pad;
            if (slen >= buflen) {
                ret = -EPROTO;
            } else {
                memmove(buf, (char *)h->conreq.str + hic->hdrlen + sizeof(uint32_t), slen);
                buf[slen] = 0;
                ret = buflen;
                if(trace_cmd)
                {
                    unsigned int i,j;
                    int last_good = 0;
                    char dbgbuf[257];
                    
                    j = 0;
                    
                    memset(dbgbuf,0,256);
                    for(i=0;i<slen;i++)
                    {
                        char Char = buf[i];
                        if(i>256)
                            break;
                        
                        if(Char == 0)
                            Char = '\n';
                        
                        if(Char == '\r')
                            continue;
                            
                        if(Char == '\n')
                        {
                            if(j)
                            {
                                dbgbuf[j] = Char;
                                j++;
                            }
                            continue;  
                        }
                        dbgbuf[j] = Char;
                        last_good = j; // 
                        j++;
                    }
                    dbgbuf[last_good+1] = 0;
                    if(last_good){CON(" <-DUT:%s\n",dbgbuf);}
                }
            }
            break;
        }
    }

    return ret;
}

//Sends a command and reads but ignores the reply
int send_cmd(const char *cmd)
{
    char buffer[512];
    int ret;
    int i;
  
    ENTRY;

    if(trace_cmd)
    {
        CON(" ->DUT:%s",cmd);
    } 
    
    error_msg = no_error;

    if(console_write(connection,cmd))
    {
        sprintf(is_error, "Failed to send cmd to WiFi-chip");
        error_msg = is_error;
        RETURNI(-1);
    }
  
    i=0;
  
    while(1)
    {
        memset(buffer,0,sizeof(buffer));
        usleep(5000);
        ret = console_read(connection, buffer, sizeof(buffer));
        if(ret < 0)
        {
            sprintf(is_error, "Reading response from WiFi-chip failed");
            error_msg = is_error;
            RETURNI(-1);
        }

        if(ret == 0)
        {
            if(i++ > 1)
                break;

            continue;
        }

        if(strlen(buffer) > 2)
        {
            if(memcmp(&buffer[2],"RXSTAT:",7) == 0)
            {
                int value[32];
                int num = 0;
                char *pos = &buffer[9];
                int a;
                
                //makes bad valued point out...
                memset(value,0xFF,sizeof(value));
                
                for(num = 0; num < 32; num++)
                {
                    value[num] = atoi(pos);
                    pos = strchr(pos,',');
                    if(pos == NULL)
                        break;
                        
                    pos++; 
                }
                num++;
                
                rx_frames += value[2] + value[3] + value[4] + value[5];
                bad_crc_frames += value[10];
            }
        }
    }

    RETURNI(SUCCESS);
}
