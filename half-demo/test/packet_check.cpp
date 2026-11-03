#include <cstdio>
#include <cassert>
#include <RPWProtocol.h>
#include <TDMA.h>
#include <SelectiveRepeatARQ.h>
#include <ResultQueue.h>

int main() {
    static_assert(rpw::Header==24 && rpw::ApplicationBytes==6 && rpw::DataPacketBytes==34,"v5 layout");
    assert(rpw::crc32((const uint8_t*)"123456789",9)==0xcbf43926);
    assert(ml::batteryCode(0)==0 && ml::batteryCode(50)==4 && ml::batteryCode(89)==6 && ml::batteryCode(100)==7);
    rpw::Packet p;
    p.type=rpw::Data; p.node=1; p.run=0x12345678; p.token=0xaabbccdd;
    p.batch=1; p.measurement=41; p.count=1; p.len=6;
    assert(ml::encode(1,31,7,ml::fromBits(0x3f5f9db2),p.body,sizeof p.body));
    assert(p.body[0]==1 && p.body[1]==255 && rpw::get32(p.body+2)==0x3f5f9db2);
    uint8_t wire[48]; unsigned n=rpw::serialize(p,wire);
    assert(n==34 && wire[1]==5 && wire[23]==6);
    rpw::Packet decoded; ml::Result result;
    assert(rpw::parse(decoded,wire,n) && rpw::measurement(decoded,result));
    assert(ml::floatBits(result.probability)==0x3f5f9db2);
    for(unsigned i=0;i<n;i++) printf("%02x",wire[i]);
    puts("");
    wire[1]=4; rpw::put32(wire+30,rpw::crc32(wire,30)); assert(!rpw::parse(decoded,wire,34));
    assert(!ml::encode(1,0,7,.5f,p.body,6) && !ml::encode(1,1,8,.5f,p.body,6));
    assert(!ml::encode(1,1,7,.5f,p.body,5) && !ml::encode(1,1,7,NAN,p.body,6));
    assert(tdma::airtime(34)==78 && tdma::airtime(35)==78);
    assert(tdma::cell()==108 && tdma::slot()==1099 && tdma::frame()==2258);
    // Keep partial batches and selective retries, including duplicate ACK loss.
    for(unsigned count=1;count<=8;count++) {
        arq::Window sender(count); arq::Receiver head;
        assert(head.grant(7,1,count));
        for(unsigned i=1;i<count;i++) { assert(head.classify(i,i+1,100+i)==1); head.commit(i,i+1,100+i); }
        assert(sender.ack(head.bits)); assert(sender.missing(0));
        for(unsigned i=1;i<count;i++) assert(!sender.missing(i));
        assert(sender.advance()); head.commit(0,1,100);
        assert(head.classify(0,1,100)==2 && head.classify(0,1,101)==0);
        assert(sender.ack(head.bits) && sender.complete());
    }
    ml::ResultQueue<32> queue;
    for(unsigned i=1;i<=32;i++) assert(queue.push(i,1,3,7,.25f));
    assert(queue.full() && !queue.push(33,1,3,7,.5f));
    assert(queue.pop(1) && queue.at(0)->id==2 && queue.push(33,1,3,7,.5f));
    puts("PASS v5 codec/CRC, float bits, queue, TDMA airtime and selective retries");
}
