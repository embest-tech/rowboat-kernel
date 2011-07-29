/*
 * TI Touch Screen driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
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


#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/input/ti_tscadc.h>

#define TSCADC_REG_IRQEOI		0x020
#define TSCADC_REG_IRQSTATUS		0x028
#define TSCADC_REG_IRQENABLE		0x02C
#define TSCADC_REG_IRQWAKEUP		0x034
#define TSCADC_REG_CTRL			0x040
#define TSCADC_REG_ADCFSM		0x044
#define TSCADC_REG_CLKDIV		0x04C
#define TSCADC_REG_SE			0x054
#define TSCADC_REG_IDLECONFIG		0x058
#define TSCADC_REG_CHARGECONFIG		0x05C
#define TSCADC_REG_STEPCONFIG1		0x064
#define TSCADC_REG_STEPDELAY1		0x068
#define TSCADC_REG_STEPCONFIG2		0x06C
#define TSCADC_REG_STEPDELAY2		0x070
#define TSCADC_REG_FIFO0		0x100

/*	Register Bitfields	*/
#define TSCADC_IRQWKUP_ENB		BIT(0)
#define TSCADC_STPENB_STEPENB		(7 << 0)
#define TSCADC_IRQENB_IRQHWPEN		BIT(10)
#define TSCADC_IRQENB_IRQEOS		BIT(1)
#define TSCADC_IRQENB_FIFO_OVERFLOW	BIT(3)
#define TSCADC_IRQENB_PENUP		BIT(9)
#define TSCADC_STEPCONFIG_MODE_HWSYNC	0x3
#define TSCADC_STEPCONFIG_2SAMPLES_AVG	BIT(2)
#define TSCADC_STEPCONFIG_XPP		BIT(5)
#define TSCADC_STEPCONFIG_XNN		BIT(6)
#define TSCADC_STEPCONFIG_YPP		BIT(7)
#define TSCADC_STEPCONFIG_YNN		BIT(8)
#define TSCADC_STEPCONFIG_XNP		BIT(9)
#define TSCADC_STEPCONFIG_YPN		BIT(10)
#define TSCADC_STEPCONFIG_RFP_X		(1 << 12)
#define TSCADC_STEPCONFIG_RFP_4_Y	(1 << 13)
#define TSCADC_STEPCONFIG_RFP_5_Y	(1 << 12)
#define TSCADC_STEPCONFIG_RFP_8_Y	(1 << 13)
#define TSCADC_STEPCONFIG_INM		(1 << 18)
#define TSCADC_STEPCONFIG_INP_4		(1 << 20)
#define TSCADC_STEPCONFIG_INP_5		(1 << 21)
#define TSCADC_STEPCONFIG_INP_8_X	(3 << 20)
#define TSCADC_STEPCONFIG_INP_8_Y	(1 << 21)
#define TSCADC_STEPCONFIG_RFM_4_X	(1 << 23)
#define TSCADC_STEPCONFIG_RFM_5_X	(1 << 24)
#define TSCADC_STEPCONFIG_RFM_8_X	(1 << 23)
#define TSCADC_STEPCONFIG_RFM_Y		(1 << 24)
#define TSCADC_STEPCONFIG_OPENDLY	(0xf << 0)
#define TSCADC_STEPCONFIG_SAMPLEDLY	BIT(25)
#define TSCADC_STEPCHARGE_INM		BIT(18)
#define TSCADC_STEPCHARGE_RFM		(3 << 23)
#define TSCADC_CNTRLREG_TSCSSENB	BIT(0)
#define TSCADC_CNTRLREG_STEPID		BIT(1)
#define TSCADC_CNTRLREG_STEPCONFIGWRT	BIT(2)
#define TSCADC_CNTRLREG_TSCENB		BIT(7)
#define TSCADC_CNTRLREG_4WIRE		(0x1 << 5)
#define TSCADC_CNTRLREG_5WIRE		(0x1 << 6)
#define TSCADC_CNTRLREG_8WIRE		(0x3 << 5)
#define TSCADC_ADCFSM_STEPID		0x10
#define TSCADC_ADCFSM_FSM		BIT(5)

#define ADC_CLK				3000000

#define MAX_12BIT                       ((1 << 12) - 1)

struct tscadc {
	struct input_dev	*input;
	int			wires;
	struct clk		*clk;
	int			irq;
	void __iomem		*tsc_base;
};

static unsigned int tscadc_readl(struct tscadc *ts, unsigned int reg)
{
	return readl(ts->tsc_base + reg);
}

static void tscadc_writel(struct tscadc *tsc, unsigned int reg,
					unsigned int val)
{
	writel(val, tsc->tsc_base + reg);
}

static void tsc_step_config(struct tscadc *ts_dev)
{
	int	stepconfig1, stepconfig2, delay;

	/* Configure the Step registers */
	stepconfig1 = TSCADC_STEPCONFIG_MODE_HWSYNC |
			TSCADC_STEPCONFIG_2SAMPLES_AVG | TSCADC_STEPCONFIG_XPP |
			TSCADC_STEPCONFIG_XNN | TSCADC_STEPCONFIG_INM |
			TSCADC_STEPCONFIG_RFP_X;

	stepconfig2 = TSCADC_STEPCONFIG_MODE_HWSYNC |
			TSCADC_STEPCONFIG_2SAMPLES_AVG | TSCADC_STEPCONFIG_YNN |
			TSCADC_STEPCONFIG_INM | TSCADC_STEPCONFIG_RFM_Y;
	switch (ts_dev->wires) {
	case 4:
		stepconfig1 |= TSCADC_STEPCONFIG_INP_4 |
				TSCADC_STEPCONFIG_RFM_4_X;

		stepconfig2 |= TSCADC_STEPCONFIG_YPP |
				TSCADC_STEPCONFIG_RFP_4_Y;
		break;
	case 5:
		stepconfig1 |= TSCADC_STEPCONFIG_YPP |
				TSCADC_STEPCONFIG_YNN |
				TSCADC_STEPCONFIG_INP_5 |
				TSCADC_STEPCONFIG_RFM_5_X;

		stepconfig2 |= TSCADC_STEPCONFIG_XPP |
				TSCADC_STEPCONFIG_XNP |
				TSCADC_STEPCONFIG_YPN |
				TSCADC_STEPCONFIG_RFP_5_Y |
				TSCADC_STEPCONFIG_INP_5;
		break;
	case 8:
		stepconfig1 |= TSCADC_STEPCONFIG_INP_8_X |
				TSCADC_STEPCONFIG_RFM_8_X;

		stepconfig2 |= TSCADC_STEPCONFIG_YPP |
				TSCADC_STEPCONFIG_RFP_8_Y |
				TSCADC_STEPCONFIG_INP_8_Y;
		break;
	}
	delay = TSCADC_STEPCONFIG_OPENDLY | TSCADC_STEPCONFIG_SAMPLEDLY;

	tscadc_writel(ts_dev, TSCADC_REG_STEPCONFIG1, stepconfig1);
	tscadc_writel(ts_dev, TSCADC_REG_STEPDELAY1, delay);
	tscadc_writel(ts_dev, TSCADC_REG_STEPCONFIG2, stepconfig2);
	tscadc_writel(ts_dev, TSCADC_REG_STEPDELAY2, delay);

	tscadc_writel(ts_dev, TSCADC_REG_SE, TSCADC_STPENB_STEPENB);
}

static void tsc_idle_config(struct tscadc *ts_config)
{
	/* Idle mode touch screen config */
	unsigned int	 idleconfig;

	idleconfig = TSCADC_STEPCONFIG_YNN |
				TSCADC_STEPCONFIG_RFP_X |
				TSCADC_STEPCONFIG_INM ;

	switch (ts_config->wires) {
	case 4:
		idleconfig |= TSCADC_STEPCONFIG_INP_4 |
			TSCADC_STEPCONFIG_RFM_4_X;
		break;
	case 5:
		idleconfig |= TSCADC_STEPCONFIG_INP_5 |
			TSCADC_STEPCONFIG_RFM_5_X;
		break;
	case 8:
		idleconfig |= TSCADC_STEPCONFIG_INP_4 |
			TSCADC_STEPCONFIG_RFM_8_X;
		break;
	}
	tscadc_writel(ts_config, TSCADC_REG_IDLECONFIG, idleconfig);
}

static irqreturn_t tscadc_interrupt(int irq, void *dev)
{
	struct tscadc		*ts_dev = (struct tscadc *)dev;
	struct input_dev	*input_dev = ts_dev->input;
	unsigned int		status, store, cntrlreg, irqclr = 0;
	int			absx, absy;
	int			charge, fsm;

	status = tscadc_readl(ts_dev, TSCADC_REG_IRQSTATUS);
	tscadc_writel(ts_dev, TSCADC_REG_SE, 0x0);

	/* Pen touch event */
	if (status & TSCADC_IRQENB_IRQHWPEN)
		irqclr = TSCADC_IRQENB_IRQHWPEN;

	if (status & TSCADC_IRQENB_PENUP) {
		/* Pen up event */
		charge = TSCADC_STEPCHARGE_INM | TSCADC_STEPCHARGE_RFM;
		tscadc_writel(ts_dev, TSCADC_REG_CHARGECONFIG, charge);
		input_report_key(input_dev, BTN_TOUCH, 0);
		input_sync(input_dev);
		tsc_idle_config(ts_dev);
		irqclr |= TSCADC_IRQENB_PENUP;

	}
	if (status & TSCADC_IRQENB_IRQEOS) {
		/* ADC is done with sampling, ready to read the data */
		absx = tscadc_readl(ts_dev, TSCADC_REG_FIFO0);
		absy = tscadc_readl(ts_dev, TSCADC_REG_FIFO0);

		input_report_abs(input_dev, ABS_X, absx);
		input_report_abs(input_dev, ABS_Y, absy);
		input_report_key(input_dev, BTN_TOUCH, 1);
		input_sync(input_dev);

		irqclr |= TSCADC_IRQENB_IRQEOS;

	}
	if (status & TSCADC_IRQENB_FIFO_OVERFLOW) {
		/* FIFO overflow condition */
		cntrlreg = tscadc_readl(ts_dev, TSCADC_REG_CTRL);
		cntrlreg &= ~TSCADC_CNTRLREG_TSCSSENB;
		tscadc_writel(ts_dev, TSCADC_REG_CTRL, cntrlreg);

		irqclr |= TSCADC_IRQENB_FIFO_OVERFLOW;
	}

	tscadc_writel(ts_dev, TSCADC_REG_IRQSTATUS, irqclr);
	fsm = tscadc_readl(ts_dev, TSCADC_REG_ADCFSM);
	if ((fsm & TSCADC_ADCFSM_FSM) &&
			(fsm & TSCADC_ADCFSM_STEPID)) {
		store = tscadc_readl(ts_dev, TSCADC_REG_CTRL);
		store |= TSCADC_CNTRLREG_TSCSSENB;
		tscadc_writel(ts_dev, TSCADC_REG_CTRL, store);
	}

	 /* check pending interrupts */
	tscadc_writel(ts_dev, TSCADC_REG_IRQEOI, 0x0);

	tscadc_writel(ts_dev, TSCADC_REG_SE, TSCADC_STPENB_STEPENB);
	return IRQ_HANDLED;
}

/*
* The functions for inserting/removing driver as a module.
*/

static	int __devinit tscadc_probe(struct platform_device *pdev)
{
	struct tscadc			*ts_dev;
	struct input_dev		*input_dev;
	int				err;
	int				clk_value;
	int				clock_rate, irqenable, ctrl;
	struct	tsc_data		*pdata = pdev->dev.platform_data;
	struct resource			*res;
	struct clk			*tsc_ick;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no memory resource defined.\n");
		return -EINVAL;
	}

	/* Allocate memory for device */
	ts_dev = kzalloc(sizeof(struct tscadc), GFP_KERNEL);
	if (!ts_dev) {
		dev_err(&pdev->dev, "failed to allocate memory.\n");
		return -ENOMEM;
	}

	ts_dev->irq = platform_get_irq(pdev, 0);
	if (ts_dev->irq < 0) {
		dev_err(&pdev->dev, "no irq ID is specified.\n");
		return -ENODEV;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device.\n");
		err = -ENOMEM;
		goto err_free_mem;
	}
	ts_dev->input = input_dev;

	ts_dev->tsc_base = ioremap(res->start, resource_size(res));
	if (!ts_dev->tsc_base) {
		dev_err(&pdev->dev, "failed to map registers.\n");
		err = -ENOMEM;
		goto err_release_mem;
	}

	err = request_irq(ts_dev->irq, tscadc_interrupt, IRQF_DISABLED,
				pdev->dev.driver->name, ts_dev);
	if (err) {
		dev_err(&pdev->dev, "failed to allocate irq.\n");
		goto err_unmap_regs;
	}

	tsc_ick = clk_get(&pdev->dev, "adc_tsc_ick");
	if (IS_ERR(tsc_ick)) {
		dev_err(&pdev->dev, "failed to get TSC ick\n");
		goto err_free_irq;
	}
	clk_enable(tsc_ick);

	ts_dev->clk = clk_get(&pdev->dev, "adc_tsc_fck");
	if (IS_ERR(ts_dev->clk)) {
		dev_err(&pdev->dev, "failed to get TSC fck\n");
		err = PTR_ERR(ts_dev->clk);
		goto err_free_irq;
	}
	clock_rate = clk_get_rate(ts_dev->clk);
	clk_value = clock_rate / ADC_CLK;
	if (clk_value < 7) {
		dev_err(&pdev->dev, "clock input less than min clock requirement\n");
		goto err_fail;
	}
	/* TSCADC_CLKDIV needs to be configured to the value minus 1 */
	clk_value = clk_value - 1;
	tscadc_writel(ts_dev, TSCADC_REG_CLKDIV, clk_value);

	 /* Enable wake-up of the SoC using touchscreen */
	tscadc_writel(ts_dev, TSCADC_REG_IRQWAKEUP, TSCADC_IRQWKUP_ENB);

	ts_dev->wires = pdata->wires;

	/* Set the control register bits */
	ctrl = TSCADC_CNTRLREG_STEPCONFIGWRT |
			TSCADC_CNTRLREG_TSCENB |
			TSCADC_CNTRLREG_STEPID;
	switch (ts_dev->wires) {
	case 4:
		ctrl |= TSCADC_CNTRLREG_4WIRE;
		break;
	case 5:
		ctrl |= TSCADC_CNTRLREG_5WIRE;
		break;
	case 8:
		ctrl |= TSCADC_CNTRLREG_8WIRE;
		break;
	}
	tscadc_writel(ts_dev, TSCADC_REG_CTRL, ctrl);

	/* Set register bits for Idel Config Mode */
	tsc_idle_config(ts_dev);

	/* IRQ Enable */
	irqenable = TSCADC_IRQENB_IRQHWPEN |
			TSCADC_IRQENB_IRQEOS |
			TSCADC_IRQENB_PENUP | TSCADC_IRQENB_FIFO_OVERFLOW;
	tscadc_writel(ts_dev, TSCADC_REG_IRQENABLE, irqenable);

	tsc_step_config(ts_dev);

	ctrl |= TSCADC_CNTRLREG_TSCSSENB;
	tscadc_writel(ts_dev, TSCADC_REG_CTRL, ctrl);

	input_dev->name = "ti-tsc-adcc";
	input_dev->dev.parent = &pdev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, 0, 0);

	/* register to the input system */
	err = input_register_device(input_dev);
	if (err)
		goto err_fail;

	return 0;

err_fail:
	clk_disable(ts_dev->clk);
	clk_put(ts_dev->clk);
err_free_irq:
	free_irq(ts_dev->irq, ts_dev);
err_unmap_regs:
	iounmap(ts_dev->tsc_base);
err_release_mem:
	release_mem_region(res->start, resource_size(res));
	input_free_device(ts_dev->input);
err_free_mem:
	kfree(ts_dev);
	return err;
}

static int __devexit tscadc_remove(struct platform_device *pdev)
{
	struct tscadc		*ts_dev = dev_get_drvdata(&pdev->dev);
	struct resource		*res;

	free_irq(ts_dev->irq, ts_dev);

	input_unregister_device(ts_dev->input);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iounmap(ts_dev->tsc_base);
	release_mem_region(res->start, resource_size(res));

	clk_disable(ts_dev->clk);
	clk_put(ts_dev->clk);

	kfree(ts_dev);

	return 0;
}

static struct platform_driver ti_tsc_driver = {
	.probe	  = tscadc_probe,
	.remove	 = __devexit_p(tscadc_remove),
	.driver	 = {
		.name   = "tsc",
	},
};

static int __init ti_tsc_init(void)
{
	return platform_driver_register(&ti_tsc_driver);
}

static void __exit ti_tsc_exit(void)
{
	platform_driver_unregister(&ti_tsc_driver);
}

module_init(ti_tsc_init);
module_exit(ti_tsc_exit);
