#include "contiki.h"
#include "net/rime/rime.h"
#include "sys/etimer.h"
#include "stdio.h"

#include "dev/button-sensor.h"


#define MAX_RETRANSMISSIONS 5

//unlocked (0) by default
static int alarm_state = 0;

static int command = 0;
static int button_pressed = 0;

PROCESS(handle_command_process, "Handle command process");
AUTOSTART_PROCESSES(&handle_command_process);
  

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *senderAddr){
  printf("broadcast message received from %d.%d: '%s'\n", senderAddr->u8[0], 
				senderAddr->u8[1], (char *)packetbuf_dataptr());
}

/*
   the callbacks broadcast_sent take 3 parameter: a link to the bradcast connection stack , an
   integer status that specify the status of the trasmission (status == 0 -> ok
                                                              status == 1 -> collision
                                                              status == 2 -> NOACK
                                                              etc....)
   Finally we have int num_tx that is the number of retrasmissions that have to be performed
*/
static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){
  printf("broadcast message sent with status %d. Transmission number = %d\n", status, num_tx);
}


static void recv_runicast(struct runicast_conn *c, const linkaddr_t *sender_addr, uint8_t seqno){
  printf("runicast message received form %d.%d. Sequence number = %d\n", sender_addr->u8[0], sender_addr->u8[1], seqno);
}

static void sent_runicast(struct runicast_conn *c, const linkaddr_t *receiver_addr, uint8_t retransmissions)
{
  printf("runicast message sent to %d.%d, retransmissions %d\n", receiver_addr->u8[0], receiver_addr->u8[1], retransmissions);
}

/*
  timedout_runicast called when timeout expired
*/
static void timedout_runicast(struct runicast_conn *c, const linkaddr_t *receiver_addr, uint8_t retransmissions)
{
  printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", 
                         receiver_addr->u8[0], receiver_addr->u8[1], retransmissions);
}


//Be careful to the order
static const struct broadcast_callbacks broadcast_call = {broadcast_recv, broadcast_sent}; 
static struct broadcast_conn broadcast;
static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};
static struct runicast_conn runicast;



PROCESS_THREAD(handle_command_process, ev, data){

  static struct etimer et;

  /*
    triggered only when there is a PROCESS_EXIT event, in this case we don't require to keep
    the connection open
  */
  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));


  PROCESS_BEGIN();

  SENSORS_ACTIVATE(button_sensor);

  /*
    we open the connection. The second parameter is the channel on which the node will 
    communicate.
    it is a sort of port
  */
  broadcast_open(&broadcast, 129, &broadcast_call);
  runicast_open(&runicast, 144, &runicast_calls); //open our runicast connection over the channel #144

  while(1) {
    etimer_set(&et, CLOCK_SECOND*4);

    PROCESS_WAIT_EVENT();

    if (ev == sensors_event && data == &button_sensor){
      printf("Button pressed\n");

      if (button_pressed == 0)
        //set the timer the first time
        etimer_set(&et, CLOCK_SECOND*4);
      else
        //restart the timer (4 seconds from NOW)
        etimer_restart(&et);


      button_pressed ++;

    }else 
      if (etimer_expired(&et)){        
        if (button_pressed < 1 || button_pressed > 5)
          printf("Error: command not recognized.\n");
        else {
          command = button_pressed;
          button_pressed = 0;
          printf ("Command = %d.\n", command);
      }
    }

    /*
    packetbuf_copyfrom("New message", 12);
    //broadcast_send(&broadcast);


    /*
    this is not carrier sennsing... this is if I don't trasmitt then we can send.
    take into accont that retrasnission can happen so, sometime, i cannot be able
    to send at this step because i need to trasmit an old message.
    *//*
    if(!runicast_is_transmitting(&runicast)) {

      linkaddr_t recv;
      packetbuf_copyfrom("Hello from Node 1", 18); // we put our data in the packet
      recv.u8[0] = 3;
      recv.u8[1] = 0;

      printf("%u.%u: sending runicast to address %u.%u\n", linkaddr_node_addr.u8[0], linkaddr_node_addr.u8[1], recv.u8[0], recv.u8[1]);

      /*
        then we call the runicast send
      *//*
      runicast_send(&runicast, &recv, MAX_RETRANSMISSIONS);
    }*/
  }

  PROCESS_END();
}


