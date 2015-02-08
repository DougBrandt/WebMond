/*
 *	Singly Linked List Data Structure implemented in C
 *
 */

#include <stdlib.h>
#include <assert.h>

#include "singlyLinkedList.h"

int InitLL(LinkedList **l)
{
   assert(l);

   *l = calloc(1, sizeof (LinkedList));
   if (!*l)
      return -1;
	
   (*l)->head = NULL;
   (*l)->tail = NULL;
   (*l)->count = 0;

   if (pthread_mutex_init(&((*l)->mutex), NULL) != 0) {
      return -1;
   }

   return 0;
}

int DestroyLL(LinkedList **l)
{
   NodeEntry *cur;
   assert(l);
   assert(*l);

   while ((*l)->head) {
      cur = (*l)->head;
      (*l)->head = (*l)->head->next;
      free(cur->data);
      cur->data = NULL;
      free(cur);
      cur = NULL;
   }

   if (pthread_mutex_destroy(&((*l)->mutex)) != 0) {
      return -1;
   }

   free(*l);
   *l = NULL;

   return 0;
}

int LLClear(LinkedList *l)
{
   NodeEntry *cur;
   assert(l);

   while (l->head) {
      cur = l->head;
      l->head = l->head->next;
      free(cur->data);
      cur->data = NULL;
      free(cur);
      cur = NULL;
   }

   l->head = NULL;
   l->tail = NULL;
   l->count = 0;

   return 0;
}

void *LLGet(LinkedList *l, int idx)
{
   int i = 0;
   NodeEntry *cur;
   void *ret = NULL;
   assert(l);
   assert(idx >= 0);

   cur = l->head;

   while (cur && i < idx) {
      cur = cur->next;
      i++;
   }

   if (cur)
      ret = cur->data;

   return ret;
}

int LLInsertTail(LinkedList *l, void *data)
{
   NodeEntry *newNode = NULL;
   assert(l);
   assert(data);

   newNode = calloc(1, sizeof (NodeEntry));
   if (!newNode)
      return -1;

   newNode->data = data;
   newNode->next = NULL;
   if (l->tail)
      l->tail->next = newNode;
   if (l->count == 0)
      l->head = newNode;
   l->tail = newNode;

   l->count++;

   return 0;
}

void *LLRemoveHead(LinkedList *l)
{
   void *ret = NULL;
   NodeEntry *cur;
   assert(l);

   if (l->head) {
      ret = l->head->data;
      cur = l->head;
      l->head = l->head->next;
      l->count--;
      free(cur);
      cur = NULL;
   }

   return ret;
}

int LLSize(LinkedList *l)
{
   assert(l);

   return l->count;
}

