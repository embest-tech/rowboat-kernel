/*
 * arch/arm/mach-omap2/board-am335xevm-bt.c
 * Copyright (c) Vishveshwar Bhat <vishveshwar.bhat@ti.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 *	    AM335xevm bluetooth on/off driver
 *
 * Based on arch/arm/mach-s3c2410/h1940-bluetooth.c
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/rfkill.h>

#include <mach/board-am335xevm.h>

#define DRV_NAME "am335xevm-bt"

/* Convert GPIO signal to GPIO pin number */
#define GPIO_TO_PIN(bank, gpio) (32 * (bank) + (gpio))
#define GPIO_BT_EN                        GPIO_TO_PIN(3, 21)

/* Bluetooth control */
static void am335xevm_bt_enable(int on)
{
	if (on) {
		printk("Gpio value is :%d\n", GPIO_BT_EN);
		gpio_set_value(GPIO_BT_EN, 0);
		msleep(1);
		printk("WL1271: BT Enable\n");
		gpio_set_value(GPIO_BT_EN, 1);
		msleep(100);
	}
	else {
		printk("WL1271: BT Disable\n");
		gpio_set_value(GPIO_BT_EN, 0);
		msleep(100);
	}
}

static int am335xevm_bt_set_block(void *data, bool blocked)
{
	am335xevm_bt_enable(!blocked);
	return 0;
}

static const struct rfkill_ops am335xevm_bt_rfkill_ops = {
	.set_block = am335xevm_bt_set_block,
};

static int __devinit am335xevm_bt_probe(struct platform_device *pdev)
{
	struct rfkill *rfk;
	int ret = 0;

	rfk = rfkill_alloc(DRV_NAME, &pdev->dev, RFKILL_TYPE_BLUETOOTH,
			&am335xevm_bt_rfkill_ops, NULL);
	if (!rfk) {
		ret = -ENOMEM;
		goto err_rfk_alloc;
	}

	ret = rfkill_register(rfk);
	if (ret)
		goto err_rfkill;

	platform_set_drvdata(pdev, rfk);

	return 0;

err_rfkill:
	rfkill_destroy(rfk);
err_rfk_alloc:
	return ret;
}

static int am335xevm_bt_remove(struct platform_device *pdev)
{
	struct rfkill *rfk = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (rfk) {
		rfkill_unregister(rfk);
		rfkill_destroy(rfk);
	}
	rfk = NULL;

	am335xevm_bt_enable(0);

	return 0;
}


static struct platform_driver am335xevm_bt_driver = {
	.driver		= {
		.name	= DRV_NAME,
	},
	.probe		= am335xevm_bt_probe,
	.remove		= am335xevm_bt_remove,
};


static int __init am335xevm_bt_init(void)
{
	return platform_driver_register(&am335xevm_bt_driver);
}

static void __exit am335xevm_bt_exit(void)
{
	platform_driver_unregister(&am335xevm_bt_driver);
}

module_init(am335xevm_bt_init);
module_exit(am335xevm_bt_exit);

MODULE_AUTHOR("Vishveshwar Bhat <vishveshwar.bhat@ti.com>");
MODULE_DESCRIPTION("Power On/Off driver for AM335xevm Bluetooth");
MODULE_LICENSE("GPL");
