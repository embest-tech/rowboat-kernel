/*
 * AM33XX Power Management Routines
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
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
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <linux/completion.h>
#include <linux/pm_runtime.h>

#include <mach/board-am335xevm.h>
#include <plat/prcm.h>
#include <plat/mailbox.h>
#include <plat/sram.h>
#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>
#include <plat/emif.h>

#include <asm/suspend.h>
#include <asm/proc-fns.h>
#include <asm/sizes.h>

#include "pm.h"
#include "cm33xx.h"
#include "pm33xx.h"
#include "control.h"
#include "clockdomain.h"
#include "powerdomain.h"

void (*am33xx_do_wfi_sram)(u32 *);

#define DS_MODE		DS0_ID	/* DS0/1_ID */
#define MODULE_DISABLE	0x0
#define MODULE_ENABLE	0x2

#ifdef CONFIG_SUSPEND
void __iomem *ipc_regs;
void __iomem *m3_eoi;
void __iomem *m3_code;
u32 suspend_cfg_param_list[SUSPEND_CFG_PARAMS_END];

bool enable_deep_sleep = true;
static suspend_state_t suspend_state = PM_SUSPEND_ON;

static struct device *mpu_dev;
static struct omap_mbox *m3_mbox;
static struct powerdomain *cefuse_pwrdm, *gfx_pwrdm, *per_pwrdm;
static struct clockdomain *gfx_l3_clkdm, *gfx_l4ls_clkdm;

static int m3_state = M3_STATE_UNKNOWN;
static int m3_version = M3_VERSION_UNKNOWN;

static int am33xx_ipc_cmd(struct a8_wkup_m3_ipc_data *);
static int am33xx_verify_lp_state(int);
static void am33xx_m3_state_machine_reset(void);

static DECLARE_COMPLETION(a8_m3_sync);

static int am33xx_pm_prepare_late(void)
{
	int ret = 0;

	am335x_save_padconf();
	am33xx_setup_pinmux_on_suspend();

	return ret;
}

static void am33xx_pm_finish(void)
{
	am33xx_standby_release(suspend_state);
	am335x_restore_padconf();
}

static int am33xx_do_sram_idle(long unsigned int state)
{
	am33xx_do_wfi_sram(&suspend_cfg_param_list[0]);

	return 0;
}

static int am33xx_pm_suspend(void)
{
	int state, ret = 0;

	struct omap_hwmod *gpmc_oh, *usb_oh, *gpio1_oh, *rtc_oh;

	usb_oh		= omap_hwmod_lookup("usb_otg_hs");
	gpmc_oh		= omap_hwmod_lookup("gpmc");
	gpio1_oh	= omap_hwmod_lookup("gpio1");	/* WKUP domain GPIO */
	rtc_oh		= omap_hwmod_lookup("rtc");

	omap_hwmod_enable(usb_oh);
	omap_hwmod_enable(gpmc_oh);

	/*
	 * Keep USB module enabled during standby
	 * to enable USB remote wakeup
	 * Note: This will result in hard-coding USB state
	 * during standby
	 */
	if (suspend_state != PM_SUSPEND_STANDBY)
		omap_hwmod_idle(usb_oh);

	omap_hwmod_idle(gpmc_oh);

	/*
	 * Keep RTC module enabled during standby
	 * for PG2.x to enable wakeup from RTC.
	 */
	if ((omap_rev() >= AM335X_REV_ES2_0) &&
		(suspend_state == PM_SUSPEND_STANDBY))
		omap_hwmod_enable(rtc_oh);

	/*
	 * Disable the GPIO module. This ensure that
	 * only sWAKEUP interrupts to Cortex-M3 get generated
	 *
	 * XXX: EVM_SK uses a GPIO0 pin for VTP control
	 * in suspend and hence we can't do this for EVM_SK
	 * alone. The side-effect of this is that GPIO wakeup
	 * might have issues. Refer to commit 672639b for the
	 * details
	 */
	/*
	 * Keep GPIO0 module enabled during standby to
	 * support wakeup via GPIO0 keys.
	 */
	if ((suspend_cfg_param_list[EVM_ID] != EVM_SK) &&
			(suspend_state != PM_SUSPEND_STANDBY))
		omap_hwmod_idle(gpio1_oh);
	/*
	 * Update Suspend_State value to be used in sleep33xx.S to keep
	 * GPIO0 module enabled during standby for EVM-SK
	 */
	if (suspend_state == PM_SUSPEND_STANDBY)
		suspend_cfg_param_list[SUSPEND_STATE] = PM_STANDBY;
	else
		suspend_cfg_param_list[SUSPEND_STATE] = PM_DS0;

	/*
	 * Keep Touchscreen module enabled during standby
	 * to enable wakeup from standby.
	 */
	if (suspend_state == PM_SUSPEND_STANDBY)
		writel(0x2, AM33XX_CM_WKUP_ADC_TSC_CLKCTRL);

	if (gfx_l3_clkdm && gfx_l4ls_clkdm) {
		clkdm_sleep(gfx_l3_clkdm);
		clkdm_sleep(gfx_l4ls_clkdm);
	}

	/* Try to put GFX to sleep */
	if (gfx_pwrdm)
		pwrdm_set_next_pwrst(gfx_pwrdm, PWRDM_POWER_OFF);
	else
		pr_err("Could not program GFX to low power state\n");

	omap3_intc_suspend();

	am33xx_standby_setup(suspend_state);

	writel(0x0, AM33XX_CM_MPU_MPU_CLKCTRL);

	ret = cpu_suspend(0, am33xx_do_sram_idle);

	writel(0x2, AM33XX_CM_MPU_MPU_CLKCTRL);

	if (gfx_pwrdm) {
		state = pwrdm_read_pwrst(gfx_pwrdm);
		if (state != PWRDM_POWER_OFF)
			pr_err("GFX domain did not transition to low power state\n");
		else
			pr_info("GFX domain entered low power state\n");
	}

	/* XXX: Why do we need to wakeup the clockdomains? */
	if(gfx_l3_clkdm && gfx_l4ls_clkdm) {
		clkdm_wakeup(gfx_l3_clkdm);
		clkdm_wakeup(gfx_l4ls_clkdm);
	}

	/*
	 * Touchscreen module was enabled during standby
	 * Disable it here.
	 */
	if (suspend_state == PM_SUSPEND_STANDBY)
		writel(0x0, AM33XX_CM_WKUP_ADC_TSC_CLKCTRL);

	/*
	 * Put USB module to idle on resume from standby
	 */
	if (suspend_state == PM_SUSPEND_STANDBY)
		omap_hwmod_idle(usb_oh);

	/*
	 * Put RTC module to idle on resume from standby
	 * for PG2.x.
	 */
	if ((omap_rev() >= AM335X_REV_ES2_0) &&
		(suspend_state == PM_SUSPEND_STANDBY))
		omap_hwmod_idle(rtc_oh);

	ret = am33xx_verify_lp_state(ret);

	/*
	 * Enable the GPIO module. Once the driver is
	 * fully adapted to runtime PM this will go away
	 */
	/*
	 * During standby, GPIO was not disabled. Hence no
	 * need to enable it here.
	 */
	if ((suspend_cfg_param_list[EVM_ID] != EVM_SK) &&
			(suspend_state != PM_SUSPEND_STANDBY))
		omap_hwmod_enable(gpio1_oh);

	return ret;
}

static int am33xx_pm_enter(suspend_state_t unused)
{
	int ret = 0;

	switch (suspend_state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		ret = am33xx_pm_suspend();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int am33xx_pm_begin(suspend_state_t state)
{
	int ret = 0;
	int state_id = 0;

	disable_hlt();

	switch (state) {
	case PM_SUSPEND_STANDBY:
		state_id = 0xb;
		break;
	case PM_SUSPEND_MEM:
		state_id = 0x3;
		break;
	}

	/*
	 * Populate the resume address as part of IPC data
	 * The offset to be added comes from sleep33xx.S
	 * Add 4 bytes to ensure that resume happens from
	 * the word *after* the word which holds the resume offset
	 */
	am33xx_lp_ipc.resume_addr = (DS_RESUME_BASE + am33xx_resume_offset + 4);
	am33xx_lp_ipc.sleep_mode  = state_id;
	am33xx_lp_ipc.ipc_data1	  = DS_IPC_DEFAULT;
	am33xx_lp_ipc.ipc_data2   = DS_IPC_DEFAULT;

	am33xx_ipc_cmd(&am33xx_lp_ipc);

	m3_state = M3_STATE_MSG_FOR_LP;

	omap_mbox_enable_irq(m3_mbox, IRQ_RX);

	ret = omap_mbox_msg_send(m3_mbox, 0xABCDABCD);
	if (ret) {
		pr_err("A8<->CM3 MSG for LP failed\n");
		am33xx_m3_state_machine_reset();
		ret = -1;
	}

	if (!wait_for_completion_timeout(&a8_m3_sync, msecs_to_jiffies(5000))) {
		pr_err("A8<->CM3 sync failure\n");
		am33xx_m3_state_machine_reset();
		ret = -1;
	} else {
		pr_debug("Message sent for entering %s\n",
			(DS_MODE == DS0_ID ? "DS0" : "DS1"));
		omap_mbox_disable_irq(m3_mbox, IRQ_RX);
	}

	suspend_state = state;
	return ret;
}

static void am33xx_m3_state_machine_reset(void)
{
	int ret = 0;

	am33xx_lp_ipc.resume_addr = 0x0;
	am33xx_lp_ipc.sleep_mode  = 0xe;
	am33xx_lp_ipc.ipc_data1	  = DS_IPC_DEFAULT;
	am33xx_lp_ipc.ipc_data2   = DS_IPC_DEFAULT;

	am33xx_ipc_cmd(&am33xx_lp_ipc);

	m3_state = M3_STATE_MSG_FOR_RESET;

	ret = omap_mbox_msg_send(m3_mbox, 0xABCDABCD);
	if (!ret) {
		pr_debug("Message sent for resetting M3 state machine\n");
		if (!wait_for_completion_timeout(&a8_m3_sync, msecs_to_jiffies(5000)))
			pr_err("A8<->CM3 sync failure\n");
	} else {
		pr_err("Could not reset M3 state machine!!!\n");
		m3_state = M3_STATE_UNKNOWN;
	}
}

static void am33xx_pm_end(void)
{
	suspend_state = PM_SUSPEND_ON;

	omap_mbox_enable_irq(m3_mbox, IRQ_RX);

	am33xx_m3_state_machine_reset();

	enable_hlt();

	return;
}

static int am33xx_pm_valid(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		return 1;
	default:
		return 0;
	}
}

static const struct platform_suspend_ops am33xx_pm_ops = {
	.begin		= am33xx_pm_begin,
	.end		= am33xx_pm_end,
	.enter		= am33xx_pm_enter,
	.valid		= am33xx_pm_valid,
	.prepare	= am33xx_pm_prepare_late,
	.finish		= am33xx_pm_finish,
};

int am33xx_ipc_cmd(struct a8_wkup_m3_ipc_data *data)
{
	writel(data->resume_addr, ipc_regs);
	writel(data->sleep_mode, ipc_regs + 0x4);
	writel(data->ipc_data1, ipc_regs + 0x8);
	writel(data->ipc_data2, ipc_regs + 0xc);

	return 0;
}

/* return 0 if no reset M3 needed, 1 otherwise */
static int am33xx_verify_lp_state(int core_suspend_stat)
{
	int status, ret = 0;

	if (core_suspend_stat) {
		pr_err("Kernel core reported suspend failure\n");
		ret = -1;
		goto clear_old_status;
	}

	status = readl(ipc_regs + 0x4);
	status &= 0xffff0000;

	if (status == 0x0) {
		pr_info("Successfully transitioned all domains to low power state\n");
		if (am33xx_lp_ipc.sleep_mode == DS0_ID)
			per_pwrdm->ret_logic_off_counter++;
		goto clear_old_status;
	} else if (status == 0x10000) {
		pr_err("Could not enter low power state\n"
			"Please check for active clocks in PER domain\n");
		ret = -1;
		goto clear_old_status;
	} else {
		pr_err("Something is terribly wrong :(\nStatus = %0x\n",
				status);
		ret = -1;
	}

clear_old_status:
	/* After decoding write back the bad status */
	status = readl(ipc_regs + 0x4);
	status &= 0xffff0000;
	status |= 0x10000;
	writel(status, ipc_regs + 0x4);

	return ret;
}

/*
 * Dummy notifier for the mailbox
 * TODO: Can this be completely removed?
 */
int wkup_m3_mbox_msg(struct notifier_block *self, unsigned long len, void *msg)
{
	return 0;
}

static struct notifier_block wkup_m3_mbox_notifier = {
	.notifier_call = wkup_m3_mbox_msg,
};

static irqreturn_t wkup_m3_txev_handler(int irq, void *unused)
{
	writel(0x1, m3_eoi);

	if (m3_state == M3_STATE_RESET) {
		m3_state = M3_STATE_INITED;
		m3_version = readl(ipc_regs + 0x8);
		m3_version &= 0x0000ffff;
		if (m3_version == M3_VERSION_UNKNOWN) {
			pr_warning("Unable to read CM3 firmware version\n");
		} else {
			pr_info("Cortex M3 Firmware Version = 0x%x\n",
							m3_version);
		}
	} else if (m3_state == M3_STATE_MSG_FOR_RESET) {
		m3_state = M3_STATE_INITED;
		omap_mbox_msg_rx_flush(m3_mbox);
		if (m3_mbox->ops->ack_irq)
			m3_mbox->ops->ack_irq(m3_mbox, IRQ_RX);
		complete(&a8_m3_sync);
	} else if (m3_state == M3_STATE_MSG_FOR_LP) {
		omap_mbox_msg_rx_flush(m3_mbox);
		if (m3_mbox->ops->ack_irq)
			m3_mbox->ops->ack_irq(m3_mbox, IRQ_RX);
		complete(&a8_m3_sync);
	} else if (m3_state == M3_STATE_UNKNOWN) {
		pr_err("IRQ %d with CM3 in unknown state\n", irq);
		omap_mbox_msg_rx_flush(m3_mbox);
		if (m3_mbox->ops->ack_irq)
			m3_mbox->ops->ack_irq(m3_mbox, IRQ_RX);
		return IRQ_NONE;
	}

	writel(0x0, m3_eoi);

	return IRQ_HANDLED;
}

/* Initiliaze WKUP_M3, load the binary blob and let it run */
static int wkup_m3_init(void)
{
	struct clk *m3_clk;
	struct omap_hwmod *wkup_m3_oh;
	const struct firmware *firmware;
	int ret = 0;
	int ipc_reg_r = 0;

	wkup_m3_oh = omap_hwmod_lookup("wkup_m3");

	if (!wkup_m3_oh) {
		pr_err("%s: could not find omap_hwmod\n", __func__);
		ret = -ENODEV;
		goto exit;
	}

	ipc_regs = ioremap(A8_M3_IPC_REGS, 0x4*8);
	if (!ipc_regs) {
		pr_err("Could not ioremap the IPC area\b");
		ret = -ENOMEM;
		goto exit;
	}

	m3_eoi = ioremap(M3_TXEV_EOI, 0x4);
	if (!m3_eoi) {
		pr_err("Could not ioremap the EOI register\n");
		ret = -ENOMEM;
		goto err1;
	}

	/* Reserve the MBOX for sending messages to M3 */
	m3_mbox = omap_mbox_get("wkup_m3", &wkup_m3_mbox_notifier);
	if (IS_ERR(m3_mbox)) {
		pr_err("Could not reserve mailbox for A8->M3 IPC\n");
		ret = -ENODEV;
		goto err2;
	}

	/* Enable access to the M3 code and data area from A8 */
	m3_clk = clk_get(NULL, "wkup_m3_fck");
	if (IS_ERR(m3_clk)) {
		pr_err("%s failed to enable WKUP_M3 clock\n", __func__);
		goto err3;
	}

	if (clk_enable(m3_clk)) {
		pr_err("%s WKUP_M3: clock enable Failed\n", __func__);
		goto err4;
	}

	m3_code = ioremap(M3_UMEM, SZ_16K);
	if (!m3_code) {
		pr_err("%s Could not ioremap M3 code space\n", __func__);
		ret = -ENOMEM;
		goto err5;
	}

	pr_info("Trying to load am335x-pm-firmware.bin (60 secs timeout)\n");

	ret = request_firmware(&firmware, "am335x-pm-firmware.bin", mpu_dev);
	if (ret < 0) {
		dev_err(mpu_dev, "request_firmware failed\n");
		goto err6;
	} else {
		memcpy(m3_code, firmware->data, firmware->size);
		pr_info("Copied the M3 firmware to UMEM\n");
	}

	ret = request_irq(AM33XX_IRQ_M3_M3SP_TXEV, wkup_m3_txev_handler,
			  IRQF_DISABLED, "wkup_m3_txev", NULL);
	if (ret) {
		pr_err("%s request_irq failed for 0x%x\n", __func__,
			AM33XX_IRQ_M3_M3SP_TXEV);
		goto err6;
	}

	m3_state = M3_STATE_RESET;

	/*
	 * Invalidate M3 firmware version before hardreset.
	 * Write invalid version in lower 4 nibbles of parameter
	 * register (ipc_regs + 0x8).
	 */
	ipc_reg_r = readl(ipc_regs + 0x8);
	ipc_reg_r &= 0xffff0000;
	m3_version |= ipc_reg_r;
	writel(m3_version, ipc_regs + 0x8);

	ret = omap_hwmod_deassert_hardreset(wkup_m3_oh, "wkup_m3");
	if (ret) {
		pr_err("Could not deassert the reset for WKUP_M3\n");
		goto err6;
	} else {
		return 0;
	}

err6:
	release_firmware(firmware);
	iounmap(m3_code);
err5:
	clk_disable(m3_clk);
err4:
	clk_put(m3_clk);
err3:
	omap_mbox_put(m3_mbox, &wkup_m3_mbox_notifier);
err2:
	iounmap(m3_eoi);
err1:
	iounmap(ipc_regs);
exit:
	return ret;
}

/*
 * Initiate sleep transition for other clockdomains, if
 * they are not used
 */
static int __init clkdms_setup(struct clockdomain *clkdm, void *unused)
{
	if (clkdm->flags & CLKDM_CAN_FORCE_SLEEP &&
			atomic_read(&clkdm->usecount) == 0)
		clkdm_sleep(clkdm);
	return 0;
}
#endif /* CONFIG_SUSPEND */

/*
 * Push the minimal suspend-resume code to SRAM
 */
void am33xx_push_sram_idle(void)
{
	am33xx_do_wfi_sram = (void *)omap_sram_push
					(am33xx_do_wfi, am33xx_do_wfi_sz);
}

static int __init am33xx_pm_init(void)
{
	int ret;
#ifdef CONFIG_SUSPEND
	void __iomem *base;
	u32 reg;
	u32 evm_id;

#endif
	if (!cpu_is_am33xx())
		return -ENODEV;

	pr_info("Power Management for AM33XX family\n");

#ifdef CONFIG_SUSPEND
/* Read SDRAM_CONFIG register to determine Memory Type */
	base = am33xx_get_ram_base();
	reg = readl(base + EMIF4_0_SDRAM_CONFIG);
	reg = (reg & SDRAM_TYPE_MASK) >> SDRAM_TYPE_SHIFT;
	suspend_cfg_param_list[MEMORY_TYPE] = reg;

/*
 * vtp_ctrl register value for DDR2 and DDR3 as suggested
 * by h/w team
 */
	if (reg == MEM_TYPE_DDR2)
		suspend_cfg_param_list[SUSP_VTP_CTRL_VAL] = SUSP_VTP_CTRL_DDR2;
	else
		suspend_cfg_param_list[SUSP_VTP_CTRL_VAL] = SUSP_VTP_CTRL_DDR3;


	/* Get Board Id */
	evm_id = am335x_evm_get_id();
	if (evm_id != -EINVAL)
		suspend_cfg_param_list[EVM_ID] = evm_id;
	else
		suspend_cfg_param_list[EVM_ID] = 0xff;

	/* CPU Revision */
	reg = omap_rev();
	if (reg >= AM335X_REV_ES2_0)
		suspend_cfg_param_list[CPU_REV] = CPU_REV_2;
	else
		suspend_cfg_param_list[CPU_REV] = CPU_REV_1;

	(void) clkdm_for_each(clkdms_setup, NULL);

	/* CEFUSE domain should be turned off post bootup */
	cefuse_pwrdm = pwrdm_lookup("cefuse_pwrdm");
	if (cefuse_pwrdm == NULL)
		pr_err("Failed to get cefuse_pwrdm\n");
	else
		pwrdm_set_next_pwrst(cefuse_pwrdm, PWRDM_POWER_OFF);

	gfx_pwrdm = pwrdm_lookup("gfx_pwrdm");
	if (gfx_pwrdm == NULL)
		pr_err("Failed to get gfx_pwrdm\n");

	per_pwrdm = pwrdm_lookup("per_pwrdm");
	if (per_pwrdm == NULL)
		pr_err("Failed to get per_pwrdm\n");

	gfx_l3_clkdm = clkdm_lookup("gfx_l3_clkdm");
	if (gfx_l3_clkdm == NULL)
		pr_err("Failed to get gfx_l3_clkdm\n");

	gfx_l4ls_clkdm = clkdm_lookup("gfx_l4ls_gfx_clkdm");
	if (gfx_l4ls_clkdm == NULL)
		pr_err("Failed to get gfx_l4ls_gfx_clkdm\n");

	mpu_dev = omap_device_get_by_hwmod_name("mpu");

	if (!mpu_dev) {
		pr_warning("%s: unable to get the mpu device\n", __func__);
		return -EINVAL;
	}

	ret = wkup_m3_init();

	if (ret) {
		pr_err("Could not initialise WKUP_M3. "
			"Power management will be compromised\n");
		enable_deep_sleep = false;
	}

	if (enable_deep_sleep)
		suspend_set_ops(&am33xx_pm_ops);
#endif /* CONFIG_SUSPEND */

	return ret;
}
late_initcall(am33xx_pm_init);
