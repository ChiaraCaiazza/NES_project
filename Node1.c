#include "contiki.h"
#include "net/rime/rime.h"
#include "sys/etimer.h"
#include "stdio.h"

#include "dev/leds.h"



#define MAX_RETRANSMISSIONS 5

//unlocked (0) by default
static int alarm_state = 0;

static int command = 0;

static unsigned char leds_status;

PROCESS(main_process, "Main process");
PROCESS(blinking_process, "Blinking process");
PROCESS(open_door, "Open the door");

AUTOSTART_PROCESSES(&main_process);
  

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *senderAddr){
  printf("broadcast message received from %d.%d: '%s'\n", senderAddr->u8[0], 
				senderAddr->u8[1], (char *)packetbuf_dataptr());

  //obtain the int command code from the message
  command = *(char *)packetbuf_dataptr() - '0';
  printf("Received broadcast command = %d\n", command);

  switch (command){
    case 1:
      //the leds have to start blinking or stop blinking, depending on the state
      //of the alarm

      //update the state of the alarm
      alarm_state = (alarm_state == 0)?1:0;
    
      //the user activates the alarm
      if (alarm_state)
        process_start(&blinking_process, NULL);

      //the user deactivate the alarm
      if (!alarm_state)
        process_exit(&blinking_process);

      printf ("alarm_state = %d\n", alarm_state);

      break;
    case 3:
      //open(and automatically close) both the door and the gate

      //this is the node on the door: it waits for 14 seconds, then it blinks 
      //for 16 second with a period of two seconds
      process_start(&open_door, NULL);

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
//static struct runicast_conn runicast;



PROCESS_THREAD(main_process, ev, data){
  /*
    triggered only when there is a PROCESS_EXIT event, in this case we don't require to keep
    the connection open
  */
  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  //PROCESS_EXITHANDLER(runicast_close(&runicast));


  PROCESS_BEGIN();


  /*
    we open the connection. The second parameter is the channel on which the node will 
    communicate.
    it is a sort of port
  */
  broadcast_open(&broadcast, 129, &broadcast_call);
  //runicast_open(&runicast, 144, &runicast_calls); //open our runicast connection over the channel #144

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

  //save the state of the leds
  leds_status = leds_get();

  leds_off(LEDS_ALL);

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
      //restore the last status of the leds
      leds_set(leds_status);
    }

  }

  PROCESS_END();
}


PROCESS_THREAD(open_door, ev, data){
  static struct etimer door_timer, blink_door_timer;

  PROCESS_BEGIN();

  //waits 14 seconds
  etimer_set(&door_timer, CLOCK_SECOND*14);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&door_timer));

  //then the blue led blink every two seconds for 16 seconds
  etimer_set(&blink_door_timer, CLOCK_SECOND);
  etimer_set(&door_timer, CLOCK_SECOND*16);
  leds_toggle(LEDS_BLUE);
  
  while(1){
    PROCESS_WAIT_EVENT();
    if (etimer_expired(&blink_door_timer)){
      leds_toggle(LEDS_BLUE);
      etimer_restart(&blink_door_timer);
    }

    if (etimer_expired(&door_timer)){
      leds_off(LEDS_BLUE);
      PROCESS_EXIT();
    }
  }

  PROCESS_END();
}
