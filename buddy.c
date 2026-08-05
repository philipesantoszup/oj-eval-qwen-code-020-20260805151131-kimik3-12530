#include "buddy.h"
#include <stdlib.h>

#define NULL ((void *)0)

#define MAXRANK 16
#define PAGE_SIZE 4096L
#define ENOMEM 12

static char *pool_base;
static long pool_pages;

static int *page_rank;        /* rank of the block containing each page */
static long *fl_prev, *fl_next; /* free-list links (valid for block head pages) */
static unsigned char *blk_alloc; /* 1 if the block headed by this page is allocated */
static long free_head[MAXRANK + 1];
static long free_cnt[MAXRANK + 1];

static int rank_pages(int r) { return 1 << (r - 1); }

static void set_rank(long b, int r)
{
    long i, n = rank_pages(r);
    for (i = 0; i < n; i++)
        page_rank[b + i] = r;
}

static void list_remove(long b, int r)
{
    long pv = fl_prev[b], nx = fl_next[b];
    if (pv >= 0)
        fl_next[pv] = nx;
    else
        free_head[r] = nx;
    if (nx >= 0)
        fl_prev[nx] = pv;
    fl_prev[b] = fl_next[b] = -1;
    free_cnt[r]--;
}

static void list_push(long b, int r)
{
    fl_prev[b] = -1;
    fl_next[b] = free_head[r];
    if (free_head[r] >= 0)
        fl_prev[free_head[r]] = b;
    free_head[r] = b;
    free_cnt[r]++;
}

int init_page(void *p, int pgcount)
{
    long pos;
    int r;

    if (!p || pgcount <= 0)
        return -EINVAL;

    free(page_rank);
    free(fl_prev);
    free(fl_next);
    free(blk_alloc);
    page_rank = NULL;
    fl_prev = NULL;
    fl_next = NULL;
    blk_alloc = NULL;
    pool_base = NULL;
    pool_pages = 0;

    page_rank = malloc(sizeof(int) * (size_t)pgcount);
    fl_prev = malloc(sizeof(long) * (size_t)pgcount);
    fl_next = malloc(sizeof(long) * (size_t)pgcount);
    blk_alloc = malloc((size_t)pgcount);
    if (!page_rank || !fl_prev || !fl_next || !blk_alloc) {
        free(page_rank);
        free(fl_prev);
        free(fl_next);
        free(blk_alloc);
        page_rank = NULL;
        fl_prev = NULL;
        fl_next = NULL;
        blk_alloc = NULL;
        return -ENOMEM;
    }

    pool_base = (char *)p;
    pool_pages = pgcount;
    for (r = 1; r <= MAXRANK; r++) {
        free_head[r] = -1;
        free_cnt[r] = 0;
    }

    /* decompose the pool into aligned power-of-2 blocks */
    pos = 0;
    while (pos < pool_pages) {
        long remaining = pool_pages - pos;
        long sz = 1;
        int br = 1;
        while ((sz << 1) <= remaining &&
               (sz << 1) <= (1L << (MAXRANK - 1)) &&
               (pos % (sz << 1)) == 0) {
            sz <<= 1;
            br++;
        }
        set_rank(pos, br);
        blk_alloc[pos] = 0;
        list_push(pos, br);
        pos += sz;
    }
    return OK;
}

void *alloc_pages(int rank)
{
    int k;
    long b;

    if (rank < 1 || rank > MAXRANK)
        return ERR_PTR(-EINVAL);
    if (!pool_base)
        return ERR_PTR(-ENOSPC);

    for (k = rank; k <= MAXRANK && free_head[k] < 0; k++)
        ;
    if (k > MAXRANK)
        return ERR_PTR(-ENOSPC);

    b = free_head[k];
    list_remove(b, k);
    while (k > rank) {
        long buddy;
        k--;
        buddy = b + rank_pages(k);
        set_rank(buddy, k);
        blk_alloc[buddy] = 0;
        list_push(buddy, k);
    }
    set_rank(b, rank);
    blk_alloc[b] = 1;
    return (void *)(pool_base + b * PAGE_SIZE);
}

int return_pages(void *p)
{
    long off, b;
    int r;

    if (!pool_base || !p)
        return -EINVAL;
    off = (char *)p - pool_base;
    if (off < 0 || off >= pool_pages * PAGE_SIZE || off % PAGE_SIZE != 0)
        return -EINVAL;
    b = off / PAGE_SIZE;
    r = page_rank[b];
    if ((b & (long)(rank_pages(r) - 1)) != 0)
        return -EINVAL;
    if (blk_alloc[b] != 1)
        return -EINVAL;

    blk_alloc[b] = 0;
    while (r < MAXRANK) {
        long buddy = b ^ (long)rank_pages(r);
        if (buddy >= pool_pages || page_rank[buddy] != r || blk_alloc[buddy])
            break;
        list_remove(buddy, r);
        if (buddy < b)
            b = buddy;
        r++;
    }
    set_rank(b, r);
    list_push(b, r);
    return OK;
}

int query_ranks(void *p)
{
    long off;

    if (!pool_base || !p)
        return -EINVAL;
    off = (char *)p - pool_base;
    if (off < 0 || off >= pool_pages * PAGE_SIZE || off % PAGE_SIZE != 0)
        return -EINVAL;
    return page_rank[off / PAGE_SIZE];
}

int query_page_counts(int rank)
{
    if (rank < 1 || rank > MAXRANK)
        return -EINVAL;
    return (int)free_cnt[rank];
}
