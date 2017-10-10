#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#include <signal.h>

#include "pds/epicstools/EpicsCA.hh"
#include "pds/epicstools/PVWriter.hh"

#include "pds/cphw/Reg.hh"
#include "pds/cphw/AxiVersion.hh"

#include "tdm.hh"

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>

#define PVPUTD(pv,val) {                                                 \
        *reinterpret_cast<double*>(pv->data()) = val; pv->put(); }
#define PVPUTI(pv,val) {                                                 \
        *reinterpret_cast<int*>(pv->data()) = val; pv->put(); }

using Pds_Epics::EpicsCA;
using Pds_Epics::PVWriter;

namespace TDM {
  class Point {
  public:
    Point() : txd(0), pd(0), clk(0) {}
    Point(unsigned _txd, int _pd, unsigned _clk) :
      txd(_txd), pd(_pd), clk(_clk) {}
  public:
    std::ostream& write(std::ostream& s) const {
      s << txd << ' '
        << pd << ' '
        << clk;
      return s;
    }
    std::istream& read(std::istream& s) {
      s >> txd >> pd >> clk;
      return s;
    }
    Point operator+(const Point& o) const {
      return Point(txd+o.txd,
                   pd +o.pd,
                   clk+o.clk);
    }
    Point operator-(const Point& o) const {
      return Point(txd-o.txd,
                   pd -o.pd,
                   clk-o.clk);
    }
    Point operator*(double f) const {
      return Point(unsigned(f*double(txd)),
                   int     (f*double(pd)),
                   unsigned(f*double(clk)));
    }
  public:
    unsigned txd;
    int      pd;
    unsigned clk;
  };

  std::ostream& operator<<(std::ostream& s, const Point& p) {
    return p.write(s);
  }
  std::istream& operator>>(std::istream& s, Point& p) {
    return p.read(s);
  }

  class LoopInit {
  public:
    LoopInit() : _verbose(false) {}
    LoopInit(Module& module, const char* base, unsigned ch, bool verbose) : 
      _link   (&module._link[ch]), 
      _pvstage(new PVWriter(to_name(base,ch,"STAGE"))),
      _pvtxd  (new PVWriter(to_name(base,ch,"TXD"))),
      _pvph   (new PVWriter(to_name(base,ch,"PD"))),
      _pvtxdps(new PVWriter(to_name(base,ch,"TXDPS"))),
      _pvphps (new PVWriter(to_name(base,ch,"PDPS"))),
      _pvclk  (new PVWriter(to_name(base,ch,"CLK"))),
      _pvdclk (new PVWriter(to_name(base,ch,"DCLK"))),
      _stage  (S_INIT),
      _verbose(verbose) {}
  public:
    static const char* to_name(const char* base, 
                               unsigned    ch,
                               const char* name) {
      std::ostringstream o;
      o << base << ":CH" << ch << ":SCAN:" << name;
      return o.str().c_str();
    }

    bool next() {

      Point pt(_link->txDelay(),
               _link->loopPhase(),
               _link->loopClks());

      //  skip invalid points
      if (_prev.clk)
        if ((pt.clk > _prev.clk+1) || (pt.clk+8 < _prev.clk))
          return true;

      PVPUTI(_pvstage, _stage);
      PVPUTI(_pvtxd  , pt.txd);
      PVPUTI(_pvph   , pt.pd);
      PVPUTI(_pvclk  , pt.clk);
      PVPUTI(_pvdclk , _link->pmDelay());
      PVPUTD(_pvtxdps, pt.txd*txd_to_ps);
      PVPUTD(_pvphps , pt.pd *pd_to_ps);

      unsigned delay = pt.txd;
      delay += 0x8000/50;

      switch(_stage) {
      case S_BEG1:
        //  Adjust pmDelay to maximize range
        _link->pmDelay( (MAX_PD-pt.pd)*5/MAX_PD + 1 );
        //  resample this delay
        delay -= 0x8000/50;
        _stage = S_BEG2;
        break;
      case S_BEG2:
        _beg = pt;
        _stage = S_END;
        break;
      case S_INIT:
        if (pt.clk < _prev.clk)
          _stage = S_BEG1;
        break;
      case S_END:
        if (pt.clk < _prev.clk) {
          _end = _prev;
          _stage = S_DONE;
        }
        break;
      default:
        break;
      }

      _prev = pt;

      if (_verbose) {
        std::cout << "next " << pt << std::endl;
        dump();
      }

      //
      //  Launch move to next point
      //
      _link->txDelay(delay);

      return _stage<S_DONE;
    }
    void finalize(Point& low, Point& high) const {
      low  = _beg;
      high = _end;
    }
    void dump() const {
      std::cout << "beg " << _beg << " "
                << "end " << _end << " "
                << "low " << _low << " "
                << "high " << _high << " "
                << "prev " << _prev << std::endl;
    }
  private:
    enum { S_INIT=0, S_BEG1, S_BEG2, S_END, S_DONE };
    DelayLink* _link;
    PVWriter* _pvstage;
    PVWriter* _pvtxd;
    PVWriter* _pvph;
    PVWriter* _pvtxdps;
    PVWriter* _pvphps;
    PVWriter* _pvclk;
    PVWriter* _pvdclk;
    Point _beg;
    Point _end;
    Point _low;
    Point _high;
    Point _prev;
    unsigned _stage;
    bool  _verbose;
  };

  class LoopChannel {
  public:
    LoopChannel() {}
    LoopChannel(const char* base) :
      _pvlock(new PVWriter(to_name(base, "LOCK"))),
      _pd    (new PVWriter(to_name(base, "PD"))),
      _dpd   (new PVWriter(to_name(base, "DPD"))),
      _dpdps (new PVWriter(to_name(base, "DPDPS"))),
      _dpdpsa(new PVWriter(to_name(base, "DPDPSA"))),
      _txd   (new PVWriter(to_name(base, "TXD"))),
      _dtxd  (new PVWriter(to_name(base, "DTXD"))),
      _dtxdps(new PVWriter(to_name(base, "DTXDPS"))),
      _enable(false),
      _verbose(false),
      _nsum  (0),
      _vsum  (0)
        {}
  public:
    static const char* to_name(const char* base, 
                               const char* name) {
      std::ostringstream o;
      o << base << ":" << name;
      return o.str().c_str();
    }
    void save (std::ostream& s) {
      s << _low  << " " 
        << _high << " "
        << _init << " "
        << _curr << std::endl;
    }
    void load (std::istream& s) {
      s >> _low >> _high >> _init >> _curr;
    }
    void init (DelayLink&      link, 
               const LoopInit& init) {
      init.finalize(_low,_high);
      _init = (_low+_high)*0.5;
      _curr = _init;  // and apply
      link.txDelay(_curr.txd,true);
    }
    void reset(DelayLink&      link,
               const LoopInit& init) {
      Point low, high;
      init.finalize(low,high);
      int off = 0.5*((low+high)-(_low+_high)).txd;
      _low  = low;
      _high = high;
      _init.txd += off;
      _curr.txd += off; // and apply
      link.txDelay(_curr.txd,true);
    }
    void process(DelayLink& link) {
      int        pd = link.loopPhase();
      int       dpd = pd-_init.pd;

      //  Step quarter-way
      unsigned txtgt = _init.txd - int(dpd*pd_to_txd);
      unsigned txd  = link.txDelay();
      unsigned ntxd = (txtgt + 3*txd)/4;
      int      dtxd = int(ntxd) - int(_init.txd);

      unsigned lock = link.allLock() ? 1:0;

      PVPUTI(_pvlock, lock);
      PVPUTI(_pd  ,pd);
      PVPUTI(_dpd ,dpd);
      PVPUTD(_dpdps ,dpd*pd_to_ps);
      PVPUTI(_txd ,txd);
      PVPUTI(_dtxd,dtxd);
      PVPUTD(_dtxdps,dtxd*txd_to_ps);

      _vsum += double(dpd);
      _nsum++;
      if (_nsum >= 100) {
        PVPUTD(_dpdpsa,_vsum*pd_to_ps/double(_nsum));
        _vsum = 0;
        _nsum = 0;
      }

      if (_enable)
        link.txDelay(ntxd);
      _curr.txd = txd;
      _curr.pd  = pd;
      if (_verbose)
        printf("ph %08x:%7.2f  dpd %8d:%7.2f  txd %08x:%7.2f  clks %u\n",
               pd,  double( pd)*pd_to_ps, 
               dpd, double(dpd)*pd_to_ps, 
               txd, double(txd)*txd_to_ps,
               link.loopClks());
    }
    void enable    (bool v) { _enable=v; }
    void setVerbose(bool v) { _verbose=v; }
  private:
    Point     _low;
    Point     _high;
    Point     _init;
    Point     _curr;
    PVWriter* _pvlock;
    PVWriter* _pd;
    PVWriter* _dpd;
    PVWriter* _dpdps;
    PVWriter* _dpdpsa;
    PVWriter* _txd;
    PVWriter* _dtxd;
    PVWriter* _dtxdps;
    bool      _enable;
    bool      _verbose;
    unsigned  _nsum;
    double    _vsum;
  };

  class LoopControl {
  public:
    LoopControl() : _id(0) {}
    LoopControl(const char* id,
                const char* base) : 
      _id(id) {
      char buff[64];
      sprintf(buff,"%s:SRC:LOCK",base);
      _srcLock = new PVWriter(buff);
      for(unsigned i=0; i<NLINKS; i++) {
        sprintf(buff,"%s:CH%u",base,i);
        _channel[i] = LoopChannel(buff);
      }
    }
  public:
    void load(const char* path) {
      char buff[128];
      sprintf(buff,"%s/tdm.%s",path,_id);
      printf("Loading settings from %s\n",buff);
      std::ifstream f(buff);
      if (!f.good()) {
        perror("Load settings failed");
        exit(1);
      }
      for(unsigned i=0; i<NLINKS; i++)
        _channel[i].load(f);
      f.close();
    }
    void save(const char* path) {
      char buff[128];
      sprintf(buff,"%s/tdm.%s",path,_id);
      printf("Saving settings to %s\n",buff);
      std::ofstream f(buff);
      for(unsigned i=0; i<NLINKS; i++)
        _channel[i].save(f);
      f.close();
    }
    void init (Module& m, const LoopInit* init) {
      for(unsigned i=0; i<NLINKS; i++)
        _channel[i].init(m._link[i],init[i]);
      usleep(1000000);
    }
    void reset(Module& m, const LoopInit* init) {
      for(unsigned i=0; i<NLINKS; i++)
        _channel[i].reset(m._link[i],init[i]);
      usleep(1000000);
    }
    void process(Module& m) {
      for(unsigned i=0; i<NLINKS; i++)
        _channel[i].process(m._link[i]);
      unsigned lock = m._source.allLock() ? 1:0;
      PVPUTI(_srcLock, lock);
    }
    void enable(unsigned v) {
      for(unsigned i=0; i<NLINKS; i++)
        if (v & (1<<i))
          _channel[i].enable(true);
    }
    void setVerbose(unsigned v) {
      for(unsigned i=0; i<NLINKS; i++)
        if (v & (1<<i))
          _channel[i].setVerbose(true);
    }
  public:
    const char* _id;
    LoopChannel _channel[NLINKS];
    PVWriter*   _srcLock;
  };
};


using namespace TDM;

static LoopControl ctrl;
static const char* path = ".";

void sigHandler( int signal ) {
  ctrl.save(path);
  ::exit(signal);
}

static void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <IP addr (dotted notation)>\n"
         "         -b <name>   (PV base name)\n"
         "         -i          (initialize module)\n"
         "         -r          (recover after reset)\n"
         "         -p <path>   (path for initialization parameter files)\n");
}

int main(int argc, char** argv) {

  extern char* optarg;

  int c;
  bool lUsage = false;
  const char* ip = "10.0.3.104";
  bool lInit=false, lReset=false;
  const char* path = ".";
  const char* base = "TDM";
  unsigned verbose = 0;
  unsigned enable = 0;

  while ( (c=getopt( argc, argv, "a:b:p:e:v:ir")) != EOF ) {
    switch(c) {
    case 'a':
      ip = optarg; break;
      break;
    case 'b':
      base = optarg;
      break;
    case 'p':
      path = optarg;
      break;
    case 'e':
      enable  = strtoul(optarg,NULL,0);
      break;
    case 'v':
      verbose = strtoul(optarg,NULL,0);
      break;
    case 'i':
      lInit = true;
      break;
    case 'r':
      lReset = true;
      break;
    default:
      lUsage = true;
      break;
    }
  }

  if (lUsage) {
    usage(argv[0]);
    exit(1);
  }

  //  EPICS thread initialization
  SEVCHK ( ca_context_create(ca_enable_preemptive_callback ), 
           "tdmca calling ca_context_create" );

  Pds::Cphw::Reg::set(ip, 8193, 0);

  Pds::Cphw::AxiVersion* vsn = new((void*)0) Pds::Cphw::AxiVersion;
  printf("buildStamp %s\n",vsn->buildStamp().c_str());

  Module*  m = new((void*)0x80000000) Module;

  LoopInit init[NLINKS];
  for(unsigned i=0; i<NLINKS; i++) {
    init[i] = LoopInit(*m, base, i, verbose&(1<<i));
  }

  ctrl = LoopControl(ip,base);

  if (lInit || lReset) {
    printf("Scanning...\n");
    m->txWait(0, true);

    unsigned mask = (1<<NLINKS)-1;
    unsigned j=0;  // limit to 100 steps for now
    while(mask && j++ < 100) {
      usleep(2000000);  // wait for phase measurement to complete
      for(unsigned i=0; i<NLINKS && mask; i++) {
        if (mask&(1<<i) && !init[i].next())
          mask &= ~(1<<i);
      }
      printf("[%u:%x]\n",j,mask);
      ca_flush_io();
    }
    for(unsigned i=0; i<NLINKS; i++)
      init[i].dump();
    printf("Scanning Done.\n");
  }

  ctrl.enable    (enable);
  ctrl.setVerbose(verbose);

  //
  //  Reinitialize loop parameters:
  //    Determine new TD, PM valid bounds and initial settings
  //
  if (lInit) {
      ctrl.init(*m,init);
  }
  //
  //  Locate new offset for loop parameters:
  //    Determine new TD bounds and setting base on known PM bounds and setting
  //
  else if (lReset) {
      ctrl.load(path);
      ctrl.reset(*m,init);
    }
  else {
      ctrl.load(path);
    }

  ctrl.save(path);

  ::signal( SIGINT , sigHandler );
  ::signal( SIGKILL, sigHandler );

  //
  //  Delay Compensation Loop
  //
  while(1) {
    usleep(1000000);
    ctrl.process(*m);
    ca_flush_io();
  }

  return 0;
}
