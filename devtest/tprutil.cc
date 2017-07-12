
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

#include "pds/tpr/Module.hh"
#include "pds/service/CmdLineTools.hh"

#include <string>

extern int optind;

using namespace Pds;

static void dumpClk(Tpr::TprCore& core)
{
  struct timespec ts; ts.tv_sec=1; ts.tv_nsec=0;
  unsigned v0,v1;
  for(unsigned i=0; i<10; i++) {
    v0 = core.RxRecClks;
    nanosleep(&ts,0);
    v1 = core.RxRecClks;
    unsigned d = v1-v0;
    double r = double(d)*16.e-6;
    double dr = r - 1300/7.;
    printf("RxRecClkRate: %7.4f MHz [%7.4f]\n",r,dr);
  }
}

void usage(const char* p) {
  printf("Usage: %s -r <evr a/b>\n",p);
}

int main(int argc, char** argv) {

  extern char* optarg;
  char evrid='a';

  char* endptr;
  int c;
  bool lUsage = false;
  bool parseOK;

  bool lCheckClk = false;

  while ( (c=getopt( argc, argv, "cr:h")) != EOF ) {
    switch(c) {
    case 'r':
      evrid  = optarg[0];
      if (strlen(optarg) != 1) {
        printf("%s: option `-r' parsing error\n", argv[0]);
        lUsage = true;
      }
      break;
    case 'c':      lCheckClk = true; break;
    case 'h':
      usage(argv[0]);
      exit(0);
    case '?':
    default:
      lUsage = true;
      break;
    }
  }

  if (optind < argc) {
    printf("%s: invalid argument -- %s\n",argv[0], argv[optind]);
    lUsage = true;
  }

  if (lUsage) {
    usage(argv[0]);
    exit(1);
  }

  {
    Tpr::TprReg* p = reinterpret_cast<Tpr::TprReg*>(0);
    printf("RxRecClks[%p]\n",&p->tpr.RxRecClks);
  }

  {
    char evrdev[16];
    sprintf(evrdev,"/dev/tpr%c",evrid);
    printf("Using tpr %s\n",evrdev);

    int fd = open(evrdev, O_RDWR);
    if (fd<0) {
      perror("Could not open");
      return -1;
    }

    if (lCheckClk) {
      void* ptr = mmap(0, sizeof(Tpr::TprReg), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
      if (ptr == MAP_FAILED) {
        perror("Failed to map");
        return -2;
      }
      
      Tpr::TprReg* p = reinterpret_cast<Tpr::TprReg*>(ptr);
      dumpClk(p->tpr);
      //      p->tpr.dump();
      //      p->base.dump();
    }
  }

  return 0;
}

