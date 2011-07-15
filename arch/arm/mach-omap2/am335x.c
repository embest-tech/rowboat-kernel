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
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>

/* LCD controller is similar to DA850 */
#include <video/da8xx-fb.h>

#include <mach/board-am335xevm.h>

#include <asm/mach/map.h>

#include <plat/asp.h>
#include <plat/mmc.h>

#include "mux.h"
#include "hsmmc.h"

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

/* Module pin mux for mcasp0 */
static struct module_pinmux_config mcasp0_pin_mux[] = {
	{"mcasp0_aclkx.mcasp0_aclkx", OMAP_MUX_MODE0 |
						AM335X_PIN_INPUT_PULLDOWN},
	{"mcasp0_fsx.mcasp0_fsx", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mcasp0_axr0.mcasp0_axr0", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mcasp0_axr1.mcasp0_axr1", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{0, 0},
};

/* Module pin mux for mcasp1 */
static struct module_pinmux_config mcasp1_pin_mux[] = {
	{"mii1_crs.mcasp1_aclkx", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxerr.mcasp1_fsx", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_col.mcasp1_axr2", OMAP_MUX_MODE4 | AM335X_PIN_OUTPUT},
	{"mii1_refclk.mcasp1_axr3", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{0, 0},
};

/* Module pin mux for rgmii1 */
static struct module_pinmux_config rgmii1_pin_mux[] = {
	{"mii1_txen.rgmii1_tctl", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_rxdv.rgmii1_rctl", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_txd3.rgmii1_td3", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_txd2.rgmii1_td2", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_txd1.rgmii1_td1", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_txd0.rgmii1_td0", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_txclk.rgmii1_tclk", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"mii1_rxclk.rgmii1_rclk", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd3.rgmii1_rd3", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd2.rgmii1_rd2", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd1.rgmii1_rd1", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd0.rgmii1_rd0", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"rmii1_refclk.rmii1_refclk", OMAP_MUX_MODE0 |
						AM335X_PIN_INPUT_PULLDOWN},
	{"mdio_data.mdio_data", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mdio_clk.mdio_clk", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{0, 0},
};

/* Module pin mux for rgmii2 */
static struct module_pinmux_config rgmii2_pin_mux[] = {
	{"gpmc_a0.rgmii2_tctl", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a1.rgmii2_rctl", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_a2.rgmii2_td3", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a3.rgmii2_td2", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a4.rgmii2_td1", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a5.rgmii2_td0", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a6.rgmii2_tclk", OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"gpmc_a7.rgmii2_rclk", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_a8.rgmii2_rd3", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_a9.rgmii2_rd2", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_a10.rgmii2_rd1", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_a11.rgmii2_rd0", OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_col.rmii2_refclk", OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"mdio_data.mdio_data", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mdio_clk.mdio_clk", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
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

/* Module pin mux for LCDC */
static struct module_pinmux_config lcdc_pin_mux[] = {
	{"lcd_data0.lcd_data0",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data1.lcd_data1",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data2.lcd_data2",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data3.lcd_data3",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data4.lcd_data4",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data5.lcd_data5",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data6.lcd_data6",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data7.lcd_data7",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data8.lcd_data8",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data9.lcd_data9",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data10.lcd_data10",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data11.lcd_data11",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data12.lcd_data12",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data13.lcd_data13",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data14.lcd_data14",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data15.lcd_data15",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"gpmc_ad8.lcd_data16",		OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad9.lcd_data17",		OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad10.lcd_data18",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad11.lcd_data19",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad12.lcd_data20",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad13.lcd_data21",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad14.lcd_data22",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"gpmc_ad15.lcd_data23",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT},
	{"lcd_vsync.lcd_vsync",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_hsync.lcd_hsync",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_pclk.lcd_pclk",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_ac_bias_en.lcd_ac_bias_en", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{0, 0},
};

/* Module pin mux for mmc0 */
static struct module_pinmux_config mmc0_pin_mux[] = {
	{"mmc0_dat3.mmc0_dat3",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_dat2.mmc0_dat2",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_dat1.mmc0_dat1",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_dat0.mmc0_dat0",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_clk.mmc0_clk",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_cmd.mmc0_cmd",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mmc0_sdwp.mmc0_sdwp",	OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"mmc0_sdcd.mmc0_sdcd",	OMAP_MUX_MODE5 | AM335X_PIN_INPUT_PULLUP},
	{0, 0},
};

/* Module pin mux for mmc1 */
static struct module_pinmux_config mmc1_pin_mux[] = {
	{"gpmc_ad7.mmc1_dat7",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad6.mmc1_dat6",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad5.mmc1_dat5",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad4.mmc1_dat4",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad3.mmc1_dat3",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad2.mmc1_dat2",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad1.mmc1_dat1",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad0.mmc1_dat0",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_csn1.mmc1_clk",	OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_csn2.mmc1_cmd",	OMAP_MUX_MODE2 | AM335X_PIN_INPUT_PULLUP},
	{"uart1_rxd.mmc1_sdwp",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"mcasp0_fsx.mmc1_sdcd", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{0, 0},
};

/* Module pin mux for mmc2 */
static struct module_pinmux_config mmc2_pin_mux[] = {
	{"gpmc_ad11.mmc2_dat7",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad10.mmc2_dat6",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad9.mmc2_dat5",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad8.mmc2_dat4",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad15.mmc2_dat3",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad14.mmc2_dat2",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad13.mmc2_dat1",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad12.mmc2_dat0",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_clk.mmc2_clk",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_csn3.mmc2_cmd",	OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLUP},
	{0, 0},
};

/*
* Module Platform data.
* Place all Platform specific data below.
*/

/* Audio Platform Data */
static u8 am335x_iis_serializer_direction0[] = {
	TX_MODE,	RX_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
};

static struct snd_platform_data am335x_evm_snd_data0 = {
	.tx_dma_offset	= 0x46000000,	/* McASP0 */
	.rx_dma_offset	= 0x46000000,
	.op_mode	= DAVINCI_MCASP_IIS_MODE,
	.num_serializer	= ARRAY_SIZE(am335x_iis_serializer_direction0),
	.tdm_slots	= 2,
	.serial_dir	= am335x_iis_serializer_direction0,
	.asp_chan_q	= EVENTQ_2,
	.version	= MCASP_VERSION_2,
	.txnumevt	= 1,
	.rxnumevt	= 1,
};

static u8 am335x_iis_serializer_direction1[] = {
	INACTIVE_MODE,	INACTIVE_MODE,	TX_MODE,	RX_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,	INACTIVE_MODE,
};

static struct snd_platform_data am335x_evm_snd_data1 = {
	.tx_dma_offset	= 0x46400000,	/* McASP1 */
	.rx_dma_offset	= 0x46400000,
	.op_mode	= DAVINCI_MCASP_IIS_MODE,
	.num_serializer	= ARRAY_SIZE(am335x_iis_serializer_direction1),
	.tdm_slots	= 2,
	.serial_dir	= am335x_iis_serializer_direction1,
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


static const struct display_panel disp_panel = {
	WVGA,
	32,
	32,
	COLOR_ACTIVE,
};

static struct lcd_ctrl_config lcd_cfg = {
	&disp_panel,
	.ac_bias		= 255,
	.ac_bias_intrpt		= 0,
	.dma_burst_sz		= 16,
	.bpp			= 32,
	.fdd			= 255,
	.tft_alt_mode		= 0,
	.stn_565_mode		= 0,
	.mono_8bit_mode		= 0,
	.invert_line_clock	= 1,
	.invert_frm_clock	= 1,
	.sync_edge		= 0,
	.sync_ctrl		= 1,
	.raster_order		= 0,
};

struct da8xx_lcdc_platform_data TFC_S9700RTWV35TR_01B_pdata = {
	.manu_name		= "ThreeFive",
	.controller_data	= &lcd_cfg,
	.type			= "TFC_S9700RTWV35TR_01B",
};

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc            = 1,
		.caps           = MMC_CAP_4_BIT_DATA,
		.gpio_cd        = -EINVAL,/* Dedicated pins for CD and WP */
		.gpio_wp        = -EINVAL,
		.ocr_mask       = MMC_VDD_33_34,
	},
	{
		.mmc            = 0,	/* will be set at runtime */
	},
	{
		.mmc            = 0,	/* will be set at runtime */
	},
	{}      /* Terminator */
};

/*
* Module Initialization/setup function.
* Place all module init/setup function call below.
*/

/* Setup McASP 0 & 1 */

static void mcasp0_init(int evm_id, int profile)
{
	/* Configure McASP */
	am335x_register_mcasp0(&am335x_evm_snd_data0);
	return;
}

static void mcasp1_init(int evm_id, int profile)
{
	/* Configure McASP */
	am335x_register_mcasp1(&am335x_evm_snd_data1);
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

/* setup lcd */

static void __init conf_disp_pll(struct platform_device *lcdc_device)
{
	int ret;
	struct clk *lcdc_clk;
	struct clk *lcdc_parent;
	struct clk *disp_pll;

	lcdc_clk = clk_get(&lcdc_device->dev, NULL);
	if (IS_ERR(lcdc_clk)) {
		pr_err("Cannot request lcdc_fck\n");
		goto error0;
	}

	lcdc_parent = clk_get(NULL, "disp_div2_ck");
	if (IS_ERR(lcdc_parent)) {
		pr_err("Cannot request lcdc_parent\n");
		goto error1;
	}
	ret = clk_set_parent(lcdc_clk, lcdc_parent);

	disp_pll = clk_get(NULL, "dpll_disp_ck");
	if (IS_ERR(disp_pll)) {
		pr_err("Cannot request disp_pll\n");
		goto error2;
	}

	clk_set_rate(disp_pll, 600000000);
	return;
error2:
	clk_put(lcdc_parent);
error1:
	clk_put(lcdc_clk);
error0:
	pr_err("Failed to configure display PLL\n");
	return;
}

static void lcdc_init(int evm_id, int profile)
{
	struct platform_device *lcdc_device;
	lcdc_device = am33xx_register_lcdc(&TFC_S9700RTWV35TR_01B_pdata);
	if (lcdc_device != NULL)
		conf_disp_pll(lcdc_device);
	else
		pr_info("Failed to register LCDC device\n");
	return;
}

static void mmc1_init(int evm_id, int profile)
{
	mmc[1].mmc = 2;
	mmc[1].caps = MMC_CAP_8_BIT_DATA;
	mmc[1].gpio_cd = -EINVAL;
	mmc[1].gpio_wp = -EINVAL;
	mmc[1].ocr_mask = MMC_VDD_33_34;

	/* mmc will be initialized when mmc)_init is called */
	return;
}

static void mmc2_init(int evm_id, int profile)
{
	mmc[2].mmc = 3;
	mmc[2].caps = MMC_CAP_8_BIT_DATA;
	mmc[2].gpio_cd = -EINVAL;
	mmc[2].gpio_wp = -EINVAL;
	mmc[2].ocr_mask = MMC_VDD_33_34;

	/* mmc will be initialized when mmc)_init is called */
	return;
}

static void mmc0_init(int evm_id, int profile)
{
	omap2_hsmmc_init(mmc);
	return;
}

/* Low-Cost EVM */
static struct evm_dev_cfg low_cost_evm_dev_cfg[] = {
	{rgmii1_pin_mux, NULL, PROFILE_NONE},
	{mmc0_pin_mux, mmc0_init, PROFILE_NONE},
	{0, 0, 0},
};

/* General Purpose EVM */
static struct evm_dev_cfg gen_purp_evm_dev_cfg[] = {
	{mcasp1_pin_mux, mcasp1_init, (PROFILE_0 | PROFILE_3) },
	{mcasp0_pin_mux, mcasp0_init, PROFILE_7},
	{rgmii1_pin_mux, NULL, PROFILE_ALL},
	{rgmii2_pin_mux, NULL, (PROFILE_1 | PROFILE_2 | PROFILE_4 |
								PROFILE_6) },
	{spi0_pin_mux, spi0_init, PROFILE_2},
	{lcdc_pin_mux, lcdc_init, (PROFILE_0 | PROFILE_1 | PROFILE_2
			| PROFILE_7) },
	{mmc1_pin_mux, mmc1_init, PROFILE_2},
	{mmc2_pin_mux, mmc2_init, PROFILE_4},
	{mmc0_pin_mux, mmc0_init, PROFILE_ALL},
	{0, 0, 0},
};

/* Industrial Auto Motor Control EVM */
static struct evm_dev_cfg ind_auto_mtrl_evm_dev_cfg[] = {
	{spi1_pin_mux, spi1_init, PROFILE_ALL},
	{mmc0_pin_mux, mmc0_init, PROFILE_ALL},
	{0, 0, 0},
};

/* IP-Phone EVM */
static struct evm_dev_cfg ip_phn_evm_dev_cfg[] = {
	{mcasp1_pin_mux, mcasp1_init, PROFILE_NONE},
	{mmc0_pin_mux, mmc0_init, PROFILE_NONE},
	{rgmii1_pin_mux, NULL, PROFILE_NONE},
	{rgmii2_pin_mux, NULL, PROFILE_NONE},
	{lcdc_pin_mux, lcdc_init, PROFILE_NONE},
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
