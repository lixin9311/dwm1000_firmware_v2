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
static uint8 tx_poll_msg[] = {0x44, 0x88, 0, 0xCA, 0xDE, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0, 0};

uint8 usart_tx_buffer[USART_BUFFER_LEN];

uint8 TDOA_status = STATUS_IDLE;
uint8 TDOA_id;
uint64 TDOA_T1;
uint64 TDOA_T2;
uint64 TDOA_T3;
uint64 TDOA_T4;

uint8 auto_beacon = 0;
uint8 status = STATUS_IDLE;
uint8 mac[2];
uint8 pan[2];
uint8 bmac[2] = {0xFF, 0xFF};
uint8 bpan[2] = {0xFF, 0xFF};
uint8 rx_buffer[RX_BUF_LEN];
uint8 silent = 0;
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

void set_dst(uint8 *buf, uint8 *addr) {
  buf[5] = addr[0];
  buf[6] = addr[1];
}

int check_addr(uint8 *buf) {
  if (memcmp(buf+3, pan, 2) != 0) {
    if (memcmp(buf+3, bpan, 2) != 0) {
      return 0;
    }
  }
  if (memcmp(buf+5, mac, 2) != 0) {
    if (memcmp(buf+5, bmac, 2) != 0) {
      return 0;
    }
  }
  return 1;
}

void dump_u64(uint64 i) {
  debugf("%02x%02x%02x%02x%02x", (uint8)((i>>32)&0xff),(uint8)((i>>24)&0xff),(uint8)((i>>16)&0xff),(uint8)((i>>8)&0xff),(uint8)((i>>0)&0xff));
}

void beacon(void) {
  set_src(tx_poll_msg);
  dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
  dwt_writetxfctrl(sizeof(tx_poll_msg), 0);
  set_status(STATUS_POLL);
  dwt_forcetrxoff();
  dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
}

void tdoa_beacon(uint8 id) {
  uint8 tx_tdoa_msg[] = {0x46, 0x88, 0, 0xCA, 0xDE, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0};
  set_src(tx_tdoa_msg);
  tx_tdoa_msg[9] = id;
  dwt_writetxdata(sizeof(tx_tdoa_msg), tx_tdoa_msg, 0);
  dwt_writetxfctrl(sizeof(tx_tdoa_msg), 0);
  dwt_forcetrxoff();
  dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
}

void calculate_distance(uint8 *target) {
  uint64 delay;
  uint32 resp_rx_ts = dwt_readrxtimestamplo32();
  resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_DELAY_IDX+1], &delay, RESP_MSG_TS_LEN);
  int32 rtd_init = resp_rx_ts - poll_tx_ts;
  int32 rtd_resp = (int32)delay;
  int32 tmp = (rtd_init - rtd_resp);
  usart_tx_buffer[0] = target[0];
  usart_tx_buffer[1] = target[1];
  usart_tx_buffer[2] = ((tmp)&0xff);
  usart_tx_buffer[3] = ((tmp>>8)&0xff);
  usart_tx_buffer[4] = ((tmp>>16)&0xff);
  usart_tx_buffer[5] = ((tmp>>24)&0xff);
  send_to_host(USART_BEACON, 6, usart_tx_buffer);
  // printf2("DIST(%02x%02x): %d\r\n", target[0], rx_buffer[1], tmp);
  // 因为内存和浮点数问题，这个数字就不在单片机上处理了
  // 距离计算方法: tmp / 2 * 1.0 / 499.2e6 / 128.0 * 299702547
}

void response_poll(uint8 *target) {
  uint8 tx_buffer[] = {0x44, 0x88, 0, 0xCA, 0xDE, 'Y', 'U', 'K', 'I', 0x02, 0, 0, 0, 0, 0, 0};
  set_src(tx_buffer);
  tx_buffer[5] = target[0];
  tx_buffer[6] = target[1];
  dwt_forcetrxoff();
  uint64 poll_rx_ts = get_rx_timestamp_u64();
  // 100ms 换算成计数器是0x17CDC00，十进制‭24960000‬，3CF00是1ms，这里随机一下，防止发生帧碰撞
  uint32 resp_tx_time = ((poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8) + (0x3CF00 * (0));
  dwt_setdelayedtrxtime(resp_tx_time);
  uint64 resp_tx_ts = (((uint64)(resp_tx_time & 0xFFFFFFFE)) << 8) + TX_ANT_DLY;
  resp_msg_set_ts(&tx_buffer[RESP_MSG_POLL_DELAY_IDX+1], resp_tx_ts - poll_rx_ts, RESP_MSG_TS_LEN);
  dwt_writetxdata(sizeof(tx_buffer), tx_buffer, 0);
  dwt_writetxfctrl(sizeof(tx_buffer), 0);
  int r = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
  if (r !=  DWT_SUCCESS) {
	  dwt_rxenable(0);
  } else {
	  // printf2("r\r\n");
  }
}

void TDOA_init(uint8 *target, uint8 *id) {
  uint8 tx_buffer[] = {0x45, 0x88, 0, 0xCA, 0xDE, 'Y', 'U', 'K', 'I', 0x01, 0, 0, 0};
  set_src(tx_buffer);
  tx_buffer[5] = target[0];
  tx_buffer[6] = target[1];
  tx_buffer[RESP_MSG_POLL_DELAY_IDX+1] = *id;
  dwt_forcetrxoff();
  dwt_writetxdata(sizeof(tx_buffer), tx_buffer, 0);
  dwt_writetxfctrl(sizeof(tx_buffer), 0);
  TDOA_status = TDOA_INIT;
  int r = dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
  if (r !=  DWT_SUCCESS) {
    dwt_rxenable(0);
  } else {
    // printf2("r\r\n");
  }
}

void TDOA_stage1(uint8 *target, uint8 *id) {
  // 同步指令，记录RX Timestamp，并发送，记录TX timestamp
  uint8 tx_buffer[] = {0x45, 0x88, 0, 0xCA, 0xDE, 'Y', 'U', 'K', 'I', 0x02, 0, 0, 0, 0, 0, 0, 0, 0};
  set_src(tx_buffer);
  tx_buffer[5] = target[0];
  tx_buffer[6] = target[1];
  tx_buffer[RESP_MSG_POLL_DELAY_IDX+1] = *id;
  dwt_forcetrxoff();
  TDOA_T2 = get_rx_timestamp_u64();
  resp_msg_set_ts(&tx_buffer[RESP_MSG_POLL_DELAY_IDX+2], TDOA_T2, 5);
  dwt_writetxdata(sizeof(tx_buffer), tx_buffer, 0);
  dwt_writetxfctrl(sizeof(tx_buffer), 0);
  TDOA_status = TDOA_SEND;
  TDOA_id = *id;
  int r = dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
  if (r !=  DWT_SUCCESS) {
    dwt_rxenable(0);
  } else {
    // printf2("r\r\n");
  }
}

void TDOA_handler(uint8 *target, uint8 *id) {
  // 同步指令，记录RX Timestamp，并发送，记录TX timestamp
  uint64 ts = get_rx_timestamp_u64();
  usart_tx_buffer[0] = pan[3];
  usart_tx_buffer[1] = pan[4];
  usart_tx_buffer[2] = target[0];
  usart_tx_buffer[3] = target[1];
  usart_tx_buffer[4] = *id;
  resp_msg_set_ts(usart_tx_buffer+5, ts, 5);
  send_to_host(USART_TS, 10, usart_tx_buffer);
}

void TDOA_stage2(uint8 *target, uint8 *id) {
  TDOA_T4 = get_rx_timestamp_u64();
  resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_DELAY_IDX+2], &TDOA_T2, 5);
  usart_tx_buffer[0] = pan[0];
  usart_tx_buffer[1] = pan[1];
  usart_tx_buffer[2] = target[0];
  usart_tx_buffer[3] = target[1];
  usart_tx_buffer[4] = *id;
  resp_msg_set_ts(usart_tx_buffer+5, TDOA_T1, 5);
  resp_msg_set_ts(usart_tx_buffer+10, TDOA_T2, 5);
  resp_msg_set_ts(usart_tx_buffer+15, TDOA_T4, 5);
  send_to_host(USART_CLKSYNC, 20, usart_tx_buffer);
}

void instance_rxcallback(const dwt_callback_data_t *rxd) {
  // 如果成功接收并且CRC成功
  if (rxd->event == DWT_SIG_RX_OKAY) {
    if (RX_BUF_LEN >= rxd->datalength) {
      dwt_readrxdata(rx_buffer, rxd->datalength, 0);
    }
    if (check_addr(rx_buffer) == 0) {
      // debugf("%s", "not for me.\r");
      // debugf("target: %02x%02x%02x%02x, me: %02x%02x%02x%02x\r", rx_buffer[3],rx_buffer[4], rx_buffer[5], rx_buffer[6], pan[0], pan[1], mac[0], mac[1]);
      dwt_rxenable(0);
      return;
    }
    switch (rxd->fctrl[0]) {
      // beacon
      // case 0x40:
      //   break;
      // data
      case 0x41:
        send_to_host(USART_MSG, (uint8)(rxd->datalength), rx_buffer);
        dwt_rxenable(0);
        break;
      // ranging
      case 0x44:
        // 可以使用帧过滤
        if (rx_buffer[9] == 0x01) {
          if (silent == 1) {
            dwt_rxenable(0);
            break;
          }
          response_poll(rx_buffer+7);
        } else {
          calculate_distance(rx_buffer+7);
          dwt_rxenable(0);
        }
        break;
      case 0x45:
        if (rx_buffer[9] == 0x01) {
          // 同步指令，记录RX Timestamp，并发送，记录TX timestamp
          TDOA_stage1(rx_buffer+7, rx_buffer+10);
        } else if (rx_buffer[9] == 0x02) {
          // 同步返回，记录RX Timestamp
          TDOA_stage2(rx_buffer+7, rx_buffer+10);
          dwt_rxenable(0);
        }
        break;
      case 0x46:
        TDOA_handler(rx_buffer+7, rx_buffer+9);
        dwt_rxenable(0);
        break;
      // case 0x47:
      //   break;
      default:
        dwt_rxenable(0);
        break;
    }

  } else if (rxd->event == DWT_SIG_RX_TIMEOUT) { // 超时
	  dwt_rxenable(0);
  } else { // 其他错误，清除状态机状态
	  dwt_rxenable(0);
  }
}

void instance_txcallback(const dwt_callback_data_t *txd) {
  if (txd->event == DWT_SIG_TX_DONE) {
    if (status == STATUS_POLL) {
      poll_tx_ts = dwt_readtxtimestamplo32();
      status = STATUS_IDLE;
    }
    if (TDOA_status == TDOA_INIT) {
      TDOA_T1 = get_tx_timestamp_u64();
      TDOA_status = STATUS_IDLE;
    } else if(TDOA_status == TDOA_SEND) {
      TDOA_T3 = get_tx_timestamp_u64();
      TDOA_status = STATUS_IDLE;
      usart_tx_buffer[0] = pan[0];
      usart_tx_buffer[1] = pan[1];
      usart_tx_buffer[2] = mac[0];
      usart_tx_buffer[3] = mac[1];
      usart_tx_buffer[4] = TDOA_id;
      resp_msg_set_ts(usart_tx_buffer+5, TDOA_T3, 5);
      send_to_host(USART_CLKSYNC, 10, usart_tx_buffer);
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

uint64 get_tx_timestamp_u64(void) {
  uint8 ts_tab[5];
  uint64 ts = 0;
  int i;
  dwt_readtxtimestamp(ts_tab);
  for (i = 4; i >= 0; i--) {
    ts <<= 8;
    ts |= ts_tab[i];
  }
  return ts;
}

void resp_msg_get_ts(uint8 *ts_field, uint64 *ts, int len) {
  int i;
  *ts = 0;
  for (i = 0; i < len; i++) {
    *ts |= (uint64)ts_field[i] << (i * 8);
  }
}

void resp_msg_set_ts(uint8 *ts_field, const uint64 ts, int len) {
  int i;
  for (i = 0; i < len; i++) {
    ts_field[i] = (ts >> (i * 8)) & 0xFF;
  }
}

void main_loop(void) {
  while (1) {
  }
}

void reset(void) {
  reset_DW1000();
  spi_set_rate_low();
  dwt_initialise(DWT_LOADUCODE);
  spi_set_rate_high();
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
        set_dst(tx_poll_msg, &usart_rx_buffer[1]);
        dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
        dwt_writetxfctrl(sizeof(tx_poll_msg), 0);
        set_status(STATUS_POLL);
        dwt_forcetrxoff();
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
        break;
      case USART_SETADDR:
        set_pan((usart_rx_buffer[3]<<8) + usart_rx_buffer[2]);
        set_mac((usart_rx_buffer[5]<<8) + usart_rx_buffer[4]);
        break;
      case USART_RST:
        reset();
        break;
      case USART_KEEPSILENT:
        silent = usart_rx_buffer[2];
        break;
      case USART_AUTOBEACON:
        if (usart_rx_buffer[2] == 1) {
          enable_auto_beacon();
        } else {
          disable_auto_beacon();
        }
        break;
      case USART_LOG:
      case USART_CLKSYNC:
        TDOA_init(usart_rx_buffer+3, usart_rx_buffer+2);
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
