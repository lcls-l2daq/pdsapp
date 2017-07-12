
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

#include "pds/epicstools/EpicsCA.hh"
#include "pds/epicstools/PVWriter.hh"

#include <string>
#include <vector>
#include <sstream>

extern int optind;

static const unsigned BsaRecLen = 32*1024;

using Pds_Epics::EpicsCA;
using Pds_Epics::PVWriter;

#include <BsaField.hh>
#include <Processor.hh>

class Monitor : public Pds_Epics::PVMonitorCb {
public:
  Monitor() {}
  void updated() {}
};

static Monitor monly;

static const char* to_name(const char* base,
                           unsigned    array,
                           const char* ext)
{
  std::ostringstream o;
  o << base << ":CONTROL:BSA" << array << ":" << ext;
  return o.str().c_str();
}

class BsaControl : public Pds_Epics::PVMonitorCb {
public:
  BsaControl(AmcCarrierHw& hw,
             const char*   pvbase,
             unsigned      array) :
    _hw    (hw),
    _array (array),
    _destn (to_name(pvbase,array,"DESTN") ,&monly),
    _evsel (to_name(pvbase,array,"RSEL")  ,&monly),
    _frate (to_name(pvbase,array,"FRATE") ,&monly),
    _arate (to_name(pvbase,array,"ARATE") ,&monly),
    _ats   (to_name(pvbase,array,"ATS")   ,&monly),
    _seqidx(to_name(pvbase,array,"SEQIDX"),&monly),
    _seqbit(to_name(pvbase,array,"SEQBIT"),&monly),
    _ntoavg(to_name(pvbase,array,"NTOAVG"),&monly),
    _avgtwr(to_name(pvbase,array,"AVGTWR"),&monly),
    _start (to_name(pvbase,array,"START") ,this),
    _status(to_name(pvbase,array,"STATUS"))
  {
  }
#define PVVAL(pv,dtype) *reinterpret_cast<dtype*>(pv.data())
  void updated()
  {
    if (PVVAL(_start,unsigned)) {
      int destn = PVVAL(_destn,int);
      Bsa::BeamSelect beam(abs(destn),destn<0);
      Bsa::RateSelect rate;
      switch(PVVAL(_evsel,unsigned)) {
      case 0:
        rate = Bsa::RateSelect((Bsa::RateSelect::FixedRate)PVVAL(_frate,unsigned));
        break;
      case 1:
        rate = Bsa::RateSelect((Bsa::RateSelect::ACRate)PVVAL(_arate,unsigned));
        break;
      case 2:
        rate = Bsa::RateSelect(PVVAL(_seqidx,unsigned),PVVAL(_seqbit,unsigned));
        break;
      default:
        break;
      }
      _hw.start(_array, beam, rate, PVVAL(_avgtwr,unsigned), PVVAL(_ntoavg,unsigned));

      PVVAL(_status,int)=1;
      PVVAL(_start ,int)=0;
      _status.put();
      _start .put();
      ca_flush_io();
    }
  }
  unsigned done() 
  {
  }
private:
  AmcCarrierHw& _hw;
  unsigned      _array;
  EpicsCA _destn;
  EpicsCA _evsel;
  EpicsCA _frate;
  EpicsCA _arate;
  EpicsCA _ats;
  EpicsCA _seqidx;
  EpicsCA _seqbit;
  EpicsCA _ntoavg;
  EpicsCA _avgtwr;
  EpicsCA _start ;
  PVWriter _status;
};

class Control {
public:
  Control(const char* ip,
          const char* pvbase) : _hw(ip,true), _bsa(64)
  {
    for(unsigned i=0; i<64; i++)
      bsa[i] = new BsaControl(_hw,pvbase,i);
  }
  void done(unsigned array)
  {
    _bsa[array]->done();
  }
private:
  AmcCarrierHw _hw;
  std::vector<BsaControl*> _bsa;
};

//====================================================================
//  These belong to the core
//
class PidField : public Bsa::Field {
public:
  const char* name() const { return "PulseId"; }
  double      extract(const void* p) const { return reinterpret_cast<const uint64_t*>(p)[-2]; }
};

class NAvgField : public Bsa::Field {
public:
  const char* name() const { return "NAvg"; }
  double      extract(const void* p) const { return reinterpret_cast<const uint32_t*>(p)[-1]; }
};
//
//====================================================================
//  These belong to the application
//
class AppPidField : public Bsa::Field {
public:
  const char* name() const { return "Pid"; }
  double      extract(const void* p) const { return reinterpret_cast<const uint64_t*>(p)[0]; }
};

class AppTSField : public Bsa::Field {
public:
  const char* name() const { return "TS"; }
  double      extract(const void* p) const { 
    const uint32_t* u = reinterpret_cast<const uint32_t*>(p);
    return double(u[3])+1.e-9*double(u[2]); }
};

class AppFixedRateField : public Bsa::Field {
public:
  const char* name() const { return "FixedRate"; }
  double      extract(const void* p) const { return reinterpret_cast<const uint32_t*>(p)[4]&0x3ff; }
};

static const char* to_name(const char* base,
                           const char* ext) {
  std::ostringstream o;
  o << base << ":" << ext;
  return o.str().c_str();
}

//=================================================================
//  An EPICS implementation should replace this
//
class CaPv : public Bsa::Pv {
public:
  CaPv(const std::string& name,
       Bsa::Field& f ) :
    _f(f), _v(0), _ca(to_name(name.c_str(),f.name())) 
  { _v.reserve(32*1024); }
  ~CaPv() {}
  const Bsa::Field& field() const { return _f; }
  void setTimestamp(unsigned sec,
                    unsigned nsec) {
    _ts_sec  = sec;
    _ts_nsec = nsec;
  }
  void clear() { _v.clear(); printf("clear %s\n", _f.name()); }
  void append(double v) { _v.push_back(v); }
  void flush() {
    memcpy( _ca.data(), _v.data(), _v.size()*sizeof(double) );
    _ca.put();
  }
private:
  Bsa::Field& _f;
  unsigned    _ts_sec;
  unsigned    _ts_nsec;
  std::vector<double> _v;
  PVWriter            _ca;
};

class CaPvArray : public Bsa::PvArray {
public:
  CaPvArray() {}
  CaPvArray(const std::string&              base,
            unsigned                        array,
            const std::vector<Bsa::Field*>& fields) :
    _array(array),
    _pvs  (fields.size())
  {
    std::ostringstream o;
    o << base << ":BSA" << array;
    std::string name = o.str();
    for(unsigned i=0; i<fields.size(); i++)
      _pvs[i] = new CaPv(name, *fields[i]);
  }
  ~CaPvArray() { for(unsigned i=0; i<_pvs.size(); i++) delete _pvs[i]; }
public:
  unsigned array() const { return _array; }
  std::vector<Bsa::Pv*> pvs() { return _pvs; }
  void flush() { for(unsigned i=0; i<_pvs.size(); i++) _pvs[i]->flush(); }
private:
  unsigned              _array;
  std::vector<Bsa::Pv*> _pvs;
};

int main(int argc, char* argv[])
{
  const char* ip = "192.168.2.10";
  const char* pvname = 0;
  unsigned uinterval=1000000;
  int c;
  while( (c=getopt(argc,argv,"a:P:I:"))!=-1 ) {
    switch(c) {
    case 'a':
      ip = optarg; break;
    case 'P':
      pvname = optarg; break;
    case 'I':
      uinterval = unsigned(1.e6*strtod(optarg,NULL));
      break;
    default:
      //      show_usage(argv[0]);
      exit(1);
    }
  }

  //  Handle control PVs
  Bsa::Control* control = new Bsa::Control(ip);

  //
  //  The application fills this vector
  //  The rest is handled by the core here
  //
  std::vector<Bsa::Field*> fields;
  fields.push_back(new AppPidField);
  fields.push_back(new AppTSField);
  fields.push_back(new AppFixedRateField);

  //  The core adds these common fields
  fields.push_back(new PidField);
  fields.push_back(new NAvgField);

  std::vector<CaPvArray> arrays(64);
  for(unsigned i=0; i<64; i++)
    arrays[i] = CaPvArray(pvname, i, fields);

  Bsa::Processor* p = Bsa::Processor::create(ip);

  //
  //  We poll here.  
  //  Real implementation will process when caget is requested.
  //
  while(1) {
    bool updated=false;

    for(unsigned i=0; i<arrays.size(); i++) {
      if (p->update(arrays[i])) {
        arrays[i].flush();
        updated=true;
        control->done(i);
      }
    }

    if (updated)
      ca_flush_io();

    usleep(uinterval);
  }

  return 1;
}
