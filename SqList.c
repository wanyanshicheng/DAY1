#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct seqlist {
    void *data;         // 数据区
    int size;           // 每个元素的大小
    int capacity;       // 当前容量
    int length;         // 当前长度
} seqlist;

// 初始化顺序表
int seqlist_init(seqlist **mylist, int len, int size) {
    if (mylist == NULL || len <= 0 || size <= 0) {
        return -1;
    }
    
    *mylist = (seqlist *)malloc(sizeof(seqlist));
    if (*mylist == NULL) {
        return -1;
    }
    
    (*mylist)->data = malloc(len * size);
    if ((*mylist)->data == NULL) {
        free(*mylist);
        *mylist = NULL;
        return -1;
    }
    
    (*mylist)->size = size;
    (*mylist)->capacity = len;
    (*mylist)->length = 0;
    
    return 0;
}

// 插入元素
int seqlist_insert(seqlist *mylist, const void *data, int index) {
    if (mylist == NULL || data == NULL || index < 0 || index > mylist->length) {
        return -1;
    }
    
    // 检查是否需要扩容
    if (mylist->length >= mylist->capacity) {
        int new_capacity = mylist->capacity * 2;
        void *new_data = realloc(mylist->data, new_capacity * mylist->size);
        if (new_data == NULL) {
            return -1;
        }
        mylist->data = new_data;
        mylist->capacity = new_capacity;
    }
    
    // 移动元素
    if (index < mylist->length) {
        memmove((char *)mylist->data + (index + 1) * mylist->size,
                (char *)mylist->data + index * mylist->size,
                (mylist->length - index) * mylist->size);
    }
    
    // 插入新元素
    memcpy((char *)mylist->data + index * mylist->size, data, mylist->size);
    mylist->length++;
    
    return 0;
}

// 查找元素
void *seqlist_search(const seqlist *mylist, const void *key, int(*cmp)(const void *data, const void *key)) {
    if (mylist == NULL || key == NULL || cmp == NULL) {
        return NULL;
    }
    
    for (int i = 0; i < mylist->length; i++) {
        if (cmp((char *)mylist->data + i * mylist->size, key) == 0) {
            return (char *)mylist->data + i * mylist->size;
        }
    }
    
    return NULL;
}

// 删除元素
int seqlist_delete(seqlist *mylist, const void *key, int(*cmp)(const void *data, const void *key)) {
    if (mylist == NULL || key == NULL || cmp == NULL) {
        return -1;
    }
    
    for (int i = 0; i < mylist->length; i++) {
        if (cmp((char *)mylist->data + i * mylist->size, key) == 0) {
            // 移动元素覆盖要删除的元素
            if (i < mylist->length - 1) {
                memmove((char *)mylist->data + i * mylist->size,
                        (char *)mylist->data + (i + 1) * mylist->size,
                        (mylist->length - i - 1) * mylist->size);
            }
            mylist->length--;
            return 0;  // 只删除第一个匹配的元素
        }
    }
    
    return -1;  // 未找到匹配元素
}

// 遍历元素
void seqlist_traval(const seqlist *mylist, void(*pri)(const void *data)) {
    if (mylist == NULL || pri == NULL) {
        return;
    }
    
    for (int i = 0; i < mylist->length; i++) {
        pri((char *)mylist->data + i * mylist->size);
    }
}

// 销毁顺序表
void seqlist_destory(seqlist **mylist) {
    if (mylist == NULL || *mylist == NULL) {
        return;
    }
    
    if ((*mylist)->data != NULL) {
        free((*mylist)->data);
    }
    
    free(*mylist);
    *mylist = NULL;
}