/*
 * for arch/arm/mach-omap2/board-am335xevm.c
 *
 * Code for AM335X EVM.
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

#ifndef _BOARD_AM335X_H
#define _BOARD_AM335X_H

#define BASEBOARD_I2C_ADDR		0x50
#define GP_DAUG_BOARD_I2C_ADDR	0x51
#define IA_DAUG_BOARD_I2C_ADDR	0x52
#define IPP_DAUG_BOARD_I2C_ADDR	0x53

#define DAUGHTER_BOARD_NONE	-1
#define BASEBOARD_ONLY		0  /* Baseboard only Config */
#define DAUGHTER_BOARD_GP	1  /* General Purpose Daughterboard */
#define DAUGHTER_BOARD_IA	2  /* Industrial Automation Daughterboard */
#define DAUGHTER_BOARD_IPP	3  /* IP Phone Daughterboard */

/* REVIST : check posibility of PROFILE_(x) syntax usage */
#define PROFILE_0	0
#define PROFILE_1	1
#define PROFILE_2	2
#define PROFILE_3	3
#define PROFILE_4	4
#define PROFILE_5	5
#define PROFILE_6	6
#define PROFILE_7	7

u32 am335x_get_profile_selection(void);
u32 am335x_get_daughter_board_id(void);

#endif
