
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
typedef struct timeval timeval;
 struct queue {
  	queue * next;
  	queue * prev;
  	packet_t *pkt;
    timeval * transitionTime;
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
  int sentEOF;
  int recvEOF;
  int timeout;
  //timeval EOFsentTime; 
  /* Add your own data fields below this */

};
rel_t *rel_list;

//Helper function declarations
void send_prepare(packet_t * packet);
packet_t * create_data_packet(rel_t * s);
int check_close(rel_t * s);
int timeval_subtract(timeval * result, timeval * x, timeval * y);
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
  r-> SWS = cc-> window;
  r-> LAR= 0;
  r-> LFS = cc->window;
  r-> NFE = 0;
  r->sentEOF = 0;
  r->recvEOF = 0;
  //r->EOFsentTime = -1;
  r -> timeout = cc -> timeout;

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

int check_close(rel_t * s){ //Still need to check for time condition!
  timeval *currentTime = malloc(sizeof(timeval));
  gettimeofday(currentTime,NULL);
  //int timeSinceEOF = difference of EOFsentTime and currentTime as an int
  if(s->SendQ == NULL && s->RecQ == NULL && s->sentEOF == 1 && s->recvEOF == 1) //&& timeSinceEOF >=2*s->timeout
    return 1;
  return 0;
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
  uint16_t sum = pkt-> cksum;
  uint16_t len = pkt->len; 
  pkt-> cksum = 0;
  if(cksum(pkt, len)!= sum){
    return;
  }
  pkt->cksum = sum;
  if(len == EOF_PACKET_SIZE){ //Check for closing conditions, need to change this to account for timer condition
    if(check_close(r) == 1){
      rel_destroy(r);
      return;
    }
  }
  if(len == 8){
    uint32_t ackno = pkt -> ackno;
    queue * head = r-> SendQ;
    if(head == NULL){
      if(check_close(r) == 1){
        rel_destroy(r);
        return;
      }
      return;
    }
    queue * current = head;
    while(current != NULL){
      if(current->pkt->seqno < ackno){
        queue * temp = current -> prev;
        if(temp != NULL){
          temp -> next = current -> next;
        }else{
          head = current -> next;
          r -> SendQ = head;
        }
        current -> next -> prev = temp;
        temp = current;
        current = current -> next;
        free(temp);
        r-> LAR = r-> LAR + 1;    
      }else{
        current = current -> next;
      }
    }
    if(head == NULL){
      if(check_close(r) == 1){
        rel_destroy(r);
        return;
      }
    }
    
    return;
  }else if(len > 8){
    uint32_t seqno = pkt -> seqno;
    if(seqno < r->NFE && seqno >= r->NFE + r->SWS){
      return;
    }
    queue * newpkt = (queue * )malloc(sizeof(struct queue));
    newpkt -> pkt = pkt;
    queue * head = r-> RecQ;
    if(head == NULL){
      r->RecQ = newpkt;
    }else{
      queue * current = head;
      while(1){
        if(current -> pkt -> seqno == seqno){
          break;
        }
        if(current -> pkt -> seqno < seqno){
          current = current -> next;
          if(current -> next == NULL){
            current -> next = newpkt;
            break;
          }
        }else{
          queue * temp = current -> prev;
          if(temp != NULL){
            temp -> next = newpkt;
          } else{
            r->RecQ = newpkt;
          }
          current -> next -> prev = newpkt;
          newpkt -> next = current;
          break;
        }
      }
    }

  }

}


void
rel_read (rel_t *s)
{
  while(s->LFS - s->LAR <= s->SWS){
    packet_t * newPacket = create_data_packet(s);
    if(newPacket == NULL)
      return;
    else if(newPacket->len == EOF_PACKET_SIZE){//check for -1 aka EOF
      s->sentEOF =1;
      if(check_close(s) == 1){
        rel_destroy(s);
        return;
      }
    }
    send_prepare(newPacket);
    conn_sendpkt(s->c, newPacket, (size_t)newPacket->len);
    queue * sent = xmalloc(sizeof(queue));    
    sent->pkt = newPacket;
    if(s->SendQ == NULL){
      s->SendQ = sent;
      s->SendQ->next = NULL;
      s->SendQ->prev = NULL;
    }else{
      s->SendQ->prev = sent;
      sent->next = s->SendQ;
      sent->prev = NULL;
      s->SendQ = sent;
    }
    gettimeofday(s->SendQ->transitionTime,NULL);
    if(s->sentEOF == 1)
      //gettimeofday(&(s->EOFsentTime,NULL));        
    s->LFS++;
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
    packet->len = EOF_PACKET_SIZE + bytes;
  }
  packet->ackno = (uint32_t) 1;
  packet->seqno=s->LFS+1;
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
        free(packet);
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
  if(check_close(r) == 1){
    rel_destroy(r);
    return;
  }

  if(ackno != -1) {
    packet_t * acknowledgementPacket = (packet_t *) malloc(8);
    acknowledgementPacket->cksum = 0;
    uint16_t ackSize = 8;
    acknowledgementPacket->len = ackSize;
    acknowledgementPacket->ackno = ackno;
    uint16_t checkSum = cksum(acknowledgementPacket, ackSize);
    acknowledgementPacket->cksum = checkSum;
    conn_sendpkt(connection, acknowledgementPacket, ackSize);
  }
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */
  rel_t * r = rel_list;
  queue * head = r -> SendQ;
  queue * current = head; 
  timeval * t = malloc(sizeof(struct timeval));
  timeval * diff = malloc(sizeof(struct timeval));
  gettimeofday(t, NULL);
  while(current!=NULL){
    timeval_subtract(diff, t, current -> transitionTime);
    int timediff = (diff->tv_sec + diff->tv_usec/1000000)/1000;
    if(timediff > r->timeout){
        gettimeofday(current -> transitionTime, NULL);
        conn_sendpkt(r->c, current -> pkt, (size_t)current->pkt -> len);
    }
  }
  free(t);
  free(diff);
}

int
timeval_subtract (result, x, y)
     struct timeval *result, *x, *y;
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

