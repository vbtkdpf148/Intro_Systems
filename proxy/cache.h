#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* represents a page of cached object */
typedef struct cache_page {
    char *uri;
    struct cache_page *prev; // ptr to prev page on cache
    struct cache_page *next; // ptr to next page on cache
    unsigned long size; // size of payload
    char *payload;
} cache_p;

/* Function prototypes */
void init_cache();
void add_page(cache_p *page);
void delete_page(cache_p *page, int eliminate);
cache_p *is_hit(char *uri);
cache_p *new_page(char *host_uri, char *host_payload, unsigned long size);

#endif

