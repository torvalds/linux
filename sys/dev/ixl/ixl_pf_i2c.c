/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#include "ixl_pf.h"

#define IXL_I2C_T_RISE		1
#define IXL_I2C_T_FALL		1
#define IXL_I2C_T_SU_DATA	1
#define IXL_I2C_T_SU_STA	5
#define IXL_I2C_T_SU_STO	4
#define IXL_I2C_T_HD_STA	4
#define IXL_I2C_T_LOW		5
#define IXL_I2C_T_HIGH		4
#define IXL_I2C_T_BUF		5
#define IXL_I2C_CLOCK_STRETCHING_TIMEOUT 500

#define IXL_I2C_REG(_hw)	\
    I40E_GLGEN_I2CPARAMS(_hw->func_caps.mdio_port_num)

/* I2C bit-banging functions */
static s32	ixl_set_i2c_data(struct ixl_pf *pf, u32 *i2cctl, bool data);
static bool	ixl_get_i2c_data(struct ixl_pf *pf, u32 *i2cctl);
static void	ixl_raise_i2c_clk(struct ixl_pf *pf, u32 *i2cctl);
static void	ixl_lower_i2c_clk(struct ixl_pf *pf, u32 *i2cctl);
static s32	ixl_clock_out_i2c_bit(struct ixl_pf *pf, bool data);
static s32	ixl_get_i2c_ack(struct ixl_pf *pf);
static s32	ixl_clock_out_i2c_byte(struct ixl_pf *pf, u8 data);
static s32	ixl_clock_in_i2c_bit(struct ixl_pf *pf, bool *data);
static s32	ixl_clock_in_i2c_byte(struct ixl_pf *pf, u8 *data);
static void 	ixl_i2c_bus_clear(struct ixl_pf *pf);
static void	ixl_i2c_start(struct ixl_pf *pf);
static void	ixl_i2c_stop(struct ixl_pf *pf);

static s32	ixl_wait_for_i2c_completion(struct i40e_hw *hw, u8 portnum);

/**
 *  ixl_i2c_bus_clear - Clears the I2C bus
 *  @hw: pointer to hardware structure
 *
 *  Clears the I2C bus by sending nine clock pulses.
 *  Used when data line is stuck low.
 **/
static void
ixl_i2c_bus_clear(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 i2cctl = rd32(hw, IXL_I2C_REG(hw));
	u32 i;

	DEBUGFUNC("ixl_i2c_bus_clear");

	ixl_i2c_start(pf);

	ixl_set_i2c_data(pf, &i2cctl, 1);

	for (i = 0; i < 9; i++) {
		ixl_raise_i2c_clk(pf, &i2cctl);

		/* Min high period of clock is 4us */
		i40e_usec_delay(IXL_I2C_T_HIGH);

		ixl_lower_i2c_clk(pf, &i2cctl);

		/* Min low period of clock is 4.7us*/
		i40e_usec_delay(IXL_I2C_T_LOW);
	}

	ixl_i2c_start(pf);

	/* Put the i2c bus back to default state */
	ixl_i2c_stop(pf);
}

/**
 *  ixl_i2c_stop - Sets I2C stop condition
 *  @hw: pointer to hardware structure
 *
 *  Sets I2C stop condition (Low -> High on SDA while SCL is High)
 **/
static void
ixl_i2c_stop(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 i2cctl = rd32(hw, IXL_I2C_REG(hw));

	DEBUGFUNC("ixl_i2c_stop");

	/* Stop condition must begin with data low and clock high */
	ixl_set_i2c_data(pf, &i2cctl, 0);
	ixl_raise_i2c_clk(pf, &i2cctl);

	/* Setup time for stop condition (4us) */
	i40e_usec_delay(IXL_I2C_T_SU_STO);

	ixl_set_i2c_data(pf, &i2cctl, 1);

	/* bus free time between stop and start (4.7us)*/
	i40e_usec_delay(IXL_I2C_T_BUF);
}

/**
 *  ixl_clock_in_i2c_byte - Clocks in one byte via I2C
 *  @hw: pointer to hardware structure
 *  @data: data byte to clock in
 *
 *  Clocks in one byte data via I2C data/clock
 **/
static s32
ixl_clock_in_i2c_byte(struct ixl_pf *pf, u8 *data)
{
	s32 i;
	bool bit = 0;

	DEBUGFUNC("ixl_clock_in_i2c_byte");

	for (i = 7; i >= 0; i--) {
		ixl_clock_in_i2c_bit(pf, &bit);
		*data |= bit << i;
	}

	return I40E_SUCCESS;
}

/**
 *  ixl_clock_in_i2c_bit - Clocks in one bit via I2C data/clock
 *  @hw: pointer to hardware structure
 *  @data: read data value
 *
 *  Clocks in one bit via I2C data/clock
 **/
static s32
ixl_clock_in_i2c_bit(struct ixl_pf *pf, bool *data)
{
	struct i40e_hw *hw = &pf->hw;
	u32 i2cctl = rd32(hw, IXL_I2C_REG(hw));

	DEBUGFUNC("ixl_clock_in_i2c_bit");

	ixl_raise_i2c_clk(pf, &i2cctl);

	/* Minimum high period of clock is 4us */
	i40e_usec_delay(IXL_I2C_T_HIGH);

	i2cctl = rd32(hw, IXL_I2C_REG(hw));
	i2cctl |= I40E_GLGEN_I2CPARAMS_DATA_OE_N_MASK;
	wr32(hw, IXL_I2C_REG(hw), i2cctl);
	ixl_flush(hw);

	i2cctl = rd32(hw, IXL_I2C_REG(hw));
	*data = ixl_get_i2c_data(pf, &i2cctl);

	ixl_lower_i2c_clk(pf, &i2cctl);

	/* Minimum low period of clock is 4.7 us */
	i40e_usec_delay(IXL_I2C_T_LOW);

	return I40E_SUCCESS;
}

/**
 *  ixl_get_i2c_ack - Polls for I2C ACK
 *  @hw: pointer to hardware structure
 *
 *  Clocks in/out one bit via I2C data/clock
 **/
static s32
ixl_get_i2c_ack(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	s32 status = I40E_SUCCESS;
	u32 i = 0;
	u32 i2cctl = rd32(hw, IXL_I2C_REG(hw));
	u32 timeout = 10;
	bool ack = 1;

	ixl_raise_i2c_clk(pf, &i2cctl);

	/* Minimum high period of clock is 4us */
	i40e_usec_delay(IXL_I2C_T_HIGH);

	i2cctl = rd32(hw, IXL_I2C_REG(hw));
	i2cctl |= I40E_GLGEN_I2CPARAMS_DATA_OE_N_MASK;
	wr32(hw, IXL_I2C_REG(hw), i2cctl);
	ixl_flush(hw);

	/* Poll for ACK.  Note that ACK in I2C spec is
	 * transition from 1 to 0 */
	for (i = 0; i < timeout; i++) {
		i2cctl = rd32(hw, IXL_I2C_REG(hw));
		ack = ixl_get_i2c_data(pf, &i2cctl);

		i40e_usec_delay(1);
		if (!ack)
			break;
	}

	if (ack) {
		ixl_dbg(pf, IXL_DBG_I2C, "I2C ack was not received.\n");
		status = I40E_ERR_PHY;
	}

	ixl_lower_i2c_clk(pf, &i2cctl);

	/* Minimum low period of clock is 4.7 us */
	i40e_usec_delay(IXL_I2C_T_LOW);

	return status;
}

/**
 *  ixl_clock_out_i2c_bit - Clocks in/out one bit via I2C data/clock
 *  @hw: pointer to hardware structure
 *  @data: data value to write
 *
 *  Clocks out one bit via I2C data/clock
 **/
static s32
ixl_clock_out_i2c_bit(struct ixl_pf *pf, bool data)
{
	struct i40e_hw *hw = &pf->hw;
	s32 status;
	u32 i2cctl = rd32(hw, IXL_I2C_REG(hw));

	status = ixl_set_i2c_data(pf, &i2cctl, data);
	if (status == I40E_SUCCESS) {
		ixl_raise_i2c_clk(pf, &i2cctl);

		/* Minimum high period of clock is 4us */
		i40e_usec_delay(IXL_I2C_T_HIGH);

		ixl_lower_i2c_clk(pf, &i2cctl);

		/* Minimum low period of clock is 4.7 us.
		 * This also takes care of the data hold time.
		 */
		i40e_usec_delay(IXL_I2C_T_LOW);
	} else {
		status = I40E_ERR_PHY;
		ixl_dbg(pf, IXL_DBG_I2C, "I2C data was not set to %#x\n", data);
	}

	return status;
}

/**
 *  ixl_clock_out_i2c_byte - Clocks out one byte via I2C
 *  @hw: pointer to hardware structure
 *  @data: data byte clocked out
 *
 *  Clocks out one byte data via I2C data/clock
 **/
static s32
ixl_clock_out_i2c_byte(struct ixl_pf *pf, u8 data)
{
	struct i40e_hw *hw = &pf->hw;
	s32 status = I40E_SUCCESS;
	s32 i;
	u32 i2cctl;
	bool bit;

	DEBUGFUNC("ixl_clock_out_i2c_byte");

	for (i = 7; i >= 0; i--) {
		bit = (data >> i) & 0x1;
		status = ixl_clock_out_i2c_bit(pf, bit);

		if (status != I40E_SUCCESS)
			break;
	}

	/* Release SDA line (set high) */
	i2cctl = rd32(hw, IXL_I2C_REG(hw));
	i2cctl |= I40E_GLGEN_I2CPARAMS_DATA_OUT_MASK;
	i2cctl &= ~(I40E_GLGEN_I2CPARAMS_DATA_OE_N_MASK);
	wr32(hw, IXL_I2C_REG(hw), i2cctl);
	ixl_flush(hw);

	return status;
}

/**
 *  ixl_lower_i2c_clk - Lowers the I2C SCL clock
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *
 *  Lowers the I2C clock line '1'->'0'
 **/
static void
ixl_lower_i2c_clk(struct ixl_pf *pf, u32 *i2cctl)
{
	struct i40e_hw *hw = &pf->hw;

	*i2cctl &= ~(I40E_GLGEN_I2CPARAMS_CLK_MASK);
	*i2cctl &= ~(I40E_GLGEN_I2CPARAMS_CLK_OE_N_MASK);

	wr32(hw, IXL_I2C_REG(hw), *i2cctl);
	ixl_flush(hw);

	/* SCL fall time (300ns) */
	i40e_usec_delay(IXL_I2C_T_FALL);
}

/**
 *  ixl_raise_i2c_clk - Raises the I2C SCL clock
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *
 *  Raises the I2C clock line '0'->'1'
 **/
static void
ixl_raise_i2c_clk(struct ixl_pf *pf, u32 *i2cctl)
{
	struct i40e_hw *hw = &pf->hw;
	u32 i = 0;
	u32 timeout = IXL_I2C_CLOCK_STRETCHING_TIMEOUT;
	u32 i2cctl_r = 0;

	for (i = 0; i < timeout; i++) {
		*i2cctl |= I40E_GLGEN_I2CPARAMS_CLK_MASK;
		*i2cctl &= ~(I40E_GLGEN_I2CPARAMS_CLK_OE_N_MASK);

		wr32(hw, IXL_I2C_REG(hw), *i2cctl);
		ixl_flush(hw);
		/* SCL rise time (1000ns) */
		i40e_usec_delay(IXL_I2C_T_RISE);

		i2cctl_r = rd32(hw, IXL_I2C_REG(hw));
		if (i2cctl_r & I40E_GLGEN_I2CPARAMS_CLK_IN_MASK)
			break;
	}
}

/**
 *  ixl_get_i2c_data - Reads the I2C SDA data bit
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *
 *  Returns the I2C data bit value
 **/
static bool
ixl_get_i2c_data(struct ixl_pf *pf, u32 *i2cctl)
{
	bool data;

	if (*i2cctl & I40E_GLGEN_I2CPARAMS_DATA_IN_MASK)
		data = 1;
	else
		data = 0;

	return data;
}

/**
 *  ixl_set_i2c_data - Sets the I2C data bit
 *  @hw: pointer to hardware structure
 *  @i2cctl: Current value of I2CCTL register
 *  @data: I2C data value (0 or 1) to set
 *
 *  Sets the I2C data bit
 **/
static s32
ixl_set_i2c_data(struct ixl_pf *pf, u32 *i2cctl, bool data)
{
	struct i40e_hw *hw = &pf->hw;
	s32 status = I40E_SUCCESS;

	DEBUGFUNC("ixl_set_i2c_data");

	if (data)
		*i2cctl |= I40E_GLGEN_I2CPARAMS_DATA_OUT_MASK;
	else
		*i2cctl &= ~(I40E_GLGEN_I2CPARAMS_DATA_OUT_MASK);
	*i2cctl &= ~(I40E_GLGEN_I2CPARAMS_DATA_OE_N_MASK);

	wr32(hw, IXL_I2C_REG(hw), *i2cctl);
	ixl_flush(hw);

	/* Data rise/fall (1000ns/300ns) and set-up time (250ns) */
	i40e_usec_delay(IXL_I2C_T_RISE + IXL_I2C_T_FALL + IXL_I2C_T_SU_DATA);

	/* Verify data was set correctly */
	*i2cctl = rd32(hw, IXL_I2C_REG(hw));
	if (data != ixl_get_i2c_data(pf, i2cctl)) {
		status = I40E_ERR_PHY;
		ixl_dbg(pf, IXL_DBG_I2C, "Error - I2C data was not set to %X.\n", data);
	}

	return status;
}

/**
 *  ixl_i2c_start - Sets I2C start condition
 *  Sets I2C start condition (High -> Low on SDA while SCL is High)
 **/
static void
ixl_i2c_start(struct ixl_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 i2cctl = rd32(hw, IXL_I2C_REG(hw));

	DEBUGFUNC("ixl_i2c_start");

	/* Start condition must begin with data and clock high */
	ixl_set_i2c_data(pf, &i2cctl, 1);
	ixl_raise_i2c_clk(pf, &i2cctl);

	/* Setup time for start condition (4.7us) */
	i40e_usec_delay(IXL_I2C_T_SU_STA);

	ixl_set_i2c_data(pf, &i2cctl, 0);

	/* Hold time for start condition (4us) */
	i40e_usec_delay(IXL_I2C_T_HD_STA);

	ixl_lower_i2c_clk(pf, &i2cctl);

	/* Minimum low period of clock is 4.7 us */
	i40e_usec_delay(IXL_I2C_T_LOW);

}

/**
 *  ixl_read_i2c_byte_bb - Reads 8 bit word over I2C
 **/
s32
ixl_read_i2c_byte_bb(struct ixl_pf *pf, u8 byte_offset,
		  u8 dev_addr, u8 *data)
{
	struct i40e_hw *hw = &pf->hw;
	u32 max_retry = 10;
	u32 retry = 0;
	bool nack = 1;
	s32 status;
	*data = 0;

	u32 i2cctl = rd32(hw, IXL_I2C_REG(hw));
	i2cctl |= I40E_GLGEN_I2CPARAMS_I2CBB_EN_MASK;
	wr32(hw, IXL_I2C_REG(hw), i2cctl);
	ixl_flush(hw);

	do {
		ixl_i2c_start(pf);

		/* Device Address and write indication */
		status = ixl_clock_out_i2c_byte(pf, dev_addr);
		if (status != I40E_SUCCESS) {
			ixl_dbg(pf, IXL_DBG_I2C, "dev_addr clock out error\n");
			goto fail;
		}

		status = ixl_get_i2c_ack(pf);
		if (status != I40E_SUCCESS) {
			ixl_dbg(pf, IXL_DBG_I2C, "dev_addr i2c ack error\n");
			goto fail;
		}

		status = ixl_clock_out_i2c_byte(pf, byte_offset);
		if (status != I40E_SUCCESS) {
			ixl_dbg(pf, IXL_DBG_I2C, "byte_offset clock out error\n");
			goto fail;
		}

		status = ixl_get_i2c_ack(pf);
		if (status != I40E_SUCCESS) {
			ixl_dbg(pf, IXL_DBG_I2C, "byte_offset i2c ack error\n");
			goto fail;
		}

		ixl_i2c_start(pf);

		/* Device Address and read indication */
		status = ixl_clock_out_i2c_byte(pf, (dev_addr | 0x1));
		if (status != I40E_SUCCESS)
			goto fail;

		status = ixl_get_i2c_ack(pf);
		if (status != I40E_SUCCESS)
			goto fail;

		status = ixl_clock_in_i2c_byte(pf, data);
		if (status != I40E_SUCCESS)
			goto fail;

		status = ixl_clock_out_i2c_bit(pf, nack);
		if (status != I40E_SUCCESS)
			goto fail;

		ixl_i2c_stop(pf);
		status = I40E_SUCCESS;
		goto done;

fail:
		ixl_i2c_bus_clear(pf);
		i40e_msec_delay(100);
		retry++;
		if (retry < max_retry)
			ixl_dbg(pf, IXL_DBG_I2C, "I2C byte read error - Retrying\n");
		else
			ixl_dbg(pf, IXL_DBG_I2C, "I2C byte read error\n");

	} while (retry < max_retry);
done:
	i2cctl = rd32(hw, IXL_I2C_REG(hw));
	i2cctl &= ~I40E_GLGEN_I2CPARAMS_I2CBB_EN_MASK;
	wr32(hw, IXL_I2C_REG(hw), i2cctl);
	ixl_flush(hw);

	return status;
}

/**
 *  ixl_write_i2c_byte_bb - Writes 8 bit word over I2C
 **/
s32
ixl_write_i2c_byte_bb(struct ixl_pf *pf, u8 byte_offset,
		       u8 dev_addr, u8 data)
{
	struct i40e_hw *hw = &pf->hw;
	s32 status = I40E_SUCCESS;
	u32 max_retry = 1;
	u32 retry = 0;

	u32 i2cctl = rd32(hw, IXL_I2C_REG(hw));
	i2cctl |= I40E_GLGEN_I2CPARAMS_I2CBB_EN_MASK;
	wr32(hw, IXL_I2C_REG(hw), i2cctl);
	ixl_flush(hw);

	do {
		ixl_i2c_start(pf);

		status = ixl_clock_out_i2c_byte(pf, dev_addr);
		if (status != I40E_SUCCESS)
			goto fail;

		status = ixl_get_i2c_ack(pf);
		if (status != I40E_SUCCESS)
			goto fail;

		status = ixl_clock_out_i2c_byte(pf, byte_offset);
		if (status != I40E_SUCCESS)
			goto fail;

		status = ixl_get_i2c_ack(pf);
		if (status != I40E_SUCCESS)
			goto fail;

		status = ixl_clock_out_i2c_byte(pf, data);
		if (status != I40E_SUCCESS)
			goto fail;

		status = ixl_get_i2c_ack(pf);
		if (status != I40E_SUCCESS)
			goto fail;

		ixl_i2c_stop(pf);
		goto write_byte_out;

fail:
		ixl_i2c_bus_clear(pf);
		i40e_msec_delay(100);
		retry++;
		if (retry < max_retry)
			ixl_dbg(pf, IXL_DBG_I2C, "I2C byte write error - Retrying\n");
		else
			ixl_dbg(pf, IXL_DBG_I2C, "I2C byte write error\n");
	} while (retry < max_retry);

write_byte_out:
	i2cctl = rd32(hw, IXL_I2C_REG(hw));
	i2cctl &= ~I40E_GLGEN_I2CPARAMS_I2CBB_EN_MASK;
	wr32(hw, IXL_I2C_REG(hw), i2cctl);
	ixl_flush(hw);

	return status;
}

/**
 *  ixl_read_i2c_byte - Reads 8 bit word over I2C using a hardware register
 **/
s32
ixl_read_i2c_byte_reg(struct ixl_pf *pf, u8 byte_offset,
		  u8 dev_addr, u8 *data)
{
	struct i40e_hw *hw = &pf->hw;
	u32 reg = 0;
	s32 status;
	*data = 0;

	reg |= (byte_offset << I40E_GLGEN_I2CCMD_REGADD_SHIFT);
	reg |= (((dev_addr >> 1) & 0x7) << I40E_GLGEN_I2CCMD_PHYADD_SHIFT);
	reg |= I40E_GLGEN_I2CCMD_OP_MASK;
	wr32(hw, I40E_GLGEN_I2CCMD(hw->func_caps.mdio_port_num), reg);

	status = ixl_wait_for_i2c_completion(hw, hw->func_caps.mdio_port_num);

	/* Get data from I2C register */
	reg = rd32(hw, I40E_GLGEN_I2CCMD(hw->func_caps.mdio_port_num));

	/* Retrieve data readed from EEPROM */
	*data = (u8)(reg & 0xff);

	if (status)
		ixl_dbg(pf, IXL_DBG_I2C, "I2C byte read error\n");
	return status;
}

/**
 *  ixl_write_i2c_byte - Writes 8 bit word over I2C using a hardware register
 **/
s32
ixl_write_i2c_byte_reg(struct ixl_pf *pf, u8 byte_offset,
		       u8 dev_addr, u8 data)
{
	struct i40e_hw *hw = &pf->hw;
	s32 status = I40E_SUCCESS;
	u32 reg = 0;
	u8 upperbyte = 0;
	u16 datai2c = 0;

	status = ixl_read_i2c_byte_reg(pf, byte_offset + 1, dev_addr, &upperbyte);
	datai2c = ((u16)upperbyte << 8) | (u16)data;
	reg = rd32(hw, I40E_GLGEN_I2CCMD(hw->func_caps.mdio_port_num));

	/* Form write command */
	reg &= ~I40E_GLGEN_I2CCMD_PHYADD_MASK;
	reg |= (((dev_addr >> 1) & 0x7) << I40E_GLGEN_I2CCMD_PHYADD_SHIFT);
	reg &= ~I40E_GLGEN_I2CCMD_REGADD_MASK;
	reg |= (byte_offset << I40E_GLGEN_I2CCMD_REGADD_SHIFT);
	reg &= ~I40E_GLGEN_I2CCMD_DATA_MASK;
	reg |= (datai2c << I40E_GLGEN_I2CCMD_DATA_SHIFT);
	reg &= ~I40E_GLGEN_I2CCMD_OP_MASK;

	/* Write command to registers controling I2C - data and address. */
	wr32(hw, I40E_GLGEN_I2CCMD(hw->func_caps.mdio_port_num), reg);

	status = ixl_wait_for_i2c_completion(hw, hw->func_caps.mdio_port_num);

	if (status)
		ixl_dbg(pf, IXL_DBG_I2C, "I2C byte write error\n");
	return status;
}

/**
 *  ixl_wait_for_i2c_completion
 **/
static s32
ixl_wait_for_i2c_completion(struct i40e_hw *hw, u8 portnum)
{
	s32 status = 0;
	u32 timeout = 100;
	u32 reg;
	do {
		reg = rd32(hw, I40E_GLGEN_I2CCMD(portnum));
		if ((reg & I40E_GLGEN_I2CCMD_R_MASK) != 0)
			break;
		i40e_usec_delay(10);
	} while (timeout-- > 0);

	if (timeout == 0)
		return I40E_ERR_TIMEOUT;
	else
		return status;
}

/**
 *  ixl_read_i2c_byte - Reads 8 bit word over I2C using a hardware register
 **/
s32
ixl_read_i2c_byte_aq(struct ixl_pf *pf, u8 byte_offset,
		  u8 dev_addr, u8 *data)
{
	struct i40e_hw *hw = &pf->hw;
	s32 status = I40E_SUCCESS;
	u32 reg;

	status = i40e_aq_get_phy_register(hw,
					I40E_AQ_PHY_REG_ACCESS_EXTERNAL_MODULE,
					dev_addr,
					byte_offset,
					&reg, NULL);

	if (status)
		ixl_dbg(pf, IXL_DBG_I2C, "I2C byte read status %s, error %s\n",
		    i40e_stat_str(hw, status), i40e_aq_str(hw, hw->aq.asq_last_status));
	else
		*data = (u8)reg;

	return status;
}

/**
 *  ixl_write_i2c_byte - Writes 8 bit word over I2C using a hardware register
 **/
s32
ixl_write_i2c_byte_aq(struct ixl_pf *pf, u8 byte_offset,
		       u8 dev_addr, u8 data)
{
	struct i40e_hw *hw = &pf->hw;
	s32 status = I40E_SUCCESS;

	status = i40e_aq_set_phy_register(hw,
					I40E_AQ_PHY_REG_ACCESS_EXTERNAL_MODULE,
					dev_addr,
					byte_offset,
					data, NULL);

	if (status)
		ixl_dbg(pf, IXL_DBG_I2C, "I2C byte write status %s, error %s\n",
		    i40e_stat_str(hw, status), i40e_aq_str(hw, hw->aq.asq_last_status));

	return status;
}
