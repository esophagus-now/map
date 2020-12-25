#include <stdlib.h>
#include <string.h>

#include "map.h"
#include "list.h"

uint32_t map_val_hash(void const *a, unsigned sz) {
    //Is this too slow? There are so many ideas for optimization:
    // - Working on 4 bytes at a time 
    // - Sparsely sampling the value (if it's really big)
    // - A different hash function entirely
    // - (Or just use a library fn from someone else)

    uint32_t hash = 0xA5A5A5A5;

    char const *bytes = (char const *) a;

    int i;
    for (i = 0; i < sz; i++) {
        hash = hash*147 + bytes[i];
    }

    return hash;
}
uint32_t map_ptr_hash(void const *a, unsigned sz) {
    return map_val_hash(*(void const**)a, sz);
}
uint32_t map_str_hash(void const *a, unsigned sz) {
    uint32_t hash = 0xA5A5A5A5;

    char const *str = *(char const **) a;

    while (str && *str) {
        hash = hash*147 + *str++;
    }

    return hash;
}

int map_val_comp(void const *a, void const *b, unsigned sz) {
    //C compiler should be able to optimize this away this wrapper.
    //The reason to use it is to get around the compiler warnings and 
    //link-time headaches.

    return memcmp(a, b, sz);
}
int map_ptr_comp(void const *a, void const *b, unsigned sz) {
    return map_val_comp(*(void const**) a, *(void const**) b, sz);
}
int map_str_comp(void const *a, void const *b, unsigned sz) {
    //Again: this wrapper should be able to go away
    return strcmp(*(char const**)a, *(char const**)b);
}

void map_val_free(void *a) {
    fprintf(stderr, "Warning: trying to free a value");
}
void map_ptr_free(void *a) {
    //I hope the C compiler makes the wrapper go away!
    free(*(void **)a);
}

//Internal function that sets up the free list of entries. A 
//little more streamlined to manually manage prev and next.
void __map_init_entries(map *md) {
    list_head *head = &md->empties;
    //Get the list_head from the first (non-sentinel) entry
    head->next = md->entries + md->entry_sz + md->list_head_off;
    
    list_head *cur = head->next;
    list_head *prev = head;

    //Do the first slots-1 entries
    int i;
    for (i = 0; i < md->slots - 1; i++) {
        list_head *next = (void*)cur + md->entry_sz;
        cur->prev = prev;
        cur->next = next;
        prev = cur;
        cur = next;
    }

    //The last entry needs to loop back around
    cur->prev = prev;
    cur->next = head;
    head->prev = cur;
}

//Returns NULL if not found, or pointer to value if found
void *map_search(map const* md, void const* k) {
    //See the big comment in the __map_metadata struct. This 
    //is the trick that lets us avoid dealing with pointers-
    //to-pointers.
    void const *pk = md->key_is_ptr ? &k : k;

    uint32_t idx = (md->hash(pk, md->key_sz) % md->slots) + 1;
    
    void *cur_entry = md->entries + md->entry_sz*idx;

    __entry_flags *flags = cur_entry + md->flag_off;

    //Return early if this entry is empty
    if (!flags->is_filled) {
        return NULL;
    }
    
    list_head *list_head_from_entry = cur_entry + md->list_head_off;

    //Search through the bucket
    while(1) {
        void const *key_from_entry = cur_entry + md->key_off;

        //If this matches the key, we're done
        if (md->key_comp(key_from_entry, pk, md->key_sz) == 0) {
            return cur_entry + md->val_off;
        }

        if (flags->is_last) break;

        //Otherwise, step all our variables to the next entry
        list_head_from_entry = list_head_from_entry->next;
        cur_entry = list_head_from_entry - md->list_head_off;
        flags = cur_entry + md->flag_off;
    }

    return NULL;
}

//Traverses entire list and checks if any of the keys/values should
//be freed. TODO? Have a fast version that assumes no nodes need to 
//be freed?
void map_free(map *md) {
    //Sentinel (first entry in array) is the head of the 
    //list of full nodes
    list_head *head = md->entries + md->list_head_off;

    //Free all the nodes in the list.
    //Note: No need to manage linked list pointers since 
    //these will all get freed anyway
    list_head *cur;
    for (cur = head->next; cur != head; cur = cur->next) {
        void *cur_entry = (void*)cur - md->list_head_off;
        __entry_flags *flags = cur_entry + md->flag_off;

        if (flags->free_key) {
            md->key_free(cur_entry + md->key_off);
        }
        if (flags->free_val) {
            md->val_free(cur_entry + md->val_off);
        }
    }

    free(md->entries);
}

static void fill_entry(
    void *e, 
    map const *md,
    void const *k, int free_key,
    void const *v, int free_val,
    int last
) {
    __entry_flags *flags = e + md->flag_off;
    flags->is_filled = 1;
    flags->is_last = last ? 1 : 0;
    flags->free_key = free_key ? 1 : 0;
    flags->free_val = free_val ? 1 : 0;
    //See the big comment in the __map_metadata struct. This 
    //is part of the trick that lets us avoid pointers-to-
    //pointers. 
    unsigned key_sz = (md->key_is_ptr) ? sizeof(void*) : md->key_sz;
    unsigned val_sz = (md->val_is_ptr) ? sizeof(void*) : md->val_sz;
    memcpy(e + md->key_off, k, key_sz);
    memcpy(e + md->val_off, v, val_sz);
}

static void map_expand(map *md) {
    void *new_entries = calloc((md->slots+1)*2, md->entry_sz);
    if (!new_entries) {
        FAST_FAIL("out of memory");
    }
    //Set the filled list in the new entries to be empty 
    list_head *new_filled_list = new_entries + md->list_head_off;
    new_filled_list->prev = new_filled_list;
    new_filled_list->next = new_filled_list;

    //Copy all the filled entries to the new storage. By the way, the 
    //code in this function is much smoother ever since I put the 
    //sentinel for filled node in entries[0] (the empties sentinel 
    //used to be there)
    list_head *head = md->entries + md->list_head_off;

    void *old_entries = md->entries; //Need to keep this so we can free later
    //These need to be set so that __map_insert will work
    md->entries = new_entries;
    md->slots = 2*(md->slots+1) - 1; //Another advantage of sentinel: 2n+1 is coprime with n

    //Build the initial linked list of free nodes (note: this had to be 
    //done after setting the new entries in md)
    __map_init_entries(md);

    list_head *cur;
    for (cur = head->next; cur != head; cur = cur->next) {
        //Annoying: because of my trick involving pointers that 
        //simplifies the API, I have to "undo" already having a 
        //pointer-to-pointer
        void *entry = cur - md->list_head_off;
        __entry_flags *flags = entry + md->flag_off;
        void *k = entry + md->key_off;
        if (md->key_is_ptr) k = *(void**)k;
        void *v = entry + md->val_off;
        if (md->val_is_ptr) v = *(void**)v;
        map_insert(md, k, flags->free_key, v, flags->free_val);
    }

    //Notice we don't call the specific freeing functions on the 
    //keys and values; we just free the old memory. 
    free(old_entries);
}

//Returns 0 on success, 1 if previous value overwritten,
//or negative on error
int map_insert(
    map *md, 
    void const *k, int free_key,
    void const *v, int free_val
) {
    //See the big comment in the __map_metadata struct. This 
    //is the trick that lets us avoid dealing with pointers-
    //to-pointers.
    void const *pk = md->key_is_ptr ? &k : k;
    void const *pv = md->val_is_ptr ? &v : v;

    uint32_t idx = (md->hash(pk, md->key_sz) % md->slots) + 1;

    void *hit_by_hash = md->entries + md->entry_sz*idx;
    __entry_flags *hbh_flags = hit_by_hash + md->flag_off;
    list_head *hbh_node = hit_by_hash + md->list_head_off;

    //If the current entry is free, we can write in the 
    //new value and terminate early 
    if (!hbh_flags->is_filled) {
        //Move this node into the the linked list of 
        //filled nodes
        list_del(hbh_node);
        //Remember: sentinel (first entry in array) is head of list 
        //of filled nodes
        list_add((list_head*)(md->entries+md->list_head_off), hbh_node);
        fill_entry(hit_by_hash, md, pk, free_key, pv, free_val, 1);
        return 0;
    }

    //Search the bucket to see if this element already
    //exists
    void *cur = hit_by_hash; //Notice that we save the entry hit by the hash
    list_head *cur_node = hbh_node;
    __entry_flags *cur_flags = hbh_flags;
    while(1) {
        void *key = cur + md->key_off;
        if (!md->key_comp(key, pk, md->key_sz)) {
            //Overwrite entry and return 1
            if (cur_flags->free_key) {
                md->key_free(key);
            }
            if (cur_flags->free_val) {
                md->val_free(cur + md->val_off);
            }

            //Notice we don't modify the is_last flag
            fill_entry(cur, md, pk, free_key, pv, free_val, cur_flags->is_last);

            return 1;
        }

        if (cur_flags->is_last) break;
        cur_node = cur_node->next;
        cur = cur_node - md->list_head_off;
        cur_flags = cur + md->flag_off;
    } 

    //Item not found. 

    //The only way to insert is to use a free element
    if(map_full(md)) {
        map_expand(md);
        //So all of the work we just did has essentially gone to 
        //waste because all the memory and indices have been moved 
        //around. The simplest thing to do is to just recurse; the 
        //small amount of time we would save with a more clever
        //implementation is pretty small compared to the time we're 
        //forced to spend to rehash everything into the new table

        return map_insert(md, k, free_key, v, free_val);
    }

    list_head *free_entry_node = __map_first_free_entry(md);
    void *free_entry = free_entry_node - md->list_head_off;

    //Remove the free entry from the linked list of free nodes
    list_del(free_entry_node);

    //Is this an optimization? Instead of just putting the
    //new element into the free entry and adding that entry 
    //to the bucket, instead put the new entry into the 
    //location that was hit by the hash and move the entry 
    //that was originally there to the free entry
    //NOTE: technically, we don't need to spend the extra
    //time in the following memcpy to also copy the linked 
    //list pointers; we're going to overwrite them in a second
    //when we insert the free entry after the hit-by-hash 
    //element. 
    memcpy(free_entry, hit_by_hash, md->entry_sz);
    fill_entry(hit_by_hash, md, pk, free_key, pv, free_val, 0);


    //The situation now looks like this:
    //  (prev is the linked list item that is before hbh, and next is 
    //   the one after it. I drew them beside hbh to simplify the 
    //   diagram, but technically, they could be anywhere in the array)
    //      ______________free.prev pointer_______________
    //     /                                             |
    //  +------+-------+------+----+--------+-----+----+----+----+----+
    //  |prev <-> hbh <-> next|    |   ...  |     |    |free|    |    |
    //  +------+-------+------+----+--------+-----+----+----+----+----+
    //                     \______free.next pointer______|
    //

    //We want to insert free between hbh and next.  
    hbh_node->next->prev = free_entry_node; //next.prev = free
    hbh_node->next = free_entry_node;       //hbh.next = free
    free_entry_node->prev = hbh_node;       //free.prev = hbh

    //All done!

    return 0;
}

//If someone wants to search by value, there is no other alternative 
//than to look through everything in the map. Returns the pointer to 
//the value in the entry if found, or NULL if not found.
static list_head *find_by_value(map *md, void const *v) {
    //See the big comment in the __map_metadata struct. This 
    //is the trick that lets us avoid dealing with pointers-
    //to-pointers.
    void const *pv = md->val_is_ptr ? &v : v;

    //Remember: sentinel (first element of entries array) is the 
    //head of list of filled nodes
    list_head *head = md->entries + md->list_head_off;

    list_head *cur;
    for (cur = head->next; cur != head; cur = cur->next) {
        void *val = ((void*)cur) - md->list_head_off + md->val_off;
        if (md->val_comp(val, pv, md->val_sz)) {
            return val;
        }
    }

    return NULL;
}

//Searches for either pk_needle or pv_needle depending on which one 
//is not NULL. If both are given, will search using key but will also 
//make sure value matches. Returns 0 if entry was deleted, 1 if it 
//wasn't found, or negative on error
int map_search_delete(map *md, void const *k_needle, void const *v_needle) {
    void *found_val;

    if (k_needle) {
        found_val = map_search(md, k_needle);
        if (!found_val) return 1; //Not found

        //If the user also gave a value, make sure that the value 
        //found in this entry matches it:
        if (v_needle) {
            //See the big comment in the __map_metadata struct. This 
            //is the trick that lets us avoid dealing with pointers-
            //to-pointers.
            void const *pv = md->val_is_ptr ? &v_needle : v_needle;
            if (md->val_comp(found_val, pv, md->val_sz) != 0) {
                return 1; // Not found 
            }
        }
    } else {
        found_val = find_by_value(md, v_needle);
        if (!found_val) return 1; //Not found
    }

    //If we made it here, it's because we need to get deletin'
    void *entry = found_val - md->val_off;
    list_head *node = entry + md->list_head_off;
    __entry_flags *flags = entry + md->flag_off;

    //Compute the hash of this value before we free the key.
    //The reason we do this will become clear later.
    uint32_t hash = md->hash(entry+md->key_off, md->key_sz);
    uint32_t idx = (hash % md->slots) + 1;

    //Free key and value, if necessary
    if (flags->free_key) {
        md->key_free(entry + md->key_off);
    }
    if (flags->free_val) {
        md->val_free(found_val);
    }

    //Here's where things get a little insane. If this entry is 
    //already in the correct position to be hit by the hash, we 
    //will find the next entry in this bucket with the same hash 
    //(if there is one) and overwrite the current entry. This is 
    //because I don't want tombstones (I just don't like how 
    //"hacky" they feel, although they probably really improve 
    //performance). 
    //  -> But Marco, isn't your code hacky as hell? Well yes, 
    //     but that's because C doesn't have templates and we 
    //     are forced to use the preprocessor to try and keep
    //     things maangeable for the API. I'm half done this
    //     same implementation in C++ and I don't feel that it's
    //     as hacky.
    if (entry == md->entries + md->entry_sz*idx && !flags->is_last) {
        //Follow this bucket until we find the next element with 
        //the same index after hashing
        list_head *cur_node = node->next;
        list_head *sentinel_node = md->entries + md->list_head_off;
        while(cur_node != sentinel_node) {
            void *cur_entry = ((void*)cur_node) - md->list_head_off;
            __entry_flags *cur_flags = cur_entry + md->flag_off;

            void *pk = cur_entry + md->key_off;
            uint32_t cur_hash = md->hash(pk,md->entry_sz);
            uint32_t cur_idx = (cur_hash % md->slots) + 1;
            
            if(cur_idx == idx) {
                //Overwrite the found entry with this one
                fill_entry(
                    entry, 
                    md, 
                    pk, 
                    cur_flags->free_key, 
                    cur_entry + md->val_off,
                    cur_flags->free_val,
                    cur_flags->is_last
                );

                //Now set these variables from the outer scope
                //to point to the entry whose values were copied
                //in the previous call to fill_entry. This is 
                //because the code after this loop unconditionally
                //deletes the entry indicated by entry, node, and flags.
                entry = cur_entry;
                node = cur_node;
                flags = cur_flags;

                break;
            }

            if (cur_flags->is_last) break;

            cur_node = cur_node->next;
        }
    }


    //Now move this node over to the empty list, making sure 
    //to correctly manage the flags 
    //Note: this is why we have a sentinel as the head of the 
    //list of the filled nodes; the flags are guaranteed to 
    //be there
    list_head *prev_full = node->prev;
    __entry_flags *prev_flags = ((void*)prev_full)-md->list_head_off+md->flag_off;

    //Subtle: to understand why this always works, we need to 
    //think about all four cases 
    //Case 1. 
    //This is the last entry of a bucket with other entries. Then
    //the entry before us is now made the last entry in this bucket.
    //Case 2.
    //This is the last and only entry in a bucket, and the previous
    //node is the sentinel. Then it is safe to edit the flags in the 
    //sentinel because they are never used for anything.
    //Case 3.
    //This is the last and only entry in a bucket, and the previous
    //node is NOT the sentinel, but it IS the last entry in its own 
    //bucket. Then it is safe to mark it as last a second time.
    //Case 4.
    //This is the last and only entry in a bucket, and the previous
    //node is NOT the sentinel, and is NOT the last entry in its own
    //bucket. In this case, it is NOT correct to mark the previous 
    //node as last; however, this case is impossible (luckily).
    //Suppose that the previous entry is from another bucket, but it 
    //is not the last entry in its bucket (and thus should not be 
    //marked last). But the only way this is possible is if our node 
    //is in the same bucket as it (since it points to us). Then our 
    //node cannot be the only entry in our bucket and we have a 
    //contradiction.
    if (flags->is_last) {
        prev_flags->is_last = 1;
    }

    //Remove from list of filled nodes
    flags->is_filled = 0; 
    list_del(node);

    //Add back into list of empty nodes
    list_add(&md->empties, node);

    //Phew, done!
    
    return 0;
}