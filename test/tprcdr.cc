#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>

#include <string>
#include <vector>
#include <sstream>

static const double CLK_FREQ = 1300e6/7.;
static bool _dump=false;


namespace Pds {
  namespace Tpr {
    class RxDesc {
    public:
      RxDesc(uint32_t* d, unsigned sz) : maxSize(sz), data(d) {}
    public:
      uint32_t  maxSize;
      uint32_t* data; 
    };
  };
};

namespace Pds {
  namespace Tpr {

    class AxiVersion {
    public:
      std::string buildStamp() const;
    public:
      volatile uint32_t FpgaVersion; 
      volatile uint32_t ScratchPad; 
      volatile uint32_t DeviceDnaHigh; 
      volatile uint32_t DeviceDnaLow; 
      volatile uint32_t FdSerialHigh; 
      volatile uint32_t FdSerialLow; 
      volatile uint32_t MasterReset; 
      volatile uint32_t FpgaReload; 
      volatile uint32_t FpgaReloadAddress; 
      volatile uint32_t Counter; 
      volatile uint32_t FpgaReloadHalt; 
      volatile uint32_t reserved_11[0x100-11];
      volatile uint32_t UserConstants[64];
      volatile uint32_t reserved_0x140[0x200-0x140];
      volatile uint32_t BuildStamp[64];
      volatile uint32_t reserved_0x240[0x4000-0x240];
    };

    class XBar {
    public:
      enum InMode  { StraightIn , LoopIn };
      enum OutMode { StraightOut, LoopOut };
      void setEvr( InMode  m );
      void setEvr( OutMode m );
      void setTpr( InMode  m );
      void setTpr( OutMode m );
      void dump() const;
    public:
      volatile uint32_t outMap[4];
    };

    class TprBase {
    public:
      enum { NCHANNELS=12 };
      enum { NTRIGGERS=12 };
      enum Destination { Any };
      enum FixedRate { _1M, _500K, _100K, _10K, _1K, _100H, _10H, _1H };
    public:
      void dump() const;
      void setupDma    (unsigned fullThr=0x3f2);
      void setupDaq    (unsigned i,
                        unsigned partition);
      void setupChannel(unsigned i,
                        Destination d,
                        FixedRate   r,
                        unsigned    bsaPresample,
                        unsigned    bsaDelay,
                        unsigned    bsaWidth);
      void setupTrigger(unsigned i,
                        unsigned source,
                        unsigned polarity,
                        unsigned delay,
                        unsigned width,
                        unsigned delayTap=0);
    public:
      volatile uint32_t irqEnable;
      volatile uint32_t irqStatus;
      volatile uint32_t partitionAddr;
      volatile uint32_t gtxDebug;
      volatile uint32_t countReset;
      volatile uint32_t trigMaster;
      volatile uint32_t dmaFullThr;
      volatile uint32_t reserved_1C;
      struct {  // 0x20
        volatile uint32_t control;
        volatile uint32_t evtSel;
        volatile uint32_t evtCount;
        volatile uint32_t bsaDelay;
        volatile uint32_t bsaWidth;
        volatile uint32_t bsaCount; // not implemented
        volatile uint32_t bsaData;  // not implemented
        volatile uint32_t reserved[1];
      } channel[NCHANNELS];
      volatile uint32_t reserved_20[2];
      volatile uint32_t frameCount;
      volatile uint32_t reserved_2C[2];
      volatile uint32_t bsaCntlCount; // not implemented
      volatile uint32_t bsaCntlData;  // not implemented
      volatile uint32_t reserved_b[1+(14-NCHANNELS)*8];
      struct { // 0x200
        volatile uint32_t control; // input, polarity, enabled
        volatile uint32_t delay;
        volatile uint32_t width;
        volatile uint32_t delayTap;
      } trigger[NTRIGGERS];
    };

    class DmaControl {
    public:
      void dump() const;
      void test();
      void setEmptyThr(unsigned);
    public:
      volatile uint32_t rxFree;
      volatile uint32_t reserved_4[15];
      volatile uint32_t rxFreeStat;
      volatile uint32_t reserved_14[47];
      volatile uint32_t rxMaxFrame;
      volatile uint32_t rxFifoSize;
      volatile uint32_t rxCount;
      volatile uint32_t lastDesc;
    };

    class TprCore {
    public:
      bool clkSel     () const;
      void clkSel     (bool lcls2);
      bool rxPolarity () const;
      void rxPolarity (bool p);
      void resetRx    ();
      void resetRxPll ();
      void resetCounts();
      void dump() const;
    public:
      volatile uint32_t SOFcounts;
      volatile uint32_t EOFcounts;
      volatile uint32_t Msgcounts;
      volatile uint32_t CRCerrors;
      volatile uint32_t RxRecClks;
      volatile uint32_t RxRstDone;
      volatile uint32_t RxDecErrs;
      volatile uint32_t RxDspErrs;
      volatile uint32_t CSR;
      uint32_t          reserved;
      volatile uint32_t TxRefClks;
      volatile uint32_t BypassCnts;
    };

    class RingB {
    public:
      void enable(bool l);
      void clear ();
      void dump() const;
      void dumpFrames() const;
    public:
      volatile uint32_t data[0x1fff];
      volatile uint32_t csr;
    };

    class TpgMini {
    public:
      void setBsa(unsigned rate,
                  unsigned ntoavg, unsigned navg);
      void dump() const;
    public:
      volatile uint32_t ClkSel;
      volatile uint32_t BaseCntl;
      volatile uint32_t PulseIdU;
      volatile uint32_t PulseIdL;
      volatile uint32_t TStampU;
      volatile uint32_t TStampL;
      volatile uint32_t FixedRate[10];
      volatile uint32_t RateReload;
      volatile uint32_t HistoryCntl;
      volatile uint32_t FwVersion;
      volatile uint32_t Resources;
      volatile uint32_t BsaCompleteU;
      volatile uint32_t BsaCompleteL;
      volatile uint32_t reserved_22[128-22];
      struct {
        volatile uint32_t l;
        volatile uint32_t h;
      } BsaDef[64];
      volatile uint32_t reserved_256[320-256];
      volatile uint32_t CntPLL;
      volatile uint32_t Cnt186M;
      volatile uint32_t reserved_322;
      volatile uint32_t CntIntvl;
      volatile uint32_t CntBRT;
    };

    // Memory map of TPR registers (EvrCardG2 BAR 1)
    class TprReg {
    public:
      uint32_t   reserved_0    [(0x10000)>>2];
      AxiVersion version;  // 0x00010000
      uint32_t   reserved_10000[(0x40000-0x20000)>>2];  // boot_mem is here
      XBar       xbar;     // 0x00040000
      uint32_t   reserved_30010[(0x80000-0x40010)>>2];
      TprBase    base;     // 0x00080000
      uint32_t   reserved_80400[(0x400-sizeof(TprBase))/4];
      DmaControl dma;      // 0x00080400
      uint32_t   reserved_1    [(0x40000-0x400-sizeof(DmaControl))/4];
      TprCore    tpr;      // 0x000C0000
      uint32_t   reserved_tpr  [(0x10000-sizeof(TprCore))/4];
      RingB      ring0;    // 0x000D0000
      uint32_t   reserved_ring0[(0x10000-sizeof(RingB))/4];
      RingB      ring1;    // 0x000E0000
      uint32_t   reserved_ring1[(0x10000-sizeof(RingB))/4];
      TpgMini    tpg;      // 0x000F0000
    };
  };
};

using namespace Pds::Tpr;

std::string AxiVersion::buildStamp() const {
  uint32_t tmp[64];
  for(unsigned i=0; i<64; i++)
    tmp[i] = BuildStamp[i];
  return std::string(reinterpret_cast<const char*>(tmp));
}

void XBar::setEvr( InMode  m ) { outMap[2] = m==StraightIn  ? 0:2; }
void XBar::setEvr( OutMode m ) { outMap[0] = m==StraightOut ? 2:0; }
void XBar::setTpr( InMode  m ) { outMap[3] = m==StraightIn  ? 1:3; }
void XBar::setTpr( OutMode m ) { outMap[1] = m==StraightOut ? 3:1; }
void XBar::dump() const { for(unsigned i=0; i<4; i++) printf("Out[%d]: %d\n",i,outMap[i]); }

void TprBase::dump() const {
  static const unsigned NChan=12;
  printf("irqEnable [%p]: %08x\n",&irqEnable,irqEnable);
  printf("irqStatus [%p]: %08x\n",&irqStatus,irqStatus);
  printf("gtxDebug  [%p]: %08x\n",&gtxDebug  ,gtxDebug);
  printf("trigSel   [%p]: %08x\n",&trigMaster,trigMaster);
  printf("channel0  [%p]\n",&channel[0].control);
  printf("control : ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",channel[i].control);
  printf("\nevtCount: ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",channel[i].evtCount);
  printf("\nbsaCount: ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",channel[i].bsaCount);
  printf("\nevtSel  : ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",channel[i].evtSel);
  printf("\nbsaDelay: ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",channel[i].bsaDelay);
  printf("\nbsaWidth: ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",channel[i].bsaWidth);
  printf("\nframeCnt: %08x\n",frameCount);
  printf("bsaCnCnt: %08x\n",bsaCntlCount);
  printf("trigger0  [%p]\n",&trigger[0].control);
  printf("trgCntrl: ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",trigger[i].control);
  printf("\ntrgDelay: ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",trigger[i].delay);
  printf("\ntrgWidth: ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",trigger[i].width);
  printf("\ntrgDelayTap: ");
  for(unsigned i=0; i<NChan; i++)      printf("%08x ",trigger[i].delayTap);
  printf("\n");
}

void TprBase::setupDma    (unsigned fullThr) {
  dmaFullThr = fullThr;
}

void TprBase::setupDaq    (unsigned i,
                           unsigned partition) {
  channel[i].evtSel   = (1<<30) | (3<<14) | partition; // 
  channel[i].control = 5;
}

void TprBase::setupChannel(unsigned i,
                           Destination d,
                           FixedRate   r,
                           unsigned    bsaPresample,
                           unsigned    bsaDelay,
                           unsigned    bsaWidth) {
  channel[i].control  = 0;
  channel[i].evtSel   = (1<<30) | unsigned(r); //
  channel[i].bsaDelay = (bsaPresample<<20) | bsaDelay;
  channel[i].bsaWidth = bsaWidth;
  channel[i].control  = bsaWidth ? 7 : 5;
}

void TprBase::setupTrigger(unsigned i,
                           unsigned source,
                           unsigned polarity,
                           unsigned delay,
                           unsigned width,
                           unsigned delayTap) {
  trigger[i].control  = (polarity ? (1<<16):0);
  trigger[i].delay    = delay;
  trigger[i].width    = width;
  trigger[i].control  = (source&0xffff) | (polarity ? (1<<16):0) | (1<<31);
  trigger[i].delayTap = delayTap;
}

void DmaControl::dump() const {
  printf("DMA Control\n");
  printf("\trxFreeStat : %8x\n",rxFreeStat);
  printf("\trxMaxFrame : %8x\n",rxMaxFrame);
  printf("\trxFifoSize : %8x\n",rxFifoSize&0x3ff);
  printf("\trxEmptyThr : %8x\n",(rxFifoSize>>16)&0x3ff);
  printf("\trxCount    : %8x\n",rxCount);
  printf("\tlastDesc   : %8x\n",lastDesc);
}

void DmaControl::test() {
  printf("DMA Control test\n");
  volatile unsigned v1 = rxMaxFrame;
  rxMaxFrame = 0x80001000;
  volatile unsigned v2 = rxMaxFrame;
  printf("\trxMaxFrame : %8x [%8x] %8x\n",v1,0x80001000,v2);

  v1     = rxFreeStat;
  rxFree = 0xdeadbeef;
  v2     = rxFreeStat;
  printf("\trxFreeStat [%8x], rxFree [%8x], lastDesc[%8x], rxFreeStat[%8x]\n",
         v1, 0xdeadbeef, lastDesc, v2);
}

void DmaControl::setEmptyThr(unsigned v)
{
  volatile unsigned v1 = rxFifoSize;
  rxFifoSize = ((v&0x3ff)<<16) | (v1&0x3ff);
}

bool TprCore::clkSel    () const {
  uint32_t v = CSR;
  return v&(1<<4);
}

void TprCore::clkSel    (bool lcls2) {
  volatile uint32_t v = CSR;
  v = lcls2 ? (v|(1<<4)) : (v&~(1<<4));
  CSR = v;
}

bool TprCore::rxPolarity() const {
  uint32_t v = CSR;
  return v&(1<<2);
}

void TprCore::rxPolarity(bool p) {
  volatile uint32_t v = CSR;
  v = p ? (v|(1<<2)) : (v&~(1<<2));
  CSR = v;
  usleep(10);
  CSR = v|(1<<3);
  usleep(10);
  CSR = v&~(1<<3);
}

void TprCore::resetRx() {
  volatile uint32_t v = CSR;
  CSR = (v|(1<<3));
  usleep(10);
  CSR = (v&~(1<<3));
}

void TprCore::resetRxPll() {
  volatile uint32_t v = CSR;
  CSR = (v|(1<<7));
  usleep(10);
  CSR = (v&~(1<<7));
}

void TprCore::resetCounts() {
  volatile uint32_t v = CSR;
  CSR = (v|1);
  usleep(10);
  CSR = (v&~1);
}

void TprCore::dump() const {
  printf("SOFcounts: %08x\n", SOFcounts);
  printf("EOFcounts: %08x\n", EOFcounts);
  printf("Msgcounts: %08x\n", Msgcounts);
  printf("CRCerrors: %08x\n", CRCerrors);
  printf("RxRecClks: %08x\n", RxRecClks);
  printf("RxRstDone: %08x\n", RxRstDone);
  printf("RxDecErrs: %08x\n", RxDecErrs);
  printf("RxDspErrs: %08x\n", RxDspErrs);
  printf("CSR      : %08x\n", CSR); 
  printf("TxRefClks: %08x\n", TxRefClks);
  printf("BypDone  : %04x\n", (BypassCnts>> 0)&0xffff);
  printf("BypResets: %04x\n", (BypassCnts>>16)&0xffff);
}


void RingB::enable(bool l) {
  volatile uint32_t v = csr;
  csr = l ? (v|1) : (v&~1);
}
void RingB::clear() {
  volatile uint32_t v = csr;
  csr = v|2;
  usleep(10);
  csr = v&~2;
}
void RingB::dump() const
{
  for(unsigned i=0; i<0x1fff; i++)
    printf("%05x%c",data[i],(i&0xf)==0xf ? '\n':' ');
}
void RingB::dumpFrames() const
{
#define print_u32 {                             \
    volatile uint32_t v  = (data[j++]<<16);     \
    v = (v>>16) | (data[j++]<<16);              \
    printf("%8x ",v);                           \
  }
#define print_u32be {                           \
    volatile uint32_t v  = (data[j++]&0xffff);  \
    v = (v<<16) | (data[j++]&0xffff);           \
    printf("%8x ",v);                           \
  }
#define print_u64 {                             \
    uint64_t v  = (uint64_t(data[j++])<<48);    \
    v = (v>>16) | (uint64_t(data[j++])<<48);    \
    v = (v>>16) | (uint64_t(data[j++])<<48);    \
    v = (v>>16) | (uint64_t(data[j++])<<48);    \
    printf("%16lx ",v);                         \
  }
  printf("%16.16s %16.16s %16.16s %8.8s %8.8s %8.8s %8.8s %8.8s %8.8s\n",
         "Version","PulseID","TimeStamp","Markers","BeamReq",
         "BsaInit","BsaActiv","BsaAvgD","BsaDone");
  unsigned i=0;
  while(i<0x1fff) {
    if (data[i]==0x1b5f7) {  // Start of frame
      if (i+80 >= 0x1fff)
        break;
      unsigned j=i+1;
      print_u64; // version
      print_u64; // pulse ID
      print_u64; // time stamp
      print_u32; // rates/timeslot
      print_u32; // beamreq
      j += 10;
      print_u32; // bsainit
      j += 2;
      print_u32; // bsaactive
      j += 2;
      print_u32; // bsaavgdone
      j += 2;
      print_u32; // bsadone
      j += 2;
      printf("\n");
      i += 80;
    }
    else
      i++;
  }
#undef print_u32
#undef print_u32be
#undef print_u64
}


void TpgMini::setBsa(unsigned rate,
                     unsigned ntoavg,
                     unsigned navg)
{
  BsaDef[0].l = (1<<31) | (rate&0xffff);
  BsaDef[0].h = (navg<<16) | (ntoavg&0xffff);
}

void TpgMini::dump() const
{
  printf("ClkSel:\t%08x\n",ClkSel);
  printf("BaseCntl:\t%08x\n",BaseCntl);
  printf("PulseIdU:\t%08x\n",PulseIdU);
  printf("PulseIdL:\t%08x\n",PulseIdL);
  printf("TStampU:\t%08x\n",TStampU);
  printf("TStampL:\t%08x\n",TStampL);
  for(unsigned i=0; i<10; i++)
    printf("FixedRate[%d]:\t%08x\n",i,FixedRate[i]);
  printf("HistoryCntl:\t%08x\n",HistoryCntl);
  printf("FwVersion:\t%08x\n",FwVersion);
  printf("Resources:\t%08x\n",Resources);
  printf("BsaCompleteU:\t%08x\n",BsaCompleteU);
  printf("BsaCompleteL:\t%08x\n",BsaCompleteL);
  printf("BsaDef[0]:\t%08x/%08x\n",BsaDef[0].l,BsaDef[0].h);
  printf("CntPLL:\t%08x\n",CntPLL);
  printf("Cnt186M:\t%08x\n",Cnt186M);
  printf("CntIntvl:\t%08x\n",CntIntvl);
  printf("CntBRT:\t%08x\n",CntBRT);
}

extern int optind;

void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -r <a..z>\n");
  printf("         -p<channel,delay,width,polarity,delayTap]>\n");
  printf("         -c<channel,rate[,bsaPresample,bsaDelay,bsaWidth]>\n");
  printf("\t<rate>: {0=1MHz, 1=0.5MHz, 2=100kHz, 3=10kHz, 4=1kHz, 5=100Hz, 6=10Hz, 7=1Hz}\n");
}

namespace Pds {
  namespace Tpr {
    class Channel {
    public:
      Channel() : bsaWid(0) {}
      unsigned             channel;
      TprBase::Destination destn;
      TprBase::FixedRate   rate;
      unsigned             bsaPre;
      unsigned             bsaDel;
      unsigned             bsaWid;
    };
  };
};

static const char* to_name(const std::string& base,
                           const char*        ext)
{
  std::string o(base);
  o += ":";
  o += std::string(ext);
  printf("to_name [%s]\n",o.c_str());
  return o.c_str();
}

static std::string to_name(const char* base,
                           unsigned    arg)
{
  std::ostringstream o;
  o << base << ":CH" << arg;
  printf("to_name [%s]\n",o.str().c_str());
  return o.str();
}


namespace Pds {
  class TprControl {
  public:
    TprControl(TprReg* p, const char* name, bool clksel=false) :
      _dev       ( p ),
      _CSR       ( p->tpr.CSR ),
      _frames    ( p->tpr.Msgcounts ),
      _rxClks    ( p->tpr.RxRecClks ),
      _txClks    ( p->tpr.TxRefClks )
    {
      printf("version @%p\n",&p->version);
      printf("xbar    @%p\n",&p->xbar);
      printf("base    @%p\n",&p->base);
      printf("tpr     @%p\n",&p->tpr);
      printf("tpg     @%p\n",&p->tpg);

      printf("FpgaVersion: %08x\n", p->version.FpgaVersion);
      printf("BuildStamp: %s\n", p->version.buildStamp().c_str());

      p->xbar.dump();
      p->tpr.dump();
      p->base.dump();

      p->tpr .clkSel(clksel);

      timespec tvo;
      clock_gettime(CLOCK_REALTIME,&tvo);
      while(1) {
        usleep(1000000);
        timespec tv;
        clock_gettime(CLOCK_REALTIME,&tv);
        double dt = double(tv.tv_sec-tvo.tv_sec)+
          1.e-9*(double(tv.tv_nsec)-double(tvo.tv_nsec));
        tvo = tv;
        report(dt);

        if (_dump) {
          _dev->ring1.enable(false);
          _dev->ring1.dump();
          _dev->ring1.clear();
          _dev->ring1.enable(true);
        }
      }
    }
    ~TprControl() {}
  public:
    void report(double dt) {
      { unsigned v = _dev->tpr.CSR;
        unsigned v_delta = v ^ _CSR;
        if (v_delta & (1<<1))
          printf("linkState %u\n", (v&(1<<1)?1:0) );
        if (v_delta & (1<<5))
          printf("linkLatch %u\n", (v&(1<<5)?1:0) );
        _CSR = v;
      }

      { unsigned v = _dev->tpr.RxDspErrs + _dev->tpr.RxDecErrs;
        printf( "rxErrs %u\n", v ); }

      { unsigned v = _dev->tpr.RxRecClks;
        double value = 16.e-6*double(v-_rxClks)/dt;
        printf("RxClkRate %g\n",value);
        _rxClks = v; }
      { unsigned v = _dev->tpr.TxRefClks;
        double value = 16.e-6*double(v-_txClks)/dt;
        printf("TxClkRate %g\n",value);
        _txClks = v; }
    }
  private:
    TprReg*  _dev;
    unsigned _CSR;
    unsigned _frames;
    unsigned _rxClks;
    unsigned _txClks;
  };
};
    


int main(int argc, char** argv) {

  extern char* optarg;
  char tprid='a';
  const char* name = "TPR";
  bool lClkSel = false;

  //  char* endptr;
  int c;
  bool lUsage = false;
  while ( (c=getopt( argc, argv, "r:n:dCh?")) != EOF ) {
    switch(c) {
    case 'r':
      tprid  = optarg[0];
      if (strlen(optarg) != 1) {
        printf("%s: option `-r' parsing error\n", argv[0]);
        lUsage = true;
      }
      break;
    case 'C':
      lClkSel=true;
      break;
    case 'd':
      _dump = true;
      break;
    case 'n':
      name = optarg;
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
    sprintf(dev,"/dev/tpr%c",tprid);
    printf("Using tpr %s\n",dev);

    int fd = open(dev, O_RDWR);
    if (fd<0) {
      perror("Could not open");
      return -1;
    }

    void* ptr = mmap(0, sizeof(TprReg), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
      perror("Failed to map");
      return -2;
    }

    reinterpret_cast<TprReg*>(ptr)->xbar.outMap[2]=0;
    reinterpret_cast<TprReg*>(ptr)->xbar.outMap[3]=1;

    reinterpret_cast<TprReg*>(ptr)->ring0.dump();
    reinterpret_cast<TprReg*>(ptr)->ring1.dump();

    *new Pds::TprControl(reinterpret_cast<TprReg*>(ptr),
                         name,
                         lClkSel);

  }

  return 0;
}

