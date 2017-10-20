#include "contiki.h"
#include "net/rime/rime.h"
#include "sys/etimer.h"
#include "stdio.h"
#include "dev/leds.h"
#include "dev/light-sensor.h"

#define MAX_RETRANSMISSIONS 5

//unlocked (0) by default
int alarm_state = 0;
//the gate is locked by default
int gate_locked = 1;

static int command = 0;

static unsigned char leds_status;

PROCESS(main_process, "Main process");
PROCESS(blinking_process, "Blinking process");
PROCESS(locking_gate, "Locks the gate");
PROCESS(open_gate, "Open the gate");
PROCESS(sensing_light, "sensing_light");
AUTOSTART_PROCESSES(&main_process);
  

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *senderAddr){
  printf("broadcast message received from %d.%d.\n", senderAddr->u8[0], 
				senderAddr->u8[1]);

  //obtain the int command code from the message
  command = *(int*)packetbuf_dataptr();
  //printf("Received command = %d\n", command);

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

      break;
    case 3:
      //open(and automatically close) both the door and the gate

      //this is the node on the gate. the blue led blinks for 16 seconds then 
      //stops;
      process_start(&open_gate, NULL);

      break;
    case 4031:
      //node 1 refuse to activate the alarm
      
      //deactivate the alarm
      process_exit(&blinking_process);
      alarm_state = 0;
      
      break;
    default:
      printf("Error: command not recognized\n");
  }
}


static void broadcast_sent(struct broadcast_conn *c, int status, int num_tx){
  printf("broadcast message sent with status %d. Transmission number = %d\n", 
                    status, num_tx);
}


static void recv_runicast(struct runicast_conn *c, const linkaddr_t *sender_addr, 
                                                                uint8_t seqno){
  printf("runicast message received from %d.%d. Sequence number = %d\n", 
                    sender_addr->u8[0], sender_addr->u8[1], seqno);

  command = *(int*)packetbuf_dataptr();
  //printf("Received command = %d\n", command);

  switch (command){
    case 2:
      //The CU asked to open/close the gate

      //update the state of the gate
      gate_locked = (gate_locked == 0)?1:0;

      process_start(&locking_gate, NULL);

      break;
    case 5:
      //Obtain the external light value and send it to the central unit
      process_start(&sensing_light, NULL);

      break;
    default:
      printf("Error: command not recognized\n");
  }
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
static struct runicast_conn runicast_CU;



PROCESS_THREAD(main_process, ev, data){
  /*
    triggered only when there is a PROCESS_EXIT event, in this case we don't require to keep
    the connection open
  */
  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast_CU));


  PROCESS_BEGIN();

  //we initialize the lock of the gate
  process_start(&locking_gate, NULL);

  //we open the connection
  broadcast_open(&broadcast, 129, &broadcast_call);
  runicast_open(&runicast_CU, 130, &runicast_calls); //open our runicast connection over the channel #144

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

    if (etimer_expired(&blinking_timer) && alarm_state){
      //the timer is expired and the alarm is active

      //the timer expired: toggle every led
      leds_toggle(LEDS_ALL);
      //restart the timer
      etimer_reset(&blinking_timer);
    }

    if (ev == PROCESS_EVENT_EXIT)
      //restore the last status of the leds
      leds_set(leds_status);
  }

  PROCESS_END();
}


PROCESS_THREAD (locking_gate, ev, data){
  PROCESS_BEGIN();

  if (gate_locked){
    //we are locking the gate
    leds_off(LEDS_GREEN);
    leds_on(LEDS_RED);
  }else{
  //we are unlocking the gate
    leds_on(LEDS_GREEN);
    leds_off(LEDS_RED);
  }
  
  PROCESS_END();
}


PROCESS_THREAD(open_gate, ev, data){
  static struct etimer gate_timer, blink_gate_timer;

  PROCESS_BEGIN();

  //the blue led blink every two seconds for 16 seconds
  etimer_set(&blink_gate_timer, CLOCK_SECOND);
  etimer_set(&gate_timer, CLOCK_SECOND*16);
  leds_toggle(LEDS_BLUE);
  
  while(1){
    PROCESS_WAIT_EVENT();
    if (etimer_expired(&blink_gate_timer)){
      leds_toggle(LEDS_BLUE);
      etimer_restart(&blink_gate_timer);
    }

    if (etimer_expired(&gate_timer)){
      leds_off(LEDS_BLUE);
      PROCESS_EXIT();
    }
  }

  PROCESS_END();
}


PROCESS_THREAD(sensing_light, ev, data){
  PROCESS_BEGIN();

  //sensing for the minimun amount of time
  SENSORS_ACTIVATE(light_sensor);
  //normalized sample of light
  int light = 10*light_sensor.value(LIGHT_SENSOR_PHOTOSYNTHETIC)/7;
  printf("Sensed light %d lux\n", light);
  SENSORS_DEACTIVATE(light_sensor);

  //transmit the light measurement to the CU
  if(!runicast_is_transmitting(&runicast_CU)){
    linkaddr_t recv;

    //the receiver is the CU with rime addr 3.0
    recv.u8[0] = 3;
    recv.u8[1] = 0;

    packetbuf_copyfrom((void*)&light, sizeof(int));
    runicast_send(&runicast_CU, &recv, MAX_RETRANSMISSIONS);
  }

  PROCESS_END();
}

