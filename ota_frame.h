#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <cstdint>

// Lop giao thuc ban tin RS485 cho OTA FPGA (theo tai lieu dac ta ban tin)
namespace Ota {

enum class Cmd : uint8_t {
    LoadCode    = 0x55,
    EraseFlash  = 0x66,
    StatusQuery = 0x88,
    StatusReply = 0x99,
    BootRequest = 0xAA,
};

constexpr uint8_t kHeaderByte1 = 0xAB;
constexpr uint8_t kHeaderByte2 = 0xCD;
constexpr uint8_t kEnderByte1  = 0xE1;
constexpr uint8_t kEnderByte2  = 0xE2;

constexpr int kDataChunkSize    = 256;
constexpr int kRequestFrameSize = 269; // Header2+Addr2+CMD2... (xem buildRequestFrame)
constexpr int kReplyFrameSize   = 25;

// Dia chi thuc 2 byte: byte1 = dia chi motherboard, byte2 = dia chi TRB tren motherboard do.
// Mot he thong co the co nhieu motherboard, moi motherboard co nhieu TRB.
struct Address {
    uint8_t motherboard = 0;
    uint8_t trb = 0;

    bool operator==(const Address &o) const { return motherboard == o.motherboard && trb == o.trb; }
    bool operator!=(const Address &o) const { return !(*this == o); }
};

constexpr Address kBroadcastAddress{0xFF, 0xFF};

// Bit loi trong truong CODE cua ban tin tra loi 0x99 (1 = loi, 0 = on)
enum ErrorBit : uint8_t {
    ErrUartFrame   = 1u << 0, // loi khung UART (Header/Ender/CRC) cua ban tin gan nhat
    ErrFlashWrite  = 1u << 1, // loi ghi/verify Flash
    ErrFlashErase  = 1u << 2, // loi xoa Flash
    ErrHwicapRead  = 1u << 3, // loi doc HWICAP
    ErrIprogReboot = 1u << 4, // loi lenh IPROG reboot
};

struct StatusReply {
    uint8_t ack     = 0; // "Xac nhan loi/khong loi MB1": bit0=loi, bit1=tot
    uint8_t code    = 0; // bitmask ErrorBit
    quint32 stat    = 0; // raw, hien thi hex trong giai doan bring-up
    quint32 bootsts = 0;
    quint32 wbstar  = 0;

    bool hasError(ErrorBit bit) const { return code & bit; }
};

// CRC16-CCITT (CCITT-FALSE): poly 0x1021, init 0xFFFF, khong reflect
quint16 crc16Ccitt(const uchar *data, int len);

// Xay dung 4 loai ban tin PC -> FPGA. addr = Ota::kBroadcastAddress de broadcast,
// hoac dia chi (motherboard, trb) cu the de unicast (vi du gui lai goi loi).
QByteArray buildEraseFlash(Address addr);
QByteArray buildLoadCode(Address addr, quint16 packetId, const QByteArray &chunk);
QByteArray buildStatusQuery(Address addr);
QByteArray buildBootRequest(Address addr);

// Phan tich ban tin FPGA -> PC (CMD 0x99). Tra ve false neu sai header/ender/crc/co size.
bool parseStatusReply(const QByteArray &raw, StatusReply *out);

// Dinh dang hien thi "mb:trb" (so nguyen thap phan 0-255 moi phan), dung chung cho log va bang UI.
QString addressToString(Address a);

} // namespace Ota