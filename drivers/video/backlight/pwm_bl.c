/*
 * linux/drivers/video/backlight/pwm_bl.c
 *
 * simple PWM based backlight control, board code has to setup
 * 1) pin configuration so PWM waveforms can output
 * 2) platform_data being correctly configured
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/slab.h>

#include <linux/i2c/bq2416x.h>

#ifdef CONFIG_SMARTQ_S7
#include <linux/notifier.h>
#include <linux/delay.h>

static int flag_battery_low = 0;
static struct backlight_device *global_bl;
#endif

#if 0//defined(CONFIG_SMARTQ_T20)
#include "../../../arch/arm/mach-omap2/mux.h"
#include <linux/gpio.h>

#define GPIO_BL_PWM 94
static unsigned char bl_stop = 0;
#endif

struct pwm_bl_data {
	struct pwm_device	*pwm;
	struct device		*dev;
	unsigned int		period;
	unsigned int		lth_brightness;
	int			(*notify)(struct device *,
					  int brightness);
	int			(*check_fb)(struct device *, struct fb_info *);
};

#if defined(CONFIG_CHARGER_BQ2416X) //|| defined(CONFIG_CHARGER_BQ2416X_MODULE)
extern void bq2416x_set_charger_current(int mode);
#endif

static int pwm_backlight_update_status(struct backlight_device *bl)
{
	struct pwm_bl_data *pb = dev_get_drvdata(&bl->dev);
	int brightness = bl->props.brightness;
	int max = bl->props.max_brightness;
        int scale = bl->props.scale;

        if (scale > 8) scale = 0;

	if (bl->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	if (bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;

	if (pb->notify)
		brightness = pb->notify(pb->dev, brightness);

	brightness >>= scale;

#ifdef CONFIG_PANEL_HS_HSD101PWW1
#define SCREEN_DIM 15
#else
#define SCREEN_DIM 5
#endif

	if (scale && 0 < brightness && brightness < SCREEN_DIM)
	    brightness = SCREEN_DIM;

	if (brightness == 0) {
		pwm_config(pb->pwm, 0, pb->period);
		pwm_disable(pb->pwm);
#if 0//defined(CONFIG_SMARTQ_T20)
		if (!pwm->config.polarity) {
			omap_mux_set_gpio(OMAP_MUX_MODE3, GPIO_BL_PWM);
			gpio_direction_output(GPIO_BL_PWM, 1);
			bl_stop = 1;
		}
#endif
#if defined(CONFIG_CHARGER_BQ2416X) //|| defined(CONFIG_CHARGER_BQ2416X_MODULE)
		bq2416x_set_charger_current(SLEEP_MODE);
#endif
	} else {
#if 0//defined(CONFIG_SMARTQ_T20)
		if (bl_stop) {	bl_stop = 0;
			omap_mux_set_gpio(OMAP_MUX_MODE1, GPIO_BL_PWM);
		}
#endif
#ifdef CONFIG_SMARTQ_S7
		brightness -= (((flag_battery_low * 3) * brightness)/ (max * 2) );
#endif
		brightness = pb->lth_brightness +
			(brightness * (pb->period - pb->lth_brightness) / max);
		pwm_config(pb->pwm, brightness, pb->period);
		pwm_enable(pb->pwm);
#if defined(CONFIG_SMARTQ_S7) || defined(CONFIG_SMARTQ_K7)
		pwm_config(pb->pwm, brightness, pb->period);
		pwm_enable(pb->pwm);
#endif
#if defined(CONFIG_CHARGER_BQ2416X) //|| defined(CONFIG_CHARGER_BQ2416X_MODULE)
		bq2416x_set_charger_current(WORK_MODE);
#endif
	}
	return 0;
}

#ifdef CONFIG_SMARTQ_S7
int battery_low_handler(struct notifier_block *self, unsigned long val, void *data)
{
	unsigned int *pData = (unsigned int *)data;
	unsigned int i = 0;
	unsigned int last_battery_low = 0;
	if (*pData)
	{
		i = (*pData % 100);
		last_battery_low = flag_battery_low;
		for (flag_battery_low = last_battery_low; flag_battery_low <= i; flag_battery_low += ((abs(i - last_battery_low) / 20) + 1))
		{
			backlight_update_status(global_bl);
			msleep_interruptible(200);
		}
		flag_battery_low = i;
		backlight_update_status(global_bl);
	}
	else
	{
		i = flag_battery_low;
		for (; flag_battery_low >= 0; flag_battery_low -= ((i / 20) + 1))
		{
			backlight_update_status(global_bl);
			msleep_interruptible(200);
		}
		flag_battery_low = 0;
		backlight_update_status(global_bl);
	}

	return 0;
}
EXPORT_SYMBOL(battery_low_handler);
#endif

static int pwm_backlight_get_brightness(struct backlight_device *bl)
{
	return bl->props.brightness;
}

static int pwm_backlight_check_fb(struct backlight_device *bl,
				  struct fb_info *info)
{
	struct pwm_bl_data *pb = dev_get_drvdata(&bl->dev);

	return !pb->check_fb || pb->check_fb(pb->dev, info);
}

static const struct backlight_ops pwm_backlight_ops = {
	.update_status	= pwm_backlight_update_status,
	.get_brightness	= pwm_backlight_get_brightness,
	.check_fb	= pwm_backlight_check_fb,
};

static int pwm_backlight_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct platform_pwm_backlight_data *data = pdev->dev.platform_data;
	struct backlight_device *bl;
	struct pwm_bl_data *pb;
	int ret;

	if (!data) {
		dev_err(&pdev->dev, "failed to find platform data\n");
		return -EINVAL;
	}

	if (data->init) {
		ret = data->init(&pdev->dev);
		if (ret < 0)
			return ret;
	}

	pb = kzalloc(sizeof(*pb), GFP_KERNEL);
	if (!pb) {
		dev_err(&pdev->dev, "no memory for state\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	pb->period = data->pwm_period_ns;
	pb->notify = data->notify;
	pb->check_fb = data->check_fb;
	pb->lth_brightness = data->lth_brightness *
		(data->pwm_period_ns / data->max_brightness);
	pb->dev = &pdev->dev;

	pb->pwm = pwm_request(data->pwm_id, "backlight");
	if (IS_ERR(pb->pwm)) {
		dev_err(&pdev->dev, "unable to request PWM for backlight\n");
		ret = PTR_ERR(pb->pwm);
		goto err_pwm;
	} else
		dev_dbg(&pdev->dev, "got pwm for backlight\n");

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = data->max_brightness;
	bl = backlight_device_register(dev_name(&pdev->dev), &pdev->dev, pb,
				       &pwm_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		ret = PTR_ERR(bl);
		goto err_bl;
	}

	bl->props.brightness = data->dft_brightness;
	backlight_update_status(bl);	// XXX:
	backlight_update_status(bl);

	platform_set_drvdata(pdev, bl);

#ifdef CONFIG_SMARTQ_S7
	global_bl = bl;
#endif

	return 0;

err_bl:
	pwm_free(pb->pwm);
err_pwm:
	kfree(pb);
err_alloc:
	if (data->exit)
		data->exit(&pdev->dev);
	return ret;
}

static int pwm_backlight_remove(struct platform_device *pdev)
{
	struct platform_pwm_backlight_data *data = pdev->dev.platform_data;
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct pwm_bl_data *pb = dev_get_drvdata(&bl->dev);

	backlight_device_unregister(bl);
	pwm_config(pb->pwm, 0, pb->period);
	pwm_disable(pb->pwm);
	pwm_free(pb->pwm);
	kfree(pb);
	if (data->exit)
		data->exit(&pdev->dev);
	return 0;
}

#ifdef CONFIG_PM
static int pwm_backlight_suspend(struct platform_device *pdev,
				 pm_message_t state)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct pwm_bl_data *pb = dev_get_drvdata(&bl->dev);

	if (pb->notify)
		pb->notify(pb->dev, 0);
	pwm_config(pb->pwm, 0, pb->period);
	pwm_disable(pb->pwm);
	return 0;
}

static int pwm_backlight_resume(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);

	backlight_update_status(bl);
	return 0;
}
#else
#define pwm_backlight_suspend	NULL
#define pwm_backlight_resume	NULL
#endif

static struct platform_driver pwm_backlight_driver = {
	.driver		= {
		.name	= "pwm-backlight",
		.owner	= THIS_MODULE,
	},
	.probe		= pwm_backlight_probe,
	.remove		= pwm_backlight_remove,
	.suspend	= pwm_backlight_suspend,
	.resume		= pwm_backlight_resume,
};

static int __init pwm_backlight_init(void)
{
	return platform_driver_register(&pwm_backlight_driver);
}
fs_initcall(pwm_backlight_init);

static void __exit pwm_backlight_exit(void)
{
	platform_driver_unregister(&pwm_backlight_driver);
}
module_exit(pwm_backlight_exit);

MODULE_DESCRIPTION("PWM based Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pwm-backlight");
