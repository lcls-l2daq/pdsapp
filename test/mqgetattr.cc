#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <mqueue.h>

enum {PERMS = S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH};
enum {PERMS_IN  = S_IRUSR|S_IRGRP|S_IROTH};
enum {PERMS_OUT  = S_IWUSR|S_IWGRP|S_IWOTH};
enum {OFLAGS = O_RDONLY};

int main(int argc, char* argv[])
{
  char* qname = 0;
  bool lverbose = false;

  int c;
  while ( (c=getopt( argc, argv, "n:hv")) != EOF ) {
    switch(c) {
    case 'n': qname    = optarg; break;
    case 'v': lverbose   = true; break;
    default:
      break;
    }
  }

  
  struct mq_attr mqattr;
  mqd_t queue = mq_open(qname, O_RDONLY, PERMS_IN, &mqattr);
  if (queue == (mqd_t)-1) {
    perror("mq_open");
    return -1;
  }

  if (mq_getattr(queue, &mqattr)<0) {
    perror("mq_getattr");
    return -2;
  }

  printf("mqattr flags %zu maxmsg %zu msgsize %zu curmsgs %zu\n",
         mqattr.mq_flags,
         mqattr.mq_maxmsg,
         mqattr.mq_msgsize,
         mqattr.mq_curmsgs);

  char* msg = new char[mqattr.mq_msgsize];

  if (mq_receive(queue, msg, mqattr.mq_msgsize, NULL)<0) {
    perror("mq_receive");
    return -3;
  }

  for(unsigned i=0; i<mqattr.mq_msgsize; i++)
    printf(" %02x", msg[i]);
  printf("\n");

  return 0;
}
  
