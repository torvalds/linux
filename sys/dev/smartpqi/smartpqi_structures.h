/*-
 * Copyright (c) 2018 Microsemi Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $FreeBSD$ */

#ifndef _PQI_STRUCTURES_H
#define _PQI_STRUCTURES_H




struct bmic_host_wellness_driver_version {
	uint8_t		start_tag[4];
	uint8_t		driver_version_tag[2];
	uint16_t	driver_version_length;
	char		driver_version[32];
	uint8_t		end_tag[2];

}OS_ATTRIBUTE_PACKED;


struct bmic_host_wellness_time {
	uint8_t		start_tag[4];
	uint8_t		time_tag[2];
	uint16_t	time_length;
	uint8_t		hour;
	uint8_t		min;
	uint8_t		sec;
	uint8_t		reserved;
	uint8_t		month;
	uint8_t		day;
	uint8_t		century;
	uint8_t		year;
	uint8_t		dont_write_tag[2];
	uint8_t		end_tag[2];

}OS_ATTRIBUTE_PACKED;


/* As per PQI Spec pqi-2r00a , 6.2.2. */

/* device capability register , for admin q table 24 */
struct pqi_dev_adminq_cap {
	uint8_t		max_admin_ibq_elem;
	uint8_t		max_admin_obq_elem;
	uint8_t		admin_ibq_elem_len;	
	uint8_t		admin_obq_elem_len;
	uint16_t	max_pqi_dev_reset_tmo;
	uint8_t		res[2];
}OS_ATTRIBUTE_PACKED;

/* admin q parameter reg , table 36 */
struct admin_q_param {
	uint8_t		num_iq_elements;
	uint8_t		num_oq_elements;
	uint8_t		intr_msg_num;
	uint8_t		msix_disable;
}OS_ATTRIBUTE_PACKED;

struct pqi_registers {
	uint64_t		signature;
	uint64_t		admin_q_config;
	uint64_t	 	pqi_dev_adminq_cap;
	uint32_t		legacy_intr_status;
	uint32_t		legacy_intr_mask_set;
	uint32_t		legacy_intr_mask_clr;
	uint8_t			res1[28];
	uint32_t		pqi_dev_status;
	uint8_t			res2[4];
	uint64_t		admin_ibq_pi_offset;
	uint64_t		admin_obq_ci_offset;
	uint64_t		admin_ibq_elem_array_addr;
	uint64_t		admin_obq_elem_array_addr;
	uint64_t		admin_ibq_ci_addr;
	uint64_t		admin_obq_pi_addr;
	uint32_t	 	admin_q_param;		
	uint8_t			res3[4];
	uint32_t		pqi_dev_err;
	uint8_t			res4[4];
	uint64_t		error_details;
	uint32_t		dev_reset;
	uint32_t		power_action;
	uint8_t			res5[104];
}OS_ATTRIBUTE_PACKED;

/*
 * IOA controller registers
 * Mapped in PCIe BAR 0.
 */
 
struct ioa_registers {
	uint8_t		res1[0x18];				 	
	uint32_t	host_to_ioa_db_mask_clr; 	/* 18h */
	uint8_t		res2[4];				 	
	uint32_t	host_to_ioa_db;			/* 20h */
	uint8_t		res3[4];
	uint32_t	host_to_ioa_db_clr;		/* 28h */
	uint8_t		res4[8];
	uint32_t	ioa_to_host_glob_int_mask;	/* 34h */
	uint8_t		res5[0x64];	
	uint32_t	ioa_to_host_db;			/* 9Ch */
	uint32_t	ioa_to_host_db_clr;		/* A0h */
	uint8_t		res6[4];
	uint32_t	ioa_to_host_db_mask;		/* A8h */
	uint32_t	ioa_to_host_db_mask_clr;	/* ACh */
	uint32_t	scratchpad0;			/* B0h */
	uint32_t	scratchpad1;			/* B4h */
	uint32_t	scratchpad2;			/* B8h */
	uint32_t	scratchpad3_fw_status;		/* BCh */
	uint8_t		res7[8];
	uint32_t	scratchpad4;			/* C8h */
	uint8_t		res8[0xf34];			/* 0xC8 + 4 + 0xf34 = 1000h */
	uint32_t	mb[8];				/* 1000h */
}OS_ATTRIBUTE_PACKED;


/* PQI Preferred settings */
struct pqi_pref_settings {
	uint16_t	max_cmd_size;
	uint16_t	max_fib_size;
}OS_ATTRIBUTE_PACKED;

/* pqi capability by sis interface */
struct pqi_cap {
	uint32_t	max_sg_elem;
	uint32_t	max_transfer_size;
	uint32_t	max_outstanding_io;
	uint32_t	conf_tab_off;
	uint32_t	conf_tab_sz;
}OS_ATTRIBUTE_PACKED;

struct pqi_conf_table {
	uint8_t		sign[8];		/* "CFGTABLE" */
	uint32_t	first_section_off;
};

struct pqi_conf_table_section_header {
	uint16_t	section_id;
	uint16_t	next_section_off;
};

struct pqi_conf_table_general_info {
	struct pqi_conf_table_section_header header;
	uint32_t	section_len;
	uint32_t	max_outstanding_req;
	uint32_t	max_sg_size;
	uint32_t	max_sg_per_req;
};

struct pqi_conf_table_debug {
	struct pqi_conf_table_section_header header;
	uint32_t	scratchpad;
};

struct pqi_conf_table_heartbeat {
	struct pqi_conf_table_section_header header;
	uint32_t	heartbeat_counter;
};

typedef union pqi_reset_reg {
	struct {
		uint32_t reset_type : 3;
		uint32_t reserved : 2;
		uint32_t reset_action : 3;
		uint32_t hold_in_pd1 : 1;
		uint32_t reserved2 : 23;
	} bits;
	uint32_t all_bits;
}pqi_reset_reg_t;

/* Memory descriptor for DMA memory allocation */	
typedef struct dma_mem {
	void			*virt_addr;
	dma_addr_t 		dma_addr;
	uint32_t 		size;
	uint32_t 		align;
	char 			*tag;
        bus_dma_tag_t dma_tag;
        bus_dmamap_t dma_map;
}dma_mem_t;

/* Lock should be 8 byte aligned  */

#ifndef LOCKFREE_STACK

typedef struct pqi_taglist {
        uint32_t        max_elem;
        uint32_t        num_elem;
        uint32_t        head;
        uint32_t        tail;
        uint32_t       	*elem_array;
	boolean_t       lockcreated;
	char            lockname[LOCKNAME_SIZE];
	OS_LOCK_T       lock	OS_ATTRIBUTE_ALIGNED(8);
}pqi_taglist_t;

#else	/* LOCKFREE_STACK */

union head_list {
        struct {
                uint32_t        seq_no; /* To avoid aba problem */
                uint32_t        index;  /* Index at the top of the stack */
        }top;
        uint64_t        data;
};
/* lock-free stack used to push and pop the tag used for IO request */
typedef struct  lockless_stack {
        uint32_t                        *next_index_array;
        uint32_t                        num_elements;
        volatile union head_list      head	OS_ATTRIBUTE_ALIGNED(8);
}lockless_stack_t;

#endif /* LOCKFREE_STACK */

/*
 * PQI SGL descriptor layouts.
 */
/*
 * SGL (Scatter Gather List) descriptor Codes
 */

#define SGL_DESCRIPTOR_CODE_DATA_BLOCK                     0x0
#define SGL_DESCRIPTOR_CODE_BIT_BUCKET                     0x1
#define SGL_DESCRIPTOR_CODE_STANDARD_SEGMENT               0x2
#define SGL_DESCRIPTOR_CODE_LAST_STANDARD_SEGMENT          0x3
#define SGL_DESCRIPTOR_CODE_LAST_ALTERNATIVE_SGL_SEGMENT   0x4
#define SGL_DESCRIPTOR_CODE_VENDOR_SPECIFIC                0xF

typedef struct sgl_descriptor
{
	uint64_t	addr;	/* !< Bytes 0-7.  The starting 64-bit memory byte address of the data block. */
	uint32_t	length;	/* !< Bytes 8-11.  The length in bytes of the data block.  Set to 0x00000000 specifies that no data be transferred. */
	uint8_t		res[3];	/* !< Bytes 12-14. */
	uint8_t     	zero : 4; /* !< Byte 15, Bits 0-3. */
	uint8_t     	type : 4; /* !< Byte 15, Bits 4-7. sgl descriptor type */
} sg_desc_t;

/* PQI IUs */
typedef struct iu_header 
{
	uint8_t		iu_type;
	uint8_t		comp_feature;
	uint16_t	iu_length;
}OS_ATTRIBUTE_PACKED iu_header_t;


typedef struct general_admin_request /* REPORT_PQI_DEVICE_CAPABILITY, REPORT_MANUFACTURER_INFO,  REPORT_OPERATIONAL_IQ,  REPORT_OPERATIONAL_OQ all same layout. */
{
	iu_header_t	header;		/* !< Bytes 0-3. */
	uint16_t	res1;
	uint16_t	work;
	uint16_t	req_id;		/* !< Bytes 8-9. request identifier */
	uint8_t		fn_code;	/* !< Byte 10. which administrator function */
	union {
		struct {
			uint8_t		res2[33];	/* !< Bytes 11-43. function specific */
			uint32_t	buf_size;	/* !< Bytes 44-47. size in bytes of the Data-In/Out Buffer */
			sg_desc_t	sg_desc;	/* !< Bytes 48-63. SGL */ 
        } OS_ATTRIBUTE_PACKED general_func;

		struct {
			uint8_t		res1;
			uint16_t	qid;
			uint8_t		res2[2];
			uint64_t	elem_arr_addr;
			uint64_t	iq_ci_addr;
			uint16_t	num_elem;
			uint16_t	elem_len;
			uint8_t		queue_proto;
			uint8_t		arb_prio;
			uint8_t		res3[22];
			uint32_t	vend_specific;
        } OS_ATTRIBUTE_PACKED create_op_iq;

		struct {
			uint8_t		res1;
			uint16_t	qid;
			uint8_t		res2[2];
			uint64_t	elem_arr_addr;
			uint64_t	ob_pi_addr;
			uint16_t	num_elem;
			uint16_t	elem_len;
			uint8_t		queue_proto;
			uint8_t		res3[3];
			uint16_t	intr_msg_num;
			uint16_t	coales_count;
			uint32_t	min_coales_time;
			uint32_t	max_coales_time;
			uint8_t		res4[8];
			uint32_t	vend_specific;
        } OS_ATTRIBUTE_PACKED create_op_oq;

		struct {
			uint8_t		res1;
			uint16_t	qid;
			uint8_t		res2[50];
        } OS_ATTRIBUTE_PACKED delete_op_queue;

		struct {
			uint8_t		res1;
			uint16_t	qid;
			uint8_t		res2[46];
			uint32_t	vend_specific;
        } OS_ATTRIBUTE_PACKED change_op_iq_prop;

    } OS_ATTRIBUTE_PACKED req_type;

}OS_ATTRIBUTE_PACKED gen_adm_req_iu_t;


typedef struct general_admin_response {
	iu_header_t 	header;
	uint16_t	res1;
	uint16_t	work;
	uint16_t	req_id;
	uint8_t		fn_code;
	uint8_t		status;
	union {
		struct {
			uint8_t		status_desc[4];
			uint64_t	pi_offset;
			uint8_t		res[40];
        }  OS_ATTRIBUTE_PACKED  create_op_iq;

		struct {
			uint8_t		status_desc[4];
			uint64_t	ci_offset;
			uint8_t		res[40];
        }  OS_ATTRIBUTE_PACKED  create_op_oq;
    }  OS_ATTRIBUTE_PACKED  resp_type;
} OS_ATTRIBUTE_PACKED gen_adm_resp_iu_t ;

/*report and set Event config IU*/

typedef struct pqi_event_config_request {
	iu_header_t   	header;
	uint16_t	response_queue_id;	/* specifies the OQ where the response
					                    	IU is to be delivered */
	uint8_t	    	work_area[2];		/* reserved for driver use */
	uint16_t	request_id;
	union {
		uint16_t  	reserved;           /* Report event config iu */ 
		uint16_t  	global_event_oq_id; /* Set event config iu */
	}iu_specific;
	uint32_t	buffer_length;
	sg_desc_t     	sg_desc;
}pqi_event_config_request_t;
#if 0
typedef struct pqi_set_event_config_request {
	iu_header_t  header;
	uint16_t  	response_queue_id;  /* specifies the OQ where the response
													IU is to be delivered */
	uint8_t   	work_area[2];   /* reserved for driver use */
	uint16_t	request_id;
	uint16_t	global_event_oq_id;
	uint32_t	buffer_length;
	sg_desc_t 	sg_desc;
}pqi_set_event_config_request_t;
#endif
  
 /* Report/Set event config data-in/data-out buffer structure */
  
#define PQI_MAX_EVENT_DESCRIPTORS 255

struct pqi_event_descriptor {
	uint8_t  	event_type;
	uint8_t  	reserved;
	uint16_t	oq_id;
};
  
typedef struct pqi_event_config {
	uint8_t  	reserved[2];
	uint8_t  	num_event_descriptors;
	uint8_t  	reserved1;
	struct		pqi_event_descriptor descriptors[PQI_MAX_EVENT_DESCRIPTORS];
}pqi_event_config_t;

/*management response IUs */
typedef struct pqi_management_response{
   	iu_header_t		header;
	uint16_t 		reserved1;
	uint8_t 		work_area[2];
	uint16_t		req_id;
	uint8_t 		result;
	uint8_t			reserved[5];
	uint64_t		result_data;
}pqi_management_response_t;
  /*Event response IU*/
typedef struct pqi_event_response {
	iu_header_t 		header;
	uint16_t 		reserved1;
	uint8_t 		work_area[2];
	uint8_t 		event_type;
	uint8_t 		reserved2 : 7;
	uint8_t 		request_acknowledge : 1;
	uint16_t	  	event_id;
	uint32_t	  	additional_event_id;
	uint8_t 		data[16];
}pqi_event_response_t;

  /*event acknowledge IU*/
typedef struct pqi_event_acknowledge_request {
	iu_header_t 		header;
	uint16_t 		reserved1;
	uint8_t 		work_area[2];
	uint8_t 		event_type;
	uint8_t 		reserved2;
	uint16_t	  	event_id;
	uint32_t	  	additional_event_id;
}pqi_event_acknowledge_request_t;

struct pqi_event {
	boolean_t	pending;
	uint8_t	  	event_type;
	uint16_t	event_id;
	uint32_t	additional_event_id;
};


typedef struct op_q_params
{
	uint8_t		fn_code;	
	uint16_t        qid;		
	uint16_t	num_elem;
	uint16_t	elem_len;
	uint16_t	int_msg_num;
    
} OS_ATTRIBUTE_PACKED op_q_params;


/* Driver will use this structure to interpret the error 
   info element returned from a failed requests */
typedef struct raid_path_error_info_elem {
	uint8_t		data_in_result;		/* !< Byte 0.  See SOP spec Table 77. */ 
	uint8_t		data_out_result;	/* !< Byte 1.  See SOP spec Table 78. */
	uint8_t		reserved[3];		/* !< Bytes 2-4. */
	uint8_t		status;			/* !< Byte 5. See SAM-5 specification "Status" codes Table 40. Defined in Storport.h */
	uint16_t	status_qual;		/* !< Bytes 6-7. See SAM-5 specification Table 43. */
	uint16_t	sense_data_len;		/* !< Bytes 8-9. See SOP specification table 79. */
	uint16_t	resp_data_len;		/* !< Bytes 10-11. See SOP specification table 79. */
	uint32_t	data_in_transferred;	/* !< Bytes 12-15. If "dada_in_result = 0x01 (DATA_IN BUFFER UNDERFLOW)", Indicates the number of contiguous bytes starting with offset zero in Data-In buffer else Ignored. */
	uint32_t	data_out_transferred;	/* !< Bytes 16-19. If "data_out_result = 0x01 (DATA_OUT BUFFER UNDERFLOW)", Indicates the number of contiguous bytes starting with offset zero in Data-Out buffer else Ignored. */
	uint8_t		data[256];              /* !< Bytes 20-275. Response Data buffer or Sense Data buffer but not both. */
}OS_ATTRIBUTE_PACKED raid_path_error_info_elem_t;

#define PQI_ERROR_BUFFER_ELEMENT_LENGTH sizeof(raid_path_error_info_elem_t)

typedef enum error_data_present 
{
   DATA_PRESENT_NO_DATA       = 0,   /* !< No data present in Data buffer. */ 
   DATA_PRESENT_RESPONSE_DATA = 1,   /* !< Response data is present in Data buffer. */
   DATA_PRESENT_SENSE_DATA    = 2    /* !< Sense data is present in Data buffer. */
} error_data_present_t;

typedef struct aio_path_error_info_elem 
{
	uint8_t		status;			/* !< Byte 0.  See SAM-5 specification "SCSI Status" codes Table 40. Defined in Storport.h */
	uint8_t		service_resp;		/* !< Byte 1.  SCSI Service Response.  */ 
	uint8_t		data_pres;		/* !< Byte 2.  Bits [7:2] reserved. Bits [1:0] - 0=No data, 1=Response data, 2=Sense data. */
	uint8_t		reserved1;		/* !< Byte 3.  Reserved. */ 
	uint32_t	resd_count;		/* !< Bytes 4-7.  The residual data length in bytes. Need the original transfer size and if Status is OverRun or UnderRun. */
	uint16_t	data_len;		/* !< Bytes 8-9.  The amount of Sense data or Response data returned in Response/Sense Data buffer. */ 
	uint16_t	reserved2;		/* !< Bytes 10.  Reserved. */
	uint8_t		data[256];		/* !< Bytes 11-267. Response data buffer or Sense data buffer but not both. */
	uint8_t		padding[8];		/* !< Bytes 268-275.  Padding to make AIO_PATH_ERROR_INFO_ELEMENT = RAID_PATH_ERROR_INFO_ELEMENT */ 
}OS_ATTRIBUTE_PACKED aio_path_error_info_elem_t;

struct init_base_struct {
	uint32_t	revision;		/* revision of init structure */
	uint32_t	flags;			/* reserved */
	uint32_t	err_buf_paddr_l;	/* lower 32 bits of physical address of error buffer */
	uint32_t	err_buf_paddr_h;	/* upper 32 bits of physical address of error buffer */
	uint32_t	err_buf_elem_len;	/* length of each element in error buffer (in bytes) */
	uint32_t	err_buf_num_elem;	/* number of elements in error buffer */
}OS_ATTRIBUTE_PACKED; 

/* Queue details */
typedef struct ib_queue {
	uint32_t	q_id;
	uint32_t	num_elem;
	uint32_t	elem_size;
	char 		*array_virt_addr;
	dma_addr_t	array_dma_addr;
	uint32_t	pi_local;
	uint32_t	pi_register_offset;
	uint32_t	*pi_register_abs;
	uint32_t	*ci_virt_addr;
	dma_addr_t	ci_dma_addr;
	boolean_t	created;
	boolean_t	lockcreated;
	char		lockname[LOCKNAME_SIZE];
	OS_PQILOCK_T	lock	OS_ATTRIBUTE_ALIGNED(8);
}ib_queue_t;

typedef struct ob_queue {
	uint32_t	q_id;
	uint32_t	num_elem;
	uint32_t	elem_size;
	uint32_t	intr_msg_num;
	char		*array_virt_addr;
	dma_addr_t	array_dma_addr;
	uint32_t	ci_local;
	uint32_t	ci_register_offset;
	uint32_t	*ci_register_abs;
	uint32_t	*pi_virt_addr;
	dma_addr_t	pi_dma_addr;
	boolean_t	created;
}ob_queue_t;

typedef struct pqisrc_sg_desc{
	uint64_t	addr;
	uint32_t	len;
	uint32_t	flags;
}sgt_t;


typedef struct pqi_iu_layer_desc {
	uint8_t		ib_spanning_supported : 1;
	uint8_t		res1 : 7;
	uint8_t		res2[5];
	uint16_t	max_ib_iu_len;
	uint8_t		ob_spanning_supported : 1;
	uint8_t		res3 : 7;
	uint8_t		res4[5];
	uint16_t	max_ob_iu_len;
}OS_ATTRIBUTE_PACKED pqi_iu_layer_desc_t;


/* Response IU data */
typedef struct pqi_device_capabilities {
	uint16_t	length;
	uint8_t		res1[6];
	uint8_t		ibq_arb_priority_support_bitmask;
	uint8_t		max_aw_a;
	uint8_t		max_aw_b;
	uint8_t		max_aw_c;
	uint8_t		max_arb_burst : 3;
	uint8_t		res2 : 4;
	uint8_t		iqa : 1;
	uint8_t		res3[2];
	uint8_t		iq_freeze : 1;
	uint8_t		res4 : 7;
	uint16_t	max_iqs;
	uint16_t	max_iq_elements;
	uint8_t		res5[4];
	uint16_t	max_iq_elem_len;
	uint16_t	min_iq_elem_len;
	uint8_t		res6[2];
	uint16_t	max_oqs;
	uint16_t	max_oq_elements;
	uint16_t	intr_coales_time_granularity;
	uint16_t	max_oq_elem_len;
	uint16_t	min_oq_elem_len;
	uint8_t		res7[24];
	pqi_iu_layer_desc_t iu_layer_desc[32];
}OS_ATTRIBUTE_PACKED pqi_dev_cap_t;

/* IO path */

typedef struct pqi_aio_req {
	iu_header_t	header;
	uint16_t	response_queue_id;
	uint8_t		work_area[2];
	uint16_t	req_id;
	uint8_t		res1[2];
	uint32_t	nexus;
	uint32_t	buf_len;
	uint8_t		data_dir : 2;
	uint8_t		partial : 1;
	uint8_t		mem_type : 1;
	uint8_t		fence : 1;
	uint8_t		encrypt_enable : 1;
	uint8_t		res2 : 2;
	uint8_t		task_attr : 3;
	uint8_t		cmd_prio : 4;
	uint8_t		res3 : 1;
	uint16_t	encrypt_key_index;
	uint32_t	encrypt_twk_low;
	uint32_t	encrypt_twk_high;
	uint8_t		cdb[16];
	uint16_t	err_idx;
	uint8_t		num_sg;
	uint8_t		cdb_len;
	uint8_t		lun[8];
	uint8_t		res4[4];
	sgt_t		sg_desc[4];
}OS_ATTRIBUTE_PACKED pqi_aio_req_t;


typedef struct pqisrc_raid_request {
	iu_header_t 	header;
	uint16_t 	response_queue_id;	/* specifies the OQ where the response
					   IU is to be delivered */
	uint8_t		work_area[2];	/* reserved for driver use */
	uint16_t 	request_id;
	uint16_t 	nexus_id;
	uint32_t 	buffer_length;
	uint8_t		lun_number[8];
	uint16_t 	protocol_spec;
	uint8_t		data_direction : 2;
	uint8_t		partial : 1;
	uint8_t		reserved1 : 4;
	uint8_t		fence : 1;
	uint16_t 	error_index;
	uint8_t		reserved2;
	uint8_t		task_attribute : 3;
	uint8_t		command_priority : 4;
	uint8_t		reserved3 : 1;
	uint8_t		reserved4 : 2;
	uint8_t		additional_cdb_bytes_usage : 3;
	uint8_t		reserved5 : 3;
	uint8_t		cdb[16];
	uint8_t		additional_cdb_bytes[16];
	sgt_t		sg_descriptors[4];
}OS_ATTRIBUTE_PACKED pqisrc_raid_req_t;


typedef struct pqi_tmf_req {
        iu_header_t     header;
        uint16_t        resp_qid;
        uint8_t         work_area[2];
        uint16_t        req_id;
        uint16_t        nexus;
        uint8_t         res1[4];
        uint8_t         lun[8];
        uint16_t        protocol_spec;
        uint16_t        obq_id_to_manage;
        uint16_t        req_id_to_manage;
        uint8_t         tmf;
        uint8_t         res2 : 7;
        uint8_t         fence : 1;
}OS_ATTRIBUTE_PACKED pqi_tmf_req_t;


typedef struct pqi_tmf_resp {
        iu_header_t     header;
        uint16_t        resp_qid;
        uint8_t         work_area[2];
        uint16_t        req_id;
        uint16_t        nexus;
        uint8_t         add_resp_info[3];
        uint8_t         resp_code;
}pqi_tmf_resp_t;


struct pqi_io_response {
	iu_header_t	header;
	uint16_t	queue_id;
	uint8_t		work_area[2];	
	uint16_t	request_id;
	uint16_t	error_index;
	uint8_t		reserved[4];
}OS_ATTRIBUTE_PACKED;


struct pqi_enc_info {
	uint16_t	data_enc_key_index;
	uint32_t	encrypt_tweak_lower;
	uint32_t	encrypt_tweak_upper;
};


typedef struct pqi_scsi_device {
	device_type_t	devtype;		/* as reported by INQUIRY commmand */
	uint8_t		device_type;		/* as reported by
					   BMIC_IDENTIFY_PHYSICAL_DEVICE - only
					   valid for devtype = TYPE_DISK */
	int		bus;
	int		target;
	int		lun;
	uint8_t 	flags;
	uint8_t		scsi3addr[8];
	uint64_t	wwid;
	uint8_t		is_physical_device : 1;
	uint8_t		is_external_raid_device : 1;
	uint8_t 	target_lun_valid : 1;
	uint8_t		expose_device : 1;
	uint8_t		no_uld_attach : 1;
	uint8_t		is_obdr_device : 1;
	uint8_t 	aio_enabled : 1;
	uint8_t		device_gone : 1;
	uint8_t		new_device : 1;
	uint8_t		volume_offline : 1;
	uint8_t		vendor[8];		/* bytes 8-15 of inquiry data */
	uint8_t		model[16];		/* bytes 16-31 of inquiry data */
	uint64_t	sas_address;
	uint8_t		raid_level;
	uint16_t	queue_depth;		/* max. queue_depth for this device */
	uint16_t	advertised_queue_depth;
	uint32_t	ioaccel_handle;
	uint8_t		volume_status;
	uint8_t		active_path_index;
	uint8_t		path_map;
	uint8_t		bay;
	uint8_t		box[8];
	uint16_t	phys_connector[8];
	int		offload_config;		/* I/O accel RAID offload configured */
	int		offload_enabled;	/* I/O accel RAID offload enabled */
	int		offload_enabled_pending;
	int		offload_to_mirror;	/* Send next I/O accelerator RAID
						   offload request to mirror drive. */
	struct raid_map *raid_map;	/* I/O accelerator RAID map */
	int 		reset_in_progress;
	os_dev_info_t	*dip;			/*os specific scsi device information*/
	boolean_t	invalid;		
}pqi_scsi_dev_t;


struct sense_header_scsi {		/* See SPC-3 section 4.5 */
	uint8_t 	response_code;		/* permit: 0x0, 0x70, 0x71, 0x72, 0x73 */
	uint8_t 	sense_key;
	uint8_t 	asc;
	uint8_t 	ascq;
	uint8_t 	byte4;
	uint8_t 	byte5;
	uint8_t 	byte6;
	uint8_t 	additional_length;	/* always 0 for fixed sense format */
}OS_ATTRIBUTE_PACKED;



typedef struct report_lun_header {
	uint32_t 	list_length;
	uint8_t		extended_response;
	uint8_t		reserved[3];
}OS_ATTRIBUTE_PACKED reportlun_header_t;


typedef struct report_lun_ext_entry {
	uint8_t		lunid[8];
	uint64_t 	wwid;
	uint8_t		device_type;
	uint8_t		device_flags;
	uint8_t		lun_count;	/* number of LUNs in a multi-LUN device */
	uint8_t		redundant_paths;
	uint32_t 	ioaccel_handle;
}OS_ATTRIBUTE_PACKED reportlun_ext_entry_t;


typedef struct report_lun_data_ext {
	reportlun_header_t 	header;
	reportlun_ext_entry_t 	lun_entries[1];
}OS_ATTRIBUTE_PACKED reportlun_data_ext_t;

typedef struct raidmap_data {
	uint32_t 	ioaccel_handle;
	uint8_t		xor_mult[2];
	uint8_t		reserved[2];
}OS_ATTRIBUTE_PACKED raidmap_data_t;

typedef struct raid_map {
	uint32_t	structure_size;		/* size of entire structure in bytes */
	uint32_t	volume_blk_size;	/* bytes / block in the volume */
	uint64_t	volume_blk_cnt;		/* logical blocks on the volume */
	uint8_t		phys_blk_shift;		/* shift factor to convert between
					   units of logical blocks and physical
					   disk blocks */
	uint8_t		parity_rotation_shift;	/* shift factor to convert between units
					   of logical stripes and physical
					   stripes */
	uint16_t	strip_size;		/* blocks used on each disk / stripe */
	uint64_t	disk_starting_blk;	/* first disk block used in volume */
	uint64_t	disk_blk_cnt;		/* disk blocks used by volume / disk */
	uint16_t	data_disks_per_row;	/* data disk entries / row in the map */
	uint16_t	metadata_disks_per_row;	/* mirror/parity disk entries / row
					   in the map */
	uint16_t	row_cnt;		/* rows in each layout map */
	uint16_t	layout_map_count;	/* layout maps (1 map per mirror/parity
					   group) */
	uint16_t	flags;
	uint16_t	data_encryption_key_index;
	uint8_t		reserved[16];
	raidmap_data_t 	dev_data[RAID_MAP_MAX_ENTRIES];
}OS_ATTRIBUTE_PACKED pqisrc_raid_map_t;


typedef struct bmic_ident_ctrl {
	uint8_t		conf_ld_count;
	uint32_t	conf_sign;
	uint8_t		fw_version[4];
	uint8_t		rom_fw_rev[4];
	uint8_t		hw_rev;
	uint8_t		reserved[140];
	uint16_t	extended_lun_count;
	uint8_t		reserved1[34];
	uint16_t	fw_build_number;
	uint8_t		reserved2[100];
	uint8_t		ctrl_mode;
	uint8_t		reserved3[32];
}OS_ATTRIBUTE_PACKED bmic_ident_ctrl_t;

typedef struct bmic_identify_physical_device {
	uint8_t		scsi_bus;		/* SCSI Bus number on controller */
	uint8_t		scsi_id;		/* SCSI ID on this bus */
	uint16_t	block_size;		/* sector size in bytes */
	uint32_t	total_blocks;		/* number for sectors on drive */
	uint32_t	reserved_blocks;	/* controller reserved (RIS) */
	uint8_t		model[40];		/* Physical Drive Model */
	uint8_t		serial_number[40];	/* Drive Serial Number */
	uint8_t		firmware_revision[8];	/* drive firmware revision */
	uint8_t		scsi_inquiry_bits;	/* inquiry byte 7 bits */
	uint8_t		compaq_drive_stamp;	/* 0 means drive not stamped */
	uint8_t		last_failure_reason;
	uint8_t		flags;
	uint8_t		more_flags;
	uint8_t		scsi_lun;		/* SCSI LUN for phys drive */
	uint8_t		yet_more_flags;
	uint8_t		even_more_flags;
	uint32_t	spi_speed_rules;
	uint8_t		phys_connector[2];	/* connector number on controller */
	uint8_t		phys_box_on_bus;	/* phys enclosure this drive resides */
	uint8_t		phys_bay_in_box;	/* phys drv bay this drive resides */
	uint32_t	rpm;			/* drive rotational speed in RPM */
	uint8_t		device_type;		/* type of drive */
	uint8_t		sata_version;		/* only valid when device_type =
					   BMIC_DEVICE_TYPE_SATA */
	uint64_t	big_total_block_count;
	uint64_t	ris_starting_lba;
	uint32_t	ris_size;
	uint8_t		wwid[20];
	uint8_t		controller_phy_map[32];
	uint16_t	phy_count;
	uint8_t		phy_connected_dev_type[256];
	uint8_t		phy_to_drive_bay_num[256];
	uint16_t	phy_to_attached_dev_index[256];
	uint8_t		box_index;
	uint8_t		reserved;
	uint16_t	extra_physical_drive_flags;
	uint8_t		negotiated_link_rate[256];
	uint8_t		phy_to_phy_map[256];
	uint8_t		redundant_path_present_map;
	uint8_t		redundant_path_failure_map;
	uint8_t		active_path_number;
	uint16_t	alternate_paths_phys_connector[8];
	uint8_t		alternate_paths_phys_box_on_port[8];
	uint8_t		multi_lun_device_lun_count;
	uint8_t		minimum_good_fw_revision[8];
	uint8_t		unique_inquiry_bytes[20];
	uint8_t		current_temperature_degreesC;
	uint8_t		temperature_threshold_degreesC;
	uint8_t		max_temperature_degreesC;
	uint8_t		logical_blocks_per_phys_block_exp;
	uint16_t	current_queue_depth_limit;
	uint8_t		switch_name[10];
	uint16_t	switch_port;
	uint8_t		alternate_paths_switch_name[40];
	uint8_t		alternate_paths_switch_port[8];
	uint16_t	power_on_hours;
	uint16_t	percent_endurance_used;
	uint8_t		drive_authentication;
	uint8_t		smart_carrier_authentication;
	uint8_t		smart_carrier_app_fw_version;
	uint8_t		smart_carrier_bootloader_fw_version;
	uint8_t		encryption_key_name[64];
	uint32_t	misc_drive_flags;
	uint16_t	dek_index;
	uint8_t		padding[112];
}OS_ATTRIBUTE_PACKED bmic_ident_physdev_t;

typedef struct pqisrc_bmic_flush_cache {
	uint8_t		disable_cache;
	uint8_t		power_action;
	uint8_t		ndu_flush_cache;
	uint8_t		halt_event;
	uint8_t		reserved[28];
} OS_ATTRIBUTE_PACKED pqisrc_bmic_flush_cache_t;

/* for halt_event member of pqisrc_bmic_flush_cache_t */
enum pqisrc_flush_cache_event_type {
	PQISRC_NONE_CACHE_FLUSH_ONLY = 0,
	PQISRC_SHUTDOWN = 1,
	PQISRC_HIBERNATE = 2,
	PQISRC_SUSPEND = 3,
	PQISRC_RESTART = 4
};

struct pqisrc_softstate;
struct request_container_block;
typedef void (*success_callback)(struct pqisrc_softstate *, struct request_container_block *);
typedef void (*error_callback)(struct pqisrc_softstate *, struct request_container_block *, uint16_t);

/* Request container block */
typedef struct request_container_block {
	void			*req;
	void			*error_info;
	REQUEST_STATUS_T	status;
	uint32_t		tag;
	sgt_t			*sg_chain_virt;
	dma_addr_t		sg_chain_dma;
	uint32_t		data_dir;
	pqi_scsi_dev_t		*dvp;
	struct pqisrc_softstate	*softs;
	success_callback	success_cmp_callback;
	error_callback		error_cmp_callback;
	uint8_t			*cdbp;
	int			cmdlen;
	uint32_t		bcount;	/* buffer size in byte */
	uint32_t		ioaccel_handle;
	boolean_t 		encrypt_enable;
	struct pqi_enc_info 	enc_info;
	int			cm_flags;
	void			*cm_data; /* pointer to data in kernel space */
	bus_dmamap_t		cm_datamap;
	uint32_t		nseg;
	union ccb		*cm_ccb;
	sgt_t			*sgt;	/* sg table */
	int 			resp_qid;
	boolean_t		req_pending;
}rcb_t;

typedef struct tid_pool {
	int 			tid[PQI_MAX_PHYSICALS];
	int			index;
}tid_pool_t;

typedef struct pqisrc_softstate {
	OS_SPECIFIC_T			os_specific;  
	struct ioa_registers		*ioa_reg; 
	struct pqi_registers		*pqi_reg;	
	char				*pci_mem_base_vaddr;
	PCI_ACC_HANDLE_T		pci_mem_handle; 
	struct pqi_cap			pqi_cap;
	struct pqi_pref_settings	pref_settings;
	char				fw_version[11];
	uint16_t			fw_build_number;
	uint32_t			card;		/* index to aac_cards */
	uint16_t			vendid;		/* vendor id */
	uint16_t			subvendid;	/* sub vendor id */
	uint16_t			devid;		/* device id */
	uint16_t			subsysid;	/* sub system id */
	controller_state_t		ctlr_state;		
	struct dma_mem			err_buf_dma_mem;
	struct dma_mem			admin_queue_dma_mem;
	struct dma_mem			op_ibq_dma_mem;
	struct dma_mem			op_obq_dma_mem;
	struct dma_mem			event_q_dma_mem;
	struct dma_mem			sg_dma_desc[PQISRC_MAX_OUTSTANDING_REQ];
	ib_queue_t			admin_ib_queue;
	ob_queue_t			admin_ob_queue;
	ob_queue_t			event_q;
	ob_queue_t			op_ob_q[PQISRC_MAX_SUPPORTED_OP_OB_Q - 1];/* 1 event queue */
	ib_queue_t			op_raid_ib_q[PQISRC_MAX_SUPPORTED_OP_RAID_IB_Q];
	ib_queue_t			op_aio_ib_q[PQISRC_MAX_SUPPORTED_OP_AIO_IB_Q];
	uint32_t			max_outstanding_io;
	uint32_t			max_io_for_scsi_ml;
	uint32_t			num_op_raid_ibq;
	uint32_t			num_op_aio_ibq;
	uint32_t			num_op_obq;
	uint32_t			num_elem_per_op_ibq;
	uint32_t			num_elem_per_op_obq;
	uint32_t			ibq_elem_size;
	uint32_t			obq_elem_size;
	pqi_dev_cap_t			pqi_dev_cap;
	uint16_t			max_ib_iu_length_per_fw;
	uint16_t			max_ib_iu_length;
	unsigned			max_sg_per_iu;
	uint8_t				ib_spanning_supported : 1;
	uint8_t				ob_spanning_supported : 1;
	pqi_event_config_t		event_config; 
	struct pqi_event		pending_events[PQI_NUM_SUPPORTED_EVENTS];
	int				intr_type;
	int				intr_count;
	int				num_cpus_online;
	boolean_t			share_opq_and_eventq;
	rcb_t				*rcb;
#ifndef LOCKFREE_STACK
	pqi_taglist_t			taglist;
#else
	lockless_stack_t		taglist;
#endif /* LOCKFREE_STACK */
	boolean_t			devlist_lockcreated;    
	OS_LOCK_T			devlist_lock	OS_ATTRIBUTE_ALIGNED(8);
	char				devlist_lock_name[LOCKNAME_SIZE];
	pqi_scsi_dev_t			*device_list[PQI_MAX_DEVICES][PQI_MAX_MULTILUN];
	OS_SEMA_LOCK_T			scan_lock;
	uint8_t				lun_count[PQI_MAX_DEVICES];
	uint64_t			target_sas_addr[PQI_MAX_EXT_TARGETS];
	OS_ATOMIC64_T			num_intrs;
	uint64_t			prev_num_intrs;
	uint64_t			prev_heartbeat_count;
	uint64_t			*heartbeat_counter_abs_addr;
	uint64_t			heartbeat_counter_off;
	uint64_t			num_heartbeats_requested;
	uint32_t			bus_id;
	uint32_t			device_id;
	uint32_t			func_id;
	char 				*os_name;
	boolean_t			ctrl_online;
	uint8_t				pqi_reset_quiesce_allowed : 1;
	boolean_t 			ctrl_in_pqi_mode;
	tid_pool_t			tid_pool;	
}pqisrc_softstate_t;

#endif
