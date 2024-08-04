#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/printk_self.h>

int iGlobalLogPrintLevel;
EXPORT_SYMBOL(iGlobalLogPrintLevel);

int iGlobalLogPrintTimes; 
EXPORT_SYMBOL(iGlobalLogPrintTimes);

void GlobalLogParametersInit(int iLevel, int iTimes)
{
        iGlobalLogPrintLevel = iLevel;
        iGlobalLogPrintTimes = iTimes;
}
EXPORT_SYMBOL(GlobalLogParametersInit);


