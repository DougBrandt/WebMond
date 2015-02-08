
#ifndef __SinglyLinkedList_H_
#define __SinglyLinkedList_H_

#include <pthread.h>

struct NodeEntry {
   void *data;
   struct NodeEntry *next;
};

typedef struct NodeEntry NodeEntry;

struct LinkedList {
   pthread_mutex_t mutex;
   NodeEntry *head;
   NodeEntry *tail;
   int count;
};

typedef struct LinkedList LinkedList;

int InitLL(LinkedList **l);
int DestroyLL(LinkedList **l);

int LLClear(LinkedList *l);
void *LLGet(LinkedList *l, int idx);
int LLInsertTail(LinkedList *l, void *data);
void *LLRemoveHead(LinkedList *l);
int LLSize(LinkedList *l);

#endif // __SinglyLinkedList_H_
