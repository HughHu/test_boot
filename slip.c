/*
 * Copyright (c) 2016 Cesanta Software Limited & Espressif Systems (Shanghai) PTE LTD
 * All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
 * Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

//#include "rom_functions.h"
#include "slip.h"

struct slip
{
    char* tx_buf;
    char* rx_buf;
    int32_t tx_idx;
    int32_t rx_idx;
};

struct slip s_slip;

void
SLIP_init(char* tx_buf, char* rx_buf)
{
    s_slip.tx_buf = tx_buf;
    s_slip.rx_buf = rx_buf;
    s_slip.tx_idx = 0;
    s_slip.rx_idx = 0;
}

int32_t
SLIP_get_tx_size()
{
    return s_slip.tx_idx;
}

int32_t
SLIP_ge_rx_size()
{
    return s_slip.rx_idx;
}

void
uart_tx_one_char(char ch)
{
    s_slip.tx_buf[s_slip.tx_idx] = ch;
    s_slip.tx_idx++;
}

char
uart_rx_one_char_block()
{
    char ch;
    ch = s_slip.rx_buf[s_slip.rx_idx];
    s_slip.rx_idx++;
    return ch;
}
void
SLIP_send_frame_delimiter(void)
{
    uart_tx_one_char('\xc0');
}

void
SLIP_send_frame_data(char ch)
{
    if (ch == '\xc0') {
        uart_tx_one_char('\xdb');
        uart_tx_one_char('\xdc');
    } else if (ch == '\xdb') {
        uart_tx_one_char('\xdb');
        uart_tx_one_char('\xdd');
    } else {
        uart_tx_one_char(ch);
    }
}

void
SLIP_send_frame_data_buf(const void* buf, uint32_t size)
{
    int i;
    const uint8_t* buf_c = (const uint8_t*)buf;
    for (i = 0; i < size; i++) {
        SLIP_send_frame_data(buf_c[i]);
    }
}

void
SLIP_send(const void* pkt, uint32_t size)
{
    SLIP_send_frame_delimiter();
    SLIP_send_frame_data_buf(pkt, size);
    SLIP_send_frame_delimiter();
}

int16_t
SLIP_recv_byte(char byte, slip_state_t* state)
{
    //ifyLogPrint("SLIP_recv_byte, start state is %d\n", *state);
    if (byte == '\xc0') {
        if (*state == SLIP_NO_FRAME) {
            *state = SLIP_FRAME;
            //ifyLogPrint("SLIP get 0x%02x, state is %d\n", (uint8_t)byte, *state);
            return SLIP_NO_BYTE;
        } else {
            *state = SLIP_NO_FRAME;
            //ifyLogPrint("SLIP get 0x%02x, state is %d\n", (uint8_t)byte, *state);
            return SLIP_FINISHED_FRAME;
        }
    }

    switch (*state) {
        case SLIP_NO_FRAME:
            //ifyLogPrint("SLIP get 0x%02x, state is %d\n", (uint8_t)byte, *state);
            return SLIP_NO_BYTE;
        case SLIP_FRAME:
            if (byte == '\xdb') {
                *state = SLIP_FRAME_ESCAPING;
                //ifyLogPrint("SLIP get 0x%02x, state is %d\n", (uint8_t)byte, *state);
                return SLIP_NO_BYTE;
            }
            //ifyLogPrint("SLIP get 0x%02x, state is %d\n", (uint8_t)byte, *state);
            return (uint8_t)byte;
        case SLIP_FRAME_ESCAPING:
            if (byte == '\xdc') {
                *state = SLIP_FRAME;
                //ifyLogPrint("SLIP get 0x%02x, state is %d\n", (uint8_t)byte, *state);
                return (uint8_t)'\xc0';
            }
            if (byte == '\xdd') {
                *state = SLIP_FRAME;
                //ifyLogPrint("SLIP get 0x%02x, state is %d\n", (uint8_t)byte, *state);
                return (uint8_t)'\xdb';
            }
            //ifyLogPrint("SLIP get 0x%02x, frame error\n", (uint8_t)byte);
            return SLIP_NO_BYTE; /* actually a framing error */
    }
    //ifyLogPrint("SLIP get 0x%02x, frame error\n", byte);
    return SLIP_NO_BYTE; /* actually a framing error */
}

uint32_t
SLIP_recv(void* pkt, uint32_t max_len)
{
    uint32_t len = 0;
    slip_state_t state = SLIP_NO_FRAME;
    uint8_t* p = (uint8_t*) pkt;

    int16_t r;
    do {
        r = SLIP_recv_byte(uart_rx_one_char_block(), &state);
        if (r >= 0 && len < max_len) {
            p[len++] = (uint8_t)r;
        }
    } while (r != SLIP_FINISHED_FRAME);

    return len;
}
