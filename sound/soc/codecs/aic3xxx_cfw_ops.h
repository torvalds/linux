/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AIC3XXX_CFW_OPS_H_
#define AIC3XXX_CFW_OPS_H_

#ifdef AIC3XXX_CFW_HOST_BLD
void *aic3xxx_cfw_init(void *pcfw, int n);
void *aic3xxx_cfw_getpjt(void *ps);
cfw_project *aic3xxx_cfw_unpickle(void *,int);
unsigned int crc32(unsigned int *pdata, int n);
#endif

int aic3xxx_cfw_reload(void *pv, void *pcfw, int n);
int aic3xxx_cfw_setmode(void *pv, int mode);
int aic3xxx_cfw_setmode_cfg(void *pv, int mode, int cfg);
int aic3xxx_cfw_setcfg(void *pv, int cfg);
int aic3xxx_cfw_transition(void *pv, char *ttype);
int aic3xxx_cfw_set_pll(void *pv, int asi);


#define AIC3XX_COPS_MDSP_D  (0x00000003u)
#define AIC3XX_COPS_MDSP_A  (0x00000030u)
#define AIC3XX_COPS_MDSP_ALL (AIC3XX_COPS_MDSP_D|AIC3XX_COPS_MDSP_A)

#define AIC3XX_ABUF_MDSP_D1 (0x00000001u)
#define AIC3XX_ABUF_MDSP_D2 (0x00000002u)
#define AIC3XX_ABUF_MDSP_A  (0x00000010u)
#define AIC3XX_ABUF_MDSP_ALL \
    (AIC3XX_ABUF_MDSP_D1| AIC3XX_ABUF_MDSP_D2| AIC3XX_ABUF_MDSP_A)

typedef struct aic3xxx_codec_ops {
    int (*reg_read)(void *p, unsigned int reg);
    int (*reg_write)(void *p, unsigned int reg,
            unsigned char val);
    int (*set_bits)(void *p, unsigned int reg, 
            unsigned char mask, unsigned char val);
    int (*bulk_read)(void *p, unsigned int reg, 
            int count, u8 *buf);
    int (*bulk_write)(void *p, unsigned int reg, 
            int count, const u8 *buf);

    int (*lock)(void *p);
    int (*unlock)(void *p);
    int (*stop)(void *p, int mask);
    int (*restore)(void *p, int runstate);
    int (*bswap)(void *p,int mask);
} aic3xxx_codec_ops;

typedef struct cfw_state {
    cfw_project *pjt;
    const aic3xxx_codec_ops *ops;
    void *ops_obj;
    int cur_mode;
    int cur_pfw;
    int cur_ovly;
    int cur_cfg;
} cfw_state;

#ifndef AIC3XXX_CFW_HOST_BLD
#ifdef DEBUG

#   define DBG(fmt,...) printk("CFW[%s:%d]: " fmt "\n",      \
                        __FILE__, __LINE__, ##__VA_ARGS__)
#else

#   define DBG(fmt,...) 

#endif

#endif
#endif
