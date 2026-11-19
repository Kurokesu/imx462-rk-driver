// SPDX-License-Identifier: GPL-2.0
/*
 * Sony IMX462 CMOS Image Sensor Driver
 *
 * Copyright (C) 2019 FRAMOS GmbH.
 *
 * Copyright (C) 2019 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/rational.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <asm/unaligned.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define IMX462_REG_SIZE_SHIFT				16
#define IMX462_REG_ADDR_MASK				0xffff
#define IMX462_REG_8BIT(n)				((1U << IMX462_REG_SIZE_SHIFT) | (n))
#define IMX462_REG_16BIT(n)				((2U << IMX462_REG_SIZE_SHIFT) | (n))
#define IMX462_REG_24BIT(n)				((3U << IMX462_REG_SIZE_SHIFT) | (n))

#define IMX462_STANDBY					IMX462_REG_8BIT(0x3000)
#define IMX462_REGHOLD					IMX462_REG_8BIT(0x3001)
#define IMX462_XMSTA					IMX462_REG_8BIT(0x3002)
#define IMX462_ADBIT					IMX462_REG_8BIT(0x3005)
#define IMX462_ADBIT_10BIT				(0 << 0)
#define IMX462_ADBIT_12BIT				(1 << 0)
#define IMX462_CTRL_07					IMX462_REG_8BIT(0x3007)
#define IMX462_VREVERSE					BIT(0)
#define IMX462_HREVERSE					BIT(1)
#define IMX462_WINMODE_1080P				(0 << 4)
#define IMX462_WINMODE_720P				(1 << 4)
#define IMX462_WINMODE_CROP				(4 << 4)
#define IMX462_FR_FDG_SEL				IMX462_REG_8BIT(0x3009)
#define IMX462_BLKLEVEL					IMX462_REG_16BIT(0x300a)
#define IMX462_GAIN					IMX462_REG_8BIT(0x3014)
#define IMX462_VMAX					IMX462_REG_24BIT(0x3018)
#define IMX462_VMAX_MAX					0x3ffff
#define IMX462_HMAX					IMX462_REG_16BIT(0x301c)
#define IMX462_HMAX_MAX					0xffff
#define IMX462_SHS1					IMX462_REG_24BIT(0x3020)
#define IMX462_WINWV_OB					IMX462_REG_8BIT(0x303a)
#define IMX462_WINPV					IMX462_REG_16BIT(0x303c)
#define IMX462_WINWV					IMX462_REG_16BIT(0x303e)
#define IMX462_WINPH					IMX462_REG_16BIT(0x3040)
#define IMX462_WINWH					IMX462_REG_16BIT(0x3042)
#define IMX462_OUT_CTRL					IMX462_REG_8BIT(0x3046)
#define IMX462_ODBIT_10BIT				(0 << 0)
#define IMX462_ODBIT_12BIT				(1 << 0)
#define IMX462_OPORTSEL_PARALLEL			(0x0 << 4)
#define IMX462_OPORTSEL_LVDS_2CH			(0xd << 4)
#define IMX462_OPORTSEL_LVDS_4CH			(0xe << 4)
#define IMX462_OPORTSEL_LVDS_8CH			(0xf << 4)
#define IMX462_XSOUTSEL					IMX462_REG_8BIT(0x304b)
#define IMX462_XSOUTSEL_XVSOUTSEL_HIGH			(0 << 0)
#define IMX462_XSOUTSEL_XVSOUTSEL_VSYNC			(2 << 0)
#define IMX462_XSOUTSEL_XHSOUTSEL_HIGH			(0 << 2)
#define IMX462_XSOUTSEL_XHSOUTSEL_HSYNC			(2 << 2)
#define IMX462_INCKSEL1					IMX462_REG_8BIT(0x305c)
#define IMX462_INCKSEL2					IMX462_REG_8BIT(0x305d)
#define IMX462_INCKSEL3					IMX462_REG_8BIT(0x305e)
#define IMX462_INCKSEL4					IMX462_REG_8BIT(0x305f)
#define IMX462_PGCTRL					IMX462_REG_8BIT(0x308c)
#define IMX462_ADBIT1					IMX462_REG_8BIT(0x3129)
#define IMX462_ADBIT1_10BIT				0x1d
#define IMX462_ADBIT1_12BIT				0x00
#define IMX462_INCKSEL5					IMX462_REG_8BIT(0x315e)
#define IMX462_INCKSEL6					IMX462_REG_8BIT(0x3164)
#define IMX462_ADBIT2					IMX462_REG_8BIT(0x317c)
#define IMX462_ADBIT2_10BIT				0x12
#define IMX462_ADBIT2_12BIT				0x00
#define IMX462_CHIP_ID					IMX462_REG_16BIT(0x319a)
#define IMX462_ADBIT3					IMX462_REG_8BIT(0x31ec)
#define IMX462_ADBIT3_10BIT				0x37
#define IMX462_ADBIT3_12BIT				0x0e
#define IMX462_REPETITION				IMX462_REG_8BIT(0x3405)
#define IMX462_PHY_LANE_NUM				IMX462_REG_8BIT(0x3407)
#define IMX462_OPB_SIZE_V				IMX462_REG_8BIT(0x3414)
#define IMX462_Y_OUT_SIZE				IMX462_REG_16BIT(0x3418)
#define IMX462_CSI_DT_FMT				IMX462_REG_16BIT(0x3441)
#define IMX462_CSI_DT_FMT_RAW10				0x0a0a
#define IMX462_CSI_DT_FMT_RAW12				0x0c0c
#define IMX462_CSI_LANE_MODE				IMX462_REG_8BIT(0x3443)
#define IMX462_EXTCK_FREQ				IMX462_REG_16BIT(0x3444)
#define IMX462_TCLKPOST					IMX462_REG_16BIT(0x3446)
#define IMX462_THSZERO					IMX462_REG_16BIT(0x3448)
#define IMX462_THSPREPARE				IMX462_REG_16BIT(0x344a)
#define IMX462_TCLKTRAIL				IMX462_REG_16BIT(0x344c)
#define IMX462_THSTRAIL					IMX462_REG_16BIT(0x344e)
#define IMX462_TCLKZERO					IMX462_REG_16BIT(0x3450)
#define IMX462_TCLKPREPARE				IMX462_REG_16BIT(0x3452)
#define IMX462_TLPX					IMX462_REG_16BIT(0x3454)
#define IMX462_X_OUT_SIZE				IMX462_REG_16BIT(0x3472)
#define IMX462_INCKSEL7					IMX462_REG_8BIT(0x3480)

#define IMX462_PGCTRL_REGEN				BIT(0)
#define IMX462_PGCTRL_THRU				BIT(1)
#define IMX462_PGCTRL_MODE(n)				((n) << 4)

/* Number of lines by which exposure must be less than VMAX */
#define IMX462_EXPOSURE_OFFSET				2

#define IMX462_PIXEL_RATE				148500000

/*
 * The IMX290 pixel array is organized as follows:
 *
 *     +------------------------------------+
 *     |           Optical Black            |     }  Vertical effective optical black (10)
 * +---+------------------------------------+---+
 * |   |                                    |   | }  Effective top margin (8)
 * |   |   +----------------------------+   |   | \
 * |   |   |                            |   |   |  |
 * |   |   |                            |   |   |  |
 * |   |   |                            |   |   |  |
 * |   |   |    Recording Pixel Area    |   |   |  | Recommended height (1080)
 * |   |   |                            |   |   |  |
 * |   |   |                            |   |   |  |
 * |   |   |                            |   |   |  |
 * |   |   +----------------------------+   |   | /
 * |   |                                    |   | }  Effective bottom margin (9)
 * +---+------------------------------------+---+
 *  <-> <-> <--------------------------> <-> <->
 *                                            \----  Ignored right margin (4)
 *                                        \--------  Effective right margin (9)
 *                       \-------------------------  Recommended width (1920)
 *       \-----------------------------------------  Effective left margin (8)
 *   \---------------------------------------------  Ignored left margin (4)
 *
 * The optical black lines are output over CSI-2 with a separate data type.
 *
 * The pixel array is meant to have 1920x1080 usable pixels after image
 * processing in an ISP. It has 8 (9) extra active pixels usable for color
 * processing in the ISP on the top and left (bottom and right) sides of the
 * image. In addition, 4 additional pixels are present on the left and right
 * sides of the image, documented as "ignored area".
 *
 * As far as is understood, all pixels of the pixel array (ignored area, color
 * processing margins and recording area) can be output by the sensor.
 */

#define IMX462_PIXEL_ARRAY_WIDTH			1945
#define IMX462_PIXEL_ARRAY_HEIGHT			1097
#define IMX462_PIXEL_ARRAY_MARGIN_LEFT			12
#define IMX462_PIXEL_ARRAY_MARGIN_RIGHT			13
#define IMX462_PIXEL_ARRAY_MARGIN_TOP			8
#define IMX462_PIXEL_ARRAY_MARGIN_BOTTOM		9
#define IMX462_PIXEL_ARRAY_RECORDING_WIDTH		1920
#define IMX462_PIXEL_ARRAY_RECORDING_HEIGHT		1080

/* Equivalent value for 16bpp */
#define IMX462_BLACK_LEVEL_DEFAULT			3840

#define IMX462_NUM_SUPPLIES				3

enum imx462_colour_variant {
	IMX462_VARIANT_COLOUR,
	IMX462_VARIANT_MONO,
	IMX462_VARIANT_MAX
};

enum imx462_model {
	IMX462_MODEL_IMX462LQR,
	IMX462_MODEL_IMX462LLR,
};

struct imx462_model_info {
	enum imx462_colour_variant colour_variant;
	const struct imx462_regval *init_regs;
	size_t init_regs_num;
	unsigned int max_analog_gain;
	const char *name;
};

enum imx462_clk_freq {
	IMX462_CLK_37_125,
	IMX462_CLK_74_25,
	IMX462_NUM_CLK
};

struct imx462_regval {
	u32 reg;
	u32 val;
};

/*
 * Clock configuration for registers INCKSEL1 to INCKSEL6.
 */
struct imx462_clk_cfg {
	u8 incksel1;
	u8 incksel2;
	u8 incksel3;
	u8 incksel4;
	u8 incksel5;
	u8 incksel6;
};

struct imx462_mode {
	u32 width;
	u32 height;
	u32 hmax_min;
	u32 vmax_min;
	u8 link_freq_index;
	u8 ctrl_07;

	const struct imx462_regval *data;
	u32 data_size;

	const struct imx462_clk_cfg *clk_cfg;
};

struct imx462_csi_cfg {
	u16 repetition;
	u16 tclkpost;
	u16 thszero;
	u16 thsprepare;
	u16 tclktrail;
	u16 thstrail;
	u16 tclkzero;
	u16 tclkprepare;
	u16 tlpx;
};

struct imx462 {
	struct device *dev;
	struct clk *xclk;
	struct regmap *regmap;
	enum imx462_clk_freq xclk_idx;
	u8 nlanes;
	const struct imx462_model_info *model;

	struct v4l2_subdev sd;
	struct media_pad pad;

	const struct imx462_mode *current_mode;

	struct regulator_bulk_data supplies[IMX462_NUM_SUPPLIES];
	struct gpio_desc *rst_gpio;

	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct {
		struct v4l2_ctrl *hflip;
		struct v4l2_ctrl *vflip;
	};
};

static inline struct imx462 *to_imx462(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx462, sd);
}

/* -----------------------------------------------------------------------------
 * Modes and formats
 */

static const struct imx462_regval imx462_global_init_settings[] = {
	{ IMX462_WINWV_OB, 12 },
	{ IMX462_WINPH, 0 },
	{ IMX462_WINPV, 0 },
	{ IMX462_WINWH, 1948 },
	{ IMX462_WINWV, 1097 },
	{ IMX462_XSOUTSEL, IMX462_XSOUTSEL_XVSOUTSEL_VSYNC |
			   IMX462_XSOUTSEL_XHSOUTSEL_HSYNC },
	{ IMX462_REG_8BIT(0x3012), 0x64 },
	{ IMX462_REG_8BIT(0x3013), 0x00 },
};

static const struct imx462_regval imx462_global_init_settings_462[] = {
	{ IMX462_REG_8BIT(0x300f), 0x00 },
	{ IMX462_REG_8BIT(0x3010), 0x21 },
	{ IMX462_REG_8BIT(0x3011), 0x02 },
	{ IMX462_REG_8BIT(0x3016), 0x09 },
	{ IMX462_REG_8BIT(0x3070), 0x02 },
	{ IMX462_REG_8BIT(0x3071), 0x11 },
	{ IMX462_REG_8BIT(0x309b), 0x10 },
	{ IMX462_REG_8BIT(0x309c), 0x22 },
	{ IMX462_REG_8BIT(0x30a2), 0x02 },
	{ IMX462_REG_8BIT(0x30a6), 0x20 },
	{ IMX462_REG_8BIT(0x30a8), 0x20 },
	{ IMX462_REG_8BIT(0x30aa), 0x20 },
	{ IMX462_REG_8BIT(0x30ac), 0x20 },
	{ IMX462_REG_8BIT(0x30b0), 0x43 },
	{ IMX462_REG_8BIT(0x3119), 0x9e },
	{ IMX462_REG_8BIT(0x311c), 0x1e },
	{ IMX462_REG_8BIT(0x311e), 0x08 },
	{ IMX462_REG_8BIT(0x3128), 0x05 },
	{ IMX462_REG_8BIT(0x313d), 0x83 },
	{ IMX462_REG_8BIT(0x3150), 0x03 },
	{ IMX462_REG_8BIT(0x317e), 0x00 },
	{ IMX462_REG_8BIT(0x32b8), 0x50 },
	{ IMX462_REG_8BIT(0x32b9), 0x10 },
	{ IMX462_REG_8BIT(0x32ba), 0x00 },
	{ IMX462_REG_8BIT(0x32bb), 0x04 },
	{ IMX462_REG_8BIT(0x32c8), 0x50 },
	{ IMX462_REG_8BIT(0x32c9), 0x10 },
	{ IMX462_REG_8BIT(0x32ca), 0x00 },
	{ IMX462_REG_8BIT(0x32cb), 0x04 },
	{ IMX462_REG_8BIT(0x332c), 0xd3 },
	{ IMX462_REG_8BIT(0x332d), 0x10 },
	{ IMX462_REG_8BIT(0x332e), 0x0d },
	{ IMX462_REG_8BIT(0x3358), 0x06 },
	{ IMX462_REG_8BIT(0x3359), 0xe1 },
	{ IMX462_REG_8BIT(0x335a), 0x11 },
	{ IMX462_REG_8BIT(0x3360), 0x1e },
	{ IMX462_REG_8BIT(0x3361), 0x61 },
	{ IMX462_REG_8BIT(0x3362), 0x10 },
	{ IMX462_REG_8BIT(0x33b0), 0x50 },
	{ IMX462_REG_8BIT(0x33b2), 0x1a },
	{ IMX462_REG_8BIT(0x33b3), 0x04 },
};

#define IMX462_NUM_CLK_REGS	2
static const struct imx462_regval xclk_regs[][IMX462_NUM_CLK_REGS] = {
	[IMX462_CLK_37_125] = {
		{ IMX462_EXTCK_FREQ, (37125 * 256) / 1000 },
		{ IMX462_INCKSEL7, 0x49 },
	},
	[IMX462_CLK_74_25] = {
		{ IMX462_EXTCK_FREQ, (74250 * 256) / 1000 },
		{ IMX462_INCKSEL7, 0x92 },
	},
};

static const struct imx462_regval imx462_1080p_settings[] = {
	/* mode settings */
	{ IMX462_WINWV_OB, 12 },
	{ IMX462_OPB_SIZE_V, 10 },
	{ IMX462_X_OUT_SIZE, 1920 },
	{ IMX462_Y_OUT_SIZE, 1080 },
};

static const struct imx462_regval imx462_720p_settings[] = {
	/* mode settings */
	{ IMX462_WINWV_OB, 6 },
	{ IMX462_OPB_SIZE_V, 4 },
	{ IMX462_X_OUT_SIZE, 1280 },
	{ IMX462_Y_OUT_SIZE, 720 },
};

static const struct imx462_regval imx462_10bit_settings[] = {
	{ IMX462_ADBIT, IMX462_ADBIT_10BIT },
	{ IMX462_OUT_CTRL, IMX462_ODBIT_10BIT },
	{ IMX462_ADBIT1, IMX462_ADBIT1_10BIT },
	{ IMX462_ADBIT2, IMX462_ADBIT2_10BIT },
	{ IMX462_ADBIT3, IMX462_ADBIT3_10BIT },
	{ IMX462_CSI_DT_FMT, IMX462_CSI_DT_FMT_RAW10 },
};

static const struct imx462_regval imx462_12bit_settings[] = {
	{ IMX462_ADBIT, IMX462_ADBIT_12BIT },
	{ IMX462_OUT_CTRL, IMX462_ODBIT_12BIT },
	{ IMX462_ADBIT1, IMX462_ADBIT1_12BIT },
	{ IMX462_ADBIT2, IMX462_ADBIT2_12BIT },
	{ IMX462_ADBIT3, IMX462_ADBIT3_12BIT },
	{ IMX462_CSI_DT_FMT, IMX462_CSI_DT_FMT_RAW12 },
};

static const struct imx462_csi_cfg imx462_csi_222_75mhz = {
	/* 222.75MHz or 445.5Mbit/s per lane */
	.repetition = 0x10,
	.tclkpost = 87,
	.thszero = 55,
	.thsprepare = 31,
	.tclktrail = 31,
	.thstrail = 31,
	.tclkzero = 119,
	.tclkprepare = 31,
	.tlpx = 23,
};

static const struct imx462_csi_cfg imx462_csi_445_5mhz = {
	/* 445.5MHz or 891Mbit/s per lane */
	.repetition = 0x00,
	.tclkpost = 119,
	.thszero = 103,
	.thsprepare = 71,
	.tclktrail = 55,
	.thstrail = 63,
	.tclkzero = 255,
	.tclkprepare = 63,
	.tlpx = 55,
};

static const struct imx462_csi_cfg imx462_csi_148_5mhz = {
	/* 148.5MHz or 297Mbit/s per lane */
	.repetition = 0x10,
	.tclkpost = 79,
	.thszero = 47,
	.thsprepare = 23,
	.tclktrail = 23,
	.thstrail = 23,
	.tclkzero = 87,
	.tclkprepare = 23,
	.tlpx = 23,
};

static const struct imx462_csi_cfg imx462_csi_297mhz = {
	/* 297MHz or 594Mbit/s per lane */
	.repetition = 0x00,
	.tclkpost = 103,
	.thszero = 87,
	.thsprepare = 47,
	.tclktrail = 39,
	.thstrail = 47,
	.tclkzero = 191,
	.tclkprepare = 47,
	.tlpx = 39,
};

/* supported link frequencies */
#define FREQ_INDEX_1080P	0
#define FREQ_INDEX_720P		1
static const s64 imx462_link_freq_2lanes[] = {
	[FREQ_INDEX_1080P] = 445500000,
	[FREQ_INDEX_720P] = 297000000,
};

static const s64 imx462_link_freq_4lanes[] = {
	[FREQ_INDEX_1080P] = 222750000,
	[FREQ_INDEX_720P] = 148500000,
};

/*
 * In this function and in the similar ones below We rely on imx462_probe()
 * to ensure that nlanes is either 2 or 4.
 */
static inline const s64 *imx462_link_freqs_ptr(const struct imx462 *imx462)
{
	if (imx462->nlanes == 2)
		return imx462_link_freq_2lanes;
	else
		return imx462_link_freq_4lanes;
}

static inline int imx462_link_freqs_num(const struct imx462 *imx462)
{
	if (imx462->nlanes == 2)
		return ARRAY_SIZE(imx462_link_freq_2lanes);
	else
		return ARRAY_SIZE(imx462_link_freq_4lanes);
}

static const struct imx462_clk_cfg imx462_1080p_clock_config[] = {
	[IMX462_CLK_37_125] = {
		/* 37.125MHz clock config */
		.incksel1 = 0x18,
		.incksel2 = 0x03,
		.incksel3 = 0x20,
		.incksel4 = 0x01,
		.incksel5 = 0x1a,
		.incksel6 = 0x1a,
	},
	[IMX462_CLK_74_25] = {
		/* 74.25MHz clock config */
		.incksel1 = 0x0c,
		.incksel2 = 0x03,
		.incksel3 = 0x10,
		.incksel4 = 0x01,
		.incksel5 = 0x1b,
		.incksel6 = 0x1b,
	},
};

static const struct imx462_clk_cfg imx462_720p_clock_config[] = {
	[IMX462_CLK_37_125] = {
		/* 37.125MHz clock config */
		.incksel1 = 0x20,
		.incksel2 = 0x00,
		.incksel3 = 0x20,
		.incksel4 = 0x01,
		.incksel5 = 0x1a,
		.incksel6 = 0x1a,
	},
	[IMX462_CLK_74_25] = {
		/* 74.25MHz clock config */
		.incksel1 = 0x10,
		.incksel2 = 0x00,
		.incksel3 = 0x10,
		.incksel4 = 0x01,
		.incksel5 = 0x1b,
		.incksel6 = 0x1b,
	},
};

/* Mode configs */
static const struct imx462_mode imx462_modes_2lanes[] = {
	{
		.width = 1920,
		.height = 1080,
		.hmax_min = 2200,
		.vmax_min = 1125,
		.link_freq_index = FREQ_INDEX_1080P,
		.ctrl_07 = IMX462_WINMODE_1080P,
		.data = imx462_1080p_settings,
		.data_size = ARRAY_SIZE(imx462_1080p_settings),
		.clk_cfg = imx462_1080p_clock_config,
	},
	{
		.width = 1280,
		.height = 720,
		.hmax_min = 3300,
		.vmax_min = 750,
		.link_freq_index = FREQ_INDEX_720P,
		.ctrl_07 = IMX462_WINMODE_720P,
		.data = imx462_720p_settings,
		.data_size = ARRAY_SIZE(imx462_720p_settings),
		.clk_cfg = imx462_720p_clock_config,
	},
};

static const struct imx462_mode imx462_modes_4lanes[] = {
	{
		.width = 1920,
		.height = 1080,
		.hmax_min = 2200,
		.vmax_min = 1125,
		.link_freq_index = FREQ_INDEX_1080P,
		.ctrl_07 = IMX462_WINMODE_1080P,
		.data = imx462_1080p_settings,
		.data_size = ARRAY_SIZE(imx462_1080p_settings),
		.clk_cfg = imx462_1080p_clock_config,
	},
	{
		.width = 1280,
		.height = 720,
		.hmax_min = 3300,
		.vmax_min = 750,
		.link_freq_index = FREQ_INDEX_720P,
		.ctrl_07 = IMX462_WINMODE_720P,
		.data = imx462_720p_settings,
		.data_size = ARRAY_SIZE(imx462_720p_settings),
		.clk_cfg = imx462_720p_clock_config,
	},
};

static inline const struct imx462_mode *imx462_modes_ptr(const struct imx462 *imx462)
{
	if (imx462->nlanes == 2)
		return imx462_modes_2lanes;
	else
		return imx462_modes_4lanes;
}

static inline int imx462_modes_num(const struct imx462 *imx462)
{
	if (imx462->nlanes == 2)
		return ARRAY_SIZE(imx462_modes_2lanes);
	else
		return ARRAY_SIZE(imx462_modes_4lanes);
}

struct imx462_format_info {
	u32 code[IMX462_VARIANT_MAX];
	u8 bpp;
	const struct imx462_regval *regs;
	unsigned int num_regs;
};

static const struct imx462_format_info imx462_formats[] = {
	{
		.code = {
			[IMX462_VARIANT_COLOUR] = MEDIA_BUS_FMT_SRGGB10_1X10,
			[IMX462_VARIANT_MONO] = MEDIA_BUS_FMT_Y10_1X10
		},
		.bpp = 10,
		.regs = imx462_10bit_settings,
		.num_regs = ARRAY_SIZE(imx462_10bit_settings),
	}, {
		.code = {
			[IMX462_VARIANT_COLOUR] = MEDIA_BUS_FMT_SRGGB12_1X12,
			[IMX462_VARIANT_MONO] = MEDIA_BUS_FMT_Y12_1X12
		},
		.bpp = 12,
		.regs = imx462_12bit_settings,
		.num_regs = ARRAY_SIZE(imx462_12bit_settings),
	}
};

static const struct imx462_format_info *
imx462_format_info(const struct imx462 *imx462, u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx462_formats); ++i) {
		const struct imx462_format_info *info = &imx462_formats[i];

		if (info->code[imx462->model->colour_variant] == code)
			return info;
	}

	return NULL;
}

/* -----------------------------------------------------------------------------
 * Register access
 */

static int __always_unused imx462_read(struct imx462 *imx462, u32 addr, u32 *value)
{
	u8 data[3] = { 0, 0, 0 };
	int ret;

	ret = regmap_raw_read(imx462->regmap, addr & IMX462_REG_ADDR_MASK,
			      data, (addr >> IMX462_REG_SIZE_SHIFT) & 3);
	if (ret < 0) {
		dev_err(imx462->dev, "%u-bit read from 0x%04x failed: %d\n",
			((addr >> IMX462_REG_SIZE_SHIFT) & 3) * 8,
			 addr & IMX462_REG_ADDR_MASK, ret);
		return ret;
	}

	*value = get_unaligned_le24(data);
	return 0;
}

static int imx462_write(struct imx462 *imx462, u32 addr, u32 value, int *err)
{
	u8 data[3];
	int ret;

	if (err && *err)
		return *err;

	put_unaligned_le24(value, data);

	ret = regmap_raw_write(imx462->regmap, addr & IMX462_REG_ADDR_MASK,
			       data, (addr >> IMX462_REG_SIZE_SHIFT) & 3);
	if (ret < 0) {
		dev_err(imx462->dev, "%u-bit write to 0x%04x failed: %d\n",
			((addr >> IMX462_REG_SIZE_SHIFT) & 3) * 8,
			 addr & IMX462_REG_ADDR_MASK, ret);
		if (err)
			*err = ret;
	}

	return ret;
}

static int imx462_set_register_array(struct imx462 *imx462,
				     const struct imx462_regval *settings,
				     unsigned int num_settings)
{
	unsigned int i;
	int ret;

	for (i = 0; i < num_settings; ++i, ++settings) {
		ret = imx462_write(imx462, settings->reg, settings->val, NULL);
		if (ret < 0)
			return ret;
	}

	/* Provide 10ms settle time */
	usleep_range(10000, 11000);

	return 0;
}

static int imx462_set_clock(struct imx462 *imx462)
{
	const struct imx462_mode *mode = imx462->current_mode;
	enum imx462_clk_freq clk_idx = imx462->xclk_idx;
	const struct imx462_clk_cfg *clk_cfg = &mode->clk_cfg[clk_idx];
	int ret;

	ret = imx462_set_register_array(imx462, xclk_regs[clk_idx],
					IMX462_NUM_CLK_REGS);

	imx462_write(imx462, IMX462_INCKSEL1, clk_cfg->incksel1, &ret);
	imx462_write(imx462, IMX462_INCKSEL2, clk_cfg->incksel2, &ret);
	imx462_write(imx462, IMX462_INCKSEL3, clk_cfg->incksel3, &ret);
	imx462_write(imx462, IMX462_INCKSEL4, clk_cfg->incksel4, &ret);
	imx462_write(imx462, IMX462_INCKSEL5, clk_cfg->incksel5, &ret);
	imx462_write(imx462, IMX462_INCKSEL6, clk_cfg->incksel6, &ret);

	return ret;
}

static int imx462_set_data_lanes(struct imx462 *imx462)
{
	int ret = 0;

	imx462_write(imx462, IMX462_PHY_LANE_NUM, imx462->nlanes - 1, &ret);
	imx462_write(imx462, IMX462_CSI_LANE_MODE, imx462->nlanes - 1, &ret);
	imx462_write(imx462, IMX462_FR_FDG_SEL, 0x01, &ret);

	return ret;
}

static int imx462_set_black_level(struct imx462 *imx462,
				  const struct v4l2_mbus_framefmt *format,
				  unsigned int black_level, int *err)
{
	unsigned int bpp = imx462_format_info(imx462, format->code)->bpp;

	return imx462_write(imx462, IMX462_BLKLEVEL,
			    black_level >> (16 - bpp), err);
}

static int imx462_set_csi_config(struct imx462 *imx462)
{
	const s64 *link_freqs = imx462_link_freqs_ptr(imx462);
	const struct imx462_csi_cfg *csi_cfg;
	int ret = 0;

	switch (link_freqs[imx462->current_mode->link_freq_index]) {
	case 445500000:
		csi_cfg = &imx462_csi_445_5mhz;
		break;
	case 297000000:
		csi_cfg = &imx462_csi_297mhz;
		break;
	case 222750000:
		csi_cfg = &imx462_csi_222_75mhz;
		break;
	case 148500000:
		csi_cfg = &imx462_csi_148_5mhz;
		break;
	default:
		return -EINVAL;
	}

	imx462_write(imx462, IMX462_REPETITION, csi_cfg->repetition, &ret);
	imx462_write(imx462, IMX462_TCLKPOST, csi_cfg->tclkpost, &ret);
	imx462_write(imx462, IMX462_THSZERO, csi_cfg->thszero, &ret);
	imx462_write(imx462, IMX462_THSPREPARE, csi_cfg->thsprepare, &ret);
	imx462_write(imx462, IMX462_TCLKTRAIL, csi_cfg->tclktrail, &ret);
	imx462_write(imx462, IMX462_THSTRAIL, csi_cfg->thstrail, &ret);
	imx462_write(imx462, IMX462_TCLKZERO, csi_cfg->tclkzero, &ret);
	imx462_write(imx462, IMX462_TCLKPREPARE, csi_cfg->tclkprepare, &ret);
	imx462_write(imx462, IMX462_TLPX, csi_cfg->tlpx, &ret);

	return ret;
}

static int imx462_setup_format(struct imx462 *imx462,
			       const struct v4l2_mbus_framefmt *format)
{
	const struct imx462_format_info *info;
	int ret;

	info = imx462_format_info(imx462, format->code);

	ret = imx462_set_register_array(imx462, info->regs, info->num_regs);
	if (ret < 0) {
		dev_err(imx462->dev, "Could not set format registers\n");
		return ret;
	}

	return imx462_set_black_level(imx462, format,
				      IMX462_BLACK_LEVEL_DEFAULT, &ret);
}

/* ----------------------------------------------------------------------------
 * Controls
 */
static void imx462_exposure_update(struct imx462 *imx462,
				   const struct imx462_mode *mode)
{
	unsigned int exposure_max;

	exposure_max = imx462->vblank->val + mode->height -
		       IMX462_EXPOSURE_OFFSET;
	__v4l2_ctrl_modify_range(imx462->exposure, 1, exposure_max, 1,
				 exposure_max);
}

static int imx462_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx462 *imx462 = container_of(ctrl->handler,
					     struct imx462, ctrls);
	const struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	int ret = 0, vmax;

	/*
	 * Return immediately for controls that don't need to be applied to the
	 * device.
	 */
	if (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY)
		return 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Changing vblank changes the allowed range for exposure. */
		imx462_exposure_update(imx462, imx462->current_mode);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(imx462->dev))
		return 0;

	state = v4l2_subdev_get_locked_active_state(&imx462->sd);
	format = v4l2_subdev_get_pad_format(&imx462->sd, state, 0);

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = imx462_write(imx462, IMX462_GAIN, ctrl->val, NULL);
		break;

	case V4L2_CID_VBLANK:
		ret = imx462_write(imx462, IMX462_VMAX,
				   ctrl->val + imx462->current_mode->height,
				   NULL);
		/*
		 * Due to the way that exposure is programmed in this sensor in
		 * relation to VMAX, we have to reprogramme it whenever VMAX is
		 * changed.
		 * Update ctrl so that the V4L2_CID_EXPOSURE case can refer to
		 * it.
		 */
		ctrl = imx462->exposure;
		fallthrough;
	case V4L2_CID_EXPOSURE:
		vmax = imx462->vblank->val + imx462->current_mode->height;
		ret = imx462_write(imx462, IMX462_SHS1,
				   vmax - ctrl->val - 1, NULL);
		break;

	case V4L2_CID_TEST_PATTERN:
		if (ctrl->val) {
			imx462_set_black_level(imx462, format, 0, &ret);
			usleep_range(10000, 11000);
			imx462_write(imx462, IMX462_PGCTRL,
				     (u8)(IMX462_PGCTRL_REGEN |
				     IMX462_PGCTRL_THRU |
				     IMX462_PGCTRL_MODE(ctrl->val)), &ret);
		} else {
			imx462_write(imx462, IMX462_PGCTRL, 0x00, &ret);
			usleep_range(10000, 11000);
			imx462_set_black_level(imx462, format,
					       IMX462_BLACK_LEVEL_DEFAULT, &ret);
		}
		break;

	case V4L2_CID_HBLANK:
		ret = imx462_write(imx462, IMX462_HMAX,
				   ctrl->val + imx462->current_mode->width,
				   NULL);
		break;

	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
	{
		u32 reg;

		reg = imx462->current_mode->ctrl_07;
		if (imx462->hflip->val)
			reg |= IMX462_HREVERSE;
		if (imx462->vflip->val)
			reg |= IMX462_VREVERSE;
		ret = imx462_write(imx462, IMX462_CTRL_07, reg, NULL);
		break;
	}

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(imx462->dev);
	pm_runtime_put_autosuspend(imx462->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx462_ctrl_ops = {
	.s_ctrl = imx462_set_ctrl,
};

static const char * const imx462_test_pattern_menu[] = {
	"Disabled",
	"Sequence Pattern 1",
	"Horizontal Color-bar Chart",
	"Vertical Color-bar Chart",
	"Sequence Pattern 2",
	"Gradation Pattern 1",
	"Gradation Pattern 2",
	"000/555h Toggle Pattern",
};

static void imx462_ctrl_update(struct imx462 *imx462,
			       const struct v4l2_mbus_framefmt *format,
			       const struct imx462_mode *mode)
{
	unsigned int hblank_min = mode->hmax_min - mode->width;
	unsigned int hblank_max = IMX462_HMAX_MAX - mode->width;
	unsigned int vblank_min = mode->vmax_min - mode->height;
	unsigned int vblank_max = IMX462_VMAX_MAX - mode->height;

	__v4l2_ctrl_s_ctrl(imx462->link_freq, mode->link_freq_index);

	__v4l2_ctrl_modify_range(imx462->hblank, hblank_min, hblank_max, 1,
				 hblank_min);
	__v4l2_ctrl_modify_range(imx462->vblank, vblank_min, vblank_max, 1,
				 vblank_min);
}

static int imx462_ctrl_init(struct imx462 *imx462)
{
	struct v4l2_fwnode_device_properties props;
	int ret;

	ret = v4l2_fwnode_device_parse(imx462->dev, &props);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&imx462->ctrls, 11);

	/*
	 * The sensor has an analog gain and a digital gain, both controlled
	 * through a single gain value, expressed in 0.3dB increments. Values
	 * from 0.0dB (0) to 30.0dB (100) apply analog gain only, higher values
	 * up to 72.0dB (240) add further digital gain. Limit the range to
	 * analog gain only, support for digital gain can be added separately
	 * if needed.
	 */
	v4l2_ctrl_new_std(&imx462->ctrls, &imx462_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, 0,
			  imx462->model->max_analog_gain, 1, 0);

	/*
	 * Correct range will be determined through imx462_ctrl_update setting
	 * V4L2_CID_VBLANK.
	 */
	imx462->exposure = v4l2_ctrl_new_std(&imx462->ctrls, &imx462_ctrl_ops,
					     V4L2_CID_EXPOSURE, 1, 65535, 1,
					     65535);

	/*
	 * Set the link frequency, pixel rate, horizontal blanking and vertical
	 * blanking to hardcoded values, they will be updated by
	 * imx462_ctrl_update().
	 */
	imx462->link_freq =
		v4l2_ctrl_new_int_menu(&imx462->ctrls, &imx462_ctrl_ops,
				       V4L2_CID_LINK_FREQ,
				       imx462_link_freqs_num(imx462) - 1, 0,
				       imx462_link_freqs_ptr(imx462));
	if (imx462->link_freq)
		imx462->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(&imx462->ctrls, &imx462_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  IMX462_PIXEL_RATE, IMX462_PIXEL_RATE, 1,
			  IMX462_PIXEL_RATE);

	v4l2_ctrl_new_std_menu_items(&imx462->ctrls, &imx462_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx462_test_pattern_menu) - 1,
				     0, 0, imx462_test_pattern_menu);

	/*
	 * Actual range will be set from imx462_ctrl_update later in the probe.
	 */
	imx462->hblank = v4l2_ctrl_new_std(&imx462->ctrls, &imx462_ctrl_ops,
					   V4L2_CID_HBLANK, 1, 1, 1, 1);

	imx462->vblank = v4l2_ctrl_new_std(&imx462->ctrls, &imx462_ctrl_ops,
					   V4L2_CID_VBLANK, 1, 1, 1, 1);

	imx462->hflip = v4l2_ctrl_new_std(&imx462->ctrls, &imx462_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	imx462->vflip = v4l2_ctrl_new_std(&imx462->ctrls, &imx462_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_cluster(2, &imx462->hflip);

	v4l2_ctrl_new_fwnode_properties(&imx462->ctrls, &imx462_ctrl_ops,
					&props);

	imx462->sd.ctrl_handler = &imx462->ctrls;

	if (imx462->ctrls.error) {
		ret = imx462->ctrls.error;
		v4l2_ctrl_handler_free(&imx462->ctrls);
		return ret;
	}

	return 0;
}

/* ----------------------------------------------------------------------------
 * Subdev operations
 */

/* Start streaming */
static int imx462_start_streaming(struct imx462 *imx462,
				  struct v4l2_subdev_state *state)
{
	const struct v4l2_mbus_framefmt *format;
	int ret;

	/* Set init register settings */
	ret = imx462_set_register_array(imx462, imx462_global_init_settings,
					ARRAY_SIZE(imx462_global_init_settings));
	if (ret < 0) {
		dev_err(imx462->dev, "Could not set init registers\n");
		return ret;
	}

	/* Set mdel specific init register settings */
	ret = imx462_set_register_array(imx462, imx462->model->init_regs,
					imx462->model->init_regs_num);
	if (ret < 0) {
		dev_err(imx462->dev, "Could not set model specific init registers\n");
		return ret;
	}

	/* Set clock parameters based on mode and xclk */
	ret = imx462_set_clock(imx462);
	if (ret < 0) {
		dev_err(imx462->dev, "Could not set clocks - %d\n", ret);
		return ret;
	}

	/* Set data lane count */
	ret = imx462_set_data_lanes(imx462);
	if (ret < 0) {
		dev_err(imx462->dev, "Could not set data lanes - %d\n", ret);
		return ret;
	}

	ret = imx462_set_csi_config(imx462);
	if (ret < 0) {
		dev_err(imx462->dev, "Could not set csi cfg - %d\n", ret);
		return ret;
	}

	/* Apply the register values related to current frame format */
	format = v4l2_subdev_get_pad_format(&imx462->sd, state, 0);
	ret = imx462_setup_format(imx462, format);
	if (ret < 0) {
		dev_err(imx462->dev, "Could not set frame format - %d\n", ret);
		return ret;
	}

	/* Apply default values of current mode */
	ret = imx462_set_register_array(imx462, imx462->current_mode->data,
					imx462->current_mode->data_size);
	if (ret < 0) {
		dev_err(imx462->dev, "Could not set current mode - %d\n", ret);
		return ret;
	}

	/* Apply customized values from user */
	ret = __v4l2_ctrl_handler_setup(imx462->sd.ctrl_handler);
	if (ret) {
		dev_err(imx462->dev, "Could not sync v4l2 controls - %d\n", ret);
		return ret;
	}

	imx462_write(imx462, IMX462_STANDBY, 0x00, &ret);

	msleep(30);

	/* Start streaming */
	return imx462_write(imx462, IMX462_XMSTA, 0x00, &ret);
}

/* Stop streaming */
static int imx462_stop_streaming(struct imx462 *imx462)
{
	int ret = 0;

	imx462_write(imx462, IMX462_STANDBY, 0x01, &ret);

	msleep(30);

	return imx462_write(imx462, IMX462_XMSTA, 0x01, &ret);
}

static int imx462_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx462 *imx462 = to_imx462(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable) {
		ret = pm_runtime_resume_and_get(imx462->dev);
		if (ret < 0)
			goto unlock;

		ret = imx462_start_streaming(imx462, state);
		if (ret) {
			dev_err(imx462->dev, "Start stream failed\n");
			pm_runtime_put_sync(imx462->dev);
			goto unlock;
		}
	} else {
		imx462_stop_streaming(imx462);
		pm_runtime_mark_last_busy(imx462->dev);
		pm_runtime_put_autosuspend(imx462->dev);
	}

	/*
	 * vflip and hflip should not be changed during streaming as the sensor
	 * will produce an invalid frame.
	 */
	__v4l2_ctrl_grab(imx462->vflip, enable);
	__v4l2_ctrl_grab(imx462->hflip, enable);

unlock:
	v4l2_subdev_unlock_state(state);
	return ret;
}

static int imx462_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx462 *imx462 = to_imx462(sd);
	const struct imx462_mode *mode;
	struct v4l2_subdev_state *state;
	unsigned long num, den;
	u64 frame_pixels;

	state = v4l2_subdev_lock_and_get_active_state(sd);
	mode = imx462->current_mode;
	frame_pixels = (u64)(mode->width + imx462->hblank->val) *
		       (mode->height + imx462->vblank->val);
	v4l2_subdev_unlock_state(state);

	rational_best_approximation(frame_pixels, IMX462_PIXEL_RATE,
				    U32_MAX, U32_MAX, &num, &den);
	fi->interval.numerator = num;
	fi->interval.denominator = den;

	return 0;
}

static int imx462_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	const struct imx462 *imx462 = to_imx462(sd);

	if (code->index >= ARRAY_SIZE(imx462_formats))
		return -EINVAL;

	code->code = imx462_formats[code->index].code[imx462->model->colour_variant];

	return 0;
}

static int imx462_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	const struct imx462 *imx462 = to_imx462(sd);
	const struct imx462_mode *imx462_modes = imx462_modes_ptr(imx462);

	if (!imx462_format_info(imx462, fse->code))
		return -EINVAL;

	if (fse->index >= imx462_modes_num(imx462))
		return -EINVAL;

	fse->min_width = imx462_modes[fse->index].width;
	fse->max_width = imx462_modes[fse->index].width;
	fse->min_height = imx462_modes[fse->index].height;
	fse->max_height = imx462_modes[fse->index].height;

	return 0;
}

/* Rockchip rkcif and rkaiq enumerate by index alone and expect code and size filled in */
static int imx462_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	const struct imx462 *imx462 = to_imx462(sd);
	unsigned int num_formats = ARRAY_SIZE(imx462_formats);
	const struct imx462_mode *mode;
	unsigned long num, den;

	if (fie->index >= imx462_modes_num(imx462) * num_formats)
		return -EINVAL;

	mode = &imx462_modes_ptr(imx462)[fie->index / num_formats];

	fie->code = imx462_formats[fie->index % num_formats]
			.code[imx462->model->colour_variant];
	fie->width = mode->width;
	fie->height = mode->height;

	rational_best_approximation(mode->hmax_min * mode->vmax_min,
				    IMX462_PIXEL_RATE, U32_MAX, U32_MAX,
				    &num, &den);
	fie->interval.numerator = num;
	fie->interval.denominator = den;

	return 0;
}

static int imx462_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct imx462 *imx462 = to_imx462(sd);
	const struct imx462_mode *mode;
	struct v4l2_mbus_framefmt *format;

	mode = v4l2_find_nearest_size(imx462_modes_ptr(imx462),
				      imx462_modes_num(imx462), width, height,
				      fmt->format.width, fmt->format.height);

	fmt->format.width = mode->width;
	fmt->format.height = mode->height;

	if (!imx462_format_info(imx462, fmt->format.code))
		fmt->format.code = imx462_formats[0].code[imx462->model->colour_variant];

	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->format.quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;

	format = v4l2_subdev_get_pad_format(sd, sd_state, 0);

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		imx462->current_mode = mode;

		imx462_ctrl_update(imx462, &fmt->format, mode);
		imx462_exposure_update(imx462, mode);
	}

	*format = fmt->format;

	return 0;
}

static int imx462_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct imx462 *imx462 = to_imx462(sd);
	struct v4l2_mbus_framefmt *format;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {
		format = v4l2_subdev_get_pad_format(sd, sd_state, 0);

		/*
		 * The sensor moves the readout by 1 pixel based on flips to
		 * keep the Bayer order the same.
		 */
		sel->r.top = IMX462_PIXEL_ARRAY_MARGIN_TOP
			   + (IMX462_PIXEL_ARRAY_RECORDING_HEIGHT - format->height) / 2
			   + imx462->vflip->val;
		sel->r.left = IMX462_PIXEL_ARRAY_MARGIN_LEFT
			    + (IMX462_PIXEL_ARRAY_RECORDING_WIDTH - format->width) / 2
			    + imx462->hflip->val;
		sel->r.width = format->width;
		sel->r.height = format->height;

		return 0;
	}

	/* No CROP_BOUNDS, Rockchip rkcif and mipi-csi2 read it as crop inside output frame */
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = IMX462_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX462_PIXEL_ARRAY_HEIGHT;

		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.top = IMX462_PIXEL_ARRAY_MARGIN_TOP;
		sel->r.left = IMX462_PIXEL_ARRAY_MARGIN_LEFT;
		sel->r.width = IMX462_PIXEL_ARRAY_RECORDING_WIDTH;
		sel->r.height = IMX462_PIXEL_ARRAY_RECORDING_HEIGHT;

		return 0;

	default:
		return -EINVAL;
	}
}

static int imx462_get_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				  struct v4l2_mbus_config *config)
{
	struct imx462 *imx462 = to_imx462(sd);

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->bus.mipi_csi2.num_data_lanes = imx462->nlanes;

	return 0;
}

static int imx462_entity_init_cfg(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.width = 1920,
			.height = 1080,
		},
	};

	imx462_set_fmt(subdev, sd_state, &fmt);

	return 0;
}

static const struct v4l2_subdev_core_ops imx462_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops imx462_video_ops = {
	.g_frame_interval = imx462_g_frame_interval,
	.s_stream = imx462_set_stream,
};

static const struct v4l2_subdev_pad_ops imx462_pad_ops = {
	.init_cfg = imx462_entity_init_cfg,
	.enum_mbus_code = imx462_enum_mbus_code,
	.enum_frame_size = imx462_enum_frame_size,
	.enum_frame_interval = imx462_enum_frame_interval,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx462_set_fmt,
	.get_selection = imx462_get_selection,
	.get_mbus_config = imx462_get_mbus_config,
};

static const struct v4l2_subdev_ops imx462_subdev_ops = {
	.core = &imx462_core_ops,
	.video = &imx462_video_ops,
	.pad = &imx462_pad_ops,
};

static const struct media_entity_operations imx462_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int imx462_subdev_init(struct imx462 *imx462)
{
	struct i2c_client *client = to_i2c_client(imx462->dev);
	const struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	int ret;

	imx462->current_mode = &imx462_modes_ptr(imx462)[0];

	v4l2_i2c_subdev_init(&imx462->sd, client, &imx462_subdev_ops);
	imx462->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
	imx462->sd.dev = imx462->dev;
	imx462->sd.entity.ops = &imx462_subdev_entity_ops;
	imx462->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	imx462->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&imx462->sd.entity, 1, &imx462->pad);
	if (ret < 0) {
		dev_err(imx462->dev, "Could not register media entity\n");
		return ret;
	}

	ret = imx462_ctrl_init(imx462);
	if (ret < 0) {
		dev_err(imx462->dev, "Control initialization error %d\n", ret);
		goto err_media;
	}

	imx462->sd.state_lock = imx462->ctrls.lock;

	ret = v4l2_subdev_init_finalize(&imx462->sd);
	if (ret < 0) {
		dev_err(imx462->dev, "subdev initialization error %d\n", ret);
		goto err_ctrls;
	}

	state = v4l2_subdev_lock_and_get_active_state(&imx462->sd);
	format = v4l2_subdev_get_pad_format(&imx462->sd, state, 0);
	imx462_ctrl_update(imx462, format, imx462->current_mode);
	v4l2_subdev_unlock_state(state);

	return 0;

err_ctrls:
	v4l2_ctrl_handler_free(&imx462->ctrls);
err_media:
	media_entity_cleanup(&imx462->sd.entity);
	return ret;
}

static void imx462_subdev_cleanup(struct imx462 *imx462)
{
	v4l2_subdev_cleanup(&imx462->sd);
	media_entity_cleanup(&imx462->sd.entity);
	v4l2_ctrl_handler_free(&imx462->ctrls);
}

/* ----------------------------------------------------------------------------
 * Power management
 */

static int imx462_power_on(struct imx462 *imx462)
{
	int ret;

	ret = clk_prepare_enable(imx462->xclk);
	if (ret) {
		dev_err(imx462->dev, "Failed to enable clock\n");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(imx462->supplies),
				    imx462->supplies);
	if (ret) {
		dev_err(imx462->dev, "Failed to enable regulators\n");
		clk_disable_unprepare(imx462->xclk);
		return ret;
	}

	usleep_range(1, 2);
	gpiod_set_value_cansleep(imx462->rst_gpio, 0);
	usleep_range(30000, 31000);

	return 0;
}

static void imx462_power_off(struct imx462 *imx462)
{
	clk_disable_unprepare(imx462->xclk);
	gpiod_set_value_cansleep(imx462->rst_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(imx462->supplies), imx462->supplies);
}

static int imx462_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx462 *imx462 = to_imx462(sd);

	return imx462_power_on(imx462);
}

static int imx462_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx462 *imx462 = to_imx462(sd);

	imx462_power_off(imx462);

	return 0;
}

static const struct dev_pm_ops imx462_pm_ops = {
	RUNTIME_PM_OPS(imx462_runtime_suspend, imx462_runtime_resume, NULL)
};

/* ----------------------------------------------------------------------------
 * Probe & remove
 */

static const struct regmap_config imx462_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

static const char * const imx462_supply_name[IMX462_NUM_SUPPLIES] = {
	"vdda",
	"vddd",
	"vdddo",
};

static int imx462_get_regulators(struct device *dev, struct imx462 *imx462)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx462->supplies); i++)
		imx462->supplies[i].supply = imx462_supply_name[i];

	return devm_regulator_bulk_get(dev, ARRAY_SIZE(imx462->supplies),
				       imx462->supplies);
}

static int imx462_init_clk(struct imx462 *imx462)
{
	u32 xclk_freq;
	int ret;

	ret = device_property_read_u32(imx462->dev, "clock-frequency",
				       &xclk_freq);
	if (ret) {
		dev_err(imx462->dev, "Could not get xclk frequency\n");
		return ret;
	}

	/* external clock must be 37.125 MHz or 74.25MHz */
	switch (xclk_freq) {
	case 37125000:
		imx462->xclk_idx = IMX462_CLK_37_125;
		break;
	case 74250000:
		imx462->xclk_idx = IMX462_CLK_74_25;
		break;
	default:
		dev_err(imx462->dev, "External clock frequency %u is not supported\n",
			xclk_freq);
		return -EINVAL;
	}

	ret = clk_set_rate(imx462->xclk, xclk_freq);
	if (ret) {
		dev_err(imx462->dev, "Could not set xclk frequency\n");
		return ret;
	}

	return 0;
}

/*
 * Returns 0 if all link frequencies used by the driver for the given number
 * of MIPI data lanes are mentioned in the device tree, or the value of the
 * first missing frequency otherwise.
 */
static s64 imx462_check_link_freqs(const struct imx462 *imx462,
				   const struct v4l2_fwnode_endpoint *ep)
{
	int i, j;
	const s64 *freqs = imx462_link_freqs_ptr(imx462);
	int freqs_count = imx462_link_freqs_num(imx462);

	for (i = 0; i < freqs_count; i++) {
		for (j = 0; j < ep->nr_of_link_frequencies; j++)
			if (freqs[i] == ep->link_frequencies[j])
				break;
		if (j == ep->nr_of_link_frequencies)
			return freqs[i];
	}
	return 0;
}

static const struct imx462_model_info imx462_models[] = {
	[IMX462_MODEL_IMX462LQR] = {
		.colour_variant = IMX462_VARIANT_COLOUR,
		.init_regs = imx462_global_init_settings_462,
		.init_regs_num = ARRAY_SIZE(imx462_global_init_settings_462),
		.max_analog_gain = 98,
		.name = "imx462",
	},
	[IMX462_MODEL_IMX462LLR] = {
		.colour_variant = IMX462_VARIANT_MONO,
		.init_regs = imx462_global_init_settings_462,
		.init_regs_num = ARRAY_SIZE(imx462_global_init_settings_462),
		.max_analog_gain = 98,
		.name = "imx462",
	},
};

static int imx462_parse_dt(struct imx462 *imx462)
{
	/* Only CSI2 is supported for now: */
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *endpoint;
	int ret;
	s64 fq;

	imx462->model = of_device_get_match_data(imx462->dev);

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(imx462->dev), NULL);
	if (!endpoint) {
		dev_err(imx462->dev, "Endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &ep);
	fwnode_handle_put(endpoint);
	if (ret == -ENXIO) {
		dev_err(imx462->dev, "Unsupported bus type, should be CSI2\n");
		goto done;
	} else if (ret) {
		dev_err(imx462->dev, "Parsing endpoint node failed\n");
		goto done;
	}

	/* Get number of data lanes */
	imx462->nlanes = ep.bus.mipi_csi2.num_data_lanes;
	if (imx462->nlanes != 2 && imx462->nlanes != 4) {
		dev_err(imx462->dev, "Invalid data lanes: %d\n", imx462->nlanes);
		ret = -EINVAL;
		goto done;
	}

	dev_dbg(imx462->dev, "Using %u data lanes\n", imx462->nlanes);

	if (!ep.nr_of_link_frequencies) {
		dev_err(imx462->dev, "link-frequency property not found in DT\n");
		ret = -EINVAL;
		goto done;
	}

	/* Check that link frequences for all the modes are in device tree */
	fq = imx462_check_link_freqs(imx462, &ep);
	if (fq) {
		dev_err(imx462->dev, "Link frequency of %lld is not supported\n",
			fq);
		ret = -EINVAL;
		goto done;
	}

	ret = 0;

done:
	v4l2_fwnode_endpoint_free(&ep);
	return ret;
}

static int imx462_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx462 *imx462;
	int ret;

	imx462 = devm_kzalloc(dev, sizeof(*imx462), GFP_KERNEL);
	if (!imx462)
		return -ENOMEM;

	imx462->dev = dev;
	imx462->regmap = devm_regmap_init_i2c(client, &imx462_regmap_config);
	if (IS_ERR(imx462->regmap)) {
		dev_err(dev, "Unable to initialize I2C\n");
		return -ENODEV;
	}

	ret = imx462_parse_dt(imx462);
	if (ret)
		return ret;

	/* Acquire resources. */
	imx462->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(imx462->xclk))
		return dev_err_probe(dev, PTR_ERR(imx462->xclk),
				     "Could not get xclk\n");

	ret = imx462_get_regulators(dev, imx462);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot get regulators\n");

	imx462->rst_gpio = devm_gpiod_get_optional(dev, "reset",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(imx462->rst_gpio))
		return dev_err_probe(dev, PTR_ERR(imx462->rst_gpio),
				     "Cannot get reset gpio\n");

	/* Initialize external clock frequency. */
	ret = imx462_init_clk(imx462);
	if (ret)
		return ret;

	/*
	 * Enable power management. The driver supports runtime PM, but needs to
	 * work when runtime PM is disabled in the kernel. To that end, power
	 * the sensor on manually here.
	 */
	ret = imx462_power_on(imx462);
	if (ret < 0) {
		dev_err(dev, "Could not power on the device\n");
		return ret;
	}

	/*
	 * Enable runtime PM with autosuspend. As the device has been powered
	 * manually, mark it as active, and increase the usage count without
	 * resuming the device.
	 */
	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	/* Initialize the V4L2 subdev. */
	ret = imx462_subdev_init(imx462);
	if (ret)
		goto err_pm;

	v4l2_i2c_subdev_set_name(&imx462->sd, client,
				 imx462->model->name, NULL);

	/*
	 * Finally, register the V4L2 subdev. This must be done after
	 * initializing everything as the subdev can be used immediately after
	 * being registered.
	 */
	ret = v4l2_async_register_subdev(&imx462->sd);
	if (ret < 0) {
		dev_err(dev, "Could not register v4l2 device\n");
		goto err_subdev;
	}

	/*
	 * Decrease the PM usage count. The device will get suspended after the
	 * autosuspend delay, turning the power off.
	 */
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_subdev:
	imx462_subdev_cleanup(imx462);
err_pm:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	imx462_power_off(imx462);
	return ret;
}

static void imx462_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx462 *imx462 = to_imx462(sd);

	v4l2_async_unregister_subdev(sd);
	imx462_subdev_cleanup(imx462);

	/*
	 * Disable runtime PM. In case runtime PM is disabled in the kernel,
	 * make sure to turn power off manually.
	 */
	pm_runtime_disable(imx462->dev);
	if (!pm_runtime_status_suspended(imx462->dev))
		imx462_power_off(imx462);
	pm_runtime_set_suspended(imx462->dev);
}

static const struct of_device_id imx462_of_match[] = {
	{
		.compatible = "sony,imx462lqr",
		.data = &imx462_models[IMX462_MODEL_IMX462LQR],
	}, {
		.compatible = "sony,imx462llr",
		.data = &imx462_models[IMX462_MODEL_IMX462LLR],
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx462_of_match);

static struct i2c_driver imx462_i2c_driver = {
	.probe_new = imx462_probe,
	.remove = imx462_remove,
	.driver = {
		.name = "imx462",
		.pm = pm_ptr(&imx462_pm_ops),
		.of_match_table = imx462_of_match,
	},
};

module_i2c_driver(imx462_i2c_driver);

MODULE_DESCRIPTION("Sony IMX462 CMOS Image Sensor Driver");
MODULE_AUTHOR("FRAMOS GmbH");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_LICENSE("GPL v2");
