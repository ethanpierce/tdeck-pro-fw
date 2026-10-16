#pragma once
#include <vector>
#include <deque>
#include <cstring>
struct FakeQueue {size_t limit,itemSize;std::deque<std::vector<unsigned char>> data;};
using QueueHandle_t=FakeQueue*;
inline QueueHandle_t xQueueCreate(size_t n,size_t s) {return new FakeQueue{n,s,{}};}
inline void vQueueDelete(QueueHandle_t q) {delete q;}
inline int xQueueSend(QueueHandle_t q,const void* p,uint32_t) {if(q->data.size()==q->limit)return 0;q->data.emplace_back(q->itemSize);memcpy(q->data.back().data(),p,q->itemSize);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* p,uint32_t) {if(q->data.empty())return 0;memcpy(p,q->data.front().data(),q->itemSize);q->data.pop_front();return 1;}
