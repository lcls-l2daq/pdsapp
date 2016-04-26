//
//  Run LCLS-II EVR in DAQ mode
//    L0T from partition word
//    No BSA
//    N trigger channels
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

#include "evgr/evr/evr.hh"

#include <string>
#include <vector>

#include "pds/service/Histogram.hh"
#include "pds/tpr/Module.hh"

using Tpr::DmaControl;
using Tpr::XBar;
using Tpr::TprCore;
using Tpr::RingB;
using Tpr::TpgMini;

namespace TprDS {

class TprBase {
public:
  enum { NCHANNELS=12 };
  enum { NTRIGGERS=12 };
  enum Destination { Any };
  enum FixedRate { _1M, _500K, _100K, _10K, _1K, _100H, _10H, _1H };
public:
  void dump() const;
  void setupDaq    (unsigned i,
                    unsigned partition,
                    unsigned dataLength);
public:
  volatile uint32_t irqEnable;
  volatile uint32_t irqStatus;;
  volatile uint32_t reserved_8;
  volatile uint32_t gtxDebug;
  volatile uint32_t countReset;
  volatile uint32_t dmaFullThr; // [32-bit words]
  volatile uint32_t reserved_18[2];
  struct {  // 0x20
    volatile uint32_t control;
    volatile uint32_t evtSel;
    volatile uint32_t evtCount;
    volatile uint32_t testData;  // [32-bit words]
    volatile uint32_t reserved[4];
  } channel[NCHANNELS];
  volatile uint32_t reserved_20[2];
  volatile uint32_t frameCount;
  volatile uint32_t pauseCount;
  volatile uint32_t overflowCount;
  volatile uint32_t idleCount;
  volatile uint32_t reserved_38[1];
  volatile uint32_t reserved_b[1+(14-NCHANNELS)*8];
  struct { // 0x200
    volatile uint32_t control; // input, polarity, enabled
    volatile uint32_t delay;
    volatile uint32_t width;
    volatile uint32_t reserved_t;
  } trigger[NTRIGGERS];
};

// Memory map of TPR registers (EvrCardG2 BAR 1)
class TprReg {
public:
  TprBase  base;
  volatile uint32_t reserved_0    [(0x400-sizeof(TprBase))/4];
  DmaControl dma;
  volatile uint32_t reserved_1    [(0x10000-0x400-sizeof(DmaControl))/4];
  XBar     xbar;
  volatile uint32_t reserved_xbar [(0x30000-sizeof(XBar))/4];
  TprCore  tpr;
  volatile uint32_t reserved_tpr  [(0x10000-sizeof(TprCore))/4];
  RingB    ring0;
  volatile uint32_t reserved_ring0[(0x10000-sizeof(RingB))/4];
  RingB    ring1;
  volatile uint32_t reserved_ring1[(0x10000-sizeof(RingB))/4];
  TpgMini  tpg;
};
};

void TprDS::TprBase::dump() const {
  static const unsigned NChan=12;
  printf("irqEnable [%p]: %08x\n",&irqEnable,irqEnable);
  printf("irqStatus [%p]: %08x\n",&irqStatus,irqStatus);
  printf("gtxDebug  [%p]: %08x\n",&gtxDebug  ,gtxDebug);
  printf("dmaFullThr[%p]: %08x\n",&dmaFullThr,dmaFullThr);
  printf("channel0  [%p]\n",&channel[0].control);
  printf("control : ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",channel[i].control);
  printf("\nevtCount: ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",channel[i].evtCount);
  printf("\nevtSel  : ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",channel[i].evtSel);
  printf("\ntestData : ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",channel[i].testData);
  printf("\nframeCnt: %08x\n",frameCount);
  printf("pauseCnt  : %08x\n",pauseCount);
  printf("ovfloCnt  : %08x\n",overflowCount);
  printf("idleCnt   : %08x\n",idleCount);
}

void TprDS::TprBase::setupDaq    (unsigned i,
                                  unsigned partition,
                                  unsigned dataLength) {
  channel[i].evtSel    = (1<<31) | (3<<14) | partition; // 
  //  channel[i].evtSel    = (1<<31) | (0<<14) | partition; // 10Hz fixed rate
  channel[i].testData  = dataLength;
  channel[i].control   = 5;
}

extern int optind;

static const unsigned NCHANNELS = 14;
static const unsigned NTRIGGERS = 12;
using namespace TprDS;

class ThreadArgs {
public:
  int fd;
  unsigned busyTime;
  sem_t sem;
};


class DaqStats {
public:
  DaqStats() : _values(5) {
    for(unsigned i=0; i<_values.size(); i++)
      _values[i]=0;
  }
public:
  static const char** names();
  std::vector<unsigned> values() const { return _values; }
public:
  unsigned& eventFrames () { return _values[0]; }
  unsigned& dropFrames  () { return _values[1]; }
  unsigned& repeatFrames() { return _values[2]; }
  unsigned& tagMisses   () { return _values[3]; }
  unsigned& corrupt     () { return _values[4]; }
private:
  std::vector<unsigned> _values;
};  

const char** DaqStats::names() {
  static const char* _names[] = {"eventFrames",
                                 "dropFrames",
                                 "repeatFrames",
                                 "tagMisses",
                                 "corrupt" };
  return _names;
}


class DmaStats {
public:
  DmaStats() : _values(4) {
    for(unsigned i=0; i<_values.size(); i++)
      _values[i]=0;
  }
  DmaStats(TprDS::TprBase& o) : _values(4) {
    //    frameCount   () = o.frameCount;
    frameCount   () = o.channel[1].evtCount;
    pauseCount   () = o.pauseCount;
    overflowCount() = o.overflowCount;
    idleCount    () = o.idleCount;
  }

public:
  static const char** names();
  std::vector<unsigned> values() const { return _values; }
public:
  unsigned& frameCount   () { return _values[0]; }
  unsigned& pauseCount   () { return _values[1]; }
  unsigned& overflowCount() { return _values[2]; }
  unsigned& idleCount    () { return _values[3]; }
private:
  std::vector<unsigned> _values;
};  

const char** DmaStats::names() {
  static const char* _names[] = {"frameCount",
                                 "pauseCount",
                                 "overflowCount",
                                 "idleCount" };
  return _names;
}


template <class T> class RateMonitor {
public:
  RateMonitor() {}
  RateMonitor(const T& o) {
    clock_gettime(CLOCK_REALTIME,&tv);
    _t = o;
  }
  RateMonitor<T>& operator=(const RateMonitor<T>& o) {
    tv = o.tv;
    _t = o._t;
    return *this;
  }
public:
  void dump(const RateMonitor<T>& o) {
    double dt = double(o.tv.tv_sec-tv.tv_sec)+1.e-9*(double(o.tv.tv_nsec)-double(tv.tv_nsec));
    for(unsigned i=0; i<_t.values().size(); i++)
      printf("%10u %15.15s [%10u] : %g\n",
             _t.values()[i],
             _t.names()[i],
             o._t.values()[i]-_t.values()[i],
             double(o._t.values()[i]-_t.values()[i])/dt);
  }
private:
  timespec tv;
  T _t;
};
  

static DaqStats  daqStats;
static Pds::Histogram readSize(64,1);

static unsigned nPrint = 20;

static void* read_thread(void*);

void usage(const char* p) {
  printf("Usage: %s -r <evr a/b> [-T|-L]\n",p);
  printf("\t-L program xbar for nearend loopback\n");
  printf("\t-T accesses TPR (BAR1)\n");
}

static TprReg* tprReg=0;

void sigHandler( int signal ) {
  if (tprReg) {
    tprReg->base.channel[0].control=0;
  }
  readSize.dump();

  ::exit(signal);
}

int main(int argc, char** argv) {
  extern char* optarg;
  char evrid='a';
  unsigned partition=0;
  unsigned emptyThr=2;
  unsigned fullThr=-1U;
  unsigned length=16;
  unsigned rate=6;
  ThreadArgs args;
  args.fd = -1;
  args.busyTime = 0;
  
  int c;
  bool lUsage = false;
  while ( (c=getopt( argc, argv, "r:v:S:B:E:F:R:h")) != EOF ) {
    switch(c) {
    case 'r':
      evrid  = optarg[0];
      if (strlen(optarg) != 1) {
        printf("%s: option `-r' parsing error\n", argv[0]);
        lUsage = true;
      }
      break;
    case 'B':
      args.busyTime = strtoul(optarg,NULL,0);
      break;
    case 'E':
      emptyThr = strtoul(optarg,NULL,0);
      break;
    case 'F':
      fullThr = strtoul(optarg,NULL,0);
      break;
    case 'S':
      length = strtoul(optarg,NULL,0);
      break;
    case 'R':
      rate = strtoul(optarg,NULL,0);
    case 'v':
      nPrint = strtoul(optarg,NULL,0);
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
  //  Configure the XBAR for straight-in/out
  //
  {
    char evrdev[16];
    sprintf(evrdev,"/dev/er%c3",evrid);
    printf("Using evr %s\n",evrdev);

    int fd = open(evrdev, O_RDWR);
    if (fd<0) {
      perror("Could not open");
      return -1;
    }

    void* ptr = mmap(0, sizeof(Tpr::EvrReg), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
      perror("Failed to map");
      return -2;
    }

    Tpr::EvrReg* p = reinterpret_cast<Tpr::EvrReg*>(ptr);

    printf("SLAC Version[%p]: %08x\n", 
	   &(p->evr),
	   ((volatile uint32_t*)(&p->evr))[0x30>>2]);

    p->evr.IrqEnable(0);

    printf("Axi Version [%p]: BuildStamp: %s\n", 
	   &(p->version),
	   p->version.buildStamp().c_str());
    
    printf("[%p] [%p] [%p]\n",p, &(p->version), &(p->xbar));

    p->xbar.setTpr(XBar::StraightIn);
    p->xbar.setTpr(XBar::StraightOut);
  }

  //
  //  Configure channels, event selection
  //
    char evrdev[16];
    sprintf(evrdev,"/dev/er%c3_1",evrid);
    printf("Using tpr %s\n",evrdev);

    int fd = open(evrdev, O_RDWR);
    if (fd<0) {
      perror("Could not open");
      return -1;
    }

    args.fd  = fd;
    sem_init(&args.sem,0,0);

    void* ptr = mmap(0, sizeof(TprReg), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
      perror("Failed to map");
      return -2;
    }

    TprReg* p = tprReg = reinterpret_cast<TprReg*>(ptr);

    p->tpr.rxPolarity(false);  // RTM
    //    p->tpr.rxPolarity(true); // Timing RTM
    p->tpr.resetCounts(); 
    p->dma.setEmptyThr(emptyThr);
    p->base.dmaFullThr=fullThr;

    p->base.dump();
    p->dma.dump();
    p->tpr.dump();
    
    //  flush out all the old
    { printf("flushing\n");
      unsigned nflush=0;
      uint32_t* data = new uint32_t[1024];
      EvrRxDesc* desc = new EvrRxDesc;
      desc->maxSize = 1024;
      desc->data    = data;
      pollfd pfd;
      pfd.fd = args.fd;
      pfd.events = POLLIN;
      while(poll(&pfd,1,100)>0) { 
        read(args.fd, desc, sizeof(*desc));
        nflush++;
      }
      delete[] data;
      delete desc;
      printf("done flushing [%u]\n",nflush);
    }
    
    //
    //  Create thread to receive DMAS and validate the data
    //
    { 
      pthread_attr_t tattr;
      pthread_attr_init(&tattr);
      pthread_t tid;
      if (pthread_create(&tid, &tattr, &read_thread, &args))
        perror("Error creating read thread");
      usleep(10000);
    }

    ::signal( SIGINT, sigHandler );

    //    sem_wait(&args.sem);
    p->base.countReset = 1;
    usleep(1);
    p->base.countReset = 0;

    //  only one channel implemented
    p->base.setupDaq(0,rate,length);

    p->base.dump();

  RateMonitor<DaqStats> ostats(daqStats);
  DmaStats d;
  RateMonitor<DmaStats> dstats(d);

  unsigned och0=0;

  while(1) {
    usleep(1000000);

    printf("--------------\n");
    unsigned dmaStat = p->dma.rxFreeStat;
    printf("Full/Valid/Empty : %u/%u/%u  Count :%x\n",
           (dmaStat>>31)&1, (dmaStat>>30)&1, (dmaStat>>29)&1,
           dmaStat&0x3ff);

    { RateMonitor<DaqStats> stats(daqStats);
      ostats.dump(stats);
      ostats = stats; }

    printf("RxErrs/Resets: %08x/%08x\n", 
           p->tpr.RxDecErrs+p->tpr.RxDspErrs,
           p->tpr.RxRstDone);

    { unsigned uch0 = p->base.channel[0].evtCount;
      unsigned utot = p->base.channel[6].evtCount;
      printf("eventCount: %08x:%08x [%d]\n",uch0,utot,uch0-och0);
      och0 = uch0;
    }

    { DmaStats d(p->base);
      RateMonitor<DmaStats> dmaStats(d);
      dstats.dump(dmaStats);
      dstats = dmaStats; }
  }

  return 0;
}

void* read_thread(void* arg)
{
  ThreadArgs targs = *reinterpret_cast<ThreadArgs*>(arg);

  uint32_t* data = new uint32_t[1024];
  
  EvrRxDesc* desc = new EvrRxDesc;
  desc->maxSize = 1024;
  desc->data    = data;
  
  unsigned ntag = 0;
  uint64_t opid = 0;
  ssize_t nb;

  //  sem_post(&targs.sem);

  while(1) {
    if ((nb = read(targs.fd, desc, sizeof(*desc)))>=0) {
      { printf("READ %zd words\n",nb);
        uint32_t* p     = (uint32_t*)data;
        for(unsigned i=0; i<nb; i++)
          printf(" %08x",p[i]);
        printf("\n"); }
      uint32_t* p     = (uint32_t*)data;
      if (((p[1]>>0)&0xffff)==0) {
        daqStats.eventFrames()++;
        ntag = ((p[2]>>1)+1)&0x1f;
        opid = p[4];
        opid = (opid<<32) | p[3];
        break;
      }
    }
  }

  uint64_t pid_busy = opid+(1ULL<<20);

  while (1) {
    if ((nb = read(targs.fd, desc, sizeof(*desc)))<0)
      break;

    readSize.bump(nb);

    uint32_t* p     = (uint32_t*)data;
    uint32_t  len   = p[0];
    uint32_t  etag  = p[1];
    uint32_t  pword = p[2];
    uint64_t  pid   = p[4]; pid = (pid<<32)|p[3];

    if (etag&(1ULL<<30)) {
      daqStats.dropFrames()++;
    }

    if ((etag&0xffff)==0) {
      daqStats.eventFrames()++;

      unsigned tag = (pword>>1)&0x1f;

      if (tag != ntag) {
#if 1
        printf("Tag error: %x:%x  pid: %016lx:%016lx\n",
               tag, ntag, pid, opid);
#endif
        if (pid==opid) {
          daqStats.repeatFrames()++;
        }
        else {
          //          if ((p[2]>>48)==1) {
          if (0) {
            daqStats.corrupt()++;
            printf("corrupt [%zd]: ",nb);
            uint32_t* p32 = (uint32_t*)data;
            for(unsigned i=0; i<15; i++)
              printf(" %08x",p32[i]);
            printf("\n"); 
          }
          else
            daqStats.tagMisses() += ((tag-ntag)&0x1f);
        }
      }
      opid = pid;

      ntag = (tag+1)&0x1f;

      if (nPrint) {
        nPrint--;
	printf("EVENT  [0x%x]:",(etag&0xffff));
        for(unsigned i=0; i<nb+1; i++)
          printf(" %08x",p[i]);
        printf("\n");
      }

      if (targs.busyTime && opid > pid_busy) {
        usleep(targs.busyTime);
        pid_busy = opid + (1ULL<<20);
      }
    }
  }

  printf("read_thread done\n");

  return 0;
}
