
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

#include <sstream>
#include <string>
#include <vector>

#include "pds/cphw/Reg.hh"
#include "pds/cphw/AxiVersion.hh"

namespace Timing {
  class DelayPLL {
  public:
    Pds::Cphw::Reg _reg;
  };

  class DelayLink {
  public:
    Pds::Cphw::Reg _csr;
    Pds::Cphw::Reg _pherr;
    Pds::Cphw::Reg _get;
    Pds::Cphw::Reg _set;
  };

  class DelayModule {
  public:
    DelayPLL  _pll [2];
    Pds::Cphw::Reg _qpll[2];
    DelayLink _link[14];
    Pds::Cphw::Reg _refclk[3];
  };
};

extern int optind;

void usage(const char* p) {
  printf("Usage: %s [-a <IP addr (dotted notation)>] [-p <port>] [-w]\n",p);
}

int main(int argc, char** argv) {

  extern char* optarg;

  int c;
  bool lUsage = false;

  const char* ip = "10.0.2.103";
  unsigned short port = 8192;

  int link=-1;
  int delay=0;
  int rlink=-1;
  int tlink=-1;
  int latencyHist=-1;
  int phaseScan=-1;
  bool lErrorPoll=false;
  bool lClockPoll=false;

  char* endptr=0;
  
  while ( (c=getopt( argc, argv, "a:p:c:r:t:L:T:eCh")) != EOF ) {
    switch(c) {
    case 'C':
      lClockPoll=true;
      break;
    case 'e':
      lErrorPoll=true;
      break;
    case 'a':
      ip = optarg; break;
      break;
    case 'p':
      port = strtoul(optarg,NULL,0);
      break;
    case 'c':
      link = strtoul(optarg,&endptr,0);
      delay = strtol(endptr+1,&endptr,0);
      break;
    case 'r':
      rlink = strtoul(optarg,&endptr,0);
      break;
    case 't':
      tlink = strtoul(optarg,&endptr,0);
      break;
    case 'L':
      latencyHist = strtoul(optarg,&endptr,0);
      break;
    case 'T':
      phaseScan = strtoul(optarg,&endptr,0);
      break;
    case '?':
    default:
      lUsage = true;
      break;
    }
  }

  if (lUsage) {
    usage(argv[0]);
    exit(1);
  }

  Pds::Cphw::Reg::set(ip, port, 0);

  Pds::Cphw::AxiVersion* vsn = new((void*)0) Pds::Cphw::AxiVersion;
  printf("buildStamp %s\n",vsn->buildStamp().c_str());

  Timing::DelayModule*  m = new((void*)0x80000000) Timing::DelayModule;

  if (lClockPoll) {
    while(1) {
      printf("clkRate");
      for(unsigned i=0; i<3; i++) {
        unsigned v = unsigned(m->_refclk[i]);
        printf("  %09u [%01x]",
               v&0x1fffffff,
               v>>29);
      }
      printf("\n");
      usleep(100000);
    }
  }

  if (lErrorPoll) {
    unsigned buff[14]; memset(buff,0,14*sizeof(unsigned));
    while(1) {
      for(unsigned i=0; i<14; i++) {
        unsigned v = unsigned(m->_link[i]._csr)&0xfff;
        if (v < (buff[i]&0xfff)) v |= (1<<12);
        v -= (buff[i]&0xfff);
        printf(" %04u",v);
        buff[i] += v;
      }
      printf("\n");
      usleep(100000);
    }
  }

  if (phaseScan>=0) {
    unsigned buff[128]; memset(buff,0,128*sizeof(unsigned));
    for(unsigned j=0; j<256; j++) {
      //      for(unsigned i=0; i<14; i++) {
      { unsigned i=0;
        m->_link[i]._csr = unsigned(m->_link[i]._csr) | (1<<29);
        usleep(10);
        m->_link[i]._csr = unsigned(m->_link[i]._csr) &~ (1<<29);
      }
      usleep(200000);
      //      for(unsigned i=0; i<14; i++) {
      { unsigned i=0;
        unsigned v = unsigned(m->_link[i]._get);
        printf("  %08x", v);

        v = (v>>16)&0x7f;
        buff[v]++;

        if (phaseScan==0 && v > 0x7c)
          return 0;
      }
      printf("\n");
    }
    for(unsigned i=0; i<128; i++)
      printf("%04u%c", buff[i], (i&7)==7 ? '\n':' ');
    return 0;
  }

  if (latencyHist>=0) {
    unsigned buff[64]; memset(buff,0,64*sizeof(unsigned));
    unsigned n=0;
    unsigned last = (unsigned(m->_link[latencyHist]._csr)>>16)&0x3f;
    buff[last]++;
    while(n < 100) {
      unsigned v =  (unsigned(m->_link[latencyHist]._csr)>>16)&0x3f;
      if (v != last) {
        n++;
        buff[v]++;
        last = v;
        printf("%u [%x]..\n",n,last);
      }
    }
    for(unsigned i=0; i<64; i++)
      printf("%04x%c", buff[i], (i&7)==7 ? '\n':' ');
    return 0;
  }

  if (tlink>=0) {
    m->_link[tlink]._csr = unsigned(m->_link[tlink]._csr) | (1<<29);
    usleep(10);
    m->_link[tlink]._csr = unsigned(m->_link[tlink]._csr) &~ (1<<29);
    usleep(100000);
  }

  if (rlink>=0) {
    if ((rlink%7)==0) {
      unsigned v= unsigned(m->_pll[rlink/7]._reg);
      m->_pll[rlink/7]._reg = v & ~(1<<27);
      usleep(10);
      m->_pll[rlink/7]._reg = v |  (1<<27);
      usleep(100000);
    }

    m->_link[rlink]._csr = unsigned(m->_link[rlink]._csr) | (1<<30);
    usleep(10);
    m->_link[rlink]._csr = unsigned(m->_link[rlink]._csr) &~ (1<<30);
    usleep(100000);
  }

  if (link>=0) {
    m->_link[link]._set = delay;
  }

  printf("clkRate");
  for(unsigned i=0; i<3; i++) {
    unsigned v = unsigned(m->_refclk[i]);
    printf("  %f MHz", double(v&0x1fffffff)*1.e-6);
    if ((v>>29)&1) printf(" [SLOW]");
    if ((v>>30)&1) printf(" [FAST]");
    if ((v>>31)&1) printf(" [LOCK]");
  }
  printf("\n");

  for(unsigned i=0; i<2; i++) {
    unsigned v = unsigned(m->_pll[i]._reg);
    printf("  %08x [%s %s]", v, (v&(1<<30)) ? "LOL":"", (v&(1<<31)) ? "LOS":"");
  }
  for(unsigned i=0; i<2; i++) {
    unsigned v = unsigned(m->_qpll[i])&0xf;
    printf("  %02x [%s %s %s %s]", v, 
           ((v>>0)&1)?"LOCK":"UNL",
           ((v>>1)&1)?"LOCK":"UNL",
           ((v>>2)&1)?"RST":"",
           ((v>>3)&1)?"RST":"" );
  }
  printf("\n-----\n");

  for(unsigned i=0; i<7; i++) {
    printf("CH%u",i);
    unsigned v = unsigned(m->_link[i]._csr);
    printf("  %08x  RxErr %04u  Latency %02x  PhErr %08x:", 
           v, v&0xfff, (v>>16)&0x7f, unsigned(m->_link[i]._pherr));
    v = unsigned(m->_link[i+7]._csr);
    printf("  %08x  RxErr %04u  Latency %02x  PhErr %08x\n", 
           v, v&0xfff, (v>>16)&0x7f, unsigned(m->_link[i+7]._pherr));
  }
  printf("-----\n");

  while(1) {
    usleep(100000);
    for(unsigned i=0; i<7; i++)
      printf("  %04x.%04x", 
             unsigned(m->_link[i]._get),
             unsigned(m->_link[i]._pherr)>>16);
    printf("\n");
  }
}

