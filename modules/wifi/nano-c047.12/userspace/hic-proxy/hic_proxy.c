
#include <stdio.h>      
#include <string.h>
#include <stdlib.h>     
#include <unistd.h>     
#include <signal.h>
#include <sys/types.h>  
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/stat.h>   
#include <inttypes.h>
#include <linux/if.h>
#include <errno.h>
#include <fcntl.h>      
#include <termios.h>
#include <err.h>
#include <nanoioctl.h>
#include "host_flash.h"

#ifdef ANDROID
#include "../android.h"
#endif

char * default_filename = "mib.bin";

static int debug = 0x3;
#define APP_DEBUG(...) { if(debug & 0x1) printf(__VA_ARGS__); }
#define APP_INFO(...)  { if(debug & 0x2) printf(__VA_ARGS__); }
#define APP_ASSERT(as) { if(debug && !(as)) { \
printf("%s:%d: ASSERTION FAILED\n", __func__, __LINE__); } }

static void
printbuf(const void *data, size_t len, const char *prefix)
{
   unsigned int i, j;
   const unsigned char *p = data;
   for(i = 0; i < len; i += 16) {
      printf("%s %04x: ", prefix, i);
      for(j = 0; j < 16; j++) {
         if(i + j < len)
            printf("%02x ", p[i+j]); 
         else
            printf("   ");
      }   
      printf(" : ");
      for(j = 0; j < 16; j++) {
         if(i + j < len) {   
#define isprint(c) ((c) >= 32 && (c) <= 126)
            if(isprint(p[i+j]))
               printf("%c", p[i+j]);
            else
               printf(".");
         } else
            printf(" ");
      }
      printf("\n");
   }
}

void redirect_stdout(const char *filename)
{
   int fd;
   if(filename == NULL) {
      fd = open("/dev/null", O_WRONLY);
      if(fd < 0)
         err(1, "open(/dev/null)");
   } else {
      fd = open(filename, O_CREAT|O_WRONLY|O_APPEND, 0666);
      if(fd < 0)
         err(1, "open(%s)", filename);
   }
   dup2(fd, STDOUT_FILENO);
   dup2(fd, STDERR_FILENO);
   close(fd);
}

/* Flash message type (host_header.msgType) */
#define HIC_MESSAGE_TYPE_FLASH_PRG   5
#define MAC_API_PRIMITIVE_TYPE_REQ    0
#define MAC_API_PRIMITIVE_TYPE_CFM    0x80
#define HOST_FLASH_VENDOR           0xff

/******************************************************************************/
/* DRIVER TO MAC MESSAGE ID's                                                 */
/******************************************************************************/
#define HIC_MAC_START_PRG_REQ            (0 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_WRITE_FLASH_REQ          (1 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_END_PRG_REQ              (2 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_START_READ_REQ           (3 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_READ_FLASH_REQ           (4 | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_END_READ_REQ             (5 | MAC_API_PRIMITIVE_TYPE_REQ)

/******************************************************************************/
/* MAC TO DRIVER MESSAGE ID's                                                 */
/******************************************************************************/
#define HIC_MAC_START_PRG_CFM            (0 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_WRITE_FLASH_CFM          (1 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_END_PRG_CFM              (2 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_START_READ_CFM           (3 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_READ_FLASH_CFM           (4 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_END_READ_CFM             (5 | MAC_API_PRIMITIVE_TYPE_CFM)
#define HIC_MAC_CFM                      (6 | MAC_API_PRIMITIVE_TYPE_CFM)

#define TYPE_INDEX                    2
#define ID_INDEX                      3
#define PAYLOAD_PAD_INDEX             5
#define HEADER_PAD_INDEX              4

static void host_write(int host_fd, void* buf, size_t len)
{
   unsigned int num_written = 0;
   int status;

   APP_DEBUG("forwarding reply\n");
   
   while(num_written < len) {
      status = write(host_fd, buf + num_written, len - num_written);
      if(status < 0) err(1, "write");
      num_written += status;
   }
}

static int open_socket(int port)
{
   int s, s2;
   struct sockaddr_in sin;
   socklen_t sin_len;
   int one = 1;
   int status;
   int flags;
    
   s = socket(AF_INET, SOCK_STREAM, 0);
   if(s < 0) err(1, "socket");
	    
   memset(&sin, 0, sizeof(sin));
   sin.sin_port = htons(port);
	    
   status = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
   if(status < 0) warn("SO_REUSEADDR");
    
   status = bind(s, (struct sockaddr*) &sin, sizeof(sin));
   if(status < 0) err(1, "bind");
	    
   status = listen(s, 1);
   if(status < 0) err(1, "listen");
    	    
   APP_INFO("listening on port %d\n", (int) ntohs(sin.sin_port));
	    
   sin_len = sizeof(sin);
   s2 = accept(s, (struct sockaddr*) &sin, &sin_len);
   if(s2 < 0) err(1, "accept");

   APP_INFO("connection from %s\n", inet_ntoa(sin.sin_addr));

   flags = fcntl(s2, F_GETFL);
   //set non-blocking mode
   fcntl(s2, F_SETFL, flags | O_NONBLOCK | O_NDELAY);

   return s2;
}

static int open_serial(char* dev, speed_t speed)
{
   int status;
   struct termios ios;
   int fd;
   int flags;

   fd = open(dev, O_RDWR);
   if(fd < 0) err(1, "open");

   status = tcgetattr(fd, &ios);
   if(status < 0) err(1, "tcgetattr");
    
   ios.c_cflag = CS8 | CREAD | CLOCAL;
   ios.c_lflag = 0;
   ios.c_iflag = 0;
   ios.c_oflag = 0;
   ios.c_cc[VMIN] = 1;
   ios.c_cc[VTIME] = 5;
   status = tcsetattr(fd, TCSANOW, &ios);
   if(status < 0) err(1, "tcsetattr");

   tcflush(fd, TCIOFLUSH);
   if(cfsetispeed(&ios, speed)) err(1,"input baudrate");   
   if(cfsetospeed(&ios, speed)) err(1,"output baudrate");

   status = tcsetattr(fd, TCSANOW, &ios);
   if(status < 0) err(1, "tcsetattr");

   flags = fcntl(fd, F_GETFL);
   //set non-blocking mode
   fcntl(fd, F_SETFL, flags | O_NONBLOCK | O_NDELAY);
   return fd;

}

static int term_fd;
static struct termios term_otio;

static void reset_terminal(void)
{
   tcsetattr(term_fd, TCSANOW, &term_otio);
}

static void reset_terminal2(int signo)
{
   reset_terminal();
   raise(signo);
}

static int open_terminal(void)
{
   char *tty;
   struct termios tio;
   struct sigaction sa;
   int ret;
   int flags;
   
   if(!isatty(STDIN_FILENO))
      errx(1, "stdin is not a terminal");
      
   tty = ttyname(STDIN_FILENO);
   term_fd = open(tty, O_RDWR);
   if(term_fd < 0)
      err(1, "%s", tty);

   ret = tcgetattr(term_fd, &term_otio);
   if(ret < 0)
      err(1, "tcgetattr(%s)", tty);
    
   tio = term_otio;
   tio.c_cflag = CS8 | CREAD | CLOCAL;
   tio.c_lflag = 0;
   tio.c_iflag = 0;
   tio.c_oflag = 0;
   tio.c_cc[VMIN] = 1;
   tio.c_cc[VTIME] = 5;

   atexit(reset_terminal);
   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = reset_terminal2;
   sa.sa_flags = SA_RESTART|SA_RESETHAND;
   sigaction(SIGSEGV, &sa, NULL);
   sigaction(SIGILL, &sa, NULL);
   sigaction(SIGSEGV, &sa, NULL);
   sigaction(SIGTERM, &sa, NULL);
   sigaction(SIGINT, &sa, NULL);

   ret = tcsetattr(term_fd, TCSANOW, &tio);
   if(ret < 0)
      err(1, "tcsetattr(%s)", tty);

   tcflush(term_fd, TCIOFLUSH);

   flags = fcntl(term_fd, F_GETFL);
   //set non-blocking mode
   fcntl(term_fd, F_SETFL, flags | O_NONBLOCK | O_NDELAY);

   return term_fd;
}

void usage()
{
   printf("Usage: hic_proxy [-s <serial device> -b <baudrate>] [-i] [-p <port number>] [-f <file name>] " \
	  "<interface> " \
	  "\nOptions: " \
	  "\n\t h: this help" \
	  "\n\t d: debug control (0 silent, 1 debug messages, 2 info messages, 3 both)" \
	  "\n\t s: use serial device as client interface (default is to use ethernet)" \
	  "\n\t b: serial port baudrate (9600,19200,57600,default=115200)" \
	  "\n\t p: port number to use in ethernet client " \
          "\n\t i: use stdin/stdout as client interface" \
          "interface (default 12345)\n\t " \
          "f: HOST persistent storage MIB data filename (default mib.bin)\n\t" \
          "\nInterface:" \
          "\n\t Nanoradio wireless network interface\n\n");
   exit(0);	
}


int poll_target(int nrx_fd, struct ifreq *ifr)
{
   int status;
   struct nanoioctl* nr = (struct nanoioctl*) ifr->ifr_data;
    
   nr->length = sizeof(nr->data);    
   status = ioctl(nrx_fd, SIOCNRXRAWRX, ifr);    
   if(status < 0) err(1, "ioctl (rawrx)");

   return nr->length;
}


int poll_host(int host_fd, struct ifreq *ifr)
{
   int status;
   int message_type;
   size_t num_read = 0, len;
   char buf[1024];
   struct nanoioctl* nr = (struct nanoioctl*) ifr->ifr_data;
   int host_fd_flags;
   int flags;

   status = read(host_fd, buf, 2);
    
   if(status == 0)
     return 0;
     
   if(status < 0) 
     {
       if (errno == EAGAIN)
	 return 0;
       else
	 err(1,"read");
     }

   num_read += status;
    
   while(num_read < 2) {
      status = read(host_fd, buf + num_read, 2 - num_read);
      if(status < 0) 
	{
	  if(errno != EAGAIN)  
	    err(1, "read");
	}
      else
	num_read += status;
   }
            
   /* Ok, we got 2 bytes from nanoloader */
   len = *((uint16_t*) buf);
   APP_ASSERT((len > 0 && len < 1022));
   
   APP_DEBUG("Packet size is %d\n", len);

   /*  Read the rest of the packet */
   while(num_read < len + 2) {
     status = read(host_fd, buf + num_read, (len + 2 ) - num_read);
     if(status < 0) 
	{
	  if(errno != EAGAIN)  
	    err(1, "read");
	}
     else
       {
	 num_read += status;
       }
   }		 
   APP_DEBUG("Packet recieved\n"); 
   
   memcpy(nr->data, buf, num_read);
    
   nr->length = num_read;
   return num_read;
}

void nrx_write(int nrx_fd, struct ifreq* ifr)
{
   int status;
   struct nanoioctl* nr = (struct nanoioctl*) ifr->ifr_data;

   APP_INFO("Writing packet to target\n"); 
     
   APP_ASSERT(ifr != NULL);

   status = ioctl(nrx_fd, SIOCNRXRAWTX, ifr);

   if(status < 0) err(1, "ioctl (rawtx)");

}

void * flash_cmd(struct ifreq* ifr, void * handle, char * filename)
{
  
   static int host_flash_active = 0;

   int data_index;
   int vendor;
   int sector;
   int message_id;
   struct nanoioctl* nr = (struct nanoioctl*) ifr->ifr_data;

   message_id = nr->data[ID_INDEX];
   data_index = nr->data[HEADER_PAD_INDEX] + 4; 
 
   APP_DEBUG("flash message id: %d\n",message_id);
    
   switch(message_id) {
 
   case HIC_MAC_START_PRG_REQ:
      //start a flash write
      vendor = nr->data[data_index];
      sector = nr->data[data_index+1];
 
      if(vendor == HOST_FLASH_VENDOR)
         {
            APP_DEBUG("host flash command \n");
            host_flash_active = 1;
            handle = host_flash_open(filename,HOST_FLASH_WRITE_FLAG);
            if (handle) nr->data[data_index] = 0;
            else nr->data[data_index] = 1;
            //reply
            nr->data[ID_INDEX] = HIC_MAC_START_PRG_CFM;
         }
      else return NULL;

      break;

   case HIC_MAC_WRITE_FLASH_REQ:

      if(host_flash_active) 
         {
            //start to write flash data	                                    	
            nr->data[data_index] = host_flash_write(&nr->data[data_index],nr->length - data_index,handle);
            //reply
            nr->data[ID_INDEX] = HIC_MAC_WRITE_FLASH_CFM;;
         }
      else return NULL;
      break; 
                                    
   case HIC_MAC_END_PRG_REQ:
     
      if(host_flash_active)
         {
            //stop write to flash                               
            //reply
            nr->data[ID_INDEX] = HIC_MAC_END_PRG_CFM;
            nr->data[data_index] =  host_flash_close(handle);
            
            host_flash_active = 0;
         }
      else return NULL;
      break;
   case HIC_MAC_START_READ_REQ:
     
      //start a flash read cmd
      vendor = nr->data[data_index];
      sector = nr->data[data_index+1];

      if(vendor == HOST_FLASH_VENDOR)
         {
            host_flash_active = 1;
            //reply
            nr->data[ID_INDEX] = HIC_MAC_START_READ_CFM;
            
            handle = host_flash_open(filename,HOST_FLASH_READ_FLAG);
            if (handle) nr->data[data_index] = 0;
            else nr->data[data_index] = 1;
         }
      else return NULL;

      break;

   case HIC_MAC_READ_FLASH_REQ:
      //start to read flash data
        			  
      if(host_flash_active)
         {
            nr->data[ID_INDEX] = HIC_MAC_READ_FLASH_CFM;
            nr->data[TYPE_INDEX] = HIC_MESSAGE_TYPE_FLASH_PRG;
            nr->data[PAYLOAD_PAD_INDEX] = 0; 
            { 
               int i;
               i = host_flash_read(&nr->data[data_index],HOST_FLASH_MAX_READ_SIZE,handle);      
               APP_ASSERT(i);
               nr->length = data_index + i;
               *(uint16_t*)nr->data = (nr->length - 2);
            }
         }
      else return NULL;
      break; 
      
   case HIC_MAC_END_READ_REQ:
      
      if(host_flash_active)
         {
            host_flash_active = 0;
            //stop read from flash                             
            //reply
            nr->data[ID_INDEX] = HIC_MAC_END_READ_CFM;
            nr->data[data_index] =  host_flash_close(handle);
         }
      else return NULL;
      break;
   default:
      APP_ASSERT(0);
      break;
   }

   if (message_id != HIC_MAC_READ_FLASH_REQ) {
      //Common reply data
      *(uint16_t*)nr->data = (data_index - 1);
      nr->data[TYPE_INDEX] = HIC_MESSAGE_TYPE_FLASH_PRG;
      nr->data[PAYLOAD_PAD_INDEX] = 0; 
      nr->length = (*(uint16_t*)nr->data) + 2;
   }

   return handle;    
}

int main(int argc, char **argv)
{
   int status;
   int nrx_fd;
   int host_rfd;
   int host_wfd;
   int use_stdin = 0;
   char * ser_dev = NULL;
   int port = 12345;
   char * filename = default_filename;
   void * handle = NULL;
   int host_flash = 0;
   int input_baudrate = 115200;
   speed_t baudrate = B115200;
   int opt;

   struct ifreq ifr;
   struct nanoioctl nr;
   
   while((opt = getopt(argc, argv, "d:s:p:f:b:i")) != -1) {
      switch(opt) {
         case 'b':
            input_baudrate = atoi(optarg);
            switch(input_baudrate) {
               case 9600: baudrate = B9600; break;
               case 19200: baudrate = B19200; break;
               case 38400: baudrate = B38400; break;
               case 57600: baudrate = B57600; break;
               case 115200: baudrate = B115200; break;
               default: usage();
            }
            break;
         case 'd':
            debug = atoi(optarg);
            break;
         case 'f':
            filename = optarg;
            break;
         case 'i':
            use_stdin = 1;
            break;
         case 'p':
            port = atoi(optarg);
            break;
         case 's':
            ser_dev = optarg;
            break;
         default:
            break;
      }
   }
   if(optind == argc)
      usage();

   strcpy(ifr.ifr_name, argv[optind]);
   ifr.ifr_data = (char *)&nr;
   memset(&nr,0,sizeof(nr));
   nr.magic = NR_MAGIC;

   APP_INFO("Nanoradio network interface: %s\n", ifr.ifr_name);
   APP_INFO("Saving persistent MIB data of type HOST to: %s\n", filename);

   if(use_stdin) {
      APP_INFO("Using stdin/stdout as client interface\n");
      host_wfd = host_rfd = open_terminal();
      redirect_stdout("/tmp/hic_proxy.log");
   } else if(ser_dev) 
      {
	APP_INFO("Using %s (baudrate = %d) as serial client interface\n", ser_dev,input_baudrate);
	host_wfd = host_rfd = open_serial(ser_dev, baudrate);
      }
   else 
      {
         APP_INFO("Using ethernet as client interface\n");
         host_wfd = host_rfd = open_socket(port);
      }
    
   nrx_fd = socket(AF_INET, SOCK_DGRAM, 0);
   if(nrx_fd < 0) err(1, "socket");

   for(;;) {      
      status = poll_host(host_rfd, &ifr);
      if(status) {
	
         
         if((nr.data[TYPE_INDEX] ==  HIC_MESSAGE_TYPE_FLASH_PRG))
            {
               APP_DEBUG("flash cmd recieved\n");

               //examine flash cmd
               handle = flash_cmd(&ifr,handle,filename);
 
               if(handle)
                  {
                     //send local host flash cmd reply
                     host_write(host_wfd, nr.data, nr.length);
                  }
               else
                  {
                     //forward to target
                     nrx_write(nrx_fd, &ifr);
                  }
            }
         else
            {
               //forward to target
               nrx_write(nrx_fd, &ifr);
            }
      }
	
      status = poll_target(nrx_fd, &ifr);
  
      if(status)
         host_write(host_wfd, nr.data, nr.length);

      usleep(5000);
   }

   if(!use_stdin) {
      close(host_rfd);
   }
   close(nrx_fd);
}



