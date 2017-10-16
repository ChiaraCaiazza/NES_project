#include "contiki.h"
#include "net/rime/rime.h" //required to use rime
#include "random.h"  //generate random value for random delay
#include "stdio.h"
#include "sys/etimer.h"

/*
  we declare only one process with autodeclaration
*/

PROCESS(broadcast_process, "Broadcast process");  

AUTOSTART_PROCESSES(&broadcast_process);


/*
  we declare two callback function so that the process thread can call them.
  callback function are slightly different from normal function because they are 
  automatically triggered when an event arrive.

  broadcast_recv triggered when a broadcast packet is received.
  broadcast_sent triggered when a broadcast packet is sent.


  we declare a struct composed by 2 field.
  static const struct broadcast_callbacks: represent the connection it is composed 
  by two pointer to the two function.
  The order which we specify our callback is important: the first has to be the 
  receive one and the second has to be the sent one. the inverse don't work.

  the callbacks broadcast_recv take 2 parameter: a link to the bradcast connection stack and a 
  pointer to an node's address (in the first case the sender address[i am the receiver] 
  and in the second case is the address of the receiver[i am the sender]). This address 
  is an array of (2) byte
  packetbuf_dataptr returns a pointer to void

   the callbacks broadcast_recv take 3 parameter: a link to the bradcast connection stack , an
   integer status that specify the status of the trasmission (status == 0 -> ok
                                                              status == 1 -> collision
                                                              status == 2 -> NOACK
                                                              etc....)
   Finally we have int num_tx that is the number of retrasmissions that have to be performed

*/
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from){

  printf("broadcast message received from %d.%d: '%s'\n", from->u8[0], from->u8[1], (char *)packetbuf_dataptr());
}

static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){

  printf("broadcast message sent. Status %d. For this packet, this is transmission number %d\n", status, num_tx);
}


static const struct broadcast_callbacks broadcast_call = {broadcast_recv, broadcast_sent}; //Be careful to the order
static struct broadcast_conn broadcast;



PROCESS_THREAD(broadcast_process, ev, data){

  static struct etimer et;

  /*
    triggered only when there is a PROCESS_EXIT event, in this case we don't require to keep
    the connection open
  */
  PROCESS_EXITHANDLER(broadcast_close(&broadcast));

  PROCESS_BEGIN();

  /*
    we open the connection. The second parameter is the channel on which the node will 
    communicate.
    it is a sort of port
  */
  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {

    /* Delay 4-8 seconds */
    etimer_set(&et, CLOCK_SECOND*4 + random_rand()%(CLOCK_SECOND * 4));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    packetbuf_copyfrom("New message", 12);
    broadcast_send(&broadcast);
  }

  PROCESS_END();
}
