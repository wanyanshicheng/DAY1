#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define InitSize 10;

typedef struct 
{
    void *data;
    int size;
    int length;
    int capacity;  
}seqlist;

int seqlist_init(seqlist **mylist, int len, int size);
int seqlist_insert(seqlist *mylist, const void *data, int index);
void *seqlist_search(const seqlist *mylist, const void *key, int(*cmp)(const void *data, const void *key));
int seqlist_delete(seqlist *mylist, const void *key, int(*cmp)(const void *data, const void *key));
void seqlist_traval(const seqlist *mylist, void(*pri)(const void *data));
void seqlist_destory(seqlist **mylist);