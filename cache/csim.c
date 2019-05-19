/* Cache simulator by Kun Woo Yoo (kunwooy) */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include "cachelab.h"

/* represents a cache line, where usecount is the indicator for when the
 * block was used */
typedef struct {
    int valid;
    long tag;
    int block;
    int usecount; /* tells which operation used the line */
    int isdirty; /* tell whether the line is dirty */
} cache_line;

/* represents a set */
typedef struct {
    cache_line* lines;
} cache_set;

/* represents a whole cache */
typedef struct {
    cache_set* sets;
} cache;

/* represents dirty evicted/cached counter, as well as dimension of cache */
typedef struct {
    int s;
    int E;
    int b;
    int devicted;
    int dcached;
} dim;

cache* make_cache(dim* ourdim) {
    int s = ourdim->s;
    int E = ourdim->E;

    int S = 1 << s;
    cache *new_cache = (cache*) malloc(sizeof(cache));
    new_cache->sets = (cache_set*) malloc(sizeof(cache_set) * S);
    cache_line *current_line;

    /* now for each set, we create lines */
    for (int i = 0; i < S; i ++) {
	    new_cache->sets[i].lines = malloc(sizeof(cache_line) * E);
	    current_line = new_cache->sets[i].lines;
        for (int j = 0; j < E; j ++) {
            current_line[j].valid = 0;
    	    current_line[j].tag = 0;
            current_line[j].block = 0;
            current_line[j].usecount = 0;
            current_line[j].isdirty = 0;
        }
    }

    return new_cache;
}

/* free my cache */
void freecache(cache* our_cache, dim* ourdim) {
    free(ourdim);
    free(our_cache->sets->lines);
    free(our_cache->sets);
    free(our_cache);
    return;
}

/* isHit returns 1 when an op hits, 0 when it misses, and -1 when evicted */
int isHit (cache_set* targetset, long addrtag, dim* ourdim, int opnum) {
    cache_line *targetline = targetset->lines;
    int max_lines = ourdim->E;
    int checkfull = 0 ;/* counter to check whether a set is full or not */

    /* now check all lines in a set */
    for (int i = 0; i < max_lines; i++) {
    	if (targetline[i].tag == addrtag) {
            if (targetline[i].valid == 1) {
                targetline[i].usecount = opnum;
                return 1;
            }
	    }
	    checkfull += (targetline[i].valid);
    }
    /* set is full when all lines are valid */
    if (checkfull == max_lines) {
	    return -1;
    } else { /*when set is not full, then miss */
        return 0;
    }
}

/* updatecache updates a cache after a trace */
void updatecache (cache_set* targetset, long addrtag, dim* ourdim, int opnum,
                  int didHit) {
    cache_line *targetline = targetset->lines;
    int max_lines = ourdim->E;
    /* if hit, return */
    if (didHit == 1) return;

    /* if miss and not full, then update */
    if (didHit == 0) {
        for (int i = 0; i < max_lines; i++) {
            if (targetline[i].valid == 0) {
                targetline[i].tag = addrtag;
                targetline[i].valid = 1;
                targetline[i].usecount = opnum;
                return;
            }
        }
    }

    /* if miss and full, then evict and update */
    /* first, find what should be evicted */
    int lru = opnum;
    int lru_line;
    if (didHit == -1) {
        for (int j = 0; j < max_lines; j++) {
            if (targetline[j].usecount < lru) {
                lru = targetline[j].usecount;
	        lru_line = j;
            }
        }
    }

    /* now, evict the LRU line and change values */
    cache_line *lruline = &targetline[lru_line];
    lruline->valid = 1;
    lruline->usecount = opnum;
    lruline->tag = addrtag;
    return;
}

/* markdirty marks the isdirty bit when necessary,
 * as well as changes dirtybytes values */
void markdirty (cache_set* targetset, long addrtag, dim* ourdim, int opnum,
                char op, int didHit) {
    int max_lines = ourdim->E;
    int wasdirty;
    cache_line *targetline;

    /* first, find what which line needs to be modified using opnum */
    for (int i = 0; i < max_lines; i++) {
        if (targetset->lines[i].usecount == opnum) {
            wasdirty = targetset->lines[i].isdirty;
            targetline = &(targetset->lines[i]);
        }
    }

    /* if it was a Save op */
    if (op == 'S') {
        targetline->isdirty = 1;
        ourdim->dcached += (1 - wasdirty);
        if (didHit < 1) {
            ourdim->devicted += (wasdirty);
        }
    } else { /* if it's a Load op */
        if (didHit < 1) {
            targetline->isdirty = 0;
            ourdim->dcached -= (wasdirty);
            ourdim->devicted += (wasdirty);
        }
    }
    return;
}




int main(int argc, char* argv[]) {
    dim *ourdim = (dim*) malloc(sizeof(dim));
    char* trace_name;
    int i;
    /* get the argument and dimensions using getop */
    while ((i = getopt(argc, argv, "s:E:b:t:")) != -1) {
        switch(i) {

            case('s'):
                ourdim->s = atoi(optarg);
                break;

            case('E'):
	        ourdim->E = atoi(optarg);
	        break;

            case('b'):
	        ourdim->b = atoi(optarg);
                break;

            case('t'):
                trace_name = optarg;
                break;
            default:
                exit(1);
    	}
    }

    /* start by making an empty cache */
    cache *new_cache = make_cache(ourdim);

    /* declare variables for trace inputs */
    char op;
    long unsigned address;
    int size;

    long setindex;
    long addrtag;
    int hit = 0;
    int miss = 0;
    int evict = 0;
    int opnum = 0;
    ourdim->devicted = 0;
    ourdim->dcached = 0;

    int s = ourdim->s;
    int b = ourdim->b;
    long setmask = ((1 << (s + b)) - 1) - ((1 << b) - 1);
    long tagmask = (~0) - ((1 << (s+b)) - 1);
    cache_set *targetset;
    int didHit;

    /* now start reading in */
    FILE *traces = fopen(trace_name, "r");
    while (fscanf(traces, " %c %lx,%d", &op, &address, &size) == 3) {
	    setindex = (address & setmask) >> b;
        addrtag = (long) ((unsigned long) (address & tagmask)) >> (s+b);
        targetset = &(new_cache->sets[setindex]);
        didHit = isHit(targetset, addrtag, ourdim, opnum);

        /* if the operation is hit */
        if (didHit == 1) {
            updatecache(targetset, addrtag, ourdim, opnum, didHit);
            hit++;
	    }

        /* if missed */
	    if (didHit == 0) {
	        updatecache(targetset, addrtag, ourdim, opnum, didHit);
	        miss++;
        }

        /* if missed, and needs to be evicted */
	    if (didHit == -1) {
    	    updatecache(targetset, addrtag, ourdim, opnum, didHit);
            miss++;
	        evict++;
	    }
        /* finally, handle dirty bytes */
        markdirty(targetset, addrtag, ourdim, opnum, op, didHit);
	    opnum++;
    }
    int d_cached = (ourdim->dcached) * (1 << b);
    int d_evicted = (ourdim->devicted) * (1 << b);
    printSummary(hit, miss, evict, d_cached, d_evicted);
    freecache(new_cache, ourdim);
    return 0;
}



