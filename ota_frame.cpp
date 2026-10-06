#include "ota_frame.h"

namespace Ota {

quint16 crc16Ccitt(const uchar *data, int len)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < len; ++i) {
        crc ^= quint16(data[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
    }
    return crc;
}

static QByteArray buildRequestFrame(Cmd cmd, Address addr, quint16 id, uint8_t length,
                                    const QByteArray &data256)
{
    QByteArray f;
    f.reserve(kRequestFrameSize);
    f.append(char(kHeaderByte1)).append(char(kHeaderByte2));
    f.append(char(uint8_t(cmd))).append(char(uint8_t(cmd)));
    f.append(char(addr.motherboard)).append(char(addr.trb));
    f.append(char(id >> 8)).append(char(id & 0xFF));
    f.append(char(length));
    f.append(data256);

    // CRC tinh tren Addr+ID+Length+Data, khong gom Header/CMD/Ender
    const auto *body = reinterpret_cast<const uchar *>(f.constData()) + 4;
    quint16 crc = crc16Ccitt(body, 2 + 2 + 1 + kDataChunkSize);
    f.append(char(crc >> 8)).append(char(crc & 0xFF));
    f.append(char(kEnderByte1)).append(char(kEnderByte2));
    return f;
}

QByteArray buildEraseFlash(Address addr)
{
    // ID, Length khong dung o ban tin nay -> de 0; Data khong dung -> fill FF
    return buildRequestFrame(Cmd::EraseFlash, addr, 0, 0, QByteArray(kDataChunkSize, char(0xFF)));
}

QByteArray buildLoadCode(Address addr, quint16 packetId, const QByteArray &chunk)
{
    Q_ASSERT(chunk.size() > 0 && chunk.size() <= kDataChunkSize);
    // Quy uoc Length = so byte thuc dung - 1 (gia tri 0-255 tuong ung 1-256 byte du lieu)
    uint8_t length = uint8_t(chunk.size() - 1);
    QByteArray data = chunk;
    if (data.size() < kDataChunkSize)
        data.append(QByteArray(kDataChunkSize - data.size(), char(0xFF)));
    return buildRequestFrame(Cmd::LoadCode, addr, packetId, length, data);
}

QByteArray buildStatusQuery(Address addr)
{
    return buildRequestFrame(Cmd::StatusQuery, addr, 0, 0, QByteArray(kDataChunkSize, char(0xFF)));
}

QByteArray buildBootRequest(Address addr)
{
    return buildRequestFrame(Cmd::BootRequest, addr, 0, 0, QByteArray(kDataChunkSize, char(0xFF)));
}

bool parseStatusReply(const QByteArray &raw, StatusReply *out)
{
    if (raw.size() != kReplyFrameSize || !out)
        return false;

    const auto *d = reinterpret_cast<const uchar *>(raw.constData());
    if (d[0] != kHeaderByte1 || d[1] != kHeaderByte2)
        return false;
    if (d[2] != uint8_t(Cmd::StatusReply) || d[3] != uint8_t(Cmd::StatusReply))
        return false;
    if (d[23] != kEnderByte1 || d[24] != kEnderByte2)
        return false;

    quint16 crcCalc = crc16Ccitt(d + 4, 17); // Ack+CODE+STAT+BOOTSTS+WBSTAR+Rsv
    quint16 crcRecv = quint16(d[21] << 8 | d[22]);
    if (crcCalc != crcRecv)
        return false;

    out->ack  = d[4];
    out->code = d[5];
    out->stat    = quint32(d[6])  << 24 | quint32(d[7])  << 16 | quint32(d[8])  << 8 | d[9];
    out->bootsts = quint32(d[10]) << 24 | quint32(d[11]) << 16 | quint32(d[12]) << 8 | d[13];
    out->wbstar  = quint32(d[14]) << 24 | quint32(d[15]) << 16 | quint32(d[16]) << 8 | d[17];
    return true;
}

QString addressToString(Address a)
{
    return QStringLiteral("%1:%2")
    .arg(a.motherboard, 2, 16, QChar('0'))
        .arg(a.trb, 2, 16, QChar('0'));
}

} // namespace Ota