#pragma once
#include <MLResult.h>
namespace rpw {
enum Type:uint8_t {Beacon=1,Ready=2,Data=3,Ack=4,Finish=5};
constexpr uint8_t Version=5;
constexpr unsigned Header=24,CRC=4,ApplicationBytes=ml::PayloadBytes,MaxBody=20;
constexpr unsigned DataPacketBytes=Header+ApplicationBytes+CRC,MaxPacketBytes=Header+MaxBody+CRC;
inline void put16(uint8_t*p,unsigned v){p[0]=v;p[1]=v>>8;}
inline void put32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
inline unsigned get16(const uint8_t*p){return p[0]|unsigned(p[1])<<8;}
inline uint32_t get32(const uint8_t*p){uint32_t x=0;for(unsigned i=0;i<4;i++)x|=uint32_t(p[i])<<(8*i);return x;}
inline uint32_t crc32(const uint8_t*p,unsigned n){uint32_t c=0xffffffff;while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320:0);}return ~c;}
inline unsigned battery(double p){return ml::batteryCode(p);}
struct Packet {
 uint8_t type=0,node=0,index=0,count=0,attempt=0,len=0;
 uint32_t run=0,token=0,batch=0,measurement=0;uint8_t body[MaxBody]={};
};
inline bool fields(const Packet&p){
 if(p.type<Beacon||p.type>Finish||p.node>2||!p.run)return false;
 if(p.type==Beacon)return p.node==0&&p.len==20&&p.index==0&&p.count==0&&p.attempt==0&&!p.token&&!p.batch&&!p.measurement;
 if(!p.node||!p.token||!p.batch||p.count<1||p.count>8||p.attempt>=5)return false;
 if(p.type!=Data&&(p.index||p.measurement||p.attempt))return false;
 if(p.type==Data)return p.index<p.count&&p.measurement&&p.len==ApplicationBytes;
 return p.type==Ready?p.len==0:p.type==Ack?(p.len==1&&!(p.body[0]&~((1u<<p.count)-1))):(p.len==1&&p.body[0]<=1);
}
inline unsigned serialize(const Packet&p,uint8_t*b,unsigned capacity=MaxPacketBytes){
 if(!b||!fields(p)||p.len>MaxBody||capacity<Header+p.len+CRC)return 0;
 b[0]=0xa7;b[1]=Version;b[2]=p.type;b[3]=p.node;put32(b+4,p.run);put32(b+8,p.token);put32(b+12,p.batch);put32(b+16,p.measurement);
 b[20]=p.index;b[21]=p.count;b[22]=p.attempt;b[23]=p.len;memcpy(b+24,p.body,p.len);put32(b+24+p.len,crc32(b,24+p.len));return 28+p.len;
}
inline bool parse(Packet&p,const uint8_t*b,unsigned n){
 if(!b||n<28||n>MaxPacketBytes||b[0]!=0xa7||b[1]!=Version||b[23]>MaxBody||n!=28u+b[23]||get32(b+n-4)!=crc32(b,n-4))return false;
 p=Packet{};p.type=b[2];p.node=b[3];p.run=get32(b+4);p.token=get32(b+8);p.batch=get32(b+12);p.measurement=get32(b+16);p.index=b[20];p.count=b[21];p.attempt=b[22];p.len=b[23];memcpy(p.body,b+24,p.len);return fields(p);
}
inline bool matchingAck(const Packet&p,unsigned node,uint32_t run,uint32_t token,uint32_t batch,unsigned count){
 return count>=1&&count<=8&&p.type==Ack&&p.node==node&&p.run==run&&p.token==token&&p.batch==batch&&p.count==count&&p.len==1&&!(p.body[0]&~((1u<<count)-1));
}
inline unsigned application(uint8_t node,unsigned day,unsigned bat,float probability,uint8_t*out,unsigned cap){
 return ml::encode(node,day,bat,probability,out,cap)?ApplicationBytes:0;
}
inline bool measurement(const Packet&p,ml::Result&result){
 return p.type==Data&&p.body[0]==p.node&&ml::decode(p.body,p.len,result);
}
}
