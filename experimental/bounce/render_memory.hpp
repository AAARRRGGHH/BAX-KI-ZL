#pragma once
#include <memory>
#include <new>
#include <vector>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
namespace sm::bounce {
// Even small cached tiles must stay out of the internal DMA/task-stack pool.
template<class T> struct RenderAllocator {
 using value_type=T;
 using is_always_equal=std::true_type;
 RenderAllocator()=default;
 template<class U> RenderAllocator(const RenderAllocator<U>&){}
 T* allocate(size_t n){
#ifdef ESP_PLATFORM
   if(n>size_t(-1)/sizeof(T))throw std::bad_alloc();
   auto* p=static_cast<T*>(heap_caps_malloc(n*sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
   if(!p)throw std::bad_alloc();
   return p;
#else
   return std::allocator<T>{}.allocate(n);
#endif
 }
 void deallocate(T* p,size_t n){
#ifdef ESP_PLATFORM
   (void)n;heap_caps_free(p);
#else
   std::allocator<T>{}.deallocate(p,n);
#endif
 }
 template<class U> bool operator==(const RenderAllocator<U>&)const{return true;}
 template<class U> bool operator!=(const RenderAllocator<U>&)const{return false;}
};
template<class T> using RenderVector=std::vector<T,RenderAllocator<T>>;
}
