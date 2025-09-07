#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define InitSize 10
typedef struct {
    int *data;
    int length;
    int MaxSize;
}Sqlist;

void intit_list(Sqlist *l){
    l->data = (int *)malloc(InitSize*sizeof(int));
    l->length = 0;
    l->MaxSize = InitSize;
}


void Increasesize(Sqlist *l,int len){
    l->data = (int *)realloc(l->data,(l->MaxSize+len)*sizeof(int));
    l->MaxSize +=len;
}

bool ListInsert(Sqlist *l,int i,int e){
    if(i<0||i>l->length)
        return false;
}