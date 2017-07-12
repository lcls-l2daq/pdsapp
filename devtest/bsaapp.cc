//
//  Utility to control BSA
//
//  Record is 128 bytes length:
//    PulseID     :   8 bytes
//    NWords      :   4 bytes
//    ?           :   4 bytes
//    Sensor data : 112 bytes
//
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <semaphore.h>
#include <new>
#include <arpa/inet.h>
#include <math.h>

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

//=================================================================
//  This is just for the text dump application
//  An EPICS implementation should replace this
//
class TextPv : public Bsa::Pv {
public:
  TextPv(Bsa::Field& f) : _f(f), _v(0) {}
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
    printf("-- %20.20s [%u.%09u] --\n", _f.name(), _ts_sec, _ts_nsec);
    for(unsigned i=0; i<_v.size(); i++)
      printf("%f%c", _v[i], (i%5)==4?'\n':' ');
    if (_v.size()%5)
      printf("\n");
  }
private:
  Bsa::Field& _f;
  unsigned    _ts_sec;
  unsigned    _ts_nsec;
  std::vector<double> _v;
};

class TextPvArray : public Bsa::PvArray {
public:
  TextPvArray(unsigned                array,
              const std::vector<Bsa::Pv*>& pvs) :
    _array(array),
    _pvs  (pvs)
  {
  }
public:
  unsigned array() const { return _array; }
  std::vector<Bsa::Pv*> pvs() { return std::vector<Bsa::Pv*>(_pvs); }
  void reset(uint32_t, uint32_t) {}
  void append(uint64_t) {}
private:
  unsigned _array;
  const std::vector<Bsa::Pv*>& _pvs;
};

//=======================================================

static void show_usage(const char* p)
{
  printf("** Fetch BSA data **\n");
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <IP address dotted notation> : set carrier IP\n");
  printf("         -F <array>                      : force fetch of BSA array\n");
  printf("         -I <update interval>            : retries updates\n");
}

int main(int argc, char* argv[])
{
  const char* ip = "192.168.2.10";
  unsigned array=0;
  unsigned uinterval=1000000;
  int c;
  while( (c=getopt(argc,argv,"a:F:I:"))!=-1 ) {
    switch(c) {
    case 'a':
      ip = optarg; break;
    case 'F':
      array = strtoul(optarg,NULL,0);
      break;
    case 'I':
      uinterval = unsigned(1.e6*strtod(optarg,NULL));
      break;
    default:
      show_usage(argv[0]);
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

  //  We only need this vector for the "dump" function call
  std::vector<TextPv*> pvs;
  for(unsigned i=0; i<fields.size(); i++)
    pvs.push_back(new TextPv(*fields[i]));

  std::vector<Bsa::Pv*> cpvs;
  for(unsigned i=0; i<pvs.size(); i++)
    cpvs.push_back(pvs[i]);
  
  TextPvArray pva(array, cpvs);

  Bsa::Processor* p = Bsa::Processor::create(ip,false);

  //
  //  We simulate periodic update requests here
  //
  while(1) {
    if (p->update(pva)) {
      for(unsigned i=0; i<pvs.size(); i++)
        pvs[i]->flush();
    }
    usleep(uinterval);
  }

  return 1;
}
