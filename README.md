# DWM1000 Firmware v2 [STM32 Program]

## 简介
基于UWB室内定位的下位机驱动。

第二版程序，使用decadriver抽象驱动。

定位精度：20cm

定位频率：每秒1~30次

使用频率：6.5GHz

使用带宽：500MHz

使用功率：<-35dBm/MHz

基本架构
~~~
         SPI        UART
DWM1000 <===> STM32 <===> Host(PC/Android/MCU)
~~~

## Build
首先搞定依赖

```shell
sudo apt-get install build-essential binutils-arm-none-eabi \
  gdb-arm-none-eabi gcc-arm-none-eabi openocd
```

使用make编译，并刷写

```shell
make install
make clean
```

### 天线延时的设置

从距离到时间的换算公式为:
延时 = 距离(米) / 4.6917519677e-3

其中4.6917519677e-3为40位计数器一个周斯内电磁波的传播距离

天线延迟主要来自于64MHz PRF产生的误差，这个误差换算成天线延迟大概是16436。

如果需要更高精度，需要手动调校。


### 输出内容的设置

为了不与Glibc冲突，使用printf2进行串口输出。

## 与下位机通讯

Baudrate 115200

基础报文如下，PC到STM32无需转义，驱动层注意加锁，以及每次写入完毕后保持一个10ms的延迟。
STM32到PC端是经过一个类SLIP转义的。
~~~
类SLIP转义:
0xDB为转义标志, '\n'为结束标志
0xDB被转义成0xDB, 0xDC
'\n'被转义成0xDB, 0xDD


MSG
+-------+--------+--------+-----------------+
| Bytes |    0   |    1   |        N        |
+-------+--------+--------+-----------------+
|  def  |  TYPE  |  LEN   | Payload(126MAX) |
+-------+--------+--------+-----------------+

消息最大长度为128 (126 + 2) 字节
  1. 消息类型 Type
    0x00: 消息
    0x01: 测距以及Beacon
    0x02: 设置MAC地址以及PANID
    0x03: RESET
    0x04: Auto Beacon
    0x05: Log Msg
  2. Packet Length
    Payload长度，以无符号8位整型(unsigned char)表示
  3. Payload
    不同消息参照不同的内容

0x00 消息

Payload的基础格式参照`IEEE 802.15.4a`标准。
我们尽量采用短地址以节约资源。
Frame Control: 一定要采用 0x41, 0x88
Sequence Num: Place Holder
Dest PANID, Dest Addr, Src Addr: 注意字节序，如果目标地址设置成了"KI" 则要填 "IK"
FCS: 自动添加，但是需要留位置，两个0x00就行。
原则上来说，这些都是在驱动层来处理的，app层无需关心。
+---------------+--------------+------------+-----------+-----------+----------+---------+
| Frame Control | Sequence Num | Dest PANID | Dest Addr | Src Addr  |  Payload |   FCS   |
+---------------+--------------+------------+-----------+-----------+----------+---------+
| 2 Bytes       | 1 Byte       | 2 Bytes    | 2 Bytes   | 2 Bytes   | Var Bytes| 2 Bytes |
+---------------+--------------+------------+-----------+-----------+----------+---------+
| 0x41, 0x88    | 0x00         | 0xDECA     | "YU"      | "KI"      | "Hello!" |   Auto  |
+---------------+--------------+------------+-----------+-----------+----------+---------+

0x01 测距以及Beacon

不需要填len
Payload为空则为广播。
若Payload为目标PANID以及MAC地址，则单播。(尚未实现)

接收到的下位机到PC的Payload消息格式:
+------------+---------------+
|    Addr    |   Distance    |
+------------+---------------+
|  2 Bytes   |   4 bytes     |
+------------+---------------+
| 0xCA, 0xDE | 低字节在前(int)|
+------------+---------------+

由于下位机的浮点数处理有点问题，此处得到的距离并不是真正的距离。
真实距离 = distance / 2.0 * 1.0 / 499.2e6 / 128.0 * 299702547

0x02 设置MAC地址以及PANID

Payload 格式:
+------------+-----------+
|    PANID   |   Addr    |
+------------+-----------+
|  2 Bytes   |  2 Bytes  |
+------------+-----------+
| 0xCA, 0xDE | 'U', 'Y'  |
+------------+-----------+

0x03 RESET:
尚未实现

0x04 Auto Beacon:

Payload 格式:
+------------+
|   Enable   |
+------------+
|  1 Bytes   |
+------------+
|0x00 or 0x01|
+------------+

~~~

## 无线通信数据包格式
基础格式参照`IEEE 802.15.4a`标准

我们尽量采用短地址
~~~
802.15.4a Frame:
+---------------+--------------+------------+-----------+-----------+----------+-----------+---------+
| Frame Control | Sequence Num | Dest PANID | Dest Addr | Src PANID | Src Addr |  Payload  |   FCS   |
+---------------+--------------+------------+-----------+-----------+----------+-----------+---------+
| 2 Bytes       | 1 Byte       | 2 Bytes    | 8 Bytes   | 2 Bytes   | 8 Bytes  | Var Bytes | 2 Bytes |
+---------------+--------------+------------+-----------+-----------+----------+-----------+---------+

Frame Control:
+------+---+---+---+----------+---------+--------+----------+---+---+---+-----+-----+----+----+-----+----+
| Bits | 0 | 1 | 2 |    3     |    4    |   5    |    6     | 7 | 8 | 9 | 10  | 11  | 12 | 13 | 14  | 15 |
+------+---+---+---+----------+---------+--------+----------+---+---+---+-----+-----+----+----+-----+----+
|      | Frame     | Security | Frame   | ACK    | PANID    | Reserved  | Dest Addr | Frame   | Src Addr |
|      | Type      | Enabled  | Pending | Requst | Compress |           | Mode      | Version | Mode     |
+------+-----------+----------+---------+--------+----------+-----------+-----------+---------+----------+
1. Frame Type
        100 - Location Service (802.15.4a Reserved).
                Then the first byte of Payload is used to identify the LS message type.
                0x00 - LS Req
                        Request for Location Service.
                0x01 - LS ACK
                        ACK for LS Req, and mark the receive time of LS Req(T_Req) and the sent time of LS ACK(T_ACK).
                0x02 - LS Data
                        Return T_ACK - T_Req.
                0x03 - LS Information Return
                        Return the distance data to the ACK node.
                0x04 - LS Forward
                        Forward the Location data to a specific node.
2. Other Fields
        Please reference 802.15.4a.
~~~

## API
参见decadriver手册

## 算法
已知三个锚点ABC的位置，以及未知点X到ABC三点的距离。由于UWB测距精度十分高，我们直接采用所测距离，以ABC三点为底面，X为顶点构成四面体。
由海伦-秦九昭公式可以轻松得到这个四面体的体积，进而再求出X到ABC底面的高度。这样就能得到X的相对位置。

## TODO
RESET 指令。

回复POLL的时候，定时发送需要加一个随机数，否则容易发生帧碰撞。
