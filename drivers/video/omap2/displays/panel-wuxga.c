/*
 * wuxga panel support
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/delay.h>

#include <plat/display.h>

static struct omap_video_timings wuxga_panel_timings = {
	.x_res        = 1920,
	.y_res        = 1200,
	.pixel_clock   = 154000,
	.hfp          = 48,
	.hsw          = 32,
	.hbp          = 80,
	.vfp          = 3,
	.vsw          = 6,
	.vbp          = 26,
};

static int wuxga_panel_power_on(struct omap_dss_device *dssdev)
{
	int r;

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	if (dssdev->platform_enable) {
		r = dssdev->platform_enable(dssdev);
		if (r)
			goto err1;
	}

	return 0;
err1:
	omapdss_dpi_display_disable(dssdev);
err0:
	return r;
}

static void wuxga_panel_power_off(struct omap_dss_device *dssdev)
{
	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);

	omapdss_dpi_display_disable(dssdev);
}

static int wuxga_panel_probe(struct omap_dss_device *dssdev)
{
	dssdev->panel.config = OMAP_DSS_LCD_TFT;
	dssdev->panel.timings = wuxga_panel_timings;

	return 0;
}

static void wuxga_panel_remove(struct omap_dss_device *dssdev)
{
}

static int wuxga_panel_enable(struct omap_dss_device *dssdev)
{
	int r = 0;

	r = wuxga_panel_power_on(dssdev);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void wuxga_panel_disable(struct omap_dss_device *dssdev)
{
	wuxga_panel_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static int wuxga_panel_suspend(struct omap_dss_device *dssdev)
{
	wuxga_panel_power_off(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;
	return 0;
}

static int wuxga_panel_resume(struct omap_dss_device *dssdev)
{
	int r = 0;

	r = wuxga_panel_power_on(dssdev);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void wuxga_panel_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	dpi_set_timings(dssdev, timings);
}

static void wuxga_panel_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	*timings = dssdev->panel.timings;
}

static int wuxga_panel_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	return dpi_check_timings(dssdev, timings);
}

static struct omap_dss_driver wuxga_driver = {
	.probe        = wuxga_panel_probe,
	.remove               = wuxga_panel_remove,

	.enable               = wuxga_panel_enable,
	.disable       = wuxga_panel_disable,
	.suspend       = wuxga_panel_suspend,
	.resume               = wuxga_panel_resume,

	.set_timings   = wuxga_panel_set_timings,
	.get_timings   = wuxga_panel_get_timings,
	.check_timings = wuxga_panel_check_timings,

	.driver         = {
		.name   = "wuxga_panel",
		.owner  = THIS_MODULE,
	},
};

static int __init wuxga_panel_drv_init(void)
{
	return omap_dss_register_driver(&wuxga_driver);
}

static void __exit wuxga_panel_drv_exit(void)
{
	omap_dss_unregister_driver(&wuxga_driver);
}

module_init(wuxga_panel_drv_init);
module_exit(wuxga_panel_drv_exit);
MODULE_LICENSE("GPL");
