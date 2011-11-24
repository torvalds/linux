//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------


#include "ar6000_drv.h"
#include "htc.h"
#include <linux/fs.h>

#include "target_reg_table.h"
#include "host_reg_table.h
//
// defines
//

#define MAX_FILENAME 1023
#define EEPROM_WAIT_LIMIT 16 

#define EEPROM_SZ 768

/* soft mac */
#define ATH_MAC_LEN                         6
#define ATH_SOFT_MAC_TMP_BUF_LEN            64
unsigned char mac_addr[ATH_MAC_LEN];
unsigned char soft_mac_tmp_buf[ATH_SOFT_MAC_TMP_BUF_LEN];
char *p_mac = NULL;
/* soft mac */

//
// static variables
//

static A_UCHAR eeprom_data[EEPROM_SZ];
static A_UINT32 sys_sleep_reg;
static HIF_DEVICE *p_bmi_device;

//
// Functions
//

/* soft mac */
static int
wmic_ether_aton(const char *orig, A_UINT8 *eth)
{
  const char *bufp;
  int i;

  i = 0;
  for(bufp = orig; *bufp != '\0'; ++bufp) {
    unsigned int val;
    unsigned char c = *bufp++;
    if (c >= '0' && c <= '9') val = c - '0';
    else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
    else {
        printk("%s: MAC value is invalid\n", __FUNCTION__);
        break;
    }

    val <<= 4;
    c = *bufp++;
    if (c >= '0' && c <= '9') val |= c - '0';
    else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
    else {
        printk("%s: MAC value is invalid\n", __FUNCTION__);
        break;
    }

    eth[i] = (unsigned char) (val & 0377);
    if(++i == ATH_MAC_LEN) {
	    /* That's it.  Any trailing junk? */
	    if (*bufp != '\0') {
		    return 0;
	    }
	    return 1;
    }
    if (*bufp != ':')
	    break;
  }
  return 0;
}

static void
update_mac(unsigned char* eeprom, int size, unsigned char* macaddr)
{
	int i;
	A_UINT16* ptr = (A_UINT16*)(eeprom+4);
	A_UINT16  checksum = 0;

	memcpy(eeprom+10,macaddr,6);

	*ptr = 0;
	ptr = (A_UINT16*)eeprom;

	for (i=0; i<size; i+=2) {
		checksum ^= *ptr++;
	}
	checksum = ~checksum;

	ptr = (A_UINT16*)(eeprom+4);
	*ptr = checksum;
	return;
}
/* soft mac */

/* Read a Target register and return its value. */
inline void
BMI_read_reg(A_UINT32 address, A_UINT32 *pvalue)
{
    BMIReadSOCRegister(p_bmi_device, address, pvalue);
}

/* Write a value to a Target register. */
inline void
BMI_write_reg(A_UINT32 address, A_UINT32 value)
{
    BMIWriteSOCRegister(p_bmi_device, address, value);
}

/* Read Target memory word and return its value. */
inline void
BMI_read_mem(A_UINT32 address, A_UINT32 *pvalue)
{
    BMIReadMemory(p_bmi_device, address, (A_UCHAR*)(pvalue), 4);
}

/* Write a word to a Target memory. */
inline void
BMI_write_mem(A_UINT32 address, A_UINT8 *p_data, A_UINT32 sz)
{
    BMIWriteMemory(p_bmi_device, address, (A_UCHAR*)(p_data), sz); 
}

/*
 * Enable and configure the Target's Serial Interface
 * so we can access the EEPROM.
 */
static void
enable_SI(HIF_DEVICE *p_device)
{
    A_UINT32 regval;

    printk("%s\n", __FUNCTION__);

    p_bmi_device = p_device;

    BMI_read_reg(RTC_WMAC_BASE_ADDRESS+WLAN_SYSTEM_SLEEP_OFFSET, &sys_sleep_reg);
    BMI_write_reg(RTC_WMAC_BASE_ADDRESS+WLAN_SYSTEM_SLEEP_OFFSET, SYSTEM_SLEEP_DISABLE_SET(1)); //disable system sleep temporarily

    BMI_read_reg(RTC_SOC_BASE_ADDRESS+CLOCK_CONTROL_OFFSET, &regval);
    regval &= ~CLOCK_CONTROL_SI0_CLK_MASK;
    BMI_write_reg(RTC_SOC_BASE_ADDRESS+CLOCK_CONTROL_OFFSET, regval);

    BMI_read_reg(RTC_SOC_BASE_ADDRESS+RESET_CONTROL_OFFSET, &regval);
    regval &= ~RESET_CONTROL_SI0_RST_MASK;
    BMI_write_reg(RTC_SOC_BASE_ADDRESS+RESET_CONTROL_OFFSET, regval);


    BMI_read_reg(GPIO_BASE_ADDRESS+GPIO_PIN0_OFFSET, &regval);
    regval &= ~GPIO_PIN0_CONFIG_MASK;
    BMI_write_reg(GPIO_BASE_ADDRESS+GPIO_PIN0_OFFSET, regval);

    BMI_read_reg(GPIO_BASE_ADDRESS+GPIO_PIN1_OFFSET, &regval);
    regval &= ~GPIO_PIN1_CONFIG_MASK;
    BMI_write_reg(GPIO_BASE_ADDRESS+GPIO_PIN1_OFFSET, regval);

    /* SI_CONFIG = 0x500a6; */
    regval =    SI_CONFIG_BIDIR_OD_DATA_SET(1)  |
                SI_CONFIG_I2C_SET(1)            |
                SI_CONFIG_POS_SAMPLE_SET(1)     |
                SI_CONFIG_INACTIVE_CLK_SET(1)   |
                SI_CONFIG_INACTIVE_DATA_SET(1)   |
                SI_CONFIG_DIVIDER_SET(6);
    BMI_write_reg(SI_BASE_ADDRESS+SI_CONFIG_OFFSET, regval);
    
}

static void
disable_SI(void)
{
    A_UINT32 regval;
    
    printk("%s\n", __FUNCTION__);

    BMI_write_reg(RTC_SOC_BASE_ADDRESS+RESET_CONTROL_OFFSET, RESET_CONTROL_SI0_RST_MASK);
    BMI_read_reg(RTC_SOC_BASE_ADDRESS+CLOCK_CONTROL_OFFSET, &regval);
    regval |= CLOCK_CONTROL_SI0_CLK_MASK;
    BMI_write_reg(RTC_SOC_BASE_ADDRESS+CLOCK_CONTROL_OFFSET, regval);//Gate SI0 clock
    BMI_write_reg(RTC_WMAC_BASE_ADDRESS+WLAN_SYSTEM_SLEEP_OFFSET, sys_sleep_reg); //restore system sleep setting
}

/*
 * Tell the Target to start an 8-byte read from EEPROM,
 * putting the results in Target RX_DATA registers.
 */
static void
request_8byte_read(int offset)
{
    A_UINT32 regval;

//    printk("%s: request_8byte_read from offset 0x%x\n", __FUNCTION__, offset);

    
    /* SI_TX_DATA0 = read from offset */
        regval =(0xa1<<16)|
                ((offset & 0xff)<<8)    |
                (0xa0 | ((offset & 0xff00)>>7));
    
        BMI_write_reg(SI_BASE_ADDRESS+SI_TX_DATA0_OFFSET, regval);

        regval = SI_CS_START_SET(1)      |
                SI_CS_RX_CNT_SET(8)     |
                SI_CS_TX_CNT_SET(3);
        BMI_write_reg(SI_BASE_ADDRESS+SI_CS_OFFSET, regval);
}

/*
 * Tell the Target to start a 4-byte write to EEPROM,
 * writing values from Target TX_DATA registers.
 */
static void
request_4byte_write(int offset, A_UINT32 data)
{
    A_UINT32 regval;

    printk("%s: request_4byte_write (0x%x) to offset 0x%x\n", __FUNCTION__, data, offset);

        /* SI_TX_DATA0 = write data to offset */
        regval =    ((data & 0xffff) <<16)    |
                ((offset & 0xff)<<8)    |
                (0xa0 | ((offset & 0xff00)>>7));
        BMI_write_reg(SI_BASE_ADDRESS+SI_TX_DATA0_OFFSET, regval);

        regval =    data >> 16;
        BMI_write_reg(SI_BASE_ADDRESS+SI_TX_DATA1_OFFSET, regval);

        regval =    SI_CS_START_SET(1)      |
                SI_CS_RX_CNT_SET(0)     |
                SI_CS_TX_CNT_SET(6);
        BMI_write_reg(SI_BASE_ADDRESS+SI_CS_OFFSET, regval);
}

/*
 * Check whether or not an EEPROM request that was started
 * earlier has completed yet.
 */
static A_BOOL
request_in_progress(void)
{
    A_UINT32 regval;

    /* Wait for DONE_INT in SI_CS */
    BMI_read_reg(SI_BASE_ADDRESS+SI_CS_OFFSET, &regval);

//    printk("%s: request in progress SI_CS=0x%x\n", __FUNCTION__, regval);
    if (regval & SI_CS_DONE_ERR_MASK) {
        printk("%s: EEPROM signaled ERROR (0x%x)\n", __FUNCTION__, regval);
    }

    return (!(regval & SI_CS_DONE_INT_MASK));
}

/*
 * try to detect the type of EEPROM,16bit address or 8bit address
 */

static void eeprom_type_detect(void)
{
    A_UINT32 regval;
    A_UINT8 i = 0;

    request_8byte_read(0x100);
   /* Wait for DONE_INT in SI_CS */
    do{
        BMI_read_reg(SI_BASE_ADDRESS+SI_CS_OFFSET, &regval);
        if (regval & SI_CS_DONE_ERR_MASK) {
            printk("%s: ERROR : address type was wrongly set\n", __FUNCTION__);     
            break;
        }
        if (i++ == EEPROM_WAIT_LIMIT) {
            printk("%s: EEPROM not responding\n", __FUNCTION__);
        }
    } while(!(regval & SI_CS_DONE_INT_MASK));
}

/*
 * Extract the results of a completed EEPROM Read request
 * and return them to the caller.
 */
inline void
read_8byte_results(A_UINT32 *data)
{
    /* Read SI_RX_DATA0 and SI_RX_DATA1 */
    BMI_read_reg(SI_BASE_ADDRESS+SI_RX_DATA0_OFFSET, &data[0]);
    BMI_read_reg(SI_BASE_ADDRESS+SI_RX_DATA1_OFFSET, &data[1]);
}


/*
 * Wait for a previously started command to complete.
 * Timeout if the command is takes "too long".
 */
static void
wait_for_eeprom_completion(void)
{
    int i=0;

    while (request_in_progress()) {
        if (i++ == EEPROM_WAIT_LIMIT) {
            printk("%s: EEPROM not responding\n", __FUNCTION__);
        }
    }
}

/*
 * High-level function which starts an 8-byte read,
 * waits for it to complete, and returns the result.
 */
static void
fetch_8bytes(int offset, A_UINT32 *data)
{
    request_8byte_read(offset);
    wait_for_eeprom_completion();
    read_8byte_results(data);

    /* Clear any pending intr */
    BMI_write_reg(SI_BASE_ADDRESS+SI_CS_OFFSET, SI_CS_DONE_INT_MASK);
}

/*
 * High-level function which starts a 4-byte write,
 * and waits for it to complete.
 */
inline void
commit_4bytes(int offset, A_UINT32 data)
{
    request_4byte_write(offset, data);
    wait_for_eeprom_completion();
}
/* ATHENV */
#ifdef ANDROID_ENV
void eeprom_ar6000_transfer(HIF_DEVICE *device, char *fake_file, char *p_mac)
{
    A_UINT32 first_word;
    A_UINT32 board_data_addr;
    int i;

    printk("%s: Enter\n", __FUNCTION__);

    enable_SI(device);
    eeprom_type_detect();

    if (fake_file) {
        /*
         * Transfer from file to Target RAM.
         * Fetch source data from file.
         */
        mm_segment_t		oldfs;
        struct file		*filp;
        struct inode		*inode = NULL;
        int			length;

        /* open file */
        oldfs = get_fs();
        set_fs(KERNEL_DS);
        filp = filp_open(fake_file, O_RDONLY, S_IRUSR);

        if (IS_ERR(filp)) {
            printk("%s: file %s filp_open error\n", __FUNCTION__, fake_file);
            set_fs(oldfs);
            return;
        }

        if (!filp->f_op) {
            printk("%s: File Operation Method Error\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
            return;
        }

        inode = GET_INODE_FROM_FILEP(filep);
        if (!inode) {
            printk("%s: Get inode from filp failed\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
            return;
        }

        printk("%s file offset opsition: %xh\n", __FUNCTION__, (unsigned)filp->f_pos);

        /* file's size */
        length = i_size_read(inode->i_mapping->host);
        printk("%s: length=%d\n", __FUNCTION__, length);
        if (length != EEPROM_SZ) {
            printk("%s: The file's size is not as expected\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
            return;
        }

        /* read data */
        if (filp->f_op->read(filp, eeprom_data, length, &filp->f_pos) != length) {
            printk("%s: file read error\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
            return;
        }

        /* read data out successfully */
        filp_close(filp, NULL);
        set_fs(oldfs);
    } else {
        /*
         * Read from EEPROM to file OR transfer from EEPROM to Target RAM.
         * Fetch EEPROM_SZ Bytes of Board Data, 8 bytes at a time.
         */

        fetch_8bytes(0, (A_UINT32 *)(&eeprom_data[0]));

        /* Check the first word of EEPROM for validity */
        first_word = *((A_UINT32 *)eeprom_data);

        if ((first_word == 0) || (first_word == 0xffffffff)) {
            printk("Did not find EEPROM with valid Board Data.\n");
        }

        for (i=8; i<EEPROM_SZ; i+=8) {
            fetch_8bytes(i, (A_UINT32 *)(&eeprom_data[i]));
        }
    }

    /* soft mac */
    if (p_mac) {

        mm_segment_t		oldfs;
        struct file		*filp;
        struct inode		*inode = NULL;
        int			length;
        
        /* open file */
        oldfs = get_fs();
        set_fs(KERNEL_DS);
        filp = filp_open(p_mac, O_RDONLY, S_IRUSR);
        
        printk("%s try to open file %s\n", __FUNCTION__, p_mac);

        if (IS_ERR(filp)) {
            printk("%s: file %s filp_open error\n", __FUNCTION__, p_mac);
            set_fs(oldfs);
            return;
        }
        
        if (!filp->f_op) {
            printk("%s: File Operation Method Error\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
            return;
        }
        
        inode = GET_INODE_FROM_FILEP(filep);
        if (!inode) {
            printk("%s: Get inode from filp failed\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
            return;
        }
        
        printk("%s file offset opsition: %xh\n", __FUNCTION__, (unsigned)filp->f_pos);
        
        /* file's size */
        length = i_size_read(inode->i_mapping->host);
        printk("%s: length=%d\n", __FUNCTION__, length);
        if (length > ATH_SOFT_MAC_TMP_BUF_LEN) {
            printk("%s: MAC file's size is not as expected\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
            return;
        }
        
        /* read data */
        if (filp->f_op->read(filp, soft_mac_tmp_buf, length, &filp->f_pos) != length) {
            printk("%s: file read error\n", __FUNCTION__);
            filp_close(filp, NULL);
            set_fs(oldfs);
            return;
        }

#if 0
        /* the data we just read */
        printk("%s: mac address from the file:\n", __FUNCTION__);
        for (i = 0; i < length; i++)
            printk("[%c(0x%x)],", soft_mac_tmp_buf[i], soft_mac_tmp_buf[i]);
        printk("\n");
#endif

        /* read data out successfully */
        filp_close(filp, NULL);
        set_fs(oldfs);

        /* convert mac address */
        if (!wmic_ether_aton(soft_mac_tmp_buf, mac_addr)) {
            printk("%s: convert mac value fail\n", __FUNCTION__);
            return;
        }

#if 0
        /* the converted mac address */
        printk("%s: the converted mac value\n", __FUNCTION__);
        for (i = 0; i < ATH_MAC_LEN; i++)
            printk("[0x%x],", mac_addr[i]);
        printk("\n");
#endif
    }
    /* soft mac */

    /* Determine where in Target RAM to write Board Data */
    BMI_read_mem( AR6002_HOST_INTEREST_ITEM_ADDRESS(hi_board_data), &board_data_addr);
    if (board_data_addr == 0) {
        printk("hi_board_data is zero\n");
    }

    /* soft mac */
#if 1
    /* Update MAC address in RAM */
    if (p_mac) {
	    update_mac(eeprom_data, EEPROM_SZ, mac_addr);
    }
#endif
#if 0
    /* mac address in eeprom array */
    printk("%s: mac values in eeprom array\n", __FUNCTION__);
    for (i = 10; i < 10 + 6; i++)
        printk("[0x%x],", eeprom_data[i]);
    printk("\n");
#endif
    /* soft mac */

    /* Write EEPROM data to Target RAM */
    BMI_write_mem(board_data_addr, ((A_UINT8 *)eeprom_data), EEPROM_SZ);

    /* Record the fact that Board Data IS initialized */
    {
       A_UINT32 one = 1;
       BMI_write_mem(AR6002_HOST_INTEREST_ITEM_ADDRESS(hi_board_data_initialized),
                     (A_UINT8 *)&one, sizeof(A_UINT32));
    }

    disable_SI();
}
