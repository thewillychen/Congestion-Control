
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

  typedef struct sentPacket sentPacket;

  struct sentPacket {
    packet_t *pkt;
    timeval * transmissionTime;
    int valid;
    int timeCount;
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
  int EOFsentTime; 
  int prevPacketFull;
  //queue * SendQend;
  sentPacket * sentPackets;
  /* Add your own data fields below this */

};
rel_t *rel_list;

//Helper function declarations
void send_prepare(packet_t * packet);
void read_prepare(packet_t * packet);
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
  fprintf(stderr, "create\n");
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
  if (rel_list){
    rel_list->prev = &r->next;
    fprintf(stderr, "prev %p\n", rel_list->prev);
  }
  rel_list = r;
  r-> SWS = cc-> window;
  r-> LAR= 0;
  r-> LFS = 0;
  r-> NFE = 1;
  r->sentEOF = 0;
  r->recvEOF = 0;
  r->sentPackets =(sentPacket*) malloc(sizeof(sentPacket)*r->SWS);
  r->EOFsentTime = 0;

  r -> timeout = cc -> timeout;
  r->prevPacketFull = 0;

  /* Do any other initialization you need here */

  fprintf(stderr, "createexit\n");
  return r;
}

void
rel_destroy (rel_t *r)
{
  fprintf(stderr, "destroyed %p\n", r);
  if (r->next)
     r->next->prev = r->prev;
   *r->prev = r->next;
  conn_destroy (r->c);
  free(r);
  /* Free any other allocated memory here */
}

int check_close(rel_t * s){ //Still need to check for time condition!
  int timeSinceEOF =0;
  if(s->sentEOF > 0){
    // timeval *currentTime = malloc(sizeof(timeval));
    // timeval *diff = malloc(sizeof(timeval));
    // gettimeofday(currentTime,NULL);
    // timeval_subtract(diff, currentTime, s-> EOFsentTime);
    // timeSinceEOF = (diff->tv_sec + diff->tv_usec/1000000)/1000;
    // free(currentTime);
    // free(diff);
  }
  //int timeSinceEOF = difference of EOFsentTime and currentTime as an int
  if(s->SendQ == NULL && s->RecQ == NULL && s->sentEOF == 1 && s->recvEOF == 1 && timeSinceEOF >=2*s->timeout)
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
 //printf("does this print");
  // FILE * output = fopen("output.txt", "a");
  // fprintf(output, "relrecv %d", getpid());
  // fclose(output);
  r = rel_list;
  uint16_t sum = pkt-> cksum;
  uint16_t len = ntohs(pkt->len); 
  pkt-> cksum = 0;
  if(cksum(pkt, len)!= sum){
    fprintf(stderr, "cksum fail\n");
    return;
  }
  read_prepare(pkt);
//  output = fopen("output.txt", "a");
//  fprintf(output, "recvpkt len %d  \n", len);
 // fclose(output);
  pkt->cksum = sum;
  if(len == EOF_PACKET_SIZE){ //Check for closing conditions, need to change this to account for timer condition
  // fprintf(stderr, "is this getting called bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb \n");
    if(check_close(r) == 1){
         //  FILE * output = fopen("output.txt", "a");
         //  fprintf(output, "closed %d \n", pkt->ackno);
         // fclose(output);
    fprintf(stderr, "is this getting called bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb \n");
      rel_destroy(r);
  //    fprintf(output, "eof rec\n");
      return;
    }
    return;
  }
//  fclose(output);
  if(len == 8){
    //FILE* output = fopen("output.txt", "a");
   // fprintf(output, "Ack Received: %d\n", 1);
    int ackno = pkt->ackno;
    int i;
    for(i = 0; i<r->SWS; i++){
      if(r->sentPackets[i].valid == 1 && r->sentPackets[i].pkt->seqno < ackno){
        r->sentPackets[i].valid = -1;
    //    fprintf(output, "Incrementing LAR: %d\n", r->LAR);
        //r->LAR = (r->LAR)+ 1;
   
      //  fprintf(output, "Incremented LAR: %d\n", r->LAR);
        //break;
      }
     
     // fprintf(output, "%d seqno %d \n", r->sentPackets[i].valid, r->sentPackets[i].pkt->seqno);
    }
    r->LAR = ackno -1;
    // for(i=0; i <r->SWS; i++){
    //   if(r->sentPackets[i].valid == 1 && r->sentPackets[i].pkt->seqno < ackno){
    //     r->sentPackets[i].valid = -1;
    //   }
    // }
    //fclose(output);
    rel_read(r);
  // FILE * output = fopen("output.txt", "a");
  // fprintf(output, "ack rec %d  \n", ackno);
  // fclose(output);
  }else if(len > 8){
    // FILE * output = fopen("output.txt", "a");
    // fprintf(output, "seqno %d NFE %d SWS %d\n", pkt->seqno, r->NFE, r->SWS);
    // fclose(output);
    uint32_t seqno = pkt -> seqno;
    if(seqno < r->NFE || seqno >= r->NFE + r->SWS){
      return;
    }
    queue * newpkt = (queue * )malloc(sizeof(struct queue));
    newpkt -> pkt = pkt;
    newpkt->next = NULL;
    newpkt->prev = NULL;
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
          if(current -> next == NULL){
            current -> next = newpkt;
            break;
          }
            current = current -> next;
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

      rel_output(r);
      fprintf(stderr, "rec data to out seq %d NFE %d\n", seqno, r->NFE);
  }
  // FILE * output = fopen("output.txt", "a");
  // fprintf(output, "relrecv %d", getpid());
  // fclose(output);
}


void
rel_read (rel_t *s)
{
  //s = rel_list;
  //fprintf(stderr, "rel read s pointer: %p , rel_list point : %p\n",s,rel_list );
  fprintf(stderr,"Last frame sent: %d, Last ack Recv: %d, sws: %d\n", s->LFS, s->LAR, s->SWS);
  if(s->LFS - s->LAR <= s->SWS){ //&& s->prevPacketFull != 1){
    fprintf(stderr, "rel read in while, %p\n",s );
    packet_t * newPacket = create_data_packet(s);
    if(newPacket == NULL){
      fprintf(stderr, "newPacket is null\n");
      return;
    }else if(newPacket->len == EOF_PACKET_SIZE){//check for -1 aka EOF
      s->sentEOF =1;
   //   fprintf(stderr, "is this getting called aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \n");
      if(check_close(s) == 1){
     //   fprintf(stderr, "is this getting called aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \n");
        rel_destroy(s);
        return;
      }
    }
    int length = newPacket->len;
    fprintf(stderr,"Sent packet: %s\n", newPacket->data);
    send_prepare(newPacket);
    conn_sendpkt(s->c, newPacket, (size_t)length);
    read_prepare(newPacket);
    int i;
    for(i =0; i < s->SWS; i++){

      if(s->sentPackets[i].valid < 1){
        s->sentPackets[i].valid = 1;
        //gettimeofday(s->sentPackets[i].transmissionTime,NULL);
        s->sentPackets[i].timeCount = 0;

        //memcpy(s->sentPackets[i].pkt, &newPacket, length);
        s->sentPackets[i].pkt = newPacket;
        // FILE * output = fopen("output.txt", "a");
        // fprintf(output, "relread");
        // fclose(output);
        
        break;
      }

    }

    //queue *sent = malloc(sizeof(queue));    
    //free(sent);
    // if(s->sentEOF == 1)
    //   gettimeofday(s->EOFsentTime,NULL);        
    s->LFS++;
  }

  //s->prevPacketFull =0;
  fprintf(stderr, "rel read done\n");
}

packet_t * create_data_packet(rel_t * s){
  packet_t *packet;
  packet = (packet_t*)malloc(sizeof(packet_t));
  fprintf(stderr, "Creating packets %p, %p\n", s, packet->data);
  int bytes = conn_input(s->c, packet->data, PACKET_DATA_MAX_SIZE);
  //fprintf(stderr, "conn input didnt segfault%p, %p, %d\n", s, packet->data, bytes);

  if(bytes == 0){ //Nothing left to send so exit
    free(packet);
    return NULL;
  }

  if(bytes == -1){
    packet->len = EOF_PACKET_SIZE;
  }else{
    packet->len = EOF_PACKET_SIZE + bytes;
    if(packet->len <(PACKET_DATA_MAX_SIZE+EOF_PACKET_SIZE)){
    s->prevPacketFull = 1;
    fprintf(stderr, "reaches end of input");
    }
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

void read_prepare(packet_t * packet){
  packet->ackno = ntohl(packet->ackno);
  packet->seqno = ntohl(packet->seqno);
  packet->len = ntohs(packet->len);

}
void
rel_output (rel_t *r)
{
  conn_t * connection = r->c;
  queue * recvQ = r->RecQ;
        //   FILE * output = fopen("output.txt", "a");
        // fprintf(output, "output reached");
        // fclose(output);

  int ackno = -1;
  while(recvQ != NULL) {
    packet_t * packet = recvQ->pkt;
    int seqno = packet->seqno;
    if(r->NFE == seqno) {
      int remainingBufSpace = conn_bufspace(connection);
      char* data = packet->data;
      int dataSize = packet->len - 12;
      if(dataSize <= remainingBufSpace) {
        // FILE * output = fopen("output.txt", "a");
        // fprintf(output, "output seqno: %d NFE: %d \n",packet -> seqno, r->NFE);
        // fclose(output);
        conn_output(connection, data, dataSize);
        ackno = seqno + 1;
        r->NFE = ackno;
        queue * prev = r->RecQ;
        r->RecQ = recvQ->next;
        if(r->RecQ != NULL){
          r->RecQ->prev = NULL;
        }
        free(prev);
      }
      else {
        break;
      }
    }
    else { 
      break;
    } 
    recvQ = r->RecQ;
  }
  // if(check_close(r) == 1){
  //   rel_destroy(r);
  //   return;
  // }

  if(ackno != -1) {
    packet_t * acknowledgementPacket = (packet_t *) malloc(8);
    acknowledgementPacket->cksum = 0;
    uint16_t ackSize = 8;
    acknowledgementPacket->len = htons(ackSize);
    acknowledgementPacket->ackno = htonl(ackno);
    uint16_t checkSum = cksum(acknowledgementPacket, ackSize);
    acknowledgementPacket->cksum = checkSum;
    conn_sendpkt(connection, acknowledgementPacket, ackSize);
    free(acknowledgementPacket);
  }
}

void
rel_timer ()
{
  /* Retransmit any packets that need to be retransmitted */
  rel_t * r = rel_list;

  // timeval * t = malloc(sizeof(struct timeval));
  // timeval * diff = malloc(sizeof(struct timeval));
  // gettimeofday(t, NULL);
  int i;
  for(i = 0; i < r->SWS; i++){
    //timeval_subtract(diff, t, r->sentPackets[i].transmissionTime);
    //int timediff = (diff->tv_sec + diff->tv_usec/1000000)/1000;
    //fprintf(stderr, "%d\n", timediff);
    //if(timediff > r->timeout && r->sentPackets[i].valid ==1){
    if(r->sentPackets[i].valid == 1) {
        if(r->sentPackets[i].pkt-> len == EOF_PACKET_SIZE){
          r->EOFsentTime = r->EOFsentTime +1;
        }
        if(r->sentPackets[i].timeCount >= 5) {
          //gettimeofday(r->sentPackets[i].transmissionTime, NULL);
          r->sentPackets[i].timeCount = 0;
          int len = r->sentPackets[i].pkt->len;
          r->sentPackets[i].pkt->ackno = htonl(r->sentPackets[i].pkt->ackno);
          r->sentPackets[i].pkt->seqno = htonl(r->sentPackets[i].pkt->seqno);
          r->sentPackets[i].pkt->len = htons(r->sentPackets[i].pkt->len);
          
          conn_sendpkt(r->c, r->sentPackets[i].pkt, (size_t)len);
          read_prepare(r->sentPackets[i].pkt);
        }
        else{
          r->sentPackets[i].timeCount = r->sentPackets[i].timeCount + 1;
        }
    }

    
  }
  //free(t);
  //free(diff);
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

