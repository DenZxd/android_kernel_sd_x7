/* drivers/input/touchscreen/goodix_touch.c
 * dix_ts_power
 *
 * Copyright (C) 2010 - 2011 Goodix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/input/mt.h>
#include <linux/platform_device.h>
#include <mach/gpio.h>
#include <linux/irq.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>
#include <linux/goodix_touch.h>

#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <asm/uaccess.h>

//*************************Touchkey Surpport Part*****************************
#define HAVE_TOUCH_KEY
#ifdef HAVE_TOUCH_KEY
	#define READ_COOR_ADDR 0x00
	static uint16_t tp_key_array2[]={
									  KEY_MENU,				//MENU
									  KEY_HOME,				//HOME
									  KEY_BACK				//BACK
									 };
	#define MAX_KEY_NUM	 (sizeof(tp_key_array2)/sizeof(tp_key_array2[0]))
#else
	#define READ_COOR_ADDR 0x01
#endif
//*****************************End of Part II*********************************

#define GOODIX_USE_IRQ 1
#if 0
extern char key_led_bright ;
extern  void open_key_led(void);
extern  void close_key_led(void);
static struct delayed_work led_work;
#endif

unsigned int oldcrc32 = 0xFFFFFFFF;
unsigned int crc32_table[256];
unsigned int ulPolynomial = 0x04c11db7;
unsigned char rd_cfg_addr;
unsigned char rd_cfg_len;
unsigned char g_enter_isp = 0;

static const char *goodix_ts_name = "Goodix";
static struct workqueue_struct *goodix_wq;
struct i2c_client * i2c_connect_client = NULL;
static struct proc_dir_entry *goodix_proc_entry;

#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h);
static void goodix_ts_late_resume(struct early_suspend *h);
#endif

static int goodix_update_write(struct file *filp, const char __user *buff, unsigned long len, void *data);
static int goodix_update_read(char *page, char **start, off_t off, int count, int *eof, void *data);

/*******************************************************
Description:
	Read data from the i2c slave device;
	This operation consisted of 2 i2c_msgs,the first msg used
	to write the operate address,the second msg used to read data.

Parameter:
	client:	i2c device.
	buf[0]:operate address.
	buf[1]~buf[len]:read data buffer.
	len:operate length.

return:
	numbers of i2c_msgs to transfer
*********************************************************/
static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret=-1;
	int retries = 0;
	struct goodix_i2c_rmi_platform_data *pdata;

	pdata = client->dev.platform_data;
	msgs[0].flags = 0;
	msgs[0].addr = client->addr;
	msgs[0].len = 1;
	msgs[0].buf = &buf[0];

	msgs[1].flags = I2C_M_RD;
	msgs[1].addr = client->addr;
	msgs[1].len = len-1;
	msgs[1].buf = &buf[1];

	disable_irq(client->irq);
	while (retries < 5)
	{
		ret = i2c_transfer(client->adapter, msgs, 2);
		if(ret == 2)
			break;

		retries++;
	}
	enable_irq(client->irq);

	return ret;
}

/*******************************************************
Description:
	write data to the i2c slave device.

Parameter:
	client:	i2c device.
	buf[0]:operate address.
	buf[1]~buf[len]:write data buffer.
	len:operate length.

return:
	numbers of i2c_msgs to transfer.
*********************************************************/
static int i2c_write_bytes(struct i2c_client *client, uint8_t *data, int len)
{
	struct i2c_msg msg;
	int ret = -1;
	int retries = 0;
	struct goodix_i2c_rmi_platform_data *pdata;

	pdata = client->dev.platform_data;
	msg.flags = 0;
	msg.addr = client->addr;
	msg.len = len;
	msg.buf = data;

	disable_irq(client->irq);
	while (retries < 5)
	{
		ret=i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1)
			break;

		retries++;
	}
	enable_irq(client->irq);

	return ret;
}

/*******************************************************
Description:
	Goodix touchscreen initialize function.

Parameter:
	ts:	i2c client private struct.

return:
	Executive outcomes.0---succeed.
*******************************************************/
static int goodix_ts_power(struct i2c_client *client, struct goodix_ts_data * ts, int on);
static int goodix_init_panel(struct goodix_ts_data *ts)
{
	int ret = -1;
	uint8_t rd_cfg_buf[7] = {0x66, };
	uint8_t rd_version_buf[2] = {0x240, };

	msleep(40);

	ret = i2c_read_bytes(ts->client, rd_cfg_buf, 2);
	if (ret != 2) {
	    dev_err(&ts->client->dev, "fail to get goodix version\n");
	    return -1;
	}
#if 1
        uint8_t config_info[] = {
        0x65,0xa0,0x03,0x00,0x04,0x00,0xaa,0x6e,
        0x00,0x00,0x00,0x03,0x05,0x10,0x4c,0x41,
        0x41,0x20,0x07,0x00,0x80,0x80,0x64,0x6e,0x1d,0x1c,0x1b,0x1a,0x19,0x18,0x17,0x16,
        0x15,0x14,0x13,0x12,0x11,0x10,0x0F,0x0E,0x0D,0x0C,0x0B,0x0A,0x09,0x08,0x07,0x06,
        0x05,0x04,0x03,0x02,0x01,0x00,0x64,0x46,0x7d,0xB6,0x00,0x00,0x00,0x00,0x00,0x16,
        0x19,0x19,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00
        };
        ret=i2c_write_bytes(ts->client,config_info, (sizeof(config_info)/sizeof(config_info[0])));
#endif
	ret = i2c_read_bytes(ts->client, rd_cfg_buf, 7);
	if (ret != 2)
	{
		dev_err(&ts->client->dev, "Read resolution & max_touch_num failed, use default value!\n");
		ts->abs_x_max = TOUCH_MAX_WIDTH;
		ts->abs_y_max = TOUCH_MAX_HEIGHT;
		ts->max_touch_num = MAX_FINGER_NUM;
		ts->int_trigger_type = INT_TRIGGER;

		return 0;
	}


	ts->abs_x_max = TOUCH_MAX_WIDTH;
	ts->abs_y_max = TOUCH_MAX_HEIGHT;
	ts->max_touch_num = rd_cfg_buf[5] & 0x0f;
	ts->int_trigger_type = rd_cfg_buf[6] & 0x03;

	if ((!ts->abs_x_max) || (!ts->abs_y_max) || (!ts->max_touch_num))
	{
		dev_err(&ts->client->dev, "Read invalid resolution & max_touch_num, use default value!\n");
		ts->abs_x_max = TOUCH_MAX_WIDTH;
		ts->abs_y_max = TOUCH_MAX_HEIGHT;
		ts->max_touch_num = MAX_FINGER_NUM;
	}

	rd_cfg_buf[0] = 0x6e;
	rd_cfg_buf[1] = 0x00;
	i2c_read_bytes(ts->client, rd_cfg_buf, 2);

        ts->fingers = 0;
        ts->fingerbits = 0;

	msleep(10);

	return 0;

}

/*******************************************************
Description:
	Read goodix touchscreen version function.

Parameter:
	ts:	i2c client private struct.

return:
	Executive outcomes.0---succeed.
*******************************************************/
static int goodix_read_version(struct goodix_ts_data *ts, char **version)
{
	int ret = -1, count = 0;
	char *version_data;
	char *p;

	*version = (char *)vmalloc(18);
	version_data = *version;
	if (!version_data)
		return -ENOMEM;

	p = version_data;
	memset(version_data, 0, sizeof(version_data));
	version_data[0] = 240;

	ret = i2c_read_bytes(ts->client, version_data, 17);
	if (ret < 0)
		return ret;
	version_data[17] = '\0';

	if(*p == '\0')
		return 0;

	do {
		if((*p > 122) || (*p < 48 && *p != 32) || (*p >57 && *p  < 65)
			||(*p > 90 && *p < 97 && *p  != '_'))	//check illeqal character
			count++;
	} while (*++p != '\0');

	if (count > 2)
		return 0;
	else
		return 1;
}

/*******************************************************
Description:
	Goodix touchscreen work function.

Parameter:
	ts:	i2c client private struct.

return:
	Executive outcomes.0---succeed.goodix_ts_work_funcgoodix_ts_work_func
*******************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{
	int ret = -1;
	int tmp = 0;
	int key_locked_stat;
	uint8_t point_data[(1 - READ_COOR_ADDR) + 1 + 2 + 5 * MAX_FINGER_NUM + 1] = { 0 };	//read address(1byte)+key index(1byte)+point mask(2bytes)+5bytes*MAX_FINGER_NUM+coor checksum(1byte)
	uint8_t check_sum = 0;
	uint16_t finger_current = 0;
	uint16_t finger_bit = 0;
	unsigned int count = 0, point_count = 0;
	unsigned int position = 0;
	uint8_t track_id[MAX_FINGER_NUM] = {0};
	unsigned int input_x = 0;
	unsigned int input_y = 0;
	unsigned int input_w = 0;
	unsigned char index = 0;
	unsigned char touch_num = 0;
	struct goodix_i2c_rmi_platform_data *pdata;

	struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);
	if (g_enter_isp)
		return ;

	pdata = ts->client->dev.platform_data;
#ifdef CONFIG_SMARTQ_T15
#if defined(CONFIG_SWITCH_GPIO) || defined(CONFIG_SWITCH_GPIO_MODULE)
	if(pdata && pdata->get_lock_state)
	    key_locked_stat = pdata->get_lock_state();
#endif
#endif

#if defined(GOODIX_USE_IRQ)
COORDINATE_POLL:
        /*
	if((ts->int_trigger_type > 1) && (gpio_get_value(pdata->irq_gpio) != (ts->int_trigger_type & 0x01)))
	{
		goto NO_ACTION;
	}*/
#endif

	if (tmp > 9)
	{
		dev_err(&(ts->client->dev), "I2C transfer error,touchscreen stop working.\n");
		goto XFER_ERROR;
	}

	if (ts->bad_data)
		msleep(20);

	point_data[0] = READ_COOR_ADDR;	//read coor address
	ret = i2c_read_bytes(ts->client, point_data, sizeof(point_data) / sizeof(point_data[0]));
	if (ret <= 0)
	{
		dev_err(&(ts->client->dev)," %s : I2C transfer error. Number:%d\n ", __func__, ret);
		ts->bad_data = 1;
		tmp ++;
		ts->retry++;
	#if defined(GOODIX_USE_IRQ)
		if (ts->int_trigger_type > 1)
			goto COORDINATE_POLL;
		else
			goto XFER_ERROR;
	#endif
	}

	ts->bad_data = 0;
	finger_current = (point_data[3 - READ_COOR_ADDR] << 8) + point_data[2 - READ_COOR_ADDR];

	if (finger_current)
	{
		point_count = 0;
		finger_bit = finger_current;
		for (count = 0; (finger_bit != 0) && (count < ts->max_touch_num); count++)	//cal how many point touch currntly
		{
			if(finger_bit & 0x01)
			{
				track_id[point_count] = count;
				point_count++;
			}
			finger_bit >>= 1;
		}
		touch_num = point_count;

		check_sum = point_data[2 - READ_COOR_ADDR] + point_data[3 - READ_COOR_ADDR];	//cal coor checksum
		count = 4 - READ_COOR_ADDR;
		for (point_count *= 5; point_count > 0; point_count--)
			check_sum += point_data[count++];

		check_sum += point_data[count];
		if (check_sum != 0)	//checksum verify error
		{
			dev_err(&ts->client->dev, "%s : coor checksum error!\n",__func__);
		#if defined(GOODIX_USE_IRQ)
			if (ts->int_trigger_type > 1)
				goto COORDINATE_POLL;
			else
				goto XFER_ERROR;
		#endif
		}
	}
	else
		touch_num = 0;

        if ((0 == touch_num) && (0 == point_data[1]) && (ts->int_trigger_type > 1) && (gpio_get_value(pdata->irq_gpio) == (ts->int_trigger_type & 0x01))) {
                //printk("wrong release,ignore it\n");
                msleep(POLL_TIME);
                goto COORDINATE_POLL;
        }
#if 0
	if (touch_num)
	{
		for (index = 0; index < touch_num; index++)
		{
			position = 4 - READ_COOR_ADDR + 5 * index;
			input_x = (unsigned int)(point_data[position] << 8) + (unsigned int)(point_data[position+1]);
			input_y = (unsigned int)(point_data[position+2] << 8) + (unsigned int)(point_data[position+3]);
			input_w = (unsigned int)(point_data[position+4]);

			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, input_y);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, ts->abs_y_max - input_x);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, input_w);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, input_w);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, track_id[index]);
			input_report_key(ts->input_dev, BTN_TOUCH, 1);
			input_mt_sync(ts->input_dev);
		}
	}
	else
	{
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
		input_report_key(ts->input_dev, BTN_TOUCH, 0);
		input_mt_sync(ts->input_dev);
	}
#else
        u32 bitmask;
        u32 finger_flag = finger_current;
        int px, py,i;
        index = 0;
        for(i=0; i<ts->max_touch_num; i++) {
                bitmask=1<<i;
                if(finger_flag & bitmask) { /*finger exists*/
                        position = 4 - READ_COOR_ADDR + 5 * index;
                        index++;
                        px = (unsigned int)(point_data[position] << 8) + (unsigned int)(point_data[position+1]);
                        py = (unsigned int)(point_data[position+2] << 8) + (unsigned int)(point_data[position+3]);
                        input_w = (unsigned int)(point_data[position+4]);

                        ts->prev_x[i]=py;
                        ts->prev_y[i]=ts->abs_y_max - px;

                        if(ts->fingerbits & (1<<i)) {
                                //printk("Finger%d move:(%d,%d)\n",i,ts->prev_x[i],ts->prev_y[i]);
                        }
                        else {
                                //printk("Finger%d enter:(%d,%d)\n",i,ts->prev_x[i],ts->prev_y[i]);
                                ts->fingerbits|=(1<<i);
                        }

                        input_mt_slot(ts->input_dev, i);
                        input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER,true);
                        //input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, i);
                        input_report_abs(ts->input_dev, ABS_MT_POSITION_X, ts->prev_x[i]);
                        input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, ts->prev_y[i]);
                        //input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, input_w);
                        //input_report_key(ts->input_dev, BTN_TOUCH, 1);
                        //input_mt_sync(ts->input_dev);
                }
                else {
                        if(ts->fingers & bitmask) {
                                //printk("Finger%d leave:(%d,%d)\n",i,ts->prev_x[i],ts->prev_y[i]);
                                input_mt_slot(ts->input_dev, i);
                                input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER,false);
                                //input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, i);
                                //input_report_abs(ts->input_dev, ABS_MT_POSITION_X, ts->prev_x[i]);
                                //input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, ts->prev_y[i]);
                                //input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
                                //input_report_key(ts->input_dev, BTN_TOUCH, 0);
                                //input_mt_sync(ts->input_dev);
                                ts->fingerbits&=~(1<<i);
                        }
                }
        }
        ts->fingers=finger_flag;
#endif
#ifdef HAVE_TOUCH_KEY
	if (point_data[1]) {
#if 0
		if(!key_led_bright)
		{
			open_key_led();
			schedule_delayed_work(&led_work,8*HZ);
		}
#endif
	}

	for (count = 0; count < MAX_KEY_NUM; count++) {
		int value = !!(point_data[1] & (0x01 << count));

		if (!key_locked_stat)
			input_report_key(ts->input_dev, tp_key_array2[count], value);
		else if (!value)
			input_report_key(ts->input_dev, tp_key_array2[count], value);
	}
#endif
	input_sync(ts->input_dev);

#if defined(GOODIX_USE_IRQ)
	if((ts->int_trigger_type > 1) && (gpio_get_value(pdata->irq_gpio) == (ts->int_trigger_type & 0x01)))
	{
		msleep(POLL_TIME);
		goto COORDINATE_POLL;
	}
#endif
	goto END_WORK_FUNC;

NO_ACTION:
#ifdef HAVE_TOUCH_KEY
	if (point_data[1]) {
#if 0
		if(!key_led_bright)
		{
			open_key_led();
			schedule_delayed_work(&led_work,8*HZ);
		}
#endif
	}

	for (count = 0; count < MAX_KEY_NUM; count++) {
		int value = !!(point_data[1] & (0x01 << count));

		if (!key_locked_stat)
			input_report_key(ts->input_dev, tp_key_array2[count], value);
		else if (!value)
			input_report_key(ts->input_dev, tp_key_array2[count], value);
	}
	input_sync(ts->input_dev);
#endif
END_WORK_FUNC:
XFER_ERROR:
	if(ts->use_irq)
		enable_irq(ts->client->irq);
}

/*******************************************************
Description:
	Timer interrupt service routine.

Parameter:
	timer:	timer struct pointer.

return:
	Timer work mode. HRTIMER_NORESTART---not restart mode
*******************************************************/
static enum hrtimer_restart goodix_ts_timer_func(struct hrtimer *timer)
{
	struct goodix_ts_data *ts = container_of(timer, struct goodix_ts_data, timer);
	queue_work(goodix_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, (POLL_TIME+6)*1000000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

/*******************************************************
Description:
	External interrupt service routine.

Parameter:
	irq:	interrupt number.
	dev_id: private data pointer.

return:
	irq execute status.
*******************************************************/
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct goodix_ts_data *ts = dev_id;

	if (ts->first_irq)
	{
		ts->first_irq = false;
		return IRQ_HANDLED;
	}

	disable_irq_nosync(ts->client->irq);
	queue_work(goodix_wq, &ts->work);

	return IRQ_HANDLED;
}

/*******************************************************
Description:
	Goodix touchscreen power manage function.

Parameter:
	on:	power status.0---suspend;1---resume.

return:
	Executive outcomes.-1---i2c transfer error;0---succeed.
*******************************************************/
static int goodix_ts_power(struct i2c_client *client, struct goodix_ts_data * ts, int on)
{
	int ret = -1;
	unsigned char i2c_control_buf[2] = {80,  1};	//suspend cmd
	int retry = 0;
	struct goodix_i2c_rmi_platform_data *pdata;

	pdata = client->dev.platform_data;

	if (on != 0 && on !=1)
	{
		dev_err(&ts->client->dev, "%s: Cant't support this command.", goodix_ts_name);
		return -EINVAL;
	}

	if (ts != NULL && !ts->use_irq)
		return -2;

	if (on == 0)		//suspend
	{
		while (retry < 5)
		{
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);
			if (ret == 1)
				break;

			dev_err(&ts->client->dev, "%s : Send cmd failed!\n",__func__);
			retry++;
			msleep(10);
		}
		if (ret > 0)
			ret = 0;

		gpio_direction_output(pdata->irq_gpio, 1);
	}
	else if(on == 1)	//resume
	{
		gpio_direction_output(pdata->irq_gpio, 0);
		msleep(20);

		if (ts->use_irq)
		{
			pdata->irq_init();
			pdata->rst();
		}
		else
			gpio_direction_input(pdata->irq_gpio);

		ret = 0;
	}
	return ret;
}

/*******************************************************
Description:
	Goodix debug sysfs cat version function.

Parameter:
	standard sysfs show param.

return:
	Executive outcomes. 0---failed.
*******************************************************/
static ssize_t goodix_debug_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = 0;
	char *version_info = NULL;
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);
	if (ts == NULL)
		return 0;

	ret = goodix_read_version(ts, &version_info);
	if(ret <= 0)
	{
		vfree(version_info);
		return 0;
	}

	sprintf(buf,"Goodix TouchScreen Version:%s\n",(version_info+1));
	vfree(version_info);
	ret = strlen(buf);
	return ret;
}

/*******************************************************
Description:
	Goodix debug sysfs cat resolution function.

Parameter:
	standard sysfs show param.

return:
	Executive outcomes. 0---failed.
*******************************************************/
static ssize_t goodix_debug_resolution_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;
	int i = 0;
	char *version_info = NULL;
	struct goodix_ts_data *ts;
	struct goodix_i2c_rmi_platform_data *pdata;

	ts = i2c_get_clientdata(i2c_connect_client);
	sprintf(buf,"ABS_X_MAX = %d,ABS_Y_MAX = %d\n",ts->abs_x_max,ts->abs_y_max);

	pdata = ts->client->dev.platform_data;

	while (1)
	{
		i++;
		if (ts->use_irq)
			disable_irq(ts->client->irq);
		else
			hrtimer_cancel(&ts->timer);

		ts->first_irq = true;

		if (ts->power)
		{
			ret = ts->power(ts->client, ts, 0);
			if (ret < 0)
				dev_err(&ts->client->dev, "goodix_ts_resume power off failed\n");
		}

		msleep(200);

		if (ts->power)
		{
			ret = ts->power(ts->client, ts, 1);
			if (ret < 0)
				dev_err(&ts->client->dev, "goodix_ts_resume power on failed\n");
		}

		if (ts->use_irq)
			enable_irq(ts->client->irq);
		else
			hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

		msleep(200);

		ret = goodix_read_version(ts, &version_info);
		if (ret <= 0)
		{
			dev_err(&ts->client->dev, "%s : [%d] failed\n",__func__, i);
			break;
		}
		else
			dev_err(&ts->client->dev, "%s : [%d] ok\n",__func__, i);

		if (i > 100000)
			break;
		msleep(2000);
    }

	return strlen(buf);
}

/*******************************************************
Description:
	Goodix debug sysfs cat version function.

Parameter:
	standard sysfs show param.

return:
	Executive outcomes. 0---failed.
*******************************************************/
static ssize_t goodix_debug_diffdata_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int i;
	int ret = -1;
	int short_tmp;
	struct goodix_ts_data *ts;
	char diff_data_cmd[2] = {80, 202};
	unsigned char diff_data[2241] = {00,};
	struct goodix_i2c_rmi_platform_data *pdata;

	ts = i2c_get_clientdata(i2c_connect_client);
	pdata = ts->client->dev.platform_data;

	ts->first_irq = true;
	pdata->rst();

	return 0;

#if 1
	disable_irq(ts->client->irq);

	ts = i2c_get_clientdata(i2c_connect_client);
	ret = i2c_write_bytes(ts->client, diff_data_cmd, 2);
	if (ret != 1)
	{
		dev_err(&ts->client->dev, "Write diff data cmd failed!\n");
		enable_irq(ts->client->irq);
		return 0;
	}

	while (gpio_get_value(pdata->irq_gpio)) ;
	ret = i2c_read_bytes(ts->client, diff_data, sizeof(diff_data));
	if (ret != 2)
	{
		dev_err(&ts->client->dev, "Read diff data failed!\n");
		enable_irq(ts->client->irq);
		return 0;
	}
	for (i = 1; i < sizeof(diff_data); i += 2)
	{
		short_tmp = diff_data[i] + (diff_data[i+1] << 8);

		if(short_tmp & 0x8000)
			short_tmp -= 65535;
		if(short_tmp == 512)
			continue;

		sprintf(buf+strlen(buf)," %d",short_tmp);
	}

	diff_data_cmd[1] = 0;
	ret = i2c_write_bytes(ts->client, diff_data_cmd, 2);
	if (ret != 1)
	{
		dev_err(&ts->client->dev, "Write diff data cmd failed!\n");
		enable_irq(ts->client->irq);
		return 0;
	}
	enable_irq(ts->client->irq);
#endif
	return strlen(buf);
}

/*******************************************************
Description:
	Goodix debug sysfs echo calibration function.

Parameter:
	standard sysfs store param.

return:
	Executive outcomes..
*******************************************************/
static ssize_t goodix_debug_calibration_store(struct device *dev,
			struct device_attribute *attr, const char *buf, ssize_t count)
{
	int ret = -1;
	char cal_cmd_buf[] = {110,1};
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);

	if ((*buf == 10) || (*buf == 49))
	{
		ret = i2c_write_bytes(ts->client,cal_cmd_buf,2);
		if (ret!=1)
		{
			dev_err(&ts->client->dev,"Calibration failed!\n");
			return count;
		}
	}

	return count;
}

static DEVICE_ATTR(version, S_IRUGO, goodix_debug_version_show, NULL);
static DEVICE_ATTR(resolution, S_IRUGO, goodix_debug_resolution_show, NULL);
static DEVICE_ATTR(diffdata, S_IRUGO, goodix_debug_diffdata_show, NULL);
static DEVICE_ATTR(calibration, S_IWUSR , NULL, goodix_debug_calibration_store);

static struct attribute *goodix_touch_attrs[] = {
	&dev_attr_version.attr,
	&dev_attr_resolution.attr,
	&dev_attr_diffdata.attr,
	&dev_attr_calibration.attr,
	NULL
};

static const struct attribute_group goodix_touch_attr_group = {
	.attrs = goodix_touch_attrs,
};
/********************************************************
Description:
	Goodix debug sysfs init function.

Parameter:
	none.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int goodix_debug_sysfs_init(void)
{
	int ret ;

	ret = sysfs_create_group(&i2c_connect_client->dev.kobj, &goodix_touch_attr_group);
	if (ret)
	{
		dev_err(&i2c_connect_client->dev, "%s: sysfs_create_group failed\n", __func__);
		return ret;
	}

	return 0 ;
}

static void goodix_debug_sysfs_deinit(void)
{
	sysfs_remove_group(&i2c_connect_client->dev.kobj, &goodix_touch_attr_group);
}

/*******************************************************
Description:
	Goodix touchscreen probe function.

Parameter:
	client:	i2c device struct.
	id:device id.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	int retry = 0;
	struct goodix_ts_data *ts;
	const char irq_table[4] = {IRQ_TYPE_EDGE_RISING,
							   IRQ_TYPE_EDGE_FALLING,
							   IRQ_TYPE_LEVEL_LOW,
							   IRQ_TYPE_LEVEL_HIGH};

	struct goodix_i2c_rmi_platform_data *pdata;

	if (!client->dev.platform_data){
		pr_err("%s:platform_data is NULL\n",__func__);
		return -1;
	}

	pdata = client->dev.platform_data;
	if (pdata->gpio_init) pdata->gpio_init();

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		dev_err(&client->dev, "%s Must have I2C_FUNC_I2C.\n",__func__);
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL)
	{
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	i2c_connect_client = client;

	INIT_WORK(&ts->work, goodix_ts_work_func);
	ts->client = client;
	ts->use_irq = 1;
	ts->first_irq = true;
	i2c_set_clientdata(client, ts);

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL)
	{
		ret = -ENOMEM;
		dev_err(&client->dev,"%s Failed to allocate input device\n",__func__);
		goto err_input_dev_alloc_failed;
	}

	goodix_ts_power(client, ts, 1);
	//pdata->rst();
	for (retry = 0; retry < 3; retry++)
	{
		ret = goodix_init_panel(ts);
		msleep(2);
		if (ret != 0)
			continue;
		else
			break;
	}
	if (ret != 0)
	{
		ts->bad_data = 1;
		goto err_init_godix_ts;
	}

	client->irq = pdata->irq;
	pdata->irq_init();
	//pdata->rst();

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
	//ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	//ts->input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);		// absolute coor (x,y)

#ifdef HAVE_TOUCH_KEY
	for (retry = 0; retry < MAX_KEY_NUM; retry++)
	{
		input_set_capability(ts->input_dev,EV_KEY,tp_key_array2[retry]);
	}
#endif

	//input_set_abs_params(ts->input_dev, ABS_X, 0, ts->abs_x_max, 0, 0);
	//input_set_abs_params(ts->input_dev, ABS_Y, 0, ts->abs_y_max, 0, 0);
//	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);

#ifdef GOODIX_MULTI_TOUCH
        input_mt_init_slots(ts->input_dev, ts->max_touch_num);
//	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
//	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max, 0, 0);
//	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, ts->max_touch_num, 0, 0);
#endif

	sprintf(ts->phys, "input/ts");
	ts->input_dev->name = goodix_ts_name;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 10427;	//screen firmware version

	ret = input_register_device(ts->input_dev);
	if (ret)
	{
		dev_err(&client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}
	ts->bad_data = 0;

#ifdef GOODIX_USE_IRQ
	if (client->irq)
	{
		ts->first_irq = true;
		ret  = request_irq(client->irq, goodix_ts_irq_handler ,  irq_table[ts->int_trigger_type],
			client->name, ts);
		if (ret != 0)
		{
			dev_err(&client->dev,"%s : Cannot allocate ts INT!ERRNO:%d\n", __func__, ret);
			gpio_direction_input(pdata->irq_gpio);
			gpio_free(pdata->irq_gpio);
			goto err_gpio_request_failed;
		}
		else
		{
			disable_irq(client->irq);
			ts->use_irq = 1;
		}
	}
#endif
//    INIT_DELAYED_WORK(&led_work,close_key_led);

err_gpio_request_failed:
	if (!ts->use_irq)
	{
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = goodix_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

	if(ts->use_irq)
		enable_irq(client->irq);
#if defined(GOODIX_USE_IRQ)
	if(ts->use_irq)
		ts->power = goodix_ts_power;
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = goodix_ts_early_suspend;
	ts->early_suspend.resume = goodix_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	goodix_proc_entry = create_proc_entry("goodix-update", 0666, NULL);
	if (goodix_proc_entry == NULL)
	{
		dev_err(&client->dev, "%s : Couldn't create proc entry!\n",__func__);
		ret = -ENOMEM;
		goto err_create_proc_entry;
	}
	else
	{
		goodix_proc_entry->write_proc = goodix_update_write;
		goodix_proc_entry->read_proc = goodix_update_read;
	//	goodix_proc_entry->owner =THIS_MODULE;
	}
#endif
	goodix_debug_sysfs_init();
	return 0;

err_init_godix_ts:
	if (ts->use_irq)
	{
		ts->use_irq = 0;
		free_irq(client->irq,ts);
#ifdef GOODIX_USE_IRQ
	//	gpio_direction_input(pdata->irq_gpio);
	//	gpio_free(pdata->irq_gpio;
#endif
	}
	else
		hrtimer_cancel(&ts->timer);

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	i2c_set_clientdata(client, NULL);
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
err_create_proc_entry:
	return ret;
}

/*******************************************************
Description:
	Goodix touchscreen driver release function.

Parameter:
	client:	i2c device struct.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int goodix_ts_remove(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);
	struct goodix_i2c_rmi_platform_data *pdata = ts->client->dev.platform_data;
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	remove_proc_entry("goodix-update", NULL);
#endif
	goodix_debug_sysfs_deinit();
	if (ts && ts->use_irq)
	{
#ifdef GOODIX_USE_IRQ
		gpio_direction_input(pdata->irq_gpio);
		gpio_free(pdata->irq_gpio);
#endif
		free_irq(client->irq, ts);
	}
	else if(ts)
		hrtimer_cancel(&ts->timer);

	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

static int goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret, i;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->use_irq)
		disable_irq(client->irq);
	else
		hrtimer_cancel(&ts->timer);

	ts->first_irq = true;

	if (ts->power)
	{
		ret = ts->power(client, ts, 0);
		if (ret < 0)
			dev_err(&ts->client->dev, "goodix_ts_resume power off failed\n");
	}

        for (i=0; i< ts->max_touch_num; i++) {
                if (ts->fingerbits & (1<<i)) {
                        input_mt_slot(ts->input_dev, i);
                        input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER,false);
                }
        }

//    cancel_delayed_work_sync(&led_work);
//    close_key_led();
	return 0;
}

static int goodix_ts_resume(struct i2c_client *client)
{
	int ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->power)
	{
		ret = ts->power(client, ts, 1);
		if (ret < 0)
			dev_err(&ts->client->dev, "goodix_ts_resume power on failed\n");
	}

	if (ts->use_irq)
		enable_irq(client->irq);
	else
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h)
{
	struct goodix_ts_data *ts;

	ts = container_of(h, struct goodix_ts_data, early_suspend);
	goodix_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void goodix_ts_late_resume(struct early_suspend *h)
{
	struct goodix_ts_data *ts;

	ts = container_of(h, struct goodix_ts_data, early_suspend);
	goodix_ts_resume(ts->client);
}
#endif

//******************************Begin of firmware update surpport*******************************
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
/**
@brief CRC cal proc,include : Reflect,init_crc32_table,GenerateCRC32
@param global var oldcrc32
@return states
*/
static unsigned int Reflect(unsigned long int ref, char ch)
{
	int i;
	unsigned int value=0;

	for (i = 1; i < (ch + 1); i++)
	{
		if (ref & 1)
			value |= 1 << (ch - i);
		ref >>= 1;
	}
	return value;
}

/*---------------------------------------------------------------------------------------------------------*/
/*  CRC Check Program INIT                                                                                 */
/*---------------------------------------------------------------------------------------------------------*/
static void init_crc32_table(void)
{
	int i,j;
	unsigned int temp;
	unsigned int t1,t2;
	unsigned int flag;

	for (i = 0; i <= 0xFF; i++)
	{
		temp = Reflect(i, 8);
		crc32_table[i] = temp << 24;
		for (j = 0; j < 8; j++)
		{
			flag = crc32_table[i] & 0x80000000;
			t1 = (crc32_table[i] << 1);
			if (flag == 0)
				t2 = 0;
			else
				t2 = ulPolynomial;
			crc32_table[i] = t1^t2;
		}
		crc32_table[i] = Reflect(crc32_table[i], 32);
	}
}

/*---------------------------------------------------------------------------------------------------------*/
/*  CRC main Program                                                                                       */
/*---------------------------------------------------------------------------------------------------------*/
static void GenerateCRC32(unsigned char * buf, unsigned int len)
{
	unsigned int i;
	unsigned int t;

	for (i = 0; i != len; ++i)
	{
		t = (oldcrc32 ^ buf[i]) & 0xFF;
		oldcrc32 = ((oldcrc32 >> 8) & 0xFFFFFF) ^ crc32_table[t];
	}
}

static struct file * update_file_open(char * path, mm_segment_t * old_fs_p)
{
	int errno = -1;
	struct file * filp = NULL;

	filp = filp_open(path, O_RDONLY, 0644);

	if (!filp || IS_ERR(filp))
	{
		if (!filp)
			errno = -ENOENT;
		else
			errno = PTR_ERR(filp);

		printk(KERN_ERR "The update file for Guitar open error.\n");
		return NULL;
	}
	*old_fs_p = get_fs();
	set_fs(get_ds());

	filp->f_op->llseek(filp,0,0);
	return filp ;
}

static void update_file_close(struct file * filp, mm_segment_t old_fs)
{
	set_fs(old_fs);
	if (filp)
		filp_close(filp, NULL);
}
static int update_get_flen(char * path)
{
	int length ;
	struct file * file_ck = NULL;
	mm_segment_t old_fs;

	file_ck = update_file_open(path, &old_fs);
	if (file_ck == NULL)
		return 0;

	length = file_ck->f_op->llseek(file_ck, 0, SEEK_END);
	if(length < 0)
		length = 0;

	update_file_close(file_ck, old_fs);
	return length;
}
static int update_file_check(char * path)
{
	int count, ret, length;
	unsigned char buffer[64] = { 0 };
	struct file * file_ck = NULL;
	mm_segment_t old_fs;

	file_ck = update_file_open(path, &old_fs);

	if (path != NULL)
		printk(KERN_ERR "File Path:%s\n", path);

	if (file_ck == NULL)
		return -ERROR_NO_FILE;

	length = file_ck->f_op->llseek(file_ck, 0, SEEK_END);

	if (length <= 0 || (length%4) != 0)
	{
		update_file_close(file_ck, old_fs);
		return -ERROR_FILE_TYPE;
	}

	//set file point to the begining of the file
	file_ck->f_op->llseek(file_ck, 0, SEEK_SET);
	oldcrc32 = 0xFFFFFFFF;
	init_crc32_table();

	while (length > 0)
	{
		ret = file_ck->f_op->read(file_ck, buffer, sizeof(buffer), &file_ck->f_pos);
		if (ret > 0)
		{
			for (count = 0; count < ret;  count++)
				GenerateCRC32(&buffer[count],1);
		}
		else
		{
			update_file_close(file_ck, old_fs);
			return -ERROR_FILE_READ;
		}
		length -= ret;
	}
	oldcrc32 = ~oldcrc32;

	update_file_close(file_ck, old_fs);
	return 1;
}

unsigned char wait_slave_ready(struct goodix_ts_data *ts, unsigned short *timeout)
{
	int ret;
	unsigned char i2c_state_buf[2] = {ADDR_STA, UNKNOWN_ERROR};

	while (*timeout < MAX_TIMEOUT)
	{
		ret = i2c_read_bytes(ts->client, i2c_state_buf, 2);
		if (ret <= 0)
			return ERROR_I2C_TRANSFER;
		if (i2c_state_buf[1] & SLAVE_READY)
		{
			return i2c_state_buf[1];
		}
		msleep(10);
		*timeout += 5;
	}

	return 0;
}

static int goodix_update_write(struct file *filp, const char __user *buff, unsigned long len, void *data)
{
	int ret = -1;
	unsigned int rd_len;
	unsigned char i = 0;
	unsigned char cmd[220];
	unsigned char retries = 0;
	unsigned char i2c_rd_buf[160];
	static unsigned int file_len = 0;
	static unsigned short time_count = 0;
	static unsigned char update_path[100];
	static unsigned char update_need_config = 0;
	unsigned char i2c_states_buf[2] = {ADDR_STA, 0};
	unsigned char i2c_control_buf[2] = {ADDR_CMD, 0};
	unsigned char i2c_data_buf[PACK_SIZE+1+8] = {ADDR_DAT,};

	unsigned char checksum_error_times = 0;
#ifdef UPDATE_NEW_PROTOCOL
	unsigned int frame_checksum = 0;
	unsigned int frame_number = 0;
#else
	unsigned char send_crc = 0;
#endif

	struct file * file_data = NULL;
	mm_segment_t old_fs;
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);
	if (ts == NULL)
		return 0;

	if (copy_from_user(&cmd, buff, len))
	{
		return -EFAULT;
	}

	switch (cmd[0])
	{
		case STEP_SET_PATH:
			memset(update_path, 0, 100);
			strncpy(update_path, cmd+1, 100);
			if (update_path[0] == 0)
				return 0;
			else
				return 1;
		case STEP_CHECK_FILE:
			ret = update_file_check(update_path);
			if (ret <= 0)
			{
				dev_err(&ts->client->dev, "%s : fialed to check update file!\n",__func__);
				return ret;
			}
			msleep(500);
			return 1;
		case STEP_WRITE_SYN:
			i2c_control_buf[1] = UPDATE_START;
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);
			if (ret <= 0)
			{
				ret = ERROR_I2C_TRANSFER;
				return ret;
			}
			//the time include time(APROM -> LDROM) and time(LDROM init)
			msleep(1000);
			return 1;
		case STEP_WAIT_SYN:
			while (retries < MAX_I2C_RETRIES)
			{
				i2c_states_buf[1] = UNKNOWN_ERROR;
				ret = i2c_read_bytes(ts->client, i2c_states_buf, 2);
				if (i2c_states_buf[1] & UPDATE_START)
				{
					if (i2c_states_buf[1] & NEW_UPDATE_START)
					{
					#ifdef UPDATE_NEW_PROTOCOL
						update_need_config = 1;
						return 2;
					#else
						return 1;
					#endif
					}
					break;
				}
				msleep(5);
				retries++;
				time_count += 10;
			}
			if ((retries >= MAX_I2C_RETRIES) && (!(i2c_states_buf[1] & UPDATE_START)))
			{
				if (ret <= 0)
					return 0;
				else
					return -1;
			}
			return 1;
		case STEP_WRITE_LENGTH:
			file_len = update_get_flen(update_path);
			if (file_len <= 0)
			{
				dev_err(&ts->client->dev, "%s : get update file length failed!\n",__func__);
				return -1;
			}
			file_len += 4;
			i2c_data_buf[1] = (file_len>>24) & 0xff;
			i2c_data_buf[2] = (file_len>>16) & 0xff;
			i2c_data_buf[3] = (file_len>>8) & 0xff;
			i2c_data_buf[4] = file_len & 0xff;
			file_len -= 4;

			ret = i2c_write_bytes(ts->client, i2c_data_buf, 5);
			if (ret <= 0)
			{
				ret = ERROR_I2C_TRANSFER;
				return 0;
			}
			return 1;
		case STEP_WAIT_READY:
			ret = wait_slave_ready(ts, &time_count);
			if (ret == ERROR_I2C_TRANSFER)
				return 0;

			if (!ret)
			{
				return -1;
			}

			return 1;
		case STEP_WRITE_DATA:
#ifdef UPDATE_NEW_PROTOCOL
			file_data = update_file_open(update_path, &old_fs);
			if (file_data == NULL)
			{
				return -1;
			}
			frame_number = 0;
			while (file_len >= 0)
			{
				i2c_data_buf[0] = ADDR_DAT;
				rd_len = (file_len >= PACK_SIZE) ? PACK_SIZE : file_len;
				frame_checksum = 0;
				if (file_len)
				{
					ret = file_data->f_op->read(file_data, i2c_data_buf+1+4, rd_len, &file_data->f_pos);
					if(ret <= 0)
					{
						dev_err(&ts->client->dev, "[GOODiX_ISP_NEW]:Read File Data Failed!\n");
						return -1;
					}
					i2c_data_buf[1] = (frame_number>>24)&0xff;
					i2c_data_buf[2] = (frame_number>>16)&0xff;
					i2c_data_buf[3] = (frame_number>>8)&0xff;
					i2c_data_buf[4] = frame_number&0xff;
					frame_number++;
					frame_checksum = 0;

					for (i = 0; i < rd_len; i++)
					{
						frame_checksum += i2c_data_buf[5+i];
					}
					frame_checksum = 0 - frame_checksum;
					i2c_data_buf[5+rd_len+0] = frame_checksum&0xff;
					i2c_data_buf[5+rd_len+1] = (frame_checksum>>8)&0xff;
					i2c_data_buf[5+rd_len+2] = (frame_checksum>>16)&0xff;
					i2c_data_buf[5+rd_len+3] = (frame_checksum>>24)&0xff;
				}
rewrite:
				ret = i2c_write_bytes(ts->client, i2c_data_buf, 1+4+rd_len+4);
				if (ret != 1)
				{
					dev_err(&ts->client->dev, "[GOODiX_ISP_NEW]:Write File Data Failed!Return:%d\n", ret);
					return 0;
				}

				memset(i2c_rd_buf, 0x00, 1+4+rd_len+4);
				ret = i2c_read_bytes(ts->client, i2c_rd_buf, 1+4+rd_len+4);
				if (ret != 2)
				{
					dev_err(&ts->client->dev, "[GOODiX_ISP_NEW]:Read File Data Failed!Return:%d\n", ret);
					return 0;
				}

				for (i = 1; i < (1+4+rd_len+4); i++)	//check communication
				{
					if (i2c_rd_buf[i] != i2c_data_buf[i])
					{
						i = 0;
						break;
					}
				}
				if (!i)
				{
					i2c_control_buf[0] = ADDR_CMD;
					i2c_control_buf[1] = 0x03;
					i2c_write_bytes(ts->client, i2c_control_buf, 2);		//communication error
					dev_err(&ts->client->dev, "[GOODiX_ISP_NEW]:File Data Frame readback check Error!\n");
				}
				else
				{
					i2c_control_buf[1] = 0x04;	//let LDROM write flash
					i2c_write_bytes(ts->client, i2c_control_buf, 2);
				}

				//Wait for slave ready signal.and read the checksum
				ret = wait_slave_ready(ts, &time_count);
				if ((ret & CHECKSUM_ERROR) || (!i))
				{
					if (i)
						dev_err(&ts->client->dev, "[GOODiX_ISP_NEW]:File Data Frame checksum Error!\n");

					checksum_error_times++;
					msleep(20);
					if(checksum_error_times > 20)	//max retry times.
						return 0;
					goto rewrite;
				}

				checksum_error_times = 0;
				if (ret & (FRAME_ERROR))
				{
					dev_err(&ts->client->dev, "[GOODiX_ISP_NEW]:File Data Frame Miss!\n");
					return 0;
				}

				if (ret == ERROR_I2C_TRANSFER)
					return 0;

				if (!ret)
				{
					return -1;
				}

				if (file_len < PACK_SIZE)
				{
					update_file_close(file_data, old_fs);
					break;
				}
				file_len -= rd_len;
			}	//end of while((file_len >= 0))
			return 1;
#else
			file_data = update_file_open(update_path, &old_fs);
			if (file_data == NULL)	//file_data has been opened at the last time
			{
				return -1;
			}

			while ((file_len >= 0) && (!send_crc))
			{
				i2c_data_buf[0] = ADDR_DAT;
				rd_len = (file_len >= PACK_SIZE) ? PACK_SIZE : file_len;
				if (file_len)
				{
					ret = file_data->f_op->read(file_data, i2c_data_buf+1, rd_len, &file_data->f_pos);
					if (ret <= 0)
					{
						return -1;
					}
				}
				if (file_len < PACK_SIZE)
				{
					send_crc = 1;
					update_file_close(file_data, old_fs);
					i2c_data_buf[file_len+1] = oldcrc32&0xff;
					i2c_data_buf[file_len+2] = (oldcrc32>>8)&0xff;
					i2c_data_buf[file_len+3] = (oldcrc32>>16)&0xff;
					i2c_data_buf[file_len+4] = (oldcrc32>>24)&0xff;
					ret = i2c_write_bytes(ts->client, i2c_data_buf, (file_len+1+4));
					if (ret != 1)
					{
						dev_err(&ts->client->dev, "[GOODiX_ISP_OLD]:Write File Data Failed!Return:%d\n", ret);
						return 0;
					}
					break;
				}
				else
				{
					ret = i2c_write_bytes(ts->client, i2c_data_buf, PACK_SIZE+1);
					if (ret != 1)
					{
						dev_err(&ts->client->dev, "[GOODiX_ISP_OLD]:Write File Data Failed!Return:%d\n", ret);
						return 0;
					}
				}
				file_len -= rd_len;

				//Wait for slave ready signal.
				ret = wait_slave_ready(ts, &time_count);
				if (ret == ERROR_I2C_TRANSFER)
					return 0;
				if (!ret)
				{
					return -1;
				}
				//Slave is ready.
			} //end of while((file_len >= 0) && (!send_crc))
			return 1;
#endif
		case STEP_READ_STATUS:
			while (time_count < MAX_TIMEOUT)
			{
				ret = i2c_read_bytes(ts->client, i2c_states_buf, 2);
				if (ret <= 0)
				{
					return 0;
				}
				if (i2c_states_buf[1] & SLAVE_READY)
				{
					if (!(i2c_states_buf[1] &0xf0))
					{
						return 1;
					}
					else
					{
						dev_err(&ts->client->dev, "The firmware updating failed!update state:0x%x\n",i2c_states_buf[1]);
						return 0;

					}
				}
				msleep(1);
				time_count += 5;
			}
			return -1;
		case FUN_CLR_VAL:	//clear the static val
			time_count = 0;
			file_len = 0;
			update_need_config = 0;
			return 1;
		case FUN_CMD:	//functional command
			if (cmd[1] == CMD_DISABLE_TP)
			{
				g_enter_isp = 1;
				if (ts->use_irq)
					disable_irq(ts->client->irq);
			}
			else if (cmd[1] == CMD_ENABLE_TP)
			{
				g_enter_isp = 0;
				if (ts->use_irq)
					enable_irq(ts->client->irq);
			}
			else if (cmd[1] == CMD_READ_VER)
			{
				ts->read_mode = MODE_RD_VER;
			}
			else if (cmd[1] == CMD_READ_RAW)
			{
				ts->read_mode = MODE_RD_RAW;
				i2c_control_buf[1] = 201;
				ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);	//read raw data cmd
				if(ret <= 0)
				{
					dev_err(&ts->client->dev, "Write read raw data cmd failed!\n");
					return 0;
				}
				msleep(200);
			}
			else if (cmd[1] == CMD_READ_DIF)
			{
				ts->read_mode = MODE_RD_DIF;
				i2c_control_buf[1] = 202;
				ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);	//read diff data cmd
				if	(ret <= 0)
				{
					dev_err(&ts->client->dev, "Write read raw data cmd failed!\n");
					return 0;
				}
				msleep(200);
			}
			else if (cmd[1] == CMD_READ_CFG)
			{
				ts->read_mode = MODE_RD_CFG;
				rd_cfg_addr = cmd[2];
				rd_cfg_len = cmd[3];
			}
			else if (cmd[1] == CMD_SYS_REBOOT)
			{
#ifdef CONFIG_TOUCHSCREEN_GOODIX_MODULE
				emergency_sync();
#else
				sys_sync();
#endif
				msleep(200);
				kernel_restart(NULL);
			}
			return 1;
		case FUN_WRITE_CONFIG:

			if ((cmd[2] > 83) && (cmd[2] < 240) && cmd[1])
			{
				checksum_error_times = 0;
reconfig:
				ret = i2c_write_bytes(ts->client, cmd+2, cmd[1]);
				if (ret != 1)
				{
					dev_err(&ts->client->dev, "Write Config failed!return:%d\n",ret);
					return -1;
				}
				if (!update_need_config)
					return 1;

				i2c_rd_buf[0] = cmd[2];
				ret = i2c_read_bytes(ts->client, i2c_rd_buf, cmd[1]);
				if (ret != 2)
				{
					dev_err(&ts->client->dev, "Read Config failed!return:%d\n",ret);
					return -1;
				}
				for (i = 0; i < cmd[1]; i++)
				{
					if (i2c_rd_buf[i] != cmd[i+2])
					{
						dev_err(&ts->client->dev, "Config readback check failed!\n");
						i = 0;
						break;
					}
				}
				if (!i)
				{
					i2c_control_buf[0] = ADDR_CMD;
					i2c_control_buf[1] = 0x03;
					i2c_write_bytes(ts->client, i2c_control_buf, 2);	//communication error
					checksum_error_times++;
					msleep(20);
					if (checksum_error_times > 20)	//max retry times.
						return 0;
					goto reconfig;
				}
				else
				{
					i2c_control_buf[0] = ADDR_CMD;
					i2c_control_buf[1] = 0x04;	//let LDROM write flash
					i2c_write_bytes(ts->client, i2c_control_buf, 2);
					return 1;
				}

			}
			else
			{
				dev_err(&ts->client->dev, "Invalid config addr!\n");
				return -1;
			}
		default:
			return -ENOSYS;
	}
	return 0;
}

static int goodix_update_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
	int ret = -1;
	struct goodix_ts_data *ts;
	int len = 0;
	char *version_info = NULL;
	unsigned char read_data[1201] = {80, };

	ts = i2c_get_clientdata(i2c_connect_client);
	if (ts == NULL)
		return 0;

	if (ts->read_mode == MODE_RD_VER)	//read version data
	{
		ret = goodix_read_version(ts, &version_info);
		if (ret <= 0)
		{
			dev_err(&ts->client->dev, "Read version data failed!\n");
			vfree(version_info);
			return 0;
		}

		for (len = 0;len < 100; len++)
		{
			if (*(version_info + len) == '\0')
				break;
		}
		strncpy(page, version_info+1, len + 1);
		vfree(version_info);
		*eof = 1;
		return len + 1;
	}
	else if ((ts->read_mode == MODE_RD_RAW) || (ts->read_mode == MODE_RD_DIF))	//read raw data or diff
	{
		ret = i2c_read_bytes(ts->client, read_data, 1201);
		if (ret <= 0)
		{
			if (ts->read_mode == 2)
				dev_err(&ts->client->dev, "Read raw data failed!\n");
			if( ts->read_mode == 3)
				dev_err(&ts->client->dev, "Read diff data failed!\n");
			return 0;
		}
		memcpy(page, read_data+1, 1200);
		*eof = 1;
		*start = NULL;
		return 1200;
	}
	else if (ts->read_mode == MODE_RD_CFG)
	{
		if ((rd_cfg_addr > 83) && (rd_cfg_addr < 240))
		{
			read_data[0] = rd_cfg_addr;
		}
		else
		{
			read_data[0] = 101;
		}
		if ((rd_cfg_len < 0) || (rd_cfg_len > 156))
		{
			rd_cfg_len = 239 - read_data[0];
		}

		ret = i2c_read_bytes(ts->client, read_data, rd_cfg_len);
		if (ret <= 0)
		{
			dev_err(&ts->client->dev, "Read config info failed!\n");
			return 0;
		}
		memcpy(page, read_data+1, rd_cfg_len);
		return rd_cfg_len;
	}
	return len;
}
#endif

//******************************End of firmware update surpport*******************************
static const struct i2c_device_id goodix_ts_id[] = {
	{ GOODIX_I2C_NAME, 0 },
	{ }
};

static struct i2c_driver goodix_ts_driver = {
	.probe		= goodix_ts_probe,
	.remove		= goodix_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= goodix_ts_suspend,
	.resume		= goodix_ts_resume,
#endif
	.id_table	= goodix_ts_id,
	.driver = {
		.name	= GOODIX_I2C_NAME,
		.owner = THIS_MODULE,
	},
};

/*******************************************************
Description:
	Driver Install function.
return:
	Executive Outcomes. 0---succeed.
********************************************************/
static int __devinit goodix_ts_init(void)
{
	int ret;

	goodix_wq = create_workqueue("goodix_wq");	//create a work queue and worker thread
	if (!goodix_wq)
		return -ENOMEM;

	ret = i2c_add_driver(&goodix_ts_driver);
	return ret;
}

/*******************************************************
Description:
	Driver uninstall function.
return:
	Executive Outcomes. 0---succeed.
********************************************************/
static void __exit goodix_ts_exit(void)
{
	i2c_del_driver(&goodix_ts_driver);
	if (goodix_wq)
		destroy_workqueue(goodix_wq);	//release our work queue
}

late_initcall(goodix_ts_init);
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");
