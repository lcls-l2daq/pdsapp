
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
#include "pds/cphw/AmcTiming.hh"
#include "tdm.hh"

static bool lverbose = false;

static void clockPoll  (TDM::Module*);
static void errorPoll  (TDM::Module*);
static void phaseScan  (TDM::Module*, int);
static void txResetScan(TDM::Module*, int);
static void scanTest   (TDM::Module*);
static void txScan     (TDM::Module*, unsigned, unsigned);
static void txScanFast (TDM::Module*, unsigned, unsigned);
static void latencyHist(TDM::Module*, int);
static void txAlignHist(TDM::Module*, int);

extern int optind;

using Pds::Cphw::XBar;

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
  int alink=-1;
  int rlink=-1;
  int tlink=-1;
  int hlink=-1;
  bool ltxScan=false;
  int txScanBegin=0;
  int txScanEnd  =64*1024;
  int vphaseScan=-1;
  bool lErrorPoll=false;
  bool lClockPoll=false;
  bool lScanTest=false;
  bool lLoopback=false;
  bool lReset=false;

  char* endptr=0;
  
  while ( (c=getopt( argc, argv, "a:p:c:r:t:L:T:A:evlCRSh")) != EOF ) {
    switch(c) {
    case 'C':
      lClockPoll=true;
      break;
    case 'R':
      lReset=true;
      break;
    case 'S':
      lScanTest=true;
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
      hlink = strtoul(optarg,&endptr,0);
      break;
    case 'T':
      ltxScan = true;
      txScanBegin = strtoul(optarg  ,&endptr,0);
      txScanEnd   = strtoul(endptr+1,&endptr,0);
      break;
    case 'A':
      alink = strtoul(optarg,&endptr,0);
      break;
    case 'X':
      vphaseScan = strtoul(optarg,&endptr,0);
      break;
    case 'l':
      lLoopback = true;
      break;
    case 'v':
      lverbose = true;
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

  Pds::Cphw::AmcTiming* atim = new((void*)0) Pds::Cphw::AmcTiming;
  printf("buildStamp %s\n",atim->version.buildStamp().c_str());

  atim->xbar.setOut(XBar::FPGA,XBar::RTM0);

  TDM::Module*  m = new((void*)0x80000000) TDM::Module;

  /*
  m->_source.rxAlignTarget(10);
  m->_link[0x0].rxAlignTarget(11);
  m->_link[0x1].rxAlignTarget(11);
  m->_link[0x2].rxAlignTarget(11);
  m->_link[0x3].rxAlignTarget(10);
  m->_link[0x4].rxAlignTarget(11);
  m->_link[0x5].rxAlignTarget(11);
  m->_link[0x6].rxAlignTarget(10);
  m->_link[0x7].rxAlignTarget(11);
  m->_link[0x8].rxAlignTarget(11);
  m->_link[0x9].rxAlignTarget(10);
  m->_link[0xa].rxAlignTarget(11);
  m->_link[0xb].rxAlignTarget(11);
  m->_link[0xc].rxAlignTarget(11);
  m->_link[0xd].rxAlignTarget(11);
  */
  m->_source.rxAlignTarget(10);
  m->_link[0x0].rxAlignTarget(10);
  m->_link[0x0].rxPolarity   (true);
  m->_link[0x1].rxAlignTarget(11);
  m->_link[0x2].rxAlignTarget(11);
  m->_link[0x3].rxAlignTarget(10);
  m->_link[0x4].rxAlignTarget(10);
  m->_link[0x5].rxAlignTarget(10);
  m->_link[0x5].rxPolarity   (true);
  m->_link[0x6].rxAlignTarget(10);
  m->_link[0x7].rxAlignTarget(11);
  m->_link[0x8].rxAlignTarget(11);
  m->_link[0x9].rxAlignTarget(10);
  m->_link[0xa].rxAlignTarget(11);
  m->_link[0xb].rxAlignTarget(11);
  m->_link[0xc].rxAlignTarget(11);
  m->_link[0xd].rxAlignTarget(11);

  if (lLoopback) {
    for(unsigned i=0; i<NLINKS; i++) {
      m->_link[i].loopback(!m->_link[i].loopback());
      m->rxReset(i);
    }
    return 0;
  }

  if (lReset)
    m->_source.rxReset();

  if (lClockPoll)
    clockPoll(m);

  if (lErrorPoll) 
    errorPoll(m);

  if (lScanTest)
    scanTest(m);

  if (ltxScan)
    txScanFast(m,txScanBegin,txScanEnd);

  if (vphaseScan>=0)
    phaseScan(m,vphaseScan);

  if (hlink>=0)
    latencyHist(m,hlink);

  if (alink>=0)
    txAlignHist(m,alink);

  // if (tlink>=0)
  //   txResetScan(m,tlink);
  if (tlink>=0) {
    m->txReset(tlink);
    return 0;
  }

  if (rlink>=0) {
    if ((rlink%7)==0)
      m->pllReset(rlink/7);
    m->rxReset(rlink);
  }

  if (link>=0) {
    m->_link[link].txDelay(delay);
  }

  printf("clkRate");
  for(unsigned i=0; i<5; i++) {
    printf("  %f MHz", double(m->_refclk[i].rateHz())*1.e-6);
  }
  printf("\n");

  for(unsigned i=0; i<2; i++) {
    unsigned v = unsigned(m->_pll[i]._reg);
    printf("  %08x [%s %s]", v, (v&(1<<30)) ? "LOL":"", (v&(1<<31)) ? "LOS":"");
  }
  for(unsigned i=0; i<2; i++) {
    unsigned v = unsigned(m->_qpll[i]);
    printf("  %02x [%s.%02x %s.%02x %s %s]", v, 
           ((v>>0)&1)?"LOCK":"UNL", (v>>16)&0xff,
           ((v>>1)&1)?"LOCK":"UNL", (v>>24)&0xff,
           ((v>>2)&1)?"RST":"",
           ((v>>3)&1)?"RST":"" );
  }
  printf("\n-----\n");

  {
    printf("SRC ");
    unsigned v = unsigned(m->_source._csr);
    printf("  %08x  RxErr %04u  Latency %02x[%02x.%02x]  Phase %08x\n", 
           v, 
           m->_source.rxErrors     (),
           m->_source.rxAlignValue (),
           m->_source.rxAlignTarget(),
           m->_source.rxAlignMask  (),
           unsigned(m->_source._loopPhase));
  }
  for(unsigned i=0; i<NLINKS; i++) {
    printf("CH%02u",i);
    unsigned v = unsigned(m->_link[i]._csr);
    printf("  %08x  RxErr %04u  Latency %02x[%02x.%02x]  Phase %08x\n",
           v, 
           m->_link[i].rxErrors     (),
           m->_link[i].rxAlignValue (),
           m->_link[i].rxAlignTarget(),
           m->_link[i].rxAlignMask  (),
           unsigned(m->_link[i]._loopPhase));
  }
  printf("-----\n");

  int init[7];

  for(unsigned i=0; i<7; i++)
    init[i] = m->_link[i].loopPhase();

  while(1) {
    usleep(1000000);
    for(unsigned i=0; i<7; i++) {
      int v = m->_link[i].loopPhase();
      printf(" %7.2f[%3u:%7.2f]", 
             pd_to_ps*double(v-init[i]), m->_link[i].loopClks(), pd_to_ps*double(v));
    }
    printf("\n");
  }
}

void clockPoll(TDM::Module* m)
{
    while(1) {
      printf("clkRate");
      for(unsigned i=0; i<5; i++) {
        printf("  %7.2f [%x]",
               double(m->_refclk[i].rateHz())*1.e-6,
               m->_refclk[i].status());
      }
      printf("\n");
      usleep(100000);
    }
}

void errorPoll(TDM::Module* m)
{
    unsigned buff[15]; memset(buff,0,15*sizeof(unsigned));
    unsigned prev[15];
    {
      unsigned v = unsigned(m->_source._csr)&0xfff;
      prev[NLINKS] = v&0xfff;
      printf(" %04u[%05x:%04x]", 
             v,
             unsigned(m->_source._csr)>>12,
             buff[NLINKS]);
    }
    for(unsigned i=0; i<NLINKS; i++) {
      unsigned v = unsigned(m->_link[i]._csr)&0xfff;
      prev[i] = v&0xfff;
      printf(" %04u[%05x:%04x]", 
             v,
             unsigned(m->_link[i]._csr)>>12,
             buff[i]);
    }
    printf("\n");

    while(1) {
      {
        unsigned v = unsigned(m->_source._csr)&0xfff;
        if (v < (prev[NLINKS]&0xfff)) v |= (1<<12);
        unsigned dv = v-prev[NLINKS];
        buff[NLINKS] += dv;
        prev[NLINKS] = v&0xfff;
        printf(" %04u[%05x:%04x]", 
               dv, 
               unsigned(m->_source._csr)>>12,
               buff[NLINKS]);
      }
      for(unsigned i=0; i<NLINKS; i++) {
        unsigned v = unsigned(m->_link[i]._csr)&0xfff;
        if (v < (prev[i]&0xfff)) v |= (1<<12);
        unsigned dv = v-prev[i];
        buff[i] += dv;
        prev[i] = v&0xfff;
        printf(" %04u[%05x:%04x]", 
               dv, 
               unsigned(m->_link[i]._csr)>>12,
               buff[i]);
      }
      printf("\n");
      usleep(100000);
    }
}

static void txWait(TDM::Module* m,
                   unsigned             v,
                   bool              fast=false)
{
  unsigned s = v;
  unsigned q = 0x7e;
  for(unsigned i=0; i<7; i++)
    m->_link[i].txDelay(s,fast);
  do {
    usleep(fast ? 10000 : 1000000);
    for(unsigned i=0; i<7; i++)
      if (m->_link[i].txDelay() == s)
        q &= ~(1<<i);
    if (lverbose)
      printf("..%x[%x]\n",q,m->_link[1].txDelay());
  } while( q != 0 );
}

//
//  Adjust TXD control to test for any bias
//
static void scanTest(TDM::Module* m)
{
  unsigned s = 0x1000;
  txWait(m,s,true);

  int init[NLINKS];
  usleep(1000000);
  for(unsigned j=0; j<NLINKS; j++)
    init[j] = m->_link[j].loopPhase();

  for(unsigned i=0; i<10; i++) {
    for(unsigned j=0; j<100; j++) {
      txWait(m,s+i,true);
      txWait(m,s,true);
    }
    usleep(1000000);
    printf("%u ",i);
    for(unsigned j=0; j<NLINKS; j++) {
      int v = m->_link[j].loopPhase();
      printf(" %7.2f", pd_to_ps*double(v-init[j]));
    }
    printf("\n");
  }
  exit(1);
}

void txScan(TDM::Module* m,
            unsigned             begin,
            unsigned             end)
{
  txWait(m,begin);

  unsigned s = end;
  unsigned q = (1<<NLINKS)-1;
  for(unsigned i=0; i<NLINKS; i++)
    m->_link[i].txDelay(s);
  do {
    usleep(1000000);
    printf("%08x", s);
    for(unsigned i=0; i<NLINKS; i++) {
      unsigned g = m->_link[i].txDelay();
      int v = m->_link[i].loopPhase();
      printf(" %08x %3u %7.2f", g, m->_link[i].loopClks(), pd_to_ps*double(v));
      if (g == s)
        q &= ~(1<<i);
    }
    printf("\n");
  } while( q != 0 );

  for(unsigned i=0; i<NLINKS; i++)
    m->_link[i].txDelay(0);

  exit(0);
}

void txScanFast(TDM::Module* m,
                unsigned             begin,
                unsigned             end)
{
  txWait(m,begin,true);

  for(unsigned i=0; i<7; i++)
    printf("%u ",m->_link[i].rxAlignValue());
  printf("\n");

  /*
  for(unsigned j=0; j<16; j++) {
    for(unsigned i=0; i<7; i++)
      m->_link[i].pmDelay(j);
    usleep(2000000);
    for(unsigned i=0; i<7; i++) {
      int v = m->_link[i].loopPhase();
      printf(" %02u %3u %d", j, m->_link[i].loopClks(), v);
    }
    printf("\n");
  }
  printf("---\n");
  */

  for(unsigned i=0; i<7; i++)
    m->_link[i].pmDelay(0);

  for(unsigned j=0; j<51; j++) {
    unsigned s = begin + j*(end-begin)/50;
    txWait(m,s,true);
    usleep(2000000);
    printf("%08x", s);
    for(unsigned i=0; i<7; i++) {
      unsigned g = m->_link[i].txDelay();
      int v = m->_link[i].loopPhase();
      printf(" %08x %3u %d", g, m->_link[i].loopClks(), v);
    }
    printf("\n");
  } 

  for(unsigned i=0; i<7; i++)
    m->_link[i].txDelay(0,true);

  exit(0);
}

void phaseScan(TDM::Module* m,
               int                  scan)
{
    unsigned buff[128]; memset(buff,0,128*sizeof(unsigned));
    for(unsigned j=0; j<256; j++) {
      //      for(unsigned i=0; i<NLINKS; i++) {
      { unsigned i=0;
        m->_link[i]._csr = unsigned(m->_link[i]._csr) | (1<<29);
        usleep(10);
        m->_link[i]._csr = unsigned(m->_link[i]._csr) &~ (1<<29);
      }
      usleep(200000);
      //      for(unsigned i=0; i<NLINKS; i++) {
      { unsigned i=0;
        unsigned v = m->_link[i].txDelay();
        printf("  %08x", v);

        v = (v>>16)&0x7f;
        buff[v]++;

        if (scan==0 && v > 0x7c)
          exit(0);
      }
      printf("\n");
    }
    for(unsigned i=0; i<128; i++)
      printf("%04u%c", buff[i], (i&7)==7 ? '\n':' ');

    exit(0);
}

void latencyHist(TDM::Module* m,
                 int link)
{
    printf("RXLOCK\n");
    for(unsigned i=0; i<128; i++)
      printf("%5u%c", m->_link[link].rxLockEntry(i), (i%10)==9 ? '\n':' ');
    /*
    printf("\nTXLOCK\n");
    for(unsigned i=0; i<1024; i++) {
      printf("%5u%c", m->_link[link].txLockEntry(i), (i%10)==9 ? '\n':' ');
    }
    */
    printf("\n");

    exit(0);
}

void txResetScan(TDM::Module* m,
                 int link)
{
  m->_link[link].txAlignTarget(-1U);
  while(1) {
    m->txReset(link);
    bool lock=false;
    do {
      usleep(10000);
      bool ll = m->_link[link].allLock();
      if (lverbose) 
        printf("       [%08x]\n",unsigned(m->_link[link]._csr));
      if (ll & lock) break;
      lock = ll;
    } while (1);
    usleep(1000000);
    int v = m->_link[link].loopPhase();
    printf("  %7.2f:%06u [%08x]\n", 
           pd_to_ps*double(v), 
           m->_link[link].txAlignValue(),
           unsigned(m->_link[link]._csr));
  }
}

void txAlignHist(TDM::Module* m, 
                 int link)
{
  unsigned h[256];
  memset(h,0,256*sizeof(unsigned));

  while(1) {
    unsigned p = 0;
    for(unsigned i=0; i<128; ) {
      unsigned v = m->_link[link].txAlignValue();
      if (v != p) {
        h[v>>8]++;
        i++;
      }
      p = v;
      usleep(10000);
    }
    for(unsigned i=0; i<256; i++)
      printf("%04u%c", h[i], (i%10)==9 ? '\n':' ');
    printf("\n---\n");
  }
}
