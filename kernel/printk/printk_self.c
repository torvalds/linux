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

void GlobalLogLevelSet(const char *filename)
{	
	if(!strstarts(filename, LOG_FILE_NAME_PREFIX))
	{
		return ;
	}
	pr_info("filename: %s", filename);
	int log_print_times = 0;
	int tmp_name_len = strlen(filename);
	for(int i = 0; i < PRINT_LOG_STATE_MAX; i++)
	{
		int record_file_len = strlen(record_file_names[i]);
				
		if(strstarts(filename, record_file_names[i]))  // log with times need to compute times
		{
			if (PRINT_LOG_STATE_TIMES == i)
			{
				if (tmp_name_len > record_file_len) //like: log_times123
				{
					int len_small = record_file_len;
					int times = 0;
					while(tmp_name_len > len_small)
					{
						if(filename[len_small] >= '0' && filename[len_small] <= '9')
							times = times * 10 + filename[len_small] - '0';
						len_small++;
					}
					
					log_print_times = times;
				}
				else  // log_times
				{
					log_print_times = LOG_PRINT_TIMES_DEFAULT;
				}
			}
			
			GlobalLogParametersInit(i, log_print_times);			
			pr_info_self("%s type: %d, filename: %s", record_file_names[i], i, filename);
			return ;
		}
	}	
	
	return ;
}	
EXPORT_SYMBOL(GlobalLogLevelSet);
