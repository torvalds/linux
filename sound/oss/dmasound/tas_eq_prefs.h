#ifndef _TAS_EQ_PREFS_H_
#define _TAS_EQ_PREFS_H_

struct tas_eq_pref_t {
	u_int sample_rate;
	u_int device_id;
	u_int output_id;
	u_int speaker_id;

	struct tas_drce_t *drce;

	u_int filter_count;
	struct tas_biquad_ctrl_t *biquads;
};

#endif /* _TAS_EQ_PREFS_H_ */

/*
 * Local Variables:
 * tab-width: 8
 * indent-tabs-mode: t
 * c-basic-offset: 8
 * End:
 */
