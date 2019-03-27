/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef _DBG_FW_FUNCS_H
#define _DBG_FW_FUNCS_H
/**************************** Public Functions *******************************/

/**
 * @brief ecore_dbg_set_bin_ptr - Sets a pointer to the binary data with debug
 * arrays.
 *
 * @param bin_ptr - a pointer to the binary data with debug arrays.
 */
enum dbg_status ecore_dbg_set_bin_ptr(const u8 * const bin_ptr);

/**
 * @brief ecore_dbg_set_app_ver - Sets the version of the calling app.
 *
 * The application should call this function with the TOOLS_VERSION
 * it compiles with. Must be called before all other debug functions.
 *
 * @return error if one of the following holds:
 *	- the specified app version is not supported
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_set_app_ver(u32 ver);

/**
 * @brief ecore_dbg_get_fw_func_ver - Returns the FW func version.
 *
 * @return the FW func version.
 */
u32 ecore_dbg_get_fw_func_ver(void);

/**
* @brief ecore_dbg_get_chip_id - Returns the FW func version.
*
* @param p_hwfn - HW device data
*
* @return the chip ID.
*/
enum chip_ids ecore_dbg_get_chip_id(struct ecore_hwfn *p_hwfn);

/**
* @brief ecore_read_regs - Reads registers into a buffer (using GRC).
*
* @param p_hwfn -	HW device data
* @param p_ptt -	Ptt window used for writing the registers.
* @param buf -	Destination buffer.
* @param addr -	Source GRC address in dwords.
* @param len -	Number of registers to read.
*/
void ecore_read_regs(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt,
					 u32 *buf,
					 u32 addr,
					 u32 len);

/**
 * @brief ecore_dbg_bus_reset - Resets the Debug block.
 *
 * After reset:
 * - The last recording is erased.
 * - Recording is directed to the internal buffer.
 * - Wrap-around recording is selected.
 * - All HW blocks are disabled.
 * - All Storms are disabled and all SEM filters are cleared.
 *
 * @param p_hwfn -		    HW device data
 * @param p_ptt -		    Ptt window used for writing the registers.
 * @param one_shot_en -     Enable/Disable one-shot recording. If disabled,
 *			    wrap-around recording is used instead.
 * @param force_hw_dwords - If set to 0, no. of HW/Storm dwords per cycle is
 *			    chosen automatically based on the enabled inputs.
 *			    Otherwise, no. of HW dwords per cycle is forced to
 *			    the specified value. Valid values: 0/2/4/8.
 * @param unify_inputs -    If true, all recorded data is associated with a
 *			    single input, as if all data was received from the
 *			    same block. Otherwise, each data unit is associated
 *			    with its original input.
 * @param grc_input_en -    Enable/Disable recording GRC input. If enabled, the
 *			    GRC input is recorded to the lsb dword of a cycle.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 *	- force_hw_dwords is invalid.
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_reset(struct ecore_hwfn *p_hwfn,
									struct ecore_ptt *p_ptt,
									bool one_shot_en,
									u8 force_hw_dwords,
									bool unify_inputs,
									bool grc_input_en);

/**
 * @brief ecore_dbg_bus_set_pci_output - Directs debug output to a PCI buffer.
 *
 * @param p_hwfn -		HW device data
 * @param p_ptt -		Ptt window used for writing the registers.
 * @param buf_size_kb - Size of PCI buffer to allocate (in KB). Must be aligned
 *			to PCI request size.
 *
 * @return error if one of the following holds:
 *	- the Debug block wasn't reset since last recording
 *	- the version wasn't set
 *	- the output was already set
 *	- the PCI buffer size is not aligned to PCI packet size
 *	- the PCI buffer allocation failed
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_set_pci_output(struct ecore_hwfn *p_hwfn,
											 struct ecore_ptt *p_ptt,
											 u16 buf_size_kb);

/**
 * @brief ecore_dbg_bus_set_nw_output - Directs debug output to the network.
 *
 * @param p_hwfn -			HW device data
 * @param p_ptt -			Ptt window used for writing the registers.
 * @param port_id -		Port ID to transmit the debug data on
 * @param dest_addr_lo32 -	Destination MAC address (for Eth header)
 * @param dest_addr_hi16
 * @param data_limit_size_kb -  Data limit size in KB (valid only for one-shot)
 *				If set to 0, data limit won't be configured. 
 * @param send_to_other_engine -If true:
 *				1) The NW output will be sent to the DBG block
 *				   of the other engine.
 *				2) port_id argument is ignored.
 *				3) rcv_from_other_engine should be set to false
 *				   The other engine DBG block should call this
 *				   function with rcv_from_other_engine set to
 *				   true.
 * @param rcv_from_other_engine-If true:
 *				1) the DBG block receives the NW output sent
 *				   from the other engine DBG block, and sends
 *				   it to a NW port in the current engine
 *				   (according to port_id).
 *				2) The src/dest addresses and eth_type
 *				   arguments are ignored.
 *				3) send_to_other_engine should be set to false.
 *				   The other engine DBG block should call this
 *				   function with send_to_other_engine set to
 *				   true.
 *
 * @return error if one of the following holds:
 *	- the Debug block wasn't reset since last recording
 *	- the version wasn't set
 *	- the output was already set
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_set_nw_output(struct ecore_hwfn *p_hwfn,
											struct ecore_ptt *p_ptt,
											u8 port_id,
											u32 dest_addr_lo32,
											u16 dest_addr_hi16,
											u16 data_limit_size_kb,
											bool send_to_other_engine,
											bool rcv_from_other_engine);

/**
 * @brief ecore_dbg_bus_enable_block - Enables recording of the specified block
 *
 * Each recording cycle contains 4 "units". If the recorded HW data requires up
 * to 4 dwords per cycle, each unit is one dword (32 bits). Otherwise, each
 * unit is 2 dwords (64 bits).
 *
 * @param p_hwfn -		HW device data
 * @param block -	block to be enabled.
 * @param line_num -	debug line number to select.
 * @param cycle_en -	4-bit value. If bit i is set, unit i is enabled.
 * @param right_shift -	number of units to  right the debug data (0-3).
 * @param force_valid - 4-bit value. If bit i is set, unit i is forced valid.
 * @param force_frame - 4-bit value. If bit i is set, the frame bit of unit i
 *			is forced.
 *
 * @return error if one of the following holds:
 *	- the Debug block wasn't reset since last recording
 *	- the version wasn't set
 *	- block is not valid
 *	- block was already enabled
 *	- cycle_en, force_valid or force_frame are wider than 4 bits
 *	- right_shift is larger than 3
 *	- cycle unit 0 is enabled, but GRC or timestamp were also enabled.
 *	- Too many inputs were enabled.
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_enable_block(struct ecore_hwfn *p_hwfn,
										   enum block_id block,
										   u8 line_num,
										   u8 cycle_en,
										   u8 right_shift,
										   u8 force_valid,
										   u8 force_frame);

/**
 * @brief ecore_dbg_bus_enable_storm - Enables recording of the specified Storm
 *
 * @param p_hwfn -		HW device data
 * @param storm -	Storm to be enabled.
 * @param storm_mode-	Storm mode
 *
 * @return error if one of the following holds:
 *	- the Debug block wasn't reset since last recording
 *	- the version wasn't set
 *	- the specified storm or mode is invalid
 *	- Storm was already enabled
 *	- only HW data can be recorded
 *	- Too many inputs were enabled.
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_enable_storm(struct ecore_hwfn *p_hwfn,
										   enum dbg_storms storm,
										   enum dbg_bus_storm_modes storm_mode);

/**
 * @brief ecore_dbg_bus_enable_timestamp - Enables timestamp recording.
 *
 * When enabled, the timestamp input is always recorded to the lsb dword of
 * a cycle, with HW ID 0.
 *
 * @param p_hwfn -	     HW device data
 * @param p_ptt -	     Ptt window used for writing the registers.
 * @param valid_en - 3-bit value. The Timestamp will be recorded in a cycle if
 *		     bit i is set and unit i+1 is valid.
 * @param frame_en - 3-bit value. The Timestamp will be recorded in a cycle if
 *		     bit i is set and unit i+1 has frame bit set.
 * @param tick_len - timestamp tick length in cycles, minus 1. A value of 0
 *		     means one cycle.
 *
 * @return error if one of the following holds:
 *	- the Debug block wasn't reset since last recording
 *	- the version wasn't set
 *	- valid_en or frame_en are wider than 4 bits
 *	- Both timestamp and GRC are enabled.
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_enable_timestamp(struct ecore_hwfn *p_hwfn,
											   struct ecore_ptt *p_ptt,
											   u8 valid_en,
											   u8 frame_en,
											   u32 tick_len);

/**
 * @brief ecore_dbg_bus_add_eid_range_sem_filter- Add Event ID range SEM filter
 *
 * @param p_hwfn -     HW device data
 * @param storm -   Storm to be filtered.
 * @param min_eid - minimal Event ID to filter on.
 * @param max_eid - maximal Event ID to filter on.
 *
 * @return error if one of the following holds:
 *	- the specified Storm is invalid
 *	- the specified Storm wasn't enabled
 *	- the EID range is not valid
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_add_eid_range_sem_filter(struct ecore_hwfn *p_hwfn,
													   enum dbg_storms storm,
													   u8 min_eid,
													   u8 max_eid);

/**
 * @brief ecore_dbg_bus_add_eid_mask_sem_filter - Add Event ID mask SEM filter
 *
 * @param p_hwfn -      HW device data
 * @param storm -    Storm to be filtered.
 * @param eid_val -  Event ID value.
 * @param eid_mask - Event ID mask. 0's in the mask = don't care bits.
 *
 * @return error if one of the following holds:
 *	- the specified Storm is invalid
 *	- the specified Storm wasn't enabled
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_add_eid_mask_sem_filter(struct ecore_hwfn *p_hwfn,
													  enum dbg_storms storm,
													  u8 eid_val,
													  u8 eid_mask);

/**
 * @brief ecore_dbg_bus_add_cid_sem_filter - Adds a CID SEM filter.
 *
 * @param p_hwfn -   HW device data
 * @param storm	- Storm to be filtered.
 * @param cid -   CID to filter on.
 *
 * @return error if one of the following holds:
 *	- the specified Storm is invalid
 *	- the specified Storm wasn't enabled
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_add_cid_sem_filter(struct ecore_hwfn *p_hwfn,
												 enum dbg_storms storm,
												 u32 cid);

/**
 * @brief ecore_dbg_bus_enable_filter - Enables the recording filter.
 *
 * A filter contains up to 4 constraints. The data is "filtered in" when the
 * added constraints hold.
 *
 * @param p_hwfn -		  HW device data
 * @param p_ptt -		  Ptt window used for writing the registers.
 * @param block -	  block to filter on.
 * @param const_msg_len	- Constant message length (in cycles) to be used for
 *			  message-based filter constraints. If set to 0,
 *			  message length is based only on frame bit received
 *			  from HW (no constant message length).
 *
 * @return error if one of the following holds:
 *	- the Debug block wasn't reset since last recording
 *	- the version wasn't set
 *	- the filter was already enabled
 *	- block is not valid or not enabled
 *	- more than 4 dwords are recorded per-cycle (forbids filters)
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_enable_filter(struct ecore_hwfn *p_hwfn,
											struct ecore_ptt *p_ptt,
											enum block_id block,
											u8 const_msg_len);

/**
 * @brief ecore_dbg_bus_enable_trigger - Enables the recording trigger.
 *
 * A trigger contains up to 3 states, where each state contains up to
 * 4 constraints. After the constraints of a state hold for a specified number
 * of times, the DBG block moves to the next state. If there's no next state,
 * the DBG block triggers.
 *
 * @param p_hwfn -			HW device data
 * @param p_ptt -			Ptt window used for writing the registers.
 * @param rec_pre_trigger -	if true, recording starts before the trigger.
 *				if false, recording starts at the trigger.
 * @param pre_chunks -		max number of chunks to record before the
 *				trigger (1-47). If set to 0, recording starts
 *				from time 0. Ignored if rec_pre_trigger is
 *				false.
 * @param rec_post_trigger -	if true, recording ends after the trigger.
 *				if false, recording ends at the trigger.
 * @param post_cycles -		max number of cycles to record after the
 *				trigger (0x1-0xffffffff). If set to 0,
 *				recording ends only when stopped by the user.
 *				Ignored if rec_post_trigger is false.
 * @param filter_pre_trigger -	if true, data is filtered before the trigger.
 *				Ignored if the filter wasn't enabled.
 * @param filter_post_trigger -	if true, data is filtered after the trigger.
 *				Ignored if the filter wasn't enabled.
 *
 * @return error if one of the following holds:
 *	- the Debug block wasn't reset since last recording
 *	- the version wasn't set
 *	- the trigger was already enabled
 *	- more than 4 dwords are recorded per-cycle (forbids triggers)
 *	- pre_chunks is not in the range 0-47. 
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_enable_trigger(struct ecore_hwfn *p_hwfn,
											 struct ecore_ptt *p_ptt,
											 bool rec_pre_trigger,
											 u8 pre_chunks,
											 bool rec_post_trigger,
											 u32 post_cycles,
											 bool filter_pre_trigger,
											 bool filter_post_trigger);

/**
 * @brief ecore_dbg_bus_add_trigger_state - Adds a trigger state.
 *
 * Up to 3 trigger states can be added, where each state contains up to
 * 4 constraints. After the constraints of a state hold for the specified
 * number of times, the DBG block moves to the next state. If there's no next
 * state, the DBG block triggers.
 *
 * @param p_hwfn -		  HW device data
 * @param p_ptt -		  Ptt window used for writing the registers.
 * @param block	-	  block to trigger on.
 * @param const_msg_len	- Constant message length (in cycles) to be used for
 *			  message-based filter constraints. If set to 0,
 *			  message length is based only on frame bit received
 *			  from HW (no constant message length).
 * @param count_to_next	- The number of times the constraints of the state
 *			  should hold before moving to the next state. Must be
 *			  non-zero.
 *
 * @return error if one of the following holds:
 *	- The trigger wasn't enabled.
 *	- more than 3 trigger states were added
 *	- block is not valid or not enabled
 *	- count_to_next is 0
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_add_trigger_state(struct ecore_hwfn *p_hwfn,
												struct ecore_ptt *p_ptt,
												enum block_id block,
												u8 const_msg_len,
												u16 count_to_next);

/**
 * @brief ecore_dbg_bus_add_constraint - Adds a filter/trigger constraint.
 *
 * The constraint is added to a filter or trigger state, which ever was added
 * last. The filter/trigger happens if both of the following hold:
 * 1. All mandatory constraints are true.
 * 2. At least one optional (non-mandatory) constraints is true.
 *
 * @param p_hwfn -			  HW device data
 * @param p_ptt -			  Ptt window used for writing the registers.
 * @param op -			  constraint operation
 * @param data -		  32-bit data to compare with the recorded
 *				  data.
 * @param data_mask -		  32-bit mask for data comparison. If mask bit
 *				  i is 1, data bit i is compared, otherwise
 *				  it's ignored.
 *				  For eq/ne operations: any mask can be used.
 *				  For other operations: the mask must be
 *				  non-zero, and the 1's in the mask must be
 *				  continuous.
 * @param compare_frame -	  indicates if the frame bit should be
 *				  compared. Must be false for all operations
 *				  other than eq/ne.
 * @param frame_bit -		  frame bit to compare with the recorded data
 *				  (0/1). ignored if compare_frame is false.
 * @param cycle_offset -	  offset in cycles from the beginning of the
 *				  message, where cycle = 4 dwords.
 * @param dword_offset_in_cycle	- offset in dwords from the beginning of the
 *				  cycle (0-3).
 * @param is_mandatory -	  indicates if this constraint is mandatory
 *				  (true) or optional (false). The data is
 *				  filtered-in if all mandatory constraints hold
 *				  AND at least one optional constraint (if
 *				  added) holds.
 *
 * @return error if one of the following holds:
 *	- a filter or trigger state weren't enabled
 *	- all 4 filter constraints were added already
 *	- the op string is invalid
 *	- the data mask is invalid.
 *	- frame bit is not 0/1.
 *	- cycle_offset and dword_offset are not in the range 0-3.
 *	- compare_frame is true and operation is not eq/ne.
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_add_constraint(struct ecore_hwfn *p_hwfn,
											 struct ecore_ptt *p_ptt,
											 enum dbg_bus_constraint_ops constraint_op,
											 u32 data,
											 u32 data_mask,
											 bool compare_frame,
											 u8 frame_bit,
											 u8 cycle_offset,
											 u8 dword_offset_in_cycle,
											 bool is_mandatory);

/**
 * @brief ecore_dbg_bus_start - Starts the recording.
 *
 * @param p_hwfn - HW device data
 * @param p_ptt - Ptt window used for writing the registers.
 *
 * @return error if one of the following holds:
 *	- the Debug block wasn't reset since last recording
 *	- the version wasn't set
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_start(struct ecore_hwfn *p_hwfn,
									struct ecore_ptt *p_ptt);

/**
 * @brief ecore_dbg_bus_stop - Stops the recording and flushes the internal
 * buffer.
 *
 * @param p_hwfn - HW device data
 * @param p_ptt - Ptt window used for writing the registers.
 *
 * @return error if a recording is not in progress, ok otherwise.
 */
enum dbg_status ecore_dbg_bus_stop(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt);

/**
 * @brief ecore_dbg_bus_get_dump_buf_size - Returns the required buffer size
 * for Debug Bus recording.
 *
 * @param p_hwfn -      HW device data
 * @param p_ptt -	     Ptt window used for writing the registers.
 * @param buf_size - OUT: the required size (in dwords) of the buffer for
 *		     dumping the recorded Debug Bus data. If recording to the
 *		     internal buffer, the size of the internal buffer is
 *		     returned. If recording to PCI, the size of the PCI buffer
 *		     is returned. Otherwise, 0 is returned.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
												struct ecore_ptt *p_ptt,
												u32 *buf_size);

/**
 * @brief ecore_dbg_bus_dump - Dumps the recorded Debug Bus data into the
 * specified buffer.
 *
 * The dumped data starts with a header. If recording to NW, only a header is
 * dumped. The dumped size is assigned to num_dumped_dwords.
 *
 * @param p_hwfn -			HW device data
 * @param p_ptt -			Ptt window used for writing the registers.
 * @param dump_buf -		Pointer to copy the recorded data into.
 * @param buf_size_in_dwords -	Size of the specified buffer in dwords.
 * @param num_dumped_dwords -	OUT: number of dumped dwords.
 *
 * @return error if one of the following holds:
 *	- a recording wasn't started/stopped
 *	- the specified dump buffer is too small
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_bus_dump(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt,
								   u32 *dump_buf,
								   u32 buf_size_in_dwords,
								   u32 *num_dumped_dwords);

/**
 * @brief ecore_dbg_grc_config - Sets the value of a GRC parameter.
 *
 * @param p_hwfn -		HW device data
 * @param grc_param -	GRC parameter
 * @param val -		Value to set.

 * @return error if one of the following holds:
 *	- the version wasn't set
 *	- grc_param is invalid
 *	- val is outside the allowed boundaries
 */
enum dbg_status ecore_dbg_grc_config(struct ecore_hwfn *p_hwfn,
									 enum dbg_grc_params grc_param,
									 u32 val);

/**
* @brief ecore_dbg_grc_set_params_default - Reverts all GRC parameters to their
* default value.
*
* @param p_hwfn - HW device data
*/
void ecore_dbg_grc_set_params_default(struct ecore_hwfn *p_hwfn);

/**
 * @brief ecore_dbg_grc_get_dump_buf_size - Returns the required buffer size
 * for GRC Dump.
 *
 * @param p_hwfn -      HW device data
 * @param p_ptt -	     Ptt window used for writing the registers.
 * @param buf_size - OUT: required buffer size (in dwords) for GRC Dump data.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_grc_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
												struct ecore_ptt *p_ptt,
												u32 *buf_size);

/**
 * @brief ecore_dbg_grc_dump - Dumps GRC data into the specified buffer.
 *
 * @param p_hwfn -			HW device data
 * @param p_ptt -			Ptt window used for writing the registers.
 * @param dump_buf -		Pointer to write the collected GRC data into.
 * @param buf_size_in_dwords -	Size of the specified buffer in dwords.
 * @param num_dumped_dwords -	OUT: number of dumped dwords.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 *	- the specified dump buffer is too small
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_grc_dump(struct ecore_hwfn *p_hwfn,
								   struct ecore_ptt *p_ptt,
								   u32 *dump_buf,
								   u32 buf_size_in_dwords,
								   u32 *num_dumped_dwords);

/**
 * @brief ecore_dbg_idle_chk_get_dump_buf_size - Returns the required buffer
 * size for idle check results.
 *
 * @param p_hwfn -      HW device data
 * @param p_ptt -      Ptt window used for writing the registers.
 * @param buf_size - OUT: required buffer size (in dwords) for idle check data.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_idle_chk_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
													 struct ecore_ptt *p_ptt,
													 u32 *buf_size);

/**
 * @brief ecore_dbg_idle_chk_dump - Performs idle check and writes the results
 * into the specified buffer.
 *
 * @param p_hwfn -			HW device data
 * @param p_ptt -			Ptt window used for writing the registers.
 * @param dump_buf -		Pointer to write the idle check data into.
 * @param buf_size_in_dwords -	Size of the specified buffer in dwords.
 * @param num_dumped_dwords -	OUT: number of dumped dwords.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 *	- the specified buffer is too small
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_idle_chk_dump(struct ecore_hwfn *p_hwfn,
										struct ecore_ptt *p_ptt,
										u32 *dump_buf,
										u32 buf_size_in_dwords,
										u32 *num_dumped_dwords);

/**
 * @brief ecore_dbg_mcp_trace_get_dump_buf_size - Returns the required buffer
 * size for mcp trace results.
 *
 * @param p_hwfn -	     HW device data
 * @param p_ptt -	     Ptt window used for writing the registers.
 * @param buf_size - OUT: required buffer size (in dwords) for mcp trace data.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 *	- the trace data in MCP scratchpad contain an invalid signature
 *	- the bundle ID in NVRAM is invalid
 *	- the trace meta data cannot be found (in NVRAM or image file)
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_mcp_trace_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
													  struct ecore_ptt *p_ptt,
													  u32 *buf_size);

/**
 * @brief ecore_dbg_mcp_trace_dump - Performs mcp trace and writes the results
 * into the specified buffer.
 *
 * @param p_hwfn -			HW device data
 * @param p_ptt -			Ptt window used for writing the registers.
 * @param dump_buf -		Pointer to write the mcp trace data into.
 * @param buf_size_in_dwords -	Size of the specified buffer in dwords.
 * @param num_dumped_dwords -	OUT: number of dumped dwords.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 *	- the specified buffer is too small
 *	- the trace data in MCP scratchpad contain an invalid signature
 *	- the bundle ID in NVRAM is invalid
 *	- the trace meta data cannot be found (in NVRAM or image file)
 *	- the trace meta data cannot be read (from NVRAM or image file)
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_mcp_trace_dump(struct ecore_hwfn *p_hwfn,
										 struct ecore_ptt *p_ptt,
										 u32 *dump_buf,
										 u32 buf_size_in_dwords,
										 u32 *num_dumped_dwords);

/**
 * @brief ecore_dbg_reg_fifo_get_dump_buf_size - Returns the required buffer
 * size for grc trace fifo results.
 *
 * @param p_hwfn -      HW device data
 * @param p_ptt -      Ptt window used for writing the registers.
 * @param buf_size - OUT: required buffer size (in dwords) for reg fifo data.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_reg_fifo_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
													 struct ecore_ptt *p_ptt,
													 u32 *buf_size);

/**
 * @brief ecore_dbg_reg_fifo_dump - Reads the reg fifo and writes the results
 * into the specified buffer.
 *
 * @param p_hwfn -			HW device data
 * @param p_ptt -			Ptt window used for writing the registers.
 * @param dump_buf -		Pointer to write the reg fifo data into.
 * @param buf_size_in_dwords -	Size of the specified buffer in dwords.
 * @param num_dumped_dwords -	OUT: number of dumped dwords.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 *	- the specified buffer is too small
 *	- DMAE transaction failed
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_reg_fifo_dump(struct ecore_hwfn *p_hwfn,
										struct ecore_ptt *p_ptt,
										u32 *dump_buf,
										u32 buf_size_in_dwords,
										u32 *num_dumped_dwords);

/**
* @brief ecore_dbg_igu_fifo_get_dump_buf_size - Returns the required buffer
* size for the IGU fifo results.
*
* @param p_hwfn -      HW device data
* @param p_ptt -      Ptt window used for writing the registers.
* @param buf_size - OUT: required buffer size (in dwords) for IGU fifo data.
*
* @return error if one of the following holds:
*	- the version wasn't set
* Otherwise, returns ok.
*/
enum dbg_status ecore_dbg_igu_fifo_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
													 struct ecore_ptt *p_ptt,
													 u32 *buf_size);

/**
* @brief ecore_dbg_igu_fifo_dump - Reads the IGU fifo and writes the results
* into the specified buffer.
*
* @param p_hwfn -			HW device data
* @param p_ptt -			Ptt window used for writing the registers.
* @param dump_buf -		Pointer to write the IGU fifo data into.
* @param buf_size_in_dwords -	Size of the specified buffer in dwords.
* @param num_dumped_dwords -	OUT: number of dumped dwords.
*
* @return error if one of the following holds:
*	- the version wasn't set
*	- the specified buffer is too small
*	- DMAE transaction failed
* Otherwise, returns ok.
*/
enum dbg_status ecore_dbg_igu_fifo_dump(struct ecore_hwfn *p_hwfn,
										struct ecore_ptt *p_ptt,
										u32 *dump_buf,
										u32 buf_size_in_dwords,
										u32 *num_dumped_dwords);

/**
 * @brief ecore_dbg_protection_override_get_dump_buf_size - Return the required
 * buffer size for protection override window results.
 *
 * @param p_hwfn -      HW device data
 * @param p_ptt -      Ptt window used for writing the registers.
 * @param buf_size - OUT: required buffer size (in dwords) for protection
 *		     override data.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_protection_override_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
																struct ecore_ptt *p_ptt,
																u32 *buf_size);
/**
 * @brief ecore_dbg_protection_override_dump - Reads protection override window
 * entries and writes the results into the specified buffer.
 *
 * @param p_hwfn -			HW device data
 * @param p_ptt -			Ptt window used for writing the registers.
 * @param dump_buf -		Pointer to write the protection override data
 *				into.
 * @param buf_size_in_dwords -	Size of the specified buffer in dwords.
 * @param num_dumped_dwords -	OUT: number of dumped dwords.
 *
 * @return error if one of the following holds:
 *	- the version wasn't set
 *	- the specified buffer is too small
 *	- DMAE transaction failed
 * Otherwise, returns ok.
 */
enum dbg_status ecore_dbg_protection_override_dump(struct ecore_hwfn *p_hwfn,
												   struct ecore_ptt *p_ptt,
												   u32 *dump_buf,
												   u32 buf_size_in_dwords,
												   u32 *num_dumped_dwords);

/**
* @brief ecore_dbg_fw_asserts_get_dump_buf_size - Returns the required buffer
* size for FW Asserts results.
*
* @param p_hwfn -	    HW device data
* @param p_ptt -	    Ptt window used for writing the registers.
* @param buf_size - OUT: required buffer size (in dwords) for FW Asserts data.
*
* @return error if one of the following holds:
*	- the version wasn't set
* Otherwise, returns ok.
*/
enum dbg_status ecore_dbg_fw_asserts_get_dump_buf_size(struct ecore_hwfn *p_hwfn,
													   struct ecore_ptt *p_ptt,
													   u32 *buf_size);

/**
* @brief ecore_dbg_fw_asserts_dump - Reads the FW Asserts and writes the
* results into the specified buffer.
*
* @param p_hwfn -			HW device data
* @param p_ptt -			Ptt window used for writing the registers.
* @param dump_buf -		Pointer to write the FW Asserts data into.
* @param buf_size_in_dwords -	Size of the specified buffer in dwords.
* @param num_dumped_dwords -	OUT: number of dumped dwords.
*
* @return error if one of the following holds:
*	- the version wasn't set
*	- the specified buffer is too small
* Otherwise, returns ok.
*/
enum dbg_status ecore_dbg_fw_asserts_dump(struct ecore_hwfn *p_hwfn,
										  struct ecore_ptt *p_ptt,
										  u32 *dump_buf,
										  u32 buf_size_in_dwords,
										  u32 *num_dumped_dwords);

/**
* @brief ecore_dbg_read_attn - Reads the attention registers of the specified
* block and type, and writes the results into the specified buffer.
*
* @param p_hwfn -		HW device data
* @param p_ptt -		Ptt window used for writing the registers.
* @param block -	Block ID.
* @param attn_type -	Attention type.
* @param clear_status -	Indicates if the attention status should be cleared.
* @param results -	OUT: Pointer to write the read results into
*
* @return error if one of the following holds:
*	- the version wasn't set
* Otherwise, returns ok.
*/
enum dbg_status ecore_dbg_read_attn(struct ecore_hwfn *p_hwfn,
									struct ecore_ptt *p_ptt,
									enum block_id block,
									enum dbg_attn_type attn_type,
									bool clear_status,
									struct dbg_attn_block_result *results);
									
/**
* @brief ecore_dbg_print_attn - Prints attention registers values in the
* specified results struct.
*
* @param p_hwfn -     HW device data
* @param results - Pointer to the attention read results
*
* @return error if one of the following holds:
*	- the version wasn't set
* Otherwise, returns ok.
*/
enum dbg_status ecore_dbg_print_attn(struct ecore_hwfn *p_hwfn,
									 struct dbg_attn_block_result *results);

/**
* @brief ecore_is_block_in_reset - Returns true if the specified block is in
* reset, false otherwise.
*
* @param p_hwfn   - HW device data
* @param p_ptt   - Ptt window used for writing the registers.
* @param block - Block ID.
*
* @return true if the specified block is in reset, false otherwise.
*/
bool ecore_is_block_in_reset(struct ecore_hwfn *p_hwfn,
							 struct ecore_ptt *p_ptt,
							 enum block_id block);


#endif
