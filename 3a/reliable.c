
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

#define PACKET_DATA_MAX_SIZE 500
#define EOF_PACKET_SIZE 12

typedef struct queue queue;

 struct queue {
  	queue * next;
  	queue * prev;
  	packet_t *pkt;
  };

struct reliable_state {
  rel_t *next;			/* Linked list for traversing all connections */
  rel_t **prev;
  uint32_t SWS;
  uint32_t LAR;
  uint32_t LFS;   // increment this 
  uint32_t NFE;
  conn_t *c;			/* This is the connection object */
  queue * SendQ;
  queue * RecQ;
  /* Add your own data fields below this */

};
rel_t *rel_list;





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
  r-> SWS = cc-> window;
  r-> LAR= -1;
  r-> LFS = cc->window;
  r-> NFE = 0;

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
  free(r);
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
  while(s->LFS - s->LAR <= s->SWS){
    packet_t * newPacket = create_data_packet(s);
    if(newPacket == NULL)
      return;
    else if(NewPacket->len == EOF_PACKET_SIZE){//check for -1 aka EOF
      //Do something?
    }
    send_prepare(newPacket);
    conn_sendpkt(s->c, newPacket, (size_t)newPacket->len);
    queue * sent = xmalloc(sizeof(queue));
    sent->pkt = newPacket;
    if(s->sendQ == NULL){
      sendQ = sent;
      sendQ->next == NULL;
      sendQ->prev == NULL;
    }else{
      sendQ->prev = sent;
      sent->next = sendQ;
      sent->prev = NULL;
      sendQ = sent;
    }
    LFS++;
  }
}

packet_t * create_data_packet(rel_t * s){
  packet_t *packet;
  packet = xmalloc(sizeof(*packet));

  int bytes = conn_input(s->c, packet->data, PACKET_DATA_MAX_SIZE);
  if(bytes == 0){ //Nothing left to send so exit
    free(packet);
    return NULL;
  }

  if(bytes == -1){
    packet->len = EOF_PACKET_SIZE;
  }else{
    packet-> = EOF_PACKET_SIZE + bytes;
  }
  packet->ackno = (uint32_t) 1;
  packet->seqno=LFS+1;
  return packet;
}

void send_prepare(packet_t * packet){
  int length = packet->len;
  packet->ackno = htonl(packet->ackno);
  packet->seqno = htonl(packet->seqno);
  packet->len = htons(packet->len);
  packet->cksum = cksum(packet, length);
}

void
rel_output (rel_t *r)
{
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */

}
