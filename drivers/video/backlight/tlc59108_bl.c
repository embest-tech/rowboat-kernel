/*
 *  Generic Backlight Driver
 *
 *  Copyright (c) 2004-2008 Richard Purdie
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/errno.h>

#define DEVICE_NAME "tlc59108-backlight"

struct tlc59108_backlight_data {
	struct i2c_client	*client;
	int		current_brightness;
};

static int tlc59108_bl_intensity;
static struct backlight_device *generic_backlight_device;
static struct generic_bl_info *bl_machinfo;
static int tlc59108_bl_set(struct i2c_client *client, int brightness);


static int tlc59108_bl_send_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;
	struct tlc59108_backlight_data *board_data = bl_get_data(bd);
	struct i2c_client *client = board_data->client;

	if (bd->props.power != FB_BLANK_UNBLANK)
		intensity = 0;
	if (bd->props.state & BL_CORE_FBBLANK)
		intensity = 0;
	if (bd->props.state & BL_CORE_SUSPENDED)
		intensity = 0;

	tlc59108_bl_set(client, intensity);
	tlc59108_bl_intensity = intensity;

	if (bl_machinfo->kick_battery)
		bl_machinfo->kick_battery();

	return 0;
}

static int tlc59108_bl_get_intensity(struct backlight_device *bd)
{
	return tlc59108_bl_intensity;
}

static const struct backlight_ops tlc59108_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = tlc59108_bl_get_intensity,
	.update_status  = tlc59108_bl_send_intensity,
};

#define PER2HEX(percentage)	((256 * percentage)/100)
#define HEX2PER(hex)		((hex/256) * 100)
#define	PWM_MODE		0x2
#define PWM_REG			0x4

static int tlc59108_bl_init_hw(struct i2c_client *client)
{
	s32 ret;
	s32 data;

	if (NULL == client)
		return -EIO;

	ret = i2c_smbus_write_byte_data(client, 0x00, 0x00);
	if (ret < 0)
		return -EIO;

	data = i2c_smbus_read_byte_data(client, 0x0C);
	if (data < 0)
		return -EIO;

	data &= ~0x30;
	data |= (PWM_MODE << 4);

	ret = i2c_smbus_write_byte_data(client, 0x0C, (u8)data);
	if (ret < 0)
		return -EIO;

	/* Set the brightness to 80% */
	ret = tlc59108_bl_set(client, 80);
	if (ret < 0) {
		printk(KERN_ERR "Unable to set the default brightness.\n");
		return -EIO;
	}

	return 0;
}

/* brightness from 1 to 100 */
static int tlc59108_bl_set(struct i2c_client *client, int brightness)
{
	u32 hex;
	s32 ret;

	if (brightness < 0)
		brightness = 0;
	if (brightness > 100)
		brightness = 100;

	hex = PER2HEX(brightness);
	if (hex > 0xFF)
		hex = 0xFF;

	ret = i2c_smbus_write_byte_data(client, PWM_REG, (u8)hex);
	if (ret < 0)
		return -EIO;
	return 0;
}

static int tlc59108_bl_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct backlight_properties props;
	struct generic_bl_info *machinfo = client->dev.platform_data;
	const char *name = DEVICE_NAME;
	struct backlight_device *bd;
	struct tlc59108_backlight_data *data;

	data = kzalloc(sizeof(struct tlc59108_backlight_data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	data->client = client;
	data->current_brightness = machinfo->default_intensity;

	bl_machinfo = machinfo;
	if (!machinfo->limit_mask)
		machinfo->limit_mask = -1;

	if (machinfo->name)
		name = machinfo->name;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = machinfo->max_intensity;
	bd = backlight_device_register(name, &client->dev, data,
						&tlc59108_bl_ops, &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	i2c_set_clientdata(client, bd);

	bd->props.power = FB_BLANK_UNBLANK;
	bd->props.brightness = machinfo->default_intensity;

	if (tlc59108_bl_init_hw(client) < 0)
		goto err;

	backlight_update_status(bd);

	generic_backlight_device = bd;

	printk(KERN_INFO "Generic Backlight Driver Initialized.\n");
	return 0;
err:
	backlight_device_unregister(bd);
	return -EIO;
}

static int tlc59108_bl_remove(struct i2c_client *client)
{
	struct backlight_device *bd = i2c_get_clientdata(client);

	bd->props.power = 0;
	bd->props.brightness = 0;
	backlight_update_status(bd);

	backlight_device_unregister(bd);

	printk(KERN_INFO "tlc59108 backlight driver unloaded\n");
	return 0;
}

static const struct i2c_device_id tlc59108_bl_idtable[] = {
	{ DEVICE_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tlc59108_bl_idtable);

static struct i2c_driver tlc59108_bl_driver = {
	.probe		= tlc59108_bl_probe,
	.remove		= tlc59108_bl_remove,
	.id_table	= tlc59108_bl_idtable,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= DEVICE_NAME,
	},
};

static int __init tlc59108_bl_init(void)
{
	return i2c_add_driver(&tlc59108_bl_driver);
}

static void __exit tlc59108_bl_exit(void)
{
	i2c_del_driver(&tlc59108_bl_driver);
}

module_init(tlc59108_bl_init);
module_exit(tlc59108_bl_exit);

MODULE_AUTHOR("Ratheesh S<ratheesh@mistralsolutions.com>");
MODULE_DESCRIPTION("TLC59108 Backlight Driver");
MODULE_LICENSE("GPL");
