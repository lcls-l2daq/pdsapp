#include <string>
#include <sstream>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>

#include "pds/cphw/Reg.hh"

#include "pds/xpm/Module.hh"
#include "pds/xpm/PVStats.hh"
#include "pds/xpm/PVCtrls.hh"
#include "pds/xpm/PVPStats.hh"
#include "pds/xpm/PVPCtrls.hh"

#include "pds/epicstools/EpicsCA.hh"
#include "pds/epicstools/PVWriter.hh"
#include "pds/epicstools/PVMonitorCb.hh"

#include "pds/service/Routine.hh"
#include "pds/service/Semaphore.hh"
#include "pds/service/Task.hh"
#include "pds/service/Timer.hh"

using Pds_Epics::EpicsCA;
using Pds_Epics::PVWriter;
using Pds_Epics::PVMonitorCb;
using Pds::Xpm::CoreCounts;
using Pds::Xpm::L0Stats;
using std::string;

extern int optind;

namespace Pds {
  namespace Xpm {

    //class StatsTimer;

    //static StatsTimer* theTimer = NULL;

    static void sigHandler( int signal )
    {
      //if (theTimer)  theTimer->cancel();

      ::exit(signal);
    }

    class StatsTimer : public Timer, public PVMonitorCb {
    public:
      StatsTimer(Module& dev);
      ~StatsTimer() { _task->destroy(); }
    public:
      void allocate(const char* module_prefix,
                    const char* partition_prefix);
      void start   ();
      void cancel  ();
      void expired ();
      Task* task() { return _task; }
      //      unsigned duration  () const { return 1000; }
      unsigned duration  () const { return 1010; }  // 1% error on timer
      unsigned repetitive() const { return 1; }
    public:  // PVMonitorCb interface
      void     updated   ();
    private:
      void     _allocate();
    private:
      Module&    _dev;
      Semaphore  _sem;
      Task*      _task;
      string     _module_prefix;
      string     _partition_prefix;
      EpicsCA*   _partPV;
      PVWriter*  _paddrPV;
      unsigned   _partitions;
      timespec   _t;
      CoreCounts _c;
      L0Stats    _s   [Pds::Xpm::Module::NPartitions];
      PVPStats   _pvps[Pds::Xpm::Module::NPartitions];
      PVStats    _pvs;
      PVCtrls    _pvc;
      PVPCtrls*  _pvpc[Pds::Xpm::Module::NPartitions];
      friend class PvAllocate;
    };

    class PvAllocate : public Routine {
    public:
      PvAllocate(StatsTimer& parent) : _parent(parent) {}
    public:
      void routine() { _parent._allocate(); delete this; }
    private:
      StatsTimer& _parent;
    };

  };
};

using namespace Pds;
using namespace Pds::Xpm;

StatsTimer::StatsTimer(Module& dev) :
  _dev       (dev),
  _sem       (Semaphore::FULL),
  _task      (new Task(TaskObject("PtnS"))),
  _partitions(0),
  _pvc       (dev,_sem)
{
}

void StatsTimer::allocate(const char* module_prefix,
                          const char* partition_prefix)
{
  _module_prefix    = module_prefix;
  _partition_prefix = partition_prefix;

  _task->call(new PvAllocate(*this));
}

void StatsTimer::_allocate()
{
  _pvs.allocate(_module_prefix);
  _pvc.allocate(_module_prefix);

  for(unsigned i=0; i<Pds::Xpm::Module::NPartitions; i++) {
    std::stringstream ostr;
    ostr << _partition_prefix << ":" << i;
    _pvps[i].allocate(ostr.str());
    _pvpc[i] = new PVPCtrls(_dev,_sem,i);
    _pvpc[i]->allocate(ostr.str());
  }

  { std::stringstream ostr;
    ostr << _module_prefix << ":PARTITIONS";
    _partPV  = new EpicsCA(ostr.str().c_str(),this); }

  { std::stringstream ostr;
    ostr << _module_prefix << ":PAddr";
    _paddrPV = new PVWriter(ostr.str().c_str()); }
}

void StatsTimer::updated()  // PARTITIONS updated
{
  unsigned partitions = *reinterpret_cast<const unsigned*>(_partPV->data());

  printf("StatsTimer::updated  _partitions %08x -> %08x\n",
         _partitions, partitions);

  //  _sem.take();

  //
  //  Disable partition control 
  //
  unsigned disabled = _partitions & ~partitions;
  for(unsigned i=0; i<Pds::Xpm::Module::NPartitions; i++) {
    if ((1<<i)&disabled) {
      _pvpc[i]->enable(false);
    }
  }

  //
  //  Update new partition controls
  //
  unsigned enabled = partitions & ~_partitions;
  for(unsigned i=0; i<Pds::Xpm::Module::NPartitions; i++) {
    if ((1<<i)&enabled) {
      _dev.setPartition(i);
      _s[i] = _dev.l0Stats();
      _pvpc[i]->enable(true);
    }
  }

  //  _sem.give();

  _partitions = partitions;
}

void StatsTimer::start()
{
  //theTimer = this;

  _dev._timing.resetStats();

  //  _pvs.begin(_dev.l0Stats());
  Timer::start();
}

void StatsTimer::cancel()
{
  Timer::cancel();
  expired();
}

//
//  Update EPICS PVs
//
void StatsTimer::expired()
{
  timespec t; clock_gettime(CLOCK_REALTIME,&t);
  CoreCounts c = _dev.counts();
  double dt = double(t.tv_sec-_t.tv_sec)+1.e-9*(double(t.tv_nsec)-double(_t.tv_nsec));
  _sem.take();
  _pvs.update(c,_c,dt);
  _sem.give();
  for(unsigned i=0; i<Pds::Xpm::Module::NPartitions; i++) {
    if ((1<<i)&_partitions) {
      _sem.take();
      _dev.setPartition(i);
      L0Stats    s = _dev.l0Stats();
      _pvps[i].update(s,_s[i],dt);
      _sem.give();
      _s[i]=s;
      printf("-- partition %u --\n",i);
      s.dump();
    }
  }

  _c=c;
  _t=t;

  *reinterpret_cast<unsigned*>(_paddrPV->data()) = _dev._paddr; 
  _paddrPV->put();

  ca_flush_io();
}


void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <IP addr> (default: 10.0.2.102)\n"
         "         -p <port>    (default: 8193)\n"
         "         -P <prefix>  (default: DAQ:LAB2\n");
}

int main(int argc, char** argv)
{
  extern char* optarg;

  int c;
  bool lUsage = false;

  const char* ip = "10.0.2.102";
  unsigned short port = 8193;
  const char* prefix = "DAQ:LAB2";
  char module_prefix[64];
  char partition_prefix[64];

  while ( (c=getopt( argc, argv, "a:p:P:h")) != EOF ) {
    switch(c) {
    case 'a':
      ip = optarg;
      break;
    case 'p':
      port = strtoul(optarg,NULL,0);
      break;
    case 'P':
      prefix = optarg;
      break;
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

  //  Crate number is "c" in IP a.b.c.d
  sprintf(module_prefix   ,"%s:XPM:%u" ,
          prefix,
          strtoul(strchr(strchr(ip,'.')+1,'.')+1,NULL,10));
  sprintf(partition_prefix,"%s:PART",prefix);

  Pds::Cphw::Reg::set(ip, port, 0);

  Module* m = Module::locate();
  m->init();

  StatsTimer* timer = new StatsTimer(*m);

  Task* task = new Task(Task::MakeThisATask);

  ::signal( SIGINT, sigHandler );

  timer->allocate(module_prefix,
                  partition_prefix);
  timer->start();

  task->mainLoop();

  sleep(1);                    // Seems to help prevent a crash in cpsw on exit

  return 0;
}
