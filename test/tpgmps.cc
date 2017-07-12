
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <signal.h>
#include <new>

#include "pds/cphw/Reg.hh"

#include <string>

class MpsSim {
public:
  unsigned latchDiag() const { return unsigned(_csr)&1; }
  unsigned tag      () const { return unsigned(_tag_ts)&0xffff; }
  unsigned timestamp() const { return (unsigned(_tag_ts)>>16)&0xffff; }
  unsigned pclass   (unsigned i) const
  { 
    return (unsigned(_pclass[i/4])>>(8*(i&3)))&0xf; 
  }
  void setLatch(bool v) 
  { 
    if (v) _csr.setBit(0);
    else   _csr.clearBit(0);
  }
  void setStrobe() { _csr.setBit(31); }
  void setTag   (unsigned t) { 
    unsigned v = _tag_ts;
    v &= ~0xffff;
    v |= (t&0xffff);
    _tag_ts = v;
  }
  void setClass(unsigned d,
                unsigned c)
  {
    unsigned v = _pclass[d/4];
    v &= ~((0xff)<<(8*(d&3)));
    v |= (c&0xf)<<(8*(d&3));
    _pclass[d/4] = v;
  }
private:
  Pds::Cphw::Reg _csr;
  Pds::Cphw::Reg _tag_ts;
  uint32_t       _reserved[2];
  Pds::Cphw::Reg _pclass[2];
};


void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <IP addr (dotted notation)> : Use network <IP>\n");
  printf("         -C <dst,class>                 : Set MPS dest to power class\n");
  printf("         -L <tag>                       : Latch with tag\n");
}

int main(int argc, char** argv) {

  const char* ip = "192.168.2.10";
  char* endptr = 0;

  unsigned latch_tag=0;

  unsigned pc=0;
  int      dst=-1;

  opterr = 0;

  char opts[32];
  sprintf(opts,"a:C:L:h");

  int c;
  while( (c=getopt(argc,argv,opts))!=-1 ) {
    switch(c) {
    case 'a':
      ip = optarg;
      break;
    case 'C':
      dst = strtoul(optarg,&endptr,0);
      pc  = strtoul(endptr+1,&endptr,0);
      break;
    case 'L':
      latch_tag = strtoul(optarg,NULL,0);
      break;
    case 'h':
      usage(argv[0]); return 1;
    default:
      break;
    }
  }

  Pds::Cphw::Reg::set(ip, 8192, 0);

  MpsSim* p = new ((void*)0x82000000) MpsSim;

  if (dst >= 0)
    p->setClass(dst,pc);

  p->setTag  (latch_tag);
  p->setLatch(latch_tag != 0);

  if (latch_tag || (dst>=0))
    p->setStrobe();

  unsigned latch, tag, timestamp;
  unsigned pclass[16];

  latch     = p->latchDiag();
  tag       = p->tag();
  timestamp = p->timestamp();

  for(unsigned i=0; i<16; i++)
    pclass[i] = p->pclass(i);

  printf("Latch [%u]  Tag [%x]  Timestamp[%x]\n",
         latch, tag, timestamp );
  for(unsigned i=0; i<16; i++)
    printf("  c%u[%x]", i, pclass[i]);
  printf("\n");
  
  return 0;
}
