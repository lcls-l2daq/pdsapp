
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

#include <string>

static inline double dtime(timespec& tsn, timespec& tso)
{
  return double(tsn.tv_sec-tso.tv_sec)+1.e-9*(double(tsn.tv_nsec)-double(tso.tv_nsec));
}

extern int optind;

using namespace Pds::Xpm;

void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <IP addr (dotted notation)> : Use network <IP>\n");
  printf("         -L <0 - 7>                     : Enable selected link\n");
}

void sigHandler( int signal ) {
  Module* m = Module::locate();
  m->setL0Enabled(false);
  ::exit(signal);
}

int main(int argc, char** argv) {

  extern char* optarg;

  int c;
  bool lUsage = false;

  const char* ip = "192.168.2.10";
  unsigned short port = 8192;
  int fixedRate=-1;
  unsigned linkEnable=0;

  while ( (c=getopt( argc, argv, "a:F:L:h")) != EOF ) {
    switch(c) {
    case 'a':
      ip = optarg; break;
      break;
    case 'F':
      fixedRate = atoi(optarg);
      break;
    case 'L':
      linkEnable = strtoul(optarg,NULL,0);
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

  Pds::Cphw::Reg::set(ip, port, 0x80000000);

  Module* m = Module::locate();

  while(1) {
    unsigned rx = m->rxLinkStat();
    unsigned tx = m->txLinkStat();
    printf("rx/tx Status: %08x/%08x \t %c %c %04x %c\n",
           rx,tx,
           ((rx>>31)&1)?'.':'+',  // Revisit: These are now meaningless
           ((rx>>30)&1)?'.':'+',  // Revisit: These are now meaningless
           ((rx>>16)&0x3fff),     // Revisit: These are now meaningless
           ((rx>>0)&1)?'.':'+');  // Revisit: These are now meaningless
    usleep(100000);
  }

  return 0;
}
