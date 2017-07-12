
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <new>

#include "pds/quadadc/Globals.hh"
#include "pds/quadadc/Module.hh"

#include <string>

using namespace Pds::QuadAdc;

int main(int argc, char** argv) {

  char qadc='a';

  while(1) {
    char devname[16];
    sprintf(devname,"/dev/qadc%c",qadc);
    int fd = open(devname, O_RDWR);
    if (fd<0) {
      break;
    }
    printf("Opened %s\n",devname);

    void* ptr = mmap(0, sizeof(Pds::QuadAdc::Module), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
      perror("Failed to map");
      return -2;
    }
    
    Module* p = reinterpret_cast<Module*>(ptr);

    printf("BuildStamp: %s\n", 
           p->version.buildStamp().c_str());

    p->i2c_sw_control.select(I2cSwitch::LocalBus);
    printf("FMC A: %s present power %s\n",
           p->fmc_core.present() ? "":"not",
           p->fmc_core.powerGood() ? "up":"down");

    p->i2c_sw_control.select(I2cSwitch::LocalBus);  // ClkSynth is on local bus
    p->clksynth.setup();
    usleep(10000);

    p->tpr.setLCLS();
    p->tpr.resetRxPll();
    usleep(10000);
    p->tpr.resetRx();
    usleep(10000);
    p->tpr.resetCounts();
    usleep(10000);
    p->base.resetDma();

    p->i2c_sw_control.select(I2cSwitch::PrimaryFmc); 
    p->fmc_init();
    p->train_io();

    qadc++;
  }

  return 0;
}
