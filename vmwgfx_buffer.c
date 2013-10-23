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

#include "vmwgfx_drv.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"

static uint32_t vram_placement_flags = TTM_PL_FLAG_VRAM |
	TTM_PL_FLAG_CACHED;

static uint32_t vram_ne_placement_flags = TTM_PL_FLAG_VRAM |
	TTM_PL_FLAG_CACHED |
	TTM_PL_FLAG_NO_EVICT;

static uint32_t sys_placement_flags = TTM_PL_FLAG_SYSTEM |
	TTM_PL_FLAG_CACHED;

static uint32_t gmr_placement_flags = VMW_PL_FLAG_GMR |
	TTM_PL_FLAG_CACHED;

static uint32_t gmr_ne_placement_flags = VMW_PL_FLAG_GMR |
	TTM_PL_FLAG_CACHED |
	TTM_PL_FLAG_NO_EVICT;

struct ttm_placement vmw_vram_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 1,
	.placement = &vram_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &vram_placement_flags
};

static uint32_t vram_gmr_placement_flags[] = {
	TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED,
	VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED
};

static uint32_t gmr_vram_placement_flags[] = {
	VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED,
	TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED
};

struct ttm_placement vmw_vram_gmr_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 2,
	.placement = vram_gmr_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &gmr_placement_flags
};

static uint32_t vram_gmr_ne_placement_flags[] = {
	TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED | TTM_PL_FLAG_NO_EVICT,
	VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED | TTM_PL_FLAG_NO_EVICT
};

struct ttm_placement vmw_vram_gmr_ne_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 2,
	.placement = vram_gmr_ne_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &gmr_ne_placement_flags
};

struct ttm_placement vmw_vram_sys_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 1,
	.placement = &vram_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags
};

struct ttm_placement vmw_vram_ne_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 1,
	.placement = &vram_ne_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &vram_ne_placement_flags
};

struct ttm_placement vmw_sys_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 1,
	.placement = &sys_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags
};

static uint32_t evictable_placement_flags[] = {
	TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED,
	TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED,
	VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED
};

struct ttm_placement vmw_evictable_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 3,
	.placement = evictable_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags
};

struct ttm_placement vmw_srf_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 1,
	.num_busy_placement = 2,
	.placement = &gmr_placement_flags,
	.busy_placement = gmr_vram_placement_flags
};

struct vmw_ttm_backend {
	struct ttm_backend backend;
	struct page **pages;
	unsigned long num_pages;
	struct vmw_private *dev_priv;
	int gmr_id;
	struct sg_table sgt;
	struct vmw_sg_table vsgt;
	uint64_t sg_alloc_size;
};


/**
 * vmw_ttm_map_phys - get untranslated device addresses for TTM pages
 *
 * @vmw_be: Pointer to a struct vmw_ttm_backend
 *
 * This function should be used to obtain untranslated device addresses
 * for the TTM pages. Should be used when IOMMU remapping is not
 * desired.
 */
static void vmw_ttm_map_phys(struct vmw_ttm_backend *vmw_be)
{
	struct scatterlist *sgl = vmw_be->vsgt.sgt->sgl;

	if (unlikely(sgl == NULL))
		return;

	do {
		sgl->dma_address = sg_phys(sgl);
		if (sg_is_last(sgl))
			break;
		if (sg_is_chain(sgl))
			sgl = sg_chain_ptr(sgl);
		else
			sgl++;
	} while (true);
}

/**
 * vmw_ttm_unmap_from_dma - unmap  device addresses previsouly mapped for
 * TTM pages
 *
 * @vmw_be: Pointer to a struct vmw_ttm_backend
 *
 * Used to free dma mappings previously mapped by vmw_ttm_map_for_dma.
 */
static void vmw_ttm_unmap_from_dma(struct vmw_ttm_backend *vmw_be)
{
	struct device *dev = vmw_be->dev_priv->dev->dev;

	dma_unmap_sg(dev, vmw_be->sgt.sgl, vmw_be->sgt.nents,
		DMA_BIDIRECTIONAL);
	vmw_be->sgt.nents = vmw_be->sgt.orig_nents;
}

/**
 * vmw_ttm_map_for_dma - map TTM pages to get device addresses
 *
 * @vmw_be: Pointer to a struct vmw_ttm_backend
 *
 * This function is used to get device addresses from the kernel DMA layer.
 * However, it's violating the DMA API in that when this operation has been
 * performed, it's illegal for the CPU to write to the pages without first
 * unmapping the DMA mappings, or calling dma_sync_sg_for_cpu(). It is
 * therefore only legal to call this function if we know that the function
 * dma_sync_sg_for_cpu() is a NOP, and dma_sync_sg_for_device() is at most
 * a CPU write buffer flush.
 */
static int vmw_ttm_map_for_dma(struct vmw_ttm_backend *vmw_be)
{
	struct device *dev = vmw_be->dev_priv->dev->dev;
	int ret;

	ret = dma_map_sg(dev, vmw_be->sgt.sgl, vmw_be->sgt.orig_nents,
			 DMA_BIDIRECTIONAL);
	if (unlikely(ret == 0))
		return -ENOMEM;

	vmw_be->sgt.nents = ret;

	return 0;
}

/**
 * vmw_ttm_map_dma - Make sure TTM pages are visible to the device
 *
 * @vmw_be: Pointer to a struct vmw_ttm_backend
 *
 * Select the correct function for and make sure the TTM pages are
 * visible to the device. Allocate storage for the device mappings.
 * If a mapping has already been performed, indicated by the storage
 * pointer being non NULL, the function returns success.
 */
static int vmw_ttm_map_dma(struct vmw_ttm_backend *vmw_be)
{
	struct vmw_private *dev_priv = vmw_be->dev_priv;
	struct ttm_mem_global *glob = vmw_mem_glob(dev_priv);
	struct sg_page_iter iter;
	dma_addr_t old;
	int ret = 0;
	static size_t sgl_size;
	static size_t sgt_size;

	if (vmw_be->vsgt.sgt)
		return 0;

	if (unlikely(!sgl_size)) {
		sgl_size = ttm_round_pot(sizeof(struct scatterlist));
		sgt_size = ttm_round_pot(sizeof(struct sg_table));
	}

	vmw_be->sg_alloc_size = sgt_size + sgl_size * vmw_be->num_pages;
	ret = ttm_mem_global_alloc(glob, vmw_be->sg_alloc_size, false, true);
	if (unlikely(ret != 0))
		return ret;

	ret = sg_alloc_table_from_pages(&vmw_be->sgt, vmw_be->pages,
					vmw_be->num_pages, 0,
					(unsigned long)
					vmw_be->num_pages << PAGE_SHIFT,
					GFP_KERNEL);
	if (unlikely(ret != 0))
		goto out_sg_alloc_fail;

	if (vmw_be->num_pages > vmw_be->sgt.nents) {
		uint64_t over_alloc =
			sgl_size * (vmw_be->num_pages - vmw_be->sgt.nents);

		ttm_mem_global_free(glob, over_alloc);
		vmw_be->sg_alloc_size -= over_alloc;
	}

	vmw_be->vsgt.sgt = &vmw_be->sgt;

	switch (dev_priv->map_mode) {
	case vmw_dma_map_bind:
	case vmw_dma_map_populate:
		ret = vmw_ttm_map_for_dma(vmw_be);
		break;
	case vmw_dma_phys:
		vmw_ttm_map_phys(vmw_be);
		break;
	default:
		BUG();
	}

	if (unlikely(ret != 0))
		goto out_map_fail;

	old = ~((dma_addr_t) 0);
	vmw_be->vsgt.num_regions = 0;
	for_each_sg_page(vmw_be->sgt.sgl, &iter, vmw_be->sgt.orig_nents, 0) {
		dma_addr_t cur = sg_page_iter_dma_address(&iter);
		if (cur != old + PAGE_SIZE)
			vmw_be->vsgt.num_regions++;
		old = cur;
	}

	return 0;

out_map_fail:
	sg_free_table(vmw_be->vsgt.sgt);
	vmw_be->vsgt.sgt = NULL;
out_sg_alloc_fail:
	ttm_mem_global_free(glob, vmw_be->sg_alloc_size);
	return ret;
}

/**
 * vmw_ttm_unmap_dma - Tear down any TTM page device mappings
 *
 * @vmw_be: Pointer to a struct vmw_ttm_backend
 *
 * Tear down any previously set up device DMA mappings and free
 * any storage space allocated for them. If there are no mappings set up,
 * this function is a NOP.
 */
static void vmw_ttm_unmap_dma(struct vmw_ttm_backend *vmw_be)
{
	struct vmw_private *dev_priv = vmw_be->dev_priv;

	if (!vmw_be->vsgt.sgt)
		return;

	switch (dev_priv->map_mode) {
	case vmw_dma_map_bind:
	case vmw_dma_map_populate:
		vmw_ttm_unmap_from_dma(vmw_be);
		/* Fall through */
	case vmw_dma_phys:
		sg_free_table(vmw_be->vsgt.sgt);
		ttm_mem_global_free(vmw_mem_glob(dev_priv),
				    vmw_be->sg_alloc_size);
		vmw_be->vsgt.sgt = NULL;
		break;
	default:
		break;
	}
}

static int vmw_ttm_populate(struct ttm_backend *backend,
			    unsigned long num_pages, struct page **pages,
			    struct page *dummy_read_page)
{
	struct vmw_ttm_backend *vmw_be =
	    container_of(backend, struct vmw_ttm_backend, backend);

	vmw_be->pages = pages;
	vmw_be->num_pages = num_pages;

	return 0;
}

static int vmw_ttm_bind(struct ttm_backend *backend, struct ttm_mem_reg *bo_mem)
{
	struct vmw_ttm_backend *vmw_be =
	    container_of(backend, struct vmw_ttm_backend, backend);
	int ret;

	ret = vmw_ttm_map_dma(vmw_be);
	if (unlikely(ret != 0))
		return ret;

	vmw_be->gmr_id = bo_mem->start;

	return vmw_gmr_bind(vmw_be->dev_priv, &vmw_be->vsgt,
			    vmw_be->num_pages, vmw_be->gmr_id);
}

static int vmw_ttm_unbind(struct ttm_backend *backend)
{
	struct vmw_ttm_backend *vmw_be =
	    container_of(backend, struct vmw_ttm_backend, backend);

	vmw_gmr_unbind(vmw_be->dev_priv, vmw_be->gmr_id);

	if (vmw_be->dev_priv->map_mode == vmw_dma_map_bind)
		vmw_ttm_unmap_dma(vmw_be);

	return 0;
}

static void vmw_ttm_clear(struct ttm_backend *backend)
{
	struct vmw_ttm_backend *vmw_be =
		container_of(backend, struct vmw_ttm_backend, backend);

	vmw_ttm_unmap_dma(vmw_be);

	vmw_be->pages = NULL;
	vmw_be->num_pages = 0;
}

static void vmw_ttm_destroy(struct ttm_backend *backend)
{
	struct vmw_ttm_backend *vmw_be =
	    container_of(backend, struct vmw_ttm_backend, backend);

	kfree(vmw_be);
}

static struct ttm_backend_func vmw_ttm_func = {
	.populate = vmw_ttm_populate,
	.clear = vmw_ttm_clear,
	.bind = vmw_ttm_bind,
	.unbind = vmw_ttm_unbind,
	.destroy = vmw_ttm_destroy,
};

struct ttm_backend *vmw_ttm_backend_init(struct ttm_bo_device *bdev)
{
	struct vmw_ttm_backend *vmw_be;

	vmw_be = kzalloc(sizeof(*vmw_be), GFP_KERNEL);
	if (!vmw_be)
		return NULL;

	vmw_be->backend.func = &vmw_ttm_func;
	vmw_be->dev_priv = container_of(bdev, struct vmw_private, bdev);

	return &vmw_be->backend;
}

int vmw_invalidate_caches(struct ttm_bo_device *bdev, uint32_t flags)
{
	return 0;
}

int vmw_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
		      struct ttm_mem_type_manager *man)
{
	switch (type) {
	case TTM_PL_SYSTEM:
		/* System memory */

		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_CACHED;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_VRAM:
		/* "On-card" video ram */
		man->func = &ttm_bo_manager_func;
		man->gpu_offset = 0;
		man->flags = TTM_MEMTYPE_FLAG_FIXED | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_CACHED;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case VMW_PL_GMR:
		/*
		 * "Guest Memory Regions" is an aperture like feature with
		 *  one slot per bo. There is an upper limit of the number of
		 *  slots as well as the bo size.
		 */
		man->func = &vmw_gmrid_manager_func;
		man->gpu_offset = 0;
		man->flags = TTM_MEMTYPE_FLAG_CMA | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_CACHED;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

void vmw_evict_flags(struct ttm_buffer_object *bo,
		     struct ttm_placement *placement)
{
	*placement = vmw_sys_placement;
}

static int vmw_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	struct ttm_object_file *tfile =
		vmw_fpriv((struct drm_file *)filp->private_data)->tfile;

	return vmw_user_dmabuf_verify_access(bo, tfile);
}

static int vmw_ttm_io_mem_reserve(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct vmw_private *dev_priv = container_of(bdev, struct vmw_private, bdev);

	mem->bus.addr = NULL;
	mem->bus.is_iomem = false;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
	case VMW_PL_GMR:
		return 0;
	case TTM_PL_VRAM:
		mem->bus.offset = mem->start << PAGE_SHIFT;
		mem->bus.base = dev_priv->vram_start;
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void vmw_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
}

static int vmw_ttm_fault_reserve_notify(struct ttm_buffer_object *bo)
{
	return 0;
}

/**
 * FIXME: We're using the old vmware polling method to sync.
 * Do this with fences instead.
 */

static void *vmw_sync_obj_ref(void *sync_obj)
{

	return (void *)
		vmw_fence_obj_reference((struct vmw_fence_obj *) sync_obj);
}

static void vmw_sync_obj_unref(void **sync_obj)
{
	vmw_fence_obj_unreference((struct vmw_fence_obj **) sync_obj);
}

static int vmw_sync_obj_flush(void *sync_obj, void *sync_arg)
{
	vmw_fence_obj_flush((struct vmw_fence_obj *) sync_obj);
	return 0;
}

static bool vmw_sync_obj_signaled(void *sync_obj, void *sync_arg)
{
	unsigned long flags = (unsigned long) sync_arg;
	return	vmw_fence_obj_signaled((struct vmw_fence_obj *) sync_obj,
				       (uint32_t) flags);

}

static int vmw_sync_obj_wait(void *sync_obj, void *sync_arg,
			     bool lazy, bool interruptible)
{
	unsigned long flags = (unsigned long) sync_arg;

	return vmw_fence_obj_wait((struct vmw_fence_obj *) sync_obj,
				  (uint32_t) flags,
				  lazy, interruptible,
				  VMW_FENCE_WAIT_TIMEOUT);
}

struct ttm_bo_driver vmw_bo_driver = {
	.create_ttm_backend_entry = vmw_ttm_backend_init,
	.invalidate_caches = vmw_invalidate_caches,
	.init_mem_type = vmw_init_mem_type,
	.evict_flags = vmw_evict_flags,
	.move = NULL,
	.verify_access = vmw_verify_access,
	.sync_obj_signaled = vmw_sync_obj_signaled,
	.sync_obj_wait = vmw_sync_obj_wait,
	.sync_obj_flush = vmw_sync_obj_flush,
	.sync_obj_unref = vmw_sync_obj_unref,
	.sync_obj_ref = vmw_sync_obj_ref,
	.move_notify = NULL,
	.swap_notify = NULL,
	.fault_reserve_notify = &vmw_ttm_fault_reserve_notify,
	.io_mem_reserve = &vmw_ttm_io_mem_reserve,
	.io_mem_free = &vmw_ttm_io_mem_free,
};
