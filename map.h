#ifndef MAP_H
#define MAP_H 1

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "list.h"
#include "fast_fail.h"

typedef uint32_t map_hash_fn(void const *, unsigned);
uint32_t map_val_hash(void const *a, unsigned sz);
uint32_t map_ptr_hash(void const *a, unsigned sz);
uint32_t map_str_hash(void const *a, unsigned sz);

typedef int map_comp_fn(void const *, void const *, unsigned);
int map_val_comp(void const *a, void const *b, unsigned sz);
int map_ptr_comp(void const *a, void const *b, unsigned sz);
int map_str_comp(void const *a, void const *b, unsigned sz);

typedef void map_free_fn(void *);
void map_val_free(void *a); //This is technically not needed
void map_ptr_free(void *a);
#define map_str_free map_ptr_free

typedef struct {
    unsigned    is_filled   :1;
    unsigned    is_last     :1;
    unsigned    free_key    :1;
    unsigned    free_val    :1;
} __entry_flags;

typedef struct {
    uint32_t slots; //Does not include sentinel

    //Could have kept head of list of full nodes here,
    //but the sentinel already has space for it (and 
    //we get a benefit when it comes to managing flags).
    list_head empties; //Linked list of empty nodes
    //OTOH, I didn't want to put both lists into the
    //sentinel because I don't want the size of the 
    //sentinel to be bigger than the size of a record

    //Specific functions needed to manage this map.
    //It is possible for the user to define custom 
    //functions.
    map_hash_fn *hash;
    map_comp_fn *key_comp;
    map_comp_fn *val_comp;
    void (*key_free)(void *);
    void (*val_free)(void *);

    //These are little pre-computed helpers to slightly
    //nicen up the interface. The reason is because we
    //want to use pointers and memcpy when the items in 
    //the map are stored values, but we just want to use 
    //simple assignment when the items stored in the map 
    //are pointers.
    //The reason is NOT for performance; actually, we
    //probably lose some small amount of performance for 
    //doing this. Instead, the problem is that if we use 
    //the same pointer+memcpy approach in both cases, you 
    //end up having to pass a pointer-to-pointer.
    //
    //Example, a map from a big value type to another big value type:
    //  struct my_big_struct big_boy;
    //  my_big_struct_init(&big_boy);
    //  struct another_struct bigger_boi;
    //  another_struct_init(&bigger_boi);
    //
    //  MAP_DECL(struct my_big_struct, struct another_struct, my_map);
    //  map_init(
    //      my_map, 
    //      map_val_hash, 
    //      map_val_comp, 
    //      map_val_comp, 
    //      map_val_free, 
    //      map_val_free
    //  );
    //
    //  map_insert(my_map, &big_boy, 0, &bigger_boi, 0);
    //
    //In the above example, the correct thing happens: the big structs
    //are passed by reference (much better than passing by value), and 
    //then memcpy'd into the entry in the hash map.
    //
    //Now consider a different example (but clearly much more common)
    //where we want to map strings to our big struct:
    //
    //  struct another_struct bigger_boi;
    //  another_struct_init(&bigger_boi);
    //
    //  MAP_DECL(char const *, struct another_struct, my_map);
    //  map_init(
    //      my_map, 
    //      map_str_hash, 
    //      map_str_comp, 
    //      map_val_comp, 
    //      map_str_free, 
    //      map_val_free
    //  );
    //  
    //  char str[32]; //This stack variable will be freed, so we want to 
    //                //copy it into the hash map
    //  scanf("%31s", str); //Or whatever
    //
    //  //map_insert(my_map, strdup(str), 1, &bigger_boi, 0); //Can't do this!
    //  char *copied_str = strdup(str);
    //  map_insert(my_map, &copied_str, 1, &bigger_boi, 0); //Gross
    //
    //To explain what's happening here, the entry in the hash map is storing
    //a pointer to const char. Thus, if we want to write to it using memcpy,
    //we must take the address of that pointer, and for the same reason, we 
    //must take the address of the pointer we're trying to copy from. Worst
    //of all, because we have to take the address of the char const* we're 
    //trying to copy, it must be an l-value. It's enough of a hassle that I 
    //decided to add a runtime cost just to prevent having to do this kind of 
    //ugly business in the calling code.
    int key_is_ptr;
    int val_is_ptr;

    //Remember: first entry is sentinel
    void *entries;
    unsigned entry_sz;

    //C++ templates would have prevented this mess. Sadly,
    //I need all of this information for the generic map 
    //management functions.
    //Couldn't find a way to make this const. Oh well, we'll
    //lose some optimizations
    unsigned list_head_off;
    unsigned flag_off;
    unsigned key_off;
    unsigned key_sz;
    unsigned val_off;
    unsigned val_sz;
} map;

#define MAP_STRUCT(ktype, vtype) \
struct {                         \
    list_head entry_list;        \
    __entry_flags flags;         \
    ktype key;                   \
    vtype val;                   \
}

//Internal function that sets up the free list of entries.
void __map_init_entries(map *md);

//Some helpers to make map_init a little friendlier
#define VAL2VAL map_val_hash,map_val_comp,map_val_comp,map_val_free,map_val_free,sizeof(entries->key),sizeof(entries->val)
#define VAL2PTR map_val_hash,map_val_comp,map_ptr_comp,map_val_free,map_ptr_free,sizeof(entries->key),sizeof(*entries->val)
#define VAL2STR map_val_hash,map_val_comp,map_str_comp,map_val_free,map_str_free,sizeof(entries->key),0
#define PTR2VAL map_ptr_hash,map_ptr_comp,map_val_comp,map_ptr_free,map_val_free,sizeof(*entries->key),sizeof(entries->val)
#define PTR2PTR map_ptr_hash,map_ptr_comp,map_ptr_comp,map_ptr_free,map_ptr_free,sizeof(*entries->key),sizeof(*entries->val)
#define PTR2STR map_ptr_hash,map_ptr_comp,map_str_comp,map_ptr_free,map_str_free,sizeof(*entries->key),0
#define STR2VAL map_str_hash,map_str_comp,map_val_comp,map_str_free,map_val_free,0,sizeof(entries->val)
#define STR2PTR map_str_hash,map_str_comp,map_ptr_comp,map_str_free,map_ptr_free,0,sizeof(*entries->val)
#define STR2STR map_str_hash,map_str_comp,map_str_comp,map_str_free,map_str_free,0,0

//https://stackoverflow.com/questions/29962560/understanding-defer-and-obstruct-macros/30009264
#define EMPTY()
#define DEFER(id) id EMPTY()
#define EXPAND(...) __VA_ARGS__
#define map_init(m,ktype,vtype,x) EXPAND(DEFER(map_custom_init)(m,ktype,vtype,x))

//Sadly, standard offsetof requires an actual type name, so we need this 
//dirty, dirty hack.
#define anon_offsetof(ptr,member) \
    ((void*)(&(ptr)->member) - (void*)(ptr))

#define MAP_INIT_SZ 4
//Does not free existing map data. Sadly, we have the same 
//problem as qsort that we can't type-check the given
//function pointers (i.e. their arguments have to all be 
//void pointers instead of pointers to the specific types).
//Would have been nicer as a callable function to avoid this 
//inlining penalty, so maybe I'll change that later on. The 
//downside is that all those sizeofs and offsets need to be 
//passed into the function, which means a lot of parameters
#define map_custom_init(m,ktype,vtype,hsh,kcmp,vcmp,kfree,vfree,ksz,vsz) \
do {                                                                     \
    MAP_STRUCT(ktype,vtype) *entries =                                   \
        calloc(MAP_INIT_SZ,sizeof(*entries));                            \
    if (!entries) FAST_FAIL("out of memory");                            \
    list_head *fulls = &entries->entry_list;                             \
    fulls->next = fulls;                                                 \
    fulls->prev = fulls;                                                 \
                                                                         \
    *(m) = (map) {                                                       \
        .slots = MAP_INIT_SZ - 1,                                        \
                                                                         \
        .hash = hsh,                                                     \
        .key_comp = kcmp,                                                \
        .val_comp = vcmp,                                                \
        .key_free = kfree,                                               \
        .val_free = vfree,                                               \
                                                                         \
        .key_is_ptr = (kcmp==map_ptr_comp||kcmp==map_str_comp),          \
        .val_is_ptr = (vcmp==map_ptr_comp||vcmp==map_str_comp),          \
                                                                         \
        .entries = entries,                                              \
        .entry_sz = sizeof(*entries),                                    \
                                                                         \
        .list_head_off = anon_offsetof(entries,entry_list),              \
        .flag_off = anon_offsetof(entries,flags),                        \
        .key_off = anon_offsetof(entries,key),                           \
        .key_sz = ksz,                                                   \
        .val_off = anon_offsetof(entries,val),                           \
        .val_sz = vsz,                                                   \
    };                                                                   \
                                                                         \
    __map_init_entries(m);                                               \
} while(0)

//Traverses entire list and checks if any of the keys/values should
//be freed. TODO? Have a fast version that assumes no nodes need to 
//be freed?
void map_free(map *md);

//Returns NULL if not found, or pointer to value if found
void *map_search(map const *md, void const *key);

//Returns 0 on success, 1 if previous value overwritten,
//or negative on error
int map_insert(
    map *md, 
    void const *k, int free_key,
    void const *v, int free_val
);

//Searches for either k_needle or v_needle depending on which one 
//is not NULL. If both are given, will search using key but will also 
//make sure value matches. Returns 0 if entry was deleted, 1 if it 
//wasn't found, or negative on error
int map_search_delete(map *md, void const *k_needle, void const *v_needle);

//Some little helper macros
#define map_full(m) (list_empty(&(m)->empties))
#define __map_first_free_entry(m) ((m)->empties.next)
//Confirmed that these add no overhead when compiling with -O2
//(thanks, Godbolt!)
#define RV_AMP(x) ((__typeof__(x)[1]){x})
#define STR_AMP(x) ((char const*[1]){(char const*)(x)})

//TODO: is there any nice way to deal with key_is_ptr? 
#define ASSERT_MAP_TYPE(m, ktype, vtype)                           \
do {                                                               \
    MAP_STRUCT(ktype,vtype) *dummy = NULL;                         \
    assert((m)->entry_sz == sizeof(*dummy));                       \
    assert((m)->list_head_off == anon_offsetof(dummy,entry_list)); \
    assert((m)->flag_off == anon_offsetof(dummy,flags));           \
    assert((m)->key_off == anon_offsetof(dummy,key));              \
    assert((m)->val_off == anon_offsetof(dummy,val));              \
} while(0)

#endif