/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */
#ifndef __CAM_CAL_LIST_H
#define __CAM_CAL_LIST_H
#include <linux/i2c.h>

/* code at 2022/10/26 start */
#define DEFAULT_MAX_EEPROM_SIZE_16K 0x4000
/* code at 2022/10/26 end */
typedef unsigned int (*cam_cal_cmd_func) (struct i2c_client *client,
	unsigned int addr, unsigned char *data, unsigned int size);

struct stCAM_CAL_LIST_STRUCT {
	unsigned int sensorID;
	unsigned int slaveID;
	cam_cal_cmd_func readCamCalData;
	unsigned int maxEepromSize;
};

extern unsigned int  zte_s5k4h7_read_region(struct i2c_client *client,
	unsigned int addr,
	unsigned char *data,
	unsigned int size);
extern unsigned int  zte_s5k4h7_sub_read_region(struct i2c_client *client,
	unsigned int addr,
	unsigned char *data,
	unsigned int size);

unsigned int cam_cal_get_sensor_list
		(struct stCAM_CAL_LIST_STRUCT **ppCamcalList);

#endif				/* __CAM_CAL_LIST_H */
