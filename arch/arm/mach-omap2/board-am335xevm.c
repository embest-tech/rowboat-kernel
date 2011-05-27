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
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/init.h>

#include <mach/hardware.h>
#include <mach/board-am335xevm.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/irqs.h>
#include <plat/board.h>
#include <plat/common.h>
#include <plat/asp.h>
#include <plat/mmc.h>

#include "mux.h"
#include "hsmmc.h"

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

/*
* mmc 1 & 2 is present on GP daughter board. However they work
* depending on profile selection.
*/
static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc            = 1,
		.caps           = MMC_CAP_4_BIT_DATA,
		.gpio_cd        = -EINVAL,/* Dedicated pins for CD and WP */
		.gpio_wp        = -EINVAL,
		.ocr_mask       = MMC_VDD_33_34,
	},
	{
		.mmc            = 2,
		.caps           = MMC_CAP_8_BIT_DATA,
		.gpio_cd        = -EINVAL,
		.gpio_wp        = -EINVAL,
		.ocr_mask       = MMC_VDD_33_34,
	},
	{
		.mmc            = 3,
		.caps           = MMC_CAP_4_BIT_DATA,
		.gpio_cd        = -EINVAL,
		.gpio_wp        = -EINVAL,
		.ocr_mask       = MMC_VDD_33_34,
	},
	{}      /* Terminator */
};

static u8 am33xx_iis_serializer_direction[] = {
	TX_MODE,	RX_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
};

static struct snd_platform_data am33xx_evm_snd_data = {
	.tx_dma_offset	= 0x46400000,	/* McASP1 */
	.rx_dma_offset	= 0x46400000,
	.op_mode	= DAVINCI_MCASP_IIS_MODE,
	.num_serializer = ARRAY_SIZE(am33xx_iis_serializer_direction),
	.tdm_slots	= 2,
	.serial_dir	= am33xx_iis_serializer_direction,
	.asp_chan_q	= EVENTQ_2,
	.version	= MCASP_VERSION_2,
	.txnumevt	= 1,
	.rxnumevt	= 1,
};

/*
* Daughter Card Detection.
* Every board has a ID memory (EEPROM) on board. We probe these devices at
* machine init, starting from daughter board and ending with baseboard.
* Assumptions :
*	1. Daughter boards are probed 1st. Baseboard probe is called last.
*	2. These probes are called only once.
*/
static u32 daughter_board_id;

u32 am335x_get_profile_selection(void)
{
	/*
	* TODO : update with CPLD/DIN switch read and profile
	* detection logic
	*/
	return PROFILE_0;
}
EXPORT_SYMBOL(am335x_get_profile_selection);

static void setup_bb_gp_db_config(void)
{
	u32 prof_sel = am335x_get_profile_selection();
	pr_info("Baseboard + GP daughter board detected\n");
	pr_info("Selected profile : %d\n", prof_sel);

	/*
	* TODO/REVIST -
	* Based on selected profile, configure Pin Mux, Clock setup,
	* read data from eeprom & register devices.
	*/

	/* Configure McASP */
	if ((prof_sel == PROFILE_0) || (prof_sel == PROFILE_3)) {
		/* Audio Codec is available in Profile 0 & 3 respectively */
		omap_mux_init_signal("mcasp1_aclkx",
			OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mcasp1_fsx",
			OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mcasp0_axr0",
			OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN);
		omap_mux_init_signal("mcasp0_axr1",
			OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN);

		/* register device data */
		am33xx_register_mcasp(0, &am33xx_evm_snd_data);
	}

	/* Configure MMC */
	/* Check which profile is selected */
	if ((prof_sel == PROFILE_2) || (prof_sel == PROFILE_4)) {
		/* MMC 1 & 2 is available in Profile 2 & 4 respectively */
		if (prof_sel == PROFILE_2) {
			/* MMC2 is not available in Profile 2 */
			mmc[2].mmc = 0;
		}

		if (prof_sel == PROFILE_4) {
			/* MMC1 is not available in Profile 4 */
			mmc[1].mmc = 0;

			/* Profile 4 yet to be completed. Disable till then */
			mmc[2].mmc = 0;
		}
	}

	omap2_hsmmc_init(mmc);
}

static void setup_bb_ia_db_config(void)
{
	u32 prof_sel = am335x_get_profile_selection();

	pr_info("Baseboard + IA daughter board detected\n");
	pr_info("Selected profile : %d\n", prof_sel);

	/*
	* TODO/REVIST -
	* Based on selected profile, configure Pin Mux, Clock setup,
	* read data from eeprom & register devices.
	*/
}

static void setup_bb_ipp_db_config(void)
{
	pr_info("Baseboard + IP-Phone daughter board detected\n");

	/*
	* TODO/REVIST -
	* configure Pin Mux, Clock setup, read data from eeprom
	* & register devices.
	*/

	/* Configure McASP */
	am33xx_register_mcasp(0, &am33xx_evm_snd_data);
}

static void setup_bb_only_config(void)
{
	pr_info("Baseboard only detected\n");

	/*
	* TODO/REVIST -
	* configure Pin Mux, Clock setup, read data from eeprom
	* & register devices.
	*/


	/* Configure MMC */
	/* MMC 1/2 are not accessible in "baseboard-only" mode */
	mmc[1].mmc = 0;
	mmc[2].mmc = 0;
	omap2_hsmmc_init(mmc);
}

static void am335x_set_baseboard
				(struct memory_accessor *mem_acc, void *context)
{
	/* Check for what configuration detected */
	pr_info("AM335x EVM : ");
	switch (daughter_board_id) {
	case DAUGHTER_BOARD_GP:
		setup_bb_gp_db_config();
		break;

	case DAUGHTER_BOARD_IA:
		setup_bb_ia_db_config();
		break;

	case DAUGHTER_BOARD_IPP:
		setup_bb_ipp_db_config();
		break;

	 case DAUGHTER_BOARD_NONE:
		 setup_bb_only_config();
		break;

	default:
		pr_err("Invalid EVM configuration found\n");
	}

}

static void am335x_set_daughter_board
				(struct memory_accessor *mem_acc, void *context)
{
	u32 db_status = (u32)context;

	/* update status only on daughter board detection */
	daughter_board_id = db_status;
}

u32 am335x_get_daughter_board_id(void)
{
	return daughter_board_id;
}
EXPORT_SYMBOL(am335x_get_daughter_board_id);

static struct at24_platform_data am335x_gp_eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = am335x_set_daughter_board,
	.context        = (void *)DAUGHTER_BOARD_GP,
};

static struct at24_platform_data am335x_ia_eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = am335x_set_daughter_board,
	.context        = (void *)DAUGHTER_BOARD_IA,
};

static struct at24_platform_data am335x_ipp_eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = am335x_set_daughter_board,
	.context        = (void *)DAUGHTER_BOARD_IPP,
};

static struct at24_platform_data am335x_bb_eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = am335x_set_baseboard,
	.context        = (void *)DAUGHTER_BOARD_NONE,
};

static struct i2c_board_info __initdata am335x_i2c_boardinfo[] = {
	{
		/* GP daughter board */
		I2C_BOARD_INFO("24c256", GP_DAUG_BOARD_I2C_ADDR),
		.platform_data  = &am335x_gp_eeprom_info,
	},
	{
		/* IA daughter board */
		I2C_BOARD_INFO("24c256", IA_DAUG_BOARD_I2C_ADDR),
		.platform_data  = &am335x_ia_eeprom_info,
	},
	{
		/* IP-Phone daughter board */
		I2C_BOARD_INFO("24c256", IPP_DAUG_BOARD_I2C_ADDR),
		.platform_data  = &am335x_ipp_eeprom_info,
	},
	{
		/* Baseboard */
		I2C_BOARD_INFO("24c256", BASEBOARD_I2C_ADDR),
		.platform_data  = &am335x_bb_eeprom_info,
	},
};

static void __init am335x_evm_init_irq(void)
{
	omap2_init_common_infrastructure();
	omap2_init_common_devices(NULL, NULL);
	omap_init_irq();
}

static void __init am335x_evm_i2c_init(void)
{
	/* Initially no daughter board is considered */
	daughter_board_id = DAUGHTER_BOARD_NONE;

	/*
	* There are 3 instances of I2C in AM335x but currently only one
	* instance is being used on the AM335x EVM.
	*/
	omap_register_i2c_bus(1, 100, am335x_i2c_boardinfo,
				ARRAY_SIZE(am335x_i2c_boardinfo));
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
