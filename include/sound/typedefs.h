/*
 * Typedef's for backward compatibility (for out-of-kernel drivers)
 *
 * This file will be removed soon in future
 */

/* core stuff */
typedef struct snd_card snd_card_t;
typedef struct snd_device snd_device_t;
typedef struct snd_device_ops snd_device_ops_t;
typedef enum snd_card_type snd_card_type_t;
typedef struct snd_minor snd_minor_t;

/* info */
typedef struct snd_info_entry snd_info_entry_t;
typedef struct snd_info_buffer snd_info_buffer_t;

/* control */
typedef struct snd_ctl_file snd_ctl_file_t;
typedef struct snd_kcontrol snd_kcontrol_t;
typedef struct snd_kcontrol_new snd_kcontrol_new_t;
typedef struct snd_kcontrol_volatile snd_kcontrol_volatile_t;
typedef struct snd_kctl_event snd_kctl_event_t;
typedef struct snd_aes_iec958 snd_aes_iec958_t;
typedef struct snd_ctl_card_info snd_ctl_card_info_t;
typedef struct snd_ctl_elem_id snd_ctl_elem_id_t;
typedef struct snd_ctl_elem_list snd_ctl_elem_list_t;
typedef struct snd_ctl_elem_info snd_ctl_elem_info_t;
typedef struct snd_ctl_elem_value snd_ctl_elem_value_t;
typedef struct snd_ctl_event snd_ctl_event_t;
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
typedef struct snd_mixer_oss snd_mixer_oss_t;
#endif

/* timer */
typedef struct snd_timer snd_timer_t;
typedef struct snd_timer_instance snd_timer_instance_t;
typedef struct snd_timer_id snd_timer_id_t;
typedef struct snd_timer_ginfo snd_timer_ginfo_t;
typedef struct snd_timer_gparams snd_timer_gparams_t;
typedef struct snd_timer_gstatus snd_timer_gstatus_t;
typedef struct snd_timer_select snd_timer_select_t;
typedef struct snd_timer_info snd_timer_info_t;
typedef struct snd_timer_params snd_timer_params_t;
typedef struct snd_timer_status snd_timer_status_t;
typedef struct snd_timer_read snd_timer_read_t;
typedef struct snd_timer_tread snd_timer_tread_t;

/* PCM */
typedef struct snd_pcm snd_pcm_t;
typedef struct snd_pcm_str snd_pcm_str_t;
typedef struct snd_pcm_substream snd_pcm_substream_t;
typedef struct snd_pcm_info snd_pcm_info_t;
typedef struct snd_pcm_hw_params snd_pcm_hw_params_t;
typedef struct snd_pcm_sw_params snd_pcm_sw_params_t;
typedef struct snd_pcm_channel_info snd_pcm_channel_info_t;
typedef struct snd_pcm_status snd_pcm_status_t;
typedef struct snd_pcm_mmap_status snd_pcm_mmap_status_t;
typedef struct snd_pcm_mmap_control snd_pcm_mmap_control_t;
typedef struct snd_mask snd_mask_t;
typedef struct snd_sg_buf snd_pcm_sgbuf_t;

typedef struct snd_interval snd_interval_t;
typedef struct snd_xferi snd_xferi_t;
typedef struct snd_xfern snd_xfern_t;
typedef struct snd_xferv snd_xferv_t;

typedef struct snd_pcm_file snd_pcm_file_t;
typedef struct snd_pcm_runtime snd_pcm_runtime_t;
typedef struct snd_pcm_hardware snd_pcm_hardware_t;
typedef struct snd_pcm_ops snd_pcm_ops_t;
typedef struct snd_pcm_hw_rule snd_pcm_hw_rule_t;
typedef struct snd_pcm_hw_constraints snd_pcm_hw_constraints_t;
typedef struct snd_ratnum ratnum_t;
typedef struct snd_ratden ratden_t;
typedef struct snd_pcm_hw_constraint_ratnums snd_pcm_hw_constraint_ratnums_t;
typedef struct snd_pcm_hw_constraint_ratdens snd_pcm_hw_constraint_ratdens_t;
typedef struct snd_pcm_hw_constraint_list snd_pcm_hw_constraint_list_t;
typedef struct snd_pcm_group snd_pcm_group_t;
typedef struct snd_pcm_notify snd_pcm_notify_t;

/* rawmidi */
typedef struct snd_rawmidi snd_rawmidi_t;
typedef struct snd_rawmidi_info snd_rawmidi_info_t;
typedef struct snd_rawmidi_params snd_rawmidi_params_t;
typedef struct snd_rawmidi_status snd_rawmidi_status_t;
typedef struct snd_rawmidi_runtime snd_rawmidi_runtime_t;
typedef struct snd_rawmidi_substream snd_rawmidi_substream_t;
typedef struct snd_rawmidi_str snd_rawmidi_str_t;
typedef struct snd_rawmidi_ops snd_rawmidi_ops_t;
typedef struct snd_rawmidi_global_ops snd_rawmidi_global_ops_t;
typedef struct snd_rawmidi_file snd_rawmidi_file_t;

/* hwdep */
typedef struct snd_hwdep snd_hwdep_t;
typedef struct snd_hwdep_info snd_hwdep_info_t;
typedef struct snd_hwdep_dsp_status snd_hwdep_dsp_status_t;
typedef struct snd_hwdep_dsp_image snd_hwdep_dsp_image_t;
typedef struct snd_hwdep_ops snd_hwdep_ops_t;

/* sequencer */
typedef struct snd_seq_port_info snd_seq_port_info_t;
typedef struct snd_seq_port_subscribe snd_seq_port_subscribe_t;
typedef struct snd_seq_event snd_seq_event_t;
typedef struct snd_seq_addr snd_seq_addr_t;
typedef struct snd_seq_ev_volume snd_seq_ev_volume_t;
typedef struct snd_seq_ev_loop snd_seq_ev_loop_t;
typedef struct snd_seq_remove_events snd_seq_remove_events_t;
typedef struct snd_seq_query_subs snd_seq_query_subs_t;
typedef struct snd_seq_system_info snd_seq_system_info_t;
typedef struct snd_seq_client_info snd_seq_client_info_t;
typedef struct snd_seq_queue_info snd_seq_queue_info_t;
typedef struct snd_seq_queue_status snd_seq_queue_status_t;
typedef struct snd_seq_queue_tempo snd_seq_queue_tempo_t;
typedef struct snd_seq_queue_owner snd_seq_queue_owner_t;
typedef struct snd_seq_queue_timer snd_seq_queue_timer_t;
typedef struct snd_seq_queue_client snd_seq_queue_client_t;
typedef struct snd_seq_client_pool snd_seq_client_pool_t;
typedef struct snd_seq_instr snd_seq_instr_t;
typedef struct snd_seq_instr_data snd_seq_instr_data_t;
typedef struct snd_seq_instr_header snd_seq_instr_header_t;

typedef struct snd_seq_user_client user_client_t;
typedef struct snd_seq_kernel_client kernel_client_t;
typedef struct snd_seq_client client_t;
typedef struct snd_seq_queue queue_t;

/* seq_device */
typedef struct snd_seq_device snd_seq_device_t;
typedef struct snd_seq_dev_ops snd_seq_dev_ops_t;

/* seq_midi */
typedef struct snd_midi_event snd_midi_event_t;

/* seq_midi_emul */
typedef struct snd_midi_channel snd_midi_channel_t;
typedef struct snd_midi_channel_set snd_midi_channel_set_t;
typedef struct snd_midi_op snd_midi_op_t;

/* seq_oss */
typedef struct snd_seq_oss_arg snd_seq_oss_arg_t;
typedef struct snd_seq_oss_callback snd_seq_oss_callback_t;
typedef struct snd_seq_oss_reg snd_seq_oss_reg_t;

/* virmidi */
typedef struct snd_virmidi_dev snd_virmidi_dev_t;
typedef struct snd_virmidi snd_virmidi_t;

/* seq_instr */
typedef struct snd_seq_kcluster snd_seq_kcluster_t;
typedef struct snd_seq_kinstr_ops snd_seq_kinstr_ops_t;
typedef struct snd_seq_kinstr snd_seq_kinstr_t;
typedef struct snd_seq_kinstr_list snd_seq_kinstr_list_t;

/* ac97 */
typedef struct snd_ac97_bus ac97_bus_t;
typedef struct snd_ac97_bus_ops ac97_bus_ops_t;
typedef struct snd_ac97_template ac97_template_t;
typedef struct snd_ac97 ac97_t;

/* opl3/4 */
typedef struct snd_opl3 opl3_t;
typedef struct snd_opl4 opl4_t;

/* mpu401 */
typedef struct snd_mpu401 mpu401_t;

/* i2c */
typedef struct snd_i2c_device snd_i2c_device_t;
typedef struct snd_i2c_bus snd_i2c_bus_t;

typedef struct snd_ak4531 ak4531_t;

