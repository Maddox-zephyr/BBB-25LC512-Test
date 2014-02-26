#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "libsoc_spi.h"
#include "libsoc_debug.h"

/**
 * 
 * This spi_test is intended to be run on beaglebone black hardware
 * and uses the SPIDEV0 device on pins P9_22 as SCLK, P9_21 as d0 (MISO), 
 * P9_18 as d1(MOSI), and P9_17 as CS.  Be sure to enable SPI0 by loading
 * an appropriate Device Tree Compiler Overlay.
 *
 * The BeagleBone is connected to a MicroChip 25LC512-I/P 64K EEPROM.  Same connection for the LC1024.
 *
 *                                  _____________
 *   P9_17     Chip Select   Pin 1 | CS      VCC | Pin 8     VCC (3.3v)  P9_4
 *   P9_21     d0 MISO       Pin 2 | d0     Hold | Pin 7     VCC (3.3v)  P9_4
 *   P9_4      VCC (3.3v)    Pin 3 | WP     SCLK | Pin 6     SCLK        P9_22
 *   P9_1      GND           Pin 4 | GND      d1 | Pin 5     MOSI d1     P9_18
 *                                 ---------------
 *
 * The test covers erase the entire chip (0xFF), writing all the pages worth of data (128 bytes),
 * then read each page back, and compare the data
 * read against the data sent.
 * 
 *  2/25/2014 by Maddox at zephyr-labs.com (adapted from spi example for 25LC640 at the libsoc git repository "spi_test.c")
 */
 
#define SPI_DEVICE   1
#define CHIP_SELECT  0

#define EEPROM_DEVICE_ID    0x29        // Microchip's ID = 0x29 for the LC512 and LC1024
#define EEPROM_SIZE 524288/8            // This device is 64k bytes, 512Kbits
#define EEPROM_PAGE_SIZE 128
#define EEPROM_NUM_PAGES EEPROM_SIZE/EEPROM_PAGE_SIZE    // 512 pages of 128 bytes
#define EEPROM_WRITE_DELAY  3300000     // Delay in nanoseconds to wait for the write to complete (5 ms max)
#define EEPROM_ERASE_DELAY  6800000     // Delay in nanoseconds to wait for the whole chip to erase

#define WREN	0x06        // write enable
#define WRDI	0x04        // write disable
#define WRITE	0x02	    // initialize start of write sequence
#define READ	0x03        // initialize start of read sequence
#define CE	    0xc7	    // erase all sectors of memory (LC512/LC1024)
#define RDSR    0x05        // read STATUS register
#define WRSR    0x01        // Write Status Register
#define PE      0x42        // Page Erase (LC512/LC1024)
#define SE	    0xd8	    // Sector Erase (same function as Page Erase on this device) (LC512/LC1024)
#define	RDID	0xab	    // Release from Deep power-down & read electronic signature (LC512/LC1024)
#define	DPD	    0xb9	    // Deep Power-Down mode (LC512/LC1024)

static uint8_t tx[EEPROM_PAGE_SIZE+3], rx[EEPROM_PAGE_SIZE+3];
static uint8_t status = 0;


uint8_t read_status_register(spi* spi_dev) {
    
  printf("Reading STATUS register\n");
    
  tx[0] = RDSR;
  tx[1] = 0;
  rx[0] = 0;
  rx[1] = 0;
  
  libsoc_spi_rw(spi_dev, tx, rx, 2);
  
  printf("STATUS is 0x%02x\n", rx[1]);
  
  return rx[1];
}

uint8_t release_pwrdwn_read_sig(spi* spi_dev) {
    
  printf("Releasing from Deep power-down and Read Electronic Signature\n");
    
  tx[0] = RDID;
  tx[1] = 0;
  tx[2] = 0;
  tx[3] = 0;
  rx[0] = 0;
  rx[1] = 0;
  rx[2] = 0;
  rx[3] = 0;
  
  libsoc_spi_rw(spi_dev, tx, rx, 4);
  
  printf("STATUS is 0x%02x\n", rx[3]);
  
  return rx[3];
}

int set_write_enable(spi* spi_dev) {
  
  tx[0] = WREN;
  
  printf("Setting write enable bit\n");
   
  libsoc_spi_write(spi_dev, tx, 1);
  
  return EXIT_SUCCESS;
}

int erase_device(spi* spi_dev) {
  
  set_write_enable(spi_dev);
  
  tx[0] = CE;
  
  printf("Erase all Sectors\n");
   
  libsoc_spi_write(spi_dev, tx, 1);
  
  nanosleep((struct timespec[]){{0, EEPROM_ERASE_DELAY}}, NULL);   // Erases can take around 5 ms
  
  
  do {
  
    status = read_status_register(spi_dev);
    
    if (status & 0x01) {
      printf("Erase in progress...\n");
    } else {
      printf("Erase finished...\n");
    }
    
  } while (status & 0x1);
  
  return EXIT_SUCCESS;
}


int write_page(spi* spi_dev, uint16_t page_address, uint8_t* data, int len) {
  
  if (len > EEPROM_PAGE_SIZE) {
    printf("Page size is 128 bytes\n");
    return EXIT_FAILURE;
  }
  
  set_write_enable(spi_dev);
  
  printf("Writing to page %d\n", page_address);
  
  page_address = page_address * EEPROM_PAGE_SIZE;
  
  tx[0] = WRITE;
  
  tx[1] = (page_address >> 8);
  tx[2] = page_address;
  
  
  int i;
  
  for (i=0; i<len; i++) {
  
    tx[(i+3)] = data[i];
  }
   
  libsoc_spi_write(spi_dev, tx, (len+3));
  
  nanosleep((struct timespec[]){{0, EEPROM_WRITE_DELAY}}, NULL);   // Writes can take around 5 ms, observed 3.7 msec
  
  
  do {
  
    status = read_status_register(spi_dev);
    
    if (status & 0x01) {
      printf("Write in progress...\n");
    } else {
      printf("Write finished...\n");
    }
    
  } while (status & 0x1);
  
  return EXIT_SUCCESS;
  
}

int read_page(spi* spi_dev, uint16_t page_address, uint8_t* data, int len) {
  
/*  printf("Reading page address %d\n", page_address); */
  
  tx[0] = READ;
  
  page_address = page_address * EEPROM_PAGE_SIZE;
  
  tx[1] = (page_address >> 8);
  tx[2] = page_address;
  
  libsoc_spi_rw(spi_dev, tx, rx, (len+3));
  
  int i;
  
  for (i=0; i<len; i++)
  {
    data[i] = rx[(i+3)];
  }
  
  return EXIT_SUCCESS;
}

int set_deep_power_down(spi* spi_dev) {
  
  tx[0] = DPD;
  
  printf(" : Putting Device into Deep Power Down\n");
   
  libsoc_spi_write(spi_dev, tx, 1);
  
  return EXIT_SUCCESS;
}

/*
 * MAIN
 */

int main()
{
  libsoc_set_debug(0);
   
  uint16_t page;
  int i;

  spi* spi_dev = libsoc_spi_init(SPI_DEVICE, CHIP_SELECT);

  if (!spi_dev)
  {
    printf("Failed to get spidev device!\n");
    return EXIT_FAILURE;
  }

  libsoc_spi_set_mode(spi_dev, MODE_0);
  libsoc_spi_get_mode(spi_dev);
  
  libsoc_spi_set_speed(spi_dev, 10000000);	// Change from 1MHz to 10Mhz
  libsoc_spi_get_speed(spi_dev);
  
  libsoc_spi_set_bits_per_word(spi_dev, BITS_8);
  libsoc_spi_get_bits_per_word(spi_dev);
  
 
  int len = EEPROM_PAGE_SIZE;
  
  uint8_t data[EEPROM_PAGE_SIZE], data_read[EEPROM_PAGE_SIZE];
  

  for (i=0; i<len; i++) {     // Initialize buffer with incrementing pattern from 0 to 127
    data[i] = i;
  }
/*
 *  Read Device ID (and release from deep power)
*/

 status = release_pwrdwn_read_sig(spi_dev);     // Release from Deep power-down and Read Electronic Signature

 if(status == EEPROM_DEVICE_ID){
/*
 *   Erase the whole EEPROM
 */
  erase_device(spi_dev);        // Start with all 0xFF bytes everywhere

/* 
 *  Verify that page 0, byte 0 is 0xFF
 */
 
 page = 0;
 len = 1;           // Read one byte
 printf("Reading page 0, byte 0 to validate erase\n");
 
 read_page(spi_dev, page, data_read, len);
 
 if(data_read[0] != 0xFF) {
     printf(" Expected page 0, byte 0 to be 0xFF, but data_read[0] = 0x%02x", data_read[0]);
     
     libsoc_spi_free(spi_dev);

     return EXIT_FAILURE;
 }
 
  
/*
 *   Write EEPROM page-by-page
 */
  
  len = EEPROM_PAGE_SIZE;
  
  for (page = 0; page < EEPROM_NUM_PAGES; page++) {
  
  write_page(spi_dev, page, data, len);
} 

/*
 *   Read EEPROM page-by-page
 */
 
  printf(" : Read all pages back and compare to what we wrote\n");
  
  for (page = 0; page < EEPROM_NUM_PAGES; page++) {
      
  read_page(spi_dev, page, data_read, len);
  
  for (i=0; i<len; i++) {
    /* printf("data[%d] = 0x%02x : data_read[%d] = 0x%02x", i, data[i], i, data_read[i]); */
    
    if (data[i] != data_read[i]) {
      printf("Data Miscompare Error: Page %d : data[%d] = 0x%02x : data_read[%d] = 0x%02x", page, i, data[i], i, data_read[i]);
    } 
  }
}
  printf(" : Test Complete\n");
  
  set_deep_power_down(spi_dev);
  
  libsoc_spi_free(spi_dev);

  return EXIT_SUCCESS;
}

/*
 * If this Device is not a 25LC512...
 */

 else {
      printf("Test Terminated - Wrong Device\n");
      
      set_deep_power_down(spi_dev);
      
      libsoc_spi_free(spi_dev);
      
      return EXIT_FAILURE;
  }
  status = 0;
}