#ifndef _AML_GSL_COMMON_H_
#define _AML_GSL_COMMON_H_
#include "linux/amlogic/input/common.h"
struct fw_data
{
    u32 offset : 8;
    u32 : 0;
    u32 val;
};

struct gsl_touch_info
{
	int x[10];
	int y[10];
	int id[10];
	int finger_num;	
};

struct aml_gsl_api {
	unsigned int (*gsl_mask_tiaoping) (void);
	unsigned int (*gsl_version_id) (void);
	void (*gsl_alg_id_main) (struct gsl_touch_info *cinfo);
	void (*gsl_DataInit) (int *ret);
};

extern void aml_gsl_clear_api(void);
extern int aml_gsl_register_api(struct aml_gsl_api *api);
extern struct aml_gsl_api* aml_gsl_get_api(void);
#endif