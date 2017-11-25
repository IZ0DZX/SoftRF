/*
 *
 * Protocol_FANET.h
 *
 * Encoder and decoder for open FANET radio protocol
 * URL: https://github.com/3s1d/fanet-stm32/tree/master/Src/fanet
 *
 * Copyright (C) 2017 Linar Yusupov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PROTOCOL_FANET_H
#define PROTOCOL_FANET_H

#include <app.h>

#define FANET_PREAMBLE_TYPE   0 /* TBD */
#define FANET_PREAMBLE_SIZE   0 /* TBD */

#define FANET_SYNCWORD        { 0xF1 }  /* TBD */
#define FANET_SYNCWORD_SIZE   1         /* TBD */
/* MAC_FRM_MIN_HEADER_LENGTH + MAC_FRM_ADDR_LENGTH + MAC_FRM_SIGNATURE_LENGTH + APP_TYPE1_SIZE */
#define FANET_PAYLOAD_SIZE    21 
#define FANET_CRC_TYPE        0  /* TBD */
#define FANET_CRC_SIZE        0  /* TBD */

typedef struct {

  uint8_t data[FANET_PAYLOAD_SIZE];

} fanet_packet_t;

extern const rf_proto_desc_t fanet_proto_desc;

bool fanet_decode(void *, ufo_t *, ufo_t *);
size_t fanet_encode(void *, ufo_t *);

#endif /* PROTOCOL_FANET_H */
