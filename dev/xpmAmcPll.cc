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

#include "pds/xpm/Module.hh"
#include "pds/cphw/RingBuffer.hh"
#include "pds/cphw/AmcTiming.hh"

#include <string>

enum { NDSLinks = 14 };

extern int optind;

static bool done = false;

using namespace Pds::Xpm;

typedef struct
{
  unsigned linkEnables;
  unsigned duration;
  unsigned count;
  unsigned rxErrs[NDSLinks];
  bool     done;
} LinkResults;

static void* handle_results(void*);

void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <IP addr (dotted notation)> : Use network <IP>\n");
  printf("         -L <ds link mask>              : Enable selected link(s)\n");
  printf("         -R                             : Reset selected link(s)\n");
  printf("         -l                             : Put selected link(s) in loopback mode\n");
  printf("         -f                             : Select PLL frequency\n");
  printf("         -S                             : Select PLL bandwidth\n");
  printf("         -r                             : Reset PLL\n");
  printf("         -s <skew steps>                : Change PLL skew\n");
  printf("         -b                             : Bypass PLL\n");
  printf("         -d <seconds>                   : Duration to measure rxLinkErrs for\n");
}

void sigHandler( int signal ) {
  done = true;
}

void print(unsigned linkEnables, unsigned count, unsigned errs[2][NDSLinks], unsigned dCnt)
{
  unsigned cnt = count;
  unsigned prv = cnt++ & 1;
  unsigned cur = cnt++ & 1;

  printf("second: %5u, dCnt = %u, dRxErrs = ", count, dCnt);
  for (unsigned i = 0; linkEnables; i++)
  {
    if (linkEnables&(1<<i))
    {
      unsigned diff = errs[cur][i] - errs[prv][i];
      printf("%u: %f  ", i, (double)diff / (double)dCnt);
      linkEnables &= ~(1 << i);
    }
  }
  printf("\n");
}

int main(int argc, char** argv) {

  extern char* optarg;

  int c;
  bool lUsage = false;

  const char* ip        = "10.0.2.102";
  unsigned short port   = 8192;
  unsigned linkEnable   = (1 << NDSLinks) - 1;
  unsigned linkLoopback = 0;
  unsigned duration     = 5;            // Seconds
  int skewSteps=0;
  bool lreset=false;
  bool lPll=false;
  bool lPllbypass=false;
  int  freqSel=-1;
  int  bwSel=-1;

  while ( (c=getopt( argc, argv, "a:L:l:s:f:S:d:DRbrh")) != EOF ) {
    switch(c) {
    case 'a':
      ip = optarg; break;
      break;
    case 'L':
      linkEnable = strtoul(optarg,NULL,0);
      break;
    case 'R':
      lreset = true;
      break;
    case 'f':
      freqSel = strtoul(optarg,NULL,0);
      break;
    case 'S':
      bwSel = strtoul(optarg,NULL,0);
      break;
    case 'r':
      lPll = true;
      break;
    case 'b':
      lPllbypass = true;
      break;
    case 's':
      skewSteps = atoi(optarg);
      break;
    case 'l':
      linkLoopback = strtoul(optarg,NULL,0);
      break;
    case 'd':
      duration = strtoul(optarg,NULL,0);
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

  Pds::Cphw::AmcTiming* t = new((void*)0) Pds::Cphw::AmcTiming;
  printf("buildStamp %s\n",t->version.buildStamp().c_str());

  t->resetStats();
  usleep(1000);
  unsigned rxDecErrs = (unsigned)t->RxDecErrs;
  unsigned rxDspErrs = (unsigned)t->RxDspErrs;
  usleep(1000);

  if ((((unsigned)t->CSR & 0x02) == 0)      ||
      ((unsigned)t->RxDecErrs != rxDecErrs) ||
      ((unsigned)t->RxDspErrs != rxDspErrs))
  {
    printf("Exiting because source timing does not appear to be stable:\n");
    t->dumpStats();
    exit(1);
  }

  Module* m = new((void*)0x80000000) Module;

  m->dumpPll();

  if (lPll) {
    m->pllBwSel  (6);
    m->pllRateSel(0xa);
    m->pllFrqSel (0x692);
    m->pllBypass (false);
    m->pllReset  ();
    usleep(10000);
    m->dumpPll();
  }

  if (freqSel>=0) {
    m->pllFrqSel(freqSel);
    m->pllReset();
  }

  if (bwSel>=0) {
    m->pllBwSel(bwSel);
    m->pllReset();
  }

  if (skewSteps)
    m->pllSkew(skewSteps);

  if (lPllbypass) {
    m->pllBypass(true);
    m->pllReset();
  }

  m->dumpPll();

  unsigned links = (1 << NDSLinks) - 1;
  for(unsigned i=0; links; i++)
    if (links&(1<<i)) {
      m->linkLoopback(i,(linkLoopback&(1<<i))!=0);
      links &= ~(1<<i);
    }

  if (lreset) {
    unsigned linkReset=linkEnable;
    for(unsigned i=0; linkReset; i++)
      if (linkReset&(1<<i)) {
        m->txLinkReset(i);
        m->rxLinkReset(i);
        usleep(10000);
        linkReset &= ~(1<<i);
      }
  }

  m->setL0Enabled(false);

  m->clearLinks();
  links = linkEnable;
  for(unsigned i=0; links; i++)
    if (links&(1<<i)) {
      m->linkEnable(i,true);
      links &= ~(1<<i);
    }

  m->init();
  links = linkEnable;
  for(unsigned i=0; links; i++)
    if (links&(1<<i)) {
      printf("rx/tx Status: %u: %08x/%08x\n", i, m->rxLinkStat(i), m->txLinkStat(i));
      links &= ~(1<<i);
    }

  ::signal( SIGINT, sigHandler );

  LinkResults lr;
  memset(&lr, 0, sizeof(lr));
  lr.linkEnables = linkEnable;
  lr.duration    = duration;

  //
  //  Create thread to report results
  //
  pthread_attr_t tattr;
  pthread_attr_init(&tattr);
  pthread_t tid;
  if (pthread_create(&tid, &tattr, &handle_results, &lr))
    perror("Error creating results reporting thread");

  unsigned  count = 0;
  while (!lr.done)
  {
    lr.count = count++;

    unsigned links = linkEnable;
    for (unsigned i = 0; links; i++)
    {
      if (links&(1<<i))
      {
        m->setLink(i);
        unsigned e0 = m->_dsLinkStatus;
        usleep(350);      // In 350 uS, the 186 MHz clock will count ~64K times
        unsigned e1 = m->_dsLinkStatus;
        unsigned short diff = ((unsigned short)(e1 & 0x0000ffff) -
                               (unsigned short)(e0 & 0x0000ffff));
        lr.rxErrs[i] += diff;
        links &= ~(1 << i);
      }
    }
  }

  m->setL0Enabled(false);

  void* retVal;
  pthread_join(tid, &retVal);

  return 0;
}


void* handle_results(void* arg)
{
  LinkResults* lr = (LinkResults*)arg;
  unsigned count = 0;
  unsigned rxErrs[2][NDSLinks];
  unsigned cntN = lr->count, cnt0 = cntN - 1;

  while (!done)
  {
    usleep(1000000);

    cnt0 = cntN;
    memcpy(rxErrs[count++ & 1], lr->rxErrs, sizeof(lr->rxErrs));
    cntN = lr->count;

    unsigned links = lr->linkEnables;
    bool changed = false;
    for (unsigned i = 0; links; i++)
    {
      if (links&(1<<i))
      {
        changed |= (rxErrs[0][i] != rxErrs[1][i]);
        links &= ~(1 << i);
      }
    }

    if (changed & !done)  print(lr->linkEnables, count, rxErrs, cntN - cnt0);
    else                { printf("second: %5u\r", count); fflush(stdout); }

    if (lr->duration && (count == lr->duration))  break;
  }

  print(lr->linkEnables, count, rxErrs, cntN - cnt0);

  lr->done = true;

  pthread_exit(0);
  return 0;
}
