
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

#define PACKET_DATA_MAX_SIZE 1000
#define EOF_PACKET_SIZE 16
#define ACK_PACKET_SIZE 12
#define MAX_PACKET_SIZE 1016
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
  rel_t *next;      /* Linked list for traversing all connections */
  rel_t **prev;
  uint32_t SWS;   //Sliding Window Size - sender
  uint32_t LAR;   //Last Ack Received - sender
  uint32_t LFS;   //Last Frame Sent - sender
  uint32_t NFE;   //Next Frame Expected - receiver
  uint32_t LFR;   //Last Frame Read - receiver
  conn_t *c;      /* This is the connection object */
  queue * SendQ;
  queue * RecQ;
  int sentEOF;
  int recvEOF;
  int timeout;
  int dupack;
  int EOFsentTime; 
  int prevPacketFull;
  int rcvWindow;
  double congestWindow;
  int aimd;
  //queue * SendQend;
  int arraySize;
  sentPacket * sentPackets;
  int incrtTimer;
  struct timespec * startTime;
  /* Add your own data fields below this */

};

rel_t *rel_list;

//Helper function declarations
void send_prepare(packet_t * packet);
void read_prepare(packet_t * packet);
void sentPacketSize(rel_t * r);
packet_t * create_data_packet(rel_t * s);
int min(int a, int b);
int check_close(rel_t * s);
int timeval_subtract(timeval * result, timeval * x, timeval * y);
int acktoSend;
void create_send_ack_packet(rel_t * r);
void incrementCongestion(rel_t * r);
int max(int i, double j);
packet_t * createEOF(rel_t * s);

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
  r->aimd = 0;
  r->c = c;
  r->next = rel_list;
  r->prev = &rel_list;
  if (rel_list){
    rel_list->prev = &r->next;
  //  fprintf(stderr, "prev %p\n", rel_list->prev);
  }
  rel_list = r;
  r-> SWS = cc-> window;
  r->arraySize = r->SWS*2;
  r-> LAR= 0;
  r-> LFS = 0;
  r-> NFE = 1;
  r->sentEOF = 0;
  r->recvEOF = 0;
  r->sentPackets =(sentPacket*) malloc(sizeof(sentPacket)*2*r->SWS);
  r->EOFsentTime = 0;
  r->congestWindow = 1;
  r->incrtTimer = 0;
  r -> timeout = cc -> timeout;
  r->prevPacketFull = 0;
  r->rcvWindow = r->SWS;
  r->startTime = malloc(sizeof(struct timespec));
  clock_gettime(CLOCK_MONOTONIC, r->startTime);
  /* Do any other initialization you need here */

  //fprintf(stderr, "createexit\n");
  return r;
}

void
rel_destroy (rel_t *r)
{
  //fprintf(stderr, "destroyed %p\n", r);
  if (r->next)
     r->next->prev = r->prev;
   *r->prev = r->next;
  conn_destroy (r->c);
  free(r);
  struct timespec * endTime = malloc(sizeof(struct timespec));
  clock_gettime(CLOCK_MONOTONIC, endTime);

  int beginTimeSec = r->startTime->tv_sec;
  int endTimeSec = endTime->tv_sec;
  fprintf(stderr, "Elapsed time: %d\n", endTimeSec-beginTimeSec);
  /* Free any other allocated memory here */
}


void
rel_demux (const struct config_common *cc,
	   const struct sockaddr_storage *ss,
	   packet_t *pkt, size_t len)
{
  //leave it blank here!!!
}

void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
  r = rel_list;
//  fprintf(stderr, "rel_recv length %d seqno %d NFE %d LAR %d \n", pkt->len, pkt->seqno, r->NFE, r->LAR);
  uint16_t sum = pkt-> cksum;
  uint16_t len = ntohs(pkt->len); 

  pkt-> cksum = 0;
  if( (len<ACK_PACKET_SIZE || len <=MAX_PACKET_SIZE) && cksum(pkt, len)!= sum){
    return;
  }

  read_prepare(pkt);
   fprintf(stderr, "rel_recv length %d seqno %d ackno %d NFE %d LAR %d SWS %d rcvwndw %d congestWindow %G\n", pkt->len, pkt->seqno, pkt->ackno, r->NFE, r->LAR, r->SWS, r->rcvWindow, r->congestWindow);
  pkt->cksum = sum;

  if(len == ACK_PACKET_SIZE){
    int ackno = pkt->ackno;
    int i;
    int found=0;
    for(i = 0; i<r->arraySize; i++){
      if(r->sentPackets[i].valid == 1){
        fprintf(stderr, "within loop of rel_recv queue packet seqno %d ackno %d \n", r->sentPackets[i].pkt->seqno, ackno);
        if(r->sentPackets[i].pkt->seqno < ackno){
          r->sentPackets[i].valid = -1;
          found = 1;
        } 
      }
             
    }
    fprintf(stderr, "found %d\n", found);
    if(found ==1 ){
        r->rcvWindow = pkt->rwnd;
        sentPacketSize(r);
        r->LAR = ackno -1;      
        r->dupack = 0;
    }else{
        r->dupack = r->dupack+1;
        if(r->dupack ==3){
          for(i=0; i<r->arraySize; i++){
            if(r->sentPackets[i].valid == 1 && r->sentPackets[i].pkt->seqno == ackno){
              int len = r->sentPackets[i].pkt->len;
              r->sentPackets[i].pkt->ackno = htonl(r->sentPackets[i].pkt->ackno);
              r->sentPackets[i].pkt->seqno = htonl(r->sentPackets[i].pkt->seqno);
              r->sentPackets[i].pkt->len = htons(r->sentPackets[i].pkt->len);
              r->sentPackets[i].pkt->rwnd = htonl(r->sentPackets[i].pkt->rwnd);
              r->sentPackets[i].pkt->cksum = 0;
              r->sentPackets[i].pkt->cksum = cksum(r->sentPackets[i].pkt,len);
              conn_sendpkt(r->c, r->sentPackets[i].pkt, (size_t)len);
              read_prepare(r->sentPackets[i].pkt);
            }
          }
          r->dupack =0;
          r->congestWindow = max(1,r->congestWindow/2);
          r->aimd=1;
          return;
        }
    }
    rel_read(r);
  }else if(len > ACK_PACKET_SIZE && len<=MAX_PACKET_SIZE){
    uint32_t seqno = pkt -> seqno;
    if((seqno < r->NFE)){
      create_send_ack_packet(r);
        // packet_t * acknowledgementPacket = (packet_t *) malloc(ACK_PACKET_SIZE);
        // acknowledgementPacket->cksum = 0;
        // uint16_t ackSize = ACK_PACKET_SIZE;
        // acknowledgementPacket->len = htons(ackSize);
        // acknowledgementPacket->ackno = htonl(r->NFE);
        // acknowledgementPacket->rwnd = htonl(r->rcvWindow);
        // uint16_t checkSum = cksum(acknowledgementPacket, ackSize);
        // acknowledgementPacket->cksum = checkSum;
        // conn_sendpkt(r->c, acknowledgementPacket, ackSize);
        // free(acknowledgementPacket);
    } 
    if(seqno >= (r->NFE + r->SWS)){
      //fprintf(stderr,"dropping seqno %d NFE%d\n", seqno, r->NFE);
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
   }
   fprintf(stderr, "rel_recv returns");
}

void
rel_read (rel_t *s)
{
  if(s->c->sender_receiver == RECEIVER)
  {
    if(!(s->sentEOF)) {
      packet_t * packet = createEOF(s);
      conn_sendpkt(s->c, packet, EOF_PACKET_SIZE);
      s->sentPackets[0].pkt = packet;
      s->sentPackets[0].valid = 1;
      s->sentPackets[0].timeCount = 0;
      s->sentEOF = 1;
    }

    //if already sent EOF to the sender
    //  return;
    //else
    //  send EOF to the sender
  }
  else //run in the sender mode
  {
    fprintf(stderr, "tryingsending packet LFS %d, LAR %d, SWS %d, sentEOF %d\n", s->LFS, s->LAR, s->SWS, s->sentEOF);
    if(s->LFS - s->LAR < s->SWS && s->sentEOF!=1){ //&& s->prevPacketFull != 1){
      packet_t * newPacket = create_data_packet(s);
      if(newPacket == NULL){
        return;
      }else if(newPacket->len == EOF_PACKET_SIZE){//check for -1 aka EOF 
        s->sentEOF =1;
      }
      int length = newPacket->len;
      send_prepare(newPacket);
      conn_sendpkt(s->c, newPacket, (size_t)length);
      read_prepare(newPacket);
      int i;
      fprintf(stderr, "sent packets successfully\n");
      for(i =0; i < s->arraySize; i++){
        if(s->sentPackets[i].valid < 1){
          fprintf(stderr, "sent packet: seqno:%d \n", newPacket->seqno);
          s->sentPackets[i].valid = 1;
          s->sentPackets[i].timeCount = 0;
          s->sentPackets[i].pkt = newPacket;
          break;
        }

      }
      if(check_close(s) == 1){
        rel_destroy(s);
        return;
      }       
      s->LFS++;
    }
  }
}

packet_t * createEOF(rel_t * s) {
  packet_t * packet = malloc(sizeof(packet_t));
  packet->cksum = 0;
  packet->len = EOF_PACKET_SIZE;
  packet->ackno = 0;
  packet->rwnd = s->rcvWindow;
  packet->seqno = 1;
  send_prepare(packet);
  return packet;
}

int min(int a, int b){
  if(a <= b) 
    return a;

 return b;
}
void sentPacketSize(rel_t * r){
  int size = min(r->rcvWindow, r->congestWindow);
  r->SWS = size;
  if(r->arraySize<size){
    
    sentPacket * newArray = malloc(sizeof(sentPacket)*size*2);
    sentPacket * temp = r->sentPackets;
    int i;
    for(i=0; i<r->arraySize; i++){
      newArray[i] = temp[i];
    }
    r->sentPackets = newArray;
    r->arraySize = size*2;
    //free(temp);
  }
}

packet_t * create_data_packet(rel_t * s){
  packet_t *packet;
  packet = (packet_t*)malloc(sizeof(packet_t));
  //fprintf(stderr, "Creating packets %p, %s\n", s, packet->data);
  int bytes = conn_input(s->c, packet->data, PACKET_DATA_MAX_SIZE);
 // fprintf(stderr, "conn input didnt segfault%p, %p, %d\n", s, packet->data, bytes);

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
   // fprintf(stderr, "reaches end of input len %d \n", packet->len);
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
  packet->rwnd = htonl(packet->rwnd);
  packet->cksum = cksum(packet, length);
}

void read_prepare(packet_t * packet){
  packet->ackno = ntohl(packet->ackno);
  packet->seqno = ntohl(packet->seqno);
  packet->len = ntohs(packet->len);
  packet->rwnd = ntohl(packet->rwnd);

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
      int dataSize = packet->len - 12;
      
      if(dataSize <= remainingBufSpace) {

        if(r->recvEOF != 1){
         conn_output(connection, data, dataSize);
        }
       if(packet->len == EOF_PACKET_SIZE ){
         r->recvEOF = 1; 
      //  if(packet->seqno != 1){
        //r->sentEOF =2;
        //rel_read(r);
        packet_t * EOFPacket = createEOF(r);
        conn_sendpkt(r->c, EOFPacket, EOF_PACKET_SIZE);
        free(EOFPacket);
      }
        ackno = seqno + 1;
        r->NFE = ackno;
        int advertisedWindow = (int)remainingBufSpace/MAX_PACKET_SIZE;
        fprintf(stderr, "advertised window %d \n", advertisedWindow);
        r->rcvWindow=advertisedWindow;  
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
    else if(r->NFE < seqno){
      create_send_ack_packet(r);
    }
    else { 
      break;
    } 
    recvQ = r->RecQ;
  }


  if(ackno != -1) {
  //  fprintf(stderr, "sending ack %d  \n",ackno);
    // packet_t * acknowledgementPacket = (packet_t *) malloc(ACK_PACKET_SIZE);
    // acknowledgementPacket->cksum = 0;
    // uint16_t ackSize = ACK_PACKET_SIZE;
    // acknowledgementPacket->len = htons(ackSize);
    // acknowledgementPacket->ackno = htonl(ackno);
    // acknowledgementPacket->rwnd = htonl(r->rcvWindow);
    // uint16_t checkSum = cksum(acknowledgementPacket, ackSize);
    // acknowledgementPacket->cksum = checkSum;
    // conn_sendpkt(connection, acknowledgementPacket, ackSize);
    // free(acknowledgementPacket);
    create_send_ack_packet(r);
  }

    if(check_close(r) == 1){
    rel_destroy(r);
    return;
  }
}

void create_send_ack_packet(rel_t * r){
    packet_t * acknowledgementPacket = (packet_t *) malloc(ACK_PACKET_SIZE);
    acknowledgementPacket->cksum = 0;
    uint16_t ackSize = ACK_PACKET_SIZE;
    acknowledgementPacket->len = htons(ackSize);
    acknowledgementPacket->ackno = htonl(r->NFE);
    acknowledgementPacket->rwnd = htonl(r->rcvWindow);
    uint16_t checkSum = cksum(acknowledgementPacket, ackSize);
    acknowledgementPacket->cksum = checkSum;
    conn_sendpkt(r->c, acknowledgementPacket, ackSize);
    free(acknowledgementPacket);
}


int check_close(rel_t * s){ //Still need to check for time condition!
  int i;
  int check = 1;
  for(i=0; i<s->SWS; i++){
    if(s->sentPackets[i].valid == 1){
      check =0;
      break;
    }
  }
  //int timeSinceEOF = difference of EOFsentTime and currentTime as an int
  fprintf(stderr, "%d %d %d %d %d %d\n",check, s->RecQ==NULL, s->sentEOF, s->recvEOF, s->EOFsentTime, getpid());
  if(check && s->RecQ == NULL && s->sentEOF == 1 && s->recvEOF == 1 && s->EOFsentTime >=10){
    fprintf(stderr, "I closed!!!");
    return 1;
  }
  return 0;
}

void
rel_timer ()
{
    /* Retransmit any packets that need to be retransmitted */
  rel_t * r = rel_list;
  int i;
  if(r->sentEOF== 1){
    r->EOFsentTime = r->EOFsentTime +1;
  }
  for(i = 0; i < r->SWS; i++){

    if(r->sentPackets[i].valid == 1) {
      if(r->sentPackets[i].timeCount >= r->timeout/10) {
        r->sentPackets[i].timeCount = 0;
        int len = r->sentPackets[i].pkt->len;
        r->sentPackets[i].pkt->ackno = htonl(r->sentPackets[i].pkt->ackno);
        r->sentPackets[i].pkt->seqno = htonl(r->sentPackets[i].pkt->seqno);
        r->sentPackets[i].pkt->len = htons(r->sentPackets[i].pkt->len);
        r->sentPackets[i].pkt->cksum = 0;
        r->sentPackets[i].pkt->cksum = cksum(r->sentPackets[i].pkt,len);
        conn_sendpkt(r->c, r->sentPackets[i].pkt, (size_t)len);
        read_prepare(r->sentPackets[i].pkt);
      }
      else{
        r->sentPackets[i].timeCount = r->sentPackets[i].timeCount + 1;
      }
    }
    if(check_close(r) == 1){
      rel_destroy(r);
      return;
    }
  }
  
  if(r->incrtTimer >= 4) {
    int incrCongestion = 1;
    int j;
    for(j=0; j < r->arraySize; j++) {
      packet_t * packet = r->sentPackets[j].pkt;
      if(r->sentPackets[j].valid == 1 && packet->seqno <= r->LAR) {
        incrCongestion = 0;
        break;
      }
    }
    if(incrCongestion) {
      incrementCongestion(r);
    }else {
      r->congestWindow = max(1,r->congestWindow/2);
      r->aimd = 1;
    }
    r->incrtTimer = -1;
  }
  r->incrtTimer = r->incrtTimer + 1;
  /* Retransmit any packets that need to be retransmitted */
}

int 
max(int i, double j)
{
  if(i > j){
    return i;
  }
  return (int)j;
}

void
incrementCongestion(rel_t * r) {
  if(r->aimd) {
    r->congestWindow = r->congestWindow + 1;
  }
  else{
    r->congestWindow = r->congestWindow * 2;
  }
}
