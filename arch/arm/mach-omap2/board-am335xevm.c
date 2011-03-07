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

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/irqs.h>
#include <plat/board.h>
#include <plat/common.h>

#include "mux.h"

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
