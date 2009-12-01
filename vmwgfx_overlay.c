/**************************************************************************
 *
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#include "drmP.h"
#include "vmwgfx_drv.h"

#include "ttm/ttm_placement.h"

#include "svga_overlay.h"
#include "svga_escape.h"

#define VMW_MAX_NUM_STREAMS 1

struct vmw_stream
{
	struct vmw_dma_buffer *buf;
	bool paused;
};

/**
 * Overlay control
 */
struct vmw_overlay
{
	/*
	 * Each stream is a single overlay. In Xv these are called ports.
	 */
	struct vmw_stream stream[VMW_MAX_NUM_STREAMS];

};

static inline struct vmw_overlay * vmw_overlay(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	return dev_priv ? dev_priv->overlay_priv : NULL;
}

struct vmw_escape_header {
	uint32_t cmd;
	SVGAFifoCmdEscape body;
};

struct vmw_escape_video_flush {
	struct vmw_escape_header escape;
	SVGAEscapeVideoFlush flush;
};

static inline void fill_escape(struct vmw_escape_header *header,
			       uint32_t size)
{
	header->cmd = SVGA_CMD_ESCAPE;
	header->body.nsid = SVGA_ESCAPE_NSID_VMWARE;
	header->body.size = size;
}

static inline void fill_flush(struct vmw_escape_video_flush *cmd,
			      uint32_t stream_id)
{
	fill_escape(&cmd->escape, sizeof(cmd->flush));
	cmd->flush.cmdType = SVGA_ESCAPE_VMWARE_VIDEO_FLUSH;
	cmd->flush.streamId = stream_id;
}

static int vmw_dmabuf_pin_in_vram(struct vmw_private *dev_priv,
				  struct vmw_dma_buffer *buf,
				  bool pin)
{
	struct ttm_buffer_object *bo = &buf->base;
	unsigned flags = 0;
	int ret;

	ret = ttm_bo_reserve(bo, false, false, false, 0);
	if (unlikely(ret != 0))
		goto err;

	flags |= TTM_PL_FLAG_VRAM;
	flags |= TTM_PL_FLAG_CACHED;
	if (pin)
		flags |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_buffer_object_validate(bo,
					 flags,
					 false,
					 false);

	ttm_bo_unreserve(bo);

err:
	return ret;
}

static int vmw_overlay_send_put(struct vmw_private *dev_priv,
				struct vmw_dma_buffer *buf,
				struct drm_vmw_overlay_arg *arg)
{
	struct {
		struct vmw_escape_header escape;
		struct {
			struct {
				uint32_t cmdType;
				uint32_t streamId;
			} header;
			struct {
				uint32_t registerId;
				uint32_t value;
			} items[SVGA_VIDEO_PITCH_3 + 1];
		} body;
		struct vmw_escape_video_flush flush;
	} *cmds;
	int i;

	cmds = vmw_fifo_reserve(dev_priv, sizeof(*cmds));

	fill_escape(&cmds->escape, sizeof(cmds->body));
	cmds->body.header.cmdType = SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS;
	cmds->body.header.streamId = arg->stream_id;

	for (i = 0; i <= SVGA_VIDEO_PITCH_3; i++) {
		cmds->body.items[i].registerId = i;
	}

	cmds->body.items[SVGA_VIDEO_ENABLED].value     = true;
	cmds->body.items[SVGA_VIDEO_FLAGS].value       = arg->flags;
	cmds->body.items[SVGA_VIDEO_DATA_OFFSET].value = buf->base.offset + arg->offset;
	cmds->body.items[SVGA_VIDEO_FORMAT].value      = arg->format;
	cmds->body.items[SVGA_VIDEO_COLORKEY].value    = arg->color_key;
	cmds->body.items[SVGA_VIDEO_SIZE].value        = arg->size;
	cmds->body.items[SVGA_VIDEO_WIDTH].value       = arg->width;
	cmds->body.items[SVGA_VIDEO_HEIGHT].value      = arg->height;
	cmds->body.items[SVGA_VIDEO_SRC_X].value       = arg->src.x;
	cmds->body.items[SVGA_VIDEO_SRC_Y].value       = arg->src.y;
	cmds->body.items[SVGA_VIDEO_SRC_WIDTH].value   = arg->src.w;
	cmds->body.items[SVGA_VIDEO_SRC_HEIGHT].value  = arg->src.h;
	cmds->body.items[SVGA_VIDEO_DST_X].value       = arg->dst.x;
	cmds->body.items[SVGA_VIDEO_DST_Y].value       = arg->dst.y;
	cmds->body.items[SVGA_VIDEO_DST_WIDTH].value   = arg->dst.w;
	cmds->body.items[SVGA_VIDEO_DST_HEIGHT].value  = arg->dst.h;
	cmds->body.items[SVGA_VIDEO_PITCH_1].value     = arg->pitch[0];
	cmds->body.items[SVGA_VIDEO_PITCH_2].value     = arg->pitch[1];
	cmds->body.items[SVGA_VIDEO_PITCH_3].value     = arg->pitch[2];

	fill_flush(&cmds->flush, arg->stream_id);

	vmw_fifo_commit(dev_priv, sizeof(*cmds));

	return 0;
}

static int vmw_overlay_send_stop(struct vmw_private *dev_priv,
				 uint32_t stream_id)
{
	struct {
		struct vmw_escape_header escape;
		SVGAEscapeVideoSetRegs body;
		struct vmw_escape_video_flush flush;
	} *cmds;

	cmds = vmw_fifo_reserve(dev_priv, sizeof(*cmds));

	fill_escape(&cmds->escape, sizeof(cmds->body));
	cmds->body.header.cmdType = SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS;
	cmds->body.header.streamId = stream_id;
	cmds->body.items[0].registerId = SVGA_VIDEO_ENABLED;
	cmds->body.items[0].value = false;
	fill_flush(&cmds->flush, stream_id);

	vmw_fifo_commit(dev_priv, sizeof(*cmds));
	
	return 0;
}

static int vmw_overlay_stop(struct vmw_private *dev_priv,
			    uint32_t stream_id)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	struct vmw_stream *stream = &overlay->stream[stream_id];
	int ret;

	DRM_INFO("      %s: enter\n", __func__);

	/* no buffer attached the stream is completely stopped */
	if (!stream->buf)
		return 0;

	/* If the stream is paused this is already done */
	if (!stream->paused) {
		ret = vmw_overlay_send_stop(dev_priv, stream_id);
		if (ret)
			return ret;

		ret = vmw_dmabuf_pin_in_vram(dev_priv, stream->buf, false);
		WARN_ON(ret != 0);
	}

	vmw_dmabuf_unreference(&stream->buf);
	stream->paused = false;

	return 0;
}

static int vmw_overlay_update_stream(struct vmw_private *dev_priv,
				     struct vmw_dma_buffer *buf,
				     struct drm_vmw_overlay_arg *arg)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	struct vmw_stream *stream = &overlay->stream[arg->stream_id];
	int ret = 0;

	if (!buf)
		return -EINVAL;

	DRM_DEBUG("   %s: old %p, new %p, %spaused\n", __func__,
		  stream->buf, buf, stream->paused ? "" : "not ");

	/* If the buffers match and not paused then just send the put. */
	if (stream->buf == buf && !stream->paused)
		return vmw_overlay_send_put(dev_priv, buf, arg);

	/* If there is a buffer call stop to completely stop the stream */
	if (stream->buf) {
		ret = vmw_overlay_stop(dev_priv, arg->stream_id);
		if (ret)
			return ret;
	}

	ret = vmw_dmabuf_pin_in_vram(dev_priv, buf, true);
	if (ret)
		return ret;

	ret = vmw_overlay_send_put(dev_priv, buf, arg);
	if (ret) {
		WARN_ON(vmw_dmabuf_pin_in_vram(dev_priv, buf, false) != 0);
	} else {
		stream->buf = vmw_dmabuf_reference(buf);
	}

	return ret;
}

int vmw_overlay_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_vmw_overlay_arg *arg =
	    (struct drm_vmw_overlay_arg *)data;
	struct vmw_dma_buffer *buf;
	int ret;

	DRM_INFO("%s: enter\n", __func__);

	if (arg->stream_id > VMW_MAX_NUM_STREAMS)
		return -EINVAL;

	if (!arg->enabled)
		return vmw_overlay_stop(dev_priv, arg->stream_id);

	ret = vmw_user_dmabuf_lookup(tfile, arg->handle, &buf);
	if (ret)
		return ret;

	ret = vmw_overlay_update_stream(dev_priv, buf, arg);

	vmw_dmabuf_unreference(&buf);

	return ret;
}

int vmw_overlay_init(struct vmw_private *dev_priv)
{
	struct vmw_overlay *overlay;
	int i;

	if (dev_priv->overlay_priv) {
		DRM_INFO("overlay system already on\n");
		return -EINVAL;
	}

	overlay = kmalloc(GFP_KERNEL, sizeof(*overlay));
	if (!overlay)
		return -ENOMEM;

	memset(overlay, 0, sizeof(*overlay));
	for (i = 0; i < VMW_MAX_NUM_STREAMS; i++) {
		overlay->stream[i].buf = NULL;
		overlay->stream[i].paused = false;
	}

	dev_priv->overlay_priv = overlay;

	return 0;
}

int vmw_overlay_close(struct vmw_private *dev_priv)
{
	struct vmw_overlay *overlay = dev_priv->overlay_priv;
	bool forgotten_buffer = false;
	int i;

	if (!overlay)
		return -ENOSYS;

	for (i = 0; i < VMW_MAX_NUM_STREAMS; i++) {
		if (overlay->stream[i].buf) {
			forgotten_buffer = true;
			vmw_overlay_stop(dev_priv, i);
		}
	}

	WARN_ON(forgotten_buffer);

	dev_priv->overlay_priv = NULL;
	kfree(overlay);

	return 0;
}
