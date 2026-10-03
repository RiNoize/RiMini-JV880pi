#pragma once
// RiJV880: continuous, causal 64 kHz stereo sample-rate converter.
// Coefficients are prepared off the audio callback. No allocations, locks,
// skipped input, block-dependent phase correction, or padding in process().
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#if defined(__aarch64__)
 #include <arm_neon.h>
#endif
class RiRateConverter {
public:
    static constexpr int sourceRate=64000, taps=64, maxPhases=1024;
    void prepare(int outputRate) {
        if(outputRate<8000 || outputRate>384000) throw std::invalid_argument("Unsupported audio sample rate");
        rate=outputRate;
        phases=std::min(rate/std::gcd(rate,sourceRate),maxPhases);
        constexpr double pi=3.14159265358979323846;
        const double cutoff=0.90*std::min(1.0,double(rate)/sourceRate);
        for(int p=0;p<phases;++p) {
            double sum=0;
            for(int k=0;k<taps;++k) {
                const double d=k+double(p)/phases-(taps-1)*0.5;
                const double v=pi*cutoff*d;
                const double window=std::abs(d)<taps*0.5 ? 0.42+0.5*std::cos(2*pi*d/taps)+0.08*std::cos(4*pi*d/taps):0;
                const double value=cutoff*(std::abs(v)<1e-12?1.0:std::sin(v)/v)*window;
                coefficients[p][k]=float(value);sum+=value;
            }
            for(auto& c:coefficients[p])c=float(c/sum);
        }
        resetHistory();
    }
    void resetHistory() noexcept {historyL.fill(0);historyR.fill(0);phase=0;pending=1;head=0;}
    int getRate()const noexcept{return rate;}
    int inputNeeded(int frames)const noexcept {
        if(frames<=0 || rate<=0)return 0;
        return pending+int((phase+int64_t(frames-1)*sourceRate)/rate);
    }
    // Input contains at least inputNeeded(frames) valid stereo frames.
    int process(const float* inL,const float* inR,float* outL,float* outR,int frames) noexcept {
        int consumed=0;
        for(int n=0;n<frames;++n) {
            for(int k=0;k<pending;++k) {
                head=(head-1)&(taps-1);
                historyL[head]=historyL[head+taps]=inL[consumed];
                historyR[head]=historyR[head+taps]=inR[consumed++];
            }
            const float* c=coefficients[int(phase*phases/rate)].data();
            const float* l=historyL.data()+head;const float* r=historyR.data()+head;
#if defined(__aarch64__)
            float32x4_t al=vdupq_n_f32(0),ar=vdupq_n_f32(0);
            for(int k=0;k<taps;k+=4) {
                const auto ck=vld1q_f32(c+k);
                al=vfmaq_f32(al,vld1q_f32(l+k),ck);ar=vfmaq_f32(ar,vld1q_f32(r+k),ck);
            }
            outL[n]=vaddvq_f32(al);outR[n]=vaddvq_f32(ar);
#else
            float al[4]{},ar[4]{};
            for(int k=0;k<taps;k+=4)for(int j=0;j<4;++j){al[j]+=l[k+j]*c[k+j];ar[j]+=r[k+j]*c[k+j];}
            outL[n]=(al[0]+al[1])+(al[2]+al[3]);outR[n]=(ar[0]+ar[1])+(ar[2]+ar[3]);
#endif
            phase+=sourceRate;pending=int(phase/rate);phase%=rate;
        }
        return consumed;
    }
private:
    alignas(64) std::array<std::array<float,taps>,maxPhases> coefficients{};
    alignas(64) std::array<float,taps*2> historyL{},historyR{};
    int rate=0,phases=1,pending=1,head=0;int64_t phase=0;
};
