/**
 ** pgpdaq
 **
 **   Manage XPM and DTI to trigger and readout pgpcard (dev03)
 **
 **/

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
#include "pds/cphw/Reg64.hh"

using Pds::Cphw::Reg;
using Pds::Cphw::Reg64;

extern int optind;

void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <IP addr (dotted notation)> : Use network <IP>\n");
  printf("         -e                             : Enable DTI link\n");
}

class Xpm {
private:
  uint32_t _reserved[0x80000000>>2];

  Reg      _paddr;
  Reg      _index; // [3:0]=partn, [9:4]=link, [15:10]=linkDbg, [19:16]=amc

  class LinkConfig {
  public:
    Reg    _control;
  public:
    void setup(bool lEnable) {
      // txDelay=0, partn=0, src=0, lb=F, txReset=F, rxReset=F, enable=F
      _control = (0x3<<29);
      usleep(1);
      _control = 0;
    }
  } _linkConfig;
  
  class LinkStatus {
  private:
    Reg    _status;
  public:
    void status(bool& rxReady,
                bool& txReady,
                bool& isXpm,
                unsigned& rxErrs) {
      unsigned v = _status;
      rxErrs  = v&0xffff;
      txReady = v&(1<<17);
      rxReady = v&(1<<19);
      isXpm   = v&(1<<20);
    }
  } _linkStatus;

  Reg _pllCSR;
  
  class PartitionL0Config {
  private:
    Reg _control1;
    Reg _control2;
  public:
    void start(unsigned rate) {
      _control1 = 1;
      _control2 = (rate&0xffff) | 0x80000000;
      _control1 = 0x80010000;
    }
    void stop() {
      _control1 = 1;
    }
  } _partitionL0Config;
  
  class PartitionStatus {
  public:
    Reg64 _enabled;
    Reg64 _inhibited;
    Reg64 _ninput;
    Reg64 _ninhibited;
    Reg64 _naccepted;
    Reg64 _nacceptedl1;
  } _partitionStatus;

  Reg _reserved1[(144-76)>>2];

  Reg _partitionSrcInhibits[32];

public:
  Xpm() {}
  void init();
  void linkStats(unsigned* v,
                 unsigned* dv);
};

void Xpm::init()
{
  unsigned rxReady = 0, txReady = 0;
  bool rxR, txR, isX; unsigned tmp;

  for(unsigned i=0; i<32; i++) {
    _index = i<<4;
    _linkStatus.status(rxR,txR,isX,tmp);
    if (rxR) rxReady |= (1<<i);
    if (txR) txReady |= (1<<i);
  }
  printf("rxReady %08x;  txReady %08x\n",rxReady,txReady);

  for(unsigned j=0; j<2; j++) {
    for(unsigned i=0; i<7; i++) {
      _index = (i+j*7)<<4;
      unsigned uc = _linkConfig._control;
      _linkConfig.setup(false);
      printf("Amc[%u]:Link[%u]: %08x -> %08x\n",
             j, i, uc, unsigned(_linkConfig._control));
    }
  }
}

void Xpm::linkStats(unsigned* v,
                    unsigned* dv) {
  unsigned idx0 = _index & ~0x103f0;
  bool b0,b1,b2;
  unsigned q;
  for(unsigned j=0; j<2; j++) {
    for(unsigned i=0; i<7; i++) {
      unsigned k = i+j*7;
      _index = idx0 | (k<<4);
      _linkStatus.status(b0,b1,b2,q);
      dv[k] = (q-v[k])&0xffff;
      v [k] = q;
    }
  }
}


int main(int argc, char** argv) {

  extern char* optarg;

  int c;

  const char* ip  = "10.0.1.102";

  while ( (c=getopt( argc, argv, "a:")) != EOF ) {
    switch(c) {
    case 'a': ip = optarg; break;
    default:  usage(argv[0]); return 0;
    }
  }

  //  Setup XPM
  Pds::Cphw::Reg::set(ip, 8192, 0);
  Xpm* xpm = new (0)Xpm;

  xpm->init();

  unsigned  v[16];
  unsigned dv[16];
  memset(v, 0, sizeof(v));

  while(1) {
    usleep(1000000);
    xpm->linkStats(v,dv);
    for(unsigned j=0; j<2; j++) {
      for(unsigned i=0; i<7; i++) {
        unsigned k=i+j*7;
        printf("%08u:",dv[k]);
      }
    }
    printf("\n");
  }

  return 0;
}
