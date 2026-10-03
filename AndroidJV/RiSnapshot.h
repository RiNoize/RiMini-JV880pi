#pragma once
#include <array>
#include <atomic>
#include <cstddef>
// Single audio producer, single GUI consumer. Acquire/release exchanges transfer
// slot ownership. No waiting and no concurrently writable slot.
template<std::size_t Bytes> class RiSnapshot {
public:
    using Data=std::array<unsigned char,Bytes>;
    void publish(const Data& value) noexcept {
        slots[back]=value;back=middle.exchange(back|4,std::memory_order_acq_rel)&3;
    }
    bool read(Data& value) noexcept {
        if((middle.load(std::memory_order_acquire)&4)==0)return false;
        front=middle.exchange(front,std::memory_order_acq_rel)&3;value=slots[front];return true;
    }
private:
    std::array<Data,3> slots{};std::atomic<int> middle{1};int front=0,back=2;
};
