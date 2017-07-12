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
#include "pds/cphw/AmcTiming.hh"
#include "pds/cphw/XBar.hh"

using Pds::Cphw::Reg;
using Pds::Cphw::Reg64;
using Pds::Cphw::AmcTiming;
using Pds::Cphw::XBar;

extern int optind;

void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <IP addr (dotted notation)> : Use network <IP>\n");
}

class Dti {
private:  // only what's necessary here
  AmcTiming _timing;
  uint32_t  _reserved[(0x80000000-sizeof(AmcTiming))>>2];
  class UsLinkControl {
  private:
    Reg _control;
    Reg _dataSrc;
    Reg _dataType;
    uint32_t _reserved;
  public:
    void enable(unsigned fwdmask,
                unsigned delay) {
      if (fwdmask) {
        // enable=T, tagEnable=F, L1Enable=F, partn=0, fwdMode=RR
        _control = 1 | ((delay&0xff)<<8) | ((fwdmask&0x1fff)<<16);
      }
      else {
        _control = 0;
      }
    }
  } _usLink[7];
  Reg _linkUp; // [6:0]=us, [15]=bp, [28:16]=ds

  Reg _linkIdx; // [3:0]=us, [19:16]=ds, [30]=countReset, [31=countUpdate
  uint32_t _reserved1[2];

  class UsLinkStatus {
  public:
    Reg _rxErrs;
    Reg _rxFull;
    Reg _ibRecv;
    Reg _ibEvt;
  } _usLinkStatus;
  class DsLinkStatus {
  public:
    Reg _rxErrs;
    Reg _rxFull;
    Reg _obSent;
    uint32_t _reserved;
  } _dsLinkStatus;

  uint32_t reserved2[(0x10000000-160)>>2];

  class Pgp2bAxi {
  public:
    Reg      _countReset;
    uint32_t _reserved[17];
    Reg      _rxFrames;
    uint32_t _reserved2[5];
    Reg      _txFrames;
    uint32_t _reserved3[5];
    Reg      _txOpcodes;
    Reg      _rxOpcodes;
    uint32_t _reserved4[0x80>>2];
  } _pgp[2];

public:
  Dti() {}
  void start()
  {
    _timing.xbar.setOut( XBar::RTM0, XBar::FPGA );
    _timing.xbar.setOut( XBar::FPGA, XBar::RTM0 );

    _linkIdx = 1<<30;
    _pgp[0]._countReset = 1;
    _pgp[1]._countReset = 1;

    _usLink[0].enable(0x1, 0);

    _pgp[0]._countReset = 0;
    _pgp[1]._countReset = 0;
    _linkIdx = 1<<31;
  }
  void stop()
  {
    _usLink[0].enable(0, 0);
  }
  void stats(uint32_t* v,
             uint32_t* dv) {
#define UFILL(s) {     \
      uint32_t q = _usLinkStatus.s; \
      *dv++ = q - *v; \
      *v++  = q; }
    UFILL(_rxErrs);
    UFILL(_rxFull);
    UFILL(_ibRecv);
    UFILL(_ibEvt);
#define DFILL(s) {     \
      uint32_t q = _dsLinkStatus.s; \
      *dv++ = q - *v; \
      *v++  = q; }
    DFILL(_rxErrs);
    DFILL(_rxFull);
    DFILL(_obSent);
#define PFILL(s) {          \
      uint32_t q = _pgp[0].s; \
      *dv++ = q - *v;         \
      *v++  = q;              \
      q = _pgp[1].s;          \
      *dv++ = q - *v;         \
      *v++  = q; }
    PFILL(_rxFrames);
    PFILL(_txFrames);
    PFILL(_rxOpcodes);
    PFILL(_txOpcodes);
  }
};

void sigHandler( int signal ) {
  Dti* m = new((void*)0) Dti;
  m->stop();
  ::exit(signal);
}

int main(int argc, char** argv) {

  extern char* optarg;

  int c;

  const char* ip  = "10.0.1.103";
  unsigned partition = 0;

  while ( (c=getopt( argc, argv, "a:")) != EOF ) {
    switch(c) {
    case 'a': ip = optarg; break;
    default:  usage(argv[0]); return 0;
    }
  }

  ::signal( SIGINT, sigHandler );

  //  Setup DTI
  Pds::Cphw::Reg::set(ip, 8192, 0);
  Dti* dti = new (0)Dti;
  dti->start();

  uint32_t stats[15], dstats[15];
  memset(stats, 0, sizeof(stats));
  static const char* title[] = 
    { "usRxErrs   ",
      "usRxFull   ",
      "usIbRecv   ",
      "usIbEvt    ",
      "dsRxErrs   ",
      "dsRxFull   ",
      "dsObSent   ",
      "usRxFrames ",
      "dsRxFrames ",
      "usTxFrames ",
      "dsTxFrames ",
      "usRxOpcodes",
      "dsRxOpcodes",
      "usTxOpcodes",
      "dsTxOpcodes" };

  while(1) {
    sleep(1);
    dti->stats(stats,dstats);
    for(unsigned i=0; i<15; i++)
      printf("%s: %010u [%010u]\n", title[i], stats[i], dstats[i]);
    printf("----\n");
  }

  return 0;
}
