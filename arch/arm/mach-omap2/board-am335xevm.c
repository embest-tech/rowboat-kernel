/*
 * Copyright (C) 2011 Texas Instruments, Inc. - http://www.ti.com/
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>

#include <mach/hardware.h>
#include <mach/board-am335xevm.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/irqs.h>
#include <plat/board.h>
#include <plat/common.h>

#include "mux.h"

/* AM335X - CPLD Register Offsets */
#define	CPLD_DEVICE_HDR	0x00 /* CPLD Header */
#define	CPLD_DEVICE_ID	0x04 /* CPLD identification */
#define	CPLD_DEVICE_REV	0x0C /* Revision of the CPLD code */
#define	CPLD_CFG_REG	0x10 /* Configuration Register */

static struct i2c_client *cpld_client;

static u32 am335x_evm_id;

/*
* EVM Config held in On-Board eeprom device.
*
* Header Format
*
*  Name			Size	Contents
*			(Bytes)
*-------------------------------------------------------------
*  Header		4	0xAA, 0x55, 0x33, 0xEE
*
*  Board Name		8	Name for board in ASCII.
*				example "A33515BB" = "AM335X
				Low Cost EVM board"
*
*  Version		4	Hardware version code for board in
*				in ASCII. "1.0A" = rev.01.0A
*
* Configuration		32	Codes(TBD) to show the configuration
* option			setup on this board.
*
* Available		32720	Available space for other non-volatile
*				data.
*/
struct eeprom_config {
	u32	header;
	char board_name[8];
	u32	version;
	u8 config_opt[32];
};

static struct eeprom_config dghtr_brd_config, baseboard_config;

#define AM335X_EEPROM_HEADER		0xEE3355AA
#define EEPROM_BOARD_NAME_LENGTH		8

/* current profile if exists else PROFILE_0 on error */
u32 am335x_get_profile_selection(void)
{
	int val = 0;

	if (!cpld_client)
		/* error checking is not done in func's calling this routine.
		so return profile 0 on error */
		return PROFILE_0;

	val = i2c_smbus_read_word_data(cpld_client, CPLD_CFG_REG);
	if (val < 0)
		return PROFILE_0;
	else
		return 1L << (val & 0x7);
}
EXPORT_SYMBOL(am335x_get_profile_selection);

u32 am335x_get_am335x_evm_id(void)
{
	return am335x_evm_id;
}
EXPORT_SYMBOL(am335x_get_am335x_evm_id);

u32 am335x_get_daughter_board_rev(void)
{
	return dghtr_brd_config.version;
}
EXPORT_SYMBOL(am335x_get_daughter_board_rev);

u32 am335x_get_baseboard_rev(void)
{
	return baseboard_config.version;
}
EXPORT_SYMBOL(am335x_get_baseboard_rev);

void setup_general_purpose_evm(struct eeprom_config *evm_config)
{
	u32 prof_sel = am335x_get_profile_selection();

	pr_info("AM335x General Purpose EVM Config detected\n");
	pr_info("Selected profile : %d\n", prof_sel);

	/*
	* TODO/REVIST -
	* configure Pin Mux, Clock setup & register devices.
	*/
}

void setup_ind_auto_motor_ctrl_evm(struct eeprom_config *evm_config)
{
	u32 prof_sel = am335x_get_profile_selection();

	pr_info("AM335x Ind. Auto. Motor Control EVM Config detected\n");
	pr_info("Selected profile : %d\n", prof_sel);

	/*
	* TODO/REVIST -
	* configure Pin Mux, Clock setup & register devices.
	*/
}

void setup_ip_phone_evm(struct eeprom_config *evm_config)
{
	pr_info("AAM335x IP Phone EVM Config detected\n");

	/*
	* TODO/REVIST -
	* configure Pin Mux, Clock setup & register devices.
	*/

}

void setup_low_cost_evm(struct eeprom_config *evm_config)
{
	pr_info("AM335x Low Cost EVM Config detected\n");

	/*
	* TODO/REVIST -
	* configure Pin Mux, Clock setup & register devices.
	*/
}

static void am335x_setup_daughter_board_evm
			(struct memory_accessor *mem_acc, void *context)
{
	int ret;
	char brd_name[9], rev[5];

	/* read eeprom and get config structure */
	ret = mem_acc->read(mem_acc, (char *)&dghtr_brd_config, 0,
					sizeof(struct eeprom_config));
	if (ret != sizeof(struct eeprom_config)) {
		pr_warning("AM335X: EVM Config Read Failed: %d\n", ret);
		return;
	}

	if (dghtr_brd_config.header != AM335X_EEPROM_HEADER) {
		pr_warning("AM335X: wrong header 0x%x, expected 0x%x\n",
				dghtr_brd_config.header, AM335X_EEPROM_HEADER);
		return;
	}

	memcpy(brd_name, dghtr_brd_config.board_name, 8);
	brd_name[8] = '\0';

	memcpy(rev, (void *)&dghtr_brd_config.version, 4);
	rev[4] = '\0';

	if (!strncmp("A335GPBD", brd_name, EEPROM_BOARD_NAME_LENGTH)) {
		am335x_evm_id = GEN_PURP_EVM;
		setup_general_purpose_evm(&dghtr_brd_config);
	} else if (!strncmp("A335IAMC", brd_name,
						EEPROM_BOARD_NAME_LENGTH)) {
		am335x_evm_id = IND_AUT_MTR_EVM;
		setup_ind_auto_motor_ctrl_evm(&dghtr_brd_config);
	} else if (!strncmp("A335IPPH", brd_name,
						EEPROM_BOARD_NAME_LENGTH)) {
		am335x_evm_id = IP_PHN_EVM;
		setup_ip_phone_evm(&dghtr_brd_config);
	} else {
		pr_warning("AM335X: Invalid board name %s\n", brd_name);
	}
}

static void am335x_setup_baseboard
			(struct memory_accessor *mem_acc, void *context)
{
	int ret;
	char brd_name[9], rev[5];

	/* If any daughter board is already detected, Baseboard devices are
	*  already setup there. If no daughter board is detected, then setup
	*  devices for Low Cost EVM
	*/
	if (am335x_evm_id == LOW_COST_EVM) {
		/* read eeprom and get config structure */
		ret = mem_acc->read(mem_acc, (char *)&baseboard_config, 0,
					sizeof(struct eeprom_config));
		if (ret != sizeof(struct eeprom_config)) {
			pr_warning("AM335X: EVM Config read fail: %d\n", ret);
			return;
		}

		if (baseboard_config.header != AM335X_EEPROM_HEADER) {
			pr_warning("AM335X: wrong header 0x%x, expected 0x%x\n",
				baseboard_config.header, AM335X_EEPROM_HEADER);
			return;
		}

		memcpy(brd_name, baseboard_config.board_name, 8);
		brd_name[8] = '\0';

		memcpy(rev, (void *)&baseboard_config.version, 4);
		rev[4] = '\0';

		/* Todo : Baseboard name is yet to be finalized */
		if (!strncmp("A33515BB", brd_name,
						EEPROM_BOARD_NAME_LENGTH)) {
			setup_low_cost_evm(&baseboard_config);
		} else {
			pr_warning("AM335X: Invalid board name found %s\n",
								brd_name);
		}
	}

}

static struct at24_platform_data am335x_daughter_board_eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = am335x_setup_daughter_board_evm,
	.context        = (void *)NULL,
};

static struct at24_platform_data am335x_baseboard_eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = am335x_setup_baseboard,
	.context        = (void *)NULL,
};

/*
* Daughter board Detection.
* Every board has a ID memory (EEPROM) on board. We probe these devices at
* machine init, starting from daughter board and ending with baseboard.
* Assumptions :
*	1. probe for i2c devices are called in the order they are included in
*	   the below struct. Daughter boards eeprom are probed 1st. Baseboard
*	   eeprom probe is called last.
*/
static struct i2c_board_info __initdata am335x_i2c_boardinfo[] = {
	{
		/* Daughter Board EEPROM */
		I2C_BOARD_INFO("24c256", DAUG_BOARD_I2C_ADDR),
		.platform_data  = &am335x_daughter_board_eeprom_info,
	},
	{
		/* Baseboard board EEPROM */
		I2C_BOARD_INFO("24c256", BASEBOARD_I2C_ADDR),
		.platform_data  = &am335x_baseboard_eeprom_info,
	},
	{
		I2C_BOARD_INFO("cpld_reg", 0x35),
	},

};

static int cpld_reg_probe(struct i2c_client *client,
	    const struct i2c_device_id *id)
{
	cpld_client = client;
	return 0;
}

static int __devexit cpld_reg_remove(struct i2c_client *client)
{
	cpld_client = NULL;
	return 0;
}

static const struct i2c_device_id cpld_reg_id[] = {
	{ "cpld_reg", 0 },
	{ }
};

static struct i2c_driver cpld_reg_driver = {
	.driver = {
		.name	= "cpld_reg",
	},
	.probe		= cpld_reg_probe,
	.remove		= cpld_reg_remove,
	.id_table	= cpld_reg_id,
};

static void evm_init_cpld(void)
{
	i2c_add_driver(&cpld_reg_driver);
}


static void __init am335x_evm_i2c_init(void)
{
	/* Initially assume Low Cost EVM Config */
	am335x_evm_id = LOW_COST_EVM;

	evm_init_cpld();

	/*
	* There are 3 instances of I2C in AM335x but instance one is
	* connected to eeprom on EVM.
	*/
	omap_register_i2c_bus(1, 100, am335x_i2c_boardinfo,
				ARRAY_SIZE(am335x_i2c_boardinfo));
}

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	AM335X_MUX(UART0_CTSN, OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT),
	AM335X_MUX(UART0_RTSN, OMAP_MUX_MODE0 | AM335X_PIN_INPUT),
	AM335X_MUX(UART0_RXD, OMAP_MUX_MODE0 | AM335X_PIN_INPUT),
	AM335X_MUX(UART0_TXD, OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT),
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#else
#define	board_mux	NULL
#endif

static void __init am335x_evm_init_irq(void)
{
	omap2_init_common_infrastructure();
	omap2_init_common_devices(NULL, NULL);
	omap_init_irq();
}

static void __init am335x_evm_init(void)
{
	am335x_mux_init(board_mux);
	omap_serial_init();
	am335x_evm_i2c_init();
}

static void __init am335x_evm_map_io(void)
{
	omap2_set_globals_am335x();
	ti81xx_map_common_io();
}

MACHINE_START(AM335XEVM, "am335xevm")
	/* Maintainer: Texas Instruments */
	.boot_params	= 0x80000100,
	.map_io		= am335x_evm_map_io,
	.init_irq	= am335x_evm_init_irq,
	.init_machine	= am335x_evm_init,
	.timer		= &omap_timer,
MACHINE_END
