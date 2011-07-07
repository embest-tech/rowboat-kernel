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

#include <mach/board-am335xevm.h>

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

/* Low-Cost EVM */
static struct evm_dev_cfg low_cost_evm_dev_cfg[] = {
	{0, 0, 0},
};

/* General Purpose EVM */
static struct evm_dev_cfg gen_purp_evm_dev_cfg[] = {
	{mcasp1_pin_mux, mcasp1_init, (PROFILE_0 | PROFILE_3 | PROFILE_7) },
	{0, 0, 0},
};

/* Industrial Auto Motor Control EVM */
static struct evm_dev_cfg ind_auto_mtrl_evm_dev_cfg[] = {
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
