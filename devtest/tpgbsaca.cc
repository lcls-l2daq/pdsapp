
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>

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
  void append() { _v.push_back(NAN); }
  void append(unsigned n,
              double   mean,
              double   rms2) { _v.push_back(mean); }
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
  void reset(uint32_t sec, uint32_t nsec) {}
  void append(uint64_t pulseId) {}
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

  Bsa::Processor* p = Bsa::Processor::create(ip,false);

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
      }
    }

    if (updated)
      ca_flush_io();

    usleep(uinterval);
  }

  return 1;
}
