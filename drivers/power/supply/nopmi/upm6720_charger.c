/*
* upm6720 battery charging driver
*/
#define pr_fmt(fmt)	"[upm6720] %s: " fmt, __func__

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>

#include "upm6720_reg.h"
#include <linux/iio/iio.h>
#include <linux/iio/consumer.h>
#include <linux/qti_power_supply.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>


typedef enum {
	CHG_SW_CAP_MODE,
	CHG_BYPASS_MODE,
} chg_mode_t;

typedef enum {
	UPM_ADC_IBUS,
	UPM_ADC_VBUS,
	UPM_ADC_VAC1,
	UPM_ADC_VAC2,
	UPM_ADC_VOUT,
	UPM_ADC_VBAT,
	UPM_ADC_IBAT,
	UPM_ADC_TSBUS,
	UPM_ADC_TSBAT,
	UPM_ADC_TDIE,
	UPM_ADC_MAX,
} adc_channel_t;

#define UPM6720_ROLE_STANDALONE  0
#define UPM6720_ROLE_SLAVE       1
#define UPM6720_ROLE_MASTER      2

enum {
    UPM6720_STDALONE,
    UPM6720_SLAVE,
    UPM6720_MASTER,
};

static int upm6720_mode_data[] = {
    [UPM6720_STDALONE] = UPM6720_ROLE_STANDALONE,
    [UPM6720_SLAVE] = UPM6720_ROLE_SLAVE,
    [UPM6720_MASTER] = UPM6720_ROLE_MASTER,
};

#define upm_err(fmt, ...)								\
do {											\
    if (upm->mode == UPM6720_ROLE_MASTER)						\
        printk(KERN_ERR "[UPM6720-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else if (upm->mode == UPM6720_ROLE_SLAVE)					\
        printk(KERN_ERR "[UPM6720-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else										\
        printk(KERN_ERR "[UPM6720-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

#define upm_info(fmt, ...)								\
do {											\
    if (upm->mode == UPM6720_ROLE_MASTER)						\
        printk(KERN_INFO "[UPM6720-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else if (upm->mode == UPM6720_ROLE_SLAVE)					\
        printk(KERN_INFO "[UPM6720-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else										\
        printk(KERN_INFO "[UPM6720-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

#define upm_dbg(fmt, ...)								\
do {											\
    if (upm->mode == UPM6720_ROLE_MASTER)						\
        printk(KERN_DEBUG "[UPM6720-MASTER]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else if (upm->mode == UPM6720_ROLE_SLAVE)					\
        printk(KERN_DEBUG "[UPM6720-SLAVE]:%s:" fmt, __func__, ##__VA_ARGS__);	\
    else										\
        printk(KERN_DEBUG "[UPM6720-STANDALONE]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

struct upm6720_cfg {
	bool bat_ovp_disable;
	bool bat_ocp_disable;
	bool bus_ucp_disable;
	bool bus_rcp_disable;
	bool vout_ovp_disable;
	bool tdie_flt_disable;
	bool tsbus_flt_disable;
	bool tsbat_flt_disable;
	bool wdt_disable;
	bool vbus_errhi_disable;

	int bat_ovp_th;
	int bat_ocp_th;
	int bus_ovp_th;
	int bus_ocp_th;
	int bus_ucp_th;
	int bus_rcp_th;
	int vac1_ovp_th;
	int vac2_ovp_th;
	int vout_ovp_th;
	int tdie_flt_th;
	int tsbus_flt_th;
	int tsbat_flt_th;

	bool bat_ovp_mask;
	bool bat_ocp_mask;
	bool bus_ovp_mask;
	bool bus_ocp_mask;
	bool bus_ucp_mask;
	bool bus_rcp_mask;
	bool vout_ovp_mask;
	bool vac1_ovp_mask;
	bool vac2_ovp_mask;

	bool vout_present_mask;
	bool vac1_present_mask;
	bool vac2_present_mask;
	bool vbus_present_mask;
	bool acrb1_config_mask;
	bool acrb2_config_mask;
	bool cfly_short_mask;
	bool adc_done_mask;
	bool ss_timeout_mask;
	bool tsbus_flt_mask;
	bool tsbat_flt_mask;
	bool tdie_flt_mask;
	bool wd_mask;
	bool regn_good_mask;
	bool conv_active_mask;
	bool vbus_errhi_mask;

	bool bat_ovp_alm_disable;
	bool bat_ocp_alm_disable;
	bool bat_ucp_alm_disable;
	bool bus_ovp_alm_disable;
	bool tdie_alm_disable;

	int bat_ovp_alm_th;
	int bat_ocp_alm_th;
	int bat_ucp_alm_th;
	int bus_ovp_alm_th;
	int bus_ocp_alm_th;
	int tdie_alm_th;

	bool bat_ovp_alm_mask;
	bool bat_ocp_alm_mask;
	bool bat_ucp_alm_mask;
	bool bus_ovp_alm_mask;
	bool bus_ocp_alm_mask;
	bool tsbus_tsbat_alm_mask;
	bool tdie_alm_mask;

	bool bus_pd_en;
	bool vac1_pd_en;
	bool vac2_pd_en;

	int sense_r_mohm;
	int ss_timeout;
	int wdt_set;
	bool chg_config_1;
	int fsw_set;
	int freq_shift;
	int ibus_ucp_fall_dg_sel;

	bool adc_enable;
	int adc_rate;
	int adc_avg;
	int adc_avg_init;
	int adc_sample;
	bool ibus_adc_disable;
	bool vbus_adc_disable;
	bool vac1_adc_disable;
	bool vac2_adc_disable;
	bool vout_adc_disable;
	bool vbat_adc_disable;
	bool ibat_adc_disable;
	bool tsbus_adc_disable;
	bool tsbat_adc_disable;
	bool tdie_adc_disable;
};

struct upm6720 {
    struct device *dev;
    struct i2c_client *client;

    int part_no;
    int revision;

    int mode;

    struct mutex data_lock;
    struct mutex i2c_rw_lock;
    struct mutex irq_complete;

    bool irq_waiting;
    bool irq_disabled;
    bool resume_completed;

    bool usb_present;
    bool charge_enabled;	/* Register bit status */

    int irq_gpio;
    int irq;

    /* ADC reading */
    int vbat_volt;
    int vbus_volt;
    int vout_volt;
    int vac1_volt;
	int vac2_volt;

    int ibat_curr;
    int ibus_curr;

	int bus_temp;
	int bat_temp;
    int die_temp;

	bool bat_ovp_flt;
	bool vout_ovp_flt;
	bool bat_ocp_flt;
	bool bus_ovp_flt;
	bool bus_ocp_flt;
	bool bus_ucp_flt;
	bool bus_rcp_flt;
	bool cfly_short_flt;
	bool vac1_ovp_flt;
	bool vac2_ovp_flt;
	bool tsbus_flt;
	bool tsbat_flt;
	bool tdie_flt;

	bool bat_ovp_alm;
	bool bat_ocp_alm;
	bool bat_ucp_alm;
	bool bus_ovp_alm;
	bool bus_ocp_alm;
	bool tsbus_tsbat_alm;
	bool tdie_alm;

	bool vout_present;
	bool vac1_present;
	bool vac2_present;
	bool vbus_present;
	bool acrb1_config;
	bool acrb2_config;

	bool adc_done;
	bool ss_timeout;
	bool wd_stat;
	bool regn_good;
	bool conv_active;
	bool vbus_errhi;

    bool vbat_reg;
    bool ibat_reg;

	int  prev_alarm;
	int  prev_fault;

    struct upm6720_cfg *cfg;

    int skip_writes;
    int skip_reads;

    struct upm6720_platform_data *platform_data;

    struct delayed_work monitor_work;

    struct dentry *debug_root;

    struct power_supply_desc psy_desc;
    struct power_supply_config psy_cfg;
    struct power_supply *fc2_psy;

    struct iio_dev          *indio_dev;
	struct iio_chan_spec    *iio_chan;
	struct iio_channel	*int_iio_chans;

};

struct upm6720_iio_channels {
	const char *datasheet_name;
	int channel_num;
	enum iio_chan_type type;
	long info_mask;
};

#define UPM6720_IIO_CHAN(_name, _num, _type, _mask)		\
	{						\
		.datasheet_name = _name,		\
		.channel_num = _num,			\
		.type = _type,				\
		.info_mask = _mask,			\
	},

#define UPM6720_CHAN_VOLT(_name, _num)			\
	UPM6720_IIO_CHAN(_name, _num, IIO_VOLTAGE,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define UPM6720_CHAN_CUR(_name, _num)			\
	UPM6720_IIO_CHAN(_name, _num, IIO_CURRENT,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define UPM6720_CHAN_TEMP(_name, _num)			\
	UPM6720_IIO_CHAN(_name, _num, IIO_TEMP,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define UPM6720_CHAN_POW(_name, _num)			\
	UPM6720_IIO_CHAN(_name, _num, IIO_POWER,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define UPM6720_CHAN_ENERGY(_name, _num)			\
	UPM6720_IIO_CHAN(_name, _num, IIO_ENERGY,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define UPM6720_CHAN_COUNT(_name, _num)			\
	UPM6720_IIO_CHAN(_name, _num, IIO_COUNT,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

static const struct upm6720_iio_channels upm6720_iio_psy_channels[] = {
	UPM6720_CHAN_ENERGY("up_present", PSY_IIO_PRESENT)
	UPM6720_CHAN_ENERGY("up_charging_enabled", PSY_IIO_CHARGING_ENABLED)
	UPM6720_CHAN_ENERGY("up_status", PSY_IIO_STATUS)
	UPM6720_CHAN_ENERGY("up_battery_present", PSY_IIO_SP2130_BATTERY_PRESENT)
	UPM6720_CHAN_ENERGY("up_vbus_present", PSY_IIO_SP2130_VBUS_PRESENT)
	UPM6720_CHAN_VOLT("up_battery_voltage", PSY_IIO_SP2130_BATTERY_VOLTAGE)
	UPM6720_CHAN_CUR("up_battery_current", PSY_IIO_SP2130_BATTERY_CURRENT)
	UPM6720_CHAN_TEMP("up_battery_temperature", PSY_IIO_SP2130_BATTERY_TEMPERATURE)
	UPM6720_CHAN_VOLT("up_bus_voltage", PSY_IIO_SP2130_BUS_VOLTAGE)
	UPM6720_CHAN_CUR("up_bus_current", PSY_IIO_SP2130_BUS_CURRENT)
	UPM6720_CHAN_TEMP("up_bus_temperature", PSY_IIO_SP2130_BUS_TEMPERATURE)
	UPM6720_CHAN_TEMP("up_die_temperature", PSY_IIO_SP2130_DIE_TEMPERATURE)
	UPM6720_CHAN_ENERGY("up_alarm_status", PSY_IIO_SP2130_ALARM_STATUS)
	UPM6720_CHAN_ENERGY("up_fault_status", PSY_IIO_SP2130_FAULT_STATUS)
	UPM6720_CHAN_ENERGY("up_vbus_error_status", PSY_IIO_SP2130_VBUS_ERROR_STATUS)
//	UPM6720_CHAN_ENERGY("up_enable_adc", PSY_IIO_SP2130_ENABLE_ADC)
//	UPM6720_CHAN_ENERGY("up_enable_acdrv1", PSY_IIO_SP2130_ACDRV1_ENABLED)
//	UPM6720_CHAN_ENERGY("up_enable_otg", PSY_IIO_SP2130_ENABLE_OTG)
};

#define BAT_OVP_FAULT_SHIFT         0
#define BAT_OCP_FAULT_SHIFT         1
#define BUS_OVP_FAULT_SHIFT         2
#define BUS_OCP_FAULT_SHIFT         3
#define BAT_THERM_FAULT_SHIFT       4
#define BUS_THERM_FAULT_SHIFT       5
#define DIE_THERM_FAULT_SHIFT       6

#define BAT_OVP_FAULT_MASK          (1 << BAT_OVP_FAULT_SHIFT)
#define BAT_OCP_FAULT_MASK          (1 << BAT_OCP_FAULT_SHIFT)
#define BUS_OVP_FAULT_MASK          (1 << BUS_OVP_FAULT_SHIFT)
#define BUS_OCP_FAULT_MASK          (1 << BUS_OCP_FAULT_SHIFT)
#define BAT_THERM_FAULT_MASK        (1 << BAT_THERM_FAULT_SHIFT)
#define BUS_THERM_FAULT_MASK        (1 << BUS_THERM_FAULT_SHIFT)
#define DIE_THERM_FAULT_MASK        (1 << DIE_THERM_FAULT_SHIFT)

#define BAT_OVP_ALARM_SHIFT         0
#define BAT_OCP_ALARM_SHIFT         1
#define BUS_OVP_ALARM_SHIFT         2
#define BUS_OCP_ALARM_SHIFT         3
#define BAT_THERM_ALARM_SHIFT       4
#define BUS_THERM_ALARM_SHIFT       5
#define DIE_THERM_ALARM_SHIFT       6
#define BAT_UCP_ALARM_SHIFT         7

#define BAT_OVP_ALARM_MASK          (1 << BAT_OVP_ALARM_SHIFT)
#define BAT_OCP_ALARM_MASK          (1 << BAT_OCP_ALARM_SHIFT)
#define BUS_OVP_ALARM_MASK          (1 << BUS_OVP_ALARM_SHIFT)
#define BUS_OCP_ALARM_MASK          (1 << BUS_OCP_ALARM_SHIFT)
#define BAT_THERM_ALARM_MASK        (1 << BAT_THERM_ALARM_SHIFT)
#define BUS_THERM_ALARM_MASK        (1 << BUS_THERM_ALARM_SHIFT)
#define DIE_THERM_ALARM_MASK        (1 << DIE_THERM_ALARM_SHIFT)
#define BAT_UCP_ALARM_MASK          (1 << BAT_UCP_ALARM_SHIFT)

#define VBAT_REG_STATUS_SHIFT       0
#define IBAT_REG_STATUS_SHIFT       1

#define VBAT_REG_STATUS_MASK        (1 << VBAT_REG_STATUS_SHIFT)
#define IBAT_REG_STATUS_MASK        (1 << VBAT_REG_STATUS_SHIFT)


static struct upm6720 *g_upm;

/************************************************************************/
static int __upm6720_read_byte(struct upm6720 *upm, u8 reg, u8 *data)
{
    s32 ret;

    ret = i2c_smbus_read_byte_data(upm->client, reg);
    if (ret < 0) {
        pr_err("i2c read fail: can't read from reg 0x%02X\n", reg);
        return ret;
    }

    *data = (u8) ret;

    return 0;
}

static int __upm6720_write_byte(struct upm6720 *upm, int reg, u8 val)
{
    s32 ret;

    ret = i2c_smbus_write_byte_data(upm->client, reg, val);
    if (ret < 0) {
        pr_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
            val, reg, ret);
        return ret;
    }
    return 0;
}

static int upm6720_read_byte(struct upm6720 *upm, u8 reg, u8 *data)
{
    int ret;

    if (upm->skip_reads) {
        *data = 0;
        return 0;
    }

    mutex_lock(&upm->i2c_rw_lock);
    ret = __upm6720_read_byte(upm, reg, data);
    mutex_unlock(&upm->i2c_rw_lock);

    return ret;
}

static int upm6720_write_byte(struct upm6720 *upm, u8 reg, u8 data)
{
    int ret;

    if (upm->skip_writes)
        return 0;

    mutex_lock(&upm->i2c_rw_lock);
    ret = __upm6720_write_byte(upm, reg, data);
    mutex_unlock(&upm->i2c_rw_lock);

    return ret;
}

static int upm6720_update_bits(struct upm6720*upm, u8 reg,
                    u8 mask, u8 data)
{
    int ret;
    u8 tmp;

    if (upm->skip_reads || upm->skip_writes)
        return 0;

    mutex_lock(&upm->i2c_rw_lock);
    ret = __upm6720_read_byte(upm, reg, &tmp);
    if (ret) {
        pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);
        goto out;
    }

    tmp &= ~mask;
    tmp |= data & mask;

    ret = __upm6720_write_byte(upm, reg, tmp);
    if (ret)
        pr_err("Failed: reg=%02X, ret=%d\n", reg, ret);

out:
    mutex_unlock(&upm->i2c_rw_lock);
    return ret;
}

/*********************************************************************/
static int upm6720_en_surge_protect(struct upm6720 *upm, bool enable)
{
	int ret = 0;

	pr_err("enable:%d\n", enable);
	if (enable) {
		ret = upm6720_write_byte(upm, 0xbe, 0x6e);
		ret |= upm6720_write_byte(upm, 0xc3, 0x80);
	} else {
		ret = upm6720_write_byte(upm, 0xbe, 0x00);
	}

	return ret;
}

static int upm6720_enable_charge(struct upm6720 *upm, bool enable)
{	
    u8 val;

    if (enable) {
		upm6720_en_surge_protect(upm, false);
        val = UPM6720_CHG_EN_ENABLE;
	} else {
		upm6720_en_surge_protect(upm, true);
        val = UPM6720_CHG_EN_DISABLE;
	}

    val <<= UPM6720_CHG_EN_SHIFT;

    pr_err("upm6720 charger %s\n", enable == false ? "disable" : "enable");
    return upm6720_update_bits(upm, UPM6720_REG_0F,
                UPM6720_CHG_EN_MASK, val);
}

static int upm6720_check_charge_enabled(struct upm6720 *upm, bool *enable)
{
    int ret = 0;
    u8 val = 0, val1 = 0;
	
	ret = upm6720_read_byte(upm, UPM6720_REG_0F, &val);
	ret |= upm6720_read_byte(upm, UPM6720_REG_17, &val1);
	if ((!ret) && (val & UPM6720_CHG_EN_MASK) && (val1 & UPM6720_CONV_ACTIVE_STAT_MASK)) { 
		*enable = true;
	} else {
		*enable = false;
	}

	return ret;
}

#if 0
static int upm6720_set_chg_config(struct upm6720 *upm, bool enable)
{
	u8 val;

    if (enable)
        val = UPM6720_CHG_CONFIG_1_TRUE;
    else
        val = UPM6720_CHG_CONFIG_1_FALSE;

    val <<= UPM6720_CHG_CONFIG_1_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_05,
                UPM6720_CHG_CONFIG_1_MASK, val);
}
#endif

static int upm6720_enable_wdt(struct upm6720 *upm, bool enable)
{
    u8 val;

    if (enable)
        val = UPM6720_WATCHDOG_ENABLE;
    else
        val = UPM6720_WATCHDOG_DISABLE;

    val <<= UPM6720_WATCHDOG_DIS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_10,
                UPM6720_WATCHDOG_DIS_MASK, val);
}

static int upm6720_set_wdt(struct upm6720 *upm, int ms)
{
    u8 val;

	if (ms <= 0) {
		val = UPM6720_WATCHDOG_30S;
	} else if (ms <= 500) {
		val = UPM6720_WATCHDOG_0S5;
	} else if (ms <= 1000) {
		val = UPM6720_WATCHDOG_1S;
	} else if (ms <= 5000) {
		val = UPM6720_WATCHDOG_5S;
	} else {
		val = UPM6720_WATCHDOG_30S;
	}

    val <<= UPM6720_WATCHDOG_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_10,
                UPM6720_WATCHDOG_MASK, val);
}

static int upm6720_set_reg_reset(struct upm6720 *upm)
{
    u8 val = 1;

    val <<= UPM6720_REG_RST_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_0F,
                UPM6720_REG_RST_MASK, val);
}


static int upm6720_enable_batovp(struct upm6720 *upm, bool enable)
{
    u8 val;

    if (enable)
        val = UPM6720_BAT_OVP_ENABLE;
    else
        val = UPM6720_BAT_OVP_DISABLE;

    val <<= UPM6720_BAT_OVP_DIS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_00,
                UPM6720_BAT_OVP_DIS_MASK, val);
}

static int upm6720_set_batovp_th(struct upm6720 *upm, int threshold)
{
    u8 val;

	threshold *= 1000;

    if (threshold < UPM6720_BAT_OVP_BASE) {
        threshold = UPM6720_BAT_OVP_BASE;
    } else if (threshold > UPM6720_BAT_OVP_MAX) {
		threshold = UPM6720_BAT_OVP_MAX;
	}

    val = (threshold - UPM6720_BAT_OVP_BASE - UPM6720_BAT_OVP_OFFSET) / UPM6720_BAT_OVP_LSB;

    val <<= UPM6720_BAT_OVP_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_00,
                UPM6720_BAT_OVP_MASK, val);
}

static int upm6720_enable_batocp(struct upm6720 *upm, bool enable)
{
    u8 val;

    if (enable)
        val = UPM6720_BAT_OCP_ENABLE;
    else
        val = UPM6720_BAT_OCP_DISABLE;

    val <<= UPM6720_BAT_OCP_DIS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_02,
                UPM6720_BAT_OCP_DIS_MASK, val);
}

static int upm6720_enable_busucp(struct upm6720 *upm, bool enable)
{
	u8 val;

    if (enable)
        val = UPM6720_BUS_UCP_ENABLE;
    else
        val = UPM6720_BUS_UCP_DISABLE;

    val <<= UPM6720_BUS_UCP_DIS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_05,
                UPM6720_BUS_UCP_DIS_MASK, val);
}

static int upm6720_enable_busrcp(struct upm6720 *upm, bool enable)
{
	u8 val;

    if (enable)
        val = UPM6720_BUS_RCP_ENABLE;
    else
        val = UPM6720_BUS_RCP_DISABLE;

    val <<= UPM6720_BUS_RCP_DIS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_05,
                UPM6720_BUS_RCP_DIS_MASK, val);
}

static int upm6720_enable_voutovp(struct upm6720 *upm, bool enable)
{
	u8 val;

    if (enable)
        val = UPM6720_VOUT_OVP_DIS_ENABLE;
    else
        val = UPM6720_VOUT_OVP_DIS_DISABLE;

    val <<= UPM6720_VOUT_OVP_DIS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_12,
                UPM6720_VOUT_OVP_DIS_MASK, val);
}

static int upm6720_enable_tdie_flt(struct upm6720 *upm, bool enable)
{
	u8 val;

    if (enable)
        val = UPM6720_TDIE_FLT_ENABLE;
    else
        val = UPM6720_TDIE_FLT_DISABLE;

    val <<= UPM6720_TDIE_FLT_DIS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_0A,
                UPM6720_TDIE_FLT_DIS_MASK, val);
}

static int upm6720_enable_tsbus_flt(struct upm6720 *upm, bool enable)
{
	u8 val;

    if (enable)
        val = UPM6720_TSBUS_FLT_ENABLE;
    else
        val = UPM6720_TSBUS_FLT_DISABLE;

    val <<= UPM6720_TSBUS_FLT_DIS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_0A,
                UPM6720_TSBUS_FLT_DIS_MASK, val);
}

static int upm6720_enable_tsbat_flt(struct upm6720 *upm, bool enable)
{
	u8 val;

    if (enable)
        val = UPM6720_TSBAT_FLT_ENABLE;
    else
        val = UPM6720_TSBAT_FLT_DISABLE;

    val <<= UPM6720_TSBAT_FLT_DIS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_0A,
                UPM6720_TSBAT_FLT_DIS_MASK, val);
}

static int upm6720_enable_vbus_errhi(struct upm6720 *upm, bool enable)
{
	u8 val;

    if (enable)
        val = UPM6720_VBUS_ERRHI_DIS_ENABLE;
    else
        val = UPM6720_VBUS_ERRHI_DIS_DISABLE;

    val <<= UPM6720_VBUS_ERRHI_DIS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_05,
                UPM6720_VBUS_ERRHI_DIS_MASK, val);
}

static int upm6720_set_batocp_th(struct upm6720 *upm, int threshold)
{
    u8 val;

    if (threshold < UPM6720_BAT_OCP_MIN) {
        threshold = UPM6720_BAT_OCP_MIN;
    } else if (threshold > UPM6720_BAT_OCP_MAX) {
        threshold = UPM6720_BAT_OCP_MAX;
    }

    val = (threshold - UPM6720_BAT_OCP_BASE - UPM6720_BAT_OCP_OFFSET) / UPM6720_BAT_OCP_LSB;

    val <<= UPM6720_BAT_OCP_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_02,
                UPM6720_BAT_OCP_MASK, val);
}

static int upm6720_set_busovp_th(struct upm6720 *upm, chg_mode_t mode, int threshold)
{
    u8 val = 0;

	if (mode == CHG_SW_CAP_MODE) {
		if (threshold < UPM6720_SW_CAP_BUS_OVP_BASE) {
        	threshold = UPM6720_SW_CAP_BUS_OVP_BASE;
		} else if (threshold > UPM6720_SW_CAP_BUS_OVP_MAX) {
        	threshold = UPM6720_SW_CAP_BUS_OVP_MAX;
		}
    	val = (threshold - UPM6720_SW_CAP_BUS_OVP_BASE - UPM6720_SW_CAP_BUS_OVP_OFFSET) / UPM6720_SW_CAP_BUS_OVP_LSB;
	} else if (mode == CHG_BYPASS_MODE) {
		if (threshold < UPM6720_BYPASS_BUS_OVP_BASE) {
        	threshold = UPM6720_BYPASS_BUS_OVP_BASE;
		} else if (threshold > UPM6720_BYPASS_BUS_OVP_MAX) {
        	threshold = UPM6720_BYPASS_BUS_OVP_MAX;
		}
    	val = (threshold - UPM6720_BYPASS_BUS_OVP_BASE - UPM6720_BYPASS_BUS_OVP_OFFSET) / UPM6720_BYPASS_BUS_OVP_LSB;
	}

    val <<= UPM6720_BUS_OVP_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_06,
                UPM6720_BUS_OVP_MASK, val);
}

static int upm6720_set_busocp_th(struct upm6720 *upm, chg_mode_t mode, int threshold)
{
    u8 val = 0;

	if (mode == CHG_SW_CAP_MODE) {
		if (threshold < UPM6720_SW_CAP_BUS_OCP_BASE) {
        	threshold = UPM6720_SW_CAP_BUS_OCP_BASE;
		} else if (threshold > UPM6720_SW_CAP_BUS_OCP_MAX) {
        	threshold = UPM6720_SW_CAP_BUS_OCP_MAX;
		}
    	val = (threshold - UPM6720_SW_CAP_BUS_OCP_BASE - UPM6720_SW_CAP_BUS_OCP_OFFSET) / UPM6720_SW_CAP_BUS_OCP_LSB;
	} else if (mode == CHG_BYPASS_MODE) {
		if (threshold < UPM6720_BYPASS_BUS_OCP_BASE) {
        	threshold = UPM6720_BYPASS_BUS_OCP_BASE;
		} else if (threshold > UPM6720_BYPASS_BUS_OCP_MAX) {
        	threshold = UPM6720_BYPASS_BUS_OCP_MAX;
		}
    	val = (threshold - UPM6720_BYPASS_BUS_OCP_BASE - UPM6720_BYPASS_BUS_OCP_OFFSET) / UPM6720_BYPASS_BUS_OCP_LSB;
	}

    val <<= UPM6720_BUS_OCP_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_08,
                UPM6720_BUS_OCP_MASK, val);
}

static int upm6720_set_busucp_th(struct upm6720 *upm, int threshold)
{
    u8 val;

	if (threshold == 250) {
		val = UPM6720_BUS_UCP_250MA;
	} else {
		val = UPM6720_BUS_UCP_RESERVED;
	}

    val <<= UPM6720_BUS_UCP_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_05,
                UPM6720_BUS_UCP_MASK, val);
}

static int upm6720_set_busrcp_th(struct upm6720 *upm, int threshold)
{
    u8 val;

	if (threshold == 300) {
		val = UPM6720_BUS_RCP_300MA;
	} else {
		val = UPM6720_BUS_RCP_RESERVED;
	}

    val <<= UPM6720_BUS_RCP_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_05,
                UPM6720_BUS_RCP_MASK, val);
}

static int upm6720_set_vac1ovp_th(struct upm6720 *upm, int threshold)
{
    u8 val;

	if (threshold <= 0) {
		val = UPM6720_AC1_OVP_18V;
	} else if (threshold <= 6500) {
		val = UPM6720_AC1_OVP_6V5;
	} else if (threshold <= 10500) {
		val = UPM6720_AC1_OVP_10V5;
	} else if (threshold <= 12000) {
		val = UPM6720_AC1_OVP_12V;
	} else if (threshold <= 14000) {
		val = UPM6720_AC1_OVP_14V;
	} else if (threshold <= 16000) {
		val = UPM6720_AC1_OVP_16V;
	} else {
		val = UPM6720_AC1_OVP_18V;
	}

    val <<= UPM6720_AC1_OVP_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_0E,
                UPM6720_AC1_OVP_MASK, val);
}

static int upm6720_set_vac2ovp_th(struct upm6720 *upm, int threshold)
{
    u8 val;

	if (threshold <= 0) {
		val = UPM6720_AC2_OVP_18V;
	} else if (threshold <= 6500) {
		val = UPM6720_AC2_OVP_6V5;
	} else if (threshold <= 10500) {
		val = UPM6720_AC2_OVP_10V5;
	} else if (threshold <= 12000) {
		val = UPM6720_AC2_OVP_12V;
	} else if (threshold <= 14000) {
		val = UPM6720_AC2_OVP_14V;
	} else if (threshold <= 16000) {
		val = UPM6720_AC2_OVP_16V;
	} else {
		val = UPM6720_AC2_OVP_18V;
	}

    val <<= UPM6720_AC2_OVP_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_0E,
                UPM6720_AC2_OVP_MASK, val);
}

static int upm6720_set_voutovp_th(struct upm6720 *upm, int threshold)
{
    u8 val;

	if (threshold <= 0) {
		val = UPM6720_VOUT_OVP_5V;
	} else if (threshold <= 4700) {
		val = UPM6720_VOUT_OVP_4V7;
	} else if (threshold <= 4800) {
		val = UPM6720_VOUT_OVP_4V8;
	} else if (threshold <= 4900) {
		val = UPM6720_VOUT_OVP_4V9;
	} else {
		val = UPM6720_VOUT_OVP_5V;
	}

    val <<= UPM6720_VOUT_OVP_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_12,
                UPM6720_VOUT_OVP_MASK, val);
}

static int upm6720_set_tdie_flt_th(struct upm6720 *upm, int threshold)
{
    u8 val;

	if (threshold < UPM6720_TDIE_FLT_BASE) {
		threshold = UPM6720_TDIE_FLT_BASE;
	} else if (threshold > UPM6720_TDIE_FLT_MAX) {
		threshold = UPM6720_TDIE_FLT_MAX;
	}

	val = (threshold - UPM6720_TDIE_FLT_BASE) / UPM6720_TDIE_FLT_LSB;
    val <<= UPM6720_TDIE_FLT_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_0A,
                UPM6720_TDIE_FLT_MASK, val);
}

static int upm6720_set_tsbus_flt_th(struct upm6720 *upm, int threshold)
{
    u8 val;

	threshold *= 100;

	if (threshold < UPM6720_TSBUS_FLT_BASE) {
		threshold = UPM6720_TSBUS_FLT_BASE;
	} else if (threshold > UPM6720_TSBUS_FLT_MAX) {
		threshold = UPM6720_TSBUS_FLT_MAX;
	}

	val = (threshold - UPM6720_TSBUS_FLT_BASE - UPM6720_TSBUS_FLT_OFFSET) / UPM6720_TSBUS_FLT_LSB;
    val <<= UPM6720_TSBUS_FLT_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_0C,
                UPM6720_TSBUS_FLT_MASK, val);
}

static int upm6720_set_tsbat_flt_th(struct upm6720 *upm, int threshold)
{
    u8 val;

	threshold *= 100;

	if (threshold < UPM6720_TSBAT_FLT_BASE) {
		threshold = UPM6720_TSBAT_FLT_BASE;
	} else if (threshold > UPM6720_TSBAT_FLT_MAX) {
		threshold = UPM6720_TSBAT_FLT_MAX;
	}

	val = (threshold - UPM6720_TSBAT_FLT_BASE - UPM6720_TSBAT_FLT_OFFSET) / UPM6720_TSBAT_FLT_LSB;
    val <<= UPM6720_TSBAT_FLT_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_0D,
                UPM6720_TSBAT_FLT_MASK, val);
}

static int upm6720_set_bat_ovp_alm_th(struct upm6720 *upm, int threshold)
{
	u8 val;

	if (threshold < UPM6720_BAT_OVP_ALM_BASE) {
		threshold = UPM6720_BAT_OVP_ALM_BASE;
	} else if (threshold > UPM6720_BAT_OVP_ALM_MAX) {
		threshold = UPM6720_BAT_OVP_ALM_MAX;
	}

	val = (threshold - UPM6720_BAT_OVP_ALM_BASE - UPM6720_BAT_OVP_ALM_OFFSET) / UPM6720_BAT_OVP_ALM_LSB;
    val <<= UPM6720_BAT_OVP_ALM_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_01,
                UPM6720_BAT_OVP_ALM_MASK, val);
}

static int upm6720_set_bat_ocp_alm_th(struct upm6720 *upm, int threshold)
{
	u8 val;

	if (threshold < UPM6720_BAT_OCP_ALM_BASE) {
		threshold = UPM6720_BAT_OCP_ALM_BASE;
	} else if (threshold > UPM6720_BAT_OCP_ALM_MAX) {
		threshold = UPM6720_BAT_OCP_ALM_MAX;
	}

	val = (threshold - UPM6720_BAT_OCP_ALM_BASE - UPM6720_BAT_OCP_ALM_OFFSET) / UPM6720_BAT_OCP_ALM_LSB;
    val <<= UPM6720_BAT_OCP_ALM_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_03,
                UPM6720_BAT_OCP_ALM_MASK, val);
}

static int upm6720_set_bat_ucp_alm_th(struct upm6720 *upm, int threshold)
{
	u8 val;

	if (threshold < UPM6720_BAT_UCP_ALM_BASE) {
		threshold = UPM6720_BAT_UCP_ALM_BASE;
	} else if (threshold > UPM6720_BAT_UCP_ALM_MAX) {
		threshold = UPM6720_BAT_UCP_ALM_MAX;
	}

	val = (threshold - UPM6720_BAT_UCP_ALM_BASE - UPM6720_BAT_UCP_ALM_OFFSET) / UPM6720_BAT_UCP_ALM_LSB;
    val <<= UPM6720_BAT_UCP_ALM_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_04,
                UPM6720_BAT_UCP_ALM_MASK, val);
}

static int upm6720_set_bus_ovp_alm_th(struct upm6720 *upm, chg_mode_t mode, int threshold)
{
	u8 val;

	if (mode == CHG_SW_CAP_MODE) {
		if (threshold < UPM6720_SW_CAP_BUS_OVP_ALM_BASE) {
			threshold = UPM6720_SW_CAP_BUS_OVP_ALM_BASE;
		} else if (threshold > UPM6720_SW_CAP_BUS_OVP_ALM_MAX) {
			threshold = UPM6720_SW_CAP_BUS_OVP_ALM_MAX;
		}
		val = (threshold - UPM6720_SW_CAP_BUS_OVP_ALM_BASE - UPM6720_SW_CAP_BUS_OVP_ALM_OFFSET) / UPM6720_SW_CAP_BUS_OVP_ALM_LSB;
	} else {
		if (threshold < UPM6720_BYPASS_BUS_OVP_ALM_BASE) {
			threshold = UPM6720_BYPASS_BUS_OVP_ALM_BASE;
		} else if (threshold > UPM6720_BYPASS_BUS_OVP_ALM_MAX) {
			threshold = UPM6720_BYPASS_BUS_OVP_ALM_MAX;
		}
		val = (threshold - UPM6720_BYPASS_BUS_OVP_ALM_BASE - UPM6720_BYPASS_BUS_OVP_ALM_OFFSET) / UPM6720_BYPASS_BUS_OVP_ALM_LSB;
	}

    val <<= UPM6720_BUS_OVP_ALM_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_07,
                UPM6720_BUS_OVP_ALM_MASK, val);
}

static int upm6720_set_bus_ocp_alm_th(struct upm6720 *upm, int threshold)
{
	u8 val;

	if (threshold < UPM6720_BUS_OCP_ALM_BASE) {
		threshold = UPM6720_BUS_OCP_ALM_BASE;
	} else if (threshold > UPM6720_BUS_OCP_ALM_MAX) {
		threshold = UPM6720_BUS_OCP_ALM_MAX;
	}

	val = (threshold - UPM6720_BUS_OCP_ALM_BASE - UPM6720_BUS_OCP_ALM_OFFSET) / UPM6720_BUS_OCP_ALM_LSB;
    val <<= UPM6720_BUS_OCP_ALM_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_09,
                UPM6720_BUS_OCP_ALM_MASK, val);
}

static int upm6720_set_tdie_alm_th(struct upm6720 *upm, int threshold)
{
	u8 val;

	threshold *= 10;

	if (threshold < UPM6720_TDIE_ALM_BASE) {
		threshold = UPM6720_TDIE_ALM_BASE;
	} else if (threshold > UPM6720_TDIE_ALM_MAX) {
		threshold = UPM6720_TDIE_ALM_MAX;
	}

	val = (threshold - UPM6720_TDIE_ALM_BASE - UPM6720_TDIE_ALM_OFFSET) / UPM6720_TDIE_ALM_LSB;
    val <<= UPM6720_TDIE_ALM_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_0B,
                UPM6720_TDIE_ALM_MASK, val);
}

static int upm6720_enable_adc(struct upm6720 *upm, bool enable)
{
    u8 val;

    if (enable)
        val = UPM6720_ADC_EN_ENABLE;
    else
        val = UPM6720_ADC_EN_DISABLE;

    val <<= UPM6720_ADC_EN_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_23,
                UPM6720_ADC_EN_MASK, val);
}

int upm6720_set_adc(bool enable)
{
	int ret = -EINVAL;
	if (g_upm)
		ret = upm6720_enable_adc(g_upm, enable);
	return ret;
}
EXPORT_SYMBOL(upm6720_set_adc);

static int upm6720_set_adc_scanrate(struct upm6720 *upm, bool oneshot)
{
    u8 val;

    if (oneshot)
        val = UPM6720_ADC_RATE_ONE_SHOT;
    else
        val = UPM6720_ADC_RATE_CONTINUES;

    val <<= UPM6720_ADC_RATE_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_23,
                UPM6720_ADC_RATE_MASK, val);
}

#define ADC_REG_BASE UPM6720_REG_25
static int upm6720_get_adc_data(struct upm6720 *upm, adc_channel_t channel,  int *result)
{
    int ret = 0;
    u8 val_l = 0, val_h = 0;
    s16 val = 0;
	int sval = 0;

    if(channel >= UPM_ADC_MAX) {
		return 0;
	}

    ret = upm6720_read_byte(upm, ADC_REG_BASE + (channel << 1), &val_h);
	if (ret < 0) {
        return ret;
    }

    ret = upm6720_read_byte(upm, ADC_REG_BASE + (channel << 1) + 1, &val_l);
    if (ret < 0) {
        return ret;
    }

    val = (val_h << 8) | val_l;

	switch (channel) {
		case UPM_ADC_IBUS:	// !!!only switched cap mode, bypass mode will do later
			sval = val * UPM6720_IBUS_ADC_SW_CAP_LSB / UPM6720_IBUS_ADC_SW_CAP_PRECISION + UPM6720_IBUS_ADC_SW_CAP_BASE + UPM6720_IBUS_ADC_SW_CAP_OFFSET;
			break;
		case UPM_ADC_VBUS:
			sval = val * UPM6720_VBUS_ADC_LSB / UPM6720_VBUS_ADC_PRECISION + UPM6720_VBUS_ADC_BASE + UPM6720_VBUS_ADC_OFFSET;
			break;
		case UPM_ADC_VAC1:
			sval = val * UPM6720_VAC1_ADC_LSB / UPM6720_IBUS_ADC_SW_CAP_PRECISION + UPM6720_VAC1_ADC_BASE + UPM6720_VAC1_ADC_OFFSET;
			break;
		case UPM_ADC_VAC2:
			sval = val * UPM6720_VAC2_ADC_LSB / UPM6720_VAC2_ADC_PRECISION + UPM6720_VAC2_ADC_BASE + UPM6720_VAC2_ADC_OFFSET;
			break;
		case UPM_ADC_VOUT:
			sval = val * UPM6720_VOUT_ADC_LSB / UPM6720_VOUT_ADC_PRECISION + UPM6720_VOUT_ADC_BASE + UPM6720_VOUT_ADC_OFFSET;
			break;
		case UPM_ADC_VBAT:
			sval = val * UPM6720_VBAT_ADC_LSB / UPM6720_VBAT_ADC_PRECISION + UPM6720_VBAT_ADC_BASE + UPM6720_VBAT_ADC_OFFSET;
			break;
		case UPM_ADC_IBAT:
			sval = val * UPM6720_IBAT_ADC_LSB / UPM6720_IBAT_ADC_PRECISION + UPM6720_IBAT_ADC_BASE + UPM6720_IBAT_ADC_OFFSET;
			break;
		case UPM_ADC_TSBUS:
			sval = val * UPM6720_TSBUS_ADC_LSB / UPM6720_TSBUS_ADC_PRECISION + UPM6720_TSBUS_ADC_BASE + UPM6720_TSBUS_ADC_OFFSET;
			break;
		case UPM_ADC_TSBAT:
			sval = val * UPM6720_TSBAT_ADC_LSB / UPM6720_TSBAT_ADC_PRECISION + UPM6720_TSBAT_ADC_BASE + UPM6720_TSBAT_ADC_OFFSET;
			break;
		case UPM_ADC_TDIE:
			sval = val * UPM6720_TDIE_ADC_LSB / UPM6720_TDIE_ADC_PRECISION + UPM6720_TDIE_ADC_BASE + UPM6720_TDIE_ADC_OFFSET;
			break;
		default:
			break;
	}

    *result = sval;
	pr_err("channel:%d, val_h:0x%x, val_l:0x%x, sval:%d\n",
		channel, val_h, val_l, sval);

    return ret;
}

static int upm6720_set_adc_channel_enable(struct upm6720 *upm, adc_channel_t channel, bool enable)
{
	u8 reg = 0, mask = 0, val = 0, shift = 0;

	if (channel >= UPM_ADC_MAX) {
		 return -EINVAL;
	}

	switch (channel) {
		case UPM_ADC_IBUS:
			reg = UPM6720_REG_23;
			mask = UPM6720_IBUS_ADC_DIS_MASK;
			shift = UPM6720_IBUS_ADC_DIS_SHIFT;
			break;

		case UPM_ADC_VBUS:
			reg = UPM6720_REG_23;
			mask = UPM6720_VBUS_ADC_DIS_MASK;
			shift = UPM6720_VBUS_ADC_DIS_SHIFT;
			break;

		case UPM_ADC_VAC1:
			reg = UPM6720_REG_24;
			mask = UPM6720_VAC1_ADC_DIS_MASK;
			shift = UPM6720_VAC1_ADC_DIS_SHIFT;
			break;

		case UPM_ADC_VAC2:
			reg = UPM6720_REG_24;
			mask = UPM6720_VAC2_ADC_DIS_MASK;
			shift = UPM6720_VAC2_ADC_DIS_SHIFT;
			break;

		case UPM_ADC_VOUT:
			reg = UPM6720_REG_24;
			mask = UPM6720_VOUT_ADC_DIS_MASK;
			shift = UPM6720_VOUT_ADC_DIS_SHIFT;
			break;

		case UPM_ADC_VBAT:
			reg = UPM6720_REG_24;
			mask = UPM6720_VBAT_ADC_DIS_MASK;
			shift = UPM6720_VBAT_ADC_DIS_SHIFT;
			break;

		case UPM_ADC_IBAT:
			reg = UPM6720_REG_24;
			mask = UPM6720_IBAT_ADC_DIS_MASK;
			shift = UPM6720_IBAT_ADC_DIS_SHIFT;
			break;

		case UPM_ADC_TSBUS:
			reg = UPM6720_REG_24;
			mask = UPM6720_TSBUS_ADC_DIS_MASK;
			shift = UPM6720_TSBUS_ADC_DIS_SHIFT;
			break;

		case UPM_ADC_TSBAT:
			reg = UPM6720_REG_24;
			mask = UPM6720_TSBAT_ADC_DIS_MASK;
			shift = UPM6720_TSBAT_ADC_DIS_SHIFT;
			break;

		case UPM_ADC_TDIE:
			reg = UPM6720_REG_24;
			mask = UPM6720_TDIE_ADC_DIS_MASK;
			shift = UPM6720_TDIE_ADC_DIS_SHIFT;
			break;

		default:
			break;
	}

	val = (!enable) << shift;

    return upm6720_update_bits(upm, reg, mask, val);
}

static int upm6720_set_sense_resistor(struct upm6720 *upm, int r_mohm)
{
    u8 val;

	if (r_mohm >= 5) {
		val = UPM6720_RSNS_5MOHM;
	} else {
		val = UPM6720_RSNS_2MOHM;
	}

    val <<= UPM6720_RSNS_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_11,
                UPM6720_RSNS_MASK, val);
}

static int upm6720_set_ss_timeout(struct upm6720 *upm, int timeout)
{
    u8 val;

	if (timeout <= 0) {
		val = UPM6720_SS_TIMEOUT_10S;
	} else if (timeout <= 7) {
		val = UPM6720_SS_TIMEOUT_6MS25;
	} else if (timeout <= 13) {
		val = UPM6720_SS_TIMEOUT_12MS5;
	} else if (timeout <= 25) {
		val = UPM6720_SS_TIMEOUT_25MS;
	} else if (timeout <= 50) {
		val = UPM6720_SS_TIMEOUT_50MS;
	} else if (timeout <= 100) {
		val = UPM6720_SS_TIMEOUT_100MS;
	} else if (timeout <= 400) {
		val = UPM6720_SS_TIMEOUT_400MS;
	} else if (timeout <= 1500) {
		val = UPM6720_SS_TIMEOUT_1S5;
	} else {
		val = UPM6720_SS_TIMEOUT_10S;
	}

    val <<= UPM6720_SS_TIMEOUT_SHIFT;

    return upm6720_update_bits(upm, UPM6720_REG_11,
                UPM6720_SS_TIMEOUT_MASK, val);
}

static int upm6720_get_work_mode(struct upm6720 *upm, int *mode)
{
    int ret;
    u8 val;

    ret = upm6720_read_byte(upm, UPM6720_REG_12, &val);
    if (ret) {
        pr_err("Failed to read operation mode register\n");
        return ret;
    }

    val = (val & UPM6720_MS_MASK) >> UPM6720_MS_SHIFT;
    if (val == UPM6720_MS_PRIMARY)
        *mode = UPM6720_ROLE_MASTER;
    else if (val == UPM6720_MS_SECONDARY)
        *mode = UPM6720_ROLE_SLAVE;
    else 
        *mode = UPM6720_ROLE_STANDALONE;

    pr_err("work mode:%s\n", *mode == UPM6720_ROLE_STANDALONE ? "Standalone" :
            (*mode == UPM6720_ROLE_SLAVE ? "Slave" : "Master"));
    return ret;
}

static int upm6720_detect_device(struct upm6720 *upm)
{
    int ret;
    u8 data;

    ret = upm6720_read_byte(upm, UPM6720_REG_22, &data);
    if (ret == 0) {
        upm->part_no = (data & UPM6720_DEVICE_ID_MASK) >> UPM6720_DEVICE_ID_SHIFT;
    }

    return ret;
}

static int upm6720_parse_dt(struct upm6720 *upm, struct device *dev)
{
    int ret;
    struct device_node *np = dev->of_node;

    upm->cfg = devm_kzalloc(dev, sizeof(struct upm6720_cfg),
                    GFP_KERNEL);

    if (!upm->cfg)
        return -ENOMEM;

    upm->cfg->bat_ovp_disable = of_property_read_bool(np,
            "upm6720,bat-ovp-disable");
	upm->cfg->bat_ocp_disable = of_property_read_bool(np,
            "upm6720,bat-ocp_disable");
	upm->cfg->bus_ucp_disable = of_property_read_bool(np,
			"upm6720,bus-ucp-disable");
	upm->cfg->bus_rcp_disable = of_property_read_bool(np,
			"upm6720,bus-rcp-disable");
	upm->cfg->vout_ovp_disable = of_property_read_bool(np,
            "upm6720,vout-ovp-disable");
	upm->cfg->tdie_flt_disable = of_property_read_bool(np,
            "upm6720,tdie-flt-disable");
	upm->cfg->tsbus_flt_disable = of_property_read_bool(np,
			"upm6720,tsbus-flt-disable");
	upm->cfg->tsbat_flt_disable = of_property_read_bool(np,
			"upm6720,tsbat-flt-disable");
	upm->cfg->wdt_disable = of_property_read_bool(np,
			"upm6720,wdt-disable");
	upm->cfg->vbus_errhi_disable = of_property_read_bool(np,
			"upm6720,vbus-errhi-disable");

	ret = of_property_read_u32(np, "upm6720,bat-ovp-threshold",
            &upm->cfg->bat_ovp_th);
    if (ret) {
        pr_err("failed to read bat-ovp-threshold\n");
        return ret;
    }

	ret = of_property_read_u32(np, "upm6720,bat-ocp-threshold",
            &upm->cfg->bat_ocp_th);
    if (ret) {
        pr_err("failed to read bat-ocp-threshold\n");
        return ret;
    }

	ret = of_property_read_u32(np, "upm6720,bat-ocp-threshold",
            &upm->cfg->bat_ocp_th);
    if (ret) {
        pr_err("failed to read bat-ocp-threshold\n");
        return ret;
    }

	ret = of_property_read_u32(np, "upm6720,bus-ovp-threshold",
            &upm->cfg->bus_ovp_th);
    if (ret) {
        pr_err("failed to read bus-ovp-threshold\n");
        return ret;
    }

    ret = of_property_read_u32(np, "upm6720,bus-ocp-threshold",
            &upm->cfg->bus_ocp_th);
    if (ret) {
        pr_err("failed to read bus-ocp-threshold\n");
        return ret;
    }

    ret = of_property_read_u32(np, "upm6720,bus-ucp-threshold",
            &upm->cfg->bus_ucp_th);
    if (ret) {
        pr_err("failed to read bus-ucp-threshold\n");
        //return ret;
    }

    ret = of_property_read_u32(np, "upm6720,bus-rcp-threshold",
            &upm->cfg->bus_rcp_th);
    if (ret) {
        pr_err("failed to read bus-rcp-threshold\n");
        //return ret;
    }

    ret = of_property_read_u32(np, "upm6720,vac1-ovp-threshold",
            &upm->cfg->vac1_ovp_th);
    if (ret) {
        pr_err("failed to read vac1-ovp-threshold\n");
        //return ret;
    }

    ret = of_property_read_u32(np, "upm6720,vac2-ovp-threshold",
            &upm->cfg->vac2_ovp_th);
    if (ret) {
        pr_err("failed to read vac2-ovp-threshold\n");
        //return ret;
    }

	ret = of_property_read_u32(np, "upm6720,vout-ovp-threshold",
            &upm->cfg->vout_ovp_th);
    if (ret) {
        pr_err("failed to read vout-ovp-threshold\n");
        //return ret;
    }

	ret = of_property_read_u32(np, "upm6720,tdie-flt-threshold",
            &upm->cfg->tdie_flt_th);
    if (ret) {
        pr_err("failed to read tdie-flt-threshold\n");
        //return ret;
    }

	ret = of_property_read_u32(np, "upm6720,tsbus-flt-threshold",
            &upm->cfg->tsbus_flt_th);
    if (ret) {
        pr_err("failed to read tsbus-flt-threshold\n");
        //return ret;
    }

	ret = of_property_read_u32(np, "upm6720,tsbat-flt-threshold",
			&upm->cfg->tsbat_flt_th);
	if (ret) {
		pr_err("failed to read tsbat-flt-threshold\n");
		//return ret;
	}

	upm->cfg->bat_ovp_mask = of_property_read_bool(np,
            "upm6720,bat-ovp-mask");
	upm->cfg->bat_ocp_mask = of_property_read_bool(np,
			"upm6720,bat-ocp-mask");
	upm->cfg->bus_ovp_mask = of_property_read_bool(np,
            "upm6720,bus-ovp-mask");
	upm->cfg->bus_ocp_mask = of_property_read_bool(np,
			"upm6720,bus-ocp-mask");
	upm->cfg->bus_ucp_mask = of_property_read_bool(np,
            "upm6720,bus-ucp-mask");
	upm->cfg->bus_rcp_mask = of_property_read_bool(np,
			"upm6720,bus-rcp-mask");
	upm->cfg->vout_ovp_mask = of_property_read_bool(np,
            "upm6720,vout-ovp-mask");
	upm->cfg->vac1_ovp_mask = of_property_read_bool(np,
			"upm6720,vac1-ovp-mask");
	upm->cfg->vac2_ovp_mask = of_property_read_bool(np,
			"upm6720,vac2-ovp-mask");

	upm->cfg->vout_present_mask = of_property_read_bool(np,
            "upm6720,vout-present-mask");
	upm->cfg->vac1_present_mask = of_property_read_bool(np,
			"upm6720,vac1-present-mask");
	upm->cfg->vac2_present_mask = of_property_read_bool(np,
            "upm6720,vac2-present-mask");
	upm->cfg->vbus_present_mask = of_property_read_bool(np,
			"upm6720,vbus-present-mask");
	upm->cfg->acrb1_config_mask = of_property_read_bool(np,
            "upm6720,acrb1-config-mask");
	upm->cfg->acrb2_config_mask = of_property_read_bool(np,
			"upm6720,acrb2-config-mask");
	upm->cfg->cfly_short_mask = of_property_read_bool(np,
            "upm6720,cfly-short-mask");
	upm->cfg->adc_done_mask = of_property_read_bool(np,
			"upm6720,adc-done-mask");
	upm->cfg->ss_timeout_mask = of_property_read_bool(np,
			"upm6720,ss-timeout-mask");
	upm->cfg->tsbus_flt_mask = of_property_read_bool(np,
            "upm6720,tsbus-flt-mask");
	upm->cfg->tsbat_flt_mask = of_property_read_bool(np,
			"upm6720,tsbat-flt-mask");
	upm->cfg->tdie_flt_mask = of_property_read_bool(np,
            "upm6720,tdie-flt-mask");
	upm->cfg->wd_mask = of_property_read_bool(np,
			"upm6720,wd-mask");
	upm->cfg->regn_good_mask = of_property_read_bool(np,
            "upm6720,regn-good-mask");
	upm->cfg->conv_active_mask = of_property_read_bool(np,
			"upm6720,conv-active-mask");
	upm->cfg->vbus_errhi_mask = of_property_read_bool(np,
            "upm6720,vbus-errhi-mask");

	upm->cfg->bat_ovp_alm_disable = of_property_read_bool(np,
            "upm6720,bat-ovp-alm-disable");
	upm->cfg->bat_ocp_alm_disable = of_property_read_bool(np,
			"upm6720,bat-ocp-alm-disable");
	upm->cfg->bat_ucp_alm_disable = of_property_read_bool(np,
            "upm6720,bat-ucp-alm-disable");
	upm->cfg->bus_ovp_alm_disable = of_property_read_bool(np,
			"upm6720,bus-ovp-alm-disable");
	upm->cfg->tdie_alm_disable = of_property_read_bool(np,
            "upm6720,tdie-alm-disable");

	ret = of_property_read_u32(np, "upm6720,bat-ovp-alm-threshold",
			&upm->cfg->bat_ovp_alm_th);
	if (ret) {
		pr_err("failed to read bat-ovp-alm-threshold\n");
		//return ret;
	}

	ret = of_property_read_u32(np, "upm6720,bat-ocp-alm-threshold",
			&upm->cfg->bat_ocp_alm_th);
	if (ret) {
		pr_err("failed to read bat-ocp-alm-threshold\n");
		//return ret;
	}

	ret = of_property_read_u32(np, "upm6720,bat-ucp-alm-threshold",
			&upm->cfg->bat_ucp_alm_th);
	if (ret) {
		pr_err("failed to read bat-ucp-alm-threshold\n");
		//return ret;
	}

	ret = of_property_read_u32(np, "upm6720,bus-ovp-alm-threshold",
			&upm->cfg->bus_ovp_alm_th);
	if (ret) {
		pr_err("failed to read bus-ovp-alm-threshold\n");
		//return ret;
	}

	ret = of_property_read_u32(np, "upm6720,bus-ocp-alm-threshold",
			&upm->cfg->bus_ocp_alm_th);
	if (ret) {
		pr_err("failed to read bus-ocp-alm-threshold\n");
		//return ret;
	}

	ret = of_property_read_u32(np, "upm6720,tdie-alm-threshold",
			&upm->cfg->tdie_alm_th);
	if (ret) {
		pr_err("failed to read tdie-alm-threshold\n");
		//return ret;
	}

	upm->cfg->bat_ovp_alm_mask = of_property_read_bool(np,
            "upm6720,bat-ovp-alm-mask");
	upm->cfg->bat_ocp_alm_mask = of_property_read_bool(np,
			"upm6720,bat-ocp-alm-mask");
	upm->cfg->bat_ucp_alm_mask = of_property_read_bool(np,
            "upm6720,bat-ucp-alm-mask");
	upm->cfg->bus_ovp_alm_mask = of_property_read_bool(np,
			"upm6720,bus-ovp-alm-mask");
	upm->cfg->bus_ocp_alm_mask = of_property_read_bool(np,
            "upm6720,bus-ocp-alm-mask");
	upm->cfg->tsbus_tsbat_alm_mask = of_property_read_bool(np,
			"upm6720,tsbus-tsbat-alm-mask");
	upm->cfg->tdie_alm_mask = of_property_read_bool(np,
            "upm6720,tdie-alm-mask");

	upm->cfg->bus_pd_en = of_property_read_bool(np,
            "upm6720,bus-pulldown-en");
	upm->cfg->vac1_pd_en = of_property_read_bool(np,
			"upm6720,vac1-pulldown-en");
	upm->cfg->vac2_pd_en = of_property_read_bool(np,
            "upm6720,vac2-pulldown-en");

    ret = of_property_read_u32(np, "upm6720,sense-resistor-mohm",
            &upm->cfg->sense_r_mohm);
    if (ret) {
        pr_err("failed to read sense-resistor-mohm\n");
        return ret;
    }

    ret = of_property_read_u32(np, "upm6720,ss-timeout",
            &upm->cfg->ss_timeout);
    if (ret) {
        pr_err("failed to read ss-timeout\n");
        //return ret;
    }

    ret = of_property_read_u32(np, "upm6720,wdt-set",
            &upm->cfg->wdt_set);
    if (ret) {
        pr_err("failed to read wdt-set\n");
        //return ret;
    }

	upm->cfg->chg_config_1 = of_property_read_bool(np,
        	"upm6720,chg-config-1");

    ret = of_property_read_u32(np, "upm6720,fsw-set",
            &upm->cfg->fsw_set);
    if (ret) {
        pr_err("failed to read fsw-set\n");
        //return ret;
    }

    ret = of_property_read_u32(np, "upm6720,freq-shift",
            &upm->cfg->freq_shift);
    if (ret) {
        pr_err("failed to read freq-shift\n");
        //return ret;
    }

    ret = of_property_read_u32(np, "upm6720,ibus-ucp-fall-dg-sel",
            &upm->cfg->ibus_ucp_fall_dg_sel);
    if (ret) {
        pr_err("failed to read ibus-ucp-fall-dg-sel\n");
        //return ret;
    }

	upm->cfg->adc_enable = of_property_read_bool(np,
			"upm6720,adc-enable");

    ret = of_property_read_u32(np, "upm6720,adc-rate",
            &upm->cfg->adc_rate);
    if (ret) {
        pr_err("failed to read adc-rate\n");
        //return ret;
    }

	ret = of_property_read_u32(np, "upm6720,adc-avg",
            &upm->cfg->adc_avg);
    if (ret) {
        pr_err("failed to read adc-avg\n");
        //return ret;
    }

	ret = of_property_read_u32(np, "upm6720,adc-avg-init",
            &upm->cfg->adc_avg_init);
    if (ret) {
        pr_err("failed to read adc-avg-init\n");
        //return ret;
    }

	ret = of_property_read_u32(np, "upm6720,adc-sample-bit",
            &upm->cfg->adc_sample);
    if (ret) {
        pr_err("failed to read adc-sample-bit\n");
        //return ret;
    }

	upm->cfg->ibus_adc_disable = of_property_read_bool(np,
            "upm6720,ibus-adc-disable");
	upm->cfg->vbus_adc_disable = of_property_read_bool(np,
			"upm6720,vbus-adc-disable");
	upm->cfg->vac1_adc_disable = of_property_read_bool(np,
            "upm6720,vac1-adc-disable");
	upm->cfg->vac2_adc_disable = of_property_read_bool(np,
			"upm6720,vac2-adc-disable");
	upm->cfg->vout_adc_disable = of_property_read_bool(np,
            "upm6720,vout-adc-disable");
	upm->cfg->vbat_adc_disable = of_property_read_bool(np,
			"upm6720,vbat-adc-disable");
	upm->cfg->ibat_adc_disable = of_property_read_bool(np,
            "upm6720,ibat-adc-disable");
	upm->cfg->tsbus_adc_disable = of_property_read_bool(np,
            "upm6720,tsbus-adc-disable");
	upm->cfg->tsbat_adc_disable = of_property_read_bool(np,
			"upm6720,tsbat-adc-disable");
	upm->cfg->tdie_adc_disable = of_property_read_bool(np,
            "upm6720,tdie-adc-disable");

    return 0;
}

static int upm6720_init_protection(struct upm6720 *upm)
{
    int ret;

    ret = upm6720_enable_batovp(upm, !upm->cfg->bat_ovp_disable);
    pr_err("%s bat ovp %s\n",
        upm->cfg->bat_ovp_disable ? "disable" : "enable",
        !ret ? "successfullly" : "failed");

    ret = upm6720_enable_batocp(upm, !upm->cfg->bat_ocp_disable);
    pr_err("%s bat ocp %s\n",
        upm->cfg->bat_ocp_disable ? "disable" : "enable",
        !ret ? "successfullly" : "failed");

    ret = upm6720_enable_busucp(upm, !upm->cfg->bus_ucp_disable);
    pr_err("%s bus ucp %s\n",
        upm->cfg->bus_ucp_disable ? "disable" : "enable",
        !ret ? "successfullly" : "failed");

    ret = upm6720_enable_busrcp(upm, !upm->cfg->bus_rcp_disable);
    pr_err("%s bus rcp %s\n",
        upm->cfg->bus_rcp_disable ? "disable" : "enable",
        !ret ? "successfullly" : "failed");

    ret = upm6720_enable_voutovp(upm, !upm->cfg->vout_ovp_disable);
    pr_err("%s vout ovp %s\n",
        upm->cfg->vout_ovp_disable ? "disable" : "enable",
        !ret ? "successfullly" : "failed");

    ret = upm6720_enable_tdie_flt(upm, !upm->cfg->tdie_flt_disable);
    pr_err("%s tdie flt %s\n",
        upm->cfg->tdie_flt_disable ? "disable" : "enable",
        !ret ? "successfullly" : "failed");

    ret = upm6720_enable_tsbus_flt(upm, !upm->cfg->tsbus_flt_disable);
    pr_err("%s tsbus flt %s\n",
        upm->cfg->tsbus_flt_disable ? "disable" : "enable",
        !ret ? "successfullly" : "failed");

    ret = upm6720_enable_tsbat_flt(upm, !upm->cfg->tsbat_flt_disable);
    pr_err("%s tsbat flt %s\n",
        upm->cfg->tsbat_flt_disable ? "disable" : "enable",
        !ret ? "successfullly" : "failed");

    ret = upm6720_enable_vbus_errhi(upm, !upm->cfg->vbus_errhi_disable);
    pr_err("%s vbus errhi %s\n",
        upm->cfg->vbus_errhi_disable ? "disable" : "enable",
        !ret ? "successfullly" : "failed");

    ret = upm6720_set_batovp_th(upm, upm->cfg->bat_ovp_th);
    pr_err("set bat ovp th %d %s\n", upm->cfg->bat_ovp_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_batocp_th(upm, upm->cfg->bat_ocp_th);
    pr_err("set bat ocp threshold %d %s\n", upm->cfg->bat_ocp_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_busovp_th(upm, CHG_SW_CAP_MODE, upm->cfg->bus_ovp_th);
    pr_err("set bus ovp threshold %d %s\n", upm->cfg->bus_ovp_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_busocp_th(upm, CHG_SW_CAP_MODE, upm->cfg->bus_ocp_th);
    pr_err("set bus ocp threshold %d %s\n", upm->cfg->bus_ocp_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_busucp_th(upm, upm->cfg->bus_ucp_th);
    pr_err("set bus ucp threshold %d %s\n", upm->cfg->bus_ucp_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_busrcp_th(upm, upm->cfg->bus_rcp_th);
    pr_err("set bus rcp threshold %d %s\n", upm->cfg->bus_rcp_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_vac1ovp_th(upm, upm->cfg->vac1_ovp_th);
    pr_err("set vac1 ovp threshold %d %s\n", upm->cfg->vac1_ovp_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_vac2ovp_th(upm, upm->cfg->vac2_ovp_th);
    pr_err("set vac2 ovp threshold %d %s\n", upm->cfg->vac2_ovp_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_voutovp_th(upm, upm->cfg->vout_ovp_th);
    pr_err("set vout ovp threshold %d %s\n", upm->cfg->vout_ovp_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_tdie_flt_th(upm, upm->cfg->tdie_flt_th);
    pr_err("set tdie flt threshold %d %s\n", upm->cfg->tdie_flt_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_tsbus_flt_th(upm, upm->cfg->tsbus_flt_th);
    pr_err("set tsbus flt threshold %d %s\n", upm->cfg->tsbus_flt_th,
        !ret ? "successfully" : "failed");

    ret = upm6720_set_tsbat_flt_th(upm, upm->cfg->tsbat_flt_th);
    pr_err("set tsbat flt threshold %d %s\n", upm->cfg->tsbat_flt_th,
        !ret ? "successfully" : "failed");

    return 0;
}

static int upm6720_init_alarm(struct upm6720 *upm)
{
	upm6720_set_bat_ovp_alm_th(upm, upm->cfg->bat_ovp_alm_th);
	upm6720_set_bat_ocp_alm_th(upm, upm->cfg->bat_ocp_alm_th);
	upm6720_set_bat_ucp_alm_th(upm, upm->cfg->bat_ucp_alm_th);
	upm6720_set_bus_ovp_alm_th(upm, CHG_SW_CAP_MODE  , upm->cfg->bus_ovp_alm_th);
	upm6720_set_bus_ocp_alm_th(upm, upm->cfg->bus_ocp_alm_th);
	upm6720_set_tdie_alm_th(upm, upm->cfg->tdie_alm_th);

	return 0;
}

static int upm6720_init_adc(struct upm6720 *upm)
{

    upm6720_set_adc_scanrate(upm, upm->cfg->adc_rate);
    upm6720_set_adc_channel_enable(upm, UPM_ADC_IBUS, !upm->cfg->ibus_adc_disable);
    upm6720_set_adc_channel_enable(upm, UPM_ADC_VBUS, !upm->cfg->vbus_adc_disable);
    upm6720_set_adc_channel_enable(upm, UPM_ADC_VAC1, !upm->cfg->vac1_adc_disable);
    upm6720_set_adc_channel_enable(upm, UPM_ADC_VAC2, !upm->cfg->vac2_adc_disable);
    upm6720_set_adc_channel_enable(upm, UPM_ADC_VOUT, !upm->cfg->vout_adc_disable);
    upm6720_set_adc_channel_enable(upm, UPM_ADC_VBAT, !upm->cfg->vbat_adc_disable);
    upm6720_set_adc_channel_enable(upm, UPM_ADC_IBAT, !upm->cfg->ibat_adc_disable);
	upm6720_set_adc_channel_enable(upm, UPM_ADC_TSBUS, !upm->cfg->tsbus_adc_disable);
    upm6720_set_adc_channel_enable(upm, UPM_ADC_TSBAT, !upm->cfg->tsbat_adc_disable);
    upm6720_set_adc_channel_enable(upm, UPM_ADC_TDIE, !upm->cfg->tdie_adc_disable);

    upm6720_enable_adc(upm, upm->cfg->adc_enable);

    return 0;
}

static int upm6720_init_device(struct upm6720 *upm)
{
    upm6720_set_reg_reset(upm);
    upm6720_enable_wdt(upm, !upm->cfg->wdt_disable);
    upm6720_set_wdt(upm, upm->cfg->wdt_set);

    upm6720_set_ss_timeout(upm, upm->cfg->ss_timeout);
    upm6720_set_sense_resistor(upm, upm->cfg->sense_r_mohm);

    upm6720_init_protection(upm);
	upm6720_init_alarm(upm);
    upm6720_init_adc(upm);


    return 0;
}

static int upm6720_set_present(struct upm6720 *upm, bool present)
{
    upm->usb_present = present;

    if (present)
        upm6720_init_device(upm);

    return 0;
}

static void upm6720_dump_registers(struct upm6720 *upm)
{
	int ret;
	u8 val;
	u8 addr;

	for (addr = 0x00; addr <= 0x21; addr++) {
		ret = upm6720_read_byte(upm, addr, &val);
		if (!ret)
			pr_err("Reg[%02X] = 0x%02X\n", addr, val);
	}
}

static void upm6720_monitor_work(struct work_struct *work)
{
	struct upm6720 *upm = container_of(
			work, struct upm6720, monitor_work.work);
	
	pr_err("monitor dump\n");
	
	upm6720_dump_registers(upm);
	
	schedule_delayed_work(&upm->monitor_work, msecs_to_jiffies(3*1000));
}

#if 0
static ssize_t upm6720_show_registers(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct upm6720 *upm = dev_get_drvdata(dev);
    u8 addr;
    u8 val;
    u8 tmpbuf[300];
    int len;
    int idx = 0;
    int ret;

    idx = snprintf(buf, PAGE_SIZE, "%s:\n", "upm6720");
    for (addr = 0x0; addr <= 0x38; addr++) {
        ret = upm6720_read_byte(upm, addr, &val);
        if (ret == 0) {
            len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[%.2X] = 0x%.2x\n", addr, val);
            memcpy(&buf[idx], tmpbuf, len);
            idx += len;
        }
    }

    return idx;
}

static ssize_t upm6720_store_register(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    struct upm6720 *upm = dev_get_drvdata(dev);
    int ret;
    unsigned int reg;
    unsigned int val;

    ret = sscanf(buf, "%x %x", &reg, &val);
    if (ret == 2 && reg <= 0x38)
        upm6720_write_byte(upm, (unsigned char)reg, (unsigned char)val);

    return count;
}

static DEVICE_ATTR(registers, 0660, upm6720_show_registers, upm6720_store_register);

static void upm6720_create_device_node(struct device *dev)
{
    device_create_file(dev, &dev_attr_registers);
}
#endif

static enum power_supply_property upm6720_charger_props[] = {
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
    POWER_SUPPLY_PROP_STATUS,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
#if 0
    POWER_SUPPLY_PROP_CHARGING_ENABLED,

    POWER_SUPPLY_PROP_UPM_BUS_VOLTAGE,
    POWER_SUPPLY_PROP_UPM_BUS_CURRENT,
    POWER_SUPPLY_PROP_UPM_BAT_VOLTAGE,
    POWER_SUPPLY_PROP_UPM_BAT_CURRENT,
    POWER_SUPPLY_PROP_UPM_AC1_VOLTAGE,
    POWER_SUPPLY_PROP_UPM_AC2_VOLTAGE,
    POWER_SUPPLY_PROP_UPM_OUT_VOLTAGE,
    POWER_SUPPLY_PROP_UPM_DIE_TEMP,
#endif
};

static int upm6720_charger_get_property(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
    struct upm6720 *upm = power_supply_get_drvdata(psy);
    int result;
    int ret = 0;
    bool en;

    switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
        val->intval = upm->usb_present;
		ret = 0;
        break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
        ret = upm6720_get_adc_data(upm, UPM_ADC_VBUS, &result);
        if (!ret)
            upm->vbus_volt = result;
        val->intval = upm->vbus_volt;
        break;

    case POWER_SUPPLY_PROP_STATUS:
        ret = upm6720_check_charge_enabled(upm, &en);
        if (!ret)
            upm->charge_enabled = en;
        val->intval = upm->charge_enabled ? POWER_SUPPLY_STATUS_CHARGING : POWER_SUPPLY_STATUS_NOT_CHARGING;
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        ret = upm6720_get_adc_data(upm, UPM_ADC_VBAT, &result);
        if (!ret)
            upm->vbat_volt = result;
        val->intval = upm->vbat_volt;
        break;
#if 0
    case POWER_SUPPLY_PROP_UPM_BUS_VOLTAGE:
        ret = upm6720_get_adc_data(upm, UPM_ADC_VBUS, &result);
        if (!ret)
            upm->vbus_volt = result;
        val->intval = upm->vbus_volt;
        break;
    case POWER_SUPPLY_PROP_UPM_BUS_CURRENT:
        ret = upm6720_get_adc_data(upm, UPM_ADC_IBUS, &result);
        if (!ret)
            upm->ibus_curr = result;
        val->intval = upm->ibus_curr;
        break;
	case POWER_SUPPLY_PROP_UPM_BAT_VOLTAGE:
        ret = upm6720_get_adc_data(upm, UPM_ADC_VBAT, &result);
        if (!ret)
            upm->vbat_volt = result;
        val->intval = upm->vbat_volt;
        break;
	case POWER_SUPPLY_PROP_UPM_BAT_CURRENT:
        ret = upm6720_get_adc_data(upm, UPM_ADC_IBAT, &result);
        if (!ret)
            upm->ibat_curr = result;
        val->intval = upm->ibat_curr;
        break;
	case POWER_SUPPLY_PROP_UPM_AC1_VOLTAGE:
        ret = upm6720_get_adc_data(upm, UPM_ADC_VAC1, &result);
        if (!ret)
            upm->vac1_volt = result;
        val->intval = upm->vac1_volt;
        break;
	case POWER_SUPPLY_PROP_UPM_AC2_VOLTAGE:
        ret = upm6720_get_adc_data(upm, UPM_ADC_VAC2, &result);
        if (!ret)
            upm->vac2_volt = result;
        val->intval = upm->vac2_volt;
        break;
	case POWER_SUPPLY_PROP_UPM_OUT_VOLTAGE:
        ret = upm6720_get_adc_data(upm, UPM_ADC_VOUT, &result);
        if (!ret)
            upm->vout_volt = result;
        val->intval = upm->vout_volt;
        break;
    case POWER_SUPPLY_PROP_UPM_DIE_TEMP:
        ret = upm6720_get_adc_data(upm, UPM_ADC_TDIE, &result);
        if (!ret)
            upm->die_temp = result;
        val->intval = upm->die_temp;
        break;
#endif
    default:
        return -EINVAL;
    }

	if (ret)
		pr_err("err: psp:%d, ret:%d", psp, ret);

    return ret;
}

static int upm6720_charger_set_property(struct power_supply *psy,
                    enum power_supply_property prop,
                    const union power_supply_propval *val)
{
    struct upm6720 *upm = power_supply_get_drvdata(psy);

    switch (prop) {
#if 0
    case POWER_SUPPLY_PROP_CHARGING_ENABLED:
        upm6720_enable_charge(upm, val->intval);
        upm6720_check_charge_enabled(upm, &upm->charge_enabled);
        pr_err("POWER_SUPPLY_PROP_CHARGING_ENABLED: %s\n",
                val->intval ? "enable" : "disable");
        break;
#endif
    case POWER_SUPPLY_PROP_PRESENT:
        upm6720_set_present(upm, !!val->intval);
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int upm6720_charger_is_writeable(struct power_supply *psy,
                    enum power_supply_property prop)
{
    int ret = 0;

    switch (prop) {
#if 0
    case POWER_SUPPLY_PROP_CHARGING_ENABLED:
        ret = 1;
        break;
#endif
    default:
        ret = 0;
        break;
    }
    return ret;
}

static int upm6720_psy_register(struct upm6720 *upm)
{
    upm->psy_cfg.drv_data = upm;
    upm->psy_cfg.of_node = upm->dev->of_node;

    if (upm->mode == UPM6720_ROLE_MASTER)
        upm->psy_desc.name = "upm6720-master";
    else if (upm->mode == UPM6720_ROLE_SLAVE)
        upm->psy_desc.name = "upm6720-slave";
    else
        upm->psy_desc.name = "charger_standalone";

    upm->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
    upm->psy_desc.properties = upm6720_charger_props;
    upm->psy_desc.num_properties = ARRAY_SIZE(upm6720_charger_props);
    upm->psy_desc.get_property = upm6720_charger_get_property;
    upm->psy_desc.set_property = upm6720_charger_set_property;
    upm->psy_desc.property_is_writeable = upm6720_charger_is_writeable;

    upm->fc2_psy = devm_power_supply_register(upm->dev, 
            &upm->psy_desc, &upm->psy_cfg);
    if (IS_ERR(upm->fc2_psy)) {
        pr_err("failed to register fc2_psy\n");
        return PTR_ERR(upm->fc2_psy);
    }

    pr_err("%s power supply register successfully\n", upm->psy_desc.name);

    return 0;
}

static void upm6720_check_alarm_fault_status(struct upm6720 *upm)
{
    int ret, i;
    u8 val = 0;

    mutex_lock(&upm->data_lock);

	ret = upm6720_read_byte(upm, UPM6720_REG_13, &val);
	if (!ret) {
		pr_err("STAT REG[0x%.2X]: 0x%.2X\n", UPM6720_REG_13, val);
		upm->bat_ovp_flt = !!(val & UPM6720_BAT_OVP_STAT_MASK);
		upm->bat_ovp_alm = !!(val & UPM6720_BAT_OVP_ALM_STAT_MASK);
		upm->vout_ovp_flt = !!(val & UPM6720_VOUT_OVP_STAT_MASK);
		upm->bat_ocp_flt = !!(val & UPM6720_BAT_OCP_STAT_MASK);
		upm->bat_ocp_alm = !!(val & UPM6720_BAT_OCP_ALM_STAT_MASK);
		upm->bat_ucp_alm = !!(val & UPM6720_BAT_UCP_ALM_STAT_MASK);
		upm->bus_ovp_flt = !!(val & UPM6720_BUS_OVP_STAT_MASK);
		upm->bus_ovp_alm = !!(val & UPM6720_BUS_OVP_ALM_STAT_MASK);
	}

	ret = upm6720_read_byte(upm, UPM6720_REG_14, &val);
	if (!ret) {
		pr_err("STAT REG[0x%.2X]: 0x%.2X\n", UPM6720_REG_14, val);
		upm->bus_ocp_flt = !!(val & UPM6720_BUS_OCP_STAT_MASK);
		upm->bus_ocp_alm = !!(val & UPM6720_BUS_OCP_ALM_STAT_MASK);
		upm->bus_ucp_flt = !!(val & UPM6720_BUS_UCP_STAT_MASK);
		upm->bus_rcp_flt = !!(val & UPM6720_BUS_RCP_STAT_MASK);
		upm->cfly_short_flt = !!(val & UPM6720_CFLY_SHORT_STAT_MASK);
	}

	ret = upm6720_read_byte(upm, UPM6720_REG_15, &val);
	if (!ret) {
		pr_err("STAT REG[0x%.2X]: 0x%.2X\n", UPM6720_REG_15, val);
		upm->vac1_ovp_flt = !!(val & UPM6720_VAC1_OVP_STAT_MASK);
		upm->vac2_ovp_flt = !!(val & UPM6720_VAC2_OVP_STAT_MASK);
		upm->vout_present = !!(val & UPM6720_VOUT_PRESENT_STAT_MASK);
		upm->vac1_present = !!(val & UPM6720_VAC1_PRESENT_STAT_MASK);
		upm->vac2_present = !!(val & UPM6720_VAC2_PRESENT_STAT_MASK);
		upm->vbus_present = !!(val & UPM6720_VBUS_PRESENT_STAT_MASK);
		upm->acrb1_config = !!(val & UPM6720_ACRB1_CONFIG_STAT_MASK);
		upm->acrb2_config = !!(val & UPM6720_ACRB2_CONFIG_STAT_MASK);
	}

	ret = upm6720_read_byte(upm, UPM6720_REG_16, &val);
	if (!ret) {
		pr_err("STAT REG[0x%.2X]: 0x%.2X\n", UPM6720_REG_16, val);
		upm->adc_done = !!(val & UPM6720_ADC_DONE_STAT_MASK);
		upm->ss_timeout = !!(val & UPM6720_SS_TIMEOUT_STAT_MASK);
		upm->tsbus_tsbat_alm = !!(val & UPM6720_TSBUS_TSBAT_ALM_STAT_MASK);
		upm->tsbus_flt = !!(val & UPM6720_TSBUS_FLT_STAT_MASK);
		upm->tsbat_flt = !!(val & UPM6720_TSBAT_FLT_STAT_MASK);
		upm->tdie_flt = !!(val & UPM6720_TDIE_FLT_STAT_MASK);
		upm->tdie_alm = !!(val & UPM6720_TDIE_ALM_STAT_MASK);
		upm->wd_stat = !!(val & UPM6720_WD_STAT_MASK);
	}

	ret = upm6720_read_byte(upm, UPM6720_REG_17, &val);
	if (!ret) {
		pr_err("STAT REG[0x%.2X]: 0x%.2X\n", UPM6720_REG_17, val);
		upm->regn_good = !!(val & UPM6720_REGN_GOOD_STAT_MASK);
		upm->conv_active = !!(val & UPM6720_CONV_ACTIVE_STAT_MASK);
		upm->vbus_errhi = !!(val & UPM6720_VBUS_ERRHI_STAT_MASK);
	}

	for (i = UPM6720_REG_18; i <= UPM6720_REG_1C; i++) {
		ret = upm6720_read_byte(upm, i, &val);
	    if (!ret) {
			pr_err("FLAG REG[0x%.2X]: 0x%.2X\n", i, val);
		}
	}

    mutex_unlock(&upm->data_lock);
}

/*
* interrupt does nothing, just info event chagne, other module could get info
* through power supply interface
*/
static irqreturn_t upm6720_charger_interrupt(int irq, void *dev_id)
{
    struct upm6720 *upm = dev_id;

    pr_err("INT OCCURED\n");

    mutex_lock(&upm->irq_complete);
    upm->irq_waiting = true;
    if (!upm->resume_completed) {
        dev_dbg(upm->dev, "IRQ triggered before device-resume\n");
        if (!upm->irq_disabled) {
            disable_irq_nosync(irq);
            upm->irq_disabled = true;
        }
		upm->irq_waiting = false;
        mutex_unlock(&upm->irq_complete);
        return IRQ_HANDLED;
    }

    upm6720_check_alarm_fault_status(upm);

	upm->irq_waiting = false;
    mutex_unlock(&upm->irq_complete);
    power_supply_changed(upm->fc2_psy);

    return IRQ_HANDLED;
}

static int upm6720_irq_register(struct upm6720 *upm)
{
    int ret;
    struct device_node *node = upm->dev->of_node;

    if (!node) {
        pr_err("device tree node missing\n");
        return -EINVAL;
    }

    upm->irq_gpio = of_get_named_gpio(node, "upm6720,irq-gpio", 0);
    if (!gpio_is_valid(upm->irq_gpio)) {
        pr_err("fail to valid gpio : %d\n", upm->irq_gpio);
        return -EINVAL;
    }

    ret = gpio_request_one(upm->irq_gpio, GPIOF_DIR_IN, "upm6720_irq");
    if (ret) {
        pr_err("fail to request upm6720 irq\n");
        return EINVAL;
    }

    upm->irq =gpio_to_irq(upm->irq_gpio);
    if (upm->irq < 0) {
        pr_err("fail to gpio to irq\n");
        return EINVAL;
    }

    if (upm->mode == UPM6720_ROLE_STANDALONE) {
        ret = devm_request_threaded_irq(&upm->client->dev, upm->irq, NULL,
                upm6720_charger_interrupt, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                "upm6720 standalone irq", upm);
    } else if (upm->mode == UPM6720_ROLE_MASTER) {
        ret = devm_request_threaded_irq(&upm->client->dev, upm->irq, NULL,
                upm6720_charger_interrupt, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                "upm6720 master irq", upm);
    } else {
        ret = devm_request_threaded_irq(&upm->client->dev, upm->irq, NULL,
                upm6720_charger_interrupt, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                "upm6720 slave irq", upm);
    }
    if (ret < 0) {
        pr_err("request irq for irq=%d failed, ret=%d\n", upm->irq, ret);
        return ret;
    }
    enable_irq_wake(upm->irq);

    return ret;
}

static void determine_initial_status(struct upm6720 *upm)
{
    if (upm->irq)
        upm6720_charger_interrupt(upm->irq, upm);
}

static int upm6720_iio_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val1,
		int val2, long mask)
{
	struct upm6720 *upm = iio_priv(indio_dev);
	int rc = 0;

	switch (chan->channel) {
		case PSY_IIO_CHARGING_ENABLED:
			upm6720_enable_charge(upm, val1);
			upm6720_check_charge_enabled(upm, &upm->charge_enabled);
			pr_err("cp charge enabled: %s\n", val1 ? "enable" : "disable");
			break;
		case PSY_IIO_PRESENT:
			upm6720_set_present(upm, !!val1);
			break;
#if 0
	case PSY_IIO_SP2130_ENABLE_ADC:
		sp2130_enable_adc(sc, !!val1);
		sc->adc_status = !!val1;
		break;
	case PSY_IIO_SP2130_ACDRV1_ENABLED:
		sp2130_enable_acdrv1(sc, val1);
		sc_info("POWER_SUPPLY_PROP_ACDRV1_ENABLED: %s\n",
				val1 ? "enable" : "disable");
	case PSY_IIO_SP2130_ENABLE_OTG:
		sp2130_enable_otg(sc, val1);
		sc_info("POWER_SUPPLY_PROP_OTG_ENABLED: %s\n",
				val1 ? "enable" : "disable");
#endif
		default:
			pr_debug("Unsupported UPM6720 IIO chan %d\n", chan->channel);
			rc = -EINVAL;
			break;
	}

	if (rc < 0)
		pr_err("Couldn't write IIO channel %d, rc = %d\n",
			chan->channel, rc);

	return rc;
}

static int upm6720_iio_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int *val1,
		int *val2, long mask)
{
	struct upm6720 *upm = iio_priv(indio_dev);
	int result = 0;
	int ret = 0;
	*val1 = 0;

	switch (chan->channel) {
		case PSY_IIO_CHARGING_ENABLED:
			upm6720_check_charge_enabled(upm, &upm->charge_enabled);
			*val1 = upm->charge_enabled;
			break;
		case PSY_IIO_STATUS:
			*val1 = 0;
			break;
		case PSY_IIO_PRESENT:
			*val1 = 1;
			break;
		case PSY_IIO_SP2130_BATTERY_PRESENT:
			*val1 = 1;
			break;
		case PSY_IIO_SP2130_VBUS_PRESENT:
			*val1 = upm->vbus_present;
			break;
		case PSY_IIO_SP2130_BATTERY_VOLTAGE:
			ret = upm6720_get_adc_data(upm, UPM_ADC_VBAT, &result);
	        if (!ret)
	            upm->vbat_volt = result;
			*val1 = upm->vbat_volt;
			break;
		case PSY_IIO_SP2130_BATTERY_CURRENT:
			ret = upm6720_get_adc_data(upm, UPM_ADC_IBAT, &result);
	        if (!ret)
	            upm->ibat_curr = result;
			*val1 = upm->ibat_curr;
			break;
		case PSY_IIO_SP2130_BATTERY_TEMPERATURE:
			ret = upm6720_get_adc_data(upm, UPM_ADC_TSBAT, &result);
			if (!ret)
				upm->bat_temp = result;
			*val1 = upm->bat_temp;
			break;
		case PSY_IIO_SP2130_BUS_VOLTAGE:
			ret = upm6720_get_adc_data(upm, UPM_ADC_VBUS, &result);
	        if (!ret)
	            upm->vbus_volt = result;
			*val1 = upm->vbus_volt;
			break;
		case PSY_IIO_SP2130_BUS_CURRENT:
			ret = upm6720_get_adc_data(upm, UPM_ADC_IBUS, &result);
	        if (!ret)
	            upm->ibus_curr = result;
			*val1 = upm->ibus_curr;
			break;
		case PSY_IIO_SP2130_BUS_TEMPERATURE:
			ret = upm6720_get_adc_data(upm, UPM_ADC_TSBUS, &result);
	        if (!ret)
	            upm->bus_temp = result;
			*val1 = upm->bus_temp;
			break;
		case PSY_IIO_SP2130_DIE_TEMPERATURE:
			ret = upm6720_get_adc_data(upm, UPM_ADC_TDIE, &result);
	        if (!ret)
	            upm->die_temp = result;
			*val1= upm->die_temp;
			break;
		case PSY_IIO_SP2130_ALARM_STATUS:
			//upm6720_check_alarm_fault_status(upm);
			*val1 = (upm->bat_ovp_alm << BAT_OVP_ALARM_SHIFT) |
					(upm->bat_ocp_alm << BAT_OCP_ALARM_SHIFT) |
					(upm->bat_ucp_alm << BAT_UCP_ALARM_SHIFT) |
					(upm->bus_ovp_alm << BUS_OVP_ALARM_SHIFT) |
					(upm->bus_ocp_alm << BUS_OCP_ALARM_SHIFT) |
					(upm->tsbus_tsbat_alm << BAT_THERM_ALARM_SHIFT) |
					(upm->tsbus_tsbat_alm << BUS_THERM_ALARM_SHIFT) |
					(upm->tdie_alm << DIE_THERM_ALARM_SHIFT);
			break;
		case PSY_IIO_SP2130_FAULT_STATUS:
			//upm6720_check_alarm_fault_status(upm);
			*val1 = (upm->bat_ovp_flt << BAT_OVP_FAULT_SHIFT) |
					(upm->bat_ocp_flt << BAT_OCP_FAULT_SHIFT) |
					(upm->bus_ovp_flt << BUS_OVP_FAULT_SHIFT) |
					(upm->bus_ocp_flt << BUS_OCP_FAULT_SHIFT) |
					(upm->tsbat_flt << BAT_THERM_FAULT_SHIFT) |
					(upm->tsbus_flt << BUS_THERM_FAULT_SHIFT) |
					(upm->tdie_alm << DIE_THERM_FAULT_SHIFT);
			break;
		case PSY_IIO_SP2130_VBUS_ERROR_STATUS:
			//upm6720_check_alarm_fault_status(upm);
			*val1 = upm->vbus_errhi << 4;
			break;
#if 0
		case PSY_IIO_SP2130_ENABLE_ADC:
			*val1 = sc->adc_status;
			break;
		case PSY_IIO_SP2130_ACDRV1_ENABLED:
			sp2130_check_enable_acdrv1(sc, &sc->acdrv1_enable);
			*val1 = sc->acdrv1_enable;
			sc_info("check acdrv1 enable: %d\n", *val1);
		case PSY_IIO_SP2130_ENABLE_OTG:
			sp2130_check_enable_otg(sc, &sc->otg_enable);
			*val1 = sc->otg_enable;
			sc_info("check otg enable: %d\n", *val1);
#endif
		default:
			pr_debug("Unsupported QG IIO chan %d\n", chan->channel);
			ret = -EINVAL;
			 break;
	}

	if (ret < 0) {
		pr_err("Couldn't read IIO channel %d, ret = %d\n",
			chan->channel, ret);
		return ret;
	}

	return IIO_VAL_INT;
}

static int upm6720_iio_of_xlate(struct iio_dev *indio_dev,
				const struct of_phandle_args *iiospec)
{
	struct upm6720 *upm = iio_priv(indio_dev);
	struct iio_chan_spec *iio_chan = upm->iio_chan;
	int i;

	for (i = 0; i < ARRAY_SIZE(upm6720_iio_psy_channels);
					i++, iio_chan++) {
		// pr_err("upm6720_iio_of: iio_chan->channel %d, iiospec->args[0]:%d\n",iio_chan->channel,iiospec->args[0]);
		if (iio_chan->channel == iiospec->args[0])
			return i;
	}
	return -EINVAL;
}

static const struct iio_info upm6720_iio_info = {
	.read_raw	= upm6720_iio_read_raw,
	.write_raw	= upm6720_iio_write_raw,
	.of_xlate	= upm6720_iio_of_xlate,
};

int upm6720_init_iio_psy(struct upm6720 *chip)
{
	struct iio_dev *indio_dev = chip->indio_dev;
	struct iio_chan_spec *chan;
	int num_iio_channels = ARRAY_SIZE(upm6720_iio_psy_channels);
	int rc, i;

	pr_err("upm6720_init_iio_psy start\n");
	chip->iio_chan = devm_kcalloc(chip->dev, num_iio_channels,
				sizeof(*chip->iio_chan), GFP_KERNEL);
	if (!chip->iio_chan)
		return -ENOMEM;

	chip->int_iio_chans = devm_kcalloc(chip->dev,
				num_iio_channels,
				sizeof(*chip->int_iio_chans),
				GFP_KERNEL);
	if (!chip->int_iio_chans)
		return -ENOMEM;

	indio_dev->info = &upm6720_iio_info;
	indio_dev->dev.parent = chip->dev;
	indio_dev->dev.of_node = chip->dev->of_node;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = chip->iio_chan;
	indio_dev->num_channels = num_iio_channels;

	indio_dev->name = "cp-standalone";
	for (i = 0; i < num_iio_channels; i++) {
		chip->int_iio_chans[i].indio_dev = indio_dev;
		chan = &chip->iio_chan[i];
		chip->int_iio_chans[i].channel = chan;
		chan->address = i;
		chan->channel = upm6720_iio_psy_channels[i].channel_num;
		chan->type = upm6720_iio_psy_channels[i].type;
		chan->datasheet_name =
			upm6720_iio_psy_channels[i].datasheet_name;
		chan->extend_name =
			upm6720_iio_psy_channels[i].datasheet_name;
		chan->info_mask_separate =
			upm6720_iio_psy_channels[i].info_mask;
	}

	rc = devm_iio_device_register(chip->dev, indio_dev);
	if (rc)
		pr_err("Failed to register UPM6720 IIO device, rc=%d\n", rc);

	pr_err("UPM6720 IIO device, rc=%d\n", rc);
	return rc;
}

static struct of_device_id upm6720_charger_match_table[] = {
    {
        .compatible = "unisemipower,upm6720-standalone",
        .data = &upm6720_mode_data[UPM6720_STDALONE],
    },
    {
        .compatible = "unisemipower,upm6720-master",
        .data = &upm6720_mode_data[UPM6720_MASTER],
    },
    {
        .compatible = "unisemipower,upm6720-slave",
        .data = &upm6720_mode_data[UPM6720_SLAVE],
    },
    {},
};

static int upm6720_charger_probe(struct i2c_client *client,
                    const struct i2c_device_id *id)
{
    struct upm6720 *upm;
    const struct of_device_id *match;
    struct device_node *node = client->dev.of_node;
    int ret;
    struct iio_dev *indio_dev;

	pr_err("upm6720 probe start\n");
    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*upm));
    if (!indio_dev) {
    	pr_err("iio alloc failed\n");
	return -ENOMEM;
    }

	upm = iio_priv(indio_dev);
	if (!upm) {
		pr_err("upm6720 is out of memory\n");
		return -ENOMEM;
	}
	upm->indio_dev = indio_dev;
#if 0
    upm = devm_kzalloc(&client->dev, sizeof(struct upm6720), GFP_KERNEL);
    if (!upm)
        return -ENOMEM;
#endif
    upm->dev = &client->dev;
    upm->client = client;

    mutex_init(&upm->i2c_rw_lock);
    mutex_init(&upm->data_lock);
    mutex_init(&upm->irq_complete);

    upm->resume_completed = true;
    upm->irq_waiting = false;

    ret = upm6720_detect_device(upm);
    if (ret) {
        pr_err("No upm6720 device found!\n");
        goto err_1;
    }

    i2c_set_clientdata(client, upm);
    //upm6720_create_device_node(&(client->dev));

    match = of_match_node(upm6720_charger_match_table, node);
    if (match == NULL) {
        pr_err("device tree match not found!\n");
        goto err_1;
    }

    upm6720_get_work_mode(upm, &upm->mode);

    if (upm->mode !=  *(int *)match->data) {
		pr_err("device operation mode mismatch with dts configuration\n");
		goto err_1;
	}

    ret = upm6720_parse_dt(upm, &client->dev);
    if (ret)
        goto err_1;

    ret = upm6720_init_device(upm);
    if (ret) {
        pr_err("Failed to init device\n");
        goto err_1;
    }

	ret = upm6720_en_surge_protect(upm, true);
	if (ret) {
		pr_err("en surge protect fail\n");
		goto err_1;
	}

    ret = upm6720_psy_register(upm);
    if (ret)
        goto err_2;

    ret = upm6720_irq_register(upm);
    if (ret)
        goto err_2;

    device_init_wakeup(upm->dev, 1);

    determine_initial_status(upm);
	
	INIT_DELAYED_WORK(&upm->monitor_work, upm6720_monitor_work);
	//schedule_delayed_work(&upm->monitor_work, msecs_to_jiffies(3*1000));

	upm6720_init_iio_psy(upm);		
	g_upm = upm;

    pr_err("upm6720 probe successfully, Part Num:%d\n!",
                upm->part_no);

    return 0;

err_2:
    power_supply_unregister(upm->fc2_psy);
err_1:
    mutex_destroy(&upm->i2c_rw_lock);
    mutex_destroy(&upm->data_lock);
    mutex_destroy(&upm->irq_complete);
    pr_err("upm6720 probe fail\n");
    devm_kfree(&client->dev, upm);
    return ret;
}

static inline bool is_device_suspended(struct upm6720 *upm)
{
    return !upm->resume_completed;
}

static int upm6720_suspend(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct upm6720 *upm = i2c_get_clientdata(client);

    mutex_lock(&upm->irq_complete);
    upm->resume_completed = false;
    mutex_unlock(&upm->irq_complete);
    pr_err("Suspend successfully!");

    return 0;
}

static int upm6720_suspend_noirq(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct upm6720 *upm = i2c_get_clientdata(client);

    if (upm->irq_waiting) {
        pr_err_ratelimited("Aborting suspend, an interrupt was detected while suspending\n");
        return -EBUSY;
    }
    return 0;
}

static int upm6720_resume(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct upm6720 *upm = i2c_get_clientdata(client);

    mutex_lock(&upm->irq_complete);
    upm->resume_completed = true;
    if (upm->irq_waiting) {
        upm->irq_disabled = false;
        enable_irq(client->irq);
        mutex_unlock(&upm->irq_complete);
        upm6720_charger_interrupt(client->irq, upm);
    } else {
        mutex_unlock(&upm->irq_complete);
    }

    power_supply_changed(upm->fc2_psy);
    pr_err("Resume successfully!");

    return 0;
}

static const struct dev_pm_ops upm6720_pm_ops = {
    .resume		= upm6720_resume,
    .suspend_noirq = upm6720_suspend_noirq,
    .suspend	= upm6720_suspend,
};

static int upm6720_charger_remove(struct i2c_client *client)
{
    struct upm6720 *upm = i2c_get_clientdata(client);

    upm6720_enable_adc(upm, false);
    power_supply_unregister(upm->fc2_psy);
    mutex_destroy(&upm->data_lock);
    mutex_destroy(&upm->irq_complete);

    return 0;
}

static void upm6720_charger_shutdown(struct i2c_client *client)
{
    struct upm6720 *upm = i2c_get_clientdata(client);

    upm6720_enable_adc(upm, false);
    mutex_destroy(&upm->i2c_rw_lock);
}

static struct i2c_driver upm6720_charger_driver = {
    .driver     = {
        .name   = "upm6720-charger",
        .owner  = THIS_MODULE,
        .of_match_table = upm6720_charger_match_table,
        .pm = &upm6720_pm_ops,
    },
    .probe      = upm6720_charger_probe,
    .remove     = upm6720_charger_remove,
    .shutdown   = upm6720_charger_shutdown,
};

module_i2c_driver(upm6720_charger_driver);

MODULE_DESCRIPTION("unisemipower upm6720 charge pump driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("unisemipower <lai.du@unisemipower.com>");
