#ifndef __AXP_ALGORITHM_H__
#define __AXP_ALGORITHM_H__

struct axp_charger;
struct battery_parameter;
extern int  report_delay;

extern void axp_update_coulomb(struct axp_charger *charger, struct battery_parameter *axp_pmu_battery);
extern void axp_update_ocv_vol(struct axp_charger *charger);
extern void axp_update_battery_capacity(struct axp_charger *charger, struct battery_parameter *axp_pmu_battery);
extern void axp_post_update_process(struct axp_charger * charger);
extern void axp_battery_probe_process(struct axp_charger *charger, struct battery_parameter *axp_pmu_battery);
extern void axp_battery_suspend_process(void);
extern void axp_battery_resume_process(struct axp_charger *charger, struct battery_parameter *axp_pmu_battery, int ocv_resume);
extern ssize_t axp_format_dbg_buffer(struct axp_charger *charger, char *buf);

#endif /* __AXP_ALGORITHM_H__ */
