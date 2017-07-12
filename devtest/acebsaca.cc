
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
class PidLField : public Bsa::Field {
public:
  const char* name() const { return "PIDL"; }
  double      extract(const void* p) const { return reinterpret_cast<const int32_t*>(p)[-4]; }
};

class PidHField : public Bsa::Field {
public:
  const char* name() const { return "PIDH"; }
  double      extract(const void* p) const { return reinterpret_cast<const int32_t*>(p)[-3]; }
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
class DigInField : public Bsa::Field {
public:
  DigInField(unsigned index) : _index(index)
  {
    std::ostringstream o;
    o << "DIG" << index;
    _name = o.str();
  }
  const char* name() const { return _name.c_str(); }
  double      extract(const void* p) const { 
    unsigned v = reinterpret_cast<const uint32_t*>(p)[0];
    v ^= 0xf; // first four inputs (LEMO) of digital card are inverted
    return (v>>_index)&1; }
private:
  unsigned    _index;
  std::string _name;
};

class AppPidHField : public Bsa::Field {
public:
  const char* name() const { return "APIDH"; }
  double      extract(const void* p) const { return reinterpret_cast<const int32_t*>(p)[1]; }
};

class AppPidLField : public Bsa::Field {
public:
  AppPidLField(unsigned i) : _chan(i)
  {
    std::ostringstream o;
    o << "APIDL" << i;
    _name = o.str();
  }
  const char* name() const { return _name.c_str(); }
  double      extract(const void* p) const { return reinterpret_cast<const int32_t*>(p)[2+_chan]; }
  unsigned    _chan;
  std::string _name;
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
       Bsa::Field& f,
       bool dbg) :
    _f(f), _v(0), 
    _ca (to_name (name.c_str(),f.name())), 
    _dbg(dbg) 
  { _v.reserve(32*1024); }
  ~CaPv() {}
  const Bsa::Field& field() const { return _f; }
  void setTimestamp(unsigned sec,
                    unsigned nsec) {
    _ts_sec  = sec;
    _ts_nsec = nsec;
  }
  void clear() { _v.clear(); }
  void append() { _v.push_back(NAN); }
  void append(unsigned n,
              double   mean,
              double   rms2) { _v.push_back(mean); }
  void flush() {
    if (_ca.connected()) {
      unsigned nbytes = _v.size()*sizeof(int);
      if (nbytes > _ca.data_size())
        nbytes = _ca.data_size();
      memcpy( _ca.data(), _v.data(), nbytes);
      _ca.put();
    }
  }
private:
  Bsa::Field& _f;
  unsigned    _ts_sec;
  unsigned    _ts_nsec;
  std::vector<int>    _v;
  PVWriter            _ca;
  bool        _dbg;
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
      _pvs[i] = new CaPv(name, *fields[i], array==2);
    _can = new PVWriter(to_name(name.c_str(),"NACQ"));
  }
  ~CaPvArray() {}// for(unsigned i=0; i<_pvs.size(); i++) delete _pvs[i]; }
public:
  unsigned array() const { return _array; }
  std::vector<Bsa::Pv*> pvs() { return _pvs; }
  void flush(int n) { 
    *reinterpret_cast<int32_t*>(_can->data()) = n;
    _can->put();
    for(unsigned i=0; i<_pvs.size(); i++) _pvs[i]->flush(); 
  }
  void reset(uint32_t, uint32_t) {}
  void append(uint64_t) {}
private:
  unsigned              _array;
  std::vector<Bsa::Pv*> _pvs;
  PVWriter*             _can;
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
  for(unsigned i=0; i<5; i++) 
    fields.push_back(new DigInField(i));
  fields.push_back(new AppPidHField);
  //  for(unsigned i=2; i<28; i++)
  //    fields.push_back(new AppPidLField(i));

  //  The core adds these common fields
  fields.push_back(new PidHField);
  fields.push_back(new PidLField);
  fields.push_back(new NAvgField);

  //  EPICS thread initialization
  SEVCHK ( ca_context_create(ca_enable_preemptive_callback ), 
           "acebsaca calling ca_context_create" );

  std::vector<CaPvArray> arrays(60);
  for(unsigned i=0; i<arrays.size(); i++)
    arrays[i] = CaPvArray(pvname, i, fields);

  Bsa::Processor* p = Bsa::Processor::create(ip,true);

  //
  //  We poll here.  
  //  Real implementation will process when caget is requested.
  //
  while(1) {
    bool updated=false;

    for(unsigned i=0; i<arrays.size(); i++) {
      int n = p->update(arrays[i]);
      if (n) {
        printf("array %u updated\n",i);
        
        arrays[i].flush(n);
        updated=true;
      }
    }

    if (updated)
      ca_flush_io();

    usleep(uinterval);
  }

  return 1;
}
