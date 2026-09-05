/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / JBIG decoder filter, based on JBIG-KIT
 *  (https://www.cl.cam.ac.uk/~mgk25/jbigkit/).
 *
 *  JBIG images are bi-level: the decoded plane is one bit per pixel, 1 meaning
 *  black. It is expanded to 8-bit greyscale here so the rest of the graph does
 *  not have to know about bit packing.
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>
#include <stdlib.h>

#include <jbig.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;
	Bool is_playing;
} GF_JBIGDecCtx;

static GF_Err jbigdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_JBIGDecCtx *ctx = (GF_JBIGDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	if (!ctx->opid)
		ctx->opid = gf_filter_pid_new(filter);

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_VISUAL));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	return GF_OK;
}

static Bool jbigdec_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_JBIGDecCtx *ctx = (GF_JBIGDecCtx *)gf_filter_get_udta(filter);
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		ctx->is_playing = GF_TRUE;
		return GF_FALSE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		return GF_FALSE;
	}
}

static GF_Err jbigdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size, out_size, x, y;
	long width, height;
	size_t consumed = 0;
	int res;
	unsigned char *plane;
	struct jbg_dec_state state;
	GF_JBIGDecCtx *ctx = (GF_JBIGDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);
	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	jbg_dec_init(&state);
	res = jbg_dec_in(&state, (unsigned char *)data, size, &consumed);
	gf_filter_pid_drop_packet(ctx->ipid);

	/* JBG_EOK_INTR means "stopped on a resolution layer", which is a valid
	 * decode as far as this filter is concerned. */
	if ((res != JBG_EOK) && (res != JBG_EOK_INTR))
	{
		jbg_dec_free(&state);
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[JBIGDec] Failed to decode JBIG image: %s\n", jbg_strerror(res, JBG_EN)));
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	width = jbg_dec_getwidth(&state);
	height = jbg_dec_getheight(&state);
	plane = jbg_dec_getimage(&state, 0);
	if ((width <= 0) || (height <= 0) || !plane)
	{
		jbg_dec_free(&state);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	/* Bi-level expanded to RGB rather than to GF_PIXEL_GREYSCALE: writegen has
	 * no adaptation path from a greyscale pid in this build ("No suitable
	 * filter to adapt caps"), so the grey level is replicated on 3 channels. */
	out_size = (u32)width * (u32)height * 3;

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT((u32)width));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT((u32)height));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT((u32)width * 3));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
	if (!dst_pck)
	{
		jbg_dec_free(&state);
		return GF_OUT_OF_MEM;
	}

	{
		u32 stride = ((u32)width + 7) / 8;
		for (y = 0; y < (u32)height; y++)
		{
			for (x = 0; x < (u32)width; x++)
			{
				u32 bit = plane[y * stride + (x >> 3)] >> (7 - (x & 7)) & 1;
				u8 v = bit ? 0 : 255; /* 1 = black */
				u8 *px = output + ((size_t)y * width + x) * 3;
				px[0] = px[1] = px[2] = v;
			}
		}
	}
	jbg_dec_free(&state);

	gf_filter_pck_set_cts(dst_pck, 0);
	gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_set_eos(ctx->opid);
	return GF_EOS;
}

static void jbigdec_finalize(GF_Filter *filter)
{
}

static const GF_FilterCapability JBIGDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "jbg|jbig"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "image/jbig"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister JBIGDecoderRegister = {
	.name = "jbigdec",
	GF_FS_SET_DESCRIPTION("JBIG image decoder")
		GF_FS_SET_HELP("This filter decodes JBIG (ITU-T T.82) bi-level images using JBIG-KIT.")
			.private_size = sizeof(GF_JBIGDecCtx),
	SETCAPS(JBIGDecCaps),
	.configure_pid = jbigdec_configure_pid,
	.process = jbigdec_process,
	.process_event = jbigdec_process_event,
	.finalize = jbigdec_finalize,
};

const GF_FilterRegister *EMSCRIPTEN_KEEPALIVE jbigdec_register(GF_FilterSession *session)
{
	return &JBIGDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_jbigdec(void) {
    gf_filter_auto_register("jbigdec", jbigdec_register);
}
