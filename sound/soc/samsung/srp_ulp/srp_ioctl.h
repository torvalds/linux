#ifndef __SRP_IOCTL_H__
#define __SRP_IOCTL_H__

#ifdef __cplusplus
extern "C" {
#endif

#define SRP_INIT				(0x10000)
#define SRP_DEINIT				(0x10001)

#define SRP_PAUSE				(0x20000)
#define SRP_STOP				(0x20001)
#define SRP_FLUSH				(0x20002)
#define SRP_WAIT_EOS				(0x20003)
#define SRP_EFFECT				(0x20004)
#define SRP_SEND_EOS				(0x20005)
#define SRP_RESUME_EOS			(0x20006)

#define SRP_PENDING_STATE			(0x30000)
#define SRP_ERROR_STATE			(0x30001)
#define SRP_DECODED_FRAME_NO			(0x30002)
#define SRP_DECODED_ONE_FRAME_SIZE		(0x30003)
#define SRP_DECODED_FRAME_SIZE		(0x30004)
#define SRP_DECODED_PCM_SIZE			(0x30005)
#define SRP_CHANNEL_COUNT			(0x30006)
#define SRP_STOP_EOS_STATE			(0x30007)

#define SRP_CTRL_SET_GAIN			(0xFF000)
#define SRP_CTRL_SET_EFFECT			(0xFF001)
#define SRP_CTRL_GET_PCM_1KFRAME		(0xFF002)
#define SRP_CTRL_PCM_DUMP_OP			(0xFF003)
#define SRP_CTRL_SET_GAIN_SUB_LR		(0xFF004)
#define SRP_CTRL_FORCE_MONO			(0xFF005)
#define SRP_CTRL_AMFILTER_LOAD		(0xFF006)
#define SRP_CTRL_SB_TABLET			(0xFF007)

#define SRP_CTRL_EFFECT_ENABLE		(0xFF010)
#define SRP_CTRL_EFFECT_DEF			(0xFF011)
#define SRP_CTRL_EFFECT_EQ_USR		(0xFF012)
#define SRP_CTRL_EFFECT_SPEAKER		(0xFF013)

#define SRP_CTRL_IS_RUNNING			(0xFF100)
#define SRP_CTRL_IS_OPENED			(0xFF101)
#define SRP_CTRL_GET_OP_LEVEL		(0xFF102)
#define SRP_CTRL_IS_PCM_DUMP			(0xFF103)
#define SRP_CTRL_IS_FORCE_MONO		(0xFF104)

#define SRP_CTRL_ALTFW_STATE			(0xFF200)
#define SRP_CTRL_ALTFW_LOAD			(0xFF201)

#define SRP_FW_CODE1				0
#define SRP_FW_CODE20			1
#define SRP_FW_CODE21			2
#define SRP_FW_CODE22			3
#define SRP_FW_CODE30			4
#define SRP_FW_CODE31			5

#define SRP_FW_VLIW				0
#define SRP_FW_CGA				1
#define SRP_FW_CGA_SA			2
#define SRP_FW_DATA				3

#ifdef __cplusplus
}
#endif
#endif /* __SRP_IOCTL_H__ */
