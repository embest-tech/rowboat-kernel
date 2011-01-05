/* mt9t112 Camera
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MT9T112_H__
#define __MT9T112_H__

#include <media/v4l2-int-device.h>

#define MT9T112_I2C_ADDR		(0x78 >> 1)

#define MT9T112_CLK_MAX			(54000000) /* 54MHz */
#define MT9T112_CLK_MIN			(6000000)  /* 6Mhz */

#define MT9T112_FLAG_PCLK_RISING_EDGE	(1 << 0)
#define MT9T112_FLAG_DATAWIDTH_8	(1 << 1) /* default width is 10 */

struct mt9t112_pll_divider {
	u8 m, n;
	u8 p1, p2, p3, p4, p5, p6, p7;
};

/*
 * mt9t112 camera info
 */
struct mt9t112_camera_info {
	u32 flags;
	struct mt9t112_pll_divider divider;
};

struct mt9t112_platform_data {
	char *master;
	int (*power_set) (struct v4l2_int_device *s, enum v4l2_power on);
	int (*ifparm) (struct v4l2_ifparm *p);
	int (*priv_data_set) (void *);
	/* Interface control params */
	bool clk_polarity;
	bool hs_polarity;
	bool vs_polarity;
};

#endif /* __MT9T112_H__ */
