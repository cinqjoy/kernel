#include "globals.h"
#include "errno.h"

#include "util/string.h"
#include "util/debug.h"

#include "mm/mmobj.h"
#include "mm/pframe.h"
#include "mm/mm.h"
#include "mm/page.h"
#include "mm/slab.h"
#include "mm/tlb.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/shadowd.h"

#define SHADOW_SINGLETON_THRESHOLD 5

int shadow_count = 0; /* for debugging/verification purposes */
#ifdef __SHADOWD__
/*
 * number of shadow objects with a single parent, that is another shadow
 * object in the shadow objects tree(singletons)
 */
static int shadow_singleton_count = 0;
#endif

static slab_allocator_t *shadow_allocator;

static void shadow_ref(mmobj_t *o);
static void shadow_put(mmobj_t *o);
static int  shadow_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf);
static int  shadow_fillpage(mmobj_t *o, pframe_t *pf);
static int  shadow_dirtypage(mmobj_t *o, pframe_t *pf);
static int  shadow_cleanpage(mmobj_t *o, pframe_t *pf);

static mmobj_ops_t shadow_mmobj_ops = {
        .ref = shadow_ref,
        .put = shadow_put,
        .lookuppage = shadow_lookuppage,
        .fillpage  = shadow_fillpage,
        .dirtypage = shadow_dirtypage,
        .cleanpage = shadow_cleanpage
};

/*
 * This function is called at boot time to initialize the
 * shadow page sub system. Currently it only initializes the
 * shadow_allocator object.
 */
void
shadow_init()
{
	shadow_allocator=slab_allocator_create("shadowobj", sizeof(mmobj_t));
	KASSERT(shadow_allocator);
}

/*
 * You'll want to use the shadow_allocator to allocate the mmobj to
 * return, then then initialize it. Take a look in mm/mmobj.h for
 * macros which can be of use here. Make sure your initial
 * reference count is correct.
 */
mmobj_t *
shadow_create()
{
        mmobj_t *shadowobj;
    	shadowobj = slab_obj_alloc(shadow_allocator);
    	KASSERT(shadowobj);
        mmobj_init(shadowobj, &shadow_mmobj_ops);
        shadowobj->mmo_refcount = 1;
    	return shadowobj;
}

/* Implementation of mmobj entry points: */

/*
 * Increment the reference count on the object.
 */
static void
shadow_ref(mmobj_t *o)
{
		KASSERT(o && (0 < o->mmo_refcount) && (&shadow_mmobj_ops == o->mmo_ops));
		o->mmo_refcount++;
}

/*
 * Decrement the reference count on the object. If, however, the
 * reference count on the object reaches the number of resident
 * pages of the object, we can conclude that the object is no
 * longer in use and, since it is a shadow object, it will never
 * be used again. You should unpin and uncache all of the object's
 * pages and then free the object itself.
 */
static void
shadow_put(mmobj_t *o)
{
		KASSERT(o && (0 < o->mmo_refcount) && (&shadow_mmobj_ops == o->mmo_ops));
		o->mmo_refcount--;
		if(o->mmo_refcount==o->mmo_nrespages){
			pframe_t *pf;
			list_iterate_begin(&o->mmo_respages,pf,pframe_t,pf_olink){
				while(pframe_is_pinned(pf)) pframe_unpin(pf);
                if (pframe_is_busy(pf)) sched_sleep_on(&pf->pf_waitq);
                if (pframe_is_dirty(pf)) pframe_clean(pf);
                pframe_free(pf);
			}list_iterate_end();
		}slab_obj_free(shadow_allocator,o);
}

/* This function looks up the given page in this shadow object. The
 * forwrite argument is true if the page is being looked up for
 * writing, false if it is being looked up for reading. This function
 * must handle all do-not-copy-on-not-write magic (i.e. when forwrite
 * is false find the first shadow object in the chain which has the
 * given page resident). copy-on-write magic (necessary when forwrite
 * is true) is handled in shadow_fillpage, not here. */
static int
shadow_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf)
{
		pframe_t *tmp_pf;
		list_iterate_begin(&o->mmo_respages,tmp_pf,pframe_t,pf_olink){
			if(tmp_pf->pf_pagenum==pagenum&&tmp_pf->pf_obj==o){
				*pf=tmp_pf;
				return 0;
			}
		}list_iterate_end();

		if(!forwrite){/* looked up for reading */
			if(o->mmo_shadowed){
				return o->mmo_shadowed->mmo_ops->lookuppage(o->mmo_shadowed,pagenum,forwrite,pf);
			}else return -1; /* Should not be here */
		}
		else{/* looked up for writing */
			return pframe_get(o,pagenum,pf);
		}
}

/* As per the specification in mmobj.h, fill the page frame starting
 * at address pf->pf_addr with the contents of the page identified by
 * pf->pf_obj and pf->pf_pagenum. This function handles all
 * copy-on-write magic (i.e. if there is a shadow object which has
 * data for the pf->pf_pagenum-th page then we should take that data,
 * if no such shadow object exists we need to follow the chain of
 * shadow objects all the way to the bottom object and take the data
 * for the pf->pf_pagenum-th page from the last object in the chain). */
static int
shadow_fillpage(mmobj_t *o, pframe_t *pf)
{
		KASSERT(pframe_is_busy(pf));
	    KASSERT(!pframe_is_pinned(pf));
		pframe_t *tmp_pf;
		if(o->mmo_ops->lookuppage(o,pf->pf_pagenum,0,&tmp_pf)==0){
			memcpy(pf->pf_addr,tmp_pf->pf_addr,PAGE_SIZE);
			return 0;
		}else return -1; /* Should not be here */
}

/* These next two functions are not difficult. */

static int
shadow_dirtypage(mmobj_t *o, pframe_t *pf)
{
	pframe_t *tmp_pf;
	list_iterate_begin(&o->mmo_respages,tmp_pf,pframe_t,pf_olink){
		if(tmp_pf->pf_pagenum==pf->pf_pagenum&&tmp_pf->pf_obj==pf->pf_obj){
			pframe_set_dirty(tmp_pf);
			return 0;
		}
	}list_iterate_end();
	return 0;
}

static int
shadow_cleanpage(mmobj_t *o, pframe_t *pf)
{
	pframe_t *tmp_pf;
	list_iterate_begin(&o->mmo_respages,tmp_pf,pframe_t,pf_olink){
		if(tmp_pf->pf_pagenum==pf->pf_pagenum&&tmp_pf->pf_obj==pf->pf_obj){
			memcpy(tmp_pf->pf_addr,pf->pf_addr,PAGE_SIZE);
			return 0;
		}
	}list_iterate_end();
	return 0;
}
