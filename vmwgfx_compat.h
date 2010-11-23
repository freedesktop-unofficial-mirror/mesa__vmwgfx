/*
 * Copyright (C) 2009 VMware, Inc., Palo Alto, CA., USA
 *  
 * This file is put together using various bits from the linux kernel.
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive
 * for more details.
 */


/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com> and others.
 *
 * Compatibility defines to make it possible to use the standalone
 * vmwgfx driver in newer kernels.
 */

#ifndef _VMWGFX_COMPAT_H_
#define _VMWGFX_COMPAT_H_

#include <linux/version.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/kref.h>
#ifndef CONFIG_FB_DEFERRED_IO
#include <linux/fb.h>
#endif

/*
 * Defines for standalone building.
 */

#define VMWGFX_STANDALONE
#define TTM_STANDALONE

#undef EXPORT_SYMBOL
#define EXPORT_SYMBOL(_sym)

#undef DRM_UNLOCKED
#define DRM_UNLOCKED 0

#define drm_get_pci_dev(_pdev, _ent, _driver) \
	drm_get_dev(_pdev, _ent, _driver);

extern int ttm_init(void);
extern void ttm_exit(void);

/**
 * Handover available?
 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))
#define VMWGFX_HANDOVER
#endif

/** 
 * getrawmonotonic
 */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28))
static inline void getrawmonotonic(struct timespec *ts)
{
	*ts = current_kernel_time();
}
#endif

/**
 * timespec_sub
 */

static inline struct timespec __vmw_timespec_sub(struct timespec t1,
						 struct timespec t2)
{
	t1.tv_sec -= t2.tv_sec;
	if (t2.tv_nsec > t1.tv_nsec) {
		t1.tv_nsec += (1000000000L - t2.tv_nsec);
		t1.tv_sec -= 1L;
	} else
		t1.tv_nsec -= t2.tv_nsec;
	
	return t1;
}

#undef timespec_sub
#define timespec_sub(_t1, _t2) \
	__vmw_timespec_sub(_t1, _t2)

/* 
 * dev_set_name 
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30))
#define dev_set_name(_dev, _name) \
	({snprintf((_dev)->bus_id, BUS_ID_SIZE, (_name)); 0;})
#endif

/*
 * ioremap_wc
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
#undef ioremap_wc
#define ioremap_wc(_offset, _size) \
	ioremap_nocache(_offset, _size)
#endif

/*
 * kmap_atomic_prot
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31))
#undef kmap_atomic_prot
#define kmap_atomic_prot(_page, _km_type, _prot) \
	kmap_atomic(_page, _km_type)
#endif

/*
 * set_memory_wc
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
#undef set_memory_wc
#define set_memory_wc(_pa, _num) \
	set_memory_uc(_pa, _num)
#endif

/*
 * shmem_file_setup
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28))
#undef shmem_file_setup
#define shmem_file_setup(_fname, _size, _dummy) \
	(ERR_PTR(-ENOMEM))
#endif

/*
 * vm_insert_mixed
 */ 
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29))
#define vm_insert_mixed(_vma, _addr, _pfn) \
	vm_insert_pfn(_vma, _addr, _pfn)
#undef VM_MIXEDMAP
#define VM_MIXEDMAP VM_PFNMAP
#endif

/*
 * fault vs nopfn
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
#define TTM_HAVE_NOPFN
#define VMWGFX_HAVE_NOPFN
#endif

/*
 * pgprot_writecombine
 */
#if defined(__i386__) || defined(__x86_64__)
#undef pgprot_writecombine
#define pgprot_writecombine(_prot) \
	pgprot_noncached(_prot)
#endif

/*
 * const vm_operations_struct
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
#define TTM_HAVE_CVOS
#endif

/*
 * const sysfs_ops
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
#define TTM_HAVE_CSO
#endif

/*
 * list_cut_position
 */
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,27))
static inline void __vmwgfx_lcp(struct list_head *list,
		struct list_head *head, struct list_head *entry)
{
	struct list_head *new_first = entry->next;
	list->next = head->next;
	list->next->prev = list;
	list->prev = entry;
	entry->next = list;
	head->next = new_first;
	new_first->prev = head;
}

static inline void vmwgfx_lcp(struct list_head *list,
		struct list_head *head, struct list_head *entry)
{
	if (list_empty(head))
		return;
	if ((head->next == head->prev) &&
		(head->next != entry && head != entry))
		return;
	if (entry == head)
		INIT_LIST_HEAD(list);
	else
		__vmwgfx_lcp(list, head, entry);
}

#undef list_cut_position
#define list_cut_position(_list, _head, _entry) \
  vmwgfx_lcp(_list, _head, _entry)
#endif

/**
 * set_pages_array_wc
 * No caching attributes on vmwgfx.
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
static inline int set_pages_array_wc(struct page **pages, int addrinarray)
{
	return 0;
}
#endif

/**
 * set_pages_array_uc and set_pages_array_wb()
 * No caching attributes on vmwgfx.
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30))
static inline int set_pages_array_uc(struct page **pages, int addrinarray)
{
	return 0;
}

static inline int set_pages_array_wb(struct page **pages, int addrinarray)
{
	return 0;
}
#endif

/**
 * CONFIG_FB_DEFERRED_IO might not be defined. Yet we rely on it.
 * Thus, provide a copy of the implementation in vmw_defio.c.
 * Declarations and prototypes go here.
 */

#ifndef CONFIG_FB_DEFERRED_IO
struct vmw_fb_deferred_par;
struct vmw_fb_deferred_io {
	/* delay between mkwrite and deferred handler */
	unsigned long delay;
	struct mutex lock; /* mutex that protects the page list */
	struct list_head pagelist; /* list of touched pages */
	/* callback */
	void (*deferred_io)(struct vmw_fb_deferred_par *par,
			    struct list_head *pagelist);
};

struct vmw_fb_deferred_par {
	struct delayed_work deferred_work;
	struct vmw_fb_deferred_io *fbdefio;
	struct fb_info *info;
};

#define fb_deferred_io_init(_info) \
	vmw_fb_deferred_io_init(_info)
#define fb_deferred_io_cleanup(_info) \
	vmw_fb_deferred_io_cleanup(_info)

extern void vmw_fb_deferred_io_init(struct fb_info *info);
extern void vmw_fb_deferred_io_cleanup(struct fb_info *info);

#define VMWGFX_FB_DEFERRED
#endif

/**
 * Power management
 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
#define VMW_HAS_PM_OPS
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29))
#define dev_pm_ops pm_ops
#endif
#endif

/**
 * kref_sub
 */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
static inline int vmwgfx_kref_sub(struct kref *kref, unsigned int count,
				  void (*release)(struct kref *kref))
{
	if (atomic_sub_and_test((int) count, &kref->refcount)) {
		release(kref);
		return 1;
	}
	return 0;
}
#define kref_sub(_a, _b, _c) vmwgfx_kref_sub(_a, _b, _c)
#endif

/**
 * kmap_atomic
 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
#define VMW_HAS_STACK_KMAP_ATOMIC
#endif

#endif
