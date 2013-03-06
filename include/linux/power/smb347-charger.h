#ifndef SMB347_CHARGER_H
#define SMB347_CHARGER_H

/*
 * @chg_en_pin: charge enable pin (smb347's c4 pin)
 * @chg_ctl_pin: charge control pin (smb347's d2 pin)
 * @chg_stat_pin: charge stat pin (smb347's f5 pin)
 * @chg_susp_pin: charge usb suspend pin (smb347's d3 pin)
 * @max_current: dc and hc input current limit 
 *               can set 300ma/500ma/700ma/900ma/1200ma
 *               or 1500ma/1800ma/2000ma/2200ma/2500ma
 * @otg_power_form_smb: if otg 5v power form smb347 set 1 otherwise set 0
 */
struct smb347_info{
	unsigned int chg_en_pin;        
	unsigned int chg_ctl_pin;       
	unsigned int chg_stat_pin;	
	unsigned int chg_susp_pin;
        unsigned int max_current;       
        bool    otg_power_form_smb;
};

extern int smb347_is_chg_ok(void);
extern int smb347_is_charging(void);

#endif
