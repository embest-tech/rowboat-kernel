/*
 * mt9t112 Camera Driver
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Based on ov772x driver, mt9m111 driver,
 *
 * Copyright (C) 2008 Kuninori Morimoto <morimoto.kuninori@renesas.com>
 * Copyright (C) 2008, Robert Jarzmik <robert.jarzmik@free.fr>
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/mt9t112.h>
#include <media/v4l2-int-device.h>
#include <media/v4l2-chip-ident.h>

/* you can check PLL/clock info */
/* #define EXT_CLOCK 24000000 */

/************************************************************************


			macro


************************************************************************/
/*
 * frame size
 */
#define MAX_WIDTH   2048
#define MAX_HEIGHT  1536

#define MAX_WIDTH_PREV   1024
#define MAX_HEIGHT_PREV  768

#define VGA_WIDTH   640
#define VGA_HEIGHT  480

/*
 * macro of read/write
 */
#define ECHECKER(ret, x)		\
	do {				\
		(ret) = (x);		\
		if ((ret) < 0)		\
			return ret;	\
	} while (0)

#define mt9t112_reg_write(ret, client, a, b) \
	ECHECKER(ret, __mt9t112_reg_write(client, a, b))
#define mt9t112_mcu_write(ret, client, a, b) \
	ECHECKER(ret, __mt9t112_mcu_write(client, a, b))

#define mt9t112_reg_mask_set(ret, client, a, b, c) \
	ECHECKER(ret, __mt9t112_reg_mask_set(client, a, b, c))
#define mt9t112_mcu_mask_set(ret, client, a, b, c) \
	ECHECKER(ret, __mt9t112_mcu_mask_set(client, a, b, c))

#define mt9t112_reg_read(ret, client, a) \
	ECHECKER(ret, __mt9t112_reg_read(client, a))

#define mt9t112_mcu_read(ret, client, a) \
	ECHECKER(ret, __mt9t112_mcu_read(client, a))

/*
 * Logical address
 */
#define _VAR(id, offset, base)	(base | (id & 0x1f) << 10 | (offset & 0x3ff))
#define VAR(id, offset)  _VAR(id, offset, 0x0000)
#define VAR8(id, offset) _VAR(id, offset, 0x8000)

/************************************************************************


			struct


************************************************************************/
struct mt9t112_frame_size {
	u16 width;
	u16 height;
};

struct mt9t112_priv {
	struct mt9t112_platform_data	*pdata;
	struct v4l2_int_device		*v4l2_int_device;
	struct mt9t112_camera_info	info;
	struct i2c_client		*client;
	struct v4l2_pix_format		 pix;
	int				 model;
	u32				 flags;
/* for flags */
#define INIT_DONE  (1<<0)
};

/************************************************************************


			supported format


************************************************************************/

const static struct v4l2_fmtdesc mt9t112_formats[] = {
	{
		.description	= "YUYV (YUV 4:2:2), packed",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
	},
	{
		.description	= "RGB555, le",
		.pixelformat	= V4L2_PIX_FMT_RGB555,
	},
	{
		.description	= "RGB565, le",
		.pixelformat	= V4L2_PIX_FMT_RGB565,
	},
};

/************************************************************************


			supported sizes


************************************************************************/
const static struct mt9t112_frame_size mt9t112_sizes[] = {
	{  640, 480 },
	{ 2048, 1536}
};

/************************************************************************


			supported sizes


************************************************************************/
const struct v4l2_fract mt9t112_frameintervals[] = {
	{  .numerator = 1, .denominator = 30 }
};

/************************************************************************


			general function


************************************************************************/
static u16 mt9t112_pixfmt_to_fmt(u32 pixelformat)
{
	switch (pixelformat) {
	case V4L2_PIX_FMT_RGB555:
		return 8;
	case V4L2_PIX_FMT_RGB565:
		return 4;
	case V4L2_PIX_FMT_YUYV:
		/* FALLTHROUGH */
	default:
		return 1;
	}
}

static u16 mt9t112_pixfmt_to_order(u32 pixelformat)
{
	switch (pixelformat) {
	case V4L2_PIX_FMT_RGB555:
		/* FALLTHROUGH */
	case V4L2_PIX_FMT_RGB565:
		return 2;
	case V4L2_PIX_FMT_YUYV:
		/* FALLTHROUGH */
	default:
		return 0;
	}
}

static int __mt9t112_reg_read(const struct i2c_client *client, u16 command)
{
	struct i2c_msg msg[2];
	u8 buf[2];
	int ret;

	command = swab16(command);

	msg[0].addr  = client->addr;
	msg[0].flags = 0;
	msg[0].len   = 2;
	msg[0].buf   = (u8 *)&command;

	msg[1].addr  = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = 2;
	msg[1].buf   = buf;

	/*
	 * if return value of this function is < 0,
	 * it mean error.
	 * else, under 16bit is valid data.
	 */
	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0)
		return ret;

	memcpy(&ret, buf, 2);
	return swab16(ret);
}

static int __mt9t112_reg_write(const struct i2c_client *client,
			       u16 command, u16 data)
{
	struct i2c_msg msg;
	u8 buf[4];
	int ret;

	command = swab16(command);
	data = swab16(data);

	memcpy(buf + 0, &command, 2);
	memcpy(buf + 2, &data,    2);

	msg.addr  = client->addr;
	msg.flags = 0;
	msg.len   = 4;
	msg.buf   = buf;

	/*
	 * i2c_transfer return message length,
	 * but this function should return 0 if correct case
	 */
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		ret = 0;

	return ret;
}

static int __mt9t112_reg_mask_set(const struct i2c_client *client,
				  u16  command,
				  u16  mask,
				  u16  set)
{
	int val = __mt9t112_reg_read(client, command);
	if (val < 0)
		return val;

	val &= ~mask;
	val |= set & mask;

	return __mt9t112_reg_write(client, command, val);
}

/* mcu access */
static int __mt9t112_mcu_read(const struct i2c_client *client, u16 command)
{
	int ret;

	ret = __mt9t112_reg_write(client, 0x098E, command);
	if (ret < 0)
		return ret;

	return __mt9t112_reg_read(client, 0x0990);
}

static int __mt9t112_mcu_write(const struct i2c_client *client,
			       u16 command, u16 data)
{
	int ret;

	ret = __mt9t112_reg_write(client, 0x098E, command);
	if (ret < 0)
		return ret;

	return __mt9t112_reg_write(client, 0x0990, data);
}

static int __mt9t112_mcu_mask_set(const struct i2c_client *client,
				  u16  command,
				  u16  mask,
				  u16  set)
{
	int val = __mt9t112_mcu_read(client, command);
	if (val < 0)
		return val;

	val &= ~mask;
	val |= set & mask;

	return __mt9t112_mcu_write(client, command, val);
}

static int mt9t112_reset(const struct i2c_client *client)
{
	int ret;

	mt9t112_reg_mask_set(ret, client, 0x001a, 0x0001, 0x0001);
	msleep(1);
	mt9t112_reg_mask_set(ret, client, 0x001a, 0x0001, 0x0000);

	return ret;
}

#ifndef EXT_CLOCK
#define CLOCK_INFO(a, b)
#else
#define CLOCK_INFO(a, b) mt9t112_clock_info(a, b)
static int mt9t112_clock_info(const struct i2c_client *client, u32 ext)
{
	int m, n, p1, p2, p3, p4, p5, p6, p7;
	u32 vco, clk;
	char *enable;

	ext /= 1000; /* kbyte order */

	mt9t112_reg_read(n, client, 0x0012);
	p1 = n & 0x000f;
	n = n >> 4;
	p2 = n & 0x000f;
	n = n >> 4;
	p3 = n & 0x000f;

	mt9t112_reg_read(n, client, 0x002a);
	p4 = n & 0x000f;
	n = n >> 4;
	p5 = n & 0x000f;
	n = n >> 4;
	p6 = n & 0x000f;

	mt9t112_reg_read(n, client, 0x002c);
	p7 = n & 0x000f;

	mt9t112_reg_read(n, client, 0x0010);
	m = n & 0x00ff;
	n = (n >> 8) & 0x003f;

	enable = ((6000 > ext) || (54000 < ext)) ? "X" : "";
	dev_info(&client->dev, "EXTCLK          : %10u K %s\n", ext, enable);

	vco = 2 * m * ext / (n+1);
	enable = ((384000 > vco) || (768000 < vco)) ? "X" : "";
	dev_info(&client->dev, "VCO             : %10u K %s\n", vco, enable);

	clk = vco / (p1+1) / (p2+1);
	enable = (96000 < clk) ? "X" : "";
	dev_info(&client->dev, "PIXCLK          : %10u K %s\n", clk, enable);

	clk = vco / (p3+1);
	enable = (768000 < clk) ? "X" : "";
	dev_info(&client->dev, "MIPICLK         : %10u K %s\n", clk, enable);

	clk = vco / (p6+1);
	enable = (96000 < clk) ? "X" : "";
	dev_info(&client->dev, "MCU CLK         : %10u K %s\n", clk, enable);

	clk = vco / (p5+1);
	enable = (54000 < clk) ? "X" : "";
	dev_info(&client->dev, "SOC CLK         : %10u K %s\n", clk, enable);

	clk = vco / (p4+1);
	enable = (70000 < clk) ? "X" : "";
	dev_info(&client->dev, "Sensor CLK      : %10u K %s\n", clk, enable);

	clk = vco / (p7+1);
	dev_info(&client->dev, "External sensor : %10u K\n", clk);

	clk = ext / (n+1);
	enable = ((2000 > clk) || (24000 < clk)) ? "X" : "";
	dev_info(&client->dev, "PFD             : %10u K %s\n", clk, enable);

	return 0;
}
#endif

static void mt9t112_frame_check(u32 *width, u32 *height)
{
	if (*width > MAX_WIDTH)
		*width = MAX_WIDTH;

	if (*height > MAX_HEIGHT)
		*height = MAX_HEIGHT;
}

#define PLL_ADJ(x)	((x != 0) ? x - 1 : 0)

static int mt9t112_set_pll_dividers(const struct i2c_client *client,
				    u8 m, u8 n,
				    u8 p1, u8 p2, u8 p3,
				    u8 p4, u8 p5, u8 p6,
				    u8 p7)
{
	int ret;
	u16 val;

	/* N/M */
	val = (n << 8) |
	      (m << 0);
	mt9t112_reg_mask_set(ret, client, 0x0010, 0x3fff, val);

	/* P1/P2/P3 */
	val = ((PLL_ADJ(p3) & 0x0F) << 8) |
	      ((PLL_ADJ(p2) & 0x0F) << 4) |
	      ((PLL_ADJ(p1) & 0x0F) << 0);
	mt9t112_reg_mask_set(ret, client, 0x0012, 0x0fff, val);

	/* P4/P5/P6 */
	val = (0x7         << 12) |
	      ((PLL_ADJ(p6) & 0x0F) <<  8) |
	      ((PLL_ADJ(p5) & 0x0F) <<  4) |
	      ((PLL_ADJ(p4) & 0x0F) <<  0);
	mt9t112_reg_mask_set(ret, client, 0x002A, 0x7fff, val);

	/* P7 */
	val = (0x1         << 12) |
	      ((PLL_ADJ(p7) & 0x0F) <<  0);
	mt9t112_reg_mask_set(ret, client, 0x002C, 0x100f, val);

	return ret;
}

static int mt9t112_init_pll(const struct i2c_client *client)
{
	struct mt9t112_priv *priv = i2c_get_clientdata(client);
	int data, i, ret;

	mt9t112_reg_mask_set(ret, client, 0x0014, 0x003, 0x0001);

	/* PLL control: BYPASS PLL = 8517 */
	mt9t112_reg_write(ret, client, 0x0014, 0x2145);

	/* Replace these registers when new timing parameters are generated */
	mt9t112_set_pll_dividers(client,
				 priv->info.divider.m,
				 priv->info.divider.n,
				 priv->info.divider.p1,
				 priv->info.divider.p2,
				 priv->info.divider.p3,
				 priv->info.divider.p4,
				 priv->info.divider.p5,
				 priv->info.divider.p6,
				 priv->info.divider.p7);

	/*
	 * TEST_BYPASS  on
	 * PLL_ENABLE   on
	 * SEL_LOCK_DET on
	 * TEST_BYPASS  off
	 */
	mt9t112_reg_write(ret, client, 0x0014, 0x2525);
	mt9t112_reg_write(ret, client, 0x0014, 0x2527);
	mt9t112_reg_write(ret, client, 0x0014, 0x3427);
	mt9t112_reg_write(ret, client, 0x0014, 0x3027);

	mdelay(10);

	/*
	 * PLL_BYPASS off
	 * Reference clock count
	 * I2C Master Clock Divider
	 */
	mt9t112_reg_write(ret, client, 0x0014, 0x3046);
	mt9t112_reg_write(ret, client, 0x0022, 0x0190);
	mt9t112_reg_write(ret, client, 0x3B84, 0x0212);

	/* External sensor clock is PLL bypass */
	mt9t112_reg_write(ret, client, 0x002E, 0x0500);

	mt9t112_reg_mask_set(ret, client, 0x0018, 0x0002, 0x0002);
	mt9t112_reg_mask_set(ret, client, 0x3B82, 0x0004, 0x0004);

	/* MCU disabled */
	mt9t112_reg_mask_set(ret, client, 0x0018, 0x0004, 0x0004);

	/* out of standby */
	mt9t112_reg_mask_set(ret, client, 0x0018, 0x0001, 0);

	mdelay(50);

	/*
	 * Standby Workaround
	 * Disable Secondary I2C Pads
	 */
	mt9t112_reg_write(ret, client, 0x0614, 0x0001);
	mdelay(1);
	mt9t112_reg_write(ret, client, 0x0614, 0x0001);
	mdelay(1);
	mt9t112_reg_write(ret, client, 0x0614, 0x0001);
	mdelay(1);
	mt9t112_reg_write(ret, client, 0x0614, 0x0001);
	mdelay(1);
	mt9t112_reg_write(ret, client, 0x0614, 0x0001);
	mdelay(1);
	mt9t112_reg_write(ret, client, 0x0614, 0x0001);
	mdelay(1);

	/* poll to verify out of standby. Must Poll this bit */
	for (i = 0; i < 100; i++) {
		mt9t112_reg_read(data, client, 0x0018);
		if (!(0x4000 & data))
			break;

		mdelay(10);
	}

	return ret;
}

static int mt9t112_init_setting(const struct i2c_client *client)
{

	int ret;

	/* Output Width (A) */
	mt9t112_mcu_write(ret, client, VAR(26, 0), 640);

	/* Output Height (A) */
	mt9t112_mcu_write(ret, client, VAR(26, 2), 480);

	/* Adaptive Output Clock (A) */
	mt9t112_mcu_mask_set(ret, client, VAR(26, 160), 0x0040, 0x0000);

	/* Row Start (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 2), 0);

	/* Column Start (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 4), 0);

	/* Row End (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 6), 1549);

	/* Column End (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 8), 2061);

	/* Read Mode (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 12), 0x046C);

	/* Fine Correction (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 15), 0x00CC);

	/* Fine IT Min (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 17), 0x0381);

	/* Fine IT Max Margin (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 19), 0x024F);

	/* Base Frame Lines (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 29), 0x0378);

	/* Min Line Length (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 31), 0x05D0);

	/* Line Length (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 37), 0x07AC);

	/* Context Width (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 43), 8 + 1024);

	/* Context Height (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 45), 8 + 768);

	/* Output Width (B) */
	mt9t112_mcu_write(ret, client, VAR(27, 0), 2048);

	/* Output Hieght (B) */
	mt9t112_mcu_write(ret, client, VAR(27, 2), 1536);

	/* Adaptive Output Clock (B) */
	mt9t112_mcu_mask_set(ret, client, VAR(27, 160), 0x0040, 0x0000);

	/* Row Start (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 74), 0x004);

	/* Column Start (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 76), 0x004);

	/* Row End (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 78), 0x60B);

	/* Column End (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 80), 0x80B);

	/* Fine Correction (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 87), 0x008C);

	/* Fine IT Min (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 89), 0x01F1);

	/* Fine IT Max Margin (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 91), 0x00FF);

	/* Base Frame Lines (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 101), 0x066C);

	/* Min Line Length (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 103), 0x0378);

	/* Line Length (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 109), 0x0CB1);

	/* Context Width (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 115), 8 + 2048);

	/* Context Height (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 117), 8 + 1536);

	/*
	 * Flicker Dectection registers
	 * This section should be replaced whenever new Timing file is generated
	 * All the following registers need to be replaced
	 * Following registers are generated from Register Wizard but user can
	 * modify them. For detail see auto flicker detection tuning
	 */

	/* FD_FDPERIOD_SELECT */
	mt9t112_mcu_write(ret, client, VAR8(8, 5), 0x01);

	/* PRI_B_CONFIG_FD_ALGO_RUN */
	mt9t112_mcu_write(ret, client, VAR(27, 17), 0x0003);

	/* PRI_A_CONFIG_FD_ALGO_RUN */
	mt9t112_mcu_write(ret, client, VAR(26, 17), 0x0003);

	/*
	 * AFD range detection tuning registers
	 */

	/* search_f1_50 */
	mt9t112_mcu_write(ret, client, VAR8(18, 165), 0x25);

	/* search_f2_50 */
	mt9t112_mcu_write(ret, client, VAR8(18, 166), 0x28);

	/* search_f1_60 */
	mt9t112_mcu_write(ret, client, VAR8(18, 167), 0x2C);

	/* search_f2_60 */
	mt9t112_mcu_write(ret, client, VAR8(18, 168), 0x2F);

	/* period_50Hz (A) */
	mt9t112_mcu_write(ret, client, VAR8(18, 68), 0xBA);

	/* secret register by aptina */
	/* period_50Hz (A MSB) */
	mt9t112_mcu_write(ret, client, VAR8(18, 303), 0x00);

	/* period_60Hz (A) */
	mt9t112_mcu_write(ret, client, VAR8(18, 69), 0x9B);

	/* secret register by aptina */
	/* period_60Hz (A MSB) */
	mt9t112_mcu_write(ret, client, VAR8(18, 301), 0x00);

	/* period_50Hz (B) */
	mt9t112_mcu_write(ret, client, VAR8(18, 140), 0x82);

	/* secret register by aptina */
	/* period_50Hz (B) MSB */
	mt9t112_mcu_write(ret, client, VAR8(18, 304), 0x00);

	/* period_60Hz (B) */
	mt9t112_mcu_write(ret, client, VAR8(18, 141), 0x6D);

	/* secret register by aptina */
	/* period_60Hz (B) MSB */
	mt9t112_mcu_write(ret, client, VAR8(18, 302), 0x00);

	/* FD Mode */
	mt9t112_mcu_write(ret, client, VAR8(8, 2), 0x10);

	/* Stat_min */
	mt9t112_mcu_write(ret, client, VAR8(8, 9), 0x02);

	/* Stat_max */
	mt9t112_mcu_write(ret, client, VAR8(8, 10), 0x03);

	/* Min_amplitude */
	mt9t112_mcu_write(ret, client, VAR8(8, 12), 0x0A);

	/* RX FIFO Watermark (A) */
	mt9t112_mcu_write(ret, client, VAR(18, 70), 0x0080);

	/* RX FIFO Watermark (B) */
	mt9t112_mcu_write(ret, client, VAR(18, 142), 0x0080);

	/* MCLK: 24MHz
	 * PCLK: 73MHz
	 * CorePixCLK: 36.5 MHz
	 */
	mt9t112_mcu_write(ret, client, VAR8(18, 0x0044), 11);
	mt9t112_mcu_write(ret, client, VAR8(18, 0x012F), 1);
	mt9t112_mcu_write(ret, client, VAR8(18, 0x0045), 222);
	mt9t112_mcu_write(ret, client, VAR8(18, 0x012D), 0);
	mt9t112_mcu_write(ret, client, VAR8(18, 0x008c), 161);
	mt9t112_mcu_write(ret, client, VAR8(18, 0x0130), 0);
	mt9t112_mcu_write(ret, client, VAR8(18, 0x008d), 134);
	mt9t112_mcu_write(ret, client, VAR8(18, 0x012E), 0);

	mt9t112_mcu_write(ret, client, VAR8(18, 0x00A5), 36);
	mt9t112_mcu_write(ret, client, VAR8(18, 0x00a6), 38);
	mt9t112_mcu_write(ret, client, VAR8(18, 0x00a7), 43);
	mt9t112_mcu_write(ret, client, VAR8(18, 0x00a8), 45);

	return ret;
}

static int mt9t112_auto_focus_setting(const struct i2c_client *client)
{
	int ret;

	mt9t112_mcu_write(ret, client, VAR(12, 13),	0x000F);
	mt9t112_mcu_write(ret, client, VAR(12, 23),	0x0F0F);
	mt9t112_mcu_write(ret, client, VAR8(1, 0),	0x06);

	mt9t112_reg_write(ret, client, 0x0614, 0x0000);

	mt9t112_mcu_write(ret, client, VAR8(1, 0),	0x05);
	mt9t112_mcu_write(ret, client, VAR8(12, 2),	0x02);
	mt9t112_mcu_write(ret, client, VAR(12, 3),	0x0002);
	mt9t112_mcu_write(ret, client, VAR(17, 3),	0x8001);
	mt9t112_mcu_write(ret, client, VAR(17, 11),	0x0025);
	mt9t112_mcu_write(ret, client, VAR(17, 13),	0x0193);
	mt9t112_mcu_write(ret, client, VAR8(17, 33),	0x18);
	mt9t112_mcu_write(ret, client, VAR8(1, 0),	0x05);

	return ret;
}

static int mt9t112_auto_focus_trigger(const struct i2c_client *client)
{
	int ret;

	mt9t112_mcu_write(ret, client, VAR8(12, 25), 0x01);

	return ret;
}

static int mt9t112_goto_preview(const struct i2c_client *client)
{
	int ret, trycount = 0;

	/* Is it already in preview mode? */
	mt9t112_mcu_read(ret, client, VAR8(1, 1));
	if (ret == 0x3)
		return 0;

	/* Go to preview mode */
	mt9t112_mcu_write(ret, client, VAR8(1, 0), 1);
	do {
		mt9t112_mcu_read(ret, client, VAR8(1, 1));
		mdelay(1);
	} while ((ret != 0x3) && (++trycount < 100));

	if (trycount >= 100)
		return -EBUSY;

	return 0;
}

static int mt9t112_goto_capture(const struct i2c_client *client)
{
	int ret, trycount = 0;

	/* Is it already in capture mode? */
	mt9t112_mcu_read(ret, client, VAR8(1, 1));
	if (ret == 0x7)
		return 0;

	/* Num Frames Run (B) */
	mt9t112_mcu_write(ret, client, VAR(27, 5), 0);

	/* Go to capture mode */
	mt9t112_mcu_write(ret, client, VAR8(1, 0), 2);
	do {
		mt9t112_mcu_read(ret, client, VAR8(1, 1));
		mdelay(1);
	} while ((ret != 0x7) && (++trycount < 100));

	if (trycount >= 100)
		return -EBUSY;

	return 0;
}

static int mt9t112_init_camera(const struct i2c_client *client)
{
	int ret;

	ECHECKER(ret, mt9t112_reset(client));

	ECHECKER(ret, mt9t112_init_pll(client));

	ECHECKER(ret, mt9t112_init_setting(client));

	ECHECKER(ret, mt9t112_auto_focus_setting(client));

	mt9t112_reg_mask_set(ret, client, 0x0018, 0x0004, 0);

	/* Analog setting B */
	mt9t112_reg_write(ret, client, 0x3084, 0x2409);
	mt9t112_reg_write(ret, client, 0x3092, 0x0A49);
	mt9t112_reg_write(ret, client, 0x3094, 0x4949);
	mt9t112_reg_write(ret, client, 0x3096, 0x4950);

	/*
	 * Disable adaptive clock
	 * PRI_A_CONFIG_JPEG_OB_TX_CONTROL_VAR
	 * PRI_B_CONFIG_JPEG_OB_TX_CONTROL_VAR
	 */
	mt9t112_mcu_write(ret, client, VAR(26, 160), 0x0A2E);
	mt9t112_mcu_write(ret, client, VAR(27, 160), 0x0A2E);

	/* Disable Dac_TXLO */
	mt9t112_reg_write(ret, client, 0x316C, 0x350F);

	/* Set max slew rates */
	mt9t112_reg_write(ret, client, 0x1E, 0x777);

	return ret;
}

static int mt9t112_set_params(struct i2c_client *client, u32 width, u32 height,
			      u32 pixelformat)
{
	struct mt9t112_priv *priv = i2c_get_clientdata(client);
	int i;

	/*
	 * frame size check
	 */
	mt9t112_frame_check(&width, &height);

	/*
	 * get color format
	 */
	for (i = 0; i < ARRAY_SIZE(mt9t112_formats); i++)
		if (mt9t112_formats[i].pixelformat == pixelformat)
			break;

	if (i == ARRAY_SIZE(mt9t112_formats))
		return -EINVAL;

	priv->pix.width  = (u16)width;
	priv->pix.height = (u16)height;

	priv->pix.pixelformat = pixelformat;

	return 0;
}

static int mt9t112_v4l2_int_cropcap(struct v4l2_int_device *s,
				    struct v4l2_cropcap *a)
{
	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= VGA_WIDTH;
	a->bounds.height		= VGA_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int mt9t112_v4l2_int_g_crop(struct v4l2_int_device *s,
				   struct v4l2_crop *a)
{
	a->c.left	= 0;
	a->c.top	= 0;
	a->c.width	= VGA_WIDTH;
	a->c.height	= VGA_HEIGHT;
	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int mt9t112_v4l2_int_s_crop(struct v4l2_int_device *s,
				   struct v4l2_crop *a)
{
	if ((a->c.left != 0) ||
	    (a->c.top != 0) ||
	    (a->c.width != VGA_WIDTH) ||
	    (a->c.height != VGA_HEIGHT)) {
		return -EINVAL;
	}
	return 0;
}

static int mt9t112_v4l2_int_g_fmt_cap(struct v4l2_int_device *s,
				      struct v4l2_format *f)
{
	struct mt9t112_priv *priv = s->priv;
	struct i2c_client *client = priv->client;

	if ((priv->pix.pixelformat == 0) ||
	    (priv->pix.width == 0) ||
	    (priv->pix.height == 0)) {
		int ret = mt9t112_set_params(client, VGA_WIDTH, VGA_HEIGHT,
					     V4L2_PIX_FMT_YUYV);
		if (ret < 0)
			return ret;
	}

	f->fmt.pix.width	= priv->pix.width;
	f->fmt.pix.height	= priv->pix.height;
	/* TODO: set colorspace */
	f->fmt.pix.pixelformat	= priv->pix.pixelformat;
	f->fmt.pix.field	= V4L2_FIELD_NONE;

	return 0;
}


static int mt9t112_v4l2_int_s_fmt_cap(struct v4l2_int_device *s,
				      struct v4l2_format *f)
{
	struct mt9t112_priv *priv = s->priv;
	struct i2c_client *client = priv->client;

	/* TODO: set colorspace */
	return mt9t112_set_params(client, f->fmt.pix.width, f->fmt.pix.height,
				  f->fmt.pix.pixelformat);
}

static int mt9t112_v4l2_int_try_fmt_cap(struct v4l2_int_device *s,
					struct v4l2_format *f)
{
	mt9t112_frame_check(&f->fmt.pix.width, &f->fmt.pix.height);

	/* TODO: set colorspace */
	f->fmt.pix.field = V4L2_FIELD_NONE;

	return 0;
}

static int mt9t112_v4l2_int_enum_fmt_cap(struct v4l2_int_device *s,
					 struct v4l2_fmtdesc *fmt)
{
	int index = fmt->index;
	enum v4l2_buf_type type = fmt->type;

	memset(fmt, 0, sizeof(*fmt));
	fmt->index = index;
	fmt->type = type;

	switch (fmt->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (index >= ARRAY_SIZE(mt9t112_formats))
			return -EINVAL;
	break;
	default:
		return -EINVAL;
	}

	fmt->flags = mt9t112_formats[index].flags;
	strlcpy(fmt->description, mt9t112_formats[index].description,
					sizeof(fmt->description));
	fmt->pixelformat = mt9t112_formats[index].pixelformat;
	return 0;
}

static int mt9t112_v4l2_int_s_parm(struct v4l2_int_device *s,
				   struct v4l2_streamparm *a)
{
	/* TODO: set paramters */
	return 0;
}

static int mt9t112_v4l2_int_g_parm(struct v4l2_int_device *s,
				   struct v4l2_streamparm *a)
{
	struct v4l2_captureparm *cparm = &a->parm.capture;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(a, 0, sizeof(*a));
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	cparm->capability = V4L2_CAP_TIMEPERFRAME;
	/* FIXME: Is 30 fps really the only option? */
	cparm->timeperframe.numerator = 1;
	cparm->timeperframe.denominator = 30;

	return 0;
}

/************************************************************************


			i2c driver


************************************************************************/

static int mt9t112_detect(struct i2c_client *client)
{
	struct mt9t112_priv *priv = i2c_get_clientdata(client);
	const char          *devname;
	int                  chipid;

	/*
	 * check and show chip ID
	 */
	mt9t112_reg_read(chipid, client, 0x0000);

	switch (chipid) {
	case 0x2680:
		devname = "mt9t111";
		priv->model = V4L2_IDENT_MT9T111;
		break;
	case 0x2682:
		devname = "mt9t112";
		priv->model = V4L2_IDENT_MT9T112;
		break;
	default:
		dev_err(&client->dev, "Product ID error %04x\n", chipid);
		return -ENODEV;
	}

	dev_info(&client->dev, "%s chip ID %04x\n", devname, chipid);

	return 0;
}

static int mt9t112_v4l2_int_s_power(struct v4l2_int_device *s,
				    enum v4l2_power power)
{
	struct mt9t112_priv *priv = s->priv;
	struct i2c_client *client = priv->client;
	u16 param = (MT9T112_FLAG_PCLK_RISING_EDGE &
		     priv->info.flags) ? 0x0001 : 0x0000;
	int ret;

	switch (power) {
	case V4L2_POWER_STANDBY:
		/* FALLTHROUGH */
	case V4L2_POWER_OFF:
		ret = priv->pdata->power_set(s, power);
		if (ret < 0) {
			dev_err(&client->dev, "Unable to set
			 target board power " "state (OFF/STANDBY)\n");
			return ret;
		}
		break;
	case V4L2_POWER_ON:
		ret = priv->pdata->power_set(s, power);
		if (ret < 0) {
			dev_err(&client->dev, "Unable to set
		 target board power " "state (ON)\n");
			return ret;
		}
		if (!(priv->flags & INIT_DONE)) {
			ECHECKER(ret, mt9t112_detect(client));

			priv->flags |= INIT_DONE;
		}

		ECHECKER(ret, mt9t112_init_camera(client));

		/* Invert PCLK (Data sampled on falling edge of pixclk) */
		mt9t112_reg_write(ret, client, 0x3C20, param);

		mdelay(5);
		mt9t112_mcu_write(ret, client, VAR(26, 7),
				  mt9t112_pixfmt_to_fmt(priv->pix.pixelformat));
		mt9t112_mcu_write(ret, client, VAR(26, 9),
		  mt9t112_pixfmt_to_order(priv->pix.pixelformat));
		mt9t112_mcu_write(ret, client, VAR8(1, 0), 0x06);

		ECHECKER(ret, mt9t112_goto_preview(client));

		if ((priv->pix.width == MAX_WIDTH) &&
		    (priv->pix.height == MAX_HEIGHT)) {
			ECHECKER(ret, mt9t112_goto_capture(client));
		}

		ECHECKER(ret, mt9t112_auto_focus_trigger(client));

		dev_dbg(&client->dev, "format : %d\n", priv->pix.pixelformat);
		dev_dbg(&client->dev, "size   : %d x %d\n",
			priv->pix.width,
			priv->pix.height);

		CLOCK_INFO(client, EXT_CLOCK);
	}
	return 0;
}

static int mt9t112_v4l2_int_g_priv(struct v4l2_int_device *s, void *p)
{
	struct mt9t112_priv *priv = s->priv;

	return priv->pdata->priv_data_set(p);
}

static int mt9t112_v4l2_int_g_ifparm(struct v4l2_int_device *s,
				     struct v4l2_ifparm *p)
{
	struct mt9t112_priv *priv = s->priv;
	int rval;

	if (p == NULL)
		return -EINVAL;

	if (!priv->pdata->ifparm)
		return -EINVAL;

	rval = priv->pdata->ifparm(p);
	if (rval) {
		v4l_err(priv->client, "g_ifparm.Err[%d]\n", rval);
		return rval;
	}

	p->u.ycbcr.clock_curr = 40 * 1000000; /* temporal value */

	return 0;
}

static int mt9t112_v4l2_int_enum_framesizes(struct v4l2_int_device *s,
					    struct v4l2_frmsizeenum *frms)
{
	int ifmt;

	for (ifmt = 0; ifmt < ARRAY_SIZE(mt9t112_formats); ifmt++)
		if (mt9t112_formats[ifmt].pixelformat == frms->pixel_format)
			break;

	if (ifmt == ARRAY_SIZE(mt9t112_formats))
		return -EINVAL;

	/* Do we already reached all discrete framesizes? */
	if (frms->index >= ARRAY_SIZE(mt9t112_sizes))
		return -EINVAL;

	frms->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	frms->discrete.width = mt9t112_sizes[frms->index].width;
	frms->discrete.height = mt9t112_sizes[frms->index].height;

	return 0;

}

static int mt9t112_v4l2_int_enum_frameintervals(struct v4l2_int_device *s,
						struct v4l2_frmivalenum *frmi)
{
	int ifmt;

	for (ifmt = 0; ifmt < ARRAY_SIZE(mt9t112_formats); ifmt++)
		if (mt9t112_formats[ifmt].pixelformat == frmi->pixel_format)
			break;

	if (ifmt == ARRAY_SIZE(mt9t112_formats))
		return -EINVAL;

	if (frmi->index >= ARRAY_SIZE(mt9t112_frameintervals))
		return -EINVAL;

	frmi->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	frmi->discrete.numerator =
				mt9t112_frameintervals[frmi->index].numerator;
	frmi->discrete.denominator =
				mt9t112_frameintervals[frmi->index].denominator;
	return 0;
}

static struct v4l2_int_ioctl_desc mt9t112_ioctl_desc[] = {
	{ .num = vidioc_int_enum_framesizes_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_enum_framesizes },
	{ .num = vidioc_int_enum_frameintervals_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_enum_frameintervals },
	{ .num = vidioc_int_s_power_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_s_power },
	{ .num = vidioc_int_g_priv_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_g_priv },
	{ .num = vidioc_int_g_ifparm_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_g_ifparm },
	{ .num = vidioc_int_enum_fmt_cap_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_enum_fmt_cap },
	{ .num = vidioc_int_try_fmt_cap_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_try_fmt_cap },
	{ .num = vidioc_int_g_fmt_cap_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_g_fmt_cap },
	{ .num = vidioc_int_s_fmt_cap_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_s_fmt_cap },
	{ .num = vidioc_int_g_parm_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_g_parm },
	{ .num = vidioc_int_s_parm_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_s_parm },
	{ .num = vidioc_int_cropcap_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_cropcap },
	{ .num = vidioc_int_g_crop_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_g_crop },
	{ .num = vidioc_int_s_crop_num,
	  .func = (v4l2_int_ioctl_func *)mt9t112_v4l2_int_s_crop },
};

static struct v4l2_int_slave mt9t112_slave = {
	.ioctls = mt9t112_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(mt9t112_ioctl_desc),
};

static int mt9t112_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct mt9t112_priv        *priv;
	struct v4l2_int_device     *v4l2_int_device;
	int                         ret;

	if (!client->dev.platform_data) {
		dev_err(&client->dev, "no platform data?\n");
		return -ENODEV;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_int_device = kzalloc(sizeof(*v4l2_int_device), GFP_KERNEL);
	if (!v4l2_int_device) {
		kfree(priv);
		return -ENOMEM;
	}

	v4l2_int_device->module = THIS_MODULE;
	strncpy(v4l2_int_device->name, "mt9t112",
					 sizeof(v4l2_int_device->name));
	v4l2_int_device->type = v4l2_int_type_slave;
	v4l2_int_device->u.slave = &mt9t112_slave;
	v4l2_int_device->priv = priv;

	priv->v4l2_int_device = v4l2_int_device;
	priv->client = client;
	priv->pdata = client->dev.platform_data;

	/* Revisit: Init Sensor info settings */
	priv->info.divider.m = 24;
	priv->info.divider.n = 1;
	priv->info.divider.p1 = 0;
	priv->info.divider.p2 = 8;
	priv->info.divider.p3 = 0;
	priv->info.divider.p4 = 11;
	priv->info.divider.p5 = 11;
	priv->info.divider.p6 = 8;
	priv->info.divider.p7 = 0;
	priv->info.flags = MT9T112_FLAG_PCLK_RISING_EDGE;

	i2c_set_clientdata(client, priv);

	ret = v4l2_int_device_register(priv->v4l2_int_device);
	if (ret) {
		i2c_set_clientdata(client, NULL);
		kfree(v4l2_int_device);
		kfree(priv);
	}

	return ret;
}

static int mt9t112_remove(struct i2c_client *client)
{
	struct mt9t112_priv *priv = i2c_get_clientdata(client);

	v4l2_int_device_unregister(priv->v4l2_int_device);
	i2c_set_clientdata(client, NULL);

	kfree(priv->v4l2_int_device);
	kfree(priv);
	return 0;
}

static const struct i2c_device_id mt9t112_id[] = {
	{ "mt9t112", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9t112_id);

static struct i2c_driver mt9t112_i2c_driver = {
	.driver = {
		.name = "mt9t112",
	},
	.probe    = mt9t112_probe,
	.remove   = mt9t112_remove,
	.id_table = mt9t112_id,
};

/************************************************************************


			module function


************************************************************************/
static int __init mt9t112_module_init(void)
{
	return i2c_add_driver(&mt9t112_i2c_driver);
}

static void __exit mt9t112_module_exit(void)
{
	i2c_del_driver(&mt9t112_i2c_driver);
}

module_init(mt9t112_module_init);
module_exit(mt9t112_module_exit);

MODULE_DESCRIPTION("mt9t112 sensor driver");
MODULE_AUTHOR("Kuninori Morimoto");
MODULE_LICENSE("GPL v2");
