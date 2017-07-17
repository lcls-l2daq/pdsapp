#include "pds/cphw/AmcTiming.hh"

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <new>

using namespace Pds::Cphw;

void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <ip address, dotted notation>\n");
}

int main(int argc, char* argv[])
{
  extern char* optarg;
  char c;

  const char* ip = 0;
  double interval=1.0;

  while ( (c=getopt( argc, argv, "a:t:h")) != EOF ) {
    switch(c) {
    case 'a':
      ip = optarg;
      break;
    case 't':
      interval = strtod(optarg,NULL);
      break;
    default:
      usage(argv[0]);
      return 0;
    }
  }

  Pds::Cphw::Reg::set(ip, 8192, 0);
  Pds::Cphw::AmcTiming* t = new((void*)0) Pds::Cphw::AmcTiming;

  unsigned rxErrs = 0;
  char buff[256];

  while(1) {
    usleep(interval*1e6);
    unsigned newErrs = t->RxDecErrs+t->RxDspErrs;
    if (newErrs != rxErrs) {
      time_t t = time(NULL);
      ctime_r(&t,buff);
      buff[strlen(buff)-1]=0;
      printf("%s: %s: %u\n",buff,ip,newErrs);
      rxErrs = newErrs;
    }

  }

  return 0;
}
