/*
 * omap4panda.c  --  SoC audio for TI OMAP4 Panda
 *
 * Author: Misael Lopez Cruz <misael.lopez@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c/twl.h>
#include <linux/mfd/twl6040-codec.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <plat/hardware.h>
#include <plat/mux.h>
#include <plat/mcbsp.h>

#include "omap-mcpdm.h"
#include "omap-abe.h"
#include "omap-pcm.h"
#include "omap-mcbsp.h"
#include "../codecs/twl6040.h"

#ifdef CONFIG_SND_OMAP_SOC_HDMI
#include "omap-hdmi.h"
#endif

static int twl6040_power_mode;
static int omap4panda_mcpdm_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int clk_id, freq;
	int ret;

	if (twl6040_power_mode) {
		clk_id = TWL6040_SYSCLK_SEL_HPPLL;
		freq = 38400000;
	} else {
		clk_id = TWL6040_SYSCLK_SEL_LPPLL;
		freq = 32768;
	}

	/* set the codec mclk */
	ret = snd_soc_dai_set_sysclk(codec_dai, clk_id, freq,
				SND_SOC_CLOCK_IN);
	if (ret)
		pr_err("can't set codec system clock\n");

	return ret;
}

static struct snd_soc_ops omap4panda_mcpdm_ops = {
	.hw_params = omap4panda_mcpdm_hw_params,
};

static int omap4panda_mcbsp_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int be_id = rtd->dai_link->be_id;
	int ret = 0;

	if (be_id == OMAP_ABE_DAI_BT_VX) {
	        ret = snd_soc_dai_set_fmt(cpu_dai,
                                  SND_SOC_DAIFMT_DSP_B |
                                  SND_SOC_DAIFMT_NB_IF |
                                  SND_SOC_DAIFMT_CBM_CFM);
	} else {
		ret = snd_soc_dai_set_fmt(cpu_dai,
				  SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	}

	if (ret < 0) {
		pr_err("can't set cpu DAI configuration\n");
		return ret;
	}

	/* Set McBSP clock to external */
	ret = snd_soc_dai_set_sysclk(cpu_dai, OMAP_MCBSP_SYSCLK_CLKS_FCLK,
				     64 * params_rate(params),
				     SND_SOC_CLOCK_IN);
	if (ret < 0)
		pr_err("can't set cpu system clock\n");

	return ret;
}

static struct snd_soc_ops omap4panda_mcbsp_ops = {
	.hw_params = omap4panda_mcbsp_hw_params,
};

static int mcbsp_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_interval *channels = hw_param_interval(params,
                                       SNDRV_PCM_HW_PARAM_CHANNELS);
	unsigned int be_id = rtd->dai_link->be_id;
	unsigned int threshold;


	switch (be_id) {
	case OMAP_ABE_DAI_MM_FM:
		channels->min = 2;
		threshold = 2;
		break;
	case OMAP_ABE_DAI_BT_VX:
		channels->min = 1;
		threshold = 1;
		break;
	default:
		threshold = 1;
		break;
	}

	snd_mask_set(&params->masks[SNDRV_PCM_HW_PARAM_FORMAT -
				    SNDRV_PCM_HW_PARAM_FIRST_MASK],
		     SNDRV_PCM_FORMAT_S16_LE);

	omap_mcbsp_set_tx_threshold(cpu_dai->id, threshold);
	omap_mcbsp_set_rx_threshold(cpu_dai->id, threshold);

	return 0;
}

static int omap4panda_get_power_mode(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = twl6040_power_mode;
	return 0;
}

static int omap4panda_set_power_mode(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	if (twl6040_power_mode == ucontrol->value.integer.value[0])
		return 0;

	twl6040_power_mode = ucontrol->value.integer.value[0];

	return 1;
}

static const char *power_texts[] = {"Low-Power", "High-Performance"};

static const struct soc_enum omap4panda_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, power_texts),
};

static const struct snd_kcontrol_new omap4panda_controls[] = {
	SOC_ENUM_EXT("TWL6040 Power Mode", omap4panda_enum[0],
		     omap4panda_get_power_mode, omap4panda_set_power_mode),
};

/* OMAP4 Panda machine DAPM */
static const struct snd_soc_dapm_widget omap4panda_twl6040_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_INPUT("Aux/FM Stereo In"),
	SND_SOC_DAPM_LINE("Aux/FM Stereo Out", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* External Speakers: HFL, HFR (expansion board) */
	{"Ext Spk", NULL, "HFL"},
	{"Ext Spk", NULL, "HFR"},

	/* Auxiliary Output */
	/* TODO: Add aux output in TWL6040 */
	{"Aux/FM Stereo Out", NULL, "AuxL"},
	{"Aux/FM Stereo Out", NULL, "AuxR"},

	/* Headset Mic: HSMIC with bias */
	{"HSMIC", NULL, "Headset Mic Bias"},
	{"Headset Mic Bias", NULL, "Headset Mic"},

	/* Headset Stereophone (Headphone): HSOL, HSOR */
	{"Headset Stereophone", NULL, "HSOL"},
	{"Headset Stereophone", NULL, "HSOR"},

	/* Aux/FM Stereo In: AFML, AFMR */
	{"AFML", NULL, "Aux/FM Stereo In"},
	{"AFMR", NULL, "Aux/FM Stereo In"},
};

static int omap4panda_twl6040_init_hs(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	int ret;

	/* Add OMAP4 Panda specific controls */
	ret = snd_soc_add_controls(codec, omap4panda_controls,
				   ARRAY_SIZE(omap4panda_controls));
	if (ret)
		return ret;

	/* Add OMAP4 Panda specific widgets */
	ret = snd_soc_dapm_new_controls(codec->dapm,
				omap4panda_twl6040_dapm_widgets,
				ARRAY_SIZE(omap4panda_twl6040_dapm_widgets));
	if (ret)
		return ret;

	/* Set up OMAP4 Panda specific audio path audio_map */
	snd_soc_dapm_add_routes(codec->dapm, audio_map, ARRAY_SIZE(audio_map));

	/* OMAP4 Panda connected pins */
	snd_soc_dapm_enable_pin(codec->dapm, "Ext Spk");
	snd_soc_dapm_enable_pin(codec->dapm, "AFML");
	snd_soc_dapm_enable_pin(codec->dapm, "AFMR");
	snd_soc_dapm_enable_pin(codec->dapm, "AuxL");
	snd_soc_dapm_enable_pin(codec->dapm, "AuxR");
	snd_soc_dapm_enable_pin(codec->dapm, "Headset Mic");
	snd_soc_dapm_enable_pin(codec->dapm, "Headset Stereophone");

	/* TWL6040 unused pins */
	snd_soc_dapm_disable_pin(codec->dapm, "MAINMIC");
	snd_soc_dapm_disable_pin(codec->dapm, "SUBMIC");
	snd_soc_dapm_disable_pin(codec->dapm, "EP");

	ret = snd_soc_dapm_sync(codec->dapm);
	if (ret)
		return ret;

	/* wait 500 ms before switching of HS power */
	rtd->pmdown_time = 500;

	return ret;
}

static int omap4panda_twl6040_init_hf(struct snd_soc_pcm_runtime *rtd)
{
	/* wait 500 ms before switching of HF power */
	rtd->pmdown_time = 500;
	return 0;
}

static struct snd_soc_dai_driver dai[] = {
{
	.name = "Bluetooth",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "FM Digital",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
},
{
	.name = "HDMI",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
},
};

static const char *mm1_be[] = {
	OMAP_ABE_BE_PDM_DL1,
	OMAP_ABE_BE_PDM_UL1,
	OMAP_ABE_BE_PDM_DL2,
	OMAP_ABE_BE_BT_VX,
	OMAP_ABE_BE_MM_EXT0,
	OMAP_ABE_BE_DMIC0,
	OMAP_ABE_BE_DMIC1,
	OMAP_ABE_BE_DMIC2,
};

static const char *mm2_be[] = {
	OMAP_ABE_BE_PDM_UL1,
	OMAP_ABE_BE_BT_VX,
	OMAP_ABE_BE_MM_EXT0,
	OMAP_ABE_BE_DMIC0,
	OMAP_ABE_BE_DMIC1,
	OMAP_ABE_BE_DMIC2,
};

static const char *tones_be[] = {
	OMAP_ABE_BE_PDM_DL1,
	OMAP_ABE_BE_PDM_DL2,
	OMAP_ABE_BE_BT_VX,
	OMAP_ABE_BE_MM_EXT0,
};

static const char *mm_lp_be[] = {
	OMAP_ABE_BE_PDM_DL1,
	OMAP_ABE_BE_PDM_DL2,
	OMAP_ABE_BE_BT_VX,
	OMAP_ABE_BE_MM_EXT0,
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link omap4panda_dai[] = {

/*
 * Frontend DAIs - i.e. userspace visible interfaces (ALSA PCMs)
 */

	{
		.name = "OMAP4 Panda Media",
		.stream_name = "Multimedia",

		/* ABE components - MM-UL & MM_DL */
		.cpu_dai_name = "MultiMedia1",
		.platform_name = "omap-pcm-audio",

		.dynamic = 1, /* BE is dynamic */
		.supported_be = mm1_be,
		.num_be = ARRAY_SIZE(mm1_be),
		.fe_playback_channels = 2,
		.fe_capture_channels = 8,
		.no_host_mode = SND_SOC_DAI_LINK_OPT_HOST,
	},
	{
		.name = "OMAP4 Panda Media Capture",
		.stream_name = "Multimedia Capture",

		/* ABE components - MM-UL2 */
		.cpu_dai_name = "MultiMedia2",
		.platform_name = "omap-pcm-audio",

		.dynamic = 1, /* BE is dynamic */
		.supported_be = mm2_be,
		.num_be = ARRAY_SIZE(mm2_be),
		.fe_capture_channels = 2,
	},
	{
		.name = "OMAP4 Panda Voice",
		.stream_name = "Voice",

		/* ABE components - VX-UL & VX-DL */
		.cpu_dai_name = "Voice",
		.platform_name = "omap-pcm-audio",

		.dynamic = 1, /* BE is dynamic */
		.supported_be = mm1_be,
		.num_be = ARRAY_SIZE(mm1_be),
		.fe_playback_channels = 2,
		.fe_capture_channels = 2,
		.no_host_mode = SND_SOC_DAI_LINK_OPT_HOST,
	},
	{
		.name = "OMAP4 Panda Tones Playback",
		.stream_name = "Tone Playback",

		/* ABE components - TONES_DL */
		.cpu_dai_name = "Tones",
		.platform_name = "omap-pcm-audio",

		.dynamic = 1, /* BE is dynamic */
		.supported_be = tones_be,
		.num_be = ARRAY_SIZE(tones_be),
		.fe_playback_channels = 2,
	},
	{
		.name = "OMAP4 Panda Media LP",
		.stream_name = "Multimedia",

		/* ABE components - MM-DL (mmap) */
		.cpu_dai_name = "MultiMedia1 LP",
		.platform_name = "omap-aess-audio",

		.dynamic = 1, /* BE is dynamic */
		.supported_be = mm_lp_be,
		.num_be = ARRAY_SIZE(mm_lp_be),
		.fe_playback_channels = 2,
		.no_host_mode = SND_SOC_DAI_LINK_OPT_HOST,
	},
#ifdef CONFIG_SND_OMAP_SOC_HDMI
	{
		.name = "hdmi",
		.stream_name = "HDMI",

		.cpu_dai_name = "hdmi-dai",
		.platform_name = "omap-pcm-audio",

		/* HDMI*/
		.codec_dai_name = "HDMI",

		.no_codec = 1,
	},
#endif
	{
		.name = "Legacy McBSP",
		.stream_name = "Multimedia",

		/* ABE components - MCBSP2 - MM-EXT */
		.cpu_dai_name = "omap-mcbsp-dai.1",
		.platform_name = "omap-pcm-audio",

		/* FM */
		.codec_dai_name = "FM Digital",

		.no_codec = 1, /* TODO: have a dummy CODEC */
		.ops = &omap4panda_mcbsp_ops,
	},
	{
		.name = "Legacy McPDM",
		.stream_name = "Headset Playback",

		/* ABE components - DL1 */
		.cpu_dai_name = "mcpdm-dl",
		.platform_name = "omap-pcm-audio",

		/* Phoenix - DL1 DAC */
		.codec_dai_name =  "twl6040-dl1",
		.codec_name = "twl6040-codec",

		.ops = &omap4panda_mcpdm_ops,
	},

/*
 * Backend DAIs - i.e. dynamically matched interfaces, invisible to userspace.
 * Matched to above interfaces at runtime, based upon use case.
 */

	{
		.name = OMAP_ABE_BE_PDM_DL1,
		.stream_name = "HS Playback",

		/* ABE components - DL1 */
		.cpu_dai_name = "mcpdm-dl1",
		.platform_name = "omap-aess-audio",

		/* Phoenix - DL1 DAC */
		.codec_dai_name =  "twl6040-dl1",
		.codec_name = "twl6040-codec",

		.no_pcm = 1, /* don't create ALSA pcm for this */
		.init = omap4panda_twl6040_init_hs,
		.ops = &omap4panda_mcpdm_ops,
		.be_id = OMAP_ABE_DAI_PDM_DL1,
	},
	{
		.name = OMAP_ABE_BE_PDM_UL1,
		.stream_name = "Analog Capture",

		/* ABE components - UL1 */
		.cpu_dai_name = "mcpdm-ul1",
		.platform_name = "omap-aess-audio",

		/* Phoenix - UL ADC */
		.codec_dai_name =  "twl6040-ul",
		.codec_name = "twl6040-codec",

		.no_pcm = 1, /* don't create ALSA pcm for this */
		.ops = &omap4panda_mcpdm_ops,
		.be_id = OMAP_ABE_DAI_PDM_UL,
	},
	{
		.name = OMAP_ABE_BE_PDM_DL2,
		.stream_name = "HF Playback",

		/* ABE components - DL2 */
		.cpu_dai_name = "mcpdm-dl2",
		.platform_name = "omap-aess-audio",

		/* Phoenix - DL2 DAC */
		.codec_dai_name =  "twl6040-dl2",
		.codec_name = "twl6040-codec",

		.no_pcm = 1, /* don't create ALSA pcm for this */
		.init = omap4panda_twl6040_init_hf,
		.ops = &omap4panda_mcpdm_ops,
		.be_id = OMAP_ABE_DAI_PDM_DL2,
	},
	{
		.name = OMAP_ABE_BE_BT_VX,
		.stream_name = "BT",

		/* ABE components - MCBSP1 - BT-VX */
		.cpu_dai_name = "omap-mcbsp-dai.0",
		.platform_name = "omap-aess-audio",

		/* Bluetooth */
		.codec_dai_name = "Bluetooth",

		.no_pcm = 1, /* don't create ALSA pcm for this */
		.no_codec = 1, /* TODO: have a dummy CODEC */
		.be_hw_params_fixup = mcbsp_be_hw_params_fixup,
		.ops = &omap4panda_mcbsp_ops,
		.be_id = OMAP_ABE_DAI_BT_VX,
	},
	{
		.name = OMAP_ABE_BE_MM_EXT0,
		.stream_name = "FM",

		/* ABE components - MCBSP2 - MM-EXT */
		.cpu_dai_name = "omap-mcbsp-dai.1",
		.platform_name = "omap-aess-audio",

		/* FM */
		.codec_dai_name = "FM Digital",

		.no_pcm = 1, /* don't create ALSA pcm for this */
		.no_codec = 1, /* TODO: have a dummy CODEC */
		.be_hw_params_fixup = mcbsp_be_hw_params_fixup,
		.ops = &omap4panda_mcbsp_ops,
		.be_id = OMAP_ABE_DAI_MM_FM,
	},
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_omap4panda = {
	.name = "OMAP4 Panda",
	.long_name = "TI OMAP4 Panda Board",
	.dai_link = omap4panda_dai,
	.num_links = ARRAY_SIZE(omap4panda_dai),
};

static struct platform_device *omap4panda_snd_device;

static int __init omap4panda_soc_init(void)
{
	int ret = 0;

	if (!machine_is_omap4_panda()) {
		pr_err("Not Panda board!\n");
		return -ENODEV;
	}
	pr_info("OMA4 Panda SoC init\n");

	omap4panda_snd_device = platform_device_alloc("soc-audio", -1);
	if (!omap4panda_snd_device) {
		pr_err("Platform device allocation failed\n");
		return -ENOMEM;
	}

	snd_soc_register_dais(&omap4panda_snd_device->dev, dai, ARRAY_SIZE(dai));

	platform_set_drvdata(omap4panda_snd_device, &snd_soc_omap4panda);

	ret = platform_device_add(omap4panda_snd_device);
	if (ret)
		goto plat_err;

	twl6040_power_mode = 0;

	return 0;

plat_err:
	platform_device_put(omap4panda_snd_device);
	return ret;
}
module_init(omap4panda_soc_init);

static void __exit omap4panda_soc_exit(void)
{
	platform_device_unregister(omap4panda_snd_device);
}
module_exit(omap4panda_soc_exit);

MODULE_AUTHOR("Misael Lopez Cruz <misael.lopez@ti.com>");
MODULE_DESCRIPTION("ALSA SoC OMAP4 Panda");
MODULE_LICENSE("GPL");

