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

using Pds::Cphw::Reg;
using Pds::Cphw::Reg64;
using Pds::Cphw::AmcTiming;
using Pds::Cphw::RingBuffer;
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
    _linkIdx = (1<<30);
    usleep(1);
    _linkIdx = (1<<31);
  }
  void linkStats(uint32_t* v,
                 uint32_t* dv) {
    for(unsigned i=0; i<7; i++) {
      _linkIdx = (i<<0) | (1<<31);
      uint32_t q = _usLinkStatus._rxErrs;
      *dv++ = q - *v;
      *v++  = q; 
    }
    for(unsigned i=0; i<7; i++) {
      _linkIdx = (i<<16) | (1<<31);
      uint32_t q = _dsLinkStatus._rxErrs;
      *dv++ = q - *v;
      *v++  = q; 
    }
  }
};

void sigHandler( int signal ) {
  ::exit(signal);
}

int main(int argc, char** argv) {

  extern char* optarg;

  int c;

  const char* ip  = "10.0.1.103";
  unsigned partition = 0;
  bool lRTM = false;

  while ( (c=getopt( argc, argv, "a:r")) != EOF ) {
    switch(c) {
    case 'a': ip = optarg; break;
    case 'r': lRTM = true; break;
    default:  usage(argv[0]); return 0;
    }
  }

  ::signal( SIGINT, sigHandler );

  //  Setup DTI
  Pds::Cphw::Reg::set(ip, 8192, 0);
  Dti* dti = new (0)Dti;
  dti->start(lRTM);

  unsigned  v[14];
  unsigned dv[14];

  dti->linkStats(v,dv);
  for(unsigned j=0; j<14; j++) {
    printf("%08x:",v[j]>>24);
    v[j]=0;
  }
  printf("\n--\n");

  while(1) {
    usleep(1000000);
    dti->linkStats(v,dv);
    for(unsigned j=0; j<14; j++)
      //      printf("%08x:",v[j]);
      printf("%08u:",dv[j]&0xffff);
    printf("\n");
  }

  return 0;
}
