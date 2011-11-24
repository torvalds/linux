

/*!
 * @defgroup persistant_api Persistant Storage API
 *
 * The persistant storage API have to implemented if the host platform should
 * store device configuration data.
 *
 * The user of the Persistant storage API is the hic-proxy application.
 * 'hic-proxy' will use the API in order to write device configuration data
 * which has the 'host flash' storage type.
 *
 * Basically, the device configuration data is a collection of MIB settings.
 * These settings can be evaluated and stored using 'hic-proxy' together with
 * the nanoloader application or any other application using the ProdTest API.
 * 
 * In practice, the persistant storage will be some kind of flash memory.
 *
 * @{
 */

#include <stdint.h>

#define HOST_FLASH_MAX_READ_SIZE 256

#define HOST_FLASH_STATUS_OK                    0
#define HOST_FLASH_STATUS_ERROR                 1

#define HOST_FLASH_WRITE_FLAG (1 << 1)
#define HOST_FLASH_READ_FLAG  (1 << 0)

/*!
 * @brief Open persistent storage area.
 *
 * This function will be called prior to any call to host_flash_write(), 
 * host_flash_read() and host_flash_close().
 *
 * This function should, if necessary, prepare the persistent storage area for
 * writing. If the persistent storage area is accessed through the file system,
 * this function can probably be implemented as a regular open().
 *
 * After this call, the read/write offset for the area should be 0.
 *
 * @param filename This parameter will carry a string identifier for the
 *                 persistent storage area.  If the persistent storage is
 *                 accessed thorugh the file system, this parameter can be
 *                 used as the file name.
 *
 * @param flags Bitmask that specifies if the persistent storage area should be
 *              prepared for read or write access.
 *
 * @return 
 * - A handle to the opened persistent storage area on success
 * - NULL on failure
 *
 */
void * host_flash_open(char * filename, uint32_t flags);


/*!
 * @brief Write to persistent storage area.
 *
 * This function will be called when data should be written to a previously 
 * opened (host_flash_open()) persistent storage area.
 *
 * This function should write the data availble in buf to the current offset
 * in the persistent storage area indentifed by handle.
 *
 * host_flash_write() can be called multiple times for each open persistent
 * storage area.
 * Each call should result in the data being appended to the area.
 *
 * If the persistent storage area is accessed through the file system,
 * this function can probably be implemented as a regular write().
 *
 * @param buf Pointer to a buffer containing the data to write.
 * @param size Size of the data the write.
 * @param handle Persistent Storage area identifier.
 *
 * @return 
 * - HOST_FLASH_STATUS_OK on success
 * - HOST_FLASH_STATUS_ERROR on failure
 *
 */
int host_flash_write(void * buf, size_t size, void * handle);


/*!
 * @brief Read from persistent storage area.
 *
 * This function will be called when data should be read from a previously 
 * opened (host_flash_open()) persistent storage area.
 *
 * This function should read 'size' bytes of from the persistent storage area 
 * identified by handle. The data should be available in buf after the call.
 *
 * host_flash_read() can be called multiple times for each open persistent
 * storage area. Each call should result in sequential data reads.
 *
 * If the persistent storage area is accessed through the file system,
 * this function can probably be implemented as a regular read().
 *
 * @param buf Pointer to a buffer that should hold the read data.
 * @param size Number of bytes to read.
 * @param handle Persistent Storage area identifier.
 *
 * @return 
 * - HOST_FLASH_STATUS_OK on success
 * - HOST_FLASH_STATUS_ERROR on failure
 *
 */
int host_flash_read(void * buf, size_t size, void * handle);



/*!
 * @brief Close a persistent storage area.
 *
 * This function will be called when a persistent storage area should be 
 * closed. All pending data should be flushed to the persistent 
 * storage area when this call returns.
 *
 * If the persistent storage area is accessed through the file system,
 * this function can probably be implemented as a regular close().
 *
 * @param handle The persistent storage area identifier.
 *
 * @return 
 * - HOST_FLASH_STATUS_OK on success
 * - HOST_FLASH_STATUS_ERROR on failure
 *
 */
int host_flash_close(void *handle);


/** @} */ /* End of Persistent Storage API group */
