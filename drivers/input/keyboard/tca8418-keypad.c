/*
 * TCA8418 keypad driver
 *
 * This driver reads the key pressed and reports it to the linux input system
 * TCA6416 driver is the reference driver for this
 *
 * Copyright (C) {2011} Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/tca8418_keypad.h>

/*register address*/
#define CFG           	0x01
#define INT_STATUS      0x02
#define KEY_LCK_EC      0x03
#define KEY_EVENT_A     0x04
#define KP_GPIO1        0x1D
#define KP_GPIO2        0x1E

#define K_INT           0x01
#define BIT7_MASK      	0x80
#define LCK_EC_MASK     0x0f
#define KEY_MASK        0x7f
#define CLEAR_IRQ       0x01
#define INT_OVF_VAL     0x21

static const struct i2c_device_id tca8418_id[] = {
	{ "tca8418-keys", 0, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tca6416_id);

struct tca8418_keypad_chip {
	u8 row_val;
	u8 col_val;
	struct i2c_client *client;
	struct input_dev *input;
	struct delayed_work dwork;
	int irqnum;
	bool use_polling;
	struct tca8418_button buttons[];
};

static int tca8418_write_reg(struct tca8418_keypad_chip *chip, int reg, u8 val)
{
	int error;

	error = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (error < 0) {
		dev_err(&chip->client->dev,
			"%s failed, reg: %d, val: %d, error: %d\n",
			__func__, reg, val, error);
		return error;
	}

	return 0;
}

static int tca8418_read_reg(struct tca8418_keypad_chip *chip, int reg, u8 *val)
{
	int retval;

	retval = i2c_smbus_read_byte_data(chip->client, reg);
	if (retval < 0) {
		dev_err(&chip->client->dev, "%s failed, reg: %d, error: %d\n",
			__func__, reg, retval);
		return retval;
	}

	*val = (u8)retval;
	return 0;
}

static void tca8418_keys_scan(struct tca8418_keypad_chip *chip)
{
	struct input_dev *input = chip->input;
	u8 val_int_status, val_key_lck_ec, val_key_event_a, val_counter;
	u8 scancode;
	int error, index;
	struct tca8418_button *button;

	error = tca8418_read_reg(chip, INT_STATUS, &val_int_status);
	if (error)
		return;
	if ((val_int_status & K_INT) == K_INT) {
		/*read the counter value to see how many keys are pressed*/
		error = tca8418_read_reg(chip, KEY_LCK_EC, &val_key_lck_ec);
		if (error)
			return;
		val_counter = (val_key_lck_ec & LCK_EC_MASK);

		/*read the keys from KEY_EVENT_A register*/
		for (index = 1; index <= val_counter; index++) {
			error = tca8418_read_reg(chip, KEY_EVENT_A,
					 &val_key_event_a);
			if (error)
				return;
			if ((val_key_event_a & BIT7_MASK) == BIT7_MASK) {
				scancode = (val_key_event_a & KEY_MASK);
				button = &chip->buttons[scancode - 1];
				input_event(input, button->type,
							button->code, 1);
			} else if ((val_key_event_a & BIT7_MASK) == 0x0) {
				button = &chip->buttons[val_key_event_a - 1];
				input_event(input, button->type,
							button->code, 0);
			}
		}

		/*clear int bit*/
		error = tca8418_write_reg(chip, INT_STATUS, CLEAR_IRQ);
		if (error)
			return;
	}

}

/*
 * This is threaded IRQ handler and this can (and will) sleep.
 */
static irqreturn_t tca8418_keys_isr(int irq, void *dev_id)
{
	struct tca8418_keypad_chip *chip = dev_id;
	schedule_delayed_work(&chip->dwork, msecs_to_jiffies(100));
	return IRQ_HANDLED;
}

static void tca8418_keys_work_func(struct work_struct *work)
{
	struct tca8418_keypad_chip *chip =
		container_of(work, struct tca8418_keypad_chip, dwork.work);

	tca8418_keys_scan(chip);
	if (chip->use_polling)
		schedule_delayed_work(&chip->dwork, msecs_to_jiffies(100));
}

static int tca8418_keys_open(struct input_dev *dev)
{
	struct tca8418_keypad_chip *chip = input_get_drvdata(dev);

	/* Get initial device state in case it has switches */
	tca8418_keys_scan(chip);

	if (chip->use_polling)
		schedule_delayed_work(&chip->dwork, msecs_to_jiffies(100));
	else
		enable_irq(chip->irqnum);

	return 0;
}

static void tca8418_keys_close(struct input_dev *dev)
{
	struct tca8418_keypad_chip *chip = input_get_drvdata(dev);

	if (chip->use_polling)
		cancel_delayed_work_sync(&chip->dwork);
	else
		disable_irq(chip->irqnum);
}

static int tca8418_setup_registers(struct tca8418_keypad_chip *chip)
{
	int error;

	uint8_t val = 0;

	/* ensure that pins are set as keypad */
	error = tca8418_read_reg(chip, KP_GPIO1, &val);
	if (error)
		return error;

	/*making corresponding rows as part of keymatrix,
	this needs to be provided by the platform data*/
	val |= (chip->row_val);
	error = tca8418_write_reg(chip, KP_GPIO1,
				  val);
	if (error)
		return error;

	/* ensure that pins are set as keypad */
	error = tca8418_read_reg(chip, KP_GPIO2, &val);
	if (error)
		return error;

	/*making corresponding cols as part of keymatrix,
	this needs to be provided by the platform data*/
	val |= (chip->col_val);
	error = tca8418_write_reg(chip, KP_GPIO2, val);
	if (error)
		return error;

	/* enable interrupt and overflow mode(overflow mode to
	overwrite the data when  FIFO is full)*/
	error = tca8418_read_reg(chip, CFG, &val);
	if (error)
		return error;

	val |= INT_OVF_VAL;
	error = tca8418_write_reg(chip, CFG, val);
	if (error)
		return error;

	return 0;
}

static int __devinit tca8418_keypad_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct tca8418_keys_platform_data *pdata;
	struct tca8418_keypad_chip *chip;
	struct input_dev *input;
	int error;
	int i;

	/* Check functionality */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE)) {
		dev_err(&client->dev, "%s adapter not supported\n",
			dev_driver_string(&client->adapter->dev));
		return -ENODEV;
	}

	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_dbg(&client->dev, "no platform data\n");
		return -EINVAL;
	}

	chip = kzalloc(sizeof(struct tca8418_keypad_chip) +
		       pdata->nbuttons * sizeof(struct tca8418_button),
		       GFP_KERNEL);
	input = input_allocate_device();
	if (!chip || !input) {
		error = -ENOMEM;
		goto fail1;
	}

	chip->client = client;
	chip->input = input;
	chip->use_polling = pdata->use_polling;
	chip->row_val = pdata->row_val;
	chip->col_val = pdata->col_val;

	INIT_DELAYED_WORK(&chip->dwork, tca8418_keys_work_func);

	input->phys = "tca8418-keys/input0";
	input->name = "TCA8418 Keypad";
	input->dev.parent = &client->dev;

	input->open = tca8418_keys_open;
	input->close = tca8418_keys_close;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	/* Enable auto repeat feature of Linux input subsystem */
	if (pdata->rep)
		__set_bit(EV_REP, input->evbit);

	for (i = 0; i < pdata->nbuttons; i++) {
		unsigned int type;

		chip->buttons[i] = pdata->buttons[i];
		type = (pdata->buttons[i].type) ?: EV_KEY;
		input_set_capability(input, type, pdata->buttons[i].code);
	}

	input_set_drvdata(input, chip);

	/*
	 * Initialize cached registers from their original values.
	 */
	error = tca8418_setup_registers(chip);
	if (error)
		goto fail1;

	if (!chip->use_polling) {
		if (pdata->irq_is_gpio)
			chip->irqnum = gpio_to_irq(client->irq);
		else
			chip->irqnum = client->irq;
		error = request_irq(chip->irqnum, tca8418_keys_isr,
						pdata->irq_flags,
					     "tca8418-keypad", chip);
		if (error) {
			dev_dbg(&client->dev,
				"Unable to claim irq %d; error %d\n",
				chip->irqnum, error);
			goto fail1;
		}
		disable_irq(chip->irqnum);
	}

	error = input_register_device(input);
	if (error) {
		dev_dbg(&client->dev,
			"Unable to register input device, error: %d\n", error);
		goto fail2;
	}

	i2c_set_clientdata(client, chip);

	return 0;

fail2:
	if (!chip->use_polling) {
		free_irq(chip->irqnum, chip);
		enable_irq(chip->irqnum);
	}
fail1:
	input_free_device(input);
	kfree(chip);
	return error;
}

static int __devexit tca8418_keypad_remove(struct i2c_client *client)
{
	struct tca8418_keypad_chip *chip = i2c_get_clientdata(client);

	if (!chip->use_polling) {
		free_irq(chip->irqnum, chip);
		enable_irq(chip->irqnum);
	}

	input_unregister_device(chip->input);
	kfree(chip);

	return 0;
}


static struct i2c_driver tca8418_keypad_driver = {
	.driver = {
		.name	= "tca8418-keypad",
	},
	.probe		= tca8418_keypad_probe,
	.remove		= __devexit_p(tca8418_keypad_remove),
	.id_table	= tca8418_id,
};

static int __init tca8418_keypad_init(void)
{
	return i2c_add_driver(&tca8418_keypad_driver);
}

subsys_initcall(tca8418_keypad_init);

static void __exit tca8418_keypad_exit(void)
{
	i2c_del_driver(&tca8418_keypad_driver);
}
module_exit(tca8418_keypad_exit);

MODULE_AUTHOR("sumahegde@ti.com");
MODULE_DESCRIPTION("Keypad driver over tca8418 IO expander");
MODULE_LICENSE("GPL");
