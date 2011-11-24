#include <stdio.h>
#include "wlandutlibx.h"

#define CON printf
#define DBG

//used for testing the test program...
#define TEST_NONE   0

//sends modulated data continiously
#define TEST_TX     1

//counts how many frames have been received @ specified rate
#define TEST_RX     2


static void print_usage(void);

static int packet_size = 1024;
static int channel = 6;
static int time = 3;
static int burst_interval = 10;
static int output_power = 21; 

static int str_comp(char *str1, char *str2, int len)
{
    int i;
    
    for(i=0;i<len;i++)
    {
        if(str1[i] !=  str2[i])
            return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    
    int status;
    int i;
    int debug = 0;
    int long_preamble = 0;
    
    int test = TEST_NONE;
    DataRate rate_e = DATA_RATE_1M;
    char *rate_s = "1Mbps";
    char *interface_name = NULL;
    
    
    DBG("argc=%u\n", argc);

    if(argc < 2)
    {
        print_usage();
        return -1;
    }

    interface_name = argv[1];

    
    
    for(i=2;i<argc;i++)
    {
        if(str_comp(argv[i], "-ch:", 4))
        {
            channel = atoi(argv[i]+4);
            if((channel > 14) || (channel < 1))
            {
                CON("Invalid channel: %u\n", channel);
                return -1;
            }
            continue;
        }

        if(str_comp(argv[i], "-time:", 6))
        {
            time = atoi(argv[i]+6);
            if((time > 24*3600) || (time < 1))
            {
                CON("Invalid time: %u\n", time);
                return -1;
            }
            continue;
        }

        if(str_comp(argv[i], "-rate:", 6))
        {
            int rate = atoi(argv[i]+6);
            switch(rate)
            {
                case 1:
                case 1000:
                    rate_e = DATA_RATE_1M;
                    rate_s = "1Mbps";
                    break;
                case 2:    
                case 2000:
                    rate_e = DATA_RATE_2M;
                    rate_s = "2Mbps";
                    break;
                case 5500:
                    rate_e = DATA_RATE_5_5M;
                    rate_s = "5.5Mbps";
                    break;
                //case 6: the user migh have writen "-rate:6.5"...    
                case 6000:
                    rate_e = DATA_RATE_6M;
                    rate_s = "6Mbps";
                    break;
                case 6500:
                    rate_e = DATA_RATE_MCS0;
                    rate_s = "6.5Mbps (MCS0)";
                    break;    
                case 9:    
                case 9000:
                    rate_e = DATA_RATE_9M;
                    rate_s = "9Mbps";
                    break;
                case 11:    
                case 11000:
                    rate_e = DATA_RATE_11M;
                    rate_s = "11Mbps";
                    break;
                case 12:    
                case 12000:
                    rate_e = DATA_RATE_12M;
                    rate_s = "12Mbps";
                    break;
                case 13:    
                case 13000:
                    rate_e = DATA_RATE_MCS1;
                    rate_s = "13Mbps (MSC1)";
                    break;
                case 18:    
                case 18000:
                    rate_e = DATA_RATE_18M;
                    rate_s = "18Mbps";
                    break;
                case 19500:
                    rate_e = DATA_RATE_MCS2;
                    rate_s = "19.5Mbps (MSC2)";
                    break;
                case 22:    
                case 22000:
                    rate_e = DATA_RATE_22M;
                    rate_s = "22Mbps";
                    break;
                case 24:    
                case 24000:
                    rate_e = DATA_RATE_24M;
                    rate_s = "24Mbps";
                    break;
                case 26:    
                case 26000:
                    rate_e = DATA_RATE_MCS3;
                    rate_s = "26Mbps (MSC3)";
                    break;
                case 33:    
                case 33000:
                    rate_e = DATA_RATE_33M;
                    rate_s = "33Mbps";
                    break;
                case 36:    
                case 36000:
                    rate_e = DATA_RATE_36M;
                    rate_s = "36Mbps";
                    break;
                case 39:
                case 39000:
                    rate_e = DATA_RATE_MCS4;
                    rate_s = "39Mbps (MCS4)";
                    break;
                case 48:    
                case 48000:
                    rate_e = DATA_RATE_48M;
                    rate_s = "48Mbps";
                    break;
                case 52:    
                case 52000:
                    rate_e = DATA_RATE_MCS5;
                    rate_s = "52Mbps (MCS5)";
                    break;
                case 54:    
                case 54000:
                    rate_e = DATA_RATE_54M;
                    rate_s = "54Mbps";
                    break;    
                case 58500:
                    rate_e = DATA_RATE_MCS6;
                    rate_s = "58.5Mbps (MCS6)";
                    break;
                case 65:    
                case 65000:
                    rate_e = DATA_RATE_MCS7;
                    rate_s = "65Mbps (MSC7)";
                    break;
               default:
                   CON("Invalid modulation rate %uKbps\n",rate);
                   return -1;
           }
           continue;
        }
        
        if(str_comp(argv[i], "-bi:", 4))
        {
            burst_interval = atoi(argv[i]+4);
            if(burst_interval < 0)
            {
                CON("Invalid burst interval: %u\n", burst_interval);
                return -1;
            }
            continue;
        }
         
        if(str_comp(argv[i], "-size:", 6))
        {
            packet_size = atoi(argv[i]+6);
            if((packet_size < 0) || (packet_size > 1536))
            {
                CON("Invalid packet size: %u\n", packet_size);
                return -1;
            }
            continue;
        }
        
        if(str_comp(argv[i], "-pwr:", 5))
        {
            output_power = atoi(argv[i]+5);
            if((output_power < 0) || (output_power > 30))
            {
                CON("Invalid output power: %ddBm\n", output_power);
                return -1;
            }
            continue;
        }     

        if(str_comp(argv[i], "none", 4))
        {
            test = TEST_NONE;
            continue;
        }

        if(str_comp(argv[i], "tx", 2))
        {
            test = TEST_TX;
            continue;
        }

        if(str_comp(argv[i], "rx", 2))
        {
            test = TEST_RX;
            continue;
        }
        
        if(str_comp(argv[i], "-lp", 3))
        {
            long_preamble = 1;
            continue;
        }  

        if(str_comp(argv[i], "-dbg", 4))
        {
            enable_debug();
            continue;
        }

        CON("I do not understand argument \"%s\", ignoring\n", argv[i]);
    }

    status = set_interface(interface_name);
    if(status)
    {
        CON("Could not set interface name\n");
        CON("  Driver say:\"%s\"\n",GetErrorString());
        return -1;
    }
  
  
    status = OpenDUT();
    if(status)
    {
        CON("Could not open connection to Nanoradio WiFi-chip\n");
        CON("  Driver say:\"%s\"\n",GetErrorString());
        return -1;
    }
     
    status = SetLongPreamble(long_preamble);
    if(status)
    {
        CON("Failed to set long preamble\n");
        CON("  Driver say:\"%s\"\n",GetErrorString());
        goto exit;
    }
     
    status = SetBurstInterval(burst_interval);
    if(status)
    {
        CON("Failed to set burst interval\n");
        CON("  Driver say:\"%s\"\n",GetErrorString());
        goto exit;
    }   
  
  
    status = SetChannel(channel);
    if(status)
    {
        CON("Failed to write channel to Nanoradio WiFi-chip\n");
        CON("  Driver say:\"%s\"\n",GetErrorString());
        goto exit;
    }
    
    status = SetPayload(packet_size);
    if(status)
    {
        CON("Failed to set packet size\n");
        CON("  Driver say:\"%s\"\n",GetErrorString());
        goto exit;
    }
  

  status = SetDataRate(rate_e);
  if(status)
  {
    CON("Failed to write data rate to Nanoradio WiFi-chip\n");
    CON("  Driver say:\"%s\"\n",GetErrorString());
    goto exit;
  }

    switch(test)
    {
        case TEST_NONE:
            break;
        case TEST_TX:
        {
            status = TxGain(output_power);
            if(status)
            {
                CON("Failed to set output power\n");
                CON("  Driver say:\"%s\"\n",GetErrorString());
                goto exit;
            }   
         
            status = TxStartWithMod();
            if(status)
            {
                CON("Failed to start TX\n");
                CON("  Driver say:\"%s\"\n",GetErrorString());
                goto exit;
            }
            else
            {
                CON("Sending frames continiously on channel %u with rate %s for %u seconds\n",channel,rate_s,time);
                sleep(time);
                status = TxStop();
                if(status)
                {
                    CON("Failed to stop TX\n");
                    CON("  Driver say:\"%s\"\n",GetErrorString());
                    goto exit;
                }
                
            }
        }
        break;
        
        case TEST_RX:
        {
            status = RxStart();
            if(status)
            {
                CON("Failed to start RX\n");
                CON("  Driver say:\"%s\"\n",GetErrorString());
                goto exit;
            }
            else
            {
                CON("Counting frames on channel %u with rate %s for %u seconds\n",channel,rate_s,time);
                sleep(time);
                status = RxStop();
                if(status)
                {
                    CON("Failed to stop RX\n");
                    CON("  Driver say:\"%s\"\n",GetErrorString());
                    goto exit;
                }
                else
                {
                    CON("Good frames:%u\n", GetGoodFrame());
                }
            }
      }
      break;
  }

exit:

    CloseDUT();
    return status;
}


static void print_usage(void)
{
    CON("Cmd line test tool for testing Nanoradio WiFi RF-parameters\n");
    CON("Usage: nrxtest INTERFACE [TEST] [SETTINGS]\n");
    CON("       INTERFACE       name of network interface, eg eth0 or wlan0\n");
    CON("       TEST            one of the following:\n");
    CON("                       none (default) just configures WiFi-chip\n");
    CON("                       tx send dataframes\n");
    CON("                       rx count the number of received frames\n");
    CON("       -ch:CH          1..13, default is %u\n",channel);
    CON("       -rate:RATE      RATE in Kbps (1000 for 1Mbps...54000 for 54Mbps)\n");
    CON("                       default is 1Mbps\n");
    CON("       -time:TIME      1...86400 sec. Configures the test-time, when applicable\n");
    CON("                       default is %u sec\n",time);
    CON("       -lp             Use long preamble (default is short)\n");
    CON("       -bi:INTERVAL    Configure burst intervall (default is %u)\n",burst_interval);
    CON("       -size:PKT_SIZE  Sets size of tx packet (default is %u)\n",packet_size);
    CON("       -pwr:TX_POWER   0..21, sets the wanted tx power in dBm on chip output\n");
    CON("                       Default is %udBm\n",output_power);
    CON("                       If the request can't be satisfied the closest value possible will be used.\n");
    CON("       -dbg            Display the commands sent to/from chip\n");
    CON("For questions or support, contact Mats Söderhäll at Nanoradio AB\n");
}
