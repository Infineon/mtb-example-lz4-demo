/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for LZ4 Demo Example using HAL APIs.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2022, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "lz4.h"
#include "application_img.h"
#include <string.h>

/*******************************************************************************
* Macros
*******************************************************************************/
/* Macro to ceil the value of two numbers x and y.*/
#define CEILING(x,y) (((x) + (y) - 1) / (y))

/* The size of the Flash page.  */
#define FLASH_PAGE_SIZE         (512u)

/* The number of Flash pages needed to store the image.  */
#define PAGE_COUNT             CEILING(IMAGE_SIZE, FLASH_PAGE_SIZE)

/* Commands for compression, decompression and execution of image. */
#define COMPRESSION_CMD         'c'
#define DECOMPRESSION_CMD       'd'
#define EXECUTION_CMD           'e'

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
static int32_t decompress_string(uint8_t* cmp_string, uint8_t* uncomp_string, int32_t str_len);
static int32_t compress_string(const uint8_t* str_src, uint8_t* str_dst, int32_t str_len);
static cy_rslt_t flash_write_fn(cyhal_flash_t flash_obj, uint32_t flash_data_ptr, uint8_t * src_str_ram);
static void execute_app(uint32_t * app_address);

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* Variable for storing character read from terminal. */
static uint8_t uart_read_value;

/* Address pointing to the flash location where the de-compressed image is stored. */
static uint32_t *flash_ptr = (uint32_t *)FLASH_IMG_ADDRESS;

/* Pointer to function that is used to jump into reset handler address */
typedef void (*_fn_jump_ptr_t)(void);

/* Flash object */
static cyhal_flash_t flash_obj;

/* Array to store compressed image. */
static uint8_t compressed_image[IMAGE_SIZE] = {0};

/* Array to store decompressed image. */
static uint8_t decompressed_image[IMAGE_SIZE] = {0};

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function. It Initializes the device and board peripherals.
* The loop checks for the user command and process the commands.
* The command can be to compress the image (bootable_arr), decompress the
* image or to execute the decompressed image.
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

#if defined (CY_DEVICE_SECURE)
    cyhal_wdt_t wdt_obj;

    /* Clear watchdog timer so that it doesn't trigger a reset */
    result = cyhal_wdt_init(&wdt_obj, cyhal_wdt_get_max_timeout_ms());
    CY_ASSERT(CY_RSLT_SUCCESS == result);
    cyhal_wdt_free(&wdt_obj);
#endif /* #if defined (CY_DEVICE_SECURE) */

    /* Number of bytes compressed. */
    int32_t compressed_bytes = 0;

    /* Decompressed status. */
    int32_t decompressed_status = 0;

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    
    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* In the case of software reset switch to app image. */
    if((CYHAL_SYSTEM_RESET_SOFT == cyhal_system_get_reset_reason()) && ((*flash_ptr) != 0))
    {
        /* Boot to application image.*/
        execute_app(flash_ptr);
    }

    /* Initialize retarget-io to use the debug UART port */
    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX,
                                 CY_RETARGET_IO_BAUDRATE);

    /* retarget-io init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("****************** "
           "LZ4 compression and decompression demo"
           "****************** \r\n\n");

    /* Display available commands */
    printf("Available commands \r\n");
    printf("c : Compress the application image\r\n");
    printf("d : De-compress the application image and store it in flash\r\n");
    printf("e : Execute the de-compressed application image\r\n\n");

    for (;;)
    {
        /* Check if 'Enter' key was pressed */
        if (CY_RSLT_SUCCESS == cyhal_uart_getc(&cy_retarget_io_uart_obj, &uart_read_value, 1))
        {
            if (COMPRESSION_CMD == uart_read_value) /*Compress the bin image */
            {
                printf("Starting LZ4 Compression...");
                compressed_bytes = compress_string(bootable_arr, compressed_image, IMAGE_SIZE);
                if(compressed_bytes == 0)
                {
                    printf("The LZ4 compression has failed\r\n");
                }
                printf("Done.\r\n");
            }
            else if (DECOMPRESSION_CMD == uart_read_value) /* de-compress the compressed image */
            {
                printf("LZ4 Decompression:");
                decompressed_status = decompress_string(compressed_image, decompressed_image, compressed_bytes);
                if(decompressed_status < 0)
                {
                    printf("The LZ4 decompression has failed\r\n");
                }
                else
                {
                    if(!strcmp((char*)bootable_arr, (char*)decompressed_image))
                    {
                        printf("Done. The source image and the de-compressed image are the same.\r\n");
                        result = flash_write_fn(flash_obj, (uint32_t)flash_ptr, decompressed_image);
                        if(result != CY_RSLT_SUCCESS)
                        {
                            printf("The Flash write failed\r\n");
                        }
                    }
                }
            }
            else if (EXECUTION_CMD == uart_read_value) /* execute the de-compressed image */
            {
               __NVIC_SystemReset();
            }
        }
    }
}

/*******************************************************************************
* Function Name: compress_string
********************************************************************************
* Summary:
* This is the function to compress a string.
*
* Parameters:
*    str_src       source string to be compressed.
*    str_dst       pointer to the decompressed image
*    str_len       size of source string to be compressed.
*
* Return:
*    bytes_passed  the number of bytes written into str_dst buffer
*
*******************************************************************************/
static int32_t compress_string(const uint8_t* str_src, uint8_t* str_dst, int32_t str_len)
{
    int32_t bytes_passed = LZ4_compress_default((char*)str_src, (char*)str_dst, str_len, IMAGE_SIZE);
    return bytes_passed;
}

/*******************************************************************************
* Function Name: decompress_string
********************************************************************************
* Summary:
* This is the function to decompress a string.
*
* Parameters:
*    cmp_string          source string to be decompressed.
*    uncomp_string       pointer to the uncompressed image
*    str_len             complete size of the compressed block.
*
* Return:
*    bytes_passed        the number of bytes decompressed into destination buffer
*
*******************************************************************************/
static int32_t decompress_string(uint8_t* cmp_string, uint8_t* uncomp_string, int32_t str_len)
{
    int32_t decompressed_len = 0;
    decompressed_len =  LZ4_decompress_safe ((char*)cmp_string, (char*)uncomp_string, str_len, IMAGE_SIZE);
    return decompressed_len;
}

/*******************************************************************************
* Function Name: flash_write_fn
********************************************************************************
* Summary:
* This is the function to write to the flash memory. It first erases the
* sectors of the flash, where we are writing data. Then performs a blocking
* write to the flash.
*
* Parameters:
*    flash_obj           Flash object
*    flash_data_ptr      destination flash location for the write operation
*    src_str_ram         Source address for the write operation.
*
* Return:
*    result              result indicating status of flash write operation
*
*******************************************************************************/
static cy_rslt_t flash_write_fn(cyhal_flash_t flash_obj, uint32_t flash_data_ptr, uint8_t * src_str_ram)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;            /* Flash Write Status */
    int flash_page_no = 0;

    /* Blocking flash erase prior to flash write */
    for(flash_page_no=0; flash_page_no < PAGE_COUNT; flash_page_no++)
    {
        result = cyhal_flash_erase(&flash_obj, (uint32_t)flash_data_ptr);
        if (result != CY_RSLT_SUCCESS)
        {
            CY_ASSERT(0);
        }
        result = cyhal_flash_write(&flash_obj, (uint32_t)flash_data_ptr, (uint32_t*) src_str_ram);
       if (result != CY_RSLT_SUCCESS)
       {
           CY_ASSERT(0);
       }
       flash_data_ptr = flash_data_ptr + FLASH_PAGE_SIZE;
       src_str_ram = src_str_ram + FLASH_PAGE_SIZE;
    }
    return result;
}

/*******************************************************************************
* Function Name: execute_app
********************************************************************************
* Summary:
*   Function to execute an app (custom bootloader).
*
* Parameters:
*  app_address      address in flash where the image is stored
*
* Return:
*    void
*
*******************************************************************************/
static void execute_app(uint32_t * app_address )
{
    /*Calling the reset handler of the second application.*/
    ((_fn_jump_ptr_t) ((uint32_t )*(++app_address)))();
}

/* [] END OF FILE */
