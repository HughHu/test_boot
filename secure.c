/*
 * secure.c
 *
 *  Created on: 2023.8
 *      Author: 
 */



#include "chip.h"
#include <stdio.h>
#include <string.h>

#include "Driver_CRYPTO.h"
#include "secure.h"

static uint32_t boot_private_key[8];
static uint32_t boot_enc_key[8];
static uint32_t boot_sec_count = 0;
static uint8_t  boot_enc_ready = 0;
void* CRYPTO0_Handler;

static volatile int32_t CRYPTO_Result = CSK_DRIVER_OK;
static volatile uint32_t CRYPTO_DONE = 0;

static int32_t CRYPTO_BOOT_EventCallback(uint32_t event, int32_t result, void* workspace){
    if(CSK_CRYPTO_EVENT_WAIT_DONE == event)
    {
        while(!CRYPTO_DONE);
        CRYPTO_DONE = 0;
        return CRYPTO_Result;
    }
    else if(CSK_CRYPTO_EVENT_DONE == event)
    {
        CRYPTO_Result = result;
        CRYPTO_DONE = 1;
    }
    return CSK_DRIVER_OK;
}

// initialize secure module
int secure_init()
{
    boot_sec_count = 0;
    boot_enc_ready = 0;
    CRYPTO0_Handler = CRYPTO0();
    CRYPTO_Initialize(CRYPTO0_Handler, CRYPTO_BOOT_EventCallback, NULL);
    CRYPTO_PowerControl(CRYPTO0_Handler, CSK_CRYPTO_HW_ECC_RSA, CSK_POWER_FULL);
    CRYPTO_Control(CRYPTO0_Handler, CSK_CRYPTO_SET_ECC_CURVE, (uint32_t)&CRYPTO_ECC_CURVE_P256);

    return 1;
}

// shutdown secure module
int secure_shutdown()
{
    boot_enc_ready = 0;
    CRYPTO_PowerControl(CRYPTO0_Handler, CSK_CRYPTO_HW_ECC_RSA, CSK_POWER_OFF);
    CRYPTO_Uninitialize(CRYPTO0_Handler);

    return 1;
}

// get local public key
int secure_get_local_public_key(uint32_t *buff)
{
    CRYPTO_ECC_Generate_Key(CRYPTO0_Handler, SysTimer_GetLoadValue(), boot_private_key, buff);
    return 1;
}

// set peer public key
int secure_set_peer_public_key(uint32_t *buff, uint8_t *checksum)
{
    uint32_t key_buff[16];
    CRYPTO_ECC_Multiply(CRYPTO0_Handler, key_buff, buff, boot_private_key);

    // generate enc key
    for(int i=0;i<8;i++)
        boot_enc_key[i] = key_buff[i]^key_buff[8+i];

    *checksum = calculate_checksum(boot_enc_key, 32);
    boot_enc_ready = 1;

    return 1;
}

// decrypt data
int secure_decrypt_data(esp_command_req_t *cmd)
{
    if(!boot_enc_ready)
        return 0;
    if(cmd->data_len < 16)
        return 0;

    cmd->data_len -= 16;

    uint32_t aes_length[4] = {16, 12, 8, cmd->data_len};
    uint32_t nonce[4];
    nonce[0] = 0x7473694C;
    nonce[1] = 0x49416E65;
    nonce[2] = boot_sec_count++;
    nonce[3] = 0;

    CRYPTO_Control(CRYPTO0_Handler,CSK_CRYPTO_SET_AES_KEY_SIZE_256, 0);
    CRYPTO_Control(CRYPTO0_Handler,CSK_CRYPTO_SET_AES_MODE, CSK_CRYPTO_AES_MODE_GCM);
    CRYPTO_Control(CRYPTO0_Handler,CSK_CRYPTO_SET_AES_LENGTHS, (uint32_t)aes_length);
    CRYPTO_Control(CRYPTO0_Handler, CSK_CRYPTO_AES_KEY_MODE_USER, (uint32_t)boot_enc_key);
    CRYPTO_Control(CRYPTO0_Handler, CSK_CRYPTO_SET_AES_IV, (uint32_t)nonce);
    // process header
    CRYPTO_AES_Decrypt(CRYPTO0_Handler, (uint32_t *)cmd, 8, NULL);
    // copy mac
    memcpy(aes_length, cmd->data_buf + cmd->data_len, 16);
    // process data
    CRYPTO_AES_Decrypt(CRYPTO0_Handler, (uint32_t *)cmd->data_buf, cmd->data_len, (uint32_t *)cmd->data_buf);

    // get mac
    CRYPTO_Control(CRYPTO0_Handler,CSK_CRYPTO_GET_AES_MAC, nonce);
    if(memcmp(aes_length, nonce, 16)!=0)
        return -1;

    return 1;
}


