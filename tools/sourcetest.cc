#include "pds/collection/CollectionManager.hh"
#include "pds/collection/CollectionServer.hh"
#include "pds/utility/Transition.hh"
#include "pds/collection/CollectionPorts.hh"
#include "pds/collection/Route.hh"
#include "pds/utility/StreamPorts.hh"
#include "pds/service/Client.hh"
#include "pds/xtc/EvrDatagram.hh"
#include "pds/utility/Transition.hh"
#include "pds/service/Task.hh"

#include <time.h> // Required for timespec struct and nanosleep()
#include <stdlib.h> // Required for timespec struct and nanosleep()
#include <string.h>

using namespace Pds;

class EvrService: public CollectionManager {
public:
  EvrService(unsigned);
  virtual ~EvrService();

private:
  // Implements CollectionManager
  virtual void message(const Node& hdr, const Message& msg);

private:
  Client   _outlet;
  unsigned _evr;
};


static const unsigned MaxPayload = sizeof(Transition);
static const unsigned ConnectTimeOut = 500; // 1/2 second

EvrService::EvrService(unsigned platform) :
  CollectionManager(Level::Observer, platform, 
                    MaxPayload, ConnectTimeOut, NULL),
  _outlet(sizeof(EvrDatagram),0),
  _evr   (0)
{}

EvrService::~EvrService() 
{
}

void EvrService::message(const Node& hdr, const Message& msg) 
{
  if (hdr.level() == Level::Control) {
    if (msg.type() == Message::Transition) {
      const Transition& tr = reinterpret_cast<const Transition&>(msg);
      if (tr.id() == Transition::L1Accept &&
	  tr.phase() == Transition::Record) {
	EvrDatagram datagram(tr.sequence(), _evr++);
	Ins dst(StreamPorts::event(hdr.platform(),
				      Level::Segment));
	_outlet.send((char*)&datagram,0,0,dst);
	printf("EvrService::out %x:%08x/%08x to %x/%d\n",
	       datagram.evr,
	       datagram.seq.highAll(),datagram.seq.low(),
	       dst.address(),dst.portId());
      }
      else {
	_evr = 0;  // reset the sequence on any transition
      }
    }
  }
}

int main(int argc, char** argv)
{
  if (argc != 2) {
    printf("usage: %s <platform>\n", argv[0]);
    return 0;
  }

  char* end;
  unsigned platform = strtoul(argv[1], &end, 0);
  platform &= 0xff;
  EvrService evr(platform);

  evr.connect();

  Task* task = new Task(Task::MakeThisATask);
  task->mainLoop();

  return 0;
}
