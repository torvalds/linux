/*
 *rk818-battery.h - Battery fuel gauge driver structures
 *
 */

#include <linux/time.h>
/* Gas Gauge Constatnts */
#define TEMP_0C			2732
#define MAX_CAPACITY		0x7fff
#define MAX_SOC			100
#define MAX_PERCENTAGE		100

/* Num, cycles with no Learning, after this many cycles, the gauge
   start adjusting FCC, based on Estimated Cell Degradation */
#define NO_LEARNING_CYCLES	25
/* Size of the OCV Lookup table */
#define OCV_TABLE_SIZE		21
/*
 * OCV Config
 */
struct ocv_config {
/*voltage_diff, current_diff: Maximal allowed deviation of the voltage and the current from
one reading to the next that allows the fuel gauge to apply an OCV correction. The main purpose
 of these thresholds is to filter current and voltage spikes. Recommended value: these value are highly
 depend on the load nature. if the load creates a lot of current spikes .the value may need to be increase*/
	uint8_t voltage_diff;
	uint8_t current_diff;
/* sleep_enter_current: if the current remains under this threshold for [sleep_enter_samples]
    consecutive samples. the gauge enters the SLEEP MODE*/
	uint8_t sleep_enter_current;
/*sleep_enter_samples: the number of samples that satis fy asleep enter or exit condition in order
to actually enter of exit SLEEP mode*/
	uint8_t sleep_enter_samples;
/*sleep_exit_samples: to exit SLEEP mode , average current should pass this threshold first. then 
current should remain above this threshold for [sleep_exit_samples] consecutive samples*/	
	uint8_t sleep_exit_current;
/*sleep_exit_samples: to exit SLEEP mode, average current should pass this threshold first, then current
should remain above this threshold for [sleep_exit_samples] consecutive samples.*/
	uint8_t sleep_exit_samples;
/*relax_period: defines the number of seconds the fuel gauge should spend in the SLEEP mode
before entering the OCV mode, this setting makes the gauge wait for a cell voltage recovery after
a charge or discharge operation*/
	uint16_t relax_period;
/* flat_zone_low : flat_zone_high :if soc falls into the flat zone low% - flat zone high %.the fuel gauge
wait for a cell voltage recovery after a charge or discharge operation.*/
	uint8_t flat_zone_low;
	uint8_t flat_zone_high;
/*FCC leaning is disqualified if the discharge capacity in the OCV mode is greater than this threshold*/
	uint16_t max_ocv_discharge;
/*the 21-point OCV table*/
	uint16_t table[OCV_TABLE_SIZE];
	//uint16_t *table;
};

/* EDV Point */
struct edv_point {
	int16_t voltage;
	uint8_t percent;
};

/* EDV Point tracking data */
struct edv_state {
	int16_t voltage;
	uint8_t percent;
	int16_t min_capacity;
	uint8_t edv_cmp;
};

/* EDV Configuration */
struct edv_config {
/*avieraging: True = evokes averaging on voltage reading to detect an EDV condition.
                     False = no averaging of voltage readings to detect an EDV conditation.*/
	bool averaging;
/*sequential_edv: the sequential_edv setting defines how many times in a row the battery should
pass the EDV threshold to detect an EDV condition. this setting is intended to fiter short voltage spikes 
cause by current spikes*/
	uint8_t sequential_edv;
/*filter_light: difine the calculated EDV voltage recovery IIR filter strength
    light-lsetting : for light load (below Qmax/5)
    heavy setting : for ligh load (above Qmax/5)
    the filter is applied only if the load is greater than Qmax/3
    if average = True. then the Qmax/5 threshold is compared to averge current.
    otherwise it is compared to current.
    Recommended value: 15-255. 255---disabsle the filter    
 */
	uint8_t filter_light;
	uint8_t filter_heavy;
/*overload_current: the current level above which an EDV condition will not be detected and 
capacity not reconciled*/
	int16_t overload_current;

	struct edv_point edv[3]; //xsf
/*edv: the end-of-discharge voltage-to-capactiy correlation points.*/
	//struct edv_point *edv; 
};

/* General Battery Cell Gauging Configuration */
struct cell_config {
	bool cc_polarity;  //´ý¶¨To Be Determined
	bool cc_out;
	/*ocv_below_edv1: if set (True), OCV correction allowed bellow EDV1 point*/
	bool ocv_below_edv1;
	/*cc_voltage: the charge complete voltage threshold(e.g. 4.2v) of the battery.
	charge cannot be considered complete if the battery voltage is below this threshold*/
	int16_t cc_voltage;
	/*cc_current:the charge complete current threshold(e.g. c/20). charge cannot  be considered complete
	when charge current and average current are greater than this threshold*/
	int16_t cc_current;
	/*design_capacity: design capacity of the battery. the battery datasheet should provide this value*/
	uint16_t design_capacity;
	/*design_qmax: the calculated discharge capacity of the OCV discharge curve*/
	int16_t design_qmax;
	/*r_sense: the resistance of the current sence element. the sense resistor needs to be slelected to 
	ensure accurate current measuremen and integration at currents >OFF consumption*/
	uint8_t r_sense;
 	/*qmax_adjust: the value decremented from QMAX every cycle for aging compensation.*/
	uint8_t qmax_adjust;
	/*fcc_adjust: the value decremented from the FCC when no learning happen for 25 cycles in a row*/
	uint8_t fcc_adjust;
	/*max_overcharge: the fuel gauge tracks the capacity that goes into the battery after a termination
	condition is detected. this improve gauging accuracy if the charger's charge termination condition does't
	match to the fuel gauge charge termination condition.*/
	uint16_t max_overcharge;
	/*electronics_load: the current that the system consumes int the OFF mode(MPU low power, screen  OFF)*/
	uint16_t electronics_load;
	/*max_increment: the maximum increment of FCC if the learned capacity is much greater than the exiting 
	FCC. recommentded value 150mAh*/
	int16_t max_increment;
	/*max_decrement: the maximum increment of FCC if the learned capacity is much lower
	than the exiting FCC*/
	int16_t max_decrement;
	/*low_temp: the correlation between voltage and remaining capacity is considered inaccurate below
	this temperature. any leaning will be disqualified, if the battery temperature is below this threshold*/
	uint8_t low_temp;
	/*deep_dsg_voltage:in order to qualify capacity learning on the discharge, the battery voltage should
	be within EDV-deep-dsg_voltage and EDV.*/
	uint16_t deep_dsg_voltage;
	/*max_dsg_voltage:limits the amount of the estimated discharge when learning is in progress.\
	if the amount of the capacity estimation get greater than this threshold ,the learning gets disqualified*/
	uint16_t max_dsg_estimate;
	/*light_load: FCC learning on discharge disqualifies if the load is below this threshold when the
	when EDV2 is reached.*/
	uint8_t light_load;
	/*near_full: this defines a capacity zone from FCC to FCC - near_full. A discharge cycles start
	from this capacity zone qualifies for FCC larning.*/
	uint16_t near_full;
	/*cycle_threshold: the amount of capacity that should be dicharged from the battery to increment
	the cycle count by 1.cycle counting happens on the discharge only.*/
	uint16_t cycle_threshold;
	/*recharge: the voltage of recharge.*/
	uint16_t recharge;
      /*mode_swtich_capacity: this defines how much capacity should pass through the coulomb counter
	  to cause a cycle count start condition (either charge or discharge). the gauge support 2 cycle typeds.
	  charge and discharge. a cycle starts when mode_switch_capacity passes through the coulomb counter
	  the cycle get canceled and switches to the opposite direciton if mode_switch_capacity passes though
	  the coulomb counter in oppositer direciton.*/
	uint8_t mode_switch_capacity;
	/*call_period: approximate time between fuel gauge calls.*/
	uint8_t call_period;

	struct ocv_config *ocv;
	struct edv_config *edv;
	//struct ocv_config  ocv;
	//struct edv_config  edv;

};

/* Cell State */
/*
light-load: ( < C/40)

*/
struct cell_state {
/*SOC : state-of-charge of the battery in %,it represents the % full of the battery from the
 system empty voltage.
 SOC = NAC/FCC,  SOC = 1 -DOD
*/
	int16_t	soc;
/* nac :nominal avaiable charge of the battery in mAh. it represents the present 
remain capacity of the battery to the system empty voltage under nominal conditions*/
	int16_t	nac;
/*fcc: full battery capacity .this represents the discharge capacity of the battery from
the defined full condition to the system empty voltage(EDV0) under nominal conditions.
 the value is learned by the algorithm on qualified charge and discharge cycleds*/
	int16_t fcc;
/* qmax: the battery capacity(mAh) at the OCV curve discharge rate*/
	int16_t qmax;

	int16_t voltage;
	int16_t av_voltage;
	int16_t cur;
	int16_t av_current;

	int16_t temperature;
/*cycle_count: it represents how many charge or discharge cycles a battery has experience.
this is used to estimate the change of impedance of the battery due to "aging"*/
	int16_t cycle_count;
/*sleep : in this mode ,the battery fuel gauge is counting discharge with the coulomb
counter and checking for the battery relaxed condition, if a relaxed battery is
destected the fuel gauge enters OCV mode*/
	bool sleep;
	bool relax;

	bool chg;
	bool dsg;

	bool edv0;
	bool edv1;
	bool edv2;
	bool ocv;
	bool cc;
	bool full;

	bool eocl;
	bool vcq;
	bool vdq;
	bool init;

	struct timeval sleep_timer;
	struct timeval el_sleep_timer;
	uint16_t cumulative_sleep;

	int16_t prev_soc;
	int16_t learn_q;
	uint16_t dod_eoc;
	int16_t learn_offset;
	uint16_t learned_cycle;
	int16_t new_fcc;
	int16_t	ocv_total_q;
	int16_t	ocv_enter_q;
	int16_t	negative_q;
	int16_t	overcharge_q;
	int16_t	charge_cycle_q;
	int16_t	discharge_cycle_q;
	int16_t	cycle_q;
	uint8_t	sequential_cc;
	uint8_t	sleep_samples;
	uint8_t	sequential_edvs;

	uint16_t electronics_load;
	uint16_t cycle_dsg_estimate;

	struct edv_state edv;

	bool updated;
	bool calibrate;

	struct cell_config *config;
};

struct battery_platform_data {
	int *battery_tmp_tbl;
	unsigned int tblsize;
       u32  * battery_ocv ;
	unsigned int  ocv_size;

	unsigned int monitoring_interval;

	unsigned int max_charger_currentmA;
	unsigned int max_charger_voltagemV;
	unsigned int termination_currentmA;

	unsigned int max_bat_voltagemV;
	unsigned int low_bat_voltagemV;

	unsigned int sense_resistor_mohm;

	/* twl6032 */
	unsigned long features;
       unsigned long errata;

      	struct cell_config *cell_cfg;
};


