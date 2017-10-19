#include "contiki.h"
#include "net/rime/rime.h"
#include "sys/etimer.h"
#include "stdio.h"
#include "core/lib/random.h"
#include "dev/leds.h"
#include "dev/sht11/sht11-sensor.h"
#include "dev/button-sensor.h"


#define MAX_RETRANSMISSIONS 5

//unlocked (0) by default
int alarm_state = 0;
//off(0) by default
int light_state = 0;
int command = 0;
int reject_locking = 0;
int temperatures[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
int temperature_index = 0;

static unsigned char leds_status;

PROCESS(main_process, "Main process");
PROCESS(blinking_process, "Blinking process");
PROCESS(open_door, "Open the door");
PROCESS(temperature_process, "Temperature process");
PROCESS(compute_mean_temperateure, "Compute mean temperature");

AUTOSTART_PROCESSES(&main_process, &temperature_process);
  

static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *senderAddr){
  printf("broadcast message received from %d.%d: '%s'\n", senderAddr->u8[0], 
				senderAddr->u8[1], (char *)packetbuf_dataptr());

  //obtain the int command code from the message
  command = *(int*)packetbuf_dataptr();
  //printf("Received broadcast command = %d\n", command);

  switch (command){
    case 1:
      //the leds have to start blinking or stop blinking, depending on the state
      //of the alarm
        printf ("alarm_state_old = %d\n", alarm_state);

      //update the state of the alarm
      alarm_state = (alarm_state == 0)?1:0;

        printf ("alarm_state = %d\n", alarm_state);
    
      //the user activates the alarm
      if (alarm_state)
        process_start(&blinking_process, NULL);

      //the user deactivate the alarm
      if (!alarm_state)
        process_exit(&blinking_process);

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
  printf("runicast message received from %d.%d. Sequence number = %d\n", 
                    sender_addr->u8[0], sender_addr->u8[1], seqno);

  command = *(int*)packetbuf_dataptr();
  //printf("Received command = %d\n", command);

  switch (command){
    case 4:
      //compute the mean temp and send it to the CU
      process_start(&compute_mean_temperateure, NULL);

      break;
    default:
      printf("Error: command not recognized\n");
  }
}


static void sent_runicast(struct runicast_conn *c, const linkaddr_t *receiver_addr, uint8_t retransmissions)
{
  printf("runicast message sent to %d.%d, retransmissions %d\n", receiver_addr->u8[0], receiver_addr->u8[1], retransmissions);
}


/*******************************************************************************
  timedout_runicast called when timeout expired
*******************************************************************************/
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
    triggered only when there is a PROCESS_EXIT event, in this case we don't 
    require to keep the connection open
  */
  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast_CU));

  PROCESS_BEGIN();

  //at the beginning the lights are off
  leds_off(LEDS_GREEN);
  leds_on(LEDS_RED);

  SENSORS_ACTIVATE(button_sensor);

  //we open the connection
  broadcast_open(&broadcast, 129, &broadcast_call);
  runicast_open(&runicast_CU, 131, &runicast_calls); //open our runicast connection over the channel #144

  while(1) {

    PROCESS_WAIT_EVENT_UNTIL(ev==sensors_event && data==&button_sensor);

    //the alarm is not active
    if (!alarm_state){
      //update the state of the lights
      light_state = (light_state)?0:1;

      //toggle the leds
      leds_toggle(LEDS_RED);
      leds_toggle(LEDS_GREEN);
    }
  }

  SENSORS_DEACTIVATE(button_sensor);

  PROCESS_END();
}


PROCESS_THREAD (blinking_process, ev, data){

  static struct etimer blinking_timer;

  PROCESS_BEGIN();

  if (reject_locking){
    printf("Refuse to activate the alarm\n");
    int error = 4031;
    packetbuf_copyfrom((void*)&error, sizeof(int));
    
    //send the command in broadcast
    broadcast_send(&broadcast);

    //restore the status of the lock (unlocked)
    alarm_state = 0;

    PROCESS_EXIT();

    break;
  }

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

  reject_locking = 1;
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
      reject_locking = 0;
      PROCESS_EXIT();
    }
  }

  PROCESS_END();
}

/******************************************************************************* 
    every 10 seconds take a new temperature measurement.
    The last 5 measurement are stored in the temperatures array
*******************************************************************************/
PROCESS_THREAD(temperature_process, ev, data){
  static struct etimer temperature_timer;

  PROCESS_BEGIN();
  etimer_set(&temperature_timer, CLOCK_SECOND*10);

  while(1){
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&temperature_timer));

    //the sensor has to stay active for the minimum amount of time
    SENSORS_ACTIVATE(sht11_sensor);
    //actual (normalized) temp sample
    temperatures[temperature_index%5]  = (
                            sht11_sensor.value(SHT11_SENSOR_TEMP)/10-396)/10;
    //stop the sensing phase
    SENSORS_DEACTIVATE(sht11_sensor);

    //collect a sample every 10 second
    etimer_restart(&temperature_timer);

    //24° is the default value
    //RANDOM_MAX = 65535 -> random_rand()/6000 at most 10 values
    //             +/-5°(more or less)
      temperatures[temperature_index%5] += (int)random_rand()/6000;;

    temperature_index++;

    //printf("temperatures = {%d, %d, %d, %d, %d}\n", temperatures[0],
    //        temperatures[1], temperatures[2], temperatures[3], temperatures[4]);
  }

  PROCESS_END();
}


PROCESS_THREAD(compute_mean_temperateure, ev, data){
  int mean_temperature = 0;
  int i;

  PROCESS_BEGIN();
    
  for (i = 0; i<5; i++){
    if (i<temperature_index){
      mean_temperature += temperatures[i];
    }
  }

  if (temperature_index<5)
    mean_temperature /= temperature_index;
  else
    mean_temperature /= 5;

  printf("mean_temperature %d\n", mean_temperature);

  if(!runicast_is_transmitting(&runicast_CU)){
    linkaddr_t recv;

    //Central unit has rime address 3.0
    recv.u8[0] = 3;
    recv.u8[1] = 0;

    packetbuf_copyfrom((void*)&mean_temperature, sizeof(int));
    //send
    runicast_send(&runicast_CU, &recv, MAX_RETRANSMISSIONS);
  }

  PROCESS_END();
}