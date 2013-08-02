/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/memory.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/krait-regulator.h>
#include <linux/msm_tsens.h>
#include <linux/msm_thermal.h>
#include <asm/mach/map.h>
#include <asm/hardware/gic.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <mach/board.h>
#include <mach/gpiomux.h>
#include <mach/msm_iomap.h>
#if defined(CONFIG_MOTOR_DRV_ISA1400)
	extern void vienna_motor_init(void);
#endif
#ifdef CONFIG_ION_MSM
#include <mach/ion.h>
#endif
#ifdef CONFIG_VIBETONZ
#include <linux/vibrator.h>
#endif
#include <mach/msm_memtypes.h>
#include <mach/msm_smd.h>
#include <mach/restart.h>
#include <mach/rpm-smd.h>
#ifdef CONFIG_SEC_DEBUG
#include <mach/sec_debug.h>
#endif
#include <mach/rpm-regulator-smd.h>
#include <mach/socinfo.h>
#include "board-dt.h"
#include "clock.h"
#include "devices.h"
#include "spm.h"
#include "modem_notifier.h"
#include "lpm_resources.h"
#include <linux/module.h>
#include "platsmp.h"

#ifdef CONFIG_USB_SWITCH_TSU6721
#include <linux/i2c/tsu6721.h>
#endif
#ifdef CONFIG_LEDS_MAX77803
#include <linux/leds-max77803.h>
#endif
#include <linux/mfd/max77803.h>
#include <linux/i2c-gpio.h>
#include <linux/i2c.h>
#ifdef CONFIG_VIDEO_MHL_V2
#include <linux/sii8240.h>
#endif
#ifdef CONFIG_BCM2079X_NFC_I2C
#include <linux/nfc/bcm2079x.h>
#endif
#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
#include <mach/tdmb_pdata.h>
#endif

#ifdef CONFIG_PROC_AVC
#include <linux/proc_avc.h>
#endif

#ifdef CONFIG_SEC_THERMISTOR
#include <mach/sec_thermistor.h>
#include <mach/msm8974-thermistor.h>
#endif

#ifdef CONFIG_REGULATOR_LP8720
#include <linux/regulator/lp8720.h>
#endif

#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
#define GPIO_TDMB_SPI_MOSI		8
#define GPIO_TDMB_SPI_MISO		9
#define GPIO_TDMB_SPI_CS		10
#define GPIO_TDMB_SPI_CLK		11
#define GPIO_TDMB_INT			73
#define GPIO_TDMB_ANT_DET		18
#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
#define GPIO_TDMB_EN			43
#define GPIO_TDMB_RST			41
#define GPIO_TDMB_EN_REV02		47
#define GPIO_TDMB_RST_REV02		48
#elif defined(CONFIG_MACH_HLTESKT) ||defined(CONFIG_MACH_HLTEKTT)\
	|| defined(CONFIG_MACH_HLTELGT)
#define GPIO_TDMB_EN			79
#endif
#endif

/* HDMI/MHL */
#define GPIO_MHL_RST		60
#define PM_GPIO_MHL_EN		486 /* PM_GIPO = 1(472) */
#define GPIO_MHL_SCL		52
#define GPIO_MHL_SDA		51
#define GPIO_MHL_WAKE_UP	96
#define GPIO_MHL_INT		31
#define MSM_MHL_I2C_BUS_ID	7

#define MSM_FSA9485_I2C_BUS_ID		15



#ifdef CONFIG_SENSORS_SSP
static struct regulator *vsensor_2p85, *vsensor_1p8;
static int __init sensor_hub_init(void)
{
	int ret;
	vsensor_2p85 = regulator_get(NULL, "8941_l18");
	if (IS_ERR(vsensor_2p85))
		pr_err("[SSP] could not get 8941_l18, %ld\n",
			PTR_ERR(vsensor_2p85));

	vsensor_1p8 = regulator_get(NULL, "8941_lvs1");
	if (IS_ERR(vsensor_1p8))
		pr_err("[SSP] could not get 8941_lvs1, %ld\n",
			PTR_ERR(vsensor_1p8));

	ret = regulator_enable(vsensor_2p85);
	if (ret)
		pr_err("%s: error enabling regulator 2p85\n", __func__);

	ret = regulator_enable(vsensor_1p8);
	if (ret)
		pr_err("%s: error enabling regulator 1p8\n", __func__);

	return 0;
}
#endif

#if defined(CONFIG_BATTERY_SAMSUNG)
extern void __init samsung_init_battery(void);
#endif

#ifdef CONFIG_USB_SWITCH_TSU6721
static BLOCKING_NOTIFIER_HEAD(acc_notifier);
int acc_register_notifier(struct notifier_block *nb)
{
	int ret;
	ret = blocking_notifier_chain_register(&acc_notifier, nb);
	return ret;
}

int acc_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&acc_notifier, nb);
}

static int acc_notify(int event)
{
	int ret;
	ret = blocking_notifier_call_chain(&acc_notifier, event, NULL);
	return ret;
}

void fsa9485_muic_mhl_notify(int attached)
{
	pr_info("MUIC attached:%d\n", attached);
	if (attached == TSU6721_ATTACHED) {
		/*MHL_On(1);*/ /* GPIO_LEVEL_HIGH */
		pr_info("MHL Attached !!\n");
#ifdef CONFIG_VIDEO_MHL_V2
		acc_notify(1);
#endif
	} else {
		/*MHL_On(0);*/ /* GPIO_LEVEL_LOW */
		pr_info("MHL Detached !!\n");
#ifdef CONFIG_VIDEO_MHL_V2
		acc_notify(0);
#endif
	}
}

static struct tsu6721_platform_data tsu6721_pdata = {
	.mhl_notify = fsa9485_muic_mhl_notify,
};

struct i2c_board_info micro_usb_i2c_devices_info[] __initdata = {
	{
		I2C_BOARD_INFO("tsu6721", 0x4A >> 1),
		.platform_data = &tsu6721_pdata,
		.irq = 0,
	},
};

#endif
#if defined(CONFIG_VIBETONZ) && defined(CONFIG_MOTOR_DRV_MAX77803)
struct max77803_haptic_platform_data max77803_haptic_pdata = {
	.max_timeout = 10000,
	.duty = 44000,
	.period = 44642,
	.reg2 = MOTOR_LRA | EXT_PWM | DIVIDER_128,
	.init_hw = NULL,
	.motor_en = NULL,
	.pwm_id = 1,
	.regulator_name = NULL,
};
#endif

#ifdef CONFIG_LEDS_MAX77803
struct max77803_led_platform_data max77803_led_pdata = {
	.num_leds = 2,

	.leds[0].name = "leds-sec1",
	.leds[0].id = MAX77803_FLASH_LED_1,
	.leds[0].timer = MAX77803_FLASH_TIME_187P5MS,
	.leds[0].timer_mode = MAX77803_TIMER_MODE_MAX_TIMER,
	.leds[0].cntrl_mode = MAX77803_LED_CTRL_BY_FLASHSTB,
	.leds[0].brightness = 0x3D,

	.leds[1].name = "torch-sec1",
	.leds[1].id = MAX77803_TORCH_LED_1,
	.leds[1].cntrl_mode = MAX77803_LED_CTRL_BY_FLASHSTB,
	.leds[1].brightness = 0x06,
};
#endif

struct i2c_registry {
	int					   bus;
	struct i2c_board_info *info;
	int					   len;
};


#ifdef CONFIG_REGULATOR_LP8720
#define MSM_LP8720_I2C_BUS_ID   25
#define GPIO_SUBPMIC_SDA	29
#define GPIO_SUBPMIC_SCL	30
#ifdef CONFIG_MACH_MONTBLANC
#define GPIO_SUBPMIC_EN	561  // 535 + 26 = 561
#else
#define GPIO_SUBPMIC_EN	26  // for temp, not use, PM_GPIO
#endif

#define LP8720_VREG_CONSUMERS(_id) \
	static struct regulator_consumer_supply lp8720_vreg_consumers_##_id[]

#define LP8720_VREG_INIT(_id, _min_uV, _max_uV, _always_on) \
	static struct regulator_init_data lp8720_##_id##_init_data = { \
		.constraints = { \
			.min_uV			= _min_uV, \
			.max_uV			= _max_uV, \
			.apply_uV		= 1, \
			.always_on		= _always_on, \
			.valid_modes_mask = REGULATOR_MODE_NORMAL, \
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | \
							REGULATOR_CHANGE_STATUS, \
		}, \
		.num_consumer_supplies = ARRAY_SIZE(lp8720_vreg_consumers_##_id), \
		.consumer_supplies = lp8720_vreg_consumers_##_id, \
	}

#define LP8720_VREG_INIT_DATA(_id) \
	(struct regulator_init_data *)&lp8720_##_id##_init_data

LP8720_VREG_CONSUMERS(LDO1) = {
	REGULATOR_SUPPLY("lp8720_ldo1", 	NULL),
};

LP8720_VREG_CONSUMERS(LDO2) = {
	REGULATOR_SUPPLY("lp8720_ldo2", 	NULL),
	REGULATOR_SUPPLY("touchkey_ldo", NULL),		
};

LP8720_VREG_CONSUMERS(LDO3) = {
	REGULATOR_SUPPLY("lp8720_ldo3", 	NULL),
};

LP8720_VREG_CONSUMERS(LDO4) = {
	REGULATOR_SUPPLY("lp8720_ldo4", 	NULL),
};

LP8720_VREG_CONSUMERS(LDO5) = {
	REGULATOR_SUPPLY("lp8720_ldo5", 	NULL),
};

LP8720_VREG_CONSUMERS(BUCK1) = {
	REGULATOR_SUPPLY("lp8720_buck1", 	NULL),
};

LP8720_VREG_INIT(LDO1, 1200000, 3300000, 0);
LP8720_VREG_INIT(LDO2, 1200000, 3300000, 0);
LP8720_VREG_INIT(LDO3, 1200000, 3300000, 0);
LP8720_VREG_INIT(LDO4, 800000, 2850000, 0);
LP8720_VREG_INIT(LDO5, 3000000, 3000000, 1);
LP8720_VREG_INIT(BUCK1, 1800000, 1800000, 1);

static struct lp8720_regulator_subdev lp8720_regulators[]= {
	{LP8720_LDO1, LP8720_VREG_INIT_DATA(LDO1)},
	{LP8720_LDO2, LP8720_VREG_INIT_DATA(LDO2)},
	{LP8720_LDO3, LP8720_VREG_INIT_DATA(LDO3)},
	{LP8720_LDO4, LP8720_VREG_INIT_DATA(LDO4)},
	{LP8720_LDO5, LP8720_VREG_INIT_DATA(LDO5)},
	{LP8720_BUCK_V1, LP8720_VREG_INIT_DATA(BUCK1)},
};

static struct i2c_gpio_platform_data lp8720_i2c_gpio_data = {
	.sda_pin = GPIO_SUBPMIC_SDA,
	.scl_pin = GPIO_SUBPMIC_SCL,
	.udelay = 5,
	.sda_is_open_drain = 0,
	.scl_is_open_drain = 0,
	.scl_is_output_only = 0,
};

static struct platform_device lp8720_i2c_gpio_device = {
	.name = "i2c-gpio",
	.id = MSM_LP8720_I2C_BUS_ID,
	.dev = {
		.platform_data = &lp8720_i2c_gpio_data,
	},
};

static struct lp8720_platform_data lp8720_pmic_pdata = {
	.name = "lp8720-en",
	.en_pin = GPIO_SUBPMIC_EN,
	.num_regulators = ARRAY_SIZE(lp8720_regulators),
	.regulators = lp8720_regulators,
};

static struct i2c_board_info lp8720_pmic_info[] __initdata = {
	{
		I2C_BOARD_INFO("lp8720", 0x7d),
		.platform_data = &lp8720_pmic_pdata,
	},
};
#endif /* CONFIG_REGULATOR_LP8720 */

static struct i2c_registry msm8960_i2c_devices[] __initdata = {
#ifdef CONFIG_USB_SWITCH_TSU6721
		{
			MSM_FSA9485_I2C_BUS_ID,
			micro_usb_i2c_devices_info,
			ARRAY_SIZE(micro_usb_i2c_devices_info),
		},
#endif
#ifdef CONFIG_REGULATOR_LP8720
	{
		MSM_LP8720_I2C_BUS_ID,
		lp8720_pmic_info,
		ARRAY_SIZE(lp8720_pmic_info),
	},
#endif
};

static struct memtype_reserve msm8974_reserve_table[] __initdata = {
	[MEMTYPE_SMI] = {
	},
	[MEMTYPE_EBI0] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
	[MEMTYPE_EBI1] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
};

static int msm8974_paddr_to_memtype(phys_addr_t paddr)
{
	return MEMTYPE_EBI1;
}

static struct reserve_info msm8974_reserve_info __initdata = {
	.memtype_reserve_table = msm8974_reserve_table,
	.paddr_to_memtype = msm8974_paddr_to_memtype,
};

void __init msm_8974_reserve(void)
{
	reserve_info = &msm8974_reserve_info;
	of_scan_flat_dt(dt_scan_for_memory_reserve, msm8974_reserve_table);
	msm_reserve();
}

static void __init msm8974_early_memory(void)
{
	reserve_info = &msm8974_reserve_info;
	of_scan_flat_dt(dt_scan_for_memory_hole, msm8974_reserve_table);
}

static struct platform_device *common_devices[] __initdata = {
#ifdef CONFIG_SEC_THERMISTOR
        &sec_device_thermistor,
#endif
#ifdef CONFIG_REGULATOR_LP8720
	&lp8720_i2c_gpio_device,
#endif

};

#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
static void tdmb_gpio_init(void)
{
	unsigned int gpio_tdmb_en;
#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
	unsigned int gpio_tdmb_rst;
	if(system_rev < 3) {
		gpio_tdmb_en = GPIO_TDMB_EN_REV02;
		gpio_tdmb_rst = GPIO_TDMB_RST_REV02;
	} else {
		gpio_tdmb_en = GPIO_TDMB_EN;
		gpio_tdmb_rst = GPIO_TDMB_RST;
	}
#elif defined(CONFIG_MACH_HLTESKT) ||defined(CONFIG_MACH_HLTEKTT)\
	|| defined(CONFIG_MACH_HLTELGT)
	gpio_tdmb_en = GPIO_TDMB_EN;
#endif

	gpio_tlmm_config(GPIO_CFG(gpio_tdmb_en, GPIOMUX_FUNC_GPIO,
		GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		GPIO_CFG_ENABLE);
	gpio_set_value(gpio_tdmb_en, 0);

#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
	gpio_tlmm_config(GPIO_CFG(gpio_tdmb_rst, GPIOMUX_FUNC_GPIO,
		GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		GPIO_CFG_ENABLE);
	gpio_set_value(gpio_tdmb_rst, 0);
#endif

	gpio_tlmm_config(GPIO_CFG(GPIO_TDMB_INT, GPIOMUX_FUNC_GPIO,
		GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		GPIO_CFG_ENABLE);
}
static void tdmb_gpio_on(void)
{
	unsigned int gpio_tdmb_en;
#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
	unsigned int gpio_tdmb_rst;
	if(system_rev < 3) {
		gpio_tdmb_en = GPIO_TDMB_EN_REV02;
		gpio_tdmb_rst = GPIO_TDMB_RST_REV02;
	} else {
		gpio_tdmb_en = GPIO_TDMB_EN;
		gpio_tdmb_rst = GPIO_TDMB_RST;
	}
#elif defined(CONFIG_MACH_HLTESKT) ||defined(CONFIG_MACH_HLTEKTT)\
	|| defined(CONFIG_MACH_HLTELGT)
	gpio_tdmb_en = GPIO_TDMB_EN;
#endif
	printk(KERN_DEBUG "tdmb_gpio_on\n");

	gpio_tlmm_config(GPIO_CFG(gpio_tdmb_en, GPIOMUX_FUNC_GPIO,
		GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),
		GPIO_CFG_ENABLE);
#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
	gpio_tlmm_config(GPIO_CFG(gpio_tdmb_rst, GPIOMUX_FUNC_GPIO,
		GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),
		GPIO_CFG_ENABLE);
#endif
	gpio_tlmm_config(GPIO_CFG(GPIO_TDMB_INT, GPIOMUX_FUNC_GPIO,
		GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),
		GPIO_CFG_ENABLE);
	gpio_set_value(gpio_tdmb_en, 1);
	usleep_range(20000, 20000);
#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
	gpio_set_value(gpio_tdmb_rst, 0);
	usleep_range(2000, 2000);
	gpio_set_value(gpio_tdmb_rst, 1);
	usleep_range(10000, 10000);
#endif
}

static void tdmb_gpio_off(void)
{
	unsigned int gpio_tdmb_en;
#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
	unsigned int gpio_tdmb_rst;
	if(system_rev < 3) {
		gpio_tdmb_en = GPIO_TDMB_EN_REV02;
		gpio_tdmb_rst = GPIO_TDMB_RST_REV02;
	} else {
		gpio_tdmb_en = GPIO_TDMB_EN;
		gpio_tdmb_rst = GPIO_TDMB_RST;
	}
#elif defined(CONFIG_MACH_HLTESKT) ||defined(CONFIG_MACH_HLTEKTT)\
	|| defined(CONFIG_MACH_HLTELGT)
	gpio_tdmb_en = GPIO_TDMB_EN;
#endif

	printk(KERN_DEBUG "tdmb_gpio_off\n");

	gpio_set_value(gpio_tdmb_en, 0);
	usleep_range(1000, 1000);
#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
	gpio_set_value(gpio_tdmb_rst, 0);
#endif
	gpio_tlmm_config(GPIO_CFG(gpio_tdmb_en, GPIOMUX_FUNC_GPIO,
		GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		GPIO_CFG_ENABLE);
#if defined(CONFIG_MACH_KS01SKT) || defined(CONFIG_MACH_KS01KTT)\
	|| defined(CONFIG_MACH_KS01LGT)
	gpio_tlmm_config(GPIO_CFG(gpio_tdmb_rst, GPIOMUX_FUNC_GPIO,
		GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		GPIO_CFG_ENABLE);
#endif
	gpio_tlmm_config(GPIO_CFG(GPIO_TDMB_INT, GPIOMUX_FUNC_GPIO,
		GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		GPIO_CFG_ENABLE);
}

static struct tdmb_platform_data tdmb_pdata = {
	.gpio_on = tdmb_gpio_on,
	.gpio_off = tdmb_gpio_off,
};

static struct platform_device tdmb_device = {
	.name			= "tdmb",
	.id				= -1,
	.dev			= {
		.platform_data = &tdmb_pdata,
	},
};

static int __init tdmb_dev_init(void)
{
#if defined(CONFIG_TDMB_ANT_DET)
	gpio_tlmm_config(GPIO_CFG(GPIO_TDMB_ANT_DET, GPIOMUX_FUNC_GPIO,
		GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),
		GPIO_CFG_ENABLE);

	tdmb_pdata.gpio_ant_det = GPIO_TDMB_ANT_DET;
	tdmb_pdata.irq_ant_det = gpio_to_irq(GPIO_TDMB_ANT_DET);
#endif

	tdmb_pdata.irq = gpio_to_irq(GPIO_TDMB_INT);
	tdmb_pdata.system_rev = system_rev;
	platform_device_register(&tdmb_device);

	tdmb_gpio_init();

	return 0;
}
#endif

struct class *sec_class;
EXPORT_SYMBOL(sec_class);

static void samsung_sys_class_init(void)
{
	pr_info("samsung sys class init.\n");

	sec_class = class_create(THIS_MODULE, "sec");

	if (IS_ERR(sec_class)) {
		pr_err("Failed to create class(sec)!\n");
		return;
	}

	pr_info("samsung sys class end.\n");
};

/*
 * Used to satisfy dependencies for devices that need to be
 * run early or in a particular order. Most likely your device doesn't fall
 * into this category, and thus the driver should not be added here. The
 * EPROBE_DEFER can satisfy most dependency problems.
 */
void __init msm8974_add_drivers(void)
{
	msm_init_modem_notifier_list();
	msm_smd_init();
	msm_rpm_driver_init();
	msm_lpmrs_module_init();
	rpm_regulator_smd_driver_init();
	msm_spm_device_init();
	krait_power_init();
	if (of_board_is_rumi())
		msm_clock_init(&msm8974_rumi_clock_init_data);
	else
		msm_clock_init(&msm8974_clock_init_data);
	tsens_tm_init_driver();
	msm_thermal_device_init();
}

static struct of_dev_auxdata msm8974_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("qcom,hsusb-otg", 0xF9A55000, \
			"msm_otg", NULL),
	OF_DEV_AUXDATA("qcom,ehci-host", 0xF9A55000, \
			"msm_ehci_host", NULL),
	OF_DEV_AUXDATA("qcom,dwc-usb3-msm", 0xF9200000, \
			"msm_dwc3", NULL),
	OF_DEV_AUXDATA("qcom,usb-bam-msm", 0xF9304000, \
			"usb_bam", NULL),
	OF_DEV_AUXDATA("qcom,spi-qup-v2", 0xF9924000, \
			"spi_qsd.1", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9824000, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF98A4000, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9864000, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF98E4000, \
			"msm_sdcc.4", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9824900, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF98A4900, \
			"msm_sdcc.2", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF9864900, \
			"msm_sdcc.3", NULL),
	OF_DEV_AUXDATA("qcom,sdhci-msm", 0xF98E4900, \
			"msm_sdcc.4", NULL),
	OF_DEV_AUXDATA("qcom,msm-rng", 0xF9BFF000, \
			"msm_rng", NULL),
	OF_DEV_AUXDATA("qcom,qseecom", 0xFE806000, \
			"qseecom", NULL),
	OF_DEV_AUXDATA("qcom,mdss_mdp", 0xFD900000, "mdp.0", NULL),
	OF_DEV_AUXDATA("qcom,msm-tsens", 0xFC4A8000, \
			"msm-tsens", NULL),
	OF_DEV_AUXDATA("qcom,qcedev", 0xFD440000, \
			"qcedev.0", NULL),
	OF_DEV_AUXDATA("qcom,qcrypto", 0xFD440000, \
			"qcrypto.0", NULL),
	OF_DEV_AUXDATA("qcom,hsic-host", 0xF9A00000, \
			"msm_hsic_host", NULL),
	{}
};

static void __init msm8974_map_io(void)
{
	msm_map_8974_io();
}

static void __init register_i2c_devices(void)
{
	int i;

	/* Run the array and install devices as appropriate */
	for (i = 0; i < ARRAY_SIZE(msm8960_i2c_devices); ++i) {
		i2c_register_board_info(msm8960_i2c_devices[i].bus,
					msm8960_i2c_devices[i].info,
					msm8960_i2c_devices[i].len);
	}
}

#if defined(CONFIG_KEYBOARD_MATRIX) && defined(CONFIG_MACH_MONTBLANC)
#include "board-montblanc-keypad.c"
#endif

void __init msm8974_init(void)
{
	struct of_dev_auxdata *adata = msm8974_auxdata_lookup;

#ifdef CONFIG_SEC_DEBUG
	sec_debug_init();
#endif

#ifdef CONFIG_PROC_AVC
	sec_avc_log_init();
#endif

	if (socinfo_init() < 0)
		pr_err("%s: socinfo_init() failed\n", __func__);

	msm_8974_init_gpiomux();
	regulator_has_full_constraints();
	of_platform_populate(NULL, of_default_bus_match_table, adata, NULL);

	samsung_sys_class_init();

	msm8974_add_drivers();

	platform_add_devices(common_devices, ARRAY_SIZE(common_devices));
	register_i2c_devices();

#ifdef CONFIG_SENSORS_SSP
	sensor_hub_init();
#endif
#if defined(CONFIG_KEYBOARD_MATRIX) && defined(CONFIG_MACH_MONTBLANC)
	platform_device_register(&folder_keypad_device);
#endif
#if defined(CONFIG_BATTERY_SAMSUNG)
	samsung_init_battery();
#endif
#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
	tdmb_dev_init();
#endif
#if defined(CONFIG_BT_BCM4335) || defined(CONFIG_BT_BCM4339)
	msm8974_bt_init();
#endif
#if defined(CONFIG_BCM4335) || defined(CONFIG_BCM4335_MODULE) || defined(CONFIG_BCM4339) || defined(CONFIG_BCM4339_MODULE)
	brcm_wlan_init();
#endif

#if defined (CONFIG_MOTOR_DRV_ISA1400)
	vienna_motor_init();
#endif
}

void __init msm8974_init_very_early(void)
{
	msm8974_early_memory();
}

static const char *msm8974_dt_match[] __initconst = {
	"qcom,msm8974",
	"qcom,apq8074",
	NULL
};

DT_MACHINE_START(MSM8974_DT, "Qualcomm MSM 8974 (Flattened Device Tree)")
	.map_io = msm8974_map_io,
	.init_irq = msm_dt_init_irq,
	.init_machine = msm8974_init,
	.handle_irq = gic_handle_irq,
	.timer = &msm_dt_timer,
	.dt_compat = msm8974_dt_match,
	.reserve = msm_8974_reserve,
	.init_very_early = msm8974_init_very_early,
	.restart = msm_restart,
	.smp = &msm8974_smp_ops,
MACHINE_END
