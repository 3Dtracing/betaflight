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
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "platform.h"
#include "scheduler.h"

#include "common/axis.h"
#include "common/color.h"
#include "common/atomic.h"
#include "common/maths.h"
#include "common/typeconversion.h"

#include "drivers/nvic.h"

#include "drivers/sensor.h"
#include "drivers/system.h"
#include "drivers/gpio.h"
#include "drivers/light_led.h"
#include "drivers/sound_beeper.h"
#include "drivers/timer.h"
#include "drivers/serial.h"
#include "drivers/serial_softserial.h"
#include "drivers/serial_uart.h"
#include "drivers/accgyro.h"
#include "drivers/compass.h"
#include "drivers/pwm_mapping.h"
#include "drivers/pwm_rx.h"
#include "drivers/adc.h"
#include "drivers/bus_i2c.h"
#include "drivers/bus_bst.h"
#include "drivers/bus_spi.h"
#include "drivers/inverter.h"
#include "drivers/flash_m25p16.h"
#include "drivers/sonar_hcsr04.h"
#include "drivers/gyro_sync.h"
#include "drivers/usb_io.h"
#include "drivers/transponder_ir.h"
#include "drivers/sdcard.h"

#include "rx/rx.h"

#include "io/beeper.h"
#include "io/serial.h"
#include "io/flashfs.h"
#include "io/gps.h"
#include "io/escservo.h"
#include "io/rc_controls.h"
#include "io/gimbal.h"
#include "io/ledstrip.h"
#include "io/display.h"
#include "io/asyncfatfs/asyncfatfs.h"
#include "io/transponder_ir.h"

#include "sensors/sensors.h"
#include "sensors/sonar.h"
#include "sensors/barometer.h"
#include "sensors/compass.h"
#include "sensors/acceleration.h"
#include "sensors/gyro.h"
#include "sensors/battery.h"
#include "sensors/boardalignment.h"
#include "sensors/initialisation.h"

#include "telemetry/telemetry.h"
#include "blackbox/blackbox.h"

#include "flight/pid.h"
#include "flight/imu.h"
#include "flight/mixer.h"
#include "flight/failsafe.h"
#include "flight/navigation.h"

#include "config/runtime_config.h"
#include "config/config.h"
#include "config/config_profile.h"
#include "config/config_master.h"

#ifdef USE_HARDWARE_REVISION_DETECTION
#include "hardware_revision.h"
#endif

#include "build_config.h"
#include "debug.h"

#ifdef OSD

#include "io/osd.h"
#include "drivers/max7456.h"
#include "drivers/rtc6705.h"
#include "scheduler.h"
#include "common/printf.h"

#define MICROSECONDS_IN_A_SECOND (1000 * 1000)

#define OSD_UPDATE_FREQUENCY (MICROSECONDS_IN_A_SECOND / 5)
#define OSD_LINE_LENGTH 30

static uint32_t next_osd_update_at = 0;
static uint32_t armed_seconds = 0;
static uint32_t armed_at = 0;
static bool armed = false;

static uint8_t current_page = 0;
static uint8_t sticks[] = {0,0,0,0};
static char string_buffer[30];
static uint8_t cursor_row = 255;
static uint8_t cursor_col = 0;
static bool in_menu = false;
extern uint16_t rssi;


#ifdef USE_RTC6705
void update_vtx_band(bool increase, uint8_t col) {
    (void)col;
    if (increase) {
        if (current_vtx_channel < 32)
            current_vtx_channel += 8;
    } else {
        if (current_vtx_channel > 7)
            current_vtx_channel -= 8;
    }
}

void print_vtx_band(uint16_t pos, uint8_t col) {
    (void)col;
    sprintf(string_buffer,  "%s", vtx_bands[current_vtx_channel / 8]);
    max7456_write_string(string_buffer, pos);
}

void update_vtx_channel(bool increase, uint8_t col) {
    (void)col;
    if (increase) {
        if ((current_vtx_channel % 8) < 7)
            current_vtx_channel++;
    } else {
        if ((current_vtx_channel % 8) > 0)
            current_vtx_channel--;
    }
}

void print_vtx_channel(uint16_t pos, uint8_t col) {
    (void)col;
    sprintf(string_buffer,  "%d", current_vtx_channel % 8 + 1);
    max7456_write_string(string_buffer, pos);
}

void print_vtx_freq(uint16_t pos, uint8_t col) {
    (void)col;
    sprintf(string_buffer,  "%d M", vtx_freq[current_vtx_channel]);
    max7456_write_string(string_buffer, pos);
}
#endif

void print_pid(uint16_t pos, uint8_t col, int pid_term) {
    switch(col) {
        case 0:
            if (IS_PID_CONTROLLER_FP_BASED(currentProfile->pidProfile.pidController))
                sprintf(string_buffer, "%d", (int)(currentProfile->pidProfile.P_f[pid_term] * 10.0));
            else
                sprintf(string_buffer, "%d", currentProfile->pidProfile.P8[pid_term]);
            break;
        case 1:
            if (IS_PID_CONTROLLER_FP_BASED(currentProfile->pidProfile.pidController))
                sprintf(string_buffer, "%d", (int)(currentProfile->pidProfile.I_f[pid_term] * 100.0));
            else
                sprintf(string_buffer, "%d", currentProfile->pidProfile.I8[pid_term]);
            break;
        case 2:
            if (IS_PID_CONTROLLER_FP_BASED(currentProfile->pidProfile.pidController))
                sprintf(string_buffer, "%d", (int)(currentProfile->pidProfile.D_f[pid_term] * 1000.0));
            else
                sprintf(string_buffer, "%d", currentProfile->pidProfile.D8[pid_term]);
            break;
        default:
            return;
    }
    max7456_write_string(string_buffer, pos);
}

void print_roll_pid(uint16_t pos, uint8_t col) {
    print_pid(pos, col, ROLL);
}

void print_pitch_pid(uint16_t pos, uint8_t col) {
    print_pid(pos, col, PITCH);
}

void print_yaw_pid(uint16_t pos, uint8_t col) {
    print_pid(pos, col, YAW);
}

void print_roll_rate(uint16_t pos, uint8_t col) {
    if (col == 0) {
        sprintf(string_buffer, "%d", currentControlRateProfile->rates[FD_ROLL]);
        max7456_write_string(string_buffer, pos);
    }
}

void print_pitch_rate(uint16_t pos, uint8_t col) {
    if (col == 0) {
        sprintf(string_buffer, "%d", currentControlRateProfile->rates[FD_PITCH]);
        max7456_write_string(string_buffer, pos);
    }
}

void print_yaw_rate(uint16_t pos, uint8_t col) {
    if (col == 0) {
        sprintf(string_buffer, "%d", currentControlRateProfile->rates[FD_YAW]);
        max7456_write_string(string_buffer, pos);
    }
}

void update_int_pid(bool inc, uint8_t col, int pid_term) {
    void* ptr;
    switch(col) {
        case 0:
            ptr = &currentProfile->pidProfile.P8[pid_term];
            break;
        case 1:
            ptr = &currentProfile->pidProfile.I8[pid_term];
            break;
        case 2:
            ptr = &currentProfile->pidProfile.D8[pid_term];
            break;
        default:
            return;
    }

    if (inc) {
        if (*(uint8_t*)ptr < 200)
            *(uint8_t*)ptr += 1;
    } else {
        if (*(uint8_t*)ptr > 0)
            *(uint8_t*)ptr -= 1;
    }
}

void update_float_pid(bool inc, uint8_t col, int pid_term) {
    void* ptr;
    float diff;

    switch(col) {
        case 0:
            ptr = &currentProfile->pidProfile.P_f[pid_term];
            diff = 0.1;
            break;
        case 1:
            ptr = &currentProfile->pidProfile.I_f[pid_term];
            diff = 0.01;
            break;
        case 2:
            ptr = &currentProfile->pidProfile.D_f[pid_term];
            diff = 0.001;
            break;
    }

    if (inc) {
        if (*(float*)ptr < 100.0)
            *(float*)ptr += diff;
    } else {
        if (*(float*)ptr > 0.0)
            *(float*)ptr -= diff;
    }
}

void update_roll_pid(bool inc, uint8_t col) {
    if (IS_PID_CONTROLLER_FP_BASED(currentProfile->pidProfile.pidController))
        update_float_pid(inc, col, ROLL);
    else
        update_int_pid(inc, col, ROLL);
}

void update_pitch_pid(bool inc, uint8_t col) {
    if (IS_PID_CONTROLLER_FP_BASED(currentProfile->pidProfile.pidController))
        update_float_pid(inc, col, PITCH);
    else
        update_int_pid(inc, col, PITCH);
}

void update_yaw_pid(bool inc, uint8_t col) {
    if (IS_PID_CONTROLLER_FP_BASED(currentProfile->pidProfile.pidController))
        update_float_pid(inc, col, YAW);
    else
        update_int_pid(inc, col, YAW);
}

void update_roll_rate(bool inc, uint8_t col) {
    (void)col;
    if (inc) {
        if (currentControlRateProfile->rates[FD_ROLL] < CONTROL_RATE_CONFIG_ROLL_PITCH_RATE_MAX)
            currentControlRateProfile->rates[FD_ROLL]++;
    } else {
        if (currentControlRateProfile->rates[FD_ROLL] > 0)
            currentControlRateProfile->rates[FD_ROLL]--;
    }
}

void update_pitch_rate(bool increase, uint8_t col) {
    (void)col;
    if (increase) {
        if (currentControlRateProfile->rates[FD_PITCH] < CONTROL_RATE_CONFIG_ROLL_PITCH_RATE_MAX)
            currentControlRateProfile->rates[FD_PITCH]++;
    } else {
        if (currentControlRateProfile->rates[FD_PITCH] > 0)
            currentControlRateProfile->rates[FD_PITCH]--;
    }
}

void update_yaw_rate(bool increase, uint8_t col) {
    (void)col;
    if (increase) {
        if (currentControlRateProfile->rates[FD_YAW] < CONTROL_RATE_CONFIG_YAW_RATE_MAX)
            currentControlRateProfile->rates[FD_YAW]++;
    } else {
        if (currentControlRateProfile->rates[FD_YAW] > 0)
            currentControlRateProfile->rates[FD_YAW]--;
    }
}

void print_average_system_load(uint16_t pos, uint8_t col) {
    (void)col;
    sprintf(string_buffer, "%d", averageSystemLoadPercent);
    max7456_write_string(string_buffer, pos);
}

void print_batt_voltage(uint16_t pos, uint8_t col) {
    (void)col;
    sprintf(string_buffer, "%d.%1d", vbat / 10, vbat % 10);
    max7456_write_string(string_buffer, pos);
}

/*
    TODO: add this to menu
    { "rc_rate",                    VAR_UINT8  | PROFILE_RATE_VALUE, &masterConfig.profile[0].controlRateProfile[0].rcRate8, .config.minmax = { 0,  250 } },
    { "rc_expo",                    VAR_UINT8  | PROFILE_RATE_VALUE, &masterConfig.profile[0].controlRateProfile[0].rcExpo8, .config.minmax = { 0,  100 } },
    { "rc_yaw_expo",                VAR_UINT8  | PROFILE_RATE_VALUE, &masterConfig.profile[0].controlRateProfile[0].rcYawExpo8, .config.minmax = { 0,  100 } },
    { "thr_mid",                    VAR_UINT8  | PROFILE_RATE_VALUE, &masterConfig.profile[0].controlRateProfile[0].thrMid8, .config.minmax = { 0,  100 } },
    { "thr_expo",                   VAR_UINT8  | PROFILE_RATE_VALUE, &masterConfig.profile[0].controlRateProfile[0].thrExpo8, .config.minmax = { 0,  100 } },
    { "tpa_rate",                   VAR_UINT8  | PROFILE_RATE_VALUE, &masterConfig.profile[0].controlRateProfile[0].dynThrPID, .config.minmax = { 0,  CONTROL_RATE_CONFIG_TPA_MAX} },
    { "tpa_breakpoint",             VAR_UINT16 | PROFILE_RATE_VALUE, &masterConfig.profile[0].controlRateProfile[0].tpa_breakpoint, .config.minmax = { PWM_RANGE_MIN,  PWM_RANGE_MAX} },
    { "acro_plus_factor",           VAR_UINT8  | MASTER_VALUE, &masterConfig.rxConfig.acroPlusFactor, .config.minmax = {1, 100 } },
    { "acro_plus_offset",           VAR_UINT8  | MASTER_VALUE, &masterConfig.rxConfig.acroPlusOffset, .config.minmax = {1, 90 } },
*/

page_t menu_pages[] = {
    {
        .title = "STATUS",
        .cols_number = 1,
        .rows_number = 2,
        .cols = { 
            {
                .title = NULL,
                .x_pos = 15
            }
        },
        .rows = {
            {
                .title  = "AVG LOAD",
                .y_pos  = 0,
                .update = NULL,
                .print  = print_average_system_load
            },
            {
                .title  = "BATT",
                .y_pos  = 1,
                .update = NULL,
                .print  = print_batt_voltage
            },
        }
    },
#ifdef USE_RTC6705
    {   
        .title       = "VTX SETTINGS",
        .cols_number = 1,
        .rows_number = 3,
        .cols = { 
            {
                .title = NULL,
                .x_pos = 15
            }
        },
        .rows = { 
            {
                .title  = "BAND",
                .y_pos  = 0,
                .update = update_vtx_band,
                .print  = print_vtx_band
            },
            {
                .title  = "CHANNEL",
                .y_pos  = 1,
                .update = update_vtx_channel,
                .print  = print_vtx_channel
            },
            {
                .title  = "FREQUENCY",
                .y_pos  = 2,
                .update = NULL,
                .print  = print_vtx_freq
            }
        }
    },
#endif
    {
        .title       = "PID SETTINGS",
        .cols_number = 3,
        .rows_number = 6,
        .cols = { 
            {
                .title = "P",
                .x_pos = 13
            },
            {
                .title = "I",
                .x_pos = 19
            },
            {
                .title = "D",
                .x_pos = 25
            }
        },
        .rows = { 
            {
                .title  = "ROLL",
                .y_pos  = 0,
                .update = update_roll_pid,
                .print  = print_roll_pid
            },
            {
                .title  = "PITCH",
                .y_pos  = 1,
                .update = update_pitch_pid,
                .print  = print_pitch_pid
            },
            {
                .title  = "YAW",
                .y_pos  = 2,
                .update = update_yaw_pid,
                .print  = print_yaw_pid
            },
            {
                .title  = "ROLL_RATE",
                .y_pos  = 3,
                .update = update_roll_rate,
                .print  = print_roll_rate
            },
            {
                .title  = "PITCH_RATE",
                .y_pos  = 4,
                .update = update_pitch_rate,
                .print  = print_pitch_rate
            },
            {
                .title  = "YAW_RATE",
                .y_pos  = 5,
                .update = update_yaw_rate,
                .print  = print_yaw_rate
            },
        }
    },
};

void show_menu(void) {
    uint8_t line = 1;
    uint16_t pos;
    col_t *col;
    row_t *row;
    uint16_t cursor_x, cursor_y;

    sprintf(string_buffer, "EXIT     SAVE+EXIT     PAGE");
    max7456_write_string(string_buffer, 12 * OSD_LINE_LENGTH + 1);

    pos = (OSD_LINE_LENGTH - strlen(menu_pages[current_page].title)) / 2 + line * OSD_LINE_LENGTH;
    sprintf(string_buffer, "%s", menu_pages[current_page].title);
    max7456_write_string(string_buffer, pos);

    line += 2;

    for (int i = 0; i < menu_pages[current_page].cols_number; i++){
        col = &menu_pages[current_page].cols[i];
        if (cursor_col == i)
            cursor_x = col->x_pos - 1;

        if (col->title) {
            sprintf(string_buffer, "%s", col->title);
            max7456_write_string(string_buffer, line * OSD_LINE_LENGTH + col->x_pos);
        }
    }

    line++;
    for (int i = 0; i < menu_pages[current_page].rows_number; i++) {
        row = &menu_pages[current_page].rows[i];
        if (cursor_row == i)
            cursor_y = line + row->y_pos;

        sprintf(string_buffer, "%s", row->title);
        max7456_write_string(string_buffer, (line + row->y_pos) * OSD_LINE_LENGTH + 1);
        for (int j = 0; j < menu_pages[current_page].cols_number; j++) {
            col = &menu_pages[current_page].cols[j];
            row->print((line + row->y_pos) * OSD_LINE_LENGTH + col->x_pos, j);
        }
    }

    if (sticks[YAW] > 90 && sticks[ROLL] > 10 && sticks[ROLL] < 90 && sticks[PITCH] > 10 && sticks[PITCH] < 90) {
        if (cursor_row > MAX_MENU_ROWS) {
            switch(cursor_col) {
                case 0:
                    in_menu = false;
                    break;
                case 1:
                    in_menu = false;
#ifdef USE_RTC6705
                    if (masterConfig.vtx_channel != current_vtx_channel) {
                        masterConfig.vtx_channel = current_vtx_channel;
                        rtc6705_set_channel(vtx_freq[current_vtx_channel]);
                    }
#endif
                    writeEEPROM();
                    break;
                case 2:
                    if (current_page < (sizeof(menu_pages) / sizeof(page_t) - 1))
                        current_page++;
                    else
                        current_page = 0;
            }
        } else {
            if (menu_pages[current_page].rows[cursor_row].update)
                menu_pages[current_page].rows[cursor_row].update(true, cursor_col);
        }
    }

    if (sticks[YAW] < 10 && sticks[ROLL] > 10 && sticks[ROLL] < 90 && sticks[PITCH] > 10 && sticks[PITCH] < 90) {
        if (cursor_row > MAX_MENU_ROWS) {
            if (cursor_col == 2 && current_page > 0) {
                current_page--;
            }
        } else {
            if (menu_pages[current_page].rows[cursor_row].update)
                menu_pages[current_page].rows[cursor_row].update(false, cursor_col);
        }
    }

    if (sticks[PITCH] > 90 && sticks[YAW] > 10 && sticks[YAW] < 90) {
        if (cursor_row > MAX_MENU_ROWS) {
            cursor_row = menu_pages[current_page].rows_number - 1;
            cursor_col = 0;
        } else {
            if (cursor_row > 0)
                cursor_row--;
        }
    }
    if (sticks[PITCH] < 10 && sticks[YAW] > 10 && sticks[YAW] < 90) {
        if (cursor_row < (menu_pages[current_page].rows_number - 1))
            cursor_row++;
        else
            cursor_row = 255;
    }
    if (sticks[ROLL] > 90 && sticks[YAW] > 10 && sticks[YAW] < 90) {
        if (cursor_row > MAX_MENU_ROWS) {
            if (cursor_col < 2)
                cursor_col++;
        } else {
            if (cursor_col < (menu_pages[current_page].cols_number - 1))
                cursor_col++;
        }
    }
    if (sticks[ROLL] < 10 && sticks[YAW] > 10 && sticks[YAW] < 90) {
        if (cursor_col > 0)
            cursor_col--;
    }

    if (cursor_row > MAX_MENU_ROWS) {
        cursor_row = 255;
        cursor_y = 12;
        switch(cursor_col) {
            case 0:
                cursor_x = 0;
                break;
            case 1:
                cursor_x = 9;
                break;
            case 2:
                cursor_x = 23;
                break;
            default:
                cursor_x = 0;
        }
    }
    max7456_write_string(">", cursor_x + cursor_y * OSD_LINE_LENGTH);
}

void updateOsd(void)
{
    static uint8_t skip = 0;
    static bool blink = false;
    static uint8_t arming = 0;
    uint32_t seconds;
    char line[30];
    uint32_t now = micros();

    bool updateNow = (int32_t)(now - next_osd_update_at) >= 0L;
    if (!updateNow) {
        return;
    }
    next_osd_update_at = now + OSD_UPDATE_FREQUENCY;
    if ( !(skip % 2))
        blink = !blink;

    if (skip++ & 1) {
        if (ARMING_FLAG(ARMED)) {
            if (!armed) {
                armed = true;
                armed_at = now;
                in_menu = false;
                arming = 5;
            }
        } else {
            if (armed) {
                armed = false;
                armed_seconds += (now - armed_at) / 1000000;
            }
            for (uint8_t channelIndex = 0; channelIndex < 4; channelIndex++) {
                sticks[channelIndex] = (constrain(rcData[channelIndex], PWM_RANGE_MIN, PWM_RANGE_MAX) - PWM_RANGE_MIN) * 100 / (PWM_RANGE_MAX - PWM_RANGE_MIN);
            }
            if (!in_menu && sticks[YAW] < 10 && sticks[THROTTLE] > 10 && sticks[THROTTLE] < 90 && sticks[ROLL] > 10 && sticks[ROLL] < 90 && sticks[PITCH] > 90) {
                in_menu = true;
                cursor_row = 255;
                cursor_col = 2;
            }
        }
        if (in_menu) {
            show_menu();
        } else {
            if (batteryWarningVoltage > vbat && blink) {
                max7456_write_string("LOW VOLTAGE", 310);
            }
            if (arming && blink) {
                max7456_write_string("ARMED", 283);
                arming--;
            }
            if (!armed) {
                max7456_write_string("DISARMED", 281);
            }

            sprintf(line, "\x97%d.%1d", vbat / 10, vbat % 10);
            max7456_write_string(line, 361);
            sprintf(line, "\xba%d", rssi / 10);
            max7456_write_string(line, 331);
            sprintf(line, "\x7e%3d", (constrain(rcData[THROTTLE], PWM_RANGE_MIN, PWM_RANGE_MAX) - PWM_RANGE_MIN) * 100 / (PWM_RANGE_MAX - PWM_RANGE_MIN));
            max7456_write_string(line, 381);
            seconds = (now - armed_at) / 1000000 + armed_seconds;
            sprintf(line, "\x9C %02d:%02d", seconds / 60, seconds % 60);
            max7456_write_string(line, 351);
            print_average_system_load(26, 0);
        }
    } else {
        max7456_draw_screen_fast();
    }
}


void osdInit(void)
{
#ifdef USE_RTC6705
    rtc6705_init();
    current_vtx_channel = masterConfig.vtx_channel;
    rtc6705_set_channel(vtx_freq[current_vtx_channel]);
#endif
    max7456_init();

}

#endif
