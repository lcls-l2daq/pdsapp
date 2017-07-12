
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

#include "evgr/evr/evr.hh"
#include "pds/epicstools/EpicsCA.hh"
#include "pds/epicstools/PVWriter.hh"

#include <string>
#include <vector>
#include <sstream>

extern int optind;

#define MOD_SHARED 12
#define MAX_TPR_ALLQ (32*1024)
#define MAX_TPR_CHNQ  1024
#define MSG_SIZE      32

static const unsigned BsaRecLen = 32*1024;

using Pds_Epics::EpicsCA;
using Pds_Epics::PVWriter;

namespace Pds {
  namespace Tpr {

// DMA Buffer Size, Bytes (could be as small as 512B)
#define BUF_SIZE 4096
#define NUMBER_OF_RX_BUFFERS 256

    class TprEntry {
    public:
      uint32_t word[MSG_SIZE];
    };

    class ChnQueue {
    public:
      TprEntry entry[MAX_TPR_CHNQ];
    };

    class TprQIndex {
    public:
      long long idx[MAX_TPR_ALLQ];
    };

    class TprQueues {
    public:
      TprEntry  allq  [MAX_TPR_ALLQ];
      ChnQueue  chnq  [MOD_SHARED];
      TprQIndex allrp [MOD_SHARED]; // indices into allq
      long long        allwp [MOD_SHARED]; // write pointer into allrp
      long long        chnwp [MOD_SHARED]; // write pointer into chnq's
      long long        gwp;
      int              fifofull;
    };
  };
};

using namespace Pds::Tpr;

static uint64_t eventFrames     =0;
static uint64_t bsaControlFrames=0;
static uint64_t bsaChannelFrames=0;

static void countFrame(const uint32_t* p)
{
  //
  //  We only expect BSA_CHN messages in this queue
  //
  switch(p[1]&0xffff) {
  case 0:
    eventFrames++;
    break;
  case 1:
    bsaControlFrames++;
    break;
  case 2:
    bsaChannelFrames++;
    break;
  default:
    break;
  }
}

static const char* to_name(const char* base,
                           unsigned    chan,
                           unsigned    array,
                           const char* field)
{
  std::ostringstream o;
  o << base;
  o << ":TPR:CH" << chan 
    << ":BSA" << array
    << ":"    << field;
  return o.str().c_str();
}

//
//  Generate triggers
//  Generate BSA
//
namespace Pds {
  class BsaUpdate {
  public:
    BsaUpdate(const char*   name,
              unsigned      chan,
              unsigned      array) :
      _stime     (0),
      _index     (0),
      // Base
      _sec       (to_name(name,chan,array,"SEC")),
      _nsec      (to_name(name,chan,array,"NSEC")),
      _pidl      (to_name(name,chan,array,"PIDL")),
      _pidh      (to_name(name,chan,array,"PIDH")),
      // Application
      _utime     (to_name(name,chan,array,"UTIME"))
    {
    }
  public:
    //
    //  Initialize the circular buffers
    //
    void initialize(uint64_t stime) {
      _stime = stime;
      _index = 0;
    }
    //
    //  Accumulate in a circular buffer
    //
    void accumulate(uint64_t  pid, 
                    uint64_t  tv ) {
      unsigned i = _index%BsaRecLen;
      _mpidl [i] = _mpidl [i+BsaRecLen] = pid&0xffffffff;
      _mpidh [i] = _mpidh [i+BsaRecLen] = pid>>32;
      _mtime [i] = _mtime [i+BsaRecLen] = double(tv);
      _index++;
    }
    //
    //  Write the records from the circular buffer
    //
    void update() {
      unsigned i = _index>BsaRecLen ? _index%BsaRecLen : 0;
      unsigned n = _index>BsaRecLen ? BsaRecLen : _index;

      memcpy(_pidl .data(), &_mpidl [i], n*sizeof(uint32_t));
      memcpy(_pidh .data(), &_mpidh [i], n*sizeof(uint32_t));
      memcpy(_utime.data(), &_mtime [i], n*sizeof(double));
        
      *reinterpret_cast<unsigned*>(_sec .data()) = _stime>>32;
      *reinterpret_cast<unsigned*>(_nsec.data()) = _stime&0xffffffff;
    }
  private:
    uint64_t      _stime;
    unsigned      _index;
    uint32_t      _mpidl [BsaRecLen*2];
    uint32_t      _mpidh [BsaRecLen*2];
    double        _mtime [BsaRecLen*2];
    PVWriter      _sec;
    PVWriter      _nsec;
    PVWriter      _pidl;
    PVWriter      _pidh;
    PVWriter      _utime;
  };
};


static void* read_thread(void*);

void usage(const char* p) {
  printf("Usage: %s -r <a/b> -i <channel> [-v]\n",p);
}

int main(int argc, char** argv) {

  extern char* optarg;
  char tprid='a';
  int idx=0;
  const char* prefix = 0;
  bool lverbose=false;

  //  char* endptr;
  int c;
  bool lUsage = false;
  while ( (c=getopt( argc, argv, "r:i:P:vh?")) != EOF ) {
    switch(c) {
    case 'r':
      tprid  = optarg[0];
      if (strlen(optarg) != 1) {
        printf("%s: option `-r' parsing error\n", argv[0]);
        lUsage = true;
      }
      break;
    case 'i':
      idx = atoi(optarg);
      break;
    case 'P':
      prefix = optarg;
      break;
    case 'v':
      lverbose = true;
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

  {
    char dev[16];
    sprintf(dev,"/dev/tpr%c%c",tprid,idx<0 ? 'b': (idx<10 ? '0'+idx : 'a'+(idx-10)));
    printf("Using tpr %s\n",dev);

    int fd = open(dev, O_RDWR);
    if (fd<0) {
      perror("Could not open");
      return -1;
    }

    void* ptr = mmap(0, sizeof(TprQueues), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
      perror("Failed to map");
      return -2;
    }

    if (!lverbose) {
      pthread_attr_t tattr;
      pthread_attr_init(&tattr);
      pthread_t tid;
      if (pthread_create(&tid, &tattr, &read_thread, 0))
        perror("Error creating read thread");
    }

    TprQueues& q = *(TprQueues*)ptr;

    char* buff = new char[32];

    std::vector<Pds::BsaUpdate*> blist(64);
    for(unsigned i=0; i<64; i++)
      blist[i] = new Pds::BsaUpdate(prefix,idx,i);

    if (1) {
      int64_t allrp = q.allwp[idx];
      int64_t chnrp = q.chnwp[idx];
      while(1) {
        read(fd, buff, 32);
        
        //  Simulated data
        timespec ts;
        clock_gettime(CLOCK_REALTIME,&ts);
        uint64_t tv = ts.tv_sec;
        tv <<= 32;
        tv |= ts.tv_nsec;

        if (1) {
          while(chnrp < q.chnwp[idx]) {
            const uint32_t* p = reinterpret_cast<const uint32_t*>
              (&q.chnq[idx].entry[chnrp &(MAX_TPR_CHNQ-1)].word[0]);
            countFrame(p);
            if ((p[2]&0xffff)==2) {
              // BSA accumulate
              unsigned ch  = (p[2]>>16)&0x3f;
              uint64_t pid = *reinterpret_cast<const uint64_t*>(&p[3]);
              blist[ch]->accumulate(pid,tv);
            }
            chnrp++;
          }
          while(allrp < q.allwp[idx]) {
            const uint32_t* p = reinterpret_cast<const uint32_t*>
              (&q.allq[q.allrp[idx].idx[allrp &(MAX_TPR_ALLQ-1)] &(MAX_TPR_ALLQ-1) ].word[0]);
            countFrame(p);
            // BSA control frame
            if ((p[2]&0xffff)==1) {
              // BSA done
              uint64_t done = *reinterpret_cast<const uint64_t*>(&p[7]);
              for(unsigned k=0; done; k++) {
                if (done&(1<<k))
                  blist[k]->update();
                done &= ~(1<<k);
              }
              // BSA init
              uint64_t init   = *reinterpret_cast<const uint64_t*>(&p[5]);
              uint64_t tstamp = *reinterpret_cast<const uint64_t*>(&p[3]);
              for(unsigned k=0; init; k++) {
                if (init&(1<<k))
                  blist[k]->initialize(tstamp);
                init &= ~(1<<k);
              }
            }
            allrp++;
          }
        }
      }
    }
  }

  return 0;
}

void* read_thread(void* arg)
{
  uint64_t evFrames=eventFrames;
  uint64_t ctlFrames=bsaControlFrames;
  uint64_t chnFrames=bsaChannelFrames;

  while(1) {
    sleep(1);
    printf("EvFrames %9lu : BsaCntl %9lu : BsaChan %9lu\n",
           eventFrames-evFrames,
           bsaControlFrames-ctlFrames,
           bsaChannelFrames-chnFrames);
    evFrames=eventFrames;
    ctlFrames=bsaControlFrames;
    chnFrames=bsaChannelFrames;
  }

  return 0;
}

