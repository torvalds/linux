/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "mod_info_packet.h"
#include "core_types.h"
#include "dc_types.h"
#include "mod_shared.h"
#include "mod_freesync.h"
#include "dc.h"

enum vsc_packet_revision {
	vsc_packet_undefined = 0,
	//01h = VSC SDP supports only 3D stereo.
	vsc_packet_rev1 = 1,
	//02h = 3D stereo + PSR.
	vsc_packet_rev2 = 2,
	//03h = 3D stereo + PSR2.
	vsc_packet_rev3 = 3,
	//04h = 3D stereo + PSR/PSR2 + Y-coordinate.
	vsc_packet_rev4 = 4,
	//05h = 3D stereo + PSR/PSR2 + Y-coordinate + Pixel Encoding/Colorimetry Format
	vsc_packet_rev5 = 5,
};

#define HDMI_INFOFRAME_TYPE_VENDOR 0x81
#define HF_VSIF_VERSION 1

// VTEM Byte Offset
#define VTEM_PB0		0
#define VTEM_PB1		1
#define VTEM_PB2		2
#define VTEM_PB3		3
#define VTEM_PB4		4
#define VTEM_PB5		5
#define VTEM_PB6		6

#define VTEM_MD0		7
#define VTEM_MD1		8
#define VTEM_MD2		9
#define VTEM_MD3		10


// VTEM Byte Masks
//PB0
#define MASK_VTEM_PB0__RESERVED0  0x01
#define MASK_VTEM_PB0__SYNC       0x02
#define MASK_VTEM_PB0__VFR        0x04
#define MASK_VTEM_PB0__AFR        0x08
#define MASK_VTEM_PB0__DS_TYPE    0x30
	//0: Periodic pseudo-static EM Data Set
	//1: Periodic dynamic EM Data Set
	//2: Unique EM Data Set
	//3: Reserved
#define MASK_VTEM_PB0__END        0x40
#define MASK_VTEM_PB0__NEW        0x80

//PB1
#define MASK_VTEM_PB1__RESERVED1 0xFF

//PB2
#define MASK_VTEM_PB2__ORGANIZATION_ID 0xFF
	//0: This is a Vendor Specific EM Data Set
	//1: This EM Data Set is defined by This Specification (HDMI 2.1 r102.clean)
	//2: This EM Data Set is defined by CTA-861-G
	//3: This EM Data Set is defined by VESA
//PB3
#define MASK_VTEM_PB3__DATA_SET_TAG_MSB    0xFF
//PB4
#define MASK_VTEM_PB4__DATA_SET_TAG_LSB    0xFF
//PB5
#define MASK_VTEM_PB5__DATA_SET_LENGTH_MSB 0xFF
//PB6
#define MASK_VTEM_PB6__DATA_SET_LENGTH_LSB 0xFF



//PB7-27 (20 bytes):
//PB7 = MD0
#define MASK_VTEM_MD0__VRR_EN         0x01
#define MASK_VTEM_MD0__M_CONST        0x02
#define MASK_VTEM_MD0__QMS_EN         0x04
#define MASK_VTEM_MD0__RESERVED2      0x08
#define MASK_VTEM_MD0__FVA_FACTOR_M1  0xF0

//MD1
#define MASK_VTEM_MD1__BASE_VFRONT    0xFF

//MD2
#define MASK_VTEM_MD2__BASE_REFRESH_RATE_98  0x03
#define MASK_VTEM_MD2__RB                    0x04
#define MASK_VTEM_MD2__NEXT_TFR              0xF8

//MD3
#define MASK_VTEM_MD3__BASE_REFRESH_RATE_07  0xFF

enum ColorimetryRGBDP {
	ColorimetryRGB_DP_sRGB               = 0,
	ColorimetryRGB_DP_AdobeRGB           = 3,
	ColorimetryRGB_DP_P3                 = 4,
	ColorimetryRGB_DP_CustomColorProfile = 5,
	ColorimetryRGB_DP_ITU_R_BT2020RGB    = 6,
};
enum ColorimetryYCCDP {
	ColorimetryYCC_DP_ITU601        = 0,
	ColorimetryYCC_DP_ITU709        = 1,
	ColorimetryYCC_DP_AdobeYCC      = 5,
	ColorimetryYCC_DP_ITU2020YCC    = 6,
	ColorimetryYCC_DP_ITU2020YCbCr  = 7,
};

void mod_build_vsc_infopacket(const struct dc_stream_state *stream,
		struct dc_info_packet *info_packet,
		enum dc_color_space cs,
		enum color_transfer_func tf)
{
	unsigned int vsc_packet_revision = vsc_packet_undefined;
	unsigned int i;
	unsigned int pixelEncoding = 0;
	unsigned int colorimetryFormat = 0;
	bool stereo3dSupport = false;

	if (stream->timing.timing_3d_format != TIMING_3D_FORMAT_NONE && stream->view_format != VIEW_3D_FORMAT_NONE) {
		vsc_packet_revision = vsc_packet_rev1;
		stereo3dSupport = true;
	}

	/* VSC packet set to 4 for PSR-SU, or 2 for PSR1 */
	if (stream->link->psr_settings.psr_version == DC_PSR_VERSION_SU_1)
		vsc_packet_revision = vsc_packet_rev4;
	else if (stream->link->replay_settings.config.replay_supported)
		vsc_packet_revision = vsc_packet_rev4;
	else if (stream->link->psr_settings.psr_version == DC_PSR_VERSION_1)
		vsc_packet_revision = vsc_packet_rev2;

	/* Update to revision 5 for extended colorimetry support */
	if (stream->use_vsc_sdp_for_colorimetry)
		vsc_packet_revision = vsc_packet_rev5;

	/* VSC packet not needed based on the features
	 * supported by this DP display
	 */
	if (vsc_packet_revision == vsc_packet_undefined)
		return;

	if (vsc_packet_revision == vsc_packet_rev4) {
		/* Secondary-data Packet ID = 0*/
		info_packet->hb0 = 0x00;
		/* 07h - Packet Type Value indicating Video
		 * Stream Configuration packet
		 */
		info_packet->hb1 = 0x07;
		/* 04h = VSC SDP supporting 3D stereo + PSR/PSR2 + Y-coordinate
		 * (applies to eDP v1.4 or higher).
		 */
		info_packet->hb2 = 0x04;
		/* 0Eh = VSC SDP supporting 3D stereo + PSR2
		 * (HB2 = 04h), with Y-coordinate of first scan
		 * line of the SU region
		 */
		info_packet->hb3 = 0x0E;

		for (i = 0; i < 28; i++)
			info_packet->sb[i] = 0;

		info_packet->valid = true;
	}

	if (vsc_packet_revision == vsc_packet_rev2) {
		/* Secondary-data Packet ID = 0*/
		info_packet->hb0 = 0x00;
		/* 07h - Packet Type Value indicating Video
		 * Stream Configuration packet
		 */
		info_packet->hb1 = 0x07;
		/* 02h = VSC SDP supporting 3D stereo and PSR
		 * (applies to eDP v1.3 or higher).
		 */
		info_packet->hb2 = 0x02;
		/* 08h = VSC packet supporting 3D stereo + PSR
		 * (HB2 = 02h).
		 */
		info_packet->hb3 = 0x08;

		for (i = 0; i < 28; i++)
			info_packet->sb[i] = 0;

		info_packet->valid = true;
	}

	if (vsc_packet_revision == vsc_packet_rev1) {

		info_packet->hb0 = 0x00;	// Secondary-data Packet ID = 0
		info_packet->hb1 = 0x07;	// 07h = Packet Type Value indicating Video Stream Configuration packet
		info_packet->hb2 = 0x01;	// 01h = Revision number. VSC SDP supporting 3D stereo only
		info_packet->hb3 = 0x01;	// 01h = VSC SDP supporting 3D stereo only (HB2 = 01h).

		info_packet->valid = true;
	}

	if (stereo3dSupport) {
		/* ==============================================================================================================|
		 * A. STEREO 3D
		 * ==============================================================================================================|
		 * VSC Payload (1 byte) From DP1.2 spec
		 *
		 * Bits 3:0 (Stereo Interface Method Code)  |  Bits 7:4 (Stereo Interface Method Specific Parameter)
		 * -----------------------------------------------------------------------------------------------------
		 * 0 = Non Stereo Video                     |  Must be set to 0x0
		 * -----------------------------------------------------------------------------------------------------
		 * 1 = Frame/Field Sequential               |  0x0: L + R view indication based on MISC1 bit 2:1
		 *                                          |  0x1: Right when Stereo Signal = 1
		 *                                          |  0x2: Left when Stereo Signal = 1
		 *                                          |  (others reserved)
		 * -----------------------------------------------------------------------------------------------------
		 * 2 = Stacked Frame                        |  0x0: Left view is on top and right view on bottom
		 *                                          |  (others reserved)
		 * -----------------------------------------------------------------------------------------------------
		 * 3 = Pixel Interleaved                    |  0x0: horiz interleaved, right view pixels on even lines
		 *                                          |  0x1: horiz interleaved, right view pixels on odd lines
		 *                                          |  0x2: checker board, start with left view pixel
		 *                                          |  0x3: vertical interleaved, start with left view pixels
		 *                                          |  0x4: vertical interleaved, start with right view pixels
		 *                                          |  (others reserved)
		 * -----------------------------------------------------------------------------------------------------
		 * 4 = Side-by-side                         |  0x0: left half represents left eye view
		 *                                          |  0x1: left half represents right eye view
		 */
		switch (stream->timing.timing_3d_format) {
		case TIMING_3D_FORMAT_HW_FRAME_PACKING:
		case TIMING_3D_FORMAT_SW_FRAME_PACKING:
		case TIMING_3D_FORMAT_TOP_AND_BOTTOM:
		case TIMING_3D_FORMAT_TB_SW_PACKED:
			info_packet->sb[0] = 0x02; // Stacked Frame, Left view is on top and right view on bottom.
			break;
		case TIMING_3D_FORMAT_DP_HDMI_INBAND_FA:
		case TIMING_3D_FORMAT_INBAND_FA:
			info_packet->sb[0] = 0x01; // Frame/Field Sequential, L + R view indication based on MISC1 bit 2:1
			break;
		case TIMING_3D_FORMAT_SIDE_BY_SIDE:
		case TIMING_3D_FORMAT_SBS_SW_PACKED:
			info_packet->sb[0] = 0x04; // Side-by-side
			break;
		default:
			info_packet->sb[0] = 0x00; // No Stereo Video, Shall be cleared to 0x0.
			break;
		}

	}

	/* 05h = VSC SDP supporting 3D stereo, PSR2, and Pixel Encoding/Colorimetry Format indication.
	 *   Added in DP1.3, a DP Source device is allowed to indicate the pixel encoding/colorimetry
	 *   format to the DP Sink device with VSC SDP only when the DP Sink device supports it
	 *   (i.e., VSC_SDP_EXTENSION_FOR_COLORIMETRY_SUPPORTED bit in the DPRX_FEATURE_ENUMERATION_LIST
	 *   register (DPCD Address 02210h, bit 3) is set to 1).
	 *   (Requires VSC_SDP_EXTENSION_FOR_COLORIMETRY_SUPPORTED bit set to 1 in DPCD 02210h. This
	 *   DPCD register is exposed in the new Extended Receiver Capability field for DPCD Rev. 1.4
	 *   (and higher). When MISC1. bit 6. is Set to 1, a Source device uses a VSC SDP to indicate
	 *   the Pixel Encoding/Colorimetry Format and that a Sink device must ignore MISC1, bit 7, and
	 *   MISC0, bits 7:1 (MISC1, bit 7. and MISC0, bits 7:1 become "don't care").)
	 */
	if (vsc_packet_revision == vsc_packet_rev5) {
		/* Secondary-data Packet ID = 0 */
		info_packet->hb0 = 0x00;
		/* 07h - Packet Type Value indicating Video Stream Configuration packet */
		info_packet->hb1 = 0x07;
		/* 05h = VSC SDP supporting 3D stereo, PSR2, and Pixel Encoding/Colorimetry Format indication. */
		info_packet->hb2 = 0x05;
		/* 13h = VSC SDP supporting 3D stereo, + PSR2, + Pixel Encoding/Colorimetry Format indication (HB2 = 05h). */
		info_packet->hb3 = 0x13;

		info_packet->valid = true;

		/* Set VSC SDP fields for pixel encoding and colorimetry format from DP 1.3 specs
		 * Data Bytes DB 18~16
		 * Bits 3:0 (Colorimetry Format)        |  Bits 7:4 (Pixel Encoding)
		 * ----------------------------------------------------------------------------------------------------
		 * 0x0 = sRGB                           |  0 = RGB
		 * 0x1 = RGB Wide Gamut Fixed Point
		 * 0x2 = RGB Wide Gamut Floating Point
		 * 0x3 = AdobeRGB
		 * 0x4 = DCI-P3
		 * 0x5 = CustomColorProfile
		 * (others reserved)
		 * ----------------------------------------------------------------------------------------------------
		 * 0x0 = ITU-R BT.601                   |  1 = YCbCr444
		 * 0x1 = ITU-R BT.709
		 * 0x2 = xvYCC601
		 * 0x3 = xvYCC709
		 * 0x4 = sYCC601
		 * 0x5 = AdobeYCC601
		 * 0x6 = ITU-R BT.2020 Y'cC'bcC'rc
		 * 0x7 = ITU-R BT.2020 Y'C'bC'r
		 * (others reserved)
		 * ----------------------------------------------------------------------------------------------------
		 * 0x0 = ITU-R BT.601                   |  2 = YCbCr422
		 * 0x1 = ITU-R BT.709
		 * 0x2 = xvYCC601
		 * 0x3 = xvYCC709
		 * 0x4 = sYCC601
		 * 0x5 = AdobeYCC601
		 * 0x6 = ITU-R BT.2020 Y'cC'bcC'rc
		 * 0x7 = ITU-R BT.2020 Y'C'bC'r
		 * (others reserved)
		 * ----------------------------------------------------------------------------------------------------
		 * 0x0 = ITU-R BT.601                   |  3 = YCbCr420
		 * 0x1 = ITU-R BT.709
		 * 0x2 = xvYCC601
		 * 0x3 = xvYCC709
		 * 0x4 = sYCC601
		 * 0x5 = AdobeYCC601
		 * 0x6 = ITU-R BT.2020 Y'cC'bcC'rc
		 * 0x7 = ITU-R BT.2020 Y'C'bC'r
		 * (others reserved)
		 * ----------------------------------------------------------------------------------------------------
		 * 0x0 =DICOM Part14 Grayscale          |  4 = Yonly
		 * Display Function
		 * (others reserved)
		 */

		/* Set Pixel Encoding */
		switch (stream->timing.pixel_encoding) {
		case PIXEL_ENCODING_RGB:
			pixelEncoding = 0x0;  /* RGB = 0h */
			break;
		case PIXEL_ENCODING_YCBCR444:
			pixelEncoding = 0x1;  /* YCbCr444 = 1h */
			break;
		case PIXEL_ENCODING_YCBCR422:
			pixelEncoding = 0x2;  /* YCbCr422 = 2h */
			break;
		case PIXEL_ENCODING_YCBCR420:
			pixelEncoding = 0x3;  /* YCbCr420 = 3h */
			break;
		default:
			pixelEncoding = 0x0;  /* default RGB = 0h */
			break;
		}

		/* Set Colorimetry format based on pixel encoding */
		switch (stream->timing.pixel_encoding) {
		case PIXEL_ENCODING_RGB:
			if ((cs == COLOR_SPACE_SRGB) ||
					(cs == COLOR_SPACE_SRGB_LIMITED))
				colorimetryFormat = ColorimetryRGB_DP_sRGB;
			else if (cs == COLOR_SPACE_ADOBERGB)
				colorimetryFormat = ColorimetryRGB_DP_AdobeRGB;
			else if ((cs == COLOR_SPACE_2020_RGB_FULLRANGE) ||
					(cs == COLOR_SPACE_2020_RGB_LIMITEDRANGE))
				colorimetryFormat = ColorimetryRGB_DP_ITU_R_BT2020RGB;
			break;

		case PIXEL_ENCODING_YCBCR444:
		case PIXEL_ENCODING_YCBCR422:
		case PIXEL_ENCODING_YCBCR420:
			/* Note: xvYCC probably not supported correctly here on DP since colorspace translation
			 * loses distinction between BT601 vs xvYCC601 in translation
			 */
			if (cs == COLOR_SPACE_YCBCR601)
				colorimetryFormat = ColorimetryYCC_DP_ITU601;
			else if (cs == COLOR_SPACE_YCBCR709)
				colorimetryFormat = ColorimetryYCC_DP_ITU709;
			else if (cs == COLOR_SPACE_ADOBERGB)
				colorimetryFormat = ColorimetryYCC_DP_AdobeYCC;
			else if (cs == COLOR_SPACE_2020_YCBCR_LIMITED)
				colorimetryFormat = ColorimetryYCC_DP_ITU2020YCbCr;

			if (cs == COLOR_SPACE_2020_YCBCR_LIMITED && tf == TRANSFER_FUNC_GAMMA_22)
				colorimetryFormat = ColorimetryYCC_DP_ITU709;
			break;

		default:
			colorimetryFormat = ColorimetryRGB_DP_sRGB;
			break;
		}

		info_packet->sb[16] = (pixelEncoding << 4) | colorimetryFormat;

		/* Set color depth */
		switch (stream->timing.display_color_depth) {
		case COLOR_DEPTH_666:
			/* NOTE: This is actually not valid for YCbCr pixel encoding to have 6 bpc
			 *       as of DP1.4 spec, but value of 0 probably reserved here for potential future use.
			 */
			info_packet->sb[17] = 0;
			break;
		case COLOR_DEPTH_888:
			info_packet->sb[17] = 1;
			break;
		case COLOR_DEPTH_101010:
			info_packet->sb[17] = 2;
			break;
		case COLOR_DEPTH_121212:
			info_packet->sb[17] = 3;
			break;
		/*case COLOR_DEPTH_141414: -- NO SUCH FORMAT IN DP SPEC */
		case COLOR_DEPTH_161616:
			info_packet->sb[17] = 4;
			break;
		default:
			info_packet->sb[17] = 0;
			break;
		}

		/* all YCbCr are always limited range */
		if ((cs == COLOR_SPACE_SRGB_LIMITED) ||
				(cs == COLOR_SPACE_2020_RGB_LIMITEDRANGE) ||
				(pixelEncoding != 0x0)) {
			info_packet->sb[17] |= 0x80; /* DB17 bit 7 set to 1 for CEA timing. */
		}

		/* Content Type (Bits 2:0)
		 *  0 = Not defined.
		 *  1 = Graphics.
		 *  2 = Photo.
		 *  3 = Video.
		 *  4 = Game.
		 */
		info_packet->sb[18] = 0;
	}
}

/**
 *  mod_build_hf_vsif_infopacket - Prepare HDMI Vendor Specific info frame.
 *                                 Follows HDMI Spec to build up Vendor Specific info frame
 *
 *  @stream:      contains data we may need to construct VSIF (i.e. timing_3d_format, etc.)
 *  @info_packet: output structure where to store VSIF
 */
void mod_build_hf_vsif_infopacket(const struct dc_stream_state *stream,
		struct dc_info_packet *info_packet)
{
		unsigned int length = 5;
		bool hdmi_vic_mode = false;
		uint8_t checksum = 0;
		uint32_t i = 0;
		enum dc_timing_3d_format format;

		info_packet->valid = false;
		format = stream->timing.timing_3d_format;
		if (stream->view_format == VIEW_3D_FORMAT_NONE)
			format = TIMING_3D_FORMAT_NONE;

		if (stream->timing.hdmi_vic != 0
				&& stream->timing.h_total >= 3840
				&& stream->timing.v_total >= 2160
				&& format == TIMING_3D_FORMAT_NONE)
			hdmi_vic_mode = true;

		if ((format == TIMING_3D_FORMAT_NONE) && !hdmi_vic_mode)
			return;

		info_packet->sb[1] = 0x03;
		info_packet->sb[2] = 0x0C;
		info_packet->sb[3] = 0x00;

		if (format != TIMING_3D_FORMAT_NONE)
			info_packet->sb[4] = (2 << 5);

		else if (hdmi_vic_mode)
			info_packet->sb[4] = (1 << 5);

		switch (format) {
		case TIMING_3D_FORMAT_HW_FRAME_PACKING:
		case TIMING_3D_FORMAT_SW_FRAME_PACKING:
			info_packet->sb[5] = (0x0 << 4);
			break;

		case TIMING_3D_FORMAT_SIDE_BY_SIDE:
		case TIMING_3D_FORMAT_SBS_SW_PACKED:
			info_packet->sb[5] = (0x8 << 4);
			length = 6;
			break;

		case TIMING_3D_FORMAT_TOP_AND_BOTTOM:
		case TIMING_3D_FORMAT_TB_SW_PACKED:
			info_packet->sb[5] = (0x6 << 4);
			break;

		default:
			break;
		}

		if (hdmi_vic_mode)
			info_packet->sb[5] = stream->timing.hdmi_vic;

		info_packet->hb0 = HDMI_INFOFRAME_TYPE_VENDOR;
		info_packet->hb1 = 0x01;
		info_packet->hb2 = (uint8_t) (length);

		checksum += info_packet->hb0;
		checksum += info_packet->hb1;
		checksum += info_packet->hb2;

		for (i = 1; i <= length; i++)
			checksum += info_packet->sb[i];

		info_packet->sb[0] = (uint8_t) (0x100 - checksum);

		info_packet->valid = true;
}

void mod_build_adaptive_sync_infopacket(const struct dc_stream_state *stream,
		enum adaptive_sync_type asType,
		const struct AS_Df_params *param,
		struct dc_info_packet *info_packet)
{
	info_packet->valid = false;

	memset(info_packet, 0, sizeof(struct dc_info_packet));

	switch (asType) {
	case ADAPTIVE_SYNC_TYPE_DP:
		if (stream != NULL)
			mod_build_adaptive_sync_infopacket_v2(stream, param, info_packet);
		break;
	case FREESYNC_TYPE_PCON_IN_WHITELIST:
	case ADAPTIVE_SYNC_TYPE_EDP:
		mod_build_adaptive_sync_infopacket_v1(info_packet);
		break;
	case ADAPTIVE_SYNC_TYPE_NONE:
	case FREESYNC_TYPE_PCON_NOT_IN_WHITELIST:
	default:
		break;
	}
}

void mod_build_adaptive_sync_infopacket_v1(struct dc_info_packet *info_packet)
{
	info_packet->valid = true;
	// HEADER {HB0, HB1, HB2, HB3} = {00, Type, Version, Length}
	info_packet->hb0 = 0x00;
	info_packet->hb1 = 0x22;
	info_packet->hb2 = AS_SDP_VER_1;
	info_packet->hb3 = 0x00;
}

void mod_build_adaptive_sync_infopacket_v2(const struct dc_stream_state *stream,
		const struct AS_Df_params *param,
		struct dc_info_packet *info_packet)
{
	info_packet->valid = true;
	// HEADER {HB0, HB1, HB2, HB3} = {00, Type, Version, Length}
	info_packet->hb0 = 0x00;
	info_packet->hb1 = 0x22;
	info_packet->hb2 = AS_SDP_VER_2;
	info_packet->hb3 = AS_DP_SDP_LENGTH;

	//Payload
	info_packet->sb[0] = param->supportMode; //1: AVT; 0: FAVT
	info_packet->sb[1] = (stream->timing.v_total & 0x00FF);
	info_packet->sb[2] = (stream->timing.v_total & 0xFF00) >> 8;
	//info_packet->sb[3] = 0x00; Target RR, not use fot AVT
	info_packet->sb[4] = (param->increase.support << 6 | param->decrease.support << 7);
	info_packet->sb[5] = param->increase.frame_duration_hex;
	info_packet->sb[6] = param->decrease.frame_duration_hex;
}

