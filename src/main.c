#include <string.h>

#include "deca_device_api.h"
#include "deca_regs.h"
#include "deca_types.h"
#include "deca_sleep.h"
#include "lcd.h"
#include "port.h"
#include "instance.h"

#define TX
// #define RX
#define APP_NAME "SS TWR INIT v1.1"
#define RNG_DELAY_MS 1000

/* Default communication configuration. We use here EVK1000's mode 4. See NOTE 1 below. */
static dwt_config_t config = {
    2,               /* Channel number. */
    DWT_PRF_64M,     /* Pulse repetition frequency. */
    DWT_PLEN_128,    /* Preamble length. */
    DWT_PAC8,        /* Preamble acquisition chunk size. Used in RX only. */
    9,               /* TX preamble code. Used in TX only. */
    9,               /* RX preamble code. Used in RX only. */
    0,               /* Use non-standard SFD (Boolean) */
    DWT_BR_6M8,      /* Data rate. */
    DWT_PHRMODE_STD, /* PHY header mode. */
    (129 + 8 - 8)    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
};

// beacon 应该是0x40 注意该死的字节序
static uint8 tx_poll_msg[] = {0x44, 0x88, 0, 0xCA, 0xDE, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0, 0};

int main(void)
{
    peripherals_init();
    printf2("%s\r\n",APP_NAME);

    reset_DW1000();
    spi_set_rate_low();
    dwt_initialise(DWT_LOADUCODE);
    spi_set_rate_high();


    dwt_configure(&config);
    #ifdef TX
      printf2("%s\r\n", "role: TX");
      set_pan(0xDECA);
      set_mac(0xFFF1);
    #endif
    #ifdef RX
      printf2("%s\r\n", "role: RX");
      set_pan(0xDECA);
      set_mac(0xFFF2);
    #endif

    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);

    dwt_setrxaftertxdelay(0);
    dwt_setrxtimeout(350);
    instance_init();
    while (1)
    {
      #ifdef TX
        set_src(tx_poll_msg);
        dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
        dwt_writetxfctrl(sizeof(tx_poll_msg), 0);
        set_status(STATUS_POLL);
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
        {
        /* Clear RX error events in the DW1000 status register. */
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
        }
        deca_sleep(RNG_DELAY_MS);
      #endif
    }
}
