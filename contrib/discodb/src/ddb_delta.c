#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include <ddb_list.h>
#include <ddb_internal.h>

#include <ddb_delta.h>

static uint32_t allocate_bits(char **buf, uint64_t *buf_size,
                              uint32_t size_in_bits)
{
    uint32_t len = size_in_bits >> 3;
    if (size_in_bits & 7)
        len += 1;
    /* + 8 is for write_bits and read_bits which may try to access
     * at most 7 bytes out of array bounds */
    if (len + 8 > *buf_size){
        *buf_size = len + 8;
        if (!(*buf = realloc(*buf, *buf_size)))
            return 0;
    }
    memset(*buf, 0, *buf_size);
    return len;
}

static int id_cmp(const void *p1, const void *p2)
{
    const uint64_t x = *(const uint64_t*)p1;
    const uint64_t y = *(const uint64_t*)p2;

    if (x > y)
        return 1;
    else if (x < y)
        return -1;
    return 0;
}

void ddb_delta_cursor_next(struct ddb_delta_cursor *c)
{
    if (c->num_left){
        uint32_t v = read_bits(c->deltas, c->offset, c->bits);
        c->cur_id += v;
        c->offset += c->bits;
        c->num_left--;
    }
}

void ddb_delta_cursor(struct ddb_delta_cursor *c, const char *src)
{
    c->num_left = *(uint32_t*)src;
    c->cur_id = 0;

    if (c->num_left){
        c->deltas = &src[4];
        c->bits = read_bits(c->deltas, 0, 5) + 1;
        c->offset = 5;
    }
}

int ddb_delta_encode(const struct ddb_list *values,
                     char **buf,
                     uint64_t *buf_size,
                     uint64_t *size,
                     int *duplicates)
{
    uint32_t len;
    uint64_t *list = ddb_list_pointer(values, &len);

    if (!len){
        *size = 0;
        return 0;
    }

    qsort(list, len, 8, id_cmp);

    /* find maximum delta -> bits needed per id */
    uint32_t i, max_diff = list[0];
    for (i = 1; i < len; i++){
        uint32_t d = list[i] - list[i - 1];
        if (!d)
            *duplicates = 1;
        if (d > max_diff)
            max_diff = d;
    }

    uint64_t offs = 0;
    uint32_t prev = 0;
    uint32_t bits = bits_needed(max_diff);
    if (!(*size = allocate_bits(buf, buf_size, 32 + 5 + bits * len)))
        return -1;

    /* values field:
       [ num_vals (32 bits) | bits_needed (5 bits) |
         delta-encoded values (bits * num_vals) ]
    */
    memcpy(*buf, &len, 4);
    offs = 32;
    write_bits(*buf, offs, bits - 1);
    offs += 5;
    for (i = 0; i < len; i++){
        write_bits(*buf, offs, list[i] - prev);
        prev = list[i];
        offs += bits;
    }
    return 0;
}
