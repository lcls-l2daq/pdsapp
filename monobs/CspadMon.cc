#include "pdsapp/monobs/CspadMon.hh"
#include "pdsapp/monobs/ShmClient.hh"
#include "pdsapp/monobs/Handler.hh"
#include "pds/config/CsPadConfigType.hh"
#include "pds/config/CsPadDataType.hh"
#include "pds/config/CsPad2x2ConfigType.hh"
#include "pds/config/CsPad2x2DataType.hh"
#include "pds/config/EvrConfigType.hh"
#include "pdsdata/xtc/DetInfo.hh"
#include "pdsdata/xtc/ClockTime.hh"
#include "pdsdata/xtc/Xtc.hh"
#include "pds/epicstools/PVWriter.hh"

#include <math.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using Pds_Epics::PVWriter;

static const unsigned MaxSections=32;
static const unsigned BYKIK=162;
static const unsigned ALKIK=163;
static const Pds::DetInfo evrInfo(0,Pds::DetInfo::NoDetector, 0, Pds::DetInfo::Evr, 0);
static bool _verbose = false;

typedef float    CspadSection  [Pds::CsPad::ColumnsPerASIC][2*Pds::CsPad::MaxRowsPerASIC];
typedef unsigned CspadStatusMap[Pds::CsPad::ColumnsPerASIC][2*Pds::CsPad::MaxRowsPerASIC];

namespace PdsCas {
  class CspadPixelStatus {
  public:
    enum {VeryHot=1,
          Hot=2,
          Cold=4};
    CspadPixelStatus() : _badFlags(1) {}
    ~CspadPixelStatus() {}
  public:
    bool ok(unsigned col, unsigned row) const 
    { return ((_section[col][row] & _badFlags) == 0); }
    void setBadFlags(unsigned flag) { _badFlags=flag; }
  public:
    unsigned _badFlags;
    CspadStatusMap _section;
  };

  class EvrHandler : public Handler {
  public:
    EvrHandler() :
      Handler(evrInfo, Pds::TypeId::Id_EvrData, Pds::TypeId::Id_EvrConfig) {}
    ~EvrHandler() {}
  public:
    void   _configure(const void* payload, const Pds::ClockTime& t) {}
    void   _event    (const void* payload, const Pds::ClockTime& t)
    {
      const EvrDataType& d = *reinterpret_cast<const EvrDataType*>(payload);
      for(unsigned i=0; i<d.numFifoEvents(); i++)
        if (d.fifoEvents()[i].eventCode()==BYKIK ||
            d.fifoEvents()[i].eventCode()==ALKIK) {
          _bykik = true;
          return;
        }
      _bykik = false;
    }
    void   _damaged  () {}
  public:
    void   initialize() {}
    void   update_pv () {}
  public:
    bool _bykik;
  };

  class CspadHandler : public Handler {
  public:
    CspadHandler(const Pds::Src&     info) :
      Handler(info, Pds::TypeId::Id_CspadElement, Pds::TypeId::Id_CspadConfig)
    {
      _offsets = new CspadSection    [MaxSections];
      _result  = new CspadSection    [MaxSections];
      _status  = new CspadPixelStatus[MaxSections];
    
      sprintf(_statusMap,"/tmp/cspad.%08x.sta",info.phy());
      sprintf(_pedFile  ,"/tmp/cspad.%08x.dat",info.phy());
    }
    ~CspadHandler() {}
  public:
    void   _configure(const void* payload, const Pds::ClockTime& t) 
    {
      _config = *reinterpret_cast<const CsPadConfigType*>(payload);
      _loadConstants(); 
    }
    void   _event    (const void* payload, const Pds::ClockTime& t) 
    { 
      _payload=payload;
    }
  public:
    void  process()
    {
      if (_validConstants) {
        for(unsigned q=0; q<_config.numQuads(); q++) {
          const CsPadElementType& elem = reinterpret_cast<const CsPadDataType*>(_payload)->quads(_config,q);
          ndarray<const int16_t,3> a = elem.data(_config);
          for(unsigned i=0; i<a.shape()[0]; i++) {
            unsigned section_id = elem.quad()*8 + i;
            ndarray<const int16_t,2> s = make_ndarray(&a[i][0][0],a.shape()[1],a.shape()[2]);
            try {
              float mean_rms = _getPedestalConsistency(s, _offsets[section_id], _status[section_id]);
              //  Fill a PV array with these values?
              printf("section %d : pedrms %g\n",section_id,mean_rms);
            }
            catch (std::string errMsg) {
            }
          }
        }
      }
    }
    void   _damaged  () {}
  public:
    void   initialize() {}
    void   update_pv () {}
  private:
    void   _loadConstants() {
      _validConstants=false;

      FILE* status = fopen(_statusMap, "r");
      if (status) {
        printf("Loading status map from %s\n", _statusMap);
        char* linep = NULL;
        size_t sz = 0;
        char* sEnd;
        for (unsigned section=0; section < MaxSections; section++) {
          CspadStatusMap& s = _status[section]._section;
          for (unsigned col=0; col < Pds::CsPad::ColumnsPerASIC; col++) {
            getline(&linep, &sz, status);
            s[col][0] = strtoul(linep, &sEnd, 0);
            for (unsigned row=1; row < 2*Pds::CsPad::MaxRowsPerASIC; row++)
              s[col][row] = strtoul(sEnd, &sEnd, 0);
          }
        }
      }
      else {
        printf("Failed to open %s, no valid status map\n", _statusMap);
        return;
      }

      FILE* peds = fopen(_pedFile, "r");
      if (peds) {
        printf("Loading pedestal map from %s\n", _pedFile);
        char* linep = NULL;
        size_t sz = 0;
        char* pEnd;
        for (unsigned section=0; section < MaxSections; section++) {
          CspadSection& p = _offsets[section];
          for (unsigned col=0; col < Pds::CsPad::ColumnsPerASIC; col++) {
            getline(&linep, &sz, peds);
            p[col][0] = strtod(linep, &pEnd);
            for (unsigned row=1; row < 2*Pds::CsPad::MaxRowsPerASIC; row++)
              p[col][row] = strtod(pEnd, &pEnd);
          }
        }
      }
      else {
        printf("Failed to open %s, no valid pedestal map\n", _pedFile);
        return;
      }
      _validConstants = true;
    }

    float  _getPedestalConsistency(const ndarray<const int16_t,2>& s,
                                   const CspadSection&        o,
                                   const CspadPixelStatus&    m)
    {
      double s0 = 0.;
      double s1 = 0.;
      double s2 = 0.;
      for(unsigned col=0; col<s.shape()[0]; col++) {
        for(unsigned row=0; row<s.shape()[1]; row++) {
          if (not m.ok(col, row)) continue; // simple if slow
          float val = float(s[col][row]) - o[col][row];
          s0++;
          s1 += val;
          s2 += val*val;
        }
      }
      return sqrt(s0*s2 - s1*s1)/s0;
    }
  private:
    CspadSection*     _offsets;  // current valid dark frame offsets
    CspadSection*     _result ;  // we keep ownership of result array
    CspadPixelStatus* _status ;
    char              _statusMap[64];
    char              _pedFile  [64];
    bool              _validConstants;
    const void*       _payload;
    CsPadConfigType   _config;
  };

  class CspadEHandler : public EvtHandler {
  public:
    CspadEHandler(const EvrHandler& evr,
                  CspadHandler& cspad) :
      _evr  (evr),
      _cspad(cspad) {}
  public:
    void   _configure(const Pds::ClockTime& t) {}
    void   _event    (const Pds::ClockTime& t) {
      if (_evr._bykik)
        _cspad.process();
    }
    void   _damaged  () {}
  public:
    void         initialize() {}
    void         update_pv () {}
  private:
    const EvrHandler& _evr;
    CspadHandler& _cspad;
  };

  class CspadTemp {
  public:
    CspadTemp ( ) {
      // Constants
      const double coeffA = -1.4141963e1;
      const double coeffB =  4.4307830e3;
      const double coeffC = -3.4078983e4;
      const double coeffD = -8.8941929e6;
      const double t25    = 10000.0;
      const double k0     = 273.15;
      const double vmax   = 3.3;
      const double vref   = 2.5;
      const double rdiv   = 20000;
      
      // Temp range
      const double minTemp = -50;
      const double maxTemp = 150;
      const double incTemp = 0.01;
      
      double       temp;
      double       tk;
      double       res;
      double       volt;
      unsigned int idx;

      temp = minTemp;
      while ( temp < maxTemp ) {
        tk = k0 + temp;
        res = t25 * exp(coeffA+(coeffB/tk)+(coeffC/(tk*tk))+(coeffD/(tk*tk*tk)));      
        volt = (res*vmax)/(rdiv+res);
        idx = (int)((volt / vref) * (double)(adcCnt-1));
        if ( idx < adcCnt ) tempTable[idx] = temp; 
        temp += incTemp;
      }
    }

    // Get Temperature from adc value, deg C
    double getTemp (unsigned adcValue)
    {
      if ( adcValue < adcCnt) return(tempTable[adcValue]);
      else return(0);
    }
  private:
    enum { adcCnt = 4096 };
    double tempTable[adcCnt];
  };

  static CspadTemp _cspad_temp;

  class CspadTHandler : public Handler {
  public:
    enum { NTHERMS=16 };
    CspadTHandler(const char* pvbase, const DetInfo& info) :
      Handler(info, Pds::TypeId::Id_CspadElement, Pds::TypeId::Id_CspadConfig),
      _initialized(false)
    { 
      strncpy(_pvName,pvbase,PVNAMELEN); 
    }
    ~CspadTHandler()
    {
      if (_initialized) {
        for(unsigned i=0; i<NTHERMS; i++)
          delete _valu_writer[i];
      }
    }
  public:
    void   _configure(const void* payload, const Pds::ClockTime& t) 
    {
      _cfg = *reinterpret_cast<const CsPadConfigType*>(payload);
    }
    void   _event    (const void* payload, const Pds::ClockTime& t)
    {
      if (!_initialized) return;

      for(unsigned q=0; q<_cfg.numQuads(); q++) {
        const CsPadElementType& elem = reinterpret_cast<const CsPadDataType*>(payload)->quads(_cfg,q);
        ndarray<const uint16_t,1> a = elem.sb_temp();
        for(unsigned j=0; j<a.shape()[0]; j++) {
          int i = 4*elem.quad()+j;
          *reinterpret_cast<double*>(_valu_writer[i]->data()) = _cspad_temp.getTemp(a[j]);
          if (_verbose)
            printf("sb_temp  quad %d  therm %d  %f [%d]\n",
                   elem.quad(), j,
                   *reinterpret_cast<double*>(_valu_writer[i]->data()),
                   a[j]);
        }
      }
    }
    void   _damaged  () {}
  public:
    void    initialize()
    {
      char buff[64];
      for(unsigned i=0; i<NTHERMS; i++) {
        sprintf(buff,"%s:QUAD%d:TEMP%d.VAL",_pvName,i/4,i%4);
        printf("Initializing CspadT %s\n",buff);
        _valu_writer[i] = new PVWriter(buff);
      }
      _initialized = true;
    }
    void    update_pv () 
    {
      for(unsigned i=0; i<NTHERMS; i++) 
        _valu_writer[i]->put();
    }
  private:
    enum { PVNAMELEN=32 };
    char _pvName[PVNAMELEN];
    bool _initialized;
    PVWriter* _valu_writer[NTHERMS];
    CsPadConfigType _cfg;
  };

  class CspadMiniTHandler : public Handler {
  public:
    enum { NTHERMS=4 };
    CspadMiniTHandler(const char* pvbase, const DetInfo& info) :
      Handler(info, Pds::TypeId::Id_Cspad2x2Element, Pds::TypeId::Id_Cspad2x2Config),
      _initialized(false)
    { 
      strncpy(_pvName,pvbase,PVNAMELEN); 
    }
    ~CspadMiniTHandler()
    {
      if (_initialized) {
        for(unsigned i=0; i<NTHERMS; i++)
          delete _valu_writer[i];
      }
    }
  public:
    void   _configure(const void* payload, const Pds::ClockTime& t) 
    {
      _cfg = *reinterpret_cast<const CsPad2x2ConfigType*>(payload);
    }
    void   _event    (const void* payload, const Pds::ClockTime& t)
    {
      if (!_initialized) return;

      const CsPad2x2DataType& elem = *reinterpret_cast<const CsPad2x2DataType*>(payload);
      for(int a=0; a<NTHERMS; a++)
        *reinterpret_cast<double*>(_valu_writer[a]->data()) = 
          _cspad_temp.getTemp(elem.sb_temp()[a]);
    }
    void   _damaged  () {}
  public:
    void    initialize()
    {
      char buff[64];
      for(unsigned i=0; i<NTHERMS; i++) {
        sprintf(buff,"%s:TEMP%d.VAL",_pvName,i%4);
        printf("Initializing CspadMiniT %s\n",buff);
        _valu_writer[i] = new PVWriter(buff);
      }
      _initialized = true;
    }
    void    update_pv () 
    {
      for(unsigned i=0; i<NTHERMS; i++) 
        _valu_writer[i]->put();
    }
  private:
    enum { PVNAMELEN=32 };
    char _pvName[PVNAMELEN];
    bool _initialized;
    PVWriter* _valu_writer[NTHERMS];
    CsPad2x2ConfigType _cfg;
  };
};

void PdsCas::CspadMon::monitor(ShmClient&     client,
                               const char*    pvbase,
                               const DetInfo& det)
{
  EvrHandler* evr = new EvrHandler;
  CspadHandler* cspad = new CspadHandler(det);
  client.insert(evr);
  client.insert(cspad);
  client.insert(new CspadEHandler(*evr,*cspad));
  client.insert(new CspadTHandler(pvbase,det));
  client.insert(new CspadMiniTHandler(pvbase,det));
}

void PdsCas::CspadMon::monitor(ShmClient&     client,
                               const char*    pvbase,
                               unsigned       detid,
                               unsigned       devid)
{
  {
    DetInfo info(0, DetInfo::Detector(detid), 0, DetInfo::Cspad, devid);
    EvrHandler* evr = new EvrHandler;
    CspadHandler* cspad = new CspadHandler(info);
    client.insert(evr);
    client.insert(cspad);
    client.insert(new CspadEHandler(*evr,*cspad));
    client.insert(new CspadTHandler(pvbase,info));
  }
  {
    DetInfo info(0, DetInfo::Detector(detid), 0, DetInfo::Cspad2x2, devid);
    client.insert(new CspadMiniTHandler(pvbase,info));
  }
}

void PdsCas::CspadMon::verbose() { _verbose=true; }
