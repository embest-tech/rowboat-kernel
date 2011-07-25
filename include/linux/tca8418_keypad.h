/*
 * TCA8418 kepad support
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

#ifndef _TCA8418_KEYS_H
#define _TCA8418_KEYS_H

#include <linux/types.h>

struct tca8418_button {
	/* Configuration parameters */
	int code;		/* input event code (KEY_*, SW_*) */
	int active_low;
	int type;		/* input event type (EV_KEY, EV_SW) */
};

struct tca8418_keys_platform_data {
	struct tca8418_button *buttons;
	int nbuttons;
	unsigned int rep:1;	/* enable input subsystem auto repeat */
	u8 row_val;
	u8 col_val;
	int irq_is_gpio;
	int use_polling;	/* use polling if Interrupt is not connected*/
	int irq_flags;
};
#endif
