#include "pds/service/CmdLineTools.hh"

#include "usdusb4/include/libusdusb4.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <semaphore.h>

#include "libusb.h"

#include <list>

extern int optind;

static int reset_usb()
{
  int n = 0;

  libusb_context* pctx;

  libusb_init(&pctx);

  const int vid = 0x09c9;
  const int pid = 0x0044;

  libusb_device_handle* phdl = libusb_open_device_with_vid_pid(pctx, vid, pid);
  if (phdl) {
    libusb_reset_device(phdl);
    libusb_close(phdl);
    n = 1;
  }

  libusb_exit(pctx);

  return n;
}

static void close_usb(int isig)
{
  printf("close_usb %d\n",isig);
  //  USB4_Shutdown();
  const char* nsem = "Usb4-0000";
  printf("Unlinking semaphore %s\n",nsem);
  if (sem_unlink(nsem))
    perror("Error unlinking usb4 semaphore");
  exit(0);
}

class UsdUsbData {
public:
  enum { Encoder_Inputs = 4 };
  enum { Analog_Inputs = 4 };
  enum { Digital_Inputs = 8 };
  enum { NCHANNELS = 4 };
  UsdUsbData() {}
  ~UsdUsbData() {}
public:
  void operator delete(void*) {}
public:
  uint8_t	_header[6];
  uint8_t	_din;
  uint8_t	_estop;
  uint32_t	_timestamp;
  uint32_t	_count[Encoder_Inputs];
  uint8_t	_status[4];
  uint16_t	_ain[Analog_Inputs];
};

using namespace Pds;

static void usage(const char* p)
{
  printf("Usage: %s [OPTIONS]\n"
         "\n"
         "Options:\n"
         "    -z                          zeroes encoder counts\n"
         "    -t                          disable testing time step check\n"
         "    -h                          print this message and exit\n", p);
}

int main(int argc, char** argv) {

  // parse the command line for our boot parameters
  bool lzero = false;
  bool lUsage = false;
  bool tsc = true;

  extern char* optarg;
  int c;
  while ( (c=getopt( argc, argv, "zth")) != EOF ) {
    switch(c) {
    case 'z':
      lzero = true;
      break;
    case 't':
      tsc = false;
      break;
    case '?':
    default:
      lUsage = true;
      break;
    }
  }

  if (optind < argc) {
    printf("%s: invalid argument -- %s\n",argv[0], argv[optind]);
    lUsage = true;
  }

  if (lUsage) {
    usage(argv[0]);
    return 1;
  }

   printf("UsdUsb is %sabling testing time step check\n", tsc ? "en" : "dis");

  //
  //  There must be a way to detect multiple instruments, but I don't know it yet
  //
  reset_usb();

  short deviceCount = 0;
  printf("Initializing device\n");
  int result = USB4_Initialize(&deviceCount);
  if (result != USB4_SUCCESS) {
    printf("Failed to initialize USB4 driver (%d)\n",result);
    close_usb(0);
    return 1;
  }

  //
  //  Need to shutdown the USB driver properly
  //
  struct sigaction int_action;

  int_action.sa_handler = close_usb;
  sigemptyset(&int_action.sa_mask);
  int_action.sa_flags = 0;
  int_action.sa_flags |= SA_RESTART;

  if (sigaction(SIGINT, &int_action, 0) > 0)
    printf("Couldn't set up SIGINT handler\n");
  if (sigaction(SIGKILL, &int_action, 0) > 0)
    printf("Couldn't set up SIGKILL handler\n");
  if (sigaction(SIGSEGV, &int_action, 0) > 0)
    printf("Couldn't set up SIGSEGV handler\n");
  if (sigaction(SIGABRT, &int_action, 0) > 0)
    printf("Couldn't set up SIGABRT handler\n");
  if (sigaction(SIGTERM, &int_action, 0) > 0)
    printf("Couldn't set up SIGTERM handler\n");

  printf("Found %d devices\n", deviceCount);

  if (lzero) {
    for(unsigned i=0; i<UsdUsbData::NCHANNELS; i++) {
      if ((result = USB4_SetPresetValue(deviceCount, i, 0)) != USB4_SUCCESS)
	printf("Failed to set preset value for channel %d : %d\n",i, result);
      if ((result = USB4_ResetCount(deviceCount, i)) != USB4_SUCCESS)
	printf("Failed to set preset value for channel %d : %d\n",i, result);
    }
    close_usb(0);
    return 1;
  }

  unsigned    _dev = 0;

  //
  //  Configure
  //
  {
#ifdef DBUG
#define USBDBUG1(func,arg0) {                   \
      printf("%s: %d\n",#func,arg0);            \
      USB4_##func(arg0); }

#define USBDBUG2(func,arg0,arg1) {              \
      printf("%s: %d %d\n",#func,arg0,arg1);    \
      USB4_##func(arg0,arg1); }

#define USBDBUG3(func,arg0,arg1,arg2) {                         \
      printf("%s: %d %d %d\n",#func,arg0,arg1,arg2);            \
      _nerror += USB4_##func(arg0,arg1,arg2)!=USB4_SUCCESS; }
#else
#define USBDBUG1(func,arg0) USB4_##func(arg0)
#define USBDBUG2(func,arg0,arg1) USB4_##func(arg0,arg1)
#define USBDBUG3(func,arg0,arg1,arg2) _nerror += USB4_##func(arg0,arg1,arg2)!=USB4_SUCCESS
#endif

    unsigned _nerror = 0;

    for(unsigned i=0; i<UsdUsbData::NCHANNELS; i++) {
      //      USBDBUG3( SetMultiplier ,_dev, i, (int)_config.quadrature_mode()[i]);
      //      USBDBUG3( SetCounterMode,_dev, i, (int)_config.counting_mode  ()[i]);
      USBDBUG3( SetMultiplier ,_dev, i, 0);
      USBDBUG3( SetCounterMode,_dev, i, 0);
      USBDBUG3( SetForward    ,_dev, i, 0); // A/B assignment (normal)
      USBDBUG3( SetCaptureEnabled,_dev, i, 1);
      USBDBUG3( SetCounterEnabled,_dev, i, 1);
    }

    // Clear the FIFO buffer
    USBDBUG1(ClearFIFOBuffer,_dev);

    // Enable the FIFO buffer
    USBDBUG1(EnableFIFOBuffer,_dev);

    // Clear the captured status register
    USBDBUG2(ClearCapturedStatus,_dev, 0);

    static int DIN_CFG[] = { 0, 0, 0, 0, 0, 0, 0, 1 };
    USB4_SetDigitalInputTriggerConfig(_dev, DIN_CFG, DIN_CFG);

    USBDBUG1(ClearDigitalInputTriggerStatus,_dev);

    printf("Configuration Done\n");

  }

  //
  //  Start the reader loop
  //
  enum { MAX_RECORDS = 1024 };
  USB4_FIFOBufferRecord _records[MAX_RECORDS];
  unsigned timestamp_sav=0;

  while(1) {
    int nRecords = MAX_RECORDS;
    const int tmo = 100; // milliseconds
    int status = USB4_ReadFIFOBufferStruct(_dev, 
                                           &nRecords,
                                           _records,
                                           tmo);
    if (status != USB4_SUCCESS) {
      printf("ReadFIFO result %d\n",status);
    }
    else {
      for(unsigned i=0; i<nRecords; i++) {
        UsdUsbData* d = new (&_records[i]) UsdUsbData;
        // timestamp increments at 48 MHz
        printf("timestamp: %u [%f us]\n", d->_timestamp, double(unsigned(d->_timestamp-timestamp_sav))/48.);
        timestamp_sav = d->_timestamp;
        for(unsigned i=0; i<4; i++)
          printf("\tencoder_count: %u\n", d->_count[i]);
        for(unsigned i=0; i<4; i++)
          printf("\tanalog_in: %u\n", d->_ain[i]);
        printf("\tdigital_in: 0x%x\n", d->_din);
        delete d;
      }
    }
  }

  return 0;
}
