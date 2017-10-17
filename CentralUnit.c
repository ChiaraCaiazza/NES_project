#include "contiki.h"
#include "net/rime/rime.h"
#include "sys/etimer.h"
#include "stdio.h"

#include "dev/button-sensor.h"


#define MAX_RETRANSMISSIONS 5

//unlocked (0) by default
static int alarm_state = 0;
//gate locked(1) by default
static int gate_locked = 1;

static int command = 0;
static int button_pressed = 0;

PROCESS(handle_command_process, "Handle command process");
PROCESS(display_process, "Display the available commands");
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
  printf("runicast message received from %d.%d. Sequence number = %d\n", sender_addr->u8[0], sender_addr->u8[1], seqno);
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
static struct runicast_conn runicast_node2;



PROCESS_THREAD(handle_command_process, ev, data){

  static struct etimer et;

  /*
    triggered only when there is a PROCESS_EXIT event, in this case we don't require to keep
    the connection open
  */
  PROCESS_EXITHANDLER(broadcast_close(&broadcast));
  PROCESS_EXITHANDLER(runicast_close(&runicast_node2));


  PROCESS_BEGIN();

  SENSORS_ACTIVATE(button_sensor);

  /*
    we open the connection. The second parameter is the channel on which the node will 
    communicate.
    it is a sort of port
  */
  broadcast_open(&broadcast, 129, &broadcast_call);
  //open a runicast connection with Node2 (garden) over the channel 130
  runicast_open(&runicast_node2,130, &runicast_calls); 

  while(1) {

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

        if (alarm_state && button_pressed != 1){
          button_pressed = 0;
          printf("Command rejected. Deactivate the alarm first.\n");
        }
        else if(!runicast_is_transmitting(&runicast_node2)){
          command = button_pressed;
          button_pressed = 0;
          printf ("Command = %d.\n", command);

          linkaddr_t recv;
          char char_command = '0' + command;
          packetbuf_copyfrom(&char_command, sizeof(char_command));

          switch(command){
            case 1:
              //lock-unlock the alarm

              //change the state of the alarm
              alarm_state = (alarm_state == 0)?1:0;

              //send the command in broadcast
              broadcast_send(&broadcast);

              break;
            case 2:
              //sent a runicast message to node 2 so that the gate could be 
              //opened/closed
            
              //the receiver has rime address 2.0
              recv.u8[0] = 2;
              recv.u8[1] = 0;

              //then we call the runicast send    
              runicast_send(&runicast_node2, &recv, MAX_RETRANSMISSIONS);

              break;
            case 3:
              //open and automatically close both the door and the gate
              broadcast_send(&broadcast);
              break;
            case 4:
             
               break;
            case 5:
             
               break;
            default:
              //
                printf("Error: command not recognized.\n");

            }
          }

          //display the available commands
          process_start(&display_process, NULL);
      }
  }

  PROCESS_END();
}


PROCESS_THREAD(display_process, ev, data){
  PROCESS_BEGIN();

  if (alarm_state)
    printf("1- Deactivate the alarm\n");
  else{
    printf("1- Activate the alarm\n");

    if (gate_locked)
      printf("2- Unlock the gate\n");
    else
      printf("2- Lock the gate\n");

    printf("3- Open (and automatically close) both the door and the gate in order to let a guest enter\n");
    printf("4- Obtain the average of the last 5 temperature values\n");
    printf("5- Obtain the external light\n");
  }

  PROCESS_END();
}