/* GStreamer
 * Copyright (C) 2017 Ericsson AB. All rights reserved.
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Copyright 2015 The Chromium Authors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *    * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * SECTION:element-nvh265sldec
 * @title: nvh265sldec
 *
 * GstCodecs based NVIDIA H.265 video decoder
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=/path/to/h265/file ! parsebin ! nvh265sldec ! videoconvert ! autovideosink
 * ```
 *
 * Since: 1.18
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/cuda/gstcudautils.h>
#include "gstnvh265dec.h"
#include "gstnvdecoder.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_nv_h265_dec_debug);
#define GST_CAT_DEFAULT gst_nv_h265_dec_debug

typedef struct _GstNvH265Dec
{
  GstH265Decoder parent;

  GstCudaContext *context;
  GstNvDecoder *decoder;
  CUVIDPICPARAMS params;

  /* slice buffer which will be passed to CUVIDPICPARAMS::pBitstreamData */
  guint8 *bitstream_buffer;
  /* allocated memory size of bitstream_buffer */
  gsize bitstream_buffer_alloc_size;
  /* current offset of bitstream_buffer (per frame) */
  gsize bitstream_buffer_offset;

  guint *slice_offsets;
  guint slice_offsets_alloc_len;
  guint num_slices;

  guint width, height;
  guint coded_width, coded_height;
  guint bitdepth;
  guint chroma_format_idc;
} GstNvH265Dec;

typedef struct _GstNvH265DecClass
{
  GstH265DecoderClass parent_class;
  guint cuda_device_id;
} GstNvH265DecClass;

enum
{
  PROP_0,
  PROP_CUDA_DEVICE_ID,
};

static GTypeClass *parent_class = NULL;

#define GST_NV_H265_DEC(object) ((GstNvH265Dec *) (object))
#define GST_NV_H265_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstNvH265DecClass))

static void gst_nv_h265_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_nv_h265_dec_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_nv_h265_dec_open (GstVideoDecoder * decoder);
static gboolean gst_nv_h265_dec_close (GstVideoDecoder * decoder);
static gboolean gst_nv_h265_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_nv_h265_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_nv_h265_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);

/* GstH265Decoder */
static GstFlowReturn gst_nv_h265_dec_new_sequence (GstH265Decoder * decoder,
    const GstH265SPS * sps, gint max_dpb_size);
static GstFlowReturn gst_nv_h265_dec_new_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * frame, GstH265Picture * picture);
static GstFlowReturn gst_nv_h265_dec_output_picture (GstH265Decoder *
    decoder, GstVideoCodecFrame * frame, GstH265Picture * picture);
static GstFlowReturn gst_nv_h265_dec_start_picture (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GstH265Dpb * dpb);
static GstFlowReturn gst_nv_h265_dec_decode_slice (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice,
    GArray * ref_pic_list0, GArray * ref_pic_list1);
static GstFlowReturn gst_nv_h265_dec_end_picture (GstH265Decoder * decoder,
    GstH265Picture * picture);
static guint
gst_nv_h265_dec_get_preferred_output_delay (GstH265Decoder * decoder,
    gboolean live);

static void
gst_nv_h265_dec_class_init (GstNvH265DecClass * klass,
    GstNvDecoderClassData * cdata)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstH265DecoderClass *h265decoder_class = GST_H265_DECODER_CLASS (klass);

  object_class->get_property = gst_nv_h265_dec_get_property;

  /**
   * GstNvH265SLDec:cuda-device-id:
   *
   * Assigned CUDA device id
   *
   * Since: 1.22
   */
  g_object_class_install_property (object_class, PROP_CUDA_DEVICE_ID,
      g_param_spec_uint ("cuda-device-id", "CUDA device id",
          "Assigned CUDA device id", 0, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_nv_h265_dec_set_context);

  parent_class = (GTypeClass *) g_type_class_peek_parent (klass);
  gst_element_class_set_static_metadata (element_class,
      "NVDEC H.265 Stateless Decoder",
      "Codec/Decoder/Video/Hardware",
      "NVIDIA H.265 video decoder", "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_nv_h265_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_nv_h265_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_nv_h265_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_nv_h265_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_nv_h265_dec_src_query);

  h265decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_nv_h265_dec_new_sequence);
  h265decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_nv_h265_dec_new_picture);
  h265decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_nv_h265_dec_output_picture);
  h265decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_nv_h265_dec_start_picture);
  h265decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_nv_h265_dec_decode_slice);
  h265decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_nv_h265_dec_end_picture);
  h265decoder_class->get_preferred_output_delay =
      GST_DEBUG_FUNCPTR (gst_nv_h265_dec_get_preferred_output_delay);

  klass->cuda_device_id = cdata->cuda_device_id;

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_nv_h265_dec_init (GstNvH265Dec * self)
{
}

static void
gst_nv_h265_dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstNvH265DecClass *klass = GST_NV_H265_DEC_GET_CLASS (object);

  switch (prop_id) {
    case PROP_CUDA_DEVICE_ID:
      g_value_set_uint (value, klass->cuda_device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_h265_dec_set_context (GstElement * element, GstContext * context)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (element);
  GstNvH265DecClass *klass = GST_NV_H265_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "set context %s",
      gst_context_get_context_type (context));

  if (gst_cuda_handle_set_context (element, context, klass->cuda_device_id,
          &self->context)) {
    goto done;
  }

  if (self->decoder)
    gst_nv_decoder_handle_set_context (self->decoder, element, context);

done:
  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_nv_h265_dec_open (GstVideoDecoder * decoder)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);
  GstNvH265DecClass *klass = GST_NV_H265_DEC_GET_CLASS (self);

  if (!gst_cuda_ensure_element_context (GST_ELEMENT (self),
          klass->cuda_device_id, &self->context)) {
    GST_ERROR_OBJECT (self, "Required element data is unavailable");
    return FALSE;
  }

  self->decoder = gst_nv_decoder_new (self->context);
  if (!self->decoder) {
    GST_ERROR_OBJECT (self, "Failed to create decoder object");
    gst_clear_object (&self->context);

    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_nv_h265_dec_close (GstVideoDecoder * decoder)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);

  gst_clear_object (&self->decoder);
  gst_clear_object (&self->context);

  g_clear_pointer (&self->bitstream_buffer, g_free);
  g_clear_pointer (&self->slice_offsets, g_free);

  self->bitstream_buffer_alloc_size = 0;
  self->slice_offsets_alloc_len = 0;

  return TRUE;
}

static gboolean
gst_nv_h265_dec_negotiate (GstVideoDecoder * decoder)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);
  GstH265Decoder *h265dec = GST_H265_DECODER (decoder);

  GST_DEBUG_OBJECT (self, "negotiate");

  gst_nv_decoder_negotiate (self->decoder, decoder, h265dec->input_state);

  /* TODO: add support D3D11 memory */

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_nv_h265_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);

  if (!gst_nv_decoder_decide_allocation (self->decoder, decoder, query)) {
    GST_WARNING_OBJECT (self, "Failed to handle decide allocation");
    return FALSE;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_nv_h265_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_cuda_handle_context_query (GST_ELEMENT (decoder), query,
              self->context)) {
        return TRUE;
      } else if (self->decoder &&
          gst_nv_decoder_handle_context_query (self->decoder, decoder, query)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
}

static GstFlowReturn
gst_nv_h265_dec_new_sequence (GstH265Decoder * decoder, const GstH265SPS * sps,
    gint max_dpb_size)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);
  gint crop_width, crop_height;
  gboolean modified = FALSE;

  GST_LOG_OBJECT (self, "new sequence");

  if (sps->conformance_window_flag) {
    crop_width = sps->crop_rect_width;
    crop_height = sps->crop_rect_height;
  } else {
    crop_width = sps->width;
    crop_height = sps->height;
  }

  if (self->width != crop_width || self->height != crop_height ||
      self->coded_width != sps->width || self->coded_height != sps->height) {
    GST_INFO_OBJECT (self, "resolution changed %dx%d (%dx%d)",
        crop_width, crop_height, sps->width, sps->height);
    self->width = crop_width;
    self->height = crop_height;
    self->coded_width = sps->width;
    self->coded_height = sps->height;
    modified = TRUE;
  }

  if (self->bitdepth != sps->bit_depth_luma_minus8 + 8) {
    GST_INFO_OBJECT (self, "bitdepth changed");
    self->bitdepth = sps->bit_depth_luma_minus8 + 8;
    modified = TRUE;
  }

  if (self->chroma_format_idc != sps->chroma_format_idc) {
    GST_INFO_OBJECT (self, "chroma format changed");
    self->chroma_format_idc = sps->chroma_format_idc;
    modified = TRUE;
  }

  if (modified || !gst_nv_decoder_is_configured (self->decoder)) {
    GstVideoInfo info;
    GstVideoFormat out_format = GST_VIDEO_FORMAT_UNKNOWN;

    if (self->bitdepth == 8) {
      if (self->chroma_format_idc == 1) {
        out_format = GST_VIDEO_FORMAT_NV12;
      } else if (self->chroma_format_idc == 3) {
        out_format = GST_VIDEO_FORMAT_Y444;
      } else {
        GST_FIXME_OBJECT (self, "8 bits supports only 4:2:0 or 4:4:4 format");
      }
    } else if (self->bitdepth == 10) {
      if (self->chroma_format_idc == 1) {
        out_format = GST_VIDEO_FORMAT_P010_10LE;
      } else if (self->chroma_format_idc == 3) {
        out_format = GST_VIDEO_FORMAT_Y444_16LE;
      } else {
        GST_FIXME_OBJECT (self, "10 bits supports only 4:2:0 or 4:4:4 format");
      }
    } else if (self->bitdepth == 12 || self->bitdepth == 16) {
      if (self->chroma_format_idc == 1) {
        out_format = GST_VIDEO_FORMAT_P016_LE;
      } else if (self->chroma_format_idc == 3) {
        out_format = GST_VIDEO_FORMAT_Y444_16LE;
      } else {
        GST_FIXME_OBJECT (self, "%d bits supports only 4:2:0 or 4:4:4 format",
            self->bitdepth);
      }
    }

    if (out_format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_ERROR_OBJECT (self, "Could not support bitdepth/chroma format");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    gst_video_info_set_format (&info, out_format, self->width, self->height);

    if (!gst_nv_decoder_configure (self->decoder,
            cudaVideoCodec_HEVC, &info, self->coded_width, self->coded_height,
            self->bitdepth, max_dpb_size, FALSE)) {
      GST_ERROR_OBJECT (self, "Failed to configure decoder");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    memset (&self->params, 0, sizeof (CUVIDPICPARAMS));
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_h265_dec_new_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * cframe, GstH265Picture * picture)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);
  GstNvDecoderFrame *frame;

  frame = gst_nv_decoder_new_frame (self->decoder);
  if (!frame) {
    GST_ERROR_OBJECT (self, "No available decoder frame");
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self, "New decoder frame %p (index %d)", frame, frame->index);

  gst_h265_picture_set_user_data (picture,
      frame, (GDestroyNotify) gst_nv_decoder_frame_unref);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_h265_dec_output_picture (GstH265Decoder * decoder,
    GstVideoCodecFrame * frame, GstH265Picture * picture)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstNvDecoderFrame *decoder_frame;

  GST_LOG_OBJECT (self,
      "Outputting picture %p (poc %d)", picture, picture->pic_order_cnt);

  decoder_frame =
      (GstNvDecoderFrame *) gst_h265_picture_get_user_data (picture);
  if (!decoder_frame) {
    GST_ERROR_OBJECT (self, "No decoder frame in picture %p", picture);
    goto error;
  }

  if (!gst_nv_decoder_finish_frame (self->decoder, vdec, picture->discont_state,
          decoder_frame, &frame->output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to handle output picture");
    goto error;
  }

  gst_h265_picture_unref (picture);

  return gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);

error:
  gst_video_decoder_drop_frame (vdec, frame);
  gst_h265_picture_unref (picture);

  return GST_FLOW_ERROR;
}

static GstNvDecoderFrame *
gst_nv_h265_dec_get_decoder_frame_from_picture (GstNvH265Dec * self,
    GstH265Picture * picture)
{
  GstNvDecoderFrame *frame;

  frame = (GstNvDecoderFrame *) gst_h265_picture_get_user_data (picture);

  if (!frame)
    GST_DEBUG_OBJECT (self, "current picture does not have decoder frame");

  return frame;
}

static void
gst_nv_h265_dec_picture_params_from_sps (GstNvH265Dec * self,
    const GstH265SPS * sps, CUVIDHEVCPICPARAMS * params)
{
#define COPY_FIELD(f) \
  (params)->f = (sps)->f
#define COPY_FIELD_WITH_PREFIX(f) \
  (params)->G_PASTE(sps_,f) = (sps)->f
#define COPY_FIELD_EXTENSION(f) \
  (params)->f = (sps)->sps_extension_params.f

  params->pic_width_in_luma_samples = sps->width;
  params->pic_height_in_luma_samples = sps->height;
  COPY_FIELD (log2_min_luma_coding_block_size_minus3);
  COPY_FIELD (log2_diff_max_min_luma_coding_block_size);
  COPY_FIELD (log2_min_transform_block_size_minus2);
  COPY_FIELD (log2_diff_max_min_transform_block_size);
  COPY_FIELD (pcm_enabled_flag);
  COPY_FIELD (log2_min_pcm_luma_coding_block_size_minus3);
  COPY_FIELD (log2_diff_max_min_pcm_luma_coding_block_size);
  COPY_FIELD (pcm_sample_bit_depth_luma_minus1);
  COPY_FIELD (pcm_sample_bit_depth_chroma_minus1);
  COPY_FIELD (pcm_loop_filter_disabled_flag);
  COPY_FIELD (strong_intra_smoothing_enabled_flag);
  COPY_FIELD (max_transform_hierarchy_depth_intra);
  COPY_FIELD (max_transform_hierarchy_depth_inter);
  COPY_FIELD (max_transform_hierarchy_depth_inter);
  COPY_FIELD (amp_enabled_flag);
  COPY_FIELD (separate_colour_plane_flag);
  COPY_FIELD (log2_max_pic_order_cnt_lsb_minus4);
  COPY_FIELD (num_short_term_ref_pic_sets);
  COPY_FIELD (long_term_ref_pics_present_flag);
  COPY_FIELD (num_long_term_ref_pics_sps);
  COPY_FIELD_WITH_PREFIX (temporal_mvp_enabled_flag);
  COPY_FIELD (sample_adaptive_offset_enabled_flag);

  params->scaling_list_enable_flag = sps->scaling_list_enabled_flag;

  COPY_FIELD (bit_depth_luma_minus8);
  COPY_FIELD (bit_depth_chroma_minus8);

  /* Extension fields */
  COPY_FIELD (sps_range_extension_flag);
  if (sps->sps_range_extension_flag) {
    COPY_FIELD_EXTENSION (high_precision_offsets_enabled_flag);
    COPY_FIELD_EXTENSION (transform_skip_rotation_enabled_flag);
    COPY_FIELD_EXTENSION (implicit_rdpcm_enabled_flag);
    COPY_FIELD_EXTENSION (explicit_rdpcm_enabled_flag);
    COPY_FIELD_EXTENSION (extended_precision_processing_flag);
    COPY_FIELD_EXTENSION (intra_smoothing_disabled_flag);
    COPY_FIELD_EXTENSION (persistent_rice_adaptation_enabled_flag);
    COPY_FIELD_EXTENSION (cabac_bypass_alignment_enabled_flag);
  }
#undef COPY_FIELD
#undef COPY_FIELD_WITH_PREFIX
#undef COPY_FIELD_EXTENSION
}

static gboolean
gst_nv_h265_dec_picture_params_from_pps (GstNvH265Dec * self,
    const GstH265PPS * pps, CUVIDHEVCPICPARAMS * params)
{
  gint i;

#define COPY_FIELD(f) \
  (params)->f = (pps)->f
#define COPY_FIELD_WITH_PREFIX(f) \
  (params)->G_PASTE(pps_,f) = (pps)->f
#define COPY_FIELD_EXTENSION(f) \
  (params)->f = (pps)->pps_extension_params.f

  COPY_FIELD (dependent_slice_segments_enabled_flag);
  COPY_FIELD (slice_segment_header_extension_present_flag);
  COPY_FIELD (sign_data_hiding_enabled_flag);
  COPY_FIELD (cu_qp_delta_enabled_flag);
  COPY_FIELD (diff_cu_qp_delta_depth);
  COPY_FIELD (init_qp_minus26);
  COPY_FIELD_WITH_PREFIX (cb_qp_offset);
  COPY_FIELD_WITH_PREFIX (cr_qp_offset);
  COPY_FIELD (constrained_intra_pred_flag);
  COPY_FIELD (weighted_pred_flag);
  COPY_FIELD (weighted_bipred_flag);
  COPY_FIELD (transform_skip_enabled_flag);
  COPY_FIELD (transquant_bypass_enabled_flag);
  COPY_FIELD (entropy_coding_sync_enabled_flag);
  COPY_FIELD (log2_parallel_merge_level_minus2);
  COPY_FIELD (num_extra_slice_header_bits);
  COPY_FIELD (loop_filter_across_tiles_enabled_flag);
  COPY_FIELD (loop_filter_across_slices_enabled_flag);
  COPY_FIELD (output_flag_present_flag);
  COPY_FIELD (num_ref_idx_l0_default_active_minus1);
  COPY_FIELD (num_ref_idx_l1_default_active_minus1);
  COPY_FIELD (lists_modification_present_flag);
  COPY_FIELD (cabac_init_present_flag);
  COPY_FIELD_WITH_PREFIX (slice_chroma_qp_offsets_present_flag);
  COPY_FIELD (deblocking_filter_override_enabled_flag);
  COPY_FIELD_WITH_PREFIX (deblocking_filter_disabled_flag);
  COPY_FIELD_WITH_PREFIX (beta_offset_div2);
  COPY_FIELD_WITH_PREFIX (tc_offset_div2);
  COPY_FIELD (tiles_enabled_flag);
  COPY_FIELD (uniform_spacing_flag);

  if (pps->tiles_enabled_flag) {
    guint num_tile_columns;
    guint num_tile_rows;

    COPY_FIELD (num_tile_columns_minus1);
    COPY_FIELD (num_tile_rows_minus1);

    if (pps->num_tile_columns_minus1 >
        G_N_ELEMENTS (params->column_width_minus1)) {
      GST_ERROR_OBJECT (self,
          "Too large column_width_minus1 %d", pps->num_tile_columns_minus1);
      return FALSE;
    }

    if (pps->num_tile_rows_minus1 > G_N_ELEMENTS (params->row_height_minus1)) {
      GST_ERROR_OBJECT (self,
          "Too large num_tile_rows_minus1 %d", pps->num_tile_rows_minus1);
      return FALSE;
    }

    /* XXX: The size of column_width_minus1 array in CUVIDHEVCPICPARAMS struct
     * is 21 which is inconsistent with the spec.
     * Just copy values as many as possible */
    num_tile_columns = MIN (pps->num_tile_columns_minus1,
        G_N_ELEMENTS (pps->column_width_minus1));
    num_tile_rows = MIN (pps->num_tile_rows_minus1,
        G_N_ELEMENTS (pps->row_height_minus1));

    for (i = 0; i < num_tile_columns; i++)
      COPY_FIELD (column_width_minus1[i]);

    for (i = 0; i < num_tile_rows; i++)
      COPY_FIELD (row_height_minus1[i]);
  }

  COPY_FIELD (pps_range_extension_flag);
  if (pps->pps_range_extension_flag) {
    COPY_FIELD_EXTENSION (cross_component_prediction_enabled_flag);
    COPY_FIELD_EXTENSION (chroma_qp_offset_list_enabled_flag);
    COPY_FIELD_EXTENSION (diff_cu_chroma_qp_offset_depth);
    COPY_FIELD_EXTENSION (chroma_qp_offset_list_len_minus1);
    for (i = 0; i < G_N_ELEMENTS (params->cb_qp_offset_list); i++)
      COPY_FIELD_EXTENSION (cb_qp_offset_list[i]);
    for (i = 0; i < G_N_ELEMENTS (params->cr_qp_offset_list); i++)
      COPY_FIELD_EXTENSION (cr_qp_offset_list[i]);
    COPY_FIELD_EXTENSION (log2_sao_offset_scale_luma);
    COPY_FIELD_EXTENSION (log2_sao_offset_scale_chroma);
  }
#undef COPY_FIELD
#undef COPY_FIELD_WITH_PREFIX
#undef COPY_FIELD_EXTENSION

  return TRUE;
}

static void
gst_nv_h265_dec_reset_bitstream_params (GstNvH265Dec * self)
{
  self->bitstream_buffer_offset = 0;
  self->num_slices = 0;

  self->params.nBitstreamDataLen = 0;
  self->params.pBitstreamData = NULL;
  self->params.nNumSlices = 0;
  self->params.pSliceDataOffsets = NULL;
}

static GstFlowReturn
gst_nv_h265_dec_start_picture (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice, GstH265Dpb * dpb)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);
  CUVIDPICPARAMS *params = &self->params;
  CUVIDHEVCPICPARAMS *h265_params = &params->CodecSpecific.hevc;
  const GstH265SliceHdr *slice_header = &slice->header;
  const GstH265SPS *sps;
  const GstH265PPS *pps;
  GstNvDecoderFrame *frame;
  GArray *dpb_array;
  gint num_ref_pic;
  gint i, j, k;
  const GstH265ScalingList *scaling_list = NULL;

  /* both NVDEC and h265parser are using the same order */
  G_STATIC_ASSERT (sizeof (scaling_list->scaling_lists_4x4) ==
      sizeof (h265_params->ScalingList4x4));
  G_STATIC_ASSERT (sizeof (scaling_list->scaling_lists_8x8) ==
      sizeof (h265_params->ScalingList8x8));
  G_STATIC_ASSERT (sizeof (scaling_list->scaling_lists_16x16) ==
      sizeof (h265_params->ScalingList16x16));
  G_STATIC_ASSERT (sizeof (scaling_list->scaling_lists_32x32) ==
      sizeof (h265_params->ScalingList32x32));

  g_return_val_if_fail (slice_header->pps != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (slice_header->pps->sps != NULL, GST_FLOW_ERROR);

  frame = gst_nv_h265_dec_get_decoder_frame_from_picture (self, picture);

  if (!frame) {
    GST_ERROR_OBJECT (self,
        "Couldn't get decoder frame frame picture %p", picture);
    return GST_FLOW_ERROR;
  }

  gst_nv_h265_dec_reset_bitstream_params (self);

  pps = slice_header->pps;
  sps = pps->sps;

  /* FIXME: update sps/pps related params only when it's required */
  params->PicWidthInMbs = sps->pic_width_in_luma_samples / 16;
  params->FrameHeightInMbs = sps->pic_height_in_luma_samples / 16;
  params->CurrPicIdx = frame->index;

  /* nBitstreamDataLen, pBitstreamData, nNumSlices and pSliceDataOffsets
   * will be set later */
  params->ref_pic_flag = picture->ref;
  params->intra_pic_flag = GST_H265_IS_NAL_TYPE_IRAP (slice->nalu.type);

  h265_params->IrapPicFlag = GST_H265_IS_NAL_TYPE_IRAP (slice->nalu.type);
  h265_params->IdrPicFlag = GST_H265_IS_NAL_TYPE_IDR (slice->nalu.type);

  gst_nv_h265_dec_picture_params_from_sps (self, sps, h265_params);
  if (!gst_nv_h265_dec_picture_params_from_pps (self, pps, h265_params)) {
    GST_ERROR_OBJECT (self, "Couldn't copy pps");
    return GST_FLOW_ERROR;
  }

  /* Fill reference */
  if (decoder->NumPocStCurrBefore >
      G_N_ELEMENTS (h265_params->RefPicSetStCurrBefore)) {
    GST_ERROR_OBJECT (self, "Too many RefPicSetStCurrBefore");
    return GST_FLOW_ERROR;
  }

  if (decoder->NumPocStCurrAfter >
      G_N_ELEMENTS (h265_params->RefPicSetStCurrAfter)) {
    GST_ERROR_OBJECT (self, "Too many RefPicSetStCurrAfter");
    return GST_FLOW_ERROR;
  }

  if (decoder->NumPocLtCurr > G_N_ELEMENTS (h265_params->RefPicSetLtCurr)) {
    GST_ERROR_OBJECT (self, "Too many RefPicSetLtCurr");
    return GST_FLOW_ERROR;
  }

  /* Fill ref list */
  h265_params->NumBitsForShortTermRPSInSlice =
      slice_header->short_term_ref_pic_set_size;
  h265_params->NumDeltaPocsOfRefRpsIdx =
      slice_header->short_term_ref_pic_sets.NumDeltaPocsOfRefRpsIdx;
  h265_params->NumPocTotalCurr = decoder->NumPicTotalCurr;
  h265_params->NumPocStCurrBefore = decoder->NumPocStCurrBefore;
  h265_params->NumPocStCurrAfter = decoder->NumPocStCurrAfter;
  h265_params->NumPocLtCurr = decoder->NumPocLtCurr;
  h265_params->CurrPicOrderCntVal = picture->pic_order_cnt;

  dpb_array = gst_h265_dpb_get_pictures_all (dpb);
  /* count only referenced frame */
  num_ref_pic = 0;
  for (i = 0; i < dpb_array->len; i++) {
    GstH265Picture *other = g_array_index (dpb_array, GstH265Picture *, i);
    GstNvDecoderFrame *other_frame;
    gint picture_index = -1;

    if (!other->ref)
      continue;

    if (num_ref_pic >= G_N_ELEMENTS (h265_params->RefPicIdx)) {
      GST_ERROR_OBJECT (self, "Too many reference frames");
      return GST_FLOW_ERROR;
    }

    other_frame = gst_nv_h265_dec_get_decoder_frame_from_picture (self, other);
    if (other_frame)
      picture_index = other_frame->index;

    h265_params->RefPicIdx[num_ref_pic] = picture_index;
    h265_params->PicOrderCntVal[num_ref_pic] = other->pic_order_cnt;
    h265_params->IsLongTerm[num_ref_pic] = other->long_term;

    num_ref_pic++;
  }
  g_array_unref (dpb_array);

  for (i = 0, j = 0; i < num_ref_pic; i++) {
    GstH265Picture *other = NULL;

    while (!other && j < decoder->NumPocStCurrBefore)
      other = decoder->RefPicSetStCurrBefore[j++];

    if (other) {
      for (k = 0; k < num_ref_pic; k++) {
        if (h265_params->PicOrderCntVal[k] == other->pic_order_cnt) {
          h265_params->RefPicSetStCurrBefore[i] = k;
          break;
        }
      }
    }
  }

  for (i = 0, j = 0; i < num_ref_pic; i++) {
    GstH265Picture *other = NULL;

    while (!other && j < decoder->NumPocStCurrAfter)
      other = decoder->RefPicSetStCurrAfter[j++];

    if (other) {
      for (k = 0; k < num_ref_pic; k++) {
        if (h265_params->PicOrderCntVal[k] == other->pic_order_cnt) {
          h265_params->RefPicSetStCurrAfter[i] = k;
          break;
        }
      }
    }
  }

  for (i = 0, j = 0; i < num_ref_pic; i++) {
    GstH265Picture *other = NULL;

    while (!other && j < decoder->NumPocLtCurr)
      other = decoder->RefPicSetLtCurr[j++];

    if (other) {
      for (k = 0; k < num_ref_pic; k++) {
        if (h265_params->PicOrderCntVal[k] == other->pic_order_cnt) {
          h265_params->RefPicSetLtCurr[i] = k;
          break;
        }
      }
    }
  }

  /* Fill scaling list */
  if (pps->scaling_list_data_present_flag ||
      (sps->scaling_list_enabled_flag
          && !sps->scaling_list_data_present_flag)) {
    scaling_list = &pps->scaling_list;
  } else {
    scaling_list = &sps->scaling_list;
  }

  memcpy (h265_params->ScalingList4x4, scaling_list->scaling_lists_4x4,
      sizeof (scaling_list->scaling_lists_4x4));
  memcpy (h265_params->ScalingList8x8, scaling_list->scaling_lists_8x8,
      sizeof (scaling_list->scaling_lists_8x8));
  memcpy (h265_params->ScalingList16x16, scaling_list->scaling_lists_16x16,
      sizeof (scaling_list->scaling_lists_16x16));
  memcpy (h265_params->ScalingList32x32, scaling_list->scaling_lists_32x32,
      sizeof (scaling_list->scaling_lists_32x32));

  for (i = 0; i < G_N_ELEMENTS (h265_params->ScalingListDCCoeff16x16); i++) {
    h265_params->ScalingListDCCoeff16x16[i] =
        scaling_list->scaling_list_dc_coef_minus8_16x16[i] + 8;
  }

  for (i = 0; i < G_N_ELEMENTS (h265_params->ScalingListDCCoeff32x32); i++) {
    h265_params->ScalingListDCCoeff32x32[i] =
        scaling_list->scaling_list_dc_coef_minus8_32x32[i] + 8;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_h265_dec_decode_slice (GstH265Decoder * decoder,
    GstH265Picture * picture, GstH265Slice * slice,
    GArray * ref_pic_list0, GArray * ref_pic_list1)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);
  gsize new_size;

  GST_LOG_OBJECT (self, "Decode slice, nalu size %u", slice->nalu.size);

  if (self->slice_offsets_alloc_len < self->num_slices + 1) {
    self->slice_offsets_alloc_len = 2 * (self->num_slices + 1);

    self->slice_offsets = (guint *) g_realloc_n (self->slice_offsets,
        self->slice_offsets_alloc_len, sizeof (guint));
  }
  self->slice_offsets[self->num_slices] = self->bitstream_buffer_offset;
  GST_LOG_OBJECT (self, "Slice offset %u for slice %d",
      self->slice_offsets[self->num_slices], self->num_slices);

  self->num_slices++;

  new_size = self->bitstream_buffer_offset + slice->nalu.size + 3;
  if (self->bitstream_buffer_alloc_size < new_size) {
    self->bitstream_buffer_alloc_size = 2 * new_size;

    self->bitstream_buffer = (guint8 *) g_realloc (self->bitstream_buffer,
        self->bitstream_buffer_alloc_size);
  }

  self->bitstream_buffer[self->bitstream_buffer_offset] = 0;
  self->bitstream_buffer[self->bitstream_buffer_offset + 1] = 0;
  self->bitstream_buffer[self->bitstream_buffer_offset + 2] = 1;

  memcpy (self->bitstream_buffer + self->bitstream_buffer_offset + 3,
      slice->nalu.data + slice->nalu.offset, slice->nalu.size);
  self->bitstream_buffer_offset = new_size;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_h265_dec_end_picture (GstH265Decoder * decoder, GstH265Picture * picture)
{
  GstNvH265Dec *self = GST_NV_H265_DEC (decoder);
  gboolean ret;
  CUVIDPICPARAMS *params = &self->params;

  params->nBitstreamDataLen = self->bitstream_buffer_offset;
  params->pBitstreamData = self->bitstream_buffer;
  params->nNumSlices = self->num_slices;
  params->pSliceDataOffsets = self->slice_offsets;

  GST_LOG_OBJECT (self, "End picture, bitstream len: %" G_GSIZE_FORMAT
      ", num slices %d", self->bitstream_buffer_offset, self->num_slices);

  ret = gst_nv_decoder_decode_picture (self->decoder, &self->params);

  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to decode picture");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static guint
gst_nv_h265_dec_get_preferred_output_delay (GstH265Decoder * decoder,
    gboolean live)
{
  /* Prefer to zero latency for live pipeline */
  if (live)
    return 0;

  /* NVCODEC SDK uses 4 frame delay for better throughput performance */
  return 4;
}

void
gst_nv_h265_dec_register (GstPlugin * plugin, guint device_id, guint rank,
    GstCaps * sink_caps, GstCaps * src_caps, gboolean is_primary)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  GstNvDecoderClassData *cdata;
  gint index = 0;
  GValue value_list = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
  GTypeInfo type_info = {
    sizeof (GstNvH265DecClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_nv_h265_dec_class_init,
    NULL,
    NULL,
    sizeof (GstNvH265Dec),
    0,
    (GInstanceInitFunc) gst_nv_h265_dec_init,
  };

  GST_DEBUG_CATEGORY_INIT (gst_nv_h265_dec_debug, "nvh265dec", 0, "nvh265dec");

  cdata = g_new0 (GstNvDecoderClassData, 1);
  cdata->sink_caps = gst_caps_copy (sink_caps);

  /* Update stream-format since we support packetized format as well */
  g_value_init (&value_list, GST_TYPE_LIST);
  g_value_init (&value, G_TYPE_STRING);

  g_value_set_static_string (&value, "hev1");
  gst_value_list_append_value (&value_list, &value);

  g_value_set_static_string (&value, "hvc1");
  gst_value_list_append_value (&value_list, &value);

  g_value_set_static_string (&value, "byte-stream");
  gst_value_list_append_value (&value_list, &value);

  gst_caps_set_value (cdata->sink_caps, "stream-format", &value_list);
  g_value_unset (&value);
  g_value_unset (&value_list);

  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  cdata->src_caps = gst_caps_ref (src_caps);
  cdata->cuda_device_id = device_id;

  if (is_primary) {
    type_name = g_strdup ("GstNvH265Dec");
    feature_name = g_strdup ("nvh265dec");
  } else {
    type_name = g_strdup ("GstNvH265SLDec");
    feature_name = g_strdup ("nvh265sldec");
  }

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    if (is_primary) {
      type_name = g_strdup_printf ("GstNvH265Device%dDec", index);
      feature_name = g_strdup_printf ("nvh265device%ddec", index);
    } else {
      type_name = g_strdup_printf ("GstNvH265SLDevice%dDec", index);
      feature_name = g_strdup_printf ("nvh265sldevice%ddec", index);
    }
  }

  type_info.class_data = cdata;
  type = g_type_register_static (GST_TYPE_H265_DECODER,
      type_name, &type_info, 0);

  /* make lower rank than default device */
  if (rank > 0 && index > 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
