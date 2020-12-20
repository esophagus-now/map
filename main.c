#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map.h"
#include "list.h"

void print_map(MAP_PTR_PARAM(char *, int, my_param)) {
    __map_metadata *md = (*my_param)->md;
    list_head *head = md->entries + md->list_head_off;

    list_head *cur;
    int count = 0;
    for (cur = head->next; cur != head; cur = cur->next) {
        void *entry = ((void*)cur) - md->list_head_off;
        printf("Key %s ", *(char const**) (entry+md->key_off));
        printf("Value %d\n", *(int*) (entry+md->val_off));
        count++;
    }

    printf("Total count: %d\n", count);
}

uint32_t deref_str_hash(void const *p) {
    char const **pstr = (char const **)p;
    char const *str = *pstr;

    uint32_t hash = 0xA5A5A5A5;

    while(str && *str) {
        hash = hash*147 + *str++;
    }

    return hash;
}

int deref_strcmp(void const *a, void const *b) {
    char const **pa = (char const**) a;
    char const **pb = (char const**) b;
    return strcmp(*pa, *pb);
}

void deref_free(void *v) {
    void *to_free = *(void**)v;
    free(to_free);
}

int main(void) {
    MAP_DECL(char *, int, map);
    map_init(map, STR2VAL);

    print_map(MAP_ARG(map));

    char cmd[32];

    while (scanf("%31s", cmd) > 0) {
        if (!strcmp(cmd, "set")) {
            char word[32];
            int val;
            scanf("%31s%d", word, &val);
            char *copied = strdup(word);
            int rc = map_insert(&map, copied, 1, &val, 0);
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
            int *val = map_search(&map, word);
            if (val) {
                printf("%d\n", *val);
            } else {
                printf("(null)\n");
            }
        } else if (!strcmp(cmd, "delk")) {
            char word[32];
            scanf("%31s", word);
            int rc = map_search_delete(&map, word, NULL);
            if (rc == 0) {
                puts("Deleted");
            } else {
                puts("Not found");
            }
        } else if (!strcmp(cmd, "print")) {
            print_map(MAP_ARG(map));
        } else {
            int *val = map_search(&map, cmd);
            if (val) {
                printf("%d\n", *val);
            } else {
                printf("(null)\n");
            }
        }
    }

    print_map(MAP_ARG(map));
    
    map_free(map);

    return 0;
}