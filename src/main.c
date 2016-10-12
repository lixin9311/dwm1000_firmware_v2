#include <string.h>

#include "deca_device_api.h"
#include "port.h"
#include "instance.h"

#define APP_NAME "DW1000 Firmware v2"

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

int main(void) {
  peripherals_init();
  debugf("%s\r",APP_NAME);

  reset_DW1000();
  spi_set_rate_low();
  dwt_initialise(DWT_LOADUCODE);
  spi_set_rate_high();


  dwt_configure(&config);
  set_pan(0xDECA);
  set_mac(0xFFF0);
  dwt_enableframefilter(DWT_FF_BEACON_EN | DWT_FF_DATA_EN | DWT_FF_ACK_EN | DWT_FF_MAC_EN | DWT_FF_RSVD_EN);
  dwt_setrxantennadelay(RX_ANT_DLY);
  dwt_settxantennadelay(TX_ANT_DLY);
  instance_init();
  dwt_setautorxreenable(1);
  dwt_rxenable(0);
  int_init();
  main_loop();
  return 0;
}
