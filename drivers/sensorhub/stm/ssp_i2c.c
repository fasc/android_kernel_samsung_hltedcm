/*
 *  Copyright (C) 2012, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */
#include "ssp.h"

#define LIMIT_DELAY_CNT		200
#define RECEIVEBUFFERSIZE	12
#define DEBUG_SHOW_DATA	0


static int wakeup_mcu(int int_gpio)
{
	gpio_set_value_cansleep(int_gpio, 0);
	udelay(20);
	gpio_set_value_cansleep(int_gpio, 1);

	return 0;
}

int waiting_wakeup_mcu(struct ssp_data *data)
{
	int iDelaycnt = 0;
	while (!gpio_get_value_cansleep(data->mcu_int1)
		&& (iDelaycnt++ < LIMIT_DELAY_CNT)
		&& (data->bSspShutdown == false))
		mdelay(5);

	if (iDelaycnt >= LIMIT_DELAY_CNT) {
		pr_err("[SSP]: %s - MCU Irq Timeout!!\n", __func__);
		data->uBusyCnt++;
	} else {
		data->uBusyCnt = 0;
	}

	iDelaycnt = 0;

	while (!gpio_get_value_cansleep(data->mcu_int2)
		&& (iDelaycnt++ < LIMIT_DELAY_CNT)
		&& (data->bSspShutdown == false))
		mdelay(5);

	if (iDelaycnt >= LIMIT_DELAY_CNT) {
		pr_err("[SSP]: %s - MCU Wakeup Timeout!!\n", __func__);
		data->uTimeOutCnt++;
	} else {
		data->uTimeOutCnt = 0;
	}

	wakeup_mcu(data->ap_int);
	if (data->bSspShutdown == true)
		return ERROR;

	return SUCCESS;
}

int waiting_init_mcu(struct ssp_data *data)
{
	int iDelaycnt = 0;

	while (!gpio_get_value_cansleep(data->mcu_int1)
		&& (iDelaycnt++ < LIMIT_DELAY_CNT))
		mdelay(5);

	if (iDelaycnt >= LIMIT_DELAY_CNT) {
		pr_err("[SSP]: %s - MCU Irq Timeout!!\n", __func__);
		data->uBusyCnt++;
	} else {
		data->uBusyCnt = 0;
	}

	iDelaycnt = 0;
	wakeup_mcu(data->ap_int);
	while (!gpio_get_value_cansleep(data->mcu_int2)
		&& (iDelaycnt++ < LIMIT_DELAY_CNT))
		mdelay(5);

	if (iDelaycnt >= LIMIT_DELAY_CNT) {
		pr_err("[SSP]: %s - MCU Wakeup Timeout!!\n", __func__);
		data->uTimeOutCnt++;
	} else {
		data->uTimeOutCnt = 0;
	}

	return SUCCESS;
}

int ssp_spi_checkrecvstart(char *rxBuf, int len)
{
	unsigned int i;
	for (i = 0; i < len ; i++)	{
		if (rxBuf[i] == 0x00)
			continue;
		break;
	}

	if (i == len)
		return -EFAULT;
	return i;
}


#if DEBUG_SHOW_DATA
void show_ssp_data(char *buff, int len, int type)
{
	unsigned int i;
	char showdata[len*2+2];
	memset(showdata, 0, len*2+2);
	for (i = 0; i < len; i++)
		sprintf(showdata, "%s%02x", showdata, buff[i]);

	if (type == 1)
		pr_err("[SSP]hb received len=%d	%s", len, showdata);
	else if (type == 2)
		pr_err("[SSP]hb received while send %s", showdata);
	else
		pr_err("[SSP]hb sending len=%d  %s", len, showdata);
}

#endif


int ssp_i2c_read(struct ssp_data *data, char *pTxData, u16 uTxLength,
	char *pRxData, u16 uRxLength, int iRetries)
{
	int iRet = 0, iDiffTime = 0, iTimeTemp;
	struct timeval cur_time;

	do_gettimeofday(&cur_time);
	iTimeTemp = (int)cur_time.tv_sec;

#if DEBUG_SHOW_DATA
	show_ssp_data(pTxData, uTxLength, 3);
#endif

	mutex_lock(&data->comm_mutex);


	do {
		char* pSyncrxbuf;
		char* pSynctxbuf;
		unsigned int uRxsize;
		int nStartpos;
		unsigned int uReceivedData;
		int nRetry = 0;

		uRxsize = uTxLength + RECEIVEBUFFERSIZE + uRxLength;

		pSyncrxbuf = (char*)kzalloc(uRxsize,GFP_KERNEL);
		if(pSyncrxbuf == NULL) {
			pr_err("[SSP]: %s - failed to allocate memory\n",
				__func__);
			iRet = -ENOMEM;
			return iRet;
		}
		pSynctxbuf = (char*)kzalloc(uRxsize,GFP_KERNEL);
		if(pSynctxbuf == NULL) {
			pr_err("[SSP]: %s - failed to allocate memory\n",
				__func__);
			iRet = -ENOMEM;
			kfree(pSyncrxbuf);
			return iRet;
		}

		memcpy(pSynctxbuf,pTxData,uTxLength);
		iRet = ssp_spi_sync(data->spi,pSynctxbuf,uRxsize, pSyncrxbuf);
		nStartpos = ssp_spi_checkrecvstart(pSyncrxbuf,uRxsize);

/* receiving not started. retry : 122us for 12byte transfer +145us for standby transaction */
		while(nStartpos < 0 && nRetry < 100)
		{
			//retry 100 makes 30ms
			ssp_spi_sync(data->spi,NULL,uRxsize, pSyncrxbuf);

			nStartpos = ssp_spi_checkrecvstart(pSyncrxbuf,uRxsize);
			nRetry++;
		}
		nRetry=0;

		if(nStartpos<0)
		{
			iRet=-1;
		}
		else
		{
			uReceivedData = uRxsize-nStartpos;
			if(uReceivedData < uRxLength  )
			{
				memcpy(pRxData,pSyncrxbuf+nStartpos,uReceivedData);
				ssp_spi_sync(data->spi,NULL,uRxLength-uReceivedData, pRxData+uReceivedData);

			}
			else  // received respected data size
			{
				memcpy(pRxData,pSyncrxbuf+nStartpos,uRxLength);
			}


		}

		kfree(pSyncrxbuf);
		kfree(pSynctxbuf);

		if (iRet < 0) {
			do_gettimeofday(&cur_time);
			iDiffTime = (int)cur_time.tv_sec - iTimeTemp;
			iTimeTemp = (int)cur_time.tv_sec;
			if (iDiffTime >= 4) {
				pr_err("[SSP]: %s - i2c time out %d!\n",
					__func__, iDiffTime);
				break;
			}
			pr_err("[SSP]: %s - i2c transfer error %d! retry...\n",
				__func__, iRet);
			mdelay(1);
		} else {
#if DEBUG_SHOW_DATA
			show_ssp_data(pRxData, uRxLength, 1);
#endif

			mutex_unlock(&data->comm_mutex);

			return SUCCESS;
		}
	} while (iRetries--);

	mutex_unlock(&data->comm_mutex);

	return ERROR;
}

int ssp_send_cmd(struct ssp_data *data, char command)
{
	char chRxBuf = 0;
	int iRet = 0, iRetries = DEFAULT_RETRIES;

	if (waiting_wakeup_mcu(data) < 0)
		return ERROR;

	iRet = ssp_i2c_read(data, &command, 1, &chRxBuf, 1, DEFAULT_RETRIES);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - command 0x%x failed %d\n",
				__func__, command, iRet);
		return ERROR;
	} else if (chRxBuf != MSG_ACK) {
		while (iRetries--) {
			mdelay(10);
			pr_err("[SSP]: %s - command 0x%x retry...\n", __func__, command);
			iRet = ssp_i2c_read(data, &command, 1, &chRxBuf, 1,
				DEFAULT_RETRIES);
			if ((iRet == SUCCESS) && (chRxBuf == MSG_ACK))
				break;
		}

		if (iRetries < 0) {
			data->uInstFailCnt++;
			return FAIL;
		}
	}

	data->uInstFailCnt = 0;
	ssp_dbg("[SSP]: %s - command 0x%x\n", __func__, command);

	return SUCCESS;
}

int send_instruction(struct ssp_data *data, u8 uInst,
	u8 uSensorType, u8 *uSendBuf, u8 uLength)
{
	char chTxbuf[uLength + 4];
	char chRxbuf = 0;
	int iRet = 0, iRetries = DEFAULT_RETRIES;

	if (data->fw_dl_state == FW_DL_STATE_DOWNLOADING) {
		pr_err("[SSP] %s - Skip Inst! DL state = %d\n",
			__func__, data->fw_dl_state);
		return SUCCESS;
	} else if ((!(data->uSensorState & (1 << uSensorType)))
		&& (uInst <= CHANGE_DELAY)) {
		pr_err("[SSP]: %s - Bypass Inst Skip! - %u\n",
			__func__, uSensorType);
		return FAIL;
	}

	if (waiting_wakeup_mcu(data) < 0)
		return ERROR;

	chTxbuf[0] = MSG2SSP_SSM;
	chTxbuf[1] = (char)(uLength + 4);

	switch (uInst) {
	case REMOVE_SENSOR:
		chTxbuf[2] = MSG2SSP_INST_BYPASS_SENSOR_REMOVE;
		break;
	case ADD_SENSOR:
		chTxbuf[2] = MSG2SSP_INST_BYPASS_SENSOR_ADD;
		break;
	case CHANGE_DELAY:
		chTxbuf[2] = MSG2SSP_INST_CHANGE_DELAY;
		break;
	case GO_SLEEP:
		chTxbuf[2] = MSG2SSP_AP_STATUS_SLEEP;
		break;
	case FACTORY_MODE:
		chTxbuf[2] = MSG2SSP_INST_SENSOR_SELFTEST;
		break;
	case REMOVE_LIBRARY:
		chTxbuf[2] = MSG2SSP_INST_LIBRARY_REMOVE;
		break;
	case ADD_LIBRARY:
		chTxbuf[2] = MSG2SSP_INST_LIBRARY_ADD;
		break;
	default:
		chTxbuf[2] = uInst;
		break;
	}

	chTxbuf[3] = uSensorType;
	memcpy(&chTxbuf[4], uSendBuf, uLength);

	iRet = ssp_i2c_read(data, &(chTxbuf[0]), uLength + 4, &chRxbuf, 1,
		DEFAULT_RETRIES);
	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - Instruction CMD Fail %d\n", __func__, iRet);
		return ERROR;
	} else if (chRxbuf != MSG_ACK) {
		while (iRetries--) {
			mdelay(10);
			pr_err("[SSP]: %s - Instruction CMD retry...\n",
				__func__);
			if (waiting_wakeup_mcu(data) < 0)
				return ERROR;
			iRet = ssp_i2c_read(data, &(chTxbuf[0]),
				uLength + 4, &chRxbuf, 1, DEFAULT_RETRIES);
			if ((iRet == SUCCESS) && (chRxbuf == MSG_ACK))
				break;
		}

		if (iRetries < 0) {
			data->uInstFailCnt++;
			return FAIL;
		}
	}

	data->uInstFailCnt = 0;
	ssp_dbg("[SSP]: %s - Inst = 0x%x, Sensor Type = 0x%x, data = %u\n",
		__func__, chTxbuf[2], chTxbuf[3], chTxbuf[4]);
	return SUCCESS;
}

int get_chipid(struct ssp_data *data)
{
	int iRet;
	char sendbuf[2];
	char recvbuf;
	sendbuf[0] = MSG2SSP_AP_WHOAMI;
	sendbuf[1] = '\0';

	if (waiting_init_mcu(data) < 0)
		return ERROR;

	/* read chip id */
	iRet = ssp_i2c_read(data, sendbuf, 2, &recvbuf, 1, DEFAULT_RETRIES);
	if (iRet == SUCCESS)
		return recvbuf;
	else
		return ERROR;
}

int set_sensor_position(struct ssp_data *data)
{
	char chTxBuf[5] = { 0, };
	char chRxData = 0;
	int iRet = 0;

	if (waiting_init_mcu(data) < 0)
		return ERROR;

	chTxBuf[0] = MSG2SSP_AP_SENSOR_FORMATION;

	/* Please refer to ssp_get_positions on the file
	 * board-universal_5410-sensor.c */
	chTxBuf[1] = data->accel_position;
	chTxBuf[2] = data->accel_position;
	chTxBuf[3] = data->mag_position;
	chTxBuf[4] = 0;

	pr_info("[SSP] Sensor Posision A : %u, G : %u, M: %u, P: %u\n",
		chTxBuf[1], chTxBuf[2], chTxBuf[3], chTxBuf[4]);

	iRet = ssp_i2c_read(data, chTxBuf, 5, &chRxData, 1, DEFAULT_RETRIES);
	if ((chRxData != MSG_ACK) || (iRet != SUCCESS)) {
		pr_err("[SSP]: %s - i2c fail %d\n", __func__, iRet);
		iRet = ERROR;
	}

	return iRet;
}

void set_proximity_threshold(struct ssp_data *data,
	unsigned char uData1, unsigned char uData2)
{
	char chTxBuf[3] = { 0, };
	char chRxBuf = 0;
	int iRet = 0, iRetries = DEFAULT_RETRIES;

	if (!(data->uSensorState & 0x20)) {
		pr_info("[SSP]: %s - Skip this function!!!"\
			", proximity sensor is not connected(0x%x)\n",
			__func__, data->uSensorState);
		return;
	}
	if (waiting_wakeup_mcu(data) < 0 ||
		data->fw_dl_state == FW_DL_STATE_DOWNLOADING) {
		pr_info("[SSP] : %s, skip DL state = %d\n", __func__,
			data->fw_dl_state);
		return;
	}

	chTxBuf[0] = MSG2SSP_AP_SENSOR_PROXTHRESHOLD;
	chTxBuf[1] = uData1;
	chTxBuf[2] = uData2;

	iRet = ssp_i2c_read(data, chTxBuf, 3, &chRxBuf, 1, DEFAULT_RETRIES);
	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - SENSOR_PROXTHRESHOLD CMD fail %d\n",
			__func__, iRet);
		return;
	} else if (chRxBuf != MSG_ACK) {
		while (iRetries--) {
			mdelay(10);
			pr_err("[SSP]: %s - MSG2SSP_AP_SENSOR_PROXTHRESHOLD "\
				"CMD retry...\n", __func__);
			iRet = ssp_i2c_read(data, chTxBuf, 3, &chRxBuf, 1,
				DEFAULT_RETRIES);
			if ((iRet == SUCCESS) && (chRxBuf == MSG_ACK))
				break;
		}

		if (iRetries < 0) {
			data->uInstFailCnt++;
			return;
		}
	}

	data->uInstFailCnt = 0;
	pr_info("[SSP]: Proximity Threshold - %u, %u\n", uData1, uData2);
}

void set_proximity_barcode_enable(struct ssp_data *data, bool bEnable)
{
	char chTxBuf[2] = { 0, };
	char chRxBuf = 0;
	int iRet = 0, iRetries = DEFAULT_RETRIES;

	if (waiting_wakeup_mcu(data) < 0)
		return;

	chTxBuf[0] = MSG2SSP_AP_SENSOR_BARCODE_EMUL;
	chTxBuf[1] = bEnable;

	data->bBarcodeEnabled = bEnable;

	iRet = ssp_i2c_read(data, chTxBuf, 2, &chRxBuf, 1, DEFAULT_RETRIES);
	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - SENSOR_BARCODE_EMUL CMD fail %d\n",
				__func__, iRet);
		return;
	} else if (chRxBuf != MSG_ACK) {
		while (iRetries--) {
			mdelay(10);
			pr_err("[SSP]: %s - MSG2SSP_AP_SENSOR_BARCODE_EMUL "\
				"CMD retry...\n", __func__);
			iRet = ssp_i2c_read(data, chTxBuf, 2, &chRxBuf, 1,
				DEFAULT_RETRIES);
			if ((iRet == SUCCESS) && (chRxBuf == MSG_ACK))
				break;
		}

		if (iRetries < 0) {
			data->uInstFailCnt++;
			return;
		}
	}

	data->uInstFailCnt = 0;
	pr_info("[SSP] Proximity Barcode En : %u\n", bEnable);
}

void set_gesture_current(struct ssp_data *data, unsigned char uData1)
{
	char chTxBuf[2] = { 0, };
	char chRxBuf = 0;
	int iRet = 0, iRetries = DEFAULT_RETRIES;

	if (waiting_wakeup_mcu(data) < 0 ||
		data->fw_dl_state == FW_DL_STATE_DOWNLOADING) {
		pr_info("[SSP] : %s, skip DL state = %d\n", __func__,
			data->fw_dl_state);
		return;
	}

	chTxBuf[0] = MSG2SSP_AP_SENSOR_GESTURE_CURRENT;
	chTxBuf[1] = uData1;

	iRet = ssp_i2c_read(data, chTxBuf, 2, &chRxBuf, 1, DEFAULT_RETRIES);
	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - SENSOR_GESTURE_CURRENT CMD fail %d\n",
			__func__, iRet);
		return;
	} else if (chRxBuf != MSG_ACK) {
		while (iRetries--) {
			mdelay(10);
			pr_err("[SSP]: %s - MSG2SSP_AP_SENSOR_GESTURE_CURRENT "\
				"CMD retry...\n", __func__);
			iRet = ssp_i2c_read(data, chTxBuf, 2, &chRxBuf, 1,
				DEFAULT_RETRIES);
			if ((iRet == SUCCESS) && (chRxBuf == MSG_ACK))
				break;
		}

		if (iRetries < 0) {
			data->uInstFailCnt++;
			return;
		}
	}

	data->uInstFailCnt = 0;
	pr_info("[SSP]: Gesture Current Setting - %u\n", uData1);
}

unsigned int get_sensor_scanning_info(struct ssp_data *data)
{
	char chTxBuf = MSG2SSP_AP_SENSOR_SCANNING;
	char chRxData[2] = {0,};
	int iRet = 0;

	if (waiting_init_mcu(data) < 0)
		return ERROR;

	iRet = ssp_i2c_read(data, &chTxBuf, 1, chRxData, 2, DEFAULT_RETRIES);
	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - i2c failed %d\n", __func__, iRet);
		return 0;
	}
	return ((unsigned int)chRxData[0] << 8) | chRxData[1];
}

unsigned int get_firmware_rev(struct ssp_data *data)
{
	char chTxData = MSG2SSP_AP_FIRMWARE_REV;
	char chRxBuf[3] = { 0, };
	unsigned int uRev = 99999;
	int iRet;

	if (waiting_wakeup_mcu(data) < 0)
		return ERROR;

	iRet = ssp_i2c_read(data, &chTxData, 1, chRxBuf, 3, DEFAULT_RETRIES);
	if (iRet != SUCCESS)
		pr_err("[SSP]: %s - i2c fail %d\n", __func__, iRet);
	else
		uRev = ((unsigned int)chRxBuf[0] << 16)
			| ((unsigned int)chRxBuf[1] << 8) | chRxBuf[2];
	return uRev;
}

int get_fuserom_data(struct ssp_data *data)
{
	char chTxBuf[2] = { 0, };
	char chRxBuf[2] = { 0, };
	int iRet = 0;
	unsigned int uLength = 0;

	if (waiting_init_mcu(data) < 0)
		return ERROR;

	chTxBuf[0] = MSG2SSP_AP_STT;
	chTxBuf[1] = MSG2SSP_AP_FUSEROM;

	/* chRxBuf is Two Byte because msg is large */
	iRet = ssp_i2c_read(data, chTxBuf, 2, chRxBuf, 2, DEFAULT_RETRIES);
	uLength = ((unsigned int)chRxBuf[0] << 8) + (unsigned int)chRxBuf[1];

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - MSG2SSP_AP_STT - i2c fail %d\n",
				__func__, iRet);
		goto err_read_fuserom;
	} else if (uLength <= 0) {
		pr_err("[SSP]: %s - No ready data. length = %u\n",
				__func__, uLength);
		goto err_read_fuserom;
	} else {
		if (data->iLibraryLength != 0)
			kfree(data->pchLibraryBuf);

		data->iLibraryLength = (int)uLength;
		data->pchLibraryBuf =
		    kzalloc((data->iLibraryLength * sizeof(char)), GFP_KERNEL);

		chTxBuf[0] = MSG2SSP_SRM;
		iRet = ssp_i2c_read(data, chTxBuf, 1, data->pchLibraryBuf,
			(u16)data->iLibraryLength, DEFAULT_RETRIES);
		if (iRet != SUCCESS) {
			pr_err("[SSP]: %s - Fail to receive SSP data %d\n",
				__func__, iRet);
			kfree(data->pchLibraryBuf);
			data->iLibraryLength = 0;
			goto err_read_fuserom;
		}
	}

	data->uFuseRomData[0] = data->pchLibraryBuf[0];
	data->uFuseRomData[1] = data->pchLibraryBuf[1];
	data->uFuseRomData[2] = data->pchLibraryBuf[2];

	pr_info("[SSP] FUSE ROM Data %d , %d, %d\n", data->uFuseRomData[0],
		data->uFuseRomData[1], data->uFuseRomData[2]);

	data->iLibraryLength = 0;
	kfree(data->pchLibraryBuf);
	return SUCCESS;

err_read_fuserom:
	data->uFuseRomData[0] = 0;
	data->uFuseRomData[1] = 0;
	data->uFuseRomData[2] = 0;

	return FAIL;
}

static int ssp_receive_msg(struct ssp_data *data,  u8 uLength)
{
	char chTxBuf = 0;
	char *pchRcvDataFrame = NULL;	/* SSP-AP Massage data buffer */
	int iRet = 0;

	if (uLength > 0) {
		pchRcvDataFrame = kzalloc((uLength * sizeof(char)), GFP_KERNEL);
		if (pchRcvDataFrame == NULL) {
			pr_err("[SSP]: %s - failed to allocate memory\n",
				__func__);
			iRet = -ENOMEM;
			return iRet;
		}
		chTxBuf = MSG2SSP_SRM;
		iRet = ssp_i2c_read(data, &chTxBuf, 1, pchRcvDataFrame,
				(u16)uLength, 0);
		if (iRet != SUCCESS) {
			pr_err("[SSP]: %s - Fail to receive data %d\n",
				__func__, iRet);
			kfree(pchRcvDataFrame);
			return ERROR;
		}
	} else {
		pr_err("[SSP]: %s - No ready data. length = %d\n",
			__func__, uLength);
		return FAIL;
	}

	parse_dataframe(data, pchRcvDataFrame, uLength);

	kfree(pchRcvDataFrame);
	return uLength;
}

int select_irq_msg(struct ssp_data *data)
{
	u8 chLength = 0;
	char chTxBuf = 0;
	char chRxBuf[2] = { 0, };
	int iRet = 0;

	chTxBuf = MSG2SSP_SSD;
	iRet = ssp_i2c_read(data, &chTxBuf, 1, chRxBuf, 2, 4);

	if (iRet != SUCCESS) {
		pr_err("[SSP]: %s - MSG2SSP_SSD error %d\n", __func__, iRet);
		return ERROR;
	} else {
		if (chRxBuf[0] == MSG2SSP_RTS) {
			chLength = (u8)chRxBuf[1];
			ssp_receive_msg(data, chLength);
			data->uSsdFailCnt = 0;
		}
#ifdef CONFIG_SENSORS_SSP_SENSORHUB
		else if (chRxBuf[0] == MSG2SSP_STT) {
			pr_info("%s: MSG2SSP_STT irq", __func__);
			iRet = ssp_sensorhub_handle_large_data(data,
					(u8)chRxBuf[1]);
			if (iRet < 0) {
				pr_err("%s: ssp sensorhub large data err(%d)",
					__func__, iRet);
			}
			data->uSsdFailCnt = 0;
		}
#endif
		else if (chRxBuf[0] == MSG2SSP_NO_DATA) {
			pr_info("%s: MSG2SSP_NO_DATA irq [0]: 0x%x, [1]: 0x%x\n",
				__func__, chRxBuf[0], chRxBuf[1]);
		} else {
			pr_err("[SSP]: %s - MSG2SSP_SSD Data fail "\
				"[0]: 0x%x, [1]: 0x%x\n", __func__,
				chRxBuf[0], chRxBuf[1]);
			if ((chRxBuf[0] == 0) && (chRxBuf[1] == 0))
				data->uSsdFailCnt++;
		}
	}
	return SUCCESS;
}
