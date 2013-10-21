/*
 * Copyright (C) 2007 Jens Axboe <jens.axboe@oracle.com>
 *
 * Scatterlist handling helpers.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2. See the file COPYING for more details.
 */

#include <linux/scatterlist.h>
#include "vmwgfx_compat.h"
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))

/**
 * sg_alloc_table_from_pages - Allocate and initialize an sg table from
 *			       an array of pages
 * @sgt:	The sg table header to use
 * @pages:	Pointer to an array of page pointers
 * @n_pages:	Number of pages in the pages array
 * @offset:     Offset from start of the first page to the start of a buffer
 * @size:       Number of valid bytes in the buffer (after offset)
 * @gfp_mask:	GFP allocation mask
 *
 *  Description:
 *    Allocate and initialize an sg table from a list of pages. Contiguous
 *    ranges of the pages are squashed into a single scatterlist node. A user
 *    may provide an offset at a start and a size of valid data in a buffer
 *    specified by the page array. The returned sg table is released by
 *    sg_free_table.
 *
 * Returns:
 *   0 on success, negative error on failure
 */
int sg_alloc_table_from_pages(struct sg_table *sgt,
	struct page **pages, unsigned int n_pages,
	unsigned long offset, unsigned long size,
	gfp_t gfp_mask)
{
	unsigned int chunks;
	unsigned int i;
	unsigned int cur_page;
	int ret;
	struct scatterlist *s;

	/* compute number of contiguous chunks */
	chunks = 1;
	for (i = 1; i < n_pages; ++i)
		if (page_to_pfn(pages[i]) != page_to_pfn(pages[i - 1]) + 1)
			++chunks;

	ret = sg_alloc_table(sgt, chunks, gfp_mask);
	if (unlikely(ret))
		return ret;

	/* merging chunks and putting them into the scatterlist */
	cur_page = 0;
	for_each_sg(sgt->sgl, s, sgt->orig_nents, i) {
		unsigned long chunk_size;
		unsigned int j;

		/* look for the end of the current chunk */
		for (j = cur_page + 1; j < n_pages; ++j)
			if (page_to_pfn(pages[j]) !=
			    page_to_pfn(pages[j - 1]) + 1)
				break;

		chunk_size = ((j - cur_page) << PAGE_SHIFT) - offset;
		sg_set_page(s, pages[cur_page], min(size, chunk_size), offset);
		size -= chunk_size;
		offset = 0;
		cur_page = j;
	}

	return 0;
}

#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))

static int sg_page_count(struct scatterlist *sg)
{
	return PAGE_ALIGN(sg->offset + sg->length) >> PAGE_SHIFT;
}

void __sg_page_iter_start(struct sg_page_iter *piter,
			  struct scatterlist *sglist, unsigned int nents,
			  unsigned long pgoffset)
{
	piter->__pg_advance = 0;
	piter->__nents = nents;

	piter->sg = sglist;
	piter->sg_pgoffset = pgoffset;
}

bool __sg_page_iter_next(struct sg_page_iter *piter)
{
	if (!piter->__nents || !piter->sg)
		return false;

	piter->sg_pgoffset += piter->__pg_advance;
	piter->__pg_advance = 1;

	while (piter->sg_pgoffset >= sg_page_count(piter->sg)) {
		piter->sg_pgoffset -= sg_page_count(piter->sg);
		piter->sg = sg_next(piter->sg);
		if (!--piter->__nents || !piter->sg)
			return false;
	}

	return true;
}
#endif
