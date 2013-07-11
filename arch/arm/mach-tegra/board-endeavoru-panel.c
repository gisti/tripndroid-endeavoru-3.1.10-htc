/*
 * arch/arm/mach-tegra/board-endeavor-panel.c
 *
 * Copyright (c) 2011-2012, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/resource.h>
#include <asm/mach-types.h>
#include <linux/platform_device.h>
#include <linux/earlysuspend.h>
#include <linux/tegra_pwm_bl.h>
#include <asm/atomic.h>
#include <linux/nvhost.h>
#include <linux/nvmap.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/dc.h>
#include <mach/fb.h>
#include <mach/hardware.h>
#include <mach/panel_id.h>
#include <mach/board_htc.h>
#include <mach/tegra_fb.h>

#include "board.h"
#include "devices.h"
#include "gpio-names.h"
#include "tegra3_host1x_devices.h"

#include <linux/disp_debug.h>
#include <linux/debugfs.h>

#define POWER_WAKEUP_ENR 7

#define DC_CTRL_MODE	TEGRA_DC_OUT_ONE_SHOT_MODE
/* Select panel to be used. */
#define DSI_PANEL_RESET 1

#ifdef CONFIG_TEGRA_DC
static struct regulator *endeavor_dsi_reg = NULL;
static struct regulator *v_lcm_3v3 = NULL;
static struct regulator *v_lcmio_1v8 = NULL;

static struct regulator *endeavor_hdmi_reg = NULL;
static struct regulator *endeavor_hdmi_pll = NULL;
#endif

#define LCM_TE			TEGRA_GPIO_PJ1
#define LCM_PWM			TEGRA_GPIO_PW1
#define LCM_RST			TEGRA_GPIO_PN6

#define MHL_INT         TEGRA_GPIO_PC7
#define MHL_USB_SEL     TEGRA_GPIO_PE0
#define MHL_1V2_EN      TEGRA_GPIO_PE4
#define MHL_RST         TEGRA_GPIO_PE6
#define MHL_HPD         TEGRA_GPIO_PN7
#define MHL_DDC_CLK     TEGRA_GPIO_PV4
#define MHL_DDC_DATA    TEGRA_GPIO_PV5
#define MHL_3V3_EN      TEGRA_GPIO_PY2

static struct workqueue_struct *bkl_wq;
static struct work_struct bkl_work;
static struct timer_list bkl_timer;

static int is_power_on = 0;

static struct gpio panel_init_gpios[] = {
    {LCM_TE,        GPIOF_IN,               "lcm_te"},
    {LCM_PWM,       GPIOF_OUT_INIT_LOW,     "pm0"},
    {LCM_RST,       GPIOF_OUT_INIT_HIGH,    "lcm reset"},
    {MHL_INT,       GPIOF_IN,               "mhl_int"},
    {MHL_1V2_EN,    GPIOF_OUT_INIT_HIGH,    "mhl_1v2_en"},
    {MHL_RST,       GPIOF_OUT_INIT_HIGH,    "mhl_rst"},
    {MHL_HPD,       GPIOF_IN,               "mhl_hpd"},
    {MHL_3V3_EN,    GPIOF_OUT_INIT_HIGH,    "mhl_3v3_en"},
};

static tegra_dc_bl_output endeavor_bl_output_measured_a02 = {
	1, 5, 9, 10, 11, 12, 12, 13,
	13, 14, 14, 15, 15, 16, 16, 17,
	17, 18, 18, 19, 19, 20, 21, 21,
	22, 22, 23, 24, 24, 25, 26, 26,
	27, 27, 28, 29, 29, 31, 31, 32,
	32, 33, 34, 35, 36, 36, 37, 38,
	39, 39, 40, 41, 41, 42, 43, 43,
	44, 45, 45, 46, 47, 47, 48, 49,
	49, 50, 51, 51, 52, 53, 53, 54,
	55, 56, 56, 57, 58, 59, 60, 61,
	61, 62, 63, 64, 65, 65, 66, 67,
	67, 68, 69, 69, 70, 71, 71, 72,
	73, 73, 74, 74, 75, 76, 76, 77,
	77, 78, 79, 79, 80, 81, 82, 83,
	83, 84, 85, 85, 86, 86, 88, 89,
	90, 91, 91, 92, 93, 93, 94, 95,
	95, 96, 97, 97, 98, 99, 99, 100,
	101, 101, 102, 103, 103, 104, 105, 105,
	107, 107, 108, 109, 110, 111, 111, 112,
	113, 113, 114, 115, 115, 116, 117, 117,
	118, 119, 119, 120, 121, 122, 123, 124,
	124, 125, 126, 126, 127, 128, 129, 129,
	130, 131, 131, 132, 133, 133, 134, 135,
	135, 136, 137, 137, 138, 139, 139, 140,
	142, 142, 143, 144, 145, 146, 147, 147,
	148, 149, 149, 150, 151, 152, 153, 153,
	153, 154, 155, 156, 157, 158, 158, 159,
	160, 161, 162, 163, 163, 164, 165, 165,
	166, 166, 167, 168, 169, 169, 170, 170,
	171, 172, 173, 173, 174, 175, 175, 176,
	176, 178, 178, 179, 180, 181, 182, 182,
	183, 184, 185, 186, 186, 187, 188, 188
};
static atomic_t sd_brightness = ATOMIC_INIT(255);

/*global varible for work around*/
static bool g_display_on = true;
static p_tegra_dc_bl_output bl_output;

#define BACKLIGHT_MAX 255

#define ORIG_PWM_MAX 255
#define ORIG_PWM_DEF 78
#define ORIG_PWM_MIN 30

#define MAP_PWM_LOW_DEF         79
#define MAP_PWM_HIGH_DEF        102

static int def_pwm = ORIG_PWM_DEF;

static int endeavor_backlight_notify(struct device *unused, int brightness)
{
	int cur_sd_brightness = atomic_read(&sd_brightness);

	if (brightness > 255)
		pr_info("Error: Brightness > 255!\n");
	else
		brightness = bl_output[brightness];

	brightness = (brightness * cur_sd_brightness) / 255;

	return brightness;
}

static int endeavor_disp1_check_fb(struct device *dev, struct fb_info *info);

/*
 * In case which_pwm is TEGRA_PWM_PM0,
 * gpio_conf_to_sfio should be TEGRA_GPIO_PW0: set LCD_CS1_N pin to SFIO
 * In case which_pwm is TEGRA_PWM_PM1,
 * gpio_conf_to_sfio should be TEGRA_GPIO_PW1: set LCD_M1 pin to SFIO
 */
static struct platform_tegra_pwm_backlight_data endeavor_disp1_backlight_data = {
	.which_dc		= 0,
	.which_pwm		= TEGRA_PWM_PM1,
	.gpio_conf_to_sfio	= TEGRA_GPIO_PW1,
	.switch_to_sfio		= &tegra_gpio_disable,
	.max_brightness		= 255,
	.dft_brightness		= 50,
	.notify		= endeavor_backlight_notify,
	.period			= 0xFF,
	.clk_div		= 20,
	.clk_select		= 0,
	.backlight_mode = MIPI_BACKLIGHT,	//Set MIPI_BACKLIGHT as default
	/* Only toggle backlight on fb blank notifications for disp1 */
	.check_fb	= endeavor_disp1_check_fb,
	.backlight_status	= BACKLIGHT_ENABLE,
	.dimming_enable	= true,
	.cam_launch_bkl_value = 181,
};

static struct platform_device endeavor_disp1_backlight_device = {
	.name	= "tegra-pwm-bl",
	.id	= -1,
	.dev	= {
		.platform_data = &endeavor_disp1_backlight_data,
	},
};

#ifdef CONFIG_TEGRA_DC
static int endeavor_hdmi_vddio_enable(void)
{
	return 0;
}

static int endeavor_hdmi_vddio_disable(void)
{
	return 0;
}

static int endeavor_hdmi_enable(void)
{
	REGULATOR_GET(endeavor_hdmi_reg, "avdd_hdmi");
	regulator_enable(endeavor_hdmi_reg);

	REGULATOR_GET(endeavor_hdmi_pll, "avdd_hdmi_pll");
	regulator_enable(endeavor_hdmi_pll);

failed:
	return 0;
}

static int endeavor_hdmi_disable(void)
{

	regulator_disable(endeavor_hdmi_reg);
	regulator_put(endeavor_hdmi_reg);
	endeavor_hdmi_reg = NULL;

	regulator_disable(endeavor_hdmi_pll);
	regulator_put(endeavor_hdmi_pll);
	endeavor_hdmi_pll = NULL;

	return 0;
}
static struct resource endeavor_disp1_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_GENERAL,
		.end	= INT_DISPLAY_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.start	= 0,	/* Filled in by endeavor_panel_init() */
		.end	= 0,	/* Filled in by endeavor_panel_init() */
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "dsi_regs",
		.start	= TEGRA_DSI_BASE,
		.end	= TEGRA_DSI_BASE + TEGRA_DSI_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource endeavor_disp2_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_B_GENERAL,
		.end	= INT_DISPLAY_B_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY2_BASE,
		.end	= TEGRA_DISPLAY2_BASE + TEGRA_DISPLAY2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.flags	= IORESOURCE_MEM,
		.start	= 0,
		.end	= 0,
	},
	{
		.name	= "hdmi_regs",
		.start	= TEGRA_HDMI_BASE,
		.end	= TEGRA_HDMI_BASE + TEGRA_HDMI_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_dc_sd_settings endeavor_sd_settings = {
	.enable = 1, /* Normal mode operation */
	.use_auto_pwm = false,
	.hw_update_delay = 0,
	.bin_width = -1,
	.aggressiveness = 1,
	.phase_in_adjustments = true,
#ifdef CONFIG_TEGRA_SD_GEN2
	.k_limit_enable = true,
	.k_limit = 180,
	.sd_window_enable = false,
	.soft_clipping_enable = true,
	/* Low soft clipping threshold to compensate for aggressive k_limit */
	.soft_clipping_threshold = 128,
	.smooth_k_enable = true,
	.smooth_k_incr = 4,
#endif
	.use_vid_luma = false,
	/* Default video coefficients */
	.coeff = {5, 9, 2},
	.fc = {0, 0},
	/* Immediate backlight changes */
	.blp = {1024, 255},
	/* Gammas: R: 2.2 G: 2.2 B: 2.2 */
	/* Default BL TF */
	.bltf = {
			{
				{57, 65, 74, 83},
				{93, 103, 114, 126},
				{138, 151, 165, 179},
				{194, 209, 225, 242},
			},
			{
				{58, 66, 75, 84},
				{94, 105, 116, 127},
				{140, 153, 166, 181},
				{196, 211, 227, 244},
			},
			{
				{60, 68, 77, 87},
				{97, 107, 119, 130},
				{143, 156, 170, 184},
				{199, 215, 231, 248},
			},
			{
				{64, 73, 82, 91},
				{102, 113, 124, 137},
				{149, 163, 177, 192},
				{207, 223, 240, 255},
			},
		},
	/* Default LUT */
	.lut = {
			{
				{250, 250, 250},
				{194, 194, 194},
				{149, 149, 149},
				{113, 113, 113},
				{82, 82, 82},
				{56, 56, 56},
				{34, 34, 34},
				{15, 15, 15},
				{0, 0, 0},
			},
			{
				{246, 246, 246},
				{191, 191, 191},
				{147, 147, 147},
				{111, 111, 111},
				{80, 80, 80},
				{55, 55, 55},
				{33, 33, 33},
				{14, 14, 14},
				{0, 0, 0},
			},
			{
				{239, 239, 239},
				{185, 185, 185},
				{142, 142, 142},
				{107, 107, 107},
				{77, 77, 77},
				{52, 52, 52},
				{30, 30, 30},
				{12, 12, 12},
				{0, 0, 0},
			},
			{
				{224, 224, 224},
				{173, 173, 173},
				{133, 133, 133},
				{99, 99, 99},
				{70, 70, 70},
				{46, 46, 46},
				{25, 25, 25},
				{7, 7, 7},
				{0, 0, 0},
			},
		},
	.sd_brightness = &sd_brightness,
	.bl_device = &endeavor_disp1_backlight_device,
};

static struct tegra_fb_data endeavor_hdmi_fb_data = {
	.win		= 0,
	.xres		= 1366,
	.yres		= 768,
	.bits_per_pixel	= 32,
	.flags		= TEGRA_FB_FLIP_ON_PROBE,
};

static struct tegra_dc_out endeavor_disp2_out = {
	.type		= TEGRA_DC_OUT_HDMI,
	.flags		= TEGRA_DC_OUT_HOTPLUG_HIGH,
	.parent_clk     = "pll_d2_out0",

	.dcc_bus	= 3,
	.hotplug_gpio	= MHL_HPD,

	.max_pixclock	= KHZ2PICOS(148500),

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,

	.enable		= endeavor_hdmi_enable,
	.disable	= endeavor_hdmi_disable,
	.postsuspend	= endeavor_hdmi_vddio_disable,
	.hotplug_init	= endeavor_hdmi_vddio_enable,
};

static struct tegra_dc_platform_data endeavor_disp2_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &endeavor_disp2_out,
	.fb		= &endeavor_hdmi_fb_data,
	.emc_clk_rate	= 300000000,
};

static int endeavor_dsi_panel_enable(void)
{
	/*TODO the power-on sequence move to bridge_reset*/
	return 0;
}
static int bridge_reset(void)
{
	int err = 0;

	if (is_power_on) {
		DISP_INFO_LN("is_power_on:%d\n", is_power_on);
		return 0;
	}

	/*TODO delay for DSI hardware stable*/
	hr_msleep(10);

	/*change LCM_TE & LCM_PWM to SFIO*/
	//tegra_gpio_disable(LCM_PWM);
	tegra_gpio_disable(LCM_TE);

	/*TODO: workaround to prevent panel off during dc_probe, remove it later*/
	if(g_display_on)
	{
		REGULATOR_GET(endeavor_dsi_reg, "avdd_dsi_csi");
		regulator_enable(endeavor_dsi_reg);

		REGULATOR_GET(v_lcm_3v3, "v_lcm_3v3");
		REGULATOR_GET(v_lcmio_1v8, "v_lcmio_1v8");

		regulator_enable(v_lcmio_1v8);
		regulator_enable(v_lcm_3v3);

		DISP_INFO_LN("Workaround for first panel init sequence\n");
		goto success;
		return 0;
	}

	REGULATOR_GET(endeavor_dsi_reg, "avdd_dsi_csi");
	regulator_enable(endeavor_dsi_reg);

	REGULATOR_GET(v_lcm_3v3, "v_lcm_3v3");
	REGULATOR_GET(v_lcmio_1v8, "v_lcmio_1v8");

	/*LCD_RST pull low*/
	gpio_set_value(LCM_RST, 0);
	hr_msleep(5);
	/*Turn on LCMIO_1V8_EN*/
	regulator_enable(v_lcmio_1v8);
	hr_msleep(1);
	/*Turn on LCMIO_3V3_EN*/
	regulator_enable(v_lcm_3v3);
	switch (g_panel_id) {
		case PANEL_ID_ENR_SHARP_HX_XA:
		case PANEL_ID_ENR_SHARP_HX_C3:
		case PANEL_ID_ENRTD_SHARP_HX_XA:
		case PANEL_ID_ENRTD_SHARP_HX_C3:
		case PANEL_ID_ENR_SHARP_HX_C4:
		case PANEL_ID_ENRTD_SHARP_HX_C4:
			hr_msleep(10);
			/*read LCD_ID0,LCD_ID1*/
			hr_msleep(2);
		break;
		default:
			hr_msleep(20);
	}
	gpio_set_value(LCM_RST, 1);
	hr_msleep(1);
failed:
success:
	is_power_on = 1;
	DISP_INFO_LN("is_power_on:%d\n", is_power_on);

	return err;
}

static int ic_reset(void)
{
	int err = 0;

	DISP_INFO_IN();

	if(g_display_on) {
		g_display_on = false;
		DISP_INFO_LN("Workaround for first panel init sequence\n");
		goto success;
		return 0;
	}
	hr_msleep(2);
	gpio_set_value(LCM_RST, 0);
	hr_msleep(1);
	gpio_set_value(LCM_RST, 1);
	hr_msleep(10);

success:
	DISP_INFO_OUT();
	return err;
}

static int endeavor_dsi_panel_disable(void)
{
	int err = 0;

	if (!is_power_on) {
		DISP_INFO_LN("is_power_on:%d\n", is_power_on);
		return 0;
	}

	gpio_set_value(LCM_RST, 0);
	hr_msleep(12);

	REGULATOR_GET(v_lcm_3v3, "v_lcm_3v3");
	regulator_disable(v_lcm_3v3);
	hr_msleep(5);

	REGULATOR_GET(v_lcmio_1v8, "v_lcmio_1v8");
	regulator_disable(v_lcmio_1v8);


	REGULATOR_GET(endeavor_dsi_reg, "avdd_dsi_csi");
	regulator_disable(endeavor_dsi_reg);

	/*change LCM_TE & LCM_PWM to GPIO*/
	//tegra_gpio_enable(LCM_PWM);
	tegra_gpio_enable(LCM_TE);

	is_power_on = 0;
	DISP_INFO_LN("is_power_on:%d\n", is_power_on);
failed:

	DISP_INFO_OUT();

	return err;
}
#endif

static void endeavor_stereo_set_mode(int mode)
{
	switch (mode) {
	case TEGRA_DC_STEREO_MODE_2D:
		/*gpio_set_value(TEGRA_GPIO_PH1, ENDEAVOR_STEREO_2D);*/
		break;
	case TEGRA_DC_STEREO_MODE_3D:
		/*gpio_set_value(TEGRA_GPIO_PH1, ENDEAVOR_STEREO_3D);*/
		break;
	}
}

static void endeavor_stereo_set_orientation(int mode)
{
	switch (mode) {
	case TEGRA_DC_STEREO_LANDSCAPE:
		/*gpio_set_value(TEGRA_GPIO_PH2, ENDEAVOR_STEREO_LANDSCAPE);*/
		break;
	case TEGRA_DC_STEREO_PORTRAIT:
		/*gpio_set_value(TEGRA_GPIO_PH2, ENDEAVOR_STEREO_PORTRAIT);*/
		break;
	}
}

#ifdef CONFIG_TEGRA_DC
static int endeavor_dsi_panel_postsuspend(void)
{
	int err = 0;


	return err;
}
#endif

/*  --- -------------------------------------------  ---*/

static struct tegra_dsi_cmd dsi_init_sharp_nt_c2_9a_cmd[]= {
	DSI_CMD_SHORT(0x15, 0xC2, 0x08),

	DSI_CMD_SHORT(0x15, 0xFF, 0x03),
	DSI_CMD_SHORT(0x15, 0xFE, 0x08),
	DSI_CMD_SHORT(0x15, 0x18, 0x00),
	DSI_CMD_SHORT(0x15, 0x19, 0x00),
	DSI_CMD_SHORT(0x15, 0x1A, 0x00),
	DSI_CMD_SHORT(0x15, 0x25, 0x26),

	DSI_CMD_SHORT(0x15, 0x00, 0x00),
	DSI_CMD_SHORT(0x15, 0x01, 0x05),
	DSI_CMD_SHORT(0x15, 0x02, 0x10),
	DSI_CMD_SHORT(0x15, 0x03, 0x17),
	DSI_CMD_SHORT(0x15, 0x04, 0x22),
	DSI_CMD_SHORT(0x15, 0x05, 0x26),
	DSI_CMD_SHORT(0x15, 0x06, 0x29),
	DSI_CMD_SHORT(0x15, 0x07, 0x29),
	DSI_CMD_SHORT(0x15, 0x08, 0x26),
	DSI_CMD_SHORT(0x15, 0x09, 0x23),
	DSI_CMD_SHORT(0x15, 0x0A, 0x17),
	DSI_CMD_SHORT(0x15, 0x0B, 0x12),
	DSI_CMD_SHORT(0x15, 0x0C, 0x06),
	DSI_CMD_SHORT(0x15, 0x0D, 0x02),
	DSI_CMD_SHORT(0x15, 0x0E, 0x01),
	DSI_CMD_SHORT(0x15, 0x0F, 0x00),

	DSI_CMD_SHORT(0x15, 0xFB, 0x01),
	DSI_CMD_SHORT(0x15, 0xFF, 0x00),
	DSI_CMD_SHORT(0x15, 0xFE, 0x01),

	DSI_CMD_SHORT(0x15, 0xFF, 0x05),
	DSI_CMD_SHORT(0x15, 0xFB, 0x01),
	DSI_CMD_SHORT(0x15, 0x28, 0x01),
	DSI_CMD_SHORT(0x15, 0x2F, 0x02),
	DSI_CMD_SHORT(0x15, 0xFF, 0x00),

	DSI_CMD_SHORT(0x05, 0x11, 0x00),
	DSI_DLY_MS(125),

	DSI_CMD_SHORT(0x15, 0xFF, 0x01),
	DSI_CMD_SHORT(0x15, 0xFE, 0x02),
	DSI_CMD_SHORT(0x15, 0x75, 0x00),
	DSI_CMD_SHORT(0x15, 0x76, 0x9F),
	DSI_CMD_SHORT(0x15, 0x77, 0x00),
	DSI_CMD_SHORT(0x15, 0x78, 0xA8),
	DSI_CMD_SHORT(0x15, 0x79, 0x00),
	DSI_CMD_SHORT(0x15, 0x7A, 0xB8),
	DSI_CMD_SHORT(0x15, 0x7B, 0x00),
	DSI_CMD_SHORT(0x15, 0x7C, 0xC8),
	DSI_CMD_SHORT(0x15, 0x7D, 0x00),
	DSI_CMD_SHORT(0x15, 0x7E, 0xD5),
	DSI_CMD_SHORT(0x15, 0x7F, 0x00),
	DSI_CMD_SHORT(0x15, 0x80, 0xE2),
	DSI_CMD_SHORT(0x15, 0x81, 0x00),
	DSI_CMD_SHORT(0x15, 0x82, 0xEE),
	DSI_CMD_SHORT(0x15, 0x83, 0x00),
	DSI_CMD_SHORT(0x15, 0x84, 0xFA),
	DSI_CMD_SHORT(0x15, 0x85, 0x01),
	DSI_CMD_SHORT(0x15, 0x86, 0x04),
	DSI_CMD_SHORT(0x15, 0x87, 0x01),
	DSI_CMD_SHORT(0x15, 0x88, 0x2A),
	DSI_CMD_SHORT(0x15, 0x89, 0x01),
	DSI_CMD_SHORT(0x15, 0x8A, 0x4A),
	DSI_CMD_SHORT(0x15, 0x8B, 0x01),
	DSI_CMD_SHORT(0x15, 0x8C, 0x7F),
	DSI_CMD_SHORT(0x15, 0x8D, 0x01),
	DSI_CMD_SHORT(0x15, 0x8E, 0xAC),
	DSI_CMD_SHORT(0x15, 0x8F, 0x01),
	DSI_CMD_SHORT(0x15, 0x90, 0xF2),
	DSI_CMD_SHORT(0x15, 0x91, 0x02),
	DSI_CMD_SHORT(0x15, 0x92, 0x26),
	DSI_CMD_SHORT(0x15, 0x93, 0x02),
	DSI_CMD_SHORT(0x15, 0x94, 0x27),
	DSI_CMD_SHORT(0x15, 0x95, 0x02),
	DSI_CMD_SHORT(0x15, 0x96, 0x56),
	DSI_CMD_SHORT(0x15, 0x97, 0x02),
	DSI_CMD_SHORT(0x15, 0x98, 0x89),
	DSI_CMD_SHORT(0x15, 0x99, 0x02),
	DSI_CMD_SHORT(0x15, 0x9A, 0xAA),
	DSI_CMD_SHORT(0x15, 0x9B, 0x02),
	DSI_CMD_SHORT(0x15, 0x9C, 0xD8),
	DSI_CMD_SHORT(0x15, 0x9D, 0x02),
	DSI_CMD_SHORT(0x15, 0x9E, 0xF8),
	DSI_CMD_SHORT(0x15, 0x9F, 0x03),
	DSI_CMD_SHORT(0x15, 0xA0, 0x21),
	DSI_CMD_SHORT(0x15, 0xA2, 0x03),
	DSI_CMD_SHORT(0x15, 0xA3, 0x2C),
	DSI_CMD_SHORT(0x15, 0xA4, 0x03),
	DSI_CMD_SHORT(0x15, 0xA5, 0x3D),
	DSI_CMD_SHORT(0x15, 0xA6, 0x03),
	DSI_CMD_SHORT(0x15, 0xA7, 0x4A),
	DSI_CMD_SHORT(0x15, 0xA9, 0x03),
	DSI_CMD_SHORT(0x15, 0xAA, 0x60),
	DSI_CMD_SHORT(0x15, 0xAB, 0x03),
	DSI_CMD_SHORT(0x15, 0xAC, 0x72),
	DSI_CMD_SHORT(0x15, 0xAD, 0x03),
	DSI_CMD_SHORT(0x15, 0xAE, 0x8F),
	DSI_CMD_SHORT(0x15, 0xAF, 0x03),
	DSI_CMD_SHORT(0x15, 0xB0, 0xA9),
	DSI_CMD_SHORT(0x15, 0xB1, 0x03),
	DSI_CMD_SHORT(0x15, 0xB2, 0xAC),
	DSI_CMD_SHORT(0x15, 0xB3, 0x01),
	DSI_CMD_SHORT(0x15, 0xB4, 0x06),
	DSI_CMD_SHORT(0x15, 0xB5, 0x01),
	DSI_CMD_SHORT(0x15, 0xB6, 0x0D),
	DSI_CMD_SHORT(0x15, 0xB7, 0x01),
	DSI_CMD_SHORT(0x15, 0xB8, 0x1D),
	DSI_CMD_SHORT(0x15, 0xB9, 0x01),
	DSI_CMD_SHORT(0x15, 0xBA, 0x2B),
	DSI_CMD_SHORT(0x15, 0xBB, 0x01),
	DSI_CMD_SHORT(0x15, 0xBC, 0x38),
	DSI_CMD_SHORT(0x15, 0xBD, 0x01),
	DSI_CMD_SHORT(0x15, 0xBE, 0x44),
	DSI_CMD_SHORT(0x15, 0xBF, 0x01),
	DSI_CMD_SHORT(0x15, 0xC0, 0x4F),
	DSI_CMD_SHORT(0x15, 0xC1, 0x01),
	DSI_CMD_SHORT(0x15, 0xC2, 0x59),
	DSI_CMD_SHORT(0x15, 0xC3, 0x01),
	DSI_CMD_SHORT(0x15, 0xC4, 0x62),
	DSI_CMD_SHORT(0x15, 0xC5, 0x01),
	DSI_CMD_SHORT(0x15, 0xC6, 0x84),
	DSI_CMD_SHORT(0x15, 0xC7, 0x01),
	DSI_CMD_SHORT(0x15, 0xC8, 0xA0),
	DSI_CMD_SHORT(0x15, 0xC9, 0x01),
	DSI_CMD_SHORT(0x15, 0xCA, 0xCD),
	DSI_CMD_SHORT(0x15, 0xCB, 0x01),
	DSI_CMD_SHORT(0x15, 0xCC, 0xF1),
	DSI_CMD_SHORT(0x15, 0xCD, 0x02),
	DSI_CMD_SHORT(0x15, 0xCE, 0x2C),
	DSI_CMD_SHORT(0x15, 0xCF, 0x02),
	DSI_CMD_SHORT(0x15, 0xD0, 0x60),
	DSI_CMD_SHORT(0x15, 0xD1, 0x02),
	DSI_CMD_SHORT(0x15, 0xD2, 0x61),
	DSI_CMD_SHORT(0x15, 0xD3, 0x02),
	DSI_CMD_SHORT(0x15, 0xD4, 0x90),
	DSI_CMD_SHORT(0x15, 0xD5, 0x02),
	DSI_CMD_SHORT(0x15, 0xD6, 0xC3),
	DSI_CMD_SHORT(0x15, 0xD7, 0x02),
	DSI_CMD_SHORT(0x15, 0xD8, 0xE4),
	DSI_CMD_SHORT(0x15, 0xD9, 0x03),
	DSI_CMD_SHORT(0x15, 0xDA, 0x12),
	DSI_CMD_SHORT(0x15, 0xDB, 0x03),
	DSI_CMD_SHORT(0x15, 0xDC, 0x32),
	DSI_CMD_SHORT(0x15, 0xDD, 0x03),
	DSI_CMD_SHORT(0x15, 0xDE, 0x5B),
	DSI_CMD_SHORT(0x15, 0xDF, 0x03),
	DSI_CMD_SHORT(0x15, 0xE0, 0x66),
	DSI_CMD_SHORT(0x15, 0xE1, 0x03),
	DSI_CMD_SHORT(0x15, 0xE2, 0x77),
	DSI_CMD_SHORT(0x15, 0xE3, 0x03),
	DSI_CMD_SHORT(0x15, 0xE4, 0x84),
	DSI_CMD_SHORT(0x15, 0xE5, 0x03),
	DSI_CMD_SHORT(0x15, 0xE6, 0x99),
	DSI_CMD_SHORT(0x15, 0xE7, 0x03),
	DSI_CMD_SHORT(0x15, 0xE8, 0xAB),
	DSI_CMD_SHORT(0x15, 0xE9, 0x03),
	DSI_CMD_SHORT(0x15, 0xEA, 0xC9),
	DSI_CMD_SHORT(0x15, 0xEB, 0x03),
	DSI_CMD_SHORT(0x15, 0xEC, 0xE3),
	DSI_CMD_SHORT(0x15, 0xED, 0x03),
	DSI_CMD_SHORT(0x15, 0xEE, 0xE6),
	DSI_CMD_SHORT(0x15, 0xEF, 0x00),
	DSI_CMD_SHORT(0x15, 0xF0, 0x9F),
	DSI_CMD_SHORT(0x15, 0xF1, 0x00),
	DSI_CMD_SHORT(0x15, 0xF2, 0xA8),
	DSI_CMD_SHORT(0x15, 0xF3, 0x00),
	DSI_CMD_SHORT(0x15, 0xF4, 0xB8),
	DSI_CMD_SHORT(0x15, 0xF5, 0x00),
	DSI_CMD_SHORT(0x15, 0xF6, 0xC8),
	DSI_CMD_SHORT(0x15, 0xF7, 0x00),
	DSI_CMD_SHORT(0x15, 0xF8, 0xD5),
	DSI_CMD_SHORT(0x15, 0xF9, 0x00),
	DSI_CMD_SHORT(0x15, 0xFA, 0xE2),
	DSI_CMD_SHORT(0x15, 0xFF, 0x00),
	DSI_CMD_SHORT(0x15, 0xFE, 0x01),
	DSI_CMD_SHORT(0x15, 0xFF, 0x02),
	DSI_CMD_SHORT(0x15, 0xFE, 0x04),
	DSI_CMD_SHORT(0x15, 0x00, 0x00),
	DSI_CMD_SHORT(0x15, 0x01, 0xEE),
	DSI_CMD_SHORT(0x15, 0x02, 0x00),
	DSI_CMD_SHORT(0x15, 0x03, 0xFA),
	DSI_CMD_SHORT(0x15, 0x04, 0x01),
	DSI_CMD_SHORT(0x15, 0x05, 0x04),
	DSI_CMD_SHORT(0x15, 0x06, 0x01),
	DSI_CMD_SHORT(0x15, 0x07, 0x2A),
	DSI_CMD_SHORT(0x15, 0x08, 0x01),
	DSI_CMD_SHORT(0x15, 0x09, 0x4A),
	DSI_CMD_SHORT(0x15, 0x0A, 0x01),
	DSI_CMD_SHORT(0x15, 0x0B, 0x7F),
	DSI_CMD_SHORT(0x15, 0x0C, 0x01),
	DSI_CMD_SHORT(0x15, 0x0D, 0xAC),
	DSI_CMD_SHORT(0x15, 0x0E, 0x01),
	DSI_CMD_SHORT(0x15, 0x0F, 0xF2),
	DSI_CMD_SHORT(0x15, 0x10, 0x02),
	DSI_CMD_SHORT(0x15, 0x11, 0x26),
	DSI_CMD_SHORT(0x15, 0x12, 0x02),
	DSI_CMD_SHORT(0x15, 0x13, 0x27),
	DSI_CMD_SHORT(0x15, 0x14, 0x02),
	DSI_CMD_SHORT(0x15, 0x15, 0x56),
	DSI_CMD_SHORT(0x15, 0x16, 0x02),
	DSI_CMD_SHORT(0x15, 0x17, 0x89),
	DSI_CMD_SHORT(0x15, 0x18, 0x02),
	DSI_CMD_SHORT(0x15, 0x19, 0xAA),
	DSI_CMD_SHORT(0x15, 0x1A, 0x02),
	DSI_CMD_SHORT(0x15, 0x1B, 0xD8),
	DSI_CMD_SHORT(0x15, 0x1C, 0x02),
	DSI_CMD_SHORT(0x15, 0x1D, 0xF8),
	DSI_CMD_SHORT(0x15, 0x1E, 0x03),
	DSI_CMD_SHORT(0x15, 0x1F, 0x21),
	DSI_CMD_SHORT(0x15, 0x20, 0x03),
	DSI_CMD_SHORT(0x15, 0x21, 0x2C),
	DSI_CMD_SHORT(0x15, 0x22, 0x03),
	DSI_CMD_SHORT(0x15, 0x23, 0x3D),
	DSI_CMD_SHORT(0x15, 0x24, 0x03),
	DSI_CMD_SHORT(0x15, 0x25, 0x4A),
	DSI_CMD_SHORT(0x15, 0x26, 0x03),
	DSI_CMD_SHORT(0x15, 0x27, 0x60),
	DSI_CMD_SHORT(0x15, 0x28, 0x03),
	DSI_CMD_SHORT(0x15, 0x29, 0x72),
	DSI_CMD_SHORT(0x15, 0x2A, 0x03),
	DSI_CMD_SHORT(0x15, 0x2B, 0x8F),
	DSI_CMD_SHORT(0x15, 0x2D, 0x03),
	DSI_CMD_SHORT(0x15, 0x2F, 0xA9),
	DSI_CMD_SHORT(0x15, 0x30, 0x03),
	DSI_CMD_SHORT(0x15, 0x31, 0xAC),
	DSI_CMD_SHORT(0x15, 0x32, 0x01),
	DSI_CMD_SHORT(0x15, 0x33, 0x06),
	DSI_CMD_SHORT(0x15, 0x34, 0x01),
	DSI_CMD_SHORT(0x15, 0x35, 0x0D),
	DSI_CMD_SHORT(0x15, 0x36, 0x01),
	DSI_CMD_SHORT(0x15, 0x37, 0x1D),
	DSI_CMD_SHORT(0x15, 0x38, 0x01),
	DSI_CMD_SHORT(0x15, 0x39, 0x2B),
	DSI_CMD_SHORT(0x15, 0x3A, 0x01),
	DSI_CMD_SHORT(0x15, 0x3B, 0x38),
	DSI_CMD_SHORT(0x15, 0x3D, 0x01),
	DSI_CMD_SHORT(0x15, 0x3F, 0x44),
	DSI_CMD_SHORT(0x15, 0x40, 0x01),
	DSI_CMD_SHORT(0x15, 0x41, 0x4F),
	DSI_CMD_SHORT(0x15, 0x42, 0x01),
	DSI_CMD_SHORT(0x15, 0x43, 0x59),
	DSI_CMD_SHORT(0x15, 0x44, 0x01),
	DSI_CMD_SHORT(0x15, 0x45, 0x62),
	DSI_CMD_SHORT(0x15, 0x46, 0x01),
	DSI_CMD_SHORT(0x15, 0x47, 0x84),
	DSI_CMD_SHORT(0x15, 0x48, 0x01),
	DSI_CMD_SHORT(0x15, 0x49, 0xA0),
	DSI_CMD_SHORT(0x15, 0x4A, 0x01),
	DSI_CMD_SHORT(0x15, 0x4B, 0xCD),
	DSI_CMD_SHORT(0x15, 0x4C, 0x01),
	DSI_CMD_SHORT(0x15, 0x4D, 0xF1),
	DSI_CMD_SHORT(0x15, 0x4E, 0x02),
	DSI_CMD_SHORT(0x15, 0x4F, 0x2C),
	DSI_CMD_SHORT(0x15, 0x50, 0x02),
	DSI_CMD_SHORT(0x15, 0x51, 0x60),
	DSI_CMD_SHORT(0x15, 0x52, 0x02),
	DSI_CMD_SHORT(0x15, 0x53, 0x61),
	DSI_CMD_SHORT(0x15, 0x54, 0x02),
	DSI_CMD_SHORT(0x15, 0x55, 0x90),
	DSI_CMD_SHORT(0x15, 0x56, 0x02),
	DSI_CMD_SHORT(0x15, 0x58, 0xC3),
	DSI_CMD_SHORT(0x15, 0x59, 0x02),
	DSI_CMD_SHORT(0x15, 0x5A, 0xE4),
	DSI_CMD_SHORT(0x15, 0x5B, 0x03),
	DSI_CMD_SHORT(0x15, 0x5C, 0x12),
	DSI_CMD_SHORT(0x15, 0x5D, 0x03),
	DSI_CMD_SHORT(0x15, 0x5E, 0x32),
	DSI_CMD_SHORT(0x15, 0x5F, 0x03),
	DSI_CMD_SHORT(0x15, 0x60, 0x5B),
	DSI_CMD_SHORT(0x15, 0x61, 0x03),
	DSI_CMD_SHORT(0x15, 0x62, 0x66),
	DSI_CMD_SHORT(0x15, 0x63, 0x03),
	DSI_CMD_SHORT(0x15, 0x64, 0x77),
	DSI_CMD_SHORT(0x15, 0x65, 0x03),
	DSI_CMD_SHORT(0x15, 0x66, 0x84),
	DSI_CMD_SHORT(0x15, 0x67, 0x03),
	DSI_CMD_SHORT(0x15, 0x68, 0x99),
	DSI_CMD_SHORT(0x15, 0x69, 0x03),
	DSI_CMD_SHORT(0x15, 0x6A, 0xAB),
	DSI_CMD_SHORT(0x15, 0x6B, 0x03),
	DSI_CMD_SHORT(0x15, 0x6C, 0xC9),
	DSI_CMD_SHORT(0x15, 0x6D, 0x03),
	DSI_CMD_SHORT(0x15, 0x6E, 0xE3),
	DSI_CMD_SHORT(0x15, 0x6F, 0x03),
	DSI_CMD_SHORT(0x15, 0x70, 0xE6),
	DSI_CMD_SHORT(0x15, 0x71, 0x00),
	DSI_CMD_SHORT(0x15, 0x72, 0x01),
	DSI_CMD_SHORT(0x15, 0x73, 0x00),
	DSI_CMD_SHORT(0x15, 0x74, 0x30),
	DSI_CMD_SHORT(0x15, 0x75, 0x00),
	DSI_CMD_SHORT(0x15, 0x76, 0x4F),
	DSI_CMD_SHORT(0x15, 0x77, 0x00),
	DSI_CMD_SHORT(0x15, 0x78, 0x68),
	DSI_CMD_SHORT(0x15, 0x79, 0x00),
	DSI_CMD_SHORT(0x15, 0x7A, 0x81),
	DSI_CMD_SHORT(0x15, 0x7B, 0x00),
	DSI_CMD_SHORT(0x15, 0x7C, 0x97),
	DSI_CMD_SHORT(0x15, 0x7D, 0x00),
	DSI_CMD_SHORT(0x15, 0x7E, 0xAB),
	DSI_CMD_SHORT(0x15, 0x7F, 0x00),
	DSI_CMD_SHORT(0x15, 0x80, 0xBC),
	DSI_CMD_SHORT(0x15, 0x81, 0x00),
	DSI_CMD_SHORT(0x15, 0x82, 0xCC),
	DSI_CMD_SHORT(0x15, 0x83, 0x00),
	DSI_CMD_SHORT(0x15, 0x84, 0xFF),
	DSI_CMD_SHORT(0x15, 0x85, 0x01),
	DSI_CMD_SHORT(0x15, 0x86, 0x27),
	DSI_CMD_SHORT(0x15, 0x87, 0x01),
	DSI_CMD_SHORT(0x15, 0x88, 0x66),
	DSI_CMD_SHORT(0x15, 0x89, 0x01),
	DSI_CMD_SHORT(0x15, 0x8A, 0x99),
	DSI_CMD_SHORT(0x15, 0x8B, 0x01),
	DSI_CMD_SHORT(0x15, 0x8C, 0xE6),
	DSI_CMD_SHORT(0x15, 0x8D, 0x02),
	DSI_CMD_SHORT(0x15, 0x8E, 0x1E),
	DSI_CMD_SHORT(0x15, 0x8F, 0x02),
	DSI_CMD_SHORT(0x15, 0x90, 0x1F),
	DSI_CMD_SHORT(0x15, 0x91, 0x02),
	DSI_CMD_SHORT(0x15, 0x92, 0x51),
	DSI_CMD_SHORT(0x15, 0x93, 0x02),
	DSI_CMD_SHORT(0x15, 0x94, 0x85),
	DSI_CMD_SHORT(0x15, 0x95, 0x02),
	DSI_CMD_SHORT(0x15, 0x96, 0xA6),
	DSI_CMD_SHORT(0x15, 0x97, 0x02),
	DSI_CMD_SHORT(0x15, 0x98, 0xD4),
	DSI_CMD_SHORT(0x15, 0x99, 0x02),
	DSI_CMD_SHORT(0x15, 0x9A, 0xF4),
	DSI_CMD_SHORT(0x15, 0x9B, 0x03),
	DSI_CMD_SHORT(0x15, 0x9C, 0x1E),
	DSI_CMD_SHORT(0x15, 0x9D, 0x03),
	DSI_CMD_SHORT(0x15, 0x9E, 0x2A),
	DSI_CMD_SHORT(0x15, 0x9F, 0x03),
	DSI_CMD_SHORT(0x15, 0xA0, 0x39),
	DSI_CMD_SHORT(0x15, 0xA2, 0x03),
	DSI_CMD_SHORT(0x15, 0xA3, 0x48),
	DSI_CMD_SHORT(0x15, 0xA4, 0x03),
	DSI_CMD_SHORT(0x15, 0xA5, 0x5C),
	DSI_CMD_SHORT(0x15, 0xA6, 0x03),
	DSI_CMD_SHORT(0x15, 0xA7, 0x70),
	DSI_CMD_SHORT(0x15, 0xA9, 0x03),
	DSI_CMD_SHORT(0x15, 0xAA, 0x8D),
	DSI_CMD_SHORT(0x15, 0xAB, 0x03),
	DSI_CMD_SHORT(0x15, 0xAC, 0xA9),
	DSI_CMD_SHORT(0x15, 0xAD, 0x03),
	DSI_CMD_SHORT(0x15, 0xAE, 0xAC),
	DSI_CMD_SHORT(0x15, 0xAF, 0x00),
	DSI_CMD_SHORT(0x15, 0xB0, 0x67),
	DSI_CMD_SHORT(0x15, 0xB1, 0x00),
	DSI_CMD_SHORT(0x15, 0xB2, 0x96),
	DSI_CMD_SHORT(0x15, 0xB3, 0x00),
	DSI_CMD_SHORT(0x15, 0xB4, 0xB3),
	DSI_CMD_SHORT(0x15, 0xB5, 0x00),
	DSI_CMD_SHORT(0x15, 0xB6, 0xCC),
	DSI_CMD_SHORT(0x15, 0xB7, 0x00),
	DSI_CMD_SHORT(0x15, 0xB8, 0xE3),
	DSI_CMD_SHORT(0x15, 0xB9, 0x00),
	DSI_CMD_SHORT(0x15, 0xBA, 0xF8),
	DSI_CMD_SHORT(0x15, 0xBB, 0x01),
	DSI_CMD_SHORT(0x15, 0xBC, 0x0B),
	DSI_CMD_SHORT(0x15, 0xBD, 0x01),
	DSI_CMD_SHORT(0x15, 0xBE, 0x1B),
	DSI_CMD_SHORT(0x15, 0xBF, 0x01),
	DSI_CMD_SHORT(0x15, 0xC0, 0x2A),
	DSI_CMD_SHORT(0x15, 0xC1, 0x01),
	DSI_CMD_SHORT(0x15, 0xC2, 0x59),
	DSI_CMD_SHORT(0x15, 0xC3, 0x01),
	DSI_CMD_SHORT(0x15, 0xC4, 0x7D),
	DSI_CMD_SHORT(0x15, 0xC5, 0x01),
	DSI_CMD_SHORT(0x15, 0xC6, 0xB4),
	DSI_CMD_SHORT(0x15, 0xC7, 0x01),
	DSI_CMD_SHORT(0x15, 0xC8, 0xDE),
	DSI_CMD_SHORT(0x15, 0xC9, 0x02),
	DSI_CMD_SHORT(0x15, 0xCA, 0x20),
	DSI_CMD_SHORT(0x15, 0xCB, 0x02),
	DSI_CMD_SHORT(0x15, 0xCC, 0x58),
	DSI_CMD_SHORT(0x15, 0xCD, 0x02),
	DSI_CMD_SHORT(0x15, 0xCE, 0x59),
	DSI_CMD_SHORT(0x15, 0xCF, 0x02),
	DSI_CMD_SHORT(0x15, 0xD0, 0x8B),
	DSI_CMD_SHORT(0x15, 0xD1, 0x02),
	DSI_CMD_SHORT(0x15, 0xD2, 0xBE),
	DSI_CMD_SHORT(0x15, 0xD3, 0x02),
	DSI_CMD_SHORT(0x15, 0xD4, 0xE0),
	DSI_CMD_SHORT(0x15, 0xD5, 0x03),
	DSI_CMD_SHORT(0x15, 0xD6, 0x0E),
	DSI_CMD_SHORT(0x15, 0xD7, 0x03),
	DSI_CMD_SHORT(0x15, 0xD8, 0x2E),
	DSI_CMD_SHORT(0x15, 0xD9, 0x03),
	DSI_CMD_SHORT(0x15, 0xDA, 0x58),
	DSI_CMD_SHORT(0x15, 0xDB, 0x03),
	DSI_CMD_SHORT(0x15, 0xDC, 0x64),
	DSI_CMD_SHORT(0x15, 0xDD, 0x03),
	DSI_CMD_SHORT(0x15, 0xDE, 0x73),
	DSI_CMD_SHORT(0x15, 0xDF, 0x03),
	DSI_CMD_SHORT(0x15, 0xE0, 0x82),
	DSI_CMD_SHORT(0x15, 0xE1, 0x03),
	DSI_CMD_SHORT(0x15, 0xE2, 0x96),
	DSI_CMD_SHORT(0x15, 0xE3, 0x03),
	DSI_CMD_SHORT(0x15, 0xE4, 0xAA),
	DSI_CMD_SHORT(0x15, 0xE5, 0x03),
	DSI_CMD_SHORT(0x15, 0xE6, 0xC7),
	DSI_CMD_SHORT(0x15, 0xE7, 0x03),
	DSI_CMD_SHORT(0x15, 0xE8, 0xE3),
	DSI_CMD_SHORT(0x15, 0xE9, 0x03),
	DSI_CMD_SHORT(0x15, 0xEA, 0xE6),
	DSI_CMD_SHORT(0x15, 0xFF, 0x00),
	DSI_CMD_SHORT(0x15, 0xFE, 0x01),

	DSI_CMD_SHORT(0x15, 0x35, 0x00),

	DSI_CMD_SHORT(0x15, 0xFF, 0x04),
	DSI_CMD_SHORT(0x15, 0x0A, 0x07),
	DSI_CMD_SHORT(0x15, 0x09, 0x20),
	DSI_CMD_SHORT(0x15, 0xFF, 0x00),

	DSI_CMD_SHORT(0x15, 0xFF, 0xEE),
	DSI_CMD_SHORT(0x15, 0x12, 0x50),
	DSI_CMD_SHORT(0x15, 0x13, 0x02),
	DSI_CMD_SHORT(0x15, 0x6A, 0x60),
	DSI_CMD_SHORT(0x15, 0xFB, 0x01),
	DSI_CMD_SHORT(0x15, 0xFF, 0x00),

	DSI_CMD_SHORT(0x05, 0x29, 0x00),
	DSI_DLY_MS(42),
	DSI_CMD_SHORT(0x15, 0xBA, 0x01),

	DSI_CMD_SHORT(0x15, 0x53, 0x2C),
	DSI_CMD_SHORT(0x15, 0x55, 0x83),
	DSI_CMD_SHORT(0x15, 0x5E, 0x06),
};

static struct tegra_dsi_cmd dsi_early_suspend_cmd[] = {
	DSI_CMD_SHORT(0x05, 0x28, 0x00),
	DSI_DLY_MS(20),
	DSI_CMD_SHORT(0x05, 0x34, 0x00),
};

static struct tegra_dsi_cmd dsi_late_resume_cmd[] = {
	DSI_CMD_SHORT(0x15, 0x35, 0x00),
	DSI_CMD_SHORT(0x05, 0x29, 0x00),
	DSI_DLY_MS(40),
};

static struct tegra_dsi_cmd dsi_suspend_cmd[] = {
	DSI_CMD_SHORT(0x05, 0x28, 0x00),
	DSI_DLY_MS(20),
	DSI_CMD_SHORT(0x05, 0x34, 0x00),
	DSI_CMD_SHORT(0x05, 0x10, 0x00),
	DSI_DLY_MS(130),
};

struct tegra_dsi_out endeavor_dsi = {
	.n_data_lanes = 2,
	.pixel_format = TEGRA_DSI_PIXEL_FORMAT_24BIT_P,
	.rated_refresh_rate = 60,
	.refresh_rate = 60,

	.virtual_channel = TEGRA_DSI_VIRTUAL_CHANNEL_0,

	.panel_has_frame_buffer = true,
	.dsi_instance = 0,

	.panel_reset = DSI_PANEL_RESET,
	.power_saving_suspend = true,
	.n_init_cmd = ARRAY_SIZE(dsi_init_sharp_nt_c2_9a_cmd),
	.dsi_init_cmd = dsi_init_sharp_nt_c2_9a_cmd,

	.n_early_suspend_cmd = ARRAY_SIZE(dsi_early_suspend_cmd),
	.dsi_early_suspend_cmd = dsi_early_suspend_cmd,

	.n_late_resume_cmd = ARRAY_SIZE(dsi_late_resume_cmd),
	.dsi_late_resume_cmd = dsi_late_resume_cmd,

	.n_suspend_cmd = ARRAY_SIZE(dsi_suspend_cmd),
	.dsi_suspend_cmd = dsi_suspend_cmd,
	.video_clock_mode = TEGRA_DSI_VIDEO_CLOCK_TX_ONLY,
	.video_data_type = TEGRA_DSI_VIDEO_TYPE_COMMAND_MODE,

	.lp_cmd_mode_freq_khz = 20000,

	/* TODO: Get the vender recommended freq */
	.lp_read_cmd_mode_freq_khz = 200000,
};

static struct tegra_stereo_out endeavor_stereo = {
	.set_mode		= &endeavor_stereo_set_mode,
	.set_orientation	= &endeavor_stereo_set_orientation,
};

#ifdef CONFIG_TEGRA_DC
static struct tegra_dc_mode endeavor_dsi_modes[] = {
	{
#if (DC_CTRL_MODE & TEGRA_DC_OUT_ONE_SHOT_MODE)
		.pclk = 39446000,
#else
		.pclk = 35860000,
#endif
		.h_ref_to_sync = 4,
		.v_ref_to_sync = 1,
		.h_sync_width = 16,
		.v_sync_width = 1,
		.h_back_porch = 29,
		.v_back_porch = 1,
		.h_active = 720,
		.v_active = 1280,
		.h_front_porch = 55,
		.v_front_porch = 2,
	},
};


static struct tegra_fb_data endeavor_dsi_fb_data = {
	.win		= 0,
	.xres		= 720,
	.yres		= 1280,
	.bits_per_pixel	= 32,
	.flags		= TEGRA_FB_FLIP_ON_PROBE,
};

static struct tegra_dc_out endeavor_disp1_out = {
	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,
	.sd_settings	= &endeavor_sd_settings,

	.flags		= DC_CTRL_MODE,

	.type		= TEGRA_DC_OUT_DSI,

	.modes		= endeavor_dsi_modes,
	.n_modes	= ARRAY_SIZE(endeavor_dsi_modes),

	.dsi		= &endeavor_dsi,
	.stereo		= &endeavor_stereo,

	.enable		= endeavor_dsi_panel_enable,
	.disable	= endeavor_dsi_panel_disable,
	.postsuspend	= endeavor_dsi_panel_postsuspend,

	.width		= 53,
	.height		= 95,
	/*TODO let power-on sequence wait until dsi hardware init*/
	.bridge_reset = bridge_reset,
	.ic_reset = ic_reset,

	.power_wakeup = POWER_WAKEUP_ENR,
	.performance_tuning = 1,
	.video_min_bw = 51000000,
};
static struct tegra_dc_platform_data endeavor_disp1_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &endeavor_disp1_out,
	.emc_clk_rate	= 204000000,
	.fb		= &endeavor_dsi_fb_data,
#ifdef CONFIG_TEGRA_DC_CMU
	.cmu_enable	= 1,
#endif
};

static struct nvhost_device endeavor_disp1_device = {
	.name		= "tegradc",
	.id		= 0,
	.resource	= endeavor_disp1_resources,
	.num_resources	= ARRAY_SIZE(endeavor_disp1_resources),
	.dev = {
		.platform_data = &endeavor_disp1_pdata,
	},
};

static int endeavor_disp1_check_fb(struct device *dev, struct fb_info *info)
{
	return info->device == &endeavor_disp1_device.dev;
}

static struct nvhost_device endeavor_disp2_device = {
	.name		= "tegradc",
	.id		= 1,
	.resource	= endeavor_disp2_resources,
	.num_resources	= ARRAY_SIZE(endeavor_disp2_resources),
	.dev = {
		.platform_data = &endeavor_disp2_pdata,
	},
};
#endif

#if defined(CONFIG_TEGRA_NVMAP)
static struct nvmap_platform_carveout endeavor_carveouts[] = {
	[0] = NVMAP_HEAP_CARVEOUT_IRAM_INIT,
	[1] = {
		.name		= "generic-0",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_GENERIC,
		.base		= 0,	/* Filled in by endeavor_panel_init() */
		.size		= 0,	/* Filled in by endeavor_panel_init() */
		.buddy_size	= SZ_32K,
	},
};

static struct nvmap_platform_data endeavor_nvmap_data = {
	.carveouts	= endeavor_carveouts,
	.nr_carveouts	= ARRAY_SIZE(endeavor_carveouts),
};

static struct platform_device endeavor_nvmap_device = {
	.name	= "tegra-nvmap",
	.id	= -1,
	.dev	= {
		.platform_data = &endeavor_nvmap_data,
	},
};
#endif

static struct platform_device *endeavor_gfx_devices[] __initdata = {
#if defined(CONFIG_TEGRA_NVMAP)
	&endeavor_nvmap_device,
#endif
	&tegra_pwfm0_device,
};

static struct platform_device *endeavor_bl_devices[]  = {
	&endeavor_disp1_backlight_device,
};

static void bkl_do_work(struct work_struct *work)
{
	struct backlight_device *bl = platform_get_drvdata(&endeavor_disp1_backlight_device);
	if (bl) {
		backlight_update_status(bl);
	}
}


static void bkl_update(unsigned long data) {
	queue_work(bkl_wq, &bkl_work);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
/* put early_suspend/late_resume handlers here for the display in order
 * to keep the code out of the display driver, keeping it closer to upstream
 */
struct early_suspend endeavor_panel_early_suspender;
#ifdef CONFIG_HTC_ONMODE_CHARGING
struct early_suspend endeavor_panel_onchg_suspender;
#endif

static void endeavor_panel_early_suspend(struct early_suspend *h)
{
	struct backlight_device *bl = platform_get_drvdata(&endeavor_disp1_backlight_device);

	if (bl) {
		del_timer_sync(&bkl_timer);
		flush_workqueue(bkl_wq);
	}

	/* power down LCD, add use a black screen for HDMI */
	if (num_registered_fb > 0)
		fb_blank(registered_fb[0], FB_BLANK_POWERDOWN);
	if (num_registered_fb > 1)
		fb_blank(registered_fb[1], FB_BLANK_NORMAL);
}

static void endeavor_panel_late_resume(struct early_suspend *h)
{
	unsigned i;

	for (i = 0; i < num_registered_fb; i++)
		fb_blank(registered_fb[i], FB_BLANK_UNBLANK);

	mod_timer(&bkl_timer, jiffies + msecs_to_jiffies(50));
}

#ifdef CONFIG_HTC_ONMODE_CHARGING
static void endeavor_panel_onchg_suspend(struct early_suspend *h)
{
	struct backlight_device *bl = platform_get_drvdata(&endeavor_disp1_backlight_device);

	if (bl) {
		del_timer_sync(&bkl_timer);
		flush_workqueue(bkl_wq);
	}

	/* power down LCD */
	if (num_registered_fb > 0)
		fb_blank(registered_fb[0], FB_BLANK_POWERDOWN);

}

static void endeavor_panel_onchg_resume(struct early_suspend *h)
{
	unsigned i;

	fb_blank(registered_fb[0], FB_BLANK_UNBLANK);

	mod_timer(&bkl_timer, jiffies + msecs_to_jiffies(50));

}
#endif /* onmode charge */
#endif /* early suspend */

#define CAB_LOW		1
#define CAB_DEF		2
#define CAB_HIGH	3

static int bkl_calibration_get(void *data, u64 *val)
{
	return 0;
}

static int bkl_calibration_set(void *data, u64 val)
{
	struct backlight_device *bl = platform_get_drvdata(&endeavor_disp1_backlight_device);
	switch (val) {
		case CAB_LOW:
			def_pwm = MAP_PWM_LOW_DEF;
		break;
		case CAB_HIGH:
			def_pwm = MAP_PWM_HIGH_DEF;
		break;
		case CAB_DEF:
		default:
			def_pwm = ORIG_PWM_DEF;
	}
	backlight_update_status(bl);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(bkl_calibration_fops, bkl_calibration_get,
			bkl_calibration_set, "%llu\n");

int __init endeavor_panel_init(void)
{
	int err;
	int i = 0;
	int pin_count;

	struct resource __maybe_unused *res;
	struct board_info board_info;

	err = gpio_request_array(panel_init_gpios, ARRAY_SIZE(panel_init_gpios));
	if (err < 0) {
		pr_err("%s: gpio_request failed %d\n", __func__, err);
		return err;
	}

	pin_count = ARRAY_SIZE(panel_init_gpios);
	for (i = 0; i < pin_count; i++) {
		tegra_gpio_enable(panel_init_gpios[i].gpio);
	}

	bl_output = endeavor_bl_output_measured_a02;
	tegra_get_board_info(&board_info);

#ifdef CONFIG_HAS_EARLYSUSPEND
	endeavor_panel_early_suspender.suspend = endeavor_panel_early_suspend;
	endeavor_panel_early_suspender.resume = endeavor_panel_late_resume;
	endeavor_panel_early_suspender.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	register_early_suspend(&endeavor_panel_early_suspender);

#ifdef CONFIG_HTC_ONMODE_CHARGING
	endeavor_panel_onchg_suspender.suspend = endeavor_panel_onchg_suspend;
	endeavor_panel_onchg_suspender.resume = endeavor_panel_onchg_resume;
	endeavor_panel_onchg_suspender.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	register_onchg_suspend(&endeavor_panel_onchg_suspender);
#endif
#endif

#if defined(CONFIG_TEGRA_NVMAP)
	endeavor_carveouts[1].base = tegra_carveout_start;
	endeavor_carveouts[1].size = tegra_carveout_size;
#endif

#ifdef CONFIG_TEGRA_GRHOST
	err = tegra3_register_host1x_devices();
	if (err)
		return err;
#endif

	err = platform_add_devices(endeavor_gfx_devices,
				ARRAY_SIZE(endeavor_gfx_devices));

#if defined(CONFIG_TEGRA_GRHOST) && defined(CONFIG_TEGRA_DC)
	res = nvhost_get_resource_byname(&endeavor_disp1_device,
					 IORESOURCE_MEM, "fbmem");
	if (res) {
		res->start = tegra_fb_start;
		res->end = tegra_fb_start + tegra_fb_size - 1;
	}
#endif

	/* Copy the bootloader fb to the fb. */
	tegra_move_framebuffer(tegra_fb_start, tegra_bootloader_fb_start,
		min(tegra_fb_size, tegra_bootloader_fb_size));

        /*switch PWM_setting by panel_id*/
        switch (g_panel_id & BKL_CAB_MASK) {
		case BKL_CAB_LOW:
			def_pwm = MAP_PWM_LOW_DEF;
		break;
		case BKL_CAB_HIGH:
			def_pwm = MAP_PWM_HIGH_DEF;
		break;
		case BKL_CAB_OFF:
		case BKL_CAB_DEF:
		default:
			def_pwm = ORIG_PWM_DEF;
        }

	g_panel_id &= ~BKL_CAB_MASK;

		endeavor_dsi.n_init_cmd = ARRAY_SIZE(dsi_init_sharp_nt_c2_9a_cmd);
		endeavor_dsi.dsi_init_cmd = dsi_init_sharp_nt_c2_9a_cmd;

#if defined(CONFIG_TEGRA_GRHOST) && defined(CONFIG_TEGRA_DC)

	if (!err) {
		err = nvhost_device_register(&endeavor_disp1_device);
	}

	res = nvhost_get_resource_byname(&endeavor_disp2_device,
					 IORESOURCE_MEM, "fbmem");
	res->start = tegra_fb2_start;
	res->end = tegra_fb2_start + tegra_fb2_size - 1;
	if (!err) {
		err = nvhost_device_register(&endeavor_disp2_device);
	}
		
#endif

#if defined(CONFIG_TEGRA_GRHOST) && defined(CONFIG_TEGRA_NVAVP)
	if (!err)
		err = nvhost_device_register(&nvavp_device);
#endif

	INIT_WORK(&bkl_work, bkl_do_work);
	bkl_wq = create_workqueue("bkl_wq");
	setup_timer(&bkl_timer, bkl_update, 0);

	if (!err)
		err = platform_add_devices(endeavor_bl_devices,
				ARRAY_SIZE(endeavor_bl_devices));
	return err;
}
