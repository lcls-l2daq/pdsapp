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
#include "pds/cphw/RingBuffer.hh"
#include "pds/cphw/XBar.hh"

//#define NO_DSPGP

using Pds::Cphw::Reg;
using Pds::Cphw::Reg64;
using Pds::Cphw::AmcTiming;
using Pds::Cphw::RingBuffer;
using Pds::Cphw::XBar;

extern int optind;
bool _keepRunning = false;    // keep running on exit
unsigned _partn = 0;

void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <IP addr (dotted notation)> : Use network <IP>\n");
  printf("         -t <partition>                 : Upstream link partition (default=0)\n");
  printf("         -k                             : Keep running on exit\n");
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
        // enable=T, tagEnable=F, L1Enable=F, fwdMode=RR
        _control = 1 | ((_partn&0xf)<<4) | ((delay&0xff)<<8) | ((fwdmask&0x1fff)<<16);
        //        _control = 1 | ((delay&0xff)<<8) | ((0&0x1fff)<<16);
      }
      else {
        _control = 0;
      }
    }
  } _usLink[7];
  Reg _linkUp; // [6:0]=us, [15]=bp, [28:16]=ds

  Reg _linkIdx; // [3:0]=us, [19:16]=ds, [30]=countReset, [31=countUpdate

  Reg _bpLinkSent;

  uint32_t _reserved1;

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

  uint32_t reserved2a[4];

  Reg _qpll_bpUpdate;

  uint32_t reserved2[(0x10000000-180)>>2];

  class Pgp2bAxi {
  public:
    Reg      _countReset;
    uint32_t _reserved[16];
    Reg      _rxFrameErrs;
    Reg      _rxFrames;
    uint32_t _reserved2[4];
    Reg      _txFrameErrs;
    Reg      _txFrames;
    uint32_t _reserved3[5];
    Reg      _txOpcodes;
    Reg      _rxOpcodes;
    uint32_t _reserved4[0x80>>2];
  } _pgp[2];

  uint32_t reserved3[(0x10000000-0x200)>>2];
  RingBuffer _ringb;

public:
  Dti() {}
  void dumpb()
  {
    RingBuffer* b = &_ringb;
    printf("RingB @ %p\n",b);
    b->enable(false);
    b->clear();
    b->enable(true);
    usleep(1000);
    b->enable(false);
    b->dump();
    printf("\n");
  }
  void start(bool lRTM)
  {
    //  XPM connected through RTM
    _timing.xbar.setOut( XBar::RTM0, XBar::FPGA );
    _timing.xbar.setOut( XBar::FPGA, lRTM ? XBar::RTM0 : XBar::BP );

    _qpll_bpUpdate = 100<<16;

    _linkIdx = 1<<30;
    _pgp[0]._countReset = 1;
#ifndef NO_DSPGP
    _pgp[1]._countReset = 1;
#endif
    _usLink[0].enable(0x1, 0);

    _pgp[0]._countReset = 0;
#ifndef NO_DSPGP
    _pgp[1]._countReset = 0;
#endif
    _linkIdx = 1<<31;

    dumpb();
  }
  void stop()
  {
    if (!_keepRunning) {
      _usLink[0].enable(0, 0);
    }
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
#ifndef NO_DSPGP
#define PFILL(s) {          \
      uint32_t q = _pgp[0].s; \
      *dv++ = q - *v;         \
      *v++  = q;              \
      q = _pgp[1].s;          \
      *dv++ = q - *v;         \
      *v++  = q; }
#else
#define PFILL(s) {          \
      uint32_t q = _pgp[0].s; \
      *dv++ = q - *v;         \
      *v++  = q;              \
      *dv++ = 0; v++; }
#endif
    PFILL(_rxFrames);
    PFILL(_rxFrameErrs);
    PFILL(_txFrames);
    PFILL(_txFrameErrs);
    PFILL(_rxOpcodes);
    PFILL(_txOpcodes);
#define FILL(s) {               \
      uint32_t q = s;           \
      *dv++ = q - *v;           \
      *v++  = q; }
    FILL(_bpLinkSent);

    printf("link partition: %u\n", _partn);
    printf("linkUp   : %08x\n",unsigned(_linkUp));
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
  bool lRTM = false;

  while ( (c=getopt( argc, argv, "a:t:rkh")) != EOF ) {
    switch(c) {
    case 'a': ip = optarg; break;
    case 't': _partn = strtoul(optarg, NULL, 0); break;
    case 'r': lRTM = true; break;
    case 'k': _keepRunning = true; break;
    case 'h': default:  usage(argv[0]); return 0;
    }
  }

  ::signal( SIGINT, sigHandler );

  //  Setup DTI
  Pds::Cphw::Reg::set(ip, 8192, 0);
  Dti* dti = new (0)Dti;
  dti->start(lRTM);

  uint32_t stats[20], dstats[20];
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
      "usRxFrErrs ",
      "dsRxFrErrs ",
      "usTxFrames ",
      "dsTxFrames ",
      "usTxFrErrs ",
      "dsTxFrErrs ",
      "usRxOpcodes",
      "dsRxOpcodes",
      "usTxOpcodes",
      "dsTxOpcodes",
      "bpLinkSent " };

  while(1) {
    sleep(1);
    dti->stats(stats,dstats);
    for(unsigned i=0; i<20; i++)
      printf("%s: %010u [%010u]\n", title[i], stats[i], dstats[i]);
    printf("----\n");
  }

  return 0;
}
