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
  private:
    Reg    _control;
  public:
    void setup(bool lEnable) {
      // txDelay=0, partn=0, src=0, lb=F, txReset=F, rxReset=F, enable=F
      //      _control = 0;
      // txDelay=0, partn=0, src=0, lb=F, txReset=F, rxReset=F, enable=F
      _control = lEnable ? (1<<31) : 0;
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
  void start(unsigned rate, bool lEnableDTI) {
    // Configure dslink
    _index = (5<<4);
    _linkConfig.setup(lEnableDTI);
    // Setup partition
    _partitionL0Config.start(rate);
  }
  void stop() {
    _partitionL0Config.stop();
  }
  void stats(uint64_t* v,
             uint64_t* dv) {
#define FILL(s) {     \
      uint64_t q = _partitionStatus.s; \
      *dv++ = q - *v; \
      *v++  = q; }
    FILL(_enabled);
    FILL(_inhibited);
    FILL(_ninput);
    FILL(_ninhibited);
    FILL(_naccepted);
#undef FILL
  }
  void inhibits(uint32_t* v, 
                uint32_t* dv) {
    
#define FILL(s) {     \
      uint32_t q = _partitionSrcInhibits[s]; \
      *dv++ = q - *v; \
      *v++  = q; }
    for(unsigned i=0; i<32; i++) 
      FILL(i);
#undef FILL
  }
};


void sigHandler( int signal ) {
  Xpm* xpm = new (0)Xpm;
  xpm->stop();
  ::exit(signal);
}


int main(int argc, char** argv) {

  extern char* optarg;

  int c;

  const char* ip  = "10.0.1.102";
  unsigned fixed_rate = 6;
  bool lEnableDTI=false;

  while ( (c=getopt( argc, argv, "a:er:")) != EOF ) {
    switch(c) {
    case 'a': ip = optarg; break;
    case 'e': lEnableDTI=true; break;
    case 'r': fixed_rate = strtoul(optarg,NULL,0); break;
    default:  usage(argv[0]); return 0;
    }
  }

  ::signal( SIGINT, sigHandler );

  //  Setup XPM
  Pds::Cphw::Reg::set(ip, 8192, 0);
  Xpm* xpm = new (0)Xpm;
  xpm->start(fixed_rate, lEnableDTI);

  uint64_t stats[5], dstats[5];
  memset(stats, 0, sizeof(stats));
  static const char* title[] = { "enabled   ",
                                 "inhibited ",
                                 "ninput    ",
                                 "ninhibited",
                                 "naccepted " };
  uint32_t inh[32], dinh[32];
  memset(inh, 0, sizeof(inh));

  while(1) {
    sleep(1);
    xpm->stats(stats,dstats);
    for(unsigned i=0; i<5; i++)
      printf("%s: %016llu [%010llu]\n", title[i], stats[i], dstats[i]);

    xpm->inhibits(inh,dinh);
    for(unsigned i=0; i<32; i++)
      printf("%09u [%09u]%s", inh[i], dinh[i], (i&3)==3 ? "\n":"  ");

    printf("----\n");
  }

  return 0;
}
