/*
 * secure.h
 *
 *  Created on: 2023Äê8ÔÂ28ÈÕ
 *      Author: LAPTOP-07
 */

#ifndef TOOLS_UART_BURN_TOOL_CRYPTO_CRYPTO_H_
#define TOOLS_UART_BURN_TOOL_CRYPTO_CRYPTO_H_

#include "stub_load.h"

// initialize secure module
int secure_init();
// shutdown secure module
int secure_shutdown();
// get local public key
int secure_get_local_public_key(uint32_t *buff);
// set peer public key
int secure_set_peer_public_key(uint32_t *buff, uint8_t *checksum);
// decrypt data
int secure_decrypt_data(esp_command_req_t *cmd);


#endif /* TOOLS_UART_BURN_TOOL_CRYPTO_CRYPTO_H_ */
