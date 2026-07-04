#pragma once

#include <cstdint>
#include <list>
#include <mutex>
#include <iterator>

namespace mc::render {

// Thread-safe custom memory sub-allocator
class FreeListAllocator {
public:
  struct Allocation {
    uint32_t start = 0;
    uint32_t count = 0;
    bool valid = false;
  };

  FreeListAllocator(uint32_t capacity = 0) {
    if (capacity > 0) {
      m_blocks.push_back({0, capacity, true});
    }
  }

  void Initialize(uint32_t capacity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_blocks.clear();
    m_blocks.push_back({0, capacity, true});
  }

  Allocation Allocate(uint32_t count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
      if (it->isFree && it->size >= count) {
        Allocation alloc{it->start, count, true};
        if (it->size > count) {
          m_blocks.insert(it, {it->start, count, false});
          it->start += count;
          it->size -= count;
        } else {
          it->isFree = false;
        }
        return alloc;
      }
    }
    return {0, 0, false};
  }

  void Free(Allocation alloc) {
    if (!alloc.valid || alloc.count == 0) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
      if (!it->isFree && it->start == alloc.start && it->size == alloc.count) {
        it->isFree = true;
        
        // Merge with next
        auto next = std::next(it);
        if (next != m_blocks.end() && next->isFree) {
          it->size += next->size;
          m_blocks.erase(next);
        }
        
        // Merge with prev
        if (it != m_blocks.begin()) {
          auto prev = std::prev(it);
          if (prev->isFree) {
            prev->size += it->size;
            m_blocks.erase(it);
          }
        }
        break;
      }
    }
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_blocks.clear();
  }

private:
  struct Block {
    uint32_t start;
    uint32_t size;
    bool isFree;
  };
  std::list<Block> m_blocks;
  std::mutex m_mutex;
};

} // namespace mc::render
