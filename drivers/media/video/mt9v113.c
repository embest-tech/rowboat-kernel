/*
 * drivers/media/video/mt9v113.c
 *
 * Based on TI TVP5146/47 decoder driver
 *
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/io.h>

#include <media/v4l2-int-device.h>
#include <media/mt9v113.h>

#include "mt9v113_regs.h"

/* Module Name */
#define MT9V113_MODULE_NAME		"mt9v113"

/* Private macros for TVP */
#define I2C_RETRY_COUNT                 (5)

/* Debug functions */
static int debug = 1;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

/*
 * enum mt9v113_std - enum for supported standards
 */
enum mt9v113_std {
	MT9V113_STD_VGA = 0,
	MT9V113_STD_QVGA,
	MT9V113_STD_INVALID
};

/*
 * enum mt9v113_state - enum for different decoder states
 */
enum mt9v113_state {
	STATE_NOT_DETECTED,
	STATE_DETECTED
};

/*
 * struct mt9v113_std_info - Structure to store standard informations
 * @width: Line width in pixels
 * @height:Number of active lines
 * @video_std: Value to write in REG_VIDEO_STD register
 * @standard: v4l2 standard structure information
 */
struct mt9v113_std_info {
	unsigned long width;
	unsigned long height;
	u8 video_std;
	struct v4l2_standard standard;
};

/*
 * struct mt9v113_decoded - decoder object
 * @v4l2_int_device: Slave handle
 * @pdata: Board specific
 * @client: I2C client data
 * @id: Entry from I2C table
 * @ver: Chip version
 * @state: decoder state - detected or not-detected
 * @pix: Current pixel format
 * @num_fmts: Number of formats
 * @fmt_list: Format list
 * @current_std: Current standard
 * @num_stds: Number of standards
 * @std_list: Standards list
 */
struct mt9v113_decoder {
	struct v4l2_int_device *v4l2_int_device;
	const struct mt9v113_platform_data *pdata;
	struct i2c_client *client;

	struct i2c_device_id *id;

	int ver;
	enum mt9v113_state state;

	struct v4l2_pix_format pix;
	int num_fmts;
	const struct v4l2_fmtdesc *fmt_list;

	enum mt9v113_std current_std;
	int num_stds;
	struct mt9v113_std_info *std_list;
};

/* MT9V113 register set for VGA mode */
static struct mt9v113_reg mt9v113_vga_reg[] = {
	{TOK_WRITE, 0x098C, 0x2739},
	{TOK_WRITE, 0x0990, 0x0000},
	{TOK_WRITE, 0x098C, 0x273B},
	{TOK_WRITE, 0x0990, 0x027F},
	{TOK_WRITE, 0x098C, 0x273D},
	{TOK_WRITE, 0x0990, 0x0000},
	{TOK_WRITE, 0x098C, 0x273F},
	{TOK_WRITE, 0x0990, 0x01DF},
	{TOK_WRITE, 0x098C, 0x2703},
	{TOK_WRITE, 0x0990, 0x0280},
	{TOK_WRITE, 0x098C, 0x2705},
	{TOK_WRITE, 0x0990, 0x01E0},
	{TOK_WRITE, 0x098C, 0x2715},
	{TOK_WRITE, 0x0990, 0x0001},
	{TOK_WRITE, 0x098C, 0x2717},
	{TOK_WRITE, 0x0990, 0x0026},
	{TOK_WRITE, 0x098C, 0x2719},
	{TOK_WRITE, 0x0990, 0x001A},
	{TOK_WRITE, 0x098C, 0x271B},
	{TOK_WRITE, 0x0990, 0x006B},
	{TOK_WRITE, 0x098C, 0x271D},
	{TOK_WRITE, 0x0990, 0x006B},
	{TOK_WRITE, 0x098C, 0x271F},
	{TOK_WRITE, 0x0990, 0x0202},
	{TOK_WRITE, 0x098C, 0x2721},
	{TOK_WRITE, 0x0990, 0x034A},

	{TOK_WRITE, 0x098C, 0xA103},
	{TOK_WRITE, 0x0990, 0x0005},
	{TOK_DELAY, 0, 100},
	{TOK_TERM, 0, 0},
};

/* MT9V113 default register values */
static struct mt9v113_reg mt9v113_reg_list[] = {
	{TOK_WRITE, 0x0018, 0x4028},
	{TOK_DELAY, 0, 100},
	{TOK_WRITE, 0x001A, 0x0011},
	{TOK_WRITE, 0x001A, 0x0010},
	{TOK_WRITE, 0x0018, 0x4028},
	{TOK_DELAY, 0, 100},
	{TOK_WRITE, 0x098C, 0x02F0},
	{TOK_WRITE, 0x0990, 0x0000},
	{TOK_WRITE, 0x098C, 0x02F2},
	{TOK_WRITE, 0x0990, 0x0210},
	{TOK_WRITE, 0x098C, 0x02F4},
	{TOK_WRITE, 0x0990, 0x001A},
	{TOK_WRITE, 0x098C, 0x2145},
	{TOK_WRITE, 0x0990, 0x02F4},
	{TOK_WRITE, 0x098C, 0xA134},
	{TOK_WRITE, 0x0990, 0x0001},
	{TOK_WRITE, 0x31E0, 0x0001},
	{TOK_WRITE, 0x001A, 0x0210},
	{TOK_WRITE, 0x001E, 0x0777},
	{TOK_WRITE, 0x0016, 0x42DF},
	{TOK_WRITE, 0x0014, 0x2145},
	{TOK_WRITE, 0x0010, 0x0234},
	{TOK_WRITE, 0x0012, 0x0000},
	{TOK_WRITE, 0x0014, 0x244B},
	{TOK_WRITE, 0x0014, 0x304B},
	{TOK_DELAY, 0, 100},
	{TOK_WRITE, 0x0014, 0xB04A},
	{TOK_WRITE, 0x098C, 0xAB1F},
	{TOK_WRITE, 0x0990, 0x00C7},
	{TOK_WRITE, 0x098C, 0xAB31},
	{TOK_WRITE, 0x0990, 0x001E},
	{TOK_WRITE, 0x098C, 0x274F},
	{TOK_WRITE, 0x0990, 0x0004},
	{TOK_WRITE, 0x098C, 0x2741},
	{TOK_WRITE, 0x0990, 0x0004},
	{TOK_WRITE, 0x098C, 0xAB20},
	{TOK_WRITE, 0x0990, 0x0054},
	{TOK_WRITE, 0x098C, 0xAB21},
	{TOK_WRITE, 0x0990, 0x0046},
	{TOK_WRITE, 0x098C, 0xAB22},
	{TOK_WRITE, 0x0990, 0x0002},
	{TOK_WRITE, 0x098C, 0xAB24},
	{TOK_WRITE, 0x0990, 0x0005},
	{TOK_WRITE, 0x098C, 0x2B28},
	{TOK_WRITE, 0x0990, 0x170C},
	{TOK_WRITE, 0x098C, 0x2B2A},
	{TOK_WRITE, 0x0990, 0x3E80},
	{TOK_WRITE, 0x3210, 0x09A8},
	{TOK_WRITE, 0x098C, 0x2306},
	{TOK_WRITE, 0x0990, 0x0315},
	{TOK_WRITE, 0x098C, 0x2308},
	{TOK_WRITE, 0x0990, 0xFDDC},
	{TOK_WRITE, 0x098C, 0x230A},
	{TOK_WRITE, 0x0990, 0x003A},
	{TOK_WRITE, 0x098C, 0x230C},
	{TOK_WRITE, 0x0990, 0xFF58},
	{TOK_WRITE, 0x098C, 0x230E},
	{TOK_WRITE, 0x0990, 0x02B7},
	{TOK_WRITE, 0x098C, 0x2310},
	{TOK_WRITE, 0x0990, 0xFF31},
	{TOK_WRITE, 0x098C, 0x2312},
	{TOK_WRITE, 0x0990, 0xFF4C},
	{TOK_WRITE, 0x098C, 0x2314},
	{TOK_WRITE, 0x0990, 0xFE4C},
	{TOK_WRITE, 0x098C, 0x2316},
	{TOK_WRITE, 0x0990, 0x039E},
	{TOK_WRITE, 0x098C, 0x2318},
	{TOK_WRITE, 0x0990, 0x001C},
	{TOK_WRITE, 0x098C, 0x231A},
	{TOK_WRITE, 0x0990, 0x0039},
	{TOK_WRITE, 0x098C, 0x231C},
	{TOK_WRITE, 0x0990, 0x007F},
	{TOK_WRITE, 0x098C, 0x231E},
	{TOK_WRITE, 0x0990, 0xFF77},
	{TOK_WRITE, 0x098C, 0x2320},
	{TOK_WRITE, 0x0990, 0x000A},
	{TOK_WRITE, 0x098C, 0x2322},
	{TOK_WRITE, 0x0990, 0x0020},
	{TOK_WRITE, 0x098C, 0x2324},
	{TOK_WRITE, 0x0990, 0x001B},
	{TOK_WRITE, 0x098C, 0x2326},
	{TOK_WRITE, 0x0990, 0xFFC6},
	{TOK_WRITE, 0x098C, 0x2328},
	{TOK_WRITE, 0x0990, 0x0086},
	{TOK_WRITE, 0x098C, 0x232A},
	{TOK_WRITE, 0x0990, 0x00B5},
	{TOK_WRITE, 0x098C, 0x232C},
	{TOK_WRITE, 0x0990, 0xFEC3},
	{TOK_WRITE, 0x098C, 0x232E},
	{TOK_WRITE, 0x0990, 0x0001},
	{TOK_WRITE, 0x098C, 0x2330},
	{TOK_WRITE, 0x0990, 0xFFEF},
	{TOK_WRITE, 0x098C, 0xA348},
	{TOK_WRITE, 0x0990, 0x0008},
	{TOK_WRITE, 0x098C, 0xA349},
	{TOK_WRITE, 0x0990, 0x0002},
	{TOK_WRITE, 0x098C, 0xA34A},
	{TOK_WRITE, 0x0990, 0x0090},
	{TOK_WRITE, 0x098C, 0xA34B},
	{TOK_WRITE, 0x0990, 0x00FF},
	{TOK_WRITE, 0x098C, 0xA34C},
	{TOK_WRITE, 0x0990, 0x0075},
	{TOK_WRITE, 0x098C, 0xA34D},
	{TOK_WRITE, 0x0990, 0x00EF},
	{TOK_WRITE, 0x098C, 0xA351},
	{TOK_WRITE, 0x0990, 0x0000},
	{TOK_WRITE, 0x098C, 0xA352},
	{TOK_WRITE, 0x0990, 0x007F},
	{TOK_WRITE, 0x098C, 0xA354},
	{TOK_WRITE, 0x0990, 0x0043},
	{TOK_WRITE, 0x098C, 0xA355},
	{TOK_WRITE, 0x0990, 0x0001},
	{TOK_WRITE, 0x098C, 0xA35D},
	{TOK_WRITE, 0x0990, 0x0078},
	{TOK_WRITE, 0x098C, 0xA35E},
	{TOK_WRITE, 0x0990, 0x0086},
	{TOK_WRITE, 0x098C, 0xA35F},
	{TOK_WRITE, 0x0990, 0x007E},
	{TOK_WRITE, 0x098C, 0xA360},
	{TOK_WRITE, 0x0990, 0x0082},
	{TOK_WRITE, 0x098C, 0x2361},
	{TOK_WRITE, 0x0990, 0x0040},
	{TOK_WRITE, 0x098C, 0xA363},
	{TOK_WRITE, 0x0990, 0x00D2},
	{TOK_WRITE, 0x098C, 0xA364},
	{TOK_WRITE, 0x0990, 0x00F6},
	{TOK_WRITE, 0x098C, 0xA302},
	{TOK_WRITE, 0x0990, 0x0000},
	{TOK_WRITE, 0x098C, 0xA303},
	{TOK_WRITE, 0x0990, 0x00EF},
	{TOK_WRITE, 0x098C, 0xAB20},
	{TOK_WRITE, 0x0990, 0x0024},
	{TOK_WRITE, 0x098C, 0xA103},
	{TOK_WRITE, 0x0990, 0x0006},
	{TOK_DELAY, 0, 100},
	{TOK_WRITE, 0x098C, 0xA103},
	{TOK_WRITE, 0x0990, 0x0005},
	{TOK_DELAY, 0, 100},
	{TOK_WRITE, 0x098C, 0x222D},
	{TOK_WRITE, 0x0990, 0x0081},
	{TOK_WRITE, 0x098C, 0xA408},
	{TOK_WRITE, 0x0990, 0x001F},
	{TOK_WRITE, 0x098C, 0xA409},
	{TOK_WRITE, 0x0990, 0x0021},
	{TOK_WRITE, 0x098C, 0xA40A},
	{TOK_WRITE, 0x0990, 0x0025},
	{TOK_WRITE, 0x098C, 0xA40B},
	{TOK_WRITE, 0x0990, 0x0027},
	{TOK_WRITE, 0x098C, 0x2411},
	{TOK_WRITE, 0x0990, 0x0081},
	{TOK_WRITE, 0x098C, 0x2413},
	{TOK_WRITE, 0x0990, 0x009A},
	{TOK_WRITE, 0x098C, 0x2415},
	{TOK_WRITE, 0x0990, 0x0081},
	{TOK_WRITE, 0x098C, 0x2417},
	{TOK_WRITE, 0x0990, 0x009A},
	{TOK_WRITE, 0x098C, 0xA404},
	{TOK_WRITE, 0x0990, 0x0010},
	{TOK_WRITE, 0x098C, 0xA40D},
	{TOK_WRITE, 0x0990, 0x0002},
	{TOK_WRITE, 0x098C, 0xA40E},
	{TOK_WRITE, 0x0990, 0x0003},
	{TOK_WRITE, 0x098C, 0xA410},
	{TOK_WRITE, 0x0990, 0x000A},

	{TOK_WRITE, 0x098C, 0xA20C},
	{TOK_WRITE, 0x0990, 0x0003},
	{TOK_WRITE, 0x098C, 0xA20B},
	{TOK_WRITE, 0x0990, 0x0000},
	{TOK_WRITE, 0x098C, 0xA215},
	{TOK_WRITE, 0x0990, 0x0004},

	{TOK_WRITE, 0x098C, 0xA103},
	{TOK_WRITE, 0x0990, 0x0006},
	{TOK_DELAY, 0, 100},
	/* test pattern all white*/
	/* {TOK_WRITE, 0x098C, 0xA766},
	{TOK_WRITE, 0x0990, 0x0001},
	*/
	{TOK_WRITE, 0x098C, 0xA103},
	{TOK_WRITE, 0x0990, 0x0005},
	{TOK_DELAY, 0, 100},
	{TOK_TERM, 0, 0},
};

/* List of image formats supported by mt9v113
 * Currently we are using 8 bit mode only, but can be
 * extended to 10/20 bit mode.
 */
static const struct v4l2_fmtdesc mt9v113_fmt_list[] = {
	{
	 .index = 0,
	 .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	 .flags = 0,
	 .description = "8-bit UYVY 4:2:2 Format",
	 .pixelformat = V4L2_PIX_FMT_UYVY,
	},
};

/*
 * Supported standards -
 *
 * Currently supports two standards only, need to add support for rest of the
 * modes, like SECAM, etc...
 */
static struct mt9v113_std_info mt9v113_std_list[] = {
	/* Standard: STD_NTSC_MJ */
	[MT9V113_STD_VGA] = {
	 .width = VGA_NUM_ACTIVE_PIXELS,
	 .height = VGA_NUM_ACTIVE_LINES,
	 .video_std = MT9V113_IMAGE_STD_VGA,
	 .standard = {
		      .index = 0,
		      .id = MT9V113_IMAGE_STD_VGA,
		      .name = "VGA",
		      .frameperiod = {1001, 30000},
		      .framelines = 480
		     },
	/* Standard: STD_PAL_BDGHIN */
	},
	[MT9V113_STD_QVGA] = {
	 .width = QVGA_NUM_ACTIVE_PIXELS,
	 .height = QVGA_NUM_ACTIVE_LINES,
	 .video_std = MT9V113_IMAGE_STD_QVGA,
	 .standard = {
		      .index = 1,
		      .id = MT9V113_IMAGE_STD_QVGA,
		      .name = "QVGA",
		      .frameperiod = {1001, 30000},
		      .framelines = 320
		     },
	},
	/* Standard: need to add for additional standard */
};
/*
 * Control structure for Auto Gain
 *     This is temporary data, will get replaced once
 *     v4l2_ctrl_query_fill supports it.
 */
static const struct v4l2_queryctrl mt9v113_autogain_ctrl = {
	.id = V4L2_CID_AUTOGAIN,
	.name = "Gain, Automatic",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.minimum = 0,
	.maximum = 1,
	.step = 1,
	.default_value = 1,
};

const struct v4l2_fract mt9v113_frameintervals[] = {
	{  .numerator = 1, .denominator = 10 }
};

static int mt9v113_read_reg(struct i2c_client *client, unsigned short reg)
{
	int err = 0;
	struct i2c_msg msg[1];
	unsigned char data[2];
	unsigned short val = 0;

	if (!client->adapter) {
		err = -ENODEV;
		return err;
	} else {
		/* TODO: addr should be set up where else client->addr */
		msg->addr = MT9V113_I2C_ADDR;
		msg->flags = 0;
		msg->len = I2C_TWO_BYTE_TRANSFER;
		msg->buf = data;
		data[0] = (reg & I2C_TXRX_DATA_MASK_UPPER) >>
			    I2C_TXRX_DATA_SHIFT;
		data[1] = (reg & I2C_TXRX_DATA_MASK);
		err = i2c_transfer(client->adapter, msg, 1);
		if (err >= 0) {
			msg->flags = I2C_M_RD;
			/* 2 byte read */
			msg->len = I2C_TWO_BYTE_TRANSFER;
			err = i2c_transfer(client->adapter, msg, 1);
			if (err >= 0) {
				val = ((data[0] & I2C_TXRX_DATA_MASK)
					<< I2C_TXRX_DATA_SHIFT)
				    | (data[1] & I2C_TXRX_DATA_MASK);
			}
		}
	}
	return (int)(0x0000ffff & val);
}


static int mt9v113_write_reg(struct i2c_client *client,
					unsigned short reg, unsigned short val)
{
	int err = 0;
	int trycnt = 0;

	struct i2c_msg msg[1];
	unsigned char data[4];
	err = -1;

	v4l_dbg(1, debug, client,
		 "mt9v113_write_reg reg=0x%x, val=0x%x\n",
		 reg, val);

	while ((err < 0) && (trycnt < I2C_RETRY_COUNT)) {
		trycnt++;
		if (!client->adapter) {
			err = -ENODEV;
		} else {
			/* TODO:addr should be set up where else client->addr */
			msg->addr = MT9V113_I2C_ADDR;
			msg->flags = 0;
			msg->len = I2C_FOUR_BYTE_TRANSFER;
			msg->buf = data;
			data[0] = (reg & I2C_TXRX_DATA_MASK_UPPER) >>
			    I2C_TXRX_DATA_SHIFT;
			data[1] = (reg & I2C_TXRX_DATA_MASK);
			data[2] = (val & I2C_TXRX_DATA_MASK_UPPER) >>
			    I2C_TXRX_DATA_SHIFT;
			data[3] = (val & I2C_TXRX_DATA_MASK);
			err = i2c_transfer(client->adapter, msg, 1);
		}
	}
	if (err < 0)
		printk(KERN_INFO "\n I2C write failed");

	return err;
}

/*
 * mt9v113_write_regs : Initializes a list of registers
 *		if token is TOK_TERM, then entire write operation terminates
 *		if token is TOK_DELAY, then a delay of 'val' msec is introduced
 *		if token is TOK_SKIP, then the register write is skipped
 *		if token is TOK_WRITE, then the register write is performed
 *
 * reglist - list of registers to be written
 * Returns zero if successful, or non-zero otherwise.
 */
static int mt9v113_write_regs(struct i2c_client *client,
			      const struct mt9v113_reg reglist[])
{
	int err;
	const struct mt9v113_reg *next = reglist;

	for (; next->token != TOK_TERM; next++) {
		if (next->token == TOK_DELAY) {
			msleep(next->val);
			continue;
		}

		if (next->token == TOK_SKIP)
			continue;

		err = mt9v113_write_reg(client, next->reg, next->val);
		if (err < 0) {
			v4l_err(client, "Write failed. Err[%d]\n", err);
			return err;
		}
	}
	return 0;
}

/*
 * mt9v113_get_current_std:
 * Returns the current standard
 */
static enum mt9v113_std mt9v113_get_current_std(struct mt9v113_decoder
						*decoder)
{
	return MT9V113_STD_VGA;
}

/*
 * Configure the mt9v113 with the current register settings
 * Returns zero if successful, or non-zero otherwise.
 */
static int mt9v113_configure(struct mt9v113_decoder *decoder)
{
	int err;

	/* common register initialization */
	err =
	    mt9v113_write_regs(decoder->client, mt9v113_reg_list);
	if (err)
		return err;

	return 0;
}

/*
 * Configure the MT9V113 to VGA mode
 * Returns zero if successful, or non-zero otherwise.
 */
static int mt9v113_vga_mode(struct mt9v113_decoder *decoder)
{
	int err;

	err =
	    mt9v113_write_regs(decoder->client, mt9v113_vga_reg);
	if (err)
		return err;

	return 0;
}


/*
 * ioctl_enum_framesizes - V4L2 sensor if handler for vidioc_int_enum_framesizes
 * @s: pointer to standard V4L2 device structure
 * @frms: pointer to standard V4L2 framesizes enumeration structure
 *
 * Returns possible framesizes depending on choosen pixel format
 */
static int ioctl_enum_framesizes(struct v4l2_int_device *s,
					struct v4l2_frmsizeenum *frms)
{
	struct mt9v113_decoder *decoder = s->priv;
	int ifmt;

	for (ifmt = 0; ifmt < decoder->num_fmts; ifmt++) {
		if (frms->pixel_format == decoder->fmt_list[ifmt].pixelformat)
			break;
	}
	/* Is requested pixelformat not found on sensor? */
	if (ifmt == decoder->num_fmts)
		return -EINVAL;

	/* Do we already reached all discrete framesizes? */
	if (frms->index >= decoder->num_stds)
		return -EINVAL;

	frms->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	frms->discrete.width = decoder->std_list[frms->index].width;
	frms->discrete.height = decoder->std_list[frms->index].height;

	return 0;

}

static int ioctl_enum_frameintervals(struct v4l2_int_device *s,
					struct v4l2_frmivalenum *frmi)
{
	struct mt9v113_decoder *decoder = s->priv;
	int ifmt;

	if (frmi->index >= 1)
		return -EINVAL;

	for (ifmt = 0; ifmt < decoder->num_fmts; ifmt++) {
		if (frmi->pixel_format == decoder->fmt_list[ifmt].pixelformat)
			break;
	}
	/* Is requested pixelformat not found on sensor? */
	if (ifmt == decoder->num_fmts)
		return -EINVAL;

	if (frmi->index >= ARRAY_SIZE(mt9v113_frameintervals))
		return -EINVAL;

	frmi->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	frmi->discrete.numerator =
		mt9v113_frameintervals[frmi->index].numerator;
	frmi->discrete.denominator =
		mt9v113_frameintervals[frmi->index].denominator;
	return 0;
}

/*
 * Detect if an mt9v113 is present, and if so which revision.
 * A device is considered to be detected if the chip ID (LSB and MSB)
 * registers match the expected values.
 * Any value of the rom version register is accepted.
 * Returns ENODEV error number if no device is detected, or zero
 * if a device is detected.
 */
static int mt9v113_detect(struct mt9v113_decoder *decoder)
{
	unsigned short val = 0;

	val = mt9v113_read_reg(decoder->client, REG_CHIP_ID);

	v4l_dbg(1, debug, decoder->client, "chip id detected 0x%x\n", val);

	if (MT9V113_CHIP_ID != val) {
		/* We didn't read the values we expected, so this must not be
		 * MT9V113.
		 */
		v4l_err(decoder->client,
			"chip id mismatch read 0x%x,
				 expecting 0x%x\n", val, MT9V113_CHIP_ID);
		return -ENODEV;
	}

	decoder->ver = val;
	decoder->state = STATE_DETECTED;

	v4l_info(decoder->client,
			"%s found at 0x%x (%s)\n", decoder->client->name,
			decoder->client->addr << 1,
			decoder->client->adapter->name);

	return 0;
}

/*
 * Following are decoder interface functions implemented by
 * mt9v113 decoder driver.
 */

/*
 * ioctl_querystd - V4L2 decoder interface handler for VIDIOC_QUERYSTD ioctl
 * @s: pointer to standard V4L2 device structure
 * @std_id: standard V4L2 std_id ioctl enum
 *
 * Returns the current standard detected by mt9v113. If no active input is
 * detected, returns -EINVAL
 */
static int ioctl_querystd(struct v4l2_int_device *s, v4l2_std_id *std_id)
{
	struct mt9v113_decoder *decoder = s->priv;
	enum mt9v113_std current_std;

	if (std_id == NULL)
		return -EINVAL;

	/* get the current standard */
	current_std = mt9v113_get_current_std(decoder);
	if (current_std == MT9V113_IMAGE_STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;
	*std_id = decoder->std_list[current_std].standard.id;

	v4l_dbg(1, debug, decoder->client, "Current STD: %s",
			decoder->std_list[current_std].standard.name);
	return 0;
}

/*
 * ioctl_s_std - V4L2 decoder interface handler for VIDIOC_S_STD ioctl
 * @s: pointer to standard V4L2 device structure
 * @std_id: standard V4L2 v4l2_std_id ioctl enum
 *
 * If std_id is supported, sets the requested standard. Otherwise, returns
 * -EINVAL
 */
static int ioctl_s_std(struct v4l2_int_device *s, v4l2_std_id *std_id)
{
	struct mt9v113_decoder *decoder = s->priv;
	int err, i;

	if (std_id == NULL)
		return -EINVAL;

	for (i = 0; i < decoder->num_stds; i++)
		if (*std_id & decoder->std_list[i].standard.id)
			break;

	if ((i == decoder->num_stds) || (i == MT9V113_STD_INVALID))
		return -EINVAL;

	err = mt9v113_write_reg(decoder->client, REG_VIDEO_STD,
				decoder->std_list[i].video_std);
	if (err)
		return err;

	decoder->current_std = i;
	mt9v113_reg_list[REG_VIDEO_STD].val = decoder->std_list[i].video_std;

	v4l_dbg(1, debug, decoder->client, "Standard set to: %s",
			decoder->std_list[i].standard.name);
	return 0;
}

/*
 * ioctl_s_routing - V4L2 decoder interface handler for VIDIOC_S_INPUT ioctl
 * @s: pointer to standard V4L2 device structure
 * @index: number of the input
 *
 * If index is valid, selects the requested input. Otherwise, returns -EINVAL if
 * the input is not supported or there is no active signal present in the
 * selected input.
 */
static int ioctl_s_routing(struct v4l2_int_device *s,
				struct v4l2_routing *route)
{
	return 0;
}

/*
 * ioctl_queryctrl - V4L2 decoder interface handler for VIDIOC_QUERYCTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @qctrl: standard V4L2 v4l2_queryctrl structure
 *
 * If the requested control is supported, returns the control information.
 * Otherwise, returns -EINVAL if the control is not supported.
 */
static int
ioctl_queryctrl(struct v4l2_int_device *s, struct v4l2_queryctrl *qctrl)
{
	struct mt9v113_decoder *decoder = s->priv;
	int err = -EINVAL;

	if (qctrl == NULL)
		return err;

	switch (qctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		/* Brightness supported is same as standard one (0-255),
		 * so make use of standard API provided.
		 */
		err = v4l2_ctrl_query_fill(qctrl, 0, 255, 1, 128);
		break;
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
		/* Saturation and Contrast supported is -
		 *	Contrast: 0 - 255 (Default - 128)
		 *	Saturation: 0 - 255 (Default - 128)
		 */
		err = v4l2_ctrl_query_fill(qctrl, 0, 255, 1, 128);
		break;
	case V4L2_CID_HUE:
		/* Hue Supported is -
		 *	Hue - -180 - +180 (Default - 0, Step - +180)
		 */
		err = v4l2_ctrl_query_fill(qctrl, -180, 180, 180, 0);
		break;
	case V4L2_CID_AUTOGAIN:
		/* Autogain is either 0 or 1*/
		memcpy(qctrl, &mt9v113_autogain_ctrl,
				sizeof(struct v4l2_queryctrl));
		err = 0;
		break;
	default:
		v4l_err(decoder->client,
			"invalid control id %d\n", qctrl->id);
		return err;
	}

	v4l_dbg(1, debug, decoder->client,
			"Query Control: %s : Min - %d, Max - %d, Def - %d",
			qctrl->name,
			qctrl->minimum,
			qctrl->maximum,
			qctrl->default_value);

	return err;
}

/*
 * ioctl_g_ctrl - V4L2 decoder interface handler for VIDIOC_G_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @ctrl: pointer to v4l2_control structure
 *
 * If the requested control is supported, returns the control's current
 * value from the decoder. Otherwise, returns -EINVAL if the control is not
 * supported.
 */
static int
ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *ctrl)
{
	struct mt9v113_decoder *decoder = s->priv;

	if (ctrl == NULL)
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = mt9v113_reg_list[REG_BRIGHTNESS].val;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = mt9v113_reg_list[REG_CONTRAST].val;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = mt9v113_reg_list[REG_SATURATION].val;
		break;
	case V4L2_CID_HUE:
		ctrl->value = mt9v113_reg_list[REG_HUE].val;
		if (ctrl->value == 0x7F)
			ctrl->value = 180;
		else if (ctrl->value == 0x80)
			ctrl->value = -180;
		else
			ctrl->value = 0;

		break;
	case V4L2_CID_AUTOGAIN:
		ctrl->value = mt9v113_reg_list[REG_AFE_GAIN_CTRL].val;
		if ((ctrl->value & 0x3) == 3)
			ctrl->value = 1;
		else
			ctrl->value = 0;

		break;
	default:
		v4l_err(decoder->client,
			"invalid control id %d\n", ctrl->id);
		return -EINVAL;
	}

	v4l_dbg(1, debug, decoder->client,
			"Get Control: ID - %d - %d",
			ctrl->id, ctrl->value);
	return 0;
}

/*
 * ioctl_s_ctrl - V4L2 decoder interface handler for VIDIOC_S_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @ctrl: pointer to v4l2_control structure
 *
 * If the requested control is supported, sets the control's current
 * value in HW. Otherwise, returns -EINVAL if the control is not supported.
 */
static int
ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *ctrl)
{
	struct mt9v113_decoder *decoder = s->priv;
	int err = -EINVAL, value;

	if (ctrl == NULL)
		return err;

	value = (__s32) ctrl->value;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (ctrl->value < 0 || ctrl->value > 255) {
			v4l_err(decoder->client,
					"invalid brightness setting %d\n",
					ctrl->value);
			return -ERANGE;
		}
		err = mt9v113_write_reg(decoder->client, REG_BRIGHTNESS,
				value);
		if (err)
			return err;
		mt9v113_reg_list[REG_BRIGHTNESS].val = value;
		break;
	case V4L2_CID_CONTRAST:
		if (ctrl->value < 0 || ctrl->value > 255) {
			v4l_err(decoder->client,
					"invalid contrast setting %d\n",
					ctrl->value);
			return -ERANGE;
		}
		err = mt9v113_write_reg(decoder->client, REG_CONTRAST,
				value);
		if (err)
			return err;
		mt9v113_reg_list[REG_CONTRAST].val = value;
		break;
	case V4L2_CID_SATURATION:
		if (ctrl->value < 0 || ctrl->value > 255) {
			v4l_err(decoder->client,
					"invalid saturation setting %d\n",
					ctrl->value);
			return -ERANGE;
		}
		err = mt9v113_write_reg(decoder->client, REG_SATURATION,
				value);
		if (err)
			return err;
		mt9v113_reg_list[REG_SATURATION].val = value;
		break;
	case V4L2_CID_HUE:
		if (value == 180)
			value = 0x7F;
		else if (value == -180)
			value = 0x80;
		else if (value == 0)
			value = 0;
		else {
			v4l_err(decoder->client,
					"invalid hue setting %d\n",
					ctrl->value);
			return -ERANGE;
		}
		err = mt9v113_write_reg(decoder->client, REG_HUE,
				value);
		if (err)
			return err;
		mt9v113_reg_list[REG_HUE].val = value;
		break;
	case V4L2_CID_AUTOGAIN:
		if (value == 1)
			value = 0x0F;
		else if (value == 0)
			value = 0x0C;
		else {
			v4l_err(decoder->client,
					"invalid auto gain setting %d\n",
					ctrl->value);
			return -ERANGE;
		}
		err = mt9v113_write_reg(decoder->client, REG_AFE_GAIN_CTRL,
				value);
		if (err)
			return err;
		mt9v113_reg_list[REG_AFE_GAIN_CTRL].val = value;
		break;
	default:
		v4l_err(decoder->client,
			"invalid control id %d\n", ctrl->id);
		return err;
	}

	v4l_dbg(1, debug, decoder->client,
			"Set Control: ID - %d - %d",
			ctrl->id, ctrl->value);

	return err;
}

/*
 * ioctl_enum_fmt_cap - Implement the CAPTURE buffer VIDIOC_ENUM_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @fmt: standard V4L2 VIDIOC_ENUM_FMT ioctl structure
 *
 * Implement the VIDIOC_ENUM_FMT ioctl to enumerate supported formats
 */
static int
ioctl_enum_fmt_cap(struct v4l2_int_device *s, struct v4l2_fmtdesc *fmt)
{
	struct mt9v113_decoder *decoder = s->priv;
	int index;

	if (fmt == NULL)
		return -EINVAL;

	index = fmt->index;
	if ((index >= decoder->num_fmts) || (index < 0))
		return -EINVAL;	/* Index out of bound */

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	memcpy(fmt, &decoder->fmt_list[index],
		sizeof(struct v4l2_fmtdesc));

	v4l_dbg(1, debug, decoder->client,
			"Current FMT: index - %d (%s)",
			decoder->fmt_list[index].index,
			decoder->fmt_list[index].description);
	return 0;
}

/*
 * ioctl_try_fmt_cap - Implement the CAPTURE buffer VIDIOC_TRY_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 VIDIOC_TRY_FMT ioctl structure
 *
 * Implement the VIDIOC_TRY_FMT ioctl for the CAPTURE buffer type. This
 * ioctl is used to negotiate the image capture size and pixel format
 * without actually making it take effect.
 */
static int
ioctl_try_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct mt9v113_decoder *decoder = s->priv;
	int ifmt;
	struct v4l2_pix_format *pix;
	enum mt9v113_std current_std;

	if (f == NULL)
		return -EINVAL;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	pix = &f->fmt.pix;

	/* Calculate height and width based on current standard */
	current_std = mt9v113_get_current_std(decoder);
	if (current_std == MT9V113_STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;
	pix->width = decoder->std_list[current_std].width;
	pix->height = decoder->std_list[current_std].height;

	for (ifmt = 0; ifmt < decoder->num_fmts; ifmt++) {
		if (pix->pixelformat ==
			decoder->fmt_list[ifmt].pixelformat)
			break;
	}
	if (ifmt == decoder->num_fmts)
		ifmt = 0;	/* None of the format matched, select default */
	pix->pixelformat = decoder->fmt_list[ifmt].pixelformat;

	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = pix->width * 2;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pix->priv = 0;

	v4l_dbg(1, debug, decoder->client,
			"Try FMT: pixelformat - %s, bytesperline - %d"
			"Width - %d, Height - %d",
			decoder->fmt_list[ifmt].description, pix->bytesperline,
			pix->width, pix->height);
	return 0;
}

/*
 * ioctl_s_fmt_cap - V4L2 decoder interface handler for VIDIOC_S_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 VIDIOC_S_FMT ioctl structure
 *
 * If the requested format is supported, configures the HW to use that
 * format, returns error code if format not supported or HW can't be
 * correctly configured.
 */
static int
ioctl_s_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct mt9v113_decoder *decoder = s->priv;
	struct v4l2_pix_format *pix;
	int rval;

	if (f == NULL)
		return -EINVAL;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	pix = &f->fmt.pix;
	rval = ioctl_try_fmt_cap(s, f);
	if (rval)
		return rval;

	decoder->pix = *pix;

	return rval;
}

/*
 * ioctl_g_fmt_cap - V4L2 decoder interface handler for ioctl_g_fmt_cap
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 v4l2_format structure
 *
 * Returns the decoder's current pixel format in the v4l2_format
 * parameter.
 */
static int
ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct mt9v113_decoder *decoder = s->priv;

	if (f == NULL)
		return -EINVAL;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	f->fmt.pix = decoder->pix;

	v4l_dbg(1, debug, decoder->client,
			"Current FMT: bytesperline - %d"
			"Width - %d, Height - %d",
			decoder->pix.bytesperline,
			decoder->pix.width, decoder->pix.height);
	return 0;
}

/*
 * ioctl_g_parm - V4L2 decoder interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the decoder's video CAPTURE parameters.
 */
static int
ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct mt9v113_decoder *decoder = s->priv;
	struct v4l2_captureparm *cparm;
	enum mt9v113_std current_std;

	if (a == NULL)
		return -EINVAL;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	memset(a, 0, sizeof(*a));
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	/* get the current standard */
	current_std = mt9v113_get_current_std(decoder);
	if (current_std == MT9V113_STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;

	cparm = &a->parm.capture;
	cparm->capability = V4L2_CAP_TIMEPERFRAME;
	cparm->timeperframe =
		decoder->std_list[current_std].standard.frameperiod;

	return 0;
}

/*
 * ioctl_s_parm - V4L2 decoder interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the decoder to use the input parameters, if possible. If
 * not possible, returns the appropriate error code.
 */
static int
ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct mt9v113_decoder *decoder = s->priv;
	struct v4l2_fract *timeperframe;
	enum mt9v113_std current_std;

	if (a == NULL)
		return -EINVAL;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	timeperframe = &a->parm.capture.timeperframe;

	/* get the current standard */
	current_std = mt9v113_get_current_std(decoder);
	if (current_std == MT9V113_STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;

	*timeperframe =
	    decoder->std_list[current_std].standard.frameperiod;

	return 0;
}

/*
 * ioctl_g_ifparm - V4L2 decoder interface handler for vidioc_int_g_ifparm_num
 * @s: pointer to standard V4L2 device structure
 * @p: pointer to standard V4L2 vidioc_int_g_ifparm_num ioctl structure
 *
 * Gets slave interface parameters.
 * Calculates the required xclk value to support the requested
 * clock parameters in p. This value is returned in the p
 * parameter.
 */
static int ioctl_g_ifparm(struct v4l2_int_device *s, struct v4l2_ifparm *p)
{
	struct mt9v113_decoder *decoder = s->priv;
	int rval;

	if (p == NULL)
		return -EINVAL;

	if (NULL == decoder->pdata->ifparm)
		return -EINVAL;

	rval = decoder->pdata->ifparm(p);
	if (rval) {
		v4l_err(decoder->client, "g_ifparm.Err[%d]\n", rval);
		return rval;
	}

	p->u.bt656.clock_curr = 27000000; /* TODO:read clock rate from sensor */

	return 0;
}

/*
 * ioctl_g_priv - V4L2 decoder interface handler for vidioc_int_g_priv_num
 * @s: pointer to standard V4L2 device structure
 * @p: void pointer to hold decoder's private data address
 *
 * Returns device's (decoder's) private data area address in p parameter
 */
static int ioctl_g_priv(struct v4l2_int_device *s, void *p)
{
	struct mt9v113_decoder *decoder = s->priv;

	if (NULL == decoder->pdata->priv_data_set)
		return -EINVAL;

	return decoder->pdata->priv_data_set(p);
}

/*
 * ioctl_s_power - V4L2 decoder interface handler for vidioc_int_s_power_num
 * @s: pointer to standard V4L2 device structure
 * @on: power state to which device is to be set
 *
 * Sets devices power state to requrested state, if possible.
 */
static int ioctl_s_power(struct v4l2_int_device *s, enum v4l2_power on)
{
	struct mt9v113_decoder *decoder = s->priv;
	int err = 0;

	switch (on) {
	case V4L2_POWER_OFF:
		/* Power Down Sequence */
		/* TODO: FIXME: implement proper OFF and Standby code here */
#if 0
		err = mt9v113_write_reg(decoder->client, REG_OPERATION_MODE,
				0x01);
#endif
		/* Disable mux for mt9v113 data path */
		if (decoder->pdata->power_set)
			err |= decoder->pdata->power_set(s, on);
		decoder->state = STATE_NOT_DETECTED;
		break;

	case V4L2_POWER_STANDBY:
		if (decoder->pdata->power_set)
			err = decoder->pdata->power_set(s, on);
		break;

	case V4L2_POWER_ON:
		/* Enable mux for mt9v113 data path */
		if (decoder->state == STATE_NOT_DETECTED) {

			if (decoder->pdata->power_set)
				err = decoder->pdata->power_set(s, on);

			/* Detect the sensor is not already detected */
			err |= mt9v113_detect(decoder);
			if (err) {
				v4l_err(decoder->client,
						"Unable to detect decoder\n");
				return err;
			}
		}
		/* Only VGA mode for now */
		err |= mt9v113_configure(decoder);
		err |= mt9v113_vga_mode(decoder);
		break;

	default:
		err = -ENODEV;
		break;
	}

	return err;
}

/*
 * ioctl_init - V4L2 decoder interface handler for VIDIOC_INT_INIT
 * @s: pointer to standard V4L2 device structure
 *
 * Initialize the decoder device (calls mt9v113_configure())
 */
static int ioctl_init(struct v4l2_int_device *s)
{
	struct mt9v113_decoder *decoder = s->priv;
	int err = 0;

	err |= mt9v113_configure(decoder);
	err |= mt9v113_vga_mode(decoder);

	return err;
}

/*
 * ioctl_dev_exit - V4L2 decoder interface handler for vidioc_int_dev_exit_num
 * @s: pointer to standard V4L2 device structure
 *
 * Delinitialise the dev. at slave detach. The complement of ioctl_dev_init.
 */
static int ioctl_dev_exit(struct v4l2_int_device *s)
{
	return 0;
}

/*
 * ioctl_dev_init - V4L2 decoder interface handler for vidioc_int_dev_init_num
 * @s: pointer to standard V4L2 device structure
 *
 * Initialise the device when slave attaches to the master. Returns 0 if
 * mt9v113 device could be found, otherwise returns appropriate error.
 */
static int ioctl_dev_init(struct v4l2_int_device *s)
{
	struct mt9v113_decoder *decoder = s->priv;
	int err;

	err = mt9v113_detect(decoder);
	if (err < 0) {
		v4l_err(decoder->client,
			"Unable to detect decoder\n");
		return err;
	}

	v4l_info(decoder->client,
		 "chip version 0x%.2x detected\n", decoder->ver);

	err |= mt9v113_configure(decoder);
	err |= mt9v113_vga_mode(decoder);

	return 0;
}

static struct v4l2_int_ioctl_desc mt9v113_ioctl_desc[] = {
	{vidioc_int_dev_init_num, (v4l2_int_ioctl_func *) ioctl_dev_init},
	{vidioc_int_dev_exit_num, (v4l2_int_ioctl_func *) ioctl_dev_exit},
	{vidioc_int_s_power_num, (v4l2_int_ioctl_func *) ioctl_s_power},
	{vidioc_int_g_priv_num, (v4l2_int_ioctl_func *) ioctl_g_priv},
	{vidioc_int_g_ifparm_num, (v4l2_int_ioctl_func *) ioctl_g_ifparm},
	{vidioc_int_init_num, (v4l2_int_ioctl_func *) ioctl_init},
	{vidioc_int_enum_fmt_cap_num,
	 (v4l2_int_ioctl_func *) ioctl_enum_fmt_cap},
	{vidioc_int_try_fmt_cap_num,
	 (v4l2_int_ioctl_func *) ioctl_try_fmt_cap},
	{vidioc_int_g_fmt_cap_num,
	 (v4l2_int_ioctl_func *) ioctl_g_fmt_cap},
	{vidioc_int_s_fmt_cap_num,
	 (v4l2_int_ioctl_func *) ioctl_s_fmt_cap},
	{vidioc_int_g_parm_num, (v4l2_int_ioctl_func *) ioctl_g_parm},
	{vidioc_int_s_parm_num, (v4l2_int_ioctl_func *) ioctl_s_parm},
	{vidioc_int_queryctrl_num,
	 (v4l2_int_ioctl_func *) ioctl_queryctrl},
	{vidioc_int_g_ctrl_num, (v4l2_int_ioctl_func *) ioctl_g_ctrl},
	{vidioc_int_s_ctrl_num, (v4l2_int_ioctl_func *) ioctl_s_ctrl},
	{vidioc_int_querystd_num, (v4l2_int_ioctl_func *) ioctl_querystd},
	{vidioc_int_s_std_num, (v4l2_int_ioctl_func *) ioctl_s_std},
	{vidioc_int_s_video_routing_num,
		(v4l2_int_ioctl_func *) ioctl_s_routing},
	{vidioc_int_enum_framesizes_num,
		(v4l2_int_ioctl_func *)ioctl_enum_framesizes},
	{vidioc_int_enum_frameintervals_num,
		(v4l2_int_ioctl_func *)ioctl_enum_frameintervals},
};

static struct v4l2_int_slave mt9v113_slave = {
	.ioctls = mt9v113_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(mt9v113_ioctl_desc),
};

static struct mt9v113_decoder mt9v113_dev = {
	.state = STATE_NOT_DETECTED,

	.fmt_list = mt9v113_fmt_list,
	.num_fmts = ARRAY_SIZE(mt9v113_fmt_list),

	.pix = {		/* Default to 8-bit YUV 422 */
		.width = VGA_NUM_ACTIVE_PIXELS,
		.height = VGA_NUM_ACTIVE_LINES,
		.pixelformat = V4L2_PIX_FMT_UYVY,
		.field = V4L2_FIELD_NONE,
		.bytesperline = VGA_NUM_ACTIVE_PIXELS * 2,
		.sizeimage =
		VGA_NUM_ACTIVE_PIXELS * 2 * VGA_NUM_ACTIVE_LINES,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
		},

	.current_std = MT9V113_STD_VGA,
	.std_list = mt9v113_std_list,
	.num_stds = ARRAY_SIZE(mt9v113_std_list),

};

static struct v4l2_int_device mt9v113_int_device = {
	.module = THIS_MODULE,
	.name = MT9V113_MODULE_NAME,
	.priv = &mt9v113_dev,
	.type = v4l2_int_type_slave,
	.u = {
	      .slave = &mt9v113_slave,
	      },
};

/*
 * mt9v113_probe - decoder driver i2c probe handler
 * @client: i2c driver client device structure
 *
 * Register decoder as an i2c client device and V4L2
 * device.
 */
static int
mt9v113_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct mt9v113_decoder *decoder = &mt9v113_dev;
	int err;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	decoder->pdata = client->dev.platform_data;
	if (!decoder->pdata) {
		v4l_err(client, "No platform data!!\n");
		return -ENODEV;
	}
	/*
	 * Save the id data, required for power up sequence
	 */
	decoder->id = (struct i2c_device_id *)id;
	/* Attach to Master */
	strcpy(mt9v113_int_device.u.slave->attach_to, decoder->pdata->master);
	decoder->v4l2_int_device = &mt9v113_int_device;
	decoder->client = client;
	i2c_set_clientdata(client, decoder);

	/* Register with V4L2 layer as slave device */
	err = v4l2_int_device_register(decoder->v4l2_int_device);
	if (err) {
		i2c_set_clientdata(client, NULL);
		v4l_err(client,
			"Unable to register to v4l2. Err[%d]\n", err);

	} else
		v4l_info(client, "Registered to v4l2 master %s!!\n",
				decoder->pdata->master);

	return 0;
}

/*
 * mt9v113_remove - decoder driver i2c remove handler
 * @client: i2c driver client device structure
 *
 * Unregister decoder as an i2c client device and V4L2
 * device. Complement of mt9v113_probe().
 */
static int __exit mt9v113_remove(struct i2c_client *client)
{
	struct mt9v113_decoder *decoder = i2c_get_clientdata(client);

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	v4l2_int_device_unregister(decoder->v4l2_int_device);
	i2c_set_clientdata(client, NULL);

	return 0;
}
/*
 * mt9v113 Init/Power on Sequence
 */
static const struct mt9v113_reg mt9v113m_init_reg_seq[] = {
	{TOK_WRITE, REG_OPERATION_MODE, 0x01},
	{TOK_WRITE, REG_OPERATION_MODE, 0x00},
};
static const struct mt9v113_init_seq mt9v113m_init = {
	.no_regs = ARRAY_SIZE(mt9v113m_init_reg_seq),
	.init_reg_seq = mt9v113m_init_reg_seq,
};
/*
 * I2C Device Table -
 *
 * name - Name of the actual device/chip.
 * driver_data - Driver data
 */
static const struct i2c_device_id mt9v113_id[] = {
	{"mt9v113", (unsigned long)&mt9v113m_init},
	{},
};

MODULE_DEVICE_TABLE(i2c, mt9v113_id);

static struct i2c_driver mt9v113_i2c_driver = {
	.driver = {
		   .name = MT9V113_MODULE_NAME,
		   .owner = THIS_MODULE,
		   },
	.probe = mt9v113_probe,
	.remove = __exit_p(mt9v113_remove),
	.id_table = mt9v113_id,
};

/*
 * mt9v113_init
 *
 * Module init function
 */
static int __init mt9v113_init(void)
{
	return i2c_add_driver(&mt9v113_i2c_driver);
}

/*
 * mt9v113_cleanup
 *
 * Module exit function
 */
static void __exit mt9v113_cleanup(void)
{
	i2c_del_driver(&mt9v113_i2c_driver);
}

module_init(mt9v113_init);
module_exit(mt9v113_cleanup);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("MT9V113 linux decoder driver");
MODULE_LICENSE("GPL");
