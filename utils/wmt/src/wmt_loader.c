#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
//For directory operation
#include <dirent.h>

#include <sys/ioctl.h>

#include <string.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "wmt_loader"

#define WCN_COMBO_LOADER_DEV				"/dev/wmtdetect"

#define WMT_DETECT_IOC_MAGIC            'w'
#define COMBO_IOCTL_GET_CHIP_ID       _IOR(WMT_DETECT_IOC_MAGIC, 0, int)
#define COMBO_IOCTL_SET_CHIP_ID       _IOW(WMT_DETECT_IOC_MAGIC, 1, int)
#define COMBO_IOCTL_EXT_CHIP_DETECT   _IOR(WMT_DETECT_IOC_MAGIC, 2, int)
#define COMBO_IOCTL_GET_SOC_CHIP_ID   _IOR(WMT_DETECT_IOC_MAGIC, 3, int)
#define COMBO_IOCTL_DO_MODULE_INIT    _IOR(WMT_DETECT_IOC_MAGIC, 4, int)
#define COMBO_IOCTL_MODULE_CLEANUP    _IOR(WMT_DETECT_IOC_MAGIC, 5, int)
#define COMBO_IOCTL_EXT_CHIP_PWR_ON   _IOR(WMT_DETECT_IOC_MAGIC, 6, int)
#define COMBO_IOCTL_EXT_CHIP_PWR_OFF  _IOR(WMT_DETECT_IOC_MAGIC, 7, int)
#define COMBO_IOCTL_DO_SDIO_AUDOK     _IOR(WMT_DETECT_IOC_MAGIC, 8, int)


static int gLoaderFd = -1;

int main(int argc, char *argv[])
{
    int iRet = -1;
    int chipId = -1;
    int count = 0;
    int gLoaderFd = -1;
   
    printf("init combo device\r\n");
    do{
        gLoaderFd = open(WCN_COMBO_LOADER_DEV, O_RDWR | O_NOCTTY);
        if(gLoaderFd < 0)
        {
	     count ++;
	     printf("Can't open device node(%s) count(%d)\n", WCN_COMBO_LOADER_DEV,count);
             usleep(300000);
        } else
	     break;
    } while(1);

    printf("Opened combo device\r\n");

    // Get Device ID
    chipId = ioctl(gLoaderFd, COMBO_IOCTL_GET_SOC_CHIP_ID, NULL);
    printf("get device id : %d\r\n", chipId);
    if( chipId == -1) {
        printf("invalid device id, exit\r\n");
        return -1;
    }

    // Set Device ID
    iRet = ioctl(gLoaderFd, COMBO_IOCTL_SET_CHIP_ID, chipId);
    printf("set device id : %d\r\n", chipId);
    if( iRet < 0 ) {
        printf("failed to set device id\r\n");
        return -1;
    }

    // do module init 
    iRet = ioctl(gLoaderFd, COMBO_IOCTL_DO_MODULE_INIT, chipId);
    printf("do module init: %d\r\n", chipId);
    if( iRet < 0 ) {
        printf("failed to init module \r\n");
        return -1;
    }

    return 0;
}
