/* Cache structure for proxy
 * Made by : Kun Woo Yoo
 * Andrew ID : kunwooy
 *
 * The cache structure is implemented using doubly linked list.
 * All cache pages are allocated through malloc, and when uncached, are
 * freed. Also, semaphore has been used to avoid race conditions.
 */

#include "cache.h"


// global variables
cache_p *cache_first; // ptr to first page in cache
cache_p *cache_last; // ptr to last page in cache
volatile unsigned long cache_size; // total size of cache
volatile int readcnt; // for semaphore
sem_t mutex, w; // semaphore variables

/* init_cache initializes an empty cache and sets up sem-vars.
 */
void init_cache() {

    // initialize cache
    cache_first = NULL;
    cache_last = NULL;
    cache_size = 0;
    readcnt = 0;
    // initialize semaphore global variables
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);

    return;
}

/*
 * add_page adds a page to the cache. To implement semi-LRU,
 * add_page adds a page to the last in list.
 */
void add_page(cache_p *page) {

    // if only element
    if (cache_first == NULL || cache_last == NULL) {
        cache_first = page;
        cache_last = page;
        page->prev = NULL;
        page->next = NULL;
        return;
    }

    // add to last
    cache_last->next = page;
    page->next = NULL;
    page->prev = cache_last;
    cache_last = page;

    // finally, increment cache size
    cache_size += (page->size);

    return;
}


/*
 * delete_page deletes the targeted page from cache. To implement semi-LRU,
 * when deleting page for a space in cache (eliminate = 1),
 * the first element is deleted.
 */
void delete_page(cache_p *page, int eliminate) {

    cache_p *prev = page->prev;
    cache_p *next = page->next;

    if (prev == NULL && next == NULL) { // only element
        cache_first = NULL;
        cache_last = NULL;
    }

    else if (prev != NULL && next == NULL) { // last element
        prev->next = NULL;
        cache_last = prev;
    }

    else if (prev == NULL && next != NULL) { // first element
        next->prev = NULL;
        cache_first = next;
    }

    else { // in middle
        prev->next = next;
        next->prev = prev;
    }

    // now decrease the cache size
    cache_size -= (page->size);

    // if totally eliminate, free the page
    if (eliminate) {
        Free(page->uri);
        Free(page->payload);
        Free(page);
    }

    return;
}

/* is_hit checks whether the given hostname's content is saved in the cache
 * returns the address of cache page if hit, NULL if misses.
 */
cache_p *is_hit(char *hostname) {

    char *page_host;
    cache_p *target = cache_first;

    while (target != NULL) {
        page_host = target->uri;

        // if matches
        if (!strcmp(page_host, hostname)) {
            return target;
        }

        target = target->next;
    }

    // if not found
    return NULL;
}

cache_p *new_page(char *host_uri, char *host_payload, unsigned long size) {

    cache_p *new = (cache_p *) Malloc(sizeof(cache_p));
    new->uri = Malloc(strlen(host_uri) + 1); // to include '\0'
    new->payload = Malloc(strlen(host_payload) + 1);

    strcpy(new->uri, host_uri); // copy uri
    strcpy(new->payload, host_payload); // copy payload
    new->size = size;

    return new;
}


