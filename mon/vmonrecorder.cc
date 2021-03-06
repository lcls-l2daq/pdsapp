#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

#include "pds/service/CmdLineTools.hh"
#include "pds/service/Semaphore.hh"
#include "pds/service/Task.hh"
#include "pds/service/Timer.hh"
#include "pds/mon/MonConsumerClient.hh"
#include "pds/management/VmonClientManager.hh"
#include "pds/vmon/VmonRecorder.hh"
#include "pds/mon/MonPort.hh"
#include "pds/utility/Transition.hh"

void printHelp(const char* program);

static const unsigned SUMCOUNT=60;

namespace Pds {
  class MyDriver : public MonConsumerClient,
		   public VmonClientManager,
		   public Timer {
  public:
    MyDriver(unsigned char platform,
	     const char*   partition,
             const char*   path) :
      VmonClientManager(platform, partition, *this),
      _recorder        (new VmonRecorder(path)),
      _recsum          (new VmonRecorder(path,"vmon_sum")),
      _task            (new Task(TaskObject("VmonTmr")))
    {
      VmonClientManager::start();
      if (!CollectionManager::connect()) {
	printf("platform %x unavailable\n",platform);
	exit(-1);
      }
      _recorder->enable();
      _recsum  ->enable();
      Timer::start();
    }
    ~MyDriver() { _recorder->disable(); _recsum->disable(); }

    Task*    task    () { return _task; }
    unsigned duration() const { return 1000; }
    void     expired() {
      _recorder->flush();
      _recsum  ->flush();
      if (++_sum >= SUMCOUNT)
        _sum=0;
      request_payload();
    }
    unsigned repetitive() const { return 1; }
  public:
    void post(const Transition& tr) {
      switch(tr.id()) {
      case TransitionId::BeginRun:
        { const unsigned MAX_RUNS=100000;
          unsigned env = tr.env().value();
          _recorder->begin(env < MAX_RUNS ? int(env) : -1);
          _recsum  ->begin(-1);
          _sum=-1;
        } break;
      case TransitionId::EndRun:
        _recorder->end();
        break;
      case TransitionId::Unmap:
        _recsum  ->end();
        break;
      default:
        break;
      }
      VmonClientManager::post(tr);
    }
  private:
    // Implements MonConsumerClient
    void process(MonClient& client, MonConsumerClient::Type type, int result=0) {
      if (type==MonConsumerClient::Description) {
        _recorder->description(client);
        _recsum  ->description(client);
      }
      else if (type==MonConsumerClient::Payload    ) {
        _recorder->payload    (client);
        if (_sum==0)
          _recsum->payload    (client);
      }
    }
    void add(Pds::MonClient&) {}
  private:
    VmonRecorder* _recorder;
    VmonRecorder* _recsum;
    Task*         _task;
    unsigned      _sum;
  };
};

using namespace Pds;

static MyDriver* driver;

static struct sigaction old_actions[64];

void sigintHandler(int iSignal)
{
  printf("vmonrecorder stopped by signal %d\n",iSignal);
  delete driver;
  sigaction(iSignal,&old_actions[iSignal],NULL);
  raise(iSignal);
}

int main(int argc, char **argv) 
{
  const unsigned NO_PLATFORM = (unsigned)-1;
  unsigned platform = NO_PLATFORM;
  const char* partition = 0;
  const char* path = ".";
  bool parseValid = true;

  int c;
  while ((c = getopt(argc, argv, "p:P:o:")) != -1) {
    switch (c) {
    case 'p':
      parseValid &= CmdLineTools::parseUInt(optarg,platform);
      break;
    case 'P':
      partition = optarg;
      break;
    case 'o':
      path = optarg;
      break;
    default:
      printHelp(argv[0]);
      return 0;
    }
  }

  parseValid &= (optind==argc);

  if (!parseValid || partition==0 || platform==NO_PLATFORM) {
    printHelp(argv[0]);
    return -1;
  }

  driver = new MyDriver(platform,
			partition,
                        path);

  // Unix signal support
  struct sigaction int_action;

  int_action.sa_handler = sigintHandler;
  sigemptyset(&int_action.sa_mask);
  int_action.sa_flags = 0;
  int_action.sa_flags |= SA_RESTART;

#define setup_sig(sig) {                                        \
    if (sigaction(sig, &int_action, &old_actions[sig]) > 0)     \
      printf("Couldn't set up #sig handler\n");                 \
  }

  setup_sig(SIGINT);
  setup_sig(SIGKILL);
  setup_sig(SIGSEGV);
  setup_sig(SIGABRT);
  setup_sig(SIGTERM);

  Semaphore sem(Semaphore::EMPTY);
  sem.take();

  sigintHandler(SIGINT);
  return 0;
}


void printHelp(const char* program)
{
  printf("usage: %s [-p <platform>] [-P <partition name>] [-o <output path>]\n", program);
}
