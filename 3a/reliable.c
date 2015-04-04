
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include "rlib.h"

typedef struct queue queue;

 struct queue {
  	queue * next;
  	queue * prev;
  	packet_t *pkt;
  };

struct reliable_state {
  rel_t *next;			/* Linked list for traversing all connections */
  rel_t **prev;
  uint32_t LAR;
  uint32_t LFS;
  uint32_t NFE;
  conn_t *c;			/* This is the connection object */
  queue * SendQ;
  queue * RecQ;
  /* Add your own data fields below this */

};
rel_t *rel_list;


int acktoSend;


/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss,
	    const struct config_common *cc)
{
  rel_t *r;

  r = xmalloc (sizeof (*r));
  memset (r, 0, sizeof (*r));

  if (!c) {
    c = conn_create (r, ss);
    if (!c) {
      free (r);
      return NULL;
    }
  }

  r->c = c;
  r->next = rel_list;
  r->prev = &rel_list;
  if (rel_list)
    rel_list->prev = &r->next;
  rel_list = r;

  /* Do any other initialization you need here */


  return r;
}

void
rel_destroy (rel_t *r)
{
  if (r->next)
    r->next->prev = r->prev;
  *r->prev = r->next;
  conn_destroy (r->c);

  /* Free any other allocated memory here */
}


/* This function only gets called when the process is running as a
 * server and must handle connections from multiple clients.  You have
 * to look up the rel_t structure based on the address in the
 * sockaddr_storage passed in.  If this is a new connection (sequence
 * number 1), you will need to allocate a new conn_t using rel_create
 * ().  (Pass rel_create NULL for the conn_t, so it will know to
 * allocate a new connection.)
 */
void
rel_demux (const struct config_common *cc,
	   const struct sockaddr_storage *ss,
	   packet_t *pkt, size_t len)
{
}

void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
}


void
rel_read (rel_t *s)
{
}

void
rel_output (rel_t *r)
{
  conn_t * connection = r->c;
  queue * recvQ = r->RecQ;

  int ackno = -1;
  while(recvQ != NULL) {
    packet_t * packet = recvQ->pkt;
    int seqno = packet->seqno;
    if(r->NFE == seqno) {
      int remainingBufSpace = conn_bufspace(connection);
      char* data = packet->data;
      int dataSize = sizeof(data);
      if(dataSize <= remainingBufSpace) {
        conn_output(connection, data, dataSize);
        ackno = seqno + 1;
        r->NFE = ackno;
        r->RecQ = recvQ->next;
        recvQ = recvQ->next;
      }
      else {
        break;
      }
    }
    else { 
      break;
    }
    recvQ = recvQ->next;
  }

  if(ackno != -1) {
    struct ack_packet acknowledgementPacket;
    acknowledgementPacket.cksum = 0;
    int ackSize = sizeof(struct ack_packet);
    acknowledgementPacket.len = ackSize;
    acknowledgementPacket.ackno = ackno;
    uint16_t checkSum = cksum(&acknowledgementPacket, ackSize);
    acknowledgementPacket.cksum = checkSum;
    packet_t* convertedPtr = (packet_t*) &acknowledgementPacket;
    conn_sendpkt(connection, convertedPtr, ackSize);
  }
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */

}
