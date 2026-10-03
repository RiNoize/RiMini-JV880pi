#pragma once
#include <atomic>
#include <algorithm>
#include <fstream>
#include <string>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <time.h>
// Sysfs discovery is performed by the UI before playback. This never modifies
// clock limits, governors or thermal policies. Affinity may be denied by Android.
class RiAudioCpu {
public:
    RiAudioCpu() {
        CPU_ZERO(&fastMask);cpu_set_t allowed;CPU_ZERO(&allowed);
        if(sched_getaffinity(0,sizeof(allowed),&allowed)!=0)return;
        long best=0;long score[CPU_SETSIZE]{};bool capacity=false;
        for(int cpu=0;cpu<CPU_SETSIZE;++cpu)if(CPU_ISSET(cpu,&allowed)) {
            score[cpu]=readScore(cpu,"cpu_capacity");capacity|=score[cpu]>0;
        }
        if(!capacity)for(int cpu=0;cpu<CPU_SETSIZE;++cpu)if(CPU_ISSET(cpu,&allowed))score[cpu]=readScore(cpu,"cpufreq/cpuinfo_max_freq");
        for(int cpu=0;cpu<CPU_SETSIZE;++cpu)if(CPU_ISSET(cpu,&allowed))best=std::max(best,score[cpu]);
        for(int cpu=0;cpu<CPU_SETSIZE;++cpu)if(CPU_ISSET(cpu,&allowed)&&best>0&&score[cpu]==best)CPU_SET(cpu,&fastMask);
        available=CPU_COUNT(&fastMask)>0&&CPU_COUNT(&fastMask)<CPU_COUNT(&allowed);
    }
    void selectFast(bool fast) noexcept{requested.store(fast);}
    bool isAvailable()const noexcept{return available;}
    void beginStream() noexcept{lastTid=-1;applied=-1;}
    void onCallback() noexcept {
        const int tid=int(syscall(SYS_gettid));
        if(tid!=lastTid){lastTid=tid;applied=-1;CPU_ZERO(&originalMask);sched_getaffinity(0,sizeof(originalMask),&originalMask);}
        const int desired=requested.load(std::memory_order_relaxed)&&available?1:0;
        if(applied!=desired) {
            cpu_set_t chosen=originalMask;if(desired)CPU_AND(&chosen,&fastMask,&originalMask);
            int result=0;if(CPU_COUNT(&chosen)==0)result=-EINVAL;
            else if(sched_setaffinity(0,sizeof(chosen),&chosen)!=0)result=-errno;
            status.store(result<0?result:desired,std::memory_order_relaxed);applied=desired;
        }
        core.store(sched_getcpu(),std::memory_order_relaxed);
    }
    void restore() noexcept{if(lastTid>=0)sched_setaffinity(lastTid,sizeof(originalMask),&originalMask);lastTid=-1;applied=-1;}
    static double threadSeconds() noexcept{timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return double(t.tv_sec)+double(t.tv_nsec)*1e-9;}
    std::atomic<int> core{-1},status{0};
private:
    static long readScore(int cpu,const char* suffix){long value=0;std::ifstream f("/sys/devices/system/cpu/cpu"+std::to_string(cpu)+"/"+suffix);f>>value;return value;}
    cpu_set_t fastMask{},originalMask{};std::atomic<bool> requested{true};bool available=false;int lastTid=-1,applied=-1;
};
