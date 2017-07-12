
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

#include "pds/service/CmdLineTools.hh"
#include "pds/management/SegmentLevel.hh"
#include "pds/management/EventAppCallback.hh"
#include "pds/management/FastSegWire.hh"
#include "pds/service/Task.hh"

#include "pds/tpr/Server.hh"
#include "pds/tpr/Manager.hh"
#include "pds/tpr/Module.hh"

#include <string>

extern int optind;

static const unsigned NCHANNELS = 12;
static const unsigned NTRIGGERS = 12;

static unsigned eventFrames;
static unsigned bsaControlFrames;
static unsigned bsaChannelFrames[NCHANNELS];
static unsigned bsaChannelAcqs  [NCHANNELS];
static uint64_t bsaChannelMask  [NCHANNELS];
static unsigned dropFrames;
static unsigned nPrint = 0;

static void* read_thread(void*);

using namespace Pds::Tpr;

void usage(const char* p) {
  printf("Usage: %s -r <evr a/b> [-T|-L]\n",p);
  printf("\t-L program xbar for nearend loopback\n");
  printf("\t-T accesses TPR (BAR1)\n");
}

int main(int argc, char** argv) {

  // parse the command line for our boot parameters
  const uint32_t NO_PLATFORM = uint32_t(-1UL);
  uint32_t  platform  = NO_PLATFORM;
  char*     evrid     = 0;
  bool      lUsage    = false;
  bool      lMonitor  = false;

  Pds::DetInfo info(0,DetInfo::NoDetector,0,DetInfo::NoDevice,0);

  char* uniqueid = (char *)NULL;
  EventOptions options;

  int c;
  while ( (c=getopt( argc, argv, "i:p:r:u:def:mh")) != EOF) {
    switch(c) {
    case 'i':
      if (!CmdLineTools::parseDetInfo(optarg,info)) {
        printf("%s: option `-i' parsing error\n", argv[0]);
        lUsage = true;
      }
      break;
    case 'p':
      if (!CmdLineTools::parseUInt(optarg,platform)) {
        printf("%s: option `-p' parsing error\n", argv[0]);
        lUsage = true;
      }
      break;
    case 'r':
      evrid = optarg;
      if (strlen(evrid) != 1) {
        printf("%s: option `-r' parsing error\n", argv[0]);
        lUsage = true;
      }
      break;
    case 'u':
      if (strlen(optarg) > SrcAlias::AliasNameMax-1) {
        printf("Device alias '%s' exceeds %d chars, ignored\n", optarg, SrcAlias::AliasNameMax-1);
      } else {
        uniqueid = optarg;
      }
      break;
    case 'd': options.mode = EventOptions::Display; break;
    case 'e': options.mode = EventOptions::Decoder; break;
    case 'f': options.outfile = optarg; break;
    case 'm': lMonitor = true; break;
    case 'h':
      usage(argv[0]);
      return 0;
    case '?':
    default:
      lUsage = true;
    }
  }

  if (platform==NO_PLATFORM) {
    printf("%s: platform required\n",argv[0]);
    lUsage = true;
  }

  options.platform = platform;

//   if (optind < argc) {
//     printf("%s: invalid argument -- %s\n",argv[0], argv[optind]);
//     lUsage = true;
//   }

  if (info.detector() == Pds::DetInfo::NumDetector) {
    printf("%s: detinfo is required\n", argv[0]);
    lUsage = true;
  }

  if (!options.validate(argv[0]))
    return 0;

  if (lUsage) {
    usage(argv[0]);
    exit(1);
  }

  TprDS::TprReg* p;

  { char dev[16];
    sprintf(dev,"/dev/tpr%c3",evrid ? evrid[0] : 'a');
    printf("Using tpr %s\n",dev);

    int fd = open(dev, O_RDWR);
    if (fd<0) {
      perror("Could not open");
      return -1;
    }

    void* ptr = mmap(0, sizeof(Tpr::TprReg), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
      perror("Failed to map");
      return -2;
    }

    reinterpret_cast<TprReg*>(ptr)->xbar.outMap[2]=0;
    reinterpret_cast<TprReg*>(ptr)->xbar.outMap[3]=1;

    printf("Axi Version [%p]: BuildStamp: %s\n", 
	   &(p->version),
	   p->version.buildStamp().c_str());

    p = reinterpret_cast<TprDS::TprReg*>(ptr);
  }

  int fd;
  TprQueues* q;

  { char dev[16];
    sprintf(dev,"/dev/tpr%c0",evrid ? evrid[0] : 'a');
    printf("Using tpr %s\n",dev);

    fd = open(dev, O_RDONLY);
    if (fd<0) {
      perror("Could not open");
      return -1;
    }

    void* ptr = mmap(0, sizeof(TprQueues), PROT_READ, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
      perror("Failed to map");
      exit(1);
    }

    q = (TprQueues*)ptr;
  }


  { for(unsigned i=0; i<12; i++)
      p->base.channel[i].control=0;
    uint32_t data[1024];
    Tpr::RxDesc desc(data,1024);
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    while( ::poll(&pfd,1,1000)>0 )
      ::read(fd,&desc,sizeof(Tpr::RxDesc));
  }
  

  Pds::DetInfo src(p->base.partitionAddr,
                   info.detector(),info.detId(),
                   info.device  (),info.devId());
  printf("Using %s\n",Pds::DetInfo::name(src));

  TprDS::Server*  server  = new TprDS::Server (fd, src);
  TprDS::Manager* manager = new TprDS::Manager(*p, *server, 
                                               *new CfgClientNfs(src),
                                               lMonitor);

  Task* task = new Task(Task::MakeThisATask);
  std::list<Appliance*> apps;
  if (options.outfile)
    apps.push_back(new RecorderQ(options.outfile, 
                                 options.chunkSize, 
                                 options.uSizeThreshold, 
                                 options.delayXfer, 
                                 NULL,
                                 options.expname));
  switch(options.mode) {
  case EventOptions::Counter:
    apps.push_back(new CountAction); break;
  case EventOptions::Decoder:
    apps.push_back(new Decoder(Level::Segment)); break;
  case EventOptions::Display:
    apps.push_back(new StatsTree); break;
  default:
    break;
  }
  apps.push_back(&manager->appliance());
  EventAppCallback* seg = new EventAppCallback(task, platform, apps);
  FastSegWire settings(*server, p->base.partitionAddr, uniqueid, 
                       (1<<12), 16, (1<<15));
  SegmentLevel* seglevel = new SegmentLevel(platform, settings, *seg);
  seglevel->attach();

  task->mainLoop();
  return 0;
}
