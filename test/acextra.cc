#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h>

static void rangauss2(double& x, double& y) {
  double v = double(random())/double(RAND_MAX);
  double r = sqrt(-2*log(v));
  double f = double(random())/double(RAND_MAX)*2*M_PI;
  x = r*cos(f);
  y = r*sin(f);
}

class Adc {
public:
  Adc(unsigned n, double sigma) : 
    _n(n), 
    _sigma(sigma),
    _dy   (new double[n]),
    _de   (new double [n]),
    _dsumy(0),
    _dsumiy(0),
    _y    (new int16_t[n]),
    _yo   (new double [n]),
    _ye   (new int32_t[n]),
    _idx  (0),
    _sumy (0),
    _sumiy(0),
    _nt   (0),
    _sumt (0),
    _sumtt(0)
  {
    _f = 0;
    if ((n&1)==0) {
      for(unsigned i=0; i<(n>>1); i++) {
        int y = (i<<1)+1;
        _f += y*y;
      }
    }
    else {
      for(unsigned i=0; i<(n>>1); i++) {
        int y = (i+1)<<1;
        _f += y*y;
      }
    }

    _di0 = 0.5*double(n-1);
    _dii = _f*0.5;

    printf("Phase  Ampl  Err  SumV  SumIV   Meas  Extr\n");
  }
public:
  void update(double v, double err, double phase) 
  {
    double dy = v + err*_sigma;

    _dsumy  -= _dy[_idx];

    _dsumiy -= _dsumy;
    _dsumiy += (dy+_dy[_idx])*_di0;

    _dsumy  += dy;
    _dy[_idx] = dy;

    double de = _dsumiy * (_di0 + 7) / _dii +
      _dsumy / double(_n);

    printf("%f  %f  %f  %f  %f  %f  %f\n",
           phase, v, err,
           _dsumy, _dsumiy, dy, de);

    /*
    int16_t y = int(v + err*sigma)<<2;
    _sumiy -= _sumy;
    _sumiy += (y*_n)>>1;
    _sumy  += y - _y[idx];
    _y[idx] = y;
    */
    _idx++;
    if (_idx==_n) _idx=0;
  }
private:
  unsigned _n;      // size of data averaging
  double   _sigma;  // spread in measurement
  double*  _dy;
  double*  _de;
  double   _di0;    // i of latest point
  double   _dsumy;
  double   _dsumiy;
  double   _dii;    // sum of i**2

  int16_t* _y;      // history of measurements
  double*  _yo;     // history of ideals
  int32_t* _ye;     // history of extrapolations
  unsigned _idx;    // index in circular buffers (histories)
  int64_t  _sumy;   // iterative sum of (y_i)
  int64_t  _sumiy;  // iterative sum of (i*y_i)
  int32_t  _f;      //
  unsigned _nt;     // number of zero crossings found
  double   _sumt;   // first moment
  double   _sumtt;  // second moment
};
      
static void show_usage(const char* p)
{
  printf("Usage: %s -a <Server IP address: dotted notation> [-v]\n",p);
}

void sigHandler( int signal ) {
  ::exit(signal);
}

void* statusThread(void*);
  
int main(int argc, char* argv[])
{
  unsigned    nacc  = 40;
  unsigned    sigma = 16.;
  double      ampl  = 1024.;
  unsigned    N = 200;

  int c;
  while ( (c=getopt( argc, argv, "a:n:N:s:hv")) != EOF ) {
    switch(c) {
    case 'a':
      ampl = strtod(optarg,NULL);
      break;
    case 'n':
      nacc = strtoul(optarg,NULL,0);
      break;
    case 'N':
      N = strtoul(optarg,NULL,0);
      break;
    case 's':
      sigma = strtod(optarg,NULL);
      break;
    case 'h':
      show_usage(argv[0]);
      return 0;
    default:
      printf("Unknown option: -%c\n",c);
      return -1;
    }
  }

  Adc adc(nacc,sigma);

  double z[2];
  for(unsigned i=0; i<N; i++) {
    double f = double(i)*2*M_PI*360*14./1.e6;
    if ((i&1)==0)
      rangauss2(z[0],z[1]);
    else
      z[0]=z[1];
    double v = ampl*cos(f);
    adc.update(v,z[0],f);
  }

  return 0;
}
