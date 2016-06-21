#include <string.h>

#include "instance.h"
#include "deca_device_api.h"
#include "deca_spi.h"
#include "deca_types.h"
#include "port.h"
#include "stm32f10x_it.h"
#include "deca_sleep.h"

#define RNG_DELAY_MS 1000

extern uint8 usart_status;
extern uint8 usart_rx_buffer[USART_BUFFER_LEN];
extern uint16 usart_index;
// beacon 应该是0x40 注意该死的字节序
static uint8 tx_poll_msg[] = {0x44, 0x88, 0, 0xCA, 0xDE, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0, 0};

uint8 usart_tx_buffer[USART_BUFFER_LEN];

uint8 auto_beacon = 0;
uint8 status = STATUS_IDLE;
uint8 mac[2];
uint8 pan[2];
uint8 bmac[2] = {0xFF, 0xFF};
uint8 bpan[2] = {0xFF, 0xFF};
uint8 rx_buffer[RX_BUF_LEN];
// 定位用
uint32 poll_tx_ts;
uint32 resp_rx_ts;

void set_status(const uint8 s) {
  status = s;
}

void set_mac(uint16 dmac) {
  mac[0] = dmac;
  mac[1] = dmac>>8;
  dwt_setaddress16(dmac);
}

void set_pan(uint16 dpan) {
  pan[0] = dpan;
  pan[1] = dpan>>8;
  dwt_setpanid(dpan);
}

void set_src(uint8 *buf) {
  buf[3] = pan[0];
  buf[4] = pan[1];
  buf[7] = mac[0];
  buf[8] = mac[1];
}

int check_addr(uint8 *buf) {
  if (memcmp(buf+3, pan, 2) == 0) {
    if (memcmp(buf+3, bpan, 2) == 0) {
      return 0;
    }
  }
  if (memcmp(buf+5, mac, 2) == 0) {
    if (memcmp(buf+5, bmac, 2) == 0) {
      return 0;
    }
  }
  return 1;
}

void calculate_distance(uint8 *target) {
  uint32 poll_rx_ts, resp_tx_ts;
  uint64 resp_rx_ts = dwt_readrxtimestamplo32();
  resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
  resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);
  int32 rtd_init = resp_rx_ts - poll_tx_ts;
  int32 rtd_resp = resp_tx_ts - poll_rx_ts;
  int tmp = (rtd_init - rtd_resp);
  // TODO
  // send_to_host(USART_BEACON);
  printf2("DIST(%02x%02x): %d\r\n", target[0], rx_buffer[1], tmp);
  // 因为内存和浮点数问题，这个数字就不在单片机上处理了
  // 距离计算方法: tmp / 2 * 1.0 / 499.2e6 / 128.0 * 299702547
}

void response_poll(uint8 *target) {
  uint8 tx_buffer[] = {0x45, 0x88, 0, 0xCA, 0xDE, 'Y', 'U', 'K', 'I', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  set_src(tx_buffer);
  tx_buffer[5] = target[0];
  tx_buffer[6] = target[1];
  dwt_forcetrxoff();
  uint64 poll_rx_ts = get_rx_timestamp_u64();
  uint32 resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
  dwt_setdelayedtrxtime(resp_tx_time);
  uint64 resp_tx_ts = (((uint64)(resp_tx_time & 0xFFFFFFFE)) << 8) + TX_ANT_DLY;
  resp_msg_set_ts(&tx_buffer[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
  resp_msg_set_ts(&tx_buffer[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);
  dwt_writetxdata(sizeof(tx_buffer), tx_buffer, 0);
  dwt_writetxfctrl(sizeof(tx_buffer), 0);
  int r = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
  if (r !=  DWT_SUCCESS) {
	  dwt_rxenable(0);
  } else {
	  // printf2("r\r\n");
  }
}

void instance_rxcallback(const dwt_callback_data_t *rxd) {
  // 如果成功接收并且CRC成功
  if (rxd->event == DWT_SIG_RX_OKAY) {
    if (RX_BUF_LEN >= rxd->datalength) {
      dwt_readrxdata(rx_buffer, rxd->datalength, 0);
    }
    if (check_addr(rx_buffer) == 0) {
      printf2("%s", "not for me.\r\n");
      return;
    }
    switch (rxd->fctrl[0]) {
      // beacon
      case 0x40:

        break;
      // data
      case 0x41:
        send_to_host(USART_MSG, (uint8)(rxd->datalength), rx_buffer);
        break;
      // ranging
      case 0x44:
        response_poll(rx_buffer+7);
        break;
      case 0x45:
        calculate_distance(rx_buffer+7);
        break;
      default:
        break;
    }

  } else if (rxd->event == DWT_SIG_RX_TIMEOUT) { // 超时

  } else { // 其他错误，清除状态机状态

  }
}

void instance_txcallback(const dwt_callback_data_t *txd) {
  if (txd->event == DWT_SIG_TX_DONE) {
    if (status == STATUS_POLL) {
      poll_tx_ts = dwt_readtxtimestamplo32();
      status = STATUS_IDLE;
    }
  }
}

void instance_init() {
  dwt_setcallbacks(instance_txcallback, instance_rxcallback);
  dwt_setinterrupt(DWT_INT_TFRS | DWT_INT_RFCG /*| (DWT_INT_ARFE | DWT_INT_RFSL | DWT_INT_SFDT | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFTO | DWT_INT_RXPTO)*/, 1);
}

uint64 get_rx_timestamp_u64(void) {
  uint8 ts_tab[5];
  uint64 ts = 0;
  int i;
  dwt_readrxtimestamp(ts_tab);
  for (i = 4; i >= 0; i--) {
    ts <<= 8;
    ts |= ts_tab[i];
  }
  return ts;
}

void resp_msg_get_ts(uint8 *ts_field, uint32 *ts) {
  int i;
  *ts = 0;
  for (i = 0; i < RESP_MSG_TS_LEN; i++) {
    *ts += ts_field[i] << (i * 8);
  }
}

void resp_msg_set_ts(uint8 *ts_field, const uint64 ts) {
  int i;
  for (i = 0; i < RESP_MSG_TS_LEN; i++) {
    ts_field[i] = (ts >> (i * 8)) & 0xFF;
  }
}

void main_loop(void) {
  while (1) {
      if (auto_beacon == 1) {
        set_src(tx_poll_msg);
        dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
        dwt_writetxfctrl(sizeof(tx_poll_msg), 0);
        set_status(STATUS_POLL);
        dwt_forcetrxoff();
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
      }
      deca_sleep(RNG_DELAY_MS);
  }
}

void usart_handle(void) {
  uint8 len;
  uint8 *payload;
  if (usart_status == 2) {
    switch (usart_rx_buffer[0]) {
      case USART_MSG:
        len = usart_rx_buffer[1];
        payload = usart_rx_buffer+2;
        set_src(payload);
        dwt_writetxdata((uint16)(len), payload, 0);
        dwt_writetxfctrl((uint16)(len), 0);
        dwt_forcetrxoff();
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
        break;
      case USART_BEACON:
        set_src(tx_poll_msg);
        dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
        dwt_writetxfctrl(sizeof(tx_poll_msg), 0);
        set_status(STATUS_POLL);
        dwt_forcetrxoff();
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
        break;
      case USART_SETADDR:
        set_pan((usart_rx_buffer[2]<<8) + usart_rx_buffer[3]);
        set_mac((usart_rx_buffer[4]<<8) + usart_rx_buffer[5]);
        break;
      case USART_RST:
      case USART_AUTOBEACON:
        auto_beacon = usart_rx_buffer[2];
        break;
      case USART_LOG:
      case 'a':
        auto_beacon = 1;
        break;
      case 'b':
        auto_beacon = 0;
        break;
    }
		memset(usart_rx_buffer, 0, USART_BUFFER_LEN);
		usart_status = 0;
		usart_index = 0;
	}
}
