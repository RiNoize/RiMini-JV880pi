#include "RiRateConverter.h"
#include "RiSnapshot.h"
#include <vector>
#include <iostream>
#include <chrono>
#include <cassert>
#include <memory>
#include <thread>
#include <cstring>
static constexpr double pi=3.14159265358979323846;
struct Output{std::vector<float> l,r;int consumed=0;};
Output render(int rate,int count,const std::vector<int>& blocks,double hz=12000,bool dc=false){
 auto c=std::make_unique<RiRateConverter>();c->prepare(rate);Output o;o.l.resize(count);o.r.resize(count);int pos=0,cycle=0;
 while(pos<count){int n=std::min(blocks[cycle++%blocks.size()],count-pos),need=c->inputNeeded(n);std::vector<float> l(need),r(need);
  for(int i=0;i<need;++i){l[i]=dc?1.f:float(.5*std::sin(2*pi*hz*(o.consumed+i)/64000));r[i]=-l[i];}
  assert(c->process(l.data(),r.data(),o.l.data()+pos,o.r.data()+pos,n)==need);o.consumed+=need;pos+=n;
 }
 assert(o.consumed==1+int(int64_t(count-1)*64000/rate));return o;
}
double rms(const std::vector<float>&v){double s=0;for(size_t i=1024;i<v.size();++i)s+=double(v[i])*v[i];return std::sqrt(s/(v.size()-1024));}
int main(){
 for(int rate:{32000,44100,48000,96000}){
  auto a=render(rate,rate*3,{512}),b=render(rate,rate*3,{1,17,128,256,1024,37});assert(a.l==b.l&&a.r==b.r&&a.consumed==b.consumed);
  for(size_t i=0;i<a.l.size();++i)assert(std::isfinite(a.l[i])&&std::abs(a.r[i]+a.l[i])<1e-7);
  auto dc=render(rate,rate,{1024},0,true);for(size_t i=256;i<dc.l.size();++i)assert(std::abs(dc.l[i]-1)<2e-6);
  std::cout<<"PASS rate="<<rate<<" variable blocks, exact source count, stereo, DC unity\n";
 }
 auto pass=render(48000,96000,{1024},15000),stop=render(48000,96000,{1024},30000);
 double passDb=20*std::log10(rms(pass.l)/(.5/std::sqrt(2.))),stopDb=20*std::log10(rms(stop.l)/(.5/std::sqrt(2.)));
 std::cout<<"15kHz gain "<<passDb<<" dB; 30kHz rejection "<<stopDb<<" dB\n";assert(std::abs(passDb)<.03&&stopDb<-70);
 RiSnapshot<196> snapshot;std::atomic<bool> done{false};
 std::thread producer([&]{for(unsigned n=1;n<300000;++n){RiSnapshot<196>::Data d;for(int i=0;i<49;++i)memcpy(d.data()+i*4,&n,4);snapshot.publish(d);}done.store(true);});
 unsigned last=0,count=0;while(!done.load()){RiSnapshot<196>::Data d;if(snapshot.read(d)){unsigned v=0;memcpy(&v,d.data(),4);assert(v>=last);last=v;for(int i=0;i<49;++i){unsigned w;memcpy(&w,d.data()+i*4,4);assert(w==v);}++count;}}
 producer.join();std::cout<<"PASS LCD coherent snapshots, consumed "<<count<<" updates\n";
 auto c=std::make_unique<RiRateConverter>();c->prepare(48000);float l[2048]{},r[2048]{},ol[1024]{},orr[1024]{};volatile float sink=0;
 auto t=std::chrono::steady_clock::now();for(int n=0;n<10000;++n){c->process(l,r,ol,orr,1024);sink=ol[999];}
 double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();
 std::cout<<"Converter benchmark: "<<elapsed<<" s for "<<10240000./48000<<" audio s (host only, not tablet)\n";
}
