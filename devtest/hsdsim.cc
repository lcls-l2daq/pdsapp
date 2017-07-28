/**
 **  Simulate high speed digitizer data:
 **
 **    Simulates raw data as a simple ramp
 **    Data format is:
 **    [32-byte event header]
 **      [ 63:  0] pulseid, 
 **      [127: 64] timestamp, 
 **      [159:128] tag word, 
 **      [191:160] l1 counter
 **      [235:192] raw samples
 **      [243:236] channel mask
 **      [307:244] reserved
 **      For each channel in channel mask
 **        For each FEX algorithm configured
 **          [16-byte FEX header]
 **            [ 30:  0] number of 2-byte payload samples (NSAMP)
 **            [ 31 ]    overflow
 **            [ 47: 32] begin offset into payload (in 2-byte units)
 **            [ 63: 48] internal buffer number
 **            [127: 64] reserved
 **          [NSAMP*2-byte payload samples]
 **            [15]      data type (0=sample data, 1=spacer)
 **            [14:0]    data (sample value or spacer samples-1)
 **/

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

#include <list>

struct EventHeader { 
  uint64_t pulseId;
  uint64_t timeStamp;
  uint32_t trigTag;
  uint32_t l1Count;
  unsigned rawSamples:24;
  unsigned channelMask:8;
  uint32_t reserved;
};

struct FexHeader {
  unsigned payloadSamples:31;
  unsigned overflow      :1;
  unsigned payloadOffset :16;
  unsigned bufferIndex   :16;
  uint64_t reserved;
};

class Sim {
private:
  unsigned _seed;
  unsigned _nsamples;
  char*    _buff;
public:
  Sim(unsigned nsamples);
  ~Sim();
public:
  const char* generate();
  size_t      size() const;
};

class Fex {
public:
  enum { RAW=0, THR=1 };
  static Fex* create(const char* args);
public:
  Fex();
  virtual ~Fex() {}
  virtual char* process(const char* begin,
                        const char* end,
                        char* output) = 0;
  virtual Fex* clone() = 0;
protected:
  bool include() { return (_iscale++%_prescale)==0; }
private:
  unsigned _prescale;
  unsigned _iscale;
};

class RawFex : public Fex {
public:
  RawFex(const char* args);
  ~RawFex();
public:
  Fex* clone();
  char* process(const char* begin,
                const char* end,
                char*       output);
private:
  unsigned _first;
  unsigned _nsamples;
};

class ThrFex : public Fex {
public:
  ThrFex(const char* args);
  ~ThrFex();
public:
  Fex* clone();
  char* process(const char* begin,
                const char* end,
                char*       output);
private:
  unsigned _low;
  unsigned _high;
};


void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -n <events>       : simulate # events\n");
  printf("         -c <nchannels>    : simulate nchannels readout\n");
  printf("         -s <samples>      : simulate # samples\n");
  printf("         -o <filename>     : record to filename\n");
  printf("         -x <FEX_ID,PARMS> : add FEX_ID alorithm with its config PARMS\n");
  printf("            RAW,<prescale>,<first>,<nsamples>\n");
  printf("            THR,<prescale>,<low>,<high>\n");
}

int main(int argc, char** argv) {

  extern char* optarg;

  int c;
  bool lUsage = false;
  unsigned nevents   = 0;
  unsigned nchannels = 0;
  unsigned nsamples  = 0;
  FILE* outfile = 0;
  std::list<Fex*> fexlist;

  while ( (c=getopt( argc, argv, "n:c:x:o:s:")) != EOF ) {
    switch(c) {
    case 'n': nevents   = strtoul(optarg,NULL,0); break;
    case 's': nsamples  = strtoul(optarg,NULL,0); nsamples = (nsamples+15)&~0xf; break;
    case 'c': nchannels = strtoul(optarg,NULL,0); break;
    case 'x': fexlist.push_back(Fex::create(optarg)); break;
    case 'o': outfile = fopen(optarg,"w");
      if (!outfile) {
        perror("Opening output file");
        return 1;
      }
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

  //  Clone fex for each channel
  std::list< std::list<Fex*> > chfex;
  for(unsigned i=0; i<nchannels; i++) {
    std::list<Fex*> fexl;
    for(std::list<Fex*>::iterator it=fexlist.begin(); it!=fexlist.end(); it++)
      fexl.push_back((*it)->clone());
    chfex.push_back(fexl);
  }

  uint64_t pulseId = 0;
  uint64_t timeStamp;
  { timespec tv; clock_gettime(CLOCK_REALTIME,&tv);
    timeStamp = tv.tv_sec;
    timeStamp <<= 32;
    timeStamp += tv.tv_nsec; }

  Sim sim(nsamples);
  char* event = new char[1<<24];  // big enough for any 1 event

  while(nevents--) {
    char* p = event;
    //  Write 32 byte event header
    { uint64_t* u = reinterpret_cast<uint64_t*>(p);
      *u++ = pulseId;
      *u++ = (timeStamp += 14000/13);
      *u++ = (pulseId<<32) | 0;
      *u++ = (((1<<nchannels)-1)<<24) | nsamples;
      pulseId++;
      p = reinterpret_cast<char*>(u); }

    //  Write each channel
    for(std::list< std::list<Fex*> >::iterator chit=chfex.begin();
        chit!=chfex.end(); chit++) {
      //  Simulate raw data
      const char* begin = sim.generate();
      const char* end   = begin + sim.size();
      //  Apply data reductions
      for(std::list<Fex*>::iterator it=chit->begin(); it!=chit->end(); it++) {
        p = (*it)->process(begin, end, p);
      }
    }
    if (outfile)
      fwrite(event, event-p, 1, outfile);
    else {
      { // print it
        printf("----\n");
        const uint32_t* q = reinterpret_cast<const uint32_t*>(event);
        const uint32_t* e = reinterpret_cast<const uint32_t*>(p);
        for(unsigned i=0; q<e; i++)
          printf("%08x%c", *q++, (i&7)==7 ? '\n':' ');
        unsigned sz = p-event;
        if (sz&0x1f)
          printf("\n");
        printf("[%x]---\n",sz);
      }
      { //  Iterate through it
        const EventHeader* evHdr = reinterpret_cast<const EventHeader*>(event);
        printf("[Event]: pulseId %016llx  timestamp %016llx  l1Count %08x  channelMask %02x\n",
               evHdr->pulseId, evHdr->timeStamp, evHdr->l1Count, evHdr->channelMask);
        const char* p = reinterpret_cast<const char*>(evHdr+1);
        for(unsigned ch=0; ch<8; ch++) {
          if (evHdr->channelMask & (1<<ch)) {
            printf("  [Channel %u]\n", ch);
            for(std::list<Fex*>::iterator it=fexlist.begin(); it!=fexlist.end(); it++) {
              const FexHeader* fexHdr = reinterpret_cast<const FexHeader*>(p);
              printf("    [Fex]: payload %u  offset %u\n", 
                     fexHdr->payloadSamples, fexHdr->payloadOffset);
              const uint16_t* payload = reinterpret_cast<const uint16_t*>(fexHdr+1);
              //  Print only the samples in this event's window
              unsigned nsamp=0;
              for(unsigned i=0; 
                  i<fexHdr->payloadSamples && // bounds of data buffer
                    nsamp<evHdr->rawSamples;  // bounds of valid data
                  i++) {
                if (payload[i] & (1<<15))  {
                  unsigned nskip = (payload[i]&0x7fff)+1;
                  if (nsamp < fexHdr->payloadOffset) {  // trim the early samples
                    if (nskip + nsamp <= fexHdr->payloadOffset) {
                      nsamp += nskip;
                      continue;
                    }
                    else
                      nskip -= fexHdr->payloadOffset - nsamp;
                  }
                  printf(" [%u]", (payload[i]&0x7fff)+1);
                  nsamp += (payload[i]&0x7fff)+1;
                }
                else {
                  if (nsamp >= fexHdr->payloadOffset)
                    printf(" %04x", payload[i]&0x7fff);
                  nsamp++;
                }
              }
              printf("\n");
              p = reinterpret_cast<const char*>(payload + fexHdr->payloadSamples);
            }
          }
        }
      }
    }
  }

  delete[] event;

  if (outfile)
    fclose(outfile);

  return 0;
}

Sim::Sim(unsigned nsamples) : 
  _seed(0), _nsamples(nsamples), _buff(new char[nsamples*2]) {}

Sim::~Sim() { delete[] _buff; }

const char* Sim::generate() {
  // Generate the samples
  uint16_t* s = reinterpret_cast<uint16_t*>(_buff);
  for(unsigned i=0; i<_nsamples; i++)
    *s++ = (_seed++)&0x3ff;
  return _buff;
}

size_t Sim::size() const { return _nsamples*sizeof(uint16_t); }

Fex::Fex() : _prescale(0), _iscale(0) {}

Fex* Fex::create(const char* args)
{
  Fex* result=0;
  char* fexargs;
  unsigned prescale = strtoul(&args[4],&fexargs,0);
  if (strncmp(args,"RAW",3)==0)
    result = new RawFex(fexargs+1);
  else if (strncmp(args,"THR",3)==0)
    result = new ThrFex(fexargs+1);

  if (result) {
    result->_prescale = prescale;
    return result;
  }

  return 0;
}

RawFex::RawFex(const char* args) {
  char* endptr;
  _first    = strtoul(args, &endptr, 0);
  _nsamples = strtoul(endptr+1, NULL, 0);
  printf("RawFex with first %u, nsamples %u\n", _first, _nsamples);
}

RawFex::~RawFex() {}

Fex* RawFex::clone() { return new RawFex(*this); }

char* RawFex::process(const char* begin,
                      const char* end,
                      char*       output) {
  bool lInclude = include();
  unsigned ufirst = _first>>3;
  unsigned ulast  = (_first+_nsamples-1)>>3;
  //  Insert 16 byte FEX header
  uint32_t* p = reinterpret_cast<uint32_t*>(output);
  if (!lInclude) {
    *p++ = 0;
    *p++ = ((Fex::RAW)<<16);
    *p++ = 0;
    *p++ = 0;
  }
  else {
    *p++ = (ulast-ufirst+1)<<3;
    *p++ = ((Fex::RAW)<<16) | (_first&0x7);
    *p++ = 0;
    *p++ = 0;
    const uint16_t* q = reinterpret_cast<const uint16_t*>(begin);
    for(unsigned i=ufirst; i<=ulast; i++, p+=4)
      memcpy(p, &q[i<<3], 16);
  }
  return reinterpret_cast<char*>(p);
}


ThrFex::ThrFex(const char* args) {
  char* endptr;
  _low  = strtoul(args, &endptr, 0);
  _high = strtoul(endptr+1, NULL, 0);
  printf("ThrFex with low %u, high %u\n", _low, _high);
}

ThrFex::~ThrFex() {}

Fex* ThrFex::clone() { return new ThrFex(*this); }

char* ThrFex::process(const char* begin,
                      const char* end,
                      char*       output) {
  bool lInclude = include();
  //  Insert 16 byte FEX header
  uint32_t* p = reinterpret_cast<uint32_t*>(output);
  if (!lInclude) {
    *p++ = 0;
    *p++ = ((Fex::THR)<<16);
    *p++ = 0;
    *p++ = 0;
    return reinterpret_cast<char*>(p);
  }

  {
    p++;
    *p++ = ((Fex::THR)<<16);
    *p++ = 0;
    *p++ = 0;
    const uint16_t* q = reinterpret_cast<const uint16_t*>(begin);
    const uint16_t* e = reinterpret_cast<const uint16_t*>(end);
    uint16_t* u = reinterpret_cast<uint16_t*>(p);
    unsigned nskip = 0;
    while(q < e) {
      if (*q<_low || *q>_high) {
        if (nskip) {
          *u++ = (1<<15) | (nskip-1);
          nskip = 0;
        }
        *u++ = *q;
      }
      else {
        if (++nskip==0x8000) {
          *u++ = (1<<15) | (nskip-1);
          nskip = 0;
        }
      }
      q++;
    }
    if (nskip)
      *u++ = (1<<15) | (nskip-1);

    unsigned sz = reinterpret_cast<char*>(u)-(output+16);
    sz = (sz+0xf)&~0xf;
    *reinterpret_cast<uint32_t*>(output) = sz>>1;
    return output+16+sz;
  }
}
