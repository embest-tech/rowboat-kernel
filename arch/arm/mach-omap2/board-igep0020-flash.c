/*
 * linux/arch/arm/mach-omap2/board-igep0020-flash.c
 *
 * Copyright (C) 2009 Integration Software and Electronics Engineering
 *
 * Modified from linux/arch/arm/mach-omap2/board-omap3evm-flash.c
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
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/onenand_regs.h>
#include <linux/types.h>
#include <linux/io.h>

#include <mach/board-igep0020.h>
#include <mach/board.h>
#include <mach/gpmc.h>
#include <mach/onenand.h>

#if defined(CONFIG_MTD_ONENAND_OMAP2) || \
         defined(CONFIG_MTD_ONENAND_OMAP2_MODULE)

/* NAND04GR4E1A ( x2 Flash built-in COMBO POP MEMORY )
 * Since the device is equipped with two DataRAMs, and two-plane NAND
 * Flash memory array, these two component enables simultaneous program
 * of 4KiB. Plane1 has only even blocks such as block0, block2, block4
 * while Plane2 has only odd blocks such as block1, block3, block5.
 * So MTD regards it as 4KiB page size and 256KiB block size 64*(2*2048)
 */

static struct mtd_partition igep2_onenand_partitions[] = {
	{
		.name           = "X-Loader",
		.offset         = 0,
		.size           = 2 * (64*(2*2048))
	},
	{
		.name           = "U-Boot",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 6 * (64*(2*2048)),
	},
	{
		.name           = "U-Boot Env",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 2 * (64*(2*2048)),
	},
	{
		.name           = "Kernel",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 12 * (64*(2*2048)),
	},
	{
		.name           = "File System",
		.offset         = MTDPART_OFS_APPEND,
		.size           = MTDPART_SIZ_FULL,
	},
};

static int igep2_onenand_setup(void __iomem *onenand_base, int freq)
{
       /* nothing is required to be setup for onenand as of now */
       return 0;
}

static struct omap_onenand_platform_data igep2_onenand_data = {
	.parts = igep2_onenand_partitions,
	.nr_parts = ARRAY_SIZE(igep2_onenand_partitions),
	.onenand_setup = igep2_onenand_setup,
	.dma_channel	= -1,	/* disable DMA in OMAP OneNAND driver */
};

static struct platform_device igep2_onenand_device = {
	.name		= "omap2-onenand",
	.id		= -1,
	.dev = {
		.platform_data = &igep2_onenand_data,
	},
};

void __init igep2_flash_init(void)
{
	u8		cs = 0;
	u8		onenandcs = GPMC_CS_NUM + 1;

	while (cs < GPMC_CS_NUM) {
		u32 ret = 0;
		ret = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);

		/*
		* xloader/Uboot would have programmed the NAND/oneNAND
		* base address for us This is a ugly hack. The proper
		* way of doing this is to pass the setup of u-boot up
		* to kernel using kernel params - something on the
		* lines of machineID. Check if NAND/oneNAND is configured
		*/
		if ((ret & 0xC00) == 0x800) {
			/* NAND found */
		} else {
			ret = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG7);
			if ((ret & 0x3F) == (ONENAND_MAP >> 24))
				/* ONENAND found */
				onenandcs = cs;
		}
		cs++;
	}
	if ( (onenandcs > GPMC_CS_NUM)) {
		printk(KERN_INFO "OneNAND: Unable to find configuration "
				" in GPMC\n ");
		return;
	}

	if (onenandcs < GPMC_CS_NUM) {
		igep2_onenand_data.cs = onenandcs;
		if (platform_device_register(&igep2_onenand_device) < 0)
			printk(KERN_ERR "Unable to register OneNAND device\n");
	}
}

#else

void __init igep2_flash_init(void) {}

#endif
