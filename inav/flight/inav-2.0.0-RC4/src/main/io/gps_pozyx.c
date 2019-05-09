/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

#include "common/typeconversion.h"
#include "platform.h"

#if defined(USE_GPS) && defined(USE_GPS_PROTO_POZYX)

#include "build/build_config.h"
#include "build/debug.h"

#include "common/axis.h"
#include "common/maths.h"
#include "common/utils.h"

#include "drivers/serial.h"
#include "drivers/time.h"

#include "fc/config.h"
#include "fc/runtime_config.h"

#include "io/serial.h"
#include "io/gps.h"
#include "io/gps_private.h"

/* This is a light implementation of a POZYX frame decoding
   It assumes there are some POZYX frames to decode on the serial bus
   Now verifies checksum correctly before applying data

   Here we use only the following data :
     - x
     - y
     - z
*/

#define NO_FRAME        0
#define FRAME_NAV       1
#define FRAME_MAG       2

// do we have new position information?
static bool _new_position;

// do we have new speed information?
static bool _new_speed;

static uint32_t grab_fields(char *src, uint8_t mult)
{                               // convert string to uint32
    uint32_t i;
    uint32_t tmp = 0;
    for (i = 0; src[i] != 0; i++) {
        if (src[i] == '.') {
            i++;
            if (mult == 0)
                break;
            else
                src[i + mult] = 0;
        }
        tmp *= 10;
        if (src[i] >= '0' && src[i] <= '9')
            tmp += src[i] - '0';
        if (i >= 15)
            return 0; // out of bounds
    }
    return tmp;
}

#define POZYX_BUFFER_SIZE        16 // TODO uniks: check actual buffer size

static bool gpsNewFramePOZYX(char c)
{
    static gpsDataPozyx_t gps_Msg;

    static uint8_t param = 0, offset = 0, parity = 0;
    static char string[POZYX_BUFFER_SIZE];
    static uint8_t checksum_param, gps_frame = NO_FRAME;

    switch (c) {
        case '$':
            param = 0;
            offset = 0;
            parity = 0;
            break;
        case ',':
        case '*':
            string[offset] = 0;
            if (param == 0) {       //frame identification
                uint8_t frame = grab_fields(string, 0);
                gps_frame = NO_FRAME;
                //if (strcmp(string, "GPS") == 0) {
                if(frame == FRAME_NAV) {
                    gps_frame = FRAME_NAV;
                //} else if (strcmp(string, "MAG") == 0) {
                } else if(frame == FRAME_MAG) {
                    gps_frame = FRAME_MAG;
                }
                // TODO uniks add anchor init frame wich sets anchor position X,Y,Z
            }

            switch (gps_frame) {
                //************* POZYX LOCATION FRAME parsing *************
                // TODO uniks look at FAKE_GPS (gps.c:282)
                case FRAME_NAV:
                    switch (param) {
                        case 1:
                            gps_Msg.time = grab_fields(string, 2);
                            break;
                        case 2:
                            gps_Msg.date = grab_fields(string, 0);
                            break;
                        case 3:
                            gps_Msg.latitude = grab_fields(string,8);   // TODO uniks : mm ?
                            break;
                        case 4:
                            gps_Msg.longitude = grab_fields(string,8);  // TODO uniks : mm ?
                            break;
                        case 5:
                            gps_Msg.altitude = grab_fields(string, 8) / 10;  // TODO uniks : cm ?
                            break;
                        case 6:
                            gps_Msg.vel_n = grab_fields(string, 0);  // TODO uniks : d/s ?
                            break;
                        case 7:
                            gps_Msg.vel_e = grab_fields(string, 0);  // TODO uniks : d/s ?
                            break;
                        case 8:
                            gps_Msg.vel_d = grab_fields(string, 0);  // TODO uniks : d/s ?
                            break;
                    }
                    break;
                case FRAME_MAG:
                    switch (param) {
                        case 1:
                            gps_Msg.mag_x = grab_fields(string, 2); // TODO uniks : µT ?
                            break;
                        case 2:
                            gps_Msg.mag_y = grab_fields(string, 2); // TODO uniks : µT ?
                            break;
                        case 3:
                            gps_Msg.mag_z = grab_fields(string, 2); // TODO uniks : µT ?
                            break;
                    }
                    break;
            }

            param++;
            offset = 0;
            if (c == '*')
                checksum_param = 1;
            else
                parity ^= c;
            break;
        case '\r':
        case '\n':
            if (checksum_param) {   //parity checksum
                uint8_t checksum = 16 * ((string[0] >= 'A') ? string[0] - 'A' + 10 : string[0] - '0') + ((string[1] >= 'A') ? string[1] - 'A' + 10 : string[1] - '0');

                if (checksum == parity) {
                    gpsStats.packetCount++;
                    switch (gps_frame) {
                        case FRAME_NAV:
                            // gps lat,lon,alt
                            // time sec,min,hour,day,month,year
                            // fixtype
                            // vel north,east,down
                            // hdop,eph,epv,numsat,groundspeed,groundcourse
                            // flags validVelNE,validVelD,validEPE,validTime

                            gpsSol.llh.lat = gps_Msg.latitude;
                            gpsSol.llh.lon = gps_Msg.longitude;
                            gpsSol.llh.alt = gps_Msg.altitude;

                            // This check will miss 00:00:00.00, but we shouldn't care - next report will be valid
                            // TODO uniks: maybe dont set time since it is less acurate than actual gps time
                            if (gps_Msg.date != 0 && gps_Msg.time != 0) {
                                gpsSol.time.year = (gps_Msg.date % 100) + 2000;
                                gpsSol.time.month = (gps_Msg.date / 100) % 100;
                                gpsSol.time.day = (gps_Msg.date / 10000) % 100;
                                gpsSol.time.hours = (gps_Msg.time / 1000000) % 100;
                                gpsSol.time.minutes = (gps_Msg.time / 10000) % 100;
                                gpsSol.time.seconds = (gps_Msg.time / 100) % 100;
                                gpsSol.time.millis = (gps_Msg.time & 100) * 10;
                                gpsSol.flags.validTime = 1;
                            }
                            else {
                                gpsSol.flags.validTime = 0;
                            }

                            gpsSol.fixType = GPS_FIX_3D;

                            gpsSol.velNED[0] = gps_Msg.vel_n;  // cm/s
                            gpsSol.velNED[1] = gps_Msg.vel_e;  // cm/s
                            gpsSol.velNED[2] = gps_Msg.vel_d;  // cm/s

                            // TODO uniks: maybe its possible to get horizontal and vertical accuracy from pozyx
                            gpsSol.hdop = 1;  // PDOP
                            gpsSol.eph = 1;   // hAcc in cm
                            gpsSol.epv = 1;   // vAcc in cm
                            gpsSol.numSat = 12;
                            gpsSol.groundSpeed = sqrtf(powf(gpsSol.velNED[0], 2)+powf(gpsSol.velNED[1], 2)); //cm/s

                            // calculate gps heading from VELNE
                            gpsSol.groundCourse = (uint16_t) (fmodf(RADIANS_TO_DECIDEGREES(atan2_approx(gpsSol.velNED[1], gpsSol.velNED[0]))+3600.0f,3600.0f));


                            gpsSol.flags.validVelNE = 1;
                            gpsSol.flags.validVelD = 1;
                            gpsSol.flags.validEPE = 1;

                            _new_speed = true;
                            _new_position = true;
                            break;
                        case FRAME_MAG:
                            // mag xyz

                            gpsSol.magData[0] = gps_Msg.mag_x;
                            gpsSol.magData[1] = gps_Msg.mag_y;
                            gpsSol.magData[2] = gps_Msg.mag_z;

                            gpsSol.flags.validMag = 1;

                            break;
                    } // end switch
                } else {
                    gpsStats.errors++;
                }
            }
            checksum_param = 0;
            break;
        default:
            if (offset < (POZYX_BUFFER_SIZE-1)) {    // leave 1 byte to trailing zero
                string[offset++] = c;

                // only checksum if character is recorded and used (will cause checksum failure on dropped characters)
                if (!checksum_param)
                    parity ^= c;
            }
    }

    return (_new_position && _new_speed);
}

static bool gpsReceiveData(void)
{
    bool hasNewData = false;

    if (gpsState.gpsPort) {
        while (serialRxBytesWaiting(gpsState.gpsPort)) {
            uint8_t newChar = serialRead(gpsState.gpsPort);
            if (gpsNewFramePOZYX(newChar)) {
                gpsSol.flags.gpsHeartbeat = !gpsSol.flags.gpsHeartbeat;
                _new_speed = _new_position = false;
                hasNewData = true;
            }
        }
    }

    return hasNewData;
}

static bool gpsInitialize(void)
{
    gpsSetState(GPS_CHANGE_BAUD);
    return false;
}

static bool gpsChangeBaud(void)
{
    gpsFinalizeChangeBaud();
    return false;
}

bool gpsHandlePOZYX(void)
{
    // Receive data
    bool hasNewData = gpsReceiveData();

    // Process state
    switch (gpsState.state) {
    default:
        return false;

    case GPS_INITIALIZING:
        return gpsInitialize();

    case GPS_CHANGE_BAUD:
        return gpsChangeBaud();

    case GPS_CHECK_VERSION:
    case GPS_CONFIGURE:
        // No autoconfig, switch straight to receiving data
        gpsSetState(GPS_RECEIVING_DATA);
        return false;

    case GPS_RECEIVING_DATA:
        return hasNewData;
    }
}

#endif
