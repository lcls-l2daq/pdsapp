#ifndef tdm_hh
#define tdm_hh

//
//  Convert phase detector counts to picoseconds
//
static const double pd_to_ps  = 10*7/1300e6/double(1UL<<26)*1.e12;
static const double txd_to_ps = 7/1300e6/double(40)/double(64)*1.e12;
//static const double pd_to_txd = 5/double(1UL<<13);
static const double pd_to_txd = 25/double(1UL<<16);
static const unsigned MAX_PD = (1<<25);

#define NLINKS 14

#define BF_EXTRACT( reg, sh, nb ) \
  (unsigned(reg) >> sh) & ((1<<nb)-1)

#define BF_REPLACE( reg, f, sh, nb ) {                  \
    unsigned m = ((1<<nb)-1)<<sh;                       \
    reg =  (unsigned(reg) & ~m) | (( f<<sh ) & m );     \
  }

namespace TDM {
  class DelayPLL {
  public:
    Pds::Cphw::Reg _reg;
  };

  class SyncClockFreq {
  private:
    Pds::Cphw::Reg _reg;
  public:
    unsigned status() const { return unsigned(_reg)>>29; }
    unsigned rateHz() const { return unsigned(_reg)&0x1fffffff; }
  };

  class DelayLink {
  public:
    Pds::Cphw::Reg _csr;
    Pds::Cphw::Reg _txAlign;
    Pds::Cphw::Reg _txDelayRd;
    Pds::Cphw::Reg _txDelayWr;
    //
    Pds::Cphw::Reg _loopPhase;
    Pds::Cphw::Reg _rxAlign;
    Pds::Cphw::Reg _haddr;
    Pds::Cphw::Reg _hdata;
  public:
    bool     loopback     () const { return BF_EXTRACT(_csr,31,1); }
    void     loopback     (bool v) { BF_REPLACE(_csr,(v?1:0),31,1); }
    bool     rxPolarity   () const { return BF_EXTRACT(_csr,24,1); }
    void     rxPolarity   (bool v) { BF_REPLACE(_csr,(v?1:0),24,1); }
    bool     allLock      () const { return (unsigned(_csr)>>25)==0xf; }
    unsigned rxErrors     () const { return unsigned(_csr)&0xfff; }
    unsigned txAlignTarget() const { return unsigned(_txAlign)&0xffff; }
    void     txAlignTarget(unsigned v) { _txAlign = v&0xffff; }
    unsigned txAlignValue () const { return unsigned(_txAlign)>>16; }
    unsigned rxAlignTarget() const { return BF_EXTRACT(_rxAlign,0,7); }
    void     rxAlignTarget(unsigned v) { BF_REPLACE(_rxAlign,v,0,7); }
    unsigned rxAlignMask  () const { return BF_EXTRACT(_rxAlign,8,7); }
    void     rxAlignMask  (unsigned v) { BF_REPLACE(_rxAlign,v,8,7); }
    unsigned rxAlignValue () const { return BF_EXTRACT(_csr,16,7); }
  public:
    unsigned txDelay      () const { return unsigned(_txDelayRd); }
    void     txDelay      (unsigned v, bool fast=false) { _txDelayWr = v | (fast ? (1<<31):0); }
    unsigned pmDelay      () const { return BF_EXTRACT(_loopPhase,28,4); }
    void     pmDelay      (unsigned v) { BF_REPLACE(_loopPhase,v,28,4); }
    int      loopPhase    () const { return int(unsigned(_loopPhase)<<5)>>5; }
    unsigned loopClks     () const { return BF_EXTRACT(_rxAlign,16,16); }
    void     rxReset() {
      unsigned v = unsigned(_csr);
      _csr = v | (1<<30);
      usleep(10);
      _csr = v &~ (1<<30);
      usleep(100000);
    }

  public:
    unsigned txLockEntry(unsigned addr) {
      _haddr = addr;
      usleep(1);
      return _hdata & 0xffff;
    }
    unsigned rxLockEntry(unsigned addr) {
      _haddr = addr << 16;
      usleep(1);
      return unsigned(_hdata) >> 16;
    }
  };

  class Module {
  public:
    DelayPLL       _pll [2];
    Pds::Cphw::Reg _qpll[2];
    SyncClockFreq  _refclk[5];
    uint32_t       _rsv_100[(0x100-0x24)>>2];
    DelayLink      _source;
    uint32_t       _rsv_200[(0x100-sizeof(_source))>>2];
    DelayLink      _link[14];
  public:
    void pllReset(int amc) {
      unsigned v= unsigned(_pll[amc]._reg);
      _pll[amc]._reg = v & ~(1<<27);
      usleep(10);
      _pll[amc]._reg = v |  (1<<27);
      usleep(100000);
    }
    void rxReset(int link) {
      unsigned v = unsigned(_link[link]._csr);
      _link[link]._csr = v | (1<<30);
      usleep(10);
      _link[link]._csr = v &~ (1<<30);
      usleep(100000);
    }
    void txReset(unsigned mask) {
      for(unsigned i=0; i<14; i++)
        if (mask & (1<<i)) {
          unsigned v = unsigned(_link[i]._csr);
          _link[i]._csr = v | (1<<29);
          usleep(10);
          _link[i]._csr = v &~ (1<<29);
        }
      usleep(100000);
    }
    void txWait(unsigned v, bool fast=false) {
      unsigned s = v;
      unsigned q = (1<<NLINKS)-1;
      for(unsigned i=0; i<NLINKS; i++) {
        _link[i].pmDelay(0);
        _link[i].txDelay(s,fast);
      }
      do {
        usleep(fast ? 10000 : 1000000);
        for(unsigned i=0; i<NLINKS; i++)
          if (_link[i].txDelay() == s)
            q &= ~(1<<i);
      } while( q != 0 );
    }
  };
};

#endif
