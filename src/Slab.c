#pragma once
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "Slab.h"
#include <Windows.h>
#include "Buddy.h"
#include <string.h>

#define NAME_LEN (60)
#define LOCK(mutex) (WaitForSingleObject(mutex, INFINITE))
#define UNLOCK(mutex) (ReleaseMutex(mutex))

typedef unsigned int m_kmem_bufctl_t;
unsigned int BUFCTL_END = UINT_MAX;

#define slab_fctl(slabp)\
		((m_kmem_bufctl_t*)((slab_s*)slabp + 1))

typedef struct m_slab_s{
	int list; // { free = 0, partial = 1, full = 2};
	unsigned int colour_offset;
	void* saddr;
	unsigned int objects_inuse;
	unsigned int free_objects;
	struct m_slab_s* next;

	m_kmem_bufctl_t free;

	
}slab_s;

struct kmem_cache_s {
	slab_s* slabs_full;
	slab_s* slabs_free;
	slab_s* slabs_partial;

	size_t object_size;
	unsigned int objects_per_slab;
	unsigned int pageS_order;
	unsigned int objects_in_use;

	size_t colour;
	unsigned int colour_off;
	unsigned int colour_next;

	unsigned int growing;

	void(*ctor)(void*);
	void(*dtor)(void*);
	size_t free_dtor_arr_size;

	char name[NAME_LEN];
	int error_code;
	HANDLE m_mutex;

	struct kmem_cache_s* next;
	struct kmem_cache_s* prev;
};

typedef struct m_main_cache {
	kmem_cache_t* head;
	kmem_cache_t* size_n_caches[13];
	HANDLE mutex;
}main_cache;

void m_kmem_cache_estimate(unsigned int blocks, kmem_cache_t* cachep);
int m_kmem_cache_free(kmem_cache_t* cachep, void* objp);
void* m_kmem_cache_alloc(kmem_cache_t* cachep);
int m_kmem_cache_shrink(kmem_cache_t* cachep);

static main_cache* main_cache_ptr;

void kmem_init(void* space, int block_num)
{
	void* start_from = buddy_initialize(space, block_num);

	
	main_cache_ptr = (main_cache*)buddy_take(1);
	main_cache_ptr->head = NULL;
	main_cache_ptr->mutex = CreateMutex(NULL, FALSE, NULL);

	for (int i = 0; i < 13; i++) {
		main_cache_ptr->size_n_caches[i] = NULL;
	}
}

kmem_cache_t* kmem_cache_create(const char* name, size_t obj_size, void(*ctor)(void*), void(*dtor)(void*))
{
	if (main_cache_ptr == NULL) {
		return NULL; //neka greska
	}

	LOCK(main_cache_ptr->mutex);
	kmem_cache_t* cache_p = (kmem_cache_t*)buddy_take(1);
	if (cache_p == NULL) return NULL; // no enough space;

	cache_p->next = main_cache_ptr->head;
	if (cache_p->next) {
		cache_p->next->prev = cache_p;
	}
	cache_p->prev = NULL;
	main_cache_ptr->head = cache_p;
	UNLOCK(main_cache_ptr->mutex);

	cache_p->slabs_free = NULL;
	cache_p->slabs_partial = NULL;
	cache_p->slabs_full = NULL;

	cache_p->object_size = obj_size;
	cache_p->objects_per_slab = 0;
	cache_p->objects_in_use = 0;
	cache_p->colour_next = 0;
	cache_p->colour_off = 0;
	cache_p->ctor = ctor;
	cache_p->dtor = dtor;
	cache_p->growing = 0;
	cache_p->error_code = 0;
	cache_p->m_mutex = CreateMutex(NULL, FALSE, NULL);

	strcpy_s(cache_p->name, sizeof(cache_p->name), name);

	//numofblocks je broj stvarnih blokova potrebnih za 1 objekat -> ne mora biti stepen 2 -> znaci moze biti 3
	int numofblocks = (int)(ceil(obj_size / (double)BLOCK_SIZE));		//celobrojno deljenje zato (double) cast
	int level = (int)log2(numofblocks) + !isPwrOfTwo(numofblocks);
	unsigned int blocks_for1_object = (1 << level);		//ovo je ustvari blocks_to_allocate = najmanje blokova potrebno da se smesti 1 objekat izrazeno u najmanjem stepenu broja dva

	m_kmem_cache_estimate(blocks_for1_object, cache_p);
	
	//printf("cache->color Number = %d \n", cache_p->colour);
	//printf_s("New cache %s has address = %p size of cache struct = %d \n", cache_p->name, cache_p, sizeof(struct kmem_cache_s));

	return cache_p;
}


//m										blocks = br blokova stepen 2
void m_kmem_cache_estimate(unsigned int blocks, kmem_cache_t* cachep)
{
	if (cachep->dtor) {		//used to know if we need to call destructor when kmem_cache_destroy is called - we do not need to call it if user used some object(he will call kmem_free), but we need to call it for all objects that are initialized at start but not used
		cachep->free_dtor_arr_size = 2 * sizeof(unsigned int);
	}
	else {
		cachep->free_dtor_arr_size = sizeof(unsigned int);
	}

	unsigned int blks = blocks;
	int done = 0;
	size_t wastage;
	size_t mem;

	while (!done) {

		wastage = blks * BLOCK_SIZE;
		wastage -= sizeof(slab_s);
		mem = 0;


		while ((mem + cachep->object_size + cachep->free_dtor_arr_size) <= wastage) {
			mem = mem + cachep->object_size + cachep->free_dtor_arr_size;
			cachep->objects_per_slab++;
		}
		
		done = 1;
		if (cachep->objects_per_slab == 0) {
			done = 0;
			blks *= 2;		//ne moram logaritmovati jer znam da mi je blocks proslednjen stepen 2
		}
	}

	cachep->pageS_order = (int)log2(blks);

	wastage -= mem;
	if (wastage > 0) {
		unsigned int col = wastage / CACHE_L1_LINE_SIZE;
		cachep->colour = col;
	}
	else {
		cachep->colour = 0;
	}
}

void* kmem_cache_alloc(kmem_cache_t* cachep)
{
	if (cachep == NULL) return NULL;

	LOCK(cachep->m_mutex);
	void* obj = m_kmem_cache_alloc(cachep);
	UNLOCK(cachep->m_mutex);

	return obj;
}

void* m_kmem_cache_alloc(kmem_cache_t* cachep){
	void* objectp;
	slab_s* slabp = NULL;

	if (cachep->slabs_partial != NULL) {
		slabp = cachep->slabs_partial;
	}
	else if (cachep->slabs_free != NULL) {
		slabp = cachep->slabs_free;
		cachep->slabs_free = cachep->slabs_free->next;

		/* Add slab to partial list */
		slabp->next = cachep->slabs_partial;
		cachep->slabs_partial = slabp;
		slabp->list = 1;
	}
	else if (cachep->slabs_partial == NULL && cachep->slabs_free == NULL) {	 /* Creating new slab - no free or partial slabs available */
		int slab_allocate = 1 << cachep->pageS_order;

		LOCK(main_cache_ptr->mutex);
		slab_s* new_slab = (slab_s*)buddy_take(slab_allocate);
		UNLOCK(main_cache_ptr->mutex);


		if (new_slab == NULL) {
			cachep->error_code = 1;
			return NULL;
		}
		slabp = new_slab;

		/* Making it partial because I will allocate one object since that's the reason kmem_alloc is called. */
		new_slab->list = 1;
		cachep->growing = 1;
		new_slab->next = cachep->slabs_partial;
		cachep->slabs_partial = new_slab;

		new_slab->objects_inuse = 0;
		new_slab->free_objects = cachep->objects_per_slab;
		new_slab->colour_offset = cachep->colour_off;

		unsigned int i = cachep->colour_next;
		if (++i > cachep->colour) {
			cachep->colour_next = 0;
			cachep->colour_off = 0;
		}
		else {
			cachep->colour_next++;
			cachep->colour_off += CACHE_L1_LINE_SIZE;
		}


		new_slab->saddr = (unsigned char*)new_slab + sizeof(slab_s) + cachep->objects_per_slab * cachep->free_dtor_arr_size + new_slab->colour_offset;
		new_slab->free = 0;

		for (unsigned int i = 0; i < cachep->objects_per_slab - 1; i++) {
			slab_fctl(slabp)[i] = i + 1;

			if (cachep->dtor)
				slab_fctl(slabp)[i + cachep->objects_per_slab] = 1;			/* Destructor needs to be called. */
		}

		slab_fctl(slabp)[cachep->objects_per_slab - 1] = BUFCTL_END;


		if (cachep->ctor) {
			void* temp_mem = new_slab->saddr;
			for (unsigned int i = 0; i < cachep->objects_per_slab; i++) {
				LOCK(main_cache_ptr->mutex);
				cachep->ctor(temp_mem);
				UNLOCK(main_cache_ptr->mutex);
				temp_mem = (unsigned char*)temp_mem + cachep->object_size;
			}
		}
		//printf_s("New Slab = %p for cache %s with num of blokcs = %d \n", (void*)new_slab, cachep->name, 1 << cachep->pageS_order);
	}
	

	/* Always executed */

	objectp = (unsigned char*)slabp->saddr + slabp->free * cachep->object_size;
	slabp->free = slab_fctl(slabp)[slabp->free];

	cachep->objects_in_use++;
	slabp->objects_inuse++;
	slabp->free_objects--;
	if (slabp->free_objects == 0) {					/* Adding it to full list */
		cachep->slabs_partial = slabp->next;
		slabp->next = cachep->slabs_full;
		cachep->slabs_full = slabp;
		slabp->list = 2;
	}

	return objectp;
}

void kmem_cache_free(kmem_cache_t* cachep, void* objp)
{
	LOCK(cachep->m_mutex);
	m_kmem_cache_free(cachep, objp);
	UNLOCK(cachep->m_mutex);
}

//m
int m_obj_in_slab(kmem_cache_t* cachep, slab_s* tmp_slab, void* objp) {
	return ((unsigned char*)tmp_slab->saddr <= (unsigned char*)objp) && ((unsigned char*)objp < ((unsigned char*)tmp_slab + (1 << cachep->pageS_order) * BLOCK_SIZE));
}

//m
int m_kmem_cache_free(kmem_cache_t* cachep, void* objp) 
{
	if (!cachep) return -1;
	slab_s* tmp_slab = NULL;
	slab_s* prev_slab = NULL;
	int cond = 0;
	int in_slab = 0; // 0 = partial, 1 = full

	if (cachep->slabs_partial) {
		tmp_slab = cachep->slabs_partial;
		cond = m_obj_in_slab(cachep, tmp_slab, objp);

		while (!cond && tmp_slab != NULL) {
			prev_slab = tmp_slab;
			tmp_slab = tmp_slab->next;
			if (tmp_slab)
				cond = m_obj_in_slab(cachep, tmp_slab, objp);
			else break;
		}
	}

	if (!cond) {						/* If cond = 0 -> its not in partial, but it is probably in full_slab list. */
		prev_slab = NULL;
		tmp_slab = cachep->slabs_full;
		if (tmp_slab) {
			cond = m_obj_in_slab(cachep, tmp_slab, objp);

			while (!cond && tmp_slab != NULL) {
				prev_slab = tmp_slab;
				tmp_slab = tmp_slab->next;
				if (tmp_slab)
					cond = m_obj_in_slab(cachep, tmp_slab, objp);
				else break;
			}

			if (cond) {
				in_slab = 1;
			}
			else { //postoji slabs full ali nije u slabs fullu
				in_slab = -1;
				cachep->error_code = 2;
				//printf("| kmem_cache_free | Slab not found \n");
				return -1;
			}
		}
		else {
			//sigurno je greska jer ni ne postoji slabs_full
			in_slab = -1;
			cachep->error_code = 2;
			//printf("| kmem_cache_free | Slab not found \n");
			return -1;
		}
	}
	if (tmp_slab) {			//zbog kompajla - zeleno bude podvuceno za null ptr
		tmp_slab->objects_inuse--;
		tmp_slab->free_objects++;
		cachep->objects_in_use--;

		if (in_slab == 0) {			//found in partial list

			if (tmp_slab->objects_inuse == 0) {
				if (prev_slab)
					prev_slab->next = tmp_slab->next;
				else
					cachep->slabs_partial = tmp_slab->next;

				tmp_slab->next = cachep->slabs_free;
				cachep->slabs_free = tmp_slab;
				//printf("| kmem_cache_free | Slab found in partial list. \n");
			}
		}

		if (in_slab == 1) {				// Ako je objekat nadjen u full slabu, on sigurno nakon oslobadjanja ide u partial slab OSIM ako je bio jedini objekat u slabu-u -> ide u free
			if (prev_slab)
				prev_slab->next = tmp_slab->next;
			else
				cachep->slabs_full = tmp_slab->next;

			if (tmp_slab->objects_inuse == 0) {
				tmp_slab->next = cachep->slabs_free;
				cachep->slabs_free = tmp_slab;
			}
			else {
				tmp_slab->next = cachep->slabs_partial;
				cachep->slabs_partial = tmp_slab;
			}

			//printf("| kmem_cache_free | Slab found in full list. \n");
		}
	
		slab_s* slabp = tmp_slab;
		unsigned int obj_index = ((unsigned char*)objp - (unsigned char*)slabp->saddr) / cachep->object_size;
		slab_fctl(slabp)[obj_index] = slabp->free;
		slabp->free = obj_index;

		if (cachep->dtor) {
			LOCK(main_cache_ptr->mutex);
			cachep->dtor(objp);
			UNLOCK(main_cache_ptr->mutex);

			slab_fctl(slabp)[obj_index + cachep->objects_per_slab] = 0;			//Set to 0 so later you know that destructor for this object is already called so no need to call it again when kmem_destroy_cache.
		}
	}

	return 1;

}

int kmem_cache_shrink(kmem_cache_t* cachep)
{
	//error
	if (!cachep) return -1;

	LOCK(cachep->m_mutex);
	int ret = m_kmem_cache_shrink(cachep, 0);
	UNLOCK(cachep->m_mutex);

	return ret;
}

int m_kmem_cache_shrink(kmem_cache_t* cachep, int destroy_flag) {

	if (!destroy_flag) {
		if (cachep->growing == 1) {
			cachep->growing = 0;
			return 0;
		}
	}

	slab_s* tmp_slab = cachep->slabs_free;
	void* object_ptr = 0;
	unsigned int object_index = 0;
	int dtor_flag = 0;
	int blocks_freed = 0;
	int to_free = 0;

	//iterira kroz sve free slabove
	while (tmp_slab) {
		to_free = (1 << cachep->pageS_order);

		LOCK(main_cache_ptr->mutex);
		buddy_add((void*)tmp_slab, to_free);
		UNLOCK(main_cache_ptr->mutex);

		blocks_freed += to_free;

		//call destructor for unused but initialized (at start - kmem_alloc) objects.
		if (cachep->dtor) {
			for (int i = 0; i < cachep->objects_per_slab; i++) {
				object_ptr = (unsigned char*)tmp_slab->saddr + i * cachep->object_size;
				object_index = ((unsigned char*)object_ptr - (unsigned char*)tmp_slab->saddr) / cachep->object_size;
				dtor_flag = slab_fctl(tmp_slab)[object_index + cachep->objects_per_slab];
				if (dtor_flag) {
					//printf("usao %d\n", i);
					LOCK(main_cache_ptr->mutex);
					cachep->dtor(object_ptr);
					UNLOCK(main_cache_ptr->mutex);
				}
			}
		}
		tmp_slab = tmp_slab->next;
	}


	cachep->growing = 0;
	cachep->slabs_free = NULL;

	return blocks_freed;
}

void kmem_cache_destroy(kmem_cache_t* cachep)
{
	//error
	if (!cachep) return;

	LOCK(main_cache_ptr->mutex);
	LOCK(cachep->m_mutex);

	if (cachep->slabs_partial || cachep->slabs_full) {
		cachep->error_code = 3;
		return; //errorr2 ne moze da destroy pre pozivanja kemm_free
	}

	if (cachep->prev)
		cachep->prev->next = cachep->next;
	if (cachep->next)
		cachep->next->prev = cachep->prev;
	if (main_cache_ptr->head == cachep)
		main_cache_ptr->head->next = cachep->next;

	cachep->next = 0; 
	cachep->prev = 0;
	
	m_kmem_cache_shrink(cachep, 1);

	//printf_s("Dodajem cache %p u buddy\n", cachep);
	buddy_add((void*)cachep, 1);

	UNLOCK(cachep->m_mutex);
	CloseHandle(cachep->m_mutex);
	UNLOCK(main_cache_ptr->mutex);
}

void kmem_cache_info(kmem_cache_t* cachep)
{
	if (!cachep) return;
	LOCK(main_cache_ptr->mutex);

	char name[32];
	unsigned int slabsCnt = 0;
	unsigned int active_objs = 0;
	strcpy_s(name, sizeof(name), cachep->name);

	slab_s* tmp = cachep->slabs_free;

	while (tmp) {
		slabsCnt++;
		tmp = tmp->next;
	}

	tmp = cachep->slabs_partial;

	while (tmp) {
		slabsCnt++;
		active_objs += tmp->objects_inuse;
		tmp = tmp->next;
	}

	tmp = cachep->slabs_full;
	while(tmp){
		slabsCnt++;
		active_objs += tmp->objects_inuse;
		tmp = tmp->next;
	}
	
	unsigned int cache_size = slabsCnt * (1 << cachep->pageS_order);
	double percent;
	double dole = (double)slabsCnt * (double)cachep->objects_per_slab;
	if (dole != 0)
		percent = ((double)cachep->objects_in_use) / dole * 100;
	else percent = 0;
	//printf("Cache next = %p, cache prev = %p \n", cachep->next, cachep->prev);

	printf_s("%-32s %-15s %-15s %-12s %-20s %-10s \n", "Name", "Object Size", "CacheS Blocks", "NumOfSlabs", "Objects per slab", "Usage %");
	printf_s("%-32s %-15d %-15d %-12d %-20d %-10.2f \n", name, cachep->object_size, cache_size, slabsCnt, cachep->objects_per_slab, percent);
	//printf_s("%d active objects \n", active_objs);

	UNLOCK(main_cache_ptr->mutex);
}



void* kmalloc(size_t size)
{
	size_t msize = size;
	if (size < (1 << 5)) msize = 1 << 5;
	if (size > (1 << 17)) return NULL;

	char name[NAME_LEN];
	int index = (int)ceil(log2(size));
	index -= 5;

	kmem_cache_t** size_n = main_cache_ptr->size_n_caches;
	
	LOCK(main_cache_ptr->mutex);

	if (size_n[index] == NULL) {
		sprintf_s(name, sizeof(name), "size-2^%d", index + 5);
		size_n[index] = kmem_cache_create(name, msize, NULL, NULL);
	}

	void* buff = kmem_cache_alloc(size_n[index]);		/* Mora biti u ovom mutexu, jer neko moze pozvati za dati kes kfree u kojem se poziva m_kmem_cache_free, i ukoliko ne bi bilo mutexa moze se desiti
														da u jednom trenutku jedna nit zove kmem_cache_alloc(size_n[i]) a druga m_kmem_cache_free i tu se poremeti nesto.*/
	UNLOCK(main_cache_ptr->mutex);


	return buff;
}

void kfree(const void* objp) {
	if (!objp) return;

	kmem_cache_t** size_n = main_cache_ptr->size_n_caches;
	int found = 0;
	int index = -1;

	LOCK(main_cache_ptr->mutex);

	for (int i = 0; i < 13; i++) {
		if (size_n[i] == NULL) continue;

		if (m_kmem_cache_free(size_n[i], objp) == -1) {
			size_n[i]->error_code = 0;	//reset error code, since it's set to -2 in kmem_cache_free if not found in given cache
		}
		else {
			/* Slab found for size-n cache and freed given objp */
			//kmem_cache_destroy(size_n[i]);
			break;
		}
	}
	
	UNLOCK(main_cache_ptr->mutex);
}

int kmem_cache_error(kmem_cache_t* cachep)
{
	if (!cachep) return -1;

	LOCK(main_cache_ptr->mutex);
	LOCK(cachep->m_mutex);

	int ret = -1; 
	ret = cachep->error_code; 
	if (ret == 0) {
		printf_s("No errors for given cache. \n");
	}
	else if (ret == 1) {
		printf_s("No enough memory for allocating new slab. \n");
	}
	else if (ret == 2) {
		printf_s("For function kmem_cache_free, argument objectp is not found in given cache. \n");
	}

	UNLOCK(cachep->m_mutex);
	UNLOCK(main_cache_ptr->mutex);

	return ret;
}
