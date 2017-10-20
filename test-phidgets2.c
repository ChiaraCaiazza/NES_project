#include <stdio.h>
#include "contiki.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "dev/z1-phidgets.h"
#include "sys/etimer.h"
#include "math.h"
#include "net/rime/rime.h"

//extension off by default(0)
static int extension_active = 0;
int temperature = 20;
int human_sensed = 0;

/*---------------------------------------------------------------------------*/
PROCESS(sensing_process, "Test Button & ADC");
PROCESS(main_process, "Main process");
AUTOSTART_PROCESSES(&main_process);
/*---------------------------------------------------------------------------*/
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *senderAddr){
  printf("broadcast message received from %d.%d: '%s'\n", senderAddr->u8[0], 
        senderAddr->u8[1], (char *)packetbuf_dataptr());

  int* data = (int*)packetbuf_dataptr();
  int command = *data;

  switch (command){
    case 6:
      //the user activate/deactivate the extension

      //update the state
      extension_active = (extension_active)?0:1;

      if (extension_active){
        //the user activate the extension
        process_start(&sensing_process, NULL);
      }

      if (!extension_active){
        //the user deactivate the sensing
        process_exit(&sensing_process);
      }

      break;
    default:
      printf("Error: command not recognized\n");
  }
}


//Be careful to the order
static const struct broadcast_callbacks broadcast_call = {broadcast_recv, NULL}; 
static struct broadcast_conn broadcast;


PROCESS_THREAD(main_process, ev, data){
  PROCESS_EXITHANDLER(broadcast_close(&broadcast)); 

  PROCESS_BEGIN();
  SENSORS_ACTIVATE(button_sensor);

  broadcast_open(&broadcast, 128, &broadcast_call);
  //extension off by default
  leds_on(LEDS_RED);

  while(1){
    
    PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);

    //refuse to modify the temperature if the extension is not active
    if (!extension_active){
      printf("Command rejacted: activate the extension first\n");
      continue;
    }
      
    temperature++;

    //temp [18;24]
    if (temperature>24)
      temperature = 18;

    printf("temperature = %d\n", temperature);
  }

  PROCESS_END();
}


PROCESS_THREAD(sensing_process, ev, data)
{
  static struct etimer sensing_timer;

  PROCESS_BEGIN();
  SENSORS_ACTIVATE(phidgets);

  etimer_set(&sensing_timer, CLOCK_SECOND);
  //led red=ON ->stop to use the extension
  leds_off(LEDS_RED);

  while(1) {
    PROCESS_WAIT_EVENT();
    if (etimer_expired(&sensing_timer)){
      double aux;

      //reset the timer
      etimer_reset(&sensing_timer);
      //while sensing the blue led toggle
      leds_toggle(LEDS_BLUE);

      //obtain the measurement
      aux = phidgets.value(PHIDGET5V_1);

      printf("Phidget 5V 1:%d\n", (int)aux);
      
      aux *= 3300;  // battery ref value typical when connected over USB
      //aux = (aux>>12); //divide by 4096
      aux /= 4096;
      aux *= 5000;  //External voltage reference as
      aux /= 3000;  //internal voltage divider for the 5V phidget (ADC0,
                    //ADC3) has 5:3 relationship

      printf("aux:%d\n", (int)aux);
      int a = (int)aux;
      int db = 20 * log10f((double)a);
      
      printf ("db %d\n", db);
      
    }
     
    if (ev == PROCESS_EVENT_EXIT){
      //the user deactivate the extension behaviour

      //led red=ON ->stop to use the extension
      leds_off(LEDS_BLUE);
      leds_on(LEDS_RED);

      etimer_stop(&sensing_timer);
      //deactivate the sensor
      SENSORS_ACTIVATE(phidgets);
    }
  }
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
