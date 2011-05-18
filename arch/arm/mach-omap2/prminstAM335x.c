/*
 * AM335x PRM instance functions
 *
 * Copyright (C) {YEAR} Texas Instruments Incorporated - http://www.ti.com/
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
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include <plat/common.h>

#include "prmAM335x.h"
#include "prminstAM335x.h"
#include "prm-regbits-AM335x.h"
#include "prcmAM335x.h"

/* Read a register in a PRM instance */
u32 am335x_prminst_read_inst_reg(s16 inst, u16 idx)
{
	return __raw_readl(prm_base + inst + idx);
}

/* Write into a register in a PRM instance */
void am335x_prminst_write_inst_reg(u32 val, s16 inst, u16 idx)
{
	__raw_writel(val, prm_base + inst + idx);
}

/* Read-modify-write a register in PRM. Caller must lock */
u32 am335x_prminst_rmw_inst_reg_bits(u32 mask, u32 bits, s16 inst,
				   s16 idx)
{
	u32 v;

	v = am335x_prminst_read_inst_reg(inst, idx);
	v &= ~mask;
	v |= bits;
	am335x_prminst_write_inst_reg(v, inst, idx);

	return v;
}
