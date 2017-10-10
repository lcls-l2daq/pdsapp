/**
 ** pgpdaq
 **
 **   Manage XPM and DTI to trigger and readout pgpcard (dev03)
 **
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
#include <pthread.h>
#include <signal.h>
#include <new>
#include "pds/cphw/Reg.hh"
using Pds::Cphw::Reg;

extern int optind;
bool _keepRunning = false;    // keep running on exit
unsigned _partn = 0;

static int row_, column_;

void usage(const char* p) {
  printf("Usage: %s [options]\n",p);
  printf("Options: -a <IP addr (dotted notation)> : Use network <IP>\n");
  printf("         -f <filename>                  : Data output file\n");
  printf("         -s                             : Sparse scan\n");
  printf("         -p <prescale>                  : Sample count prescale (2**(17+<prescale>))\n");
}

static inline unsigned getf(unsigned i, unsigned n, unsigned sh)
{
  unsigned v = i;
  return (v>>sh)&((1<<n)-1);
}

static inline unsigned getf(const Pds::Cphw::Reg& i, unsigned n, unsigned sh)
{
  unsigned v = i;
  return (v>>sh)&((1<<n)-1);
}

static inline unsigned setf(Pds::Cphw::Reg& o, unsigned v, unsigned n, unsigned sh)
{
  unsigned r = unsigned(o);
  unsigned q = r;
  q &= ~(((1<<n)-1)<<sh);
  q |= (v&((1<<n)-1))<<sh;
  o = q;
  return q;
}

class Dti {
private:  // only what's necessary here
  uint32_t  _reserved_90000004[(0x90000004)>>2];
  class UsLinkPgp {
  private:
    Reg _resetRx;
  public:
    void resetRx() {
      _resetRx = 1;
      usleep(10);
      _resetRx = 0;
    }
  } _usLinkPgp;

  uint32_t  _reserved_b0000000[(0xB0000000-0x90000008)>>2];
  class UsLinkScan {
  public:
    uint32_t _reserved_3c[0x3c];
    Reg      _es_control;  // [15:10] control, [9] errdet_en, [8], eye_scan_en, [4:0] prescale
    uint32_t _reserved_3f[0x3f-0x3d];
    Reg      _es_qualifier[5];
    Reg      _es_qual_mask[5];
    Reg      _es_sdata_mask[5];
    uint32_t _reserved_rf[0x4f-0x4e];
    Reg      _es_horz_offset; // [15:4]
    uint32_t _reserved_97[0x97-0x50];
    Reg      _rx_eyescan_vs; // [10] neg_dir, [9] ut_sign, [8:2] code (vert_offset), [1:0] range
    uint32_t _reserved_151[0x151-0x98];
    Reg      _es_error_count;         
    Reg      _es_sample_count;         
    Reg      _es_control_status;
    uint32_t _reserved_200 [0x200-0x154];
  public:
    void run(unsigned& error_count,
             unsigned long long& sample_count)
    {
      setf(_es_control, 1, 1, 10); // -> run
      while(1) {
        unsigned nwait=0;
        do {
          usleep(100);
          nwait++;
        } while(getf(_es_control_status,1,0)==0 and nwait < 1000);
        if (getf(_es_control_status,3,1)==2)
          break;
        //        printf("\tstate : %x\n", getf(_es_control_status,3,1));
      }
      error_count = _es_error_count;
      sample_count = _es_sample_count;
      sample_count <<= (1 + getf(_es_control,5,0));
    }            
  } _usLinkScan[7];

public:
  Dti() {}
  void scan(const char* ofile, unsigned prescale=0, bool lsparse=false)
  {
    FILE* f = fopen(ofile,"w");

    UsLinkScan& us = _usLinkScan[2];
    unsigned control = us._es_control;
    if ((control & (1<<8))==0) {
      printf("Enabling eyescan\n");
      setf(us._es_control, 1, 1, 8);  // eyescan_en
      _usLinkPgp.resetRx();
      usleep(100000);
    }

    unsigned status = us._es_control_status;
    printf("eyescan status: %04x\n",status);
    if ((status & 0xe) != 0) {
      printf("Forcing to WAIT state\n");
      setf(us._es_control, 0, 1, 10);
    }
    do {
      usleep(1);
    } while ( getf(us._es_control_status, 4, 0) != 1 );
    printf("WAIT state\n");
    
    setf(us._es_control, 1, 1, 9);  // errdet_en

    setf(us._es_control, prescale, 5, 0);

    setf(us._es_sdata_mask[0], 0xffff, 16, 0);
    setf(us._es_sdata_mask[1], 0xffff, 16, 0);
    setf(us._es_sdata_mask[2], 0xff00, 16, 0);
    setf(us._es_sdata_mask[3], 0x000f, 16, 0);
    setf(us._es_sdata_mask[4], 0xffff, 16, 0);
    for(unsigned i=0; i<5; i++)
      setf(us._es_qual_mask[i], 0xffff, 16, 0);

    setf(us._rx_eyescan_vs, 3, 2, 0); // range
    setf(us._rx_eyescan_vs, 0, 1, 9); // ut sign
    setf(us._rx_eyescan_vs, 0, 1, 10); // neg_dir
    setf(us._es_horz_offset, 0, 12, 4); // zero horz offset

    char stime[200];

    for(int j=-31; j<32; j++) {
      row_ = j;

      time_t t = time(NULL);
      struct tm* tmp = localtime(&t);
      if (tmp)
        strftime(stime, sizeof(stime), "%T", tmp);

      printf("es_horz_offset: %i [%s]\n",j, stime);
      setf(us._es_horz_offset, j, 12, 4);
      setf(us._rx_eyescan_vs, 0, 9, 2); // zero vert offset

      unsigned long long sample_count;
      unsigned error_count=-1, error_count_p=-1;

      for(int i=-1; i>=-127; i--) {
        column_ = i;
        setf(us._rx_eyescan_vs, i, 9, 2); // vert offset
        us.run(error_count,sample_count);

        fprintf(f, "%d %d %u %llu\n",
                j, i, 
                error_count,
                sample_count);
                
        setf(us._es_control, 0, 1, 10); // -> wait

        if (error_count==0 && error_count_p==0 && !lsparse) {
          //          printf("\t%i\n",i);
          break;
        }

        error_count_p=error_count;

        if (lsparse)
          i -= 19;
      }
      setf(us._rx_eyescan_vs, 0, 9, 2); // zero vert offset
      error_count_p = -1;
      for(int i=127; i>=0; i--) {
        column_ = i;
        setf(us._rx_eyescan_vs, i, 9, 2); // vert offset
        us.run(error_count,sample_count);

        fprintf(f, "%d %d %u %llu\n",
                j, i, 
                error_count,
                sample_count);
                
        setf(us._es_control, 0, 1, 10); // -> wait

        if (error_count==0 && error_count_p==0 && !lsparse) {
          //          printf("\t%i\n",i);
          break;
        }

        error_count_p=error_count;

        if (lsparse)
          i -= 19;
      }
      if (lsparse)
        j += 3;
    }
    fclose(f);
  }
};

static void* progress(void*)
{
  while(1) {
    sleep(60);
    printf("progress: %d,%d\n", row_, column_);
  }
  return 0;
}

int main(int argc, char** argv) {

  extern char* optarg;

  int c;

  const char* ip  = "10.0.1.103";
  unsigned prescale = 0;
  const char* outfile = "eyescan.dat";
  bool lsparse = false;

  while ( (c=getopt( argc, argv, "a:f:p:sh")) != EOF ) {
    switch(c) {
    case 'a': ip = optarg; break;
    case 'f': outfile = optarg; break;
    case 'p': prescale = strtoul(optarg,NULL,0); break;
    case 's': lsparse = true; break;
    case 'h': default:  usage(argv[0]); return 0;
    }
  }

  pthread_attr_t tattr;
  pthread_attr_init(&tattr);
  pthread_t tid;
  if (pthread_create(&tid, &tattr, &progress, 0))
    perror("Error creating progress thread");

  //  Setup DTI
  Pds::Cphw::Reg::set(ip, 8192, 0);
  Dti* dti = new (0)Dti;
  dti->scan(outfile,prescale,lsparse);

  return 0;
}
