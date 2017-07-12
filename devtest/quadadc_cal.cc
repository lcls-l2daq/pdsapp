//
//

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#include <poll.h>
#include <signal.h>
#include <arpa/inet.h>

#include <string>
#include <vector>

#include "pds/quadadc/Module.hh"
#include "pdsdata/psddl/generic1d.ddl.h"

using Pds::TprDS::TprBase;
using Pds::Tpr::RxDesc;

extern int optind;

static bool lVerbose = false;
static unsigned nPrint = 20;

class ThreadArgs {
public:
  int fd;
  int rate;
};

using namespace Pds;

void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options:\n");
  printf("\t-I interleave\n");
  printf("\t-R rate     : Set trigger rate [0:929kHz, 1:71kHz, 2:10kHz, 3:1kHz, 4:100Hz, 5:10Hz\n");
  printf("\t-V          : Dump out all events");
}

static Pds::QuadAdc::Module* reg=0;

void sigHandler( int signal ) {
  if (reg) {
    reg->base.stop();
    reg->dma_core.dump();
  }
  
  ::exit(signal);
}

int main(int argc, char** argv) {
  extern char* optarg;
  char evrid='a';
  unsigned length=16;  // multiple of 16
  bool lTest=false;
  Pds::QuadAdc::Module::TestPattern pattern = Pds::QuadAdc::Module::Flash11;
  int interleave = -1;
  unsigned delay=0;
  unsigned prescale=1;

  ThreadArgs args;
  args.fd = -1;
  args.rate = 6;

  int c;
  bool lUsage = false;
  while ( (c=getopt( argc, argv, "I:r:v:D:P:S:R:T:Vh")) != EOF ) {
    switch(c) {
    case 'I': interleave = atoi(optarg); break;
    case 'r':
      evrid  = optarg[0];
      if (strlen(optarg) != 1) {
        printf("%s: option `-r' parsing error\n", argv[0]);
        lUsage = true;
      }
      break;
    case 'S':
      length = strtoul(optarg,NULL,0);
      break;
    case 'R':
      args.rate = strtoul(optarg,NULL,0);
      break;
    case 'T':
      lTest = true;
      pattern = (Pds::QuadAdc::Module::TestPattern)strtoul(optarg,NULL,0);
      break;
    case 'V':
      lVerbose = true;
      break;
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

  //
  //  Configure channels, event selection
  //
  char dev[16];
  sprintf(dev,"/dev/qadc%c",evrid);
  printf("Using %s\n",dev);

  int fd = open(dev, O_RDWR);
  if (fd<0) {
    perror("Could not open");
    return -1;
  }

  args.fd  = fd;

  void* ptr = mmap(0, sizeof(Pds::QuadAdc::Module), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (ptr == MAP_FAILED) {
    perror("Failed to map");
    return -2;
  }

  Pds::QuadAdc::Module* p = reg = reinterpret_cast<Pds::QuadAdc::Module*>(ptr);

  ::signal( SIGINT, sigHandler );

  //
  //  Enabling the test pattern causes a realignment of the clocks
  //   (avoid it, if possible)
  //
  p->i2c_sw_control.select(Pds::QuadAdc::I2cSwitch::PrimaryFmc); 
  p->disable_test_pattern();
  if (lTest) {
    p->enable_test_pattern(pattern);
  }
  //  p->enable_test_pattern(Module::DMA);

  p->base.init();

  //  p->dma_core.init(0x400);
  p->dma_core.init(32+48*(length));
  // p->dma_core.init();
  p->dma_core.dump();

  //  p->dma.setEmptyThr(emptyThr);
  //  p->base.dmaFullThr=fullThr;

  p->base.dump();
  //  p->dma.dump();
  p->tpr.dump();

  //  flush out all the old
  { printf("flushing\n");
    unsigned nflush=0;
    uint32_t* data = new uint32_t[1<<20];
    RxDesc* desc = new RxDesc(data,1<<20);
    pollfd pfd;
    pfd.fd = args.fd;
    pfd.events = POLLIN;
    while(poll(&pfd,1,0)>0) { 
      read(args.fd, desc, sizeof(*desc));
      nflush++;
    }
    delete[] data;
    delete desc;
    printf("done flushing [%u]\n",nflush);
  }
    
  //    sem_wait(&args.sem);
  p->base.resetCounts();

  p->setAdcMux( interleave>=0, (1<<interleave));

  p->base.setupLCLS(args.rate,length);
  p->base.prescale = (delay<<6) | (prescale&0x3f);

  p->base.dump();

  Pds::Generic1D::ConfigV0* config;
  {
    //  Create the Generic1DConfig
    unsigned config_length     [4];
    unsigned config_sample_type[4];
    int      config_offset     [4];
    double   config_period     [4];
    unsigned delay = 0;
    unsigned channel = interleave;
    unsigned ps = 1;
      
    for(unsigned i=0; i<4; i++) {
      config_sample_type[i] = Pds::Generic1D::ConfigV0::UINT16;
      config_offset     [i] = double(delay)/119.e6;
      config_length     [i] = (interleave<0 || i==channel) ? length : 0;
      config_period     [i] = (0.8e-9)*(interleave<0 ? double(ps) : 0.25);
    }
    config = new (new char[Pds::Generic1D::ConfigV0(4,0,0,0,0)._sizeof()]) 
      Pds::Generic1D::ConfigV0(4,
                               config_length,
                               config_sample_type,
                               config_offset,
                               config_period);
  }

  p->base.start();

  uint32_t* data = new uint32_t[1<<24];
  
  RxDesc* desc = new RxDesc(data,1<<20);
  
  ssize_t nb;

  while(1) {
    if ((nb = read(args.fd, desc, sizeof(*desc)))<0) {
      //      perror("read error");
      //      break;
      //  timeout in driver
      continue;
    }

    uint32_t* p     = (uint32_t*)data;

    if (nPrint || lVerbose) {
      printf("EVENT  [0x%x]:",(p[1]&0xffff));
      unsigned ilimit = lVerbose ? nb : 16;
      for(unsigned i=0; i<ilimit; i++)
        printf(" %08x",p[i]);
      printf("\n");
      nPrint--;
    }

    //  Overwrite p[7] so it looks like a Generic1DData
    p[7] = 4*nb - 8*sizeof(uint32_t);

    const Pds::Generic1D::DataV0* data = reinterpret_cast<const Pds::Generic1D::DataV0*>(&p[7]);

    //  Analyze!
    
    //  loop over channels
    for(unsigned ch=0; ch<4; ch++) {
      ndarray<const uint16_t,1> array = data->data_u16(*config, ch);

      printf("channel %u: \n",ch);
      //  loop over samples
      for(unsigned s=0; s<array.shape()[0]; s++) {
        if (s<8)
          printf(" %u", array[s]);
      }
      printf("\n");
    }
  }

  printf("read_thread done\n");

  return 0;
}
