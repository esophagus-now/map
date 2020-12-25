#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "map.h"
#include "list.h"
#include "vector.h"

//I was getting tired of seeing that annoying warning
char *strdup(char const *);

void print_map(map const *md) {
    map_assert_type(md, char const*, uint32_t, STR2VAL);

    map_iter it;
    int count = 0;
    for (it = map_begin(md); it != map_end(md); map_iter_step(it)) {
        char const *key;
        uint32_t val;
        map_iter_deref(md, it, &key, &val);
        printf("Key %s ", key);
        printf("Value %d\n", val);
        count++;
    }

    printf("Total count: %d\n", count);
}

typedef enum {
    MAP_SET,

} map_op_t;

typedef struct {
    map_op_t op;
    char const *str;
    uint32_t hash;
} map_op;

int main(void) {
    map m;
    map_init(&m, char const*, uint32_t, STR2VAL);

    print_map(&m);

    char cmd[32];

    while (scanf("%31s", cmd) > 0) {
        if (!strcmp(cmd, "set")) {
            char word[32];
            int val;
            scanf("%31s%d", word, &val);
            char *copied = strdup(word);
            int rc = map_insert(&m, copied, 1, &val, 0);
            if (rc < 0) {
                puts("Full");
                free(copied);
            } else if (rc == 1) {
                puts("Overwritten");
                free(copied);
            } else {
                puts("Written");
            }
        } else if (!strcmp(cmd, "get")) {
            char word[32];
            scanf("%31s", word);
            int *val = map_search(&m, word);
            if (val) {
                printf("%d\n", *val);
            } else {
                printf("(null)\n");
            }
        } else if (!strcmp(cmd, "delk")) {
            char word[32];
            scanf("%31s", word);
            int rc = map_search_delete(&m, word, NULL);
            if (rc == 0) {
                puts("Deleted");
            } else {
                puts("Not found");
            }
        } else if (!strcmp(cmd, "print")) {
            print_map(&m);
        } else {
            int *val = map_search(&m, cmd);
            if (val) {
                printf("%d\n", *val);
            } else {
                printf("(null)\n");
            }
        }
    }

    print_map(&m);
    
    map_free(&m);

    /*
    VECTOR_DECL(map_op, inputs);
    vector_init(inputs);

    printf("%d\n", inputs_cap);

    vector_free(inputs);

    map_free(&m);
    */
    return 0;
}