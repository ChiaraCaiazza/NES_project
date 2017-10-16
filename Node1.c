#include "contiki.h"
#include "net/rime/rime.h"
#include "sys/etimer.h"
#include "stdio.h"

#include "dev/leds.h"



#define MAX_RETRANSMISSIONS 5

//unlocked (0) by default
static int alarm_state = 0;

static int command = 0;

struct leds{
  int red;
  int green;
  int blue;
} leds_status;

PROCESS(main_process, "Main process");
PROCESS(blinking_process, "Blinking process");
AUTOSTART_PROCESSES(&main_process);
  

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *senderAddr){
  printf("broadcast message received from %d.%d: '%s'\n", senderAddr->u8[0], 
				senderAddr->u8[1], (char *)packetbuf_dataptr());

  //obtain the int command code from the message
  command = *(char *)packetbuf_dataptr() - '0';
  printf("Received command = %d\n", command);

  switch (command){
    case 1:
      alarm_state = (alarm_state == 0)?1:0;
      //the leds have to start blinking or stop blinking, depending on the state
      //of the alarm

      //the user activates the alarm
      if (alarm_state)
        process_start(&blinking_process, NULL);

      //the user deactivate the alarm

      if (!alarm_state)
        process_exit(&blinking_process);

         
      

      printf ("alarm_state = %d\n", alarm_state);
      break;
    default:
      printf("Error: command not recognized\n");
  }
}


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



PROCESS_THREAD(main_process, ev, data){
  /*
    triggered only when there is a PROCESS_EXIT event, in this case we don't require to keep
    the connection open
  */
  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast));


  PROCESS_BEGIN();


  /*
    we open the connection. The second parameter is the channel on which the node will 
    communicate.
    it is a sort of port
  */
  broadcast_open(&broadcast, 129, &broadcast_call);
  runicast_open(&runicast, 144, &runicast_calls); //open our runicast connection over the channel #144

  while(1) {

    PROCESS_WAIT_EVENT();

    
  }

  PROCESS_END();
}


PROCESS_THREAD (blinking_process, ev, data){

  static struct etimer blinking_timer;

  PROCESS_BEGIN();

  //2 sec period: 1 sec on, 1 sec off
  etimer_set(&blinking_timer, CLOCK_SECOND);

  while(1){
    
    PROCESS_WAIT_EVENT();

    //the timer is expired and the alarm is active
    if (etimer_expired(&blinking_timer) && alarm_state){
      //the timer expired: toggle every led
      leds_toggle(LEDS_ALL);
      //restart the timer
      etimer_reset(&blinking_timer);
    }

    if (ev == PROCESS_EVENT_EXIT){
      printf("Exiting\n");

      //restore the last status of the leds
      (leds_status.red)? leds_on(LEDS_RED): leds_off(LEDS_RED);
      (leds_status.green)? leds_on(LEDS_GREEN): leds_off(LEDS_GREEN);
      (leds_status.blue)? leds_on(LEDS_BLUE): leds_off(LEDS_BLUE); 


    }



  }

  PROCESS_END();
}

