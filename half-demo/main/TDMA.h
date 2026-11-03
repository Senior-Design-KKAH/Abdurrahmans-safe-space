#pragma once
#include <stdint.h>
#include <math.h>
#include <RPWProtocol.h>
namespace tdma {
inline uint32_t airtime(unsigned bytes){return uint32_t(ceil((8+4.25+8+fmax(ceil((8.0*bytes+16)/28),0)*5)*1.024));}
// The control cell uses READY/FIN (28/29 bytes), DATA cells use exactly 34.
inline uint32_t cell(){return airtime(rpw::DataPacketBytes)+30;}
inline uint32_t slot(){return 9*cell()+30+airtime(29)+30;}
inline uint32_t frame(){return 60+2*slot();}
inline bool due(uint32_t n,uint32_t t){return int32_t(n-t)>=0;}
inline uint32_t base(unsigned node){return 30+(node-1)*slot();}
inline bool uplink(unsigned node,uint32_t offset){return node>=1&&node<=2&&offset>=base(node)&&offset<base(node)+9*cell();}
}
