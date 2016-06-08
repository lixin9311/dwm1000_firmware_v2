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

## 与上位机通信的数据包格式
还没实现

Baudrate 115200

~~~
CMD/LOC
+-------+---------------+---------------+----------------+----------------+
| Bytes |       0       |       1       |     PARAMS     |       N        |
+-------+---------------+---------------+----------------+----------------+
|  def  |      TYPE     |    SUBTYPE    |                |     CRC8       |
+-------+---------------+---------------+----------------+----------------+

MSG
+-------+---------------+---------------+---------------------------+
| Bytes |       0       |       1       |           2 - 5           |
+-------+---------------+---------------+---------------------------+
|  def  |      TYPE     |      LEN      |        SEQ (IN BYTE)      |
+-------+---------------+---------------+---------------------------+
+-------+----------------------+----------------------+
| Bytes |        6 - 13        |       14 - 21        |
+-------+----------------------+----------------------+
|  def  |      SRC ADDRESS     |     DST ADDRESS      |
+-------+----------------------+----------------------+
+-------+---------------------+---------------+----------------+
| Bytes |     22 -  85        |      86       |       87       |
+-------+---------------------+---------------+----------------+
|  def  |    Payload max(64)  |     CRC16     |      CRC16     |
+-------+---------------------+---------------+----------------+

MSG TOTAL LENGTH MAX 88 (0 - 87)
 1. Frame Type
        //00 - RES
        01 - Message
                Host to Controller(H2C)
                        The payload carries the raw message to sent, see raw_write().
                Controller to Host(C2H)
                        The payload carries the raw message received, see raw_read().
        10 - Distance / Location poll Trigger the Location Service.
             On
             Off
             Calibration

        11 - Command
             Reboot.
             Write Reg.
             H2C - Read Reg.
             C2H - Return the Read Reg Result.
             Set log level?
 2. Packet Length
        Total length of all Payloads in a sequence in Unsigned 8 bits Integer.
 3. CRC
        CRC16 of the frame.
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
