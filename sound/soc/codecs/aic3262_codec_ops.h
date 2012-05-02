#define SYNC_STATE(p) aic3262_reg_read(p->codec->control_data,AIC3262_DAC_PRB)

#define AIC3262_COPS_MDSP_A	0x30
#define AIC3262_COPS_MDSP_A_L	0x20
#define AIC3262_COPS_MDSP_A_R	0x10



#define AIC3262_COPS_MDSP_D	0x03
#define AIC3262_COPS_MDSP_D_L	0x02
#define AIC3262_COPS_MDSP_D_R	0x01


int get_runstate(void *);

int aic3262_dsp_pwrup(void *,int);

int aic3262_pwr_down(void *,int ,int ,int ,int);

int aic3262_dsp_pwrdwn_status(void *);

int aic3262_ops_reg_read(void *p,unsigned int reg);

int aic3262_ops_reg_write(void  *p,unsigned int reg,unsigned char mval);

int aic3262_ops_set_bits(void *p,unsigned int reg,unsigned char mask, unsigned char val);

int aic3262_ops_bulk_read(void *p,unsigned int reg,int count, u8 *buf);

int aic3262_ops_bulk_write(void *p, unsigned int reg,int count, const u8 *buf);

int aic3262_ops_lock(void *pv);

int aic3262_ops_unlock(void *pv);

int aic3262_ops_stop (void *pv, int mask);

int aic3262_ops_restore(void *pv, int run_state);

int aic3262_ops_adaptivebuffer_swap(void *pv,int mask);

int aic3262_restart_dsps_sync(void *pv,int run_state);
