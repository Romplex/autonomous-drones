#include <Pozyx.h>
#include <Pozyx_definitions.h>

#define DEBUG
#define USE_POZYX

#ifndef USE_POZYX
  #define X 0     // cm
  #define Y 0     // cm
  #define Z 500   // cm
#endif

#ifndef DEBUG1
  unsigned long t_;
#endif

const unsigned int GPS_INTERVAL       = 250;      // every 250ms

// TODO: use numbers instead of strings!
static const String ID_LOCATION       = "POZYX";  // message ID for location data
static const String ID_ANCHOR         = "ANCHOR"; // message ID for anchor calibration
static const String ID_MISSION_START  = "M_START";// message ID for mission start
static const String ID_MISSION_STOP   = "M_STOP"; // message ID for mission stop
static const String ID_WP_ADD         = "WP_ADD"; // message ID for adding wp to mission
static const String ID_WP_REMOVE      = "WP_DEL"; // message ID for removing wp from mission


//        #############################################
//        ######### APPLY TAG PARAMETERS HERE #########
//        #############################################

uint16_t remote_id = 0x6760;                            // set this to the ID of the remote device
bool     remote = false;                                // set this to true to use the remote ID

boolean  use_processing = false;                        // set this to true to output data for the processing sketch

const uint8_t num_anchors = 4;                          // the number of anchors
uint16_t anchors[num_anchors] = {0x6951, 0x6E59, 0x695D, 0x690B};     // the network id of the anchors: change these to the network ids of your anchors.
// TODO: measure actual coordinates
int32_t anchors_x[num_anchors] = {0,5340,6812,-541};     // anchor x-coorindates in mm
int32_t anchors_y[num_anchors] = {0,0,-8923,-10979};     // anchor y-coordinates in mm
int32_t heights[num_anchors] = {1500, 2000, 2500, 3000};// anchor z-coordinates in mm

uint8_t algorithm = POZYX_POS_ALG_UWB_ONLY;             // positioning algorithm to use. try POZYX_POS_ALG_TRACKING for fast moving objects.
uint8_t dimension = POZYX_3D;                           // positioning dimension
int32_t height = 1000;                                  // height of device, required in 2.5D positioning

//        #############################################
//        ######### APPLY TAG PARAMETERS HERE #########
//        #############################################


void setup() {
  Serial.begin(115200); // 57600 or 115200
  while(!Serial);

#ifdef USE_POZYX
  #ifdef DEBUG
    Serial.println("-   INIT POZYX   -");
  #endif
  if(Pozyx.begin() == POZYX_FAILURE){
#ifdef DEBUG
    Serial.println("ERROR: Unable to connect to POZYX shield");
    Serial.println("Reset required");
    Serial.flush();
#endif
    delay(100);
    abort();
  }
#endif

#ifdef DEBUG
  Serial.println(F("----------POZYX POSITIONING V1.1----------"));
  Serial.println(F("NOTES:"));
  Serial.println(F("- No parameters required."));
  Serial.println();
  Serial.println(F("- System will auto start anchor configuration"));
  Serial.println();
  Serial.println(F("- System will auto start positioning"));
  Serial.println(F("----------POZYX POSITIONING V1.1----------"));
  Serial.println();
  Serial.println(F("Performing manual anchor configuration:"));
#endif

#ifdef USE_POZYX
  // clear all previous devices in the device list
  Pozyx.clearDevices(remote_id);
  // sets the anchor manually
  setAnchorsManual();
  // sets the positioning algorithm
  Pozyx.setPositionAlgorithm(algorithm, dimension, remote_id);
#endif

  // TODO: delay of 2000 needed after flush?
  Serial.flush();
#ifdef USE_POZYX
  delay(2000);
#endif

#ifdef DEBUG
  Serial.println(F("Starting positioning: "));
#endif
}

void loop() {
#ifndef DEBUG
  t_ = millis();
#endif

#ifdef USE_POZYX
  coordinates_t position;
  int status;
  if(remote){
    status = Pozyx.doRemotePositioning(remote_id, &position, dimension, height, algorithm);
  }else{
    status = Pozyx.doPositioning(&position, dimension, height, algorithm);
  }
#endif

#if defined(DEBUG1) && defined(USE_POZYX)
  if (status == POZYX_SUCCESS){
    // prints out the result
    printCoordinates(position);
  }else{
    // prints out the error code
    printErrorCode("positioning");
  }
#endif

#ifdef DEBUG1
  Serial.print("x: "); Serial.println(position.x);
  Serial.print("y: "); Serial.println(position.y);
  Serial.print("z: "); Serial.println(position.z);
#endif

#ifdef USE_POZYX
  int coordinates[3] = {position.x, position.y, position.z};
#else
  int coordinates[3] = {X,Y,Z};
#endif

  String msg = genMsg(coordinates[0], coordinates[1], coordinates[2], millis()); 
  
  Serial.println(msg);
  Serial.flush();
#ifdef DEBUG
  delay(250);
#else
  delay( GPS_INTERVAL - (millis() - t_) );
#endif
}



// prints the coordinates for either humans or for processing
void printCoordinates(coordinates_t coor){
  uint16_t network_id = remote_id;
  if (network_id == NULL){
    network_id = 0;
  }
  if(!use_processing){
    Serial.print("POS ID 0x");
    Serial.print(network_id, HEX);
    Serial.print(", x(mm): ");
    Serial.print(coor.x);
    Serial.print(", y(mm): ");
    Serial.print(coor.y);
    Serial.print(", z(mm): ");
    Serial.println(coor.z);
  }else{
    Serial.print("POS,0x");
    Serial.print(network_id,HEX);
    Serial.print(",");
    Serial.print(coor.x);
    Serial.print(",");
    Serial.print(coor.y);
    Serial.print(",");
    Serial.println(coor.z);
  }
}

// error printing function for debugging
void printErrorCode(String operation){
  uint8_t error_code;
  if (remote_id == NULL){
    Pozyx.getErrorCode(&error_code);
    Serial.print("ERROR ");
    Serial.print(operation);
    Serial.print(", local error code: 0x");
    Serial.println(error_code, HEX);
    return;
  }
  int status = Pozyx.getErrorCode(&error_code, remote_id);
  if(status == POZYX_SUCCESS){
    Serial.print("ERROR ");
    Serial.print(operation);
    Serial.print(" on ID 0x");
    Serial.print(remote_id, HEX);
    Serial.print(", error code: 0x");
    Serial.println(error_code, HEX);
  }else{
    Pozyx.getErrorCode(&error_code);
    Serial.print("ERROR ");
    Serial.print(operation);
    Serial.print(", couldn't retrieve remote error code, local error: 0x");
    Serial.println(error_code, HEX);
  }
}

// print out the anchor coordinates
void printCalibrationResult(){
  uint8_t list_size;
  int status;

  status = Pozyx.getDeviceListSize(&list_size, remote_id);
  Serial.print("list size: ");
  Serial.println(status*list_size);

  if(list_size == 0){
    printErrorCode("configuration");
    return;
  }

  uint16_t device_ids[list_size];
  status &= Pozyx.getDeviceIds(device_ids, list_size, remote_id);

  Serial.println(F("Calibration result:"));
  Serial.print(F("Anchors found: "));
  Serial.println(list_size);

  coordinates_t anchor_coor;
  for(int i = 0; i < list_size; i++)
  {
    Serial.print("ANCHOR,");
    Serial.print("0x");
    Serial.print(device_ids[i], HEX);
    Serial.print(",");
    Pozyx.getDeviceCoordinates(device_ids[i], &anchor_coor, remote_id);
    Serial.print(anchor_coor.x);
    Serial.print(",");
    Serial.print(anchor_coor.y);
    Serial.print(",");
    Serial.println(anchor_coor.z);
  }
}

// function to manually set the anchor coordinates
void setAnchorsManual(){
  for(int i = 0; i < num_anchors; i++){
    device_coordinates_t anchor;
    anchor.network_id = anchors[i];
    anchor.flag = 0x1;
    anchor.pos.x = anchors_x[i];
    anchor.pos.y = anchors_y[i];
    anchor.pos.z = heights[i];
    Pozyx.addDevice(anchor, remote_id);
  }
  if (num_anchors > 4){
    Pozyx.setSelectionOfAnchors(POZYX_ANCHOR_SEL_AUTO, num_anchors, remote_id);
  }
}


// prepares char array for Serial communication
// TODO: add other msg IDs and format msg respectively
String genMsg(int x, int y, int z, unsigned long t) {
  String tt = formatTime(t);

  char x_sign = '+';
  char y_sign = '+';
  char z_sign = '+';

  if(x < 0) {
    x = -x;
    x_sign = '-';
  }

  if(y < 0) {
    y = -y;
    y_sign = '-';
  }

  if(z < 0) {
    z = -z;
    z_sign = '-';
  }

  String str = "$"
        + ID_LOCATION+","
        + tt+","
        + x+","
        + x_sign+","
        + y+","
        + y_sign+","
        + z+","
        + z_sign
        + "*";
  byte len = str.length()+1;
  char buff[len];

  // TODO: fix String->Char, Char->String conversions!
  str.toCharArray(buff, len);
  String msg = str + calcCRC(buff, sizeof(buff));
  return msg;
}

String genMsg(double x, double y, double altitude) {
  return genMsg(x, y, altitude, millis());
}

// format time: hhmmss.sss
String formatTime(unsigned long t) {
  int dd = t / 86400000;
  int hh = (t % 86400000) / 3600000;
  int mm = ((t % 86400000) % 3600000) / 60000;
  double ss = (((t % 86400000) % 3600000) % 60000) / 1000.0;

  String h = (hh > 9 ? String(hh) : "0" + String(hh));
  String m = (mm > 9 ? String(mm) : "0" + String(mm));
  String s = (ss > 9 ? String(ss,3) : "0" + String(ss,3));
  return h+m+s;
}

// NMEA CRC: XOR each byte with previous for all chars between '$' and '*'
String calcCRC(char* buff, byte buff_len) {
  char c;
  byte i;
  byte start_with = 0;
  byte end_with = 0;
  byte crc = 0;

  for (i = 0; i < buff_len; i++) {
    c = buff[i];
    if(c == '$') start_with = i;
    else if(c == '*') end_with = i;

  }
  if (end_with > start_with){
    for (i = start_with+1; i < end_with; i++){
      crc = crc ^ buff[i];  // XOR every character between '$' and '*'
    }
  } else {
#ifdef DEBUG
    Serial.println("CRC ERROR");
#endif
    return "-1";
  }
  return String(crc, HEX);
}
