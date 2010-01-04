/*
 * linux/arch/arm/mach-omap2/board-igep0020.c
 *
 * Copyright (C) 2009 Integration Software and Electronics Engineering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>

#include <linux/regulator/machine.h>
#include <linux/i2c/twl4030.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/board-igep0020.h>
#include <mach/board.h>
#include <mach/usb-musb.h>
#include <mach/usb-ehci.h>
#include <mach/common.h>
#include <mach/gpmc.h>
#include <mach/mcspi.h>
#include <mach/mux.h>
#include <mach/omap-pm.h>
#include <mach/clock.h>
#include <mach/omapfb.h>
#include <mach/display.h>

#include "sdram-numonyx-m65kxxxxam.h"
#include "twl4030-generic-scripts.h"
#include "mmc-twl4030.h"
#include "pm.h"
#include "omap3-opp.h"

#define GPMC_CS0_BASE  0x60
#define GPMC_CS_SIZE   0x30

#if defined(CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)

#include <linux/smsc911x.h>

static struct resource igep2_smsc911x_resources[] = {
	[0] = {
		.name	= "smsc911x-memory",
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config igep2_smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct platform_device igep2_smsc911x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(igep2_smsc911x_resources),
	.resource	= igep2_smsc911x_resources,
	.dev		= {
		.platform_data = &igep2_smsc911x_config,
	},
};

static inline void __init igep2_init_smsc911x(void)
{
	unsigned long cs_mem_base;

	if (gpmc_cs_request(IGEP2_SMSC911X_CS, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed request for GPMC mem for smsc911x\n");
		return;
	}

	igep2_smsc911x_resources[0].start = cs_mem_base + 0x0;
	igep2_smsc911x_resources[0].end   = cs_mem_base + 0xff;
	if ((gpio_request(IGEP2_SMSC911X_GPIO, "SMSC911X IRQ") == 0) &&
	    (gpio_direction_input(IGEP2_SMSC911X_GPIO) == 0)) {
		gpio_export(IGEP2_SMSC911X_GPIO, 0);
	} else {
		printk(KERN_ERR "could not obtain gpio for SMSC911X IRQ\n");
		return;
	}

	igep2_smsc911x_resources[1].start = OMAP_GPIO_IRQ(IGEP2_SMSC911X_GPIO);
	igep2_smsc911x_resources[1].end	  = 0;

	platform_device_register(&igep2_smsc911x_device);
}
#else
static inline void __init igep2_init_smsc911x(void) { return; }
#endif

static struct spi_board_info igep2_spi_board_info[] __initdata = {
        {
                .modalias               = "spidev",
                .bus_num                = 1,
                .chip_select            = 3,
                .max_speed_hz           = 20000000,
                .mode                   = SPI_MODE_2,
        },
        {
                .modalias               = "spidev",
                .bus_num                = 2,
                .chip_select            = 0,
                .max_speed_hz           = 20000000,
                .mode                   = SPI_MODE_2,
        },
};

static struct omap_uart_config igep2_uart_config __initdata = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct twl4030_usb_data igep2_twl4030_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static struct twl4030_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
	{
		.mmc		= 2,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
	{}	/* Terminator */
};

static int igep2_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	twl4030_mmc_init(mmc);

	return 0;
}

static struct twl4030_gpio_platform_data igep2_twl4030_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.setup		= igep2_twl_gpio_setup,
	.use_leds	= false,
	.pullups	= BIT(1),
	.pulldowns	= BIT(2) | BIT(6) | BIT(7) | BIT(8) | BIT(13)
				| BIT(15) | BIT(16) | BIT(17),
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data igep2_vmmc1 = {
	.constraints = {
		.valid_modes_mask = REGULATOR_MODE_NORMAL
				| REGULATOR_MODE_STANDBY,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE
				| REGULATOR_CHANGE_MODE
				| REGULATOR_CHANGE_STATUS,
	},
};

/* VDAC for DSS driving S-Video (8 mA unloaded, max 65 mA) */
static struct regulator_init_data igep2_vdac = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct twl4030_platform_data igep2_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,
	.usb		= &igep2_twl4030_usb_data,
	.gpio		= &igep2_twl4030_gpio_data,
	.power		= GENERIC3430_T2SCRIPTS_DATA,
	.vmmc1		= &igep2_vmmc1,
	.vdac		= &igep2_vdac,
};

static struct i2c_board_info __initdata igep2_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &igep2_twldata,
	},
};

static int __init igep2_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, igep2_i2c_boardinfo,
			ARRAY_SIZE(igep2_i2c_boardinfo));
	/* Bus 3 is attached to the DVI port where devices like the pico DLP
	 * projector don't work reliably with 400kHz */
	omap_register_i2c_bus(3, 100, NULL, 0);
	return 0;
}

static void __init igep2_init_irq(void)
{
	omap2_init_common_hw(m65kxxxxam_sdrc_params, omap3_mpu_rate_table,
                             omap3_dsp_rate_table, omap3_l3_rate_table);
	omap_init_irq();
	omap_gpio_init();
}

/* DSS */
static int igep2_enable_dvi(struct omap_display *display)
{
       if (display->hw_config.panel_reset_gpio != -1)
               gpio_direction_output(display->hw_config.panel_reset_gpio, 1);

	return 0;
}

static void igep2_disable_dvi(struct omap_display *display)
{
       if (display->hw_config.panel_reset_gpio != -1)
               gpio_direction_output(display->hw_config.panel_reset_gpio, 0);
}

static struct omap_display_data igep2_display_data_dvi = {
       .type = OMAP_DISPLAY_TYPE_DPI,
       .name = "dvi",
       .panel_name = "panel-generic",
       .u.dpi.data_lines = 24,
       .panel_reset_gpio = 170,
       .panel_enable = igep2_enable_dvi,
       .panel_disable = igep2_disable_dvi,
};

static struct omap_dss_platform_data igep2_dss_data = {
       .num_displays = 1,
       .displays = {
               &igep2_display_data_dvi,
       }
};

static struct platform_device igep2_dss_device = {
       .name          = "omap-dss",
       .id            = -1,
       .dev            = {
               .platform_data = &igep2_dss_data,
       },
};

static void __init igep2_display_init(void)
{
       int r;

       r = gpio_request(igep2_display_data_dvi.panel_reset_gpio, "DVI reset");
       if (r < 0)
               printk(KERN_ERR "Unable to get DVI reset GPIO\n");
}

static struct omap_board_config_kernel igep2_config[] __initdata = {
	{ OMAP_TAG_UART,	&igep2_uart_config },
};

static struct platform_device *igep2_devices[] __initdata = {
	&igep2_dss_device,
};

static void __init igep2_init(void)
{
	igep2_i2c_init();
	platform_add_devices(igep2_devices, ARRAY_SIZE(igep2_devices));
	omap_board_config = igep2_config;
	omap_board_config_size = ARRAY_SIZE(igep2_config);

	spi_register_board_info(igep2_spi_board_info, ARRAY_SIZE(igep2_spi_board_info));

	omap_serial_init();
	omap_cfg_reg(J25_34XX_GPIO170);

	usb_musb_init();
	usb_ehci_init();

	igep2_flash_init();

	igep2_display_init();

	igep2_init_smsc911x();

	/* GPIO userspace leds (green & red) */
	if ((gpio_request(IGEP2_GPIO_LED_GREEN, "GPIO_LED_GREEN") == 0) && (gpio_direction_output(IGEP2_GPIO_LED_GREEN, 1) == 0)) {
		gpio_export(IGEP2_GPIO_LED_GREEN, 1);
	} else {
		printk(KERN_ERR "could not obtain gpio for " "GPIO_LED_GREEN\n");
	}
	if ((gpio_request(IGEP2_GPIO_LED_RED, "GPIO_LED_RED") == 0) && (gpio_direction_output(IGEP2_GPIO_LED_RED, 1) == 0)) {
		gpio_export(IGEP2_GPIO_LED_RED, 1);
	} else {
		printk(KERN_ERR "could not obtain gpio for " "GPIO_LED_RED\n");
	}

	/* GPIO W-LAN + Bluetooth combo module */
	if ((gpio_request(IGEP2_GPIO_WIFI_NPD, "GPIO_WIFI_NPD") == 0) && (gpio_direction_output(IGEP2_GPIO_WIFI_NPD, 1) == 0)) {
		if (gpio_export(IGEP2_GPIO_WIFI_NPD, false))
			printk(KERN_ERR "could not export gpio for" "GPIO_WIFI_NPD\n");
	} else {
		printk(KERN_ERR "could not obtain gpio for" "GPIO_WIFI_NPD\n");
	}
	if ((gpio_request(IGEP2_GPIO_WIFI_NRESET, "GPIO_WIFI_NRESET") == 0) && (gpio_direction_output(IGEP2_GPIO_WIFI_NRESET, 1) == 0)) {
		if (gpio_export(IGEP2_GPIO_WIFI_NRESET, false))
			printk(KERN_ERR "could not export gpio for " "GPIO_WIFI_NRESET\n");
		gpio_set_value(IGEP2_GPIO_WIFI_NRESET, 0);
		udelay(10);
		gpio_set_value(IGEP2_GPIO_WIFI_NRESET, 1);
	} else {
		printk(KERN_ERR "could not obtain gpio for " "GPIO_WIFI_NRESET\n");
	}

	/* GPIO USB host reset */
    if ((gpio_request(IGEP2_GPIO_EXT_PHY_USB, "GPIO_EXT_PHY_USB") == 0) && (gpio_direction_output(IGEP2_GPIO_EXT_PHY_USB, 1) == 0)) {
    	gpio_export(IGEP2_GPIO_EXT_PHY_USB, 1);
    } else {
        printk(KERN_ERR "could not obtain gpio for " "GPIO_EXT_PHY_USB\n");
    }
}

static void __init igep2_map_io(void)
{
	omap2_set_globals_35xx();
	omap2_set_sdram_vram(1280 * 1024 * 4 * 3, 0);
	omap2_map_common_io();
}

MACHINE_START(IGEP0020, "IGEP v2 board")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= igep2_map_io,
	.init_irq	= igep2_init_irq,
	.init_machine	= igep2_init,
	.timer		= &omap_timer,
MACHINE_END
