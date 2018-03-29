/* Station Interlock Controller

   Anthony Good
   K3NG
   anthony.good@gmail.com



*/
#include <EEPROM.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

#define CODE_VERSION "1.0.2016072602"

#define FEATURE_WEB_SERVER
#define FEATURE_UDP
// #define DEBUG_STATION_INTERLOCK
// #define DEBUG_UDP
// #define DEBUG_UDP_WRITE
// #define DEBUG_UDP_PACKET_SEND_TIME

// pin definitions
#define pin_input_tx_request_1 3
#define pin_input_tx_request_2 5  // pin 4 is SD CS
#define pin_input_tx_request_3 6
#define pin_input_tx_request_4 7
#define pin_input_tx_request_5 A0
#define pin_input_tx_request_6 A1
#define pin_input_tx_request_7 A2
#define pin_input_tx_request_8 A3
#define pin_input_tx_request_9 22
#define pin_input_tx_request_10 23
#define pin_input_tx_request_11 24
#define pin_input_tx_request_12 25
#define pin_input_tx_request_13 26
#define pin_input_tx_request_14 27
#define pin_input_tx_request_15 28
#define pin_input_tx_request_16 29  

#define pin_output_tx_inhibit_1 8
#define pin_output_tx_inhibit_2 9
#define pin_output_tx_inhibit_3 11   // pin 10 is Ethernet CS
#define pin_output_tx_inhibit_4 12
#define pin_output_tx_inhibit_5 A4
#define pin_output_tx_inhibit_6 A5
#define pin_output_tx_inhibit_7 A6
#define pin_output_tx_inhibit_8 A7
#define pin_output_tx_inhibit_9 30
#define pin_output_tx_inhibit_10 31
#define pin_output_tx_inhibit_11 32
#define pin_output_tx_inhibit_12 33
#define pin_output_tx_inhibit_13 34
#define pin_output_tx_inhibit_14 35
#define pin_output_tx_inhibit_15 36
#define pin_output_tx_inhibit_16 37

// settings
#define NUMBER_OF_STATIONS 8
#define INPUT_ACTIVE_DEFAULT LOW
#define INPUT_INACTIVE_DEFAULT HIGH
#define OUTPUT_ACTIVE_DEFAULT HIGH
#define OUTPUT_INACTIVE_DEFAULT LOW
#define UDP_LISTENER_PORT 8888
#define UDP_DEFAULT_BROADCAST_PORT 1234
#define EEPROM_MAGIC_NUMBER 13           // change this to force EEPROM initialization
#define MAX_STATIONS 16


#define MSG_BRQ 1
#define MSG_BOK 2
#define MSG_BNA 3
#define MSG_BUN 4
#define MSG_BST 5


// variables


struct config_t {  
  uint8_t input_active_state[MAX_STATIONS+1];
  uint8_t output_active_state[MAX_STATIONS+1];
  uint8_t input_inactive_state[MAX_STATIONS+1];
  uint8_t output_inactive_state[MAX_STATIONS+1];
  uint8_t station_enabled[MAX_STATIONS+1];
  char station_name[16][MAX_STATIONS+1];
  unsigned int udp_broadcast_port;
  uint8_t ip[4];
  uint8_t gateway[4];  
  uint8_t subnet[4];       
} configuration;

uint8_t state_interlock = 0;
int pin_input[MAX_STATIONS+1];
int pin_output[MAX_STATIONS+1];
byte config_dirty = 0;
unsigned long last_config_write = 0;


#if defined(DEBUG_STATION_INTERLOCK) || defined(DEBUG_UDP) || defined(DEBUG_UDP_WRITE) || defined(DEBUG_UDP_PACKET_SEND_TIME)
  #define DEBUG_SERIAL_PORT_ON
  #include "debug.h"
  DebugClass debug;
  HardwareSerial * debug_serial_port;
  uint8_t debug_mode = 1;
  #define DEBUG_SERIAL_PORT &Serial
  #define DEBUG_SERIAL_PORT_BAUD_RATE 115200
#endif //DEBUG_STATION_INTERLOCK

uint8_t default_ip[] = {192,168,1,178};                      // default IP address ("192.168.1.178")
uint8_t default_gateway[] = {192,168,1,1};                   // default gateway
uint8_t default_subnet[] = {255,255,255,0};                  // default subnet mask
uint8_t ip_broadcast[4];  

#if defined(FEATURE_WEB_SERVER)
  #define MAX_WEB_REQUEST 512
  uint8_t mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };   // default physical mac address
  String readString;
  uint8_t valid_request = 0;
  EthernetServer server(80);                             // default server port 
  #define MAX_PARSE_RESULTS 32
  struct parse_get_result_t{
    String parameter;
    String value_string;
    long value_long;
  };
  struct parse_get_result_t parse_get_results[MAX_PARSE_RESULTS];
  int parse_get_results_index = 0;
  uint8_t restart_networking = 0;
#endif //FEATURE_WEB_SERVER

#if defined(FEATURE_UDP)
  unsigned int udp_listener_port = UDP_LISTENER_PORT;
  EthernetUDP Udp;
#endif //FEATURE_UDP

#if defined(DEBUG_UDP_WRITE)
  uint8_t udp_write_debug_column = 0;
  int udp_write_byte_counter = 0;
#endif

//-------------------------------------------------------------------------------------------------------

void setup(){

  initialize_settings();  
  initialize_serial(); 
  initialize_pins();
  #if defined(FEATURE_WEB_SERVER) || defined(FEATURE_UDP)
    initialize_ethernet();
  #endif
  #if defined(FEATURE_WEB_SERVER)
    initialize_web_server();
  #endif
  #if defined(FEATURE_UDP)
    initialize_udp();
  #endif

}

//-------------------------------------------------------------------------------------------------------

void loop(){

  check_inputs();
  check_for_dirty_configuration();
  #if defined(FEATURE_WEB_SERVER)
    service_web_server();
    check_for_network_restart();
  #endif //FEATURE_WEB_SERVER
  #if defined(FEATURE_UDP)
    service_udp();
  #endif    
}



//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER) || defined(FEATURE_UDP)

  void initialize_ethernet(){

    Ethernet.begin(mac, configuration.ip, configuration.gateway, configuration.subnet);

  }

#endif

//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_UDP)

  void initialize_udp(){

    int udpbegin_result = Udp.begin(udp_listener_port);

    #if defined(DEBUG_SERIAL_PORT_ON)
      if (!udpbegin_result){
        debug_serial_port->println("initialize_udp: Udp.begin error");
      }
    #endif

  }

#endif //FEATURE_UDP
//-------------------------------------------------------------------------------------------------------

void initialize_settings(){

  if (!read_settings_from_eeprom()){
    for (int x = 1;x < (MAX_STATIONS+1);x++){
      configuration.input_active_state[x] = INPUT_ACTIVE_DEFAULT;
      configuration.input_inactive_state[x] = INPUT_INACTIVE_DEFAULT;
      configuration.output_active_state[x] = OUTPUT_ACTIVE_DEFAULT;
      configuration.output_inactive_state[x] = OUTPUT_INACTIVE_DEFAULT; 
      if (x > NUMBER_OF_STATIONS){
        configuration.station_enabled[x] = 0;
      } else {
        configuration.station_enabled[x] = 1;
      }
      configuration.station_name[0][x] = 'S';
      configuration.station_name[1][x] = 'T';
      configuration.station_name[2][x] = 'A';
      configuration.station_name[3][x] = 'T';
      configuration.station_name[4][x] = 'I';
      configuration.station_name[5][x] = 'O';      
      configuration.station_name[6][x] = 'N';
      if (x < 10){
        configuration.station_name[7][x] = x + 48;      
        configuration.station_name[8][x] = 0;   
      } else {
        configuration.station_name[7][x] = '1';
        configuration.station_name[8][x] = (x - 10) + 48;
        configuration.station_name[9][x] = 0;
      }   
    }
    for (int x = 0;x < 4;x++){
      configuration.ip[x] = default_ip[x];  
      configuration.gateway[x] = default_gateway[x];
      configuration.subnet[x] = default_subnet[x]; 
    }      


    configuration.udp_broadcast_port = UDP_DEFAULT_BROADCAST_PORT;

    write_settings_to_eeprom(1);
  }

  update_ip_broadcast_address();

}

//-------------------------------------------------------------------------------------------------------

void initialize_serial(){

  #if defined(DEBUG_SERIAL_PORT_ON)
    debug_serial_port = DEBUG_SERIAL_PORT;
    debug_serial_port->begin(DEBUG_SERIAL_PORT_BAUD_RATE);
    debug.println(F("initialize_serial: debug serial port open"));
  #endif

} 


//-------------------------------------------------------------------------------------------------------


void initialize_pins(){

  // inputs

  if (pin_input_tx_request_1){
    pinMode(pin_input_tx_request_1, INPUT);
    digitalWrite(pin_input_tx_request_1, configuration.input_inactive_state[1]);
    pin_input[1] = pin_input_tx_request_1;
  }
  if ((pin_input_tx_request_2) && (NUMBER_OF_STATIONS > 1)){
    pinMode(pin_input_tx_request_2, INPUT);
    digitalWrite(pin_input_tx_request_2, configuration.input_inactive_state[2]);
    pin_input[2] = pin_input_tx_request_2;
  }
  if ((pin_input_tx_request_3) && (NUMBER_OF_STATIONS > 2)){
    pinMode(pin_input_tx_request_3, INPUT);
    digitalWrite(pin_input_tx_request_3, configuration.input_inactive_state[3]);
    pin_input[3] = pin_input_tx_request_3;
  }
  if ((pin_input_tx_request_4) && (NUMBER_OF_STATIONS > 3)){
    pinMode(pin_input_tx_request_4, INPUT);
    digitalWrite(pin_input_tx_request_4, configuration.input_inactive_state[4]);
    pin_input[4] = pin_input_tx_request_4;
  }  
  if ((pin_input_tx_request_5) && (NUMBER_OF_STATIONS > 4)){
    pinMode(pin_input_tx_request_5, INPUT);
    digitalWrite(pin_input_tx_request_5, configuration.input_inactive_state[5]);
    pin_input[5] = pin_input_tx_request_5;
  }  
  if ((pin_input_tx_request_6) && (NUMBER_OF_STATIONS > 5)){
    pinMode(pin_input_tx_request_6, INPUT);
    digitalWrite(pin_input_tx_request_6, configuration.input_inactive_state[6]);
    pin_input[6] = pin_input_tx_request_6;
  }  
  if ((pin_input_tx_request_7) && (NUMBER_OF_STATIONS > 6)){
    pinMode(pin_input_tx_request_7, INPUT);
    digitalWrite(pin_input_tx_request_7, configuration.input_inactive_state[7]);
    pin_input[7] = pin_input_tx_request_7;
  }  
  if ((pin_input_tx_request_8) && (NUMBER_OF_STATIONS > 7)){
    pinMode(pin_input_tx_request_8, INPUT);
    digitalWrite(pin_input_tx_request_8, configuration.input_inactive_state[8]);
    pin_input[8] = pin_input_tx_request_8;
  } 
  if ((pin_input_tx_request_9)  && (NUMBER_OF_STATIONS > 8)){
    pinMode(pin_input_tx_request_9, INPUT);
    digitalWrite(pin_input_tx_request_9, configuration.input_inactive_state[9]);
    pin_input[9] = pin_input_tx_request_9;
  }  
  if ((pin_input_tx_request_10)  && (NUMBER_OF_STATIONS > 9)){
    pinMode(pin_input_tx_request_10, INPUT);
    digitalWrite(pin_input_tx_request_10, configuration.input_inactive_state[10]);
    pin_input[10] = pin_input_tx_request_10;
  }  
  if ((pin_input_tx_request_11)  && (NUMBER_OF_STATIONS > 10)){
    pinMode(pin_input_tx_request_11, INPUT);
    digitalWrite(pin_input_tx_request_11, configuration.input_inactive_state[11]);
    pin_input[11] = pin_input_tx_request_11;
  }  
  if ((pin_input_tx_request_12)  && (NUMBER_OF_STATIONS > 11)){
    pinMode(pin_input_tx_request_12, INPUT);
    digitalWrite(pin_input_tx_request_12, configuration.input_inactive_state[12]);
    pin_input[12] = pin_input_tx_request_12;
  }  
  if ((pin_input_tx_request_13)  && (NUMBER_OF_STATIONS > 12)){
    pinMode(pin_input_tx_request_13, INPUT);
    digitalWrite(pin_input_tx_request_13, configuration.input_inactive_state[13]);
    pin_input[13] = pin_input_tx_request_13;
  }  
  if ((pin_input_tx_request_14)  && (NUMBER_OF_STATIONS > 13)){
    pinMode(pin_input_tx_request_14, INPUT);
    digitalWrite(pin_input_tx_request_14, configuration.input_inactive_state[14]);
    pin_input[14] = pin_input_tx_request_14;
  }  
  if ((pin_input_tx_request_15)  && (NUMBER_OF_STATIONS > 14)){
    pinMode(pin_input_tx_request_15, INPUT);
    digitalWrite(pin_input_tx_request_15, configuration.input_inactive_state[15]);
    pin_input[15] = pin_input_tx_request_15;
  }  
  if ((pin_input_tx_request_16)  && (NUMBER_OF_STATIONS > 15)){
    pinMode(pin_input_tx_request_16, INPUT);
    digitalWrite(pin_input_tx_request_16, configuration.input_inactive_state[16]);
    pin_input[16] = pin_input_tx_request_16;
  }                     

  // outputs

  if (pin_output_tx_inhibit_1){
    pinMode(pin_output_tx_inhibit_1, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_1, configuration.output_inactive_state[1]);
    pin_output[1] = pin_output_tx_inhibit_1;
  }
  if ((pin_output_tx_inhibit_2) && (NUMBER_OF_STATIONS > 1)){
    pinMode(pin_output_tx_inhibit_2, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_2, configuration.output_inactive_state[2]);
    pin_output[2] = pin_output_tx_inhibit_2;
  }
  if ((pin_output_tx_inhibit_3) && (NUMBER_OF_STATIONS > 2)){
    pinMode(pin_output_tx_inhibit_3, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_3, configuration.output_inactive_state[3]);
    pin_output[3] = pin_output_tx_inhibit_3;
  }
  if ((pin_output_tx_inhibit_4) && (NUMBER_OF_STATIONS > 3)){
    pinMode(pin_output_tx_inhibit_4, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_4, configuration.output_inactive_state[4]);
    pin_output[4] = pin_output_tx_inhibit_4;
  }
  if ((pin_output_tx_inhibit_5)  && (NUMBER_OF_STATIONS > 4)){
    pinMode(pin_output_tx_inhibit_5, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_5, configuration.output_inactive_state[5]);
    pin_output[5] = pin_output_tx_inhibit_5;
  }  
  if ((pin_output_tx_inhibit_6)  && (NUMBER_OF_STATIONS > 5)){
    pinMode(pin_output_tx_inhibit_6, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_6, configuration.output_inactive_state[6]);
    pin_output[6] = pin_output_tx_inhibit_6;
  }  
  if ((pin_output_tx_inhibit_7)  && (NUMBER_OF_STATIONS > 6)){
    pinMode(pin_output_tx_inhibit_7, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_7, configuration.output_inactive_state[7]);
    pin_output[7] = pin_output_tx_inhibit_7;
  }  
  if ((pin_output_tx_inhibit_8)  && (NUMBER_OF_STATIONS > 7)){
    pinMode(pin_output_tx_inhibit_8, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_8, configuration.output_inactive_state[8]);
    pin_output[8] = pin_output_tx_inhibit_8;
  } 
  if ((pin_output_tx_inhibit_9)  && (NUMBER_OF_STATIONS > 8)){
    pinMode(pin_output_tx_inhibit_9, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_9, configuration.output_inactive_state[9]);
    pin_output[9] = pin_output_tx_inhibit_9;
  }  
  if ((pin_output_tx_inhibit_10)  && (NUMBER_OF_STATIONS > 9)){
    pinMode(pin_output_tx_inhibit_10, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_10, configuration.output_inactive_state[10]);
    pin_output[10] = pin_output_tx_inhibit_10;
  }  
  if ((pin_output_tx_inhibit_11)  && (NUMBER_OF_STATIONS > 10)){
    pinMode(pin_output_tx_inhibit_11, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_11, configuration.output_inactive_state[11]);
    pin_output[11] = pin_output_tx_inhibit_11;
  }  
  if ((pin_output_tx_inhibit_12)  && (NUMBER_OF_STATIONS > 11)){
    pinMode(pin_output_tx_inhibit_12, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_12, configuration.output_inactive_state[12]);
    pin_output[12] = pin_output_tx_inhibit_12;
  }  
  if ((pin_output_tx_inhibit_13)  && (NUMBER_OF_STATIONS > 12)){
    pinMode(pin_output_tx_inhibit_13, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_13, configuration.output_inactive_state[13]);
    pin_output[13] = pin_output_tx_inhibit_13;
  }  
  if ((pin_output_tx_inhibit_14)  && (NUMBER_OF_STATIONS > 13)){
    pinMode(pin_output_tx_inhibit_14, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_14, configuration.output_inactive_state[14]);
    pin_output[14] = pin_output_tx_inhibit_14;
  }  
  if ((pin_output_tx_inhibit_15)  && (NUMBER_OF_STATIONS > 14)){
    pinMode(pin_output_tx_inhibit_15, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_15, configuration.output_inactive_state[15]);
    pin_output[15] = pin_output_tx_inhibit_15;
  }  
  if ((pin_output_tx_inhibit_16)  && (NUMBER_OF_STATIONS > 15)){
    pinMode(pin_output_tx_inhibit_16, OUTPUT);
    digitalWrite(pin_output_tx_inhibit_16, configuration.output_inactive_state[16]);
    pin_output[16] = pin_output_tx_inhibit_16;
  }


}



//-------------------------------------------------------------------------------------------------------

void check_inputs(){

  uint8_t pin_read = 0;
  uint8_t previous_interlock_state = state_interlock;

  if (state_interlock == 0){
    for (int x = 1;x < (NUMBER_OF_STATIONS+1);x++){
      pin_read = digitalRead(pin_input[x]);
      if ((pin_read == configuration.input_active_state[x]) && (configuration.station_enabled[x] == 1)){
        // activate all the other TX inhibits
        for (int y = 1;y < (NUMBER_OF_STATIONS+1);y++){
          if (y != x){
            digitalWrite(pin_output[y], configuration.output_active_state[y]);
          }
        }
      	state_interlock = x;
      	x = NUMBER_OF_STATIONS + 1;
        #if defined(FEATURE_UDP)
      	  send_udp_packets(previous_interlock_state,state_interlock);
        #endif
      }
    }
  } else {
    pin_read = digitalRead(pin_input[state_interlock]);
    if (pin_read == configuration.input_inactive_state[state_interlock]){
      // take all tx inhibit outputs to inactive
      for (int x = 1;x < (NUMBER_OF_STATIONS+1);x++){
        digitalWrite(pin_output[x], configuration.output_inactive_state[x]);
      }
      state_interlock = 0;  	
      #if defined(FEATURE_UDP)
        send_udp_packets(previous_interlock_state,state_interlock);
      #endif      
    }
  }  

}


//-------------------------------------------------------------------------------------------------------
void write_settings_to_eeprom(int initialize_eeprom) {  
 
  
  if (initialize_eeprom) {
    //configuration.magic_number = eeprom_magic_number;
    EEPROM.write(0,EEPROM_MAGIC_NUMBER); 
  }

  const byte* p = (const byte*)(const void*)&configuration;
  unsigned int i;
  int ee = 1;  // starting point of configuration struct
  for (i = 0; i < sizeof(configuration); i++){
    EEPROM.write(ee++, *p++);  
  }
  
  
  config_dirty = 0;
  
  
}

//-------------------------------------------------------------------------------------------------------

int read_settings_from_eeprom() {

  // returns 1 if eeprom had valid settings, returns 0 if eeprom needs initialized
  


    if (EEPROM.read(0) == EEPROM_MAGIC_NUMBER){
    
      byte* p = (byte*)(void*)&configuration;
      unsigned int i;
      int ee = 1; // starting point of configuration struct
      for (i = 0; i < sizeof(configuration); i++){
        *p++ = EEPROM.read(ee++);  
      }

      config_dirty = 0;

      return 1;
    } else {
      return 0;
    }
  

  return 0;

}

//-------------------------------------------------------------------------------------------------------

void check_for_dirty_configuration()
{


  if ((config_dirty) && ((millis()-last_config_write) > 3000)) {
    write_settings_to_eeprom(0);
    last_config_write = millis();
    #ifdef DEBUG_STATION_INTERLOCK
      debug_serial_port->println(F("check_for_dirty_configuration: wrote config"));
    #endif
    config_dirty = 0;
  }

}

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void initialize_web_server(){


  
  server.begin();
  #ifdef DEBUG_STATION_INTERLOCK
    debug_serial_port->print(F("initialize_web_server: server is at "));
    debug_serial_port->println(Ethernet.localIP());
  #endif 
}
#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void check_for_network_restart(){

  if (restart_networking){
    initialize_web_server();
    restart_networking = 0;
  }
}
#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void service_web_server() {


  // Create a client connection
  EthernetClient client = server.available();
  if (client) {

    valid_request = 0;

    while (client.connected()){   
      if (client.available()){
        char c = client.read();
     
        //read char by char HTTP request
        if (readString.length() < MAX_WEB_REQUEST){
          //store characters to string
          readString += c;
          #if defined(DEBUG_STATION_INTERLOCK)
            debug.print(c);
          #endif //DEBUG_STATION_INTERLOCK  
        } else {
          // readString = "";
        }

        //has HTTP request ended?
        if (c == '\n'){ 

          #if defined(DEBUG_STATION_INTERLOCK)
            debug.println(readString); //print to serial monitor for debuging     
          #endif //DEBUG_STATION_INTERLOCK

          if (readString.startsWith("GET / ")){
            valid_request = 1;
            web_print_page_main_menu(client);
          }

          if (readString.startsWith("GET /About")){
            valid_request = 1;
            web_print_page_about(client);
          }


          if (readString.startsWith("GET /NetworkSettings")){
            valid_request = 1;
            // are there form results being posted?
            if (readString.indexOf("?ip0=") > 0){
              web_print_page_network_settings_process(client);
            } else {
              web_print_page_network_settings(client);
            }
          }

          if (readString.startsWith("GET /PortSettings1")){
            valid_request = 1;
            // are there form results being posted?
            if (readString.indexOf("?") > 0){
              web_print_page_port_settings_process(client); 
            } else {           
              web_print_page_port_settings(client,1);
            }
          }

          if (readString.startsWith("GET /PortSettings2")){
            valid_request = 1;
            // are there form results being posted?
            if (readString.indexOf("?") > 0){
              web_print_page_port_settings_process(client); 
            } else {           
              web_print_page_port_settings(client,2);
            }
          }          
          if (readString.startsWith("GET /PortSettings3")){
            valid_request = 1;
            // are there form results being posted?
            if (readString.indexOf("?") > 0){
              web_print_page_port_settings_process(client); 
            } else {           
              web_print_page_port_settings(client,3);
            }
          }

          if (readString.startsWith("GET /PortSettings4")){
            valid_request = 1;
            // are there form results being posted?
            if (readString.indexOf("?") > 0){
              web_print_page_port_settings_process(client); 
            } else {           
              web_print_page_port_settings(client,4);
            }
          } 

          if (readString.startsWith("GET /Status")){
            valid_request = 1;
            web_print_page_status(client); 
          }

          if (!valid_request){
            web_print_page_404(client);                      
          }

          delay(1);
          client.stop();
          readString = "";  
         }
       }
    }
  }

}
#endif //FEATURE_WEB_SERVER
//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)
void web_print_header(EthernetClient client){

  web_client_println(client,"HTTP/1.1 200 OK");
  web_client_println(client,"Content-Type: text/html");
  web_client_println(client,"");     
  web_client_println(client,"<HTML>");
  web_client_println(client,"<HEAD>");
  web_client_println(client,"<meta name='apple-mobile-web-app-capable' content='yes' />");
  web_client_println(client,"<meta name='apple-mobile-web-app-status-bar-style' content='black-translucent' />");


}
#endif //FEATURE_WEB_SERVER
//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)
void web_print_style_sheet(EthernetClient client){


  web_client_print(client,F("<style>body{margin:60px 0px; padding:0px;text-align:center;font-family:\"Trebuchet MS\", Arial, Helvetica, sans-serif;}h1{text-align: center;font-family:Arial, \"Trebuchet MS\", Helvetica,"));
  web_client_print(client,F("sans-serif;}h2{text-align: center;font-family:\"Trebuchet MS\", Arial, Helvetica, sans-serif;}"));

  // button-looking hyperlinks
  web_client_print(client,F("a.internal{text-decoration:none;width:75px;height:50px;border-color:black;border-top:2px solid;border-bottom:2px solid;border-right:2px solid;border-left:2px solid;border-radius:10px 10px 10px;"));
  web_client_print(client,F("-o-border-radius:10px 10px 10px;-webkit-border-radius:10px 10px 10px;font-family:\"Trebuchet MS\",Arial, Helvetica, sans-serif;"));
  web_client_print(client,F("-moz-border-radius:10px 10px 10px;background-color:#293F5E;padding:8px;text-align:center;}"));
  web_client_print(client,F("a.internal:link {color:white;}      /* unvisited link */ a.internal:visited {color:white;}  /* visited link */"));
  web_client_print(client,F(" a.internal:hover {color:white;}  /* mouse over link */ a.internal:active {color:white;}  /* selected link */"));

  // external hyperlinks
  web_client_print(client,F("a.external{"));
  web_client_print(client,F("font-family:\"Trebuchet MS\",Arial, Helvetica, sans-serif;"));
  web_client_print(client,F("text-align:center;}"));
  web_client_print(client,F("a.external:link {color:blue;}      /* unvisited link */ a.external:visited {color:purple;}  /* visited link */"));
  web_client_println(client,F(" a.external:hover {color:red;}  /* mouse over link */ a.external:active {color:green;}  /* selected link */ "));

  // ip address text blocks
  web_client_println(client,F(".addr {width: 30px; text-align:center }"));

  // ip port text blocks
  web_client_println(client,F(".ipprt {width: 45px; text-align:center }"));

  web_client_println(client,F(".container {display: flex;}"));

 /*for demo purposes only */
  web_client_println(client,F(".column {flex: 1; background: #f2f2f2; border: 1px solid #e6e6e6; box-sizing: border-box;}"));
  web_client_println(client,F(".column-1 {order: 1;} .column-2 {order: 2;} .column-3 {order: 3;} .column-4 {order: 4;} .column-5 {order: 5;}"));




  web_client_println(client,F("</style>"));

  // web_client_print(client,"<style>body{margin:60px 0px; padding:0px;text-align:center;}h1{text-align: center;font-family:Arial, \"Trebuchet MS\", Helvetica,");
  // web_client_print(client,"sans-serif;}h2{text-align: center;font-family:Arial, \"Trebuchet MS\", Helvetica, sans-serif;}");
  // web_client_print(client,"a{text-decoration:none;width:75px;height:50px;border-color:black;border-top:2px solid;border-bottom:2px solid;border-right:2px solid;border-left:2px solid;border-radius:10px 10px 10px;");
  // web_client_print(client,"-o-border-radius:10px 10px 10px;-webkit-border-radius:10px 10px 10px;font-family:\"Trebuchet MS\",Arial, Helvetica, sans-serif;");
  // web_client_print(client,"-moz-border-radius:10px 10px 10px;background-color:#293F5E;padding:8px;text-align:center;}");
  // web_client_print(client,"a:link {color:white;}      /* unvisited link */ a:visited {color:white;}  /* visited link */");
  // web_client_println(client," a:hover {color:white;}  /* mouse over link */ a:active {color:white;}  /* selected link */</style>");

}
#endif //FEATURE_WEB_SERVER
//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)
void web_print_home_link(EthernetClient client){


  web_client_println(client,"<br />");
  web_client_println(client,"<a href=\"\x2F\" class=\"internal\">Home</a><br />");


}
#endif //FEATURE_WEB_SERVER
//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)
void web_print_footer(EthernetClient client){


  web_client_println(client,"<br />");
  web_client_println(client,"</BODY>");
  web_client_println(client,"</HTML>");


}
#endif //FEATURE_WEB_SERVER
//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)
void web_print_title(EthernetClient client){


  web_client_println(client,"<TITLE>Station Interlock Controller</TITLE>");
  web_client_println(client,"</HEAD>");
  web_client_println(client,"<BODY>");


}
#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)

void web_print_page_network_settings(EthernetClient client){

  web_print_header(client);

  web_print_style_sheet(client);

  web_print_title(client);

  web_client_println(client,"<H1>Network Settings</H1>");
  web_client_println(client,"<hr />");
  web_client_println(client,"<br />");  

  // input form
  web_client_print(client,"<br><br><form>IP: <input type=\"text\" name=\"ip0\" class=\"addr\" value=\"");
  web_client_print(client,configuration.ip[0]);
  web_client_print(client,"\"");
  web_client_print(client,">.<input type=\"text\" name=\"ip1\" class=\"addr\" value=\"");
  web_client_print(client,configuration.ip[1]);
  web_client_print(client,"\"");  
  web_client_print(client,">.<input type=\"text\" name=\"ip2\" class=\"addr\" value=\"");
  web_client_print(client,configuration.ip[2]);
  web_client_print(client,"\"");  
  web_client_print(client,">.<input type=\"text\" name=\"ip3\" class=\"addr\" value=\"");
  web_client_print(client,configuration.ip[3]);
  web_client_println(client,"\">");  


  web_client_print(client,"<br><br>Gateway: <input type=\"text\" name=\"gw0\" class=\"addr\" value=\"");
  web_client_print(client,configuration.gateway[0]);
  web_client_print(client,"\"");
  web_client_print(client,">.<input type=\"text\" name=\"gw1\" class=\"addr\" value=\"");
  web_client_print(client,configuration.gateway[1]);
  web_client_print(client,"\"");  
  web_client_print(client,">.<input type=\"text\" name=\"gw2\" class=\"addr\" value=\"");
  web_client_print(client,configuration.gateway[2]);
  web_client_print(client,"\"");  
  web_client_print(client,">.<input type=\"text\" name=\"gw3\" class=\"addr\" value=\"");
  web_client_print(client,configuration.gateway[3]);
  web_client_println(client,"\">");

  web_client_print(client,"<br><br>Subnet Mask: <input type=\"text\" name=\"sn0\" class=\"addr\" value=\"");
  web_client_print(client,configuration.subnet[0]);
  web_client_print(client,"\"");
  web_client_print(client,">.<input type=\"text\" name=\"sn1\" class=\"addr\" value=\"");
  web_client_print(client,configuration.subnet[1]);
  web_client_print(client,"\"");  
  web_client_print(client,">.<input type=\"text\" name=\"sn2\" class=\"addr\" value=\"");
  web_client_print(client,configuration.subnet[2]);
  web_client_print(client,"\"");  
  web_client_print(client,">.<input type=\"text\" name=\"sn3\" class=\"addr\" value=\"");
  web_client_print(client,configuration.subnet[3]);
  web_client_println(client,"\">");

  web_client_print(client,"<br><br>UDP Broadcast Port: <input type=\"text\" name=\"ud\" class=\"ipprt\" value=\"");
  web_client_print(client,configuration.udp_broadcast_port);
  web_client_println(client,"\">");

  web_client_print(client,"<br><br>Broadcast: ");
  web_client_print(client,ip_broadcast[0]);
  web_client_print(client,".");
  web_client_print(client,ip_broadcast[1]);
  web_client_print(client,".");
  web_client_print(client,ip_broadcast[2]);
  web_client_print(client,".");
  web_client_print(client,ip_broadcast[3]);


  web_client_println(client,"<br><br><input type=\"submit\" value=\"Save\"></form>");

  web_print_home_link(client);
  
  web_print_footer(client); 

}


#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)

void web_print_page_404(EthernetClient client){

  web_client_println(client,"HTTP/1.1 404 NOT FOUND");
  web_client_println(client,"Content-Type: text/html");
  web_client_println(client,"");             
  web_client_println(client,"<HTML><HEAD></HEAD>");
  web_client_println(client,"<BODY>");
  web_client_println(client,"Sorry, dude.  Page not found.");
  web_print_home_link(client);            
  web_print_footer(client); 

}

#endif //FEATURE_WEB_SERVER
//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)

void web_print_page_about(EthernetClient client){

  web_print_header(client);

  web_client_println(client,"<meta http-equiv=\"refresh\" content=\"5\"/>");

  web_print_style_sheet(client);


  web_print_title(client);

  web_client_println(client,"<H1>About</H1>");
  web_client_println(client,"<hr />");
  web_client_println(client,"<br />"); 
  web_client_print(client,"Code Version: ");
  web_client_println(client,CODE_VERSION);
  web_client_println(client,"<br />");

  void* HP = malloc(4);
  if (HP){
    free (HP);
  }
  unsigned long free = (unsigned long)SP - (unsigned long)HP;

  // web_client_print(client,"Heap = 0x");
  // web_client_println(client,(unsigned long)HP,HEX);
  // web_client_println(client,"<br />");           
  // web_client_print(client,"Stack = 0x");
  // web_client_println(client,(unsigned long)SP,HEX);
  // web_client_println(client,"<br />");  

  // web_client_print(client,"Free Memory = 0x");
  // if (free < 0x0FFF){web_client_print(client,"0");}
  // web_client_println(client,(unsigned long)free,HEX);
  // web_client_println(client,"<br />");            
  web_client_print(client,free);
  web_client_println(client," bytes free<br/><br/>");


  unsigned long seconds = millis() / 1000L;

  int days = seconds / 86400L;
  seconds = seconds - (long(days) * 86400L);
  
  int hours = seconds / 3600L;
  seconds = seconds - (long(hours) * 3600L);
  
  int minutes = seconds / 60L;
  seconds = seconds - (minutes * 60);

  web_client_print(client,days);
  web_client_print(client,":");
  if (hours < 10) {web_client_print(client,"0");}
  web_client_print(client,hours);
  web_client_print(client,":");
  if (minutes < 10) {web_client_print(client,"0");}
  web_client_print(client,minutes);
  web_client_print(client,":");
  if (seconds < 10) {web_client_print(client,"0");}
  web_client_print(client,seconds);    
  web_client_println(client," dd:hh:mm:ss uptime<br />");

  web_client_println(client,"<br /><br /><br />Anthony Good, K3NG");
  web_client_println(client,"<br />anthony.good@gmail.com"); 
  web_client_println(client,"<br /><a href=\"http://blog.radioartisan.com/\"\" class=\"external\">Radio Artisan</a>");

  web_client_println(client,"<br /><br /><br />With design guidiance and testing by James Balls, M0CKE<br /><br />");

  web_print_home_link(client);

  web_print_footer(client);
} 

#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)
void parse_get(String str){

  String workstring = "";
  String parameter = "";
  String value = "";

  for(int x = 0;x < MAX_PARSE_RESULTS;x++){
    parse_get_results[x].parameter = "";
    parse_get_results[x].value_string = "";
    parse_get_results[x].value_long = 0;
  }
  parse_get_results_index = 0;

  #if defined(DEBUG_STATION_INTERLOCK)
    debug.print("parse_get: raw workstring: ");
    Serial.println(str);
  #endif  

  workstring = str.substring(str.indexOf("?")+1);

  #if defined(DEBUG_STATION_INTERLOCK)
    debug.print("parse_get: workstring: ");
    Serial.println(workstring);
  #endif  

  while(workstring.indexOf("=") > 0){
    parameter = workstring.substring(0,workstring.indexOf("="));
    if(workstring.indexOf("&") > 0){
      value = workstring.substring(workstring.indexOf("=")+1,workstring.indexOf("&"));
      workstring = workstring.substring(workstring.indexOf("&")+1);
    } else {
      value = workstring.substring(workstring.indexOf("=")+1,workstring.indexOf(" "));
      // value = workstring.substring(workstring.indexOf("=")+1);
      workstring = "";
    }
    #if defined(DEBUG_STATION_INTERLOCK)
      debug.print("parse_get: parameter: ");
      Serial.print(parameter);
      debug.print(" value: ");
      Serial.println(value);   
    #endif //DEBUG_STATION_INTERLOCK
    // value.trim();
    // parameter.trim();

    if (parse_get_results_index < MAX_PARSE_RESULTS){
      parse_get_results[parse_get_results_index].parameter = parameter;
      parse_get_results[parse_get_results_index].value_string = value;
      parse_get_results[parse_get_results_index].value_long = value.toInt();
      
      // Serial.print(parse_get_results_index);
      // Serial.print(":");      
      // Serial.print(parse_get_results[parse_get_results_index].parameter);
      // Serial.print(":");
      // Serial.print(parse_get_results[parse_get_results_index].value_string);
      // Serial.print(":");    
      // Serial.print(parse_get_results[parse_get_results_index].value_long);
      // Serial.println("$");

      parse_get_results_index++;
    }
  }


}
#endif //FEATURE_WEB_SERVER


//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)

void web_print_page_port_settings_process(EthernetClient client){

  uint8_t act[MAX_STATIONS+1];
  uint8_t inp[MAX_STATIONS+1];
  uint8_t out[MAX_STATIONS+1];
  uint8_t valid[MAX_STATIONS+1];
  String station_name[MAX_STATIONS+1];


  uint8_t invalid_data = 0;

  for (int x = 1; x < (MAX_STATIONS + 1);x++){valid[x] = 0;}

  parse_get(readString);
  if (parse_get_results_index){

    // this thing below could undoubtedly be written more efficiently, but this is a hobby and life is short

    for (int x = 0; x < parse_get_results_index; x++){
      if (parse_get_results[x].parameter == "act1"){act[1] = parse_get_results[x].value_long;}  
      if (parse_get_results[x].parameter == "act2"){act[2] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act3"){act[3] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act4"){act[4] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act5"){act[5] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act6"){act[6] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act7"){act[7] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act8"){act[8] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act9"){act[9] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act10"){act[10] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act11"){act[11] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act12"){act[12] = parse_get_results[x].value_long;}   
      if (parse_get_results[x].parameter == "act13"){act[13] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act14"){act[14] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act15"){act[15] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "act16"){act[16] = parse_get_results[x].value_long;}               

      if (parse_get_results[x].parameter == "inp1"){inp[1] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp2"){inp[2] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp3"){inp[3] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp4"){inp[4] = parse_get_results[x].value_long;} 
      if (parse_get_results[x].parameter == "inp5"){inp[5] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp6"){inp[6] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp7"){inp[7] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp8"){inp[8] = parse_get_results[x].value_long;} 
      if (parse_get_results[x].parameter == "inp9"){inp[9] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp10"){inp[10] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp11"){inp[11] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp12"){inp[12] = parse_get_results[x].value_long;} 
      if (parse_get_results[x].parameter == "inp13"){inp[13] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp14"){inp[14] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp15"){inp[15] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "inp16"){inp[16] = parse_get_results[x].value_long;}                   

      if (parse_get_results[x].parameter == "out1"){out[1] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out2"){out[2] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out3"){out[3] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out4"){out[4] = parse_get_results[x].value_long;} 
      if (parse_get_results[x].parameter == "out5"){out[5] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out6"){out[6] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out7"){out[7] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out8"){out[8] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out9"){out[9] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out10"){out[10] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out11"){out[11] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out12"){out[12] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out13"){out[13] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out14"){out[14] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out15"){out[15] = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "out16"){out[16] = parse_get_results[x].value_long;}                  

      if (parse_get_results[x].parameter == "sn1"){station_name[1] = parse_get_results[x].value_string;valid[1]=1;}
      if (parse_get_results[x].parameter == "sn2"){station_name[2] = parse_get_results[x].value_string;valid[2]=1;}
      if (parse_get_results[x].parameter == "sn3"){station_name[3] = parse_get_results[x].value_string;valid[3]=1;}
      if (parse_get_results[x].parameter == "sn4"){station_name[4] = parse_get_results[x].value_string;valid[4]=1;}
      if (parse_get_results[x].parameter == "sn5"){station_name[5] = parse_get_results[x].value_string;valid[5]=1;}
      if (parse_get_results[x].parameter == "sn6"){station_name[6] = parse_get_results[x].value_string;valid[6]=1;}
      if (parse_get_results[x].parameter == "sn7"){station_name[7] = parse_get_results[x].value_string;valid[7]=1;}
      if (parse_get_results[x].parameter == "sn8"){station_name[8] = parse_get_results[x].value_string;valid[8]=1;}  
      if (parse_get_results[x].parameter == "sn9"){station_name[9] = parse_get_results[x].value_string;valid[9]=1;}
      if (parse_get_results[x].parameter == "sn10"){station_name[10] = parse_get_results[x].value_string;valid[10]=1;}
      if (parse_get_results[x].parameter == "sn11"){station_name[11] = parse_get_results[x].value_string;valid[11]=1;}
      if (parse_get_results[x].parameter == "sn12"){station_name[12] = parse_get_results[x].value_string;valid[12]=1;}  
      if (parse_get_results[x].parameter == "sn13"){station_name[13] = parse_get_results[x].value_string;valid[13]=1;}
      if (parse_get_results[x].parameter == "sn14"){station_name[14] = parse_get_results[x].value_string;valid[14]=1;}
      if (parse_get_results[x].parameter == "sn15"){station_name[15] = parse_get_results[x].value_string;valid[15]=1;}
      if (parse_get_results[x].parameter == "sn16"){station_name[16] = parse_get_results[x].value_string;valid[16]=1;}  
                                                                   
    }



    // data validation

   // TODO: some actual data validation

    if (invalid_data){   

      web_print_header(client);
      web_print_style_sheet(client);
      web_print_title(client);
      web_client_println(client,"<br>Bad data!<br>");
      web_print_home_link(client);
      web_print_footer(client);

    } else {


      for (int x = 1; x < (NUMBER_OF_STATIONS + 1);x++){

        int y;

        if (valid[x]){

          for (y = 0;((y < (station_name[x].length()+1)) && (y < 31)); y++){
            configuration.station_name[y][x] = toupper(station_name[x].charAt(y));
          }
          configuration.station_name[y+1][x] = 0;


          if (act[x] == 1){
            configuration.station_enabled[x] = 1;
          } else {
            configuration.station_enabled[x] = 0;
          }
          if (inp[x] == 1){
            configuration.input_active_state[x] = HIGH;
            configuration.input_inactive_state[x] = LOW;
          } else {
            configuration.input_active_state[x] = LOW;
            configuration.input_inactive_state[x] = HIGH;
          }
          if (out[x] == 1){
            configuration.output_active_state[x] = HIGH;
            configuration.output_inactive_state[x] = LOW;
          } else {
            configuration.output_active_state[x] = LOW;
            configuration.output_inactive_state[x] = HIGH;
          }
        }
      }
                 

      web_print_header(client);
      web_client_print(client,"<meta http-equiv=\"refresh\" content=\"5; URL='http://");
      web_client_print(client,configuration.ip[0]);
      web_client_print(client,".");
      web_client_print(client,configuration.ip[1]);
      web_client_print(client,".");
      web_client_print(client,configuration.ip[2]);
      web_client_print(client,".");  
      web_client_print(client,configuration.ip[3]);                                                    
      web_client_println(client,"'\" />");
      web_print_style_sheet(client);
      web_print_title(client);
      web_client_println(client,"<br>Configuration saved<br>");
      web_print_home_link(client);
      web_print_footer(client);
      config_dirty = 1;
    }
  }
}
#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)

void web_print_page_port_settings(EthernetClient client,uint8_t group){


  uint8_t port_start = 0;
  uint8_t port_end = 0;

  switch(group){
    case 1: port_start = 1; port_end = 4; break;
    case 2: port_start = 5; port_end = 8; break;
    case 3: port_start = 9; port_end = 12; break;        
    case 4: port_start = 13; port_end = 16; break;
  }

  web_print_header(client);

  web_print_style_sheet(client);

  web_print_title(client);

  web_client_println(client,"<H1>Port Settings</H1>");
  web_client_println(client,"<hr />");
  web_client_println(client,"<br />");  

  // input form
  web_client_println(client,"<br><br><form>");
  ;  
  for (int x = port_start;x < (port_end+1) /*(NUMBER_OF_STATIONS + 1)*/;x++){
    
    if ((x % 2) == 0){
      web_client_println(client,"<div class=\"column column-2)\">");
    } else {
      web_client_println(client,"<div class=\"container\">");
      web_client_println(client,"<div class=\"column column-1)\">");
    }
    web_client_print(client,"<h2>Port ");
    web_client_print(client,x);
    web_client_print(client,"</h2><br>");


    // station name
    web_client_print(client,"Station Name ");
    web_client_print(client,"<input type=\"text\" name=\"sn");
    web_client_print(client,x);
    web_client_print(client,"\" value=\"");
    for (int y = 0;y < 16;y++){
      if (configuration.station_name[y][x] == 0){
        y = 32;
      } else {
        client.write(configuration.station_name[y][x]);
      }
    }       
    web_client_print(client,"\"><br>");

    // port active/inactive radio buttons
    web_client_print(client,"<input type=\"radio\" name=\"act");
    web_client_print(client,x);
    web_client_print(client,"\" value=\"1\" ");
    if (configuration.station_enabled[x]) {web_client_print(client,"checked");}
    web_client_print(client,">Enabled <input type=\"radio\" name=\"act");
    web_client_print(client,x);
    web_client_print(client,"\" value=\"0\" ");
    if (!(configuration.station_enabled[x])) {web_client_print(client,"checked");}
    web_client_print(client,">Disabled<br>");


    // input active state
    web_client_print(client,"TX Request Input Active State: <input type=\"radio\" name=\"inp"); 
    web_client_print(client,x);
    web_client_print(client,"\" value=\"1\" ");
    if (configuration.input_active_state[x] == HIGH) {web_client_print(client,"checked");}
    web_client_print(client,">HIGH <input type=\"radio\" name=\"inp");
    web_client_print(client,x);
    web_client_print(client,"\" value=\"0\" ");
    if (configuration.input_active_state[x] == LOW) {web_client_print(client,"checked");} 
    web_client_print(client,">LOW<br>");  

    // output active state
    web_client_print(client,"TX Inhibit Output Active State: <input type=\"radio\" name=\"out"); 
    web_client_print(client,x);
    web_client_print(client,"\" value=\"1\" ");
    if (configuration.output_active_state[x] == HIGH) {web_client_print(client,"checked");}
    web_client_print(client,">HIGH <input type=\"radio\" name=\"out");
    web_client_print(client,x);
    web_client_print(client,"\" value=\"0\" ");
    if (configuration.output_active_state[x] == LOW) {web_client_print(client,"checked");} 
    web_client_print(client,">LOW<br>");

    web_client_println(client,"<br>");
    web_client_println(client,"</div>"); 
    if ((x % 2) == 0){
      web_client_println(client,"</div>");
    }
  }

  web_client_println(client,"<br><br><input type=\"submit\" value=\"Save\"></form>");

  web_print_home_link(client);
  
  web_print_footer(client);

}
#endif //FEATURE_WEB_SERVER             


//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)

void web_print_page_main_menu(EthernetClient client){


  web_print_header(client);

  web_print_style_sheet(client);

  web_print_title(client);

  web_client_println(client,"<H1>K3NG Contest Station Transmit Interlock Controller</H1>");
  web_client_println(client,"<hr />");
  web_client_println(client,"<br />");  


  web_client_println(client,F("<br />")); 
  web_client_println(client,F("<a href=\"Status\"\" class=\"internal\">Status</a><br /><br />"));
  web_client_println(client,F("<a href=\"PortSettings1\"\" class=\"internal\">Port 1 - 4 Settings</a><br /><br />"));
  web_client_println(client,F("<a href=\"PortSettings2\"\" class=\"internal\">Port 5 - 8 Settings</a><br /><br />"));
  web_client_println(client,F("<a href=\"PortSettings3\"\" class=\"internal\">Port 9 - 12 Settings</a><br /><br />"));
  web_client_println(client,F("<a href=\"PortSettings4\"\" class=\"internal\">Port 13 - 16 Settings</a><br /><br />"));  
  web_client_println(client,F("<a href=\"NetworkSettings\"\" class=\"internal\">Network Settings</a><br /><br />"));        
  web_client_println(client,F("<a href=\"About\"\" class=\"internal\">About</a><br />"));

  web_print_footer(client); 

}

#endif //FEATURE_WEB_SERVER          

             
//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)

void web_print_page_status(EthernetClient client){

  uint8_t pin_read = 0;

  web_print_header(client);

  web_print_style_sheet(client);

  web_client_println(client,"<meta http-equiv=\"refresh\" content=\"5\"/>");

  web_print_title(client);

  web_client_println(client,"<H1>Status</H1>");
  web_client_println(client,"<hr />");
  web_client_println(client,"<br />");  



  for (int x = 1;x < (NUMBER_OF_STATIONS + 1);x++){
    

    web_client_print(client,"Port ");
    web_client_print(client,x);

    web_client_print(client,": ");

    for (int y = 0;y < 32;y++){
      if (configuration.station_name[y][x] == 0){
        y = 32;
      } else {
        web_client_write(client,(uint8_t)configuration.station_name[y][x]);
      } 
    }  
    web_client_print(client," ");
    pin_read = digitalRead(pin_input[x]);
    if (configuration.station_enabled[x] == 1){
      
      if (pin_read == configuration.input_active_state[x]){
        //web_client_print(client," Active");
        if (state_interlock == x){
          web_client_print(client,"TX");
        } else {
          web_client_print(client,"TX LOCKED OUT");
        }      
      } else {       
        if ((state_interlock != x) && (state_interlock != 0)){
          web_client_print(client,"TX INHIBIT");
        } else {
          web_client_print(client,"Inactive");
        }
      }
    } else {
      web_client_print(client,"Disabled");
    }
    web_client_print(client,"<br>");
  }

  // web_client_println(client,"<br />");  
  // web_client_println(client,"<a href=\"/Control?button1on\"\">Turn On LED</a>");
  // web_client_println(client,"<a href=\"/Control?button1off\"\">Turn Off LED</a><br />");   
  // web_client_println(client,"<br />");     
  // web_client_println(client,"<br />"); 

  web_print_home_link(client);

  web_print_footer(client);

}
#endif //FEATURE_WEB_SERVER            

//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_WEB_SERVER)

void web_print_page_network_settings_process(EthernetClient client){

  uint8_t ip0 = 0;
  uint8_t ip1 = 0;
  uint8_t ip2 = 0;
  uint8_t ip3 = 0;
  uint8_t gw0 = 0;
  uint8_t gw1 = 0;
  uint8_t gw2 = 0;
  uint8_t gw3 = 0;              
  uint8_t sn0 = 0;
  uint8_t sn1 = 0;
  uint8_t sn2 = 0;
  uint8_t sn3 = 0;

  uint8_t invalid_data = 0;

  unsigned int ud = 0;

  parse_get(readString);
  if (parse_get_results_index){

    for (int x = 0; x < parse_get_results_index; x++){
      if (parse_get_results[x].parameter == "ip0"){ip0 = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "ip1"){ip1 = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "ip2"){ip2 = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "ip3"){ip3 = parse_get_results[x].value_long;}

      if (parse_get_results[x].parameter == "gw0"){gw0 = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "gw1"){gw1 = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "gw2"){gw2 = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "gw3"){gw3 = parse_get_results[x].value_long;}

      if (parse_get_results[x].parameter == "sn0"){sn0 = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "sn1"){sn1 = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "sn2"){sn2 = parse_get_results[x].value_long;}
      if (parse_get_results[x].parameter == "sn3"){sn3 = parse_get_results[x].value_long;} 

      if (parse_get_results[x].parameter == "ud"){ud = parse_get_results[x].value_long;}                                   
    }

    //invalid_data = 1;


    // data validation

    if ((ip0 == 0) || (ip3 == 255) || (ip3 == 0)) {invalid_data = 1;}
    if (((ip0 & sn0) != (gw0 & sn0)) || ((ip1 & sn1) != (gw1 & sn1)) || ((ip2 & sn2) != (gw2 & sn2)) || ((ip3 & sn3) != (gw3 & sn3))) {invalid_data = 1;}
    if ((sn0 == 0) || (sn1 > sn0) || (sn2 > sn1) || (sn3 > sn2) || (sn3 > 252)) {invalid_data = 1;}

    if (invalid_data){

      web_print_header(client);
      web_print_style_sheet(client);
      web_print_title(client);
      web_client_println(client,"<br>Bad data!<br>");
      web_print_home_link(client);
      web_print_footer(client);

    } else {

      configuration.ip[0] = ip0;
      configuration.ip[1] = ip1;
      configuration.ip[2] = ip2;
      configuration.ip[3] = ip3; 

      configuration.gateway[0] = gw0;
      configuration.gateway[1] = gw1;
      configuration.gateway[2] = gw2;
      configuration.gateway[3] = gw3; 

      configuration.subnet[0] = sn0;
      configuration.subnet[1] = sn1;
      configuration.subnet[2] = sn2;
      configuration.subnet[3] = sn3;  

      configuration.udp_broadcast_port = ud;  

      update_ip_broadcast_address();              

      web_print_header(client);
      web_client_print(client,"<meta http-equiv=\"refresh\" content=\"5; URL='http://");
      web_client_print(client,ip0);
      web_client_print(client,".");
      web_client_print(client,ip1);
      web_client_print(client,".");
      web_client_print(client,ip2);
      web_client_print(client,".");  
      web_client_print(client,ip3);                                                    
      web_client_println(client,"'\" />");
      web_print_style_sheet(client);
      web_print_title(client);
      web_client_println(client,"<br>Configuration saved<br>Restarting networking<br><br>You will be redirected to new address in 5 seconds...<br>");
      web_print_home_link(client);
      web_print_footer(client);
      restart_networking = 1;
      config_dirty = 1;
    }
  }
}
#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------


void update_ip_broadcast_address(){


  for (int x = 0; x < 4; x++){
    ip_broadcast[x] = (configuration.ip[x] & configuration.subnet[x]) | ~ configuration.subnet[x];
  }

}

//-------------------------------------------------------------------------------------------------------
#if defined(FEATURE_UDP)
void service_udp(){

  char packet_buffer[UDP_TX_PACKET_MAX_SIZE];  //buffer to hold incoming packet,
  // char  ReplyBuffer[] = "acknowledged";       // a string to send back


  int packet_size = Udp.parsePacket();
  if (packet_size) {

    #if defined(DEBUG_UDP)
      debug.print("service_udp: received packet: size ");
      debug.println(packet_size);
      debug.print("from ");
      IPAddress remote = Udp.remoteIP();
      for (int i = 0; i < 4; i++) {
        debug.print(remote[i], DEC);
        if (i < 3) {
          debug.print(".");
        }
      }
      debug.print(":");
      debug.println(Udp.remotePort());

      // read the packet into packetBufffer
      Udp.read(packet_buffer, UDP_TX_PACKET_MAX_SIZE);
      debug.print("service_udp: contents:");
      debug.println(packet_buffer);
    #endif //DEBUG_UDP

    // send a reply to the IP address and port that sent us the packet we received
    // Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
    // udp_write(ReplyBuffer);
    // Udp.endPacket();
  }

}

#endif //FEATURE_UDP

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_UDP)


void send_DXL_udp_packet(uint8_t message_type,char message_sender[16],char message_destination[16]){

/*

NEW 2016-06-13

  Packet structure - ??? bytes total

    parameter             format    bytes

    message_sender        unicode   32
    message_destination   unicode   32
    message_type          unicode   8
    message_id            unicode   8   <- changed
    message_data          byte      80  <- changed
     |
     | station_id            unicode   32
     | rq_info               unicode   20
     | padding  28 bytes <- changed

OLD (Prior to 2016-06-13)

  Packet structure - 572 bytes total

    parameter             format    bytes

    message_sender        unicode   32
    message_destination   unicode   32
    message_type          unicode   8
    message_data          byte      500
     |
     | station_id            unicode   32
     | rq_info               unicode   20
     | additional_info       unicode   60
     
*/


    IPAddress ip(ip_broadcast[0], ip_broadcast[1], ip_broadcast[2], ip_broadcast[3]);
    uint8_t null_hit = 0;
    String message_type_char = "";
    static int serial_number = 1;
    int serial_number_temp = 0;

    #if defined(DEBUG_UDP_PACKET_SEND_TIME)
      unsigned long packet_start_time = millis();
    #endif

    int beginpacket_result = Udp.beginPacket(ip, configuration.udp_broadcast_port);

    #if defined(DEBUG_UDP_PACKET_SEND_TIME)
      unsigned long beginpacket_time = millis();
    #endif    

    #if defined(DEBUG_UDP_WRITE)
      debug_serial_port->print("\r\nsend_DXL_udp_packet: packet start: message_type: ");
      switch(message_type){
        case MSG_BRQ: debug_serial_port->print("BRQ"); break;
        case MSG_BOK: debug_serial_port->print("BOK"); break;
        case MSG_BNA: debug_serial_port->print("BNA"); break;
        case MSG_BUN: debug_serial_port->print("BUN"); break;
        case MSG_BST: debug_serial_port->print("BST"); break;                  
      } 
      debug_serial_port->print(" message_sender: ");
      debug_serial_port->print(message_sender);
      debug_serial_port->print(" message_destination: ");
      debug_serial_port->println(message_destination);
      debug_serial_port->println("");
      udp_write_debug_column = 0;
      udp_write_byte_counter = 0;
    #endif



    // message_sender 
    for (int x = 0;x < 16;x++){
      if (!null_hit){
        udp_write(message_sender[x]);
        if (message_sender[x] == 0){
          null_hit = 1;
        }
      } else {
        udp_write((uint8_t)0);
      }
      udp_write((uint8_t)0);
    }
    null_hit = 0;

    // message_destination 
    for (int x = 0;x < 16;x++){
      if (!null_hit){
        udp_write(message_destination[x]);
        if (message_destination[x] == 0){
          null_hit = 1;
        }
      } else {
        udp_write((uint8_t)0);
      }
      udp_write((uint8_t)0);
    }
    null_hit = 0;


    // message_type
    switch(message_type){
      case MSG_BRQ: message_type_char = "BRQ"; break;
      case MSG_BOK: message_type_char = "BOK"; break;
      case MSG_BNA: message_type_char = "BNA"; break;
      case MSG_BUN: message_type_char = "BUN"; break;
      case MSG_BST: message_type_char = "BST"; break;                  
    }
    for (int x = 0;x < 4;x++){
      //if (x < (message_type_char.length()-1)){
        udp_write(message_type_char.charAt(x));
      //} else {
      //  udp_write((uint8_t)0);
      //}
      udp_write((uint8_t)0);
    }

//zzzzzz
    // message_id
    #if !defined(OPTION_OLD_DXL_PACKET_TYPE)
      serial_number_temp = serial_number;
      udp_write((uint8_t)((serial_number_temp/1000)+48));
      udp_write((uint8_t)0);
      serial_number_temp = serial_number_temp - ((serial_number_temp/1000) * 1000);
      udp_write((uint8_t)((serial_number_temp/100)+48));
      udp_write((uint8_t)0);
      serial_number_temp = serial_number_temp - ((serial_number_temp/100) * 100); 
      udp_write((uint8_t)((serial_number_temp/10)+48));
      udp_write((uint8_t)0);
      serial_number_temp = serial_number_temp - (serial_number_temp/10) * 10; 
      udp_write((uint8_t)((serial_number_temp)+48));  
      udp_write((uint8_t)0);
      serial_number++;
      if (serial_number > 9999){serial_number = 1;}
    #endif //OPTION_OLD_DXL_PACKET_TYPE

    // station_id 
    for (int x = 0;x < 16;x++){
      if (!null_hit){
        udp_write(message_sender[x]);
        if (message_sender[x] == 0){
          null_hit = 1;
        }
      } else {
        udp_write((uint8_t)0);
      }
      udp_write((uint8_t)0);
    }
    null_hit = 0;

    // rq_info
    for (int x = 0;x < 10;x++){
      if (x < (message_type_char.length())){
        udp_write(message_type_char.charAt(x));
      } else {
        udp_write((uint8_t)0);
      }
      udp_write((uint8_t)0);
    }


    #if defined(OPTION_OLD_DXL_PACKET_TYPE)
      // additional_info + padding of zeros
      for (int x = 0;x < 448 ;x++){
        udp_write((uint8_t)0);
      }      
    #else  //OPTION_OLD_DXL_PACKET_TYPE
    // additional_info + padding of zeros
      for (int x = 0;x < 28 ;x++){
        udp_write((uint8_t)0);
      }  
    #endif //OPTION_OLD_DXL_PACKET_TYPE

    #if defined(DEBUG_UDP_PACKET_SEND_TIME)
      unsigned long end_udp_write_time = millis();
    #endif   

    int endpacket_result = Udp.endPacket();

    #if defined(DEBUG_UDP_PACKET_SEND_TIME)
      unsigned long endpacket_time = millis();
    #endif    

    #if defined(DEBUG_UDP_PACKET_SEND_TIME)
      debug_serial_port->print("\r\nsend_DXL_udp_packet: ");
      debug_serial_port->print(ip_broadcast[0]);
      debug_serial_port->print(".");
      debug_serial_port->print(ip_broadcast[1]);
      debug_serial_port->print(".");
      debug_serial_port->print(ip_broadcast[2]);
      debug_serial_port->print(".");
      debug_serial_port->print(ip_broadcast[3]);
      debug_serial_port->print(":");   
      debug_serial_port->println(configuration.udp_broadcast_port);  
      debug_serial_port->print("send_DXL_udp_packet: total packet send time: ");
      debug_serial_port->print(endpacket_time-packet_start_time);
      debug_serial_port->println(" mS");
      debug_serial_port->print("send_DXL_udp_packet: beginPacket time: ");
      debug_serial_port->print(beginpacket_time - packet_start_time);
      debug_serial_port->println(" mS");    
      debug_serial_port->print("send_DXL_udp_packet: udp_write time: ");
      debug_serial_port->print(end_udp_write_time-beginpacket_time);
      debug_serial_port->println(" mS");
      debug_serial_port->print("send_DXL_udp_packet: endPacket time: ");
      debug_serial_port->print(endpacket_time - end_udp_write_time);
      debug_serial_port->println(" mS"); 
      if (!beginpacket_result){
        debug_serial_port->println("send_DXL_udp_packet: beginPacket error ");                   
      }      
      if (!endpacket_result){
        debug_serial_port->println("send_DXL_udp_packet: endPacket error");
      }
    #endif

    #if defined(DEBUG_UDP_WRITE)
      debug_serial_port->print("\r\nsend_DXL_udp_packet: endPacket   udp_write_byte_counter:");
      debug_serial_port->println(udp_write_byte_counter);
    #endif    
}
#endif //FEATURE_UDP

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_UDP)
void send_udp_packets(uint8_t old_interlock_state,uint8_t new_interlock_state){

  char message_sender[16];
  char message_destination[16];



  for (int x = 1;x < (NUMBER_OF_STATIONS+1);x++){
    if ((configuration.station_enabled[x]) && (new_interlock_state != 0) && (new_interlock_state != x)){  // send a packet to a station if it's active and not holding the lock
      for (int y = 0;y < 16;y++){
        message_sender[y] = configuration.station_name[y][new_interlock_state];
      }  
      for (int y = 0;y < 16;y++){
        message_destination[y] = configuration.station_name[y][x];
      }        
      send_DXL_udp_packet(MSG_BRQ,message_sender,message_destination);
    }
    if ((configuration.station_enabled[x]) && (new_interlock_state == 0) && (old_interlock_state != x)){ // send out BSTs (blocking stops)
      for (int y = 0;y < 16;y++){
        message_sender[y] = configuration.station_name[y][old_interlock_state];
      }  
      for (int y = 0;y < 16;y++){
        message_destination[y] = configuration.station_name[y][x];
      } 
      send_DXL_udp_packet(MSG_BST,message_sender,message_destination);
    }
  }

}
#endif //FEATURE_UDP

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_UDP)
void udp_write(uint8_t byte_to_write){

  Udp.write(byte_to_write);

  // check_inputs();  // this may cause problems!
  

  #if defined(DEBUG_UDP_WRITE)

    static char ascii_sent[17] = "";

    udp_write_byte_counter++;
    debug_serial_port->print(" ");
    if (byte_to_write < 16){
      debug_serial_port->print("0");
    }
    debug_serial_port->print(byte_to_write,HEX);
    if (byte_to_write == 0){
      ascii_sent[udp_write_debug_column] = '.';
    } else {
      ascii_sent[udp_write_debug_column] = byte_to_write;
    }
    udp_write_debug_column++;
    if (udp_write_debug_column > 15){
      strcat(ascii_sent,"");
      debug_serial_port->print("  ");
      debug_serial_port->println(ascii_sent);
      strcpy(ascii_sent,"");
      udp_write_debug_column = 0;
    }
  #endif

}
#endif //FEATURE_UDP

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void web_client_print(EthernetClient client,const char *str){

  client.print(str);
  check_inputs();

}

#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void web_client_println(EthernetClient client,const char *str){

  client.println(str);
  check_inputs();

}

#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void web_client_print(EthernetClient client,int i){

  client.print(i);
  check_inputs();

}
#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void web_client_print(EthernetClient client,unsigned long i){

  client.print(i);
  check_inputs();

}
#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void web_client_print(EthernetClient client,unsigned int i){

  client.print(i);
  check_inputs();

}
#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void web_client_println(EthernetClient client,unsigned long i){

  client.println(i);
  check_inputs();

}
#endif //FEATURE_WEB_SERVER
//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void web_client_println(EthernetClient client,unsigned long i,int something){

  client.println(i,something);
  check_inputs();

}
#endif //FEATURE_WEB_SERVER
//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void web_client_write(EthernetClient client,uint8_t i){

  client.write(i);
  check_inputs();

}
#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void web_client_println(EthernetClient client,const __FlashStringHelper *str){

  web_client_print(client,str);
  client.println();

}

#endif //FEATURE_WEB_SERVER

//-------------------------------------------------------------------------------------------------------

#if defined(FEATURE_WEB_SERVER)
void web_client_print(EthernetClient client,const __FlashStringHelper *str){

  char c;
  if(!str) return;
  char charstring[255] = "";
  int charstringindex = 0;
  
  /* since str is a const we can't increment it, so do this instead */
  char *p = (char *)str;
  
  /* keep going until we find the null */
  while((c = pgm_read_byte(p++))){
    if (charstringindex < 254){
      charstring[charstringindex] = c;
      charstringindex++;
    }

  }
  charstring[charstringindex] = 0;
  client.print(charstring);

}

#endif //FEATURE_WEB_SERVER


// http://192.168.1.178/PortSettings?sn1=STATION1&act1=1&inp1=0&out1=1&sn2=STATION2&act2=1&inp2=0&out2=1&sn3=STATION3&act3=1&inp3=0&out3=1&sn4=STATION4&act4=1&inp4=0&out4=1&sn5=Station5&act5=0&inp5=0&out5=1&sn6=Station6&act6=0&inp6=0&out6=1&sn7=Station7&act7=0&inp7=0&out7=1&sn8=Station8&act8=0&inp8=0&out8=1&sn9=Station9&act9=0&inp9=0&out9=1&sn10=Station10&act10=0&inp10=0&out10=1&sn11=Station11&act11=0&inp11=0&out11=1&sn12=Station12&act12=0&inp12=0&out12=1&sn13=Station13&act13=0&inp13=0&out13=1&sn14=Station14&act14=0&inp14=0&out14=1&sn15=Station15&act15=0&inp15=0&out15=1&sn16=STation16&act16=0&inp16=0&out16=1