/*
 * AM335X chip specific setup
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <mach/board-am335xevm.h>

#include <asm/mach/map.h>

#include <plat/asp.h>

#include "mux.h"

/* module pin mux structure */
struct module_pinmux_config {
	const char *string_name; /* signal name format */
	int val; /* Options for the mux register value */
};

struct evm_dev_cfg {
	struct module_pinmux_config *module_pin_mux; /* module pin mux */
	void (*device_init)(int evm_id, int profile);
	int profile;	/* Profiles (0-7) in which the module is present */
};

/*
* Module Pin-Mux description.
* Declare pin-mux only for those devices that are available (exmpl. under
* specific profile if the evm has profiles).
*/

/* Module pin mux for mcasp1 */
static struct module_pinmux_config mcasp1_pin_mux[] = {
	{"mcasp1_aclkx",	OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"mcasp1_fsx",		OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"mcasp1_axr2",		OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"mcasp1_axr3",		OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{0, 0},
};

static struct module_pinmux_config rgmii1_pin_mux[] = {
	{"rgmii1_tctl",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii1_rctl",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rgmii1_td3",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii1_td2",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii1_td1",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii1_td0",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii1_tclk",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii1_rclk",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rgmii1_rd3",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rgmii1_rd2",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rgmii1_rd1",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rgmii1_rd0",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rmii1_refclk",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mdio_data",		OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mdio_clk",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{0, 0},
};

/* Module pin mux for rgmii2 */
static struct module_pinmux_config rgmii2_pin_mux[] = {
	{"rgmii2_tctl",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii2_rctl",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rgmii2_td3",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii2_td2",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii2_td1",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii2_td0",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii2_tclk",		OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"rgmii2_rclk",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rgmii2_rd3",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rgmii2_rd2",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rgmii2_rd1",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rgmii2_rd0",		OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rmii2_refclk",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"mdio_data",		OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mdio_clk",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{0, 0},
};

/* Module pin mux for spi0 */
static struct module_pinmux_config spi0_pin_mux[] = {
	{"spi0_sclk",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"spi0_d0",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"spi0_d1",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"spi0_cs0",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{0, 0},
};

/* Module pin mux for spi1 */
static struct module_pinmux_config spi1_pin_mux[] = {
	{"spi1_sclk",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"spi1_d0",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"spi1_d1",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"spi1_cs0",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{0, 0},
};

/*
* Module Platform data.
* Place all Platform specific data below.
*/

/* Audio Platform Data */
static u8 am335x_iis_serializer_direction[] = {
	INACTIVE_MODE,	INACTIVE_MODE,	TX_MODE,	RX_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
};

static struct snd_platform_data am335x_evm_snd_data = {
	.tx_dma_offset	= 0x46400000,	/* McASP1 */
	.rx_dma_offset	= 0x46400000,
	.op_mode	= DAVINCI_MCASP_IIS_MODE,
	.num_serializer	= ARRAY_SIZE(am335x_iis_serializer_direction),
	.tdm_slots	= 2,
	.serial_dir	= am335x_iis_serializer_direction,
	.asp_chan_q	= EVENTQ_2,
	.version	= MCASP_VERSION_2,
	.txnumevt	= 1,
	.rxnumevt	= 1,
};

/* SPI 0/1 Platform Data */

/* SPI flash information */
struct mtd_partition am335x_spi_partitions[] = {
	/* All the partition sizes are listed in terms of erase size */
	{
		.name       = "U-Boot-min",
		.offset     = 0,
		.size       = 32 * SZ_4K,
		.mask_flags = MTD_WRITEABLE,    /* force read-only */
	},
	{
		.name       = "U-Boot",
		.offset     = MTDPART_OFS_APPEND,
		.size       = 64 * SZ_4K,
		.mask_flags = MTD_WRITEABLE,    /* force read-only */
	},
	{
		.name       = "U-Boot Env",
		.offset     = MTDPART_OFS_APPEND,
		.size       = 2 * SZ_4K,
	},
	{
		.name       = "Kernel",
		.offset     = MTDPART_OFS_APPEND,
		.size       = 640 * SZ_4K,
	},
	{
		.name       = "File System",
		.offset     = MTDPART_OFS_APPEND,
		.size       = MTDPART_SIZ_FULL,     /* size ~= 1.1 MiB */
	}
};

const struct flash_platform_data am335x_spi_flash = {
	.type      = "w25q64",
	.name      = "spi_flash",
	.parts     = am335x_spi_partitions,
	.nr_parts  = ARRAY_SIZE(am335x_spi_partitions),
};

struct spi_board_info am335x_spi0_slave_info[] = {
	{
		.modalias      = "m25p80",
		.platform_data = &am335x_spi_flash,
		.irq           = -1,
		.max_speed_hz  = 80000000,
		.bus_num       = 1,
		.chip_select   = 0,
	},
};

struct spi_board_info am335x_spi1_slave_info[] = {
	{
		.modalias      = "m25p80",
		.platform_data = &am335x_spi_flash,
		.irq           = -1,
		.max_speed_hz  = 80000000,
		.bus_num       = 2,
		.chip_select   = 0,
	},
};

/*
* Module Initialization/setup function.
* Place all module init/setup function call below.
*/

/* Setup McASP */
static void mcasp1_init(int evm_id, int profile)
{
	/* Configure McASP */
	am335x_register_mcasp(0, &am335x_evm_snd_data);
	return;
}

/* setup spi0 */
static void spi0_init(int evm_id, int profile)
{
	spi_register_board_info(am335x_spi0_slave_info,
					ARRAY_SIZE(am335x_spi0_slave_info));
	return;
}

/* setup spi1 */
static void spi1_init(int evm_id, int profile)
{
	spi_register_board_info(am335x_spi1_slave_info,
			ARRAY_SIZE(am335x_spi1_slave_info));
	return;
}

/* Low-Cost EVM */
static struct evm_dev_cfg low_cost_evm_dev_cfg[] = {
	{rgmii1_pin_mux, NULL, PROFILE_NONE},
	{0, 0, 0},
};

/* General Purpose EVM */
static struct evm_dev_cfg gen_purp_evm_dev_cfg[] = {
	{mcasp1_pin_mux, mcasp1_init, (PROFILE_0 | PROFILE_3 | PROFILE_7) },
	{rgmii1_pin_mux, NULL, PROFILE_ALL},
	{rgmii2_pin_mux, NULL, (PROFILE_1 | PROFILE_2 | PROFILE_4 |
								PROFILE_6) },
	{spi0_pin_mux, spi0_init, PROFILE_2},
	{0, 0, 0},
};

/* Industrial Auto Motor Control EVM */
static struct evm_dev_cfg ind_auto_mtrl_evm_dev_cfg[] = {
	{spi1_pin_mux, spi1_init, PROFILE_ALL},
	{0, 0, 0},
};

/* IP-Phone EVM */
static struct evm_dev_cfg ip_phn_evm_dev_cfg[] = {
	{mcasp1_pin_mux, mcasp1_init, PROFILE_NONE},
	{0, 0, 0},
};

/*
* @pin_mux - single module pin-mux structure which defines pin-mux
*			details for all its pins.
*/
static void setup_module_pin_mux(struct module_pinmux_config *pin_mux)
{
	int i;

	for (i = 0; pin_mux->string_name != NULL; pin_mux++)
		omap_mux_init_signal(pin_mux->string_name, pin_mux->val);

}

/*
* @evm_id - evm id which needs to be configured
* @dev_cfg - single evm structure which includes
*				all module inits, pin-mux defines
* @profile - if present, else PROFILE_NONE
*/
static void _configure_device(int evm_id,
			struct evm_dev_cfg *dev_cfg, int profile)
{
	int i;

	/*
	* Only General Purpose & Industrial Auto Motro Control
	* EVM has profiles. So check if this evm has profile.
	* If not, ignore the profile comparison
	*/

	if (profile == PROFILE_NONE) {
		for (i = 0; dev_cfg->module_pin_mux != NULL; dev_cfg++) {
			setup_module_pin_mux(dev_cfg->module_pin_mux);
			if (dev_cfg->device_init)
				dev_cfg->device_init(evm_id, profile);
		}
	} else {
		for (i = 0; dev_cfg->module_pin_mux != NULL; dev_cfg++) {
			if (dev_cfg->profile & profile) {
				setup_module_pin_mux(dev_cfg->module_pin_mux);
				if (dev_cfg->device_init)
					dev_cfg->device_init(evm_id, profile);
			}
		}
	}
}

/*
* @evm_id - evm id which needs to be configured
* @profile - if present, else PROFILE_NONE
*
* return Success if valid evm id is sent evm_id else -1
*/
int am335x_configure_evm_devices(int evm_id, int profile)
{
	int ret = 0;

	if (evm_id == LOW_COST_EVM) {
		_configure_device(evm_id, low_cost_evm_dev_cfg, profile);
	} else if (evm_id == GEN_PURP_EVM) {
		_configure_device(evm_id, gen_purp_evm_dev_cfg, profile);
	} else if (evm_id == IND_AUT_MTR_EVM) {
		_configure_device(evm_id, ind_auto_mtrl_evm_dev_cfg, profile);
	} else if (evm_id == IP_PHN_EVM) {
		_configure_device(evm_id, ip_phn_evm_dev_cfg, profile);
	} else {
		ret = -1;
		pr_err("AM335X : Invalid Board id %d\n", evm_id);
	}

	return ret;
}
