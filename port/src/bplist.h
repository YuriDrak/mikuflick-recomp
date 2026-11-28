/*
 * Minimal Apple binary property list (bplist00) reader. Plain C, no SDL, so the
 * setup tool (tools/mfextract) can use it too.
 */
#ifndef BPLIST_H
#define BPLIST_H

#include <stddef.h>
#include <stdint.h>

typedef enum { BP_NULL, BP_BOOL, BP_INT, BP_REAL, BP_STR, BP_DATA, BP_ARRAY, BP_DICT, BP_UID } BpType;
typedef struct BpNode {
    BpType type;
    int64_t i;               /* BP_BOOL, BP_INT, BP_UID (NSKeyedArchiver object reference) */
    double r;
    char *s;                 /* BP_STR (UTF-8), BP_DATA (raw) */
    size_t n;                /* bytes for STR/DATA, items for ARRAY/DICT */
    struct BpNode **items;   /* ARRAY values, DICT values */
    char **keys;             /* DICT keys */
} BpNode;

BpNode *bp_parse(const uint8_t *data, size_t len);
void bp_free(BpNode *n);
BpNode *bp_get(const BpNode *dict, const char *key);
double bp_num(const BpNode *n);

#endif
