
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
//  fprintf(stderr, "create\n");
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
  //  fprintf(stderr, "prev %p\n", rel_list->prev);
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
  /* Free any other allocated memory here */
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
  //fprintf(stderr, "%d %d %d %d %d %d\n",check, s->RecQ==NULL, s->sentEOF, s->recvEOF, s->EOFsentTime, getpid());
  if(check && s->RecQ == NULL && s->sentEOF == 1 && s->recvEOF == 1 && s->EOFsentTime >=10)
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
 // fprintf(stderr,"rec in id %d \n",getpid());
  // FILE * output = fopen("output.txt", "a");
  // fprintf(output, "relrecv %d", getpid());
  // fclose(output);
  r = rel_list;

  // if(pkt==NULL){
  //   fprintf(stderr,"rec done no pkt id %d \n",getpid());
  //   return;
  // }

  uint16_t sum = pkt-> cksum;
  uint16_t len = ntohs(pkt->len); 

  pkt-> cksum = 0;
  if( (len<8 || len <=512) && cksum(pkt, len)!= sum){
  //    fprintf(stderr,"cksum fail length %d id %d seqno %d NFE %d\n",len,getpid(), ntohl(pkt->seqno), r->NFE);
    return;
  }
  // if((len<8 || len <=512)){
  //   fprintf(stderr,"rec bad length id %d \n",getpid());
  //   return;
  // }

  read_prepare(pkt);
  //fprintf(stderr,"rec length %d id %d seqno %d NFE %d\n",len,getpid(), pkt->seqno, r->NFE);
//  output = fopen("output.txt", "a");
// fprintf(stderr, "recvpkt len %d  seqno %d id %d\n", len, pkt->seqno, getpid());
 // fclose(output);
  pkt->cksum = sum;
 //Check for closing conditions, need to change this to account for timer condition
  // fprintf(stderr, "is this getting called bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb \n");
  //       if(pkt->len == EOF_PACKET_SIZE){
  //         r->recvEOF =1;
  //         // if(pkt->seqno!=1)
  //         //   return;
  //       }
  // //   if(check_close(r) == 1){
  //        //  FILE * output = fopen("output.txt", "a");
  //        //  fprintf(output, "closed %d \n", pkt->ackno);
  //        // fclose(output);
  //     rel_destroy(r);
  // //    fprintf(output, "eof rec\n");
  //     return;
  
  // //  if(pkt->seqno == 1)
  //   //  return;
  // }
//  fclose(output);
 //    fprintf(stderr, "rec mid len %d id %d\n",len, getpid());
  if(len == 8){

    //FILE* output = fopen("output.txt", "a");
    //fprintf(stderr, "Ack Received: %d\n", pkt->ackno);
    int ackno = pkt->ackno;
    int i;
    for(i = 0; i<r->SWS; i++){
      if(r->sentPackets[i].valid == 1 && r->sentPackets[i].pkt->seqno < ackno){
        r->sentPackets[i].valid = -1;
    //   fprintf(stderr, "Recieved ack %d id %d valid %d \n", ackno, getpid(), r->sentPackets[i].valid);
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
  }else if(len > 8 && len<=512){
    // FILE * output = fopen("output.txt", "a");
    // fprintf(output, "seqno %d NFE %d SWS %d\n", pkt->seqno, r->NFE, r->SWS);
    // fclose(output);
//   fprintf(stderr,"here \n");
    uint32_t seqno = pkt -> seqno;
    //fprintf(stderr,"here %d id %d\n", seqno, getpid());
    if((seqno < r->NFE)){
        packet_t * acknowledgementPacket = (packet_t *) malloc(8);
        acknowledgementPacket->cksum = 0;
        uint16_t ackSize = 8;
        acknowledgementPacket->len = htons(ackSize);
        acknowledgementPacket->ackno = htonl(r->NFE);
        uint16_t checkSum = cksum(acknowledgementPacket, ackSize);
        acknowledgementPacket->cksum = checkSum;
        conn_sendpkt(r->c, acknowledgementPacket, ackSize);
        free(acknowledgementPacket);
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
    // queue * current = r->RecQ;
    // if(current == NULL){
    //   //     fprintf(stderr,"null q");
    // }
    //   while(current!=NULL){
    //     if(current->pkt!=NULL){
    //       fprintf(stderr, " recq %d ",current->pkt->seqno);
    //     }else{
    //       fprintf(stderr,"null pkt");
    //       }
    //       current = current->next;
    //     }
      
    //   fprintf(stderr, "id %d\n",getpid());
      rel_output(r);
      //fprintf(stderr, "rec data to out seq %d NFE %d\n", seqno, r->NFE);
   }
   //  fprintf(stderr, "rec fin len %d id %d\n",len, getpid());
  // FILE * output = fopen("output.txt", "a");
  // fprintf(output, "relrecv %d", getpid());
  // fclose(output);
   //fprintf(stderr,"rec closed id %d \n",getpid());
}


void
rel_read (rel_t *s)
{
  //s = rel_list;
  //fprintf(stderr, "rel read in %d\n",getpid());
  //fprintf(stderr,"Last frame sent: %d, Last ack Recv: %d, sws: %d sentEOF %d id %d\n", s->LFS, s->LAR, s->SWS, s->sentEOF, getpid());
  if(s->LFS - s->LAR < s->SWS && s->sentEOF!=1){ //&& s->prevPacketFull != 1){
 //   fprintf(stderr, "rel read in while, %p\n",s );
    packet_t * newPacket = create_data_packet(s);
    if(newPacket == NULL){
      //fprintf(stderr, "newPacket is null\n");

      return;
    }else if(newPacket->len == EOF_PACKET_SIZE){//check for -1 aka EOF
  
      s->sentEOF =1;
//      fprintf(stderr, "is this getting called aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa %d \n", getpid());
      

    }
    int length = newPacket->len;
  //  fprintf(stderr,"Sent packet: %s\n", newPacket->data);
    send_prepare(newPacket);
    conn_sendpkt(s->c, newPacket, (size_t)length);
    read_prepare(newPacket);
    int i;
    for(i =0; i < s->SWS; i++){
      //fprintf(stderr, "valid %d  \n",s->sentPackets[i].valid);
      if(s->sentPackets[i].valid < 1){
     //   fprintf(stderr, "putting in queue seqno %d id %d valid %d v seqno %d \n", newPacket->seqno, getpid(), s->sentPackets[i].valid, s->sentPackets[i].pkt->seqno);
        s->sentPackets[i].valid = 1;
        //gettimeofday(s->sentPackets[i].transmissionTime,NULL);
        s->sentPackets[i].timeCount = 0;

        //memcpy(s->sentPackets[i].pkt, &newPacket, length);
        s->sentPackets[i].pkt = newPacket;
        // FILE * output = fopen("output.txt", "a");
        // fprintf(output, "relread");
        // fclose(output);
        //        fprintf(stderr, "putting in queue seqno %d id %d valid %d v seqno %d \n", newPacket->seqno, getpid(), s->sentPackets[i].valid, s->sentPackets[i].pkt->seqno);

        break;
      }

    }
          if(check_close(s) == 1){
     //   fprintf(stderr, "is this getting called aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \n");
        rel_destroy(s);
        return;
      }
    //queue *sent = malloc(sizeof(queue));    
    //free(sent);
    // if(s->sentEOF == 1)
    //   gettimeofday(s->EOFsentTime,NULL);        
    s->LFS++;
  }

  //s->prevPacketFull =0;
  //fprintf(stderr, "rel read done %d\n", getpid());
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
  //fprintf(stderr, "out id %d\n", getpid());
  int ackno = -1;
  while(recvQ != NULL) {
    packet_t * packet = recvQ->pkt;
    int seqno = packet->seqno;
  //  fprintf(stderr, "here NFE %d seqno %d reof %d id %d", r->NFE, seqno, r->recvEOF, getpid());
    if(r->NFE == seqno) {

      int remainingBufSpace = conn_bufspace(connection);
      char* data = packet->data;
      int dataSize = packet->len - 12;
      if(dataSize <= remainingBufSpace) {
        // FILE * output = fopen("output.txt", "a");
        // fprintf(output, "output seqno: %d NFE: %d \n",packet -> seqno, r->NFE);
        // fclose(output);
    //    fprintf(stderr,"here \n");
        if(r->recvEOF != 1){
      //   fprintf(stderr, "data %s datasize %d id %d\n",data , dataSize, getpid());
         conn_output(connection, data, dataSize);
        }
       if(packet->len == EOF_PACKET_SIZE ){
         r->recvEOF = 1; 
      //  if(packet->seqno != 1){
        r->sentEOF =2;
        rel_read(r);

        //r->sentEOF =1;
        // packet_t * eofpkt = malloc(12);
        // eofpkt->len = 12;
        // eofpkt->seqno= 1;
        // send_prepare(eofpkt);
        // conn_sendpkt(r->c, eofpkt, EOF_PACKET_SIZE);
      //}
      }
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


  if(ackno != -1) {
  //  fprintf(stderr, "sending ack %d  \n",ackno);
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

    if(check_close(r) == 1){
    rel_destroy(r);
    return;
  }
 // fprintf(stderr, "out done ackno %d NFE %d id %d\n",ackno, r->NFE, getpid() );
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
          if(r->sentEOF== 1){
          r->EOFsentTime = r->EOFsentTime +1;
        }
  for(i = 0; i < r->SWS; i++){
    //timeval_subtract(diff, t, r->sentPackets[i].transmissionTime);
    //int timediff = (diff->tv_sec + diff->tv_usec/1000000)/1000;

    //if(timediff > r->timeout && r->sentPackets[i].valid ==1){

    if(r->sentPackets[i].valid == 1) {
       //       fprintf(stderr, "%d  timecount %d i %d\n", r->sentPackets[i].pkt->seqno, r->sentPackets[i].timeCount,i);
          //fprintf(stderr, "%d\n", r->sentPackets[i].pkt->seqno);

        if(r->sentPackets[i].timeCount >= 5) {
          //gettimeofday(r->sentPackets[i].transmissionTime, NULL);
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
     //   fprintf(stderr, "is this getting called aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \n");
        rel_destroy(r);
        return;
      }
    
  }
  //free(t);
  //free(diff);
}


