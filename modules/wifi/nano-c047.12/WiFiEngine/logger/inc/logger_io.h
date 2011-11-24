#ifndef __LOGGER_IO
#define __LOGGER_IO

#ifdef C_LOGGING
/*! Init global default logger.
 */
void logger_io_init(void);

/*! Push whatever is in to log buffer to file
 * @param path WIFI_ENGINE_LOG_FILE1_PATH - WIFI_ENGINE_LOG_FILE5_PATH
 * @param append to file or truncate
 * @return 1 on successfull write (also on empty buf), 
 *         0 on failure (file access not supported, error, disk full, ...)
 */
int log_to_file(const char *path, int append);
#else
#define log_to_file(_path, _append)    (void)0
#define logger_io_init()
#endif /* C_LOGGING */

#endif /* __LOGGER_IO */
