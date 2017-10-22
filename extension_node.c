#include <stdio.h>
#include "contiki.h"
#include "dev/sht11/sht11-sensor.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "dev/z1-phidgets.h"
#include "sys/etimer.h"
#include "math.h"
#include "net/rime/rime.h"
#include "dev/light-sensor.h"
#include "core/lib/random.h"


#define SAMPLE_TO_DEACTIVATE 120
#define SAMPLE_TO_ACTIVATE 30

//extension off by default(0)
static int extension_active = 0;
int sample_noise = 0;
int sample_silence = 0;
static int temperature = 20;
int human_sensed = 0;
int sample_to_be_activated = -1;

/*---------------------------------------------------------------------------*/
PROCESS(sensing_process, "Test Button & ADC");
PROCESS(temperature_monitoring_process, "Temperature monitoring process");
PROCESS(main_process, "Main process");
AUTOSTART_PROCESSES(&main_process);
/*---------------------------------------------------------------------------*/
static void broadcast_recv(struct broadcast_conn *c, const linkaddr_t *senderAddr){
  printf("broadcast message received from %d.%d.\n", senderAddr->u8[0], 
        senderAddr->u8[1]);

  int* data = (int*)packetbuf_dataptr();
  int command = *data;

  switch (command){
    case 6:
      //the user activate/deactivate the extension

      //update the state
      extension_active = (extension_active)?0:1;
      human_sensed = 0;

      if (extension_active){
        //the user activate the extension
        process_start(&sensing_process, NULL);
      }

      if (!extension_active){
        //the user deactivate the sensing
        process_exit(&sensing_process);
        process_exit(&temperature_monitoring_process);
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
  leds_off(LEDS_GREEN);

  while(1){
    PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);

    //refuse to modify the temperature if the extension is not active
    if (!extension_active){
      printf("Command rejected: activate the extension first\n");
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


      SENSORS_ACTIVATE(phidgets);
      //obtain the measurement
      aux = phidgets.value(PHIDGET5V_1);
      //deactivate the sensor
      SENSORS_DEACTIVATE(phidgets);
      
      aux *= 3300;  // battery ref value typical when connected over USB
      //aux = (aux>>12); //divide by 4096
      aux /= 4096;
      aux *= 5000;  //External voltage reference as
      aux /= 3000;  //internal voltage divider for the 5V phidget (ADC0,
                    //ADC3) has 5:3 relationship

      //printf("aux:%d\n", (int)aux);
      int a = (int)aux;
      int db = 20 * log10f((double)a);
      

/*******************************************************************************/
      if (sample_to_be_activated == -1){
        //RANDOM_MAX = 65535 -> random_rand()/6000 at most 10 values
        //[-5, 5]
        //if I obtain 0 then i will use a fixed db value
        int random_number = (int)random_rand()/6000;
        //printf("Random=%d sample_to_be_activated=%d\n", random_number, sample_to_be_activated);

        if (random_number == 0){
          printf("random = 0!\n");
          if (!human_sensed)
            sample_to_be_activated = 1;
          else
            sample_to_be_activated = 0;
        }
      }

      if (sample_to_be_activated == 1)
        db = 80;
      if (sample_to_be_activated == 0)
        db = 10;

      //printf ("db %d\n", db);
/*********************************************************************************/      

      if (db<=20 && human_sensed){
        sample_silence++;
        sample_noise = 0;

        if (sample_silence >= SAMPLE_TO_DEACTIVATE){
          sample_to_be_activated = -1;

          //there is none in the room
          human_sensed = 0;
          //the green led is on if someone is inside
          leds_off(LEDS_GREEN);

          //monitors the temperature is not usefull anymore
          process_exit(&temperature_monitoring_process);

          //during the night we turn off the tv's leds
          //sensing for the minimun amount of time
          SENSORS_ACTIVATE(light_sensor);
          //normalized sample of light
          int light = 10*light_sensor.value(LIGHT_SENSOR_PHOTOSYNTHETIC)/7;
          printf("Sensed light %d lux. ", light);
          SENSORS_DEACTIVATE(light_sensor);

          if (light < 10)
            printf("Leds = off\n");
          else 
            printf("Nothing to do.\n");
        }
      }

      if (db>20 && !human_sensed){
        sample_noise++;
        sample_silence = 0;

        if (sample_noise >= SAMPLE_TO_ACTIVATE){
          sample_to_be_activated = -1;

          //someone is inside the room
          human_sensed = 1;
          //the green led is on if someone is inside
          leds_on(LEDS_GREEN);

          process_start(&temperature_monitoring_process, NULL);
        }
      } 
    }
     
    if (ev == PROCESS_EVENT_EXIT){
      //the user deactivate the extension behaviour

      //led red=ON ->stop to use the extension
      leds_off(LEDS_BLUE);
      leds_off(LEDS_GREEN);
      leds_on(LEDS_RED);

      etimer_stop(&sensing_timer);

      sample_noise = 0;
      sample_silence = 0;
      sample_to_be_activated = -1;
    }
  }
  PROCESS_END();
}


PROCESS_THREAD(temperature_monitoring_process, ev, data){
  static struct etimer temperature_timer;
  int temp;

  PROCESS_BEGIN();
  printf("Someone is inside\n");

  //check the temperature every 10 seconds
  etimer_set(&temperature_timer, CLOCK_SECOND*10);

  while(1){
    if (ev == sensors_event && data != &button_sensor)
      continue;

    if (human_sensed && extension_active){ 

      //the sensor has to stay active for the minimum amount of time
      SENSORS_ACTIVATE(sht11_sensor);
      //actual (normalized) temp sample (-39 is the default value)
      temp = (sht11_sensor.value(SHT11_SENSOR_TEMP)/10-396)/10;
      //now the desired temperature is the default value
      temp = temp + 39 + temperature; 
      //RANDOM_MAX = 65535 -> random_rand()/10000 at most 6 values
      //             +/-3°(more or less)
      temp += (int)random_rand()/10000;
      //stop the sensing phase
      SENSORS_DEACTIVATE(sht11_sensor);
    
      printf ("Desired temperature = %d. Actual temperature = %d.", 
          temperature, temp);
      if (temp > (temperature + 1) || temp < (temperature - 1))
        printf(" Activate the air conditioning system.\n");
      else
        printf(" Nothing to do.\n");
    }

    PROCESS_WAIT_EVENT(); 
    
    if (etimer_expired(&temperature_timer)){
      //every 10 seconds!!
      etimer_reset(&temperature_timer);
    }

    if (ev == PROCESS_EVENT_EXIT){
      etimer_stop(&temperature_timer);
      printf("Air conditioner = off\n");
    }
  }

  PROCESS_END();
}