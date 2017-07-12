
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

#include "pds/dti/Module.hh"
#include "pds/cphw/AxiVersion.hh"
#include "pds/epicstools/PVWriter.hh"

#include <sstream>
#include <string>
#include <vector>

using Pds_Epics::PVWriter;

namespace Pds {
  namespace Dti {
    class PVStats {
    public:
      PVStats() : _pv(0) {}
      ~PVStats() {}
    public:
      void allocate(const std::string& title) {
        if (ca_current_context() == NULL) {
          printf("Initializing context\n");
          SEVCHK ( ca_context_create(ca_enable_preemptive_callback ), 
                   "Calling ca_context_create" );
        }

        for(unsigned i=0; i<_pv.size(); i++)
          delete _pv[i];
        _pv.resize(0);

        std::ostringstream o;
        o << "DTI:";
        std::string pvbase = o.str();

        _pv.push_back( new PVWriter((pvbase+"USLINKS" ).c_str()) );
        _pv.push_back( new PVWriter((pvbase+"BPLINK"  ).c_str()) );
        _pv.push_back( new PVWriter((pvbase+"DSLINKS" ).c_str()) );
        _pv.push_back( new PVWriter((pvbase+"QPLLLOCK").c_str()) );
        for(unsigned i=0; i<Pds::Dti::Module::MaxUsLinks; i++) {
          std::ostringstream u;
          u << "US" << i << ":";
          std::string usn = u.str();
          _pv.push_back( new PVWriter((pvbase+usn+"RXERRS").c_str()) );
          _pv.push_back( new PVWriter((pvbase+usn+"RXFULL").c_str()) );
          _pv.push_back( new PVWriter((pvbase+usn+"IBRECV").c_str()) );
          _pv.push_back( new PVWriter((pvbase+usn+"IBEVTS").c_str()) );
          _pv.push_back( new PVWriter((pvbase+usn+"OBRECV").c_str()) );
          _pv.push_back( new PVWriter((pvbase+usn+"OBSENT").c_str()) );
        }
        for(unsigned i=0; i<Pds::Dti::Module::MaxDsLinks; i++) {
          std::ostringstream u;
          u << "DS" << i << ":";
          std::string usn = u.str();
          _pv.push_back( new PVWriter((pvbase+usn+"RXERRS").c_str()) );
          _pv.push_back( new PVWriter((pvbase+usn+"RXFULL").c_str()) );
          _pv.push_back( new PVWriter((pvbase+usn+"OBSENT").c_str()) );
        }


        _pv.push_back( new PVWriter((pvbase+"MONCLK0:RATE" ).c_str()) );
        _pv.push_back( new PVWriter((pvbase+"MONCLK1:RATE" ).c_str()) );
        _pv.push_back( new PVWriter((pvbase+"MONCLK2:RATE" ).c_str()) );
        _pv.push_back( new PVWriter((pvbase+"MONCLK3:RATE" ).c_str()) );
        printf("PV stats allocated\n");
      }
    public:
      void update(Module& m, double dt) 
      { 
#define PVPUT(v) { *reinterpret_cast<int*>(_pv[j]->data()) = int(v); _pv[j]->put(); j++; }
#define PVPUTD(v) { *reinterpret_cast<double*>(_pv[j]->data()) = double(v); _pv[j]->put(); j++; }
        m._countCtrl = 0;
        unsigned j=0;
        unsigned linksUp = m._linksUp;
        printf("linksUp: %08x\n",linksUp);

        PVPUT(linksUp&((1<<Module::MaxUsLinks)-1));
        PVPUT((linksUp>>15)&1);
        PVPUT((linksUp>>16)&((1<<Module::MaxDsLinks)-1));
        PVPUT(m._qpllLock);

        for(unsigned i=0; i<Module::MaxUsLinks; i++) {
          m._countCtrl = i;
          PVPUT(m._usRxErrs);
          PVPUT(m._usRxFull);
          PVPUT(m._usIbRecv);
          PVPUT(m._usIbEvt);
          PVPUT(uint64_t(m._usAppRecv));
          PVPUT(uint64_t(m._usAppSent));
        }
        for(unsigned i=0; i<Module::MaxDsLinks; i++) {
          m._countCtrl = i<<16;
          PVPUT(m._dsRxErrs);
          PVPUT(m._dsRxFull);
          PVPUT(uint64_t(m._dsObSent));
        }
        //        m._countCtrl = (1<<30);
        m._countCtrl = (1<<31);

        for(unsigned i=0; i<4; i++) {
          double v = (m._monClk[i]&0x1fffffff)*1.e-6;
          PVPUTD(v);
        }
        ca_flush_io(); }
    private:
      std::vector<PVWriter*> _pv;
    };
  };
};

extern int optind;

void usage(const char* p) {
  printf("Usage: %s [-a <IP addr (dotted notation)>] [-p <port>] [-w]\n",p);
}

static void* dti_input(void*);
static void  configLinks();

int main(int argc, char** argv) {

  extern char* optarg;

  int c;
  bool lUsage = false;

  const char* ip = "10.0.2.103";
  unsigned short port = 8192;

  while ( (c=getopt( argc, argv, "a:p:wh")) != EOF ) {
    switch(c) {
    case 'a':
      ip = optarg; break;
      break;
    case 'p':
      port = strtoul(optarg,NULL,0);
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

  Pds::Cphw::Reg::set(ip, port, 0);

  Pds::Cphw::AxiVersion* vsn = new((void*)0) Pds::Cphw::AxiVersion;
  printf("buildStamp %s\n",vsn->buildStamp().c_str());

  Pds::Dti::Module*         m = new((void*)0x80000000) Pds::Dti::Module;
  m->_countCtrl = (1<<30);
  m->_countCtrl = (1<<31);

  Pds::Dti::PVStats*       pv = new Pds::Dti::PVStats;
  pv->allocate(vsn->buildStamp());

  timespec _t; clock_gettime(CLOCK_REALTIME,&_t);

  while(1) {

    sleep(1);

    timespec t; clock_gettime(CLOCK_REALTIME,&t);
    pv->update(*m,double(t.tv_sec-_t.tv_sec)+1.e-9*(double(t.tv_nsec)-double(_t.tv_nsec)));
    _t=t;
  }
}

