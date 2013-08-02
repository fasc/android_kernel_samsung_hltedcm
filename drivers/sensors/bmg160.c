/*
 * Copyright (C) 2013 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>

#include "sensors_core.h"
#include "bmg160_reg.h"

/* It's for HW issue in Rev 0.4 */
#define EXECPTION_FOR_I2CFAIL

#define VENDOR_NAME                        "BOSCH"
#define MODEL_NAME                         "BMG160"
#define MODULE_NAME                        "gyro_sensor"

#define CALIBRATION_FILE_PATH              "/efs/gyro_calibration_data"
#define CALIBRATION_DATA_AMOUNT            20

#define BMG160_DEFAULT_DELAY               200
#define	BMG160_CHIP_ID                     0x0F

#define BMG160_TOP_UPPER_RIGHT             0
#define BMG160_TOP_LOWER_RIGHT             1
#define BMG160_TOP_LOWER_LEFT              2
#define BMG160_TOP_UPPER_LEFT              3
#define BMG160_BOTTOM_UPPER_RIGHT          4
#define BMG160_BOTTOM_LOWER_RIGHT          5
#define BMG160_BOTTOM_LOWER_LEFT           6
#define BMG160_BOTTOM_UPPER_LEFT           7

struct bmg160_v {
	union {
		s16 v[3];
		struct {
			s16 x;
			s16 y;
			s16 z;
		};
	};
};

struct bmg160_p {
	struct i2c_client *client;
	struct input_dev *input;
	struct delayed_work work;
	struct device *factory_device;
	struct bmg160_v gyrodata;
	struct bmg160_v caldata;

	atomic_t delay;
	atomic_t enable;

	u32 chip_pos;
	int gyro_dps;
	int gyro_int;
	int gyro_drdy;
#ifdef EXECPTION_FOR_I2CFAIL
	int i2cfail_cnt;
#endif

};

static int bmg160_open_calibration(struct bmg160_p *);

static int bmg160_smbus_read_byte_block(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data, unsigned char len)
{
	s32 dummy;

	dummy = i2c_smbus_read_i2c_block_data(client, reg_addr, len, data);
	if (dummy < 0) {
		pr_err("[SENSOR]: %s - i2c bus read error %d\n",
			__func__, dummy);
		return -EIO;
	}
	return 0;
}

static int bmg160_smbus_read_byte(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *buf)
{
	s32 dummy;

	dummy = i2c_smbus_read_byte_data(client, reg_addr);
	if (dummy < 0) {
		pr_err("[SENSOR]: %s - i2c bus read error %d\n",
			__func__, dummy);
		return -EIO;
	}
	*buf = dummy & 0x000000ff;

	return 0;
}

static int bmg160_smbus_write_byte(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *buf)
{
	s32 dummy;

	dummy = i2c_smbus_write_byte_data(client, reg_addr, *buf);
	if (dummy < 0) {
		pr_err("[SENSOR]: %s - i2c bus read error %d\n",
			__func__, dummy);
		return -EIO;
	}
	return 0;
}

static int bmg160_get_bw(struct bmg160_p *data, unsigned char *bandwidth)
{
	int ret;
	unsigned char temp;

	ret = bmg160_smbus_read_byte(data->client, BMG160_BW_ADDR__REG, &temp);
	*bandwidth = BMG160_GET_BITSLICE(temp, BMG160_BW_ADDR);

	return ret;
}

static int bmg160_get_autosleepdur(struct bmg160_p *data,
		unsigned char *duration)
{
	int ret = 0;
	unsigned char temp;

	ret = bmg160_smbus_read_byte(data->client,
		BMG160_MODE_LPM2_ADDR_AUTOSLEEPDUR__REG, &temp);

	*duration = BMG160_GET_BITSLICE(temp,
			BMG160_MODE_LPM2_ADDR_AUTOSLEEPDUR);

	return ret;
}

static int bmg160_set_autosleepdur(struct bmg160_p *data,
		unsigned char duration, unsigned char bandwith)
{
	int ret = 0;
	unsigned char temp, autosleepduration;

	ret = bmg160_smbus_read_byte(data->client,
		BMG160_MODE_LPM2_ADDR_AUTOSLEEPDUR__REG, &temp);

	switch (bandwith) {
	case BMG160_No_Filter:
		if (duration > BMG160_4ms_AutoSleepDur)
			autosleepduration = duration;
		else
			autosleepduration = BMG160_4ms_AutoSleepDur;
		break;
	case BMG160_BW_230Hz:
		if (duration > BMG160_4ms_AutoSleepDur)
			autosleepduration = duration;
		else
			autosleepduration = BMG160_4ms_AutoSleepDur;
		break;
	case BMG160_BW_116Hz:
		if (duration > BMG160_4ms_AutoSleepDur)
			autosleepduration = duration;
		else
			autosleepduration = BMG160_4ms_AutoSleepDur;
		break;
	case BMG160_BW_47Hz:
		if (duration > BMG160_5ms_AutoSleepDur)
			autosleepduration = duration;
		else
			autosleepduration = BMG160_5ms_AutoSleepDur;
		break;
	case BMG160_BW_23Hz:
		if (duration > BMG160_10ms_AutoSleepDur)
			autosleepduration = duration;
		else
		autosleepduration = BMG160_10ms_AutoSleepDur;
		break;
	case BMG160_BW_12Hz:
		if (duration > BMG160_20ms_AutoSleepDur)
			autosleepduration = duration;
		else
		autosleepduration = BMG160_20ms_AutoSleepDur;
		break;
	case BMG160_BW_64Hz:
		if (duration > BMG160_10ms_AutoSleepDur)
			autosleepduration = duration;
		else
			autosleepduration = BMG160_10ms_AutoSleepDur;
		break;
	case BMG160_BW_32Hz:
		if (duration > BMG160_20ms_AutoSleepDur)
			autosleepduration = duration;
		else
			autosleepduration = BMG160_20ms_AutoSleepDur;
		break;
	default:
		autosleepduration = BMG160_No_AutoSleepDur;
	}

	temp = BMG160_SET_BITSLICE(temp, BMG160_MODE_LPM2_ADDR_AUTOSLEEPDUR,
			autosleepduration);

	ret += bmg160_smbus_write_byte(data->client,
			BMG160_MODE_LPM2_ADDR_AUTOSLEEPDUR__REG, &temp);

	return ret;
}

static int bmg160_get_mode(struct bmg160_p *data, unsigned char *mode)
{
	int ret = 0;
	unsigned char buf1 = 0;
	unsigned char buf2 = 0;
	unsigned char buf3 = 0;

	ret = bmg160_smbus_read_byte(data->client, BMG160_MODE_LPM1_ADDR, &buf1);
	ret += bmg160_smbus_read_byte(data->client,
			BMG160_MODE_LPM2_ADDR, &buf2);

	buf1  = (buf1 & 0xA0) >> 5;
	buf3  = (buf2 & 0x40) >> 6;
	buf2  = (buf2 & 0x80) >> 7;

	if (buf3 == 0x01)
		*mode = BMG160_MODE_ADVANCEDPOWERSAVING;
	else if ((buf1 == 0x00) && (buf2 == 0x00))
		*mode = BMG160_MODE_NORMAL;
	else if ((buf1 == 0x01) || (buf1 == 0x05))
		*mode = BMG160_MODE_DEEPSUSPEND;
	else if ((buf1 == 0x04) && (buf2 == 0x00))
		*mode = BMG160_MODE_SUSPEND;
	else if ((buf1 == 0x04) && (buf2 == 0x01))
		*mode = BMG160_MODE_FASTPOWERUP;

	return ret;
}

static int bmg160_set_range(struct bmg160_p *data, unsigned char range)
{
	int ret = 0;
	unsigned char temp;

	ret = bmg160_smbus_read_byte(data->client,
			BMG160_RANGE_ADDR_RANGE__REG, &temp);
	temp = BMG160_SET_BITSLICE(temp, BMG160_RANGE_ADDR_RANGE, range);
	ret += bmg160_smbus_write_byte(data->client,
			BMG160_RANGE_ADDR_RANGE__REG, &temp);

	return ret;
}

static int bmg160_set_bw(struct bmg160_p *data, unsigned char bandwidth)
{
	int ret = 0;
	unsigned char temp, autosleepduration, mode = 0;

	bmg160_get_mode(data, &mode);
	if (mode == BMG160_MODE_ADVANCEDPOWERSAVING) {
		bmg160_get_autosleepdur(data, &autosleepduration);
		bmg160_set_autosleepdur(data, autosleepduration, bandwidth);
	}

	ret = bmg160_smbus_read_byte(data->client, BMG160_BW_ADDR__REG, &temp);
	temp = BMG160_SET_BITSLICE(temp, BMG160_BW_ADDR, bandwidth);
	ret += bmg160_smbus_write_byte(data->client,
		BMG160_BW_ADDR__REG, &temp);

	return ret;
}

static int bmg160_set_mode(struct bmg160_p *data, unsigned char mode)
{
	int ret = 0;
	unsigned char buf1, buf2, buf3;
	unsigned char autosleepduration;
	unsigned char v_bw_u8r;

	ret = bmg160_smbus_read_byte(data->client,
			BMG160_MODE_LPM1_ADDR, &buf1);
	ret += bmg160_smbus_read_byte(data->client,
			BMG160_MODE_LPM2_ADDR, &buf2);

	switch (mode) {
	case BMG160_MODE_NORMAL:
		buf1 = BMG160_SET_BITSLICE(buf1, BMG160_MODE_LPM1, 0);
		buf2 = BMG160_SET_BITSLICE(buf2,
			BMG160_MODE_LPM2_ADDR_FAST_POWERUP, 0);
		buf3 = BMG160_SET_BITSLICE(buf2,
			BMG160_MODE_LPM2_ADDR_ADV_POWERSAVING, 0);
		ret += bmg160_smbus_write_byte(data->client,
				BMG160_MODE_LPM1_ADDR, &buf1);
		mdelay(1);
		ret += bmg160_smbus_write_byte(data->client,
				BMG160_MODE_LPM2_ADDR, &buf3);
		break;
	case BMG160_MODE_DEEPSUSPEND:
		buf1 = BMG160_SET_BITSLICE(buf1, BMG160_MODE_LPM1, 1);
		buf2 = BMG160_SET_BITSLICE(buf2,
			BMG160_MODE_LPM2_ADDR_FAST_POWERUP, 0);
		buf3 = BMG160_SET_BITSLICE(buf2,
			BMG160_MODE_LPM2_ADDR_ADV_POWERSAVING, 0);
		ret += bmg160_smbus_write_byte(data->client,
				BMG160_MODE_LPM1_ADDR, &buf1);
		mdelay(1);
		ret += bmg160_smbus_write_byte(data->client,
				BMG160_MODE_LPM2_ADDR, &buf3);
		break;
	case BMG160_MODE_SUSPEND:
		buf1 = BMG160_SET_BITSLICE(buf1, BMG160_MODE_LPM1, 4);
		buf2 = BMG160_SET_BITSLICE(buf2,
			BMG160_MODE_LPM2_ADDR_FAST_POWERUP, 0);
		buf3 = BMG160_SET_BITSLICE(buf2,
			BMG160_MODE_LPM2_ADDR_ADV_POWERSAVING, 0);
		ret += bmg160_smbus_write_byte(data->client,
				BMG160_MODE_LPM1_ADDR, &buf1);
		mdelay(1);
		ret += bmg160_smbus_write_byte(data->client,
				BMG160_MODE_LPM2_ADDR, &buf3);
		break;
	case BMG160_MODE_FASTPOWERUP:
		buf1 = BMG160_SET_BITSLICE(buf1, BMG160_MODE_LPM1, 4);
		buf2 = BMG160_SET_BITSLICE(buf2,
			BMG160_MODE_LPM2_ADDR_FAST_POWERUP, 1);
		buf3 = BMG160_SET_BITSLICE(buf2,
			BMG160_MODE_LPM2_ADDR_ADV_POWERSAVING, 0);
		ret += bmg160_smbus_write_byte(data->client,
				BMG160_MODE_LPM1_ADDR, &buf1);
		mdelay(1);
		ret += bmg160_smbus_write_byte(data->client,
				BMG160_MODE_LPM2_ADDR, &buf3);
		break;
	case BMG160_MODE_ADVANCEDPOWERSAVING:
		/* Configuring the proper settings for auto
		sleep duration */
		bmg160_get_bw(data, &v_bw_u8r);
		bmg160_get_autosleepdur(data, &autosleepduration);
		bmg160_set_autosleepdur(data, autosleepduration, v_bw_u8r);
		ret += bmg160_smbus_read_byte(data->client,
				BMG160_MODE_LPM2_ADDR, &buf2);
		/* Configuring the advanced power saving mode*/
		buf1 = BMG160_SET_BITSLICE(buf1, BMG160_MODE_LPM1, 0);
		buf2 = BMG160_SET_BITSLICE(buf2,
			BMG160_MODE_LPM2_ADDR_FAST_POWERUP, 0);
		buf3 = BMG160_SET_BITSLICE(buf2,
			BMG160_MODE_LPM2_ADDR_ADV_POWERSAVING, 1);
		ret += bmg160_smbus_write_byte(data->client,
				BMG160_MODE_LPM1_ADDR, &buf1);
		mdelay(1);
		ret += bmg160_smbus_write_byte(data->client,
				BMG160_MODE_LPM2_ADDR, &buf3);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int bmg160_read_gyro_xyz(struct bmg160_p *data, struct bmg160_v *gyro)
{
	int ret = 0;
	unsigned char temp[6];

	ret = bmg160_smbus_read_byte_block(data->client,
			BMG160_RATE_X_LSB_VALUEX__REG, temp, 6);

	temp[0] = BMG160_GET_BITSLICE(temp[0], BMG160_RATE_X_LSB_VALUEX);
	gyro->x = (short)((((short)((signed char)temp[1])) << 8) | (temp[0]));

	temp[2] = BMG160_GET_BITSLICE(temp[2], BMG160_RATE_Y_LSB_VALUEY);
	gyro->y = (short)((((short)((signed char)temp[3])) << 8) | (temp[2]));

	temp[4] = BMG160_GET_BITSLICE(temp[4], BMG160_RATE_Z_LSB_VALUEZ);
	gyro->z = (short)((((short)((signed char)temp[5])) << 8) | (temp[4]));

	remap_sensor_data(gyro->v, data->chip_pos);

	if (data->gyro_dps == BMG160_RANGE_250DPS) {
		gyro->x = (gyro->x >> 1) - data->caldata.x;
		gyro->y = (gyro->y >> 1) - data->caldata.y;
		gyro->z = (gyro->z >> 1) - data->caldata.z;
	} else if (data->gyro_dps == BMG160_RANGE_2000DPS) {
		gyro->x = (gyro->x << 2) - data->caldata.x;
		gyro->y = (gyro->y << 2) - data->caldata.y;
		gyro->z = (gyro->z << 2) - data->caldata.z;
	} else {
		gyro->x = gyro->x - data->caldata.x;
		gyro->y = gyro->y - data->caldata.y;
		gyro->z = gyro->z - data->caldata.z;
	}

	return ret;
}

static void bmg160_work_func(struct work_struct *work)
{
	struct bmg160_v gyro;
	struct bmg160_p *data = container_of((struct delayed_work *)work,
			struct bmg160_p, work);
	unsigned long delay = msecs_to_jiffies(atomic_read(&data->delay));

#ifdef EXECPTION_FOR_I2CFAIL
	int ret;

	ret = bmg160_read_gyro_xyz(data, &gyro);
	if (ret < 0)
		data->i2cfail_cnt++;
	if (data->i2cfail_cnt > 5)
		return;
#else
	bmg160_read_gyro_xyz(data, &gyro);
#endif

	input_report_rel(data->input, REL_RX, gyro.x - data->caldata.x);
	input_report_rel(data->input, REL_RY, gyro.y - data->caldata.y);
	input_report_rel(data->input, REL_RZ, gyro.z - data->caldata.z);
	input_sync(data->input);
	data->gyrodata = gyro;

	schedule_delayed_work(&data->work, delay);
}

static void bmg160_set_enable(struct bmg160_p *data, int enable)
{
	int pre_enable = atomic_read(&data->enable);

	if (enable) {
		if (pre_enable == 0) {
			bmg160_open_calibration(data);
			bmg160_set_mode(data, BMG160_MODE_NORMAL);
			schedule_delayed_work(&data->work,
				msecs_to_jiffies(atomic_read(&data->delay)));
			atomic_set(&data->enable, 1);
		}
	} else {
		if (pre_enable == 1) {
			bmg160_set_mode(data, BMG160_MODE_SUSPEND);
			cancel_delayed_work_sync(&data->work);
			atomic_set(&data->enable, 0);
		}
	}
}

static ssize_t bmg160_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bmg160_p *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&data->enable));
}

static ssize_t bmg160_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	u8 enable;
	int ret;
	struct bmg160_p *data = dev_get_drvdata(dev);

	ret = kstrtou8(buf, 2, &enable);
	if (ret) {
		pr_err("[SENSOR]: %s - Invalid Argument\n", __func__);
		return ret;
	}

	pr_info("[SENSOR]: %s - new_value = %u\n", __func__, enable);
	if ((enable == 0) || (enable == 1))
		bmg160_set_enable(data, (int)enable);

	return size;
}

static ssize_t bmg160_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bmg160_p *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&data->delay));
}

static ssize_t bmg160_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int ret;
	int64_t delay;
	struct bmg160_p *data = dev_get_drvdata(dev);

	ret = kstrtoll(buf, 10, &delay);
	if (ret) {
		pr_err("[SENSOR]: %s - Invalid Argument\n", __func__);
		return ret;
	}

	atomic_set(&data->delay, (unsigned int)delay);
	pr_info("[SENSOR]: %s - poll_delay = %lld\n", __func__, delay);

	return size;
}

static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
		bmg160_delay_show, bmg160_delay_store);
static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
		bmg160_enable_show, bmg160_enable_store);

static struct attribute *bmg160_attributes[] = {
	&dev_attr_poll_delay.attr,
	&dev_attr_enable.attr,
	NULL
};

static struct attribute_group bmg160_attribute_group = {
	.attrs = bmg160_attributes
};

static ssize_t bmg160_vendor_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", VENDOR_NAME);
}

static ssize_t bmg160_name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", MODEL_NAME);
}

static int bmg160_open_calibration(struct bmg160_p *data)
{
	int ret = 0;
	mm_segment_t old_fs;
	struct file *cal_filp = NULL;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cal_filp = filp_open(CALIBRATION_FILE_PATH, O_RDONLY, 0666);
	if (IS_ERR(cal_filp)) {
		set_fs(old_fs);
		ret = PTR_ERR(cal_filp);

		data->caldata.x = 0;
		data->caldata.y = 0;
		data->caldata.z = 0;

		pr_err("[SENSOR]: %s - cal_filp open failed(%d)\n",
			__func__, ret);

		return ret;
	}

	ret = cal_filp->f_op->read(cal_filp, (char *)&data->caldata,
		3 * sizeof(int), &cal_filp->f_pos);
	if (ret != 3 * sizeof(int))
		ret = -EIO;

	filp_close(cal_filp, current->files);
	set_fs(old_fs);

	pr_info("[SENSOR]: open gyro calibration %d, %d, %d\n",
		data->caldata.x, data->caldata.y, data->caldata.z);

	if ((data->caldata.x == 0) && (data->caldata.y == 0)
		&& (data->caldata.z == 0))
		return -EIO;

	return ret;
}

static int bmg160_do_calibrate(struct bmg160_p *data, int enable)
{
	int sum[3] = { 0, };
	int ret = 0, cnt;
	struct file *cal_filp = NULL;
	struct bmg160_v gyro;
	mm_segment_t old_fs;

	if (enable) {
		data->caldata.x = 0;
		data->caldata.y = 0;
		data->caldata.z = 0;

		if (atomic_read(&data->enable) == 1)
			cancel_delayed_work_sync(&data->work);
		else
			bmg160_set_mode(data, BMG160_MODE_NORMAL);

		msleep(300);

		for (cnt = 0; cnt < CALIBRATION_DATA_AMOUNT; cnt++) {
			bmg160_read_gyro_xyz(data, &gyro);
			sum[0] += gyro.x;
			sum[1] += gyro.y;
			sum[2] += gyro.z;
			mdelay(10);
		}

		if (atomic_read(&data->enable) == 1)
			schedule_delayed_work(&data->work,
				msecs_to_jiffies(atomic_read(&data->delay)));
		else
			bmg160_set_mode(data, BMG160_MODE_SUSPEND);

		data->caldata.x = (sum[0] / CALIBRATION_DATA_AMOUNT);
		data->caldata.y = (sum[1] / CALIBRATION_DATA_AMOUNT);
		data->caldata.z = (sum[2] / CALIBRATION_DATA_AMOUNT);
	} else {
		data->caldata.x = 0;
		data->caldata.y = 0;
		data->caldata.z = 0;
	}

	pr_info("[SENSOR]: %s - do gyro calibrate %d, %d, %d\n", __func__,
		data->caldata.x, data->caldata.y, data->caldata.z);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	cal_filp = filp_open(CALIBRATION_FILE_PATH,
			O_CREAT | O_TRUNC | O_WRONLY, 0666);
	if (IS_ERR(cal_filp)) {
		pr_err("[SENSOR]: %s - Can't open calibration file\n",
			__func__);
		set_fs(old_fs);
		ret = PTR_ERR(cal_filp);
		return ret;
	}

	ret = cal_filp->f_op->write(cal_filp, (char *)&data->caldata,
		3 * sizeof(int), &cal_filp->f_pos);
	if (ret != 3 * sizeof(int)) {
		pr_err("[SENSOR]: %s - Can't write the caldata to file\n",
			__func__);
		ret = -EIO;
	}

	filp_close(cal_filp, current->files);
	set_fs(old_fs);

	return ret;
}

static ssize_t bmg160_calibration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	struct bmg160_p *data = dev_get_drvdata(dev);

	ret = bmg160_open_calibration(data);
	if (ret < 0)
		pr_err("[SENSOR]: %s - calibration open failed(%d)\n",
			__func__, ret);

	pr_info("[SENSOR]: %s - cal data %d %d %d - ret : %d\n", __func__,
		data->caldata.x, data->caldata.y, data->caldata.z, ret);

	return snprintf(buf, PAGE_SIZE, "%d %d %d %d\n", ret, data->caldata.x,
			data->caldata.y, data->caldata.z);
}

static ssize_t bmg160_calibration_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int ret;
	int64_t dEnable;
	struct bmg160_p *data = dev_get_drvdata(dev);

	ret = kstrtoll(buf, 10, &dEnable);
	if (ret < 0)
		return ret;

	ret = bmg160_do_calibrate(data, (int)dEnable);
	if (ret < 0)
		pr_err("[SENSOR]: %s - gyro calibrate failed\n", __func__);

	return size;
}

static ssize_t bmg160_raw_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bmg160_v gyro;
	struct bmg160_p *data = dev_get_drvdata(dev);

	if (atomic_read(&data->enable) == 0) {
		bmg160_set_mode(data, BMG160_MODE_NORMAL);
		msleep(20);
		bmg160_read_gyro_xyz(data, &gyro);
		bmg160_set_mode(data, BMG160_MODE_SUSPEND);
	} else {
		gyro = data->gyrodata;
	}

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n",
		gyro.x - data->caldata.x,
		gyro.y - data->caldata.y,
		gyro.z - data->caldata.z);
}

static ssize_t bmg160_selftest_dps_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int newdps = 0;
	struct bmg160_p *data = dev_get_drvdata(dev);

	sscanf(buf, "%d", &newdps);

	if (newdps == 250)
		data->gyro_dps = BMG160_RANGE_250DPS;
	else if (newdps == 500)
		data->gyro_dps = BMG160_RANGE_500DPS;
	else if (newdps == 2000)
		data->gyro_dps = BMG160_RANGE_2000DPS;
	else
		data->gyro_dps = BMG160_RANGE_500DPS;

	pr_info("[SENSOR]: %s - %d dps = %d\n", __func__,
		newdps, data->gyro_dps);

	return size;
}

static ssize_t bmg160_selftest_dps_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bmg160_p *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", data->gyro_dps);
}

static int bmg160_selftest_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0, result;
	unsigned char tmp1 = 0;
	unsigned char tmp2 = 0;
	struct bmg160_p *data = dev_get_drvdata(dev);

	ret = bmg160_smbus_read_byte(data->client,
			BMG160_SELF_TEST_ADDR, &tmp1);
	tmp2  = BMG160_GET_BITSLICE(tmp1, BMG160_SELF_TEST_ADDR_RATEOK);
	tmp1  = BMG160_SET_BITSLICE(tmp1, BMG160_SELF_TEST_ADDR_TRIGBIST, 1);
	ret += bmg160_smbus_write_byte(data->client,
			BMG160_SELF_TEST_ADDR_TRIGBIST__REG, &tmp1);

	/* Waiting time to complete the selftest process */
	mdelay(10);

	/* Reading Selftest result bir bist_failure */
	ret += bmg160_smbus_read_byte(data->client,
			BMG160_SELF_TEST_ADDR_BISTFAIL__REG, &tmp1);
	tmp1  = BMG160_GET_BITSLICE(tmp1, BMG160_SELF_TEST_ADDR_BISTFAIL);

	if (ret < 0)
		pr_err("[SENSOR]: %s- selftest i2c failed %d\n", __func__, ret);

	if ((tmp1 == 0x00) && (tmp2 == 0x01))
		result = 1;
	else
		result = -EIO;

	pr_info("[SENSOR]: %s- rate %u, bist %u\n", __func__, tmp2, tmp1);

	return snprintf(buf, PAGE_SIZE, "%d,%d,%d\n", result, tmp1, tmp2);
}

static DEVICE_ATTR(name, S_IRUGO, bmg160_name_show, NULL);
static DEVICE_ATTR(vendor, S_IRUGO, bmg160_vendor_show, NULL);
static DEVICE_ATTR(calibration, S_IRUGO | S_IWUSR | S_IWGRP,
	bmg160_calibration_show, bmg160_calibration_store);
static DEVICE_ATTR(raw_data, S_IRUGO, bmg160_raw_data_show, NULL);
static DEVICE_ATTR(selftest, S_IRUGO, bmg160_selftest_show, NULL);
static DEVICE_ATTR(selftest_dps, S_IRUGO | S_IWUSR | S_IWGRP,
	bmg160_selftest_dps_show, bmg160_selftest_dps_store);

static struct device_attribute *sensor_attrs[] = {
	&dev_attr_name,
	&dev_attr_vendor,
	&dev_attr_calibration,
	&dev_attr_raw_data,
	&dev_attr_selftest,
	&dev_attr_selftest_dps,
	NULL,
};

static int bmg160_setup_pin(struct bmg160_p *data)
{
	int ret;

	ret = gpio_request(data->gyro_int, "GYRO_INT");
	if (ret < 0) {
		pr_err("[SENSOR] %s - gpio %d request failed (%d)\n",
			__func__, data->gyro_int, ret);
		goto exit;
	}

	ret = gpio_direction_input(data->gyro_int);
	if (ret < 0) {
		pr_err("[SENSOR]: %s - failed to set gpio %d as input (%d)\n",
			__func__, data->gyro_int, ret);
		goto exit_gyro_int;
	}

	ret = gpio_request(data->gyro_drdy, "GYRO_DRDY");
	if (ret < 0) {
		pr_err("[SENSOR]: %s - gpio %d request failed (%d)\n",
			__func__, data->gyro_drdy, ret);
		goto exit_gyro_int;
	}

	ret = gpio_direction_input(data->gyro_drdy);
	if (ret < 0) {
		pr_err("[SENSOR]: %s - failed to set gpio %d as input (%d)\n",
			__func__, data->gyro_drdy, ret);
		goto exit_gyro_drdy;
	}

	goto exit;

exit_gyro_drdy:
	gpio_free(data->gyro_drdy);
exit_gyro_int:
	gpio_free(data->gyro_int);
exit:
	return ret;
}

static int bmg160_input_init(struct bmg160_p *data)
{
	int ret = 0;
	struct input_dev *dev;

	dev = input_allocate_device();
	if (!dev)
		return -ENOMEM;

	dev->name = MODULE_NAME;
	dev->id.bustype = BUS_I2C;

	input_set_capability(dev, EV_REL, REL_RX);
	input_set_capability(dev, EV_REL, REL_RY);
	input_set_capability(dev, EV_REL, REL_RZ);

	input_set_drvdata(dev, data);

	ret = input_register_device(dev);
	if (ret < 0) {
		input_free_device(dev);
		return ret;
	}

	ret = sensors_create_symlink(&dev->dev.kobj, dev->name);
	if (ret < 0) {
		input_unregister_device(dev);
		return ret;
	}

	/* sysfs node creation */
	ret = sysfs_create_group(&dev->dev.kobj, &bmg160_attribute_group);
	if (ret < 0) {
		input_unregister_device(dev);
		return ret;
	}

	data->input = dev;
	return 0;
}

static int bmg160_parse_dt(struct bmg160_p *data, struct device *dev)
{
	struct device_node *dNode = dev->of_node;
	enum of_gpio_flags flags;

	if (dNode == NULL)
		return -ENODEV;

	data->gyro_int = of_get_named_gpio_flags(dNode,
		"bmg160-i2c,gyro_int-gpio", 0, &flags);
	if (data->gyro_int < 0) {
		pr_err("[SENSOR]: %s - get gyro_int error\n", __func__);
		return -ENODEV;
	}

	data->gyro_drdy = of_get_named_gpio_flags(dNode,
		"bmg160-i2c,gyro_drdy-gpio", 0, &flags);
	if (data->gyro_drdy < 0) {
		pr_err("[SENSOR]: %s - gyro_drdy error\n", __func__);
		return -ENODEV;
	}

	if (of_property_read_u32(dNode,
			"bmg160-i2c,chip_pos", &data->chip_pos) < 0)
		data->chip_pos = BMG160_TOP_LOWER_RIGHT;

	return 0;
}

static int bmg160_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret = -ENODEV;
	struct bmg160_p *data = NULL;

	pr_info("[SENSOR]: %s - Probe Start!\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("[SENSOR]: %s - i2c_check_functionality error\n",
			__func__);
		goto exit;
	}

	data = kzalloc(sizeof(struct bmg160_p), GFP_KERNEL);
	if (data == NULL) {
		pr_err("[SENSOR]: %s - kzalloc error\n", __func__);
		ret = -ENOMEM;
		goto exit_kzalloc;
	}

	ret = bmg160_parse_dt(data, &client->dev);
	if (ret < 0) {
		pr_err("[SENSOR]: %s - of_node error\n", __func__);
		ret = -ENODEV;
		goto exit_of_node;
	}

	ret = bmg160_setup_pin(data);
	if (ret) {
		pr_err("[SENSOR]: %s - could not setup pin\n", __func__);
		goto exit_setup_pin;
	}

	/* read chip id */
	ret = i2c_smbus_read_word_data(client, BMG160_CHIP_ID_REG);
	if ((ret & 0x00ff) != BMG160_CHIP_ID) {
		pr_err("[SENSOR]: %s - chip id failed %d\n", __func__, ret);
		ret = -ENODEV;
		goto exit_read_chipid;
	}

	i2c_set_clientdata(client, data);
	data->client = client;

	/* input device init */
	ret = bmg160_input_init(data);
	if (ret < 0)
		goto exit_input_init;

	sensors_register(data->factory_device, data, sensor_attrs, MODULE_NAME);

	/* workqueue init */
	INIT_DELAYED_WORK(&data->work, bmg160_work_func);
	atomic_set(&data->delay, BMG160_DEFAULT_DELAY);
	atomic_set(&data->enable, 0);

	data->gyro_dps = BMG160_RANGE_500DPS;
	bmg160_set_bw(data, BMG160_BW_23Hz);
	bmg160_set_range(data, data->gyro_dps);
	bmg160_set_mode(data, BMG160_MODE_SUSPEND);

#ifdef EXECPTION_FOR_I2CFAIL
	data->i2cfail_cnt = 0;
#endif

	pr_info("[SENSOR]: %s - Probe done!(chip pos : %d)\n",
		__func__, data->chip_pos);

	return 0;

exit_input_init:
exit_read_chipid:
	gpio_free(data->gyro_int);
	gpio_free(data->gyro_drdy);
exit_setup_pin:
exit_of_node:
	kfree(data);
exit_kzalloc:
exit:
	pr_err("[SENSOR]: %s - Probe fail!\n", __func__);

	return ret;
}

static int __devexit bmg160_remove(struct i2c_client *client)
{
	struct bmg160_p *data = (struct bmg160_p *)i2c_get_clientdata(client);

	if (atomic_read(&data->enable) == 1)
		bmg160_set_enable(data, 0);

	cancel_delayed_work_sync(&data->work);
	sensors_unregister(data->factory_device, sensor_attrs);
	sensors_remove_symlink(&data->input->dev.kobj, data->input->name);

	sysfs_remove_group(&data->input->dev.kobj, &bmg160_attribute_group);
	input_unregister_device(data->input);

	gpio_free(data->gyro_int);
	gpio_free(data->gyro_drdy);

	kfree(data);

	return 0;
}

static int bmg160_suspend(struct device *dev)
{
	struct bmg160_p *data = dev_get_drvdata(dev);

	if (atomic_read(&data->enable) == 1) {
		bmg160_set_mode(data, BMG160_MODE_SUSPEND);
		cancel_delayed_work_sync(&data->work);
	}

	return 0;
}

static int bmg160_resume(struct device *dev)
{
	struct bmg160_p *data = dev_get_drvdata(dev);

	if (atomic_read(&data->enable) == 1) {
		bmg160_set_mode(data, BMG160_MODE_NORMAL);
		schedule_delayed_work(&data->work,
			msecs_to_jiffies(atomic_read(&data->delay)));
	}

	return 0;
}

static struct of_device_id bmg160_match_table[] = {
	{ .compatible = "bmg160-i2c",},
	{},
};

static const struct i2c_device_id bmg160_id[] = {
	{ "bmg160_match_table", 0 },
	{ }
};

static const struct dev_pm_ops bmg160_pm_ops = {
	.suspend = bmg160_suspend,
	.resume = bmg160_resume,
};

static struct i2c_driver bmg160_driver = {
	.driver = {
		.name	= MODEL_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = bmg160_match_table,
		.pm = &bmg160_pm_ops
	},
	.probe		= bmg160_probe,
	.remove		= __devexit_p(bmg160_remove),
	.id_table	= bmg160_id,
};

static int __init BMG160_init(void)
{
	return i2c_add_driver(&bmg160_driver);
}

static void __exit BMG160_exit(void)
{
	i2c_del_driver(&bmg160_driver);
}
module_init(BMG160_init);
module_exit(BMG160_exit);

MODULE_DESCRIPTION("bmg160 gyroscope sensor driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
