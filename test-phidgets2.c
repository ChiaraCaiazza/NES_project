#include <stdio.h>
#include "contiki.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "dev/z1-phidgets.h"
#include "sys/etimer.h"
#include "math.h"
/*---------------------------------------------------------------------------*/
PROCESS(sensing_process, "Test Button & ADC");
AUTOSTART_PROCESSES(&sensing_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(sensing_process, ev, data)
{
  static struct etimer sensing_timer;
  PROCESS_BEGIN();
  SENSORS_ACTIVATE(phidgets);

  etimer_set(&sensing_timer, CLOCK_SECOND);

  while(1) {
    printf("Please press the User Button\n");

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&sensing_timer));
    etimer_reset(&sensing_timer);

    leds_toggle(LEDS_GREEN);



    double aux;
    aux = phidgets.value(PHIDGET5V_1);

    printf("Phidget 5V 1:%d\n", (int)aux);
    /*printf("Phidget 5V 2:%d\n", phidgets.value(PHIDGET5V_2));
    printf("Phidget 3V 1:%d\n", phidgets.value(PHIDGET3V_1));
    printf("Phidget 3V 2:%d\n", phidgets.value(PHIDGET3V_2));*/

	
    
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
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
