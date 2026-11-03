#pragma once
#include <stdint.h>
namespace arq {
inline uint8_t mask(unsigned n){return n>=1&&n<=8?uint8_t((1u<<n)-1):0;}
struct Window {
 uint8_t count=0,bits=0,attempt=0;
 explicit Window(unsigned n=0):count(n){}
 bool missing(unsigned i)const{return i<count&&!(bits&(1<<i));}
 bool ack(unsigned b){if(b&~mask(count))return false;bits|=b;return true;}
 bool complete()const{return count&&bits==mask(count);}
 bool exhausted()const{return attempt>=4;}
 // Caller advances after this frame's dedicated ACK opportunity.
 bool advance(){if(complete()||exhausted())return false;attempt++;return true;}
};
struct Receiver {
 uint32_t token=0,batch=0,ids[8]={},digests[8]={};uint8_t count=0,bits=0;
 bool grant(uint32_t t,uint32_t b,unsigned n){
 if(!t||!b||!mask(n))return false;
 if(t==token&&b==batch)return n==count;
 if(t==token&&b<=batch)return false;
 *this=Receiver{};token=t;batch=b;count=n;return true;
 }
 // 0 invalid/conflict, 1 new, 2 exact duplicate. Commit only after storage.
 unsigned classify(unsigned i,uint32_t id,uint32_t digest)const{
 if(i>=count||!id)return 0;
 if(bits&(1<<i))return ids[i]==id&&digests[i]==digest?2:0;
 for(unsigned j=0;j<count;j++)if((bits&(1<<j))&&ids[j]==id)return 0;
 return 1;
 }
 void commit(unsigned i,uint32_t id,uint32_t digest){ids[i]=id;digests[i]=digest;bits|=1<<i;}
};
}
