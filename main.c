#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map.h"
#include "list.h"

//I was getting tired of seeing that annoying warning
char *strdup(char const *);

void print_map(map const *md) {
    map_assert_type(md, char const*, int, STR2VAL);
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

int main(void) {
    map m;
    map_init(&m, char const*, int, STR2VAL);

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

    return 0;
}