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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/phy.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/gpio.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/input/ti_tscadc.h>

/* LCD controller is similar to DA850 */
#include <video/da8xx-fb.h>

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

#include "board-flash.h"
#include "mux.h"
#include "hsmmc.h"

#define AM335X_LCD_BL_PIN	GPIO_TO_PIN(0, 7)

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

/* AM335X - CPLD Register Offsets */
#define	CPLD_DEVICE_HDR	0x00 /* CPLD Header */
#define	CPLD_DEVICE_ID	0x04 /* CPLD identification */
#define	CPLD_DEVICE_REV	0x0C /* Revision of the CPLD code */
#define	CPLD_CFG_REG	0x10 /* Configuration Register */

/* TLK PHY IDs */
#define TLK110_PHY_ID		0x2000A201
#define TLK110_PHY_MASK		0xfffffff0

/* TLK110 PHY register offsets */
#define TLK110_COARSEGAIN_REG	0x00A3
#define TLK110_LPFHPF_REG	0x00AC
#define TLK110_SPAREANALOG_REG	0x00B9
#define TLK110_VRCR_REG		0x00D0
#define TLK110_SETFFE_REG	0x0107
#define TLK110_FTSP_REG		0x0154
#define TLK110_ALFATPIDL_REG	0x002A
#define TLK110_PSCOEF21_REG	0x0096
#define TLK110_PSCOEF3_REG	0x0097
#define TLK110_ALFAFACTOR1_REG	0x002C
#define TLK110_ALFAFACTOR2_REG	0x0023
#define TLK110_CFGPS_REG	0x0095
#define TLK110_FTSPTXGAIN_REG	0x0150
#define TLK110_SWSCR3_REG	0x000B
#define TLK110_SCFALLBACK_REG	0x0040
#define TLK110_PHYRCR_REG	0x001F

/* TLK110 register writes values */
#define TLK110_COARSEGAIN_VAL	0x0000
#define TLK110_LPFHPF_VAL	0x8000
#define TLK110_SPANALOG_VAL	0x0000
#define TLK110_VRCR_VAL		0x0008
#define TLK110_SETFFE_VAL	0x0605
#define TLK110_FTSP_VAL		0x0255
#define TLK110_ALFATPIDL_VAL	0x7998
#define TLK110_PSCOEF21_VAL	0x3A20
#define TLK110_PSCOEF3_VAL	0x003F
#define TLK110_ALFACTOR1_VAL	0xFF80
#define TLK110_ALFACTOR2_VAL	0x021C
#define TLK110_CFGPS_VAL	0x0000
#define TLK110_FTSPTXGAIN_VAL	0x6A88
#define TLK110_SWSCR3_VAL	0x0000
#define TLK110_SCFALLBACK_VAL	0xC11D
#define TLK110_PHYRCR_VAL	0x4000

#ifdef CONFIG_TLK110_WORKAROUND
#define am335x_tlk110_phy_init()\
	do {	\
		phy_register_fixup_for_uid(TLK110_PHY_ID,\
					TLK110_PHY_MASK,\
					am335x_tlk110_phy_fixup);\
	} while (0);
#else
#define am335x_tlk110_phy_init() do { } while (0);
#endif

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

#define EEPROM_MAC_ADDRESS_OFFSET	48 /* 4+8+4+32 */
#define EEPROM_NO_OF_MAC_ADDR		3
struct eeprom_mac {
	char mac_addr[EEPROM_NO_OF_MAC_ADDR][ETH_ALEN];
};

static struct eeprom_mac am335x_mac_config;

/*
* @mac_id - MAC 0/1/2 Address
*/
char *am335x_get_mac_addr(unsigned int mac_id)
{
	/* EEPROM stores only 3 MAC Address */
	if (mac_id > (EEPROM_NO_OF_MAC_ADDR - 1))
		mac_id = 0;

	return &am335x_mac_config.mac_addr[mac_id][0];
}

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
	{"rmii1_refclk.mcasp1_axr3", OMAP_MUX_MODE4 |
					AM335X_PIN_INPUT_PULLDOWN},
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

/* Module pin mux for mii1 */
static struct module_pinmux_config mii1_pin_mux[] = {
	{"mii1_rxerr.mii1_rxerr", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_txen.mii1_txen", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mii1_rxdv.mii1_rxdv", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_txd3.mii1_txd3", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mii1_txd2.mii1_txd2", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mii1_txd1.mii1_txd1", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mii1_txd0.mii1_txd0", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"mii1_txclk.mii1_txclk", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxclk.mii1_rxclk", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd3.mii1_rxd3", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd2.mii1_rxd2", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd1.mii1_rxd1", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mii1_rxd0.mii1_rxd0", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"mdio_data.mdio_data", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"mdio_clk.mdio_clk", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{0, 0},
};
/* Module pin mux for spi0 */
static struct module_pinmux_config spi0_pin_mux[] = {
	{"spi0_sclk.spi0_sclk", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"spi0_d0.spi0_d0", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"spi0_d1.spi0_d1", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"spi0_cs0.spi0_cs0", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{0, 0},
};

/* Module pin mux for spi1 */
static struct module_pinmux_config spi1_pin_mux[] = {
	{"mcasp0_aclkx.spi1_sclk", OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"mcasp0_fsx.spi1_d0", OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"mcasp0_axr0.spi1_d1", OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{"mcasp0_ahclkr.spi1_cs0", OMAP_MUX_MODE3 | AM335X_PIN_INPUT_PULLDOWN},
	{0, 0},
};

/* Module pin mux for LCDC */
static struct module_pinmux_config lcdc_pin_mux[] = {
	{"lcd_data0.lcd_data0",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data1.lcd_data1",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data2.lcd_data2",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data3.lcd_data3",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data4.lcd_data4",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data5.lcd_data5",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data6.lcd_data6",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data7.lcd_data7",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data8.lcd_data8",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data9.lcd_data9",		OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data10.lcd_data10",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data11.lcd_data11",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data12.lcd_data12",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data13.lcd_data13",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data14.lcd_data14",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
	{"lcd_data15.lcd_data15",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT
						       | AM335X_PULL_DISA},
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
	{"ecap0_in_pwm0_out.gpio0_7", OMAP_MUX_MODE7 | AM335X_PIN_OUTPUT},
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
	{"mcasp0_aclkr.mmc0_sdwp", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{"spi0_cs1.mmc0_sdcd",  OMAP_MUX_MODE5 | AM335X_PIN_INPUT_PULLUP},
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
	{"spi0_cs0.mmc2_sdwp",	OMAP_MUX_MODE1 | AM335X_PIN_INPUT_PULLUP},
	{"mcasp0_axr0.mmc2_sdcd", OMAP_MUX_MODE4 | AM335X_PIN_INPUT_PULLDOWN},
	{0, 0},
};

/* Module pin mux for TSC */
static struct module_pinmux_config tsc_pin_mux[] = {
	{"ain0.ain0",           OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"ain1.ain1",           OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"ain2.ain2",           OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"ain3.ain3",           OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"vrefp.vrefp",         OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"vrefn.vrefn",         OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{0, 0},
};

/* Module pin mux for nand */
static struct module_pinmux_config nand_pin_mux[] = {
	{"gpmc_ad0.gpmc_ad0",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad1.gpmc_ad1",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad2.gpmc_ad2",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad3.gpmc_ad3",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad4.gpmc_ad4",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad5.gpmc_ad5",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad6.gpmc_ad6",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad7.gpmc_ad7",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_wait0.gpmc_wait0", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"gpmc_wpn.gpmc_wpn",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{"gpmc_csn0.gpmc_csn0",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{"gpmc_advn_ale.gpmc_advn_ale",	OMAP_MUX_MODE0 |
						AM335X_PIN_OUTPUT_PULLUP},
	{"gpmc_oen_ren.gpmc_oen_ren",	OMAP_MUX_MODE0 |
						AM335X_PIN_OUTPUT_PULLUP},
	{"gpmc_wen.gpmc_wen", OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{"gpmc_ben0_cle.gpmc_ben0_cle",	OMAP_MUX_MODE0 |
						AM335X_PIN_OUTPUT_PULLUP},
	{0, 0},
};

static struct module_pinmux_config i2c0_pin_mux[] = {
	{"i2c0_sda.i2c0_sda",   OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"i2c0_scl.i2c0_scl",   OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{0,0},
};

static struct module_pinmux_config i2c1_pin_mux[] = {
	{"spi0_d1.i2c1_sda",    OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{"spi0_cs0.i2c1_scl",   OMAP_MUX_MODE2 | AM335X_PIN_OUTPUT},
	{0,0},
};

/* Module pin mux for nor device */
static struct module_pinmux_config nor_pin_mux[] = {
	{"lcd_data0.gpmc_a0",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data1.gpmc_a1",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data2.gpmc_a2",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data3.gpmc_a3",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data4.gpmc_a4",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data5.gpmc_a5",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data6.gpmc_a6",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data7.gpmc_a7",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"gpmc_a8.gpmc_a8",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"gpmc_a9.gpmc_a9",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"gpmc_a10.gpmc_a10",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"gpmc_a11.gpmc_a11",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT},
	{"lcd_data8.gpmc_a12",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data9.gpmc_a13",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data10.gpmc_a14",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data11.gpmc_a15",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data12.gpmc_a16",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data13.gpmc_a17",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data14.gpmc_a18",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"lcd_data15.gpmc_a19",	OMAP_MUX_MODE1 | AM335X_PIN_OUTPUT |
		AM335X_PULL_DISA},
	{"gpmc_a4.gpmc_a20",	OMAP_MUX_MODE4 | AM335X_PIN_OUTPUT},
	{"gpmc_a5.gpmc_a21",	OMAP_MUX_MODE4 | AM335X_PIN_OUTPUT},
	{"gpmc_a6.gpmc_a22",	OMAP_MUX_MODE4 | AM335X_PIN_OUTPUT},
	{"gpmc_ad0.gpmc_ad0",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad1.gpmc_ad1",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad2.gpmc_ad2",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad3.gpmc_ad3",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad4.gpmc_ad4",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad5.gpmc_ad5",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad6.gpmc_ad6",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad7.gpmc_ad7",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad8.gpmc_ad8",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad9.gpmc_ad9",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad10.gpmc_ad10",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad11.gpmc_ad11",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad12.gpmc_ad12",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad13.gpmc_ad13",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad14.gpmc_ad14",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_ad15.gpmc_ad15",	OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLDOWN},
	{"gpmc_csn0.gpmc_csn0",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{"gpmc_oen_ren.gpmc_oen_ren",	OMAP_MUX_MODE0 |
						AM335X_PIN_OUTPUT_PULLUP},
	{"gpmc_wen.gpmc_wen",	OMAP_MUX_MODE0 | AM335X_PIN_OUTPUT_PULLUP},
	{"gpmc_wait0.gpmc_wait0", OMAP_MUX_MODE0 | AM335X_PIN_INPUT_PULLUP},
	{"lcd_ac_bias_en.gpio2_25", OMAP_MUX_MODE7 | AM335X_PIN_INPUT_PULLDOWN},
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

static struct resource tsc_resources[] = {
	[0] = {
		.start  = AM335X_TSC_BASE,
		.end    = AM335X_TSC_BASE + SZ_8K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = AM335X_IRQ_ADC_GEN,
		.end    = AM335X_IRQ_ADC_GEN,
		.flags  = IORESOURCE_IRQ,
	},
};

struct tsc_data am335x_touchscreen_data = {
	.wires = 4,
};

static struct platform_device tsc_device = {
	.name = "tsc",
	.id = -1,
	.dev = {
		.platform_data = &am335x_touchscreen_data,
	},
	.num_resources = ARRAY_SIZE(tsc_resources),
	.resource       = tsc_resources,
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
	int status;

	status = gpio_request(AM335X_LCD_BL_PIN, "lcd bl\n");
	if (status < 0)
		pr_warn("Failed to request gpio for LCD backlight\n");

	gpio_direction_output(AM335X_LCD_BL_PIN, 1);

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

static void am335x_tsc_init(int evm_id, int profile)
{
	int err;
	err = platform_device_register(&tsc_device);
	if (err)
		pr_err("failed to register touchscreen device\n");
}

/* NAND partition information */
static struct mtd_partition am335x_nand_partitions[] = {
/* All the partition sizes are listed in terms of NAND block size */
	{
		.name           = "U-Boot-min",
		.offset         = 0,    /* Offset = 0x0 */
		.size           = SZ_128K,
		.mask_flags     = MTD_WRITEABLE,        /* force read-only */
	},
	{
		.name           = "U-Boot",
		.offset         = MTDPART_OFS_APPEND,   /* Offset = 0x128K */
		.size           = 18 * SZ_128K,
		.mask_flags     = MTD_WRITEABLE,        /* force read-only */
	},
	{
		.name           = "U-Boot Env",
		.offset         = MTDPART_OFS_APPEND,   /* Offset = 0x260000 */
		.size           = 1 * SZ_128K,
	},
	{
		.name           = "Kernel",
		.offset         = MTDPART_OFS_APPEND,   /* Offset = 0x280000 */
		.size           = 34 * SZ_128K,
	},
	{
		.name           = "File System",
		.offset         = MTDPART_OFS_APPEND,   /* Offset = 0x6C0000 */
		.size           = 1601 * SZ_128K,
	},
	{
		.name           = "Reserved",
		.offset         = MTDPART_OFS_APPEND,   /* Offset = 0xCEE0000 */
		.size           = MTDPART_SIZ_FULL,
	},
};

static void evm_nand_init(int evm_id, int profile)
{
	board_nand_init(am335x_nand_partitions,
		ARRAY_SIZE(am335x_nand_partitions), 0, 0);
}

/* NOR partition information */
static struct mtd_partition am335x_nor_partitions[] = {
	/*(U-Boot min) in the first sector */
	{
		.name		= "U-Boot-min",
		.offset		= 0,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},

	/* U-Boot full */
	{
		.name		= "U-Boot",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 2 * SZ_128K,
		.mask_flags	= 0,
	},
	/* bootloader params */
	{
		.name		= "env",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= 0,
	},
	/* kernel */
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 2 * SZ_2M,
		.mask_flags	= 0
	},
	/* file system */
	{
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	},
};

static void evm_nor_init(int evm_id, int profile)
{
	board_nor_init(am335x_nor_partitions,
		ARRAY_SIZE(am335x_nor_partitions), 0);
}

/* Ethernet TLK110 PHY register configuration */
#ifdef CONFIG_TLK110_WORKAROUND
static int am335x_tlk110_phy_fixup(struct phy_device *phydev)
{
	unsigned int val;

	/* This is done as a workaround to support TLK110 rev1.0 phy */
	val = phy_read(phydev, TLK110_COARSEGAIN_REG);
	phy_write(phydev, TLK110_COARSEGAIN_REG, (val | TLK110_COARSEGAIN_VAL));

	val = phy_read(phydev, TLK110_LPFHPF_REG);
	phy_write(phydev, TLK110_LPFHPF_REG, (val | TLK110_LPFHPF_VAL));

	val = phy_read(phydev, TLK110_SPAREANALOG_REG);
	phy_write(phydev, TLK110_SPAREANALOG_REG, (val | TLK110_SPANALOG_VAL));

	val = phy_read(phydev, TLK110_VRCR_REG);
	phy_write(phydev, TLK110_VRCR_REG, (val | TLK110_VRCR_VAL));

	val = phy_read(phydev, TLK110_SETFFE_REG);
	phy_write(phydev, TLK110_SETFFE_REG, (val | TLK110_SETFFE_VAL));

	val = phy_read(phydev, TLK110_FTSP_REG);
	phy_write(phydev, TLK110_FTSP_REG, (val | TLK110_FTSP_VAL));

	val = phy_read(phydev, TLK110_ALFATPIDL_REG);
	phy_write(phydev, TLK110_ALFATPIDL_REG, (val | TLK110_ALFATPIDL_VAL));

	val = phy_read(phydev, TLK110_PSCOEF21_REG);
	phy_write(phydev, TLK110_PSCOEF21_REG, (val | TLK110_PSCOEF21_VAL));

	val = phy_read(phydev, TLK110_PSCOEF3_REG);
	phy_write(phydev, TLK110_PSCOEF3_REG, (val | TLK110_PSCOEF3_VAL));

	val = phy_read(phydev, TLK110_ALFAFACTOR1_REG);
	phy_write(phydev, TLK110_ALFAFACTOR1_REG, (val | TLK110_ALFACTOR1_VAL));

	val = phy_read(phydev, TLK110_ALFAFACTOR2_REG);
	phy_write(phydev, TLK110_ALFAFACTOR2_REG, (val | TLK110_ALFACTOR2_VAL));

	val = phy_read(phydev, TLK110_CFGPS_REG);
	phy_write(phydev, TLK110_CFGPS_REG, (val | TLK110_CFGPS_VAL));

	val = phy_read(phydev, TLK110_FTSPTXGAIN_REG);
	phy_write(phydev, TLK110_FTSPTXGAIN_REG, (val | TLK110_FTSPTXGAIN_VAL));

	val = phy_read(phydev, TLK110_SWSCR3_REG);
	phy_write(phydev, TLK110_SWSCR3_REG, (val | TLK110_SWSCR3_VAL));

	val = phy_read(phydev, TLK110_SCFALLBACK_REG);
	phy_write(phydev, TLK110_SCFALLBACK_REG, (val | TLK110_SCFALLBACK_VAL));

	val = phy_read(phydev, TLK110_PHYRCR_REG);
	phy_write(phydev, TLK110_PHYRCR_REG, (val | TLK110_PHYRCR_VAL));

	return 0;
}
#endif

/* Low-Cost EVM */
static struct evm_dev_cfg low_cost_evm_dev_cfg[] = {
	{rgmii1_pin_mux, NULL, PROFILE_NONE},
	{mmc0_pin_mux, mmc0_init, PROFILE_NONE},
	{nand_pin_mux, evm_nand_init, PROFILE_NONE},
	{i2c0_pin_mux, NULL, PROFILE_NONE },
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
	{tsc_pin_mux, am335x_tsc_init, (PROFILE_0 | PROFILE_1 | PROFILE_2 
			| PROFILE_7) },
	{mmc1_pin_mux, mmc1_init, PROFILE_2},
	{mmc2_pin_mux, mmc2_init, PROFILE_4},
	{mmc0_pin_mux, mmc0_init, PROFILE_ALL},
	{nand_pin_mux, evm_nand_init, (PROFILE_ALL & ~PROFILE_2 & ~PROFILE_3)},
	{nor_pin_mux, evm_nor_init, PROFILE_3},
	{i2c0_pin_mux, NULL, PROFILE_ALL },
	{i2c1_pin_mux, NULL, (PROFILE_0 | PROFILE_3 | PROFILE_7)},
	{0, 0, 0},
};

/* Industrial Auto Motor Control EVM */
static struct evm_dev_cfg ind_auto_mtrl_evm_dev_cfg[] = {
	{spi1_pin_mux, spi1_init, PROFILE_ALL},
	{mmc0_pin_mux, mmc0_init, PROFILE_ALL},
	{mii1_pin_mux, NULL, PROFILE_ALL},
	{nand_pin_mux, evm_nand_init, PROFILE_ALL},
	{i2c0_pin_mux, NULL, PROFILE_ALL },
	{0, 0, 0},
};

/* IP-Phone EVM */
static struct evm_dev_cfg ip_phn_evm_dev_cfg[] = {
	{mcasp1_pin_mux, mcasp1_init, PROFILE_NONE},
	{mmc0_pin_mux, mmc0_init, PROFILE_NONE},
	{rgmii1_pin_mux, NULL, PROFILE_NONE},
	{rgmii2_pin_mux, NULL, PROFILE_NONE},
	{lcdc_pin_mux, lcdc_init, PROFILE_NONE},
	{tsc_pin_mux, am335x_tsc_init, PROFILE_NONE},
	{nand_pin_mux, evm_nand_init, PROFILE_NONE},
	{i2c0_pin_mux, NULL, PROFILE_NONE},
	{i2c1_pin_mux, NULL, PROFILE_NONE},
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

void setup_general_purpose_evm(struct eeprom_config *evm_config)
{
	u32 prof_sel = am335x_get_profile_selection();

	pr_info("AM335x General Purpose EVM Config detected\n");
	pr_info("Selected profile : %d\n", prof_sel);

	/*
	* TODO/REVIST -
	* configure Pin Mux, Clock setup & register devices.
	*/

	am335x_configure_evm_devices(GEN_PURP_EVM, prof_sel);
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

	/* Only Profile 0 is supported */
	if (prof_sel != PROFILE_0) {
		pr_err("AM335X: Only Profile 0 is supported\n");
		return;
	}

	am335x_configure_evm_devices(IND_AUT_MTR_EVM, PROFILE_0);

	/* Initialize TLK110 PHY registers for phy version 1.0 */
	am335x_tlk110_phy_init();
}

void setup_ip_phone_evm(struct eeprom_config *evm_config)
{
	pr_info("AAM335x IP Phone EVM Config detected\n");

	/*
	* TODO/REVIST -
	* configure Pin Mux, Clock setup & register devices.
	*/

	am335x_configure_evm_devices(IP_PHN_EVM, PROFILE_NONE);
}

void setup_low_cost_evm(struct eeprom_config *evm_config)
{
	pr_info("AM335x Low Cost EVM Config detected\n");

	/*
	* TODO/REVIST -
	* configure Pin Mux, Clock setup & register devices.
	*/

	am335x_configure_evm_devices(LOW_COST_EVM, PROFILE_NONE);
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
	} else if (!strncmp("A335IAMC", brd_name,
						EEPROM_BOARD_NAME_LENGTH)) {
		am335x_evm_id = IND_AUT_MTR_EVM;
	} else if (!strncmp("A335IPPH", brd_name,
						EEPROM_BOARD_NAME_LENGTH)) {
		am335x_evm_id = IP_PHN_EVM;
	} else {
		pr_warning("AM335X: Invalid board name %s!!\n", brd_name);
		pr_warning("Assuming Low Cost EVM Config\n");
	}
}

static void am335x_setup_baseboard
			(struct memory_accessor *mem_acc, void *context)
{
	int ret;
	char brd_name[9], rev[5];

	/* 1st get the MAC address from EEPROM */
	ret = mem_acc->read(mem_acc, (char *)&am335x_mac_config,
			EEPROM_MAC_ADDRESS_OFFSET, sizeof(struct eeprom_mac));
	if (ret != sizeof(struct eeprom_mac)) {
		pr_warning("AM335X: EVM Config read fail: %d\n", ret);
		return;
	}

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
	} else if (am335x_evm_id == GEN_PURP_EVM) {
		setup_general_purpose_evm(&dghtr_brd_config);
	} else if (am335x_evm_id == IND_AUT_MTR_EVM) {
		setup_ind_auto_motor_ctrl_evm(&dghtr_brd_config);
	} else if (am335x_evm_id == IP_PHN_EVM) {
		setup_ip_phone_evm(&dghtr_brd_config);
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

static struct i2c_board_info __initdata am335x_i2c_boardinfo1[] = {
	{
		I2C_BOARD_INFO("tlv320aic3x", 0x1b),
	}
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

	omap_register_i2c_bus(1, 100, am335x_i2c_boardinfo,
				ARRAY_SIZE(am335x_i2c_boardinfo));

	omap_register_i2c_bus(2, 100, am335x_i2c_boardinfo1,
		ARRAY_SIZE(am335x_i2c_boardinfo1));
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
