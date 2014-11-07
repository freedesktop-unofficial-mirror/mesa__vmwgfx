/******************************************************************************
 *
 * Copyright © 2014 VMware, Inc., Palo Alto, CA., USA
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
 ******************************************************************************/

#include "vmwgfx_kms.h"


#define vmw_crtc_to_stdu(x) \
	container_of(x, struct vmw_screen_target_display_unit, base.crtc)
#define vmw_encoder_to_stdu(x) \
	container_of(x, struct vmw_screen_target_display_unit, base.encoder)
#define vmw_connector_to_stdu(x) \
	container_of(x, struct vmw_screen_target_display_unit, base.connector)



/**
 *  struct vmw_screen_target_display - screen target display unit metadata
 *
 *  TBD for when a bounce buffer is implemented
 */
struct vmw_screen_target_display {
	/* Once bounce buffer is implemented, its definition will come here.
	 * The current code cannot support all of the KMS features, e.g.
	 * panning, extended desktop.  To get that to work, we need to allocate
	 * a large bounce buffer and blit the appropriate content to the
	 * screen target(s)
	 */
};


/**
 * struct vmw_screen_target_display_unit
 *
 * @base: VMW specific DU structure
 * @defined:  true if the current display unit has been initialized
 */
struct vmw_screen_target_display_unit {
	struct vmw_display_unit base;

	bool defined;                  /* true if defined */
};



static void vmw_stdu_destroy(struct vmw_screen_target_display_unit *stdu);



/******************************************************************************
 * Screen Target Display Unit CRTC Functions
 *****************************************************************************/


static void vmw_stdu_crtc_destroy(struct drm_crtc *crtc)
{
	vmw_stdu_destroy(vmw_crtc_to_stdu(crtc));
}



/**
 * vmw_stdu_define_st - Defines a Screen Target
 *
 * @dev_priv:  VMW DRM device
 * @stdu: display unit to create a Screen Target for
 * @x: X offset for the screen target in the display topology
 * @y: Y offset for the screen target in the display topology
 * @mode:  mode parameters
 *
 * Creates a STDU that we can used later.  This function is called whenever the
 * framebuffer size changes.
 *
 * RETURNs:
 * 0 on success, error code on failure
 */
static int vmw_stdu_define_st(struct vmw_private *dev_priv,
			      struct vmw_screen_target_display_unit *stdu,
			      uint32_t x, uint32_t y,
			      struct drm_display_mode *mode)
{
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineGBScreenTarget body;
	} *cmd;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Out of FIFO space defining Screen Target\n");
		return -ENOMEM;
	}

	cmd->header.id   = SVGA_3D_CMD_DEFINE_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;
	cmd->body.width  = mode->hdisplay;
	cmd->body.height = mode->vdisplay;
	cmd->body.xRoot  = x;
	cmd->body.yRoot  = y;
	cmd->body.flags  = (0 == cmd->body.stid) ? SVGA_STFLAG_PRIMARY : 0;
	cmd->body.dpi    = 0;

	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	stdu->defined = true;

	return 0;
}



/**
 * vmw_stdu_bind_st - Binds a surface to a Screen Target
 *
 * @dev_priv: VMW DRM device
 * @stdu: display unit affected
 * @fb: Buffer to bind to the screen target.  Set to NULL to blank screen.
 *
 * Binding a surface to a Screen Target the same as flipping
 */
static int vmw_stdu_bind_st(struct vmw_private *dev_priv,
			    struct vmw_screen_target_display_unit *stdu,
			    struct drm_framebuffer *fb)
{
	struct vmw_surface *surface = NULL;
	SVGA3dSurfaceImageId image;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdBindGBScreenTarget body;
	} *cmd;


	if (!stdu->defined) {
		DRM_ERROR("No screen target defined\n");
		return -EINVAL;
	}

	if (fb)
		surface = (vmw_framebuffer_to_vfbs(fb))->surface;

	/* Set up image using information in vfb */
	memset(&image, 0, sizeof(image));
	image.sid = surface ? surface->res.id : SVGA3D_INVALID_ID;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Out of FIFO space binding a screen target\n");
		return -ENOMEM;
	}

	cmd->header.id   = SVGA_3D_CMD_BIND_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;
	cmd->body.image  = image;

	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;
}



/**
 * vmw_stdu_update_st - Updates a Screen Target
 *
 * @dev_priv: VMW DRM device
 * @stdu: display unit affected
 * @update_area: area that needs to be updated
 *
 * This function needs to be called whenever the content of a screen
 * target changes.
 *
 * RETURNS:
 * 0 on success, error code on failure
 */
static int vmw_stdu_update_st(struct vmw_private *dev_priv,
			      struct vmw_screen_target_display_unit *stdu,
			      struct drm_clip_rect *update_area)
{
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdUpdateGBScreenTarget body;
	} *cmd;


	if (!stdu->defined) {
		DRM_ERROR("No screen target defined");
		return -EINVAL;
	}

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Out of FIFO space updating a Screen Target\n");
		return -ENOMEM;
	}

	cmd->header.id   = SVGA_3D_CMD_UPDATE_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;
	cmd->body.rect.x = update_area->x1;
	cmd->body.rect.y = update_area->y1;
	cmd->body.rect.w = update_area->x2 - update_area->x1;
	cmd->body.rect.h = update_area->y2 - update_area->y1;

	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;
}



/**
 * vmw_stdu_destroy_st - Destroy a Screen Target
 *
 * @dev_priv:  VMW DRM device
 * @stdu: display unit to destroy
 */
static int vmw_stdu_destroy_st(struct vmw_private *dev_priv,
			       struct vmw_screen_target_display_unit *stdu)
{
	int    ret;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroyGBScreenTarget body;
	} *cmd;


	/* Nothing to do if not successfully defined */
	if (unlikely(!stdu->defined))
		return 0;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Out of FIFO space, screen target not destroyed\n");
		return -ENOMEM;
	}

	cmd->header.id   = SVGA_3D_CMD_DESTROY_GB_SCREENTARGET;
	cmd->header.size = sizeof(cmd->body);

	cmd->body.stid   = stdu->base.unit;

	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	/* Force sync */
	ret = vmw_fallback_wait(dev_priv, false, true, 0, false, 3*HZ);
	if (unlikely(ret != 0))
		DRM_ERROR("Failed to sync with HW");

	stdu->defined = false;

	return ret;
}



/**
 * vmw_stdu_crtc_set_config - Sets a mode
 *
 * @set:  mode parameters
 *
 * This function is the device-specific portion of the DRM CRTC mode set.
 * For the SVGA device, we do this by defining a Screen Target, binding a
 * GB Surface to that target, and finally update the screen target.
 *
 * RETURNS:
 * 0 on success, error code otherwise
 */
static int vmw_stdu_crtc_set_config(struct drm_mode_set *set)
{
	struct vmw_private *dev_priv;
	struct vmw_screen_target_display_unit *stdu;
	struct vmw_framebuffer_surface *new_vfbs, *cur_vfbs;
	struct drm_display_mode *mode;
	struct drm_framebuffer  *new_fb;
	struct drm_crtc      *crtc;
	struct drm_encoder   *encoder;
	struct drm_connector *connector;
	struct drm_clip_rect update_area = {0};
	int    ret;


	if (!set || !set->crtc)
		return -EINVAL;

	crtc     = set->crtc;
	stdu     = vmw_crtc_to_stdu(crtc);
	mode     = set->mode;
	new_fb   = set->fb;
	new_vfbs = new_fb ? vmw_framebuffer_to_vfbs(new_fb) : NULL;
	cur_vfbs = crtc->fb ? vmw_framebuffer_to_vfbs(crtc->fb) : NULL;
	dev_priv = vmw_priv(crtc->dev);


	if (new_vfbs && new_vfbs->base.dmabuf) {
		DRM_ERROR("DMA Buffer cannot be used with Screen Targets\n");
		return -EINVAL;
	}

	if (set->num_connectors > 1) {
		DRM_ERROR("Too many connectors\n");
		return -EINVAL;
	}

	if (set->num_connectors == 1 &&
	    set->connectors[0] != &stdu->base.connector) {
		DRM_ERROR("Connectors don't match %p %p\n",
			set->connectors[0], &stdu->base.connector);
		return -EINVAL;
	}

	/* Since they always map one to one these are safe */
	connector = &stdu->base.connector;
	encoder   = &stdu->base.encoder;

	/* After this point the CRTC will be considered off unless a new fb
	 * is bound
	 */
	if (stdu->defined) {
		/* Unbind the current surface by binding an invalid one */
		ret = vmw_stdu_bind_st(dev_priv, stdu, NULL);
		if (unlikely(ret != 0))
			return ret;

		/* Update the Screen Target, the display will now be blank */
		if (crtc->fb) {
			update_area.x2 = crtc->fb->width;
			update_area.y2 = crtc->fb->height;

			ret = vmw_stdu_update_st(dev_priv, stdu, &update_area);
			if (unlikely(ret != 0))
				return ret;
		}
	}

	crtc->fb           = NULL;
	crtc->x            = 0;
	crtc->y            = 0;
	crtc->enabled      = false;
	encoder->crtc      = NULL;
	connector->encoder = NULL;

	/* Unbind the current fb, if any */
	if (cur_vfbs) {
		vmw_resource_unpin(&cur_vfbs->surface->res);

		ret = vmw_stdu_destroy_st(dev_priv, stdu);
		/* The hardware is hung, give up */
		if (unlikely(ret != 0))
			return ret;
	}


	/* Any of these conditions means the caller wants CRTC to be off */
	if (set->num_connectors == 0 || !mode || !new_vfbs)
		return 0;


	if (set->x + mode->hdisplay > new_fb->width ||
	    set->y + mode->vdisplay > new_fb->height) {
		DRM_ERROR("Set outside of framebuffer\n");
		return -EINVAL;
	}


	/* Pin the buffer. This defines and binds the MOB and GB Surface */
	ret = vmw_resource_pin(&new_vfbs->surface->res);
	if (unlikely(ret != 0))
		goto err;


	/* Steps to displaying a surface, assume surface is already bound:
	 *   1.  define a screen target
	 *   2.  bind a fb to the screen target
	 *   3.  update that screen target
	 */
	ret = vmw_stdu_define_st(dev_priv, stdu, set->x, set->y, mode);
	if (unlikely(ret != 0))
		goto err_unpin;

	ret = vmw_stdu_bind_st(dev_priv, stdu, new_fb);
	if (unlikely(ret != 0))
		goto err_unpin_destroy_st;

	update_area.x2 = new_fb->width;
	update_area.y2 = new_fb->height;

	ret = vmw_stdu_update_st(dev_priv, stdu, &update_area);
	if (unlikely(ret != 0))
		goto err_unpin_destroy_st;

	connector->encoder = encoder;
	encoder->crtc      = crtc;
	crtc->mode    = *mode;
	crtc->fb      = new_fb;
	crtc->x       = set->x;
	crtc->y       = set->y;
	crtc->enabled = true;

	return ret;

err_unpin_destroy_st:
	vmw_stdu_destroy_st(dev_priv, stdu);
err_unpin:
	vmw_resource_unpin(&new_vfbs->surface->res);
err:
	return ret;
}



/**
 * vmw_stdu_crtc_page_flip - Binds a buffer to a screen target
 *
 * @crtc: CRTC to attach FB to
 * @fb: FB to attach
 * @event: Event to be posted. This event should've been alloced
 *         using k[mz]alloc, and should've been completely initialized.
 *
 * This function sends a bind command to the SVGA device, binding the FB
 * specified in the parameter to the screen target associated with the CRTC.
 * The binding command has the same effect as a page flip.
 *
 * RETURNS:
 * 0 on success, error code on failure
 */
static int vmw_stdu_crtc_page_flip(struct drm_crtc *crtc,
				   struct drm_framebuffer *new_fb,
				   struct drm_pending_vblank_event *event)
{
	struct vmw_private *dev_priv = vmw_priv(crtc->dev);
	struct vmw_screen_target_display_unit *stdu;
	struct vmw_framebuffer_surface *new_vfbs;
	struct drm_framebuffer *old_fb;
	struct drm_file *file_priv = NULL;
	struct vmw_fence_obj *fence = NULL;
	struct drm_clip_rect update_area = {0};
	int ret;


	if (crtc == NULL)
		return -EINVAL;

	vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
	if (!fence)
		return -EINVAL;


	dev_priv = vmw_priv(crtc->dev);
	stdu     = vmw_crtc_to_stdu(crtc);
	old_fb   = crtc->fb;
	crtc->fb = new_fb;
	new_vfbs = vmw_framebuffer_to_vfbs(new_fb);

	if (new_fb) {
		update_area.x2 = new_fb->width;
		update_area.y2 = new_fb->height;
	}

	if (event)
		file_priv = event->base.file_priv;

	if (stdu->defined) {
		/* Unbind the current surface */
		ret = vmw_stdu_bind_st(dev_priv, stdu, NULL);
		if (unlikely(ret != 0))
			goto err_unref_fence;
	}

	/* Unpin the current FB, if any */
	if (old_fb) {
		struct vmw_framebuffer_surface *current_vfbs;

		current_vfbs = vmw_framebuffer_to_vfbs(old_fb);

		vmw_resource_unpin(&current_vfbs->surface->res);
	}

	ret = vmw_resource_pin(&new_vfbs->surface->res);
	if (unlikely(ret != 0))
		goto err_unref_fence;

	ret = vmw_stdu_bind_st(dev_priv, stdu, new_fb);
	if (unlikely(ret != 0))
		goto err_unref_fence;

	ret = vmw_stdu_update_st(dev_priv, stdu, &update_area);
	if (unlikely(ret != 0))
		goto err_unref_fence;

	ret = vmw_event_fence_action_queue(file_priv, fence,
					   &event->base,
					   &event->event.tv_sec,
					   &event->event.tv_usec,
					   true);

err_unref_fence:
	vmw_fence_obj_unreference(&fence);

	return ret;
}



/*
 *  Screen Target CRTC dispatch table
 */
static struct drm_crtc_funcs vmw_stdu_crtc_funcs = {
	.save = vmw_du_crtc_save,
	.restore = vmw_du_crtc_restore,
	.cursor_set = vmw_du_crtc_cursor_set,
	.cursor_move = vmw_du_crtc_cursor_move,
	.gamma_set = vmw_du_crtc_gamma_set,
	.destroy = vmw_stdu_crtc_destroy,
	.set_config = vmw_stdu_crtc_set_config,
	.page_flip = vmw_stdu_crtc_page_flip,
};



/******************************************************************************
 * Screen Target Display Unit Encoder Functions
 *****************************************************************************/

/**
 * vmw_stdu_encoder_destroy - This is a no op for vmwgfx
 *
 * @encoder: encoder to destroy
 *
 * This is a no op in our case because the proper clean up will be done by
 * vmw_stdu_crtc_destroy
 */
static void vmw_stdu_encoder_destroy(struct drm_encoder *encoder)
{
	vmw_stdu_destroy(vmw_encoder_to_stdu(encoder));
}

static struct drm_encoder_funcs vmw_stdu_encoder_funcs = {
	.destroy = vmw_stdu_encoder_destroy,
};



/******************************************************************************
 * Screen Target Display Unit Connector Functions
 *****************************************************************************/

/**
 * vmw_stdu_connector_destroy - This is a no op for vmwgfx
 *
 * @connector: connector to destroy
 *
 * This is a no op in our case because the proper clean up will be done by
 * vmw_stdu_crtc_destroy
 */
static void vmw_stdu_connector_destroy(struct drm_connector *connector)
{
	vmw_stdu_destroy(vmw_connector_to_stdu(connector));
}

static struct drm_connector_funcs vmw_stdu_connector_funcs = {
	.dpms = vmw_du_connector_dpms,
	.save = vmw_du_connector_save,
	.restore = vmw_du_connector_restore,
	.detect = vmw_du_connector_detect,
	.fill_modes = vmw_du_connector_fill_modes,
	.set_property = vmw_du_connector_set_property,
	.destroy = vmw_stdu_connector_destroy,
};



/**
 * vmw_stdu_init - Sets up a Screen Target Display Unit
 *
 * @dev_priv: VMW DRM device
 * @unit: unit number range from 0 to VMWGFX_NUM_DISPLAY_UNITS
 *
 * This function is called once per CRTC, and allocates one Screen Target
 * display unit to represent that CRTC.  Since the SVGA device does not separate
 * out encoder and connector, they are represented as part of the STDU as well.
 */
static int vmw_stdu_init(struct vmw_private *dev_priv, unsigned unit)
{
	struct vmw_screen_target_display_unit *stdu;
	struct drm_device *dev = dev_priv->dev;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;


	stdu = kzalloc(sizeof(*stdu), GFP_KERNEL);
	if (!stdu)
		return -ENOMEM;

	stdu->base.unit = unit;
	crtc = &stdu->base.crtc;
	encoder = &stdu->base.encoder;
	connector = &stdu->base.connector;

	stdu->base.pref_active = (unit == 0);
	stdu->base.pref_width  = dev_priv->initial_width;
	stdu->base.pref_height = dev_priv->initial_height;
	stdu->base.pref_mode   = NULL;
	stdu->base.is_implicit = true;

	drm_connector_init(dev, connector, &vmw_stdu_connector_funcs,
			   DRM_MODE_CONNECTOR_VIRTUAL);
	connector->status = vmw_du_connector_detect(connector, false);

	drm_encoder_init(dev, encoder, &vmw_stdu_encoder_funcs,
			 DRM_MODE_ENCODER_VIRTUAL);
	drm_mode_connector_attach_encoder(connector, encoder);
	encoder->possible_crtcs = (1 << unit);
	encoder->possible_clones = 0;

	drm_crtc_init(dev, crtc, &vmw_stdu_crtc_funcs);

	drm_mode_crtc_set_gamma_size(crtc, 256);

	drm_connector_attach_property(connector,
				      dev->mode_config.dirty_info_property,
				      1);

	return 0;
}



/**
 *  vmw_stdu_destroy - Cleans up a vmw_screen_target_display_unit
 *
 *  @stdu:  Screen Target Display Unit to be destroyed
 *
 *  Clean up after vmw_stdu_init
 */
static void vmw_stdu_destroy(struct vmw_screen_target_display_unit *stdu)
{
	vmw_du_cleanup(&stdu->base);
	kfree(stdu);
}



/******************************************************************************
 * Screen Target Display KMS Functions
 *
 * These functions are called by the common KMS code in vmwgfx_kms.c
 *****************************************************************************/

/**
 * vmw_kms_stdu_init_display - Initializes a Screen Target based display
 *
 * @dev_priv: VMW DRM device
 *
 * This function initialize a Screen Target based display device.  It checks
 * the capability bits to make sure the underlying hardware can support
 * screen targets, and then creates the maximum number of CRTCs, a.k.a Display
 * Units, as supported by the display hardware.
 *
 * RETURNS:
 * 0 on success, error code otherwise
 */
int vmw_kms_stdu_init_display(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	int i, ret;


	/* Do nothing if Screen Target support is turned off */
	if (!VMWGFX_ENABLE_SCREEN_TARGET_OTABLE)
		return -ENOSYS;

	if (dev_priv->stdu_priv) {
		DRM_INFO("Screen Target Display device already enabled\n");
		return -EINVAL;
	}

	if (!(dev_priv->capabilities & SVGA_CAP_GBOBJECTS)) {
		DRM_INFO("Hardware cannot support Screen Target\n");
		return -ENOSYS;
	}

	dev_priv->stdu_priv = kmalloc(sizeof(*dev_priv->stdu_priv),
				      GFP_KERNEL);

	if (unlikely(dev_priv->stdu_priv == NULL))
		return -ENOMEM;


	ret = drm_vblank_init(dev, VMWGFX_NUM_DISPLAY_UNITS);
	if (unlikely(ret != 0))
		goto err_free;

	ret = drm_mode_create_dirty_info_property(dev);
	if (unlikely(ret != 0))
		goto err_vblank_cleanup;

	for (i = 0; i < VMWGFX_NUM_DISPLAY_UNITS; ++i)
		vmw_stdu_init(dev_priv, i);

	dev_priv->active_display_unit = vmw_du_screen_target;

	DRM_INFO("Screen Target Display device initialized\n");

	return 0;

err_vblank_cleanup:
	drm_vblank_cleanup(dev);
err_free:
	kfree(dev_priv->stdu_priv);
	dev_priv->stdu_priv = NULL;
	return ret;
}



/**
 * vmw_kms_stdu_close_display - Cleans up after vmw_kms_stdu_init_display
 *
 * @dev_priv: VMW DRM device
 *
 * Frees up any resources allocated by vmw_kms_stdu_init_display
 *
 * RETURNS:
 * 0 on success
 */
int vmw_kms_stdu_close_display(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;


	if (!dev_priv->stdu_priv)
		return -ENOSYS;

	drm_vblank_cleanup(dev);

	kfree(dev_priv->stdu_priv);
	dev_priv->stdu_priv = NULL;


	return 0;
}



/**
 * vmw_kms_stdu_do_surface_dirty - updates a dirty rectange to SVGA device
 *
 * @dev_priv: VMW DRM device
 * @framebuffer: FB with the new content to be copied to SVGA device
 * @clip_rects: array of dirty rectanges
 * @num_of_clip_rects: number of rectanges in @clips
 * @increment: increment to the next dirty rect in @clips
 *
 * This function sends an Update command to the SVGA device.  This will notify
 * the device that a region needs to be copied to the screen.  At this time
 * we are not coalescing clip rects into one large clip rect because the SVGA
 * device will do it for us.
 *
 * RETURNS:
 * 0 on success, error code otherwise
 */
int vmw_kms_stdu_do_surface_dirty(struct vmw_private *dev_priv,
				  struct drm_file *file_priv,
				  struct vmw_framebuffer *framebuffer,
				  struct drm_clip_rect *clip_rects,
				  unsigned num_of_clip_rects, int increment)
{
	struct vmw_screen_target_display_unit *stdu[VMWGFX_NUM_DISPLAY_UNITS];
	struct drm_clip_rect *cur_rect;
	struct drm_crtc *crtc;

	unsigned num_of_du = 0, cur_du, count = 0;
	int      ret = 0;


	BUG_ON(!clip_rects || !num_of_clip_rects);

	/* Figure out all the DU affected by this surface */
	list_for_each_entry(crtc, &dev_priv->dev->mode_config.crtc_list,
			    head) {
		if (crtc->fb != &framebuffer->base)
			continue;

		stdu[num_of_du++] = vmw_crtc_to_stdu(crtc);
	}


	for (cur_du = 0; cur_du < num_of_du; cur_du++)
		for (cur_rect = clip_rects;
		     count < num_of_clip_rects && ret == 0;
		     cur_rect += increment, count++) {
			ret = vmw_stdu_update_st(dev_priv, stdu[cur_du],
						 cur_rect);
		}

	return ret;
}

